/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2011 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2008 Xsigo Systems Inc. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
 * Copyright (c) 2010 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2009-2011 ZIH, TU Dresden, Federal Republic of Germany. All rights reserved.
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
 *    Implementation of osm_pr_rcv_t.
 * This object represents the PathRecord Receiver object.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <complib/cl_qlist.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_SA_PATH_RECORD_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_base.h>
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_pkey.h>
#include <opensm/osm_multicast.h>
#include <opensm/osm_partition.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_qos_policy.h>
#include <opensm/osm_sa.h>
#include <opensm/osm_router.h>
#include <opensm/osm_prefix_route.h>
#include <opensm/osm_ucast_lash.h>

#define SA_PR_RESP_SIZE SA_ITEM_RESP_SIZE(path_rec)

#define MAX_HOPS 64

static inline boolean_t sa_path_rec_is_tavor_port(IN const osm_port_t * p_port)
{
	osm_node_t const *p_node;
	ib_net32_t vend_id;

	p_node = p_port->p_node;
	vend_id = ib_node_info_get_vendor_id(&p_node->node_info);

	return ((p_node->node_info.device_id == CL_HTON16(23108)) &&
		((vend_id == CL_HTON32(OSM_VENDOR_ID_MELLANOX)) ||
		 (vend_id == CL_HTON32(OSM_VENDOR_ID_TOPSPIN)) ||
		 (vend_id == CL_HTON32(OSM_VENDOR_ID_SILVERSTORM)) ||
		 (vend_id == CL_HTON32(OSM_VENDOR_ID_VOLTAIRE))));
}

static boolean_t
sa_path_rec_apply_tavor_mtu_limit(IN const ib_path_rec_t * p_pr,
				  IN const osm_port_t * p_src_port,
				  IN const osm_port_t * p_dest_port,
				  IN const ib_net64_t comp_mask)
{
	uint8_t required_mtu;

	/* only if at least one of the ports is a Tavor device */
	if (!sa_path_rec_is_tavor_port(p_src_port) &&
	    !sa_path_rec_is_tavor_port(p_dest_port))
		return FALSE;

	/*
	   we can apply the patch if either:
	   1. No MTU required
	   2. Required MTU <
	   3. Required MTU = 1K or 512 or 256
	   4. Required MTU > 256 or 512
	 */
	required_mtu = ib_path_rec_mtu(p_pr);
	if ((comp_mask & IB_PR_COMPMASK_MTUSELEC) &&
	    (comp_mask & IB_PR_COMPMASK_MTU)) {
		switch (ib_path_rec_mtu_sel(p_pr)) {
		case 0:	/* must be greater than */
		case 2:	/* exact match */
			if (IB_MTU_LEN_1024 < required_mtu)
				return FALSE;
			break;

		case 1:	/* must be less than */
			/* can't be disqualified by this one */
			break;

		case 3:	/* largest available */
			/* the ULP intentionally requested */
			/* the largest MTU possible */
			return FALSE;

		default:
			/* if we're here, there's a bug in ib_path_rec_mtu_sel() */
			CL_ASSERT(FALSE);
			break;
		}
	}

	return TRUE;
}

static ib_api_status_t pr_rcv_get_path_parms(IN osm_sa_t * sa,
					     IN const ib_path_rec_t * p_pr,
					     IN const osm_alias_guid_t * p_src_alias_guid,
					     IN const uint16_t src_lid_ho,
					     IN const osm_alias_guid_t * p_dest_alias_guid,
					     IN const uint16_t dest_lid_ho,
					     IN const ib_net64_t comp_mask,
					     OUT osm_path_parms_t * p_parms)
{
	const osm_node_t *p_node;
	const osm_physp_t *p_physp, *p_physp0;
	const osm_physp_t *p_src_physp;
	const osm_physp_t *p_dest_physp;
	const osm_prtn_t *p_prtn = NULL;
	osm_opensm_t *p_osm;
	struct osm_routing_engine *p_re;
	const ib_port_info_t *p_pi, *p_pi0;
	ib_api_status_t status = IB_SUCCESS;
	ib_net16_t pkey;
	uint8_t mtu;
	uint8_t rate, p0_extended_rate, dest_rate;
	uint8_t pkt_life;
	uint8_t required_mtu;
	uint8_t required_rate;
	uint8_t required_pkt_life;
	uint8_t sl;
	uint8_t in_port_num;
	ib_net16_t dest_lid;
	uint8_t i;
	ib_slvl_table_t *p_slvl_tbl = NULL;
	osm_qos_level_t *p_qos_level = NULL;
	uint16_t valid_sl_mask = 0xffff;
	int hops = 0;
	int extended, p0_extended;

	OSM_LOG_ENTER(sa->p_log);

	dest_lid = cl_hton16(dest_lid_ho);

	p_dest_physp = p_dest_alias_guid->p_base_port->p_physp;
	p_physp = p_src_alias_guid->p_base_port->p_physp;
	p_src_physp = p_physp;
	p_pi = &p_physp->port_info;
	p_osm = sa->p_subn->p_osm;
	p_re = p_osm->routing_engine_used;

	mtu = ib_port_info_get_mtu_cap(p_pi);
	extended = p_pi->capability_mask & IB_PORT_CAP_HAS_EXT_SPEEDS;
	rate = ib_port_info_compute_rate(p_pi, extended);

	/*
	   Mellanox Tavor device performance is better using 1K MTU.
	   If required MTU and MTU selector are such that 1K is OK
	   and at least one end of the path is Tavor we override the
	   port MTU with 1K.
	 */
	if (sa->p_subn->opt.enable_quirks &&
	    sa_path_rec_apply_tavor_mtu_limit(p_pr,
					      p_src_alias_guid->p_base_port,
					      p_dest_alias_guid->p_base_port,
					      comp_mask))
		if (mtu > IB_MTU_LEN_1024) {
			mtu = IB_MTU_LEN_1024;
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"Optimized Path MTU to 1K for Mellanox Tavor device\n");
		}

	/*
	   Walk the subnet object from source to destination,
	   tracking the most restrictive rate and mtu values along the way...

	   If source port node is a switch, then p_physp should
	   point to the port that routes the destination lid
	 */

	p_node = osm_physp_get_node_ptr(p_physp);

	if (p_node->sw) {
		/*
		 * Source node is a switch.
		 * Make sure that p_physp points to the out port of the
		 * switch that routes to the destination lid (dest_lid_ho)
		 */
		p_physp = osm_switch_get_route_by_lid(p_node->sw, dest_lid);
		if (p_physp == 0) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F02: "
				"Cannot find routing from LID %u to LID %u on "
				"switch %s (GUID: 0x%016" PRIx64 ")\n",
				src_lid_ho, dest_lid_ho, p_node->print_desc,
				cl_ntoh64(osm_node_get_node_guid(p_node)));
			status = IB_NOT_FOUND;
			goto Exit;
		}
	}

	if (sa->p_subn->opt.qos) {
		/*
		 * Whether this node is switch or CA, the IN port for
		 * the sl2vl table is 0, because this is a source node.
		 */
		p_slvl_tbl = osm_physp_get_slvl_tbl(p_physp, 0);

		/* update valid SLs that still exist on this route */
		for (i = 0; i < IB_MAX_NUM_VLS; i++) {
			if (valid_sl_mask & (1 << i) &&
			    ib_slvl_table_get(p_slvl_tbl, i) == IB_DROP_VL)
				valid_sl_mask &= ~(1 << i);
		}
		if (!valid_sl_mask) {
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"All the SLs lead to VL15 on this path\n");
			status = IB_NOT_FOUND;
			goto Exit;
		}
	}

	/*
	 * Same as above
	 */
	p_node = osm_physp_get_node_ptr(p_dest_physp);

	if (p_node->sw) {
		/*
		 * if destination is switch, we want p_dest_physp to point to port 0
		 */
		p_dest_physp =
		    osm_switch_get_route_by_lid(p_node->sw, dest_lid);

		if (p_dest_physp == 0) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F03: "
				"Can't find routing from LID %u to LID %u on "
				"switch %s (GUID: 0x%016" PRIx64 ")\n",
				src_lid_ho, dest_lid_ho, p_node->print_desc,
				cl_ntoh64(osm_node_get_node_guid(p_node)));
			status = IB_NOT_FOUND;
			goto Exit;
		}

	}

	/*
	 * Now go through the path step by step
	 */

	while (p_physp != p_dest_physp) {

		int tmp_pnum = p_physp->port_num;
		p_node = osm_physp_get_node_ptr(p_physp);
		p_physp = osm_physp_get_remote(p_physp);

		if (p_physp == 0) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F05: "
				"Can't find remote phys port of %s (GUID: "
				"0x%016"PRIx64") port %d "
				"while routing from LID %u to LID %u\n",
				p_node->print_desc,
				cl_ntoh64(osm_node_get_node_guid(p_node)),
				tmp_pnum, src_lid_ho, dest_lid_ho);
			status = IB_ERROR;
			goto Exit;
		}

		in_port_num = osm_physp_get_port_num(p_physp);

		/*
		   This is point to point case (no switch in between)
		 */
		if (p_physp == p_dest_physp)
			break;

		p_node = osm_physp_get_node_ptr(p_physp);

		if (!p_node->sw) {
			/*
			   There is some sort of problem in the subnet object!
			   If this isn't a switch, we should have reached
			   the destination by now!
			 */
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F06: "
				"Internal error, bad path while routing "
				"%s (GUID: 0x%016"PRIx64") port %d to "
				"%s (GUID: 0x%016"PRIx64") port %d; "
				"ended at %s port %d\n",
				p_src_alias_guid->p_base_port->p_node->print_desc,
				cl_ntoh64(p_src_alias_guid->p_base_port->p_node->node_info.node_guid),
				p_src_alias_guid->p_base_port->p_physp->port_num,
				p_dest_alias_guid->p_base_port->p_node->print_desc,
				cl_ntoh64(p_dest_alias_guid->p_base_port->p_node->node_info.node_guid),
				p_dest_alias_guid->p_base_port->p_physp->port_num,
				p_node->print_desc,
				p_physp->port_num);
			status = IB_ERROR;
			goto Exit;
		}

		/*
		   Check parameters for the ingress port in this switch.
		 */
		p_pi = &p_physp->port_info;

		if (mtu > ib_port_info_get_mtu_cap(p_pi))
			mtu = ib_port_info_get_mtu_cap(p_pi);

		p_physp0 = osm_node_get_physp_ptr((osm_node_t *)p_node, 0);
		p_pi0 = &p_physp0->port_info;
		p0_extended = p_pi0->capability_mask & IB_PORT_CAP_HAS_EXT_SPEEDS;
		p0_extended_rate = ib_port_info_compute_rate(p_pi, p0_extended);
		if (ib_path_compare_rates(rate, p0_extended_rate) > 0)
			rate = p0_extended_rate;

		/*
		   Continue with the egress port on this switch.
		 */
		p_physp = osm_switch_get_route_by_lid(p_node->sw, dest_lid);
		if (p_physp == 0) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F07: "
				"Dead end path on switch "
				"%s (GUID: 0x%016"PRIx64") to LID %u\n",
				p_node->print_desc,
				cl_ntoh64(osm_node_get_node_guid(p_node)),
				dest_lid_ho);
			status = IB_ERROR;
			goto Exit;
		}

		p_pi = &p_physp->port_info;

		if (mtu > ib_port_info_get_mtu_cap(p_pi))
			mtu = ib_port_info_get_mtu_cap(p_pi);

		p_physp0 = osm_node_get_physp_ptr((osm_node_t *)p_node, 0);
		p_pi0 = &p_physp0->port_info;
		p0_extended = p_pi0->capability_mask & IB_PORT_CAP_HAS_EXT_SPEEDS;
		p0_extended_rate = ib_port_info_compute_rate(p_pi, p0_extended);
		if (ib_path_compare_rates(rate, p0_extended_rate) > 0)
			rate = p0_extended_rate;

		if (sa->p_subn->opt.qos) {
			/*
			 * Check SL2VL table of the switch and update valid SLs
			 */
			p_slvl_tbl =
			    osm_physp_get_slvl_tbl(p_physp, in_port_num);
			for (i = 0; i < IB_MAX_NUM_VLS; i++) {
				if (valid_sl_mask & (1 << i) &&
				    ib_slvl_table_get(p_slvl_tbl,
						      i) == IB_DROP_VL)
					valid_sl_mask &= ~(1 << i);
			}
			if (!valid_sl_mask) {
				OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "All the SLs "
					"lead to VL15 on this path\n");
				status = IB_NOT_FOUND;
				goto Exit;
			}
		}

		/* update number of hops traversed */
		hops++;
		if (hops > MAX_HOPS) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F25: "
				"Path from GUID 0x%016" PRIx64 " (%s port %d) "
				"to lid %u GUID 0x%016" PRIx64 " (%s port %d) "
				"needs more than %d hops, max %d hops allowed\n",
				cl_ntoh64(osm_physp_get_port_guid(p_src_physp)),
				p_src_physp->p_node->print_desc,
				p_src_physp->port_num,
				dest_lid_ho,
				cl_ntoh64(osm_physp_get_port_guid
					  (p_dest_physp)),
				p_dest_physp->p_node->print_desc,
				p_dest_physp->port_num,
				hops,
				MAX_HOPS);
			status = IB_NOT_FOUND;
			goto Exit;
		}
	}

	/*
	   p_physp now points to the destination
	 */
	p_pi = &p_physp->port_info;

	if (mtu > ib_port_info_get_mtu_cap(p_pi))
		mtu = ib_port_info_get_mtu_cap(p_pi);

	extended = p_pi->capability_mask & IB_PORT_CAP_HAS_EXT_SPEEDS;
	dest_rate = ib_port_info_compute_rate(p_pi, extended);
	if (ib_path_compare_rates(rate, dest_rate) > 0)
		rate = dest_rate;

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"Path min MTU = %u, min rate = %u\n", mtu, rate);

	/*
	 * Get QoS Level object according to the path request
	 * and adjust path parameters according to QoS settings
	 */
	if (sa->p_subn->opt.qos &&
	    sa->p_subn->p_qos_policy &&
	    (p_qos_level =
	     osm_qos_policy_get_qos_level_by_pr(sa->p_subn->p_qos_policy,
						p_pr, p_src_physp, p_dest_physp,
						comp_mask))) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"PathRecord request matches QoS Level '%s' (%s)\n",
			p_qos_level->name, p_qos_level->use ?
			p_qos_level->use : "no description");

		if (p_qos_level->mtu_limit_set
		    && (mtu > p_qos_level->mtu_limit))
			mtu = p_qos_level->mtu_limit;

		if (p_qos_level->rate_limit_set
		    && (ib_path_compare_rates(rate, p_qos_level->rate_limit) > 0))
			rate = p_qos_level->rate_limit;

		if (p_qos_level->sl_set) {
			sl = p_qos_level->sl;
			if (!(valid_sl_mask & (1 << sl))) {
				status = IB_NOT_FOUND;
				goto Exit;
			}
		}
	}

	/*
	 * Set packet lifetime.
	 * According to spec definition IBA 1.2 Table 205
	 * PacketLifeTime description, for loopback paths,
	 * packetLifeTime shall be zero.
	 */
	if (p_src_alias_guid->p_base_port == p_dest_alias_guid->p_base_port)
		pkt_life = 0;
	else if (p_qos_level && p_qos_level->pkt_life_set)
		pkt_life = p_qos_level->pkt_life;
	else
		pkt_life = sa->p_subn->opt.subnet_timeout;

	/*
	   Determine if these values meet the user criteria
	   and adjust appropriately
	 */

	/* we silently ignore cases where only the MTU selector is defined */
	if ((comp_mask & IB_PR_COMPMASK_MTUSELEC) &&
	    (comp_mask & IB_PR_COMPMASK_MTU)) {
		required_mtu = ib_path_rec_mtu(p_pr);
		switch (ib_path_rec_mtu_sel(p_pr)) {
		case 0:	/* must be greater than */
			if (mtu <= required_mtu)
				status = IB_NOT_FOUND;
			break;

		case 1:	/* must be less than */
			if (mtu >= required_mtu) {
				/* adjust to use the highest mtu
				   lower than the required one */
				if (required_mtu > 1)
					mtu = required_mtu - 1;
				else
					status = IB_NOT_FOUND;
			}
			break;

		case 2:	/* exact match */
			if (mtu < required_mtu)
				status = IB_NOT_FOUND;
			else
				mtu = required_mtu;
			break;

		case 3:	/* largest available */
			/* can't be disqualified by this one */
			break;

		default:
			/* if we're here, there's a bug in ib_path_rec_mtu_sel() */
			CL_ASSERT(FALSE);
			status = IB_ERROR;
			break;
		}
	}
	if (status != IB_SUCCESS)
		goto Exit;

	/* we silently ignore cases where only the Rate selector is defined */
	if ((comp_mask & IB_PR_COMPMASK_RATESELEC) &&
	    (comp_mask & IB_PR_COMPMASK_RATE)) {
		required_rate = ib_path_rec_rate(p_pr);
		switch (ib_path_rec_rate_sel(p_pr)) {
		case 0:	/* must be greater than */
			if (ib_path_compare_rates(rate, required_rate) <= 0)
				status = IB_NOT_FOUND;
			break;

		case 1:	/* must be less than */
			if (ib_path_compare_rates(rate, required_rate) >= 0) {
				/* adjust the rate to use the highest rate
				   lower than the required one */
				rate = ib_path_rate_get_prev(required_rate);
				if (!rate)
					status = IB_NOT_FOUND;
			}
			break;

		case 2:	/* exact match */
			if (ib_path_compare_rates(rate, required_rate))
				status = IB_NOT_FOUND;
			else
				rate = required_rate;
			break;

		case 3:	/* largest available */
			/* can't be disqualified by this one */
			break;

		default:
			/* if we're here, there's a bug in ib_path_rec_mtu_sel() */
			CL_ASSERT(FALSE);
			status = IB_ERROR;
			break;
		}
	}
	if (status != IB_SUCCESS)
		goto Exit;

	/* we silently ignore cases where only the PktLife selector is defined */
	if ((comp_mask & IB_PR_COMPMASK_PKTLIFETIMESELEC) &&
	    (comp_mask & IB_PR_COMPMASK_PKTLIFETIME)) {
		required_pkt_life = ib_path_rec_pkt_life(p_pr);
		switch (ib_path_rec_pkt_life_sel(p_pr)) {
		case 0:	/* must be greater than */
			if (pkt_life <= required_pkt_life)
				status = IB_NOT_FOUND;
			break;

		case 1:	/* must be less than */
			if (pkt_life >= required_pkt_life) {
				/* adjust the lifetime to use the highest possible
				   lower than the required one */
				if (required_pkt_life > 1)
					pkt_life = required_pkt_life - 1;
				else
					status = IB_NOT_FOUND;
			}
			break;

		case 2:	/* exact match */
			if (pkt_life < required_pkt_life)
				status = IB_NOT_FOUND;
			else
				pkt_life = required_pkt_life;
			break;

		case 3:	/* smallest available */
			/* can't be disqualified by this one */
			break;

		default:
			/* if we're here, there's a bug in ib_path_rec_pkt_life_sel() */
			CL_ASSERT(FALSE);
			status = IB_ERROR;
			break;
		}
	}

	if (status != IB_SUCCESS)
		goto Exit;

	/*
	 * set Pkey for this path record request
	 */

	if ((comp_mask & IB_PR_COMPMASK_RAWTRAFFIC) &&
	    (cl_ntoh32(p_pr->hop_flow_raw) & (1 << 31)))
		pkey = osm_physp_find_common_pkey(p_src_physp, p_dest_physp,
						  sa->p_subn->opt.allow_both_pkeys);

	else if (comp_mask & IB_PR_COMPMASK_PKEY) {
		/*
		 * PR request has a specific pkey:
		 * Check that source and destination share this pkey.
		 * If QoS level has pkeys, check that this pkey exists
		 * in the QoS level pkeys.
		 * PR returned pkey is the requested pkey.
		 */
		pkey = p_pr->pkey;
		if (!osm_physp_share_this_pkey(p_src_physp, p_dest_physp, pkey,
					       sa->p_subn->opt.allow_both_pkeys)) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F1A: "
				"Ports 0x%016" PRIx64 " (%s port %d) and "
				"0x%016" PRIx64 " (%s port %d) "
				"do not share specified PKey 0x%04x\n",
				cl_ntoh64(osm_physp_get_port_guid(p_src_physp)),
				p_src_physp->p_node->print_desc,
				p_src_physp->port_num,
				cl_ntoh64(osm_physp_get_port_guid
					  (p_dest_physp)),
				p_dest_physp->p_node->print_desc,
				p_dest_physp->port_num,
				cl_ntoh16(pkey));
			status = IB_NOT_FOUND;
			goto Exit;
		}
		if (p_qos_level && p_qos_level->pkey_range_len &&
		    !osm_qos_level_has_pkey(p_qos_level, pkey)) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F1D: "
				"QoS level \"%s\" doesn't define specified PKey 0x%04x "
				"for ports 0x%016" PRIx64 " (%s port %d) and "
				"0x%016"PRIx64" (%s port %d)\n",
				p_qos_level->name,
				cl_ntoh16(pkey),
				cl_ntoh64(osm_physp_get_port_guid(p_src_physp)),
				p_src_physp->p_node->print_desc,
				p_src_alias_guid->p_base_port->p_physp->port_num,
				cl_ntoh64(osm_physp_get_port_guid
					  (p_dest_physp)),
				p_dest_physp->p_node->print_desc,
				p_dest_alias_guid->p_base_port->p_physp->port_num);
			status = IB_NOT_FOUND;
			goto Exit;
		}

	} else if (p_qos_level && p_qos_level->pkey_range_len) {
		/*
		 * PR request doesn't have a specific pkey, but QoS level
		 * has pkeys - get shared pkey from QoS level pkeys
		 */
		pkey = osm_qos_level_get_shared_pkey(p_qos_level,
						     p_src_physp, p_dest_physp,
						     sa->p_subn->opt.allow_both_pkeys);
		if (!pkey) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F1E: "
				"Ports 0x%016" PRIx64 " (%s) and "
				"0x%016" PRIx64 " (%s) do not share "
				"PKeys defined by QoS level \"%s\"\n",
				cl_ntoh64(osm_physp_get_port_guid(p_src_physp)),
				p_src_physp->p_node->print_desc,
				cl_ntoh64(osm_physp_get_port_guid
					  (p_dest_physp)),
				p_dest_physp->p_node->print_desc,
				p_qos_level->name);
			status = IB_NOT_FOUND;
			goto Exit;
		}
	} else {
		/*
		 * Neither PR request nor QoS level have pkey.
		 * Just get any shared pkey.
		 */
		pkey = osm_physp_find_common_pkey(p_src_physp, p_dest_physp,
						  sa->p_subn->opt.allow_both_pkeys);
		if (!pkey) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F1B: "
				"Ports src 0x%016"PRIx64" (%s port %d) and "
				"dst 0x%016"PRIx64" (%s port %d) do not have "
				"any shared PKeys\n",
				cl_ntoh64(osm_physp_get_port_guid(p_src_physp)),
				p_src_physp->p_node->print_desc,
				p_src_physp->port_num,
				cl_ntoh64(osm_physp_get_port_guid
					  (p_dest_physp)),
				p_dest_physp->p_node->print_desc,
				p_dest_physp->port_num);
			status = IB_NOT_FOUND;
			goto Exit;
		}
	}

	if (pkey) {
		p_prtn =
		    (osm_prtn_t *) cl_qmap_get(&sa->p_subn->prtn_pkey_tbl,
					       pkey & cl_hton16((uint16_t) ~
								0x8000));
		if (p_prtn ==
		    (osm_prtn_t *) cl_qmap_end(&sa->p_subn->prtn_pkey_tbl))
			p_prtn = NULL;
	}

	/*
	 * Set PathRecord SL
	 */

	if (comp_mask & IB_PR_COMPMASK_SL) {
		/*
		 * Specific SL was requested
		 */
		sl = ib_path_rec_sl(p_pr);

		if (p_qos_level && p_qos_level->sl_set
		    && (p_qos_level->sl != sl)) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F1F: "
				"QoS constraints: required PathRecord SL (%u) "
				"doesn't match QoS policy \"%s\" SL (%u) "
				"[%s port %d <-> %s port %d]\n", sl,
				p_qos_level->name,
				p_qos_level->sl,
				p_src_alias_guid->p_base_port->p_node->print_desc,
				p_src_alias_guid->p_base_port->p_physp->port_num,
				p_dest_alias_guid->p_base_port->p_node->print_desc,
				p_dest_alias_guid->p_base_port->p_physp->port_num);
			status = IB_NOT_FOUND;
			goto Exit;
		}

	} else if (p_qos_level && p_qos_level->sl_set) {
		/*
		 * No specific SL was requested, but there is an SL in
		 * QoS level.
		 */
		sl = p_qos_level->sl;

		if (pkey && p_prtn && p_prtn->sl != p_qos_level->sl)
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"QoS level SL (%u) overrides partition SL (%u)\n",
				p_qos_level->sl, p_prtn->sl);

	} else if (pkey) {
		/*
		 * No specific SL in request or in QoS level - use partition SL
		 */
		if (!p_prtn) {
			sl = OSM_DEFAULT_SL;
			/* this may be possible when pkey tables are created somehow in
			   previous runs or things are going wrong here */
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F1C: "
				"No partition found for PKey 0x%04x - "
				"using default SL %d "
				"[%s port %d <-> %s port %d]\n",
				cl_ntoh16(pkey), sl,
				p_src_alias_guid->p_base_port->p_node->print_desc,
				p_src_alias_guid->p_base_port->p_physp->port_num,
				p_dest_alias_guid->p_base_port->p_node->print_desc,
				p_dest_alias_guid->p_base_port->p_physp->port_num);
		} else
			sl = p_prtn->sl;
	} else if (sa->p_subn->opt.qos) {
		if (valid_sl_mask & (1 << OSM_DEFAULT_SL))
			sl = OSM_DEFAULT_SL;
		else {
			for (i = 0; i < IB_MAX_NUM_VLS; i++)
				if (valid_sl_mask & (1 << i))
					break;
			sl = i;
		}
	} else
		sl = OSM_DEFAULT_SL;

	if (sa->p_subn->opt.qos && !(valid_sl_mask & (1 << sl))) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F24: "
			"Selected SL (%u) leads to VL15 "
			"[%s port %d <-> %s port %d]\n",
			sl,
			p_src_alias_guid->p_base_port->p_node->print_desc,
			p_src_alias_guid->p_base_port->p_physp->port_num,
			p_dest_alias_guid->p_base_port->p_node->print_desc,
			p_dest_alias_guid->p_base_port->p_physp->port_num);
		status = IB_NOT_FOUND;
		goto Exit;
	}

	/*
	 * If the routing engine wants to have a say in path SL selection,
	 * send the currently computed SL value as a hint and let the routing
	 * engine override it.
	 */
	if (p_re && p_re->path_sl) {
		uint8_t pr_sl;
		pr_sl = sl;

		sl = p_re->path_sl(p_re->context, sl,
				   cl_hton16(src_lid_ho), cl_hton16(dest_lid_ho));

		if ((comp_mask & IB_PR_COMPMASK_SL) && (sl != pr_sl)) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F2A: "
				"Requested SL (%u) doesn't match SL calculated"
				"by routing engine (%u) "
				"[%s port %d <-> %s port %d]\n",
				pr_sl,
				sl,
				p_src_alias_guid->p_base_port->p_node->print_desc,
				p_src_alias_guid->p_base_port->p_physp->port_num,
				p_dest_alias_guid->p_base_port->p_node->print_desc,
				p_dest_alias_guid->p_base_port->p_physp->port_num);
			status = IB_NOT_FOUND;
			goto Exit;
		}
	}
	/* reset pkey when raw traffic */
	if (comp_mask & IB_PR_COMPMASK_RAWTRAFFIC &&
	    cl_ntoh32(p_pr->hop_flow_raw) & (1 << 31))
		pkey = 0;

	p_parms->mtu = mtu;
	p_parms->rate = rate;
	p_parms->pkt_life = pkt_life;
	p_parms->pkey = pkey;
	p_parms->sl = sl;

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Path params: mtu = %u, rate = %u,"
		" packet lifetime = %u, pkey = 0x%04X, sl = %u\n",
		mtu, rate, pkt_life, cl_ntoh16(pkey), sl);
Exit:
	OSM_LOG_EXIT(sa->p_log);
	return status;
}

ib_api_status_t osm_get_path_params(IN osm_sa_t * sa,
				    IN const osm_port_t * p_src_port,
				    IN const uint16_t slid_ho,
				    IN const osm_port_t * p_dest_port,
				    IN const uint16_t dlid_ho,
				    OUT osm_path_parms_t * p_parms)
{
	osm_alias_guid_t *p_src_alias_guid, *p_dest_alias_guid;
	ib_path_rec_t pr;

	if (!p_src_port || !slid_ho || !p_dest_port || !dlid_ho)
		return IB_INVALID_PARAMETER;

	memset(&pr, 0, sizeof(ib_path_rec_t));

	p_src_alias_guid = osm_get_alias_guid_by_guid(sa->p_subn,
						      osm_port_get_guid(p_src_port));
	p_dest_alias_guid = osm_get_alias_guid_by_guid(sa->p_subn,
						       osm_port_get_guid(p_dest_port));
	return pr_rcv_get_path_parms(sa, &pr,
				     p_src_alias_guid, slid_ho,
				     p_dest_alias_guid, dlid_ho, 0, p_parms);
}

static void pr_rcv_build_pr(IN osm_sa_t * sa,
			    IN const osm_alias_guid_t * p_src_alias_guid,
			    IN const osm_alias_guid_t * p_dest_alias_guid,
			    IN const ib_gid_t * p_sgid,
			    IN const ib_gid_t * p_dgid,
			    IN const uint16_t src_lid_ho,
			    IN const uint16_t dest_lid_ho,
			    IN const uint8_t preference,
			    IN const osm_path_parms_t * p_parms,
			    OUT ib_path_rec_t * p_pr)
{
	const osm_physp_t *p_src_physp, *p_dest_physp;

	OSM_LOG_ENTER(sa->p_log);

	if (p_dgid)
		p_pr->dgid = *p_dgid;
	else {
		p_dest_physp = p_dest_alias_guid->p_base_port->p_physp;

		p_pr->dgid.unicast.prefix =
		    osm_physp_get_subnet_prefix(p_dest_physp);
		p_pr->dgid.unicast.interface_id = p_dest_alias_guid->alias_guid;
	}
	if (p_sgid)
		p_pr->sgid = *p_sgid;
	else {
		p_src_physp = p_src_alias_guid->p_base_port->p_physp;

		p_pr->sgid.unicast.prefix = osm_physp_get_subnet_prefix(p_src_physp);
		p_pr->sgid.unicast.interface_id = p_src_alias_guid->alias_guid;
	}

	p_pr->dlid = cl_hton16(dest_lid_ho);
	p_pr->slid = cl_hton16(src_lid_ho);

	p_pr->hop_flow_raw &= cl_hton32(1 << 31);

	/* Only set HopLimit if going through a router */
	if (p_dgid)
		p_pr->hop_flow_raw |= cl_hton32(IB_HOPLIMIT_MAX);

	p_pr->pkey = p_parms->pkey;
	ib_path_rec_set_sl(p_pr, p_parms->sl);
	ib_path_rec_set_qos_class(p_pr, 0);
	p_pr->mtu = (uint8_t) (p_parms->mtu | 0x80);
	p_pr->rate = (uint8_t) (p_parms->rate | 0x80);

	/* According to 1.2 spec definition Table 205 PacketLifeTime description,
	   for loopback paths, packetLifeTime shall be zero. */
	if (p_src_alias_guid->p_base_port == p_dest_alias_guid->p_base_port)
		p_pr->pkt_life = 0x80;	/* loopback */
	else
		p_pr->pkt_life = (uint8_t) (p_parms->pkt_life | 0x80);

	p_pr->preference = preference;

	/* always return num_path = 0 so this is only the reversible component */
	if (p_parms->reversible)
		p_pr->num_path = 0x80;

	OSM_LOG_EXIT(sa->p_log);
}

static osm_sa_item_t *pr_rcv_get_lid_pair_path(IN osm_sa_t * sa,
					       IN const ib_path_rec_t * p_pr,
					       IN const osm_alias_guid_t * p_src_alias_guid,
					       IN const osm_alias_guid_t * p_dest_alias_guid,
					       IN const ib_gid_t * p_sgid,
					       IN const ib_gid_t * p_dgid,
					       IN const uint16_t src_lid_ho,
					       IN const uint16_t dest_lid_ho,
					       IN const ib_net64_t comp_mask,
					       IN const uint8_t preference)
{
	osm_path_parms_t path_parms;
	osm_path_parms_t rev_path_parms;
	osm_sa_item_t *p_pr_item;
	ib_api_status_t status, rev_path_status;

	OSM_LOG_ENTER(sa->p_log);

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Src LID %u, Dest LID %u\n",
		src_lid_ho, dest_lid_ho);

	p_pr_item = malloc(SA_PR_RESP_SIZE);
	if (p_pr_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F01: "
			"Unable to allocate path record\n");
		goto Exit;
	}
	memset(p_pr_item, 0, SA_PR_RESP_SIZE);

	status = pr_rcv_get_path_parms(sa, p_pr, p_src_alias_guid, src_lid_ho,
				       p_dest_alias_guid, dest_lid_ho,
				       comp_mask, &path_parms);

	if (status != IB_SUCCESS) {
		free(p_pr_item);
		p_pr_item = NULL;
		goto Exit;
	}

	/* now try the reversible path */
	rev_path_status = pr_rcv_get_path_parms(sa, p_pr, p_dest_alias_guid,
						dest_lid_ho, p_src_alias_guid,
						src_lid_ho, comp_mask,
						&rev_path_parms);

	path_parms.reversible = (rev_path_status == IB_SUCCESS);

	/* did we get a Reversible Path compmask ? */
	/*
	   NOTE that if the reversible component = 0, it is a don't care
	   rather than requiring non-reversible paths ...
	   see Vol1 Ver1.2 p900 l16
	 */
	if ((comp_mask & IB_PR_COMPMASK_REVERSIBLE) &&
	    !path_parms.reversible && (p_pr->num_path & 0x80)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Requested reversible path but failed to get one\n");
		free(p_pr_item);
		p_pr_item = NULL;
		goto Exit;
	}

	pr_rcv_build_pr(sa, p_src_alias_guid, p_dest_alias_guid, p_sgid, p_dgid,
			src_lid_ho, dest_lid_ho, preference, &path_parms,
			&p_pr_item->resp.path_rec);

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return p_pr_item;
}

static void pr_rcv_get_port_pair_paths(IN osm_sa_t * sa,
				       IN const ib_sa_mad_t *sa_mad,
				       IN const osm_port_t * p_req_port,
				       IN const osm_alias_guid_t * p_src_alias_guid,
				       IN const osm_alias_guid_t * p_dest_alias_guid,
				       IN const ib_gid_t * p_sgid,
				       IN const ib_gid_t * p_dgid,
				       IN cl_qlist_t * p_list)
{
	const ib_path_rec_t *p_pr = ib_sa_mad_get_payload_ptr(sa_mad);
	ib_net64_t comp_mask = sa_mad->comp_mask;
	osm_sa_item_t *p_pr_item;
	uint16_t src_lid_min_ho;
	uint16_t src_lid_max_ho;
	uint16_t dest_lid_min_ho;
	uint16_t dest_lid_max_ho;
	uint16_t src_lid_ho;
	uint16_t dest_lid_ho;
	uint32_t path_num;
	uint8_t preference;
	unsigned iterations, src_offset, dest_offset;

	OSM_LOG_ENTER(sa->p_log);

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"Src port 0x%016" PRIx64 ", Dst port 0x%016" PRIx64 "\n",
		cl_ntoh64(p_src_alias_guid->alias_guid),
		cl_ntoh64(p_dest_alias_guid->alias_guid));

	/* Check that the req_port, src_port and dest_port all share a
	   pkey. The check is done on the default physical port of the ports. */
	if (osm_port_share_pkey(sa->p_log, p_req_port,
				p_src_alias_guid->p_base_port,
				sa->p_subn->opt.allow_both_pkeys) == FALSE
	    || osm_port_share_pkey(sa->p_log, p_req_port,
				   p_dest_alias_guid->p_base_port,
				   sa->p_subn->opt.allow_both_pkeys) == FALSE
	    || osm_port_share_pkey(sa->p_log, p_src_alias_guid->p_base_port,
				   p_dest_alias_guid->p_base_port,
				   sa->p_subn->opt.allow_both_pkeys) == FALSE)
		/* One of the pairs doesn't share a pkey so the path is disqualified. */
		goto Exit;

	/*
	   We shouldn't be here if the paths are disqualified in some way...
	   Thus, we assume every possible connection is valid.

	   We desire to return high-quality paths first.
	   In OpenSM, higher quality means least overlap with other paths.
	   This is acheived in practice by returning paths with
	   different LID value on each end, which means these
	   paths are more redundant that paths with the same LID repeated
	   on one side.  For example, in OpenSM the paths between two
	   endpoints with LMC = 1 might be as follows:

	   Port A, LID 1 <-> Port B, LID 3
	   Port A, LID 1 <-> Port B, LID 4
	   Port A, LID 2 <-> Port B, LID 3
	   Port A, LID 2 <-> Port B, LID 4

	   The OpenSM unicast routing algorithms attempt to disperse each path
	   to as varied a physical path as is reasonable.  1<->3 and 1<->4 have
	   more physical overlap (hence less redundancy) than 1<->3 and 2<->4.

	   OpenSM ranks paths in three preference groups:

	   Preference Value    Description
	   ----------------    -------------------------------------------
	   0             Redundant in both directions with other
	   pref value = 0 paths

	   1             Redundant in one direction with other
	   pref value = 0 and pref value = 1 paths

	   2             Not redundant in either direction with
	   other paths

	   3-FF          Unused

	   SA clients don't need to know these details, only that the lower
	   preference paths are preferred, as stated in the spec.  The paths
	   may not actually be physically redundant depending on the topology
	   of the subnet, but the point of LMC > 0 is to offer redundancy,
	   so it is assumed that the subnet is physically appropriate for the
	   specified LMC value.  A more advanced implementation would inspect for
	   physical redundancy, but I'm not going to bother with that now.
	 */

	/*
	   Refine our search if the client specified end-point LIDs
	 */
	if (comp_mask & IB_PR_COMPMASK_DLID)
		dest_lid_max_ho = dest_lid_min_ho = cl_ntoh16(p_pr->dlid);
	else
		osm_port_get_lid_range_ho(p_dest_alias_guid->p_base_port,
					  &dest_lid_min_ho, &dest_lid_max_ho);

	if (comp_mask & IB_PR_COMPMASK_SLID)
		src_lid_max_ho = src_lid_min_ho = cl_ntoh16(p_pr->slid);
	else
		osm_port_get_lid_range_ho(p_src_alias_guid->p_base_port,
					  &src_lid_min_ho, &src_lid_max_ho);

	if (src_lid_min_ho == 0) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Obtained source LID of 0. No such LID possible "
			"(%s port %d)\n",
			p_src_alias_guid->p_base_port->p_node->print_desc,
			p_src_alias_guid->p_base_port->p_physp->port_num);
		goto Exit;
	}

	if (dest_lid_min_ho == 0) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Obtained destination LID of 0. No such LID possible "
			"(%s port %d)\n",
			p_dest_alias_guid->p_base_port->p_node->print_desc,
			p_dest_alias_guid->p_base_port->p_physp->port_num);
		goto Exit;
	}

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"Src LIDs [%u-%u], Dest LIDs [%u-%u]\n",
		src_lid_min_ho, src_lid_max_ho,
		dest_lid_min_ho, dest_lid_max_ho);

	src_lid_ho = src_lid_min_ho;
	dest_lid_ho = dest_lid_min_ho;

	/*
	   Preferred paths come first in OpenSM
	 */
	preference = 0;
	path_num = 0;

	/* If SubnAdmGet, assume NumbPaths 1 (1.2 erratum) */
	if (sa_mad->method == IB_MAD_METHOD_GET)
		iterations = 1;
	else if (comp_mask & IB_PR_COMPMASK_NUMBPATH)
		iterations = ib_path_rec_num_path(p_pr);
	else
		iterations = (unsigned) (-1);

	while (path_num < iterations) {
		/*
		   These paths are "fully redundant"
		 */

		p_pr_item = pr_rcv_get_lid_pair_path(sa, p_pr, p_src_alias_guid,
						     p_dest_alias_guid,
						     p_sgid, p_dgid,
						     src_lid_ho, dest_lid_ho,
						     comp_mask, preference);

		if (p_pr_item) {
			cl_qlist_insert_tail(p_list, &p_pr_item->list_item);
			++path_num;
		}

		if (++src_lid_ho > src_lid_max_ho)
			break;

		if (++dest_lid_ho > dest_lid_max_ho)
			break;
	}

	/*
	   Check if we've accumulated all the paths that the user cares to see
	 */
	if (path_num == iterations)
		goto Exit;

	/*
	   Don't bother reporting preference 1 paths for now.
	   It's more trouble than it's worth and can only occur
	   if ports have different LMC values, which isn't supported
	   by OpenSM right now anyway.
	 */
	preference = 2;
	src_lid_ho = src_lid_min_ho;
	dest_lid_ho = dest_lid_min_ho;
	src_offset = 0;
	dest_offset = 0;

	/*
	   Iterate over the remaining paths
	 */
	while (path_num < iterations) {
		dest_offset++;
		dest_lid_ho++;

		if (dest_lid_ho > dest_lid_max_ho) {
			src_offset++;
			src_lid_ho++;

			if (src_lid_ho > src_lid_max_ho)
				break;	/* done */

			dest_offset = 0;
			dest_lid_ho = dest_lid_min_ho;
		}

		/*
		   These paths are "fully non-redundant" with paths already
		   identified above and consequently not of much value.

		   Don't return paths we already identified above, as indicated
		   by the offset values being equal.
		 */
		if (src_offset == dest_offset)
			continue;	/* already reported */

		p_pr_item = pr_rcv_get_lid_pair_path(sa, p_pr, p_src_alias_guid,
						     p_dest_alias_guid, p_sgid,
						     p_dgid, src_lid_ho,
						     dest_lid_ho, comp_mask,
						     preference);

		if (p_pr_item) {
			cl_qlist_insert_tail(p_list, &p_pr_item->list_item);
			++path_num;
		}
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

/* Find the router port that is configured to handle this prefix, if any */
static ib_net64_t find_router(const osm_sa_t *sa, ib_net64_t prefix)
{
	osm_prefix_route_t *route = NULL;
	osm_router_t *rtr;
	cl_qlist_t *l = &sa->p_subn->prefix_routes_list;
	cl_list_item_t *i;

	OSM_LOG(sa->p_log, OSM_LOG_VERBOSE, "Non local DGID subnet prefix "
		"0x%016" PRIx64 "\n", cl_ntoh64(prefix));

	for (i = cl_qlist_head(l); i != cl_qlist_end(l); i = cl_qlist_next(i)) {
		osm_prefix_route_t *r = (osm_prefix_route_t *)i;
		if (!r->prefix || r->prefix == prefix) {
			route = r;
			break;
		}
	}
	if (!route)
		return 0;

	if (route->guid == 0) /* first router */
		rtr = (osm_router_t *) cl_qmap_head(&sa->p_subn->rtr_guid_tbl);
	else
		rtr = (osm_router_t *) cl_qmap_get(&sa->p_subn->rtr_guid_tbl,
						   route->guid);

	if (rtr == (osm_router_t *) cl_qmap_end(&sa->p_subn->rtr_guid_tbl))
		return 0;

	return osm_port_get_guid(osm_router_get_port_ptr(rtr));
}

ib_net16_t osm_pr_get_end_points(IN osm_sa_t * sa,
				 IN const ib_sa_mad_t *sa_mad,
				 OUT const osm_alias_guid_t ** pp_src_alias_guid,
				 OUT const osm_alias_guid_t ** pp_dest_alias_guid,
				 OUT const osm_port_t ** pp_src_port,
				 OUT const osm_port_t ** pp_dest_port,
				 OUT const ib_gid_t ** pp_sgid,
				 OUT const ib_gid_t ** pp_dgid)
{
	const ib_path_rec_t *p_pr = ib_sa_mad_get_payload_ptr(sa_mad);
	ib_net64_t comp_mask = sa_mad->comp_mask;
	ib_net64_t dest_guid;
	ib_net16_t sa_status = IB_SA_MAD_STATUS_SUCCESS;

	OSM_LOG_ENTER(sa->p_log);

	/*
	   Determine what fields are valid and then get a pointer
	   to the source and destination port objects, if possible.
	 */

	/*
	   Check a few easy disqualifying cases up front before getting
	   into the endpoints.
	 */

	*pp_src_alias_guid = NULL;
	*pp_src_port = NULL;
	if (comp_mask & IB_PR_COMPMASK_SGID) {
		if (!ib_gid_is_link_local(&p_pr->sgid)) {
			if (ib_gid_get_subnet_prefix(&p_pr->sgid) !=
			    sa->p_subn->opt.subnet_prefix) {
				/*
				   This 'error' is the client's fault (bad gid)
				   so don't enter it as an error in our own log.
				   Return an error response to the client.
				 */
				OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
					"Non local SGID subnet prefix 0x%016"
					PRIx64 "\n",
					cl_ntoh64(p_pr->sgid.unicast.prefix));
				sa_status = IB_SA_MAD_STATUS_INVALID_GID;
				goto Exit;
			}
		}

		*pp_src_alias_guid = osm_get_alias_guid_by_guid(sa->p_subn,
								p_pr->sgid.unicast.interface_id);
		if (!*pp_src_alias_guid) {
			/*
			   This 'error' is the client's fault (bad gid) so
			   don't enter it as an error in our own log.
			   Return an error response to the client.
			 */
			OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
				"No source port with GUID 0x%016" PRIx64 "\n",
				cl_ntoh64(p_pr->sgid.unicast.interface_id));
			sa_status = IB_SA_MAD_STATUS_INVALID_GID;
			goto Exit;
		}
		if (pp_sgid)
			*pp_sgid = &p_pr->sgid;
	}

	if (comp_mask & IB_PR_COMPMASK_SLID) {
		*pp_src_port = osm_get_port_by_lid(sa->p_subn, p_pr->slid);
		if (!*pp_src_port) {
			/*
			   This 'error' is the client's fault (bad lid) so
			   don't enter it as an error in our own log.
			   Return an error response to the client.
			 */
			OSM_LOG(sa->p_log, OSM_LOG_VERBOSE, "No source port "
				"with LID %u\n", cl_ntoh16(p_pr->slid));
			sa_status = IB_SA_MAD_STATUS_NO_RECORDS;
			goto Exit;
		}
	}

	*pp_dest_alias_guid = NULL;
	*pp_dest_port = NULL;
	if (comp_mask & IB_PR_COMPMASK_DGID) {
		if (!ib_gid_is_link_local(&p_pr->dgid) &&
		    !ib_gid_is_multicast(&p_pr->dgid) &&
		    ib_gid_get_subnet_prefix(&p_pr->dgid) !=
		    sa->p_subn->opt.subnet_prefix) {
			dest_guid = find_router(sa, p_pr->dgid.unicast.prefix);
			if (!dest_guid) {
				char gid_str[INET6_ADDRSTRLEN];
				OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
					"Off subnet DGID %s, but router not "
					"found\n",
					inet_ntop(AF_INET6, p_pr->dgid.raw,
						  gid_str, sizeof(gid_str)));
				sa_status = IB_SA_MAD_STATUS_INVALID_GID;
				goto Exit;
			}
			if (pp_dgid)
				*pp_dgid = &p_pr->dgid;
		} else
			dest_guid = p_pr->dgid.unicast.interface_id;

		*pp_dest_alias_guid = osm_get_alias_guid_by_guid(sa->p_subn,
								 dest_guid);
		if (!*pp_dest_alias_guid) {
			/*
			   This 'error' is the client's fault (bad gid) so
			   don't enter it as an error in our own log.
			   Return an error response to the client.
			 */
			OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
				"No dest port with GUID 0x%016" PRIx64 "\n",
				cl_ntoh64(dest_guid));
			sa_status = IB_SA_MAD_STATUS_INVALID_GID;
			goto Exit;
		}
	}

	if (comp_mask & IB_PR_COMPMASK_DLID) {
		*pp_dest_port = osm_get_port_by_lid(sa->p_subn, p_pr->dlid);
		if (!*pp_dest_port) {
			/*
			   This 'error' is the client's fault (bad lid)
			   so don't enter it as an error in our own log.
			   Return an error response to the client.
			 */
			OSM_LOG(sa->p_log, OSM_LOG_VERBOSE, "No dest port "
				"with LID %u\n", cl_ntoh16(p_pr->dlid));
			sa_status = IB_SA_MAD_STATUS_NO_RECORDS;
			goto Exit;
		}
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return sa_status;
}

static void pr_rcv_process_world(IN osm_sa_t * sa, IN const ib_sa_mad_t * sa_mad,
				 IN const osm_port_t * requester_port,
				 IN const ib_gid_t * p_sgid,
				 IN const ib_gid_t * p_dgid,
				 IN cl_qlist_t * p_list)
{
	const cl_qmap_t *p_tbl;
	const osm_alias_guid_t *p_dest_alias_guid, *p_src_alias_guid;

	OSM_LOG_ENTER(sa->p_log);

	/*
	   Iterate the entire port space over itself.
	   A path record from a port to itself is legit, so no
	   need for a special case there.

	   We compute both A -> B and B -> A, since we don't have
	   any check to determine the reversability of the paths.
	 */
	p_tbl = &sa->p_subn->alias_port_guid_tbl;

	p_dest_alias_guid = (osm_alias_guid_t *) cl_qmap_head(p_tbl);
	while (p_dest_alias_guid != (osm_alias_guid_t *) cl_qmap_end(p_tbl)) {
		p_src_alias_guid = (osm_alias_guid_t *) cl_qmap_head(p_tbl);
		while (p_src_alias_guid != (osm_alias_guid_t *) cl_qmap_end(p_tbl)) {
			pr_rcv_get_port_pair_paths(sa, sa_mad, requester_port,
						   p_src_alias_guid,
						   p_dest_alias_guid,
						   p_sgid, p_dgid, p_list);
			if (sa_mad->method == IB_MAD_METHOD_GET &&
			    cl_qlist_count(p_list) > 0)
				goto Exit;

			p_src_alias_guid =
			    (osm_alias_guid_t *) cl_qmap_next(&p_src_alias_guid->map_item);
		}

		p_dest_alias_guid =
		    (osm_alias_guid_t *) cl_qmap_next(&p_dest_alias_guid->map_item);
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

void osm_pr_process_half(IN osm_sa_t * sa, IN const ib_sa_mad_t * sa_mad,
				IN const osm_port_t * requester_port,
				IN const osm_alias_guid_t * p_src_alias_guid,
				IN const osm_alias_guid_t * p_dest_alias_guid,
				IN const ib_gid_t * p_sgid,
				IN const ib_gid_t * p_dgid,
				IN cl_qlist_t * p_list)
{
	const cl_qmap_t *p_tbl;
	const osm_alias_guid_t *p_alias_guid;

	OSM_LOG_ENTER(sa->p_log);

	/*
	   Iterate over every port, looking for matches...
	   A path record from a port to itself is legit, so no
	   need to special case that one.
	 */
	p_tbl = &sa->p_subn->alias_port_guid_tbl;

	if (p_src_alias_guid) {
		/*
		   The src port if fixed, so iterate over destination ports.
		 */
		p_alias_guid = (osm_alias_guid_t *) cl_qmap_head(p_tbl);
		while (p_alias_guid != (osm_alias_guid_t *) cl_qmap_end(p_tbl)) {
			pr_rcv_get_port_pair_paths(sa, sa_mad, requester_port,
						   p_src_alias_guid,
						   p_alias_guid,
						   p_sgid, p_dgid, p_list);
			if (sa_mad->method == IB_MAD_METHOD_GET &&
			    cl_qlist_count(p_list) > 0)
				break;
			p_alias_guid = (osm_alias_guid_t *) cl_qmap_next(&p_alias_guid->map_item);
		}
	} else {
		/*
		   The dest port if fixed, so iterate over source ports.
		 */
		p_alias_guid = (osm_alias_guid_t *) cl_qmap_head(p_tbl);
		while (p_alias_guid != (osm_alias_guid_t *) cl_qmap_end(p_tbl)) {
			pr_rcv_get_port_pair_paths(sa, sa_mad, requester_port,
						   p_alias_guid,
						   p_dest_alias_guid, p_sgid,
						   p_dgid, p_list);
			if (sa_mad->method == IB_MAD_METHOD_GET &&
			    cl_qlist_count(p_list) > 0)
				break;
			p_alias_guid = (osm_alias_guid_t *) cl_qmap_next(&p_alias_guid->map_item);
		}
	}

	OSM_LOG_EXIT(sa->p_log);
}

void osm_pr_process_pair(IN osm_sa_t * sa, IN const ib_sa_mad_t * sa_mad,
				IN const osm_port_t * requester_port,
				IN const osm_alias_guid_t * p_src_alias_guid,
				IN const osm_alias_guid_t * p_dest_alias_guid,
				IN const ib_gid_t * p_sgid,
				IN const ib_gid_t * p_dgid,
				IN cl_qlist_t * p_list)
{
	OSM_LOG_ENTER(sa->p_log);

	pr_rcv_get_port_pair_paths(sa, sa_mad, requester_port, p_src_alias_guid,
				   p_dest_alias_guid, p_sgid, p_dgid, p_list);

	OSM_LOG_EXIT(sa->p_log);
}

static ib_api_status_t pr_match_mgrp_attributes(IN osm_sa_t * sa,
						IN const ib_sa_mad_t * sa_mad,
						IN const osm_mgrp_t * p_mgrp)
{
	const ib_path_rec_t *p_pr = ib_sa_mad_get_payload_ptr(sa_mad);
	ib_net64_t comp_mask = sa_mad->comp_mask;
	const osm_port_t *port;
	ib_api_status_t status = IB_ERROR;
	uint32_t flow_label;
	uint8_t sl, hop_limit;

	OSM_LOG_ENTER(sa->p_log);

	/* check that MLID of the MC group matches the PathRecord DLID */
	if ((comp_mask & IB_PR_COMPMASK_DLID) && p_mgrp->mlid != p_pr->dlid) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"DLID 0x%x is not MLID 0x%x for MC group\n",
			 cl_ntoh16(p_pr->dlid), cl_ntoh16(p_mgrp->mlid));
		goto Exit;
	}

	/* If SGID and/or SLID specified, should validate as member of MC group */
	if (comp_mask & IB_PR_COMPMASK_SGID) {
		if (!osm_mgrp_get_mcm_alias_guid(p_mgrp,
						 p_pr->sgid.unicast.interface_id)) {
			char gid_str[INET6_ADDRSTRLEN];
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"SGID %s is not a member of MC group\n",
				inet_ntop(AF_INET6, p_pr->sgid.raw,
					  gid_str, sizeof gid_str));
			goto Exit;
		}
	}

	if (comp_mask & IB_PR_COMPMASK_SLID) {
		port = osm_get_port_by_lid(sa->p_subn, p_pr->slid);
		if (!port || !osm_mgrp_get_mcm_port(p_mgrp, port->guid)) {
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"Either no port with SLID %u found or "
				"SLID not a member of MC group\n",
				cl_ntoh16(p_pr->slid));
			goto Exit;
		}
	}

	/* Also, MTU, rate, packet lifetime, and raw traffic requested are not currently checked */
	if ((comp_mask & IB_PR_COMPMASK_PKEY) &&
	    p_pr->pkey != p_mgrp->mcmember_rec.pkey) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Pkey 0x%x doesn't match MC group Pkey 0x%x\n",
			cl_ntoh16(p_pr->pkey),
			cl_ntoh16(p_mgrp->mcmember_rec.pkey));
		goto Exit;
	}

	ib_member_get_sl_flow_hop(p_mgrp->mcmember_rec.sl_flow_hop,
				  &sl, &flow_label, &hop_limit);

	if ((comp_mask & IB_PR_COMPMASK_SL) && ib_path_rec_sl(p_pr) != sl) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"SL %d doesn't match MC group SL %d\n",
			ib_path_rec_sl(p_pr), sl);
		goto Exit;
	}

	/* If SubnAdmGet, assume NumbPaths of 1 (1.2 erratum) */
	if ((comp_mask & IB_PR_COMPMASK_NUMBPATH) &&
	    sa_mad->method != IB_MAD_METHOD_GET &&
	    ib_path_rec_num_path(p_pr) == 0) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Number of paths requested is 0\n");
		goto Exit;
	}

	if ((comp_mask & IB_PR_COMPMASK_FLOWLABEL) &&
	    ib_path_rec_flow_lbl(p_pr) != flow_label) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Flow label 0x%x doesn't match MC group "
			" flow label 0x%x\n",
			ib_path_rec_flow_lbl(p_pr), flow_label);
		goto Exit;
	}

	if ((comp_mask & IB_PR_COMPMASK_HOPLIMIT) &&
	    ib_path_rec_hop_limit(p_pr) != hop_limit) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Hop limit %u doesn't match MC group hop limit %u\n",
			ib_path_rec_hop_limit(p_pr), hop_limit);
		goto Exit;
	}


	if ((comp_mask & IB_PR_COMPMASK_TCLASS) &&
	    p_pr->tclass != p_mgrp->mcmember_rec.tclass) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"TClass 0x%02x doesn't match MC group TClass 0x%02x\n",
			p_pr->tclass, p_mgrp->mcmember_rec.tclass);
		goto Exit;
	}

	status = IB_SUCCESS;

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return status;
}

static void pr_process_multicast(osm_sa_t * sa, const ib_sa_mad_t *sa_mad,
				 cl_qlist_t *list)
{
	ib_path_rec_t *pr = ib_sa_mad_get_payload_ptr(sa_mad);
	osm_mgrp_t *mgrp;
	ib_api_status_t status;
	osm_sa_item_t *pr_item;
	uint32_t flow_label;
	uint8_t sl, hop_limit;

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Multicast destination requested\n");

	mgrp = osm_get_mgrp_by_mgid(sa->p_subn, &pr->dgid);
	if (!mgrp) {
		char gid_str[INET6_ADDRSTRLEN];
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F09: "
			"No MC group found for PathRecord destination GID %s\n",
			inet_ntop(AF_INET6, pr->dgid.raw, gid_str,
				  sizeof gid_str));
		return;
	}

	/* Make sure the rest of the PathRecord matches the MC group attributes */
	status = pr_match_mgrp_attributes(sa, sa_mad, mgrp);
	if (status != IB_SUCCESS) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F19: "
			"MC group attributes don't match PathRecord request\n");
		return;
	}

	pr_item = malloc(SA_PR_RESP_SIZE);
	if (pr_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F18: "
			"Unable to allocate path record for MC group\n");
		return;
	}
	memset(pr_item, 0, sizeof(cl_list_item_t));

	/* Copy PathRecord request into response */
	pr_item->resp.path_rec = *pr;

	/* Now, use the MC info to cruft up the PathRecord response */
	pr_item->resp.path_rec.dgid = mgrp->mcmember_rec.mgid;
	pr_item->resp.path_rec.dlid = mgrp->mcmember_rec.mlid;
	pr_item->resp.path_rec.tclass = mgrp->mcmember_rec.tclass;
	pr_item->resp.path_rec.num_path = 1;
	pr_item->resp.path_rec.pkey = mgrp->mcmember_rec.pkey;

	/* MTU, rate, and packet lifetime should be exactly */
	pr_item->resp.path_rec.mtu = (IB_PATH_SELECTOR_EXACTLY << 6) | mgrp->mcmember_rec.mtu;
	pr_item->resp.path_rec.rate = (IB_PATH_SELECTOR_EXACTLY << 6) | mgrp->mcmember_rec.rate;
	pr_item->resp.path_rec.pkt_life = (IB_PATH_SELECTOR_EXACTLY << 6) | mgrp->mcmember_rec.pkt_life;

	/* SL, Hop Limit, and Flow Label */
	ib_member_get_sl_flow_hop(mgrp->mcmember_rec.sl_flow_hop,
				  &sl, &flow_label, &hop_limit);
	ib_path_rec_set_sl(&pr_item->resp.path_rec, sl);
	ib_path_rec_set_qos_class(&pr_item->resp.path_rec, 0);

	/* HopLimit is not yet set in non link local MC groups */
	/* If it were, this would not be needed */
	if (ib_mgid_get_scope(&mgrp->mcmember_rec.mgid) !=
	    IB_MC_SCOPE_LINK_LOCAL)
		hop_limit = IB_HOPLIMIT_MAX;

	pr_item->resp.path_rec.hop_flow_raw =
	    cl_hton32(hop_limit) | (flow_label << 8);

	cl_qlist_insert_tail(list, &pr_item->list_item);
}

void osm_pr_rcv_process(IN void *context, IN void *data)
{
	osm_sa_t *sa = context;
	osm_madw_t *p_madw = data;
	const ib_sa_mad_t *p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	ib_path_rec_t *p_pr = ib_sa_mad_get_payload_ptr(p_sa_mad);
	cl_qlist_t pr_list;
	const ib_gid_t *p_sgid = NULL, *p_dgid = NULL;
	const osm_alias_guid_t *p_src_alias_guid, *p_dest_alias_guid;
	const osm_port_t *p_src_port, *p_dest_port;
	osm_port_t *requester_port;
	uint8_t rate, mtu;

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	CL_ASSERT(p_sa_mad->attr_id == IB_MAD_ATTR_PATH_RECORD);

	/* we only support SubnAdmGet and SubnAdmGetTable methods */
	if (p_sa_mad->method != IB_MAD_METHOD_GET &&
	    p_sa_mad->method != IB_MAD_METHOD_GETTABLE) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F17: "
			"Unsupported Method (%s) for PathRecord request\n",
			ib_get_sa_method_str(p_sa_mad->method));
		osm_sa_send_error(sa, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		goto Exit;
	}

	/* Validate rate if supplied */
	if ((p_sa_mad->comp_mask & IB_PR_COMPMASK_RATESELEC) &&
	    (p_sa_mad->comp_mask & IB_PR_COMPMASK_RATE)) {
		rate = ib_path_rec_rate(p_pr);
		if (!ib_rate_is_valid(rate)) {
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
			goto Exit;
		}
	}
	/* Validate MTU if supplied */
	if ((p_sa_mad->comp_mask & IB_PR_COMPMASK_MTUSELEC) &&
	    (p_sa_mad->comp_mask & IB_PR_COMPMASK_MTU)) {
		mtu = ib_path_rec_mtu(p_pr);
		if (!ib_mtu_is_valid(mtu)) {
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
			goto Exit;
		}
	}

	/* Make sure either none or both ServiceID parameters are supplied */
	if ((p_sa_mad->comp_mask & IB_PR_COMPMASK_SERVICEID) != 0 &&
	    (p_sa_mad->comp_mask & IB_PR_COMPMASK_SERVICEID) !=
	     IB_PR_COMPMASK_SERVICEID) {
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_INSUF_COMPS);
		goto Exit;
	}

	cl_qlist_init(&pr_list);

	/*
	   Most SA functions (including this one) are read-only on the
	   subnet object, so we grab the lock non-exclusively.
	 */
	cl_plock_acquire(sa->p_lock);

	/* update the requester physical port */
	requester_port = osm_get_port_by_mad_addr(sa->p_log, sa->p_subn,
						  osm_madw_get_mad_addr_ptr
						  (p_madw));
	if (requester_port == NULL) {
		cl_plock_release(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1F16: "
			"Cannot find requester physical port\n");
		goto Exit;
	}

	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Requester port GUID 0x%" PRIx64 "\n",
			cl_ntoh64(osm_port_get_guid(requester_port)));
		osm_dump_path_record_v2(sa->p_log, p_pr, FILE_ID, OSM_LOG_DEBUG);
	}

	/* Handle multicast destinations separately */
	if ((p_sa_mad->comp_mask & IB_PR_COMPMASK_DGID) &&
	    ib_gid_is_multicast(&p_pr->dgid)) {
		pr_process_multicast(sa, p_sa_mad, &pr_list);
		goto Unlock;
	}

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Unicast destination requested\n");

	if (osm_pr_get_end_points(sa, p_sa_mad,
				  &p_src_alias_guid, &p_dest_alias_guid,
				  &p_src_port, &p_dest_port,
				  &p_sgid, &p_dgid) != IB_SA_MAD_STATUS_SUCCESS)
		goto Unlock;

	if (p_src_alias_guid && p_src_port &&
	    p_src_alias_guid->p_base_port != p_src_port) {
		cl_plock_release(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
			"Requester port GUID 0x%" PRIx64 ": Port for SGUID "
			"0x%" PRIx64 " not same as port for SLID %u\n",
			cl_ntoh64(osm_port_get_guid(requester_port)),
			cl_ntoh64(p_pr->sgid.unicast.interface_id),
			cl_ntoh16(p_pr->slid));
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	if (p_dest_alias_guid && p_dest_port &&
	    p_dest_alias_guid->p_base_port != p_dest_port) {
		cl_plock_release(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
			"Requester port GUID 0x%" PRIx64 ": Port for DGUID "
			"0x%" PRIx64 " not same as port for DLID %u\n",
			cl_ntoh64(osm_port_get_guid(requester_port)),
			cl_ntoh64(p_pr->dgid.unicast.interface_id),
			cl_ntoh16(p_pr->dlid));
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	/*
	   What happens next depends on the type of endpoint information
	   that was specified....
	 */
	if (p_src_alias_guid) {
		if (p_dest_alias_guid)
			osm_pr_process_pair(sa, p_sa_mad, requester_port,
					    p_src_alias_guid, p_dest_alias_guid,
					    p_sgid, p_dgid, &pr_list);
		else if (!p_dest_port)
			osm_pr_process_half(sa, p_sa_mad, requester_port,
					    p_src_alias_guid, NULL, p_sgid,
					    p_dgid, &pr_list);
		else {
			/* Get all alias GUIDs for the dest port */
			p_dest_alias_guid = (osm_alias_guid_t *) cl_qmap_head(&sa->p_subn->alias_port_guid_tbl);
			while (p_dest_alias_guid !=
			       (osm_alias_guid_t *) cl_qmap_end(&sa->p_subn->alias_port_guid_tbl)) {
				if (osm_get_port_by_alias_guid(sa->p_subn, p_dest_alias_guid->alias_guid) ==
				    p_dest_port)
					osm_pr_process_pair(sa, p_sa_mad,
							    requester_port,
							    p_src_alias_guid,
							    p_dest_alias_guid,
							    p_sgid, p_dgid,
							    &pr_list);
				if (p_sa_mad->method == IB_MAD_METHOD_GET &&
				    cl_qlist_count(&pr_list) > 0)
					break;

				p_dest_alias_guid = (osm_alias_guid_t *) cl_qmap_next(&p_dest_alias_guid->map_item);
			}
		}
	} else {
		if (p_dest_alias_guid && !p_src_port)
			osm_pr_process_half(sa, p_sa_mad, requester_port,
					    NULL, p_dest_alias_guid, p_sgid,
					    p_dgid, &pr_list);
		else if (!p_src_port && !p_dest_port)
			/*
			   Katie, bar the door!
			 */
			pr_rcv_process_world(sa, p_sa_mad, requester_port,
					     p_sgid, p_dgid, &pr_list);
		else if (p_dest_alias_guid && p_src_port) {
			/* Get all alias GUIDs for the src port */
			p_src_alias_guid = (osm_alias_guid_t *) cl_qmap_head(&sa->p_subn->alias_port_guid_tbl);
			while (p_src_alias_guid !=
			       (osm_alias_guid_t *) cl_qmap_end(&sa->p_subn->alias_port_guid_tbl)) {
				if (osm_get_port_by_alias_guid(sa->p_subn,
							       p_src_alias_guid->alias_guid) ==
				    p_src_port)
					osm_pr_process_pair(sa, p_sa_mad,
							    requester_port,
							    p_src_alias_guid,
							    p_dest_alias_guid,
							    p_sgid, p_dgid,
							    &pr_list);
				if (p_sa_mad->method == IB_MAD_METHOD_GET &&
				    cl_qlist_count(&pr_list) > 0)
					break;
				p_src_alias_guid = (osm_alias_guid_t *) cl_qmap_next(&p_src_alias_guid->map_item);
			}
		} else if (p_src_port && !p_dest_port) {
			/* Get all alias GUIDs for the src port */
			p_src_alias_guid = (osm_alias_guid_t *) cl_qmap_head(&sa->p_subn->alias_port_guid_tbl);
			while (p_src_alias_guid !=
			       (osm_alias_guid_t *) cl_qmap_end(&sa->p_subn->alias_port_guid_tbl)) {
				if (osm_get_port_by_alias_guid(sa->p_subn,
							       p_src_alias_guid->alias_guid) ==
				    p_src_port)
					osm_pr_process_half(sa, p_sa_mad,
							    requester_port,
							    p_src_alias_guid,
							    NULL, p_sgid,
							    p_dgid, &pr_list);
				p_src_alias_guid = (osm_alias_guid_t *) cl_qmap_next(&p_src_alias_guid->map_item);
			}
		} else if (p_dest_port && !p_src_port) {
			/* Get all alias GUIDs for the dest port */
			p_dest_alias_guid = (osm_alias_guid_t *) cl_qmap_head(&sa->p_subn->alias_port_guid_tbl);
			while (p_dest_alias_guid !=
			       (osm_alias_guid_t *) cl_qmap_end(&sa->p_subn->alias_port_guid_tbl)) {
				if (osm_get_port_by_alias_guid(sa->p_subn,
							       p_dest_alias_guid->alias_guid) ==
				    p_dest_port)
					osm_pr_process_half(sa, p_sa_mad,
							    requester_port,
							    NULL,
							    p_dest_alias_guid,
							    p_sgid, p_dgid,
							    &pr_list);
				p_dest_alias_guid = (osm_alias_guid_t *) cl_qmap_next(&p_dest_alias_guid->map_item);
			}
		} else {
			/* Get all alias GUIDs for the src port */
			p_src_alias_guid = (osm_alias_guid_t *) cl_qmap_head(&sa->p_subn->alias_port_guid_tbl);
			while (p_src_alias_guid !=
			       (osm_alias_guid_t *) cl_qmap_end(&sa->p_subn->alias_port_guid_tbl)) {
				if (osm_get_port_by_alias_guid(sa->p_subn,
							       p_src_alias_guid->alias_guid) ==
				    p_src_port) {
					/* Get all alias GUIDs for the dest port */
					p_dest_alias_guid = (osm_alias_guid_t *) cl_qmap_head(&sa->p_subn->alias_port_guid_tbl);
					while (p_dest_alias_guid !=
					       (osm_alias_guid_t *) cl_qmap_end(&sa->p_subn->alias_port_guid_tbl)) {
						if (osm_get_port_by_alias_guid(sa->p_subn,
									       p_dest_alias_guid->alias_guid) ==
						    p_dest_port)
						osm_pr_process_pair(sa,
								    p_sa_mad,
								    requester_port,
								    p_src_alias_guid,
								    p_dest_alias_guid,
								    p_sgid,
								    p_dgid,
								    &pr_list);
						if (p_sa_mad->method == IB_MAD_METHOD_GET &&
						    cl_qlist_count(&pr_list) > 0)
							break;
						p_dest_alias_guid = (osm_alias_guid_t *) cl_qmap_next(&p_dest_alias_guid->map_item);
					}
				}
				if (p_sa_mad->method == IB_MAD_METHOD_GET &&
				    cl_qlist_count(&pr_list) > 0)
					break;
				p_src_alias_guid = (osm_alias_guid_t *) cl_qmap_next(&p_src_alias_guid->map_item);
			}
		}
	}

Unlock:
	cl_plock_release(sa->p_lock);

	/* Now, (finally) respond to the PathRecord request */
	osm_sa_respond(sa, p_madw, sizeof(ib_path_rec_t), &pr_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}
