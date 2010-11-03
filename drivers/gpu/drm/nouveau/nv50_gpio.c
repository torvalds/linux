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
#include "nouveau_hw.h"

#include "nv50_display.h"

static void nv50_gpio_isr(struct drm_device *dev);

static int
nv50_gpio_location(struct dcb_gpio_entry *gpio, uint32_t *reg, uint32_t *shift)
{
	const uint32_t nv50_gpio_reg[4] = { 0xe104, 0xe108, 0xe280, 0xe284 };

	if (gpio->line >= 32)
		return -EINVAL;

	*reg = nv50_gpio_reg[gpio->line >> 3];
	*shift = (gpio->line & 7) << 2;
	return 0;
}

int
nv50_gpio_get(struct drm_device *dev, enum dcb_gpio_tag tag)
{
	struct dcb_gpio_entry *gpio;
	uint32_t r, s, v;

	gpio = nouveau_bios_gpio_entry(dev, tag);
	if (!gpio)
		return -ENOENT;

	if (nv50_gpio_location(gpio, &r, &s))
		return -EINVAL;

	v = nv_rd32(dev, r) >> (s + 2);
	return ((v & 1) == (gpio->state[1] & 1));
}

int
nv50_gpio_set(struct drm_device *dev, enum dcb_gpio_tag tag, int state)
{
	struct dcb_gpio_entry *gpio;
	uint32_t r, s, v;

	gpio = nouveau_bios_gpio_entry(dev, tag);
	if (!gpio)
		return -ENOENT;

	if (nv50_gpio_location(gpio, &r, &s))
		return -EINVAL;

	v  = nv_rd32(dev, r) & ~(0x3 << s);
	v |= (gpio->state[state] ^ 2) << s;
	nv_wr32(dev, r, v);
	return 0;
}

void
nv50_gpio_irq_enable(struct drm_device *dev, enum dcb_gpio_tag tag, bool on)
{
	struct dcb_gpio_entry *gpio;
	u32 reg, mask;

	gpio = nouveau_bios_gpio_entry(dev, tag);
	if (!gpio) {
		NV_ERROR(dev, "gpio tag 0x%02x not found\n", tag);
		return;
	}

	reg  = gpio->line < 16 ? 0xe050 : 0xe070;
	mask = 0x00010001 << (gpio->line & 0xf);

	nv_wr32(dev, reg + 4, mask);
	nv_mask(dev, reg + 0, mask, on ? mask : 0);
}

int
nv50_gpio_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	/* disable, and ack any pending gpio interrupts */
	nv_wr32(dev, 0xe050, 0x00000000);
	nv_wr32(dev, 0xe054, 0xffffffff);
	if (dev_priv->chipset >= 0x90) {
		nv_wr32(dev, 0xe070, 0x00000000);
		nv_wr32(dev, 0xe074, 0xffffffff);
	}

	INIT_WORK(&dev_priv->hpd_work, nv50_display_irq_hotplug_bh);
	spin_lock_init(&dev_priv->hpd_state.lock);
	nouveau_irq_register(dev, 21, nv50_gpio_isr);
	return 0;
}

void
nv50_gpio_fini(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	nv_wr32(dev, 0xe050, 0x00000000);
	if (dev_priv->chipset >= 0x90)
		nv_wr32(dev, 0xe070, 0x00000000);
	nouveau_irq_unregister(dev, 21);
}

static void
nv50_gpio_isr(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t hpd0_bits, hpd1_bits = 0;

	hpd0_bits = nv_rd32(dev, 0xe054);
	nv_wr32(dev, 0xe054, hpd0_bits);

	if (dev_priv->chipset >= 0x90) {
		hpd1_bits = nv_rd32(dev, 0xe074);
		nv_wr32(dev, 0xe074, hpd1_bits);
	}

	spin_lock(&dev_priv->hpd_state.lock);
	dev_priv->hpd_state.hpd0_bits |= hpd0_bits;
	dev_priv->hpd_state.hpd1_bits |= hpd1_bits;
	spin_unlock(&dev_priv->hpd_state.lock);

	queue_work(dev_priv->wq, &dev_priv->hpd_work);
}
