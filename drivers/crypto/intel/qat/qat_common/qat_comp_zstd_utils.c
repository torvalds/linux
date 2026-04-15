// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2026 Intel Corporation */
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/unaligned.h>
#include <linux/zstd.h>

#include "qat_comp_zstd_utils.h"

#define ML_BITS		4
#define ML_MASK		((1U << ML_BITS) - 1)
#define RUN_BITS	(8 - ML_BITS)
#define RUN_MASK	((1U << RUN_BITS) - 1)
#define LZ4S_MINMATCH	2

/*
 * ZSTD blocks can decompress to at most min(windowSize, 128KB) bytes.
 * Insert explicit block delimiters to keep blocks within this limit.
 */
#define QAT_ZSTD_BLOCK_MAX	ZSTD_BLOCKSIZE_MAX

static int emit_delimiter(ZSTD_Sequence *out_seqs, size_t *seqs_idx,
			  size_t out_seqs_capacity, unsigned int lz4s_buff_size)
{
	if (*seqs_idx >= out_seqs_capacity - 1) {
		pr_debug("QAT ZSTD: sequence overflow (seqs_idx:%zu, capacity:%zu, lz4s_size:%u)\n",
			 *seqs_idx, out_seqs_capacity, lz4s_buff_size);
		return -EOVERFLOW;
	}

	out_seqs[*seqs_idx].offset = 0;
	out_seqs[*seqs_idx].litLength = 0;
	out_seqs[*seqs_idx].matchLength = 0;
	(*seqs_idx)++;

	return 0;
}

int qat_alg_dec_lz4s(ZSTD_Sequence *out_seqs, size_t out_seqs_capacity,
		     unsigned char *lz4s_buff, unsigned int lz4s_buff_size,
		     unsigned char *literals, unsigned int *lit_len)
{
	unsigned char *end_ip = lz4s_buff + lz4s_buff_size;
	unsigned char *start, *dest, *dest_end;
	unsigned int hist_literal_len = 0;
	unsigned char *ip = lz4s_buff;
	size_t block_decomp_size = 0;
	size_t seqs_idx = 0;
	int ret;

	*lit_len = 0;

	if (!lz4s_buff_size)
		return 0;

	while (ip < end_ip) {
		size_t literal_len = 0, match_len = 0;
		const unsigned int token = *ip++;
		size_t length = 0;
		size_t offset = 0;

		/* Get literal length */
		length = token >> ML_BITS;
		if (length == RUN_MASK) {
			unsigned int s;

			do {
				s = *ip++;
				length += s;
			} while (s == 255);
		}

		literal_len = length;

		start = ip;
		dest = literals;
		dest_end = literals + length;

		do {
			memcpy(dest, start, QAT_ZSTD_LIT_COPY_LEN);
			dest += QAT_ZSTD_LIT_COPY_LEN;
			start += QAT_ZSTD_LIT_COPY_LEN;
		} while (dest < dest_end);

		literals += length;
		*lit_len += length;

		ip += length;
		if (ip == end_ip) {
			literal_len += hist_literal_len;
			/*
			 * If adding trailing literals would overflow the
			 * current block, close it first.
			 */
			if (block_decomp_size + literal_len > QAT_ZSTD_BLOCK_MAX) {
				ret = emit_delimiter(out_seqs, &seqs_idx,
						     out_seqs_capacity,
						     lz4s_buff_size);
				if (ret)
					return ret;
			}
			out_seqs[seqs_idx].litLength = literal_len;
			out_seqs[seqs_idx].offset = offset;
			out_seqs[seqs_idx].matchLength = match_len;
			break;
		}

		offset = get_unaligned_le16(ip);
		ip += 2;

		length = token & ML_MASK;
		if (length == ML_MASK) {
			unsigned int s;

			do {
				s = *ip++;
				length += s;
			} while (s == 255);
		}
		if (length != 0) {
			length += LZ4S_MINMATCH;
			match_len = (unsigned short)length;
			literal_len += hist_literal_len;

			/*
			 * If this sequence would push the current block past
			 * the ZSTD maximum, close the block first.
			 */
			if (block_decomp_size + literal_len + match_len > QAT_ZSTD_BLOCK_MAX) {
				ret = emit_delimiter(out_seqs, &seqs_idx,
						     out_seqs_capacity,
						     lz4s_buff_size);
				if (ret)
					return ret;

				block_decomp_size = 0;
			}

			out_seqs[seqs_idx].offset = offset;
			out_seqs[seqs_idx].litLength = literal_len;
			out_seqs[seqs_idx].matchLength = match_len;
			hist_literal_len = 0;
			seqs_idx++;
			if (seqs_idx >= out_seqs_capacity - 1) {
				pr_debug("QAT ZSTD: sequence overflow (seqs_idx:%zu, capacity:%zu, lz4s_size:%u)\n",
					 seqs_idx, out_seqs_capacity, lz4s_buff_size);
				return -EOVERFLOW;
			}

			block_decomp_size += literal_len + match_len;
		} else {
			if (literal_len > 0) {
				/*
				 * When match length is 0, the literal length needs
				 * to be temporarily stored and processed together
				 * with the next data block.
				 */
				hist_literal_len += literal_len;
			}
		}
	}

	return seqs_idx + 1;
}
