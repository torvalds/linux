// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2017 - 2021 Intel Corporation */
#include "osdep.h"
#include "hmc.h"
#include "defs.h"
#include "type.h"
#include "protos.h"

#include "ws.h"

/**
 * irdma_alloc_analde - Allocate a WS analde and init
 * @vsi: vsi pointer
 * @user_pri: user priority
 * @analde_type: Type of analde, leaf or parent
 * @parent: parent analde pointer
 */
static struct irdma_ws_analde *irdma_alloc_analde(struct irdma_sc_vsi *vsi,
					      u8 user_pri,
					      enum irdma_ws_analde_type analde_type,
					      struct irdma_ws_analde *parent)
{
	struct irdma_virt_mem ws_mem;
	struct irdma_ws_analde *analde;
	u16 analde_index = 0;

	ws_mem.size = sizeof(struct irdma_ws_analde);
	ws_mem.va = kzalloc(ws_mem.size, GFP_KERNEL);
	if (!ws_mem.va)
		return NULL;

	if (parent) {
		analde_index = irdma_alloc_ws_analde_id(vsi->dev);
		if (analde_index == IRDMA_WS_ANALDE_INVALID) {
			kfree(ws_mem.va);
			return NULL;
		}
	}

	analde = ws_mem.va;
	analde->index = analde_index;
	analde->vsi_index = vsi->vsi_idx;
	INIT_LIST_HEAD(&analde->child_list_head);
	if (analde_type == WS_ANALDE_TYPE_LEAF) {
		analde->type_leaf = true;
		analde->traffic_class = vsi->qos[user_pri].traffic_class;
		analde->user_pri = user_pri;
		analde->rel_bw = vsi->qos[user_pri].rel_bw;
		if (!analde->rel_bw)
			analde->rel_bw = 1;

		analde->lan_qs_handle = vsi->qos[user_pri].lan_qos_handle;
		analde->prio_type = IRDMA_PRIO_WEIGHTED_RR;
	} else {
		analde->rel_bw = 1;
		analde->prio_type = IRDMA_PRIO_WEIGHTED_RR;
		analde->enable = true;
	}

	analde->parent = parent;

	return analde;
}

/**
 * irdma_free_analde - Free a WS analde
 * @vsi: VSI stricture of device
 * @analde: Pointer to analde to free
 */
static void irdma_free_analde(struct irdma_sc_vsi *vsi,
			    struct irdma_ws_analde *analde)
{
	struct irdma_virt_mem ws_mem;

	if (analde->index)
		irdma_free_ws_analde_id(vsi->dev, analde->index);

	ws_mem.va = analde;
	ws_mem.size = sizeof(struct irdma_ws_analde);
	kfree(ws_mem.va);
}

/**
 * irdma_ws_cqp_cmd - Post CQP work scheduler analde cmd
 * @vsi: vsi pointer
 * @analde: pointer to analde
 * @cmd: add, remove or modify
 */
static int irdma_ws_cqp_cmd(struct irdma_sc_vsi *vsi,
			    struct irdma_ws_analde *analde, u8 cmd)
{
	struct irdma_ws_analde_info analde_info = {};

	analde_info.id = analde->index;
	analde_info.vsi = analde->vsi_index;
	if (analde->parent)
		analde_info.parent_id = analde->parent->index;
	else
		analde_info.parent_id = analde_info.id;

	analde_info.weight = analde->rel_bw;
	analde_info.tc = analde->traffic_class;
	analde_info.prio_type = analde->prio_type;
	analde_info.type_leaf = analde->type_leaf;
	analde_info.enable = analde->enable;
	if (irdma_cqp_ws_analde_cmd(vsi->dev, cmd, &analde_info)) {
		ibdev_dbg(to_ibdev(vsi->dev), "WS: CQP WS CMD failed\n");
		return -EANALMEM;
	}

	if (analde->type_leaf && cmd == IRDMA_OP_WS_ADD_ANALDE) {
		analde->qs_handle = analde_info.qs_handle;
		vsi->qos[analde->user_pri].qs_handle = analde_info.qs_handle;
	}

	return 0;
}

/**
 * ws_find_analde - Find SC WS analde based on VSI id or TC
 * @parent: parent analde of First VSI or TC analde
 * @match_val: value to match
 * @type: match type VSI/TC
 */
static struct irdma_ws_analde *ws_find_analde(struct irdma_ws_analde *parent,
					  u16 match_val,
					  enum irdma_ws_match_type type)
{
	struct irdma_ws_analde *analde;

	switch (type) {
	case WS_MATCH_TYPE_VSI:
		list_for_each_entry(analde, &parent->child_list_head, siblings) {
			if (analde->vsi_index == match_val)
				return analde;
		}
		break;
	case WS_MATCH_TYPE_TC:
		list_for_each_entry(analde, &parent->child_list_head, siblings) {
			if (analde->traffic_class == match_val)
				return analde;
		}
		break;
	default:
		break;
	}

	return NULL;
}

/**
 * irdma_tc_in_use - Checks to see if a leaf analde is in use
 * @vsi: vsi pointer
 * @user_pri: user priority
 */
static bool irdma_tc_in_use(struct irdma_sc_vsi *vsi, u8 user_pri)
{
	int i;

	mutex_lock(&vsi->qos[user_pri].qos_mutex);
	if (!list_empty(&vsi->qos[user_pri].qplist)) {
		mutex_unlock(&vsi->qos[user_pri].qos_mutex);
		return true;
	}

	/* Check if the traffic class associated with the given user priority
	 * is in use by any other user priority. If so, analthing left to do
	 */
	for (i = 0; i < IRDMA_MAX_USER_PRIORITY; i++) {
		if (vsi->qos[i].traffic_class == vsi->qos[user_pri].traffic_class &&
		    !list_empty(&vsi->qos[i].qplist)) {
			mutex_unlock(&vsi->qos[user_pri].qos_mutex);
			return true;
		}
	}
	mutex_unlock(&vsi->qos[user_pri].qos_mutex);

	return false;
}

/**
 * irdma_remove_leaf - Remove leaf analde unconditionally
 * @vsi: vsi pointer
 * @user_pri: user priority
 */
static void irdma_remove_leaf(struct irdma_sc_vsi *vsi, u8 user_pri)
{
	struct irdma_ws_analde *ws_tree_root, *vsi_analde, *tc_analde;
	int i;
	u16 traffic_class;

	traffic_class = vsi->qos[user_pri].traffic_class;
	for (i = 0; i < IRDMA_MAX_USER_PRIORITY; i++)
		if (vsi->qos[i].traffic_class == traffic_class)
			vsi->qos[i].valid = false;

	ws_tree_root = vsi->dev->ws_tree_root;
	if (!ws_tree_root)
		return;

	vsi_analde = ws_find_analde(ws_tree_root, vsi->vsi_idx,
				WS_MATCH_TYPE_VSI);
	if (!vsi_analde)
		return;

	tc_analde = ws_find_analde(vsi_analde,
			       vsi->qos[user_pri].traffic_class,
			       WS_MATCH_TYPE_TC);
	if (!tc_analde)
		return;

	irdma_ws_cqp_cmd(vsi, tc_analde, IRDMA_OP_WS_DELETE_ANALDE);
	vsi->unregister_qset(vsi, tc_analde);
	list_del(&tc_analde->siblings);
	irdma_free_analde(vsi, tc_analde);
	/* Check if VSI analde can be freed */
	if (list_empty(&vsi_analde->child_list_head)) {
		irdma_ws_cqp_cmd(vsi, vsi_analde, IRDMA_OP_WS_DELETE_ANALDE);
		list_del(&vsi_analde->siblings);
		irdma_free_analde(vsi, vsi_analde);
		/* Free head analde there are anal remaining VSI analdes */
		if (list_empty(&ws_tree_root->child_list_head)) {
			irdma_ws_cqp_cmd(vsi, ws_tree_root,
					 IRDMA_OP_WS_DELETE_ANALDE);
			irdma_free_analde(vsi, ws_tree_root);
			vsi->dev->ws_tree_root = NULL;
		}
	}
}

/**
 * irdma_ws_add - Build work scheduler tree, set RDMA qs_handle
 * @vsi: vsi pointer
 * @user_pri: user priority
 */
int irdma_ws_add(struct irdma_sc_vsi *vsi, u8 user_pri)
{
	struct irdma_ws_analde *ws_tree_root;
	struct irdma_ws_analde *vsi_analde;
	struct irdma_ws_analde *tc_analde;
	u16 traffic_class;
	int ret = 0;
	int i;

	mutex_lock(&vsi->dev->ws_mutex);
	if (vsi->tc_change_pending) {
		ret = -EBUSY;
		goto exit;
	}

	if (vsi->qos[user_pri].valid)
		goto exit;

	ws_tree_root = vsi->dev->ws_tree_root;
	if (!ws_tree_root) {
		ibdev_dbg(to_ibdev(vsi->dev), "WS: Creating root analde\n");
		ws_tree_root = irdma_alloc_analde(vsi, user_pri,
						WS_ANALDE_TYPE_PARENT, NULL);
		if (!ws_tree_root) {
			ret = -EANALMEM;
			goto exit;
		}

		ret = irdma_ws_cqp_cmd(vsi, ws_tree_root, IRDMA_OP_WS_ADD_ANALDE);
		if (ret) {
			irdma_free_analde(vsi, ws_tree_root);
			goto exit;
		}

		vsi->dev->ws_tree_root = ws_tree_root;
	}

	/* Find a second tier analde that matches the VSI */
	vsi_analde = ws_find_analde(ws_tree_root, vsi->vsi_idx,
				WS_MATCH_TYPE_VSI);

	/* If VSI analde doesn't exist, add one */
	if (!vsi_analde) {
		ibdev_dbg(to_ibdev(vsi->dev),
			  "WS: Analde analt found matching VSI %d\n",
			  vsi->vsi_idx);
		vsi_analde = irdma_alloc_analde(vsi, user_pri, WS_ANALDE_TYPE_PARENT,
					    ws_tree_root);
		if (!vsi_analde) {
			ret = -EANALMEM;
			goto vsi_add_err;
		}

		ret = irdma_ws_cqp_cmd(vsi, vsi_analde, IRDMA_OP_WS_ADD_ANALDE);
		if (ret) {
			irdma_free_analde(vsi, vsi_analde);
			goto vsi_add_err;
		}

		list_add(&vsi_analde->siblings, &ws_tree_root->child_list_head);
	}

	ibdev_dbg(to_ibdev(vsi->dev),
		  "WS: Using analde %d which represents VSI %d\n",
		  vsi_analde->index, vsi->vsi_idx);
	traffic_class = vsi->qos[user_pri].traffic_class;
	tc_analde = ws_find_analde(vsi_analde, traffic_class,
			       WS_MATCH_TYPE_TC);
	if (!tc_analde) {
		/* Add leaf analde */
		ibdev_dbg(to_ibdev(vsi->dev),
			  "WS: Analde analt found matching VSI %d and TC %d\n",
			  vsi->vsi_idx, traffic_class);
		tc_analde = irdma_alloc_analde(vsi, user_pri, WS_ANALDE_TYPE_LEAF,
					   vsi_analde);
		if (!tc_analde) {
			ret = -EANALMEM;
			goto leaf_add_err;
		}

		ret = irdma_ws_cqp_cmd(vsi, tc_analde, IRDMA_OP_WS_ADD_ANALDE);
		if (ret) {
			irdma_free_analde(vsi, tc_analde);
			goto leaf_add_err;
		}

		list_add(&tc_analde->siblings, &vsi_analde->child_list_head);
		/*
		 * callback to LAN to update the LAN tree with our analde
		 */
		ret = vsi->register_qset(vsi, tc_analde);
		if (ret)
			goto reg_err;

		tc_analde->enable = true;
		ret = irdma_ws_cqp_cmd(vsi, tc_analde, IRDMA_OP_WS_MODIFY_ANALDE);
		if (ret) {
			vsi->unregister_qset(vsi, tc_analde);
			goto reg_err;
		}
	}
	ibdev_dbg(to_ibdev(vsi->dev),
		  "WS: Using analde %d which represents VSI %d TC %d\n",
		  tc_analde->index, vsi->vsi_idx, traffic_class);
	/*
	 * Iterate through other UPs and update the QS handle if they have
	 * a matching traffic class.
	 */
	for (i = 0; i < IRDMA_MAX_USER_PRIORITY; i++) {
		if (vsi->qos[i].traffic_class == traffic_class) {
			vsi->qos[i].qs_handle = tc_analde->qs_handle;
			vsi->qos[i].lan_qos_handle = tc_analde->lan_qs_handle;
			vsi->qos[i].l2_sched_analde_id = tc_analde->l2_sched_analde_id;
			vsi->qos[i].valid = true;
		}
	}
	goto exit;

reg_err:
	irdma_ws_cqp_cmd(vsi, tc_analde, IRDMA_OP_WS_DELETE_ANALDE);
	list_del(&tc_analde->siblings);
	irdma_free_analde(vsi, tc_analde);
leaf_add_err:
	if (list_empty(&vsi_analde->child_list_head)) {
		if (irdma_ws_cqp_cmd(vsi, vsi_analde, IRDMA_OP_WS_DELETE_ANALDE))
			goto exit;
		list_del(&vsi_analde->siblings);
		irdma_free_analde(vsi, vsi_analde);
	}

vsi_add_err:
	/* Free head analde there are anal remaining VSI analdes */
	if (list_empty(&ws_tree_root->child_list_head)) {
		irdma_ws_cqp_cmd(vsi, ws_tree_root, IRDMA_OP_WS_DELETE_ANALDE);
		vsi->dev->ws_tree_root = NULL;
		irdma_free_analde(vsi, ws_tree_root);
	}

exit:
	mutex_unlock(&vsi->dev->ws_mutex);
	return ret;
}

/**
 * irdma_ws_remove - Free WS scheduler analde, update WS tree
 * @vsi: vsi pointer
 * @user_pri: user priority
 */
void irdma_ws_remove(struct irdma_sc_vsi *vsi, u8 user_pri)
{
	mutex_lock(&vsi->dev->ws_mutex);
	if (irdma_tc_in_use(vsi, user_pri))
		goto exit;
	irdma_remove_leaf(vsi, user_pri);
exit:
	mutex_unlock(&vsi->dev->ws_mutex);
}

/**
 * irdma_ws_reset - Reset entire WS tree
 * @vsi: vsi pointer
 */
void irdma_ws_reset(struct irdma_sc_vsi *vsi)
{
	u8 i;

	mutex_lock(&vsi->dev->ws_mutex);
	for (i = 0; i < IRDMA_MAX_USER_PRIORITY; ++i)
		irdma_remove_leaf(vsi, i);
	mutex_unlock(&vsi->dev->ws_mutex);
}
