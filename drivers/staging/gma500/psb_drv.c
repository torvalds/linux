/**************************************************************************
 * Copyright (c) 2007, Intel Corporation.
 * All Rights Reserved.
 * Copyright (c) 2008, Tungsten Graphics, Inc. Cedar Park, TX., USA.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/

#include <drm/drmP.h>
#include <drm/drm.h>
#include "psb_drm.h"
#include "psb_drv.h"
#include "psb_fb.h"
#include "psb_reg.h"
#include "psb_intel_reg.h"
#include "psb_intel_bios.h"
#include <drm/drm_pciids.h>
#include "psb_powermgmt.h"
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/pm_runtime.h>
#include <acpi/video.h>

int drm_psb_debug;
static int drm_psb_trap_pagefaults;

int drm_psb_no_fb;

static int psb_probe(struct pci_dev *pdev, const struct pci_device_id *ent);

MODULE_PARM_DESC(debug, "Enable debug output");
MODULE_PARM_DESC(no_fb, "Disable FBdev");
MODULE_PARM_DESC(trap_pagefaults, "Error and reset on MMU pagefaults");
module_param_named(debug, drm_psb_debug, int, 0600);
module_param_named(no_fb, drm_psb_no_fb, int, 0600);
module_param_named(trap_pagefaults, drm_psb_trap_pagefaults, int, 0600);


static struct pci_device_id pciidlist[] = {
	{ 0x8086, 0x8108, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PSB_8108 },
	{ 0x8086, 0x8109, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PSB_8109 },
	{ 0x8086, 0x4100, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MRST_4100},
	{ 0x8086, 0x4101, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MRST_4100},
	{ 0x8086, 0x4102, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MRST_4100},
	{ 0x8086, 0x4103, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MRST_4100},
	{ 0x8086, 0x4104, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MRST_4100},
	{ 0x8086, 0x4105, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MRST_4100},
	{ 0x8086, 0x4106, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MRST_4100},
	{ 0x8086, 0x4107, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MRST_4100},
	{ 0, 0, 0}
};
MODULE_DEVICE_TABLE(pci, pciidlist);

/*
 * Standard IOCTLs.
 */

#define DRM_IOCTL_PSB_KMS_OFF	\
		DRM_IO(DRM_PSB_KMS_OFF + DRM_COMMAND_BASE)
#define DRM_IOCTL_PSB_KMS_ON	\
		DRM_IO(DRM_PSB_KMS_ON + DRM_COMMAND_BASE)
#define DRM_IOCTL_PSB_SIZES	\
		DRM_IOR(DRM_PSB_SIZES + DRM_COMMAND_BASE, \
			struct drm_psb_sizes_arg)
#define DRM_IOCTL_PSB_FUSE_REG	\
		DRM_IOWR(DRM_PSB_FUSE_REG + DRM_COMMAND_BASE, uint32_t)
#define DRM_IOCTL_PSB_DC_STATE	\
		DRM_IOW(DRM_PSB_DC_STATE + DRM_COMMAND_BASE, \
			struct drm_psb_dc_state_arg)
#define DRM_IOCTL_PSB_ADB	\
		DRM_IOWR(DRM_PSB_ADB + DRM_COMMAND_BASE, uint32_t)
#define DRM_IOCTL_PSB_MODE_OPERATION	\
		DRM_IOWR(DRM_PSB_MODE_OPERATION + DRM_COMMAND_BASE, \
			 struct drm_psb_mode_operation_arg)
#define DRM_IOCTL_PSB_STOLEN_MEMORY	\
		DRM_IOWR(DRM_PSB_STOLEN_MEMORY + DRM_COMMAND_BASE, \
			 struct drm_psb_stolen_memory_arg)
#define DRM_IOCTL_PSB_REGISTER_RW	\
		DRM_IOWR(DRM_PSB_REGISTER_RW + DRM_COMMAND_BASE, \
			 struct drm_psb_register_rw_arg)
#define DRM_IOCTL_PSB_DPST	\
		DRM_IOWR(DRM_PSB_DPST + DRM_COMMAND_BASE, \
			 uint32_t)
#define DRM_IOCTL_PSB_GAMMA	\
		DRM_IOWR(DRM_PSB_GAMMA + DRM_COMMAND_BASE, \
			 struct drm_psb_dpst_lut_arg)
#define DRM_IOCTL_PSB_DPST_BL	\
		DRM_IOWR(DRM_PSB_DPST_BL + DRM_COMMAND_BASE, \
			 uint32_t)
#define DRM_IOCTL_PSB_GET_PIPE_FROM_CRTC_ID	\
		DRM_IOWR(DRM_PSB_GET_PIPE_FROM_CRTC_ID + DRM_COMMAND_BASE, \
			 struct drm_psb_get_pipe_from_crtc_id_arg)

static int psb_sizes_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
static int psb_dc_state_ioctl(struct drm_device *dev, void * data,
			      struct drm_file *file_priv);
static int psb_adb_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
static int psb_mode_operation_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
static int psb_stolen_memory_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file_priv);
static int psb_register_rw_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
static int psb_dpst_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
static int psb_gamma_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
static int psb_dpst_bl_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv);

#define PSB_IOCTL_DEF(ioctl, func, flags) \
	[DRM_IOCTL_NR(ioctl) - DRM_COMMAND_BASE] = {ioctl, flags, func}

static struct drm_ioctl_desc psb_ioctls[] = {
	PSB_IOCTL_DEF(DRM_IOCTL_PSB_KMS_OFF, psbfb_kms_off_ioctl,
		      DRM_ROOT_ONLY),
	PSB_IOCTL_DEF(DRM_IOCTL_PSB_KMS_ON,
			psbfb_kms_on_ioctl,
			DRM_ROOT_ONLY),
	PSB_IOCTL_DEF(DRM_IOCTL_PSB_SIZES, psb_sizes_ioctl, DRM_AUTH),
	PSB_IOCTL_DEF(DRM_IOCTL_PSB_DC_STATE, psb_dc_state_ioctl, DRM_AUTH),
	PSB_IOCTL_DEF(DRM_IOCTL_PSB_ADB, psb_adb_ioctl, DRM_AUTH),
	PSB_IOCTL_DEF(DRM_IOCTL_PSB_MODE_OPERATION, psb_mode_operation_ioctl,
		      DRM_AUTH),
	PSB_IOCTL_DEF(DRM_IOCTL_PSB_STOLEN_MEMORY, psb_stolen_memory_ioctl,
		      DRM_AUTH),
	PSB_IOCTL_DEF(DRM_IOCTL_PSB_REGISTER_RW, psb_register_rw_ioctl,
		      DRM_AUTH),
	PSB_IOCTL_DEF(DRM_IOCTL_PSB_DPST, psb_dpst_ioctl, DRM_AUTH),
	PSB_IOCTL_DEF(DRM_IOCTL_PSB_GAMMA, psb_gamma_ioctl, DRM_AUTH),
	PSB_IOCTL_DEF(DRM_IOCTL_PSB_DPST_BL, psb_dpst_bl_ioctl, DRM_AUTH),
	PSB_IOCTL_DEF(DRM_IOCTL_PSB_GET_PIPE_FROM_CRTC_ID,
					psb_intel_get_pipe_from_crtc_id, 0),

};

static void psb_lastclose(struct drm_device *dev)
{
	return;
}

static void psb_do_takedown(struct drm_device *dev)
{
	/* FIXME: do we need to clean up the gtt here ? */
}

void mrst_get_fuse_settings(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct pci_dev *pci_root = pci_get_bus_and_slot(0, 0);
	uint32_t fuse_value = 0;
	uint32_t fuse_value_tmp = 0;

#define FB_REG06 0xD0810600
#define FB_MIPI_DISABLE  (1 << 11)
#define FB_REG09 0xD0810900
#define FB_REG09 0xD0810900
#define FB_SKU_MASK  0x7000
#define FB_SKU_SHIFT 12
#define FB_SKU_100 0
#define FB_SKU_100L 1
#define FB_SKU_83 2
	pci_write_config_dword(pci_root, 0xD0, FB_REG06);
	pci_read_config_dword(pci_root, 0xD4, &fuse_value);

	dev_priv->iLVDS_enable = fuse_value & FB_MIPI_DISABLE;

	DRM_INFO("internal display is %s\n",
		 dev_priv->iLVDS_enable ? "LVDS display" : "MIPI display");

	 /*prevent Runtime suspend at start*/
	 if (dev_priv->iLVDS_enable) {
		dev_priv->is_lvds_on = true;
		dev_priv->is_mipi_on = false;
	}
	else {
		dev_priv->is_mipi_on = true;
		dev_priv->is_lvds_on = false;
	}

	dev_priv->video_device_fuse = fuse_value;

	pci_write_config_dword(pci_root, 0xD0, FB_REG09);
	pci_read_config_dword(pci_root, 0xD4, &fuse_value);

	DRM_INFO("SKU values is 0x%x. \n", fuse_value);
	fuse_value_tmp = (fuse_value & FB_SKU_MASK) >> FB_SKU_SHIFT;

	dev_priv->fuse_reg_value = fuse_value;

	switch (fuse_value_tmp) {
	case FB_SKU_100:
		dev_priv->core_freq = 200;
		break;
	case FB_SKU_100L:
		dev_priv->core_freq = 100;
		break;
	case FB_SKU_83:
		dev_priv->core_freq = 166;
		break;
	default:
		DRM_ERROR("Invalid SKU values, SKU value = 0x%08x\n", fuse_value_tmp);
		dev_priv->core_freq = 0;
	}
	DRM_INFO("LNC core clk is %dMHz.\n", dev_priv->core_freq);
	pci_dev_put(pci_root);
}

void mid_get_pci_revID (struct drm_psb_private *dev_priv)
{
	uint32_t platform_rev_id = 0;
	struct pci_dev *pci_gfx_root = pci_get_bus_and_slot(0, PCI_DEVFN(2, 0));

	/*get the revison ID, B0:D2:F0;0x08 */
	pci_read_config_dword(pci_gfx_root, 0x08, &platform_rev_id);
	dev_priv->platform_rev_id = (uint8_t) platform_rev_id;
	pci_dev_put(pci_gfx_root);
	PSB_DEBUG_ENTRY("platform_rev_id is %x\n",	dev_priv->platform_rev_id);
}

void mrst_get_vbt_data(struct drm_psb_private *dev_priv)
{
	struct mrst_vbt *vbt = &dev_priv->vbt_data;
	u32 platform_config_address;
	u16 new_size;
	u8 *vbt_virtual;
	u8 bpi;
	u8 number_desc = 0;
	struct mrst_timing_info *dp_ti = &dev_priv->gct_data.DTD;
	struct gct_r10_timing_info ti;
	void *pGCT;
	struct pci_dev *pci_gfx_root = pci_get_bus_and_slot(0, PCI_DEVFN(2, 0));

	/*get the address of the platform config vbt, B0:D2:F0;0xFC */
	pci_read_config_dword(pci_gfx_root, 0xFC, &platform_config_address);
	pci_dev_put(pci_gfx_root);
	DRM_INFO("drm platform config address is %x\n",
			platform_config_address);

	/* check for platform config address == 0. */
	/* this means fw doesn't support vbt */

	if (platform_config_address == 0) {
		vbt->size = 0;
		return;
	}

	/* get the virtual address of the vbt */
	vbt_virtual = ioremap(platform_config_address, sizeof(*vbt));

	memcpy(vbt, vbt_virtual, sizeof(*vbt));
	iounmap(vbt_virtual); /* Free virtual address space */

	printk(KERN_ALERT "GCT revision is %x\n", vbt->revision);

	switch (vbt->revision) {
	case 0:
		vbt->mrst_gct = NULL;
		vbt->mrst_gct = \
			ioremap(platform_config_address + sizeof(*vbt) - 4,
					vbt->size - sizeof(*vbt) + 4);
		pGCT = vbt->mrst_gct;
		bpi = ((struct mrst_gct_v1 *)pGCT)->PD.BootPanelIndex;
		dev_priv->gct_data.bpi = bpi;
		dev_priv->gct_data.pt =
			((struct mrst_gct_v1 *)pGCT)->PD.PanelType;
		memcpy(&dev_priv->gct_data.DTD,
			&((struct mrst_gct_v1 *)pGCT)->panel[bpi].DTD,
				sizeof(struct mrst_timing_info));
		dev_priv->gct_data.Panel_Port_Control =
		  ((struct mrst_gct_v1 *)pGCT)->panel[bpi].Panel_Port_Control;
		dev_priv->gct_data.Panel_MIPI_Display_Descriptor =
		  ((struct mrst_gct_v1 *)pGCT)->panel[bpi].Panel_MIPI_Display_Descriptor;
		break;
	case 1:
		vbt->mrst_gct = NULL;
		vbt->mrst_gct = \
			ioremap(platform_config_address + sizeof(*vbt) - 4,
					vbt->size - sizeof(*vbt) + 4);
		pGCT = vbt->mrst_gct;
		bpi = ((struct mrst_gct_v2 *)pGCT)->PD.BootPanelIndex;
		dev_priv->gct_data.bpi = bpi;
		dev_priv->gct_data.pt =
			((struct mrst_gct_v2 *)pGCT)->PD.PanelType;
		memcpy(&dev_priv->gct_data.DTD,
			&((struct mrst_gct_v2 *)pGCT)->panel[bpi].DTD,
				sizeof(struct mrst_timing_info));
		dev_priv->gct_data.Panel_Port_Control =
		  ((struct mrst_gct_v2 *)pGCT)->panel[bpi].Panel_Port_Control;
		dev_priv->gct_data.Panel_MIPI_Display_Descriptor =
		  ((struct mrst_gct_v2 *)pGCT)->panel[bpi].Panel_MIPI_Display_Descriptor;
		break;
	case 0x10:
		/*header definition changed from rev 01 (v2) to rev 10h. */
		/*so, some values have changed location*/
		new_size = vbt->checksum; /*checksum contains lo size byte*/
		/*LSB of mrst_gct contains hi size byte*/
		new_size |= ((0xff & (unsigned int)vbt->mrst_gct)) << 8;

		vbt->checksum = vbt->size; /*size contains the checksum*/
		if (new_size > 0xff)
			vbt->size = 0xff; /*restrict size to 255*/
		else
			vbt->size = new_size;

		/* number of descriptors defined in the GCT */
		number_desc = ((0xff00 & (unsigned int)vbt->mrst_gct)) >> 8;
		bpi = ((0xff0000 & (unsigned int)vbt->mrst_gct)) >> 16;
		vbt->mrst_gct = NULL;
		vbt->mrst_gct = \
			ioremap(platform_config_address + GCT_R10_HEADER_SIZE,
				GCT_R10_DISPLAY_DESC_SIZE * number_desc);
		pGCT = vbt->mrst_gct;
		pGCT = (u8 *)pGCT + (bpi*GCT_R10_DISPLAY_DESC_SIZE);
		dev_priv->gct_data.bpi = bpi; /*save boot panel id*/

		/*copy the GCT display timings into a temp structure*/
		memcpy(&ti, pGCT, sizeof(struct gct_r10_timing_info));

		/*now copy the temp struct into the dev_priv->gct_data*/
		dp_ti->pixel_clock = ti.pixel_clock;
		dp_ti->hactive_hi = ti.hactive_hi;
		dp_ti->hactive_lo = ti.hactive_lo;
		dp_ti->hblank_hi = ti.hblank_hi;
		dp_ti->hblank_lo = ti.hblank_lo;
		dp_ti->hsync_offset_hi = ti.hsync_offset_hi;
		dp_ti->hsync_offset_lo = ti.hsync_offset_lo;
		dp_ti->hsync_pulse_width_hi = ti.hsync_pulse_width_hi;
		dp_ti->hsync_pulse_width_lo = ti.hsync_pulse_width_lo;
		dp_ti->vactive_hi = ti.vactive_hi;
		dp_ti->vactive_lo = ti.vactive_lo;
		dp_ti->vblank_hi = ti.vblank_hi;
		dp_ti->vblank_lo = ti.vblank_lo;
		dp_ti->vsync_offset_hi = ti.vsync_offset_hi;
		dp_ti->vsync_offset_lo = ti.vsync_offset_lo;
		dp_ti->vsync_pulse_width_hi = ti.vsync_pulse_width_hi;
		dp_ti->vsync_pulse_width_lo = ti.vsync_pulse_width_lo;

		/*mov the MIPI_Display_Descriptor data from GCT to dev priv*/
		dev_priv->gct_data.Panel_MIPI_Display_Descriptor =
							*((u8 *)pGCT + 0x0d);
		dev_priv->gct_data.Panel_MIPI_Display_Descriptor |=
						(*((u8 *)pGCT + 0x0e)) << 8;
		break;
	default:
		printk(KERN_ERR "Unknown revision of GCT!\n");
		vbt->size = 0;
	}
}

static void psb_get_core_freq(struct drm_device *dev)
{
	uint32_t clock;
	struct pci_dev *pci_root = pci_get_bus_and_slot(0, 0);
	struct drm_psb_private *dev_priv = dev->dev_private;

	/*pci_write_config_dword(pci_root, 0xD4, 0x00C32004);*/
	/*pci_write_config_dword(pci_root, 0xD0, 0xE0033000);*/

	pci_write_config_dword(pci_root, 0xD0, 0xD0050300);
	pci_read_config_dword(pci_root, 0xD4, &clock);
	pci_dev_put(pci_root);

	switch (clock & 0x07) {
	case 0:
		dev_priv->core_freq = 100;
		break;
	case 1:
		dev_priv->core_freq = 133;
		break;
	case 2:
		dev_priv->core_freq = 150;
		break;
	case 3:
		dev_priv->core_freq = 178;
		break;
	case 4:
		dev_priv->core_freq = 200;
		break;
	case 5:
	case 6:
	case 7:
		dev_priv->core_freq = 266;
	default:
		dev_priv->core_freq = 0;
	}
}

static int psb_do_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv =
	    (struct drm_psb_private *) dev->dev_private;
	struct psb_gtt *pg = dev_priv->pg;

	uint32_t stolen_gtt;
	uint32_t tt_start;
	uint32_t tt_pages;

	int ret = -ENOMEM;

	if (pg->mmu_gatt_start & 0x0FFFFFFF) {
		DRM_ERROR("Gatt must be 256M aligned. This is a bug.\n");
		ret = -EINVAL;
		goto out_err;
	}


	stolen_gtt = (pg->stolen_size >> PAGE_SHIFT) * 4;
	stolen_gtt = (stolen_gtt + PAGE_SIZE - 1) >> PAGE_SHIFT;
	stolen_gtt =
	    (stolen_gtt < pg->gtt_pages) ? stolen_gtt : pg->gtt_pages;

	dev_priv->gatt_free_offset = pg->mmu_gatt_start +
	    (stolen_gtt << PAGE_SHIFT) * 1024;

	if (1 || drm_debug) {
		uint32_t core_id = PSB_RSGX32(PSB_CR_CORE_ID);
		uint32_t core_rev = PSB_RSGX32(PSB_CR_CORE_REVISION);
		DRM_INFO("SGX core id = 0x%08x\n", core_id);
		DRM_INFO("SGX core rev major = 0x%02x, minor = 0x%02x\n",
			 (core_rev & _PSB_CC_REVISION_MAJOR_MASK) >>
			 _PSB_CC_REVISION_MAJOR_SHIFT,
			 (core_rev & _PSB_CC_REVISION_MINOR_MASK) >>
			 _PSB_CC_REVISION_MINOR_SHIFT);
		DRM_INFO
		    ("SGX core rev maintenance = 0x%02x, designer = 0x%02x\n",
		     (core_rev & _PSB_CC_REVISION_MAINTENANCE_MASK) >>
		     _PSB_CC_REVISION_MAINTENANCE_SHIFT,
		     (core_rev & _PSB_CC_REVISION_DESIGNER_MASK) >>
		     _PSB_CC_REVISION_DESIGNER_SHIFT);
	}


	spin_lock_init(&dev_priv->irqmask_lock);

	tt_pages = (pg->gatt_pages < PSB_TT_PRIV0_PLIMIT) ?
	    pg->gatt_pages : PSB_TT_PRIV0_PLIMIT;
	tt_start = dev_priv->gatt_free_offset - pg->mmu_gatt_start;
	tt_pages -= tt_start >> PAGE_SHIFT;
	/* FIXME: can we kill ta_mem_size ? */
	dev_priv->sizes.ta_mem_size = 0;

	PSB_WSGX32(0x00000000, PSB_CR_BIF_BANK0);
	PSB_WSGX32(0x00000000, PSB_CR_BIF_BANK1);
	PSB_RSGX32(PSB_CR_BIF_BANK1);
        PSB_WSGX32(PSB_RSGX32(PSB_CR_BIF_CTRL) | _PSB_MMU_ER_MASK,
							PSB_CR_BIF_CTRL);
	psb_spank(dev_priv);

	/* mmu_gatt ?? */
      	PSB_WSGX32(pg->gatt_start, PSB_CR_BIF_TWOD_REQ_BASE);

	return 0;
out_err:
	psb_do_takedown(dev);
	return ret;
}

static int psb_driver_unload(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv =
	    (struct drm_psb_private *) dev->dev_private;

	/* Kill vblank etc here */

	psb_backlight_exit(); /*writes minimum value to backlight HW reg */

	if (drm_psb_no_fb == 0)
		psb_modeset_cleanup(dev);

	if (dev_priv) {
		psb_lid_timer_takedown(dev_priv);

		psb_do_takedown(dev);


		if (dev_priv->pf_pd) {
			psb_mmu_free_pagedir(dev_priv->pf_pd);
			dev_priv->pf_pd = NULL;
		}
		if (dev_priv->mmu) {
			struct psb_gtt *pg = dev_priv->pg;

			down_read(&pg->sem);
			psb_mmu_remove_pfn_sequence(
				psb_mmu_get_default_pd
				(dev_priv->mmu),
				pg->mmu_gatt_start,
				dev_priv->vram_stolen_size >> PAGE_SHIFT);
			up_read(&pg->sem);
			psb_mmu_driver_takedown(dev_priv->mmu);
			dev_priv->mmu = NULL;
		}
		psb_gtt_takedown(dev);
		if (dev_priv->scratch_page) {
			__free_page(dev_priv->scratch_page);
			dev_priv->scratch_page = NULL;
		}
		if (dev_priv->vdc_reg) {
			iounmap(dev_priv->vdc_reg);
			dev_priv->vdc_reg = NULL;
		}
		if (dev_priv->sgx_reg) {
			iounmap(dev_priv->sgx_reg);
			dev_priv->sgx_reg = NULL;
		}

		kfree(dev_priv);
		dev->dev_private = NULL;

		/*destroy VBT data*/
		psb_intel_destroy_bios(dev);
	}

	gma_power_uninit(dev);

	return 0;
}


static int psb_driver_load(struct drm_device *dev, unsigned long chipset)
{
	struct drm_psb_private *dev_priv;
	unsigned long resource_start;
	struct psb_gtt *pg;
	unsigned long irqflags;
	int ret = -ENOMEM;
	uint32_t tt_pages;
	struct drm_connector *connector;
	struct psb_intel_output *psb_intel_output;

	dev_priv = kzalloc(sizeof(*dev_priv), GFP_KERNEL);
	if (dev_priv == NULL)
		return -ENOMEM;

	if (IS_MRST(dev))
		dev_priv->num_pipe = 1;
	else
		dev_priv->num_pipe = 2;

	dev_priv->dev = dev;

	dev->dev_private = (void *) dev_priv;
	dev_priv->chipset = chipset;

	PSB_DEBUG_INIT("Mapping MMIO\n");
	resource_start = pci_resource_start(dev->pdev, PSB_MMIO_RESOURCE);

	dev_priv->vdc_reg =
	    ioremap(resource_start + PSB_VDC_OFFSET, PSB_VDC_SIZE);
	if (!dev_priv->vdc_reg)
		goto out_err;

	if (IS_MRST(dev))
		dev_priv->sgx_reg = ioremap(resource_start + MRST_SGX_OFFSET,
							PSB_SGX_SIZE);
	else
		dev_priv->sgx_reg = ioremap(resource_start + PSB_SGX_OFFSET,
							PSB_SGX_SIZE);

	if (!dev_priv->sgx_reg)
		goto out_err;

	if (IS_MRST(dev)) {
		mrst_get_fuse_settings(dev);
		mrst_get_vbt_data(dev_priv);
		mid_get_pci_revID(dev_priv);
	} else {
		psb_get_core_freq(dev);
		psb_intel_opregion_init(dev);
		psb_intel_init_bios(dev);
	}

	/* Init OSPM support */
	gma_power_init(dev);

	ret = -ENOMEM;

	dev_priv->scratch_page = alloc_page(GFP_DMA32 | __GFP_ZERO);
	if (!dev_priv->scratch_page)
		goto out_err;

	set_pages_uc(dev_priv->scratch_page, 1);

	ret = psb_gtt_init(dev, 0);
	if (ret)
		goto out_err;

	dev_priv->mmu = psb_mmu_driver_init((void *)0,
					drm_psb_trap_pagefaults, 0,
					dev_priv);
	if (!dev_priv->mmu)
		goto out_err;

	pg = dev_priv->pg;

	tt_pages = (pg->gatt_pages < PSB_TT_PRIV0_PLIMIT) ?
		(pg->gatt_pages) : PSB_TT_PRIV0_PLIMIT;


	dev_priv->pf_pd = psb_mmu_alloc_pd(dev_priv->mmu, 1, 0);
	if (!dev_priv->pf_pd)
		goto out_err;

	psb_mmu_set_pd_context(psb_mmu_get_default_pd(dev_priv->mmu), 0);
	psb_mmu_set_pd_context(dev_priv->pf_pd, 1);

	ret = psb_do_init(dev);
	if (ret)
		return ret;

	PSB_WSGX32(0x20000000, PSB_CR_PDS_EXEC_BASE);
	PSB_WSGX32(0x30000000, PSB_CR_BIF_3D_REQ_BASE);

/*	igd_opregion_init(&dev_priv->opregion_dev); */
	acpi_video_register();
	if (dev_priv->lid_state)
		psb_lid_timer_init(dev_priv);

	ret = drm_vblank_init(dev, dev_priv->num_pipe);
	if (ret)
		goto out_err;

	/*
	 * Install interrupt handlers prior to powering off SGX or else we will
	 * crash.
	 */
	dev_priv->vdc_irq_mask = 0;
	dev_priv->pipestat[0] = 0;
	dev_priv->pipestat[1] = 0;
	dev_priv->pipestat[2] = 0;
	spin_lock_irqsave(&dev_priv->irqmask_lock, irqflags);
	PSB_WVDC32(0xFFFFFFFF, PSB_HWSTAM);
	PSB_WVDC32(0x00000000, PSB_INT_ENABLE_R);
	PSB_WVDC32(0xFFFFFFFF, PSB_INT_MASK_R);
	spin_unlock_irqrestore(&dev_priv->irqmask_lock, irqflags);
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_irq_install(dev);

	dev->vblank_disable_allowed = 1;

	dev->max_vblank_count = 0xffffff; /* only 24 bits of frame count */

	dev->driver->get_vblank_counter = psb_get_vblank_counter;

	if (drm_psb_no_fb == 0) {
		psb_modeset_init(dev);
		psb_fbdev_init(dev);
		drm_kms_helper_poll_init(dev);
	}

	/* Only add backlight support if we have LVDS output */
	list_for_each_entry(connector, &dev->mode_config.connector_list,
			    head) {
		psb_intel_output = to_psb_intel_output(connector);

		switch (psb_intel_output->type) {
		case INTEL_OUTPUT_LVDS:
			ret = psb_backlight_init(dev);
			break;
		}
	}

	if (ret)
		return ret;
#if 0
	/*enable runtime pm at last*/
	pm_runtime_enable(&dev->pdev->dev);
	pm_runtime_set_active(&dev->pdev->dev);
#endif
	/*Intel drm driver load is done, continue doing pvr load*/
	DRM_DEBUG("Pvr driver load\n");
	return 0;
out_err:
	psb_driver_unload(dev);
	return ret;
}

int psb_driver_device_is_agp(struct drm_device *dev)
{
	return 0;
}


static int psb_sizes_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_psb_private *dev_priv = psb_priv(dev);
	struct drm_psb_sizes_arg *arg =
		(struct drm_psb_sizes_arg *) data;

	*arg = dev_priv->sizes;
	return 0;
}

static int psb_dc_state_ioctl(struct drm_device *dev, void * data,
				struct drm_file *file_priv)
{
	uint32_t flags;
	uint32_t obj_id;
	struct drm_mode_object *obj;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	struct drm_psb_dc_state_arg *arg =
		(struct drm_psb_dc_state_arg *)data;

	flags = arg->flags;
	obj_id = arg->obj_id;

	if (flags & PSB_DC_CRTC_MASK) {
		obj = drm_mode_object_find(dev, obj_id,
				DRM_MODE_OBJECT_CRTC);
		if (!obj) {
			DRM_DEBUG("Invalid CRTC object.\n");
			return -EINVAL;
		}

		crtc = obj_to_crtc(obj);

		mutex_lock(&dev->mode_config.mutex);
		if (drm_helper_crtc_in_use(crtc)) {
			if (flags & PSB_DC_CRTC_SAVE)
				crtc->funcs->save(crtc);
			else
				crtc->funcs->restore(crtc);
		}
		mutex_unlock(&dev->mode_config.mutex);

		return 0;
	} else if (flags & PSB_DC_OUTPUT_MASK) {
		obj = drm_mode_object_find(dev, obj_id,
				DRM_MODE_OBJECT_CONNECTOR);
		if (!obj) {
			DRM_DEBUG("Invalid connector id.\n");
			return -EINVAL;
		}

		connector = obj_to_connector(obj);
		if (flags & PSB_DC_OUTPUT_SAVE)
			connector->funcs->save(connector);
		else
			connector->funcs->restore(connector);

		return 0;
	}

	DRM_DEBUG("Bad flags 0x%x\n", flags);
	return -EINVAL;
}

static int psb_dpst_bl_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_psb_private *dev_priv = psb_priv(dev);
	uint32_t *arg = data;
	struct backlight_device bd;
	dev_priv->blc_adj2 = *arg;

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	bd.props.brightness = psb_get_brightness(&bd);
	psb_set_brightness(&bd);
#endif
	return 0;
}

static int psb_adb_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_psb_private *dev_priv = psb_priv(dev);
	uint32_t *arg = data;
	struct backlight_device bd;
	dev_priv->blc_adj1 = *arg;

#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	bd.props.brightness = psb_get_brightness(&bd);
	psb_set_brightness(&bd);
#endif
	return 0;
}

/* return the current mode to the dpst module */
static int psb_dpst_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_psb_private *dev_priv = psb_priv(dev);
	uint32_t *arg = data;
	uint32_t x;
	uint32_t y;
	uint32_t reg;

	if (!gma_power_begin(dev, 0))
		return -EIO;

	reg = PSB_RVDC32(PIPEASRC);

	gma_power_end(dev);

	/* horizontal is the left 16 bits */
	x = reg >> 16;
	/* vertical is the right 16 bits */
	y = reg & 0x0000ffff;

	/* the values are the image size minus one */
	x++;
	y++;

	*arg = (x << 16) | y;

	return 0;
}
static int psb_gamma_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_psb_dpst_lut_arg *lut_arg = data;
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	struct psb_intel_crtc *psb_intel_crtc;
	int i = 0;
	int32_t obj_id;

	obj_id = lut_arg->output_id;
	obj = drm_mode_object_find(dev, obj_id, DRM_MODE_OBJECT_CONNECTOR);
	if (!obj) {
		DRM_DEBUG("Invalid Connector object.\n");
		return -EINVAL;
	}

	connector = obj_to_connector(obj);
	crtc = connector->encoder->crtc;
	psb_intel_crtc = to_psb_intel_crtc(crtc);

	for (i = 0; i < 256; i++)
		psb_intel_crtc->lut_adj[i] = lut_arg->lut[i];

	psb_intel_crtc_load_lut(crtc);

	return 0;
}

static int psb_mode_operation_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	uint32_t obj_id;
	uint16_t op;
	struct drm_mode_modeinfo *umode;
	struct drm_display_mode *mode = NULL;
	struct drm_psb_mode_operation_arg *arg;
	struct drm_mode_object *obj;
	struct drm_connector *connector;
	struct drm_framebuffer *drm_fb;
	struct psb_framebuffer *psb_fb;
	struct drm_connector_helper_funcs *connector_funcs;
	int ret = 0;
	int resp = MODE_OK;
	struct drm_psb_private *dev_priv = psb_priv(dev);

	arg = (struct drm_psb_mode_operation_arg *)data;
	obj_id = arg->obj_id;
	op = arg->operation;

	switch (op) {
	case PSB_MODE_OPERATION_SET_DC_BASE:
		obj = drm_mode_object_find(dev, obj_id, DRM_MODE_OBJECT_FB);
		if (!obj) {
			DRM_ERROR("Invalid FB id %d\n", obj_id);
			return -EINVAL;
		}

		drm_fb = obj_to_fb(obj);
		psb_fb = to_psb_fb(drm_fb);

		if (gma_power_begin(dev, 0)) {
			REG_WRITE(DSPASURF, psb_fb->gtt->offset);
			REG_READ(DSPASURF);
			gma_power_end(dev);
		} else {
			dev_priv->saveDSPASURF = psb_fb->gtt->offset;
		}

		return 0;
	case PSB_MODE_OPERATION_MODE_VALID:
		umode = &arg->mode;

		mutex_lock(&dev->mode_config.mutex);

		obj = drm_mode_object_find(dev, obj_id,
					DRM_MODE_OBJECT_CONNECTOR);
		if (!obj) {
			ret = -EINVAL;
			goto mode_op_out;
		}

		connector = obj_to_connector(obj);

		mode = drm_mode_create(dev);
		if (!mode) {
			ret = -ENOMEM;
			goto mode_op_out;
		}

		/* drm_crtc_convert_umode(mode, umode); */
		{
			mode->clock = umode->clock;
			mode->hdisplay = umode->hdisplay;
			mode->hsync_start = umode->hsync_start;
			mode->hsync_end = umode->hsync_end;
			mode->htotal = umode->htotal;
			mode->hskew = umode->hskew;
			mode->vdisplay = umode->vdisplay;
			mode->vsync_start = umode->vsync_start;
			mode->vsync_end = umode->vsync_end;
			mode->vtotal = umode->vtotal;
			mode->vscan = umode->vscan;
			mode->vrefresh = umode->vrefresh;
			mode->flags = umode->flags;
			mode->type = umode->type;
			strncpy(mode->name, umode->name, DRM_DISPLAY_MODE_LEN);
			mode->name[DRM_DISPLAY_MODE_LEN-1] = 0;
		}

		connector_funcs = (struct drm_connector_helper_funcs *)
				   connector->helper_private;

		if (connector_funcs->mode_valid) {
			resp = connector_funcs->mode_valid(connector, mode);
			arg->data = (void *)resp;
		}

		/*do some clean up work*/
		if (mode)
			drm_mode_destroy(dev, mode);
mode_op_out:
		mutex_unlock(&dev->mode_config.mutex);
		return ret;

	default:
		DRM_DEBUG("Unsupported psb mode operation");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int psb_stolen_memory_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file_priv)
{
	struct drm_psb_private *dev_priv = psb_priv(dev);
	struct drm_psb_stolen_memory_arg *arg = data;

	arg->base = dev_priv->stolen_base;
	arg->size = dev_priv->vram_stolen_size;

	return 0;
}

static int psb_register_rw_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct drm_psb_private *dev_priv = psb_priv(dev);
	struct drm_psb_register_rw_arg *arg = data;
	bool usage = arg->b_force_hw_on ? true : false;

	if (arg->display_write_mask != 0) {
		if (gma_power_begin(dev, usage)) {
			if (arg->display_write_mask & REGRWBITS_PFIT_CONTROLS)
				PSB_WVDC32(arg->display.pfit_controls,
					   PFIT_CONTROL);
			if (arg->display_write_mask &
			    REGRWBITS_PFIT_AUTOSCALE_RATIOS)
				PSB_WVDC32(arg->display.pfit_autoscale_ratios,
					   PFIT_AUTO_RATIOS);
			if (arg->display_write_mask &
			    REGRWBITS_PFIT_PROGRAMMED_SCALE_RATIOS)
				PSB_WVDC32(
				   arg->display.pfit_programmed_scale_ratios,
				   PFIT_PGM_RATIOS);
			if (arg->display_write_mask & REGRWBITS_PIPEASRC)
				PSB_WVDC32(arg->display.pipeasrc,
					   PIPEASRC);
			if (arg->display_write_mask & REGRWBITS_PIPEBSRC)
				PSB_WVDC32(arg->display.pipebsrc,
					   PIPEBSRC);
			if (arg->display_write_mask & REGRWBITS_VTOTAL_A)
				PSB_WVDC32(arg->display.vtotal_a,
					   VTOTAL_A);
			if (arg->display_write_mask & REGRWBITS_VTOTAL_B)
				PSB_WVDC32(arg->display.vtotal_b,
					   VTOTAL_B);
			gma_power_end(dev);
		} else {
			if (arg->display_write_mask & REGRWBITS_PFIT_CONTROLS)
				dev_priv->savePFIT_CONTROL =
						arg->display.pfit_controls;
			if (arg->display_write_mask &
			    REGRWBITS_PFIT_AUTOSCALE_RATIOS)
				dev_priv->savePFIT_AUTO_RATIOS =
					arg->display.pfit_autoscale_ratios;
			if (arg->display_write_mask &
			    REGRWBITS_PFIT_PROGRAMMED_SCALE_RATIOS)
				dev_priv->savePFIT_PGM_RATIOS =
				   arg->display.pfit_programmed_scale_ratios;
			if (arg->display_write_mask & REGRWBITS_PIPEASRC)
				dev_priv->savePIPEASRC = arg->display.pipeasrc;
			if (arg->display_write_mask & REGRWBITS_PIPEBSRC)
				dev_priv->savePIPEBSRC = arg->display.pipebsrc;
			if (arg->display_write_mask & REGRWBITS_VTOTAL_A)
				dev_priv->saveVTOTAL_A = arg->display.vtotal_a;
			if (arg->display_write_mask & REGRWBITS_VTOTAL_B)
				dev_priv->saveVTOTAL_B = arg->display.vtotal_b;
		}
	}

	if (arg->display_read_mask != 0) {
		if (gma_power_begin(dev, usage)) {
			if (arg->display_read_mask &
			    REGRWBITS_PFIT_CONTROLS)
				arg->display.pfit_controls =
						PSB_RVDC32(PFIT_CONTROL);
			if (arg->display_read_mask &
			    REGRWBITS_PFIT_AUTOSCALE_RATIOS)
				arg->display.pfit_autoscale_ratios =
						PSB_RVDC32(PFIT_AUTO_RATIOS);
			if (arg->display_read_mask &
			    REGRWBITS_PFIT_PROGRAMMED_SCALE_RATIOS)
				arg->display.pfit_programmed_scale_ratios =
						PSB_RVDC32(PFIT_PGM_RATIOS);
			if (arg->display_read_mask & REGRWBITS_PIPEASRC)
				arg->display.pipeasrc = PSB_RVDC32(PIPEASRC);
			if (arg->display_read_mask & REGRWBITS_PIPEBSRC)
				arg->display.pipebsrc = PSB_RVDC32(PIPEBSRC);
			if (arg->display_read_mask & REGRWBITS_VTOTAL_A)
				arg->display.vtotal_a = PSB_RVDC32(VTOTAL_A);
			if (arg->display_read_mask & REGRWBITS_VTOTAL_B)
				arg->display.vtotal_b = PSB_RVDC32(VTOTAL_B);
			gma_power_end(dev);
		} else {
			if (arg->display_read_mask &
			    REGRWBITS_PFIT_CONTROLS)
				arg->display.pfit_controls =
						dev_priv->savePFIT_CONTROL;
			if (arg->display_read_mask &
			    REGRWBITS_PFIT_AUTOSCALE_RATIOS)
				arg->display.pfit_autoscale_ratios =
						dev_priv->savePFIT_AUTO_RATIOS;
			if (arg->display_read_mask &
			    REGRWBITS_PFIT_PROGRAMMED_SCALE_RATIOS)
				arg->display.pfit_programmed_scale_ratios =
						dev_priv->savePFIT_PGM_RATIOS;
			if (arg->display_read_mask & REGRWBITS_PIPEASRC)
				arg->display.pipeasrc = dev_priv->savePIPEASRC;
			if (arg->display_read_mask & REGRWBITS_PIPEBSRC)
				arg->display.pipebsrc = dev_priv->savePIPEBSRC;
			if (arg->display_read_mask & REGRWBITS_VTOTAL_A)
				arg->display.vtotal_a = dev_priv->saveVTOTAL_A;
			if (arg->display_read_mask & REGRWBITS_VTOTAL_B)
				arg->display.vtotal_b = dev_priv->saveVTOTAL_B;
		}
	}

	if (arg->overlay_write_mask != 0) {
		if (gma_power_begin(dev, usage)) {
			if (arg->overlay_write_mask & OV_REGRWBITS_OGAM_ALL) {
				PSB_WVDC32(arg->overlay.OGAMC5, OV_OGAMC5);
				PSB_WVDC32(arg->overlay.OGAMC4, OV_OGAMC4);
				PSB_WVDC32(arg->overlay.OGAMC3, OV_OGAMC3);
				PSB_WVDC32(arg->overlay.OGAMC2, OV_OGAMC2);
				PSB_WVDC32(arg->overlay.OGAMC1, OV_OGAMC1);
				PSB_WVDC32(arg->overlay.OGAMC0, OV_OGAMC0);
			}
			if (arg->overlay_write_mask & OVC_REGRWBITS_OGAM_ALL) {
				PSB_WVDC32(arg->overlay.OGAMC5, OVC_OGAMC5);
				PSB_WVDC32(arg->overlay.OGAMC4, OVC_OGAMC4);
				PSB_WVDC32(arg->overlay.OGAMC3, OVC_OGAMC3);
				PSB_WVDC32(arg->overlay.OGAMC2, OVC_OGAMC2);
				PSB_WVDC32(arg->overlay.OGAMC1, OVC_OGAMC1);
				PSB_WVDC32(arg->overlay.OGAMC0, OVC_OGAMC0);
			}

			if (arg->overlay_write_mask & OV_REGRWBITS_OVADD) {
				PSB_WVDC32(arg->overlay.OVADD, OV_OVADD);

				if (arg->overlay.b_wait_vblank) {
					/* Wait for 20ms.*/
					unsigned long vblank_timeout = jiffies
								+ HZ/50;
					uint32_t temp;
					while (time_before_eq(jiffies,
							vblank_timeout)) {
						temp = PSB_RVDC32(OV_DOVASTA);
						if ((temp & (0x1 << 31)) != 0)
							break;
						cpu_relax();
					}
				}
			}
			if (arg->overlay_write_mask & OVC_REGRWBITS_OVADD) {
				PSB_WVDC32(arg->overlay.OVADD, OVC_OVADD);
				if (arg->overlay.b_wait_vblank) {
					/* Wait for 20ms.*/
					unsigned long vblank_timeout =
							jiffies + HZ/50;
					uint32_t temp;
					while (time_before_eq(jiffies,
							vblank_timeout)) {
						temp = PSB_RVDC32(OVC_DOVCSTA);
						if ((temp & (0x1 << 31)) != 0)
							break;
						cpu_relax();
					}
				}
			}
			gma_power_end(dev);
		} else {
			if (arg->overlay_write_mask & OV_REGRWBITS_OGAM_ALL) {
				dev_priv->saveOV_OGAMC5 = arg->overlay.OGAMC5;
				dev_priv->saveOV_OGAMC4 = arg->overlay.OGAMC4;
				dev_priv->saveOV_OGAMC3 = arg->overlay.OGAMC3;
				dev_priv->saveOV_OGAMC2 = arg->overlay.OGAMC2;
				dev_priv->saveOV_OGAMC1 = arg->overlay.OGAMC1;
				dev_priv->saveOV_OGAMC0 = arg->overlay.OGAMC0;
			}
			if (arg->overlay_write_mask & OVC_REGRWBITS_OGAM_ALL) {
				dev_priv->saveOVC_OGAMC5 = arg->overlay.OGAMC5;
				dev_priv->saveOVC_OGAMC4 = arg->overlay.OGAMC4;
				dev_priv->saveOVC_OGAMC3 = arg->overlay.OGAMC3;
				dev_priv->saveOVC_OGAMC2 = arg->overlay.OGAMC2;
				dev_priv->saveOVC_OGAMC1 = arg->overlay.OGAMC1;
				dev_priv->saveOVC_OGAMC0 = arg->overlay.OGAMC0;
			}
			if (arg->overlay_write_mask & OV_REGRWBITS_OVADD)
				dev_priv->saveOV_OVADD = arg->overlay.OVADD;
			if (arg->overlay_write_mask & OVC_REGRWBITS_OVADD)
				dev_priv->saveOVC_OVADD = arg->overlay.OVADD;
		}
	}

	if (arg->overlay_read_mask != 0) {
		if (gma_power_begin(dev, usage)) {
			if (arg->overlay_read_mask & OV_REGRWBITS_OGAM_ALL) {
				arg->overlay.OGAMC5 = PSB_RVDC32(OV_OGAMC5);
				arg->overlay.OGAMC4 = PSB_RVDC32(OV_OGAMC4);
				arg->overlay.OGAMC3 = PSB_RVDC32(OV_OGAMC3);
				arg->overlay.OGAMC2 = PSB_RVDC32(OV_OGAMC2);
				arg->overlay.OGAMC1 = PSB_RVDC32(OV_OGAMC1);
				arg->overlay.OGAMC0 = PSB_RVDC32(OV_OGAMC0);
			}
			if (arg->overlay_read_mask & OVC_REGRWBITS_OGAM_ALL) {
				arg->overlay.OGAMC5 = PSB_RVDC32(OVC_OGAMC5);
				arg->overlay.OGAMC4 = PSB_RVDC32(OVC_OGAMC4);
				arg->overlay.OGAMC3 = PSB_RVDC32(OVC_OGAMC3);
				arg->overlay.OGAMC2 = PSB_RVDC32(OVC_OGAMC2);
				arg->overlay.OGAMC1 = PSB_RVDC32(OVC_OGAMC1);
				arg->overlay.OGAMC0 = PSB_RVDC32(OVC_OGAMC0);
			}
			if (arg->overlay_read_mask & OV_REGRWBITS_OVADD)
				arg->overlay.OVADD = PSB_RVDC32(OV_OVADD);
			if (arg->overlay_read_mask & OVC_REGRWBITS_OVADD)
				arg->overlay.OVADD = PSB_RVDC32(OVC_OVADD);
			gma_power_end(dev);
		} else {
			if (arg->overlay_read_mask & OV_REGRWBITS_OGAM_ALL) {
				arg->overlay.OGAMC5 = dev_priv->saveOV_OGAMC5;
				arg->overlay.OGAMC4 = dev_priv->saveOV_OGAMC4;
				arg->overlay.OGAMC3 = dev_priv->saveOV_OGAMC3;
				arg->overlay.OGAMC2 = dev_priv->saveOV_OGAMC2;
				arg->overlay.OGAMC1 = dev_priv->saveOV_OGAMC1;
				arg->overlay.OGAMC0 = dev_priv->saveOV_OGAMC0;
			}
			if (arg->overlay_read_mask & OVC_REGRWBITS_OGAM_ALL) {
				arg->overlay.OGAMC5 = dev_priv->saveOVC_OGAMC5;
				arg->overlay.OGAMC4 = dev_priv->saveOVC_OGAMC4;
				arg->overlay.OGAMC3 = dev_priv->saveOVC_OGAMC3;
				arg->overlay.OGAMC2 = dev_priv->saveOVC_OGAMC2;
				arg->overlay.OGAMC1 = dev_priv->saveOVC_OGAMC1;
				arg->overlay.OGAMC0 = dev_priv->saveOVC_OGAMC0;
			}
			if (arg->overlay_read_mask & OV_REGRWBITS_OVADD)
				arg->overlay.OVADD = dev_priv->saveOV_OVADD;
			if (arg->overlay_read_mask & OVC_REGRWBITS_OVADD)
				arg->overlay.OVADD = dev_priv->saveOVC_OVADD;
		}
	}

	if (arg->sprite_enable_mask != 0) {
		if (gma_power_begin(dev, usage)) {
			PSB_WVDC32(0x1F3E, DSPARB);
			PSB_WVDC32(arg->sprite.dspa_control
					| PSB_RVDC32(DSPACNTR), DSPACNTR);
			PSB_WVDC32(arg->sprite.dspa_key_value, DSPAKEYVAL);
			PSB_WVDC32(arg->sprite.dspa_key_mask, DSPAKEYMASK);
			PSB_WVDC32(PSB_RVDC32(DSPASURF), DSPASURF);
			PSB_RVDC32(DSPASURF);
			PSB_WVDC32(arg->sprite.dspc_control, DSPCCNTR);
			PSB_WVDC32(arg->sprite.dspc_stride, DSPCSTRIDE);
			PSB_WVDC32(arg->sprite.dspc_position, DSPCPOS);
			PSB_WVDC32(arg->sprite.dspc_linear_offset, DSPCLINOFF);
			PSB_WVDC32(arg->sprite.dspc_size, DSPCSIZE);
			PSB_WVDC32(arg->sprite.dspc_surface, DSPCSURF);
			PSB_RVDC32(DSPCSURF);
			gma_power_end(dev);
		}
	}

	if (arg->sprite_disable_mask != 0) {
		if (gma_power_begin(dev, usage)) {
			PSB_WVDC32(0x3F3E, DSPARB);
			PSB_WVDC32(0x0, DSPCCNTR);
			PSB_WVDC32(arg->sprite.dspc_surface, DSPCSURF);
			PSB_RVDC32(DSPCSURF);
			gma_power_end(dev);
		}
	}

	if (arg->subpicture_enable_mask != 0) {
		if (gma_power_begin(dev, usage)) {
			uint32_t temp;
			if (arg->subpicture_enable_mask & REGRWBITS_DSPACNTR) {
				temp =  PSB_RVDC32(DSPACNTR);
				temp &= ~DISPPLANE_PIXFORMAT_MASK;
				temp &= ~DISPPLANE_BOTTOM;
				temp |= DISPPLANE_32BPP;
				PSB_WVDC32(temp, DSPACNTR);

				temp =  PSB_RVDC32(DSPABASE);
				PSB_WVDC32(temp, DSPABASE);
				PSB_RVDC32(DSPABASE);
				temp =  PSB_RVDC32(DSPASURF);
				PSB_WVDC32(temp, DSPASURF);
				PSB_RVDC32(DSPASURF);
			}
			if (arg->subpicture_enable_mask & REGRWBITS_DSPBCNTR) {
				temp =  PSB_RVDC32(DSPBCNTR);
				temp &= ~DISPPLANE_PIXFORMAT_MASK;
				temp &= ~DISPPLANE_BOTTOM;
				temp |= DISPPLANE_32BPP;
				PSB_WVDC32(temp, DSPBCNTR);

				temp =  PSB_RVDC32(DSPBBASE);
				PSB_WVDC32(temp, DSPBBASE);
				PSB_RVDC32(DSPBBASE);
				temp =  PSB_RVDC32(DSPBSURF);
				PSB_WVDC32(temp, DSPBSURF);
				PSB_RVDC32(DSPBSURF);
			}
			if (arg->subpicture_enable_mask & REGRWBITS_DSPCCNTR) {
				temp =  PSB_RVDC32(DSPCCNTR);
				temp &= ~DISPPLANE_PIXFORMAT_MASK;
				temp &= ~DISPPLANE_BOTTOM;
				temp |= DISPPLANE_32BPP;
				PSB_WVDC32(temp, DSPCCNTR);

				temp =  PSB_RVDC32(DSPCBASE);
				PSB_WVDC32(temp, DSPCBASE);
				PSB_RVDC32(DSPCBASE);
				temp =  PSB_RVDC32(DSPCSURF);
				PSB_WVDC32(temp, DSPCSURF);
				PSB_RVDC32(DSPCSURF);
			}
			gma_power_end(dev);
		}
	}

	if (arg->subpicture_disable_mask != 0) {
		if (gma_power_begin(dev, usage)) {
			uint32_t temp;
			if (arg->subpicture_disable_mask & REGRWBITS_DSPACNTR) {
				temp =  PSB_RVDC32(DSPACNTR);
				temp &= ~DISPPLANE_PIXFORMAT_MASK;
				temp |= DISPPLANE_32BPP_NO_ALPHA;
				PSB_WVDC32(temp, DSPACNTR);

				temp =  PSB_RVDC32(DSPABASE);
				PSB_WVDC32(temp, DSPABASE);
				PSB_RVDC32(DSPABASE);
				temp =  PSB_RVDC32(DSPASURF);
				PSB_WVDC32(temp, DSPASURF);
				PSB_RVDC32(DSPASURF);
			}
			if (arg->subpicture_disable_mask & REGRWBITS_DSPBCNTR) {
				temp =  PSB_RVDC32(DSPBCNTR);
				temp &= ~DISPPLANE_PIXFORMAT_MASK;
				temp |= DISPPLANE_32BPP_NO_ALPHA;
				PSB_WVDC32(temp, DSPBCNTR);

				temp =  PSB_RVDC32(DSPBBASE);
				PSB_WVDC32(temp, DSPBBASE);
				PSB_RVDC32(DSPBBASE);
				temp =  PSB_RVDC32(DSPBSURF);
				PSB_WVDC32(temp, DSPBSURF);
				PSB_RVDC32(DSPBSURF);
			}
			if (arg->subpicture_disable_mask & REGRWBITS_DSPCCNTR) {
				temp =  PSB_RVDC32(DSPCCNTR);
				temp &= ~DISPPLANE_PIXFORMAT_MASK;
				temp |= DISPPLANE_32BPP_NO_ALPHA;
				PSB_WVDC32(temp, DSPCCNTR);

				temp =  PSB_RVDC32(DSPCBASE);
				PSB_WVDC32(temp, DSPCBASE);
				PSB_RVDC32(DSPCBASE);
				temp =  PSB_RVDC32(DSPCSURF);
				PSB_WVDC32(temp, DSPCSURF);
				PSB_RVDC32(DSPCSURF);
			}
			gma_power_end(dev);
		}
	}

	return 0;
}

static int psb_driver_open(struct drm_device *dev, struct drm_file *priv)
{
	return 0;
}

static void psb_driver_close(struct drm_device *dev, struct drm_file *priv)
{
}

static long psb_unlocked_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	static unsigned int runtime_allowed;
	unsigned int nr = DRM_IOCTL_NR(cmd);

	DRM_DEBUG("cmd = %x, nr = %x\n", cmd, nr);

	if (runtime_allowed == 1 && dev_priv->is_lvds_on) {
		runtime_allowed++;
		pm_runtime_allow(&dev->pdev->dev);
		dev_priv->rpm_enabled = 1;
	}
	return drm_ioctl(filp, cmd, arg);
	
	/* FIXME: do we need to wrap the other side of this */
}


/* When a client dies:
 *    - Check for and clean up flipped page state
 */
void psb_driver_preclose(struct drm_device *dev, struct drm_file *priv)
{
}

static void psb_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	drm_put_dev(dev);
}

static const struct dev_pm_ops psb_pm_ops = {
	.runtime_suspend = psb_runtime_suspend,
	.runtime_resume = psb_runtime_resume,
	.runtime_idle = psb_runtime_idle,
};

static struct vm_operations_struct psb_gem_vm_ops = {
	.fault = psb_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static struct drm_driver driver = {
	.driver_features = DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED | \
			   DRIVER_IRQ_VBL | DRIVER_MODESET| DRIVER_GEM ,
	.load = psb_driver_load,
	.unload = psb_driver_unload,

	.ioctls = psb_ioctls,
	.num_ioctls = DRM_ARRAY_SIZE(psb_ioctls),
	.device_is_agp = psb_driver_device_is_agp,
	.irq_preinstall = psb_irq_preinstall,
	.irq_postinstall = psb_irq_postinstall,
	.irq_uninstall = psb_irq_uninstall,
	.irq_handler = psb_irq_handler,
	.enable_vblank = psb_enable_vblank,
	.disable_vblank = psb_disable_vblank,
	.get_vblank_counter = psb_get_vblank_counter,
	.lastclose = psb_lastclose,
	.open = psb_driver_open,
	.preclose = psb_driver_preclose,
	.postclose = psb_driver_close,
	.reclaim_buffers = drm_core_reclaim_buffers,

	.gem_init_object = psb_gem_init_object,
	.gem_free_object = psb_gem_free_object,
	.gem_vm_ops = &psb_gem_vm_ops,
	.dumb_create = psb_gem_dumb_create,
	.dumb_map_offset = psb_gem_dumb_map_gtt,
	.dumb_destroy = psb_gem_dumb_destroy,

	.fops = {
		 .owner = THIS_MODULE,
		 .open = drm_open,
		 .release = drm_release,
		 .unlocked_ioctl = psb_unlocked_ioctl,
		 .mmap = drm_gem_mmap,
		 .poll = drm_poll,
		 .fasync = drm_fasync,
		 .read = drm_read,
	 },
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = PSB_DRM_DRIVER_DATE,
	.major = PSB_DRM_DRIVER_MAJOR,
	.minor = PSB_DRM_DRIVER_MINOR,
	.patchlevel = PSB_DRM_DRIVER_PATCHLEVEL
};

static struct pci_driver psb_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.resume = gma_power_resume,
	.suspend = gma_power_suspend,
	.probe = psb_probe,
	.remove = psb_remove,
#ifdef CONFIG_PM
	.driver.pm = &psb_pm_ops,
#endif
};

static int psb_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	/* MLD Added this from Inaky's patch */
	if (pci_enable_msi(pdev))
		DRM_ERROR("Enable MSI failed!\n");
	return drm_get_pci_dev(pdev, ent, &driver);
}

static int __init psb_init(void)
{
	return drm_pci_init(&driver, &psb_pci_driver);
}

static void __exit psb_exit(void)
{
	drm_pci_exit(&driver, &psb_pci_driver);
}

late_initcall(psb_init);
module_exit(psb_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
