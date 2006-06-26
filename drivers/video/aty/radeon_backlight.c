/*
 * Backlight code for ATI Radeon based graphic cards
 *
 * Copyright (c) 2000 Ani Joshi <ajoshi@kernel.crashing.org>
 * Copyright (c) 2003 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 * Copyright (c) 2006 Michael Hanselmann <linux-kernel@hansmi.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "radeonfb.h"
#include <linux/backlight.h>

#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif

#define MAX_RADEON_LEVEL 0xFF

static struct backlight_properties radeon_bl_data;

struct radeon_bl_privdata {
	struct radeonfb_info *rinfo;
	uint8_t negative;
};

static int radeon_bl_get_level_brightness(struct radeon_bl_privdata *pdata,
		int level)
{
	struct fb_info *info = pdata->rinfo->info;
	int rlevel;

	mutex_lock(&info->bl_mutex);

	/* Get and convert the value */
	rlevel = pdata->rinfo->info->bl_curve[level] *
		 FB_BACKLIGHT_MAX / MAX_RADEON_LEVEL;

	mutex_unlock(&info->bl_mutex);

	if (pdata->negative)
		rlevel = MAX_RADEON_LEVEL - rlevel;

	if (rlevel < 0)
		rlevel = 0;
	else if (rlevel > MAX_RADEON_LEVEL)
		rlevel = MAX_RADEON_LEVEL;

	return rlevel;
}

static int radeon_bl_update_status(struct backlight_device *bd)
{
	struct radeon_bl_privdata *pdata = class_get_devdata(&bd->class_dev);
	struct radeonfb_info *rinfo = pdata->rinfo;
	u32 lvds_gen_cntl, tmpPixclksCntl;
	int level;

	if (rinfo->mon1_type != MT_LCD)
		return 0;

	/* We turn off the LCD completely instead of just dimming the
	 * backlight. This provides some greater power saving and the display
	 * is useless without backlight anyway.
	 */
        if (bd->props->power != FB_BLANK_UNBLANK ||
	    bd->props->fb_blank != FB_BLANK_UNBLANK)
		level = 0;
	else
		level = bd->props->brightness;

	del_timer_sync(&rinfo->lvds_timer);
	radeon_engine_idle();

	lvds_gen_cntl = INREG(LVDS_GEN_CNTL);
	if (level > 0) {
		lvds_gen_cntl &= ~LVDS_DISPLAY_DIS;
		if (!(lvds_gen_cntl & LVDS_BLON) || !(lvds_gen_cntl & LVDS_ON)) {
			lvds_gen_cntl |= (rinfo->init_state.lvds_gen_cntl & LVDS_DIGON);
			lvds_gen_cntl |= LVDS_BLON | LVDS_EN;
			OUTREG(LVDS_GEN_CNTL, lvds_gen_cntl);
			lvds_gen_cntl &= ~LVDS_BL_MOD_LEVEL_MASK;
			lvds_gen_cntl |=
				(radeon_bl_get_level_brightness(pdata, level) <<
				 LVDS_BL_MOD_LEVEL_SHIFT);
			lvds_gen_cntl |= LVDS_ON;
			lvds_gen_cntl |= (rinfo->init_state.lvds_gen_cntl & LVDS_BL_MOD_EN);
			rinfo->pending_lvds_gen_cntl = lvds_gen_cntl;
			mod_timer(&rinfo->lvds_timer,
				  jiffies + msecs_to_jiffies(rinfo->panel_info.pwr_delay));
		} else {
			lvds_gen_cntl &= ~LVDS_BL_MOD_LEVEL_MASK;
			lvds_gen_cntl |=
				(radeon_bl_get_level_brightness(pdata, level) <<
				 LVDS_BL_MOD_LEVEL_SHIFT);
			OUTREG(LVDS_GEN_CNTL, lvds_gen_cntl);
		}
		rinfo->init_state.lvds_gen_cntl &= ~LVDS_STATE_MASK;
		rinfo->init_state.lvds_gen_cntl |= rinfo->pending_lvds_gen_cntl
			& LVDS_STATE_MASK;
	} else {
		/* Asic bug, when turning off LVDS_ON, we have to make sure
		   RADEON_PIXCLK_LVDS_ALWAYS_ON bit is off
		*/
		tmpPixclksCntl = INPLL(PIXCLKS_CNTL);
		if (rinfo->is_mobility || rinfo->is_IGP)
			OUTPLLP(PIXCLKS_CNTL, 0, ~PIXCLK_LVDS_ALWAYS_ONb);
		lvds_gen_cntl &= ~(LVDS_BL_MOD_LEVEL_MASK | LVDS_BL_MOD_EN);
		lvds_gen_cntl |= (radeon_bl_get_level_brightness(pdata, 0) <<
				  LVDS_BL_MOD_LEVEL_SHIFT);
		lvds_gen_cntl |= LVDS_DISPLAY_DIS;
		OUTREG(LVDS_GEN_CNTL, lvds_gen_cntl);
		udelay(100);
		lvds_gen_cntl &= ~(LVDS_ON | LVDS_EN);
		OUTREG(LVDS_GEN_CNTL, lvds_gen_cntl);
		lvds_gen_cntl &= ~(LVDS_DIGON);
		rinfo->pending_lvds_gen_cntl = lvds_gen_cntl;
		mod_timer(&rinfo->lvds_timer,
			  jiffies + msecs_to_jiffies(rinfo->panel_info.pwr_delay));
		if (rinfo->is_mobility || rinfo->is_IGP)
			OUTPLL(PIXCLKS_CNTL, tmpPixclksCntl);
	}
	rinfo->init_state.lvds_gen_cntl &= ~LVDS_STATE_MASK;
	rinfo->init_state.lvds_gen_cntl |= (lvds_gen_cntl & LVDS_STATE_MASK);

	return 0;
}

static int radeon_bl_get_brightness(struct backlight_device *bd)
{
	return bd->props->brightness;
}

static struct backlight_properties radeon_bl_data = {
	.owner		= THIS_MODULE,
	.get_brightness = radeon_bl_get_brightness,
	.update_status	= radeon_bl_update_status,
	.max_brightness = (FB_BACKLIGHT_LEVELS - 1),
};

void radeonfb_bl_init(struct radeonfb_info *rinfo)
{
	struct backlight_device *bd;
	struct radeon_bl_privdata *pdata;
	char name[12];

	if (rinfo->mon1_type != MT_LCD)
		return;

#ifdef CONFIG_PMAC_BACKLIGHT
	if (!pmac_has_backlight_type("ati") &&
	    !pmac_has_backlight_type("mnca"))
		return;
#endif

	pdata = kmalloc(sizeof(struct radeon_bl_privdata), GFP_KERNEL);
	if (!pdata) {
		printk("radeonfb: Memory allocation failed\n");
		goto error;
	}

	snprintf(name, sizeof(name), "radeonbl%d", rinfo->info->node);

	bd = backlight_device_register(name, pdata, &radeon_bl_data);
	if (IS_ERR(bd)) {
		rinfo->info->bl_dev = NULL;
		printk("radeonfb: Backlight registration failed\n");
		goto error;
	}

	pdata->rinfo = rinfo;

	/* Pardon me for that hack... maybe some day we can figure out in what
	 * direction backlight should work on a given panel?
	 */
	pdata->negative =
		(rinfo->family != CHIP_FAMILY_RV200 &&
		 rinfo->family != CHIP_FAMILY_RV250 &&
		 rinfo->family != CHIP_FAMILY_RV280 &&
		 rinfo->family != CHIP_FAMILY_RV350);

#ifdef CONFIG_PMAC_BACKLIGHT
	pdata->negative = pdata->negative ||
		machine_is_compatible("PowerBook4,3") ||
		machine_is_compatible("PowerBook6,3") ||
		machine_is_compatible("PowerBook6,5");
#endif

	mutex_lock(&rinfo->info->bl_mutex);
	rinfo->info->bl_dev = bd;
	fb_bl_default_curve(rinfo->info, 0,
		 63 * FB_BACKLIGHT_MAX / MAX_RADEON_LEVEL,
		217 * FB_BACKLIGHT_MAX / MAX_RADEON_LEVEL);
	mutex_unlock(&rinfo->info->bl_mutex);

	up(&bd->sem);
	bd->props->brightness = radeon_bl_data.max_brightness;
	bd->props->power = FB_BLANK_UNBLANK;
	bd->props->update_status(bd);
	down(&bd->sem);

#ifdef CONFIG_PMAC_BACKLIGHT
	mutex_lock(&pmac_backlight_mutex);
	if (!pmac_backlight)
		pmac_backlight = bd;
	mutex_unlock(&pmac_backlight_mutex);
#endif

	printk("radeonfb: Backlight initialized (%s)\n", name);

	return;

error:
	kfree(pdata);
	return;
}

void radeonfb_bl_exit(struct radeonfb_info *rinfo)
{
#ifdef CONFIG_PMAC_BACKLIGHT
	mutex_lock(&pmac_backlight_mutex);
#endif

	mutex_lock(&rinfo->info->bl_mutex);
	if (rinfo->info->bl_dev) {
		struct radeon_bl_privdata *pdata;

#ifdef CONFIG_PMAC_BACKLIGHT
		if (pmac_backlight == rinfo->info->bl_dev)
			pmac_backlight = NULL;
#endif

		pdata = class_get_devdata(&rinfo->info->bl_dev->class_dev);
		backlight_device_unregister(rinfo->info->bl_dev);
		kfree(pdata);
		rinfo->info->bl_dev = NULL;

		printk("radeonfb: Backlight unloaded\n");
	}
	mutex_unlock(&rinfo->info->bl_mutex);

#ifdef CONFIG_PMAC_BACKLIGHT
	mutex_unlock(&pmac_backlight_mutex);
#endif
}
