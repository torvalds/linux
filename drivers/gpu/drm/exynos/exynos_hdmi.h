/*
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _EXYNOS_HDMI_H_
#define _EXYNOS_HDMI_H_

struct hdmi_conf {
	int width;
	int height;
	int vrefresh;
	bool interlace;
	const u8 *hdmiphy_data;
	const struct hdmi_preset_conf *conf;
};

struct hdmi_resources {
	struct clk *hdmi;
	struct clk *sclk_hdmi;
	struct clk *sclk_pixel;
	struct clk *sclk_hdmiphy;
	struct clk *hdmiphy;
	struct regulator_bulk_data *regul_bulk;
	int regul_count;
};

struct hdmi_context {
	struct device			*dev;
	struct drm_device		*drm_dev;
	struct fb_videomode		*default_timing;
	unsigned int			default_win;
	unsigned int			default_bpp;
	bool				hpd_handle;
	bool				enabled;

	struct resource			*regs_res;
	/** base address of HDMI registers */
	void __iomem *regs;
	/** HDMI hotplug interrupt */
	unsigned int irq;
	/** workqueue for delayed work */
	struct workqueue_struct *wq;
	/** hotplug handling work */
	struct work_struct hotplug_work;

	struct i2c_client *ddc_port;
	struct i2c_client *hdmiphy_port;

	/** current hdmiphy conf index */
	int cur_conf;
	/** other resources */
	struct hdmi_resources res;

	void *parent_ctx;
};


void hdmi_attach_ddc_client(struct i2c_client *ddc);
void hdmi_attach_hdmiphy_client(struct i2c_client *hdmiphy);

extern struct i2c_driver hdmiphy_driver;
extern struct i2c_driver ddc_driver;

#endif
