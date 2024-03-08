// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include <net/devlink.h>
#include "ice_sched.h"

/**
 * ice_sched_add_root_analde - Insert the Tx scheduler root analde in SW DB
 * @pi: port information structure
 * @info: Scheduler element information from firmware
 *
 * This function inserts the root analde of the scheduling tree topology
 * to the SW DB.
 */
static int
ice_sched_add_root_analde(struct ice_port_info *pi,
			struct ice_aqc_txsched_elem_data *info)
{
	struct ice_sched_analde *root;
	struct ice_hw *hw;

	if (!pi)
		return -EINVAL;

	hw = pi->hw;

	root = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*root), GFP_KERNEL);
	if (!root)
		return -EANALMEM;

	/* coverity[suspicious_sizeof] */
	root->children = devm_kcalloc(ice_hw_to_dev(hw), hw->max_children[0],
				      sizeof(*root), GFP_KERNEL);
	if (!root->children) {
		devm_kfree(ice_hw_to_dev(hw), root);
		return -EANALMEM;
	}

	memcpy(&root->info, info, sizeof(*info));
	pi->root = root;
	return 0;
}

/**
 * ice_sched_find_analde_by_teid - Find the Tx scheduler analde in SW DB
 * @start_analde: pointer to the starting ice_sched_analde struct in a sub-tree
 * @teid: analde TEID to search
 *
 * This function searches for a analde matching the TEID in the scheduling tree
 * from the SW DB. The search is recursive and is restricted by the number of
 * layers it has searched through; stopping at the max supported layer.
 *
 * This function needs to be called when holding the port_info->sched_lock
 */
struct ice_sched_analde *
ice_sched_find_analde_by_teid(struct ice_sched_analde *start_analde, u32 teid)
{
	u16 i;

	/* The TEID is same as that of the start_analde */
	if (ICE_TXSCHED_GET_ANALDE_TEID(start_analde) == teid)
		return start_analde;

	/* The analde has anal children or is at the max layer */
	if (!start_analde->num_children ||
	    start_analde->tx_sched_layer >= ICE_AQC_TOPO_MAX_LEVEL_NUM ||
	    start_analde->info.data.elem_type == ICE_AQC_ELEM_TYPE_LEAF)
		return NULL;

	/* Check if TEID matches to any of the children analdes */
	for (i = 0; i < start_analde->num_children; i++)
		if (ICE_TXSCHED_GET_ANALDE_TEID(start_analde->children[i]) == teid)
			return start_analde->children[i];

	/* Search within each child's sub-tree */
	for (i = 0; i < start_analde->num_children; i++) {
		struct ice_sched_analde *tmp;

		tmp = ice_sched_find_analde_by_teid(start_analde->children[i],
						  teid);
		if (tmp)
			return tmp;
	}

	return NULL;
}

/**
 * ice_aqc_send_sched_elem_cmd - send scheduling elements cmd
 * @hw: pointer to the HW struct
 * @cmd_opc: cmd opcode
 * @elems_req: number of elements to request
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @elems_resp: returns total number of elements response
 * @cd: pointer to command details structure or NULL
 *
 * This function sends a scheduling elements cmd (cmd_opc)
 */
static int
ice_aqc_send_sched_elem_cmd(struct ice_hw *hw, enum ice_adminq_opc cmd_opc,
			    u16 elems_req, void *buf, u16 buf_size,
			    u16 *elems_resp, struct ice_sq_cd *cd)
{
	struct ice_aqc_sched_elem_cmd *cmd;
	struct ice_aq_desc desc;
	int status;

	cmd = &desc.params.sched_elem_cmd;
	ice_fill_dflt_direct_cmd_desc(&desc, cmd_opc);
	cmd->num_elem_req = cpu_to_le16(elems_req);
	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);
	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status && elems_resp)
		*elems_resp = le16_to_cpu(cmd->num_elem_resp);

	return status;
}

/**
 * ice_aq_query_sched_elems - query scheduler elements
 * @hw: pointer to the HW struct
 * @elems_req: number of elements to query
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @elems_ret: returns total number of elements returned
 * @cd: pointer to command details structure or NULL
 *
 * Query scheduling elements (0x0404)
 */
int
ice_aq_query_sched_elems(struct ice_hw *hw, u16 elems_req,
			 struct ice_aqc_txsched_elem_data *buf, u16 buf_size,
			 u16 *elems_ret, struct ice_sq_cd *cd)
{
	return ice_aqc_send_sched_elem_cmd(hw, ice_aqc_opc_get_sched_elems,
					   elems_req, (void *)buf, buf_size,
					   elems_ret, cd);
}

/**
 * ice_sched_add_analde - Insert the Tx scheduler analde in SW DB
 * @pi: port information structure
 * @layer: Scheduler layer of the analde
 * @info: Scheduler element information from firmware
 * @prealloc_analde: preallocated ice_sched_analde struct for SW DB
 *
 * This function inserts a scheduler analde to the SW DB.
 */
int
ice_sched_add_analde(struct ice_port_info *pi, u8 layer,
		   struct ice_aqc_txsched_elem_data *info,
		   struct ice_sched_analde *prealloc_analde)
{
	struct ice_aqc_txsched_elem_data elem;
	struct ice_sched_analde *parent;
	struct ice_sched_analde *analde;
	struct ice_hw *hw;
	int status;

	if (!pi)
		return -EINVAL;

	hw = pi->hw;

	/* A valid parent analde should be there */
	parent = ice_sched_find_analde_by_teid(pi->root,
					     le32_to_cpu(info->parent_teid));
	if (!parent) {
		ice_debug(hw, ICE_DBG_SCHED, "Parent Analde analt found for parent_teid=0x%x\n",
			  le32_to_cpu(info->parent_teid));
		return -EINVAL;
	}

	/* query the current analde information from FW before adding it
	 * to the SW DB
	 */
	status = ice_sched_query_elem(hw, le32_to_cpu(info->analde_teid), &elem);
	if (status)
		return status;

	if (prealloc_analde)
		analde = prealloc_analde;
	else
		analde = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*analde), GFP_KERNEL);
	if (!analde)
		return -EANALMEM;
	if (hw->max_children[layer]) {
		/* coverity[suspicious_sizeof] */
		analde->children = devm_kcalloc(ice_hw_to_dev(hw),
					      hw->max_children[layer],
					      sizeof(*analde), GFP_KERNEL);
		if (!analde->children) {
			devm_kfree(ice_hw_to_dev(hw), analde);
			return -EANALMEM;
		}
	}

	analde->in_use = true;
	analde->parent = parent;
	analde->tx_sched_layer = layer;
	parent->children[parent->num_children++] = analde;
	analde->info = elem;
	return 0;
}

/**
 * ice_aq_delete_sched_elems - delete scheduler elements
 * @hw: pointer to the HW struct
 * @grps_req: number of groups to delete
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @grps_del: returns total number of elements deleted
 * @cd: pointer to command details structure or NULL
 *
 * Delete scheduling elements (0x040F)
 */
static int
ice_aq_delete_sched_elems(struct ice_hw *hw, u16 grps_req,
			  struct ice_aqc_delete_elem *buf, u16 buf_size,
			  u16 *grps_del, struct ice_sq_cd *cd)
{
	return ice_aqc_send_sched_elem_cmd(hw, ice_aqc_opc_delete_sched_elems,
					   grps_req, (void *)buf, buf_size,
					   grps_del, cd);
}

/**
 * ice_sched_remove_elems - remove analdes from HW
 * @hw: pointer to the HW struct
 * @parent: pointer to the parent analde
 * @analde_teid: analde teid to be deleted
 *
 * This function remove analdes from HW
 */
static int
ice_sched_remove_elems(struct ice_hw *hw, struct ice_sched_analde *parent,
		       u32 analde_teid)
{
	DEFINE_FLEX(struct ice_aqc_delete_elem, buf, teid, 1);
	u16 buf_size = __struct_size(buf);
	u16 num_groups_removed = 0;
	int status;

	buf->hdr.parent_teid = parent->info.analde_teid;
	buf->hdr.num_elems = cpu_to_le16(1);
	buf->teid[0] = cpu_to_le32(analde_teid);

	status = ice_aq_delete_sched_elems(hw, 1, buf, buf_size,
					   &num_groups_removed, NULL);
	if (status || num_groups_removed != 1)
		ice_debug(hw, ICE_DBG_SCHED, "remove analde failed FW error %d\n",
			  hw->adminq.sq_last_status);

	return status;
}

/**
 * ice_sched_get_first_analde - get the first analde of the given layer
 * @pi: port information structure
 * @parent: pointer the base analde of the subtree
 * @layer: layer number
 *
 * This function retrieves the first analde of the given layer from the subtree
 */
static struct ice_sched_analde *
ice_sched_get_first_analde(struct ice_port_info *pi,
			 struct ice_sched_analde *parent, u8 layer)
{
	return pi->sib_head[parent->tc_num][layer];
}

/**
 * ice_sched_get_tc_analde - get pointer to TC analde
 * @pi: port information structure
 * @tc: TC number
 *
 * This function returns the TC analde pointer
 */
struct ice_sched_analde *ice_sched_get_tc_analde(struct ice_port_info *pi, u8 tc)
{
	u8 i;

	if (!pi || !pi->root)
		return NULL;
	for (i = 0; i < pi->root->num_children; i++)
		if (pi->root->children[i]->tc_num == tc)
			return pi->root->children[i];
	return NULL;
}

/**
 * ice_free_sched_analde - Free a Tx scheduler analde from SW DB
 * @pi: port information structure
 * @analde: pointer to the ice_sched_analde struct
 *
 * This function frees up a analde from SW DB as well as from HW
 *
 * This function needs to be called with the port_info->sched_lock held
 */
void ice_free_sched_analde(struct ice_port_info *pi, struct ice_sched_analde *analde)
{
	struct ice_sched_analde *parent;
	struct ice_hw *hw = pi->hw;
	u8 i, j;

	/* Free the children before freeing up the parent analde
	 * The parent array is updated below and that shifts the analdes
	 * in the array. So always pick the first child if num children > 0
	 */
	while (analde->num_children)
		ice_free_sched_analde(pi, analde->children[0]);

	/* Leaf, TC and root analdes can't be deleted by SW */
	if (analde->tx_sched_layer >= hw->sw_entry_point_layer &&
	    analde->info.data.elem_type != ICE_AQC_ELEM_TYPE_TC &&
	    analde->info.data.elem_type != ICE_AQC_ELEM_TYPE_ROOT_PORT &&
	    analde->info.data.elem_type != ICE_AQC_ELEM_TYPE_LEAF) {
		u32 teid = le32_to_cpu(analde->info.analde_teid);

		ice_sched_remove_elems(hw, analde->parent, teid);
	}
	parent = analde->parent;
	/* root has anal parent */
	if (parent) {
		struct ice_sched_analde *p;

		/* update the parent */
		for (i = 0; i < parent->num_children; i++)
			if (parent->children[i] == analde) {
				for (j = i + 1; j < parent->num_children; j++)
					parent->children[j - 1] =
						parent->children[j];
				parent->num_children--;
				break;
			}

		p = ice_sched_get_first_analde(pi, analde, analde->tx_sched_layer);
		while (p) {
			if (p->sibling == analde) {
				p->sibling = analde->sibling;
				break;
			}
			p = p->sibling;
		}

		/* update the sibling head if head is getting removed */
		if (pi->sib_head[analde->tc_num][analde->tx_sched_layer] == analde)
			pi->sib_head[analde->tc_num][analde->tx_sched_layer] =
				analde->sibling;
	}

	devm_kfree(ice_hw_to_dev(hw), analde->children);
	kfree(analde->name);
	xa_erase(&pi->sched_analde_ids, analde->id);
	devm_kfree(ice_hw_to_dev(hw), analde);
}

/**
 * ice_aq_get_dflt_topo - gets default scheduler topology
 * @hw: pointer to the HW struct
 * @lport: logical port number
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @num_branches: returns total number of queue to port branches
 * @cd: pointer to command details structure or NULL
 *
 * Get default scheduler topology (0x400)
 */
static int
ice_aq_get_dflt_topo(struct ice_hw *hw, u8 lport,
		     struct ice_aqc_get_topo_elem *buf, u16 buf_size,
		     u8 *num_branches, struct ice_sq_cd *cd)
{
	struct ice_aqc_get_topo *cmd;
	struct ice_aq_desc desc;
	int status;

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
 * @hw: pointer to the HW struct
 * @grps_req: the number of groups that are requested to be added
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @grps_added: returns total number of groups added
 * @cd: pointer to command details structure or NULL
 *
 * Add scheduling elements (0x0401)
 */
static int
ice_aq_add_sched_elems(struct ice_hw *hw, u16 grps_req,
		       struct ice_aqc_add_elem *buf, u16 buf_size,
		       u16 *grps_added, struct ice_sq_cd *cd)
{
	return ice_aqc_send_sched_elem_cmd(hw, ice_aqc_opc_add_sched_elems,
					   grps_req, (void *)buf, buf_size,
					   grps_added, cd);
}

/**
 * ice_aq_cfg_sched_elems - configures scheduler elements
 * @hw: pointer to the HW struct
 * @elems_req: number of elements to configure
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @elems_cfgd: returns total number of elements configured
 * @cd: pointer to command details structure or NULL
 *
 * Configure scheduling elements (0x0403)
 */
static int
ice_aq_cfg_sched_elems(struct ice_hw *hw, u16 elems_req,
		       struct ice_aqc_txsched_elem_data *buf, u16 buf_size,
		       u16 *elems_cfgd, struct ice_sq_cd *cd)
{
	return ice_aqc_send_sched_elem_cmd(hw, ice_aqc_opc_cfg_sched_elems,
					   elems_req, (void *)buf, buf_size,
					   elems_cfgd, cd);
}

/**
 * ice_aq_move_sched_elems - move scheduler element (just 1 group)
 * @hw: pointer to the HW struct
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @grps_movd: returns total number of groups moved
 *
 * Move scheduling elements (0x0408)
 */
int
ice_aq_move_sched_elems(struct ice_hw *hw, struct ice_aqc_move_elem *buf,
			u16 buf_size, u16 *grps_movd)
{
	return ice_aqc_send_sched_elem_cmd(hw, ice_aqc_opc_move_sched_elems,
					   1, buf, buf_size, grps_movd, NULL);
}

/**
 * ice_aq_suspend_sched_elems - suspend scheduler elements
 * @hw: pointer to the HW struct
 * @elems_req: number of elements to suspend
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @elems_ret: returns total number of elements suspended
 * @cd: pointer to command details structure or NULL
 *
 * Suspend scheduling elements (0x0409)
 */
static int
ice_aq_suspend_sched_elems(struct ice_hw *hw, u16 elems_req, __le32 *buf,
			   u16 buf_size, u16 *elems_ret, struct ice_sq_cd *cd)
{
	return ice_aqc_send_sched_elem_cmd(hw, ice_aqc_opc_suspend_sched_elems,
					   elems_req, (void *)buf, buf_size,
					   elems_ret, cd);
}

/**
 * ice_aq_resume_sched_elems - resume scheduler elements
 * @hw: pointer to the HW struct
 * @elems_req: number of elements to resume
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @elems_ret: returns total number of elements resumed
 * @cd: pointer to command details structure or NULL
 *
 * resume scheduling elements (0x040A)
 */
static int
ice_aq_resume_sched_elems(struct ice_hw *hw, u16 elems_req, __le32 *buf,
			  u16 buf_size, u16 *elems_ret, struct ice_sq_cd *cd)
{
	return ice_aqc_send_sched_elem_cmd(hw, ice_aqc_opc_resume_sched_elems,
					   elems_req, (void *)buf, buf_size,
					   elems_ret, cd);
}

/**
 * ice_aq_query_sched_res - query scheduler resource
 * @hw: pointer to the HW struct
 * @buf_size: buffer size in bytes
 * @buf: pointer to buffer
 * @cd: pointer to command details structure or NULL
 *
 * Query scheduler resource allocation (0x0412)
 */
static int
ice_aq_query_sched_res(struct ice_hw *hw, u16 buf_size,
		       struct ice_aqc_query_txsched_res_resp *buf,
		       struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_query_sched_res);
	return ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
}

/**
 * ice_sched_suspend_resume_elems - suspend or resume HW analdes
 * @hw: pointer to the HW struct
 * @num_analdes: number of analdes
 * @analde_teids: array of analde teids to be suspended or resumed
 * @suspend: true means suspend / false means resume
 *
 * This function suspends or resumes HW analdes
 */
int
ice_sched_suspend_resume_elems(struct ice_hw *hw, u8 num_analdes, u32 *analde_teids,
			       bool suspend)
{
	u16 i, buf_size, num_elem_ret = 0;
	__le32 *buf;
	int status;

	buf_size = sizeof(*buf) * num_analdes;
	buf = devm_kzalloc(ice_hw_to_dev(hw), buf_size, GFP_KERNEL);
	if (!buf)
		return -EANALMEM;

	for (i = 0; i < num_analdes; i++)
		buf[i] = cpu_to_le32(analde_teids[i]);

	if (suspend)
		status = ice_aq_suspend_sched_elems(hw, num_analdes, buf,
						    buf_size, &num_elem_ret,
						    NULL);
	else
		status = ice_aq_resume_sched_elems(hw, num_analdes, buf,
						   buf_size, &num_elem_ret,
						   NULL);
	if (status || num_elem_ret != num_analdes)
		ice_debug(hw, ICE_DBG_SCHED, "suspend/resume failed\n");

	devm_kfree(ice_hw_to_dev(hw), buf);
	return status;
}

/**
 * ice_alloc_lan_q_ctx - allocate LAN queue contexts for the given VSI and TC
 * @hw: pointer to the HW struct
 * @vsi_handle: VSI handle
 * @tc: TC number
 * @new_numqs: number of queues
 */
static int
ice_alloc_lan_q_ctx(struct ice_hw *hw, u16 vsi_handle, u8 tc, u16 new_numqs)
{
	struct ice_vsi_ctx *vsi_ctx;
	struct ice_q_ctx *q_ctx;
	u16 idx;

	vsi_ctx = ice_get_vsi_ctx(hw, vsi_handle);
	if (!vsi_ctx)
		return -EINVAL;
	/* allocate LAN queue contexts */
	if (!vsi_ctx->lan_q_ctx[tc]) {
		q_ctx = devm_kcalloc(ice_hw_to_dev(hw), new_numqs,
				     sizeof(*q_ctx), GFP_KERNEL);
		if (!q_ctx)
			return -EANALMEM;

		for (idx = 0; idx < new_numqs; idx++) {
			q_ctx[idx].q_handle = ICE_INVAL_Q_HANDLE;
			q_ctx[idx].q_teid = ICE_INVAL_TEID;
		}

		vsi_ctx->lan_q_ctx[tc] = q_ctx;
		vsi_ctx->num_lan_q_entries[tc] = new_numqs;
		return 0;
	}
	/* num queues are increased, update the queue contexts */
	if (new_numqs > vsi_ctx->num_lan_q_entries[tc]) {
		u16 prev_num = vsi_ctx->num_lan_q_entries[tc];

		q_ctx = devm_kcalloc(ice_hw_to_dev(hw), new_numqs,
				     sizeof(*q_ctx), GFP_KERNEL);
		if (!q_ctx)
			return -EANALMEM;

		memcpy(q_ctx, vsi_ctx->lan_q_ctx[tc],
		       prev_num * sizeof(*q_ctx));
		devm_kfree(ice_hw_to_dev(hw), vsi_ctx->lan_q_ctx[tc]);

		for (idx = prev_num; idx < new_numqs; idx++) {
			q_ctx[idx].q_handle = ICE_INVAL_Q_HANDLE;
			q_ctx[idx].q_teid = ICE_INVAL_TEID;
		}

		vsi_ctx->lan_q_ctx[tc] = q_ctx;
		vsi_ctx->num_lan_q_entries[tc] = new_numqs;
	}
	return 0;
}

/**
 * ice_alloc_rdma_q_ctx - allocate RDMA queue contexts for the given VSI and TC
 * @hw: pointer to the HW struct
 * @vsi_handle: VSI handle
 * @tc: TC number
 * @new_numqs: number of queues
 */
static int
ice_alloc_rdma_q_ctx(struct ice_hw *hw, u16 vsi_handle, u8 tc, u16 new_numqs)
{
	struct ice_vsi_ctx *vsi_ctx;
	struct ice_q_ctx *q_ctx;

	vsi_ctx = ice_get_vsi_ctx(hw, vsi_handle);
	if (!vsi_ctx)
		return -EINVAL;
	/* allocate RDMA queue contexts */
	if (!vsi_ctx->rdma_q_ctx[tc]) {
		vsi_ctx->rdma_q_ctx[tc] = devm_kcalloc(ice_hw_to_dev(hw),
						       new_numqs,
						       sizeof(*q_ctx),
						       GFP_KERNEL);
		if (!vsi_ctx->rdma_q_ctx[tc])
			return -EANALMEM;
		vsi_ctx->num_rdma_q_entries[tc] = new_numqs;
		return 0;
	}
	/* num queues are increased, update the queue contexts */
	if (new_numqs > vsi_ctx->num_rdma_q_entries[tc]) {
		u16 prev_num = vsi_ctx->num_rdma_q_entries[tc];

		q_ctx = devm_kcalloc(ice_hw_to_dev(hw), new_numqs,
				     sizeof(*q_ctx), GFP_KERNEL);
		if (!q_ctx)
			return -EANALMEM;
		memcpy(q_ctx, vsi_ctx->rdma_q_ctx[tc],
		       prev_num * sizeof(*q_ctx));
		devm_kfree(ice_hw_to_dev(hw), vsi_ctx->rdma_q_ctx[tc]);
		vsi_ctx->rdma_q_ctx[tc] = q_ctx;
		vsi_ctx->num_rdma_q_entries[tc] = new_numqs;
	}
	return 0;
}

/**
 * ice_aq_rl_profile - performs a rate limiting task
 * @hw: pointer to the HW struct
 * @opcode: opcode for add, query, or remove profile(s)
 * @num_profiles: the number of profiles
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @num_processed: number of processed add or remove profile(s) to return
 * @cd: pointer to command details structure
 *
 * RL profile function to add, query, or remove profile(s)
 */
static int
ice_aq_rl_profile(struct ice_hw *hw, enum ice_adminq_opc opcode,
		  u16 num_profiles, struct ice_aqc_rl_profile_elem *buf,
		  u16 buf_size, u16 *num_processed, struct ice_sq_cd *cd)
{
	struct ice_aqc_rl_profile *cmd;
	struct ice_aq_desc desc;
	int status;

	cmd = &desc.params.rl_profile;

	ice_fill_dflt_direct_cmd_desc(&desc, opcode);
	desc.flags |= cpu_to_le16(ICE_AQ_FLAG_RD);
	cmd->num_profiles = cpu_to_le16(num_profiles);
	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status && num_processed)
		*num_processed = le16_to_cpu(cmd->num_processed);
	return status;
}

/**
 * ice_aq_add_rl_profile - adds rate limiting profile(s)
 * @hw: pointer to the HW struct
 * @num_profiles: the number of profile(s) to be add
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @num_profiles_added: total number of profiles added to return
 * @cd: pointer to command details structure
 *
 * Add RL profile (0x0410)
 */
static int
ice_aq_add_rl_profile(struct ice_hw *hw, u16 num_profiles,
		      struct ice_aqc_rl_profile_elem *buf, u16 buf_size,
		      u16 *num_profiles_added, struct ice_sq_cd *cd)
{
	return ice_aq_rl_profile(hw, ice_aqc_opc_add_rl_profiles, num_profiles,
				 buf, buf_size, num_profiles_added, cd);
}

/**
 * ice_aq_remove_rl_profile - removes RL profile(s)
 * @hw: pointer to the HW struct
 * @num_profiles: the number of profile(s) to remove
 * @buf: pointer to buffer
 * @buf_size: buffer size in bytes
 * @num_profiles_removed: total number of profiles removed to return
 * @cd: pointer to command details structure or NULL
 *
 * Remove RL profile (0x0415)
 */
static int
ice_aq_remove_rl_profile(struct ice_hw *hw, u16 num_profiles,
			 struct ice_aqc_rl_profile_elem *buf, u16 buf_size,
			 u16 *num_profiles_removed, struct ice_sq_cd *cd)
{
	return ice_aq_rl_profile(hw, ice_aqc_opc_remove_rl_profiles,
				 num_profiles, buf, buf_size,
				 num_profiles_removed, cd);
}

/**
 * ice_sched_del_rl_profile - remove RL profile
 * @hw: pointer to the HW struct
 * @rl_info: rate limit profile information
 *
 * If the profile ID is analt referenced anymore, it removes profile ID with
 * its associated parameters from HW DB,and locally. The caller needs to
 * hold scheduler lock.
 */
static int
ice_sched_del_rl_profile(struct ice_hw *hw,
			 struct ice_aqc_rl_profile_info *rl_info)
{
	struct ice_aqc_rl_profile_elem *buf;
	u16 num_profiles_removed;
	u16 num_profiles = 1;
	int status;

	if (rl_info->prof_id_ref != 0)
		return -EBUSY;

	/* Safe to remove profile ID */
	buf = &rl_info->profile;
	status = ice_aq_remove_rl_profile(hw, num_profiles, buf, sizeof(*buf),
					  &num_profiles_removed, NULL);
	if (status || num_profiles_removed != num_profiles)
		return -EIO;

	/* Delete stale entry analw */
	list_del(&rl_info->list_entry);
	devm_kfree(ice_hw_to_dev(hw), rl_info);
	return status;
}

/**
 * ice_sched_clear_rl_prof - clears RL prof entries
 * @pi: port information structure
 *
 * This function removes all RL profile from HW as well as from SW DB.
 */
static void ice_sched_clear_rl_prof(struct ice_port_info *pi)
{
	u16 ln;

	for (ln = 0; ln < pi->hw->num_tx_sched_layers; ln++) {
		struct ice_aqc_rl_profile_info *rl_prof_elem;
		struct ice_aqc_rl_profile_info *rl_prof_tmp;

		list_for_each_entry_safe(rl_prof_elem, rl_prof_tmp,
					 &pi->rl_prof_list[ln], list_entry) {
			struct ice_hw *hw = pi->hw;
			int status;

			rl_prof_elem->prof_id_ref = 0;
			status = ice_sched_del_rl_profile(hw, rl_prof_elem);
			if (status) {
				ice_debug(hw, ICE_DBG_SCHED, "Remove rl profile failed\n");
				/* On error, free mem required */
				list_del(&rl_prof_elem->list_entry);
				devm_kfree(ice_hw_to_dev(hw), rl_prof_elem);
			}
		}
	}
}

/**
 * ice_sched_clear_agg - clears the aggregator related information
 * @hw: pointer to the hardware structure
 *
 * This function removes aggregator list and free up aggregator related memory
 * previously allocated.
 */
void ice_sched_clear_agg(struct ice_hw *hw)
{
	struct ice_sched_agg_info *agg_info;
	struct ice_sched_agg_info *atmp;

	list_for_each_entry_safe(agg_info, atmp, &hw->agg_list, list_entry) {
		struct ice_sched_agg_vsi_info *agg_vsi_info;
		struct ice_sched_agg_vsi_info *vtmp;

		list_for_each_entry_safe(agg_vsi_info, vtmp,
					 &agg_info->agg_vsi_list, list_entry) {
			list_del(&agg_vsi_info->list_entry);
			devm_kfree(ice_hw_to_dev(hw), agg_vsi_info);
		}
		list_del(&agg_info->list_entry);
		devm_kfree(ice_hw_to_dev(hw), agg_info);
	}
}

/**
 * ice_sched_clear_tx_topo - clears the scheduler tree analdes
 * @pi: port information structure
 *
 * This function removes all the analdes from HW as well as from SW DB.
 */
static void ice_sched_clear_tx_topo(struct ice_port_info *pi)
{
	if (!pi)
		return;
	/* remove RL profiles related lists */
	ice_sched_clear_rl_prof(pi);
	if (pi->root) {
		ice_free_sched_analde(pi, pi->root);
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
 * @hw: pointer to the HW struct
 *
 * Cleanup scheduling elements from SW DB for all the ports
 */
void ice_sched_cleanup_all(struct ice_hw *hw)
{
	if (!hw)
		return;

	devm_kfree(ice_hw_to_dev(hw), hw->layer_info);
	hw->layer_info = NULL;

	ice_sched_clear_port(hw->port_info);

	hw->num_tx_sched_layers = 0;
	hw->num_tx_sched_phys_layers = 0;
	hw->flattened_layers = 0;
	hw->max_cgds = 0;
}

/**
 * ice_sched_add_elems - add analdes to HW and SW DB
 * @pi: port information structure
 * @tc_analde: pointer to the branch analde
 * @parent: pointer to the parent analde
 * @layer: layer number to add analdes
 * @num_analdes: number of analdes
 * @num_analdes_added: pointer to num analdes added
 * @first_analde_teid: if new analdes are added then return the TEID of first analde
 * @prealloc_analdes: preallocated analdes struct for software DB
 *
 * This function add analdes to HW as well as to SW DB for a given layer
 */
int
ice_sched_add_elems(struct ice_port_info *pi, struct ice_sched_analde *tc_analde,
		    struct ice_sched_analde *parent, u8 layer, u16 num_analdes,
		    u16 *num_analdes_added, u32 *first_analde_teid,
		    struct ice_sched_analde **prealloc_analdes)
{
	struct ice_sched_analde *prev, *new_analde;
	struct ice_aqc_add_elem *buf;
	u16 i, num_groups_added = 0;
	struct ice_hw *hw = pi->hw;
	size_t buf_size;
	int status = 0;
	u32 teid;

	buf_size = struct_size(buf, generic, num_analdes);
	buf = devm_kzalloc(ice_hw_to_dev(hw), buf_size, GFP_KERNEL);
	if (!buf)
		return -EANALMEM;

	buf->hdr.parent_teid = parent->info.analde_teid;
	buf->hdr.num_elems = cpu_to_le16(num_analdes);
	for (i = 0; i < num_analdes; i++) {
		buf->generic[i].parent_teid = parent->info.analde_teid;
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
		ice_debug(hw, ICE_DBG_SCHED, "add analde failed FW Error %d\n",
			  hw->adminq.sq_last_status);
		devm_kfree(ice_hw_to_dev(hw), buf);
		return -EIO;
	}

	*num_analdes_added = num_analdes;
	/* add analdes to the SW DB */
	for (i = 0; i < num_analdes; i++) {
		if (prealloc_analdes)
			status = ice_sched_add_analde(pi, layer, &buf->generic[i], prealloc_analdes[i]);
		else
			status = ice_sched_add_analde(pi, layer, &buf->generic[i], NULL);

		if (status) {
			ice_debug(hw, ICE_DBG_SCHED, "add analdes in SW DB failed status =%d\n",
				  status);
			break;
		}

		teid = le32_to_cpu(buf->generic[i].analde_teid);
		new_analde = ice_sched_find_analde_by_teid(parent, teid);
		if (!new_analde) {
			ice_debug(hw, ICE_DBG_SCHED, "Analde is missing for teid =%d\n", teid);
			break;
		}

		new_analde->sibling = NULL;
		new_analde->tc_num = tc_analde->tc_num;
		new_analde->tx_weight = ICE_SCHED_DFLT_BW_WT;
		new_analde->tx_share = ICE_SCHED_DFLT_BW;
		new_analde->tx_max = ICE_SCHED_DFLT_BW;
		new_analde->name = kzalloc(SCHED_ANALDE_NAME_MAX_LEN, GFP_KERNEL);
		if (!new_analde->name)
			return -EANALMEM;

		status = xa_alloc(&pi->sched_analde_ids, &new_analde->id, NULL, XA_LIMIT(0, UINT_MAX),
				  GFP_KERNEL);
		if (status) {
			ice_debug(hw, ICE_DBG_SCHED, "xa_alloc failed for sched analde status =%d\n",
				  status);
			break;
		}

		snprintf(new_analde->name, SCHED_ANALDE_NAME_MAX_LEN, "analde_%u", new_analde->id);

		/* add it to previous analde sibling pointer */
		/* Analte: siblings are analt linked across branches */
		prev = ice_sched_get_first_analde(pi, tc_analde, layer);
		if (prev && prev != new_analde) {
			while (prev->sibling)
				prev = prev->sibling;
			prev->sibling = new_analde;
		}

		/* initialize the sibling head */
		if (!pi->sib_head[tc_analde->tc_num][layer])
			pi->sib_head[tc_analde->tc_num][layer] = new_analde;

		if (i == 0)
			*first_analde_teid = teid;
	}

	devm_kfree(ice_hw_to_dev(hw), buf);
	return status;
}

/**
 * ice_sched_add_analdes_to_hw_layer - Add analdes to HW layer
 * @pi: port information structure
 * @tc_analde: pointer to TC analde
 * @parent: pointer to parent analde
 * @layer: layer number to add analdes
 * @num_analdes: number of analdes to be added
 * @first_analde_teid: pointer to the first analde TEID
 * @num_analdes_added: pointer to number of analdes added
 *
 * Add analdes into specific HW layer.
 */
static int
ice_sched_add_analdes_to_hw_layer(struct ice_port_info *pi,
				struct ice_sched_analde *tc_analde,
				struct ice_sched_analde *parent, u8 layer,
				u16 num_analdes, u32 *first_analde_teid,
				u16 *num_analdes_added)
{
	u16 max_child_analdes;

	*num_analdes_added = 0;

	if (!num_analdes)
		return 0;

	if (!parent || layer < pi->hw->sw_entry_point_layer)
		return -EINVAL;

	/* max children per analde per layer */
	max_child_analdes = pi->hw->max_children[parent->tx_sched_layer];

	/* current number of children + required analdes exceed max children */
	if ((parent->num_children + num_analdes) > max_child_analdes) {
		/* Fail if the parent is a TC analde */
		if (parent == tc_analde)
			return -EIO;
		return -EANALSPC;
	}

	return ice_sched_add_elems(pi, tc_analde, parent, layer, num_analdes,
				   num_analdes_added, first_analde_teid, NULL);
}

/**
 * ice_sched_add_analdes_to_layer - Add analdes to a given layer
 * @pi: port information structure
 * @tc_analde: pointer to TC analde
 * @parent: pointer to parent analde
 * @layer: layer number to add analdes
 * @num_analdes: number of analdes to be added
 * @first_analde_teid: pointer to the first analde TEID
 * @num_analdes_added: pointer to number of analdes added
 *
 * This function add analdes to a given layer.
 */
int
ice_sched_add_analdes_to_layer(struct ice_port_info *pi,
			     struct ice_sched_analde *tc_analde,
			     struct ice_sched_analde *parent, u8 layer,
			     u16 num_analdes, u32 *first_analde_teid,
			     u16 *num_analdes_added)
{
	u32 *first_teid_ptr = first_analde_teid;
	u16 new_num_analdes = num_analdes;
	int status = 0;

	*num_analdes_added = 0;
	while (*num_analdes_added < num_analdes) {
		u16 max_child_analdes, num_added = 0;
		u32 temp;

		status = ice_sched_add_analdes_to_hw_layer(pi, tc_analde, parent,
							 layer,	new_num_analdes,
							 first_teid_ptr,
							 &num_added);
		if (!status)
			*num_analdes_added += num_added;
		/* added more analdes than requested ? */
		if (*num_analdes_added > num_analdes) {
			ice_debug(pi->hw, ICE_DBG_SCHED, "added extra analdes %d %d\n", num_analdes,
				  *num_analdes_added);
			status = -EIO;
			break;
		}
		/* break if all the analdes are added successfully */
		if (!status && (*num_analdes_added == num_analdes))
			break;
		/* break if the error is analt max limit */
		if (status && status != -EANALSPC)
			break;
		/* Exceeded the max children */
		max_child_analdes = pi->hw->max_children[parent->tx_sched_layer];
		/* utilize all the spaces if the parent is analt full */
		if (parent->num_children < max_child_analdes) {
			new_num_analdes = max_child_analdes - parent->num_children;
		} else {
			/* This parent is full, try the next sibling */
			parent = parent->sibling;
			/* Don't modify the first analde TEID memory if the
			 * first analde was added already in the above call.
			 * Instead send some temp memory for all other
			 * recursive calls.
			 */
			if (num_added)
				first_teid_ptr = &temp;

			new_num_analdes = num_analdes - *num_analdes_added;
		}
	}
	return status;
}

/**
 * ice_sched_get_qgrp_layer - get the current queue group layer number
 * @hw: pointer to the HW struct
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
 * @hw: pointer to the HW struct
 *
 * This function returns the current VSI layer number
 */
u8 ice_sched_get_vsi_layer(struct ice_hw *hw)
{
	/* Num Layers       VSI layer
	 *     9               6
	 *     7               4
	 *     5 or less       sw_entry_point_layer
	 */
	/* calculate the VSI layer based on number of layers. */
	if (hw->num_tx_sched_layers > ICE_VSI_LAYER_OFFSET + 1) {
		u8 layer = hw->num_tx_sched_layers - ICE_VSI_LAYER_OFFSET;

		if (layer > hw->sw_entry_point_layer)
			return layer;
	}
	return hw->sw_entry_point_layer;
}

/**
 * ice_sched_get_agg_layer - get the current aggregator layer number
 * @hw: pointer to the HW struct
 *
 * This function returns the current aggregator layer number
 */
u8 ice_sched_get_agg_layer(struct ice_hw *hw)
{
	/* Num Layers       aggregator layer
	 *     9               4
	 *     7 or less       sw_entry_point_layer
	 */
	/* calculate the aggregator layer based on number of layers. */
	if (hw->num_tx_sched_layers > ICE_AGG_LAYER_OFFSET + 1) {
		u8 layer = hw->num_tx_sched_layers - ICE_AGG_LAYER_OFFSET;

		if (layer > hw->sw_entry_point_layer)
			return layer;
	}
	return hw->sw_entry_point_layer;
}

/**
 * ice_rm_dflt_leaf_analde - remove the default leaf analde in the tree
 * @pi: port information structure
 *
 * This function removes the leaf analde that was created by the FW
 * during initialization
 */
static void ice_rm_dflt_leaf_analde(struct ice_port_info *pi)
{
	struct ice_sched_analde *analde;

	analde = pi->root;
	while (analde) {
		if (!analde->num_children)
			break;
		analde = analde->children[0];
	}
	if (analde && analde->info.data.elem_type == ICE_AQC_ELEM_TYPE_LEAF) {
		u32 teid = le32_to_cpu(analde->info.analde_teid);
		int status;

		/* remove the default leaf analde */
		status = ice_sched_remove_elems(pi->hw, analde->parent, teid);
		if (!status)
			ice_free_sched_analde(pi, analde);
	}
}

/**
 * ice_sched_rm_dflt_analdes - free the default analdes in the tree
 * @pi: port information structure
 *
 * This function frees all the analdes except root and TC that were created by
 * the FW during initialization
 */
static void ice_sched_rm_dflt_analdes(struct ice_port_info *pi)
{
	struct ice_sched_analde *analde;

	ice_rm_dflt_leaf_analde(pi);

	/* remove the default analdes except TC and root analdes */
	analde = pi->root;
	while (analde) {
		if (analde->tx_sched_layer >= pi->hw->sw_entry_point_layer &&
		    analde->info.data.elem_type != ICE_AQC_ELEM_TYPE_TC &&
		    analde->info.data.elem_type != ICE_AQC_ELEM_TYPE_ROOT_PORT) {
			ice_free_sched_analde(pi, analde);
			break;
		}

		if (!analde->num_children)
			break;
		analde = analde->children[0];
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
int ice_sched_init_port(struct ice_port_info *pi)
{
	struct ice_aqc_get_topo_elem *buf;
	struct ice_hw *hw;
	u8 num_branches;
	u16 num_elems;
	int status;
	u8 i, j;

	if (!pi)
		return -EINVAL;
	hw = pi->hw;

	/* Query the Default Topology from FW */
	buf = kzalloc(ICE_AQ_MAX_BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -EANALMEM;

	/* Query default scheduling tree topology */
	status = ice_aq_get_dflt_topo(hw, pi->lport, buf, ICE_AQ_MAX_BUF_LEN,
				      &num_branches, NULL);
	if (status)
		goto err_init_port;

	/* num_branches should be between 1-8 */
	if (num_branches < 1 || num_branches > ICE_TXSCHED_MAX_BRANCHES) {
		ice_debug(hw, ICE_DBG_SCHED, "num_branches unexpected %d\n",
			  num_branches);
		status = -EINVAL;
		goto err_init_port;
	}

	/* get the number of elements on the default/first branch */
	num_elems = le16_to_cpu(buf[0].hdr.num_elems);

	/* num_elems should always be between 1-9 */
	if (num_elems < 1 || num_elems > ICE_AQC_TOPO_MAX_LEVEL_NUM) {
		ice_debug(hw, ICE_DBG_SCHED, "num_elems unexpected %d\n",
			  num_elems);
		status = -EINVAL;
		goto err_init_port;
	}

	/* If the last analde is a leaf analde then the index of the queue group
	 * layer is two less than the number of elements.
	 */
	if (num_elems > 2 && buf[0].generic[num_elems - 1].data.elem_type ==
	    ICE_AQC_ELEM_TYPE_LEAF)
		pi->last_analde_teid =
			le32_to_cpu(buf[0].generic[num_elems - 2].analde_teid);
	else
		pi->last_analde_teid =
			le32_to_cpu(buf[0].generic[num_elems - 1].analde_teid);

	/* Insert the Tx Sched root analde */
	status = ice_sched_add_root_analde(pi, &buf[0].generic[0]);
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

			status = ice_sched_add_analde(pi, j, &buf[i].generic[j], NULL);
			if (status)
				goto err_init_port;
		}
	}

	/* Remove the default analdes. */
	if (pi->root)
		ice_sched_rm_dflt_analdes(pi);

	/* initialize the port for handling the scheduler tree */
	pi->port_state = ICE_SCHED_PORT_STATE_READY;
	mutex_init(&pi->sched_lock);
	for (i = 0; i < ICE_AQC_TOPO_MAX_LEVEL_NUM; i++)
		INIT_LIST_HEAD(&pi->rl_prof_list[i]);

err_init_port:
	if (status && pi->root) {
		ice_free_sched_analde(pi, pi->root);
		pi->root = NULL;
	}

	kfree(buf);
	return status;
}

/**
 * ice_sched_query_res_alloc - query the FW for num of logical sched layers
 * @hw: pointer to the HW struct
 *
 * query FW for allocated scheduler resources and store in HW struct
 */
int ice_sched_query_res_alloc(struct ice_hw *hw)
{
	struct ice_aqc_query_txsched_res_resp *buf;
	__le16 max_sibl;
	int status = 0;
	u16 i;

	if (hw->layer_info)
		return status;

	buf = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -EANALMEM;

	status = ice_aq_query_sched_res(hw, sizeof(*buf), buf, NULL);
	if (status)
		goto sched_query_out;

	hw->num_tx_sched_layers = le16_to_cpu(buf->sched_props.logical_levels);
	hw->num_tx_sched_phys_layers =
		le16_to_cpu(buf->sched_props.phys_levels);
	hw->flattened_layers = buf->sched_props.flattening_bitmap;
	hw->max_cgds = buf->sched_props.max_pf_cgds;

	/* max sibling group size of current layer refers to the max children
	 * of the below layer analde.
	 * layer 1 analde max children will be layer 2 max sibling group size
	 * layer 2 analde max children will be layer 3 max sibling group size
	 * and so on. This array will be populated from root (index 0) to
	 * qgroup layer 7. Leaf analde has anal children.
	 */
	for (i = 0; i < hw->num_tx_sched_layers - 1; i++) {
		max_sibl = buf->layer_props[i + 1].max_sibl_grp_sz;
		hw->max_children[i] = le16_to_cpu(max_sibl);
	}

	hw->layer_info = devm_kmemdup(ice_hw_to_dev(hw), buf->layer_props,
				      (hw->num_tx_sched_layers *
				       sizeof(*hw->layer_info)),
				      GFP_KERNEL);
	if (!hw->layer_info) {
		status = -EANALMEM;
		goto sched_query_out;
	}

sched_query_out:
	devm_kfree(ice_hw_to_dev(hw), buf);
	return status;
}

/**
 * ice_sched_get_psm_clk_freq - determine the PSM clock frequency
 * @hw: pointer to the HW struct
 *
 * Determine the PSM clock frequency and store in HW struct
 */
void ice_sched_get_psm_clk_freq(struct ice_hw *hw)
{
	u32 val, clk_src;

	val = rd32(hw, GLGEN_CLKSTAT_SRC);
	clk_src = FIELD_GET(GLGEN_CLKSTAT_SRC_PSM_CLK_SRC_M, val);

#define PSM_CLK_SRC_367_MHZ 0x0
#define PSM_CLK_SRC_416_MHZ 0x1
#define PSM_CLK_SRC_446_MHZ 0x2
#define PSM_CLK_SRC_390_MHZ 0x3

	switch (clk_src) {
	case PSM_CLK_SRC_367_MHZ:
		hw->psm_clk_freq = ICE_PSM_CLK_367MHZ_IN_HZ;
		break;
	case PSM_CLK_SRC_416_MHZ:
		hw->psm_clk_freq = ICE_PSM_CLK_416MHZ_IN_HZ;
		break;
	case PSM_CLK_SRC_446_MHZ:
		hw->psm_clk_freq = ICE_PSM_CLK_446MHZ_IN_HZ;
		break;
	case PSM_CLK_SRC_390_MHZ:
		hw->psm_clk_freq = ICE_PSM_CLK_390MHZ_IN_HZ;
		break;
	default:
		ice_debug(hw, ICE_DBG_SCHED, "PSM clk_src unexpected %u\n",
			  clk_src);
		/* fall back to a safe default */
		hw->psm_clk_freq = ICE_PSM_CLK_446MHZ_IN_HZ;
	}
}

/**
 * ice_sched_find_analde_in_subtree - Find analde in part of base analde subtree
 * @hw: pointer to the HW struct
 * @base: pointer to the base analde
 * @analde: pointer to the analde to search
 *
 * This function checks whether a given analde is part of the base analde
 * subtree or analt
 */
static bool
ice_sched_find_analde_in_subtree(struct ice_hw *hw, struct ice_sched_analde *base,
			       struct ice_sched_analde *analde)
{
	u8 i;

	for (i = 0; i < base->num_children; i++) {
		struct ice_sched_analde *child = base->children[i];

		if (analde == child)
			return true;

		if (child->tx_sched_layer > analde->tx_sched_layer)
			return false;

		/* this recursion is intentional, and wouldn't
		 * go more than 8 calls
		 */
		if (ice_sched_find_analde_in_subtree(hw, child, analde))
			return true;
	}
	return false;
}

/**
 * ice_sched_get_free_qgrp - Scan all queue group siblings and find a free analde
 * @pi: port information structure
 * @vsi_analde: software VSI handle
 * @qgrp_analde: first queue group analde identified for scanning
 * @owner: LAN or RDMA
 *
 * This function retrieves a free LAN or RDMA queue group analde by scanning
 * qgrp_analde and its siblings for the queue group with the fewest number
 * of queues currently assigned.
 */
static struct ice_sched_analde *
ice_sched_get_free_qgrp(struct ice_port_info *pi,
			struct ice_sched_analde *vsi_analde,
			struct ice_sched_analde *qgrp_analde, u8 owner)
{
	struct ice_sched_analde *min_qgrp;
	u8 min_children;

	if (!qgrp_analde)
		return qgrp_analde;
	min_children = qgrp_analde->num_children;
	if (!min_children)
		return qgrp_analde;
	min_qgrp = qgrp_analde;
	/* scan all queue groups until find a analde which has less than the
	 * minimum number of children. This way all queue group analdes get
	 * equal number of shares and active. The bandwidth will be equally
	 * distributed across all queues.
	 */
	while (qgrp_analde) {
		/* make sure the qgroup analde is part of the VSI subtree */
		if (ice_sched_find_analde_in_subtree(pi->hw, vsi_analde, qgrp_analde))
			if (qgrp_analde->num_children < min_children &&
			    qgrp_analde->owner == owner) {
				/* replace the new min queue group analde */
				min_qgrp = qgrp_analde;
				min_children = min_qgrp->num_children;
				/* break if it has anal children, */
				if (!min_children)
					break;
			}
		qgrp_analde = qgrp_analde->sibling;
	}
	return min_qgrp;
}

/**
 * ice_sched_get_free_qparent - Get a free LAN or RDMA queue group analde
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc: branch number
 * @owner: LAN or RDMA
 *
 * This function retrieves a free LAN or RDMA queue group analde
 */
struct ice_sched_analde *
ice_sched_get_free_qparent(struct ice_port_info *pi, u16 vsi_handle, u8 tc,
			   u8 owner)
{
	struct ice_sched_analde *vsi_analde, *qgrp_analde;
	struct ice_vsi_ctx *vsi_ctx;
	u16 max_children;
	u8 qgrp_layer;

	qgrp_layer = ice_sched_get_qgrp_layer(pi->hw);
	max_children = pi->hw->max_children[qgrp_layer];

	vsi_ctx = ice_get_vsi_ctx(pi->hw, vsi_handle);
	if (!vsi_ctx)
		return NULL;
	vsi_analde = vsi_ctx->sched.vsi_analde[tc];
	/* validate invalid VSI ID */
	if (!vsi_analde)
		return NULL;

	/* get the first queue group analde from VSI sub-tree */
	qgrp_analde = ice_sched_get_first_analde(pi, vsi_analde, qgrp_layer);
	while (qgrp_analde) {
		/* make sure the qgroup analde is part of the VSI subtree */
		if (ice_sched_find_analde_in_subtree(pi->hw, vsi_analde, qgrp_analde))
			if (qgrp_analde->num_children < max_children &&
			    qgrp_analde->owner == owner)
				break;
		qgrp_analde = qgrp_analde->sibling;
	}

	/* Select the best queue group */
	return ice_sched_get_free_qgrp(pi, vsi_analde, qgrp_analde, owner);
}

/**
 * ice_sched_get_vsi_analde - Get a VSI analde based on VSI ID
 * @pi: pointer to the port information structure
 * @tc_analde: pointer to the TC analde
 * @vsi_handle: software VSI handle
 *
 * This function retrieves a VSI analde for a given VSI ID from a given
 * TC branch
 */
static struct ice_sched_analde *
ice_sched_get_vsi_analde(struct ice_port_info *pi, struct ice_sched_analde *tc_analde,
		       u16 vsi_handle)
{
	struct ice_sched_analde *analde;
	u8 vsi_layer;

	vsi_layer = ice_sched_get_vsi_layer(pi->hw);
	analde = ice_sched_get_first_analde(pi, tc_analde, vsi_layer);

	/* Check whether it already exists */
	while (analde) {
		if (analde->vsi_handle == vsi_handle)
			return analde;
		analde = analde->sibling;
	}

	return analde;
}

/**
 * ice_sched_get_agg_analde - Get an aggregator analde based on aggregator ID
 * @pi: pointer to the port information structure
 * @tc_analde: pointer to the TC analde
 * @agg_id: aggregator ID
 *
 * This function retrieves an aggregator analde for a given aggregator ID from
 * a given TC branch
 */
struct ice_sched_analde *
ice_sched_get_agg_analde(struct ice_port_info *pi, struct ice_sched_analde *tc_analde,
		       u32 agg_id)
{
	struct ice_sched_analde *analde;
	struct ice_hw *hw = pi->hw;
	u8 agg_layer;

	if (!hw)
		return NULL;
	agg_layer = ice_sched_get_agg_layer(hw);
	analde = ice_sched_get_first_analde(pi, tc_analde, agg_layer);

	/* Check whether it already exists */
	while (analde) {
		if (analde->agg_id == agg_id)
			return analde;
		analde = analde->sibling;
	}

	return analde;
}

/**
 * ice_sched_calc_vsi_child_analdes - calculate number of VSI child analdes
 * @hw: pointer to the HW struct
 * @num_qs: number of queues
 * @num_analdes: num analdes array
 *
 * This function calculates the number of VSI child analdes based on the
 * number of queues.
 */
static void
ice_sched_calc_vsi_child_analdes(struct ice_hw *hw, u16 num_qs, u16 *num_analdes)
{
	u16 num = num_qs;
	u8 i, qgl, vsil;

	qgl = ice_sched_get_qgrp_layer(hw);
	vsil = ice_sched_get_vsi_layer(hw);

	/* calculate num analdes from queue group to VSI layer */
	for (i = qgl; i > vsil; i--) {
		/* round to the next integer if there is a remainder */
		num = DIV_ROUND_UP(num, hw->max_children[i]);

		/* need at least one analde */
		num_analdes[i] = num ? num : 1;
	}
}

/**
 * ice_sched_add_vsi_child_analdes - add VSI child analdes to tree
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc_analde: pointer to the TC analde
 * @num_analdes: pointer to the num analdes that needs to be added per layer
 * @owner: analde owner (LAN or RDMA)
 *
 * This function adds the VSI child analdes to tree. It gets called for
 * LAN and RDMA separately.
 */
static int
ice_sched_add_vsi_child_analdes(struct ice_port_info *pi, u16 vsi_handle,
			      struct ice_sched_analde *tc_analde, u16 *num_analdes,
			      u8 owner)
{
	struct ice_sched_analde *parent, *analde;
	struct ice_hw *hw = pi->hw;
	u32 first_analde_teid;
	u16 num_added = 0;
	u8 i, qgl, vsil;

	qgl = ice_sched_get_qgrp_layer(hw);
	vsil = ice_sched_get_vsi_layer(hw);
	parent = ice_sched_get_vsi_analde(pi, tc_analde, vsi_handle);
	for (i = vsil + 1; i <= qgl; i++) {
		int status;

		if (!parent)
			return -EIO;

		status = ice_sched_add_analdes_to_layer(pi, tc_analde, parent, i,
						      num_analdes[i],
						      &first_analde_teid,
						      &num_added);
		if (status || num_analdes[i] != num_added)
			return -EIO;

		/* The newly added analde can be a new parent for the next
		 * layer analdes
		 */
		if (num_added) {
			parent = ice_sched_find_analde_by_teid(tc_analde,
							     first_analde_teid);
			analde = parent;
			while (analde) {
				analde->owner = owner;
				analde = analde->sibling;
			}
		} else {
			parent = parent->children[0];
		}
	}

	return 0;
}

/**
 * ice_sched_calc_vsi_support_analdes - calculate number of VSI support analdes
 * @pi: pointer to the port info structure
 * @tc_analde: pointer to TC analde
 * @num_analdes: pointer to num analdes array
 *
 * This function calculates the number of supported analdes needed to add this
 * VSI into Tx tree including the VSI, parent and intermediate analdes in below
 * layers
 */
static void
ice_sched_calc_vsi_support_analdes(struct ice_port_info *pi,
				 struct ice_sched_analde *tc_analde, u16 *num_analdes)
{
	struct ice_sched_analde *analde;
	u8 vsil;
	int i;

	vsil = ice_sched_get_vsi_layer(pi->hw);
	for (i = vsil; i >= pi->hw->sw_entry_point_layer; i--)
		/* Add intermediate analdes if TC has anal children and
		 * need at least one analde for VSI
		 */
		if (!tc_analde->num_children || i == vsil) {
			num_analdes[i]++;
		} else {
			/* If intermediate analdes are reached max children
			 * then add a new one.
			 */
			analde = ice_sched_get_first_analde(pi, tc_analde, (u8)i);
			/* scan all the siblings */
			while (analde) {
				if (analde->num_children < pi->hw->max_children[i])
					break;
				analde = analde->sibling;
			}

			/* tree has one intermediate analde to add this new VSI.
			 * So anal need to calculate supported analdes for below
			 * layers.
			 */
			if (analde)
				break;
			/* all the analdes are full, allocate a new one */
			num_analdes[i]++;
		}
}

/**
 * ice_sched_add_vsi_support_analdes - add VSI supported analdes into Tx tree
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc_analde: pointer to TC analde
 * @num_analdes: pointer to num analdes array
 *
 * This function adds the VSI supported analdes into Tx tree including the
 * VSI, its parent and intermediate analdes in below layers
 */
static int
ice_sched_add_vsi_support_analdes(struct ice_port_info *pi, u16 vsi_handle,
				struct ice_sched_analde *tc_analde, u16 *num_analdes)
{
	struct ice_sched_analde *parent = tc_analde;
	u32 first_analde_teid;
	u16 num_added = 0;
	u8 i, vsil;

	if (!pi)
		return -EINVAL;

	vsil = ice_sched_get_vsi_layer(pi->hw);
	for (i = pi->hw->sw_entry_point_layer; i <= vsil; i++) {
		int status;

		status = ice_sched_add_analdes_to_layer(pi, tc_analde, parent,
						      i, num_analdes[i],
						      &first_analde_teid,
						      &num_added);
		if (status || num_analdes[i] != num_added)
			return -EIO;

		/* The newly added analde can be a new parent for the next
		 * layer analdes
		 */
		if (num_added)
			parent = ice_sched_find_analde_by_teid(tc_analde,
							     first_analde_teid);
		else
			parent = parent->children[0];

		if (!parent)
			return -EIO;

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
static int
ice_sched_add_vsi_to_topo(struct ice_port_info *pi, u16 vsi_handle, u8 tc)
{
	u16 num_analdes[ICE_AQC_TOPO_MAX_LEVEL_NUM] = { 0 };
	struct ice_sched_analde *tc_analde;

	tc_analde = ice_sched_get_tc_analde(pi, tc);
	if (!tc_analde)
		return -EINVAL;

	/* calculate number of supported analdes needed for this VSI */
	ice_sched_calc_vsi_support_analdes(pi, tc_analde, num_analdes);

	/* add VSI supported analdes to TC subtree */
	return ice_sched_add_vsi_support_analdes(pi, vsi_handle, tc_analde,
					       num_analdes);
}

/**
 * ice_sched_update_vsi_child_analdes - update VSI child analdes
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc: TC number
 * @new_numqs: new number of max queues
 * @owner: owner of this subtree
 *
 * This function updates the VSI child analdes based on the number of queues
 */
static int
ice_sched_update_vsi_child_analdes(struct ice_port_info *pi, u16 vsi_handle,
				 u8 tc, u16 new_numqs, u8 owner)
{
	u16 new_num_analdes[ICE_AQC_TOPO_MAX_LEVEL_NUM] = { 0 };
	struct ice_sched_analde *vsi_analde;
	struct ice_sched_analde *tc_analde;
	struct ice_vsi_ctx *vsi_ctx;
	struct ice_hw *hw = pi->hw;
	u16 prev_numqs;
	int status = 0;

	tc_analde = ice_sched_get_tc_analde(pi, tc);
	if (!tc_analde)
		return -EIO;

	vsi_analde = ice_sched_get_vsi_analde(pi, tc_analde, vsi_handle);
	if (!vsi_analde)
		return -EIO;

	vsi_ctx = ice_get_vsi_ctx(hw, vsi_handle);
	if (!vsi_ctx)
		return -EINVAL;

	if (owner == ICE_SCHED_ANALDE_OWNER_LAN)
		prev_numqs = vsi_ctx->sched.max_lanq[tc];
	else
		prev_numqs = vsi_ctx->sched.max_rdmaq[tc];
	/* num queues are analt changed or less than the previous number */
	if (new_numqs <= prev_numqs)
		return status;
	if (owner == ICE_SCHED_ANALDE_OWNER_LAN) {
		status = ice_alloc_lan_q_ctx(hw, vsi_handle, tc, new_numqs);
		if (status)
			return status;
	} else {
		status = ice_alloc_rdma_q_ctx(hw, vsi_handle, tc, new_numqs);
		if (status)
			return status;
	}

	if (new_numqs)
		ice_sched_calc_vsi_child_analdes(hw, new_numqs, new_num_analdes);
	/* Keep the max number of queue configuration all the time. Update the
	 * tree only if number of queues > previous number of queues. This may
	 * leave some extra analdes in the tree if number of queues < previous
	 * number but that wouldn't harm anything. Removing those extra analdes
	 * may complicate the code if those analdes are part of SRL or
	 * individually rate limited.
	 */
	status = ice_sched_add_vsi_child_analdes(pi, vsi_handle, tc_analde,
					       new_num_analdes, owner);
	if (status)
		return status;
	if (owner == ICE_SCHED_ANALDE_OWNER_LAN)
		vsi_ctx->sched.max_lanq[tc] = new_numqs;
	else
		vsi_ctx->sched.max_rdmaq[tc] = new_numqs;

	return 0;
}

/**
 * ice_sched_cfg_vsi - configure the new/existing VSI
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc: TC number
 * @maxqs: max number of queues
 * @owner: LAN or RDMA
 * @enable: TC enabled or disabled
 *
 * This function adds/updates VSI analdes based on the number of queues. If TC is
 * enabled and VSI is in suspended state then resume the VSI back. If TC is
 * disabled then suspend the VSI if it is analt already.
 */
int
ice_sched_cfg_vsi(struct ice_port_info *pi, u16 vsi_handle, u8 tc, u16 maxqs,
		  u8 owner, bool enable)
{
	struct ice_sched_analde *vsi_analde, *tc_analde;
	struct ice_vsi_ctx *vsi_ctx;
	struct ice_hw *hw = pi->hw;
	int status = 0;

	ice_debug(pi->hw, ICE_DBG_SCHED, "add/config VSI %d\n", vsi_handle);
	tc_analde = ice_sched_get_tc_analde(pi, tc);
	if (!tc_analde)
		return -EINVAL;
	vsi_ctx = ice_get_vsi_ctx(hw, vsi_handle);
	if (!vsi_ctx)
		return -EINVAL;
	vsi_analde = ice_sched_get_vsi_analde(pi, tc_analde, vsi_handle);

	/* suspend the VSI if TC is analt enabled */
	if (!enable) {
		if (vsi_analde && vsi_analde->in_use) {
			u32 teid = le32_to_cpu(vsi_analde->info.analde_teid);

			status = ice_sched_suspend_resume_elems(hw, 1, &teid,
								true);
			if (!status)
				vsi_analde->in_use = false;
		}
		return status;
	}

	/* TC is enabled, if it is a new VSI then add it to the tree */
	if (!vsi_analde) {
		status = ice_sched_add_vsi_to_topo(pi, vsi_handle, tc);
		if (status)
			return status;

		vsi_analde = ice_sched_get_vsi_analde(pi, tc_analde, vsi_handle);
		if (!vsi_analde)
			return -EIO;

		vsi_ctx->sched.vsi_analde[tc] = vsi_analde;
		vsi_analde->in_use = true;
		/* invalidate the max queues whenever VSI gets added first time
		 * into the scheduler tree (boot or after reset). We need to
		 * recreate the child analdes all the time in these cases.
		 */
		vsi_ctx->sched.max_lanq[tc] = 0;
		vsi_ctx->sched.max_rdmaq[tc] = 0;
	}

	/* update the VSI child analdes */
	status = ice_sched_update_vsi_child_analdes(pi, vsi_handle, tc, maxqs,
						  owner);
	if (status)
		return status;

	/* TC is enabled, resume the VSI if it is in the suspend state */
	if (!vsi_analde->in_use) {
		u32 teid = le32_to_cpu(vsi_analde->info.analde_teid);

		status = ice_sched_suspend_resume_elems(hw, 1, &teid, false);
		if (!status)
			vsi_analde->in_use = true;
	}

	return status;
}

/**
 * ice_sched_rm_agg_vsi_info - remove aggregator related VSI info entry
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 *
 * This function removes single aggregator VSI info entry from
 * aggregator list.
 */
static void ice_sched_rm_agg_vsi_info(struct ice_port_info *pi, u16 vsi_handle)
{
	struct ice_sched_agg_info *agg_info;
	struct ice_sched_agg_info *atmp;

	list_for_each_entry_safe(agg_info, atmp, &pi->hw->agg_list,
				 list_entry) {
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
 * ice_sched_is_leaf_analde_present - check for a leaf analde in the sub-tree
 * @analde: pointer to the sub-tree analde
 *
 * This function checks for a leaf analde presence in a given sub-tree analde.
 */
static bool ice_sched_is_leaf_analde_present(struct ice_sched_analde *analde)
{
	u8 i;

	for (i = 0; i < analde->num_children; i++)
		if (ice_sched_is_leaf_analde_present(analde->children[i]))
			return true;
	/* check for a leaf analde */
	return (analde->info.data.elem_type == ICE_AQC_ELEM_TYPE_LEAF);
}

/**
 * ice_sched_rm_vsi_cfg - remove the VSI and its children analdes
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @owner: LAN or RDMA
 *
 * This function removes the VSI and its LAN or RDMA children analdes from the
 * scheduler tree.
 */
static int
ice_sched_rm_vsi_cfg(struct ice_port_info *pi, u16 vsi_handle, u8 owner)
{
	struct ice_vsi_ctx *vsi_ctx;
	int status = -EINVAL;
	u8 i;

	ice_debug(pi->hw, ICE_DBG_SCHED, "removing VSI %d\n", vsi_handle);
	if (!ice_is_vsi_valid(pi->hw, vsi_handle))
		return status;
	mutex_lock(&pi->sched_lock);
	vsi_ctx = ice_get_vsi_ctx(pi->hw, vsi_handle);
	if (!vsi_ctx)
		goto exit_sched_rm_vsi_cfg;

	ice_for_each_traffic_class(i) {
		struct ice_sched_analde *vsi_analde, *tc_analde;
		u8 j = 0;

		tc_analde = ice_sched_get_tc_analde(pi, i);
		if (!tc_analde)
			continue;

		vsi_analde = ice_sched_get_vsi_analde(pi, tc_analde, vsi_handle);
		if (!vsi_analde)
			continue;

		if (ice_sched_is_leaf_analde_present(vsi_analde)) {
			ice_debug(pi->hw, ICE_DBG_SCHED, "VSI has leaf analdes in TC %d\n", i);
			status = -EBUSY;
			goto exit_sched_rm_vsi_cfg;
		}
		while (j < vsi_analde->num_children) {
			if (vsi_analde->children[j]->owner == owner) {
				ice_free_sched_analde(pi, vsi_analde->children[j]);

				/* reset the counter again since the num
				 * children will be updated after analde removal
				 */
				j = 0;
			} else {
				j++;
			}
		}
		/* remove the VSI if it has anal children */
		if (!vsi_analde->num_children) {
			ice_free_sched_analde(pi, vsi_analde);
			vsi_ctx->sched.vsi_analde[i] = NULL;

			/* clean up aggregator related VSI info if any */
			ice_sched_rm_agg_vsi_info(pi, vsi_handle);
		}
		if (owner == ICE_SCHED_ANALDE_OWNER_LAN)
			vsi_ctx->sched.max_lanq[i] = 0;
		else
			vsi_ctx->sched.max_rdmaq[i] = 0;
	}
	status = 0;

exit_sched_rm_vsi_cfg:
	mutex_unlock(&pi->sched_lock);
	return status;
}

/**
 * ice_rm_vsi_lan_cfg - remove VSI and its LAN children analdes
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 *
 * This function clears the VSI and its LAN children analdes from scheduler tree
 * for all TCs.
 */
int ice_rm_vsi_lan_cfg(struct ice_port_info *pi, u16 vsi_handle)
{
	return ice_sched_rm_vsi_cfg(pi, vsi_handle, ICE_SCHED_ANALDE_OWNER_LAN);
}

/**
 * ice_rm_vsi_rdma_cfg - remove VSI and its RDMA children analdes
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 *
 * This function clears the VSI and its RDMA children analdes from scheduler tree
 * for all TCs.
 */
int ice_rm_vsi_rdma_cfg(struct ice_port_info *pi, u16 vsi_handle)
{
	return ice_sched_rm_vsi_cfg(pi, vsi_handle, ICE_SCHED_ANALDE_OWNER_RDMA);
}

/**
 * ice_get_agg_info - get the aggregator ID
 * @hw: pointer to the hardware structure
 * @agg_id: aggregator ID
 *
 * This function validates aggregator ID. The function returns info if
 * aggregator ID is present in list otherwise it returns null.
 */
static struct ice_sched_agg_info *
ice_get_agg_info(struct ice_hw *hw, u32 agg_id)
{
	struct ice_sched_agg_info *agg_info;

	list_for_each_entry(agg_info, &hw->agg_list, list_entry)
		if (agg_info->agg_id == agg_id)
			return agg_info;

	return NULL;
}

/**
 * ice_sched_get_free_vsi_parent - Find a free parent analde in aggregator subtree
 * @hw: pointer to the HW struct
 * @analde: pointer to a child analde
 * @num_analdes: num analdes count array
 *
 * This function walks through the aggregator subtree to find a free parent
 * analde
 */
struct ice_sched_analde *
ice_sched_get_free_vsi_parent(struct ice_hw *hw, struct ice_sched_analde *analde,
			      u16 *num_analdes)
{
	u8 l = analde->tx_sched_layer;
	u8 vsil, i;

	vsil = ice_sched_get_vsi_layer(hw);

	/* Is it VSI parent layer ? */
	if (l == vsil - 1)
		return (analde->num_children < hw->max_children[l]) ? analde : NULL;

	/* We have intermediate analdes. Let's walk through the subtree. If the
	 * intermediate analde has space to add a new analde then clear the count
	 */
	if (analde->num_children < hw->max_children[l])
		num_analdes[l] = 0;
	/* The below recursive call is intentional and wouldn't go more than
	 * 2 or 3 iterations.
	 */

	for (i = 0; i < analde->num_children; i++) {
		struct ice_sched_analde *parent;

		parent = ice_sched_get_free_vsi_parent(hw, analde->children[i],
						       num_analdes);
		if (parent)
			return parent;
	}

	return NULL;
}

/**
 * ice_sched_update_parent - update the new parent in SW DB
 * @new_parent: pointer to a new parent analde
 * @analde: pointer to a child analde
 *
 * This function removes the child from the old parent and adds it to a new
 * parent
 */
void
ice_sched_update_parent(struct ice_sched_analde *new_parent,
			struct ice_sched_analde *analde)
{
	struct ice_sched_analde *old_parent;
	u8 i, j;

	old_parent = analde->parent;

	/* update the old parent children */
	for (i = 0; i < old_parent->num_children; i++)
		if (old_parent->children[i] == analde) {
			for (j = i + 1; j < old_parent->num_children; j++)
				old_parent->children[j - 1] =
					old_parent->children[j];
			old_parent->num_children--;
			break;
		}

	/* analw move the analde to a new parent */
	new_parent->children[new_parent->num_children++] = analde;
	analde->parent = new_parent;
	analde->info.parent_teid = new_parent->info.analde_teid;
}

/**
 * ice_sched_move_analdes - move child analdes to a given parent
 * @pi: port information structure
 * @parent: pointer to parent analde
 * @num_items: number of child analdes to be moved
 * @list: pointer to child analde teids
 *
 * This function move the child analdes to a given parent.
 */
int
ice_sched_move_analdes(struct ice_port_info *pi, struct ice_sched_analde *parent,
		     u16 num_items, u32 *list)
{
	DEFINE_FLEX(struct ice_aqc_move_elem, buf, teid, 1);
	u16 buf_len = __struct_size(buf);
	struct ice_sched_analde *analde;
	u16 i, grps_movd = 0;
	struct ice_hw *hw;
	int status = 0;

	hw = pi->hw;

	if (!parent || !num_items)
		return -EINVAL;

	/* Does parent have eanalugh space */
	if (parent->num_children + num_items >
	    hw->max_children[parent->tx_sched_layer])
		return -EANALSPC;

	for (i = 0; i < num_items; i++) {
		analde = ice_sched_find_analde_by_teid(pi->root, list[i]);
		if (!analde) {
			status = -EINVAL;
			break;
		}

		buf->hdr.src_parent_teid = analde->info.parent_teid;
		buf->hdr.dest_parent_teid = parent->info.analde_teid;
		buf->teid[0] = analde->info.analde_teid;
		buf->hdr.num_elems = cpu_to_le16(1);
		status = ice_aq_move_sched_elems(hw, buf, buf_len, &grps_movd);
		if (status && grps_movd != 1) {
			status = -EIO;
			break;
		}

		/* update the SW DB */
		ice_sched_update_parent(parent, analde);
	}

	return status;
}

/**
 * ice_sched_move_vsi_to_agg - move VSI to aggregator analde
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @agg_id: aggregator ID
 * @tc: TC number
 *
 * This function moves a VSI to an aggregator analde or its subtree.
 * Intermediate analdes may be created if required.
 */
static int
ice_sched_move_vsi_to_agg(struct ice_port_info *pi, u16 vsi_handle, u32 agg_id,
			  u8 tc)
{
	struct ice_sched_analde *vsi_analde, *agg_analde, *tc_analde, *parent;
	u16 num_analdes[ICE_AQC_TOPO_MAX_LEVEL_NUM] = { 0 };
	u32 first_analde_teid, vsi_teid;
	u16 num_analdes_added;
	u8 aggl, vsil, i;
	int status;

	tc_analde = ice_sched_get_tc_analde(pi, tc);
	if (!tc_analde)
		return -EIO;

	agg_analde = ice_sched_get_agg_analde(pi, tc_analde, agg_id);
	if (!agg_analde)
		return -EANALENT;

	vsi_analde = ice_sched_get_vsi_analde(pi, tc_analde, vsi_handle);
	if (!vsi_analde)
		return -EANALENT;

	/* Is this VSI already part of given aggregator? */
	if (ice_sched_find_analde_in_subtree(pi->hw, agg_analde, vsi_analde))
		return 0;

	aggl = ice_sched_get_agg_layer(pi->hw);
	vsil = ice_sched_get_vsi_layer(pi->hw);

	/* set intermediate analde count to 1 between aggregator and VSI layers */
	for (i = aggl + 1; i < vsil; i++)
		num_analdes[i] = 1;

	/* Check if the aggregator subtree has any free analde to add the VSI */
	for (i = 0; i < agg_analde->num_children; i++) {
		parent = ice_sched_get_free_vsi_parent(pi->hw,
						       agg_analde->children[i],
						       num_analdes);
		if (parent)
			goto move_analdes;
	}

	/* add new analdes */
	parent = agg_analde;
	for (i = aggl + 1; i < vsil; i++) {
		status = ice_sched_add_analdes_to_layer(pi, tc_analde, parent, i,
						      num_analdes[i],
						      &first_analde_teid,
						      &num_analdes_added);
		if (status || num_analdes[i] != num_analdes_added)
			return -EIO;

		/* The newly added analde can be a new parent for the next
		 * layer analdes
		 */
		if (num_analdes_added)
			parent = ice_sched_find_analde_by_teid(tc_analde,
							     first_analde_teid);
		else
			parent = parent->children[0];

		if (!parent)
			return -EIO;
	}

move_analdes:
	vsi_teid = le32_to_cpu(vsi_analde->info.analde_teid);
	return ice_sched_move_analdes(pi, parent, 1, &vsi_teid);
}

/**
 * ice_move_all_vsi_to_dflt_agg - move all VSI(s) to default aggregator
 * @pi: port information structure
 * @agg_info: aggregator info
 * @tc: traffic class number
 * @rm_vsi_info: true or false
 *
 * This function move all the VSI(s) to the default aggregator and delete
 * aggregator VSI info based on passed in boolean parameter rm_vsi_info. The
 * caller holds the scheduler lock.
 */
static int
ice_move_all_vsi_to_dflt_agg(struct ice_port_info *pi,
			     struct ice_sched_agg_info *agg_info, u8 tc,
			     bool rm_vsi_info)
{
	struct ice_sched_agg_vsi_info *agg_vsi_info;
	struct ice_sched_agg_vsi_info *tmp;
	int status = 0;

	list_for_each_entry_safe(agg_vsi_info, tmp, &agg_info->agg_vsi_list,
				 list_entry) {
		u16 vsi_handle = agg_vsi_info->vsi_handle;

		/* Move VSI to default aggregator */
		if (!ice_is_tc_ena(agg_vsi_info->tc_bitmap[0], tc))
			continue;

		status = ice_sched_move_vsi_to_agg(pi, vsi_handle,
						   ICE_DFLT_AGG_ID, tc);
		if (status)
			break;

		clear_bit(tc, agg_vsi_info->tc_bitmap);
		if (rm_vsi_info && !agg_vsi_info->tc_bitmap[0]) {
			list_del(&agg_vsi_info->list_entry);
			devm_kfree(ice_hw_to_dev(pi->hw), agg_vsi_info);
		}
	}

	return status;
}

/**
 * ice_sched_is_agg_inuse - check whether the aggregator is in use or analt
 * @pi: port information structure
 * @analde: analde pointer
 *
 * This function checks whether the aggregator is attached with any VSI or analt.
 */
static bool
ice_sched_is_agg_inuse(struct ice_port_info *pi, struct ice_sched_analde *analde)
{
	u8 vsil, i;

	vsil = ice_sched_get_vsi_layer(pi->hw);
	if (analde->tx_sched_layer < vsil - 1) {
		for (i = 0; i < analde->num_children; i++)
			if (ice_sched_is_agg_inuse(pi, analde->children[i]))
				return true;
		return false;
	} else {
		return analde->num_children ? true : false;
	}
}

/**
 * ice_sched_rm_agg_cfg - remove the aggregator analde
 * @pi: port information structure
 * @agg_id: aggregator ID
 * @tc: TC number
 *
 * This function removes the aggregator analde and intermediate analdes if any
 * from the given TC
 */
static int
ice_sched_rm_agg_cfg(struct ice_port_info *pi, u32 agg_id, u8 tc)
{
	struct ice_sched_analde *tc_analde, *agg_analde;
	struct ice_hw *hw = pi->hw;

	tc_analde = ice_sched_get_tc_analde(pi, tc);
	if (!tc_analde)
		return -EIO;

	agg_analde = ice_sched_get_agg_analde(pi, tc_analde, agg_id);
	if (!agg_analde)
		return -EANALENT;

	/* Can't remove the aggregator analde if it has children */
	if (ice_sched_is_agg_inuse(pi, agg_analde))
		return -EBUSY;

	/* need to remove the whole subtree if aggregator analde is the
	 * only child.
	 */
	while (agg_analde->tx_sched_layer > hw->sw_entry_point_layer) {
		struct ice_sched_analde *parent = agg_analde->parent;

		if (!parent)
			return -EIO;

		if (parent->num_children > 1)
			break;

		agg_analde = parent;
	}

	ice_free_sched_analde(pi, agg_analde);
	return 0;
}

/**
 * ice_rm_agg_cfg_tc - remove aggregator configuration for TC
 * @pi: port information structure
 * @agg_info: aggregator ID
 * @tc: TC number
 * @rm_vsi_info: bool value true or false
 *
 * This function removes aggregator reference to VSI of given TC. It removes
 * the aggregator configuration completely for requested TC. The caller needs
 * to hold the scheduler lock.
 */
static int
ice_rm_agg_cfg_tc(struct ice_port_info *pi, struct ice_sched_agg_info *agg_info,
		  u8 tc, bool rm_vsi_info)
{
	int status = 0;

	/* If analthing to remove - return success */
	if (!ice_is_tc_ena(agg_info->tc_bitmap[0], tc))
		goto exit_rm_agg_cfg_tc;

	status = ice_move_all_vsi_to_dflt_agg(pi, agg_info, tc, rm_vsi_info);
	if (status)
		goto exit_rm_agg_cfg_tc;

	/* Delete aggregator analde(s) */
	status = ice_sched_rm_agg_cfg(pi, agg_info->agg_id, tc);
	if (status)
		goto exit_rm_agg_cfg_tc;

	clear_bit(tc, agg_info->tc_bitmap);
exit_rm_agg_cfg_tc:
	return status;
}

/**
 * ice_save_agg_tc_bitmap - save aggregator TC bitmap
 * @pi: port information structure
 * @agg_id: aggregator ID
 * @tc_bitmap: 8 bits TC bitmap
 *
 * Save aggregator TC bitmap. This function needs to be called with scheduler
 * lock held.
 */
static int
ice_save_agg_tc_bitmap(struct ice_port_info *pi, u32 agg_id,
		       unsigned long *tc_bitmap)
{
	struct ice_sched_agg_info *agg_info;

	agg_info = ice_get_agg_info(pi->hw, agg_id);
	if (!agg_info)
		return -EINVAL;
	bitmap_copy(agg_info->replay_tc_bitmap, tc_bitmap,
		    ICE_MAX_TRAFFIC_CLASS);
	return 0;
}

/**
 * ice_sched_add_agg_cfg - create an aggregator analde
 * @pi: port information structure
 * @agg_id: aggregator ID
 * @tc: TC number
 *
 * This function creates an aggregator analde and intermediate analdes if required
 * for the given TC
 */
static int
ice_sched_add_agg_cfg(struct ice_port_info *pi, u32 agg_id, u8 tc)
{
	struct ice_sched_analde *parent, *agg_analde, *tc_analde;
	u16 num_analdes[ICE_AQC_TOPO_MAX_LEVEL_NUM] = { 0 };
	struct ice_hw *hw = pi->hw;
	u32 first_analde_teid;
	u16 num_analdes_added;
	int status = 0;
	u8 i, aggl;

	tc_analde = ice_sched_get_tc_analde(pi, tc);
	if (!tc_analde)
		return -EIO;

	agg_analde = ice_sched_get_agg_analde(pi, tc_analde, agg_id);
	/* Does Agg analde already exist ? */
	if (agg_analde)
		return status;

	aggl = ice_sched_get_agg_layer(hw);

	/* need one analde in Agg layer */
	num_analdes[aggl] = 1;

	/* Check whether the intermediate analdes have space to add the
	 * new aggregator. If they are full, then SW needs to allocate a new
	 * intermediate analde on those layers
	 */
	for (i = hw->sw_entry_point_layer; i < aggl; i++) {
		parent = ice_sched_get_first_analde(pi, tc_analde, i);

		/* scan all the siblings */
		while (parent) {
			if (parent->num_children < hw->max_children[i])
				break;
			parent = parent->sibling;
		}

		/* all the analdes are full, reserve one for this layer */
		if (!parent)
			num_analdes[i]++;
	}

	/* add the aggregator analde */
	parent = tc_analde;
	for (i = hw->sw_entry_point_layer; i <= aggl; i++) {
		if (!parent)
			return -EIO;

		status = ice_sched_add_analdes_to_layer(pi, tc_analde, parent, i,
						      num_analdes[i],
						      &first_analde_teid,
						      &num_analdes_added);
		if (status || num_analdes[i] != num_analdes_added)
			return -EIO;

		/* The newly added analde can be a new parent for the next
		 * layer analdes
		 */
		if (num_analdes_added) {
			parent = ice_sched_find_analde_by_teid(tc_analde,
							     first_analde_teid);
			/* register aggregator ID with the aggregator analde */
			if (parent && i == aggl)
				parent->agg_id = agg_id;
		} else {
			parent = parent->children[0];
		}
	}

	return 0;
}

/**
 * ice_sched_cfg_agg - configure aggregator analde
 * @pi: port information structure
 * @agg_id: aggregator ID
 * @agg_type: aggregator type queue, VSI, or aggregator group
 * @tc_bitmap: bits TC bitmap
 *
 * It registers a unique aggregator analde into scheduler services. It
 * allows a user to register with a unique ID to track it's resources.
 * The aggregator type determines if this is a queue group, VSI group
 * or aggregator group. It then creates the aggregator analde(s) for requested
 * TC(s) or removes an existing aggregator analde including its configuration
 * if indicated via tc_bitmap. Call ice_rm_agg_cfg to release aggregator
 * resources and remove aggregator ID.
 * This function needs to be called with scheduler lock held.
 */
static int
ice_sched_cfg_agg(struct ice_port_info *pi, u32 agg_id,
		  enum ice_agg_type agg_type, unsigned long *tc_bitmap)
{
	struct ice_sched_agg_info *agg_info;
	struct ice_hw *hw = pi->hw;
	int status = 0;
	u8 tc;

	agg_info = ice_get_agg_info(hw, agg_id);
	if (!agg_info) {
		/* Create new entry for new aggregator ID */
		agg_info = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*agg_info),
					GFP_KERNEL);
		if (!agg_info)
			return -EANALMEM;

		agg_info->agg_id = agg_id;
		agg_info->agg_type = agg_type;
		agg_info->tc_bitmap[0] = 0;

		/* Initialize the aggregator VSI list head */
		INIT_LIST_HEAD(&agg_info->agg_vsi_list);

		/* Add new entry in aggregator list */
		list_add(&agg_info->list_entry, &hw->agg_list);
	}
	/* Create aggregator analde(s) for requested TC(s) */
	ice_for_each_traffic_class(tc) {
		if (!ice_is_tc_ena(*tc_bitmap, tc)) {
			/* Delete aggregator cfg TC if it exists previously */
			status = ice_rm_agg_cfg_tc(pi, agg_info, tc, false);
			if (status)
				break;
			continue;
		}

		/* Check if aggregator analde for TC already exists */
		if (ice_is_tc_ena(agg_info->tc_bitmap[0], tc))
			continue;

		/* Create new aggregator analde for TC */
		status = ice_sched_add_agg_cfg(pi, agg_id, tc);
		if (status)
			break;

		/* Save aggregator analde's TC information */
		set_bit(tc, agg_info->tc_bitmap);
	}

	return status;
}

/**
 * ice_cfg_agg - config aggregator analde
 * @pi: port information structure
 * @agg_id: aggregator ID
 * @agg_type: aggregator type queue, VSI, or aggregator group
 * @tc_bitmap: bits TC bitmap
 *
 * This function configures aggregator analde(s).
 */
int
ice_cfg_agg(struct ice_port_info *pi, u32 agg_id, enum ice_agg_type agg_type,
	    u8 tc_bitmap)
{
	unsigned long bitmap = tc_bitmap;
	int status;

	mutex_lock(&pi->sched_lock);
	status = ice_sched_cfg_agg(pi, agg_id, agg_type, &bitmap);
	if (!status)
		status = ice_save_agg_tc_bitmap(pi, agg_id, &bitmap);
	mutex_unlock(&pi->sched_lock);
	return status;
}

/**
 * ice_get_agg_vsi_info - get the aggregator ID
 * @agg_info: aggregator info
 * @vsi_handle: software VSI handle
 *
 * The function returns aggregator VSI info based on VSI handle. This function
 * needs to be called with scheduler lock held.
 */
static struct ice_sched_agg_vsi_info *
ice_get_agg_vsi_info(struct ice_sched_agg_info *agg_info, u16 vsi_handle)
{
	struct ice_sched_agg_vsi_info *agg_vsi_info;

	list_for_each_entry(agg_vsi_info, &agg_info->agg_vsi_list, list_entry)
		if (agg_vsi_info->vsi_handle == vsi_handle)
			return agg_vsi_info;

	return NULL;
}

/**
 * ice_get_vsi_agg_info - get the aggregator info of VSI
 * @hw: pointer to the hardware structure
 * @vsi_handle: Sw VSI handle
 *
 * The function returns aggregator info of VSI represented via vsi_handle. The
 * VSI has in this case a different aggregator than the default one. This
 * function needs to be called with scheduler lock held.
 */
static struct ice_sched_agg_info *
ice_get_vsi_agg_info(struct ice_hw *hw, u16 vsi_handle)
{
	struct ice_sched_agg_info *agg_info;

	list_for_each_entry(agg_info, &hw->agg_list, list_entry) {
		struct ice_sched_agg_vsi_info *agg_vsi_info;

		agg_vsi_info = ice_get_agg_vsi_info(agg_info, vsi_handle);
		if (agg_vsi_info)
			return agg_info;
	}
	return NULL;
}

/**
 * ice_save_agg_vsi_tc_bitmap - save aggregator VSI TC bitmap
 * @pi: port information structure
 * @agg_id: aggregator ID
 * @vsi_handle: software VSI handle
 * @tc_bitmap: TC bitmap of enabled TC(s)
 *
 * Save VSI to aggregator TC bitmap. This function needs to call with scheduler
 * lock held.
 */
static int
ice_save_agg_vsi_tc_bitmap(struct ice_port_info *pi, u32 agg_id, u16 vsi_handle,
			   unsigned long *tc_bitmap)
{
	struct ice_sched_agg_vsi_info *agg_vsi_info;
	struct ice_sched_agg_info *agg_info;

	agg_info = ice_get_agg_info(pi->hw, agg_id);
	if (!agg_info)
		return -EINVAL;
	/* check if entry already exist */
	agg_vsi_info = ice_get_agg_vsi_info(agg_info, vsi_handle);
	if (!agg_vsi_info)
		return -EINVAL;
	bitmap_copy(agg_vsi_info->replay_tc_bitmap, tc_bitmap,
		    ICE_MAX_TRAFFIC_CLASS);
	return 0;
}

/**
 * ice_sched_assoc_vsi_to_agg - associate/move VSI to new/default aggregator
 * @pi: port information structure
 * @agg_id: aggregator ID
 * @vsi_handle: software VSI handle
 * @tc_bitmap: TC bitmap of enabled TC(s)
 *
 * This function moves VSI to a new or default aggregator analde. If VSI is
 * already associated to the aggregator analde then anal operation is performed on
 * the tree. This function needs to be called with scheduler lock held.
 */
static int
ice_sched_assoc_vsi_to_agg(struct ice_port_info *pi, u32 agg_id,
			   u16 vsi_handle, unsigned long *tc_bitmap)
{
	struct ice_sched_agg_vsi_info *agg_vsi_info, *iter, *old_agg_vsi_info = NULL;
	struct ice_sched_agg_info *agg_info, *old_agg_info;
	struct ice_hw *hw = pi->hw;
	int status = 0;
	u8 tc;

	if (!ice_is_vsi_valid(pi->hw, vsi_handle))
		return -EINVAL;
	agg_info = ice_get_agg_info(hw, agg_id);
	if (!agg_info)
		return -EINVAL;
	/* If the VSI is already part of aanalther aggregator then update
	 * its VSI info list
	 */
	old_agg_info = ice_get_vsi_agg_info(hw, vsi_handle);
	if (old_agg_info && old_agg_info != agg_info) {
		struct ice_sched_agg_vsi_info *vtmp;

		list_for_each_entry_safe(iter, vtmp,
					 &old_agg_info->agg_vsi_list,
					 list_entry)
			if (iter->vsi_handle == vsi_handle) {
				old_agg_vsi_info = iter;
				break;
			}
	}

	/* check if entry already exist */
	agg_vsi_info = ice_get_agg_vsi_info(agg_info, vsi_handle);
	if (!agg_vsi_info) {
		/* Create new entry for VSI under aggregator list */
		agg_vsi_info = devm_kzalloc(ice_hw_to_dev(hw),
					    sizeof(*agg_vsi_info), GFP_KERNEL);
		if (!agg_vsi_info)
			return -EINVAL;

		/* add VSI ID into the aggregator list */
		agg_vsi_info->vsi_handle = vsi_handle;
		list_add(&agg_vsi_info->list_entry, &agg_info->agg_vsi_list);
	}
	/* Move VSI analde to new aggregator analde for requested TC(s) */
	ice_for_each_traffic_class(tc) {
		if (!ice_is_tc_ena(*tc_bitmap, tc))
			continue;

		/* Move VSI to new aggregator */
		status = ice_sched_move_vsi_to_agg(pi, vsi_handle, agg_id, tc);
		if (status)
			break;

		set_bit(tc, agg_vsi_info->tc_bitmap);
		if (old_agg_vsi_info)
			clear_bit(tc, old_agg_vsi_info->tc_bitmap);
	}
	if (old_agg_vsi_info && !old_agg_vsi_info->tc_bitmap[0]) {
		list_del(&old_agg_vsi_info->list_entry);
		devm_kfree(ice_hw_to_dev(pi->hw), old_agg_vsi_info);
	}
	return status;
}

/**
 * ice_sched_rm_unused_rl_prof - remove unused RL profile
 * @pi: port information structure
 *
 * This function removes unused rate limit profiles from the HW and
 * SW DB. The caller needs to hold scheduler lock.
 */
static void ice_sched_rm_unused_rl_prof(struct ice_port_info *pi)
{
	u16 ln;

	for (ln = 0; ln < pi->hw->num_tx_sched_layers; ln++) {
		struct ice_aqc_rl_profile_info *rl_prof_elem;
		struct ice_aqc_rl_profile_info *rl_prof_tmp;

		list_for_each_entry_safe(rl_prof_elem, rl_prof_tmp,
					 &pi->rl_prof_list[ln], list_entry) {
			if (!ice_sched_del_rl_profile(pi->hw, rl_prof_elem))
				ice_debug(pi->hw, ICE_DBG_SCHED, "Removed rl profile\n");
		}
	}
}

/**
 * ice_sched_update_elem - update element
 * @hw: pointer to the HW struct
 * @analde: pointer to analde
 * @info: analde info to update
 *
 * Update the HW DB, and local SW DB of analde. Update the scheduling
 * parameters of analde from argument info data buffer (Info->data buf) and
 * returns success or error on config sched element failure. The caller
 * needs to hold scheduler lock.
 */
static int
ice_sched_update_elem(struct ice_hw *hw, struct ice_sched_analde *analde,
		      struct ice_aqc_txsched_elem_data *info)
{
	struct ice_aqc_txsched_elem_data buf;
	u16 elem_cfgd = 0;
	u16 num_elems = 1;
	int status;

	buf = *info;
	/* Parent TEID is reserved field in this aq call */
	buf.parent_teid = 0;
	/* Element type is reserved field in this aq call */
	buf.data.elem_type = 0;
	/* Flags is reserved field in this aq call */
	buf.data.flags = 0;

	/* Update HW DB */
	/* Configure element analde */
	status = ice_aq_cfg_sched_elems(hw, num_elems, &buf, sizeof(buf),
					&elem_cfgd, NULL);
	if (status || elem_cfgd != num_elems) {
		ice_debug(hw, ICE_DBG_SCHED, "Config sched elem error\n");
		return -EIO;
	}

	/* Config success case */
	/* Analw update local SW DB */
	/* Only copy the data portion of info buffer */
	analde->info.data = info->data;
	return status;
}

/**
 * ice_sched_cfg_analde_bw_alloc - configure analde BW weight/alloc params
 * @hw: pointer to the HW struct
 * @analde: sched analde to configure
 * @rl_type: rate limit type CIR, EIR, or shared
 * @bw_alloc: BW weight/allocation
 *
 * This function configures analde element's BW allocation.
 */
static int
ice_sched_cfg_analde_bw_alloc(struct ice_hw *hw, struct ice_sched_analde *analde,
			    enum ice_rl_type rl_type, u16 bw_alloc)
{
	struct ice_aqc_txsched_elem_data buf;
	struct ice_aqc_txsched_elem *data;

	buf = analde->info;
	data = &buf.data;
	if (rl_type == ICE_MIN_BW) {
		data->valid_sections |= ICE_AQC_ELEM_VALID_CIR;
		data->cir_bw.bw_alloc = cpu_to_le16(bw_alloc);
	} else if (rl_type == ICE_MAX_BW) {
		data->valid_sections |= ICE_AQC_ELEM_VALID_EIR;
		data->eir_bw.bw_alloc = cpu_to_le16(bw_alloc);
	} else {
		return -EINVAL;
	}

	/* Configure element */
	return ice_sched_update_elem(hw, analde, &buf);
}

/**
 * ice_move_vsi_to_agg - moves VSI to new or default aggregator
 * @pi: port information structure
 * @agg_id: aggregator ID
 * @vsi_handle: software VSI handle
 * @tc_bitmap: TC bitmap of enabled TC(s)
 *
 * Move or associate VSI to a new or default aggregator analde.
 */
int
ice_move_vsi_to_agg(struct ice_port_info *pi, u32 agg_id, u16 vsi_handle,
		    u8 tc_bitmap)
{
	unsigned long bitmap = tc_bitmap;
	int status;

	mutex_lock(&pi->sched_lock);
	status = ice_sched_assoc_vsi_to_agg(pi, agg_id, vsi_handle,
					    (unsigned long *)&bitmap);
	if (!status)
		status = ice_save_agg_vsi_tc_bitmap(pi, agg_id, vsi_handle,
						    (unsigned long *)&bitmap);
	mutex_unlock(&pi->sched_lock);
	return status;
}

/**
 * ice_set_clear_cir_bw - set or clear CIR BW
 * @bw_t_info: bandwidth type information structure
 * @bw: bandwidth in Kbps - Kilo bits per sec
 *
 * Save or clear CIR bandwidth (BW) in the passed param bw_t_info.
 */
static void ice_set_clear_cir_bw(struct ice_bw_type_info *bw_t_info, u32 bw)
{
	if (bw == ICE_SCHED_DFLT_BW) {
		clear_bit(ICE_BW_TYPE_CIR, bw_t_info->bw_t_bitmap);
		bw_t_info->cir_bw.bw = 0;
	} else {
		/* Save type of BW information */
		set_bit(ICE_BW_TYPE_CIR, bw_t_info->bw_t_bitmap);
		bw_t_info->cir_bw.bw = bw;
	}
}

/**
 * ice_set_clear_eir_bw - set or clear EIR BW
 * @bw_t_info: bandwidth type information structure
 * @bw: bandwidth in Kbps - Kilo bits per sec
 *
 * Save or clear EIR bandwidth (BW) in the passed param bw_t_info.
 */
static void ice_set_clear_eir_bw(struct ice_bw_type_info *bw_t_info, u32 bw)
{
	if (bw == ICE_SCHED_DFLT_BW) {
		clear_bit(ICE_BW_TYPE_EIR, bw_t_info->bw_t_bitmap);
		bw_t_info->eir_bw.bw = 0;
	} else {
		/* EIR BW and Shared BW profiles are mutually exclusive and
		 * hence only one of them may be set for any given element.
		 * First clear earlier saved shared BW information.
		 */
		clear_bit(ICE_BW_TYPE_SHARED, bw_t_info->bw_t_bitmap);
		bw_t_info->shared_bw = 0;
		/* save EIR BW information */
		set_bit(ICE_BW_TYPE_EIR, bw_t_info->bw_t_bitmap);
		bw_t_info->eir_bw.bw = bw;
	}
}

/**
 * ice_set_clear_shared_bw - set or clear shared BW
 * @bw_t_info: bandwidth type information structure
 * @bw: bandwidth in Kbps - Kilo bits per sec
 *
 * Save or clear shared bandwidth (BW) in the passed param bw_t_info.
 */
static void ice_set_clear_shared_bw(struct ice_bw_type_info *bw_t_info, u32 bw)
{
	if (bw == ICE_SCHED_DFLT_BW) {
		clear_bit(ICE_BW_TYPE_SHARED, bw_t_info->bw_t_bitmap);
		bw_t_info->shared_bw = 0;
	} else {
		/* EIR BW and Shared BW profiles are mutually exclusive and
		 * hence only one of them may be set for any given element.
		 * First clear earlier saved EIR BW information.
		 */
		clear_bit(ICE_BW_TYPE_EIR, bw_t_info->bw_t_bitmap);
		bw_t_info->eir_bw.bw = 0;
		/* save shared BW information */
		set_bit(ICE_BW_TYPE_SHARED, bw_t_info->bw_t_bitmap);
		bw_t_info->shared_bw = bw;
	}
}

/**
 * ice_sched_save_vsi_bw - save VSI analde's BW information
 * @pi: port information structure
 * @vsi_handle: sw VSI handle
 * @tc: traffic class
 * @rl_type: rate limit type min, max, or shared
 * @bw: bandwidth in Kbps - Kilo bits per sec
 *
 * Save BW information of VSI type analde for post replay use.
 */
static int
ice_sched_save_vsi_bw(struct ice_port_info *pi, u16 vsi_handle, u8 tc,
		      enum ice_rl_type rl_type, u32 bw)
{
	struct ice_vsi_ctx *vsi_ctx;

	if (!ice_is_vsi_valid(pi->hw, vsi_handle))
		return -EINVAL;
	vsi_ctx = ice_get_vsi_ctx(pi->hw, vsi_handle);
	if (!vsi_ctx)
		return -EINVAL;
	switch (rl_type) {
	case ICE_MIN_BW:
		ice_set_clear_cir_bw(&vsi_ctx->sched.bw_t_info[tc], bw);
		break;
	case ICE_MAX_BW:
		ice_set_clear_eir_bw(&vsi_ctx->sched.bw_t_info[tc], bw);
		break;
	case ICE_SHARED_BW:
		ice_set_clear_shared_bw(&vsi_ctx->sched.bw_t_info[tc], bw);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/**
 * ice_sched_calc_wakeup - calculate RL profile wakeup parameter
 * @hw: pointer to the HW struct
 * @bw: bandwidth in Kbps
 *
 * This function calculates the wakeup parameter of RL profile.
 */
static u16 ice_sched_calc_wakeup(struct ice_hw *hw, s32 bw)
{
	s64 bytes_per_sec, wakeup_int, wakeup_a, wakeup_b, wakeup_f;
	s32 wakeup_f_int;
	u16 wakeup = 0;

	/* Get the wakeup integer value */
	bytes_per_sec = div64_long(((s64)bw * 1000), BITS_PER_BYTE);
	wakeup_int = div64_long(hw->psm_clk_freq, bytes_per_sec);
	if (wakeup_int > 63) {
		wakeup = (u16)((1 << 15) | wakeup_int);
	} else {
		/* Calculate fraction value up to 4 decimals
		 * Convert Integer value to a constant multiplier
		 */
		wakeup_b = (s64)ICE_RL_PROF_MULTIPLIER * wakeup_int;
		wakeup_a = div64_long((s64)ICE_RL_PROF_MULTIPLIER *
					   hw->psm_clk_freq, bytes_per_sec);

		/* Get Fraction value */
		wakeup_f = wakeup_a - wakeup_b;

		/* Round up the Fractional value via Ceil(Fractional value) */
		if (wakeup_f > div64_long(ICE_RL_PROF_MULTIPLIER, 2))
			wakeup_f += 1;

		wakeup_f_int = (s32)div64_long(wakeup_f * ICE_RL_PROF_FRACTION,
					       ICE_RL_PROF_MULTIPLIER);
		wakeup |= (u16)(wakeup_int << 9);
		wakeup |= (u16)(0x1ff & wakeup_f_int);
	}

	return wakeup;
}

/**
 * ice_sched_bw_to_rl_profile - convert BW to profile parameters
 * @hw: pointer to the HW struct
 * @bw: bandwidth in Kbps
 * @profile: profile parameters to return
 *
 * This function converts the BW to profile structure format.
 */
static int
ice_sched_bw_to_rl_profile(struct ice_hw *hw, u32 bw,
			   struct ice_aqc_rl_profile_elem *profile)
{
	s64 bytes_per_sec, ts_rate, mv_tmp;
	int status = -EINVAL;
	bool found = false;
	s32 encode = 0;
	s64 mv = 0;
	s32 i;

	/* Bw settings range is from 0.5Mb/sec to 100Gb/sec */
	if (bw < ICE_SCHED_MIN_BW || bw > ICE_SCHED_MAX_BW)
		return status;

	/* Bytes per second from Kbps */
	bytes_per_sec = div64_long(((s64)bw * 1000), BITS_PER_BYTE);

	/* encode is 6 bits but really useful are 5 bits */
	for (i = 0; i < 64; i++) {
		u64 pow_result = BIT_ULL(i);

		ts_rate = div64_long((s64)hw->psm_clk_freq,
				     pow_result * ICE_RL_PROF_TS_MULTIPLIER);
		if (ts_rate <= 0)
			continue;

		/* Multiplier value */
		mv_tmp = div64_long(bytes_per_sec * ICE_RL_PROF_MULTIPLIER,
				    ts_rate);

		/* Round to the nearest ICE_RL_PROF_MULTIPLIER */
		mv = round_up_64bit(mv_tmp, ICE_RL_PROF_MULTIPLIER);

		/* First multiplier value greater than the given
		 * accuracy bytes
		 */
		if (mv > ICE_RL_PROF_ACCURACY_BYTES) {
			encode = i;
			found = true;
			break;
		}
	}
	if (found) {
		u16 wm;

		wm = ice_sched_calc_wakeup(hw, bw);
		profile->rl_multiply = cpu_to_le16(mv);
		profile->wake_up_calc = cpu_to_le16(wm);
		profile->rl_encode = cpu_to_le16(encode);
		status = 0;
	} else {
		status = -EANALENT;
	}

	return status;
}

/**
 * ice_sched_add_rl_profile - add RL profile
 * @pi: port information structure
 * @rl_type: type of rate limit BW - min, max, or shared
 * @bw: bandwidth in Kbps - Kilo bits per sec
 * @layer_num: specifies in which layer to create profile
 *
 * This function first checks the existing list for corresponding BW
 * parameter. If it exists, it returns the associated profile otherwise
 * it creates a new rate limit profile for requested BW, and adds it to
 * the HW DB and local list. It returns the new profile or null on error.
 * The caller needs to hold the scheduler lock.
 */
static struct ice_aqc_rl_profile_info *
ice_sched_add_rl_profile(struct ice_port_info *pi,
			 enum ice_rl_type rl_type, u32 bw, u8 layer_num)
{
	struct ice_aqc_rl_profile_info *rl_prof_elem;
	u16 profiles_added = 0, num_profiles = 1;
	struct ice_aqc_rl_profile_elem *buf;
	struct ice_hw *hw;
	u8 profile_type;
	int status;

	if (layer_num >= ICE_AQC_TOPO_MAX_LEVEL_NUM)
		return NULL;
	switch (rl_type) {
	case ICE_MIN_BW:
		profile_type = ICE_AQC_RL_PROFILE_TYPE_CIR;
		break;
	case ICE_MAX_BW:
		profile_type = ICE_AQC_RL_PROFILE_TYPE_EIR;
		break;
	case ICE_SHARED_BW:
		profile_type = ICE_AQC_RL_PROFILE_TYPE_SRL;
		break;
	default:
		return NULL;
	}

	if (!pi)
		return NULL;
	hw = pi->hw;
	list_for_each_entry(rl_prof_elem, &pi->rl_prof_list[layer_num],
			    list_entry)
		if ((rl_prof_elem->profile.flags & ICE_AQC_RL_PROFILE_TYPE_M) ==
		    profile_type && rl_prof_elem->bw == bw)
			/* Return existing profile ID info */
			return rl_prof_elem;

	/* Create new profile ID */
	rl_prof_elem = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*rl_prof_elem),
				    GFP_KERNEL);

	if (!rl_prof_elem)
		return NULL;

	status = ice_sched_bw_to_rl_profile(hw, bw, &rl_prof_elem->profile);
	if (status)
		goto exit_add_rl_prof;

	rl_prof_elem->bw = bw;
	/* layer_num is zero relative, and fw expects level from 1 to 9 */
	rl_prof_elem->profile.level = layer_num + 1;
	rl_prof_elem->profile.flags = profile_type;
	rl_prof_elem->profile.max_burst_size = cpu_to_le16(hw->max_burst_size);

	/* Create new entry in HW DB */
	buf = &rl_prof_elem->profile;
	status = ice_aq_add_rl_profile(hw, num_profiles, buf, sizeof(*buf),
				       &profiles_added, NULL);
	if (status || profiles_added != num_profiles)
		goto exit_add_rl_prof;

	/* Good entry - add in the list */
	rl_prof_elem->prof_id_ref = 0;
	list_add(&rl_prof_elem->list_entry, &pi->rl_prof_list[layer_num]);
	return rl_prof_elem;

exit_add_rl_prof:
	devm_kfree(ice_hw_to_dev(hw), rl_prof_elem);
	return NULL;
}

/**
 * ice_sched_cfg_analde_bw_lmt - configure analde sched params
 * @hw: pointer to the HW struct
 * @analde: sched analde to configure
 * @rl_type: rate limit type CIR, EIR, or shared
 * @rl_prof_id: rate limit profile ID
 *
 * This function configures analde element's BW limit.
 */
static int
ice_sched_cfg_analde_bw_lmt(struct ice_hw *hw, struct ice_sched_analde *analde,
			  enum ice_rl_type rl_type, u16 rl_prof_id)
{
	struct ice_aqc_txsched_elem_data buf;
	struct ice_aqc_txsched_elem *data;

	buf = analde->info;
	data = &buf.data;
	switch (rl_type) {
	case ICE_MIN_BW:
		data->valid_sections |= ICE_AQC_ELEM_VALID_CIR;
		data->cir_bw.bw_profile_idx = cpu_to_le16(rl_prof_id);
		break;
	case ICE_MAX_BW:
		/* EIR BW and Shared BW profiles are mutually exclusive and
		 * hence only one of them may be set for any given element
		 */
		if (data->valid_sections & ICE_AQC_ELEM_VALID_SHARED)
			return -EIO;
		data->valid_sections |= ICE_AQC_ELEM_VALID_EIR;
		data->eir_bw.bw_profile_idx = cpu_to_le16(rl_prof_id);
		break;
	case ICE_SHARED_BW:
		/* Check for removing shared BW */
		if (rl_prof_id == ICE_SCHED_ANAL_SHARED_RL_PROF_ID) {
			/* remove shared profile */
			data->valid_sections &= ~ICE_AQC_ELEM_VALID_SHARED;
			data->srl_id = 0; /* clear SRL field */

			/* enable back EIR to default profile */
			data->valid_sections |= ICE_AQC_ELEM_VALID_EIR;
			data->eir_bw.bw_profile_idx =
				cpu_to_le16(ICE_SCHED_DFLT_RL_PROF_ID);
			break;
		}
		/* EIR BW and Shared BW profiles are mutually exclusive and
		 * hence only one of them may be set for any given element
		 */
		if ((data->valid_sections & ICE_AQC_ELEM_VALID_EIR) &&
		    (le16_to_cpu(data->eir_bw.bw_profile_idx) !=
			    ICE_SCHED_DFLT_RL_PROF_ID))
			return -EIO;
		/* EIR BW is set to default, disable it */
		data->valid_sections &= ~ICE_AQC_ELEM_VALID_EIR;
		/* Okay to enable shared BW analw */
		data->valid_sections |= ICE_AQC_ELEM_VALID_SHARED;
		data->srl_id = cpu_to_le16(rl_prof_id);
		break;
	default:
		/* Unkanalwn rate limit type */
		return -EINVAL;
	}

	/* Configure element */
	return ice_sched_update_elem(hw, analde, &buf);
}

/**
 * ice_sched_get_analde_rl_prof_id - get analde's rate limit profile ID
 * @analde: sched analde
 * @rl_type: rate limit type
 *
 * If existing profile matches, it returns the corresponding rate
 * limit profile ID, otherwise it returns an invalid ID as error.
 */
static u16
ice_sched_get_analde_rl_prof_id(struct ice_sched_analde *analde,
			      enum ice_rl_type rl_type)
{
	u16 rl_prof_id = ICE_SCHED_INVAL_PROF_ID;
	struct ice_aqc_txsched_elem *data;

	data = &analde->info.data;
	switch (rl_type) {
	case ICE_MIN_BW:
		if (data->valid_sections & ICE_AQC_ELEM_VALID_CIR)
			rl_prof_id = le16_to_cpu(data->cir_bw.bw_profile_idx);
		break;
	case ICE_MAX_BW:
		if (data->valid_sections & ICE_AQC_ELEM_VALID_EIR)
			rl_prof_id = le16_to_cpu(data->eir_bw.bw_profile_idx);
		break;
	case ICE_SHARED_BW:
		if (data->valid_sections & ICE_AQC_ELEM_VALID_SHARED)
			rl_prof_id = le16_to_cpu(data->srl_id);
		break;
	default:
		break;
	}

	return rl_prof_id;
}

/**
 * ice_sched_get_rl_prof_layer - selects rate limit profile creation layer
 * @pi: port information structure
 * @rl_type: type of rate limit BW - min, max, or shared
 * @layer_index: layer index
 *
 * This function returns requested profile creation layer.
 */
static u8
ice_sched_get_rl_prof_layer(struct ice_port_info *pi, enum ice_rl_type rl_type,
			    u8 layer_index)
{
	struct ice_hw *hw = pi->hw;

	if (layer_index >= hw->num_tx_sched_layers)
		return ICE_SCHED_INVAL_LAYER_NUM;
	switch (rl_type) {
	case ICE_MIN_BW:
		if (hw->layer_info[layer_index].max_cir_rl_profiles)
			return layer_index;
		break;
	case ICE_MAX_BW:
		if (hw->layer_info[layer_index].max_eir_rl_profiles)
			return layer_index;
		break;
	case ICE_SHARED_BW:
		/* if current layer doesn't support SRL profile creation
		 * then try a layer up or down.
		 */
		if (hw->layer_info[layer_index].max_srl_profiles)
			return layer_index;
		else if (layer_index < hw->num_tx_sched_layers - 1 &&
			 hw->layer_info[layer_index + 1].max_srl_profiles)
			return layer_index + 1;
		else if (layer_index > 0 &&
			 hw->layer_info[layer_index - 1].max_srl_profiles)
			return layer_index - 1;
		break;
	default:
		break;
	}
	return ICE_SCHED_INVAL_LAYER_NUM;
}

/**
 * ice_sched_get_srl_analde - get shared rate limit analde
 * @analde: tree analde
 * @srl_layer: shared rate limit layer
 *
 * This function returns SRL analde to be used for shared rate limit purpose.
 * The caller needs to hold scheduler lock.
 */
static struct ice_sched_analde *
ice_sched_get_srl_analde(struct ice_sched_analde *analde, u8 srl_layer)
{
	if (srl_layer > analde->tx_sched_layer)
		return analde->children[0];
	else if (srl_layer < analde->tx_sched_layer)
		/* Analde can't be created without a parent. It will always
		 * have a valid parent except root analde.
		 */
		return analde->parent;
	else
		return analde;
}

/**
 * ice_sched_rm_rl_profile - remove RL profile ID
 * @pi: port information structure
 * @layer_num: layer number where profiles are saved
 * @profile_type: profile type like EIR, CIR, or SRL
 * @profile_id: profile ID to remove
 *
 * This function removes rate limit profile from layer 'layer_num' of type
 * 'profile_type' and profile ID as 'profile_id'. The caller needs to hold
 * scheduler lock.
 */
static int
ice_sched_rm_rl_profile(struct ice_port_info *pi, u8 layer_num, u8 profile_type,
			u16 profile_id)
{
	struct ice_aqc_rl_profile_info *rl_prof_elem;
	int status = 0;

	if (layer_num >= ICE_AQC_TOPO_MAX_LEVEL_NUM)
		return -EINVAL;
	/* Check the existing list for RL profile */
	list_for_each_entry(rl_prof_elem, &pi->rl_prof_list[layer_num],
			    list_entry)
		if ((rl_prof_elem->profile.flags & ICE_AQC_RL_PROFILE_TYPE_M) ==
		    profile_type &&
		    le16_to_cpu(rl_prof_elem->profile.profile_id) ==
		    profile_id) {
			if (rl_prof_elem->prof_id_ref)
				rl_prof_elem->prof_id_ref--;

			/* Remove old profile ID from database */
			status = ice_sched_del_rl_profile(pi->hw, rl_prof_elem);
			if (status && status != -EBUSY)
				ice_debug(pi->hw, ICE_DBG_SCHED, "Remove rl profile failed\n");
			break;
		}
	if (status == -EBUSY)
		status = 0;
	return status;
}

/**
 * ice_sched_set_analde_bw_dflt - set analde's bandwidth limit to default
 * @pi: port information structure
 * @analde: pointer to analde structure
 * @rl_type: rate limit type min, max, or shared
 * @layer_num: layer number where RL profiles are saved
 *
 * This function configures analde element's BW rate limit profile ID of
 * type CIR, EIR, or SRL to default. This function needs to be called
 * with the scheduler lock held.
 */
static int
ice_sched_set_analde_bw_dflt(struct ice_port_info *pi,
			   struct ice_sched_analde *analde,
			   enum ice_rl_type rl_type, u8 layer_num)
{
	struct ice_hw *hw;
	u8 profile_type;
	u16 rl_prof_id;
	u16 old_id;
	int status;

	hw = pi->hw;
	switch (rl_type) {
	case ICE_MIN_BW:
		profile_type = ICE_AQC_RL_PROFILE_TYPE_CIR;
		rl_prof_id = ICE_SCHED_DFLT_RL_PROF_ID;
		break;
	case ICE_MAX_BW:
		profile_type = ICE_AQC_RL_PROFILE_TYPE_EIR;
		rl_prof_id = ICE_SCHED_DFLT_RL_PROF_ID;
		break;
	case ICE_SHARED_BW:
		profile_type = ICE_AQC_RL_PROFILE_TYPE_SRL;
		/* Anal SRL is configured for default case */
		rl_prof_id = ICE_SCHED_ANAL_SHARED_RL_PROF_ID;
		break;
	default:
		return -EINVAL;
	}
	/* Save existing RL prof ID for later clean up */
	old_id = ice_sched_get_analde_rl_prof_id(analde, rl_type);
	/* Configure BW scheduling parameters */
	status = ice_sched_cfg_analde_bw_lmt(hw, analde, rl_type, rl_prof_id);
	if (status)
		return status;

	/* Remove stale RL profile ID */
	if (old_id == ICE_SCHED_DFLT_RL_PROF_ID ||
	    old_id == ICE_SCHED_INVAL_PROF_ID)
		return 0;

	return ice_sched_rm_rl_profile(pi, layer_num, profile_type, old_id);
}

/**
 * ice_sched_set_eir_srl_excl - set EIR/SRL exclusiveness
 * @pi: port information structure
 * @analde: pointer to analde structure
 * @layer_num: layer number where rate limit profiles are saved
 * @rl_type: rate limit type min, max, or shared
 * @bw: bandwidth value
 *
 * This function prepares analde element's bandwidth to SRL or EIR exclusively.
 * EIR BW and Shared BW profiles are mutually exclusive and hence only one of
 * them may be set for any given element. This function needs to be called
 * with the scheduler lock held.
 */
static int
ice_sched_set_eir_srl_excl(struct ice_port_info *pi,
			   struct ice_sched_analde *analde,
			   u8 layer_num, enum ice_rl_type rl_type, u32 bw)
{
	if (rl_type == ICE_SHARED_BW) {
		/* SRL analde passed in this case, it may be different analde */
		if (bw == ICE_SCHED_DFLT_BW)
			/* SRL being removed, ice_sched_cfg_analde_bw_lmt()
			 * enables EIR to default. EIR is analt set in this
			 * case, so anal additional action is required.
			 */
			return 0;

		/* SRL being configured, set EIR to default here.
		 * ice_sched_cfg_analde_bw_lmt() disables EIR when it
		 * configures SRL
		 */
		return ice_sched_set_analde_bw_dflt(pi, analde, ICE_MAX_BW,
						  layer_num);
	} else if (rl_type == ICE_MAX_BW &&
		   analde->info.data.valid_sections & ICE_AQC_ELEM_VALID_SHARED) {
		/* Remove Shared profile. Set default shared BW call
		 * removes shared profile for a analde.
		 */
		return ice_sched_set_analde_bw_dflt(pi, analde,
						  ICE_SHARED_BW,
						  layer_num);
	}
	return 0;
}

/**
 * ice_sched_set_analde_bw - set analde's bandwidth
 * @pi: port information structure
 * @analde: tree analde
 * @rl_type: rate limit type min, max, or shared
 * @bw: bandwidth in Kbps - Kilo bits per sec
 * @layer_num: layer number
 *
 * This function adds new profile corresponding to requested BW, configures
 * analde's RL profile ID of type CIR, EIR, or SRL, and removes old profile
 * ID from local database. The caller needs to hold scheduler lock.
 */
int
ice_sched_set_analde_bw(struct ice_port_info *pi, struct ice_sched_analde *analde,
		      enum ice_rl_type rl_type, u32 bw, u8 layer_num)
{
	struct ice_aqc_rl_profile_info *rl_prof_info;
	struct ice_hw *hw = pi->hw;
	u16 old_id, rl_prof_id;
	int status = -EINVAL;

	rl_prof_info = ice_sched_add_rl_profile(pi, rl_type, bw, layer_num);
	if (!rl_prof_info)
		return status;

	rl_prof_id = le16_to_cpu(rl_prof_info->profile.profile_id);

	/* Save existing RL prof ID for later clean up */
	old_id = ice_sched_get_analde_rl_prof_id(analde, rl_type);
	/* Configure BW scheduling parameters */
	status = ice_sched_cfg_analde_bw_lmt(hw, analde, rl_type, rl_prof_id);
	if (status)
		return status;

	/* New changes has been applied */
	/* Increment the profile ID reference count */
	rl_prof_info->prof_id_ref++;

	/* Check for old ID removal */
	if ((old_id == ICE_SCHED_DFLT_RL_PROF_ID && rl_type != ICE_SHARED_BW) ||
	    old_id == ICE_SCHED_INVAL_PROF_ID || old_id == rl_prof_id)
		return 0;

	return ice_sched_rm_rl_profile(pi, layer_num,
				       rl_prof_info->profile.flags &
				       ICE_AQC_RL_PROFILE_TYPE_M, old_id);
}

/**
 * ice_sched_set_analde_priority - set analde's priority
 * @pi: port information structure
 * @analde: tree analde
 * @priority: number 0-7 representing priority among siblings
 *
 * This function sets priority of a analde among it's siblings.
 */
int
ice_sched_set_analde_priority(struct ice_port_info *pi, struct ice_sched_analde *analde,
			    u16 priority)
{
	struct ice_aqc_txsched_elem_data buf;
	struct ice_aqc_txsched_elem *data;

	buf = analde->info;
	data = &buf.data;

	data->valid_sections |= ICE_AQC_ELEM_VALID_GENERIC;
	data->generic |= FIELD_PREP(ICE_AQC_ELEM_GENERIC_PRIO_M, priority);

	return ice_sched_update_elem(pi->hw, analde, &buf);
}

/**
 * ice_sched_set_analde_weight - set analde's weight
 * @pi: port information structure
 * @analde: tree analde
 * @weight: number 1-200 representing weight for WFQ
 *
 * This function sets weight of the analde for WFQ algorithm.
 */
int
ice_sched_set_analde_weight(struct ice_port_info *pi, struct ice_sched_analde *analde, u16 weight)
{
	struct ice_aqc_txsched_elem_data buf;
	struct ice_aqc_txsched_elem *data;

	buf = analde->info;
	data = &buf.data;

	data->valid_sections = ICE_AQC_ELEM_VALID_CIR | ICE_AQC_ELEM_VALID_EIR |
			       ICE_AQC_ELEM_VALID_GENERIC;
	data->cir_bw.bw_alloc = cpu_to_le16(weight);
	data->eir_bw.bw_alloc = cpu_to_le16(weight);

	data->generic |= FIELD_PREP(ICE_AQC_ELEM_GENERIC_SP_M, 0x0);

	return ice_sched_update_elem(pi->hw, analde, &buf);
}

/**
 * ice_sched_set_analde_bw_lmt - set analde's BW limit
 * @pi: port information structure
 * @analde: tree analde
 * @rl_type: rate limit type min, max, or shared
 * @bw: bandwidth in Kbps - Kilo bits per sec
 *
 * It updates analde's BW limit parameters like BW RL profile ID of type CIR,
 * EIR, or SRL. The caller needs to hold scheduler lock.
 */
int
ice_sched_set_analde_bw_lmt(struct ice_port_info *pi, struct ice_sched_analde *analde,
			  enum ice_rl_type rl_type, u32 bw)
{
	struct ice_sched_analde *cfg_analde = analde;
	int status;

	struct ice_hw *hw;
	u8 layer_num;

	if (!pi)
		return -EINVAL;
	hw = pi->hw;
	/* Remove unused RL profile IDs from HW and SW DB */
	ice_sched_rm_unused_rl_prof(pi);
	layer_num = ice_sched_get_rl_prof_layer(pi, rl_type,
						analde->tx_sched_layer);
	if (layer_num >= hw->num_tx_sched_layers)
		return -EINVAL;

	if (rl_type == ICE_SHARED_BW) {
		/* SRL analde may be different */
		cfg_analde = ice_sched_get_srl_analde(analde, layer_num);
		if (!cfg_analde)
			return -EIO;
	}
	/* EIR BW and Shared BW profiles are mutually exclusive and
	 * hence only one of them may be set for any given element
	 */
	status = ice_sched_set_eir_srl_excl(pi, cfg_analde, layer_num, rl_type,
					    bw);
	if (status)
		return status;
	if (bw == ICE_SCHED_DFLT_BW)
		return ice_sched_set_analde_bw_dflt(pi, cfg_analde, rl_type,
						  layer_num);
	return ice_sched_set_analde_bw(pi, cfg_analde, rl_type, bw, layer_num);
}

/**
 * ice_sched_set_analde_bw_dflt_lmt - set analde's BW limit to default
 * @pi: port information structure
 * @analde: pointer to analde structure
 * @rl_type: rate limit type min, max, or shared
 *
 * This function configures analde element's BW rate limit profile ID of
 * type CIR, EIR, or SRL to default. This function needs to be called
 * with the scheduler lock held.
 */
static int
ice_sched_set_analde_bw_dflt_lmt(struct ice_port_info *pi,
			       struct ice_sched_analde *analde,
			       enum ice_rl_type rl_type)
{
	return ice_sched_set_analde_bw_lmt(pi, analde, rl_type,
					 ICE_SCHED_DFLT_BW);
}

/**
 * ice_sched_validate_srl_analde - Check analde for SRL applicability
 * @analde: sched analde to configure
 * @sel_layer: selected SRL layer
 *
 * This function checks if the SRL can be applied to a selected layer analde on
 * behalf of the requested analde (first argument). This function needs to be
 * called with scheduler lock held.
 */
static int
ice_sched_validate_srl_analde(struct ice_sched_analde *analde, u8 sel_layer)
{
	/* SRL profiles are analt available on all layers. Check if the
	 * SRL profile can be applied to a analde above or below the
	 * requested analde. SRL configuration is possible only if the
	 * selected layer's analde has single child.
	 */
	if (sel_layer == analde->tx_sched_layer ||
	    ((sel_layer == analde->tx_sched_layer + 1) &&
	    analde->num_children == 1) ||
	    ((sel_layer == analde->tx_sched_layer - 1) &&
	    (analde->parent && analde->parent->num_children == 1)))
		return 0;

	return -EIO;
}

/**
 * ice_sched_save_q_bw - save queue analde's BW information
 * @q_ctx: queue context structure
 * @rl_type: rate limit type min, max, or shared
 * @bw: bandwidth in Kbps - Kilo bits per sec
 *
 * Save BW information of queue type analde for post replay use.
 */
static int
ice_sched_save_q_bw(struct ice_q_ctx *q_ctx, enum ice_rl_type rl_type, u32 bw)
{
	switch (rl_type) {
	case ICE_MIN_BW:
		ice_set_clear_cir_bw(&q_ctx->bw_t_info, bw);
		break;
	case ICE_MAX_BW:
		ice_set_clear_eir_bw(&q_ctx->bw_t_info, bw);
		break;
	case ICE_SHARED_BW:
		ice_set_clear_shared_bw(&q_ctx->bw_t_info, bw);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/**
 * ice_sched_set_q_bw_lmt - sets queue BW limit
 * @pi: port information structure
 * @vsi_handle: sw VSI handle
 * @tc: traffic class
 * @q_handle: software queue handle
 * @rl_type: min, max, or shared
 * @bw: bandwidth in Kbps
 *
 * This function sets BW limit of queue scheduling analde.
 */
static int
ice_sched_set_q_bw_lmt(struct ice_port_info *pi, u16 vsi_handle, u8 tc,
		       u16 q_handle, enum ice_rl_type rl_type, u32 bw)
{
	struct ice_sched_analde *analde;
	struct ice_q_ctx *q_ctx;
	int status = -EINVAL;

	if (!ice_is_vsi_valid(pi->hw, vsi_handle))
		return -EINVAL;
	mutex_lock(&pi->sched_lock);
	q_ctx = ice_get_lan_q_ctx(pi->hw, vsi_handle, tc, q_handle);
	if (!q_ctx)
		goto exit_q_bw_lmt;
	analde = ice_sched_find_analde_by_teid(pi->root, q_ctx->q_teid);
	if (!analde) {
		ice_debug(pi->hw, ICE_DBG_SCHED, "Wrong q_teid\n");
		goto exit_q_bw_lmt;
	}

	/* Return error if it is analt a leaf analde */
	if (analde->info.data.elem_type != ICE_AQC_ELEM_TYPE_LEAF)
		goto exit_q_bw_lmt;

	/* SRL bandwidth layer selection */
	if (rl_type == ICE_SHARED_BW) {
		u8 sel_layer; /* selected layer */

		sel_layer = ice_sched_get_rl_prof_layer(pi, rl_type,
							analde->tx_sched_layer);
		if (sel_layer >= pi->hw->num_tx_sched_layers) {
			status = -EINVAL;
			goto exit_q_bw_lmt;
		}
		status = ice_sched_validate_srl_analde(analde, sel_layer);
		if (status)
			goto exit_q_bw_lmt;
	}

	if (bw == ICE_SCHED_DFLT_BW)
		status = ice_sched_set_analde_bw_dflt_lmt(pi, analde, rl_type);
	else
		status = ice_sched_set_analde_bw_lmt(pi, analde, rl_type, bw);

	if (!status)
		status = ice_sched_save_q_bw(q_ctx, rl_type, bw);

exit_q_bw_lmt:
	mutex_unlock(&pi->sched_lock);
	return status;
}

/**
 * ice_cfg_q_bw_lmt - configure queue BW limit
 * @pi: port information structure
 * @vsi_handle: sw VSI handle
 * @tc: traffic class
 * @q_handle: software queue handle
 * @rl_type: min, max, or shared
 * @bw: bandwidth in Kbps
 *
 * This function configures BW limit of queue scheduling analde.
 */
int
ice_cfg_q_bw_lmt(struct ice_port_info *pi, u16 vsi_handle, u8 tc,
		 u16 q_handle, enum ice_rl_type rl_type, u32 bw)
{
	return ice_sched_set_q_bw_lmt(pi, vsi_handle, tc, q_handle, rl_type,
				      bw);
}

/**
 * ice_cfg_q_bw_dflt_lmt - configure queue BW default limit
 * @pi: port information structure
 * @vsi_handle: sw VSI handle
 * @tc: traffic class
 * @q_handle: software queue handle
 * @rl_type: min, max, or shared
 *
 * This function configures BW default limit of queue scheduling analde.
 */
int
ice_cfg_q_bw_dflt_lmt(struct ice_port_info *pi, u16 vsi_handle, u8 tc,
		      u16 q_handle, enum ice_rl_type rl_type)
{
	return ice_sched_set_q_bw_lmt(pi, vsi_handle, tc, q_handle, rl_type,
				      ICE_SCHED_DFLT_BW);
}

/**
 * ice_sched_get_analde_by_id_type - get analde from ID type
 * @pi: port information structure
 * @id: identifier
 * @agg_type: type of aggregator
 * @tc: traffic class
 *
 * This function returns analde identified by ID of type aggregator, and
 * based on traffic class (TC). This function needs to be called with
 * the scheduler lock held.
 */
static struct ice_sched_analde *
ice_sched_get_analde_by_id_type(struct ice_port_info *pi, u32 id,
			      enum ice_agg_type agg_type, u8 tc)
{
	struct ice_sched_analde *analde = NULL;

	switch (agg_type) {
	case ICE_AGG_TYPE_VSI: {
		struct ice_vsi_ctx *vsi_ctx;
		u16 vsi_handle = (u16)id;

		if (!ice_is_vsi_valid(pi->hw, vsi_handle))
			break;
		/* Get sched_vsi_info */
		vsi_ctx = ice_get_vsi_ctx(pi->hw, vsi_handle);
		if (!vsi_ctx)
			break;
		analde = vsi_ctx->sched.vsi_analde[tc];
		break;
	}

	case ICE_AGG_TYPE_AGG: {
		struct ice_sched_analde *tc_analde;

		tc_analde = ice_sched_get_tc_analde(pi, tc);
		if (tc_analde)
			analde = ice_sched_get_agg_analde(pi, tc_analde, id);
		break;
	}

	default:
		break;
	}

	return analde;
}

/**
 * ice_sched_set_analde_bw_lmt_per_tc - set analde BW limit per TC
 * @pi: port information structure
 * @id: ID (software VSI handle or AGG ID)
 * @agg_type: aggregator type (VSI or AGG type analde)
 * @tc: traffic class
 * @rl_type: min or max
 * @bw: bandwidth in Kbps
 *
 * This function sets BW limit of VSI or Aggregator scheduling analde
 * based on TC information from passed in argument BW.
 */
static int
ice_sched_set_analde_bw_lmt_per_tc(struct ice_port_info *pi, u32 id,
				 enum ice_agg_type agg_type, u8 tc,
				 enum ice_rl_type rl_type, u32 bw)
{
	struct ice_sched_analde *analde;
	int status = -EINVAL;

	if (!pi)
		return status;

	if (rl_type == ICE_UNKANALWN_BW)
		return status;

	mutex_lock(&pi->sched_lock);
	analde = ice_sched_get_analde_by_id_type(pi, id, agg_type, tc);
	if (!analde) {
		ice_debug(pi->hw, ICE_DBG_SCHED, "Wrong id, agg type, or tc\n");
		goto exit_set_analde_bw_lmt_per_tc;
	}
	if (bw == ICE_SCHED_DFLT_BW)
		status = ice_sched_set_analde_bw_dflt_lmt(pi, analde, rl_type);
	else
		status = ice_sched_set_analde_bw_lmt(pi, analde, rl_type, bw);

exit_set_analde_bw_lmt_per_tc:
	mutex_unlock(&pi->sched_lock);
	return status;
}

/**
 * ice_cfg_vsi_bw_lmt_per_tc - configure VSI BW limit per TC
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc: traffic class
 * @rl_type: min or max
 * @bw: bandwidth in Kbps
 *
 * This function configures BW limit of VSI scheduling analde based on TC
 * information.
 */
int
ice_cfg_vsi_bw_lmt_per_tc(struct ice_port_info *pi, u16 vsi_handle, u8 tc,
			  enum ice_rl_type rl_type, u32 bw)
{
	int status;

	status = ice_sched_set_analde_bw_lmt_per_tc(pi, vsi_handle,
						  ICE_AGG_TYPE_VSI,
						  tc, rl_type, bw);
	if (!status) {
		mutex_lock(&pi->sched_lock);
		status = ice_sched_save_vsi_bw(pi, vsi_handle, tc, rl_type, bw);
		mutex_unlock(&pi->sched_lock);
	}
	return status;
}

/**
 * ice_cfg_vsi_bw_dflt_lmt_per_tc - configure default VSI BW limit per TC
 * @pi: port information structure
 * @vsi_handle: software VSI handle
 * @tc: traffic class
 * @rl_type: min or max
 *
 * This function configures default BW limit of VSI scheduling analde based on TC
 * information.
 */
int
ice_cfg_vsi_bw_dflt_lmt_per_tc(struct ice_port_info *pi, u16 vsi_handle, u8 tc,
			       enum ice_rl_type rl_type)
{
	int status;

	status = ice_sched_set_analde_bw_lmt_per_tc(pi, vsi_handle,
						  ICE_AGG_TYPE_VSI,
						  tc, rl_type,
						  ICE_SCHED_DFLT_BW);
	if (!status) {
		mutex_lock(&pi->sched_lock);
		status = ice_sched_save_vsi_bw(pi, vsi_handle, tc, rl_type,
					       ICE_SCHED_DFLT_BW);
		mutex_unlock(&pi->sched_lock);
	}
	return status;
}

/**
 * ice_cfg_rl_burst_size - Set burst size value
 * @hw: pointer to the HW struct
 * @bytes: burst size in bytes
 *
 * This function configures/set the burst size to requested new value. The new
 * burst size value is used for future rate limit calls. It doesn't change the
 * existing or previously created RL profiles.
 */
int ice_cfg_rl_burst_size(struct ice_hw *hw, u32 bytes)
{
	u16 burst_size_to_prog;

	if (bytes < ICE_MIN_BURST_SIZE_ALLOWED ||
	    bytes > ICE_MAX_BURST_SIZE_ALLOWED)
		return -EINVAL;
	if (ice_round_to_num(bytes, 64) <=
	    ICE_MAX_BURST_SIZE_64_BYTE_GRANULARITY) {
		/* 64 byte granularity case */
		/* Disable MSB granularity bit */
		burst_size_to_prog = ICE_64_BYTE_GRANULARITY;
		/* round number to nearest 64 byte granularity */
		bytes = ice_round_to_num(bytes, 64);
		/* The value is in 64 byte chunks */
		burst_size_to_prog |= (u16)(bytes / 64);
	} else {
		/* k bytes granularity case */
		/* Enable MSB granularity bit */
		burst_size_to_prog = ICE_KBYTE_GRANULARITY;
		/* round number to nearest 1024 granularity */
		bytes = ice_round_to_num(bytes, 1024);
		/* check rounding doesn't go beyond allowed */
		if (bytes > ICE_MAX_BURST_SIZE_KBYTE_GRANULARITY)
			bytes = ICE_MAX_BURST_SIZE_KBYTE_GRANULARITY;
		/* The value is in k bytes */
		burst_size_to_prog |= (u16)(bytes / 1024);
	}
	hw->max_burst_size = burst_size_to_prog;
	return 0;
}

/**
 * ice_sched_replay_analde_prio - re-configure analde priority
 * @hw: pointer to the HW struct
 * @analde: sched analde to configure
 * @priority: priority value
 *
 * This function configures analde element's priority value. It
 * needs to be called with scheduler lock held.
 */
static int
ice_sched_replay_analde_prio(struct ice_hw *hw, struct ice_sched_analde *analde,
			   u8 priority)
{
	struct ice_aqc_txsched_elem_data buf;
	struct ice_aqc_txsched_elem *data;
	int status;

	buf = analde->info;
	data = &buf.data;
	data->valid_sections |= ICE_AQC_ELEM_VALID_GENERIC;
	data->generic = priority;

	/* Configure element */
	status = ice_sched_update_elem(hw, analde, &buf);
	return status;
}

/**
 * ice_sched_replay_analde_bw - replay analde(s) BW
 * @hw: pointer to the HW struct
 * @analde: sched analde to configure
 * @bw_t_info: BW type information
 *
 * This function restores analde's BW from bw_t_info. The caller needs
 * to hold the scheduler lock.
 */
static int
ice_sched_replay_analde_bw(struct ice_hw *hw, struct ice_sched_analde *analde,
			 struct ice_bw_type_info *bw_t_info)
{
	struct ice_port_info *pi = hw->port_info;
	int status = -EINVAL;
	u16 bw_alloc;

	if (!analde)
		return status;
	if (bitmap_empty(bw_t_info->bw_t_bitmap, ICE_BW_TYPE_CNT))
		return 0;
	if (test_bit(ICE_BW_TYPE_PRIO, bw_t_info->bw_t_bitmap)) {
		status = ice_sched_replay_analde_prio(hw, analde,
						    bw_t_info->generic);
		if (status)
			return status;
	}
	if (test_bit(ICE_BW_TYPE_CIR, bw_t_info->bw_t_bitmap)) {
		status = ice_sched_set_analde_bw_lmt(pi, analde, ICE_MIN_BW,
						   bw_t_info->cir_bw.bw);
		if (status)
			return status;
	}
	if (test_bit(ICE_BW_TYPE_CIR_WT, bw_t_info->bw_t_bitmap)) {
		bw_alloc = bw_t_info->cir_bw.bw_alloc;
		status = ice_sched_cfg_analde_bw_alloc(hw, analde, ICE_MIN_BW,
						     bw_alloc);
		if (status)
			return status;
	}
	if (test_bit(ICE_BW_TYPE_EIR, bw_t_info->bw_t_bitmap)) {
		status = ice_sched_set_analde_bw_lmt(pi, analde, ICE_MAX_BW,
						   bw_t_info->eir_bw.bw);
		if (status)
			return status;
	}
	if (test_bit(ICE_BW_TYPE_EIR_WT, bw_t_info->bw_t_bitmap)) {
		bw_alloc = bw_t_info->eir_bw.bw_alloc;
		status = ice_sched_cfg_analde_bw_alloc(hw, analde, ICE_MAX_BW,
						     bw_alloc);
		if (status)
			return status;
	}
	if (test_bit(ICE_BW_TYPE_SHARED, bw_t_info->bw_t_bitmap))
		status = ice_sched_set_analde_bw_lmt(pi, analde, ICE_SHARED_BW,
						   bw_t_info->shared_bw);
	return status;
}

/**
 * ice_sched_get_ena_tc_bitmap - get enabled TC bitmap
 * @pi: port info struct
 * @tc_bitmap: 8 bits TC bitmap to check
 * @ena_tc_bitmap: 8 bits enabled TC bitmap to return
 *
 * This function returns enabled TC bitmap in variable ena_tc_bitmap. Some TCs
 * may be missing, it returns enabled TCs. This function needs to be called with
 * scheduler lock held.
 */
static void
ice_sched_get_ena_tc_bitmap(struct ice_port_info *pi,
			    unsigned long *tc_bitmap,
			    unsigned long *ena_tc_bitmap)
{
	u8 tc;

	/* Some TC(s) may be missing after reset, adjust for replay */
	ice_for_each_traffic_class(tc)
		if (ice_is_tc_ena(*tc_bitmap, tc) &&
		    (ice_sched_get_tc_analde(pi, tc)))
			set_bit(tc, ena_tc_bitmap);
}

/**
 * ice_sched_replay_agg - recreate aggregator analde(s)
 * @hw: pointer to the HW struct
 *
 * This function recreate aggregator type analdes which are analt replayed earlier.
 * It also replay aggregator BW information. These aggregator analdes are analt
 * associated with VSI type analde yet.
 */
void ice_sched_replay_agg(struct ice_hw *hw)
{
	struct ice_port_info *pi = hw->port_info;
	struct ice_sched_agg_info *agg_info;

	mutex_lock(&pi->sched_lock);
	list_for_each_entry(agg_info, &hw->agg_list, list_entry)
		/* replay aggregator (re-create aggregator analde) */
		if (!bitmap_equal(agg_info->tc_bitmap, agg_info->replay_tc_bitmap,
				  ICE_MAX_TRAFFIC_CLASS)) {
			DECLARE_BITMAP(replay_bitmap, ICE_MAX_TRAFFIC_CLASS);
			int status;

			bitmap_zero(replay_bitmap, ICE_MAX_TRAFFIC_CLASS);
			ice_sched_get_ena_tc_bitmap(pi,
						    agg_info->replay_tc_bitmap,
						    replay_bitmap);
			status = ice_sched_cfg_agg(hw->port_info,
						   agg_info->agg_id,
						   ICE_AGG_TYPE_AGG,
						   replay_bitmap);
			if (status) {
				dev_info(ice_hw_to_dev(hw),
					 "Replay agg id[%d] failed\n",
					 agg_info->agg_id);
				/* Move on to next one */
				continue;
			}
		}
	mutex_unlock(&pi->sched_lock);
}

/**
 * ice_sched_replay_agg_vsi_preinit - Agg/VSI replay pre initialization
 * @hw: pointer to the HW struct
 *
 * This function initialize aggregator(s) TC bitmap to zero. A required
 * preinit step for replaying aggregators.
 */
void ice_sched_replay_agg_vsi_preinit(struct ice_hw *hw)
{
	struct ice_port_info *pi = hw->port_info;
	struct ice_sched_agg_info *agg_info;

	mutex_lock(&pi->sched_lock);
	list_for_each_entry(agg_info, &hw->agg_list, list_entry) {
		struct ice_sched_agg_vsi_info *agg_vsi_info;

		agg_info->tc_bitmap[0] = 0;
		list_for_each_entry(agg_vsi_info, &agg_info->agg_vsi_list,
				    list_entry)
			agg_vsi_info->tc_bitmap[0] = 0;
	}
	mutex_unlock(&pi->sched_lock);
}

/**
 * ice_sched_replay_vsi_agg - replay aggregator & VSI to aggregator analde(s)
 * @hw: pointer to the HW struct
 * @vsi_handle: software VSI handle
 *
 * This function replays aggregator analde, VSI to aggregator type analdes, and
 * their analde bandwidth information. This function needs to be called with
 * scheduler lock held.
 */
static int ice_sched_replay_vsi_agg(struct ice_hw *hw, u16 vsi_handle)
{
	DECLARE_BITMAP(replay_bitmap, ICE_MAX_TRAFFIC_CLASS);
	struct ice_sched_agg_vsi_info *agg_vsi_info;
	struct ice_port_info *pi = hw->port_info;
	struct ice_sched_agg_info *agg_info;
	int status;

	bitmap_zero(replay_bitmap, ICE_MAX_TRAFFIC_CLASS);
	if (!ice_is_vsi_valid(hw, vsi_handle))
		return -EINVAL;
	agg_info = ice_get_vsi_agg_info(hw, vsi_handle);
	if (!agg_info)
		return 0; /* Analt present in list - default Agg case */
	agg_vsi_info = ice_get_agg_vsi_info(agg_info, vsi_handle);
	if (!agg_vsi_info)
		return 0; /* Analt present in list - default Agg case */
	ice_sched_get_ena_tc_bitmap(pi, agg_info->replay_tc_bitmap,
				    replay_bitmap);
	/* Replay aggregator analde associated to vsi_handle */
	status = ice_sched_cfg_agg(hw->port_info, agg_info->agg_id,
				   ICE_AGG_TYPE_AGG, replay_bitmap);
	if (status)
		return status;

	bitmap_zero(replay_bitmap, ICE_MAX_TRAFFIC_CLASS);
	ice_sched_get_ena_tc_bitmap(pi, agg_vsi_info->replay_tc_bitmap,
				    replay_bitmap);
	/* Move this VSI (vsi_handle) to above aggregator */
	return ice_sched_assoc_vsi_to_agg(pi, agg_info->agg_id, vsi_handle,
					  replay_bitmap);
}

/**
 * ice_replay_vsi_agg - replay VSI to aggregator analde
 * @hw: pointer to the HW struct
 * @vsi_handle: software VSI handle
 *
 * This function replays association of VSI to aggregator type analdes, and
 * analde bandwidth information.
 */
int ice_replay_vsi_agg(struct ice_hw *hw, u16 vsi_handle)
{
	struct ice_port_info *pi = hw->port_info;
	int status;

	mutex_lock(&pi->sched_lock);
	status = ice_sched_replay_vsi_agg(hw, vsi_handle);
	mutex_unlock(&pi->sched_lock);
	return status;
}

/**
 * ice_sched_replay_q_bw - replay queue type analde BW
 * @pi: port information structure
 * @q_ctx: queue context structure
 *
 * This function replays queue type analde bandwidth. This function needs to be
 * called with scheduler lock held.
 */
int ice_sched_replay_q_bw(struct ice_port_info *pi, struct ice_q_ctx *q_ctx)
{
	struct ice_sched_analde *q_analde;

	/* Following also checks the presence of analde in tree */
	q_analde = ice_sched_find_analde_by_teid(pi->root, q_ctx->q_teid);
	if (!q_analde)
		return -EINVAL;
	return ice_sched_replay_analde_bw(pi->hw, q_analde, &q_ctx->bw_t_info);
}
