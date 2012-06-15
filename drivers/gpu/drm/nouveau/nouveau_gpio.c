/*
 * Copyright 2011 Red Hat Inc.
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
#include "nouveau_i2c.h"
#include "nouveau_gpio.h"

static u8 *
dcb_gpio_table(struct drm_device *dev)
{
	u8 *dcb = dcb_table(dev);
	if (dcb) {
		if (dcb[0] >= 0x30 && dcb[1] >= 0x0c)
			return ROMPTR(dev, dcb[0x0a]);
		if (dcb[0] >= 0x22 && dcb[-1] >= 0x13)
			return ROMPTR(dev, dcb[-15]);
	}
	return NULL;
}

static u8 *
dcb_gpio_entry(struct drm_device *dev, int idx, int ent, u8 *version)
{
	u8 *table = dcb_gpio_table(dev);
	if (table) {
		*version = table[0];
		if (*version < 0x30 && ent < table[2])
			return table + 3 + (ent * table[1]);
		else if (ent < table[2])
			return table + table[1] + (ent * table[3]);
	}
	return NULL;
}

int
nouveau_gpio_drive(struct drm_device *dev, int idx, int line, int dir, int out)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;

	return pgpio->drive ? pgpio->drive(dev, line, dir, out) : -ENODEV;
}

int
nouveau_gpio_sense(struct drm_device *dev, int idx, int line)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;

	return pgpio->sense ? pgpio->sense(dev, line) : -ENODEV;
}

int
nouveau_gpio_find(struct drm_device *dev, int idx, u8 func, u8 line,
		  struct gpio_func *gpio)
{
	u8 *table, *entry, version;
	int i = -1;

	if (line == 0xff && func == 0xff)
		return -EINVAL;

	while ((entry = dcb_gpio_entry(dev, idx, ++i, &version))) {
		if (version < 0x40) {
			u16 data = ROM16(entry[0]);
			*gpio = (struct gpio_func) {
				.line = (data & 0x001f) >> 0,
				.func = (data & 0x07e0) >> 5,
				.log[0] = (data & 0x1800) >> 11,
				.log[1] = (data & 0x6000) >> 13,
			};
		} else
		if (version < 0x41) {
			*gpio = (struct gpio_func) {
				.line = entry[0] & 0x1f,
				.func = entry[1],
				.log[0] = (entry[3] & 0x18) >> 3,
				.log[1] = (entry[3] & 0x60) >> 5,
			};
		} else {
			*gpio = (struct gpio_func) {
				.line = entry[0] & 0x3f,
				.func = entry[1],
				.log[0] = (entry[4] & 0x30) >> 4,
				.log[1] = (entry[4] & 0xc0) >> 6,
			};
		}

		if ((line == 0xff || line == gpio->line) &&
		    (func == 0xff || func == gpio->func))
			return 0;
	}

	/* DCB 2.2, fixed TVDAC GPIO data */
	if ((table = dcb_table(dev)) && table[0] >= 0x22) {
		if (func == DCB_GPIO_TVDAC0) {
			*gpio = (struct gpio_func) {
				.func = DCB_GPIO_TVDAC0,
				.line = table[-4] >> 4,
				.log[0] = !!(table[-5] & 2),
				.log[1] =  !(table[-5] & 2),
			};
			return 0;
		}
	}

	/* Apple iMac G4 NV18 */
	if (nv_match_device(dev, 0x0189, 0x10de, 0x0010)) {
		if (func == DCB_GPIO_TVDAC0) {
			*gpio = (struct gpio_func) {
				.func = DCB_GPIO_TVDAC0,
				.line = 4,
				.log[0] = 0,
				.log[1] = 1,
			};
			return 0;
		}
	}

	return -EINVAL;
}

int
nouveau_gpio_set(struct drm_device *dev, int idx, u8 tag, u8 line, int state)
{
	struct gpio_func gpio;
	int ret;

	ret = nouveau_gpio_find(dev, idx, tag, line, &gpio);
	if (ret == 0) {
		int dir = !!(gpio.log[state] & 0x02);
		int out = !!(gpio.log[state] & 0x01);
		ret = nouveau_gpio_drive(dev, idx, gpio.line, dir, out);
	}

	return ret;
}

int
nouveau_gpio_get(struct drm_device *dev, int idx, u8 tag, u8 line)
{
	struct gpio_func gpio;
	int ret;

	ret = nouveau_gpio_find(dev, idx, tag, line, &gpio);
	if (ret == 0) {
		ret = nouveau_gpio_sense(dev, idx, gpio.line);
		if (ret >= 0)
			ret = (ret == (gpio.log[1] & 1));
	}

	return ret;
}

int
nouveau_gpio_irq(struct drm_device *dev, int idx, u8 tag, u8 line, bool on)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	struct gpio_func gpio;
	int ret;

	ret = nouveau_gpio_find(dev, idx, tag, line, &gpio);
	if (ret == 0) {
		if (idx == 0 && pgpio->irq_enable)
			pgpio->irq_enable(dev, gpio.line, on);
		else
			ret = -ENODEV;
	}

	return ret;
}

struct gpio_isr {
	struct drm_device *dev;
	struct list_head head;
	struct work_struct work;
	int idx;
	struct gpio_func func;
	void (*handler)(void *, int);
	void *data;
	bool inhibit;
};

static void
nouveau_gpio_isr_bh(struct work_struct *work)
{
	struct gpio_isr *isr = container_of(work, struct gpio_isr, work);
	struct drm_device *dev = isr->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	unsigned long flags;
	int state;

	state = nouveau_gpio_get(dev, isr->idx, isr->func.func, isr->func.line);
	if (state >= 0)
		isr->handler(isr->data, state);

	spin_lock_irqsave(&pgpio->lock, flags);
	isr->inhibit = false;
	spin_unlock_irqrestore(&pgpio->lock, flags);
}

void
nouveau_gpio_isr(struct drm_device *dev, int idx, u32 line_mask)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	struct gpio_isr *isr;

	if (idx != 0)
		return;

	spin_lock(&pgpio->lock);
	list_for_each_entry(isr, &pgpio->isr, head) {
		if (line_mask & (1 << isr->func.line)) {
			if (isr->inhibit)
				continue;
			isr->inhibit = true;
			schedule_work(&isr->work);
		}
	}
	spin_unlock(&pgpio->lock);
}

int
nouveau_gpio_isr_add(struct drm_device *dev, int idx, u8 tag, u8 line,
		     void (*handler)(void *, int), void *data)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	struct gpio_isr *isr;
	unsigned long flags;
	int ret;

	isr = kzalloc(sizeof(*isr), GFP_KERNEL);
	if (!isr)
		return -ENOMEM;

	ret = nouveau_gpio_find(dev, idx, tag, line, &isr->func);
	if (ret) {
		kfree(isr);
		return ret;
	}

	INIT_WORK(&isr->work, nouveau_gpio_isr_bh);
	isr->dev = dev;
	isr->handler = handler;
	isr->data = data;
	isr->idx = idx;

	spin_lock_irqsave(&pgpio->lock, flags);
	list_add(&isr->head, &pgpio->isr);
	spin_unlock_irqrestore(&pgpio->lock, flags);
	return 0;
}

void
nouveau_gpio_isr_del(struct drm_device *dev, int idx, u8 tag, u8 line,
		     void (*handler)(void *, int), void *data)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	struct gpio_isr *isr, *tmp;
	struct gpio_func func;
	unsigned long flags;
	LIST_HEAD(tofree);
	int ret;

	ret = nouveau_gpio_find(dev, idx, tag, line, &func);
	if (ret == 0) {
		spin_lock_irqsave(&pgpio->lock, flags);
		list_for_each_entry_safe(isr, tmp, &pgpio->isr, head) {
			if (memcmp(&isr->func, &func, sizeof(func)) ||
			    isr->idx != idx ||
			    isr->handler != handler || isr->data != data)
				continue;
			list_move(&isr->head, &tofree);
		}
		spin_unlock_irqrestore(&pgpio->lock, flags);

		list_for_each_entry_safe(isr, tmp, &tofree, head) {
			flush_work_sync(&isr->work);
			kfree(isr);
		}
	}
}

int
nouveau_gpio_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;

	INIT_LIST_HEAD(&pgpio->isr);
	spin_lock_init(&pgpio->lock);

	return nouveau_gpio_init(dev);
}

void
nouveau_gpio_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;

	nouveau_gpio_fini(dev);
	BUG_ON(!list_empty(&pgpio->isr));
}

int
nouveau_gpio_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	int ret = 0;

	if (pgpio->init)
		ret = pgpio->init(dev);

	return ret;
}

void
nouveau_gpio_fini(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;

	if (pgpio->fini)
		pgpio->fini(dev);
}

void
nouveau_gpio_reset(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u8 *entry, version;
	int ent = -1;

	while ((entry = dcb_gpio_entry(dev, 0, ++ent, &version))) {
		u8 func = 0xff, line, defs, unk0, unk1;
		if (version >= 0x41) {
			defs = !!(entry[0] & 0x80);
			line = entry[0] & 0x3f;
			func = entry[1];
			unk0 = entry[2];
			unk1 = entry[3] & 0x1f;
		} else
		if (version >= 0x40) {
			line = entry[0] & 0x1f;
			func = entry[1];
			defs = !!(entry[3] & 0x01);
			unk0 = !!(entry[3] & 0x02);
			unk1 = !!(entry[3] & 0x04);
		} else {
			break;
		}

		if (func == 0xff)
			continue;

		nouveau_gpio_func_set(dev, func, defs);

		if (dev_priv->card_type >= NV_D0) {
			nv_mask(dev, 0x00d610 + (line * 4), 0xff, unk0);
			if (unk1--)
				nv_mask(dev, 0x00d740 + (unk1 * 4), 0xff, line);
		} else
		if (dev_priv->card_type >= NV_50) {
			static const u32 regs[] = { 0xe100, 0xe28c };
			u32 val = (unk1 << 16) | unk0;
			u32 reg = regs[line >> 4]; line &= 0x0f;

			nv_mask(dev, reg, 0x00010001 << line, val << line);
		}
	}
}
