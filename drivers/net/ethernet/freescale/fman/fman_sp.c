/*
 * Copyright 2008 - 2015 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
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

#include "fman_sp.h"
#include "fman.h"

void fman_sp_set_buf_pools_in_asc_order_of_buf_sizes(struct fman_ext_pools
						     *fm_ext_pools,
						     u8 *ordered_array,
						     u16 *sizes_array)
{
	u16 buf_size = 0;
	int i = 0, j = 0, k = 0;

	/* First we copy the external buffers pools information
	 * to an ordered local array
	 */
	for (i = 0; i < fm_ext_pools->num_of_pools_used; i++) {
		/* get pool size */
		buf_size = fm_ext_pools->ext_buf_pool[i].size;

		/* keep sizes in an array according to poolId
		 * for direct access
		 */
		sizes_array[fm_ext_pools->ext_buf_pool[i].id] = buf_size;

		/* save poolId in an ordered array according to size */
		for (j = 0; j <= i; j++) {
			/* this is the next free place in the array */
			if (j == i)
				ordered_array[i] =
				    fm_ext_pools->ext_buf_pool[i].id;
			else {
				/* find the right place for this poolId */
				if (buf_size < sizes_array[ordered_array[j]]) {
					/* move the pool_ids one place ahead
					 * to make room for this poolId
					 */
					for (k = i; k > j; k--)
						ordered_array[k] =
						    ordered_array[k - 1];

					/* now k==j, this is the place for
					 * the new size
					 */
					ordered_array[k] =
					    fm_ext_pools->ext_buf_pool[i].id;
					break;
				}
			}
		}
	}
}

int fman_sp_build_buffer_struct(struct fman_sp_int_context_data_copy *
				int_context_data_copy,
				struct fman_buffer_prefix_content *
				buffer_prefix_content,
				struct fman_sp_buf_margins *buf_margins,
				struct fman_sp_buffer_offsets *buffer_offsets,
				u8 *internal_buf_offset)
{
	u32 tmp;

	/* Align start of internal context data to 16 byte */
	int_context_data_copy->ext_buf_offset = (u16)
		((buffer_prefix_content->priv_data_size & (OFFSET_UNITS - 1)) ?
		((buffer_prefix_content->priv_data_size + OFFSET_UNITS) &
			~(u16)(OFFSET_UNITS - 1)) :
		buffer_prefix_content->priv_data_size);

	/* Translate margin and int_context params to FM parameters */
	/* Initialize with illegal value. Later we'll set legal values. */
	buffer_offsets->prs_result_offset = (u32)ILLEGAL_BASE;
	buffer_offsets->time_stamp_offset = (u32)ILLEGAL_BASE;
	buffer_offsets->hash_result_offset = (u32)ILLEGAL_BASE;

	/* Internally the driver supports 4 options
	 * 1. prsResult/timestamp/hashResult selection (in fact 8 options,
	 * but for simplicity we'll
	 * relate to it as 1).
	 * 2. All IC context (from AD) not including debug.
	 */

	/* This case covers the options under 1 */
	/* Copy size must be in 16-byte granularity. */
	int_context_data_copy->size =
	    (u16)((buffer_prefix_content->pass_prs_result ? 32 : 0) +
		  ((buffer_prefix_content->pass_time_stamp ||
		  buffer_prefix_content->pass_hash_result) ? 16 : 0));

	/* Align start of internal context data to 16 byte */
	int_context_data_copy->int_context_offset =
	    (u8)(buffer_prefix_content->pass_prs_result ? 32 :
		 ((buffer_prefix_content->pass_time_stamp ||
		 buffer_prefix_content->pass_hash_result) ? 64 : 0));

	if (buffer_prefix_content->pass_prs_result)
		buffer_offsets->prs_result_offset =
		    int_context_data_copy->ext_buf_offset;
	if (buffer_prefix_content->pass_time_stamp)
		buffer_offsets->time_stamp_offset =
		    buffer_prefix_content->pass_prs_result ?
		    (int_context_data_copy->ext_buf_offset +
			sizeof(struct fman_prs_result)) :
		    int_context_data_copy->ext_buf_offset;
	if (buffer_prefix_content->pass_hash_result)
		/* If PR is not requested, whether TS is
		 * requested or not, IC will be copied from TS
			 */
		buffer_offsets->hash_result_offset =
		buffer_prefix_content->pass_prs_result ?
			(int_context_data_copy->ext_buf_offset +
				sizeof(struct fman_prs_result) + 8) :
			int_context_data_copy->ext_buf_offset + 8;

	if (int_context_data_copy->size)
		buf_margins->start_margins =
		    (u16)(int_context_data_copy->ext_buf_offset +
			  int_context_data_copy->size);
	else
		/* No Internal Context passing, STartMargin is
		 * immediately after private_info
		 */
		buf_margins->start_margins =
		    buffer_prefix_content->priv_data_size;

	/* align data start */
	tmp = (u32)(buf_margins->start_margins %
		    buffer_prefix_content->data_align);
	if (tmp)
		buf_margins->start_margins +=
		    (buffer_prefix_content->data_align - tmp);
	buffer_offsets->data_offset = buf_margins->start_margins;

	return 0;
}
