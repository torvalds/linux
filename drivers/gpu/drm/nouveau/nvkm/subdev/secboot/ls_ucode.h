/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
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

#ifndef __NVKM_SECBOOT_LS_UCODE_H__
#define __NVKM_SECBOOT_LS_UCODE_H__

#include <core/os.h>
#include <core/subdev.h>
#include <subdev/secboot.h>

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
	u8 prd_keys[2][16];
	u8 dbg_keys[2][16];
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

int acr_ls_ucode_load_fecs(const struct nvkm_subdev *, struct ls_ucode_img *);
int acr_ls_ucode_load_gpccs(const struct nvkm_subdev *, struct ls_ucode_img *);


#endif
