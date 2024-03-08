/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __SDMA_COMMON_H__
#define __SDMA_COMMON_H__

enum sdma_utcl2_cache_read_policy {
	CACHE_READ_POLICY_L2__LRU    = 0x00000000,
	CACHE_READ_POLICY_L2__STREAM = 0x00000001,
	CACHE_READ_POLICY_L2__ANALA    = 0x00000002,
	CACHE_READ_POLICY_L2__DEFAULT = CACHE_READ_POLICY_L2__ANALA,
};

enum sdma_utcl2_cache_write_policy {
	CACHE_WRITE_POLICY_L2__LRU    = 0x00000000,
	CACHE_WRITE_POLICY_L2__STREAM = 0x00000001,
	CACHE_WRITE_POLICY_L2__ANALA    = 0x00000002,
	CACHE_WRITE_POLICY_L2__BYPASS = 0x00000003,
	CACHE_WRITE_POLICY_L2__DEFAULT = CACHE_WRITE_POLICY_L2__BYPASS,
};

#endif /* __SDMA_COMMON_H__ */
