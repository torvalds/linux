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
#include "hs_ucode.h"

struct ls_ucode_img;

#define ACR_R352_MAX_APPS 8

#define LSF_FLAG_LOAD_CODE_AT_0		1
#define LSF_FLAG_DMACTL_REQ_CTX		4
#define LSF_FLAG_FORCE_PRIV_LOAD	8

static inline u32
hsf_load_header_app_off(const struct hsf_load_header *hdr, u32 app)
{
	return hdr->apps[app];
}

static inline u32
hsf_load_header_app_size(const struct hsf_load_header *hdr, u32 app)
{
	return hdr->apps[hdr->num_apps + app];
}

/**
 * struct acr_r352_lsf_func - manages a specific LS firmware version
 *
 * @generate_bl_desc: function called on a block of bl_desc_size to generate the
 *		      proper bootloader descriptor for this LS firmware
 * @bl_desc_size: size of the bootloader descriptor
 * @lhdr_flags: LS flags
 */
struct acr_r352_lsf_func {
	void (*generate_bl_desc)(const struct nvkm_acr *,
				 const struct ls_ucode_img *, u64, void *);
	u32 bl_desc_size;
	u32 lhdr_flags;
};

/**
 * struct acr_r352_ls_func - manages a single LS falcon
 *
 * @load: load the external firmware into a ls_ucode_img
 * @post_run: hook called right after the ACR is executed
 */
struct acr_r352_ls_func {
	int (*load)(const struct nvkm_secboot *, int maxver,
		    struct ls_ucode_img *);
	int (*post_run)(const struct nvkm_acr *, const struct nvkm_secboot *);
	int version_max;
	const struct acr_r352_lsf_func *version[];
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
	void (*fixup_hs_desc)(struct acr_r352 *, struct nvkm_secboot *, void *);
	u32 hs_bl_desc_size;
	bool shadow_blob;

	struct ls_ucode_img *(*ls_ucode_img_load)(const struct acr_r352 *,
						  const struct nvkm_secboot *,
						  enum nvkm_secboot_falcon);
	int (*ls_fill_headers)(struct acr_r352 *, struct list_head *);
	int (*ls_write_wpr)(struct acr_r352 *, struct list_head *,
			    struct nvkm_gpuobj *, u64);

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
		u32 __load_apps[ACR_R352_MAX_APPS * 2];
	};

	/* HS FW - unlock WPR region (dGPU only) */
	struct nvkm_gpuobj *unload_blob;
	struct {
		struct hsf_load_header unload_bl_header;
		u32 __unload_apps[ACR_R352_MAX_APPS * 2];
	};

	/* HS bootloader */
	void *hsbl_blob;

	/* HS bootloader for unload blob, if using a different falcon */
	void *hsbl_unload_blob;

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
						const struct nvkm_secboot *,
						enum nvkm_secboot_falcon);
int acr_r352_ls_fill_headers(struct acr_r352 *, struct list_head *);
int acr_r352_ls_write_wpr(struct acr_r352 *, struct list_head *,
			  struct nvkm_gpuobj *, u64);

void acr_r352_fixup_hs_desc(struct acr_r352 *, struct nvkm_secboot *, void *);

#endif
