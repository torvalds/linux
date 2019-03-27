/*
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2007,2009 Mellanox Technologies LTD. All rights reserved.
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
 *      Implementation of Up Down Algorithm using ranking & Min Hop
 *      Calculation functions
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <ctype.h>
#include <complib/cl_debug.h>
#include <complib/cl_qmap.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_UCAST_UPDN_C
#include <opensm/osm_switch.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_ucast_mgr.h>

/* //////////////////////////// */
/*  Local types                 */
/* //////////////////////////// */

/* direction */
typedef enum updn_switch_dir {
	UP = 0,
	DOWN
} updn_switch_dir_t;

/* updn structure */
typedef struct updn {
	unsigned num_roots;
	osm_opensm_t *p_osm;
} updn_t;

struct updn_node {
	cl_list_item_t list;
	osm_switch_t *sw;
	uint64_t id;
	updn_switch_dir_t dir;
	unsigned rank;
	unsigned visited;
};

/* This function returns direction based on rank and guid info of current &
   remote ports */
static updn_switch_dir_t updn_get_dir(unsigned cur_rank, unsigned rem_rank,
				      uint64_t cur_id, uint64_t rem_id)
{
	/* HACK: comes to solve root nodes connection, in a classic subnet root nodes do not connect
	   directly, but in case they are we assign to root node an UP direction to allow UPDN to discover
	   the subnet correctly (and not from the point of view of the last root node).
	 */
	if (!cur_rank && !rem_rank)
		return UP;

	if (cur_rank < rem_rank)
		return DOWN;
	else if (cur_rank > rem_rank)
		return UP;
	else {
		/* Equal rank, decide by id number, bigger == UP direction */
		if (cur_id > rem_id)
			return UP;
		else
			return DOWN;
	}
}

/**********************************************************************
 * This function does the bfs of min hop table calculation by guid index
 * as a starting point.
 **********************************************************************/
static int updn_bfs_by_node(IN osm_log_t * p_log, IN osm_subn_t * p_subn,
			    IN osm_switch_t * p_sw)
{
	uint8_t pn, pn_rem;
	cl_qlist_t list;
	uint16_t lid;
	struct updn_node *u;
	updn_switch_dir_t next_dir, current_dir;

	OSM_LOG_ENTER(p_log);

	lid = osm_node_get_base_lid(p_sw->p_node, 0);
	lid = cl_ntoh16(lid);
	osm_switch_set_hops(p_sw, lid, 0, 0);

	OSM_LOG(p_log, OSM_LOG_DEBUG,
		"Starting from switch - port GUID 0x%" PRIx64 " lid %u\n",
		cl_ntoh64(p_sw->p_node->node_info.port_guid), lid);

	u = p_sw->priv;
	u->dir = UP;

	/* Update list with the new element */
	cl_qlist_init(&list);
	cl_qlist_insert_tail(&list, &u->list);

	/* BFS the list till no next element */
	while (!cl_is_qlist_empty(&list)) {
		u = (struct updn_node *)cl_qlist_remove_head(&list);
		u->visited = 0;	/* cleanup */
		current_dir = u->dir;
		/* Go over all ports of the switch and find unvisited remote nodes */
		for (pn = 1; pn < u->sw->num_ports; pn++) {
			osm_node_t *p_remote_node;
			struct updn_node *rem_u;
			uint8_t current_min_hop, remote_min_hop,
			    set_hop_return_value;
			osm_switch_t *p_remote_sw;

			p_remote_node =
			    osm_node_get_remote_node(u->sw->p_node, pn,
						     &pn_rem);
			/* If no remote node OR remote node is not a SWITCH
			   continue to next pn */
			if (!p_remote_node || !p_remote_node->sw)
				continue;
			/* Fetch remote guid only after validation of remote node */
			p_remote_sw = p_remote_node->sw;
			rem_u = p_remote_sw->priv;
			/* Decide which direction to mark it (UP/DOWN) */
			next_dir = updn_get_dir(u->rank, rem_u->rank,
						u->id, rem_u->id);

			/* Check if this is a legal step : the only illegal step is going
			   from DOWN to UP */
			if ((current_dir == DOWN) && (next_dir == UP)) {
				OSM_LOG(p_log, OSM_LOG_DEBUG,
					"Avoiding move from 0x%016" PRIx64
					" to 0x%016" PRIx64 "\n",
					cl_ntoh64(osm_node_get_node_guid(u->sw->p_node)),
					cl_ntoh64(osm_node_get_node_guid(p_remote_node)));
				/* Illegal step */
				continue;
			}
			/* Set MinHop value for the current lid */
			current_min_hop = osm_switch_get_least_hops(u->sw, lid);
			/* Check hop count if better insert into list && update
			   the remote node Min Hop Table */
			remote_min_hop =
			    osm_switch_get_hop_count(p_remote_sw, lid, pn_rem);
			if (current_min_hop + 1 < remote_min_hop) {
				set_hop_return_value =
				    osm_switch_set_hops(p_remote_sw, lid,
							pn_rem,
							current_min_hop + 1);
				if (set_hop_return_value) {
					OSM_LOG(p_log, OSM_LOG_ERROR, "ERR AA01: "
						"Invalid value returned from set min hop is: %d\n",
						set_hop_return_value);
				}
				/* Check if remote port has already been visited */
				if (!rem_u->visited) {
					/* Insert updn_switch item into the list */
					rem_u->dir = next_dir;
					rem_u->visited = 1;
					cl_qlist_insert_tail(&list,
							     &rem_u->list);
				}
			}
		}
	}

	OSM_LOG_EXIT(p_log);
	return 0;
}

/* NOTE : PLS check if we need to decide that the first */
/*        rank is a SWITCH for BFS purpose */
static int updn_subn_rank(IN updn_t * p_updn)
{
	osm_switch_t *p_sw;
	osm_physp_t *p_physp, *p_remote_physp;
	cl_qlist_t list;
	cl_map_item_t *item;
	struct updn_node *u, *remote_u;
	uint8_t num_ports, port_num;
	osm_log_t *p_log = &p_updn->p_osm->log;
	unsigned max_rank = 0;

	OSM_LOG_ENTER(p_log);
	cl_qlist_init(&list);

	/* add all roots to the list */
	for (item = cl_qmap_head(&p_updn->p_osm->subn.sw_guid_tbl);
	     item != cl_qmap_end(&p_updn->p_osm->subn.sw_guid_tbl);
	     item = cl_qmap_next(item)) {
		p_sw = (osm_switch_t *)item;
		u = p_sw->priv;
		if (!u->rank)
			cl_qlist_insert_tail(&list, &u->list);
	}

	/* BFS the list till it's empty */
	while (!cl_is_qlist_empty(&list)) {
		u = (struct updn_node *)cl_qlist_remove_head(&list);
		/* Go over all remote nodes and rank them (if not already visited) */
		p_sw = u->sw;
		num_ports = p_sw->num_ports;
		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"Handling switch GUID 0x%" PRIx64 "\n",
			cl_ntoh64(osm_node_get_node_guid(p_sw->p_node)));
		for (port_num = 1; port_num < num_ports; port_num++) {
			ib_net64_t port_guid;

			/* Current port fetched in order to get remote side */
			p_physp =
			    osm_node_get_physp_ptr(p_sw->p_node, port_num);

			if (!p_physp)
				continue;

			p_remote_physp = p_physp->p_remote_physp;

			/*
			   make sure that all the following occur on p_remote_physp:
			   1. The port isn't NULL
			   2. It is a switch
			 */
			if (p_remote_physp && p_remote_physp->p_node->sw) {
				remote_u = p_remote_physp->p_node->sw->priv;
				port_guid = p_remote_physp->port_guid;

				if (remote_u->rank > u->rank + 1) {
					remote_u->rank = u->rank + 1;
					max_rank = remote_u->rank;
					cl_qlist_insert_tail(&list,
							     &remote_u->list);
					OSM_LOG(p_log, OSM_LOG_DEBUG,
						"Rank of port GUID 0x%" PRIx64
						" = %u\n", cl_ntoh64(port_guid),
						remote_u->rank);
				}
			}
		}
	}

	/* Print Summary of ranking */
	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"Subnet ranking completed. Max Node Rank = %d\n", max_rank);
	OSM_LOG_EXIT(p_log);
	return 0;
}

/* hack: preserve min hops entries to any other root switches */
static void updn_clear_non_root_hops(updn_t * updn, osm_switch_t * sw)
{
	osm_port_t *port;
	unsigned i;

	for (i = 0; i < sw->num_hops; i++)
		if (sw->hops[i]) {
			port = osm_get_port_by_lid_ho(&updn->p_osm->subn, i);
			if (!port || !port->p_node->sw
			    || ((struct updn_node *)port->p_node->sw->priv)->
			    rank != 0)
				memset(sw->hops[i], 0xff, sw->num_ports);
		}
}

static int updn_set_min_hop_table(IN updn_t * p_updn)
{
	osm_subn_t *p_subn = &p_updn->p_osm->subn;
	osm_log_t *p_log = &p_updn->p_osm->log;
	osm_switch_t *p_sw;
	cl_map_item_t *item;

	OSM_LOG_ENTER(p_log);

	/* Go over all the switches in the subnet - for each init their Min Hop
	   Table */
	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"Init Min Hop Table of all switches [\n");

	for (item = cl_qmap_head(&p_updn->p_osm->subn.sw_guid_tbl);
	     item != cl_qmap_end(&p_updn->p_osm->subn.sw_guid_tbl);
	     item = cl_qmap_next(item)) {
		p_sw = (osm_switch_t *)item;
		/* Clear Min Hop Table */
		if (p_subn->opt.connect_roots)
			updn_clear_non_root_hops(p_updn, p_sw);
		else
			osm_switch_clear_hops(p_sw);
	}

	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"Init Min Hop Table of all switches ]\n");

	/* Now do the BFS for each port  in the subnet */
	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"BFS through all port guids in the subnet [\n");

	for (item = cl_qmap_head(&p_updn->p_osm->subn.sw_guid_tbl);
	     item != cl_qmap_end(&p_updn->p_osm->subn.sw_guid_tbl);
	     item = cl_qmap_next(item)) {
		p_sw = (osm_switch_t *)item;
		updn_bfs_by_node(p_log, p_subn, p_sw);
	}

	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"BFS through all port guids in the subnet ]\n");
	/* Cleanup */
	OSM_LOG_EXIT(p_log);
	return 0;
}

static int updn_build_lid_matrices(IN updn_t * p_updn)
{
	int status;

	OSM_LOG_ENTER(&p_updn->p_osm->log);

	OSM_LOG(&p_updn->p_osm->log, OSM_LOG_VERBOSE,
		"Ranking all port guids in the list\n");
	if (!p_updn->num_roots) {
		OSM_LOG(&p_updn->p_osm->log, OSM_LOG_ERROR, "ERR AA0A: "
			"No guids were provided or number of guids is 0\n");
		status = -1;
		goto _exit;
	}

	/* Check if it's not a switched subnet */
	if (cl_is_qmap_empty(&p_updn->p_osm->subn.sw_guid_tbl)) {
		OSM_LOG(&p_updn->p_osm->log, OSM_LOG_ERROR, "ERR AA0B: "
			"This is not a switched subnet, cannot perform UPDN algorithm\n");
		status = -1;
		goto _exit;
	}

	/* Rank the subnet switches */
	if (updn_subn_rank(p_updn)) {
		OSM_LOG(&p_updn->p_osm->log, OSM_LOG_ERROR, "ERR AA0E: "
			"Failed to assign ranks\n");
		status = -1;
		goto _exit;
	}

	/* After multiple ranking need to set Min Hop Table by UpDn algorithm  */
	OSM_LOG(&p_updn->p_osm->log, OSM_LOG_VERBOSE,
		"Setting all switches' Min Hop Table\n");
	status = updn_set_min_hop_table(p_updn);

_exit:
	OSM_LOG_EXIT(&p_updn->p_osm->log);
	return status;
}

static struct updn_node *create_updn_node(osm_switch_t * sw)
{
	struct updn_node *u;

	u = malloc(sizeof(*u));
	if (!u)
		return NULL;
	memset(u, 0, sizeof(*u));
	u->sw = sw;
	u->id = cl_ntoh64(osm_node_get_node_guid(sw->p_node));
	u->rank = 0xffffffff;
	return u;
}

static void delete_updn_node(struct updn_node *u)
{
	u->sw->priv = NULL;
	free(u);
}

/* Find Root nodes automatically by Min Hop Table info */
static void updn_find_root_nodes_by_min_hop(OUT updn_t * p_updn)
{
	osm_opensm_t *p_osm = p_updn->p_osm;
	osm_switch_t *p_sw;
	osm_port_t *p_port;
	osm_physp_t *p_physp;
	cl_map_item_t *item;
	double thd1, thd2;
	unsigned i, cas_num = 0;
	unsigned *cas_per_sw;
	uint16_t lid_ho;

	OSM_LOG_ENTER(&p_osm->log);

	OSM_LOG(&p_osm->log, OSM_LOG_DEBUG,
		"Current number of ports in the subnet is %d\n",
		cl_qmap_count(&p_osm->subn.port_guid_tbl));

	lid_ho = (uint16_t) cl_ptr_vector_get_size(&p_updn->p_osm->subn.port_lid_tbl) + 1;
	cas_per_sw = malloc(lid_ho * sizeof(*cas_per_sw));
	if (!cas_per_sw) {
		OSM_LOG(&p_osm->log, OSM_LOG_ERROR, "ERR AA14: "
			"cannot alloc mem for CAs per switch counter array\n");
		goto _exit;
	}
	memset(cas_per_sw, 0, lid_ho * sizeof(*cas_per_sw));

	/* Find the Maximum number of CAs (and routers) for histogram normalization */
	OSM_LOG(&p_osm->log, OSM_LOG_VERBOSE,
		"Finding the number of CAs and storing them in cl_map\n");
	for (item = cl_qmap_head(&p_updn->p_osm->subn.port_guid_tbl);
	     item != cl_qmap_end(&p_updn->p_osm->subn.port_guid_tbl);
	     item = cl_qmap_next(item)) {
		p_port = (osm_port_t *)item;
		if (!p_port->p_node->sw) {
			p_physp = p_port->p_physp->p_remote_physp;
			if (!p_physp || !p_physp->p_node->sw)
				continue;
			lid_ho = osm_node_get_base_lid(p_physp->p_node, 0);
			lid_ho = cl_ntoh16(lid_ho);
			cas_per_sw[lid_ho]++;
			cas_num++;
		}
	}

	thd1 = cas_num * 0.9;
	thd2 = cas_num * 0.05;
	OSM_LOG(&p_osm->log, OSM_LOG_DEBUG,
		"Found %u CAs and RTRs, %u SWs in the subnet. "
		"Thresholds are thd1 = %f && thd2 = %f\n",
		cas_num, cl_qmap_count(&p_osm->subn.sw_guid_tbl), thd1, thd2);

	OSM_LOG(&p_osm->log, OSM_LOG_VERBOSE,
		"Passing through all switches to collect Min Hop info\n");
	for (item = cl_qmap_head(&p_updn->p_osm->subn.sw_guid_tbl);
	     item != cl_qmap_end(&p_updn->p_osm->subn.sw_guid_tbl);
	     item = cl_qmap_next(item)) {
		unsigned hop_hist[IB_SUBNET_PATH_HOPS_MAX];
		uint16_t max_lid_ho;
		uint8_t hop_val;
		uint16_t numHopBarsOverThd1 = 0;
		uint16_t numHopBarsOverThd2 = 0;

		p_sw = (osm_switch_t *) item;

		memset(hop_hist, 0, sizeof(hop_hist));

		max_lid_ho = p_sw->max_lid_ho;
		for (lid_ho = 1; lid_ho <= max_lid_ho; lid_ho++)
			if (cas_per_sw[lid_ho]) {
				hop_val =
				    osm_switch_get_least_hops(p_sw, lid_ho);
				if (hop_val >= IB_SUBNET_PATH_HOPS_MAX)
					continue;

				hop_hist[hop_val] += cas_per_sw[lid_ho];
			}

		/* Now recognize the spines by requiring one bar to be
		   above 90% of the number of CAs and RTRs */
		for (i = 0; i < IB_SUBNET_PATH_HOPS_MAX; i++) {
			if (hop_hist[i] > thd1)
				numHopBarsOverThd1++;
			if (hop_hist[i] > thd2)
				numHopBarsOverThd2++;
		}

		/* If thd conditions are valid - rank the root node */
		if (numHopBarsOverThd1 == 1 && numHopBarsOverThd2 == 1) {
			OSM_LOG(&p_osm->log, OSM_LOG_DEBUG,
				"Ranking GUID 0x%" PRIx64 " as root node\n",
				cl_ntoh64(osm_node_get_node_guid(p_sw->p_node)));
			((struct updn_node *)p_sw->priv)->rank = 0;
			p_updn->num_roots++;
		}
	}

	free(cas_per_sw);
_exit:
	OSM_LOG_EXIT(&p_osm->log);
	return;
}

static void dump_roots(cl_map_item_t *item, FILE *file, void *cxt)
{
	osm_switch_t *sw = (osm_switch_t *)item;
	if (!((struct updn_node *)sw->priv)->rank)
		fprintf(file, "0x%" PRIx64 "\n",
			cl_ntoh64(osm_node_get_node_guid(sw->p_node)));
}

static int update_id(void *cxt, uint64_t guid, char *p)
{
	osm_opensm_t *osm = cxt;
	osm_switch_t *sw;
	uint64_t id;
	char *e;

	sw = osm_get_switch_by_guid(&osm->subn, cl_hton64(guid));
	if (!sw) {
		OSM_LOG(&osm->log, OSM_LOG_VERBOSE,
			"switch with guid 0x%" PRIx64 " is not found\n", guid);
		return 0;
	}

	id = strtoull(p, &e, 0);
	if (*e && !isspace(*e)) {
		OSM_LOG(&osm->log, OSM_LOG_ERROR,
			"ERR AA05: cannot parse node id \'%s\'", p);
		return -1;
	}

	OSM_LOG(&osm->log, OSM_LOG_DEBUG,
		"update node 0x%" PRIx64 " id to 0x%" PRIx64 "\n", guid, id);

	((struct updn_node *)sw->priv)->id = id;

	return 0;
}

static int rank_root_node(void *cxt, uint64_t guid, char *p)
{
	updn_t *updn = cxt;
	osm_switch_t *sw;

	sw = osm_get_switch_by_guid(&updn->p_osm->subn, cl_hton64(guid));
	if (!sw) {
		OSM_LOG(&updn->p_osm->log, OSM_LOG_VERBOSE,
			"switch with guid 0x%" PRIx64 " is not found\n", guid);
		return 0;
	}

	OSM_LOG(&updn->p_osm->log, OSM_LOG_DEBUG,
		"Ranking root port GUID 0x%" PRIx64 "\n", guid);

	((struct updn_node *)sw->priv)->rank = 0;
	updn->num_roots++;

	return 0;
}

/* UPDN callback function */
static int updn_lid_matrices(void *ctx)
{
	updn_t *p_updn = ctx;
	cl_map_item_t *item;
	osm_switch_t *p_sw;
	int ret = 0;

	OSM_LOG_ENTER(&p_updn->p_osm->log);

	for (item = cl_qmap_head(&p_updn->p_osm->subn.sw_guid_tbl);
	     item != cl_qmap_end(&p_updn->p_osm->subn.sw_guid_tbl);
	     item = cl_qmap_next(item)) {
		p_sw = (osm_switch_t *)item;
		p_sw->priv = create_updn_node(p_sw);
		if (!p_sw->priv) {
			OSM_LOG(&(p_updn->p_osm->log), OSM_LOG_ERROR, "ERR AA0C: "
				"cannot create updn node\n");
			OSM_LOG_EXIT(&p_updn->p_osm->log);
			return -1;
		}
	}

	/* First setup root nodes */
	p_updn->num_roots = 0;

	if (p_updn->p_osm->subn.opt.root_guid_file) {
		OSM_LOG(&p_updn->p_osm->log, OSM_LOG_DEBUG,
			"UPDN - Fetching root nodes from file \'%s\'\n",
			p_updn->p_osm->subn.opt.root_guid_file);

		ret = parse_node_map(p_updn->p_osm->subn.opt.root_guid_file,
				     rank_root_node, p_updn);
		if (ret) {
			OSM_LOG(&p_updn->p_osm->log, OSM_LOG_ERROR, "ERR AA02: "
				"cannot parse root guids file \'%s\'\n",
				p_updn->p_osm->subn.opt.root_guid_file);
			osm_ucast_mgr_build_lid_matrices(&p_updn->p_osm->sm.ucast_mgr);
			updn_find_root_nodes_by_min_hop(p_updn);
		} else if (p_updn->p_osm->subn.opt.connect_roots &&
			   p_updn->num_roots > 1)
			osm_ucast_mgr_build_lid_matrices(&p_updn->p_osm->sm.ucast_mgr);
	} else {
		osm_ucast_mgr_build_lid_matrices(&p_updn->p_osm->sm.ucast_mgr);
		updn_find_root_nodes_by_min_hop(p_updn);
	}

	if (p_updn->p_osm->subn.opt.ids_guid_file) {
		OSM_LOG(&p_updn->p_osm->log, OSM_LOG_DEBUG,
			"UPDN - update node ids from file \'%s\'\n",
			p_updn->p_osm->subn.opt.ids_guid_file);

		ret = parse_node_map(p_updn->p_osm->subn.opt.ids_guid_file,
				     update_id, p_updn->p_osm);
		if (ret)
			OSM_LOG(&p_updn->p_osm->log, OSM_LOG_ERROR, "ERR AA03: "
				"cannot parse node ids file \'%s\'\n",
				p_updn->p_osm->subn.opt.ids_guid_file);
	}

	/* Only if there are assigned root nodes do the algorithm, otherwise perform do nothing */
	if (p_updn->num_roots) {
		OSM_LOG(&p_updn->p_osm->log, OSM_LOG_DEBUG,
			"activating UPDN algorithm\n");
		ret = updn_build_lid_matrices(p_updn);
	} else {
		OSM_LOG(&p_updn->p_osm->log, OSM_LOG_INFO,
			"disabling UPDN algorithm, no root nodes were found\n");
		ret = -1;
	}

	if (OSM_LOG_IS_ACTIVE_V2(&p_updn->p_osm->log, OSM_LOG_ROUTING))
		osm_dump_qmap_to_file(p_updn->p_osm, "opensm-updn-roots.dump",
				      &p_updn->p_osm->subn.sw_guid_tbl,
				      dump_roots, NULL);

	for (item = cl_qmap_head(&p_updn->p_osm->subn.sw_guid_tbl);
	     item != cl_qmap_end(&p_updn->p_osm->subn.sw_guid_tbl);
	     item = cl_qmap_next(item)) {
		p_sw = (osm_switch_t *) item;
		delete_updn_node(p_sw->priv);
	}

	OSM_LOG_EXIT(&p_updn->p_osm->log);
	return ret;
}

static void updn_delete(void *context)
{
	free(context);
}

int osm_ucast_updn_setup(struct osm_routing_engine *r, osm_opensm_t *osm)
{
	updn_t *updn;

	updn = malloc(sizeof(updn_t));
	if (!updn)
		return -1;
	memset(updn, 0, sizeof(updn_t));

	updn->p_osm = osm;

	r->context = updn;
	r->destroy = updn_delete;
	r->build_lid_matrices = updn_lid_matrices;

	return 0;
}
