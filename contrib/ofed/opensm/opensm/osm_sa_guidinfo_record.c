/*
 * Copyright (c) 2006-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2012 Mellanox Technologies LTD. All rights reserved.
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
 *    Implementation of osm_gir_rcv_t.
 * This object represents the GUIDInfoRecord Receiver object.
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
#define FILE_ID OSM_FILE_SA_GUIDINFO_RECORD_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_guid.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_pkey.h>
#include <opensm/osm_sa.h>

#define SA_GIR_RESP_SIZE SA_ITEM_RESP_SIZE(guid_rec)

#define MOD_GIR_COMP_MASK (IB_GIR_COMPMASK_LID | IB_GIR_COMPMASK_BLOCKNUM)

typedef struct osm_gir_item {
	cl_list_item_t list_item;
	ib_guidinfo_record_t rec;
} osm_gir_item_t;

typedef struct osm_gir_search_ctxt {
	const ib_guidinfo_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	cl_qlist_t *p_list;
	osm_sa_t *sa;
	const osm_physp_t *p_req_physp;
} osm_gir_search_ctxt_t;

static ib_api_status_t gir_rcv_new_gir(IN osm_sa_t * sa,
				       IN const osm_node_t * p_node,
				       IN cl_qlist_t * p_list,
				       IN ib_net64_t const match_port_guid,
				       IN ib_net16_t const match_lid,
				       IN const osm_physp_t * p_physp,
				       IN uint8_t const block_num)
{
	osm_sa_item_t *p_rec_item;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sa->p_log);

	p_rec_item = malloc(SA_GIR_RESP_SIZE);
	if (p_rec_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5102: "
			"rec_item alloc failed\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"New GUIDInfoRecord: lid %u, block num %d\n",
		cl_ntoh16(match_lid), block_num);

	memset(p_rec_item, 0, SA_GIR_RESP_SIZE);

	p_rec_item->resp.guid_rec.lid = match_lid;
	p_rec_item->resp.guid_rec.block_num = block_num;
	if (p_physp->p_guids)
		memcpy(&p_rec_item->resp.guid_rec.guid_info,
		       *p_physp->p_guids + block_num * GUID_TABLE_MAX_ENTRIES,
		       sizeof(ib_guid_info_t));
	else if (!block_num)
		p_rec_item->resp.guid_rec.guid_info.guid[0] = osm_physp_get_port_guid(p_physp);

	cl_qlist_insert_tail(p_list, &p_rec_item->list_item);

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return status;
}

static void sa_gir_create_gir(IN osm_sa_t * sa, IN osm_node_t * p_node,
			      IN cl_qlist_t * p_list,
			      IN ib_net64_t const match_port_guid,
			      IN ib_net16_t const match_lid,
			      IN const osm_physp_t * p_req_physp,
			      IN uint8_t const match_block_num)
{
	const osm_physp_t *p_physp;
	uint8_t port_num;
	uint8_t num_ports;
	uint16_t match_lid_ho;
	ib_net16_t base_lid_ho;
	ib_net16_t max_lid_ho;
	uint8_t lmc;
	ib_net64_t port_guid;
	uint8_t block_num, start_block_num, end_block_num, num_blocks;

	OSM_LOG_ENTER(sa->p_log);

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"Looking for GUIDRecord with LID: %u GUID:0x%016"
		PRIx64 "\n", cl_ntoh16(match_lid), cl_ntoh64(match_port_guid));

	/*
	   For switches, do not return the GUIDInfo record(s)
	   for each port on the switch, just for port 0.
	 */
	if (osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH)
		num_ports = 1;
	else
		num_ports = osm_node_get_num_physp(p_node);

	for (port_num = 0; port_num < num_ports; port_num++) {
		p_physp = osm_node_get_physp_ptr(p_node, port_num);
		if (!p_physp)
			continue;

		/* Check to see if the found p_physp and the requester physp
		   share a pkey. If not, continue */
		if (!osm_physp_share_pkey(sa->p_log, p_physp, p_req_physp,
					  sa->p_subn->opt.allow_both_pkeys))
			continue;

		port_guid = osm_physp_get_port_guid(p_physp);

		if (match_port_guid && (port_guid != match_port_guid))
			continue;

		/*
		   Note: the following check is a temporary workaround
		   Since 1. GUIDCap should never be 0 on ports where this applies
		   and   2. GUIDCap should not be used on ports where it doesn't apply
		   So this should really be a check for whether the port is a
		   switch external port or not!
		 */
		if (p_physp->port_info.guid_cap == 0)
			continue;

		num_blocks = p_physp->port_info.guid_cap / 8;
		if (p_physp->port_info.guid_cap % 8)
			num_blocks++;
		if (match_block_num == 255) {
			start_block_num = 0;
			end_block_num = num_blocks - 1;
		} else {
			if (match_block_num >= num_blocks)
				continue;
			end_block_num = start_block_num = match_block_num;
		}

		base_lid_ho = cl_ntoh16(osm_physp_get_base_lid(p_physp));
		match_lid_ho = cl_ntoh16(match_lid);
		if (match_lid_ho) {
			lmc = osm_physp_get_lmc(p_physp);
			max_lid_ho = (uint16_t) (base_lid_ho + (1 << lmc) - 1);

			/*
			   We validate that the lid belongs to this node.
			 */
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"Comparing LID: %u <= %u <= %u\n",
				base_lid_ho, match_lid_ho, max_lid_ho);

			if (match_lid_ho < base_lid_ho
			    || match_lid_ho > max_lid_ho)
				continue;
		}

		for (block_num = start_block_num; block_num <= end_block_num;
		     block_num++)
			gir_rcv_new_gir(sa, p_node, p_list, port_guid,
					cl_ntoh16(base_lid_ho), p_physp,
					block_num);
	}

	OSM_LOG_EXIT(sa->p_log);
}

static void sa_gir_by_comp_mask_cb(IN cl_map_item_t * p_map_item, IN void *cxt)
{
	const osm_gir_search_ctxt_t *p_ctxt = cxt;
	osm_node_t *const p_node = (osm_node_t *) p_map_item;
	const ib_guidinfo_record_t *const p_rcvd_rec = p_ctxt->p_rcvd_rec;
	const osm_physp_t *const p_req_physp = p_ctxt->p_req_physp;
	osm_sa_t *sa = p_ctxt->sa;
	const ib_guid_info_t *p_comp_gi;
	ib_net64_t const comp_mask = p_ctxt->comp_mask;
	ib_net64_t match_port_guid = 0;
	ib_net16_t match_lid = 0;
	uint8_t match_block_num = 255;

	OSM_LOG_ENTER(p_ctxt->sa->p_log);

	if (comp_mask & IB_GIR_COMPMASK_LID)
		match_lid = p_rcvd_rec->lid;

	if (comp_mask & IB_GIR_COMPMASK_BLOCKNUM)
		match_block_num = p_rcvd_rec->block_num;

	p_comp_gi = &p_rcvd_rec->guid_info;
	/* Different rule for block 0 v. other blocks */
	if (comp_mask & IB_GIR_COMPMASK_GID0) {
		if (!p_rcvd_rec->block_num)
			match_port_guid = osm_physp_get_port_guid(p_req_physp);
		if (p_comp_gi->guid[0] != match_port_guid)
			goto Exit;
	}

	if (comp_mask & IB_GIR_COMPMASK_GID1) {
		if (p_comp_gi->guid[1] != 0)
			goto Exit;
	}

	if (comp_mask & IB_GIR_COMPMASK_GID2) {
		if (p_comp_gi->guid[2] != 0)
			goto Exit;
	}

	if (comp_mask & IB_GIR_COMPMASK_GID3) {
		if (p_comp_gi->guid[3] != 0)
			goto Exit;
	}

	if (comp_mask & IB_GIR_COMPMASK_GID4) {
		if (p_comp_gi->guid[4] != 0)
			goto Exit;
	}

	if (comp_mask & IB_GIR_COMPMASK_GID5) {
		if (p_comp_gi->guid[5] != 0)
			goto Exit;
	}

	if (comp_mask & IB_GIR_COMPMASK_GID6) {
		if (p_comp_gi->guid[6] != 0)
			goto Exit;
	}

	if (comp_mask & IB_GIR_COMPMASK_GID7) {
		if (p_comp_gi->guid[7] != 0)
			goto Exit;
	}

	sa_gir_create_gir(sa, p_node, p_ctxt->p_list, match_port_guid,
			  match_lid, p_req_physp, match_block_num);

Exit:
	OSM_LOG_EXIT(p_ctxt->sa->p_log);
}

static inline boolean_t check_mod_comp_mask(ib_net64_t comp_mask)
{
	return ((comp_mask & MOD_GIR_COMP_MASK) == MOD_GIR_COMP_MASK);
}

static uint8_t coalesce_comp_mask(IN osm_madw_t *p_madw)
{
	uint8_t comp_mask = 0;
	ib_sa_mad_t *p_sa_mad;

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	if (p_sa_mad->comp_mask & IB_GIR_COMPMASK_GID0)
		comp_mask |= 1<<0;
	if (p_sa_mad->comp_mask & IB_GIR_COMPMASK_GID1)
		comp_mask |= 1<<1;
	if (p_sa_mad->comp_mask & IB_GIR_COMPMASK_GID2)
		comp_mask |= 1<<2;
	if (p_sa_mad->comp_mask & IB_GIR_COMPMASK_GID3)
		comp_mask |= 1<<3;
	if (p_sa_mad->comp_mask & IB_GIR_COMPMASK_GID4)
		comp_mask |= 1<<4;
	if (p_sa_mad->comp_mask & IB_GIR_COMPMASK_GID5)
		comp_mask |= 1<<5;
	if (p_sa_mad->comp_mask & IB_GIR_COMPMASK_GID6)
		comp_mask |= 1<<6;
	if (p_sa_mad->comp_mask & IB_GIR_COMPMASK_GID7)
		comp_mask |= 1<<7;
	return comp_mask;
}

static void guidinfo_respond(IN osm_sa_t *sa, IN osm_madw_t *p_madw,
			     IN ib_guidinfo_record_t * p_guidinfo_rec)
{
	cl_qlist_t rec_list;
	osm_sa_item_t *item;

	OSM_LOG_ENTER(sa->p_log);

	item = malloc(SA_GIR_RESP_SIZE);
	if (!item) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5101: "
			"rec_item alloc failed\n");
		goto Exit;
	}

	item->resp.guid_rec = *p_guidinfo_rec;

	cl_qlist_init(&rec_list);
	cl_qlist_insert_tail(&rec_list, &item->list_item);

	osm_sa_respond(sa, p_madw, sizeof(ib_guidinfo_record_t), &rec_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

static void gir_respond(IN osm_sa_t *sa, IN osm_madw_t *p_madw)
{
	ib_sa_mad_t *p_sa_mad;
	ib_guidinfo_record_t *p_rcvd_rec;
	ib_guidinfo_record_t guidinfo_rec;

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec = (ib_guidinfo_record_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);
	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG))
		osm_dump_guidinfo_record_v2(sa->p_log, p_rcvd_rec, FILE_ID, OSM_LOG_DEBUG);

	guidinfo_rec = *p_rcvd_rec;
	guidinfo_respond(sa, p_madw, &guidinfo_rec);
}

static ib_net64_t sm_assigned_guid(uint8_t assigned_byte)
{
	static uint32_t uniq_count;

	if (++uniq_count == 0) {
		uniq_count--;
		return 0;
	}
	return cl_hton64(((uint64_t) uniq_count) |
			 (((uint64_t) assigned_byte) << 32) |
			 (((uint64_t) OSM_VENDOR_ID_OPENIB) << 40));
}

static void del_guidinfo(IN osm_sa_t *sa, IN osm_madw_t *p_madw,
			 IN osm_port_t *p_port, IN uint8_t block_num)
{
	int i;
	uint32_t max_block;
	ib_sa_mad_t *p_sa_mad;
	ib_guidinfo_record_t *p_rcvd_rec;
	ib_net64_t del_alias_guid;
	osm_alias_guid_t *p_alias_guid;
	cl_list_item_t *p_list_item;
	osm_mcm_port_t *p_mcm_port;
	osm_mcm_alias_guid_t *p_mcm_alias_guid;
	uint8_t del_mask;
	int dirty = 0;

	if (!p_port->p_physp->p_guids)
		goto Exit;

	max_block = (p_port->p_physp->port_info.guid_cap + GUID_TABLE_MAX_ENTRIES - 1) /
		     GUID_TABLE_MAX_ENTRIES;

	if (block_num >= max_block) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5116: "
			"block_num %d is higher than Max GUID Cap block %d "
			"for port GUID 0x%" PRIx64 "\n",
			block_num, max_block, cl_ntoh64(p_port->p_physp->port_guid));
		CL_PLOCK_RELEASE(sa->p_lock);
		osm_sa_send_error(sa, p_madw,
				  IB_SA_MAD_STATUS_NO_RECORDS);
		return;
	}

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec =
		(ib_guidinfo_record_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	del_mask = coalesce_comp_mask(p_madw);

	for (i = block_num * GUID_TABLE_MAX_ENTRIES;
	     (block_num + 1) * GUID_TABLE_MAX_ENTRIES < p_port->p_physp->port_info.guid_cap ? i < (block_num + 1) * GUID_TABLE_MAX_ENTRIES : i < p_port->p_physp->port_info.guid_cap;
	     i++) {
		/* can't delete block 0 index 0 (base guid is RO) for alias guid table */
		if (i == 0 && p_sa_mad->comp_mask & IB_GIR_COMPMASK_GID0) {
			CL_PLOCK_RELEASE(sa->p_lock);
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"Not allowed to delete RO GID 0\n");
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
			return;
		}
		if (!(del_mask & 1<<(i % 8)))
			continue;

		del_alias_guid = (*p_port->p_physp->p_guids)[i];
		if (del_alias_guid) {
			/* Search all of port's multicast groups for alias */
			p_list_item = cl_qlist_head(&p_port->mcm_list);
			while (p_list_item != cl_qlist_end(&p_port->mcm_list)) {
				p_mcm_port = cl_item_obj(p_list_item,
							 p_mcm_port, list_item);
				p_list_item = cl_qlist_next(p_list_item);
				p_mcm_alias_guid = osm_mgrp_get_mcm_alias_guid(p_mcm_port->mgrp, del_alias_guid);
				if (p_mcm_alias_guid) {
					CL_PLOCK_RELEASE(sa->p_lock);
					osm_sa_send_error(sa, p_madw,
							  IB_SA_MAD_STATUS_DENIED);
					return;
				}
			}
		}
	}

	for (i = block_num * GUID_TABLE_MAX_ENTRIES;
	     (block_num + 1) * GUID_TABLE_MAX_ENTRIES < p_port->p_physp->port_info.guid_cap ? i < (block_num + 1) * GUID_TABLE_MAX_ENTRIES : i < p_port->p_physp->port_info.guid_cap;
	     i++) {
		if (!(del_mask & 1<<(i % 8)))
			continue;

		del_alias_guid = (*p_port->p_physp->p_guids)[i];
		if (del_alias_guid) {
			/* remove original from alias guid table */
			p_alias_guid = (osm_alias_guid_t *)
				cl_qmap_remove(&sa->p_subn->alias_port_guid_tbl,
					       del_alias_guid);
			if (p_alias_guid != (osm_alias_guid_t *)
						cl_qmap_end(&sa->p_subn->alias_port_guid_tbl))
				osm_alias_guid_delete(&p_alias_guid);
			else
				OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 510B: "
					"Original alias GUID 0x%" PRIx64
					" at index %u not found\n",
					cl_ntoh64(del_alias_guid), i);
			/* clear guid at index */
			(*p_port->p_physp->p_guids)[i] = 0;
			dirty = 1;
		}
	}

	if (dirty) {
		if (osm_queue_guidinfo(sa, p_port, block_num))
			osm_sm_signal(sa->sm, OSM_SIGNAL_GUID_PROCESS_REQUEST);
		sa->dirty = TRUE;
	}

	memcpy(&p_rcvd_rec->guid_info,
	       &((*p_port->p_physp->p_guids)[block_num * GUID_TABLE_MAX_ENTRIES]),
	       sizeof(ib_guid_info_t));

Exit:
	CL_PLOCK_RELEASE(sa->p_lock);
	gir_respond(sa, p_madw);
}

static void set_guidinfo(IN osm_sa_t *sa, IN osm_madw_t *p_madw,
			 IN osm_port_t *p_port, IN uint8_t block_num)
{
	uint32_t max_block;
	int i, j, dirty = 0;
	ib_sa_mad_t *p_sa_mad;
	ib_guidinfo_record_t *p_rcvd_rec;
	osm_assigned_guids_t *p_assigned_guids = 0;
	osm_alias_guid_t *p_alias_guid, *p_alias_guid_check;
	cl_map_item_t *p_item;
	ib_net64_t set_alias_guid, del_alias_guid, assigned_guid;
	uint8_t set_mask;

	max_block = (p_port->p_physp->port_info.guid_cap + GUID_TABLE_MAX_ENTRIES - 1) /
		     GUID_TABLE_MAX_ENTRIES;
	if (block_num >= max_block) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5118: "
			"block_num %d is higher than Max GUID Cap block %d "
			"for port GUID 0x%" PRIx64 "\n",
			block_num, max_block, cl_ntoh64(p_port->p_physp->port_guid));
		CL_PLOCK_RELEASE(sa->p_lock);
		osm_sa_send_error(sa, p_madw,
				  IB_SA_MAD_STATUS_NO_RECORDS);
		return;
	}
	if (!p_port->p_physp->p_guids) {
		p_port->p_physp->p_guids = calloc(max_block * GUID_TABLE_MAX_ENTRIES,
						  sizeof(ib_net64_t));
		if (!p_port->p_physp->p_guids) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5103: "
				"GUID table memory allocation failed for port "
				"GUID 0x%" PRIx64 "\n",
				cl_ntoh64(p_port->p_physp->port_guid));
			CL_PLOCK_RELEASE(sa->p_lock);
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_NO_RESOURCES);
			return;
		}
		/* setup base port guid in index 0 */
		(*p_port->p_physp->p_guids)[0] = p_port->p_physp->port_guid;
	}

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec = (ib_guidinfo_record_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Dump of incoming record\n");
		osm_dump_guidinfo_record_v2(sa->p_log, p_rcvd_rec, FILE_ID, OSM_LOG_DEBUG);
	}

	set_mask = coalesce_comp_mask(p_madw);

	for (i = block_num * GUID_TABLE_MAX_ENTRIES;
	     (block_num + 1) * GUID_TABLE_MAX_ENTRIES < p_port->p_physp->port_info.guid_cap ? i < (block_num + 1) * GUID_TABLE_MAX_ENTRIES : i < p_port->p_physp->port_info.guid_cap;
	     i++) {
		/* can't set block 0 index 0 (base guid is RO) for alias guid table */
		if (i == 0 && p_sa_mad->comp_mask & IB_GIR_COMPMASK_GID0) {
			CL_PLOCK_RELEASE(sa->p_lock);
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"Not allowed to set RO GID 0\n");
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
			return;
		}

		if (!(set_mask & 1<<(i % 8)))
			continue;

		set_alias_guid = p_rcvd_rec->guid_info.guid[i % 8];
		if (!set_alias_guid) {
			/* was a GUID previously assigned for this index ? */
			set_alias_guid = (*p_port->p_physp->p_guids)[i];
			if (set_alias_guid) {
				p_rcvd_rec->guid_info.guid[i % 8] = set_alias_guid;
				continue;
			}
			/* Is there a persistent SA assigned guid for this index ? */
			if (!p_assigned_guids)
				p_assigned_guids =
				    osm_get_assigned_guids_by_guid(sa->p_subn,
								   p_port->p_physp->port_guid);
			if (p_assigned_guids) {
				set_alias_guid = p_assigned_guids->assigned_guid[i];
				if (set_alias_guid) {
					p_rcvd_rec->guid_info.guid[i % 8] = set_alias_guid;
					p_item = cl_qmap_get(&sa->sm->p_subn->alias_port_guid_tbl,
							     set_alias_guid);
					if (p_item == cl_qmap_end(&sa->sm->p_subn->alias_port_guid_tbl))
						goto add_alias_guid;
					else {
						p_alias_guid = (osm_alias_guid_t *) p_item;
						if (p_alias_guid->p_base_port != p_port) {
							OSM_LOG(sa->p_log,
								OSM_LOG_ERROR,
								"ERR 5110: "
								" Assigned alias port GUID 0x%" PRIx64
								" index %d base port GUID 0x%" PRIx64
								" now attempted on port GUID 0x%" PRIx64
								"\n",
								cl_ntoh64(p_alias_guid->alias_guid), i,
								cl_ntoh64(p_alias_guid->p_base_port->guid),
								cl_ntoh64(p_port->guid));
							/* clear response guid at index to indicate duplicate */
							p_rcvd_rec->guid_info.guid[i % 8] = 0;
						}
						continue;
					}
				}
			}
		}
		if (!set_alias_guid) {
			for (j = 0; j < 1000; j++) {
				assigned_guid = sm_assigned_guid(sa->p_subn->opt.sm_assigned_guid);
				if (!assigned_guid) {
					CL_PLOCK_RELEASE(sa->p_lock);
					OSM_LOG(sa->p_log, OSM_LOG_ERROR,
						"ERR 510E: No more assigned guids available\n");
					osm_sa_send_error(sa, p_madw,
							  IB_SA_MAD_STATUS_NO_RESOURCES);
					return;
				}
				p_item = cl_qmap_get(&sa->sm->p_subn->alias_port_guid_tbl,
						     assigned_guid);
				if (p_item == cl_qmap_end(&sa->sm->p_subn->alias_port_guid_tbl)) {
					set_alias_guid = assigned_guid;
					p_rcvd_rec->guid_info.guid[i % 8] = assigned_guid;
					if (!p_assigned_guids) {
						p_assigned_guids = osm_assigned_guids_new(p_port->p_physp->port_guid,
											  max_block * GUID_TABLE_MAX_ENTRIES);
						if (p_assigned_guids) {
							cl_qmap_insert(&(sa->p_subn->assigned_guids_tbl),
								       p_assigned_guids->port_guid,
								       &p_assigned_guids->map_item);
						} else {
							OSM_LOG(sa->p_log,
								OSM_LOG_ERROR,
								"ERR 510D: osm_assigned_guids_new failed port GUID 0x%" PRIx64 " index %d\n",
								cl_ntoh64(p_port->p_physp->port_guid), i);
							CL_PLOCK_RELEASE(sa->p_lock);
							osm_sa_send_error(sa, p_madw,
									  IB_SA_MAD_STATUS_NO_RESOURCES);
							return;
						}
					}
					if (p_assigned_guids)
						p_assigned_guids->assigned_guid[i] = assigned_guid;
					break;
				}
			}
			if (!set_alias_guid) {
				OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 510A: "
					"SA assigned GUID %d failed for "
					"port GUID 0x%" PRIx64 "\n", i,
					cl_ntoh64(p_port->p_physp->port_guid));
				continue;
			}
		}

add_alias_guid:
		/* allocate alias guid and add to alias guid table */
		p_alias_guid = osm_alias_guid_new(set_alias_guid, p_port);
		if (!p_alias_guid) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5107: "
				"Alias guid %d memory allocation failed"
				" for port GUID 0x%" PRIx64 "\n",
				i, cl_ntoh64(p_port->p_physp->port_guid));
			CL_PLOCK_RELEASE(sa->p_lock);
			return;
		}

		p_alias_guid_check =
			(osm_alias_guid_t *) cl_qmap_insert(&sa->sm->p_subn->alias_port_guid_tbl,
							    p_alias_guid->alias_guid,
							    &p_alias_guid->map_item);
		if (p_alias_guid_check != p_alias_guid) {
			/* alias GUID is a duplicate if it exists on another port or on the same port but at another index */
			if (p_alias_guid_check->p_base_port != p_port ||
			    (*p_port->p_physp->p_guids)[i] != set_alias_guid) {
				OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5108: "
					"Duplicate alias port GUID 0x%" PRIx64
					" index %d base port GUID 0x%" PRIx64
					", alias GUID already assigned to "
					"base port GUID 0x%" PRIx64 "\n",
					cl_ntoh64(p_alias_guid->alias_guid), i,
					cl_ntoh64(p_alias_guid->p_base_port->guid),
					cl_ntoh64(p_alias_guid_check->p_base_port->guid));
				/* clear response guid at index to indicate duplicate */
				p_rcvd_rec->guid_info.guid[i % 8] = 0;
			}
			osm_alias_guid_delete(&p_alias_guid);
		} else {
			del_alias_guid = (*p_port->p_physp->p_guids)[i];
			if (del_alias_guid) {
				/* remove original from alias guid table */
				p_alias_guid_check = (osm_alias_guid_t *)
					cl_qmap_remove(&sa->p_subn->alias_port_guid_tbl,
						       del_alias_guid);
				if (p_alias_guid_check)
					osm_alias_guid_delete(&p_alias_guid_check);
				else
					OSM_LOG(sa->p_log, OSM_LOG_ERROR,
						"ERR 510C: Original alias GUID "
						"0x%" PRIx64 "at index %u "
						"not found\n",
						cl_ntoh64(del_alias_guid),
						i);
			}

			/* insert or replace guid at index */
			(*p_port->p_physp->p_guids)[i] = set_alias_guid;
			dirty = 1;
		}
	}

	if (dirty) {
		if (osm_queue_guidinfo(sa, p_port, block_num))
			osm_sm_signal(sa->sm, OSM_SIGNAL_GUID_PROCESS_REQUEST);
		sa->dirty = TRUE;
	}

	memcpy(&p_rcvd_rec->guid_info,
	       &((*p_port->p_physp->p_guids)[block_num * GUID_TABLE_MAX_ENTRIES]),
	       sizeof(ib_guid_info_t));

	CL_PLOCK_RELEASE(sa->p_lock);
	gir_respond(sa, p_madw);
}

static void get_guidinfo(IN osm_sa_t *sa, IN osm_madw_t *p_madw,
			 IN osm_physp_t *p_req_physp)
{
	const ib_sa_mad_t *p_rcvd_mad;
	const ib_guidinfo_record_t *p_rcvd_rec;
	cl_qlist_t rec_list;
	osm_gir_search_ctxt_t context;

	p_rcvd_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec =
	    (ib_guidinfo_record_t *) ib_sa_mad_get_payload_ptr(p_rcvd_mad);

	cl_qlist_init(&rec_list);

	context.p_rcvd_rec = p_rcvd_rec;
	context.p_list = &rec_list;
	context.comp_mask = p_rcvd_mad->comp_mask;
	context.sa = sa;
	context.p_req_physp = p_req_physp;


	cl_qmap_apply_func(&sa->p_subn->node_guid_tbl, sa_gir_by_comp_mask_cb,
			   &context);

	CL_PLOCK_RELEASE(sa->p_lock);

	osm_sa_respond(sa, p_madw, sizeof(ib_guidinfo_record_t), &rec_list);
}

void osm_gir_rcv_process(IN void *ctx, IN void *data)
{
	osm_sa_t *sa = ctx;
	osm_madw_t *p_madw = data;
	const ib_sa_mad_t *p_rcvd_mad;
	osm_physp_t *p_req_physp;
	osm_port_t *p_port;
	const ib_guidinfo_record_t *p_rcvd_rec;

	CL_ASSERT(sa);

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_rcvd_mad = osm_madw_get_sa_mad_ptr(p_madw);

	CL_ASSERT(p_rcvd_mad->attr_id == IB_MAD_ATTR_GUIDINFO_RECORD);

	switch(p_rcvd_mad->method) {
	case IB_MAD_METHOD_GET:
	case IB_MAD_METHOD_GETTABLE:
		/* update the requester physical port */
		CL_PLOCK_ACQUIRE(sa->p_lock);
		p_req_physp = osm_get_physp_by_mad_addr(sa->p_log, sa->p_subn,
							osm_madw_get_mad_addr_ptr(p_madw));
		if (p_req_physp == NULL) {
			CL_PLOCK_RELEASE(sa->p_lock);
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5104: "
				"Cannot find requester physical port\n");
			goto Exit;
		}
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Requester port GUID 0x%" PRIx64 "\n",
			cl_ntoh64(osm_physp_get_port_guid(p_req_physp)));

		get_guidinfo(sa, p_madw, p_req_physp);
		goto Exit;
	case IB_MAD_METHOD_SET:
	case IB_MAD_METHOD_DELETE:
		if (!check_mod_comp_mask(p_rcvd_mad->comp_mask)) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5106: "
				"component mask = 0x%016" PRIx64 ", "
				"expected comp mask = 0x%016" PRIx64 "\n",
				cl_ntoh64(p_rcvd_mad->comp_mask),
				CL_NTOH64(MOD_GIR_COMP_MASK));
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_INSUF_COMPS);
			goto Exit;
		}
		p_rcvd_rec = (ib_guidinfo_record_t *) ib_sa_mad_get_payload_ptr(p_rcvd_mad);

		/* update the requester physical port */
		CL_PLOCK_EXCL_ACQUIRE(sa->p_lock);
		p_req_physp = osm_get_physp_by_mad_addr(sa->p_log, sa->p_subn,
							osm_madw_get_mad_addr_ptr(p_madw));
		if (p_req_physp == NULL) {
			CL_PLOCK_RELEASE(sa->p_lock);
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5104: "
				"Cannot find requester physical port\n");
			goto Exit;
		}
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Requester port GUID 0x%" PRIx64 "\n",
			cl_ntoh64(osm_physp_get_port_guid(p_req_physp)));

		p_port = osm_get_port_by_lid(sa->p_subn, p_rcvd_rec->lid);
		if (!p_port) {
			CL_PLOCK_RELEASE(sa->p_lock);
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5117: "
				"Port with LID %u not found\n",
				cl_ntoh16(p_rcvd_rec->lid));
				osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_NO_RECORDS);
			goto Exit;
		}
		if (!osm_physp_share_pkey(sa->p_log, p_req_physp, p_port->p_physp,
					  sa->p_subn->opt.allow_both_pkeys)) {
			CL_PLOCK_RELEASE(sa->p_lock);
			goto Exit;
		}

		if (p_rcvd_mad->method == IB_MAD_METHOD_SET)
			set_guidinfo(sa, p_madw, p_port, p_rcvd_rec->block_num);
		else
			del_guidinfo(sa, p_madw, p_port, p_rcvd_rec->block_num);
		break;
	default:
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5105: "
			"Unsupported Method (%s) for GUIDInfoRecord request\n",
			ib_get_sa_method_str(p_rcvd_mad->method));
		osm_sa_send_error(sa, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		break;
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
}
