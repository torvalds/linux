/*
 * Silead touchscreen driver DMI based configuration code
 *
 * Copyright (c) 2017 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Red Hat authors:
 * Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/notifier.h>
#include <linux/property.h>
#include <linux/string.h>

struct silead_ts_dmi_data {
	const char *acpi_name;
	const struct property_entry *properties;
};

static const struct property_entry cube_iwork8_air_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1660),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 900),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3670-cube-iwork8-air.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct silead_ts_dmi_data cube_iwork8_air_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= cube_iwork8_air_props,
};

static const struct property_entry jumper_ezpad_mini3_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1700),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1150),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3676-jumper-ezpad-mini3.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct silead_ts_dmi_data jumper_ezpad_mini3_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= jumper_ezpad_mini3_props,
};

static const struct property_entry dexp_ursus_7w_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 890),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 630),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1686-dexp-ursus-7w.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct silead_ts_dmi_data dexp_ursus_7w_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= dexp_ursus_7w_props,
};

static const struct property_entry surftab_wintron70_st70416_6_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 884),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 632),
	PROPERTY_ENTRY_STRING("firmware-name",
			      "gsl1686-surftab-wintron70-st70416-6.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct silead_ts_dmi_data surftab_wintron70_st70416_6_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= surftab_wintron70_st70416_6_props,
};

static const struct property_entry gp_electronic_t701_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 960),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 640),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-x"),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_STRING("firmware-name",
			      "gsl1680-gp-electronic-t701.fw"),
	{ }
};

static const struct silead_ts_dmi_data gp_electronic_t701_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= gp_electronic_t701_props,
};

static const struct property_entry pipo_w2s_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1660),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 880),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-x"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name",
			      "gsl1680-pipo-w2s.fw"),
	{ }
};

static const struct silead_ts_dmi_data pipo_w2s_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= pipo_w2s_props,
};

static const struct property_entry pov_mobii_wintab_p800w_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1800),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1150),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name",
			      "gsl3692-pov-mobii-wintab-p800w.fw"),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct silead_ts_dmi_data pov_mobii_wintab_p800w_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= pov_mobii_wintab_p800w_props,
};

static const struct property_entry itworks_tw891_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1600),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 890),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3670-itworks-tw891.fw"),
	{ }
};

static const struct silead_ts_dmi_data itworks_tw891_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= itworks_tw891_props,
};

static const struct property_entry chuwi_hi8_pro_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1728),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1148),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3680-chuwi-hi8-pro.fw"),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct silead_ts_dmi_data chuwi_hi8_pro_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= chuwi_hi8_pro_props,
};

static const struct property_entry digma_citi_e200_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1980),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1500),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_STRING("firmware-name",
			      "gsl1686-digma_citi_e200.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct silead_ts_dmi_data digma_citi_e200_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= digma_citi_e200_props,
};

static const struct dmi_system_id silead_ts_dmi_table[] = {
	{
		/* CUBE iwork8 Air */
		.driver_data = (void *)&cube_iwork8_air_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "cube"),
			DMI_MATCH(DMI_PRODUCT_NAME, "i1-TF"),
			DMI_MATCH(DMI_BOARD_NAME, "Cherry Trail CR"),
		},
	},
	{
		/* Jumper EZpad mini3 */
		.driver_data = (void *)&jumper_ezpad_mini3_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			/* jumperx.T87.KFBNEEA02 with the version-nr dropped */
			DMI_MATCH(DMI_BIOS_VERSION, "jumperx.T87.KFBNEEA"),
		},
	},
	{
		/* DEXP Ursus 7W */
		.driver_data = (void *)&dexp_ursus_7w_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "7W"),
		},
	},
	{
		/* Trekstor Surftab Wintron 7.0 ST70416-6 */
		.driver_data = (void *)&surftab_wintron70_st70416_6_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ST70416-6"),
			/* Exact match, different versions need different fw */
			DMI_MATCH(DMI_BIOS_VERSION, "TREK.G.WI71C.JGBMRBA04"),
		},
	},
	{
		/* Ployer Momo7w (same hardware as the Trekstor ST70416-6) */
		.driver_data = (void *)&surftab_wintron70_st70416_6_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Shenzhen PLOYER"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MOMO7W"),
			/* Exact match, different versions need different fw */
			DMI_MATCH(DMI_BIOS_VERSION, "MOMO.G.WI71C.MABMRBA02"),
		},
	},
	{
		/* GP-electronic T701 */
		.driver_data = (void *)&gp_electronic_t701_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "T701"),
			DMI_MATCH(DMI_BIOS_VERSION, "BYT70A.YNCHENG.WIN.007"),
		},
	},
	{
		/* Pipo W2S */
		.driver_data = (void *)&pipo_w2s_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "PIPO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "W2S"),
		},
	},
	{
		/* Point of View mobii wintab p800w */
		.driver_data = (void *)&pov_mobii_wintab_p800w_data,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "Aptio CRB"),
			DMI_MATCH(DMI_BIOS_VERSION, "3BAIR1013"),
			/* Above matches are too generic, add bios-date match */
			DMI_MATCH(DMI_BIOS_DATE, "08/22/2014"),
		},
	},
	{
		/* I.T.Works TW891 */
		.driver_data = (void *)&itworks_tw891_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "To be filled by O.E.M."),
			DMI_MATCH(DMI_PRODUCT_NAME, "TW891"),
		},
	},
	{
		/* Chuwi Hi8 Pro */
		.driver_data = (void *)&chuwi_hi8_pro_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hampoo"),
			DMI_MATCH(DMI_PRODUCT_NAME, "X1D3_C806N"),
		},
	},
	{
		/* Digma Citi E200 */
		.driver_data = (void *)&digma_citi_e200_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Digma"),
			DMI_MATCH(DMI_PRODUCT_NAME, "CITI E200"),
			DMI_MATCH(DMI_BOARD_NAME, "Cherry Trail CR"),
		},
	},
	{ },
};

static const struct silead_ts_dmi_data *silead_ts_data;

static void silead_ts_dmi_add_props(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	int error;

	if (has_acpi_companion(dev) &&
	    !strncmp(silead_ts_data->acpi_name, client->name, I2C_NAME_SIZE)) {
		error = device_add_properties(dev, silead_ts_data->properties);
		if (error)
			dev_err(dev, "failed to add properties: %d\n", error);
	}
}

static int silead_ts_dmi_notifier_call(struct notifier_block *nb,
				       unsigned long action, void *data)
{
	struct device *dev = data;
	struct i2c_client *client;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		client = i2c_verify_client(dev);
		if (client)
			silead_ts_dmi_add_props(client);
		break;

	default:
		break;
	}

	return 0;
}

static struct notifier_block silead_ts_dmi_notifier = {
	.notifier_call = silead_ts_dmi_notifier_call,
};

static int __init silead_ts_dmi_init(void)
{
	const struct dmi_system_id *dmi_id;
	int error;

	dmi_id = dmi_first_match(silead_ts_dmi_table);
	if (!dmi_id)
		return 0; /* Not an error */

	silead_ts_data = dmi_id->driver_data;

	error = bus_register_notifier(&i2c_bus_type, &silead_ts_dmi_notifier);
	if (error)
		pr_err("%s: failed to register i2c bus notifier: %d\n",
			__func__, error);

	return error;
}

/*
 * We are registering out notifier after i2c core is initialized and i2c bus
 * itself is ready (which happens at postcore initcall level), but before
 * ACPI starts enumerating devices (at subsys initcall level).
 */
arch_initcall(silead_ts_dmi_init);
