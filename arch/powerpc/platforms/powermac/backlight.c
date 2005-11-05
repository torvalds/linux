/*
 * Miscellaneous procedures for dealing with the PowerMac hardware.
 * Contains support for the backlight.
 *
 *   Copyright (C) 2000 Benjamin Herrenschmidt
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/reboot.h>
#include <linux/nvram.h>
#include <linux/console.h>
#include <asm/sections.h>
#include <asm/ptrace.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/nvram.h>
#include <asm/backlight.h>

#include <linux/adb.h>
#include <linux/pmu.h>

static struct backlight_controller *backlighter;
static void* backlighter_data;
static int backlight_autosave;
static int backlight_level = BACKLIGHT_MAX;
static int backlight_enabled = 1;
static int backlight_req_level = -1;
static int backlight_req_enable = -1;

static void backlight_callback(void *);
static DECLARE_WORK(backlight_work, backlight_callback, NULL);

void register_backlight_controller(struct backlight_controller *ctrler,
					  void *data, char *type)
{
	struct device_node* bk_node;
	char *prop;
	int valid = 0;

	/* There's already a matching controller, bail out */
	if (backlighter != NULL)
		return;

	bk_node = find_devices("backlight");

#ifdef CONFIG_ADB_PMU
	/* Special case for the old PowerBook since I can't test on it */
	backlight_autosave = machine_is_compatible("AAPL,3400/2400")
		|| machine_is_compatible("AAPL,3500");
	if ((backlight_autosave
	     || machine_is_compatible("AAPL,PowerBook1998")
	     || machine_is_compatible("PowerBook1,1"))
	    && !strcmp(type, "pmu"))
		valid = 1;
#endif
	if (bk_node) {
		prop = get_property(bk_node, "backlight-control", NULL);
		if (prop && !strncmp(prop, type, strlen(type)))
			valid = 1;
	}
	if (!valid)
		return;
	backlighter = ctrler;
	backlighter_data = data;

	if (bk_node && !backlight_autosave)
		prop = get_property(bk_node, "bklt", NULL);
	else
		prop = NULL;
	if (prop) {
		backlight_level = ((*prop)+1) >> 1;
		if (backlight_level > BACKLIGHT_MAX)
			backlight_level = BACKLIGHT_MAX;
	}

#ifdef CONFIG_ADB_PMU
	if (backlight_autosave) {
		struct adb_request req;
		pmu_request(&req, NULL, 2, 0xd9, 0);
		while (!req.complete)
			pmu_poll();
		backlight_level = req.reply[0] >> 4;
	}
#endif
	acquire_console_sem();
	if (!backlighter->set_enable(1, backlight_level, data))
		backlight_enabled = 1;
	release_console_sem();

	printk(KERN_INFO "Registered \"%s\" backlight controller,"
	       "level: %d/15\n", type, backlight_level);
}
EXPORT_SYMBOL(register_backlight_controller);

void unregister_backlight_controller(struct backlight_controller
					    *ctrler, void *data)
{
	/* We keep the current backlight level (for now) */
	if (ctrler == backlighter && data == backlighter_data)
		backlighter = NULL;
}
EXPORT_SYMBOL(unregister_backlight_controller);

static int __set_backlight_enable(int enable)
{
	int rc;

	if (!backlighter)
		return -ENODEV;
	acquire_console_sem();
	rc = backlighter->set_enable(enable, backlight_level,
				     backlighter_data);
	if (!rc)
		backlight_enabled = enable;
	release_console_sem();
	return rc;
}
int set_backlight_enable(int enable)
{
	if (!backlighter)
		return -ENODEV;
	backlight_req_enable = enable;
	schedule_work(&backlight_work);
	return 0;
}

EXPORT_SYMBOL(set_backlight_enable);

int get_backlight_enable(void)
{
	if (!backlighter)
		return -ENODEV;
	return backlight_enabled;
}
EXPORT_SYMBOL(get_backlight_enable);

static int __set_backlight_level(int level)
{
	int rc = 0;

	if (!backlighter)
		return -ENODEV;
	if (level < BACKLIGHT_MIN)
		level = BACKLIGHT_OFF;
	if (level > BACKLIGHT_MAX)
		level = BACKLIGHT_MAX;
	acquire_console_sem();
	if (backlight_enabled)
		rc = backlighter->set_level(level, backlighter_data);
	if (!rc)
		backlight_level = level;
	release_console_sem();
	if (!rc && !backlight_autosave) {
		level <<=1;
		if (level & 0x10)
			level |= 0x01;
		// -- todo: save to property "bklt"
	}
	return rc;
}
int set_backlight_level(int level)
{
	if (!backlighter)
		return -ENODEV;
	backlight_req_level = level;
	schedule_work(&backlight_work);
	return 0;
}

EXPORT_SYMBOL(set_backlight_level);

int get_backlight_level(void)
{
	if (!backlighter)
		return -ENODEV;
	return backlight_level;
}
EXPORT_SYMBOL(get_backlight_level);

static void backlight_callback(void *dummy)
{
	int level, enable;

	do {
		level = backlight_req_level;
		enable = backlight_req_enable;
		mb();

		if (level >= 0)
			__set_backlight_level(level);
		if (enable >= 0)
			__set_backlight_enable(enable);
	} while(cmpxchg(&backlight_req_level, level, -1) != level ||
		cmpxchg(&backlight_req_enable, enable, -1) != enable);
}
