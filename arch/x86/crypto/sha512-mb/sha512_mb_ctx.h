/*
 * Header file for multi buffer SHA512 context
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 *  Copyright(c) 2016 Intel Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU General Public License as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  Contact Information:
 *      Megha Dey <megha.dey@linux.intel.com>
 *
 *  BSD LICENSE
 *
 *  Copyright(c) 2016 Intel Corporation.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 *    * Neither the name of Intel Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SHA_MB_CTX_INTERNAL_H
#define _SHA_MB_CTX_INTERNAL_H

#include "sha512_mb_mgr.h"

#define HASH_UPDATE          0x00
#define HASH_LAST            0x01
#define HASH_DONE            0x02
#define HASH_FINAL           0x04

#define HASH_CTX_STS_IDLE       0x00
#define HASH_CTX_STS_PROCESSING 0x01
#define HASH_CTX_STS_LAST       0x02
#define HASH_CTX_STS_COMPLETE   0x04

enum hash_ctx_error {
	HASH_CTX_ERROR_NONE               =  0,
	HASH_CTX_ERROR_INVALID_FLAGS      = -1,
	HASH_CTX_ERROR_ALREADY_PROCESSING = -2,
	HASH_CTX_ERROR_ALREADY_COMPLETED  = -3,
};

#define hash_ctx_user_data(ctx)  ((ctx)->user_data)
#define hash_ctx_digest(ctx)     ((ctx)->job.result_digest)
#define hash_ctx_processing(ctx) ((ctx)->status & HASH_CTX_STS_PROCESSING)
#define hash_ctx_complete(ctx)   ((ctx)->status == HASH_CTX_STS_COMPLETE)
#define hash_ctx_status(ctx)     ((ctx)->status)
#define hash_ctx_error(ctx)      ((ctx)->error)
#define hash_ctx_init(ctx) \
	do { \
		(ctx)->error = HASH_CTX_ERROR_NONE; \
		(ctx)->status = HASH_CTX_STS_COMPLETE; \
	} while (0)

/* Hash Constants and Typedefs */
#define SHA512_DIGEST_LENGTH          8
#define SHA512_LOG2_BLOCK_SIZE        7

#define SHA512_PADLENGTHFIELD_SIZE    16

#ifdef SHA_MB_DEBUG
#define assert(expr) \
do { \
	if (unlikely(!(expr))) { \
		printk(KERN_ERR "Assertion failed! %s,%s,%s,line=%d\n", \
		#expr, __FILE__, __func__, __LINE__); \
	} \
} while (0)
#else
#define assert(expr) do {} while (0)
#endif

struct sha512_ctx_mgr {
	struct sha512_mb_mgr mgr;
};

/* typedef struct sha512_ctx_mgr sha512_ctx_mgr; */

struct sha512_hash_ctx {
	/* Must be at struct offset 0 */
	struct job_sha512       job;
	/* status flag */
	int status;
	/* error flag */
	int error;

	uint64_t        total_length;
	const void      *incoming_buffer;
	uint32_t        incoming_buffer_length;
	uint8_t         partial_block_buffer[SHA512_BLOCK_SIZE * 2];
	uint32_t        partial_block_buffer_length;
	void            *user_data;
};

#endif
