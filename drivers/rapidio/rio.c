// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RapidIO interconnect services
 * (RapidIO Interconnect Specification, http://www.rapidio.org)
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright 2009 - 2013 Integrated Device Technology, Inc.
 * Alex Bounine <alexandre.bounine@idt.com>
 */

#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/rio_ids.h>
#include <linux/rio_regs.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include "rio.h"

/*
 * struct rio_pwrite - RIO portwrite event
 * @node:    Node in list of doorbell events
 * @pwcback: Doorbell event callback
 * @context: Handler specific context to pass on event
 */
struct rio_pwrite {
	struct list_head node;

	int (*pwcback)(struct rio_mport *mport, void *context,
		       union rio_pw_msg *msg, int step);
	void *context;
};

MODULE_DESCRIPTION("RapidIO Subsystem Core");
MODULE_AUTHOR("Matt Porter <mporter@kernel.crashing.org>");
MODULE_AUTHOR("Alexandre Bounine <alexandre.bounine@idt.com>");
MODULE_LICENSE("GPL");

static int hdid[RIO_MAX_MPORTS];
static int ids_num;
module_param_array(hdid, int, &ids_num, 0);
MODULE_PARM_DESC(hdid,
	"Destination ID assignment to local RapidIO controllers");

static LIST_HEAD(rio_devices);
static LIST_HEAD(rio_nets);
static DEFINE_SPINLOCK(rio_global_list_lock);

static LIST_HEAD(rio_mports);
static LIST_HEAD(rio_scans);
static DEFINE_MUTEX(rio_mport_list_lock);
static unsigned char next_portid;
static DEFINE_SPINLOCK(rio_mmap_lock);

/**
 * rio_local_get_device_id - Get the base/extended device id for a port
 * @port: RIO master port from which to get the deviceid
 *
 * Reads the base/extended device id from the local device
 * implementing the master port. Returns the 8/16-bit device
 * id.
 */
u16 rio_local_get_device_id(struct rio_mport *port)
{
	u32 result;

	rio_local_read_config_32(port, RIO_DID_CSR, &result);

	return (RIO_GET_DID(port->sys_size, result));
}
EXPORT_SYMBOL_GPL(rio_local_get_device_id);

/**
 * rio_query_mport - Query mport device attributes
 * @port: mport device to query
 * @mport_attr: mport attributes data structure
 *
 * Returns attributes of specified mport through the
 * pointer to attributes data structure.
 */
int rio_query_mport(struct rio_mport *port,
		    struct rio_mport_attr *mport_attr)
{
	if (!port->ops->query_mport)
		return -ENODATA;
	return port->ops->query_mport(port, mport_attr);
}
EXPORT_SYMBOL(rio_query_mport);

/**
 * rio_alloc_net- Allocate and initialize a new RIO network data structure
 * @mport: Master port associated with the RIO network
 *
 * Allocates a RIO network structure, initializes per-network
 * list heads, and adds the associated master port to the
 * network list of associated master ports. Returns a
 * RIO network pointer on success or %NULL on failure.
 */
struct rio_net *rio_alloc_net(struct rio_mport *mport)
{
	struct rio_net *net = kzalloc(sizeof(*net), GFP_KERNEL);

	if (net) {
		INIT_LIST_HEAD(&net->node);
		INIT_LIST_HEAD(&net->devices);
		INIT_LIST_HEAD(&net->switches);
		INIT_LIST_HEAD(&net->mports);
		mport->net = net;
	}
	return net;
}
EXPORT_SYMBOL_GPL(rio_alloc_net);

int rio_add_net(struct rio_net *net)
{
	int err;

	err = device_register(&net->dev);
	if (err)
		return err;
	spin_lock(&rio_global_list_lock);
	list_add_tail(&net->node, &rio_nets);
	spin_unlock(&rio_global_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(rio_add_net);

void rio_free_net(struct rio_net *net)
{
	spin_lock(&rio_global_list_lock);
	if (!list_empty(&net->node))
		list_del(&net->node);
	spin_unlock(&rio_global_list_lock);
	if (net->release)
		net->release(net);
	device_unregister(&net->dev);
}
EXPORT_SYMBOL_GPL(rio_free_net);

/**
 * rio_local_set_device_id - Set the base/extended device id for a port
 * @port: RIO master port
 * @did: Device ID value to be written
 *
 * Writes the base/extended device id from a device.
 */
void rio_local_set_device_id(struct rio_mport *port, u16 did)
{
	rio_local_write_config_32(port, RIO_DID_CSR,
				  RIO_SET_DID(port->sys_size, did));
}
EXPORT_SYMBOL_GPL(rio_local_set_device_id);

/**
 * rio_add_device- Adds a RIO device to the device model
 * @rdev: RIO device
 *
 * Adds the RIO device to the global device list and adds the RIO
 * device to the RIO device list.  Creates the generic sysfs nodes
 * for an RIO device.
 */
int rio_add_device(struct rio_dev *rdev)
{
	int err;

	atomic_set(&rdev->state, RIO_DEVICE_RUNNING);
	err = device_register(&rdev->dev);
	if (err)
		return err;

	spin_lock(&rio_global_list_lock);
	list_add_tail(&rdev->global_list, &rio_devices);
	if (rdev->net) {
		list_add_tail(&rdev->net_list, &rdev->net->devices);
		if (rdev->pef & RIO_PEF_SWITCH)
			list_add_tail(&rdev->rswitch->node,
				      &rdev->net->switches);
	}
	spin_unlock(&rio_global_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(rio_add_device);

/*
 * rio_del_device - removes a RIO device from the device model
 * @rdev: RIO device
 * @state: device state to set during removal process
 *
 * Removes the RIO device to the kernel device list and subsystem's device list.
 * Clears sysfs entries for the removed device.
 */
void rio_del_device(struct rio_dev *rdev, enum rio_device_state state)
{
	pr_debug("RIO: %s: removing %s\n", __func__, rio_name(rdev));
	atomic_set(&rdev->state, state);
	spin_lock(&rio_global_list_lock);
	list_del(&rdev->global_list);
	if (rdev->net) {
		list_del(&rdev->net_list);
		if (rdev->pef & RIO_PEF_SWITCH) {
			list_del(&rdev->rswitch->node);
			kfree(rdev->rswitch->route_table);
		}
	}
	spin_unlock(&rio_global_list_lock);
	device_unregister(&rdev->dev);
}
EXPORT_SYMBOL_GPL(rio_del_device);

/**
 * rio_request_inb_mbox - request inbound mailbox service
 * @mport: RIO master port from which to allocate the mailbox resource
 * @dev_id: Device specific pointer to pass on event
 * @mbox: Mailbox number to claim
 * @entries: Number of entries in inbound mailbox queue
 * @minb: Callback to execute when inbound message is received
 *
 * Requests ownership of an inbound mailbox resource and binds
 * a callback function to the resource. Returns %0 on success.
 */
int rio_request_inb_mbox(struct rio_mport *mport,
			 void *dev_id,
			 int mbox,
			 int entries,
			 void (*minb) (struct rio_mport * mport, void *dev_id, int mbox,
				       int slot))
{
	int rc = -ENOSYS;
	struct resource *res;

	if (!mport->ops->open_inb_mbox)
		goto out;

	res = kzalloc(sizeof(*res), GFP_KERNEL);
	if (res) {
		rio_init_mbox_res(res, mbox, mbox);

		/* Make sure this mailbox isn't in use */
		rc = request_resource(&mport->riores[RIO_INB_MBOX_RESOURCE],
				      res);
		if (rc < 0) {
			kfree(res);
			goto out;
		}

		mport->inb_msg[mbox].res = res;

		/* Hook the inbound message callback */
		mport->inb_msg[mbox].mcback = minb;

		rc = mport->ops->open_inb_mbox(mport, dev_id, mbox, entries);
		if (rc) {
			mport->inb_msg[mbox].mcback = NULL;
			mport->inb_msg[mbox].res = NULL;
			release_resource(res);
			kfree(res);
		}
	} else
		rc = -ENOMEM;

      out:
	return rc;
}
EXPORT_SYMBOL_GPL(rio_request_inb_mbox);

/**
 * rio_release_inb_mbox - release inbound mailbox message service
 * @mport: RIO master port from which to release the mailbox resource
 * @mbox: Mailbox number to release
 *
 * Releases ownership of an inbound mailbox resource. Returns 0
 * if the request has been satisfied.
 */
int rio_release_inb_mbox(struct rio_mport *mport, int mbox)
{
	int rc;

	if (!mport->ops->close_inb_mbox || !mport->inb_msg[mbox].res)
		return -EINVAL;

	mport->ops->close_inb_mbox(mport, mbox);
	mport->inb_msg[mbox].mcback = NULL;

	rc = release_resource(mport->inb_msg[mbox].res);
	if (rc)
		return rc;

	kfree(mport->inb_msg[mbox].res);
	mport->inb_msg[mbox].res = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(rio_release_inb_mbox);

/**
 * rio_request_outb_mbox - request outbound mailbox service
 * @mport: RIO master port from which to allocate the mailbox resource
 * @dev_id: Device specific pointer to pass on event
 * @mbox: Mailbox number to claim
 * @entries: Number of entries in outbound mailbox queue
 * @moutb: Callback to execute when outbound message is sent
 *
 * Requests ownership of an outbound mailbox resource and binds
 * a callback function to the resource. Returns 0 on success.
 */
int rio_request_outb_mbox(struct rio_mport *mport,
			  void *dev_id,
			  int mbox,
			  int entries,
			  void (*moutb) (struct rio_mport * mport, void *dev_id, int mbox, int slot))
{
	int rc = -ENOSYS;
	struct resource *res;

	if (!mport->ops->open_outb_mbox)
		goto out;

	res = kzalloc(sizeof(*res), GFP_KERNEL);
	if (res) {
		rio_init_mbox_res(res, mbox, mbox);

		/* Make sure this outbound mailbox isn't in use */
		rc = request_resource(&mport->riores[RIO_OUTB_MBOX_RESOURCE],
				      res);
		if (rc < 0) {
			kfree(res);
			goto out;
		}

		mport->outb_msg[mbox].res = res;

		/* Hook the inbound message callback */
		mport->outb_msg[mbox].mcback = moutb;

		rc = mport->ops->open_outb_mbox(mport, dev_id, mbox, entries);
		if (rc) {
			mport->outb_msg[mbox].mcback = NULL;
			mport->outb_msg[mbox].res = NULL;
			release_resource(res);
			kfree(res);
		}
	} else
		rc = -ENOMEM;

      out:
	return rc;
}
EXPORT_SYMBOL_GPL(rio_request_outb_mbox);

/**
 * rio_release_outb_mbox - release outbound mailbox message service
 * @mport: RIO master port from which to release the mailbox resource
 * @mbox: Mailbox number to release
 *
 * Releases ownership of an inbound mailbox resource. Returns 0
 * if the request has been satisfied.
 */
int rio_release_outb_mbox(struct rio_mport *mport, int mbox)
{
	int rc;

	if (!mport->ops->close_outb_mbox || !mport->outb_msg[mbox].res)
		return -EINVAL;

	mport->ops->close_outb_mbox(mport, mbox);
	mport->outb_msg[mbox].mcback = NULL;

	rc = release_resource(mport->outb_msg[mbox].res);
	if (rc)
		return rc;

	kfree(mport->outb_msg[mbox].res);
	mport->outb_msg[mbox].res = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(rio_release_outb_mbox);

/**
 * rio_setup_inb_dbell - bind inbound doorbell callback
 * @mport: RIO master port to bind the doorbell callback
 * @dev_id: Device specific pointer to pass on event
 * @res: Doorbell message resource
 * @dinb: Callback to execute when doorbell is received
 *
 * Adds a doorbell resource/callback pair into a port's
 * doorbell event list. Returns 0 if the request has been
 * satisfied.
 */
static int
rio_setup_inb_dbell(struct rio_mport *mport, void *dev_id, struct resource *res,
		    void (*dinb) (struct rio_mport * mport, void *dev_id, u16 src, u16 dst,
				  u16 info))
{
	struct rio_dbell *dbell = kmalloc(sizeof(*dbell), GFP_KERNEL);

	if (!dbell)
		return -ENOMEM;

	dbell->res = res;
	dbell->dinb = dinb;
	dbell->dev_id = dev_id;

	mutex_lock(&mport->lock);
	list_add_tail(&dbell->node, &mport->dbells);
	mutex_unlock(&mport->lock);
	return 0;
}

/**
 * rio_request_inb_dbell - request inbound doorbell message service
 * @mport: RIO master port from which to allocate the doorbell resource
 * @dev_id: Device specific pointer to pass on event
 * @start: Doorbell info range start
 * @end: Doorbell info range end
 * @dinb: Callback to execute when doorbell is received
 *
 * Requests ownership of an inbound doorbell resource and binds
 * a callback function to the resource. Returns 0 if the request
 * has been satisfied.
 */
int rio_request_inb_dbell(struct rio_mport *mport,
			  void *dev_id,
			  u16 start,
			  u16 end,
			  void (*dinb) (struct rio_mport * mport, void *dev_id, u16 src,
					u16 dst, u16 info))
{
	int rc;
	struct resource *res = kzalloc(sizeof(*res), GFP_KERNEL);

	if (res) {
		rio_init_dbell_res(res, start, end);

		/* Make sure these doorbells aren't in use */
		rc = request_resource(&mport->riores[RIO_DOORBELL_RESOURCE],
				      res);
		if (rc < 0) {
			kfree(res);
			goto out;
		}

		/* Hook the doorbell callback */
		rc = rio_setup_inb_dbell(mport, dev_id, res, dinb);
	} else
		rc = -ENOMEM;

      out:
	return rc;
}
EXPORT_SYMBOL_GPL(rio_request_inb_dbell);

/**
 * rio_release_inb_dbell - release inbound doorbell message service
 * @mport: RIO master port from which to release the doorbell resource
 * @start: Doorbell info range start
 * @end: Doorbell info range end
 *
 * Releases ownership of an inbound doorbell resource and removes
 * callback from the doorbell event list. Returns 0 if the request
 * has been satisfied.
 */
int rio_release_inb_dbell(struct rio_mport *mport, u16 start, u16 end)
{
	int rc = 0, found = 0;
	struct rio_dbell *dbell;

	mutex_lock(&mport->lock);
	list_for_each_entry(dbell, &mport->dbells, node) {
		if ((dbell->res->start == start) && (dbell->res->end == end)) {
			list_del(&dbell->node);
			found = 1;
			break;
		}
	}
	mutex_unlock(&mport->lock);

	/* If we can't find an exact match, fail */
	if (!found) {
		rc = -EINVAL;
		goto out;
	}

	/* Release the doorbell resource */
	rc = release_resource(dbell->res);

	/* Free the doorbell event */
	kfree(dbell);

      out:
	return rc;
}
EXPORT_SYMBOL_GPL(rio_release_inb_dbell);

/**
 * rio_request_outb_dbell - request outbound doorbell message range
 * @rdev: RIO device from which to allocate the doorbell resource
 * @start: Doorbell message range start
 * @end: Doorbell message range end
 *
 * Requests ownership of a doorbell message range. Returns a resource
 * if the request has been satisfied or %NULL on failure.
 */
struct resource *rio_request_outb_dbell(struct rio_dev *rdev, u16 start,
					u16 end)
{
	struct resource *res = kzalloc(sizeof(struct resource), GFP_KERNEL);

	if (res) {
		rio_init_dbell_res(res, start, end);

		/* Make sure these doorbells aren't in use */
		if (request_resource(&rdev->riores[RIO_DOORBELL_RESOURCE], res)
		    < 0) {
			kfree(res);
			res = NULL;
		}
	}

	return res;
}
EXPORT_SYMBOL_GPL(rio_request_outb_dbell);

/**
 * rio_release_outb_dbell - release outbound doorbell message range
 * @rdev: RIO device from which to release the doorbell resource
 * @res: Doorbell resource to be freed
 *
 * Releases ownership of a doorbell message range. Returns 0 if the
 * request has been satisfied.
 */
int rio_release_outb_dbell(struct rio_dev *rdev, struct resource *res)
{
	int rc = release_resource(res);

	kfree(res);

	return rc;
}
EXPORT_SYMBOL_GPL(rio_release_outb_dbell);

/**
 * rio_add_mport_pw_handler - add port-write message handler into the list
 *                            of mport specific pw handlers
 * @mport:   RIO master port to bind the portwrite callback
 * @context: Handler specific context to pass on event
 * @pwcback: Callback to execute when portwrite is received
 *
 * Returns 0 if the request has been satisfied.
 */
int rio_add_mport_pw_handler(struct rio_mport *mport, void *context,
			     int (*pwcback)(struct rio_mport *mport,
			     void *context, union rio_pw_msg *msg, int step))
{
	struct rio_pwrite *pwrite = kzalloc(sizeof(*pwrite), GFP_KERNEL);

	if (!pwrite)
		return -ENOMEM;

	pwrite->pwcback = pwcback;
	pwrite->context = context;
	mutex_lock(&mport->lock);
	list_add_tail(&pwrite->node, &mport->pwrites);
	mutex_unlock(&mport->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(rio_add_mport_pw_handler);

/**
 * rio_del_mport_pw_handler - remove port-write message handler from the list
 *                            of mport specific pw handlers
 * @mport:   RIO master port to bind the portwrite callback
 * @context: Registered handler specific context to pass on event
 * @pwcback: Registered callback function
 *
 * Returns 0 if the request has been satisfied.
 */
int rio_del_mport_pw_handler(struct rio_mport *mport, void *context,
			     int (*pwcback)(struct rio_mport *mport,
			     void *context, union rio_pw_msg *msg, int step))
{
	int rc = -EINVAL;
	struct rio_pwrite *pwrite;

	mutex_lock(&mport->lock);
	list_for_each_entry(pwrite, &mport->pwrites, node) {
		if (pwrite->pwcback == pwcback && pwrite->context == context) {
			list_del(&pwrite->node);
			kfree(pwrite);
			rc = 0;
			break;
		}
	}
	mutex_unlock(&mport->lock);

	return rc;
}
EXPORT_SYMBOL_GPL(rio_del_mport_pw_handler);

/**
 * rio_request_inb_pwrite - request inbound port-write message service for
 *                          specific RapidIO device
 * @rdev: RIO device to which register inbound port-write callback routine
 * @pwcback: Callback routine to execute when port-write is received
 *
 * Binds a port-write callback function to the RapidIO device.
 * Returns 0 if the request has been satisfied.
 */
int rio_request_inb_pwrite(struct rio_dev *rdev,
	int (*pwcback)(struct rio_dev *rdev, union rio_pw_msg *msg, int step))
{
	int rc = 0;

	spin_lock(&rio_global_list_lock);
	if (rdev->pwcback)
		rc = -ENOMEM;
	else
		rdev->pwcback = pwcback;

	spin_unlock(&rio_global_list_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(rio_request_inb_pwrite);

/**
 * rio_release_inb_pwrite - release inbound port-write message service
 *                          associated with specific RapidIO device
 * @rdev: RIO device which registered for inbound port-write callback
 *
 * Removes callback from the rio_dev structure. Returns 0 if the request
 * has been satisfied.
 */
int rio_release_inb_pwrite(struct rio_dev *rdev)
{
	int rc = -ENOMEM;

	spin_lock(&rio_global_list_lock);
	if (rdev->pwcback) {
		rdev->pwcback = NULL;
		rc = 0;
	}

	spin_unlock(&rio_global_list_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(rio_release_inb_pwrite);

/**
 * rio_pw_enable - Enables/disables port-write handling by a master port
 * @mport: Master port associated with port-write handling
 * @enable:  1=enable,  0=disable
 */
void rio_pw_enable(struct rio_mport *mport, int enable)
{
	if (mport->ops->pwenable) {
		mutex_lock(&mport->lock);

		if ((enable && ++mport->pwe_refcnt == 1) ||
		    (!enable && mport->pwe_refcnt && --mport->pwe_refcnt == 0))
			mport->ops->pwenable(mport, enable);
		mutex_unlock(&mport->lock);
	}
}
EXPORT_SYMBOL_GPL(rio_pw_enable);

/**
 * rio_map_inb_region -- Map inbound memory region.
 * @mport: Master port.
 * @local: physical address of memory region to be mapped
 * @rbase: RIO base address assigned to this window
 * @size: Size of the memory region
 * @rflags: Flags for mapping.
 *
 * Return: 0 -- Success.
 *
 * This function will create the mapping from RIO space to local memory.
 */
int rio_map_inb_region(struct rio_mport *mport, dma_addr_t local,
			u64 rbase, u32 size, u32 rflags)
{
	int rc;
	unsigned long flags;

	if (!mport->ops->map_inb)
		return -1;
	spin_lock_irqsave(&rio_mmap_lock, flags);
	rc = mport->ops->map_inb(mport, local, rbase, size, rflags);
	spin_unlock_irqrestore(&rio_mmap_lock, flags);
	return rc;
}
EXPORT_SYMBOL_GPL(rio_map_inb_region);

/**
 * rio_unmap_inb_region -- Unmap the inbound memory region
 * @mport: Master port
 * @lstart: physical address of memory region to be unmapped
 */
void rio_unmap_inb_region(struct rio_mport *mport, dma_addr_t lstart)
{
	unsigned long flags;
	if (!mport->ops->unmap_inb)
		return;
	spin_lock_irqsave(&rio_mmap_lock, flags);
	mport->ops->unmap_inb(mport, lstart);
	spin_unlock_irqrestore(&rio_mmap_lock, flags);
}
EXPORT_SYMBOL_GPL(rio_unmap_inb_region);

/**
 * rio_map_outb_region -- Map outbound memory region.
 * @mport: Master port.
 * @destid: destination id window points to
 * @rbase: RIO base address window translates to
 * @size: Size of the memory region
 * @rflags: Flags for mapping.
 * @local: physical address of memory region mapped
 *
 * Return: 0 -- Success.
 *
 * This function will create the mapping from RIO space to local memory.
 */
int rio_map_outb_region(struct rio_mport *mport, u16 destid, u64 rbase,
			u32 size, u32 rflags, dma_addr_t *local)
{
	int rc;
	unsigned long flags;

	if (!mport->ops->map_outb)
		return -ENODEV;

	spin_lock_irqsave(&rio_mmap_lock, flags);
	rc = mport->ops->map_outb(mport, destid, rbase, size,
		rflags, local);
	spin_unlock_irqrestore(&rio_mmap_lock, flags);

	return rc;
}
EXPORT_SYMBOL_GPL(rio_map_outb_region);

/**
 * rio_unmap_inb_region -- Unmap the inbound memory region
 * @mport: Master port
 * @destid: destination id mapping points to
 * @rstart: RIO base address window translates to
 */
void rio_unmap_outb_region(struct rio_mport *mport, u16 destid, u64 rstart)
{
	unsigned long flags;

	if (!mport->ops->unmap_outb)
		return;

	spin_lock_irqsave(&rio_mmap_lock, flags);
	mport->ops->unmap_outb(mport, destid, rstart);
	spin_unlock_irqrestore(&rio_mmap_lock, flags);
}
EXPORT_SYMBOL_GPL(rio_unmap_outb_region);

/**
 * rio_mport_get_physefb - Helper function that returns register offset
 *                      for Physical Layer Extended Features Block.
 * @port: Master port to issue transaction
 * @local: Indicate a local master port or remote device access
 * @destid: Destination ID of the device
 * @hopcount: Number of switch hops to the device
 * @rmap: pointer to location to store register map type info
 */
u32
rio_mport_get_physefb(struct rio_mport *port, int local,
		      u16 destid, u8 hopcount, u32 *rmap)
{
	u32 ext_ftr_ptr;
	u32 ftr_header;

	ext_ftr_ptr = rio_mport_get_efb(port, local, destid, hopcount, 0);

	while (ext_ftr_ptr)  {
		if (local)
			rio_local_read_config_32(port, ext_ftr_ptr,
						 &ftr_header);
		else
			rio_mport_read_config_32(port, destid, hopcount,
						 ext_ftr_ptr, &ftr_header);

		ftr_header = RIO_GET_BLOCK_ID(ftr_header);
		switch (ftr_header) {

		case RIO_EFB_SER_EP_ID:
		case RIO_EFB_SER_EP_REC_ID:
		case RIO_EFB_SER_EP_FREE_ID:
		case RIO_EFB_SER_EP_M1_ID:
		case RIO_EFB_SER_EP_SW_M1_ID:
		case RIO_EFB_SER_EPF_M1_ID:
		case RIO_EFB_SER_EPF_SW_M1_ID:
			*rmap = 1;
			return ext_ftr_ptr;

		case RIO_EFB_SER_EP_M2_ID:
		case RIO_EFB_SER_EP_SW_M2_ID:
		case RIO_EFB_SER_EPF_M2_ID:
		case RIO_EFB_SER_EPF_SW_M2_ID:
			*rmap = 2;
			return ext_ftr_ptr;

		default:
			break;
		}

		ext_ftr_ptr = rio_mport_get_efb(port, local, destid,
						hopcount, ext_ftr_ptr);
	}

	return ext_ftr_ptr;
}
EXPORT_SYMBOL_GPL(rio_mport_get_physefb);

/**
 * rio_get_comptag - Begin or continue searching for a RIO device by component tag
 * @comp_tag: RIO component tag to match
 * @from: Previous RIO device found in search, or %NULL for new search
 *
 * Iterates through the list of known RIO devices. If a RIO device is
 * found with a matching @comp_tag, a pointer to its device
 * structure is returned. Otherwise, %NULL is returned. A new search
 * is initiated by passing %NULL to the @from argument. Otherwise, if
 * @from is not %NULL, searches continue from next device on the global
 * list.
 */
struct rio_dev *rio_get_comptag(u32 comp_tag, struct rio_dev *from)
{
	struct list_head *n;
	struct rio_dev *rdev;

	spin_lock(&rio_global_list_lock);
	n = from ? from->global_list.next : rio_devices.next;

	while (n && (n != &rio_devices)) {
		rdev = rio_dev_g(n);
		if (rdev->comp_tag == comp_tag)
			goto exit;
		n = n->next;
	}
	rdev = NULL;
exit:
	spin_unlock(&rio_global_list_lock);
	return rdev;
}
EXPORT_SYMBOL_GPL(rio_get_comptag);

/**
 * rio_set_port_lockout - Sets/clears LOCKOUT bit (RIO EM 1.3) for a switch port.
 * @rdev: Pointer to RIO device control structure
 * @pnum: Switch port number to set LOCKOUT bit
 * @lock: Operation : set (=1) or clear (=0)
 */
int rio_set_port_lockout(struct rio_dev *rdev, u32 pnum, int lock)
{
	u32 regval;

	rio_read_config_32(rdev,
		RIO_DEV_PORT_N_CTL_CSR(rdev, pnum),
		&regval);
	if (lock)
		regval |= RIO_PORT_N_CTL_LOCKOUT;
	else
		regval &= ~RIO_PORT_N_CTL_LOCKOUT;

	rio_write_config_32(rdev,
		RIO_DEV_PORT_N_CTL_CSR(rdev, pnum),
		regval);
	return 0;
}
EXPORT_SYMBOL_GPL(rio_set_port_lockout);

/**
 * rio_enable_rx_tx_port - enable input receiver and output transmitter of
 * given port
 * @port: Master port associated with the RIO network
 * @local: local=1 select local port otherwise a far device is reached
 * @destid: Destination ID of the device to check host bit
 * @hopcount: Number of hops to reach the target
 * @port_num: Port (-number on switch) to enable on a far end device
 *
 * Returns 0 or 1 from on General Control Command and Status Register
 * (EXT_PTR+0x3C)
 */
int rio_enable_rx_tx_port(struct rio_mport *port,
			  int local, u16 destid,
			  u8 hopcount, u8 port_num)
{
#ifdef CONFIG_RAPIDIO_ENABLE_RX_TX_PORTS
	u32 regval;
	u32 ext_ftr_ptr;
	u32 rmap;

	/*
	* enable rx input tx output port
	*/
	pr_debug("rio_enable_rx_tx_port(local = %d, destid = %d, hopcount = "
		 "%d, port_num = %d)\n", local, destid, hopcount, port_num);

	ext_ftr_ptr = rio_mport_get_physefb(port, local, destid,
					    hopcount, &rmap);

	if (local) {
		rio_local_read_config_32(port,
				ext_ftr_ptr + RIO_PORT_N_CTL_CSR(0, rmap),
				&regval);
	} else {
		if (rio_mport_read_config_32(port, destid, hopcount,
			ext_ftr_ptr + RIO_PORT_N_CTL_CSR(port_num, rmap),
				&regval) < 0)
			return -EIO;
	}

	regval = regval | RIO_PORT_N_CTL_EN_RX | RIO_PORT_N_CTL_EN_TX;

	if (local) {
		rio_local_write_config_32(port,
			ext_ftr_ptr + RIO_PORT_N_CTL_CSR(0, rmap), regval);
	} else {
		if (rio_mport_write_config_32(port, destid, hopcount,
			ext_ftr_ptr + RIO_PORT_N_CTL_CSR(port_num, rmap),
				regval) < 0)
			return -EIO;
	}
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(rio_enable_rx_tx_port);


/**
 * rio_chk_dev_route - Validate route to the specified device.
 * @rdev:  RIO device failed to respond
 * @nrdev: Last active device on the route to rdev
 * @npnum: nrdev's port number on the route to rdev
 *
 * Follows a route to the specified RIO device to determine the last available
 * device (and corresponding RIO port) on the route.
 */
static int
rio_chk_dev_route(struct rio_dev *rdev, struct rio_dev **nrdev, int *npnum)
{
	u32 result;
	int p_port, rc = -EIO;
	struct rio_dev *prev = NULL;

	/* Find switch with failed RIO link */
	while (rdev->prev && (rdev->prev->pef & RIO_PEF_SWITCH)) {
		if (!rio_read_config_32(rdev->prev, RIO_DEV_ID_CAR, &result)) {
			prev = rdev->prev;
			break;
		}
		rdev = rdev->prev;
	}

	if (!prev)
		goto err_out;

	p_port = prev->rswitch->route_table[rdev->destid];

	if (p_port != RIO_INVALID_ROUTE) {
		pr_debug("RIO: link failed on [%s]-P%d\n",
			 rio_name(prev), p_port);
		*nrdev = prev;
		*npnum = p_port;
		rc = 0;
	} else
		pr_debug("RIO: failed to trace route to %s\n", rio_name(rdev));
err_out:
	return rc;
}

/**
 * rio_mport_chk_dev_access - Validate access to the specified device.
 * @mport: Master port to send transactions
 * @destid: Device destination ID in network
 * @hopcount: Number of hops into the network
 */
int
rio_mport_chk_dev_access(struct rio_mport *mport, u16 destid, u8 hopcount)
{
	int i = 0;
	u32 tmp;

	while (rio_mport_read_config_32(mport, destid, hopcount,
					RIO_DEV_ID_CAR, &tmp)) {
		i++;
		if (i == RIO_MAX_CHK_RETRY)
			return -EIO;
		mdelay(1);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rio_mport_chk_dev_access);

/**
 * rio_chk_dev_access - Validate access to the specified device.
 * @rdev: Pointer to RIO device control structure
 */
static int rio_chk_dev_access(struct rio_dev *rdev)
{
	return rio_mport_chk_dev_access(rdev->net->hport,
					rdev->destid, rdev->hopcount);
}

/**
 * rio_get_input_status - Sends a Link-Request/Input-Status control symbol and
 *                        returns link-response (if requested).
 * @rdev: RIO devive to issue Input-status command
 * @pnum: Device port number to issue the command
 * @lnkresp: Response from a link partner
 */
static int
rio_get_input_status(struct rio_dev *rdev, int pnum, u32 *lnkresp)
{
	u32 regval;
	int checkcount;

	if (lnkresp) {
		/* Read from link maintenance response register
		 * to clear valid bit */
		rio_read_config_32(rdev,
			RIO_DEV_PORT_N_MNT_RSP_CSR(rdev, pnum),
			&regval);
		udelay(50);
	}

	/* Issue Input-status command */
	rio_write_config_32(rdev,
		RIO_DEV_PORT_N_MNT_REQ_CSR(rdev, pnum),
		RIO_MNT_REQ_CMD_IS);

	/* Exit if the response is not expected */
	if (!lnkresp)
		return 0;

	checkcount = 3;
	while (checkcount--) {
		udelay(50);
		rio_read_config_32(rdev,
			RIO_DEV_PORT_N_MNT_RSP_CSR(rdev, pnum),
			&regval);
		if (regval & RIO_PORT_N_MNT_RSP_RVAL) {
			*lnkresp = regval;
			return 0;
		}
	}

	return -EIO;
}

/**
 * rio_clr_err_stopped - Clears port Error-stopped states.
 * @rdev: Pointer to RIO device control structure
 * @pnum: Switch port number to clear errors
 * @err_status: port error status (if 0 reads register from device)
 *
 * TODO: Currently this routine is not compatible with recovery process
 * specified for idt_gen3 RapidIO switch devices. It has to be reviewed
 * to implement universal recovery process that is compatible full range
 * off available devices.
 * IDT gen3 switch driver now implements HW-specific error handler that
 * issues soft port reset to the port to reset ERR_STOP bits and ackIDs.
 */
static int rio_clr_err_stopped(struct rio_dev *rdev, u32 pnum, u32 err_status)
{
	struct rio_dev *nextdev = rdev->rswitch->nextdev[pnum];
	u32 regval;
	u32 far_ackid, far_linkstat, near_ackid;

	if (err_status == 0)
		rio_read_config_32(rdev,
			RIO_DEV_PORT_N_ERR_STS_CSR(rdev, pnum),
			&err_status);

	if (err_status & RIO_PORT_N_ERR_STS_OUT_ES) {
		pr_debug("RIO_EM: servicing Output Error-Stopped state\n");
		/*
		 * Send a Link-Request/Input-Status control symbol
		 */
		if (rio_get_input_status(rdev, pnum, &regval)) {
			pr_debug("RIO_EM: Input-status response timeout\n");
			goto rd_err;
		}

		pr_debug("RIO_EM: SP%d Input-status response=0x%08x\n",
			 pnum, regval);
		far_ackid = (regval & RIO_PORT_N_MNT_RSP_ASTAT) >> 5;
		far_linkstat = regval & RIO_PORT_N_MNT_RSP_LSTAT;
		rio_read_config_32(rdev,
			RIO_DEV_PORT_N_ACK_STS_CSR(rdev, pnum),
			&regval);
		pr_debug("RIO_EM: SP%d_ACK_STS_CSR=0x%08x\n", pnum, regval);
		near_ackid = (regval & RIO_PORT_N_ACK_INBOUND) >> 24;
		pr_debug("RIO_EM: SP%d far_ackID=0x%02x far_linkstat=0x%02x" \
			 " near_ackID=0x%02x\n",
			pnum, far_ackid, far_linkstat, near_ackid);

		/*
		 * If required, synchronize ackIDs of near and
		 * far sides.
		 */
		if ((far_ackid != ((regval & RIO_PORT_N_ACK_OUTSTAND) >> 8)) ||
		    (far_ackid != (regval & RIO_PORT_N_ACK_OUTBOUND))) {
			/* Align near outstanding/outbound ackIDs with
			 * far inbound.
			 */
			rio_write_config_32(rdev,
				RIO_DEV_PORT_N_ACK_STS_CSR(rdev, pnum),
				(near_ackid << 24) |
					(far_ackid << 8) | far_ackid);
			/* Align far outstanding/outbound ackIDs with
			 * near inbound.
			 */
			far_ackid++;
			if (!nextdev) {
				pr_debug("RIO_EM: nextdev pointer == NULL\n");
				goto rd_err;
			}

			rio_write_config_32(nextdev,
				RIO_DEV_PORT_N_ACK_STS_CSR(nextdev,
					RIO_GET_PORT_NUM(nextdev->swpinfo)),
				(far_ackid << 24) |
				(near_ackid << 8) | near_ackid);
		}
rd_err:
		rio_read_config_32(rdev, RIO_DEV_PORT_N_ERR_STS_CSR(rdev, pnum),
				   &err_status);
		pr_debug("RIO_EM: SP%d_ERR_STS_CSR=0x%08x\n", pnum, err_status);
	}

	if ((err_status & RIO_PORT_N_ERR_STS_INP_ES) && nextdev) {
		pr_debug("RIO_EM: servicing Input Error-Stopped state\n");
		rio_get_input_status(nextdev,
				     RIO_GET_PORT_NUM(nextdev->swpinfo), NULL);
		udelay(50);

		rio_read_config_32(rdev, RIO_DEV_PORT_N_ERR_STS_CSR(rdev, pnum),
				   &err_status);
		pr_debug("RIO_EM: SP%d_ERR_STS_CSR=0x%08x\n", pnum, err_status);
	}

	return (err_status & (RIO_PORT_N_ERR_STS_OUT_ES |
			      RIO_PORT_N_ERR_STS_INP_ES)) ? 1 : 0;
}

/**
 * rio_inb_pwrite_handler - inbound port-write message handler
 * @mport:  mport device associated with port-write
 * @pw_msg: pointer to inbound port-write message
 *
 * Processes an inbound port-write message. Returns 0 if the request
 * has been satisfied.
 */
int rio_inb_pwrite_handler(struct rio_mport *mport, union rio_pw_msg *pw_msg)
{
	struct rio_dev *rdev;
	u32 err_status, em_perrdet, em_ltlerrdet;
	int rc, portnum;
	struct rio_pwrite *pwrite;

#ifdef DEBUG_PW
	{
		u32 i;

		pr_debug("%s: PW to mport_%d:\n", __func__, mport->id);
		for (i = 0; i < RIO_PW_MSG_SIZE / sizeof(u32); i = i + 4) {
			pr_debug("0x%02x: %08x %08x %08x %08x\n",
				i * 4, pw_msg->raw[i], pw_msg->raw[i + 1],
				pw_msg->raw[i + 2], pw_msg->raw[i + 3]);
		}
	}
#endif

	rdev = rio_get_comptag((pw_msg->em.comptag & RIO_CTAG_UDEVID), NULL);
	if (rdev) {
		pr_debug("RIO: Port-Write message from %s\n", rio_name(rdev));
	} else {
		pr_debug("RIO: %s No matching device for CTag 0x%08x\n",
			__func__, pw_msg->em.comptag);
	}

	/* Call a device-specific handler (if it is registered for the device).
	 * This may be the service for endpoints that send device-specific
	 * port-write messages. End-point messages expected to be handled
	 * completely by EP specific device driver.
	 * For switches rc==0 signals that no standard processing required.
	 */
	if (rdev && rdev->pwcback) {
		rc = rdev->pwcback(rdev, pw_msg, 0);
		if (rc == 0)
			return 0;
	}

	mutex_lock(&mport->lock);
	list_for_each_entry(pwrite, &mport->pwrites, node)
		pwrite->pwcback(mport, pwrite->context, pw_msg, 0);
	mutex_unlock(&mport->lock);

	if (!rdev)
		return 0;

	/*
	 * FIXME: The code below stays as it was before for now until we decide
	 * how to do default PW handling in combination with per-mport callbacks
	 */

	portnum = pw_msg->em.is_port & 0xFF;

	/* Check if device and route to it are functional:
	 * Sometimes devices may send PW message(s) just before being
	 * powered down (or link being lost).
	 */
	if (rio_chk_dev_access(rdev)) {
		pr_debug("RIO: device access failed - get link partner\n");
		/* Scan route to the device and identify failed link.
		 * This will replace device and port reported in PW message.
		 * PW message should not be used after this point.
		 */
		if (rio_chk_dev_route(rdev, &rdev, &portnum)) {
			pr_err("RIO: Route trace for %s failed\n",
				rio_name(rdev));
			return -EIO;
		}
		pw_msg = NULL;
	}

	/* For End-point devices processing stops here */
	if (!(rdev->pef & RIO_PEF_SWITCH))
		return 0;

	if (rdev->phys_efptr == 0) {
		pr_err("RIO_PW: Bad switch initialization for %s\n",
			rio_name(rdev));
		return 0;
	}

	/*
	 * Process the port-write notification from switch
	 */
	if (rdev->rswitch->ops && rdev->rswitch->ops->em_handle)
		rdev->rswitch->ops->em_handle(rdev, portnum);

	rio_read_config_32(rdev, RIO_DEV_PORT_N_ERR_STS_CSR(rdev, portnum),
			   &err_status);
	pr_debug("RIO_PW: SP%d_ERR_STS_CSR=0x%08x\n", portnum, err_status);

	if (err_status & RIO_PORT_N_ERR_STS_PORT_OK) {

		if (!(rdev->rswitch->port_ok & (1 << portnum))) {
			rdev->rswitch->port_ok |= (1 << portnum);
			rio_set_port_lockout(rdev, portnum, 0);
			/* Schedule Insertion Service */
			pr_debug("RIO_PW: Device Insertion on [%s]-P%d\n",
			       rio_name(rdev), portnum);
		}

		/* Clear error-stopped states (if reported).
		 * Depending on the link partner state, two attempts
		 * may be needed for successful recovery.
		 */
		if (err_status & (RIO_PORT_N_ERR_STS_OUT_ES |
				  RIO_PORT_N_ERR_STS_INP_ES)) {
			if (rio_clr_err_stopped(rdev, portnum, err_status))
				rio_clr_err_stopped(rdev, portnum, 0);
		}
	}  else { /* if (err_status & RIO_PORT_N_ERR_STS_PORT_UNINIT) */

		if (rdev->rswitch->port_ok & (1 << portnum)) {
			rdev->rswitch->port_ok &= ~(1 << portnum);
			rio_set_port_lockout(rdev, portnum, 1);

			if (rdev->phys_rmap == 1) {
			rio_write_config_32(rdev,
				RIO_DEV_PORT_N_ACK_STS_CSR(rdev, portnum),
				RIO_PORT_N_ACK_CLEAR);
			} else {
				rio_write_config_32(rdev,
					RIO_DEV_PORT_N_OB_ACK_CSR(rdev, portnum),
					RIO_PORT_N_OB_ACK_CLEAR);
				rio_write_config_32(rdev,
					RIO_DEV_PORT_N_IB_ACK_CSR(rdev, portnum),
					0);
			}

			/* Schedule Extraction Service */
			pr_debug("RIO_PW: Device Extraction on [%s]-P%d\n",
			       rio_name(rdev), portnum);
		}
	}

	rio_read_config_32(rdev,
		rdev->em_efptr + RIO_EM_PN_ERR_DETECT(portnum), &em_perrdet);
	if (em_perrdet) {
		pr_debug("RIO_PW: RIO_EM_P%d_ERR_DETECT=0x%08x\n",
			 portnum, em_perrdet);
		/* Clear EM Port N Error Detect CSR */
		rio_write_config_32(rdev,
			rdev->em_efptr + RIO_EM_PN_ERR_DETECT(portnum), 0);
	}

	rio_read_config_32(rdev,
		rdev->em_efptr + RIO_EM_LTL_ERR_DETECT, &em_ltlerrdet);
	if (em_ltlerrdet) {
		pr_debug("RIO_PW: RIO_EM_LTL_ERR_DETECT=0x%08x\n",
			 em_ltlerrdet);
		/* Clear EM L/T Layer Error Detect CSR */
		rio_write_config_32(rdev,
			rdev->em_efptr + RIO_EM_LTL_ERR_DETECT, 0);
	}

	/* Clear remaining error bits and Port-Write Pending bit */
	rio_write_config_32(rdev, RIO_DEV_PORT_N_ERR_STS_CSR(rdev, portnum),
			    err_status);

	return 0;
}
EXPORT_SYMBOL_GPL(rio_inb_pwrite_handler);

/**
 * rio_mport_get_efb - get pointer to next extended features block
 * @port: Master port to issue transaction
 * @local: Indicate a local master port or remote device access
 * @destid: Destination ID of the device
 * @hopcount: Number of switch hops to the device
 * @from: Offset of  current Extended Feature block header (if 0 starts
 * from	ExtFeaturePtr)
 */
u32
rio_mport_get_efb(struct rio_mport *port, int local, u16 destid,
		      u8 hopcount, u32 from)
{
	u32 reg_val;

	if (from == 0) {
		if (local)
			rio_local_read_config_32(port, RIO_ASM_INFO_CAR,
						 &reg_val);
		else
			rio_mport_read_config_32(port, destid, hopcount,
						 RIO_ASM_INFO_CAR, &reg_val);
		return reg_val & RIO_EXT_FTR_PTR_MASK;
	} else {
		if (local)
			rio_local_read_config_32(port, from, &reg_val);
		else
			rio_mport_read_config_32(port, destid, hopcount,
						 from, &reg_val);
		return RIO_GET_BLOCK_ID(reg_val);
	}
}
EXPORT_SYMBOL_GPL(rio_mport_get_efb);

/**
 * rio_mport_get_feature - query for devices' extended features
 * @port: Master port to issue transaction
 * @local: Indicate a local master port or remote device access
 * @destid: Destination ID of the device
 * @hopcount: Number of switch hops to the device
 * @ftr: Extended feature code
 *
 * Tell if a device supports a given RapidIO capability.
 * Returns the offset of the requested extended feature
 * block within the device's RIO configuration space or
 * 0 in case the device does not support it.
 */
u32
rio_mport_get_feature(struct rio_mport * port, int local, u16 destid,
		      u8 hopcount, int ftr)
{
	u32 asm_info, ext_ftr_ptr, ftr_header;

	if (local)
		rio_local_read_config_32(port, RIO_ASM_INFO_CAR, &asm_info);
	else
		rio_mport_read_config_32(port, destid, hopcount,
					 RIO_ASM_INFO_CAR, &asm_info);

	ext_ftr_ptr = asm_info & RIO_EXT_FTR_PTR_MASK;

	while (ext_ftr_ptr) {
		if (local)
			rio_local_read_config_32(port, ext_ftr_ptr,
						 &ftr_header);
		else
			rio_mport_read_config_32(port, destid, hopcount,
						 ext_ftr_ptr, &ftr_header);
		if (RIO_GET_BLOCK_ID(ftr_header) == ftr)
			return ext_ftr_ptr;

		ext_ftr_ptr = RIO_GET_BLOCK_PTR(ftr_header);
		if (!ext_ftr_ptr)
			break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rio_mport_get_feature);

/**
 * rio_get_asm - Begin or continue searching for a RIO device by vid/did/asm_vid/asm_did
 * @vid: RIO vid to match or %RIO_ANY_ID to match all vids
 * @did: RIO did to match or %RIO_ANY_ID to match all dids
 * @asm_vid: RIO asm_vid to match or %RIO_ANY_ID to match all asm_vids
 * @asm_did: RIO asm_did to match or %RIO_ANY_ID to match all asm_dids
 * @from: Previous RIO device found in search, or %NULL for new search
 *
 * Iterates through the list of known RIO devices. If a RIO device is
 * found with a matching @vid, @did, @asm_vid, @asm_did, the reference
 * count to the device is incrememted and a pointer to its device
 * structure is returned. Otherwise, %NULL is returned. A new search
 * is initiated by passing %NULL to the @from argument. Otherwise, if
 * @from is not %NULL, searches continue from next device on the global
 * list. The reference count for @from is always decremented if it is
 * not %NULL.
 */
struct rio_dev *rio_get_asm(u16 vid, u16 did,
			    u16 asm_vid, u16 asm_did, struct rio_dev *from)
{
	struct list_head *n;
	struct rio_dev *rdev;

	WARN_ON(in_interrupt());
	spin_lock(&rio_global_list_lock);
	n = from ? from->global_list.next : rio_devices.next;

	while (n && (n != &rio_devices)) {
		rdev = rio_dev_g(n);
		if ((vid == RIO_ANY_ID || rdev->vid == vid) &&
		    (did == RIO_ANY_ID || rdev->did == did) &&
		    (asm_vid == RIO_ANY_ID || rdev->asm_vid == asm_vid) &&
		    (asm_did == RIO_ANY_ID || rdev->asm_did == asm_did))
			goto exit;
		n = n->next;
	}
	rdev = NULL;
      exit:
	rio_dev_put(from);
	rdev = rio_dev_get(rdev);
	spin_unlock(&rio_global_list_lock);
	return rdev;
}
EXPORT_SYMBOL_GPL(rio_get_asm);

/**
 * rio_get_device - Begin or continue searching for a RIO device by vid/did
 * @vid: RIO vid to match or %RIO_ANY_ID to match all vids
 * @did: RIO did to match or %RIO_ANY_ID to match all dids
 * @from: Previous RIO device found in search, or %NULL for new search
 *
 * Iterates through the list of known RIO devices. If a RIO device is
 * found with a matching @vid and @did, the reference count to the
 * device is incrememted and a pointer to its device structure is returned.
 * Otherwise, %NULL is returned. A new search is initiated by passing %NULL
 * to the @from argument. Otherwise, if @from is not %NULL, searches
 * continue from next device on the global list. The reference count for
 * @from is always decremented if it is not %NULL.
 */
struct rio_dev *rio_get_device(u16 vid, u16 did, struct rio_dev *from)
{
	return rio_get_asm(vid, did, RIO_ANY_ID, RIO_ANY_ID, from);
}
EXPORT_SYMBOL_GPL(rio_get_device);

/**
 * rio_std_route_add_entry - Add switch route table entry using standard
 *   registers defined in RIO specification rev.1.3
 * @mport: Master port to issue transaction
 * @destid: Destination ID of the device
 * @hopcount: Number of switch hops to the device
 * @table: routing table ID (global or port-specific)
 * @route_destid: destID entry in the RT
 * @route_port: destination port for specified destID
 */
static int
rio_std_route_add_entry(struct rio_mport *mport, u16 destid, u8 hopcount,
			u16 table, u16 route_destid, u8 route_port)
{
	if (table == RIO_GLOBAL_TABLE) {
		rio_mport_write_config_32(mport, destid, hopcount,
				RIO_STD_RTE_CONF_DESTID_SEL_CSR,
				(u32)route_destid);
		rio_mport_write_config_32(mport, destid, hopcount,
				RIO_STD_RTE_CONF_PORT_SEL_CSR,
				(u32)route_port);
	}

	udelay(10);
	return 0;
}

/**
 * rio_std_route_get_entry - Read switch route table entry (port number)
 *   associated with specified destID using standard registers defined in RIO
 *   specification rev.1.3
 * @mport: Master port to issue transaction
 * @destid: Destination ID of the device
 * @hopcount: Number of switch hops to the device
 * @table: routing table ID (global or port-specific)
 * @route_destid: destID entry in the RT
 * @route_port: returned destination port for specified destID
 */
static int
rio_std_route_get_entry(struct rio_mport *mport, u16 destid, u8 hopcount,
			u16 table, u16 route_destid, u8 *route_port)
{
	u32 result;

	if (table == RIO_GLOBAL_TABLE) {
		rio_mport_write_config_32(mport, destid, hopcount,
				RIO_STD_RTE_CONF_DESTID_SEL_CSR, route_destid);
		rio_mport_read_config_32(mport, destid, hopcount,
				RIO_STD_RTE_CONF_PORT_SEL_CSR, &result);

		*route_port = (u8)result;
	}

	return 0;
}

/**
 * rio_std_route_clr_table - Clear swotch route table using standard registers
 *   defined in RIO specification rev.1.3.
 * @mport: Master port to issue transaction
 * @destid: Destination ID of the device
 * @hopcount: Number of switch hops to the device
 * @table: routing table ID (global or port-specific)
 */
static int
rio_std_route_clr_table(struct rio_mport *mport, u16 destid, u8 hopcount,
			u16 table)
{
	u32 max_destid = 0xff;
	u32 i, pef, id_inc = 1, ext_cfg = 0;
	u32 port_sel = RIO_INVALID_ROUTE;

	if (table == RIO_GLOBAL_TABLE) {
		rio_mport_read_config_32(mport, destid, hopcount,
					 RIO_PEF_CAR, &pef);

		if (mport->sys_size) {
			rio_mport_read_config_32(mport, destid, hopcount,
						 RIO_SWITCH_RT_LIMIT,
						 &max_destid);
			max_destid &= RIO_RT_MAX_DESTID;
		}

		if (pef & RIO_PEF_EXT_RT) {
			ext_cfg = 0x80000000;
			id_inc = 4;
			port_sel = (RIO_INVALID_ROUTE << 24) |
				   (RIO_INVALID_ROUTE << 16) |
				   (RIO_INVALID_ROUTE << 8) |
				   RIO_INVALID_ROUTE;
		}

		for (i = 0; i <= max_destid;) {
			rio_mport_write_config_32(mport, destid, hopcount,
					RIO_STD_RTE_CONF_DESTID_SEL_CSR,
					ext_cfg | i);
			rio_mport_write_config_32(mport, destid, hopcount,
					RIO_STD_RTE_CONF_PORT_SEL_CSR,
					port_sel);
			i += id_inc;
		}
	}

	udelay(10);
	return 0;
}

/**
 * rio_lock_device - Acquires host device lock for specified device
 * @port: Master port to send transaction
 * @destid: Destination ID for device/switch
 * @hopcount: Hopcount to reach switch
 * @wait_ms: Max wait time in msec (0 = no timeout)
 *
 * Attepts to acquire host device lock for specified device
 * Returns 0 if device lock acquired or EINVAL if timeout expires.
 */
int rio_lock_device(struct rio_mport *port, u16 destid,
		    u8 hopcount, int wait_ms)
{
	u32 result;
	int tcnt = 0;

	/* Attempt to acquire device lock */
	rio_mport_write_config_32(port, destid, hopcount,
				  RIO_HOST_DID_LOCK_CSR, port->host_deviceid);
	rio_mport_read_config_32(port, destid, hopcount,
				 RIO_HOST_DID_LOCK_CSR, &result);

	while (result != port->host_deviceid) {
		if (wait_ms != 0 && tcnt == wait_ms) {
			pr_debug("RIO: timeout when locking device %x:%x\n",
				destid, hopcount);
			return -EINVAL;
		}

		/* Delay a bit */
		mdelay(1);
		tcnt++;
		/* Try to acquire device lock again */
		rio_mport_write_config_32(port, destid,
			hopcount,
			RIO_HOST_DID_LOCK_CSR,
			port->host_deviceid);
		rio_mport_read_config_32(port, destid,
			hopcount,
			RIO_HOST_DID_LOCK_CSR, &result);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rio_lock_device);

/**
 * rio_unlock_device - Releases host device lock for specified device
 * @port: Master port to send transaction
 * @destid: Destination ID for device/switch
 * @hopcount: Hopcount to reach switch
 *
 * Returns 0 if device lock released or EINVAL if fails.
 */
int rio_unlock_device(struct rio_mport *port, u16 destid, u8 hopcount)
{
	u32 result;

	/* Release device lock */
	rio_mport_write_config_32(port, destid,
				  hopcount,
				  RIO_HOST_DID_LOCK_CSR,
				  port->host_deviceid);
	rio_mport_read_config_32(port, destid, hopcount,
		RIO_HOST_DID_LOCK_CSR, &result);
	if ((result & 0xffff) != 0xffff) {
		pr_debug("RIO: badness when releasing device lock %x:%x\n",
			 destid, hopcount);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rio_unlock_device);

/**
 * rio_route_add_entry- Add a route entry to a switch routing table
 * @rdev: RIO device
 * @table: Routing table ID
 * @route_destid: Destination ID to be routed
 * @route_port: Port number to be routed
 * @lock: apply a hardware lock on switch device flag (1=lock, 0=no_lock)
 *
 * If available calls the switch specific add_entry() method to add a route
 * entry into a switch routing table. Otherwise uses standard RT update method
 * as defined by RapidIO specification. A specific routing table can be selected
 * using the @table argument if a switch has per port routing tables or
 * the standard (or global) table may be used by passing
 * %RIO_GLOBAL_TABLE in @table.
 *
 * Returns %0 on success or %-EINVAL on failure.
 */
int rio_route_add_entry(struct rio_dev *rdev,
			u16 table, u16 route_destid, u8 route_port, int lock)
{
	int rc = -EINVAL;
	struct rio_switch_ops *ops = rdev->rswitch->ops;

	if (lock) {
		rc = rio_lock_device(rdev->net->hport, rdev->destid,
				     rdev->hopcount, 1000);
		if (rc)
			return rc;
	}

	spin_lock(&rdev->rswitch->lock);

	if (!ops || !ops->add_entry) {
		rc = rio_std_route_add_entry(rdev->net->hport, rdev->destid,
					     rdev->hopcount, table,
					     route_destid, route_port);
	} else if (try_module_get(ops->owner)) {
		rc = ops->add_entry(rdev->net->hport, rdev->destid,
				    rdev->hopcount, table, route_destid,
				    route_port);
		module_put(ops->owner);
	}

	spin_unlock(&rdev->rswitch->lock);

	if (lock)
		rio_unlock_device(rdev->net->hport, rdev->destid,
				  rdev->hopcount);

	return rc;
}
EXPORT_SYMBOL_GPL(rio_route_add_entry);

/**
 * rio_route_get_entry- Read an entry from a switch routing table
 * @rdev: RIO device
 * @table: Routing table ID
 * @route_destid: Destination ID to be routed
 * @route_port: Pointer to read port number into
 * @lock: apply a hardware lock on switch device flag (1=lock, 0=no_lock)
 *
 * If available calls the switch specific get_entry() method to fetch a route
 * entry from a switch routing table. Otherwise uses standard RT read method
 * as defined by RapidIO specification. A specific routing table can be selected
 * using the @table argument if a switch has per port routing tables or
 * the standard (or global) table may be used by passing
 * %RIO_GLOBAL_TABLE in @table.
 *
 * Returns %0 on success or %-EINVAL on failure.
 */
int rio_route_get_entry(struct rio_dev *rdev, u16 table,
			u16 route_destid, u8 *route_port, int lock)
{
	int rc = -EINVAL;
	struct rio_switch_ops *ops = rdev->rswitch->ops;

	if (lock) {
		rc = rio_lock_device(rdev->net->hport, rdev->destid,
				     rdev->hopcount, 1000);
		if (rc)
			return rc;
	}

	spin_lock(&rdev->rswitch->lock);

	if (!ops || !ops->get_entry) {
		rc = rio_std_route_get_entry(rdev->net->hport, rdev->destid,
					     rdev->hopcount, table,
					     route_destid, route_port);
	} else if (try_module_get(ops->owner)) {
		rc = ops->get_entry(rdev->net->hport, rdev->destid,
				    rdev->hopcount, table, route_destid,
				    route_port);
		module_put(ops->owner);
	}

	spin_unlock(&rdev->rswitch->lock);

	if (lock)
		rio_unlock_device(rdev->net->hport, rdev->destid,
				  rdev->hopcount);
	return rc;
}
EXPORT_SYMBOL_GPL(rio_route_get_entry);

/**
 * rio_route_clr_table - Clear a switch routing table
 * @rdev: RIO device
 * @table: Routing table ID
 * @lock: apply a hardware lock on switch device flag (1=lock, 0=no_lock)
 *
 * If available calls the switch specific clr_table() method to clear a switch
 * routing table. Otherwise uses standard RT write method as defined by RapidIO
 * specification. A specific routing table can be selected using the @table
 * argument if a switch has per port routing tables or the standard (or global)
 * table may be used by passing %RIO_GLOBAL_TABLE in @table.
 *
 * Returns %0 on success or %-EINVAL on failure.
 */
int rio_route_clr_table(struct rio_dev *rdev, u16 table, int lock)
{
	int rc = -EINVAL;
	struct rio_switch_ops *ops = rdev->rswitch->ops;

	if (lock) {
		rc = rio_lock_device(rdev->net->hport, rdev->destid,
				     rdev->hopcount, 1000);
		if (rc)
			return rc;
	}

	spin_lock(&rdev->rswitch->lock);

	if (!ops || !ops->clr_table) {
		rc = rio_std_route_clr_table(rdev->net->hport, rdev->destid,
					     rdev->hopcount, table);
	} else if (try_module_get(ops->owner)) {
		rc = ops->clr_table(rdev->net->hport, rdev->destid,
				    rdev->hopcount, table);

		module_put(ops->owner);
	}

	spin_unlock(&rdev->rswitch->lock);

	if (lock)
		rio_unlock_device(rdev->net->hport, rdev->destid,
				  rdev->hopcount);

	return rc;
}
EXPORT_SYMBOL_GPL(rio_route_clr_table);

#ifdef CONFIG_RAPIDIO_DMA_ENGINE

static bool rio_chan_filter(struct dma_chan *chan, void *arg)
{
	struct rio_mport *mport = arg;

	/* Check that DMA device belongs to the right MPORT */
	return mport == container_of(chan->device, struct rio_mport, dma);
}

/**
 * rio_request_mport_dma - request RapidIO capable DMA channel associated
 *   with specified local RapidIO mport device.
 * @mport: RIO mport to perform DMA data transfers
 *
 * Returns pointer to allocated DMA channel or NULL if failed.
 */
struct dma_chan *rio_request_mport_dma(struct rio_mport *mport)
{
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	return dma_request_channel(mask, rio_chan_filter, mport);
}
EXPORT_SYMBOL_GPL(rio_request_mport_dma);

/**
 * rio_request_dma - request RapidIO capable DMA channel that supports
 *   specified target RapidIO device.
 * @rdev: RIO device associated with DMA transfer
 *
 * Returns pointer to allocated DMA channel or NULL if failed.
 */
struct dma_chan *rio_request_dma(struct rio_dev *rdev)
{
	return rio_request_mport_dma(rdev->net->hport);
}
EXPORT_SYMBOL_GPL(rio_request_dma);

/**
 * rio_release_dma - release specified DMA channel
 * @dchan: DMA channel to release
 */
void rio_release_dma(struct dma_chan *dchan)
{
	dma_release_channel(dchan);
}
EXPORT_SYMBOL_GPL(rio_release_dma);

/**
 * rio_dma_prep_xfer - RapidIO specific wrapper
 *   for device_prep_slave_sg callback defined by DMAENGINE.
 * @dchan: DMA channel to configure
 * @destid: target RapidIO device destination ID
 * @data: RIO specific data descriptor
 * @direction: DMA data transfer direction (TO or FROM the device)
 * @flags: dmaengine defined flags
 *
 * Initializes RapidIO capable DMA channel for the specified data transfer.
 * Uses DMA channel private extension to pass information related to remote
 * target RIO device.
 *
 * Returns: pointer to DMA transaction descriptor if successful,
 *          error-valued pointer or NULL if failed.
 */
struct dma_async_tx_descriptor *rio_dma_prep_xfer(struct dma_chan *dchan,
	u16 destid, struct rio_dma_data *data,
	enum dma_transfer_direction direction, unsigned long flags)
{
	struct rio_dma_ext rio_ext;

	if (!dchan->device->device_prep_slave_sg) {
		pr_err("%s: prep_rio_sg == NULL\n", __func__);
		return NULL;
	}

	rio_ext.destid = destid;
	rio_ext.rio_addr_u = data->rio_addr_u;
	rio_ext.rio_addr = data->rio_addr;
	rio_ext.wr_type = data->wr_type;

	return dmaengine_prep_rio_sg(dchan, data->sg, data->sg_len,
				     direction, flags, &rio_ext);
}
EXPORT_SYMBOL_GPL(rio_dma_prep_xfer);

/**
 * rio_dma_prep_slave_sg - RapidIO specific wrapper
 *   for device_prep_slave_sg callback defined by DMAENGINE.
 * @rdev: RIO device control structure
 * @dchan: DMA channel to configure
 * @data: RIO specific data descriptor
 * @direction: DMA data transfer direction (TO or FROM the device)
 * @flags: dmaengine defined flags
 *
 * Initializes RapidIO capable DMA channel for the specified data transfer.
 * Uses DMA channel private extension to pass information related to remote
 * target RIO device.
 *
 * Returns: pointer to DMA transaction descriptor if successful,
 *          error-valued pointer or NULL if failed.
 */
struct dma_async_tx_descriptor *rio_dma_prep_slave_sg(struct rio_dev *rdev,
	struct dma_chan *dchan, struct rio_dma_data *data,
	enum dma_transfer_direction direction, unsigned long flags)
{
	return rio_dma_prep_xfer(dchan,	rdev->destid, data, direction, flags);
}
EXPORT_SYMBOL_GPL(rio_dma_prep_slave_sg);

#endif /* CONFIG_RAPIDIO_DMA_ENGINE */

/**
 * rio_find_mport - find RIO mport by its ID
 * @mport_id: number (ID) of mport device
 *
 * Given a RIO mport number, the desired mport is located
 * in the global list of mports. If the mport is found, a pointer to its
 * data structure is returned.  If no mport is found, %NULL is returned.
 */
struct rio_mport *rio_find_mport(int mport_id)
{
	struct rio_mport *port;

	mutex_lock(&rio_mport_list_lock);
	list_for_each_entry(port, &rio_mports, node) {
		if (port->id == mport_id)
			goto found;
	}
	port = NULL;
found:
	mutex_unlock(&rio_mport_list_lock);

	return port;
}

/**
 * rio_register_scan - enumeration/discovery method registration interface
 * @mport_id: mport device ID for which fabric scan routine has to be set
 *            (RIO_MPORT_ANY = set for all available mports)
 * @scan_ops: enumeration/discovery operations structure
 *
 * Registers enumeration/discovery operations with RapidIO subsystem and
 * attaches it to the specified mport device (or all available mports
 * if RIO_MPORT_ANY is specified).
 *
 * Returns error if the mport already has an enumerator attached to it.
 * In case of RIO_MPORT_ANY skips mports with valid scan routines (no error).
 */
int rio_register_scan(int mport_id, struct rio_scan *scan_ops)
{
	struct rio_mport *port;
	struct rio_scan_node *scan;
	int rc = 0;

	pr_debug("RIO: %s for mport_id=%d\n", __func__, mport_id);

	if ((mport_id != RIO_MPORT_ANY && mport_id >= RIO_MAX_MPORTS) ||
	    !scan_ops)
		return -EINVAL;

	mutex_lock(&rio_mport_list_lock);

	/*
	 * Check if there is another enumerator already registered for
	 * the same mport ID (including RIO_MPORT_ANY). Multiple enumerators
	 * for the same mport ID are not supported.
	 */
	list_for_each_entry(scan, &rio_scans, node) {
		if (scan->mport_id == mport_id) {
			rc = -EBUSY;
			goto err_out;
		}
	}

	/*
	 * Allocate and initialize new scan registration node.
	 */
	scan = kzalloc(sizeof(*scan), GFP_KERNEL);
	if (!scan) {
		rc = -ENOMEM;
		goto err_out;
	}

	scan->mport_id = mport_id;
	scan->ops = scan_ops;

	/*
	 * Traverse the list of registered mports to attach this new scan.
	 *
	 * The new scan with matching mport ID overrides any previously attached
	 * scan assuming that old scan (if any) is the default one (based on the
	 * enumerator registration check above).
	 * If the new scan is the global one, it will be attached only to mports
	 * that do not have their own individual operations already attached.
	 */
	list_for_each_entry(port, &rio_mports, node) {
		if (port->id == mport_id) {
			port->nscan = scan_ops;
			break;
		} else if (mport_id == RIO_MPORT_ANY && !port->nscan)
			port->nscan = scan_ops;
	}

	list_add_tail(&scan->node, &rio_scans);

err_out:
	mutex_unlock(&rio_mport_list_lock);

	return rc;
}
EXPORT_SYMBOL_GPL(rio_register_scan);

/**
 * rio_unregister_scan - removes enumeration/discovery method from mport
 * @mport_id: mport device ID for which fabric scan routine has to be
 *            unregistered (RIO_MPORT_ANY = apply to all mports that use
 *            the specified scan_ops)
 * @scan_ops: enumeration/discovery operations structure
 *
 * Removes enumeration or discovery method assigned to the specified mport
 * device. If RIO_MPORT_ANY is specified, removes the specified operations from
 * all mports that have them attached.
 */
int rio_unregister_scan(int mport_id, struct rio_scan *scan_ops)
{
	struct rio_mport *port;
	struct rio_scan_node *scan;

	pr_debug("RIO: %s for mport_id=%d\n", __func__, mport_id);

	if (mport_id != RIO_MPORT_ANY && mport_id >= RIO_MAX_MPORTS)
		return -EINVAL;

	mutex_lock(&rio_mport_list_lock);

	list_for_each_entry(port, &rio_mports, node)
		if (port->id == mport_id ||
		    (mport_id == RIO_MPORT_ANY && port->nscan == scan_ops))
			port->nscan = NULL;

	list_for_each_entry(scan, &rio_scans, node) {
		if (scan->mport_id == mport_id) {
			list_del(&scan->node);
			kfree(scan);
			break;
		}
	}

	mutex_unlock(&rio_mport_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(rio_unregister_scan);

/**
 * rio_mport_scan - execute enumeration/discovery on the specified mport
 * @mport_id: number (ID) of mport device
 */
int rio_mport_scan(int mport_id)
{
	struct rio_mport *port = NULL;
	int rc;

	mutex_lock(&rio_mport_list_lock);
	list_for_each_entry(port, &rio_mports, node) {
		if (port->id == mport_id)
			goto found;
	}
	mutex_unlock(&rio_mport_list_lock);
	return -ENODEV;
found:
	if (!port->nscan) {
		mutex_unlock(&rio_mport_list_lock);
		return -EINVAL;
	}

	if (!try_module_get(port->nscan->owner)) {
		mutex_unlock(&rio_mport_list_lock);
		return -ENODEV;
	}

	mutex_unlock(&rio_mport_list_lock);

	if (port->host_deviceid >= 0)
		rc = port->nscan->enumerate(port, 0);
	else
		rc = port->nscan->discover(port, RIO_SCAN_ENUM_NO_WAIT);

	module_put(port->nscan->owner);
	return rc;
}

static void rio_fixup_device(struct rio_dev *dev)
{
}

static int rio_init(void)
{
	struct rio_dev *dev = NULL;

	while ((dev = rio_get_device(RIO_ANY_ID, RIO_ANY_ID, dev)) != NULL) {
		rio_fixup_device(dev);
	}
	return 0;
}

static struct workqueue_struct *rio_wq;

struct rio_disc_work {
	struct work_struct	work;
	struct rio_mport	*mport;
};

static void disc_work_handler(struct work_struct *_work)
{
	struct rio_disc_work *work;

	work = container_of(_work, struct rio_disc_work, work);
	pr_debug("RIO: discovery work for mport %d %s\n",
		 work->mport->id, work->mport->name);
	if (try_module_get(work->mport->nscan->owner)) {
		work->mport->nscan->discover(work->mport, 0);
		module_put(work->mport->nscan->owner);
	}
}

int rio_init_mports(void)
{
	struct rio_mport *port;
	struct rio_disc_work *work;
	int n = 0;

	if (!next_portid)
		return -ENODEV;

	/*
	 * First, run enumerations and check if we need to perform discovery
	 * on any of the registered mports.
	 */
	mutex_lock(&rio_mport_list_lock);
	list_for_each_entry(port, &rio_mports, node) {
		if (port->host_deviceid >= 0) {
			if (port->nscan && try_module_get(port->nscan->owner)) {
				port->nscan->enumerate(port, 0);
				module_put(port->nscan->owner);
			}
		} else
			n++;
	}
	mutex_unlock(&rio_mport_list_lock);

	if (!n)
		goto no_disc;

	/*
	 * If we have mports that require discovery schedule a discovery work
	 * for each of them. If the code below fails to allocate needed
	 * resources, exit without error to keep results of enumeration
	 * process (if any).
	 * TODO: Implement restart of discovery process for all or
	 * individual discovering mports.
	 */
	rio_wq = alloc_workqueue("riodisc", 0, 0);
	if (!rio_wq) {
		pr_err("RIO: unable allocate rio_wq\n");
		goto no_disc;
	}

	work = kcalloc(n, sizeof *work, GFP_KERNEL);
	if (!work) {
		destroy_workqueue(rio_wq);
		goto no_disc;
	}

	n = 0;
	mutex_lock(&rio_mport_list_lock);
	list_for_each_entry(port, &rio_mports, node) {
		if (port->host_deviceid < 0 && port->nscan) {
			work[n].mport = port;
			INIT_WORK(&work[n].work, disc_work_handler);
			queue_work(rio_wq, &work[n].work);
			n++;
		}
	}

	flush_workqueue(rio_wq);
	mutex_unlock(&rio_mport_list_lock);
	pr_debug("RIO: destroy discovery workqueue\n");
	destroy_workqueue(rio_wq);
	kfree(work);

no_disc:
	rio_init();

	return 0;
}
EXPORT_SYMBOL_GPL(rio_init_mports);

static int rio_get_hdid(int index)
{
	if (ids_num == 0 || ids_num <= index || index >= RIO_MAX_MPORTS)
		return -1;

	return hdid[index];
}

int rio_mport_initialize(struct rio_mport *mport)
{
	if (next_portid >= RIO_MAX_MPORTS) {
		pr_err("RIO: reached specified max number of mports\n");
		return -ENODEV;
	}

	atomic_set(&mport->state, RIO_DEVICE_INITIALIZING);
	mport->id = next_portid++;
	mport->host_deviceid = rio_get_hdid(mport->id);
	mport->nscan = NULL;
	mutex_init(&mport->lock);
	mport->pwe_refcnt = 0;
	INIT_LIST_HEAD(&mport->pwrites);

	return 0;
}
EXPORT_SYMBOL_GPL(rio_mport_initialize);

int rio_register_mport(struct rio_mport *port)
{
	struct rio_scan_node *scan = NULL;
	int res = 0;

	mutex_lock(&rio_mport_list_lock);

	/*
	 * Check if there are any registered enumeration/discovery operations
	 * that have to be attached to the added mport.
	 */
	list_for_each_entry(scan, &rio_scans, node) {
		if (port->id == scan->mport_id ||
		    scan->mport_id == RIO_MPORT_ANY) {
			port->nscan = scan->ops;
			if (port->id == scan->mport_id)
				break;
		}
	}

	list_add_tail(&port->node, &rio_mports);
	mutex_unlock(&rio_mport_list_lock);

	dev_set_name(&port->dev, "rapidio%d", port->id);
	port->dev.class = &rio_mport_class;
	atomic_set(&port->state, RIO_DEVICE_RUNNING);

	res = device_register(&port->dev);
	if (res)
		dev_err(&port->dev, "RIO: mport%d registration failed ERR=%d\n",
			port->id, res);
	else
		dev_dbg(&port->dev, "RIO: registered mport%d\n", port->id);

	return res;
}
EXPORT_SYMBOL_GPL(rio_register_mport);

static int rio_mport_cleanup_callback(struct device *dev, void *data)
{
	struct rio_dev *rdev = to_rio_dev(dev);

	if (dev->bus == &rio_bus_type)
		rio_del_device(rdev, RIO_DEVICE_SHUTDOWN);
	return 0;
}

static int rio_net_remove_children(struct rio_net *net)
{
	/*
	 * Unregister all RapidIO devices residing on this net (this will
	 * invoke notification of registered subsystem interfaces as well).
	 */
	device_for_each_child(&net->dev, NULL, rio_mport_cleanup_callback);
	return 0;
}

int rio_unregister_mport(struct rio_mport *port)
{
	pr_debug("RIO: %s %s id=%d\n", __func__, port->name, port->id);

	/* Transition mport to the SHUTDOWN state */
	if (atomic_cmpxchg(&port->state,
			   RIO_DEVICE_RUNNING,
			   RIO_DEVICE_SHUTDOWN) != RIO_DEVICE_RUNNING) {
		pr_err("RIO: %s unexpected state transition for mport %s\n",
			__func__, port->name);
	}

	if (port->net && port->net->hport == port) {
		rio_net_remove_children(port->net);
		rio_free_net(port->net);
	}

	/*
	 * Unregister all RapidIO devices attached to this mport (this will
	 * invoke notification of registered subsystem interfaces as well).
	 */
	mutex_lock(&rio_mport_list_lock);
	list_del(&port->node);
	mutex_unlock(&rio_mport_list_lock);
	device_unregister(&port->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(rio_unregister_mport);
