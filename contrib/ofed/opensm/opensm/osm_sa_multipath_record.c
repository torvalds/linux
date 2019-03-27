/*
 * Copyright (c) 2006-2009 Voltaire, Inc. All rights reserved.
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
 * 	Implementation of osm_mpr_rcv_t.
 *	This object represents the MultiPath Record Receiver object.
 *	This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#if defined (VENDOR_RMPP_SUPPORT) && defined (DUAL_SIDED_RMPP)

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <complib/cl_qlist.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_SA_MULTIPATH_RECORD_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_partition.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_qos_policy.h>
#include <opensm/osm_sa.h>

#define OSM_SA_MPR_MAX_NUM_PATH        127
#define MAX_HOPS 64

#define SA_MPR_RESP_SIZE SA_ITEM_RESP_SIZE(mpr_rec)

static boolean_t sa_multipath_rec_is_tavor_port(IN const osm_port_t * p_port)
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
sa_multipath_rec_apply_tavor_mtu_limit(IN const ib_multipath_rec_t * p_mpr,
				       IN const osm_port_t * p_src_port,
				       IN const osm_port_t * p_dest_port,
				       IN const ib_net64_t comp_mask)
{
	uint8_t required_mtu;

	/* only if at least one of the ports is a Tavor device */
	if (!sa_multipath_rec_is_tavor_port(p_src_port) &&
	    !sa_multipath_rec_is_tavor_port(p_dest_port))
		return FALSE;

	/*
	   we can apply the patch if either:
	   1. No MTU required
	   2. Required MTU <
	   3. Required MTU = 1K or 512 or 256
	   4. Required MTU > 256 or 512
	 */
	required_mtu = ib_multipath_rec_mtu(p_mpr);
	if ((comp_mask & IB_MPR_COMPMASK_MTUSELEC) &&
	    (comp_mask & IB_MPR_COMPMASK_MTU)) {
		switch (ib_multipath_rec_mtu_sel(p_mpr)) {
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
			break;

		default:
			/* if we're here, there's a bug in ib_multipath_rec_mtu_sel() */
			CL_ASSERT(FALSE);
			break;
		}
	}

	return TRUE;
}

static ib_api_status_t mpr_rcv_get_path_parms(IN osm_sa_t * sa,
					      IN const ib_multipath_rec_t *
					      p_mpr,
					      IN const osm_alias_guid_t * p_src_alias_guid,
					      IN const osm_alias_guid_t * p_dest_alias_guid,
					      IN const uint16_t src_lid_ho,
					      IN const uint16_t dest_lid_ho,
					      IN const ib_net64_t comp_mask,
					      OUT osm_path_parms_t * p_parms)
{
	const osm_node_t *p_node;
	const osm_physp_t *p_physp, *p_physp0;
	const osm_physp_t *p_src_physp;
	const osm_physp_t *p_dest_physp;
	const osm_prtn_t *p_prtn = NULL;
	const ib_port_info_t *p_pi, *p_pi0;
	ib_slvl_table_t *p_slvl_tbl;
	ib_api_status_t status = IB_SUCCESS;
	uint8_t mtu;
	uint8_t rate, p0_extended_rate, dest_rate;
	uint8_t pkt_life;
	uint8_t required_mtu;
	uint8_t required_rate;
	ib_net16_t required_pkey;
	uint8_t required_sl;
	uint8_t required_pkt_life;
	ib_net16_t dest_lid;
	int hops = 0;
	int in_port_num = 0;
	uint8_t i;
	osm_qos_level_t *p_qos_level = NULL;
	uint16_t valid_sl_mask = 0xffff;
	int extended, p0_extended;

	OSM_LOG_ENTER(sa->p_log);

	dest_lid = cl_hton16(dest_lid_ho);

	p_dest_physp = p_dest_alias_guid->p_base_port->p_physp;
	p_physp = p_src_alias_guid->p_base_port->p_physp;
	p_src_physp = p_physp;
	p_pi = &p_physp->port_info;

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
	    sa_multipath_rec_apply_tavor_mtu_limit(p_mpr,
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
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4514: "
				"Can't find routing from LID %u to LID %u on "
				"switch %s (GUID 0x%016" PRIx64 ")\n",
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
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4515: "
				"Can't find routing from LID %u to LID %u on "
				"switch %s (GUID 0x%016" PRIx64 ")\n",
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
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4505: "
				"Can't find remote phys port of %s (GUID "
				"0x%016" PRIx64 ") port %d "
				"while routing from LID %u to LID %u",
				p_node->print_desc,
				cl_ntoh64(osm_node_get_node_guid(p_node)),
				tmp_pnum, src_lid_ho, dest_lid_ho);
			status = IB_ERROR;
			goto Exit;
		}

		/* update number of hops traversed */
		hops++;
		if (hops > MAX_HOPS) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4520: "
				"Path from GUID 0x%016" PRIx64 " (%s) to"
				" lid %u GUID 0x%016" PRIx64 " (%s) needs"
				" more than %d hops, max %d hops allowed\n",
				cl_ntoh64(osm_physp_get_port_guid(p_src_physp)),
				p_src_physp->p_node->print_desc, dest_lid_ho,
				cl_ntoh64(osm_physp_get_port_guid
					  (p_dest_physp)),
				p_dest_physp->p_node->print_desc, hops,
				MAX_HOPS);
			status = IB_NOT_FOUND;
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
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4503: "
				"Internal error, bad path while routing "
				"from %s (GUID: 0x%016"PRIx64") port %d "
				"to %s (GUID: 0x%016"PRIx64") port %d; "
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
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4516: "
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
				OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
					"All the SLs lead to VL15 "
					"on this path\n");
				status = IB_NOT_FOUND;
				goto Exit;
			}
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
	 * Get QoS Level object according to the MultiPath request
	 * and adjust MultiPath parameters according to QoS settings
	 */
	if (sa->p_subn->opt.qos && sa->p_subn->p_qos_policy &&
	    (p_qos_level =
	     osm_qos_policy_get_qos_level_by_mpr(sa->p_subn->p_qos_policy,
						 p_mpr, p_src_physp,
						 p_dest_physp, comp_mask))) {

		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"MultiPathRecord request matches QoS Level '%s' (%s)\n",
			p_qos_level->name,
			p_qos_level->use ? p_qos_level->use : "no description");

		if (p_qos_level->mtu_limit_set
		    && (mtu > p_qos_level->mtu_limit))
			mtu = p_qos_level->mtu_limit;

		if (p_qos_level->rate_limit_set
		    && (ib_path_compare_rates(rate, p_qos_level->rate_limit) > 0))
			rate = p_qos_level->rate_limit;

		if (p_qos_level->sl_set) {
			required_sl = p_qos_level->sl;
			if (!(valid_sl_mask & (1 << required_sl))) {
				status = IB_NOT_FOUND;
				goto Exit;
			}
		}
	}

	/*
	   Determine if these values meet the user criteria
	 */

	/* we silently ignore cases where only the MTU selector is defined */
	if ((comp_mask & IB_MPR_COMPMASK_MTUSELEC) &&
	    (comp_mask & IB_MPR_COMPMASK_MTU)) {
		required_mtu = ib_multipath_rec_mtu(p_mpr);
		switch (ib_multipath_rec_mtu_sel(p_mpr)) {
		case 0:	/* must be greater than */
			if (mtu <= required_mtu)
				status = IB_NOT_FOUND;
			break;

		case 1:	/* must be less than */
			if (mtu >= required_mtu) {
				/* adjust to use the highest mtu
				   lower then the required one */
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
			/* if we're here, there's a bug in ib_multipath_rec_mtu_sel() */
			CL_ASSERT(FALSE);
			status = IB_ERROR;
			break;
		}
	}
	if (status != IB_SUCCESS)
		goto Exit;

	/* we silently ignore cases where only the Rate selector is defined */
	if ((comp_mask & IB_MPR_COMPMASK_RATESELEC) &&
	    (comp_mask & IB_MPR_COMPMASK_RATE)) {
		required_rate = ib_multipath_rec_rate(p_mpr);
		switch (ib_multipath_rec_rate_sel(p_mpr)) {
		case 0:	/* must be greater than */
			if (ib_path_compare_rates(rate, required_rate) <= 0)
				status = IB_NOT_FOUND;
			break;

		case 1:	/* must be less than */
			if (ib_path_compare_rates(rate, required_rate) >= 0) {
				/* adjust the rate to use the highest rate
				   lower then the required one */
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
			/* if we're here, there's a bug in ib_multipath_rec_mtu_sel() */
			CL_ASSERT(FALSE);
			status = IB_ERROR;
			break;
		}
	}
	if (status != IB_SUCCESS)
		goto Exit;

	/* Verify the pkt_life_time */
	/* According to spec definition IBA 1.2 Table 205 PacketLifeTime description,
	   for loopback paths, packetLifeTime shall be zero. */
	if (p_src_alias_guid->p_base_port == p_dest_alias_guid->p_base_port)
		pkt_life = 0;	/* loopback */
	else if (p_qos_level && p_qos_level->pkt_life_set)
		pkt_life = p_qos_level->pkt_life;
	else
		pkt_life = sa->p_subn->opt.subnet_timeout;

	/* we silently ignore cases where only the PktLife selector is defined */
	if ((comp_mask & IB_MPR_COMPMASK_PKTLIFETIMESELEC) &&
	    (comp_mask & IB_MPR_COMPMASK_PKTLIFETIME)) {
		required_pkt_life = ib_multipath_rec_pkt_life(p_mpr);
		switch (ib_multipath_rec_pkt_life_sel(p_mpr)) {
		case 0:	/* must be greater than */
			if (pkt_life <= required_pkt_life)
				status = IB_NOT_FOUND;
			break;

		case 1:	/* must be less than */
			if (pkt_life >= required_pkt_life) {
				/* adjust the lifetime to use the highest possible
				   lower then the required one */
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
	 * set Pkey for this MultiPath record request
	 */

	if (comp_mask & IB_MPR_COMPMASK_RAWTRAFFIC &&
	    cl_ntoh32(p_mpr->hop_flow_raw) & (1 << 31))
		required_pkey =
		    osm_physp_find_common_pkey(p_src_physp, p_dest_physp,
					       sa->p_subn->opt.allow_both_pkeys);

	else if (comp_mask & IB_MPR_COMPMASK_PKEY) {
		/*
		 * MPR request has a specific pkey:
		 * Check that source and destination share this pkey.
		 * If QoS level has pkeys, check that this pkey exists
		 * in the QoS level pkeys.
		 * MPR returned pkey is the requested pkey.
		 */
		required_pkey = p_mpr->pkey;
		if (!osm_physp_share_this_pkey
		    (p_src_physp, p_dest_physp, required_pkey,
		     sa->p_subn->opt.allow_both_pkeys)) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4518: "
				"Ports src 0x%016"PRIx64" (%s port %d) "
				"and dst 0x%016"PRIx64" (%s port %d) "
				"do not share the specified PKey 0x%04x\n",
				cl_ntoh64(osm_physp_get_port_guid(p_src_physp)),
				p_src_physp->p_node->print_desc,
				p_src_physp->port_num,
				cl_ntoh64(osm_physp_get_port_guid
					  (p_dest_physp)),
				p_dest_physp->p_node->print_desc,
				p_dest_physp->port_num,
				cl_ntoh16(required_pkey));
			status = IB_NOT_FOUND;
			goto Exit;
		}
		if (p_qos_level && p_qos_level->pkey_range_len &&
		    !osm_qos_level_has_pkey(p_qos_level, required_pkey)) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 451C: "
				"Ports src 0x%016"PRIx64" (%s port %d) "
				"and dst 0x%016"PRIx64" (%s port %d) "
				"do not share specified PKey (0x%04x) as "
				"defined by QoS level \"%s\"\n",
				cl_ntoh64(osm_physp_get_port_guid(p_src_physp)),
				p_src_physp->p_node->print_desc,
				p_src_physp->port_num,
				cl_ntoh64(osm_physp_get_port_guid
					  (p_dest_physp)),
				p_dest_physp->p_node->print_desc,
				p_dest_physp->port_num,
				cl_ntoh16(required_pkey),
				p_qos_level->name);
			status = IB_NOT_FOUND;
			goto Exit;
		}

	} else if (p_qos_level && p_qos_level->pkey_range_len) {
		/*
		 * MPR request doesn't have a specific pkey, but QoS level
		 * has pkeys - get shared pkey from QoS level pkeys
		 */
		required_pkey = osm_qos_level_get_shared_pkey(p_qos_level,
							      p_src_physp,
							      p_dest_physp,
							      sa->p_subn->opt.allow_both_pkeys);
		if (!required_pkey) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 451D: "
				"Ports src 0x%016"PRIx64" (%s port %d) "
				"and dst 0x%016"PRIx64" (%s port %d) "
				"do not share a PKey as defined by QoS "
				"level \"%s\"\n",
				cl_ntoh64(osm_physp_get_port_guid(p_src_physp)),
				p_src_physp->p_node->print_desc,
				p_src_physp->port_num,
				cl_ntoh64(osm_physp_get_port_guid
					  (p_dest_physp)),
				p_dest_physp->p_node->print_desc,
				p_dest_physp->port_num,
				p_qos_level->name);
			status = IB_NOT_FOUND;
			goto Exit;
		}

	} else {
		/*
		 * Neither MPR request nor QoS level have pkey.
		 * Just get any shared pkey.
		 */
		required_pkey =
		    osm_physp_find_common_pkey(p_src_physp, p_dest_physp,
					       sa->p_subn->opt.allow_both_pkeys);
		if (!required_pkey) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4519: "
				"Ports src 0x%016"PRIx64" (%s port %d) "
				"and dst 0x%016"PRIx64" (%s port %d) "
				"do not have any shared PKeys\n",
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

	if (required_pkey) {
		p_prtn =
		    (osm_prtn_t *) cl_qmap_get(&sa->p_subn->prtn_pkey_tbl,
					       required_pkey &
					       cl_ntoh16((uint16_t) ~ 0x8000));
		if (p_prtn ==
		    (osm_prtn_t *) cl_qmap_end(&sa->p_subn->prtn_pkey_tbl))
			p_prtn = NULL;
	}

	/*
	 * Set MultiPathRecord SL.
	 */

	if (comp_mask & IB_MPR_COMPMASK_SL) {
		/*
		 * Specific SL was requested
		 */
		required_sl = ib_multipath_rec_sl(p_mpr);

		if (p_qos_level && p_qos_level->sl_set &&
		    p_qos_level->sl != required_sl) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 451E: "
				"QoS constraints: required MultiPathRecord SL "
				"(%u) doesn't match QoS policy \"%s\" SL (%u) "
				"[%s port %d <-> %s port %d]\n", required_sl,
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
		 * No specific SL was requested,
		 * but there is an SL in QoS level.
		 */
		required_sl = p_qos_level->sl;

		if (required_pkey && p_prtn && p_prtn->sl != p_qos_level->sl)
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"QoS level SL (%u) overrides partition SL (%u)\n",
				p_qos_level->sl, p_prtn->sl);

	} else if (required_pkey) {
		/*
		 * No specific SL in request or in QoS level - use partition SL
		 */
		p_prtn =
		    (osm_prtn_t *) cl_qmap_get(&sa->p_subn->prtn_pkey_tbl,
					       required_pkey &
					       cl_ntoh16((uint16_t) ~ 0x8000));
		if (!p_prtn) {
			required_sl = OSM_DEFAULT_SL;
			/* this may be possible when pkey tables are created somehow in
			   previous runs or things are going wrong here */
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 451A: "
				"No partition found for PKey 0x%04x - "
				"using default SL %d "
				"[%s port %d <-> %s port %d]\n",
				cl_ntoh16(required_pkey), required_sl,
				p_src_alias_guid->p_base_port->p_node->print_desc,
				p_src_alias_guid->p_base_port->p_physp->port_num,
				p_dest_alias_guid->p_base_port->p_node->print_desc,
				p_dest_alias_guid->p_base_port->p_physp->port_num);
		} else
			required_sl = p_prtn->sl;

	} else if (sa->p_subn->opt.qos) {
		if (valid_sl_mask & (1 << OSM_DEFAULT_SL))
			required_sl = OSM_DEFAULT_SL;
		else {
			for (i = 0; i < IB_MAX_NUM_VLS; i++)
				if (valid_sl_mask & (1 << i))
					break;
			required_sl = i;
		}
	} else
		required_sl = OSM_DEFAULT_SL;

	if (sa->p_subn->opt.qos && !(valid_sl_mask & (1 << required_sl))) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 451F: "
			"Selected SL (%u) leads to VL15 "
			"[%s port %d <-> %s port %d]\n",
			required_sl,
			p_src_alias_guid->p_base_port->p_node->print_desc,
			p_src_alias_guid->p_base_port->p_physp->port_num,
			p_dest_alias_guid->p_base_port->p_node->print_desc,
			p_dest_alias_guid->p_base_port->p_physp->port_num);
		status = IB_NOT_FOUND;
		goto Exit;
	}

	/* reset pkey when raw traffic */
	if (comp_mask & IB_MPR_COMPMASK_RAWTRAFFIC &&
	    cl_ntoh32(p_mpr->hop_flow_raw) & (1 << 31))
		required_pkey = 0;

	p_parms->mtu = mtu;
	p_parms->rate = rate;
	p_parms->pkey = required_pkey;
	p_parms->pkt_life = pkt_life;
	p_parms->sl = required_sl;
	p_parms->hops = hops;

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "MultiPath params:"
		" mtu = %u, rate = %u, packet lifetime = %u,"
		" pkey = 0x%04X, sl = %u, hops = %u\n", mtu, rate,
		pkt_life, cl_ntoh16(required_pkey), required_sl, hops);

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return status;
}

static void mpr_rcv_build_pr(IN osm_sa_t * sa,
			     IN const osm_alias_guid_t * p_src_alias_guid,
			     IN const osm_alias_guid_t * p_dest_alias_guid,
			     IN uint16_t src_lid_ho, IN uint16_t dest_lid_ho,
			     IN uint8_t preference,
			     IN const osm_path_parms_t * p_parms,
			     OUT ib_path_rec_t * p_pr)
{
	const osm_physp_t *p_src_physp, *p_dest_physp;

	OSM_LOG_ENTER(sa->p_log);

	p_src_physp = p_src_alias_guid->p_base_port->p_physp;
	p_dest_physp = p_dest_alias_guid->p_base_port->p_physp;

	p_pr->dgid.unicast.prefix = osm_physp_get_subnet_prefix(p_dest_physp);
	p_pr->dgid.unicast.interface_id = p_dest_alias_guid->alias_guid;

	p_pr->sgid.unicast.prefix = osm_physp_get_subnet_prefix(p_src_physp);
	p_pr->sgid.unicast.interface_id = p_src_alias_guid->alias_guid;

	p_pr->dlid = cl_hton16(dest_lid_ho);
	p_pr->slid = cl_hton16(src_lid_ho);

	p_pr->hop_flow_raw &= cl_hton32(1 << 31);

	p_pr->pkey = p_parms->pkey;
	ib_path_rec_set_qos_class(p_pr, 0);
	ib_path_rec_set_sl(p_pr, p_parms->sl);
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

static osm_sa_item_t *mpr_rcv_get_lid_pair_path(IN osm_sa_t * sa,
						IN const ib_multipath_rec_t *
						p_mpr,
						IN const osm_alias_guid_t *
						p_src_alias_guid,
						IN const osm_alias_guid_t *
						p_dest_alias_guid,
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

	p_pr_item = malloc(SA_MPR_RESP_SIZE);
	if (p_pr_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4501: "
			"Unable to allocate path record\n");
		goto Exit;
	}
	memset(p_pr_item, 0, SA_MPR_RESP_SIZE);

	status = mpr_rcv_get_path_parms(sa, p_mpr, p_src_alias_guid,
					p_dest_alias_guid,
					src_lid_ho, dest_lid_ho,
					comp_mask, &path_parms);

	if (status != IB_SUCCESS) {
		free(p_pr_item);
		p_pr_item = NULL;
		goto Exit;
	}

	/* now try the reversible path */
	rev_path_status = mpr_rcv_get_path_parms(sa, p_mpr, p_dest_alias_guid,
						 p_src_alias_guid,
						 dest_lid_ho, src_lid_ho,
						 comp_mask, &rev_path_parms);
	path_parms.reversible = (rev_path_status == IB_SUCCESS);

	/* did we get a Reversible Path compmask ? */
	/*
	   NOTE that if the reversible component = 0, it is a don't care
	   rather then requiring non-reversible paths ...
	   see Vol1 Ver1.2 p900 l16
	 */
	if (comp_mask & IB_MPR_COMPMASK_REVERSIBLE) {
		if ((!path_parms.reversible && (p_mpr->num_path & 0x80))) {
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"Requested reversible path but failed to get one\n");

			free(p_pr_item);
			p_pr_item = NULL;
			goto Exit;
		}
	}

	p_pr_item->resp.mpr_rec.p_src_port = p_src_alias_guid->p_base_port;
	p_pr_item->resp.mpr_rec.p_dest_port = p_dest_alias_guid->p_base_port;
	p_pr_item->resp.mpr_rec.hops = path_parms.hops;

	mpr_rcv_build_pr(sa, p_src_alias_guid, p_dest_alias_guid, src_lid_ho,
			 dest_lid_ho, preference, &path_parms,
			 &p_pr_item->resp.mpr_rec.path_rec);

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return p_pr_item;
}

static uint32_t mpr_rcv_get_port_pair_paths(IN osm_sa_t * sa,
					    IN const ib_multipath_rec_t * p_mpr,
					    IN const osm_port_t * p_req_port,
					    IN const osm_alias_guid_t * p_src_alias_guid,
					    IN const osm_alias_guid_t * p_dest_alias_guid,
					    IN const uint32_t rem_paths,
					    IN const ib_net64_t comp_mask,
					    IN cl_qlist_t * p_list)
{
	osm_sa_item_t *p_pr_item;
	uint16_t src_lid_min_ho;
	uint16_t src_lid_max_ho;
	uint16_t dest_lid_min_ho;
	uint16_t dest_lid_max_ho;
	uint16_t src_lid_ho;
	uint16_t dest_lid_ho;
	uint32_t path_num = 0;
	uint8_t preference;
	unsigned src_offset, dest_offset;

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
	   In OpenSM, higher quality mean least overlap with other paths.
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

	   Preference Value           Description
	   ----------------           -------------------------------------------
	   0                  Redundant in both directions with other
	   pref value = 0 paths

	   1                  Redundant in one direction with other
	   pref value = 0 and pref value = 1 paths

	   2                  Not redundant in either direction with
	   other paths

	   3-FF                       Unused

	   SA clients don't need to know these details, only that the lower
	   preference paths are preferred, as stated in the spec.  The paths
	   may not actually be physically redundant depending on the topology
	   of the subnet, but the point of LMC > 0 is to offer redundancy,
	   so I assume the subnet is physically appropriate for the specified
	   LMC value.  A more advanced implementation could inspect for physical
	   redundancy, but I'm not going to bother with that now.
	 */

	osm_port_get_lid_range_ho(p_src_alias_guid->p_base_port,
				  &src_lid_min_ho, &src_lid_max_ho);
	osm_port_get_lid_range_ho(p_dest_alias_guid->p_base_port,
				  &dest_lid_min_ho, &dest_lid_max_ho);

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Src LID [%u-%u], Dest LID [%u-%u]\n",
		src_lid_min_ho, src_lid_max_ho,
		dest_lid_min_ho, dest_lid_max_ho);

	src_lid_ho = src_lid_min_ho;
	dest_lid_ho = dest_lid_min_ho;

	/*
	   Preferred paths come first in OpenSM
	 */
	preference = 0;

	while (path_num < rem_paths) {
		/*
		   These paths are "fully redundant"
		 */
		p_pr_item = mpr_rcv_get_lid_pair_path(sa, p_mpr,
						      p_src_alias_guid,
						      p_dest_alias_guid,
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
	if (path_num == rem_paths)
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
	while (path_num < rem_paths) {
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

		p_pr_item = mpr_rcv_get_lid_pair_path(sa, p_mpr,
						      p_src_alias_guid,
						      p_dest_alias_guid,
						      src_lid_ho, dest_lid_ho,
						      comp_mask, preference);

		if (p_pr_item) {
			cl_qlist_insert_tail(p_list, &p_pr_item->list_item);
			++path_num;
		}
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return path_num;
}

#undef min
#define min(x,y)	(((x) < (y)) ? (x) : (y))

static osm_sa_item_t *mpr_rcv_get_apm_port_pair_paths(IN osm_sa_t * sa,
						      IN const
						      ib_multipath_rec_t *
						      p_mpr,
						      IN const osm_alias_guid_t *
						      p_src_alias_guid,
						      IN const osm_alias_guid_t *
						      p_dest_alias_guid,
						      IN int base_offs,
						      IN const ib_net64_t
						      comp_mask,
						      IN cl_qlist_t * p_list)
{
	osm_sa_item_t *p_pr_item = 0;
	uint16_t src_lid_min_ho;
	uint16_t src_lid_max_ho;
	uint16_t dest_lid_min_ho;
	uint16_t dest_lid_max_ho;
	uint16_t src_lid_ho;
	uint16_t dest_lid_ho;
	unsigned iterations;
	int src_lids, dest_lids;

	OSM_LOG_ENTER(sa->p_log);

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Src port 0x%016" PRIx64 ", "
		"Dst port 0x%016" PRIx64 ", base offs %d\n",
		cl_ntoh64(p_src_alias_guid->alias_guid),
		cl_ntoh64(p_dest_alias_guid->alias_guid),
		base_offs);

	osm_port_get_lid_range_ho(p_src_alias_guid->p_base_port,
				  &src_lid_min_ho, &src_lid_max_ho);
	osm_port_get_lid_range_ho(p_dest_alias_guid->p_base_port,
				  &dest_lid_min_ho, &dest_lid_max_ho);

	src_lid_ho = src_lid_min_ho;
	dest_lid_ho = dest_lid_min_ho;

	src_lids = src_lid_max_ho - src_lid_min_ho + 1;
	dest_lids = dest_lid_max_ho - dest_lid_min_ho + 1;

	src_lid_ho += base_offs % src_lids;
	dest_lid_ho += base_offs % dest_lids;

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"Src LIDs [%u-%u] hashed %u, "
		"Dest LIDs [%u-%u] hashed %u\n",
		src_lid_min_ho, src_lid_max_ho, src_lid_ho,
		dest_lid_min_ho, dest_lid_max_ho, dest_lid_ho);

	iterations = min(src_lids, dest_lids);

	while (iterations--) {
		/*
		   These paths are "fully redundant"
		 */
		p_pr_item = mpr_rcv_get_lid_pair_path(sa, p_mpr,
						      p_src_alias_guid,
						      p_dest_alias_guid,
						      src_lid_ho, dest_lid_ho,
						      comp_mask, 0);

		if (p_pr_item) {
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"Found matching path from Src LID %u to Dest LID %u with %d hops\n",
				src_lid_ho, dest_lid_ho, p_pr_item->resp.mpr_rec.hops);
			break;
		}

		if (++src_lid_ho > src_lid_max_ho)
			src_lid_ho = src_lid_min_ho;

		if (++dest_lid_ho > dest_lid_max_ho)
			dest_lid_ho = dest_lid_min_ho;
	}

	OSM_LOG_EXIT(sa->p_log);
	return p_pr_item;
}

static ib_net16_t mpr_rcv_get_gids(IN osm_sa_t * sa, IN const ib_gid_t * gids,
				   IN int ngids, IN int is_sgid,
				   OUT osm_alias_guid_t ** pp_alias_guid)
{
	osm_alias_guid_t *p_alias_guid;
	ib_net16_t ib_status = IB_SUCCESS;
	int i;

	OSM_LOG_ENTER(sa->p_log);

	for (i = 0; i < ngids; i++, gids++) {
		if (!ib_gid_is_link_local(gids)) {
			if ((is_sgid && ib_gid_is_multicast(gids)) ||
			    (ib_gid_get_subnet_prefix(gids) !=
			     sa->p_subn->opt.subnet_prefix)) {
				/*
				   This 'error' is the client's fault (bad gid)
				   so don't enter it as an error in our own log.
				   Return an error response to the client.
				 */
				OSM_LOG(sa->p_log, OSM_LOG_VERBOSE, "ERR 451B: "
					"%sGID 0x%016" PRIx64
					" is multicast or non local subnet prefix\n",
					is_sgid ? "S" : "D",
					cl_ntoh64(gids->unicast.prefix));

				ib_status = IB_SA_MAD_STATUS_INVALID_GID;
				goto Exit;
			}
		}

		p_alias_guid =
		    osm_get_alias_guid_by_guid(sa->p_subn,
					       gids->unicast.interface_id);
		if (!p_alias_guid) {
			/*
			   This 'error' is the client's fault (bad gid) so
			   don't enter it as an error in our own log.
			   Return an error response to the client.
			 */
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4506: "
				"No port with GUID 0x%016" PRIx64 "\n",
				cl_ntoh64(gids->unicast.interface_id));

			ib_status = IB_SA_MAD_STATUS_INVALID_GID;
			goto Exit;
		}

		pp_alias_guid[i] = p_alias_guid;
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);

	return ib_status;
}

static ib_net16_t mpr_rcv_get_end_points(IN osm_sa_t * sa,
					 IN const osm_madw_t * p_madw,
					 OUT osm_alias_guid_t ** pp_alias_guids,
					 OUT int *nsrc, OUT int *ndest)
{
	const ib_multipath_rec_t *p_mpr;
	const ib_sa_mad_t *p_sa_mad;
	ib_net64_t comp_mask;
	ib_net16_t sa_status = IB_SA_MAD_STATUS_SUCCESS;
	ib_gid_t *gids;

	OSM_LOG_ENTER(sa->p_log);

	/*
	   Determine what fields are valid and then get a pointer
	   to the source and destination port objects, if possible.
	 */
	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_mpr = (ib_multipath_rec_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);
	gids = (ib_gid_t *) p_mpr->gids;

	comp_mask = p_sa_mad->comp_mask;

	/*
	   Check a few easy disqualifying cases up front before getting
	   into the endpoints.
	 */
	*nsrc = *ndest = 0;

	if (comp_mask & IB_MPR_COMPMASK_SGIDCOUNT) {
		*nsrc = p_mpr->sgid_count;
		if (*nsrc > IB_MULTIPATH_MAX_GIDS)
			*nsrc = IB_MULTIPATH_MAX_GIDS;
		sa_status = mpr_rcv_get_gids(sa, gids, *nsrc, 1, pp_alias_guids);
		if (sa_status != IB_SUCCESS)
			goto Exit;
	}

	if (comp_mask & IB_MPR_COMPMASK_DGIDCOUNT) {
		*ndest = p_mpr->dgid_count;
		if (*ndest + *nsrc > IB_MULTIPATH_MAX_GIDS)
			*ndest = IB_MULTIPATH_MAX_GIDS - *nsrc;
		sa_status =
		    mpr_rcv_get_gids(sa, gids + *nsrc, *ndest, 0,
				     pp_alias_guids + *nsrc);
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return sa_status;
}

#define hash_lids(a, b, lmc)	\
	(((((a) >> (lmc)) << 4) | ((b) >> (lmc))) % 103)

static void mpr_rcv_get_apm_paths(IN osm_sa_t * sa,
				  IN const ib_multipath_rec_t * p_mpr,
				  IN const osm_port_t * p_req_port,
				  IN osm_alias_guid_t ** _pp_alias_guids,
				  IN const ib_net64_t comp_mask,
				  IN cl_qlist_t * p_list)
{
	osm_alias_guid_t *pp_alias_guids[4];
	osm_sa_item_t *matrix[2][2];
	int base_offs, src_lid_ho, dest_lid_ho;
	int sumA, sumB, minA, minB;

	OSM_LOG_ENTER(sa->p_log);

	/*
	 * We want to:
	 *    1. use different lid offsets (from base) for the resultant paths
	 *    to increase the probability of redundant paths or in case
	 *    of Clos - to ensure it (different offset => different spine!)
	 *    2. keep consistent paths no matter of direction and order of ports
	 *    3. distibute the lid offsets to balance the load
	 * So, we sort the ports (within the srcs, and within the dests),
	 * hash the lids of S0, D0 (after the sort), and call mpr_rcv_get_apm_port_pair_paths
	 * with base_lid for S0, D0 and base_lid + 1 for S1, D1. This way we will get
	 * always the same offsets - order independent, and make sure different spines are used.
	 * Note that the diagonals on a Clos have the same number of hops, so it doesn't
	 * really matter which diagonal we use.
	 */
	if (_pp_alias_guids[0]->p_base_port->guid <
	    _pp_alias_guids[1]->p_base_port->guid) {
		pp_alias_guids[0] = _pp_alias_guids[0];
		pp_alias_guids[1] = _pp_alias_guids[1];
	} else {
		pp_alias_guids[0] = _pp_alias_guids[1];
		pp_alias_guids[1] = _pp_alias_guids[0];
	}
	if (_pp_alias_guids[2]->p_base_port->guid <
	    _pp_alias_guids[3]->p_base_port->guid) {
		pp_alias_guids[2] = _pp_alias_guids[2];
		pp_alias_guids[3] = _pp_alias_guids[3];
	} else {
		pp_alias_guids[2] = _pp_alias_guids[3];
		pp_alias_guids[3] = _pp_alias_guids[2];
	}

	src_lid_ho = osm_port_get_base_lid(pp_alias_guids[0]->p_base_port);
	dest_lid_ho = osm_port_get_base_lid(pp_alias_guids[2]->p_base_port);

	base_offs = src_lid_ho < dest_lid_ho ?
	    hash_lids(src_lid_ho, dest_lid_ho, sa->p_subn->opt.lmc) :
	    hash_lids(dest_lid_ho, src_lid_ho, sa->p_subn->opt.lmc);

	matrix[0][0] =
	    mpr_rcv_get_apm_port_pair_paths(sa, p_mpr, pp_alias_guids[0],
					    pp_alias_guids[2], base_offs,
					    comp_mask, p_list);
	matrix[0][1] =
	    mpr_rcv_get_apm_port_pair_paths(sa, p_mpr, pp_alias_guids[0],
					    pp_alias_guids[3], base_offs,
					    comp_mask, p_list);
	matrix[1][0] =
	    mpr_rcv_get_apm_port_pair_paths(sa, p_mpr, pp_alias_guids[1],
					    pp_alias_guids[2], base_offs + 1,
					    comp_mask, p_list);
	matrix[1][1] =
	    mpr_rcv_get_apm_port_pair_paths(sa, p_mpr, pp_alias_guids[1],
					    pp_alias_guids[3], base_offs + 1,
					    comp_mask, p_list);

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "APM matrix:\n"
		"\t{0,0} 0x%X->0x%X (%d)\t| {0,1} 0x%X->0x%X (%d)\n"
		"\t{1,0} 0x%X->0x%X (%d)\t| {1,1} 0x%X->0x%X (%d)\n",
		matrix[0][0] ? matrix[0][0]->resp.mpr_rec.path_rec.slid : 0,
		matrix[0][0] ? matrix[0][0]->resp.mpr_rec.path_rec.dlid : 0,
		matrix[0][0] ? matrix[0][0]->resp.mpr_rec.hops : 0,
		matrix[0][1] ? matrix[0][1]->resp.mpr_rec.path_rec.slid : 0,
		matrix[0][1] ? matrix[0][1]->resp.mpr_rec.path_rec.dlid : 0,
		matrix[0][1] ? matrix[0][1]->resp.mpr_rec.hops : 0,
		matrix[1][0] ? matrix[1][0]->resp.mpr_rec.path_rec.slid : 0,
		matrix[1][0] ? matrix[1][0]->resp.mpr_rec.path_rec.dlid : 0,
		matrix[1][0] ? matrix[1][0]->resp.mpr_rec.hops : 0,
		matrix[1][1] ? matrix[1][1]->resp.mpr_rec.path_rec.slid : 0,
		matrix[1][1] ? matrix[1][1]->resp.mpr_rec.path_rec.dlid : 0,
		matrix[1][1] ? matrix[1][1]->resp.mpr_rec.hops : 0);

	sumA = minA = sumB = minB = 0;

	/* check diagonal A {(0,0), (1,1)} */
	if (matrix[0][0]) {
		sumA += matrix[0][0]->resp.mpr_rec.hops;
		minA = matrix[0][0]->resp.mpr_rec.hops;
	}
	if (matrix[1][1]) {
		sumA += matrix[1][1]->resp.mpr_rec.hops;
		if (minA)
			minA = min(minA, matrix[1][1]->resp.mpr_rec.hops);
		else
			minA = matrix[1][1]->resp.mpr_rec.hops;
	}

	/* check diagonal B {(0,1), (1,0)} */
	if (matrix[0][1]) {
		sumB += matrix[0][1]->resp.mpr_rec.hops;
		minB = matrix[0][1]->resp.mpr_rec.hops;
	}
	if (matrix[1][0]) {
		sumB += matrix[1][0]->resp.mpr_rec.hops;
		if (minB)
			minB = min(minB, matrix[1][0]->resp.mpr_rec.hops);
		else
			minB = matrix[1][0]->resp.mpr_rec.hops;
	}

	/* and the winner is... */
	if (minA <= minB || (minA == minB && sumA < sumB)) {
		/* Diag A */
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Diag {0,0} & {1,1} is the best:\n"
			"\t{0,0} 0x%X->0x%X (%d)\t & {1,1} 0x%X->0x%X (%d)\n",
			matrix[0][0] ? matrix[0][0]->resp.mpr_rec.path_rec.slid : 0,
			matrix[0][0] ? matrix[0][0]->resp.mpr_rec.path_rec.dlid : 0,
			matrix[0][0] ? matrix[0][0]->resp.mpr_rec.hops : 0,
			matrix[1][1] ? matrix[1][1]->resp.mpr_rec.path_rec.slid : 0,
			matrix[1][1] ? matrix[1][1]->resp.mpr_rec.path_rec.dlid : 0,
			matrix[1][1] ? matrix[1][1]->resp.mpr_rec.hops : 0);
		if (matrix[0][0])
			cl_qlist_insert_tail(p_list, &matrix[0][0]->list_item);
		if (matrix[1][1])
			cl_qlist_insert_tail(p_list, &matrix[1][1]->list_item);
		free(matrix[0][1]);
		free(matrix[1][0]);
	} else {
		/* Diag B */
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Diag {0,1} & {1,0} is the best:\n"
			"\t{0,1} 0x%X->0x%X (%d)\t & {1,0} 0x%X->0x%X (%d)\n",
			matrix[0][1] ? matrix[0][1]->resp.mpr_rec.path_rec.slid : 0,
			matrix[0][1] ? matrix[0][1]->resp.mpr_rec.path_rec.dlid : 0,
			matrix[0][1] ? matrix[0][1]->resp.mpr_rec.hops : 0,
			matrix[1][0] ? matrix[1][0]->resp.mpr_rec.path_rec.slid : 0,
			matrix[1][0] ? matrix[1][0]->resp.mpr_rec.path_rec.dlid: 0,
			matrix[1][0] ? matrix[1][0]->resp.mpr_rec.hops : 0);
		if (matrix[0][1])
			cl_qlist_insert_tail(p_list, &matrix[0][1]->list_item);
		if (matrix[1][0])
			cl_qlist_insert_tail(p_list, &matrix[1][0]->list_item);
		free(matrix[0][0]);
		free(matrix[1][1]);
	}

	OSM_LOG_EXIT(sa->p_log);
}

static void mpr_rcv_process_pairs(IN osm_sa_t * sa,
				  IN const ib_multipath_rec_t * p_mpr,
				  IN osm_port_t * p_req_port,
				  IN osm_alias_guid_t ** pp_alias_guids,
				  IN const int nsrc, IN int ndest,
				  IN ib_net64_t comp_mask,
				  IN cl_qlist_t * p_list)
{
	osm_alias_guid_t **pp_src_alias_guid, **pp_es;
	osm_alias_guid_t **pp_dest_alias_guid, **pp_ed;
	uint32_t max_paths, num_paths, total_paths = 0;

	OSM_LOG_ENTER(sa->p_log);

	if (comp_mask & IB_MPR_COMPMASK_NUMBPATH)
		max_paths = p_mpr->num_path & 0x7F;
	else
		max_paths = OSM_SA_MPR_MAX_NUM_PATH;

	for (pp_src_alias_guid = pp_alias_guids, pp_es = pp_alias_guids + nsrc;
	     pp_src_alias_guid < pp_es; pp_src_alias_guid++) {
		for (pp_dest_alias_guid = pp_es, pp_ed = pp_es + ndest;
		     pp_dest_alias_guid < pp_ed; pp_dest_alias_guid++) {
			num_paths =
			    mpr_rcv_get_port_pair_paths(sa, p_mpr, p_req_port,
							*pp_src_alias_guid,
							*pp_dest_alias_guid,
							max_paths - total_paths,
							comp_mask, p_list);
			total_paths += num_paths;
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"%d paths %d total paths %d max paths\n",
				num_paths, total_paths, max_paths);
			/* Just take first NumbPaths found */
			if (total_paths >= max_paths)
				goto Exit;
		}
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

void osm_mpr_rcv_process(IN void *context, IN void *data)
{
	osm_sa_t *sa = context;
	osm_madw_t *p_madw = data;
	const ib_multipath_rec_t *p_mpr;
	ib_sa_mad_t *p_sa_mad;
	osm_port_t *requester_port;
	osm_alias_guid_t *pp_alias_guids[IB_MULTIPATH_MAX_GIDS];
	cl_qlist_t pr_list;
	ib_net16_t sa_status;
	int nsrc, ndest;
	uint8_t rate, mtu;

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_mpr = (ib_multipath_rec_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	CL_ASSERT(p_sa_mad->attr_id == IB_MAD_ATTR_MULTIPATH_RECORD);

	if ((p_sa_mad->rmpp_flags & IB_RMPP_FLAG_ACTIVE) != IB_RMPP_FLAG_ACTIVE) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4510: "
			"Invalid request since RMPP_FLAG_ACTIVE is not set\n");
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	/* we only support SubnAdmGetMulti method */
	if (p_sa_mad->method != IB_MAD_METHOD_GETMULTI) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4513: "
			"Unsupported Method (%s) for MultiPathRecord request\n",
			ib_get_sa_method_str(p_sa_mad->method));
		osm_sa_send_error(sa, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		goto Exit;
	}

	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG))
		osm_dump_multipath_record_v2(sa->p_log, p_mpr, FILE_ID, OSM_LOG_DEBUG);

	/* Make sure required components (S/DGIDCount) are supplied */
	if (!(p_sa_mad->comp_mask & IB_MPR_COMPMASK_SGIDCOUNT) ||
	    !(p_sa_mad->comp_mask & IB_MPR_COMPMASK_DGIDCOUNT)) {
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_INSUF_COMPS);
		goto Exit;
	}

	/* Validate rate if supplied */
	if ((p_sa_mad->comp_mask & IB_MPR_COMPMASK_RATESELEC) &&
	    (p_sa_mad->comp_mask & IB_MPR_COMPMASK_RATE)) {
		rate = ib_multipath_rec_rate(p_mpr);
		if (!ib_rate_is_valid(rate)) {
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
			goto Exit;
		}
	}
	/* Validate MTU if supplied */
	if ((p_sa_mad->comp_mask & IB_MPR_COMPMASK_MTUSELEC) &&
	    (p_sa_mad->comp_mask & IB_MPR_COMPMASK_MTU)) {
		mtu = ib_multipath_rec_mtu(p_mpr);
		if (!ib_mtu_is_valid(mtu)) {
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
			goto Exit;
		}
	}

	/* Make sure either none or both ServiceID parameters are supplied */
	if ((p_sa_mad->comp_mask & IB_MPR_COMPMASK_SERVICEID) != 0 &&
	    (p_sa_mad->comp_mask & IB_MPR_COMPMASK_SERVICEID) !=
	     IB_MPR_COMPMASK_SERVICEID) {
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
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4517: "
			"Cannot find requester physical port\n");
		goto Exit;
	}

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"Requester port GUID 0x%" PRIx64 "\n",
		cl_ntoh64(osm_port_get_guid(requester_port)));

	sa_status = mpr_rcv_get_end_points(sa, p_madw, pp_alias_guids,
					   &nsrc, &ndest);

	if (sa_status != IB_SA_MAD_STATUS_SUCCESS || !nsrc || !ndest) {
		cl_plock_release(sa->p_lock);
		if (sa_status == IB_SA_MAD_STATUS_SUCCESS && (!nsrc || !ndest))
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4512: "
				"mpr_rcv_get_end_points failed, # GIDs found; "
				"src %d; dest %d)\n", nsrc, ndest);
		if (sa_status == IB_SA_MAD_STATUS_SUCCESS)
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
		else
			osm_sa_send_error(sa, p_madw, sa_status);
		goto Exit;
	}

	/* APM request */
	if (nsrc == 2 && ndest == 2 && (p_mpr->num_path & 0x7F) == 2)
		mpr_rcv_get_apm_paths(sa, p_mpr, requester_port, pp_alias_guids,
				      p_sa_mad->comp_mask, &pr_list);
	else
		mpr_rcv_process_pairs(sa, p_mpr, requester_port, pp_alias_guids,
				      nsrc, ndest, p_sa_mad->comp_mask,
				      &pr_list);

	cl_plock_release(sa->p_lock);

	/* o15-0.2.7: If MultiPath is supported, then SA shall respond to a
	   SubnAdmGetMulti() containing a valid MultiPathRecord attribute with
	   a set of zero or more PathRecords satisfying the constraints
	   indicated in the MultiPathRecord received. The PathRecord Attribute
	   ID shall be used in the response.
	 */
	p_sa_mad->attr_id = IB_MAD_ATTR_PATH_RECORD;
	osm_sa_respond(sa, p_madw, sizeof(ib_path_rec_t), &pr_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}
#endif
