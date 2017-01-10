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

/*
 * Secure boot is the process by which NVIDIA-signed firmware is loaded into
 * some of the falcons of a GPU. For production devices this is the only way
 * for the firmware to access useful (but sensitive) registers.
 *
 * A Falcon microprocessor supporting advanced security modes can run in one of
 * three modes:
 *
 * - Non-secure (NS). In this mode, functionality is similar to Falcon
 *   architectures before security modes were introduced (pre-Maxwell), but
 *   capability is restricted. In particular, certain registers may be
 *   inaccessible for reads and/or writes, and physical memory access may be
 *   disabled (on certain Falcon instances). This is the only possible mode that
 *   can be used if you don't have microcode cryptographically signed by NVIDIA.
 *
 * - Heavy Secure (HS). In this mode, the microprocessor is a black box - it's
 *   not possible to read or write any Falcon internal state or Falcon registers
 *   from outside the Falcon (for example, from the host system). The only way
 *   to enable this mode is by loading microcode that has been signed by NVIDIA.
 *   (The loading process involves tagging the IMEM block as secure, writing the
 *   signature into a Falcon register, and starting execution. The hardware will
 *   validate the signature, and if valid, grant HS privileges.)
 *
 * - Light Secure (LS). In this mode, the microprocessor has more privileges
 *   than NS but fewer than HS. Some of the microprocessor state is visible to
 *   host software to ease debugging. The only way to enable this mode is by HS
 *   microcode enabling LS mode. Some privileges available to HS mode are not
 *   available here. LS mode is introduced in GM20x.
 *
 * Secure boot consists in temporarily switching a HS-capable falcon (typically
 * PMU) into HS mode in order to validate the LS firmwares of managed falcons,
 * load them, and switch managed falcons into LS mode. Once secure boot
 * completes, no falcon remains in HS mode.
 *
 * Secure boot requires a write-protected memory region (WPR) which can only be
 * written by the secure falcon. On dGPU, the driver sets up the WPR region in
 * video memory. On Tegra, it is set up by the bootloader and its location and
 * size written into memory controller registers.
 *
 * The secure boot process takes place as follows:
 *
 * 1) A LS blob is constructed that contains all the LS firmwares we want to
 *    load, along with their signatures and bootloaders.
 *
 * 2) A HS blob (also called ACR) is created that contains the signed HS
 *    firmware in charge of loading the LS firmwares into their respective
 *    falcons.
 *
 * 3) The HS blob is loaded (via its own bootloader) and executed on the
 *    HS-capable falcon. It authenticates itself, switches the secure falcon to
 *    HS mode and setup the WPR region around the LS blob (dGPU) or copies the
 *    LS blob into the WPR region (Tegra).
 *
 * 4) The LS blob is now secure from all external tampering. The HS falcon
 *    checks the signatures of the LS firmwares and, if valid, switches the
 *    managed falcons to LS mode and makes them ready to run the LS firmware.
 *
 * 5) The managed falcons remain in LS mode and can be started.
 *
 */

#include "priv.h"

#include <core/gpuobj.h>
#include <core/firmware.h>
#include <subdev/fb.h>

enum {
	FALCON_DMAIDX_UCODE		= 0,
	FALCON_DMAIDX_VIRT		= 1,
	FALCON_DMAIDX_PHYS_VID		= 2,
	FALCON_DMAIDX_PHYS_SYS_COH	= 3,
	FALCON_DMAIDX_PHYS_SYS_NCOH	= 4,
};

/**
 * struct fw_bin_header - header of firmware files
 * @bin_magic:		always 0x3b1d14f0
 * @bin_ver:		version of the bin format
 * @bin_size:		entire image size including this header
 * @header_offset:	offset of the firmware/bootloader header in the file
 * @data_offset:	offset of the firmware/bootloader payload in the file
 * @data_size:		size of the payload
 *
 * This header is located at the beginning of the HS firmware and HS bootloader
 * files, to describe where the headers and data can be found.
 */
struct fw_bin_header {
	u32 bin_magic;
	u32 bin_ver;
	u32 bin_size;
	u32 header_offset;
	u32 data_offset;
	u32 data_size;
};

/**
 * struct fw_bl_desc - firmware bootloader descriptor
 * @start_tag:		starting tag of bootloader
 * @desc_dmem_load_off:	DMEM offset of flcn_bl_dmem_desc
 * @code_off:		offset of code section
 * @code_size:		size of code section
 * @data_off:		offset of data section
 * @data_size:		size of data section
 *
 * This structure is embedded in bootloader firmware files at to describe the
 * IMEM and DMEM layout expected by the bootloader.
 */
struct fw_bl_desc {
	u32 start_tag;
	u32 dmem_load_off;
	u32 code_off;
	u32 code_size;
	u32 data_off;
	u32 data_size;
};


/*
 *
 * LS blob structures
 *
 */

/**
 * struct lsf_ucode_desc - LS falcon signatures
 * @prd_keys:		signature to use when the GPU is in production mode
 * @dgb_keys:		signature to use when the GPU is in debug mode
 * @b_prd_present:	whether the production key is present
 * @b_dgb_present:	whether the debug key is present
 * @falcon_id:		ID of the falcon the ucode applies to
 *
 * Directly loaded from a signature file.
 */
struct lsf_ucode_desc {
	u8  prd_keys[2][16];
	u8  dbg_keys[2][16];
	u32 b_prd_present;
	u32 b_dbg_present;
	u32 falcon_id;
};

/**
 * struct lsf_lsb_header - LS firmware header
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
struct lsf_lsb_header {
	struct lsf_ucode_desc signature;
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
#define LSF_FLAG_LOAD_CODE_AT_0		1
#define LSF_FLAG_DMACTL_REQ_CTX		4
#define LSF_FLAG_FORCE_PRIV_LOAD	8
};

/**
 * struct lsf_wpr_header - LS blob WPR Header
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
struct lsf_wpr_header {
	u32  falcon_id;
	u32  lsb_offset;
	u32  bootstrap_owner;
	u32  lazy_bootstrap;
	u32  status;
#define LSF_IMAGE_STATUS_NONE				0
#define LSF_IMAGE_STATUS_COPY				1
#define LSF_IMAGE_STATUS_VALIDATION_CODE_FAILED		2
#define LSF_IMAGE_STATUS_VALIDATION_DATA_FAILED		3
#define LSF_IMAGE_STATUS_VALIDATION_DONE		4
#define LSF_IMAGE_STATUS_VALIDATION_SKIPPED		5
#define LSF_IMAGE_STATUS_BOOTSTRAP_READY		6
};


/**
 * struct ls_ucode_img_desc - descriptor of firmware image
 * @descriptor_size:		size of this descriptor
 * @image_size:			size of the whole image
 * @bootloader_start_offset:	start offset of the bootloader in ucode image
 * @bootloader_size:		size of the bootloader
 * @bootloader_imem_offset:	start off set of the bootloader in IMEM
 * @bootloader_entry_point:	entry point of the bootloader in IMEM
 * @app_start_offset:		start offset of the LS firmware
 * @app_size:			size of the LS firmware's code and data
 * @app_imem_offset:		offset of the app in IMEM
 * @app_imem_entry:		entry point of the app in IMEM
 * @app_dmem_offset:		offset of the data in DMEM
 * @app_resident_code_offset:	offset of app code from app_start_offset
 * @app_resident_code_size:	size of the code
 * @app_resident_data_offset:	offset of data from app_start_offset
 * @app_resident_data_size:	size of data
 *
 * A firmware image contains the code, data, and bootloader of a given LS
 * falcon in a single blob. This structure describes where everything is.
 *
 * This can be generated from a (bootloader, code, data) set if they have
 * been loaded separately, or come directly from a file.
 */
struct ls_ucode_img_desc {
	u32 descriptor_size;
	u32 image_size;
	u32 tools_version;
	u32 app_version;
	char date[64];
	u32 bootloader_start_offset;
	u32 bootloader_size;
	u32 bootloader_imem_offset;
	u32 bootloader_entry_point;
	u32 app_start_offset;
	u32 app_size;
	u32 app_imem_offset;
	u32 app_imem_entry;
	u32 app_dmem_offset;
	u32 app_resident_code_offset;
	u32 app_resident_code_size;
	u32 app_resident_data_offset;
	u32 app_resident_data_size;
	u32 nb_overlays;
	struct {u32 start; u32 size; } load_ovl[64];
	u32 compressed;
};

/**
 * struct ls_ucode_img - temporary storage for loaded LS firmwares
 * @node:		to link within lsf_ucode_mgr
 * @falcon_id:		ID of the falcon this LS firmware is for
 * @ucode_desc:		loaded or generated map of ucode_data
 * @ucode_header:	header of the firmware
 * @ucode_data:		firmware payload (code and data)
 * @ucode_size:		size in bytes of data in ucode_data
 * @wpr_header:		WPR header to be written to the LS blob
 * @lsb_header:		LSB header to be written to the LS blob
 *
 * Preparing the WPR LS blob requires information about all the LS firmwares
 * (size, etc) to be known. This structure contains all the data of one LS
 * firmware.
 */
struct ls_ucode_img {
	struct list_head node;
	enum nvkm_secboot_falcon falcon_id;

	struct ls_ucode_img_desc ucode_desc;
	u32 *ucode_header;
	u8 *ucode_data;
	u32 ucode_size;

	struct lsf_wpr_header wpr_header;
	struct lsf_lsb_header lsb_header;
};

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


/*
 *
 * HS blob structures
 *
 */

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
	struct {
		u32 sec_code_off;
		u32 sec_code_size;
	} app[0];
};

/**
 * Convenience function to duplicate a firmware file in memory and check that
 * it has the required minimum size.
 */
static void *
gm200_secboot_load_firmware(struct nvkm_subdev *subdev, const char *name,
		    size_t min_size)
{
	const struct firmware *fw;
	void *blob;
	int ret;

	ret = nvkm_firmware_get(subdev->device, name, &fw);
	if (ret)
		return ERR_PTR(ret);
	if (fw->size < min_size) {
		nvkm_error(subdev, "%s is smaller than expected size %zu\n",
			   name, min_size);
		nvkm_firmware_put(fw);
		return ERR_PTR(-EINVAL);
	}
	blob = kmemdup(fw->data, fw->size, GFP_KERNEL);
	nvkm_firmware_put(fw);
	if (!blob)
		return ERR_PTR(-ENOMEM);

	return blob;
}


/*
 * Low-secure blob creation
 */

#define BL_DESC_BLK_SIZE 256
/**
 * Build a ucode image and descriptor from provided bootloader, code and data.
 *
 * @bl:		bootloader image, including 16-bytes descriptor
 * @code:	LS firmware code segment
 * @data:	LS firmware data segment
 * @desc:	ucode descriptor to be written
 *
 * Return: allocated ucode image with corresponding descriptor information. desc
 *         is also updated to contain the right offsets within returned image.
 */
static void *
ls_ucode_img_build(const struct firmware *bl, const struct firmware *code,
		   const struct firmware *data, struct ls_ucode_img_desc *desc)
{
	struct fw_bin_header *bin_hdr = (void *)bl->data;
	struct fw_bl_desc *bl_desc = (void *)bl->data + bin_hdr->header_offset;
	void *bl_data = (void *)bl->data + bin_hdr->data_offset;
	u32 pos = 0;
	void *image;

	desc->bootloader_start_offset = pos;
	desc->bootloader_size = ALIGN(bl_desc->code_size, sizeof(u32));
	desc->bootloader_imem_offset = bl_desc->start_tag * 256;
	desc->bootloader_entry_point = bl_desc->start_tag * 256;

	pos = ALIGN(pos + desc->bootloader_size, BL_DESC_BLK_SIZE);
	desc->app_start_offset = pos;
	desc->app_size = ALIGN(code->size, BL_DESC_BLK_SIZE) +
			 ALIGN(data->size, BL_DESC_BLK_SIZE);
	desc->app_imem_offset = 0;
	desc->app_imem_entry = 0;
	desc->app_dmem_offset = 0;
	desc->app_resident_code_offset = 0;
	desc->app_resident_code_size = ALIGN(code->size, BL_DESC_BLK_SIZE);

	pos = ALIGN(pos + desc->app_resident_code_size, BL_DESC_BLK_SIZE);
	desc->app_resident_data_offset = pos - desc->app_start_offset;
	desc->app_resident_data_size = ALIGN(data->size, BL_DESC_BLK_SIZE);

	desc->image_size = ALIGN(bl_desc->code_size, BL_DESC_BLK_SIZE) +
			   desc->app_size;

	image = kzalloc(desc->image_size, GFP_KERNEL);
	if (!image)
		return ERR_PTR(-ENOMEM);

	memcpy(image + desc->bootloader_start_offset, bl_data,
	       bl_desc->code_size);
	memcpy(image + desc->app_start_offset, code->data, code->size);
	memcpy(image + desc->app_start_offset + desc->app_resident_data_offset,
	       data->data, data->size);

	return image;
}

/**
 * ls_ucode_img_load_generic() - load and prepare a LS ucode image
 *
 * Load the LS microcode, bootloader and signature and pack them into a single
 * blob. Also generate the corresponding ucode descriptor.
 */
static int
ls_ucode_img_load_generic(struct nvkm_subdev *subdev,
			  struct ls_ucode_img *img, const char *falcon_name,
			  const u32 falcon_id)
{
	const struct firmware *bl, *code, *data;
	struct lsf_ucode_desc *lsf_desc;
	char f[64];
	int ret;

	img->ucode_header = NULL;

	snprintf(f, sizeof(f), "gr/%s_bl", falcon_name);
	ret = nvkm_firmware_get(subdev->device, f, &bl);
	if (ret)
		goto error;

	snprintf(f, sizeof(f), "gr/%s_inst", falcon_name);
	ret = nvkm_firmware_get(subdev->device, f, &code);
	if (ret)
		goto free_bl;

	snprintf(f, sizeof(f), "gr/%s_data", falcon_name);
	ret = nvkm_firmware_get(subdev->device, f, &data);
	if (ret)
		goto free_inst;

	img->ucode_data = ls_ucode_img_build(bl, code, data,
					     &img->ucode_desc);
	if (IS_ERR(img->ucode_data)) {
		ret = PTR_ERR(img->ucode_data);
		goto free_data;
	}
	img->ucode_size = img->ucode_desc.image_size;

	snprintf(f, sizeof(f), "gr/%s_sig", falcon_name);
	lsf_desc = gm200_secboot_load_firmware(subdev, f, sizeof(*lsf_desc));
	if (IS_ERR(lsf_desc)) {
		ret = PTR_ERR(lsf_desc);
		goto free_image;
	}
	/* not needed? the signature should already have the right value */
	lsf_desc->falcon_id = falcon_id;
	memcpy(&img->lsb_header.signature, lsf_desc, sizeof(*lsf_desc));
	img->falcon_id = lsf_desc->falcon_id;
	kfree(lsf_desc);

	/* success path - only free requested firmware files */
	goto free_data;

free_image:
	kfree(img->ucode_data);
free_data:
	nvkm_firmware_put(data);
free_inst:
	nvkm_firmware_put(code);
free_bl:
	nvkm_firmware_put(bl);
error:
	return ret;
}

typedef int (*lsf_load_func)(struct nvkm_subdev *, struct ls_ucode_img *);

static int
ls_ucode_img_load_fecs(struct nvkm_subdev *subdev, struct ls_ucode_img *img)
{
	return ls_ucode_img_load_generic(subdev, img, "fecs",
					 NVKM_SECBOOT_FALCON_FECS);
}

static int
ls_ucode_img_load_gpccs(struct nvkm_subdev *subdev, struct ls_ucode_img *img)
{
	return ls_ucode_img_load_generic(subdev, img, "gpccs",
					 NVKM_SECBOOT_FALCON_GPCCS);
}

/**
 * ls_ucode_img_load() - create a lsf_ucode_img and load it
 */
static struct ls_ucode_img *
ls_ucode_img_load(struct nvkm_subdev *subdev, lsf_load_func load_func)
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

static const lsf_load_func lsf_load_funcs[] = {
	[NVKM_SECBOOT_FALCON_END] = NULL, /* reserve enough space */
	[NVKM_SECBOOT_FALCON_FECS] = ls_ucode_img_load_fecs,
	[NVKM_SECBOOT_FALCON_GPCCS] = ls_ucode_img_load_gpccs,
};

/**
 * ls_ucode_img_populate_bl_desc() - populate a DMEM BL descriptor for LS image
 * @img:	ucode image to generate against
 * @desc:	descriptor to populate
 * @sb:		secure boot state to use for base addresses
 *
 * Populate the DMEM BL descriptor with the information contained in a
 * ls_ucode_desc.
 *
 */
static void
ls_ucode_img_populate_bl_desc(struct ls_ucode_img *img, u64 wpr_addr,
			      struct gm200_flcn_bl_desc *desc)
{
	struct ls_ucode_img_desc *pdesc = &img->ucode_desc;
	u64 addr_base;

	addr_base = wpr_addr + img->lsb_header.ucode_off +
		    pdesc->app_start_offset;

	memset(desc, 0, sizeof(*desc));
	desc->ctx_dma = FALCON_DMAIDX_UCODE;
	desc->code_dma_base.lo = lower_32_bits(
		(addr_base + pdesc->app_resident_code_offset));
	desc->code_dma_base.hi = upper_32_bits(
		(addr_base + pdesc->app_resident_code_offset));
	desc->non_sec_code_size = pdesc->app_resident_code_size;
	desc->data_dma_base.lo = lower_32_bits(
		(addr_base + pdesc->app_resident_data_offset));
	desc->data_dma_base.hi = upper_32_bits(
		(addr_base + pdesc->app_resident_data_offset));
	desc->data_size = pdesc->app_resident_data_size;
	desc->code_entry_point = pdesc->app_imem_entry;
}

#define LSF_LSB_HEADER_ALIGN 256
#define LSF_BL_DATA_ALIGN 256
#define LSF_BL_DATA_SIZE_ALIGN 256
#define LSF_BL_CODE_SIZE_ALIGN 256
#define LSF_UCODE_DATA_ALIGN 4096

/**
 * ls_ucode_img_fill_headers - fill the WPR and LSB headers of an image
 * @gsb:	secure boot device used
 * @img:	image to generate for
 * @offset:	offset in the WPR region where this image starts
 *
 * Allocate space in the WPR area from offset and write the WPR and LSB headers
 * accordingly.
 *
 * Return: offset at the end of this image.
 */
static u32
ls_ucode_img_fill_headers(struct gm200_secboot *gsb, struct ls_ucode_img *img,
			  u32 offset)
{
	struct lsf_wpr_header *whdr = &img->wpr_header;
	struct lsf_lsb_header *lhdr = &img->lsb_header;
	struct ls_ucode_img_desc *desc = &img->ucode_desc;

	if (img->ucode_header) {
		nvkm_fatal(&gsb->base.subdev,
			    "images withough loader are not supported yet!\n");
		return offset;
	}

	/* Fill WPR header */
	whdr->falcon_id = img->falcon_id;
	whdr->bootstrap_owner = gsb->base.func->boot_falcon;
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

	lhdr->flags = 0;
	if (img->falcon_id == gsb->base.func->boot_falcon)
		lhdr->flags = LSF_FLAG_DMACTL_REQ_CTX;

	/* GPCCS will be loaded using PRI */
	if (img->falcon_id == NVKM_SECBOOT_FALCON_GPCCS)
		lhdr->flags |= LSF_FLAG_FORCE_PRIV_LOAD;

	/* Align (size bloat) and save off BL descriptor size */
	lhdr->bl_data_size = ALIGN(sizeof(struct gm200_flcn_bl_desc),
				   LSF_BL_DATA_SIZE_ALIGN);
	/*
	 * Align, save off, and include the additional BL data
	 */
	offset = ALIGN(offset, LSF_BL_DATA_ALIGN);
	lhdr->bl_data_off = offset;
	offset += lhdr->bl_data_size;

	return offset;
}

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
		kfree(img->ucode_header);
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
ls_ucode_mgr_fill_headers(struct gm200_secboot *gsb, struct ls_ucode_mgr *mgr)
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
		offset = ls_ucode_img_fill_headers(gsb, img, offset);
	}

	mgr->wpr_size = offset;
}

/**
 * ls_ucode_mgr_write_wpr - write the WPR blob contents
 */
static int
ls_ucode_mgr_write_wpr(struct gm200_secboot *gsb, struct ls_ucode_mgr *mgr,
		       struct nvkm_gpuobj *wpr_blob)
{
	struct ls_ucode_img *img;
	u32 pos = 0;

	nvkm_kmap(wpr_blob);

	list_for_each_entry(img, &mgr->img_list, node) {
		nvkm_gpuobj_memcpy_to(wpr_blob, pos, &img->wpr_header,
				      sizeof(img->wpr_header));

		nvkm_gpuobj_memcpy_to(wpr_blob, img->wpr_header.lsb_offset,
				     &img->lsb_header, sizeof(img->lsb_header));

		/* Generate and write BL descriptor */
		if (!img->ucode_header) {
			u8 desc[gsb->func->bl_desc_size];
			struct gm200_flcn_bl_desc gdesc;

			ls_ucode_img_populate_bl_desc(img, gsb->wpr_addr,
						      &gdesc);
			gsb->func->fixup_bl_desc(&gdesc, &desc);
			nvkm_gpuobj_memcpy_to(wpr_blob,
					      img->lsb_header.bl_data_off,
					      &desc, gsb->func->bl_desc_size);
		}

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
 * gm200_secboot_prepare_ls_blob() - prepare the LS blob
 *
 * For each securely managed falcon, load the FW, signatures and bootloaders and
 * prepare a ucode blob. Then, compute the offsets in the WPR region for each
 * blob, and finally write the headers and ucode blobs into a GPU object that
 * will be copied into the WPR region by the HS firmware.
 */
static int
gm200_secboot_prepare_ls_blob(struct gm200_secboot *gsb)
{
	struct nvkm_secboot *sb = &gsb->base;
	struct nvkm_device *device = sb->subdev.device;
	struct ls_ucode_mgr mgr;
	int falcon_id;
	int ret;

	ls_ucode_mgr_init(&mgr);

	/* Load all LS blobs */
	for_each_set_bit(falcon_id, &gsb->base.func->managed_falcons,
			 NVKM_SECBOOT_FALCON_END) {
		struct ls_ucode_img *img;

		img = ls_ucode_img_load(&sb->subdev, lsf_load_funcs[falcon_id]);

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
	ls_ucode_mgr_fill_headers(gsb, &mgr);
	mgr.wpr_size = ALIGN(mgr.wpr_size, WPR_ALIGNMENT);

	/* Allocate GPU object that will contain the WPR region */
	ret = nvkm_gpuobj_new(device, mgr.wpr_size, WPR_ALIGNMENT, false, NULL,
			      &gsb->ls_blob);
	if (ret)
		goto cleanup;

	nvkm_debug(&sb->subdev, "%d managed LS falcons, WPR size is %d bytes\n",
		    mgr.count, mgr.wpr_size);

	/* If WPR address and size are not fixed, set them to fit the LS blob */
	if (!gsb->wpr_size) {
		gsb->wpr_addr = gsb->ls_blob->addr;
		gsb->wpr_size = gsb->ls_blob->size;
	}

	/* Write LS blob */
	ret = ls_ucode_mgr_write_wpr(gsb, &mgr, gsb->ls_blob);
	if (ret)
		nvkm_gpuobj_del(&gsb->ls_blob);

cleanup:
	ls_ucode_mgr_cleanup(&mgr);

	return ret;
}

/*
 * High-secure blob creation
 */

/**
 * gm200_secboot_hsf_patch_signature() - patch HS blob with correct signature
 */
static void
gm200_secboot_hsf_patch_signature(struct gm200_secboot *gsb, void *acr_image)
{
	struct nvkm_secboot *sb = &gsb->base;
	struct fw_bin_header *hsbin_hdr = acr_image;
	struct hsf_fw_header *fw_hdr = acr_image + hsbin_hdr->header_offset;
	void *hs_data = acr_image + hsbin_hdr->data_offset;
	void *sig;
	u32 sig_size;

	/* Falcon in debug or production mode? */
	if ((nvkm_rd32(sb->subdev.device, sb->base + 0xc08) >> 20) & 0x1) {
		sig = acr_image + fw_hdr->sig_dbg_offset;
		sig_size = fw_hdr->sig_dbg_size;
	} else {
		sig = acr_image + fw_hdr->sig_prod_offset;
		sig_size = fw_hdr->sig_prod_size;
	}

	/* Patch signature */
	memcpy(hs_data + fw_hdr->patch_loc, sig + fw_hdr->patch_sig, sig_size);
}

/**
 * gm200_secboot_populate_hsf_bl_desc() - populate BL descriptor for HS image
 */
static void
gm200_secboot_populate_hsf_bl_desc(void *acr_image,
				   struct gm200_flcn_bl_desc *bl_desc)
{
	struct fw_bin_header *hsbin_hdr = acr_image;
	struct hsf_fw_header *fw_hdr = acr_image + hsbin_hdr->header_offset;
	struct hsf_load_header *load_hdr = acr_image + fw_hdr->hdr_offset;

	/*
	 * Descriptor for the bootloader that will load the ACR image into
	 * IMEM/DMEM memory.
	 */
	fw_hdr = acr_image + hsbin_hdr->header_offset;
	load_hdr = acr_image + fw_hdr->hdr_offset;
	memset(bl_desc, 0, sizeof(*bl_desc));
	bl_desc->ctx_dma = FALCON_DMAIDX_VIRT;
	bl_desc->non_sec_code_off = load_hdr->non_sec_code_off;
	bl_desc->non_sec_code_size = load_hdr->non_sec_code_size;
	bl_desc->sec_code_off = load_hdr->app[0].sec_code_off;
	bl_desc->sec_code_size = load_hdr->app[0].sec_code_size;
	bl_desc->code_entry_point = 0;
	/*
	 * We need to set code_dma_base to the virtual address of the acr_blob,
	 * and add this address to data_dma_base before writing it into DMEM
	 */
	bl_desc->code_dma_base.lo = 0;
	bl_desc->data_dma_base.lo = load_hdr->data_dma_base;
	bl_desc->data_size = load_hdr->data_size;
}

/**
 * gm200_secboot_prepare_hs_blob - load and prepare a HS blob and BL descriptor
 *
 * @gsb secure boot instance to prepare for
 * @fw name of the HS firmware to load
 * @blob pointer to gpuobj that will be allocated to receive the HS FW payload
 * @bl_desc pointer to the BL descriptor to write for this firmware
 * @patch whether we should patch the HS descriptor (only for HS loaders)
 */
static int
gm200_secboot_prepare_hs_blob(struct gm200_secboot *gsb, const char *fw,
			      struct nvkm_gpuobj **blob,
			      struct gm200_flcn_bl_desc *bl_desc, bool patch)
{
	struct nvkm_subdev *subdev = &gsb->base.subdev;
	void *acr_image;
	struct fw_bin_header *hsbin_hdr;
	struct hsf_fw_header *fw_hdr;
	void *acr_data;
	struct hsf_load_header *load_hdr;
	struct hsflcn_acr_desc *desc;
	int ret;

	acr_image = gm200_secboot_load_firmware(subdev, fw, 0);
	if (IS_ERR(acr_image))
		return PTR_ERR(acr_image);
	hsbin_hdr = acr_image;

	/* Patch signature */
	gm200_secboot_hsf_patch_signature(gsb, acr_image);

	acr_data = acr_image + hsbin_hdr->data_offset;

	/* Patch descriptor? */
	if (patch) {
		fw_hdr = acr_image + hsbin_hdr->header_offset;
		load_hdr = acr_image + fw_hdr->hdr_offset;
		desc = acr_data + load_hdr->data_dma_base;
		gsb->func->fixup_hs_desc(gsb, desc);
	}

	/* Generate HS BL descriptor */
	gm200_secboot_populate_hsf_bl_desc(acr_image, bl_desc);

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

/*
 * High-secure bootloader blob creation
 */

static int
gm200_secboot_prepare_hsbl_blob(struct gm200_secboot *gsb)
{
	struct nvkm_subdev *subdev = &gsb->base.subdev;

	gsb->hsbl_blob = gm200_secboot_load_firmware(subdev, "acr/bl", 0);
	if (IS_ERR(gsb->hsbl_blob)) {
		int ret = PTR_ERR(gsb->hsbl_blob);

		gsb->hsbl_blob = NULL;
		return ret;
	}

	return 0;
}

/**
 * gm20x_secboot_prepare_blobs - load blobs common to all GM20X GPUs.
 *
 * This includes the LS blob, HS ucode loading blob, and HS bootloader.
 *
 * The HS ucode unload blob is only used on dGPU.
 */
int
gm20x_secboot_prepare_blobs(struct gm200_secboot *gsb)
{
	int ret;

	/* Load and prepare the managed falcon's firmwares */
	if (!gsb->ls_blob) {
		ret = gm200_secboot_prepare_ls_blob(gsb);
		if (ret)
			return ret;
	}

	/* Load the HS firmware that will load the LS firmwares */
	if (!gsb->acr_load_blob) {
		ret = gm200_secboot_prepare_hs_blob(gsb, "acr/ucode_load",
						&gsb->acr_load_blob,
						&gsb->acr_load_bl_desc, true);
		if (ret)
			return ret;
	}

	/* Load the HS firmware bootloader */
	if (!gsb->hsbl_blob) {
		ret = gm200_secboot_prepare_hsbl_blob(gsb);
		if (ret)
			return ret;
	}

	return 0;
}

static int
gm200_secboot_prepare_blobs(struct gm200_secboot *gsb)
{
	int ret;

	ret = gm20x_secboot_prepare_blobs(gsb);
	if (ret)
		return ret;

	/* dGPU only: load the HS firmware that unprotects the WPR region */
	if (!gsb->acr_unload_blob) {
		ret = gm200_secboot_prepare_hs_blob(gsb, "acr/ucode_unload",
					       &gsb->acr_unload_blob,
					       &gsb->acr_unload_bl_desc, false);
		if (ret)
			return ret;
	}

	return 0;
}

static int
gm200_secboot_blobs_ready(struct gm200_secboot *gsb)
{
	struct nvkm_subdev *subdev = &gsb->base.subdev;
	int ret;

	/* firmware already loaded, nothing to do... */
	if (gsb->firmware_ok)
		return 0;

	ret = gsb->func->prepare_blobs(gsb);
	if (ret) {
		nvkm_error(subdev, "failed to load secure firmware\n");
		return ret;
	}

	gsb->firmware_ok = true;

	return 0;
}


/*
 * Secure Boot Execution
 */

/**
 * gm200_secboot_load_hs_bl() - load HS bootloader into DMEM and IMEM
 */
static void
gm200_secboot_load_hs_bl(struct gm200_secboot *gsb, void *data, u32 data_size)
{
	struct nvkm_device *device = gsb->base.subdev.device;
	struct fw_bin_header *hdr = gsb->hsbl_blob;
	struct fw_bl_desc *hsbl_desc = gsb->hsbl_blob + hdr->header_offset;
	void *blob_data = gsb->hsbl_blob + hdr->data_offset;
	void *hsbl_code = blob_data + hsbl_desc->code_off;
	void *hsbl_data = blob_data + hsbl_desc->data_off;
	u32 code_size = ALIGN(hsbl_desc->code_size, 256);
	const u32 base = gsb->base.base;
	u32 blk;
	u32 tag;
	int i;

	/*
	 * Copy HS bootloader data
	 */
	nvkm_wr32(device, base + 0x1c0, (0x00000000 | (0x1 << 24)));
	for (i = 0; i < hsbl_desc->data_size / 4; i++)
		nvkm_wr32(device, base + 0x1c4, ((u32 *)hsbl_data)[i]);

	/*
	 * Copy HS bootloader interface structure where the HS descriptor
	 * expects it to be
	 */
	nvkm_wr32(device, base + 0x1c0,
		  (hsbl_desc->dmem_load_off | (0x1 << 24)));
	for (i = 0; i < data_size / 4; i++)
		nvkm_wr32(device, base + 0x1c4, ((u32 *)data)[i]);

	/* Copy HS bootloader code to end of IMEM */
	blk = (nvkm_rd32(device, base + 0x108) & 0x1ff) - (code_size >> 8);
	tag = hsbl_desc->start_tag;
	nvkm_wr32(device, base + 0x180, ((blk & 0xff) << 8) | (0x1 << 24));
	for (i = 0; i < code_size / 4; i++) {
		/* write new tag every 256B */
		if ((i & 0x3f) == 0) {
			nvkm_wr32(device, base + 0x188, tag & 0xffff);
			tag++;
		}
		nvkm_wr32(device, base + 0x184, ((u32 *)hsbl_code)[i]);
	}
	nvkm_wr32(device, base + 0x188, 0);
}

/**
 * gm200_secboot_setup_falcon() - set up the secure falcon for secure boot
 */
static int
gm200_secboot_setup_falcon(struct gm200_secboot *gsb)
{
	struct nvkm_device *device = gsb->base.subdev.device;
	struct fw_bin_header *hdr = gsb->hsbl_blob;
	struct fw_bl_desc *hsbl_desc = gsb->hsbl_blob + hdr->header_offset;
	/* virtual start address for boot vector */
	u32 virt_addr = hsbl_desc->start_tag << 8;
	const u32 base = gsb->base.base;
	const u32 reg_base = base + 0xe00;
	u32 inst_loc;
	int ret;

	ret = nvkm_secboot_falcon_reset(&gsb->base);
	if (ret)
		return ret;

	/* setup apertures - virtual */
	nvkm_wr32(device, reg_base + 4 * (FALCON_DMAIDX_UCODE), 0x4);
	nvkm_wr32(device, reg_base + 4 * (FALCON_DMAIDX_VIRT), 0x0);
	/* setup apertures - physical */
	nvkm_wr32(device, reg_base + 4 * (FALCON_DMAIDX_PHYS_VID), 0x4);
	nvkm_wr32(device, reg_base + 4 * (FALCON_DMAIDX_PHYS_SYS_COH),
		  0x4 | 0x1);
	nvkm_wr32(device, reg_base + 4 * (FALCON_DMAIDX_PHYS_SYS_NCOH),
		  0x4 | 0x2);

	/* Set context */
	if (nvkm_memory_target(gsb->inst->memory) == NVKM_MEM_TARGET_VRAM)
		inst_loc = 0x0; /* FB */
	else
		inst_loc = 0x3; /* Non-coherent sysmem */

	nvkm_mask(device, base + 0x048, 0x1, 0x1);
	nvkm_wr32(device, base + 0x480,
		  ((gsb->inst->addr >> 12) & 0xfffffff) |
		  (inst_loc << 28) | (1 << 30));

	/* Set boot vector to code's starting virtual address */
	nvkm_wr32(device, base + 0x104, virt_addr);

	return 0;
}

/**
 * gm200_secboot_run_hs_blob() - run the given high-secure blob
 */
static int
gm200_secboot_run_hs_blob(struct gm200_secboot *gsb, struct nvkm_gpuobj *blob,
			  struct gm200_flcn_bl_desc *desc)
{
	struct nvkm_vma vma;
	u64 vma_addr;
	const u32 bl_desc_size = gsb->func->bl_desc_size;
	u8 bl_desc[bl_desc_size];
	int ret;

	/* Map the HS firmware so the HS bootloader can see it */
	ret = nvkm_gpuobj_map(blob, gsb->vm, NV_MEM_ACCESS_RW, &vma);
	if (ret)
		return ret;

	/* Add the mapping address to the DMA bases */
	vma_addr = flcn64_to_u64(desc->code_dma_base) + vma.offset;
	desc->code_dma_base.lo = lower_32_bits(vma_addr);
	desc->code_dma_base.hi = upper_32_bits(vma_addr);
	vma_addr = flcn64_to_u64(desc->data_dma_base) + vma.offset;
	desc->data_dma_base.lo = lower_32_bits(vma_addr);
	desc->data_dma_base.hi = upper_32_bits(vma_addr);

	/* Fixup the BL header */
	gsb->func->fixup_bl_desc(desc, &bl_desc);

	/* Reset the falcon and make it ready to run the HS bootloader */
	ret = gm200_secboot_setup_falcon(gsb);
	if (ret)
		goto done;

	/* Load the HS bootloader into the falcon's IMEM/DMEM */
	gm200_secboot_load_hs_bl(gsb, &bl_desc, bl_desc_size);

	/* Start the HS bootloader */
	ret = nvkm_secboot_falcon_run(&gsb->base);
	if (ret)
		goto done;

done:
	/* Restore the original DMA addresses */
	vma_addr = flcn64_to_u64(desc->code_dma_base) - vma.offset;
	desc->code_dma_base.lo = lower_32_bits(vma_addr);
	desc->code_dma_base.hi = upper_32_bits(vma_addr);
	vma_addr = flcn64_to_u64(desc->data_dma_base) - vma.offset;
	desc->data_dma_base.lo = lower_32_bits(vma_addr);
	desc->data_dma_base.hi = upper_32_bits(vma_addr);

	/* We don't need the ACR firmware anymore */
	nvkm_gpuobj_unmap(&vma);

	return ret;
}

/*
 * gm200_secboot_reset() - execute secure boot from the prepared state
 *
 * Load the HS bootloader and ask the falcon to run it. This will in turn
 * load the HS firmware and run it, so once the falcon stops all the managed
 * falcons should have their LS firmware loaded and be ready to run.
 */
int
gm200_secboot_reset(struct nvkm_secboot *sb, enum nvkm_secboot_falcon falcon)
{
	struct gm200_secboot *gsb = gm200_secboot(sb);
	int ret;

	/* Make sure all blobs are ready */
	ret = gm200_secboot_blobs_ready(gsb);
	if (ret)
		return ret;

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

	/* If WPR is set and we have an unload blob, run it to unlock WPR */
	if (gsb->acr_unload_blob &&
	    gsb->falcon_state[NVKM_SECBOOT_FALCON_FECS] != NON_SECURE) {
		ret = gm200_secboot_run_hs_blob(gsb, gsb->acr_unload_blob,
						&gsb->acr_unload_bl_desc);
		if (ret)
			return ret;
	}

	/* Reload all managed falcons */
	ret = gm200_secboot_run_hs_blob(gsb, gsb->acr_load_blob,
					&gsb->acr_load_bl_desc);
	if (ret)
		return ret;

end:
	gsb->falcon_state[falcon] = RESET;
	return 0;
}

int
gm200_secboot_start(struct nvkm_secboot *sb, enum nvkm_secboot_falcon falcon)
{
	struct gm200_secboot *gsb = gm200_secboot(sb);
	int base;

	switch (falcon) {
	case NVKM_SECBOOT_FALCON_FECS:
		base = 0x409000;
		break;
	case NVKM_SECBOOT_FALCON_GPCCS:
		base = 0x41a000;
		break;
	default:
		nvkm_error(&sb->subdev, "cannot start unhandled falcon!\n");
		return -EINVAL;
	}

	nvkm_wr32(sb->subdev.device, base + 0x130, 0x00000002);
	gsb->falcon_state[falcon] = RUNNING;

	return 0;
}



int
gm200_secboot_init(struct nvkm_secboot *sb)
{
	struct gm200_secboot *gsb = gm200_secboot(sb);
	struct nvkm_device *device = sb->subdev.device;
	struct nvkm_vm *vm;
	const u64 vm_area_len = 600 * 1024;
	int ret;

	/* Allocate instance block and VM */
	ret = nvkm_gpuobj_new(device, 0x1000, 0, true, NULL, &gsb->inst);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x8000, 0, true, NULL, &gsb->pgd);
	if (ret)
		return ret;

	ret = nvkm_vm_new(device, 0, vm_area_len, 0, NULL, &vm);
	if (ret)
		return ret;

	atomic_inc(&vm->engref[NVKM_SUBDEV_PMU]);

	ret = nvkm_vm_ref(vm, &gsb->vm, gsb->pgd);
	nvkm_vm_ref(NULL, &vm, NULL);
	if (ret)
		return ret;

	nvkm_kmap(gsb->inst);
	nvkm_wo32(gsb->inst, 0x200, lower_32_bits(gsb->pgd->addr));
	nvkm_wo32(gsb->inst, 0x204, upper_32_bits(gsb->pgd->addr));
	nvkm_wo32(gsb->inst, 0x208, lower_32_bits(vm_area_len - 1));
	nvkm_wo32(gsb->inst, 0x20c, upper_32_bits(vm_area_len - 1));
	nvkm_done(gsb->inst);

	return 0;
}

static int
gm200_secboot_fini(struct nvkm_secboot *sb, bool suspend)
{
	struct gm200_secboot *gsb = gm200_secboot(sb);
	int ret = 0;
	int i;

	/* Run the unload blob to unprotect the WPR region */
	if (gsb->acr_unload_blob &&
	    gsb->falcon_state[NVKM_SECBOOT_FALCON_FECS] != NON_SECURE)
		ret = gm200_secboot_run_hs_blob(gsb, gsb->acr_unload_blob,
						&gsb->acr_unload_bl_desc);

	for (i = 0; i < NVKM_SECBOOT_FALCON_END; i++)
		gsb->falcon_state[i] = NON_SECURE;

	return ret;
}

void *
gm200_secboot_dtor(struct nvkm_secboot *sb)
{
	struct gm200_secboot *gsb = gm200_secboot(sb);

	nvkm_gpuobj_del(&gsb->acr_unload_blob);

	kfree(gsb->hsbl_blob);
	nvkm_gpuobj_del(&gsb->acr_load_blob);
	nvkm_gpuobj_del(&gsb->ls_blob);

	nvkm_vm_ref(NULL, &gsb->vm, gsb->pgd);
	nvkm_gpuobj_del(&gsb->pgd);
	nvkm_gpuobj_del(&gsb->inst);

	return gsb;
}


static const struct nvkm_secboot_func
gm200_secboot = {
	.dtor = gm200_secboot_dtor,
	.init = gm200_secboot_init,
	.fini = gm200_secboot_fini,
	.reset = gm200_secboot_reset,
	.start = gm200_secboot_start,
	.managed_falcons = BIT(NVKM_SECBOOT_FALCON_FECS) |
			   BIT(NVKM_SECBOOT_FALCON_GPCCS),
	.boot_falcon = NVKM_SECBOOT_FALCON_PMU,
};

/**
 * gm200_fixup_bl_desc - just copy the BL descriptor
 *
 * Use the GM200 descriptor format by default.
 */
static void
gm200_secboot_fixup_bl_desc(const struct gm200_flcn_bl_desc *desc, void *ret)
{
	memcpy(ret, desc, sizeof(*desc));
}

static void
gm200_secboot_fixup_hs_desc(struct gm200_secboot *gsb,
			    struct hsflcn_acr_desc *desc)
{
	desc->ucode_blob_base = gsb->ls_blob->addr;
	desc->ucode_blob_size = gsb->ls_blob->size;

	desc->wpr_offset = 0;

	/* WPR region information for the HS binary to set up */
	desc->wpr_region_id = 1;
	desc->regions.no_regions = 1;
	desc->regions.region_props[0].region_id = 1;
	desc->regions.region_props[0].start_addr = gsb->wpr_addr >> 8;
	desc->regions.region_props[0].end_addr =
		(gsb->wpr_addr + gsb->wpr_size) >> 8;
}

static const struct gm200_secboot_func
gm200_secboot_func = {
	.bl_desc_size = sizeof(struct gm200_flcn_bl_desc),
	.fixup_bl_desc = gm200_secboot_fixup_bl_desc,
	.fixup_hs_desc = gm200_secboot_fixup_hs_desc,
	.prepare_blobs = gm200_secboot_prepare_blobs,
};

int
gm200_secboot_new(struct nvkm_device *device, int index,
		  struct nvkm_secboot **psb)
{
	int ret;
	struct gm200_secboot *gsb;

	gsb = kzalloc(sizeof(*gsb), GFP_KERNEL);
	if (!gsb) {
		psb = NULL;
		return -ENOMEM;
	}
	*psb = &gsb->base;

	ret = nvkm_secboot_ctor(&gm200_secboot, device, index, &gsb->base);
	if (ret)
		return ret;

	gsb->func = &gm200_secboot_func;

	return 0;
}

MODULE_FIRMWARE("nvidia/gm200/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gm200/acr/ucode_load.bin");
MODULE_FIRMWARE("nvidia/gm200/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gm200/gr/sw_method_init.bin");

MODULE_FIRMWARE("nvidia/gm204/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gm204/acr/ucode_load.bin");
MODULE_FIRMWARE("nvidia/gm204/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gm204/gr/sw_method_init.bin");

MODULE_FIRMWARE("nvidia/gm206/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gm206/acr/ucode_load.bin");
MODULE_FIRMWARE("nvidia/gm206/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gm206/gr/sw_method_init.bin");

MODULE_FIRMWARE("nvidia/gp100/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp100/acr/ucode_load.bin");
MODULE_FIRMWARE("nvidia/gp100/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/fecs_bl.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/fecs_inst.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/fecs_data.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/fecs_sig.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/gpccs_bl.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/gpccs_inst.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/gpccs_data.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/gpccs_sig.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/sw_ctx.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/sw_nonctx.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/sw_bundle_init.bin");
MODULE_FIRMWARE("nvidia/gp100/gr/sw_method_init.bin");
