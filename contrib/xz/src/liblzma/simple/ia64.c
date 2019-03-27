///////////////////////////////////////////////////////////////////////////////
//
/// \file       ia64.c
/// \brief      Filter for IA64 (Itanium) binaries
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "simple_private.h"


static size_t
ia64_code(void *simple lzma_attribute((__unused__)),
		uint32_t now_pos, bool is_encoder,
		uint8_t *buffer, size_t size)
{
	static const uint32_t BRANCH_TABLE[32] = {
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		4, 4, 6, 6, 0, 0, 7, 7,
		4, 4, 0, 0, 4, 4, 0, 0
	};

	size_t i;
	for (i = 0; i + 16 <= size; i += 16) {
		const uint32_t instr_template = buffer[i] & 0x1F;
		const uint32_t mask = BRANCH_TABLE[instr_template];
		uint32_t bit_pos = 5;

		for (size_t slot = 0; slot < 3; ++slot, bit_pos += 41) {
			if (((mask >> slot) & 1) == 0)
				continue;

			const size_t byte_pos = (bit_pos >> 3);
			const uint32_t bit_res = bit_pos & 0x7;
			uint64_t instruction = 0;

			for (size_t j = 0; j < 6; ++j)
				instruction += (uint64_t)(
						buffer[i + j + byte_pos])
						<< (8 * j);

			uint64_t inst_norm = instruction >> bit_res;

			if (((inst_norm >> 37) & 0xF) == 0x5
					&& ((inst_norm >> 9) & 0x7) == 0
					/* &&  (inst_norm & 0x3F)== 0 */
					) {
				uint32_t src = (uint32_t)(
						(inst_norm >> 13) & 0xFFFFF);
				src |= ((inst_norm >> 36) & 1) << 20;

				src <<= 4;

				uint32_t dest;
				if (is_encoder)
					dest = now_pos + (uint32_t)(i) + src;
				else
					dest = src - (now_pos + (uint32_t)(i));

				dest >>= 4;

				inst_norm &= ~((uint64_t)(0x8FFFFF) << 13);
				inst_norm |= (uint64_t)(dest & 0xFFFFF) << 13;
				inst_norm |= (uint64_t)(dest & 0x100000)
						<< (36 - 20);

				instruction &= (1 << bit_res) - 1;
				instruction |= (inst_norm << bit_res);

				for (size_t j = 0; j < 6; j++)
					buffer[i + j + byte_pos] = (uint8_t)(
							instruction
							>> (8 * j));
			}
		}
	}

	return i;
}


static lzma_ret
ia64_coder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter_info *filters, bool is_encoder)
{
	return lzma_simple_coder_init(next, allocator, filters,
			&ia64_code, 0, 16, 16, is_encoder);
}


extern lzma_ret
lzma_simple_ia64_encoder_init(lzma_next_coder *next,
		const lzma_allocator *allocator,
		const lzma_filter_info *filters)
{
	return ia64_coder_init(next, allocator, filters, true);
}


extern lzma_ret
lzma_simple_ia64_decoder_init(lzma_next_coder *next,
		const lzma_allocator *allocator,
		const lzma_filter_info *filters)
{
	return ia64_coder_init(next, allocator, filters, false);
}
