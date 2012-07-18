/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies.
 * All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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

#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mlx4/cmd.h>
#include <linux/mlx4/qp.h>
#include <linux/if_ether.h>

#include "mlx4.h"
#include "fw.h"

#define MLX4_MAC_VALID		(1ull << 63)

struct mac_res {
	struct list_head list;
	u64 mac;
	u8 port;
};

struct res_common {
	struct list_head	list;
	struct rb_node		node;
	u64		        res_id;
	int			owner;
	int			state;
	int			from_state;
	int			to_state;
	int			removing;
};

enum {
	RES_ANY_BUSY = 1
};

struct res_gid {
	struct list_head	list;
	u8			gid[16];
	enum mlx4_protocol	prot;
	enum mlx4_steer_type	steer;
};

enum res_qp_states {
	RES_QP_BUSY = RES_ANY_BUSY,

	/* QP number was allocated */
	RES_QP_RESERVED,

	/* ICM memory for QP context was mapped */
	RES_QP_MAPPED,

	/* QP is in hw ownership */
	RES_QP_HW
};

struct res_qp {
	struct res_common	com;
	struct res_mtt	       *mtt;
	struct res_cq	       *rcq;
	struct res_cq	       *scq;
	struct res_srq	       *srq;
	struct list_head	mcg_list;
	spinlock_t		mcg_spl;
	int			local_qpn;
};

enum res_mtt_states {
	RES_MTT_BUSY = RES_ANY_BUSY,
	RES_MTT_ALLOCATED,
};

static inline const char *mtt_states_str(enum res_mtt_states state)
{
	switch (state) {
	case RES_MTT_BUSY: return "RES_MTT_BUSY";
	case RES_MTT_ALLOCATED: return "RES_MTT_ALLOCATED";
	default: return "Unknown";
	}
}

struct res_mtt {
	struct res_common	com;
	int			order;
	atomic_t		ref_count;
};

enum res_mpt_states {
	RES_MPT_BUSY = RES_ANY_BUSY,
	RES_MPT_RESERVED,
	RES_MPT_MAPPED,
	RES_MPT_HW,
};

struct res_mpt {
	struct res_common	com;
	struct res_mtt	       *mtt;
	int			key;
};

enum res_eq_states {
	RES_EQ_BUSY = RES_ANY_BUSY,
	RES_EQ_RESERVED,
	RES_EQ_HW,
};

struct res_eq {
	struct res_common	com;
	struct res_mtt	       *mtt;
};

enum res_cq_states {
	RES_CQ_BUSY = RES_ANY_BUSY,
	RES_CQ_ALLOCATED,
	RES_CQ_HW,
};

struct res_cq {
	struct res_common	com;
	struct res_mtt	       *mtt;
	atomic_t		ref_count;
};

enum res_srq_states {
	RES_SRQ_BUSY = RES_ANY_BUSY,
	RES_SRQ_ALLOCATED,
	RES_SRQ_HW,
};

struct res_srq {
	struct res_common	com;
	struct res_mtt	       *mtt;
	struct res_cq	       *cq;
	atomic_t		ref_count;
};

enum res_counter_states {
	RES_COUNTER_BUSY = RES_ANY_BUSY,
	RES_COUNTER_ALLOCATED,
};

struct res_counter {
	struct res_common	com;
	int			port;
};

enum res_xrcdn_states {
	RES_XRCD_BUSY = RES_ANY_BUSY,
	RES_XRCD_ALLOCATED,
};

struct res_xrcdn {
	struct res_common	com;
	int			port;
};

enum res_fs_rule_states {
	RES_FS_RULE_BUSY = RES_ANY_BUSY,
	RES_FS_RULE_ALLOCATED,
};

struct res_fs_rule {
	struct res_common	com;
};

static void *res_tracker_lookup(struct rb_root *root, u64 res_id)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct res_common *res = container_of(node, struct res_common,
						      node);

		if (res_id < res->res_id)
			node = node->rb_left;
		else if (res_id > res->res_id)
			node = node->rb_right;
		else
			return res;
	}
	return NULL;
}

static int res_tracker_insert(struct rb_root *root, struct res_common *res)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	/* Figure out where to put new node */
	while (*new) {
		struct res_common *this = container_of(*new, struct res_common,
						       node);

		parent = *new;
		if (res->res_id < this->res_id)
			new = &((*new)->rb_left);
		else if (res->res_id > this->res_id)
			new = &((*new)->rb_right);
		else
			return -EEXIST;
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&res->node, parent, new);
	rb_insert_color(&res->node, root);

	return 0;
}

/* For Debug uses */
static const char *ResourceType(enum mlx4_resource rt)
{
	switch (rt) {
	case RES_QP: return "RES_QP";
	case RES_CQ: return "RES_CQ";
	case RES_SRQ: return "RES_SRQ";
	case RES_MPT: return "RES_MPT";
	case RES_MTT: return "RES_MTT";
	case RES_MAC: return  "RES_MAC";
	case RES_EQ: return "RES_EQ";
	case RES_COUNTER: return "RES_COUNTER";
	case RES_FS_RULE: return "RES_FS_RULE";
	case RES_XRCD: return "RES_XRCD";
	default: return "Unknown resource type !!!";
	};
}

int mlx4_init_resource_tracker(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i;
	int t;

	priv->mfunc.master.res_tracker.slave_list =
		kzalloc(dev->num_slaves * sizeof(struct slave_list),
			GFP_KERNEL);
	if (!priv->mfunc.master.res_tracker.slave_list)
		return -ENOMEM;

	for (i = 0 ; i < dev->num_slaves; i++) {
		for (t = 0; t < MLX4_NUM_OF_RESOURCE_TYPE; ++t)
			INIT_LIST_HEAD(&priv->mfunc.master.res_tracker.
				       slave_list[i].res_list[t]);
		mutex_init(&priv->mfunc.master.res_tracker.slave_list[i].mutex);
	}

	mlx4_dbg(dev, "Started init_resource_tracker: %ld slaves\n",
		 dev->num_slaves);
	for (i = 0 ; i < MLX4_NUM_OF_RESOURCE_TYPE; i++)
		priv->mfunc.master.res_tracker.res_tree[i] = RB_ROOT;

	spin_lock_init(&priv->mfunc.master.res_tracker.lock);
	return 0 ;
}

void mlx4_free_resource_tracker(struct mlx4_dev *dev,
				enum mlx4_res_tracker_free_type type)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i;

	if (priv->mfunc.master.res_tracker.slave_list) {
		if (type != RES_TR_FREE_STRUCTS_ONLY)
			for (i = 0 ; i < dev->num_slaves; i++)
				if (type == RES_TR_FREE_ALL ||
				    dev->caps.function != i)
					mlx4_delete_all_resources_for_slave(dev, i);

		if (type != RES_TR_FREE_SLAVES_ONLY) {
			kfree(priv->mfunc.master.res_tracker.slave_list);
			priv->mfunc.master.res_tracker.slave_list = NULL;
		}
	}
}

static void update_ud_gid(struct mlx4_dev *dev,
			  struct mlx4_qp_context *qp_ctx, u8 slave)
{
	u32 ts = (be32_to_cpu(qp_ctx->flags) >> 16) & 0xff;

	if (MLX4_QP_ST_UD == ts)
		qp_ctx->pri_path.mgid_index = 0x80 | slave;

	mlx4_dbg(dev, "slave %d, new gid index: 0x%x ",
		slave, qp_ctx->pri_path.mgid_index);
}

static int mpt_mask(struct mlx4_dev *dev)
{
	return dev->caps.num_mpts - 1;
}

static void *find_res(struct mlx4_dev *dev, int res_id,
		      enum mlx4_resource type)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	return res_tracker_lookup(&priv->mfunc.master.res_tracker.res_tree[type],
				  res_id);
}

static int get_res(struct mlx4_dev *dev, int slave, u64 res_id,
		   enum mlx4_resource type,
		   void *res)
{
	struct res_common *r;
	int err = 0;

	spin_lock_irq(mlx4_tlock(dev));
	r = find_res(dev, res_id, type);
	if (!r) {
		err = -ENONET;
		goto exit;
	}

	if (r->state == RES_ANY_BUSY) {
		err = -EBUSY;
		goto exit;
	}

	if (r->owner != slave) {
		err = -EPERM;
		goto exit;
	}

	r->from_state = r->state;
	r->state = RES_ANY_BUSY;
	mlx4_dbg(dev, "res %s id 0x%llx to busy\n",
		 ResourceType(type), r->res_id);

	if (res)
		*((struct res_common **)res) = r;

exit:
	spin_unlock_irq(mlx4_tlock(dev));
	return err;
}

int mlx4_get_slave_from_resource_id(struct mlx4_dev *dev,
				    enum mlx4_resource type,
				    u64 res_id, int *slave)
{

	struct res_common *r;
	int err = -ENOENT;
	int id = res_id;

	if (type == RES_QP)
		id &= 0x7fffff;
	spin_lock(mlx4_tlock(dev));

	r = find_res(dev, id, type);
	if (r) {
		*slave = r->owner;
		err = 0;
	}
	spin_unlock(mlx4_tlock(dev));

	return err;
}

static void put_res(struct mlx4_dev *dev, int slave, u64 res_id,
		    enum mlx4_resource type)
{
	struct res_common *r;

	spin_lock_irq(mlx4_tlock(dev));
	r = find_res(dev, res_id, type);
	if (r)
		r->state = r->from_state;
	spin_unlock_irq(mlx4_tlock(dev));
}

static struct res_common *alloc_qp_tr(int id)
{
	struct res_qp *ret;

	ret = kzalloc(sizeof *ret, GFP_KERNEL);
	if (!ret)
		return NULL;

	ret->com.res_id = id;
	ret->com.state = RES_QP_RESERVED;
	ret->local_qpn = id;
	INIT_LIST_HEAD(&ret->mcg_list);
	spin_lock_init(&ret->mcg_spl);

	return &ret->com;
}

static struct res_common *alloc_mtt_tr(int id, int order)
{
	struct res_mtt *ret;

	ret = kzalloc(sizeof *ret, GFP_KERNEL);
	if (!ret)
		return NULL;

	ret->com.res_id = id;
	ret->order = order;
	ret->com.state = RES_MTT_ALLOCATED;
	atomic_set(&ret->ref_count, 0);

	return &ret->com;
}

static struct res_common *alloc_mpt_tr(int id, int key)
{
	struct res_mpt *ret;

	ret = kzalloc(sizeof *ret, GFP_KERNEL);
	if (!ret)
		return NULL;

	ret->com.res_id = id;
	ret->com.state = RES_MPT_RESERVED;
	ret->key = key;

	return &ret->com;
}

static struct res_common *alloc_eq_tr(int id)
{
	struct res_eq *ret;

	ret = kzalloc(sizeof *ret, GFP_KERNEL);
	if (!ret)
		return NULL;

	ret->com.res_id = id;
	ret->com.state = RES_EQ_RESERVED;

	return &ret->com;
}

static struct res_common *alloc_cq_tr(int id)
{
	struct res_cq *ret;

	ret = kzalloc(sizeof *ret, GFP_KERNEL);
	if (!ret)
		return NULL;

	ret->com.res_id = id;
	ret->com.state = RES_CQ_ALLOCATED;
	atomic_set(&ret->ref_count, 0);

	return &ret->com;
}

static struct res_common *alloc_srq_tr(int id)
{
	struct res_srq *ret;

	ret = kzalloc(sizeof *ret, GFP_KERNEL);
	if (!ret)
		return NULL;

	ret->com.res_id = id;
	ret->com.state = RES_SRQ_ALLOCATED;
	atomic_set(&ret->ref_count, 0);

	return &ret->com;
}

static struct res_common *alloc_counter_tr(int id)
{
	struct res_counter *ret;

	ret = kzalloc(sizeof *ret, GFP_KERNEL);
	if (!ret)
		return NULL;

	ret->com.res_id = id;
	ret->com.state = RES_COUNTER_ALLOCATED;

	return &ret->com;
}

static struct res_common *alloc_xrcdn_tr(int id)
{
	struct res_xrcdn *ret;

	ret = kzalloc(sizeof *ret, GFP_KERNEL);
	if (!ret)
		return NULL;

	ret->com.res_id = id;
	ret->com.state = RES_XRCD_ALLOCATED;

	return &ret->com;
}

static struct res_common *alloc_fs_rule_tr(u64 id)
{
	struct res_fs_rule *ret;

	ret = kzalloc(sizeof *ret, GFP_KERNEL);
	if (!ret)
		return NULL;

	ret->com.res_id = id;
	ret->com.state = RES_FS_RULE_ALLOCATED;

	return &ret->com;
}

static struct res_common *alloc_tr(u64 id, enum mlx4_resource type, int slave,
				   int extra)
{
	struct res_common *ret;

	switch (type) {
	case RES_QP:
		ret = alloc_qp_tr(id);
		break;
	case RES_MPT:
		ret = alloc_mpt_tr(id, extra);
		break;
	case RES_MTT:
		ret = alloc_mtt_tr(id, extra);
		break;
	case RES_EQ:
		ret = alloc_eq_tr(id);
		break;
	case RES_CQ:
		ret = alloc_cq_tr(id);
		break;
	case RES_SRQ:
		ret = alloc_srq_tr(id);
		break;
	case RES_MAC:
		printk(KERN_ERR "implementation missing\n");
		return NULL;
	case RES_COUNTER:
		ret = alloc_counter_tr(id);
		break;
	case RES_XRCD:
		ret = alloc_xrcdn_tr(id);
		break;
	case RES_FS_RULE:
		ret = alloc_fs_rule_tr(id);
		break;
	default:
		return NULL;
	}
	if (ret)
		ret->owner = slave;

	return ret;
}

static int add_res_range(struct mlx4_dev *dev, int slave, u64 base, int count,
			 enum mlx4_resource type, int extra)
{
	int i;
	int err;
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct res_common **res_arr;
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct rb_root *root = &tracker->res_tree[type];

	res_arr = kzalloc(count * sizeof *res_arr, GFP_KERNEL);
	if (!res_arr)
		return -ENOMEM;

	for (i = 0; i < count; ++i) {
		res_arr[i] = alloc_tr(base + i, type, slave, extra);
		if (!res_arr[i]) {
			for (--i; i >= 0; --i)
				kfree(res_arr[i]);

			kfree(res_arr);
			return -ENOMEM;
		}
	}

	spin_lock_irq(mlx4_tlock(dev));
	for (i = 0; i < count; ++i) {
		if (find_res(dev, base + i, type)) {
			err = -EEXIST;
			goto undo;
		}
		err = res_tracker_insert(root, res_arr[i]);
		if (err)
			goto undo;
		list_add_tail(&res_arr[i]->list,
			      &tracker->slave_list[slave].res_list[type]);
	}
	spin_unlock_irq(mlx4_tlock(dev));
	kfree(res_arr);

	return 0;

undo:
	for (--i; i >= base; --i)
		rb_erase(&res_arr[i]->node, root);

	spin_unlock_irq(mlx4_tlock(dev));

	for (i = 0; i < count; ++i)
		kfree(res_arr[i]);

	kfree(res_arr);

	return err;
}

static int remove_qp_ok(struct res_qp *res)
{
	if (res->com.state == RES_QP_BUSY)
		return -EBUSY;
	else if (res->com.state != RES_QP_RESERVED)
		return -EPERM;

	return 0;
}

static int remove_mtt_ok(struct res_mtt *res, int order)
{
	if (res->com.state == RES_MTT_BUSY ||
	    atomic_read(&res->ref_count)) {
		printk(KERN_DEBUG "%s-%d: state %s, ref_count %d\n",
		       __func__, __LINE__,
		       mtt_states_str(res->com.state),
		       atomic_read(&res->ref_count));
		return -EBUSY;
	} else if (res->com.state != RES_MTT_ALLOCATED)
		return -EPERM;
	else if (res->order != order)
		return -EINVAL;

	return 0;
}

static int remove_mpt_ok(struct res_mpt *res)
{
	if (res->com.state == RES_MPT_BUSY)
		return -EBUSY;
	else if (res->com.state != RES_MPT_RESERVED)
		return -EPERM;

	return 0;
}

static int remove_eq_ok(struct res_eq *res)
{
	if (res->com.state == RES_MPT_BUSY)
		return -EBUSY;
	else if (res->com.state != RES_MPT_RESERVED)
		return -EPERM;

	return 0;
}

static int remove_counter_ok(struct res_counter *res)
{
	if (res->com.state == RES_COUNTER_BUSY)
		return -EBUSY;
	else if (res->com.state != RES_COUNTER_ALLOCATED)
		return -EPERM;

	return 0;
}

static int remove_xrcdn_ok(struct res_xrcdn *res)
{
	if (res->com.state == RES_XRCD_BUSY)
		return -EBUSY;
	else if (res->com.state != RES_XRCD_ALLOCATED)
		return -EPERM;

	return 0;
}

static int remove_fs_rule_ok(struct res_fs_rule *res)
{
	if (res->com.state == RES_FS_RULE_BUSY)
		return -EBUSY;
	else if (res->com.state != RES_FS_RULE_ALLOCATED)
		return -EPERM;

	return 0;
}

static int remove_cq_ok(struct res_cq *res)
{
	if (res->com.state == RES_CQ_BUSY)
		return -EBUSY;
	else if (res->com.state != RES_CQ_ALLOCATED)
		return -EPERM;

	return 0;
}

static int remove_srq_ok(struct res_srq *res)
{
	if (res->com.state == RES_SRQ_BUSY)
		return -EBUSY;
	else if (res->com.state != RES_SRQ_ALLOCATED)
		return -EPERM;

	return 0;
}

static int remove_ok(struct res_common *res, enum mlx4_resource type, int extra)
{
	switch (type) {
	case RES_QP:
		return remove_qp_ok((struct res_qp *)res);
	case RES_CQ:
		return remove_cq_ok((struct res_cq *)res);
	case RES_SRQ:
		return remove_srq_ok((struct res_srq *)res);
	case RES_MPT:
		return remove_mpt_ok((struct res_mpt *)res);
	case RES_MTT:
		return remove_mtt_ok((struct res_mtt *)res, extra);
	case RES_MAC:
		return -ENOSYS;
	case RES_EQ:
		return remove_eq_ok((struct res_eq *)res);
	case RES_COUNTER:
		return remove_counter_ok((struct res_counter *)res);
	case RES_XRCD:
		return remove_xrcdn_ok((struct res_xrcdn *)res);
	case RES_FS_RULE:
		return remove_fs_rule_ok((struct res_fs_rule *)res);
	default:
		return -EINVAL;
	}
}

static int rem_res_range(struct mlx4_dev *dev, int slave, u64 base, int count,
			 enum mlx4_resource type, int extra)
{
	u64 i;
	int err;
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct res_common *r;

	spin_lock_irq(mlx4_tlock(dev));
	for (i = base; i < base + count; ++i) {
		r = res_tracker_lookup(&tracker->res_tree[type], i);
		if (!r) {
			err = -ENOENT;
			goto out;
		}
		if (r->owner != slave) {
			err = -EPERM;
			goto out;
		}
		err = remove_ok(r, type, extra);
		if (err)
			goto out;
	}

	for (i = base; i < base + count; ++i) {
		r = res_tracker_lookup(&tracker->res_tree[type], i);
		rb_erase(&r->node, &tracker->res_tree[type]);
		list_del(&r->list);
		kfree(r);
	}
	err = 0;

out:
	spin_unlock_irq(mlx4_tlock(dev));

	return err;
}

static int qp_res_start_move_to(struct mlx4_dev *dev, int slave, int qpn,
				enum res_qp_states state, struct res_qp **qp,
				int alloc)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct res_qp *r;
	int err = 0;

	spin_lock_irq(mlx4_tlock(dev));
	r = res_tracker_lookup(&tracker->res_tree[RES_QP], qpn);
	if (!r)
		err = -ENOENT;
	else if (r->com.owner != slave)
		err = -EPERM;
	else {
		switch (state) {
		case RES_QP_BUSY:
			mlx4_dbg(dev, "%s: failed RES_QP, 0x%llx\n",
				 __func__, r->com.res_id);
			err = -EBUSY;
			break;

		case RES_QP_RESERVED:
			if (r->com.state == RES_QP_MAPPED && !alloc)
				break;

			mlx4_dbg(dev, "failed RES_QP, 0x%llx\n", r->com.res_id);
			err = -EINVAL;
			break;

		case RES_QP_MAPPED:
			if ((r->com.state == RES_QP_RESERVED && alloc) ||
			    r->com.state == RES_QP_HW)
				break;
			else {
				mlx4_dbg(dev, "failed RES_QP, 0x%llx\n",
					  r->com.res_id);
				err = -EINVAL;
			}

			break;

		case RES_QP_HW:
			if (r->com.state != RES_QP_MAPPED)
				err = -EINVAL;
			break;
		default:
			err = -EINVAL;
		}

		if (!err) {
			r->com.from_state = r->com.state;
			r->com.to_state = state;
			r->com.state = RES_QP_BUSY;
			if (qp)
				*qp = r;
		}
	}

	spin_unlock_irq(mlx4_tlock(dev));

	return err;
}

static int mr_res_start_move_to(struct mlx4_dev *dev, int slave, int index,
				enum res_mpt_states state, struct res_mpt **mpt)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct res_mpt *r;
	int err = 0;

	spin_lock_irq(mlx4_tlock(dev));
	r = res_tracker_lookup(&tracker->res_tree[RES_MPT], index);
	if (!r)
		err = -ENOENT;
	else if (r->com.owner != slave)
		err = -EPERM;
	else {
		switch (state) {
		case RES_MPT_BUSY:
			err = -EINVAL;
			break;

		case RES_MPT_RESERVED:
			if (r->com.state != RES_MPT_MAPPED)
				err = -EINVAL;
			break;

		case RES_MPT_MAPPED:
			if (r->com.state != RES_MPT_RESERVED &&
			    r->com.state != RES_MPT_HW)
				err = -EINVAL;
			break;

		case RES_MPT_HW:
			if (r->com.state != RES_MPT_MAPPED)
				err = -EINVAL;
			break;
		default:
			err = -EINVAL;
		}

		if (!err) {
			r->com.from_state = r->com.state;
			r->com.to_state = state;
			r->com.state = RES_MPT_BUSY;
			if (mpt)
				*mpt = r;
		}
	}

	spin_unlock_irq(mlx4_tlock(dev));

	return err;
}

static int eq_res_start_move_to(struct mlx4_dev *dev, int slave, int index,
				enum res_eq_states state, struct res_eq **eq)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct res_eq *r;
	int err = 0;

	spin_lock_irq(mlx4_tlock(dev));
	r = res_tracker_lookup(&tracker->res_tree[RES_EQ], index);
	if (!r)
		err = -ENOENT;
	else if (r->com.owner != slave)
		err = -EPERM;
	else {
		switch (state) {
		case RES_EQ_BUSY:
			err = -EINVAL;
			break;

		case RES_EQ_RESERVED:
			if (r->com.state != RES_EQ_HW)
				err = -EINVAL;
			break;

		case RES_EQ_HW:
			if (r->com.state != RES_EQ_RESERVED)
				err = -EINVAL;
			break;

		default:
			err = -EINVAL;
		}

		if (!err) {
			r->com.from_state = r->com.state;
			r->com.to_state = state;
			r->com.state = RES_EQ_BUSY;
			if (eq)
				*eq = r;
		}
	}

	spin_unlock_irq(mlx4_tlock(dev));

	return err;
}

static int cq_res_start_move_to(struct mlx4_dev *dev, int slave, int cqn,
				enum res_cq_states state, struct res_cq **cq)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct res_cq *r;
	int err;

	spin_lock_irq(mlx4_tlock(dev));
	r = res_tracker_lookup(&tracker->res_tree[RES_CQ], cqn);
	if (!r)
		err = -ENOENT;
	else if (r->com.owner != slave)
		err = -EPERM;
	else {
		switch (state) {
		case RES_CQ_BUSY:
			err = -EBUSY;
			break;

		case RES_CQ_ALLOCATED:
			if (r->com.state != RES_CQ_HW)
				err = -EINVAL;
			else if (atomic_read(&r->ref_count))
				err = -EBUSY;
			else
				err = 0;
			break;

		case RES_CQ_HW:
			if (r->com.state != RES_CQ_ALLOCATED)
				err = -EINVAL;
			else
				err = 0;
			break;

		default:
			err = -EINVAL;
		}

		if (!err) {
			r->com.from_state = r->com.state;
			r->com.to_state = state;
			r->com.state = RES_CQ_BUSY;
			if (cq)
				*cq = r;
		}
	}

	spin_unlock_irq(mlx4_tlock(dev));

	return err;
}

static int srq_res_start_move_to(struct mlx4_dev *dev, int slave, int index,
				 enum res_cq_states state, struct res_srq **srq)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct res_srq *r;
	int err = 0;

	spin_lock_irq(mlx4_tlock(dev));
	r = res_tracker_lookup(&tracker->res_tree[RES_SRQ], index);
	if (!r)
		err = -ENOENT;
	else if (r->com.owner != slave)
		err = -EPERM;
	else {
		switch (state) {
		case RES_SRQ_BUSY:
			err = -EINVAL;
			break;

		case RES_SRQ_ALLOCATED:
			if (r->com.state != RES_SRQ_HW)
				err = -EINVAL;
			else if (atomic_read(&r->ref_count))
				err = -EBUSY;
			break;

		case RES_SRQ_HW:
			if (r->com.state != RES_SRQ_ALLOCATED)
				err = -EINVAL;
			break;

		default:
			err = -EINVAL;
		}

		if (!err) {
			r->com.from_state = r->com.state;
			r->com.to_state = state;
			r->com.state = RES_SRQ_BUSY;
			if (srq)
				*srq = r;
		}
	}

	spin_unlock_irq(mlx4_tlock(dev));

	return err;
}

static void res_abort_move(struct mlx4_dev *dev, int slave,
			   enum mlx4_resource type, int id)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct res_common *r;

	spin_lock_irq(mlx4_tlock(dev));
	r = res_tracker_lookup(&tracker->res_tree[type], id);
	if (r && (r->owner == slave))
		r->state = r->from_state;
	spin_unlock_irq(mlx4_tlock(dev));
}

static void res_end_move(struct mlx4_dev *dev, int slave,
			 enum mlx4_resource type, int id)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct res_common *r;

	spin_lock_irq(mlx4_tlock(dev));
	r = res_tracker_lookup(&tracker->res_tree[type], id);
	if (r && (r->owner == slave))
		r->state = r->to_state;
	spin_unlock_irq(mlx4_tlock(dev));
}

static int valid_reserved(struct mlx4_dev *dev, int slave, int qpn)
{
	return mlx4_is_qp_reserved(dev, qpn);
}

static int qp_alloc_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			u64 in_param, u64 *out_param)
{
	int err;
	int count;
	int align;
	int base;
	int qpn;

	switch (op) {
	case RES_OP_RESERVE:
		count = get_param_l(&in_param);
		align = get_param_h(&in_param);
		err = __mlx4_qp_reserve_range(dev, count, align, &base);
		if (err)
			return err;

		err = add_res_range(dev, slave, base, count, RES_QP, 0);
		if (err) {
			__mlx4_qp_release_range(dev, base, count);
			return err;
		}
		set_param_l(out_param, base);
		break;
	case RES_OP_MAP_ICM:
		qpn = get_param_l(&in_param) & 0x7fffff;
		if (valid_reserved(dev, slave, qpn)) {
			err = add_res_range(dev, slave, qpn, 1, RES_QP, 0);
			if (err)
				return err;
		}

		err = qp_res_start_move_to(dev, slave, qpn, RES_QP_MAPPED,
					   NULL, 1);
		if (err)
			return err;

		if (!valid_reserved(dev, slave, qpn)) {
			err = __mlx4_qp_alloc_icm(dev, qpn);
			if (err) {
				res_abort_move(dev, slave, RES_QP, qpn);
				return err;
			}
		}

		res_end_move(dev, slave, RES_QP, qpn);
		break;

	default:
		err = -EINVAL;
		break;
	}
	return err;
}

static int mtt_alloc_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			 u64 in_param, u64 *out_param)
{
	int err = -EINVAL;
	int base;
	int order;

	if (op != RES_OP_RESERVE_AND_MAP)
		return err;

	order = get_param_l(&in_param);
	base = __mlx4_alloc_mtt_range(dev, order);
	if (base == -1)
		return -ENOMEM;

	err = add_res_range(dev, slave, base, 1, RES_MTT, order);
	if (err)
		__mlx4_free_mtt_range(dev, base, order);
	else
		set_param_l(out_param, base);

	return err;
}

static int mpt_alloc_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			 u64 in_param, u64 *out_param)
{
	int err = -EINVAL;
	int index;
	int id;
	struct res_mpt *mpt;

	switch (op) {
	case RES_OP_RESERVE:
		index = __mlx4_mr_reserve(dev);
		if (index == -1)
			break;
		id = index & mpt_mask(dev);

		err = add_res_range(dev, slave, id, 1, RES_MPT, index);
		if (err) {
			__mlx4_mr_release(dev, index);
			break;
		}
		set_param_l(out_param, index);
		break;
	case RES_OP_MAP_ICM:
		index = get_param_l(&in_param);
		id = index & mpt_mask(dev);
		err = mr_res_start_move_to(dev, slave, id,
					   RES_MPT_MAPPED, &mpt);
		if (err)
			return err;

		err = __mlx4_mr_alloc_icm(dev, mpt->key);
		if (err) {
			res_abort_move(dev, slave, RES_MPT, id);
			return err;
		}

		res_end_move(dev, slave, RES_MPT, id);
		break;
	}
	return err;
}

static int cq_alloc_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			u64 in_param, u64 *out_param)
{
	int cqn;
	int err;

	switch (op) {
	case RES_OP_RESERVE_AND_MAP:
		err = __mlx4_cq_alloc_icm(dev, &cqn);
		if (err)
			break;

		err = add_res_range(dev, slave, cqn, 1, RES_CQ, 0);
		if (err) {
			__mlx4_cq_free_icm(dev, cqn);
			break;
		}

		set_param_l(out_param, cqn);
		break;

	default:
		err = -EINVAL;
	}

	return err;
}

static int srq_alloc_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			 u64 in_param, u64 *out_param)
{
	int srqn;
	int err;

	switch (op) {
	case RES_OP_RESERVE_AND_MAP:
		err = __mlx4_srq_alloc_icm(dev, &srqn);
		if (err)
			break;

		err = add_res_range(dev, slave, srqn, 1, RES_SRQ, 0);
		if (err) {
			__mlx4_srq_free_icm(dev, srqn);
			break;
		}

		set_param_l(out_param, srqn);
		break;

	default:
		err = -EINVAL;
	}

	return err;
}

static int mac_add_to_slave(struct mlx4_dev *dev, int slave, u64 mac, int port)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct mac_res *res;

	res = kzalloc(sizeof *res, GFP_KERNEL);
	if (!res)
		return -ENOMEM;
	res->mac = mac;
	res->port = (u8) port;
	list_add_tail(&res->list,
		      &tracker->slave_list[slave].res_list[RES_MAC]);
	return 0;
}

static void mac_del_from_slave(struct mlx4_dev *dev, int slave, u64 mac,
			       int port)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct list_head *mac_list =
		&tracker->slave_list[slave].res_list[RES_MAC];
	struct mac_res *res, *tmp;

	list_for_each_entry_safe(res, tmp, mac_list, list) {
		if (res->mac == mac && res->port == (u8) port) {
			list_del(&res->list);
			kfree(res);
			break;
		}
	}
}

static void rem_slave_macs(struct mlx4_dev *dev, int slave)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct list_head *mac_list =
		&tracker->slave_list[slave].res_list[RES_MAC];
	struct mac_res *res, *tmp;

	list_for_each_entry_safe(res, tmp, mac_list, list) {
		list_del(&res->list);
		__mlx4_unregister_mac(dev, res->port, res->mac);
		kfree(res);
	}
}

static int mac_alloc_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			 u64 in_param, u64 *out_param)
{
	int err = -EINVAL;
	int port;
	u64 mac;

	if (op != RES_OP_RESERVE_AND_MAP)
		return err;

	port = get_param_l(out_param);
	mac = in_param;

	err = __mlx4_register_mac(dev, port, mac);
	if (err >= 0) {
		set_param_l(out_param, err);
		err = 0;
	}

	if (!err) {
		err = mac_add_to_slave(dev, slave, mac, port);
		if (err)
			__mlx4_unregister_mac(dev, port, mac);
	}
	return err;
}

static int vlan_alloc_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			 u64 in_param, u64 *out_param)
{
	return 0;
}

static int counter_alloc_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			     u64 in_param, u64 *out_param)
{
	u32 index;
	int err;

	if (op != RES_OP_RESERVE)
		return -EINVAL;

	err = __mlx4_counter_alloc(dev, &index);
	if (err)
		return err;

	err = add_res_range(dev, slave, index, 1, RES_COUNTER, 0);
	if (err)
		__mlx4_counter_free(dev, index);
	else
		set_param_l(out_param, index);

	return err;
}

static int xrcdn_alloc_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			   u64 in_param, u64 *out_param)
{
	u32 xrcdn;
	int err;

	if (op != RES_OP_RESERVE)
		return -EINVAL;

	err = __mlx4_xrcd_alloc(dev, &xrcdn);
	if (err)
		return err;

	err = add_res_range(dev, slave, xrcdn, 1, RES_XRCD, 0);
	if (err)
		__mlx4_xrcd_free(dev, xrcdn);
	else
		set_param_l(out_param, xrcdn);

	return err;
}

int mlx4_ALLOC_RES_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd)
{
	int err;
	int alop = vhcr->op_modifier;

	switch (vhcr->in_modifier) {
	case RES_QP:
		err = qp_alloc_res(dev, slave, vhcr->op_modifier, alop,
				   vhcr->in_param, &vhcr->out_param);
		break;

	case RES_MTT:
		err = mtt_alloc_res(dev, slave, vhcr->op_modifier, alop,
				    vhcr->in_param, &vhcr->out_param);
		break;

	case RES_MPT:
		err = mpt_alloc_res(dev, slave, vhcr->op_modifier, alop,
				    vhcr->in_param, &vhcr->out_param);
		break;

	case RES_CQ:
		err = cq_alloc_res(dev, slave, vhcr->op_modifier, alop,
				   vhcr->in_param, &vhcr->out_param);
		break;

	case RES_SRQ:
		err = srq_alloc_res(dev, slave, vhcr->op_modifier, alop,
				    vhcr->in_param, &vhcr->out_param);
		break;

	case RES_MAC:
		err = mac_alloc_res(dev, slave, vhcr->op_modifier, alop,
				    vhcr->in_param, &vhcr->out_param);
		break;

	case RES_VLAN:
		err = vlan_alloc_res(dev, slave, vhcr->op_modifier, alop,
				    vhcr->in_param, &vhcr->out_param);
		break;

	case RES_COUNTER:
		err = counter_alloc_res(dev, slave, vhcr->op_modifier, alop,
					vhcr->in_param, &vhcr->out_param);
		break;

	case RES_XRCD:
		err = xrcdn_alloc_res(dev, slave, vhcr->op_modifier, alop,
				      vhcr->in_param, &vhcr->out_param);
		break;

	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int qp_free_res(struct mlx4_dev *dev, int slave, int op, int cmd,
		       u64 in_param)
{
	int err;
	int count;
	int base;
	int qpn;

	switch (op) {
	case RES_OP_RESERVE:
		base = get_param_l(&in_param) & 0x7fffff;
		count = get_param_h(&in_param);
		err = rem_res_range(dev, slave, base, count, RES_QP, 0);
		if (err)
			break;
		__mlx4_qp_release_range(dev, base, count);
		break;
	case RES_OP_MAP_ICM:
		qpn = get_param_l(&in_param) & 0x7fffff;
		err = qp_res_start_move_to(dev, slave, qpn, RES_QP_RESERVED,
					   NULL, 0);
		if (err)
			return err;

		if (!valid_reserved(dev, slave, qpn))
			__mlx4_qp_free_icm(dev, qpn);

		res_end_move(dev, slave, RES_QP, qpn);

		if (valid_reserved(dev, slave, qpn))
			err = rem_res_range(dev, slave, qpn, 1, RES_QP, 0);
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

static int mtt_free_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			u64 in_param, u64 *out_param)
{
	int err = -EINVAL;
	int base;
	int order;

	if (op != RES_OP_RESERVE_AND_MAP)
		return err;

	base = get_param_l(&in_param);
	order = get_param_h(&in_param);
	err = rem_res_range(dev, slave, base, 1, RES_MTT, order);
	if (!err)
		__mlx4_free_mtt_range(dev, base, order);
	return err;
}

static int mpt_free_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			u64 in_param)
{
	int err = -EINVAL;
	int index;
	int id;
	struct res_mpt *mpt;

	switch (op) {
	case RES_OP_RESERVE:
		index = get_param_l(&in_param);
		id = index & mpt_mask(dev);
		err = get_res(dev, slave, id, RES_MPT, &mpt);
		if (err)
			break;
		index = mpt->key;
		put_res(dev, slave, id, RES_MPT);

		err = rem_res_range(dev, slave, id, 1, RES_MPT, 0);
		if (err)
			break;
		__mlx4_mr_release(dev, index);
		break;
	case RES_OP_MAP_ICM:
			index = get_param_l(&in_param);
			id = index & mpt_mask(dev);
			err = mr_res_start_move_to(dev, slave, id,
						   RES_MPT_RESERVED, &mpt);
			if (err)
				return err;

			__mlx4_mr_free_icm(dev, mpt->key);
			res_end_move(dev, slave, RES_MPT, id);
			return err;
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

static int cq_free_res(struct mlx4_dev *dev, int slave, int op, int cmd,
		       u64 in_param, u64 *out_param)
{
	int cqn;
	int err;

	switch (op) {
	case RES_OP_RESERVE_AND_MAP:
		cqn = get_param_l(&in_param);
		err = rem_res_range(dev, slave, cqn, 1, RES_CQ, 0);
		if (err)
			break;

		__mlx4_cq_free_icm(dev, cqn);
		break;

	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int srq_free_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			u64 in_param, u64 *out_param)
{
	int srqn;
	int err;

	switch (op) {
	case RES_OP_RESERVE_AND_MAP:
		srqn = get_param_l(&in_param);
		err = rem_res_range(dev, slave, srqn, 1, RES_SRQ, 0);
		if (err)
			break;

		__mlx4_srq_free_icm(dev, srqn);
		break;

	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int mac_free_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			    u64 in_param, u64 *out_param)
{
	int port;
	int err = 0;

	switch (op) {
	case RES_OP_RESERVE_AND_MAP:
		port = get_param_l(out_param);
		mac_del_from_slave(dev, slave, in_param, port);
		__mlx4_unregister_mac(dev, port, in_param);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;

}

static int vlan_free_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			    u64 in_param, u64 *out_param)
{
	return 0;
}

static int counter_free_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			    u64 in_param, u64 *out_param)
{
	int index;
	int err;

	if (op != RES_OP_RESERVE)
		return -EINVAL;

	index = get_param_l(&in_param);
	err = rem_res_range(dev, slave, index, 1, RES_COUNTER, 0);
	if (err)
		return err;

	__mlx4_counter_free(dev, index);

	return err;
}

static int xrcdn_free_res(struct mlx4_dev *dev, int slave, int op, int cmd,
			  u64 in_param, u64 *out_param)
{
	int xrcdn;
	int err;

	if (op != RES_OP_RESERVE)
		return -EINVAL;

	xrcdn = get_param_l(&in_param);
	err = rem_res_range(dev, slave, xrcdn, 1, RES_XRCD, 0);
	if (err)
		return err;

	__mlx4_xrcd_free(dev, xrcdn);

	return err;
}

int mlx4_FREE_RES_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd)
{
	int err = -EINVAL;
	int alop = vhcr->op_modifier;

	switch (vhcr->in_modifier) {
	case RES_QP:
		err = qp_free_res(dev, slave, vhcr->op_modifier, alop,
				  vhcr->in_param);
		break;

	case RES_MTT:
		err = mtt_free_res(dev, slave, vhcr->op_modifier, alop,
				   vhcr->in_param, &vhcr->out_param);
		break;

	case RES_MPT:
		err = mpt_free_res(dev, slave, vhcr->op_modifier, alop,
				   vhcr->in_param);
		break;

	case RES_CQ:
		err = cq_free_res(dev, slave, vhcr->op_modifier, alop,
				  vhcr->in_param, &vhcr->out_param);
		break;

	case RES_SRQ:
		err = srq_free_res(dev, slave, vhcr->op_modifier, alop,
				   vhcr->in_param, &vhcr->out_param);
		break;

	case RES_MAC:
		err = mac_free_res(dev, slave, vhcr->op_modifier, alop,
				   vhcr->in_param, &vhcr->out_param);
		break;

	case RES_VLAN:
		err = vlan_free_res(dev, slave, vhcr->op_modifier, alop,
				   vhcr->in_param, &vhcr->out_param);
		break;

	case RES_COUNTER:
		err = counter_free_res(dev, slave, vhcr->op_modifier, alop,
				       vhcr->in_param, &vhcr->out_param);
		break;

	case RES_XRCD:
		err = xrcdn_free_res(dev, slave, vhcr->op_modifier, alop,
				     vhcr->in_param, &vhcr->out_param);

	default:
		break;
	}
	return err;
}

/* ugly but other choices are uglier */
static int mr_phys_mpt(struct mlx4_mpt_entry *mpt)
{
	return (be32_to_cpu(mpt->flags) >> 9) & 1;
}

static int mr_get_mtt_addr(struct mlx4_mpt_entry *mpt)
{
	return (int)be64_to_cpu(mpt->mtt_addr) & 0xfffffff8;
}

static int mr_get_mtt_size(struct mlx4_mpt_entry *mpt)
{
	return be32_to_cpu(mpt->mtt_sz);
}

static int qp_get_mtt_addr(struct mlx4_qp_context *qpc)
{
	return be32_to_cpu(qpc->mtt_base_addr_l) & 0xfffffff8;
}

static int srq_get_mtt_addr(struct mlx4_srq_context *srqc)
{
	return be32_to_cpu(srqc->mtt_base_addr_l) & 0xfffffff8;
}

static int qp_get_mtt_size(struct mlx4_qp_context *qpc)
{
	int page_shift = (qpc->log_page_size & 0x3f) + 12;
	int log_sq_size = (qpc->sq_size_stride >> 3) & 0xf;
	int log_sq_sride = qpc->sq_size_stride & 7;
	int log_rq_size = (qpc->rq_size_stride >> 3) & 0xf;
	int log_rq_stride = qpc->rq_size_stride & 7;
	int srq = (be32_to_cpu(qpc->srqn) >> 24) & 1;
	int rss = (be32_to_cpu(qpc->flags) >> 13) & 1;
	int xrc = (be32_to_cpu(qpc->local_qpn) >> 23) & 1;
	int sq_size;
	int rq_size;
	int total_pages;
	int total_mem;
	int page_offset = (be32_to_cpu(qpc->params2) >> 6) & 0x3f;

	sq_size = 1 << (log_sq_size + log_sq_sride + 4);
	rq_size = (srq|rss|xrc) ? 0 : (1 << (log_rq_size + log_rq_stride + 4));
	total_mem = sq_size + rq_size;
	total_pages =
		roundup_pow_of_two((total_mem + (page_offset << 6)) >>
				   page_shift);

	return total_pages;
}

static int check_mtt_range(struct mlx4_dev *dev, int slave, int start,
			   int size, struct res_mtt *mtt)
{
	int res_start = mtt->com.res_id;
	int res_size = (1 << mtt->order);

	if (start < res_start || start + size > res_start + res_size)
		return -EPERM;
	return 0;
}

int mlx4_SW2HW_MPT_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd)
{
	int err;
	int index = vhcr->in_modifier;
	struct res_mtt *mtt;
	struct res_mpt *mpt;
	int mtt_base = mr_get_mtt_addr(inbox->buf) / dev->caps.mtt_entry_sz;
	int phys;
	int id;

	id = index & mpt_mask(dev);
	err = mr_res_start_move_to(dev, slave, id, RES_MPT_HW, &mpt);
	if (err)
		return err;

	phys = mr_phys_mpt(inbox->buf);
	if (!phys) {
		err = get_res(dev, slave, mtt_base, RES_MTT, &mtt);
		if (err)
			goto ex_abort;

		err = check_mtt_range(dev, slave, mtt_base,
				      mr_get_mtt_size(inbox->buf), mtt);
		if (err)
			goto ex_put;

		mpt->mtt = mtt;
	}

	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
	if (err)
		goto ex_put;

	if (!phys) {
		atomic_inc(&mtt->ref_count);
		put_res(dev, slave, mtt->com.res_id, RES_MTT);
	}

	res_end_move(dev, slave, RES_MPT, id);
	return 0;

ex_put:
	if (!phys)
		put_res(dev, slave, mtt->com.res_id, RES_MTT);
ex_abort:
	res_abort_move(dev, slave, RES_MPT, id);

	return err;
}

int mlx4_HW2SW_MPT_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd)
{
	int err;
	int index = vhcr->in_modifier;
	struct res_mpt *mpt;
	int id;

	id = index & mpt_mask(dev);
	err = mr_res_start_move_to(dev, slave, id, RES_MPT_MAPPED, &mpt);
	if (err)
		return err;

	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
	if (err)
		goto ex_abort;

	if (mpt->mtt)
		atomic_dec(&mpt->mtt->ref_count);

	res_end_move(dev, slave, RES_MPT, id);
	return 0;

ex_abort:
	res_abort_move(dev, slave, RES_MPT, id);

	return err;
}

int mlx4_QUERY_MPT_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd)
{
	int err;
	int index = vhcr->in_modifier;
	struct res_mpt *mpt;
	int id;

	id = index & mpt_mask(dev);
	err = get_res(dev, slave, id, RES_MPT, &mpt);
	if (err)
		return err;

	if (mpt->com.from_state != RES_MPT_HW) {
		err = -EBUSY;
		goto out;
	}

	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);

out:
	put_res(dev, slave, id, RES_MPT);
	return err;
}

static int qp_get_rcqn(struct mlx4_qp_context *qpc)
{
	return be32_to_cpu(qpc->cqn_recv) & 0xffffff;
}

static int qp_get_scqn(struct mlx4_qp_context *qpc)
{
	return be32_to_cpu(qpc->cqn_send) & 0xffffff;
}

static u32 qp_get_srqn(struct mlx4_qp_context *qpc)
{
	return be32_to_cpu(qpc->srqn) & 0x1ffffff;
}

int mlx4_RST2INIT_QP_wrapper(struct mlx4_dev *dev, int slave,
			     struct mlx4_vhcr *vhcr,
			     struct mlx4_cmd_mailbox *inbox,
			     struct mlx4_cmd_mailbox *outbox,
			     struct mlx4_cmd_info *cmd)
{
	int err;
	int qpn = vhcr->in_modifier & 0x7fffff;
	struct res_mtt *mtt;
	struct res_qp *qp;
	struct mlx4_qp_context *qpc = inbox->buf + 8;
	int mtt_base = qp_get_mtt_addr(qpc) / dev->caps.mtt_entry_sz;
	int mtt_size = qp_get_mtt_size(qpc);
	struct res_cq *rcq;
	struct res_cq *scq;
	int rcqn = qp_get_rcqn(qpc);
	int scqn = qp_get_scqn(qpc);
	u32 srqn = qp_get_srqn(qpc) & 0xffffff;
	int use_srq = (qp_get_srqn(qpc) >> 24) & 1;
	struct res_srq *srq;
	int local_qpn = be32_to_cpu(qpc->local_qpn) & 0xffffff;

	err = qp_res_start_move_to(dev, slave, qpn, RES_QP_HW, &qp, 0);
	if (err)
		return err;
	qp->local_qpn = local_qpn;

	err = get_res(dev, slave, mtt_base, RES_MTT, &mtt);
	if (err)
		goto ex_abort;

	err = check_mtt_range(dev, slave, mtt_base, mtt_size, mtt);
	if (err)
		goto ex_put_mtt;

	err = get_res(dev, slave, rcqn, RES_CQ, &rcq);
	if (err)
		goto ex_put_mtt;

	if (scqn != rcqn) {
		err = get_res(dev, slave, scqn, RES_CQ, &scq);
		if (err)
			goto ex_put_rcq;
	} else
		scq = rcq;

	if (use_srq) {
		err = get_res(dev, slave, srqn, RES_SRQ, &srq);
		if (err)
			goto ex_put_scq;
	}

	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
	if (err)
		goto ex_put_srq;
	atomic_inc(&mtt->ref_count);
	qp->mtt = mtt;
	atomic_inc(&rcq->ref_count);
	qp->rcq = rcq;
	atomic_inc(&scq->ref_count);
	qp->scq = scq;

	if (scqn != rcqn)
		put_res(dev, slave, scqn, RES_CQ);

	if (use_srq) {
		atomic_inc(&srq->ref_count);
		put_res(dev, slave, srqn, RES_SRQ);
		qp->srq = srq;
	}
	put_res(dev, slave, rcqn, RES_CQ);
	put_res(dev, slave, mtt_base, RES_MTT);
	res_end_move(dev, slave, RES_QP, qpn);

	return 0;

ex_put_srq:
	if (use_srq)
		put_res(dev, slave, srqn, RES_SRQ);
ex_put_scq:
	if (scqn != rcqn)
		put_res(dev, slave, scqn, RES_CQ);
ex_put_rcq:
	put_res(dev, slave, rcqn, RES_CQ);
ex_put_mtt:
	put_res(dev, slave, mtt_base, RES_MTT);
ex_abort:
	res_abort_move(dev, slave, RES_QP, qpn);

	return err;
}

static int eq_get_mtt_addr(struct mlx4_eq_context *eqc)
{
	return be32_to_cpu(eqc->mtt_base_addr_l) & 0xfffffff8;
}

static int eq_get_mtt_size(struct mlx4_eq_context *eqc)
{
	int log_eq_size = eqc->log_eq_size & 0x1f;
	int page_shift = (eqc->log_page_size & 0x3f) + 12;

	if (log_eq_size + 5 < page_shift)
		return 1;

	return 1 << (log_eq_size + 5 - page_shift);
}

static int cq_get_mtt_addr(struct mlx4_cq_context *cqc)
{
	return be32_to_cpu(cqc->mtt_base_addr_l) & 0xfffffff8;
}

static int cq_get_mtt_size(struct mlx4_cq_context *cqc)
{
	int log_cq_size = (be32_to_cpu(cqc->logsize_usrpage) >> 24) & 0x1f;
	int page_shift = (cqc->log_page_size & 0x3f) + 12;

	if (log_cq_size + 5 < page_shift)
		return 1;

	return 1 << (log_cq_size + 5 - page_shift);
}

int mlx4_SW2HW_EQ_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd)
{
	int err;
	int eqn = vhcr->in_modifier;
	int res_id = (slave << 8) | eqn;
	struct mlx4_eq_context *eqc = inbox->buf;
	int mtt_base = eq_get_mtt_addr(eqc) / dev->caps.mtt_entry_sz;
	int mtt_size = eq_get_mtt_size(eqc);
	struct res_eq *eq;
	struct res_mtt *mtt;

	err = add_res_range(dev, slave, res_id, 1, RES_EQ, 0);
	if (err)
		return err;
	err = eq_res_start_move_to(dev, slave, res_id, RES_EQ_HW, &eq);
	if (err)
		goto out_add;

	err = get_res(dev, slave, mtt_base, RES_MTT, &mtt);
	if (err)
		goto out_move;

	err = check_mtt_range(dev, slave, mtt_base, mtt_size, mtt);
	if (err)
		goto out_put;

	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
	if (err)
		goto out_put;

	atomic_inc(&mtt->ref_count);
	eq->mtt = mtt;
	put_res(dev, slave, mtt->com.res_id, RES_MTT);
	res_end_move(dev, slave, RES_EQ, res_id);
	return 0;

out_put:
	put_res(dev, slave, mtt->com.res_id, RES_MTT);
out_move:
	res_abort_move(dev, slave, RES_EQ, res_id);
out_add:
	rem_res_range(dev, slave, res_id, 1, RES_EQ, 0);
	return err;
}

static int get_containing_mtt(struct mlx4_dev *dev, int slave, int start,
			      int len, struct res_mtt **res)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct res_mtt *mtt;
	int err = -EINVAL;

	spin_lock_irq(mlx4_tlock(dev));
	list_for_each_entry(mtt, &tracker->slave_list[slave].res_list[RES_MTT],
			    com.list) {
		if (!check_mtt_range(dev, slave, start, len, mtt)) {
			*res = mtt;
			mtt->com.from_state = mtt->com.state;
			mtt->com.state = RES_MTT_BUSY;
			err = 0;
			break;
		}
	}
	spin_unlock_irq(mlx4_tlock(dev));

	return err;
}

int mlx4_WRITE_MTT_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd)
{
	struct mlx4_mtt mtt;
	__be64 *page_list = inbox->buf;
	u64 *pg_list = (u64 *)page_list;
	int i;
	struct res_mtt *rmtt = NULL;
	int start = be64_to_cpu(page_list[0]);
	int npages = vhcr->in_modifier;
	int err;

	err = get_containing_mtt(dev, slave, start, npages, &rmtt);
	if (err)
		return err;

	/* Call the SW implementation of write_mtt:
	 * - Prepare a dummy mtt struct
	 * - Translate inbox contents to simple addresses in host endianess */
	mtt.offset = 0;  /* TBD this is broken but I don't handle it since
			    we don't really use it */
	mtt.order = 0;
	mtt.page_shift = 0;
	for (i = 0; i < npages; ++i)
		pg_list[i + 2] = (be64_to_cpu(page_list[i + 2]) & ~1ULL);

	err = __mlx4_write_mtt(dev, &mtt, be64_to_cpu(page_list[0]), npages,
			       ((u64 *)page_list + 2));

	if (rmtt)
		put_res(dev, slave, rmtt->com.res_id, RES_MTT);

	return err;
}

int mlx4_HW2SW_EQ_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd)
{
	int eqn = vhcr->in_modifier;
	int res_id = eqn | (slave << 8);
	struct res_eq *eq;
	int err;

	err = eq_res_start_move_to(dev, slave, res_id, RES_EQ_RESERVED, &eq);
	if (err)
		return err;

	err = get_res(dev, slave, eq->mtt->com.res_id, RES_MTT, NULL);
	if (err)
		goto ex_abort;

	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
	if (err)
		goto ex_put;

	atomic_dec(&eq->mtt->ref_count);
	put_res(dev, slave, eq->mtt->com.res_id, RES_MTT);
	res_end_move(dev, slave, RES_EQ, res_id);
	rem_res_range(dev, slave, res_id, 1, RES_EQ, 0);

	return 0;

ex_put:
	put_res(dev, slave, eq->mtt->com.res_id, RES_MTT);
ex_abort:
	res_abort_move(dev, slave, RES_EQ, res_id);

	return err;
}

int mlx4_GEN_EQE(struct mlx4_dev *dev, int slave, struct mlx4_eqe *eqe)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_slave_event_eq_info *event_eq;
	struct mlx4_cmd_mailbox *mailbox;
	u32 in_modifier = 0;
	int err;
	int res_id;
	struct res_eq *req;

	if (!priv->mfunc.master.slave_state)
		return -EINVAL;

	event_eq = &priv->mfunc.master.slave_state[slave].event_eq[eqe->type];

	/* Create the event only if the slave is registered */
	if (event_eq->eqn < 0)
		return 0;

	mutex_lock(&priv->mfunc.master.gen_eqe_mutex[slave]);
	res_id = (slave << 8) | event_eq->eqn;
	err = get_res(dev, slave, res_id, RES_EQ, &req);
	if (err)
		goto unlock;

	if (req->com.from_state != RES_EQ_HW) {
		err = -EINVAL;
		goto put;
	}

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		goto put;
	}

	if (eqe->type == MLX4_EVENT_TYPE_CMD) {
		++event_eq->token;
		eqe->event.cmd.token = cpu_to_be16(event_eq->token);
	}

	memcpy(mailbox->buf, (u8 *) eqe, 28);

	in_modifier = (slave & 0xff) | ((event_eq->eqn & 0xff) << 16);

	err = mlx4_cmd(dev, mailbox->dma, in_modifier, 0,
		       MLX4_CMD_GEN_EQE, MLX4_CMD_TIME_CLASS_B,
		       MLX4_CMD_NATIVE);

	put_res(dev, slave, res_id, RES_EQ);
	mutex_unlock(&priv->mfunc.master.gen_eqe_mutex[slave]);
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;

put:
	put_res(dev, slave, res_id, RES_EQ);

unlock:
	mutex_unlock(&priv->mfunc.master.gen_eqe_mutex[slave]);
	return err;
}

int mlx4_QUERY_EQ_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd)
{
	int eqn = vhcr->in_modifier;
	int res_id = eqn | (slave << 8);
	struct res_eq *eq;
	int err;

	err = get_res(dev, slave, res_id, RES_EQ, &eq);
	if (err)
		return err;

	if (eq->com.from_state != RES_EQ_HW) {
		err = -EINVAL;
		goto ex_put;
	}

	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);

ex_put:
	put_res(dev, slave, res_id, RES_EQ);
	return err;
}

int mlx4_SW2HW_CQ_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd)
{
	int err;
	int cqn = vhcr->in_modifier;
	struct mlx4_cq_context *cqc = inbox->buf;
	int mtt_base = cq_get_mtt_addr(cqc) / dev->caps.mtt_entry_sz;
	struct res_cq *cq;
	struct res_mtt *mtt;

	err = cq_res_start_move_to(dev, slave, cqn, RES_CQ_HW, &cq);
	if (err)
		return err;
	err = get_res(dev, slave, mtt_base, RES_MTT, &mtt);
	if (err)
		goto out_move;
	err = check_mtt_range(dev, slave, mtt_base, cq_get_mtt_size(cqc), mtt);
	if (err)
		goto out_put;
	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
	if (err)
		goto out_put;
	atomic_inc(&mtt->ref_count);
	cq->mtt = mtt;
	put_res(dev, slave, mtt->com.res_id, RES_MTT);
	res_end_move(dev, slave, RES_CQ, cqn);
	return 0;

out_put:
	put_res(dev, slave, mtt->com.res_id, RES_MTT);
out_move:
	res_abort_move(dev, slave, RES_CQ, cqn);
	return err;
}

int mlx4_HW2SW_CQ_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd)
{
	int err;
	int cqn = vhcr->in_modifier;
	struct res_cq *cq;

	err = cq_res_start_move_to(dev, slave, cqn, RES_CQ_ALLOCATED, &cq);
	if (err)
		return err;
	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
	if (err)
		goto out_move;
	atomic_dec(&cq->mtt->ref_count);
	res_end_move(dev, slave, RES_CQ, cqn);
	return 0;

out_move:
	res_abort_move(dev, slave, RES_CQ, cqn);
	return err;
}

int mlx4_QUERY_CQ_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd)
{
	int cqn = vhcr->in_modifier;
	struct res_cq *cq;
	int err;

	err = get_res(dev, slave, cqn, RES_CQ, &cq);
	if (err)
		return err;

	if (cq->com.from_state != RES_CQ_HW)
		goto ex_put;

	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
ex_put:
	put_res(dev, slave, cqn, RES_CQ);

	return err;
}

static int handle_resize(struct mlx4_dev *dev, int slave,
			 struct mlx4_vhcr *vhcr,
			 struct mlx4_cmd_mailbox *inbox,
			 struct mlx4_cmd_mailbox *outbox,
			 struct mlx4_cmd_info *cmd,
			 struct res_cq *cq)
{
	int err;
	struct res_mtt *orig_mtt;
	struct res_mtt *mtt;
	struct mlx4_cq_context *cqc = inbox->buf;
	int mtt_base = cq_get_mtt_addr(cqc) / dev->caps.mtt_entry_sz;

	err = get_res(dev, slave, cq->mtt->com.res_id, RES_MTT, &orig_mtt);
	if (err)
		return err;

	if (orig_mtt != cq->mtt) {
		err = -EINVAL;
		goto ex_put;
	}

	err = get_res(dev, slave, mtt_base, RES_MTT, &mtt);
	if (err)
		goto ex_put;

	err = check_mtt_range(dev, slave, mtt_base, cq_get_mtt_size(cqc), mtt);
	if (err)
		goto ex_put1;
	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
	if (err)
		goto ex_put1;
	atomic_dec(&orig_mtt->ref_count);
	put_res(dev, slave, orig_mtt->com.res_id, RES_MTT);
	atomic_inc(&mtt->ref_count);
	cq->mtt = mtt;
	put_res(dev, slave, mtt->com.res_id, RES_MTT);
	return 0;

ex_put1:
	put_res(dev, slave, mtt->com.res_id, RES_MTT);
ex_put:
	put_res(dev, slave, orig_mtt->com.res_id, RES_MTT);

	return err;

}

int mlx4_MODIFY_CQ_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd)
{
	int cqn = vhcr->in_modifier;
	struct res_cq *cq;
	int err;

	err = get_res(dev, slave, cqn, RES_CQ, &cq);
	if (err)
		return err;

	if (cq->com.from_state != RES_CQ_HW)
		goto ex_put;

	if (vhcr->op_modifier == 0) {
		err = handle_resize(dev, slave, vhcr, inbox, outbox, cmd, cq);
		goto ex_put;
	}

	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
ex_put:
	put_res(dev, slave, cqn, RES_CQ);

	return err;
}

static int srq_get_mtt_size(struct mlx4_srq_context *srqc)
{
	int log_srq_size = (be32_to_cpu(srqc->state_logsize_srqn) >> 24) & 0xf;
	int log_rq_stride = srqc->logstride & 7;
	int page_shift = (srqc->log_page_size & 0x3f) + 12;

	if (log_srq_size + log_rq_stride + 4 < page_shift)
		return 1;

	return 1 << (log_srq_size + log_rq_stride + 4 - page_shift);
}

int mlx4_SW2HW_SRQ_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd)
{
	int err;
	int srqn = vhcr->in_modifier;
	struct res_mtt *mtt;
	struct res_srq *srq;
	struct mlx4_srq_context *srqc = inbox->buf;
	int mtt_base = srq_get_mtt_addr(srqc) / dev->caps.mtt_entry_sz;

	if (srqn != (be32_to_cpu(srqc->state_logsize_srqn) & 0xffffff))
		return -EINVAL;

	err = srq_res_start_move_to(dev, slave, srqn, RES_SRQ_HW, &srq);
	if (err)
		return err;
	err = get_res(dev, slave, mtt_base, RES_MTT, &mtt);
	if (err)
		goto ex_abort;
	err = check_mtt_range(dev, slave, mtt_base, srq_get_mtt_size(srqc),
			      mtt);
	if (err)
		goto ex_put_mtt;

	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
	if (err)
		goto ex_put_mtt;

	atomic_inc(&mtt->ref_count);
	srq->mtt = mtt;
	put_res(dev, slave, mtt->com.res_id, RES_MTT);
	res_end_move(dev, slave, RES_SRQ, srqn);
	return 0;

ex_put_mtt:
	put_res(dev, slave, mtt->com.res_id, RES_MTT);
ex_abort:
	res_abort_move(dev, slave, RES_SRQ, srqn);

	return err;
}

int mlx4_HW2SW_SRQ_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd)
{
	int err;
	int srqn = vhcr->in_modifier;
	struct res_srq *srq;

	err = srq_res_start_move_to(dev, slave, srqn, RES_SRQ_ALLOCATED, &srq);
	if (err)
		return err;
	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
	if (err)
		goto ex_abort;
	atomic_dec(&srq->mtt->ref_count);
	if (srq->cq)
		atomic_dec(&srq->cq->ref_count);
	res_end_move(dev, slave, RES_SRQ, srqn);

	return 0;

ex_abort:
	res_abort_move(dev, slave, RES_SRQ, srqn);

	return err;
}

int mlx4_QUERY_SRQ_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd)
{
	int err;
	int srqn = vhcr->in_modifier;
	struct res_srq *srq;

	err = get_res(dev, slave, srqn, RES_SRQ, &srq);
	if (err)
		return err;
	if (srq->com.from_state != RES_SRQ_HW) {
		err = -EBUSY;
		goto out;
	}
	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
out:
	put_res(dev, slave, srqn, RES_SRQ);
	return err;
}

int mlx4_ARM_SRQ_wrapper(struct mlx4_dev *dev, int slave,
			 struct mlx4_vhcr *vhcr,
			 struct mlx4_cmd_mailbox *inbox,
			 struct mlx4_cmd_mailbox *outbox,
			 struct mlx4_cmd_info *cmd)
{
	int err;
	int srqn = vhcr->in_modifier;
	struct res_srq *srq;

	err = get_res(dev, slave, srqn, RES_SRQ, &srq);
	if (err)
		return err;

	if (srq->com.from_state != RES_SRQ_HW) {
		err = -EBUSY;
		goto out;
	}

	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
out:
	put_res(dev, slave, srqn, RES_SRQ);
	return err;
}

int mlx4_GEN_QP_wrapper(struct mlx4_dev *dev, int slave,
			struct mlx4_vhcr *vhcr,
			struct mlx4_cmd_mailbox *inbox,
			struct mlx4_cmd_mailbox *outbox,
			struct mlx4_cmd_info *cmd)
{
	int err;
	int qpn = vhcr->in_modifier & 0x7fffff;
	struct res_qp *qp;

	err = get_res(dev, slave, qpn, RES_QP, &qp);
	if (err)
		return err;
	if (qp->com.from_state != RES_QP_HW) {
		err = -EBUSY;
		goto out;
	}

	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
out:
	put_res(dev, slave, qpn, RES_QP);
	return err;
}

int mlx4_INIT2RTR_QP_wrapper(struct mlx4_dev *dev, int slave,
			     struct mlx4_vhcr *vhcr,
			     struct mlx4_cmd_mailbox *inbox,
			     struct mlx4_cmd_mailbox *outbox,
			     struct mlx4_cmd_info *cmd)
{
	struct mlx4_qp_context *qpc = inbox->buf + 8;

	update_ud_gid(dev, qpc, (u8)slave);

	return mlx4_GEN_QP_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
}

int mlx4_2RST_QP_wrapper(struct mlx4_dev *dev, int slave,
			 struct mlx4_vhcr *vhcr,
			 struct mlx4_cmd_mailbox *inbox,
			 struct mlx4_cmd_mailbox *outbox,
			 struct mlx4_cmd_info *cmd)
{
	int err;
	int qpn = vhcr->in_modifier & 0x7fffff;
	struct res_qp *qp;

	err = qp_res_start_move_to(dev, slave, qpn, RES_QP_MAPPED, &qp, 0);
	if (err)
		return err;
	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
	if (err)
		goto ex_abort;

	atomic_dec(&qp->mtt->ref_count);
	atomic_dec(&qp->rcq->ref_count);
	atomic_dec(&qp->scq->ref_count);
	if (qp->srq)
		atomic_dec(&qp->srq->ref_count);
	res_end_move(dev, slave, RES_QP, qpn);
	return 0;

ex_abort:
	res_abort_move(dev, slave, RES_QP, qpn);

	return err;
}

static struct res_gid *find_gid(struct mlx4_dev *dev, int slave,
				struct res_qp *rqp, u8 *gid)
{
	struct res_gid *res;

	list_for_each_entry(res, &rqp->mcg_list, list) {
		if (!memcmp(res->gid, gid, 16))
			return res;
	}
	return NULL;
}

static int add_mcg_res(struct mlx4_dev *dev, int slave, struct res_qp *rqp,
		       u8 *gid, enum mlx4_protocol prot,
		       enum mlx4_steer_type steer)
{
	struct res_gid *res;
	int err;

	res = kzalloc(sizeof *res, GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	spin_lock_irq(&rqp->mcg_spl);
	if (find_gid(dev, slave, rqp, gid)) {
		kfree(res);
		err = -EEXIST;
	} else {
		memcpy(res->gid, gid, 16);
		res->prot = prot;
		res->steer = steer;
		list_add_tail(&res->list, &rqp->mcg_list);
		err = 0;
	}
	spin_unlock_irq(&rqp->mcg_spl);

	return err;
}

static int rem_mcg_res(struct mlx4_dev *dev, int slave, struct res_qp *rqp,
		       u8 *gid, enum mlx4_protocol prot,
		       enum mlx4_steer_type steer)
{
	struct res_gid *res;
	int err;

	spin_lock_irq(&rqp->mcg_spl);
	res = find_gid(dev, slave, rqp, gid);
	if (!res || res->prot != prot || res->steer != steer)
		err = -EINVAL;
	else {
		list_del(&res->list);
		kfree(res);
		err = 0;
	}
	spin_unlock_irq(&rqp->mcg_spl);

	return err;
}

int mlx4_QP_ATTACH_wrapper(struct mlx4_dev *dev, int slave,
			       struct mlx4_vhcr *vhcr,
			       struct mlx4_cmd_mailbox *inbox,
			       struct mlx4_cmd_mailbox *outbox,
			       struct mlx4_cmd_info *cmd)
{
	struct mlx4_qp qp; /* dummy for calling attach/detach */
	u8 *gid = inbox->buf;
	enum mlx4_protocol prot = (vhcr->in_modifier >> 28) & 0x7;
	int err;
	int qpn;
	struct res_qp *rqp;
	int attach = vhcr->op_modifier;
	int block_loopback = vhcr->in_modifier >> 31;
	u8 steer_type_mask = 2;
	enum mlx4_steer_type type = (gid[7] & steer_type_mask) >> 1;

	qpn = vhcr->in_modifier & 0xffffff;
	err = get_res(dev, slave, qpn, RES_QP, &rqp);
	if (err)
		return err;

	qp.qpn = qpn;
	if (attach) {
		err = add_mcg_res(dev, slave, rqp, gid, prot, type);
		if (err)
			goto ex_put;

		err = mlx4_qp_attach_common(dev, &qp, gid,
					    block_loopback, prot, type);
		if (err)
			goto ex_rem;
	} else {
		err = rem_mcg_res(dev, slave, rqp, gid, prot, type);
		if (err)
			goto ex_put;
		err = mlx4_qp_detach_common(dev, &qp, gid, prot, type);
	}

	put_res(dev, slave, qpn, RES_QP);
	return 0;

ex_rem:
	/* ignore error return below, already in error */
	(void) rem_mcg_res(dev, slave, rqp, gid, prot, type);
ex_put:
	put_res(dev, slave, qpn, RES_QP);

	return err;
}

int mlx4_QP_FLOW_STEERING_ATTACH_wrapper(struct mlx4_dev *dev, int slave,
					 struct mlx4_vhcr *vhcr,
					 struct mlx4_cmd_mailbox *inbox,
					 struct mlx4_cmd_mailbox *outbox,
					 struct mlx4_cmd_info *cmd)
{
	int err;

	if (dev->caps.steering_mode !=
	    MLX4_STEERING_MODE_DEVICE_MANAGED)
		return -EOPNOTSUPP;

	err = mlx4_cmd_imm(dev, inbox->dma, &vhcr->out_param,
			   vhcr->in_modifier, 0,
			   MLX4_QP_FLOW_STEERING_ATTACH, MLX4_CMD_TIME_CLASS_A,
			   MLX4_CMD_NATIVE);
	if (err)
		return err;

	err = add_res_range(dev, slave, vhcr->out_param, 1, RES_FS_RULE, 0);
	if (err) {
		mlx4_err(dev, "Fail to add flow steering resources.\n ");
		/* detach rule*/
		mlx4_cmd(dev, vhcr->out_param, 0, 0,
			 MLX4_QP_FLOW_STEERING_ATTACH, MLX4_CMD_TIME_CLASS_A,
			 MLX4_CMD_NATIVE);
	}
	return err;
}

int mlx4_QP_FLOW_STEERING_DETACH_wrapper(struct mlx4_dev *dev, int slave,
					 struct mlx4_vhcr *vhcr,
					 struct mlx4_cmd_mailbox *inbox,
					 struct mlx4_cmd_mailbox *outbox,
					 struct mlx4_cmd_info *cmd)
{
	int err;

	if (dev->caps.steering_mode !=
	    MLX4_STEERING_MODE_DEVICE_MANAGED)
		return -EOPNOTSUPP;

	err = rem_res_range(dev, slave, vhcr->in_param, 1, RES_FS_RULE, 0);
	if (err) {
		mlx4_err(dev, "Fail to remove flow steering resources.\n ");
		return err;
	}

	err = mlx4_cmd(dev, vhcr->in_param, 0, 0,
		       MLX4_QP_FLOW_STEERING_DETACH, MLX4_CMD_TIME_CLASS_A,
		       MLX4_CMD_NATIVE);
	return err;
}

enum {
	BUSY_MAX_RETRIES = 10
};

int mlx4_QUERY_IF_STAT_wrapper(struct mlx4_dev *dev, int slave,
			       struct mlx4_vhcr *vhcr,
			       struct mlx4_cmd_mailbox *inbox,
			       struct mlx4_cmd_mailbox *outbox,
			       struct mlx4_cmd_info *cmd)
{
	int err;
	int index = vhcr->in_modifier & 0xffff;

	err = get_res(dev, slave, index, RES_COUNTER, NULL);
	if (err)
		return err;

	err = mlx4_DMA_wrapper(dev, slave, vhcr, inbox, outbox, cmd);
	put_res(dev, slave, index, RES_COUNTER);
	return err;
}

static void detach_qp(struct mlx4_dev *dev, int slave, struct res_qp *rqp)
{
	struct res_gid *rgid;
	struct res_gid *tmp;
	struct mlx4_qp qp; /* dummy for calling attach/detach */

	list_for_each_entry_safe(rgid, tmp, &rqp->mcg_list, list) {
		qp.qpn = rqp->local_qpn;
		(void) mlx4_qp_detach_common(dev, &qp, rgid->gid, rgid->prot,
					     rgid->steer);
		list_del(&rgid->list);
		kfree(rgid);
	}
}

static int _move_all_busy(struct mlx4_dev *dev, int slave,
			  enum mlx4_resource type, int print)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker =
		&priv->mfunc.master.res_tracker;
	struct list_head *rlist = &tracker->slave_list[slave].res_list[type];
	struct res_common *r;
	struct res_common *tmp;
	int busy;

	busy = 0;
	spin_lock_irq(mlx4_tlock(dev));
	list_for_each_entry_safe(r, tmp, rlist, list) {
		if (r->owner == slave) {
			if (!r->removing) {
				if (r->state == RES_ANY_BUSY) {
					if (print)
						mlx4_dbg(dev,
							 "%s id 0x%llx is busy\n",
							  ResourceType(type),
							  r->res_id);
					++busy;
				} else {
					r->from_state = r->state;
					r->state = RES_ANY_BUSY;
					r->removing = 1;
				}
			}
		}
	}
	spin_unlock_irq(mlx4_tlock(dev));

	return busy;
}

static int move_all_busy(struct mlx4_dev *dev, int slave,
			 enum mlx4_resource type)
{
	unsigned long begin;
	int busy;

	begin = jiffies;
	do {
		busy = _move_all_busy(dev, slave, type, 0);
		if (time_after(jiffies, begin + 5 * HZ))
			break;
		if (busy)
			cond_resched();
	} while (busy);

	if (busy)
		busy = _move_all_busy(dev, slave, type, 1);

	return busy;
}
static void rem_slave_qps(struct mlx4_dev *dev, int slave)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct list_head *qp_list =
		&tracker->slave_list[slave].res_list[RES_QP];
	struct res_qp *qp;
	struct res_qp *tmp;
	int state;
	u64 in_param;
	int qpn;
	int err;

	err = move_all_busy(dev, slave, RES_QP);
	if (err)
		mlx4_warn(dev, "rem_slave_qps: Could not move all qps to busy"
			  "for slave %d\n", slave);

	spin_lock_irq(mlx4_tlock(dev));
	list_for_each_entry_safe(qp, tmp, qp_list, com.list) {
		spin_unlock_irq(mlx4_tlock(dev));
		if (qp->com.owner == slave) {
			qpn = qp->com.res_id;
			detach_qp(dev, slave, qp);
			state = qp->com.from_state;
			while (state != 0) {
				switch (state) {
				case RES_QP_RESERVED:
					spin_lock_irq(mlx4_tlock(dev));
					rb_erase(&qp->com.node,
						 &tracker->res_tree[RES_QP]);
					list_del(&qp->com.list);
					spin_unlock_irq(mlx4_tlock(dev));
					kfree(qp);
					state = 0;
					break;
				case RES_QP_MAPPED:
					if (!valid_reserved(dev, slave, qpn))
						__mlx4_qp_free_icm(dev, qpn);
					state = RES_QP_RESERVED;
					break;
				case RES_QP_HW:
					in_param = slave;
					err = mlx4_cmd(dev, in_param,
						       qp->local_qpn, 2,
						       MLX4_CMD_2RST_QP,
						       MLX4_CMD_TIME_CLASS_A,
						       MLX4_CMD_NATIVE);
					if (err)
						mlx4_dbg(dev, "rem_slave_qps: failed"
							 " to move slave %d qpn %d to"
							 " reset\n", slave,
							 qp->local_qpn);
					atomic_dec(&qp->rcq->ref_count);
					atomic_dec(&qp->scq->ref_count);
					atomic_dec(&qp->mtt->ref_count);
					if (qp->srq)
						atomic_dec(&qp->srq->ref_count);
					state = RES_QP_MAPPED;
					break;
				default:
					state = 0;
				}
			}
		}
		spin_lock_irq(mlx4_tlock(dev));
	}
	spin_unlock_irq(mlx4_tlock(dev));
}

static void rem_slave_srqs(struct mlx4_dev *dev, int slave)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct list_head *srq_list =
		&tracker->slave_list[slave].res_list[RES_SRQ];
	struct res_srq *srq;
	struct res_srq *tmp;
	int state;
	u64 in_param;
	LIST_HEAD(tlist);
	int srqn;
	int err;

	err = move_all_busy(dev, slave, RES_SRQ);
	if (err)
		mlx4_warn(dev, "rem_slave_srqs: Could not move all srqs to "
			  "busy for slave %d\n", slave);

	spin_lock_irq(mlx4_tlock(dev));
	list_for_each_entry_safe(srq, tmp, srq_list, com.list) {
		spin_unlock_irq(mlx4_tlock(dev));
		if (srq->com.owner == slave) {
			srqn = srq->com.res_id;
			state = srq->com.from_state;
			while (state != 0) {
				switch (state) {
				case RES_SRQ_ALLOCATED:
					__mlx4_srq_free_icm(dev, srqn);
					spin_lock_irq(mlx4_tlock(dev));
					rb_erase(&srq->com.node,
						 &tracker->res_tree[RES_SRQ]);
					list_del(&srq->com.list);
					spin_unlock_irq(mlx4_tlock(dev));
					kfree(srq);
					state = 0;
					break;

				case RES_SRQ_HW:
					in_param = slave;
					err = mlx4_cmd(dev, in_param, srqn, 1,
						       MLX4_CMD_HW2SW_SRQ,
						       MLX4_CMD_TIME_CLASS_A,
						       MLX4_CMD_NATIVE);
					if (err)
						mlx4_dbg(dev, "rem_slave_srqs: failed"
							 " to move slave %d srq %d to"
							 " SW ownership\n",
							 slave, srqn);

					atomic_dec(&srq->mtt->ref_count);
					if (srq->cq)
						atomic_dec(&srq->cq->ref_count);
					state = RES_SRQ_ALLOCATED;
					break;

				default:
					state = 0;
				}
			}
		}
		spin_lock_irq(mlx4_tlock(dev));
	}
	spin_unlock_irq(mlx4_tlock(dev));
}

static void rem_slave_cqs(struct mlx4_dev *dev, int slave)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct list_head *cq_list =
		&tracker->slave_list[slave].res_list[RES_CQ];
	struct res_cq *cq;
	struct res_cq *tmp;
	int state;
	u64 in_param;
	LIST_HEAD(tlist);
	int cqn;
	int err;

	err = move_all_busy(dev, slave, RES_CQ);
	if (err)
		mlx4_warn(dev, "rem_slave_cqs: Could not move all cqs to "
			  "busy for slave %d\n", slave);

	spin_lock_irq(mlx4_tlock(dev));
	list_for_each_entry_safe(cq, tmp, cq_list, com.list) {
		spin_unlock_irq(mlx4_tlock(dev));
		if (cq->com.owner == slave && !atomic_read(&cq->ref_count)) {
			cqn = cq->com.res_id;
			state = cq->com.from_state;
			while (state != 0) {
				switch (state) {
				case RES_CQ_ALLOCATED:
					__mlx4_cq_free_icm(dev, cqn);
					spin_lock_irq(mlx4_tlock(dev));
					rb_erase(&cq->com.node,
						 &tracker->res_tree[RES_CQ]);
					list_del(&cq->com.list);
					spin_unlock_irq(mlx4_tlock(dev));
					kfree(cq);
					state = 0;
					break;

				case RES_CQ_HW:
					in_param = slave;
					err = mlx4_cmd(dev, in_param, cqn, 1,
						       MLX4_CMD_HW2SW_CQ,
						       MLX4_CMD_TIME_CLASS_A,
						       MLX4_CMD_NATIVE);
					if (err)
						mlx4_dbg(dev, "rem_slave_cqs: failed"
							 " to move slave %d cq %d to"
							 " SW ownership\n",
							 slave, cqn);
					atomic_dec(&cq->mtt->ref_count);
					state = RES_CQ_ALLOCATED;
					break;

				default:
					state = 0;
				}
			}
		}
		spin_lock_irq(mlx4_tlock(dev));
	}
	spin_unlock_irq(mlx4_tlock(dev));
}

static void rem_slave_mrs(struct mlx4_dev *dev, int slave)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct list_head *mpt_list =
		&tracker->slave_list[slave].res_list[RES_MPT];
	struct res_mpt *mpt;
	struct res_mpt *tmp;
	int state;
	u64 in_param;
	LIST_HEAD(tlist);
	int mptn;
	int err;

	err = move_all_busy(dev, slave, RES_MPT);
	if (err)
		mlx4_warn(dev, "rem_slave_mrs: Could not move all mpts to "
			  "busy for slave %d\n", slave);

	spin_lock_irq(mlx4_tlock(dev));
	list_for_each_entry_safe(mpt, tmp, mpt_list, com.list) {
		spin_unlock_irq(mlx4_tlock(dev));
		if (mpt->com.owner == slave) {
			mptn = mpt->com.res_id;
			state = mpt->com.from_state;
			while (state != 0) {
				switch (state) {
				case RES_MPT_RESERVED:
					__mlx4_mr_release(dev, mpt->key);
					spin_lock_irq(mlx4_tlock(dev));
					rb_erase(&mpt->com.node,
						 &tracker->res_tree[RES_MPT]);
					list_del(&mpt->com.list);
					spin_unlock_irq(mlx4_tlock(dev));
					kfree(mpt);
					state = 0;
					break;

				case RES_MPT_MAPPED:
					__mlx4_mr_free_icm(dev, mpt->key);
					state = RES_MPT_RESERVED;
					break;

				case RES_MPT_HW:
					in_param = slave;
					err = mlx4_cmd(dev, in_param, mptn, 0,
						     MLX4_CMD_HW2SW_MPT,
						     MLX4_CMD_TIME_CLASS_A,
						     MLX4_CMD_NATIVE);
					if (err)
						mlx4_dbg(dev, "rem_slave_mrs: failed"
							 " to move slave %d mpt %d to"
							 " SW ownership\n",
							 slave, mptn);
					if (mpt->mtt)
						atomic_dec(&mpt->mtt->ref_count);
					state = RES_MPT_MAPPED;
					break;
				default:
					state = 0;
				}
			}
		}
		spin_lock_irq(mlx4_tlock(dev));
	}
	spin_unlock_irq(mlx4_tlock(dev));
}

static void rem_slave_mtts(struct mlx4_dev *dev, int slave)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker =
		&priv->mfunc.master.res_tracker;
	struct list_head *mtt_list =
		&tracker->slave_list[slave].res_list[RES_MTT];
	struct res_mtt *mtt;
	struct res_mtt *tmp;
	int state;
	LIST_HEAD(tlist);
	int base;
	int err;

	err = move_all_busy(dev, slave, RES_MTT);
	if (err)
		mlx4_warn(dev, "rem_slave_mtts: Could not move all mtts to "
			  "busy for slave %d\n", slave);

	spin_lock_irq(mlx4_tlock(dev));
	list_for_each_entry_safe(mtt, tmp, mtt_list, com.list) {
		spin_unlock_irq(mlx4_tlock(dev));
		if (mtt->com.owner == slave) {
			base = mtt->com.res_id;
			state = mtt->com.from_state;
			while (state != 0) {
				switch (state) {
				case RES_MTT_ALLOCATED:
					__mlx4_free_mtt_range(dev, base,
							      mtt->order);
					spin_lock_irq(mlx4_tlock(dev));
					rb_erase(&mtt->com.node,
						 &tracker->res_tree[RES_MTT]);
					list_del(&mtt->com.list);
					spin_unlock_irq(mlx4_tlock(dev));
					kfree(mtt);
					state = 0;
					break;

				default:
					state = 0;
				}
			}
		}
		spin_lock_irq(mlx4_tlock(dev));
	}
	spin_unlock_irq(mlx4_tlock(dev));
}

static void rem_slave_fs_rule(struct mlx4_dev *dev, int slave)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker =
		&priv->mfunc.master.res_tracker;
	struct list_head *fs_rule_list =
		&tracker->slave_list[slave].res_list[RES_FS_RULE];
	struct res_fs_rule *fs_rule;
	struct res_fs_rule *tmp;
	int state;
	u64 base;
	int err;

	err = move_all_busy(dev, slave, RES_FS_RULE);
	if (err)
		mlx4_warn(dev, "rem_slave_fs_rule: Could not move all mtts to busy for slave %d\n",
			  slave);

	spin_lock_irq(mlx4_tlock(dev));
	list_for_each_entry_safe(fs_rule, tmp, fs_rule_list, com.list) {
		spin_unlock_irq(mlx4_tlock(dev));
		if (fs_rule->com.owner == slave) {
			base = fs_rule->com.res_id;
			state = fs_rule->com.from_state;
			while (state != 0) {
				switch (state) {
				case RES_FS_RULE_ALLOCATED:
					/* detach rule */
					err = mlx4_cmd(dev, base, 0, 0,
						       MLX4_QP_FLOW_STEERING_DETACH,
						       MLX4_CMD_TIME_CLASS_A,
						       MLX4_CMD_NATIVE);

					spin_lock_irq(mlx4_tlock(dev));
					rb_erase(&fs_rule->com.node,
						 &tracker->res_tree[RES_FS_RULE]);
					list_del(&fs_rule->com.list);
					spin_unlock_irq(mlx4_tlock(dev));
					kfree(fs_rule);
					state = 0;
					break;

				default:
					state = 0;
				}
			}
		}
		spin_lock_irq(mlx4_tlock(dev));
	}
	spin_unlock_irq(mlx4_tlock(dev));
}

static void rem_slave_eqs(struct mlx4_dev *dev, int slave)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct list_head *eq_list =
		&tracker->slave_list[slave].res_list[RES_EQ];
	struct res_eq *eq;
	struct res_eq *tmp;
	int err;
	int state;
	LIST_HEAD(tlist);
	int eqn;
	struct mlx4_cmd_mailbox *mailbox;

	err = move_all_busy(dev, slave, RES_EQ);
	if (err)
		mlx4_warn(dev, "rem_slave_eqs: Could not move all eqs to "
			  "busy for slave %d\n", slave);

	spin_lock_irq(mlx4_tlock(dev));
	list_for_each_entry_safe(eq, tmp, eq_list, com.list) {
		spin_unlock_irq(mlx4_tlock(dev));
		if (eq->com.owner == slave) {
			eqn = eq->com.res_id;
			state = eq->com.from_state;
			while (state != 0) {
				switch (state) {
				case RES_EQ_RESERVED:
					spin_lock_irq(mlx4_tlock(dev));
					rb_erase(&eq->com.node,
						 &tracker->res_tree[RES_EQ]);
					list_del(&eq->com.list);
					spin_unlock_irq(mlx4_tlock(dev));
					kfree(eq);
					state = 0;
					break;

				case RES_EQ_HW:
					mailbox = mlx4_alloc_cmd_mailbox(dev);
					if (IS_ERR(mailbox)) {
						cond_resched();
						continue;
					}
					err = mlx4_cmd_box(dev, slave, 0,
							   eqn & 0xff, 0,
							   MLX4_CMD_HW2SW_EQ,
							   MLX4_CMD_TIME_CLASS_A,
							   MLX4_CMD_NATIVE);
					if (err)
						mlx4_dbg(dev, "rem_slave_eqs: failed"
							 " to move slave %d eqs %d to"
							 " SW ownership\n", slave, eqn);
					mlx4_free_cmd_mailbox(dev, mailbox);
					atomic_dec(&eq->mtt->ref_count);
					state = RES_EQ_RESERVED;
					break;

				default:
					state = 0;
				}
			}
		}
		spin_lock_irq(mlx4_tlock(dev));
	}
	spin_unlock_irq(mlx4_tlock(dev));
}

static void rem_slave_counters(struct mlx4_dev *dev, int slave)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct list_head *counter_list =
		&tracker->slave_list[slave].res_list[RES_COUNTER];
	struct res_counter *counter;
	struct res_counter *tmp;
	int err;
	int index;

	err = move_all_busy(dev, slave, RES_COUNTER);
	if (err)
		mlx4_warn(dev, "rem_slave_counters: Could not move all counters to "
			  "busy for slave %d\n", slave);

	spin_lock_irq(mlx4_tlock(dev));
	list_for_each_entry_safe(counter, tmp, counter_list, com.list) {
		if (counter->com.owner == slave) {
			index = counter->com.res_id;
			rb_erase(&counter->com.node,
				 &tracker->res_tree[RES_COUNTER]);
			list_del(&counter->com.list);
			kfree(counter);
			__mlx4_counter_free(dev, index);
		}
	}
	spin_unlock_irq(mlx4_tlock(dev));
}

static void rem_slave_xrcdns(struct mlx4_dev *dev, int slave)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_resource_tracker *tracker = &priv->mfunc.master.res_tracker;
	struct list_head *xrcdn_list =
		&tracker->slave_list[slave].res_list[RES_XRCD];
	struct res_xrcdn *xrcd;
	struct res_xrcdn *tmp;
	int err;
	int xrcdn;

	err = move_all_busy(dev, slave, RES_XRCD);
	if (err)
		mlx4_warn(dev, "rem_slave_xrcdns: Could not move all xrcdns to "
			  "busy for slave %d\n", slave);

	spin_lock_irq(mlx4_tlock(dev));
	list_for_each_entry_safe(xrcd, tmp, xrcdn_list, com.list) {
		if (xrcd->com.owner == slave) {
			xrcdn = xrcd->com.res_id;
			rb_erase(&xrcd->com.node, &tracker->res_tree[RES_XRCD]);
			list_del(&xrcd->com.list);
			kfree(xrcd);
			__mlx4_xrcd_free(dev, xrcdn);
		}
	}
	spin_unlock_irq(mlx4_tlock(dev));
}

void mlx4_delete_all_resources_for_slave(struct mlx4_dev *dev, int slave)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	mutex_lock(&priv->mfunc.master.res_tracker.slave_list[slave].mutex);
	/*VLAN*/
	rem_slave_macs(dev, slave);
	rem_slave_qps(dev, slave);
	rem_slave_srqs(dev, slave);
	rem_slave_cqs(dev, slave);
	rem_slave_mrs(dev, slave);
	rem_slave_eqs(dev, slave);
	rem_slave_mtts(dev, slave);
	rem_slave_counters(dev, slave);
	rem_slave_xrcdns(dev, slave);
	rem_slave_fs_rule(dev, slave);
	mutex_unlock(&priv->mfunc.master.res_tracker.slave_list[slave].mutex);
}
