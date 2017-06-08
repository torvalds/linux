/*
 * Header file for multi buffer SHA256 algorithm manager
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
 *	Megha Dey <megha.dey@linux.intel.com>
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
#ifndef __SHA_MB_MGR_H
#define __SHA_MB_MGR_H

#include <linux/types.h>

#define NUM_SHA256_DIGEST_WORDS 8

enum job_sts {	STS_UNKNOWN = 0,
		STS_BEING_PROCESSED = 1,
		STS_COMPLETED = 2,
		STS_INTERNAL_ERROR = 3,
		STS_ERROR = 4
};

struct job_sha256 {
	u8	*buffer;
	u32	len;
	u32	result_digest[NUM_SHA256_DIGEST_WORDS] __aligned(32);
	enum	job_sts status;
	void	*user_data;
};

/* SHA256 out-of-order scheduler */

/* typedef uint32_t sha8_digest_array[8][8]; */

struct sha256_args_x8 {
	uint32_t	digest[8][8];
	uint8_t		*data_ptr[8];
};

struct sha256_lane_data {
	struct job_sha256 *job_in_lane;
};

struct sha256_mb_mgr {
	struct sha256_args_x8 args;

	uint32_t lens[8];

	/* each byte is index (0...7) of unused lanes */
	uint64_t unused_lanes;
	/* byte 4 is set to FF as a flag */
	struct sha256_lane_data ldata[8];
};


#define SHA256_MB_MGR_NUM_LANES_AVX2 8

void sha256_mb_mgr_init_avx2(struct sha256_mb_mgr *state);
struct job_sha256 *sha256_mb_mgr_submit_avx2(struct sha256_mb_mgr *state,
					 struct job_sha256 *job);
struct job_sha256 *sha256_mb_mgr_flush_avx2(struct sha256_mb_mgr *state);
struct job_sha256 *sha256_mb_mgr_get_comp_job_avx2(struct sha256_mb_mgr *state);

#endif
