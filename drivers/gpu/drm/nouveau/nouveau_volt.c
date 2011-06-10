/*
 * Copyright 2010 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_pm.h"

static const enum dcb_gpio_tag vidtag[] = { 0x04, 0x05, 0x06, 0x1a, 0x73 };
static int nr_vidtag = sizeof(vidtag) / sizeof(vidtag[0]);

int
nouveau_voltage_gpio_get(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *gpio = &dev_priv->engine.gpio;
	struct nouveau_pm_voltage *volt = &dev_priv->engine.pm.voltage;
	u8 vid = 0;
	int i;

	for (i = 0; i < nr_vidtag; i++) {
		if (!(volt->vid_mask & (1 << i)))
			continue;

		vid |= gpio->get(dev, vidtag[i]) << i;
	}

	return nouveau_volt_lvl_lookup(dev, vid);
}

int
nouveau_voltage_gpio_set(struct drm_device *dev, int voltage)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *gpio = &dev_priv->engine.gpio;
	struct nouveau_pm_voltage *volt = &dev_priv->engine.pm.voltage;
	int vid, i;

	vid = nouveau_volt_vid_lookup(dev, voltage);
	if (vid < 0)
		return vid;

	for (i = 0; i < nr_vidtag; i++) {
		if (!(volt->vid_mask & (1 << i)))
			continue;

		gpio->set(dev, vidtag[i], !!(vid & (1 << i)));
	}

	return 0;
}

int
nouveau_volt_vid_lookup(struct drm_device *dev, int voltage)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_voltage *volt = &dev_priv->engine.pm.voltage;
	int i;

	for (i = 0; i < volt->nr_level; i++) {
		if (volt->level[i].voltage == voltage)
			return volt->level[i].vid;
	}

	return -ENOENT;
}

int
nouveau_volt_lvl_lookup(struct drm_device *dev, int vid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_voltage *volt = &dev_priv->engine.pm.voltage;
	int i;

	for (i = 0; i < volt->nr_level; i++) {
		if (volt->level[i].vid == vid)
			return volt->level[i].voltage;
	}

	return -ENOENT;
}

void
nouveau_volt_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct nouveau_pm_voltage *voltage = &pm->voltage;
	struct nvbios *bios = &dev_priv->vbios;
	struct bit_entry P;
	u8 *volt = NULL, *entry;
	int i, headerlen, recordlen, entries, vidmask, vidshift;

	if (bios->type == NVBIOS_BIT) {
		if (bit_table(dev, 'P', &P))
			return;

		if (P.version == 1)
			volt = ROMPTR(bios, P.data[16]);
		else
		if (P.version == 2)
			volt = ROMPTR(bios, P.data[12]);
		else {
			NV_WARN(dev, "unknown volt for BIT P %d\n", P.version);
		}
	} else {
		if (bios->data[bios->offset + 6] < 0x27) {
			NV_DEBUG(dev, "BMP version too old for voltage\n");
			return;
		}

		volt = ROMPTR(bios, bios->data[bios->offset + 0x98]);
	}

	if (!volt) {
		NV_DEBUG(dev, "voltage table pointer invalid\n");
		return;
	}

	switch (volt[0]) {
	case 0x10:
	case 0x11:
	case 0x12:
		headerlen = 5;
		recordlen = volt[1];
		entries   = volt[2];
		vidshift  = 0;
		vidmask   = volt[4];
		break;
	case 0x20:
		headerlen = volt[1];
		recordlen = volt[3];
		entries   = volt[2];
		vidshift  = 0; /* could be vidshift like 0x30? */
		vidmask   = volt[5];
		break;
	case 0x30:
		headerlen = volt[1];
		recordlen = volt[2];
		entries   = volt[3];
		vidmask   = volt[4];
		/* no longer certain what volt[5] is, if it's related to
		 * the vid shift then it's definitely not a function of
		 * how many bits are set.
		 *
		 * after looking at a number of nva3+ vbios images, they
		 * all seem likely to have a static shift of 2.. lets
		 * go with that for now until proven otherwise.
		 */
		vidshift  = 2;
		break;
	case 0x40:
		headerlen = volt[1];
		recordlen = volt[2];
		entries   = volt[3]; /* not a clue what the entries are for.. */
		vidmask   = volt[11]; /* guess.. */
		vidshift  = 0;
		break;
	default:
		NV_WARN(dev, "voltage table 0x%02x unknown\n", volt[0]);
		return;
	}

	/* validate vid mask */
	voltage->vid_mask = vidmask;
	if (!voltage->vid_mask)
		return;

	i = 0;
	while (vidmask) {
		if (i > nr_vidtag) {
			NV_DEBUG(dev, "vid bit %d unknown\n", i);
			return;
		}

		if (!nouveau_bios_gpio_entry(dev, vidtag[i])) {
			NV_DEBUG(dev, "vid bit %d has no gpio tag\n", i);
			return;
		}

		vidmask >>= 1;
		i++;
	}

	/* parse vbios entries into common format */
	voltage->version = volt[0];
	if (voltage->version < 0x40) {
		voltage->nr_level = entries;
		voltage->level =
			kcalloc(entries, sizeof(*voltage->level), GFP_KERNEL);
		if (!voltage->level)
			return;

		entry = volt + headerlen;
		for (i = 0; i < entries; i++, entry += recordlen) {
			voltage->level[i].voltage = entry[0] * 10000;
			voltage->level[i].vid     = entry[1] >> vidshift;
		}
	} else {
		u32 volt_uv = ROM32(volt[4]);
		s16 step_uv = ROM16(volt[8]);
		u8 vid;

		voltage->nr_level = voltage->vid_mask + 1;
		voltage->level = kcalloc(voltage->nr_level,
					 sizeof(*voltage->level), GFP_KERNEL);
		if (!voltage->level)
			return;

		for (vid = 0; vid <= voltage->vid_mask; vid++) {
			voltage->level[vid].voltage = volt_uv;
			voltage->level[vid].vid = vid;
			volt_uv += step_uv;
		}
	}

	voltage->supported = true;
}

void
nouveau_volt_fini(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_voltage *volt = &dev_priv->engine.pm.voltage;

	kfree(volt->level);
}
