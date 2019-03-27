/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2015 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
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
 */

/*
 * Abstract:
 *    Implementation of osm_req_t.
 * This object represents the generic attribute requester.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_REQ_C
#include <opensm/osm_madw.h>
#include <opensm/osm_attrib_req.h>
#include <opensm/osm_log.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_vl15intf.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_db_pack.h>

/**********************************************************************
  The plock must be held before calling this function.
**********************************************************************/
static ib_net64_t req_determine_mkey(IN osm_sm_t * sm,
				     IN const osm_dr_path_t * p_path)
{
	osm_node_t *p_node;
	osm_port_t *p_sm_port;
	osm_physp_t *p_physp;
	ib_net64_t dest_port_guid = 0, m_key;
	uint8_t hop;

	OSM_LOG_ENTER(sm->p_log);

	p_physp = NULL;

	p_sm_port = osm_get_port_by_guid(sm->p_subn, sm->p_subn->sm_port_guid);

	/* hop_count == 0: destination port guid is SM */
	if (p_path->hop_count == 0) {
		dest_port_guid = sm->p_subn->sm_port_guid;
		goto Remote_Guid;
	}

	if (p_sm_port) {
		p_node = p_sm_port->p_node;
		if (osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH)
			p_physp = osm_node_get_physp_ptr(p_node, p_path->path[1]);
		else
			p_physp = p_sm_port->p_physp;
	}

	/* hop_count == 1: outgoing physp is SM physp */
	for (hop = 2; p_physp && hop <= p_path->hop_count; hop++) {
		p_physp = p_physp->p_remote_physp;
		if (!p_physp)
			break;
		p_node = p_physp->p_node;
		p_physp = osm_node_get_physp_ptr(p_node, p_path->path[hop]);
	}

	/* At this point, p_physp points at the outgoing physp on the
	   last hop, or NULL if we don't know it.
	*/
	if (!p_physp) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR,
			"ERR 1107: Outgoing physp is null on non-hop_0!\n");
		osm_dump_dr_path_v2(sm->p_log, p_path, FILE_ID, OSM_LOG_ERROR);
		dest_port_guid = 0;
		goto Remote_Guid;
	}

	if (p_physp->p_remote_physp) {
		dest_port_guid = p_physp->p_remote_physp->port_guid;
		goto Remote_Guid;
	}

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG, "Target port guid unknown, "
		"using persistent DB\n");
	if (!osm_db_neighbor_get(sm->p_subn->p_neighbor,
				 cl_ntoh64(p_physp->port_guid),
				 p_physp->port_num,
				 &dest_port_guid, NULL)) {
		dest_port_guid = cl_hton64(dest_port_guid);
	}

Remote_Guid:
	if (dest_port_guid) {
		if (!osm_db_guid2mkey_get(sm->p_subn->p_g2m,
					  cl_ntoh64(dest_port_guid), &m_key)) {
			m_key = cl_hton64(m_key);
			OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
				"Found mkey for guid 0x%"
				PRIx64 "\n", cl_ntoh64(dest_port_guid));
		} else {
			OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
				"Target port mkey unknown, using default\n");
			m_key = sm->p_subn->opt.m_key;
		}
	} else {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Target port guid unknown, using default\n");
		m_key = sm->p_subn->opt.m_key;
	}

	OSM_LOG_EXIT(sm->p_log);

	return m_key;
}

/**********************************************************************
  The plock must be held before calling this function.
**********************************************************************/
ib_api_status_t osm_req_get(IN osm_sm_t * sm, IN const osm_dr_path_t * p_path,
			    IN ib_net16_t attr_id, IN ib_net32_t attr_mod,
			    IN boolean_t find_mkey, ib_net64_t m_key,
			    IN cl_disp_msgid_t err_msg,
			    IN const osm_madw_context_t * p_context)
{
	osm_madw_t *p_madw;
	ib_api_status_t status = IB_SUCCESS;
	ib_net64_t m_key_calc;
	ib_net64_t tid;

	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_path);
	CL_ASSERT(attr_id);

	/* do nothing if we are exiting ... */
	if (osm_exit_flag)
		goto Exit;

	/* p_context may be NULL. */

	p_madw = osm_mad_pool_get(sm->p_mad_pool, sm->mad_ctrl.h_bind,
				  MAD_BLOCK_SIZE, NULL);
	if (p_madw == NULL) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR,
			"ERR 1101: Unable to acquire MAD\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	tid = cl_hton64((uint64_t) cl_atomic_inc(&sm->sm_trans_id)
						 & (uint64_t)(0xFFFFFFFF));
	if (tid == 0)
		tid = cl_hton64((uint64_t) cl_atomic_inc(&sm->sm_trans_id)
							 & (uint64_t)(0xFFFFFFFF));

	if (sm->p_subn->opt.m_key_lookup == TRUE) {
		if (find_mkey == TRUE)
			m_key_calc = req_determine_mkey(sm, p_path);
		else
			m_key_calc = m_key;
	} else
		m_key_calc = sm->p_subn->opt.m_key;

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
		"Getting %s (0x%X), modifier 0x%X, TID 0x%" PRIx64
		", MKey 0x%016" PRIx64 "\n",
		ib_get_sm_attr_str(attr_id), cl_ntoh16(attr_id),
		cl_ntoh32(attr_mod), cl_ntoh64(tid), cl_ntoh64(m_key_calc));

	ib_smp_init_new(osm_madw_get_smp_ptr(p_madw), IB_MAD_METHOD_GET,
			tid, attr_id, attr_mod, p_path->hop_count,
			m_key_calc, p_path->path,
			IB_LID_PERMISSIVE, IB_LID_PERMISSIVE);

	p_madw->mad_addr.dest_lid = IB_LID_PERMISSIVE;
	p_madw->mad_addr.addr_type.smi.source_lid = IB_LID_PERMISSIVE;
	p_madw->resp_expected = TRUE;
	p_madw->fail_msg = err_msg;

	/*
	   Fill in the mad wrapper context for the recipient.
	   In this case, the only thing the recipient needs is the
	   guid value.
	 */

	if (p_context)
		p_madw->context = *p_context;

	osm_vl15_post(sm->p_vl15, p_madw);

Exit:
	OSM_LOG_EXIT(sm->p_log);
	return status;
}

/**********************************************************************
  The plock must be held before calling this function.
**********************************************************************/
osm_madw_t *osm_prepare_req_set(IN osm_sm_t * sm, IN const osm_dr_path_t * p_path,
				IN const uint8_t * p_payload,
				IN size_t payload_size,
				IN ib_net16_t attr_id, IN ib_net32_t attr_mod,
				IN boolean_t find_mkey, IN ib_net64_t m_key,
				IN cl_disp_msgid_t err_msg,
				IN const osm_madw_context_t * p_context)
{
	osm_madw_t *p_madw = NULL;
	ib_net64_t m_key_calc;
	ib_net64_t tid;

	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_path);
	CL_ASSERT(attr_id);
	CL_ASSERT(p_payload);

	/* do nothing if we are exiting ... */
	if (osm_exit_flag)
		goto Exit;

	/* p_context may be NULL. */

	p_madw = osm_mad_pool_get(sm->p_mad_pool, sm->mad_ctrl.h_bind,
				  MAD_BLOCK_SIZE, NULL);
	if (p_madw == NULL) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR,
			"ERR 1102: Unable to acquire MAD\n");
		goto Exit;
	}

	tid = cl_hton64((uint64_t) cl_atomic_inc(&sm->sm_trans_id)
						 & (uint64_t)(0xFFFFFFFF));
	if (tid == 0)
		tid = cl_hton64((uint64_t) cl_atomic_inc(&sm->sm_trans_id)
							 & (uint64_t)(0xFFFFFFFF));

	if (sm->p_subn->opt.m_key_lookup == TRUE) {
		if (find_mkey == TRUE)
			m_key_calc = req_determine_mkey(sm, p_path);
		else
			m_key_calc = m_key;
	} else
		m_key_calc = sm->p_subn->opt.m_key;

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
		"Setting %s (0x%X), modifier 0x%X, TID 0x%" PRIx64
		", MKey 0x%016" PRIx64 "\n",
		ib_get_sm_attr_str(attr_id), cl_ntoh16(attr_id),
		cl_ntoh32(attr_mod), cl_ntoh64(tid), cl_ntoh64(m_key_calc));

	ib_smp_init_new(osm_madw_get_smp_ptr(p_madw), IB_MAD_METHOD_SET,
			tid, attr_id, attr_mod, p_path->hop_count,
			m_key_calc, p_path->path,
			IB_LID_PERMISSIVE, IB_LID_PERMISSIVE);

	p_madw->mad_addr.dest_lid = IB_LID_PERMISSIVE;
	p_madw->mad_addr.addr_type.smi.source_lid = IB_LID_PERMISSIVE;
	p_madw->resp_expected = TRUE;
	p_madw->fail_msg = err_msg;

	/*
	   Fill in the mad wrapper context for the recipient.
	   In this case, the only thing the recipient needs is the
	   guid value.
	 */

	if (p_context)
		p_madw->context = *p_context;

	memcpy(osm_madw_get_smp_ptr(p_madw)->data, p_payload, payload_size);

Exit:
	OSM_LOG_EXIT(sm->p_log);
	return p_madw;
}

void osm_send_req_mad(IN osm_sm_t * sm, IN osm_madw_t *p_madw)
{
	CL_ASSERT(p_madw);
	CL_ASSERT(sm);

	osm_vl15_post(sm->p_vl15, p_madw);
}

/**********************************************************************
  The plock MAY or MAY NOT be held before calling this function.
**********************************************************************/
ib_api_status_t osm_req_set(IN osm_sm_t * sm, IN const osm_dr_path_t * p_path,
			    IN const uint8_t * p_payload,
			    IN size_t payload_size,
			    IN ib_net16_t attr_id, IN ib_net32_t attr_mod,
			    IN boolean_t find_mkey, IN ib_net64_t m_key,
			    IN cl_disp_msgid_t err_msg,
			    IN const osm_madw_context_t * p_context)
{
	osm_madw_t *p_madw;
	ib_api_status_t status = IB_SUCCESS;

	p_madw = osm_prepare_req_set(sm, p_path, p_payload, payload_size, attr_id,
				     attr_mod, find_mkey, m_key, err_msg, p_context);
	if (p_madw == NULL)
		status = IB_INSUFFICIENT_RESOURCES;
	else
		osm_send_req_mad(sm, p_madw);

	return status;
}

int osm_send_trap144(osm_sm_t * sm, ib_net16_t local)
{
	osm_madw_t *madw;
	ib_smp_t *smp;
	ib_mad_notice_attr_t *ntc;
	osm_port_t *port, *smport;
	ib_port_info_t *pi;

	port = osm_get_port_by_guid(sm->p_subn, sm->p_subn->sm_port_guid);
	if (!port) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR,
			"ERR 1104: cannot find SM port by guid 0x%" PRIx64 "\n",
			cl_ntoh64(sm->p_subn->sm_port_guid));
		return -1;
	}

	pi = &port->p_physp->port_info;

	/* don't bother with sending trap when SMA supports this */
	if (!local &&
	    pi->capability_mask&(IB_PORT_CAP_HAS_TRAP|IB_PORT_CAP_HAS_CAP_NTC))
		return 0;

	smport = osm_get_port_by_guid(sm->p_subn, sm->master_sm_guid);
	if (!smport) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR,
			"ERR 1106: cannot find master SM port by guid 0x%" PRIx64 "\n",
			cl_ntoh64(sm->master_sm_guid));
		return -1;
	}

	madw = osm_mad_pool_get(sm->p_mad_pool,
				osm_sm_mad_ctrl_get_bind_handle(&sm->mad_ctrl),
				MAD_BLOCK_SIZE, NULL);
	if (madw == NULL) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR,
			"ERR 1105: Unable to acquire MAD\n");
		return -1;
	}

	madw->mad_addr.dest_lid = smport->p_physp->port_info.base_lid;
	madw->mad_addr.addr_type.smi.source_lid = pi->base_lid;
	madw->resp_expected = TRUE;
	madw->fail_msg = CL_DISP_MSGID_NONE;

	smp = osm_madw_get_smp_ptr(madw);
	memset(smp, 0, sizeof(*smp));

	smp->base_ver = 1;
	smp->mgmt_class = IB_MCLASS_SUBN_LID;
	smp->class_ver = 1;
	smp->method = IB_MAD_METHOD_TRAP;
	smp->trans_id = cl_hton64((uint64_t) cl_atomic_inc(&sm->sm_trans_id)
							   & (uint64_t)(0xFFFFFFFF));
	if (smp->trans_id == 0)
		smp->trans_id = cl_hton64((uint64_t) cl_atomic_inc(&sm->sm_trans_id)
								   & (uint64_t)(0xFFFFFFFF));

	smp->attr_id = IB_MAD_ATTR_NOTICE;

	ntc = (ib_mad_notice_attr_t *) smp->data;

	ntc->generic_type = 0x80 | IB_NOTICE_TYPE_INFO;
	ib_notice_set_prod_type_ho(ntc, osm_node_get_type(port->p_node));
	ntc->g_or_v.generic.trap_num = cl_hton16(SM_LOCAL_CHANGES_TRAP); /* 144 */
	ntc->issuer_lid = pi->base_lid;
	ntc->data_details.ntc_144.lid = pi->base_lid;
	ntc->data_details.ntc_144.local_changes = local ?
	    TRAP_144_MASK_OTHER_LOCAL_CHANGES : 0;
	ntc->data_details.ntc_144.new_cap_mask = pi->capability_mask;
	ntc->data_details.ntc_144.change_flgs = local;

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
		"Sending Trap 144, TID 0x%" PRIx64 " to SM lid %u\n",
		cl_ntoh64(smp->trans_id), cl_ntoh16(madw->mad_addr.dest_lid));

	osm_vl15_post(sm->p_vl15, madw);

	return 0;
}
