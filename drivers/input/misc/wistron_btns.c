// SPDX-License-Identifier: GPL-2.0-only
/*
 * Wistron laptop button driver
 * Copyright (C) 2005 Miloslav Trmac <mitr@volny.cz>
 * Copyright (C) 2005 Bernhard Rosenkraenzer <bero@arklinux.org>
 * Copyright (C) 2005 Dmitry Torokhov <dtor@mail.ru>
 */
#include <linux/io.h>
#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mc146818rtc.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/leds.h>

/* How often we poll keys - msecs */
#define POLL_INTERVAL_DEFAULT	500 /* when idle */
#define POLL_INTERVAL_BURST	100 /* when a key was recently pressed */

/* BIOS subsystem IDs */
#define WIFI		0x35
#define BLUETOOTH	0x34
#define MAIL_LED	0x31

MODULE_AUTHOR("Miloslav Trmac <mitr@volny.cz>");
MODULE_DESCRIPTION("Wistron laptop button driver");
MODULE_LICENSE("GPL v2");

static bool force; /* = 0; */
module_param(force, bool, 0);
MODULE_PARM_DESC(force, "Load even if computer is not in database");

static char *keymap_name; /* = NULL; */
module_param_named(keymap, keymap_name, charp, 0);
MODULE_PARM_DESC(keymap, "Keymap name, if it can't be autodetected [generic, 1557/MS2141]");

static struct platform_device *wistron_device;

 /* BIOS interface implementation */

static void __iomem *bios_entry_point; /* BIOS routine entry point */
static void __iomem *bios_code_map_base;
static void __iomem *bios_data_map_base;

static u8 cmos_address;

struct regs {
	u32 eax, ebx, ecx;
};

static void call_bios(struct regs *regs)
{
	unsigned long flags;

	preempt_disable();
	local_irq_save(flags);
	asm volatile ("pushl %%ebp;"
		      "movl %7, %%ebp;"
		      "call *%6;"
		      "popl %%ebp"
		      : "=a" (regs->eax), "=b" (regs->ebx), "=c" (regs->ecx)
		      : "0" (regs->eax), "1" (regs->ebx), "2" (regs->ecx),
			"m" (bios_entry_point), "m" (bios_data_map_base)
		      : "edx", "edi", "esi", "memory");
	local_irq_restore(flags);
	preempt_enable();
}

static ssize_t __init locate_wistron_bios(void __iomem *base)
{
	static unsigned char __initdata signature[] =
		{ 0x42, 0x21, 0x55, 0x30 };
	ssize_t offset;

	for (offset = 0; offset < 0x10000; offset += 0x10) {
		if (check_signature(base + offset, signature,
				    sizeof(signature)) != 0)
			return offset;
	}
	return -1;
}

static int __init map_bios(void)
{
	void __iomem *base;
	ssize_t offset;
	u32 entry_point;

	base = ioremap(0xF0000, 0x10000); /* Can't fail */
	offset = locate_wistron_bios(base);
	if (offset < 0) {
		printk(KERN_ERR "wistron_btns: BIOS entry point not found\n");
		iounmap(base);
		return -ENODEV;
	}

	entry_point = readl(base + offset + 5);
	printk(KERN_DEBUG
		"wistron_btns: BIOS signature found at %p, entry point %08X\n",
		base + offset, entry_point);

	if (entry_point >= 0xF0000) {
		bios_code_map_base = base;
		bios_entry_point = bios_code_map_base + (entry_point & 0xFFFF);
	} else {
		iounmap(base);
		bios_code_map_base = ioremap(entry_point & ~0x3FFF, 0x4000);
		if (bios_code_map_base == NULL) {
			printk(KERN_ERR
				"wistron_btns: Can't map BIOS code at %08X\n",
				entry_point & ~0x3FFF);
			goto err;
		}
		bios_entry_point = bios_code_map_base + (entry_point & 0x3FFF);
	}
	/* The Windows driver maps 0x10000 bytes, we keep only one page... */
	bios_data_map_base = ioremap(0x400, 0xc00);
	if (bios_data_map_base == NULL) {
		printk(KERN_ERR "wistron_btns: Can't map BIOS data\n");
		goto err_code;
	}
	return 0;

err_code:
	iounmap(bios_code_map_base);
err:
	return -ENOMEM;
}

static inline void unmap_bios(void)
{
	iounmap(bios_code_map_base);
	iounmap(bios_data_map_base);
}

 /* BIOS calls */

static u16 bios_pop_queue(void)
{
	struct regs regs;

	memset(&regs, 0, sizeof (regs));
	regs.eax = 0x9610;
	regs.ebx = 0x061C;
	regs.ecx = 0x0000;
	call_bios(&regs);

	return regs.eax;
}

static void bios_attach(void)
{
	struct regs regs;

	memset(&regs, 0, sizeof (regs));
	regs.eax = 0x9610;
	regs.ebx = 0x012E;
	call_bios(&regs);
}

static void bios_detach(void)
{
	struct regs regs;

	memset(&regs, 0, sizeof (regs));
	regs.eax = 0x9610;
	regs.ebx = 0x002E;
	call_bios(&regs);
}

static u8 bios_get_cmos_address(void)
{
	struct regs regs;

	memset(&regs, 0, sizeof (regs));
	regs.eax = 0x9610;
	regs.ebx = 0x051C;
	call_bios(&regs);

	return regs.ecx;
}

static u16 bios_get_default_setting(u8 subsys)
{
	struct regs regs;

	memset(&regs, 0, sizeof (regs));
	regs.eax = 0x9610;
	regs.ebx = 0x0200 | subsys;
	call_bios(&regs);

	return regs.eax;
}

static void bios_set_state(u8 subsys, int enable)
{
	struct regs regs;

	memset(&regs, 0, sizeof (regs));
	regs.eax = 0x9610;
	regs.ebx = (enable ? 0x0100 : 0x0000) | subsys;
	call_bios(&regs);
}

/* Hardware database */

#define KE_WIFI		(KE_LAST + 1)
#define KE_BLUETOOTH	(KE_LAST + 2)

#define FE_MAIL_LED 0x01
#define FE_WIFI_LED 0x02
#define FE_UNTESTED 0x80

static struct key_entry *keymap; /* = NULL; Current key map */
static bool have_wifi;
static bool have_bluetooth;
static int leds_present;	/* bitmask of leds present */

static int __init dmi_matched(const struct dmi_system_id *dmi)
{
	const struct key_entry *key;

	keymap = dmi->driver_data;
	for (key = keymap; key->type != KE_END; key++) {
		if (key->type == KE_WIFI)
			have_wifi = true;
		else if (key->type == KE_BLUETOOTH)
			have_bluetooth = true;
	}
	leds_present = key->code & (FE_MAIL_LED | FE_WIFI_LED);

	return 1;
}

static struct key_entry keymap_empty[] __initdata = {
	{ KE_END, 0 }
};

static struct key_entry keymap_fs_amilo_pro_v2000[] __initdata = {
	{ KE_KEY,  0x01, {KEY_HELP} },
	{ KE_KEY,  0x11, {KEY_PROG1} },
	{ KE_KEY,  0x12, {KEY_PROG2} },
	{ KE_WIFI, 0x30 },
	{ KE_KEY,  0x31, {KEY_MAIL} },
	{ KE_KEY,  0x36, {KEY_WWW} },
	{ KE_END,  0 }
};

static struct key_entry keymap_fs_amilo_pro_v3505[] __initdata = {
	{ KE_KEY,       0x01, {KEY_HELP} },          /* Fn+F1 */
	{ KE_KEY,       0x06, {KEY_DISPLAYTOGGLE} }, /* Fn+F4 */
	{ KE_BLUETOOTH, 0x30 },                      /* Fn+F10 */
	{ KE_KEY,       0x31, {KEY_MAIL} },          /* mail button */
	{ KE_KEY,       0x36, {KEY_WWW} },           /* www button */
	{ KE_WIFI,      0x78 },                      /* satellite dish button */
	{ KE_END,       0 }
};

static struct key_entry keymap_fs_amilo_pro_v8210[] __initdata = {
	{ KE_KEY,       0x01, {KEY_HELP} },          /* Fn+F1 */
	{ KE_KEY,       0x06, {KEY_DISPLAYTOGGLE} }, /* Fn+F4 */
	{ KE_BLUETOOTH, 0x30 },                      /* Fn+F10 */
	{ KE_KEY,       0x31, {KEY_MAIL} },          /* mail button */
	{ KE_KEY,       0x36, {KEY_WWW} },           /* www button */
	{ KE_WIFI,      0x78 },                      /* satelite dish button */
	{ KE_END,       FE_WIFI_LED }
};

static struct key_entry keymap_fujitsu_n3510[] __initdata = {
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x71, {KEY_STOPCD} },
	{ KE_KEY, 0x72, {KEY_PLAYPAUSE} },
	{ KE_KEY, 0x74, {KEY_REWIND} },
	{ KE_KEY, 0x78, {KEY_FORWARD} },
	{ KE_END, 0 }
};

static struct key_entry keymap_wistron_ms2111[] __initdata = {
	{ KE_KEY,  0x11, {KEY_PROG1} },
	{ KE_KEY,  0x12, {KEY_PROG2} },
	{ KE_KEY,  0x13, {KEY_PROG3} },
	{ KE_KEY,  0x31, {KEY_MAIL} },
	{ KE_KEY,  0x36, {KEY_WWW} },
	{ KE_END, FE_MAIL_LED }
};

static struct key_entry keymap_wistron_md40100[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x02, {KEY_CONFIG} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_KEY, 0x37, {KEY_DISPLAYTOGGLE} }, /* Display on/off */
	{ KE_END, FE_MAIL_LED | FE_WIFI_LED | FE_UNTESTED }
};

static struct key_entry keymap_wistron_ms2141[] __initdata = {
	{ KE_KEY,  0x11, {KEY_PROG1} },
	{ KE_KEY,  0x12, {KEY_PROG2} },
	{ KE_WIFI, 0x30 },
	{ KE_KEY,  0x22, {KEY_REWIND} },
	{ KE_KEY,  0x23, {KEY_FORWARD} },
	{ KE_KEY,  0x24, {KEY_PLAYPAUSE} },
	{ KE_KEY,  0x25, {KEY_STOPCD} },
	{ KE_KEY,  0x31, {KEY_MAIL} },
	{ KE_KEY,  0x36, {KEY_WWW} },
	{ KE_END,  0 }
};

static struct key_entry keymap_acer_aspire_1500[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x03, {KEY_POWER} },
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_WIFI, 0x30 },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_KEY, 0x49, {KEY_CONFIG} },
	{ KE_BLUETOOTH, 0x44 },
	{ KE_END, FE_UNTESTED }
};

static struct key_entry keymap_acer_aspire_1600[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x03, {KEY_POWER} },
	{ KE_KEY, 0x08, {KEY_MUTE} },
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x13, {KEY_PROG3} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_KEY, 0x49, {KEY_CONFIG} },
	{ KE_WIFI, 0x30 },
	{ KE_BLUETOOTH, 0x44 },
	{ KE_END, FE_MAIL_LED | FE_UNTESTED }
};

/* 3020 has been tested */
static struct key_entry keymap_acer_aspire_5020[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x03, {KEY_POWER} },
	{ KE_KEY, 0x05, {KEY_SWITCHVIDEOMODE} }, /* Display selection */
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_KEY, 0x6a, {KEY_CONFIG} },
	{ KE_WIFI, 0x30 },
	{ KE_BLUETOOTH, 0x44 },
	{ KE_END, FE_MAIL_LED | FE_UNTESTED }
};

static struct key_entry keymap_acer_travelmate_2410[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x6d, {KEY_POWER} },
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_KEY, 0x6a, {KEY_CONFIG} },
	{ KE_WIFI, 0x30 },
	{ KE_BLUETOOTH, 0x44 },
	{ KE_END, FE_MAIL_LED | FE_UNTESTED }
};

static struct key_entry keymap_acer_travelmate_110[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x02, {KEY_CONFIG} },
	{ KE_KEY, 0x03, {KEY_POWER} },
	{ KE_KEY, 0x08, {KEY_MUTE} },
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x20, {KEY_VOLUMEUP} },
	{ KE_KEY, 0x21, {KEY_VOLUMEDOWN} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_SW, 0x4a, {.sw = {SW_LID, 1}} }, /* lid close */
	{ KE_SW, 0x4b, {.sw = {SW_LID, 0}} }, /* lid open */
	{ KE_WIFI, 0x30 },
	{ KE_END, FE_MAIL_LED | FE_UNTESTED }
};

static struct key_entry keymap_acer_travelmate_300[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x02, {KEY_CONFIG} },
	{ KE_KEY, 0x03, {KEY_POWER} },
	{ KE_KEY, 0x08, {KEY_MUTE} },
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x20, {KEY_VOLUMEUP} },
	{ KE_KEY, 0x21, {KEY_VOLUMEDOWN} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_WIFI, 0x30 },
	{ KE_BLUETOOTH, 0x44 },
	{ KE_END, FE_MAIL_LED | FE_UNTESTED }
};

static struct key_entry keymap_acer_travelmate_380[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x02, {KEY_CONFIG} },
	{ KE_KEY, 0x03, {KEY_POWER} }, /* not 370 */
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x13, {KEY_PROG3} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_WIFI, 0x30 },
	{ KE_END, FE_MAIL_LED | FE_UNTESTED }
};

/* unusual map */
static struct key_entry keymap_acer_travelmate_220[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x02, {KEY_CONFIG} },
	{ KE_KEY, 0x11, {KEY_MAIL} },
	{ KE_KEY, 0x12, {KEY_WWW} },
	{ KE_KEY, 0x13, {KEY_PROG2} },
	{ KE_KEY, 0x31, {KEY_PROG1} },
	{ KE_END, FE_WIFI_LED | FE_UNTESTED }
};

static struct key_entry keymap_acer_travelmate_230[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x02, {KEY_CONFIG} },
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_END, FE_WIFI_LED | FE_UNTESTED }
};

static struct key_entry keymap_acer_travelmate_240[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x02, {KEY_CONFIG} },
	{ KE_KEY, 0x03, {KEY_POWER} },
	{ KE_KEY, 0x08, {KEY_MUTE} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_BLUETOOTH, 0x44 },
	{ KE_WIFI, 0x30 },
	{ KE_END, FE_UNTESTED }
};

static struct key_entry keymap_acer_travelmate_350[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x02, {KEY_CONFIG} },
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x13, {KEY_MAIL} },
	{ KE_KEY, 0x14, {KEY_PROG3} },
	{ KE_KEY, 0x15, {KEY_WWW} },
	{ KE_END, FE_MAIL_LED | FE_WIFI_LED | FE_UNTESTED }
};

static struct key_entry keymap_acer_travelmate_360[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x02, {KEY_CONFIG} },
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x13, {KEY_MAIL} },
	{ KE_KEY, 0x14, {KEY_PROG3} },
	{ KE_KEY, 0x15, {KEY_WWW} },
	{ KE_KEY, 0x40, {KEY_WLAN} },
	{ KE_END, FE_WIFI_LED | FE_UNTESTED } /* no mail led */
};

/* Wifi subsystem only activates the led. Therefore we need to pass
 * wifi event as a normal key, then userspace can really change the wifi state.
 * TODO we need to export led state to userspace (wifi and mail) */
static struct key_entry keymap_acer_travelmate_610[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x02, {KEY_CONFIG} },
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x13, {KEY_PROG3} },
	{ KE_KEY, 0x14, {KEY_MAIL} },
	{ KE_KEY, 0x15, {KEY_WWW} },
	{ KE_KEY, 0x40, {KEY_WLAN} },
	{ KE_END, FE_MAIL_LED | FE_WIFI_LED }
};

static struct key_entry keymap_acer_travelmate_630[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x02, {KEY_CONFIG} },
	{ KE_KEY, 0x03, {KEY_POWER} },
	{ KE_KEY, 0x08, {KEY_MUTE} }, /* not 620 */
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x13, {KEY_PROG3} },
	{ KE_KEY, 0x20, {KEY_VOLUMEUP} },
	{ KE_KEY, 0x21, {KEY_VOLUMEDOWN} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_WIFI, 0x30 },
	{ KE_END, FE_MAIL_LED | FE_UNTESTED }
};

static struct key_entry keymap_aopen_1559as[] __initdata = {
	{ KE_KEY,  0x01, {KEY_HELP} },
	{ KE_KEY,  0x06, {KEY_PROG3} },
	{ KE_KEY,  0x11, {KEY_PROG1} },
	{ KE_KEY,  0x12, {KEY_PROG2} },
	{ KE_WIFI, 0x30 },
	{ KE_KEY,  0x31, {KEY_MAIL} },
	{ KE_KEY,  0x36, {KEY_WWW} },
	{ KE_END,  0 },
};

static struct key_entry keymap_fs_amilo_d88x0[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x08, {KEY_MUTE} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x13, {KEY_PROG3} },
	{ KE_END, FE_MAIL_LED | FE_WIFI_LED | FE_UNTESTED }
};

static struct key_entry keymap_wistron_md2900[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x02, {KEY_CONFIG} },
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_WIFI, 0x30 },
	{ KE_END, FE_MAIL_LED | FE_UNTESTED }
};

static struct key_entry keymap_wistron_md96500[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x02, {KEY_CONFIG} },
	{ KE_KEY, 0x05, {KEY_SWITCHVIDEOMODE} }, /* Display selection */
	{ KE_KEY, 0x06, {KEY_DISPLAYTOGGLE} }, /* Display on/off */
	{ KE_KEY, 0x08, {KEY_MUTE} },
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x20, {KEY_VOLUMEUP} },
	{ KE_KEY, 0x21, {KEY_VOLUMEDOWN} },
	{ KE_KEY, 0x22, {KEY_REWIND} },
	{ KE_KEY, 0x23, {KEY_FORWARD} },
	{ KE_KEY, 0x24, {KEY_PLAYPAUSE} },
	{ KE_KEY, 0x25, {KEY_STOPCD} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_WIFI, 0x30 },
	{ KE_BLUETOOTH, 0x44 },
	{ KE_END, 0 }
};

static struct key_entry keymap_wistron_generic[] __initdata = {
	{ KE_KEY, 0x01, {KEY_HELP} },
	{ KE_KEY, 0x02, {KEY_CONFIG} },
	{ KE_KEY, 0x03, {KEY_POWER} },
	{ KE_KEY, 0x05, {KEY_SWITCHVIDEOMODE} }, /* Display selection */
	{ KE_KEY, 0x06, {KEY_DISPLAYTOGGLE} }, /* Display on/off */
	{ KE_KEY, 0x08, {KEY_MUTE} },
	{ KE_KEY, 0x11, {KEY_PROG1} },
	{ KE_KEY, 0x12, {KEY_PROG2} },
	{ KE_KEY, 0x13, {KEY_PROG3} },
	{ KE_KEY, 0x14, {KEY_MAIL} },
	{ KE_KEY, 0x15, {KEY_WWW} },
	{ KE_KEY, 0x20, {KEY_VOLUMEUP} },
	{ KE_KEY, 0x21, {KEY_VOLUMEDOWN} },
	{ KE_KEY, 0x22, {KEY_REWIND} },
	{ KE_KEY, 0x23, {KEY_FORWARD} },
	{ KE_KEY, 0x24, {KEY_PLAYPAUSE} },
	{ KE_KEY, 0x25, {KEY_STOPCD} },
	{ KE_KEY, 0x31, {KEY_MAIL} },
	{ KE_KEY, 0x36, {KEY_WWW} },
	{ KE_KEY, 0x37, {KEY_DISPLAYTOGGLE} }, /* Display on/off */
	{ KE_KEY, 0x40, {KEY_WLAN} },
	{ KE_KEY, 0x49, {KEY_CONFIG} },
	{ KE_SW, 0x4a, {.sw = {SW_LID, 1}} }, /* lid close */
	{ KE_SW, 0x4b, {.sw = {SW_LID, 0}} }, /* lid open */
	{ KE_KEY, 0x6a, {KEY_CONFIG} },
	{ KE_KEY, 0x6d, {KEY_POWER} },
	{ KE_KEY, 0x71, {KEY_STOPCD} },
	{ KE_KEY, 0x72, {KEY_PLAYPAUSE} },
	{ KE_KEY, 0x74, {KEY_REWIND} },
	{ KE_KEY, 0x78, {KEY_FORWARD} },
	{ KE_WIFI, 0x30 },
	{ KE_BLUETOOTH, 0x44 },
	{ KE_END, 0 }
};

static struct key_entry keymap_aopen_1557[] __initdata = {
	{ KE_KEY,  0x01, {KEY_HELP} },
	{ KE_KEY,  0x11, {KEY_PROG1} },
	{ KE_KEY,  0x12, {KEY_PROG2} },
	{ KE_WIFI, 0x30 },
	{ KE_KEY,  0x22, {KEY_REWIND} },
	{ KE_KEY,  0x23, {KEY_FORWARD} },
	{ KE_KEY,  0x24, {KEY_PLAYPAUSE} },
	{ KE_KEY,  0x25, {KEY_STOPCD} },
	{ KE_KEY,  0x31, {KEY_MAIL} },
	{ KE_KEY,  0x36, {KEY_WWW} },
	{ KE_END,  0 }
};

static struct key_entry keymap_prestigio[] __initdata = {
	{ KE_KEY,  0x11, {KEY_PROG1} },
	{ KE_KEY,  0x12, {KEY_PROG2} },
	{ KE_WIFI, 0x30 },
	{ KE_KEY,  0x22, {KEY_REWIND} },
	{ KE_KEY,  0x23, {KEY_FORWARD} },
	{ KE_KEY,  0x24, {KEY_PLAYPAUSE} },
	{ KE_KEY,  0x25, {KEY_STOPCD} },
	{ KE_KEY,  0x31, {KEY_MAIL} },
	{ KE_KEY,  0x36, {KEY_WWW} },
	{ KE_END,  0 }
};


/*
 * If your machine is not here (which is currently rather likely), please send
 * a list of buttons and their key codes (reported when loading this module
 * with force=1) and the output of dmidecode to $MODULE_AUTHOR.
 */
static const struct dmi_system_id dmi_ids[] __initconst = {
	{
		/* Fujitsu-Siemens Amilo Pro V2000 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Pro V2000"),
		},
		.driver_data = keymap_fs_amilo_pro_v2000
	},
	{
		/* Fujitsu-Siemens Amilo Pro Edition V3505 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Pro Edition V3505"),
		},
		.driver_data = keymap_fs_amilo_pro_v3505
	},
	{
		/* Fujitsu-Siemens Amilo Pro Edition V8210 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Pro Series V8210"),
		},
		.driver_data = keymap_fs_amilo_pro_v8210
	},
	{
		/* Fujitsu-Siemens Amilo M7400 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO M        "),
		},
		.driver_data = keymap_fs_amilo_pro_v2000
	},
	{
		/* Maxdata Pro 7000 DX */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MAXDATA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Pro 7000"),
		},
		.driver_data = keymap_fs_amilo_pro_v2000
	},
	{
		/* Fujitsu N3510 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "N3510"),
		},
		.driver_data = keymap_fujitsu_n3510
	},
	{
		/* Acer Aspire 1500 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 1500"),
		},
		.driver_data = keymap_acer_aspire_1500
	},
	{
		/* Acer Aspire 1600 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 1600"),
		},
		.driver_data = keymap_acer_aspire_1600
	},
	{
		/* Acer Aspire 3020 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 3020"),
		},
		.driver_data = keymap_acer_aspire_5020
	},
	{
		/* Acer Aspire 5020 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5020"),
		},
		.driver_data = keymap_acer_aspire_5020
	},
	{
		/* Acer TravelMate 2100 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 2100"),
		},
		.driver_data = keymap_acer_aspire_5020
	},
	{
		/* Acer TravelMate 2410 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 2410"),
		},
		.driver_data = keymap_acer_travelmate_2410
	},
	{
		/* Acer TravelMate C300 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate C300"),
		},
		.driver_data = keymap_acer_travelmate_300
	},
	{
		/* Acer TravelMate C100 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate C100"),
		},
		.driver_data = keymap_acer_travelmate_300
	},
	{
		/* Acer TravelMate C110 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate C110"),
		},
		.driver_data = keymap_acer_travelmate_110
	},
	{
		/* Acer TravelMate 380 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 380"),
		},
		.driver_data = keymap_acer_travelmate_380
	},
	{
		/* Acer TravelMate 370 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 370"),
		},
		.driver_data = keymap_acer_travelmate_380 /* keyboard minus 1 key */
	},
	{
		/* Acer TravelMate 220 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 220"),
		},
		.driver_data = keymap_acer_travelmate_220
	},
	{
		/* Acer TravelMate 260 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 260"),
		},
		.driver_data = keymap_acer_travelmate_220
	},
	{
		/* Acer TravelMate 230 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 230"),
			/* acerhk looks for "TravelMate F4..." ?! */
		},
		.driver_data = keymap_acer_travelmate_230
	},
	{
		/* Acer TravelMate 280 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 280"),
		},
		.driver_data = keymap_acer_travelmate_230
	},
	{
		/* Acer TravelMate 240 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 240"),
		},
		.driver_data = keymap_acer_travelmate_240
	},
	{
		/* Acer TravelMate 250 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 250"),
		},
		.driver_data = keymap_acer_travelmate_240
	},
	{
		/* Acer TravelMate 2424NWXCi */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 2420"),
		},
		.driver_data = keymap_acer_travelmate_240
	},
	{
		/* Acer TravelMate 350 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 350"),
		},
		.driver_data = keymap_acer_travelmate_350
	},
	{
		/* Acer TravelMate 360 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 360"),
		},
		.driver_data = keymap_acer_travelmate_360
	},
	{
		/* Acer TravelMate 610 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ACER"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 610"),
		},
		.driver_data = keymap_acer_travelmate_610
	},
	{
		/* Acer TravelMate 620 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 620"),
		},
		.driver_data = keymap_acer_travelmate_630
	},
	{
		/* Acer TravelMate 630 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 630"),
		},
		.driver_data = keymap_acer_travelmate_630
	},
	{
		/* AOpen 1559AS */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "E2U"),
			DMI_MATCH(DMI_BOARD_NAME, "E2U"),
		},
		.driver_data = keymap_aopen_1559as
	},
	{
		/* Medion MD 9783 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDIONNB"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MD 9783"),
		},
		.driver_data = keymap_wistron_ms2111
	},
	{
		/* Medion MD 40100 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDIONNB"),
			DMI_MATCH(DMI_PRODUCT_NAME, "WID2000"),
		},
		.driver_data = keymap_wistron_md40100
	},
	{
		/* Medion MD 2900 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDIONNB"),
			DMI_MATCH(DMI_PRODUCT_NAME, "WIM 2000"),
		},
		.driver_data = keymap_wistron_md2900
	},
	{
		/* Medion MD 42200 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Medion"),
			DMI_MATCH(DMI_PRODUCT_NAME, "WIM 2030"),
		},
		.driver_data = keymap_fs_amilo_pro_v2000
	},
	{
		/* Medion MD 96500 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDIONPC"),
			DMI_MATCH(DMI_PRODUCT_NAME, "WIM 2040"),
		},
		.driver_data = keymap_wistron_md96500
	},
	{
		/* Medion MD 95400 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDIONPC"),
			DMI_MATCH(DMI_PRODUCT_NAME, "WIM 2050"),
		},
		.driver_data = keymap_wistron_md96500
	},
	{
		/* Fujitsu Siemens Amilo D7820 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"), /* not sure */
			DMI_MATCH(DMI_PRODUCT_NAME, "Amilo D"),
		},
		.driver_data = keymap_fs_amilo_d88x0
	},
	{
		/* Fujitsu Siemens Amilo D88x0 */
		.callback = dmi_matched,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO D"),
		},
		.driver_data = keymap_fs_amilo_d88x0
	},
	{ NULL, }
};
MODULE_DEVICE_TABLE(dmi, dmi_ids);

/* Copy the good keymap, as the original ones are free'd */
static int __init copy_keymap(void)
{
	const struct key_entry *key;
	struct key_entry *new_keymap;
	unsigned int length = 1;

	for (key = keymap; key->type != KE_END; key++)
		length++;

	new_keymap = kmemdup_array(keymap, length, sizeof(struct key_entry),
				   GFP_KERNEL);
	if (!new_keymap)
		return -ENOMEM;

	keymap = new_keymap;

	return 0;
}

static int __init select_keymap(void)
{
	dmi_check_system(dmi_ids);
	if (keymap_name != NULL) {
		if (strcmp (keymap_name, "1557/MS2141") == 0)
			keymap = keymap_wistron_ms2141;
		else if (strcmp (keymap_name, "aopen1557") == 0)
			keymap = keymap_aopen_1557;
		else if (strcmp (keymap_name, "prestigio") == 0)
			keymap = keymap_prestigio;
		else if (strcmp (keymap_name, "generic") == 0)
			keymap = keymap_wistron_generic;
		else {
			printk(KERN_ERR "wistron_btns: Keymap unknown\n");
			return -EINVAL;
		}
	}
	if (keymap == NULL) {
		if (!force) {
			printk(KERN_ERR "wistron_btns: System unknown\n");
			return -ENODEV;
		}
		keymap = keymap_empty;
	}

	return copy_keymap();
}

 /* Input layer interface */

static struct input_dev *wistron_idev;
static unsigned long jiffies_last_press;
static bool wifi_enabled;
static bool bluetooth_enabled;

 /* led management */
static void wistron_mail_led_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	bios_set_state(MAIL_LED, (value != LED_OFF) ? 1 : 0);
}

/* same as setting up wifi card, but for laptops on which the led is managed */
static void wistron_wifi_led_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	bios_set_state(WIFI, (value != LED_OFF) ? 1 : 0);
}

static struct led_classdev wistron_mail_led = {
	.name			= "wistron:green:mail",
	.brightness_set		= wistron_mail_led_set,
};

static struct led_classdev wistron_wifi_led = {
	.name			= "wistron:red:wifi",
	.brightness_set		= wistron_wifi_led_set,
};

static void wistron_led_init(struct device *parent)
{
	if (leds_present & FE_WIFI_LED) {
		u16 wifi = bios_get_default_setting(WIFI);
		if (wifi & 1) {
			wistron_wifi_led.brightness = (wifi & 2) ? LED_FULL : LED_OFF;
			if (led_classdev_register(parent, &wistron_wifi_led))
				leds_present &= ~FE_WIFI_LED;
			else
				bios_set_state(WIFI, wistron_wifi_led.brightness);

		} else
			leds_present &= ~FE_WIFI_LED;
	}

	if (leds_present & FE_MAIL_LED) {
		/* bios_get_default_setting(MAIL) always returns 0, so just turn the led off */
		wistron_mail_led.brightness = LED_OFF;
		if (led_classdev_register(parent, &wistron_mail_led))
			leds_present &= ~FE_MAIL_LED;
		else
			bios_set_state(MAIL_LED, wistron_mail_led.brightness);
	}
}

static void wistron_led_remove(void)
{
	if (leds_present & FE_MAIL_LED)
		led_classdev_unregister(&wistron_mail_led);

	if (leds_present & FE_WIFI_LED)
		led_classdev_unregister(&wistron_wifi_led);
}

static inline void wistron_led_suspend(void)
{
	if (leds_present & FE_MAIL_LED)
		led_classdev_suspend(&wistron_mail_led);

	if (leds_present & FE_WIFI_LED)
		led_classdev_suspend(&wistron_wifi_led);
}

static inline void wistron_led_resume(void)
{
	if (leds_present & FE_MAIL_LED)
		led_classdev_resume(&wistron_mail_led);

	if (leds_present & FE_WIFI_LED)
		led_classdev_resume(&wistron_wifi_led);
}

static void handle_key(u8 code)
{
	const struct key_entry *key =
		sparse_keymap_entry_from_scancode(wistron_idev, code);

	if (key) {
		switch (key->type) {
		case KE_WIFI:
			if (have_wifi) {
				wifi_enabled = !wifi_enabled;
				bios_set_state(WIFI, wifi_enabled);
			}
			break;

		case KE_BLUETOOTH:
			if (have_bluetooth) {
				bluetooth_enabled = !bluetooth_enabled;
				bios_set_state(BLUETOOTH, bluetooth_enabled);
			}
			break;

		default:
			sparse_keymap_report_entry(wistron_idev, key, 1, true);
			break;
		}
		jiffies_last_press = jiffies;
	} else {
		printk(KERN_NOTICE
			"wistron_btns: Unknown key code %02X\n", code);
	}
}

static void poll_bios(bool discard)
{
	u8 qlen;
	u16 val;

	for (;;) {
		qlen = CMOS_READ(cmos_address);
		if (qlen == 0)
			break;
		val = bios_pop_queue();
		if (val != 0 && !discard)
			handle_key((u8)val);
	}
}

static int wistron_flush(struct input_dev *dev)
{
	/* Flush stale event queue */
	poll_bios(true);

	return 0;
}

static void wistron_poll(struct input_dev *dev)
{
	poll_bios(false);

	/* Increase poll frequency if user is currently pressing keys (< 2s ago) */
	if (time_before(jiffies, jiffies_last_press + 2 * HZ))
		input_set_poll_interval(dev, POLL_INTERVAL_BURST);
	else
		input_set_poll_interval(dev, POLL_INTERVAL_DEFAULT);
}

static int wistron_setup_keymap(struct input_dev *dev,
					  struct key_entry *entry)
{
	switch (entry->type) {

	/* if wifi or bluetooth are not available, create normal keys */
	case KE_WIFI:
		if (!have_wifi) {
			entry->type = KE_KEY;
			entry->keycode = KEY_WLAN;
		}
		break;

	case KE_BLUETOOTH:
		if (!have_bluetooth) {
			entry->type = KE_KEY;
			entry->keycode = KEY_BLUETOOTH;
		}
		break;

	case KE_END:
		if (entry->code & FE_UNTESTED)
			printk(KERN_WARNING "Untested laptop multimedia keys, "
				"please report success or failure to "
				"eric.piel@tremplin-utc.net\n");
		break;
	}

	return 0;
}

static int setup_input_dev(void)
{
	int error;

	wistron_idev = input_allocate_device();
	if (!wistron_idev)
		return -ENOMEM;

	wistron_idev->name = "Wistron laptop buttons";
	wistron_idev->phys = "wistron/input0";
	wistron_idev->id.bustype = BUS_HOST;
	wistron_idev->dev.parent = &wistron_device->dev;

	wistron_idev->open = wistron_flush;

	error = sparse_keymap_setup(wistron_idev, keymap, wistron_setup_keymap);
	if (error)
		goto err_free_dev;

	error = input_setup_polling(wistron_idev, wistron_poll);
	if (error)
		goto err_free_dev;

	input_set_poll_interval(wistron_idev, POLL_INTERVAL_DEFAULT);

	error = input_register_device(wistron_idev);
	if (error)
		goto err_free_dev;

	return 0;

 err_free_dev:
	input_free_device(wistron_idev);
	return error;
}

/* Driver core */

static int wistron_probe(struct platform_device *dev)
{
	int err;

	bios_attach();
	cmos_address = bios_get_cmos_address();

	if (have_wifi) {
		u16 wifi = bios_get_default_setting(WIFI);
		if (wifi & 1)
			wifi_enabled = wifi & 2;
		else
			have_wifi = 0;

		if (have_wifi)
			bios_set_state(WIFI, wifi_enabled);
	}

	if (have_bluetooth) {
		u16 bt = bios_get_default_setting(BLUETOOTH);
		if (bt & 1)
			bluetooth_enabled = bt & 2;
		else
			have_bluetooth = false;

		if (have_bluetooth)
			bios_set_state(BLUETOOTH, bluetooth_enabled);
	}

	wistron_led_init(&dev->dev);

	err = setup_input_dev();
	if (err) {
		bios_detach();
		return err;
	}

	return 0;
}

static void wistron_remove(struct platform_device *dev)
{
	wistron_led_remove();
	input_unregister_device(wistron_idev);
	bios_detach();
}

static int wistron_suspend(struct device *dev)
{
	if (have_wifi)
		bios_set_state(WIFI, 0);

	if (have_bluetooth)
		bios_set_state(BLUETOOTH, 0);

	wistron_led_suspend();

	return 0;
}

static int wistron_resume(struct device *dev)
{
	if (have_wifi)
		bios_set_state(WIFI, wifi_enabled);

	if (have_bluetooth)
		bios_set_state(BLUETOOTH, bluetooth_enabled);

	wistron_led_resume();

	poll_bios(true);

	return 0;
}

static const struct dev_pm_ops wistron_pm_ops = {
	.suspend	= wistron_suspend,
	.resume		= wistron_resume,
	.poweroff	= wistron_suspend,
	.restore	= wistron_resume,
};

static struct platform_driver wistron_driver = {
	.driver		= {
		.name	= "wistron-bios",
		.pm	= pm_sleep_ptr(&wistron_pm_ops),
	},
	.probe		= wistron_probe,
	.remove_new	= wistron_remove,
};

static int __init wb_module_init(void)
{
	int err;

	err = select_keymap();
	if (err)
		return err;

	err = map_bios();
	if (err)
		goto err_free_keymap;

	err = platform_driver_register(&wistron_driver);
	if (err)
		goto err_unmap_bios;

	wistron_device = platform_device_alloc("wistron-bios", -1);
	if (!wistron_device) {
		err = -ENOMEM;
		goto err_unregister_driver;
	}

	err = platform_device_add(wistron_device);
	if (err)
		goto err_free_device;

	return 0;

 err_free_device:
	platform_device_put(wistron_device);
 err_unregister_driver:
	platform_driver_unregister(&wistron_driver);
 err_unmap_bios:
	unmap_bios();
 err_free_keymap:
	kfree(keymap);

	return err;
}

static void __exit wb_module_exit(void)
{
	platform_device_unregister(wistron_device);
	platform_driver_unregister(&wistron_driver);
	unmap_bios();
	kfree(keymap);
}

module_init(wb_module_init);
module_exit(wb_module_exit);
