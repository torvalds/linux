/* Intel Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#include <linux/types.h>
#include <linux/module.h>
#include <net/ipv6.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <linux/if_macvlan.h>

#include "fm10k.h"

#define DRV_VERSION	"0.12.2-k"
const char fm10k_driver_version[] = DRV_VERSION;
char fm10k_driver_name[] = "fm10k";
static const char fm10k_driver_string[] =
	"Intel(R) Ethernet Switch Host Interface Driver";
static const char fm10k_copyright[] =
	"Copyright (c) 2013 Intel Corporation.";

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION("Intel(R) Ethernet Switch Host Interface Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

/** fm10k_init_module - Driver Registration Routine
 *
 * fm10k_init_module is the first routine called when the driver is
 * loaded.  All it does is register with the PCI subsystem.
 **/
static int __init fm10k_init_module(void)
{
	pr_info("%s - version %s\n", fm10k_driver_string, fm10k_driver_version);
	pr_info("%s\n", fm10k_copyright);

	return fm10k_register_pci_driver();
}
module_init(fm10k_init_module);

/**
 * fm10k_exit_module - Driver Exit Cleanup Routine
 *
 * fm10k_exit_module is called just before the driver is removed
 * from memory.
 **/
static void __exit fm10k_exit_module(void)
{
	fm10k_unregister_pci_driver();
}
module_exit(fm10k_exit_module);
