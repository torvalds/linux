// SPDX-License-Identifier: GPL-2.0-only
/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
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

static DEFINE_MUTEX(vmci_vsock_mutex); /* protects vmci_vsock_transport_cb */
static vmci_vsock_cb vmci_vsock_transport_cb;
static bool vmci_vsock_cb_host_called;

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

/*
 * vmci_register_vsock_callback() - Register the VSOCK vmci_transport callback.
 *
 * The callback will be called when the first host or guest becomes active,
 * or if they are already active when this function is called.
 * To unregister the callback, call this function with NULL parameter.
 *
 * Returns 0 on success. -EBUSY if a callback is already registered.
 */
int vmci_register_vsock_callback(vmci_vsock_cb callback)
{
	int err = 0;

	mutex_lock(&vmci_vsock_mutex);

	if (vmci_vsock_transport_cb && callback) {
		err = -EBUSY;
		goto out;
	}

	vmci_vsock_transport_cb = callback;

	if (!vmci_vsock_transport_cb) {
		vmci_vsock_cb_host_called = false;
		goto out;
	}

	if (vmci_guest_code_active())
		vmci_vsock_transport_cb(false);

	if (vmci_host_users() > 0) {
		vmci_vsock_cb_host_called = true;
		vmci_vsock_transport_cb(true);
	}

out:
	mutex_unlock(&vmci_vsock_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(vmci_register_vsock_callback);

void vmci_call_vsock_callback(bool is_host)
{
	mutex_lock(&vmci_vsock_mutex);

	if (!vmci_vsock_transport_cb)
		goto out;

	/* In the host, this function could be called multiple times,
	 * but we want to register it only once.
	 */
	if (is_host) {
		if (vmci_vsock_cb_host_called)
			goto out;

		vmci_vsock_cb_host_called = true;
	}

	vmci_vsock_transport_cb(is_host);
out:
	mutex_unlock(&vmci_vsock_mutex);
}

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
MODULE_VERSION("1.1.6.0-k");
MODULE_LICENSE("GPL v2");
