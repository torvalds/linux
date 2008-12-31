/*
 * RapidIO interconnect services
 * (RapidIO Interconnect Specification, http://www.rapidio.org)
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
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

static LIST_HEAD(rio_mports);

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
	int rc = 0;

	struct resource *res = kmalloc(sizeof(struct resource), GFP_KERNEL);

	if (res) {
		rio_init_mbox_res(res, mbox, mbox);

		/* Make sure this mailbox isn't in use */
		if ((rc =
		     request_resource(&mport->riores[RIO_INB_MBOX_RESOURCE],
				      res)) < 0) {
			kfree(res);
			goto out;
		}

		mport->inb_msg[mbox].res = res;

		/* Hook the inbound message callback */
		mport->inb_msg[mbox].mcback = minb;

		rc = rio_open_inb_mbox(mport, dev_id, mbox, entries);
	} else
		rc = -ENOMEM;

      out:
	return rc;
}

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
	rio_close_inb_mbox(mport, mbox);

	/* Release the mailbox resource */
	return release_resource(mport->inb_msg[mbox].res);
}

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
	int rc = 0;

	struct resource *res = kmalloc(sizeof(struct resource), GFP_KERNEL);

	if (res) {
		rio_init_mbox_res(res, mbox, mbox);

		/* Make sure this outbound mailbox isn't in use */
		if ((rc =
		     request_resource(&mport->riores[RIO_OUTB_MBOX_RESOURCE],
				      res)) < 0) {
			kfree(res);
			goto out;
		}

		mport->outb_msg[mbox].res = res;

		/* Hook the inbound message callback */
		mport->outb_msg[mbox].mcback = moutb;

		rc = rio_open_outb_mbox(mport, dev_id, mbox, entries);
	} else
		rc = -ENOMEM;

      out:
	return rc;
}

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
	rio_close_outb_mbox(mport, mbox);

	/* Release the mailbox resource */
	return release_resource(mport->outb_msg[mbox].res);
}

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
	int rc = 0;
	struct rio_dbell *dbell;

	if (!(dbell = kmalloc(sizeof(struct rio_dbell), GFP_KERNEL))) {
		rc = -ENOMEM;
		goto out;
	}

	dbell->res = res;
	dbell->dinb = dinb;
	dbell->dev_id = dev_id;

	list_add_tail(&dbell->node, &mport->dbells);

      out:
	return rc;
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
	int rc = 0;

	struct resource *res = kmalloc(sizeof(struct resource), GFP_KERNEL);

	if (res) {
		rio_init_dbell_res(res, start, end);

		/* Make sure these doorbells aren't in use */
		if ((rc =
		     request_resource(&mport->riores[RIO_DOORBELL_RESOURCE],
				      res)) < 0) {
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

	list_for_each_entry(dbell, &mport->dbells, node) {
		if ((dbell->res->start == start) && (dbell->res->end == end)) {
			found = 1;
			break;
		}
	}

	/* If we can't find an exact match, fail */
	if (!found) {
		rc = -EINVAL;
		goto out;
	}

	/* Delete from list */
	list_del(&dbell->node);

	/* Release the doorbell resource */
	rc = release_resource(dbell->res);

	/* Free the doorbell event */
	kfree(dbell);

      out:
	return rc;
}

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
	struct resource *res = kmalloc(sizeof(struct resource), GFP_KERNEL);

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
 * 0 in case the device does not support it.  Possible
 * values for @ftr:
 *
 * %RIO_EFB_PAR_EP_ID		LP/LVDS EP Devices
 *
 * %RIO_EFB_PAR_EP_REC_ID	LP/LVDS EP Recovery Devices
 *
 * %RIO_EFB_PAR_EP_FREE_ID	LP/LVDS EP Free Devices
 *
 * %RIO_EFB_SER_EP_ID		LP/Serial EP Devices
 *
 * %RIO_EFB_SER_EP_REC_ID	LP/Serial EP Recovery Devices
 *
 * %RIO_EFB_SER_EP_FREE_ID	LP/Serial EP Free Devices
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
		if (!(ext_ftr_ptr = RIO_GET_BLOCK_PTR(ftr_header)))
			break;
	}

	return 0;
}

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

static void rio_fixup_device(struct rio_dev *dev)
{
}

static int __devinit rio_init(void)
{
	struct rio_dev *dev = NULL;

	while ((dev = rio_get_device(RIO_ANY_ID, RIO_ANY_ID, dev)) != NULL) {
		rio_fixup_device(dev);
	}
	return 0;
}

device_initcall(rio_init);

int __devinit rio_init_mports(void)
{
	int rc = 0;
	struct rio_mport *port;

	list_for_each_entry(port, &rio_mports, node) {
		if (!request_mem_region(port->iores.start,
					port->iores.end - port->iores.start,
					port->name)) {
			printk(KERN_ERR
			       "RIO: Error requesting master port region 0x%016llx-0x%016llx\n",
			       (u64)port->iores.start, (u64)port->iores.end - 1);
			rc = -ENOMEM;
			goto out;
		}

		if (port->host_deviceid >= 0)
			rio_enum_mport(port);
		else
			rio_disc_mport(port);
	}

      out:
	return rc;
}

void rio_register_mport(struct rio_mport *port)
{
	list_add_tail(&port->node, &rio_mports);
}

EXPORT_SYMBOL_GPL(rio_local_get_device_id);
EXPORT_SYMBOL_GPL(rio_get_device);
EXPORT_SYMBOL_GPL(rio_get_asm);
EXPORT_SYMBOL_GPL(rio_request_inb_dbell);
EXPORT_SYMBOL_GPL(rio_release_inb_dbell);
EXPORT_SYMBOL_GPL(rio_request_outb_dbell);
EXPORT_SYMBOL_GPL(rio_release_outb_dbell);
EXPORT_SYMBOL_GPL(rio_request_inb_mbox);
EXPORT_SYMBOL_GPL(rio_release_inb_mbox);
EXPORT_SYMBOL_GPL(rio_request_outb_mbox);
EXPORT_SYMBOL_GPL(rio_release_outb_mbox);
