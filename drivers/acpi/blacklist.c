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
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/dmi.h>

#include "internal.h"

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

	dmi_check_system(acpi_osi_dmi_table);

	return blacklisted;
}
#ifdef CONFIG_DMI
static int __init dmi_enable_osi_linux(const struct dmi_system_id *d)
{
	acpi_dmi_osi_linux(1, d);	/* enable */
	return 0;
}
static int __init dmi_disable_osi_vista(const struct dmi_system_id *d)
{
	printk(KERN_NOTICE PREFIX "DMI detected: %s\n", d->ident);
	acpi_osi_setup("!Windows 2006");
	acpi_osi_setup("!Windows 2006 SP1");
	acpi_osi_setup("!Windows 2006 SP2");
	return 0;
}
static int __init dmi_disable_osi_win7(const struct dmi_system_id *d)
{
	printk(KERN_NOTICE PREFIX "DMI detected: %s\n", d->ident);
	acpi_osi_setup("!Windows 2009");
	return 0;
}
static int __init dmi_disable_osi_win8(const struct dmi_system_id *d)
{
	printk(KERN_NOTICE PREFIX "DMI detected: %s\n", d->ident);
	acpi_osi_setup("!Windows 2012");
	return 0;
}

static struct dmi_system_id acpi_osi_dmi_table[] __initdata = {
	{
	.callback = dmi_disable_osi_vista,
	.ident = "Fujitsu Siemens",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "ESPRIMO Mobile V5505"),
		},
	},
	{
	/*
	 * There have a NVIF method in MSI GX723 DSDT need call by Nvidia
	 * driver (e.g. nouveau) when user press brightness hotkey.
	 * Currently, nouveau driver didn't do the job and it causes there
	 * have a infinite while loop in DSDT when user press hotkey.
	 * We add MSI GX723's dmi information to this table for workaround
	 * this issue.
	 * Will remove MSI GX723 from the table after nouveau grows support.
	 */
	.callback = dmi_disable_osi_vista,
	.ident = "MSI GX723",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "GX723"),
		},
	},
	{
	.callback = dmi_disable_osi_vista,
	.ident = "Sony VGN-NS10J_S",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "VGN-NS10J_S"),
		},
	},
	{
	.callback = dmi_disable_osi_vista,
	.ident = "Sony VGN-SR290J",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "VGN-SR290J"),
		},
	},
	{
	.callback = dmi_disable_osi_vista,
	.ident = "VGN-NS50B_L",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "VGN-NS50B_L"),
		},
	},
	{
	.callback = dmi_disable_osi_vista,
	.ident = "Toshiba Satellite L355",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "Satellite L355"),
		},
	},
	{
	.callback = dmi_disable_osi_win7,
	.ident = "ASUS K50IJ",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
		     DMI_MATCH(DMI_PRODUCT_NAME, "K50IJ"),
		},
	},
	{
	.callback = dmi_disable_osi_vista,
	.ident = "Toshiba P305D",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "Satellite P305D"),
		},
	},
	{
	.callback = dmi_disable_osi_vista,
	.ident = "Toshiba NB100",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "NB100"),
		},
	},

	/*
	 * The following machines have broken backlight support when reporting
	 * the Windows 2012 OSI, so disable it until their support is fixed.
	 */
	{
	.callback = dmi_disable_osi_win8,
	.ident = "ASUS Zenbook Prime UX31A",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		     DMI_MATCH(DMI_PRODUCT_NAME, "UX31A"),
		},
	},
	{
	.callback = dmi_disable_osi_win8,
	.ident = "ThinkPad Edge E530",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "3259A2G"),
		},
	},
	{
	.callback = dmi_disable_osi_win8,
	.ident = "ThinkPad Edge E530",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "3259CTO"),
		},
	},
	{
	.callback = dmi_disable_osi_win8,
	.ident = "ThinkPad Edge E530",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "3259HJG"),
		},
	},
	{
	.callback = dmi_disable_osi_win8,
	.ident = "Acer Aspire V5-573G",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Acer Aspire"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "V5-573G/Dazzle_HW"),
		},
	},
	{
	.callback = dmi_disable_osi_win8,
	.ident = "Acer Aspire V5-572G",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Acer Aspire"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "V5-572G/Dazzle_CX"),
		},
	},
	{
	.callback = dmi_disable_osi_win8,
	.ident = "ThinkPad T431s",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "20AACTO1WW"),
		},
	},
	{
	.callback = dmi_disable_osi_win8,
	.ident = "ThinkPad T430",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "2349D15"),
		},
	},
	{
	.callback = dmi_disable_osi_win8,
	.ident = "Dell Inspiron 7737",
	.matches = {
		    DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		    DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 7737"),
		},
	},

	/*
	 * BIOS invocation of _OSI(Linux) is almost always a BIOS bug.
	 * Linux ignores it, except for the machines enumerated below.
	 */

	/*
	 * Lenovo has a mix of systems OSI(Linux) situations
	 * and thus we can not wildcard the vendor.
	 *
	 * _OSI(Linux) helps sound
	 * DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad R61"),
	 * DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad T61"),
	 * T400, T500
	 * _OSI(Linux) has Linux specific hooks
	 * DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad X61"),
	 * _OSI(Linux) is a NOP:
	 * DMI_MATCH(DMI_PRODUCT_VERSION, "3000 N100"),
	 * DMI_MATCH(DMI_PRODUCT_VERSION, "LENOVO3000 V100"),
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
	.callback = dmi_enable_osi_linux,
	.ident = "Lenovo ThinkPad X61",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad X61"),
		},
	},
	{
	.callback = dmi_enable_osi_linux,
	.ident = "Lenovo ThinkPad T400",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad T400"),
		},
	},
	{
	.callback = dmi_enable_osi_linux,
	.ident = "Lenovo ThinkPad T500",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		     DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad T500"),
		},
	},
	/*
	 * Without this this EEEpc exports a non working WMI interface, with
	 * this it exports a working "good old" eeepc_laptop interface, fixing
	 * both brightness control, and rfkill not working.
	 */
	{
	.callback = dmi_enable_osi_linux,
	.ident = "Asus EEE PC 1015PX",
	.matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer INC."),
		     DMI_MATCH(DMI_PRODUCT_NAME, "1015PX"),
		},
	},
	{}
};

#endif /* CONFIG_DMI */
