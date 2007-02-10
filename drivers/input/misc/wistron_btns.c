/*
 * Wistron laptop button driver
 * Copyright (C) 2005 Miloslav Trmac <mitr@volny.cz>
 * Copyright (C) 2005 Bernhard Rosenkraenzer <bero@arklinux.org>
 * Copyright (C) 2005 Dmitry Torokhov <dtor@mail.ru>
 *
 * You can redistribute and/or modify this program under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place Suite 330, Boston, MA 02111-1307, USA.
 */
#include <linux/io.h>
#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mc146818rtc.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/platform_device.h>

/*
 * Number of attempts to read data from queue per poll;
 * the queue can hold up to 31 entries
 */
#define MAX_POLL_ITERATIONS 64

#define POLL_FREQUENCY 10 /* Number of polls per second */

#if POLL_FREQUENCY > HZ
#error "POLL_FREQUENCY too high"
#endif

/* BIOS subsystem IDs */
#define WIFI		0x35
#define BLUETOOTH	0x34

MODULE_AUTHOR("Miloslav Trmac <mitr@volny.cz>");
MODULE_DESCRIPTION("Wistron laptop button driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.1");

static int force; /* = 0; */
module_param(force, bool, 0);
MODULE_PARM_DESC(force, "Load even if computer is not in database");

static char *keymap_name; /* = NULL; */
module_param_named(keymap, keymap_name, charp, 0);
MODULE_PARM_DESC(keymap, "Keymap name, if it can't be autodetected");

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

static void __devinit bios_attach(void)
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

static u8 __devinit bios_get_cmos_address(void)
{
	struct regs regs;

	memset(&regs, 0, sizeof (regs));
	regs.eax = 0x9610;
	regs.ebx = 0x051C;
	call_bios(&regs);

	return regs.ecx;
}

static u16 __devinit bios_get_default_setting(u8 subsys)
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

struct key_entry {
	char type;		/* See KE_* below */
	u8 code;
	unsigned keycode;	/* For KE_KEY */
};

enum { KE_END, KE_KEY, KE_WIFI, KE_BLUETOOTH };

static const struct key_entry *keymap; /* = NULL; Current key map */
static int have_wifi;
static int have_bluetooth;

static int __init dmi_matched(struct dmi_system_id *dmi)
{
	const struct key_entry *key;

	keymap = dmi->driver_data;
	for (key = keymap; key->type != KE_END; key++) {
		if (key->type == KE_WIFI)
			have_wifi = 1;
		else if (key->type == KE_BLUETOOTH)
			have_bluetooth = 1;
	}
	return 1;
}

static struct key_entry keymap_empty[] = {
	{ KE_END, 0 }
};

static struct key_entry keymap_fs_amilo_pro_v2000[] = {
	{ KE_KEY,  0x01, KEY_HELP },
	{ KE_KEY,  0x11, KEY_PROG1 },
	{ KE_KEY,  0x12, KEY_PROG2 },
	{ KE_WIFI, 0x30, 0 },
	{ KE_KEY,  0x31, KEY_MAIL },
	{ KE_KEY,  0x36, KEY_WWW },
	{ KE_END,  0 }
};

static struct key_entry keymap_fujitsu_n3510[] = {
	{ KE_KEY, 0x11, KEY_PROG1 },
	{ KE_KEY, 0x12, KEY_PROG2 },
	{ KE_KEY, 0x36, KEY_WWW },
	{ KE_KEY, 0x31, KEY_MAIL },
	{ KE_KEY, 0x71, KEY_STOPCD },
	{ KE_KEY, 0x72, KEY_PLAYPAUSE },
	{ KE_KEY, 0x74, KEY_REWIND },
	{ KE_KEY, 0x78, KEY_FORWARD },
	{ KE_END, 0 }
};

static struct key_entry keymap_wistron_ms2111[] = {
	{ KE_KEY,  0x11, KEY_PROG1 },
	{ KE_KEY,  0x12, KEY_PROG2 },
	{ KE_KEY,  0x13, KEY_PROG3 },
	{ KE_KEY,  0x31, KEY_MAIL },
	{ KE_KEY,  0x36, KEY_WWW },
	{ KE_END,  0 }
};

static struct key_entry keymap_wistron_ms2141[] = {
	{ KE_KEY,  0x11, KEY_PROG1 },
	{ KE_KEY,  0x12, KEY_PROG2 },
	{ KE_WIFI, 0x30, 0 },
	{ KE_KEY,  0x22, KEY_REWIND },
	{ KE_KEY,  0x23, KEY_FORWARD },
	{ KE_KEY,  0x24, KEY_PLAYPAUSE },
	{ KE_KEY,  0x25, KEY_STOPCD },
	{ KE_KEY,  0x31, KEY_MAIL },
	{ KE_KEY,  0x36, KEY_WWW },
	{ KE_END,  0 }
};

static struct key_entry keymap_acer_aspire_1500[] = {
	{ KE_KEY, 0x11, KEY_PROG1 },
	{ KE_KEY, 0x12, KEY_PROG2 },
	{ KE_WIFI, 0x30, 0 },
	{ KE_KEY, 0x31, KEY_MAIL },
	{ KE_KEY, 0x36, KEY_WWW },
	{ KE_BLUETOOTH, 0x44, 0 },
	{ KE_END, 0 }
};

static struct key_entry keymap_acer_travelmate_240[] = {
	{ KE_KEY, 0x31, KEY_MAIL },
	{ KE_KEY, 0x36, KEY_WWW },
	{ KE_KEY, 0x11, KEY_PROG1 },
	{ KE_KEY, 0x12, KEY_PROG2 },
	{ KE_BLUETOOTH, 0x44, 0 },
	{ KE_WIFI, 0x30, 0 },
	{ KE_END, 0 }
};

static struct key_entry keymap_aopen_1559as[] = {
	{ KE_KEY,  0x01, KEY_HELP },
	{ KE_KEY,  0x06, KEY_PROG3 },
	{ KE_KEY,  0x11, KEY_PROG1 },
	{ KE_KEY,  0x12, KEY_PROG2 },
	{ KE_WIFI, 0x30, 0 },
	{ KE_KEY,  0x31, KEY_MAIL },
	{ KE_KEY,  0x36, KEY_WWW },
	{ KE_END,  0 },
};

static struct key_entry keymap_fs_amilo_d88x0[] = {
	{ KE_KEY, 0x01, KEY_HELP },
	{ KE_KEY, 0x08, KEY_MUTE },
	{ KE_KEY, 0x31, KEY_MAIL },
	{ KE_KEY, 0x36, KEY_WWW },
	{ KE_KEY, 0x11, KEY_PROG1 },
	{ KE_KEY, 0x12, KEY_PROG2 },
	{ KE_KEY, 0x13, KEY_PROG3 },
	{ KE_END, 0 }
};

/*
 * If your machine is not here (which is currently rather likely), please send
 * a list of buttons and their key codes (reported when loading this module
 * with force=1) and the output of dmidecode to $MODULE_AUTHOR.
 */
static struct dmi_system_id dmi_ids[] __initdata = {
	{
		.callback = dmi_matched,
		.ident = "Fujitsu-Siemens Amilo Pro V2000",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Pro V2000"),
		},
		.driver_data = keymap_fs_amilo_pro_v2000
	},
	{
		.callback = dmi_matched,
		.ident = "Fujitsu-Siemens Amilo M7400",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO M        "),
		},
		.driver_data = keymap_fs_amilo_pro_v2000
	},
	{
		.callback = dmi_matched,
		.ident = "Fujitsu N3510",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "N3510"),
		},
		.driver_data = keymap_fujitsu_n3510
	},
	{
		.callback = dmi_matched,
		.ident = "Acer Aspire 1500",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 1500"),
		},
		.driver_data = keymap_acer_aspire_1500
	},
	{
		.callback = dmi_matched,
		.ident = "Acer TravelMate 240",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 240"),
		},
		.driver_data = keymap_acer_travelmate_240
	},
	{
		.callback = dmi_matched,
		.ident = "Acer TravelMate 2424NWXCi",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 2420"),
		},
		.driver_data = keymap_acer_travelmate_240
	},
	{
		.callback = dmi_matched,
		.ident = "AOpen 1559AS",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "E2U"),
			DMI_MATCH(DMI_BOARD_NAME, "E2U"),
		},
		.driver_data = keymap_aopen_1559as
	},
	{
		.callback = dmi_matched,
		.ident = "Medion MD 9783",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDIONNB"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MD 9783"),
		},
		.driver_data = keymap_wistron_ms2111
	},
	{
		.callback = dmi_matched,
		.ident = "Fujitsu Siemens Amilo D88x0",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO D"),
		},
		.driver_data = keymap_fs_amilo_d88x0
	},
	{ NULL, }
};

static int __init select_keymap(void)
{
	if (keymap_name != NULL) {
		if (strcmp (keymap_name, "1557/MS2141") == 0)
			keymap = keymap_wistron_ms2141;
		else {
			printk(KERN_ERR "wistron_btns: Keymap unknown\n");
			return -EINVAL;
		}
	}
	dmi_check_system(dmi_ids);
	if (keymap == NULL) {
		if (!force) {
			printk(KERN_ERR "wistron_btns: System unknown\n");
			return -ENODEV;
		}
		keymap = keymap_empty;
	}
	return 0;
}

 /* Input layer interface */

static struct input_dev *input_dev;

static int __devinit setup_input_dev(void)
{
	const struct key_entry *key;
	int error;

	input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	input_dev->name = "Wistron laptop buttons";
	input_dev->phys = "wistron/input0";
	input_dev->id.bustype = BUS_HOST;
	input_dev->cdev.dev = &wistron_device->dev;

	for (key = keymap; key->type != KE_END; key++) {
		if (key->type == KE_KEY) {
			input_dev->evbit[LONG(EV_KEY)] = BIT(EV_KEY);
			set_bit(key->keycode, input_dev->keybit);
		}
	}

	error = input_register_device(input_dev);
	if (error) {
		input_free_device(input_dev);
		return error;
	}

	return 0;
}

static void report_key(unsigned keycode)
{
	input_report_key(input_dev, keycode, 1);
	input_sync(input_dev);
	input_report_key(input_dev, keycode, 0);
	input_sync(input_dev);
}

 /* Driver core */

static int wifi_enabled;
static int bluetooth_enabled;

static void poll_bios(unsigned long);

static struct timer_list poll_timer = TIMER_INITIALIZER(poll_bios, 0, 0);

static void handle_key(u8 code)
{
	const struct key_entry *key;

	for (key = keymap; key->type != KE_END; key++) {
		if (code == key->code) {
			switch (key->type) {
			case KE_KEY:
				report_key(key->keycode);
				break;

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

			case KE_END:
			default:
				BUG();
			}
			return;
		}
	}
	printk(KERN_NOTICE "wistron_btns: Unknown key code %02X\n", code);
}

static void poll_bios(unsigned long discard)
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

	mod_timer(&poll_timer, jiffies + HZ / POLL_FREQUENCY);
}

static int __devinit wistron_probe(struct platform_device *dev)
{
	int err = setup_input_dev();
	if (err)
		return err;

	bios_attach();
	cmos_address = bios_get_cmos_address();

	if (have_wifi) {
		u16 wifi = bios_get_default_setting(WIFI);
		if (wifi & 1)
			wifi_enabled = (wifi & 2) ? 1 : 0;
		else
			have_wifi = 0;

		if (have_wifi)
			bios_set_state(WIFI, wifi_enabled);
	}

	if (have_bluetooth) {
		u16 bt = bios_get_default_setting(BLUETOOTH);
		if (bt & 1)
			bluetooth_enabled = (bt & 2) ? 1 : 0;
		else
			have_bluetooth = 0;

		if (have_bluetooth)
			bios_set_state(BLUETOOTH, bluetooth_enabled);
	}

	poll_bios(1); /* Flush stale event queue and arm timer */

	return 0;
}

static int __devexit wistron_remove(struct platform_device *dev)
{
	del_timer_sync(&poll_timer);
	input_unregister_device(input_dev);
	bios_detach();

	return 0;
}

#ifdef CONFIG_PM
static int wistron_suspend(struct platform_device *dev, pm_message_t state)
{
	del_timer_sync(&poll_timer);

	if (have_wifi)
		bios_set_state(WIFI, 0);

	if (have_bluetooth)
		bios_set_state(BLUETOOTH, 0);

	return 0;
}

static int wistron_resume(struct platform_device *dev)
{
	if (have_wifi)
		bios_set_state(WIFI, wifi_enabled);

	if (have_bluetooth)
		bios_set_state(BLUETOOTH, bluetooth_enabled);

	poll_bios(1);

	return 0;
}
#else
#define wistron_suspend		NULL
#define wistron_resume		NULL
#endif

static struct platform_driver wistron_driver = {
	.driver		= {
		.name	= "wistron-bios",
		.owner	= THIS_MODULE,
	},
	.probe		= wistron_probe,
	.remove		= __devexit_p(wistron_remove),
	.suspend	= wistron_suspend,
	.resume		= wistron_resume,
};

static int __init wb_module_init(void)
{
	int err;

	err = select_keymap();
	if (err)
		return err;

	err = map_bios();
	if (err)
		return err;

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

	return err;
}

static void __exit wb_module_exit(void)
{
	platform_device_unregister(wistron_device);
	platform_driver_unregister(&wistron_driver);
	unmap_bios();
}

module_init(wb_module_init);
module_exit(wb_module_exit);
