/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 PathScale, Inc.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
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
 */

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include <linux/uaccess.h>

#include <rdma/uverbs_types.h>
#include <rdma/uverbs_std_types.h>
#include "rdma_core.h"

#include "uverbs.h"
#include "core_priv.h"

/*
 * Copy a response to userspace. If the provided 'resp' is larger than the
 * user buffer it is silently truncated. If the user provided a larger buffer
 * then the trailing portion is zero filled.
 *
 * These semantics are intended to support future extension of the output
 * structures.
 */
static int uverbs_response(struct uverbs_attr_bundle *attrs, const void *resp,
			   size_t resp_len)
{
	int ret;

	if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_CORE_OUT))
		return uverbs_copy_to_struct_or_zero(
			attrs, UVERBS_ATTR_CORE_OUT, resp, resp_len);

	if (copy_to_user(attrs->ucore.outbuf, resp,
			 min(attrs->ucore.outlen, resp_len)))
		return -EFAULT;

	if (resp_len < attrs->ucore.outlen) {
		/*
		 * Zero fill any extra memory that user
		 * space might have provided.
		 */
		ret = clear_user(attrs->ucore.outbuf + resp_len,
				 attrs->ucore.outlen - resp_len);
		if (ret)
			return -EFAULT;
	}

	return 0;
}

/*
 * Copy a request from userspace. If the provided 'req' is larger than the
 * user buffer then the user buffer is zero extended into the 'req'. If 'req'
 * is smaller than the user buffer then the uncopied bytes in the user buffer
 * must be zero.
 */
static int uverbs_request(struct uverbs_attr_bundle *attrs, void *req,
			  size_t req_len)
{
	if (copy_from_user(req, attrs->ucore.inbuf,
			   min(attrs->ucore.inlen, req_len)))
		return -EFAULT;

	if (attrs->ucore.inlen < req_len) {
		memset(req + attrs->ucore.inlen, 0,
		       req_len - attrs->ucore.inlen);
	} else if (attrs->ucore.inlen > req_len) {
		if (!ib_is_buffer_cleared(attrs->ucore.inbuf + req_len,
					  attrs->ucore.inlen - req_len))
			return -EOPNOTSUPP;
	}
	return 0;
}

/*
 * Generate the value for the 'response_length' protocol used by write_ex.
 * This is the number of bytes the kernel actually wrote. Userspace can use
 * this to detect what structure members in the response the kernel
 * understood.
 */
static u32 uverbs_response_length(struct uverbs_attr_bundle *attrs,
				  size_t resp_len)
{
	return min_t(size_t, attrs->ucore.outlen, resp_len);
}

/*
 * The iterator version of the request interface is for handlers that need to
 * step over a flex array at the end of a command header.
 */
struct uverbs_req_iter {
	const void __user *cur;
	const void __user *end;
};

static int uverbs_request_start(struct uverbs_attr_bundle *attrs,
				struct uverbs_req_iter *iter,
				void *req,
				size_t req_len)
{
	if (attrs->ucore.inlen < req_len)
		return -ENOSPC;

	if (copy_from_user(req, attrs->ucore.inbuf, req_len))
		return -EFAULT;

	iter->cur = attrs->ucore.inbuf + req_len;
	iter->end = attrs->ucore.inbuf + attrs->ucore.inlen;
	return 0;
}

static int uverbs_request_next(struct uverbs_req_iter *iter, void *val,
			       size_t len)
{
	if (iter->cur + len > iter->end)
		return -ENOSPC;

	if (copy_from_user(val, iter->cur, len))
		return -EFAULT;

	iter->cur += len;
	return 0;
}

static const void __user *uverbs_request_next_ptr(struct uverbs_req_iter *iter,
						  size_t len)
{
	const void __user *res = iter->cur;

	if (iter->cur + len > iter->end)
		return (void __force __user *)ERR_PTR(-ENOSPC);
	iter->cur += len;
	return res;
}

static int uverbs_request_finish(struct uverbs_req_iter *iter)
{
	if (!ib_is_buffer_cleared(iter->cur, iter->end - iter->cur))
		return -EOPNOTSUPP;
	return 0;
}

/*
 * When calling a destroy function during an error unwind we need to pass in
 * the udata that is sanitized of all user arguments. Ie from the driver
 * perspective it looks like no udata was passed.
 */
struct ib_udata *uverbs_get_cleared_udata(struct uverbs_attr_bundle *attrs)
{
	attrs->driver_udata = (struct ib_udata){};
	return &attrs->driver_udata;
}

static struct ib_uverbs_completion_event_file *
_ib_uverbs_lookup_comp_file(s32 fd, struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = ufd_get_read(UVERBS_OBJECT_COMP_CHANNEL,
					       fd, attrs);

	if (IS_ERR(uobj))
		return (void *)uobj;

	uverbs_uobject_get(uobj);
	uobj_put_read(uobj);

	return container_of(uobj, struct ib_uverbs_completion_event_file,
			    uobj);
}
#define ib_uverbs_lookup_comp_file(_fd, _ufile)                                \
	_ib_uverbs_lookup_comp_file((_fd)*typecheck(s32, _fd), _ufile)

int ib_alloc_ucontext(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_file *ufile = attrs->ufile;
	struct ib_ucontext *ucontext;
	struct ib_device *ib_dev;

	ib_dev = srcu_dereference(ufile->device->ib_dev,
				  &ufile->device->disassociate_srcu);
	if (!ib_dev)
		return -EIO;

	ucontext = rdma_zalloc_drv_obj(ib_dev, ib_ucontext);
	if (!ucontext)
		return -ENOMEM;

	ucontext->device = ib_dev;
	ucontext->ufile = ufile;
	xa_init_flags(&ucontext->mmap_xa, XA_FLAGS_ALLOC);

	rdma_restrack_new(&ucontext->res, RDMA_RESTRACK_CTX);
	rdma_restrack_set_name(&ucontext->res, NULL);
	attrs->context = ucontext;
	return 0;
}

int ib_init_ucontext(struct uverbs_attr_bundle *attrs)
{
	struct ib_ucontext *ucontext = attrs->context;
	struct ib_uverbs_file *file = attrs->ufile;
	int ret;

	if (!down_read_trylock(&file->hw_destroy_rwsem))
		return -EIO;
	mutex_lock(&file->ucontext_lock);
	if (file->ucontext) {
		ret = -EINVAL;
		goto err;
	}

	ret = ib_rdmacg_try_charge(&ucontext->cg_obj, ucontext->device,
				   RDMACG_RESOURCE_HCA_HANDLE);
	if (ret)
		goto err;

	ret = ucontext->device->ops.alloc_ucontext(ucontext,
						   &attrs->driver_udata);
	if (ret)
		goto err_uncharge;

	rdma_restrack_add(&ucontext->res);

	/*
	 * Make sure that ib_uverbs_get_ucontext() sees the pointer update
	 * only after all writes to setup the ucontext have completed
	 */
	smp_store_release(&file->ucontext, ucontext);

	mutex_unlock(&file->ucontext_lock);
	up_read(&file->hw_destroy_rwsem);
	return 0;

err_uncharge:
	ib_rdmacg_uncharge(&ucontext->cg_obj, ucontext->device,
			   RDMACG_RESOURCE_HCA_HANDLE);
err:
	mutex_unlock(&file->ucontext_lock);
	up_read(&file->hw_destroy_rwsem);
	return ret;
}

static int ib_uverbs_get_context(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_get_context_resp resp;
	struct ib_uverbs_get_context cmd;
	struct ib_device *ib_dev;
	struct ib_uobject *uobj;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	ret = ib_alloc_ucontext(attrs);
	if (ret)
		return ret;

	uobj = uobj_alloc(UVERBS_OBJECT_ASYNC_EVENT, attrs, &ib_dev);
	if (IS_ERR(uobj)) {
		ret = PTR_ERR(uobj);
		goto err_ucontext;
	}

	resp = (struct ib_uverbs_get_context_resp){
		.num_comp_vectors = attrs->ufile->device->num_comp_vectors,
		.async_fd = uobj->id,
	};
	ret = uverbs_response(attrs, &resp, sizeof(resp));
	if (ret)
		goto err_uobj;

	ret = ib_init_ucontext(attrs);
	if (ret)
		goto err_uobj;

	ib_uverbs_init_async_event_file(
		container_of(uobj, struct ib_uverbs_async_event_file, uobj));
	rdma_alloc_commit_uobject(uobj, attrs);
	return 0;

err_uobj:
	rdma_alloc_abort_uobject(uobj, attrs, false);
err_ucontext:
	rdma_restrack_put(&attrs->context->res);
	kfree(attrs->context);
	attrs->context = NULL;
	return ret;
}

static void copy_query_dev_fields(struct ib_ucontext *ucontext,
				  struct ib_uverbs_query_device_resp *resp,
				  struct ib_device_attr *attr)
{
	struct ib_device *ib_dev = ucontext->device;

	resp->fw_ver		= attr->fw_ver;
	resp->node_guid		= ib_dev->node_guid;
	resp->sys_image_guid	= attr->sys_image_guid;
	resp->max_mr_size	= attr->max_mr_size;
	resp->page_size_cap	= attr->page_size_cap;
	resp->vendor_id		= attr->vendor_id;
	resp->vendor_part_id	= attr->vendor_part_id;
	resp->hw_ver		= attr->hw_ver;
	resp->max_qp		= attr->max_qp;
	resp->max_qp_wr		= attr->max_qp_wr;
	resp->device_cap_flags	= lower_32_bits(attr->device_cap_flags);
	resp->max_sge		= min(attr->max_send_sge, attr->max_recv_sge);
	resp->max_sge_rd	= attr->max_sge_rd;
	resp->max_cq		= attr->max_cq;
	resp->max_cqe		= attr->max_cqe;
	resp->max_mr		= attr->max_mr;
	resp->max_pd		= attr->max_pd;
	resp->max_qp_rd_atom	= attr->max_qp_rd_atom;
	resp->max_ee_rd_atom	= attr->max_ee_rd_atom;
	resp->max_res_rd_atom	= attr->max_res_rd_atom;
	resp->max_qp_init_rd_atom	= attr->max_qp_init_rd_atom;
	resp->max_ee_init_rd_atom	= attr->max_ee_init_rd_atom;
	resp->atomic_cap		= attr->atomic_cap;
	resp->max_ee			= attr->max_ee;
	resp->max_rdd			= attr->max_rdd;
	resp->max_mw			= attr->max_mw;
	resp->max_raw_ipv6_qp		= attr->max_raw_ipv6_qp;
	resp->max_raw_ethy_qp		= attr->max_raw_ethy_qp;
	resp->max_mcast_grp		= attr->max_mcast_grp;
	resp->max_mcast_qp_attach	= attr->max_mcast_qp_attach;
	resp->max_total_mcast_qp_attach	= attr->max_total_mcast_qp_attach;
	resp->max_ah			= attr->max_ah;
	resp->max_srq			= attr->max_srq;
	resp->max_srq_wr		= attr->max_srq_wr;
	resp->max_srq_sge		= attr->max_srq_sge;
	resp->max_pkeys			= attr->max_pkeys;
	resp->local_ca_ack_delay	= attr->local_ca_ack_delay;
	resp->phys_port_cnt		= ib_dev->phys_port_cnt;
}

static int ib_uverbs_query_device(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_query_device      cmd;
	struct ib_uverbs_query_device_resp resp;
	struct ib_ucontext *ucontext;
	int ret;

	ucontext = ib_uverbs_get_ucontext(attrs);
	if (IS_ERR(ucontext))
		return PTR_ERR(ucontext);

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	memset(&resp, 0, sizeof resp);
	copy_query_dev_fields(ucontext, &resp, &ucontext->device->attrs);

	return uverbs_response(attrs, &resp, sizeof(resp));
}

static int ib_uverbs_query_port(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_query_port      cmd;
	struct ib_uverbs_query_port_resp resp;
	struct ib_port_attr              attr;
	int                              ret;
	struct ib_ucontext *ucontext;
	struct ib_device *ib_dev;

	ucontext = ib_uverbs_get_ucontext(attrs);
	if (IS_ERR(ucontext))
		return PTR_ERR(ucontext);
	ib_dev = ucontext->device;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	ret = ib_query_port(ib_dev, cmd.port_num, &attr);
	if (ret)
		return ret;

	memset(&resp, 0, sizeof resp);
	copy_port_attr_to_resp(&attr, &resp, ib_dev, cmd.port_num);

	return uverbs_response(attrs, &resp, sizeof(resp));
}

static int ib_uverbs_alloc_pd(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_alloc_pd_resp resp = {};
	struct ib_uverbs_alloc_pd      cmd;
	struct ib_uobject             *uobj;
	struct ib_pd                  *pd;
	int                            ret;
	struct ib_device *ib_dev;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	uobj = uobj_alloc(UVERBS_OBJECT_PD, attrs, &ib_dev);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	pd = rdma_zalloc_drv_obj(ib_dev, ib_pd);
	if (!pd) {
		ret = -ENOMEM;
		goto err;
	}

	pd->device  = ib_dev;
	pd->uobject = uobj;
	atomic_set(&pd->usecnt, 0);

	rdma_restrack_new(&pd->res, RDMA_RESTRACK_PD);
	rdma_restrack_set_name(&pd->res, NULL);

	ret = ib_dev->ops.alloc_pd(pd, &attrs->driver_udata);
	if (ret)
		goto err_alloc;
	rdma_restrack_add(&pd->res);

	uobj->object = pd;
	uobj_finalize_uobj_create(uobj, attrs);

	resp.pd_handle = uobj->id;
	return uverbs_response(attrs, &resp, sizeof(resp));

err_alloc:
	rdma_restrack_put(&pd->res);
	kfree(pd);
err:
	uobj_alloc_abort(uobj, attrs);
	return ret;
}

static int ib_uverbs_dealloc_pd(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_dealloc_pd cmd;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	return uobj_perform_destroy(UVERBS_OBJECT_PD, cmd.pd_handle, attrs);
}

struct xrcd_table_entry {
	struct rb_node  node;
	struct ib_xrcd *xrcd;
	struct inode   *inode;
};

static int xrcd_table_insert(struct ib_uverbs_device *dev,
			    struct inode *inode,
			    struct ib_xrcd *xrcd)
{
	struct xrcd_table_entry *entry, *scan;
	struct rb_node **p = &dev->xrcd_tree.rb_node;
	struct rb_node *parent = NULL;

	entry = kmalloc(sizeof *entry, GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->xrcd  = xrcd;
	entry->inode = inode;

	while (*p) {
		parent = *p;
		scan = rb_entry(parent, struct xrcd_table_entry, node);

		if (inode < scan->inode) {
			p = &(*p)->rb_left;
		} else if (inode > scan->inode) {
			p = &(*p)->rb_right;
		} else {
			kfree(entry);
			return -EEXIST;
		}
	}

	rb_link_node(&entry->node, parent, p);
	rb_insert_color(&entry->node, &dev->xrcd_tree);
	igrab(inode);
	return 0;
}

static struct xrcd_table_entry *xrcd_table_search(struct ib_uverbs_device *dev,
						  struct inode *inode)
{
	struct xrcd_table_entry *entry;
	struct rb_node *p = dev->xrcd_tree.rb_node;

	while (p) {
		entry = rb_entry(p, struct xrcd_table_entry, node);

		if (inode < entry->inode)
			p = p->rb_left;
		else if (inode > entry->inode)
			p = p->rb_right;
		else
			return entry;
	}

	return NULL;
}

static struct ib_xrcd *find_xrcd(struct ib_uverbs_device *dev, struct inode *inode)
{
	struct xrcd_table_entry *entry;

	entry = xrcd_table_search(dev, inode);
	if (!entry)
		return NULL;

	return entry->xrcd;
}

static void xrcd_table_delete(struct ib_uverbs_device *dev,
			      struct inode *inode)
{
	struct xrcd_table_entry *entry;

	entry = xrcd_table_search(dev, inode);
	if (entry) {
		iput(inode);
		rb_erase(&entry->node, &dev->xrcd_tree);
		kfree(entry);
	}
}

static int ib_uverbs_open_xrcd(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_device *ibudev = attrs->ufile->device;
	struct ib_uverbs_open_xrcd_resp	resp = {};
	struct ib_uverbs_open_xrcd	cmd;
	struct ib_uxrcd_object         *obj;
	struct ib_xrcd                 *xrcd = NULL;
	struct inode                   *inode = NULL;
	int				new_xrcd = 0;
	struct ib_device *ib_dev;
	struct fd f = {};
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	mutex_lock(&ibudev->xrcd_tree_mutex);

	if (cmd.fd != -1) {
		/* search for file descriptor */
		f = fdget(cmd.fd);
		if (!f.file) {
			ret = -EBADF;
			goto err_tree_mutex_unlock;
		}

		inode = file_inode(f.file);
		xrcd = find_xrcd(ibudev, inode);
		if (!xrcd && !(cmd.oflags & O_CREAT)) {
			/* no file descriptor. Need CREATE flag */
			ret = -EAGAIN;
			goto err_tree_mutex_unlock;
		}

		if (xrcd && cmd.oflags & O_EXCL) {
			ret = -EINVAL;
			goto err_tree_mutex_unlock;
		}
	}

	obj = (struct ib_uxrcd_object *)uobj_alloc(UVERBS_OBJECT_XRCD, attrs,
						   &ib_dev);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto err_tree_mutex_unlock;
	}

	if (!xrcd) {
		xrcd = ib_alloc_xrcd_user(ib_dev, inode, &attrs->driver_udata);
		if (IS_ERR(xrcd)) {
			ret = PTR_ERR(xrcd);
			goto err;
		}
		new_xrcd = 1;
	}

	atomic_set(&obj->refcnt, 0);
	obj->uobject.object = xrcd;

	if (inode) {
		if (new_xrcd) {
			/* create new inode/xrcd table entry */
			ret = xrcd_table_insert(ibudev, inode, xrcd);
			if (ret)
				goto err_dealloc_xrcd;
		}
		atomic_inc(&xrcd->usecnt);
	}

	if (f.file)
		fdput(f);

	mutex_unlock(&ibudev->xrcd_tree_mutex);
	uobj_finalize_uobj_create(&obj->uobject, attrs);

	resp.xrcd_handle = obj->uobject.id;
	return uverbs_response(attrs, &resp, sizeof(resp));

err_dealloc_xrcd:
	ib_dealloc_xrcd_user(xrcd, uverbs_get_cleared_udata(attrs));

err:
	uobj_alloc_abort(&obj->uobject, attrs);

err_tree_mutex_unlock:
	if (f.file)
		fdput(f);

	mutex_unlock(&ibudev->xrcd_tree_mutex);

	return ret;
}

static int ib_uverbs_close_xrcd(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_close_xrcd cmd;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	return uobj_perform_destroy(UVERBS_OBJECT_XRCD, cmd.xrcd_handle, attrs);
}

int ib_uverbs_dealloc_xrcd(struct ib_uobject *uobject, struct ib_xrcd *xrcd,
			   enum rdma_remove_reason why,
			   struct uverbs_attr_bundle *attrs)
{
	struct inode *inode;
	int ret;
	struct ib_uverbs_device *dev = attrs->ufile->device;

	inode = xrcd->inode;
	if (inode && !atomic_dec_and_test(&xrcd->usecnt))
		return 0;

	ret = ib_dealloc_xrcd_user(xrcd, &attrs->driver_udata);

	if (ib_is_destroy_retryable(ret, why, uobject)) {
		atomic_inc(&xrcd->usecnt);
		return ret;
	}

	if (inode)
		xrcd_table_delete(dev, inode);

	return ret;
}

static int ib_uverbs_reg_mr(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_reg_mr_resp resp = {};
	struct ib_uverbs_reg_mr      cmd;
	struct ib_uobject           *uobj;
	struct ib_pd                *pd;
	struct ib_mr                *mr;
	int                          ret;
	struct ib_device *ib_dev;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	if ((cmd.start & ~PAGE_MASK) != (cmd.hca_va & ~PAGE_MASK))
		return -EINVAL;

	ret = ib_check_mr_access(cmd.access_flags);
	if (ret)
		return ret;

	uobj = uobj_alloc(UVERBS_OBJECT_MR, attrs, &ib_dev);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	pd = uobj_get_obj_read(pd, UVERBS_OBJECT_PD, cmd.pd_handle, attrs);
	if (!pd) {
		ret = -EINVAL;
		goto err_free;
	}

	if (cmd.access_flags & IB_ACCESS_ON_DEMAND) {
		if (!(pd->device->attrs.device_cap_flags &
		      IB_DEVICE_ON_DEMAND_PAGING)) {
			pr_debug("ODP support not available\n");
			ret = -EINVAL;
			goto err_put;
		}
	}

	mr = pd->device->ops.reg_user_mr(pd, cmd.start, cmd.length, cmd.hca_va,
					 cmd.access_flags,
					 &attrs->driver_udata);
	if (IS_ERR(mr)) {
		ret = PTR_ERR(mr);
		goto err_put;
	}

	mr->device  = pd->device;
	mr->pd      = pd;
	mr->type    = IB_MR_TYPE_USER;
	mr->dm	    = NULL;
	mr->sig_attrs = NULL;
	mr->uobject = uobj;
	atomic_inc(&pd->usecnt);
	mr->iova = cmd.hca_va;
	mr->length = cmd.length;

	rdma_restrack_new(&mr->res, RDMA_RESTRACK_MR);
	rdma_restrack_set_name(&mr->res, NULL);
	rdma_restrack_add(&mr->res);

	uobj->object = mr;
	uobj_put_obj_read(pd);
	uobj_finalize_uobj_create(uobj, attrs);

	resp.lkey = mr->lkey;
	resp.rkey = mr->rkey;
	resp.mr_handle = uobj->id;
	return uverbs_response(attrs, &resp, sizeof(resp));

err_put:
	uobj_put_obj_read(pd);
err_free:
	uobj_alloc_abort(uobj, attrs);
	return ret;
}

static int ib_uverbs_rereg_mr(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_rereg_mr      cmd;
	struct ib_uverbs_rereg_mr_resp resp;
	struct ib_pd                *pd = NULL;
	struct ib_mr                *mr;
	struct ib_pd		    *old_pd;
	int                          ret;
	struct ib_uobject	    *uobj;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	if (cmd.flags & ~IB_MR_REREG_SUPPORTED || !cmd.flags)
		return -EINVAL;

	if ((cmd.flags & IB_MR_REREG_TRANS) &&
	    (!cmd.start || !cmd.hca_va || 0 >= cmd.length ||
	     (cmd.start & ~PAGE_MASK) != (cmd.hca_va & ~PAGE_MASK)))
			return -EINVAL;

	uobj = uobj_get_write(UVERBS_OBJECT_MR, cmd.mr_handle, attrs);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	mr = uobj->object;

	if (mr->dm) {
		ret = -EINVAL;
		goto put_uobjs;
	}

	if (cmd.flags & IB_MR_REREG_ACCESS) {
		ret = ib_check_mr_access(cmd.access_flags);
		if (ret)
			goto put_uobjs;
	}

	if (cmd.flags & IB_MR_REREG_PD) {
		pd = uobj_get_obj_read(pd, UVERBS_OBJECT_PD, cmd.pd_handle,
				       attrs);
		if (!pd) {
			ret = -EINVAL;
			goto put_uobjs;
		}
	}

	old_pd = mr->pd;
	ret = mr->device->ops.rereg_user_mr(mr, cmd.flags, cmd.start,
					    cmd.length, cmd.hca_va,
					    cmd.access_flags, pd,
					    &attrs->driver_udata);
	if (ret)
		goto put_uobj_pd;

	if (cmd.flags & IB_MR_REREG_PD) {
		atomic_inc(&pd->usecnt);
		mr->pd = pd;
		atomic_dec(&old_pd->usecnt);
	}

	if (cmd.flags & IB_MR_REREG_TRANS) {
		mr->iova = cmd.hca_va;
		mr->length = cmd.length;
	}

	memset(&resp, 0, sizeof(resp));
	resp.lkey      = mr->lkey;
	resp.rkey      = mr->rkey;

	ret = uverbs_response(attrs, &resp, sizeof(resp));

put_uobj_pd:
	if (cmd.flags & IB_MR_REREG_PD)
		uobj_put_obj_read(pd);

put_uobjs:
	uobj_put_write(uobj);

	return ret;
}

static int ib_uverbs_dereg_mr(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_dereg_mr cmd;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	return uobj_perform_destroy(UVERBS_OBJECT_MR, cmd.mr_handle, attrs);
}

static int ib_uverbs_alloc_mw(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_alloc_mw      cmd;
	struct ib_uverbs_alloc_mw_resp resp = {};
	struct ib_uobject             *uobj;
	struct ib_pd                  *pd;
	struct ib_mw                  *mw;
	int                            ret;
	struct ib_device *ib_dev;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	uobj = uobj_alloc(UVERBS_OBJECT_MW, attrs, &ib_dev);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	pd = uobj_get_obj_read(pd, UVERBS_OBJECT_PD, cmd.pd_handle, attrs);
	if (!pd) {
		ret = -EINVAL;
		goto err_free;
	}

	if (cmd.mw_type != IB_MW_TYPE_1 && cmd.mw_type != IB_MW_TYPE_2) {
		ret = -EINVAL;
		goto err_put;
	}

	mw = rdma_zalloc_drv_obj(ib_dev, ib_mw);
	if (!mw) {
		ret = -ENOMEM;
		goto err_put;
	}

	mw->device = ib_dev;
	mw->pd = pd;
	mw->uobject = uobj;
	mw->type = cmd.mw_type;

	ret = pd->device->ops.alloc_mw(mw, &attrs->driver_udata);
	if (ret)
		goto err_alloc;

	atomic_inc(&pd->usecnt);

	uobj->object = mw;
	uobj_put_obj_read(pd);
	uobj_finalize_uobj_create(uobj, attrs);

	resp.rkey = mw->rkey;
	resp.mw_handle = uobj->id;
	return uverbs_response(attrs, &resp, sizeof(resp));

err_alloc:
	kfree(mw);
err_put:
	uobj_put_obj_read(pd);
err_free:
	uobj_alloc_abort(uobj, attrs);
	return ret;
}

static int ib_uverbs_dealloc_mw(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_dealloc_mw cmd;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	return uobj_perform_destroy(UVERBS_OBJECT_MW, cmd.mw_handle, attrs);
}

static int ib_uverbs_create_comp_channel(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_create_comp_channel	   cmd;
	struct ib_uverbs_create_comp_channel_resp  resp;
	struct ib_uobject			  *uobj;
	struct ib_uverbs_completion_event_file	  *ev_file;
	struct ib_device *ib_dev;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	uobj = uobj_alloc(UVERBS_OBJECT_COMP_CHANNEL, attrs, &ib_dev);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	ev_file = container_of(uobj, struct ib_uverbs_completion_event_file,
			       uobj);
	ib_uverbs_init_event_queue(&ev_file->ev_queue);
	uobj_finalize_uobj_create(uobj, attrs);

	resp.fd = uobj->id;
	return uverbs_response(attrs, &resp, sizeof(resp));
}

static int create_cq(struct uverbs_attr_bundle *attrs,
		     struct ib_uverbs_ex_create_cq *cmd)
{
	struct ib_ucq_object           *obj;
	struct ib_uverbs_completion_event_file    *ev_file = NULL;
	struct ib_cq                   *cq;
	int                             ret;
	struct ib_uverbs_ex_create_cq_resp resp = {};
	struct ib_cq_init_attr attr = {};
	struct ib_device *ib_dev;

	if (cmd->comp_vector >= attrs->ufile->device->num_comp_vectors)
		return -EINVAL;

	obj = (struct ib_ucq_object *)uobj_alloc(UVERBS_OBJECT_CQ, attrs,
						 &ib_dev);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	if (cmd->comp_channel >= 0) {
		ev_file = ib_uverbs_lookup_comp_file(cmd->comp_channel, attrs);
		if (IS_ERR(ev_file)) {
			ret = PTR_ERR(ev_file);
			goto err;
		}
	}

	obj->uevent.uobject.user_handle = cmd->user_handle;
	INIT_LIST_HEAD(&obj->comp_list);
	INIT_LIST_HEAD(&obj->uevent.event_list);

	attr.cqe = cmd->cqe;
	attr.comp_vector = cmd->comp_vector;
	attr.flags = cmd->flags;

	cq = rdma_zalloc_drv_obj(ib_dev, ib_cq);
	if (!cq) {
		ret = -ENOMEM;
		goto err_file;
	}
	cq->device        = ib_dev;
	cq->uobject       = obj;
	cq->comp_handler  = ib_uverbs_comp_handler;
	cq->event_handler = ib_uverbs_cq_event_handler;
	cq->cq_context    = ev_file ? &ev_file->ev_queue : NULL;
	atomic_set(&cq->usecnt, 0);

	rdma_restrack_new(&cq->res, RDMA_RESTRACK_CQ);
	rdma_restrack_set_name(&cq->res, NULL);

	ret = ib_dev->ops.create_cq(cq, &attr, &attrs->driver_udata);
	if (ret)
		goto err_free;
	rdma_restrack_add(&cq->res);

	obj->uevent.uobject.object = cq;
	obj->uevent.event_file = READ_ONCE(attrs->ufile->default_async_file);
	if (obj->uevent.event_file)
		uverbs_uobject_get(&obj->uevent.event_file->uobj);
	uobj_finalize_uobj_create(&obj->uevent.uobject, attrs);

	resp.base.cq_handle = obj->uevent.uobject.id;
	resp.base.cqe = cq->cqe;
	resp.response_length = uverbs_response_length(attrs, sizeof(resp));
	return uverbs_response(attrs, &resp, sizeof(resp));

err_free:
	rdma_restrack_put(&cq->res);
	kfree(cq);
err_file:
	if (ev_file)
		ib_uverbs_release_ucq(ev_file, obj);
err:
	uobj_alloc_abort(&obj->uevent.uobject, attrs);
	return ret;
}

static int ib_uverbs_create_cq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_create_cq      cmd;
	struct ib_uverbs_ex_create_cq	cmd_ex;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	memset(&cmd_ex, 0, sizeof(cmd_ex));
	cmd_ex.user_handle = cmd.user_handle;
	cmd_ex.cqe = cmd.cqe;
	cmd_ex.comp_vector = cmd.comp_vector;
	cmd_ex.comp_channel = cmd.comp_channel;

	return create_cq(attrs, &cmd_ex);
}

static int ib_uverbs_ex_create_cq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_ex_create_cq  cmd;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	if (cmd.comp_mask)
		return -EINVAL;

	if (cmd.reserved)
		return -EINVAL;

	return create_cq(attrs, &cmd);
}

static int ib_uverbs_resize_cq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_resize_cq	cmd;
	struct ib_uverbs_resize_cq_resp	resp = {};
	struct ib_cq			*cq;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	cq = uobj_get_obj_read(cq, UVERBS_OBJECT_CQ, cmd.cq_handle, attrs);
	if (!cq)
		return -EINVAL;

	ret = cq->device->ops.resize_cq(cq, cmd.cqe, &attrs->driver_udata);
	if (ret)
		goto out;

	resp.cqe = cq->cqe;

	ret = uverbs_response(attrs, &resp, sizeof(resp));
out:
	rdma_lookup_put_uobject(&cq->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);

	return ret;
}

static int copy_wc_to_user(struct ib_device *ib_dev, void __user *dest,
			   struct ib_wc *wc)
{
	struct ib_uverbs_wc tmp;

	tmp.wr_id		= wc->wr_id;
	tmp.status		= wc->status;
	tmp.opcode		= wc->opcode;
	tmp.vendor_err		= wc->vendor_err;
	tmp.byte_len		= wc->byte_len;
	tmp.ex.imm_data		= wc->ex.imm_data;
	tmp.qp_num		= wc->qp->qp_num;
	tmp.src_qp		= wc->src_qp;
	tmp.wc_flags		= wc->wc_flags;
	tmp.pkey_index		= wc->pkey_index;
	if (rdma_cap_opa_ah(ib_dev, wc->port_num))
		tmp.slid	= OPA_TO_IB_UCAST_LID(wc->slid);
	else
		tmp.slid	= ib_lid_cpu16(wc->slid);
	tmp.sl			= wc->sl;
	tmp.dlid_path_bits	= wc->dlid_path_bits;
	tmp.port_num		= wc->port_num;
	tmp.reserved		= 0;

	if (copy_to_user(dest, &tmp, sizeof tmp))
		return -EFAULT;

	return 0;
}

static int ib_uverbs_poll_cq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_poll_cq       cmd;
	struct ib_uverbs_poll_cq_resp  resp;
	u8 __user                     *header_ptr;
	u8 __user                     *data_ptr;
	struct ib_cq                  *cq;
	struct ib_wc                   wc;
	int                            ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	cq = uobj_get_obj_read(cq, UVERBS_OBJECT_CQ, cmd.cq_handle, attrs);
	if (!cq)
		return -EINVAL;

	/* we copy a struct ib_uverbs_poll_cq_resp to user space */
	header_ptr = attrs->ucore.outbuf;
	data_ptr = header_ptr + sizeof resp;

	memset(&resp, 0, sizeof resp);
	while (resp.count < cmd.ne) {
		ret = ib_poll_cq(cq, 1, &wc);
		if (ret < 0)
			goto out_put;
		if (!ret)
			break;

		ret = copy_wc_to_user(cq->device, data_ptr, &wc);
		if (ret)
			goto out_put;

		data_ptr += sizeof(struct ib_uverbs_wc);
		++resp.count;
	}

	if (copy_to_user(header_ptr, &resp, sizeof resp)) {
		ret = -EFAULT;
		goto out_put;
	}
	ret = 0;

	if (uverbs_attr_is_valid(attrs, UVERBS_ATTR_CORE_OUT))
		ret = uverbs_output_written(attrs, UVERBS_ATTR_CORE_OUT);

out_put:
	rdma_lookup_put_uobject(&cq->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);
	return ret;
}

static int ib_uverbs_req_notify_cq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_req_notify_cq cmd;
	struct ib_cq                  *cq;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	cq = uobj_get_obj_read(cq, UVERBS_OBJECT_CQ, cmd.cq_handle, attrs);
	if (!cq)
		return -EINVAL;

	ib_req_notify_cq(cq, cmd.solicited_only ?
			 IB_CQ_SOLICITED : IB_CQ_NEXT_COMP);

	rdma_lookup_put_uobject(&cq->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);
	return 0;
}

static int ib_uverbs_destroy_cq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_destroy_cq      cmd;
	struct ib_uverbs_destroy_cq_resp resp;
	struct ib_uobject		*uobj;
	struct ib_ucq_object        	*obj;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	uobj = uobj_get_destroy(UVERBS_OBJECT_CQ, cmd.cq_handle, attrs);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	obj = container_of(uobj, struct ib_ucq_object, uevent.uobject);
	memset(&resp, 0, sizeof(resp));
	resp.comp_events_reported  = obj->comp_events_reported;
	resp.async_events_reported = obj->uevent.events_reported;

	uobj_put_destroy(uobj);

	return uverbs_response(attrs, &resp, sizeof(resp));
}

static int create_qp(struct uverbs_attr_bundle *attrs,
		     struct ib_uverbs_ex_create_qp *cmd)
{
	struct ib_uqp_object		*obj;
	struct ib_device		*device;
	struct ib_pd			*pd = NULL;
	struct ib_xrcd			*xrcd = NULL;
	struct ib_uobject		*xrcd_uobj = ERR_PTR(-ENOENT);
	struct ib_cq			*scq = NULL, *rcq = NULL;
	struct ib_srq			*srq = NULL;
	struct ib_qp			*qp;
	struct ib_qp_init_attr		attr = {};
	struct ib_uverbs_ex_create_qp_resp resp = {};
	int				ret;
	struct ib_rwq_ind_table *ind_tbl = NULL;
	bool has_sq = true;
	struct ib_device *ib_dev;

	switch (cmd->qp_type) {
	case IB_QPT_RAW_PACKET:
		if (!capable(CAP_NET_RAW))
			return -EPERM;
		break;
	case IB_QPT_RC:
	case IB_QPT_UC:
	case IB_QPT_UD:
	case IB_QPT_XRC_INI:
	case IB_QPT_XRC_TGT:
	case IB_QPT_DRIVER:
		break;
	default:
		return -EINVAL;
	}

	obj = (struct ib_uqp_object *)uobj_alloc(UVERBS_OBJECT_QP, attrs,
						 &ib_dev);
	if (IS_ERR(obj))
		return PTR_ERR(obj);
	obj->uxrcd = NULL;
	obj->uevent.uobject.user_handle = cmd->user_handle;
	mutex_init(&obj->mcast_lock);

	if (cmd->comp_mask & IB_UVERBS_CREATE_QP_MASK_IND_TABLE) {
		ind_tbl = uobj_get_obj_read(rwq_ind_table,
					    UVERBS_OBJECT_RWQ_IND_TBL,
					    cmd->rwq_ind_tbl_handle, attrs);
		if (!ind_tbl) {
			ret = -EINVAL;
			goto err_put;
		}

		attr.rwq_ind_tbl = ind_tbl;
	}

	if (ind_tbl && (cmd->max_recv_wr || cmd->max_recv_sge || cmd->is_srq)) {
		ret = -EINVAL;
		goto err_put;
	}

	if (ind_tbl && !cmd->max_send_wr)
		has_sq = false;

	if (cmd->qp_type == IB_QPT_XRC_TGT) {
		xrcd_uobj = uobj_get_read(UVERBS_OBJECT_XRCD, cmd->pd_handle,
					  attrs);

		if (IS_ERR(xrcd_uobj)) {
			ret = -EINVAL;
			goto err_put;
		}

		xrcd = (struct ib_xrcd *)xrcd_uobj->object;
		if (!xrcd) {
			ret = -EINVAL;
			goto err_put;
		}
		device = xrcd->device;
	} else {
		if (cmd->qp_type == IB_QPT_XRC_INI) {
			cmd->max_recv_wr = 0;
			cmd->max_recv_sge = 0;
		} else {
			if (cmd->is_srq) {
				srq = uobj_get_obj_read(srq, UVERBS_OBJECT_SRQ,
							cmd->srq_handle, attrs);
				if (!srq || srq->srq_type == IB_SRQT_XRC) {
					ret = -EINVAL;
					goto err_put;
				}
			}

			if (!ind_tbl) {
				if (cmd->recv_cq_handle != cmd->send_cq_handle) {
					rcq = uobj_get_obj_read(
						cq, UVERBS_OBJECT_CQ,
						cmd->recv_cq_handle, attrs);
					if (!rcq) {
						ret = -EINVAL;
						goto err_put;
					}
				}
			}
		}

		if (has_sq)
			scq = uobj_get_obj_read(cq, UVERBS_OBJECT_CQ,
						cmd->send_cq_handle, attrs);
		if (!ind_tbl)
			rcq = rcq ?: scq;
		pd = uobj_get_obj_read(pd, UVERBS_OBJECT_PD, cmd->pd_handle,
				       attrs);
		if (!pd || (!scq && has_sq)) {
			ret = -EINVAL;
			goto err_put;
		}

		device = pd->device;
	}

	attr.event_handler = ib_uverbs_qp_event_handler;
	attr.send_cq       = scq;
	attr.recv_cq       = rcq;
	attr.srq           = srq;
	attr.xrcd	   = xrcd;
	attr.sq_sig_type   = cmd->sq_sig_all ? IB_SIGNAL_ALL_WR :
					      IB_SIGNAL_REQ_WR;
	attr.qp_type       = cmd->qp_type;
	attr.create_flags  = 0;

	attr.cap.max_send_wr     = cmd->max_send_wr;
	attr.cap.max_recv_wr     = cmd->max_recv_wr;
	attr.cap.max_send_sge    = cmd->max_send_sge;
	attr.cap.max_recv_sge    = cmd->max_recv_sge;
	attr.cap.max_inline_data = cmd->max_inline_data;

	INIT_LIST_HEAD(&obj->uevent.event_list);
	INIT_LIST_HEAD(&obj->mcast_list);

	attr.create_flags = cmd->create_flags;
	if (attr.create_flags & ~(IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK |
				IB_QP_CREATE_CROSS_CHANNEL |
				IB_QP_CREATE_MANAGED_SEND |
				IB_QP_CREATE_MANAGED_RECV |
				IB_QP_CREATE_SCATTER_FCS |
				IB_QP_CREATE_CVLAN_STRIPPING |
				IB_QP_CREATE_SOURCE_QPN |
				IB_QP_CREATE_PCI_WRITE_END_PADDING)) {
		ret = -EINVAL;
		goto err_put;
	}

	if (attr.create_flags & IB_QP_CREATE_SOURCE_QPN) {
		if (!capable(CAP_NET_RAW)) {
			ret = -EPERM;
			goto err_put;
		}

		attr.source_qpn = cmd->source_qpn;
	}

	if (cmd->qp_type == IB_QPT_XRC_TGT)
		qp = ib_create_qp(pd, &attr);
	else
		qp = _ib_create_qp(device, pd, &attr, &attrs->driver_udata,
				   obj);

	if (IS_ERR(qp)) {
		ret = PTR_ERR(qp);
		goto err_put;
	}

	if (cmd->qp_type != IB_QPT_XRC_TGT) {
		ret = ib_create_qp_security(qp, device);
		if (ret)
			goto err_cb;

		atomic_inc(&pd->usecnt);
		if (attr.send_cq)
			atomic_inc(&attr.send_cq->usecnt);
		if (attr.recv_cq)
			atomic_inc(&attr.recv_cq->usecnt);
		if (attr.srq)
			atomic_inc(&attr.srq->usecnt);
		if (ind_tbl)
			atomic_inc(&ind_tbl->usecnt);
	} else {
		/* It is done in _ib_create_qp for other QP types */
		qp->uobject = obj;
	}

	obj->uevent.uobject.object = qp;
	obj->uevent.event_file = READ_ONCE(attrs->ufile->default_async_file);
	if (obj->uevent.event_file)
		uverbs_uobject_get(&obj->uevent.event_file->uobj);

	if (xrcd) {
		obj->uxrcd = container_of(xrcd_uobj, struct ib_uxrcd_object,
					  uobject);
		atomic_inc(&obj->uxrcd->refcnt);
		uobj_put_read(xrcd_uobj);
	}

	if (pd)
		uobj_put_obj_read(pd);
	if (scq)
		rdma_lookup_put_uobject(&scq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	if (rcq && rcq != scq)
		rdma_lookup_put_uobject(&rcq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	if (srq)
		rdma_lookup_put_uobject(&srq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	if (ind_tbl)
		uobj_put_obj_read(ind_tbl);
	uobj_finalize_uobj_create(&obj->uevent.uobject, attrs);

	resp.base.qpn             = qp->qp_num;
	resp.base.qp_handle       = obj->uevent.uobject.id;
	resp.base.max_recv_sge    = attr.cap.max_recv_sge;
	resp.base.max_send_sge    = attr.cap.max_send_sge;
	resp.base.max_recv_wr     = attr.cap.max_recv_wr;
	resp.base.max_send_wr     = attr.cap.max_send_wr;
	resp.base.max_inline_data = attr.cap.max_inline_data;
	resp.response_length = uverbs_response_length(attrs, sizeof(resp));
	return uverbs_response(attrs, &resp, sizeof(resp));

err_cb:
	ib_destroy_qp_user(qp, uverbs_get_cleared_udata(attrs));

err_put:
	if (!IS_ERR(xrcd_uobj))
		uobj_put_read(xrcd_uobj);
	if (pd)
		uobj_put_obj_read(pd);
	if (scq)
		rdma_lookup_put_uobject(&scq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	if (rcq && rcq != scq)
		rdma_lookup_put_uobject(&rcq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	if (srq)
		rdma_lookup_put_uobject(&srq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	if (ind_tbl)
		uobj_put_obj_read(ind_tbl);

	uobj_alloc_abort(&obj->uevent.uobject, attrs);
	return ret;
}

static int ib_uverbs_create_qp(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_create_qp      cmd;
	struct ib_uverbs_ex_create_qp	cmd_ex;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	memset(&cmd_ex, 0, sizeof(cmd_ex));
	cmd_ex.user_handle = cmd.user_handle;
	cmd_ex.pd_handle = cmd.pd_handle;
	cmd_ex.send_cq_handle = cmd.send_cq_handle;
	cmd_ex.recv_cq_handle = cmd.recv_cq_handle;
	cmd_ex.srq_handle = cmd.srq_handle;
	cmd_ex.max_send_wr = cmd.max_send_wr;
	cmd_ex.max_recv_wr = cmd.max_recv_wr;
	cmd_ex.max_send_sge = cmd.max_send_sge;
	cmd_ex.max_recv_sge = cmd.max_recv_sge;
	cmd_ex.max_inline_data = cmd.max_inline_data;
	cmd_ex.sq_sig_all = cmd.sq_sig_all;
	cmd_ex.qp_type = cmd.qp_type;
	cmd_ex.is_srq = cmd.is_srq;

	return create_qp(attrs, &cmd_ex);
}

static int ib_uverbs_ex_create_qp(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_ex_create_qp cmd;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	if (cmd.comp_mask & ~IB_UVERBS_CREATE_QP_SUP_COMP_MASK)
		return -EINVAL;

	if (cmd.reserved)
		return -EINVAL;

	return create_qp(attrs, &cmd);
}

static int ib_uverbs_open_qp(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_create_qp_resp resp = {};
	struct ib_uverbs_open_qp        cmd;
	struct ib_uqp_object           *obj;
	struct ib_xrcd		       *xrcd;
	struct ib_qp                   *qp;
	struct ib_qp_open_attr          attr = {};
	int ret;
	struct ib_uobject *xrcd_uobj;
	struct ib_device *ib_dev;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	obj = (struct ib_uqp_object *)uobj_alloc(UVERBS_OBJECT_QP, attrs,
						 &ib_dev);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	xrcd_uobj = uobj_get_read(UVERBS_OBJECT_XRCD, cmd.pd_handle, attrs);
	if (IS_ERR(xrcd_uobj)) {
		ret = -EINVAL;
		goto err_put;
	}

	xrcd = (struct ib_xrcd *)xrcd_uobj->object;
	if (!xrcd) {
		ret = -EINVAL;
		goto err_xrcd;
	}

	attr.event_handler = ib_uverbs_qp_event_handler;
	attr.qp_num        = cmd.qpn;
	attr.qp_type       = cmd.qp_type;

	INIT_LIST_HEAD(&obj->uevent.event_list);
	INIT_LIST_HEAD(&obj->mcast_list);

	qp = ib_open_qp(xrcd, &attr);
	if (IS_ERR(qp)) {
		ret = PTR_ERR(qp);
		goto err_xrcd;
	}

	obj->uevent.uobject.object = qp;
	obj->uevent.uobject.user_handle = cmd.user_handle;

	obj->uxrcd = container_of(xrcd_uobj, struct ib_uxrcd_object, uobject);
	atomic_inc(&obj->uxrcd->refcnt);
	qp->uobject = obj;
	uobj_put_read(xrcd_uobj);
	uobj_finalize_uobj_create(&obj->uevent.uobject, attrs);

	resp.qpn = qp->qp_num;
	resp.qp_handle = obj->uevent.uobject.id;
	return uverbs_response(attrs, &resp, sizeof(resp));

err_xrcd:
	uobj_put_read(xrcd_uobj);
err_put:
	uobj_alloc_abort(&obj->uevent.uobject, attrs);
	return ret;
}

static void copy_ah_attr_to_uverbs(struct ib_uverbs_qp_dest *uverb_attr,
				   struct rdma_ah_attr *rdma_attr)
{
	const struct ib_global_route   *grh;

	uverb_attr->dlid              = rdma_ah_get_dlid(rdma_attr);
	uverb_attr->sl                = rdma_ah_get_sl(rdma_attr);
	uverb_attr->src_path_bits     = rdma_ah_get_path_bits(rdma_attr);
	uverb_attr->static_rate       = rdma_ah_get_static_rate(rdma_attr);
	uverb_attr->is_global         = !!(rdma_ah_get_ah_flags(rdma_attr) &
					 IB_AH_GRH);
	if (uverb_attr->is_global) {
		grh = rdma_ah_read_grh(rdma_attr);
		memcpy(uverb_attr->dgid, grh->dgid.raw, 16);
		uverb_attr->flow_label        = grh->flow_label;
		uverb_attr->sgid_index        = grh->sgid_index;
		uverb_attr->hop_limit         = grh->hop_limit;
		uverb_attr->traffic_class     = grh->traffic_class;
	}
	uverb_attr->port_num          = rdma_ah_get_port_num(rdma_attr);
}

static int ib_uverbs_query_qp(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_query_qp      cmd;
	struct ib_uverbs_query_qp_resp resp;
	struct ib_qp                   *qp;
	struct ib_qp_attr              *attr;
	struct ib_qp_init_attr         *init_attr;
	int                            ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	attr      = kmalloc(sizeof *attr, GFP_KERNEL);
	init_attr = kmalloc(sizeof *init_attr, GFP_KERNEL);
	if (!attr || !init_attr) {
		ret = -ENOMEM;
		goto out;
	}

	qp = uobj_get_obj_read(qp, UVERBS_OBJECT_QP, cmd.qp_handle, attrs);
	if (!qp) {
		ret = -EINVAL;
		goto out;
	}

	ret = ib_query_qp(qp, attr, cmd.attr_mask, init_attr);

	rdma_lookup_put_uobject(&qp->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);

	if (ret)
		goto out;

	memset(&resp, 0, sizeof resp);

	resp.qp_state               = attr->qp_state;
	resp.cur_qp_state           = attr->cur_qp_state;
	resp.path_mtu               = attr->path_mtu;
	resp.path_mig_state         = attr->path_mig_state;
	resp.qkey                   = attr->qkey;
	resp.rq_psn                 = attr->rq_psn;
	resp.sq_psn                 = attr->sq_psn;
	resp.dest_qp_num            = attr->dest_qp_num;
	resp.qp_access_flags        = attr->qp_access_flags;
	resp.pkey_index             = attr->pkey_index;
	resp.alt_pkey_index         = attr->alt_pkey_index;
	resp.sq_draining            = attr->sq_draining;
	resp.max_rd_atomic          = attr->max_rd_atomic;
	resp.max_dest_rd_atomic     = attr->max_dest_rd_atomic;
	resp.min_rnr_timer          = attr->min_rnr_timer;
	resp.port_num               = attr->port_num;
	resp.timeout                = attr->timeout;
	resp.retry_cnt              = attr->retry_cnt;
	resp.rnr_retry              = attr->rnr_retry;
	resp.alt_port_num           = attr->alt_port_num;
	resp.alt_timeout            = attr->alt_timeout;

	copy_ah_attr_to_uverbs(&resp.dest, &attr->ah_attr);
	copy_ah_attr_to_uverbs(&resp.alt_dest, &attr->alt_ah_attr);

	resp.max_send_wr            = init_attr->cap.max_send_wr;
	resp.max_recv_wr            = init_attr->cap.max_recv_wr;
	resp.max_send_sge           = init_attr->cap.max_send_sge;
	resp.max_recv_sge           = init_attr->cap.max_recv_sge;
	resp.max_inline_data        = init_attr->cap.max_inline_data;
	resp.sq_sig_all             = init_attr->sq_sig_type == IB_SIGNAL_ALL_WR;

	ret = uverbs_response(attrs, &resp, sizeof(resp));

out:
	kfree(attr);
	kfree(init_attr);

	return ret;
}

/* Remove ignored fields set in the attribute mask */
static int modify_qp_mask(enum ib_qp_type qp_type, int mask)
{
	switch (qp_type) {
	case IB_QPT_XRC_INI:
		return mask & ~(IB_QP_MAX_DEST_RD_ATOMIC | IB_QP_MIN_RNR_TIMER);
	case IB_QPT_XRC_TGT:
		return mask & ~(IB_QP_MAX_QP_RD_ATOMIC | IB_QP_RETRY_CNT |
				IB_QP_RNR_RETRY);
	default:
		return mask;
	}
}

static void copy_ah_attr_from_uverbs(struct ib_device *dev,
				     struct rdma_ah_attr *rdma_attr,
				     struct ib_uverbs_qp_dest *uverb_attr)
{
	rdma_attr->type = rdma_ah_find_type(dev, uverb_attr->port_num);
	if (uverb_attr->is_global) {
		rdma_ah_set_grh(rdma_attr, NULL,
				uverb_attr->flow_label,
				uverb_attr->sgid_index,
				uverb_attr->hop_limit,
				uverb_attr->traffic_class);
		rdma_ah_set_dgid_raw(rdma_attr, uverb_attr->dgid);
	} else {
		rdma_ah_set_ah_flags(rdma_attr, 0);
	}
	rdma_ah_set_dlid(rdma_attr, uverb_attr->dlid);
	rdma_ah_set_sl(rdma_attr, uverb_attr->sl);
	rdma_ah_set_path_bits(rdma_attr, uverb_attr->src_path_bits);
	rdma_ah_set_static_rate(rdma_attr, uverb_attr->static_rate);
	rdma_ah_set_port_num(rdma_attr, uverb_attr->port_num);
	rdma_ah_set_make_grd(rdma_attr, false);
}

static int modify_qp(struct uverbs_attr_bundle *attrs,
		     struct ib_uverbs_ex_modify_qp *cmd)
{
	struct ib_qp_attr *attr;
	struct ib_qp *qp;
	int ret;

	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	qp = uobj_get_obj_read(qp, UVERBS_OBJECT_QP, cmd->base.qp_handle,
			       attrs);
	if (!qp) {
		ret = -EINVAL;
		goto out;
	}

	if ((cmd->base.attr_mask & IB_QP_PORT) &&
	    !rdma_is_port_valid(qp->device, cmd->base.port_num)) {
		ret = -EINVAL;
		goto release_qp;
	}

	if ((cmd->base.attr_mask & IB_QP_AV)) {
		if (!rdma_is_port_valid(qp->device, cmd->base.dest.port_num)) {
			ret = -EINVAL;
			goto release_qp;
		}

		if (cmd->base.attr_mask & IB_QP_STATE &&
		    cmd->base.qp_state == IB_QPS_RTR) {
		/* We are in INIT->RTR TRANSITION (if we are not,
		 * this transition will be rejected in subsequent checks).
		 * In the INIT->RTR transition, we cannot have IB_QP_PORT set,
		 * but the IB_QP_STATE flag is required.
		 *
		 * Since kernel 3.14 (commit dbf727de7440), the uverbs driver,
		 * when IB_QP_AV is set, has required inclusion of a valid
		 * port number in the primary AV. (AVs are created and handled
		 * differently for infiniband and ethernet (RoCE) ports).
		 *
		 * Check the port number included in the primary AV against
		 * the port number in the qp struct, which was set (and saved)
		 * in the RST->INIT transition.
		 */
			if (cmd->base.dest.port_num != qp->real_qp->port) {
				ret = -EINVAL;
				goto release_qp;
			}
		} else {
		/* We are in SQD->SQD. (If we are not, this transition will
		 * be rejected later in the verbs layer checks).
		 * Check for both IB_QP_PORT and IB_QP_AV, these can be set
		 * together in the SQD->SQD transition.
		 *
		 * If only IP_QP_AV was set, add in IB_QP_PORT as well (the
		 * verbs layer driver does not track primary port changes
		 * resulting from path migration. Thus, in SQD, if the primary
		 * AV is modified, the primary port should also be modified).
		 *
		 * Note that in this transition, the IB_QP_STATE flag
		 * is not allowed.
		 */
			if (((cmd->base.attr_mask & (IB_QP_AV | IB_QP_PORT))
			     == (IB_QP_AV | IB_QP_PORT)) &&
			    cmd->base.port_num != cmd->base.dest.port_num) {
				ret = -EINVAL;
				goto release_qp;
			}
			if ((cmd->base.attr_mask & (IB_QP_AV | IB_QP_PORT))
			    == IB_QP_AV) {
				cmd->base.attr_mask |= IB_QP_PORT;
				cmd->base.port_num = cmd->base.dest.port_num;
			}
		}
	}

	if ((cmd->base.attr_mask & IB_QP_ALT_PATH) &&
	    (!rdma_is_port_valid(qp->device, cmd->base.alt_port_num) ||
	    !rdma_is_port_valid(qp->device, cmd->base.alt_dest.port_num) ||
	    cmd->base.alt_port_num != cmd->base.alt_dest.port_num)) {
		ret = -EINVAL;
		goto release_qp;
	}

	if ((cmd->base.attr_mask & IB_QP_CUR_STATE &&
	    cmd->base.cur_qp_state > IB_QPS_ERR) ||
	    (cmd->base.attr_mask & IB_QP_STATE &&
	    cmd->base.qp_state > IB_QPS_ERR)) {
		ret = -EINVAL;
		goto release_qp;
	}

	if (cmd->base.attr_mask & IB_QP_STATE)
		attr->qp_state = cmd->base.qp_state;
	if (cmd->base.attr_mask & IB_QP_CUR_STATE)
		attr->cur_qp_state = cmd->base.cur_qp_state;
	if (cmd->base.attr_mask & IB_QP_PATH_MTU)
		attr->path_mtu = cmd->base.path_mtu;
	if (cmd->base.attr_mask & IB_QP_PATH_MIG_STATE)
		attr->path_mig_state = cmd->base.path_mig_state;
	if (cmd->base.attr_mask & IB_QP_QKEY)
		attr->qkey = cmd->base.qkey;
	if (cmd->base.attr_mask & IB_QP_RQ_PSN)
		attr->rq_psn = cmd->base.rq_psn;
	if (cmd->base.attr_mask & IB_QP_SQ_PSN)
		attr->sq_psn = cmd->base.sq_psn;
	if (cmd->base.attr_mask & IB_QP_DEST_QPN)
		attr->dest_qp_num = cmd->base.dest_qp_num;
	if (cmd->base.attr_mask & IB_QP_ACCESS_FLAGS)
		attr->qp_access_flags = cmd->base.qp_access_flags;
	if (cmd->base.attr_mask & IB_QP_PKEY_INDEX)
		attr->pkey_index = cmd->base.pkey_index;
	if (cmd->base.attr_mask & IB_QP_EN_SQD_ASYNC_NOTIFY)
		attr->en_sqd_async_notify = cmd->base.en_sqd_async_notify;
	if (cmd->base.attr_mask & IB_QP_MAX_QP_RD_ATOMIC)
		attr->max_rd_atomic = cmd->base.max_rd_atomic;
	if (cmd->base.attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		attr->max_dest_rd_atomic = cmd->base.max_dest_rd_atomic;
	if (cmd->base.attr_mask & IB_QP_MIN_RNR_TIMER)
		attr->min_rnr_timer = cmd->base.min_rnr_timer;
	if (cmd->base.attr_mask & IB_QP_PORT)
		attr->port_num = cmd->base.port_num;
	if (cmd->base.attr_mask & IB_QP_TIMEOUT)
		attr->timeout = cmd->base.timeout;
	if (cmd->base.attr_mask & IB_QP_RETRY_CNT)
		attr->retry_cnt = cmd->base.retry_cnt;
	if (cmd->base.attr_mask & IB_QP_RNR_RETRY)
		attr->rnr_retry = cmd->base.rnr_retry;
	if (cmd->base.attr_mask & IB_QP_ALT_PATH) {
		attr->alt_port_num = cmd->base.alt_port_num;
		attr->alt_timeout = cmd->base.alt_timeout;
		attr->alt_pkey_index = cmd->base.alt_pkey_index;
	}
	if (cmd->base.attr_mask & IB_QP_RATE_LIMIT)
		attr->rate_limit = cmd->rate_limit;

	if (cmd->base.attr_mask & IB_QP_AV)
		copy_ah_attr_from_uverbs(qp->device, &attr->ah_attr,
					 &cmd->base.dest);

	if (cmd->base.attr_mask & IB_QP_ALT_PATH)
		copy_ah_attr_from_uverbs(qp->device, &attr->alt_ah_attr,
					 &cmd->base.alt_dest);

	ret = ib_modify_qp_with_udata(qp, attr,
				      modify_qp_mask(qp->qp_type,
						     cmd->base.attr_mask),
				      &attrs->driver_udata);

release_qp:
	rdma_lookup_put_uobject(&qp->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);
out:
	kfree(attr);

	return ret;
}

static int ib_uverbs_modify_qp(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_ex_modify_qp cmd;
	int ret;

	ret = uverbs_request(attrs, &cmd.base, sizeof(cmd.base));
	if (ret)
		return ret;

	if (cmd.base.attr_mask &
	    ~((IB_USER_LEGACY_LAST_QP_ATTR_MASK << 1) - 1))
		return -EOPNOTSUPP;

	return modify_qp(attrs, &cmd);
}

static int ib_uverbs_ex_modify_qp(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_ex_modify_qp cmd;
	struct ib_uverbs_ex_modify_qp_resp resp = {
		.response_length = uverbs_response_length(attrs, sizeof(resp))
	};
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	/*
	 * Last bit is reserved for extending the attr_mask by
	 * using another field.
	 */
	BUILD_BUG_ON(IB_USER_LAST_QP_ATTR_MASK == (1ULL << 31));

	if (cmd.base.attr_mask &
	    ~((IB_USER_LAST_QP_ATTR_MASK << 1) - 1))
		return -EOPNOTSUPP;

	ret = modify_qp(attrs, &cmd);
	if (ret)
		return ret;

	return uverbs_response(attrs, &resp, sizeof(resp));
}

static int ib_uverbs_destroy_qp(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_destroy_qp      cmd;
	struct ib_uverbs_destroy_qp_resp resp;
	struct ib_uobject		*uobj;
	struct ib_uqp_object        	*obj;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	uobj = uobj_get_destroy(UVERBS_OBJECT_QP, cmd.qp_handle, attrs);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	obj = container_of(uobj, struct ib_uqp_object, uevent.uobject);
	memset(&resp, 0, sizeof(resp));
	resp.events_reported = obj->uevent.events_reported;

	uobj_put_destroy(uobj);

	return uverbs_response(attrs, &resp, sizeof(resp));
}

static void *alloc_wr(size_t wr_size, __u32 num_sge)
{
	if (num_sge >= (U32_MAX - ALIGN(wr_size, sizeof (struct ib_sge))) /
		       sizeof (struct ib_sge))
		return NULL;

	return kmalloc(ALIGN(wr_size, sizeof (struct ib_sge)) +
			 num_sge * sizeof (struct ib_sge), GFP_KERNEL);
}

static int ib_uverbs_post_send(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_post_send      cmd;
	struct ib_uverbs_post_send_resp resp;
	struct ib_uverbs_send_wr       *user_wr;
	struct ib_send_wr              *wr = NULL, *last, *next;
	const struct ib_send_wr	       *bad_wr;
	struct ib_qp                   *qp;
	int                             i, sg_ind;
	int				is_ud;
	int ret, ret2;
	size_t                          next_size;
	const struct ib_sge __user *sgls;
	const void __user *wqes;
	struct uverbs_req_iter iter;

	ret = uverbs_request_start(attrs, &iter, &cmd, sizeof(cmd));
	if (ret)
		return ret;
	wqes = uverbs_request_next_ptr(&iter, cmd.wqe_size * cmd.wr_count);
	if (IS_ERR(wqes))
		return PTR_ERR(wqes);
	sgls = uverbs_request_next_ptr(
		&iter, cmd.sge_count * sizeof(struct ib_uverbs_sge));
	if (IS_ERR(sgls))
		return PTR_ERR(sgls);
	ret = uverbs_request_finish(&iter);
	if (ret)
		return ret;

	user_wr = kmalloc(cmd.wqe_size, GFP_KERNEL);
	if (!user_wr)
		return -ENOMEM;

	qp = uobj_get_obj_read(qp, UVERBS_OBJECT_QP, cmd.qp_handle, attrs);
	if (!qp) {
		ret = -EINVAL;
		goto out;
	}

	is_ud = qp->qp_type == IB_QPT_UD;
	sg_ind = 0;
	last = NULL;
	for (i = 0; i < cmd.wr_count; ++i) {
		if (copy_from_user(user_wr, wqes + i * cmd.wqe_size,
				   cmd.wqe_size)) {
			ret = -EFAULT;
			goto out_put;
		}

		if (user_wr->num_sge + sg_ind > cmd.sge_count) {
			ret = -EINVAL;
			goto out_put;
		}

		if (is_ud) {
			struct ib_ud_wr *ud;

			if (user_wr->opcode != IB_WR_SEND &&
			    user_wr->opcode != IB_WR_SEND_WITH_IMM) {
				ret = -EINVAL;
				goto out_put;
			}

			next_size = sizeof(*ud);
			ud = alloc_wr(next_size, user_wr->num_sge);
			if (!ud) {
				ret = -ENOMEM;
				goto out_put;
			}

			ud->ah = uobj_get_obj_read(ah, UVERBS_OBJECT_AH,
						   user_wr->wr.ud.ah, attrs);
			if (!ud->ah) {
				kfree(ud);
				ret = -EINVAL;
				goto out_put;
			}
			ud->remote_qpn = user_wr->wr.ud.remote_qpn;
			ud->remote_qkey = user_wr->wr.ud.remote_qkey;

			next = &ud->wr;
		} else if (user_wr->opcode == IB_WR_RDMA_WRITE_WITH_IMM ||
			   user_wr->opcode == IB_WR_RDMA_WRITE ||
			   user_wr->opcode == IB_WR_RDMA_READ) {
			struct ib_rdma_wr *rdma;

			next_size = sizeof(*rdma);
			rdma = alloc_wr(next_size, user_wr->num_sge);
			if (!rdma) {
				ret = -ENOMEM;
				goto out_put;
			}

			rdma->remote_addr = user_wr->wr.rdma.remote_addr;
			rdma->rkey = user_wr->wr.rdma.rkey;

			next = &rdma->wr;
		} else if (user_wr->opcode == IB_WR_ATOMIC_CMP_AND_SWP ||
			   user_wr->opcode == IB_WR_ATOMIC_FETCH_AND_ADD) {
			struct ib_atomic_wr *atomic;

			next_size = sizeof(*atomic);
			atomic = alloc_wr(next_size, user_wr->num_sge);
			if (!atomic) {
				ret = -ENOMEM;
				goto out_put;
			}

			atomic->remote_addr = user_wr->wr.atomic.remote_addr;
			atomic->compare_add = user_wr->wr.atomic.compare_add;
			atomic->swap = user_wr->wr.atomic.swap;
			atomic->rkey = user_wr->wr.atomic.rkey;

			next = &atomic->wr;
		} else if (user_wr->opcode == IB_WR_SEND ||
			   user_wr->opcode == IB_WR_SEND_WITH_IMM ||
			   user_wr->opcode == IB_WR_SEND_WITH_INV) {
			next_size = sizeof(*next);
			next = alloc_wr(next_size, user_wr->num_sge);
			if (!next) {
				ret = -ENOMEM;
				goto out_put;
			}
		} else {
			ret = -EINVAL;
			goto out_put;
		}

		if (user_wr->opcode == IB_WR_SEND_WITH_IMM ||
		    user_wr->opcode == IB_WR_RDMA_WRITE_WITH_IMM) {
			next->ex.imm_data =
					(__be32 __force) user_wr->ex.imm_data;
		} else if (user_wr->opcode == IB_WR_SEND_WITH_INV) {
			next->ex.invalidate_rkey = user_wr->ex.invalidate_rkey;
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

		if (next->num_sge) {
			next->sg_list = (void *) next +
				ALIGN(next_size, sizeof(struct ib_sge));
			if (copy_from_user(next->sg_list, sgls + sg_ind,
					   next->num_sge *
						   sizeof(struct ib_sge))) {
				ret = -EFAULT;
				goto out_put;
			}
			sg_ind += next->num_sge;
		} else
			next->sg_list = NULL;
	}

	resp.bad_wr = 0;
	ret = qp->device->ops.post_send(qp->real_qp, wr, &bad_wr);
	if (ret)
		for (next = wr; next; next = next->next) {
			++resp.bad_wr;
			if (next == bad_wr)
				break;
		}

	ret2 = uverbs_response(attrs, &resp, sizeof(resp));
	if (ret2)
		ret = ret2;

out_put:
	rdma_lookup_put_uobject(&qp->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);

	while (wr) {
		if (is_ud && ud_wr(wr)->ah)
			uobj_put_obj_read(ud_wr(wr)->ah);
		next = wr->next;
		kfree(wr);
		wr = next;
	}

out:
	kfree(user_wr);

	return ret;
}

static struct ib_recv_wr *
ib_uverbs_unmarshall_recv(struct uverbs_req_iter *iter, u32 wr_count,
			  u32 wqe_size, u32 sge_count)
{
	struct ib_uverbs_recv_wr *user_wr;
	struct ib_recv_wr        *wr = NULL, *last, *next;
	int                       sg_ind;
	int                       i;
	int                       ret;
	const struct ib_sge __user *sgls;
	const void __user *wqes;

	if (wqe_size < sizeof (struct ib_uverbs_recv_wr))
		return ERR_PTR(-EINVAL);

	wqes = uverbs_request_next_ptr(iter, wqe_size * wr_count);
	if (IS_ERR(wqes))
		return ERR_CAST(wqes);
	sgls = uverbs_request_next_ptr(
		iter, sge_count * sizeof(struct ib_uverbs_sge));
	if (IS_ERR(sgls))
		return ERR_CAST(sgls);
	ret = uverbs_request_finish(iter);
	if (ret)
		return ERR_PTR(ret);

	user_wr = kmalloc(wqe_size, GFP_KERNEL);
	if (!user_wr)
		return ERR_PTR(-ENOMEM);

	sg_ind = 0;
	last = NULL;
	for (i = 0; i < wr_count; ++i) {
		if (copy_from_user(user_wr, wqes + i * wqe_size,
				   wqe_size)) {
			ret = -EFAULT;
			goto err;
		}

		if (user_wr->num_sge + sg_ind > sge_count) {
			ret = -EINVAL;
			goto err;
		}

		if (user_wr->num_sge >=
		    (U32_MAX - ALIGN(sizeof *next, sizeof (struct ib_sge))) /
		    sizeof (struct ib_sge)) {
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
			if (copy_from_user(next->sg_list, sgls + sg_ind,
					   next->num_sge *
						   sizeof(struct ib_sge))) {
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

static int ib_uverbs_post_recv(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_post_recv      cmd;
	struct ib_uverbs_post_recv_resp resp;
	struct ib_recv_wr              *wr, *next;
	const struct ib_recv_wr	       *bad_wr;
	struct ib_qp                   *qp;
	int ret, ret2;
	struct uverbs_req_iter iter;

	ret = uverbs_request_start(attrs, &iter, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	wr = ib_uverbs_unmarshall_recv(&iter, cmd.wr_count, cmd.wqe_size,
				       cmd.sge_count);
	if (IS_ERR(wr))
		return PTR_ERR(wr);

	qp = uobj_get_obj_read(qp, UVERBS_OBJECT_QP, cmd.qp_handle, attrs);
	if (!qp) {
		ret = -EINVAL;
		goto out;
	}

	resp.bad_wr = 0;
	ret = qp->device->ops.post_recv(qp->real_qp, wr, &bad_wr);

	rdma_lookup_put_uobject(&qp->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);
	if (ret) {
		for (next = wr; next; next = next->next) {
			++resp.bad_wr;
			if (next == bad_wr)
				break;
		}
	}

	ret2 = uverbs_response(attrs, &resp, sizeof(resp));
	if (ret2)
		ret = ret2;
out:
	while (wr) {
		next = wr->next;
		kfree(wr);
		wr = next;
	}

	return ret;
}

static int ib_uverbs_post_srq_recv(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_post_srq_recv      cmd;
	struct ib_uverbs_post_srq_recv_resp resp;
	struct ib_recv_wr                  *wr, *next;
	const struct ib_recv_wr		   *bad_wr;
	struct ib_srq                      *srq;
	int ret, ret2;
	struct uverbs_req_iter iter;

	ret = uverbs_request_start(attrs, &iter, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	wr = ib_uverbs_unmarshall_recv(&iter, cmd.wr_count, cmd.wqe_size,
				       cmd.sge_count);
	if (IS_ERR(wr))
		return PTR_ERR(wr);

	srq = uobj_get_obj_read(srq, UVERBS_OBJECT_SRQ, cmd.srq_handle, attrs);
	if (!srq) {
		ret = -EINVAL;
		goto out;
	}

	resp.bad_wr = 0;
	ret = srq->device->ops.post_srq_recv(srq, wr, &bad_wr);

	rdma_lookup_put_uobject(&srq->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);

	if (ret)
		for (next = wr; next; next = next->next) {
			++resp.bad_wr;
			if (next == bad_wr)
				break;
		}

	ret2 = uverbs_response(attrs, &resp, sizeof(resp));
	if (ret2)
		ret = ret2;

out:
	while (wr) {
		next = wr->next;
		kfree(wr);
		wr = next;
	}

	return ret;
}

static int ib_uverbs_create_ah(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_create_ah	 cmd;
	struct ib_uverbs_create_ah_resp	 resp;
	struct ib_uobject		*uobj;
	struct ib_pd			*pd;
	struct ib_ah			*ah;
	struct rdma_ah_attr		attr = {};
	int ret;
	struct ib_device *ib_dev;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	uobj = uobj_alloc(UVERBS_OBJECT_AH, attrs, &ib_dev);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	if (!rdma_is_port_valid(ib_dev, cmd.attr.port_num)) {
		ret = -EINVAL;
		goto err;
	}

	pd = uobj_get_obj_read(pd, UVERBS_OBJECT_PD, cmd.pd_handle, attrs);
	if (!pd) {
		ret = -EINVAL;
		goto err;
	}

	attr.type = rdma_ah_find_type(ib_dev, cmd.attr.port_num);
	rdma_ah_set_make_grd(&attr, false);
	rdma_ah_set_dlid(&attr, cmd.attr.dlid);
	rdma_ah_set_sl(&attr, cmd.attr.sl);
	rdma_ah_set_path_bits(&attr, cmd.attr.src_path_bits);
	rdma_ah_set_static_rate(&attr, cmd.attr.static_rate);
	rdma_ah_set_port_num(&attr, cmd.attr.port_num);

	if (cmd.attr.is_global) {
		rdma_ah_set_grh(&attr, NULL, cmd.attr.grh.flow_label,
				cmd.attr.grh.sgid_index,
				cmd.attr.grh.hop_limit,
				cmd.attr.grh.traffic_class);
		rdma_ah_set_dgid_raw(&attr, cmd.attr.grh.dgid);
	} else {
		rdma_ah_set_ah_flags(&attr, 0);
	}

	ah = rdma_create_user_ah(pd, &attr, &attrs->driver_udata);
	if (IS_ERR(ah)) {
		ret = PTR_ERR(ah);
		goto err_put;
	}

	ah->uobject  = uobj;
	uobj->user_handle = cmd.user_handle;
	uobj->object = ah;
	uobj_put_obj_read(pd);
	uobj_finalize_uobj_create(uobj, attrs);

	resp.ah_handle = uobj->id;
	return uverbs_response(attrs, &resp, sizeof(resp));

err_put:
	uobj_put_obj_read(pd);
err:
	uobj_alloc_abort(uobj, attrs);
	return ret;
}

static int ib_uverbs_destroy_ah(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_destroy_ah cmd;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	return uobj_perform_destroy(UVERBS_OBJECT_AH, cmd.ah_handle, attrs);
}

static int ib_uverbs_attach_mcast(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_attach_mcast cmd;
	struct ib_qp                 *qp;
	struct ib_uqp_object         *obj;
	struct ib_uverbs_mcast_entry *mcast;
	int                           ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	qp = uobj_get_obj_read(qp, UVERBS_OBJECT_QP, cmd.qp_handle, attrs);
	if (!qp)
		return -EINVAL;

	obj = qp->uobject;

	mutex_lock(&obj->mcast_lock);
	list_for_each_entry(mcast, &obj->mcast_list, list)
		if (cmd.mlid == mcast->lid &&
		    !memcmp(cmd.gid, mcast->gid.raw, sizeof mcast->gid.raw)) {
			ret = 0;
			goto out_put;
		}

	mcast = kmalloc(sizeof *mcast, GFP_KERNEL);
	if (!mcast) {
		ret = -ENOMEM;
		goto out_put;
	}

	mcast->lid = cmd.mlid;
	memcpy(mcast->gid.raw, cmd.gid, sizeof mcast->gid.raw);

	ret = ib_attach_mcast(qp, &mcast->gid, cmd.mlid);
	if (!ret)
		list_add_tail(&mcast->list, &obj->mcast_list);
	else
		kfree(mcast);

out_put:
	mutex_unlock(&obj->mcast_lock);
	rdma_lookup_put_uobject(&qp->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);

	return ret;
}

static int ib_uverbs_detach_mcast(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_detach_mcast cmd;
	struct ib_uqp_object         *obj;
	struct ib_qp                 *qp;
	struct ib_uverbs_mcast_entry *mcast;
	int                           ret;
	bool                          found = false;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	qp = uobj_get_obj_read(qp, UVERBS_OBJECT_QP, cmd.qp_handle, attrs);
	if (!qp)
		return -EINVAL;

	obj = qp->uobject;
	mutex_lock(&obj->mcast_lock);

	list_for_each_entry(mcast, &obj->mcast_list, list)
		if (cmd.mlid == mcast->lid &&
		    !memcmp(cmd.gid, mcast->gid.raw, sizeof mcast->gid.raw)) {
			list_del(&mcast->list);
			kfree(mcast);
			found = true;
			break;
		}

	if (!found) {
		ret = -EINVAL;
		goto out_put;
	}

	ret = ib_detach_mcast(qp, (union ib_gid *)cmd.gid, cmd.mlid);

out_put:
	mutex_unlock(&obj->mcast_lock);
	rdma_lookup_put_uobject(&qp->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);
	return ret;
}

struct ib_uflow_resources *flow_resources_alloc(size_t num_specs)
{
	struct ib_uflow_resources *resources;

	resources = kzalloc(sizeof(*resources), GFP_KERNEL);

	if (!resources)
		return NULL;

	if (!num_specs)
		goto out;

	resources->counters =
		kcalloc(num_specs, sizeof(*resources->counters), GFP_KERNEL);
	resources->collection =
		kcalloc(num_specs, sizeof(*resources->collection), GFP_KERNEL);

	if (!resources->counters || !resources->collection)
		goto err;

out:
	resources->max = num_specs;
	return resources;

err:
	kfree(resources->counters);
	kfree(resources);

	return NULL;
}
EXPORT_SYMBOL(flow_resources_alloc);

void ib_uverbs_flow_resources_free(struct ib_uflow_resources *uflow_res)
{
	unsigned int i;

	if (!uflow_res)
		return;

	for (i = 0; i < uflow_res->collection_num; i++)
		atomic_dec(&uflow_res->collection[i]->usecnt);

	for (i = 0; i < uflow_res->counters_num; i++)
		atomic_dec(&uflow_res->counters[i]->usecnt);

	kfree(uflow_res->collection);
	kfree(uflow_res->counters);
	kfree(uflow_res);
}
EXPORT_SYMBOL(ib_uverbs_flow_resources_free);

void flow_resources_add(struct ib_uflow_resources *uflow_res,
			enum ib_flow_spec_type type,
			void *ibobj)
{
	WARN_ON(uflow_res->num >= uflow_res->max);

	switch (type) {
	case IB_FLOW_SPEC_ACTION_HANDLE:
		atomic_inc(&((struct ib_flow_action *)ibobj)->usecnt);
		uflow_res->collection[uflow_res->collection_num++] =
			(struct ib_flow_action *)ibobj;
		break;
	case IB_FLOW_SPEC_ACTION_COUNT:
		atomic_inc(&((struct ib_counters *)ibobj)->usecnt);
		uflow_res->counters[uflow_res->counters_num++] =
			(struct ib_counters *)ibobj;
		break;
	default:
		WARN_ON(1);
	}

	uflow_res->num++;
}
EXPORT_SYMBOL(flow_resources_add);

static int kern_spec_to_ib_spec_action(struct uverbs_attr_bundle *attrs,
				       struct ib_uverbs_flow_spec *kern_spec,
				       union ib_flow_spec *ib_spec,
				       struct ib_uflow_resources *uflow_res)
{
	ib_spec->type = kern_spec->type;
	switch (ib_spec->type) {
	case IB_FLOW_SPEC_ACTION_TAG:
		if (kern_spec->flow_tag.size !=
		    sizeof(struct ib_uverbs_flow_spec_action_tag))
			return -EINVAL;

		ib_spec->flow_tag.size = sizeof(struct ib_flow_spec_action_tag);
		ib_spec->flow_tag.tag_id = kern_spec->flow_tag.tag_id;
		break;
	case IB_FLOW_SPEC_ACTION_DROP:
		if (kern_spec->drop.size !=
		    sizeof(struct ib_uverbs_flow_spec_action_drop))
			return -EINVAL;

		ib_spec->drop.size = sizeof(struct ib_flow_spec_action_drop);
		break;
	case IB_FLOW_SPEC_ACTION_HANDLE:
		if (kern_spec->action.size !=
		    sizeof(struct ib_uverbs_flow_spec_action_handle))
			return -EOPNOTSUPP;
		ib_spec->action.act = uobj_get_obj_read(flow_action,
							UVERBS_OBJECT_FLOW_ACTION,
							kern_spec->action.handle,
							attrs);
		if (!ib_spec->action.act)
			return -EINVAL;
		ib_spec->action.size =
			sizeof(struct ib_flow_spec_action_handle);
		flow_resources_add(uflow_res,
				   IB_FLOW_SPEC_ACTION_HANDLE,
				   ib_spec->action.act);
		uobj_put_obj_read(ib_spec->action.act);
		break;
	case IB_FLOW_SPEC_ACTION_COUNT:
		if (kern_spec->flow_count.size !=
			sizeof(struct ib_uverbs_flow_spec_action_count))
			return -EINVAL;
		ib_spec->flow_count.counters =
			uobj_get_obj_read(counters,
					  UVERBS_OBJECT_COUNTERS,
					  kern_spec->flow_count.handle,
					  attrs);
		if (!ib_spec->flow_count.counters)
			return -EINVAL;
		ib_spec->flow_count.size =
				sizeof(struct ib_flow_spec_action_count);
		flow_resources_add(uflow_res,
				   IB_FLOW_SPEC_ACTION_COUNT,
				   ib_spec->flow_count.counters);
		uobj_put_obj_read(ib_spec->flow_count.counters);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static ssize_t spec_filter_size(const void *kern_spec_filter, u16 kern_filter_size,
				u16 ib_real_filter_sz)
{
	/*
	 * User space filter structures must be 64 bit aligned, otherwise this
	 * may pass, but we won't handle additional new attributes.
	 */

	if (kern_filter_size > ib_real_filter_sz) {
		if (memchr_inv(kern_spec_filter +
			       ib_real_filter_sz, 0,
			       kern_filter_size - ib_real_filter_sz))
			return -EINVAL;
		return ib_real_filter_sz;
	}
	return kern_filter_size;
}

int ib_uverbs_kern_spec_to_ib_spec_filter(enum ib_flow_spec_type type,
					  const void *kern_spec_mask,
					  const void *kern_spec_val,
					  size_t kern_filter_sz,
					  union ib_flow_spec *ib_spec)
{
	ssize_t actual_filter_sz;
	ssize_t ib_filter_sz;

	/* User flow spec size must be aligned to 4 bytes */
	if (kern_filter_sz != ALIGN(kern_filter_sz, 4))
		return -EINVAL;

	ib_spec->type = type;

	if (ib_spec->type == (IB_FLOW_SPEC_INNER | IB_FLOW_SPEC_VXLAN_TUNNEL))
		return -EINVAL;

	switch (ib_spec->type & ~IB_FLOW_SPEC_INNER) {
	case IB_FLOW_SPEC_ETH:
		ib_filter_sz = offsetof(struct ib_flow_eth_filter, real_sz);
		actual_filter_sz = spec_filter_size(kern_spec_mask,
						    kern_filter_sz,
						    ib_filter_sz);
		if (actual_filter_sz <= 0)
			return -EINVAL;
		ib_spec->size = sizeof(struct ib_flow_spec_eth);
		memcpy(&ib_spec->eth.val, kern_spec_val, actual_filter_sz);
		memcpy(&ib_spec->eth.mask, kern_spec_mask, actual_filter_sz);
		break;
	case IB_FLOW_SPEC_IPV4:
		ib_filter_sz = offsetof(struct ib_flow_ipv4_filter, real_sz);
		actual_filter_sz = spec_filter_size(kern_spec_mask,
						    kern_filter_sz,
						    ib_filter_sz);
		if (actual_filter_sz <= 0)
			return -EINVAL;
		ib_spec->size = sizeof(struct ib_flow_spec_ipv4);
		memcpy(&ib_spec->ipv4.val, kern_spec_val, actual_filter_sz);
		memcpy(&ib_spec->ipv4.mask, kern_spec_mask, actual_filter_sz);
		break;
	case IB_FLOW_SPEC_IPV6:
		ib_filter_sz = offsetof(struct ib_flow_ipv6_filter, real_sz);
		actual_filter_sz = spec_filter_size(kern_spec_mask,
						    kern_filter_sz,
						    ib_filter_sz);
		if (actual_filter_sz <= 0)
			return -EINVAL;
		ib_spec->size = sizeof(struct ib_flow_spec_ipv6);
		memcpy(&ib_spec->ipv6.val, kern_spec_val, actual_filter_sz);
		memcpy(&ib_spec->ipv6.mask, kern_spec_mask, actual_filter_sz);

		if ((ntohl(ib_spec->ipv6.mask.flow_label)) >= BIT(20) ||
		    (ntohl(ib_spec->ipv6.val.flow_label)) >= BIT(20))
			return -EINVAL;
		break;
	case IB_FLOW_SPEC_TCP:
	case IB_FLOW_SPEC_UDP:
		ib_filter_sz = offsetof(struct ib_flow_tcp_udp_filter, real_sz);
		actual_filter_sz = spec_filter_size(kern_spec_mask,
						    kern_filter_sz,
						    ib_filter_sz);
		if (actual_filter_sz <= 0)
			return -EINVAL;
		ib_spec->size = sizeof(struct ib_flow_spec_tcp_udp);
		memcpy(&ib_spec->tcp_udp.val, kern_spec_val, actual_filter_sz);
		memcpy(&ib_spec->tcp_udp.mask, kern_spec_mask, actual_filter_sz);
		break;
	case IB_FLOW_SPEC_VXLAN_TUNNEL:
		ib_filter_sz = offsetof(struct ib_flow_tunnel_filter, real_sz);
		actual_filter_sz = spec_filter_size(kern_spec_mask,
						    kern_filter_sz,
						    ib_filter_sz);
		if (actual_filter_sz <= 0)
			return -EINVAL;
		ib_spec->tunnel.size = sizeof(struct ib_flow_spec_tunnel);
		memcpy(&ib_spec->tunnel.val, kern_spec_val, actual_filter_sz);
		memcpy(&ib_spec->tunnel.mask, kern_spec_mask, actual_filter_sz);

		if ((ntohl(ib_spec->tunnel.mask.tunnel_id)) >= BIT(24) ||
		    (ntohl(ib_spec->tunnel.val.tunnel_id)) >= BIT(24))
			return -EINVAL;
		break;
	case IB_FLOW_SPEC_ESP:
		ib_filter_sz = offsetof(struct ib_flow_esp_filter, real_sz);
		actual_filter_sz = spec_filter_size(kern_spec_mask,
						    kern_filter_sz,
						    ib_filter_sz);
		if (actual_filter_sz <= 0)
			return -EINVAL;
		ib_spec->esp.size = sizeof(struct ib_flow_spec_esp);
		memcpy(&ib_spec->esp.val, kern_spec_val, actual_filter_sz);
		memcpy(&ib_spec->esp.mask, kern_spec_mask, actual_filter_sz);
		break;
	case IB_FLOW_SPEC_GRE:
		ib_filter_sz = offsetof(struct ib_flow_gre_filter, real_sz);
		actual_filter_sz = spec_filter_size(kern_spec_mask,
						    kern_filter_sz,
						    ib_filter_sz);
		if (actual_filter_sz <= 0)
			return -EINVAL;
		ib_spec->gre.size = sizeof(struct ib_flow_spec_gre);
		memcpy(&ib_spec->gre.val, kern_spec_val, actual_filter_sz);
		memcpy(&ib_spec->gre.mask, kern_spec_mask, actual_filter_sz);
		break;
	case IB_FLOW_SPEC_MPLS:
		ib_filter_sz = offsetof(struct ib_flow_mpls_filter, real_sz);
		actual_filter_sz = spec_filter_size(kern_spec_mask,
						    kern_filter_sz,
						    ib_filter_sz);
		if (actual_filter_sz <= 0)
			return -EINVAL;
		ib_spec->mpls.size = sizeof(struct ib_flow_spec_mpls);
		memcpy(&ib_spec->mpls.val, kern_spec_val, actual_filter_sz);
		memcpy(&ib_spec->mpls.mask, kern_spec_mask, actual_filter_sz);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int kern_spec_to_ib_spec_filter(struct ib_uverbs_flow_spec *kern_spec,
				       union ib_flow_spec *ib_spec)
{
	size_t kern_filter_sz;
	void *kern_spec_mask;
	void *kern_spec_val;

	if (check_sub_overflow((size_t)kern_spec->hdr.size,
			       sizeof(struct ib_uverbs_flow_spec_hdr),
			       &kern_filter_sz))
		return -EINVAL;

	kern_filter_sz /= 2;

	kern_spec_val = (void *)kern_spec +
		sizeof(struct ib_uverbs_flow_spec_hdr);
	kern_spec_mask = kern_spec_val + kern_filter_sz;

	return ib_uverbs_kern_spec_to_ib_spec_filter(kern_spec->type,
						     kern_spec_mask,
						     kern_spec_val,
						     kern_filter_sz, ib_spec);
}

static int kern_spec_to_ib_spec(struct uverbs_attr_bundle *attrs,
				struct ib_uverbs_flow_spec *kern_spec,
				union ib_flow_spec *ib_spec,
				struct ib_uflow_resources *uflow_res)
{
	if (kern_spec->reserved)
		return -EINVAL;

	if (kern_spec->type >= IB_FLOW_SPEC_ACTION_TAG)
		return kern_spec_to_ib_spec_action(attrs, kern_spec, ib_spec,
						   uflow_res);
	else
		return kern_spec_to_ib_spec_filter(kern_spec, ib_spec);
}

static int ib_uverbs_ex_create_wq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_ex_create_wq cmd;
	struct ib_uverbs_ex_create_wq_resp resp = {};
	struct ib_uwq_object           *obj;
	int err = 0;
	struct ib_cq *cq;
	struct ib_pd *pd;
	struct ib_wq *wq;
	struct ib_wq_init_attr wq_init_attr = {};
	struct ib_device *ib_dev;

	err = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (err)
		return err;

	if (cmd.comp_mask)
		return -EOPNOTSUPP;

	obj = (struct ib_uwq_object *)uobj_alloc(UVERBS_OBJECT_WQ, attrs,
						 &ib_dev);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	pd = uobj_get_obj_read(pd, UVERBS_OBJECT_PD, cmd.pd_handle, attrs);
	if (!pd) {
		err = -EINVAL;
		goto err_uobj;
	}

	cq = uobj_get_obj_read(cq, UVERBS_OBJECT_CQ, cmd.cq_handle, attrs);
	if (!cq) {
		err = -EINVAL;
		goto err_put_pd;
	}

	wq_init_attr.cq = cq;
	wq_init_attr.max_sge = cmd.max_sge;
	wq_init_attr.max_wr = cmd.max_wr;
	wq_init_attr.wq_type = cmd.wq_type;
	wq_init_attr.event_handler = ib_uverbs_wq_event_handler;
	wq_init_attr.create_flags = cmd.create_flags;
	INIT_LIST_HEAD(&obj->uevent.event_list);
	obj->uevent.uobject.user_handle = cmd.user_handle;

	wq = pd->device->ops.create_wq(pd, &wq_init_attr, &attrs->driver_udata);
	if (IS_ERR(wq)) {
		err = PTR_ERR(wq);
		goto err_put_cq;
	}

	wq->uobject = obj;
	obj->uevent.uobject.object = wq;
	wq->wq_type = wq_init_attr.wq_type;
	wq->cq = cq;
	wq->pd = pd;
	wq->device = pd->device;
	atomic_set(&wq->usecnt, 0);
	atomic_inc(&pd->usecnt);
	atomic_inc(&cq->usecnt);
	obj->uevent.event_file = READ_ONCE(attrs->ufile->default_async_file);
	if (obj->uevent.event_file)
		uverbs_uobject_get(&obj->uevent.event_file->uobj);

	uobj_put_obj_read(pd);
	rdma_lookup_put_uobject(&cq->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);
	uobj_finalize_uobj_create(&obj->uevent.uobject, attrs);

	resp.wq_handle = obj->uevent.uobject.id;
	resp.max_sge = wq_init_attr.max_sge;
	resp.max_wr = wq_init_attr.max_wr;
	resp.wqn = wq->wq_num;
	resp.response_length = uverbs_response_length(attrs, sizeof(resp));
	return uverbs_response(attrs, &resp, sizeof(resp));

err_put_cq:
	rdma_lookup_put_uobject(&cq->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);
err_put_pd:
	uobj_put_obj_read(pd);
err_uobj:
	uobj_alloc_abort(&obj->uevent.uobject, attrs);

	return err;
}

static int ib_uverbs_ex_destroy_wq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_ex_destroy_wq	cmd;
	struct ib_uverbs_ex_destroy_wq_resp	resp = {};
	struct ib_uobject		*uobj;
	struct ib_uwq_object		*obj;
	int				ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	if (cmd.comp_mask)
		return -EOPNOTSUPP;

	resp.response_length = uverbs_response_length(attrs, sizeof(resp));
	uobj = uobj_get_destroy(UVERBS_OBJECT_WQ, cmd.wq_handle, attrs);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	obj = container_of(uobj, struct ib_uwq_object, uevent.uobject);
	resp.events_reported = obj->uevent.events_reported;

	uobj_put_destroy(uobj);

	return uverbs_response(attrs, &resp, sizeof(resp));
}

static int ib_uverbs_ex_modify_wq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_ex_modify_wq cmd;
	struct ib_wq *wq;
	struct ib_wq_attr wq_attr = {};
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	if (!cmd.attr_mask)
		return -EINVAL;

	if (cmd.attr_mask > (IB_WQ_STATE | IB_WQ_CUR_STATE | IB_WQ_FLAGS))
		return -EINVAL;

	wq = uobj_get_obj_read(wq, UVERBS_OBJECT_WQ, cmd.wq_handle, attrs);
	if (!wq)
		return -EINVAL;

	if (cmd.attr_mask & IB_WQ_FLAGS) {
		wq_attr.flags = cmd.flags;
		wq_attr.flags_mask = cmd.flags_mask;
	}

	if (cmd.attr_mask & IB_WQ_CUR_STATE) {
		if (cmd.curr_wq_state > IB_WQS_ERR)
			return -EINVAL;

		wq_attr.curr_wq_state = cmd.curr_wq_state;
	} else {
		wq_attr.curr_wq_state = wq->state;
	}

	if (cmd.attr_mask & IB_WQ_STATE) {
		if (cmd.wq_state > IB_WQS_ERR)
			return -EINVAL;

		wq_attr.wq_state = cmd.wq_state;
	} else {
		wq_attr.wq_state = wq_attr.curr_wq_state;
	}

	ret = wq->device->ops.modify_wq(wq, &wq_attr, cmd.attr_mask,
					&attrs->driver_udata);
	rdma_lookup_put_uobject(&wq->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);
	return ret;
}

static int ib_uverbs_ex_create_rwq_ind_table(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_ex_create_rwq_ind_table cmd;
	struct ib_uverbs_ex_create_rwq_ind_table_resp  resp = {};
	struct ib_uobject *uobj;
	int err;
	struct ib_rwq_ind_table_init_attr init_attr = {};
	struct ib_rwq_ind_table *rwq_ind_tbl;
	struct ib_wq **wqs = NULL;
	u32 *wqs_handles = NULL;
	struct ib_wq	*wq = NULL;
	int i, num_read_wqs;
	u32 num_wq_handles;
	struct uverbs_req_iter iter;
	struct ib_device *ib_dev;

	err = uverbs_request_start(attrs, &iter, &cmd, sizeof(cmd));
	if (err)
		return err;

	if (cmd.comp_mask)
		return -EOPNOTSUPP;

	if (cmd.log_ind_tbl_size > IB_USER_VERBS_MAX_LOG_IND_TBL_SIZE)
		return -EINVAL;

	num_wq_handles = 1 << cmd.log_ind_tbl_size;
	wqs_handles = kcalloc(num_wq_handles, sizeof(*wqs_handles),
			      GFP_KERNEL);
	if (!wqs_handles)
		return -ENOMEM;

	err = uverbs_request_next(&iter, wqs_handles,
				  num_wq_handles * sizeof(__u32));
	if (err)
		goto err_free;

	err = uverbs_request_finish(&iter);
	if (err)
		goto err_free;

	wqs = kcalloc(num_wq_handles, sizeof(*wqs), GFP_KERNEL);
	if (!wqs) {
		err = -ENOMEM;
		goto  err_free;
	}

	for (num_read_wqs = 0; num_read_wqs < num_wq_handles;
			num_read_wqs++) {
		wq = uobj_get_obj_read(wq, UVERBS_OBJECT_WQ,
				       wqs_handles[num_read_wqs], attrs);
		if (!wq) {
			err = -EINVAL;
			goto put_wqs;
		}

		wqs[num_read_wqs] = wq;
		atomic_inc(&wqs[num_read_wqs]->usecnt);
	}

	uobj = uobj_alloc(UVERBS_OBJECT_RWQ_IND_TBL, attrs, &ib_dev);
	if (IS_ERR(uobj)) {
		err = PTR_ERR(uobj);
		goto put_wqs;
	}

	rwq_ind_tbl = rdma_zalloc_drv_obj(ib_dev, ib_rwq_ind_table);
	if (!rwq_ind_tbl) {
		err = -ENOMEM;
		goto err_uobj;
	}

	init_attr.log_ind_tbl_size = cmd.log_ind_tbl_size;
	init_attr.ind_tbl = wqs;

	rwq_ind_tbl->ind_tbl = wqs;
	rwq_ind_tbl->log_ind_tbl_size = init_attr.log_ind_tbl_size;
	rwq_ind_tbl->uobject = uobj;
	uobj->object = rwq_ind_tbl;
	rwq_ind_tbl->device = ib_dev;
	atomic_set(&rwq_ind_tbl->usecnt, 0);

	err = ib_dev->ops.create_rwq_ind_table(rwq_ind_tbl, &init_attr,
					       &attrs->driver_udata);
	if (err)
		goto err_create;

	for (i = 0; i < num_wq_handles; i++)
		rdma_lookup_put_uobject(&wqs[i]->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	kfree(wqs_handles);
	uobj_finalize_uobj_create(uobj, attrs);

	resp.ind_tbl_handle = uobj->id;
	resp.ind_tbl_num = rwq_ind_tbl->ind_tbl_num;
	resp.response_length = uverbs_response_length(attrs, sizeof(resp));
	return uverbs_response(attrs, &resp, sizeof(resp));

err_create:
	kfree(rwq_ind_tbl);
err_uobj:
	uobj_alloc_abort(uobj, attrs);
put_wqs:
	for (i = 0; i < num_read_wqs; i++) {
		rdma_lookup_put_uobject(&wqs[i]->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
		atomic_dec(&wqs[i]->usecnt);
	}
err_free:
	kfree(wqs_handles);
	kfree(wqs);
	return err;
}

static int ib_uverbs_ex_destroy_rwq_ind_table(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_ex_destroy_rwq_ind_table cmd;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	if (cmd.comp_mask)
		return -EOPNOTSUPP;

	return uobj_perform_destroy(UVERBS_OBJECT_RWQ_IND_TBL,
				    cmd.ind_tbl_handle, attrs);
}

static int ib_uverbs_ex_create_flow(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_create_flow	  cmd;
	struct ib_uverbs_create_flow_resp resp = {};
	struct ib_uobject		  *uobj;
	struct ib_flow			  *flow_id;
	struct ib_uverbs_flow_attr	  *kern_flow_attr;
	struct ib_flow_attr		  *flow_attr;
	struct ib_qp			  *qp;
	struct ib_uflow_resources	  *uflow_res;
	struct ib_uverbs_flow_spec_hdr	  *kern_spec;
	struct uverbs_req_iter iter;
	int err;
	void *ib_spec;
	int i;
	struct ib_device *ib_dev;

	err = uverbs_request_start(attrs, &iter, &cmd, sizeof(cmd));
	if (err)
		return err;

	if (cmd.comp_mask)
		return -EINVAL;

	if (!capable(CAP_NET_RAW))
		return -EPERM;

	if (cmd.flow_attr.flags >= IB_FLOW_ATTR_FLAGS_RESERVED)
		return -EINVAL;

	if ((cmd.flow_attr.flags & IB_FLOW_ATTR_FLAGS_DONT_TRAP) &&
	    ((cmd.flow_attr.type == IB_FLOW_ATTR_ALL_DEFAULT) ||
	     (cmd.flow_attr.type == IB_FLOW_ATTR_MC_DEFAULT)))
		return -EINVAL;

	if (cmd.flow_attr.num_of_specs > IB_FLOW_SPEC_SUPPORT_LAYERS)
		return -EINVAL;

	if (cmd.flow_attr.size >
	    (cmd.flow_attr.num_of_specs * sizeof(struct ib_uverbs_flow_spec)))
		return -EINVAL;

	if (cmd.flow_attr.reserved[0] ||
	    cmd.flow_attr.reserved[1])
		return -EINVAL;

	if (cmd.flow_attr.num_of_specs) {
		kern_flow_attr = kmalloc(sizeof(*kern_flow_attr) + cmd.flow_attr.size,
					 GFP_KERNEL);
		if (!kern_flow_attr)
			return -ENOMEM;

		*kern_flow_attr = cmd.flow_attr;
		err = uverbs_request_next(&iter, &kern_flow_attr->flow_specs,
					  cmd.flow_attr.size);
		if (err)
			goto err_free_attr;
	} else {
		kern_flow_attr = &cmd.flow_attr;
	}

	err = uverbs_request_finish(&iter);
	if (err)
		goto err_free_attr;

	uobj = uobj_alloc(UVERBS_OBJECT_FLOW, attrs, &ib_dev);
	if (IS_ERR(uobj)) {
		err = PTR_ERR(uobj);
		goto err_free_attr;
	}

	qp = uobj_get_obj_read(qp, UVERBS_OBJECT_QP, cmd.qp_handle, attrs);
	if (!qp) {
		err = -EINVAL;
		goto err_uobj;
	}

	if (qp->qp_type != IB_QPT_UD && qp->qp_type != IB_QPT_RAW_PACKET) {
		err = -EINVAL;
		goto err_put;
	}

	flow_attr = kzalloc(struct_size(flow_attr, flows,
				cmd.flow_attr.num_of_specs), GFP_KERNEL);
	if (!flow_attr) {
		err = -ENOMEM;
		goto err_put;
	}
	uflow_res = flow_resources_alloc(cmd.flow_attr.num_of_specs);
	if (!uflow_res) {
		err = -ENOMEM;
		goto err_free_flow_attr;
	}

	flow_attr->type = kern_flow_attr->type;
	flow_attr->priority = kern_flow_attr->priority;
	flow_attr->num_of_specs = kern_flow_attr->num_of_specs;
	flow_attr->port = kern_flow_attr->port;
	flow_attr->flags = kern_flow_attr->flags;
	flow_attr->size = sizeof(*flow_attr);

	kern_spec = kern_flow_attr->flow_specs;
	ib_spec = flow_attr + 1;
	for (i = 0; i < flow_attr->num_of_specs &&
			cmd.flow_attr.size >= sizeof(*kern_spec) &&
			cmd.flow_attr.size >= kern_spec->size;
	     i++) {
		err = kern_spec_to_ib_spec(
				attrs, (struct ib_uverbs_flow_spec *)kern_spec,
				ib_spec, uflow_res);
		if (err)
			goto err_free;

		flow_attr->size +=
			((union ib_flow_spec *) ib_spec)->size;
		cmd.flow_attr.size -= kern_spec->size;
		kern_spec = ((void *)kern_spec) + kern_spec->size;
		ib_spec += ((union ib_flow_spec *) ib_spec)->size;
	}
	if (cmd.flow_attr.size || (i != flow_attr->num_of_specs)) {
		pr_warn("create flow failed, flow %d: %d bytes left from uverb cmd\n",
			i, cmd.flow_attr.size);
		err = -EINVAL;
		goto err_free;
	}

	flow_id = qp->device->ops.create_flow(qp, flow_attr,
					      &attrs->driver_udata);

	if (IS_ERR(flow_id)) {
		err = PTR_ERR(flow_id);
		goto err_free;
	}

	ib_set_flow(uobj, flow_id, qp, qp->device, uflow_res);

	rdma_lookup_put_uobject(&qp->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);
	kfree(flow_attr);

	if (cmd.flow_attr.num_of_specs)
		kfree(kern_flow_attr);
	uobj_finalize_uobj_create(uobj, attrs);

	resp.flow_handle = uobj->id;
	return uverbs_response(attrs, &resp, sizeof(resp));

err_free:
	ib_uverbs_flow_resources_free(uflow_res);
err_free_flow_attr:
	kfree(flow_attr);
err_put:
	rdma_lookup_put_uobject(&qp->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);
err_uobj:
	uobj_alloc_abort(uobj, attrs);
err_free_attr:
	if (cmd.flow_attr.num_of_specs)
		kfree(kern_flow_attr);
	return err;
}

static int ib_uverbs_ex_destroy_flow(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_destroy_flow	cmd;
	int				ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	if (cmd.comp_mask)
		return -EINVAL;

	return uobj_perform_destroy(UVERBS_OBJECT_FLOW, cmd.flow_handle, attrs);
}

static int __uverbs_create_xsrq(struct uverbs_attr_bundle *attrs,
				struct ib_uverbs_create_xsrq *cmd,
				struct ib_udata *udata)
{
	struct ib_uverbs_create_srq_resp resp = {};
	struct ib_usrq_object           *obj;
	struct ib_pd                    *pd;
	struct ib_srq                   *srq;
	struct ib_srq_init_attr          attr;
	int ret;
	struct ib_uobject *xrcd_uobj;
	struct ib_device *ib_dev;

	obj = (struct ib_usrq_object *)uobj_alloc(UVERBS_OBJECT_SRQ, attrs,
						  &ib_dev);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	if (cmd->srq_type == IB_SRQT_TM)
		attr.ext.tag_matching.max_num_tags = cmd->max_num_tags;

	if (cmd->srq_type == IB_SRQT_XRC) {
		xrcd_uobj = uobj_get_read(UVERBS_OBJECT_XRCD, cmd->xrcd_handle,
					  attrs);
		if (IS_ERR(xrcd_uobj)) {
			ret = -EINVAL;
			goto err;
		}

		attr.ext.xrc.xrcd = (struct ib_xrcd *)xrcd_uobj->object;
		if (!attr.ext.xrc.xrcd) {
			ret = -EINVAL;
			goto err_put_xrcd;
		}

		obj->uxrcd = container_of(xrcd_uobj, struct ib_uxrcd_object, uobject);
		atomic_inc(&obj->uxrcd->refcnt);
	}

	if (ib_srq_has_cq(cmd->srq_type)) {
		attr.ext.cq = uobj_get_obj_read(cq, UVERBS_OBJECT_CQ,
						cmd->cq_handle, attrs);
		if (!attr.ext.cq) {
			ret = -EINVAL;
			goto err_put_xrcd;
		}
	}

	pd = uobj_get_obj_read(pd, UVERBS_OBJECT_PD, cmd->pd_handle, attrs);
	if (!pd) {
		ret = -EINVAL;
		goto err_put_cq;
	}

	attr.event_handler  = ib_uverbs_srq_event_handler;
	attr.srq_type       = cmd->srq_type;
	attr.attr.max_wr    = cmd->max_wr;
	attr.attr.max_sge   = cmd->max_sge;
	attr.attr.srq_limit = cmd->srq_limit;

	INIT_LIST_HEAD(&obj->uevent.event_list);
	obj->uevent.uobject.user_handle = cmd->user_handle;

	srq = ib_create_srq_user(pd, &attr, obj, udata);
	if (IS_ERR(srq)) {
		ret = PTR_ERR(srq);
		goto err_put_pd;
	}

	obj->uevent.uobject.object = srq;
	obj->uevent.uobject.user_handle = cmd->user_handle;
	obj->uevent.event_file = READ_ONCE(attrs->ufile->default_async_file);
	if (obj->uevent.event_file)
		uverbs_uobject_get(&obj->uevent.event_file->uobj);

	if (cmd->srq_type == IB_SRQT_XRC)
		resp.srqn = srq->ext.xrc.srq_num;

	if (cmd->srq_type == IB_SRQT_XRC)
		uobj_put_read(xrcd_uobj);

	if (ib_srq_has_cq(cmd->srq_type))
		rdma_lookup_put_uobject(&attr.ext.cq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);

	uobj_put_obj_read(pd);
	uobj_finalize_uobj_create(&obj->uevent.uobject, attrs);

	resp.srq_handle = obj->uevent.uobject.id;
	resp.max_wr = attr.attr.max_wr;
	resp.max_sge = attr.attr.max_sge;
	return uverbs_response(attrs, &resp, sizeof(resp));

err_put_pd:
	uobj_put_obj_read(pd);
err_put_cq:
	if (ib_srq_has_cq(cmd->srq_type))
		rdma_lookup_put_uobject(&attr.ext.cq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);

err_put_xrcd:
	if (cmd->srq_type == IB_SRQT_XRC) {
		atomic_dec(&obj->uxrcd->refcnt);
		uobj_put_read(xrcd_uobj);
	}

err:
	uobj_alloc_abort(&obj->uevent.uobject, attrs);
	return ret;
}

static int ib_uverbs_create_srq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_create_srq      cmd;
	struct ib_uverbs_create_xsrq     xcmd;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	memset(&xcmd, 0, sizeof(xcmd));
	xcmd.response	 = cmd.response;
	xcmd.user_handle = cmd.user_handle;
	xcmd.srq_type	 = IB_SRQT_BASIC;
	xcmd.pd_handle	 = cmd.pd_handle;
	xcmd.max_wr	 = cmd.max_wr;
	xcmd.max_sge	 = cmd.max_sge;
	xcmd.srq_limit	 = cmd.srq_limit;

	return __uverbs_create_xsrq(attrs, &xcmd, &attrs->driver_udata);
}

static int ib_uverbs_create_xsrq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_create_xsrq     cmd;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	return __uverbs_create_xsrq(attrs, &cmd, &attrs->driver_udata);
}

static int ib_uverbs_modify_srq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_modify_srq cmd;
	struct ib_srq              *srq;
	struct ib_srq_attr          attr;
	int                         ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	srq = uobj_get_obj_read(srq, UVERBS_OBJECT_SRQ, cmd.srq_handle, attrs);
	if (!srq)
		return -EINVAL;

	attr.max_wr    = cmd.max_wr;
	attr.srq_limit = cmd.srq_limit;

	ret = srq->device->ops.modify_srq(srq, &attr, cmd.attr_mask,
					  &attrs->driver_udata);

	rdma_lookup_put_uobject(&srq->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);

	return ret;
}

static int ib_uverbs_query_srq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_query_srq      cmd;
	struct ib_uverbs_query_srq_resp resp;
	struct ib_srq_attr              attr;
	struct ib_srq                   *srq;
	int                             ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	srq = uobj_get_obj_read(srq, UVERBS_OBJECT_SRQ, cmd.srq_handle, attrs);
	if (!srq)
		return -EINVAL;

	ret = ib_query_srq(srq, &attr);

	rdma_lookup_put_uobject(&srq->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);

	if (ret)
		return ret;

	memset(&resp, 0, sizeof resp);

	resp.max_wr    = attr.max_wr;
	resp.max_sge   = attr.max_sge;
	resp.srq_limit = attr.srq_limit;

	return uverbs_response(attrs, &resp, sizeof(resp));
}

static int ib_uverbs_destroy_srq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_destroy_srq      cmd;
	struct ib_uverbs_destroy_srq_resp resp;
	struct ib_uobject		 *uobj;
	struct ib_uevent_object        	 *obj;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	uobj = uobj_get_destroy(UVERBS_OBJECT_SRQ, cmd.srq_handle, attrs);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	obj = container_of(uobj, struct ib_uevent_object, uobject);
	memset(&resp, 0, sizeof(resp));
	resp.events_reported = obj->events_reported;

	uobj_put_destroy(uobj);

	return uverbs_response(attrs, &resp, sizeof(resp));
}

static int ib_uverbs_ex_query_device(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_ex_query_device_resp resp = {};
	struct ib_uverbs_ex_query_device  cmd;
	struct ib_device_attr attr = {0};
	struct ib_ucontext *ucontext;
	struct ib_device *ib_dev;
	int err;

	ucontext = ib_uverbs_get_ucontext(attrs);
	if (IS_ERR(ucontext))
		return PTR_ERR(ucontext);
	ib_dev = ucontext->device;

	err = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (err)
		return err;

	if (cmd.comp_mask)
		return -EINVAL;

	if (cmd.reserved)
		return -EINVAL;

	err = ib_dev->ops.query_device(ib_dev, &attr, &attrs->driver_udata);
	if (err)
		return err;

	copy_query_dev_fields(ucontext, &resp.base, &attr);

	resp.odp_caps.general_caps = attr.odp_caps.general_caps;
	resp.odp_caps.per_transport_caps.rc_odp_caps =
		attr.odp_caps.per_transport_caps.rc_odp_caps;
	resp.odp_caps.per_transport_caps.uc_odp_caps =
		attr.odp_caps.per_transport_caps.uc_odp_caps;
	resp.odp_caps.per_transport_caps.ud_odp_caps =
		attr.odp_caps.per_transport_caps.ud_odp_caps;
	resp.xrc_odp_caps = attr.odp_caps.per_transport_caps.xrc_odp_caps;

	resp.timestamp_mask = attr.timestamp_mask;
	resp.hca_core_clock = attr.hca_core_clock;
	resp.device_cap_flags_ex = attr.device_cap_flags;
	resp.rss_caps.supported_qpts = attr.rss_caps.supported_qpts;
	resp.rss_caps.max_rwq_indirection_tables =
		attr.rss_caps.max_rwq_indirection_tables;
	resp.rss_caps.max_rwq_indirection_table_size =
		attr.rss_caps.max_rwq_indirection_table_size;
	resp.max_wq_type_rq = attr.max_wq_type_rq;
	resp.raw_packet_caps = attr.raw_packet_caps;
	resp.tm_caps.max_rndv_hdr_size	= attr.tm_caps.max_rndv_hdr_size;
	resp.tm_caps.max_num_tags	= attr.tm_caps.max_num_tags;
	resp.tm_caps.max_ops		= attr.tm_caps.max_ops;
	resp.tm_caps.max_sge		= attr.tm_caps.max_sge;
	resp.tm_caps.flags		= attr.tm_caps.flags;
	resp.cq_moderation_caps.max_cq_moderation_count  =
		attr.cq_caps.max_cq_moderation_count;
	resp.cq_moderation_caps.max_cq_moderation_period =
		attr.cq_caps.max_cq_moderation_period;
	resp.max_dm_size = attr.max_dm_size;
	resp.response_length = uverbs_response_length(attrs, sizeof(resp));

	return uverbs_response(attrs, &resp, sizeof(resp));
}

static int ib_uverbs_ex_modify_cq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_ex_modify_cq cmd;
	struct ib_cq *cq;
	int ret;

	ret = uverbs_request(attrs, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	if (!cmd.attr_mask || cmd.reserved)
		return -EINVAL;

	if (cmd.attr_mask > IB_CQ_MODERATE)
		return -EOPNOTSUPP;

	cq = uobj_get_obj_read(cq, UVERBS_OBJECT_CQ, cmd.cq_handle, attrs);
	if (!cq)
		return -EINVAL;

	ret = rdma_set_cq_moderation(cq, cmd.attr.cq_count, cmd.attr.cq_period);

	rdma_lookup_put_uobject(&cq->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);
	return ret;
}

/*
 * Describe the input structs for write(). Some write methods have an input
 * only struct, most have an input and output. If the struct has an output then
 * the 'response' u64 must be the first field in the request structure.
 *
 * If udata is present then both the request and response structs have a
 * trailing driver_data flex array. In this case the size of the base struct
 * cannot be changed.
 */
#define UAPI_DEF_WRITE_IO(req, resp)                                           \
	.write.has_resp = 1 +                                                  \
			  BUILD_BUG_ON_ZERO(offsetof(req, response) != 0) +    \
			  BUILD_BUG_ON_ZERO(sizeof_field(req, response) !=    \
					    sizeof(u64)),                      \
	.write.req_size = sizeof(req), .write.resp_size = sizeof(resp)

#define UAPI_DEF_WRITE_I(req) .write.req_size = sizeof(req)

#define UAPI_DEF_WRITE_UDATA_IO(req, resp)                                     \
	UAPI_DEF_WRITE_IO(req, resp),                                          \
		.write.has_udata =                                             \
			1 +                                                    \
			BUILD_BUG_ON_ZERO(offsetof(req, driver_data) !=        \
					  sizeof(req)) +                       \
			BUILD_BUG_ON_ZERO(offsetof(resp, driver_data) !=       \
					  sizeof(resp))

#define UAPI_DEF_WRITE_UDATA_I(req)                                            \
	UAPI_DEF_WRITE_I(req),                                                 \
		.write.has_udata =                                             \
			1 + BUILD_BUG_ON_ZERO(offsetof(req, driver_data) !=    \
					      sizeof(req))

/*
 * The _EX versions are for use with WRITE_EX and allow the last struct member
 * to be specified. Buffers that do not include that member will be rejected.
 */
#define UAPI_DEF_WRITE_IO_EX(req, req_last_member, resp, resp_last_member)     \
	.write.has_resp = 1,                                                   \
	.write.req_size = offsetofend(req, req_last_member),                   \
	.write.resp_size = offsetofend(resp, resp_last_member)

#define UAPI_DEF_WRITE_I_EX(req, req_last_member)                              \
	.write.req_size = offsetofend(req, req_last_member)

const struct uapi_definition uverbs_def_write_intf[] = {
	DECLARE_UVERBS_OBJECT(
		UVERBS_OBJECT_AH,
		DECLARE_UVERBS_WRITE(IB_USER_VERBS_CMD_CREATE_AH,
				     ib_uverbs_create_ah,
				     UAPI_DEF_WRITE_UDATA_IO(
					     struct ib_uverbs_create_ah,
					     struct ib_uverbs_create_ah_resp),
				     UAPI_DEF_METHOD_NEEDS_FN(create_ah)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_DESTROY_AH,
			ib_uverbs_destroy_ah,
			UAPI_DEF_WRITE_I(struct ib_uverbs_destroy_ah),
			UAPI_DEF_METHOD_NEEDS_FN(destroy_ah))),

	DECLARE_UVERBS_OBJECT(
		UVERBS_OBJECT_COMP_CHANNEL,
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL,
			ib_uverbs_create_comp_channel,
			UAPI_DEF_WRITE_IO(
				struct ib_uverbs_create_comp_channel,
				struct ib_uverbs_create_comp_channel_resp))),

	DECLARE_UVERBS_OBJECT(
		UVERBS_OBJECT_CQ,
		DECLARE_UVERBS_WRITE(IB_USER_VERBS_CMD_CREATE_CQ,
				     ib_uverbs_create_cq,
				     UAPI_DEF_WRITE_UDATA_IO(
					     struct ib_uverbs_create_cq,
					     struct ib_uverbs_create_cq_resp),
				     UAPI_DEF_METHOD_NEEDS_FN(create_cq)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_DESTROY_CQ,
			ib_uverbs_destroy_cq,
			UAPI_DEF_WRITE_IO(struct ib_uverbs_destroy_cq,
					  struct ib_uverbs_destroy_cq_resp),
			UAPI_DEF_METHOD_NEEDS_FN(destroy_cq)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_POLL_CQ,
			ib_uverbs_poll_cq,
			UAPI_DEF_WRITE_IO(struct ib_uverbs_poll_cq,
					  struct ib_uverbs_poll_cq_resp),
			UAPI_DEF_METHOD_NEEDS_FN(poll_cq)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_REQ_NOTIFY_CQ,
			ib_uverbs_req_notify_cq,
			UAPI_DEF_WRITE_I(struct ib_uverbs_req_notify_cq),
			UAPI_DEF_METHOD_NEEDS_FN(req_notify_cq)),
		DECLARE_UVERBS_WRITE(IB_USER_VERBS_CMD_RESIZE_CQ,
				     ib_uverbs_resize_cq,
				     UAPI_DEF_WRITE_UDATA_IO(
					     struct ib_uverbs_resize_cq,
					     struct ib_uverbs_resize_cq_resp),
				     UAPI_DEF_METHOD_NEEDS_FN(resize_cq)),
		DECLARE_UVERBS_WRITE_EX(
			IB_USER_VERBS_EX_CMD_CREATE_CQ,
			ib_uverbs_ex_create_cq,
			UAPI_DEF_WRITE_IO_EX(struct ib_uverbs_ex_create_cq,
					     reserved,
					     struct ib_uverbs_ex_create_cq_resp,
					     response_length),
			UAPI_DEF_METHOD_NEEDS_FN(create_cq)),
		DECLARE_UVERBS_WRITE_EX(
			IB_USER_VERBS_EX_CMD_MODIFY_CQ,
			ib_uverbs_ex_modify_cq,
			UAPI_DEF_WRITE_I(struct ib_uverbs_ex_modify_cq),
			UAPI_DEF_METHOD_NEEDS_FN(create_cq))),

	DECLARE_UVERBS_OBJECT(
		UVERBS_OBJECT_DEVICE,
		DECLARE_UVERBS_WRITE(IB_USER_VERBS_CMD_GET_CONTEXT,
				     ib_uverbs_get_context,
				     UAPI_DEF_WRITE_UDATA_IO(
					     struct ib_uverbs_get_context,
					     struct ib_uverbs_get_context_resp)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_QUERY_DEVICE,
			ib_uverbs_query_device,
			UAPI_DEF_WRITE_IO(struct ib_uverbs_query_device,
					  struct ib_uverbs_query_device_resp)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_QUERY_PORT,
			ib_uverbs_query_port,
			UAPI_DEF_WRITE_IO(struct ib_uverbs_query_port,
					  struct ib_uverbs_query_port_resp),
			UAPI_DEF_METHOD_NEEDS_FN(query_port)),
		DECLARE_UVERBS_WRITE_EX(
			IB_USER_VERBS_EX_CMD_QUERY_DEVICE,
			ib_uverbs_ex_query_device,
			UAPI_DEF_WRITE_IO_EX(
				struct ib_uverbs_ex_query_device,
				reserved,
				struct ib_uverbs_ex_query_device_resp,
				response_length),
			UAPI_DEF_METHOD_NEEDS_FN(query_device)),
		UAPI_DEF_OBJ_NEEDS_FN(alloc_ucontext),
		UAPI_DEF_OBJ_NEEDS_FN(dealloc_ucontext)),

	DECLARE_UVERBS_OBJECT(
		UVERBS_OBJECT_FLOW,
		DECLARE_UVERBS_WRITE_EX(
			IB_USER_VERBS_EX_CMD_CREATE_FLOW,
			ib_uverbs_ex_create_flow,
			UAPI_DEF_WRITE_IO_EX(struct ib_uverbs_create_flow,
					     flow_attr,
					     struct ib_uverbs_create_flow_resp,
					     flow_handle),
			UAPI_DEF_METHOD_NEEDS_FN(create_flow)),
		DECLARE_UVERBS_WRITE_EX(
			IB_USER_VERBS_EX_CMD_DESTROY_FLOW,
			ib_uverbs_ex_destroy_flow,
			UAPI_DEF_WRITE_I(struct ib_uverbs_destroy_flow),
			UAPI_DEF_METHOD_NEEDS_FN(destroy_flow))),

	DECLARE_UVERBS_OBJECT(
		UVERBS_OBJECT_MR,
		DECLARE_UVERBS_WRITE(IB_USER_VERBS_CMD_DEREG_MR,
				     ib_uverbs_dereg_mr,
				     UAPI_DEF_WRITE_I(struct ib_uverbs_dereg_mr),
				     UAPI_DEF_METHOD_NEEDS_FN(dereg_mr)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_REG_MR,
			ib_uverbs_reg_mr,
			UAPI_DEF_WRITE_UDATA_IO(struct ib_uverbs_reg_mr,
						struct ib_uverbs_reg_mr_resp),
			UAPI_DEF_METHOD_NEEDS_FN(reg_user_mr)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_REREG_MR,
			ib_uverbs_rereg_mr,
			UAPI_DEF_WRITE_UDATA_IO(struct ib_uverbs_rereg_mr,
						struct ib_uverbs_rereg_mr_resp),
			UAPI_DEF_METHOD_NEEDS_FN(rereg_user_mr))),

	DECLARE_UVERBS_OBJECT(
		UVERBS_OBJECT_MW,
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_ALLOC_MW,
			ib_uverbs_alloc_mw,
			UAPI_DEF_WRITE_UDATA_IO(struct ib_uverbs_alloc_mw,
						struct ib_uverbs_alloc_mw_resp),
			UAPI_DEF_METHOD_NEEDS_FN(alloc_mw)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_DEALLOC_MW,
			ib_uverbs_dealloc_mw,
			UAPI_DEF_WRITE_I(struct ib_uverbs_dealloc_mw),
			UAPI_DEF_METHOD_NEEDS_FN(dealloc_mw))),

	DECLARE_UVERBS_OBJECT(
		UVERBS_OBJECT_PD,
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_ALLOC_PD,
			ib_uverbs_alloc_pd,
			UAPI_DEF_WRITE_UDATA_IO(struct ib_uverbs_alloc_pd,
						struct ib_uverbs_alloc_pd_resp),
			UAPI_DEF_METHOD_NEEDS_FN(alloc_pd)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_DEALLOC_PD,
			ib_uverbs_dealloc_pd,
			UAPI_DEF_WRITE_I(struct ib_uverbs_dealloc_pd),
			UAPI_DEF_METHOD_NEEDS_FN(dealloc_pd))),

	DECLARE_UVERBS_OBJECT(
		UVERBS_OBJECT_QP,
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_ATTACH_MCAST,
			ib_uverbs_attach_mcast,
			UAPI_DEF_WRITE_I(struct ib_uverbs_attach_mcast),
			UAPI_DEF_METHOD_NEEDS_FN(attach_mcast),
			UAPI_DEF_METHOD_NEEDS_FN(detach_mcast)),
		DECLARE_UVERBS_WRITE(IB_USER_VERBS_CMD_CREATE_QP,
				     ib_uverbs_create_qp,
				     UAPI_DEF_WRITE_UDATA_IO(
					     struct ib_uverbs_create_qp,
					     struct ib_uverbs_create_qp_resp),
				     UAPI_DEF_METHOD_NEEDS_FN(create_qp)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_DESTROY_QP,
			ib_uverbs_destroy_qp,
			UAPI_DEF_WRITE_IO(struct ib_uverbs_destroy_qp,
					  struct ib_uverbs_destroy_qp_resp),
			UAPI_DEF_METHOD_NEEDS_FN(destroy_qp)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_DETACH_MCAST,
			ib_uverbs_detach_mcast,
			UAPI_DEF_WRITE_I(struct ib_uverbs_detach_mcast),
			UAPI_DEF_METHOD_NEEDS_FN(detach_mcast)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_MODIFY_QP,
			ib_uverbs_modify_qp,
			UAPI_DEF_WRITE_I(struct ib_uverbs_modify_qp),
			UAPI_DEF_METHOD_NEEDS_FN(modify_qp)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_POST_RECV,
			ib_uverbs_post_recv,
			UAPI_DEF_WRITE_IO(struct ib_uverbs_post_recv,
					  struct ib_uverbs_post_recv_resp),
			UAPI_DEF_METHOD_NEEDS_FN(post_recv)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_POST_SEND,
			ib_uverbs_post_send,
			UAPI_DEF_WRITE_IO(struct ib_uverbs_post_send,
					  struct ib_uverbs_post_send_resp),
			UAPI_DEF_METHOD_NEEDS_FN(post_send)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_QUERY_QP,
			ib_uverbs_query_qp,
			UAPI_DEF_WRITE_IO(struct ib_uverbs_query_qp,
					  struct ib_uverbs_query_qp_resp),
			UAPI_DEF_METHOD_NEEDS_FN(query_qp)),
		DECLARE_UVERBS_WRITE_EX(
			IB_USER_VERBS_EX_CMD_CREATE_QP,
			ib_uverbs_ex_create_qp,
			UAPI_DEF_WRITE_IO_EX(struct ib_uverbs_ex_create_qp,
					     comp_mask,
					     struct ib_uverbs_ex_create_qp_resp,
					     response_length),
			UAPI_DEF_METHOD_NEEDS_FN(create_qp)),
		DECLARE_UVERBS_WRITE_EX(
			IB_USER_VERBS_EX_CMD_MODIFY_QP,
			ib_uverbs_ex_modify_qp,
			UAPI_DEF_WRITE_IO_EX(struct ib_uverbs_ex_modify_qp,
					     base,
					     struct ib_uverbs_ex_modify_qp_resp,
					     response_length),
			UAPI_DEF_METHOD_NEEDS_FN(modify_qp))),

	DECLARE_UVERBS_OBJECT(
		UVERBS_OBJECT_RWQ_IND_TBL,
		DECLARE_UVERBS_WRITE_EX(
			IB_USER_VERBS_EX_CMD_CREATE_RWQ_IND_TBL,
			ib_uverbs_ex_create_rwq_ind_table,
			UAPI_DEF_WRITE_IO_EX(
				struct ib_uverbs_ex_create_rwq_ind_table,
				log_ind_tbl_size,
				struct ib_uverbs_ex_create_rwq_ind_table_resp,
				ind_tbl_num),
			UAPI_DEF_METHOD_NEEDS_FN(create_rwq_ind_table)),
		DECLARE_UVERBS_WRITE_EX(
			IB_USER_VERBS_EX_CMD_DESTROY_RWQ_IND_TBL,
			ib_uverbs_ex_destroy_rwq_ind_table,
			UAPI_DEF_WRITE_I(
				struct ib_uverbs_ex_destroy_rwq_ind_table),
			UAPI_DEF_METHOD_NEEDS_FN(destroy_rwq_ind_table))),

	DECLARE_UVERBS_OBJECT(
		UVERBS_OBJECT_WQ,
		DECLARE_UVERBS_WRITE_EX(
			IB_USER_VERBS_EX_CMD_CREATE_WQ,
			ib_uverbs_ex_create_wq,
			UAPI_DEF_WRITE_IO_EX(struct ib_uverbs_ex_create_wq,
					     max_sge,
					     struct ib_uverbs_ex_create_wq_resp,
					     wqn),
			UAPI_DEF_METHOD_NEEDS_FN(create_wq)),
		DECLARE_UVERBS_WRITE_EX(
			IB_USER_VERBS_EX_CMD_DESTROY_WQ,
			ib_uverbs_ex_destroy_wq,
			UAPI_DEF_WRITE_IO_EX(struct ib_uverbs_ex_destroy_wq,
					     wq_handle,
					     struct ib_uverbs_ex_destroy_wq_resp,
					     reserved),
			UAPI_DEF_METHOD_NEEDS_FN(destroy_wq)),
		DECLARE_UVERBS_WRITE_EX(
			IB_USER_VERBS_EX_CMD_MODIFY_WQ,
			ib_uverbs_ex_modify_wq,
			UAPI_DEF_WRITE_I_EX(struct ib_uverbs_ex_modify_wq,
					    curr_wq_state),
			UAPI_DEF_METHOD_NEEDS_FN(modify_wq))),

	DECLARE_UVERBS_OBJECT(
		UVERBS_OBJECT_SRQ,
		DECLARE_UVERBS_WRITE(IB_USER_VERBS_CMD_CREATE_SRQ,
				     ib_uverbs_create_srq,
				     UAPI_DEF_WRITE_UDATA_IO(
					     struct ib_uverbs_create_srq,
					     struct ib_uverbs_create_srq_resp),
				     UAPI_DEF_METHOD_NEEDS_FN(create_srq)),
		DECLARE_UVERBS_WRITE(IB_USER_VERBS_CMD_CREATE_XSRQ,
				     ib_uverbs_create_xsrq,
				     UAPI_DEF_WRITE_UDATA_IO(
					     struct ib_uverbs_create_xsrq,
					     struct ib_uverbs_create_srq_resp),
				     UAPI_DEF_METHOD_NEEDS_FN(create_srq)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_DESTROY_SRQ,
			ib_uverbs_destroy_srq,
			UAPI_DEF_WRITE_IO(struct ib_uverbs_destroy_srq,
					  struct ib_uverbs_destroy_srq_resp),
			UAPI_DEF_METHOD_NEEDS_FN(destroy_srq)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_MODIFY_SRQ,
			ib_uverbs_modify_srq,
			UAPI_DEF_WRITE_UDATA_I(struct ib_uverbs_modify_srq),
			UAPI_DEF_METHOD_NEEDS_FN(modify_srq)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_POST_SRQ_RECV,
			ib_uverbs_post_srq_recv,
			UAPI_DEF_WRITE_IO(struct ib_uverbs_post_srq_recv,
					  struct ib_uverbs_post_srq_recv_resp),
			UAPI_DEF_METHOD_NEEDS_FN(post_srq_recv)),
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_QUERY_SRQ,
			ib_uverbs_query_srq,
			UAPI_DEF_WRITE_IO(struct ib_uverbs_query_srq,
					  struct ib_uverbs_query_srq_resp),
			UAPI_DEF_METHOD_NEEDS_FN(query_srq))),

	DECLARE_UVERBS_OBJECT(
		UVERBS_OBJECT_XRCD,
		DECLARE_UVERBS_WRITE(
			IB_USER_VERBS_CMD_CLOSE_XRCD,
			ib_uverbs_close_xrcd,
			UAPI_DEF_WRITE_I(struct ib_uverbs_close_xrcd),
			UAPI_DEF_METHOD_NEEDS_FN(dealloc_xrcd)),
		DECLARE_UVERBS_WRITE(IB_USER_VERBS_CMD_OPEN_QP,
				     ib_uverbs_open_qp,
				     UAPI_DEF_WRITE_UDATA_IO(
					     struct ib_uverbs_open_qp,
					     struct ib_uverbs_create_qp_resp)),
		DECLARE_UVERBS_WRITE(IB_USER_VERBS_CMD_OPEN_XRCD,
				     ib_uverbs_open_xrcd,
				     UAPI_DEF_WRITE_UDATA_IO(
					     struct ib_uverbs_open_xrcd,
					     struct ib_uverbs_open_xrcd_resp),
				     UAPI_DEF_METHOD_NEEDS_FN(alloc_xrcd))),

	{},
};
