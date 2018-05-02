/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Eddie Dong <eddie.dong@intel.com>
 *    Jike Song <jike.song@intel.com>
 *
 * Contributors:
 *    Zhi Wang <zhi.a.wang@intel.com>
 *    Min He <min.he@intel.com>
 *    Bing Niu <bing.niu@intel.com>
 *
 */

#include "i915_drv.h"
#include "gvt.h"

enum {
	INTEL_GVT_PCI_BAR_GTTMMIO = 0,
	INTEL_GVT_PCI_BAR_APERTURE,
	INTEL_GVT_PCI_BAR_PIO,
	INTEL_GVT_PCI_BAR_MAX,
};

/* bitmap for writable bits (RW or RW1C bits, but cannot co-exist in one
 * byte) byte by byte in standard pci configuration space. (not the full
 * 256 bytes.)
 */
static const u8 pci_cfg_space_rw_bmp[PCI_INTERRUPT_LINE + 4] = {
	[PCI_COMMAND]		= 0xff, 0x07,
	[PCI_STATUS]		= 0x00, 0xf9, /* the only one RW1C byte */
	[PCI_CACHE_LINE_SIZE]	= 0xff,
	[PCI_BASE_ADDRESS_0 ... PCI_CARDBUS_CIS - 1] = 0xff,
	[PCI_ROM_ADDRESS]	= 0x01, 0xf8, 0xff, 0xff,
	[PCI_INTERRUPT_LINE]	= 0xff,
};

/**
 * vgpu_pci_cfg_mem_write - write virtual cfg space memory
 *
 * Use this function to write virtual cfg space memory.
 * For standard cfg space, only RW bits can be changed,
 * and we emulates the RW1C behavior of PCI_STATUS register.
 */
static void vgpu_pci_cfg_mem_write(struct intel_vgpu *vgpu, unsigned int off,
				   u8 *src, unsigned int bytes)
{
	u8 *cfg_base = vgpu_cfg_space(vgpu);
	u8 mask, new, old;
	int i = 0;

	for (; i < bytes && (off + i < sizeof(pci_cfg_space_rw_bmp)); i++) {
		mask = pci_cfg_space_rw_bmp[off + i];
		old = cfg_base[off + i];
		new = src[i] & mask;

		/**
		 * The PCI_STATUS high byte has RW1C bits, here
		 * emulates clear by writing 1 for these bits.
		 * Writing a 0b to RW1C bits has no effect.
		 */
		if (off + i == PCI_STATUS + 1)
			new = (~new & old) & mask;

		cfg_base[off + i] = (old & ~mask) | new;
	}

	/* For other configuration space directly copy as it is. */
	if (i < bytes)
		memcpy(cfg_base + off + i, src + i, bytes - i);
}

/**
 * intel_vgpu_emulate_cfg_read - emulate vGPU configuration space read
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_vgpu_emulate_cfg_read(struct intel_vgpu *vgpu, unsigned int offset,
	void *p_data, unsigned int bytes)
{
	if (WARN_ON(bytes > 4))
		return -EINVAL;

	if (WARN_ON(offset + bytes > vgpu->gvt->device_info.cfg_space_size))
		return -EINVAL;

	memcpy(p_data, vgpu_cfg_space(vgpu) + offset, bytes);
	return 0;
}

static int map_aperture(struct intel_vgpu *vgpu, bool map)
{
	phys_addr_t aperture_pa = vgpu_aperture_pa_base(vgpu);
	unsigned long aperture_sz = vgpu_aperture_sz(vgpu);
	u64 first_gfn;
	u64 val;
	int ret;

	if (map == vgpu->cfg_space.bar[INTEL_GVT_PCI_BAR_APERTURE].tracked)
		return 0;

	val = vgpu_cfg_space(vgpu)[PCI_BASE_ADDRESS_2];
	if (val & PCI_BASE_ADDRESS_MEM_TYPE_64)
		val = *(u64 *)(vgpu_cfg_space(vgpu) + PCI_BASE_ADDRESS_2);
	else
		val = *(u32 *)(vgpu_cfg_space(vgpu) + PCI_BASE_ADDRESS_2);

	first_gfn = (val + vgpu_aperture_offset(vgpu)) >> PAGE_SHIFT;

	ret = intel_gvt_hypervisor_map_gfn_to_mfn(vgpu, first_gfn,
						  aperture_pa >> PAGE_SHIFT,
						  aperture_sz >> PAGE_SHIFT,
						  map);
	if (ret)
		return ret;

	vgpu->cfg_space.bar[INTEL_GVT_PCI_BAR_APERTURE].tracked = map;
	return 0;
}

static int trap_gttmmio(struct intel_vgpu *vgpu, bool trap)
{
	u64 start, end;
	u64 val;
	int ret;

	if (trap == vgpu->cfg_space.bar[INTEL_GVT_PCI_BAR_GTTMMIO].tracked)
		return 0;

	val = vgpu_cfg_space(vgpu)[PCI_BASE_ADDRESS_0];
	if (val & PCI_BASE_ADDRESS_MEM_TYPE_64)
		start = *(u64 *)(vgpu_cfg_space(vgpu) + PCI_BASE_ADDRESS_0);
	else
		start = *(u32 *)(vgpu_cfg_space(vgpu) + PCI_BASE_ADDRESS_0);

	start &= ~GENMASK(3, 0);
	end = start + vgpu->cfg_space.bar[INTEL_GVT_PCI_BAR_GTTMMIO].size - 1;

	ret = intel_gvt_hypervisor_set_trap_area(vgpu, start, end, trap);
	if (ret)
		return ret;

	vgpu->cfg_space.bar[INTEL_GVT_PCI_BAR_GTTMMIO].tracked = trap;
	return 0;
}

static int emulate_pci_command_write(struct intel_vgpu *vgpu,
	unsigned int offset, void *p_data, unsigned int bytes)
{
	u8 old = vgpu_cfg_space(vgpu)[offset];
	u8 new = *(u8 *)p_data;
	u8 changed = old ^ new;
	int ret;

	vgpu_pci_cfg_mem_write(vgpu, offset, p_data, bytes);
	if (!(changed & PCI_COMMAND_MEMORY))
		return 0;

	if (old & PCI_COMMAND_MEMORY) {
		ret = trap_gttmmio(vgpu, false);
		if (ret)
			return ret;
		ret = map_aperture(vgpu, false);
		if (ret)
			return ret;
	} else {
		ret = trap_gttmmio(vgpu, true);
		if (ret)
			return ret;
		ret = map_aperture(vgpu, true);
		if (ret)
			return ret;
	}

	return 0;
}

static int emulate_pci_rom_bar_write(struct intel_vgpu *vgpu,
	unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 *pval = (u32 *)(vgpu_cfg_space(vgpu) + offset);
	u32 new = *(u32 *)(p_data);

	if ((new & PCI_ROM_ADDRESS_MASK) == PCI_ROM_ADDRESS_MASK)
		/* We don't have rom, return size of 0. */
		*pval = 0;
	else
		vgpu_pci_cfg_mem_write(vgpu, offset, p_data, bytes);
	return 0;
}

static int emulate_pci_bar_write(struct intel_vgpu *vgpu, unsigned int offset,
	void *p_data, unsigned int bytes)
{
	u32 new = *(u32 *)(p_data);
	bool lo = IS_ALIGNED(offset, 8);
	u64 size;
	int ret = 0;
	bool mmio_enabled =
		vgpu_cfg_space(vgpu)[PCI_COMMAND] & PCI_COMMAND_MEMORY;
	struct intel_vgpu_pci_bar *bars = vgpu->cfg_space.bar;

	/*
	 * Power-up software can determine how much address
	 * space the device requires by writing a value of
	 * all 1's to the register and then reading the value
	 * back. The device will return 0's in all don't-care
	 * address bits.
	 */
	if (new == 0xffffffff) {
		switch (offset) {
		case PCI_BASE_ADDRESS_0:
		case PCI_BASE_ADDRESS_1:
			size = ~(bars[INTEL_GVT_PCI_BAR_GTTMMIO].size -1);
			intel_vgpu_write_pci_bar(vgpu, offset,
						size >> (lo ? 0 : 32), lo);
			/*
			 * Untrap the BAR, since guest hasn't configured a
			 * valid GPA
			 */
			ret = trap_gttmmio(vgpu, false);
			break;
		case PCI_BASE_ADDRESS_2:
		case PCI_BASE_ADDRESS_3:
			size = ~(bars[INTEL_GVT_PCI_BAR_APERTURE].size -1);
			intel_vgpu_write_pci_bar(vgpu, offset,
						size >> (lo ? 0 : 32), lo);
			ret = map_aperture(vgpu, false);
			break;
		default:
			/* Unimplemented BARs */
			intel_vgpu_write_pci_bar(vgpu, offset, 0x0, false);
		}
	} else {
		switch (offset) {
		case PCI_BASE_ADDRESS_0:
		case PCI_BASE_ADDRESS_1:
			/*
			 * Untrap the old BAR first, since guest has
			 * re-configured the BAR
			 */
			trap_gttmmio(vgpu, false);
			intel_vgpu_write_pci_bar(vgpu, offset, new, lo);
			ret = trap_gttmmio(vgpu, mmio_enabled);
			break;
		case PCI_BASE_ADDRESS_2:
		case PCI_BASE_ADDRESS_3:
			map_aperture(vgpu, false);
			intel_vgpu_write_pci_bar(vgpu, offset, new, lo);
			ret = map_aperture(vgpu, mmio_enabled);
			break;
		default:
			intel_vgpu_write_pci_bar(vgpu, offset, new, lo);
		}
	}
	return ret;
}

/**
 * intel_vgpu_emulate_cfg_read - emulate vGPU configuration space write
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_vgpu_emulate_cfg_write(struct intel_vgpu *vgpu, unsigned int offset,
	void *p_data, unsigned int bytes)
{
	int ret;

	if (WARN_ON(bytes > 4))
		return -EINVAL;

	if (WARN_ON(offset + bytes > vgpu->gvt->device_info.cfg_space_size))
		return -EINVAL;

	/* First check if it's PCI_COMMAND */
	if (IS_ALIGNED(offset, 2) && offset == PCI_COMMAND) {
		if (WARN_ON(bytes > 2))
			return -EINVAL;
		return emulate_pci_command_write(vgpu, offset, p_data, bytes);
	}

	switch (rounddown(offset, 4)) {
	case PCI_ROM_ADDRESS:
		if (WARN_ON(!IS_ALIGNED(offset, 4)))
			return -EINVAL;
		return emulate_pci_rom_bar_write(vgpu, offset, p_data, bytes);

	case PCI_BASE_ADDRESS_0 ... PCI_BASE_ADDRESS_5:
		if (WARN_ON(!IS_ALIGNED(offset, 4)))
			return -EINVAL;
		return emulate_pci_bar_write(vgpu, offset, p_data, bytes);

	case INTEL_GVT_PCI_SWSCI:
		if (WARN_ON(!IS_ALIGNED(offset, 4)))
			return -EINVAL;
		ret = intel_vgpu_emulate_opregion_request(vgpu, *(u32 *)p_data);
		if (ret)
			return ret;
		break;

	case INTEL_GVT_PCI_OPREGION:
		if (WARN_ON(!IS_ALIGNED(offset, 4)))
			return -EINVAL;
		ret = intel_vgpu_opregion_base_write_handler(vgpu,
						   *(u32 *)p_data);
		if (ret)
			return ret;

		vgpu_pci_cfg_mem_write(vgpu, offset, p_data, bytes);
		break;
	default:
		vgpu_pci_cfg_mem_write(vgpu, offset, p_data, bytes);
		break;
	}
	return 0;
}

/**
 * intel_vgpu_init_cfg_space - init vGPU configuration space when create vGPU
 *
 * @vgpu: a vGPU
 * @primary: is the vGPU presented as primary
 *
 */
void intel_vgpu_init_cfg_space(struct intel_vgpu *vgpu,
			       bool primary)
{
	struct intel_gvt *gvt = vgpu->gvt;
	const struct intel_gvt_device_info *info = &gvt->device_info;
	u16 *gmch_ctl;

	memcpy(vgpu_cfg_space(vgpu), gvt->firmware.cfg_space,
	       info->cfg_space_size);

	if (!primary) {
		vgpu_cfg_space(vgpu)[PCI_CLASS_DEVICE] =
			INTEL_GVT_PCI_CLASS_VGA_OTHER;
		vgpu_cfg_space(vgpu)[PCI_CLASS_PROG] =
			INTEL_GVT_PCI_CLASS_VGA_OTHER;
	}

	/* Show guest that there isn't any stolen memory.*/
	gmch_ctl = (u16 *)(vgpu_cfg_space(vgpu) + INTEL_GVT_PCI_GMCH_CONTROL);
	*gmch_ctl &= ~(BDW_GMCH_GMS_MASK << BDW_GMCH_GMS_SHIFT);

	intel_vgpu_write_pci_bar(vgpu, PCI_BASE_ADDRESS_2,
				 gvt_aperture_pa_base(gvt), true);

	vgpu_cfg_space(vgpu)[PCI_COMMAND] &= ~(PCI_COMMAND_IO
					     | PCI_COMMAND_MEMORY
					     | PCI_COMMAND_MASTER);
	/*
	 * Clear the bar upper 32bit and let guest to assign the new value
	 */
	memset(vgpu_cfg_space(vgpu) + PCI_BASE_ADDRESS_1, 0, 4);
	memset(vgpu_cfg_space(vgpu) + PCI_BASE_ADDRESS_3, 0, 4);
	memset(vgpu_cfg_space(vgpu) + PCI_BASE_ADDRESS_4, 0, 8);
	memset(vgpu_cfg_space(vgpu) + INTEL_GVT_PCI_OPREGION, 0, 4);

	vgpu->cfg_space.bar[INTEL_GVT_PCI_BAR_GTTMMIO].size =
				pci_resource_len(gvt->dev_priv->drm.pdev, 0);
	vgpu->cfg_space.bar[INTEL_GVT_PCI_BAR_APERTURE].size =
				pci_resource_len(gvt->dev_priv->drm.pdev, 2);

	memset(vgpu_cfg_space(vgpu) + PCI_ROM_ADDRESS, 0, 4);
}

/**
 * intel_vgpu_reset_cfg_space - reset vGPU configuration space
 *
 * @vgpu: a vGPU
 *
 */
void intel_vgpu_reset_cfg_space(struct intel_vgpu *vgpu)
{
	u8 cmd = vgpu_cfg_space(vgpu)[PCI_COMMAND];
	bool primary = vgpu_cfg_space(vgpu)[PCI_CLASS_DEVICE] !=
				INTEL_GVT_PCI_CLASS_VGA_OTHER;

	if (cmd & PCI_COMMAND_MEMORY) {
		trap_gttmmio(vgpu, false);
		map_aperture(vgpu, false);
	}

	/**
	 * Currently we only do such reset when vGPU is not
	 * owned by any VM, so we simply restore entire cfg
	 * space to default value.
	 */
	intel_vgpu_init_cfg_space(vgpu, primary);
}
