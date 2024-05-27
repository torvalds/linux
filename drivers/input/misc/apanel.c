// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Fujitsu Lifebook Application Panel button drive
 *
 *  Copyright (C) 2007 Stephen Hemminger <shemminger@linux-foundation.org>
 *  Copyright (C) 2001-2003 Jochen Eisinger <jochen@penguin-breeder.org>
 *
 * Many Fujitsu Lifebook laptops have a small panel of buttons that are
 * accessible via the i2c/smbus interface. This driver polls those
 * buttons and generates input events.
 *
 * For more details see:
 *	http://apanel.sourceforge.net/tech.php
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/leds.h>

#define APANEL_NAME	"Fujitsu Application Panel"
#define APANEL		"apanel"

/* How often we poll keys - msecs */
#define POLL_INTERVAL_DEFAULT	1000

/* Magic constants in BIOS that tell about buttons */
enum apanel_devid {
	APANEL_DEV_NONE	  = 0,
	APANEL_DEV_APPBTN = 1,
	APANEL_DEV_CDBTN  = 2,
	APANEL_DEV_LCD	  = 3,
	APANEL_DEV_LED	  = 4,

	APANEL_DEV_MAX,
};

enum apanel_chip {
	CHIP_NONE    = 0,
	CHIP_OZ992C  = 1,
	CHIP_OZ163T  = 2,
	CHIP_OZ711M3 = 4,
};

/* Result of BIOS snooping/probing -- what features are supported */
static enum apanel_chip device_chip[APANEL_DEV_MAX];

#define MAX_PANEL_KEYS	12

struct apanel {
	struct input_dev *idev;
	struct i2c_client *client;
	unsigned short keymap[MAX_PANEL_KEYS];
	u16 nkeys;
	struct led_classdev mail_led;
};

static const unsigned short apanel_keymap[MAX_PANEL_KEYS] = {
	[0] = KEY_MAIL,
	[1] = KEY_WWW,
	[2] = KEY_PROG2,
	[3] = KEY_PROG1,

	[8] = KEY_FORWARD,
	[9] = KEY_REWIND,
	[10] = KEY_STOPCD,
	[11] = KEY_PLAYPAUSE,
};

static void report_key(struct input_dev *input, unsigned keycode)
{
	dev_dbg(input->dev.parent, "report key %#x\n", keycode);
	input_report_key(input, keycode, 1);
	input_sync(input);

	input_report_key(input, keycode, 0);
	input_sync(input);
}

/* Poll for key changes
 *
 * Read Application keys via SMI
 *  A (0x4), B (0x8), Internet (0x2), Email (0x1).
 *
 * CD keys:
 * Forward (0x100), Rewind (0x200), Stop (0x400), Pause (0x800)
 */
static void apanel_poll(struct input_dev *idev)
{
	struct apanel *ap = input_get_drvdata(idev);
	u8 cmd = device_chip[APANEL_DEV_APPBTN] == CHIP_OZ992C ? 0 : 8;
	s32 data;
	int i;

	data = i2c_smbus_read_word_data(ap->client, cmd);
	if (data < 0)
		return;	/* ignore errors (due to ACPI??) */

	/* write back to clear latch */
	i2c_smbus_write_word_data(ap->client, cmd, 0);

	if (!data)
		return;

	dev_dbg(&idev->dev, APANEL ": data %#x\n", data);
	for (i = 0; i < idev->keycodemax; i++)
		if ((1u << i) & data)
			report_key(idev, ap->keymap[i]);
}

static int mail_led_set(struct led_classdev *led,
			 enum led_brightness value)
{
	struct apanel *ap = container_of(led, struct apanel, mail_led);
	u16 led_bits = value != LED_OFF ? 0x8000 : 0x0000;

	return i2c_smbus_write_word_data(ap->client, 0x10, led_bits);
}

static int apanel_probe(struct i2c_client *client)
{
	struct apanel *ap;
	struct input_dev *idev;
	u8 cmd = device_chip[APANEL_DEV_APPBTN] == CHIP_OZ992C ? 0 : 8;
	int i, err;

	ap = devm_kzalloc(&client->dev, sizeof(*ap), GFP_KERNEL);
	if (!ap)
		return -ENOMEM;

	idev = devm_input_allocate_device(&client->dev);
	if (!idev)
		return -ENOMEM;

	ap->idev = idev;
	ap->client = client;

	i2c_set_clientdata(client, ap);

	err = i2c_smbus_write_word_data(client, cmd, 0);
	if (err) {
		dev_warn(&client->dev, "smbus write error %d\n", err);
		return err;
	}

	input_set_drvdata(idev, ap);

	idev->name = APANEL_NAME " buttons";
	idev->phys = "apanel/input0";
	idev->id.bustype = BUS_HOST;

	memcpy(ap->keymap, apanel_keymap, sizeof(apanel_keymap));
	idev->keycode = ap->keymap;
	idev->keycodesize = sizeof(ap->keymap[0]);
	idev->keycodemax = (device_chip[APANEL_DEV_CDBTN] != CHIP_NONE) ? 12 : 4;

	set_bit(EV_KEY, idev->evbit);
	for (i = 0; i < idev->keycodemax; i++)
		if (ap->keymap[i])
			set_bit(ap->keymap[i], idev->keybit);

	err = input_setup_polling(idev, apanel_poll);
	if (err)
		return err;

	input_set_poll_interval(idev, POLL_INTERVAL_DEFAULT);

	err = input_register_device(idev);
	if (err)
		return err;

	if (device_chip[APANEL_DEV_LED] != CHIP_NONE) {
		ap->mail_led.name = "mail:blue";
		ap->mail_led.brightness_set_blocking = mail_led_set;
		err = devm_led_classdev_register(&client->dev, &ap->mail_led);
		if (err)
			return err;
	}

	return 0;
}

static void apanel_shutdown(struct i2c_client *client)
{
	struct apanel *ap = i2c_get_clientdata(client);

	if (device_chip[APANEL_DEV_LED] != CHIP_NONE)
		led_set_brightness(&ap->mail_led, LED_OFF);
}

static const struct i2c_device_id apanel_id[] = {
	{ "fujitsu_apanel" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, apanel_id);

static struct i2c_driver apanel_driver = {
	.driver = {
		.name = APANEL,
	},
	.probe		= apanel_probe,
	.shutdown	= apanel_shutdown,
	.id_table	= apanel_id,
};

/* Scan the system ROM for the signature "FJKEYINF" */
static __init const void __iomem *bios_signature(const void __iomem *bios)
{
	ssize_t offset;
	const unsigned char signature[] = "FJKEYINF";

	for (offset = 0; offset < 0x10000; offset += 0x10) {
		if (check_signature(bios + offset, signature,
				    sizeof(signature)-1))
			return bios + offset;
	}
	pr_notice(APANEL ": Fujitsu BIOS signature '%s' not found...\n",
		  signature);
	return NULL;
}

static int __init apanel_init(void)
{
	void __iomem *bios;
	const void __iomem *p;
	u8 devno;
	unsigned char i2c_addr;
	int found = 0;

	bios = ioremap(0xF0000, 0x10000); /* Can't fail */

	p = bios_signature(bios);
	if (!p) {
		iounmap(bios);
		return -ENODEV;
	}

	/* just use the first address */
	p += 8;
	i2c_addr = readb(p + 3) >> 1;

	for ( ; (devno = readb(p)) & 0x7f; p += 4) {
		unsigned char method, slave, chip;

		method = readb(p + 1);
		chip = readb(p + 2);
		slave = readb(p + 3) >> 1;

		if (slave != i2c_addr) {
			pr_notice(APANEL ": only one SMBus slave "
				  "address supported, skipping device...\n");
			continue;
		}

		/* translate alternative device numbers */
		switch (devno) {
		case 6:
			devno = APANEL_DEV_APPBTN;
			break;
		case 7:
			devno = APANEL_DEV_LED;
			break;
		}

		if (devno >= APANEL_DEV_MAX)
			pr_notice(APANEL ": unknown device %u found\n", devno);
		else if (device_chip[devno] != CHIP_NONE)
			pr_warn(APANEL ": duplicate entry for devno %u\n",
				devno);

		else if (method != 1 && method != 2 && method != 4) {
			pr_notice(APANEL ": unknown method %u for devno %u\n",
				  method, devno);
		} else {
			device_chip[devno] = (enum apanel_chip) chip;
			++found;
		}
	}
	iounmap(bios);

	if (found == 0) {
		pr_info(APANEL ": no input devices reported by BIOS\n");
		return -EIO;
	}

	return i2c_add_driver(&apanel_driver);
}
module_init(apanel_init);

static void __exit apanel_cleanup(void)
{
	i2c_del_driver(&apanel_driver);
}
module_exit(apanel_cleanup);

MODULE_AUTHOR("Stephen Hemminger <shemminger@linux-foundation.org>");
MODULE_DESCRIPTION(APANEL_NAME " driver");
MODULE_LICENSE("GPL");

MODULE_ALIAS("dmi:*:svnFUJITSU:pnLifeBook*:pvr*:rvnFUJITSU:*");
MODULE_ALIAS("dmi:*:svnFUJITSU:pnLifebook*:pvr*:rvnFUJITSU:*");
