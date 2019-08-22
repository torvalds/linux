// SPDX-License-Identifier: GPL-2.0
/*
 * Ultravisor high level interfaces
 *
 * Copyright 2019, IBM Corporation.
 *
 */
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/of_fdt.h>

#include <asm/ultravisor.h>
#include <asm/firmware.h>

int __init early_init_dt_scan_ultravisor(unsigned long node, const char *uname,
					 int depth, void *data)
{
	if (!of_flat_dt_is_compatible(node, "ibm,ultravisor"))
		return 0;

	powerpc_firmware_features |= FW_FEATURE_ULTRAVISOR;
	pr_debug("Ultravisor detected!\n");
	return 1;
}
