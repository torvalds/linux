/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation version 2 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/vmw_vmci_defs.h>
#include <linux/vmw_vmci_api.h>
#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include "vmci_driver.h"
#include "vmci_event.h"

static bool vmci_disable_host;
module_param_named(disable_host, vmci_disable_host, bool, 0);
MODULE_PARM_DESC(disable_host,
		 "Disable driver host personality (default=enabled)");

static bool vmci_disable_guest;
module_param_named(disable_guest, vmci_disable_guest, bool, 0);
MODULE_PARM_DESC(disable_guest,
		 "Disable driver guest personality (default=enabled)");

static bool vmci_guest_personality_initialized;
static bool vmci_host_personality_initialized;

/*
 * vmci_get_context_id() - Gets the current context ID.
 *
 * Returns the current context ID.  Note that since this is accessed only
 * from code running in the host, this always returns the host context ID.
 */
u32 vmci_get_context_id(void)
{
	if (vmci_guest_code_active())
		return vmci_get_vm_context_id();
	else if (vmci_host_code_active())
		return VMCI_HOST_CONTEXT_ID;

	return VMCI_INVALID_ID;
}
EXPORT_SYMBOL_GPL(vmci_get_context_id);

static int __init vmci_drv_init(void)
{
	int vmci_err;
	int error;

	vmci_err = vmci_event_init();
	if (vmci_err < VMCI_SUCCESS) {
		pr_err("Failed to initialize VMCIEvent (result=%d)\n",
		       vmci_err);
		return -EINVAL;
	}

	if (!vmci_disable_guest) {
		error = vmci_guest_init();
		if (error) {
			pr_warn("Failed to initialize guest personality (err=%d)\n",
				error);
		} else {
			vmci_guest_personality_initialized = true;
			pr_info("Guest personality initialized and is %s\n",
				vmci_guest_code_active() ?
				"active" : "inactive");
		}
	}

	if (!vmci_disable_host) {
		error = vmci_host_init();
		if (error) {
			pr_warn("Unable to initialize host personality (err=%d)\n",
				error);
		} else {
			vmci_host_personality_initialized = true;
			pr_info("Initialized host personality\n");
		}
	}

	if (!vmci_guest_personality_initialized &&
	    !vmci_host_personality_initialized) {
		vmci_event_exit();
		return -ENODEV;
	}

	return 0;
}
module_init(vmci_drv_init);

static void __exit vmci_drv_exit(void)
{
	if (vmci_guest_personality_initialized)
		vmci_guest_exit();

	if (vmci_host_personality_initialized)
		vmci_host_exit();

	vmci_event_exit();
}
module_exit(vmci_drv_exit);

MODULE_AUTHOR("VMware, Inc.");
MODULE_DESCRIPTION("VMware Virtual Machine Communication Interface.");
MODULE_VERSION("1.0.0.0-k");
MODULE_LICENSE("GPL v2");
