// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Extracted from cputable.c
 *
 *  Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  Modifications for ppc64:
 *      Copyright (C) 2003 Dave Engebretsen <engebret@us.ibm.com>
 *  Copyright (C) 2005 Stephen Rothwell, IBM Corporation
 */

#include <linux/export.h>
#include <linux/cache.h>
#include <linux/of.h>

#include <asm/firmware.h>
#include <asm/kvm_guest.h>

#ifdef CONFIG_PPC64
unsigned long powerpc_firmware_features __read_mostly;
EXPORT_SYMBOL_GPL(powerpc_firmware_features);
#endif

#if defined(CONFIG_PPC_PSERIES) || defined(CONFIG_KVM_GUEST)
DEFINE_STATIC_KEY_FALSE(kvm_guest);
int __init check_kvm_guest(void)
{
	struct device_node *hyper_node;

	hyper_node = of_find_node_by_path("/hypervisor");
	if (!hyper_node)
		return 0;

	if (!of_device_is_compatible(hyper_node, "linux,kvm"))
		return 0;

	static_branch_enable(&kvm_guest);

	return 0;
}
core_initcall(check_kvm_guest); // before kvm_guest_init()
#endif
