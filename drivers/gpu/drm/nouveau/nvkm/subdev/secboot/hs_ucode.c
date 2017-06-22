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

#include "hs_ucode.h"
#include "ls_ucode.h"
#include "acr.h"

#include <engine/falcon.h>

/**
 * hs_ucode_patch_signature() - patch HS blob with correct signature for
 * specified falcon.
 */
static void
hs_ucode_patch_signature(const struct nvkm_falcon *falcon, void *acr_image,
			 bool new_format)
{
	struct fw_bin_header *hsbin_hdr = acr_image;
	struct hsf_fw_header *fw_hdr = acr_image + hsbin_hdr->header_offset;
	void *hs_data = acr_image + hsbin_hdr->data_offset;
	void *sig;
	u32 sig_size;
	u32 patch_loc, patch_sig;

	/*
	 * I had the brilliant idea to "improve" the binary format by
	 * removing this useless indirection. However to make NVIDIA files
	 * directly compatible, let's support both format.
	 */
	if (new_format) {
		patch_loc = fw_hdr->patch_loc;
		patch_sig = fw_hdr->patch_sig;
	} else {
		patch_loc = *(u32 *)(acr_image + fw_hdr->patch_loc);
		patch_sig = *(u32 *)(acr_image + fw_hdr->patch_sig);
	}

	/* Falcon in debug or production mode? */
	if (falcon->debug) {
		sig = acr_image + fw_hdr->sig_dbg_offset;
		sig_size = fw_hdr->sig_dbg_size;
	} else {
		sig = acr_image + fw_hdr->sig_prod_offset;
		sig_size = fw_hdr->sig_prod_size;
	}

	/* Patch signature */
	memcpy(hs_data + patch_loc, sig + patch_sig, sig_size);
}

void *
hs_ucode_load_blob(struct nvkm_subdev *subdev, const struct nvkm_falcon *falcon,
		   const char *fw)
{
	void *acr_image;
	bool new_format;

	acr_image = nvkm_acr_load_firmware(subdev, fw, 0);
	if (IS_ERR(acr_image))
		return acr_image;

	/* detect the format to define how signature should be patched */
	switch (((u32 *)acr_image)[0]) {
	case 0x3b1d14f0:
		new_format = true;
		break;
	case 0x000010de:
		new_format = false;
		break;
	default:
		nvkm_error(subdev, "unknown header for HS blob %s\n", fw);
		return ERR_PTR(-EINVAL);
	}

	hs_ucode_patch_signature(falcon, acr_image, new_format);

	return acr_image;
}
