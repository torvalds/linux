/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2015 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
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
 *    Implementation of inform record functions.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_INFORM_C
#include <opensm/osm_helper.h>
#include <opensm/osm_inform.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_pkey.h>
#include <opensm/osm_sa.h>
#include <opensm/osm_opensm.h>

typedef struct osm_infr_match_ctxt {
	cl_list_t *p_remove_infr_list;
	ib_mad_notice_attr_t *p_ntc;
} osm_infr_match_ctxt_t;

void osm_infr_delete(IN osm_infr_t * p_infr)
{
	free(p_infr);
}

osm_infr_t *osm_infr_new(IN const osm_infr_t * p_infr_rec)
{
	osm_infr_t *p_infr;

	CL_ASSERT(p_infr_rec);

	p_infr = (osm_infr_t *) malloc(sizeof(osm_infr_t));
	if (p_infr)
		memcpy(p_infr, p_infr_rec, sizeof(osm_infr_t));

	return p_infr;
}

static void dump_all_informs(IN const osm_subn_t * p_subn, IN osm_log_t * p_log)
{
	cl_list_item_t *p_list_item;

	if (!OSM_LOG_IS_ACTIVE_V2(p_log, OSM_LOG_DEBUG))
		return;

	p_list_item = cl_qlist_head(&p_subn->sa_infr_list);
	while (p_list_item != cl_qlist_end(&p_subn->sa_infr_list)) {
		osm_dump_inform_info_v2(p_log,
				        &((osm_infr_t *) p_list_item)->
				        inform_record.inform_info, FILE_ID, OSM_LOG_DEBUG);
		p_list_item = cl_qlist_next(p_list_item);
	}
}

/**********************************************************************
 * Match an infr by the InformInfo and Address vector
 **********************************************************************/
static cl_status_t match_inf_rec(IN const cl_list_item_t * p_list_item,
				 IN void *context)
{
	osm_infr_t *p_infr_rec = (osm_infr_t *) context;
	osm_infr_t *p_infr = (osm_infr_t *) p_list_item;
	ib_inform_info_t *p_ii_rec = &p_infr_rec->inform_record.inform_info;
	ib_inform_info_t *p_ii = &p_infr->inform_record.inform_info;
	osm_log_t *p_log = p_infr_rec->sa->p_log;
	cl_status_t status = CL_NOT_FOUND;

	OSM_LOG_ENTER(p_log);

	if (memcmp(&p_infr->report_addr, &p_infr_rec->report_addr,
		   sizeof(p_infr_rec->report_addr))) {
		OSM_LOG(p_log, OSM_LOG_DEBUG, "Differ by Address\n");
		goto Exit;
	}

	/* if inform_info.gid is not zero, ignore lid range */
	if (ib_gid_is_notzero(&p_ii_rec->gid)) {
		if (memcmp(&p_ii->gid, &p_ii_rec->gid, sizeof(p_ii->gid))) {
			OSM_LOG(p_log, OSM_LOG_DEBUG,
				"Differ by InformInfo.gid\n");
			goto Exit;
		}
	} else {
		if ((p_ii->lid_range_begin != p_ii_rec->lid_range_begin) ||
		    (p_ii->lid_range_end != p_ii_rec->lid_range_end)) {
			OSM_LOG(p_log, OSM_LOG_DEBUG,
				"Differ by InformInfo.LIDRange\n");
			goto Exit;
		}
	}

	if (p_ii->trap_type != p_ii_rec->trap_type) {
		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"Differ by InformInfo.TrapType\n");
		goto Exit;
	}

	if (p_ii->is_generic != p_ii_rec->is_generic) {
		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"Differ by InformInfo.IsGeneric\n");
		goto Exit;
	}

	if (p_ii->is_generic) {
		if (p_ii->g_or_v.generic.trap_num !=
		    p_ii_rec->g_or_v.generic.trap_num)
			OSM_LOG(p_log, OSM_LOG_DEBUG,
				"Differ by InformInfo.Generic.TrapNumber\n");
		else if (p_ii->g_or_v.generic.qpn_resp_time_val !=
			 p_ii_rec->g_or_v.generic.qpn_resp_time_val)
			OSM_LOG(p_log, OSM_LOG_DEBUG,
				"Differ by InformInfo.Generic.QPNRespTimeVal\n");
		else if (p_ii->g_or_v.generic.node_type_msb !=
			 p_ii_rec->g_or_v.generic.node_type_msb)
			OSM_LOG(p_log, OSM_LOG_DEBUG,
				"Differ by InformInfo.Generic.NodeTypeMSB\n");
		else if (p_ii->g_or_v.generic.node_type_lsb !=
			 p_ii_rec->g_or_v.generic.node_type_lsb)
			OSM_LOG(p_log, OSM_LOG_DEBUG,
				"Differ by InformInfo.Generic.NodeTypeLSB\n");
		else
			status = CL_SUCCESS;
	} else {
		if (p_ii->g_or_v.vend.dev_id != p_ii_rec->g_or_v.vend.dev_id)
			OSM_LOG(p_log, OSM_LOG_DEBUG,
				"Differ by InformInfo.Vendor.DeviceID\n");
		else if (p_ii->g_or_v.vend.qpn_resp_time_val !=
			 p_ii_rec->g_or_v.vend.qpn_resp_time_val)
			OSM_LOG(p_log, OSM_LOG_DEBUG,
				"Differ by InformInfo.Vendor.QPNRespTimeVal\n");
		else if (p_ii->g_or_v.vend.vendor_id_msb !=
			 p_ii_rec->g_or_v.vend.vendor_id_msb)
			OSM_LOG(p_log, OSM_LOG_DEBUG,
				"Differ by InformInfo.Vendor.VendorIdMSB\n");
		else if (p_ii->g_or_v.vend.vendor_id_lsb !=
			 p_ii_rec->g_or_v.vend.vendor_id_lsb)
			OSM_LOG(p_log, OSM_LOG_DEBUG,
				"Differ by InformInfo.Vendor.VendorIdLSB\n");
		else
			status = CL_SUCCESS;
	}

Exit:
	OSM_LOG_EXIT(p_log);
	return status;
}

osm_infr_t *osm_infr_get_by_rec(IN osm_subn_t const *p_subn,
				IN osm_log_t * p_log,
				IN osm_infr_t * p_infr_rec)
{
	cl_list_item_t *p_list_item;

	OSM_LOG_ENTER(p_log);

	dump_all_informs(p_subn, p_log);

	OSM_LOG(p_log, OSM_LOG_DEBUG, "Looking for Inform Record\n");
	osm_dump_inform_info_v2(p_log, &(p_infr_rec->inform_record.inform_info),
			        FILE_ID, OSM_LOG_DEBUG);
	OSM_LOG(p_log, OSM_LOG_DEBUG, "InformInfo list size %d\n",
		cl_qlist_count(&p_subn->sa_infr_list));

	p_list_item = cl_qlist_find_from_head(&p_subn->sa_infr_list,
					      match_inf_rec, p_infr_rec);

	if (p_list_item == cl_qlist_end(&p_subn->sa_infr_list))
		p_list_item = NULL;

	OSM_LOG_EXIT(p_log);
	return (osm_infr_t *) p_list_item;
}

void osm_infr_insert_to_db(IN osm_subn_t * p_subn, IN osm_log_t * p_log,
			   IN osm_infr_t * p_infr)
{
	OSM_LOG_ENTER(p_log);

	OSM_LOG(p_log, OSM_LOG_DEBUG,
		"Inserting new InformInfo Record into Database\n");
	OSM_LOG(p_log, OSM_LOG_DEBUG, "Dump before insertion (size %d)\n",
		cl_qlist_count(&p_subn->sa_infr_list));
	dump_all_informs(p_subn, p_log);

#if 0
	osm_dump_inform_info_v2(p_log,
			        &(p_infr->inform_record.inform_info),
			        FILE_ID, OSM_LOG_DEBUG);
#endif

	cl_qlist_insert_head(&p_subn->sa_infr_list, &p_infr->list_item);
	p_subn->p_osm->sa.dirty = TRUE;

	OSM_LOG(p_log, OSM_LOG_DEBUG, "Dump after insertion (size %d)\n",
		cl_qlist_count(&p_subn->sa_infr_list));
	dump_all_informs(p_subn, p_log);
	OSM_LOG_EXIT(p_log);
}

void osm_infr_remove_from_db(IN osm_subn_t * p_subn, IN osm_log_t * p_log,
			     IN osm_infr_t * p_infr)
{
	char gid_str[INET6_ADDRSTRLEN];
	OSM_LOG_ENTER(p_log);

	OSM_LOG(p_log, OSM_LOG_DEBUG, "Removing InformInfo Subscribing GID:%s"
		" Enum:0x%X from Database\n",
		inet_ntop(AF_INET6, p_infr->inform_record.subscriber_gid.raw,
			  gid_str, sizeof gid_str),
		p_infr->inform_record.subscriber_enum);

	osm_dump_inform_info_v2(p_log, &(p_infr->inform_record.inform_info),
			        FILE_ID, OSM_LOG_DEBUG);

	cl_qlist_remove_item(&p_subn->sa_infr_list, &p_infr->list_item);
	p_subn->p_osm->sa.dirty = TRUE;

	osm_infr_delete(p_infr);

	OSM_LOG_EXIT(p_log);
}

ib_api_status_t osm_infr_remove_subscriptions(IN osm_subn_t * p_subn,
					      IN osm_log_t * p_log,
					      IN ib_net64_t port_guid)
{
	cl_list_item_t *p_list_item;
	osm_infr_t *p_infr;
	ib_api_status_t status = CL_NOT_FOUND;

	OSM_LOG_ENTER(p_log);

	/* go over all inform info available at the subnet */
	/* match to the given GID and delete subscriptions if match */
	p_list_item = cl_qlist_head(&p_subn->sa_infr_list);
	while (p_list_item != cl_qlist_end(&p_subn->sa_infr_list)) {

		p_infr = (osm_infr_t *)p_list_item;
		p_list_item = cl_qlist_next(p_list_item);

		if (port_guid != p_infr->inform_record.subscriber_gid.unicast.interface_id)
			continue;

		/* Remove this event subscription */
		osm_infr_remove_from_db(p_subn, p_log, p_infr);

		status = CL_SUCCESS;
	}

	OSM_LOG_EXIT(p_log);
	return (status);
}

/**********************************************************************
 * Send a report:
 * Given a target address to send to and the notice.
 * We need to send SubnAdmReport
 **********************************************************************/
static ib_api_status_t send_report(IN osm_infr_t * p_infr_rec,	/* the informinfo */
				   IN ib_mad_notice_attr_t * p_ntc	/* notice to send */
    )
{
	osm_madw_t *p_report_madw;
	ib_mad_notice_attr_t *p_report_ntc;
	ib_mad_t *p_mad;
	ib_sa_mad_t *p_sa_mad;
	static atomic32_t trap_fwd_trans_id = 0x02DAB000;
	ib_api_status_t status = IB_SUCCESS;
	osm_log_t *p_log = p_infr_rec->sa->p_log;
	ib_net64_t tid;

	OSM_LOG_ENTER(p_log);

	/* HACK: who switches or uses the src and dest GIDs in the grh_info ?? */

	/* it is better to use LIDs since the GIDs might not be there for SMI traps */
	OSM_LOG(p_log, OSM_LOG_DEBUG, "Forwarding Notice Event from LID %u"
		" to InformInfo LID %u GUID 0x%" PRIx64 ", TID 0x%X\n",
		cl_ntoh16(p_ntc->issuer_lid),
		cl_ntoh16(p_infr_rec->report_addr.dest_lid),
		cl_ntoh64(p_infr_rec->inform_record.subscriber_gid.unicast.interface_id),
		trap_fwd_trans_id);

	/* get the MAD to send */
	p_report_madw = osm_mad_pool_get(p_infr_rec->sa->p_mad_pool,
					 p_infr_rec->h_bind, MAD_BLOCK_SIZE,
					 &(p_infr_rec->report_addr));

	if (!p_report_madw) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0203: "
			"Cannot send report to LID %u, osm_mad_pool_get failed\n",
			cl_ntoh16(p_infr_rec->report_addr.dest_lid));
		status = IB_ERROR;
		goto Exit;
	}

	p_report_madw->resp_expected = TRUE;

	/* advance trap trans id (cant simply ++ on some systems inside ntoh) */
	tid = cl_hton64((uint64_t) cl_atomic_inc(&trap_fwd_trans_id) &
			(uint64_t) (0xFFFFFFFF));
	if (trap_fwd_trans_id == 0)
		tid = cl_hton64((uint64_t) cl_atomic_inc(&trap_fwd_trans_id) &
				(uint64_t) (0xFFFFFFFF));
	p_mad = osm_madw_get_mad_ptr(p_report_madw);
	ib_mad_init_new(p_mad, IB_MCLASS_SUBN_ADM, 2, IB_MAD_METHOD_REPORT,
			tid, IB_MAD_ATTR_NOTICE, 0);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_report_madw);

	p_report_ntc = (ib_mad_notice_attr_t *) & (p_sa_mad->data);

	/* copy the notice */
	*p_report_ntc = *p_ntc;

	/* The TRUE is for: response is expected */
	osm_sa_send(p_infr_rec->sa, p_report_madw, TRUE);

Exit:
	OSM_LOG_EXIT(p_log);
	return status;
}

static int is_access_permitted(osm_infr_t *p_infr_rec,
			       osm_infr_match_ctxt_t *p_infr_match )
{
	cl_list_t *p_infr_to_remove_list = p_infr_match->p_remove_infr_list;
	ib_inform_info_t *p_ii = &(p_infr_rec->inform_record.inform_info);
	ib_mad_notice_attr_t *p_ntc = p_infr_match->p_ntc;
	uint16_t trap_num = cl_ntoh16(p_ntc->g_or_v.generic.trap_num);
	osm_subn_t *p_subn = p_infr_rec->sa->p_subn;
	osm_log_t *p_log = p_infr_rec->sa->p_log;
	osm_mgrp_t *p_mgrp;
	ib_gid_t source_gid;
	osm_port_t *p_src_port;
	osm_port_t *p_dest_port;

	/* In case of SM_GID_IN_SERVICE_TRAP(64) or SM_GID_OUT_OF_SERVICE_TRAP(65) traps
	   the source gid comparison should be done on the trap source (saved
	   as the gid in the data details field).
	   For traps SM_MGID_CREATED_TRAP(66) or SM_MGID_DESTROYED_TRAP(67)
	   the data details gid is the MGID.
	   We need to check whether the subscriber has a compatible
	   pkey with MC group.
	   In all other cases the issuer gid is the trap source.
	*/
	if (trap_num >= SM_GID_IN_SERVICE_TRAP &&
	    trap_num <= SM_MGID_DESTROYED_TRAP)
		/* The issuer of these traps is the SM so source_gid
		   is the gid saved on the data details */
		source_gid = p_ntc->data_details.ntc_64_67.gid;
	else
		source_gid = p_ntc->issuer_gid;

	p_dest_port = osm_get_port_by_lid(p_subn,
					  p_infr_rec->report_addr.dest_lid);
	if (!p_dest_port) {
		OSM_LOG(p_log, OSM_LOG_INFO,
			"Cannot find destination port with LID:%u\n",
			cl_ntoh16(p_infr_rec->report_addr.dest_lid));
		goto Exit;
	}

	/* Check if there is a pkey match. o13-17.1.1 */
	switch (trap_num) {
		case SM_MGID_CREATED_TRAP:
		case SM_MGID_DESTROYED_TRAP:
			p_mgrp = osm_get_mgrp_by_mgid(p_subn, &source_gid);
			if (!p_mgrp) {
				char gid_str[INET6_ADDRSTRLEN];
				OSM_LOG(p_log, OSM_LOG_INFO,
					"Cannot find MGID %s\n",
					inet_ntop(AF_INET6, source_gid.raw, gid_str, sizeof gid_str));
				goto Exit;
			}

			if (!osm_physp_has_pkey(p_log,
						p_mgrp->mcmember_rec.pkey,
						p_dest_port->p_physp)) {
				char gid_str[INET6_ADDRSTRLEN];
				OSM_LOG(p_log, OSM_LOG_INFO,
					"MGID %s and port GUID:0x%016" PRIx64 " do not share same pkey\n",
					inet_ntop(AF_INET6, source_gid.raw, gid_str, sizeof gid_str),
					cl_ntoh64(p_dest_port->guid));
				goto Exit;
			}
			break;

		default:
			p_src_port =
			    osm_get_port_by_guid(p_subn, source_gid.unicast.interface_id);
			if (!p_src_port) {
				OSM_LOG(p_log, OSM_LOG_INFO,
					"Cannot find source port with GUID:0x%016" PRIx64 "\n",
					cl_ntoh64(source_gid.unicast.interface_id));
				goto Exit;
			}


			if (osm_port_share_pkey(p_log, p_src_port, p_dest_port,
						p_subn->opt.allow_both_pkeys) == FALSE) {
				OSM_LOG(p_log, OSM_LOG_DEBUG, "Mismatch by Pkey\n");
				/* According to o13-17.1.2 - If this informInfo
				   does not have lid_range_begin of 0xFFFF,
				   then this informInfo request should be
				   removed from database */
				if (p_ii->lid_range_begin != 0xFFFF) {
					OSM_LOG(p_log, OSM_LOG_VERBOSE,
						"Pkey mismatch on lid_range_begin != 0xFFFF. "
						"Need to remove this informInfo from db\n");
					/* add the informInfo record to the remove_infr list */
					cl_list_insert_tail(p_infr_to_remove_list, p_infr_rec);
				}
				goto Exit;
			}
			break;
	}

	return 1;
Exit:
	return 0;
}


/**********************************************************************
 * This routine compares a given Notice and a ListItem of InformInfo type.
 * PREREQUISITE:
 * The Notice.GID should be pre-filled with the trap generator GID
 **********************************************************************/
static void match_notice_to_inf_rec(IN cl_list_item_t * p_list_item,
				    IN void *context)
{
	osm_infr_match_ctxt_t *p_infr_match = (osm_infr_match_ctxt_t *) context;
	ib_mad_notice_attr_t *p_ntc = p_infr_match->p_ntc;
	osm_infr_t *p_infr_rec = (osm_infr_t *) p_list_item;
	ib_inform_info_t *p_ii = &(p_infr_rec->inform_record.inform_info);
	osm_log_t *p_log = p_infr_rec->sa->p_log;

	OSM_LOG_ENTER(p_log);

	/* matching rules
	 * InformInfo   Notice
	 * GID          IssuerGID    if non zero must match the trap
	 * LIDRange     IssuerLID    apply only if GID=0
	 * IsGeneric    IsGeneric    is compulsory and must match the trap
	 * Type         Type         if not 0xFFFF must match
	 * TrapNumber   TrapNumber   if not 0xFFFF must match
	 * DeviceId     DeviceID     if not 0xFFFF must match
	 * QPN dont care
	 * ProducerType ProducerType match or 0xFFFFFF // EZ: actually my interpretation
	 * VendorID     VendorID     match or 0xFFFFFF
	 */

	/* GID          IssuerGID    if non zero must match the trap  */
	if (p_ii->gid.unicast.prefix != 0
	    || p_ii->gid.unicast.interface_id != 0) {
		/* match by GID */
		if (memcmp(&(p_ii->gid), &(p_ntc->issuer_gid),
			   sizeof(ib_gid_t))) {
			OSM_LOG(p_log, OSM_LOG_DEBUG, "Mismatch by GID\n");
			goto Exit;
		}
	} else {
		/* LIDRange     IssuerLID    apply only if GID=0 */
		/* If lid_range_begin of the informInfo is 0xFFFF - then it should be ignored. */
		if (p_ii->lid_range_begin != 0xFFFF) {
			/* a real lid range is given - check it */
			if ((cl_hton16(p_ii->lid_range_begin) >
			     cl_hton16(p_ntc->issuer_lid))
			    || (cl_hton16(p_ntc->issuer_lid) >
				cl_hton16(p_ii->lid_range_end))) {
				OSM_LOG(p_log, OSM_LOG_DEBUG,
					"Mismatch by LID Range. Needed: %u <= %u <= %u\n",
					cl_hton16(p_ii->lid_range_begin),
					cl_hton16(p_ntc->issuer_lid),
					cl_hton16(p_ii->lid_range_end));
				goto Exit;
			}
		}
	}

	/* IsGeneric    IsGeneric    is compulsory and must match the trap  */
	if ((p_ii->is_generic && !ib_notice_is_generic(p_ntc)) ||
	    (!p_ii->is_generic && ib_notice_is_generic(p_ntc))) {
		OSM_LOG(p_log, OSM_LOG_DEBUG, "Mismatch by Generic/Vendor\n");
		goto Exit;
	}

	/* Type         Type         if not 0xFFFF must match */
	if ((p_ii->trap_type != 0xFFFF) &&
	    (cl_ntoh16(p_ii->trap_type) != ib_notice_get_type(p_ntc))) {
		OSM_LOG(p_log, OSM_LOG_DEBUG, "Mismatch by Type\n");
		goto Exit;
	}

	/* based on generic type */
	if (p_ii->is_generic) {
		/* TrapNumber   TrapNumber   if not 0xFFFF must match */
		if ((p_ii->g_or_v.generic.trap_num != 0xFFFF) &&
		    (p_ii->g_or_v.generic.trap_num !=
		     p_ntc->g_or_v.generic.trap_num)) {
			OSM_LOG(p_log, OSM_LOG_DEBUG, "Mismatch by Trap Num\n");
			goto Exit;
		}

		/* ProducerType ProducerType match or 0xFFFFFF  */
		if ((cl_ntoh32(ib_inform_info_get_prod_type(p_ii)) != 0xFFFFFF)
		    && (ib_inform_info_get_prod_type(p_ii) !=
			ib_notice_get_prod_type(p_ntc))) {
			OSM_LOG(p_log, OSM_LOG_DEBUG,
				"Mismatch by Node Type: II=0x%06X (%s) Trap=0x%06X (%s)\n",
				cl_ntoh32(ib_inform_info_get_prod_type(p_ii)),
				ib_get_producer_type_str
				(ib_inform_info_get_prod_type(p_ii)),
				cl_ntoh32(ib_notice_get_prod_type(p_ntc)),
				ib_get_producer_type_str(ib_notice_get_prod_type
							 (p_ntc)));
			goto Exit;
		}
	} else {
		/* DeviceId     DeviceID     if not 0xFFFF must match */
		if ((p_ii->g_or_v.vend.dev_id != 0xFFFF) &&
		    (p_ii->g_or_v.vend.dev_id != p_ntc->g_or_v.vend.dev_id)) {
			OSM_LOG(p_log, OSM_LOG_DEBUG, "Mismatch by Dev Id\n");
			goto Exit;
		}

		/* VendorID     VendorID     match or 0xFFFFFF  */
		if ((ib_inform_info_get_vend_id(p_ii) != CL_HTON32(0xFFFFFF)) &&
		    (ib_inform_info_get_vend_id(p_ii) !=
		     ib_notice_get_vend_id(p_ntc))) {
			OSM_LOG(p_log, OSM_LOG_DEBUG,
				"Mismatch by Vendor ID\n");
			goto Exit;
		}
	}

	if (!is_access_permitted(p_infr_rec, p_infr_match))
		goto Exit;

	/* send the report to the address provided in the inform record */
	OSM_LOG(p_log, OSM_LOG_DEBUG, "MATCH! Sending Report...\n");
	send_report(p_infr_rec, p_ntc);

Exit:
	OSM_LOG_EXIT(p_log);
}

/**********************************************************************
 * Once a Trap was received by osm_trap_rcv, or a Trap sourced by
 * the SM was sent (Traps 64-67), this routine is called with a copy of
 * the notice data.
 * Given a notice attribute - compare and see if it matches the InformInfo
 * element and if it does - call the Report(Notice) for the
 * target QP registered by the address stored in the InformInfo element
 **********************************************************************/
static void log_notice(osm_log_t * log, osm_log_level_t level,
		       ib_mad_notice_attr_t * ntc)
{
	char gid_str[INET6_ADDRSTRLEN], gid_str2[INET6_ADDRSTRLEN];
	ib_gid_t *gid;
	ib_gid_t *gid1, *gid2;

	/* an official Event information log */
	if (ib_notice_is_generic(ntc)) {
		if ((ntc->g_or_v.generic.trap_num == CL_HTON16(SM_GID_IN_SERVICE_TRAP)) ||
		    (ntc->g_or_v.generic.trap_num == CL_HTON16(SM_GID_OUT_OF_SERVICE_TRAP)) ||
		    (ntc->g_or_v.generic.trap_num == CL_HTON16(SM_MGID_CREATED_TRAP)) ||
		    (ntc->g_or_v.generic.trap_num == CL_HTON16(SM_MGID_DESTROYED_TRAP)))
			gid = &ntc->data_details.ntc_64_67.gid;
		else
			gid = &ntc->issuer_gid;

		switch (cl_ntoh16(ntc->g_or_v.generic.trap_num)) {
		case SM_GID_IN_SERVICE_TRAP:
		case SM_GID_OUT_OF_SERVICE_TRAP:
			OSM_LOG(log, level,
				"Reporting Informational Notice \"%s\", GID:%s\n",
				ib_get_trap_str(ntc->g_or_v.generic.trap_num),
				inet_ntop(AF_INET6, gid->raw, gid_str, sizeof gid_str));
			break;
		case SM_MGID_CREATED_TRAP:
		case SM_MGID_DESTROYED_TRAP:
			OSM_LOG(log, level,
				"Reporting Informational Notice \"%s\", MGID:%s\n",
				ib_get_trap_str(ntc->g_or_v.generic.trap_num),
				inet_ntop(AF_INET6, gid->raw, gid_str, sizeof gid_str));
			break;
		case SM_UNPATH_TRAP:
		case SM_REPATH_TRAP:
			/* TODO: Fill in details once SM starts to use these traps */
			OSM_LOG(log, level,
				"Reporting Informational Notice \"%s\"n",
				ib_get_trap_str(ntc->g_or_v.generic.trap_num));
			break;
		case SM_LINK_STATE_CHANGED_TRAP:
			OSM_LOG(log, level,
				"Reporting Urgent Notice \"%s\" from switch LID %u, "
				"GUID 0x%016" PRIx64 "\n",
				ib_get_trap_str(ntc->g_or_v.generic.trap_num),
				cl_ntoh16(ntc->issuer_lid),
				cl_ntoh64(gid->unicast.interface_id));
			break;
		case SM_LINK_INTEGRITY_THRESHOLD_TRAP:
		case SM_BUFFER_OVERRUN_THRESHOLD_TRAP:
		case SM_WATCHDOG_TIMER_EXPIRED_TRAP:
			OSM_LOG(log, level,
				"Reporting Urgent Notice \"%s\" from LID %u, "
				"GUID 0x%016" PRIx64 ", port %u\n",
				ib_get_trap_str(ntc->g_or_v.generic.trap_num),
				cl_ntoh16(ntc->issuer_lid),
				cl_ntoh64(gid->unicast.interface_id),
				ntc->data_details.ntc_129_131.port_num);
			break;
		case SM_LOCAL_CHANGES_TRAP:
			if (ntc->data_details.ntc_144.local_changes == 1)
				OSM_LOG(log, level,
					"Reporting Informational Notice \"%s\" from LID %u, "
					"GUID 0x%016" PRIx64 ", ChangeFlags 0x%04x, "
					"CapabilityMask2 0x%04x\n",
					ib_get_trap_str(ntc->g_or_v.generic.trap_num),
					cl_ntoh16(ntc->issuer_lid),
					cl_ntoh64(gid->unicast.interface_id),
					cl_ntoh16(ntc->data_details.ntc_144.change_flgs),
					cl_ntoh16(ntc->data_details.ntc_144.cap_mask2));
			else
				OSM_LOG(log, level,
					"Reporting Informational Notice \"%s\" from LID %u, "
					"GUID 0x%016" PRIx64 ", new CapabilityMask 0x%08x\n",
					ib_get_trap_str(ntc->g_or_v.generic.trap_num),
					cl_ntoh16(ntc->issuer_lid),
					cl_ntoh64(gid->unicast.interface_id),
					cl_ntoh32(ntc->data_details.ntc_144.new_cap_mask));
			break;
		case SM_SYS_IMG_GUID_CHANGED_TRAP:
			OSM_LOG(log, level,
				"Reporting Informational Notice \"%s\" from LID %u, "
				"GUID 0x%016" PRIx64 ", new SysImageGUID 0x%016" PRIx64 "\n",
				ib_get_trap_str(ntc->g_or_v.generic.trap_num),
				cl_ntoh16(ntc->issuer_lid),
				cl_ntoh64(gid->unicast.interface_id),
				cl_ntoh64(ntc->data_details.ntc_145.new_sys_guid));
			break;
		case SM_BAD_MKEY_TRAP:
			OSM_LOG(log, level,
				"Reporting Security Notice \"%s\" from LID %u, "
				"GUID 0x%016" PRIx64 ", Method 0x%x, Attribute 0x%x, "
				"AttrMod 0x%x, M_Key 0x%016" PRIx64 "\n",
				ib_get_trap_str(ntc->g_or_v.generic.trap_num),
				cl_ntoh16(ntc->issuer_lid),
				cl_ntoh64(gid->unicast.interface_id),
				ntc->data_details.ntc_256.method,
				cl_ntoh16(ntc->data_details.ntc_256.attr_id),
				cl_ntoh32(ntc->data_details.ntc_256.attr_mod),
				cl_ntoh64(ntc->data_details.ntc_256.mkey));
			break;
		case SM_BAD_PKEY_TRAP:
		case SM_BAD_QKEY_TRAP:
			gid1 = &ntc->data_details.ntc_257_258.gid1;
			gid2 = &ntc->data_details.ntc_257_258.gid2;
			OSM_LOG(log, level,
				"Reporting Security Notice \"%s\" from LID %u, "
				"GUID 0x%016" PRIx64 " : LID1 %u, LID2 %u, %s 0x%x, "
				"SL %d, QP1 0x%x, QP2 0x%x, GID1 %s, GID2 %s\n",
				ib_get_trap_str(ntc->g_or_v.generic.trap_num),
				cl_ntoh16(ntc->issuer_lid),
				cl_ntoh64(gid->unicast.interface_id),
				cl_ntoh16(ntc->data_details.ntc_257_258.lid1),
				cl_ntoh16(ntc->data_details.ntc_257_258.lid2),
				cl_ntoh16(ntc->g_or_v.generic.trap_num) == SM_BAD_QKEY_TRAP ?
					"Q_Key" : "P_Key",
				cl_ntoh32(ntc->data_details.ntc_257_258.key),
				cl_ntoh32(ntc->data_details.ntc_257_258.qp1) >> 28,
				cl_ntoh32(ntc->data_details.ntc_257_258.qp1) & 0xffffff,
				cl_ntoh32(ntc->data_details.ntc_257_258.qp2) & 0xffffff,
				inet_ntop(AF_INET6, gid1->raw, gid_str, sizeof gid_str),
				inet_ntop(AF_INET6, gid2->raw, gid_str2, sizeof gid_str2));
			break;
		case SM_BAD_SWITCH_PKEY_TRAP:
			gid1 = &ntc->data_details.ntc_259.gid1;
			gid2 = &ntc->data_details.ntc_259.gid2;
			OSM_LOG(log, level,
				"Reporting Security Notice \"%s\" from switch LID %u, "
				"GUID 0x%016" PRIx64 " port %d : data_valid 0x%04x, "
				"LID1 %u, LID2 %u, PKey 0x%04x, "
				"SL %d, QP1 0x%x, QP2 0x%x, GID1 %s, GID2 %s\n",
				ib_get_trap_str(ntc->g_or_v.generic.trap_num),
				cl_ntoh16(ntc->issuer_lid),
				cl_ntoh64(gid->unicast.interface_id),
				ntc->data_details.ntc_259.port_no,
				cl_ntoh16(ntc->data_details.ntc_259.data_valid),
				cl_ntoh16(ntc->data_details.ntc_259.lid1),
				cl_ntoh16(ntc->data_details.ntc_259.lid2),
				cl_ntoh16(ntc->data_details.ntc_257_258.key),
				cl_ntoh32(ntc->data_details.ntc_259.sl_qp1) >> 28,
				cl_ntoh32(ntc->data_details.ntc_259.sl_qp1) & 0xffffff,
				cl_ntoh32(ntc->data_details.ntc_259.qp2),
				inet_ntop(AF_INET6, gid1->raw, gid_str, sizeof gid_str),
				inet_ntop(AF_INET6, gid2->raw, gid_str2, sizeof gid_str2));
			break;
		default:
			OSM_LOG(log, level,
				"Reporting Generic Notice type:%u num:%u (%s)"
				" from LID:%u GID:%s\n",
				ib_notice_get_type(ntc),
				cl_ntoh16(ntc->g_or_v.generic.trap_num),
				ib_get_trap_str(ntc->g_or_v.generic.trap_num),
				cl_ntoh16(ntc->issuer_lid),
				inet_ntop(AF_INET6, gid->raw, gid_str, sizeof gid_str));
			break;
		}
	} else
		OSM_LOG(log, level,
			"Reporting Vendor Notice type:%u vend:%u dev:%u"
			" from LID:%u GID:%s\n",
			ib_notice_get_type(ntc),
			cl_ntoh32(ib_notice_get_vend_id(ntc)),
			cl_ntoh16(ntc->g_or_v.vend.dev_id),
			cl_ntoh16(ntc->issuer_lid),
			inet_ntop(AF_INET6, ntc->issuer_gid.raw, gid_str,
				  sizeof gid_str));
}

ib_api_status_t osm_report_notice(IN osm_log_t * p_log, IN osm_subn_t * p_subn,
				  IN ib_mad_notice_attr_t * p_ntc)
{
	osm_infr_match_ctxt_t context;
	cl_list_t infr_to_remove_list;
	osm_infr_t *p_infr_rec;
	osm_infr_t *p_next_infr_rec;

	OSM_LOG_ENTER(p_log);

	/*
	 * we must make sure we are ready for this...
	 * note that the trap receivers might be initialized before
	 * the osm_infr_init call is performed.
	 */
	if (p_subn->sa_infr_list.state != CL_INITIALIZED) {
		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"Ignoring Notice Reports since Inform List is not initialized yet!\n");
		return IB_ERROR;
	}

	if (OSM_LOG_IS_ACTIVE_V2(p_log, OSM_LOG_INFO))
		log_notice(p_log, OSM_LOG_INFO, p_ntc);

	/* Create a list that will hold all the infr records that should
	   be removed due to violation. o13-17.1.2 */
	cl_list_construct(&infr_to_remove_list);
	cl_list_init(&infr_to_remove_list, 5);
	context.p_remove_infr_list = &infr_to_remove_list;
	context.p_ntc = p_ntc;

	/* go over all inform info available at the subnet */
	/* try match to the given notice and send if match */
	cl_qlist_apply_func(&p_subn->sa_infr_list, match_notice_to_inf_rec,
			    &context);

	/* If we inserted items into the infr_to_remove_list - we need to
	   remove them */
	p_infr_rec = (osm_infr_t *) cl_list_remove_head(&infr_to_remove_list);
	while (p_infr_rec != NULL) {
		p_next_infr_rec =
		    (osm_infr_t *) cl_list_remove_head(&infr_to_remove_list);
		osm_infr_remove_from_db(p_subn, p_log, p_infr_rec);
		p_infr_rec = p_next_infr_rec;
	}
	cl_list_destroy(&infr_to_remove_list);

	/* report IB traps to plugin */
	osm_opensm_report_event(p_subn->p_osm, OSM_EVENT_ID_TRAP, p_ntc);

	OSM_LOG_EXIT(p_log);

	return IB_SUCCESS;
}
