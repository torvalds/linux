// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  blacklist.c
 *
 *  Check to see if the given machine has a known bad ACPI BIOS
 *  or if the BIOS is too old.
 *  Check given machine against acpi_rev_dmi_table[].
 *
 *  Copyright (C) 2004 Len Brown <len.brown@intel.com>
 *  Copyright (C) 2002 Andy Grover <andrew.grover@intel.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/dmi.h>

#include "internal.h"

#ifdef CONFIG_DMI
static const struct dmi_system_id acpi_rev_dmi_table[] __initconst;
#endif

/*
 * POLICY: If *anything* doesn't work, put it on the blacklist.
 *	   If they are critical errors, mark it critical, and abort driver load.
 */
static struct acpi_platform_list acpi_blacklist[] __initdata = {
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

	{ }
};

int __init acpi_blacklisted(void)
{
	int i;
	int blacklisted = 0;

	i = acpi_match_platform_list(acpi_blacklist);
	if (i >= 0) {
		pr_err(PREFIX "Vendor \"%6.6s\" System \"%8.8s\" Revision 0x%x has a known ACPI BIOS problem.\n",
		       acpi_blacklist[i].oem_id,
		       acpi_blacklist[i].oem_table_id,
		       acpi_blacklist[i].oem_revision);

		pr_err(PREFIX "Reason: %s. This is a %s error\n",
		       acpi_blacklist[i].reason,
		       (acpi_blacklist[i].data ?
			"non-recoverable" : "recoverable"));

		blacklisted = acpi_blacklist[i].data;
	}

	(void)early_acpi_osi_init();
#ifdef CONFIG_DMI
	dmi_check_system(acpi_rev_dmi_table);
#endif

	return blacklisted;
}
#ifdef CONFIG_DMI
#ifdef CONFIG_ACPI_REV_OVERRIDE_POSSIBLE
static int __init dmi_enable_rev_override(const struct dmi_system_id *d)
{
	printk(KERN_NOTICE PREFIX "DMI detected: %s (force ACPI _REV to 5)\n",
	       d->ident);
	acpi_rev_override_setup(NULL);
	return 0;
}
#endif

static const struct dmi_system_id acpi_rev_dmi_table[] __initconst = {
#ifdef CONFIG_ACPI_REV_OVERRIDE_POSSIBLE
#if !IS_ENABLED(CONFIG_SND_SOC_SOF_BROADWELL)
	/*
	 * DELL XPS 13 (2015) switches sound between HDA and I2S
	 * depending on the ACPI _REV callback. If userspace supports
	 * I2S sufficiently (or if you do not care about sound), you
	 * can safely disable this quirk.
	 */
	{
	 .callback = dmi_enable_rev_override,
	 .ident = "DELL XPS 13 (2015)",
	 .matches = {
		      DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		      DMI_MATCH(DMI_PRODUCT_NAME, "XPS 13 9343"),
		},
	},
#endif
	{
	 .callback = dmi_enable_rev_override,
	 .ident = "DELL Precision 5520",
	 .matches = {
		      DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		      DMI_MATCH(DMI_PRODUCT_NAME, "Precision 5520"),
		},
	},
	{
	 .callback = dmi_enable_rev_override,
	 .ident = "DELL Precision 3520",
	 .matches = {
		      DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		      DMI_MATCH(DMI_PRODUCT_NAME, "Precision 3520"),
		},
	},
	/*
	 * Resolves a quirk with the Dell Latitude 3350 that
	 * causes the ethernet adapter to not function.
	 */
	{
	 .callback = dmi_enable_rev_override,
	 .ident = "DELL Latitude 3350",
	 .matches = {
		      DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		      DMI_MATCH(DMI_PRODUCT_NAME, "Latitude 3350"),
		},
	},
	{
	 .callback = dmi_enable_rev_override,
	 .ident = "DELL Inspiron 7537",
	 .matches = {
		      DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		      DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 7537"),
		},
	},
#endif
	{}
};

#endif /* CONFIG_DMI */
