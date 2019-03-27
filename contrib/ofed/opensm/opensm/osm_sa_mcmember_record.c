/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2015 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2008 Xsigo Systems Inc.  All rights reserved.
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
 *    Implementation of osm_mcmr_recv_t.
 * This object represents the MCMemberRecord Receiver object.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <complib/cl_qlist.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_SA_MCMEMBER_RECORD_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_pkey.h>
#include <opensm/osm_inform.h>
#include <opensm/osm_sa.h>

#define SA_MCM_RESP_SIZE SA_ITEM_RESP_SIZE(mc_rec)

#define JOIN_MC_COMP_MASK (IB_MCR_COMPMASK_MGID | \
				IB_MCR_COMPMASK_PORT_GID | \
				IB_MCR_COMPMASK_JOIN_STATE)

#define REQUIRED_MC_CREATE_COMP_MASK (IB_MCR_COMPMASK_MGID | \
					IB_MCR_COMPMASK_PORT_GID | \
					IB_MCR_COMPMASK_JOIN_STATE | \
					IB_MCR_COMPMASK_QKEY | \
					IB_MCR_COMPMASK_TCLASS | \
					IB_MCR_COMPMASK_PKEY | \
					IB_MCR_COMPMASK_FLOW | \
					IB_MCR_COMPMASK_SL)

#define IPV4_BCAST_MGID_PREFIX CL_HTON64(0xff10401b00000000ULL)
#define IPV4_BCAST_MGID_INT_ID CL_HTON64(0x00000000ffffffffULL)

static int validate_other_comp_fields(osm_log_t * p_log, ib_net64_t comp_mask,
				      const ib_member_rec_t * p_mcmr,
				      osm_mgrp_t * p_mgrp,
				      osm_log_level_t log_level);

/*********************************************************************
 Copy certain fields between two mcmember records
 used during the process of join request to copy data from the mgrp
 to the port record.
**********************************************************************/
static void copy_from_create_mc_rec(IN ib_member_rec_t * dest,
				    IN const ib_member_rec_t * src)
{
	dest->qkey = src->qkey;
	dest->mlid = src->mlid;
	dest->tclass = src->tclass;
	dest->pkey = src->pkey;
	dest->sl_flow_hop = src->sl_flow_hop;
	dest->scope_state = ib_member_set_scope_state(src->scope_state >> 4,
						      dest->scope_state & 0x0F);
	dest->mtu = src->mtu;
	dest->rate = src->rate;
	dest->pkt_life = src->pkt_life;
}

/*********************************************************************
 Return mlid to the pool of free mlids.
 But this implementation is not a pool - it simply scans through
 the MGRP database for unused mlids...
*********************************************************************/
static void free_mlid(IN osm_sa_t * sa, IN uint16_t mlid)
{
	UNUSED_PARAM(sa);
	UNUSED_PARAM(mlid);
}

/*********************************************************************
 Get a new unused mlid by scanning all the used ones in the subnet.
**********************************************************************/
/* Special Case IPv6 Solicited Node Multicast (SNM) addresses */
/* 0xff1Z601bXXXX0000 : 0x00000001ffYYYYYY */
/* Where Z is the scope, XXXX is the P_Key, and
 * YYYYYY is the last 24 bits of the port guid */
#define PREFIX_MASK CL_HTON64(0xff10ffff0000ffffULL)
#define PREFIX_SIGNATURE CL_HTON64(0xff10601b00000000ULL)
#define INT_ID_MASK CL_HTON64(0xfffffff1ff000000ULL)
#define INT_ID_SIGNATURE CL_HTON64(0x00000001ff000000ULL)

static int compare_ipv6_snm_mgids(const void *m1, const void *m2)
{
	return memcmp(m1, m2, sizeof(ib_gid_t) - 3);
}

static ib_net16_t find_ipv6_snm_mlid(osm_subn_t *subn, ib_gid_t *mgid)
{
	osm_mgrp_t *m = (osm_mgrp_t *)cl_fmap_match(&subn->mgrp_mgid_tbl, mgid,
						    compare_ipv6_snm_mgids);
	if (m != (osm_mgrp_t *)cl_fmap_end(&subn->mgrp_mgid_tbl))
		return m->mlid;
	return 0;
}

static unsigned match_ipv6_snm_mgid(ib_gid_t * mgid)
{
	return ((mgid->unicast.prefix & PREFIX_MASK) == PREFIX_SIGNATURE &&
		(mgid->unicast.interface_id & INT_ID_MASK) == INT_ID_SIGNATURE);
}

static ib_net16_t get_new_mlid(osm_sa_t * sa, ib_member_rec_t * mcmr)
{
	osm_subn_t *p_subn = sa->p_subn;
	ib_net16_t requested_mlid = mcmr->mlid;
	unsigned i, max;

	if (requested_mlid && cl_ntoh16(requested_mlid) >= IB_LID_MCAST_START_HO
	    && cl_ntoh16(requested_mlid) <= p_subn->max_mcast_lid_ho
	    && !osm_get_mbox_by_mlid(p_subn, requested_mlid))
		return requested_mlid;

	if (sa->p_subn->opt.consolidate_ipv6_snm_req
	    && match_ipv6_snm_mgid(&mcmr->mgid)
	    && (requested_mlid = find_ipv6_snm_mlid(sa->p_subn, &mcmr->mgid))) {
		char str[INET6_ADDRSTRLEN];
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Special Case Solicited Node Mcast Join for MGID %s\n",
			inet_ntop(AF_INET6, mcmr->mgid.raw, str, sizeof(str)));
		return requested_mlid;
	}

	max = p_subn->max_mcast_lid_ho - IB_LID_MCAST_START_HO + 1;
	for (i = 0; i < max; i++)
		if (!sa->p_subn->mboxes[i])
			return cl_hton16(i + IB_LID_MCAST_START_HO);

	return 0;
}

static inline boolean_t check_join_comp_mask(ib_net64_t comp_mask)
{
	return ((comp_mask & JOIN_MC_COMP_MASK) == JOIN_MC_COMP_MASK);
}

static boolean_t check_create_comp_mask(ib_net64_t comp_mask,
					ib_member_rec_t * p_recvd_mcmember_rec)
{
	return ((comp_mask & REQUIRED_MC_CREATE_COMP_MASK) ==
		REQUIRED_MC_CREATE_COMP_MASK);
}

/**********************************************************************
 Generate the response MAD
**********************************************************************/
static void mcmr_rcv_respond(IN osm_sa_t * sa, IN osm_madw_t * p_madw,
			     IN ib_member_rec_t * p_mcmember_rec)
{
	cl_qlist_t rec_list;
	osm_sa_item_t *item;

	OSM_LOG_ENTER(sa->p_log);

	item = malloc(SA_MCM_RESP_SIZE);
	if (!item) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B16: "
			"rec_item alloc failed\n");
		goto Exit;
	}

	item->resp.mc_rec = *p_mcmember_rec;

	/* Fill in the mtu, rate, and packet lifetime selectors */
	item->resp.mc_rec.mtu &= 0x3f;
	item->resp.mc_rec.mtu |= IB_PATH_SELECTOR_EXACTLY << 6;
	item->resp.mc_rec.rate &= 0x3f;
	item->resp.mc_rec.rate |= IB_PATH_SELECTOR_EXACTLY << 6;
	item->resp.mc_rec.pkt_life &= 0x3f;
	item->resp.mc_rec.pkt_life |= IB_PATH_SELECTOR_EXACTLY << 6;

	cl_qlist_init(&rec_list);
	cl_qlist_insert_tail(&rec_list, &item->list_item);

	osm_sa_respond(sa, p_madw, sizeof(ib_member_rec_t), &rec_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

/*********************************************************************
 In joining an existing group, or when querying the mc groups,
 we make sure the following components provided match: MTU and RATE
 HACK: Currently we ignore the PKT_LIFETIME field.
**********************************************************************/
static boolean_t validate_more_comp_fields(osm_log_t * p_log,
					   const osm_mgrp_t * p_mgrp,
					   const ib_member_rec_t *
					   p_recvd_mcmember_rec,
					   ib_net64_t comp_mask)
{
	uint8_t mtu_sel;
	uint8_t mtu_required;
	uint8_t mtu_mgrp;
	uint8_t rate_sel;
	uint8_t rate_required;
	uint8_t rate_mgrp;

	if (comp_mask & IB_MCR_COMPMASK_MTU_SEL) {
		mtu_sel = (uint8_t) (p_recvd_mcmember_rec->mtu >> 6);
		/* Clearing last 2 bits */
		mtu_required = (uint8_t) (p_recvd_mcmember_rec->mtu & 0x3F);
		mtu_mgrp = (uint8_t) (p_mgrp->mcmember_rec.mtu & 0x3F);
		switch (mtu_sel) {
		case 0:	/* Greater than MTU specified */
			if (mtu_mgrp <= mtu_required) {
				OSM_LOG(p_log, OSM_LOG_VERBOSE,
					"Requested mcast group has MTU %x, "
					"which is not greater than %x\n",
					mtu_mgrp, mtu_required);
				return FALSE;
			}
			break;
		case 1:	/* Less than MTU specified */
			if (mtu_mgrp >= mtu_required) {
				OSM_LOG(p_log, OSM_LOG_VERBOSE,
					"Requested mcast group has MTU %x, "
					"which is not less than %x\n",
					mtu_mgrp, mtu_required);
				return FALSE;
			}
			break;
		case 2:	/* Exactly MTU specified */
			if (mtu_mgrp != mtu_required) {
				OSM_LOG(p_log, OSM_LOG_VERBOSE,
					"Requested mcast group has MTU %x, "
					"which is not equal to %x\n",
					mtu_mgrp, mtu_required);
				return FALSE;
			}
			break;
		default:
			break;
		}
	}

	/* what about rate ? */
	if (comp_mask & IB_MCR_COMPMASK_RATE_SEL) {
		rate_sel = (uint8_t) (p_recvd_mcmember_rec->rate >> 6);
		/* Clearing last 2 bits */
		rate_required = (uint8_t) (p_recvd_mcmember_rec->rate & 0x3F);
		rate_mgrp = (uint8_t) (p_mgrp->mcmember_rec.rate & 0x3F);
		switch (rate_sel) {
		case 0:	/* Greater than RATE specified */
			if (ib_path_compare_rates(rate_mgrp, rate_required) <= 0) {
				OSM_LOG(p_log, OSM_LOG_VERBOSE,
					"Requested mcast group has RATE %x, "
					"which is not greater than %x\n",
					rate_mgrp, rate_required);
				return FALSE;
			}
			break;
		case 1:	/* Less than RATE specified */
			if (ib_path_compare_rates(rate_mgrp, rate_required) >= 0) {
				OSM_LOG(p_log, OSM_LOG_VERBOSE,
					"Requested mcast group has RATE %x, "
					"which is not less than %x\n",
					rate_mgrp, rate_required);
				return FALSE;
			}
			break;
		case 2:	/* Exactly RATE specified */
			if (ib_path_compare_rates(rate_mgrp, rate_required)) {
				OSM_LOG(p_log, OSM_LOG_VERBOSE,
					"Requested mcast group has RATE %x, "
					"which is not equal to %x\n",
					rate_mgrp, rate_required);
				return FALSE;
			}
			break;
		default:
			break;
		}
	}

	return TRUE;
}

/*********************************************************************
 In joining an existing group, we make sure the following components
 are physically realizable: MTU and RATE
**********************************************************************/
static boolean_t validate_port_caps(osm_log_t * p_log,
				    const osm_mgrp_t * p_mgrp,
				    const osm_physp_t * p_physp)
{
	const ib_port_info_t *p_pi;
	uint8_t mtu_required;
	uint8_t mtu_mgrp;
	uint8_t rate_required;
	uint8_t rate_mgrp;
	int extended;

	mtu_required = ib_port_info_get_neighbor_mtu(&p_physp->port_info);
	mtu_mgrp = (uint8_t) (p_mgrp->mcmember_rec.mtu & 0x3F);
	if (mtu_required < mtu_mgrp) {
		OSM_LOG(p_log, OSM_LOG_VERBOSE,
			"Port's MTU %x is less than %x\n",
			mtu_required, mtu_mgrp);
		return FALSE;
	}

	p_pi = &p_physp->port_info;
	extended = p_pi->capability_mask & IB_PORT_CAP_HAS_EXT_SPEEDS;
	rate_required = ib_port_info_compute_rate(p_pi, extended);
	rate_mgrp = (uint8_t) (p_mgrp->mcmember_rec.rate & 0x3F);
	if (ib_path_compare_rates(rate_required, rate_mgrp) < 0) {
		OSM_LOG(p_log, OSM_LOG_VERBOSE,
			"Port's RATE %x is less than %x\n",
			rate_required, rate_mgrp);
		return FALSE;
	}

	return TRUE;
}

/**********************************************************************
 * o15-0.2.1: If SA supports UD multicast, then if SA receives a SubnAdmSet()
 * or SubnAdmDelete() method that would modify an existing
 * MCMemberRecord, SA shall not modify that MCMemberRecord and shall
 * return an error status of ERR_REQ_INVALID in response in the
 * following cases:
 * 1. Saved MCMemberRecord.ProxyJoin is not set and the request is
 * issued by a requester with a GID other than the Port-GID.
 * 2. Saved MCMemberRecord.ProxyJoin is set and the requester is not
 * part of the partition for that MCMemberRecord.
 **********************************************************************/
static boolean_t validate_modify(IN osm_sa_t * sa, IN osm_mgrp_t * p_mgrp,
				 IN osm_mad_addr_t * p_mad_addr,
				 IN ib_member_rec_t * p_recvd_mcmember_rec,
				 OUT osm_mcm_alias_guid_t ** pp_mcm_alias_guid)
{
	ib_net64_t portguid;
	ib_gid_t request_gid;
	osm_physp_t *p_request_physp;
	ib_api_status_t res;

	portguid = p_recvd_mcmember_rec->port_gid.unicast.interface_id;

	*pp_mcm_alias_guid = osm_mgrp_get_mcm_alias_guid(p_mgrp, portguid);

	/* o15-0.2.1: If this is a new port being added - nothing to check */
	if (!*pp_mcm_alias_guid) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"This is a new port in the MC group\n");
		return TRUE;
	}

	/* We validate the request according the the proxy_join.
	   Check if the proxy_join is set or not */
	if ((*pp_mcm_alias_guid)->proxy_join == FALSE) {
		/* The proxy_join is not set. Modifying can by done only
		   if the requester GID == PortGID */
		res = osm_get_gid_by_mad_addr(sa->p_log, sa->p_subn, p_mad_addr,
					      &request_gid);
		if (res != IB_SUCCESS) {
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"Could not find port for requested address\n");
			return FALSE;
		}

		if ((*pp_mcm_alias_guid)->p_base_mcm_port->port->guid !=
		    request_gid.unicast.interface_id ||
		    (*pp_mcm_alias_guid)->port_gid.unicast.prefix !=
		    request_gid.unicast.prefix) {
			ib_gid_t base_port_gid;
			char gid_str[INET6_ADDRSTRLEN];
			char gid_str2[INET6_ADDRSTRLEN];

			base_port_gid.unicast.prefix = (*pp_mcm_alias_guid)->port_gid.unicast.prefix;
			base_port_gid.unicast.interface_id = (*pp_mcm_alias_guid)->p_base_mcm_port->port->guid;
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"No ProxyJoin but different ports: stored:"
				"%s request:%s\n",
				inet_ntop(AF_INET6, base_port_gid.raw, gid_str,
					  sizeof gid_str),
				inet_ntop(AF_INET6, request_gid.raw, gid_str2,
					  sizeof gid_str2));
			return FALSE;
		}
	} else {
		/* The proxy_join is set. Modification allowed only if the
		   requester is part of the partition for this MCMemberRecord */
		p_request_physp = osm_get_physp_by_mad_addr(sa->p_log,
							    sa->p_subn,
							    p_mad_addr);
		if (p_request_physp == NULL)
			return FALSE;

		if (!osm_physp_has_pkey(sa->p_log, p_mgrp->mcmember_rec.pkey,
					p_request_physp)) {
			/* the request port is not part of the partition for this mgrp */
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"Requesting port 0x%016" PRIx64 " has no PKey 0x%04x\n",
				cl_ntoh64(p_request_physp->port_guid),
				cl_ntoh16(p_mgrp->mcmember_rec.pkey));
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * Check legality of the requested MGID DELETE
 * o15-0.1.14 = VALID DELETE:
 * To be a valid delete MAD needs to:
 * 1 the MADs PortGID and MGID components match the PortGID and
 *   MGID of a stored MCMemberRecord;
 * 2 the MADs JoinState component contains at least one bit set to 1
 *   in the same position as that stored MCMemberRecords JoinState
 *   has a bit set to 1,
 *   i.e., the logical AND of the two JoinState components
 *   is not all zeros;
 * 3 the MADs JoinState component does not have some bits set
 *   which are not set in the stored MCMemberRecords JoinState component;
 * 4 either the stored MCMemberRecord:ProxyJoin is reset (0), and the
 *   MADs source is the stored PortGID;
 *   OR
 *   the stored MCMemberRecord:ProxyJoin is set (1), (see o15-
 *   0.1.2:); and the MADs source is a member of the partition indicated
 *   by the stored MCMemberRecord:P_Key.
 */
static boolean_t validate_delete(IN osm_sa_t * sa, IN osm_mgrp_t * p_mgrp,
				 IN osm_mad_addr_t * p_mad_addr,
				 IN ib_member_rec_t * p_recvd_mcmember_rec,
				 OUT osm_mcm_alias_guid_t ** pp_mcm_alias_guid)
{
	ib_net64_t portguid;

	portguid = p_recvd_mcmember_rec->port_gid.unicast.interface_id;

	*pp_mcm_alias_guid = osm_mgrp_get_mcm_alias_guid(p_mgrp, portguid);

	/* 1 */
	if (!*pp_mcm_alias_guid) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Failed to find the port in the MC group\n");
		return FALSE;
	}

	/* 2 */
	if (!(p_recvd_mcmember_rec->scope_state & 0x0F &
	      (*pp_mcm_alias_guid)->scope_state)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Could not find any matching bits in the stored "
			"and requested JoinStates\n");
		return FALSE;
	}

	/* 3 */
	if (((p_recvd_mcmember_rec->scope_state & 0x0F) |
	     (0x0F & (*pp_mcm_alias_guid)->scope_state)) !=
	    (0x0F & (*pp_mcm_alias_guid)->scope_state)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Some bits in the request JoinState (0x%X) are not "
			"set in the stored port (0x%X)\n",
			(p_recvd_mcmember_rec->scope_state & 0x0F),
			(0x0F & (*pp_mcm_alias_guid)->scope_state));
		return FALSE;
	}

	/* 4 */
	/* Validate according the the proxy_join (o15-0.1.2) */
	if (validate_modify(sa, p_mgrp, p_mad_addr, p_recvd_mcmember_rec,
			    pp_mcm_alias_guid) == FALSE) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"proxy_join validation failure\n");
		return FALSE;
	}
	return TRUE;
}

/*
 * Check legality of the requested MGID (note this does not hold for SA
 * created MGIDs)
 *
 * Implementing o15-0.1.5:
 * A multicast GID is considered to be invalid if:
 * 1. It does not comply with the rules as specified in 4.1.1 "GID Usage and
 *    Properties" on page 145:
 *
 * 14) The multicast GID format is (bytes are comma sep):
 *     0xff,<Fl><Sc>,<Si>,<Si>,<P>,<P>,<P>,<P>,<P>,<P>,<P>,<P>,<Id>,<Id>,<Id>,<Id>
 *     Fl  4bit = Flags (b)
 *     Sc  4bit = Scope (c)
 *     Si 16bit = Signature (2)
 *     P  64bit = GID Prefix (should be a subnet unique ID - normally Subnet Prefix)
 *     Id 32bit = Unique ID in the Subnet (might be MLID or P_Key ?)
 *
 *  a) 8-bits of 11111111 at the start of the GID identifies this as being a
 *     multicast GID.
 *  b) Flags is a set of four 1-bit flags: 000T with three flags reserved
 *     and defined as zero (0). The T flag is defined as follows:
 *     i) T = 0 indicates this is a permanently assigned (i.e. wellknown)
 *        multicast GID. See RFC 2373 and RFC 2375 as reference
 *        for these permanently assigned GIDs.
 *     ii) T = 1 indicates this is a non-permanently assigned (i.e. transient)
 *        multicast GID.
 *  c) Scope is a 4-bit multicast scope value used to limit the scope of
 *     the multicast group. The following table defines scope value and
 *     interpretation.
 *
 *     Multicast Address Scope Values:
 *     0x2 Link-local
 *     0x5 Site-local
 *     0x8 Organization-local
 *     0xE Global
 *
 * 2. It contains the SA-specific signature of 0xA01B and has the link-local
 *    scope bits set. (EZ: the idea here is that SA created MGIDs are the
 *    only source for this signature with link-local scope)
 */
static boolean_t validate_requested_mgid(IN osm_sa_t * sa,
					 IN const ib_member_rec_t * p_mcm_rec)
{
	uint16_t signature;
	boolean_t valid = TRUE;

	OSM_LOG_ENTER(sa->p_log);

	/* 14-a: mcast GID must start with 0xFF */
	if (p_mcm_rec->mgid.multicast.header[0] != 0xFF) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B01: "
			"Invalid prefix 0x%02X in requested MGID, "
			"must be 0xFF\n",
			cl_ntoh16(p_mcm_rec->mgid.multicast.header[0]));
		valid = FALSE;
		goto Exit;
	}

	/* the MGID signature can mark IPoIB or SA assigned MGIDs */
	memcpy(&signature, &(p_mcm_rec->mgid.multicast.raw_group_id),
	       sizeof(signature));
	signature = cl_ntoh16(signature);
	OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "MGID Signed as 0x%04X\n", signature);

	/*
	 * We skip any checks for MGIDs that follow IPoIB
	 * GID structure as defined by the IETF ipoib-link-multicast.
	 *
	 * For IPv4 over IB, the signature will be "0x401B".
	 *
	 * |   8    |  4 |  4 |     16 bits     | 16 bits | 48 bits  | 32 bits |
	 * +--------+----+----+-----------------+---------+----------+---------+
	 * |11111111|0001|scop|<IPoIB signature>|< P_Key >|00.......0|<all 1's>|
	 * +--------+----+----+-----------------+---------+----------+---------+
	 *
	 * For IPv6 over IB, the signature will be "0x601B".
	 *
	 * |   8    |  4 |  4 |     16 bits     | 16 bits |       80 bits      |
	 * +--------+----+----+-----------------+---------+--------------------+
	 * |11111111|0001|scop|<IPoIB signature>|< P_Key >|000.............0001|
	 * +--------+----+----+-----------------+---------+--------------------+
	 *
	 */
	if (signature == 0x401B || signature == 0x601B) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Skipping MGID Validation for IPoIB Signed (0x%04X) MGIDs\n",
			signature);
		goto Exit;
	}

	/* 14-b: the 3 upper bits in the "flags" should be zero: */
	if (p_mcm_rec->mgid.multicast.header[1] & 0xE0) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B28: "
			"Requested MGID invalid, uses Reserved Flags: flags=0x%X\n",
			(p_mcm_rec->mgid.multicast.header[1] & 0xE0) >> 4);
		valid = FALSE;
		goto Exit;
	}

	/* 2 - now what if the link local format 0xA01B is used -
	   the scope should not be link local */
	if (signature == 0xA01B &&
	    (p_mcm_rec->mgid.multicast.header[1] & 0x0F) ==
	    IB_MC_SCOPE_LINK_LOCAL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B24: "
			"Requested MGID invalid, "
			"uses 0xA01B signature but with link-local scope\n");
		valid = FALSE;
		goto Exit;
	}

	/*
	 * For SA assigned MGIDs (signature 0xA01B):
	 * There is no real way to make sure the GID Prefix is really unique.
	 * If we could enforce using the Subnet Prefix for that purpose it would
	 * have been nice. But the spec does not require it.
	 */

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return valid;
}

/**********************************************************************
 Check if the requested new MC group parameters are realizable.
 Also set the default MTU and Rate if not provided by the user.
**********************************************************************/
static boolean_t mgrp_request_is_realizable(IN osm_sa_t * sa,
					    IN ib_net64_t comp_mask,
					    IN ib_member_rec_t * p_mcm_rec,
					    IN const osm_physp_t * p_physp)
{
	uint8_t mtu_sel = 2;	/* exactly */
	uint8_t mtu_required, mtu, port_mtu;
	uint8_t rate_sel = 2;	/* exactly */
	uint8_t rate_required, rate, port_rate;
	const ib_port_info_t *p_pi;
	osm_log_t *p_log = sa->p_log;
	int extended;

	OSM_LOG_ENTER(sa->p_log);

	/*
	 * End of o15-0.2.3 specifies:
	 * ....
	 * The entity may also supply the other components such as HopLimit,
	 * MTU, etc. during group creation time. If these components are not
	 * provided during group creation time, SA will provide them for the
	 * group. The values chosen are vendor-dependent and beyond the scope
	 * of the specification.
	 *
	 * so we might also need to assign RATE/MTU if they are not comp
	 * masked in.
	 */

	p_pi = &p_physp->port_info;
	port_mtu = p_physp ? ib_port_info_get_mtu_cap(p_pi) : 0;
	if (!(comp_mask & IB_MCR_COMPMASK_MTU) ||
	    !(comp_mask & IB_MCR_COMPMASK_MTU_SEL) ||
	    (mtu_sel = (p_mcm_rec->mtu >> 6)) == 3)
		mtu = port_mtu ? port_mtu : sa->p_subn->min_ca_mtu;
	else {
		mtu_required = (uint8_t) (p_mcm_rec->mtu & 0x3F);
		mtu = mtu_required;
		switch (mtu_sel) {
		case 0:	/* Greater than MTU specified */
			if (port_mtu && mtu_required >= port_mtu) {
				OSM_LOG(p_log, OSM_LOG_VERBOSE,
					"Requested MTU %x >= the port\'s mtu:%x\n",
					mtu_required, port_mtu);
				return FALSE;
			}
			/* we provide the largest MTU possible if we can */
			if (port_mtu)
				mtu = port_mtu;
			else if (mtu_required < sa->p_subn->min_ca_mtu)
				mtu = sa->p_subn->min_ca_mtu;
			else
				mtu++;
			break;
		case 1:	/* Less than MTU specified */
			/* use the smaller of the two:
			   a. one lower then the required
			   b. the mtu of the requesting port (if exists) */
			if (port_mtu && mtu_required > port_mtu)
				mtu = port_mtu;
			else
				mtu--;
			break;
		case 2:	/* Exactly MTU specified */
		default:
			break;
		}
		/* make sure it still is in the range */
		if (mtu < IB_MIN_MTU || mtu > IB_MAX_MTU) {
			OSM_LOG(p_log, OSM_LOG_VERBOSE,
				"Calculated MTU %x is out of range\n", mtu);
			return FALSE;
		}
	}
	p_mcm_rec->mtu = (mtu_sel << 6) | mtu;

	if (p_physp) {
		extended = p_pi->capability_mask & IB_PORT_CAP_HAS_EXT_SPEEDS;
		port_rate = ib_port_info_compute_rate(p_pi, extended);
	} else
		port_rate = 0;

	if (!(comp_mask & IB_MCR_COMPMASK_RATE)
	    || !(comp_mask & IB_MCR_COMPMASK_RATE_SEL)
	    || (rate_sel = (p_mcm_rec->rate >> 6)) == 3)
		rate = port_rate ? port_rate : sa->p_subn->min_ca_rate;
	else {
		rate_required = (uint8_t) (p_mcm_rec->rate & 0x3F);
		rate = rate_required;
		switch (rate_sel) {
		case 0:	/* Greater than RATE specified */
			if (ib_path_compare_rates(rate_required, port_rate) >= 0) {
				OSM_LOG(p_log, OSM_LOG_VERBOSE,
					"Requested RATE %x >= the port\'s rate:%x\n",
					rate_required, port_rate);
				return FALSE;
			}
			/* we provide the largest RATE possible if we can */
			if (port_rate)
				rate = port_rate;
			else if (ib_path_compare_rates(rate_required,
						       sa->p_subn->min_ca_rate) < 0)
				rate = sa->p_subn->min_ca_rate;
			else
				rate = ib_path_rate_get_next(rate);
			break;
		case 1:	/* Less than RATE specified */
			/* use the smaller of the two:
			   a. one lower then the required
			   b. the rate of the requesting port (if exists) */
			if (ib_path_compare_rates(rate_required, port_rate) > 0)
				rate = port_rate;
			else
				rate = ib_path_rate_get_prev(rate);
			break;
		case 2:	/* Exactly RATE specified */
		default:
			break;
		}
		/* make sure it still is in the range */
		if (rate < IB_MIN_RATE || rate > IB_MAX_RATE) {
			OSM_LOG(p_log, OSM_LOG_VERBOSE,
				"Calculated RATE %x is out of range\n", rate);
			return FALSE;
		}
	}
	p_mcm_rec->rate = (rate_sel << 6) | rate;

	OSM_LOG_EXIT(sa->p_log);
	return TRUE;
}

static unsigned build_new_mgid(osm_sa_t * sa, ib_net64_t comp_mask,
			       ib_member_rec_t * mcmr)
{
	static uint32_t uniq_count;
	ib_gid_t *mgid = &mcmr->mgid;
	uint8_t scope;
	unsigned i;

	/* use the given scope state only if requested! */
	if (comp_mask & IB_MCR_COMPMASK_SCOPE)
		ib_member_get_scope_state(mcmr->scope_state, &scope, NULL);
	else
	/* to guarantee no collision with other subnets use local scope! */
		scope = IB_MC_SCOPE_LINK_LOCAL;

	mgid->raw[0] = 0xff;
	mgid->raw[1] = 0x10 | scope;
	mgid->raw[2] = 0xa0;
	mgid->raw[3] = 0x1b;

	memcpy(&mgid->raw[4], &sa->p_subn->opt.subnet_prefix, sizeof(uint64_t));

	for (i = 0; i < 1000; i++) {
		memcpy(&mgid->raw[10], &uniq_count, 4);
		uniq_count++;
		if (!osm_get_mgrp_by_mgid(sa->p_subn, mgid))
			return 1;
	}

	return 0;
}

/**********************************************************************
 Call this function to create a new mgrp.
**********************************************************************/
static ib_api_status_t mcmr_rcv_create_new_mgrp(IN osm_sa_t * sa,
						IN ib_net64_t comp_mask,
						IN const ib_member_rec_t * p_recvd_mcmember_rec,
						IN const osm_physp_t * p_physp,
						OUT osm_mgrp_t ** pp_mgrp)
{
	ib_net16_t mlid;
	uint16_t signature;
	ib_api_status_t status = IB_SUCCESS;
	osm_mgrp_t *bcast_mgrp;
	ib_gid_t bcast_mgid;
	ib_member_rec_t mcm_rec = *p_recvd_mcmember_rec;	/* copy for modifications */
	char gid_str[INET6_ADDRSTRLEN];

	OSM_LOG_ENTER(sa->p_log);

	/* we need to create the new MGID if it was not defined */
	if (!ib_gid_is_notzero(&p_recvd_mcmember_rec->mgid)) {
		/* create a new MGID */
		if (!build_new_mgid(sa, comp_mask, &mcm_rec)) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B23: "
				"cannot allocate unique MGID value\n");
			status = IB_SA_MAD_STATUS_NO_RESOURCES;
			goto Exit;
		}
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Allocated new MGID:%s\n",
			inet_ntop(AF_INET6, mcm_rec.mgid.raw, gid_str,
				  sizeof gid_str));
	} else if (sa->p_subn->opt.ipoib_mcgroup_creation_validation) {
		/* a specific MGID was requested so validate the resulting MGID */
		if (validate_requested_mgid(sa, &mcm_rec)) {
			memcpy(&signature, &(mcm_rec.mgid.multicast.raw_group_id),
			       sizeof(signature));
			signature = cl_ntoh16(signature);
			/* Check for IPoIB signature in MGID */
			if (signature == 0x401B || signature == 0x601B) {
				/* Derive IPoIB broadcast MGID */
				bcast_mgid.unicast.prefix = IPV4_BCAST_MGID_PREFIX;
				bcast_mgid.unicast.interface_id = IPV4_BCAST_MGID_INT_ID;
				/* Set scope in IPoIB broadcast MGID */
				bcast_mgid.multicast.header[1] =
					(bcast_mgid.multicast.header[1] & 0xF0) |
					(mcm_rec.mgid.multicast.header[1] & 0x0F);
				/* Set P_Key in IPoIB broadcast MGID */
				bcast_mgid.multicast.raw_group_id[2] =
					mcm_rec.mgid.multicast.raw_group_id[2];
				bcast_mgid.multicast.raw_group_id[3] =
					mcm_rec.mgid.multicast.raw_group_id[3];
				/* Check MC group for the IPoIB broadcast group */
				if (signature != 0x401B ||
				    memcmp(&bcast_mgid, &(mcm_rec.mgid), sizeof(ib_gid_t))) {
					bcast_mgrp = osm_get_mgrp_by_mgid(sa->p_subn,
									  &bcast_mgid);
					if (!bcast_mgrp) {
						OSM_LOG(sa->p_log, OSM_LOG_ERROR,
							"ERR 1B1B: Broadcast group %s not found, sending IB_SA_MAD_STATUS_REQ_INVALID\n",
							inet_ntop(AF_INET6, bcast_mgid.raw, gid_str, sizeof gid_str));
						status = IB_SA_MAD_STATUS_REQ_INVALID;
						goto Exit;
					}
					if (!validate_other_comp_fields(sa->p_log, comp_mask, p_recvd_mcmember_rec, bcast_mgrp, OSM_LOG_ERROR)) {
						OSM_LOG(sa->p_log, OSM_LOG_ERROR,
							"ERR 1B1C: validate_other_comp_fields failed for MGID: %s, sending IB_SA_MAD_STATUS_REQ_INVALID\n",
							inet_ntop(AF_INET6, &p_recvd_mcmember_rec->mgid, gid_str, sizeof gid_str));
						status = IB_SA_MAD_STATUS_REQ_INVALID;
						goto Exit;
					}
				}
			}
		} else {
			status = IB_SA_MAD_STATUS_REQ_INVALID;
			goto Exit;
		}
	}

	/* check the requested parameters are realizable */
	if (mgrp_request_is_realizable(sa, comp_mask, &mcm_rec, p_physp) ==
	    FALSE) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B26: "
			"Requested MGRP parameters are not realizable\n");
		status = IB_SA_MAD_STATUS_REQ_INVALID;
		goto Exit;
	}

	mlid = get_new_mlid(sa, &mcm_rec);
	if (mlid == 0) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B19: "
			"get_new_mlid failed request mlid 0x%04x\n",
			cl_ntoh16(mcm_rec.mlid));
		status = IB_SA_MAD_STATUS_NO_RESOURCES;
		goto Exit;
	}

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Obtained new mlid 0x%X\n",
		cl_ntoh16(mlid));

	mcm_rec.mlid = mlid;
	/* create a new MC Group */
	*pp_mgrp = osm_mgrp_new(sa->p_subn, mlid, &mcm_rec);
	if (*pp_mgrp == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B08: "
			"osm_mgrp_new failed\n");
		free_mlid(sa, mlid);
		status = IB_SA_MAD_STATUS_NO_RESOURCES;
		goto Exit;
	}

	/* the mcmember_record should have mtu_sel, rate_sel, and pkt_lifetime_sel = 2 */
	(*pp_mgrp)->mcmember_rec.mtu &= 0x3f;
	(*pp_mgrp)->mcmember_rec.mtu |= IB_PATH_SELECTOR_EXACTLY << 6;
	(*pp_mgrp)->mcmember_rec.rate &= 0x3f;
	(*pp_mgrp)->mcmember_rec.rate |= IB_PATH_SELECTOR_EXACTLY << 6;
	(*pp_mgrp)->mcmember_rec.pkt_life &= 0x3f;
	(*pp_mgrp)->mcmember_rec.pkt_life |= IB_PATH_SELECTOR_EXACTLY << 6;

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return status;
}

/**********************************************************************
 Call this function to find or create a new mgrp.
**********************************************************************/
osm_mgrp_t *osm_mcmr_rcv_find_or_create_new_mgrp(IN osm_sa_t * sa,
						 IN ib_net64_t comp_mask,
						 IN ib_member_rec_t *
						 p_recvd_mcmember_rec)
{
	osm_mgrp_t *mgrp;

	if ((mgrp = osm_get_mgrp_by_mgid(sa->p_subn,
					 &p_recvd_mcmember_rec->mgid)))
		return mgrp;
	if (mcmr_rcv_create_new_mgrp(sa, comp_mask, p_recvd_mcmember_rec, NULL,
				     &mgrp) == IB_SUCCESS)
		return mgrp;
	return NULL;
}

/*********************************************************************
Process a request for leaving the group
**********************************************************************/
static void mcmr_rcv_leave_mgrp(IN osm_sa_t * sa, IN osm_madw_t * p_madw)
{
	osm_mgrp_t *p_mgrp;
	ib_sa_mad_t *p_sa_mad;
	ib_member_rec_t *p_recvd_mcmember_rec;
	ib_member_rec_t mcmember_rec;
	osm_mcm_alias_guid_t *p_mcm_alias_guid;

	OSM_LOG_ENTER(sa->p_log);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_mcmember_rec =
	    (ib_member_rec_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	mcmember_rec = *p_recvd_mcmember_rec;

	/* Validate the subnet prefix in the PortGID */
	if (p_recvd_mcmember_rec->port_gid.unicast.prefix !=
	    sa->p_subn->opt.subnet_prefix) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"PortGID subnet prefix 0x%" PRIx64
			" does not match configured prefix 0x%" PRIx64 "\n",
			cl_ntoh64(p_recvd_mcmember_rec->port_gid.unicast.prefix),
			cl_ntoh64(sa->p_subn->opt.subnet_prefix));
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_INVALID_GID);
		goto Exit;
	}

	CL_PLOCK_EXCL_ACQUIRE(sa->p_lock);

	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG)) {
		osm_physp_t *p_req_physp;

		p_req_physp = osm_get_physp_by_mad_addr(sa->p_log, sa->p_subn,
							osm_madw_get_mad_addr_ptr(p_madw));
		if (p_req_physp == NULL) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B02: "
				"Cannot find requester physical port\n");
		} else {
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"Requester port GUID 0x%" PRIx64 "\n",
				cl_ntoh64(osm_physp_get_port_guid(p_req_physp)));
		}
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Dump of record\n");
		osm_dump_mc_record_v2(sa->p_log, &mcmember_rec, FILE_ID, OSM_LOG_DEBUG);
	}

	p_mgrp = osm_get_mgrp_by_mgid(sa->p_subn, &p_recvd_mcmember_rec->mgid);
	if (!p_mgrp) {
		char gid_str[INET6_ADDRSTRLEN];
		CL_PLOCK_RELEASE(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Failed since multicast group %s not present\n",
			inet_ntop(AF_INET6, p_recvd_mcmember_rec->mgid.raw,
				  gid_str, sizeof gid_str));
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	/* check validity of the delete request o15-0.1.14 */
	if (!validate_delete(sa, p_mgrp, osm_madw_get_mad_addr_ptr(p_madw),
			     p_recvd_mcmember_rec, &p_mcm_alias_guid)) {
		char gid_str[INET6_ADDRSTRLEN];
		char gid_str2[INET6_ADDRSTRLEN];
		CL_PLOCK_RELEASE(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B25: "
			"Received an invalid delete request for "
			"MGID: %s for PortGID: %s\n",
			inet_ntop(AF_INET6, p_recvd_mcmember_rec->mgid.raw,
				  gid_str, sizeof gid_str),
			inet_ntop(AF_INET6, p_recvd_mcmember_rec->port_gid.raw,
				  gid_str2, sizeof gid_str2));
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	/* remove port and/or update join state */
	osm_mgrp_remove_port(sa->p_subn, sa->p_log, p_mgrp, p_mcm_alias_guid,
			     &mcmember_rec);
	CL_PLOCK_RELEASE(sa->p_lock);

	mcmr_rcv_respond(sa, p_madw, &mcmember_rec);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

static int validate_other_comp_fields(osm_log_t * p_log, ib_net64_t comp_mask,
				      const ib_member_rec_t * p_mcmr,
				      osm_mgrp_t * p_mgrp,
				      osm_log_level_t log_level)
{
	int ret = 0;

	if ((IB_MCR_COMPMASK_QKEY & comp_mask) &&
	    p_mcmr->qkey != p_mgrp->mcmember_rec.qkey) {
		OSM_LOG(p_log, log_level, "ERR 1B30: "
			"Q_Key mismatch: query 0x%x group 0x%x\n",
			cl_ntoh32(p_mcmr->qkey),
			cl_ntoh32(p_mgrp->mcmember_rec.qkey));
		goto Exit;
	}

	if (IB_MCR_COMPMASK_PKEY & comp_mask) {
		if (!(ib_pkey_is_full_member(p_mcmr->pkey) ||
		      ib_pkey_is_full_member(p_mgrp->mcmember_rec.pkey))) {
			OSM_LOG(p_log, log_level, "ERR 1B31: "
				"Both limited P_Keys: query 0x%x group 0x%x\n",
				cl_ntoh16(p_mcmr->pkey),
				cl_ntoh16(p_mgrp->mcmember_rec.pkey));
			goto Exit;
		}
		if (ib_pkey_get_base(p_mcmr->pkey) !=
		    ib_pkey_get_base(p_mgrp->mcmember_rec.pkey)) {
			OSM_LOG(p_log, log_level, "ERR 1B32: "
				"P_Key base mismatch: query 0x%x group 0x%x\n",
				cl_ntoh16(p_mcmr->pkey),
				cl_ntoh16(p_mgrp->mcmember_rec.pkey));
			goto Exit;
		}
	}

	if ((IB_MCR_COMPMASK_TCLASS & comp_mask) &&
	    p_mcmr->tclass != p_mgrp->mcmember_rec.tclass) {
		OSM_LOG(p_log, log_level, "ERR 1B33: "
			"TClass mismatch: query %d group %d\n",
			p_mcmr->tclass, p_mgrp->mcmember_rec.tclass);
		goto Exit;
	}

	/* check SL, Flow, and Hop limit */
	{
		uint32_t mgrp_flow, query_flow;
		uint8_t mgrp_sl, query_sl;
		uint8_t mgrp_hop, query_hop;

		ib_member_get_sl_flow_hop(p_mcmr->sl_flow_hop,
					  &query_sl, &query_flow, &query_hop);

		ib_member_get_sl_flow_hop(p_mgrp->mcmember_rec.sl_flow_hop,
					  &mgrp_sl, &mgrp_flow, &mgrp_hop);

		if ((IB_MCR_COMPMASK_SL & comp_mask) && query_sl != mgrp_sl) {
			OSM_LOG(p_log, log_level, "ERR 1B34: "
				"SL mismatch: query %d group %d\n",
				query_sl, mgrp_sl);
			goto Exit;
		}

		if ((IB_MCR_COMPMASK_FLOW & comp_mask) &&
		    query_flow != mgrp_flow) {
			OSM_LOG(p_log, log_level, "ERR 1B35: "
				"FlowLabel mismatch: query 0x%x group 0x%x\n",
				query_flow, mgrp_flow);
			goto Exit;
		}

		if ((IB_MCR_COMPMASK_HOP & comp_mask) && query_hop != mgrp_hop) {
			OSM_LOG(p_log, log_level, "ERR 1B36: "
				"Hop mismatch: query %d group %d\n",
				query_hop, mgrp_hop);
			goto Exit;
		}
	}

	ret = 1;
Exit:
	return ret;
}

/**********************************************************************
 Handle a join (or create) request
**********************************************************************/
static void mcmr_rcv_join_mgrp(IN osm_sa_t * sa, IN osm_madw_t * p_madw)
{
	osm_mgrp_t *p_mgrp = NULL;
	ib_api_status_t status;
	ib_sa_mad_t *p_sa_mad;
	ib_member_rec_t *p_recvd_mcmember_rec;
	ib_member_rec_t mcmember_rec;
	osm_mcm_port_t *p_mcmr_port;
	osm_mcm_alias_guid_t *p_mcm_alias_guid;
	ib_net64_t portguid;
	osm_port_t *p_port;
	osm_physp_t *p_physp;
	osm_physp_t *p_request_physp;
	uint8_t is_new_group;	/* TRUE = there is a need to create a group */
	uint8_t join_state;
	boolean_t proxy;

	OSM_LOG_ENTER(sa->p_log);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_mcmember_rec = ib_sa_mad_get_payload_ptr(p_sa_mad);

	portguid = p_recvd_mcmember_rec->port_gid.unicast.interface_id;

	mcmember_rec = *p_recvd_mcmember_rec;

	/* Validate the subnet prefix in the PortGID */
	if (p_recvd_mcmember_rec->port_gid.unicast.prefix !=
	    sa->p_subn->opt.subnet_prefix) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"PortGID subnet prefix 0x%" PRIx64
			" does not match configured prefix 0x%" PRIx64 "\n",
			cl_ntoh64(p_recvd_mcmember_rec->port_gid.unicast.prefix),
			cl_ntoh64(sa->p_subn->opt.subnet_prefix));
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_INVALID_GID);
		goto Exit;
	}

	CL_PLOCK_EXCL_ACQUIRE(sa->p_lock);

	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG)) {
		osm_physp_t *p_req_physp;

		p_req_physp = osm_get_physp_by_mad_addr(sa->p_log, sa->p_subn,
							osm_madw_get_mad_addr_ptr(p_madw));
		if (p_req_physp == NULL) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B03: "
				"Cannot find requester physical port\n");
		} else {
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"Requester port GUID 0x%" PRIx64 "\n",
				cl_ntoh64(osm_physp_get_port_guid(p_req_physp)));
		}
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Dump of incoming record\n");
		osm_dump_mc_record_v2(sa->p_log, &mcmember_rec, FILE_ID, OSM_LOG_DEBUG);
	}

	/* make sure the requested port guid is known to the SM */
	p_port = osm_get_port_by_alias_guid(sa->p_subn, portguid);
	if (!p_port) {
		CL_PLOCK_RELEASE(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Unknown port GUID 0x%016" PRIx64 "\n",
			cl_ntoh64(portguid));
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	p_physp = p_port->p_physp;
	/* Check that the p_physp and the requester physp are in the same
	   partition. */
	p_request_physp =
	    osm_get_physp_by_mad_addr(sa->p_log, sa->p_subn,
				      osm_madw_get_mad_addr_ptr(p_madw));
	if (p_request_physp == NULL) {
		CL_PLOCK_RELEASE(sa->p_lock);
		goto Exit;
	}

	proxy = (p_physp != p_request_physp);

	if (proxy && !osm_physp_share_pkey(sa->p_log, p_physp, p_request_physp,
					   sa->p_subn->opt.allow_both_pkeys)) {
		CL_PLOCK_RELEASE(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
			"Port and requester don't share PKey\n");
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	if ((p_sa_mad->comp_mask & IB_MCR_COMPMASK_PKEY) &&
	    ib_pkey_is_invalid(p_recvd_mcmember_rec->pkey)) {
		CL_PLOCK_RELEASE(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
			"Invalid PKey supplied in request\n");
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	ib_member_get_scope_state(p_recvd_mcmember_rec->scope_state, NULL,
				  &join_state);

	/* do we need to create a new group? */
	p_mgrp = osm_get_mgrp_by_mgid(sa->p_subn, &p_recvd_mcmember_rec->mgid);
	if (!p_mgrp) {
		/* check for JoinState.FullMember = 1 o15.0.1.9 */
		if ((join_state & 0x01) != 0x01) {
			char gid_str[INET6_ADDRSTRLEN];
			CL_PLOCK_RELEASE(sa->p_lock);
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B10: "
				"Failed to create multicast group "
				"because Join State != FullMember, "
				"MGID: %s from port 0x%016" PRIx64 " (%s)\n",
				inet_ntop(AF_INET6,
					  p_recvd_mcmember_rec->mgid.raw,
					  gid_str, sizeof gid_str),
				cl_ntoh64(portguid),
				p_port->p_node->print_desc);
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
			goto Exit;
		}

		/* check the comp_mask */
		if (!check_create_comp_mask(p_sa_mad->comp_mask,
					    p_recvd_mcmember_rec)) {
			char gid_str[INET6_ADDRSTRLEN];
			CL_PLOCK_RELEASE(sa->p_lock);
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B11: "
				"Port 0x%016" PRIx64 " (%s) failed to join "
				"non-existing multicast group with MGID %s, "
				"insufficient components specified for "
				"implicit create (comp_mask 0x%" PRIx64 ")\n",
				cl_ntoh64(portguid), p_port->p_node->print_desc,
				inet_ntop(AF_INET6,
					  p_recvd_mcmember_rec->mgid.raw,
					  gid_str, sizeof gid_str),
				cl_ntoh64(p_sa_mad->comp_mask));
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_INSUF_COMPS);
			goto Exit;
		}

		status = mcmr_rcv_create_new_mgrp(sa, p_sa_mad->comp_mask,
						  p_recvd_mcmember_rec,
						  p_physp, &p_mgrp);
		if (status != IB_SUCCESS) {
			CL_PLOCK_RELEASE(sa->p_lock);
			osm_sa_send_error(sa, p_madw, status);
			goto Exit;
		}
		/* copy the MGID to the result */
		mcmember_rec.mgid = p_mgrp->mcmember_rec.mgid;
		is_new_group = 1;
	} else {
		/* no need for a new group */
		is_new_group = 0;
		if (sa->p_subn->opt.mcgroup_join_validation &&
		    !validate_other_comp_fields(sa->p_log, p_sa_mad->comp_mask,
						p_recvd_mcmember_rec, p_mgrp,
						OSM_LOG_ERROR)) {
			char gid_str[INET6_ADDRSTRLEN];
			CL_PLOCK_RELEASE(sa->p_lock);
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B1A: "
				"validate_other_comp_fields failed for "
				"MGID: %s port 0x%016" PRIx64
				" (%s), sending IB_SA_MAD_STATUS_REQ_INVALID\n",
				inet_ntop(AF_INET6,
					  p_mgrp->mcmember_rec.mgid.raw,
					  gid_str, sizeof gid_str),
				cl_ntoh64(portguid),
				p_port->p_node->print_desc);
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
			goto Exit;
		}
	}

	CL_ASSERT(p_mgrp);

	/*
	 * o15-0.2.4: If SA supports UD multicast, then SA shall cause an
	 * endport to join an existing multicast group if:
	 * 1. It receives a SubnAdmSet() method for a MCMemberRecord, and
	 *    - WE KNOW THAT ALREADY
	 * 2. The MGID is specified and matches an existing multicast
	 *    group, and
	 *    - WE KNOW THAT ALREADY
	 * 3. The MCMemberRecord:JoinState is not all 0s, and
	 * 4. PortGID is specified and
	 *    - WE KNOW THAT ALREADY (as it matched a real one)
	 * 5. All other components match that existing group, either by
	 *    being wildcarded or by having values identical to those specified
	 *    by the component mask and in use by the group with the exception
	 *    of components such as ProxyJoin and Reserved, which are ignored
	 *    by SA.
	 *
	 * We need to check #3 and #5 here:
	 */
	if (!validate_more_comp_fields(sa->p_log, p_mgrp, p_recvd_mcmember_rec,
				       p_sa_mad->comp_mask)
	    || !validate_port_caps(sa->p_log, p_mgrp, p_physp)
	    || !(join_state != 0)) {
		char gid_str[INET6_ADDRSTRLEN];
		/* since we might have created the new group we need to cleanup */
		if (is_new_group)
			osm_mgrp_cleanup(sa->p_subn, p_mgrp);
		CL_PLOCK_RELEASE(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B12: "
			"validate_more_comp_fields, validate_port_caps, "
			"or JoinState = 0 failed for MGID: %s port 0x%016" PRIx64
			" (%s), sending IB_SA_MAD_STATUS_REQ_INVALID\n",
			   inet_ntop(AF_INET6, p_mgrp->mcmember_rec.mgid.raw,
				     gid_str, sizeof gid_str),
			cl_ntoh64(portguid), p_port->p_node->print_desc);
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	/* verify that the joining port is in the partition of the group */
	if (!osm_physp_has_pkey(sa->p_log, p_mgrp->mcmember_rec.pkey, p_physp)) {
		char gid_str[INET6_ADDRSTRLEN];
		if (is_new_group)
			osm_mgrp_cleanup(sa->p_subn, p_mgrp);
		CL_PLOCK_RELEASE(sa->p_lock);
		memset(gid_str, 0, sizeof(gid_str));
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B14: "
			"Cannot join port 0x%016" PRIx64 " to MGID %s - "
			"Port is not in partition of this MC group\n",
			cl_ntoh64(portguid),
			inet_ntop(AF_INET6,
				  p_mgrp->mcmember_rec.mgid.raw,
				  gid_str, sizeof(gid_str)));
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	/*
	 * o15-0.2.1 requires validation of the requesting port
	 * in the case of modification:
	 */
	if (!is_new_group &&
	    !validate_modify(sa, p_mgrp, osm_madw_get_mad_addr_ptr(p_madw),
			     p_recvd_mcmember_rec, &p_mcm_alias_guid)) {
		char gid_str[INET6_ADDRSTRLEN];
		CL_PLOCK_RELEASE(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B13: "
			"validate_modify failed from port 0x%016" PRIx64
			" (%s) for MGID: %s, sending IB_SA_MAD_STATUS_REQ_INVALID\n",
			cl_ntoh64(portguid), p_port->p_node->print_desc,
			inet_ntop(AF_INET6,
				  p_mgrp->mcmember_rec.mgid.raw,
				  gid_str, sizeof(gid_str)));
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	/* copy qkey mlid tclass pkey sl_flow_hop mtu rate pkt_life */
	copy_from_create_mc_rec(&mcmember_rec, &p_mgrp->mcmember_rec);

	/* create or update existing port (join-state will be updated) */
	p_mcmr_port = osm_mgrp_add_port(sa->p_subn, sa->p_log, p_mgrp, p_port,
					&mcmember_rec, proxy);
	if (!p_mcmr_port) {
		/* we fail to add the port so we might need to delete the group */
		if (is_new_group)
			osm_mgrp_cleanup(sa->p_subn, p_mgrp);
		CL_PLOCK_RELEASE(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B06: "
			"osm_mgrp_add_port failed\n");
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_NO_RESOURCES);
		goto Exit;
	}

	/* Release the lock as we don't need it. */
	CL_PLOCK_RELEASE(sa->p_lock);

	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG))
		osm_dump_mc_record_v2(sa->p_log, &mcmember_rec, FILE_ID, OSM_LOG_DEBUG);

	mcmr_rcv_respond(sa, p_madw, &mcmember_rec);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

/**********************************************************************
 Add a patched multicast group to the results list
**********************************************************************/
static ib_api_status_t mcmr_rcv_new_mcmr(IN osm_sa_t * sa,
					 IN const ib_member_rec_t * p_rcvd_rec,
					 IN cl_qlist_t * p_list)
{
	osm_sa_item_t *p_rec_item;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sa->p_log);

	p_rec_item = malloc(SA_MCM_RESP_SIZE);
	if (p_rec_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B15: "
			"rec_item alloc failed\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	memset(p_rec_item, 0, sizeof(cl_list_item_t));

	/* HACK: Untrusted requesters should result with 0 Join
	   State, Port Guid, and Proxy */
	p_rec_item->resp.mc_rec = *p_rcvd_rec;
	cl_qlist_insert_tail(p_list, &p_rec_item->list_item);

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return status;
}

/**********************************************************************
 Match the given mgrp to the requested mcmr
**********************************************************************/
static void mcmr_by_comp_mask(osm_sa_t * sa, const ib_member_rec_t * p_rcvd_rec,
			      ib_net64_t comp_mask, osm_mgrp_t * p_mgrp,
			      const osm_physp_t * p_req_physp,
			      boolean_t trusted_req, cl_qlist_t * list)
{
	/* since we might change scope_state */
	ib_member_rec_t match_rec;
	osm_mcm_alias_guid_t *p_mcm_alias_guid;
	ib_net64_t portguid = p_rcvd_rec->port_gid.unicast.interface_id;
	/* will be used for group or port info */
	uint8_t scope_state;
	uint8_t scope_state_mask = 0;
	cl_map_item_t *p_item;
	ib_gid_t port_gid;
	boolean_t proxy_join = FALSE;

	OSM_LOG_ENTER(sa->p_log);

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"Checking mlid:0x%X\n", cl_ntoh16(p_mgrp->mlid));

	/* first try to eliminate the group by MGID, MLID, or P_Key */
	if ((IB_MCR_COMPMASK_MGID & comp_mask) &&
	    memcmp(&p_rcvd_rec->mgid, &p_mgrp->mcmember_rec.mgid,
		   sizeof(ib_gid_t)))
		goto Exit;

	if ((IB_MCR_COMPMASK_MLID & comp_mask) &&
	    memcmp(&p_rcvd_rec->mlid, &p_mgrp->mcmember_rec.mlid,
		   sizeof(uint16_t)))
		goto Exit;

	/* if the requester physical port doesn't have the pkey that is defined
	   for the group - exit. */
	if (!osm_physp_has_pkey(sa->p_log, p_mgrp->mcmember_rec.pkey,
				p_req_physp))
		goto Exit;

	/* now do the rest of the match */
	if (!validate_other_comp_fields(sa->p_log, comp_mask, p_rcvd_rec, p_mgrp,
					OSM_LOG_NONE))
		goto Exit;

	if ((IB_MCR_COMPMASK_PROXY & comp_mask) &&
	    p_rcvd_rec->proxy_join != p_mgrp->mcmember_rec.proxy_join)
		goto Exit;

	/* need to validate mtu, rate, and pkt_lifetime fields */
	if (validate_more_comp_fields(sa->p_log, p_mgrp, p_rcvd_rec,
				      comp_mask) == FALSE)
		goto Exit;

	/* Port specific fields */
	/* so did we get the PortGUID mask */
	if (IB_MCR_COMPMASK_PORT_GID & comp_mask) {
		/* try to find this port */
		p_mcm_alias_guid = osm_mgrp_get_mcm_alias_guid(p_mgrp, portguid);
		if (!p_mcm_alias_guid) /* port not in group */
			goto Exit;
		scope_state = p_mcm_alias_guid->scope_state;
		memcpy(&port_gid, &(p_mcm_alias_guid->port_gid),
		       sizeof(ib_gid_t));
		proxy_join = p_mcm_alias_guid->proxy_join;
	} else /* point to the group information */
		scope_state = p_mgrp->mcmember_rec.scope_state;

	if (IB_MCR_COMPMASK_SCOPE & comp_mask)
		scope_state_mask = 0xF0;

	if (IB_MCR_COMPMASK_JOIN_STATE & comp_mask)
		scope_state_mask = scope_state_mask | 0x0F;

	/* Many MC records returned */
	if (trusted_req == TRUE && !(IB_MCR_COMPMASK_PORT_GID & comp_mask)) {
		char gid_str[INET6_ADDRSTRLEN];
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Trusted req is TRUE and no specific port defined\n");

		/* return all the ports that match in this MC group */
		p_item = cl_qmap_head(&(p_mgrp->mcm_alias_port_tbl));
		while (p_item != cl_qmap_end(&(p_mgrp->mcm_alias_port_tbl))) {
			p_mcm_alias_guid = (osm_mcm_alias_guid_t *) p_item;

			if ((scope_state_mask & p_rcvd_rec->scope_state) ==
			    (scope_state_mask & p_mcm_alias_guid->scope_state)) {
				/* add to the list */
				match_rec = p_mgrp->mcmember_rec;
				match_rec.scope_state = p_mcm_alias_guid->scope_state;
				memcpy(&match_rec.port_gid,
				       &p_mcm_alias_guid->port_gid,
				       sizeof(ib_gid_t));
				OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
					"Record of port_gid: %s"
					" in multicast_lid: 0x%X is returned\n",
					inet_ntop(AF_INET6,
						  match_rec.port_gid.raw,
						  gid_str, sizeof gid_str),
					cl_ntoh16(p_mgrp->mlid));

				match_rec.proxy_join =
				    (uint8_t) (p_mcm_alias_guid->proxy_join);

				mcmr_rcv_new_mcmr(sa, &match_rec, list);
			}
			p_item = cl_qmap_next(p_item);
		}
	} else { /* One MC record returned */
		if ((scope_state_mask & p_rcvd_rec->scope_state) !=
		    (scope_state_mask & scope_state))
			goto Exit;

		/* add to the list */
		match_rec = p_mgrp->mcmember_rec;
		match_rec.scope_state = scope_state;
		memcpy(&(match_rec.port_gid), &port_gid, sizeof(ib_gid_t));
		match_rec.proxy_join = (uint8_t) proxy_join;

		mcmr_rcv_new_mcmr(sa, &match_rec, list);
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

/**********************************************************************
 Handle a query request
**********************************************************************/
static void mcmr_query_mgrp(IN osm_sa_t * sa, IN osm_madw_t * p_madw)
{
	const ib_sa_mad_t *p_rcvd_mad;
	const ib_member_rec_t *p_rcvd_rec;
	cl_qlist_t rec_list;
	ib_net64_t comp_mask;
	osm_physp_t *p_req_physp;
	boolean_t trusted_req;
	osm_mgrp_t *p_mgrp;

	OSM_LOG_ENTER(sa->p_log);

	p_rcvd_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec = (ib_member_rec_t *) ib_sa_mad_get_payload_ptr(p_rcvd_mad);
	comp_mask = p_rcvd_mad->comp_mask;

	/*
	   if sm_key is not zero and does not match we never get here
	   see main SA receiver
	 */
	trusted_req = (p_rcvd_mad->sm_key != 0);

	CL_PLOCK_ACQUIRE(sa->p_lock);

	/* update the requester physical port */
	p_req_physp = osm_get_physp_by_mad_addr(sa->p_log, sa->p_subn,
						osm_madw_get_mad_addr_ptr
						(p_madw));
	if (p_req_physp == NULL) {
		CL_PLOCK_RELEASE(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B04: "
			"Cannot find requester physical port\n");
		goto Exit;
	}

	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Requester port GUID 0x%" PRIx64 "\n",
			cl_ntoh64(osm_physp_get_port_guid(p_req_physp)));
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Dump of record\n");
		osm_dump_mc_record(sa->p_log, p_rcvd_rec, OSM_LOG_DEBUG);
	}

	cl_qlist_init(&rec_list);

	/* simply go over all MCGs and match */
	for (p_mgrp = (osm_mgrp_t *) cl_fmap_head(&sa->p_subn->mgrp_mgid_tbl);
	     p_mgrp != (osm_mgrp_t *) cl_fmap_end(&sa->p_subn->mgrp_mgid_tbl);
	     p_mgrp = (osm_mgrp_t *) cl_fmap_next(&p_mgrp->map_item))
		mcmr_by_comp_mask(sa, p_rcvd_rec, comp_mask, p_mgrp,
				  p_req_physp, trusted_req, &rec_list);

	CL_PLOCK_RELEASE(sa->p_lock);

	/*
	   p923 - The PortGID, JoinState and ProxyJoin shall be zero,
	   except in the case of a trusted request.
	   Note: In the mad controller we check that the SM_Key received on
	   the mad is valid. Meaning - is either zero or equal to the local
	   sm_key.
	 */

	if (!p_rcvd_mad->sm_key) {
		osm_sa_item_t *item;
		for (item = (osm_sa_item_t *) cl_qlist_head(&rec_list);
		     item != (osm_sa_item_t *) cl_qlist_end(&rec_list);
		     item =
		     (osm_sa_item_t *) cl_qlist_next(&item->list_item)) {
			memset(&item->resp.mc_rec.port_gid, 0, sizeof(ib_gid_t));
			ib_member_set_join_state(&item->resp.mc_rec, 0);
			item->resp.mc_rec.proxy_join = 0;
		}
	}

	osm_sa_respond(sa, p_madw, sizeof(ib_member_rec_t), &rec_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

static uint8_t rate_is_valid(IN const ib_sa_mad_t *p_sa_mad,
			     IN const ib_member_rec_t *p_recvd_mcmember_rec)
{
	uint8_t rate;

	/* Validate rate if supplied */
	if ((p_sa_mad->comp_mask & IB_MCR_COMPMASK_RATE_SEL) &&
	    (p_sa_mad->comp_mask & IB_MCR_COMPMASK_RATE)) {
		rate = (uint8_t) (p_recvd_mcmember_rec->rate & 0x3F);
		return ib_rate_is_valid(rate);
	}
	return 1;
}

static int mtu_is_valid(IN const ib_sa_mad_t *p_sa_mad,
			IN const ib_member_rec_t *p_recvd_mcmember_rec)
{
	uint8_t mtu;

	/* Validate MTU if supplied */
	if ((p_sa_mad->comp_mask & IB_MCR_COMPMASK_MTU_SEL) &&
	    (p_sa_mad->comp_mask & IB_MCR_COMPMASK_MTU)) {
		mtu = (uint8_t) (p_recvd_mcmember_rec->mtu & 0x3F);
		return ib_mtu_is_valid(mtu);
	}
	return 1;
}

void osm_mcmr_rcv_process(IN void *context, IN void *data)
{
	osm_sa_t *sa = context;
	osm_madw_t *p_madw = data;
	ib_sa_mad_t *p_sa_mad;
	ib_member_rec_t *p_recvd_mcmember_rec;

	CL_ASSERT(sa);

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_mcmember_rec =
	    (ib_member_rec_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	CL_ASSERT(p_sa_mad->attr_id == IB_MAD_ATTR_MCMEMBER_RECORD);

	switch (p_sa_mad->method) {
	case IB_MAD_METHOD_SET:
		if (!check_join_comp_mask(p_sa_mad->comp_mask)) {
			char gid_str[INET6_ADDRSTRLEN];
			char gid_str2[INET6_ADDRSTRLEN];
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B18: "
				"component mask = 0x%016" PRIx64 ", "
				"expected comp mask = 0x%016" PRIx64 ", "
				"MGID: %s for PortGID: %s\n",
				cl_ntoh64(p_sa_mad->comp_mask),
				CL_NTOH64(JOIN_MC_COMP_MASK),
				inet_ntop(AF_INET6,
					  p_recvd_mcmember_rec->mgid.raw,
					  gid_str, sizeof gid_str),
				inet_ntop(AF_INET6,
					  p_recvd_mcmember_rec->port_gid.raw,
					  gid_str2, sizeof gid_str2));
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_INSUF_COMPS);
			goto Exit;
		}
		if (!rate_is_valid(p_sa_mad, p_recvd_mcmember_rec) ||
		    !mtu_is_valid(p_sa_mad, p_recvd_mcmember_rec)) {
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
			goto Exit;
		}

		/*
		 * Join or Create Multicast Group
		 */
		mcmr_rcv_join_mgrp(sa, p_madw);
		break;
	case IB_MAD_METHOD_DELETE:
		if (!check_join_comp_mask(p_sa_mad->comp_mask)) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B20: "
				"component mask = 0x%016" PRIx64 ", "
				"expected comp mask = 0x%016" PRIx64 "\n",
				cl_ntoh64(p_sa_mad->comp_mask),
				CL_NTOH64(JOIN_MC_COMP_MASK));
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_INSUF_COMPS);
			goto Exit;
		}
		if (!rate_is_valid(p_sa_mad, p_recvd_mcmember_rec) ||
		    !mtu_is_valid(p_sa_mad, p_recvd_mcmember_rec)) {
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
			goto Exit;
		}

		/*
		 * Leave Multicast Group
		 */
		mcmr_rcv_leave_mgrp(sa, p_madw);
		break;
	case IB_MAD_METHOD_GET:
	case IB_MAD_METHOD_GETTABLE:
		if (!rate_is_valid(p_sa_mad, p_recvd_mcmember_rec) ||
		    !mtu_is_valid(p_sa_mad, p_recvd_mcmember_rec)) {
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
			goto Exit;
		}

		/*
		 * Querying a Multicast Group
		 */
		mcmr_query_mgrp(sa, p_madw);
		break;
	default:
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1B21: "
			"Unsupported Method (%s) for MCMemberRecord request\n",
			ib_get_sa_method_str(p_sa_mad->method));
		osm_sa_send_error(sa, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		break;
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return;
}
