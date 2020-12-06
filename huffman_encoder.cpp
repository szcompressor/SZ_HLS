#include "huffman_encoder.h"

namespace huf {

void HistogramMap(hls::stream<CodeT>& quant_code_stream, uint32_t hist[1024]) {
#pragma HLS DEPENDENCE variable=hist intra RAW false

    for(uint32_t i = 0; i < 1024; i++) {
    #pragma HLS PIPELINE II=1
    // #pragma HLS UNROLL  
        hist[i] = 0;
    }

    CodeT old = quant_code_stream.read();
    uint32_t acc = 0;
    for(uint32_t i = 1; i < kEngSize / 2; i++) {
    #pragma HLS PIPELINE II=1
        CodeT val = quant_code_stream.read();
        if(old == val) {
            acc = acc + 1;
        } else {
            hist[old] = acc;
            acc = hist[val] + 1;
        }
            old = val;
    }

        hist[old] = acc;
}

void HistogramReduce(uint32_t hist0[1024], uint32_t hist1[1024], hls::stream<uint32_t>& freq_stream) {
    // std::ofstream o_file0;
    // o_file0.open("C:\\Users\\Bizon\\Desktop\\sz_hls1\\inter_data\\code_freq.txt");

    for(uint16_t i = 0; i < 1024; i++) {
    #pragma HLS PIPELINE II=1
    // #pragma HLS UNROLL  
        freq_stream << (hist0[i] + hist1[i]);
        // o_file0 << (hist0[i] + hist1[i]) << "\n";
    }

    // o_file0.close();
}

//Top level function
void QuantCodeFrequency1(hls::stream<CodeT> quant_code_stream[kNumHists], hls::stream<uint32_t>& freq_stream){

#pragma HLS DATAFLOW
    uint32_t hist0[1024];
    uint32_t hist1[1024];
    // #pragma HLS ARRAY_PARTITION variable = hist0 dim = 1 complete
    // #pragma HLS ARRAY_PARTITION variable = hist1 dim = 1 complete
    #pragma HLS RESOURCE variable=hist0 core=RAM_T2P_BRAM 
    #pragma HLS RESOURCE variable=hist1 core=RAM_T2P_BRAM 

    HistogramMap(quant_code_stream[0], hist0);
    HistogramMap(quant_code_stream[1], hist1);
    HistogramReduce(hist0, hist1, freq_stream);
}

void QuantCodeFrequency0(
    hls::stream<CodeT> quant_code_stream[kNumEngs],
    uint32_t quant_code_size,
    uint32_t eng_blks[kNumEngs],
    hls::stream<uint32_t>& freq_stream) {

    // std::ofstream o_file0, o_file1;
    // o_file0.open("C:\\Users\\Bizon\\Desktop\\sz_hls1\\inter_data\\code_freq.txt");
    // o_file1.open("C:\\Users\\Bizon\\Desktop\\sz_hls1\\inter_data\\code_idx.txt");

    if (quant_code_size == 0) {
        freq_stream << 9999;
        return;
    }

    uint32_t dyn_code_tree[kCodeTreeSize];
    #pragma HLS ARRAY_PARTITION variable = dyn_code_tree dim = 1 complete

code_tree_init:
    for (uint16_t i = 0; i < kCodeTreeSize; i++) {
    #pragma HLS UNROLL
        dyn_code_tree[i] = 0;
    }

    // uint32_t idx[kNumEngs] = {0, 0, 0, 0};
    uint32_t idx[1] = {0};
code_histogram:
    for (uint32_t j = 0; j < eng_blks[0] * kBlkSize; j++) {
#pragma HLS LOOP_TRIPCOUNT min = 1048576 max = 1048576
#pragma HLS PIPELINE II = 1
// #pragma HLS dependence variable = dyn_code_tree inter false

        for (uint8_t i = 0; i < kNumEngs; i++) {
            if (idx[i] < eng_blks[i] * kBlkSize)
            {
                CodeT code = quant_code_stream[i].read();
                dyn_code_tree[code]++;  
                // if (code == 438) {
                //     o_file1 << j << "\n";
                // }          
            }
            idx[i]++;
        }
    }

    for (uint16_t i = 0; i < kCodeTreeSize; i++) 
    {
        freq_stream << dyn_code_tree[i];
        // o_file0 << dyn_code_tree[i] << "\n";
    }
    // o_file0.close();
    // o_file1.close();
}

void Filter(hls::stream<uint32_t>& freq_stream,
            Symbol* heap,
            uint16_t* heap_length) {
    uint16_t heap_len = 0;

filter_loop:
    for (uint16_t n = 0; n < kSymbolSize ; ++n) { 
#pragma HLS PIPELINE II = 1
#pragma HLS LOOP_TRIPCOUNT min = kSymbolSize max = kSymbolSize 
        heap[n].value = 0;
        heap[n].frequency = 0;
        Frequency freq = freq_stream.read();
        if (n == 1048) {
            heap[heap_len].value = n;
            heap[heap_len].frequency = 1;
            ++heap_len;
        } else if (freq != 0) {
            heap[heap_len].value = n;
            heap[heap_len].frequency = freq;
            ++heap_len;
        } 
    }

    heap_length[0] = heap_len;
}

void RadixSort(Symbol* heap, uint16_t heap_length) {
    Symbol prev_sorting[kSymbolSize];
    Digit current_digit[kSymbolSize];

    ap_uint<kSymbolBits> digit_histogram[RADIX], digit_location[RADIX];
#pragma HLS ARRAY_PARTITION variable = digit_location complete dim = 1
#pragma HLS ARRAY_PARTITION variable = digit_histogram complete dim = 1

radix_sort:
    for (uint8_t shift = 0; shift < 32; shift += BITS_PER_LOOP) {
#pragma HLS LOOP_TRIPCOUNT min = 8 max = 8 avg = 8
    init_histogram:
        for (uint8_t i = 0; i < RADIX; ++i) {
#pragma HLS LOOP_TRIPCOUNT min = 16 max = 16 avg = 16
#pragma HLS PIPELINE II = 1
            digit_histogram[i] = 0;
        }

    compute_histogram:
        for (uint16_t j = 0; j < heap_length; ++j) {
#pragma HLS LOOP_TRIPCOUNT min = 32 max = 1024
#pragma HLS PIPELINE II = 1
            Symbol val = heap[j];
            Digit digit = (val.frequency >> shift) & (RADIX - 1);
            current_digit[j] = digit;
            ++digit_histogram[digit];
            prev_sorting[j] = val;
        }
        digit_location[0] = 0;

    find_digit_location:
        for (uint8_t i = 0; i < RADIX - 1; ++i) {
#pragma HLS LOOP_TRIPCOUNT min = 16 max = 16 avg = 16
#pragma HLS PIPELINE II = 1
            digit_location[i + 1] = digit_location[i] + digit_histogram[i];
        }

    re_sort:
        for (uint16_t j = 0; j < heap_length; ++j) {
#pragma HLS LOOP_TRIPCOUNT min = 32 max = 1024
#pragma HLS PIPELINE II = 1
            Digit digit = current_digit[j];
            heap[digit_location[digit]] = prev_sorting[j];
            ++digit_location[digit];
        }
    }
}

void CreateTree(Symbol* heap,
                uint16_t num_symbols,
                ap_uint<kSymbolBits>* parent,
                ap_uint<kSymbolSize>& left,
                ap_uint<kSymbolSize>& right,
                Frequency* frequency) {
    ap_uint<kSymbolBits> tree_count = 0; // Number of intermediate node assigned a parent
    ap_uint<kSymbolBits> in_count = 0;   // Number of inputs consumed
    ap_uint<kSymbolSize> tmp;
    left = 0;
    right = 0;

    // for case with less number of symbols
    if (num_symbols < 3) num_symbols++;
// this loop needs to run at least twice
create_heap:
    for (uint16_t i = 0; i < num_symbols; ++i) {
#pragma HLS PIPELINE II = 3
#pragma HLS LOOP_TRIPCOUNT min = 32 avg = 512 max = 1024
        Frequency node_freq = 0;
        Frequency intermediate_freq = frequency[tree_count];
        Symbol s = heap[in_count];
        tmp = 1;
        tmp <<= i;

        if ((in_count < num_symbols && s.frequency <= intermediate_freq) || tree_count == i) {
            // Pick symbol from heap
            // left[i] = s.value;    // set input symbol value as left node
            node_freq = s.frequency; // Add symbol frequency to total node frequency
            // move to the next input symbol
            ++in_count;
        } else {
            // pick internal node without a parent
            // left[i] = INTERNAL_NODE;    // Set symbol to indicate an internal node
            left |= tmp;
            node_freq = intermediate_freq; // Add child node frequency
            parent[tree_count] = i;        // Set this node as child's parent
            // Go to next parent-less internal node
            ++tree_count;
        }

        intermediate_freq = frequency[tree_count];
        s = heap[in_count];
        if ((in_count < num_symbols && s.frequency <= intermediate_freq) || tree_count == i) {
            // Pick symbol from heap
            // right[i] = s.value;
            frequency[i] = node_freq + s.frequency;
            ++in_count;
        } else {
            // Pick internal node without a parent
            // right[i] = INTERNAL_NODE;
            right |= tmp;
            frequency[i] = node_freq + intermediate_freq;
            parent[tree_count] = i;
            ++tree_count;
        }
    }
}

void ComputeBitsLength(ap_uint<kSymbolBits>* parent,
                    ap_uint<kSymbolSize>& left,
                    ap_uint<kSymbolSize>& right,
                    uint16_t num_symbols,
                    Histogram* length_histogram,
                    Frequency* child_depth) {
    ap_uint<kSymbolSize> tmp;
    // for case with less number of symbols
    if (num_symbols < 2) num_symbols++;
    // Depth of the root node is 0.
    child_depth[num_symbols - 1] = 0;

// this loop needs to run at least once
traverse_tree:
    for (int16_t i = num_symbols - 2; i >= 0; --i) {
#pragma HLS LOOP_TRIPCOUNT min = 512 max = 1024
#pragma HLS pipeline II = 2
        tmp = 1;
        tmp <<= i;
        uint32_t length = child_depth[parent[i]] + 1;
        child_depth[i] = length;
        bool is_left_internal = ((left & tmp) == 0);
        bool is_right_internal = ((right & tmp) == 0);

        if ((is_left_internal || is_right_internal)) {
            uint16_t children = 1; // One child of the original node was a symbol
            if (is_left_internal && is_right_internal) {
                children = 2; // Both the children of the original node were symbols
            }
            length_histogram[length] += children;
        }
    }
}

void TruncateTree(Histogram* length_histogram, uint16_t tree_depth) {
    uint16_t j = kMaxBits;
move_nodes:
    for (uint16_t i = tree_depth - 1; i > kMaxBits; --i) {
#pragma HLS LOOP_TRIPCOUNT min = 32 max = 64 avg = 32
#pragma HLS PIPELINE II = 1
        // Look to see if there are any nodes at lengths greater than target depth
        uint16_t cnt = 0;
    reorder:
        for (; length_histogram[i] != 0;) {
#pragma HLS LOOP_TRIPCOUNT min = 3 max = 3 avg = 3
            if (j == kMaxBits) {
                // find the deepest leaf with codeword length < target depth
                --j;
                while (length_histogram[j] == 0) {
#pragma HLS LOOP_TRIPCOUNT min = 1 max = 1 avg = 1
                    --j;
                }
            }
            // Move leaf with depth i to depth j + 1
            length_histogram[j] -= 1;     // The node at level j is no longer a leaf
            length_histogram[j + 1] += 2; // Two new leaf nodes are attached at level j+1
            length_histogram[i - 1] += 1; // The leaf node at level i+1 gets attached here
            length_histogram[i] -= 2;     // Two leaf nodes have been lost from  level i

            // now deepest leaf with codeword length < target length
            // is at level (j+1) unless (j+1) == target length
            ++j;
        }
    }
}

void CanonizeTree(Symbol* heap, uint16_t num_symbols, Histogram* length_histogram, 
    uint16_t* huff_bits_length, uint16_t tree_depth) {
    int16_t length = tree_depth;
    Histogram count = 0;
// Iterate across the symbols from lowest frequency to highest
// Assign them largest bits length to smallest
process_symbols:
    for (uint16_t k = 0; k < num_symbols; ++k) {
#pragma HLS LOOP_TRIPCOUNT max = 1024 min = 1024 avg = 1024
        if (count == 0) {
            // find the next non-zero bits length k
            count = length_histogram[--length];
            while (count == 0 && length >= 0) {
#pragma HLS LOOP_TRIPCOUNT min = 1 avg = 1 max = 2
#pragma HLS PIPELINE II = 1
                // n is the number of symbols with encoded length k
                count = length_histogram[--length];
            }
        }
        if (length < 0) break;
        huff_bits_length[heap[k].value] = length; // assign symbol k to have length bits
        --count;                                  // keep assigning i bits until we have counted off n symbols
    }
}

void CreateCodeword(uint16_t* huff_bits_length,
                    Histogram* length_histogram,
                    hls::stream<Codeword>& huff_code_stream) {
    //#pragma HLS inline
    ap_uint<kMaxBits> first_codeword[kMaxBits + 1];
#pragma HLS ARRAY_PARTITION variable = first_codeword complete dim = 1

    // Computes the initial codeword value for a symbol with bits length i
    first_codeword[0] = 0;
first_codewords:
    for (uint16_t i = 1; i <= kMaxBits; ++i) {
#pragma HLS LOOP_TRIPCOUNT min = 8 max = 32
#pragma HLS PIPELINE II = 1
        first_codeword[i] = (first_codeword[i - 1] + length_histogram[i - 1]) << 1;
    }

    Codeword code;
assign_codewords:
    for (uint16_t k = 0; k < kSymbolSize ; ++k) {
#pragma HLS LOOP_TRIPCOUNT max = 1024 min = 1024 avg = 1024
#pragma HLS PIPELINE II = 1

        uint8_t length = (uint8_t)huff_bits_length[k];
    // if symbol has 0 bits, it doesn't need to be encoded
    make_codeword:
        if (length != 0) {
            ap_uint<kMaxBits> out_reversed = first_codeword[length];
            out_reversed.reverse();
            out_reversed = out_reversed >> (kMaxBits - length);

            code.codeword = (uint32_t)out_reversed;
            code.code_length = length;
            first_codeword[length]++;
        } else {
            code.codeword = 0;
            code.code_length = 0;
        }

        huff_code_stream << code;

    // std::ofstream o_file0;
    // o_file0.open("C:\\Users\\Bizon\\Desktop\\sz_hls1\\inter_data\\codeword.txt");
    // o_file0 << std::bitset<sizeof(unsigned int)*8>(code.codeword) << "\n";
    }
}

void InitBuffers(ap_uint<kSymbolBits>* parent, Frequency* inter_freq, Histogram* length_histogram, uint16_t* huff_bits_length) {
    init:
    for (uint16_t i = 0; i < kSymbolSize ; ++i) {
#pragma HLS LOOP_TRIPCOUNT min = 32 max = 1024
#pragma HLS PIPELINE II = 1

        parent[i] = 0;
        inter_freq[i] = 0;
        length_histogram[i] = 0;
        huff_bits_length[i] = 0;
        if (i < kLenHistogramSize) {
            length_histogram[i] = 0; 
        }
    }  
}

void HuffConstructTreeStream(hls::stream<Frequency>& freq_stream,
                             hls::stream<Codeword>& huff_code_stream) {
    //#pragma HLS inline
    // construct huffman tree and generate huffman codes

    // #pragma HLS DATAFLOW

    // internal buffers
    Symbol heap[kSymbolSize];
    ap_uint<kSymbolSize> left = 0;
    ap_uint<kSymbolSize> right = 0;
    ap_uint<kSymbolBits> parent[kSymbolSize];
    Frequency inter_freq[kSymbolSize];
    Histogram length_histogram[kLenHistogramSize]; 
    uint16_t huff_bits_length[kSymbolSize];

// #pragma HLS ARRAY_PARTITION variable = heap dim = 1 complete
// #pragma HLS ARRAY_PARTITION variable = parent dim = 1 complete
// #pragma HLS ARRAY_PARTITION variable = inter_freq dim = 1 complete
// #pragma HLS ARRAY_PARTITION variable = length_histogram dim = 1 complete
// #pragma HLS resource variable=inter_freq core=RAM_2P_BRAM
// #pragma HLS ARRAY_PARTITION variable = huff_bits_length dim = 1 complete

    uint16_t heap_length = 0;

    InitBuffers(parent, inter_freq, length_histogram, huff_bits_length);

    // filter the input
    Filter(freq_stream, heap, &heap_length);

    // sort the input
    RadixSort(heap, heap_length);

    // create tree
    CreateTree(heap, heap_length, parent, left, right, inter_freq);

    // get bit-lengths from tree
    ComputeBitsLength(parent, left, right, heap_length, length_histogram, inter_freq);

    // truncate tree for any bigger bit-lengths
    // truncateTree(length_histogram, kMaxBits);

    // canonize the tree
    CanonizeTree(heap, heap_length, length_histogram, huff_bits_length, kMaxBits);

    // generate huffman codewords
    CreateCodeword(huff_bits_length, length_histogram, huff_code_stream);
}

}
