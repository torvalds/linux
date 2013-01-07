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

#include <subdev/gpio.h>
#include <subdev/bios.h>
#include <subdev/bios/gpio.h>

static int
nouveau_gpio_drive(struct nouveau_gpio *gpio,
		   int idx, int line, int dir, int out)
{
	return gpio->drive ? gpio->drive(gpio, line, dir, out) : -ENODEV;
}

static int
nouveau_gpio_sense(struct nouveau_gpio *gpio, int idx, int line)
{
	return gpio->sense ? gpio->sense(gpio, line) : -ENODEV;
}

static int
nouveau_gpio_find(struct nouveau_gpio *gpio, int idx, u8 tag, u8 line,
		  struct dcb_gpio_func *func)
{
	struct nouveau_bios *bios = nouveau_bios(gpio);
	u8  ver, len;
	u16 data;

	if (line == 0xff && tag == 0xff)
		return -EINVAL;

	data = dcb_gpio_match(bios, idx, tag, line, &ver, &len, func);
	if (data)
		return 0;

	/* Apple iMac G4 NV18 */
	if (nv_device_match(nv_object(gpio), 0x0189, 0x10de, 0x0010)) {
		if (tag == DCB_GPIO_TVDAC0) {
			*func = (struct dcb_gpio_func) {
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

static int
nouveau_gpio_set(struct nouveau_gpio *gpio, int idx, u8 tag, u8 line, int state)
{
	struct dcb_gpio_func func;
	int ret;

	ret = nouveau_gpio_find(gpio, idx, tag, line, &func);
	if (ret == 0) {
		int dir = !!(func.log[state] & 0x02);
		int out = !!(func.log[state] & 0x01);
		ret = nouveau_gpio_drive(gpio, idx, func.line, dir, out);
	}

	return ret;
}

static int
nouveau_gpio_get(struct nouveau_gpio *gpio, int idx, u8 tag, u8 line)
{
	struct dcb_gpio_func func;
	int ret;

	ret = nouveau_gpio_find(gpio, idx, tag, line, &func);
	if (ret == 0) {
		ret = nouveau_gpio_sense(gpio, idx, func.line);
		if (ret >= 0)
			ret = (ret == (func.log[1] & 1));
	}

	return ret;
}

static int
nouveau_gpio_irq(struct nouveau_gpio *gpio, int idx, u8 tag, u8 line, bool on)
{
	struct dcb_gpio_func func;
	int ret;

	ret = nouveau_gpio_find(gpio, idx, tag, line, &func);
	if (ret == 0) {
		if (idx == 0 && gpio->irq_enable)
			gpio->irq_enable(gpio, func.line, on);
		else
			ret = -ENODEV;
	}

	return ret;
}

struct gpio_isr {
	struct nouveau_gpio *gpio;
	struct list_head head;
	struct work_struct work;
	int idx;
	struct dcb_gpio_func func;
	void (*handler)(void *, int);
	void *data;
	bool inhibit;
};

static void
nouveau_gpio_isr_bh(struct work_struct *work)
{
	struct gpio_isr *isr = container_of(work, struct gpio_isr, work);
	struct nouveau_gpio *gpio = isr->gpio;
	unsigned long flags;
	int state;

	state = nouveau_gpio_get(gpio, isr->idx, isr->func.func,
						 isr->func.line);
	if (state >= 0)
		isr->handler(isr->data, state);

	spin_lock_irqsave(&gpio->lock, flags);
	isr->inhibit = false;
	spin_unlock_irqrestore(&gpio->lock, flags);
}

static void
nouveau_gpio_isr_run(struct nouveau_gpio *gpio, int idx, u32 line_mask)
{
	struct gpio_isr *isr;

	if (idx != 0)
		return;

	spin_lock(&gpio->lock);
	list_for_each_entry(isr, &gpio->isr, head) {
		if (line_mask & (1 << isr->func.line)) {
			if (isr->inhibit)
				continue;
			isr->inhibit = true;
			schedule_work(&isr->work);
		}
	}
	spin_unlock(&gpio->lock);
}

static int
nouveau_gpio_isr_add(struct nouveau_gpio *gpio, int idx, u8 tag, u8 line,
		     void (*handler)(void *, int), void *data)
{
	struct gpio_isr *isr;
	unsigned long flags;
	int ret;

	isr = kzalloc(sizeof(*isr), GFP_KERNEL);
	if (!isr)
		return -ENOMEM;

	ret = nouveau_gpio_find(gpio, idx, tag, line, &isr->func);
	if (ret) {
		kfree(isr);
		return ret;
	}

	INIT_WORK(&isr->work, nouveau_gpio_isr_bh);
	isr->gpio = gpio;
	isr->handler = handler;
	isr->data = data;
	isr->idx = idx;

	spin_lock_irqsave(&gpio->lock, flags);
	list_add(&isr->head, &gpio->isr);
	spin_unlock_irqrestore(&gpio->lock, flags);
	return 0;
}

static void
nouveau_gpio_isr_del(struct nouveau_gpio *gpio, int idx, u8 tag, u8 line,
		     void (*handler)(void *, int), void *data)
{
	struct gpio_isr *isr, *tmp;
	struct dcb_gpio_func func;
	unsigned long flags;
	LIST_HEAD(tofree);
	int ret;

	ret = nouveau_gpio_find(gpio, idx, tag, line, &func);
	if (ret == 0) {
		spin_lock_irqsave(&gpio->lock, flags);
		list_for_each_entry_safe(isr, tmp, &gpio->isr, head) {
			if (memcmp(&isr->func, &func, sizeof(func)) ||
			    isr->idx != idx ||
			    isr->handler != handler || isr->data != data)
				continue;
			list_move_tail(&isr->head, &tofree);
		}
		spin_unlock_irqrestore(&gpio->lock, flags);

		list_for_each_entry_safe(isr, tmp, &tofree, head) {
			flush_work(&isr->work);
			kfree(isr);
		}
	}
}

int
nouveau_gpio_create_(struct nouveau_object *parent,
		     struct nouveau_object *engine,
		     struct nouveau_oclass *oclass, int length, void **pobject)
{
	struct nouveau_gpio *gpio;
	int ret;

	ret = nouveau_subdev_create_(parent, engine, oclass, 0, "GPIO", "gpio",
				     length, pobject);
	gpio = *pobject;
	if (ret)
		return ret;

	gpio->find = nouveau_gpio_find;
	gpio->set  = nouveau_gpio_set;
	gpio->get  = nouveau_gpio_get;
	gpio->irq  = nouveau_gpio_irq;
	gpio->isr_run = nouveau_gpio_isr_run;
	gpio->isr_add = nouveau_gpio_isr_add;
	gpio->isr_del = nouveau_gpio_isr_del;
	INIT_LIST_HEAD(&gpio->isr);
	spin_lock_init(&gpio->lock);
	return 0;
}

static struct dmi_system_id gpio_reset_ids[] = {
	{
		.ident = "Apple Macbook 10,1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro10,1"),
		}
	},
	{ }
};

int
nouveau_gpio_init(struct nouveau_gpio *gpio)
{
	int ret = nouveau_subdev_init(&gpio->base);
	if (ret == 0 && gpio->reset) {
		if (dmi_check_system(gpio_reset_ids))
			gpio->reset(gpio, DCB_GPIO_UNUSED);
	}
	return ret;
}
