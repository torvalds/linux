// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice_sched.h"

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
