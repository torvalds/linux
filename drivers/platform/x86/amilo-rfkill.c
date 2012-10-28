/*
 * Support for rfkill on some Fujitsu-Siemens Amilo laptops.
 * Copyright 2011 Ben Hutchings.
 *
 * Based in part on the fsam7440 driver, which is:
 * Copyright 2005 Alejandro Vidal Mata & Javier Vidal Mata.
 * and on the fsaa1655g driver, which is:
 * Copyright 2006 Martin Večeřa.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/i8042.h>
#include <linux/io.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>

/*
 * These values were obtained from disassembling and debugging the
 * PM.exe program installed in the Fujitsu-Siemens AMILO A1655G
 */
#define A1655_WIFI_COMMAND	0x10C5
#define A1655_WIFI_ON		0x25
#define A1655_WIFI_OFF		0x45

static int amilo_a1655_rfkill_set_block(void *data, bool blocked)
{
	u8 param = blocked ? A1655_WIFI_OFF : A1655_WIFI_ON;
	int rc;

	i8042_lock_chip();
	rc = i8042_command(&param, A1655_WIFI_COMMAND);
	i8042_unlock_chip();
	return rc;
}

static const struct rfkill_ops amilo_a1655_rfkill_ops = {
	.set_block = amilo_a1655_rfkill_set_block
};

/*
 * These values were obtained from disassembling the PM.exe program
 * installed in the Fujitsu-Siemens AMILO M 7440
 */
#define M7440_PORT1		0x118f
#define M7440_PORT2		0x118e
#define M7440_RADIO_ON1		0x12
#define M7440_RADIO_ON2		0x80
#define M7440_RADIO_OFF1	0x10
#define M7440_RADIO_OFF2	0x00

static int amilo_m7440_rfkill_set_block(void *data, bool blocked)
{
	u8 val1 = blocked ? M7440_RADIO_OFF1 : M7440_RADIO_ON1;
	u8 val2 = blocked ? M7440_RADIO_OFF2 : M7440_RADIO_ON2;

	outb(val1, M7440_PORT1);
	outb(val2, M7440_PORT2);

	/* Check whether the state has changed correctly */
	if (inb(M7440_PORT1) != val1 || inb(M7440_PORT2) != val2)
		return -EIO;

	return 0;
}

static const struct rfkill_ops amilo_m7440_rfkill_ops = {
	.set_block = amilo_m7440_rfkill_set_block
};

static const struct dmi_system_id __devinitconst amilo_rfkill_id_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_BOARD_NAME, "AMILO A1655"),
		},
		.driver_data = (void *)&amilo_a1655_rfkill_ops
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_BOARD_NAME, "AMILO M7440"),
		},
		.driver_data = (void *)&amilo_m7440_rfkill_ops
	},
	{}
};

static struct platform_device *amilo_rfkill_pdev;
static struct rfkill *amilo_rfkill_dev;

static int __devinit amilo_rfkill_probe(struct platform_device *device)
{
	int rc;
	const struct dmi_system_id *system_id =
		dmi_first_match(amilo_rfkill_id_table);

	if (!system_id)
		return -ENXIO;

	amilo_rfkill_dev = rfkill_alloc(KBUILD_MODNAME, &device->dev,
					RFKILL_TYPE_WLAN,
					system_id->driver_data, NULL);
	if (!amilo_rfkill_dev)
		return -ENOMEM;

	rc = rfkill_register(amilo_rfkill_dev);
	if (rc)
		goto fail;

	return 0;

fail:
	rfkill_destroy(amilo_rfkill_dev);
	return rc;
}

static int amilo_rfkill_remove(struct platform_device *device)
{
	rfkill_unregister(amilo_rfkill_dev);
	rfkill_destroy(amilo_rfkill_dev);
	return 0;
}

static struct platform_driver amilo_rfkill_driver = {
	.driver = {
		.name	= KBUILD_MODNAME,
		.owner	= THIS_MODULE,
	},
	.probe	= amilo_rfkill_probe,
	.remove	= amilo_rfkill_remove,
};

static int __init amilo_rfkill_init(void)
{
	int rc;

	if (dmi_first_match(amilo_rfkill_id_table) == NULL)
		return -ENODEV;

	rc = platform_driver_register(&amilo_rfkill_driver);
	if (rc)
		return rc;

	amilo_rfkill_pdev = platform_device_register_simple(KBUILD_MODNAME, -1,
							    NULL, 0);
	if (IS_ERR(amilo_rfkill_pdev)) {
		rc = PTR_ERR(amilo_rfkill_pdev);
		goto fail;
	}

	return 0;

fail:
	platform_driver_unregister(&amilo_rfkill_driver);
	return rc;
}

static void __exit amilo_rfkill_exit(void)
{
	platform_device_unregister(amilo_rfkill_pdev);
	platform_driver_unregister(&amilo_rfkill_driver);
}

MODULE_AUTHOR("Ben Hutchings <ben@decadent.org.uk>");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(dmi, amilo_rfkill_id_table);

module_init(amilo_rfkill_init);
module_exit(amilo_rfkill_exit);
