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
static void nv50_gpio_isr_bh(struct work_struct *work);

struct nv50_gpio_priv {
	struct list_head handlers;
	spinlock_t lock;
};

struct nv50_gpio_handler {
	struct drm_device *dev;
	struct list_head head;
	struct work_struct work;
	bool inhibit;

	struct dcb_gpio_entry *gpio;

	void (*handler)(void *data, int state);
	void *data;
};

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

int
nv50_gpio_irq_register(struct drm_device *dev, enum dcb_gpio_tag tag,
		       void (*handler)(void *, int), void *data)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	struct nv50_gpio_priv *priv = pgpio->priv;
	struct nv50_gpio_handler *gpioh;
	struct dcb_gpio_entry *gpio;
	unsigned long flags;

	gpio = nouveau_bios_gpio_entry(dev, tag);
	if (!gpio)
		return -ENOENT;

	gpioh = kzalloc(sizeof(*gpioh), GFP_KERNEL);
	if (!gpioh)
		return -ENOMEM;

	INIT_WORK(&gpioh->work, nv50_gpio_isr_bh);
	gpioh->dev  = dev;
	gpioh->gpio = gpio;
	gpioh->handler = handler;
	gpioh->data = data;

	spin_lock_irqsave(&priv->lock, flags);
	list_add(&gpioh->head, &priv->handlers);
	spin_unlock_irqrestore(&priv->lock, flags);
	return 0;
}

void
nv50_gpio_irq_unregister(struct drm_device *dev, enum dcb_gpio_tag tag,
			 void (*handler)(void *, int), void *data)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	struct nv50_gpio_priv *priv = pgpio->priv;
	struct nv50_gpio_handler *gpioh, *tmp;
	struct dcb_gpio_entry *gpio;
	unsigned long flags;

	gpio = nouveau_bios_gpio_entry(dev, tag);
	if (!gpio)
		return;

	spin_lock_irqsave(&priv->lock, flags);
	list_for_each_entry_safe(gpioh, tmp, &priv->handlers, head) {
		if (gpioh->gpio != gpio ||
		    gpioh->handler != handler ||
		    gpioh->data != data)
			continue;
		list_del(&gpioh->head);
		kfree(gpioh);
	}
	spin_unlock_irqrestore(&priv->lock, flags);
}

bool
nv50_gpio_irq_enable(struct drm_device *dev, enum dcb_gpio_tag tag, bool on)
{
	struct dcb_gpio_entry *gpio;
	u32 reg, mask;

	gpio = nouveau_bios_gpio_entry(dev, tag);
	if (!gpio)
		return false;

	reg  = gpio->line < 16 ? 0xe050 : 0xe070;
	mask = 0x00010001 << (gpio->line & 0xf);

	nv_wr32(dev, reg + 4, mask);
	reg = nv_mask(dev, reg + 0, mask, on ? mask : 0);
	return (reg & mask) == mask;
}

static int
nv50_gpio_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	struct nv50_gpio_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	INIT_LIST_HEAD(&priv->handlers);
	spin_lock_init(&priv->lock);
	pgpio->priv = priv;
	return 0;
}

static void
nv50_gpio_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;

	kfree(pgpio->priv);
	pgpio->priv = NULL;
}

int
nv50_gpio_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	struct nv50_gpio_priv *priv;
	int ret;

	if (!pgpio->priv) {
		ret = nv50_gpio_create(dev);
		if (ret)
			return ret;
	}
	priv = pgpio->priv;

	/* disable, and ack any pending gpio interrupts */
	nv_wr32(dev, 0xe050, 0x00000000);
	nv_wr32(dev, 0xe054, 0xffffffff);
	if (dev_priv->chipset >= 0x90) {
		nv_wr32(dev, 0xe070, 0x00000000);
		nv_wr32(dev, 0xe074, 0xffffffff);
	}

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

	nv50_gpio_destroy(dev);
}

static void
nv50_gpio_isr_bh(struct work_struct *work)
{
	struct nv50_gpio_handler *gpioh =
		container_of(work, struct nv50_gpio_handler, work);
	struct drm_nouveau_private *dev_priv = gpioh->dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	struct nv50_gpio_priv *priv = pgpio->priv;
	unsigned long flags;
	int state;

	state = pgpio->get(gpioh->dev, gpioh->gpio->tag);
	if (state < 0)
		return;

	gpioh->handler(gpioh->data, state);

	spin_lock_irqsave(&priv->lock, flags);
	gpioh->inhibit = false;
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void
nv50_gpio_isr(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	struct nv50_gpio_priv *priv = pgpio->priv;
	struct nv50_gpio_handler *gpioh;
	u32 intr0, intr1 = 0;
	u32 hi, lo, ch;

	intr0 = nv_rd32(dev, 0xe054) & nv_rd32(dev, 0xe050);
	if (dev_priv->chipset >= 0x90)
		intr1 = nv_rd32(dev, 0xe074) & nv_rd32(dev, 0xe070);

	hi = (intr0 & 0x0000ffff) | (intr1 << 16);
	lo = (intr0 >> 16) | (intr1 & 0xffff0000);
	ch = hi | lo;

	nv_wr32(dev, 0xe054, intr0);
	if (dev_priv->chipset >= 0x90)
		nv_wr32(dev, 0xe074, intr1);

	spin_lock(&priv->lock);
	list_for_each_entry(gpioh, &priv->handlers, head) {
		if (!(ch & (1 << gpioh->gpio->line)))
			continue;

		if (gpioh->inhibit)
			continue;
		gpioh->inhibit = true;

		queue_work(dev_priv->wq, &gpioh->work);
	}
	spin_unlock(&priv->lock);
}
