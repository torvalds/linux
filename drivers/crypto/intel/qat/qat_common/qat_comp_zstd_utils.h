/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2026 Intel Corporation */
#ifndef QAT_COMP_ZSTD_UTILS_H_
#define QAT_COMP_ZSTD_UTILS_H_
#include <linux/zstd_lib.h>

#define QAT_ZSTD_LIT_COPY_LEN	8

int qat_alg_dec_lz4s(ZSTD_Sequence *out_seqs, size_t out_seqs_capacity,
		     unsigned char *lz4s_buff, unsigned int lz4s_buff_size,
		     unsigned char *literals, unsigned int *lit_len);

#endif
