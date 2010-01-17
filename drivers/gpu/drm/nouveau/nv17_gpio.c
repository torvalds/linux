/*
 * Copyright (C) 2009 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_hw.h"

static bool
get_gpio_location(struct dcb_gpio_entry *ent, uint32_t *reg, uint32_t *shift,
		  uint32_t *mask)
{
	if (ent->line < 2) {
		*reg = NV_PCRTC_GPIO;
		*shift = ent->line * 16;
		*mask = 0x11;

	} else if (ent->line < 10) {
		*reg = NV_PCRTC_GPIO_EXT;
		*shift = (ent->line - 2) * 4;
		*mask = 0x3;

	} else if (ent->line < 14) {
		*reg = NV_PCRTC_850;
		*shift = (ent->line - 10) * 4;
		*mask = 0x3;

	} else {
		return false;
	}

	return true;
}

int
nv17_gpio_get(struct drm_device *dev, enum dcb_gpio_tag tag)
{
	struct dcb_gpio_entry *ent = nouveau_bios_gpio_entry(dev, tag);
	uint32_t reg, shift, mask, value;

	if (!ent)
		return -ENODEV;

	if (!get_gpio_location(ent, &reg, &shift, &mask))
		return -ENODEV;

	value = NVReadCRTC(dev, 0, reg) >> shift;

	return (ent->invert ? 1 : 0) ^ (value & 1);
}

int
nv17_gpio_set(struct drm_device *dev, enum dcb_gpio_tag tag, int state)
{
	struct dcb_gpio_entry *ent = nouveau_bios_gpio_entry(dev, tag);
	uint32_t reg, shift, mask, value;

	if (!ent)
		return -ENODEV;

	if (!get_gpio_location(ent, &reg, &shift, &mask))
		return -ENODEV;

	value = ((ent->invert ? 1 : 0) ^ (state ? 1 : 0)) << shift;
	mask = ~(mask << shift);

	NVWriteCRTC(dev, 0, reg, value | (NVReadCRTC(dev, 0, reg) & mask));

	return 0;
}
