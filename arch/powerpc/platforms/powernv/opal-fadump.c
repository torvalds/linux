// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Firmware-Assisted Dump support on POWER platform (OPAL).
 *
 * Copyright 2019, Hari Bathini, IBM Corporation.
 */

#define pr_fmt(fmt) "opal fadump: " fmt

#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>

#include <asm/opal.h>
#include <asm/fadump-internal.h>

static u64 opal_fadump_init_mem_struct(struct fw_dump *fadump_conf)
{
	return fadump_conf->reserve_dump_area_start;
}

static int opal_fadump_register(struct fw_dump *fadump_conf)
{
	return -EIO;
}

static int opal_fadump_unregister(struct fw_dump *fadump_conf)
{
	return -EIO;
}

static int opal_fadump_invalidate(struct fw_dump *fadump_conf)
{
	return -EIO;
}

static int __init opal_fadump_process(struct fw_dump *fadump_conf)
{
	return -EINVAL;
}

static void opal_fadump_region_show(struct fw_dump *fadump_conf,
				    struct seq_file *m)
{
}

static void opal_fadump_trigger(struct fadump_crash_info_header *fdh,
				const char *msg)
{
	int rc;

	rc = opal_cec_reboot2(OPAL_REBOOT_MPIPL, msg);
	if (rc == OPAL_UNSUPPORTED) {
		pr_emerg("Reboot type %d not supported.\n",
			 OPAL_REBOOT_MPIPL);
	} else if (rc == OPAL_HARDWARE)
		pr_emerg("No backend support for MPIPL!\n");
}

static struct fadump_ops opal_fadump_ops = {
	.fadump_init_mem_struct		= opal_fadump_init_mem_struct,
	.fadump_register		= opal_fadump_register,
	.fadump_unregister		= opal_fadump_unregister,
	.fadump_invalidate		= opal_fadump_invalidate,
	.fadump_process			= opal_fadump_process,
	.fadump_region_show		= opal_fadump_region_show,
	.fadump_trigger			= opal_fadump_trigger,
};

void __init opal_fadump_dt_scan(struct fw_dump *fadump_conf, u64 node)
{
	unsigned long dn;

	/*
	 * Check if Firmware-Assisted Dump is supported. if yes, check
	 * if dump has been initiated on last reboot.
	 */
	dn = of_get_flat_dt_subnode_by_name(node, "dump");
	if (dn == -FDT_ERR_NOTFOUND) {
		pr_debug("FADump support is missing!\n");
		return;
	}

	if (!of_flat_dt_is_compatible(dn, "ibm,opal-dump")) {
		pr_err("Support missing for this f/w version!\n");
		return;
	}

	fadump_conf->ops		= &opal_fadump_ops;
	fadump_conf->fadump_supported	= 1;
}
