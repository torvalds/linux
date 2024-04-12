// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Touchscreen driver DMI based configuration code
 *
 * Copyright (c) 2017 Red Hat Inc.
 *
 * Red Hat authors:
 * Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/efi_embedded_fw.h>
#include <linux/i2c.h>
#include <linux/notifier.h>
#include <linux/property.h>
#include <linux/string.h>

struct ts_dmi_data {
	/* The EFI embedded-fw code expects this to be the first member! */
	struct efi_embedded_fw_desc embedded_fw;
	const char *acpi_name;
	const struct property_entry *properties;
};

/* NOTE: Please keep all entries sorted alphabetically */

static const struct property_entry archos_101_cesium_educ_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1850),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1280),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-archos-101-cesium-educ.fw"),
	{ }
};

static const struct ts_dmi_data archos_101_cesium_educ_data = {
	.acpi_name      = "MSSL1680:00",
	.properties     = archos_101_cesium_educ_props,
};

static const struct property_entry bush_bush_windows_tablet_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1850),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1280),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-bush-bush-windows-tablet.fw"),
	{ }
};

static const struct ts_dmi_data bush_bush_windows_tablet_data = {
	.acpi_name      = "MSSL1680:00",
	.properties     = bush_bush_windows_tablet_props,
};

static const struct property_entry chuwi_hi8_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1665),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1140),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-chuwi-hi8.fw"),
	{ }
};

static const struct ts_dmi_data chuwi_hi8_data = {
	.acpi_name      = "MSSL0001:00",
	.properties     = chuwi_hi8_props,
};

static const struct property_entry chuwi_hi8_air_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1728),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1148),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3676-chuwi-hi8-air.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct ts_dmi_data chuwi_hi8_air_data = {
	.acpi_name	= "MSSL1680",
	.properties	= chuwi_hi8_air_props,
};

static const struct property_entry chuwi_hi8_pro_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 6),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 3),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1728),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1148),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3680-chuwi-hi8-pro.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data chuwi_hi8_pro_data = {
	.embedded_fw = {
		.name	= "silead/gsl3680-chuwi-hi8-pro.fw",
		.prefix = { 0xf0, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
		.length	= 39864,
		.sha256	= { 0xc0, 0x88, 0xc5, 0xef, 0xd1, 0x70, 0x77, 0x59,
			    0x4e, 0xe9, 0xc4, 0xd8, 0x2e, 0xcd, 0xbf, 0x95,
			    0x32, 0xd9, 0x03, 0x28, 0x0d, 0x48, 0x9f, 0x92,
			    0x35, 0x37, 0xf6, 0x8b, 0x2a, 0xe4, 0x73, 0xff },
	},
	.acpi_name	= "MSSL1680:00",
	.properties	= chuwi_hi8_pro_props,
};

static const struct property_entry chuwi_hi10_air_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1981),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1271),
	PROPERTY_ENTRY_U32("touchscreen-min-x", 99),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 9),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_U32("touchscreen-fuzz-x", 5),
	PROPERTY_ENTRY_U32("touchscreen-fuzz-y", 4),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-chuwi-hi10-air.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data chuwi_hi10_air_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= chuwi_hi10_air_props,
};

static const struct property_entry chuwi_hi10_plus_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 12),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 10),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1908),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1270),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-chuwi-hi10plus.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	PROPERTY_ENTRY_BOOL("silead,pen-supported"),
	PROPERTY_ENTRY_U32("silead,pen-resolution-x", 8),
	PROPERTY_ENTRY_U32("silead,pen-resolution-y", 8),
	{ }
};

static const struct ts_dmi_data chuwi_hi10_plus_data = {
	.embedded_fw = {
		.name	= "silead/gsl1680-chuwi-hi10plus.fw",
		.prefix = { 0xf0, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
		.length	= 34056,
		.sha256	= { 0xfd, 0x0a, 0x08, 0x08, 0x3c, 0xa6, 0x34, 0x4e,
			    0x2c, 0x49, 0x9c, 0xcd, 0x7d, 0x44, 0x9d, 0x38,
			    0x10, 0x68, 0xb5, 0xbd, 0xb7, 0x2a, 0x63, 0xb5,
			    0x67, 0x0b, 0x96, 0xbd, 0x89, 0x67, 0x85, 0x09 },
	},
	.acpi_name      = "MSSL0017:00",
	.properties     = chuwi_hi10_plus_props,
};

static const u32 chuwi_hi10_pro_efi_min_max[] = { 8, 1911, 8, 1271 };

static const struct property_entry chuwi_hi10_pro_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 80),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 26),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1962),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1254),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-chuwi-hi10-pro.fw"),
	PROPERTY_ENTRY_U32_ARRAY("silead,efi-fw-min-max", chuwi_hi10_pro_efi_min_max),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	PROPERTY_ENTRY_BOOL("silead,pen-supported"),
	PROPERTY_ENTRY_U32("silead,pen-resolution-x", 8),
	PROPERTY_ENTRY_U32("silead,pen-resolution-y", 8),
	{ }
};

static const struct ts_dmi_data chuwi_hi10_pro_data = {
	.embedded_fw = {
		.name	= "silead/gsl1680-chuwi-hi10-pro.fw",
		.prefix = { 0xf0, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
		.length	= 42504,
		.sha256	= { 0xdb, 0x92, 0x68, 0xa8, 0xdb, 0x81, 0x31, 0x00,
			    0x1f, 0x58, 0x89, 0xdb, 0x19, 0x1b, 0x15, 0x8c,
			    0x05, 0x14, 0xf4, 0x95, 0xba, 0x15, 0x45, 0x98,
			    0x42, 0xa3, 0xbb, 0x65, 0xe3, 0x30, 0xa5, 0x93 },
	},
	.acpi_name      = "MSSL1680:00",
	.properties     = chuwi_hi10_pro_props,
};

static const struct property_entry chuwi_hibook_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 30),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 4),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1892),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1276),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-chuwi-hibook.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data chuwi_hibook_data = {
	.embedded_fw = {
		.name	= "silead/gsl1680-chuwi-hibook.fw",
		.prefix = { 0xf0, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
		.length	= 40392,
		.sha256	= { 0xf7, 0xc0, 0xe8, 0x5a, 0x6c, 0xf2, 0xeb, 0x8d,
			    0x12, 0xc4, 0x45, 0xbf, 0x55, 0x13, 0x4c, 0x1a,
			    0x13, 0x04, 0x31, 0x08, 0x65, 0x73, 0xf7, 0xa8,
			    0x1b, 0x7d, 0x59, 0xc9, 0xe6, 0x97, 0xf7, 0x38 },
	},
	.acpi_name      = "MSSL0017:00",
	.properties     = chuwi_hibook_props,
};

static const struct property_entry chuwi_vi8_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 4),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 6),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1724),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1140),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3676-chuwi-vi8.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data chuwi_vi8_data = {
	.acpi_name      = "MSSL1680:00",
	.properties     = chuwi_vi8_props,
};

static const struct ts_dmi_data chuwi_vi8_plus_data = {
	.embedded_fw = {
		.name	= "chipone/icn8505-HAMP0002.fw",
		.prefix = { 0xb0, 0x07, 0x00, 0x00, 0xe4, 0x07, 0x00, 0x00 },
		.length	= 35012,
		.sha256	= { 0x93, 0xe5, 0x49, 0xe0, 0xb6, 0xa2, 0xb4, 0xb3,
			    0x88, 0x96, 0x34, 0x97, 0x5e, 0xa8, 0x13, 0x78,
			    0x72, 0x98, 0xb8, 0x29, 0xeb, 0x5c, 0xa7, 0xf1,
			    0x25, 0x13, 0x43, 0xf4, 0x30, 0x7c, 0xfc, 0x7c },
	},
};

static const struct property_entry chuwi_vi10_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 0),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 4),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1858),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1280),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3680-chuwi-vi10.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data chuwi_vi10_data = {
	.acpi_name      = "MSSL0002:00",
	.properties     = chuwi_vi10_props,
};

static const struct property_entry chuwi_surbook_mini_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 88),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 13),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 2040),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1524),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-chuwi-surbook-mini.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	{ }
};

static const struct ts_dmi_data chuwi_surbook_mini_data = {
	.acpi_name      = "MSSL1680:00",
	.properties     = chuwi_surbook_mini_props,
};

static const struct property_entry connect_tablet9_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 9),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 10),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1664),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 880),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-connect-tablet9.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct ts_dmi_data connect_tablet9_data = {
	.acpi_name      = "MSSL1680:00",
	.properties     = connect_tablet9_props,
};

static const struct property_entry csl_panther_tab_hd_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 1),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 20),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1980),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1526),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-csl-panther-tab-hd.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct ts_dmi_data csl_panther_tab_hd_data = {
	.acpi_name      = "MSSL1680:00",
	.properties     = csl_panther_tab_hd_props,
};

static const struct property_entry cube_iwork8_air_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 1),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 3),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1664),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 896),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3670-cube-iwork8-air.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct ts_dmi_data cube_iwork8_air_data = {
	.embedded_fw = {
		.name	= "silead/gsl3670-cube-iwork8-air.fw",
		.prefix = { 0xf0, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
		.length	= 38808,
		.sha256	= { 0xff, 0x62, 0x2d, 0xd1, 0x8a, 0x78, 0x04, 0x7b,
			    0x33, 0x06, 0xb0, 0x4f, 0x7f, 0x02, 0x08, 0x9c,
			    0x96, 0xd4, 0x9f, 0x04, 0xe1, 0x47, 0x25, 0x25,
			    0x60, 0x77, 0x41, 0x33, 0xeb, 0x12, 0x82, 0xfc },
	},
	.acpi_name	= "MSSL1680:00",
	.properties	= cube_iwork8_air_props,
};

static const struct property_entry cube_knote_i1101_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 20),
	PROPERTY_ENTRY_U32("touchscreen-min-y",  22),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1961),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1513),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3692-cube-knote-i1101.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data cube_knote_i1101_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= cube_knote_i1101_props,
};

static const struct property_entry dexp_ursus_7w_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 890),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 630),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1686-dexp-ursus-7w.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data dexp_ursus_7w_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= dexp_ursus_7w_props,
};

static const struct property_entry dexp_ursus_kx210i_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 5),
	PROPERTY_ENTRY_U32("touchscreen-min-y",  2),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1720),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1137),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-dexp-ursus-kx210i.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data dexp_ursus_kx210i_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= dexp_ursus_kx210i_props,
};

static const struct property_entry digma_citi_e200_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1980),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1500),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1686-digma_citi_e200.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data digma_citi_e200_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= digma_citi_e200_props,
};

static const struct property_entry estar_beauty_hd_props[] = {
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	{ }
};

static const struct ts_dmi_data estar_beauty_hd_data = {
	.acpi_name	= "GDIX1001:00",
	.properties	= estar_beauty_hd_props,
};

/* Generic props + data for upside-down mounted GDIX1001 touchscreens */
static const struct property_entry gdix1001_upside_down_props[] = {
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-x"),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	{ }
};

static const struct ts_dmi_data gdix1001_upside_down_data = {
	.acpi_name	= "GDIX1001",
	.properties	= gdix1001_upside_down_props,
};

static const struct ts_dmi_data gdix1002_upside_down_data = {
	.acpi_name	= "GDIX1002",
	.properties	= gdix1001_upside_down_props,
};

static const struct property_entry gp_electronic_t701_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 960),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 640),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-x"),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-gp-electronic-t701.fw"),
	{ }
};

static const struct ts_dmi_data gp_electronic_t701_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= gp_electronic_t701_props,
};

static const struct property_entry irbis_tw90_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1720),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1138),
	PROPERTY_ENTRY_U32("touchscreen-min-x", 8),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 14),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3680-irbis_tw90.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data irbis_tw90_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= irbis_tw90_props,
};

static const struct property_entry irbis_tw118_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 20),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 30),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1960),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1510),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-irbis-tw118.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct ts_dmi_data irbis_tw118_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= irbis_tw118_props,
};

static const struct property_entry itworks_tw891_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 1),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 5),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1600),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 896),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3670-itworks-tw891.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct ts_dmi_data itworks_tw891_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= itworks_tw891_props,
};

static const struct property_entry jumper_ezpad_6_pro_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1980),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1500),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3692-jumper-ezpad-6-pro.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data jumper_ezpad_6_pro_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= jumper_ezpad_6_pro_props,
};

static const struct property_entry jumper_ezpad_6_pro_b_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1980),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1500),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3692-jumper-ezpad-6-pro-b.fw"),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data jumper_ezpad_6_pro_b_data = {
	.acpi_name      = "MSSL1680:00",
	.properties     = jumper_ezpad_6_pro_b_props,
};

static const struct property_entry jumper_ezpad_6_m4_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 35),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 15),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1950),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1525),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3692-jumper-ezpad-6-m4.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data jumper_ezpad_6_m4_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= jumper_ezpad_6_m4_props,
};

static const struct property_entry jumper_ezpad_7_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 4),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 10),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 2044),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1526),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3680-jumper-ezpad-7.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,stuck-controller-bug"),
	{ }
};

static const struct ts_dmi_data jumper_ezpad_7_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= jumper_ezpad_7_props,
};

static const struct property_entry jumper_ezpad_mini3_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 23),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 16),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1700),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1138),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3676-jumper-ezpad-mini3.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct ts_dmi_data jumper_ezpad_mini3_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= jumper_ezpad_mini3_props,
};

static const struct property_entry mpman_converter9_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 8),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 8),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1664),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 880),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-mpman-converter9.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct ts_dmi_data mpman_converter9_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= mpman_converter9_props,
};

static const struct property_entry mpman_mpwin895cl_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 3),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 9),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1728),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1150),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3680-mpman-mpwin895cl.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data mpman_mpwin895cl_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= mpman_mpwin895cl_props,
};

static const struct property_entry myria_my8307_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1720),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1140),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-x"),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-myria-my8307.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data myria_my8307_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= myria_my8307_props,
};

static const struct property_entry onda_obook_20_plus_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1728),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1148),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-x"),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3676-onda-obook-20-plus.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data onda_obook_20_plus_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= onda_obook_20_plus_props,
};

static const struct property_entry onda_v80_plus_v3_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 22),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 15),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1698),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1140),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3676-onda-v80-plus-v3.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data onda_v80_plus_v3_data = {
	.embedded_fw = {
		.name	= "silead/gsl3676-onda-v80-plus-v3.fw",
		.prefix = { 0xf0, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
		.length	= 37224,
		.sha256	= { 0x8f, 0xbd, 0x8f, 0x0c, 0x6b, 0xba, 0x5b, 0xf5,
			    0xa3, 0xc7, 0xa3, 0xc0, 0x4f, 0xcd, 0xdf, 0x32,
			    0xcc, 0xe4, 0x70, 0xd6, 0x46, 0x9c, 0xd7, 0xa7,
			    0x4b, 0x82, 0x3f, 0xab, 0xc7, 0x90, 0xea, 0x23 },
	},
	.acpi_name	= "MSSL1680:00",
	.properties	= onda_v80_plus_v3_props,
};

static const struct property_entry onda_v820w_32g_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1665),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1140),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-onda-v820w-32g.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data onda_v820w_32g_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= onda_v820w_32g_props,
};

static const struct property_entry onda_v891_v5_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1715),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1140),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-x"),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name",
			      "gsl3676-onda-v891-v5.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data onda_v891_v5_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= onda_v891_v5_props,
};

static const struct property_entry onda_v891w_v1_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 46),
	PROPERTY_ENTRY_U32("touchscreen-min-y",  8),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1676),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1130),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3680-onda-v891w-v1.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data onda_v891w_v1_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= onda_v891w_v1_props,
};

static const struct property_entry onda_v891w_v3_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 35),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 15),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1625),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1135),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3676-onda-v891w-v3.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data onda_v891w_v3_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= onda_v891w_v3_props,
};

static const struct property_entry pipo_w2s_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1660),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 880),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-x"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-pipo-w2s.fw"),
	{ }
};

static const struct ts_dmi_data pipo_w2s_data = {
	.embedded_fw = {
		.name	= "silead/gsl1680-pipo-w2s.fw",
		.prefix = { 0xf0, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
		.length	= 39072,
		.sha256	= { 0xd0, 0x58, 0xc4, 0x7d, 0x55, 0x2d, 0x62, 0x18,
			    0xd1, 0x6a, 0x71, 0x73, 0x0b, 0x3f, 0xbe, 0x60,
			    0xbb, 0x45, 0x8c, 0x52, 0x27, 0xb7, 0x18, 0xf4,
			    0x31, 0x00, 0x6a, 0x49, 0x76, 0xd8, 0x7c, 0xd3 },
	},
	.acpi_name	= "MSSL1680:00",
	.properties	= pipo_w2s_props,
};

static const struct property_entry pipo_w11_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 1),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 15),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1984),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1532),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-pipo-w11.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data pipo_w11_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= pipo_w11_props,
};

static const struct property_entry positivo_c4128b_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 4),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 13),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1915),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1269),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-positivo-c4128b.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct ts_dmi_data positivo_c4128b_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= positivo_c4128b_props,
};

static const struct property_entry pov_mobii_wintab_p800w_v20_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 32),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 16),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1692),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1146),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3680-pov-mobii-wintab-p800w-v20.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data pov_mobii_wintab_p800w_v20_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= pov_mobii_wintab_p800w_v20_props,
};

static const struct property_entry pov_mobii_wintab_p800w_v21_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 1),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 8),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1794),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1148),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3692-pov-mobii-wintab-p800w.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data pov_mobii_wintab_p800w_v21_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= pov_mobii_wintab_p800w_v21_props,
};

static const struct property_entry pov_mobii_wintab_p1006w_v10_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 1),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 3),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1984),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1520),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3692-pov-mobii-wintab-p1006w-v10.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data pov_mobii_wintab_p1006w_v10_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= pov_mobii_wintab_p1006w_v10_props,
};

static const struct property_entry predia_basic_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 3),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 10),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1728),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1144),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3680-predia-basic.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data predia_basic_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= predia_basic_props,
};

static const struct property_entry rca_cambio_w101_v2_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 4),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 20),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1644),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 874),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-rca-cambio-w101-v2.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct ts_dmi_data rca_cambio_w101_v2_data = {
	.acpi_name = "MSSL1680:00",
	.properties = rca_cambio_w101_v2_props,
};

static const struct property_entry rwc_nanote_p8_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-y", 46),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1728),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1140),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-rwc-nanote-p8.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct ts_dmi_data rwc_nanote_p8_data = {
	.acpi_name = "MSSL1680:00",
	.properties = rwc_nanote_p8_props,
};

static const struct property_entry schneider_sct101ctm_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1715),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1140),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-x"),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-schneider-sct101ctm.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data schneider_sct101ctm_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= schneider_sct101ctm_props,
};

static const struct property_entry techbite_arc_11_6_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 5),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 7),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1981),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1270),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-techbite-arc-11-6.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct ts_dmi_data techbite_arc_11_6_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= techbite_arc_11_6_props,
};

static const struct property_entry teclast_tbook11_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 8),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 14),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1916),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1264),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3692-teclast-tbook11.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data teclast_tbook11_data = {
	.embedded_fw = {
		.name	= "silead/gsl3692-teclast-tbook11.fw",
		.prefix = { 0xf0, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
		.length	= 43560,
		.sha256	= { 0x9d, 0xb0, 0x3d, 0xf1, 0x00, 0x3c, 0xb5, 0x25,
			    0x62, 0x8a, 0xa0, 0x93, 0x4b, 0xe0, 0x4e, 0x75,
			    0xd1, 0x27, 0xb1, 0x65, 0x3c, 0xba, 0xa5, 0x0f,
			    0xcd, 0xb4, 0xbe, 0x00, 0xbb, 0xf6, 0x43, 0x29 },
	},
	.acpi_name	= "MSSL1680:00",
	.properties	= teclast_tbook11_props,
};

static const struct property_entry teclast_x16_plus_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 8),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 14),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1916),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1264),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3692-teclast-x16-plus.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data teclast_x16_plus_data = {
	.embedded_fw = {
		.name	= "silead/gsl3692-teclast-x16-plus.fw",
		.prefix = { 0xf0, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
		.length	= 43560,
		.sha256	= { 0x9d, 0xb0, 0x3d, 0xf1, 0x00, 0x3c, 0xb5, 0x25,
			    0x62, 0x8a, 0xa0, 0x93, 0x4b, 0xe0, 0x4e, 0x75,
			    0xd1, 0x27, 0xb1, 0x65, 0x3c, 0xba, 0xa5, 0x0f,
			    0xcd, 0xb4, 0xbe, 0x00, 0xbb, 0xf6, 0x43, 0x29 },
	},
	.acpi_name	= "MSSL1680:00",
	.properties	= teclast_x16_plus_props,
};

static const struct property_entry teclast_x3_plus_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1980),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1500),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-teclast-x3-plus.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data teclast_x3_plus_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= teclast_x3_plus_props,
};

static const struct property_entry teclast_x98plus2_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 2048),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1280),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-x"),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1686-teclast_x98plus2.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	{ }
};

static const struct ts_dmi_data teclast_x98plus2_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= teclast_x98plus2_props,
};

static const struct property_entry trekstor_primebook_c11_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1970),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1530),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-trekstor-primebook-c11.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data trekstor_primebook_c11_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= trekstor_primebook_c11_props,
};

static const struct property_entry trekstor_primebook_c13_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 2624),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1920),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-trekstor-primebook-c13.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data trekstor_primebook_c13_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= trekstor_primebook_c13_props,
};

static const struct property_entry trekstor_primetab_t13b_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 2500),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1900),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-trekstor-primetab-t13b.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-y"),
	{ }
};

static const struct ts_dmi_data trekstor_primetab_t13b_data = {
	.acpi_name  = "MSSL1680:00",
	.properties = trekstor_primetab_t13b_props,
};

static const struct property_entry trekstor_surftab_duo_w1_props[] = {
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-x"),
	{ }
};

static const struct ts_dmi_data trekstor_surftab_duo_w1_data = {
	.acpi_name	= "GDIX1001:00",
	.properties	= trekstor_surftab_duo_w1_props,
};

static const struct property_entry trekstor_surftab_twin_10_1_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 20),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 0),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1890),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1280),
	PROPERTY_ENTRY_U32("touchscreen-inverted-y", 1),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3670-surftab-twin-10-1-st10432-8.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data trekstor_surftab_twin_10_1_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= trekstor_surftab_twin_10_1_props,
};

static const struct property_entry trekstor_surftab_wintron70_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-min-x", 12),
	PROPERTY_ENTRY_U32("touchscreen-min-y", 8),
	PROPERTY_ENTRY_U32("touchscreen-size-x", 884),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 632),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1686-surftab-wintron70-st70416-6.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data trekstor_surftab_wintron70_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= trekstor_surftab_wintron70_props,
};

static const struct property_entry viglen_connect_10_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1890),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1280),
	PROPERTY_ENTRY_U32("touchscreen-fuzz-x", 6),
	PROPERTY_ENTRY_U32("touchscreen-fuzz-y", 6),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl3680-viglen-connect-10.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data viglen_connect_10_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= viglen_connect_10_props,
};

static const struct property_entry vinga_twizzle_j116_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1920),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1280),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-vinga-twizzle_j116.fw"),
	PROPERTY_ENTRY_U32("silead,max-fingers", 10),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	{ }
};

static const struct ts_dmi_data vinga_twizzle_j116_data = {
	.acpi_name	= "MSSL1680:00",
	.properties	= vinga_twizzle_j116_props,
};

/* NOTE: Please keep this table sorted alphabetically */
const struct dmi_system_id touchscreen_dmi_table[] = {
	{
		/* Archos 101 Cesium Educ */
		.driver_data = (void *)&archos_101_cesium_educ_data,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "ARCHOS 101 Cesium Educ"),
		},
	},
	{
		/* Bush Windows tablet */
		.driver_data = (void *)&bush_bush_windows_tablet_data,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Bush Windows tablet"),
		},
	},
	{
		/* Chuwi Hi8 */
		.driver_data = (void *)&chuwi_hi8_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ilife"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S806"),
		},
	},
	{
		/* Chuwi Hi8 (H1D_S806_206) */
		.driver_data = (void *)&chuwi_hi8_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "BayTrail"),
			DMI_MATCH(DMI_BIOS_VERSION, "H1D_S806_206"),
		},
	},
	{
		/* Chuwi Hi8 Air (CWI543) */
		.driver_data = (void *)&chuwi_hi8_air_data,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Default string"),
			DMI_MATCH(DMI_BOARD_NAME, "Cherry Trail CR"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Hi8 Air"),
		},
	},
	{
		/* Chuwi Hi8 Pro (CWI513) */
		.driver_data = (void *)&chuwi_hi8_pro_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hampoo"),
			DMI_MATCH(DMI_PRODUCT_NAME, "X1D3_C806N"),
		},
	},
	{
		/* Chuwi Hi10 Air */
		.driver_data = (void *)&chuwi_hi10_air_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "CHUWI INNOVATION AND TECHNOLOGY(SHENZHEN)CO.LTD"),
			DMI_MATCH(DMI_BOARD_NAME, "Cherry Trail CR"),
			DMI_MATCH(DMI_PRODUCT_SKU, "P1W6_C109D_B"),
		},
	},
	{
		/* Chuwi Hi10 Plus (CWI527) */
		.driver_data = (void *)&chuwi_hi10_plus_data,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hampoo"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Hi10 plus tablet"),
			DMI_MATCH(DMI_BOARD_NAME, "Cherry Trail CR"),
		},
	},
	{
		/* Chuwi Hi10 Pro (CWI529) */
		.driver_data = (void *)&chuwi_hi10_pro_data,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hampoo"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Hi10 pro tablet"),
			DMI_MATCH(DMI_BOARD_NAME, "Cherry Trail CR"),
		},
	},
	{
		/* Chuwi HiBook (CWI514) */
		.driver_data = (void *)&chuwi_hibook_data,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hampoo"),
			DMI_MATCH(DMI_BOARD_NAME, "Cherry Trail CR"),
			/* Above matches are too generic, add bios-date match */
			DMI_MATCH(DMI_BIOS_DATE, "05/07/2016"),
		},
	},
	{
		/* Chuwi Vi8 (CWI501) */
		.driver_data = (void *)&chuwi_vi8_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "i86"),
			DMI_MATCH(DMI_BIOS_VERSION, "CHUWI.W86JLBNR01"),
		},
	},
	{
		/* Chuwi Vi8 (CWI506) */
		.driver_data = (void *)&chuwi_vi8_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "i86"),
			DMI_MATCH(DMI_BIOS_VERSION, "CHUWI.D86JLBNR"),
		},
	},
	{
		/* Chuwi Vi8 dual-boot (CWI506) */
		.driver_data = (void *)&chuwi_vi8_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "i86"),
			DMI_MATCH(DMI_BIOS_VERSION, "CHUWI2.D86JHBNR02"),
		},
	},
	{
		/* Chuwi Vi8 Plus (CWI519) */
		.driver_data = (void *)&chuwi_vi8_plus_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hampoo"),
			DMI_MATCH(DMI_PRODUCT_NAME, "D2D3_Vi8A1"),
			DMI_MATCH(DMI_BOARD_NAME, "Cherry Trail CR"),
		},
	},
	{
		/* Chuwi Vi10 (CWI505) */
		.driver_data = (void *)&chuwi_vi10_data,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hampoo"),
			DMI_MATCH(DMI_BOARD_NAME, "BYT-PF02"),
			DMI_MATCH(DMI_SYS_VENDOR, "ilife"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S165"),
		},
	},
	{
		/* Chuwi Surbook Mini (CWI540) */
		.driver_data = (void *)&chuwi_surbook_mini_data,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hampoo"),
			DMI_MATCH(DMI_PRODUCT_NAME, "C3W6_AP108_4G"),
		},
	},
	{
		/* Connect Tablet 9 */
		.driver_data = (void *)&connect_tablet9_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Connect"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Tablet 9"),
		},
	},
	{
		/* CSL Panther Tab HD */
		.driver_data = (void *)&csl_panther_tab_hd_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "CSL Computer GmbH & Co. KG"),
			DMI_MATCH(DMI_PRODUCT_NAME, "CSL Panther Tab HD"),
		},
	},
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
		/* Cube KNote i1101 */
		.driver_data = (void *)&cube_knote_i1101_data,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hampoo"),
			DMI_MATCH(DMI_BOARD_NAME, "L1W6_I1101"),
			DMI_MATCH(DMI_SYS_VENDOR, "ALLDOCUBE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "i1101"),
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
		/* DEXP Ursus KX210i */
		.driver_data = (void *)&dexp_ursus_kx210i_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "INSYDE Corp."),
			DMI_MATCH(DMI_PRODUCT_NAME, "S107I"),
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
	{
		/* Estar Beauty HD (MID 7316R) */
		.driver_data = (void *)&estar_beauty_hd_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Estar"),
			DMI_MATCH(DMI_PRODUCT_NAME, "eSTAR BEAUTY HD Intel Quad core"),
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
		/* I.T.Works TW701 (same hardware as the Trekstor ST70416-6) */
		.driver_data = (void *)&trekstor_surftab_wintron70_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "i71c"),
			DMI_MATCH(DMI_BIOS_VERSION, "itWORKS.G.WI71C.JGBMRB"),
		},
	},
	{
		/* Irbis TW90 */
		.driver_data = (void *)&irbis_tw90_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IRBIS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TW90"),
		},
	},
	{
		/* Irbis TW118 */
		.driver_data = (void *)&irbis_tw118_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IRBIS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TW118"),
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
		/* Jumper EZpad 6 Pro */
		.driver_data = (void *)&jumper_ezpad_6_pro_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Jumper"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EZpad"),
			DMI_MATCH(DMI_BIOS_VERSION, "5.12"),
			/* Above matches are too generic, add bios-date match */
			DMI_MATCH(DMI_BIOS_DATE, "08/18/2017"),
		},
	},
	{
		/* Jumper EZpad 6 Pro B */
		.driver_data = (void *)&jumper_ezpad_6_pro_b_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Jumper"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EZpad"),
			DMI_MATCH(DMI_BIOS_VERSION, "5.12"),
			/* Above matches are too generic, add bios-date match */
			DMI_MATCH(DMI_BIOS_DATE, "04/24/2018"),
		},
	},
	{
		/* Jumper EZpad 6 m4 */
		.driver_data = (void *)&jumper_ezpad_6_m4_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "jumper"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EZpad"),
			/* Jumper8.S106x.A00C.1066 with the version dropped */
			DMI_MATCH(DMI_BIOS_VERSION, "Jumper8.S106x"),
		},
	},
	{
		/* Jumper EZpad 7 */
		.driver_data = (void *)&jumper_ezpad_7_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Jumper"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EZpad"),
			/* Jumper12x.WJ2012.bsBKRCP05 with the version dropped */
			DMI_MATCH(DMI_BIOS_VERSION, "Jumper12x.WJ2012.bsBKRCP"),
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
		/* Juno Tablet */
		.driver_data = (void *)&gdix1002_upside_down_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Default string"),
			/* Both product- and board-name being "Default string" is somewhat rare */
			DMI_MATCH(DMI_PRODUCT_NAME, "Default string"),
			DMI_MATCH(DMI_BOARD_NAME, "Default string"),
			/* Above matches are too generic, add partial bios-version match */
			DMI_MATCH(DMI_BIOS_VERSION, "JP2V1."),
		},
	},
	{
		/* Mediacom WinPad 7.0 W700 (same hw as Wintron surftab 7") */
		.driver_data = (void *)&trekstor_surftab_wintron70_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDIACOM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "WinPad 7 W10 - WPW700"),
		},
	},
	{
		/* Mediacom Flexbook Edge 11 (same hw as TS Primebook C11) */
		.driver_data = (void *)&trekstor_primebook_c11_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDIACOM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "FlexBook edge11 - M-FBE11"),
		},
	},
	{
		/* MP Man Converter 9 */
		.driver_data = (void *)&mpman_converter9_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MPMAN"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Converter9"),
		},
	},
	{
		/* MP Man MPWIN895CL */
		.driver_data = (void *)&mpman_mpwin895cl_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MPMAN"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MPWIN8900CL"),
		},
	},
	{
		/* Myria MY8307 */
		.driver_data = (void *)&myria_my8307_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Complet Electro Serv"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MY8307"),
		},
	},
	{
		/* Onda oBook 20 Plus */
		.driver_data = (void *)&onda_obook_20_plus_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ONDA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "OBOOK 20 PLUS"),
		},
	},
	{
		/* ONDA V80 plus v3 (P80PSBG9V3A01501) */
		.driver_data = (void *)&onda_v80_plus_v3_data,
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ONDA"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "V80 PLUS")
		},
	},
	{
		/* ONDA V820w DualOS */
		.driver_data = (void *)&onda_v820w_32g_data,
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "ONDA"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "V820w DualOS")
		},
	},
	{
		/* ONDA V891 v5 */
		.driver_data = (void *)&onda_v891_v5_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ONDA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ONDA Tablet"),
			DMI_MATCH(DMI_BIOS_VERSION, "ONDA.D869CJABNRBA06"),
		},
	},
	{
		/* ONDA V891w revision P891WBEBV1B00 aka v1 */
		.driver_data = (void *)&onda_v891w_v1_data,
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "ONDA"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "ONDA Tablet"),
			DMI_EXACT_MATCH(DMI_BOARD_VERSION, "V001"),
			/* Exact match, different versions need different fw */
			DMI_EXACT_MATCH(DMI_BIOS_VERSION, "ONDA.W89EBBN08"),
		},
	},
	{
		/* ONDA V891w Dual OS P891DCF2V1A01274 64GB */
		.driver_data = (void *)&onda_v891w_v3_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ONDA Tablet"),
			DMI_MATCH(DMI_BIOS_VERSION, "ONDA.D890HBBNR0A"),
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
		/* Pipo W11 */
		.driver_data = (void *)&pipo_w11_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "PIPO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "To be filled by O.E.M."),
			/* Above matches are too generic, add bios-ver match */
			DMI_MATCH(DMI_BIOS_VERSION, "JS-BI-10.6-SF133GR300-GA55B-024-F"),
		},
	},
	{
		/* Ployer Momo7w (same hardware as the Trekstor ST70416-6) */
		.driver_data = (void *)&trekstor_surftab_wintron70_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Shenzhen PLOYER"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MOMO7W"),
			/* Exact match, different versions need different fw */
			DMI_MATCH(DMI_BIOS_VERSION, "MOMO.G.WI71C.MABMRBA02"),
		},
	},
	{
		/* Positivo C4128B */
		.driver_data = (void *)&positivo_c4128b_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Positivo Tecnologia SA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "C4128B-1"),
		},
	},
	{
		/* Point of View mobii wintab p800w (v2.0) */
		.driver_data = (void *)&pov_mobii_wintab_p800w_v20_data,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "Aptio CRB"),
			DMI_MATCH(DMI_BIOS_VERSION, "3BAIR1014"),
			/* Above matches are too generic, add bios-date match */
			DMI_MATCH(DMI_BIOS_DATE, "10/24/2014"),
		},
	},
	{
		/* Predia Basic tablet) */
		.driver_data = (void *)&predia_basic_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "CherryTrail"),
			/* Above matches are too generic, add bios-version match */
			DMI_MATCH(DMI_BIOS_VERSION, "Mx.WT107.KUBNGEA"),
		},
	},
	{
		/* Point of View mobii wintab p800w (v2.1) */
		.driver_data = (void *)&pov_mobii_wintab_p800w_v21_data,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "Aptio CRB"),
			DMI_MATCH(DMI_BIOS_VERSION, "3BAIR1013"),
			/* Above matches are too generic, add bios-date match */
			DMI_MATCH(DMI_BIOS_DATE, "08/22/2014"),
		},
	},
	{
		/* Point of View mobii wintab p1006w (v1.0) */
		.driver_data = (void *)&pov_mobii_wintab_p1006w_v10_data,
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "BayTrail"),
			/* Note 105b is Foxcon's USB/PCI vendor id */
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "105B"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "0E57"),
		},
	},
	{
		/* RCA Cambio W101 v2 */
		/* https://github.com/onitake/gsl-firmware/discussions/193 */
		.driver_data = (void *)&rca_cambio_w101_v2_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "RCA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "W101SA23T1"),
		},
	},
	{
		/* RWC NANOTE P8 */
		.driver_data = (void *)&rwc_nanote_p8_data,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Default string"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AY07J"),
			DMI_MATCH(DMI_PRODUCT_SKU, "0001")
		},
	},
	{
		/* Schneider SCT101CTM */
		.driver_data = (void *)&schneider_sct101ctm_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Default string"),
			DMI_MATCH(DMI_PRODUCT_NAME, "SCT101CTM"),
		},
	},
	{
		/* Techbite Arc 11.6 */
		.driver_data = (void *)&techbite_arc_11_6_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "mPTech"),
			DMI_MATCH(DMI_PRODUCT_NAME, "techBite Arc 11.6"),
			DMI_MATCH(DMI_BOARD_NAME, "G8316_272B"),
		},
	},
	{
		/* Teclast Tbook 11 */
		.driver_data = (void *)&teclast_tbook11_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TECLAST"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TbooK 11"),
			DMI_MATCH(DMI_PRODUCT_SKU, "E5A6_A1"),
		},
	},
	{
		/* Teclast X16 Plus */
		.driver_data = (void *)&teclast_x16_plus_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TECLAST"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Default string"),
			DMI_MATCH(DMI_PRODUCT_SKU, "D3A5_A1"),
		},
	},
	{
		/* Teclast X3 Plus */
		.driver_data = (void *)&teclast_x3_plus_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TECLAST"),
			DMI_MATCH(DMI_PRODUCT_NAME, "X3 Plus"),
			DMI_MATCH(DMI_BOARD_NAME, "X3 Plus"),
		},
	},
	{
		/* Teclast X89 (Android version / BIOS) */
		.driver_data = (void *)&gdix1001_upside_down_data,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "WISKY"),
			DMI_MATCH(DMI_BOARD_NAME, "3G062i"),
		},
	},
	{
		/* Teclast X89 (Windows version / BIOS) */
		.driver_data = (void *)&gdix1001_upside_down_data,
		.matches = {
			/* tPAD is too generic, also match on bios date */
			DMI_MATCH(DMI_BOARD_VENDOR, "TECLAST"),
			DMI_MATCH(DMI_BOARD_NAME, "tPAD"),
			DMI_MATCH(DMI_BIOS_DATE, "12/19/2014"),
		},
	},
	{
		/* Teclast X98 Plus II */
		.driver_data = (void *)&teclast_x98plus2_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TECLAST"),
			DMI_MATCH(DMI_PRODUCT_NAME, "X98 Plus II"),
		},
	},
	{
		/* Teclast X98 Pro */
		.driver_data = (void *)&gdix1001_upside_down_data,
		.matches = {
			/*
			 * Only match BIOS date, because the manufacturers
			 * BIOS does not report the board name at all
			 * (sometimes)...
			 */
			DMI_MATCH(DMI_BOARD_VENDOR, "TECLAST"),
			DMI_MATCH(DMI_BIOS_DATE, "10/28/2015"),
		},
	},
	{
		/* Trekstor Primebook C11 */
		.driver_data = (void *)&trekstor_primebook_c11_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TREKSTOR"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Primebook C11"),
		},
	},
	{
		/* Trekstor Primebook C11B (same touchscreen as the C11) */
		.driver_data = (void *)&trekstor_primebook_c11_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TREKSTOR"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PRIMEBOOK C11B"),
		},
	},
	{
		/* Trekstor Primebook C13 */
		.driver_data = (void *)&trekstor_primebook_c13_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TREKSTOR"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Primebook C13"),
		},
	},
	{
		/* Trekstor Primetab T13B */
		.driver_data = (void *)&trekstor_primetab_t13b_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TREKSTOR"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Primetab T13B"),
		},
	},
	{
		/* TrekStor SurfTab duo W1 10.1 ST10432-10b */
		.driver_data = (void *)&trekstor_surftab_duo_w1_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TrekStor"),
			DMI_MATCH(DMI_PRODUCT_NAME, "SurfTab duo W1 10.1 (VT4)"),
		},
	},
	{
		/* TrekStor SurfTab twin 10.1 ST10432-8 */
		.driver_data = (void *)&trekstor_surftab_twin_10_1_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TrekStor"),
			DMI_MATCH(DMI_PRODUCT_NAME, "SurfTab twin 10.1"),
		},
	},
	{
		/* Trekstor Surftab Wintron 7.0 ST70416-6 */
		.driver_data = (void *)&trekstor_surftab_wintron70_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ST70416-6"),
			/* Exact match, different versions need different fw */
			DMI_MATCH(DMI_BIOS_VERSION, "TREK.G.WI71C.JGBMRBA04"),
		},
	},
	{
		/* Trekstor Surftab Wintron 7.0 ST70416-6, newer BIOS */
		.driver_data = (void *)&trekstor_surftab_wintron70_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TrekStor"),
			DMI_MATCH(DMI_PRODUCT_NAME, "SurfTab wintron 7.0 ST70416-6"),
			/* Exact match, different versions need different fw */
			DMI_MATCH(DMI_BIOS_VERSION, "TREK.G.WI71C.JGBMRBA05"),
		},
	},
	{
		/* Trekstor Yourbook C11B (same touchscreen as the Primebook C11) */
		.driver_data = (void *)&trekstor_primebook_c11_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TREKSTOR"),
			DMI_MATCH(DMI_PRODUCT_NAME, "YOURBOOK C11B"),
		},
	},
	{
		/* Viglen Connect 10 */
		.driver_data = (void *)&viglen_connect_10_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Viglen Ltd."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Connect 10'' Tablet PC"),
		},
	},
	{
		/* Vinga Twizzle J116 */
		.driver_data = (void *)&vinga_twizzle_j116_data,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "VINGA Twizzle J116"),
		},
	},
	{
		/* "WinBook TW100" */
		.driver_data = (void *)&gdix1001_upside_down_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "WinBook"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TW100")
		}
	},
	{
		/* WinBook TW700 */
		.driver_data = (void *)&gdix1001_upside_down_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "WinBook"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TW700")
		},
	},
	{
		/* Yours Y8W81, same case and touchscreen as Chuwi Vi8 */
		.driver_data = (void *)&chuwi_vi8_data,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "YOURS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Y8W81"),
		},
	},
	{ }
};

static const struct ts_dmi_data *ts_data;

static void ts_dmi_add_props(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	int error;

	if (has_acpi_companion(dev) &&
	    strstarts(client->name, ts_data->acpi_name)) {
		error = device_create_managed_software_node(dev, ts_data->properties, NULL);
		if (error)
			dev_err(dev, "failed to add properties: %d\n", error);
	}
}

static int ts_dmi_notifier_call(struct notifier_block *nb,
				unsigned long action, void *data)
{
	struct device *dev = data;
	struct i2c_client *client;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		client = i2c_verify_client(dev);
		if (client)
			ts_dmi_add_props(client);
		break;

	default:
		break;
	}

	return 0;
}

static struct notifier_block ts_dmi_notifier = {
	.notifier_call = ts_dmi_notifier_call,
};

static int __init ts_dmi_init(void)
{
	const struct dmi_system_id *dmi_id;
	int error;

	dmi_id = dmi_first_match(touchscreen_dmi_table);
	if (!dmi_id)
		return 0; /* Not an error */

	ts_data = dmi_id->driver_data;
	/* Some dmi table entries only provide an efi_embedded_fw_desc */
	if (!ts_data->properties)
		return 0;

	error = bus_register_notifier(&i2c_bus_type, &ts_dmi_notifier);
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
arch_initcall(ts_dmi_init);
