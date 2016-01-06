/*
 * Copyright(c) 2015 Intel Corporation.
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

static int rvt_query_device(struct ib_device *ibdev,
			    struct ib_device_attr *props,
			    struct ib_udata *uhw)
{
	/*
	 * Return rvt_dev_info.props contents
	 */
	return -EOPNOTSUPP;
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
 * @port: port number
 * @props: structure to hold returned properties
 *
 * Returns 0 on success
 */
static int rvt_query_port(struct ib_device *ibdev, u8 port,
			  struct ib_port_attr *props)
{
	/*
	 * VT-DRIVER-API: query_port_state()
	 * driver returns pretty much everything in ib_port_attr
	 */
	return -EOPNOTSUPP;
}

/**
 * rvt_modify_port
 * @ibdev: Verbs IB dev
 * @port: Port number
 * @port_modify_mask: How to change the port
 * @props: Structure to fill in
 *
 * Returns 0 on success
 */
static int rvt_modify_port(struct ib_device *ibdev, u8 port,
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
	return -EOPNOTSUPP;
}

/**
 * rvt_query_pkey - Return a pkey from the table at a given index
 * @ibdev: Verbs IB dev
 * @port: Port number
 * @intex: Index into pkey table
 *
 * Returns 0 on failure pkey otherwise
 */
static int rvt_query_pkey(struct ib_device *ibdev, u8 port, u16 index,
			  u16 *pkey)
{
	/*
	 * Driver will be responsible for keeping rvt_dev_info.pkey_table up to
	 * date. This function will just return that value. There is no need to
	 * lock, if a stale value is read and sent to the user so be it there is
	 * no way to protect against that anyway.
	 */
	return 0;
}

/**
 * rvt_query_gid - Return a gid from the table
 * @ibdev: Verbs IB dev
 * @port: Port number
 * @index: = Index in table
 * @gid: Gid to return
 *
 * Returns 0 on success
 */
static int rvt_query_gid(struct ib_device *ibdev, u8 port,
			 int index, union ib_gid *gid)
{
	/*
	 * Driver is responsible for updating the guid table. Which will be used
	 * to craft the return value. This will work similar to how query_pkey()
	 * is being done.
	 */

	return -EOPNOTSUPP;
}

/**
 * rvt_alloc_ucontext - Allocate a user context
 * @ibdev: Vers IB dev
 * @data: User data allocated
 */
static struct ib_ucontext *rvt_alloc_ucontext(struct ib_device *ibdev,
					      struct ib_udata *udata)
{
	return ERR_PTR(-EOPNOTSUPP);
}

/**
 *rvt_dealloc_ucontext - Free a user context
 *@context - Free this
 */
static int rvt_dealloc_ucontext(struct ib_ucontext *context)
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
	if (!rdi)
		return -EINVAL;

	/* Dev Ops */
	CHECK_DRIVER_OVERRIDE(rdi, query_device);
	CHECK_DRIVER_OVERRIDE(rdi, modify_device);
	CHECK_DRIVER_OVERRIDE(rdi, query_port);
	CHECK_DRIVER_OVERRIDE(rdi, modify_port);
	CHECK_DRIVER_OVERRIDE(rdi, query_pkey);
	CHECK_DRIVER_OVERRIDE(rdi, query_gid);
	CHECK_DRIVER_OVERRIDE(rdi, alloc_ucontext);
	CHECK_DRIVER_OVERRIDE(rdi, dealloc_ucontext);

	/* Queue Pairs */
	CHECK_DRIVER_OVERRIDE(rdi, create_qp);
	CHECK_DRIVER_OVERRIDE(rdi, modify_qp);
	CHECK_DRIVER_OVERRIDE(rdi, destroy_qp);
	CHECK_DRIVER_OVERRIDE(rdi, query_qp);

	/* Address Handle */
	CHECK_DRIVER_OVERRIDE(rdi, create_ah);
	CHECK_DRIVER_OVERRIDE(rdi, destroy_ah);
	CHECK_DRIVER_OVERRIDE(rdi, modify_ah);
	CHECK_DRIVER_OVERRIDE(rdi, query_ah);

	/* DMA Operations */
	rdi->ibdev.dma_ops =
		rdi->ibdev.dma_ops ? : &rvt_default_dma_mapping_ops;

	/* Protection Domain */
	CHECK_DRIVER_OVERRIDE(rdi, alloc_pd);
	CHECK_DRIVER_OVERRIDE(rdi, dealloc_pd);
	spin_lock_init(&rdi->n_pds_lock);
	rdi->n_pds_allocated = 0;

	/* We are now good to announce we exist */
	return ib_register_device(&rdi->ibdev, rdi->port_callback);
}
EXPORT_SYMBOL(rvt_register_device);

void rvt_unregister_device(struct rvt_dev_info *rdi)
{
	if (!rdi)
		return;

	ib_unregister_device(&rdi->ibdev);
}
EXPORT_SYMBOL(rvt_unregister_device);
