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

#include "acr_r367.h"
#include "acr_r361.h"

#include <core/gpuobj.h>

/*
 * r367 ACR: new LS signature format requires a rewrite of LS firmware and
 * blob creation functions. Also the hsflcn_desc layout has changed slightly.
 */

#define LSF_LSB_DEPMAP_SIZE 11

/**
 * struct acr_r367_lsf_lsb_header - LS firmware header
 *
 * See also struct acr_r352_lsf_lsb_header for documentation.
 */
struct acr_r367_lsf_lsb_header {
	/**
	 * LS falcon signatures
	 * @prd_keys:		signature to use in production mode
	 * @dgb_keys:		signature to use in debug mode
	 * @b_prd_present:	whether the production key is present
	 * @b_dgb_present:	whether the debug key is present
	 * @falcon_id:		ID of the falcon the ucode applies to
	 */
	struct {
		u8 prd_keys[2][16];
		u8 dbg_keys[2][16];
		u32 b_prd_present;
		u32 b_dbg_present;
		u32 falcon_id;
		u32 supports_versioning;
		u32 version;
		u32 depmap_count;
		u8 depmap[LSF_LSB_DEPMAP_SIZE * 2 * 4];
		u8 kdf[16];
	} signature;
	u32 ucode_off;
	u32 ucode_size;
	u32 data_size;
	u32 bl_code_size;
	u32 bl_imem_off;
	u32 bl_data_off;
	u32 bl_data_size;
	u32 app_code_off;
	u32 app_code_size;
	u32 app_data_off;
	u32 app_data_size;
	u32 flags;
};

/**
 * struct acr_r367_lsf_wpr_header - LS blob WPR Header
 *
 * See also struct acr_r352_lsf_wpr_header for documentation.
 */
struct acr_r367_lsf_wpr_header {
	u32 falcon_id;
	u32 lsb_offset;
	u32 bootstrap_owner;
	u32 lazy_bootstrap;
	u32 bin_version;
	u32 status;
#define LSF_IMAGE_STATUS_NONE				0
#define LSF_IMAGE_STATUS_COPY				1
#define LSF_IMAGE_STATUS_VALIDATION_CODE_FAILED		2
#define LSF_IMAGE_STATUS_VALIDATION_DATA_FAILED		3
#define LSF_IMAGE_STATUS_VALIDATION_DONE		4
#define LSF_IMAGE_STATUS_VALIDATION_SKIPPED		5
#define LSF_IMAGE_STATUS_BOOTSTRAP_READY		6
#define LSF_IMAGE_STATUS_REVOCATION_CHECK_FAILED		7
};

/**
 * struct ls_ucode_img_r367 - ucode image augmented with r367 headers
 */
struct ls_ucode_img_r367 {
	struct ls_ucode_img base;

	struct acr_r367_lsf_wpr_header wpr_header;
	struct acr_r367_lsf_lsb_header lsb_header;
};
#define ls_ucode_img_r367(i) container_of(i, struct ls_ucode_img_r367, base)

struct ls_ucode_img *
acr_r367_ls_ucode_img_load(const struct acr_r352 *acr,
			   const struct nvkm_secboot *sb,
			   enum nvkm_secboot_falcon falcon_id)
{
	const struct nvkm_subdev *subdev = acr->base.subdev;
	struct ls_ucode_img_r367 *img;
	int ret;

	img = kzalloc(sizeof(*img), GFP_KERNEL);
	if (!img)
		return ERR_PTR(-ENOMEM);

	img->base.falcon_id = falcon_id;

	ret = acr->func->ls_func[falcon_id]->load(sb, &img->base);
	if (ret) {
		kfree(img->base.ucode_data);
		kfree(img->base.sig);
		kfree(img);
		return ERR_PTR(ret);
	}

	/* Check that the signature size matches our expectations... */
	if (img->base.sig_size != sizeof(img->lsb_header.signature)) {
		nvkm_error(subdev, "invalid signature size for %s falcon!\n",
			   nvkm_secboot_falcon_name[falcon_id]);
		return ERR_PTR(-EINVAL);
	}

	/* Copy signature to the right place */
	memcpy(&img->lsb_header.signature, img->base.sig, img->base.sig_size);

	/* not needed? the signature should already have the right value */
	img->lsb_header.signature.falcon_id = falcon_id;

	return &img->base;
}

#define LSF_LSB_HEADER_ALIGN 256
#define LSF_BL_DATA_ALIGN 256
#define LSF_BL_DATA_SIZE_ALIGN 256
#define LSF_BL_CODE_SIZE_ALIGN 256
#define LSF_UCODE_DATA_ALIGN 4096

static u32
acr_r367_ls_img_fill_headers(struct acr_r352 *acr,
			     struct ls_ucode_img_r367 *img, u32 offset)
{
	struct ls_ucode_img *_img = &img->base;
	struct acr_r367_lsf_wpr_header *whdr = &img->wpr_header;
	struct acr_r367_lsf_lsb_header *lhdr = &img->lsb_header;
	struct ls_ucode_img_desc *desc = &_img->ucode_desc;
	const struct acr_r352_ls_func *func =
					    acr->func->ls_func[_img->falcon_id];

	/* Fill WPR header */
	whdr->falcon_id = _img->falcon_id;
	whdr->bootstrap_owner = acr->base.boot_falcon;
	whdr->bin_version = lhdr->signature.version;
	whdr->status = LSF_IMAGE_STATUS_COPY;

	/* Skip bootstrapping falcons started by someone else than ACR */
	if (acr->lazy_bootstrap & BIT(_img->falcon_id))
		whdr->lazy_bootstrap = 1;

	/* Align, save off, and include an LSB header size */
	offset = ALIGN(offset, LSF_LSB_HEADER_ALIGN);
	whdr->lsb_offset = offset;
	offset += sizeof(*lhdr);

	/*
	 * Align, save off, and include the original (static) ucode
	 * image size
	 */
	offset = ALIGN(offset, LSF_UCODE_DATA_ALIGN);
	_img->ucode_off = lhdr->ucode_off = offset;
	offset += _img->ucode_size;

	/*
	 * For falcons that use a boot loader (BL), we append a loader
	 * desc structure on the end of the ucode image and consider
	 * this the boot loader data. The host will then copy the loader
	 * desc args to this space within the WPR region (before locking
	 * down) and the HS bin will then copy them to DMEM 0 for the
	 * loader.
	 */
	lhdr->bl_code_size = ALIGN(desc->bootloader_size,
				   LSF_BL_CODE_SIZE_ALIGN);
	lhdr->ucode_size = ALIGN(desc->app_resident_data_offset,
				 LSF_BL_CODE_SIZE_ALIGN) + lhdr->bl_code_size;
	lhdr->data_size = ALIGN(desc->app_size, LSF_BL_CODE_SIZE_ALIGN) +
				lhdr->bl_code_size - lhdr->ucode_size;
	/*
	 * Though the BL is located at 0th offset of the image, the VA
	 * is different to make sure that it doesn't collide the actual
	 * OS VA range
	 */
	lhdr->bl_imem_off = desc->bootloader_imem_offset;
	lhdr->app_code_off = desc->app_start_offset +
			     desc->app_resident_code_offset;
	lhdr->app_code_size = desc->app_resident_code_size;
	lhdr->app_data_off = desc->app_start_offset +
			     desc->app_resident_data_offset;
	lhdr->app_data_size = desc->app_resident_data_size;

	lhdr->flags = func->lhdr_flags;
	if (_img->falcon_id == acr->base.boot_falcon)
		lhdr->flags |= LSF_FLAG_DMACTL_REQ_CTX;

	/* Align and save off BL descriptor size */
	lhdr->bl_data_size = ALIGN(func->bl_desc_size, LSF_BL_DATA_SIZE_ALIGN);

	/*
	 * Align, save off, and include the additional BL data
	 */
	offset = ALIGN(offset, LSF_BL_DATA_ALIGN);
	lhdr->bl_data_off = offset;
	offset += lhdr->bl_data_size;

	return offset;
}

int
acr_r367_ls_fill_headers(struct acr_r352 *acr, struct list_head *imgs)
{
	struct ls_ucode_img_r367 *img;
	struct list_head *l;
	u32 count = 0;
	u32 offset;

	/* Count the number of images to manage */
	list_for_each(l, imgs)
		count++;

	/*
	 * Start with an array of WPR headers at the base of the WPR.
	 * The expectation here is that the secure falcon will do a single DMA
	 * read of this array and cache it internally so it's ok to pack these.
	 * Also, we add 1 to the falcon count to indicate the end of the array.
	 */
	offset = sizeof(img->wpr_header) * (count + 1);

	/*
	 * Walk the managed falcons, accounting for the LSB structs
	 * as well as the ucode images.
	 */
	list_for_each_entry(img, imgs, base.node) {
		offset = acr_r367_ls_img_fill_headers(acr, img, offset);
	}

	return offset;
}

int
acr_r367_ls_write_wpr(struct acr_r352 *acr, struct list_head *imgs,
		      struct nvkm_gpuobj *wpr_blob, u64 wpr_addr)
{
	struct ls_ucode_img *_img;
	u32 pos = 0;

	nvkm_kmap(wpr_blob);

	list_for_each_entry(_img, imgs, node) {
		struct ls_ucode_img_r367 *img = ls_ucode_img_r367(_img);
		const struct acr_r352_ls_func *ls_func =
					    acr->func->ls_func[_img->falcon_id];
		u8 gdesc[ls_func->bl_desc_size];

		nvkm_gpuobj_memcpy_to(wpr_blob, pos, &img->wpr_header,
				      sizeof(img->wpr_header));

		nvkm_gpuobj_memcpy_to(wpr_blob, img->wpr_header.lsb_offset,
				     &img->lsb_header, sizeof(img->lsb_header));

		/* Generate and write BL descriptor */
		memset(gdesc, 0, ls_func->bl_desc_size);
		ls_func->generate_bl_desc(&acr->base, _img, wpr_addr, gdesc);

		nvkm_gpuobj_memcpy_to(wpr_blob, img->lsb_header.bl_data_off,
				      gdesc, ls_func->bl_desc_size);

		/* Copy ucode */
		nvkm_gpuobj_memcpy_to(wpr_blob, img->lsb_header.ucode_off,
				      _img->ucode_data, _img->ucode_size);

		pos += sizeof(img->wpr_header);
	}

	nvkm_wo32(wpr_blob, pos, NVKM_SECBOOT_FALCON_INVALID);

	nvkm_done(wpr_blob);

	return 0;
}

struct acr_r367_hsflcn_desc {
	u8 reserved_dmem[0x200];
	u32 signatures[4];
	u32 wpr_region_id;
	u32 wpr_offset;
	u32 mmu_memory_range;
#define FLCN_ACR_MAX_REGIONS 2
	struct {
		u32 no_regions;
		struct {
			u32 start_addr;
			u32 end_addr;
			u32 region_id;
			u32 read_mask;
			u32 write_mask;
			u32 client_mask;
			u32 shadow_mem_start_addr;
		} region_props[FLCN_ACR_MAX_REGIONS];
	} regions;
	u32 ucode_blob_size;
	u64 ucode_blob_base __aligned(8);
	struct {
		u32 vpr_enabled;
		u32 vpr_start;
		u32 vpr_end;
		u32 hdcp_policies;
	} vpr_desc;
};

void
acr_r367_fixup_hs_desc(struct acr_r352 *acr, struct nvkm_secboot *sb,
		       void *_desc)
{
	struct acr_r367_hsflcn_desc *desc = _desc;
	struct nvkm_gpuobj *ls_blob = acr->ls_blob;

	/* WPR region information if WPR is not fixed */
	if (sb->wpr_size == 0) {
		u64 wpr_start = ls_blob->addr;
		u64 wpr_end = ls_blob->addr + ls_blob->size;

		if (acr->func->shadow_blob)
			wpr_start += ls_blob->size / 2;

		desc->wpr_region_id = 1;
		desc->regions.no_regions = 2;
		desc->regions.region_props[0].start_addr = wpr_start >> 8;
		desc->regions.region_props[0].end_addr = wpr_end >> 8;
		desc->regions.region_props[0].region_id = 1;
		desc->regions.region_props[0].read_mask = 0xf;
		desc->regions.region_props[0].write_mask = 0xc;
		desc->regions.region_props[0].client_mask = 0x2;
		if (acr->func->shadow_blob)
			desc->regions.region_props[0].shadow_mem_start_addr =
							     ls_blob->addr >> 8;
		else
			desc->regions.region_props[0].shadow_mem_start_addr = 0;
	} else {
		desc->ucode_blob_base = ls_blob->addr;
		desc->ucode_blob_size = ls_blob->size;
	}
}

const struct acr_r352_func
acr_r367_func = {
	.fixup_hs_desc = acr_r367_fixup_hs_desc,
	.generate_hs_bl_desc = acr_r361_generate_hs_bl_desc,
	.hs_bl_desc_size = sizeof(struct acr_r361_flcn_bl_desc),
	.shadow_blob = true,
	.ls_ucode_img_load = acr_r367_ls_ucode_img_load,
	.ls_fill_headers = acr_r367_ls_fill_headers,
	.ls_write_wpr = acr_r367_ls_write_wpr,
	.ls_func = {
		[NVKM_SECBOOT_FALCON_FECS] = &acr_r361_ls_fecs_func,
		[NVKM_SECBOOT_FALCON_GPCCS] = &acr_r361_ls_gpccs_func,
		[NVKM_SECBOOT_FALCON_PMU] = &acr_r361_ls_pmu_func,
		[NVKM_SECBOOT_FALCON_SEC2] = &acr_r361_ls_sec2_func,
	},
};

struct nvkm_acr *
acr_r367_new(enum nvkm_secboot_falcon boot_falcon,
	     unsigned long managed_falcons)
{
	return acr_r352_new_(&acr_r367_func, boot_falcon, managed_falcons);
}
