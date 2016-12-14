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

struct ls_ucode_img;

#define ACR_R352_MAX_APPS 8

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

#endif
