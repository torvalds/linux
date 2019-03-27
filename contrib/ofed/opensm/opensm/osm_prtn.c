/*
 * Copyright (c) 2006-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2012 Mellanox Technologies LTD. All rights reserved.
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
 *    Implementation of osm_prtn_t.
 * This object represents an IBA partition.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <complib/cl_debug.h>
#include <iba/ib_types.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_PRTN_C
#include <opensm/osm_opensm.h>
#include <opensm/osm_partition.h>
#include <opensm/osm_node.h>
#include <opensm/osm_sa.h>
#include <opensm/osm_multicast.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>

extern int osm_prtn_config_parse_file(osm_log_t * p_log, osm_subn_t * p_subn,
				      const char *file_name);

static uint16_t global_pkey_counter;

osm_prtn_t *osm_prtn_new(IN const char *name, IN uint16_t pkey)
{
	osm_prtn_t *p = malloc(sizeof(*p));
	if (!p)
		return NULL;

	memset(p, 0, sizeof(*p));
	p->pkey = pkey;
	p->sl = OSM_DEFAULT_SL;
	p->mgrps = NULL;
	p->nmgrps = 0;
	cl_map_construct(&p->full_guid_tbl);
	cl_map_init(&p->full_guid_tbl, 32);
	cl_map_construct(&p->part_guid_tbl);
	cl_map_init(&p->part_guid_tbl, 32);

	if (name && *name)
		strncpy(p->name, name, sizeof(p->name));
	else
		snprintf(p->name, sizeof(p->name), "%04x", cl_ntoh16(pkey));

	return p;
}

void osm_prtn_delete(IN osm_subn_t * p_subn, IN OUT osm_prtn_t ** pp_prtn)
{
	char gid_str[INET6_ADDRSTRLEN];
	int i = 0;
	osm_prtn_t *p = *pp_prtn;

	cl_map_remove_all(&p->full_guid_tbl);
	cl_map_destroy(&p->full_guid_tbl);
	cl_map_remove_all(&p->part_guid_tbl);
	cl_map_destroy(&p->part_guid_tbl);

	if (p->mgrps) {
		/* Clean up mgrps */
		for (i = 0; i < p->nmgrps; i++) {
			/* osm_mgrp_cleanup will not delete
			 * "well_known" groups */
			p->mgrps[i]->well_known = FALSE;
			OSM_LOG(&p_subn->p_osm->log, OSM_LOG_DEBUG,
				"removing mgroup %s from partition (0x%x)\n",
				inet_ntop(AF_INET6,
					  p->mgrps[i]->mcmember_rec.mgid.raw,
					  gid_str, sizeof gid_str),
				cl_hton16(p->pkey));
			osm_mgrp_cleanup(p_subn, p->mgrps[i]);
		}

		free(p->mgrps);
	}

	free(p);
	*pp_prtn = NULL;
}

ib_api_status_t osm_prtn_add_port(osm_log_t * p_log, osm_subn_t * p_subn,
				  osm_prtn_t * p, ib_net64_t guid,
				  boolean_t full, boolean_t indx0)
{
	ib_api_status_t status = IB_SUCCESS;
	cl_map_t *p_tbl;
	osm_port_t *p_port;
	osm_physp_t *p_physp;

	p_port = osm_get_port_by_guid(p_subn, guid);
	if (!p_port) {
		OSM_LOG(p_log, OSM_LOG_VERBOSE,
			"port 0x%" PRIx64 " not found\n", cl_ntoh64(guid));
		return status;
	}

	p_physp = p_port->p_physp;
	if (!p_physp) {
		OSM_LOG(p_log, OSM_LOG_VERBOSE,
			"no physical for port 0x%" PRIx64 "\n",
			cl_ntoh64(guid));
		return status;
	}
	/* Set the pkey to be inserted to block 0 index 0 */
	if (indx0) {
		OSM_LOG(p_log, OSM_LOG_VERBOSE, "Setting pkey 0x%04x at indx0 "
			"for port 0x%" PRIx64 "\n",
			cl_ntoh16(p->pkey), cl_ntoh64(guid));
		osm_pkey_tbl_set_indx0_pkey(p_log, p->pkey, full,
					    &p_physp->pkeys);
	} else if (ib_pkey_get_base(p_physp->pkeys.indx0_pkey) ==
		   ib_pkey_get_base(p->pkey))
		p_physp->pkeys.indx0_pkey = 0;

	p_tbl = (full == TRUE) ? &p->full_guid_tbl : &p->part_guid_tbl;

	if (p_subn->opt.allow_both_pkeys) {
		if (cl_map_remove(p_tbl, guid))
			OSM_LOG(p_log, OSM_LOG_VERBOSE, "port 0x%" PRIx64
				" already in partition \'%s\' (0x%04x) full %d."
				" Will overwrite\n",
				cl_ntoh64(guid), p->name, cl_ntoh16(p->pkey),
				full);
	} else {
		if (cl_map_remove(&p->part_guid_tbl, guid) ||
		    cl_map_remove(&p->full_guid_tbl, guid))
			OSM_LOG(p_log, OSM_LOG_VERBOSE, "port 0x%" PRIx64
				" already in partition \'%s\' (0x%04x)."
				" Will overwrite\n",
				cl_ntoh64(guid), p->name, cl_ntoh16(p->pkey));
	}

	if (cl_map_insert(p_tbl, guid, p_physp) == NULL)
		return IB_INSUFFICIENT_MEMORY;

	return status;
}

ib_api_status_t osm_prtn_add_all(osm_log_t * p_log, osm_subn_t * p_subn,
				 osm_prtn_t * p, unsigned type,
				 boolean_t full, boolean_t indx0)
{
	cl_qmap_t *p_port_tbl = &p_subn->port_guid_tbl;
	cl_map_item_t *p_item;
	osm_port_t *p_port;
	ib_api_status_t status = IB_SUCCESS;

	p_item = cl_qmap_head(p_port_tbl);
	while (p_item != cl_qmap_end(p_port_tbl)) {
		p_port = (osm_port_t *) p_item;
		p_item = cl_qmap_next(p_item);
		if (!type || osm_node_get_type(p_port->p_node) == type) {
			status = osm_prtn_add_port(p_log, p_subn, p,
						   osm_port_get_guid(p_port),
						   full, indx0);
			if (status != IB_SUCCESS)
				goto _err;
		}
	}

_err:
	return status;
}

static ib_api_status_t
track_mgrp_w_partition(osm_log_t *p_log, osm_prtn_t *p, osm_mgrp_t *mgrp,
			osm_subn_t *p_subn, const ib_gid_t *mgid,
			ib_net16_t pkey)
{
	char gid_str[INET6_ADDRSTRLEN];
	osm_mgrp_t **tmp;
	int i = 0;

	/* check if we are already tracking this group */
	for (i = 0; i < p->nmgrps; i++)
		if (p->mgrps[i] == mgrp)
			return (IB_SUCCESS);

	/* otherwise add it to our list */
	tmp = realloc(p->mgrps, (p->nmgrps +1) * sizeof(*p->mgrps));
	if (tmp) {
		p->mgrps = tmp;
		p->mgrps[p->nmgrps] = mgrp;
		p->nmgrps++;
	} else {
		OSM_LOG(p_log, OSM_LOG_ERROR,
			"realloc error to create MC group (%s) in "
			"partition (pkey 0x%04x)\n",
			inet_ntop(AF_INET6, mgid->raw,
				  gid_str, sizeof gid_str),
			cl_ntoh16(pkey));
		mgrp->well_known = FALSE;
		osm_mgrp_cleanup(p_subn, mgrp);
		return (IB_ERROR);
	}
	mgrp->well_known = TRUE;
	return (IB_SUCCESS);
}

ib_api_status_t osm_prtn_add_mcgroup(osm_log_t * p_log, osm_subn_t * p_subn,
				     osm_prtn_t * p, uint8_t rate, uint8_t mtu,
				     uint8_t sl, uint8_t scope, uint32_t Q_Key,
				     uint8_t tclass, uint32_t FlowLabel,
				     const ib_gid_t *mgid)
{
	char gid_str[INET6_ADDRSTRLEN];
	ib_member_rec_t mc_rec;
	ib_net64_t comp_mask;
	ib_net16_t pkey;
	osm_mgrp_t *mgrp;
	osm_sa_t *p_sa = &p_subn->p_osm->sa;
	uint8_t hop_limit;

	pkey = p->pkey | cl_hton16(0x8000);
	if (!scope)
		scope = OSM_DEFAULT_MGRP_SCOPE;
	hop_limit = (scope == IB_MC_SCOPE_LINK_LOCAL) ? 0 : IB_HOPLIMIT_MAX;

	memset(&mc_rec, 0, sizeof(mc_rec));

	mc_rec.mgid = *mgid;

	mc_rec.qkey = CL_HTON32(Q_Key);
	mc_rec.mtu = mtu | (IB_PATH_SELECTOR_EXACTLY << 6);
	mc_rec.tclass = tclass;
	mc_rec.pkey = pkey;
	mc_rec.rate = rate | (IB_PATH_SELECTOR_EXACTLY << 6);
	mc_rec.pkt_life = p_subn->opt.subnet_timeout;
	mc_rec.sl_flow_hop = ib_member_set_sl_flow_hop(sl, FlowLabel, hop_limit);
	/* Scope in MCMemberRecord (if present) needs to be consistent with MGID */
	mc_rec.scope_state =
	    ib_member_set_scope_state(scope, IB_MC_REC_STATE_FULL_MEMBER);
	ib_mgid_set_scope(&mc_rec.mgid, scope);

	/* don't update rate, mtu */
	comp_mask = IB_MCR_COMPMASK_MTU | IB_MCR_COMPMASK_MTU_SEL |
	    IB_MCR_COMPMASK_RATE | IB_MCR_COMPMASK_RATE_SEL;
	mgrp = osm_mcmr_rcv_find_or_create_new_mgrp(p_sa, comp_mask, &mc_rec);
	if (!mgrp) {
		OSM_LOG(p_log, OSM_LOG_ERROR,
			"Failed to create MC group (%s) with pkey 0x%04x\n",
			inet_ntop(AF_INET6, mgid->raw, gid_str, sizeof gid_str),
			cl_ntoh16(pkey));
		return IB_ERROR;
	}

	return (track_mgrp_w_partition(p_log, p, mgrp, p_subn, mgid, pkey));
}

static uint16_t generate_pkey(osm_subn_t * p_subn)
{
	uint16_t pkey;

	cl_qmap_t *m = &p_subn->prtn_pkey_tbl;
	while (global_pkey_counter < cl_ntoh16(IB_DEFAULT_PARTIAL_PKEY) - 1) {
		pkey = ++global_pkey_counter;
		pkey = cl_hton16(pkey);
		if (cl_qmap_get(m, pkey) == cl_qmap_end(m))
			return pkey;
	}
	return 0;
}

osm_prtn_t *osm_prtn_find_by_name(osm_subn_t * p_subn, const char *name)
{
	cl_map_item_t *p_next;
	osm_prtn_t *p;

	p_next = cl_qmap_head(&p_subn->prtn_pkey_tbl);
	while (p_next != cl_qmap_end(&p_subn->prtn_pkey_tbl)) {
		p = (osm_prtn_t *) p_next;
		p_next = cl_qmap_next(&p->map_item);
		if (!strncmp(p->name, name, sizeof(p->name)))
			return p;
	}

	return NULL;
}

osm_prtn_t *osm_prtn_make_new(osm_log_t * p_log, osm_subn_t * p_subn,
			      const char *name, uint16_t pkey)
{
	osm_prtn_t *p = NULL, *p_check;

	pkey &= cl_hton16((uint16_t) ~ 0x8000);
	if (!pkey) {
		if (name && (p = osm_prtn_find_by_name(p_subn, name)))
			return p;
		if (!(pkey = generate_pkey(p_subn)))
			return NULL;
	}

	p = osm_prtn_new(name, pkey);
	if (!p) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "Unable to create"
			" partition \'%s\' (0x%04x)\n", name, cl_ntoh16(pkey));
		return NULL;
	}

	p_check = (osm_prtn_t *) cl_qmap_insert(&p_subn->prtn_pkey_tbl,
						p->pkey, &p->map_item);
	if (p != p_check) {
		OSM_LOG(p_log, OSM_LOG_VERBOSE, "Duplicated partition"
			" definition: \'%s\' (0x%04x) prev name \'%s\'"
			".  Will use it\n",
			name, cl_ntoh16(pkey), p_check->name);
		osm_prtn_delete(p_subn, &p);
		p = p_check;
	}

	return p;
}

static ib_api_status_t prtn_make_default(osm_log_t * p_log, osm_subn_t * p_subn,
					 boolean_t no_config)
{
	ib_api_status_t status = IB_UNKNOWN_ERROR;
	osm_prtn_t *p;

	p = osm_prtn_make_new(p_log, p_subn, "Default",
			      IB_DEFAULT_PARTIAL_PKEY);
	if (!p)
		goto _err;
	status = osm_prtn_add_all(p_log, p_subn, p, 0, no_config, FALSE);
	if (status != IB_SUCCESS)
		goto _err;
	cl_map_remove(&p->part_guid_tbl, p_subn->sm_port_guid);
	status =
	    osm_prtn_add_port(p_log, p_subn, p, p_subn->sm_port_guid, TRUE, FALSE);

	/* ipv4 broadcast group */
	if (no_config)
		osm_prtn_add_mcgroup(p_log, p_subn, p, OSM_DEFAULT_MGRP_RATE,
				     OSM_DEFAULT_MGRP_MTU, OSM_DEFAULT_SL,
				     0, OSM_IPOIB_BROADCAST_MGRP_QKEY, 0, 0,
				     &osm_ipoib_broadcast_mgid);

_err:
	return status;
}

ib_api_status_t osm_prtn_make_partitions(osm_log_t * p_log, osm_subn_t * p_subn)
{
	struct stat statbuf;
	const char *file_name;
	boolean_t is_config = TRUE;
	boolean_t is_wrong_config = FALSE;
	ib_api_status_t status = IB_SUCCESS;
	cl_map_item_t *p_next;
	osm_prtn_t *p;

	file_name = p_subn->opt.partition_config_file ?
	    p_subn->opt.partition_config_file : OSM_DEFAULT_PARTITION_CONFIG_FILE;
	if (stat(file_name, &statbuf)) {
		OSM_LOG(p_log, OSM_LOG_VERBOSE, "Partition configuration "
			"%s is not accessible (%s)\n", file_name,
			strerror(errno));
		is_config = FALSE;
	}

retry_default:
	/* clean up current port maps */
	p_next = cl_qmap_head(&p_subn->prtn_pkey_tbl);
	while (p_next != cl_qmap_end(&p_subn->prtn_pkey_tbl)) {
		p = (osm_prtn_t *) p_next;
		p_next = cl_qmap_next(&p->map_item);
		cl_map_remove_all(&p->part_guid_tbl);
		cl_map_remove_all(&p->full_guid_tbl);
	}

	global_pkey_counter = 0;

	status = prtn_make_default(p_log, p_subn, !is_config);
	if (status != IB_SUCCESS)
		goto _err;

	if (is_config && osm_prtn_config_parse_file(p_log, p_subn, file_name)) {
		OSM_LOG(p_log, OSM_LOG_VERBOSE, "Partition configuration "
			"was not fully processed\n");
		is_wrong_config = TRUE;
	}

	/* and now clean up empty partitions */
	p_next = cl_qmap_head(&p_subn->prtn_pkey_tbl);
	while (p_next != cl_qmap_end(&p_subn->prtn_pkey_tbl)) {
		p = (osm_prtn_t *) p_next;
		p_next = cl_qmap_next(&p->map_item);
		if (cl_map_count(&p->part_guid_tbl) == 0 &&
		    cl_map_count(&p->full_guid_tbl) == 0) {
			cl_qmap_remove_item(&p_subn->prtn_pkey_tbl,
					    (cl_map_item_t *) p);
			osm_prtn_delete(p_subn, &p);
		}
	}

	if (is_config && is_wrong_config) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "Partition configuration "
			"in error; retrying with default config\n");
		is_config = FALSE;
		goto retry_default;
	}

_err:
	return status;
}
