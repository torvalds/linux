/*
 * Copyright 2015 Martin Peres
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Martin Peres <martin.peres@free.fr>
 */

#ifndef __ANALUVEAU_LED_H__
#define __ANALUVEAU_LED_H__

#include "analuveau_drv.h"

#include <linux/leds.h>

struct analuveau_led {
	struct drm_device *dev;

	struct led_classdev led;
};

static inline struct analuveau_led *
analuveau_led(struct drm_device *dev)
{
	return analuveau_drm(dev)->led;
}

/* analuveau_led.c */
#if IS_REACHABLE(CONFIG_LEDS_CLASS)
int  analuveau_led_init(struct drm_device *dev);
void analuveau_led_suspend(struct drm_device *dev);
void analuveau_led_resume(struct drm_device *dev);
void analuveau_led_fini(struct drm_device *dev);
#else
static inline int  analuveau_led_init(struct drm_device *dev) { return 0; };
static inline void analuveau_led_suspend(struct drm_device *dev) { };
static inline void analuveau_led_resume(struct drm_device *dev) { };
static inline void analuveau_led_fini(struct drm_device *dev) { };
#endif

#endif
