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
	int (*init)(struct nvkm_secboot *);
	int (*fini)(struct nvkm_secboot *, bool suspend);
	void *(*dtor)(struct nvkm_secboot *);
	int (*reset)(struct nvkm_secboot *, enum nvkm_secboot_falcon);
	int (*start)(struct nvkm_secboot *, enum nvkm_secboot_falcon);

	/* ID of the falcon that will perform secure boot */
	enum nvkm_secboot_falcon boot_falcon;
	/* Bit-mask of IDs of managed falcons */
	unsigned long managed_falcons;
};

int nvkm_secboot_ctor(const struct nvkm_secboot_func *, struct nvkm_device *,
		      int index, struct nvkm_secboot *);
int nvkm_secboot_falcon_reset(struct nvkm_secboot *);
int nvkm_secboot_falcon_run(struct nvkm_secboot *);

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

	/*
	 * Address and size of the WPR region. On dGPU this will be the
	 * address of the LS blob. On Tegra this is a fixed region set by the
	 * bootloader
	 */
	u64 wpr_addr;
	u32 wpr_size;

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
 * @fixup_hs_desc:	hook that twiddles the HS descriptor before it is used
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

	/*
	 * Chip-specific modifications of the HS descriptor can be done here.
	 * On dGPU this is used to fill the information about the WPR region
	 * we want the HS FW to set up.
	 */
	void (*fixup_hs_desc)(struct gm200_secboot *, struct hsflcn_acr_desc *);
	int (*prepare_blobs)(struct gm200_secboot *);
};

int gm200_secboot_init(struct nvkm_secboot *);
void *gm200_secboot_dtor(struct nvkm_secboot *);
int gm200_secboot_reset(struct nvkm_secboot *, u32);
int gm200_secboot_start(struct nvkm_secboot *, u32);

int gm20x_secboot_prepare_blobs(struct gm200_secboot *);

#endif
