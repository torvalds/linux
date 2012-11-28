/*
 * Copyright (c) 2006 - 2009 Mellanox Technology Inc.  All rights reserved.
 * Copyright (C) 2008 - 2011 Bart Van Assche <bvanassche@acm.org>.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/kthread.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/atomic.h>
#include <scsi/scsi_tcq.h>
#include <target/configfs_macros.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric_configfs.h>
#include <target/target_core_fabric.h>
#include <target/target_core_configfs.h>
#include "ib_srpt.h"

/* Name of this kernel module. */
#define DRV_NAME		"ib_srpt"
#define DRV_VERSION		"2.0.0"
#define DRV_RELDATE		"2011-02-14"

#define SRPT_ID_STRING	"Linux SRP target"

#undef pr_fmt
#define pr_fmt(fmt) DRV_NAME " " fmt

MODULE_AUTHOR("Vu Pham and Bart Van Assche");
MODULE_DESCRIPTION("InfiniBand SCSI RDMA Protocol target "
		   "v" DRV_VERSION " (" DRV_RELDATE ")");
MODULE_LICENSE("Dual BSD/GPL");

/*
 * Global Variables
 */

static u64 srpt_service_guid;
static DEFINE_SPINLOCK(srpt_dev_lock);	/* Protects srpt_dev_list. */
static LIST_HEAD(srpt_dev_list);	/* List of srpt_device structures. */

static unsigned srp_max_req_size = DEFAULT_MAX_REQ_SIZE;
module_param(srp_max_req_size, int, 0444);
MODULE_PARM_DESC(srp_max_req_size,
		 "Maximum size of SRP request messages in bytes.");

static int srpt_srq_size = DEFAULT_SRPT_SRQ_SIZE;
module_param(srpt_srq_size, int, 0444);
MODULE_PARM_DESC(srpt_srq_size,
		 "Shared receive queue (SRQ) size.");

static int srpt_get_u64_x(char *buffer, struct kernel_param *kp)
{
	return sprintf(buffer, "0x%016llx", *(u64 *)kp->arg);
}
module_param_call(srpt_service_guid, NULL, srpt_get_u64_x, &srpt_service_guid,
		  0444);
MODULE_PARM_DESC(srpt_service_guid,
		 "Using this value for ioc_guid, id_ext, and cm_listen_id"
		 " instead of using the node_guid of the first HCA.");

static struct ib_client srpt_client;
static struct target_fabric_configfs *srpt_target;
static void srpt_release_channel(struct srpt_rdma_ch *ch);
static int srpt_queue_status(struct se_cmd *cmd);

/**
 * opposite_dma_dir() - Swap DMA_TO_DEVICE and DMA_FROM_DEVICE.
 */
static inline
enum dma_data_direction opposite_dma_dir(enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:	return DMA_FROM_DEVICE;
	case DMA_FROM_DEVICE:	return DMA_TO_DEVICE;
	default:		return dir;
	}
}

/**
 * srpt_sdev_name() - Return the name associated with the HCA.
 *
 * Examples are ib0, ib1, ...
 */
static inline const char *srpt_sdev_name(struct srpt_device *sdev)
{
	return sdev->device->name;
}

static enum rdma_ch_state srpt_get_ch_state(struct srpt_rdma_ch *ch)
{
	unsigned long flags;
	enum rdma_ch_state state;

	spin_lock_irqsave(&ch->spinlock, flags);
	state = ch->state;
	spin_unlock_irqrestore(&ch->spinlock, flags);
	return state;
}

static enum rdma_ch_state
srpt_set_ch_state(struct srpt_rdma_ch *ch, enum rdma_ch_state new_state)
{
	unsigned long flags;
	enum rdma_ch_state prev;

	spin_lock_irqsave(&ch->spinlock, flags);
	prev = ch->state;
	ch->state = new_state;
	spin_unlock_irqrestore(&ch->spinlock, flags);
	return prev;
}

/**
 * srpt_test_and_set_ch_state() - Test and set the channel state.
 *
 * Returns true if and only if the channel state has been set to the new state.
 */
static bool
srpt_test_and_set_ch_state(struct srpt_rdma_ch *ch, enum rdma_ch_state old,
			   enum rdma_ch_state new)
{
	unsigned long flags;
	enum rdma_ch_state prev;

	spin_lock_irqsave(&ch->spinlock, flags);
	prev = ch->state;
	if (prev == old)
		ch->state = new;
	spin_unlock_irqrestore(&ch->spinlock, flags);
	return prev == old;
}

/**
 * srpt_event_handler() - Asynchronous IB event callback function.
 *
 * Callback function called by the InfiniBand core when an asynchronous IB
 * event occurs. This callback may occur in interrupt context. See also
 * section 11.5.2, Set Asynchronous Event Handler in the InfiniBand
 * Architecture Specification.
 */
static void srpt_event_handler(struct ib_event_handler *handler,
			       struct ib_event *event)
{
	struct srpt_device *sdev;
	struct srpt_port *sport;

	sdev = ib_get_client_data(event->device, &srpt_client);
	if (!sdev || sdev->device != event->device)
		return;

	pr_debug("ASYNC event= %d on device= %s\n", event->event,
		 srpt_sdev_name(sdev));

	switch (event->event) {
	case IB_EVENT_PORT_ERR:
		if (event->element.port_num <= sdev->device->phys_port_cnt) {
			sport = &sdev->port[event->element.port_num - 1];
			sport->lid = 0;
			sport->sm_lid = 0;
		}
		break;
	case IB_EVENT_PORT_ACTIVE:
	case IB_EVENT_LID_CHANGE:
	case IB_EVENT_PKEY_CHANGE:
	case IB_EVENT_SM_CHANGE:
	case IB_EVENT_CLIENT_REREGISTER:
		/* Refresh port data asynchronously. */
		if (event->element.port_num <= sdev->device->phys_port_cnt) {
			sport = &sdev->port[event->element.port_num - 1];
			if (!sport->lid && !sport->sm_lid)
				schedule_work(&sport->work);
		}
		break;
	default:
		printk(KERN_ERR "received unrecognized IB event %d\n",
		       event->event);
		break;
	}
}

/**
 * srpt_srq_event() - SRQ event callback function.
 */
static void srpt_srq_event(struct ib_event *event, void *ctx)
{
	printk(KERN_INFO "SRQ event %d\n", event->event);
}

/**
 * srpt_qp_event() - QP event callback function.
 */
static void srpt_qp_event(struct ib_event *event, struct srpt_rdma_ch *ch)
{
	pr_debug("QP event %d on cm_id=%p sess_name=%s state=%d\n",
		 event->event, ch->cm_id, ch->sess_name, srpt_get_ch_state(ch));

	switch (event->event) {
	case IB_EVENT_COMM_EST:
		ib_cm_notify(ch->cm_id, event->event);
		break;
	case IB_EVENT_QP_LAST_WQE_REACHED:
		if (srpt_test_and_set_ch_state(ch, CH_DRAINING,
					       CH_RELEASING))
			srpt_release_channel(ch);
		else
			pr_debug("%s: state %d - ignored LAST_WQE.\n",
				 ch->sess_name, srpt_get_ch_state(ch));
		break;
	default:
		printk(KERN_ERR "received unrecognized IB QP event %d\n",
		       event->event);
		break;
	}
}

/**
 * srpt_set_ioc() - Helper function for initializing an IOUnitInfo structure.
 *
 * @slot: one-based slot number.
 * @value: four-bit value.
 *
 * Copies the lowest four bits of value in element slot of the array of four
 * bit elements called c_list (controller list). The index slot is one-based.
 */
static void srpt_set_ioc(u8 *c_list, u32 slot, u8 value)
{
	u16 id;
	u8 tmp;

	id = (slot - 1) / 2;
	if (slot & 0x1) {
		tmp = c_list[id] & 0xf;
		c_list[id] = (value << 4) | tmp;
	} else {
		tmp = c_list[id] & 0xf0;
		c_list[id] = (value & 0xf) | tmp;
	}
}

/**
 * srpt_get_class_port_info() - Copy ClassPortInfo to a management datagram.
 *
 * See also section 16.3.3.1 ClassPortInfo in the InfiniBand Architecture
 * Specification.
 */
static void srpt_get_class_port_info(struct ib_dm_mad *mad)
{
	struct ib_class_port_info *cif;

	cif = (struct ib_class_port_info *)mad->data;
	memset(cif, 0, sizeof *cif);
	cif->base_version = 1;
	cif->class_version = 1;
	cif->resp_time_value = 20;

	mad->mad_hdr.status = 0;
}

/**
 * srpt_get_iou() - Write IOUnitInfo to a management datagram.
 *
 * See also section 16.3.3.3 IOUnitInfo in the InfiniBand Architecture
 * Specification. See also section B.7, table B.6 in the SRP r16a document.
 */
static void srpt_get_iou(struct ib_dm_mad *mad)
{
	struct ib_dm_iou_info *ioui;
	u8 slot;
	int i;

	ioui = (struct ib_dm_iou_info *)mad->data;
	ioui->change_id = __constant_cpu_to_be16(1);
	ioui->max_controllers = 16;

	/* set present for slot 1 and empty for the rest */
	srpt_set_ioc(ioui->controller_list, 1, 1);
	for (i = 1, slot = 2; i < 16; i++, slot++)
		srpt_set_ioc(ioui->controller_list, slot, 0);

	mad->mad_hdr.status = 0;
}

/**
 * srpt_get_ioc() - Write IOControllerprofile to a management datagram.
 *
 * See also section 16.3.3.4 IOControllerProfile in the InfiniBand
 * Architecture Specification. See also section B.7, table B.7 in the SRP
 * r16a document.
 */
static void srpt_get_ioc(struct srpt_port *sport, u32 slot,
			 struct ib_dm_mad *mad)
{
	struct srpt_device *sdev = sport->sdev;
	struct ib_dm_ioc_profile *iocp;

	iocp = (struct ib_dm_ioc_profile *)mad->data;

	if (!slot || slot > 16) {
		mad->mad_hdr.status
			= __constant_cpu_to_be16(DM_MAD_STATUS_INVALID_FIELD);
		return;
	}

	if (slot > 2) {
		mad->mad_hdr.status
			= __constant_cpu_to_be16(DM_MAD_STATUS_NO_IOC);
		return;
	}

	memset(iocp, 0, sizeof *iocp);
	strcpy(iocp->id_string, SRPT_ID_STRING);
	iocp->guid = cpu_to_be64(srpt_service_guid);
	iocp->vendor_id = cpu_to_be32(sdev->dev_attr.vendor_id);
	iocp->device_id = cpu_to_be32(sdev->dev_attr.vendor_part_id);
	iocp->device_version = cpu_to_be16(sdev->dev_attr.hw_ver);
	iocp->subsys_vendor_id = cpu_to_be32(sdev->dev_attr.vendor_id);
	iocp->subsys_device_id = 0x0;
	iocp->io_class = __constant_cpu_to_be16(SRP_REV16A_IB_IO_CLASS);
	iocp->io_subclass = __constant_cpu_to_be16(SRP_IO_SUBCLASS);
	iocp->protocol = __constant_cpu_to_be16(SRP_PROTOCOL);
	iocp->protocol_version = __constant_cpu_to_be16(SRP_PROTOCOL_VERSION);
	iocp->send_queue_depth = cpu_to_be16(sdev->srq_size);
	iocp->rdma_read_depth = 4;
	iocp->send_size = cpu_to_be32(srp_max_req_size);
	iocp->rdma_size = cpu_to_be32(min(sport->port_attrib.srp_max_rdma_size,
					  1U << 24));
	iocp->num_svc_entries = 1;
	iocp->op_cap_mask = SRP_SEND_TO_IOC | SRP_SEND_FROM_IOC |
		SRP_RDMA_READ_FROM_IOC | SRP_RDMA_WRITE_FROM_IOC;

	mad->mad_hdr.status = 0;
}

/**
 * srpt_get_svc_entries() - Write ServiceEntries to a management datagram.
 *
 * See also section 16.3.3.5 ServiceEntries in the InfiniBand Architecture
 * Specification. See also section B.7, table B.8 in the SRP r16a document.
 */
static void srpt_get_svc_entries(u64 ioc_guid,
				 u16 slot, u8 hi, u8 lo, struct ib_dm_mad *mad)
{
	struct ib_dm_svc_entries *svc_entries;

	WARN_ON(!ioc_guid);

	if (!slot || slot > 16) {
		mad->mad_hdr.status
			= __constant_cpu_to_be16(DM_MAD_STATUS_INVALID_FIELD);
		return;
	}

	if (slot > 2 || lo > hi || hi > 1) {
		mad->mad_hdr.status
			= __constant_cpu_to_be16(DM_MAD_STATUS_NO_IOC);
		return;
	}

	svc_entries = (struct ib_dm_svc_entries *)mad->data;
	memset(svc_entries, 0, sizeof *svc_entries);
	svc_entries->service_entries[0].id = cpu_to_be64(ioc_guid);
	snprintf(svc_entries->service_entries[0].name,
		 sizeof(svc_entries->service_entries[0].name),
		 "%s%016llx",
		 SRP_SERVICE_NAME_PREFIX,
		 ioc_guid);

	mad->mad_hdr.status = 0;
}

/**
 * srpt_mgmt_method_get() - Process a received management datagram.
 * @sp:      source port through which the MAD has been received.
 * @rq_mad:  received MAD.
 * @rsp_mad: response MAD.
 */
static void srpt_mgmt_method_get(struct srpt_port *sp, struct ib_mad *rq_mad,
				 struct ib_dm_mad *rsp_mad)
{
	u16 attr_id;
	u32 slot;
	u8 hi, lo;

	attr_id = be16_to_cpu(rq_mad->mad_hdr.attr_id);
	switch (attr_id) {
	case DM_ATTR_CLASS_PORT_INFO:
		srpt_get_class_port_info(rsp_mad);
		break;
	case DM_ATTR_IOU_INFO:
		srpt_get_iou(rsp_mad);
		break;
	case DM_ATTR_IOC_PROFILE:
		slot = be32_to_cpu(rq_mad->mad_hdr.attr_mod);
		srpt_get_ioc(sp, slot, rsp_mad);
		break;
	case DM_ATTR_SVC_ENTRIES:
		slot = be32_to_cpu(rq_mad->mad_hdr.attr_mod);
		hi = (u8) ((slot >> 8) & 0xff);
		lo = (u8) (slot & 0xff);
		slot = (u16) ((slot >> 16) & 0xffff);
		srpt_get_svc_entries(srpt_service_guid,
				     slot, hi, lo, rsp_mad);
		break;
	default:
		rsp_mad->mad_hdr.status =
		    __constant_cpu_to_be16(DM_MAD_STATUS_UNSUP_METHOD_ATTR);
		break;
	}
}

/**
 * srpt_mad_send_handler() - Post MAD-send callback function.
 */
static void srpt_mad_send_handler(struct ib_mad_agent *mad_agent,
				  struct ib_mad_send_wc *mad_wc)
{
	ib_destroy_ah(mad_wc->send_buf->ah);
	ib_free_send_mad(mad_wc->send_buf);
}

/**
 * srpt_mad_recv_handler() - MAD reception callback function.
 */
static void srpt_mad_recv_handler(struct ib_mad_agent *mad_agent,
				  struct ib_mad_recv_wc *mad_wc)
{
	struct srpt_port *sport = (struct srpt_port *)mad_agent->context;
	struct ib_ah *ah;
	struct ib_mad_send_buf *rsp;
	struct ib_dm_mad *dm_mad;

	if (!mad_wc || !mad_wc->recv_buf.mad)
		return;

	ah = ib_create_ah_from_wc(mad_agent->qp->pd, mad_wc->wc,
				  mad_wc->recv_buf.grh, mad_agent->port_num);
	if (IS_ERR(ah))
		goto err;

	BUILD_BUG_ON(offsetof(struct ib_dm_mad, data) != IB_MGMT_DEVICE_HDR);

	rsp = ib_create_send_mad(mad_agent, mad_wc->wc->src_qp,
				 mad_wc->wc->pkey_index, 0,
				 IB_MGMT_DEVICE_HDR, IB_MGMT_DEVICE_DATA,
				 GFP_KERNEL);
	if (IS_ERR(rsp))
		goto err_rsp;

	rsp->ah = ah;

	dm_mad = rsp->mad;
	memcpy(dm_mad, mad_wc->recv_buf.mad, sizeof *dm_mad);
	dm_mad->mad_hdr.method = IB_MGMT_METHOD_GET_RESP;
	dm_mad->mad_hdr.status = 0;

	switch (mad_wc->recv_buf.mad->mad_hdr.method) {
	case IB_MGMT_METHOD_GET:
		srpt_mgmt_method_get(sport, mad_wc->recv_buf.mad, dm_mad);
		break;
	case IB_MGMT_METHOD_SET:
		dm_mad->mad_hdr.status =
		    __constant_cpu_to_be16(DM_MAD_STATUS_UNSUP_METHOD_ATTR);
		break;
	default:
		dm_mad->mad_hdr.status =
		    __constant_cpu_to_be16(DM_MAD_STATUS_UNSUP_METHOD);
		break;
	}

	if (!ib_post_send_mad(rsp, NULL)) {
		ib_free_recv_mad(mad_wc);
		/* will destroy_ah & free_send_mad in send completion */
		return;
	}

	ib_free_send_mad(rsp);

err_rsp:
	ib_destroy_ah(ah);
err:
	ib_free_recv_mad(mad_wc);
}

/**
 * srpt_refresh_port() - Configure a HCA port.
 *
 * Enable InfiniBand management datagram processing, update the cached sm_lid,
 * lid and gid values, and register a callback function for processing MADs
 * on the specified port.
 *
 * Note: It is safe to call this function more than once for the same port.
 */
static int srpt_refresh_port(struct srpt_port *sport)
{
	struct ib_mad_reg_req reg_req;
	struct ib_port_modify port_modify;
	struct ib_port_attr port_attr;
	int ret;

	memset(&port_modify, 0, sizeof port_modify);
	port_modify.set_port_cap_mask = IB_PORT_DEVICE_MGMT_SUP;
	port_modify.clr_port_cap_mask = 0;

	ret = ib_modify_port(sport->sdev->device, sport->port, 0, &port_modify);
	if (ret)
		goto err_mod_port;

	ret = ib_query_port(sport->sdev->device, sport->port, &port_attr);
	if (ret)
		goto err_query_port;

	sport->sm_lid = port_attr.sm_lid;
	sport->lid = port_attr.lid;

	ret = ib_query_gid(sport->sdev->device, sport->port, 0, &sport->gid);
	if (ret)
		goto err_query_port;

	if (!sport->mad_agent) {
		memset(&reg_req, 0, sizeof reg_req);
		reg_req.mgmt_class = IB_MGMT_CLASS_DEVICE_MGMT;
		reg_req.mgmt_class_version = IB_MGMT_BASE_VERSION;
		set_bit(IB_MGMT_METHOD_GET, reg_req.method_mask);
		set_bit(IB_MGMT_METHOD_SET, reg_req.method_mask);

		sport->mad_agent = ib_register_mad_agent(sport->sdev->device,
							 sport->port,
							 IB_QPT_GSI,
							 &reg_req, 0,
							 srpt_mad_send_handler,
							 srpt_mad_recv_handler,
							 sport);
		if (IS_ERR(sport->mad_agent)) {
			ret = PTR_ERR(sport->mad_agent);
			sport->mad_agent = NULL;
			goto err_query_port;
		}
	}

	return 0;

err_query_port:

	port_modify.set_port_cap_mask = 0;
	port_modify.clr_port_cap_mask = IB_PORT_DEVICE_MGMT_SUP;
	ib_modify_port(sport->sdev->device, sport->port, 0, &port_modify);

err_mod_port:

	return ret;
}

/**
 * srpt_unregister_mad_agent() - Unregister MAD callback functions.
 *
 * Note: It is safe to call this function more than once for the same device.
 */
static void srpt_unregister_mad_agent(struct srpt_device *sdev)
{
	struct ib_port_modify port_modify = {
		.clr_port_cap_mask = IB_PORT_DEVICE_MGMT_SUP,
	};
	struct srpt_port *sport;
	int i;

	for (i = 1; i <= sdev->device->phys_port_cnt; i++) {
		sport = &sdev->port[i - 1];
		WARN_ON(sport->port != i);
		if (ib_modify_port(sdev->device, i, 0, &port_modify) < 0)
			printk(KERN_ERR "disabling MAD processing failed.\n");
		if (sport->mad_agent) {
			ib_unregister_mad_agent(sport->mad_agent);
			sport->mad_agent = NULL;
		}
	}
}

/**
 * srpt_alloc_ioctx() - Allocate an SRPT I/O context structure.
 */
static struct srpt_ioctx *srpt_alloc_ioctx(struct srpt_device *sdev,
					   int ioctx_size, int dma_size,
					   enum dma_data_direction dir)
{
	struct srpt_ioctx *ioctx;

	ioctx = kmalloc(ioctx_size, GFP_KERNEL);
	if (!ioctx)
		goto err;

	ioctx->buf = kmalloc(dma_size, GFP_KERNEL);
	if (!ioctx->buf)
		goto err_free_ioctx;

	ioctx->dma = ib_dma_map_single(sdev->device, ioctx->buf, dma_size, dir);
	if (ib_dma_mapping_error(sdev->device, ioctx->dma))
		goto err_free_buf;

	return ioctx;

err_free_buf:
	kfree(ioctx->buf);
err_free_ioctx:
	kfree(ioctx);
err:
	return NULL;
}

/**
 * srpt_free_ioctx() - Free an SRPT I/O context structure.
 */
static void srpt_free_ioctx(struct srpt_device *sdev, struct srpt_ioctx *ioctx,
			    int dma_size, enum dma_data_direction dir)
{
	if (!ioctx)
		return;

	ib_dma_unmap_single(sdev->device, ioctx->dma, dma_size, dir);
	kfree(ioctx->buf);
	kfree(ioctx);
}

/**
 * srpt_alloc_ioctx_ring() - Allocate a ring of SRPT I/O context structures.
 * @sdev:       Device to allocate the I/O context ring for.
 * @ring_size:  Number of elements in the I/O context ring.
 * @ioctx_size: I/O context size.
 * @dma_size:   DMA buffer size.
 * @dir:        DMA data direction.
 */
static struct srpt_ioctx **srpt_alloc_ioctx_ring(struct srpt_device *sdev,
				int ring_size, int ioctx_size,
				int dma_size, enum dma_data_direction dir)
{
	struct srpt_ioctx **ring;
	int i;

	WARN_ON(ioctx_size != sizeof(struct srpt_recv_ioctx)
		&& ioctx_size != sizeof(struct srpt_send_ioctx));

	ring = kmalloc(ring_size * sizeof(ring[0]), GFP_KERNEL);
	if (!ring)
		goto out;
	for (i = 0; i < ring_size; ++i) {
		ring[i] = srpt_alloc_ioctx(sdev, ioctx_size, dma_size, dir);
		if (!ring[i])
			goto err;
		ring[i]->index = i;
	}
	goto out;

err:
	while (--i >= 0)
		srpt_free_ioctx(sdev, ring[i], dma_size, dir);
	kfree(ring);
	ring = NULL;
out:
	return ring;
}

/**
 * srpt_free_ioctx_ring() - Free the ring of SRPT I/O context structures.
 */
static void srpt_free_ioctx_ring(struct srpt_ioctx **ioctx_ring,
				 struct srpt_device *sdev, int ring_size,
				 int dma_size, enum dma_data_direction dir)
{
	int i;

	for (i = 0; i < ring_size; ++i)
		srpt_free_ioctx(sdev, ioctx_ring[i], dma_size, dir);
	kfree(ioctx_ring);
}

/**
 * srpt_get_cmd_state() - Get the state of a SCSI command.
 */
static enum srpt_command_state srpt_get_cmd_state(struct srpt_send_ioctx *ioctx)
{
	enum srpt_command_state state;
	unsigned long flags;

	BUG_ON(!ioctx);

	spin_lock_irqsave(&ioctx->spinlock, flags);
	state = ioctx->state;
	spin_unlock_irqrestore(&ioctx->spinlock, flags);
	return state;
}

/**
 * srpt_set_cmd_state() - Set the state of a SCSI command.
 *
 * Does not modify the state of aborted commands. Returns the previous command
 * state.
 */
static enum srpt_command_state srpt_set_cmd_state(struct srpt_send_ioctx *ioctx,
						  enum srpt_command_state new)
{
	enum srpt_command_state previous;
	unsigned long flags;

	BUG_ON(!ioctx);

	spin_lock_irqsave(&ioctx->spinlock, flags);
	previous = ioctx->state;
	if (previous != SRPT_STATE_DONE)
		ioctx->state = new;
	spin_unlock_irqrestore(&ioctx->spinlock, flags);

	return previous;
}

/**
 * srpt_test_and_set_cmd_state() - Test and set the state of a command.
 *
 * Returns true if and only if the previous command state was equal to 'old'.
 */
static bool srpt_test_and_set_cmd_state(struct srpt_send_ioctx *ioctx,
					enum srpt_command_state old,
					enum srpt_command_state new)
{
	enum srpt_command_state previous;
	unsigned long flags;

	WARN_ON(!ioctx);
	WARN_ON(old == SRPT_STATE_DONE);
	WARN_ON(new == SRPT_STATE_NEW);

	spin_lock_irqsave(&ioctx->spinlock, flags);
	previous = ioctx->state;
	if (previous == old)
		ioctx->state = new;
	spin_unlock_irqrestore(&ioctx->spinlock, flags);
	return previous == old;
}

/**
 * srpt_post_recv() - Post an IB receive request.
 */
static int srpt_post_recv(struct srpt_device *sdev,
			  struct srpt_recv_ioctx *ioctx)
{
	struct ib_sge list;
	struct ib_recv_wr wr, *bad_wr;

	BUG_ON(!sdev);
	wr.wr_id = encode_wr_id(SRPT_RECV, ioctx->ioctx.index);

	list.addr = ioctx->ioctx.dma;
	list.length = srp_max_req_size;
	list.lkey = sdev->mr->lkey;

	wr.next = NULL;
	wr.sg_list = &list;
	wr.num_sge = 1;

	return ib_post_srq_recv(sdev->srq, &wr, &bad_wr);
}

/**
 * srpt_post_send() - Post an IB send request.
 *
 * Returns zero upon success and a non-zero value upon failure.
 */
static int srpt_post_send(struct srpt_rdma_ch *ch,
			  struct srpt_send_ioctx *ioctx, int len)
{
	struct ib_sge list;
	struct ib_send_wr wr, *bad_wr;
	struct srpt_device *sdev = ch->sport->sdev;
	int ret;

	atomic_inc(&ch->req_lim);

	ret = -ENOMEM;
	if (unlikely(atomic_dec_return(&ch->sq_wr_avail) < 0)) {
		printk(KERN_WARNING "IB send queue full (needed 1)\n");
		goto out;
	}

	ib_dma_sync_single_for_device(sdev->device, ioctx->ioctx.dma, len,
				      DMA_TO_DEVICE);

	list.addr = ioctx->ioctx.dma;
	list.length = len;
	list.lkey = sdev->mr->lkey;

	wr.next = NULL;
	wr.wr_id = encode_wr_id(SRPT_SEND, ioctx->ioctx.index);
	wr.sg_list = &list;
	wr.num_sge = 1;
	wr.opcode = IB_WR_SEND;
	wr.send_flags = IB_SEND_SIGNALED;

	ret = ib_post_send(ch->qp, &wr, &bad_wr);

out:
	if (ret < 0) {
		atomic_inc(&ch->sq_wr_avail);
		atomic_dec(&ch->req_lim);
	}
	return ret;
}

/**
 * srpt_get_desc_tbl() - Parse the data descriptors of an SRP_CMD request.
 * @ioctx: Pointer to the I/O context associated with the request.
 * @srp_cmd: Pointer to the SRP_CMD request data.
 * @dir: Pointer to the variable to which the transfer direction will be
 *   written.
 * @data_len: Pointer to the variable to which the total data length of all
 *   descriptors in the SRP_CMD request will be written.
 *
 * This function initializes ioctx->nrbuf and ioctx->r_bufs.
 *
 * Returns -EINVAL when the SRP_CMD request contains inconsistent descriptors;
 * -ENOMEM when memory allocation fails and zero upon success.
 */
static int srpt_get_desc_tbl(struct srpt_send_ioctx *ioctx,
			     struct srp_cmd *srp_cmd,
			     enum dma_data_direction *dir, u64 *data_len)
{
	struct srp_indirect_buf *idb;
	struct srp_direct_buf *db;
	unsigned add_cdb_offset;
	int ret;

	/*
	 * The pointer computations below will only be compiled correctly
	 * if srp_cmd::add_data is declared as s8*, u8*, s8[] or u8[], so check
	 * whether srp_cmd::add_data has been declared as a byte pointer.
	 */
	BUILD_BUG_ON(!__same_type(srp_cmd->add_data[0], (s8)0)
		     && !__same_type(srp_cmd->add_data[0], (u8)0));

	BUG_ON(!dir);
	BUG_ON(!data_len);

	ret = 0;
	*data_len = 0;

	/*
	 * The lower four bits of the buffer format field contain the DATA-IN
	 * buffer descriptor format, and the highest four bits contain the
	 * DATA-OUT buffer descriptor format.
	 */
	*dir = DMA_NONE;
	if (srp_cmd->buf_fmt & 0xf)
		/* DATA-IN: transfer data from target to initiator (read). */
		*dir = DMA_FROM_DEVICE;
	else if (srp_cmd->buf_fmt >> 4)
		/* DATA-OUT: transfer data from initiator to target (write). */
		*dir = DMA_TO_DEVICE;

	/*
	 * According to the SRP spec, the lower two bits of the 'ADDITIONAL
	 * CDB LENGTH' field are reserved and the size in bytes of this field
	 * is four times the value specified in bits 3..7. Hence the "& ~3".
	 */
	add_cdb_offset = srp_cmd->add_cdb_len & ~3;
	if (((srp_cmd->buf_fmt & 0xf) == SRP_DATA_DESC_DIRECT) ||
	    ((srp_cmd->buf_fmt >> 4) == SRP_DATA_DESC_DIRECT)) {
		ioctx->n_rbuf = 1;
		ioctx->rbufs = &ioctx->single_rbuf;

		db = (struct srp_direct_buf *)(srp_cmd->add_data
					       + add_cdb_offset);
		memcpy(ioctx->rbufs, db, sizeof *db);
		*data_len = be32_to_cpu(db->len);
	} else if (((srp_cmd->buf_fmt & 0xf) == SRP_DATA_DESC_INDIRECT) ||
		   ((srp_cmd->buf_fmt >> 4) == SRP_DATA_DESC_INDIRECT)) {
		idb = (struct srp_indirect_buf *)(srp_cmd->add_data
						  + add_cdb_offset);

		ioctx->n_rbuf = be32_to_cpu(idb->table_desc.len) / sizeof *db;

		if (ioctx->n_rbuf >
		    (srp_cmd->data_out_desc_cnt + srp_cmd->data_in_desc_cnt)) {
			printk(KERN_ERR "received unsupported SRP_CMD request"
			       " type (%u out + %u in != %u / %zu)\n",
			       srp_cmd->data_out_desc_cnt,
			       srp_cmd->data_in_desc_cnt,
			       be32_to_cpu(idb->table_desc.len),
			       sizeof(*db));
			ioctx->n_rbuf = 0;
			ret = -EINVAL;
			goto out;
		}

		if (ioctx->n_rbuf == 1)
			ioctx->rbufs = &ioctx->single_rbuf;
		else {
			ioctx->rbufs =
				kmalloc(ioctx->n_rbuf * sizeof *db, GFP_ATOMIC);
			if (!ioctx->rbufs) {
				ioctx->n_rbuf = 0;
				ret = -ENOMEM;
				goto out;
			}
		}

		db = idb->desc_list;
		memcpy(ioctx->rbufs, db, ioctx->n_rbuf * sizeof *db);
		*data_len = be32_to_cpu(idb->len);
	}
out:
	return ret;
}

/**
 * srpt_init_ch_qp() - Initialize queue pair attributes.
 *
 * Initialized the attributes of queue pair 'qp' by allowing local write,
 * remote read and remote write. Also transitions 'qp' to state IB_QPS_INIT.
 */
static int srpt_init_ch_qp(struct srpt_rdma_ch *ch, struct ib_qp *qp)
{
	struct ib_qp_attr *attr;
	int ret;

	attr = kzalloc(sizeof *attr, GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	attr->qp_state = IB_QPS_INIT;
	attr->qp_access_flags = IB_ACCESS_LOCAL_WRITE | IB_ACCESS_REMOTE_READ |
	    IB_ACCESS_REMOTE_WRITE;
	attr->port_num = ch->sport->port;
	attr->pkey_index = 0;

	ret = ib_modify_qp(qp, attr,
			   IB_QP_STATE | IB_QP_ACCESS_FLAGS | IB_QP_PORT |
			   IB_QP_PKEY_INDEX);

	kfree(attr);
	return ret;
}

/**
 * srpt_ch_qp_rtr() - Change the state of a channel to 'ready to receive' (RTR).
 * @ch: channel of the queue pair.
 * @qp: queue pair to change the state of.
 *
 * Returns zero upon success and a negative value upon failure.
 *
 * Note: currently a struct ib_qp_attr takes 136 bytes on a 64-bit system.
 * If this structure ever becomes larger, it might be necessary to allocate
 * it dynamically instead of on the stack.
 */
static int srpt_ch_qp_rtr(struct srpt_rdma_ch *ch, struct ib_qp *qp)
{
	struct ib_qp_attr qp_attr;
	int attr_mask;
	int ret;

	qp_attr.qp_state = IB_QPS_RTR;
	ret = ib_cm_init_qp_attr(ch->cm_id, &qp_attr, &attr_mask);
	if (ret)
		goto out;

	qp_attr.max_dest_rd_atomic = 4;

	ret = ib_modify_qp(qp, &qp_attr, attr_mask);

out:
	return ret;
}

/**
 * srpt_ch_qp_rts() - Change the state of a channel to 'ready to send' (RTS).
 * @ch: channel of the queue pair.
 * @qp: queue pair to change the state of.
 *
 * Returns zero upon success and a negative value upon failure.
 *
 * Note: currently a struct ib_qp_attr takes 136 bytes on a 64-bit system.
 * If this structure ever becomes larger, it might be necessary to allocate
 * it dynamically instead of on the stack.
 */
static int srpt_ch_qp_rts(struct srpt_rdma_ch *ch, struct ib_qp *qp)
{
	struct ib_qp_attr qp_attr;
	int attr_mask;
	int ret;

	qp_attr.qp_state = IB_QPS_RTS;
	ret = ib_cm_init_qp_attr(ch->cm_id, &qp_attr, &attr_mask);
	if (ret)
		goto out;

	qp_attr.max_rd_atomic = 4;

	ret = ib_modify_qp(qp, &qp_attr, attr_mask);

out:
	return ret;
}

/**
 * srpt_ch_qp_err() - Set the channel queue pair state to 'error'.
 */
static int srpt_ch_qp_err(struct srpt_rdma_ch *ch)
{
	struct ib_qp_attr qp_attr;

	qp_attr.qp_state = IB_QPS_ERR;
	return ib_modify_qp(ch->qp, &qp_attr, IB_QP_STATE);
}

/**
 * srpt_unmap_sg_to_ib_sge() - Unmap an IB SGE list.
 */
static void srpt_unmap_sg_to_ib_sge(struct srpt_rdma_ch *ch,
				    struct srpt_send_ioctx *ioctx)
{
	struct scatterlist *sg;
	enum dma_data_direction dir;

	BUG_ON(!ch);
	BUG_ON(!ioctx);
	BUG_ON(ioctx->n_rdma && !ioctx->rdma_ius);

	while (ioctx->n_rdma)
		kfree(ioctx->rdma_ius[--ioctx->n_rdma].sge);

	kfree(ioctx->rdma_ius);
	ioctx->rdma_ius = NULL;

	if (ioctx->mapped_sg_count) {
		sg = ioctx->sg;
		WARN_ON(!sg);
		dir = ioctx->cmd.data_direction;
		BUG_ON(dir == DMA_NONE);
		ib_dma_unmap_sg(ch->sport->sdev->device, sg, ioctx->sg_cnt,
				opposite_dma_dir(dir));
		ioctx->mapped_sg_count = 0;
	}
}

/**
 * srpt_map_sg_to_ib_sge() - Map an SG list to an IB SGE list.
 */
static int srpt_map_sg_to_ib_sge(struct srpt_rdma_ch *ch,
				 struct srpt_send_ioctx *ioctx)
{
	struct se_cmd *cmd;
	struct scatterlist *sg, *sg_orig;
	int sg_cnt;
	enum dma_data_direction dir;
	struct rdma_iu *riu;
	struct srp_direct_buf *db;
	dma_addr_t dma_addr;
	struct ib_sge *sge;
	u64 raddr;
	u32 rsize;
	u32 tsize;
	u32 dma_len;
	int count, nrdma;
	int i, j, k;

	BUG_ON(!ch);
	BUG_ON(!ioctx);
	cmd = &ioctx->cmd;
	dir = cmd->data_direction;
	BUG_ON(dir == DMA_NONE);

	ioctx->sg = sg = sg_orig = cmd->t_data_sg;
	ioctx->sg_cnt = sg_cnt = cmd->t_data_nents;

	count = ib_dma_map_sg(ch->sport->sdev->device, sg, sg_cnt,
			      opposite_dma_dir(dir));
	if (unlikely(!count))
		return -EAGAIN;

	ioctx->mapped_sg_count = count;

	if (ioctx->rdma_ius && ioctx->n_rdma_ius)
		nrdma = ioctx->n_rdma_ius;
	else {
		nrdma = (count + SRPT_DEF_SG_PER_WQE - 1) / SRPT_DEF_SG_PER_WQE
			+ ioctx->n_rbuf;

		ioctx->rdma_ius = kzalloc(nrdma * sizeof *riu, GFP_KERNEL);
		if (!ioctx->rdma_ius)
			goto free_mem;

		ioctx->n_rdma_ius = nrdma;
	}

	db = ioctx->rbufs;
	tsize = cmd->data_length;
	dma_len = sg_dma_len(&sg[0]);
	riu = ioctx->rdma_ius;

	/*
	 * For each remote desc - calculate the #ib_sge.
	 * If #ib_sge < SRPT_DEF_SG_PER_WQE per rdma operation then
	 *      each remote desc rdma_iu is required a rdma wr;
	 * else
	 *      we need to allocate extra rdma_iu to carry extra #ib_sge in
	 *      another rdma wr
	 */
	for (i = 0, j = 0;
	     j < count && i < ioctx->n_rbuf && tsize > 0; ++i, ++riu, ++db) {
		rsize = be32_to_cpu(db->len);
		raddr = be64_to_cpu(db->va);
		riu->raddr = raddr;
		riu->rkey = be32_to_cpu(db->key);
		riu->sge_cnt = 0;

		/* calculate how many sge required for this remote_buf */
		while (rsize > 0 && tsize > 0) {

			if (rsize >= dma_len) {
				tsize -= dma_len;
				rsize -= dma_len;
				raddr += dma_len;

				if (tsize > 0) {
					++j;
					if (j < count) {
						sg = sg_next(sg);
						dma_len = sg_dma_len(sg);
					}
				}
			} else {
				tsize -= rsize;
				dma_len -= rsize;
				rsize = 0;
			}

			++riu->sge_cnt;

			if (rsize > 0 && riu->sge_cnt == SRPT_DEF_SG_PER_WQE) {
				++ioctx->n_rdma;
				riu->sge =
				    kmalloc(riu->sge_cnt * sizeof *riu->sge,
					    GFP_KERNEL);
				if (!riu->sge)
					goto free_mem;

				++riu;
				riu->sge_cnt = 0;
				riu->raddr = raddr;
				riu->rkey = be32_to_cpu(db->key);
			}
		}

		++ioctx->n_rdma;
		riu->sge = kmalloc(riu->sge_cnt * sizeof *riu->sge,
				   GFP_KERNEL);
		if (!riu->sge)
			goto free_mem;
	}

	db = ioctx->rbufs;
	tsize = cmd->data_length;
	riu = ioctx->rdma_ius;
	sg = sg_orig;
	dma_len = sg_dma_len(&sg[0]);
	dma_addr = sg_dma_address(&sg[0]);

	/* this second loop is really mapped sg_addres to rdma_iu->ib_sge */
	for (i = 0, j = 0;
	     j < count && i < ioctx->n_rbuf && tsize > 0; ++i, ++riu, ++db) {
		rsize = be32_to_cpu(db->len);
		sge = riu->sge;
		k = 0;

		while (rsize > 0 && tsize > 0) {
			sge->addr = dma_addr;
			sge->lkey = ch->sport->sdev->mr->lkey;

			if (rsize >= dma_len) {
				sge->length =
					(tsize < dma_len) ? tsize : dma_len;
				tsize -= dma_len;
				rsize -= dma_len;

				if (tsize > 0) {
					++j;
					if (j < count) {
						sg = sg_next(sg);
						dma_len = sg_dma_len(sg);
						dma_addr = sg_dma_address(sg);
					}
				}
			} else {
				sge->length = (tsize < rsize) ? tsize : rsize;
				tsize -= rsize;
				dma_len -= rsize;
				dma_addr += rsize;
				rsize = 0;
			}

			++k;
			if (k == riu->sge_cnt && rsize > 0 && tsize > 0) {
				++riu;
				sge = riu->sge;
				k = 0;
			} else if (rsize > 0 && tsize > 0)
				++sge;
		}
	}

	return 0;

free_mem:
	srpt_unmap_sg_to_ib_sge(ch, ioctx);

	return -ENOMEM;
}

/**
 * srpt_get_send_ioctx() - Obtain an I/O context for sending to the initiator.
 */
static struct srpt_send_ioctx *srpt_get_send_ioctx(struct srpt_rdma_ch *ch)
{
	struct srpt_send_ioctx *ioctx;
	unsigned long flags;

	BUG_ON(!ch);

	ioctx = NULL;
	spin_lock_irqsave(&ch->spinlock, flags);
	if (!list_empty(&ch->free_list)) {
		ioctx = list_first_entry(&ch->free_list,
					 struct srpt_send_ioctx, free_list);
		list_del(&ioctx->free_list);
	}
	spin_unlock_irqrestore(&ch->spinlock, flags);

	if (!ioctx)
		return ioctx;

	BUG_ON(ioctx->ch != ch);
	spin_lock_init(&ioctx->spinlock);
	ioctx->state = SRPT_STATE_NEW;
	ioctx->n_rbuf = 0;
	ioctx->rbufs = NULL;
	ioctx->n_rdma = 0;
	ioctx->n_rdma_ius = 0;
	ioctx->rdma_ius = NULL;
	ioctx->mapped_sg_count = 0;
	init_completion(&ioctx->tx_done);
	ioctx->queue_status_only = false;
	/*
	 * transport_init_se_cmd() does not initialize all fields, so do it
	 * here.
	 */
	memset(&ioctx->cmd, 0, sizeof(ioctx->cmd));
	memset(&ioctx->sense_data, 0, sizeof(ioctx->sense_data));

	return ioctx;
}

/**
 * srpt_abort_cmd() - Abort a SCSI command.
 * @ioctx:   I/O context associated with the SCSI command.
 * @context: Preferred execution context.
 */
static int srpt_abort_cmd(struct srpt_send_ioctx *ioctx)
{
	enum srpt_command_state state;
	unsigned long flags;

	BUG_ON(!ioctx);

	/*
	 * If the command is in a state where the target core is waiting for
	 * the ib_srpt driver, change the state to the next state. Changing
	 * the state of the command from SRPT_STATE_NEED_DATA to
	 * SRPT_STATE_DATA_IN ensures that srpt_xmit_response() will call this
	 * function a second time.
	 */

	spin_lock_irqsave(&ioctx->spinlock, flags);
	state = ioctx->state;
	switch (state) {
	case SRPT_STATE_NEED_DATA:
		ioctx->state = SRPT_STATE_DATA_IN;
		break;
	case SRPT_STATE_DATA_IN:
	case SRPT_STATE_CMD_RSP_SENT:
	case SRPT_STATE_MGMT_RSP_SENT:
		ioctx->state = SRPT_STATE_DONE;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&ioctx->spinlock, flags);

	if (state == SRPT_STATE_DONE) {
		struct srpt_rdma_ch *ch = ioctx->ch;

		BUG_ON(ch->sess == NULL);

		target_put_sess_cmd(ch->sess, &ioctx->cmd);
		goto out;
	}

	pr_debug("Aborting cmd with state %d and tag %lld\n", state,
		 ioctx->tag);

	switch (state) {
	case SRPT_STATE_NEW:
	case SRPT_STATE_DATA_IN:
	case SRPT_STATE_MGMT:
		/*
		 * Do nothing - defer abort processing until
		 * srpt_queue_response() is invoked.
		 */
		WARN_ON(!transport_check_aborted_status(&ioctx->cmd, false));
		break;
	case SRPT_STATE_NEED_DATA:
		/* DMA_TO_DEVICE (write) - RDMA read error. */

		/* XXX(hch): this is a horrible layering violation.. */
		spin_lock_irqsave(&ioctx->cmd.t_state_lock, flags);
		ioctx->cmd.transport_state |= CMD_T_LUN_STOP;
		ioctx->cmd.transport_state &= ~CMD_T_ACTIVE;
		spin_unlock_irqrestore(&ioctx->cmd.t_state_lock, flags);

		complete(&ioctx->cmd.transport_lun_stop_comp);
		break;
	case SRPT_STATE_CMD_RSP_SENT:
		/*
		 * SRP_RSP sending failed or the SRP_RSP send completion has
		 * not been received in time.
		 */
		srpt_unmap_sg_to_ib_sge(ioctx->ch, ioctx);
		spin_lock_irqsave(&ioctx->cmd.t_state_lock, flags);
		ioctx->cmd.transport_state |= CMD_T_LUN_STOP;
		spin_unlock_irqrestore(&ioctx->cmd.t_state_lock, flags);
		target_put_sess_cmd(ioctx->ch->sess, &ioctx->cmd);
		break;
	case SRPT_STATE_MGMT_RSP_SENT:
		srpt_set_cmd_state(ioctx, SRPT_STATE_DONE);
		target_put_sess_cmd(ioctx->ch->sess, &ioctx->cmd);
		break;
	default:
		WARN_ON("ERROR: unexpected command state");
		break;
	}

out:
	return state;
}

/**
 * srpt_handle_send_err_comp() - Process an IB_WC_SEND error completion.
 */
static void srpt_handle_send_err_comp(struct srpt_rdma_ch *ch, u64 wr_id)
{
	struct srpt_send_ioctx *ioctx;
	enum srpt_command_state state;
	struct se_cmd *cmd;
	u32 index;

	atomic_inc(&ch->sq_wr_avail);

	index = idx_from_wr_id(wr_id);
	ioctx = ch->ioctx_ring[index];
	state = srpt_get_cmd_state(ioctx);
	cmd = &ioctx->cmd;

	WARN_ON(state != SRPT_STATE_CMD_RSP_SENT
		&& state != SRPT_STATE_MGMT_RSP_SENT
		&& state != SRPT_STATE_NEED_DATA
		&& state != SRPT_STATE_DONE);

	/* If SRP_RSP sending failed, undo the ch->req_lim change. */
	if (state == SRPT_STATE_CMD_RSP_SENT
	    || state == SRPT_STATE_MGMT_RSP_SENT)
		atomic_dec(&ch->req_lim);

	srpt_abort_cmd(ioctx);
}

/**
 * srpt_handle_send_comp() - Process an IB send completion notification.
 */
static void srpt_handle_send_comp(struct srpt_rdma_ch *ch,
				  struct srpt_send_ioctx *ioctx)
{
	enum srpt_command_state state;

	atomic_inc(&ch->sq_wr_avail);

	state = srpt_set_cmd_state(ioctx, SRPT_STATE_DONE);

	if (WARN_ON(state != SRPT_STATE_CMD_RSP_SENT
		    && state != SRPT_STATE_MGMT_RSP_SENT
		    && state != SRPT_STATE_DONE))
		pr_debug("state = %d\n", state);

	if (state != SRPT_STATE_DONE) {
		srpt_unmap_sg_to_ib_sge(ch, ioctx);
		transport_generic_free_cmd(&ioctx->cmd, 0);
	} else {
		printk(KERN_ERR "IB completion has been received too late for"
		       " wr_id = %u.\n", ioctx->ioctx.index);
	}
}

/**
 * srpt_handle_rdma_comp() - Process an IB RDMA completion notification.
 *
 * XXX: what is now target_execute_cmd used to be asynchronous, and unmapping
 * the data that has been transferred via IB RDMA had to be postponed until the
 * check_stop_free() callback.  None of this is necessary anymore and needs to
 * be cleaned up.
 */
static void srpt_handle_rdma_comp(struct srpt_rdma_ch *ch,
				  struct srpt_send_ioctx *ioctx,
				  enum srpt_opcode opcode)
{
	WARN_ON(ioctx->n_rdma <= 0);
	atomic_add(ioctx->n_rdma, &ch->sq_wr_avail);

	if (opcode == SRPT_RDMA_READ_LAST) {
		if (srpt_test_and_set_cmd_state(ioctx, SRPT_STATE_NEED_DATA,
						SRPT_STATE_DATA_IN))
			target_execute_cmd(&ioctx->cmd);
		else
			printk(KERN_ERR "%s[%d]: wrong state = %d\n", __func__,
			       __LINE__, srpt_get_cmd_state(ioctx));
	} else if (opcode == SRPT_RDMA_ABORT) {
		ioctx->rdma_aborted = true;
	} else {
		WARN(true, "unexpected opcode %d\n", opcode);
	}
}

/**
 * srpt_handle_rdma_err_comp() - Process an IB RDMA error completion.
 */
static void srpt_handle_rdma_err_comp(struct srpt_rdma_ch *ch,
				      struct srpt_send_ioctx *ioctx,
				      enum srpt_opcode opcode)
{
	struct se_cmd *cmd;
	enum srpt_command_state state;
	unsigned long flags;

	cmd = &ioctx->cmd;
	state = srpt_get_cmd_state(ioctx);
	switch (opcode) {
	case SRPT_RDMA_READ_LAST:
		if (ioctx->n_rdma <= 0) {
			printk(KERN_ERR "Received invalid RDMA read"
			       " error completion with idx %d\n",
			       ioctx->ioctx.index);
			break;
		}
		atomic_add(ioctx->n_rdma, &ch->sq_wr_avail);
		if (state == SRPT_STATE_NEED_DATA)
			srpt_abort_cmd(ioctx);
		else
			printk(KERN_ERR "%s[%d]: wrong state = %d\n",
			       __func__, __LINE__, state);
		break;
	case SRPT_RDMA_WRITE_LAST:
		spin_lock_irqsave(&ioctx->cmd.t_state_lock, flags);
		ioctx->cmd.transport_state |= CMD_T_LUN_STOP;
		spin_unlock_irqrestore(&ioctx->cmd.t_state_lock, flags);
		break;
	default:
		printk(KERN_ERR "%s[%d]: opcode = %u\n", __func__,
		       __LINE__, opcode);
		break;
	}
}

/**
 * srpt_build_cmd_rsp() - Build an SRP_RSP response.
 * @ch: RDMA channel through which the request has been received.
 * @ioctx: I/O context associated with the SRP_CMD request. The response will
 *   be built in the buffer ioctx->buf points at and hence this function will
 *   overwrite the request data.
 * @tag: tag of the request for which this response is being generated.
 * @status: value for the STATUS field of the SRP_RSP information unit.
 *
 * Returns the size in bytes of the SRP_RSP response.
 *
 * An SRP_RSP response contains a SCSI status or service response. See also
 * section 6.9 in the SRP r16a document for the format of an SRP_RSP
 * response. See also SPC-2 for more information about sense data.
 */
static int srpt_build_cmd_rsp(struct srpt_rdma_ch *ch,
			      struct srpt_send_ioctx *ioctx, u64 tag,
			      int status)
{
	struct srp_rsp *srp_rsp;
	const u8 *sense_data;
	int sense_data_len, max_sense_len;

	/*
	 * The lowest bit of all SAM-3 status codes is zero (see also
	 * paragraph 5.3 in SAM-3).
	 */
	WARN_ON(status & 1);

	srp_rsp = ioctx->ioctx.buf;
	BUG_ON(!srp_rsp);

	sense_data = ioctx->sense_data;
	sense_data_len = ioctx->cmd.scsi_sense_length;
	WARN_ON(sense_data_len > sizeof(ioctx->sense_data));

	memset(srp_rsp, 0, sizeof *srp_rsp);
	srp_rsp->opcode = SRP_RSP;
	srp_rsp->req_lim_delta =
		__constant_cpu_to_be32(1 + atomic_xchg(&ch->req_lim_delta, 0));
	srp_rsp->tag = tag;
	srp_rsp->status = status;

	if (sense_data_len) {
		BUILD_BUG_ON(MIN_MAX_RSP_SIZE <= sizeof(*srp_rsp));
		max_sense_len = ch->max_ti_iu_len - sizeof(*srp_rsp);
		if (sense_data_len > max_sense_len) {
			printk(KERN_WARNING "truncated sense data from %d to %d"
			       " bytes\n", sense_data_len, max_sense_len);
			sense_data_len = max_sense_len;
		}

		srp_rsp->flags |= SRP_RSP_FLAG_SNSVALID;
		srp_rsp->sense_data_len = cpu_to_be32(sense_data_len);
		memcpy(srp_rsp + 1, sense_data, sense_data_len);
	}

	return sizeof(*srp_rsp) + sense_data_len;
}

/**
 * srpt_build_tskmgmt_rsp() - Build a task management response.
 * @ch:       RDMA channel through which the request has been received.
 * @ioctx:    I/O context in which the SRP_RSP response will be built.
 * @rsp_code: RSP_CODE that will be stored in the response.
 * @tag:      Tag of the request for which this response is being generated.
 *
 * Returns the size in bytes of the SRP_RSP response.
 *
 * An SRP_RSP response contains a SCSI status or service response. See also
 * section 6.9 in the SRP r16a document for the format of an SRP_RSP
 * response.
 */
static int srpt_build_tskmgmt_rsp(struct srpt_rdma_ch *ch,
				  struct srpt_send_ioctx *ioctx,
				  u8 rsp_code, u64 tag)
{
	struct srp_rsp *srp_rsp;
	int resp_data_len;
	int resp_len;

	resp_data_len = (rsp_code == SRP_TSK_MGMT_SUCCESS) ? 0 : 4;
	resp_len = sizeof(*srp_rsp) + resp_data_len;

	srp_rsp = ioctx->ioctx.buf;
	BUG_ON(!srp_rsp);
	memset(srp_rsp, 0, sizeof *srp_rsp);

	srp_rsp->opcode = SRP_RSP;
	srp_rsp->req_lim_delta = __constant_cpu_to_be32(1
				    + atomic_xchg(&ch->req_lim_delta, 0));
	srp_rsp->tag = tag;

	if (rsp_code != SRP_TSK_MGMT_SUCCESS) {
		srp_rsp->flags |= SRP_RSP_FLAG_RSPVALID;
		srp_rsp->resp_data_len = cpu_to_be32(resp_data_len);
		srp_rsp->data[3] = rsp_code;
	}

	return resp_len;
}

#define NO_SUCH_LUN ((uint64_t)-1LL)

/*
 * SCSI LUN addressing method. See also SAM-2 and the section about
 * eight byte LUNs.
 */
enum scsi_lun_addr_method {
	SCSI_LUN_ADDR_METHOD_PERIPHERAL   = 0,
	SCSI_LUN_ADDR_METHOD_FLAT         = 1,
	SCSI_LUN_ADDR_METHOD_LUN          = 2,
	SCSI_LUN_ADDR_METHOD_EXTENDED_LUN = 3,
};

/*
 * srpt_unpack_lun() - Convert from network LUN to linear LUN.
 *
 * Convert an 2-byte, 4-byte, 6-byte or 8-byte LUN structure in network byte
 * order (big endian) to a linear LUN. Supports three LUN addressing methods:
 * peripheral, flat and logical unit. See also SAM-2, section 4.9.4 (page 40).
 */
static uint64_t srpt_unpack_lun(const uint8_t *lun, int len)
{
	uint64_t res = NO_SUCH_LUN;
	int addressing_method;

	if (unlikely(len < 2)) {
		printk(KERN_ERR "Illegal LUN length %d, expected 2 bytes or "
		       "more", len);
		goto out;
	}

	switch (len) {
	case 8:
		if ((*((__be64 *)lun) &
		     __constant_cpu_to_be64(0x0000FFFFFFFFFFFFLL)) != 0)
			goto out_err;
		break;
	case 4:
		if (*((__be16 *)&lun[2]) != 0)
			goto out_err;
		break;
	case 6:
		if (*((__be32 *)&lun[2]) != 0)
			goto out_err;
		break;
	case 2:
		break;
	default:
		goto out_err;
	}

	addressing_method = (*lun) >> 6; /* highest two bits of byte 0 */
	switch (addressing_method) {
	case SCSI_LUN_ADDR_METHOD_PERIPHERAL:
	case SCSI_LUN_ADDR_METHOD_FLAT:
	case SCSI_LUN_ADDR_METHOD_LUN:
		res = *(lun + 1) | (((*lun) & 0x3f) << 8);
		break;

	case SCSI_LUN_ADDR_METHOD_EXTENDED_LUN:
	default:
		printk(KERN_ERR "Unimplemented LUN addressing method %u",
		       addressing_method);
		break;
	}

out:
	return res;

out_err:
	printk(KERN_ERR "Support for multi-level LUNs has not yet been"
	       " implemented");
	goto out;
}

static int srpt_check_stop_free(struct se_cmd *cmd)
{
	struct srpt_send_ioctx *ioctx = container_of(cmd,
				struct srpt_send_ioctx, cmd);

	return target_put_sess_cmd(ioctx->ch->sess, &ioctx->cmd);
}

/**
 * srpt_handle_cmd() - Process SRP_CMD.
 */
static int srpt_handle_cmd(struct srpt_rdma_ch *ch,
			   struct srpt_recv_ioctx *recv_ioctx,
			   struct srpt_send_ioctx *send_ioctx)
{
	struct se_cmd *cmd;
	struct srp_cmd *srp_cmd;
	uint64_t unpacked_lun;
	u64 data_len;
	enum dma_data_direction dir;
	sense_reason_t ret;
	int rc;

	BUG_ON(!send_ioctx);

	srp_cmd = recv_ioctx->ioctx.buf;
	cmd = &send_ioctx->cmd;
	send_ioctx->tag = srp_cmd->tag;

	switch (srp_cmd->task_attr) {
	case SRP_CMD_SIMPLE_Q:
		cmd->sam_task_attr = MSG_SIMPLE_TAG;
		break;
	case SRP_CMD_ORDERED_Q:
	default:
		cmd->sam_task_attr = MSG_ORDERED_TAG;
		break;
	case SRP_CMD_HEAD_OF_Q:
		cmd->sam_task_attr = MSG_HEAD_TAG;
		break;
	case SRP_CMD_ACA:
		cmd->sam_task_attr = MSG_ACA_TAG;
		break;
	}

	if (srpt_get_desc_tbl(send_ioctx, srp_cmd, &dir, &data_len)) {
		printk(KERN_ERR "0x%llx: parsing SRP descriptor table failed.\n",
		       srp_cmd->tag);
		ret = TCM_INVALID_CDB_FIELD;
		goto send_sense;
	}

	unpacked_lun = srpt_unpack_lun((uint8_t *)&srp_cmd->lun,
				       sizeof(srp_cmd->lun));
	rc = target_submit_cmd(cmd, ch->sess, srp_cmd->cdb,
			&send_ioctx->sense_data[0], unpacked_lun, data_len,
			MSG_SIMPLE_TAG, dir, TARGET_SCF_ACK_KREF);
	if (rc != 0) {
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto send_sense;
	}
	return 0;

send_sense:
	transport_send_check_condition_and_sense(cmd, ret, 0);
	return -1;
}

/**
 * srpt_rx_mgmt_fn_tag() - Process a task management function by tag.
 * @ch: RDMA channel of the task management request.
 * @fn: Task management function to perform.
 * @req_tag: Tag of the SRP task management request.
 * @mgmt_ioctx: I/O context of the task management request.
 *
 * Returns zero if the target core will process the task management
 * request asynchronously.
 *
 * Note: It is assumed that the initiator serializes tag-based task management
 * requests.
 */
static int srpt_rx_mgmt_fn_tag(struct srpt_send_ioctx *ioctx, u64 tag)
{
	struct srpt_device *sdev;
	struct srpt_rdma_ch *ch;
	struct srpt_send_ioctx *target;
	int ret, i;

	ret = -EINVAL;
	ch = ioctx->ch;
	BUG_ON(!ch);
	BUG_ON(!ch->sport);
	sdev = ch->sport->sdev;
	BUG_ON(!sdev);
	spin_lock_irq(&sdev->spinlock);
	for (i = 0; i < ch->rq_size; ++i) {
		target = ch->ioctx_ring[i];
		if (target->cmd.se_lun == ioctx->cmd.se_lun &&
		    target->tag == tag &&
		    srpt_get_cmd_state(target) != SRPT_STATE_DONE) {
			ret = 0;
			/* now let the target core abort &target->cmd; */
			break;
		}
	}
	spin_unlock_irq(&sdev->spinlock);
	return ret;
}

static int srp_tmr_to_tcm(int fn)
{
	switch (fn) {
	case SRP_TSK_ABORT_TASK:
		return TMR_ABORT_TASK;
	case SRP_TSK_ABORT_TASK_SET:
		return TMR_ABORT_TASK_SET;
	case SRP_TSK_CLEAR_TASK_SET:
		return TMR_CLEAR_TASK_SET;
	case SRP_TSK_LUN_RESET:
		return TMR_LUN_RESET;
	case SRP_TSK_CLEAR_ACA:
		return TMR_CLEAR_ACA;
	default:
		return -1;
	}
}

/**
 * srpt_handle_tsk_mgmt() - Process an SRP_TSK_MGMT information unit.
 *
 * Returns 0 if and only if the request will be processed by the target core.
 *
 * For more information about SRP_TSK_MGMT information units, see also section
 * 6.7 in the SRP r16a document.
 */
static void srpt_handle_tsk_mgmt(struct srpt_rdma_ch *ch,
				 struct srpt_recv_ioctx *recv_ioctx,
				 struct srpt_send_ioctx *send_ioctx)
{
	struct srp_tsk_mgmt *srp_tsk;
	struct se_cmd *cmd;
	uint64_t unpacked_lun;
	int tcm_tmr;
	int res;

	BUG_ON(!send_ioctx);

	srp_tsk = recv_ioctx->ioctx.buf;
	cmd = &send_ioctx->cmd;

	pr_debug("recv tsk_mgmt fn %d for task_tag %lld and cmd tag %lld"
		 " cm_id %p sess %p\n", srp_tsk->tsk_mgmt_func,
		 srp_tsk->task_tag, srp_tsk->tag, ch->cm_id, ch->sess);

	srpt_set_cmd_state(send_ioctx, SRPT_STATE_MGMT);
	send_ioctx->tag = srp_tsk->tag;
	tcm_tmr = srp_tmr_to_tcm(srp_tsk->tsk_mgmt_func);
	if (tcm_tmr < 0) {
		send_ioctx->cmd.se_tmr_req->response =
			TMR_TASK_MGMT_FUNCTION_NOT_SUPPORTED;
		goto fail;
	}
	transport_init_se_cmd(&send_ioctx->cmd, &srpt_target->tf_ops, ch->sess,
			0, DMA_NONE, MSG_SIMPLE_TAG, send_ioctx->sense_data);

	res = core_tmr_alloc_req(cmd, NULL, tcm_tmr, GFP_KERNEL);
	if (res < 0) {
		send_ioctx->cmd.se_tmr_req->response = TMR_FUNCTION_REJECTED;
		goto fail;
	}

	unpacked_lun = srpt_unpack_lun((uint8_t *)&srp_tsk->lun,
				       sizeof(srp_tsk->lun));
	res = transport_lookup_tmr_lun(&send_ioctx->cmd, unpacked_lun);
	if (res) {
		pr_debug("rejecting TMR for LUN %lld\n", unpacked_lun);
		send_ioctx->cmd.se_tmr_req->response = TMR_LUN_DOES_NOT_EXIST;
		goto fail;
	}

	if (srp_tsk->tsk_mgmt_func == SRP_TSK_ABORT_TASK)
		srpt_rx_mgmt_fn_tag(send_ioctx, srp_tsk->task_tag);

	transport_generic_handle_tmr(&send_ioctx->cmd);
	return;
fail:
	transport_send_check_condition_and_sense(cmd, 0, 0); // XXX:
}

/**
 * srpt_handle_new_iu() - Process a newly received information unit.
 * @ch:    RDMA channel through which the information unit has been received.
 * @ioctx: SRPT I/O context associated with the information unit.
 */
static void srpt_handle_new_iu(struct srpt_rdma_ch *ch,
			       struct srpt_recv_ioctx *recv_ioctx,
			       struct srpt_send_ioctx *send_ioctx)
{
	struct srp_cmd *srp_cmd;
	enum rdma_ch_state ch_state;

	BUG_ON(!ch);
	BUG_ON(!recv_ioctx);

	ib_dma_sync_single_for_cpu(ch->sport->sdev->device,
				   recv_ioctx->ioctx.dma, srp_max_req_size,
				   DMA_FROM_DEVICE);

	ch_state = srpt_get_ch_state(ch);
	if (unlikely(ch_state == CH_CONNECTING)) {
		list_add_tail(&recv_ioctx->wait_list, &ch->cmd_wait_list);
		goto out;
	}

	if (unlikely(ch_state != CH_LIVE))
		goto out;

	srp_cmd = recv_ioctx->ioctx.buf;
	if (srp_cmd->opcode == SRP_CMD || srp_cmd->opcode == SRP_TSK_MGMT) {
		if (!send_ioctx)
			send_ioctx = srpt_get_send_ioctx(ch);
		if (unlikely(!send_ioctx)) {
			list_add_tail(&recv_ioctx->wait_list,
				      &ch->cmd_wait_list);
			goto out;
		}
	}

	switch (srp_cmd->opcode) {
	case SRP_CMD:
		srpt_handle_cmd(ch, recv_ioctx, send_ioctx);
		break;
	case SRP_TSK_MGMT:
		srpt_handle_tsk_mgmt(ch, recv_ioctx, send_ioctx);
		break;
	case SRP_I_LOGOUT:
		printk(KERN_ERR "Not yet implemented: SRP_I_LOGOUT\n");
		break;
	case SRP_CRED_RSP:
		pr_debug("received SRP_CRED_RSP\n");
		break;
	case SRP_AER_RSP:
		pr_debug("received SRP_AER_RSP\n");
		break;
	case SRP_RSP:
		printk(KERN_ERR "Received SRP_RSP\n");
		break;
	default:
		printk(KERN_ERR "received IU with unknown opcode 0x%x\n",
		       srp_cmd->opcode);
		break;
	}

	srpt_post_recv(ch->sport->sdev, recv_ioctx);
out:
	return;
}

static void srpt_process_rcv_completion(struct ib_cq *cq,
					struct srpt_rdma_ch *ch,
					struct ib_wc *wc)
{
	struct srpt_device *sdev = ch->sport->sdev;
	struct srpt_recv_ioctx *ioctx;
	u32 index;

	index = idx_from_wr_id(wc->wr_id);
	if (wc->status == IB_WC_SUCCESS) {
		int req_lim;

		req_lim = atomic_dec_return(&ch->req_lim);
		if (unlikely(req_lim < 0))
			printk(KERN_ERR "req_lim = %d < 0\n", req_lim);
		ioctx = sdev->ioctx_ring[index];
		srpt_handle_new_iu(ch, ioctx, NULL);
	} else {
		printk(KERN_INFO "receiving failed for idx %u with status %d\n",
		       index, wc->status);
	}
}

/**
 * srpt_process_send_completion() - Process an IB send completion.
 *
 * Note: Although this has not yet been observed during tests, at least in
 * theory it is possible that the srpt_get_send_ioctx() call invoked by
 * srpt_handle_new_iu() fails. This is possible because the req_lim_delta
 * value in each response is set to one, and it is possible that this response
 * makes the initiator send a new request before the send completion for that
 * response has been processed. This could e.g. happen if the call to
 * srpt_put_send_iotcx() is delayed because of a higher priority interrupt or
 * if IB retransmission causes generation of the send completion to be
 * delayed. Incoming information units for which srpt_get_send_ioctx() fails
 * are queued on cmd_wait_list. The code below processes these delayed
 * requests one at a time.
 */
static void srpt_process_send_completion(struct ib_cq *cq,
					 struct srpt_rdma_ch *ch,
					 struct ib_wc *wc)
{
	struct srpt_send_ioctx *send_ioctx;
	uint32_t index;
	enum srpt_opcode opcode;

	index = idx_from_wr_id(wc->wr_id);
	opcode = opcode_from_wr_id(wc->wr_id);
	send_ioctx = ch->ioctx_ring[index];
	if (wc->status == IB_WC_SUCCESS) {
		if (opcode == SRPT_SEND)
			srpt_handle_send_comp(ch, send_ioctx);
		else {
			WARN_ON(opcode != SRPT_RDMA_ABORT &&
				wc->opcode != IB_WC_RDMA_READ);
			srpt_handle_rdma_comp(ch, send_ioctx, opcode);
		}
	} else {
		if (opcode == SRPT_SEND) {
			printk(KERN_INFO "sending response for idx %u failed"
			       " with status %d\n", index, wc->status);
			srpt_handle_send_err_comp(ch, wc->wr_id);
		} else if (opcode != SRPT_RDMA_MID) {
			printk(KERN_INFO "RDMA t %d for idx %u failed with"
				" status %d", opcode, index, wc->status);
			srpt_handle_rdma_err_comp(ch, send_ioctx, opcode);
		}
	}

	while (unlikely(opcode == SRPT_SEND
			&& !list_empty(&ch->cmd_wait_list)
			&& srpt_get_ch_state(ch) == CH_LIVE
			&& (send_ioctx = srpt_get_send_ioctx(ch)) != NULL)) {
		struct srpt_recv_ioctx *recv_ioctx;

		recv_ioctx = list_first_entry(&ch->cmd_wait_list,
					      struct srpt_recv_ioctx,
					      wait_list);
		list_del(&recv_ioctx->wait_list);
		srpt_handle_new_iu(ch, recv_ioctx, send_ioctx);
	}
}

static void srpt_process_completion(struct ib_cq *cq, struct srpt_rdma_ch *ch)
{
	struct ib_wc *const wc = ch->wc;
	int i, n;

	WARN_ON(cq != ch->cq);

	ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);
	while ((n = ib_poll_cq(cq, ARRAY_SIZE(ch->wc), wc)) > 0) {
		for (i = 0; i < n; i++) {
			if (opcode_from_wr_id(wc[i].wr_id) == SRPT_RECV)
				srpt_process_rcv_completion(cq, ch, &wc[i]);
			else
				srpt_process_send_completion(cq, ch, &wc[i]);
		}
	}
}

/**
 * srpt_completion() - IB completion queue callback function.
 *
 * Notes:
 * - It is guaranteed that a completion handler will never be invoked
 *   concurrently on two different CPUs for the same completion queue. See also
 *   Documentation/infiniband/core_locking.txt and the implementation of
 *   handle_edge_irq() in kernel/irq/chip.c.
 * - When threaded IRQs are enabled, completion handlers are invoked in thread
 *   context instead of interrupt context.
 */
static void srpt_completion(struct ib_cq *cq, void *ctx)
{
	struct srpt_rdma_ch *ch = ctx;

	wake_up_interruptible(&ch->wait_queue);
}

static int srpt_compl_thread(void *arg)
{
	struct srpt_rdma_ch *ch;

	/* Hibernation / freezing of the SRPT kernel thread is not supported. */
	current->flags |= PF_NOFREEZE;

	ch = arg;
	BUG_ON(!ch);
	printk(KERN_INFO "Session %s: kernel thread %s (PID %d) started\n",
	       ch->sess_name, ch->thread->comm, current->pid);
	while (!kthread_should_stop()) {
		wait_event_interruptible(ch->wait_queue,
			(srpt_process_completion(ch->cq, ch),
			 kthread_should_stop()));
	}
	printk(KERN_INFO "Session %s: kernel thread %s (PID %d) stopped\n",
	       ch->sess_name, ch->thread->comm, current->pid);
	return 0;
}

/**
 * srpt_create_ch_ib() - Create receive and send completion queues.
 */
static int srpt_create_ch_ib(struct srpt_rdma_ch *ch)
{
	struct ib_qp_init_attr *qp_init;
	struct srpt_port *sport = ch->sport;
	struct srpt_device *sdev = sport->sdev;
	u32 srp_sq_size = sport->port_attrib.srp_sq_size;
	int ret;

	WARN_ON(ch->rq_size < 1);

	ret = -ENOMEM;
	qp_init = kzalloc(sizeof *qp_init, GFP_KERNEL);
	if (!qp_init)
		goto out;

	ch->cq = ib_create_cq(sdev->device, srpt_completion, NULL, ch,
			      ch->rq_size + srp_sq_size, 0);
	if (IS_ERR(ch->cq)) {
		ret = PTR_ERR(ch->cq);
		printk(KERN_ERR "failed to create CQ cqe= %d ret= %d\n",
		       ch->rq_size + srp_sq_size, ret);
		goto out;
	}

	qp_init->qp_context = (void *)ch;
	qp_init->event_handler
		= (void(*)(struct ib_event *, void*))srpt_qp_event;
	qp_init->send_cq = ch->cq;
	qp_init->recv_cq = ch->cq;
	qp_init->srq = sdev->srq;
	qp_init->sq_sig_type = IB_SIGNAL_REQ_WR;
	qp_init->qp_type = IB_QPT_RC;
	qp_init->cap.max_send_wr = srp_sq_size;
	qp_init->cap.max_send_sge = SRPT_DEF_SG_PER_WQE;

	ch->qp = ib_create_qp(sdev->pd, qp_init);
	if (IS_ERR(ch->qp)) {
		ret = PTR_ERR(ch->qp);
		printk(KERN_ERR "failed to create_qp ret= %d\n", ret);
		goto err_destroy_cq;
	}

	atomic_set(&ch->sq_wr_avail, qp_init->cap.max_send_wr);

	pr_debug("%s: max_cqe= %d max_sge= %d sq_size = %d cm_id= %p\n",
		 __func__, ch->cq->cqe, qp_init->cap.max_send_sge,
		 qp_init->cap.max_send_wr, ch->cm_id);

	ret = srpt_init_ch_qp(ch, ch->qp);
	if (ret)
		goto err_destroy_qp;

	init_waitqueue_head(&ch->wait_queue);

	pr_debug("creating thread for session %s\n", ch->sess_name);

	ch->thread = kthread_run(srpt_compl_thread, ch, "ib_srpt_compl");
	if (IS_ERR(ch->thread)) {
		printk(KERN_ERR "failed to create kernel thread %ld\n",
		       PTR_ERR(ch->thread));
		ch->thread = NULL;
		goto err_destroy_qp;
	}

out:
	kfree(qp_init);
	return ret;

err_destroy_qp:
	ib_destroy_qp(ch->qp);
err_destroy_cq:
	ib_destroy_cq(ch->cq);
	goto out;
}

static void srpt_destroy_ch_ib(struct srpt_rdma_ch *ch)
{
	if (ch->thread)
		kthread_stop(ch->thread);

	ib_destroy_qp(ch->qp);
	ib_destroy_cq(ch->cq);
}

/**
 * __srpt_close_ch() - Close an RDMA channel by setting the QP error state.
 *
 * Reset the QP and make sure all resources associated with the channel will
 * be deallocated at an appropriate time.
 *
 * Note: The caller must hold ch->sport->sdev->spinlock.
 */
static void __srpt_close_ch(struct srpt_rdma_ch *ch)
{
	struct srpt_device *sdev;
	enum rdma_ch_state prev_state;
	unsigned long flags;

	sdev = ch->sport->sdev;

	spin_lock_irqsave(&ch->spinlock, flags);
	prev_state = ch->state;
	switch (prev_state) {
	case CH_CONNECTING:
	case CH_LIVE:
		ch->state = CH_DISCONNECTING;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&ch->spinlock, flags);

	switch (prev_state) {
	case CH_CONNECTING:
		ib_send_cm_rej(ch->cm_id, IB_CM_REJ_NO_RESOURCES, NULL, 0,
			       NULL, 0);
		/* fall through */
	case CH_LIVE:
		if (ib_send_cm_dreq(ch->cm_id, NULL, 0) < 0)
			printk(KERN_ERR "sending CM DREQ failed.\n");
		break;
	case CH_DISCONNECTING:
		break;
	case CH_DRAINING:
	case CH_RELEASING:
		break;
	}
}

/**
 * srpt_close_ch() - Close an RDMA channel.
 */
static void srpt_close_ch(struct srpt_rdma_ch *ch)
{
	struct srpt_device *sdev;

	sdev = ch->sport->sdev;
	spin_lock_irq(&sdev->spinlock);
	__srpt_close_ch(ch);
	spin_unlock_irq(&sdev->spinlock);
}

/**
 * srpt_drain_channel() - Drain a channel by resetting the IB queue pair.
 * @cm_id: Pointer to the CM ID of the channel to be drained.
 *
 * Note: Must be called from inside srpt_cm_handler to avoid a race between
 * accessing sdev->spinlock and the call to kfree(sdev) in srpt_remove_one()
 * (the caller of srpt_cm_handler holds the cm_id spinlock; srpt_remove_one()
 * waits until all target sessions for the associated IB device have been
 * unregistered and target session registration involves a call to
 * ib_destroy_cm_id(), which locks the cm_id spinlock and hence waits until
 * this function has finished).
 */
static void srpt_drain_channel(struct ib_cm_id *cm_id)
{
	struct srpt_device *sdev;
	struct srpt_rdma_ch *ch;
	int ret;
	bool do_reset = false;

	WARN_ON_ONCE(irqs_disabled());

	sdev = cm_id->context;
	BUG_ON(!sdev);
	spin_lock_irq(&sdev->spinlock);
	list_for_each_entry(ch, &sdev->rch_list, list) {
		if (ch->cm_id == cm_id) {
			do_reset = srpt_test_and_set_ch_state(ch,
					CH_CONNECTING, CH_DRAINING) ||
				   srpt_test_and_set_ch_state(ch,
					CH_LIVE, CH_DRAINING) ||
				   srpt_test_and_set_ch_state(ch,
					CH_DISCONNECTING, CH_DRAINING);
			break;
		}
	}
	spin_unlock_irq(&sdev->spinlock);

	if (do_reset) {
		ret = srpt_ch_qp_err(ch);
		if (ret < 0)
			printk(KERN_ERR "Setting queue pair in error state"
			       " failed: %d\n", ret);
	}
}

/**
 * srpt_find_channel() - Look up an RDMA channel.
 * @cm_id: Pointer to the CM ID of the channel to be looked up.
 *
 * Return NULL if no matching RDMA channel has been found.
 */
static struct srpt_rdma_ch *srpt_find_channel(struct srpt_device *sdev,
					      struct ib_cm_id *cm_id)
{
	struct srpt_rdma_ch *ch;
	bool found;

	WARN_ON_ONCE(irqs_disabled());
	BUG_ON(!sdev);

	found = false;
	spin_lock_irq(&sdev->spinlock);
	list_for_each_entry(ch, &sdev->rch_list, list) {
		if (ch->cm_id == cm_id) {
			found = true;
			break;
		}
	}
	spin_unlock_irq(&sdev->spinlock);

	return found ? ch : NULL;
}

/**
 * srpt_release_channel() - Release channel resources.
 *
 * Schedules the actual release because:
 * - Calling the ib_destroy_cm_id() call from inside an IB CM callback would
 *   trigger a deadlock.
 * - It is not safe to call TCM transport_* functions from interrupt context.
 */
static void srpt_release_channel(struct srpt_rdma_ch *ch)
{
	schedule_work(&ch->release_work);
}

static void srpt_release_channel_work(struct work_struct *w)
{
	struct srpt_rdma_ch *ch;
	struct srpt_device *sdev;
	struct se_session *se_sess;

	ch = container_of(w, struct srpt_rdma_ch, release_work);
	pr_debug("ch = %p; ch->sess = %p; release_done = %p\n", ch, ch->sess,
		 ch->release_done);

	sdev = ch->sport->sdev;
	BUG_ON(!sdev);

	se_sess = ch->sess;
	BUG_ON(!se_sess);

	target_wait_for_sess_cmds(se_sess, 0);

	transport_deregister_session_configfs(se_sess);
	transport_deregister_session(se_sess);
	ch->sess = NULL;

	srpt_destroy_ch_ib(ch);

	srpt_free_ioctx_ring((struct srpt_ioctx **)ch->ioctx_ring,
			     ch->sport->sdev, ch->rq_size,
			     ch->rsp_size, DMA_TO_DEVICE);

	spin_lock_irq(&sdev->spinlock);
	list_del(&ch->list);
	spin_unlock_irq(&sdev->spinlock);

	ib_destroy_cm_id(ch->cm_id);

	if (ch->release_done)
		complete(ch->release_done);

	wake_up(&sdev->ch_releaseQ);

	kfree(ch);
}

static struct srpt_node_acl *__srpt_lookup_acl(struct srpt_port *sport,
					       u8 i_port_id[16])
{
	struct srpt_node_acl *nacl;

	list_for_each_entry(nacl, &sport->port_acl_list, list)
		if (memcmp(nacl->i_port_id, i_port_id,
			   sizeof(nacl->i_port_id)) == 0)
			return nacl;

	return NULL;
}

static struct srpt_node_acl *srpt_lookup_acl(struct srpt_port *sport,
					     u8 i_port_id[16])
{
	struct srpt_node_acl *nacl;

	spin_lock_irq(&sport->port_acl_lock);
	nacl = __srpt_lookup_acl(sport, i_port_id);
	spin_unlock_irq(&sport->port_acl_lock);

	return nacl;
}

/**
 * srpt_cm_req_recv() - Process the event IB_CM_REQ_RECEIVED.
 *
 * Ownership of the cm_id is transferred to the target session if this
 * functions returns zero. Otherwise the caller remains the owner of cm_id.
 */
static int srpt_cm_req_recv(struct ib_cm_id *cm_id,
			    struct ib_cm_req_event_param *param,
			    void *private_data)
{
	struct srpt_device *sdev = cm_id->context;
	struct srpt_port *sport = &sdev->port[param->port - 1];
	struct srp_login_req *req;
	struct srp_login_rsp *rsp;
	struct srp_login_rej *rej;
	struct ib_cm_rep_param *rep_param;
	struct srpt_rdma_ch *ch, *tmp_ch;
	struct srpt_node_acl *nacl;
	u32 it_iu_len;
	int i;
	int ret = 0;

	WARN_ON_ONCE(irqs_disabled());

	if (WARN_ON(!sdev || !private_data))
		return -EINVAL;

	req = (struct srp_login_req *)private_data;

	it_iu_len = be32_to_cpu(req->req_it_iu_len);

	printk(KERN_INFO "Received SRP_LOGIN_REQ with i_port_id 0x%llx:0x%llx,"
	       " t_port_id 0x%llx:0x%llx and it_iu_len %d on port %d"
	       " (guid=0x%llx:0x%llx)\n",
	       be64_to_cpu(*(__be64 *)&req->initiator_port_id[0]),
	       be64_to_cpu(*(__be64 *)&req->initiator_port_id[8]),
	       be64_to_cpu(*(__be64 *)&req->target_port_id[0]),
	       be64_to_cpu(*(__be64 *)&req->target_port_id[8]),
	       it_iu_len,
	       param->port,
	       be64_to_cpu(*(__be64 *)&sdev->port[param->port - 1].gid.raw[0]),
	       be64_to_cpu(*(__be64 *)&sdev->port[param->port - 1].gid.raw[8]));

	rsp = kzalloc(sizeof *rsp, GFP_KERNEL);
	rej = kzalloc(sizeof *rej, GFP_KERNEL);
	rep_param = kzalloc(sizeof *rep_param, GFP_KERNEL);

	if (!rsp || !rej || !rep_param) {
		ret = -ENOMEM;
		goto out;
	}

	if (it_iu_len > srp_max_req_size || it_iu_len < 64) {
		rej->reason = __constant_cpu_to_be32(
				SRP_LOGIN_REJ_REQ_IT_IU_LENGTH_TOO_LARGE);
		ret = -EINVAL;
		printk(KERN_ERR "rejected SRP_LOGIN_REQ because its"
		       " length (%d bytes) is out of range (%d .. %d)\n",
		       it_iu_len, 64, srp_max_req_size);
		goto reject;
	}

	if (!sport->enabled) {
		rej->reason = __constant_cpu_to_be32(
			     SRP_LOGIN_REJ_INSUFFICIENT_RESOURCES);
		ret = -EINVAL;
		printk(KERN_ERR "rejected SRP_LOGIN_REQ because the target port"
		       " has not yet been enabled\n");
		goto reject;
	}

	if ((req->req_flags & SRP_MTCH_ACTION) == SRP_MULTICHAN_SINGLE) {
		rsp->rsp_flags = SRP_LOGIN_RSP_MULTICHAN_NO_CHAN;

		spin_lock_irq(&sdev->spinlock);

		list_for_each_entry_safe(ch, tmp_ch, &sdev->rch_list, list) {
			if (!memcmp(ch->i_port_id, req->initiator_port_id, 16)
			    && !memcmp(ch->t_port_id, req->target_port_id, 16)
			    && param->port == ch->sport->port
			    && param->listen_id == ch->sport->sdev->cm_id
			    && ch->cm_id) {
				enum rdma_ch_state ch_state;

				ch_state = srpt_get_ch_state(ch);
				if (ch_state != CH_CONNECTING
				    && ch_state != CH_LIVE)
					continue;

				/* found an existing channel */
				pr_debug("Found existing channel %s"
					 " cm_id= %p state= %d\n",
					 ch->sess_name, ch->cm_id, ch_state);

				__srpt_close_ch(ch);

				rsp->rsp_flags =
					SRP_LOGIN_RSP_MULTICHAN_TERMINATED;
			}
		}

		spin_unlock_irq(&sdev->spinlock);

	} else
		rsp->rsp_flags = SRP_LOGIN_RSP_MULTICHAN_MAINTAINED;

	if (*(__be64 *)req->target_port_id != cpu_to_be64(srpt_service_guid)
	    || *(__be64 *)(req->target_port_id + 8) !=
	       cpu_to_be64(srpt_service_guid)) {
		rej->reason = __constant_cpu_to_be32(
				SRP_LOGIN_REJ_UNABLE_ASSOCIATE_CHANNEL);
		ret = -ENOMEM;
		printk(KERN_ERR "rejected SRP_LOGIN_REQ because it"
		       " has an invalid target port identifier.\n");
		goto reject;
	}

	ch = kzalloc(sizeof *ch, GFP_KERNEL);
	if (!ch) {
		rej->reason = __constant_cpu_to_be32(
					SRP_LOGIN_REJ_INSUFFICIENT_RESOURCES);
		printk(KERN_ERR "rejected SRP_LOGIN_REQ because no memory.\n");
		ret = -ENOMEM;
		goto reject;
	}

	INIT_WORK(&ch->release_work, srpt_release_channel_work);
	memcpy(ch->i_port_id, req->initiator_port_id, 16);
	memcpy(ch->t_port_id, req->target_port_id, 16);
	ch->sport = &sdev->port[param->port - 1];
	ch->cm_id = cm_id;
	/*
	 * Avoid QUEUE_FULL conditions by limiting the number of buffers used
	 * for the SRP protocol to the command queue size.
	 */
	ch->rq_size = SRPT_RQ_SIZE;
	spin_lock_init(&ch->spinlock);
	ch->state = CH_CONNECTING;
	INIT_LIST_HEAD(&ch->cmd_wait_list);
	ch->rsp_size = ch->sport->port_attrib.srp_max_rsp_size;

	ch->ioctx_ring = (struct srpt_send_ioctx **)
		srpt_alloc_ioctx_ring(ch->sport->sdev, ch->rq_size,
				      sizeof(*ch->ioctx_ring[0]),
				      ch->rsp_size, DMA_TO_DEVICE);
	if (!ch->ioctx_ring)
		goto free_ch;

	INIT_LIST_HEAD(&ch->free_list);
	for (i = 0; i < ch->rq_size; i++) {
		ch->ioctx_ring[i]->ch = ch;
		list_add_tail(&ch->ioctx_ring[i]->free_list, &ch->free_list);
	}

	ret = srpt_create_ch_ib(ch);
	if (ret) {
		rej->reason = __constant_cpu_to_be32(
				SRP_LOGIN_REJ_INSUFFICIENT_RESOURCES);
		printk(KERN_ERR "rejected SRP_LOGIN_REQ because creating"
		       " a new RDMA channel failed.\n");
		goto free_ring;
	}

	ret = srpt_ch_qp_rtr(ch, ch->qp);
	if (ret) {
		rej->reason = __constant_cpu_to_be32(
				SRP_LOGIN_REJ_INSUFFICIENT_RESOURCES);
		printk(KERN_ERR "rejected SRP_LOGIN_REQ because enabling"
		       " RTR failed (error code = %d)\n", ret);
		goto destroy_ib;
	}
	/*
	 * Use the initator port identifier as the session name.
	 */
	snprintf(ch->sess_name, sizeof(ch->sess_name), "0x%016llx%016llx",
			be64_to_cpu(*(__be64 *)ch->i_port_id),
			be64_to_cpu(*(__be64 *)(ch->i_port_id + 8)));

	pr_debug("registering session %s\n", ch->sess_name);

	nacl = srpt_lookup_acl(sport, ch->i_port_id);
	if (!nacl) {
		printk(KERN_INFO "Rejected login because no ACL has been"
		       " configured yet for initiator %s.\n", ch->sess_name);
		rej->reason = __constant_cpu_to_be32(
				SRP_LOGIN_REJ_CHANNEL_LIMIT_REACHED);
		goto destroy_ib;
	}

	ch->sess = transport_init_session();
	if (IS_ERR(ch->sess)) {
		rej->reason = __constant_cpu_to_be32(
				SRP_LOGIN_REJ_INSUFFICIENT_RESOURCES);
		pr_debug("Failed to create session\n");
		goto deregister_session;
	}
	ch->sess->se_node_acl = &nacl->nacl;
	transport_register_session(&sport->port_tpg_1, &nacl->nacl, ch->sess, ch);

	pr_debug("Establish connection sess=%p name=%s cm_id=%p\n", ch->sess,
		 ch->sess_name, ch->cm_id);

	/* create srp_login_response */
	rsp->opcode = SRP_LOGIN_RSP;
	rsp->tag = req->tag;
	rsp->max_it_iu_len = req->req_it_iu_len;
	rsp->max_ti_iu_len = req->req_it_iu_len;
	ch->max_ti_iu_len = it_iu_len;
	rsp->buf_fmt = __constant_cpu_to_be16(SRP_BUF_FORMAT_DIRECT
					      | SRP_BUF_FORMAT_INDIRECT);
	rsp->req_lim_delta = cpu_to_be32(ch->rq_size);
	atomic_set(&ch->req_lim, ch->rq_size);
	atomic_set(&ch->req_lim_delta, 0);

	/* create cm reply */
	rep_param->qp_num = ch->qp->qp_num;
	rep_param->private_data = (void *)rsp;
	rep_param->private_data_len = sizeof *rsp;
	rep_param->rnr_retry_count = 7;
	rep_param->flow_control = 1;
	rep_param->failover_accepted = 0;
	rep_param->srq = 1;
	rep_param->responder_resources = 4;
	rep_param->initiator_depth = 4;

	ret = ib_send_cm_rep(cm_id, rep_param);
	if (ret) {
		printk(KERN_ERR "sending SRP_LOGIN_REQ response failed"
		       " (error code = %d)\n", ret);
		goto release_channel;
	}

	spin_lock_irq(&sdev->spinlock);
	list_add_tail(&ch->list, &sdev->rch_list);
	spin_unlock_irq(&sdev->spinlock);

	goto out;

release_channel:
	srpt_set_ch_state(ch, CH_RELEASING);
	transport_deregister_session_configfs(ch->sess);

deregister_session:
	transport_deregister_session(ch->sess);
	ch->sess = NULL;

destroy_ib:
	srpt_destroy_ch_ib(ch);

free_ring:
	srpt_free_ioctx_ring((struct srpt_ioctx **)ch->ioctx_ring,
			     ch->sport->sdev, ch->rq_size,
			     ch->rsp_size, DMA_TO_DEVICE);
free_ch:
	kfree(ch);

reject:
	rej->opcode = SRP_LOGIN_REJ;
	rej->tag = req->tag;
	rej->buf_fmt = __constant_cpu_to_be16(SRP_BUF_FORMAT_DIRECT
					      | SRP_BUF_FORMAT_INDIRECT);

	ib_send_cm_rej(cm_id, IB_CM_REJ_CONSUMER_DEFINED, NULL, 0,
			     (void *)rej, sizeof *rej);

out:
	kfree(rep_param);
	kfree(rsp);
	kfree(rej);

	return ret;
}

static void srpt_cm_rej_recv(struct ib_cm_id *cm_id)
{
	printk(KERN_INFO "Received IB REJ for cm_id %p.\n", cm_id);
	srpt_drain_channel(cm_id);
}

/**
 * srpt_cm_rtu_recv() - Process an IB_CM_RTU_RECEIVED or USER_ESTABLISHED event.
 *
 * An IB_CM_RTU_RECEIVED message indicates that the connection is established
 * and that the recipient may begin transmitting (RTU = ready to use).
 */
static void srpt_cm_rtu_recv(struct ib_cm_id *cm_id)
{
	struct srpt_rdma_ch *ch;
	int ret;

	ch = srpt_find_channel(cm_id->context, cm_id);
	BUG_ON(!ch);

	if (srpt_test_and_set_ch_state(ch, CH_CONNECTING, CH_LIVE)) {
		struct srpt_recv_ioctx *ioctx, *ioctx_tmp;

		ret = srpt_ch_qp_rts(ch, ch->qp);

		list_for_each_entry_safe(ioctx, ioctx_tmp, &ch->cmd_wait_list,
					 wait_list) {
			list_del(&ioctx->wait_list);
			srpt_handle_new_iu(ch, ioctx, NULL);
		}
		if (ret)
			srpt_close_ch(ch);
	}
}

static void srpt_cm_timewait_exit(struct ib_cm_id *cm_id)
{
	printk(KERN_INFO "Received IB TimeWait exit for cm_id %p.\n", cm_id);
	srpt_drain_channel(cm_id);
}

static void srpt_cm_rep_error(struct ib_cm_id *cm_id)
{
	printk(KERN_INFO "Received IB REP error for cm_id %p.\n", cm_id);
	srpt_drain_channel(cm_id);
}

/**
 * srpt_cm_dreq_recv() - Process reception of a DREQ message.
 */
static void srpt_cm_dreq_recv(struct ib_cm_id *cm_id)
{
	struct srpt_rdma_ch *ch;
	unsigned long flags;
	bool send_drep = false;

	ch = srpt_find_channel(cm_id->context, cm_id);
	BUG_ON(!ch);

	pr_debug("cm_id= %p ch->state= %d\n", cm_id, srpt_get_ch_state(ch));

	spin_lock_irqsave(&ch->spinlock, flags);
	switch (ch->state) {
	case CH_CONNECTING:
	case CH_LIVE:
		send_drep = true;
		ch->state = CH_DISCONNECTING;
		break;
	case CH_DISCONNECTING:
	case CH_DRAINING:
	case CH_RELEASING:
		WARN(true, "unexpected channel state %d\n", ch->state);
		break;
	}
	spin_unlock_irqrestore(&ch->spinlock, flags);

	if (send_drep) {
		if (ib_send_cm_drep(ch->cm_id, NULL, 0) < 0)
			printk(KERN_ERR "Sending IB DREP failed.\n");
		printk(KERN_INFO "Received DREQ and sent DREP for session %s.\n",
		       ch->sess_name);
	}
}

/**
 * srpt_cm_drep_recv() - Process reception of a DREP message.
 */
static void srpt_cm_drep_recv(struct ib_cm_id *cm_id)
{
	printk(KERN_INFO "Received InfiniBand DREP message for cm_id %p.\n",
	       cm_id);
	srpt_drain_channel(cm_id);
}

/**
 * srpt_cm_handler() - IB connection manager callback function.
 *
 * A non-zero return value will cause the caller destroy the CM ID.
 *
 * Note: srpt_cm_handler() must only return a non-zero value when transferring
 * ownership of the cm_id to a channel by srpt_cm_req_recv() failed. Returning
 * a non-zero value in any other case will trigger a race with the
 * ib_destroy_cm_id() call in srpt_release_channel().
 */
static int srpt_cm_handler(struct ib_cm_id *cm_id, struct ib_cm_event *event)
{
	int ret;

	ret = 0;
	switch (event->event) {
	case IB_CM_REQ_RECEIVED:
		ret = srpt_cm_req_recv(cm_id, &event->param.req_rcvd,
				       event->private_data);
		break;
	case IB_CM_REJ_RECEIVED:
		srpt_cm_rej_recv(cm_id);
		break;
	case IB_CM_RTU_RECEIVED:
	case IB_CM_USER_ESTABLISHED:
		srpt_cm_rtu_recv(cm_id);
		break;
	case IB_CM_DREQ_RECEIVED:
		srpt_cm_dreq_recv(cm_id);
		break;
	case IB_CM_DREP_RECEIVED:
		srpt_cm_drep_recv(cm_id);
		break;
	case IB_CM_TIMEWAIT_EXIT:
		srpt_cm_timewait_exit(cm_id);
		break;
	case IB_CM_REP_ERROR:
		srpt_cm_rep_error(cm_id);
		break;
	case IB_CM_DREQ_ERROR:
		printk(KERN_INFO "Received IB DREQ ERROR event.\n");
		break;
	case IB_CM_MRA_RECEIVED:
		printk(KERN_INFO "Received IB MRA event\n");
		break;
	default:
		printk(KERN_ERR "received unrecognized IB CM event %d\n",
		       event->event);
		break;
	}

	return ret;
}

/**
 * srpt_perform_rdmas() - Perform IB RDMA.
 *
 * Returns zero upon success or a negative number upon failure.
 */
static int srpt_perform_rdmas(struct srpt_rdma_ch *ch,
			      struct srpt_send_ioctx *ioctx)
{
	struct ib_send_wr wr;
	struct ib_send_wr *bad_wr;
	struct rdma_iu *riu;
	int i;
	int ret;
	int sq_wr_avail;
	enum dma_data_direction dir;
	const int n_rdma = ioctx->n_rdma;

	dir = ioctx->cmd.data_direction;
	if (dir == DMA_TO_DEVICE) {
		/* write */
		ret = -ENOMEM;
		sq_wr_avail = atomic_sub_return(n_rdma, &ch->sq_wr_avail);
		if (sq_wr_avail < 0) {
			printk(KERN_WARNING "IB send queue full (needed %d)\n",
			       n_rdma);
			goto out;
		}
	}

	ioctx->rdma_aborted = false;
	ret = 0;
	riu = ioctx->rdma_ius;
	memset(&wr, 0, sizeof wr);

	for (i = 0; i < n_rdma; ++i, ++riu) {
		if (dir == DMA_FROM_DEVICE) {
			wr.opcode = IB_WR_RDMA_WRITE;
			wr.wr_id = encode_wr_id(i == n_rdma - 1 ?
						SRPT_RDMA_WRITE_LAST :
						SRPT_RDMA_MID,
						ioctx->ioctx.index);
		} else {
			wr.opcode = IB_WR_RDMA_READ;
			wr.wr_id = encode_wr_id(i == n_rdma - 1 ?
						SRPT_RDMA_READ_LAST :
						SRPT_RDMA_MID,
						ioctx->ioctx.index);
		}
		wr.next = NULL;
		wr.wr.rdma.remote_addr = riu->raddr;
		wr.wr.rdma.rkey = riu->rkey;
		wr.num_sge = riu->sge_cnt;
		wr.sg_list = riu->sge;

		/* only get completion event for the last rdma write */
		if (i == (n_rdma - 1) && dir == DMA_TO_DEVICE)
			wr.send_flags = IB_SEND_SIGNALED;

		ret = ib_post_send(ch->qp, &wr, &bad_wr);
		if (ret)
			break;
	}

	if (ret)
		printk(KERN_ERR "%s[%d]: ib_post_send() returned %d for %d/%d",
				 __func__, __LINE__, ret, i, n_rdma);
	if (ret && i > 0) {
		wr.num_sge = 0;
		wr.wr_id = encode_wr_id(SRPT_RDMA_ABORT, ioctx->ioctx.index);
		wr.send_flags = IB_SEND_SIGNALED;
		while (ch->state == CH_LIVE &&
			ib_post_send(ch->qp, &wr, &bad_wr) != 0) {
			printk(KERN_INFO "Trying to abort failed RDMA transfer [%d]",
				ioctx->ioctx.index);
			msleep(1000);
		}
		while (ch->state != CH_RELEASING && !ioctx->rdma_aborted) {
			printk(KERN_INFO "Waiting until RDMA abort finished [%d]",
				ioctx->ioctx.index);
			msleep(1000);
		}
	}
out:
	if (unlikely(dir == DMA_TO_DEVICE && ret < 0))
		atomic_add(n_rdma, &ch->sq_wr_avail);
	return ret;
}

/**
 * srpt_xfer_data() - Start data transfer from initiator to target.
 */
static int srpt_xfer_data(struct srpt_rdma_ch *ch,
			  struct srpt_send_ioctx *ioctx)
{
	int ret;

	ret = srpt_map_sg_to_ib_sge(ch, ioctx);
	if (ret) {
		printk(KERN_ERR "%s[%d] ret=%d\n", __func__, __LINE__, ret);
		goto out;
	}

	ret = srpt_perform_rdmas(ch, ioctx);
	if (ret) {
		if (ret == -EAGAIN || ret == -ENOMEM)
			printk(KERN_INFO "%s[%d] queue full -- ret=%d\n",
				   __func__, __LINE__, ret);
		else
			printk(KERN_ERR "%s[%d] fatal error -- ret=%d\n",
			       __func__, __LINE__, ret);
		goto out_unmap;
	}

out:
	return ret;
out_unmap:
	srpt_unmap_sg_to_ib_sge(ch, ioctx);
	goto out;
}

static int srpt_write_pending_status(struct se_cmd *se_cmd)
{
	struct srpt_send_ioctx *ioctx;

	ioctx = container_of(se_cmd, struct srpt_send_ioctx, cmd);
	return srpt_get_cmd_state(ioctx) == SRPT_STATE_NEED_DATA;
}

/*
 * srpt_write_pending() - Start data transfer from initiator to target (write).
 */
static int srpt_write_pending(struct se_cmd *se_cmd)
{
	struct srpt_rdma_ch *ch;
	struct srpt_send_ioctx *ioctx;
	enum srpt_command_state new_state;
	enum rdma_ch_state ch_state;
	int ret;

	ioctx = container_of(se_cmd, struct srpt_send_ioctx, cmd);

	new_state = srpt_set_cmd_state(ioctx, SRPT_STATE_NEED_DATA);
	WARN_ON(new_state == SRPT_STATE_DONE);

	ch = ioctx->ch;
	BUG_ON(!ch);

	ch_state = srpt_get_ch_state(ch);
	switch (ch_state) {
	case CH_CONNECTING:
		WARN(true, "unexpected channel state %d\n", ch_state);
		ret = -EINVAL;
		goto out;
	case CH_LIVE:
		break;
	case CH_DISCONNECTING:
	case CH_DRAINING:
	case CH_RELEASING:
		pr_debug("cmd with tag %lld: channel disconnecting\n",
			 ioctx->tag);
		srpt_set_cmd_state(ioctx, SRPT_STATE_DATA_IN);
		ret = -EINVAL;
		goto out;
	}
	ret = srpt_xfer_data(ch, ioctx);

out:
	return ret;
}

static u8 tcm_to_srp_tsk_mgmt_status(const int tcm_mgmt_status)
{
	switch (tcm_mgmt_status) {
	case TMR_FUNCTION_COMPLETE:
		return SRP_TSK_MGMT_SUCCESS;
	case TMR_FUNCTION_REJECTED:
		return SRP_TSK_MGMT_FUNC_NOT_SUPP;
	}
	return SRP_TSK_MGMT_FAILED;
}

/**
 * srpt_queue_response() - Transmits the response to a SCSI command.
 *
 * Callback function called by the TCM core. Must not block since it can be
 * invoked on the context of the IB completion handler.
 */
static int srpt_queue_response(struct se_cmd *cmd)
{
	struct srpt_rdma_ch *ch;
	struct srpt_send_ioctx *ioctx;
	enum srpt_command_state state;
	unsigned long flags;
	int ret;
	enum dma_data_direction dir;
	int resp_len;
	u8 srp_tm_status;

	ret = 0;

	ioctx = container_of(cmd, struct srpt_send_ioctx, cmd);
	ch = ioctx->ch;
	BUG_ON(!ch);

	spin_lock_irqsave(&ioctx->spinlock, flags);
	state = ioctx->state;
	switch (state) {
	case SRPT_STATE_NEW:
	case SRPT_STATE_DATA_IN:
		ioctx->state = SRPT_STATE_CMD_RSP_SENT;
		break;
	case SRPT_STATE_MGMT:
		ioctx->state = SRPT_STATE_MGMT_RSP_SENT;
		break;
	default:
		WARN(true, "ch %p; cmd %d: unexpected command state %d\n",
			ch, ioctx->ioctx.index, ioctx->state);
		break;
	}
	spin_unlock_irqrestore(&ioctx->spinlock, flags);

	if (unlikely(transport_check_aborted_status(&ioctx->cmd, false)
		     || WARN_ON_ONCE(state == SRPT_STATE_CMD_RSP_SENT))) {
		atomic_inc(&ch->req_lim_delta);
		srpt_abort_cmd(ioctx);
		goto out;
	}

	dir = ioctx->cmd.data_direction;

	/* For read commands, transfer the data to the initiator. */
	if (dir == DMA_FROM_DEVICE && ioctx->cmd.data_length &&
	    !ioctx->queue_status_only) {
		ret = srpt_xfer_data(ch, ioctx);
		if (ret) {
			printk(KERN_ERR "xfer_data failed for tag %llu\n",
			       ioctx->tag);
			goto out;
		}
	}

	if (state != SRPT_STATE_MGMT)
		resp_len = srpt_build_cmd_rsp(ch, ioctx, ioctx->tag,
					      cmd->scsi_status);
	else {
		srp_tm_status
			= tcm_to_srp_tsk_mgmt_status(cmd->se_tmr_req->response);
		resp_len = srpt_build_tskmgmt_rsp(ch, ioctx, srp_tm_status,
						 ioctx->tag);
	}
	ret = srpt_post_send(ch, ioctx, resp_len);
	if (ret) {
		printk(KERN_ERR "sending cmd response failed for tag %llu\n",
		       ioctx->tag);
		srpt_unmap_sg_to_ib_sge(ch, ioctx);
		srpt_set_cmd_state(ioctx, SRPT_STATE_DONE);
		target_put_sess_cmd(ioctx->ch->sess, &ioctx->cmd);
	}

out:
	return ret;
}

static int srpt_queue_status(struct se_cmd *cmd)
{
	struct srpt_send_ioctx *ioctx;

	ioctx = container_of(cmd, struct srpt_send_ioctx, cmd);
	BUG_ON(ioctx->sense_data != cmd->sense_buffer);
	if (cmd->se_cmd_flags &
	    (SCF_TRANSPORT_TASK_SENSE | SCF_EMULATED_TASK_SENSE))
		WARN_ON(cmd->scsi_status != SAM_STAT_CHECK_CONDITION);
	ioctx->queue_status_only = true;
	return srpt_queue_response(cmd);
}

static void srpt_refresh_port_work(struct work_struct *work)
{
	struct srpt_port *sport = container_of(work, struct srpt_port, work);

	srpt_refresh_port(sport);
}

static int srpt_ch_list_empty(struct srpt_device *sdev)
{
	int res;

	spin_lock_irq(&sdev->spinlock);
	res = list_empty(&sdev->rch_list);
	spin_unlock_irq(&sdev->spinlock);

	return res;
}

/**
 * srpt_release_sdev() - Free the channel resources associated with a target.
 */
static int srpt_release_sdev(struct srpt_device *sdev)
{
	struct srpt_rdma_ch *ch, *tmp_ch;
	int res;

	WARN_ON_ONCE(irqs_disabled());

	BUG_ON(!sdev);

	spin_lock_irq(&sdev->spinlock);
	list_for_each_entry_safe(ch, tmp_ch, &sdev->rch_list, list)
		__srpt_close_ch(ch);
	spin_unlock_irq(&sdev->spinlock);

	res = wait_event_interruptible(sdev->ch_releaseQ,
				       srpt_ch_list_empty(sdev));
	if (res)
		printk(KERN_ERR "%s: interrupted.\n", __func__);

	return 0;
}

static struct srpt_port *__srpt_lookup_port(const char *name)
{
	struct ib_device *dev;
	struct srpt_device *sdev;
	struct srpt_port *sport;
	int i;

	list_for_each_entry(sdev, &srpt_dev_list, list) {
		dev = sdev->device;
		if (!dev)
			continue;

		for (i = 0; i < dev->phys_port_cnt; i++) {
			sport = &sdev->port[i];

			if (!strcmp(sport->port_guid, name))
				return sport;
		}
	}

	return NULL;
}

static struct srpt_port *srpt_lookup_port(const char *name)
{
	struct srpt_port *sport;

	spin_lock(&srpt_dev_lock);
	sport = __srpt_lookup_port(name);
	spin_unlock(&srpt_dev_lock);

	return sport;
}

/**
 * srpt_add_one() - Infiniband device addition callback function.
 */
static void srpt_add_one(struct ib_device *device)
{
	struct srpt_device *sdev;
	struct srpt_port *sport;
	struct ib_srq_init_attr srq_attr;
	int i;

	pr_debug("device = %p, device->dma_ops = %p\n", device,
		 device->dma_ops);

	sdev = kzalloc(sizeof *sdev, GFP_KERNEL);
	if (!sdev)
		goto err;

	sdev->device = device;
	INIT_LIST_HEAD(&sdev->rch_list);
	init_waitqueue_head(&sdev->ch_releaseQ);
	spin_lock_init(&sdev->spinlock);

	if (ib_query_device(device, &sdev->dev_attr))
		goto free_dev;

	sdev->pd = ib_alloc_pd(device);
	if (IS_ERR(sdev->pd))
		goto free_dev;

	sdev->mr = ib_get_dma_mr(sdev->pd, IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(sdev->mr))
		goto err_pd;

	sdev->srq_size = min(srpt_srq_size, sdev->dev_attr.max_srq_wr);

	srq_attr.event_handler = srpt_srq_event;
	srq_attr.srq_context = (void *)sdev;
	srq_attr.attr.max_wr = sdev->srq_size;
	srq_attr.attr.max_sge = 1;
	srq_attr.attr.srq_limit = 0;
	srq_attr.srq_type = IB_SRQT_BASIC;

	sdev->srq = ib_create_srq(sdev->pd, &srq_attr);
	if (IS_ERR(sdev->srq))
		goto err_mr;

	pr_debug("%s: create SRQ #wr= %d max_allow=%d dev= %s\n",
		 __func__, sdev->srq_size, sdev->dev_attr.max_srq_wr,
		 device->name);

	if (!srpt_service_guid)
		srpt_service_guid = be64_to_cpu(device->node_guid);

	sdev->cm_id = ib_create_cm_id(device, srpt_cm_handler, sdev);
	if (IS_ERR(sdev->cm_id))
		goto err_srq;

	/* print out target login information */
	pr_debug("Target login info: id_ext=%016llx,ioc_guid=%016llx,"
		 "pkey=ffff,service_id=%016llx\n", srpt_service_guid,
		 srpt_service_guid, srpt_service_guid);

	/*
	 * We do not have a consistent service_id (ie. also id_ext of target_id)
	 * to identify this target. We currently use the guid of the first HCA
	 * in the system as service_id; therefore, the target_id will change
	 * if this HCA is gone bad and replaced by different HCA
	 */
	if (ib_cm_listen(sdev->cm_id, cpu_to_be64(srpt_service_guid), 0, NULL))
		goto err_cm;

	INIT_IB_EVENT_HANDLER(&sdev->event_handler, sdev->device,
			      srpt_event_handler);
	if (ib_register_event_handler(&sdev->event_handler))
		goto err_cm;

	sdev->ioctx_ring = (struct srpt_recv_ioctx **)
		srpt_alloc_ioctx_ring(sdev, sdev->srq_size,
				      sizeof(*sdev->ioctx_ring[0]),
				      srp_max_req_size, DMA_FROM_DEVICE);
	if (!sdev->ioctx_ring)
		goto err_event;

	for (i = 0; i < sdev->srq_size; ++i)
		srpt_post_recv(sdev, sdev->ioctx_ring[i]);

	WARN_ON(sdev->device->phys_port_cnt > ARRAY_SIZE(sdev->port));

	for (i = 1; i <= sdev->device->phys_port_cnt; i++) {
		sport = &sdev->port[i - 1];
		sport->sdev = sdev;
		sport->port = i;
		sport->port_attrib.srp_max_rdma_size = DEFAULT_MAX_RDMA_SIZE;
		sport->port_attrib.srp_max_rsp_size = DEFAULT_MAX_RSP_SIZE;
		sport->port_attrib.srp_sq_size = DEF_SRPT_SQ_SIZE;
		INIT_WORK(&sport->work, srpt_refresh_port_work);
		INIT_LIST_HEAD(&sport->port_acl_list);
		spin_lock_init(&sport->port_acl_lock);

		if (srpt_refresh_port(sport)) {
			printk(KERN_ERR "MAD registration failed for %s-%d.\n",
			       srpt_sdev_name(sdev), i);
			goto err_ring;
		}
		snprintf(sport->port_guid, sizeof(sport->port_guid),
			"0x%016llx%016llx",
			be64_to_cpu(sport->gid.global.subnet_prefix),
			be64_to_cpu(sport->gid.global.interface_id));
	}

	spin_lock(&srpt_dev_lock);
	list_add_tail(&sdev->list, &srpt_dev_list);
	spin_unlock(&srpt_dev_lock);

out:
	ib_set_client_data(device, &srpt_client, sdev);
	pr_debug("added %s.\n", device->name);
	return;

err_ring:
	srpt_free_ioctx_ring((struct srpt_ioctx **)sdev->ioctx_ring, sdev,
			     sdev->srq_size, srp_max_req_size,
			     DMA_FROM_DEVICE);
err_event:
	ib_unregister_event_handler(&sdev->event_handler);
err_cm:
	ib_destroy_cm_id(sdev->cm_id);
err_srq:
	ib_destroy_srq(sdev->srq);
err_mr:
	ib_dereg_mr(sdev->mr);
err_pd:
	ib_dealloc_pd(sdev->pd);
free_dev:
	kfree(sdev);
err:
	sdev = NULL;
	printk(KERN_INFO "%s(%s) failed.\n", __func__, device->name);
	goto out;
}

/**
 * srpt_remove_one() - InfiniBand device removal callback function.
 */
static void srpt_remove_one(struct ib_device *device)
{
	struct srpt_device *sdev;
	int i;

	sdev = ib_get_client_data(device, &srpt_client);
	if (!sdev) {
		printk(KERN_INFO "%s(%s): nothing to do.\n", __func__,
		       device->name);
		return;
	}

	srpt_unregister_mad_agent(sdev);

	ib_unregister_event_handler(&sdev->event_handler);

	/* Cancel any work queued by the just unregistered IB event handler. */
	for (i = 0; i < sdev->device->phys_port_cnt; i++)
		cancel_work_sync(&sdev->port[i].work);

	ib_destroy_cm_id(sdev->cm_id);

	/*
	 * Unregistering a target must happen after destroying sdev->cm_id
	 * such that no new SRP_LOGIN_REQ information units can arrive while
	 * destroying the target.
	 */
	spin_lock(&srpt_dev_lock);
	list_del(&sdev->list);
	spin_unlock(&srpt_dev_lock);
	srpt_release_sdev(sdev);

	ib_destroy_srq(sdev->srq);
	ib_dereg_mr(sdev->mr);
	ib_dealloc_pd(sdev->pd);

	srpt_free_ioctx_ring((struct srpt_ioctx **)sdev->ioctx_ring, sdev,
			     sdev->srq_size, srp_max_req_size, DMA_FROM_DEVICE);
	sdev->ioctx_ring = NULL;
	kfree(sdev);
}

static struct ib_client srpt_client = {
	.name = DRV_NAME,
	.add = srpt_add_one,
	.remove = srpt_remove_one
};

static int srpt_check_true(struct se_portal_group *se_tpg)
{
	return 1;
}

static int srpt_check_false(struct se_portal_group *se_tpg)
{
	return 0;
}

static char *srpt_get_fabric_name(void)
{
	return "srpt";
}

static u8 srpt_get_fabric_proto_ident(struct se_portal_group *se_tpg)
{
	return SCSI_TRANSPORTID_PROTOCOLID_SRP;
}

static char *srpt_get_fabric_wwn(struct se_portal_group *tpg)
{
	struct srpt_port *sport = container_of(tpg, struct srpt_port, port_tpg_1);

	return sport->port_guid;
}

static u16 srpt_get_tag(struct se_portal_group *tpg)
{
	return 1;
}

static u32 srpt_get_default_depth(struct se_portal_group *se_tpg)
{
	return 1;
}

static u32 srpt_get_pr_transport_id(struct se_portal_group *se_tpg,
				    struct se_node_acl *se_nacl,
				    struct t10_pr_registration *pr_reg,
				    int *format_code, unsigned char *buf)
{
	struct srpt_node_acl *nacl;
	struct spc_rdma_transport_id *tr_id;

	nacl = container_of(se_nacl, struct srpt_node_acl, nacl);
	tr_id = (void *)buf;
	tr_id->protocol_identifier = SCSI_TRANSPORTID_PROTOCOLID_SRP;
	memcpy(tr_id->i_port_id, nacl->i_port_id, sizeof(tr_id->i_port_id));
	return sizeof(*tr_id);
}

static u32 srpt_get_pr_transport_id_len(struct se_portal_group *se_tpg,
					struct se_node_acl *se_nacl,
					struct t10_pr_registration *pr_reg,
					int *format_code)
{
	*format_code = 0;
	return sizeof(struct spc_rdma_transport_id);
}

static char *srpt_parse_pr_out_transport_id(struct se_portal_group *se_tpg,
					    const char *buf, u32 *out_tid_len,
					    char **port_nexus_ptr)
{
	struct spc_rdma_transport_id *tr_id;

	*port_nexus_ptr = NULL;
	*out_tid_len = sizeof(struct spc_rdma_transport_id);
	tr_id = (void *)buf;
	return (char *)tr_id->i_port_id;
}

static struct se_node_acl *srpt_alloc_fabric_acl(struct se_portal_group *se_tpg)
{
	struct srpt_node_acl *nacl;

	nacl = kzalloc(sizeof(struct srpt_node_acl), GFP_KERNEL);
	if (!nacl) {
		printk(KERN_ERR "Unable to allocate struct srpt_node_acl\n");
		return NULL;
	}

	return &nacl->nacl;
}

static void srpt_release_fabric_acl(struct se_portal_group *se_tpg,
				    struct se_node_acl *se_nacl)
{
	struct srpt_node_acl *nacl;

	nacl = container_of(se_nacl, struct srpt_node_acl, nacl);
	kfree(nacl);
}

static u32 srpt_tpg_get_inst_index(struct se_portal_group *se_tpg)
{
	return 1;
}

static void srpt_release_cmd(struct se_cmd *se_cmd)
{
	struct srpt_send_ioctx *ioctx = container_of(se_cmd,
				struct srpt_send_ioctx, cmd);
	struct srpt_rdma_ch *ch = ioctx->ch;
	unsigned long flags;

	WARN_ON(ioctx->state != SRPT_STATE_DONE);
	WARN_ON(ioctx->mapped_sg_count != 0);

	if (ioctx->n_rbuf > 1) {
		kfree(ioctx->rbufs);
		ioctx->rbufs = NULL;
		ioctx->n_rbuf = 0;
	}

	spin_lock_irqsave(&ch->spinlock, flags);
	list_add(&ioctx->free_list, &ch->free_list);
	spin_unlock_irqrestore(&ch->spinlock, flags);
}

/**
 * srpt_shutdown_session() - Whether or not a session may be shut down.
 */
static int srpt_shutdown_session(struct se_session *se_sess)
{
	return true;
}

/**
 * srpt_close_session() - Forcibly close a session.
 *
 * Callback function invoked by the TCM core to clean up sessions associated
 * with a node ACL when the user invokes
 * rmdir /sys/kernel/config/target/$driver/$port/$tpg/acls/$i_port_id
 */
static void srpt_close_session(struct se_session *se_sess)
{
	DECLARE_COMPLETION_ONSTACK(release_done);
	struct srpt_rdma_ch *ch;
	struct srpt_device *sdev;
	int res;

	ch = se_sess->fabric_sess_ptr;
	WARN_ON(ch->sess != se_sess);

	pr_debug("ch %p state %d\n", ch, srpt_get_ch_state(ch));

	sdev = ch->sport->sdev;
	spin_lock_irq(&sdev->spinlock);
	BUG_ON(ch->release_done);
	ch->release_done = &release_done;
	__srpt_close_ch(ch);
	spin_unlock_irq(&sdev->spinlock);

	res = wait_for_completion_timeout(&release_done, 60 * HZ);
	WARN_ON(res <= 0);
}

/**
 * srpt_sess_get_index() - Return the value of scsiAttIntrPortIndex (SCSI-MIB).
 *
 * A quote from RFC 4455 (SCSI-MIB) about this MIB object:
 * This object represents an arbitrary integer used to uniquely identify a
 * particular attached remote initiator port to a particular SCSI target port
 * within a particular SCSI target device within a particular SCSI instance.
 */
static u32 srpt_sess_get_index(struct se_session *se_sess)
{
	return 0;
}

static void srpt_set_default_node_attrs(struct se_node_acl *nacl)
{
}

static u32 srpt_get_task_tag(struct se_cmd *se_cmd)
{
	struct srpt_send_ioctx *ioctx;

	ioctx = container_of(se_cmd, struct srpt_send_ioctx, cmd);
	return ioctx->tag;
}

/* Note: only used from inside debug printk's by the TCM core. */
static int srpt_get_tcm_cmd_state(struct se_cmd *se_cmd)
{
	struct srpt_send_ioctx *ioctx;

	ioctx = container_of(se_cmd, struct srpt_send_ioctx, cmd);
	return srpt_get_cmd_state(ioctx);
}

/**
 * srpt_parse_i_port_id() - Parse an initiator port ID.
 * @name: ASCII representation of a 128-bit initiator port ID.
 * @i_port_id: Binary 128-bit port ID.
 */
static int srpt_parse_i_port_id(u8 i_port_id[16], const char *name)
{
	const char *p;
	unsigned len, count, leading_zero_bytes;
	int ret, rc;

	p = name;
	if (strnicmp(p, "0x", 2) == 0)
		p += 2;
	ret = -EINVAL;
	len = strlen(p);
	if (len % 2)
		goto out;
	count = min(len / 2, 16U);
	leading_zero_bytes = 16 - count;
	memset(i_port_id, 0, leading_zero_bytes);
	rc = hex2bin(i_port_id + leading_zero_bytes, p, count);
	if (rc < 0)
		pr_debug("hex2bin failed for srpt_parse_i_port_id: %d\n", rc);
	ret = 0;
out:
	return ret;
}

/*
 * configfs callback function invoked for
 * mkdir /sys/kernel/config/target/$driver/$port/$tpg/acls/$i_port_id
 */
static struct se_node_acl *srpt_make_nodeacl(struct se_portal_group *tpg,
					     struct config_group *group,
					     const char *name)
{
	struct srpt_port *sport = container_of(tpg, struct srpt_port, port_tpg_1);
	struct se_node_acl *se_nacl, *se_nacl_new;
	struct srpt_node_acl *nacl;
	int ret = 0;
	u32 nexus_depth = 1;
	u8 i_port_id[16];

	if (srpt_parse_i_port_id(i_port_id, name) < 0) {
		printk(KERN_ERR "invalid initiator port ID %s\n", name);
		ret = -EINVAL;
		goto err;
	}

	se_nacl_new = srpt_alloc_fabric_acl(tpg);
	if (!se_nacl_new) {
		ret = -ENOMEM;
		goto err;
	}
	/*
	 * nacl_new may be released by core_tpg_add_initiator_node_acl()
	 * when converting a node ACL from demo mode to explict
	 */
	se_nacl = core_tpg_add_initiator_node_acl(tpg, se_nacl_new, name,
						  nexus_depth);
	if (IS_ERR(se_nacl)) {
		ret = PTR_ERR(se_nacl);
		goto err;
	}
	/* Locate our struct srpt_node_acl and set sdev and i_port_id. */
	nacl = container_of(se_nacl, struct srpt_node_acl, nacl);
	memcpy(&nacl->i_port_id[0], &i_port_id[0], 16);
	nacl->sport = sport;

	spin_lock_irq(&sport->port_acl_lock);
	list_add_tail(&nacl->list, &sport->port_acl_list);
	spin_unlock_irq(&sport->port_acl_lock);

	return se_nacl;
err:
	return ERR_PTR(ret);
}

/*
 * configfs callback function invoked for
 * rmdir /sys/kernel/config/target/$driver/$port/$tpg/acls/$i_port_id
 */
static void srpt_drop_nodeacl(struct se_node_acl *se_nacl)
{
	struct srpt_node_acl *nacl;
	struct srpt_device *sdev;
	struct srpt_port *sport;

	nacl = container_of(se_nacl, struct srpt_node_acl, nacl);
	sport = nacl->sport;
	sdev = sport->sdev;
	spin_lock_irq(&sport->port_acl_lock);
	list_del(&nacl->list);
	spin_unlock_irq(&sport->port_acl_lock);
	core_tpg_del_initiator_node_acl(&sport->port_tpg_1, se_nacl, 1);
	srpt_release_fabric_acl(NULL, se_nacl);
}

static ssize_t srpt_tpg_attrib_show_srp_max_rdma_size(
	struct se_portal_group *se_tpg,
	char *page)
{
	struct srpt_port *sport = container_of(se_tpg, struct srpt_port, port_tpg_1);

	return sprintf(page, "%u\n", sport->port_attrib.srp_max_rdma_size);
}

static ssize_t srpt_tpg_attrib_store_srp_max_rdma_size(
	struct se_portal_group *se_tpg,
	const char *page,
	size_t count)
{
	struct srpt_port *sport = container_of(se_tpg, struct srpt_port, port_tpg_1);
	unsigned long val;
	int ret;

	ret = strict_strtoul(page, 0, &val);
	if (ret < 0) {
		pr_err("strict_strtoul() failed with ret: %d\n", ret);
		return -EINVAL;
	}
	if (val > MAX_SRPT_RDMA_SIZE) {
		pr_err("val: %lu exceeds MAX_SRPT_RDMA_SIZE: %d\n", val,
			MAX_SRPT_RDMA_SIZE);
		return -EINVAL;
	}
	if (val < DEFAULT_MAX_RDMA_SIZE) {
		pr_err("val: %lu smaller than DEFAULT_MAX_RDMA_SIZE: %d\n",
			val, DEFAULT_MAX_RDMA_SIZE);
		return -EINVAL;
	}
	sport->port_attrib.srp_max_rdma_size = val;

	return count;
}

TF_TPG_ATTRIB_ATTR(srpt, srp_max_rdma_size, S_IRUGO | S_IWUSR);

static ssize_t srpt_tpg_attrib_show_srp_max_rsp_size(
	struct se_portal_group *se_tpg,
	char *page)
{
	struct srpt_port *sport = container_of(se_tpg, struct srpt_port, port_tpg_1);

	return sprintf(page, "%u\n", sport->port_attrib.srp_max_rsp_size);
}

static ssize_t srpt_tpg_attrib_store_srp_max_rsp_size(
	struct se_portal_group *se_tpg,
	const char *page,
	size_t count)
{
	struct srpt_port *sport = container_of(se_tpg, struct srpt_port, port_tpg_1);
	unsigned long val;
	int ret;

	ret = strict_strtoul(page, 0, &val);
	if (ret < 0) {
		pr_err("strict_strtoul() failed with ret: %d\n", ret);
		return -EINVAL;
	}
	if (val > MAX_SRPT_RSP_SIZE) {
		pr_err("val: %lu exceeds MAX_SRPT_RSP_SIZE: %d\n", val,
			MAX_SRPT_RSP_SIZE);
		return -EINVAL;
	}
	if (val < MIN_MAX_RSP_SIZE) {
		pr_err("val: %lu smaller than MIN_MAX_RSP_SIZE: %d\n", val,
			MIN_MAX_RSP_SIZE);
		return -EINVAL;
	}
	sport->port_attrib.srp_max_rsp_size = val;

	return count;
}

TF_TPG_ATTRIB_ATTR(srpt, srp_max_rsp_size, S_IRUGO | S_IWUSR);

static ssize_t srpt_tpg_attrib_show_srp_sq_size(
	struct se_portal_group *se_tpg,
	char *page)
{
	struct srpt_port *sport = container_of(se_tpg, struct srpt_port, port_tpg_1);

	return sprintf(page, "%u\n", sport->port_attrib.srp_sq_size);
}

static ssize_t srpt_tpg_attrib_store_srp_sq_size(
	struct se_portal_group *se_tpg,
	const char *page,
	size_t count)
{
	struct srpt_port *sport = container_of(se_tpg, struct srpt_port, port_tpg_1);
	unsigned long val;
	int ret;

	ret = strict_strtoul(page, 0, &val);
	if (ret < 0) {
		pr_err("strict_strtoul() failed with ret: %d\n", ret);
		return -EINVAL;
	}
	if (val > MAX_SRPT_SRQ_SIZE) {
		pr_err("val: %lu exceeds MAX_SRPT_SRQ_SIZE: %d\n", val,
			MAX_SRPT_SRQ_SIZE);
		return -EINVAL;
	}
	if (val < MIN_SRPT_SRQ_SIZE) {
		pr_err("val: %lu smaller than MIN_SRPT_SRQ_SIZE: %d\n", val,
			MIN_SRPT_SRQ_SIZE);
		return -EINVAL;
	}
	sport->port_attrib.srp_sq_size = val;

	return count;
}

TF_TPG_ATTRIB_ATTR(srpt, srp_sq_size, S_IRUGO | S_IWUSR);

static struct configfs_attribute *srpt_tpg_attrib_attrs[] = {
	&srpt_tpg_attrib_srp_max_rdma_size.attr,
	&srpt_tpg_attrib_srp_max_rsp_size.attr,
	&srpt_tpg_attrib_srp_sq_size.attr,
	NULL,
};

static ssize_t srpt_tpg_show_enable(
	struct se_portal_group *se_tpg,
	char *page)
{
	struct srpt_port *sport = container_of(se_tpg, struct srpt_port, port_tpg_1);

	return snprintf(page, PAGE_SIZE, "%d\n", (sport->enabled) ? 1: 0);
}

static ssize_t srpt_tpg_store_enable(
	struct se_portal_group *se_tpg,
	const char *page,
	size_t count)
{
	struct srpt_port *sport = container_of(se_tpg, struct srpt_port, port_tpg_1);
	unsigned long tmp;
        int ret;

	ret = strict_strtoul(page, 0, &tmp);
	if (ret < 0) {
		printk(KERN_ERR "Unable to extract srpt_tpg_store_enable\n");
		return -EINVAL;
	}

	if ((tmp != 0) && (tmp != 1)) {
		printk(KERN_ERR "Illegal value for srpt_tpg_store_enable: %lu\n", tmp);
		return -EINVAL;
	}
	if (tmp == 1)
		sport->enabled = true;
	else
		sport->enabled = false;

	return count;
}

TF_TPG_BASE_ATTR(srpt, enable, S_IRUGO | S_IWUSR);

static struct configfs_attribute *srpt_tpg_attrs[] = {
	&srpt_tpg_enable.attr,
	NULL,
};

/**
 * configfs callback invoked for
 * mkdir /sys/kernel/config/target/$driver/$port/$tpg
 */
static struct se_portal_group *srpt_make_tpg(struct se_wwn *wwn,
					     struct config_group *group,
					     const char *name)
{
	struct srpt_port *sport = container_of(wwn, struct srpt_port, port_wwn);
	int res;

	/* Initialize sport->port_wwn and sport->port_tpg_1 */
	res = core_tpg_register(&srpt_target->tf_ops, &sport->port_wwn,
			&sport->port_tpg_1, sport, TRANSPORT_TPG_TYPE_NORMAL);
	if (res)
		return ERR_PTR(res);

	return &sport->port_tpg_1;
}

/**
 * configfs callback invoked for
 * rmdir /sys/kernel/config/target/$driver/$port/$tpg
 */
static void srpt_drop_tpg(struct se_portal_group *tpg)
{
	struct srpt_port *sport = container_of(tpg,
				struct srpt_port, port_tpg_1);

	sport->enabled = false;
	core_tpg_deregister(&sport->port_tpg_1);
}

/**
 * configfs callback invoked for
 * mkdir /sys/kernel/config/target/$driver/$port
 */
static struct se_wwn *srpt_make_tport(struct target_fabric_configfs *tf,
				      struct config_group *group,
				      const char *name)
{
	struct srpt_port *sport;
	int ret;

	sport = srpt_lookup_port(name);
	pr_debug("make_tport(%s)\n", name);
	ret = -EINVAL;
	if (!sport)
		goto err;

	return &sport->port_wwn;

err:
	return ERR_PTR(ret);
}

/**
 * configfs callback invoked for
 * rmdir /sys/kernel/config/target/$driver/$port
 */
static void srpt_drop_tport(struct se_wwn *wwn)
{
	struct srpt_port *sport = container_of(wwn, struct srpt_port, port_wwn);

	pr_debug("drop_tport(%s\n", config_item_name(&sport->port_wwn.wwn_group.cg_item));
}

static ssize_t srpt_wwn_show_attr_version(struct target_fabric_configfs *tf,
					      char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", DRV_VERSION);
}

TF_WWN_ATTR_RO(srpt, version);

static struct configfs_attribute *srpt_wwn_attrs[] = {
	&srpt_wwn_version.attr,
	NULL,
};

static struct target_core_fabric_ops srpt_template = {
	.get_fabric_name		= srpt_get_fabric_name,
	.get_fabric_proto_ident		= srpt_get_fabric_proto_ident,
	.tpg_get_wwn			= srpt_get_fabric_wwn,
	.tpg_get_tag			= srpt_get_tag,
	.tpg_get_default_depth		= srpt_get_default_depth,
	.tpg_get_pr_transport_id	= srpt_get_pr_transport_id,
	.tpg_get_pr_transport_id_len	= srpt_get_pr_transport_id_len,
	.tpg_parse_pr_out_transport_id	= srpt_parse_pr_out_transport_id,
	.tpg_check_demo_mode		= srpt_check_false,
	.tpg_check_demo_mode_cache	= srpt_check_true,
	.tpg_check_demo_mode_write_protect = srpt_check_true,
	.tpg_check_prod_mode_write_protect = srpt_check_false,
	.tpg_alloc_fabric_acl		= srpt_alloc_fabric_acl,
	.tpg_release_fabric_acl		= srpt_release_fabric_acl,
	.tpg_get_inst_index		= srpt_tpg_get_inst_index,
	.release_cmd			= srpt_release_cmd,
	.check_stop_free		= srpt_check_stop_free,
	.shutdown_session		= srpt_shutdown_session,
	.close_session			= srpt_close_session,
	.sess_get_index			= srpt_sess_get_index,
	.sess_get_initiator_sid		= NULL,
	.write_pending			= srpt_write_pending,
	.write_pending_status		= srpt_write_pending_status,
	.set_default_node_attributes	= srpt_set_default_node_attrs,
	.get_task_tag			= srpt_get_task_tag,
	.get_cmd_state			= srpt_get_tcm_cmd_state,
	.queue_data_in			= srpt_queue_response,
	.queue_status			= srpt_queue_status,
	.queue_tm_rsp			= srpt_queue_response,
	/*
	 * Setup function pointers for generic logic in
	 * target_core_fabric_configfs.c
	 */
	.fabric_make_wwn		= srpt_make_tport,
	.fabric_drop_wwn		= srpt_drop_tport,
	.fabric_make_tpg		= srpt_make_tpg,
	.fabric_drop_tpg		= srpt_drop_tpg,
	.fabric_post_link		= NULL,
	.fabric_pre_unlink		= NULL,
	.fabric_make_np			= NULL,
	.fabric_drop_np			= NULL,
	.fabric_make_nodeacl		= srpt_make_nodeacl,
	.fabric_drop_nodeacl		= srpt_drop_nodeacl,
};

/**
 * srpt_init_module() - Kernel module initialization.
 *
 * Note: Since ib_register_client() registers callback functions, and since at
 * least one of these callback functions (srpt_add_one()) calls target core
 * functions, this driver must be registered with the target core before
 * ib_register_client() is called.
 */
static int __init srpt_init_module(void)
{
	int ret;

	ret = -EINVAL;
	if (srp_max_req_size < MIN_MAX_REQ_SIZE) {
		printk(KERN_ERR "invalid value %d for kernel module parameter"
		       " srp_max_req_size -- must be at least %d.\n",
		       srp_max_req_size, MIN_MAX_REQ_SIZE);
		goto out;
	}

	if (srpt_srq_size < MIN_SRPT_SRQ_SIZE
	    || srpt_srq_size > MAX_SRPT_SRQ_SIZE) {
		printk(KERN_ERR "invalid value %d for kernel module parameter"
		       " srpt_srq_size -- must be in the range [%d..%d].\n",
		       srpt_srq_size, MIN_SRPT_SRQ_SIZE, MAX_SRPT_SRQ_SIZE);
		goto out;
	}

	srpt_target = target_fabric_configfs_init(THIS_MODULE, "srpt");
	if (IS_ERR(srpt_target)) {
		printk(KERN_ERR "couldn't register\n");
		ret = PTR_ERR(srpt_target);
		goto out;
	}

	srpt_target->tf_ops = srpt_template;

	/*
	 * Set up default attribute lists.
	 */
	srpt_target->tf_cit_tmpl.tfc_wwn_cit.ct_attrs = srpt_wwn_attrs;
	srpt_target->tf_cit_tmpl.tfc_tpg_base_cit.ct_attrs = srpt_tpg_attrs;
	srpt_target->tf_cit_tmpl.tfc_tpg_attrib_cit.ct_attrs = srpt_tpg_attrib_attrs;
	srpt_target->tf_cit_tmpl.tfc_tpg_param_cit.ct_attrs = NULL;
	srpt_target->tf_cit_tmpl.tfc_tpg_np_base_cit.ct_attrs = NULL;
	srpt_target->tf_cit_tmpl.tfc_tpg_nacl_base_cit.ct_attrs = NULL;
	srpt_target->tf_cit_tmpl.tfc_tpg_nacl_attrib_cit.ct_attrs = NULL;
	srpt_target->tf_cit_tmpl.tfc_tpg_nacl_auth_cit.ct_attrs = NULL;
	srpt_target->tf_cit_tmpl.tfc_tpg_nacl_param_cit.ct_attrs = NULL;

	ret = target_fabric_configfs_register(srpt_target);
	if (ret < 0) {
		printk(KERN_ERR "couldn't register\n");
		goto out_free_target;
	}

	ret = ib_register_client(&srpt_client);
	if (ret) {
		printk(KERN_ERR "couldn't register IB client\n");
		goto out_unregister_target;
	}

	return 0;

out_unregister_target:
	target_fabric_configfs_deregister(srpt_target);
	srpt_target = NULL;
out_free_target:
	if (srpt_target)
		target_fabric_configfs_free(srpt_target);
out:
	return ret;
}

static void __exit srpt_cleanup_module(void)
{
	ib_unregister_client(&srpt_client);
	target_fabric_configfs_deregister(srpt_target);
	srpt_target = NULL;
}

module_init(srpt_init_module);
module_exit(srpt_cleanup_module);
