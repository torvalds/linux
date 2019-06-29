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
#include "hs_ucode.h"

#include <core/gpuobj.h>
#include <core/firmware.h>
#include <engine/falcon.h>
#include <subdev/pmu.h>
#include <core/msgqueue.h>
#include <engine/sec2.h>

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
	u32 code_dma_base1;
	u32 data_dma_base1;
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

	base = wpr_addr + img->ucode_off + pdesc->app_start_offset;
	addr_code = (base + pdesc->app_resident_code_offset) >> 8;
	addr_data = (base + pdesc->app_resident_data_offset) >> 8;

	desc->ctx_dma = FALCON_DMAIDX_UCODE;
	desc->code_dma_base = lower_32_bits(addr_code);
	desc->code_dma_base1 = upper_32_bits(addr_code);
	desc->non_sec_code_off = pdesc->app_resident_code_offset;
	desc->non_sec_code_size = pdesc->app_resident_code_size;
	desc->code_entry_point = pdesc->app_imem_entry;
	desc->data_dma_base = lower_32_bits(addr_data);
	desc->data_dma_base1 = upper_32_bits(addr_data);
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

/**
 * struct acr_r352_lsf_lsb_header - LS firmware header
 * @signature:		signature to verify the firmware against
 * @ucode_off:		offset of the ucode blob in the WPR region. The ucode
 *                      blob contains the bootloader, code and data of the
 *                      LS falcon
 * @ucode_size:		size of the ucode blob, including bootloader
 * @data_size:		size of the ucode blob data
 * @bl_code_size:	size of the bootloader code
 * @bl_imem_off:	offset in imem of the bootloader
 * @bl_data_off:	offset of the bootloader data in WPR region
 * @bl_data_size:	size of the bootloader data
 * @app_code_off:	offset of the app code relative to ucode_off
 * @app_code_size:	size of the app code
 * @app_data_off:	offset of the app data relative to ucode_off
 * @app_data_size:	size of the app data
 * @flags:		flags for the secure bootloader
 *
 * This structure is written into the WPR region for each managed falcon. Each
 * instance is referenced by the lsb_offset member of the corresponding
 * lsf_wpr_header.
 */
struct acr_r352_lsf_lsb_header {
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
 * struct acr_r352_lsf_wpr_header - LS blob WPR Header
 * @falcon_id:		LS falcon ID
 * @lsb_offset:		offset of the lsb_lsf_header in the WPR region
 * @bootstrap_owner:	secure falcon reponsible for bootstrapping the LS falcon
 * @lazy_bootstrap:	skip bootstrapping by ACR
 * @status:		bootstrapping status
 *
 * An array of these is written at the beginning of the WPR region, one for
 * each managed falcon. The array is terminated by an instance which falcon_id
 * is LSF_FALCON_ID_INVALID.
 */
struct acr_r352_lsf_wpr_header {
	u32 falcon_id;
	u32 lsb_offset;
	u32 bootstrap_owner;
	u32 lazy_bootstrap;
	u32 status;
#define LSF_IMAGE_STATUS_NONE				0
#define LSF_IMAGE_STATUS_COPY				1
#define LSF_IMAGE_STATUS_VALIDATION_CODE_FAILED		2
#define LSF_IMAGE_STATUS_VALIDATION_DATA_FAILED		3
#define LSF_IMAGE_STATUS_VALIDATION_DONE		4
#define LSF_IMAGE_STATUS_VALIDATION_SKIPPED		5
#define LSF_IMAGE_STATUS_BOOTSTRAP_READY		6
};

/**
 * struct ls_ucode_img_r352 - ucode image augmented with r352 headers
 */
struct ls_ucode_img_r352 {
	struct ls_ucode_img base;

	const struct acr_r352_lsf_func *func;

	struct acr_r352_lsf_wpr_header wpr_header;
	struct acr_r352_lsf_lsb_header lsb_header;
};
#define ls_ucode_img_r352(i) container_of(i, struct ls_ucode_img_r352, base)

/**
 * ls_ucode_img_load() - create a lsf_ucode_img and load it
 */
struct ls_ucode_img *
acr_r352_ls_ucode_img_load(const struct acr_r352 *acr,
			   const struct nvkm_secboot *sb,
			   enum nvkm_secboot_falcon falcon_id)
{
	const struct nvkm_subdev *subdev = acr->base.subdev;
	const struct acr_r352_ls_func *func = acr->func->ls_func[falcon_id];
	struct ls_ucode_img_r352 *img;
	int ret;

	img = kzalloc(sizeof(*img), GFP_KERNEL);
	if (!img)
		return ERR_PTR(-ENOMEM);

	img->base.falcon_id = falcon_id;

	ret = func->load(sb, func->version_max, &img->base);
	if (ret < 0) {
		kfree(img->base.ucode_data);
		kfree(img->base.sig);
		kfree(img);
		return ERR_PTR(ret);
	}

	img->func = func->version[ret];

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

/**
 * acr_r352_ls_img_fill_headers - fill the WPR and LSB headers of an image
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
acr_r352_ls_img_fill_headers(struct acr_r352 *acr,
			     struct ls_ucode_img_r352 *img, u32 offset)
{
	struct ls_ucode_img *_img = &img->base;
	struct acr_r352_lsf_wpr_header *whdr = &img->wpr_header;
	struct acr_r352_lsf_lsb_header *lhdr = &img->lsb_header;
	struct ls_ucode_img_desc *desc = &_img->ucode_desc;
	const struct acr_r352_lsf_func *func = img->func;

	/* Fill WPR header */
	whdr->falcon_id = _img->falcon_id;
	whdr->bootstrap_owner = acr->base.boot_falcon;
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

/**
 * acr_r352_ls_fill_headers - fill WPR and LSB headers of all managed images
 */
int
acr_r352_ls_fill_headers(struct acr_r352 *acr, struct list_head *imgs)
{
	struct ls_ucode_img_r352 *img;
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
		offset = acr_r352_ls_img_fill_headers(acr, img, offset);
	}

	return offset;
}

/**
 * acr_r352_ls_write_wpr - write the WPR blob contents
 */
int
acr_r352_ls_write_wpr(struct acr_r352 *acr, struct list_head *imgs,
		      struct nvkm_gpuobj *wpr_blob, u64 wpr_addr)
{
	struct ls_ucode_img *_img;
	u32 pos = 0;
	u32 max_desc_size = 0;
	u8 *gdesc;

	/* Figure out how large we need gdesc to be. */
	list_for_each_entry(_img, imgs, node) {
		struct ls_ucode_img_r352 *img = ls_ucode_img_r352(_img);
		const struct acr_r352_lsf_func *ls_func = img->func;

		max_desc_size = max(max_desc_size, ls_func->bl_desc_size);
	}

	gdesc = kmalloc(max_desc_size, GFP_KERNEL);
	if (!gdesc)
		return -ENOMEM;

	nvkm_kmap(wpr_blob);

	list_for_each_entry(_img, imgs, node) {
		struct ls_ucode_img_r352 *img = ls_ucode_img_r352(_img);
		const struct acr_r352_lsf_func *ls_func = img->func;

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

	kfree(gdesc);

	return 0;
}

/* Both size and address of WPR need to be 256K-aligned */
#define WPR_ALIGNMENT	0x40000
/**
 * acr_r352_prepare_ls_blob() - prepare the LS blob
 *
 * For each securely managed falcon, load the FW, signatures and bootloaders and
 * prepare a ucode blob. Then, compute the offsets in the WPR region for each
 * blob, and finally write the headers and ucode blobs into a GPU object that
 * will be copied into the WPR region by the HS firmware.
 */
static int
acr_r352_prepare_ls_blob(struct acr_r352 *acr, struct nvkm_secboot *sb)
{
	const struct nvkm_subdev *subdev = acr->base.subdev;
	struct list_head imgs;
	struct ls_ucode_img *img, *t;
	unsigned long managed_falcons = acr->base.managed_falcons;
	u64 wpr_addr = sb->wpr_addr;
	u32 wpr_size = sb->wpr_size;
	int managed_count = 0;
	u32 image_wpr_size, ls_blob_size;
	int falcon_id;
	int ret;

	INIT_LIST_HEAD(&imgs);

	/* Load all LS blobs */
	for_each_set_bit(falcon_id, &managed_falcons, NVKM_SECBOOT_FALCON_END) {
		struct ls_ucode_img *img;

		img = acr->func->ls_ucode_img_load(acr, sb, falcon_id);
		if (IS_ERR(img)) {
			if (acr->base.optional_falcons & BIT(falcon_id)) {
				managed_falcons &= ~BIT(falcon_id);
				nvkm_info(subdev, "skipping %s falcon...\n",
					  nvkm_secboot_falcon_name[falcon_id]);
				continue;
			}
			ret = PTR_ERR(img);
			goto cleanup;
		}

		list_add_tail(&img->node, &imgs);
		managed_count++;
	}

	/* Commit the actual list of falcons we will manage from now on */
	acr->base.managed_falcons = managed_falcons;

	/*
	 * If the boot falcon has a firmare, let it manage the bootstrap of other
	 * falcons.
	 */
	if (acr->func->ls_func[acr->base.boot_falcon] &&
	    (managed_falcons & BIT(acr->base.boot_falcon))) {
		for_each_set_bit(falcon_id, &managed_falcons,
				 NVKM_SECBOOT_FALCON_END) {
			if (falcon_id == acr->base.boot_falcon)
				continue;

			acr->lazy_bootstrap |= BIT(falcon_id);
		}
	}

	/*
	 * Fill the WPR and LSF headers with the right offsets and compute
	 * required WPR size
	 */
	image_wpr_size = acr->func->ls_fill_headers(acr, &imgs);
	image_wpr_size = ALIGN(image_wpr_size, WPR_ALIGNMENT);

	ls_blob_size = image_wpr_size;

	/*
	 * If we need a shadow area, allocate twice the size and use the
	 * upper half as WPR
	 */
	if (wpr_size == 0 && acr->func->shadow_blob)
		ls_blob_size *= 2;

	/* Allocate GPU object that will contain the WPR region */
	ret = nvkm_gpuobj_new(subdev->device, ls_blob_size, WPR_ALIGNMENT,
			      false, NULL, &acr->ls_blob);
	if (ret)
		goto cleanup;

	nvkm_debug(subdev, "%d managed LS falcons, WPR size is %d bytes\n",
		    managed_count, image_wpr_size);

	/* If WPR address and size are not fixed, set them to fit the LS blob */
	if (wpr_size == 0) {
		wpr_addr = acr->ls_blob->addr;
		if (acr->func->shadow_blob)
			wpr_addr += acr->ls_blob->size / 2;

		wpr_size = image_wpr_size;
	/*
	 * But if the WPR region is set by the bootloader, it is illegal for
	 * the HS blob to be larger than this region.
	 */
	} else if (image_wpr_size > wpr_size) {
		nvkm_error(subdev, "WPR region too small for FW blob!\n");
		nvkm_error(subdev, "required: %dB\n", image_wpr_size);
		nvkm_error(subdev, "available: %dB\n", wpr_size);
		ret = -ENOSPC;
		goto cleanup;
	}

	/* Write LS blob */
	ret = acr->func->ls_write_wpr(acr, &imgs, acr->ls_blob, wpr_addr);
	if (ret)
		nvkm_gpuobj_del(&acr->ls_blob);

cleanup:
	list_for_each_entry_safe(img, t, &imgs, node) {
		kfree(img->ucode_data);
		kfree(img->sig);
		kfree(img);
	}

	return ret;
}




void
acr_r352_fixup_hs_desc(struct acr_r352 *acr, struct nvkm_secboot *sb,
		       void *_desc)
{
	struct hsflcn_acr_desc *desc = _desc;
	struct nvkm_gpuobj *ls_blob = acr->ls_blob;

	/* WPR region information if WPR is not fixed */
	if (sb->wpr_size == 0) {
		u64 wpr_start = ls_blob->addr;
		u64 wpr_end = wpr_start + ls_blob->size;

		desc->wpr_region_id = 1;
		desc->regions.no_regions = 2;
		desc->regions.region_props[0].start_addr = wpr_start >> 8;
		desc->regions.region_props[0].end_addr = wpr_end >> 8;
		desc->regions.region_props[0].region_id = 1;
		desc->regions.region_props[0].read_mask = 0xf;
		desc->regions.region_props[0].write_mask = 0xc;
		desc->regions.region_props[0].client_mask = 0x2;
	} else {
		desc->ucode_blob_base = ls_blob->addr;
		desc->ucode_blob_size = ls_blob->size;
	}
}

static void
acr_r352_generate_hs_bl_desc(const struct hsf_load_header *hdr, void *_bl_desc,
			     u64 offset)
{
	struct acr_r352_flcn_bl_desc *bl_desc = _bl_desc;
	u64 addr_code, addr_data;

	addr_code = offset >> 8;
	addr_data = (offset + hdr->data_dma_base) >> 8;

	bl_desc->ctx_dma = FALCON_DMAIDX_VIRT;
	bl_desc->code_dma_base = lower_32_bits(addr_code);
	bl_desc->non_sec_code_off = hdr->non_sec_code_off;
	bl_desc->non_sec_code_size = hdr->non_sec_code_size;
	bl_desc->sec_code_off = hsf_load_header_app_off(hdr, 0);
	bl_desc->sec_code_size = hsf_load_header_app_size(hdr, 0);
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

	acr_image = hs_ucode_load_blob(subdev, sb->boot_falcon, fw);
	if (IS_ERR(acr_image))
		return PTR_ERR(acr_image);

	hsbin_hdr = acr_image;
	fw_hdr = acr_image + hsbin_hdr->header_offset;
	load_hdr = acr_image + fw_hdr->hdr_offset;
	acr_data = acr_image + hsbin_hdr->data_offset;

	/* Patch descriptor with WPR information? */
	if (patch) {
		struct hsflcn_acr_desc *desc;

		desc = acr_data + load_hdr->data_dma_base;
		acr->func->fixup_hs_desc(acr, sb, desc);
	}

	if (load_hdr->num_apps > ACR_R352_MAX_APPS) {
		nvkm_error(subdev, "more apps (%d) than supported (%d)!",
			   load_hdr->num_apps, ACR_R352_MAX_APPS);
		ret = -EINVAL;
		goto cleanup;
	}
	memcpy(load_header, load_hdr, sizeof(*load_header) +
			  (sizeof(load_hdr->apps[0]) * 2 * load_hdr->num_apps));

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
	struct nvkm_subdev *subdev = &sb->subdev;
	int ret;

	/* Firmware already loaded? */
	if (acr->firmware_ok)
		return 0;

	/* Load and prepare the managed falcon's firmwares */
	ret = acr_r352_prepare_ls_blob(acr, sb);
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
		acr->hsbl_blob = nvkm_acr_load_firmware(subdev, "acr/bl", 0);
		if (IS_ERR(acr->hsbl_blob)) {
			ret = PTR_ERR(acr->hsbl_blob);
			acr->hsbl_blob = NULL;
			return ret;
		}

		if (acr->base.boot_falcon != NVKM_SECBOOT_FALCON_PMU) {
			acr->hsbl_unload_blob = nvkm_acr_load_firmware(subdev,
							    "acr/unload_bl", 0);
			if (IS_ERR(acr->hsbl_unload_blob)) {
				ret = PTR_ERR(acr->hsbl_unload_blob);
				acr->hsbl_unload_blob = NULL;
				return ret;
			}
		} else {
			acr->hsbl_unload_blob = acr->hsbl_blob;
		}
	}

	acr->firmware_ok = true;
	nvkm_debug(&sb->subdev, "LS blob successfully created\n");

	return 0;
}

/**
 * acr_r352_load() - prepare HS falcon to run the specified blob, mapped.
 *
 * Returns the start address to use, or a negative error value.
 */
static int
acr_r352_load(struct nvkm_acr *_acr, struct nvkm_falcon *falcon,
	      struct nvkm_gpuobj *blob, u64 offset)
{
	struct acr_r352 *acr = acr_r352(_acr);
	const u32 bl_desc_size = acr->func->hs_bl_desc_size;
	const struct hsf_load_header *load_hdr;
	struct fw_bin_header *bl_hdr;
	struct fw_bl_desc *hsbl_desc;
	void *bl, *blob_data, *hsbl_code, *hsbl_data;
	u32 code_size;
	u8 *bl_desc;

	bl_desc = kzalloc(bl_desc_size, GFP_KERNEL);
	if (!bl_desc)
		return -ENOMEM;

	/* Find the bootloader descriptor for our blob and copy it */
	if (blob == acr->load_blob) {
		load_hdr = &acr->load_bl_header;
		bl = acr->hsbl_blob;
	} else if (blob == acr->unload_blob) {
		load_hdr = &acr->unload_bl_header;
		bl = acr->hsbl_unload_blob;
	} else {
		nvkm_error(_acr->subdev, "invalid secure boot blob!\n");
		kfree(bl_desc);
		return -EINVAL;
	}

	bl_hdr = bl;
	hsbl_desc = bl + bl_hdr->header_offset;
	blob_data = bl + bl_hdr->data_offset;
	hsbl_code = blob_data + hsbl_desc->code_off;
	hsbl_data = blob_data + hsbl_desc->data_off;
	code_size = ALIGN(hsbl_desc->code_size, 256);

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

	kfree(bl_desc);
	return hsbl_desc->start_tag << 8;
}

static int
acr_r352_shutdown(struct acr_r352 *acr, struct nvkm_secboot *sb)
{
	struct nvkm_subdev *subdev = &sb->subdev;
	int i;

	/* Run the unload blob to unprotect the WPR region */
	if (acr->unload_blob && sb->wpr_set) {
		int ret;

		nvkm_debug(subdev, "running HS unload blob\n");
		ret = sb->func->run_blob(sb, acr->unload_blob, sb->halt_falcon);
		if (ret < 0)
			return ret;
		/*
		 * Unload blob will return this error code - it is not an error
		 * and the expected behavior on RM as well
		 */
		if (ret && ret != 0x1d) {
			nvkm_error(subdev, "HS unload failed, ret 0x%08x\n", ret);
			return -EINVAL;
		}
		nvkm_debug(subdev, "HS unload blob completed\n");
	}

	for (i = 0; i < NVKM_SECBOOT_FALCON_END; i++)
		acr->falcon_state[i] = NON_SECURE;

	sb->wpr_set = false;

	return 0;
}

/**
 * Check if the WPR region has been indeed set by the ACR firmware, and
 * matches where it should be.
 */
static bool
acr_r352_wpr_is_set(const struct acr_r352 *acr, const struct nvkm_secboot *sb)
{
	const struct nvkm_subdev *subdev = &sb->subdev;
	const struct nvkm_device *device = subdev->device;
	u64 wpr_lo, wpr_hi;
	u64 wpr_range_lo, wpr_range_hi;

	nvkm_wr32(device, 0x100cd4, 0x2);
	wpr_lo = (nvkm_rd32(device, 0x100cd4) & ~0xff);
	wpr_lo <<= 8;
	nvkm_wr32(device, 0x100cd4, 0x3);
	wpr_hi = (nvkm_rd32(device, 0x100cd4) & ~0xff);
	wpr_hi <<= 8;

	if (sb->wpr_size != 0) {
		wpr_range_lo = sb->wpr_addr;
		wpr_range_hi = wpr_range_lo + sb->wpr_size;
	} else {
		wpr_range_lo = acr->ls_blob->addr;
		wpr_range_hi = wpr_range_lo + acr->ls_blob->size;
	}

	return (wpr_lo >= wpr_range_lo && wpr_lo < wpr_range_hi &&
		wpr_hi > wpr_range_lo && wpr_hi <= wpr_range_hi);
}

static int
acr_r352_bootstrap(struct acr_r352 *acr, struct nvkm_secboot *sb)
{
	const struct nvkm_subdev *subdev = &sb->subdev;
	unsigned long managed_falcons = acr->base.managed_falcons;
	int falcon_id;
	int ret;

	if (sb->wpr_set)
		return 0;

	/* Make sure all blobs are ready */
	ret = acr_r352_load_blobs(acr, sb);
	if (ret)
		return ret;

	nvkm_debug(subdev, "running HS load blob\n");
	ret = sb->func->run_blob(sb, acr->load_blob, sb->boot_falcon);
	/* clear halt interrupt */
	nvkm_falcon_clear_interrupt(sb->boot_falcon, 0x10);
	sb->wpr_set = acr_r352_wpr_is_set(acr, sb);
	if (ret < 0) {
		return ret;
	} else if (ret > 0) {
		nvkm_error(subdev, "HS load failed, ret 0x%08x\n", ret);
		return -EINVAL;
	}
	nvkm_debug(subdev, "HS load blob completed\n");
	/* WPR must be set at this point */
	if (!sb->wpr_set) {
		nvkm_error(subdev, "ACR blob completed but WPR not set!\n");
		return -EINVAL;
	}

	/* Run LS firmwares post_run hooks */
	for_each_set_bit(falcon_id, &managed_falcons, NVKM_SECBOOT_FALCON_END) {
		const struct acr_r352_ls_func *func =
						  acr->func->ls_func[falcon_id];

		if (func->post_run) {
			ret = func->post_run(&acr->base, sb);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/**
 * acr_r352_reset_nopmu - dummy reset method when no PMU firmware is loaded
 *
 * Reset is done by re-executing secure boot from scratch, with lazy bootstrap
 * disabled. This has the effect of making all managed falcons ready-to-run.
 */
static int
acr_r352_reset_nopmu(struct acr_r352 *acr, struct nvkm_secboot *sb,
		     unsigned long falcon_mask)
{
	int falcon;
	int ret;

	/*
	 * Perform secure boot each time we are called on FECS. Since only FECS
	 * and GPCCS are managed and started together, this ought to be safe.
	 */
	if (!(falcon_mask & BIT(NVKM_SECBOOT_FALCON_FECS)))
		goto end;

	ret = acr_r352_shutdown(acr, sb);
	if (ret)
		return ret;

	ret = acr_r352_bootstrap(acr, sb);
	if (ret)
		return ret;

end:
	for_each_set_bit(falcon, &falcon_mask, NVKM_SECBOOT_FALCON_END) {
		acr->falcon_state[falcon] = RESET;
	}
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
	       unsigned long falcon_mask)
{
	struct acr_r352 *acr = acr_r352(_acr);
	struct nvkm_msgqueue *queue;
	int falcon;
	bool wpr_already_set = sb->wpr_set;
	int ret;

	/* Make sure secure boot is performed */
	ret = acr_r352_bootstrap(acr, sb);
	if (ret)
		return ret;

	/* No PMU interface? */
	if (!nvkm_secboot_is_managed(sb, _acr->boot_falcon)) {
		/* Redo secure boot entirely if it was already done */
		if (wpr_already_set)
			return acr_r352_reset_nopmu(acr, sb, falcon_mask);
		/* Else return the result of the initial invokation */
		else
			return ret;
	}

	switch (_acr->boot_falcon) {
	case NVKM_SECBOOT_FALCON_PMU:
		queue = sb->subdev.device->pmu->queue;
		break;
	case NVKM_SECBOOT_FALCON_SEC2:
		queue = sb->subdev.device->sec2->queue;
		break;
	default:
		return -EINVAL;
	}

	/* Otherwise just ask the LS firmware to reset the falcon */
	for_each_set_bit(falcon, &falcon_mask, NVKM_SECBOOT_FALCON_END)
		nvkm_debug(&sb->subdev, "resetting %s falcon\n",
			   nvkm_secboot_falcon_name[falcon]);
	ret = nvkm_msgqueue_acr_boot_falcons(queue, falcon_mask);
	if (ret) {
		nvkm_error(&sb->subdev, "error during falcon reset: %d\n", ret);
		return ret;
	}
	nvkm_debug(&sb->subdev, "falcon reset done\n");

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

	if (_acr->boot_falcon != NVKM_SECBOOT_FALCON_PMU)
		kfree(acr->hsbl_unload_blob);
	kfree(acr->hsbl_blob);
	nvkm_gpuobj_del(&acr->load_blob);
	nvkm_gpuobj_del(&acr->ls_blob);

	kfree(acr);
}

static const struct acr_r352_lsf_func
acr_r352_ls_fecs_func_0 = {
	.generate_bl_desc = acr_r352_generate_flcn_bl_desc,
	.bl_desc_size = sizeof(struct acr_r352_flcn_bl_desc),
};

const struct acr_r352_ls_func
acr_r352_ls_fecs_func = {
	.load = acr_ls_ucode_load_fecs,
	.version_max = 0,
	.version = {
		&acr_r352_ls_fecs_func_0,
	}
};

static const struct acr_r352_lsf_func
acr_r352_ls_gpccs_func_0 = {
	.generate_bl_desc = acr_r352_generate_flcn_bl_desc,
	.bl_desc_size = sizeof(struct acr_r352_flcn_bl_desc),
	/* GPCCS will be loaded using PRI */
	.lhdr_flags = LSF_FLAG_FORCE_PRIV_LOAD,
};

const struct acr_r352_ls_func
acr_r352_ls_gpccs_func = {
	.load = acr_ls_ucode_load_gpccs,
	.version_max = 0,
	.version = {
		&acr_r352_ls_gpccs_func_0,
	}
};



/**
 * struct acr_r352_pmu_bl_desc - PMU DMEM bootloader descriptor
 * @dma_idx:		DMA context to be used by BL while loading code/data
 * @code_dma_base:	256B-aligned Physical FB Address where code is located
 * @total_code_size:	total size of the code part in the ucode
 * @code_size_to_load:	size of the code part to load in PMU IMEM.
 * @code_entry_point:	entry point in the code.
 * @data_dma_base:	Physical FB address where data part of ucode is located
 * @data_size:		Total size of the data portion.
 * @overlay_dma_base:	Physical Fb address for resident code present in ucode
 * @argc:		Total number of args
 * @argv:		offset where args are copied into PMU's DMEM.
 *
 * Structure used by the PMU bootloader to load the rest of the code
 */
struct acr_r352_pmu_bl_desc {
	u32 dma_idx;
	u32 code_dma_base;
	u32 code_size_total;
	u32 code_size_to_load;
	u32 code_entry_point;
	u32 data_dma_base;
	u32 data_size;
	u32 overlay_dma_base;
	u32 argc;
	u32 argv;
	u16 code_dma_base1;
	u16 data_dma_base1;
	u16 overlay_dma_base1;
};

/**
 * acr_r352_generate_pmu_bl_desc() - populate a DMEM BL descriptor for PMU LS image
 *
 */
static void
acr_r352_generate_pmu_bl_desc(const struct nvkm_acr *acr,
			      const struct ls_ucode_img *img, u64 wpr_addr,
			      void *_desc)
{
	const struct ls_ucode_img_desc *pdesc = &img->ucode_desc;
	const struct nvkm_pmu *pmu = acr->subdev->device->pmu;
	struct acr_r352_pmu_bl_desc *desc = _desc;
	u64 base;
	u64 addr_code;
	u64 addr_data;
	u32 addr_args;

	base = wpr_addr + img->ucode_off + pdesc->app_start_offset;
	addr_code = (base + pdesc->app_resident_code_offset) >> 8;
	addr_data = (base + pdesc->app_resident_data_offset) >> 8;
	addr_args = pmu->falcon->data.limit;
	addr_args -= NVKM_MSGQUEUE_CMDLINE_SIZE;

	desc->dma_idx = FALCON_DMAIDX_UCODE;
	desc->code_dma_base = lower_32_bits(addr_code);
	desc->code_dma_base1 = upper_32_bits(addr_code);
	desc->code_size_total = pdesc->app_size;
	desc->code_size_to_load = pdesc->app_resident_code_size;
	desc->code_entry_point = pdesc->app_imem_entry;
	desc->data_dma_base = lower_32_bits(addr_data);
	desc->data_dma_base1 = upper_32_bits(addr_data);
	desc->data_size = pdesc->app_resident_data_size;
	desc->overlay_dma_base = lower_32_bits(addr_code);
	desc->overlay_dma_base1 = upper_32_bits(addr_code);
	desc->argc = 1;
	desc->argv = addr_args;
}

static const struct acr_r352_lsf_func
acr_r352_ls_pmu_func_0 = {
	.generate_bl_desc = acr_r352_generate_pmu_bl_desc,
	.bl_desc_size = sizeof(struct acr_r352_pmu_bl_desc),
};

static const struct acr_r352_ls_func
acr_r352_ls_pmu_func = {
	.load = acr_ls_ucode_load_pmu,
	.post_run = acr_ls_pmu_post_run,
	.version_max = 0,
	.version = {
		&acr_r352_ls_pmu_func_0,
	}
};

const struct acr_r352_func
acr_r352_func = {
	.fixup_hs_desc = acr_r352_fixup_hs_desc,
	.generate_hs_bl_desc = acr_r352_generate_hs_bl_desc,
	.hs_bl_desc_size = sizeof(struct acr_r352_flcn_bl_desc),
	.ls_ucode_img_load = acr_r352_ls_ucode_img_load,
	.ls_fill_headers = acr_r352_ls_fill_headers,
	.ls_write_wpr = acr_r352_ls_write_wpr,
	.ls_func = {
		[NVKM_SECBOOT_FALCON_FECS] = &acr_r352_ls_fecs_func,
		[NVKM_SECBOOT_FALCON_GPCCS] = &acr_r352_ls_gpccs_func,
		[NVKM_SECBOOT_FALCON_PMU] = &acr_r352_ls_pmu_func,
	},
};

static const struct nvkm_acr_func
acr_r352_base_func = {
	.dtor = acr_r352_dtor,
	.fini = acr_r352_fini,
	.load = acr_r352_load,
	.reset = acr_r352_reset,
};

struct nvkm_acr *
acr_r352_new_(const struct acr_r352_func *func,
	      enum nvkm_secboot_falcon boot_falcon,
	      unsigned long managed_falcons)
{
	struct acr_r352 *acr;
	int i;

	/* Check that all requested falcons are supported */
	for_each_set_bit(i, &managed_falcons, NVKM_SECBOOT_FALCON_END) {
		if (!func->ls_func[i])
			return ERR_PTR(-ENOTSUPP);
	}

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
