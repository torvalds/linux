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

	if (!pi)
		return ICE_ERR_PARAM;

	hw = pi->hw;

	root = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*root), GFP_KERNEL);
	if (!root)
		return ICE_ERR_NO_MEMORY;

	/* coverity[suspicious_sizeof] */
	root->children = devm_kcalloc(ice_hw_to_dev(hw), hw->max_children[0],
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
 * ice_aq_query_sched_elems - query scheduler elements
 * @hw: pointer to the hw struct
 * @elems_req: number of elements to query
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @elems_ret: returns total number of elements returned
 * @cd: pointer to command details structure or NULL
 *
 * Query scheduling elements (0x0404)
 */
static enum ice_status
ice_aq_query_sched_elems(struct ice_hw *hw, u16 elems_req,
			 struct ice_aqc_get_elem *buf, u16 buf_size,
			 u16 *elems_ret, struct ice_sq_cd *cd)
{
	struct ice_aqc_get_cfg_elem *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.get_update_elem;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_sched_elems);
	cmd->num_elem_req = cpu_to_le16(elems_req);
	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);
	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status && elems_ret)
		*elems_ret = le16_to_cpu(cmd->num_elem_resp);

	return status;
}

/**
 * ice_sched_query_elem - query element information from hw
 * @hw: pointer to the hw struct
 * @node_teid: node teid to be queried
 * @buf: buffer to element information
 *
 * This function queries HW element information
 */
static enum ice_status
ice_sched_query_elem(struct ice_hw *hw, u32 node_teid,
		     struct ice_aqc_get_elem *buf)
{
	u16 buf_size, num_elem_ret = 0;
	enum ice_status status;

	buf_size = sizeof(*buf);
	memset(buf, 0, buf_size);
	buf->generic[0].node_teid = cpu_to_le32(node_teid);
	status = ice_aq_query_sched_elems(hw, 1, buf, buf_size, &num_elem_ret,
					  NULL);
	if (status || num_elem_ret != 1)
		ice_debug(hw, ICE_DBG_SCHED, "query element failed\n");
	return status;
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
	struct ice_aqc_get_elem elem;
	struct ice_sched_node *node;
	enum ice_status status;
	struct ice_hw *hw;

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

	/* query the current node information from FW  before additing it
	 * to the SW DB
	 */
	status = ice_sched_query_elem(hw, le32_to_cpu(info->node_teid), &elem);
	if (status)
		return status;

	node = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*node), GFP_KERNEL);
	if (!node)
		return ICE_ERR_NO_MEMORY;
	if (hw->max_children[layer]) {
		/* coverity[suspicious_sizeof] */
		node->children = devm_kcalloc(ice_hw_to_dev(hw),
					      hw->max_children[layer],
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
	memcpy(&node->info, &elem.generic[0], sizeof(node->info));
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
 * ice_aq_add_sched_elems - adds scheduling element
 * @hw: pointer to the hw struct
 * @grps_req: the number of groups that are requested to be added
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @grps_added: returns total number of groups added
 * @cd: pointer to command details structure or NULL
 *
 * Add scheduling elements (0x0401)
 */
static enum ice_status
ice_aq_add_sched_elems(struct ice_hw *hw, u16 grps_req,
		       struct ice_aqc_add_elem *buf, u16 buf_size,
		       u16 *grps_added, struct ice_sq_cd *cd)
{
	struct ice_aqc_add_move_delete_elem *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.add_move_delete_elem;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_add_sched_elems);
	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);

	cmd->num_grps_req = cpu_to_le16(grps_req);
	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status && grps_added)
		*grps_added = le16_to_cpu(cmd->num_grps_updated);

	return status;
}

/**
 * ice_suspend_resume_elems - suspend/resume scheduler elements
 * @hw: pointer to the hw struct
 * @elems_req: number of elements to suspend
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @elems_ret: returns total number of elements suspended
 * @cd: pointer to command details structure or NULL
 * @cmd_code: command code for suspend or resume
 *
 * suspend/resume scheduler elements
 */
static enum ice_status
ice_suspend_resume_elems(struct ice_hw *hw, u16 elems_req,
			 struct ice_aqc_suspend_resume_elem *buf, u16 buf_size,
			 u16 *elems_ret, struct ice_sq_cd *cd,
			 enum ice_adminq_opc cmd_code)
{
	struct ice_aqc_get_cfg_elem *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.get_update_elem;
	ice_fill_dflt_direct_cmd_desc(&desc, cmd_code);
	cmd->num_elem_req = cpu_to_le16(elems_req);
	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);
	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status && elems_ret)
		*elems_ret = le16_to_cpu(cmd->num_elem_resp);
	return status;
}

/**
 * ice_aq_suspend_sched_elems - suspend scheduler elements
 * @hw: pointer to the hw struct
 * @elems_req: number of elements to suspend
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @elems_ret: returns total number of elements suspended
 * @cd: pointer to command details structure or NULL
 *
 * Suspend scheduling elements (0x0409)
 */
static enum ice_status
ice_aq_suspend_sched_elems(struct ice_hw *hw, u16 elems_req,
			   struct ice_aqc_suspend_resume_elem *buf,
			   u16 buf_size, u16 *elems_ret, struct ice_sq_cd *cd)
{
	return ice_suspend_resume_elems(hw, elems_req, buf, buf_size, elems_ret,
					cd, ice_aqc_opc_suspend_sched_elems);
}

/**
 * ice_aq_resume_sched_elems - resume scheduler elements
 * @hw: pointer to the hw struct
 * @elems_req: number of elements to resume
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @elems_ret: returns total number of elements resumed
 * @cd: pointer to command details structure or NULL
 *
 * resume scheduling elements (0x040A)
 */
static enum ice_status
ice_aq_resume_sched_elems(struct ice_hw *hw, u16 elems_req,
			  struct ice_aqc_suspend_resume_elem *buf,
			  u16 buf_size, u16 *elems_ret, struct ice_sq_cd *cd)
{
	return ice_suspend_resume_elems(hw, elems_req, buf, buf_size, elems_ret,
					cd, ice_aqc_opc_resume_sched_elems);
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
 * ice_sched_suspend_resume_elems - suspend or resume hw nodes
 * @hw: pointer to the hw struct
 * @num_nodes: number of nodes
 * @node_teids: array of node teids to be suspended or resumed
 * @suspend: true means suspend / false means resume
 *
 * This function suspends or resumes hw nodes
 */
static enum ice_status
ice_sched_suspend_resume_elems(struct ice_hw *hw, u8 num_nodes, u32 *node_teids,
			       bool suspend)
{
	struct ice_aqc_suspend_resume_elem *buf;
	u16 i, buf_size, num_elem_ret = 0;
	enum ice_status status;

	buf_size = sizeof(*buf) * num_nodes;
	buf = devm_kzalloc(ice_hw_to_dev(hw), buf_size, GFP_KERNEL);
	if (!buf)
		return ICE_ERR_NO_MEMORY;

	for (i = 0; i < num_nodes; i++)
		buf->teid[i] = cpu_to_le32(node_teids[i]);

	if (suspend)
		status = ice_aq_suspend_sched_elems(hw, num_nodes, buf,
						    buf_size, &num_elem_ret,
						    NULL);
	else
		status = ice_aq_resume_sched_elems(hw, num_nodes, buf,
						   buf_size, &num_elem_ret,
						   NULL);
	if (status || num_elem_ret != num_nodes)
		ice_debug(hw, ICE_DBG_SCHED, "suspend/resume failed\n");

	devm_kfree(ice_hw_to_dev(hw), buf);
	return status;
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
	struct ice_sched_agg_info *atmp;
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
void ice_sched_clear_port(struct ice_port_info *pi)
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
	if (!hw)
		return;

	if (hw->layer_info) {
		devm_kfree(ice_hw_to_dev(hw), hw->layer_info);
		hw->layer_info = NULL;
	}

	if (hw->port_info)
		ice_sched_clear_port(hw->port_info);

	hw->num_tx_sched_layers = 0;
	hw->num_tx_sched_phys_layers = 0;
	hw->flattened_layers = 0;
	hw->max_cgds = 0;
}

/**
 * ice_sched_add_elems - add nodes to hw and SW DB
 * @pi: port information structure
 * @tc_node: pointer to the branch node
 * @parent: pointer to the parent node
 * @layer: layer number to add nodes
 * @num_nodes: number of nodes
 * @num_nodes_added: pointer to num nodes added
 * @first_node_teid: if new nodes are added then return the teid of first node
 *
 * This function add nodes to hw as well as to SW DB for a given layer
 */
static enum ice_status
ice_sched_add_elems(struct ice_port_info *pi, struct ice_sched_node *tc_node,
		    struct ice_sched_node *parent, u8 layer, u16 num_nodes,
		    u16 *num_nodes_added, u32 *first_node_teid)
{
	struct ice_sched_node *prev, *new_node;
	struct ice_aqc_add_elem *buf;
	u16 i, num_groups_added = 0;
	enum ice_status status = 0;
	struct ice_hw *hw = pi->hw;
	u16 buf_size;
	u32 teid;

	buf_size = sizeof(*buf) + sizeof(*buf->generic) * (num_nodes - 1);
	buf = devm_kzalloc(ice_hw_to_dev(hw), buf_size, GFP_KERNEL);
	if (!buf)
		return ICE_ERR_NO_MEMORY;

	buf->hdr.parent_teid = parent->info.node_teid;
	buf->hdr.num_elems = cpu_to_le16(num_nodes);
	for (i = 0; i < num_nodes; i++) {
		buf->generic[i].parent_teid = parent->info.node_teid;
		buf->generic[i].data.elem_type = ICE_AQC_ELEM_TYPE_SE_GENERIC;
		buf->generic[i].data.valid_sections =
			ICE_AQC_ELEM_VALID_GENERIC | ICE_AQC_ELEM_VALID_CIR |
			ICE_AQC_ELEM_VALID_EIR;
		buf->generic[i].data.generic = 0;
		buf->generic[i].data.cir_bw.bw_profile_idx =
			cpu_to_le16(ICE_SCHED_DFLT_RL_PROF_ID);
		buf->generic[i].data.cir_bw.bw_alloc =
			cpu_to_le16(ICE_SCHED_DFLT_BW_WT);
		buf->generic[i].data.eir_bw.bw_profile_idx =
			cpu_to_le16(ICE_SCHED_DFLT_RL_PROF_ID);
		buf->generic[i].data.eir_bw.bw_alloc =
			cpu_to_le16(ICE_SCHED_DFLT_BW_WT);
	}

	status = ice_aq_add_sched_elems(hw, 1, buf, buf_size,
					&num_groups_added, NULL);
	if (status || num_groups_added != 1) {
		ice_debug(hw, ICE_DBG_SCHED, "add elements failed\n");
		devm_kfree(ice_hw_to_dev(hw), buf);
		return ICE_ERR_CFG;
	}

	*num_nodes_added = num_nodes;
	/* add nodes to the SW DB */
	for (i = 0; i < num_nodes; i++) {
		status = ice_sched_add_node(pi, layer, &buf->generic[i]);
		if (status) {
			ice_debug(hw, ICE_DBG_SCHED,
				  "add nodes in SW DB failed status =%d\n",
				  status);
			break;
		}

		teid = le32_to_cpu(buf->generic[i].node_teid);
		new_node = ice_sched_find_node_by_teid(parent, teid);
		if (!new_node) {
			ice_debug(hw, ICE_DBG_SCHED,
				  "Node is missing for teid =%d\n", teid);
			break;
		}

		new_node->sibling = NULL;
		new_node->tc_num = tc_node->tc_num;

		/* add it to previous node sibling pointer */
		/* Note: siblings are not linked across branches */
		prev = ice_sched_get_first_node(hw, tc_node, layer);
		if (prev && prev != new_node) {
			while (prev->sibling)
				prev = prev->sibling;
			prev->sibling = new_node;
		}

		if (i == 0)
			*first_node_teid = teid;
	}

	devm_kfree(ice_hw_to_dev(hw), buf);
	return status;
}

/**
 * ice_sched_add_nodes_to_layer - Add nodes to a given layer
 * @pi: port information structure
 * @tc_node: pointer to TC node
 * @parent: pointer to parent node
 * @layer: layer number to add nodes
 * @num_nodes: number of nodes to be added
 * @first_node_teid: pointer to the first node teid
 * @num_nodes_added: pointer to number of nodes added
 *
 * This function add nodes to a given layer.
 */
static enum ice_status
ice_sched_add_nodes_to_layer(struct ice_port_info *pi,
			     struct ice_sched_node *tc_node,
			     struct ice_sched_node *parent, u8 layer,
			     u16 num_nodes, u32 *first_node_teid,
			     u16 *num_nodes_added)
{
	u32 *first_teid_ptr = first_node_teid;
	u16 new_num_nodes, max_child_nodes;
	enum ice_status status = 0;
	struct ice_hw *hw = pi->hw;
	u16 num_added = 0;
	u32 temp;

	*num_nodes_added = 0;

	if (!num_nodes)
		return status;

	if (!parent || layer < hw->sw_entry_point_layer)
		return ICE_ERR_PARAM;

	/* max children per node per layer */
	max_child_nodes = hw->max_children[parent->tx_sched_layer];

	/* current number of children + required nodes exceed max children ? */
	if ((parent->num_children + num_nodes) > max_child_nodes) {
		/* Fail if the parent is a TC node */
		if (parent == tc_node)
			return ICE_ERR_CFG;

		/* utilize all the spaces if the parent is not full */
		if (parent->num_children < max_child_nodes) {
			new_num_nodes = max_child_nodes - parent->num_children;
			/* this recursion is intentional, and wouldn't
			 * go more than 2 calls
			 */
			status = ice_sched_add_nodes_to_layer(pi, tc_node,
							      parent, layer,
							      new_num_nodes,
							      first_node_teid,
							      &num_added);
			if (status)
				return status;

			*num_nodes_added += num_added;
		}
		/* Don't modify the first node teid memory if the first node was
		 * added already in the above call. Instead send some temp
		 * memory for all other recursive calls.
		 */
		if (num_added)
			first_teid_ptr = &temp;

		new_num_nodes = num_nodes - num_added;

		/* This parent is full, try the next sibling */
		parent = parent->sibling;

		/* this recursion is intentional, for 1024 queues
		 * per VSI, it goes max of 16 iterations.
		 * 1024 / 8 = 128 layer 8 nodes
		 * 128 /8 = 16 (add 8 nodes per iteration)
		 */
		status = ice_sched_add_nodes_to_layer(pi, tc_node, parent,
						      layer, new_num_nodes,
						      first_teid_ptr,
						      &num_added);
		*num_nodes_added += num_added;
		return status;
	}

	status = ice_sched_add_elems(pi, tc_node, parent, layer, num_nodes,
				     num_nodes_added, first_node_teid);
	return status;
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
 * ice_sched_get_vsi_layer - get the current VSI layer number
 * @hw: pointer to the hw struct
 *
 * This function returns the current VSI layer number
 */
static u8 ice_sched_get_vsi_layer(struct ice_hw *hw)
{
	/* Num Layers       VSI layer
	 *     9               6
	 *     7               4
	 *     5 or less       sw_entry_point_layer
	 */
	/* calculate the vsi layer based on number of layers. */
	if (hw->num_tx_sched_layers > ICE_VSI_LAYER_OFFSET + 1) {
		u8 layer = hw->num_tx_sched_layers - ICE_VSI_LAYER_OFFSET;

		if (layer > hw->sw_entry_point_layer)
			return layer;
	}
	return hw->sw_entry_point_layer;
}

/**
 * ice_rm_dflt_leaf_node - remove the default leaf node in the tree
 * @pi: port information structure
 *
 * This function removes the leaf node that was created by the FW
 * during initialization
 */
static void ice_rm_dflt_leaf_node(struct ice_port_info *pi)
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
static void ice_sched_rm_dflt_nodes(struct ice_port_info *pi)
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
	buf = devm_kzalloc(ice_hw_to_dev(hw), ICE_AQ_MAX_BUF_LEN, GFP_KERNEL);
	if (!buf)
		return ICE_ERR_NO_MEMORY;

	/* Query default scheduling tree topology */
	status = ice_aq_get_dflt_topo(hw, pi->lport, buf, ICE_AQ_MAX_BUF_LEN,
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
	__le16 max_sibl;
	u8 i;

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

	/* max sibling group size of current layer refers to the max children
	 * of the below layer node.
	 * layer 1 node max children will be layer 2 max sibling group size
	 * layer 2 node max children will be layer 3 max sibling group size
	 * and so on. This array will be populated from root (index 0) to
	 * qgroup layer 7. Leaf node has no children.
	 */
	for (i = 0; i < hw->num_tx_sched_layers; i++) {
		max_sibl = buf->layer_props[i].max_sibl_grp_sz;
		hw->max_children[i] = le16_to_cpu(max_sibl);
	}

	hw->layer_info = (struct ice_aqc_layer_props *)
			  devm_kmemdup(ice_hw_to_dev(hw), buf->layer_props,
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
 * @vsi_handle: software VSI handle
 * @tc: branch number
 * @owner: lan or rdma
 *
 * This function retrieves a free lan or rdma q group node
 */
struct ice_sched_node *
ice_sched_get_free_qparent(struct ice_port_info *pi, u16 vsi_handle, u8 tc,
			   u8 owner)
{
	struct ice_sched_node *vsi_node, *qgrp_node = NULL;
	struct ice_vsi_ctx *vsi_ctx;
	u16 max_children;
	u8 qgrp_layer;

	qgrp_layer = ice_sched_get_qgrp_layer(pi->hw);
	max_children = pi->hw->max_children[qgrp_layer];

	vsi_ctx = ice_get_vsi_ctx(pi->hw, vsi_handle);
	if (!vsi_ctx)
		return NULL;
	vsi_node = vsi_ctx->sched.vsi_node[tc];
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

/**
 * ice_sched_get_vsi_node - Get a VSI node based on VSI id
 * @hw: pointer to the hw struct
 * @tc_node: pointer to the TC node
 * @vsi_handle: software VSI handle
 *
 * This function retrieves a VSI node for a given VSI id from a given
 * TC branch
 */
static struct ice_sched_node *
ice_sched_get_vsi_node(struct ice_hw *hw, struct ice_sched_node *tc_node,
		       u16 vsi_handle)
{
	struct ice_sched_node *node;
	u8 vsi_layer;

	vsi_layer = ice_sched_get_vsi_layer(hw);
	node = ice_sched_get_first_node(hw, tc_node, vsi_layer);

	/* Check whether it already exists */
	while (node) {
		if (node->vsi_handle == vsi_handle)
			return node;
		node = node->sibling;
	}

	return node;
}

/**
 * ice_sched_calc_vsi_child_nodes - calculate number of VSI child nodes
 * @hw: pointer to the hw struct
 * @num_qs: number of queues
 * @num_nodes: num nodes array
 *
 * This function calculates the number of VSI child nodes based on the
 * number of queues.
 */
static void
ice_sched_calc_vsi_child_nodes(struct ice_hw *hw, u16 num_qs, u16 *num_nodes)
{
	u16 num = num_qs;
	u8 i, qgl, vsil;

	qgl = ice_sched_get_qgrp_layer(hw);
	vsil = ice_sched_get_vsi_layer(hw);

	/* calculate num nodes from q group to VSI layer */
	for (i = qgl; i > vsil; i--) {
		/* round to the next integer if there is a remainder */
		num = DIV_ROUND_UP(num, hw->max_children[i]);

		/* need at least one node */
		num_nodes[i] = num ? num : 1;
	}
}

/**
 * ice_sched_add_vsi_child_nodes - add VSI child nodes to tree
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc_node: pointer to the TC node
 * @num_nodes: pointer to the num nodes that needs to be added per layer
 * @owner: node owner (lan or rdma)
 *
 * This function adds the VSI child nodes to tree. It gets called for
 * lan and rdma separately.
 */
static enum ice_status
ice_sched_add_vsi_child_nodes(struct ice_port_info *pi, u16 vsi_handle,
			      struct ice_sched_node *tc_node, u16 *num_nodes,
			      u8 owner)
{
	struct ice_sched_node *parent, *node;
	struct ice_hw *hw = pi->hw;
	enum ice_status status;
	u32 first_node_teid;
	u16 num_added = 0;
	u8 i, qgl, vsil;

	qgl = ice_sched_get_qgrp_layer(hw);
	vsil = ice_sched_get_vsi_layer(hw);
	parent = ice_sched_get_vsi_node(hw, tc_node, vsi_handle);
	for (i = vsil + 1; i <= qgl; i++) {
		if (!parent)
			return ICE_ERR_CFG;

		status = ice_sched_add_nodes_to_layer(pi, tc_node, parent, i,
						      num_nodes[i],
						      &first_node_teid,
						      &num_added);
		if (status || num_nodes[i] != num_added)
			return ICE_ERR_CFG;

		/* The newly added node can be a new parent for the next
		 * layer nodes
		 */
		if (num_added) {
			parent = ice_sched_find_node_by_teid(tc_node,
							     first_node_teid);
			node = parent;
			while (node) {
				node->owner = owner;
				node = node->sibling;
			}
		} else {
			parent = parent->children[0];
		}
	}

	return 0;
}

/**
 * ice_sched_rm_vsi_child_nodes - remove VSI child nodes from the tree
 * @pi: port information structure
 * @vsi_node: pointer to the VSI node
 * @num_nodes: pointer to the num nodes that needs to be removed per layer
 * @owner: node owner (lan or rdma)
 *
 * This function removes the VSI child nodes from the tree. It gets called for
 * lan and rdma separately.
 */
static void
ice_sched_rm_vsi_child_nodes(struct ice_port_info *pi,
			     struct ice_sched_node *vsi_node, u16 *num_nodes,
			     u8 owner)
{
	struct ice_sched_node *node, *next;
	u8 i, qgl, vsil;
	u16 num;

	qgl = ice_sched_get_qgrp_layer(pi->hw);
	vsil = ice_sched_get_vsi_layer(pi->hw);

	for (i = qgl; i > vsil; i--) {
		num = num_nodes[i];
		node = ice_sched_get_first_node(pi->hw, vsi_node, i);
		while (node && num) {
			next = node->sibling;
			if (node->owner == owner && !node->num_children) {
				ice_free_sched_node(pi, node);
				num--;
			}
			node = next;
		}
	}
}

/**
 * ice_sched_calc_vsi_support_nodes - calculate number of VSI support nodes
 * @hw: pointer to the hw struct
 * @tc_node: pointer to TC node
 * @num_nodes: pointer to num nodes array
 *
 * This function calculates the number of supported nodes needed to add this
 * VSI into Tx tree including the VSI, parent and intermediate nodes in below
 * layers
 */
static void
ice_sched_calc_vsi_support_nodes(struct ice_hw *hw,
				 struct ice_sched_node *tc_node, u16 *num_nodes)
{
	struct ice_sched_node *node;
	u8 vsil;
	int i;

	vsil = ice_sched_get_vsi_layer(hw);
	for (i = vsil; i >= hw->sw_entry_point_layer; i--)
		/* Add intermediate nodes if TC has no children and
		 * need at least one node for VSI
		 */
		if (!tc_node->num_children || i == vsil) {
			num_nodes[i]++;
		} else {
			/* If intermediate nodes are reached max children
			 * then add a new one.
			 */
			node = ice_sched_get_first_node(hw, tc_node, (u8)i);
			/* scan all the siblings */
			while (node) {
				if (node->num_children < hw->max_children[i])
					break;
				node = node->sibling;
			}

			/* all the nodes are full, allocate a new one */
			if (!node)
				num_nodes[i]++;
		}
}

/**
 * ice_sched_add_vsi_support_nodes - add VSI supported nodes into Tx tree
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc_node: pointer to TC node
 * @num_nodes: pointer to num nodes array
 *
 * This function adds the VSI supported nodes into Tx tree including the
 * VSI, its parent and intermediate nodes in below layers
 */
static enum ice_status
ice_sched_add_vsi_support_nodes(struct ice_port_info *pi, u16 vsi_handle,
				struct ice_sched_node *tc_node, u16 *num_nodes)
{
	struct ice_sched_node *parent = tc_node;
	enum ice_status status;
	u32 first_node_teid;
	u16 num_added = 0;
	u8 i, vsil;

	if (!pi)
		return ICE_ERR_PARAM;

	vsil = ice_sched_get_vsi_layer(pi->hw);
	for (i = pi->hw->sw_entry_point_layer; i <= vsil; i++) {
		status = ice_sched_add_nodes_to_layer(pi, tc_node, parent,
						      i, num_nodes[i],
						      &first_node_teid,
						      &num_added);
		if (status || num_nodes[i] != num_added)
			return ICE_ERR_CFG;

		/* The newly added node can be a new parent for the next
		 * layer nodes
		 */
		if (num_added)
			parent = ice_sched_find_node_by_teid(tc_node,
							     first_node_teid);
		else
			parent = parent->children[0];

		if (!parent)
			return ICE_ERR_CFG;

		if (i == vsil)
			parent->vsi_handle = vsi_handle;
	}

	return 0;
}

/**
 * ice_sched_add_vsi_to_topo - add a new VSI into tree
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc: TC number
 *
 * This function adds a new VSI into scheduler tree
 */
static enum ice_status
ice_sched_add_vsi_to_topo(struct ice_port_info *pi, u16 vsi_handle, u8 tc)
{
	u16 num_nodes[ICE_AQC_TOPO_MAX_LEVEL_NUM] = { 0 };
	struct ice_sched_node *tc_node;
	struct ice_hw *hw = pi->hw;

	tc_node = ice_sched_get_tc_node(pi, tc);
	if (!tc_node)
		return ICE_ERR_PARAM;

	/* calculate number of supported nodes needed for this VSI */
	ice_sched_calc_vsi_support_nodes(hw, tc_node, num_nodes);

	/* add vsi supported nodes to tc subtree */
	return ice_sched_add_vsi_support_nodes(pi, vsi_handle, tc_node,
					       num_nodes);
}

/**
 * ice_sched_update_vsi_child_nodes - update VSI child nodes
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc: TC number
 * @new_numqs: new number of max queues
 * @owner: owner of this subtree
 *
 * This function updates the VSI child nodes based on the number of queues
 */
static enum ice_status
ice_sched_update_vsi_child_nodes(struct ice_port_info *pi, u16 vsi_handle,
				 u8 tc, u16 new_numqs, u8 owner)
{
	u16 prev_num_nodes[ICE_AQC_TOPO_MAX_LEVEL_NUM] = { 0 };
	u16 new_num_nodes[ICE_AQC_TOPO_MAX_LEVEL_NUM] = { 0 };
	struct ice_sched_node *vsi_node;
	struct ice_sched_node *tc_node;
	struct ice_vsi_ctx *vsi_ctx;
	enum ice_status status = 0;
	struct ice_hw *hw = pi->hw;
	u16 prev_numqs;
	u8 i;

	tc_node = ice_sched_get_tc_node(pi, tc);
	if (!tc_node)
		return ICE_ERR_CFG;

	vsi_node = ice_sched_get_vsi_node(hw, tc_node, vsi_handle);
	if (!vsi_node)
		return ICE_ERR_CFG;

	vsi_ctx = ice_get_vsi_ctx(hw, vsi_handle);
	if (!vsi_ctx)
		return ICE_ERR_PARAM;

	if (owner == ICE_SCHED_NODE_OWNER_LAN)
		prev_numqs = vsi_ctx->sched.max_lanq[tc];
	else
		return ICE_ERR_PARAM;

	/* num queues are not changed */
	if (prev_numqs == new_numqs)
		return status;

	/* calculate number of nodes based on prev/new number of qs */
	if (prev_numqs)
		ice_sched_calc_vsi_child_nodes(hw, prev_numqs, prev_num_nodes);

	if (new_numqs)
		ice_sched_calc_vsi_child_nodes(hw, new_numqs, new_num_nodes);

	if (prev_numqs > new_numqs) {
		for (i = 0; i < ICE_AQC_TOPO_MAX_LEVEL_NUM; i++)
			new_num_nodes[i] = prev_num_nodes[i] - new_num_nodes[i];

		ice_sched_rm_vsi_child_nodes(pi, vsi_node, new_num_nodes,
					     owner);
	} else {
		for (i = 0; i < ICE_AQC_TOPO_MAX_LEVEL_NUM; i++)
			new_num_nodes[i] -= prev_num_nodes[i];

		status = ice_sched_add_vsi_child_nodes(pi, vsi_handle, tc_node,
						       new_num_nodes, owner);
		if (status)
			return status;
	}

	vsi_ctx->sched.max_lanq[tc] = new_numqs;

	return status;
}

/**
 * ice_sched_cfg_vsi - configure the new/existing VSI
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc: TC number
 * @maxqs: max number of queues
 * @owner: lan or rdma
 * @enable: TC enabled or disabled
 *
 * This function adds/updates VSI nodes based on the number of queues. If TC is
 * enabled and VSI is in suspended state then resume the VSI back. If TC is
 * disabled then suspend the VSI if it is not already.
 */
enum ice_status
ice_sched_cfg_vsi(struct ice_port_info *pi, u16 vsi_handle, u8 tc, u16 maxqs,
		  u8 owner, bool enable)
{
	struct ice_sched_node *vsi_node, *tc_node;
	struct ice_vsi_ctx *vsi_ctx;
	enum ice_status status = 0;
	struct ice_hw *hw = pi->hw;

	tc_node = ice_sched_get_tc_node(pi, tc);
	if (!tc_node)
		return ICE_ERR_PARAM;
	vsi_ctx = ice_get_vsi_ctx(hw, vsi_handle);
	if (!vsi_ctx)
		return ICE_ERR_PARAM;
	vsi_node = ice_sched_get_vsi_node(hw, tc_node, vsi_handle);

	/* suspend the VSI if tc is not enabled */
	if (!enable) {
		if (vsi_node && vsi_node->in_use) {
			u32 teid = le32_to_cpu(vsi_node->info.node_teid);

			status = ice_sched_suspend_resume_elems(hw, 1, &teid,
								true);
			if (!status)
				vsi_node->in_use = false;
		}
		return status;
	}

	/* TC is enabled, if it is a new VSI then add it to the tree */
	if (!vsi_node) {
		status = ice_sched_add_vsi_to_topo(pi, vsi_handle, tc);
		if (status)
			return status;

		vsi_node = ice_sched_get_vsi_node(hw, tc_node, vsi_handle);
		if (!vsi_node)
			return ICE_ERR_CFG;

		vsi_ctx->sched.vsi_node[tc] = vsi_node;
		vsi_node->in_use = true;
		/* invalidate the max queues whenever VSI gets added first time
		 * into the scheduler tree (boot or after reset). We need to
		 * recreate the child nodes all the time in these cases.
		 */
		vsi_ctx->sched.max_lanq[tc] = 0;
	}

	/* update the VSI child nodes */
	status = ice_sched_update_vsi_child_nodes(pi, vsi_handle, tc, maxqs,
						  owner);
	if (status)
		return status;

	/* TC is enabled, resume the VSI if it is in the suspend state */
	if (!vsi_node->in_use) {
		u32 teid = le32_to_cpu(vsi_node->info.node_teid);

		status = ice_sched_suspend_resume_elems(hw, 1, &teid, false);
		if (!status)
			vsi_node->in_use = true;
	}

	return status;
}

/**
 * ice_sched_rm_agg_vsi_entry - remove agg related VSI info entry
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 *
 * This function removes single aggregator VSI info entry from
 * aggregator list.
 */
static void
ice_sched_rm_agg_vsi_info(struct ice_port_info *pi, u16 vsi_handle)
{
	struct ice_sched_agg_info *agg_info;
	struct ice_sched_agg_info *atmp;

	list_for_each_entry_safe(agg_info, atmp, &pi->agg_list, list_entry) {
		struct ice_sched_agg_vsi_info *agg_vsi_info;
		struct ice_sched_agg_vsi_info *vtmp;

		list_for_each_entry_safe(agg_vsi_info, vtmp,
					 &agg_info->agg_vsi_list, list_entry)
			if (agg_vsi_info->vsi_handle == vsi_handle) {
				list_del(&agg_vsi_info->list_entry);
				devm_kfree(ice_hw_to_dev(pi->hw),
					   agg_vsi_info);
				return;
			}
	}
}

/**
 * ice_sched_rm_vsi_cfg - remove the VSI and its children nodes
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @owner: LAN or RDMA
 *
 * This function removes the VSI and its LAN or RDMA children nodes from the
 * scheduler tree.
 */
static enum ice_status
ice_sched_rm_vsi_cfg(struct ice_port_info *pi, u16 vsi_handle, u8 owner)
{
	enum ice_status status = ICE_ERR_PARAM;
	struct ice_vsi_ctx *vsi_ctx;
	u8 i, j = 0;

	if (!ice_is_vsi_valid(pi->hw, vsi_handle))
		return status;
	mutex_lock(&pi->sched_lock);
	vsi_ctx = ice_get_vsi_ctx(pi->hw, vsi_handle);
	if (!vsi_ctx)
		goto exit_sched_rm_vsi_cfg;

	for (i = 0; i < ICE_MAX_TRAFFIC_CLASS; i++) {
		struct ice_sched_node *vsi_node, *tc_node;

		tc_node = ice_sched_get_tc_node(pi, i);
		if (!tc_node)
			continue;

		vsi_node = ice_sched_get_vsi_node(pi->hw, tc_node, vsi_handle);
		if (!vsi_node)
			continue;

		while (j < vsi_node->num_children) {
			if (vsi_node->children[j]->owner == owner) {
				ice_free_sched_node(pi, vsi_node->children[j]);

				/* reset the counter again since the num
				 * children will be updated after node removal
				 */
				j = 0;
			} else {
				j++;
			}
		}
		/* remove the VSI if it has no children */
		if (!vsi_node->num_children) {
			ice_free_sched_node(pi, vsi_node);
			vsi_ctx->sched.vsi_node[i] = NULL;

			/* clean up agg related vsi info if any */
			ice_sched_rm_agg_vsi_info(pi, vsi_handle);
		}
		if (owner == ICE_SCHED_NODE_OWNER_LAN)
			vsi_ctx->sched.max_lanq[i] = 0;
	}
	status = 0;

exit_sched_rm_vsi_cfg:
	mutex_unlock(&pi->sched_lock);
	return status;
}

/**
 * ice_rm_vsi_lan_cfg - remove VSI and its LAN children nodes
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 *
 * This function clears the VSI and its LAN children nodes from scheduler tree
 * for all TCs.
 */
enum ice_status ice_rm_vsi_lan_cfg(struct ice_port_info *pi, u16 vsi_handle)
{
	return ice_sched_rm_vsi_cfg(pi, vsi_handle, ICE_SCHED_NODE_OWNER_LAN);
}
