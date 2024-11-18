/*
 * Copyright (C) 2016 Martin Peres
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

/*
 * Authors:
 *  Martin Peres <martin.peres@free.fr>
 */

#include <linux/leds.h>

#include "nouveau_led.h"
#include <nvkm/subdev/gpio.h>

static enum led_brightness
nouveau_led_get_brightness(struct led_classdev *led)
{
	struct drm_device *drm_dev = container_of(led, struct nouveau_led, led)->dev;
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
	struct nvif_object *device = &drm->client.device.object;
	u32 div, duty;

	div =  nvif_rd32(device, 0x61c880) & 0x00ffffff;
	duty = nvif_rd32(device, 0x61c884) & 0x00ffffff;

	if (div > 0)
		return duty * LED_FULL / div;
	else
		return 0;
}

static void
nouveau_led_set_brightness(struct led_classdev *led, enum led_brightness value)
{
	struct drm_device *drm_dev = container_of(led, struct nouveau_led, led)->dev;
	struct nouveau_drm *drm = nouveau_drm(drm_dev);
	struct nvif_object *device = &drm->client.device.object;

	u32 input_clk = 27e6; /* PDISPLAY.SOR[1].PWM is connected to the crystal */
	u32 freq = 100; /* this is what nvidia uses and it should be good-enough */
	u32 div, duty;

	div = input_clk / freq;
	duty = value * div / LED_FULL;

	/* for now, this is safe to directly poke those registers because:
	 *  - A: nvidia never puts the logo led to any other PWM controler
	 *       than PDISPLAY.SOR[1].PWM.
	 *  - B: nouveau does not touch these registers anywhere else
	 */
	nvif_wr32(device, 0x61c880, div);
	nvif_wr32(device, 0x61c884, 0xc0000000 | duty);
}


int
nouveau_led_init(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_gpio *gpio = nvxx_gpio(drm);
	struct dcb_gpio_func logo_led;
	int ret;

	if (!gpio)
		return 0;

	/* check that there is a GPIO controlling the logo LED */
	if (nvkm_gpio_find(gpio, 0, DCB_GPIO_LOGO_LED_PWM, 0xff, &logo_led))
		return 0;

	drm->led = kzalloc(sizeof(*drm->led), GFP_KERNEL);
	if (!drm->led)
		return -ENOMEM;
	drm->led->dev = dev;

	drm->led->led.name = "nvidia-logo";
	drm->led->led.max_brightness = 255;
	drm->led->led.brightness_get = nouveau_led_get_brightness;
	drm->led->led.brightness_set = nouveau_led_set_brightness;

	ret = led_classdev_register(dev->dev, &drm->led->led);
	if (ret) {
		kfree(drm->led);
		drm->led = NULL;
		return ret;
	}

	return 0;
}

void
nouveau_led_suspend(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (drm->led)
		led_classdev_suspend(&drm->led->led);
}

void
nouveau_led_resume(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (drm->led)
		led_classdev_resume(&drm->led->led);
}

void
nouveau_led_fini(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (drm->led) {
		led_classdev_unregister(&drm->led->led);
		kfree(drm->led);
		drm->led = NULL;
	}
}
