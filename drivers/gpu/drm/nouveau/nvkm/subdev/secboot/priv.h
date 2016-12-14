/*
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
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

#ifndef __NVKM_SECBOOT_PRIV_H__
#define __NVKM_SECBOOT_PRIV_H__

#include <subdev/secboot.h>
#include <subdev/mmu.h>

struct nvkm_secboot_func {
	int (*oneinit)(struct nvkm_secboot *);
	int (*fini)(struct nvkm_secboot *, bool suspend);
	void *(*dtor)(struct nvkm_secboot *);
	int (*reset)(struct nvkm_secboot *, enum nvkm_secboot_falcon);

	/* ID of the falcon that will perform secure boot */
	enum nvkm_secboot_falcon boot_falcon;
	/* Bit-mask of IDs of managed falcons */
	unsigned long managed_falcons;
};

int nvkm_secboot_ctor(const struct nvkm_secboot_func *, struct nvkm_device *,
		      int index, struct nvkm_secboot *);

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

struct flcn_u64 {
	u32 lo;
	u32 hi;
};
static inline u64 flcn64_to_u64(const struct flcn_u64 f)
{
	return ((u64)f.hi) << 32 | f.lo;
}

/**
 * struct gm200_flcn_bl_desc - DMEM bootloader descriptor
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
struct gm200_flcn_bl_desc {
	u32 reserved[4];
	u32 signature[4];
	u32 ctx_dma;
	struct flcn_u64 code_dma_base;
	u32 non_sec_code_off;
	u32 non_sec_code_size;
	u32 sec_code_off;
	u32 sec_code_size;
	u32 code_entry_point;
	struct flcn_u64 data_dma_base;
	u32 data_size;
};

/**
 * struct secboot_ls_single_func - manages a single LS firmware
 *
 * @load: load the external firmware into a ls_ucode_img
 * @generate_bl_desc: function called on a block of bl_desc_size to generate the
 *		      proper bootloader descriptor for this LS firmware
 * @bl_desc_size: size of the bootloader descriptor
 */
struct secboot_ls_single_func {
	int (*load)(const struct nvkm_subdev *, struct ls_ucode_img *);
	void (*generate_bl_desc)(const struct ls_ucode_img *, u64, void *);
	u32 bl_desc_size;
};

/**
 * typedef secboot_ls_func - manages all the LS firmwares for this ACR
 */
typedef const struct secboot_ls_single_func *
secboot_ls_func[NVKM_SECBOOT_FALCON_END];

int gm200_ls_load_fecs(const struct nvkm_subdev *, struct ls_ucode_img *);
int gm200_ls_load_gpccs(const struct nvkm_subdev *, struct ls_ucode_img *);

/**
 * Contains the whole secure boot state, allowing it to be performed as needed
 * @wpr_addr:		physical address of the WPR region
 * @wpr_size:		size in bytes of the WPR region
 * @ls_blob:		LS blob of all the LS firmwares, signatures, bootloaders
 * @ls_blob_size:	size of the LS blob
 * @ls_blob_nb_regions:	number of LS firmwares that will be loaded
 * @acr_blob:		HS blob
 * @acr_blob_vma:	mapping of the HS blob into the secure falcon's VM
 * @acr_bl_desc:	bootloader descriptor of the HS blob
 * @hsbl_blob:		HS blob bootloader
 * @inst:		instance block for HS falcon
 * @pgd:		page directory for the HS falcon
 * @vm:			address space used by the HS falcon
 * @falcon_state:	current state of the managed falcons
 * @firmware_ok:	whether the firmware blobs have been created
 */
struct gm200_secboot {
	struct nvkm_secboot base;
	const struct gm200_secboot_func *func;
	const secboot_ls_func *ls_func;

	/*
	 * Address and size of the fixed WPR region, if any. On Tegra this
	 * region is set by the bootloader
	 */
	u64 wpr_addr;
	u32 wpr_size;

	/*
	 * Address and size of the actual WPR region.
	 */
	u64 acr_wpr_addr;
	u32 acr_wpr_size;

	/*
	 * HS FW - lock WPR region (dGPU only) and load LS FWs
	 * on Tegra the HS FW copies the LS blob into the fixed WPR instead
	 */
	struct nvkm_gpuobj *acr_load_blob;
	struct gm200_flcn_bl_desc acr_load_bl_desc;

	/* HS FW - unlock WPR region (dGPU only) */
	struct nvkm_gpuobj *acr_unload_blob;
	struct gm200_flcn_bl_desc acr_unload_bl_desc;

	/* HS bootloader */
	void *hsbl_blob;

	/* LS FWs, to be loaded by the HS ACR */
	struct nvkm_gpuobj *ls_blob;

	/* Instance block & address space used for HS FW execution */
	struct nvkm_gpuobj *inst;
	struct nvkm_gpuobj *pgd;
	struct nvkm_vm *vm;

	/* To keep track of the state of all managed falcons */
	enum {
		/* In non-secure state, no firmware loaded, no privileges*/
		NON_SECURE = 0,
		/* In low-secure mode and ready to be started */
		RESET,
		/* In low-secure mode and running */
		RUNNING,
	} falcon_state[NVKM_SECBOOT_FALCON_END];

	bool firmware_ok;
};
#define gm200_secboot(sb) container_of(sb, struct gm200_secboot, base)

/**
 * Contains functions we wish to abstract between GM200-like implementations
 * @bl_desc_size:	size of the BL descriptor used by this chip.
 * @fixup_bl_desc:	hook that generates the proper BL descriptor format from
 *			the generic GM200 format into a data array of size
 *			bl_desc_size
 * @prepare_blobs:	prepares the various blobs needed for secure booting
 */
struct gm200_secboot_func {
	/*
	 * Size of the bootloader descriptor for this chip. A block of this
	 * size is allocated before booting a falcon and the fixup_bl_desc
	 * callback is called on it
	 */
	u32 bl_desc_size;
	void (*fixup_bl_desc)(const struct gm200_flcn_bl_desc *, void *);

	int (*prepare_blobs)(struct gm200_secboot *);
};

int gm200_secboot_oneinit(struct nvkm_secboot *);
void *gm200_secboot_dtor(struct nvkm_secboot *);
int gm200_secboot_reset(struct nvkm_secboot *, enum nvkm_secboot_falcon);
int gm200_secboot_start(struct nvkm_secboot *, enum nvkm_secboot_falcon);

int gm20x_secboot_prepare_blobs(struct gm200_secboot *);

#endif
