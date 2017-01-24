/*
 * Copyright (c) 2012-2016 VMware, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of EITHER the GNU General Public License
 * version 2 as published by the Free Software Foundation or the BSD
 * 2-Clause License. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License version 2 for more details at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program available in the file COPYING in the main
 * directory of this source tree.
 *
 * The BSD 2-Clause License
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <asm/page.h>
#include <linux/inet.h>
#include <linux/io.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/vmw_pvrdma-abi.h>

#include "pvrdma.h"

/**
 * pvrdma_query_device - query device
 * @ibdev: the device to query
 * @props: the device properties
 * @uhw: user data
 *
 * @return: 0 on success, otherwise negative errno
 */
int pvrdma_query_device(struct ib_device *ibdev,
			struct ib_device_attr *props,
			struct ib_udata *uhw)
{
	struct pvrdma_dev *dev = to_vdev(ibdev);

	if (uhw->inlen || uhw->outlen)
		return -EINVAL;

	memset(props, 0, sizeof(*props));

	props->fw_ver = dev->dsr->caps.fw_ver;
	props->sys_image_guid = dev->dsr->caps.sys_image_guid;
	props->max_mr_size = dev->dsr->caps.max_mr_size;
	props->page_size_cap = dev->dsr->caps.page_size_cap;
	props->vendor_id = dev->dsr->caps.vendor_id;
	props->vendor_part_id = dev->pdev->device;
	props->hw_ver = dev->dsr->caps.hw_ver;
	props->max_qp = dev->dsr->caps.max_qp;
	props->max_qp_wr = dev->dsr->caps.max_qp_wr;
	props->device_cap_flags = dev->dsr->caps.device_cap_flags;
	props->max_sge = dev->dsr->caps.max_sge;
	props->max_cq = dev->dsr->caps.max_cq;
	props->max_cqe = dev->dsr->caps.max_cqe;
	props->max_mr = dev->dsr->caps.max_mr;
	props->max_pd = dev->dsr->caps.max_pd;
	props->max_qp_rd_atom = dev->dsr->caps.max_qp_rd_atom;
	props->max_qp_init_rd_atom = dev->dsr->caps.max_qp_init_rd_atom;
	props->atomic_cap =
		dev->dsr->caps.atomic_ops &
		(PVRDMA_ATOMIC_OP_COMP_SWAP | PVRDMA_ATOMIC_OP_FETCH_ADD) ?
		IB_ATOMIC_HCA : IB_ATOMIC_NONE;
	props->masked_atomic_cap = props->atomic_cap;
	props->max_ah = dev->dsr->caps.max_ah;
	props->max_pkeys = dev->dsr->caps.max_pkeys;
	props->local_ca_ack_delay = dev->dsr->caps.local_ca_ack_delay;
	if ((dev->dsr->caps.bmme_flags & PVRDMA_BMME_FLAG_LOCAL_INV) &&
	    (dev->dsr->caps.bmme_flags & PVRDMA_BMME_FLAG_REMOTE_INV) &&
	    (dev->dsr->caps.bmme_flags & PVRDMA_BMME_FLAG_FAST_REG_WR)) {
		props->device_cap_flags |= IB_DEVICE_MEM_MGT_EXTENSIONS;
	}

	return 0;
}

/**
 * pvrdma_query_port - query device port attributes
 * @ibdev: the device to query
 * @port: the port number
 * @props: the device properties
 *
 * @return: 0 on success, otherwise negative errno
 */
int pvrdma_query_port(struct ib_device *ibdev, u8 port,
		      struct ib_port_attr *props)
{
	struct pvrdma_dev *dev = to_vdev(ibdev);
	union pvrdma_cmd_req req;
	union pvrdma_cmd_resp rsp;
	struct pvrdma_cmd_query_port *cmd = &req.query_port;
	struct pvrdma_cmd_query_port_resp *resp = &rsp.query_port_resp;
	int err;

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_QUERY_PORT;
	cmd->port_num = port;

	err = pvrdma_cmd_post(dev, &req, &rsp, PVRDMA_CMD_QUERY_PORT_RESP);
	if (err < 0) {
		dev_warn(&dev->pdev->dev,
			 "could not query port, error: %d\n", err);
		return err;
	}

	/* props being zeroed by the caller, avoid zeroing it here */

	props->state = pvrdma_port_state_to_ib(resp->attrs.state);
	props->max_mtu = pvrdma_mtu_to_ib(resp->attrs.max_mtu);
	props->active_mtu = pvrdma_mtu_to_ib(resp->attrs.active_mtu);
	props->gid_tbl_len = resp->attrs.gid_tbl_len;
	props->port_cap_flags =
		pvrdma_port_cap_flags_to_ib(resp->attrs.port_cap_flags);
	props->max_msg_sz = resp->attrs.max_msg_sz;
	props->bad_pkey_cntr = resp->attrs.bad_pkey_cntr;
	props->qkey_viol_cntr = resp->attrs.qkey_viol_cntr;
	props->pkey_tbl_len = resp->attrs.pkey_tbl_len;
	props->lid = resp->attrs.lid;
	props->sm_lid = resp->attrs.sm_lid;
	props->lmc = resp->attrs.lmc;
	props->max_vl_num = resp->attrs.max_vl_num;
	props->sm_sl = resp->attrs.sm_sl;
	props->subnet_timeout = resp->attrs.subnet_timeout;
	props->init_type_reply = resp->attrs.init_type_reply;
	props->active_width = pvrdma_port_width_to_ib(resp->attrs.active_width);
	props->active_speed = pvrdma_port_speed_to_ib(resp->attrs.active_speed);
	props->phys_state = resp->attrs.phys_state;

	return 0;
}

/**
 * pvrdma_query_gid - query device gid
 * @ibdev: the device to query
 * @port: the port number
 * @index: the index
 * @gid: the device gid value
 *
 * @return: 0 on success, otherwise negative errno
 */
int pvrdma_query_gid(struct ib_device *ibdev, u8 port, int index,
		     union ib_gid *gid)
{
	struct pvrdma_dev *dev = to_vdev(ibdev);

	if (index >= dev->dsr->caps.gid_tbl_len)
		return -EINVAL;

	memcpy(gid, &dev->sgid_tbl[index], sizeof(union ib_gid));

	return 0;
}

/**
 * pvrdma_query_pkey - query device port's P_Key table
 * @ibdev: the device to query
 * @port: the port number
 * @index: the index
 * @pkey: the device P_Key value
 *
 * @return: 0 on success, otherwise negative errno
 */
int pvrdma_query_pkey(struct ib_device *ibdev, u8 port, u16 index,
		      u16 *pkey)
{
	int err = 0;
	union pvrdma_cmd_req req;
	union pvrdma_cmd_resp rsp;
	struct pvrdma_cmd_query_pkey *cmd = &req.query_pkey;

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_QUERY_PKEY;
	cmd->port_num = port;
	cmd->index = index;

	err = pvrdma_cmd_post(to_vdev(ibdev), &req, &rsp,
			      PVRDMA_CMD_QUERY_PKEY_RESP);
	if (err < 0) {
		dev_warn(&to_vdev(ibdev)->pdev->dev,
			 "could not query pkey, error: %d\n", err);
		return err;
	}

	*pkey = rsp.query_pkey_resp.pkey;

	return 0;
}

enum rdma_link_layer pvrdma_port_link_layer(struct ib_device *ibdev,
					    u8 port)
{
	return IB_LINK_LAYER_ETHERNET;
}

int pvrdma_modify_device(struct ib_device *ibdev, int mask,
			 struct ib_device_modify *props)
{
	unsigned long flags;

	if (mask & ~(IB_DEVICE_MODIFY_SYS_IMAGE_GUID |
		     IB_DEVICE_MODIFY_NODE_DESC)) {
		dev_warn(&to_vdev(ibdev)->pdev->dev,
			 "unsupported device modify mask %#x\n", mask);
		return -EOPNOTSUPP;
	}

	if (mask & IB_DEVICE_MODIFY_NODE_DESC) {
		spin_lock_irqsave(&to_vdev(ibdev)->desc_lock, flags);
		memcpy(ibdev->node_desc, props->node_desc, 64);
		spin_unlock_irqrestore(&to_vdev(ibdev)->desc_lock, flags);
	}

	if (mask & IB_DEVICE_MODIFY_SYS_IMAGE_GUID) {
		mutex_lock(&to_vdev(ibdev)->port_mutex);
		to_vdev(ibdev)->sys_image_guid =
			cpu_to_be64(props->sys_image_guid);
		mutex_unlock(&to_vdev(ibdev)->port_mutex);
	}

	return 0;
}

/**
 * pvrdma_modify_port - modify device port attributes
 * @ibdev: the device to modify
 * @port: the port number
 * @mask: attributes to modify
 * @props: the device properties
 *
 * @return: 0 on success, otherwise negative errno
 */
int pvrdma_modify_port(struct ib_device *ibdev, u8 port, int mask,
		       struct ib_port_modify *props)
{
	struct ib_port_attr attr;
	struct pvrdma_dev *vdev = to_vdev(ibdev);
	int ret;

	if (mask & ~IB_PORT_SHUTDOWN) {
		dev_warn(&vdev->pdev->dev,
			 "unsupported port modify mask %#x\n", mask);
		return -EOPNOTSUPP;
	}

	mutex_lock(&vdev->port_mutex);
	ret = ib_query_port(ibdev, port, &attr);
	if (ret)
		goto out;

	vdev->port_cap_mask |= props->set_port_cap_mask;
	vdev->port_cap_mask &= ~props->clr_port_cap_mask;

	if (mask & IB_PORT_SHUTDOWN)
		vdev->ib_active = false;

out:
	mutex_unlock(&vdev->port_mutex);
	return ret;
}

/**
 * pvrdma_alloc_ucontext - allocate ucontext
 * @ibdev: the IB device
 * @udata: user data
 *
 * @return: the ib_ucontext pointer on success, otherwise errno.
 */
struct ib_ucontext *pvrdma_alloc_ucontext(struct ib_device *ibdev,
					  struct ib_udata *udata)
{
	struct pvrdma_dev *vdev = to_vdev(ibdev);
	struct pvrdma_ucontext *context;
	union pvrdma_cmd_req req;
	union pvrdma_cmd_resp rsp;
	struct pvrdma_cmd_create_uc *cmd = &req.create_uc;
	struct pvrdma_cmd_create_uc_resp *resp = &rsp.create_uc_resp;
	struct pvrdma_alloc_ucontext_resp uresp;
	int ret;
	void *ptr;

	if (!vdev->ib_active)
		return ERR_PTR(-EAGAIN);

	context = kmalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return ERR_PTR(-ENOMEM);

	context->dev = vdev;
	ret = pvrdma_uar_alloc(vdev, &context->uar);
	if (ret) {
		kfree(context);
		return ERR_PTR(-ENOMEM);
	}

	/* get ctx_handle from host */
	memset(cmd, 0, sizeof(*cmd));
	cmd->pfn = context->uar.pfn;
	cmd->hdr.cmd = PVRDMA_CMD_CREATE_UC;
	ret = pvrdma_cmd_post(vdev, &req, &rsp, PVRDMA_CMD_CREATE_UC_RESP);
	if (ret < 0) {
		dev_warn(&vdev->pdev->dev,
			 "could not create ucontext, error: %d\n", ret);
		ptr = ERR_PTR(ret);
		goto err;
	}

	context->ctx_handle = resp->ctx_handle;

	/* copy back to user */
	uresp.qp_tab_size = vdev->dsr->caps.max_qp;
	ret = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
	if (ret) {
		pvrdma_uar_free(vdev, &context->uar);
		context->ibucontext.device = ibdev;
		pvrdma_dealloc_ucontext(&context->ibucontext);
		return ERR_PTR(-EFAULT);
	}

	return &context->ibucontext;

err:
	pvrdma_uar_free(vdev, &context->uar);
	kfree(context);
	return ptr;
}

/**
 * pvrdma_dealloc_ucontext - deallocate ucontext
 * @ibcontext: the ucontext
 *
 * @return: 0 on success, otherwise errno.
 */
int pvrdma_dealloc_ucontext(struct ib_ucontext *ibcontext)
{
	struct pvrdma_ucontext *context = to_vucontext(ibcontext);
	union pvrdma_cmd_req req;
	struct pvrdma_cmd_destroy_uc *cmd = &req.destroy_uc;
	int ret;

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_DESTROY_UC;
	cmd->ctx_handle = context->ctx_handle;

	ret = pvrdma_cmd_post(context->dev, &req, NULL, 0);
	if (ret < 0)
		dev_warn(&context->dev->pdev->dev,
			 "destroy ucontext failed, error: %d\n", ret);

	/* Free the UAR even if the device command failed */
	pvrdma_uar_free(to_vdev(ibcontext->device), &context->uar);
	kfree(context);

	return ret;
}

/**
 * pvrdma_mmap - create mmap region
 * @ibcontext: the user context
 * @vma: the VMA
 *
 * @return: 0 on success, otherwise errno.
 */
int pvrdma_mmap(struct ib_ucontext *ibcontext, struct vm_area_struct *vma)
{
	struct pvrdma_ucontext *context = to_vucontext(ibcontext);
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

	dev_dbg(&context->dev->pdev->dev, "create mmap region\n");

	if ((size != PAGE_SIZE) || (offset & ~PAGE_MASK)) {
		dev_warn(&context->dev->pdev->dev,
			 "invalid params for mmap region\n");
		return -EINVAL;
	}

	/* Map UAR to kernel space, VM_LOCKED? */
	vma->vm_flags |= VM_DONTCOPY | VM_DONTEXPAND;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if (io_remap_pfn_range(vma, start, context->uar.pfn, size,
			       vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

/**
 * pvrdma_alloc_pd - allocate protection domain
 * @ibdev: the IB device
 * @context: user context
 * @udata: user data
 *
 * @return: the ib_pd protection domain pointer on success, otherwise errno.
 */
struct ib_pd *pvrdma_alloc_pd(struct ib_device *ibdev,
			      struct ib_ucontext *context,
			      struct ib_udata *udata)
{
	struct pvrdma_pd *pd;
	struct pvrdma_dev *dev = to_vdev(ibdev);
	union pvrdma_cmd_req req;
	union pvrdma_cmd_resp rsp;
	struct pvrdma_cmd_create_pd *cmd = &req.create_pd;
	struct pvrdma_cmd_create_pd_resp *resp = &rsp.create_pd_resp;
	int ret;
	void *ptr;

	/* Check allowed max pds */
	if (!atomic_add_unless(&dev->num_pds, 1, dev->dsr->caps.max_pd))
		return ERR_PTR(-ENOMEM);

	pd = kmalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		ptr = ERR_PTR(-ENOMEM);
		goto err;
	}

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_CREATE_PD;
	cmd->ctx_handle = (context) ? to_vucontext(context)->ctx_handle : 0;
	ret = pvrdma_cmd_post(dev, &req, &rsp, PVRDMA_CMD_CREATE_PD_RESP);
	if (ret < 0) {
		dev_warn(&dev->pdev->dev,
			 "failed to allocate protection domain, error: %d\n",
			 ret);
		ptr = ERR_PTR(ret);
		goto freepd;
	}

	pd->privileged = !context;
	pd->pd_handle = resp->pd_handle;
	pd->pdn = resp->pd_handle;

	if (context) {
		if (ib_copy_to_udata(udata, &pd->pdn, sizeof(__u32))) {
			dev_warn(&dev->pdev->dev,
				 "failed to copy back protection domain\n");
			pvrdma_dealloc_pd(&pd->ibpd);
			return ERR_PTR(-EFAULT);
		}
	}

	/* u32 pd handle */
	return &pd->ibpd;

freepd:
	kfree(pd);
err:
	atomic_dec(&dev->num_pds);
	return ptr;
}

/**
 * pvrdma_dealloc_pd - deallocate protection domain
 * @pd: the protection domain to be released
 *
 * @return: 0 on success, otherwise errno.
 */
int pvrdma_dealloc_pd(struct ib_pd *pd)
{
	struct pvrdma_dev *dev = to_vdev(pd->device);
	union pvrdma_cmd_req req;
	struct pvrdma_cmd_destroy_pd *cmd = &req.destroy_pd;
	int ret;

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_DESTROY_PD;
	cmd->pd_handle = to_vpd(pd)->pd_handle;

	ret = pvrdma_cmd_post(dev, &req, NULL, 0);
	if (ret)
		dev_warn(&dev->pdev->dev,
			 "could not dealloc protection domain, error: %d\n",
			 ret);

	kfree(to_vpd(pd));
	atomic_dec(&dev->num_pds);

	return 0;
}

/**
 * pvrdma_create_ah - create an address handle
 * @pd: the protection domain
 * @ah_attr: the attributes of the AH
 * @udata: user data blob
 *
 * @return: the ib_ah pointer on success, otherwise errno.
 */
struct ib_ah *pvrdma_create_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr,
			       struct ib_udata *udata)
{
	struct pvrdma_dev *dev = to_vdev(pd->device);
	struct pvrdma_ah *ah;
	enum rdma_link_layer ll;

	if (!(ah_attr->ah_flags & IB_AH_GRH))
		return ERR_PTR(-EINVAL);

	ll = rdma_port_get_link_layer(pd->device, ah_attr->port_num);

	if (ll != IB_LINK_LAYER_ETHERNET ||
	    rdma_is_multicast_addr((struct in6_addr *)ah_attr->grh.dgid.raw))
		return ERR_PTR(-EINVAL);

	if (!atomic_add_unless(&dev->num_ahs, 1, dev->dsr->caps.max_ah))
		return ERR_PTR(-ENOMEM);

	ah = kzalloc(sizeof(*ah), GFP_KERNEL);
	if (!ah) {
		atomic_dec(&dev->num_ahs);
		return ERR_PTR(-ENOMEM);
	}

	ah->av.port_pd = to_vpd(pd)->pd_handle | (ah_attr->port_num << 24);
	ah->av.src_path_bits = ah_attr->src_path_bits;
	ah->av.src_path_bits |= 0x80;
	ah->av.gid_index = ah_attr->grh.sgid_index;
	ah->av.hop_limit = ah_attr->grh.hop_limit;
	ah->av.sl_tclass_flowlabel = (ah_attr->grh.traffic_class << 20) |
				      ah_attr->grh.flow_label;
	memcpy(ah->av.dgid, ah_attr->grh.dgid.raw, 16);
	memcpy(ah->av.dmac, ah_attr->dmac, 6);

	ah->ibah.device = pd->device;
	ah->ibah.pd = pd;
	ah->ibah.uobject = NULL;

	return &ah->ibah;
}

/**
 * pvrdma_destroy_ah - destroy an address handle
 * @ah: the address handle to destroyed
 *
 * @return: 0 on success.
 */
int pvrdma_destroy_ah(struct ib_ah *ah)
{
	struct pvrdma_dev *dev = to_vdev(ah->device);

	kfree(to_vah(ah));
	atomic_dec(&dev->num_ahs);

	return 0;
}
