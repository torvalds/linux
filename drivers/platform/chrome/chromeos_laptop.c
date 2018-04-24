// SPDX-License-Identifier: GPL-2.0+
// Driver to instantiate Chromebook i2c/smbus devices.
//
// Copyright (C) 2012 Google, Inc.
// Author: Benson Leung <bleung@chromium.org>

#define pr_fmt(fmt)		KBUILD_MODNAME ": " fmt

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

struct chromeos_laptop {
	/*
	 * Note that we can't mark this pointer as const because
	 * i2c_new_probed_device() changes passed in I2C board info, so.
	 */
	struct i2c_peripheral *i2c_peripherals;
	unsigned int num_i2c_peripherals;
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
	client = i2c_new_probed_device(adapter, info, addr_list, NULL);
	if (!client && alt_addr) {
		struct i2c_board_info dummy_info = {
			I2C_BOARD_INFO("dummy", info->addr),
		};
		const unsigned short alt_addr_list[] = {
			alt_addr, I2C_CLIENT_END
		};
		struct i2c_client *dummy;

		dummy = i2c_new_probed_device(adapter, &dummy_info,
					      alt_addr_list, NULL);
		if (dummy) {
			pr_debug("%d-%02x is probed at %02x\n",
				 adapter->nr, info->addr, dummy->addr);
			i2c_unregister_device(dummy);
			client = i2c_new_device(adapter, info);
		}
	}

	if (!client)
		pr_debug("failed to register device %d-%02x\n",
			 adapter->nr, info->addr);
	else
		pr_debug("added i2c device %d-%02x\n",
			 adapter->nr, info->addr);

	return client;
}

static bool chromeos_laptop_match_adapter_devid(struct device *dev, u32 devid)
{
	struct pci_dev *pdev;

	if (!dev_is_pci(dev))
		return false;

	pdev = to_pci_dev(dev);
	return devid == PCI_DEVID(pdev->bus->number, pdev->devfn);
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
	PROPERTY_ENTRY_U32_ARRAY("linux,gpio-keymap", chromebook_pixel_tp_keys),
	{ }
};

static struct i2c_peripheral chromebook_pixel_peripherals[] __initdata = {
	/* Touch Screen. */
	{
		.board_info	= {
			I2C_BOARD_INFO("atmel_mxt_ts",
					ATMEL_TS_I2C_ADDR),
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
	{ }
};
MODULE_DEVICE_TABLE(dmi, chromeos_laptop_dmi_table);

static int __init chromeos_laptop_scan_adapter(struct device *dev, void *data)
{
	struct i2c_adapter *adapter;

	adapter = i2c_verify_adapter(dev);
	if (adapter)
		chromeos_laptop_check_adapter(adapter);

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

static struct chromeos_laptop * __init
chromeos_laptop_prepare(const struct chromeos_laptop *src)
{
	struct chromeos_laptop *cros_laptop;
	struct i2c_peripheral *i2c_dev;
	struct i2c_board_info *info;
	int error;
	int i;

	cros_laptop = kzalloc(sizeof(*cros_laptop), GFP_KERNEL);
	if (!cros_laptop)
		return ERR_PTR(-ENOMEM);

	cros_laptop->i2c_peripherals = kmemdup(src->i2c_peripherals,
					       src->num_i2c_peripherals *
						sizeof(*src->i2c_peripherals),
					       GFP_KERNEL);
	if (!cros_laptop->i2c_peripherals) {
		error = -ENOMEM;
		goto err_free_cros_laptop;
	}

	cros_laptop->num_i2c_peripherals = src->num_i2c_peripherals;

	for (i = 0; i < cros_laptop->num_i2c_peripherals; i++) {
		i2c_dev = &cros_laptop->i2c_peripherals[i];
		info = &i2c_dev->board_info;

		error = chromeos_laptop_setup_irq(i2c_dev);
		if (error)
			goto err_destroy_cros_peripherals;

		/* We need to deep-copy properties */
		if (info->properties) {
			info->properties =
				property_entries_dup(info->properties);
			if (IS_ERR(info->properties)) {
				error = PTR_ERR(info->properties);
				goto err_destroy_cros_peripherals;
			}
		}
	}

	return cros_laptop;

err_destroy_cros_peripherals:
	while (--i >= 0) {
		i2c_dev = &cros_laptop->i2c_peripherals[i];
		info = &i2c_dev->board_info;
		if (info->properties)
			property_entries_free(info->properties);
	}
	kfree(cros_laptop->i2c_peripherals);
err_free_cros_laptop:
	kfree(cros_laptop);
	return ERR_PTR(error);
}

static void chromeos_laptop_destroy(const struct chromeos_laptop *cros_laptop)
{
	struct i2c_peripheral *i2c_dev;
	struct i2c_board_info *info;
	int i;

	for (i = 0; i < cros_laptop->num_i2c_peripherals; i++) {
		i2c_dev = &cros_laptop->i2c_peripherals[i];
		info = &i2c_dev->board_info;

		if (i2c_dev->client)
			i2c_unregister_device(i2c_dev->client);

		if (info->properties)
			property_entries_free(info->properties);
	}

	kfree(cros_laptop->i2c_peripherals);
	kfree(cros_laptop);
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

	error = bus_register_notifier(&i2c_bus_type,
				      &chromeos_laptop_i2c_notifier);
	if (error) {
		pr_err("failed to register i2c bus notifier: %d\n", error);
		chromeos_laptop_destroy(cros_laptop);
		return error;
	}

	/*
	 * Scan adapters that have been registered before we installed
	 * the notifier to make sure we do not miss any devices.
	 */
	i2c_for_each_dev(NULL, chromeos_laptop_scan_adapter);

	return 0;
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
