// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice_sched.h"

/**
 * ice_sched_add_root_node - Insert the Tx scheduler root node in SW DB
 * @pi: port information structure
 * @info: Scheduler element information from firmware
 *
 * This function inserts the root node of the scheduling tree topology
 * to the SW DB.
 */
static enum ice_status
ice_sched_add_root_node(struct ice_port_info *pi,
			struct ice_aqc_txsched_elem_data *info)
{
	struct ice_sched_node *root;
	struct ice_hw *hw;
	u16 max_children;

	if (!pi)
		return ICE_ERR_PARAM;

	hw = pi->hw;

	root = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*root), GFP_KERNEL);
	if (!root)
		return ICE_ERR_NO_MEMORY;

	max_children = le16_to_cpu(hw->layer_info[0].max_children);
	root->children = devm_kcalloc(ice_hw_to_dev(hw), max_children,
				      sizeof(*root), GFP_KERNEL);
	if (!root->children) {
		devm_kfree(ice_hw_to_dev(hw), root);
		return ICE_ERR_NO_MEMORY;
	}

	memcpy(&root->info, info, sizeof(*info));
	pi->root = root;
	return 0;
}

/**
 * ice_sched_find_node_by_teid - Find the Tx scheduler node in SW DB
 * @start_node: pointer to the starting ice_sched_node struct in a sub-tree
 * @teid: node teid to search
 *
 * This function searches for a node matching the teid in the scheduling tree
 * from the SW DB. The search is recursive and is restricted by the number of
 * layers it has searched through; stopping at the max supported layer.
 *
 * This function needs to be called when holding the port_info->sched_lock
 */
struct ice_sched_node *
ice_sched_find_node_by_teid(struct ice_sched_node *start_node, u32 teid)
{
	u16 i;

	/* The TEID is same as that of the start_node */
	if (ICE_TXSCHED_GET_NODE_TEID(start_node) == teid)
		return start_node;

	/* The node has no children or is at the max layer */
	if (!start_node->num_children ||
	    start_node->tx_sched_layer >= ICE_AQC_TOPO_MAX_LEVEL_NUM ||
	    start_node->info.data.elem_type == ICE_AQC_ELEM_TYPE_LEAF)
		return NULL;

	/* Check if teid matches to any of the children nodes */
	for (i = 0; i < start_node->num_children; i++)
		if (ICE_TXSCHED_GET_NODE_TEID(start_node->children[i]) == teid)
			return start_node->children[i];

	/* Search within each child's sub-tree */
	for (i = 0; i < start_node->num_children; i++) {
		struct ice_sched_node *tmp;

		tmp = ice_sched_find_node_by_teid(start_node->children[i],
						  teid);
		if (tmp)
			return tmp;
	}

	return NULL;
}

/**
 * ice_sched_add_node - Insert the Tx scheduler node in SW DB
 * @pi: port information structure
 * @layer: Scheduler layer of the node
 * @info: Scheduler element information from firmware
 *
 * This function inserts a scheduler node to the SW DB.
 */
enum ice_status
ice_sched_add_node(struct ice_port_info *pi, u8 layer,
		   struct ice_aqc_txsched_elem_data *info)
{
	struct ice_sched_node *parent;
	struct ice_sched_node *node;
	struct ice_hw *hw;
	u16 max_children;

	if (!pi)
		return ICE_ERR_PARAM;

	hw = pi->hw;

	/* A valid parent node should be there */
	parent = ice_sched_find_node_by_teid(pi->root,
					     le32_to_cpu(info->parent_teid));
	if (!parent) {
		ice_debug(hw, ICE_DBG_SCHED,
			  "Parent Node not found for parent_teid=0x%x\n",
			  le32_to_cpu(info->parent_teid));
		return ICE_ERR_PARAM;
	}

	node = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*node), GFP_KERNEL);
	if (!node)
		return ICE_ERR_NO_MEMORY;
	max_children = le16_to_cpu(hw->layer_info[layer].max_children);
	if (max_children) {
		node->children = devm_kcalloc(ice_hw_to_dev(hw), max_children,
					      sizeof(*node), GFP_KERNEL);
		if (!node->children) {
			devm_kfree(ice_hw_to_dev(hw), node);
			return ICE_ERR_NO_MEMORY;
		}
	}

	node->in_use = true;
	node->parent = parent;
	node->tx_sched_layer = layer;
	parent->children[parent->num_children++] = node;
	memcpy(&node->info, info, sizeof(*info));
	return 0;
}

/**
 * ice_aq_delete_sched_elems - delete scheduler elements
 * @hw: pointer to the hw struct
 * @grps_req: number of groups to delete
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @grps_del: returns total number of elements deleted
 * @cd: pointer to command details structure or NULL
 *
 * Delete scheduling elements (0x040F)
 */
static enum ice_status
ice_aq_delete_sched_elems(struct ice_hw *hw, u16 grps_req,
			  struct ice_aqc_delete_elem *buf, u16 buf_size,
			  u16 *grps_del, struct ice_sq_cd *cd)
{
	struct ice_aqc_add_move_delete_elem *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.add_move_delete_elem;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_delete_sched_elems);
	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);
	cmd->num_grps_req = cpu_to_le16(grps_req);

	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status && grps_del)
		*grps_del = le16_to_cpu(cmd->num_grps_updated);

	return status;
}

/**
 * ice_sched_remove_elems - remove nodes from hw
 * @hw: pointer to the hw struct
 * @parent: pointer to the parent node
 * @num_nodes: number of nodes
 * @node_teids: array of node teids to be deleted
 *
 * This function remove nodes from hw
 */
static enum ice_status
ice_sched_remove_elems(struct ice_hw *hw, struct ice_sched_node *parent,
		       u16 num_nodes, u32 *node_teids)
{
	struct ice_aqc_delete_elem *buf;
	u16 i, num_groups_removed = 0;
	enum ice_status status;
	u16 buf_size;

	buf_size = sizeof(*buf) + sizeof(u32) * (num_nodes - 1);
	buf = devm_kzalloc(ice_hw_to_dev(hw), buf_size, GFP_KERNEL);
	if (!buf)
		return ICE_ERR_NO_MEMORY;
	buf->hdr.parent_teid = parent->info.node_teid;
	buf->hdr.num_elems = cpu_to_le16(num_nodes);
	for (i = 0; i < num_nodes; i++)
		buf->teid[i] = cpu_to_le32(node_teids[i]);
	status = ice_aq_delete_sched_elems(hw, 1, buf, buf_size,
					   &num_groups_removed, NULL);
	if (status || num_groups_removed != 1)
		ice_debug(hw, ICE_DBG_SCHED, "remove elements failed\n");
	devm_kfree(ice_hw_to_dev(hw), buf);
	return status;
}

/**
 * ice_sched_get_first_node - get the first node of the given layer
 * @hw: pointer to the hw struct
 * @parent: pointer the base node of the subtree
 * @layer: layer number
 *
 * This function retrieves the first node of the given layer from the subtree
 */
static struct ice_sched_node *
ice_sched_get_first_node(struct ice_hw *hw, struct ice_sched_node *parent,
			 u8 layer)
{
	u8 i;

	if (layer < hw->sw_entry_point_layer)
		return NULL;
	for (i = 0; i < parent->num_children; i++) {
		struct ice_sched_node *node = parent->children[i];

		if (node) {
			if (node->tx_sched_layer == layer)
				return node;
			/* this recursion is intentional, and wouldn't
			 * go more than 9 calls
			 */
			return ice_sched_get_first_node(hw, node, layer);
		}
	}
	return NULL;
}

/**
 * ice_sched_get_tc_node - get pointer to TC node
 * @pi: port information structure
 * @tc: TC number
 *
 * This function returns the TC node pointer
 */
struct ice_sched_node *ice_sched_get_tc_node(struct ice_port_info *pi, u8 tc)
{
	u8 i;

	if (!pi)
		return NULL;
	for (i = 0; i < pi->root->num_children; i++)
		if (pi->root->children[i]->tc_num == tc)
			return pi->root->children[i];
	return NULL;
}

/**
 * ice_free_sched_node - Free a Tx scheduler node from SW DB
 * @pi: port information structure
 * @node: pointer to the ice_sched_node struct
 *
 * This function frees up a node from SW DB as well as from HW
 *
 * This function needs to be called with the port_info->sched_lock held
 */
void ice_free_sched_node(struct ice_port_info *pi, struct ice_sched_node *node)
{
	struct ice_sched_node *parent;
	struct ice_hw *hw = pi->hw;
	u8 i, j;

	/* Free the children before freeing up the parent node
	 * The parent array is updated below and that shifts the nodes
	 * in the array. So always pick the first child if num children > 0
	 */
	while (node->num_children)
		ice_free_sched_node(pi, node->children[0]);

	/* Leaf, TC and root nodes can't be deleted by SW */
	if (node->tx_sched_layer >= hw->sw_entry_point_layer &&
	    node->info.data.elem_type != ICE_AQC_ELEM_TYPE_TC &&
	    node->info.data.elem_type != ICE_AQC_ELEM_TYPE_ROOT_PORT &&
	    node->info.data.elem_type != ICE_AQC_ELEM_TYPE_LEAF) {
		u32 teid = le32_to_cpu(node->info.node_teid);
		enum ice_status status;

		status = ice_sched_remove_elems(hw, node->parent, 1, &teid);
		if (status)
			ice_debug(hw, ICE_DBG_SCHED,
				  "remove element failed %d\n", status);
	}
	parent = node->parent;
	/* root has no parent */
	if (parent) {
		struct ice_sched_node *p, *tc_node;

		/* update the parent */
		for (i = 0; i < parent->num_children; i++)
			if (parent->children[i] == node) {
				for (j = i + 1; j < parent->num_children; j++)
					parent->children[j - 1] =
						parent->children[j];
				parent->num_children--;
				break;
			}

		/* search for previous sibling that points to this node and
		 * remove the reference
		 */
		tc_node = ice_sched_get_tc_node(pi, node->tc_num);
		if (!tc_node) {
			ice_debug(hw, ICE_DBG_SCHED,
				  "Invalid TC number %d\n", node->tc_num);
			goto err_exit;
		}
		p = ice_sched_get_first_node(hw, tc_node, node->tx_sched_layer);
		while (p) {
			if (p->sibling == node) {
				p->sibling = node->sibling;
				break;
			}
			p = p->sibling;
		}
	}
err_exit:
	/* leaf nodes have no children */
	if (node->children)
		devm_kfree(ice_hw_to_dev(hw), node->children);
	devm_kfree(ice_hw_to_dev(hw), node);
}

/**
 * ice_aq_get_dflt_topo - gets default scheduler topology
 * @hw: pointer to the hw struct
 * @lport: logical port number
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @num_branches: returns total number of queue to port branches
 * @cd: pointer to command details structure or NULL
 *
 * Get default scheduler topology (0x400)
 */
static enum ice_status
ice_aq_get_dflt_topo(struct ice_hw *hw, u8 lport,
		     struct ice_aqc_get_topo_elem *buf, u16 buf_size,
		     u8 *num_branches, struct ice_sq_cd *cd)
{
	struct ice_aqc_get_topo *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.get_topo;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_dflt_topo);
	cmd->port_num = lport;
	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status && num_branches)
		*num_branches = cmd->num_branches;

	return status;
}

/**
 * ice_aq_query_sched_res - query scheduler resource
 * @hw: pointer to the hw struct
 * @buf_size: buffer size in bytes
 * @buf: pointer to buffer
 * @cd: pointer to command details structure or NULL
 *
 * Query scheduler resource allocation (0x0412)
 */
static enum ice_status
ice_aq_query_sched_res(struct ice_hw *hw, u16 buf_size,
		       struct ice_aqc_query_txsched_res_resp *buf,
		       struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_query_sched_res);
	return ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
}

/**
 * ice_sched_clear_tx_topo - clears the schduler tree nodes
 * @pi: port information structure
 *
 * This function removes all the nodes from HW as well as from SW DB.
 */
static void ice_sched_clear_tx_topo(struct ice_port_info *pi)
{
	struct ice_sched_agg_info *agg_info;
	struct ice_sched_vsi_info *vsi_elem;
	struct ice_sched_agg_info *atmp;
	struct ice_sched_vsi_info *tmp;
	struct ice_hw *hw;

	if (!pi)
		return;

	hw = pi->hw;

	list_for_each_entry_safe(agg_info, atmp, &pi->agg_list, list_entry) {
		struct ice_sched_agg_vsi_info *agg_vsi_info;
		struct ice_sched_agg_vsi_info *vtmp;

		list_for_each_entry_safe(agg_vsi_info, vtmp,
					 &agg_info->agg_vsi_list, list_entry) {
			list_del(&agg_vsi_info->list_entry);
			devm_kfree(ice_hw_to_dev(hw), agg_vsi_info);
		}
	}

	/* remove the vsi list */
	list_for_each_entry_safe(vsi_elem, tmp, &pi->vsi_info_list,
				 list_entry) {
		list_del(&vsi_elem->list_entry);
		devm_kfree(ice_hw_to_dev(hw), vsi_elem);
	}

	if (pi->root) {
		ice_free_sched_node(pi, pi->root);
		pi->root = NULL;
	}
}

/**
 * ice_sched_clear_port - clear the scheduler elements from SW DB for a port
 * @pi: port information structure
 *
 * Cleanup scheduling elements from SW DB
 */
static void ice_sched_clear_port(struct ice_port_info *pi)
{
	if (!pi || pi->port_state != ICE_SCHED_PORT_STATE_READY)
		return;

	pi->port_state = ICE_SCHED_PORT_STATE_INIT;
	mutex_lock(&pi->sched_lock);
	ice_sched_clear_tx_topo(pi);
	mutex_unlock(&pi->sched_lock);
	mutex_destroy(&pi->sched_lock);
}

/**
 * ice_sched_cleanup_all - cleanup scheduler elements from SW DB for all ports
 * @hw: pointer to the hw struct
 *
 * Cleanup scheduling elements from SW DB for all the ports
 */
void ice_sched_cleanup_all(struct ice_hw *hw)
{
	if (!hw || !hw->port_info)
		return;

	if (hw->layer_info)
		devm_kfree(ice_hw_to_dev(hw), hw->layer_info);

	ice_sched_clear_port(hw->port_info);

	hw->num_tx_sched_layers = 0;
	hw->num_tx_sched_phys_layers = 0;
	hw->flattened_layers = 0;
	hw->max_cgds = 0;
}

/**
 * ice_sched_get_qgrp_layer - get the current queue group layer number
 * @hw: pointer to the hw struct
 *
 * This function returns the current queue group layer number
 */
static u8 ice_sched_get_qgrp_layer(struct ice_hw *hw)
{
	/* It's always total layers - 1, the array is 0 relative so -2 */
	return hw->num_tx_sched_layers - ICE_QGRP_LAYER_OFFSET;
}

/**
 * ice_rm_dflt_leaf_node - remove the default leaf node in the tree
 * @pi: port information structure
 *
 * This function removes the leaf node that was created by the FW
 * during initialization
 */
static void
ice_rm_dflt_leaf_node(struct ice_port_info *pi)
{
	struct ice_sched_node *node;

	node = pi->root;
	while (node) {
		if (!node->num_children)
			break;
		node = node->children[0];
	}
	if (node && node->info.data.elem_type == ICE_AQC_ELEM_TYPE_LEAF) {
		u32 teid = le32_to_cpu(node->info.node_teid);
		enum ice_status status;

		/* remove the default leaf node */
		status = ice_sched_remove_elems(pi->hw, node->parent, 1, &teid);
		if (!status)
			ice_free_sched_node(pi, node);
	}
}

/**
 * ice_sched_rm_dflt_nodes - free the default nodes in the tree
 * @pi: port information structure
 *
 * This function frees all the nodes except root and TC that were created by
 * the FW during initialization
 */
static void
ice_sched_rm_dflt_nodes(struct ice_port_info *pi)
{
	struct ice_sched_node *node;

	ice_rm_dflt_leaf_node(pi);
	/* remove the default nodes except TC and root nodes */
	node = pi->root;
	while (node) {
		if (node->tx_sched_layer >= pi->hw->sw_entry_point_layer &&
		    node->info.data.elem_type != ICE_AQC_ELEM_TYPE_TC &&
		    node->info.data.elem_type != ICE_AQC_ELEM_TYPE_ROOT_PORT) {
			ice_free_sched_node(pi, node);
			break;
		}
		if (!node->num_children)
			break;
		node = node->children[0];
	}
}

/**
 * ice_sched_init_port - Initialize scheduler by querying information from FW
 * @pi: port info structure for the tree to cleanup
 *
 * This function is the initial call to find the total number of Tx scheduler
 * resources, default topology created by firmware and storing the information
 * in SW DB.
 */
enum ice_status ice_sched_init_port(struct ice_port_info *pi)
{
	struct ice_aqc_get_topo_elem *buf;
	enum ice_status status;
	struct ice_hw *hw;
	u8 num_branches;
	u16 num_elems;
	u8 i, j;

	if (!pi)
		return ICE_ERR_PARAM;
	hw = pi->hw;

	/* Query the Default Topology from FW */
	buf = devm_kcalloc(ice_hw_to_dev(hw), ICE_TXSCHED_MAX_BRANCHES,
			   sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ICE_ERR_NO_MEMORY;

	/* Query default scheduling tree topology */
	status = ice_aq_get_dflt_topo(hw, pi->lport, buf,
				      sizeof(*buf) * ICE_TXSCHED_MAX_BRANCHES,
				      &num_branches, NULL);
	if (status)
		goto err_init_port;

	/* num_branches should be between 1-8 */
	if (num_branches < 1 || num_branches > ICE_TXSCHED_MAX_BRANCHES) {
		ice_debug(hw, ICE_DBG_SCHED, "num_branches unexpected %d\n",
			  num_branches);
		status = ICE_ERR_PARAM;
		goto err_init_port;
	}

	/* get the number of elements on the default/first branch */
	num_elems = le16_to_cpu(buf[0].hdr.num_elems);

	/* num_elems should always be between 1-9 */
	if (num_elems < 1 || num_elems > ICE_AQC_TOPO_MAX_LEVEL_NUM) {
		ice_debug(hw, ICE_DBG_SCHED, "num_elems unexpected %d\n",
			  num_elems);
		status = ICE_ERR_PARAM;
		goto err_init_port;
	}

	/* If the last node is a leaf node then the index of the Q group
	 * layer is two less than the number of elements.
	 */
	if (num_elems > 2 && buf[0].generic[num_elems - 1].data.elem_type ==
	    ICE_AQC_ELEM_TYPE_LEAF)
		pi->last_node_teid =
			le32_to_cpu(buf[0].generic[num_elems - 2].node_teid);
	else
		pi->last_node_teid =
			le32_to_cpu(buf[0].generic[num_elems - 1].node_teid);

	/* Insert the Tx Sched root node */
	status = ice_sched_add_root_node(pi, &buf[0].generic[0]);
	if (status)
		goto err_init_port;

	/* Parse the default tree and cache the information */
	for (i = 0; i < num_branches; i++) {
		num_elems = le16_to_cpu(buf[i].hdr.num_elems);

		/* Skip root element as already inserted */
		for (j = 1; j < num_elems; j++) {
			/* update the sw entry point */
			if (buf[0].generic[j].data.elem_type ==
			    ICE_AQC_ELEM_TYPE_ENTRY_POINT)
				hw->sw_entry_point_layer = j;

			status = ice_sched_add_node(pi, j, &buf[i].generic[j]);
			if (status)
				goto err_init_port;
		}
	}

	/* Remove the default nodes. */
	if (pi->root)
		ice_sched_rm_dflt_nodes(pi);

	/* initialize the port for handling the scheduler tree */
	pi->port_state = ICE_SCHED_PORT_STATE_READY;
	mutex_init(&pi->sched_lock);
	INIT_LIST_HEAD(&pi->agg_list);
	INIT_LIST_HEAD(&pi->vsi_info_list);

err_init_port:
	if (status && pi->root) {
		ice_free_sched_node(pi, pi->root);
		pi->root = NULL;
	}

	devm_kfree(ice_hw_to_dev(hw), buf);
	return status;
}

/**
 * ice_sched_query_res_alloc - query the FW for num of logical sched layers
 * @hw: pointer to the HW struct
 *
 * query FW for allocated scheduler resources and store in HW struct
 */
enum ice_status ice_sched_query_res_alloc(struct ice_hw *hw)
{
	struct ice_aqc_query_txsched_res_resp *buf;
	enum ice_status status = 0;

	if (hw->layer_info)
		return status;

	buf = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ICE_ERR_NO_MEMORY;

	status = ice_aq_query_sched_res(hw, sizeof(*buf), buf, NULL);
	if (status)
		goto sched_query_out;

	hw->num_tx_sched_layers = le16_to_cpu(buf->sched_props.logical_levels);
	hw->num_tx_sched_phys_layers =
		le16_to_cpu(buf->sched_props.phys_levels);
	hw->flattened_layers = buf->sched_props.flattening_bitmap;
	hw->max_cgds = buf->sched_props.max_pf_cgds;

	 hw->layer_info = devm_kmemdup(ice_hw_to_dev(hw), buf->layer_props,
				       (hw->num_tx_sched_layers *
					sizeof(*hw->layer_info)),
				       GFP_KERNEL);
	if (!hw->layer_info) {
		status = ICE_ERR_NO_MEMORY;
		goto sched_query_out;
	}

sched_query_out:
	devm_kfree(ice_hw_to_dev(hw), buf);
	return status;
}

/**
 * ice_sched_get_vsi_info_entry - Get the vsi entry list for given vsi_id
 * @pi: port information structure
 * @vsi_id: vsi id
 *
 * This function retrieves the vsi list for the given vsi id
 */
static struct ice_sched_vsi_info *
ice_sched_get_vsi_info_entry(struct ice_port_info *pi, u16 vsi_id)
{
	struct ice_sched_vsi_info *list_elem;

	if (!pi)
		return NULL;

	list_for_each_entry(list_elem, &pi->vsi_info_list, list_entry)
		if (list_elem->vsi_id == vsi_id)
			return list_elem;
	return NULL;
}

/**
 * ice_sched_find_node_in_subtree - Find node in part of base node subtree
 * @hw: pointer to the hw struct
 * @base: pointer to the base node
 * @node: pointer to the node to search
 *
 * This function checks whether a given node is part of the base node
 * subtree or not
 */
static bool
ice_sched_find_node_in_subtree(struct ice_hw *hw, struct ice_sched_node *base,
			       struct ice_sched_node *node)
{
	u8 i;

	for (i = 0; i < base->num_children; i++) {
		struct ice_sched_node *child = base->children[i];

		if (node == child)
			return true;
		if (child->tx_sched_layer > node->tx_sched_layer)
			return false;
		/* this recursion is intentional, and wouldn't
		 * go more than 8 calls
		 */
		if (ice_sched_find_node_in_subtree(hw, child, node))
			return true;
	}
	return false;
}

/**
 * ice_sched_get_free_qparent - Get a free lan or rdma q group node
 * @pi: port information structure
 * @vsi_id: vsi id
 * @tc: branch number
 * @owner: lan or rdma
 *
 * This function retrieves a free lan or rdma q group node
 */
struct ice_sched_node *
ice_sched_get_free_qparent(struct ice_port_info *pi, u16 vsi_id, u8 tc,
			   u8 owner)
{
	struct ice_sched_node *vsi_node, *qgrp_node = NULL;
	struct ice_sched_vsi_info *list_elem;
	u16 max_children;
	u8 qgrp_layer;

	qgrp_layer = ice_sched_get_qgrp_layer(pi->hw);
	max_children = le16_to_cpu(pi->hw->layer_info[qgrp_layer].max_children);
	list_elem = ice_sched_get_vsi_info_entry(pi, vsi_id);
	if (!list_elem)
		goto lan_q_exit;
	vsi_node = list_elem->vsi_node[tc];
	/* validate invalid VSI id */
	if (!vsi_node)
		goto lan_q_exit;
	/* get the first q group node from VSI sub-tree */
	qgrp_node = ice_sched_get_first_node(pi->hw, vsi_node, qgrp_layer);
	while (qgrp_node) {
		/* make sure the qgroup node is part of the VSI subtree */
		if (ice_sched_find_node_in_subtree(pi->hw, vsi_node, qgrp_node))
			if (qgrp_node->num_children < max_children &&
			    qgrp_node->owner == owner)
				break;
		qgrp_node = qgrp_node->sibling;
	}
lan_q_exit:
	return qgrp_node;
}
