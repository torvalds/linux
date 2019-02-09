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
#include <linux/completion.h>
#include <linux/hash.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "vmci_datagram.h"
#include "vmci_doorbell.h"
#include "vmci_resource.h"
#include "vmci_driver.h"
#include "vmci_route.h"


#define VMCI_DOORBELL_INDEX_BITS	6
#define VMCI_DOORBELL_INDEX_TABLE_SIZE	(1 << VMCI_DOORBELL_INDEX_BITS)
#define VMCI_DOORBELL_HASH(_idx)	hash_32(_idx, VMCI_DOORBELL_INDEX_BITS)

/*
 * DoorbellEntry describes the a doorbell notification handle allocated by the
 * host.
 */
struct dbell_entry {
	struct vmci_resource resource;
	struct hlist_node node;
	struct work_struct work;
	vmci_callback notify_cb;
	void *client_data;
	u32 idx;
	u32 priv_flags;
	bool run_delayed;
	atomic_t active;	/* Only used by guest personality */
};

/* The VMCI index table keeps track of currently registered doorbells. */
struct dbell_index_table {
	spinlock_t lock;	/* Index table lock */
	struct hlist_head entries[VMCI_DOORBELL_INDEX_TABLE_SIZE];
};

static struct dbell_index_table vmci_doorbell_it = {
	.lock = __SPIN_LOCK_UNLOCKED(vmci_doorbell_it.lock),
};

/*
 * The max_notify_idx is one larger than the currently known bitmap index in
 * use, and is used to determine how much of the bitmap needs to be scanned.
 */
static u32 max_notify_idx;

/*
 * The notify_idx_count is used for determining whether there are free entries
 * within the bitmap (if notify_idx_count + 1 < max_notify_idx).
 */
static u32 notify_idx_count;

/*
 * The last_notify_idx_reserved is used to track the last index handed out - in
 * the case where multiple handles share a notification index, we hand out
 * indexes round robin based on last_notify_idx_reserved.
 */
static u32 last_notify_idx_reserved;

/* This is a one entry cache used to by the index allocation. */
static u32 last_notify_idx_released = PAGE_SIZE;


/*
 * Utility function that retrieves the privilege flags associated
 * with a given doorbell handle. For guest endpoints, the
 * privileges are determined by the context ID, but for host
 * endpoints privileges are associated with the complete
 * handle. Hypervisor endpoints are not yet supported.
 */
int vmci_dbell_get_priv_flags(struct vmci_handle handle, u32 *priv_flags)
{
	if (priv_flags == NULL || handle.context == VMCI_INVALID_ID)
		return VMCI_ERROR_INVALID_ARGS;

	if (handle.context == VMCI_HOST_CONTEXT_ID) {
		struct dbell_entry *entry;
		struct vmci_resource *resource;

		resource = vmci_resource_by_handle(handle,
						   VMCI_RESOURCE_TYPE_DOORBELL);
		if (!resource)
			return VMCI_ERROR_NOT_FOUND;

		entry = container_of(resource, struct dbell_entry, resource);
		*priv_flags = entry->priv_flags;
		vmci_resource_put(resource);
	} else if (handle.context == VMCI_HYPERVISOR_CONTEXT_ID) {
		/*
		 * Hypervisor endpoints for notifications are not
		 * supported (yet).
		 */
		return VMCI_ERROR_INVALID_ARGS;
	} else {
		*priv_flags = vmci_context_get_priv_flags(handle.context);
	}

	return VMCI_SUCCESS;
}

/*
 * Find doorbell entry by bitmap index.
 */
static struct dbell_entry *dbell_index_table_find(u32 idx)
{
	u32 bucket = VMCI_DOORBELL_HASH(idx);
	struct dbell_entry *dbell;

	hlist_for_each_entry(dbell, &vmci_doorbell_it.entries[bucket],
			     node) {
		if (idx == dbell->idx)
			return dbell;
	}

	return NULL;
}

/*
 * Add the given entry to the index table.  This willi take a reference to the
 * entry's resource so that the entry is not deleted before it is removed from
 * the * table.
 */
static void dbell_index_table_add(struct dbell_entry *entry)
{
	u32 bucket;
	u32 new_notify_idx;

	vmci_resource_get(&entry->resource);

	spin_lock_bh(&vmci_doorbell_it.lock);

	/*
	 * Below we try to allocate an index in the notification
	 * bitmap with "not too much" sharing between resources. If we
	 * use less that the full bitmap, we either add to the end if
	 * there are no unused flags within the currently used area,
	 * or we search for unused ones. If we use the full bitmap, we
	 * allocate the index round robin.
	 */
	if (max_notify_idx < PAGE_SIZE || notify_idx_count < PAGE_SIZE) {
		if (last_notify_idx_released < max_notify_idx &&
		    !dbell_index_table_find(last_notify_idx_released)) {
			new_notify_idx = last_notify_idx_released;
			last_notify_idx_released = PAGE_SIZE;
		} else {
			bool reused = false;
			new_notify_idx = last_notify_idx_reserved;
			if (notify_idx_count + 1 < max_notify_idx) {
				do {
					if (!dbell_index_table_find
					    (new_notify_idx)) {
						reused = true;
						break;
					}
					new_notify_idx = (new_notify_idx + 1) %
					    max_notify_idx;
				} while (new_notify_idx !=
					 last_notify_idx_released);
			}
			if (!reused) {
				new_notify_idx = max_notify_idx;
				max_notify_idx++;
			}
		}
	} else {
		new_notify_idx = (last_notify_idx_reserved + 1) % PAGE_SIZE;
	}

	last_notify_idx_reserved = new_notify_idx;
	notify_idx_count++;

	entry->idx = new_notify_idx;
	bucket = VMCI_DOORBELL_HASH(entry->idx);
	hlist_add_head(&entry->node, &vmci_doorbell_it.entries[bucket]);

	spin_unlock_bh(&vmci_doorbell_it.lock);
}

/*
 * Remove the given entry from the index table.  This will release() the
 * entry's resource.
 */
static void dbell_index_table_remove(struct dbell_entry *entry)
{
	spin_lock_bh(&vmci_doorbell_it.lock);

	hlist_del_init(&entry->node);

	notify_idx_count--;
	if (entry->idx == max_notify_idx - 1) {
		/*
		 * If we delete an entry with the maximum known
		 * notification index, we take the opportunity to
		 * prune the current max. As there might be other
		 * unused indices immediately below, we lower the
		 * maximum until we hit an index in use.
		 */
		while (max_notify_idx > 0 &&
		       !dbell_index_table_find(max_notify_idx - 1))
			max_notify_idx--;
	}

	last_notify_idx_released = entry->idx;

	spin_unlock_bh(&vmci_doorbell_it.lock);

	vmci_resource_put(&entry->resource);
}

/*
 * Creates a link between the given doorbell handle and the given
 * index in the bitmap in the device backend. A notification state
 * is created in hypervisor.
 */
static int dbell_link(struct vmci_handle handle, u32 notify_idx)
{
	struct vmci_doorbell_link_msg link_msg;

	link_msg.hdr.dst = vmci_make_handle(VMCI_HYPERVISOR_CONTEXT_ID,
					    VMCI_DOORBELL_LINK);
	link_msg.hdr.src = VMCI_ANON_SRC_HANDLE;
	link_msg.hdr.payload_size = sizeof(link_msg) - VMCI_DG_HEADERSIZE;
	link_msg.handle = handle;
	link_msg.notify_idx = notify_idx;

	return vmci_send_datagram(&link_msg.hdr);
}

/*
 * Unlinks the given doorbell handle from an index in the bitmap in
 * the device backend. The notification state is destroyed in hypervisor.
 */
static int dbell_unlink(struct vmci_handle handle)
{
	struct vmci_doorbell_unlink_msg unlink_msg;

	unlink_msg.hdr.dst = vmci_make_handle(VMCI_HYPERVISOR_CONTEXT_ID,
					      VMCI_DOORBELL_UNLINK);
	unlink_msg.hdr.src = VMCI_ANON_SRC_HANDLE;
	unlink_msg.hdr.payload_size = sizeof(unlink_msg) - VMCI_DG_HEADERSIZE;
	unlink_msg.handle = handle;

	return vmci_send_datagram(&unlink_msg.hdr);
}

/*
 * Notify another guest or the host.  We send a datagram down to the
 * host via the hypervisor with the notification info.
 */
static int dbell_notify_as_guest(struct vmci_handle handle, u32 priv_flags)
{
	struct vmci_doorbell_notify_msg notify_msg;

	notify_msg.hdr.dst = vmci_make_handle(VMCI_HYPERVISOR_CONTEXT_ID,
					      VMCI_DOORBELL_NOTIFY);
	notify_msg.hdr.src = VMCI_ANON_SRC_HANDLE;
	notify_msg.hdr.payload_size = sizeof(notify_msg) - VMCI_DG_HEADERSIZE;
	notify_msg.handle = handle;

	return vmci_send_datagram(&notify_msg.hdr);
}

/*
 * Calls the specified callback in a delayed context.
 */
static void dbell_delayed_dispatch(struct work_struct *work)
{
	struct dbell_entry *entry = container_of(work,
						 struct dbell_entry, work);

	entry->notify_cb(entry->client_data);
	vmci_resource_put(&entry->resource);
}

/*
 * Dispatches a doorbell notification to the host context.
 */
int vmci_dbell_host_context_notify(u32 src_cid, struct vmci_handle handle)
{
	struct dbell_entry *entry;
	struct vmci_resource *resource;

	if (vmci_handle_is_invalid(handle)) {
		pr_devel("Notifying an invalid doorbell (handle=0x%x:0x%x)\n",
			 handle.context, handle.resource);
		return VMCI_ERROR_INVALID_ARGS;
	}

	resource = vmci_resource_by_handle(handle,
					   VMCI_RESOURCE_TYPE_DOORBELL);
	if (!resource) {
		pr_devel("Notifying an unknown doorbell (handle=0x%x:0x%x)\n",
			 handle.context, handle.resource);
		return VMCI_ERROR_NOT_FOUND;
	}

	entry = container_of(resource, struct dbell_entry, resource);
	if (entry->run_delayed) {
		schedule_work(&entry->work);
	} else {
		entry->notify_cb(entry->client_data);
		vmci_resource_put(resource);
	}

	return VMCI_SUCCESS;
}

/*
 * Register the notification bitmap with the host.
 */
bool vmci_dbell_register_notification_bitmap(u32 bitmap_ppn)
{
	int result;
	struct vmci_notify_bm_set_msg bitmap_set_msg;

	bitmap_set_msg.hdr.dst = vmci_make_handle(VMCI_HYPERVISOR_CONTEXT_ID,
						  VMCI_SET_NOTIFY_BITMAP);
	bitmap_set_msg.hdr.src = VMCI_ANON_SRC_HANDLE;
	bitmap_set_msg.hdr.payload_size = sizeof(bitmap_set_msg) -
	    VMCI_DG_HEADERSIZE;
	bitmap_set_msg.bitmap_ppn = bitmap_ppn;

	result = vmci_send_datagram(&bitmap_set_msg.hdr);
	if (result != VMCI_SUCCESS) {
		pr_devel("Failed to register (PPN=%u) as notification bitmap (error=%d)\n",
			 bitmap_ppn, result);
		return false;
	}
	return true;
}

/*
 * Executes or schedules the handlers for a given notify index.
 */
static void dbell_fire_entries(u32 notify_idx)
{
	u32 bucket = VMCI_DOORBELL_HASH(notify_idx);
	struct dbell_entry *dbell;

	spin_lock_bh(&vmci_doorbell_it.lock);

	hlist_for_each_entry(dbell, &vmci_doorbell_it.entries[bucket], node) {
		if (dbell->idx == notify_idx &&
		    atomic_read(&dbell->active) == 1) {
			if (dbell->run_delayed) {
				vmci_resource_get(&dbell->resource);
				schedule_work(&dbell->work);
			} else {
				dbell->notify_cb(dbell->client_data);
			}
		}
	}

	spin_unlock_bh(&vmci_doorbell_it.lock);
}

/*
 * Scans the notification bitmap, collects pending notifications,
 * resets the bitmap and invokes appropriate callbacks.
 */
void vmci_dbell_scan_notification_entries(u8 *bitmap)
{
	u32 idx;

	for (idx = 0; idx < max_notify_idx; idx++) {
		if (bitmap[idx] & 0x1) {
			bitmap[idx] &= ~1;
			dbell_fire_entries(idx);
		}
	}
}

/*
 * vmci_doorbell_create() - Creates a doorbell
 * @handle:     A handle used to track the resource.  Can be invalid.
 * @flags:      Flag that determines context of callback.
 * @priv_flags: Privileges flags.
 * @notify_cb:  The callback to be ivoked when the doorbell fires.
 * @client_data:        A parameter to be passed to the callback.
 *
 * Creates a doorbell with the given callback. If the handle is
 * VMCI_INVALID_HANDLE, a free handle will be assigned, if
 * possible. The callback can be run immediately (potentially with
 * locks held - the default) or delayed (in a kernel thread) by
 * specifying the flag VMCI_FLAG_DELAYED_CB. If delayed execution
 * is selected, a given callback may not be run if the kernel is
 * unable to allocate memory for the delayed execution (highly
 * unlikely).
 */
int vmci_doorbell_create(struct vmci_handle *handle,
			 u32 flags,
			 u32 priv_flags,
			 vmci_callback notify_cb, void *client_data)
{
	struct dbell_entry *entry;
	struct vmci_handle new_handle;
	int result;

	if (!handle || !notify_cb || flags & ~VMCI_FLAG_DELAYED_CB ||
	    priv_flags & ~VMCI_PRIVILEGE_ALL_FLAGS)
		return VMCI_ERROR_INVALID_ARGS;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (entry == NULL) {
		pr_warn("Failed allocating memory for datagram entry\n");
		return VMCI_ERROR_NO_MEM;
	}

	if (vmci_handle_is_invalid(*handle)) {
		u32 context_id = vmci_get_context_id();

		if (context_id == VMCI_INVALID_ID) {
			pr_warn("Failed to get context ID\n");
			result = VMCI_ERROR_NO_RESOURCES;
			goto free_mem;
		}

		/* Let resource code allocate a free ID for us */
		new_handle = vmci_make_handle(context_id, VMCI_INVALID_ID);
	} else {
		bool valid_context = false;

		/*
		 * Validate the handle.  We must do both of the checks below
		 * because we can be acting as both a host and a guest at the
		 * same time. We always allow the host context ID, since the
		 * host functionality is in practice always there with the
		 * unified driver.
		 */
		if (handle->context == VMCI_HOST_CONTEXT_ID ||
		    (vmci_guest_code_active() &&
		     vmci_get_context_id() == handle->context)) {
			valid_context = true;
		}

		if (!valid_context || handle->resource == VMCI_INVALID_ID) {
			pr_devel("Invalid argument (handle=0x%x:0x%x)\n",
				 handle->context, handle->resource);
			result = VMCI_ERROR_INVALID_ARGS;
			goto free_mem;
		}

		new_handle = *handle;
	}

	entry->idx = 0;
	INIT_HLIST_NODE(&entry->node);
	entry->priv_flags = priv_flags;
	INIT_WORK(&entry->work, dbell_delayed_dispatch);
	entry->run_delayed = flags & VMCI_FLAG_DELAYED_CB;
	entry->notify_cb = notify_cb;
	entry->client_data = client_data;
	atomic_set(&entry->active, 0);

	result = vmci_resource_add(&entry->resource,
				   VMCI_RESOURCE_TYPE_DOORBELL,
				   new_handle);
	if (result != VMCI_SUCCESS) {
		pr_warn("Failed to add new resource (handle=0x%x:0x%x), error: %d\n",
			new_handle.context, new_handle.resource, result);
		goto free_mem;
	}

	new_handle = vmci_resource_handle(&entry->resource);
	if (vmci_guest_code_active()) {
		dbell_index_table_add(entry);
		result = dbell_link(new_handle, entry->idx);
		if (VMCI_SUCCESS != result)
			goto destroy_resource;

		atomic_set(&entry->active, 1);
	}

	*handle = new_handle;

	return result;

 destroy_resource:
	dbell_index_table_remove(entry);
	vmci_resource_remove(&entry->resource);
 free_mem:
	kfree(entry);
	return result;
}
EXPORT_SYMBOL_GPL(vmci_doorbell_create);

/*
 * vmci_doorbell_destroy() - Destroy a doorbell.
 * @handle:     The handle tracking the resource.
 *
 * Destroys a doorbell previously created with vmcii_doorbell_create. This
 * operation may block waiting for a callback to finish.
 */
int vmci_doorbell_destroy(struct vmci_handle handle)
{
	struct dbell_entry *entry;
	struct vmci_resource *resource;

	if (vmci_handle_is_invalid(handle))
		return VMCI_ERROR_INVALID_ARGS;

	resource = vmci_resource_by_handle(handle,
					   VMCI_RESOURCE_TYPE_DOORBELL);
	if (!resource) {
		pr_devel("Failed to destroy doorbell (handle=0x%x:0x%x)\n",
			 handle.context, handle.resource);
		return VMCI_ERROR_NOT_FOUND;
	}

	entry = container_of(resource, struct dbell_entry, resource);

	if (!hlist_unhashed(&entry->node)) {
		int result;

		dbell_index_table_remove(entry);

		result = dbell_unlink(handle);
		if (VMCI_SUCCESS != result) {

			/*
			 * The only reason this should fail would be
			 * an inconsistency between guest and
			 * hypervisor state, where the guest believes
			 * it has an active registration whereas the
			 * hypervisor doesn't. One case where this may
			 * happen is if a doorbell is unregistered
			 * following a hibernation at a time where the
			 * doorbell state hasn't been restored on the
			 * hypervisor side yet. Since the handle has
			 * now been removed in the guest, we just
			 * print a warning and return success.
			 */
			pr_devel("Unlink of doorbell (handle=0x%x:0x%x) unknown by hypervisor (error=%d)\n",
				 handle.context, handle.resource, result);
		}
	}

	/*
	 * Now remove the resource from the table.  It might still be in use
	 * after this, in a callback or still on the delayed work queue.
	 */
	vmci_resource_put(&entry->resource);
	vmci_resource_remove(&entry->resource);

	kfree(entry);

	return VMCI_SUCCESS;
}
EXPORT_SYMBOL_GPL(vmci_doorbell_destroy);

/*
 * vmci_doorbell_notify() - Ring the doorbell (and hide in the bushes).
 * @dst:        The handlle identifying the doorbell resource
 * @priv_flags: Priviledge flags.
 *
 * Generates a notification on the doorbell identified by the
 * handle. For host side generation of notifications, the caller
 * can specify what the privilege of the calling side is.
 */
int vmci_doorbell_notify(struct vmci_handle dst, u32 priv_flags)
{
	int retval;
	enum vmci_route route;
	struct vmci_handle src;

	if (vmci_handle_is_invalid(dst) ||
	    (priv_flags & ~VMCI_PRIVILEGE_ALL_FLAGS))
		return VMCI_ERROR_INVALID_ARGS;

	src = VMCI_INVALID_HANDLE;
	retval = vmci_route(&src, &dst, false, &route);
	if (retval < VMCI_SUCCESS)
		return retval;

	if (VMCI_ROUTE_AS_HOST == route)
		return vmci_ctx_notify_dbell(VMCI_HOST_CONTEXT_ID,
					     dst, priv_flags);

	if (VMCI_ROUTE_AS_GUEST == route)
		return dbell_notify_as_guest(dst, priv_flags);

	pr_warn("Unknown route (%d) for doorbell\n", route);
	return VMCI_ERROR_DST_UNREACHABLE;
}
EXPORT_SYMBOL_GPL(vmci_doorbell_notify);
