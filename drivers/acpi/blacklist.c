/*
 *  blacklist.c
 *
 *  Check to see if the given machine has a known bad ACPI BIOS
 *  or if the BIOS is too old.
 *  Check given machine against acpi_osi_dmi_table[].
 *
 *  Copyright (C) 2004 Len Brown <len.brown@intel.com>
 *  Copyright (C) 2002 Andy Grover <andrew.grover@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <linux/dmi.h>

enum acpi_blacklist_predicates {
	all_versions,
	less_than_or_equal,
	equal,
	greater_than_or_equal,
};

struct acpi_blacklist_item {
	char oem_id[7];
	char oem_table_id[9];
	u32 oem_revision;
	char *table;
	enum acpi_blacklist_predicates oem_revision_predicate;
	char *reason;
	u32 is_critical_error;
};

static struct dmi_system_id acpi_osi_dmi_table[] __initdata;

/*
 * POLICY: If *anything* doesn't work, put it on the blacklist.
 *	   If they are critical errors, mark it critical, and abort driver load.
 */
static struct acpi_blacklist_item acpi_blacklist[] __initdata = {
	/* Compaq Presario 1700 */
	{"PTLTD ", "  DSDT  ", 0x06040000, ACPI_SIG_DSDT, less_than_or_equal,
	 "Multiple problems", 1},
	/* Sony FX120, FX140, FX150? */
	{"SONY  ", "U0      ", 0x20010313, ACPI_SIG_DSDT, less_than_or_equal,
	 "ACPI driver problem", 1},
	/* Compaq Presario 800, Insyde BIOS */
	{"INT440", "SYSFexxx", 0x00001001, ACPI_SIG_DSDT, less_than_or_equal,
	 "Does not use _REG to protect EC OpRegions", 1},
	/* IBM 600E - _ADR should return 7, but it returns 1 */
	{"IBM   ", "TP600E  ", 0x00000105, ACPI_SIG_DSDT, less_than_or_equal,
	 "Incorrect _ADR", 1},

	{""}
};

#if	CONFIG_ACPI_BLACKLIST_YEAR

static int __init blacklist_by_year(void)
{
	int year = dmi_get_year(DMI_BIOS_DATE);
	/* Doesn't exist? Likely an old system */
	if (year == -1) {
		printk(KERN_ERR PREFIX "no DMI BIOS year, "
			"acpi=force is required to enable ACPI\n" );
		return 1;
	}
	/* 0? Likely a buggy new BIOS */
	if (year == 0) {
		printk(KERN_ERR PREFIX "DMI BIOS year==0, "
			"assuming ACPI-capable machine\n" );
		return 0;
	}
	if (year < CONFIG_ACPI_BLACKLIST_YEAR) {
		printk(KERN_ERR PREFIX "BIOS age (%d) fails cutoff (%d), "
		       "acpi=force is required to enable ACPI\n",
		       year, CONFIG_ACPI_BLACKLIST_YEAR);
		return 1;
	}
	return 0;
}
#else
static inline int blacklist_by_year(void)
{
	return 0;
}
#endif

int __init acpi_blacklisted(void)
{
	int i = 0;
	int blacklisted = 0;
	struct acpi_table_header table_header;

	while (acpi_blacklist[i].oem_id[0] != '\0') {
		if (acpi_get_table_header(acpi_blacklist[i].table, 0, &table_header)) {
			i++;
			continue;
		}

		if (strncmp(acpi_blacklist[i].oem_id, table_header.oem_id, 6)) {
			i++;
			continue;
		}

		if (strncmp
		    (acpi_blacklist[i].oem_table_id, table_header.oem_table_id,
		     8)) {
			i++;
			continue;
		}

		if ((acpi_blacklist[i].oem_revision_predicate == all_versions)
		    || (acpi_blacklist[i].oem_revision_predicate ==
			less_than_or_equal
			&& table_header.oem_revision <=
			acpi_blacklist[i].oem_revision)
		    || (acpi_blacklist[i].oem_revision_predicate ==
			greater_than_or_equal
			&& table_header.oem_revision >=
			acpi_blacklist[i].oem_revision)
		    || (acpi_blacklist[i].oem_revision_predicate == equal
			&& table_header.oem_revision ==
			acpi_blacklist[i].oem_revision)) {

			printk(KERN_ERR PREFIX
			       "Vendor \"%6.6s\" System \"%8.8s\" "
			       "Revision 0x%x has a known ACPI BIOS problem.\n",
			       acpi_blacklist[i].oem_id,
			       acpi_blacklist[i].oem_table_id,
			       acpi_blacklist[i].oem_revision);

			printk(KERN_ERR PREFIX
			       "Reason: %s. This is a %s error\n",
			       acpi_blacklist[i].reason,
			       (acpi_blacklist[i].
				is_critical_error ? "non-recoverable" :
				"recoverable"));

			blacklisted = acpi_blacklist[i].is_critical_error;
			break;
		} else {
			i++;
		}
	}

	blacklisted += blacklist_by_year();

	dmi_check_system(acpi_osi_dmi_table);

	return blacklisted;
}
#ifdef CONFIG_DMI
static int __init dmi_enable_osi_linux(const struct dmi_system_id *d)
{
	acpi_dmi_osi_linux(1, d);	/* enable */
	return 0;
}
static int __init dmi_disable_osi_linux(const struct dmi_system_id *d)
{
	acpi_dmi_osi_linux(0, d);	/* disable */
	return 0;
}
static int __init dmi_unknown_osi_linux(const struct dmi_system_id *d)
{
	acpi_dmi_osi_linux(-1, d);	/* unknown */
	return 0;
}

/*
 * Most BIOS that invoke OSI(Linux) do nothing with it.
 * But some cause Linux to break.
 * Only a couple use it to make Linux run better.
 *
 * Thus, Linux should continue to disable OSI(Linux) by default,
 * should continue to discourage BIOS writers from using it, and
 * should whitelist the few existing systems that require it.
 *
 * If it appears clear a vendor isn't using OSI(Linux)
 * for anything constructive, blacklist them by name to disable
 * unnecessary dmesg warnings on all of their products.
 */

static struct dmi_system_id acpi_osi_dmi_table[] __initdata = {
	/*
	 * Disable OSI(Linux) warnings on all "Acer, inc."
	 *
	 * _OSI(Linux) disables the latest Windows BIOS code:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 3100"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5050"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5100"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5580"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 3010"),
	 * _OSI(Linux) effect unknown:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Ferrari 5000"),
	 */
	/*
	 * note that dmi_check_system() uses strstr()
	 * to match sub-strings rather than !strcmp(),
	 * so "Acer" below matches "Acer, inc." above.
	 */
	/*
	 * Disable OSI(Linux) warnings on all "Acer"
	 *
	 * _OSI(Linux) effect unknown:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5610"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 7720Z"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 5520"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 6460"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 7510"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Extensa 5220"),
	 *
	 * _OSI(Linux) is a NOP:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5315"),
	 */
	{
	.callback = dmi_disable_osi_linux,
	.ident = "Acer",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
		},
	},
	/*
	 * Disable OSI(Linux) warnings on all "Apple Computer, Inc."
	 * Disable OSI(Linux) warnings on all "Apple Inc."
	 *
	 * _OSI(Linux) confirmed to be a NOP:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "MacBook1,1"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "MacBook2,1"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro2,2"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro3,1"),
	 * _OSI(Linux) effect unknown:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "MacPro2,1"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro1,1"),
	 */
	{
	.callback = dmi_disable_osi_linux,
	.ident = "Apple",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Apple"),
		},
	},
	/*
	 * Disable OSI(Linux) warnings on all "BenQ"
	 *
	 * _OSI(Linux) confirmed to be a NOP:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Joybook S31"),
	 */
	{
	.callback = dmi_disable_osi_linux,
	.ident = "BenQ",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "BenQ"),
		},
	},
	/*
	 * Disable OSI(Linux) warnings on all "Clevo Co."
	 *
	 * _OSI(Linux) confirmed to be a NOP:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "M570RU"),
	 */
	{
	.callback = dmi_disable_osi_linux,
	.ident = "Clevo",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Clevo Co."),
		},
	},
	/*
	 * Disable OSI(Linux) warnings on all "COMPAL"
	 *
	 * _OSI(Linux) confirmed to be a NOP:
	 * DMI_MATCH(DMI_BOARD_NAME, "HEL8X"),
	 * _OSI(Linux) unknown effect:
	 * DMI_MATCH(DMI_BOARD_NAME, "IFL91"),
	 */
	{
	.callback = dmi_disable_osi_linux,
	.ident = "Compal",
	.matches = {
		     DMI_MATCH(DMI_BIOS_VENDOR, "COMPAL"),
		},
	},
	{ /* OSI(Linux) touches USB, unknown side-effect */
	.callback = dmi_disable_osi_linux,
	.ident = "Dell Dimension 5150",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		     DMI_MATCH(DMI_PRODUCT_NAME, "Dell DM051"),
		},
	},
	{ /* OSI(Linux) is a NOP */
	.callback = dmi_disable_osi_linux,
	.ident = "Dell i1501",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		     DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 1501"),
		},
	},
	{ /* OSI(Linux) effect unknown */
	.callback = dmi_unknown_osi_linux,
	.ident = "Dell Latitude D830",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		     DMI_MATCH(DMI_PRODUCT_NAME, "Latitude D830"),
		},
	},
	{ /* OSI(Linux) effect unknown */
	.callback = dmi_unknown_osi_linux,
	.ident = "Dell OP GX620",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		     DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex GX620"),
		},
	},
	{ /* OSI(Linux) effect unknown */
	.callback = dmi_unknown_osi_linux,
	.ident = "Dell PE 1900",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		     DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 1900"),
		},
	},
	{ /* OSI(Linux) is a NOP */
	.callback = dmi_disable_osi_linux,
	.ident = "Dell PE R200",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		     DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge R200"),
		},
	},
	{ /* OSI(Linux) touches USB */
	.callback = dmi_disable_osi_linux,
	.ident = "Dell PR 390",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		     DMI_MATCH(DMI_PRODUCT_NAME, "Precision WorkStation 390"),
		},
	},
	{ /* OSI(Linux) is a NOP */
	.callback = dmi_disable_osi_linux,
	.ident = "Dell Vostro 1000",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		     DMI_MATCH(DMI_PRODUCT_NAME, "Vostro   1000"),
		},
	},
	{ /* OSI(Linux) effect unknown */
	.callback = dmi_unknown_osi_linux,
	.ident = "Dell PE SC440",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		     DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge SC440"),
		},
	},
	{ /* OSI(Linux) effect unknown */
	.callback = dmi_unknown_osi_linux,
	.ident = "Dialogue Flybook V5",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Dialogue Technology Corporation"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "Flybook V5"),
		},
	},
	/*
	 * Disable OSI(Linux) warnings on all "FUJITSU SIEMENS"
	 *
	 * _OSI(Linux) disables latest Windows BIOS code:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Pa 2510"),
	 * _OSI(Linux) confirmed to be a NOP:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Pi 1536"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Pi 1556"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Xi 1546"),
	 * _OSI(Linux) unknown effect:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Amilo M1425"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Amilo Si 1520"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "ESPRIMO Mobile V5505"),
	 */
	{
	.callback = dmi_disable_osi_linux,
	.ident = "Fujitsu Siemens",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
		},
	},
	/*
	 * Disable OSI(Linux) warnings on all "Hewlett-Packard"
	 *
	 * _OSI(Linux) confirmed to be a NOP:
	 * .ident = "HP Pavilion tx 1000"
	 * DMI_MATCH(DMI_BOARD_NAME, "30BF"),
	 * .ident = "HP Pavilion dv2000"
	 * DMI_MATCH(DMI_BOARD_NAME, "30B5"),
	 * .ident = "HP Pavilion dv5000",
	 * DMI_MATCH(DMI_BOARD_NAME, "30A7"),
	 * .ident = "HP Pavilion dv6300 30BC",
	 * DMI_MATCH(DMI_BOARD_NAME, "30BC"),
	 * .ident = "HP Pavilion dv6000",
	 * DMI_MATCH(DMI_BOARD_NAME, "30B7"),
	 * DMI_MATCH(DMI_BOARD_NAME, "30B8"),
	 * .ident = "HP Pavilion dv9000",
	 * DMI_MATCH(DMI_BOARD_NAME, "30B9"),
	 * .ident = "HP Pavilion dv9500",
	 * DMI_MATCH(DMI_BOARD_NAME, "30CB"),
	 * .ident = "HP/Compaq Presario C500",
	 * DMI_MATCH(DMI_BOARD_NAME, "30C6"),
	 * .ident = "HP/Compaq Presario F500",
	 * DMI_MATCH(DMI_BOARD_NAME, "30D3"),
	 * _OSI(Linux) unknown effect:
	 * .ident = "HP Pavilion dv6500",
	 * DMI_MATCH(DMI_BOARD_NAME, "30D0"),
	 */
	{
	.callback = dmi_disable_osi_linux,
	.ident = "Hewlett-Packard",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		},
	},
	/*
	 * Lenovo has a mix of systems OSI(Linux) situations
	 * and thus we can not wildcard the vendor.
	 *
	 * _OSI(Linux) helps sound
	 * DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad R61"),
	 * DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad T61"),
	 * _OSI(Linux) is a NOP:
	 * DMI_MATCH(DMI_PRODUCT_VERSION, "3000 N100"),
	 * _OSI(Linux) effect unknown
	 * DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad X61"),
	 */
	{
	.callback = dmi_enable_osi_linux,
	.ident = "Lenovo ThinkPad R61",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad R61"),
		},
	},
	{
	.callback = dmi_enable_osi_linux,
	.ident = "Lenovo ThinkPad T61",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad T61"),
		},
	},
	{
	.callback = dmi_unknown_osi_linux,
	.ident = "Lenovo ThinkPad X61",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad X61"),
		},
	},
	{
	.callback = dmi_unknown_osi_linux,
	.ident = "Lenovo 3000 V100",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "LENOVO3000 V100"),
		},
	},
	{
	.callback = dmi_disable_osi_linux,
	.ident = "Lenovo 3000 N100",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "3000 N100"),
		},
	},
	/*
	 * Disable OSI(Linux) warnings on all "LG Electronics"
	 *
	 * _OSI(Linux) confirmed to be a NOP:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "P1-J150B"),
	 * with DMI_MATCH(DMI_BOARD_NAME, "ROCKY"),
	 *
	 * unknown:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "S1-MDGDG"),
	 * with DMI_MATCH(DMI_BOARD_NAME, "ROCKY"),
	 */
	{
	.callback = dmi_disable_osi_linux,
	.ident = "LG",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "LG Electronics"),
		},
	},
	/* NEC - OSI(Linux) effect unknown */
	{
	.callback = dmi_unknown_osi_linux,
	.ident = "NEC VERSA M360",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "NEC Computers SAS"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "NEC VERSA M360"),
		},
	},
	/* Panasonic */
	{
	.callback = dmi_unknown_osi_linux,
	.ident = "Panasonic",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Matsushita"),
			/* Toughbook CF-52 */
		     DMI_MATCH(DMI_PRODUCT_NAME, "CF-52CCABVBG"),
		},
	},
	/*
	 * Disable OSI(Linux) warnings on all "Samsung Electronics"
	 *
	 * OSI(Linux) disables PNP0C32 and other BIOS code for Windows:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "R40P/R41P"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "R59P/R60P/R61P"),
	 */
	{
	.callback = dmi_disable_osi_linux,
	.ident = "Samsung",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		},
	},
	/*
	 * Disable OSI(Linux) warnings on all "Sony Corporation"
	 *
	 * _OSI(Linux) is a NOP:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "VGN-SZ650N"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "VGN-SZ38GP_C"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "VGN-TZ21MN_N"),
	 * _OSI(Linux) unknown effect:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "VGN-FZ11M"),
	 */
	{
	.callback = dmi_disable_osi_linux,
	.ident = "Sony",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		},
	},
	/*
	 * Disable OSI(Linux) warnings on all "TOSHIBA"
	 *
	 * _OSI(Linux) breaks sound (bugzilla 7787):
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Satellite P100"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Satellite P105"),
	 * _OSI(Linux) is a NOP:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Satellite A100"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Satellite A210"),
	 * _OSI(Linux) unknown effect:
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Satellite A135"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Satellite A200"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Satellite P205"),
	 * DMI_MATCH(DMI_PRODUCT_NAME, "Satellite U305"),
	 */
	{
	.callback = dmi_disable_osi_linux,
	.ident = "Toshiba",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
		},
	},
	{}
};

#endif /* CONFIG_DMI */
