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
#include <asm/prom.h>

#define MAX_PMU_LEVEL 0xFF

static struct backlight_properties pmu_backlight_data;
static spinlock_t pmu_backlight_lock;
static int sleeping;

static int pmu_backlight_get_level_brightness(struct fb_info *info,
		int level)
{
	int pmulevel;

	/* Get and convert the value */
	mutex_lock(&info->bl_mutex);
	pmulevel = info->bl_curve[level] * FB_BACKLIGHT_MAX / MAX_PMU_LEVEL;
	mutex_unlock(&info->bl_mutex);

	if (pmulevel < 0)
		pmulevel = 0;
	else if (pmulevel > MAX_PMU_LEVEL)
		pmulevel = MAX_PMU_LEVEL;

	return pmulevel;
}

static int pmu_backlight_update_status(struct backlight_device *bd)
{
	struct fb_info *info = class_get_devdata(&bd->class_dev);
	struct adb_request req;
	unsigned long flags;
	int level = bd->props->brightness;

	spin_lock_irqsave(&pmu_backlight_lock, flags);

	/* Don't update brightness when sleeping */
	if (sleeping)
		goto out;

	if (bd->props->power != FB_BLANK_UNBLANK ||
	    bd->props->fb_blank != FB_BLANK_UNBLANK)
		level = 0;

	if (level > 0) {
		int pmulevel = pmu_backlight_get_level_brightness(info, level);

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

out:
	spin_unlock_irqrestore(&pmu_backlight_lock, flags);

	return 0;
}

static int pmu_backlight_get_brightness(struct backlight_device *bd)
{
	return bd->props->brightness;
}

static struct backlight_properties pmu_backlight_data = {
	.owner		= THIS_MODULE,
	.get_brightness	= pmu_backlight_get_brightness,
	.update_status	= pmu_backlight_update_status,
	.max_brightness	= (FB_BACKLIGHT_LEVELS - 1),
};

#ifdef CONFIG_PM
static int pmu_backlight_sleep_call(struct pmu_sleep_notifier *self, int when)
{
	unsigned long flags;

	spin_lock_irqsave(&pmu_backlight_lock, flags);

	switch (when) {
	case PBOOK_SLEEP_REQUEST:
		sleeping = 1;
		break;
	case PBOOK_WAKE:
		sleeping = 0;
		break;
	}

	spin_unlock_irqrestore(&pmu_backlight_lock, flags);

	return PBOOK_SLEEP_OK;
}

static struct pmu_sleep_notifier pmu_backlight_sleep_notif = {
	.notifier_call = pmu_backlight_sleep_call,
};
#endif

void __init pmu_backlight_init()
{
	struct backlight_device *bd;
	struct fb_info *info;
	char name[10];
	int level, autosave;

	/* Special case for the old PowerBook since I can't test on it */
	autosave =
		machine_is_compatible("AAPL,3400/2400") ||
		machine_is_compatible("AAPL,3500");

	if (!autosave &&
	    !pmac_has_backlight_type("pmu") &&
	    !machine_is_compatible("AAPL,PowerBook1998") &&
	    !machine_is_compatible("PowerBook1,1"))
		return;

	/* Actually, this is a hack, but I don't know of a better way
	 * to get the first framebuffer device.
	 */
	info = registered_fb[0];
	if (!info) {
		printk("pmubl: No framebuffer found\n");
		goto error;
	}

	snprintf(name, sizeof(name), "pmubl%d", info->node);

	bd = backlight_device_register(name, info, &pmu_backlight_data);
	if (IS_ERR(bd)) {
		printk("pmubl: Backlight registration failed\n");
		goto error;
	}

	mutex_lock(&info->bl_mutex);
	info->bl_dev = bd;
	fb_bl_default_curve(info, 0x7F, 0x46, 0x0E);
	mutex_unlock(&info->bl_mutex);

	level = pmu_backlight_data.max_brightness;

	if (autosave) {
		/* read autosaved value if available */
		struct adb_request req;
		pmu_request(&req, NULL, 2, 0xd9, 0);
		pmu_wait_complete(&req);

		mutex_lock(&info->bl_mutex);
		level = pmac_backlight_curve_lookup(info,
				(req.reply[0] >> 4) *
				pmu_backlight_data.max_brightness / 15);
		mutex_unlock(&info->bl_mutex);
	}

	up(&bd->sem);
	bd->props->brightness = level;
	bd->props->power = FB_BLANK_UNBLANK;
	bd->props->update_status(bd);
	down(&bd->sem);

	mutex_lock(&pmac_backlight_mutex);
	if (!pmac_backlight)
		pmac_backlight = bd;
	mutex_unlock(&pmac_backlight_mutex);

#ifdef CONFIG_PM
	pmu_register_sleep_notifier(&pmu_backlight_sleep_notif);
#endif

	printk("pmubl: Backlight initialized (%s)\n", name);

	return;

error:
	return;
}
