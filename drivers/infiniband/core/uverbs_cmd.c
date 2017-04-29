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

static struct ib_uverbs_completion_event_file *
ib_uverbs_lookup_comp_file(int fd, struct ib_ucontext *context)
{
	struct ib_uobject *uobj = uobj_get_read(uobj_get_type(comp_channel),
						fd, context);
	struct ib_uobject_file *uobj_file;

	if (IS_ERR(uobj))
		return (void *)uobj;

	uverbs_uobject_get(uobj);
	uobj_put_read(uobj);

	uobj_file = container_of(uobj, struct ib_uobject_file, uobj);
	return container_of(uobj_file, struct ib_uverbs_completion_event_file,
			    uobj_file);
}

ssize_t ib_uverbs_get_context(struct ib_uverbs_file *file,
			      struct ib_device *ib_dev,
			      const char __user *buf,
			      int in_len, int out_len)
{
	struct ib_uverbs_get_context      cmd;
	struct ib_uverbs_get_context_resp resp;
	struct ib_udata                   udata;
	struct ib_ucontext		 *ucontext;
	struct file			 *filp;
	struct ib_rdmacg_object		 cg_obj;
	int ret;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	mutex_lock(&file->mutex);

	if (file->ucontext) {
		ret = -EINVAL;
		goto err;
	}

	INIT_UDATA(&udata, buf + sizeof cmd,
		   (unsigned long) cmd.response + sizeof resp,
		   in_len - sizeof cmd, out_len - sizeof resp);

	ret = ib_rdmacg_try_charge(&cg_obj, ib_dev, RDMACG_RESOURCE_HCA_HANDLE);
	if (ret)
		goto err;

	ucontext = ib_dev->alloc_ucontext(ib_dev, &udata);
	if (IS_ERR(ucontext)) {
		ret = PTR_ERR(ucontext);
		goto err_alloc;
	}

	ucontext->device = ib_dev;
	ucontext->cg_obj = cg_obj;
	/* ufile is required when some objects are released */
	ucontext->ufile = file;
	uverbs_initialize_ucontext(ucontext);

	rcu_read_lock();
	ucontext->tgid = get_task_pid(current->group_leader, PIDTYPE_PID);
	rcu_read_unlock();
	ucontext->closing = 0;

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	ucontext->umem_tree = RB_ROOT;
	init_rwsem(&ucontext->umem_rwsem);
	ucontext->odp_mrs_count = 0;
	INIT_LIST_HEAD(&ucontext->no_private_counters);

	if (!(ib_dev->attrs.device_cap_flags & IB_DEVICE_ON_DEMAND_PAGING))
		ucontext->invalidate_range = NULL;

#endif

	resp.num_comp_vectors = file->device->num_comp_vectors;

	ret = get_unused_fd_flags(O_CLOEXEC);
	if (ret < 0)
		goto err_free;
	resp.async_fd = ret;

	filp = ib_uverbs_alloc_async_event_file(file, ib_dev);
	if (IS_ERR(filp)) {
		ret = PTR_ERR(filp);
		goto err_fd;
	}

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		ret = -EFAULT;
		goto err_file;
	}

	file->ucontext = ucontext;

	fd_install(resp.async_fd, filp);

	mutex_unlock(&file->mutex);

	return in_len;

err_file:
	ib_uverbs_free_async_event_file(file);
	fput(filp);

err_fd:
	put_unused_fd(resp.async_fd);

err_free:
	put_pid(ucontext->tgid);
	ib_dev->dealloc_ucontext(ucontext);

err_alloc:
	ib_rdmacg_uncharge(&cg_obj, ib_dev, RDMACG_RESOURCE_HCA_HANDLE);

err:
	mutex_unlock(&file->mutex);
	return ret;
}

static void copy_query_dev_fields(struct ib_uverbs_file *file,
				  struct ib_device *ib_dev,
				  struct ib_uverbs_query_device_resp *resp,
				  struct ib_device_attr *attr)
{
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
	resp->max_sge		= attr->max_sge;
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
	resp->max_fmr			= attr->max_fmr;
	resp->max_map_per_fmr		= attr->max_map_per_fmr;
	resp->max_srq			= attr->max_srq;
	resp->max_srq_wr		= attr->max_srq_wr;
	resp->max_srq_sge		= attr->max_srq_sge;
	resp->max_pkeys			= attr->max_pkeys;
	resp->local_ca_ack_delay	= attr->local_ca_ack_delay;
	resp->phys_port_cnt		= ib_dev->phys_port_cnt;
}

ssize_t ib_uverbs_query_device(struct ib_uverbs_file *file,
			       struct ib_device *ib_dev,
			       const char __user *buf,
			       int in_len, int out_len)
{
	struct ib_uverbs_query_device      cmd;
	struct ib_uverbs_query_device_resp resp;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	memset(&resp, 0, sizeof resp);
	copy_query_dev_fields(file, ib_dev, &resp, &ib_dev->attrs);

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		return -EFAULT;

	return in_len;
}

ssize_t ib_uverbs_query_port(struct ib_uverbs_file *file,
			     struct ib_device *ib_dev,
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

	ret = ib_query_port(ib_dev, cmd.port_num, &attr);
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
	resp.link_layer      = rdma_port_get_link_layer(ib_dev,
							cmd.port_num);

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		return -EFAULT;

	return in_len;
}

ssize_t ib_uverbs_alloc_pd(struct ib_uverbs_file *file,
			   struct ib_device *ib_dev,
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

	uobj  = uobj_alloc(uobj_get_type(pd), file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	pd = ib_dev->alloc_pd(ib_dev, file->ucontext, &udata);
	if (IS_ERR(pd)) {
		ret = PTR_ERR(pd);
		goto err;
	}

	pd->device  = ib_dev;
	pd->uobject = uobj;
	pd->__internal_mr = NULL;
	atomic_set(&pd->usecnt, 0);

	uobj->object = pd;
	memset(&resp, 0, sizeof resp);
	resp.pd_handle = uobj->id;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		ret = -EFAULT;
		goto err_copy;
	}

	uobj_alloc_commit(uobj);

	return in_len;

err_copy:
	ib_dealloc_pd(pd);

err:
	uobj_alloc_abort(uobj);
	return ret;
}

ssize_t ib_uverbs_dealloc_pd(struct ib_uverbs_file *file,
			     struct ib_device *ib_dev,
			     const char __user *buf,
			     int in_len, int out_len)
{
	struct ib_uverbs_dealloc_pd cmd;
	struct ib_uobject          *uobj;
	int                         ret;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	uobj  = uobj_get_write(uobj_get_type(pd), cmd.pd_handle,
			       file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	ret = uobj_remove_commit(uobj);

	return ret ?: in_len;
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

ssize_t ib_uverbs_open_xrcd(struct ib_uverbs_file *file,
			    struct ib_device *ib_dev,
			    const char __user *buf, int in_len,
			    int out_len)
{
	struct ib_uverbs_open_xrcd	cmd;
	struct ib_uverbs_open_xrcd_resp	resp;
	struct ib_udata			udata;
	struct ib_uxrcd_object         *obj;
	struct ib_xrcd                 *xrcd = NULL;
	struct fd			f = {NULL, 0};
	struct inode                   *inode = NULL;
	int				ret = 0;
	int				new_xrcd = 0;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	INIT_UDATA(&udata, buf + sizeof cmd,
		   (unsigned long) cmd.response + sizeof resp,
		   in_len - sizeof cmd, out_len - sizeof  resp);

	mutex_lock(&file->device->xrcd_tree_mutex);

	if (cmd.fd != -1) {
		/* search for file descriptor */
		f = fdget(cmd.fd);
		if (!f.file) {
			ret = -EBADF;
			goto err_tree_mutex_unlock;
		}

		inode = file_inode(f.file);
		xrcd = find_xrcd(file->device, inode);
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

	obj  = (struct ib_uxrcd_object *)uobj_alloc(uobj_get_type(xrcd),
						    file->ucontext);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto err_tree_mutex_unlock;
	}

	if (!xrcd) {
		xrcd = ib_dev->alloc_xrcd(ib_dev, file->ucontext, &udata);
		if (IS_ERR(xrcd)) {
			ret = PTR_ERR(xrcd);
			goto err;
		}

		xrcd->inode   = inode;
		xrcd->device  = ib_dev;
		atomic_set(&xrcd->usecnt, 0);
		mutex_init(&xrcd->tgt_qp_mutex);
		INIT_LIST_HEAD(&xrcd->tgt_qp_list);
		new_xrcd = 1;
	}

	atomic_set(&obj->refcnt, 0);
	obj->uobject.object = xrcd;
	memset(&resp, 0, sizeof resp);
	resp.xrcd_handle = obj->uobject.id;

	if (inode) {
		if (new_xrcd) {
			/* create new inode/xrcd table entry */
			ret = xrcd_table_insert(file->device, inode, xrcd);
			if (ret)
				goto err_dealloc_xrcd;
		}
		atomic_inc(&xrcd->usecnt);
	}

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		ret = -EFAULT;
		goto err_copy;
	}

	if (f.file)
		fdput(f);

	uobj_alloc_commit(&obj->uobject);

	mutex_unlock(&file->device->xrcd_tree_mutex);
	return in_len;

err_copy:
	if (inode) {
		if (new_xrcd)
			xrcd_table_delete(file->device, inode);
		atomic_dec(&xrcd->usecnt);
	}

err_dealloc_xrcd:
	ib_dealloc_xrcd(xrcd);

err:
	uobj_alloc_abort(&obj->uobject);

err_tree_mutex_unlock:
	if (f.file)
		fdput(f);

	mutex_unlock(&file->device->xrcd_tree_mutex);

	return ret;
}

ssize_t ib_uverbs_close_xrcd(struct ib_uverbs_file *file,
			     struct ib_device *ib_dev,
			     const char __user *buf, int in_len,
			     int out_len)
{
	struct ib_uverbs_close_xrcd cmd;
	struct ib_uobject           *uobj;
	int                         ret = 0;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	uobj  = uobj_get_write(uobj_get_type(xrcd), cmd.xrcd_handle,
			       file->ucontext);
	if (IS_ERR(uobj)) {
		mutex_unlock(&file->device->xrcd_tree_mutex);
		return PTR_ERR(uobj);
	}

	ret = uobj_remove_commit(uobj);
	return ret ?: in_len;
}

int ib_uverbs_dealloc_xrcd(struct ib_uverbs_device *dev,
			   struct ib_xrcd *xrcd,
			   enum rdma_remove_reason why)
{
	struct inode *inode;
	int ret;

	inode = xrcd->inode;
	if (inode && !atomic_dec_and_test(&xrcd->usecnt))
		return 0;

	ret = ib_dealloc_xrcd(xrcd);

	if (why == RDMA_REMOVE_DESTROY && ret)
		atomic_inc(&xrcd->usecnt);
	else if (inode)
		xrcd_table_delete(dev, inode);

	return ret;
}

ssize_t ib_uverbs_reg_mr(struct ib_uverbs_file *file,
			 struct ib_device *ib_dev,
			 const char __user *buf, int in_len,
			 int out_len)
{
	struct ib_uverbs_reg_mr      cmd;
	struct ib_uverbs_reg_mr_resp resp;
	struct ib_udata              udata;
	struct ib_uobject           *uobj;
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

	ret = ib_check_mr_access(cmd.access_flags);
	if (ret)
		return ret;

	uobj  = uobj_alloc(uobj_get_type(mr), file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	pd = uobj_get_obj_read(pd, cmd.pd_handle, file->ucontext);
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

	mr = pd->device->reg_user_mr(pd, cmd.start, cmd.length, cmd.hca_va,
				     cmd.access_flags, &udata);
	if (IS_ERR(mr)) {
		ret = PTR_ERR(mr);
		goto err_put;
	}

	mr->device  = pd->device;
	mr->pd      = pd;
	mr->uobject = uobj;
	atomic_inc(&pd->usecnt);

	uobj->object = mr;

	memset(&resp, 0, sizeof resp);
	resp.lkey      = mr->lkey;
	resp.rkey      = mr->rkey;
	resp.mr_handle = uobj->id;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		ret = -EFAULT;
		goto err_copy;
	}

	uobj_put_obj_read(pd);

	uobj_alloc_commit(uobj);

	return in_len;

err_copy:
	ib_dereg_mr(mr);

err_put:
	uobj_put_obj_read(pd);

err_free:
	uobj_alloc_abort(uobj);
	return ret;
}

ssize_t ib_uverbs_rereg_mr(struct ib_uverbs_file *file,
			   struct ib_device *ib_dev,
			   const char __user *buf, int in_len,
			   int out_len)
{
	struct ib_uverbs_rereg_mr      cmd;
	struct ib_uverbs_rereg_mr_resp resp;
	struct ib_udata              udata;
	struct ib_pd                *pd = NULL;
	struct ib_mr                *mr;
	struct ib_pd		    *old_pd;
	int                          ret;
	struct ib_uobject	    *uobj;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	INIT_UDATA(&udata, buf + sizeof(cmd),
		   (unsigned long) cmd.response + sizeof(resp),
		   in_len - sizeof(cmd), out_len - sizeof(resp));

	if (cmd.flags & ~IB_MR_REREG_SUPPORTED || !cmd.flags)
		return -EINVAL;

	if ((cmd.flags & IB_MR_REREG_TRANS) &&
	    (!cmd.start || !cmd.hca_va || 0 >= cmd.length ||
	     (cmd.start & ~PAGE_MASK) != (cmd.hca_va & ~PAGE_MASK)))
			return -EINVAL;

	uobj  = uobj_get_write(uobj_get_type(mr), cmd.mr_handle,
			       file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	mr = uobj->object;

	if (cmd.flags & IB_MR_REREG_ACCESS) {
		ret = ib_check_mr_access(cmd.access_flags);
		if (ret)
			goto put_uobjs;
	}

	if (cmd.flags & IB_MR_REREG_PD) {
		pd = uobj_get_obj_read(pd, cmd.pd_handle, file->ucontext);
		if (!pd) {
			ret = -EINVAL;
			goto put_uobjs;
		}
	}

	old_pd = mr->pd;
	ret = mr->device->rereg_user_mr(mr, cmd.flags, cmd.start,
					cmd.length, cmd.hca_va,
					cmd.access_flags, pd, &udata);
	if (!ret) {
		if (cmd.flags & IB_MR_REREG_PD) {
			atomic_inc(&pd->usecnt);
			mr->pd = pd;
			atomic_dec(&old_pd->usecnt);
		}
	} else {
		goto put_uobj_pd;
	}

	memset(&resp, 0, sizeof(resp));
	resp.lkey      = mr->lkey;
	resp.rkey      = mr->rkey;

	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp)))
		ret = -EFAULT;
	else
		ret = in_len;

put_uobj_pd:
	if (cmd.flags & IB_MR_REREG_PD)
		uobj_put_obj_read(pd);

put_uobjs:
	uobj_put_write(uobj);

	return ret;
}

ssize_t ib_uverbs_dereg_mr(struct ib_uverbs_file *file,
			   struct ib_device *ib_dev,
			   const char __user *buf, int in_len,
			   int out_len)
{
	struct ib_uverbs_dereg_mr cmd;
	struct ib_uobject	 *uobj;
	int                       ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	uobj  = uobj_get_write(uobj_get_type(mr), cmd.mr_handle,
			       file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	ret = uobj_remove_commit(uobj);

	return ret ?: in_len;
}

ssize_t ib_uverbs_alloc_mw(struct ib_uverbs_file *file,
			   struct ib_device *ib_dev,
			   const char __user *buf, int in_len,
			   int out_len)
{
	struct ib_uverbs_alloc_mw      cmd;
	struct ib_uverbs_alloc_mw_resp resp;
	struct ib_uobject             *uobj;
	struct ib_pd                  *pd;
	struct ib_mw                  *mw;
	struct ib_udata		       udata;
	int                            ret;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	uobj  = uobj_alloc(uobj_get_type(mw), file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	pd = uobj_get_obj_read(pd, cmd.pd_handle, file->ucontext);
	if (!pd) {
		ret = -EINVAL;
		goto err_free;
	}

	INIT_UDATA(&udata, buf + sizeof(cmd),
		   (unsigned long)cmd.response + sizeof(resp),
		   in_len - sizeof(cmd) - sizeof(struct ib_uverbs_cmd_hdr),
		   out_len - sizeof(resp));

	mw = pd->device->alloc_mw(pd, cmd.mw_type, &udata);
	if (IS_ERR(mw)) {
		ret = PTR_ERR(mw);
		goto err_put;
	}

	mw->device  = pd->device;
	mw->pd      = pd;
	mw->uobject = uobj;
	atomic_inc(&pd->usecnt);

	uobj->object = mw;

	memset(&resp, 0, sizeof(resp));
	resp.rkey      = mw->rkey;
	resp.mw_handle = uobj->id;

	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp))) {
		ret = -EFAULT;
		goto err_copy;
	}

	uobj_put_obj_read(pd);
	uobj_alloc_commit(uobj);

	return in_len;

err_copy:
	uverbs_dealloc_mw(mw);
err_put:
	uobj_put_obj_read(pd);
err_free:
	uobj_alloc_abort(uobj);
	return ret;
}

ssize_t ib_uverbs_dealloc_mw(struct ib_uverbs_file *file,
			     struct ib_device *ib_dev,
			     const char __user *buf, int in_len,
			     int out_len)
{
	struct ib_uverbs_dealloc_mw cmd;
	struct ib_uobject	   *uobj;
	int                         ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	uobj  = uobj_get_write(uobj_get_type(mw), cmd.mw_handle,
			       file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	ret = uobj_remove_commit(uobj);
	return ret ?: in_len;
}

ssize_t ib_uverbs_create_comp_channel(struct ib_uverbs_file *file,
				      struct ib_device *ib_dev,
				      const char __user *buf, int in_len,
				      int out_len)
{
	struct ib_uverbs_create_comp_channel	   cmd;
	struct ib_uverbs_create_comp_channel_resp  resp;
	struct ib_uobject			  *uobj;
	struct ib_uverbs_completion_event_file	  *ev_file;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	uobj = uobj_alloc(uobj_get_type(comp_channel), file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	resp.fd = uobj->id;

	ev_file = container_of(uobj, struct ib_uverbs_completion_event_file,
			       uobj_file.uobj);
	ib_uverbs_init_event_queue(&ev_file->ev_queue);

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		uobj_alloc_abort(uobj);
		return -EFAULT;
	}

	uobj_alloc_commit(uobj);
	return in_len;
}

static struct ib_ucq_object *create_cq(struct ib_uverbs_file *file,
					struct ib_device *ib_dev,
				       struct ib_udata *ucore,
				       struct ib_udata *uhw,
				       struct ib_uverbs_ex_create_cq *cmd,
				       size_t cmd_sz,
				       int (*cb)(struct ib_uverbs_file *file,
						 struct ib_ucq_object *obj,
						 struct ib_uverbs_ex_create_cq_resp *resp,
						 struct ib_udata *udata,
						 void *context),
				       void *context)
{
	struct ib_ucq_object           *obj;
	struct ib_uverbs_completion_event_file    *ev_file = NULL;
	struct ib_cq                   *cq;
	int                             ret;
	struct ib_uverbs_ex_create_cq_resp resp;
	struct ib_cq_init_attr attr = {};

	if (cmd->comp_vector >= file->device->num_comp_vectors)
		return ERR_PTR(-EINVAL);

	obj  = (struct ib_ucq_object *)uobj_alloc(uobj_get_type(cq),
						  file->ucontext);
	if (IS_ERR(obj))
		return obj;

	if (cmd->comp_channel >= 0) {
		ev_file = ib_uverbs_lookup_comp_file(cmd->comp_channel,
						     file->ucontext);
		if (IS_ERR(ev_file)) {
			ret = PTR_ERR(ev_file);
			goto err;
		}
	}

	obj->uobject.user_handle = cmd->user_handle;
	obj->uverbs_file	   = file;
	obj->comp_events_reported  = 0;
	obj->async_events_reported = 0;
	INIT_LIST_HEAD(&obj->comp_list);
	INIT_LIST_HEAD(&obj->async_list);

	attr.cqe = cmd->cqe;
	attr.comp_vector = cmd->comp_vector;

	if (cmd_sz > offsetof(typeof(*cmd), flags) + sizeof(cmd->flags))
		attr.flags = cmd->flags;

	cq = ib_dev->create_cq(ib_dev, &attr, file->ucontext, uhw);
	if (IS_ERR(cq)) {
		ret = PTR_ERR(cq);
		goto err_file;
	}

	cq->device        = ib_dev;
	cq->uobject       = &obj->uobject;
	cq->comp_handler  = ib_uverbs_comp_handler;
	cq->event_handler = ib_uverbs_cq_event_handler;
	cq->cq_context    = &ev_file->ev_queue;
	atomic_set(&cq->usecnt, 0);

	obj->uobject.object = cq;
	memset(&resp, 0, sizeof resp);
	resp.base.cq_handle = obj->uobject.id;
	resp.base.cqe       = cq->cqe;

	resp.response_length = offsetof(typeof(resp), response_length) +
		sizeof(resp.response_length);

	ret = cb(file, obj, &resp, ucore, context);
	if (ret)
		goto err_cb;

	uobj_alloc_commit(&obj->uobject);

	return obj;

err_cb:
	ib_destroy_cq(cq);

err_file:
	if (ev_file)
		ib_uverbs_release_ucq(file, ev_file, obj);

err:
	uobj_alloc_abort(&obj->uobject);

	return ERR_PTR(ret);
}

static int ib_uverbs_create_cq_cb(struct ib_uverbs_file *file,
				  struct ib_ucq_object *obj,
				  struct ib_uverbs_ex_create_cq_resp *resp,
				  struct ib_udata *ucore, void *context)
{
	if (ib_copy_to_udata(ucore, &resp->base, sizeof(resp->base)))
		return -EFAULT;

	return 0;
}

ssize_t ib_uverbs_create_cq(struct ib_uverbs_file *file,
			    struct ib_device *ib_dev,
			    const char __user *buf, int in_len,
			    int out_len)
{
	struct ib_uverbs_create_cq      cmd;
	struct ib_uverbs_ex_create_cq	cmd_ex;
	struct ib_uverbs_create_cq_resp resp;
	struct ib_udata                 ucore;
	struct ib_udata                 uhw;
	struct ib_ucq_object           *obj;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	INIT_UDATA(&ucore, buf, (unsigned long)cmd.response, sizeof(cmd), sizeof(resp));

	INIT_UDATA(&uhw, buf + sizeof(cmd),
		   (unsigned long)cmd.response + sizeof(resp),
		   in_len - sizeof(cmd), out_len - sizeof(resp));

	memset(&cmd_ex, 0, sizeof(cmd_ex));
	cmd_ex.user_handle = cmd.user_handle;
	cmd_ex.cqe = cmd.cqe;
	cmd_ex.comp_vector = cmd.comp_vector;
	cmd_ex.comp_channel = cmd.comp_channel;

	obj = create_cq(file, ib_dev, &ucore, &uhw, &cmd_ex,
			offsetof(typeof(cmd_ex), comp_channel) +
			sizeof(cmd.comp_channel), ib_uverbs_create_cq_cb,
			NULL);

	if (IS_ERR(obj))
		return PTR_ERR(obj);

	return in_len;
}

static int ib_uverbs_ex_create_cq_cb(struct ib_uverbs_file *file,
				     struct ib_ucq_object *obj,
				     struct ib_uverbs_ex_create_cq_resp *resp,
				     struct ib_udata *ucore, void *context)
{
	if (ib_copy_to_udata(ucore, resp, resp->response_length))
		return -EFAULT;

	return 0;
}

int ib_uverbs_ex_create_cq(struct ib_uverbs_file *file,
			 struct ib_device *ib_dev,
			   struct ib_udata *ucore,
			   struct ib_udata *uhw)
{
	struct ib_uverbs_ex_create_cq_resp resp;
	struct ib_uverbs_ex_create_cq  cmd;
	struct ib_ucq_object           *obj;
	int err;

	if (ucore->inlen < sizeof(cmd))
		return -EINVAL;

	err = ib_copy_from_udata(&cmd, ucore, sizeof(cmd));
	if (err)
		return err;

	if (cmd.comp_mask)
		return -EINVAL;

	if (cmd.reserved)
		return -EINVAL;

	if (ucore->outlen < (offsetof(typeof(resp), response_length) +
			     sizeof(resp.response_length)))
		return -ENOSPC;

	obj = create_cq(file, ib_dev, ucore, uhw, &cmd,
			min(ucore->inlen, sizeof(cmd)),
			ib_uverbs_ex_create_cq_cb, NULL);

	if (IS_ERR(obj))
		return PTR_ERR(obj);

	return 0;
}

ssize_t ib_uverbs_resize_cq(struct ib_uverbs_file *file,
			    struct ib_device *ib_dev,
			    const char __user *buf, int in_len,
			    int out_len)
{
	struct ib_uverbs_resize_cq	cmd;
	struct ib_uverbs_resize_cq_resp	resp;
	struct ib_udata                 udata;
	struct ib_cq			*cq;
	int				ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	INIT_UDATA(&udata, buf + sizeof cmd,
		   (unsigned long) cmd.response + sizeof resp,
		   in_len - sizeof cmd, out_len - sizeof resp);

	cq = uobj_get_obj_read(cq, cmd.cq_handle, file->ucontext);
	if (!cq)
		return -EINVAL;

	ret = cq->device->resize_cq(cq, cmd.cqe, &udata);
	if (ret)
		goto out;

	resp.cqe = cq->cqe;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp.cqe))
		ret = -EFAULT;

out:
	uobj_put_obj_read(cq);

	return ret ? ret : in_len;
}

static int copy_wc_to_user(void __user *dest, struct ib_wc *wc)
{
	struct ib_uverbs_wc tmp;

	tmp.wr_id		= wc->wr_id;
	tmp.status		= wc->status;
	tmp.opcode		= wc->opcode;
	tmp.vendor_err		= wc->vendor_err;
	tmp.byte_len		= wc->byte_len;
	tmp.ex.imm_data		= (__u32 __force) wc->ex.imm_data;
	tmp.qp_num		= wc->qp->qp_num;
	tmp.src_qp		= wc->src_qp;
	tmp.wc_flags		= wc->wc_flags;
	tmp.pkey_index		= wc->pkey_index;
	tmp.slid		= wc->slid;
	tmp.sl			= wc->sl;
	tmp.dlid_path_bits	= wc->dlid_path_bits;
	tmp.port_num		= wc->port_num;
	tmp.reserved		= 0;

	if (copy_to_user(dest, &tmp, sizeof tmp))
		return -EFAULT;

	return 0;
}

ssize_t ib_uverbs_poll_cq(struct ib_uverbs_file *file,
			  struct ib_device *ib_dev,
			  const char __user *buf, int in_len,
			  int out_len)
{
	struct ib_uverbs_poll_cq       cmd;
	struct ib_uverbs_poll_cq_resp  resp;
	u8 __user                     *header_ptr;
	u8 __user                     *data_ptr;
	struct ib_cq                  *cq;
	struct ib_wc                   wc;
	int                            ret;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	cq = uobj_get_obj_read(cq, cmd.cq_handle, file->ucontext);
	if (!cq)
		return -EINVAL;

	/* we copy a struct ib_uverbs_poll_cq_resp to user space */
	header_ptr = (void __user *)(unsigned long) cmd.response;
	data_ptr = header_ptr + sizeof resp;

	memset(&resp, 0, sizeof resp);
	while (resp.count < cmd.ne) {
		ret = ib_poll_cq(cq, 1, &wc);
		if (ret < 0)
			goto out_put;
		if (!ret)
			break;

		ret = copy_wc_to_user(data_ptr, &wc);
		if (ret)
			goto out_put;

		data_ptr += sizeof(struct ib_uverbs_wc);
		++resp.count;
	}

	if (copy_to_user(header_ptr, &resp, sizeof resp)) {
		ret = -EFAULT;
		goto out_put;
	}

	ret = in_len;

out_put:
	uobj_put_obj_read(cq);
	return ret;
}

ssize_t ib_uverbs_req_notify_cq(struct ib_uverbs_file *file,
				struct ib_device *ib_dev,
				const char __user *buf, int in_len,
				int out_len)
{
	struct ib_uverbs_req_notify_cq cmd;
	struct ib_cq                  *cq;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	cq = uobj_get_obj_read(cq, cmd.cq_handle, file->ucontext);
	if (!cq)
		return -EINVAL;

	ib_req_notify_cq(cq, cmd.solicited_only ?
			 IB_CQ_SOLICITED : IB_CQ_NEXT_COMP);

	uobj_put_obj_read(cq);

	return in_len;
}

ssize_t ib_uverbs_destroy_cq(struct ib_uverbs_file *file,
			     struct ib_device *ib_dev,
			     const char __user *buf, int in_len,
			     int out_len)
{
	struct ib_uverbs_destroy_cq      cmd;
	struct ib_uverbs_destroy_cq_resp resp;
	struct ib_uobject		*uobj;
	struct ib_cq               	*cq;
	struct ib_ucq_object        	*obj;
	struct ib_uverbs_event_queue	*ev_queue;
	int                        	 ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	uobj  = uobj_get_write(uobj_get_type(cq), cmd.cq_handle,
			       file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	/*
	 * Make sure we don't free the memory in remove_commit as we still
	 * needs the uobject memory to create the response.
	 */
	uverbs_uobject_get(uobj);
	cq      = uobj->object;
	ev_queue = cq->cq_context;
	obj     = container_of(cq->uobject, struct ib_ucq_object, uobject);

	memset(&resp, 0, sizeof(resp));

	ret = uobj_remove_commit(uobj);
	if (ret) {
		uverbs_uobject_put(uobj);
		return ret;
	}

	resp.comp_events_reported  = obj->comp_events_reported;
	resp.async_events_reported = obj->async_events_reported;

	uverbs_uobject_put(uobj);
	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		return -EFAULT;

	return in_len;
}

static int create_qp(struct ib_uverbs_file *file,
		     struct ib_udata *ucore,
		     struct ib_udata *uhw,
		     struct ib_uverbs_ex_create_qp *cmd,
		     size_t cmd_sz,
		     int (*cb)(struct ib_uverbs_file *file,
			       struct ib_uverbs_ex_create_qp_resp *resp,
			       struct ib_udata *udata),
		     void *context)
{
	struct ib_uqp_object		*obj;
	struct ib_device		*device;
	struct ib_pd			*pd = NULL;
	struct ib_xrcd			*xrcd = NULL;
	struct ib_uobject		*xrcd_uobj = ERR_PTR(-ENOENT);
	struct ib_cq			*scq = NULL, *rcq = NULL;
	struct ib_srq			*srq = NULL;
	struct ib_qp			*qp;
	char				*buf;
	struct ib_qp_init_attr		attr = {};
	struct ib_uverbs_ex_create_qp_resp resp;
	int				ret;
	struct ib_rwq_ind_table *ind_tbl = NULL;
	bool has_sq = true;

	if (cmd->qp_type == IB_QPT_RAW_PACKET && !capable(CAP_NET_RAW))
		return -EPERM;

	obj  = (struct ib_uqp_object *)uobj_alloc(uobj_get_type(qp),
						  file->ucontext);
	if (IS_ERR(obj))
		return PTR_ERR(obj);
	obj->uxrcd = NULL;
	obj->uevent.uobject.user_handle = cmd->user_handle;
	mutex_init(&obj->mcast_lock);

	if (cmd_sz >= offsetof(typeof(*cmd), rwq_ind_tbl_handle) +
		      sizeof(cmd->rwq_ind_tbl_handle) &&
		      (cmd->comp_mask & IB_UVERBS_CREATE_QP_MASK_IND_TABLE)) {
		ind_tbl = uobj_get_obj_read(rwq_ind_table,
					    cmd->rwq_ind_tbl_handle,
					    file->ucontext);
		if (!ind_tbl) {
			ret = -EINVAL;
			goto err_put;
		}

		attr.rwq_ind_tbl = ind_tbl;
	}

	if ((cmd_sz >= offsetof(typeof(*cmd), reserved1) +
		       sizeof(cmd->reserved1)) && cmd->reserved1) {
		ret = -EOPNOTSUPP;
		goto err_put;
	}

	if (ind_tbl && (cmd->max_recv_wr || cmd->max_recv_sge || cmd->is_srq)) {
		ret = -EINVAL;
		goto err_put;
	}

	if (ind_tbl && !cmd->max_send_wr)
		has_sq = false;

	if (cmd->qp_type == IB_QPT_XRC_TGT) {
		xrcd_uobj = uobj_get_read(uobj_get_type(xrcd), cmd->pd_handle,
					  file->ucontext);

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
				srq = uobj_get_obj_read(srq, cmd->srq_handle,
							file->ucontext);
				if (!srq || srq->srq_type != IB_SRQT_BASIC) {
					ret = -EINVAL;
					goto err_put;
				}
			}

			if (!ind_tbl) {
				if (cmd->recv_cq_handle != cmd->send_cq_handle) {
					rcq = uobj_get_obj_read(cq, cmd->recv_cq_handle,
								file->ucontext);
					if (!rcq) {
						ret = -EINVAL;
						goto err_put;
					}
				}
			}
		}

		if (has_sq)
			scq = uobj_get_obj_read(cq, cmd->send_cq_handle,
						file->ucontext);
		if (!ind_tbl)
			rcq = rcq ?: scq;
		pd  = uobj_get_obj_read(pd, cmd->pd_handle, file->ucontext);
		if (!pd || (!scq && has_sq)) {
			ret = -EINVAL;
			goto err_put;
		}

		device = pd->device;
	}

	attr.event_handler = ib_uverbs_qp_event_handler;
	attr.qp_context    = file;
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

	obj->uevent.events_reported     = 0;
	INIT_LIST_HEAD(&obj->uevent.event_list);
	INIT_LIST_HEAD(&obj->mcast_list);

	if (cmd_sz >= offsetof(typeof(*cmd), create_flags) +
		      sizeof(cmd->create_flags))
		attr.create_flags = cmd->create_flags;

	if (attr.create_flags & ~(IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK |
				IB_QP_CREATE_CROSS_CHANNEL |
				IB_QP_CREATE_MANAGED_SEND |
				IB_QP_CREATE_MANAGED_RECV |
				IB_QP_CREATE_SCATTER_FCS |
				IB_QP_CREATE_CVLAN_STRIPPING)) {
		ret = -EINVAL;
		goto err_put;
	}

	buf = (void *)cmd + sizeof(*cmd);
	if (cmd_sz > sizeof(*cmd))
		if (!(buf[0] == 0 && !memcmp(buf, buf + 1,
					     cmd_sz - sizeof(*cmd) - 1))) {
			ret = -EINVAL;
			goto err_put;
		}

	if (cmd->qp_type == IB_QPT_XRC_TGT)
		qp = ib_create_qp(pd, &attr);
	else
		qp = device->create_qp(pd, &attr, uhw);

	if (IS_ERR(qp)) {
		ret = PTR_ERR(qp);
		goto err_put;
	}

	if (cmd->qp_type != IB_QPT_XRC_TGT) {
		qp->real_qp	  = qp;
		qp->device	  = device;
		qp->pd		  = pd;
		qp->send_cq	  = attr.send_cq;
		qp->recv_cq	  = attr.recv_cq;
		qp->srq		  = attr.srq;
		qp->rwq_ind_tbl	  = ind_tbl;
		qp->event_handler = attr.event_handler;
		qp->qp_context	  = attr.qp_context;
		qp->qp_type	  = attr.qp_type;
		atomic_set(&qp->usecnt, 0);
		atomic_inc(&pd->usecnt);
		if (attr.send_cq)
			atomic_inc(&attr.send_cq->usecnt);
		if (attr.recv_cq)
			atomic_inc(&attr.recv_cq->usecnt);
		if (attr.srq)
			atomic_inc(&attr.srq->usecnt);
		if (ind_tbl)
			atomic_inc(&ind_tbl->usecnt);
	}
	qp->uobject = &obj->uevent.uobject;

	obj->uevent.uobject.object = qp;

	memset(&resp, 0, sizeof resp);
	resp.base.qpn             = qp->qp_num;
	resp.base.qp_handle       = obj->uevent.uobject.id;
	resp.base.max_recv_sge    = attr.cap.max_recv_sge;
	resp.base.max_send_sge    = attr.cap.max_send_sge;
	resp.base.max_recv_wr     = attr.cap.max_recv_wr;
	resp.base.max_send_wr     = attr.cap.max_send_wr;
	resp.base.max_inline_data = attr.cap.max_inline_data;

	resp.response_length = offsetof(typeof(resp), response_length) +
			       sizeof(resp.response_length);

	ret = cb(file, &resp, ucore);
	if (ret)
		goto err_cb;

	if (xrcd) {
		obj->uxrcd = container_of(xrcd_uobj, struct ib_uxrcd_object,
					  uobject);
		atomic_inc(&obj->uxrcd->refcnt);
		uobj_put_read(xrcd_uobj);
	}

	if (pd)
		uobj_put_obj_read(pd);
	if (scq)
		uobj_put_obj_read(scq);
	if (rcq && rcq != scq)
		uobj_put_obj_read(rcq);
	if (srq)
		uobj_put_obj_read(srq);
	if (ind_tbl)
		uobj_put_obj_read(ind_tbl);

	uobj_alloc_commit(&obj->uevent.uobject);

	return 0;
err_cb:
	ib_destroy_qp(qp);

err_put:
	if (!IS_ERR(xrcd_uobj))
		uobj_put_read(xrcd_uobj);
	if (pd)
		uobj_put_obj_read(pd);
	if (scq)
		uobj_put_obj_read(scq);
	if (rcq && rcq != scq)
		uobj_put_obj_read(rcq);
	if (srq)
		uobj_put_obj_read(srq);
	if (ind_tbl)
		uobj_put_obj_read(ind_tbl);

	uobj_alloc_abort(&obj->uevent.uobject);
	return ret;
}

static int ib_uverbs_create_qp_cb(struct ib_uverbs_file *file,
				  struct ib_uverbs_ex_create_qp_resp *resp,
				  struct ib_udata *ucore)
{
	if (ib_copy_to_udata(ucore, &resp->base, sizeof(resp->base)))
		return -EFAULT;

	return 0;
}

ssize_t ib_uverbs_create_qp(struct ib_uverbs_file *file,
			    struct ib_device *ib_dev,
			    const char __user *buf, int in_len,
			    int out_len)
{
	struct ib_uverbs_create_qp      cmd;
	struct ib_uverbs_ex_create_qp	cmd_ex;
	struct ib_udata			ucore;
	struct ib_udata			uhw;
	ssize_t resp_size = sizeof(struct ib_uverbs_create_qp_resp);
	int				err;

	if (out_len < resp_size)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	INIT_UDATA(&ucore, buf, (unsigned long)cmd.response, sizeof(cmd),
		   resp_size);
	INIT_UDATA(&uhw, buf + sizeof(cmd),
		   (unsigned long)cmd.response + resp_size,
		   in_len - sizeof(cmd) - sizeof(struct ib_uverbs_cmd_hdr),
		   out_len - resp_size);

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

	err = create_qp(file, &ucore, &uhw, &cmd_ex,
			offsetof(typeof(cmd_ex), is_srq) +
			sizeof(cmd.is_srq), ib_uverbs_create_qp_cb,
			NULL);

	if (err)
		return err;

	return in_len;
}

static int ib_uverbs_ex_create_qp_cb(struct ib_uverbs_file *file,
				     struct ib_uverbs_ex_create_qp_resp *resp,
				     struct ib_udata *ucore)
{
	if (ib_copy_to_udata(ucore, resp, resp->response_length))
		return -EFAULT;

	return 0;
}

int ib_uverbs_ex_create_qp(struct ib_uverbs_file *file,
			   struct ib_device *ib_dev,
			   struct ib_udata *ucore,
			   struct ib_udata *uhw)
{
	struct ib_uverbs_ex_create_qp_resp resp;
	struct ib_uverbs_ex_create_qp cmd = {0};
	int err;

	if (ucore->inlen < (offsetof(typeof(cmd), comp_mask) +
			    sizeof(cmd.comp_mask)))
		return -EINVAL;

	err = ib_copy_from_udata(&cmd, ucore, min(sizeof(cmd), ucore->inlen));
	if (err)
		return err;

	if (cmd.comp_mask & ~IB_UVERBS_CREATE_QP_SUP_COMP_MASK)
		return -EINVAL;

	if (cmd.reserved)
		return -EINVAL;

	if (ucore->outlen < (offsetof(typeof(resp), response_length) +
			     sizeof(resp.response_length)))
		return -ENOSPC;

	err = create_qp(file, ucore, uhw, &cmd,
			min(ucore->inlen, sizeof(cmd)),
			ib_uverbs_ex_create_qp_cb, NULL);

	if (err)
		return err;

	return 0;
}

ssize_t ib_uverbs_open_qp(struct ib_uverbs_file *file,
			  struct ib_device *ib_dev,
			  const char __user *buf, int in_len, int out_len)
{
	struct ib_uverbs_open_qp        cmd;
	struct ib_uverbs_create_qp_resp resp;
	struct ib_udata                 udata;
	struct ib_uqp_object           *obj;
	struct ib_xrcd		       *xrcd;
	struct ib_uobject	       *uninitialized_var(xrcd_uobj);
	struct ib_qp                   *qp;
	struct ib_qp_open_attr          attr;
	int ret;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	INIT_UDATA(&udata, buf + sizeof cmd,
		   (unsigned long) cmd.response + sizeof resp,
		   in_len - sizeof cmd, out_len - sizeof resp);

	obj  = (struct ib_uqp_object *)uobj_alloc(uobj_get_type(qp),
						  file->ucontext);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	xrcd_uobj = uobj_get_read(uobj_get_type(xrcd), cmd.pd_handle,
				  file->ucontext);
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
	attr.qp_context    = file;
	attr.qp_num        = cmd.qpn;
	attr.qp_type       = cmd.qp_type;

	obj->uevent.events_reported = 0;
	INIT_LIST_HEAD(&obj->uevent.event_list);
	INIT_LIST_HEAD(&obj->mcast_list);

	qp = ib_open_qp(xrcd, &attr);
	if (IS_ERR(qp)) {
		ret = PTR_ERR(qp);
		goto err_xrcd;
	}

	obj->uevent.uobject.object = qp;
	obj->uevent.uobject.user_handle = cmd.user_handle;

	memset(&resp, 0, sizeof resp);
	resp.qpn       = qp->qp_num;
	resp.qp_handle = obj->uevent.uobject.id;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		ret = -EFAULT;
		goto err_destroy;
	}

	obj->uxrcd = container_of(xrcd_uobj, struct ib_uxrcd_object, uobject);
	atomic_inc(&obj->uxrcd->refcnt);
	qp->uobject = &obj->uevent.uobject;
	uobj_put_read(xrcd_uobj);


	uobj_alloc_commit(&obj->uevent.uobject);

	return in_len;

err_destroy:
	ib_destroy_qp(qp);
err_xrcd:
	uobj_put_read(xrcd_uobj);
err_put:
	uobj_alloc_abort(&obj->uevent.uobject);
	return ret;
}

ssize_t ib_uverbs_query_qp(struct ib_uverbs_file *file,
			   struct ib_device *ib_dev,
			   const char __user *buf, int in_len,
			   int out_len)
{
	struct ib_uverbs_query_qp      cmd;
	struct ib_uverbs_query_qp_resp resp;
	struct ib_qp                   *qp;
	struct ib_qp_attr              *attr;
	struct ib_qp_init_attr         *init_attr;
	const struct ib_global_route   *grh;
	int                            ret;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	attr      = kmalloc(sizeof *attr, GFP_KERNEL);
	init_attr = kmalloc(sizeof *init_attr, GFP_KERNEL);
	if (!attr || !init_attr) {
		ret = -ENOMEM;
		goto out;
	}

	qp = uobj_get_obj_read(qp, cmd.qp_handle, file->ucontext);
	if (!qp) {
		ret = -EINVAL;
		goto out;
	}

	ret = ib_query_qp(qp, attr, cmd.attr_mask, init_attr);

	uobj_put_obj_read(qp);

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

	resp.dest.dlid              = rdma_ah_get_dlid(&attr->ah_attr);
	resp.dest.sl                = rdma_ah_get_sl(&attr->ah_attr);
	resp.dest.src_path_bits     = rdma_ah_get_path_bits(&attr->ah_attr);
	resp.dest.static_rate       = rdma_ah_get_static_rate(&attr->ah_attr);
	resp.dest.is_global         = !!(rdma_ah_get_ah_flags(&attr->ah_attr) &
					 IB_AH_GRH);
	if (resp.dest.is_global) {
		grh = rdma_ah_read_grh(&attr->ah_attr);
		memcpy(resp.dest.dgid, grh->dgid.raw, 16);
		resp.dest.flow_label        = grh->flow_label;
		resp.dest.sgid_index        = grh->sgid_index;
		resp.dest.hop_limit         = grh->hop_limit;
		resp.dest.traffic_class     = grh->traffic_class;
	}
	resp.dest.port_num          = rdma_ah_get_port_num(&attr->ah_attr);

	resp.alt_dest.dlid          = rdma_ah_get_dlid(&attr->alt_ah_attr);
	resp.alt_dest.sl            = rdma_ah_get_sl(&attr->alt_ah_attr);
	resp.alt_dest.src_path_bits = rdma_ah_get_path_bits(&attr->alt_ah_attr);
	resp.alt_dest.static_rate
			= rdma_ah_get_static_rate(&attr->alt_ah_attr);
	resp.alt_dest.is_global
			= !!(rdma_ah_get_ah_flags(&attr->alt_ah_attr) &
						  IB_AH_GRH);
	if (resp.alt_dest.is_global) {
		grh = rdma_ah_read_grh(&attr->alt_ah_attr);
		memcpy(resp.alt_dest.dgid, grh->dgid.raw, 16);
		resp.alt_dest.flow_label    = grh->flow_label;
		resp.alt_dest.sgid_index    = grh->sgid_index;
		resp.alt_dest.hop_limit     = grh->hop_limit;
		resp.alt_dest.traffic_class = grh->traffic_class;
	}
	resp.alt_dest.port_num      = rdma_ah_get_port_num(&attr->alt_ah_attr);

	resp.max_send_wr            = init_attr->cap.max_send_wr;
	resp.max_recv_wr            = init_attr->cap.max_recv_wr;
	resp.max_send_sge           = init_attr->cap.max_send_sge;
	resp.max_recv_sge           = init_attr->cap.max_recv_sge;
	resp.max_inline_data        = init_attr->cap.max_inline_data;
	resp.sq_sig_all             = init_attr->sq_sig_type == IB_SIGNAL_ALL_WR;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		ret = -EFAULT;

out:
	kfree(attr);
	kfree(init_attr);

	return ret ? ret : in_len;
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

static int modify_qp(struct ib_uverbs_file *file,
		     struct ib_uverbs_ex_modify_qp *cmd, struct ib_udata *udata)
{
	struct ib_qp_attr *attr;
	struct ib_qp *qp;
	int ret;

	attr = kmalloc(sizeof *attr, GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	qp = uobj_get_obj_read(qp, cmd->base.qp_handle, file->ucontext);
	if (!qp) {
		ret = -EINVAL;
		goto out;
	}

	attr->qp_state		  = cmd->base.qp_state;
	attr->cur_qp_state	  = cmd->base.cur_qp_state;
	attr->path_mtu		  = cmd->base.path_mtu;
	attr->path_mig_state	  = cmd->base.path_mig_state;
	attr->qkey		  = cmd->base.qkey;
	attr->rq_psn		  = cmd->base.rq_psn;
	attr->sq_psn		  = cmd->base.sq_psn;
	attr->dest_qp_num	  = cmd->base.dest_qp_num;
	attr->qp_access_flags	  = cmd->base.qp_access_flags;
	attr->pkey_index	  = cmd->base.pkey_index;
	attr->alt_pkey_index	  = cmd->base.alt_pkey_index;
	attr->en_sqd_async_notify = cmd->base.en_sqd_async_notify;
	attr->max_rd_atomic	  = cmd->base.max_rd_atomic;
	attr->max_dest_rd_atomic  = cmd->base.max_dest_rd_atomic;
	attr->min_rnr_timer	  = cmd->base.min_rnr_timer;
	attr->port_num		  = cmd->base.port_num;
	attr->timeout		  = cmd->base.timeout;
	attr->retry_cnt		  = cmd->base.retry_cnt;
	attr->rnr_retry		  = cmd->base.rnr_retry;
	attr->alt_port_num	  = cmd->base.alt_port_num;
	attr->alt_timeout	  = cmd->base.alt_timeout;
	attr->rate_limit	  = cmd->rate_limit;

	attr->ah_attr.type = rdma_ah_find_type(qp->device,
					       cmd->base.dest.port_num);
	if (cmd->base.dest.is_global) {
		rdma_ah_set_grh(&attr->ah_attr, NULL,
				cmd->base.dest.flow_label,
				cmd->base.dest.sgid_index,
				cmd->base.dest.hop_limit,
				cmd->base.dest.traffic_class);
		rdma_ah_set_dgid_raw(&attr->ah_attr, cmd->base.dest.dgid);
	} else {
		rdma_ah_set_ah_flags(&attr->ah_attr, 0);
	}
	rdma_ah_set_dlid(&attr->ah_attr, cmd->base.dest.dlid);
	rdma_ah_set_sl(&attr->ah_attr, cmd->base.dest.sl);
	rdma_ah_set_path_bits(&attr->ah_attr, cmd->base.dest.src_path_bits);
	rdma_ah_set_static_rate(&attr->ah_attr, cmd->base.dest.static_rate);
	rdma_ah_set_port_num(&attr->ah_attr,
			     cmd->base.dest.port_num);

	attr->alt_ah_attr.type = rdma_ah_find_type(qp->device,
						   cmd->base.dest.port_num);
	if (cmd->base.alt_dest.is_global) {
		rdma_ah_set_grh(&attr->alt_ah_attr, NULL,
				cmd->base.alt_dest.flow_label,
				cmd->base.alt_dest.sgid_index,
				cmd->base.alt_dest.hop_limit,
				cmd->base.alt_dest.traffic_class);
		rdma_ah_set_dgid_raw(&attr->alt_ah_attr,
				     cmd->base.alt_dest.dgid);
	} else {
		rdma_ah_set_ah_flags(&attr->alt_ah_attr, 0);
	}

	rdma_ah_set_dlid(&attr->alt_ah_attr, cmd->base.alt_dest.dlid);
	rdma_ah_set_sl(&attr->alt_ah_attr, cmd->base.alt_dest.sl);
	rdma_ah_set_path_bits(&attr->alt_ah_attr,
			      cmd->base.alt_dest.src_path_bits);
	rdma_ah_set_static_rate(&attr->alt_ah_attr,
				cmd->base.alt_dest.static_rate);
	rdma_ah_set_port_num(&attr->alt_ah_attr,
			     cmd->base.alt_dest.port_num);

	if (qp->real_qp == qp) {
		if (cmd->base.attr_mask & IB_QP_AV) {
			ret = ib_resolve_eth_dmac(qp->device, &attr->ah_attr);
			if (ret)
				goto release_qp;
		}
		ret = qp->device->modify_qp(qp, attr,
					    modify_qp_mask(qp->qp_type,
							   cmd->base.attr_mask),
					    udata);
	} else {
		ret = ib_modify_qp(qp, attr,
				   modify_qp_mask(qp->qp_type,
						  cmd->base.attr_mask));
	}

release_qp:
	uobj_put_obj_read(qp);

out:
	kfree(attr);

	return ret;
}

ssize_t ib_uverbs_modify_qp(struct ib_uverbs_file *file,
			    struct ib_device *ib_dev,
			    const char __user *buf, int in_len,
			    int out_len)
{
	struct ib_uverbs_ex_modify_qp cmd = {};
	struct ib_udata udata;
	int ret;

	if (copy_from_user(&cmd.base, buf, sizeof(cmd.base)))
		return -EFAULT;

	if (cmd.base.attr_mask &
	    ~((IB_USER_LEGACY_LAST_QP_ATTR_MASK << 1) - 1))
		return -EOPNOTSUPP;

	INIT_UDATA(&udata, buf + sizeof(cmd.base), NULL,
		   in_len - sizeof(cmd.base), out_len);

	ret = modify_qp(file, &cmd, &udata);
	if (ret)
		return ret;

	return in_len;
}

int ib_uverbs_ex_modify_qp(struct ib_uverbs_file *file,
			   struct ib_device *ib_dev,
			   struct ib_udata *ucore,
			   struct ib_udata *uhw)
{
	struct ib_uverbs_ex_modify_qp cmd = {};
	int ret;

	/*
	 * Last bit is reserved for extending the attr_mask by
	 * using another field.
	 */
	BUILD_BUG_ON(IB_USER_LAST_QP_ATTR_MASK == (1 << 31));

	if (ucore->inlen < sizeof(cmd.base))
		return -EINVAL;

	ret = ib_copy_from_udata(&cmd, ucore, min(sizeof(cmd), ucore->inlen));
	if (ret)
		return ret;

	if (cmd.base.attr_mask &
	    ~((IB_USER_LAST_QP_ATTR_MASK << 1) - 1))
		return -EOPNOTSUPP;

	if (ucore->inlen > sizeof(cmd)) {
		if (ib_is_udata_cleared(ucore, sizeof(cmd),
					ucore->inlen - sizeof(cmd)))
			return -EOPNOTSUPP;
	}

	ret = modify_qp(file, &cmd, uhw);

	return ret;
}

ssize_t ib_uverbs_destroy_qp(struct ib_uverbs_file *file,
			     struct ib_device *ib_dev,
			     const char __user *buf, int in_len,
			     int out_len)
{
	struct ib_uverbs_destroy_qp      cmd;
	struct ib_uverbs_destroy_qp_resp resp;
	struct ib_uobject		*uobj;
	struct ib_qp               	*qp;
	struct ib_uqp_object        	*obj;
	int                        	 ret = -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	memset(&resp, 0, sizeof resp);

	uobj  = uobj_get_write(uobj_get_type(qp), cmd.qp_handle,
			       file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	qp  = uobj->object;
	obj = container_of(uobj, struct ib_uqp_object, uevent.uobject);
	/*
	 * Make sure we don't free the memory in remove_commit as we still
	 * needs the uobject memory to create the response.
	 */
	uverbs_uobject_get(uobj);

	ret = uobj_remove_commit(uobj);
	if (ret) {
		uverbs_uobject_put(uobj);
		return ret;
	}

	resp.events_reported = obj->uevent.events_reported;
	uverbs_uobject_put(uobj);

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		return -EFAULT;

	return in_len;
}

static void *alloc_wr(size_t wr_size, __u32 num_sge)
{
	if (num_sge >= (U32_MAX - ALIGN(wr_size, sizeof (struct ib_sge))) /
		       sizeof (struct ib_sge))
		return NULL;

	return kmalloc(ALIGN(wr_size, sizeof (struct ib_sge)) +
			 num_sge * sizeof (struct ib_sge), GFP_KERNEL);
}

ssize_t ib_uverbs_post_send(struct ib_uverbs_file *file,
			    struct ib_device *ib_dev,
			    const char __user *buf, int in_len,
			    int out_len)
{
	struct ib_uverbs_post_send      cmd;
	struct ib_uverbs_post_send_resp resp;
	struct ib_uverbs_send_wr       *user_wr;
	struct ib_send_wr              *wr = NULL, *last, *next, *bad_wr;
	struct ib_qp                   *qp;
	int                             i, sg_ind;
	int				is_ud;
	ssize_t                         ret = -EINVAL;
	size_t                          next_size;

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

	qp = uobj_get_obj_read(qp, cmd.qp_handle, file->ucontext);
	if (!qp)
		goto out;

	is_ud = qp->qp_type == IB_QPT_UD;
	sg_ind = 0;
	last = NULL;
	for (i = 0; i < cmd.wr_count; ++i) {
		if (copy_from_user(user_wr,
				   buf + sizeof cmd + i * cmd.wqe_size,
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

			ud->ah = uobj_get_obj_read(ah, user_wr->wr.ud.ah,
						   file->ucontext);
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
			if (copy_from_user(next->sg_list,
					   buf + sizeof cmd +
					   cmd.wr_count * cmd.wqe_size +
					   sg_ind * sizeof (struct ib_sge),
					   next->num_sge * sizeof (struct ib_sge))) {
				ret = -EFAULT;
				goto out_put;
			}
			sg_ind += next->num_sge;
		} else
			next->sg_list = NULL;
	}

	resp.bad_wr = 0;
	ret = qp->device->post_send(qp->real_qp, wr, &bad_wr);
	if (ret)
		for (next = wr; next; next = next->next) {
			++resp.bad_wr;
			if (next == bad_wr)
				break;
		}

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		ret = -EFAULT;

out_put:
	uobj_put_obj_read(qp);

	while (wr) {
		if (is_ud && ud_wr(wr)->ah)
			uobj_put_obj_read(ud_wr(wr)->ah);
		next = wr->next;
		kfree(wr);
		wr = next;
	}

out:
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
			    struct ib_device *ib_dev,
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

	qp = uobj_get_obj_read(qp, cmd.qp_handle, file->ucontext);
	if (!qp)
		goto out;

	resp.bad_wr = 0;
	ret = qp->device->post_recv(qp->real_qp, wr, &bad_wr);

	uobj_put_obj_read(qp);
	if (ret) {
		for (next = wr; next; next = next->next) {
			++resp.bad_wr;
			if (next == bad_wr)
				break;
		}
	}

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		ret = -EFAULT;

out:
	while (wr) {
		next = wr->next;
		kfree(wr);
		wr = next;
	}

	return ret ? ret : in_len;
}

ssize_t ib_uverbs_post_srq_recv(struct ib_uverbs_file *file,
				struct ib_device *ib_dev,
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

	srq = uobj_get_obj_read(srq, cmd.srq_handle, file->ucontext);
	if (!srq)
		goto out;

	resp.bad_wr = 0;
	ret = srq->device->post_srq_recv(srq, wr, &bad_wr);

	uobj_put_obj_read(srq);

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
	while (wr) {
		next = wr->next;
		kfree(wr);
		wr = next;
	}

	return ret ? ret : in_len;
}

ssize_t ib_uverbs_create_ah(struct ib_uverbs_file *file,
			    struct ib_device *ib_dev,
			    const char __user *buf, int in_len,
			    int out_len)
{
	struct ib_uverbs_create_ah	 cmd;
	struct ib_uverbs_create_ah_resp	 resp;
	struct ib_uobject		*uobj;
	struct ib_pd			*pd;
	struct ib_ah			*ah;
	struct rdma_ah_attr		attr;
	int ret;
	struct ib_udata                   udata;
	u8				*dmac;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	INIT_UDATA(&udata, buf + sizeof(cmd),
		   (unsigned long)cmd.response + sizeof(resp),
		   in_len - sizeof(cmd), out_len - sizeof(resp));

	uobj  = uobj_alloc(uobj_get_type(ah), file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	pd = uobj_get_obj_read(pd, cmd.pd_handle, file->ucontext);
	if (!pd) {
		ret = -EINVAL;
		goto err;
	}

	attr.type = rdma_ah_find_type(ib_dev, cmd.attr.port_num);
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
	dmac = rdma_ah_retrieve_dmac(&attr);
	if (dmac)
		memset(dmac, 0, ETH_ALEN);

	ah = pd->device->create_ah(pd, &attr, &udata);

	if (IS_ERR(ah)) {
		ret = PTR_ERR(ah);
		goto err_put;
	}

	ah->device  = pd->device;
	ah->pd      = pd;
	atomic_inc(&pd->usecnt);
	ah->uobject  = uobj;
	uobj->user_handle = cmd.user_handle;
	uobj->object = ah;

	resp.ah_handle = uobj->id;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp)) {
		ret = -EFAULT;
		goto err_copy;
	}

	uobj_put_obj_read(pd);
	uobj_alloc_commit(uobj);

	return in_len;

err_copy:
	rdma_destroy_ah(ah);

err_put:
	uobj_put_obj_read(pd);

err:
	uobj_alloc_abort(uobj);
	return ret;
}

ssize_t ib_uverbs_destroy_ah(struct ib_uverbs_file *file,
			     struct ib_device *ib_dev,
			     const char __user *buf, int in_len, int out_len)
{
	struct ib_uverbs_destroy_ah cmd;
	struct ib_uobject	   *uobj;
	int			    ret;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	uobj  = uobj_get_write(uobj_get_type(ah), cmd.ah_handle,
			       file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	ret = uobj_remove_commit(uobj);
	return ret ?: in_len;
}

ssize_t ib_uverbs_attach_mcast(struct ib_uverbs_file *file,
			       struct ib_device *ib_dev,
			       const char __user *buf, int in_len,
			       int out_len)
{
	struct ib_uverbs_attach_mcast cmd;
	struct ib_qp                 *qp;
	struct ib_uqp_object         *obj;
	struct ib_uverbs_mcast_entry *mcast;
	int                           ret;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	qp = uobj_get_obj_read(qp, cmd.qp_handle, file->ucontext);
	if (!qp)
		return -EINVAL;

	obj = container_of(qp->uobject, struct ib_uqp_object, uevent.uobject);

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
	uobj_put_obj_read(qp);

	return ret ? ret : in_len;
}

ssize_t ib_uverbs_detach_mcast(struct ib_uverbs_file *file,
			       struct ib_device *ib_dev,
			       const char __user *buf, int in_len,
			       int out_len)
{
	struct ib_uverbs_detach_mcast cmd;
	struct ib_uqp_object         *obj;
	struct ib_qp                 *qp;
	struct ib_uverbs_mcast_entry *mcast;
	int                           ret = -EINVAL;
	bool                          found = false;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	qp = uobj_get_obj_read(qp, cmd.qp_handle, file->ucontext);
	if (!qp)
		return -EINVAL;

	obj = container_of(qp->uobject, struct ib_uqp_object, uevent.uobject);
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
	uobj_put_obj_read(qp);
	return ret ? ret : in_len;
}

static int kern_spec_to_ib_spec_action(struct ib_uverbs_flow_spec *kern_spec,
				       union ib_flow_spec *ib_spec)
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
	default:
		return -EINVAL;
	}
	return 0;
}

static size_t kern_spec_filter_sz(struct ib_uverbs_flow_spec_hdr *spec)
{
	/* Returns user space filter size, includes padding */
	return (spec->size - sizeof(struct ib_uverbs_flow_spec_hdr)) / 2;
}

static ssize_t spec_filter_size(void *kern_spec_filter, u16 kern_filter_size,
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

static int kern_spec_to_ib_spec_filter(struct ib_uverbs_flow_spec *kern_spec,
				       union ib_flow_spec *ib_spec)
{
	ssize_t actual_filter_sz;
	ssize_t kern_filter_sz;
	ssize_t ib_filter_sz;
	void *kern_spec_mask;
	void *kern_spec_val;

	if (kern_spec->reserved)
		return -EINVAL;

	ib_spec->type = kern_spec->type;

	kern_filter_sz = kern_spec_filter_sz(&kern_spec->hdr);
	/* User flow spec size must be aligned to 4 bytes */
	if (kern_filter_sz != ALIGN(kern_filter_sz, 4))
		return -EINVAL;

	kern_spec_val = (void *)kern_spec +
		sizeof(struct ib_uverbs_flow_spec_hdr);
	kern_spec_mask = kern_spec_val + kern_filter_sz;
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
	default:
		return -EINVAL;
	}
	return 0;
}

static int kern_spec_to_ib_spec(struct ib_uverbs_flow_spec *kern_spec,
				union ib_flow_spec *ib_spec)
{
	if (kern_spec->reserved)
		return -EINVAL;

	if (kern_spec->type >= IB_FLOW_SPEC_ACTION_TAG)
		return kern_spec_to_ib_spec_action(kern_spec, ib_spec);
	else
		return kern_spec_to_ib_spec_filter(kern_spec, ib_spec);
}

int ib_uverbs_ex_create_wq(struct ib_uverbs_file *file,
			   struct ib_device *ib_dev,
			   struct ib_udata *ucore,
			   struct ib_udata *uhw)
{
	struct ib_uverbs_ex_create_wq	  cmd = {};
	struct ib_uverbs_ex_create_wq_resp resp = {};
	struct ib_uwq_object           *obj;
	int err = 0;
	struct ib_cq *cq;
	struct ib_pd *pd;
	struct ib_wq *wq;
	struct ib_wq_init_attr wq_init_attr = {};
	size_t required_cmd_sz;
	size_t required_resp_len;

	required_cmd_sz = offsetof(typeof(cmd), max_sge) + sizeof(cmd.max_sge);
	required_resp_len = offsetof(typeof(resp), wqn) + sizeof(resp.wqn);

	if (ucore->inlen < required_cmd_sz)
		return -EINVAL;

	if (ucore->outlen < required_resp_len)
		return -ENOSPC;

	if (ucore->inlen > sizeof(cmd) &&
	    !ib_is_udata_cleared(ucore, sizeof(cmd),
				 ucore->inlen - sizeof(cmd)))
		return -EOPNOTSUPP;

	err = ib_copy_from_udata(&cmd, ucore, min(sizeof(cmd), ucore->inlen));
	if (err)
		return err;

	if (cmd.comp_mask)
		return -EOPNOTSUPP;

	obj  = (struct ib_uwq_object *)uobj_alloc(uobj_get_type(wq),
						  file->ucontext);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	pd  = uobj_get_obj_read(pd, cmd.pd_handle, file->ucontext);
	if (!pd) {
		err = -EINVAL;
		goto err_uobj;
	}

	cq = uobj_get_obj_read(cq, cmd.cq_handle, file->ucontext);
	if (!cq) {
		err = -EINVAL;
		goto err_put_pd;
	}

	wq_init_attr.cq = cq;
	wq_init_attr.max_sge = cmd.max_sge;
	wq_init_attr.max_wr = cmd.max_wr;
	wq_init_attr.wq_context = file;
	wq_init_attr.wq_type = cmd.wq_type;
	wq_init_attr.event_handler = ib_uverbs_wq_event_handler;
	if (ucore->inlen >= (offsetof(typeof(cmd), create_flags) +
			     sizeof(cmd.create_flags)))
		wq_init_attr.create_flags = cmd.create_flags;
	obj->uevent.events_reported = 0;
	INIT_LIST_HEAD(&obj->uevent.event_list);
	wq = pd->device->create_wq(pd, &wq_init_attr, uhw);
	if (IS_ERR(wq)) {
		err = PTR_ERR(wq);
		goto err_put_cq;
	}

	wq->uobject = &obj->uevent.uobject;
	obj->uevent.uobject.object = wq;
	wq->wq_type = wq_init_attr.wq_type;
	wq->cq = cq;
	wq->pd = pd;
	wq->device = pd->device;
	wq->wq_context = wq_init_attr.wq_context;
	atomic_set(&wq->usecnt, 0);
	atomic_inc(&pd->usecnt);
	atomic_inc(&cq->usecnt);
	wq->uobject = &obj->uevent.uobject;
	obj->uevent.uobject.object = wq;

	memset(&resp, 0, sizeof(resp));
	resp.wq_handle = obj->uevent.uobject.id;
	resp.max_sge = wq_init_attr.max_sge;
	resp.max_wr = wq_init_attr.max_wr;
	resp.wqn = wq->wq_num;
	resp.response_length = required_resp_len;
	err = ib_copy_to_udata(ucore,
			       &resp, resp.response_length);
	if (err)
		goto err_copy;

	uobj_put_obj_read(pd);
	uobj_put_obj_read(cq);
	uobj_alloc_commit(&obj->uevent.uobject);
	return 0;

err_copy:
	ib_destroy_wq(wq);
err_put_cq:
	uobj_put_obj_read(cq);
err_put_pd:
	uobj_put_obj_read(pd);
err_uobj:
	uobj_alloc_abort(&obj->uevent.uobject);

	return err;
}

int ib_uverbs_ex_destroy_wq(struct ib_uverbs_file *file,
			    struct ib_device *ib_dev,
			    struct ib_udata *ucore,
			    struct ib_udata *uhw)
{
	struct ib_uverbs_ex_destroy_wq	cmd = {};
	struct ib_uverbs_ex_destroy_wq_resp	resp = {};
	struct ib_wq			*wq;
	struct ib_uobject		*uobj;
	struct ib_uwq_object		*obj;
	size_t required_cmd_sz;
	size_t required_resp_len;
	int				ret;

	required_cmd_sz = offsetof(typeof(cmd), wq_handle) + sizeof(cmd.wq_handle);
	required_resp_len = offsetof(typeof(resp), reserved) + sizeof(resp.reserved);

	if (ucore->inlen < required_cmd_sz)
		return -EINVAL;

	if (ucore->outlen < required_resp_len)
		return -ENOSPC;

	if (ucore->inlen > sizeof(cmd) &&
	    !ib_is_udata_cleared(ucore, sizeof(cmd),
				 ucore->inlen - sizeof(cmd)))
		return -EOPNOTSUPP;

	ret = ib_copy_from_udata(&cmd, ucore, min(sizeof(cmd), ucore->inlen));
	if (ret)
		return ret;

	if (cmd.comp_mask)
		return -EOPNOTSUPP;

	resp.response_length = required_resp_len;
	uobj  = uobj_get_write(uobj_get_type(wq), cmd.wq_handle,
			       file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	wq = uobj->object;
	obj = container_of(uobj, struct ib_uwq_object, uevent.uobject);
	/*
	 * Make sure we don't free the memory in remove_commit as we still
	 * needs the uobject memory to create the response.
	 */
	uverbs_uobject_get(uobj);

	ret = uobj_remove_commit(uobj);
	resp.events_reported = obj->uevent.events_reported;
	uverbs_uobject_put(uobj);
	if (ret)
		return ret;

	return ib_copy_to_udata(ucore, &resp, resp.response_length);
}

int ib_uverbs_ex_modify_wq(struct ib_uverbs_file *file,
			   struct ib_device *ib_dev,
			   struct ib_udata *ucore,
			   struct ib_udata *uhw)
{
	struct ib_uverbs_ex_modify_wq cmd = {};
	struct ib_wq *wq;
	struct ib_wq_attr wq_attr = {};
	size_t required_cmd_sz;
	int ret;

	required_cmd_sz = offsetof(typeof(cmd), curr_wq_state) + sizeof(cmd.curr_wq_state);
	if (ucore->inlen < required_cmd_sz)
		return -EINVAL;

	if (ucore->inlen > sizeof(cmd) &&
	    !ib_is_udata_cleared(ucore, sizeof(cmd),
				 ucore->inlen - sizeof(cmd)))
		return -EOPNOTSUPP;

	ret = ib_copy_from_udata(&cmd, ucore, min(sizeof(cmd), ucore->inlen));
	if (ret)
		return ret;

	if (!cmd.attr_mask)
		return -EINVAL;

	if (cmd.attr_mask > (IB_WQ_STATE | IB_WQ_CUR_STATE | IB_WQ_FLAGS))
		return -EINVAL;

	wq = uobj_get_obj_read(wq, cmd.wq_handle, file->ucontext);
	if (!wq)
		return -EINVAL;

	wq_attr.curr_wq_state = cmd.curr_wq_state;
	wq_attr.wq_state = cmd.wq_state;
	if (cmd.attr_mask & IB_WQ_FLAGS) {
		wq_attr.flags = cmd.flags;
		wq_attr.flags_mask = cmd.flags_mask;
	}
	ret = wq->device->modify_wq(wq, &wq_attr, cmd.attr_mask, uhw);
	uobj_put_obj_read(wq);
	return ret;
}

int ib_uverbs_ex_create_rwq_ind_table(struct ib_uverbs_file *file,
				      struct ib_device *ib_dev,
				      struct ib_udata *ucore,
				      struct ib_udata *uhw)
{
	struct ib_uverbs_ex_create_rwq_ind_table	  cmd = {};
	struct ib_uverbs_ex_create_rwq_ind_table_resp  resp = {};
	struct ib_uobject		  *uobj;
	int err = 0;
	struct ib_rwq_ind_table_init_attr init_attr = {};
	struct ib_rwq_ind_table *rwq_ind_tbl;
	struct ib_wq	**wqs = NULL;
	u32 *wqs_handles = NULL;
	struct ib_wq	*wq = NULL;
	int i, j, num_read_wqs;
	u32 num_wq_handles;
	u32 expected_in_size;
	size_t required_cmd_sz_header;
	size_t required_resp_len;

	required_cmd_sz_header = offsetof(typeof(cmd), log_ind_tbl_size) + sizeof(cmd.log_ind_tbl_size);
	required_resp_len = offsetof(typeof(resp), ind_tbl_num) + sizeof(resp.ind_tbl_num);

	if (ucore->inlen < required_cmd_sz_header)
		return -EINVAL;

	if (ucore->outlen < required_resp_len)
		return -ENOSPC;

	err = ib_copy_from_udata(&cmd, ucore, required_cmd_sz_header);
	if (err)
		return err;

	ucore->inbuf += required_cmd_sz_header;
	ucore->inlen -= required_cmd_sz_header;

	if (cmd.comp_mask)
		return -EOPNOTSUPP;

	if (cmd.log_ind_tbl_size > IB_USER_VERBS_MAX_LOG_IND_TBL_SIZE)
		return -EINVAL;

	num_wq_handles = 1 << cmd.log_ind_tbl_size;
	expected_in_size = num_wq_handles * sizeof(__u32);
	if (num_wq_handles == 1)
		/* input size for wq handles is u64 aligned */
		expected_in_size += sizeof(__u32);

	if (ucore->inlen < expected_in_size)
		return -EINVAL;

	if (ucore->inlen > expected_in_size &&
	    !ib_is_udata_cleared(ucore, expected_in_size,
				 ucore->inlen - expected_in_size))
		return -EOPNOTSUPP;

	wqs_handles = kcalloc(num_wq_handles, sizeof(*wqs_handles),
			      GFP_KERNEL);
	if (!wqs_handles)
		return -ENOMEM;

	err = ib_copy_from_udata(wqs_handles, ucore,
				 num_wq_handles * sizeof(__u32));
	if (err)
		goto err_free;

	wqs = kcalloc(num_wq_handles, sizeof(*wqs), GFP_KERNEL);
	if (!wqs) {
		err = -ENOMEM;
		goto  err_free;
	}

	for (num_read_wqs = 0; num_read_wqs < num_wq_handles;
			num_read_wqs++) {
		wq = uobj_get_obj_read(wq, wqs_handles[num_read_wqs],
				       file->ucontext);
		if (!wq) {
			err = -EINVAL;
			goto put_wqs;
		}

		wqs[num_read_wqs] = wq;
	}

	uobj  = uobj_alloc(uobj_get_type(rwq_ind_table), file->ucontext);
	if (IS_ERR(uobj)) {
		err = PTR_ERR(uobj);
		goto put_wqs;
	}

	init_attr.log_ind_tbl_size = cmd.log_ind_tbl_size;
	init_attr.ind_tbl = wqs;
	rwq_ind_tbl = ib_dev->create_rwq_ind_table(ib_dev, &init_attr, uhw);

	if (IS_ERR(rwq_ind_tbl)) {
		err = PTR_ERR(rwq_ind_tbl);
		goto err_uobj;
	}

	rwq_ind_tbl->ind_tbl = wqs;
	rwq_ind_tbl->log_ind_tbl_size = init_attr.log_ind_tbl_size;
	rwq_ind_tbl->uobject = uobj;
	uobj->object = rwq_ind_tbl;
	rwq_ind_tbl->device = ib_dev;
	atomic_set(&rwq_ind_tbl->usecnt, 0);

	for (i = 0; i < num_wq_handles; i++)
		atomic_inc(&wqs[i]->usecnt);

	resp.ind_tbl_handle = uobj->id;
	resp.ind_tbl_num = rwq_ind_tbl->ind_tbl_num;
	resp.response_length = required_resp_len;

	err = ib_copy_to_udata(ucore,
			       &resp, resp.response_length);
	if (err)
		goto err_copy;

	kfree(wqs_handles);

	for (j = 0; j < num_read_wqs; j++)
		uobj_put_obj_read(wqs[j]);

	uobj_alloc_commit(uobj);
	return 0;

err_copy:
	ib_destroy_rwq_ind_table(rwq_ind_tbl);
err_uobj:
	uobj_alloc_abort(uobj);
put_wqs:
	for (j = 0; j < num_read_wqs; j++)
		uobj_put_obj_read(wqs[j]);
err_free:
	kfree(wqs_handles);
	kfree(wqs);
	return err;
}

int ib_uverbs_ex_destroy_rwq_ind_table(struct ib_uverbs_file *file,
				       struct ib_device *ib_dev,
				       struct ib_udata *ucore,
				       struct ib_udata *uhw)
{
	struct ib_uverbs_ex_destroy_rwq_ind_table	cmd = {};
	struct ib_uobject		*uobj;
	int			ret;
	size_t required_cmd_sz;

	required_cmd_sz = offsetof(typeof(cmd), ind_tbl_handle) + sizeof(cmd.ind_tbl_handle);

	if (ucore->inlen < required_cmd_sz)
		return -EINVAL;

	if (ucore->inlen > sizeof(cmd) &&
	    !ib_is_udata_cleared(ucore, sizeof(cmd),
				 ucore->inlen - sizeof(cmd)))
		return -EOPNOTSUPP;

	ret = ib_copy_from_udata(&cmd, ucore, min(sizeof(cmd), ucore->inlen));
	if (ret)
		return ret;

	if (cmd.comp_mask)
		return -EOPNOTSUPP;

	uobj  = uobj_get_write(uobj_get_type(rwq_ind_table), cmd.ind_tbl_handle,
			       file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	return uobj_remove_commit(uobj);
}

int ib_uverbs_ex_create_flow(struct ib_uverbs_file *file,
			     struct ib_device *ib_dev,
			     struct ib_udata *ucore,
			     struct ib_udata *uhw)
{
	struct ib_uverbs_create_flow	  cmd;
	struct ib_uverbs_create_flow_resp resp;
	struct ib_uobject		  *uobj;
	struct ib_flow			  *flow_id;
	struct ib_uverbs_flow_attr	  *kern_flow_attr;
	struct ib_flow_attr		  *flow_attr;
	struct ib_qp			  *qp;
	int err = 0;
	void *kern_spec;
	void *ib_spec;
	int i;

	if (ucore->inlen < sizeof(cmd))
		return -EINVAL;

	if (ucore->outlen < sizeof(resp))
		return -ENOSPC;

	err = ib_copy_from_udata(&cmd, ucore, sizeof(cmd));
	if (err)
		return err;

	ucore->inbuf += sizeof(cmd);
	ucore->inlen -= sizeof(cmd);

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

	if (cmd.flow_attr.size > ucore->inlen ||
	    cmd.flow_attr.size >
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

		memcpy(kern_flow_attr, &cmd.flow_attr, sizeof(*kern_flow_attr));
		err = ib_copy_from_udata(kern_flow_attr + 1, ucore,
					 cmd.flow_attr.size);
		if (err)
			goto err_free_attr;
	} else {
		kern_flow_attr = &cmd.flow_attr;
	}

	uobj  = uobj_alloc(uobj_get_type(flow), file->ucontext);
	if (IS_ERR(uobj)) {
		err = PTR_ERR(uobj);
		goto err_free_attr;
	}

	qp = uobj_get_obj_read(qp, cmd.qp_handle, file->ucontext);
	if (!qp) {
		err = -EINVAL;
		goto err_uobj;
	}

	flow_attr = kzalloc(sizeof(*flow_attr) + cmd.flow_attr.num_of_specs *
			    sizeof(union ib_flow_spec), GFP_KERNEL);
	if (!flow_attr) {
		err = -ENOMEM;
		goto err_put;
	}

	flow_attr->type = kern_flow_attr->type;
	flow_attr->priority = kern_flow_attr->priority;
	flow_attr->num_of_specs = kern_flow_attr->num_of_specs;
	flow_attr->port = kern_flow_attr->port;
	flow_attr->flags = kern_flow_attr->flags;
	flow_attr->size = sizeof(*flow_attr);

	kern_spec = kern_flow_attr + 1;
	ib_spec = flow_attr + 1;
	for (i = 0; i < flow_attr->num_of_specs &&
	     cmd.flow_attr.size > offsetof(struct ib_uverbs_flow_spec, reserved) &&
	     cmd.flow_attr.size >=
	     ((struct ib_uverbs_flow_spec *)kern_spec)->size; i++) {
		err = kern_spec_to_ib_spec(kern_spec, ib_spec);
		if (err)
			goto err_free;
		flow_attr->size +=
			((union ib_flow_spec *) ib_spec)->size;
		cmd.flow_attr.size -= ((struct ib_uverbs_flow_spec *)kern_spec)->size;
		kern_spec += ((struct ib_uverbs_flow_spec *) kern_spec)->size;
		ib_spec += ((union ib_flow_spec *) ib_spec)->size;
	}
	if (cmd.flow_attr.size || (i != flow_attr->num_of_specs)) {
		pr_warn("create flow failed, flow %d: %d bytes left from uverb cmd\n",
			i, cmd.flow_attr.size);
		err = -EINVAL;
		goto err_free;
	}
	flow_id = ib_create_flow(qp, flow_attr, IB_FLOW_DOMAIN_USER);
	if (IS_ERR(flow_id)) {
		err = PTR_ERR(flow_id);
		goto err_free;
	}
	flow_id->uobject = uobj;
	uobj->object = flow_id;

	memset(&resp, 0, sizeof(resp));
	resp.flow_handle = uobj->id;

	err = ib_copy_to_udata(ucore,
			       &resp, sizeof(resp));
	if (err)
		goto err_copy;

	uobj_put_obj_read(qp);
	uobj_alloc_commit(uobj);
	kfree(flow_attr);
	if (cmd.flow_attr.num_of_specs)
		kfree(kern_flow_attr);
	return 0;
err_copy:
	ib_destroy_flow(flow_id);
err_free:
	kfree(flow_attr);
err_put:
	uobj_put_obj_read(qp);
err_uobj:
	uobj_alloc_abort(uobj);
err_free_attr:
	if (cmd.flow_attr.num_of_specs)
		kfree(kern_flow_attr);
	return err;
}

int ib_uverbs_ex_destroy_flow(struct ib_uverbs_file *file,
			      struct ib_device *ib_dev,
			      struct ib_udata *ucore,
			      struct ib_udata *uhw)
{
	struct ib_uverbs_destroy_flow	cmd;
	struct ib_uobject		*uobj;
	int				ret;

	if (ucore->inlen < sizeof(cmd))
		return -EINVAL;

	ret = ib_copy_from_udata(&cmd, ucore, sizeof(cmd));
	if (ret)
		return ret;

	if (cmd.comp_mask)
		return -EINVAL;

	uobj  = uobj_get_write(uobj_get_type(flow), cmd.flow_handle,
			       file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	ret = uobj_remove_commit(uobj);
	return ret;
}

static int __uverbs_create_xsrq(struct ib_uverbs_file *file,
				struct ib_device *ib_dev,
				struct ib_uverbs_create_xsrq *cmd,
				struct ib_udata *udata)
{
	struct ib_uverbs_create_srq_resp resp;
	struct ib_usrq_object           *obj;
	struct ib_pd                    *pd;
	struct ib_srq                   *srq;
	struct ib_uobject               *uninitialized_var(xrcd_uobj);
	struct ib_srq_init_attr          attr;
	int ret;

	obj  = (struct ib_usrq_object *)uobj_alloc(uobj_get_type(srq),
						   file->ucontext);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	if (cmd->srq_type == IB_SRQT_XRC) {
		xrcd_uobj = uobj_get_read(uobj_get_type(xrcd), cmd->xrcd_handle,
					  file->ucontext);
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

		attr.ext.xrc.cq  = uobj_get_obj_read(cq, cmd->cq_handle,
						     file->ucontext);
		if (!attr.ext.xrc.cq) {
			ret = -EINVAL;
			goto err_put_xrcd;
		}
	}

	pd  = uobj_get_obj_read(pd, cmd->pd_handle, file->ucontext);
	if (!pd) {
		ret = -EINVAL;
		goto err_put_cq;
	}

	attr.event_handler  = ib_uverbs_srq_event_handler;
	attr.srq_context    = file;
	attr.srq_type       = cmd->srq_type;
	attr.attr.max_wr    = cmd->max_wr;
	attr.attr.max_sge   = cmd->max_sge;
	attr.attr.srq_limit = cmd->srq_limit;

	obj->uevent.events_reported = 0;
	INIT_LIST_HEAD(&obj->uevent.event_list);

	srq = pd->device->create_srq(pd, &attr, udata);
	if (IS_ERR(srq)) {
		ret = PTR_ERR(srq);
		goto err_put;
	}

	srq->device        = pd->device;
	srq->pd            = pd;
	srq->srq_type	   = cmd->srq_type;
	srq->uobject       = &obj->uevent.uobject;
	srq->event_handler = attr.event_handler;
	srq->srq_context   = attr.srq_context;

	if (cmd->srq_type == IB_SRQT_XRC) {
		srq->ext.xrc.cq   = attr.ext.xrc.cq;
		srq->ext.xrc.xrcd = attr.ext.xrc.xrcd;
		atomic_inc(&attr.ext.xrc.cq->usecnt);
		atomic_inc(&attr.ext.xrc.xrcd->usecnt);
	}

	atomic_inc(&pd->usecnt);
	atomic_set(&srq->usecnt, 0);

	obj->uevent.uobject.object = srq;
	obj->uevent.uobject.user_handle = cmd->user_handle;

	memset(&resp, 0, sizeof resp);
	resp.srq_handle = obj->uevent.uobject.id;
	resp.max_wr     = attr.attr.max_wr;
	resp.max_sge    = attr.attr.max_sge;
	if (cmd->srq_type == IB_SRQT_XRC)
		resp.srqn = srq->ext.xrc.srq_num;

	if (copy_to_user((void __user *) (unsigned long) cmd->response,
			 &resp, sizeof resp)) {
		ret = -EFAULT;
		goto err_copy;
	}

	if (cmd->srq_type == IB_SRQT_XRC) {
		uobj_put_read(xrcd_uobj);
		uobj_put_obj_read(attr.ext.xrc.cq);
	}
	uobj_put_obj_read(pd);
	uobj_alloc_commit(&obj->uevent.uobject);

	return 0;

err_copy:
	ib_destroy_srq(srq);

err_put:
	uobj_put_obj_read(pd);

err_put_cq:
	if (cmd->srq_type == IB_SRQT_XRC)
		uobj_put_obj_read(attr.ext.xrc.cq);

err_put_xrcd:
	if (cmd->srq_type == IB_SRQT_XRC) {
		atomic_dec(&obj->uxrcd->refcnt);
		uobj_put_read(xrcd_uobj);
	}

err:
	uobj_alloc_abort(&obj->uevent.uobject);
	return ret;
}

ssize_t ib_uverbs_create_srq(struct ib_uverbs_file *file,
			     struct ib_device *ib_dev,
			     const char __user *buf, int in_len,
			     int out_len)
{
	struct ib_uverbs_create_srq      cmd;
	struct ib_uverbs_create_xsrq     xcmd;
	struct ib_uverbs_create_srq_resp resp;
	struct ib_udata                  udata;
	int ret;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	xcmd.response	 = cmd.response;
	xcmd.user_handle = cmd.user_handle;
	xcmd.srq_type	 = IB_SRQT_BASIC;
	xcmd.pd_handle	 = cmd.pd_handle;
	xcmd.max_wr	 = cmd.max_wr;
	xcmd.max_sge	 = cmd.max_sge;
	xcmd.srq_limit	 = cmd.srq_limit;

	INIT_UDATA(&udata, buf + sizeof cmd,
		   (unsigned long) cmd.response + sizeof resp,
		   in_len - sizeof cmd - sizeof(struct ib_uverbs_cmd_hdr),
		   out_len - sizeof resp);

	ret = __uverbs_create_xsrq(file, ib_dev, &xcmd, &udata);
	if (ret)
		return ret;

	return in_len;
}

ssize_t ib_uverbs_create_xsrq(struct ib_uverbs_file *file,
			      struct ib_device *ib_dev,
			      const char __user *buf, int in_len, int out_len)
{
	struct ib_uverbs_create_xsrq     cmd;
	struct ib_uverbs_create_srq_resp resp;
	struct ib_udata                  udata;
	int ret;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	INIT_UDATA(&udata, buf + sizeof cmd,
		   (unsigned long) cmd.response + sizeof resp,
		   in_len - sizeof cmd - sizeof(struct ib_uverbs_cmd_hdr),
		   out_len - sizeof resp);

	ret = __uverbs_create_xsrq(file, ib_dev, &cmd, &udata);
	if (ret)
		return ret;

	return in_len;
}

ssize_t ib_uverbs_modify_srq(struct ib_uverbs_file *file,
			     struct ib_device *ib_dev,
			     const char __user *buf, int in_len,
			     int out_len)
{
	struct ib_uverbs_modify_srq cmd;
	struct ib_udata             udata;
	struct ib_srq              *srq;
	struct ib_srq_attr          attr;
	int                         ret;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	INIT_UDATA(&udata, buf + sizeof cmd, NULL, in_len - sizeof cmd,
		   out_len);

	srq = uobj_get_obj_read(srq, cmd.srq_handle, file->ucontext);
	if (!srq)
		return -EINVAL;

	attr.max_wr    = cmd.max_wr;
	attr.srq_limit = cmd.srq_limit;

	ret = srq->device->modify_srq(srq, &attr, cmd.attr_mask, &udata);

	uobj_put_obj_read(srq);

	return ret ? ret : in_len;
}

ssize_t ib_uverbs_query_srq(struct ib_uverbs_file *file,
			    struct ib_device *ib_dev,
			    const char __user *buf,
			    int in_len, int out_len)
{
	struct ib_uverbs_query_srq      cmd;
	struct ib_uverbs_query_srq_resp resp;
	struct ib_srq_attr              attr;
	struct ib_srq                   *srq;
	int                             ret;

	if (out_len < sizeof resp)
		return -ENOSPC;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	srq = uobj_get_obj_read(srq, cmd.srq_handle, file->ucontext);
	if (!srq)
		return -EINVAL;

	ret = ib_query_srq(srq, &attr);

	uobj_put_obj_read(srq);

	if (ret)
		return ret;

	memset(&resp, 0, sizeof resp);

	resp.max_wr    = attr.max_wr;
	resp.max_sge   = attr.max_sge;
	resp.srq_limit = attr.srq_limit;

	if (copy_to_user((void __user *) (unsigned long) cmd.response,
			 &resp, sizeof resp))
		return -EFAULT;

	return in_len;
}

ssize_t ib_uverbs_destroy_srq(struct ib_uverbs_file *file,
			      struct ib_device *ib_dev,
			      const char __user *buf, int in_len,
			      int out_len)
{
	struct ib_uverbs_destroy_srq      cmd;
	struct ib_uverbs_destroy_srq_resp resp;
	struct ib_uobject		 *uobj;
	struct ib_srq               	 *srq;
	struct ib_uevent_object        	 *obj;
	int                         	  ret = -EINVAL;
	enum ib_srq_type		  srq_type;

	if (copy_from_user(&cmd, buf, sizeof cmd))
		return -EFAULT;

	uobj  = uobj_get_write(uobj_get_type(srq), cmd.srq_handle,
			       file->ucontext);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	srq = uobj->object;
	obj = container_of(uobj, struct ib_uevent_object, uobject);
	srq_type = srq->srq_type;
	/*
	 * Make sure we don't free the memory in remove_commit as we still
	 * needs the uobject memory to create the response.
	 */
	uverbs_uobject_get(uobj);

	memset(&resp, 0, sizeof(resp));

	ret = uobj_remove_commit(uobj);
	if (ret) {
		uverbs_uobject_put(uobj);
		return ret;
	}
	resp.events_reported = obj->events_reported;
	uverbs_uobject_put(uobj);
	if (copy_to_user((void __user *)(unsigned long)cmd.response,
			 &resp, sizeof(resp)))
		return -EFAULT;

	return in_len;
}

int ib_uverbs_ex_query_device(struct ib_uverbs_file *file,
			      struct ib_device *ib_dev,
			      struct ib_udata *ucore,
			      struct ib_udata *uhw)
{
	struct ib_uverbs_ex_query_device_resp resp = { {0} };
	struct ib_uverbs_ex_query_device  cmd;
	struct ib_device_attr attr = {0};
	int err;

	if (ucore->inlen < sizeof(cmd))
		return -EINVAL;

	err = ib_copy_from_udata(&cmd, ucore, sizeof(cmd));
	if (err)
		return err;

	if (cmd.comp_mask)
		return -EINVAL;

	if (cmd.reserved)
		return -EINVAL;

	resp.response_length = offsetof(typeof(resp), odp_caps);

	if (ucore->outlen < resp.response_length)
		return -ENOSPC;

	err = ib_dev->query_device(ib_dev, &attr, uhw);
	if (err)
		return err;

	copy_query_dev_fields(file, ib_dev, &resp.base, &attr);

	if (ucore->outlen < resp.response_length + sizeof(resp.odp_caps))
		goto end;

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	resp.odp_caps.general_caps = attr.odp_caps.general_caps;
	resp.odp_caps.per_transport_caps.rc_odp_caps =
		attr.odp_caps.per_transport_caps.rc_odp_caps;
	resp.odp_caps.per_transport_caps.uc_odp_caps =
		attr.odp_caps.per_transport_caps.uc_odp_caps;
	resp.odp_caps.per_transport_caps.ud_odp_caps =
		attr.odp_caps.per_transport_caps.ud_odp_caps;
#endif
	resp.response_length += sizeof(resp.odp_caps);

	if (ucore->outlen < resp.response_length + sizeof(resp.timestamp_mask))
		goto end;

	resp.timestamp_mask = attr.timestamp_mask;
	resp.response_length += sizeof(resp.timestamp_mask);

	if (ucore->outlen < resp.response_length + sizeof(resp.hca_core_clock))
		goto end;

	resp.hca_core_clock = attr.hca_core_clock;
	resp.response_length += sizeof(resp.hca_core_clock);

	if (ucore->outlen < resp.response_length + sizeof(resp.device_cap_flags_ex))
		goto end;

	resp.device_cap_flags_ex = attr.device_cap_flags;
	resp.response_length += sizeof(resp.device_cap_flags_ex);

	if (ucore->outlen < resp.response_length + sizeof(resp.rss_caps))
		goto end;

	resp.rss_caps.supported_qpts = attr.rss_caps.supported_qpts;
	resp.rss_caps.max_rwq_indirection_tables =
		attr.rss_caps.max_rwq_indirection_tables;
	resp.rss_caps.max_rwq_indirection_table_size =
		attr.rss_caps.max_rwq_indirection_table_size;

	resp.response_length += sizeof(resp.rss_caps);

	if (ucore->outlen < resp.response_length + sizeof(resp.max_wq_type_rq))
		goto end;

	resp.max_wq_type_rq = attr.max_wq_type_rq;
	resp.response_length += sizeof(resp.max_wq_type_rq);

	if (ucore->outlen < resp.response_length + sizeof(resp.raw_packet_caps))
		goto end;

	resp.raw_packet_caps = attr.raw_packet_caps;
	resp.response_length += sizeof(resp.raw_packet_caps);
end:
	err = ib_copy_to_udata(ucore, &resp, resp.response_length);
	return err;
}
