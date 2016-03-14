/*
 * Copyright 2008 - 2015 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __FM_SP_H
#define __FM_SP_H

#include "fman.h"
#include <linux/types.h>

#define ILLEGAL_BASE    (~0)

/* defaults */
#define DFLT_FM_SP_BUFFER_PREFIX_CONTEXT_DATA_ALIGN	64

/* Registers bit fields */
#define FMAN_SP_EXT_BUF_POOL_EN_COUNTER		0x40000000
#define FMAN_SP_EXT_BUF_POOL_VALID			0x80000000
#define FMAN_SP_EXT_BUF_POOL_BACKUP			0x20000000
#define FMAN_SP_DMA_ATTR_WRITE_OPTIMIZE		0x00100000
#define FMAN_SP_SG_DISABLE				0x80000000

/* shifts */
#define FMAN_SP_EXT_BUF_MARG_START_SHIFT		16
#define FMAN_SP_DMA_ATTR_SWP_SHIFT			30
#define FMAN_SP_IC_TO_EXT_SHIFT			16
#define FMAN_SP_IC_FROM_INT_SHIFT			8

/* structure for defining internal context copying */
struct fman_sp_int_context_data_copy {
	/* < Offset in External buffer to which internal
	 *  context is copied to (Rx) or taken from (Tx, Op).
	 */
	u16 ext_buf_offset;
	/* Offset within internal context to copy from
	 * (Rx) or to copy to (Tx, Op).
	 */
	u8 int_context_offset;
	/* Internal offset size to be copied */
	u16 size;
};

/*  struct for defining external buffer margins */
struct fman_sp_buf_margins {
	/* Number of bytes to be left at the beginning
	 * of the external buffer (must be divisible by 16)
	 */
	u16 start_margins;
	/* number of bytes to be left at the end
	 * of the external buffer(must be divisible by 16)
	 */
	u16 end_margins;
};

struct fman_sp_buffer_offsets {
	u32 data_offset;
	u32 prs_result_offset;
	u32 time_stamp_offset;
	u32 hash_result_offset;
};

int fman_sp_build_buffer_struct(struct fman_sp_int_context_data_copy
				*int_context_data_copy,
				struct fman_buffer_prefix_content
				*buffer_prefix_content,
				struct fman_sp_buf_margins *buf_margins,
				struct fman_sp_buffer_offsets
				*buffer_offsets,
				u8 *internal_buf_offset);

void fman_sp_set_buf_pools_in_asc_order_of_buf_sizes(struct fman_ext_pools
						     *fm_ext_pools,
						     u8 *ordered_array,
						     u16 *sizes_array);

#endif	/* __FM_SP_H */
