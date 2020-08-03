/*
 * Copyright (c) 2012 Mellanox Technologies. All rights reserved.
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

#include <rdma/ib_mad.h>

#include <linux/mlx4/cmd.h>
#include <linux/rbtree.h>
#include <linux/idr.h>
#include <rdma/ib_cm.h>

#include "mlx4_ib.h"

#define CM_CLEANUP_CACHE_TIMEOUT  (30 * HZ)

struct id_map_entry {
	struct rb_node node;

	u32 sl_cm_id;
	u32 pv_cm_id;
	int slave_id;
	int scheduled_delete;
	struct mlx4_ib_dev *dev;

	struct list_head list;
	struct delayed_work timeout;
};

struct rej_tmout_entry {
	int slave;
	u32 rem_pv_cm_id;
	struct delayed_work timeout;
	struct radix_tree_root *rej_tmout_root;
	/* Points to the mutex protecting this radix-tree */
	struct mutex *lock;
};

struct cm_generic_msg {
	struct ib_mad_hdr hdr;

	__be32 local_comm_id;
	__be32 remote_comm_id;
	unsigned char unused[2];
	__be16 rej_reason;
};

struct cm_sidr_generic_msg {
	struct ib_mad_hdr hdr;
	__be32 request_id;
};

struct cm_req_msg {
	unsigned char unused[0x60];
	union ib_gid primary_path_sgid;
};


static void set_local_comm_id(struct ib_mad *mad, u32 cm_id)
{
	if (mad->mad_hdr.attr_id == CM_SIDR_REQ_ATTR_ID) {
		struct cm_sidr_generic_msg *msg =
			(struct cm_sidr_generic_msg *)mad;
		msg->request_id = cpu_to_be32(cm_id);
	} else if (mad->mad_hdr.attr_id == CM_SIDR_REP_ATTR_ID) {
		pr_err("trying to set local_comm_id in SIDR_REP\n");
		return;
	} else {
		struct cm_generic_msg *msg = (struct cm_generic_msg *)mad;
		msg->local_comm_id = cpu_to_be32(cm_id);
	}
}

static u32 get_local_comm_id(struct ib_mad *mad)
{
	if (mad->mad_hdr.attr_id == CM_SIDR_REQ_ATTR_ID) {
		struct cm_sidr_generic_msg *msg =
			(struct cm_sidr_generic_msg *)mad;
		return be32_to_cpu(msg->request_id);
	} else if (mad->mad_hdr.attr_id == CM_SIDR_REP_ATTR_ID) {
		pr_err("trying to set local_comm_id in SIDR_REP\n");
		return -1;
	} else {
		struct cm_generic_msg *msg = (struct cm_generic_msg *)mad;
		return be32_to_cpu(msg->local_comm_id);
	}
}

static void set_remote_comm_id(struct ib_mad *mad, u32 cm_id)
{
	if (mad->mad_hdr.attr_id == CM_SIDR_REP_ATTR_ID) {
		struct cm_sidr_generic_msg *msg =
			(struct cm_sidr_generic_msg *)mad;
		msg->request_id = cpu_to_be32(cm_id);
	} else if (mad->mad_hdr.attr_id == CM_SIDR_REQ_ATTR_ID) {
		pr_err("trying to set remote_comm_id in SIDR_REQ\n");
		return;
	} else {
		struct cm_generic_msg *msg = (struct cm_generic_msg *)mad;
		msg->remote_comm_id = cpu_to_be32(cm_id);
	}
}

static u32 get_remote_comm_id(struct ib_mad *mad)
{
	if (mad->mad_hdr.attr_id == CM_SIDR_REP_ATTR_ID) {
		struct cm_sidr_generic_msg *msg =
			(struct cm_sidr_generic_msg *)mad;
		return be32_to_cpu(msg->request_id);
	} else if (mad->mad_hdr.attr_id == CM_SIDR_REQ_ATTR_ID) {
		pr_err("trying to set remote_comm_id in SIDR_REQ\n");
		return -1;
	} else {
		struct cm_generic_msg *msg = (struct cm_generic_msg *)mad;
		return be32_to_cpu(msg->remote_comm_id);
	}
}

static union ib_gid gid_from_req_msg(struct ib_device *ibdev, struct ib_mad *mad)
{
	struct cm_req_msg *msg = (struct cm_req_msg *)mad;

	return msg->primary_path_sgid;
}

/* Lock should be taken before called */
static struct id_map_entry *
id_map_find_by_sl_id(struct ib_device *ibdev, u32 slave_id, u32 sl_cm_id)
{
	struct rb_root *sl_id_map = &to_mdev(ibdev)->sriov.sl_id_map;
	struct rb_node *node = sl_id_map->rb_node;

	while (node) {
		struct id_map_entry *id_map_entry =
			rb_entry(node, struct id_map_entry, node);

		if (id_map_entry->sl_cm_id > sl_cm_id)
			node = node->rb_left;
		else if (id_map_entry->sl_cm_id < sl_cm_id)
			node = node->rb_right;
		else if (id_map_entry->slave_id > slave_id)
			node = node->rb_left;
		else if (id_map_entry->slave_id < slave_id)
			node = node->rb_right;
		else
			return id_map_entry;
	}
	return NULL;
}

static void id_map_ent_timeout(struct work_struct *work)
{
	struct delayed_work *delay = to_delayed_work(work);
	struct id_map_entry *ent = container_of(delay, struct id_map_entry, timeout);
	struct id_map_entry *found_ent;
	struct mlx4_ib_dev *dev = ent->dev;
	struct mlx4_ib_sriov *sriov = &dev->sriov;
	struct rb_root *sl_id_map = &sriov->sl_id_map;

	spin_lock(&sriov->id_map_lock);
	if (!xa_erase(&sriov->pv_id_table, ent->pv_cm_id))
		goto out;
	found_ent = id_map_find_by_sl_id(&dev->ib_dev, ent->slave_id, ent->sl_cm_id);
	if (found_ent && found_ent == ent)
		rb_erase(&found_ent->node, sl_id_map);

out:
	list_del(&ent->list);
	spin_unlock(&sriov->id_map_lock);
	kfree(ent);
}

static void sl_id_map_add(struct ib_device *ibdev, struct id_map_entry *new)
{
	struct rb_root *sl_id_map = &to_mdev(ibdev)->sriov.sl_id_map;
	struct rb_node **link = &sl_id_map->rb_node, *parent = NULL;
	struct id_map_entry *ent;
	int slave_id = new->slave_id;
	int sl_cm_id = new->sl_cm_id;

	ent = id_map_find_by_sl_id(ibdev, slave_id, sl_cm_id);
	if (ent) {
		pr_debug("overriding existing sl_id_map entry (cm_id = %x)\n",
			 sl_cm_id);

		rb_replace_node(&ent->node, &new->node, sl_id_map);
		return;
	}

	/* Go to the bottom of the tree */
	while (*link) {
		parent = *link;
		ent = rb_entry(parent, struct id_map_entry, node);

		if (ent->sl_cm_id > sl_cm_id || (ent->sl_cm_id == sl_cm_id && ent->slave_id > slave_id))
			link = &(*link)->rb_left;
		else
			link = &(*link)->rb_right;
	}

	rb_link_node(&new->node, parent, link);
	rb_insert_color(&new->node, sl_id_map);
}

static struct id_map_entry *
id_map_alloc(struct ib_device *ibdev, int slave_id, u32 sl_cm_id)
{
	int ret;
	struct id_map_entry *ent;
	struct mlx4_ib_sriov *sriov = &to_mdev(ibdev)->sriov;

	ent = kmalloc(sizeof (struct id_map_entry), GFP_KERNEL);
	if (!ent)
		return ERR_PTR(-ENOMEM);

	ent->sl_cm_id = sl_cm_id;
	ent->slave_id = slave_id;
	ent->scheduled_delete = 0;
	ent->dev = to_mdev(ibdev);
	INIT_DELAYED_WORK(&ent->timeout, id_map_ent_timeout);

	ret = xa_alloc_cyclic(&sriov->pv_id_table, &ent->pv_cm_id, ent,
			xa_limit_32b, &sriov->pv_id_next, GFP_KERNEL);
	if (ret >= 0) {
		spin_lock(&sriov->id_map_lock);
		sl_id_map_add(ibdev, ent);
		list_add_tail(&ent->list, &sriov->cm_list);
		spin_unlock(&sriov->id_map_lock);
		return ent;
	}

	/*error flow*/
	kfree(ent);
	mlx4_ib_warn(ibdev, "Allocation failed (err:0x%x)\n", ret);
	return ERR_PTR(-ENOMEM);
}

static struct id_map_entry *
id_map_get(struct ib_device *ibdev, int *pv_cm_id, int slave_id, int sl_cm_id)
{
	struct id_map_entry *ent;
	struct mlx4_ib_sriov *sriov = &to_mdev(ibdev)->sriov;

	spin_lock(&sriov->id_map_lock);
	if (*pv_cm_id == -1) {
		ent = id_map_find_by_sl_id(ibdev, slave_id, sl_cm_id);
		if (ent)
			*pv_cm_id = (int) ent->pv_cm_id;
	} else
		ent = xa_load(&sriov->pv_id_table, *pv_cm_id);
	spin_unlock(&sriov->id_map_lock);

	return ent;
}

static void schedule_delayed(struct ib_device *ibdev, struct id_map_entry *id)
{
	struct mlx4_ib_sriov *sriov = &to_mdev(ibdev)->sriov;
	unsigned long flags;

	spin_lock(&sriov->id_map_lock);
	spin_lock_irqsave(&sriov->going_down_lock, flags);
	/*make sure that there is no schedule inside the scheduled work.*/
	if (!sriov->is_going_down && !id->scheduled_delete) {
		id->scheduled_delete = 1;
		schedule_delayed_work(&id->timeout, CM_CLEANUP_CACHE_TIMEOUT);
	} else if (id->scheduled_delete) {
		/* Adjust timeout if already scheduled */
		mod_delayed_work(system_wq, &id->timeout, CM_CLEANUP_CACHE_TIMEOUT);
	}
	spin_unlock_irqrestore(&sriov->going_down_lock, flags);
	spin_unlock(&sriov->id_map_lock);
}

#define REJ_REASON(m) be16_to_cpu(((struct cm_generic_msg *)(m))->rej_reason)
int mlx4_ib_multiplex_cm_handler(struct ib_device *ibdev, int port, int slave_id,
		struct ib_mad *mad)
{
	struct id_map_entry *id;
	u32 sl_cm_id;
	int pv_cm_id = -1;

	if (mad->mad_hdr.attr_id == CM_REQ_ATTR_ID ||
	    mad->mad_hdr.attr_id == CM_REP_ATTR_ID ||
	    mad->mad_hdr.attr_id == CM_MRA_ATTR_ID ||
	    mad->mad_hdr.attr_id == CM_SIDR_REQ_ATTR_ID ||
	    (mad->mad_hdr.attr_id == CM_REJ_ATTR_ID && REJ_REASON(mad) == IB_CM_REJ_TIMEOUT)) {
		sl_cm_id = get_local_comm_id(mad);
		id = id_map_get(ibdev, &pv_cm_id, slave_id, sl_cm_id);
		if (id)
			goto cont;
		id = id_map_alloc(ibdev, slave_id, sl_cm_id);
		if (IS_ERR(id)) {
			mlx4_ib_warn(ibdev, "%s: id{slave: %d, sl_cm_id: 0x%x} Failed to id_map_alloc\n",
				__func__, slave_id, sl_cm_id);
			return PTR_ERR(id);
		}
	} else if (mad->mad_hdr.attr_id == CM_REJ_ATTR_ID ||
		   mad->mad_hdr.attr_id == CM_SIDR_REP_ATTR_ID) {
		return 0;
	} else {
		sl_cm_id = get_local_comm_id(mad);
		id = id_map_get(ibdev, &pv_cm_id, slave_id, sl_cm_id);
	}

	if (!id) {
		pr_debug("id{slave: %d, sl_cm_id: 0x%x} is NULL! attr_id: 0x%x\n",
			 slave_id, sl_cm_id, be16_to_cpu(mad->mad_hdr.attr_id));
		return -EINVAL;
	}

cont:
	set_local_comm_id(mad, id->pv_cm_id);

	if (mad->mad_hdr.attr_id == CM_DREQ_ATTR_ID)
		schedule_delayed(ibdev, id);
	return 0;
}

static void rej_tmout_timeout(struct work_struct *work)
{
	struct delayed_work *delay = to_delayed_work(work);
	struct rej_tmout_entry *item = container_of(delay, struct rej_tmout_entry, timeout);
	struct rej_tmout_entry *deleted;

	mutex_lock(item->lock);
	deleted = radix_tree_delete_item(item->rej_tmout_root, item->rem_pv_cm_id, NULL);
	mutex_unlock(item->lock);

	if (deleted != item)
		pr_debug("deleted(%p) != item(%p)\n", deleted, item);

	kfree(item);
}

static int alloc_rej_tmout(struct mlx4_ib_sriov *sriov, u32 rem_pv_cm_id, int slave)
{
	struct rej_tmout_entry *item;
	int sts;

	mutex_lock(&sriov->rej_tmout_lock);
	item = radix_tree_lookup(&sriov->rej_tmout_root, (unsigned long)rem_pv_cm_id);
	mutex_unlock(&sriov->rej_tmout_lock);
	if (item) {
		if (IS_ERR(item))
			return PTR_ERR(item);
		/* If a retry, adjust delayed work */
		mod_delayed_work(system_wq, &item->timeout, CM_CLEANUP_CACHE_TIMEOUT);
		return 0;
	}

	item = kmalloc(sizeof(*item), GFP_KERNEL);
	if (!item)
		return -ENOMEM;

	INIT_DELAYED_WORK(&item->timeout, rej_tmout_timeout);
	item->slave = slave;
	item->rem_pv_cm_id = rem_pv_cm_id;
	item->rej_tmout_root = &sriov->rej_tmout_root;
	item->lock = &sriov->rej_tmout_lock;

	mutex_lock(&sriov->rej_tmout_lock);
	sts = radix_tree_insert(&sriov->rej_tmout_root, (unsigned long)rem_pv_cm_id, item);
	mutex_unlock(&sriov->rej_tmout_lock);
	if (sts)
		goto err_insert;

	schedule_delayed_work(&item->timeout, CM_CLEANUP_CACHE_TIMEOUT);

	return 0;

err_insert:
	kfree(item);
	return sts;
}

static int lookup_rej_tmout_slave(struct mlx4_ib_sriov *sriov, u32 rem_pv_cm_id)
{
	struct rej_tmout_entry *item;

	mutex_lock(&sriov->rej_tmout_lock);
	item = radix_tree_lookup(&sriov->rej_tmout_root, (unsigned long)rem_pv_cm_id);
	mutex_unlock(&sriov->rej_tmout_lock);

	if (!item || IS_ERR(item)) {
		pr_debug("Could not find slave. rem_pv_cm_id 0x%x error: %d\n",
			 rem_pv_cm_id, (int)PTR_ERR(item));
		return !item ? -ENOENT : PTR_ERR(item);
	}

	return item->slave;
}

int mlx4_ib_demux_cm_handler(struct ib_device *ibdev, int port, int *slave,
			     struct ib_mad *mad)
{
	struct mlx4_ib_sriov *sriov = &to_mdev(ibdev)->sriov;
	u32 rem_pv_cm_id = get_local_comm_id(mad);
	u32 pv_cm_id;
	struct id_map_entry *id;
	int sts;

	if (mad->mad_hdr.attr_id == CM_REQ_ATTR_ID ||
	    mad->mad_hdr.attr_id == CM_SIDR_REQ_ATTR_ID) {
		union ib_gid gid;

		if (!slave)
			return 0;

		gid = gid_from_req_msg(ibdev, mad);
		*slave = mlx4_ib_find_real_gid(ibdev, port, gid.global.interface_id);
		if (*slave < 0) {
			mlx4_ib_warn(ibdev, "failed matching slave_id by gid (0x%llx)\n",
				     be64_to_cpu(gid.global.interface_id));
			return -ENOENT;
		}

		sts = alloc_rej_tmout(sriov, rem_pv_cm_id, *slave);
		if (sts)
			/* Even if this fails, we pass on the REQ to the slave */
			pr_debug("Could not allocate rej_tmout entry. rem_pv_cm_id 0x%x slave %d status %d\n",
				 rem_pv_cm_id, *slave, sts);

		return 0;
	}

	pv_cm_id = get_remote_comm_id(mad);
	id = id_map_get(ibdev, (int *)&pv_cm_id, -1, -1);

	if (!id) {
		if (mad->mad_hdr.attr_id == CM_REJ_ATTR_ID &&
		    REJ_REASON(mad) == IB_CM_REJ_TIMEOUT && slave) {
			*slave = lookup_rej_tmout_slave(sriov, rem_pv_cm_id);

			return (*slave < 0) ? *slave : 0;
		}
		pr_debug("Couldn't find an entry for pv_cm_id 0x%x, attr_id 0x%x\n",
			 pv_cm_id, be16_to_cpu(mad->mad_hdr.attr_id));
		return -ENOENT;
	}

	if (slave)
		*slave = id->slave_id;
	set_remote_comm_id(mad, id->sl_cm_id);

	if (mad->mad_hdr.attr_id == CM_DREQ_ATTR_ID ||
	    mad->mad_hdr.attr_id == CM_REJ_ATTR_ID)
		schedule_delayed(ibdev, id);

	return 0;
}

void mlx4_ib_cm_paravirt_init(struct mlx4_ib_dev *dev)
{
	spin_lock_init(&dev->sriov.id_map_lock);
	INIT_LIST_HEAD(&dev->sriov.cm_list);
	dev->sriov.sl_id_map = RB_ROOT;
	xa_init_flags(&dev->sriov.pv_id_table, XA_FLAGS_ALLOC);
	mutex_init(&dev->sriov.rej_tmout_lock);
	INIT_RADIX_TREE(&dev->sriov.rej_tmout_root, GFP_KERNEL);
}

static void rej_tmout_tree_cleanup(struct mlx4_ib_sriov *sriov, int slave)
{
	struct radix_tree_iter iter;
	bool flush_needed = false;
	__rcu void **slot;
	int cnt = 0;

	mutex_lock(&sriov->rej_tmout_lock);
	radix_tree_for_each_slot(slot, &sriov->rej_tmout_root, &iter, 0) {
		struct rej_tmout_entry *item = *slot;

		if (slave < 0 || slave == item->slave) {
			mod_delayed_work(system_wq, &item->timeout, 0);
			flush_needed = true;
			++cnt;
		}
	}
	mutex_unlock(&sriov->rej_tmout_lock);

	if (flush_needed) {
		flush_scheduled_work();
		pr_debug("Deleted %d entries in radix_tree for slave %d during cleanup\n",
			 slave, cnt);
	}
}

/* slave = -1 ==> all slaves */
/* TBD -- call paravirt clean for single slave.  Need for slave RESET event */
void mlx4_ib_cm_paravirt_clean(struct mlx4_ib_dev *dev, int slave)
{
	struct mlx4_ib_sriov *sriov = &dev->sriov;
	struct rb_root *sl_id_map = &sriov->sl_id_map;
	struct list_head lh;
	struct rb_node *nd;
	int need_flush = 0;
	struct id_map_entry *map, *tmp_map;
	/* cancel all delayed work queue entries */
	INIT_LIST_HEAD(&lh);
	spin_lock(&sriov->id_map_lock);
	list_for_each_entry_safe(map, tmp_map, &dev->sriov.cm_list, list) {
		if (slave < 0 || slave == map->slave_id) {
			if (map->scheduled_delete)
				need_flush |= !cancel_delayed_work(&map->timeout);
		}
	}

	spin_unlock(&sriov->id_map_lock);

	if (need_flush)
		flush_scheduled_work(); /* make sure all timers were flushed */

	/* now, remove all leftover entries from databases*/
	spin_lock(&sriov->id_map_lock);
	if (slave < 0) {
		while (rb_first(sl_id_map)) {
			struct id_map_entry *ent =
				rb_entry(rb_first(sl_id_map),
					 struct id_map_entry, node);

			rb_erase(&ent->node, sl_id_map);
			xa_erase(&sriov->pv_id_table, ent->pv_cm_id);
		}
		list_splice_init(&dev->sriov.cm_list, &lh);
	} else {
		/* first, move nodes belonging to slave to db remove list */
		nd = rb_first(sl_id_map);
		while (nd) {
			struct id_map_entry *ent =
				rb_entry(nd, struct id_map_entry, node);
			nd = rb_next(nd);
			if (ent->slave_id == slave)
				list_move_tail(&ent->list, &lh);
		}
		/* remove those nodes from databases */
		list_for_each_entry_safe(map, tmp_map, &lh, list) {
			rb_erase(&map->node, sl_id_map);
			xa_erase(&sriov->pv_id_table, map->pv_cm_id);
		}

		/* add remaining nodes from cm_list */
		list_for_each_entry_safe(map, tmp_map, &dev->sriov.cm_list, list) {
			if (slave == map->slave_id)
				list_move_tail(&map->list, &lh);
		}
	}

	spin_unlock(&sriov->id_map_lock);

	/* free any map entries left behind due to cancel_delayed_work above */
	list_for_each_entry_safe(map, tmp_map, &lh, list) {
		list_del(&map->list);
		kfree(map);
	}

	rej_tmout_tree_cleanup(sriov, slave);
}
