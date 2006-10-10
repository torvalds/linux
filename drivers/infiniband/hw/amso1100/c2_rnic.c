/*
 * Copyright (c) 2005 Ammasso, Inc. All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
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
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/inet.h>
#include <linux/vmalloc.h>

#include <linux/route.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>
#include <rdma/ib_smi.h>
#include "c2.h"
#include "c2_vq.h"

/* Device capabilities */
#define C2_MIN_PAGESIZE  1024

#define C2_MAX_MRS       32768
#define C2_MAX_QPS       16000
#define C2_MAX_WQE_SZ    256
#define C2_MAX_QP_WR     ((128*1024)/C2_MAX_WQE_SZ)
#define C2_MAX_SGES      4
#define C2_MAX_SGE_RD    1
#define C2_MAX_CQS       32768
#define C2_MAX_CQES      4096
#define C2_MAX_PDS       16384

/*
 * Send the adapter INIT message to the amso1100
 */
static int c2_adapter_init(struct c2_dev *c2dev)
{
	struct c2wr_init_req wr;
	int err;

	memset(&wr, 0, sizeof(wr));
	c2_wr_set_id(&wr, CCWR_INIT);
	wr.hdr.context = 0;
	wr.hint_count = cpu_to_be64(c2dev->hint_count_dma);
	wr.q0_host_shared = cpu_to_be64(c2dev->req_vq.shared_dma);
	wr.q1_host_shared = cpu_to_be64(c2dev->rep_vq.shared_dma);
	wr.q1_host_msg_pool = cpu_to_be64(c2dev->rep_vq.host_dma);
	wr.q2_host_shared = cpu_to_be64(c2dev->aeq.shared_dma);
	wr.q2_host_msg_pool = cpu_to_be64(c2dev->aeq.host_dma);

	/* Post the init message */
	err = vq_send_wr(c2dev, (union c2wr *) & wr);

	return err;
}

/*
 * Send the adapter TERM message to the amso1100
 */
static void c2_adapter_term(struct c2_dev *c2dev)
{
	struct c2wr_init_req wr;

	memset(&wr, 0, sizeof(wr));
	c2_wr_set_id(&wr, CCWR_TERM);
	wr.hdr.context = 0;

	/* Post the init message */
	vq_send_wr(c2dev, (union c2wr *) & wr);
	c2dev->init = 0;

	return;
}

/*
 * Query the adapter
 */
static int c2_rnic_query(struct c2_dev *c2dev, struct ib_device_attr *props)
{
	struct c2_vq_req *vq_req;
	struct c2wr_rnic_query_req wr;
	struct c2wr_rnic_query_rep *reply;
	int err;

	vq_req = vq_req_alloc(c2dev);
	if (!vq_req)
		return -ENOMEM;

	c2_wr_set_id(&wr, CCWR_RNIC_QUERY);
	wr.hdr.context = (unsigned long) vq_req;
	wr.rnic_handle = c2dev->adapter_handle;

	vq_req_get(c2dev, vq_req);

	err = vq_send_wr(c2dev, (union c2wr *) &wr);
	if (err) {
		vq_req_put(c2dev, vq_req);
		goto bail1;
	}

	err = vq_wait_for_reply(c2dev, vq_req);
	if (err)
		goto bail1;

	reply =
	    (struct c2wr_rnic_query_rep *) (unsigned long) (vq_req->reply_msg);
	if (!reply)
		err = -ENOMEM;
	else
		err = c2_errno(reply);
	if (err)
		goto bail2;

	props->fw_ver =
		((u64)be32_to_cpu(reply->fw_ver_major) << 32) |
		((be32_to_cpu(reply->fw_ver_minor) && 0xFFFF) << 16) |
		(be32_to_cpu(reply->fw_ver_patch) && 0xFFFF);
	memcpy(&props->sys_image_guid, c2dev->netdev->dev_addr, 6);
	props->max_mr_size         = 0xFFFFFFFF;
	props->page_size_cap       = ~(C2_MIN_PAGESIZE-1);
	props->vendor_id           = be32_to_cpu(reply->vendor_id);
	props->vendor_part_id      = be32_to_cpu(reply->part_number);
	props->hw_ver              = be32_to_cpu(reply->hw_version);
	props->max_qp              = be32_to_cpu(reply->max_qps);
	props->max_qp_wr           = be32_to_cpu(reply->max_qp_depth);
	props->device_cap_flags    = c2dev->device_cap_flags;
	props->max_sge             = C2_MAX_SGES;
	props->max_sge_rd          = C2_MAX_SGE_RD;
	props->max_cq              = be32_to_cpu(reply->max_cqs);
	props->max_cqe             = be32_to_cpu(reply->max_cq_depth);
	props->max_mr              = be32_to_cpu(reply->max_mrs);
	props->max_pd              = be32_to_cpu(reply->max_pds);
	props->max_qp_rd_atom      = be32_to_cpu(reply->max_qp_ird);
	props->max_ee_rd_atom      = 0;
	props->max_res_rd_atom     = be32_to_cpu(reply->max_global_ird);
	props->max_qp_init_rd_atom = be32_to_cpu(reply->max_qp_ord);
	props->max_ee_init_rd_atom = 0;
	props->atomic_cap          = IB_ATOMIC_NONE;
	props->max_ee              = 0;
	props->max_rdd             = 0;
	props->max_mw              = be32_to_cpu(reply->max_mws);
	props->max_raw_ipv6_qp     = 0;
	props->max_raw_ethy_qp     = 0;
	props->max_mcast_grp       = 0;
	props->max_mcast_qp_attach = 0;
	props->max_total_mcast_qp_attach = 0;
	props->max_ah              = 0;
	props->max_fmr             = 0;
	props->max_map_per_fmr     = 0;
	props->max_srq             = 0;
	props->max_srq_wr          = 0;
	props->max_srq_sge         = 0;
	props->max_pkeys           = 0;
	props->local_ca_ack_delay  = 0;

 bail2:
	vq_repbuf_free(c2dev, reply);

 bail1:
	vq_req_free(c2dev, vq_req);
	return err;
}

/*
 * Add an IP address to the RNIC interface
 */
int c2_add_addr(struct c2_dev *c2dev, u32 inaddr, u32 inmask)
{
	struct c2_vq_req *vq_req;
	struct c2wr_rnic_setconfig_req *wr;
	struct c2wr_rnic_setconfig_rep *reply;
	struct c2_netaddr netaddr;
	int err, len;

	vq_req = vq_req_alloc(c2dev);
	if (!vq_req)
		return -ENOMEM;

	len = sizeof(struct c2_netaddr);
	wr = kmalloc(c2dev->req_vq.msg_size, GFP_KERNEL);
	if (!wr) {
		err = -ENOMEM;
		goto bail0;
	}

	c2_wr_set_id(wr, CCWR_RNIC_SETCONFIG);
	wr->hdr.context = (unsigned long) vq_req;
	wr->rnic_handle = c2dev->adapter_handle;
	wr->option = cpu_to_be32(C2_CFG_ADD_ADDR);

	netaddr.ip_addr = inaddr;
	netaddr.netmask = inmask;
	netaddr.mtu = 0;

	memcpy(wr->data, &netaddr, len);

	vq_req_get(c2dev, vq_req);

	err = vq_send_wr(c2dev, (union c2wr *) wr);
	if (err) {
		vq_req_put(c2dev, vq_req);
		goto bail1;
	}

	err = vq_wait_for_reply(c2dev, vq_req);
	if (err)
		goto bail1;

	reply =
	    (struct c2wr_rnic_setconfig_rep *) (unsigned long) (vq_req->reply_msg);
	if (!reply) {
		err = -ENOMEM;
		goto bail1;
	}

	err = c2_errno(reply);
	vq_repbuf_free(c2dev, reply);

      bail1:
	kfree(wr);
      bail0:
	vq_req_free(c2dev, vq_req);
	return err;
}

/*
 * Delete an IP address from the RNIC interface
 */
int c2_del_addr(struct c2_dev *c2dev, u32 inaddr, u32 inmask)
{
	struct c2_vq_req *vq_req;
	struct c2wr_rnic_setconfig_req *wr;
	struct c2wr_rnic_setconfig_rep *reply;
	struct c2_netaddr netaddr;
	int err, len;

	vq_req = vq_req_alloc(c2dev);
	if (!vq_req)
		return -ENOMEM;

	len = sizeof(struct c2_netaddr);
	wr = kmalloc(c2dev->req_vq.msg_size, GFP_KERNEL);
	if (!wr) {
		err = -ENOMEM;
		goto bail0;
	}

	c2_wr_set_id(wr, CCWR_RNIC_SETCONFIG);
	wr->hdr.context = (unsigned long) vq_req;
	wr->rnic_handle = c2dev->adapter_handle;
	wr->option = cpu_to_be32(C2_CFG_DEL_ADDR);

	netaddr.ip_addr = inaddr;
	netaddr.netmask = inmask;
	netaddr.mtu = 0;

	memcpy(wr->data, &netaddr, len);

	vq_req_get(c2dev, vq_req);

	err = vq_send_wr(c2dev, (union c2wr *) wr);
	if (err) {
		vq_req_put(c2dev, vq_req);
		goto bail1;
	}

	err = vq_wait_for_reply(c2dev, vq_req);
	if (err)
		goto bail1;

	reply =
	    (struct c2wr_rnic_setconfig_rep *) (unsigned long) (vq_req->reply_msg);
	if (!reply) {
		err = -ENOMEM;
		goto bail1;
	}

	err = c2_errno(reply);
	vq_repbuf_free(c2dev, reply);

      bail1:
	kfree(wr);
      bail0:
	vq_req_free(c2dev, vq_req);
	return err;
}

/*
 * Open a single RNIC instance to use with all
 * low level openib calls
 */
static int c2_rnic_open(struct c2_dev *c2dev)
{
	struct c2_vq_req *vq_req;
	union c2wr wr;
	struct c2wr_rnic_open_rep *reply;
	int err;

	vq_req = vq_req_alloc(c2dev);
	if (vq_req == NULL) {
		return -ENOMEM;
	}

	memset(&wr, 0, sizeof(wr));
	c2_wr_set_id(&wr, CCWR_RNIC_OPEN);
	wr.rnic_open.req.hdr.context = (unsigned long) (vq_req);
	wr.rnic_open.req.flags = cpu_to_be16(RNIC_PRIV_MODE);
	wr.rnic_open.req.port_num = cpu_to_be16(0);
	wr.rnic_open.req.user_context = (unsigned long) c2dev;

	vq_req_get(c2dev, vq_req);

	err = vq_send_wr(c2dev, &wr);
	if (err) {
		vq_req_put(c2dev, vq_req);
		goto bail0;
	}

	err = vq_wait_for_reply(c2dev, vq_req);
	if (err) {
		goto bail0;
	}

	reply = (struct c2wr_rnic_open_rep *) (unsigned long) (vq_req->reply_msg);
	if (!reply) {
		err = -ENOMEM;
		goto bail0;
	}

	if ((err = c2_errno(reply)) != 0) {
		goto bail1;
	}

	c2dev->adapter_handle = reply->rnic_handle;

      bail1:
	vq_repbuf_free(c2dev, reply);
      bail0:
	vq_req_free(c2dev, vq_req);
	return err;
}

/*
 * Close the RNIC instance
 */
static int c2_rnic_close(struct c2_dev *c2dev)
{
	struct c2_vq_req *vq_req;
	union c2wr wr;
	struct c2wr_rnic_close_rep *reply;
	int err;

	vq_req = vq_req_alloc(c2dev);
	if (vq_req == NULL) {
		return -ENOMEM;
	}

	memset(&wr, 0, sizeof(wr));
	c2_wr_set_id(&wr, CCWR_RNIC_CLOSE);
	wr.rnic_close.req.hdr.context = (unsigned long) vq_req;
	wr.rnic_close.req.rnic_handle = c2dev->adapter_handle;

	vq_req_get(c2dev, vq_req);

	err = vq_send_wr(c2dev, &wr);
	if (err) {
		vq_req_put(c2dev, vq_req);
		goto bail0;
	}

	err = vq_wait_for_reply(c2dev, vq_req);
	if (err) {
		goto bail0;
	}

	reply = (struct c2wr_rnic_close_rep *) (unsigned long) (vq_req->reply_msg);
	if (!reply) {
		err = -ENOMEM;
		goto bail0;
	}

	if ((err = c2_errno(reply)) != 0) {
		goto bail1;
	}

	c2dev->adapter_handle = 0;

      bail1:
	vq_repbuf_free(c2dev, reply);
      bail0:
	vq_req_free(c2dev, vq_req);
	return err;
}

/*
 * Called by c2_probe to initialize the RNIC. This principally
 * involves initalizing the various limits and resouce pools that
 * comprise the RNIC instance.
 */
int c2_rnic_init(struct c2_dev *c2dev)
{
	int err;
	u32 qsize, msgsize;
	void *q1_pages;
	void *q2_pages;
	void __iomem *mmio_regs;

	/* Device capabilities */
	c2dev->device_cap_flags =
	    (IB_DEVICE_RESIZE_MAX_WR |
	     IB_DEVICE_CURR_QP_STATE_MOD |
	     IB_DEVICE_SYS_IMAGE_GUID |
	     IB_DEVICE_ZERO_STAG |
	     IB_DEVICE_SEND_W_INV | IB_DEVICE_MEM_WINDOW);

	/* Allocate the qptr_array */
	c2dev->qptr_array = vmalloc(C2_MAX_CQS * sizeof(void *));
	if (!c2dev->qptr_array) {
		return -ENOMEM;
	}

	/* Inialize the qptr_array */
	memset(c2dev->qptr_array, 0, C2_MAX_CQS * sizeof(void *));
	c2dev->qptr_array[0] = (void *) &c2dev->req_vq;
	c2dev->qptr_array[1] = (void *) &c2dev->rep_vq;
	c2dev->qptr_array[2] = (void *) &c2dev->aeq;

	/* Initialize data structures */
	init_waitqueue_head(&c2dev->req_vq_wo);
	spin_lock_init(&c2dev->vqlock);
	spin_lock_init(&c2dev->lock);

	/* Allocate MQ shared pointer pool for kernel clients. User
	 * mode client pools are hung off the user context
	 */
	err = c2_init_mqsp_pool(c2dev, GFP_KERNEL, &c2dev->kern_mqsp_pool);
	if (err) {
		goto bail0;
	}

	/* Allocate shared pointers for Q0, Q1, and Q2 from
	 * the shared pointer pool.
	 */

	c2dev->hint_count = c2_alloc_mqsp(c2dev, c2dev->kern_mqsp_pool,
					     &c2dev->hint_count_dma,
					     GFP_KERNEL);
	c2dev->req_vq.shared = c2_alloc_mqsp(c2dev, c2dev->kern_mqsp_pool,
					     &c2dev->req_vq.shared_dma,
					     GFP_KERNEL);
	c2dev->rep_vq.shared = c2_alloc_mqsp(c2dev, c2dev->kern_mqsp_pool,
					     &c2dev->rep_vq.shared_dma,
					     GFP_KERNEL);
	c2dev->aeq.shared = c2_alloc_mqsp(c2dev, c2dev->kern_mqsp_pool,
					  &c2dev->aeq.shared_dma, GFP_KERNEL);
	if (!c2dev->hint_count || !c2dev->req_vq.shared ||
	    !c2dev->rep_vq.shared || !c2dev->aeq.shared) {
		err = -ENOMEM;
		goto bail1;
	}

	mmio_regs = c2dev->kva;
	/* Initialize the Verbs Request Queue */
	c2_mq_req_init(&c2dev->req_vq, 0,
		       be32_to_cpu(readl(mmio_regs + C2_REGS_Q0_QSIZE)),
		       be32_to_cpu(readl(mmio_regs + C2_REGS_Q0_MSGSIZE)),
		       mmio_regs +
		       be32_to_cpu(readl(mmio_regs + C2_REGS_Q0_POOLSTART)),
		       mmio_regs +
		       be32_to_cpu(readl(mmio_regs + C2_REGS_Q0_SHARED)),
		       C2_MQ_ADAPTER_TARGET);

	/* Initialize the Verbs Reply Queue */
	qsize = be32_to_cpu(readl(mmio_regs + C2_REGS_Q1_QSIZE));
	msgsize = be32_to_cpu(readl(mmio_regs + C2_REGS_Q1_MSGSIZE));
	q1_pages = kmalloc(qsize * msgsize, GFP_KERNEL);
	if (!q1_pages) {
		err = -ENOMEM;
		goto bail1;
	}
	c2dev->rep_vq.host_dma = dma_map_single(c2dev->ibdev.dma_device,
					        (void *)q1_pages, qsize * msgsize,
				      		DMA_FROM_DEVICE);
	pci_unmap_addr_set(&c2dev->rep_vq, mapping, c2dev->rep_vq.host_dma);
	pr_debug("%s rep_vq va %p dma %llx\n", __FUNCTION__, q1_pages,
		 (unsigned long long) c2dev->rep_vq.host_dma);
	c2_mq_rep_init(&c2dev->rep_vq,
		   1,
		   qsize,
		   msgsize,
		   q1_pages,
		   mmio_regs +
		   be32_to_cpu(readl(mmio_regs + C2_REGS_Q1_SHARED)),
		   C2_MQ_HOST_TARGET);

	/* Initialize the Asynchronus Event Queue */
	qsize = be32_to_cpu(readl(mmio_regs + C2_REGS_Q2_QSIZE));
	msgsize = be32_to_cpu(readl(mmio_regs + C2_REGS_Q2_MSGSIZE));
	q2_pages = kmalloc(qsize * msgsize, GFP_KERNEL);
	if (!q2_pages) {
		err = -ENOMEM;
		goto bail2;
	}
	c2dev->aeq.host_dma = dma_map_single(c2dev->ibdev.dma_device,
					        (void *)q2_pages, qsize * msgsize,
				      		DMA_FROM_DEVICE);
	pci_unmap_addr_set(&c2dev->aeq, mapping, c2dev->aeq.host_dma);
	pr_debug("%s aeq va %p dma %llx\n", __FUNCTION__, q1_pages,
		 (unsigned long long) c2dev->rep_vq.host_dma);
	c2_mq_rep_init(&c2dev->aeq,
		       2,
		       qsize,
		       msgsize,
		       q2_pages,
		       mmio_regs +
		       be32_to_cpu(readl(mmio_regs + C2_REGS_Q2_SHARED)),
		       C2_MQ_HOST_TARGET);

	/* Initialize the verbs request allocator */
	err = vq_init(c2dev);
	if (err)
		goto bail3;

	/* Enable interrupts on the adapter */
	writel(0, c2dev->regs + C2_IDIS);

	/* create the WR init message */
	err = c2_adapter_init(c2dev);
	if (err)
		goto bail4;
	c2dev->init++;

	/* open an adapter instance */
	err = c2_rnic_open(c2dev);
	if (err)
		goto bail4;

	/* Initialize cached the adapter limits */
	if (c2_rnic_query(c2dev, &c2dev->props))
		goto bail5;

	/* Initialize the PD pool */
	err = c2_init_pd_table(c2dev);
	if (err)
		goto bail5;

	/* Initialize the QP pool */
	c2_init_qp_table(c2dev);
	return 0;

      bail5:
	c2_rnic_close(c2dev);
      bail4:
	vq_term(c2dev);
      bail3:
	dma_unmap_single(c2dev->ibdev.dma_device,
			 pci_unmap_addr(&c2dev->aeq, mapping),
			 c2dev->aeq.q_size * c2dev->aeq.msg_size,
		  	 DMA_FROM_DEVICE);
	kfree(q2_pages);
      bail2:
	dma_unmap_single(c2dev->ibdev.dma_device,
			 pci_unmap_addr(&c2dev->rep_vq, mapping),
			 c2dev->rep_vq.q_size * c2dev->rep_vq.msg_size,
		  	 DMA_FROM_DEVICE);
	kfree(q1_pages);
      bail1:
	c2_free_mqsp_pool(c2dev, c2dev->kern_mqsp_pool);
      bail0:
	vfree(c2dev->qptr_array);

	return err;
}

/*
 * Called by c2_remove to cleanup the RNIC resources.
 */
void c2_rnic_term(struct c2_dev *c2dev)
{

	/* Close the open adapter instance */
	c2_rnic_close(c2dev);

	/* Send the TERM message to the adapter */
	c2_adapter_term(c2dev);

	/* Disable interrupts on the adapter */
	writel(1, c2dev->regs + C2_IDIS);

	/* Free the QP pool */
	c2_cleanup_qp_table(c2dev);

	/* Free the PD pool */
	c2_cleanup_pd_table(c2dev);

	/* Free the verbs request allocator */
	vq_term(c2dev);

	/* Unmap and free the asynchronus event queue */
	dma_unmap_single(c2dev->ibdev.dma_device,
			 pci_unmap_addr(&c2dev->aeq, mapping),
			 c2dev->aeq.q_size * c2dev->aeq.msg_size,
		  	 DMA_FROM_DEVICE);
	kfree(c2dev->aeq.msg_pool.host);

	/* Unmap and free the verbs reply queue */
	dma_unmap_single(c2dev->ibdev.dma_device,
			 pci_unmap_addr(&c2dev->rep_vq, mapping),
			 c2dev->rep_vq.q_size * c2dev->rep_vq.msg_size,
		  	 DMA_FROM_DEVICE);
	kfree(c2dev->rep_vq.msg_pool.host);

	/* Free the MQ shared pointer pool */
	c2_free_mqsp_pool(c2dev, c2dev->kern_mqsp_pool);

	/* Free the qptr_array */
	vfree(c2dev->qptr_array);

	return;
}
