/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2012 ARM Limited
 */

#define pr_fmt(fmt) "vexpress-dvi: " fmt

#include <linux/fb.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/vexpress.h>


static struct vexpress_config_func *vexpress_dvimode_func;

static struct {
	u32 xres, yres, mode;
} vexpress_dvi_dvimodes[] = {
	{ 640, 480, 0 }, /* VGA */
	{ 800, 600, 1 }, /* SVGA */
	{ 1024, 768, 2 }, /* XGA */
	{ 1280, 1024, 3 }, /* SXGA */
	{ 1600, 1200, 4 }, /* UXGA */
	{ 1920, 1080, 5 }, /* HD1080 */
};

static void vexpress_dvi_mode_set(struct fb_info *info, u32 xres, u32 yres)
{
	int err = -ENOENT;
	int i;

	if (!vexpress_dvimode_func)
		return;

	for (i = 0; i < ARRAY_SIZE(vexpress_dvi_dvimodes); i++) {
		if (vexpress_dvi_dvimodes[i].xres == xres &&
				vexpress_dvi_dvimodes[i].yres == yres) {
			pr_debug("mode: %ux%u = %d\n", xres, yres,
					vexpress_dvi_dvimodes[i].mode);
			err = vexpress_config_write(vexpress_dvimode_func, 0,
					vexpress_dvi_dvimodes[i].mode);
			break;
		}
	}

	if (err)
		pr_warn("Failed to set %ux%u mode! (%d)\n", xres, yres, err);
}


static struct vexpress_config_func *vexpress_muxfpga_func;
static int vexpress_dvi_fb = -1;

static int vexpress_dvi_mux_set(struct fb_info *info)
{
	int err;
	u32 site = vexpress_get_site_by_dev(info->device);

	if (!vexpress_muxfpga_func)
		return -ENXIO;

	err = vexpress_config_write(vexpress_muxfpga_func, 0, site);
	if (!err) {
		pr_debug("Selected MUXFPGA input %d (fb%d)\n", site,
				info->node);
		vexpress_dvi_fb = info->node;
		vexpress_dvi_mode_set(info, info->var.xres,
				info->var.yres);
	} else {
		pr_warn("Failed to select MUXFPGA input %d (fb%d)! (%d)\n",
				site, info->node, err);
	}

	return err;
}

static int vexpress_dvi_fb_select(int fb)
{
	int err;
	struct fb_info *info;

	/* fb0 is the default */
	if (fb < 0)
		fb = 0;

	info = registered_fb[fb];
	if (!info || !lock_fb_info(info))
		return -ENODEV;

	err = vexpress_dvi_mux_set(info);

	unlock_fb_info(info);

	return err;
}

static ssize_t vexpress_dvi_fb_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", vexpress_dvi_fb);
}

static ssize_t vexpress_dvi_fb_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	long value;
	int err = kstrtol(buf, 0, &value);

	if (!err)
		err = vexpress_dvi_fb_select(value);

	return err ? err : count;
}

DEVICE_ATTR(fb, S_IRUGO | S_IWUSR, vexpress_dvi_fb_show,
		vexpress_dvi_fb_store);


static int vexpress_dvi_fb_event_notify(struct notifier_block *self,
			      unsigned long action, void *data)
{
	struct fb_event *event = data;
	struct fb_info *info = event->info;
	struct fb_videomode *mode = event->data;

	switch (action) {
	case FB_EVENT_FB_REGISTERED:
		if (vexpress_dvi_fb < 0)
			vexpress_dvi_mux_set(info);
		break;
	case FB_EVENT_MODE_CHANGE:
	case FB_EVENT_MODE_CHANGE_ALL:
		if (info->node == vexpress_dvi_fb)
			vexpress_dvi_mode_set(info, mode->xres, mode->yres);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block vexpress_dvi_fb_notifier = {
	.notifier_call = vexpress_dvi_fb_event_notify,
};
static bool vexpress_dvi_fb_notifier_registered;


enum vexpress_dvi_func { FUNC_MUXFPGA, FUNC_DVIMODE };

static struct of_device_id vexpress_dvi_of_match[] = {
	{
		.compatible = "arm,vexpress-muxfpga",
		.data = (void *)FUNC_MUXFPGA,
	}, {
		.compatible = "arm,vexpress-dvimode",
		.data = (void *)FUNC_DVIMODE,
	},
	{}
};

static int vexpress_dvi_probe(struct platform_device *pdev)
{
	enum vexpress_dvi_func func;
	const struct of_device_id *match =
			of_match_device(vexpress_dvi_of_match, &pdev->dev);

	if (match)
		func = (enum vexpress_dvi_func)match->data;
	else
		func = pdev->id_entry->driver_data;

	switch (func) {
	case FUNC_MUXFPGA:
		vexpress_muxfpga_func =
				vexpress_config_func_get_by_dev(&pdev->dev);
		device_create_file(&pdev->dev, &dev_attr_fb);
		break;
	case FUNC_DVIMODE:
		vexpress_dvimode_func =
				vexpress_config_func_get_by_dev(&pdev->dev);
		break;
	}

	if (!vexpress_dvi_fb_notifier_registered) {
		fb_register_client(&vexpress_dvi_fb_notifier);
		vexpress_dvi_fb_notifier_registered = true;
	}

	vexpress_dvi_fb_select(vexpress_dvi_fb);

	return 0;
}

static const struct platform_device_id vexpress_dvi_id_table[] = {
	{ .name = "vexpress-muxfpga", .driver_data = FUNC_MUXFPGA, },
	{ .name = "vexpress-dvimode", .driver_data = FUNC_DVIMODE, },
	{}
};

static struct platform_driver vexpress_dvi_driver = {
	.probe = vexpress_dvi_probe,
	.driver = {
		.name = "vexpress-dvi",
		.of_match_table = vexpress_dvi_of_match,
	},
	.id_table = vexpress_dvi_id_table,
};

static int __init vexpress_dvi_init(void)
{
	return platform_driver_register(&vexpress_dvi_driver);
}
device_initcall(vexpress_dvi_init);
