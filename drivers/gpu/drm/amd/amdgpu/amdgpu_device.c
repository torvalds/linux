/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#include <linux/power_supply.h>
#include <linux/kthread.h>
#include <linux/console.h>
#include <linux/slab.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/amdgpu_drm.h>
#include <linux/vgaarb.h>
#include <linux/vga_switcheroo.h>
#include <linux/efi.h>
#include "amdgpu.h"
#include "amdgpu_trace.h"
#include "amdgpu_i2c.h"
#include "atom.h"
#include "amdgpu_atombios.h"
#include "amdgpu_atomfirmware.h"
#include "amd_pcie.h"
#ifdef CONFIG_DRM_AMDGPU_SI
#include "si.h"
#endif
#ifdef CONFIG_DRM_AMDGPU_CIK
#include "cik.h"
#endif
#include "vi.h"
#include "soc15.h"
#include "bif/bif_4_1_d.h"
#include <linux/pci.h>
#include <linux/firmware.h>
#include "amdgpu_vf_error.h"

#include "amdgpu_amdkfd.h"
#include "amdgpu_pm.h"

#include "amdgpu_xgmi.h"

MODULE_FIRMWARE("amdgpu/vega10_gpu_info.bin");
MODULE_FIRMWARE("amdgpu/vega12_gpu_info.bin");
MODULE_FIRMWARE("amdgpu/raven_gpu_info.bin");
MODULE_FIRMWARE("amdgpu/picasso_gpu_info.bin");
MODULE_FIRMWARE("amdgpu/raven2_gpu_info.bin");

#define AMDGPU_RESUME_MS		2000

static const char *amdgpu_asic_name[] = {
	"TAHITI",
	"PITCAIRN",
	"VERDE",
	"OLAND",
	"HAINAN",
	"BONAIRE",
	"KAVERI",
	"KABINI",
	"HAWAII",
	"MULLINS",
	"TOPAZ",
	"TONGA",
	"FIJI",
	"CARRIZO",
	"STONEY",
	"POLARIS10",
	"POLARIS11",
	"POLARIS12",
	"VEGAM",
	"VEGA10",
	"VEGA12",
	"VEGA20",
	"RAVEN",
	"LAST",
};

static void amdgpu_device_get_pcie_info(struct amdgpu_device *adev);

/**
 * amdgpu_device_is_px - Is the device is a dGPU with HG/PX power control
 *
 * @dev: drm_device pointer
 *
 * Returns true if the device is a dGPU with HG/PX power control,
 * otherwise return false.
 */
bool amdgpu_device_is_px(struct drm_device *dev)
{
	struct amdgpu_device *adev = dev->dev_private;

	if (adev->flags & AMD_IS_PX)
		return true;
	return false;
}

/*
 * MMIO register access helper functions.
 */
/**
 * amdgpu_mm_rreg - read a memory mapped IO register
 *
 * @adev: amdgpu_device pointer
 * @reg: dword aligned register offset
 * @acc_flags: access flags which require special behavior
 *
 * Returns the 32 bit value from the offset specified.
 */
uint32_t amdgpu_mm_rreg(struct amdgpu_device *adev, uint32_t reg,
			uint32_t acc_flags)
{
	uint32_t ret;

	if (!(acc_flags & AMDGPU_REGS_NO_KIQ) && amdgpu_sriov_runtime(adev))
		return amdgpu_virt_kiq_rreg(adev, reg);

	if ((reg * 4) < adev->rmmio_size && !(acc_flags & AMDGPU_REGS_IDX))
		ret = readl(((void __iomem *)adev->rmmio) + (reg * 4));
	else {
		unsigned long flags;

		spin_lock_irqsave(&adev->mmio_idx_lock, flags);
		writel((reg * 4), ((void __iomem *)adev->rmmio) + (mmMM_INDEX * 4));
		ret = readl(((void __iomem *)adev->rmmio) + (mmMM_DATA * 4));
		spin_unlock_irqrestore(&adev->mmio_idx_lock, flags);
	}
	trace_amdgpu_mm_rreg(adev->pdev->device, reg, ret);
	return ret;
}

/*
 * MMIO register read with bytes helper functions
 * @offset:bytes offset from MMIO start
 *
*/

/**
 * amdgpu_mm_rreg8 - read a memory mapped IO register
 *
 * @adev: amdgpu_device pointer
 * @offset: byte aligned register offset
 *
 * Returns the 8 bit value from the offset specified.
 */
uint8_t amdgpu_mm_rreg8(struct amdgpu_device *adev, uint32_t offset) {
	if (offset < adev->rmmio_size)
		return (readb(adev->rmmio + offset));
	BUG();
}

/*
 * MMIO register write with bytes helper functions
 * @offset:bytes offset from MMIO start
 * @value: the value want to be written to the register
 *
*/
/**
 * amdgpu_mm_wreg8 - read a memory mapped IO register
 *
 * @adev: amdgpu_device pointer
 * @offset: byte aligned register offset
 * @value: 8 bit value to write
 *
 * Writes the value specified to the offset specified.
 */
void amdgpu_mm_wreg8(struct amdgpu_device *adev, uint32_t offset, uint8_t value) {
	if (offset < adev->rmmio_size)
		writeb(value, adev->rmmio + offset);
	else
		BUG();
}

/**
 * amdgpu_mm_wreg - write to a memory mapped IO register
 *
 * @adev: amdgpu_device pointer
 * @reg: dword aligned register offset
 * @v: 32 bit value to write to the register
 * @acc_flags: access flags which require special behavior
 *
 * Writes the value specified to the offset specified.
 */
void amdgpu_mm_wreg(struct amdgpu_device *adev, uint32_t reg, uint32_t v,
		    uint32_t acc_flags)
{
	trace_amdgpu_mm_wreg(adev->pdev->device, reg, v);

	if (adev->asic_type >= CHIP_VEGA10 && reg == 0) {
		adev->last_mm_index = v;
	}

	if (!(acc_flags & AMDGPU_REGS_NO_KIQ) && amdgpu_sriov_runtime(adev))
		return amdgpu_virt_kiq_wreg(adev, reg, v);

	if ((reg * 4) < adev->rmmio_size && !(acc_flags & AMDGPU_REGS_IDX))
		writel(v, ((void __iomem *)adev->rmmio) + (reg * 4));
	else {
		unsigned long flags;

		spin_lock_irqsave(&adev->mmio_idx_lock, flags);
		writel((reg * 4), ((void __iomem *)adev->rmmio) + (mmMM_INDEX * 4));
		writel(v, ((void __iomem *)adev->rmmio) + (mmMM_DATA * 4));
		spin_unlock_irqrestore(&adev->mmio_idx_lock, flags);
	}

	if (adev->asic_type >= CHIP_VEGA10 && reg == 1 && adev->last_mm_index == 0x5702C) {
		udelay(500);
	}
}

/**
 * amdgpu_io_rreg - read an IO register
 *
 * @adev: amdgpu_device pointer
 * @reg: dword aligned register offset
 *
 * Returns the 32 bit value from the offset specified.
 */
u32 amdgpu_io_rreg(struct amdgpu_device *adev, u32 reg)
{
	if ((reg * 4) < adev->rio_mem_size)
		return ioread32(adev->rio_mem + (reg * 4));
	else {
		iowrite32((reg * 4), adev->rio_mem + (mmMM_INDEX * 4));
		return ioread32(adev->rio_mem + (mmMM_DATA * 4));
	}
}

/**
 * amdgpu_io_wreg - write to an IO register
 *
 * @adev: amdgpu_device pointer
 * @reg: dword aligned register offset
 * @v: 32 bit value to write to the register
 *
 * Writes the value specified to the offset specified.
 */
void amdgpu_io_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	if (adev->asic_type >= CHIP_VEGA10 && reg == 0) {
		adev->last_mm_index = v;
	}

	if ((reg * 4) < adev->rio_mem_size)
		iowrite32(v, adev->rio_mem + (reg * 4));
	else {
		iowrite32((reg * 4), adev->rio_mem + (mmMM_INDEX * 4));
		iowrite32(v, adev->rio_mem + (mmMM_DATA * 4));
	}

	if (adev->asic_type >= CHIP_VEGA10 && reg == 1 && adev->last_mm_index == 0x5702C) {
		udelay(500);
	}
}

/**
 * amdgpu_mm_rdoorbell - read a doorbell dword
 *
 * @adev: amdgpu_device pointer
 * @index: doorbell index
 *
 * Returns the value in the doorbell aperture at the
 * requested doorbell index (CIK).
 */
u32 amdgpu_mm_rdoorbell(struct amdgpu_device *adev, u32 index)
{
	if (index < adev->doorbell.num_doorbells) {
		return readl(adev->doorbell.ptr + index);
	} else {
		DRM_ERROR("reading beyond doorbell aperture: 0x%08x!\n", index);
		return 0;
	}
}

/**
 * amdgpu_mm_wdoorbell - write a doorbell dword
 *
 * @adev: amdgpu_device pointer
 * @index: doorbell index
 * @v: value to write
 *
 * Writes @v to the doorbell aperture at the
 * requested doorbell index (CIK).
 */
void amdgpu_mm_wdoorbell(struct amdgpu_device *adev, u32 index, u32 v)
{
	if (index < adev->doorbell.num_doorbells) {
		writel(v, adev->doorbell.ptr + index);
	} else {
		DRM_ERROR("writing beyond doorbell aperture: 0x%08x!\n", index);
	}
}

/**
 * amdgpu_mm_rdoorbell64 - read a doorbell Qword
 *
 * @adev: amdgpu_device pointer
 * @index: doorbell index
 *
 * Returns the value in the doorbell aperture at the
 * requested doorbell index (VEGA10+).
 */
u64 amdgpu_mm_rdoorbell64(struct amdgpu_device *adev, u32 index)
{
	if (index < adev->doorbell.num_doorbells) {
		return atomic64_read((atomic64_t *)(adev->doorbell.ptr + index));
	} else {
		DRM_ERROR("reading beyond doorbell aperture: 0x%08x!\n", index);
		return 0;
	}
}

/**
 * amdgpu_mm_wdoorbell64 - write a doorbell Qword
 *
 * @adev: amdgpu_device pointer
 * @index: doorbell index
 * @v: value to write
 *
 * Writes @v to the doorbell aperture at the
 * requested doorbell index (VEGA10+).
 */
void amdgpu_mm_wdoorbell64(struct amdgpu_device *adev, u32 index, u64 v)
{
	if (index < adev->doorbell.num_doorbells) {
		atomic64_set((atomic64_t *)(adev->doorbell.ptr + index), v);
	} else {
		DRM_ERROR("writing beyond doorbell aperture: 0x%08x!\n", index);
	}
}

/**
 * amdgpu_invalid_rreg - dummy reg read function
 *
 * @adev: amdgpu device pointer
 * @reg: offset of register
 *
 * Dummy register read function.  Used for register blocks
 * that certain asics don't have (all asics).
 * Returns the value in the register.
 */
static uint32_t amdgpu_invalid_rreg(struct amdgpu_device *adev, uint32_t reg)
{
	DRM_ERROR("Invalid callback to read register 0x%04X\n", reg);
	BUG();
	return 0;
}

/**
 * amdgpu_invalid_wreg - dummy reg write function
 *
 * @adev: amdgpu device pointer
 * @reg: offset of register
 * @v: value to write to the register
 *
 * Dummy register read function.  Used for register blocks
 * that certain asics don't have (all asics).
 */
static void amdgpu_invalid_wreg(struct amdgpu_device *adev, uint32_t reg, uint32_t v)
{
	DRM_ERROR("Invalid callback to write register 0x%04X with 0x%08X\n",
		  reg, v);
	BUG();
}

/**
 * amdgpu_block_invalid_rreg - dummy reg read function
 *
 * @adev: amdgpu device pointer
 * @block: offset of instance
 * @reg: offset of register
 *
 * Dummy register read function.  Used for register blocks
 * that certain asics don't have (all asics).
 * Returns the value in the register.
 */
static uint32_t amdgpu_block_invalid_rreg(struct amdgpu_device *adev,
					  uint32_t block, uint32_t reg)
{
	DRM_ERROR("Invalid callback to read register 0x%04X in block 0x%04X\n",
		  reg, block);
	BUG();
	return 0;
}

/**
 * amdgpu_block_invalid_wreg - dummy reg write function
 *
 * @adev: amdgpu device pointer
 * @block: offset of instance
 * @reg: offset of register
 * @v: value to write to the register
 *
 * Dummy register read function.  Used for register blocks
 * that certain asics don't have (all asics).
 */
static void amdgpu_block_invalid_wreg(struct amdgpu_device *adev,
				      uint32_t block,
				      uint32_t reg, uint32_t v)
{
	DRM_ERROR("Invalid block callback to write register 0x%04X in block 0x%04X with 0x%08X\n",
		  reg, block, v);
	BUG();
}

/**
 * amdgpu_device_vram_scratch_init - allocate the VRAM scratch page
 *
 * @adev: amdgpu device pointer
 *
 * Allocates a scratch page of VRAM for use by various things in the
 * driver.
 */
static int amdgpu_device_vram_scratch_init(struct amdgpu_device *adev)
{
	return amdgpu_bo_create_kernel(adev, AMDGPU_GPU_PAGE_SIZE,
				       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM,
				       &adev->vram_scratch.robj,
				       &adev->vram_scratch.gpu_addr,
				       (void **)&adev->vram_scratch.ptr);
}

/**
 * amdgpu_device_vram_scratch_fini - Free the VRAM scratch page
 *
 * @adev: amdgpu device pointer
 *
 * Frees the VRAM scratch page.
 */
static void amdgpu_device_vram_scratch_fini(struct amdgpu_device *adev)
{
	amdgpu_bo_free_kernel(&adev->vram_scratch.robj, NULL, NULL);
}

/**
 * amdgpu_device_program_register_sequence - program an array of registers.
 *
 * @adev: amdgpu_device pointer
 * @registers: pointer to the register array
 * @array_size: size of the register array
 *
 * Programs an array or registers with and and or masks.
 * This is a helper for setting golden registers.
 */
void amdgpu_device_program_register_sequence(struct amdgpu_device *adev,
					     const u32 *registers,
					     const u32 array_size)
{
	u32 tmp, reg, and_mask, or_mask;
	int i;

	if (array_size % 3)
		return;

	for (i = 0; i < array_size; i +=3) {
		reg = registers[i + 0];
		and_mask = registers[i + 1];
		or_mask = registers[i + 2];

		if (and_mask == 0xffffffff) {
			tmp = or_mask;
		} else {
			tmp = RREG32(reg);
			tmp &= ~and_mask;
			tmp |= or_mask;
		}
		WREG32(reg, tmp);
	}
}

/**
 * amdgpu_device_pci_config_reset - reset the GPU
 *
 * @adev: amdgpu_device pointer
 *
 * Resets the GPU using the pci config reset sequence.
 * Only applicable to asics prior to vega10.
 */
void amdgpu_device_pci_config_reset(struct amdgpu_device *adev)
{
	pci_write_config_dword(adev->pdev, 0x7c, AMDGPU_ASIC_RESET_DATA);
}

/*
 * GPU doorbell aperture helpers function.
 */
/**
 * amdgpu_device_doorbell_init - Init doorbell driver information.
 *
 * @adev: amdgpu_device pointer
 *
 * Init doorbell driver information (CIK)
 * Returns 0 on success, error on failure.
 */
static int amdgpu_device_doorbell_init(struct amdgpu_device *adev)
{

	/* No doorbell on SI hardware generation */
	if (adev->asic_type < CHIP_BONAIRE) {
		adev->doorbell.base = 0;
		adev->doorbell.size = 0;
		adev->doorbell.num_doorbells = 0;
		adev->doorbell.ptr = NULL;
		return 0;
	}

	if (pci_resource_flags(adev->pdev, 2) & IORESOURCE_UNSET)
		return -EINVAL;

	amdgpu_asic_init_doorbell_index(adev);

	/* doorbell bar mapping */
	adev->doorbell.base = pci_resource_start(adev->pdev, 2);
	adev->doorbell.size = pci_resource_len(adev->pdev, 2);

	adev->doorbell.num_doorbells = min_t(u32, adev->doorbell.size / sizeof(u32),
					     adev->doorbell_index.max_assignment+1);
	if (adev->doorbell.num_doorbells == 0)
		return -EINVAL;

	/* For Vega, reserve and map two pages on doorbell BAR since SDMA
	 * paging queue doorbell use the second page. The
	 * AMDGPU_DOORBELL64_MAX_ASSIGNMENT definition assumes all the
	 * doorbells are in the first page. So with paging queue enabled,
	 * the max num_doorbells should + 1 page (0x400 in dword)
	 */
	if (adev->asic_type >= CHIP_VEGA10)
		adev->doorbell.num_doorbells += 0x400;

	adev->doorbell.ptr = ioremap(adev->doorbell.base,
				     adev->doorbell.num_doorbells *
				     sizeof(u32));
	if (adev->doorbell.ptr == NULL)
		return -ENOMEM;

	return 0;
}

/**
 * amdgpu_device_doorbell_fini - Tear down doorbell driver information.
 *
 * @adev: amdgpu_device pointer
 *
 * Tear down doorbell driver information (CIK)
 */
static void amdgpu_device_doorbell_fini(struct amdgpu_device *adev)
{
	iounmap(adev->doorbell.ptr);
	adev->doorbell.ptr = NULL;
}



/*
 * amdgpu_device_wb_*()
 * Writeback is the method by which the GPU updates special pages in memory
 * with the status of certain GPU events (fences, ring pointers,etc.).
 */

/**
 * amdgpu_device_wb_fini - Disable Writeback and free memory
 *
 * @adev: amdgpu_device pointer
 *
 * Disables Writeback and frees the Writeback memory (all asics).
 * Used at driver shutdown.
 */
static void amdgpu_device_wb_fini(struct amdgpu_device *adev)
{
	if (adev->wb.wb_obj) {
		amdgpu_bo_free_kernel(&adev->wb.wb_obj,
				      &adev->wb.gpu_addr,
				      (void **)&adev->wb.wb);
		adev->wb.wb_obj = NULL;
	}
}

/**
 * amdgpu_device_wb_init- Init Writeback driver info and allocate memory
 *
 * @adev: amdgpu_device pointer
 *
 * Initializes writeback and allocates writeback memory (all asics).
 * Used at driver startup.
 * Returns 0 on success or an -error on failure.
 */
static int amdgpu_device_wb_init(struct amdgpu_device *adev)
{
	int r;

	if (adev->wb.wb_obj == NULL) {
		/* AMDGPU_MAX_WB * sizeof(uint32_t) * 8 = AMDGPU_MAX_WB 256bit slots */
		r = amdgpu_bo_create_kernel(adev, AMDGPU_MAX_WB * sizeof(uint32_t) * 8,
					    PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
					    &adev->wb.wb_obj, &adev->wb.gpu_addr,
					    (void **)&adev->wb.wb);
		if (r) {
			dev_warn(adev->dev, "(%d) create WB bo failed\n", r);
			return r;
		}

		adev->wb.num_wb = AMDGPU_MAX_WB;
		memset(&adev->wb.used, 0, sizeof(adev->wb.used));

		/* clear wb memory */
		memset((char *)adev->wb.wb, 0, AMDGPU_MAX_WB * sizeof(uint32_t) * 8);
	}

	return 0;
}

/**
 * amdgpu_device_wb_get - Allocate a wb entry
 *
 * @adev: amdgpu_device pointer
 * @wb: wb index
 *
 * Allocate a wb slot for use by the driver (all asics).
 * Returns 0 on success or -EINVAL on failure.
 */
int amdgpu_device_wb_get(struct amdgpu_device *adev, u32 *wb)
{
	unsigned long offset = find_first_zero_bit(adev->wb.used, adev->wb.num_wb);

	if (offset < adev->wb.num_wb) {
		__set_bit(offset, adev->wb.used);
		*wb = offset << 3; /* convert to dw offset */
		return 0;
	} else {
		return -EINVAL;
	}
}

/**
 * amdgpu_device_wb_free - Free a wb entry
 *
 * @adev: amdgpu_device pointer
 * @wb: wb index
 *
 * Free a wb slot allocated for use by the driver (all asics)
 */
void amdgpu_device_wb_free(struct amdgpu_device *adev, u32 wb)
{
	wb >>= 3;
	if (wb < adev->wb.num_wb)
		__clear_bit(wb, adev->wb.used);
}

/**
 * amdgpu_device_resize_fb_bar - try to resize FB BAR
 *
 * @adev: amdgpu_device pointer
 *
 * Try to resize FB BAR to make all VRAM CPU accessible. We try very hard not
 * to fail, but if any of the BARs is not accessible after the size we abort
 * driver loading by returning -ENODEV.
 */
int amdgpu_device_resize_fb_bar(struct amdgpu_device *adev)
{
	u64 space_needed = roundup_pow_of_two(adev->gmc.real_vram_size);
	u32 rbar_size = order_base_2(((space_needed >> 20) | 1)) - 1;
	struct pci_bus *root;
	struct resource *res;
	unsigned i;
	u16 cmd;
	int r;

	/* Bypass for VF */
	if (amdgpu_sriov_vf(adev))
		return 0;

	/* Check if the root BUS has 64bit memory resources */
	root = adev->pdev->bus;
	while (root->parent)
		root = root->parent;

	pci_bus_for_each_resource(root, res, i) {
		if (res && res->flags & (IORESOURCE_MEM | IORESOURCE_MEM_64) &&
		    res->start > 0x100000000ull)
			break;
	}

	/* Trying to resize is pointless without a root hub window above 4GB */
	if (!res)
		return 0;

	/* Disable memory decoding while we change the BAR addresses and size */
	pci_read_config_word(adev->pdev, PCI_COMMAND, &cmd);
	pci_write_config_word(adev->pdev, PCI_COMMAND,
			      cmd & ~PCI_COMMAND_MEMORY);

	/* Free the VRAM and doorbell BAR, we most likely need to move both. */
	amdgpu_device_doorbell_fini(adev);
	if (adev->asic_type >= CHIP_BONAIRE)
		pci_release_resource(adev->pdev, 2);

	pci_release_resource(adev->pdev, 0);

	r = pci_resize_resource(adev->pdev, 0, rbar_size);
	if (r == -ENOSPC)
		DRM_INFO("Not enough PCI address space for a large BAR.");
	else if (r && r != -ENOTSUPP)
		DRM_ERROR("Problem resizing BAR0 (%d).", r);

	pci_assign_unassigned_bus_resources(adev->pdev->bus);

	/* When the doorbell or fb BAR isn't available we have no chance of
	 * using the device.
	 */
	r = amdgpu_device_doorbell_init(adev);
	if (r || (pci_resource_flags(adev->pdev, 0) & IORESOURCE_UNSET))
		return -ENODEV;

	pci_write_config_word(adev->pdev, PCI_COMMAND, cmd);

	return 0;
}

/*
 * GPU helpers function.
 */
/**
 * amdgpu_device_need_post - check if the hw need post or not
 *
 * @adev: amdgpu_device pointer
 *
 * Check if the asic has been initialized (all asics) at driver startup
 * or post is needed if  hw reset is performed.
 * Returns true if need or false if not.
 */
bool amdgpu_device_need_post(struct amdgpu_device *adev)
{
	uint32_t reg;

	if (amdgpu_sriov_vf(adev))
		return false;

	if (amdgpu_passthrough(adev)) {
		/* for FIJI: In whole GPU pass-through virtualization case, after VM reboot
		 * some old smc fw still need driver do vPost otherwise gpu hang, while
		 * those smc fw version above 22.15 doesn't have this flaw, so we force
		 * vpost executed for smc version below 22.15
		 */
		if (adev->asic_type == CHIP_FIJI) {
			int err;
			uint32_t fw_ver;
			err = request_firmware(&adev->pm.fw, "amdgpu/fiji_smc.bin", adev->dev);
			/* force vPost if error occured */
			if (err)
				return true;

			fw_ver = *((uint32_t *)adev->pm.fw->data + 69);
			if (fw_ver < 0x00160e00)
				return true;
		}
	}

	if (adev->has_hw_reset) {
		adev->has_hw_reset = false;
		return true;
	}

	/* bios scratch used on CIK+ */
	if (adev->asic_type >= CHIP_BONAIRE)
		return amdgpu_atombios_scratch_need_asic_init(adev);

	/* check MEM_SIZE for older asics */
	reg = amdgpu_asic_get_config_memsize(adev);

	if ((reg != 0) && (reg != 0xffffffff))
		return false;

	return true;
}

/* if we get transitioned to only one device, take VGA back */
/**
 * amdgpu_device_vga_set_decode - enable/disable vga decode
 *
 * @cookie: amdgpu_device pointer
 * @state: enable/disable vga decode
 *
 * Enable/disable vga decode (all asics).
 * Returns VGA resource flags.
 */
static unsigned int amdgpu_device_vga_set_decode(void *cookie, bool state)
{
	struct amdgpu_device *adev = cookie;
	amdgpu_asic_set_vga_state(adev, state);
	if (state)
		return VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
		       VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
	else
		return VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
}

/**
 * amdgpu_device_check_block_size - validate the vm block size
 *
 * @adev: amdgpu_device pointer
 *
 * Validates the vm block size specified via module parameter.
 * The vm block size defines number of bits in page table versus page directory,
 * a page is 4KB so we have 12 bits offset, minimum 9 bits in the
 * page table and the remaining bits are in the page directory.
 */
static void amdgpu_device_check_block_size(struct amdgpu_device *adev)
{
	/* defines number of bits in page table versus page directory,
	 * a page is 4KB so we have 12 bits offset, minimum 9 bits in the
	 * page table and the remaining bits are in the page directory */
	if (amdgpu_vm_block_size == -1)
		return;

	if (amdgpu_vm_block_size < 9) {
		dev_warn(adev->dev, "VM page table size (%d) too small\n",
			 amdgpu_vm_block_size);
		amdgpu_vm_block_size = -1;
	}
}

/**
 * amdgpu_device_check_vm_size - validate the vm size
 *
 * @adev: amdgpu_device pointer
 *
 * Validates the vm size in GB specified via module parameter.
 * The VM size is the size of the GPU virtual memory space in GB.
 */
static void amdgpu_device_check_vm_size(struct amdgpu_device *adev)
{
	/* no need to check the default value */
	if (amdgpu_vm_size == -1)
		return;

	if (amdgpu_vm_size < 1) {
		dev_warn(adev->dev, "VM size (%d) too small, min is 1GB\n",
			 amdgpu_vm_size);
		amdgpu_vm_size = -1;
	}
}

static void amdgpu_device_check_smu_prv_buffer_size(struct amdgpu_device *adev)
{
	struct sysinfo si;
	bool is_os_64 = (sizeof(void *) == 8) ? true : false;
	uint64_t total_memory;
	uint64_t dram_size_seven_GB = 0x1B8000000;
	uint64_t dram_size_three_GB = 0xB8000000;

	if (amdgpu_smu_memory_pool_size == 0)
		return;

	if (!is_os_64) {
		DRM_WARN("Not 64-bit OS, feature not supported\n");
		goto def_value;
	}
	si_meminfo(&si);
	total_memory = (uint64_t)si.totalram * si.mem_unit;

	if ((amdgpu_smu_memory_pool_size == 1) ||
		(amdgpu_smu_memory_pool_size == 2)) {
		if (total_memory < dram_size_three_GB)
			goto def_value1;
	} else if ((amdgpu_smu_memory_pool_size == 4) ||
		(amdgpu_smu_memory_pool_size == 8)) {
		if (total_memory < dram_size_seven_GB)
			goto def_value1;
	} else {
		DRM_WARN("Smu memory pool size not supported\n");
		goto def_value;
	}
	adev->pm.smu_prv_buffer_size = amdgpu_smu_memory_pool_size << 28;

	return;

def_value1:
	DRM_WARN("No enough system memory\n");
def_value:
	adev->pm.smu_prv_buffer_size = 0;
}

/**
 * amdgpu_device_check_arguments - validate module params
 *
 * @adev: amdgpu_device pointer
 *
 * Validates certain module parameters and updates
 * the associated values used by the driver (all asics).
 */
static void amdgpu_device_check_arguments(struct amdgpu_device *adev)
{
	if (amdgpu_sched_jobs < 4) {
		dev_warn(adev->dev, "sched jobs (%d) must be at least 4\n",
			 amdgpu_sched_jobs);
		amdgpu_sched_jobs = 4;
	} else if (!is_power_of_2(amdgpu_sched_jobs)){
		dev_warn(adev->dev, "sched jobs (%d) must be a power of 2\n",
			 amdgpu_sched_jobs);
		amdgpu_sched_jobs = roundup_pow_of_two(amdgpu_sched_jobs);
	}

	if (amdgpu_gart_size != -1 && amdgpu_gart_size < 32) {
		/* gart size must be greater or equal to 32M */
		dev_warn(adev->dev, "gart size (%d) too small\n",
			 amdgpu_gart_size);
		amdgpu_gart_size = -1;
	}

	if (amdgpu_gtt_size != -1 && amdgpu_gtt_size < 32) {
		/* gtt size must be greater or equal to 32M */
		dev_warn(adev->dev, "gtt size (%d) too small\n",
				 amdgpu_gtt_size);
		amdgpu_gtt_size = -1;
	}

	/* valid range is between 4 and 9 inclusive */
	if (amdgpu_vm_fragment_size != -1 &&
	    (amdgpu_vm_fragment_size > 9 || amdgpu_vm_fragment_size < 4)) {
		dev_warn(adev->dev, "valid range is between 4 and 9\n");
		amdgpu_vm_fragment_size = -1;
	}

	amdgpu_device_check_smu_prv_buffer_size(adev);

	amdgpu_device_check_vm_size(adev);

	amdgpu_device_check_block_size(adev);

	if (amdgpu_vram_page_split != -1 && (amdgpu_vram_page_split < 16 ||
	    !is_power_of_2(amdgpu_vram_page_split))) {
		dev_warn(adev->dev, "invalid VRAM page split (%d)\n",
			 amdgpu_vram_page_split);
		amdgpu_vram_page_split = 1024;
	}

	if (amdgpu_lockup_timeout == 0) {
		dev_warn(adev->dev, "lockup_timeout msut be > 0, adjusting to 10000\n");
		amdgpu_lockup_timeout = 10000;
	}

	adev->firmware.load_type = amdgpu_ucode_get_load_type(adev, amdgpu_fw_load_type);
}

/**
 * amdgpu_switcheroo_set_state - set switcheroo state
 *
 * @pdev: pci dev pointer
 * @state: vga_switcheroo state
 *
 * Callback for the switcheroo driver.  Suspends or resumes the
 * the asics before or after it is powered up using ACPI methods.
 */
static void amdgpu_switcheroo_set_state(struct pci_dev *pdev, enum vga_switcheroo_state state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	if (amdgpu_device_is_px(dev) && state == VGA_SWITCHEROO_OFF)
		return;

	if (state == VGA_SWITCHEROO_ON) {
		pr_info("amdgpu: switched on\n");
		/* don't suspend or resume card normally */
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;

		amdgpu_device_resume(dev, true, true);

		dev->switch_power_state = DRM_SWITCH_POWER_ON;
		drm_kms_helper_poll_enable(dev);
	} else {
		pr_info("amdgpu: switched off\n");
		drm_kms_helper_poll_disable(dev);
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		amdgpu_device_suspend(dev, true, true);
		dev->switch_power_state = DRM_SWITCH_POWER_OFF;
	}
}

/**
 * amdgpu_switcheroo_can_switch - see if switcheroo state can change
 *
 * @pdev: pci dev pointer
 *
 * Callback for the switcheroo driver.  Check of the switcheroo
 * state can be changed.
 * Returns true if the state can be changed, false if not.
 */
static bool amdgpu_switcheroo_can_switch(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	/*
	* FIXME: open_count is protected by drm_global_mutex but that would lead to
	* locking inversion with the driver load path. And the access here is
	* completely racy anyway. So don't bother with locking for now.
	*/
	return dev->open_count == 0;
}

static const struct vga_switcheroo_client_ops amdgpu_switcheroo_ops = {
	.set_gpu_state = amdgpu_switcheroo_set_state,
	.reprobe = NULL,
	.can_switch = amdgpu_switcheroo_can_switch,
};

/**
 * amdgpu_device_ip_set_clockgating_state - set the CG state
 *
 * @dev: amdgpu_device pointer
 * @block_type: Type of hardware IP (SMU, GFX, UVD, etc.)
 * @state: clockgating state (gate or ungate)
 *
 * Sets the requested clockgating state for all instances of
 * the hardware IP specified.
 * Returns the error code from the last instance.
 */
int amdgpu_device_ip_set_clockgating_state(void *dev,
					   enum amd_ip_block_type block_type,
					   enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = dev;
	int i, r = 0;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (adev->ip_blocks[i].version->type != block_type)
			continue;
		if (!adev->ip_blocks[i].version->funcs->set_clockgating_state)
			continue;
		r = adev->ip_blocks[i].version->funcs->set_clockgating_state(
			(void *)adev, state);
		if (r)
			DRM_ERROR("set_clockgating_state of IP block <%s> failed %d\n",
				  adev->ip_blocks[i].version->funcs->name, r);
	}
	return r;
}

/**
 * amdgpu_device_ip_set_powergating_state - set the PG state
 *
 * @dev: amdgpu_device pointer
 * @block_type: Type of hardware IP (SMU, GFX, UVD, etc.)
 * @state: powergating state (gate or ungate)
 *
 * Sets the requested powergating state for all instances of
 * the hardware IP specified.
 * Returns the error code from the last instance.
 */
int amdgpu_device_ip_set_powergating_state(void *dev,
					   enum amd_ip_block_type block_type,
					   enum amd_powergating_state state)
{
	struct amdgpu_device *adev = dev;
	int i, r = 0;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (adev->ip_blocks[i].version->type != block_type)
			continue;
		if (!adev->ip_blocks[i].version->funcs->set_powergating_state)
			continue;
		r = adev->ip_blocks[i].version->funcs->set_powergating_state(
			(void *)adev, state);
		if (r)
			DRM_ERROR("set_powergating_state of IP block <%s> failed %d\n",
				  adev->ip_blocks[i].version->funcs->name, r);
	}
	return r;
}

/**
 * amdgpu_device_ip_get_clockgating_state - get the CG state
 *
 * @adev: amdgpu_device pointer
 * @flags: clockgating feature flags
 *
 * Walks the list of IPs on the device and updates the clockgating
 * flags for each IP.
 * Updates @flags with the feature flags for each hardware IP where
 * clockgating is enabled.
 */
void amdgpu_device_ip_get_clockgating_state(struct amdgpu_device *adev,
					    u32 *flags)
{
	int i;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (adev->ip_blocks[i].version->funcs->get_clockgating_state)
			adev->ip_blocks[i].version->funcs->get_clockgating_state((void *)adev, flags);
	}
}

/**
 * amdgpu_device_ip_wait_for_idle - wait for idle
 *
 * @adev: amdgpu_device pointer
 * @block_type: Type of hardware IP (SMU, GFX, UVD, etc.)
 *
 * Waits for the request hardware IP to be idle.
 * Returns 0 for success or a negative error code on failure.
 */
int amdgpu_device_ip_wait_for_idle(struct amdgpu_device *adev,
				   enum amd_ip_block_type block_type)
{
	int i, r;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (adev->ip_blocks[i].version->type == block_type) {
			r = adev->ip_blocks[i].version->funcs->wait_for_idle((void *)adev);
			if (r)
				return r;
			break;
		}
	}
	return 0;

}

/**
 * amdgpu_device_ip_is_idle - is the hardware IP idle
 *
 * @adev: amdgpu_device pointer
 * @block_type: Type of hardware IP (SMU, GFX, UVD, etc.)
 *
 * Check if the hardware IP is idle or not.
 * Returns true if it the IP is idle, false if not.
 */
bool amdgpu_device_ip_is_idle(struct amdgpu_device *adev,
			      enum amd_ip_block_type block_type)
{
	int i;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (adev->ip_blocks[i].version->type == block_type)
			return adev->ip_blocks[i].version->funcs->is_idle((void *)adev);
	}
	return true;

}

/**
 * amdgpu_device_ip_get_ip_block - get a hw IP pointer
 *
 * @adev: amdgpu_device pointer
 * @type: Type of hardware IP (SMU, GFX, UVD, etc.)
 *
 * Returns a pointer to the hardware IP block structure
 * if it exists for the asic, otherwise NULL.
 */
struct amdgpu_ip_block *
amdgpu_device_ip_get_ip_block(struct amdgpu_device *adev,
			      enum amd_ip_block_type type)
{
	int i;

	for (i = 0; i < adev->num_ip_blocks; i++)
		if (adev->ip_blocks[i].version->type == type)
			return &adev->ip_blocks[i];

	return NULL;
}

/**
 * amdgpu_device_ip_block_version_cmp
 *
 * @adev: amdgpu_device pointer
 * @type: enum amd_ip_block_type
 * @major: major version
 * @minor: minor version
 *
 * return 0 if equal or greater
 * return 1 if smaller or the ip_block doesn't exist
 */
int amdgpu_device_ip_block_version_cmp(struct amdgpu_device *adev,
				       enum amd_ip_block_type type,
				       u32 major, u32 minor)
{
	struct amdgpu_ip_block *ip_block = amdgpu_device_ip_get_ip_block(adev, type);

	if (ip_block && ((ip_block->version->major > major) ||
			((ip_block->version->major == major) &&
			(ip_block->version->minor >= minor))))
		return 0;

	return 1;
}

/**
 * amdgpu_device_ip_block_add
 *
 * @adev: amdgpu_device pointer
 * @ip_block_version: pointer to the IP to add
 *
 * Adds the IP block driver information to the collection of IPs
 * on the asic.
 */
int amdgpu_device_ip_block_add(struct amdgpu_device *adev,
			       const struct amdgpu_ip_block_version *ip_block_version)
{
	if (!ip_block_version)
		return -EINVAL;

	DRM_INFO("add ip block number %d <%s>\n", adev->num_ip_blocks,
		  ip_block_version->funcs->name);

	adev->ip_blocks[adev->num_ip_blocks++].version = ip_block_version;

	return 0;
}

/**
 * amdgpu_device_enable_virtual_display - enable virtual display feature
 *
 * @adev: amdgpu_device pointer
 *
 * Enabled the virtual display feature if the user has enabled it via
 * the module parameter virtual_display.  This feature provides a virtual
 * display hardware on headless boards or in virtualized environments.
 * This function parses and validates the configuration string specified by
 * the user and configues the virtual display configuration (number of
 * virtual connectors, crtcs, etc.) specified.
 */
static void amdgpu_device_enable_virtual_display(struct amdgpu_device *adev)
{
	adev->enable_virtual_display = false;

	if (amdgpu_virtual_display) {
		struct drm_device *ddev = adev->ddev;
		const char *pci_address_name = pci_name(ddev->pdev);
		char *pciaddstr, *pciaddstr_tmp, *pciaddname_tmp, *pciaddname;

		pciaddstr = kstrdup(amdgpu_virtual_display, GFP_KERNEL);
		pciaddstr_tmp = pciaddstr;
		while ((pciaddname_tmp = strsep(&pciaddstr_tmp, ";"))) {
			pciaddname = strsep(&pciaddname_tmp, ",");
			if (!strcmp("all", pciaddname)
			    || !strcmp(pci_address_name, pciaddname)) {
				long num_crtc;
				int res = -1;

				adev->enable_virtual_display = true;

				if (pciaddname_tmp)
					res = kstrtol(pciaddname_tmp, 10,
						      &num_crtc);

				if (!res) {
					if (num_crtc < 1)
						num_crtc = 1;
					if (num_crtc > 6)
						num_crtc = 6;
					adev->mode_info.num_crtc = num_crtc;
				} else {
					adev->mode_info.num_crtc = 1;
				}
				break;
			}
		}

		DRM_INFO("virtual display string:%s, %s:virtual_display:%d, num_crtc:%d\n",
			 amdgpu_virtual_display, pci_address_name,
			 adev->enable_virtual_display, adev->mode_info.num_crtc);

		kfree(pciaddstr);
	}
}

/**
 * amdgpu_device_parse_gpu_info_fw - parse gpu info firmware
 *
 * @adev: amdgpu_device pointer
 *
 * Parses the asic configuration parameters specified in the gpu info
 * firmware and makes them availale to the driver for use in configuring
 * the asic.
 * Returns 0 on success, -EINVAL on failure.
 */
static int amdgpu_device_parse_gpu_info_fw(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[30];
	int err;
	const struct gpu_info_firmware_header_v1_0 *hdr;

	adev->firmware.gpu_info_fw = NULL;

	switch (adev->asic_type) {
	case CHIP_TOPAZ:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
#ifdef CONFIG_DRM_AMDGPU_SI
	case CHIP_VERDE:
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_OLAND:
	case CHIP_HAINAN:
#endif
#ifdef CONFIG_DRM_AMDGPU_CIK
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_MULLINS:
#endif
	case CHIP_VEGA20:
	default:
		return 0;
	case CHIP_VEGA10:
		chip_name = "vega10";
		break;
	case CHIP_VEGA12:
		chip_name = "vega12";
		break;
	case CHIP_RAVEN:
		if (adev->rev_id >= 8)
			chip_name = "raven2";
		else if (adev->pdev->device == 0x15d8)
			chip_name = "picasso";
		else
			chip_name = "raven";
		break;
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_gpu_info.bin", chip_name);
	err = request_firmware(&adev->firmware.gpu_info_fw, fw_name, adev->dev);
	if (err) {
		dev_err(adev->dev,
			"Failed to load gpu_info firmware \"%s\"\n",
			fw_name);
		goto out;
	}
	err = amdgpu_ucode_validate(adev->firmware.gpu_info_fw);
	if (err) {
		dev_err(adev->dev,
			"Failed to validate gpu_info firmware \"%s\"\n",
			fw_name);
		goto out;
	}

	hdr = (const struct gpu_info_firmware_header_v1_0 *)adev->firmware.gpu_info_fw->data;
	amdgpu_ucode_print_gpu_info_hdr(&hdr->header);

	switch (hdr->version_major) {
	case 1:
	{
		const struct gpu_info_firmware_v1_0 *gpu_info_fw =
			(const struct gpu_info_firmware_v1_0 *)(adev->firmware.gpu_info_fw->data +
								le32_to_cpu(hdr->header.ucode_array_offset_bytes));

		adev->gfx.config.max_shader_engines = le32_to_cpu(gpu_info_fw->gc_num_se);
		adev->gfx.config.max_cu_per_sh = le32_to_cpu(gpu_info_fw->gc_num_cu_per_sh);
		adev->gfx.config.max_sh_per_se = le32_to_cpu(gpu_info_fw->gc_num_sh_per_se);
		adev->gfx.config.max_backends_per_se = le32_to_cpu(gpu_info_fw->gc_num_rb_per_se);
		adev->gfx.config.max_texture_channel_caches =
			le32_to_cpu(gpu_info_fw->gc_num_tccs);
		adev->gfx.config.max_gprs = le32_to_cpu(gpu_info_fw->gc_num_gprs);
		adev->gfx.config.max_gs_threads = le32_to_cpu(gpu_info_fw->gc_num_max_gs_thds);
		adev->gfx.config.gs_vgt_table_depth = le32_to_cpu(gpu_info_fw->gc_gs_table_depth);
		adev->gfx.config.gs_prim_buffer_depth = le32_to_cpu(gpu_info_fw->gc_gsprim_buff_depth);
		adev->gfx.config.double_offchip_lds_buf =
			le32_to_cpu(gpu_info_fw->gc_double_offchip_lds_buffer);
		adev->gfx.cu_info.wave_front_size = le32_to_cpu(gpu_info_fw->gc_wave_size);
		adev->gfx.cu_info.max_waves_per_simd =
			le32_to_cpu(gpu_info_fw->gc_max_waves_per_simd);
		adev->gfx.cu_info.max_scratch_slots_per_cu =
			le32_to_cpu(gpu_info_fw->gc_max_scratch_slots_per_cu);
		adev->gfx.cu_info.lds_size = le32_to_cpu(gpu_info_fw->gc_lds_size);
		break;
	}
	default:
		dev_err(adev->dev,
			"Unsupported gpu_info table %d\n", hdr->header.ucode_version);
		err = -EINVAL;
		goto out;
	}
out:
	return err;
}

/**
 * amdgpu_device_ip_early_init - run early init for hardware IPs
 *
 * @adev: amdgpu_device pointer
 *
 * Early initialization pass for hardware IPs.  The hardware IPs that make
 * up each asic are discovered each IP's early_init callback is run.  This
 * is the first stage in initializing the asic.
 * Returns 0 on success, negative error code on failure.
 */
static int amdgpu_device_ip_early_init(struct amdgpu_device *adev)
{
	int i, r;

	amdgpu_device_enable_virtual_display(adev);

	switch (adev->asic_type) {
	case CHIP_TOPAZ:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		if (adev->asic_type == CHIP_CARRIZO || adev->asic_type == CHIP_STONEY)
			adev->family = AMDGPU_FAMILY_CZ;
		else
			adev->family = AMDGPU_FAMILY_VI;

		r = vi_set_ip_blocks(adev);
		if (r)
			return r;
		break;
#ifdef CONFIG_DRM_AMDGPU_SI
	case CHIP_VERDE:
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_OLAND:
	case CHIP_HAINAN:
		adev->family = AMDGPU_FAMILY_SI;
		r = si_set_ip_blocks(adev);
		if (r)
			return r;
		break;
#endif
#ifdef CONFIG_DRM_AMDGPU_CIK
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_MULLINS:
		if ((adev->asic_type == CHIP_BONAIRE) || (adev->asic_type == CHIP_HAWAII))
			adev->family = AMDGPU_FAMILY_CI;
		else
			adev->family = AMDGPU_FAMILY_KV;

		r = cik_set_ip_blocks(adev);
		if (r)
			return r;
		break;
#endif
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
	case CHIP_RAVEN:
		if (adev->asic_type == CHIP_RAVEN)
			adev->family = AMDGPU_FAMILY_RV;
		else
			adev->family = AMDGPU_FAMILY_AI;

		r = soc15_set_ip_blocks(adev);
		if (r)
			return r;
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	r = amdgpu_device_parse_gpu_info_fw(adev);
	if (r)
		return r;

	amdgpu_amdkfd_device_probe(adev);

	if (amdgpu_sriov_vf(adev)) {
		r = amdgpu_virt_request_full_gpu(adev, true);
		if (r)
			return -EAGAIN;
	}

	adev->powerplay.pp_feature = amdgpu_pp_feature_mask;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if ((amdgpu_ip_block_mask & (1 << i)) == 0) {
			DRM_ERROR("disabled ip block: %d <%s>\n",
				  i, adev->ip_blocks[i].version->funcs->name);
			adev->ip_blocks[i].status.valid = false;
		} else {
			if (adev->ip_blocks[i].version->funcs->early_init) {
				r = adev->ip_blocks[i].version->funcs->early_init((void *)adev);
				if (r == -ENOENT) {
					adev->ip_blocks[i].status.valid = false;
				} else if (r) {
					DRM_ERROR("early_init of IP block <%s> failed %d\n",
						  adev->ip_blocks[i].version->funcs->name, r);
					return r;
				} else {
					adev->ip_blocks[i].status.valid = true;
				}
			} else {
				adev->ip_blocks[i].status.valid = true;
			}
		}
	}

	adev->cg_flags &= amdgpu_cg_mask;
	adev->pg_flags &= amdgpu_pg_mask;

	return 0;
}

static int amdgpu_device_ip_hw_init_phase1(struct amdgpu_device *adev)
{
	int i, r;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.sw)
			continue;
		if (adev->ip_blocks[i].status.hw)
			continue;
		if (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_COMMON ||
		    adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_IH) {
			r = adev->ip_blocks[i].version->funcs->hw_init(adev);
			if (r) {
				DRM_ERROR("hw_init of IP block <%s> failed %d\n",
					  adev->ip_blocks[i].version->funcs->name, r);
				return r;
			}
			adev->ip_blocks[i].status.hw = true;
		}
	}

	return 0;
}

static int amdgpu_device_ip_hw_init_phase2(struct amdgpu_device *adev)
{
	int i, r;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.sw)
			continue;
		if (adev->ip_blocks[i].status.hw)
			continue;
		r = adev->ip_blocks[i].version->funcs->hw_init(adev);
		if (r) {
			DRM_ERROR("hw_init of IP block <%s> failed %d\n",
				  adev->ip_blocks[i].version->funcs->name, r);
			return r;
		}
		adev->ip_blocks[i].status.hw = true;
	}

	return 0;
}

static int amdgpu_device_fw_loading(struct amdgpu_device *adev)
{
	int r = 0;
	int i;

	if (adev->asic_type >= CHIP_VEGA10) {
		for (i = 0; i < adev->num_ip_blocks; i++) {
			if (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_PSP) {
				if (adev->in_gpu_reset || adev->in_suspend) {
					if (amdgpu_sriov_vf(adev) && adev->in_gpu_reset)
						break; /* sriov gpu reset, psp need to do hw_init before IH because of hw limit */
					r = adev->ip_blocks[i].version->funcs->resume(adev);
					if (r) {
						DRM_ERROR("resume of IP block <%s> failed %d\n",
							  adev->ip_blocks[i].version->funcs->name, r);
						return r;
					}
				} else {
					r = adev->ip_blocks[i].version->funcs->hw_init(adev);
					if (r) {
						DRM_ERROR("hw_init of IP block <%s> failed %d\n",
						  adev->ip_blocks[i].version->funcs->name, r);
						return r;
					}
				}
				adev->ip_blocks[i].status.hw = true;
			}
		}
	}

	if (adev->powerplay.pp_funcs && adev->powerplay.pp_funcs->load_firmware) {
		r = adev->powerplay.pp_funcs->load_firmware(adev->powerplay.pp_handle);
		if (r) {
			pr_err("firmware loading failed\n");
			return r;
		}
	}

	return 0;
}

/**
 * amdgpu_device_ip_init - run init for hardware IPs
 *
 * @adev: amdgpu_device pointer
 *
 * Main initialization pass for hardware IPs.  The list of all the hardware
 * IPs that make up the asic is walked and the sw_init and hw_init callbacks
 * are run.  sw_init initializes the software state associated with each IP
 * and hw_init initializes the hardware associated with each IP.
 * Returns 0 on success, negative error code on failure.
 */
static int amdgpu_device_ip_init(struct amdgpu_device *adev)
{
	int i, r;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		r = adev->ip_blocks[i].version->funcs->sw_init((void *)adev);
		if (r) {
			DRM_ERROR("sw_init of IP block <%s> failed %d\n",
				  adev->ip_blocks[i].version->funcs->name, r);
			return r;
		}
		adev->ip_blocks[i].status.sw = true;

		/* need to do gmc hw init early so we can allocate gpu mem */
		if (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_GMC) {
			r = amdgpu_device_vram_scratch_init(adev);
			if (r) {
				DRM_ERROR("amdgpu_vram_scratch_init failed %d\n", r);
				return r;
			}
			r = adev->ip_blocks[i].version->funcs->hw_init((void *)adev);
			if (r) {
				DRM_ERROR("hw_init %d failed %d\n", i, r);
				return r;
			}
			r = amdgpu_device_wb_init(adev);
			if (r) {
				DRM_ERROR("amdgpu_device_wb_init failed %d\n", r);
				return r;
			}
			adev->ip_blocks[i].status.hw = true;

			/* right after GMC hw init, we create CSA */
			if (amdgpu_sriov_vf(adev)) {
				r = amdgpu_allocate_static_csa(adev, &adev->virt.csa_obj,
								AMDGPU_GEM_DOMAIN_VRAM,
								AMDGPU_CSA_SIZE);
				if (r) {
					DRM_ERROR("allocate CSA failed %d\n", r);
					return r;
				}
			}
		}
	}

	r = amdgpu_ucode_create_bo(adev); /* create ucode bo when sw_init complete*/
	if (r)
		return r;

	r = amdgpu_device_ip_hw_init_phase1(adev);
	if (r)
		return r;

	r = amdgpu_device_fw_loading(adev);
	if (r)
		return r;

	r = amdgpu_device_ip_hw_init_phase2(adev);
	if (r)
		return r;

	if (adev->gmc.xgmi.num_physical_nodes > 1)
		amdgpu_xgmi_add_device(adev);
	amdgpu_amdkfd_device_init(adev);

	if (amdgpu_sriov_vf(adev))
		amdgpu_virt_release_full_gpu(adev, true);

	return 0;
}

/**
 * amdgpu_device_fill_reset_magic - writes reset magic to gart pointer
 *
 * @adev: amdgpu_device pointer
 *
 * Writes a reset magic value to the gart pointer in VRAM.  The driver calls
 * this function before a GPU reset.  If the value is retained after a
 * GPU reset, VRAM has not been lost.  Some GPU resets may destry VRAM contents.
 */
static void amdgpu_device_fill_reset_magic(struct amdgpu_device *adev)
{
	memcpy(adev->reset_magic, adev->gart.ptr, AMDGPU_RESET_MAGIC_NUM);
}

/**
 * amdgpu_device_check_vram_lost - check if vram is valid
 *
 * @adev: amdgpu_device pointer
 *
 * Checks the reset magic value written to the gart pointer in VRAM.
 * The driver calls this after a GPU reset to see if the contents of
 * VRAM is lost or now.
 * returns true if vram is lost, false if not.
 */
static bool amdgpu_device_check_vram_lost(struct amdgpu_device *adev)
{
	return !!memcmp(adev->gart.ptr, adev->reset_magic,
			AMDGPU_RESET_MAGIC_NUM);
}

/**
 * amdgpu_device_set_cg_state - set clockgating for amdgpu device
 *
 * @adev: amdgpu_device pointer
 *
 * The list of all the hardware IPs that make up the asic is walked and the
 * set_clockgating_state callbacks are run.
 * Late initialization pass enabling clockgating for hardware IPs.
 * Fini or suspend, pass disabling clockgating for hardware IPs.
 * Returns 0 on success, negative error code on failure.
 */

static int amdgpu_device_set_cg_state(struct amdgpu_device *adev,
						enum amd_clockgating_state state)
{
	int i, j, r;

	if (amdgpu_emu_mode == 1)
		return 0;

	for (j = 0; j < adev->num_ip_blocks; j++) {
		i = state == AMD_CG_STATE_GATE ? j : adev->num_ip_blocks - j - 1;
		if (!adev->ip_blocks[i].status.late_initialized)
			continue;
		/* skip CG for VCE/UVD, it's handled specially */
		if (adev->ip_blocks[i].version->type != AMD_IP_BLOCK_TYPE_UVD &&
		    adev->ip_blocks[i].version->type != AMD_IP_BLOCK_TYPE_VCE &&
		    adev->ip_blocks[i].version->type != AMD_IP_BLOCK_TYPE_VCN &&
		    adev->ip_blocks[i].version->funcs->set_clockgating_state) {
			/* enable clockgating to save power */
			r = adev->ip_blocks[i].version->funcs->set_clockgating_state((void *)adev,
										     state);
			if (r) {
				DRM_ERROR("set_clockgating_state(gate) of IP block <%s> failed %d\n",
					  adev->ip_blocks[i].version->funcs->name, r);
				return r;
			}
		}
	}

	return 0;
}

static int amdgpu_device_set_pg_state(struct amdgpu_device *adev, enum amd_powergating_state state)
{
	int i, j, r;

	if (amdgpu_emu_mode == 1)
		return 0;

	for (j = 0; j < adev->num_ip_blocks; j++) {
		i = state == AMD_PG_STATE_GATE ? j : adev->num_ip_blocks - j - 1;
		if (!adev->ip_blocks[i].status.late_initialized)
			continue;
		/* skip CG for VCE/UVD, it's handled specially */
		if (adev->ip_blocks[i].version->type != AMD_IP_BLOCK_TYPE_UVD &&
		    adev->ip_blocks[i].version->type != AMD_IP_BLOCK_TYPE_VCE &&
		    adev->ip_blocks[i].version->type != AMD_IP_BLOCK_TYPE_VCN &&
		    adev->ip_blocks[i].version->funcs->set_powergating_state) {
			/* enable powergating to save power */
			r = adev->ip_blocks[i].version->funcs->set_powergating_state((void *)adev,
											state);
			if (r) {
				DRM_ERROR("set_powergating_state(gate) of IP block <%s> failed %d\n",
					  adev->ip_blocks[i].version->funcs->name, r);
				return r;
			}
		}
	}
	return 0;
}

/**
 * amdgpu_device_ip_late_init - run late init for hardware IPs
 *
 * @adev: amdgpu_device pointer
 *
 * Late initialization pass for hardware IPs.  The list of all the hardware
 * IPs that make up the asic is walked and the late_init callbacks are run.
 * late_init covers any special initialization that an IP requires
 * after all of the have been initialized or something that needs to happen
 * late in the init process.
 * Returns 0 on success, negative error code on failure.
 */
static int amdgpu_device_ip_late_init(struct amdgpu_device *adev)
{
	int i = 0, r;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.hw)
			continue;
		if (adev->ip_blocks[i].version->funcs->late_init) {
			r = adev->ip_blocks[i].version->funcs->late_init((void *)adev);
			if (r) {
				DRM_ERROR("late_init of IP block <%s> failed %d\n",
					  adev->ip_blocks[i].version->funcs->name, r);
				return r;
			}
		}
		adev->ip_blocks[i].status.late_initialized = true;
	}

	amdgpu_device_set_cg_state(adev, AMD_CG_STATE_GATE);
	amdgpu_device_set_pg_state(adev, AMD_PG_STATE_GATE);

	queue_delayed_work(system_wq, &adev->late_init_work,
			   msecs_to_jiffies(AMDGPU_RESUME_MS));

	amdgpu_device_fill_reset_magic(adev);

	return 0;
}

/**
 * amdgpu_device_ip_fini - run fini for hardware IPs
 *
 * @adev: amdgpu_device pointer
 *
 * Main teardown pass for hardware IPs.  The list of all the hardware
 * IPs that make up the asic is walked and the hw_fini and sw_fini callbacks
 * are run.  hw_fini tears down the hardware associated with each IP
 * and sw_fini tears down any software state associated with each IP.
 * Returns 0 on success, negative error code on failure.
 */
static int amdgpu_device_ip_fini(struct amdgpu_device *adev)
{
	int i, r;

	if (adev->gmc.xgmi.num_physical_nodes > 1)
		amdgpu_xgmi_remove_device(adev);

	amdgpu_amdkfd_device_fini(adev);

	amdgpu_device_set_pg_state(adev, AMD_PG_STATE_UNGATE);
	amdgpu_device_set_cg_state(adev, AMD_CG_STATE_UNGATE);

	/* need to disable SMC first */
	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.hw)
			continue;
		if (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_SMC) {
			r = adev->ip_blocks[i].version->funcs->hw_fini((void *)adev);
			/* XXX handle errors */
			if (r) {
				DRM_DEBUG("hw_fini of IP block <%s> failed %d\n",
					  adev->ip_blocks[i].version->funcs->name, r);
			}
			adev->ip_blocks[i].status.hw = false;
			break;
		}
	}

	for (i = adev->num_ip_blocks - 1; i >= 0; i--) {
		if (!adev->ip_blocks[i].status.hw)
			continue;

		r = adev->ip_blocks[i].version->funcs->hw_fini((void *)adev);
		/* XXX handle errors */
		if (r) {
			DRM_DEBUG("hw_fini of IP block <%s> failed %d\n",
				  adev->ip_blocks[i].version->funcs->name, r);
		}

		adev->ip_blocks[i].status.hw = false;
	}


	for (i = adev->num_ip_blocks - 1; i >= 0; i--) {
		if (!adev->ip_blocks[i].status.sw)
			continue;

		if (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_GMC) {
			amdgpu_ucode_free_bo(adev);
			amdgpu_free_static_csa(&adev->virt.csa_obj);
			amdgpu_device_wb_fini(adev);
			amdgpu_device_vram_scratch_fini(adev);
		}

		r = adev->ip_blocks[i].version->funcs->sw_fini((void *)adev);
		/* XXX handle errors */
		if (r) {
			DRM_DEBUG("sw_fini of IP block <%s> failed %d\n",
				  adev->ip_blocks[i].version->funcs->name, r);
		}
		adev->ip_blocks[i].status.sw = false;
		adev->ip_blocks[i].status.valid = false;
	}

	for (i = adev->num_ip_blocks - 1; i >= 0; i--) {
		if (!adev->ip_blocks[i].status.late_initialized)
			continue;
		if (adev->ip_blocks[i].version->funcs->late_fini)
			adev->ip_blocks[i].version->funcs->late_fini((void *)adev);
		adev->ip_blocks[i].status.late_initialized = false;
	}

	if (amdgpu_sriov_vf(adev))
		if (amdgpu_virt_release_full_gpu(adev, false))
			DRM_ERROR("failed to release exclusive mode on fini\n");

	return 0;
}

static int amdgpu_device_enable_mgpu_fan_boost(void)
{
	struct amdgpu_gpu_instance *gpu_ins;
	struct amdgpu_device *adev;
	int i, ret = 0;

	mutex_lock(&mgpu_info.mutex);

	/*
	 * MGPU fan boost feature should be enabled
	 * only when there are two or more dGPUs in
	 * the system
	 */
	if (mgpu_info.num_dgpu < 2)
		goto out;

	for (i = 0; i < mgpu_info.num_dgpu; i++) {
		gpu_ins = &(mgpu_info.gpu_ins[i]);
		adev = gpu_ins->adev;
		if (!(adev->flags & AMD_IS_APU) &&
		    !gpu_ins->mgpu_fan_enabled &&
		    adev->powerplay.pp_funcs &&
		    adev->powerplay.pp_funcs->enable_mgpu_fan_boost) {
			ret = amdgpu_dpm_enable_mgpu_fan_boost(adev);
			if (ret)
				break;

			gpu_ins->mgpu_fan_enabled = 1;
		}
	}

out:
	mutex_unlock(&mgpu_info.mutex);

	return ret;
}

/**
 * amdgpu_device_ip_late_init_func_handler - work handler for ib test
 *
 * @work: work_struct.
 */
static void amdgpu_device_ip_late_init_func_handler(struct work_struct *work)
{
	struct amdgpu_device *adev =
		container_of(work, struct amdgpu_device, late_init_work.work);
	int r;

	r = amdgpu_ib_ring_tests(adev);
	if (r)
		DRM_ERROR("ib ring test failed (%d).\n", r);

	r = amdgpu_device_enable_mgpu_fan_boost();
	if (r)
		DRM_ERROR("enable mgpu fan boost failed (%d).\n", r);
}

static void amdgpu_device_delay_enable_gfx_off(struct work_struct *work)
{
	struct amdgpu_device *adev =
		container_of(work, struct amdgpu_device, gfx.gfx_off_delay_work.work);

	mutex_lock(&adev->gfx.gfx_off_mutex);
	if (!adev->gfx.gfx_off_state && !adev->gfx.gfx_off_req_count) {
		if (!amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_GFX, true))
			adev->gfx.gfx_off_state = true;
	}
	mutex_unlock(&adev->gfx.gfx_off_mutex);
}

/**
 * amdgpu_device_ip_suspend_phase1 - run suspend for hardware IPs (phase 1)
 *
 * @adev: amdgpu_device pointer
 *
 * Main suspend function for hardware IPs.  The list of all the hardware
 * IPs that make up the asic is walked, clockgating is disabled and the
 * suspend callbacks are run.  suspend puts the hardware and software state
 * in each IP into a state suitable for suspend.
 * Returns 0 on success, negative error code on failure.
 */
static int amdgpu_device_ip_suspend_phase1(struct amdgpu_device *adev)
{
	int i, r;

	amdgpu_device_set_pg_state(adev, AMD_PG_STATE_UNGATE);
	amdgpu_device_set_cg_state(adev, AMD_CG_STATE_UNGATE);

	for (i = adev->num_ip_blocks - 1; i >= 0; i--) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		/* displays are handled separately */
		if (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_DCE) {
			/* XXX handle errors */
			r = adev->ip_blocks[i].version->funcs->suspend(adev);
			/* XXX handle errors */
			if (r) {
				DRM_ERROR("suspend of IP block <%s> failed %d\n",
					  adev->ip_blocks[i].version->funcs->name, r);
			}
		}
	}

	return 0;
}

/**
 * amdgpu_device_ip_suspend_phase2 - run suspend for hardware IPs (phase 2)
 *
 * @adev: amdgpu_device pointer
 *
 * Main suspend function for hardware IPs.  The list of all the hardware
 * IPs that make up the asic is walked, clockgating is disabled and the
 * suspend callbacks are run.  suspend puts the hardware and software state
 * in each IP into a state suitable for suspend.
 * Returns 0 on success, negative error code on failure.
 */
static int amdgpu_device_ip_suspend_phase2(struct amdgpu_device *adev)
{
	int i, r;

	for (i = adev->num_ip_blocks - 1; i >= 0; i--) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		/* displays are handled in phase1 */
		if (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_DCE)
			continue;
		/* XXX handle errors */
		r = adev->ip_blocks[i].version->funcs->suspend(adev);
		/* XXX handle errors */
		if (r) {
			DRM_ERROR("suspend of IP block <%s> failed %d\n",
				  adev->ip_blocks[i].version->funcs->name, r);
		}
	}

	return 0;
}

/**
 * amdgpu_device_ip_suspend - run suspend for hardware IPs
 *
 * @adev: amdgpu_device pointer
 *
 * Main suspend function for hardware IPs.  The list of all the hardware
 * IPs that make up the asic is walked, clockgating is disabled and the
 * suspend callbacks are run.  suspend puts the hardware and software state
 * in each IP into a state suitable for suspend.
 * Returns 0 on success, negative error code on failure.
 */
int amdgpu_device_ip_suspend(struct amdgpu_device *adev)
{
	int r;

	if (amdgpu_sriov_vf(adev))
		amdgpu_virt_request_full_gpu(adev, false);

	r = amdgpu_device_ip_suspend_phase1(adev);
	if (r)
		return r;
	r = amdgpu_device_ip_suspend_phase2(adev);

	if (amdgpu_sriov_vf(adev))
		amdgpu_virt_release_full_gpu(adev, false);

	return r;
}

static int amdgpu_device_ip_reinit_early_sriov(struct amdgpu_device *adev)
{
	int i, r;

	static enum amd_ip_block_type ip_order[] = {
		AMD_IP_BLOCK_TYPE_GMC,
		AMD_IP_BLOCK_TYPE_COMMON,
		AMD_IP_BLOCK_TYPE_PSP,
		AMD_IP_BLOCK_TYPE_IH,
	};

	for (i = 0; i < ARRAY_SIZE(ip_order); i++) {
		int j;
		struct amdgpu_ip_block *block;

		for (j = 0; j < adev->num_ip_blocks; j++) {
			block = &adev->ip_blocks[j];

			if (block->version->type != ip_order[i] ||
				!block->status.valid)
				continue;

			r = block->version->funcs->hw_init(adev);
			DRM_INFO("RE-INIT: %s %s\n", block->version->funcs->name, r?"failed":"succeeded");
			if (r)
				return r;
		}
	}

	return 0;
}

static int amdgpu_device_ip_reinit_late_sriov(struct amdgpu_device *adev)
{
	int i, r;

	static enum amd_ip_block_type ip_order[] = {
		AMD_IP_BLOCK_TYPE_SMC,
		AMD_IP_BLOCK_TYPE_DCE,
		AMD_IP_BLOCK_TYPE_GFX,
		AMD_IP_BLOCK_TYPE_SDMA,
		AMD_IP_BLOCK_TYPE_UVD,
		AMD_IP_BLOCK_TYPE_VCE
	};

	for (i = 0; i < ARRAY_SIZE(ip_order); i++) {
		int j;
		struct amdgpu_ip_block *block;

		for (j = 0; j < adev->num_ip_blocks; j++) {
			block = &adev->ip_blocks[j];

			if (block->version->type != ip_order[i] ||
				!block->status.valid)
				continue;

			r = block->version->funcs->hw_init(adev);
			DRM_INFO("RE-INIT: %s %s\n", block->version->funcs->name, r?"failed":"succeeded");
			if (r)
				return r;
		}
	}

	return 0;
}

/**
 * amdgpu_device_ip_resume_phase1 - run resume for hardware IPs
 *
 * @adev: amdgpu_device pointer
 *
 * First resume function for hardware IPs.  The list of all the hardware
 * IPs that make up the asic is walked and the resume callbacks are run for
 * COMMON, GMC, and IH.  resume puts the hardware into a functional state
 * after a suspend and updates the software state as necessary.  This
 * function is also used for restoring the GPU after a GPU reset.
 * Returns 0 on success, negative error code on failure.
 */
static int amdgpu_device_ip_resume_phase1(struct amdgpu_device *adev)
{
	int i, r;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_COMMON ||
		    adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_GMC ||
		    adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_IH) {
			r = adev->ip_blocks[i].version->funcs->resume(adev);
			if (r) {
				DRM_ERROR("resume of IP block <%s> failed %d\n",
					  adev->ip_blocks[i].version->funcs->name, r);
				return r;
			}
		}
	}

	return 0;
}

/**
 * amdgpu_device_ip_resume_phase2 - run resume for hardware IPs
 *
 * @adev: amdgpu_device pointer
 *
 * First resume function for hardware IPs.  The list of all the hardware
 * IPs that make up the asic is walked and the resume callbacks are run for
 * all blocks except COMMON, GMC, and IH.  resume puts the hardware into a
 * functional state after a suspend and updates the software state as
 * necessary.  This function is also used for restoring the GPU after a GPU
 * reset.
 * Returns 0 on success, negative error code on failure.
 */
static int amdgpu_device_ip_resume_phase2(struct amdgpu_device *adev)
{
	int i, r;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_COMMON ||
		    adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_GMC ||
		    adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_IH ||
		    adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_PSP)
			continue;
		r = adev->ip_blocks[i].version->funcs->resume(adev);
		if (r) {
			DRM_ERROR("resume of IP block <%s> failed %d\n",
				  adev->ip_blocks[i].version->funcs->name, r);
			return r;
		}
	}

	return 0;
}

/**
 * amdgpu_device_ip_resume - run resume for hardware IPs
 *
 * @adev: amdgpu_device pointer
 *
 * Main resume function for hardware IPs.  The hardware IPs
 * are split into two resume functions because they are
 * are also used in in recovering from a GPU reset and some additional
 * steps need to be take between them.  In this case (S3/S4) they are
 * run sequentially.
 * Returns 0 on success, negative error code on failure.
 */
static int amdgpu_device_ip_resume(struct amdgpu_device *adev)
{
	int r;

	r = amdgpu_device_ip_resume_phase1(adev);
	if (r)
		return r;

	r = amdgpu_device_fw_loading(adev);
	if (r)
		return r;

	r = amdgpu_device_ip_resume_phase2(adev);

	return r;
}

/**
 * amdgpu_device_detect_sriov_bios - determine if the board supports SR-IOV
 *
 * @adev: amdgpu_device pointer
 *
 * Query the VBIOS data tables to determine if the board supports SR-IOV.
 */
static void amdgpu_device_detect_sriov_bios(struct amdgpu_device *adev)
{
	if (amdgpu_sriov_vf(adev)) {
		if (adev->is_atom_fw) {
			if (amdgpu_atomfirmware_gpu_supports_virtualization(adev))
				adev->virt.caps |= AMDGPU_SRIOV_CAPS_SRIOV_VBIOS;
		} else {
			if (amdgpu_atombios_has_gpu_virtualization_table(adev))
				adev->virt.caps |= AMDGPU_SRIOV_CAPS_SRIOV_VBIOS;
		}

		if (!(adev->virt.caps & AMDGPU_SRIOV_CAPS_SRIOV_VBIOS))
			amdgpu_vf_error_put(adev, AMDGIM_ERROR_VF_NO_VBIOS, 0, 0);
	}
}

/**
 * amdgpu_device_asic_has_dc_support - determine if DC supports the asic
 *
 * @asic_type: AMD asic type
 *
 * Check if there is DC (new modesetting infrastructre) support for an asic.
 * returns true if DC has support, false if not.
 */
bool amdgpu_device_asic_has_dc_support(enum amd_asic_type asic_type)
{
	switch (asic_type) {
#if defined(CONFIG_DRM_AMD_DC)
	case CHIP_BONAIRE:
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_MULLINS:
		/*
		 * We have systems in the wild with these ASICs that require
		 * LVDS and VGA support which is not supported with DC.
		 *
		 * Fallback to the non-DC driver here by default so as not to
		 * cause regressions.
		 */
		return amdgpu_dc > 0;
	case CHIP_HAWAII:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	case CHIP_RAVEN:
#endif
		return amdgpu_dc != 0;
#endif
	default:
		return false;
	}
}

/**
 * amdgpu_device_has_dc_support - check if dc is supported
 *
 * @adev: amdgpu_device_pointer
 *
 * Returns true for supported, false for not supported
 */
bool amdgpu_device_has_dc_support(struct amdgpu_device *adev)
{
	if (amdgpu_sriov_vf(adev))
		return false;

	return amdgpu_device_asic_has_dc_support(adev->asic_type);
}


static void amdgpu_device_xgmi_reset_func(struct work_struct *__work)
{
	struct amdgpu_device *adev =
		container_of(__work, struct amdgpu_device, xgmi_reset_work);

	adev->asic_reset_res =  amdgpu_asic_reset(adev);
	if (adev->asic_reset_res)
		DRM_WARN("ASIC reset failed with err r, %d for drm dev, %s",
			 adev->asic_reset_res, adev->ddev->unique);
}


/**
 * amdgpu_device_init - initialize the driver
 *
 * @adev: amdgpu_device pointer
 * @ddev: drm dev pointer
 * @pdev: pci dev pointer
 * @flags: driver flags
 *
 * Initializes the driver info and hw (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver startup.
 */
int amdgpu_device_init(struct amdgpu_device *adev,
		       struct drm_device *ddev,
		       struct pci_dev *pdev,
		       uint32_t flags)
{
	int r, i;
	bool runtime = false;
	u32 max_MBps;

	adev->shutdown = false;
	adev->dev = &pdev->dev;
	adev->ddev = ddev;
	adev->pdev = pdev;
	adev->flags = flags;
	adev->asic_type = flags & AMD_ASIC_MASK;
	adev->usec_timeout = AMDGPU_MAX_USEC_TIMEOUT;
	if (amdgpu_emu_mode == 1)
		adev->usec_timeout *= 2;
	adev->gmc.gart_size = 512 * 1024 * 1024;
	adev->accel_working = false;
	adev->num_rings = 0;
	adev->mman.buffer_funcs = NULL;
	adev->mman.buffer_funcs_ring = NULL;
	adev->vm_manager.vm_pte_funcs = NULL;
	adev->vm_manager.vm_pte_num_rqs = 0;
	adev->gmc.gmc_funcs = NULL;
	adev->fence_context = dma_fence_context_alloc(AMDGPU_MAX_RINGS);
	bitmap_zero(adev->gfx.pipe_reserve_bitmap, AMDGPU_MAX_COMPUTE_QUEUES);

	adev->smc_rreg = &amdgpu_invalid_rreg;
	adev->smc_wreg = &amdgpu_invalid_wreg;
	adev->pcie_rreg = &amdgpu_invalid_rreg;
	adev->pcie_wreg = &amdgpu_invalid_wreg;
	adev->pciep_rreg = &amdgpu_invalid_rreg;
	adev->pciep_wreg = &amdgpu_invalid_wreg;
	adev->uvd_ctx_rreg = &amdgpu_invalid_rreg;
	adev->uvd_ctx_wreg = &amdgpu_invalid_wreg;
	adev->didt_rreg = &amdgpu_invalid_rreg;
	adev->didt_wreg = &amdgpu_invalid_wreg;
	adev->gc_cac_rreg = &amdgpu_invalid_rreg;
	adev->gc_cac_wreg = &amdgpu_invalid_wreg;
	adev->audio_endpt_rreg = &amdgpu_block_invalid_rreg;
	adev->audio_endpt_wreg = &amdgpu_block_invalid_wreg;

	DRM_INFO("initializing kernel modesetting (%s 0x%04X:0x%04X 0x%04X:0x%04X 0x%02X).\n",
		 amdgpu_asic_name[adev->asic_type], pdev->vendor, pdev->device,
		 pdev->subsystem_vendor, pdev->subsystem_device, pdev->revision);

	/* mutex initialization are all done here so we
	 * can recall function without having locking issues */
	atomic_set(&adev->irq.ih.lock, 0);
	mutex_init(&adev->firmware.mutex);
	mutex_init(&adev->pm.mutex);
	mutex_init(&adev->gfx.gpu_clock_mutex);
	mutex_init(&adev->srbm_mutex);
	mutex_init(&adev->gfx.pipe_reserve_mutex);
	mutex_init(&adev->gfx.gfx_off_mutex);
	mutex_init(&adev->grbm_idx_mutex);
	mutex_init(&adev->mn_lock);
	mutex_init(&adev->virt.vf_errors.lock);
	hash_init(adev->mn_hash);
	mutex_init(&adev->lock_reset);

	amdgpu_device_check_arguments(adev);

	spin_lock_init(&adev->mmio_idx_lock);
	spin_lock_init(&adev->smc_idx_lock);
	spin_lock_init(&adev->pcie_idx_lock);
	spin_lock_init(&adev->uvd_ctx_idx_lock);
	spin_lock_init(&adev->didt_idx_lock);
	spin_lock_init(&adev->gc_cac_idx_lock);
	spin_lock_init(&adev->se_cac_idx_lock);
	spin_lock_init(&adev->audio_endpt_idx_lock);
	spin_lock_init(&adev->mm_stats.lock);

	INIT_LIST_HEAD(&adev->shadow_list);
	mutex_init(&adev->shadow_list_lock);

	INIT_LIST_HEAD(&adev->ring_lru_list);
	spin_lock_init(&adev->ring_lru_list_lock);

	INIT_DELAYED_WORK(&adev->late_init_work,
			  amdgpu_device_ip_late_init_func_handler);
	INIT_DELAYED_WORK(&adev->gfx.gfx_off_delay_work,
			  amdgpu_device_delay_enable_gfx_off);

	INIT_WORK(&adev->xgmi_reset_work, amdgpu_device_xgmi_reset_func);

	adev->gfx.gfx_off_req_count = 1;
	adev->pm.ac_power = power_supply_is_system_supplied() > 0 ? true : false;

	/* Registers mapping */
	/* TODO: block userspace mapping of io register */
	if (adev->asic_type >= CHIP_BONAIRE) {
		adev->rmmio_base = pci_resource_start(adev->pdev, 5);
		adev->rmmio_size = pci_resource_len(adev->pdev, 5);
	} else {
		adev->rmmio_base = pci_resource_start(adev->pdev, 2);
		adev->rmmio_size = pci_resource_len(adev->pdev, 2);
	}

	adev->rmmio = ioremap(adev->rmmio_base, adev->rmmio_size);
	if (adev->rmmio == NULL) {
		return -ENOMEM;
	}
	DRM_INFO("register mmio base: 0x%08X\n", (uint32_t)adev->rmmio_base);
	DRM_INFO("register mmio size: %u\n", (unsigned)adev->rmmio_size);

	/* io port mapping */
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		if (pci_resource_flags(adev->pdev, i) & IORESOURCE_IO) {
			adev->rio_mem_size = pci_resource_len(adev->pdev, i);
			adev->rio_mem = pci_iomap(adev->pdev, i, adev->rio_mem_size);
			break;
		}
	}
	if (adev->rio_mem == NULL)
		DRM_INFO("PCI I/O BAR is not found.\n");

	amdgpu_device_get_pcie_info(adev);

	/* early init functions */
	r = amdgpu_device_ip_early_init(adev);
	if (r)
		return r;

	/* doorbell bar mapping and doorbell index init*/
	amdgpu_device_doorbell_init(adev);

	/* if we have > 1 VGA cards, then disable the amdgpu VGA resources */
	/* this will fail for cards that aren't VGA class devices, just
	 * ignore it */
	vga_client_register(adev->pdev, adev, NULL, amdgpu_device_vga_set_decode);

	if (amdgpu_device_is_px(ddev))
		runtime = true;
	if (!pci_is_thunderbolt_attached(adev->pdev))
		vga_switcheroo_register_client(adev->pdev,
					       &amdgpu_switcheroo_ops, runtime);
	if (runtime)
		vga_switcheroo_init_domain_pm_ops(adev->dev, &adev->vga_pm_domain);

	if (amdgpu_emu_mode == 1) {
		/* post the asic on emulation mode */
		emu_soc_asic_init(adev);
		goto fence_driver_init;
	}

	/* Read BIOS */
	if (!amdgpu_get_bios(adev)) {
		r = -EINVAL;
		goto failed;
	}

	r = amdgpu_atombios_init(adev);
	if (r) {
		dev_err(adev->dev, "amdgpu_atombios_init failed\n");
		amdgpu_vf_error_put(adev, AMDGIM_ERROR_VF_ATOMBIOS_INIT_FAIL, 0, 0);
		goto failed;
	}

	/* detect if we are with an SRIOV vbios */
	amdgpu_device_detect_sriov_bios(adev);

	/* Post card if necessary */
	if (amdgpu_device_need_post(adev)) {
		if (!adev->bios) {
			dev_err(adev->dev, "no vBIOS found\n");
			r = -EINVAL;
			goto failed;
		}
		DRM_INFO("GPU posting now...\n");
		r = amdgpu_atom_asic_init(adev->mode_info.atom_context);
		if (r) {
			dev_err(adev->dev, "gpu post error!\n");
			goto failed;
		}
	}

	if (adev->is_atom_fw) {
		/* Initialize clocks */
		r = amdgpu_atomfirmware_get_clock_info(adev);
		if (r) {
			dev_err(adev->dev, "amdgpu_atomfirmware_get_clock_info failed\n");
			amdgpu_vf_error_put(adev, AMDGIM_ERROR_VF_ATOMBIOS_GET_CLOCK_FAIL, 0, 0);
			goto failed;
		}
	} else {
		/* Initialize clocks */
		r = amdgpu_atombios_get_clock_info(adev);
		if (r) {
			dev_err(adev->dev, "amdgpu_atombios_get_clock_info failed\n");
			amdgpu_vf_error_put(adev, AMDGIM_ERROR_VF_ATOMBIOS_GET_CLOCK_FAIL, 0, 0);
			goto failed;
		}
		/* init i2c buses */
		if (!amdgpu_device_has_dc_support(adev))
			amdgpu_atombios_i2c_init(adev);
	}

fence_driver_init:
	/* Fence driver */
	r = amdgpu_fence_driver_init(adev);
	if (r) {
		dev_err(adev->dev, "amdgpu_fence_driver_init failed\n");
		amdgpu_vf_error_put(adev, AMDGIM_ERROR_VF_FENCE_INIT_FAIL, 0, 0);
		goto failed;
	}

	/* init the mode config */
	drm_mode_config_init(adev->ddev);

	r = amdgpu_device_ip_init(adev);
	if (r) {
		/* failed in exclusive mode due to timeout */
		if (amdgpu_sriov_vf(adev) &&
		    !amdgpu_sriov_runtime(adev) &&
		    amdgpu_virt_mmio_blocked(adev) &&
		    !amdgpu_virt_wait_reset(adev)) {
			dev_err(adev->dev, "VF exclusive mode timeout\n");
			/* Don't send request since VF is inactive. */
			adev->virt.caps &= ~AMDGPU_SRIOV_CAPS_RUNTIME;
			adev->virt.ops = NULL;
			r = -EAGAIN;
			goto failed;
		}
		dev_err(adev->dev, "amdgpu_device_ip_init failed\n");
		amdgpu_vf_error_put(adev, AMDGIM_ERROR_VF_AMDGPU_INIT_FAIL, 0, 0);
		goto failed;
	}

	adev->accel_working = true;

	amdgpu_vm_check_compute_bug(adev);

	/* Initialize the buffer migration limit. */
	if (amdgpu_moverate >= 0)
		max_MBps = amdgpu_moverate;
	else
		max_MBps = 8; /* Allow 8 MB/s. */
	/* Get a log2 for easy divisions. */
	adev->mm_stats.log2_max_MBps = ilog2(max(1u, max_MBps));

	r = amdgpu_ib_pool_init(adev);
	if (r) {
		dev_err(adev->dev, "IB initialization failed (%d).\n", r);
		amdgpu_vf_error_put(adev, AMDGIM_ERROR_VF_IB_INIT_FAIL, 0, r);
		goto failed;
	}

	if (amdgpu_sriov_vf(adev))
		amdgpu_virt_init_data_exchange(adev);

	amdgpu_fbdev_init(adev);

	r = amdgpu_pm_sysfs_init(adev);
	if (r)
		DRM_ERROR("registering pm debugfs failed (%d).\n", r);

	r = amdgpu_debugfs_gem_init(adev);
	if (r)
		DRM_ERROR("registering gem debugfs failed (%d).\n", r);

	r = amdgpu_debugfs_regs_init(adev);
	if (r)
		DRM_ERROR("registering register debugfs failed (%d).\n", r);

	r = amdgpu_debugfs_firmware_init(adev);
	if (r)
		DRM_ERROR("registering firmware debugfs failed (%d).\n", r);

	r = amdgpu_debugfs_init(adev);
	if (r)
		DRM_ERROR("Creating debugfs files failed (%d).\n", r);

	if ((amdgpu_testing & 1)) {
		if (adev->accel_working)
			amdgpu_test_moves(adev);
		else
			DRM_INFO("amdgpu: acceleration disabled, skipping move tests\n");
	}
	if (amdgpu_benchmarking) {
		if (adev->accel_working)
			amdgpu_benchmark(adev, amdgpu_benchmarking);
		else
			DRM_INFO("amdgpu: acceleration disabled, skipping benchmarks\n");
	}

	/* enable clockgating, etc. after ib tests, etc. since some blocks require
	 * explicit gating rather than handling it automatically.
	 */
	r = amdgpu_device_ip_late_init(adev);
	if (r) {
		dev_err(adev->dev, "amdgpu_device_ip_late_init failed\n");
		amdgpu_vf_error_put(adev, AMDGIM_ERROR_VF_AMDGPU_LATE_INIT_FAIL, 0, r);
		goto failed;
	}

	return 0;

failed:
	amdgpu_vf_error_trans_all(adev);
	if (runtime)
		vga_switcheroo_fini_domain_pm_ops(adev->dev);

	return r;
}

/**
 * amdgpu_device_fini - tear down the driver
 *
 * @adev: amdgpu_device pointer
 *
 * Tear down the driver info (all asics).
 * Called at driver shutdown.
 */
void amdgpu_device_fini(struct amdgpu_device *adev)
{
	int r;

	DRM_INFO("amdgpu: finishing device.\n");
	adev->shutdown = true;
	/* disable all interrupts */
	amdgpu_irq_disable_all(adev);
	if (adev->mode_info.mode_config_initialized){
		if (!amdgpu_device_has_dc_support(adev))
			drm_crtc_force_disable_all(adev->ddev);
		else
			drm_atomic_helper_shutdown(adev->ddev);
	}
	amdgpu_ib_pool_fini(adev);
	amdgpu_fence_driver_fini(adev);
	amdgpu_pm_sysfs_fini(adev);
	amdgpu_fbdev_fini(adev);
	r = amdgpu_device_ip_fini(adev);
	if (adev->firmware.gpu_info_fw) {
		release_firmware(adev->firmware.gpu_info_fw);
		adev->firmware.gpu_info_fw = NULL;
	}
	adev->accel_working = false;
	cancel_delayed_work_sync(&adev->late_init_work);
	/* free i2c buses */
	if (!amdgpu_device_has_dc_support(adev))
		amdgpu_i2c_fini(adev);

	if (amdgpu_emu_mode != 1)
		amdgpu_atombios_fini(adev);

	kfree(adev->bios);
	adev->bios = NULL;
	if (!pci_is_thunderbolt_attached(adev->pdev))
		vga_switcheroo_unregister_client(adev->pdev);
	if (adev->flags & AMD_IS_PX)
		vga_switcheroo_fini_domain_pm_ops(adev->dev);
	vga_client_register(adev->pdev, NULL, NULL, NULL);
	if (adev->rio_mem)
		pci_iounmap(adev->pdev, adev->rio_mem);
	adev->rio_mem = NULL;
	iounmap(adev->rmmio);
	adev->rmmio = NULL;
	amdgpu_device_doorbell_fini(adev);
	amdgpu_debugfs_regs_cleanup(adev);
}


/*
 * Suspend & resume.
 */
/**
 * amdgpu_device_suspend - initiate device suspend
 *
 * @dev: drm dev pointer
 * @suspend: suspend state
 * @fbcon : notify the fbdev of suspend
 *
 * Puts the hw in the suspend state (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver suspend.
 */
int amdgpu_device_suspend(struct drm_device *dev, bool suspend, bool fbcon)
{
	struct amdgpu_device *adev;
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	int r;

	if (dev == NULL || dev->dev_private == NULL) {
		return -ENODEV;
	}

	adev = dev->dev_private;

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	adev->in_suspend = true;
	drm_kms_helper_poll_disable(dev);

	if (fbcon)
		amdgpu_fbdev_set_suspend(adev, 1);

	cancel_delayed_work_sync(&adev->late_init_work);

	if (!amdgpu_device_has_dc_support(adev)) {
		/* turn off display hw */
		drm_modeset_lock_all(dev);
		list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
			drm_helper_connector_dpms(connector, DRM_MODE_DPMS_OFF);
		}
		drm_modeset_unlock_all(dev);
			/* unpin the front buffers and cursors */
		list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
			struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
			struct drm_framebuffer *fb = crtc->primary->fb;
			struct amdgpu_bo *robj;

			if (amdgpu_crtc->cursor_bo) {
				struct amdgpu_bo *aobj = gem_to_amdgpu_bo(amdgpu_crtc->cursor_bo);
				r = amdgpu_bo_reserve(aobj, true);
				if (r == 0) {
					amdgpu_bo_unpin(aobj);
					amdgpu_bo_unreserve(aobj);
				}
			}

			if (fb == NULL || fb->obj[0] == NULL) {
				continue;
			}
			robj = gem_to_amdgpu_bo(fb->obj[0]);
			/* don't unpin kernel fb objects */
			if (!amdgpu_fbdev_robj_is_fb(adev, robj)) {
				r = amdgpu_bo_reserve(robj, true);
				if (r == 0) {
					amdgpu_bo_unpin(robj);
					amdgpu_bo_unreserve(robj);
				}
			}
		}
	}

	amdgpu_amdkfd_suspend(adev);

	r = amdgpu_device_ip_suspend_phase1(adev);

	/* evict vram memory */
	amdgpu_bo_evict_vram(adev);

	amdgpu_fence_driver_suspend(adev);

	r = amdgpu_device_ip_suspend_phase2(adev);

	/* evict remaining vram memory
	 * This second call to evict vram is to evict the gart page table
	 * using the CPU.
	 */
	amdgpu_bo_evict_vram(adev);

	pci_save_state(dev->pdev);
	if (suspend) {
		/* Shut down the device */
		pci_disable_device(dev->pdev);
		pci_set_power_state(dev->pdev, PCI_D3hot);
	} else {
		r = amdgpu_asic_reset(adev);
		if (r)
			DRM_ERROR("amdgpu asic reset failed\n");
	}

	return 0;
}

/**
 * amdgpu_device_resume - initiate device resume
 *
 * @dev: drm dev pointer
 * @resume: resume state
 * @fbcon : notify the fbdev of resume
 *
 * Bring the hw back to operating state (all asics).
 * Returns 0 for success or an error on failure.
 * Called at driver resume.
 */
int amdgpu_device_resume(struct drm_device *dev, bool resume, bool fbcon)
{
	struct drm_connector *connector;
	struct amdgpu_device *adev = dev->dev_private;
	struct drm_crtc *crtc;
	int r = 0;

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	if (resume) {
		pci_set_power_state(dev->pdev, PCI_D0);
		pci_restore_state(dev->pdev);
		r = pci_enable_device(dev->pdev);
		if (r)
			return r;
	}

	/* post card */
	if (amdgpu_device_need_post(adev)) {
		r = amdgpu_atom_asic_init(adev->mode_info.atom_context);
		if (r)
			DRM_ERROR("amdgpu asic init failed\n");
	}

	r = amdgpu_device_ip_resume(adev);
	if (r) {
		DRM_ERROR("amdgpu_device_ip_resume failed (%d).\n", r);
		return r;
	}
	amdgpu_fence_driver_resume(adev);


	r = amdgpu_device_ip_late_init(adev);
	if (r)
		return r;

	if (!amdgpu_device_has_dc_support(adev)) {
		/* pin cursors */
		list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
			struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

			if (amdgpu_crtc->cursor_bo) {
				struct amdgpu_bo *aobj = gem_to_amdgpu_bo(amdgpu_crtc->cursor_bo);
				r = amdgpu_bo_reserve(aobj, true);
				if (r == 0) {
					r = amdgpu_bo_pin(aobj, AMDGPU_GEM_DOMAIN_VRAM);
					if (r != 0)
						DRM_ERROR("Failed to pin cursor BO (%d)\n", r);
					amdgpu_crtc->cursor_addr = amdgpu_bo_gpu_offset(aobj);
					amdgpu_bo_unreserve(aobj);
				}
			}
		}
	}
	r = amdgpu_amdkfd_resume(adev);
	if (r)
		return r;

	/* Make sure IB tests flushed */
	flush_delayed_work(&adev->late_init_work);

	/* blat the mode back in */
	if (fbcon) {
		if (!amdgpu_device_has_dc_support(adev)) {
			/* pre DCE11 */
			drm_helper_resume_force_mode(dev);

			/* turn on display hw */
			drm_modeset_lock_all(dev);
			list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
				drm_helper_connector_dpms(connector, DRM_MODE_DPMS_ON);
			}
			drm_modeset_unlock_all(dev);
		}
		amdgpu_fbdev_set_suspend(adev, 0);
	}

	drm_kms_helper_poll_enable(dev);

	/*
	 * Most of the connector probing functions try to acquire runtime pm
	 * refs to ensure that the GPU is powered on when connector polling is
	 * performed. Since we're calling this from a runtime PM callback,
	 * trying to acquire rpm refs will cause us to deadlock.
	 *
	 * Since we're guaranteed to be holding the rpm lock, it's safe to
	 * temporarily disable the rpm helpers so this doesn't deadlock us.
	 */
#ifdef CONFIG_PM
	dev->dev->power.disable_depth++;
#endif
	if (!amdgpu_device_has_dc_support(adev))
		drm_helper_hpd_irq_event(dev);
	else
		drm_kms_helper_hotplug_event(dev);
#ifdef CONFIG_PM
	dev->dev->power.disable_depth--;
#endif
	adev->in_suspend = false;

	return 0;
}

/**
 * amdgpu_device_ip_check_soft_reset - did soft reset succeed
 *
 * @adev: amdgpu_device pointer
 *
 * The list of all the hardware IPs that make up the asic is walked and
 * the check_soft_reset callbacks are run.  check_soft_reset determines
 * if the asic is still hung or not.
 * Returns true if any of the IPs are still in a hung state, false if not.
 */
static bool amdgpu_device_ip_check_soft_reset(struct amdgpu_device *adev)
{
	int i;
	bool asic_hang = false;

	if (amdgpu_sriov_vf(adev))
		return true;

	if (amdgpu_asic_need_full_reset(adev))
		return true;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (adev->ip_blocks[i].version->funcs->check_soft_reset)
			adev->ip_blocks[i].status.hang =
				adev->ip_blocks[i].version->funcs->check_soft_reset(adev);
		if (adev->ip_blocks[i].status.hang) {
			DRM_INFO("IP block:%s is hung!\n", adev->ip_blocks[i].version->funcs->name);
			asic_hang = true;
		}
	}
	return asic_hang;
}

/**
 * amdgpu_device_ip_pre_soft_reset - prepare for soft reset
 *
 * @adev: amdgpu_device pointer
 *
 * The list of all the hardware IPs that make up the asic is walked and the
 * pre_soft_reset callbacks are run if the block is hung.  pre_soft_reset
 * handles any IP specific hardware or software state changes that are
 * necessary for a soft reset to succeed.
 * Returns 0 on success, negative error code on failure.
 */
static int amdgpu_device_ip_pre_soft_reset(struct amdgpu_device *adev)
{
	int i, r = 0;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (adev->ip_blocks[i].status.hang &&
		    adev->ip_blocks[i].version->funcs->pre_soft_reset) {
			r = adev->ip_blocks[i].version->funcs->pre_soft_reset(adev);
			if (r)
				return r;
		}
	}

	return 0;
}

/**
 * amdgpu_device_ip_need_full_reset - check if a full asic reset is needed
 *
 * @adev: amdgpu_device pointer
 *
 * Some hardware IPs cannot be soft reset.  If they are hung, a full gpu
 * reset is necessary to recover.
 * Returns true if a full asic reset is required, false if not.
 */
static bool amdgpu_device_ip_need_full_reset(struct amdgpu_device *adev)
{
	int i;

	if (amdgpu_asic_need_full_reset(adev))
		return true;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if ((adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_GMC) ||
		    (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_SMC) ||
		    (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_ACP) ||
		    (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_DCE) ||
		     adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_PSP) {
			if (adev->ip_blocks[i].status.hang) {
				DRM_INFO("Some block need full reset!\n");
				return true;
			}
		}
	}
	return false;
}

/**
 * amdgpu_device_ip_soft_reset - do a soft reset
 *
 * @adev: amdgpu_device pointer
 *
 * The list of all the hardware IPs that make up the asic is walked and the
 * soft_reset callbacks are run if the block is hung.  soft_reset handles any
 * IP specific hardware or software state changes that are necessary to soft
 * reset the IP.
 * Returns 0 on success, negative error code on failure.
 */
static int amdgpu_device_ip_soft_reset(struct amdgpu_device *adev)
{
	int i, r = 0;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (adev->ip_blocks[i].status.hang &&
		    adev->ip_blocks[i].version->funcs->soft_reset) {
			r = adev->ip_blocks[i].version->funcs->soft_reset(adev);
			if (r)
				return r;
		}
	}

	return 0;
}

/**
 * amdgpu_device_ip_post_soft_reset - clean up from soft reset
 *
 * @adev: amdgpu_device pointer
 *
 * The list of all the hardware IPs that make up the asic is walked and the
 * post_soft_reset callbacks are run if the asic was hung.  post_soft_reset
 * handles any IP specific hardware or software state changes that are
 * necessary after the IP has been soft reset.
 * Returns 0 on success, negative error code on failure.
 */
static int amdgpu_device_ip_post_soft_reset(struct amdgpu_device *adev)
{
	int i, r = 0;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (adev->ip_blocks[i].status.hang &&
		    adev->ip_blocks[i].version->funcs->post_soft_reset)
			r = adev->ip_blocks[i].version->funcs->post_soft_reset(adev);
		if (r)
			return r;
	}

	return 0;
}

/**
 * amdgpu_device_recover_vram - Recover some VRAM contents
 *
 * @adev: amdgpu_device pointer
 *
 * Restores the contents of VRAM buffers from the shadows in GTT.  Used to
 * restore things like GPUVM page tables after a GPU reset where
 * the contents of VRAM might be lost.
 *
 * Returns:
 * 0 on success, negative error code on failure.
 */
static int amdgpu_device_recover_vram(struct amdgpu_device *adev)
{
	struct dma_fence *fence = NULL, *next = NULL;
	struct amdgpu_bo *shadow;
	long r = 1, tmo;

	if (amdgpu_sriov_runtime(adev))
		tmo = msecs_to_jiffies(8000);
	else
		tmo = msecs_to_jiffies(100);

	DRM_INFO("recover vram bo from shadow start\n");
	mutex_lock(&adev->shadow_list_lock);
	list_for_each_entry(shadow, &adev->shadow_list, shadow_list) {

		/* No need to recover an evicted BO */
		if (shadow->tbo.mem.mem_type != TTM_PL_TT ||
		    shadow->parent->tbo.mem.mem_type != TTM_PL_VRAM)
			continue;

		r = amdgpu_bo_restore_shadow(shadow, &next);
		if (r)
			break;

		if (fence) {
			r = dma_fence_wait_timeout(fence, false, tmo);
			dma_fence_put(fence);
			fence = next;
			if (r <= 0)
				break;
		} else {
			fence = next;
		}
	}
	mutex_unlock(&adev->shadow_list_lock);

	if (fence)
		tmo = dma_fence_wait_timeout(fence, false, tmo);
	dma_fence_put(fence);

	if (r <= 0 || tmo <= 0) {
		DRM_ERROR("recover vram bo from shadow failed\n");
		return -EIO;
	}

	DRM_INFO("recover vram bo from shadow done\n");
	return 0;
}


/**
 * amdgpu_device_reset_sriov - reset ASIC for SR-IOV vf
 *
 * @adev: amdgpu device pointer
 * @from_hypervisor: request from hypervisor
 *
 * do VF FLR and reinitialize Asic
 * return 0 means succeeded otherwise failed
 */
static int amdgpu_device_reset_sriov(struct amdgpu_device *adev,
				     bool from_hypervisor)
{
	int r;

	if (from_hypervisor)
		r = amdgpu_virt_request_full_gpu(adev, true);
	else
		r = amdgpu_virt_reset_gpu(adev);
	if (r)
		return r;

	/* Resume IP prior to SMC */
	r = amdgpu_device_ip_reinit_early_sriov(adev);
	if (r)
		goto error;

	/* we need recover gart prior to run SMC/CP/SDMA resume */
	amdgpu_gtt_mgr_recover(&adev->mman.bdev.man[TTM_PL_TT]);

	r = amdgpu_device_fw_loading(adev);
	if (r)
		return r;

	/* now we are okay to resume SMC/CP/SDMA */
	r = amdgpu_device_ip_reinit_late_sriov(adev);
	if (r)
		goto error;

	amdgpu_irq_gpu_reset_resume_helper(adev);
	r = amdgpu_ib_ring_tests(adev);

error:
	amdgpu_virt_release_full_gpu(adev, true);
	if (!r && adev->virt.gim_feature & AMDGIM_FEATURE_GIM_FLR_VRAMLOST) {
		atomic_inc(&adev->vram_lost_counter);
		r = amdgpu_device_recover_vram(adev);
	}

	return r;
}

/**
 * amdgpu_device_should_recover_gpu - check if we should try GPU recovery
 *
 * @adev: amdgpu device pointer
 *
 * Check amdgpu_gpu_recovery and SRIOV status to see if we should try to recover
 * a hung GPU.
 */
bool amdgpu_device_should_recover_gpu(struct amdgpu_device *adev)
{
	if (!amdgpu_device_ip_check_soft_reset(adev)) {
		DRM_INFO("Timeout, but no hardware hang detected.\n");
		return false;
	}

	if (amdgpu_gpu_recovery == 0)
		goto disabled;

	if (amdgpu_sriov_vf(adev))
		return true;

	if (amdgpu_gpu_recovery == -1) {
		switch (adev->asic_type) {
		case CHIP_BONAIRE:
		case CHIP_HAWAII:
		case CHIP_TOPAZ:
		case CHIP_TONGA:
		case CHIP_FIJI:
		case CHIP_POLARIS10:
		case CHIP_POLARIS11:
		case CHIP_POLARIS12:
		case CHIP_VEGAM:
		case CHIP_VEGA20:
		case CHIP_VEGA10:
		case CHIP_VEGA12:
			break;
		default:
			goto disabled;
		}
	}

	return true;

disabled:
		DRM_INFO("GPU recovery disabled.\n");
		return false;
}


static int amdgpu_device_pre_asic_reset(struct amdgpu_device *adev,
					struct amdgpu_job *job,
					bool *need_full_reset_arg)
{
	int i, r = 0;
	bool need_full_reset  = *need_full_reset_arg;

	/* block all schedulers and reset given job's ring */
	for (i = 0; i < AMDGPU_MAX_RINGS; ++i) {
		struct amdgpu_ring *ring = adev->rings[i];

		if (!ring || !ring->sched.thread)
			continue;

		kthread_park(ring->sched.thread);

		if (job && job->base.sched != &ring->sched)
			continue;

		drm_sched_hw_job_reset(&ring->sched, job ? &job->base : NULL);

		/* after all hw jobs are reset, hw fence is meaningless, so force_completion */
		amdgpu_fence_driver_force_completion(ring);
	}



	if (!amdgpu_sriov_vf(adev)) {

		if (!need_full_reset)
			need_full_reset = amdgpu_device_ip_need_full_reset(adev);

		if (!need_full_reset) {
			amdgpu_device_ip_pre_soft_reset(adev);
			r = amdgpu_device_ip_soft_reset(adev);
			amdgpu_device_ip_post_soft_reset(adev);
			if (r || amdgpu_device_ip_check_soft_reset(adev)) {
				DRM_INFO("soft reset failed, will fallback to full reset!\n");
				need_full_reset = true;
			}
		}

		if (need_full_reset)
			r = amdgpu_device_ip_suspend(adev);

		*need_full_reset_arg = need_full_reset;
	}

	return r;
}

static int amdgpu_do_asic_reset(struct amdgpu_hive_info *hive,
			       struct list_head *device_list_handle,
			       bool *need_full_reset_arg)
{
	struct amdgpu_device *tmp_adev = NULL;
	bool need_full_reset = *need_full_reset_arg, vram_lost = false;
	int r = 0;

	/*
	 * ASIC reset has to be done on all HGMI hive nodes ASAP
	 * to allow proper links negotiation in FW (within 1 sec)
	 */
	if (need_full_reset) {
		list_for_each_entry(tmp_adev, device_list_handle, gmc.xgmi.head) {
			/* For XGMI run all resets in parallel to speed up the process */
			if (tmp_adev->gmc.xgmi.num_physical_nodes > 1) {
				if (!queue_work(system_highpri_wq, &tmp_adev->xgmi_reset_work))
					r = -EALREADY;
			} else
				r = amdgpu_asic_reset(tmp_adev);

			if (r) {
				DRM_ERROR("ASIC reset failed with err r, %d for drm dev, %s",
					 r, tmp_adev->ddev->unique);
				break;
			}
		}

		/* For XGMI wait for all PSP resets to complete before proceed */
		if (!r) {
			list_for_each_entry(tmp_adev, device_list_handle,
					    gmc.xgmi.head) {
				if (tmp_adev->gmc.xgmi.num_physical_nodes > 1) {
					flush_work(&tmp_adev->xgmi_reset_work);
					r = tmp_adev->asic_reset_res;
					if (r)
						break;
				}
			}
		}
	}


	list_for_each_entry(tmp_adev, device_list_handle, gmc.xgmi.head) {
		if (need_full_reset) {
			/* post card */
			if (amdgpu_atom_asic_init(tmp_adev->mode_info.atom_context))
				DRM_WARN("asic atom init failed!");

			if (!r) {
				dev_info(tmp_adev->dev, "GPU reset succeeded, trying to resume\n");
				r = amdgpu_device_ip_resume_phase1(tmp_adev);
				if (r)
					goto out;

				vram_lost = amdgpu_device_check_vram_lost(tmp_adev);
				if (vram_lost) {
					DRM_ERROR("VRAM is lost!\n");
					atomic_inc(&tmp_adev->vram_lost_counter);
				}

				r = amdgpu_gtt_mgr_recover(
					&tmp_adev->mman.bdev.man[TTM_PL_TT]);
				if (r)
					goto out;

				r = amdgpu_device_fw_loading(tmp_adev);
				if (r)
					return r;

				r = amdgpu_device_ip_resume_phase2(tmp_adev);
				if (r)
					goto out;

				if (vram_lost)
					amdgpu_device_fill_reset_magic(tmp_adev);

				/* Update PSP FW topology after reset */
				if (hive && tmp_adev->gmc.xgmi.num_physical_nodes > 1)
					r = amdgpu_xgmi_update_topology(hive, tmp_adev);
			}
		}


out:
		if (!r) {
			amdgpu_irq_gpu_reset_resume_helper(tmp_adev);
			r = amdgpu_ib_ring_tests(tmp_adev);
			if (r) {
				dev_err(tmp_adev->dev, "ib ring test failed (%d).\n", r);
				r = amdgpu_device_ip_suspend(tmp_adev);
				need_full_reset = true;
				r = -EAGAIN;
				goto end;
			}
		}

		if (!r)
			r = amdgpu_device_recover_vram(tmp_adev);
		else
			tmp_adev->asic_reset_res = r;
	}

end:
	*need_full_reset_arg = need_full_reset;
	return r;
}

static void amdgpu_device_post_asic_reset(struct amdgpu_device *adev,
					  struct amdgpu_job *job)
{
	int i;

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i) {
		struct amdgpu_ring *ring = adev->rings[i];

		if (!ring || !ring->sched.thread)
			continue;

		/* only need recovery sched of the given job's ring
		 * or all rings (in the case @job is NULL)
		 * after above amdgpu_reset accomplished
		 */
		if ((!job || job->base.sched == &ring->sched) && !adev->asic_reset_res)
			drm_sched_job_recovery(&ring->sched);

		kthread_unpark(ring->sched.thread);
	}

	if (!amdgpu_device_has_dc_support(adev)) {
		drm_helper_resume_force_mode(adev->ddev);
	}

	adev->asic_reset_res = 0;
}

static void amdgpu_device_lock_adev(struct amdgpu_device *adev)
{
	mutex_lock(&adev->lock_reset);
	atomic_inc(&adev->gpu_reset_counter);
	adev->in_gpu_reset = 1;
	/* Block kfd: SRIOV would do it separately */
	if (!amdgpu_sriov_vf(adev))
                amdgpu_amdkfd_pre_reset(adev);
}

static void amdgpu_device_unlock_adev(struct amdgpu_device *adev)
{
	/*unlock kfd: SRIOV would do it separately */
	if (!amdgpu_sriov_vf(adev))
                amdgpu_amdkfd_post_reset(adev);
	amdgpu_vf_error_trans_all(adev);
	adev->in_gpu_reset = 0;
	mutex_unlock(&adev->lock_reset);
}


/**
 * amdgpu_device_gpu_recover - reset the asic and recover scheduler
 *
 * @adev: amdgpu device pointer
 * @job: which job trigger hang
 *
 * Attempt to reset the GPU if it has hung (all asics).
 * Attempt to do soft-reset or full-reset and reinitialize Asic
 * Returns 0 for success or an error on failure.
 */

int amdgpu_device_gpu_recover(struct amdgpu_device *adev,
			      struct amdgpu_job *job)
{
	int r;
	struct amdgpu_hive_info *hive = NULL;
	bool need_full_reset = false;
	struct amdgpu_device *tmp_adev = NULL;
	struct list_head device_list, *device_list_handle =  NULL;

	INIT_LIST_HEAD(&device_list);

	dev_info(adev->dev, "GPU reset begin!\n");

	/*
	 * In case of XGMI hive disallow concurrent resets to be triggered
	 * by different nodes. No point also since the one node already executing
	 * reset will also reset all the other nodes in the hive.
	 */
	hive = amdgpu_get_xgmi_hive(adev);
	if (hive && adev->gmc.xgmi.num_physical_nodes > 1 &&
	    !mutex_trylock(&hive->hive_lock))
		return 0;

	/* Start with adev pre asic reset first for soft reset check.*/
	amdgpu_device_lock_adev(adev);
	r = amdgpu_device_pre_asic_reset(adev,
					 job,
					 &need_full_reset);
	if (r) {
		/*TODO Should we stop ?*/
		DRM_ERROR("GPU pre asic reset failed with err, %d for drm dev, %s ",
			  r, adev->ddev->unique);
		adev->asic_reset_res = r;
	}

	/* Build list of devices to reset */
	if  (need_full_reset && adev->gmc.xgmi.num_physical_nodes > 1) {
		if (!hive) {
			amdgpu_device_unlock_adev(adev);
			return -ENODEV;
		}

		/*
		 * In case we are in XGMI hive mode device reset is done for all the
		 * nodes in the hive to retrain all XGMI links and hence the reset
		 * sequence is executed in loop on all nodes.
		 */
		device_list_handle = &hive->device_list;
	} else {
		list_add_tail(&adev->gmc.xgmi.head, &device_list);
		device_list_handle = &device_list;
	}

retry:	/* Rest of adevs pre asic reset from XGMI hive. */
	list_for_each_entry(tmp_adev, device_list_handle, gmc.xgmi.head) {

		if (tmp_adev == adev)
			continue;

		amdgpu_device_lock_adev(tmp_adev);
		r = amdgpu_device_pre_asic_reset(tmp_adev,
						 NULL,
						 &need_full_reset);
		/*TODO Should we stop ?*/
		if (r) {
			DRM_ERROR("GPU pre asic reset failed with err, %d for drm dev, %s ",
				  r, tmp_adev->ddev->unique);
			tmp_adev->asic_reset_res = r;
		}
	}

	/* Actual ASIC resets if needed.*/
	/* TODO Implement XGMI hive reset logic for SRIOV */
	if (amdgpu_sriov_vf(adev)) {
		r = amdgpu_device_reset_sriov(adev, job ? false : true);
		if (r)
			adev->asic_reset_res = r;
	} else {
		r  = amdgpu_do_asic_reset(hive, device_list_handle, &need_full_reset);
		if (r && r == -EAGAIN)
			goto retry;
	}

	/* Post ASIC reset for all devs .*/
	list_for_each_entry(tmp_adev, device_list_handle, gmc.xgmi.head) {
		amdgpu_device_post_asic_reset(tmp_adev, tmp_adev == adev ? job : NULL);

		if (r) {
			/* bad news, how to tell it to userspace ? */
			dev_info(tmp_adev->dev, "GPU reset(%d) failed\n", atomic_read(&adev->gpu_reset_counter));
			amdgpu_vf_error_put(tmp_adev, AMDGIM_ERROR_VF_GPU_RESET_FAIL, 0, r);
		} else {
			dev_info(tmp_adev->dev, "GPU reset(%d) succeeded!\n", atomic_read(&adev->gpu_reset_counter));
		}

		amdgpu_device_unlock_adev(tmp_adev);
	}

	if (hive && adev->gmc.xgmi.num_physical_nodes > 1)
		mutex_unlock(&hive->hive_lock);

	if (r)
		dev_info(adev->dev, "GPU reset end with ret = %d\n", r);
	return r;
}

/**
 * amdgpu_device_get_pcie_info - fence pcie info about the PCIE slot
 *
 * @adev: amdgpu_device pointer
 *
 * Fetchs and stores in the driver the PCIE capabilities (gen speed
 * and lanes) of the slot the device is in. Handles APUs and
 * virtualized environments where PCIE config space may not be available.
 */
static void amdgpu_device_get_pcie_info(struct amdgpu_device *adev)
{
	struct pci_dev *pdev;
	enum pci_bus_speed speed_cap;
	enum pcie_link_width link_width;

	if (amdgpu_pcie_gen_cap)
		adev->pm.pcie_gen_mask = amdgpu_pcie_gen_cap;

	if (amdgpu_pcie_lane_cap)
		adev->pm.pcie_mlw_mask = amdgpu_pcie_lane_cap;

	/* covers APUs as well */
	if (pci_is_root_bus(adev->pdev->bus)) {
		if (adev->pm.pcie_gen_mask == 0)
			adev->pm.pcie_gen_mask = AMDGPU_DEFAULT_PCIE_GEN_MASK;
		if (adev->pm.pcie_mlw_mask == 0)
			adev->pm.pcie_mlw_mask = AMDGPU_DEFAULT_PCIE_MLW_MASK;
		return;
	}

	if (adev->pm.pcie_gen_mask == 0) {
		/* asic caps */
		pdev = adev->pdev;
		speed_cap = pcie_get_speed_cap(pdev);
		if (speed_cap == PCI_SPEED_UNKNOWN) {
			adev->pm.pcie_gen_mask |= (CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN1 |
						  CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN2 |
						  CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN3);
		} else {
			if (speed_cap == PCIE_SPEED_16_0GT)
				adev->pm.pcie_gen_mask |= (CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN1 |
							  CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN2 |
							  CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN3 |
							  CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN4);
			else if (speed_cap == PCIE_SPEED_8_0GT)
				adev->pm.pcie_gen_mask |= (CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN1 |
							  CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN2 |
							  CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN3);
			else if (speed_cap == PCIE_SPEED_5_0GT)
				adev->pm.pcie_gen_mask |= (CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN1 |
							  CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN2);
			else
				adev->pm.pcie_gen_mask |= CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN1;
		}
		/* platform caps */
		pdev = adev->ddev->pdev->bus->self;
		speed_cap = pcie_get_speed_cap(pdev);
		if (speed_cap == PCI_SPEED_UNKNOWN) {
			adev->pm.pcie_gen_mask |= (CAIL_PCIE_LINK_SPEED_SUPPORT_GEN1 |
						   CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2);
		} else {
			if (speed_cap == PCIE_SPEED_16_0GT)
				adev->pm.pcie_gen_mask |= (CAIL_PCIE_LINK_SPEED_SUPPORT_GEN1 |
							   CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2 |
							   CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3 |
							   CAIL_PCIE_LINK_SPEED_SUPPORT_GEN4);
			else if (speed_cap == PCIE_SPEED_8_0GT)
				adev->pm.pcie_gen_mask |= (CAIL_PCIE_LINK_SPEED_SUPPORT_GEN1 |
							   CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2 |
							   CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3);
			else if (speed_cap == PCIE_SPEED_5_0GT)
				adev->pm.pcie_gen_mask |= (CAIL_PCIE_LINK_SPEED_SUPPORT_GEN1 |
							   CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2);
			else
				adev->pm.pcie_gen_mask |= CAIL_PCIE_LINK_SPEED_SUPPORT_GEN1;

		}
	}
	if (adev->pm.pcie_mlw_mask == 0) {
		pdev = adev->ddev->pdev->bus->self;
		link_width = pcie_get_width_cap(pdev);
		if (link_width == PCIE_LNK_WIDTH_UNKNOWN) {
			adev->pm.pcie_mlw_mask |= AMDGPU_DEFAULT_PCIE_MLW_MASK;
		} else {
			switch (link_width) {
			case PCIE_LNK_X32:
				adev->pm.pcie_mlw_mask = (CAIL_PCIE_LINK_WIDTH_SUPPORT_X32 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X16 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X12 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X8 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X4 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X2 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X1);
				break;
			case PCIE_LNK_X16:
				adev->pm.pcie_mlw_mask = (CAIL_PCIE_LINK_WIDTH_SUPPORT_X16 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X12 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X8 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X4 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X2 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X1);
				break;
			case PCIE_LNK_X12:
				adev->pm.pcie_mlw_mask = (CAIL_PCIE_LINK_WIDTH_SUPPORT_X12 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X8 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X4 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X2 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X1);
				break;
			case PCIE_LNK_X8:
				adev->pm.pcie_mlw_mask = (CAIL_PCIE_LINK_WIDTH_SUPPORT_X8 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X4 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X2 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X1);
				break;
			case PCIE_LNK_X4:
				adev->pm.pcie_mlw_mask = (CAIL_PCIE_LINK_WIDTH_SUPPORT_X4 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X2 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X1);
				break;
			case PCIE_LNK_X2:
				adev->pm.pcie_mlw_mask = (CAIL_PCIE_LINK_WIDTH_SUPPORT_X2 |
							  CAIL_PCIE_LINK_WIDTH_SUPPORT_X1);
				break;
			case PCIE_LNK_X1:
				adev->pm.pcie_mlw_mask = CAIL_PCIE_LINK_WIDTH_SUPPORT_X1;
				break;
			default:
				break;
			}
		}
	}
}

