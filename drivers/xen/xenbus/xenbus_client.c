/******************************************************************************
 * Client-facing interface for the Xenbus driver.  In other words, the
 * interface between the Xenbus and the device-specific code, be it the
 * frontend or the backend of that driver.
 *
 * Copyright (C) 2005 XenSource Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/export.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/page.h>
#include <xen/interface/xen.h>
#include <xen/interface/event_channel.h>
#include <xen/balloon.h>
#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/xenbus.h>
#include <xen/xen.h>

#include "xenbus_probe.h"

struct xenbus_map_node {
	struct list_head next;
	union {
		struct vm_struct *area; /* PV */
		struct page *page;     /* HVM */
	};
	grant_handle_t handle;
};

static DEFINE_SPINLOCK(xenbus_valloc_lock);
static LIST_HEAD(xenbus_valloc_pages);

struct xenbus_ring_ops {
	int (*map)(struct xenbus_device *dev, int gnt, void **vaddr);
	int (*unmap)(struct xenbus_device *dev, void *vaddr);
};

static const struct xenbus_ring_ops *ring_ops __read_mostly;

const char *xenbus_strstate(enum xenbus_state state)
{
	static const char *const name[] = {
		[ XenbusStateUnknown      ] = "Unknown",
		[ XenbusStateInitialising ] = "Initialising",
		[ XenbusStateInitWait     ] = "InitWait",
		[ XenbusStateInitialised  ] = "Initialised",
		[ XenbusStateConnected    ] = "Connected",
		[ XenbusStateClosing      ] = "Closing",
		[ XenbusStateClosed	  ] = "Closed",
		[XenbusStateReconfiguring] = "Reconfiguring",
		[XenbusStateReconfigured] = "Reconfigured",
	};
	return (state < ARRAY_SIZE(name)) ? name[state] : "INVALID";
}
EXPORT_SYMBOL_GPL(xenbus_strstate);

/**
 * xenbus_watch_path - register a watch
 * @dev: xenbus device
 * @path: path to watch
 * @watch: watch to register
 * @callback: callback to register
 *
 * Register a @watch on the given path, using the given xenbus_watch structure
 * for storage, and the given @callback function as the callback.  Return 0 on
 * success, or -errno on error.  On success, the given @path will be saved as
 * @watch->node, and remains the caller's to free.  On error, @watch->node will
 * be NULL, the device will switch to %XenbusStateClosing, and the error will
 * be saved in the store.
 */
int xenbus_watch_path(struct xenbus_device *dev, const char *path,
		      struct xenbus_watch *watch,
		      void (*callback)(struct xenbus_watch *,
				       const char **, unsigned int))
{
	int err;

	watch->node = path;
	watch->callback = callback;

	err = register_xenbus_watch(watch);

	if (err) {
		watch->node = NULL;
		watch->callback = NULL;
		xenbus_dev_fatal(dev, err, "adding watch on %s", path);
	}

	return err;
}
EXPORT_SYMBOL_GPL(xenbus_watch_path);


/**
 * xenbus_watch_pathfmt - register a watch on a sprintf-formatted path
 * @dev: xenbus device
 * @watch: watch to register
 * @callback: callback to register
 * @pathfmt: format of path to watch
 *
 * Register a watch on the given @path, using the given xenbus_watch
 * structure for storage, and the given @callback function as the callback.
 * Return 0 on success, or -errno on error.  On success, the watched path
 * (@path/@path2) will be saved as @watch->node, and becomes the caller's to
 * kfree().  On error, watch->node will be NULL, so the caller has nothing to
 * free, the device will switch to %XenbusStateClosing, and the error will be
 * saved in the store.
 */
int xenbus_watch_pathfmt(struct xenbus_device *dev,
			 struct xenbus_watch *watch,
			 void (*callback)(struct xenbus_watch *,
					const char **, unsigned int),
			 const char *pathfmt, ...)
{
	int err;
	va_list ap;
	char *path;

	va_start(ap, pathfmt);
	path = kvasprintf(GFP_NOIO | __GFP_HIGH, pathfmt, ap);
	va_end(ap);

	if (!path) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating path for watch");
		return -ENOMEM;
	}
	err = xenbus_watch_path(dev, path, watch, callback);

	if (err)
		kfree(path);
	return err;
}
EXPORT_SYMBOL_GPL(xenbus_watch_pathfmt);

static void xenbus_switch_fatal(struct xenbus_device *, int, int,
				const char *, ...);

static int
__xenbus_switch_state(struct xenbus_device *dev,
		      enum xenbus_state state, int depth)
{
	/* We check whether the state is currently set to the given value, and
	   if not, then the state is set.  We don't want to unconditionally
	   write the given state, because we don't want to fire watches
	   unnecessarily.  Furthermore, if the node has gone, we don't write
	   to it, as the device will be tearing down, and we don't want to
	   resurrect that directory.

	   Note that, because of this cached value of our state, this
	   function will not take a caller's Xenstore transaction
	   (something it was trying to in the past) because dev->state
	   would not get reset if the transaction was aborted.
	 */

	struct xenbus_transaction xbt;
	int current_state;
	int err, abort;

	if (state == dev->state)
		return 0;

again:
	abort = 1;

	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_switch_fatal(dev, depth, err, "starting transaction");
		return 0;
	}

	err = xenbus_scanf(xbt, dev->nodename, "state", "%d", &current_state);
	if (err != 1)
		goto abort;

	err = xenbus_printf(xbt, dev->nodename, "state", "%d", state);
	if (err) {
		xenbus_switch_fatal(dev, depth, err, "writing new state");
		goto abort;
	}

	abort = 0;
abort:
	err = xenbus_transaction_end(xbt, abort);
	if (err) {
		if (err == -EAGAIN && !abort)
			goto again;
		xenbus_switch_fatal(dev, depth, err, "ending transaction");
	} else
		dev->state = state;

	return 0;
}

/**
 * xenbus_switch_state
 * @dev: xenbus device
 * @state: new state
 *
 * Advertise in the store a change of the given driver to the given new_state.
 * Return 0 on success, or -errno on error.  On error, the device will switch
 * to XenbusStateClosing, and the error will be saved in the store.
 */
int xenbus_switch_state(struct xenbus_device *dev, enum xenbus_state state)
{
	return __xenbus_switch_state(dev, state, 0);
}

EXPORT_SYMBOL_GPL(xenbus_switch_state);

int xenbus_frontend_closed(struct xenbus_device *dev)
{
	xenbus_switch_state(dev, XenbusStateClosed);
	complete(&dev->down);
	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_frontend_closed);

/**
 * Return the path to the error node for the given device, or NULL on failure.
 * If the value returned is non-NULL, then it is the caller's to kfree.
 */
static char *error_path(struct xenbus_device *dev)
{
	return kasprintf(GFP_KERNEL, "error/%s", dev->nodename);
}


static void xenbus_va_dev_error(struct xenbus_device *dev, int err,
				const char *fmt, va_list ap)
{
	int ret;
	unsigned int len;
	char *printf_buffer = NULL;
	char *path_buffer = NULL;

#define PRINTF_BUFFER_SIZE 4096
	printf_buffer = kmalloc(PRINTF_BUFFER_SIZE, GFP_KERNEL);
	if (printf_buffer == NULL)
		goto fail;

	len = sprintf(printf_buffer, "%i ", -err);
	ret = vsnprintf(printf_buffer+len, PRINTF_BUFFER_SIZE-len, fmt, ap);

	BUG_ON(len + ret > PRINTF_BUFFER_SIZE-1);

	dev_err(&dev->dev, "%s\n", printf_buffer);

	path_buffer = error_path(dev);

	if (path_buffer == NULL) {
		dev_err(&dev->dev, "failed to write error node for %s (%s)\n",
		       dev->nodename, printf_buffer);
		goto fail;
	}

	if (xenbus_write(XBT_NIL, path_buffer, "error", printf_buffer) != 0) {
		dev_err(&dev->dev, "failed to write error node for %s (%s)\n",
		       dev->nodename, printf_buffer);
		goto fail;
	}

fail:
	kfree(printf_buffer);
	kfree(path_buffer);
}


/**
 * xenbus_dev_error
 * @dev: xenbus device
 * @err: error to report
 * @fmt: error message format
 *
 * Report the given negative errno into the store, along with the given
 * formatted message.
 */
void xenbus_dev_error(struct xenbus_device *dev, int err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	xenbus_va_dev_error(dev, err, fmt, ap);
	va_end(ap);
}
EXPORT_SYMBOL_GPL(xenbus_dev_error);

/**
 * xenbus_dev_fatal
 * @dev: xenbus device
 * @err: error to report
 * @fmt: error message format
 *
 * Equivalent to xenbus_dev_error(dev, err, fmt, args), followed by
 * xenbus_switch_state(dev, XenbusStateClosing) to schedule an orderly
 * closedown of this driver and its peer.
 */

void xenbus_dev_fatal(struct xenbus_device *dev, int err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	xenbus_va_dev_error(dev, err, fmt, ap);
	va_end(ap);

	xenbus_switch_state(dev, XenbusStateClosing);
}
EXPORT_SYMBOL_GPL(xenbus_dev_fatal);

/**
 * Equivalent to xenbus_dev_fatal(dev, err, fmt, args), but helps
 * avoiding recursion within xenbus_switch_state.
 */
static void xenbus_switch_fatal(struct xenbus_device *dev, int depth, int err,
				const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	xenbus_va_dev_error(dev, err, fmt, ap);
	va_end(ap);

	if (!depth)
		__xenbus_switch_state(dev, XenbusStateClosing, 1);
}

/**
 * xenbus_grant_ring
 * @dev: xenbus device
 * @ring_mfn: mfn of ring to grant

 * Grant access to the given @ring_mfn to the peer of the given device.  Return
 * 0 on success, or -errno on error.  On error, the device will switch to
 * XenbusStateClosing, and the error will be saved in the store.
 */
int xenbus_grant_ring(struct xenbus_device *dev, unsigned long ring_mfn)
{
	int err = gnttab_grant_foreign_access(dev->otherend_id, ring_mfn, 0);
	if (err < 0)
		xenbus_dev_fatal(dev, err, "granting access to ring page");
	return err;
}
EXPORT_SYMBOL_GPL(xenbus_grant_ring);


/**
 * Allocate an event channel for the given xenbus_device, assigning the newly
 * created local port to *port.  Return 0 on success, or -errno on error.  On
 * error, the device will switch to XenbusStateClosing, and the error will be
 * saved in the store.
 */
int xenbus_alloc_evtchn(struct xenbus_device *dev, int *port)
{
	struct evtchn_alloc_unbound alloc_unbound;
	int err;

	alloc_unbound.dom = DOMID_SELF;
	alloc_unbound.remote_dom = dev->otherend_id;

	err = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound,
					  &alloc_unbound);
	if (err)
		xenbus_dev_fatal(dev, err, "allocating event channel");
	else
		*port = alloc_unbound.port;

	return err;
}
EXPORT_SYMBOL_GPL(xenbus_alloc_evtchn);


/**
 * Bind to an existing interdomain event channel in another domain. Returns 0
 * on success and stores the local port in *port. On error, returns -errno,
 * switches the device to XenbusStateClosing, and saves the error in XenStore.
 */
int xenbus_bind_evtchn(struct xenbus_device *dev, int remote_port, int *port)
{
	struct evtchn_bind_interdomain bind_interdomain;
	int err;

	bind_interdomain.remote_dom = dev->otherend_id;
	bind_interdomain.remote_port = remote_port;

	err = HYPERVISOR_event_channel_op(EVTCHNOP_bind_interdomain,
					  &bind_interdomain);
	if (err)
		xenbus_dev_fatal(dev, err,
				 "binding to event channel %d from domain %d",
				 remote_port, dev->otherend_id);
	else
		*port = bind_interdomain.local_port;

	return err;
}
EXPORT_SYMBOL_GPL(xenbus_bind_evtchn);


/**
 * Free an existing event channel. Returns 0 on success or -errno on error.
 */
int xenbus_free_evtchn(struct xenbus_device *dev, int port)
{
	struct evtchn_close close;
	int err;

	close.port = port;

	err = HYPERVISOR_event_channel_op(EVTCHNOP_close, &close);
	if (err)
		xenbus_dev_error(dev, err, "freeing event channel %d", port);

	return err;
}
EXPORT_SYMBOL_GPL(xenbus_free_evtchn);


/**
 * xenbus_map_ring_valloc
 * @dev: xenbus device
 * @gnt_ref: grant reference
 * @vaddr: pointer to address to be filled out by mapping
 *
 * Based on Rusty Russell's skeleton driver's map_page.
 * Map a page of memory into this domain from another domain's grant table.
 * xenbus_map_ring_valloc allocates a page of virtual address space, maps the
 * page to that address, and sets *vaddr to that address.
 * Returns 0 on success, and GNTST_* (see xen/include/interface/grant_table.h)
 * or -ENOMEM on error. If an error is returned, device will switch to
 * XenbusStateClosing and the error message will be saved in XenStore.
 */
int xenbus_map_ring_valloc(struct xenbus_device *dev, int gnt_ref, void **vaddr)
{
	return ring_ops->map(dev, gnt_ref, vaddr);
}
EXPORT_SYMBOL_GPL(xenbus_map_ring_valloc);

static int xenbus_map_ring_valloc_pv(struct xenbus_device *dev,
				     int gnt_ref, void **vaddr)
{
	struct gnttab_map_grant_ref op = {
		.flags = GNTMAP_host_map | GNTMAP_contains_pte,
		.ref   = gnt_ref,
		.dom   = dev->otherend_id,
	};
	struct xenbus_map_node *node;
	struct vm_struct *area;
	pte_t *pte;

	*vaddr = NULL;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	area = alloc_vm_area(PAGE_SIZE, &pte);
	if (!area) {
		kfree(node);
		return -ENOMEM;
	}

	op.host_addr = arbitrary_virt_to_machine(pte).maddr;

	gnttab_batch_map(&op, 1);

	if (op.status != GNTST_okay) {
		free_vm_area(area);
		kfree(node);
		xenbus_dev_fatal(dev, op.status,
				 "mapping in shared page %d from domain %d",
				 gnt_ref, dev->otherend_id);
		return op.status;
	}

	node->handle = op.handle;
	node->area = area;

	spin_lock(&xenbus_valloc_lock);
	list_add(&node->next, &xenbus_valloc_pages);
	spin_unlock(&xenbus_valloc_lock);

	*vaddr = area->addr;
	return 0;
}

static int xenbus_map_ring_valloc_hvm(struct xenbus_device *dev,
				      int gnt_ref, void **vaddr)
{
	struct xenbus_map_node *node;
	int err;
	void *addr;

	*vaddr = NULL;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	err = alloc_xenballooned_pages(1, &node->page, false /* lowmem */);
	if (err)
		goto out_err;

	addr = pfn_to_kaddr(page_to_pfn(node->page));

	err = xenbus_map_ring(dev, gnt_ref, &node->handle, addr);
	if (err)
		goto out_err_free_ballooned_pages;

	spin_lock(&xenbus_valloc_lock);
	list_add(&node->next, &xenbus_valloc_pages);
	spin_unlock(&xenbus_valloc_lock);

	*vaddr = addr;
	return 0;

 out_err_free_ballooned_pages:
	free_xenballooned_pages(1, &node->page);
 out_err:
	kfree(node);
	return err;
}


/**
 * xenbus_map_ring
 * @dev: xenbus device
 * @gnt_ref: grant reference
 * @handle: pointer to grant handle to be filled
 * @vaddr: address to be mapped to
 *
 * Map a page of memory into this domain from another domain's grant table.
 * xenbus_map_ring does not allocate the virtual address space (you must do
 * this yourself!). It only maps in the page to the specified address.
 * Returns 0 on success, and GNTST_* (see xen/include/interface/grant_table.h)
 * or -ENOMEM on error. If an error is returned, device will switch to
 * XenbusStateClosing and the error message will be saved in XenStore.
 */
int xenbus_map_ring(struct xenbus_device *dev, int gnt_ref,
		    grant_handle_t *handle, void *vaddr)
{
	struct gnttab_map_grant_ref op;

	gnttab_set_map_op(&op, (unsigned long)vaddr, GNTMAP_host_map, gnt_ref,
			  dev->otherend_id);

	gnttab_batch_map(&op, 1);

	if (op.status != GNTST_okay) {
		xenbus_dev_fatal(dev, op.status,
				 "mapping in shared page %d from domain %d",
				 gnt_ref, dev->otherend_id);
	} else
		*handle = op.handle;

	return op.status;
}
EXPORT_SYMBOL_GPL(xenbus_map_ring);


/**
 * xenbus_unmap_ring_vfree
 * @dev: xenbus device
 * @vaddr: addr to unmap
 *
 * Based on Rusty Russell's skeleton driver's unmap_page.
 * Unmap a page of memory in this domain that was imported from another domain.
 * Use xenbus_unmap_ring_vfree if you mapped in your memory with
 * xenbus_map_ring_valloc (it will free the virtual address space).
 * Returns 0 on success and returns GNTST_* on error
 * (see xen/include/interface/grant_table.h).
 */
int xenbus_unmap_ring_vfree(struct xenbus_device *dev, void *vaddr)
{
	return ring_ops->unmap(dev, vaddr);
}
EXPORT_SYMBOL_GPL(xenbus_unmap_ring_vfree);

static int xenbus_unmap_ring_vfree_pv(struct xenbus_device *dev, void *vaddr)
{
	struct xenbus_map_node *node;
	struct gnttab_unmap_grant_ref op = {
		.host_addr = (unsigned long)vaddr,
	};
	unsigned int level;

	spin_lock(&xenbus_valloc_lock);
	list_for_each_entry(node, &xenbus_valloc_pages, next) {
		if (node->area->addr == vaddr) {
			list_del(&node->next);
			goto found;
		}
	}
	node = NULL;
 found:
	spin_unlock(&xenbus_valloc_lock);

	if (!node) {
		xenbus_dev_error(dev, -ENOENT,
				 "can't find mapped virtual address %p", vaddr);
		return GNTST_bad_virt_addr;
	}

	op.handle = node->handle;
	op.host_addr = arbitrary_virt_to_machine(
		lookup_address((unsigned long)vaddr, &level)).maddr;

	if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &op, 1))
		BUG();

	if (op.status == GNTST_okay)
		free_vm_area(node->area);
	else
		xenbus_dev_error(dev, op.status,
				 "unmapping page at handle %d error %d",
				 node->handle, op.status);

	kfree(node);
	return op.status;
}

static int xenbus_unmap_ring_vfree_hvm(struct xenbus_device *dev, void *vaddr)
{
	int rv;
	struct xenbus_map_node *node;
	void *addr;

	spin_lock(&xenbus_valloc_lock);
	list_for_each_entry(node, &xenbus_valloc_pages, next) {
		addr = pfn_to_kaddr(page_to_pfn(node->page));
		if (addr == vaddr) {
			list_del(&node->next);
			goto found;
		}
	}
	node = addr = NULL;
 found:
	spin_unlock(&xenbus_valloc_lock);

	if (!node) {
		xenbus_dev_error(dev, -ENOENT,
				 "can't find mapped virtual address %p", vaddr);
		return GNTST_bad_virt_addr;
	}

	rv = xenbus_unmap_ring(dev, node->handle, addr);

	if (!rv)
		free_xenballooned_pages(1, &node->page);
	else
		WARN(1, "Leaking %p\n", vaddr);

	kfree(node);
	return rv;
}

/**
 * xenbus_unmap_ring
 * @dev: xenbus device
 * @handle: grant handle
 * @vaddr: addr to unmap
 *
 * Unmap a page of memory in this domain that was imported from another domain.
 * Returns 0 on success and returns GNTST_* on error
 * (see xen/include/interface/grant_table.h).
 */
int xenbus_unmap_ring(struct xenbus_device *dev,
		      grant_handle_t handle, void *vaddr)
{
	struct gnttab_unmap_grant_ref op;

	gnttab_set_unmap_op(&op, (unsigned long)vaddr, GNTMAP_host_map, handle);

	if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &op, 1))
		BUG();

	if (op.status != GNTST_okay)
		xenbus_dev_error(dev, op.status,
				 "unmapping page at handle %d error %d",
				 handle, op.status);

	return op.status;
}
EXPORT_SYMBOL_GPL(xenbus_unmap_ring);


/**
 * xenbus_read_driver_state
 * @path: path for driver
 *
 * Return the state of the driver rooted at the given store path, or
 * XenbusStateUnknown if no state can be read.
 */
enum xenbus_state xenbus_read_driver_state(const char *path)
{
	enum xenbus_state result;
	int err = xenbus_gather(XBT_NIL, path, "state", "%d", &result, NULL);
	if (err)
		result = XenbusStateUnknown;

	return result;
}
EXPORT_SYMBOL_GPL(xenbus_read_driver_state);

static const struct xenbus_ring_ops ring_ops_pv = {
	.map = xenbus_map_ring_valloc_pv,
	.unmap = xenbus_unmap_ring_vfree_pv,
};

static const struct xenbus_ring_ops ring_ops_hvm = {
	.map = xenbus_map_ring_valloc_hvm,
	.unmap = xenbus_unmap_ring_vfree_hvm,
};

void __init xenbus_ring_ops_init(void)
{
	if (xen_pv_domain())
		ring_ops = &ring_ops_pv;
	else
		ring_ops = &ring_ops_hvm;
}
