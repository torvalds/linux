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
 */

#ifndef __NOUVEAU_GPIO_H__
#define __NOUVEAU_GPIO_H__

struct gpio_func {
	u8 func;
	u8 line;
	u8 log[2];
};

/* nouveau_gpio.c */
int  nouveau_gpio_create(struct drm_device *);
void nouveau_gpio_destroy(struct drm_device *);
int  nouveau_gpio_init(struct drm_device *);
void nouveau_gpio_fini(struct drm_device *);
void nouveau_gpio_reset(struct drm_device *);
int  nouveau_gpio_drive(struct drm_device *, int idx, int line,
			int dir, int out);
int  nouveau_gpio_sense(struct drm_device *, int idx, int line);
int  nouveau_gpio_find(struct drm_device *, int idx, u8 tag, u8 line,
		       struct gpio_func *);
int  nouveau_gpio_set(struct drm_device *, int idx, u8 tag, u8 line, int state);
int  nouveau_gpio_get(struct drm_device *, int idx, u8 tag, u8 line);
int  nouveau_gpio_irq(struct drm_device *, int idx, u8 tag, u8 line, bool on);
void nouveau_gpio_isr(struct drm_device *, int idx, u32 mask);
int  nouveau_gpio_isr_add(struct drm_device *, int idx, u8 tag, u8 line,
			  void (*)(void *, int state), void *data);
void nouveau_gpio_isr_del(struct drm_device *, int idx, u8 tag, u8 line,
			  void (*)(void *, int state), void *data);

static inline bool
nouveau_gpio_func_valid(struct drm_device *dev, u8 tag)
{
	struct gpio_func func;
	return (nouveau_gpio_find(dev, 0, tag, 0xff, &func)) == 0;
}

static inline int
nouveau_gpio_func_set(struct drm_device *dev, u8 tag, int state)
{
	return nouveau_gpio_set(dev, 0, tag, 0xff, state);
}

static inline int
nouveau_gpio_func_get(struct drm_device *dev, u8 tag)
{
	return nouveau_gpio_get(dev, 0, tag, 0xff);
}

#endif
