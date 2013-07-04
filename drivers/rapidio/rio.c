/*
 * RapidIO interconnect services
 * (RapidIO Interconnect Specification, http://www.rapidio.org)
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright 2009 Integrated Device Technology, Inc.
 * Alex Bounine <alexandre.bounine@idt.com>
 * - Added Port-Write/Error Management initialization and handling
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

static LIST_HEAD(rio_devices);
static DEFINE_SPINLOCK(rio_global_list_lock);

static LIST_HEAD(rio_mports);
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

	err = device_add(&rdev->dev);
	if (err)
		return err;

	spin_lock(&rio_global_list_lock);
	list_add_tail(&rdev->global_list, &rio_devices);
	spin_unlock(&rio_global_list_lock);

	rio_create_sysfs_dev_files(rdev);

	return 0;
}
EXPORT_SYMBOL_GPL(rio_add_device);

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

	if (mport->ops->open_inb_mbox == NULL)
		goto out;

	res = kmalloc(sizeof(struct resource), GFP_KERNEL);

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

		rc = mport->ops->open_inb_mbox(mport, dev_id, mbox, entries);
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
	if (mport->ops->close_inb_mbox) {
		mport->ops->close_inb_mbox(mport, mbox);

		/* Release the mailbox resource */
		return release_resource(mport->inb_msg[mbox].res);
	} else
		return -ENOSYS;
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
	int rc = -ENOSYS;
	struct resource *res;

	if (mport->ops->open_outb_mbox == NULL)
		goto out;

	res = kmalloc(sizeof(struct resource), GFP_KERNEL);

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

		rc = mport->ops->open_outb_mbox(mport, dev_id, mbox, entries);
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
	if (mport->ops->close_outb_mbox) {
		mport->ops->close_outb_mbox(mport, mbox);

		/* Release the mailbox resource */
		return release_resource(mport->outb_msg[mbox].res);
	} else
		return -ENOSYS;
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
 * rio_request_inb_pwrite - request inbound port-write message service
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
	if (rdev->pwcback != NULL)
		rc = -ENOMEM;
	else
		rdev->pwcback = pwcback;

	spin_unlock(&rio_global_list_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(rio_request_inb_pwrite);

/**
 * rio_release_inb_pwrite - release inbound port-write message service
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
	int rc = 0;
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
 * rio_mport_get_physefb - Helper function that returns register offset
 *                      for Physical Layer Extended Features Block.
 * @port: Master port to issue transaction
 * @local: Indicate a local master port or remote device access
 * @destid: Destination ID of the device
 * @hopcount: Number of switch hops to the device
 */
u32
rio_mport_get_physefb(struct rio_mport *port, int local,
		      u16 destid, u8 hopcount)
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

		case RIO_EFB_SER_EP_ID_V13P:
		case RIO_EFB_SER_EP_REC_ID_V13P:
		case RIO_EFB_SER_EP_FREE_ID_V13P:
		case RIO_EFB_SER_EP_ID:
		case RIO_EFB_SER_EP_REC_ID:
		case RIO_EFB_SER_EP_FREE_ID:
		case RIO_EFB_SER_EP_FREC_ID:

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
				 rdev->phys_efptr + RIO_PORT_N_CTL_CSR(pnum),
				 &regval);
	if (lock)
		regval |= RIO_PORT_N_CTL_LOCKOUT;
	else
		regval &= ~RIO_PORT_N_CTL_LOCKOUT;

	rio_write_config_32(rdev,
				  rdev->phys_efptr + RIO_PORT_N_CTL_CSR(pnum),
				  regval);
	return 0;
}
EXPORT_SYMBOL_GPL(rio_set_port_lockout);

/**
 * rio_switch_init - Sets switch operations for a particular vendor switch
 * @rdev: RIO device
 * @do_enum: Enumeration/Discovery mode flag
 *
 * Searches the RIO switch ops table for known switch types. If the vid
 * and did match a switch table entry, then call switch initialization
 * routine to setup switch-specific routines.
 */
void rio_switch_init(struct rio_dev *rdev, int do_enum)
{
	struct rio_switch_ops *cur = __start_rio_switch_ops;
	struct rio_switch_ops *end = __end_rio_switch_ops;

	while (cur < end) {
		if ((cur->vid == rdev->vid) && (cur->did == rdev->did)) {
			pr_debug("RIO: calling init routine for %s\n",
				 rio_name(rdev));
			cur->init_hook(rdev, do_enum);
			break;
		}
		cur++;
	}

	if ((cur >= end) && (rdev->pef & RIO_PEF_STD_RT)) {
		pr_debug("RIO: adding STD routing ops for %s\n",
			rio_name(rdev));
		rdev->rswitch->add_entry = rio_std_route_add_entry;
		rdev->rswitch->get_entry = rio_std_route_get_entry;
		rdev->rswitch->clr_table = rio_std_route_clr_table;
	}

	if (!rdev->rswitch->add_entry || !rdev->rswitch->get_entry)
		printk(KERN_ERR "RIO: missing routing ops for %s\n",
		       rio_name(rdev));
}
EXPORT_SYMBOL_GPL(rio_switch_init);

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

	/*
	* enable rx input tx output port
	*/
	pr_debug("rio_enable_rx_tx_port(local = %d, destid = %d, hopcount = "
		 "%d, port_num = %d)\n", local, destid, hopcount, port_num);

	ext_ftr_ptr = rio_mport_get_physefb(port, local, destid, hopcount);

	if (local) {
		rio_local_read_config_32(port, ext_ftr_ptr +
				RIO_PORT_N_CTL_CSR(0),
				&regval);
	} else {
		if (rio_mport_read_config_32(port, destid, hopcount,
		ext_ftr_ptr + RIO_PORT_N_CTL_CSR(port_num), &regval) < 0)
			return -EIO;
	}

	if (regval & RIO_PORT_N_CTL_P_TYP_SER) {
		/* serial */
		regval = regval | RIO_PORT_N_CTL_EN_RX_SER
				| RIO_PORT_N_CTL_EN_TX_SER;
	} else {
		/* parallel */
		regval = regval | RIO_PORT_N_CTL_EN_RX_PAR
				| RIO_PORT_N_CTL_EN_TX_PAR;
	}

	if (local) {
		rio_local_write_config_32(port, ext_ftr_ptr +
					  RIO_PORT_N_CTL_CSR(0), regval);
	} else {
		if (rio_mport_write_config_32(port, destid, hopcount,
		    ext_ftr_ptr + RIO_PORT_N_CTL_CSR(port_num), regval) < 0)
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

	if (prev == NULL)
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
			rdev->phys_efptr + RIO_PORT_N_MNT_RSP_CSR(pnum),
			&regval);
		udelay(50);
	}

	/* Issue Input-status command */
	rio_write_config_32(rdev,
		rdev->phys_efptr + RIO_PORT_N_MNT_REQ_CSR(pnum),
		RIO_MNT_REQ_CMD_IS);

	/* Exit if the response is not expected */
	if (lnkresp == NULL)
		return 0;

	checkcount = 3;
	while (checkcount--) {
		udelay(50);
		rio_read_config_32(rdev,
			rdev->phys_efptr + RIO_PORT_N_MNT_RSP_CSR(pnum),
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
 */
static int rio_clr_err_stopped(struct rio_dev *rdev, u32 pnum, u32 err_status)
{
	struct rio_dev *nextdev = rdev->rswitch->nextdev[pnum];
	u32 regval;
	u32 far_ackid, far_linkstat, near_ackid;

	if (err_status == 0)
		rio_read_config_32(rdev,
			rdev->phys_efptr + RIO_PORT_N_ERR_STS_CSR(pnum),
			&err_status);

	if (err_status & RIO_PORT_N_ERR_STS_PW_OUT_ES) {
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
			rdev->phys_efptr + RIO_PORT_N_ACK_STS_CSR(pnum),
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
				rdev->phys_efptr + RIO_PORT_N_ACK_STS_CSR(pnum),
				(near_ackid << 24) |
					(far_ackid << 8) | far_ackid);
			/* Align far outstanding/outbound ackIDs with
			 * near inbound.
			 */
			far_ackid++;
			if (nextdev)
				rio_write_config_32(nextdev,
					nextdev->phys_efptr +
					RIO_PORT_N_ACK_STS_CSR(RIO_GET_PORT_NUM(nextdev->swpinfo)),
					(far_ackid << 24) |
					(near_ackid << 8) | near_ackid);
			else
				pr_debug("RIO_EM: Invalid nextdev pointer (NULL)\n");
		}
rd_err:
		rio_read_config_32(rdev,
			rdev->phys_efptr + RIO_PORT_N_ERR_STS_CSR(pnum),
			&err_status);
		pr_debug("RIO_EM: SP%d_ERR_STS_CSR=0x%08x\n", pnum, err_status);
	}

	if ((err_status & RIO_PORT_N_ERR_STS_PW_INP_ES) && nextdev) {
		pr_debug("RIO_EM: servicing Input Error-Stopped state\n");
		rio_get_input_status(nextdev,
				     RIO_GET_PORT_NUM(nextdev->swpinfo), NULL);
		udelay(50);

		rio_read_config_32(rdev,
			rdev->phys_efptr + RIO_PORT_N_ERR_STS_CSR(pnum),
			&err_status);
		pr_debug("RIO_EM: SP%d_ERR_STS_CSR=0x%08x\n", pnum, err_status);
	}

	return (err_status & (RIO_PORT_N_ERR_STS_PW_OUT_ES |
			      RIO_PORT_N_ERR_STS_PW_INP_ES)) ? 1 : 0;
}

/**
 * rio_inb_pwrite_handler - process inbound port-write message
 * @pw_msg: pointer to inbound port-write message
 *
 * Processes an inbound port-write message. Returns 0 if the request
 * has been satisfied.
 */
int rio_inb_pwrite_handler(union rio_pw_msg *pw_msg)
{
	struct rio_dev *rdev;
	u32 err_status, em_perrdet, em_ltlerrdet;
	int rc, portnum;

	rdev = rio_get_comptag((pw_msg->em.comptag & RIO_CTAG_UDEVID), NULL);
	if (rdev == NULL) {
		/* Device removed or enumeration error */
		pr_debug("RIO: %s No matching device for CTag 0x%08x\n",
			__func__, pw_msg->em.comptag);
		return -EIO;
	}

	pr_debug("RIO: Port-Write message from %s\n", rio_name(rdev));

#ifdef DEBUG_PW
	{
	u32 i;
	for (i = 0; i < RIO_PW_MSG_SIZE/sizeof(u32);) {
			pr_debug("0x%02x: %08x %08x %08x %08x\n",
				 i*4, pw_msg->raw[i], pw_msg->raw[i + 1],
				 pw_msg->raw[i + 2], pw_msg->raw[i + 3]);
			i += 4;
	}
	}
#endif

	/* Call an external service function (if such is registered
	 * for this device). This may be the service for endpoints that send
	 * device-specific port-write messages. End-point messages expected
	 * to be handled completely by EP specific device driver.
	 * For switches rc==0 signals that no standard processing required.
	 */
	if (rdev->pwcback != NULL) {
		rc = rdev->pwcback(rdev, pw_msg, 0);
		if (rc == 0)
			return 0;
	}

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
	if (rdev->rswitch->em_handle)
		rdev->rswitch->em_handle(rdev, portnum);

	rio_read_config_32(rdev,
			rdev->phys_efptr + RIO_PORT_N_ERR_STS_CSR(portnum),
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
		if (err_status & (RIO_PORT_N_ERR_STS_PW_OUT_ES |
				  RIO_PORT_N_ERR_STS_PW_INP_ES)) {
			if (rio_clr_err_stopped(rdev, portnum, err_status))
				rio_clr_err_stopped(rdev, portnum, 0);
		}
	}  else { /* if (err_status & RIO_PORT_N_ERR_STS_PORT_UNINIT) */

		if (rdev->rswitch->port_ok & (1 << portnum)) {
			rdev->rswitch->port_ok &= ~(1 << portnum);
			rio_set_port_lockout(rdev, portnum, 1);

			rio_write_config_32(rdev,
				rdev->phys_efptr +
					RIO_PORT_N_ACK_STS_CSR(portnum),
				RIO_PORT_N_ACK_CLEAR);

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
	rio_write_config_32(rdev,
			rdev->phys_efptr + RIO_PORT_N_ERR_STS_CSR(portnum),
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
int rio_std_route_add_entry(struct rio_mport *mport, u16 destid, u8 hopcount,
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
int rio_std_route_get_entry(struct rio_mport *mport, u16 destid, u8 hopcount,
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
int rio_std_route_clr_table(struct rio_mport *mport, u16 destid, u8 hopcount,
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

#ifdef CONFIG_RAPIDIO_DMA_ENGINE

static bool rio_chan_filter(struct dma_chan *chan, void *arg)
{
	struct rio_dev *rdev = arg;

	/* Check that DMA device belongs to the right MPORT */
	return (rdev->net->hport ==
		container_of(chan->device, struct rio_mport, dma));
}

/**
 * rio_request_dma - request RapidIO capable DMA channel that supports
 *   specified target RapidIO device.
 * @rdev: RIO device control structure
 *
 * Returns pointer to allocated DMA channel or NULL if failed.
 */
struct dma_chan *rio_request_dma(struct rio_dev *rdev)
{
	dma_cap_mask_t mask;
	struct dma_chan *dchan;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dchan = dma_request_channel(mask, rio_chan_filter, rdev);

	return dchan;
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
 * Returns pointer to DMA transaction descriptor or NULL if failed.
 */
struct dma_async_tx_descriptor *rio_dma_prep_slave_sg(struct rio_dev *rdev,
	struct dma_chan *dchan, struct rio_dma_data *data,
	enum dma_transfer_direction direction, unsigned long flags)
{
	struct dma_async_tx_descriptor *txd = NULL;
	struct rio_dma_ext rio_ext;

	if (dchan->device->device_prep_slave_sg == NULL) {
		pr_err("%s: prep_rio_sg == NULL\n", __func__);
		return NULL;
	}

	rio_ext.destid = rdev->destid;
	rio_ext.rio_addr_u = data->rio_addr_u;
	rio_ext.rio_addr = data->rio_addr;
	rio_ext.wr_type = data->wr_type;

	txd = dmaengine_prep_rio_sg(dchan, data->sg, data->sg_len,
					direction, flags, &rio_ext);

	return txd;
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
 * @scan_ops: enumeration/discovery control structure
 *
 * Assigns enumeration or discovery method to the specified mport device (or all
 * available mports if RIO_MPORT_ANY is specified).
 * Returns error if the mport already has an enumerator attached to it.
 * In case of RIO_MPORT_ANY ignores ports with valid scan routines and returns
 * an error if was unable to find at least one available mport.
 */
int rio_register_scan(int mport_id, struct rio_scan *scan_ops)
{
	struct rio_mport *port;
	int rc = -EBUSY;

	mutex_lock(&rio_mport_list_lock);
	list_for_each_entry(port, &rio_mports, node) {
		if (port->id == mport_id || mport_id == RIO_MPORT_ANY) {
			if (port->nscan && mport_id == RIO_MPORT_ANY)
				continue;
			else if (port->nscan)
				break;

			port->nscan = scan_ops;
			rc = 0;

			if (mport_id != RIO_MPORT_ANY)
				break;
		}
	}
	mutex_unlock(&rio_mport_list_lock);

	return rc;
}
EXPORT_SYMBOL_GPL(rio_register_scan);

/**
 * rio_unregister_scan - removes enumeration/discovery method from mport
 * @mport_id: mport device ID for which fabric scan routine has to be
 *            unregistered (RIO_MPORT_ANY = set for all available mports)
 *
 * Removes enumeration or discovery method assigned to the specified mport
 * device (or all available mports if RIO_MPORT_ANY is specified).
 */
int rio_unregister_scan(int mport_id)
{
	struct rio_mport *port;

	mutex_lock(&rio_mport_list_lock);
	list_for_each_entry(port, &rio_mports, node) {
		if (port->id == mport_id || mport_id == RIO_MPORT_ANY) {
			if (port->nscan)
				port->nscan = NULL;
			if (mport_id != RIO_MPORT_ANY)
				break;
		}
	}
	mutex_unlock(&rio_mport_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(rio_unregister_scan);

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
	work->mport->nscan->discover(work->mport, 0);
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
			if (port->nscan)
				port->nscan->enumerate(port, 0);
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
	 * TODO: Implement restart of dicovery process for all or
	 * individual discovering mports.
	 */
	rio_wq = alloc_workqueue("riodisc", 0, 0);
	if (!rio_wq) {
		pr_err("RIO: unable allocate rio_wq\n");
		goto no_disc;
	}

	work = kcalloc(n, sizeof *work, GFP_KERNEL);
	if (!work) {
		pr_err("RIO: no memory for work struct\n");
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
	mutex_unlock(&rio_mport_list_lock);

	flush_workqueue(rio_wq);
	pr_debug("RIO: destroy discovery workqueue\n");
	destroy_workqueue(rio_wq);
	kfree(work);

no_disc:
	rio_init();

	return 0;
}

static int hdids[RIO_MAX_MPORTS + 1];

static int rio_get_hdid(int index)
{
	if (!hdids[0] || hdids[0] <= index || index >= RIO_MAX_MPORTS)
		return -1;

	return hdids[index + 1];
}

static int rio_hdid_setup(char *str)
{
	(void)get_options(str, ARRAY_SIZE(hdids), hdids);
	return 1;
}

__setup("riohdid=", rio_hdid_setup);

int rio_register_mport(struct rio_mport *port)
{
	if (next_portid >= RIO_MAX_MPORTS) {
		pr_err("RIO: reached specified max number of mports\n");
		return 1;
	}

	port->id = next_portid++;
	port->host_deviceid = rio_get_hdid(port->id);
	port->nscan = NULL;
	mutex_lock(&rio_mport_list_lock);
	list_add_tail(&port->node, &rio_mports);
	mutex_unlock(&rio_mport_list_lock);
	return 0;
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
EXPORT_SYMBOL_GPL(rio_init_mports);
