/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
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

#include "acr_r352.h"
#include "ls_ucode.h"

#include <core/gpuobj.h>
#include <core/firmware.h>
#include <engine/falcon.h>

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
 * struct acr_r352_flcn_bl_desc - DMEM bootloader descriptor
 * @signature:		16B signature for secure code. 0s if no secure code
 * @ctx_dma:		DMA context to be used by BL while loading code/data
 * @code_dma_base:	256B-aligned Physical FB Address where code is located
 *			(falcon's $xcbase register)
 * @non_sec_code_off:	offset from code_dma_base where the non-secure code is
 *                      located. The offset must be multiple of 256 to help perf
 * @non_sec_code_size:	the size of the nonSecure code part.
 * @sec_code_off:	offset from code_dma_base where the secure code is
 *                      located. The offset must be multiple of 256 to help perf
 * @sec_code_size:	offset from code_dma_base where the secure code is
 *                      located. The offset must be multiple of 256 to help perf
 * @code_entry_point:	code entry point which will be invoked by BL after
 *                      code is loaded.
 * @data_dma_base:	256B aligned Physical FB Address where data is located.
 *			(falcon's $xdbase register)
 * @data_size:		size of data block. Should be multiple of 256B
 *
 * Structure used by the bootloader to load the rest of the code. This has
 * to be filled by host and copied into DMEM at offset provided in the
 * hsflcn_bl_desc.bl_desc_dmem_load_off.
 */
struct acr_r352_flcn_bl_desc {
	u32 reserved[4];
	u32 signature[4];
	u32 ctx_dma;
	u32 code_dma_base;
	u32 non_sec_code_off;
	u32 non_sec_code_size;
	u32 sec_code_off;
	u32 sec_code_size;
	u32 code_entry_point;
	u32 data_dma_base;
	u32 data_size;
};

/**
 * acr_r352_generate_flcn_bl_desc - generate generic BL descriptor for LS image
 */
static void
acr_r352_generate_flcn_bl_desc(const struct nvkm_acr *acr,
			       const struct ls_ucode_img *img, u64 wpr_addr,
			       void *_desc)
{
	struct acr_r352_flcn_bl_desc *desc = _desc;
	const struct ls_ucode_img_desc *pdesc = &img->ucode_desc;
	u64 base, addr_code, addr_data;

	base = wpr_addr + img->lsb_header.ucode_off + pdesc->app_start_offset;
	addr_code = (base + pdesc->app_resident_code_offset) >> 8;
	addr_data = (base + pdesc->app_resident_data_offset) >> 8;

	memset(desc, 0, sizeof(*desc));
	desc->ctx_dma = FALCON_DMAIDX_UCODE;
	desc->code_dma_base = lower_32_bits(addr_code);
	desc->non_sec_code_off = pdesc->app_resident_code_offset;
	desc->non_sec_code_size = pdesc->app_resident_code_size;
	desc->code_entry_point = pdesc->app_imem_entry;
	desc->data_dma_base = lower_32_bits(addr_data);
	desc->data_size = pdesc->app_resident_data_size;
}


/**
 * struct hsflcn_acr_desc - data section of the HS firmware
 *
 * This header is to be copied at the beginning of DMEM by the HS bootloader.
 *
 * @signature:		signature of ACR ucode
 * @wpr_region_id:	region ID holding the WPR header and its details
 * @wpr_offset:		offset from the WPR region holding the wpr header
 * @regions:		region descriptors
 * @nonwpr_ucode_blob_size:	size of LS blob
 * @nonwpr_ucode_blob_start:	FB location of LS blob is
 */
struct hsflcn_acr_desc {
	union {
		u8 reserved_dmem[0x200];
		u32 signatures[4];
	} ucode_reserved_space;
	u32 wpr_region_id;
	u32 wpr_offset;
	u32 mmu_mem_range;
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


/*
 * Low-secure blob creation
 */

typedef int (*lsf_load_func)(const struct nvkm_subdev *, struct ls_ucode_img *);

/**
 * ls_ucode_img_load() - create a lsf_ucode_img and load it
 */
static struct ls_ucode_img *
ls_ucode_img_load(const struct nvkm_subdev *subdev, lsf_load_func load_func)
{
	struct ls_ucode_img *img;
	int ret;

	img = kzalloc(sizeof(*img), GFP_KERNEL);
	if (!img)
		return ERR_PTR(-ENOMEM);

	ret = load_func(subdev, img);

	if (ret) {
		kfree(img);
		return ERR_PTR(ret);
	}

	return img;
}

#define LSF_LSB_HEADER_ALIGN 256
#define LSF_BL_DATA_ALIGN 256
#define LSF_BL_DATA_SIZE_ALIGN 256
#define LSF_BL_CODE_SIZE_ALIGN 256
#define LSF_UCODE_DATA_ALIGN 4096

/**
 * ls_ucode_img_fill_headers - fill the WPR and LSB headers of an image
 * @acr:	ACR to use
 * @img:	image to generate for
 * @offset:	offset in the WPR region where this image starts
 *
 * Allocate space in the WPR area from offset and write the WPR and LSB headers
 * accordingly.
 *
 * Return: offset at the end of this image.
 */
static u32
ls_ucode_img_fill_headers(struct acr_r352 *acr, struct ls_ucode_img *img,
			  u32 offset)
{
	struct lsf_wpr_header *whdr = &img->wpr_header;
	struct lsf_lsb_header *lhdr = &img->lsb_header;
	struct ls_ucode_img_desc *desc = &img->ucode_desc;
	const struct acr_r352_ls_func *func =
					    acr->func->ls_func[img->falcon_id];

	/* Fill WPR header */
	whdr->falcon_id = img->falcon_id;
	whdr->bootstrap_owner = acr->base.boot_falcon;
	whdr->status = LSF_IMAGE_STATUS_COPY;

	/* Align, save off, and include an LSB header size */
	offset = ALIGN(offset, LSF_LSB_HEADER_ALIGN);
	whdr->lsb_offset = offset;
	offset += sizeof(struct lsf_lsb_header);

	/*
	 * Align, save off, and include the original (static) ucode
	 * image size
	 */
	offset = ALIGN(offset, LSF_UCODE_DATA_ALIGN);
	lhdr->ucode_off = offset;
	offset += img->ucode_size;

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
	if (img->falcon_id == acr->base.boot_falcon)
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

/**
 * struct ls_ucode_mgr - manager for all LS falcon firmwares
 * @count:	number of managed LS falcons
 * @wpr_size:	size of the required WPR region in bytes
 * @img_list:	linked list of lsf_ucode_img
 */
struct ls_ucode_mgr {
	u16 count;
	u32 wpr_size;
	struct list_head img_list;
};

static void
ls_ucode_mgr_init(struct ls_ucode_mgr *mgr)
{
	memset(mgr, 0, sizeof(*mgr));
	INIT_LIST_HEAD(&mgr->img_list);
}

static void
ls_ucode_mgr_cleanup(struct ls_ucode_mgr *mgr)
{
	struct ls_ucode_img *img, *t;

	list_for_each_entry_safe(img, t, &mgr->img_list, node) {
		kfree(img->ucode_data);
		kfree(img);
	}
}

static void
ls_ucode_mgr_add_img(struct ls_ucode_mgr *mgr, struct ls_ucode_img *img)
{
	mgr->count++;
	list_add_tail(&img->node, &mgr->img_list);
}

/**
 * ls_ucode_mgr_fill_headers - fill WPR and LSB headers of all managed images
 */
static void
ls_ucode_mgr_fill_headers(struct acr_r352 *acr, struct ls_ucode_mgr *mgr)
{
	struct ls_ucode_img *img;
	u32 offset;

	/*
	 * Start with an array of WPR headers at the base of the WPR.
	 * The expectation here is that the secure falcon will do a single DMA
	 * read of this array and cache it internally so it's ok to pack these.
	 * Also, we add 1 to the falcon count to indicate the end of the array.
	 */
	offset = sizeof(struct lsf_wpr_header) * (mgr->count + 1);

	/*
	 * Walk the managed falcons, accounting for the LSB structs
	 * as well as the ucode images.
	 */
	list_for_each_entry(img, &mgr->img_list, node) {
		offset = ls_ucode_img_fill_headers(acr, img, offset);
	}

	mgr->wpr_size = offset;
}

/**
 * ls_ucode_mgr_write_wpr - write the WPR blob contents
 */
static int
ls_ucode_mgr_write_wpr(struct acr_r352 *acr, struct ls_ucode_mgr *mgr,
		       struct nvkm_gpuobj *wpr_blob, u32 wpr_addr)
{
	struct ls_ucode_img *img;
	u32 pos = 0;

	nvkm_kmap(wpr_blob);

	list_for_each_entry(img, &mgr->img_list, node) {
		const struct acr_r352_ls_func *ls_func =
					     acr->func->ls_func[img->falcon_id];
		u8 gdesc[ls_func->bl_desc_size];

		nvkm_gpuobj_memcpy_to(wpr_blob, pos, &img->wpr_header,
				      sizeof(img->wpr_header));

		nvkm_gpuobj_memcpy_to(wpr_blob, img->wpr_header.lsb_offset,
				     &img->lsb_header, sizeof(img->lsb_header));

		/* Generate and write BL descriptor */
		ls_func->generate_bl_desc(&acr->base, img, wpr_addr, gdesc);

		nvkm_gpuobj_memcpy_to(wpr_blob, img->lsb_header.bl_data_off,
				      gdesc, ls_func->bl_desc_size);

		/* Copy ucode */
		nvkm_gpuobj_memcpy_to(wpr_blob, img->lsb_header.ucode_off,
				      img->ucode_data, img->ucode_size);

		pos += sizeof(img->wpr_header);
	}

	nvkm_wo32(wpr_blob, pos, NVKM_SECBOOT_FALCON_INVALID);

	nvkm_done(wpr_blob);

	return 0;
}

/* Both size and address of WPR need to be 128K-aligned */
#define WPR_ALIGNMENT	0x20000
/**
 * acr_r352_prepare_ls_blob() - prepare the LS blob
 *
 * For each securely managed falcon, load the FW, signatures and bootloaders and
 * prepare a ucode blob. Then, compute the offsets in the WPR region for each
 * blob, and finally write the headers and ucode blobs into a GPU object that
 * will be copied into the WPR region by the HS firmware.
 */
static int
acr_r352_prepare_ls_blob(struct acr_r352 *acr, u64 wpr_addr, u32 wpr_size)
{
	const struct nvkm_subdev *subdev = acr->base.subdev;
	struct ls_ucode_mgr mgr;
	unsigned long managed_falcons = acr->base.managed_falcons;
	int falcon_id;
	int ret;

	ls_ucode_mgr_init(&mgr);

	/* Load all LS blobs */
	for_each_set_bit(falcon_id, &managed_falcons, NVKM_SECBOOT_FALCON_END) {
		struct ls_ucode_img *img;

		img = ls_ucode_img_load(subdev,
					acr->func->ls_func[falcon_id]->load);

		if (IS_ERR(img)) {
			ret = PTR_ERR(img);
			goto cleanup;
		}
		ls_ucode_mgr_add_img(&mgr, img);
	}

	/*
	 * Fill the WPR and LSF headers with the right offsets and compute
	 * required WPR size
	 */
	ls_ucode_mgr_fill_headers(acr, &mgr);
	mgr.wpr_size = ALIGN(mgr.wpr_size, WPR_ALIGNMENT);

	/* Allocate GPU object that will contain the WPR region */
	ret = nvkm_gpuobj_new(subdev->device, mgr.wpr_size, WPR_ALIGNMENT,
			      false, NULL, &acr->ls_blob);
	if (ret)
		goto cleanup;

	nvkm_debug(subdev, "%d managed LS falcons, WPR size is %d bytes\n",
		    mgr.count, mgr.wpr_size);

	/* If WPR address and size are not fixed, set them to fit the LS blob */
	if (wpr_size == 0) {
		wpr_addr = acr->ls_blob->addr;
		wpr_size = mgr.wpr_size;
	/*
	 * But if the WPR region is set by the bootloader, it is illegal for
	 * the HS blob to be larger than this region.
	 */
	} else if (mgr.wpr_size > wpr_size) {
		nvkm_error(subdev, "WPR region too small for FW blob!\n");
		nvkm_error(subdev, "required: %dB\n", mgr.wpr_size);
		nvkm_error(subdev, "available: %dB\n", wpr_size);
		ret = -ENOSPC;
		goto cleanup;
	}

	/* Write LS blob */
	ret = ls_ucode_mgr_write_wpr(acr, &mgr, acr->ls_blob, wpr_addr);
	if (ret)
		nvkm_gpuobj_del(&acr->ls_blob);

cleanup:
	ls_ucode_mgr_cleanup(&mgr);

	return ret;
}




/**
 * acr_r352_hsf_patch_signature() - patch HS blob with correct signature
 */
static void
acr_r352_hsf_patch_signature(struct nvkm_secboot *sb, void *acr_image)
{
	struct fw_bin_header *hsbin_hdr = acr_image;
	struct hsf_fw_header *fw_hdr = acr_image + hsbin_hdr->header_offset;
	void *hs_data = acr_image + hsbin_hdr->data_offset;
	void *sig;
	u32 sig_size;

	/* Falcon in debug or production mode? */
	if (sb->boot_falcon->debug) {
		sig = acr_image + fw_hdr->sig_dbg_offset;
		sig_size = fw_hdr->sig_dbg_size;
	} else {
		sig = acr_image + fw_hdr->sig_prod_offset;
		sig_size = fw_hdr->sig_prod_size;
	}

	/* Patch signature */
	memcpy(hs_data + fw_hdr->patch_loc, sig + fw_hdr->patch_sig, sig_size);
}

static void
acr_r352_fixup_hs_desc(struct acr_r352 *acr, struct nvkm_secboot *sb,
		       struct hsflcn_acr_desc *desc)
{
	struct nvkm_gpuobj *ls_blob = acr->ls_blob;

	desc->ucode_blob_base = ls_blob->addr;
	desc->ucode_blob_size = ls_blob->size;

	desc->wpr_offset = 0;

	/* WPR region information if WPR is not fixed */
	if (sb->wpr_size == 0) {
		desc->wpr_region_id = 1;
		desc->regions.no_regions = 1;
		desc->regions.region_props[0].region_id = 1;
		desc->regions.region_props[0].start_addr = ls_blob->addr >> 8;
		desc->regions.region_props[0].end_addr =
					   (ls_blob->addr + ls_blob->size) >> 8;
	}
}

static void
acr_r352_generate_hs_bl_desc(const struct hsf_load_header *hdr, void *_bl_desc,
			     u64 offset)
{
	struct acr_r352_flcn_bl_desc *bl_desc = _bl_desc;
	u64 addr_code, addr_data;

	memset(bl_desc, 0, sizeof(*bl_desc));
	addr_code = offset >> 8;
	addr_data = (offset + hdr->data_dma_base) >> 8;

	bl_desc->ctx_dma = FALCON_DMAIDX_VIRT;
	bl_desc->code_dma_base = lower_32_bits(addr_code);
	bl_desc->non_sec_code_off = hdr->non_sec_code_off;
	bl_desc->non_sec_code_size = hdr->non_sec_code_size;
	bl_desc->sec_code_off = hdr->app[0].sec_code_off;
	bl_desc->sec_code_size = hdr->app[0].sec_code_size;
	bl_desc->code_entry_point = 0;
	bl_desc->data_dma_base = lower_32_bits(addr_data);
	bl_desc->data_size = hdr->data_size;
}

/**
 * acr_r352_prepare_hs_blob - load and prepare a HS blob and BL descriptor
 *
 * @sb secure boot instance to prepare for
 * @fw name of the HS firmware to load
 * @blob pointer to gpuobj that will be allocated to receive the HS FW payload
 * @bl_desc pointer to the BL descriptor to write for this firmware
 * @patch whether we should patch the HS descriptor (only for HS loaders)
 */
static int
acr_r352_prepare_hs_blob(struct acr_r352 *acr, struct nvkm_secboot *sb,
			 const char *fw, struct nvkm_gpuobj **blob,
			 struct hsf_load_header *load_header, bool patch)
{
	struct nvkm_subdev *subdev = &sb->subdev;
	void *acr_image;
	struct fw_bin_header *hsbin_hdr;
	struct hsf_fw_header *fw_hdr;
	struct hsf_load_header *load_hdr;
	void *acr_data;
	int ret;

	acr_image = nvkm_acr_load_firmware(subdev, fw, 0);
	if (IS_ERR(acr_image))
		return PTR_ERR(acr_image);

	hsbin_hdr = acr_image;
	fw_hdr = acr_image + hsbin_hdr->header_offset;
	load_hdr = acr_image + fw_hdr->hdr_offset;
	acr_data = acr_image + hsbin_hdr->data_offset;

	/* Patch signature */
	acr_r352_hsf_patch_signature(sb, acr_image);

	/* Patch descriptor with WPR information? */
	if (patch) {
		struct hsflcn_acr_desc *desc;

		desc = acr_data + load_hdr->data_dma_base;
		acr_r352_fixup_hs_desc(acr, sb, desc);
	}

	if (load_hdr->num_apps > ACR_R352_MAX_APPS) {
		nvkm_error(subdev, "more apps (%d) than supported (%d)!",
			   load_hdr->num_apps, ACR_R352_MAX_APPS);
		ret = -EINVAL;
		goto cleanup;
	}
	memcpy(load_header, load_hdr, sizeof(*load_header) +
			       (sizeof(load_hdr->app[0]) * load_hdr->num_apps));

	/* Create ACR blob and copy HS data to it */
	ret = nvkm_gpuobj_new(subdev->device, ALIGN(hsbin_hdr->data_size, 256),
			      0x1000, false, NULL, blob);
	if (ret)
		goto cleanup;

	nvkm_kmap(*blob);
	nvkm_gpuobj_memcpy_to(*blob, 0, acr_data, hsbin_hdr->data_size);
	nvkm_done(*blob);

cleanup:
	kfree(acr_image);

	return ret;
}

static int
acr_r352_prepare_hsbl_blob(struct acr_r352 *acr)
{
	const struct nvkm_subdev *subdev = acr->base.subdev;
	struct fw_bin_header *hdr;
	struct fw_bl_desc *hsbl_desc;

	acr->hsbl_blob = nvkm_acr_load_firmware(subdev, "acr/bl", 0);
	if (IS_ERR(acr->hsbl_blob)) {
		int ret = PTR_ERR(acr->hsbl_blob);

		acr->hsbl_blob = NULL;
		return ret;
	}

	hdr = acr->hsbl_blob;
	hsbl_desc = acr->hsbl_blob + hdr->header_offset;

	/* virtual start address for boot vector */
	acr->base.start_address = hsbl_desc->start_tag << 8;

	return 0;
}

/**
 * acr_r352_load_blobs - load blobs common to all ACR V1 versions.
 *
 * This includes the LS blob, HS ucode loading blob, and HS bootloader.
 *
 * The HS ucode unload blob is only used on dGPU if the WPR region is variable.
 */
int
acr_r352_load_blobs(struct acr_r352 *acr, struct nvkm_secboot *sb)
{
	int ret;

	/* Firmware already loaded? */
	if (acr->firmware_ok)
		return 0;

	/* Load and prepare the managed falcon's firmwares */
	ret = acr_r352_prepare_ls_blob(acr, sb->wpr_addr, sb->wpr_size);
	if (ret)
		return ret;

	/* Load the HS firmware that will load the LS firmwares */
	if (!acr->load_blob) {
		ret = acr_r352_prepare_hs_blob(acr, sb, "acr/ucode_load",
					       &acr->load_blob,
					       &acr->load_bl_header, true);
		if (ret)
			return ret;
	}

	/* If the ACR region is dynamically programmed, we need an unload FW */
	if (sb->wpr_size == 0) {
		ret = acr_r352_prepare_hs_blob(acr, sb, "acr/ucode_unload",
					       &acr->unload_blob,
					       &acr->unload_bl_header, false);
		if (ret)
			return ret;
	}

	/* Load the HS firmware bootloader */
	if (!acr->hsbl_blob) {
		ret = acr_r352_prepare_hsbl_blob(acr);
		if (ret)
			return ret;
	}

	acr->firmware_ok = true;
	nvkm_debug(&sb->subdev, "LS blob successfully created\n");

	return 0;
}

/**
 * acr_r352_load() - prepare HS falcon to run the specified blob, mapped
 * at GPU address offset.
 */
static int
acr_r352_load(struct nvkm_acr *_acr, struct nvkm_secboot *sb,
	      struct nvkm_gpuobj *blob, u64 offset)
{
	struct acr_r352 *acr = acr_r352(_acr);
	struct nvkm_falcon *falcon = sb->boot_falcon;
	struct fw_bin_header *hdr = acr->hsbl_blob;
	struct fw_bl_desc *hsbl_desc = acr->hsbl_blob + hdr->header_offset;
	void *blob_data = acr->hsbl_blob + hdr->data_offset;
	void *hsbl_code = blob_data + hsbl_desc->code_off;
	void *hsbl_data = blob_data + hsbl_desc->data_off;
	u32 code_size = ALIGN(hsbl_desc->code_size, 256);
	const struct hsf_load_header *load_hdr;
	const u32 bl_desc_size = acr->func->hs_bl_desc_size;
	u8 bl_desc[bl_desc_size];

	/* Find the bootloader descriptor for our blob and copy it */
	if (blob == acr->load_blob) {
		load_hdr = &acr->load_bl_header;
	} else if (blob == acr->unload_blob) {
		load_hdr = &acr->unload_bl_header;
	} else {
		nvkm_error(_acr->subdev, "invalid secure boot blob!\n");
		return -EINVAL;
	}

	/*
	 * Copy HS bootloader data
	 */
	nvkm_falcon_load_dmem(falcon, hsbl_data, 0x0, hsbl_desc->data_size, 0);

	/* Copy HS bootloader code to end of IMEM */
	nvkm_falcon_load_imem(falcon, hsbl_code, falcon->code.limit - code_size,
			      code_size, hsbl_desc->start_tag, 0, false);

	/* Generate the BL header */
	acr->func->generate_hs_bl_desc(load_hdr, bl_desc, offset);

	/*
	 * Copy HS BL header where the HS descriptor expects it to be
	 */
	nvkm_falcon_load_dmem(falcon, bl_desc, hsbl_desc->dmem_load_off,
			      bl_desc_size, 0);

	return 0;
}

static int
acr_r352_shutdown(struct acr_r352 *acr, struct nvkm_secboot *sb)
{
	int i;

	/* Run the unload blob to unprotect the WPR region */
	if (acr->unload_blob && sb->wpr_set) {
		int ret;

		nvkm_debug(&sb->subdev, "running HS unload blob\n");
		ret = sb->func->run_blob(sb, acr->unload_blob);
		if (ret)
			return ret;
		nvkm_debug(&sb->subdev, "HS unload blob completed\n");
	}

	for (i = 0; i < NVKM_SECBOOT_FALCON_END; i++)
		acr->falcon_state[i] = NON_SECURE;

	sb->wpr_set = false;

	return 0;
}

static int
acr_r352_bootstrap(struct acr_r352 *acr, struct nvkm_secboot *sb)
{
	int ret;

	if (sb->wpr_set)
		return 0;

	/* Make sure all blobs are ready */
	ret = acr_r352_load_blobs(acr, sb);
	if (ret)
		return ret;

	nvkm_debug(&sb->subdev, "running HS load blob\n");
	ret = sb->func->run_blob(sb, acr->load_blob);
	if (ret)
		return ret;
	nvkm_debug(&sb->subdev, "HS load blob completed\n");

	sb->wpr_set = true;

	return 0;
}

/*
 * acr_r352_reset() - execute secure boot from the prepared state
 *
 * Load the HS bootloader and ask the falcon to run it. This will in turn
 * load the HS firmware and run it, so once the falcon stops all the managed
 * falcons should have their LS firmware loaded and be ready to run.
 */
static int
acr_r352_reset(struct nvkm_acr *_acr, struct nvkm_secboot *sb,
	       enum nvkm_secboot_falcon falcon)
{
	struct acr_r352 *acr = acr_r352(_acr);
	int ret;

	/*
	 * Dummy GM200 implementation: perform secure boot each time we are
	 * called on FECS. Since only FECS and GPCCS are managed and started
	 * together, this ought to be safe.
	 *
	 * Once we have proper PMU firmware and support, this will be changed
	 * to a proper call to the PMU method.
	 */
	if (falcon != NVKM_SECBOOT_FALCON_FECS)
		goto end;

	ret = acr_r352_shutdown(acr, sb);
	if (ret)
		return ret;

	acr_r352_bootstrap(acr, sb);
	if (ret)
		return ret;

end:
	acr->falcon_state[falcon] = RESET;
	return 0;
}

static int
acr_r352_start(struct nvkm_acr *_acr, struct nvkm_secboot *sb,
		    enum nvkm_secboot_falcon falcon)
{
	struct acr_r352 *acr = acr_r352(_acr);
	const struct nvkm_subdev *subdev = &sb->subdev;
	int base;

	switch (falcon) {
	case NVKM_SECBOOT_FALCON_FECS:
		base = 0x409000;
		break;
	case NVKM_SECBOOT_FALCON_GPCCS:
		base = 0x41a000;
		break;
	default:
		nvkm_error(subdev, "cannot start unhandled falcon!\n");
		return -EINVAL;
	}

	nvkm_wr32(subdev->device, base + 0x130, 0x00000002);
	acr->falcon_state[falcon] = RUNNING;

	return 0;
}

static int
acr_r352_fini(struct nvkm_acr *_acr, struct nvkm_secboot *sb, bool suspend)
{
	struct acr_r352 *acr = acr_r352(_acr);

	return acr_r352_shutdown(acr, sb);
}

static void
acr_r352_dtor(struct nvkm_acr *_acr)
{
	struct acr_r352 *acr = acr_r352(_acr);

	nvkm_gpuobj_del(&acr->unload_blob);

	kfree(acr->hsbl_blob);
	nvkm_gpuobj_del(&acr->load_blob);
	nvkm_gpuobj_del(&acr->ls_blob);

	kfree(acr);
}

const struct acr_r352_ls_func
acr_r352_ls_fecs_func = {
	.load = acr_ls_ucode_load_fecs,
	.generate_bl_desc = acr_r352_generate_flcn_bl_desc,
	.bl_desc_size = sizeof(struct acr_r352_flcn_bl_desc),
};

const struct acr_r352_ls_func
acr_r352_ls_gpccs_func = {
	.load = acr_ls_ucode_load_gpccs,
	.generate_bl_desc = acr_r352_generate_flcn_bl_desc,
	.bl_desc_size = sizeof(struct acr_r352_flcn_bl_desc),
	/* GPCCS will be loaded using PRI */
	.lhdr_flags = LSF_FLAG_FORCE_PRIV_LOAD,
};

const struct acr_r352_func
acr_r352_func = {
	.generate_hs_bl_desc = acr_r352_generate_hs_bl_desc,
	.hs_bl_desc_size = sizeof(struct acr_r352_flcn_bl_desc),
	.ls_func = {
		[NVKM_SECBOOT_FALCON_FECS] = &acr_r352_ls_fecs_func,
		[NVKM_SECBOOT_FALCON_GPCCS] = &acr_r352_ls_gpccs_func,
	},
};

static const struct nvkm_acr_func
acr_r352_base_func = {
	.dtor = acr_r352_dtor,
	.fini = acr_r352_fini,
	.load = acr_r352_load,
	.reset = acr_r352_reset,
	.start = acr_r352_start,
};

struct nvkm_acr *
acr_r352_new_(const struct acr_r352_func *func,
	      enum nvkm_secboot_falcon boot_falcon,
	      unsigned long managed_falcons)
{
	struct acr_r352 *acr;

	acr = kzalloc(sizeof(*acr), GFP_KERNEL);
	if (!acr)
		return ERR_PTR(-ENOMEM);

	acr->base.boot_falcon = boot_falcon;
	acr->base.managed_falcons = managed_falcons;
	acr->base.func = &acr_r352_base_func;
	acr->func = func;

	return &acr->base;
}

struct nvkm_acr *
acr_r352_new(unsigned long managed_falcons)
{
	return acr_r352_new_(&acr_r352_func, NVKM_SECBOOT_FALCON_PMU,
			     managed_falcons);
}
