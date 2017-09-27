/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __NVKM_SECBOOT_HS_UCODE_H__
#define __NVKM_SECBOOT_HS_UCODE_H__

#include <core/os.h>
#include <core/subdev.h>

struct nvkm_falcon;

/**
 * struct hsf_fw_header - HS firmware descriptor
 * @sig_dbg_offset:	offset of the debug signature
 * @sig_dbg_size:	size of the debug signature
 * @sig_prod_offset:	offset of the production signature
 * @sig_prod_size:	size of the production signature
 * @patch_loc:		offset of the offset (sic) of where the signature is
 * @patch_sig:		offset of the offset (sic) to add to sig_*_offset
 * @hdr_offset:		offset of the load header (see struct hs_load_header)
 * @hdr_size:		size of above header
 *
 * This structure is embedded in the HS firmware image at
 * hs_bin_hdr.header_offset.
 */
struct hsf_fw_header {
	u32 sig_dbg_offset;
	u32 sig_dbg_size;
	u32 sig_prod_offset;
	u32 sig_prod_size;
	u32 patch_loc;
	u32 patch_sig;
	u32 hdr_offset;
	u32 hdr_size;
};

/**
 * struct hsf_load_header - HS firmware load header
 */
struct hsf_load_header {
	u32 non_sec_code_off;
	u32 non_sec_code_size;
	u32 data_dma_base;
	u32 data_size;
	u32 num_apps;
	/*
	 * Organized as follows:
	 * - app0_code_off
	 * - app1_code_off
	 * - ...
	 * - appn_code_off
	 * - app0_code_size
	 * - app1_code_size
	 * - ...
	 */
	u32 apps[0];
};

void *hs_ucode_load_blob(struct nvkm_subdev *, const struct nvkm_falcon *,
			 const char *);

#endif
