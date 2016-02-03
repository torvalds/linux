/*
 * Copyright(c) 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include "vt.h"
#include "trace.h"

#define RVT_UVERBS_ABI_VERSION 2

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("RDMA Verbs Transport Library");

static int rvt_init(void)
{
	/* Do any work needed prior to drivers calling for registration*/
	return 0;
}
module_init(rvt_init);

static void rvt_cleanup(void)
{
}
module_exit(rvt_cleanup);

struct rvt_dev_info *rvt_alloc_device(size_t size, int nports)
{
	struct rvt_dev_info *rdi = ERR_PTR(-ENOMEM);

	rdi = (struct rvt_dev_info *)ib_alloc_device(size);
	if (!rdi)
		return rdi;

	rdi->ports = kcalloc(nports,
			     sizeof(struct rvt_ibport **),
			     GFP_KERNEL);
	if (!rdi->ports)
		ib_dealloc_device(&rdi->ibdev);

	return rdi;
}
EXPORT_SYMBOL(rvt_alloc_device);

static int rvt_query_device(struct ib_device *ibdev,
			    struct ib_device_attr *props,
			    struct ib_udata *uhw)
{
	struct rvt_dev_info *rdi = ib_to_rvt(ibdev);

	if (uhw->inlen || uhw->outlen)
		return -EINVAL;
	/*
	 * Return rvt_dev_info.dparms.props contents
	 */
	*props = rdi->dparms.props;
	return 0;
}

static int rvt_modify_device(struct ib_device *device,
			     int device_modify_mask,
			     struct ib_device_modify *device_modify)
{
	/*
	 * Change dev props. Planned support is for node desc change and sys
	 * guid change only. This matches hfi1 and qib behavior. Other drivers
	 * that support existing modifications will need to add their support.
	 */

	/*
	 * VT-DRIVER-API: node_desc_change()
	 * VT-DRIVER-API: sys_guid_change()
	 */
	return -EOPNOTSUPP;
}

/**
 * rvt_query_port: Passes the query port call to the driver
 * @ibdev: Verbs IB dev
 * @port_num: port number, 1 based from ib core
 * @props: structure to hold returned properties
 *
 * Returns 0 on success
 */
static int rvt_query_port(struct ib_device *ibdev, u8 port_num,
			  struct ib_port_attr *props)
{
	if (ibport_num_to_idx(ibdev, port_num) < 0)
		return -EINVAL;

	/*
	 * VT-DRIVER-API: query_port_state()
	 * driver returns pretty much everything in ib_port_attr
	 */
	return -EOPNOTSUPP;
}

/**
 * rvt_modify_port
 * @ibdev: Verbs IB dev
 * @port_num: Port number, 1 based from ib core
 * @port_modify_mask: How to change the port
 * @props: Structure to fill in
 *
 * Returns 0 on success
 */
static int rvt_modify_port(struct ib_device *ibdev, u8 port_num,
			   int port_modify_mask, struct ib_port_modify *props)
{
	/*
	 * VT-DRIVER-API: set_link_state()
	 * driver will set the link state using the IB enumeration
	 *
	 * VT-DRIVER-API: clear_qkey_violations()
	 * clears driver private qkey counter
	 *
	 * VT-DRIVER-API: get_lid()
	 * driver needs to return the LID
	 *
	 * TBD: send_trap() and post_mad_send() need examined to see where they
	 * fit in.
	 */
	if (ibport_num_to_idx(ibdev, port_num) < 0)
		return -EINVAL;

	return -EOPNOTSUPP;
}

/**
 * rvt_query_pkey - Return a pkey from the table at a given index
 * @ibdev: Verbs IB dev
 * @port_num: Port number, 1 based from ib core
 * @intex: Index into pkey table
 *
 * Returns 0 on failure pkey otherwise
 */
static int rvt_query_pkey(struct ib_device *ibdev, u8 port_num, u16 index,
			  u16 *pkey)
{
	/*
	 * Driver will be responsible for keeping rvt_dev_info.pkey_table up to
	 * date. This function will just return that value. There is no need to
	 * lock, if a stale value is read and sent to the user so be it there is
	 * no way to protect against that anyway.
	 */
	struct rvt_dev_info *rdi = ib_to_rvt(ibdev);
	int port_index;

	port_index = ibport_num_to_idx(ibdev, port_num);
	if (port_index < 0)
		return -EINVAL;

	if (index >= rvt_get_npkeys(rdi))
		return -EINVAL;

	*pkey = rvt_get_pkey(rdi, port_index, index);
	return 0;
}

/**
 * rvt_query_gid - Return a gid from the table
 * @ibdev: Verbs IB dev
 * @port_num: Port number, 1 based from ib core
 * @index: = Index in table
 * @gid: Gid to return
 *
 * Returns 0 on success
 */
static int rvt_query_gid(struct ib_device *ibdev, u8 port_num,
			 int index, union ib_gid *gid)
{
	/*
	 * Driver is responsible for updating the guid table. Which will be used
	 * to craft the return value. This will work similar to how query_pkey()
	 * is being done.
	 */
	if (ibport_num_to_idx(ibdev, port_num) < 0)
		return -EINVAL;

	return -EOPNOTSUPP;
}

struct rvt_ucontext {
	struct ib_ucontext ibucontext;
};

static inline struct rvt_ucontext *to_iucontext(struct ib_ucontext
						*ibucontext)
{
	return container_of(ibucontext, struct rvt_ucontext, ibucontext);
}

/**
 * rvt_alloc_ucontext - Allocate a user context
 * @ibdev: Vers IB dev
 * @data: User data allocated
 */
static struct ib_ucontext *rvt_alloc_ucontext(struct ib_device *ibdev,
					      struct ib_udata *udata)
{
	struct rvt_ucontext *context;

	context = kmalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return ERR_PTR(-ENOMEM);
	return &context->ibucontext;
}

/**
 *rvt_dealloc_ucontext - Free a user context
 *@context - Free this
 */
static int rvt_dealloc_ucontext(struct ib_ucontext *context)
{
	kfree(to_iucontext(context));
	return 0;
}

static int rvt_get_port_immutable(struct ib_device *ibdev, u8 port_num,
				  struct ib_port_immutable *immutable)
{
	return -EOPNOTSUPP;
}

/*
 * Check driver override. If driver passes a value use it, otherwise we use our
 * own value.
 */
#define CHECK_DRIVER_OVERRIDE(rdi, x) \
	rdi->ibdev.x = rdi->ibdev.x ? : rvt_ ##x

int rvt_register_device(struct rvt_dev_info *rdi)
{
	/* Validate that drivers have provided the right information */
	int ret = 0;

	if (!rdi)
		return -EINVAL;

	if ((!rdi->driver_f.port_callback) ||
	    (!rdi->driver_f.get_card_name) ||
	    (!rdi->driver_f.get_pci_dev) ||
	    (!rdi->driver_f.check_ah)) {
		pr_err("Driver not supporting req func\n");
		return -EINVAL;
	}

	/* Once we get past here we can use rvt_pr macros and tracepoints */
	trace_rvt_dbg(rdi, "Driver attempting registration");
	rvt_mmap_init(rdi);

	/* Dev Ops */
	CHECK_DRIVER_OVERRIDE(rdi, query_device);
	CHECK_DRIVER_OVERRIDE(rdi, modify_device);
	CHECK_DRIVER_OVERRIDE(rdi, query_port);
	CHECK_DRIVER_OVERRIDE(rdi, modify_port);
	CHECK_DRIVER_OVERRIDE(rdi, query_pkey);
	CHECK_DRIVER_OVERRIDE(rdi, query_gid);
	CHECK_DRIVER_OVERRIDE(rdi, alloc_ucontext);
	CHECK_DRIVER_OVERRIDE(rdi, dealloc_ucontext);
	CHECK_DRIVER_OVERRIDE(rdi, get_port_immutable);

	/* Queue Pairs */
	ret = rvt_driver_qp_init(rdi);
	if (ret) {
		pr_err("Error in driver QP init.\n");
		return -EINVAL;
	}

	CHECK_DRIVER_OVERRIDE(rdi, create_qp);
	CHECK_DRIVER_OVERRIDE(rdi, modify_qp);
	CHECK_DRIVER_OVERRIDE(rdi, destroy_qp);
	CHECK_DRIVER_OVERRIDE(rdi, query_qp);
	CHECK_DRIVER_OVERRIDE(rdi, post_send);
	CHECK_DRIVER_OVERRIDE(rdi, post_recv);
	CHECK_DRIVER_OVERRIDE(rdi, post_srq_recv);

	/* Address Handle */
	CHECK_DRIVER_OVERRIDE(rdi, create_ah);
	CHECK_DRIVER_OVERRIDE(rdi, destroy_ah);
	CHECK_DRIVER_OVERRIDE(rdi, modify_ah);
	CHECK_DRIVER_OVERRIDE(rdi, query_ah);
	spin_lock_init(&rdi->n_ahs_lock);
	rdi->n_ahs_allocated = 0;

	/* Shared Receive Queue */
	CHECK_DRIVER_OVERRIDE(rdi, create_srq);
	CHECK_DRIVER_OVERRIDE(rdi, modify_srq);
	CHECK_DRIVER_OVERRIDE(rdi, destroy_srq);
	CHECK_DRIVER_OVERRIDE(rdi, query_srq);
	rvt_driver_srq_init(rdi);

	/* Multicast */
	rvt_driver_mcast_init(rdi);
	CHECK_DRIVER_OVERRIDE(rdi, attach_mcast);
	CHECK_DRIVER_OVERRIDE(rdi, detach_mcast);

	/* Mem Region */
	ret = rvt_driver_mr_init(rdi);
	if (ret) {
		pr_err("Error in driver MR init.\n");
		goto bail_no_mr;
	}

	CHECK_DRIVER_OVERRIDE(rdi, get_dma_mr);
	CHECK_DRIVER_OVERRIDE(rdi, reg_user_mr);
	CHECK_DRIVER_OVERRIDE(rdi, dereg_mr);
	CHECK_DRIVER_OVERRIDE(rdi, alloc_mr);
	CHECK_DRIVER_OVERRIDE(rdi, alloc_fmr);
	CHECK_DRIVER_OVERRIDE(rdi, map_phys_fmr);
	CHECK_DRIVER_OVERRIDE(rdi, unmap_fmr);
	CHECK_DRIVER_OVERRIDE(rdi, dealloc_fmr);
	CHECK_DRIVER_OVERRIDE(rdi, mmap);

	/* Completion queues */
	ret = rvt_driver_cq_init(rdi);
	if (ret) {
		pr_err("Error in driver CQ init.\n");
		goto bail_mr;
	}
	CHECK_DRIVER_OVERRIDE(rdi, create_cq);
	CHECK_DRIVER_OVERRIDE(rdi, destroy_cq);
	CHECK_DRIVER_OVERRIDE(rdi, poll_cq);
	CHECK_DRIVER_OVERRIDE(rdi, req_notify_cq);
	CHECK_DRIVER_OVERRIDE(rdi, resize_cq);

	/* DMA Operations */
	rdi->ibdev.dma_ops =
		rdi->ibdev.dma_ops ? : &rvt_default_dma_mapping_ops;

	/* Protection Domain */
	CHECK_DRIVER_OVERRIDE(rdi, alloc_pd);
	CHECK_DRIVER_OVERRIDE(rdi, dealloc_pd);
	spin_lock_init(&rdi->n_pds_lock);
	rdi->n_pds_allocated = 0;

	/*
	 * There are some things which could be set by underlying drivers but
	 * really should be up to rdmavt to set. For instance drivers can't know
	 * exactly which functions rdmavt supports, nor do they know the ABI
	 * version, so we do all of this sort of stuff here.
	 */
	rdi->ibdev.uverbs_abi_ver = RVT_UVERBS_ABI_VERSION;
	rdi->ibdev.uverbs_cmd_mask =
		(1ull << IB_USER_VERBS_CMD_GET_CONTEXT)         |
		(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE)        |
		(1ull << IB_USER_VERBS_CMD_QUERY_PORT)          |
		(1ull << IB_USER_VERBS_CMD_ALLOC_PD)            |
		(1ull << IB_USER_VERBS_CMD_DEALLOC_PD)          |
		(1ull << IB_USER_VERBS_CMD_CREATE_AH)           |
		(1ull << IB_USER_VERBS_CMD_MODIFY_AH)           |
		(1ull << IB_USER_VERBS_CMD_QUERY_AH)            |
		(1ull << IB_USER_VERBS_CMD_DESTROY_AH)          |
		(1ull << IB_USER_VERBS_CMD_REG_MR)              |
		(1ull << IB_USER_VERBS_CMD_DEREG_MR)            |
		(1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL) |
		(1ull << IB_USER_VERBS_CMD_CREATE_CQ)           |
		(1ull << IB_USER_VERBS_CMD_RESIZE_CQ)           |
		(1ull << IB_USER_VERBS_CMD_DESTROY_CQ)          |
		(1ull << IB_USER_VERBS_CMD_POLL_CQ)             |
		(1ull << IB_USER_VERBS_CMD_REQ_NOTIFY_CQ)       |
		(1ull << IB_USER_VERBS_CMD_CREATE_QP)           |
		(1ull << IB_USER_VERBS_CMD_QUERY_QP)            |
		(1ull << IB_USER_VERBS_CMD_MODIFY_QP)           |
		(1ull << IB_USER_VERBS_CMD_DESTROY_QP)          |
		(1ull << IB_USER_VERBS_CMD_POST_SEND)           |
		(1ull << IB_USER_VERBS_CMD_POST_RECV)           |
		(1ull << IB_USER_VERBS_CMD_ATTACH_MCAST)        |
		(1ull << IB_USER_VERBS_CMD_DETACH_MCAST)        |
		(1ull << IB_USER_VERBS_CMD_CREATE_SRQ)          |
		(1ull << IB_USER_VERBS_CMD_MODIFY_SRQ)          |
		(1ull << IB_USER_VERBS_CMD_QUERY_SRQ)           |
		(1ull << IB_USER_VERBS_CMD_DESTROY_SRQ)         |
		(1ull << IB_USER_VERBS_CMD_POST_SRQ_RECV);
	rdi->ibdev.node_type = RDMA_NODE_IB_CA;
	rdi->ibdev.num_comp_vectors = 1;

	/* We are now good to announce we exist */
	ret =  ib_register_device(&rdi->ibdev, rdi->driver_f.port_callback);
	if (ret) {
		rvt_pr_err(rdi, "Failed to register driver with ib core.\n");
		goto bail_cq;
	}

	rvt_create_mad_agents(rdi);

	rvt_pr_info(rdi, "Registration with rdmavt done.\n");
	return ret;

bail_cq:
	rvt_cq_exit(rdi);

bail_mr:
	rvt_mr_exit(rdi);

bail_no_mr:
	rvt_qp_exit(rdi);

	return ret;
}
EXPORT_SYMBOL(rvt_register_device);

void rvt_unregister_device(struct rvt_dev_info *rdi)
{
	trace_rvt_dbg(rdi, "Driver is unregistering.");
	if (!rdi)
		return;

	rvt_free_mad_agents(rdi);

	ib_unregister_device(&rdi->ibdev);
	rvt_cq_exit(rdi);
	rvt_mr_exit(rdi);
	rvt_qp_exit(rdi);
}
EXPORT_SYMBOL(rvt_unregister_device);

/*
 * Keep track of a list of ports. No need to have a detach port.
 * They persist until the driver goes away.
 */
int rvt_init_port(struct rvt_dev_info *rdi, struct rvt_ibport *port,
		  int port_index, u16 *pkey_table)
{

	rdi->ports[port_index] = port;
	rdi->ports[port_index]->pkey_table = pkey_table;

	return 0;
}
EXPORT_SYMBOL(rvt_init_port);
