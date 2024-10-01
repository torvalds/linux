// SPDX-License-Identifier: GPL-2.0
/*
 * Backlight code for via-pmu
 *
 * Copyright (C) 1998 Paul Mackerras and Fabio Riccardi.
 * Copyright (C) 2001-2002 Benjamin Herrenschmidt
 * Copyright (C) 2006      Michael Hanselmann <linux-kernel@hansmi.ch>
 *
 */

#include <asm/ptrace.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <asm/backlight.h>

#define MAX_PMU_LEVEL 0xFF

static const struct backlight_ops pmu_backlight_data;
static DEFINE_SPINLOCK(pmu_backlight_lock);
static int sleeping, uses_pmu_bl;
static u8 bl_curve[FB_BACKLIGHT_LEVELS];

static void pmu_backlight_init_curve(u8 off, u8 min, u8 max)
{
	int i, flat, count, range = (max - min);

	bl_curve[0] = off;

	for (flat = 1; flat < (FB_BACKLIGHT_LEVELS / 16); ++flat)
		bl_curve[flat] = min;

	count = FB_BACKLIGHT_LEVELS * 15 / 16;
	for (i = 0; i < count; ++i)
		bl_curve[flat + i] = min + (range * (i + 1) / count);
}

static int pmu_backlight_curve_lookup(int value)
{
	int level = (FB_BACKLIGHT_LEVELS - 1);
	int i, max = 0;

	/* Look for biggest value */
	for (i = 0; i < FB_BACKLIGHT_LEVELS; i++)
		max = max((int)bl_curve[i], max);

	/* Look for nearest value */
	for (i = 0; i < FB_BACKLIGHT_LEVELS; i++) {
		int diff = abs(bl_curve[i] - value);
		if (diff < max) {
			max = diff;
			level = i;
		}
	}
	return level;
}

static int pmu_backlight_get_level_brightness(int level)
{
	int pmulevel;

	/* Get and convert the value */
	pmulevel = bl_curve[level] * FB_BACKLIGHT_MAX / MAX_PMU_LEVEL;
	if (pmulevel < 0)
		pmulevel = 0;
	else if (pmulevel > MAX_PMU_LEVEL)
		pmulevel = MAX_PMU_LEVEL;

	return pmulevel;
}

static int __pmu_backlight_update_status(struct backlight_device *bd)
{
	struct adb_request req;
	int level = bd->props.brightness;


	if (bd->props.power != FB_BLANK_UNBLANK ||
	    bd->props.fb_blank != FB_BLANK_UNBLANK)
		level = 0;

	if (level > 0) {
		int pmulevel = pmu_backlight_get_level_brightness(level);

		pmu_request(&req, NULL, 2, PMU_BACKLIGHT_BRIGHT, pmulevel);
		pmu_wait_complete(&req);

		pmu_request(&req, NULL, 2, PMU_POWER_CTRL,
			PMU_POW_BACKLIGHT | PMU_POW_ON);
		pmu_wait_complete(&req);
	} else {
		pmu_request(&req, NULL, 2, PMU_POWER_CTRL,
			PMU_POW_BACKLIGHT | PMU_POW_OFF);
		pmu_wait_complete(&req);
	}

	return 0;
}

static int pmu_backlight_update_status(struct backlight_device *bd)
{
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&pmu_backlight_lock, flags);
	/* Don't update brightness when sleeping */
	if (!sleeping)
		rc = __pmu_backlight_update_status(bd);
	spin_unlock_irqrestore(&pmu_backlight_lock, flags);
	return rc;
}


static const struct backlight_ops pmu_backlight_data = {
	.update_status	= pmu_backlight_update_status,

};

#ifdef CONFIG_PM
void pmu_backlight_set_sleep(int sleep)
{
	unsigned long flags;

	spin_lock_irqsave(&pmu_backlight_lock, flags);
	sleeping = sleep;
	if (pmac_backlight && uses_pmu_bl) {
		if (sleep) {
			struct adb_request req;

			pmu_request(&req, NULL, 2, PMU_POWER_CTRL,
				    PMU_POW_BACKLIGHT | PMU_POW_OFF);
			pmu_wait_complete(&req);
		} else
			__pmu_backlight_update_status(pmac_backlight);
	}
	spin_unlock_irqrestore(&pmu_backlight_lock, flags);
}
#endif /* CONFIG_PM */

void __init pmu_backlight_init(void)
{
	struct backlight_properties props;
	struct backlight_device *bd;
	char name[10];
	int level, autosave;

	/* Special case for the old PowerBook since I can't test on it */
	autosave =
		of_machine_is_compatible("AAPL,3400/2400") ||
		of_machine_is_compatible("AAPL,3500");

	if (!autosave &&
	    !pmac_has_backlight_type("pmu") &&
	    !of_machine_is_compatible("AAPL,PowerBook1998") &&
	    !of_machine_is_compatible("PowerBook1,1"))
		return;

	snprintf(name, sizeof(name), "pmubl");

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = FB_BACKLIGHT_LEVELS - 1;
	bd = backlight_device_register(name, NULL, NULL, &pmu_backlight_data,
				       &props);
	if (IS_ERR(bd)) {
		printk(KERN_ERR "PMU Backlight registration failed\n");
		return;
	}
	uses_pmu_bl = 1;
	pmu_backlight_init_curve(0x7F, 0x46, 0x0E);

	level = bd->props.max_brightness;

	if (autosave) {
		/* read autosaved value if available */
		struct adb_request req;
		pmu_request(&req, NULL, 2, 0xd9, 0);
		pmu_wait_complete(&req);

		level = pmu_backlight_curve_lookup(
				(req.reply[0] >> 4) *
				bd->props.max_brightness / 15);
	}

	bd->props.brightness = level;
	bd->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(bd);

	printk(KERN_INFO "PMU Backlight initialized (%s)\n", name);
}
