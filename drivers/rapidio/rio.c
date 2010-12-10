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

/**
 * rio_set_port_lockout - Sets/clears LOCKOUT bit (RIO EM 1.3) for a switch port.
 * @rdev: Pointer to RIO device control structure
 * @pnum: Switch port number to set LOCKOUT bit
 * @lock: Operation : set (=1) or clear (=0)
 */
int rio_set_port_lockout(struct rio_dev *rdev, u32 pnum, int lock)
{
	u8 hopcount = 0xff;
	u16 destid = rdev->destid;
	u32 regval;

	if (rdev->rswitch) {
		destid = rdev->rswitch->destid;
		hopcount = rdev->rswitch->hopcount;
	}

	rio_mport_read_config_32(rdev->net->hport, destid, hopcount,
				 rdev->phys_efptr + RIO_PORT_N_CTL_CSR(pnum),
				 &regval);
	if (lock)
		regval |= RIO_PORT_N_CTL_LOCKOUT;
	else
		regval &= ~RIO_PORT_N_CTL_LOCKOUT;

	rio_mport_write_config_32(rdev->net->hport, destid, hopcount,
				  rdev->phys_efptr + RIO_PORT_N_CTL_CSR(pnum),
				  regval);
	return 0;
}

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
	int p_port, dstid, rc = -EIO;
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

	dstid = (rdev->pef & RIO_PEF_SWITCH) ?
			rdev->rswitch->destid : rdev->destid;
	p_port = prev->rswitch->route_table[dstid];

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

/**
 * rio_chk_dev_access - Validate access to the specified device.
 * @rdev: Pointer to RIO device control structure
 */
static int rio_chk_dev_access(struct rio_dev *rdev)
{
	u8 hopcount = 0xff;
	u16 destid = rdev->destid;

	if (rdev->rswitch) {
		destid = rdev->rswitch->destid;
		hopcount = rdev->rswitch->hopcount;
	}

	return rio_mport_chk_dev_access(rdev->net->hport, destid, hopcount);
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
	struct rio_mport *mport = rdev->net->hport;
	u16 destid = rdev->rswitch->destid;
	u8 hopcount = rdev->rswitch->hopcount;
	u32 regval;
	int checkcount;

	if (lnkresp) {
		/* Read from link maintenance response register
		 * to clear valid bit */
		rio_mport_read_config_32(mport, destid, hopcount,
			rdev->phys_efptr + RIO_PORT_N_MNT_RSP_CSR(pnum),
			&regval);
		udelay(50);
	}

	/* Issue Input-status command */
	rio_mport_write_config_32(mport, destid, hopcount,
		rdev->phys_efptr + RIO_PORT_N_MNT_REQ_CSR(pnum),
		RIO_MNT_REQ_CMD_IS);

	/* Exit if the response is not expected */
	if (lnkresp == NULL)
		return 0;

	checkcount = 3;
	while (checkcount--) {
		udelay(50);
		rio_mport_read_config_32(mport, destid, hopcount,
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
	struct rio_mport *mport = rdev->net->hport;
	u16 destid = rdev->rswitch->destid;
	u8 hopcount = rdev->rswitch->hopcount;
	struct rio_dev *nextdev = rdev->rswitch->nextdev[pnum];
	u32 regval;
	u32 far_ackid, far_linkstat, near_ackid;

	if (err_status == 0)
		rio_mport_read_config_32(mport, destid, hopcount,
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
		rio_mport_read_config_32(mport, destid, hopcount,
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
			rio_mport_write_config_32(mport, destid,
				hopcount, rdev->phys_efptr +
					RIO_PORT_N_ACK_STS_CSR(pnum),
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
		rio_mport_read_config_32(mport, destid, hopcount,
			rdev->phys_efptr + RIO_PORT_N_ERR_STS_CSR(pnum),
			&err_status);
		pr_debug("RIO_EM: SP%d_ERR_STS_CSR=0x%08x\n", pnum, err_status);
	}

	if ((err_status & RIO_PORT_N_ERR_STS_PW_INP_ES) && nextdev) {
		pr_debug("RIO_EM: servicing Input Error-Stopped state\n");
		rio_get_input_status(nextdev,
				     RIO_GET_PORT_NUM(nextdev->swpinfo), NULL);
		udelay(50);

		rio_mport_read_config_32(mport, destid, hopcount,
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
	struct rio_mport *mport;
	u8 hopcount;
	u16 destid;
	u32 err_status, em_perrdet, em_ltlerrdet;
	int rc, portnum;

	rdev = rio_get_comptag(pw_msg->em.comptag, NULL);
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

	mport = rdev->net->hport;
	destid = rdev->rswitch->destid;
	hopcount = rdev->rswitch->hopcount;

	/*
	 * Process the port-write notification from switch
	 */
	if (rdev->rswitch->em_handle)
		rdev->rswitch->em_handle(rdev, portnum);

	rio_mport_read_config_32(mport, destid, hopcount,
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

			rio_mport_write_config_32(mport, destid, hopcount,
				rdev->phys_efptr +
					RIO_PORT_N_ACK_STS_CSR(portnum),
				RIO_PORT_N_ACK_CLEAR);

			/* Schedule Extraction Service */
			pr_debug("RIO_PW: Device Extraction on [%s]-P%d\n",
			       rio_name(rdev), portnum);
		}
	}

	rio_mport_read_config_32(mport, destid, hopcount,
		rdev->em_efptr + RIO_EM_PN_ERR_DETECT(portnum), &em_perrdet);
	if (em_perrdet) {
		pr_debug("RIO_PW: RIO_EM_P%d_ERR_DETECT=0x%08x\n",
			 portnum, em_perrdet);
		/* Clear EM Port N Error Detect CSR */
		rio_mport_write_config_32(mport, destid, hopcount,
			rdev->em_efptr + RIO_EM_PN_ERR_DETECT(portnum), 0);
	}

	rio_mport_read_config_32(mport, destid, hopcount,
		rdev->em_efptr + RIO_EM_LTL_ERR_DETECT, &em_ltlerrdet);
	if (em_ltlerrdet) {
		pr_debug("RIO_PW: RIO_EM_LTL_ERR_DETECT=0x%08x\n",
			 em_ltlerrdet);
		/* Clear EM L/T Layer Error Detect CSR */
		rio_mport_write_config_32(mport, destid, hopcount,
			rdev->em_efptr + RIO_EM_LTL_ERR_DETECT, 0);
	}

	/* Clear remaining error bits and Port-Write Pending bit */
	rio_mport_write_config_32(mport, destid, hopcount,
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
					resource_size(&port->iores),
					port->name)) {
			printk(KERN_ERR
			       "RIO: Error requesting master port region 0x%016llx-0x%016llx\n",
			       (u64)port->iores.start, (u64)port->iores.end);
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
