/*
 * sleep.c - x86-specific ACPI sleep support.
 *
 *  Copyright (C) 2001-2003 Patrick Mochel
 *  Copyright (C) 2001-2003 Pavel Machek <pavel@suse.cz>
 */

#include <linux/acpi.h>
#include <linux/bootmem.h>
#include <linux/dmi.h>
#include <linux/cpumask.h>

#include <asm/smp.h>

/* Ouch, we want to delete this. We already have better version in userspace, in
   s2ram from suspend.sf.net project */
static __init int reset_videomode_after_s3(const struct dmi_system_id *d)
{
	acpi_realmode_flags |= 2;
	return 0;
}

static __initdata struct dmi_system_id acpisleep_dmi_table[] = {
	{			/* Reset video mode after returning from ACPI S3 sleep */
	 .callback = reset_videomode_after_s3,
	 .ident = "Toshiba Satellite 4030cdt",
	 .matches = {
		     DMI_MATCH(DMI_PRODUCT_NAME, "S4030CDT/4.3"),
		     },
	 },
	{}
};

static int __init acpisleep_dmi_init(void)
{
	dmi_check_system(acpisleep_dmi_table);
	return 0;
}

core_initcall(acpisleep_dmi_init);
