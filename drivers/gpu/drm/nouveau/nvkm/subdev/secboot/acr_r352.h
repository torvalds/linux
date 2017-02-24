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
#ifndef __NVKM_SECBOOT_ACR_R352_H__
#define __NVKM_SECBOOT_ACR_R352_H__

#include "acr.h"
#include "ls_ucode.h"

struct ls_ucode_img;

#define ACR_R352_MAX_APPS 8

/*
 *
 * LS blob structures
 *
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
#define LSF_FLAG_LOAD_CODE_AT_0		1
#define LSF_FLAG_DMACTL_REQ_CTX		4
#define LSF_FLAG_FORCE_PRIV_LOAD	8
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

	struct acr_r352_lsf_wpr_header wpr_header;
	struct acr_r352_lsf_lsb_header lsb_header;
};
#define ls_ucode_img_r352(i) container_of(i, struct ls_ucode_img_r352, base)


/*
 * HS blob structures
 */

struct hsf_load_header_app {
	u32 sec_code_off;
	u32 sec_code_size;
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
	struct hsf_load_header_app app[0];
};

/**
 * struct acr_r352_ls_func - manages a single LS firmware
 *
 * @load: load the external firmware into a ls_ucode_img
 * @generate_bl_desc: function called on a block of bl_desc_size to generate the
 *		      proper bootloader descriptor for this LS firmware
 * @bl_desc_size: size of the bootloader descriptor
 * @lhdr_flags: LS flags
 */
struct acr_r352_ls_func {
	int (*load)(const struct nvkm_subdev *, struct ls_ucode_img *);
	void (*generate_bl_desc)(const struct nvkm_acr *,
				 const struct ls_ucode_img *, u64, void *);
	u32 bl_desc_size;
	u32 lhdr_flags;
};

struct acr_r352;

/**
 * struct acr_r352_func - manages nuances between ACR versions
 *
 * @generate_hs_bl_desc: function called on a block of bl_desc_size to generate
 *			 the proper HS bootloader descriptor
 * @hs_bl_desc_size: size of the HS bootloader descriptor
 */
struct acr_r352_func {
	void (*generate_hs_bl_desc)(const struct hsf_load_header *, void *,
				    u64);
	u32 hs_bl_desc_size;

	struct ls_ucode_img *(*ls_ucode_img_load)(const struct acr_r352 *,
						  enum nvkm_secboot_falcon);
	int (*ls_fill_headers)(struct acr_r352 *, struct list_head *);
	int (*ls_write_wpr)(struct acr_r352 *, struct list_head *,
			    struct nvkm_gpuobj *, u32);

	const struct acr_r352_ls_func *ls_func[NVKM_SECBOOT_FALCON_END];
};

/**
 * struct acr_r352 - ACR data for driver release 352 (and beyond)
 */
struct acr_r352 {
	struct nvkm_acr base;
	const struct acr_r352_func *func;

	/*
	 * HS FW - lock WPR region (dGPU only) and load LS FWs
	 * on Tegra the HS FW copies the LS blob into the fixed WPR instead
	 */
	struct nvkm_gpuobj *load_blob;
	struct {
		struct hsf_load_header load_bl_header;
		struct hsf_load_header_app __load_apps[ACR_R352_MAX_APPS];
	};

	/* HS FW - unlock WPR region (dGPU only) */
	struct nvkm_gpuobj *unload_blob;
	struct {
		struct hsf_load_header unload_bl_header;
		struct hsf_load_header_app __unload_apps[ACR_R352_MAX_APPS];
	};

	/* HS bootloader */
	void *hsbl_blob;

	/* LS FWs, to be loaded by the HS ACR */
	struct nvkm_gpuobj *ls_blob;

	/* Firmware already loaded? */
	bool firmware_ok;

	/* Falcons to lazy-bootstrap */
	u32 lazy_bootstrap;

	/* To keep track of the state of all managed falcons */
	enum {
		/* In non-secure state, no firmware loaded, no privileges*/
		NON_SECURE = 0,
		/* In low-secure mode and ready to be started */
		RESET,
		/* In low-secure mode and running */
		RUNNING,
	} falcon_state[NVKM_SECBOOT_FALCON_END];
};
#define acr_r352(acr) container_of(acr, struct acr_r352, base)

struct nvkm_acr *acr_r352_new_(const struct acr_r352_func *,
			       enum nvkm_secboot_falcon, unsigned long);

struct ls_ucode_img *acr_r352_ls_ucode_img_load(const struct acr_r352 *,
						enum nvkm_secboot_falcon);
int acr_r352_ls_fill_headers(struct acr_r352 *, struct list_head *);
int acr_r352_ls_write_wpr(struct acr_r352 *, struct list_head *,
			  struct nvkm_gpuobj *, u32);

#endif
