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
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/bug.h>

#include "vmci_datagram.h"
#include "vmci_resource.h"
#include "vmci_context.h"
#include "vmci_driver.h"
#include "vmci_event.h"
#include "vmci_route.h"

/*
 * struct datagram_entry describes the datagram entity. It is used for datagram
 * entities created only on the host.
 */
struct datagram_entry {
	struct vmci_resource resource;
	u32 flags;
	bool run_delayed;
	vmci_datagram_recv_cb recv_cb;
	void *client_data;
	u32 priv_flags;
};

struct delayed_datagram_info {
	struct datagram_entry *entry;
	struct work_struct work;
	bool in_dg_host_queue;
	/* msg and msg_payload must be together. */
	struct vmci_datagram msg;
	u8 msg_payload[];
};

/* Number of in-flight host->host datagrams */
static atomic_t delayed_dg_host_queue_size = ATOMIC_INIT(0);

/*
 * Create a datagram entry given a handle pointer.
 */
static int dg_create_handle(u32 resource_id,
			    u32 flags,
			    u32 priv_flags,
			    vmci_datagram_recv_cb recv_cb,
			    void *client_data, struct vmci_handle *out_handle)
{
	int result;
	u32 context_id;
	struct vmci_handle handle;
	struct datagram_entry *entry;

	if ((flags & VMCI_FLAG_WELLKNOWN_DG_HND) != 0)
		return VMCI_ERROR_INVALID_ARGS;

	if ((flags & VMCI_FLAG_ANYCID_DG_HND) != 0) {
		context_id = VMCI_INVALID_ID;
	} else {
		context_id = vmci_get_context_id();
		if (context_id == VMCI_INVALID_ID)
			return VMCI_ERROR_NO_RESOURCES;
	}

	handle = vmci_make_handle(context_id, resource_id);

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		pr_warn("Failed allocating memory for datagram entry\n");
		return VMCI_ERROR_NO_MEM;
	}

	entry->run_delayed = (flags & VMCI_FLAG_DG_DELAYED_CB) ? true : false;
	entry->flags = flags;
	entry->recv_cb = recv_cb;
	entry->client_data = client_data;
	entry->priv_flags = priv_flags;

	/* Make datagram resource live. */
	result = vmci_resource_add(&entry->resource,
				   VMCI_RESOURCE_TYPE_DATAGRAM,
				   handle);
	if (result != VMCI_SUCCESS) {
		pr_warn("Failed to add new resource (handle=0x%x:0x%x), error: %d\n",
			handle.context, handle.resource, result);
		kfree(entry);
		return result;
	}

	*out_handle = vmci_resource_handle(&entry->resource);
	return VMCI_SUCCESS;
}

/*
 * Internal utility function with the same purpose as
 * vmci_datagram_get_priv_flags that also takes a context_id.
 */
static int vmci_datagram_get_priv_flags(u32 context_id,
					struct vmci_handle handle,
					u32 *priv_flags)
{
	if (context_id == VMCI_INVALID_ID)
		return VMCI_ERROR_INVALID_ARGS;

	if (context_id == VMCI_HOST_CONTEXT_ID) {
		struct datagram_entry *src_entry;
		struct vmci_resource *resource;

		resource = vmci_resource_by_handle(handle,
						   VMCI_RESOURCE_TYPE_DATAGRAM);
		if (!resource)
			return VMCI_ERROR_INVALID_ARGS;

		src_entry = container_of(resource, struct datagram_entry,
					 resource);
		*priv_flags = src_entry->priv_flags;
		vmci_resource_put(resource);
	} else if (context_id == VMCI_HYPERVISOR_CONTEXT_ID)
		*priv_flags = VMCI_MAX_PRIVILEGE_FLAGS;
	else
		*priv_flags = vmci_context_get_priv_flags(context_id);

	return VMCI_SUCCESS;
}

/*
 * Calls the specified callback in a delayed context.
 */
static void dg_delayed_dispatch(struct work_struct *work)
{
	struct delayed_datagram_info *dg_info =
			container_of(work, struct delayed_datagram_info, work);

	dg_info->entry->recv_cb(dg_info->entry->client_data, &dg_info->msg);

	vmci_resource_put(&dg_info->entry->resource);

	if (dg_info->in_dg_host_queue)
		atomic_dec(&delayed_dg_host_queue_size);

	kfree(dg_info);
}

/*
 * Dispatch datagram as a host, to the host, or other vm context. This
 * function cannot dispatch to hypervisor context handlers. This should
 * have been handled before we get here by vmci_datagram_dispatch.
 * Returns number of bytes sent on success, error code otherwise.
 */
static int dg_dispatch_as_host(u32 context_id, struct vmci_datagram *dg)
{
	int retval;
	size_t dg_size;
	u32 src_priv_flags;

	dg_size = VMCI_DG_SIZE(dg);

	/* Host cannot send to the hypervisor. */
	if (dg->dst.context == VMCI_HYPERVISOR_CONTEXT_ID)
		return VMCI_ERROR_DST_UNREACHABLE;

	/* Check that source handle matches sending context. */
	if (dg->src.context != context_id) {
		pr_devel("Sender context (ID=0x%x) is not owner of src datagram entry (handle=0x%x:0x%x)\n",
			 context_id, dg->src.context, dg->src.resource);
		return VMCI_ERROR_NO_ACCESS;
	}

	/* Get hold of privileges of sending endpoint. */
	retval = vmci_datagram_get_priv_flags(context_id, dg->src,
					      &src_priv_flags);
	if (retval != VMCI_SUCCESS) {
		pr_warn("Couldn't get privileges (handle=0x%x:0x%x)\n",
			dg->src.context, dg->src.resource);
		return retval;
	}

	/* Determine if we should route to host or guest destination. */
	if (dg->dst.context == VMCI_HOST_CONTEXT_ID) {
		/* Route to host datagram entry. */
		struct datagram_entry *dst_entry;
		struct vmci_resource *resource;

		if (dg->src.context == VMCI_HYPERVISOR_CONTEXT_ID &&
		    dg->dst.resource == VMCI_EVENT_HANDLER) {
			return vmci_event_dispatch(dg);
		}

		resource = vmci_resource_by_handle(dg->dst,
						   VMCI_RESOURCE_TYPE_DATAGRAM);
		if (!resource) {
			pr_devel("Sending to invalid destination (handle=0x%x:0x%x)\n",
				 dg->dst.context, dg->dst.resource);
			return VMCI_ERROR_INVALID_RESOURCE;
		}
		dst_entry = container_of(resource, struct datagram_entry,
					 resource);
		if (vmci_deny_interaction(src_priv_flags,
					  dst_entry->priv_flags)) {
			vmci_resource_put(resource);
			return VMCI_ERROR_NO_ACCESS;
		}

		/*
		 * If a VMCI datagram destined for the host is also sent by the
		 * host, we always run it delayed. This ensures that no locks
		 * are held when the datagram callback runs.
		 */
		if (dst_entry->run_delayed ||
		    dg->src.context == VMCI_HOST_CONTEXT_ID) {
			struct delayed_datagram_info *dg_info;

			if (atomic_add_return(1, &delayed_dg_host_queue_size)
			    == VMCI_MAX_DELAYED_DG_HOST_QUEUE_SIZE) {
				atomic_dec(&delayed_dg_host_queue_size);
				vmci_resource_put(resource);
				return VMCI_ERROR_NO_MEM;
			}

			dg_info = kmalloc(sizeof(*dg_info) +
				    (size_t) dg->payload_size, GFP_ATOMIC);
			if (!dg_info) {
				atomic_dec(&delayed_dg_host_queue_size);
				vmci_resource_put(resource);
				return VMCI_ERROR_NO_MEM;
			}

			dg_info->in_dg_host_queue = true;
			dg_info->entry = dst_entry;
			memcpy(&dg_info->msg, dg, dg_size);

			INIT_WORK(&dg_info->work, dg_delayed_dispatch);
			schedule_work(&dg_info->work);
			retval = VMCI_SUCCESS;

		} else {
			retval = dst_entry->recv_cb(dst_entry->client_data, dg);
			vmci_resource_put(resource);
			if (retval < VMCI_SUCCESS)
				return retval;
		}
	} else {
		/* Route to destination VM context. */
		struct vmci_datagram *new_dg;

		if (context_id != dg->dst.context) {
			if (vmci_deny_interaction(src_priv_flags,
						  vmci_context_get_priv_flags
						  (dg->dst.context))) {
				return VMCI_ERROR_NO_ACCESS;
			} else if (VMCI_CONTEXT_IS_VM(context_id)) {
				/*
				 * If the sending context is a VM, it
				 * cannot reach another VM.
				 */

				pr_devel("Datagram communication between VMs not supported (src=0x%x, dst=0x%x)\n",
					 context_id, dg->dst.context);
				return VMCI_ERROR_DST_UNREACHABLE;
			}
		}

		/* We make a copy to enqueue. */
		new_dg = kmalloc(dg_size, GFP_KERNEL);
		if (new_dg == NULL)
			return VMCI_ERROR_NO_MEM;

		memcpy(new_dg, dg, dg_size);
		retval = vmci_ctx_enqueue_datagram(dg->dst.context, new_dg);
		if (retval < VMCI_SUCCESS) {
			kfree(new_dg);
			return retval;
		}
	}

	/*
	 * We currently truncate the size to signed 32 bits. This doesn't
	 * matter for this handler as it only support 4Kb messages.
	 */
	return (int)dg_size;
}

/*
 * Dispatch datagram as a guest, down through the VMX and potentially to
 * the host.
 * Returns number of bytes sent on success, error code otherwise.
 */
static int dg_dispatch_as_guest(struct vmci_datagram *dg)
{
	int retval;
	struct vmci_resource *resource;

	resource = vmci_resource_by_handle(dg->src,
					   VMCI_RESOURCE_TYPE_DATAGRAM);
	if (!resource)
		return VMCI_ERROR_NO_HANDLE;

	retval = vmci_send_datagram(dg);
	vmci_resource_put(resource);
	return retval;
}

/*
 * Dispatch datagram.  This will determine the routing for the datagram
 * and dispatch it accordingly.
 * Returns number of bytes sent on success, error code otherwise.
 */
int vmci_datagram_dispatch(u32 context_id,
			   struct vmci_datagram *dg, bool from_guest)
{
	int retval;
	enum vmci_route route;

	BUILD_BUG_ON(sizeof(struct vmci_datagram) != 24);

	if (VMCI_DG_SIZE(dg) > VMCI_MAX_DG_SIZE) {
		pr_devel("Payload (size=%llu bytes) too big to send\n",
			 (unsigned long long)dg->payload_size);
		return VMCI_ERROR_INVALID_ARGS;
	}

	retval = vmci_route(&dg->src, &dg->dst, from_guest, &route);
	if (retval < VMCI_SUCCESS) {
		pr_devel("Failed to route datagram (src=0x%x, dst=0x%x, err=%d)\n",
			 dg->src.context, dg->dst.context, retval);
		return retval;
	}

	if (VMCI_ROUTE_AS_HOST == route) {
		if (VMCI_INVALID_ID == context_id)
			context_id = VMCI_HOST_CONTEXT_ID;
		return dg_dispatch_as_host(context_id, dg);
	}

	if (VMCI_ROUTE_AS_GUEST == route)
		return dg_dispatch_as_guest(dg);

	pr_warn("Unknown route (%d) for datagram\n", route);
	return VMCI_ERROR_DST_UNREACHABLE;
}

/*
 * Invoke the handler for the given datagram.  This is intended to be
 * called only when acting as a guest and receiving a datagram from the
 * virtual device.
 */
int vmci_datagram_invoke_guest_handler(struct vmci_datagram *dg)
{
	struct vmci_resource *resource;
	struct datagram_entry *dst_entry;

	resource = vmci_resource_by_handle(dg->dst,
					   VMCI_RESOURCE_TYPE_DATAGRAM);
	if (!resource) {
		pr_devel("destination (handle=0x%x:0x%x) doesn't exist\n",
			 dg->dst.context, dg->dst.resource);
		return VMCI_ERROR_NO_HANDLE;
	}

	dst_entry = container_of(resource, struct datagram_entry, resource);
	if (dst_entry->run_delayed) {
		struct delayed_datagram_info *dg_info;

		dg_info = kmalloc(sizeof(*dg_info) + (size_t)dg->payload_size,
				  GFP_ATOMIC);
		if (!dg_info) {
			vmci_resource_put(resource);
			return VMCI_ERROR_NO_MEM;
		}

		dg_info->in_dg_host_queue = false;
		dg_info->entry = dst_entry;
		memcpy(&dg_info->msg, dg, VMCI_DG_SIZE(dg));

		INIT_WORK(&dg_info->work, dg_delayed_dispatch);
		schedule_work(&dg_info->work);
	} else {
		dst_entry->recv_cb(dst_entry->client_data, dg);
		vmci_resource_put(resource);
	}

	return VMCI_SUCCESS;
}

/*
 * vmci_datagram_create_handle_priv() - Create host context datagram endpoint
 * @resource_id:        The resource ID.
 * @flags:      Datagram Flags.
 * @priv_flags: Privilege Flags.
 * @recv_cb:    Callback when receiving datagrams.
 * @client_data:        Pointer for a datagram_entry struct
 * @out_handle: vmci_handle that is populated as a result of this function.
 *
 * Creates a host context datagram endpoint and returns a handle to it.
 */
int vmci_datagram_create_handle_priv(u32 resource_id,
				     u32 flags,
				     u32 priv_flags,
				     vmci_datagram_recv_cb recv_cb,
				     void *client_data,
				     struct vmci_handle *out_handle)
{
	if (out_handle == NULL)
		return VMCI_ERROR_INVALID_ARGS;

	if (recv_cb == NULL) {
		pr_devel("Client callback needed when creating datagram\n");
		return VMCI_ERROR_INVALID_ARGS;
	}

	if (priv_flags & ~VMCI_PRIVILEGE_ALL_FLAGS)
		return VMCI_ERROR_INVALID_ARGS;

	return dg_create_handle(resource_id, flags, priv_flags, recv_cb,
				client_data, out_handle);
}
EXPORT_SYMBOL_GPL(vmci_datagram_create_handle_priv);

/*
 * vmci_datagram_create_handle() - Create host context datagram endpoint
 * @resource_id:        Resource ID.
 * @flags:      Datagram Flags.
 * @recv_cb:    Callback when receiving datagrams.
 * @client_ata: Pointer for a datagram_entry struct
 * @out_handle: vmci_handle that is populated as a result of this function.
 *
 * Creates a host context datagram endpoint and returns a handle to
 * it.  Same as vmci_datagram_create_handle_priv without the priviledge
 * flags argument.
 */
int vmci_datagram_create_handle(u32 resource_id,
				u32 flags,
				vmci_datagram_recv_cb recv_cb,
				void *client_data,
				struct vmci_handle *out_handle)
{
	return vmci_datagram_create_handle_priv(
		resource_id, flags,
		VMCI_DEFAULT_PROC_PRIVILEGE_FLAGS,
		recv_cb, client_data,
		out_handle);
}
EXPORT_SYMBOL_GPL(vmci_datagram_create_handle);

/*
 * vmci_datagram_destroy_handle() - Destroys datagram handle
 * @handle:     vmci_handle to be destroyed and reaped.
 *
 * Use this function to destroy any datagram handles created by
 * vmci_datagram_create_handle{,Priv} functions.
 */
int vmci_datagram_destroy_handle(struct vmci_handle handle)
{
	struct datagram_entry *entry;
	struct vmci_resource *resource;

	resource = vmci_resource_by_handle(handle, VMCI_RESOURCE_TYPE_DATAGRAM);
	if (!resource) {
		pr_devel("Failed to destroy datagram (handle=0x%x:0x%x)\n",
			 handle.context, handle.resource);
		return VMCI_ERROR_NOT_FOUND;
	}

	entry = container_of(resource, struct datagram_entry, resource);

	vmci_resource_put(&entry->resource);
	vmci_resource_remove(&entry->resource);
	kfree(entry);

	return VMCI_SUCCESS;
}
EXPORT_SYMBOL_GPL(vmci_datagram_destroy_handle);

/*
 * vmci_datagram_send() - Send a datagram
 * @msg:        The datagram to send.
 *
 * Sends the provided datagram on its merry way.
 */
int vmci_datagram_send(struct vmci_datagram *msg)
{
	if (msg == NULL)
		return VMCI_ERROR_INVALID_ARGS;

	return vmci_datagram_dispatch(VMCI_INVALID_ID, msg, false);
}
EXPORT_SYMBOL_GPL(vmci_datagram_send);
