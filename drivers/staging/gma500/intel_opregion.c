/*
 * Copyright 2010 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * FIXME: resolve with the i915 version
 */

#include "psb_drv.h"

struct opregion_header {
	u8 signature[16];
	u32 size;
	u32 opregion_ver;
	u8 bios_ver[32];
	u8 vbios_ver[16];
	u8 driver_ver[16];
	u32 mboxes;
	u8 reserved[164];
} __packed;

struct opregion_apci {
	/*FIXME: add it later*/
} __packed;

struct opregion_swsci {
	/*FIXME: add it later*/
} __packed;

struct opregion_acpi {
	/*FIXME: add it later*/
} __packed;

int gma_intel_opregion_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 opregion_phy;
	void *base;
	u32 *lid_state;

	dev_priv->lid_state = NULL;

	pci_read_config_dword(dev->pdev, 0xfc, &opregion_phy);
	if (opregion_phy == 0)
		return -ENOTSUPP;

	base = ioremap(opregion_phy, 8*1024);
	if (!base)
		return -ENOMEM;

	lid_state = base + 0x01ac;

	dev_priv->lid_state = lid_state;
	dev_priv->lid_last_state = readl(lid_state);
	return 0;
}

int gma_intel_opregion_exit(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	if (dev_priv->lid_state)
		iounmap(dev_priv->lid_state);
	return 0;
}
