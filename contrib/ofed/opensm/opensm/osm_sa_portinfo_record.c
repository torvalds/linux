/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2011 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2013 Oracle and/or its affiliates. All rights reserved.
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
 *    Implementation of osm_pir_rcv_t.
 * This object represents the PortInfoRecord Receiver object.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <complib/cl_qlist.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_SA_PORTINFO_RECORD_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_pkey.h>
#include <opensm/osm_sa.h>

#define SA_PIR_RESP_SIZE SA_ITEM_RESP_SIZE(port_rec)

typedef struct osm_pir_search_ctxt {
	const ib_portinfo_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	cl_qlist_t *p_list;
	osm_sa_t *sa;
	const osm_physp_t *p_req_physp;
	boolean_t is_enhanced_comp_mask;
} osm_pir_search_ctxt_t;

static ib_api_status_t pir_rcv_new_pir(IN osm_sa_t * sa,
				       IN const osm_physp_t * p_physp,
				       IN osm_pir_search_ctxt_t * p_ctxt,
				       IN ib_net16_t const lid)
{
	osm_sa_item_t *p_rec_item;
	ib_port_info_t *p_pi;
	osm_physp_t *p_physp0;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sa->p_log);

	p_rec_item = malloc(SA_PIR_RESP_SIZE);
	if (p_rec_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2102: "
			"rec_item alloc failed\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"New PortInfoRecord: port 0x%016" PRIx64
		", lid %u, port %u\n",
		cl_ntoh64(osm_physp_get_port_guid(p_physp)),
		cl_ntoh16(lid), osm_physp_get_port_num(p_physp));

	memset(p_rec_item, 0, SA_PIR_RESP_SIZE);

	p_rec_item->resp.port_rec.lid = lid;
	p_rec_item->resp.port_rec.port_info = p_physp->port_info;
	if (p_ctxt->comp_mask & IB_PIR_COMPMASK_OPTIONS)
		p_rec_item->resp.port_rec.options = p_ctxt->p_rcvd_rec->options;
	if ((p_ctxt->comp_mask & IB_PIR_COMPMASK_OPTIONS) == 0 ||
	    (p_ctxt->p_rcvd_rec->options & 0x80) == 0) {
		/* Does requested port have an extended link speed active ? */
		if (osm_node_get_type(p_physp->p_node) ==
		    IB_NODE_TYPE_SWITCH) {
			p_physp0 = osm_node_get_physp_ptr(p_physp->p_node, 0);
			p_pi = &p_physp0->port_info;
		} else
			p_pi = (ib_port_info_t *) &p_physp->port_info;
		if ((p_pi->capability_mask & IB_PORT_CAP_HAS_EXT_SPEEDS) > 0) {
			if (ib_port_info_get_link_speed_ext_active(&p_physp->port_info)) {
				/* Add QDR bits to original link speed components */
				p_pi = &p_rec_item->resp.port_rec.port_info;
				ib_port_info_set_link_speed_enabled(p_pi,
								    ib_port_info_get_link_speed_enabled(p_pi) | IB_LINK_SPEED_ACTIVE_10);
				p_pi->state_info1 =
				    (uint8_t) ((p_pi->state_info1 & IB_PORT_STATE_MASK) |
					       (ib_port_info_get_link_speed_sup(p_pi) | IB_LINK_SPEED_ACTIVE_10) << IB_PORT_LINK_SPEED_SHIFT);
				p_pi->link_speed =
				    (uint8_t) ((p_pi->link_speed & IB_PORT_LINK_SPEED_ENABLED_MASK) |
					       (ib_port_info_get_link_speed_active(p_pi) | IB_LINK_SPEED_ACTIVE_10) << IB_PORT_LINK_SPEED_SHIFT);
			}
		}
	}
	p_rec_item->resp.port_rec.port_num = osm_physp_get_port_num(p_physp);

	cl_qlist_insert_tail(p_ctxt->p_list, &p_rec_item->list_item);

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return status;
}

static void sa_pir_create(IN osm_sa_t * sa, IN const osm_physp_t * p_physp,
			  IN osm_pir_search_ctxt_t * p_ctxt)
{
	uint8_t lmc;
	uint16_t max_lid_ho;
	uint16_t base_lid_ho;
	uint16_t match_lid_ho;
	osm_physp_t *p_node_physp;

	OSM_LOG_ENTER(sa->p_log);

	if (p_physp->p_node->sw) {
		p_node_physp = osm_node_get_physp_ptr(p_physp->p_node, 0);
		base_lid_ho = cl_ntoh16(osm_physp_get_base_lid(p_node_physp));
		lmc =
		    osm_switch_sp0_is_lmc_capable(p_physp->p_node->sw,
						  sa->p_subn) ?
		    osm_physp_get_lmc(p_node_physp) : 0;
	} else {
		lmc = osm_physp_get_lmc(p_physp);
		base_lid_ho = cl_ntoh16(osm_physp_get_base_lid(p_physp));
	}
	max_lid_ho = (uint16_t) (base_lid_ho + (1 << lmc) - 1);

	if (p_ctxt->comp_mask & IB_PIR_COMPMASK_LID) {
		match_lid_ho = cl_ntoh16(p_ctxt->p_rcvd_rec->lid);

		/*
		   We validate that the lid belongs to this node.
		 */
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Comparing LID: %u <= %u <= %u\n",
			base_lid_ho, match_lid_ho, max_lid_ho);

		if (match_lid_ho < base_lid_ho || match_lid_ho > max_lid_ho)
			goto Exit;
	}

	pir_rcv_new_pir(sa, p_physp, p_ctxt, cl_hton16(base_lid_ho));

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

static void sa_pir_check_physp(IN osm_sa_t * sa, IN const osm_physp_t * p_physp,
			       osm_pir_search_ctxt_t * p_ctxt)
{
	const ib_portinfo_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	const ib_port_info_t *p_comp_pi;
	const ib_port_info_t *p_pi;
	const osm_physp_t * p_physp0;
	ib_net32_t cap_mask;

	OSM_LOG_ENTER(sa->p_log);

	p_rcvd_rec = p_ctxt->p_rcvd_rec;
	comp_mask = p_ctxt->comp_mask;
	p_comp_pi = &p_rcvd_rec->port_info;
	p_pi = &p_physp->port_info;

	osm_dump_port_info_v2(sa->p_log, osm_node_get_node_guid(p_physp->p_node),
			      p_physp->port_guid, p_physp->port_num,
			      &p_physp->port_info, FILE_ID, OSM_LOG_DEBUG);

	/* We have to re-check the base_lid, since if the given
	   base_lid in p_pi is zero - we are comparing on all ports. */
	if (comp_mask & IB_PIR_COMPMASK_BASELID) {
		if (p_comp_pi->base_lid != p_pi->base_lid)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_MKEY) {
		if (p_comp_pi->m_key != p_pi->m_key)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_GIDPRE) {
		if (p_comp_pi->subnet_prefix != p_pi->subnet_prefix)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_SMLID) {
		if (p_comp_pi->master_sm_base_lid != p_pi->master_sm_base_lid)
			goto Exit;
	}

	/* IBTA 1.2 errata provides support for bitwise compare if the bit 31
	   of the attribute modifier of the Get/GetTable is set */
	if (comp_mask & IB_PIR_COMPMASK_CAPMASK) {
		if (p_ctxt->is_enhanced_comp_mask) {
			if ((p_comp_pi->capability_mask & p_pi->
			     capability_mask) != p_comp_pi->capability_mask)
				goto Exit;
		} else {
			if (p_comp_pi->capability_mask != p_pi->capability_mask)
				goto Exit;
		}
	}

	if (comp_mask & IB_PIR_COMPMASK_DIAGCODE) {
		if (p_comp_pi->diag_code != p_pi->diag_code)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_MKEYLEASEPRD) {
		if (p_comp_pi->m_key_lease_period != p_pi->m_key_lease_period)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_LOCALPORTNUM) {
		if (p_comp_pi->local_port_num != p_pi->local_port_num)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_LNKWIDTHSUPPORT) {
		if (p_comp_pi->link_width_supported !=
		    p_pi->link_width_supported)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_LNKWIDTHACTIVE) {
		if (p_comp_pi->link_width_active != p_pi->link_width_active)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_LINKWIDTHENABLED) {
		if (p_comp_pi->link_width_enabled != p_pi->link_width_enabled)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_LNKSPEEDSUPPORT) {
		if (ib_port_info_get_link_speed_sup(p_comp_pi) !=
		    ib_port_info_get_link_speed_sup(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_PORTSTATE) {
		if (ib_port_info_get_port_state(p_comp_pi) !=
		    ib_port_info_get_port_state(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_PORTPHYSTATE) {
		if (ib_port_info_get_port_phys_state(p_comp_pi) !=
		    ib_port_info_get_port_phys_state(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_LINKDWNDFLTSTATE) {
		if (ib_port_info_get_link_down_def_state(p_comp_pi) !=
		    ib_port_info_get_link_down_def_state(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_MKEYPROTBITS) {
		if (ib_port_info_get_mpb(p_comp_pi) !=
		    ib_port_info_get_mpb(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_LMC) {
		if (ib_port_info_get_lmc(p_comp_pi) !=
		    ib_port_info_get_lmc(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_LINKSPEEDACTIVE) {
		if (ib_port_info_get_link_speed_active(p_comp_pi) !=
		    ib_port_info_get_link_speed_active(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_LINKSPEEDENABLE) {
		if (ib_port_info_get_link_speed_enabled(p_comp_pi) !=
		    ib_port_info_get_link_speed_enabled(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_NEIGHBORMTU) {
		if (ib_port_info_get_neighbor_mtu(p_comp_pi) !=
		    ib_port_info_get_neighbor_mtu(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_MASTERSMSL) {
		if (ib_port_info_get_master_smsl(p_comp_pi) !=
		    ib_port_info_get_master_smsl(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_VLCAP) {
		if (ib_port_info_get_vl_cap(p_comp_pi) !=
		    ib_port_info_get_vl_cap(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_INITTYPE) {
		if (ib_port_info_get_init_type(p_comp_pi) !=
		    ib_port_info_get_init_type(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_VLHIGHLIMIT) {
		if (p_comp_pi->vl_high_limit != p_pi->vl_high_limit)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_VLARBHIGHCAP) {
		if (p_comp_pi->vl_arb_high_cap != p_pi->vl_arb_high_cap)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_VLARBLOWCAP) {
		if (p_comp_pi->vl_arb_low_cap != p_pi->vl_arb_low_cap)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_MTUCAP) {
		if (ib_port_info_get_mtu_cap(p_comp_pi) !=
		    ib_port_info_get_mtu_cap(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_VLSTALLCNT) {
		if (ib_port_info_get_vl_stall_count(p_comp_pi) !=
		    ib_port_info_get_vl_stall_count(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_HOQLIFE) {
		if ((p_comp_pi->vl_stall_life & 0x1F) !=
		    (p_pi->vl_stall_life & 0x1F))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_OPVLS) {
		if ((p_comp_pi->vl_enforce & 0xF0) != (p_pi->vl_enforce & 0xF0))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_PARENFIN) {
		if ((p_comp_pi->vl_enforce & 0x08) != (p_pi->vl_enforce & 0x08))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_PARENFOUT) {
		if ((p_comp_pi->vl_enforce & 0x04) != (p_pi->vl_enforce & 0x04))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_FILTERRAWIN) {
		if ((p_comp_pi->vl_enforce & 0x02) != (p_pi->vl_enforce & 0x02))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_FILTERRAWOUT) {
		if ((p_comp_pi->vl_enforce & 0x01) != (p_pi->vl_enforce & 0x01))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_MKEYVIO) {
		if (p_comp_pi->m_key_violations != p_pi->m_key_violations)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_PKEYVIO) {
		if (p_comp_pi->p_key_violations != p_pi->p_key_violations)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_QKEYVIO) {
		if (p_comp_pi->q_key_violations != p_pi->q_key_violations)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_GUIDCAP) {
		if (p_comp_pi->guid_cap != p_pi->guid_cap)
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_SUBNTO) {
		if (ib_port_info_get_timeout(p_comp_pi) !=
		    ib_port_info_get_timeout(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_RESPTIME) {
		if ((p_comp_pi->resp_time_value & 0x1F) !=
		    (p_pi->resp_time_value & 0x1F))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_LOCALPHYERR) {
		if (ib_port_info_get_local_phy_err_thd(p_comp_pi) !=
		    ib_port_info_get_local_phy_err_thd(p_pi))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_OVERRUNERR) {
		if (ib_port_info_get_overrun_err_thd(p_comp_pi) !=
		    ib_port_info_get_overrun_err_thd(p_pi))
			goto Exit;
	}

	/* IBTA 1.2 errata provides support for bitwise compare if the bit 31
	   of the attribute modifier of the Get/GetTable is set */
	if (comp_mask & IB_PIR_COMPMASK_CAPMASK2) {
		if (p_ctxt->is_enhanced_comp_mask) {
			if ((cl_ntoh16(p_comp_pi->capability_mask2) &
			     cl_ntoh16(p_pi->capability_mask2)) !=
			     cl_ntoh16(p_comp_pi->capability_mask2))
				goto Exit;
		} else {
			if (cl_ntoh16(p_comp_pi->capability_mask2) !=
			    cl_ntoh16(p_pi->capability_mask2))
				goto Exit;
		}
	}
	if (osm_node_get_type(p_physp->p_node) == IB_NODE_TYPE_SWITCH) {
		p_physp0 = osm_node_get_physp_ptr(p_physp->p_node, 0);
		cap_mask = p_physp0->port_info.capability_mask;
	} else
		cap_mask = p_pi->capability_mask;
	if (comp_mask & IB_PIR_COMPMASK_LINKSPDEXTACT) {
		if (((cap_mask & IB_PORT_CAP_HAS_EXT_SPEEDS) > 0) &&
		    (ib_port_info_get_link_speed_ext_active(p_comp_pi) !=
		     ib_port_info_get_link_speed_ext_active(p_pi)))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_LINKSPDEXTSUPP) {
		if (((cap_mask & IB_PORT_CAP_HAS_EXT_SPEEDS) > 0) &&
		    (ib_port_info_get_link_speed_ext_sup(p_comp_pi) !=
		     ib_port_info_get_link_speed_ext_sup(p_pi)))
			goto Exit;
	}
	if (comp_mask & IB_PIR_COMPMASK_LINKSPDEXTENAB) {
		if (((cap_mask & IB_PORT_CAP_HAS_EXT_SPEEDS) > 0) &&
		    (ib_port_info_get_link_speed_ext_enabled(p_comp_pi) !=
		     ib_port_info_get_link_speed_ext_enabled(p_pi)))
			goto Exit;
	}
	sa_pir_create(sa, p_physp, p_ctxt);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

static void sa_pir_by_comp_mask(IN osm_sa_t * sa, IN osm_node_t * p_node,
				osm_pir_search_ctxt_t * p_ctxt)
{
	const ib_portinfo_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	const osm_physp_t *p_physp;
	uint8_t port_num;
	uint8_t num_ports;
	const osm_physp_t *p_req_physp;

	OSM_LOG_ENTER(sa->p_log);

	p_rcvd_rec = p_ctxt->p_rcvd_rec;
	comp_mask = p_ctxt->comp_mask;
	p_req_physp = p_ctxt->p_req_physp;

	num_ports = osm_node_get_num_physp(p_node);

	if (comp_mask & IB_PIR_COMPMASK_PORTNUM) {
		if (p_rcvd_rec->port_num < num_ports) {
			p_physp =
			    osm_node_get_physp_ptr(p_node,
						   p_rcvd_rec->port_num);
			/* Check that the p_physp is valid, and that the
			   p_physp and the p_req_physp share a pkey. */
			if (p_physp &&
			    osm_physp_share_pkey(sa->p_log, p_req_physp, p_physp,
						 sa->p_subn->opt.allow_both_pkeys))
				sa_pir_check_physp(sa, p_physp, p_ctxt);
		}
	} else {
		for (port_num = 0; port_num < num_ports; port_num++) {
			p_physp = osm_node_get_physp_ptr(p_node, port_num);
			if (!p_physp)
				continue;

			/* if the requester and the p_physp don't share a pkey -
			   continue */
			if (!osm_physp_share_pkey(sa->p_log, p_req_physp, p_physp,
						  sa->p_subn->opt.allow_both_pkeys))
				continue;

			sa_pir_check_physp(sa, p_physp, p_ctxt);
		}
	}

	OSM_LOG_EXIT(sa->p_log);
}

static void sa_pir_by_comp_mask_cb(IN cl_map_item_t * p_map_item, IN void *cxt)
{
	osm_node_t *p_node = (osm_node_t *) p_map_item;
	osm_pir_search_ctxt_t *p_ctxt = cxt;

	sa_pir_by_comp_mask(p_ctxt->sa, p_node, p_ctxt);
}

void osm_pir_rcv_process(IN void *ctx, IN void *data)
{
	osm_sa_t *sa = ctx;
	osm_madw_t *p_madw = data;
	const ib_sa_mad_t *p_rcvd_mad;
	const ib_portinfo_record_t *p_rcvd_rec;
	const osm_port_t *p_port = NULL;
	cl_qlist_t rec_list;
	osm_pir_search_ctxt_t context;
	ib_net64_t comp_mask;
	osm_physp_t *p_req_physp;

	CL_ASSERT(sa);

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_rcvd_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec =
	    (ib_portinfo_record_t *) ib_sa_mad_get_payload_ptr(p_rcvd_mad);
	comp_mask = p_rcvd_mad->comp_mask;

	CL_ASSERT(p_rcvd_mad->attr_id == IB_MAD_ATTR_PORTINFO_RECORD);

	/* we only support SubnAdmGet and SubnAdmGetTable methods */
	if (p_rcvd_mad->method != IB_MAD_METHOD_GET &&
	    p_rcvd_mad->method != IB_MAD_METHOD_GETTABLE) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2105: "
			"Unsupported Method (%s) for PortInfoRecord request\n",
			ib_get_sa_method_str(p_rcvd_mad->method));
		osm_sa_send_error(sa, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		goto Exit;
	}

	cl_plock_acquire(sa->p_lock);

	/* update the requester physical port */
	p_req_physp = osm_get_physp_by_mad_addr(sa->p_log, sa->p_subn,
						osm_madw_get_mad_addr_ptr
						(p_madw));
	if (p_req_physp == NULL) {
		cl_plock_release(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2104: "
			"Cannot find requester physical port\n");
		goto Exit;
	}

	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Requester port GUID 0x%" PRIx64 "\n",
			cl_ntoh64(osm_physp_get_port_guid(p_req_physp)));
		osm_dump_portinfo_record_v2(sa->p_log, p_rcvd_rec, FILE_ID, OSM_LOG_DEBUG);
	}

	cl_qlist_init(&rec_list);

	context.p_rcvd_rec = p_rcvd_rec;
	context.p_list = &rec_list;
	context.comp_mask = p_rcvd_mad->comp_mask;
	context.sa = sa;
	context.p_req_physp = p_req_physp;
	context.is_enhanced_comp_mask =
	    cl_ntoh32(p_rcvd_mad->attr_mod) & (1 << 31);

	/*
	   If the user specified a LID, it obviously narrows our
	   work load, since we don't have to search every port
	 */
	if (comp_mask & (IB_PIR_COMPMASK_LID | IB_PIR_COMPMASK_BASELID)) {
		p_port = osm_get_port_by_lid(sa->p_subn, p_rcvd_rec->lid);
		if (p_port)
			sa_pir_by_comp_mask(sa, p_port->p_node, &context);
		else
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2109: "
				"No port found with requested LID %u\n",
				cl_ntoh16(p_rcvd_rec->lid));
	} else
		cl_qmap_apply_func(&sa->p_subn->node_guid_tbl,
				   sa_pir_by_comp_mask_cb, &context);

	cl_plock_release(sa->p_lock);

	/*
	   p922 - The M_Key returned shall be zero, except in the case of a
	   trusted request.
	   Note: In the mad controller we check that the SM_Key received on
	   the mad is valid. Meaning - is either zero or equal to the local
	   sm_key.
	 */
	if (!p_rcvd_mad->sm_key) {
		osm_sa_item_t *item;
		for (item = (osm_sa_item_t *) cl_qlist_head(&rec_list);
		     item != (osm_sa_item_t *) cl_qlist_end(&rec_list);
		     item = (osm_sa_item_t *) cl_qlist_next(&item->list_item))
			item->resp.port_rec.port_info.m_key = 0;
	}

	osm_sa_respond(sa, p_madw, sizeof(ib_portinfo_record_t), &rec_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}
