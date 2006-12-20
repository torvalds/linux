/*
 * Backlight code for nVidia based graphic cards
 *
 * Copyright 2004 Antonino Daplas <adaplas@pol.net>
 * Copyright (c) 2006 Michael Hanselmann <linux-kernel@hansmi.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include "nv_local.h"
#include "nv_type.h"
#include "nv_proto.h"

#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#include <asm/machdep.h>
#endif

/* We do not have any information about which values are allowed, thus
 * we used safe values.
 */
#define MIN_LEVEL 0x158
#define MAX_LEVEL 0x534
#define LEVEL_STEP ((MAX_LEVEL - MIN_LEVEL) / FB_BACKLIGHT_MAX)

static struct backlight_properties nvidia_bl_data;

/* Call with fb_info->bl_mutex held */
static int nvidia_bl_get_level_brightness(struct nvidia_par *par,
		int level)
{
	struct fb_info *info = pci_get_drvdata(par->pci_dev);
	int nlevel;

	/* Get and convert the value */
	nlevel = MIN_LEVEL + info->bl_curve[level] * LEVEL_STEP;

	if (nlevel < 0)
		nlevel = 0;
	else if (nlevel < MIN_LEVEL)
		nlevel = MIN_LEVEL;
	else if (nlevel > MAX_LEVEL)
		nlevel = MAX_LEVEL;

	return nlevel;
}

/* Call with fb_info->bl_mutex held */
static int __nvidia_bl_update_status(struct backlight_device *bd)
{
	struct nvidia_par *par = class_get_devdata(&bd->class_dev);
	u32 tmp_pcrt, tmp_pmc, fpcontrol;
	int level;

	if (!par->FlatPanel)
		return 0;

	if (bd->props->power != FB_BLANK_UNBLANK ||
	    bd->props->fb_blank != FB_BLANK_UNBLANK)
		level = 0;
	else
		level = bd->props->brightness;

	tmp_pmc = NV_RD32(par->PMC, 0x10F0) & 0x0000FFFF;
	tmp_pcrt = NV_RD32(par->PCRTC0, 0x081C) & 0xFFFFFFFC;
	fpcontrol = NV_RD32(par->PRAMDAC, 0x0848) & 0xCFFFFFCC;

	if (level > 0) {
		tmp_pcrt |= 0x1;
		tmp_pmc |= (1 << 31); /* backlight bit */
		tmp_pmc |= nvidia_bl_get_level_brightness(par, level) << 16;
		fpcontrol |= par->fpSyncs;
	} else
		fpcontrol |= 0x20000022;

	NV_WR32(par->PCRTC0, 0x081C, tmp_pcrt);
	NV_WR32(par->PMC, 0x10F0, tmp_pmc);
	NV_WR32(par->PRAMDAC, 0x848, fpcontrol);

	return 0;
}

static int nvidia_bl_update_status(struct backlight_device *bd)
{
	struct nvidia_par *par = class_get_devdata(&bd->class_dev);
	struct fb_info *info = pci_get_drvdata(par->pci_dev);
	int ret;

	mutex_lock(&info->bl_mutex);
	ret = __nvidia_bl_update_status(bd);
	mutex_unlock(&info->bl_mutex);

	return ret;
}

static int nvidia_bl_get_brightness(struct backlight_device *bd)
{
	return bd->props->brightness;
}

static struct backlight_properties nvidia_bl_data = {
	.owner		= THIS_MODULE,
	.get_brightness = nvidia_bl_get_brightness,
	.update_status	= nvidia_bl_update_status,
	.max_brightness = (FB_BACKLIGHT_LEVELS - 1),
};

void nvidia_bl_set_power(struct fb_info *info, int power)
{
	mutex_lock(&info->bl_mutex);

	if (info->bl_dev) {
		down(&info->bl_dev->sem);
		info->bl_dev->props->power = power;
		__nvidia_bl_update_status(info->bl_dev);
		up(&info->bl_dev->sem);
	}

	mutex_unlock(&info->bl_mutex);
}

void nvidia_bl_init(struct nvidia_par *par)
{
	struct fb_info *info = pci_get_drvdata(par->pci_dev);
	struct backlight_device *bd;
	char name[12];

	if (!par->FlatPanel)
		return;

#ifdef CONFIG_PMAC_BACKLIGHT
	if (!machine_is(powermac) ||
	    !pmac_has_backlight_type("mnca"))
		return;
#endif

	snprintf(name, sizeof(name), "nvidiabl%d", info->node);

	bd = backlight_device_register(name, info->dev, par, &nvidia_bl_data);
	if (IS_ERR(bd)) {
		info->bl_dev = NULL;
		printk(KERN_WARNING "nvidia: Backlight registration failed\n");
		goto error;
	}

	mutex_lock(&info->bl_mutex);
	info->bl_dev = bd;
	fb_bl_default_curve(info, 0,
		0x158 * FB_BACKLIGHT_MAX / MAX_LEVEL,
		0x534 * FB_BACKLIGHT_MAX / MAX_LEVEL);
	mutex_unlock(&info->bl_mutex);

	down(&bd->sem);
	bd->props->brightness = nvidia_bl_data.max_brightness;
	bd->props->power = FB_BLANK_UNBLANK;
	bd->props->update_status(bd);
	up(&bd->sem);

#ifdef CONFIG_PMAC_BACKLIGHT
	mutex_lock(&pmac_backlight_mutex);
	if (!pmac_backlight)
		pmac_backlight = bd;
	mutex_unlock(&pmac_backlight_mutex);
#endif

	printk("nvidia: Backlight initialized (%s)\n", name);

	return;

error:
	return;
}

void nvidia_bl_exit(struct nvidia_par *par)
{
	struct fb_info *info = pci_get_drvdata(par->pci_dev);

#ifdef CONFIG_PMAC_BACKLIGHT
	mutex_lock(&pmac_backlight_mutex);
#endif

	mutex_lock(&info->bl_mutex);
	if (info->bl_dev) {
#ifdef CONFIG_PMAC_BACKLIGHT
		if (pmac_backlight == info->bl_dev)
			pmac_backlight = NULL;
#endif

		backlight_device_unregister(info->bl_dev);

		printk("nvidia: Backlight unloaded\n");
	}
	mutex_unlock(&info->bl_mutex);

#ifdef CONFIG_PMAC_BACKLIGHT
	mutex_unlock(&pmac_backlight_mutex);
#endif
}
