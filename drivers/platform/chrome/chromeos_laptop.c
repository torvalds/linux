// SPDX-License-Identifier: GPL-2.0+
// Driver to instantiate Chromebook i2c/smbus devices.
//
// Copyright (C) 2012 Google, Inc.
// Author: Benson Leung <bleung@chromium.org>

#define pr_fmt(fmt)		KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define ATMEL_TP_I2C_ADDR	0x4b
#define ATMEL_TP_I2C_BL_ADDR	0x25
#define ATMEL_TS_I2C_ADDR	0x4a
#define ATMEL_TS_I2C_BL_ADDR	0x26
#define CYAPA_TP_I2C_ADDR	0x67
#define ELAN_TP_I2C_ADDR	0x15
#define ISL_ALS_I2C_ADDR	0x44
#define TAOS_ALS_I2C_ADDR	0x29

static const char *i2c_adapter_names[] = {
	"SMBus I801 adapter",
	"i915 gmbus vga",
	"i915 gmbus panel",
	"Synopsys DesignWare I2C adapter",
};

/* Keep this enum consistent with i2c_adapter_names */
enum i2c_adapter_type {
	I2C_ADAPTER_SMBUS = 0,
	I2C_ADAPTER_VGADDC,
	I2C_ADAPTER_PANEL,
	I2C_ADAPTER_DESIGNWARE,
};

struct i2c_peripheral {
	struct i2c_board_info board_info;
	unsigned short alt_addr;

	const char *dmi_name;
	unsigned long irqflags;
	struct resource irq_resource;

	enum i2c_adapter_type type;
	u32 pci_devid;

	struct i2c_client *client;
};

struct acpi_peripheral {
	char hid[ACPI_ID_LEN];
	const struct property_entry *properties;
};

struct chromeos_laptop {
	/*
	 * Note that we can't mark this pointer as const because
	 * i2c_new_scanned_device() changes passed in I2C board info, so.
	 */
	struct i2c_peripheral *i2c_peripherals;
	unsigned int num_i2c_peripherals;

	const struct acpi_peripheral *acpi_peripherals;
	unsigned int num_acpi_peripherals;
};

static const struct chromeos_laptop *cros_laptop;

static struct i2c_client *
chromes_laptop_instantiate_i2c_device(struct i2c_adapter *adapter,
				      struct i2c_board_info *info,
				      unsigned short alt_addr)
{
	const unsigned short addr_list[] = { info->addr, I2C_CLIENT_END };
	struct i2c_client *client;

	/*
	 * Add the i2c device. If we can't detect it at the primary
	 * address we scan secondary addresses. In any case the client
	 * structure gets assigned primary address.
	 */
	client = i2c_new_scanned_device(adapter, info, addr_list, NULL);
	if (IS_ERR(client) && alt_addr) {
		struct i2c_board_info dummy_info = {
			I2C_BOARD_INFO("dummy", info->addr),
		};
		const unsigned short alt_addr_list[] = {
			alt_addr, I2C_CLIENT_END
		};
		struct i2c_client *dummy;

		dummy = i2c_new_scanned_device(adapter, &dummy_info,
					       alt_addr_list, NULL);
		if (!IS_ERR(dummy)) {
			pr_debug("%d-%02x is probed at %02x\n",
				 adapter->nr, info->addr, dummy->addr);
			i2c_unregister_device(dummy);
			client = i2c_new_client_device(adapter, info);
		}
	}

	if (IS_ERR(client)) {
		client = NULL;
		pr_debug("failed to register device %d-%02x\n",
			 adapter->nr, info->addr);
	} else {
		pr_debug("added i2c device %d-%02x\n",
			 adapter->nr, info->addr);
	}

	return client;
}

static bool chromeos_laptop_match_adapter_devid(struct device *dev, u32 devid)
{
	struct pci_dev *pdev;

	if (!dev_is_pci(dev))
		return false;

	pdev = to_pci_dev(dev);
	return devid == pci_dev_id(pdev);
}

static void chromeos_laptop_check_adapter(struct i2c_adapter *adapter)
{
	struct i2c_peripheral *i2c_dev;
	int i;

	for (i = 0; i < cros_laptop->num_i2c_peripherals; i++) {
		i2c_dev = &cros_laptop->i2c_peripherals[i];

		/* Skip devices already created */
		if (i2c_dev->client)
			continue;

		if (strncmp(adapter->name, i2c_adapter_names[i2c_dev->type],
			    strlen(i2c_adapter_names[i2c_dev->type])))
			continue;

		if (i2c_dev->pci_devid &&
		    !chromeos_laptop_match_adapter_devid(adapter->dev.parent,
							 i2c_dev->pci_devid)) {
			continue;
		}

		i2c_dev->client =
			chromes_laptop_instantiate_i2c_device(adapter,
							&i2c_dev->board_info,
							i2c_dev->alt_addr);
	}
}

static bool chromeos_laptop_adjust_client(struct i2c_client *client)
{
	const struct acpi_peripheral *acpi_dev;
	struct acpi_device_id acpi_ids[2] = { };
	int i;
	int error;

	if (!has_acpi_companion(&client->dev))
		return false;

	for (i = 0; i < cros_laptop->num_acpi_peripherals; i++) {
		acpi_dev = &cros_laptop->acpi_peripherals[i];

		memcpy(acpi_ids[0].id, acpi_dev->hid, ACPI_ID_LEN);

		if (acpi_match_device(acpi_ids, &client->dev)) {
			error = device_add_properties(&client->dev,
						      acpi_dev->properties);
			if (error) {
				dev_err(&client->dev,
					"failed to add properties: %d\n",
					error);
				break;
			}

			return true;
		}
	}

	return false;
}

static void chromeos_laptop_detach_i2c_client(struct i2c_client *client)
{
	struct i2c_peripheral *i2c_dev;
	int i;

	for (i = 0; i < cros_laptop->num_i2c_peripherals; i++) {
		i2c_dev = &cros_laptop->i2c_peripherals[i];

		if (i2c_dev->client == client)
			i2c_dev->client = NULL;
	}
}

static int chromeos_laptop_i2c_notifier_call(struct notifier_block *nb,
					     unsigned long action, void *data)
{
	struct device *dev = data;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		if (dev->type == &i2c_adapter_type)
			chromeos_laptop_check_adapter(to_i2c_adapter(dev));
		else if (dev->type == &i2c_client_type)
			chromeos_laptop_adjust_client(to_i2c_client(dev));
		break;

	case BUS_NOTIFY_REMOVED_DEVICE:
		if (dev->type == &i2c_client_type)
			chromeos_laptop_detach_i2c_client(to_i2c_client(dev));
		break;
	}

	return 0;
}

static struct notifier_block chromeos_laptop_i2c_notifier = {
	.notifier_call = chromeos_laptop_i2c_notifier_call,
};

#define DECLARE_CROS_LAPTOP(_name)					\
static const struct chromeos_laptop _name __initconst = {		\
	.i2c_peripherals	= _name##_peripherals,			\
	.num_i2c_peripherals	= ARRAY_SIZE(_name##_peripherals),	\
}

#define DECLARE_ACPI_CROS_LAPTOP(_name)					\
static const struct chromeos_laptop _name __initconst = {		\
	.acpi_peripherals	= _name##_peripherals,			\
	.num_acpi_peripherals	= ARRAY_SIZE(_name##_peripherals),	\
}

static struct i2c_peripheral samsung_series_5_550_peripherals[] __initdata = {
	/* Touchpad. */
	{
		.board_info	= {
			I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
			.flags		= I2C_CLIENT_WAKE,
		},
		.dmi_name	= "trackpad",
		.type		= I2C_ADAPTER_SMBUS,
	},
	/* Light Sensor. */
	{
		.board_info	= {
			I2C_BOARD_INFO("isl29018", ISL_ALS_I2C_ADDR),
		},
		.dmi_name	= "lightsensor",
		.type		= I2C_ADAPTER_SMBUS,
	},
};
DECLARE_CROS_LAPTOP(samsung_series_5_550);

static struct i2c_peripheral samsung_series_5_peripherals[] __initdata = {
	/* Light Sensor. */
	{
		.board_info	= {
			I2C_BOARD_INFO("tsl2583", TAOS_ALS_I2C_ADDR),
		},
		.type		= I2C_ADAPTER_SMBUS,
	},
};
DECLARE_CROS_LAPTOP(samsung_series_5);

static const int chromebook_pixel_tp_keys[] __initconst = {
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	BTN_LEFT
};

static const struct property_entry
chromebook_pixel_trackpad_props[] __initconst = {
	PROPERTY_ENTRY_STRING("compatible", "atmel,maxtouch"),
	PROPERTY_ENTRY_U32_ARRAY("linux,gpio-keymap", chromebook_pixel_tp_keys),
	{ }
};

static const struct property_entry
chromebook_atmel_touchscreen_props[] __initconst = {
	PROPERTY_ENTRY_STRING("compatible", "atmel,maxtouch"),
	{ }
};

static struct i2c_peripheral chromebook_pixel_peripherals[] __initdata = {
	/* Touch Screen. */
	{
		.board_info	= {
			I2C_BOARD_INFO("atmel_mxt_ts",
					ATMEL_TS_I2C_ADDR),
			.properties	=
				chromebook_atmel_touchscreen_props,
			.flags		= I2C_CLIENT_WAKE,
		},
		.dmi_name	= "touchscreen",
		.irqflags	= IRQF_TRIGGER_FALLING,
		.type		= I2C_ADAPTER_PANEL,
		.alt_addr	= ATMEL_TS_I2C_BL_ADDR,
	},
	/* Touchpad. */
	{
		.board_info	= {
			I2C_BOARD_INFO("atmel_mxt_tp",
					ATMEL_TP_I2C_ADDR),
			.properties	=
				chromebook_pixel_trackpad_props,
			.flags		= I2C_CLIENT_WAKE,
		},
		.dmi_name	= "trackpad",
		.irqflags	= IRQF_TRIGGER_FALLING,
		.type		= I2C_ADAPTER_VGADDC,
		.alt_addr	= ATMEL_TP_I2C_BL_ADDR,
	},
	/* Light Sensor. */
	{
		.board_info	= {
			I2C_BOARD_INFO("isl29018", ISL_ALS_I2C_ADDR),
		},
		.dmi_name	= "lightsensor",
		.type		= I2C_ADAPTER_PANEL,
	},
};
DECLARE_CROS_LAPTOP(chromebook_pixel);

static struct i2c_peripheral hp_chromebook_14_peripherals[] __initdata = {
	/* Touchpad. */
	{
		.board_info	= {
			I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
			.flags		= I2C_CLIENT_WAKE,
		},
		.dmi_name	= "trackpad",
		.type		= I2C_ADAPTER_DESIGNWARE,
	},
};
DECLARE_CROS_LAPTOP(hp_chromebook_14);

static struct i2c_peripheral dell_chromebook_11_peripherals[] __initdata = {
	/* Touchpad. */
	{
		.board_info	= {
			I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
			.flags		= I2C_CLIENT_WAKE,
		},
		.dmi_name	= "trackpad",
		.type		= I2C_ADAPTER_DESIGNWARE,
	},
	/* Elan Touchpad option. */
	{
		.board_info	= {
			I2C_BOARD_INFO("elan_i2c", ELAN_TP_I2C_ADDR),
			.flags		= I2C_CLIENT_WAKE,
		},
		.dmi_name	= "trackpad",
		.type		= I2C_ADAPTER_DESIGNWARE,
	},
};
DECLARE_CROS_LAPTOP(dell_chromebook_11);

static struct i2c_peripheral toshiba_cb35_peripherals[] __initdata = {
	/* Touchpad. */
	{
		.board_info	= {
			I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
			.flags		= I2C_CLIENT_WAKE,
		},
		.dmi_name	= "trackpad",
		.type		= I2C_ADAPTER_DESIGNWARE,
	},
};
DECLARE_CROS_LAPTOP(toshiba_cb35);

static struct i2c_peripheral acer_c7_chromebook_peripherals[] __initdata = {
	/* Touchpad. */
	{
		.board_info	= {
			I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
			.flags		= I2C_CLIENT_WAKE,
		},
		.dmi_name	= "trackpad",
		.type		= I2C_ADAPTER_SMBUS,
	},
};
DECLARE_CROS_LAPTOP(acer_c7_chromebook);

static struct i2c_peripheral acer_ac700_peripherals[] __initdata = {
	/* Light Sensor. */
	{
		.board_info	= {
			I2C_BOARD_INFO("tsl2583", TAOS_ALS_I2C_ADDR),
		},
		.type		= I2C_ADAPTER_SMBUS,
	},
};
DECLARE_CROS_LAPTOP(acer_ac700);

static struct i2c_peripheral acer_c720_peripherals[] __initdata = {
	/* Touchscreen. */
	{
		.board_info	= {
			I2C_BOARD_INFO("atmel_mxt_ts",
					ATMEL_TS_I2C_ADDR),
			.properties	=
				chromebook_atmel_touchscreen_props,
			.flags		= I2C_CLIENT_WAKE,
		},
		.dmi_name	= "touchscreen",
		.irqflags	= IRQF_TRIGGER_FALLING,
		.type		= I2C_ADAPTER_DESIGNWARE,
		.pci_devid	= PCI_DEVID(0, PCI_DEVFN(0x15, 0x2)),
		.alt_addr	= ATMEL_TS_I2C_BL_ADDR,
	},
	/* Touchpad. */
	{
		.board_info	= {
			I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
			.flags		= I2C_CLIENT_WAKE,
		},
		.dmi_name	= "trackpad",
		.type		= I2C_ADAPTER_DESIGNWARE,
		.pci_devid	= PCI_DEVID(0, PCI_DEVFN(0x15, 0x1)),
	},
	/* Elan Touchpad option. */
	{
		.board_info	= {
			I2C_BOARD_INFO("elan_i2c", ELAN_TP_I2C_ADDR),
			.flags		= I2C_CLIENT_WAKE,
		},
		.dmi_name	= "trackpad",
		.type		= I2C_ADAPTER_DESIGNWARE,
		.pci_devid	= PCI_DEVID(0, PCI_DEVFN(0x15, 0x1)),
	},
	/* Light Sensor. */
	{
		.board_info	= {
			I2C_BOARD_INFO("isl29018", ISL_ALS_I2C_ADDR),
		},
		.dmi_name	= "lightsensor",
		.type		= I2C_ADAPTER_DESIGNWARE,
		.pci_devid	= PCI_DEVID(0, PCI_DEVFN(0x15, 0x2)),
	},
};
DECLARE_CROS_LAPTOP(acer_c720);

static struct i2c_peripheral
hp_pavilion_14_chromebook_peripherals[] __initdata = {
	/* Touchpad. */
	{
		.board_info	= {
			I2C_BOARD_INFO("cyapa", CYAPA_TP_I2C_ADDR),
			.flags		= I2C_CLIENT_WAKE,
		},
		.dmi_name	= "trackpad",
		.type		= I2C_ADAPTER_SMBUS,
	},
};
DECLARE_CROS_LAPTOP(hp_pavilion_14_chromebook);

static struct i2c_peripheral cr48_peripherals[] __initdata = {
	/* Light Sensor. */
	{
		.board_info	= {
			I2C_BOARD_INFO("tsl2563", TAOS_ALS_I2C_ADDR),
		},
		.type		= I2C_ADAPTER_SMBUS,
	},
};
DECLARE_CROS_LAPTOP(cr48);

static const u32 samus_touchpad_buttons[] __initconst = {
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	BTN_LEFT
};

static const struct property_entry samus_trackpad_props[] __initconst = {
	PROPERTY_ENTRY_STRING("compatible", "atmel,maxtouch"),
	PROPERTY_ENTRY_U32_ARRAY("linux,gpio-keymap", samus_touchpad_buttons),
	{ }
};

static struct acpi_peripheral samus_peripherals[] __initdata = {
	/* Touchpad */
	{
		.hid		= "ATML0000",
		.properties	= samus_trackpad_props,
	},
	/* Touchsceen */
	{
		.hid		= "ATML0001",
		.properties	= chromebook_atmel_touchscreen_props,
	},
};
DECLARE_ACPI_CROS_LAPTOP(samus);

static struct acpi_peripheral generic_atmel_peripherals[] __initdata = {
	/* Touchpad */
	{
		.hid		= "ATML0000",
		.properties	= chromebook_pixel_trackpad_props,
	},
	/* Touchsceen */
	{
		.hid		= "ATML0001",
		.properties	= chromebook_atmel_touchscreen_props,
	},
};
DECLARE_ACPI_CROS_LAPTOP(generic_atmel);

static const struct dmi_system_id chromeos_laptop_dmi_table[] __initconst = {
	{
		.ident = "Samsung Series 5 550",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Lumpy"),
		},
		.driver_data = (void *)&samsung_series_5_550,
	},
	{
		.ident = "Samsung Series 5",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Alex"),
		},
		.driver_data = (void *)&samsung_series_5,
	},
	{
		.ident = "Chromebook Pixel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Link"),
		},
		.driver_data = (void *)&chromebook_pixel,
	},
	{
		.ident = "Wolf",
		.matches = {
			DMI_MATCH(DMI_BIOS_VENDOR, "coreboot"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Wolf"),
		},
		.driver_data = (void *)&dell_chromebook_11,
	},
	{
		.ident = "HP Chromebook 14",
		.matches = {
			DMI_MATCH(DMI_BIOS_VENDOR, "coreboot"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Falco"),
		},
		.driver_data = (void *)&hp_chromebook_14,
	},
	{
		.ident = "Toshiba CB35",
		.matches = {
			DMI_MATCH(DMI_BIOS_VENDOR, "coreboot"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Leon"),
		},
		.driver_data = (void *)&toshiba_cb35,
	},
	{
		.ident = "Acer C7 Chromebook",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Parrot"),
		},
		.driver_data = (void *)&acer_c7_chromebook,
	},
	{
		.ident = "Acer AC700",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "ZGB"),
		},
		.driver_data = (void *)&acer_ac700,
	},
	{
		.ident = "Acer C720",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Peppy"),
		},
		.driver_data = (void *)&acer_c720,
	},
	{
		.ident = "HP Pavilion 14 Chromebook",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Butterfly"),
		},
		.driver_data = (void *)&hp_pavilion_14_chromebook,
	},
	{
		.ident = "Cr-48",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Mario"),
		},
		.driver_data = (void *)&cr48,
	},
	/* Devices with peripherals incompletely described in ACPI */
	{
		.ident = "Chromebook Pro",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Caroline"),
		},
		.driver_data = (void *)&samus,
	},
	{
		.ident = "Google Pixel 2 (2015)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Samus"),
		},
		.driver_data = (void *)&samus,
	},
	{
		.ident = "Samsung Chromebook 3",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Celes"),
		},
		.driver_data = (void *)&samus,
	},
	{
		/*
		 * Other Chromebooks with Atmel touch controllers:
		 * - Winky (touchpad)
		 * - Clapper, Expresso, Rambi, Glimmer (touchscreen)
		 */
		.ident = "Other Chromebook",
		.matches = {
			/*
			 * This will match all Google devices, not only devices
			 * with Atmel, but we will validate that the device
			 * actually has matching peripherals.
			 */
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
		},
		.driver_data = (void *)&generic_atmel,
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, chromeos_laptop_dmi_table);

static int __init chromeos_laptop_scan_peripherals(struct device *dev, void *data)
{
	int error;

	if (dev->type == &i2c_adapter_type) {
		chromeos_laptop_check_adapter(to_i2c_adapter(dev));
	} else if (dev->type == &i2c_client_type) {
		if (chromeos_laptop_adjust_client(to_i2c_client(dev))) {
			/*
			 * Now that we have needed properties re-trigger
			 * driver probe in case driver was initialized
			 * earlier and probe failed.
			 */
			error = device_attach(dev);
			if (error < 0)
				dev_warn(dev,
					 "%s: device_attach() failed: %d\n",
					 __func__, error);
		}
	}

	return 0;
}

static int __init chromeos_laptop_get_irq_from_dmi(const char *dmi_name)
{
	const struct dmi_device *dmi_dev;
	const struct dmi_dev_onboard *dev_data;

	dmi_dev = dmi_find_device(DMI_DEV_TYPE_DEV_ONBOARD, dmi_name, NULL);
	if (!dmi_dev) {
		pr_err("failed to find DMI device '%s'\n", dmi_name);
		return -ENOENT;
	}

	dev_data = dmi_dev->device_data;
	if (!dev_data) {
		pr_err("failed to get data from DMI for '%s'\n", dmi_name);
		return -EINVAL;
	}

	return dev_data->instance;
}

static int __init chromeos_laptop_setup_irq(struct i2c_peripheral *i2c_dev)
{
	int irq;

	if (i2c_dev->dmi_name) {
		irq = chromeos_laptop_get_irq_from_dmi(i2c_dev->dmi_name);
		if (irq < 0)
			return irq;

		i2c_dev->irq_resource  = (struct resource)
			DEFINE_RES_NAMED(irq, 1, NULL,
					 IORESOURCE_IRQ | i2c_dev->irqflags);
		i2c_dev->board_info.resources = &i2c_dev->irq_resource;
		i2c_dev->board_info.num_resources = 1;
	}

	return 0;
}

static int __init
chromeos_laptop_prepare_i2c_peripherals(struct chromeos_laptop *cros_laptop,
					const struct chromeos_laptop *src)
{
	struct i2c_peripheral *i2c_peripherals;
	struct i2c_peripheral *i2c_dev;
	struct i2c_board_info *info;
	int i;
	int error;

	if (!src->num_i2c_peripherals)
		return 0;

	i2c_peripherals = kmemdup(src->i2c_peripherals,
					      src->num_i2c_peripherals *
					  sizeof(*src->i2c_peripherals),
					  GFP_KERNEL);
	if (!i2c_peripherals)
		return -ENOMEM;

	for (i = 0; i < src->num_i2c_peripherals; i++) {
		i2c_dev = &i2c_peripherals[i];
		info = &i2c_dev->board_info;

		error = chromeos_laptop_setup_irq(i2c_dev);
		if (error)
			goto err_out;

		/* We need to deep-copy properties */
		if (info->properties) {
			info->properties =
				property_entries_dup(info->properties);
			if (IS_ERR(info->properties)) {
				error = PTR_ERR(info->properties);
				goto err_out;
			}
		}
	}

	cros_laptop->i2c_peripherals = i2c_peripherals;
	cros_laptop->num_i2c_peripherals = src->num_i2c_peripherals;

	return 0;

err_out:
	while (--i >= 0) {
		i2c_dev = &i2c_peripherals[i];
		info = &i2c_dev->board_info;
		if (info->properties)
			property_entries_free(info->properties);
	}
	kfree(i2c_peripherals);
	return error;
}

static int __init
chromeos_laptop_prepare_acpi_peripherals(struct chromeos_laptop *cros_laptop,
					const struct chromeos_laptop *src)
{
	struct acpi_peripheral *acpi_peripherals;
	struct acpi_peripheral *acpi_dev;
	const struct acpi_peripheral *src_dev;
	int n_peripherals = 0;
	int i;
	int error;

	for (i = 0; i < src->num_acpi_peripherals; i++) {
		if (acpi_dev_present(src->acpi_peripherals[i].hid, NULL, -1))
			n_peripherals++;
	}

	if (!n_peripherals)
		return 0;

	acpi_peripherals = kcalloc(n_peripherals,
				   sizeof(*src->acpi_peripherals),
				   GFP_KERNEL);
	if (!acpi_peripherals)
		return -ENOMEM;

	acpi_dev = acpi_peripherals;
	for (i = 0; i < src->num_acpi_peripherals; i++) {
		src_dev = &src->acpi_peripherals[i];
		if (!acpi_dev_present(src_dev->hid, NULL, -1))
			continue;

		*acpi_dev = *src_dev;

		/* We need to deep-copy properties */
		if (src_dev->properties) {
			acpi_dev->properties =
				property_entries_dup(src_dev->properties);
			if (IS_ERR(acpi_dev->properties)) {
				error = PTR_ERR(acpi_dev->properties);
				goto err_out;
			}
		}

		acpi_dev++;
	}

	cros_laptop->acpi_peripherals = acpi_peripherals;
	cros_laptop->num_acpi_peripherals = n_peripherals;

	return 0;

err_out:
	while (--i >= 0) {
		acpi_dev = &acpi_peripherals[i];
		if (acpi_dev->properties)
			property_entries_free(acpi_dev->properties);
	}

	kfree(acpi_peripherals);
	return error;
}

static void chromeos_laptop_destroy(const struct chromeos_laptop *cros_laptop)
{
	const struct acpi_peripheral *acpi_dev;
	struct i2c_peripheral *i2c_dev;
	struct i2c_board_info *info;
	int i;

	for (i = 0; i < cros_laptop->num_i2c_peripherals; i++) {
		i2c_dev = &cros_laptop->i2c_peripherals[i];
		info = &i2c_dev->board_info;

		i2c_unregister_device(i2c_dev->client);
		property_entries_free(info->properties);
	}

	for (i = 0; i < cros_laptop->num_acpi_peripherals; i++) {
		acpi_dev = &cros_laptop->acpi_peripherals[i];

		property_entries_free(acpi_dev->properties);
	}

	kfree(cros_laptop->i2c_peripherals);
	kfree(cros_laptop->acpi_peripherals);
	kfree(cros_laptop);
}

static struct chromeos_laptop * __init
chromeos_laptop_prepare(const struct chromeos_laptop *src)
{
	struct chromeos_laptop *cros_laptop;
	int error;

	cros_laptop = kzalloc(sizeof(*cros_laptop), GFP_KERNEL);
	if (!cros_laptop)
		return ERR_PTR(-ENOMEM);

	error = chromeos_laptop_prepare_i2c_peripherals(cros_laptop, src);
	if (!error)
		error = chromeos_laptop_prepare_acpi_peripherals(cros_laptop,
								 src);

	if (error) {
		chromeos_laptop_destroy(cros_laptop);
		return ERR_PTR(error);
	}

	return cros_laptop;
}

static int __init chromeos_laptop_init(void)
{
	const struct dmi_system_id *dmi_id;
	int error;

	dmi_id = dmi_first_match(chromeos_laptop_dmi_table);
	if (!dmi_id) {
		pr_debug("unsupported system\n");
		return -ENODEV;
	}

	pr_debug("DMI Matched %s\n", dmi_id->ident);

	cros_laptop = chromeos_laptop_prepare((void *)dmi_id->driver_data);
	if (IS_ERR(cros_laptop))
		return PTR_ERR(cros_laptop);

	if (!cros_laptop->num_i2c_peripherals &&
	    !cros_laptop->num_acpi_peripherals) {
		pr_debug("no relevant devices detected\n");
		error = -ENODEV;
		goto err_destroy_cros_laptop;
	}

	error = bus_register_notifier(&i2c_bus_type,
				      &chromeos_laptop_i2c_notifier);
	if (error) {
		pr_err("failed to register i2c bus notifier: %d\n",
		       error);
		goto err_destroy_cros_laptop;
	}

	/*
	 * Scan adapters that have been registered and clients that have
	 * been created before we installed the notifier to make sure
	 * we do not miss any devices.
	 */
	i2c_for_each_dev(NULL, chromeos_laptop_scan_peripherals);

	return 0;

err_destroy_cros_laptop:
	chromeos_laptop_destroy(cros_laptop);
	return error;
}

static void __exit chromeos_laptop_exit(void)
{
	bus_unregister_notifier(&i2c_bus_type, &chromeos_laptop_i2c_notifier);
	chromeos_laptop_destroy(cros_laptop);
}

module_init(chromeos_laptop_init);
module_exit(chromeos_laptop_exit);

MODULE_DESCRIPTION("Chrome OS Laptop driver");
MODULE_AUTHOR("Benson Leung <bleung@chromium.org>");
MODULE_LICENSE("GPL");
