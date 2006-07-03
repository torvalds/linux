/*
 * hypervisor.c - /sys/hypervisor subsystem.
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/kobject.h>
#include <linux/device.h>

#include "base.h"

decl_subsys(hypervisor, NULL, NULL);
EXPORT_SYMBOL_GPL(hypervisor_subsys);

int __init hypervisor_init(void)
{
	return subsystem_register(&hypervisor_subsys);
}
