/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 PathScale, Inc.  All rights reserved.
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
 * $Id: uverbs_cmd.c 2708 2005-06-24 17:27:21Z roland $
 */

#include <linux/file.h>
#include <linux/fs.h>

#include <asm/uaccess.h>

#include "uverbs.h"

#define INIT_UDATA(udata, ibuf, obuf, ilen, olen)			\
	do {								\
		(udata)->inbuf  = (void __user *) (ibuf);		\
		(udata)->outbuf = (void __user *) (obuf);		\
		(udata)->inlen  = (ilen);				\
		(udata)->outlen = (olen);				\
	} while (0)

ssize_t ib_uverbs_get_context(struct ib_uverbs_file *file,
			      const char __user *buf,
			      int in_len, int out_len)
{
	struct ib_uverbs_get_context      cmd;
	struct ib_uverbs_get_context_resp resp;
	struct ib_udata                   udata;
	struct ib_device                 *ibdev = file->device->ib_dev;
	struct ib_ucontext		 *ucontext;
	struct file			 *filp;
	int ret;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	down(&file->mutex);

	if (file->ucontext) {
		ret = -EINVAL;
		goto err;
	}

	INIT_UDATA(&udata, buf + sizeof cmd,
		   (unsigned long) cmd.response + sizeof resp,
		   in_len - sizeof cmd, out_len - sizeof resp);

	ucontext = ibdev->alloc_ucontext(ibdev, &udata);
	if (IS_ERR(ucontext))
		return PTR_ERR(file->ucontext);

	ucontext->device = ibdev;
	INIT_LIST_HEAD(&ucontext->pd_list);
	INIT_LIST_HEAD(&ucontext->mr_list);
	INIT_LIST_HEAD(&ucontext->mw_list);
	INIT_LIST_HEAD(&ucontext->cq_list);
	INIT_LIST_HEAD(&ucontext->qp_list);
	INIT_LIST_HEAD(&ucontext->srq_list);
	INIT_LIST_HEAD(&ucontext->ah_list);

	resp.num_comp_vectors = file->device->num_comp_vectors;

	filp = ib_uverbs_alloc_event_file(file, 1, &resp.async_fd);
	if (IS_ERR(filp)) {
		ret = PTR_ERR(filp);
		goto err_free;
	}

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		ret = -EFAULT;
		goto err_file;
	}

	file->async_file = filp->private_data;

	INIT_IB_EVENT_HANDLER(&file->event_handler, file->device->ib_dev,
			      ib_uverbs_event_handler);
	ret = ib_register_event_handler(&file->event_handler);
	if (ret)
		goto err_file;

	kref_get(&file->async_file->ref);
	kref_get(&file->ref);
	file->ucontext = ucontext;

	fd_install(resp.async_fd, filp);

	up(&file->mutex);

	return in_len;

err_file:
	put_unused_fd(resp.async_fd);
	fput(filp);

err_free:
	ibdev->dealloc_ucontext(ucontext);

err:
	up(&file->mutex);
	return ret;
}

ssize_t ib_uverbs_query_device(struct ib_uverbs_file *file,
			       const char __user *buf,
			       int in_len, int out_len)
{
	struct ib_uverbs_query_device      cmd;
	struct ib_uverbs_query_device_resp resp;
	struct ib_device_attr              attr;
	int                                ret;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	ret = ib_query_device(file->device->ib_dev, &attr);
	if (ret)
		return ret;

	memset(&resp, 0, sizeof resp);

	resp.fw_ver 		       = attr.fw_ver;
	resp.node_guid 		       = attr.node_guid;
	resp.sys_image_guid 	       = attr.sys_image_guid;
	resp.max_mr_size 	       = attr.max_mr_size;
	resp.page_size_cap 	       = attr.page_size_cap;
	resp.vendor_id 		       = attr.vendor_id;
	resp.vendor_part_id 	       = attr.vendor_part_id;
	resp.hw_ver 		       = attr.hw_ver;
	resp.max_qp 		       = attr.max_qp;
	resp.max_qp_wr 		       = attr.max_qp_wr;
	resp.device_cap_flags 	       = attr.device_cap_flags;
	resp.max_sge 		       = attr.max_sge;
	resp.max_sge_rd 	       = attr.max_sge_rd;
	resp.max_cq 		       = attr.max_cq;
	resp.max_cqe 		       = attr.max_cqe;
	resp.max_mr 		       = attr.max_mr;
	resp.max_pd 		       = attr.max_pd;
	resp.max_qp_rd_atom 	       = attr.max_qp_rd_atom;
	resp.max_ee_rd_atom 	       = attr.max_ee_rd_atom;
	resp.max_res_rd_atom 	       = attr.max_res_rd_atom;
	resp.max_qp_init_rd_atom       = attr.max_qp_init_rd_atom;
	resp.max_ee_init_rd_atom       = attr.max_ee_init_rd_atom;
	resp.atomic_cap 	       = attr.atomic_cap;
	resp.max_ee 		       = attr.max_ee;
	resp.max_rdd 		       = attr.max_rdd;
	resp.max_mw 		       = attr.max_mw;
	resp.max_raw_ipv6_qp 	       = attr.max_raw_ipv6_qp;
	resp.max_raw_ethy_qp 	       = attr.max_raw_ethy_qp;
	resp.max_mcast_grp 	       = attr.max_mcast_grp;
	resp.max_mcast_qp_attach       = attr.max_mcast_qp_attach;
	resp.max_total_mcast_qp_attach = attr.max_total_mcast_qp_attach;
	resp.max_ah 		       = attr.max_ah;
	resp.max_fmr 		       = attr.max_fmr;
	resp.max_map_per_fmr 	       = attr.max_map_per_fmr;
	resp.max_srq 		       = attr.max_srq;
	resp.max_srq_wr 	       = attr.max_srq_wr;
	resp.max_srq_sge 	       = attr.max_srq_sge;
	resp.max_pkeys 		       = attr.max_pkeys;
	resp.local_ca_ack_delay        = attr.local_ca_ack_delay;
	resp.phys_port_cnt	       = file->device->ib_dev->phys_port_cnt;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		return -EFAULT;

	return in_len;
}

ssize_t ib_uverbs_query_port(struct ib_uverbs_file *file,
			     const char __user *buf,
			     int in_len, int out_len)
{
	struct ib_uverbs_query_port      cmd;
	struct ib_uverbs_query_port_resp resp;
	struct ib_port_attr              attr;
	int                              ret;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	ret = ib_query_port(file->device->ib_dev, cmd.port_num, &attr);
	if (ret)
		return ret;

	memset(&resp, 0, sizeof resp);

	resp.state 	     = attr.state;
	resp.max_mtu 	     = attr.max_mtu;
	resp.active_mtu      = attr.active_mtu;
	resp.gid_tbl_len     = attr.gid_tbl_len;
	resp.port_cap_flags  = attr.port_cap_flags;
	resp.max_msg_sz      = attr.max_msg_sz;
	resp.bad_pkey_cntr   = attr.bad_pkey_cntr;
	resp.qkey_viol_cntr  = attr.qkey_viol_cntr;
	resp.pkey_tbl_len    = attr.pkey_tbl_len;
	resp.lid 	     = attr.lid;
	resp.sm_lid 	     = attr.sm_lid;
	resp.lmc 	     = attr.lmc;
	resp.max_vl_num      = attr.max_vl_num;
	resp.sm_sl 	     = attr.sm_sl;
	resp.subnet_timeout  = attr.subnet_timeout;
	resp.init_type_reply = attr.init_type_reply;
	resp.active_width    = attr.active_width;
	resp.active_speed    = attr.active_speed;
	resp.phys_state      = attr.phys_state;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		return -EFAULT;

	return in_len;
}

ssize_t ib_uverbs_alloc_pd(struct ib_uverbs_file *file,
			   const char __user *buf,
			   int in_len, int out_len)
{
	struct ib_uverbs_alloc_pd      cmd;
	struct ib_uverbs_alloc_pd_resp resp;
	struct ib_udata                udata;
	struct ib_uobject             *uobj;
	struct ib_pd                  *pd;
	int                            ret;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	INIT_UDATA(&udata, buf + sizeof cmd,
		   (unsigned long) cmd.response + sizeof resp,
		   in_len - sizeof cmd, out_len - sizeof resp);

	uobj = kmalloc(sizeof *uobj, GFP_KERNEL);
	if (!uobj)
		return -ENOMEM;

	uobj->context = file->ucontext;

	pd = file->device->ib_dev->alloc_pd(file->device->ib_dev,
					    file->ucontext, &udata);
	if (IS_ERR(pd)) {
		ret = PTR_ERR(pd);
		goto err;
	}

	pd->device  = file->device->ib_dev;
	pd->uobject = uobj;
	atomic_set(&pd->usecnt, 0);

	down(&ib_uverbs_idr_mutex);

retry:
	if (!idr_pre_get(&ib_uverbs_pd_idr, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto err_up;
	}

	ret = idr_get_new(&ib_uverbs_pd_idr, pd, &uobj->id);

	if (ret == -EAGAIN)
		goto retry;
	if (ret)
		goto err_up;

	memset(&resp, 0, sizeof resp);
	resp.pd_handle = uobj->id;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		ret = -EFAULT;
		goto err_idr;
	}

	down(&file->mutex);
	list_add_tail(&uobj->list, &file->ucontext->pd_list);
	up(&file->mutex);

	up(&ib_uverbs_idr_mutex);

	return in_len;

err_idr:
	idr_remove(&ib_uverbs_pd_idr, uobj->id);

err_up:
	up(&ib_uverbs_idr_mutex);
	ib_dealloc_pd(pd);

err:
	kfree(uobj);
	return ret;
}

ssize_t ib_uverbs_dealloc_pd(struct ib_uverbs_file *file,
			     const char __user *buf,
			     int in_len, int out_len)
{
	struct ib_uverbs_dealloc_pd cmd;
	struct ib_pd               *pd;
	struct ib_uobject          *uobj;
	int                         ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	down(&ib_uverbs_idr_mutex);

	pd = idr_find(&ib_uverbs_pd_idr, cmd.pd_handle);
	if (!pd || pd->uobject->context != file->ucontext)
		goto out;

	uobj = pd->uobject;

	ret = ib_dealloc_pd(pd);
	if (ret)
		goto out;

	idr_remove(&ib_uverbs_pd_idr, cmd.pd_handle);

	down(&file->mutex);
	list_del(&uobj->list);
	up(&file->mutex);

	kfree(uobj);

out:
	up(&ib_uverbs_idr_mutex);

	return ret ? ret : in_len;
}

ssize_t ib_uverbs_reg_mr(struct ib_uverbs_file *file,
			 const char __user *buf, int in_len,
			 int out_len)
{
	struct ib_uverbs_reg_mr      cmd;
	struct ib_uverbs_reg_mr_resp resp;
	struct ib_udata              udata;
	struct ib_umem_object       *obj;
	struct ib_pd                *pd;
	struct ib_mr                *mr;
	int                          ret;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	INIT_UDATA(&udata, buf + sizeof cmd,
		   (unsigned long) cmd.response + sizeof resp,
		   in_len - sizeof cmd, out_len - sizeof resp);

	if ((cmd.start & ~PAGE_MASK) != (cmd.hca_va & ~PAGE_MASK))
		return -EINVAL;

	/*
	 * Local write permission is required if remote write or
	 * remote atomic permission is also requested.
	 */
	if (cmd.access_flags & (IB_ACCESS_REMOTE_ATOMIC | IB_ACCESS_REMOTE_WRITE) &&
	    !(cmd.access_flags & IB_ACCESS_LOCAL_WRITE))
		return -EINVAL;

	obj = kmalloc(sizeof *obj, GFP_KERNEL);
	if (!obj)
		return -ENOMEM;

	obj->uobject.context = file->ucontext;

	/*
	 * We ask for writable memory if any access flags other than
	 * "remote read" are set.  "Local write" and "remote write"
	 * obviously require write access.  "Remote atomic" can do
	 * things like fetch and add, which will modify memory, and
	 * "MW bind" can change permissions by binding a window.
	 */
	ret = ib_umem_get(file->device->ib_dev, &obj->umem,
			  (void *) (unsigned long) cmd.start, cmd.length,
			  !!(cmd.access_flags & ~IB_ACCESS_REMOTE_READ));
	if (ret)
		goto err_free;

	obj->umem.virt_base = cmd.hca_va;

	down(&ib_uverbs_idr_mutex);

	pd = idr_find(&ib_uverbs_pd_idr, cmd.pd_handle);
	if (!pd || pd->uobject->context != file->ucontext) {
		ret = -EINVAL;
		goto err_up;
	}

	if (!pd->device->reg_user_mr) {
		ret = -ENOSYS;
		goto err_up;
	}

	mr = pd->device->reg_user_mr(pd, &obj->umem, cmd.access_flags, &udata);
	if (IS_ERR(mr)) {
		ret = PTR_ERR(mr);
		goto err_up;
	}

	mr->device  = pd->device;
	mr->pd      = pd;
	mr->uobject = &obj->uobject;
	atomic_inc(&pd->usecnt);
	atomic_set(&mr->usecnt, 0);

	memset(&resp, 0, sizeof resp);
	resp.lkey = mr->lkey;
	resp.rkey = mr->rkey;

retry:
	if (!idr_pre_get(&ib_uverbs_mr_idr, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto err_unreg;
	}

	ret = idr_get_new(&ib_uverbs_mr_idr, mr, &obj->uobject.id);

	if (ret == -EAGAIN)
		goto retry;
	if (ret)
		goto err_unreg;

	resp.mr_handle = obj->uobject.id;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		ret = -EFAULT;
		goto err_idr;
	}

	down(&file->mutex);
	list_add_tail(&obj->uobject.list, &file->ucontext->mr_list);
	up(&file->mutex);

	up(&ib_uverbs_idr_mutex);

	return in_len;

err_idr:
	idr_remove(&ib_uverbs_mr_idr, obj->uobject.id);

err_unreg:
	ib_dereg_mr(mr);

err_up:
	up(&ib_uverbs_idr_mutex);

	ib_umem_release(file->device->ib_dev, &obj->umem);

err_free:
	kfree(obj);
	return ret;
}

ssize_t ib_uverbs_dereg_mr(struct ib_uverbs_file *file,
			   const char __user *buf, int in_len,
			   int out_len)
{
	struct ib_uverbs_dereg_mr cmd;
	struct ib_mr             *mr;
	struct ib_umem_object    *memobj;
	int                       ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	down(&ib_uverbs_idr_mutex);

	mr = idr_find(&ib_uverbs_mr_idr, cmd.mr_handle);
	if (!mr || mr->uobject->context != file->ucontext)
		goto out;

	memobj = container_of(mr->uobject, struct ib_umem_object, uobject);

	ret = ib_dereg_mr(mr);
	if (ret)
		goto out;

	idr_remove(&ib_uverbs_mr_idr, cmd.mr_handle);

	down(&file->mutex);
	list_del(&memobj->uobject.list);
	up(&file->mutex);

	ib_umem_release(file->device->ib_dev, &memobj->umem);
	kfree(memobj);

out:
	up(&ib_uverbs_idr_mutex);

	return ret ? ret : in_len;
}

ssize_t ib_uverbs_create_comp_channel(struct ib_uverbs_file *file,
				      const char __user *buf, int in_len,
				      int out_len)
{
	struct ib_uverbs_create_comp_channel	   cmd;
	struct ib_uverbs_create_comp_channel_resp  resp;
	struct file				  *filp;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	filp = ib_uverbs_alloc_event_file(file, 0, &resp.fd);
	if (IS_ERR(filp))
		return PTR_ERR(filp);

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		put_unused_fd(resp.fd);
		fput(filp);
		return -EFAULT;
	}

	fd_install(resp.fd, filp);
	return in_len;
}

ssize_t ib_uverbs_create_cq(struct ib_uverbs_file *file,
			    const char __user *buf, int in_len,
			    int out_len)
{
	struct ib_uverbs_create_cq      cmd;
	struct ib_uverbs_create_cq_resp resp;
	struct ib_udata                 udata;
	struct ib_ucq_object           *uobj;
	struct ib_uverbs_event_file    *ev_file = NULL;
	struct ib_cq                   *cq;
	int                             ret;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	INIT_UDATA(&udata, buf + sizeof cmd,
		   (unsigned long) cmd.response + sizeof resp,
		   in_len - sizeof cmd, out_len - sizeof resp);

	if (cmd.comp_vector >= file->device->num_comp_vectors)
		return -EINVAL;

	if (cmd.comp_channel >= 0)
		ev_file = ib_uverbs_lookup_comp_file(cmd.comp_channel);

	uobj = kmalloc(sizeof *uobj, GFP_KERNEL);
	if (!uobj)
		return -ENOMEM;

	uobj->uobject.user_handle   = cmd.user_handle;
	uobj->uobject.context       = file->ucontext;
	uobj->uverbs_file	    = file;
	uobj->comp_events_reported  = 0;
	uobj->async_events_reported = 0;
	INIT_LIST_HEAD(&uobj->comp_list);
	INIT_LIST_HEAD(&uobj->async_list);

	cq = file->device->ib_dev->create_cq(file->device->ib_dev, cmd.cqe,
					     file->ucontext, &udata);
	if (IS_ERR(cq)) {
		ret = PTR_ERR(cq);
		goto err;
	}

	cq->device        = file->device->ib_dev;
	cq->uobject       = &uobj->uobject;
	cq->comp_handler  = ib_uverbs_comp_handler;
	cq->event_handler = ib_uverbs_cq_event_handler;
	cq->cq_context    = ev_file;
	atomic_set(&cq->usecnt, 0);

	down(&ib_uverbs_idr_mutex);

retry:
	if (!idr_pre_get(&ib_uverbs_cq_idr, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto err_up;
	}

	ret = idr_get_new(&ib_uverbs_cq_idr, cq, &uobj->uobject.id);

	if (ret == -EAGAIN)
		goto retry;
	if (ret)
		goto err_up;

	memset(&resp, 0, sizeof resp);
	resp.cq_handle = uobj->uobject.id;
	resp.cqe       = cq->cqe;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		ret = -EFAULT;
		goto err_idr;
	}

	down(&file->mutex);
	list_add_tail(&uobj->uobject.list, &file->ucontext->cq_list);
	up(&file->mutex);

	up(&ib_uverbs_idr_mutex);

	return in_len;

err_idr:
	idr_remove(&ib_uverbs_cq_idr, uobj->uobject.id);

err_up:
	up(&ib_uverbs_idr_mutex);
	ib_destroy_cq(cq);

err:
	kfree(uobj);
	return ret;
}

ssize_t ib_uverbs_poll_cq(struct ib_uverbs_file *file,
			  const char __user *buf, int in_len,
			  int out_len)
{
	struct ib_uverbs_poll_cq       cmd;
	struct ib_uverbs_poll_cq_resp *resp;
	struct ib_cq                  *cq;
	struct ib_wc                  *wc;
	int                            ret = 0;
	int                            i;
	int                            rsize;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	wc = kmalloc(cmd.ne * sizeof *wc, GFP_KERNEL);
	if (!wc)
		return -ENOMEM;

	rsize = sizeof *resp + cmd.ne * sizeof(struct ib_uverbs_wc);
	resp = kmalloc(rsize, GFP_KERNEL);
	if (!resp) {
		ret = -ENOMEM;
		goto out_wc;
	}

	down(&ib_uverbs_idr_mutex);
	cq = idr_find(&ib_uverbs_cq_idr, cmd.cq_handle);
	if (!cq || cq->uobject->context != file->ucontext) {
		ret = -EINVAL;
		goto out;
	}

	resp->count = ib_poll_cq(cq, cmd.ne, wc);

	for (i = 0; i < resp->count; i++) {
		resp->wc[i].wr_id 	   = wc[i].wr_id;
		resp->wc[i].status 	   = wc[i].status;
		resp->wc[i].opcode 	   = wc[i].opcode;
		resp->wc[i].vendor_err 	   = wc[i].vendor_err;
		resp->wc[i].byte_len 	   = wc[i].byte_len;
		resp->wc[i].imm_data 	   = (__u32 __force) wc[i].imm_data;
		resp->wc[i].qp_num 	   = wc[i].qp_num;
		resp->wc[i].src_qp 	   = wc[i].src_qp;
		resp->wc[i].wc_flags 	   = wc[i].wc_flags;
		resp->wc[i].pkey_index 	   = wc[i].pkey_index;
		resp->wc[i].slid 	   = wc[i].slid;
		resp->wc[i].sl 		   = wc[i].sl;
		resp->wc[i].dlid_path_bits = wc[i].dlid_path_bits;
		resp->wc[i].port_num 	   = wc[i].port_num;
	}

	if (copy_to_user((void __user *) (unsigned long) cmd.response, resp, rsize))
		ret = -EFAULT;

out:
	up(&ib_uverbs_idr_mutex);
	kfree(resp);

out_wc:
	kfree(wc);
	return ret ? ret : in_len;
}

ssize_t ib_uverbs_req_notify_cq(struct ib_uverbs_file *file,
				const char __user *buf, int in_len,
				int out_len)
{
	struct ib_uverbs_req_notify_cq cmd;
	struct ib_cq                  *cq;
	int                            ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	down(&ib_uverbs_idr_mutex);
	cq = idr_find(&ib_uverbs_cq_idr, cmd.cq_handle);
	if (cq && cq->uobject->context == file->ucontext) {
		ib_req_notify_cq(cq, cmd.solicited_only ?
					IB_CQ_SOLICITED : IB_CQ_NEXT_COMP);
		ret = in_len;
	}
	up(&ib_uverbs_idr_mutex);

	return ret;
}

ssize_t ib_uverbs_destroy_cq(struct ib_uverbs_file *file,
			     const char __user *buf, int in_len,
			     int out_len)
{
	struct ib_uverbs_destroy_cq      cmd;
	struct ib_uverbs_destroy_cq_resp resp;
	struct ib_cq               	*cq;
	struct ib_ucq_object        	*uobj;
	struct ib_uverbs_event_file	*ev_file;
	u64				 user_handle;
	int                        	 ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	memset(&resp, 0, sizeof resp);

	down(&ib_uverbs_idr_mutex);

	cq = idr_find(&ib_uverbs_cq_idr, cmd.cq_handle);
	if (!cq || cq->uobject->context != file->ucontext)
		goto out;

	user_handle = cq->uobject->user_handle;
	uobj        = container_of(cq->uobject, struct ib_ucq_object, uobject);
	ev_file     = cq->cq_context;

	ret = ib_destroy_cq(cq);
	if (ret)
		goto out;

	idr_remove(&ib_uverbs_cq_idr, cmd.cq_handle);

	down(&file->mutex);
	list_del(&uobj->uobject.list);
	up(&file->mutex);

	ib_uverbs_release_ucq(file, ev_file, uobj);

	resp.comp_events_reported  = uobj->comp_events_reported;
	resp.async_events_reported = uobj->async_events_reported;

	kfree(uobj);

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		ret = -EFAULT;

out:
	up(&ib_uverbs_idr_mutex);

	return ret ? ret : in_len;
}

ssize_t ib_uverbs_create_qp(struct ib_uverbs_file *file,
			    const char __user *buf, int in_len,
			    int out_len)
{
	struct ib_uverbs_create_qp      cmd;
	struct ib_uverbs_create_qp_resp resp;
	struct ib_udata                 udata;
	struct ib_uevent_object        *uobj;
	struct ib_pd                   *pd;
	struct ib_cq                   *scq, *rcq;
	struct ib_srq                  *srq;
	struct ib_qp                   *qp;
	struct ib_qp_init_attr          attr;
	int ret;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	INIT_UDATA(&udata, buf + sizeof cmd,
		   (unsigned long) cmd.response + sizeof resp,
		   in_len - sizeof cmd, out_len - sizeof resp);

	uobj = kmalloc(sizeof *uobj, GFP_KERNEL);
	if (!uobj)
		return -ENOMEM;

	down(&ib_uverbs_idr_mutex);

	pd  = idr_find(&ib_uverbs_pd_idr, cmd.pd_handle);
	scq = idr_find(&ib_uverbs_cq_idr, cmd.send_cq_handle);
	rcq = idr_find(&ib_uverbs_cq_idr, cmd.recv_cq_handle);
	srq = cmd.is_srq ? idr_find(&ib_uverbs_srq_idr, cmd.srq_handle) : NULL;

	if (!pd  || pd->uobject->context  != file->ucontext ||
	    !scq || scq->uobject->context != file->ucontext ||
	    !rcq || rcq->uobject->context != file->ucontext ||
	    (cmd.is_srq && (!srq || srq->uobject->context != file->ucontext))) {
		ret = -EINVAL;
		goto err_up;
	}

	attr.event_handler = ib_uverbs_qp_event_handler;
	attr.qp_context    = file;
	attr.send_cq       = scq;
	attr.recv_cq       = rcq;
	attr.srq           = srq;
	attr.sq_sig_type   = cmd.sq_sig_all ? IB_SIGNAL_ALL_WR : IB_SIGNAL_REQ_WR;
	attr.qp_type       = cmd.qp_type;

	attr.cap.max_send_wr     = cmd.max_send_wr;
	attr.cap.max_recv_wr     = cmd.max_recv_wr;
	attr.cap.max_send_sge    = cmd.max_send_sge;
	attr.cap.max_recv_sge    = cmd.max_recv_sge;
	attr.cap.max_inline_data = cmd.max_inline_data;

	uobj->uobject.user_handle = cmd.user_handle;
	uobj->uobject.context     = file->ucontext;
	uobj->events_reported     = 0;
	INIT_LIST_HEAD(&uobj->event_list);

	qp = pd->device->create_qp(pd, &attr, &udata);
	if (IS_ERR(qp)) {
		ret = PTR_ERR(qp);
		goto err_up;
	}

	qp->device     	  = pd->device;
	qp->pd         	  = pd;
	qp->send_cq    	  = attr.send_cq;
	qp->recv_cq    	  = attr.recv_cq;
	qp->srq	       	  = attr.srq;
	qp->uobject       = &uobj->uobject;
	qp->event_handler = attr.event_handler;
	qp->qp_context    = attr.qp_context;
	qp->qp_type	  = attr.qp_type;
	atomic_inc(&pd->usecnt);
	atomic_inc(&attr.send_cq->usecnt);
	atomic_inc(&attr.recv_cq->usecnt);
	if (attr.srq)
		atomic_inc(&attr.srq->usecnt);

	memset(&resp, 0, sizeof resp);
	resp.qpn = qp->qp_num;

retry:
	if (!idr_pre_get(&ib_uverbs_qp_idr, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto err_destroy;
	}

	ret = idr_get_new(&ib_uverbs_qp_idr, qp, &uobj->uobject.id);

	if (ret == -EAGAIN)
		goto retry;
	if (ret)
		goto err_destroy;

	resp.qp_handle       = uobj->uobject.id;
	resp.max_recv_sge    = attr.cap.max_recv_sge;
	resp.max_send_sge    = attr.cap.max_send_sge;
	resp.max_recv_wr     = attr.cap.max_recv_wr;
	resp.max_send_wr     = attr.cap.max_send_wr;
	resp.max_inline_data = attr.cap.max_inline_data;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		ret = -EFAULT;
		goto err_idr;
	}

	down(&file->mutex);
	list_add_tail(&uobj->uobject.list, &file->ucontext->qp_list);
	up(&file->mutex);

	up(&ib_uverbs_idr_mutex);

	return in_len;

err_idr:
	idr_remove(&ib_uverbs_qp_idr, uobj->uobject.id);

err_destroy:
	ib_destroy_qp(qp);

err_up:
	up(&ib_uverbs_idr_mutex);

	kfree(uobj);
	return ret;
}

ssize_t ib_uverbs_modify_qp(struct ib_uverbs_file *file,
			    const char __user *buf, int in_len,
			    int out_len)
{
	struct ib_uverbs_modify_qp cmd;
	struct ib_qp              *qp;
	struct ib_qp_attr         *attr;
	int                        ret;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	attr = kmalloc(sizeof *attr, GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	down(&ib_uverbs_idr_mutex);

	qp = idr_find(&ib_uverbs_qp_idr, cmd.qp_handle);
	if (!qp || qp->uobject->context != file->ucontext) {
		ret = -EINVAL;
		goto out;
	}

	attr->qp_state 		  = cmd.qp_state;
	attr->cur_qp_state 	  = cmd.cur_qp_state;
	attr->path_mtu 		  = cmd.path_mtu;
	attr->path_mig_state 	  = cmd.path_mig_state;
	attr->qkey 		  = cmd.qkey;
	attr->rq_psn 		  = cmd.rq_psn;
	attr->sq_psn 		  = cmd.sq_psn;
	attr->dest_qp_num 	  = cmd.dest_qp_num;
	attr->qp_access_flags 	  = cmd.qp_access_flags;
	attr->pkey_index 	  = cmd.pkey_index;
	attr->alt_pkey_index 	  = cmd.pkey_index;
	attr->en_sqd_async_notify = cmd.en_sqd_async_notify;
	attr->max_rd_atomic 	  = cmd.max_rd_atomic;
	attr->max_dest_rd_atomic  = cmd.max_dest_rd_atomic;
	attr->min_rnr_timer 	  = cmd.min_rnr_timer;
	attr->port_num 		  = cmd.port_num;
	attr->timeout 		  = cmd.timeout;
	attr->retry_cnt 	  = cmd.retry_cnt;
	attr->rnr_retry 	  = cmd.rnr_retry;
	attr->alt_port_num 	  = cmd.alt_port_num;
	attr->alt_timeout 	  = cmd.alt_timeout;

	memcpy(attr->ah_attr.grh.dgid.raw, cmd.dest.dgid, 16);
	attr->ah_attr.grh.flow_label        = cmd.dest.flow_label;
	attr->ah_attr.grh.sgid_index        = cmd.dest.sgid_index;
	attr->ah_attr.grh.hop_limit         = cmd.dest.hop_limit;
	attr->ah_attr.grh.traffic_class     = cmd.dest.traffic_class;
	attr->ah_attr.dlid 	    	    = cmd.dest.dlid;
	attr->ah_attr.sl   	    	    = cmd.dest.sl;
	attr->ah_attr.src_path_bits 	    = cmd.dest.src_path_bits;
	attr->ah_attr.static_rate   	    = cmd.dest.static_rate;
	attr->ah_attr.ah_flags 	    	    = cmd.dest.is_global ? IB_AH_GRH : 0;
	attr->ah_attr.port_num 	    	    = cmd.dest.port_num;

	memcpy(attr->alt_ah_attr.grh.dgid.raw, cmd.alt_dest.dgid, 16);
	attr->alt_ah_attr.grh.flow_label    = cmd.alt_dest.flow_label;
	attr->alt_ah_attr.grh.sgid_index    = cmd.alt_dest.sgid_index;
	attr->alt_ah_attr.grh.hop_limit     = cmd.alt_dest.hop_limit;
	attr->alt_ah_attr.grh.traffic_class = cmd.alt_dest.traffic_class;
	attr->alt_ah_attr.dlid 	    	    = cmd.alt_dest.dlid;
	attr->alt_ah_attr.sl   	    	    = cmd.alt_dest.sl;
	attr->alt_ah_attr.src_path_bits     = cmd.alt_dest.src_path_bits;
	attr->alt_ah_attr.static_rate       = cmd.alt_dest.static_rate;
	attr->alt_ah_attr.ah_flags 	    = cmd.alt_dest.is_global ? IB_AH_GRH : 0;
	attr->alt_ah_attr.port_num 	    = cmd.alt_dest.port_num;

	ret = ib_modify_qp(qp, attr, cmd.attr_mask);
	if (ret)
		goto out;

	ret = in_len;

out:
	up(&ib_uverbs_idr_mutex);
	kfree(attr);

	return ret;
}

ssize_t ib_uverbs_destroy_qp(struct ib_uverbs_file *file,
			     const char __user *buf, int in_len,
			     int out_len)
{
	struct ib_uverbs_destroy_qp      cmd;
	struct ib_uverbs_destroy_qp_resp resp;
	struct ib_qp               	*qp;
	struct ib_uevent_object        	*uobj;
	int                        	 ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	memset(&resp, 0, sizeof resp);

	down(&ib_uverbs_idr_mutex);

	qp = idr_find(&ib_uverbs_qp_idr, cmd.qp_handle);
	if (!qp || qp->uobject->context != file->ucontext)
		goto out;

	uobj = container_of(qp->uobject, struct ib_uevent_object, uobject);

	ret = ib_destroy_qp(qp);
	if (ret)
		goto out;

	idr_remove(&ib_uverbs_qp_idr, cmd.qp_handle);

	down(&file->mutex);
	list_del(&uobj->uobject.list);
	up(&file->mutex);

	ib_uverbs_release_uevent(file, uobj);

	resp.events_reported = uobj->events_reported;

	kfree(uobj);

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		ret = -EFAULT;

out:
	up(&ib_uverbs_idr_mutex);

	return ret ? ret : in_len;
}

ssize_t ib_uverbs_post_send(struct ib_uverbs_file *file,
                            const char __user *buf, int in_len,
                            int out_len)
{
	struct ib_uverbs_post_send      cmd;
	struct ib_uverbs_post_send_resp resp;
	struct ib_uverbs_send_wr       *user_wr;
	struct ib_send_wr              *wr = NULL, *last, *next, *bad_wr;
	struct ib_qp                   *qp;
	int                             i, sg_ind;
	ssize_t                         ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	if (in_len < sizeof cmd + cmd.wqe_size * cmd.wr_count +
	    cmd.sge_count * sizeof (struct ib_uverbs_sge))
		return -EINVAL;

	if (cmd.wqe_size < sizeof (struct ib_uverbs_send_wr))
		return -EINVAL;

	user_wr = kmalloc(cmd.wqe_size, GFP_KERNEL);
	if (!user_wr)
		return -ENOMEM;

	down(&ib_uverbs_idr_mutex);

	qp = idr_find(&ib_uverbs_qp_idr, cmd.qp_handle);
	if (!qp || qp->uobject->context != file->ucontext)
		goto out;

	sg_ind = 0;
	last = NULL;
	for (i = 0; i < cmd.wr_count; ++i) {
		if (copy_from_user(user_wr,
				   buf + sizeof cmd + i * cmd.wqe_size,
				   cmd.wqe_size)) {
			ret = -EFAULT;
			goto out;
		}

		if (user_wr->num_sge + sg_ind > cmd.sge_count) {
			ret = -EINVAL;
			goto out;
		}

		next = kmalloc(ALIGN(sizeof *next, sizeof (struct ib_sge)) +
			       user_wr->num_sge * sizeof (struct ib_sge),
			       GFP_KERNEL);
		if (!next) {
			ret = -ENOMEM;
			goto out;
		}

		if (!last)
			wr = next;
		else
			last->next = next;
		last = next;

		next->next       = NULL;
		next->wr_id      = user_wr->wr_id;
		next->num_sge    = user_wr->num_sge;
		next->opcode     = user_wr->opcode;
		next->send_flags = user_wr->send_flags;
		next->imm_data   = (__be32 __force) user_wr->imm_data;

		if (qp->qp_type == IB_QPT_UD) {
			next->wr.ud.ah = idr_find(&ib_uverbs_ah_idr,
						  user_wr->wr.ud.ah);
			if (!next->wr.ud.ah) {
				ret = -EINVAL;
				goto out;
			}
			next->wr.ud.remote_qpn  = user_wr->wr.ud.remote_qpn;
			next->wr.ud.remote_qkey = user_wr->wr.ud.remote_qkey;
		} else {
			switch (next->opcode) {
			case IB_WR_RDMA_WRITE:
			case IB_WR_RDMA_WRITE_WITH_IMM:
			case IB_WR_RDMA_READ:
				next->wr.rdma.remote_addr =
					user_wr->wr.rdma.remote_addr;
				next->wr.rdma.rkey        =
					user_wr->wr.rdma.rkey;
				break;
			case IB_WR_ATOMIC_CMP_AND_SWP:
			case IB_WR_ATOMIC_FETCH_AND_ADD:
				next->wr.atomic.remote_addr =
					user_wr->wr.atomic.remote_addr;
				next->wr.atomic.compare_add =
					user_wr->wr.atomic.compare_add;
				next->wr.atomic.swap = user_wr->wr.atomic.swap;
				next->wr.atomic.rkey = user_wr->wr.atomic.rkey;
				break;
			default:
				break;
			}
		}

		if (next->num_sge) {
			next->sg_list = (void *) next +
				ALIGN(sizeof *next, sizeof (struct ib_sge));
			if (copy_from_user(next->sg_list,
					   buf + sizeof cmd +
					   cmd.wr_count * cmd.wqe_size +
					   sg_ind * sizeof (struct ib_sge),
					   next->num_sge * sizeof (struct ib_sge))) {
				ret = -EFAULT;
				goto out;
			}
			sg_ind += next->num_sge;
		} else
			next->sg_list = NULL;
	}

	resp.bad_wr = 0;
	ret = qp->device->post_send(qp, wr, &bad_wr);
	if (ret)
		for (next = wr; next; next = next->next) {
			++resp.bad_wr;
			if (next == bad_wr)
				break;
		}

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		ret = -EFAULT;

out:
	up(&ib_uverbs_idr_mutex);

	while (wr) {
		next = wr->next;
		kfree(wr);
		wr = next;
	}

	kfree(user_wr);

	return ret ? ret : in_len;
}

static struct ib_recv_wr *ib_uverbs_unmarshall_recv(const char __user *buf,
						    int in_len,
						    u32 wr_count,
						    u32 sge_count,
						    u32 wqe_size)
{
	struct ib_uverbs_recv_wr *user_wr;
	struct ib_recv_wr        *wr = NULL, *last, *next;
	int                       sg_ind;
	int                       i;
	int                       ret;

	if (in_len < wqe_size * wr_count +
	    sge_count * sizeof (struct ib_uverbs_sge))
		return ERR_PTR(-EINVAL);

	if (wqe_size < sizeof (struct ib_uverbs_recv_wr))
		return ERR_PTR(-EINVAL);

	user_wr = kmalloc(wqe_size, GFP_KERNEL);
	if (!user_wr)
		return ERR_PTR(-ENOMEM);

	sg_ind = 0;
	last = NULL;
	for (i = 0; i < wr_count; ++i) {
		if (copy_from_user(user_wr, buf + i * wqe_size,
				   wqe_size)) {
			ret = -EFAULT;
			goto err;
		}

		if (user_wr->num_sge + sg_ind > sge_count) {
			ret = -EINVAL;
			goto err;
		}

		next = kmalloc(ALIGN(sizeof *next, sizeof (struct ib_sge)) +
			       user_wr->num_sge * sizeof (struct ib_sge),
			       GFP_KERNEL);
		if (!next) {
			ret = -ENOMEM;
			goto err;
		}

		if (!last)
			wr = next;
		else
			last->next = next;
		last = next;

		next->next       = NULL;
		next->wr_id      = user_wr->wr_id;
		next->num_sge    = user_wr->num_sge;

		if (next->num_sge) {
			next->sg_list = (void *) next +
				ALIGN(sizeof *next, sizeof (struct ib_sge));
			if (copy_from_user(next->sg_list,
					   buf + wr_count * wqe_size +
					   sg_ind * sizeof (struct ib_sge),
					   next->num_sge * sizeof (struct ib_sge))) {
				ret = -EFAULT;
				goto err;
			}
			sg_ind += next->num_sge;
		} else
			next->sg_list = NULL;
	}

	kfree(user_wr);
	return wr;

err:
	kfree(user_wr);

	while (wr) {
		next = wr->next;
		kfree(wr);
		wr = next;
	}

	return ERR_PTR(ret);
}

ssize_t ib_uverbs_post_recv(struct ib_uverbs_file *file,
                            const char __user *buf, int in_len,
                            int out_len)
{
	struct ib_uverbs_post_recv      cmd;
	struct ib_uverbs_post_recv_resp resp;
	struct ib_recv_wr              *wr, *next, *bad_wr;
	struct ib_qp                   *qp;
	ssize_t                         ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	wr = ib_uverbs_unmarshall_recv(buf + sizeof cmd,
				       in_len - sizeof cmd, cmd.wr_count,
				       cmd.sge_count, cmd.wqe_size);
	if (IS_ERR(wr))
		return PTR_ERR(wr);

	down(&ib_uverbs_idr_mutex);

	qp = idr_find(&ib_uverbs_qp_idr, cmd.qp_handle);
	if (!qp || qp->uobject->context != file->ucontext)
		goto out;

	resp.bad_wr = 0;
	ret = qp->device->post_recv(qp, wr, &bad_wr);
	if (ret)
		for (next = wr; next; next = next->next) {
			++resp.bad_wr;
			if (next == bad_wr)
				break;
		}


	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		ret = -EFAULT;

out:
	up(&ib_uverbs_idr_mutex);

	while (wr) {
		next = wr->next;
		kfree(wr);
		wr = next;
	}

	return ret ? ret : in_len;
}

ssize_t ib_uverbs_post_srq_recv(struct ib_uverbs_file *file,
                            const char __user *buf, int in_len,
                            int out_len)
{
	struct ib_uverbs_post_srq_recv      cmd;
	struct ib_uverbs_post_srq_recv_resp resp;
	struct ib_recv_wr                  *wr, *next, *bad_wr;
	struct ib_srq                      *srq;
	ssize_t                             ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	wr = ib_uverbs_unmarshall_recv(buf + sizeof cmd,
				       in_len - sizeof cmd, cmd.wr_count,
				       cmd.sge_count, cmd.wqe_size);
	if (IS_ERR(wr))
		return PTR_ERR(wr);

	down(&ib_uverbs_idr_mutex);

	srq = idr_find(&ib_uverbs_srq_idr, cmd.srq_handle);
	if (!srq || srq->uobject->context != file->ucontext)
		goto out;

	resp.bad_wr = 0;
	ret = srq->device->post_srq_recv(srq, wr, &bad_wr);
	if (ret)
		for (next = wr; next; next = next->next) {
			++resp.bad_wr;
			if (next == bad_wr)
				break;
		}


	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		ret = -EFAULT;

out:
	up(&ib_uverbs_idr_mutex);

	while (wr) {
		next = wr->next;
		kfree(wr);
		wr = next;
	}

	return ret ? ret : in_len;
}

ssize_t ib_uverbs_create_ah(struct ib_uverbs_file *file,
			    const char __user *buf, int in_len,
			    int out_len)
{
	struct ib_uverbs_create_ah	 cmd;
	struct ib_uverbs_create_ah_resp	 resp;
	struct ib_uobject		*uobj;
	struct ib_pd			*pd;
	struct ib_ah			*ah;
	struct ib_ah_attr		attr;
	int ret;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	uobj = kmalloc(sizeof *uobj, GFP_KERNEL);
	if (!uobj)
		return -ENOMEM;

	down(&ib_uverbs_idr_mutex);

	pd = idr_find(&ib_uverbs_pd_idr, cmd.pd_handle);
	if (!pd || pd->uobject->context != file->ucontext) {
		ret = -EINVAL;
		goto err_up;
	}

	uobj->user_handle = cmd.user_handle;
	uobj->context     = file->ucontext;

	attr.dlid 	       = cmd.attr.dlid;
	attr.sl 	       = cmd.attr.sl;
	attr.src_path_bits     = cmd.attr.src_path_bits;
	attr.static_rate       = cmd.attr.static_rate;
	attr.port_num 	       = cmd.attr.port_num;
	attr.grh.flow_label    = cmd.attr.grh.flow_label;
	attr.grh.sgid_index    = cmd.attr.grh.sgid_index;
	attr.grh.hop_limit     = cmd.attr.grh.hop_limit;
	attr.grh.traffic_class = cmd.attr.grh.traffic_class;
	memcpy(attr.grh.dgid.raw, cmd.attr.grh.dgid, 16);

	ah = ib_create_ah(pd, &attr);
	if (IS_ERR(ah)) {
		ret = PTR_ERR(ah);
		goto err_up;
	}

	ah->uobject = uobj;

retry:
	if (!idr_pre_get(&ib_uverbs_ah_idr, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto err_destroy;
	}

	ret = idr_get_new(&ib_uverbs_ah_idr, ah, &uobj->id);

	if (ret == -EAGAIN)
		goto retry;
	if (ret)
		goto err_destroy;

	resp.ah_handle = uobj->id;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		ret = -EFAULT;
		goto err_idr;
	}

	down(&file->mutex);
	list_add_tail(&uobj->list, &file->ucontext->ah_list);
	up(&file->mutex);

	up(&ib_uverbs_idr_mutex);

	return in_len;

err_idr:
	idr_remove(&ib_uverbs_ah_idr, uobj->id);

err_destroy:
	ib_destroy_ah(ah);

err_up:
	up(&ib_uverbs_idr_mutex);

	kfree(uobj);
	return ret;
}

ssize_t ib_uverbs_destroy_ah(struct ib_uverbs_file *file,
			     const char __user *buf, int in_len, int out_len)
{
	struct ib_uverbs_destroy_ah cmd;
	struct ib_ah		   *ah;
	struct ib_uobject	   *uobj;
	int			    ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	down(&ib_uverbs_idr_mutex);

	ah = idr_find(&ib_uverbs_ah_idr, cmd.ah_handle);
	if (!ah || ah->uobject->context != file->ucontext)
		goto out;

	uobj = ah->uobject;

	ret = ib_destroy_ah(ah);
	if (ret)
		goto out;

	idr_remove(&ib_uverbs_ah_idr, cmd.ah_handle);

	down(&file->mutex);
	list_del(&uobj->list);
	up(&file->mutex);

	kfree(uobj);

out:
	up(&ib_uverbs_idr_mutex);

	return ret ? ret : in_len;
}

ssize_t ib_uverbs_attach_mcast(struct ib_uverbs_file *file,
			       const char __user *buf, int in_len,
			       int out_len)
{
	struct ib_uverbs_attach_mcast cmd;
	struct ib_qp                 *qp;
	int                           ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	down(&ib_uverbs_idr_mutex);

	qp = idr_find(&ib_uverbs_qp_idr, cmd.qp_handle);
	if (qp && qp->uobject->context == file->ucontext)
		ret = ib_attach_mcast(qp, (union ib_gid *) cmd.gid, cmd.mlid);

	up(&ib_uverbs_idr_mutex);

	return ret ? ret : in_len;
}

ssize_t ib_uverbs_detach_mcast(struct ib_uverbs_file *file,
			       const char __user *buf, int in_len,
			       int out_len)
{
	struct ib_uverbs_detach_mcast cmd;
	struct ib_qp                 *qp;
	int                           ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	down(&ib_uverbs_idr_mutex);

	qp = idr_find(&ib_uverbs_qp_idr, cmd.qp_handle);
	if (qp && qp->uobject->context == file->ucontext)
		ret = ib_detach_mcast(qp, (union ib_gid *) cmd.gid, cmd.mlid);

	up(&ib_uverbs_idr_mutex);

	return ret ? ret : in_len;
}

ssize_t ib_uverbs_create_srq(struct ib_uverbs_file *file,
			     const char __user *buf, int in_len,
			     int out_len)
{
	struct ib_uverbs_create_srq      cmd;
	struct ib_uverbs_create_srq_resp resp;
	struct ib_udata                  udata;
	struct ib_uevent_object         *uobj;
	struct ib_pd                    *pd;
	struct ib_srq                   *srq;
	struct ib_srq_init_attr          attr;
	int ret;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	INIT_UDATA(&udata, buf + sizeof cmd,
		   (unsigned long) cmd.response + sizeof resp,
		   in_len - sizeof cmd, out_len - sizeof resp);

	uobj = kmalloc(sizeof *uobj, GFP_KERNEL);
	if (!uobj)
		return -ENOMEM;

	down(&ib_uverbs_idr_mutex);

	pd  = idr_find(&ib_uverbs_pd_idr, cmd.pd_handle);

	if (!pd || pd->uobject->context != file->ucontext) {
		ret = -EINVAL;
		goto err_up;
	}

	attr.event_handler  = ib_uverbs_srq_event_handler;
	attr.srq_context    = file;
	attr.attr.max_wr    = cmd.max_wr;
	attr.attr.max_sge   = cmd.max_sge;
	attr.attr.srq_limit = cmd.srq_limit;

	uobj->uobject.user_handle = cmd.user_handle;
	uobj->uobject.context     = file->ucontext;
	uobj->events_reported     = 0;
	INIT_LIST_HEAD(&uobj->event_list);

	srq = pd->device->create_srq(pd, &attr, &udata);
	if (IS_ERR(srq)) {
		ret = PTR_ERR(srq);
		goto err_up;
	}

	srq->device    	   = pd->device;
	srq->pd        	   = pd;
	srq->uobject       = &uobj->uobject;
	srq->event_handler = attr.event_handler;
	srq->srq_context   = attr.srq_context;
	atomic_inc(&pd->usecnt);
	atomic_set(&srq->usecnt, 0);

	memset(&resp, 0, sizeof resp);

retry:
	if (!idr_pre_get(&ib_uverbs_srq_idr, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto err_destroy;
	}

	ret = idr_get_new(&ib_uverbs_srq_idr, srq, &uobj->uobject.id);

	if (ret == -EAGAIN)
		goto retry;
	if (ret)
		goto err_destroy;

	resp.srq_handle = uobj->uobject.id;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		ret = -EFAULT;
		goto err_idr;
	}

	down(&file->mutex);
	list_add_tail(&uobj->uobject.list, &file->ucontext->srq_list);
	up(&file->mutex);

	up(&ib_uverbs_idr_mutex);

	return in_len;

err_idr:
	idr_remove(&ib_uverbs_srq_idr, uobj->uobject.id);

err_destroy:
	ib_destroy_srq(srq);

err_up:
	up(&ib_uverbs_idr_mutex);

	kfree(uobj);
	return ret;
}

ssize_t ib_uverbs_modify_srq(struct ib_uverbs_file *file,
			     const char __user *buf, int in_len,
			     int out_len)
{
	struct ib_uverbs_modify_srq cmd;
	struct ib_srq              *srq;
	struct ib_srq_attr          attr;
	int                         ret;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	down(&ib_uverbs_idr_mutex);

	srq = idr_find(&ib_uverbs_srq_idr, cmd.srq_handle);
	if (!srq || srq->uobject->context != file->ucontext) {
		ret = -EINVAL;
		goto out;
	}

	attr.max_wr    = cmd.max_wr;
	attr.srq_limit = cmd.srq_limit;

	ret = ib_modify_srq(srq, &attr, cmd.attr_mask);

out:
	up(&ib_uverbs_idr_mutex);

	return ret ? ret : in_len;
}

ssize_t ib_uverbs_destroy_srq(struct ib_uverbs_file *file,
			      const char __user *buf, int in_len,
			      int out_len)
{
	struct ib_uverbs_destroy_srq      cmd;
	struct ib_uverbs_destroy_srq_resp resp;
	struct ib_srq               	 *srq;
	struct ib_uevent_object        	 *uobj;
	int                         	  ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	down(&ib_uverbs_idr_mutex);

	memset(&resp, 0, sizeof resp);

	srq = idr_find(&ib_uverbs_srq_idr, cmd.srq_handle);
	if (!srq || srq->uobject->context != file->ucontext)
		goto out;

	uobj = container_of(srq->uobject, struct ib_uevent_object, uobject);

	ret = ib_destroy_srq(srq);
	if (ret)
		goto out;

	idr_remove(&ib_uverbs_srq_idr, cmd.srq_handle);

	down(&file->mutex);
	list_del(&uobj->uobject.list);
	up(&file->mutex);

	ib_uverbs_release_uevent(file, uobj);

	resp.events_reported = uobj->events_reported;

	kfree(uobj);

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		ret = -EFAULT;

out:
	up(&ib_uverbs_idr_mutex);

	return ret ? ret : in_len;
}
