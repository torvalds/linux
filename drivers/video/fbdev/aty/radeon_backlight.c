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
#include <linux/slab.h>

#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif

#define MAX_RADEON_LEVEL 0xFF

struct radeon_bl_privdata {
	struct radeonfb_info *rinfo;
	uint8_t negative;
};

static int radeon_bl_get_level_brightness(struct radeon_bl_privdata *pdata,
		int level)
{
	int rlevel;

	/* Get and convert the value */
	/* No locking of bl_curve since we read a single value */
	rlevel = pdata->rinfo->info->bl_curve[level] *
		 FB_BACKLIGHT_MAX / MAX_RADEON_LEVEL;

	if (rlevel < 0)
		rlevel = 0;
	else if (rlevel > MAX_RADEON_LEVEL)
		rlevel = MAX_RADEON_LEVEL;

	if (pdata->negative)
		rlevel = MAX_RADEON_LEVEL - rlevel;

	return rlevel;
}

static int radeon_bl_update_status(struct backlight_device *bd)
{
	struct radeon_bl_privdata *pdata = bl_get_data(bd);
	struct radeonfb_info *rinfo = pdata->rinfo;
	u32 lvds_gen_cntl, tmpPixclksCntl;
	int level;

	if (rinfo->mon1_type != MT_LCD)
		return 0;

	/* We turn off the LCD completely instead of just dimming the
	 * backlight. This provides some greater power saving and the display
	 * is useless without backlight anyway.
	 */
        if (bd->props.power != FB_BLANK_UNBLANK ||
	    bd->props.fb_blank != FB_BLANK_UNBLANK)
		level = 0;
	else
		level = bd->props.brightness;

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
	return bd->props.brightness;
}

static const struct backlight_ops radeon_bl_data = {
	.get_brightness = radeon_bl_get_brightness,
	.update_status	= radeon_bl_update_status,
};

void radeonfb_bl_init(struct radeonfb_info *rinfo)
{
	struct backlight_properties props;
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

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = FB_BACKLIGHT_LEVELS - 1;
	bd = backlight_device_register(name, rinfo->info->dev, pdata,
				       &radeon_bl_data, &props);
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
		of_machine_is_compatible("PowerBook4,3") ||
		of_machine_is_compatible("PowerBook6,3") ||
		of_machine_is_compatible("PowerBook6,5");
#endif

	rinfo->info->bl_dev = bd;
	fb_bl_default_curve(rinfo->info, 0,
		 63 * FB_BACKLIGHT_MAX / MAX_RADEON_LEVEL,
		217 * FB_BACKLIGHT_MAX / MAX_RADEON_LEVEL);

	bd->props.brightness = bd->props.max_brightness;
	bd->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(bd);

	printk("radeonfb: Backlight initialized (%s)\n", name);

	return;

error:
	kfree(pdata);
	return;
}

void radeonfb_bl_exit(struct radeonfb_info *rinfo)
{
	struct backlight_device *bd = rinfo->info->bl_dev;

	if (bd) {
		struct radeon_bl_privdata *pdata;

		pdata = bl_get_data(bd);
		backlight_device_unregister(bd);
		kfree(pdata);
		rinfo->info->bl_dev = NULL;

		printk("radeonfb: Backlight unloaded\n");
	}
}
