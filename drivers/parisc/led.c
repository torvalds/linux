// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Chassis LCD/LED driver for HP-PARISC workstations
 *
 *	(c) Copyright 2000 Red Hat Software
 *	(c) Copyright 2000 Helge Deller <hdeller@redhat.com>
 *	(c) Copyright 2001 Randolph Chung <tausq@debian.org>
 *	(c) Copyright 2000-2023 Helge Deller <deller@gmx.de>
 *
 *	The control of the LEDs and LCDs on PARISC machines has to be done
 *	completely in software.
 *
 *	The LEDs can be configured at runtime in /sys/class/leds/
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/utsname.h>
#include <linux/capability.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>
#include <linux/leds.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/processor.h>
#include <asm/hardware.h>
#include <asm/param.h>		/* HZ */
#include <asm/led.h>
#include <asm/pdc.h>

#define LED_HAS_LCD 1
#define LED_HAS_LED 2

static unsigned char led_type;		/* bitmask of LED_HAS_XXX */
static unsigned char lastleds;		/* LED state from most recent update */
static unsigned char lcd_new_text;
static unsigned char lcd_text[20];
static unsigned char lcd_text_default[20];
static unsigned char lcd_no_led_support; /* KittyHawk doesn't support LED on its LCD */

struct lcd_block {
	unsigned char command;	/* stores the command byte      */
	unsigned char on;	/* value for turning LED on     */
	unsigned char off;	/* value for turning LED off    */
};

/* Structure returned by PDC_RETURN_CHASSIS_INFO */
/* NOTE: we use unsigned long:16 two times, since the following member
   lcd_cmd_reg_addr needs to be 64bit aligned on 64bit PA2.0-machines */
struct pdc_chassis_lcd_info_ret_block {
	unsigned long model:16;		/* DISPLAY_MODEL_XXXX */
	unsigned long lcd_width:16;	/* width of the LCD in chars (DISPLAY_MODEL_LCD only) */
	unsigned long lcd_cmd_reg_addr;	/* ptr to LCD cmd-register & data ptr for LED */
	unsigned long lcd_data_reg_addr; /* ptr to LCD data-register (LCD only) */
	unsigned int min_cmd_delay;	/* delay in uS after cmd-write (LCD only) */
	unsigned char reset_cmd1;	/* command #1 for writing LCD string (LCD only) */
	unsigned char reset_cmd2;	/* command #2 for writing LCD string (LCD only) */
	unsigned char act_enable;	/* 0 = no activity (LCD only) */
	struct lcd_block heartbeat;
	struct lcd_block disk_io;
	struct lcd_block lan_rcv;
	struct lcd_block lan_tx;
	char _pad;
};


/* LCD_CMD and LCD_DATA for KittyHawk machines */
#define KITTYHAWK_LCD_CMD  F_EXTEND(0xf0190000UL)
#define KITTYHAWK_LCD_DATA (KITTYHAWK_LCD_CMD + 1)

/* lcd_info is pre-initialized to the values needed to program KittyHawk LCD's
 * HP seems to have used Sharp/Hitachi HD44780 LCDs most of the time. */
static struct pdc_chassis_lcd_info_ret_block
lcd_info __attribute__((aligned(8)))  =
{
	.model =		DISPLAY_MODEL_NONE,
	.lcd_width =		16,
	.lcd_cmd_reg_addr =	KITTYHAWK_LCD_CMD,
	.lcd_data_reg_addr =	KITTYHAWK_LCD_DATA,
	.min_cmd_delay =	80,
	.reset_cmd1 =		0x80,
	.reset_cmd2 =		0xc0,
};

/* direct access to some of the lcd_info variables */
#define LCD_CMD_REG	lcd_info.lcd_cmd_reg_addr
#define LCD_DATA_REG	lcd_info.lcd_data_reg_addr
#define LED_DATA_REG	lcd_info.lcd_cmd_reg_addr	/* LASI & ASP only */

/* ptr to LCD/LED-specific function */
static void (*led_func_ptr) (unsigned char);


static void lcd_print_now(void)
{
	int i;
	char *str = lcd_text;

	if (lcd_info.model != DISPLAY_MODEL_LCD)
		return;

	if (!lcd_new_text)
		return;
	lcd_new_text = 0;

	/* Set LCD Cursor to 1st character */
	gsc_writeb(lcd_info.reset_cmd1, LCD_CMD_REG);
	udelay(lcd_info.min_cmd_delay);

	/* Print the string */
	for (i = 0; i < lcd_info.lcd_width; i++) {
		gsc_writeb(*str ? *str++ : ' ', LCD_DATA_REG);
		udelay(lcd_info.min_cmd_delay);
	}
}

/**
 *	lcd_print()
 *
 *	@str: string to show on the LCD. If NULL, print current string again.
 *
 *	Displays the given string on the LCD-Display of newer machines.
 */
void lcd_print(const char *str)
{
	/* copy display string to buffer for procfs */
	if (str)
		strscpy(lcd_text, str, sizeof(lcd_text));
	lcd_new_text = 1;

	/* print now if LCD without any LEDs */
	if (led_type == LED_HAS_LCD)
		lcd_print_now();
}

#define	LED_DATA	0x01	/* data to shift (0:on 1:off) */
#define	LED_STROBE	0x02	/* strobe to clock data */

/**
 *	led_ASP_driver() - LED driver for the ASP controller chip
 *
 *	@leds: bitmap representing the LED status
 */
static void led_ASP_driver(unsigned char leds)
{
	int i;

	leds = ~leds;
	for (i = 0; i < 8; i++) {
		unsigned char value;
		value = (leds & 0x80) >> 7;
		gsc_writeb( value,		 LED_DATA_REG );
		gsc_writeb( value | LED_STROBE,	 LED_DATA_REG );
		leds <<= 1;
	}
}

/**
 *	led_LASI_driver() - LED driver for the LASI controller chip
 *
 *	@leds: bitmap representing the LED status
 */
static void led_LASI_driver(unsigned char leds)
{
	leds = ~leds;
	gsc_writeb( leds, LED_DATA_REG );
}

/**
 *	led_LCD_driver() - LED & LCD driver for LCD chips
 *
 *	@leds: bitmap representing the LED status
 */
static void led_LCD_driver(unsigned char leds)
{
	static const unsigned char mask[4] = {
		LED_HEARTBEAT, LED_DISK_IO,
		LED_LAN_RCV, LED_LAN_TX };

	static struct lcd_block * const blockp[4] = {
		&lcd_info.heartbeat,
		&lcd_info.disk_io,
		&lcd_info.lan_rcv,
		&lcd_info.lan_tx
	};
	static unsigned char latest_leds;
	int i;

	for (i = 0; i < 4; ++i) {
		if ((leds & mask[i]) == (latest_leds & mask[i]))
			continue;

		gsc_writeb( blockp[i]->command, LCD_CMD_REG );
		udelay(lcd_info.min_cmd_delay);

		gsc_writeb( leds & mask[i] ? blockp[i]->on :
				blockp[i]->off, LCD_DATA_REG );
		udelay(lcd_info.min_cmd_delay);
	}
	latest_leds = leds;

	lcd_print_now();
}


/**
 *	lcd_system_halt()
 *
 *	@nb: pointer to the notifier_block structure
 *	@event: the event (SYS_RESTART, SYS_HALT or SYS_POWER_OFF)
 *	@buf: pointer to a buffer (not used)
 *
 *	Called by the reboot notifier chain at shutdown. Stops all
 *	LED/LCD activities.
 */
static int lcd_system_halt(struct notifier_block *nb, unsigned long event, void *buf)
{
	const char *txt;

	switch (event) {
	case SYS_RESTART:	txt = "SYSTEM RESTART";
				break;
	case SYS_HALT:		txt = "SYSTEM HALT";
				break;
	case SYS_POWER_OFF:	txt = "SYSTEM POWER OFF";
				break;
	default:		return NOTIFY_DONE;
	}

	lcd_print(txt);

	return NOTIFY_OK;
}

static struct notifier_block lcd_system_halt_notifier = {
	.notifier_call = lcd_system_halt,
};

static void set_led(struct led_classdev *led_cdev, enum led_brightness brightness);

struct hppa_led {
	struct led_classdev	led_cdev;
	unsigned char		led_bit;
};
#define to_hppa_led(d) container_of(d, struct hppa_led, led_cdev)

typedef void (*set_handler)(struct led_classdev *, enum led_brightness);
struct led_type {
	const char	*name;
	set_handler	handler;
	const char	*default_trigger;
};

#define NUM_LEDS_PER_BOARD	8
struct hppa_drvdata {
	struct hppa_led	leds[NUM_LEDS_PER_BOARD];
};

static void set_led(struct led_classdev *led_cdev, enum led_brightness brightness)
{
	struct hppa_led *p = to_hppa_led(led_cdev);
	unsigned char led_bit = p->led_bit;

	if (brightness == LED_OFF)
		lastleds &= ~led_bit;
	else
		lastleds |= led_bit;

	if (led_func_ptr)
		led_func_ptr(lastleds);
}


static int hppa_led_generic_probe(struct platform_device *pdev,
				  struct led_type *types)
{
	struct hppa_drvdata *p;
	int i, err;

	p = devm_kzalloc(&pdev->dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	for (i = 0; i < NUM_LEDS_PER_BOARD; i++) {
		struct led_classdev *lp = &p->leds[i].led_cdev;

		p->leds[i].led_bit = BIT(i);
		lp->name = types[i].name;
		lp->brightness = LED_FULL;
		lp->brightness_set = types[i].handler;
		lp->default_trigger = types[i].default_trigger;
		err = led_classdev_register(&pdev->dev, lp);
		if (err) {
			dev_err(&pdev->dev, "Could not register %s LED\n",
			       lp->name);
			for (i--; i >= 0; i--)
				led_classdev_unregister(&p->leds[i].led_cdev);
			return err;
		}
	}

	platform_set_drvdata(pdev, p);

	return 0;
}

static int platform_led_remove(struct platform_device *pdev)
{
	struct hppa_drvdata *p = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < NUM_LEDS_PER_BOARD; i++)
		led_classdev_unregister(&p->leds[i].led_cdev);

	return 0;
}

static struct led_type mainboard_led_types[NUM_LEDS_PER_BOARD] = {
	{
		.name		= "platform-lan-tx",
		.handler	= set_led,
		.default_trigger = "tx",
	},
	{
		.name		= "platform-lan-rx",
		.handler	= set_led,
		.default_trigger = "rx",
	},
	{
		.name		= "platform-disk",
		.handler	= set_led,
		.default_trigger = "disk-activity",
	},
	{
		.name		= "platform-heartbeat",
		.handler	= set_led,
		.default_trigger = "heartbeat",
	},
	{
		.name		= "platform-LED4",
		.handler	= set_led,
		.default_trigger = "panic",
	},
	{
		.name		= "platform-LED5",
		.handler	= set_led,
		.default_trigger = "panic",
	},
	{
		.name		= "platform-LED6",
		.handler	= set_led,
		.default_trigger = "panic",
	},
	{
		.name		= "platform-LED7",
		.handler	= set_led,
		.default_trigger = "panic",
	},
};

static int platform_led_probe(struct platform_device *pdev)
{
	return hppa_led_generic_probe(pdev, mainboard_led_types);
}

MODULE_ALIAS("platform:platform-leds");

static struct platform_driver hppa_mainboard_led_driver = {
	.probe		= platform_led_probe,
	.remove		= platform_led_remove,
	.driver		= {
		.name	= "platform-leds",
	},
};

static struct platform_driver * const drivers[] = {
	&hppa_mainboard_led_driver,
};

static struct platform_device platform_leds = {
	.name = "platform-leds",
};

/**
 *	register_led_driver()
 *
 *	@model: model type, one of the DISPLAY_MODEL_XXXX values
 *	@cmd_reg: physical address of cmd register for the LED/LCD
 *	@data_reg: physical address of data register for the LED/LCD
 *
 *	Registers a chassis LED or LCD which should be driven by this driver.
 *	Only PDC-based, LASI- or ASP-style LEDs and LCDs are supported.
 */
int __init register_led_driver(int model, unsigned long cmd_reg, unsigned long data_reg)
{
	if (led_func_ptr || !data_reg)
		return 1;

	/* No LEDs when running in QEMU */
	if (running_on_qemu)
		return 1;

	lcd_info.model = model;		/* store the values */
	LCD_CMD_REG = (cmd_reg == LED_CMD_REG_NONE) ? 0 : cmd_reg;

	switch (lcd_info.model) {
	case DISPLAY_MODEL_LCD:
		LCD_DATA_REG = data_reg;
		pr_info("led: LCD display at %#lx and %#lx\n",
			LCD_CMD_REG , LCD_DATA_REG);
		led_func_ptr = led_LCD_driver;
		if (lcd_no_led_support)
			led_type = LED_HAS_LCD;
		else
			led_type = LED_HAS_LCD | LED_HAS_LED;
		break;

	case DISPLAY_MODEL_LASI:
		LED_DATA_REG = data_reg;
		led_func_ptr = led_LASI_driver;
		pr_info("led: LED display at %#lx\n", LED_DATA_REG);
		led_type = LED_HAS_LED;
		break;

	case DISPLAY_MODEL_OLD_ASP:
		LED_DATA_REG = data_reg;
		led_func_ptr = led_ASP_driver;
		pr_info("led: LED (ASP-style) display at %#lx\n",
		    LED_DATA_REG);
		led_type = LED_HAS_LED;
		break;

	default:
		pr_err("led: Unknown LCD/LED model type %d\n", lcd_info.model);
		return 1;
	}

	platform_register_drivers(drivers, ARRAY_SIZE(drivers));

	return register_reboot_notifier(&lcd_system_halt_notifier);
}

/**
 *	early_led_init()
 *
 *	early_led_init() is called early in the bootup-process and asks the
 *	PDC for an usable chassis LCD or LED. If the PDC doesn't return any
 *	info, then a LED might be detected by the LASI or ASP drivers later.
 *	KittyHawk machines have often a buggy PDC, so that we explicitly check
 *	for those machines here.
 */
static int __init early_led_init(void)
{
	struct pdc_chassis_info chassis_info;
	int ret;

	snprintf(lcd_text_default, sizeof(lcd_text_default),
		"Linux %s", init_utsname()->release);
	strcpy(lcd_text, lcd_text_default);
	lcd_new_text = 1;

	/* Work around the buggy PDC of KittyHawk-machines */
	switch (CPU_HVERSION) {
	case 0x580:		/* KittyHawk DC2-100 (K100) */
	case 0x581:		/* KittyHawk DC3-120 (K210) */
	case 0x582:		/* KittyHawk DC3 100 (K400) */
	case 0x583:		/* KittyHawk DC3 120 (K410) */
	case 0x58B:		/* KittyHawk DC2 100 (K200) */
		pr_info("LCD on KittyHawk-Machine found.\n");
		lcd_info.model = DISPLAY_MODEL_LCD;
		/* KittyHawk has no LED support on its LCD, so skip LED detection */
		lcd_no_led_support = 1;
		goto found;	/* use the preinitialized values of lcd_info */
	}

	/* initialize the struct, so that we can check for valid return values */
	chassis_info.actcnt = chassis_info.maxcnt = 0;

	ret = pdc_chassis_info(&chassis_info, &lcd_info, sizeof(lcd_info));
	if (ret != PDC_OK) {
not_found:
		lcd_info.model = DISPLAY_MODEL_NONE;
		return 1;
	}

	/* check the results. Some machines have a buggy PDC */
	if (chassis_info.actcnt <= 0 || chassis_info.actcnt != chassis_info.maxcnt)
		goto not_found;

	switch (lcd_info.model) {
	case DISPLAY_MODEL_LCD:		/* LCD display */
		if (chassis_info.actcnt <
			offsetof(struct pdc_chassis_lcd_info_ret_block, _pad)-1)
			goto not_found;
		if (!lcd_info.act_enable) {
			/* PDC tells LCD should not be used. */
			goto not_found;
		}
		break;

	case DISPLAY_MODEL_NONE:	/* no LED or LCD available */
		goto not_found;

	case DISPLAY_MODEL_LASI:	/* Lasi style 8 bit LED display */
		if (chassis_info.actcnt != 8 && chassis_info.actcnt != 32)
			goto not_found;
		break;

	default:
		pr_warn("PDC reported unknown LCD/LED model %d\n",
		       lcd_info.model);
		goto not_found;
	}

found:
	/* register the LCD/LED driver */
	return register_led_driver(lcd_info.model, LCD_CMD_REG, LCD_DATA_REG);
}
arch_initcall(early_led_init);

/**
 *	register_led_regions()
 *
 *	Register_led_regions() registers the LCD/LED regions for /procfs.
 *	At bootup - where the initialisation of the LCD/LED often happens
 *	not all internal structures of request_region() are properly set up,
 *	so that we delay the led-registration until after busdevices_init()
 *	has been executed.
 */
static void __init register_led_regions(void)
{
	switch (lcd_info.model) {
	case DISPLAY_MODEL_LCD:
		request_mem_region((unsigned long)LCD_CMD_REG,  1, "lcd_cmd");
		request_mem_region((unsigned long)LCD_DATA_REG, 1, "lcd_data");
		break;
	case DISPLAY_MODEL_LASI:
	case DISPLAY_MODEL_OLD_ASP:
		request_mem_region((unsigned long)LED_DATA_REG, 1, "led_data");
		break;
	}
}

static int __init startup_leds(void)
{
	if (platform_device_register(&platform_leds))
                printk(KERN_INFO "LED: failed to register LEDs\n");
	register_led_regions();
	return 0;
}
device_initcall(startup_leds);
