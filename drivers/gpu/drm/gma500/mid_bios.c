/**************************************************************************
 * Copyright (c) 2011, Intel Corporation.
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

/* TODO
 * - Split functions by vbt type
 * - Make them all take drm_device
 * - Check ioremap failures
 */

#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/gma_drm.h>
#include "psb_drv.h"
#include "mid_bios.h"

static void mid_get_fuse_settings(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct pci_dev *pci_root = pci_get_bus_and_slot(0, 0);
	uint32_t fuse_value = 0;
	uint32_t fuse_value_tmp = 0;

#define FB_REG06 0xD0810600
#define FB_MIPI_DISABLE  (1 << 11)
#define FB_REG09 0xD0810900
#define FB_SKU_MASK  0x7000
#define FB_SKU_SHIFT 12
#define FB_SKU_100 0
#define FB_SKU_100L 1
#define FB_SKU_83 2
	if (pci_root == NULL) {
		WARN_ON(1);
		return;
	}


	pci_write_config_dword(pci_root, 0xD0, FB_REG06);
	pci_read_config_dword(pci_root, 0xD4, &fuse_value);

	/* FB_MIPI_DISABLE doesn't mean LVDS on with Medfield */
	if (IS_MRST(dev))
		dev_priv->iLVDS_enable = fuse_value & FB_MIPI_DISABLE;

	DRM_INFO("internal display is %s\n",
		 dev_priv->iLVDS_enable ? "LVDS display" : "MIPI display");

	 /* Prevent runtime suspend at start*/
	 if (dev_priv->iLVDS_enable) {
		dev_priv->is_lvds_on = true;
		dev_priv->is_mipi_on = false;
	} else {
		dev_priv->is_mipi_on = true;
		dev_priv->is_lvds_on = false;
	}

	dev_priv->video_device_fuse = fuse_value;

	pci_write_config_dword(pci_root, 0xD0, FB_REG09);
	pci_read_config_dword(pci_root, 0xD4, &fuse_value);

	dev_dbg(dev->dev, "SKU values is 0x%x.\n", fuse_value);
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
		dev_warn(dev->dev, "Invalid SKU values, SKU value = 0x%08x\n",
								fuse_value_tmp);
		dev_priv->core_freq = 0;
	}
	dev_dbg(dev->dev, "LNC core clk is %dMHz.\n", dev_priv->core_freq);
	pci_dev_put(pci_root);
}

/*
 *	Get the revison ID, B0:D2:F0;0x08
 */
static void mid_get_pci_revID(struct drm_psb_private *dev_priv)
{
	uint32_t platform_rev_id = 0;
	struct pci_dev *pci_gfx_root = pci_get_bus_and_slot(0, PCI_DEVFN(2, 0));

	if (pci_gfx_root == NULL) {
		WARN_ON(1);
		return;
	}
	pci_read_config_dword(pci_gfx_root, 0x08, &platform_rev_id);
	dev_priv->platform_rev_id = (uint8_t) platform_rev_id;
	pci_dev_put(pci_gfx_root);
	dev_dbg(dev_priv->dev->dev, "platform_rev_id is %x\n",
					dev_priv->platform_rev_id);
}

struct mid_vbt_header {
	u32 signature;
	u8 revision;
} __packed;

/* The same for r0 and r1 */
struct vbt_r0 {
	struct mid_vbt_header vbt_header;
	u8 size;
	u8 checksum;
} __packed;

struct vbt_r10 {
	struct mid_vbt_header vbt_header;
	u8 checksum;
	u16 size;
	u8 panel_count;
	u8 primary_panel_idx;
	u8 secondary_panel_idx;
	u8 __reserved[5];
} __packed;

static int read_vbt_r0(u32 addr, struct vbt_r0 *vbt)
{
	void __iomem *vbt_virtual;

	vbt_virtual = ioremap(addr, sizeof(*vbt));
	if (vbt_virtual == NULL)
		return -1;

	memcpy_fromio(vbt, vbt_virtual, sizeof(*vbt));
	iounmap(vbt_virtual);

	return 0;
}

static int read_vbt_r10(u32 addr, struct vbt_r10 *vbt)
{
	void __iomem *vbt_virtual;

	vbt_virtual = ioremap(addr, sizeof(*vbt));
	if (!vbt_virtual)
		return -1;

	memcpy_fromio(vbt, vbt_virtual, sizeof(*vbt));
	iounmap(vbt_virtual);

	return 0;
}

static int mid_get_vbt_data_r0(struct drm_psb_private *dev_priv, u32 addr)
{
	struct vbt_r0 vbt;
	void __iomem *gct_virtual;
	struct gct_r0 gct;
	u8 bpi;

	if (read_vbt_r0(addr, &vbt))
		return -1;

	gct_virtual = ioremap(addr + sizeof(vbt), vbt.size - sizeof(vbt));
	if (!gct_virtual)
		return -1;
	memcpy_fromio(&gct, gct_virtual, sizeof(gct));
	iounmap(gct_virtual);

	bpi = gct.PD.BootPanelIndex;
	dev_priv->gct_data.bpi = bpi;
	dev_priv->gct_data.pt = gct.PD.PanelType;
	dev_priv->gct_data.DTD = gct.panel[bpi].DTD;
	dev_priv->gct_data.Panel_Port_Control =
		gct.panel[bpi].Panel_Port_Control;
	dev_priv->gct_data.Panel_MIPI_Display_Descriptor =
		gct.panel[bpi].Panel_MIPI_Display_Descriptor;

	return 0;
}

static int mid_get_vbt_data_r1(struct drm_psb_private *dev_priv, u32 addr)
{
	struct vbt_r0 vbt;
	void __iomem *gct_virtual;
	struct gct_r1 gct;
	u8 bpi;

	if (read_vbt_r0(addr, &vbt))
		return -1;

	gct_virtual = ioremap(addr + sizeof(vbt), vbt.size - sizeof(vbt));
	if (!gct_virtual)
		return -1;
	memcpy_fromio(&gct, gct_virtual, sizeof(gct));
	iounmap(gct_virtual);

	bpi = gct.PD.BootPanelIndex;
	dev_priv->gct_data.bpi = bpi;
	dev_priv->gct_data.pt = gct.PD.PanelType;
	dev_priv->gct_data.DTD = gct.panel[bpi].DTD;
	dev_priv->gct_data.Panel_Port_Control =
		gct.panel[bpi].Panel_Port_Control;
	dev_priv->gct_data.Panel_MIPI_Display_Descriptor =
		gct.panel[bpi].Panel_MIPI_Display_Descriptor;

	return 0;
}

static int mid_get_vbt_data_r10(struct drm_psb_private *dev_priv, u32 addr)
{
	struct vbt_r10 vbt;
	void __iomem *gct_virtual;
	struct gct_r10 *gct;
	struct oaktrail_timing_info *dp_ti = &dev_priv->gct_data.DTD;
	struct gct_r10_timing_info *ti;
	int ret = -1;

	if (read_vbt_r10(addr, &vbt))
		return -1;

	gct = kmalloc(sizeof(*gct) * vbt.panel_count, GFP_KERNEL);
	if (!gct)
		return -1;

	gct_virtual = ioremap(addr + sizeof(vbt),
			sizeof(*gct) * vbt.panel_count);
	if (!gct_virtual)
		goto out;
	memcpy_fromio(gct, gct_virtual, sizeof(*gct));
	iounmap(gct_virtual);

	dev_priv->gct_data.bpi = vbt.primary_panel_idx;
	dev_priv->gct_data.Panel_MIPI_Display_Descriptor =
		gct[vbt.primary_panel_idx].Panel_MIPI_Display_Descriptor;

	ti = &gct[vbt.primary_panel_idx].DTD;
	dp_ti->pixel_clock = ti->pixel_clock;
	dp_ti->hactive_hi = ti->hactive_hi;
	dp_ti->hactive_lo = ti->hactive_lo;
	dp_ti->hblank_hi = ti->hblank_hi;
	dp_ti->hblank_lo = ti->hblank_lo;
	dp_ti->hsync_offset_hi = ti->hsync_offset_hi;
	dp_ti->hsync_offset_lo = ti->hsync_offset_lo;
	dp_ti->hsync_pulse_width_hi = ti->hsync_pulse_width_hi;
	dp_ti->hsync_pulse_width_lo = ti->hsync_pulse_width_lo;
	dp_ti->vactive_hi = ti->vactive_hi;
	dp_ti->vactive_lo = ti->vactive_lo;
	dp_ti->vblank_hi = ti->vblank_hi;
	dp_ti->vblank_lo = ti->vblank_lo;
	dp_ti->vsync_offset_hi = ti->vsync_offset_hi;
	dp_ti->vsync_offset_lo = ti->vsync_offset_lo;
	dp_ti->vsync_pulse_width_hi = ti->vsync_pulse_width_hi;
	dp_ti->vsync_pulse_width_lo = ti->vsync_pulse_width_lo;

	ret = 0;
out:
	kfree(gct);
	return ret;
}

static void mid_get_vbt_data(struct drm_psb_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	u32 addr;
	u8 __iomem *vbt_virtual;
	struct mid_vbt_header vbt_header;
	struct pci_dev *pci_gfx_root = pci_get_bus_and_slot(0, PCI_DEVFN(2, 0));
	int ret = -1;

	/* Get the address of the platform config vbt */
	pci_read_config_dword(pci_gfx_root, 0xFC, &addr);
	pci_dev_put(pci_gfx_root);

	dev_dbg(dev->dev, "drm platform config address is %x\n", addr);

	if (!addr)
		goto out;

	/* get the virtual address of the vbt */
	vbt_virtual = ioremap(addr, sizeof(vbt_header));
	if (!vbt_virtual)
		goto out;

	memcpy_fromio(&vbt_header, vbt_virtual, sizeof(vbt_header));
	iounmap(vbt_virtual);

	if (memcmp(&vbt_header.signature, "$GCT", 4))
		goto out;

	dev_dbg(dev->dev, "GCT revision is %02x\n", vbt_header.revision);

	switch (vbt_header.revision) {
	case 0x00:
		ret = mid_get_vbt_data_r0(dev_priv, addr);
		break;
	case 0x01:
		ret = mid_get_vbt_data_r1(dev_priv, addr);
		break;
	case 0x10:
		ret = mid_get_vbt_data_r10(dev_priv, addr);
		break;
	default:
		dev_err(dev->dev, "Unknown revision of GCT!\n");
	}

out:
	if (ret)
		dev_err(dev->dev, "Unable to read GCT!");
	else
		dev_priv->has_gct = true;
}

int mid_chip_setup(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	mid_get_fuse_settings(dev);
	mid_get_vbt_data(dev_priv);
	mid_get_pci_revID(dev_priv);
	return 0;
}
