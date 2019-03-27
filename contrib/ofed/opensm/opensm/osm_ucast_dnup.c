/*
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2007,2009 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
 * Copyright (c) 2009 Battelle Memorial Institue. All rights reserved.
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
#define FILE_ID OSM_FILE_UCAST_DNUP_C
#include <opensm/osm_switch.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_ucast_mgr.h>

/* //////////////////////////// */
/*  Local types                 */
/* //////////////////////////// */

/* direction */
typedef enum dnup_switch_dir {
	UP = 0,
	DOWN,
	EQUAL
} dnup_switch_dir_t;

/* dnup structure */
typedef struct dnup {
	osm_opensm_t *p_osm;
} dnup_t;

struct dnup_node {
	cl_list_item_t list;
	osm_switch_t *sw;
	dnup_switch_dir_t dir;
	unsigned rank;
	unsigned visited;
};

/* This function returns direction based on rank and guid info of current &
   remote ports */
static dnup_switch_dir_t dnup_get_dir(unsigned cur_rank, unsigned rem_rank)
{
	/* HACK: comes to solve root nodes connection, in a classic subnet root nodes do not connect
	   directly, but in case they are we assign to root node an UP direction to allow DNUP to discover
	   the subnet correctly (and not from the point of view of the last root node).
	 */
	if (!cur_rank && !rem_rank)
		return EQUAL;

	if (cur_rank < rem_rank)
		return DOWN;
	else if (cur_rank > rem_rank)
		return UP;
	else
		return EQUAL;
}

/**********************************************************************
 * This function does the bfs of min hop table calculation by guid index
 * as a starting point.
 **********************************************************************/
static int dnup_bfs_by_node(IN osm_log_t * p_log, IN osm_subn_t * p_subn,
			    IN osm_switch_t * p_sw, IN uint8_t prune_weight,
			    OUT uint8_t * max_hops)
{
	uint8_t pn, pn_rem;
	cl_qlist_t list;
	uint16_t lid;
	struct dnup_node *u;
	dnup_switch_dir_t next_dir, current_dir;

	OSM_LOG_ENTER(p_log);

	lid = osm_node_get_base_lid(p_sw->p_node, 0);
	lid = cl_ntoh16(lid);
	osm_switch_set_hops(p_sw, lid, 0, 0);

	OSM_LOG(p_log, OSM_LOG_DEBUG,
		"Starting from switch - port GUID 0x%" PRIx64 " lid %u\n",
		cl_ntoh64(p_sw->p_node->node_info.port_guid), lid);

	u = p_sw->priv;
	u->dir = DOWN;

	/* Update list with the new element */
	cl_qlist_init(&list);
	cl_qlist_insert_tail(&list, &u->list);

	/* BFS the list till no next element */
	while (!cl_is_qlist_empty(&list)) {
		u = (struct dnup_node *)cl_qlist_remove_head(&list);
		u->visited = 0;	/* cleanup */
		current_dir = u->dir;
		/* Go over all ports of the switch and find unvisited remote nodes */
		for (pn = 1; pn < u->sw->num_ports; pn++) {
			osm_node_t *p_remote_node;
			struct dnup_node *rem_u;
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
			next_dir = dnup_get_dir(u->rank, rem_u->rank);

			/* Set MinHop value for the current lid */
			current_min_hop = osm_switch_get_least_hops(u->sw, lid);
			/* Check hop count if better insert into list && update
			   the remote node Min Hop Table */
			remote_min_hop =
			    osm_switch_get_hop_count(p_remote_sw, lid, pn_rem);

			/* Check if this is a legal step : the only illegal step is going
			   from UP to DOWN */
			if ((current_dir == UP) && (next_dir == DOWN)) {
				OSM_LOG(p_log, OSM_LOG_DEBUG,
					"Avoiding move from 0x%016" PRIx64
					" to 0x%016" PRIx64 "\n",
					cl_ntoh64(osm_node_get_node_guid(u->sw->p_node)),
					cl_ntoh64(osm_node_get_node_guid(p_remote_node)));
				/* Illegal step. If prune_weight is set, allow it with an
				 * additional weight
				 */
				if(prune_weight) {
					current_min_hop+=prune_weight;
					if(current_min_hop >= 64) {
						OSM_LOG(p_log, OSM_LOG_ERROR,
							"ERR AE02: Too many hops on subnet,"
							" can't relax illegal Dn/Up transition.");
						osm_switch_set_hops(p_remote_sw, lid,
								    pn_rem, OSM_NO_PATH);
					}
				} else {
					continue;
				}
			}
			if (current_min_hop + 1 < remote_min_hop) {
				set_hop_return_value =
				    osm_switch_set_hops(p_remote_sw, lid,
							pn_rem,
							current_min_hop + 1);
				if(max_hops && current_min_hop + 1 > *max_hops) {
					*max_hops = current_min_hop + 1;
				}
				if (set_hop_return_value) {
					OSM_LOG(p_log, OSM_LOG_ERROR, "ERR AE01: "
						"Invalid value returned from set min hop is: %d\n",
						set_hop_return_value);
				}
				/* Check if remote port has already been visited */
				if (!rem_u->visited) {
					/* Insert dnup_switch item into the list */
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
static int dnup_subn_rank(IN dnup_t * p_dnup)
{
	osm_switch_t *p_sw;
	osm_physp_t *p_physp, *p_remote_physp;
	cl_qlist_t list;
	cl_map_item_t *item;
	struct dnup_node *u, *remote_u;
	uint8_t num_ports, port_num;
	osm_log_t *p_log = &p_dnup->p_osm->log;
	unsigned max_rank = 0;

	OSM_LOG_ENTER(p_log);
	cl_qlist_init(&list);

	/* add all node level switches to the list */
	for (item = cl_qmap_head(&p_dnup->p_osm->subn.sw_guid_tbl);
	     item != cl_qmap_end(&p_dnup->p_osm->subn.sw_guid_tbl);
	     item = cl_qmap_next(item)) {
		p_sw = (osm_switch_t *)item;
		u = p_sw->priv;
		if (u->rank == 0)
			cl_qlist_insert_tail(&list, &u->list);
	}

	/* BFS the list till it's empty */
	while (!cl_is_qlist_empty(&list)) {
		u = (struct dnup_node *)cl_qlist_remove_head(&list);
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

static int dnup_set_min_hop_table(IN dnup_t * p_dnup)
{
	osm_subn_t *p_subn = &p_dnup->p_osm->subn;
	osm_log_t *p_log = &p_dnup->p_osm->log;
	osm_switch_t *p_sw;
	struct dnup_node *u;
	cl_map_item_t *item;
	uint8_t max_hops = 0;

	OSM_LOG_ENTER(p_log);

	/* Go over all the switches in the subnet - for each init their Min Hop
	   Table */
	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"Init Min Hop Table of all switches [\n");

	for (item = cl_qmap_head(&p_dnup->p_osm->subn.sw_guid_tbl);
	     item != cl_qmap_end(&p_dnup->p_osm->subn.sw_guid_tbl);
	     item = cl_qmap_next(item)) {
		p_sw = (osm_switch_t *)item;
		/* Clear Min Hop Table */
		osm_switch_clear_hops(p_sw);
	}

	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"Init Min Hop Table of all switches ]\n");

	/* Now do the BFS for each port  in the subnet */
	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"BFS through all port guids in the subnet [\n");

	for (item = cl_qmap_head(&p_dnup->p_osm->subn.sw_guid_tbl);
	     item != cl_qmap_end(&p_dnup->p_osm->subn.sw_guid_tbl);
	     item = cl_qmap_next(item)) {
		p_sw = (osm_switch_t *)item;
		dnup_bfs_by_node(p_log, p_subn, p_sw, 0, &max_hops);
	}
	if(p_subn->opt.connect_roots) {
		/*This is probably not necessary, by I am more comfortable
		 * clearing any possible side effects from the previous
		 * dnup routing pass
		 */
		for (item = cl_qmap_head(&p_dnup->p_osm->subn.sw_guid_tbl);
		     item != cl_qmap_end(&p_dnup->p_osm->subn.sw_guid_tbl);
		     item = cl_qmap_next(item)) {
			p_sw = (osm_switch_t *)item;
			osm_switch_clear_hops(p_sw);
			u = (struct dnup_node *) p_sw->priv;
			u->visited = 0;
		}
		for (item = cl_qmap_head(&p_dnup->p_osm->subn.sw_guid_tbl);
		     item != cl_qmap_end(&p_dnup->p_osm->subn.sw_guid_tbl);
		     item = cl_qmap_next(item)) {
			p_sw = (osm_switch_t *)item;
			dnup_bfs_by_node(p_log, p_subn, p_sw, max_hops + 1, NULL);
		}
	}

	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"BFS through all port guids in the subnet ]\n");
	/* Cleanup */
	OSM_LOG_EXIT(p_log);
	return 0;
}

static int dnup_build_lid_matrices(IN dnup_t * p_dnup)
{
	int status;

	OSM_LOG_ENTER(&p_dnup->p_osm->log);

	OSM_LOG(&p_dnup->p_osm->log, OSM_LOG_VERBOSE,
		"Ranking all port guids in the list\n");
	/* Check if it's not a switched subnet */
	if (cl_is_qmap_empty(&p_dnup->p_osm->subn.sw_guid_tbl)) {
		OSM_LOG(&p_dnup->p_osm->log, OSM_LOG_ERROR, "ERR AEOB: "
			"This is not a switched subnet, cannot perform DNUP algorithm\n");
		status = -1;
		goto _exit;
	}

	/* Rank the subnet switches */
	dnup_subn_rank(p_dnup);

	/* After multiple ranking need to set Min Hop Table by DnUp algorithm  */
	OSM_LOG(&p_dnup->p_osm->log, OSM_LOG_VERBOSE,
		"Setting all switches' Min Hop Table\n");
	status = dnup_set_min_hop_table(p_dnup);

_exit:
	OSM_LOG_EXIT(&p_dnup->p_osm->log);
	return status;
}

static struct dnup_node *create_dnup_node(osm_switch_t * sw)
{
	struct dnup_node *u;

	u = malloc(sizeof(*u));
	if (!u)
		return NULL;
	memset(u, 0, sizeof(*u));
	u->sw = sw;
	u->rank = 0xffffffff;
	return u;
}

static void delete_dnup_node(struct dnup_node *u)
{
	u->sw->priv = NULL;
	free(u);
}

/* DNUP callback function */
static int dnup_lid_matrices(void *ctx)
{
	dnup_t *p_dnup = ctx;
	cl_map_item_t *item;
	osm_switch_t *p_sw;
	int ret = 0;
	int num_leafs = 0;
	uint8_t pn, pn_rem;

	OSM_LOG_ENTER(&p_dnup->p_osm->log);

	for (item = cl_qmap_head(&p_dnup->p_osm->subn.sw_guid_tbl);
	     item != cl_qmap_end(&p_dnup->p_osm->subn.sw_guid_tbl);
	     item = cl_qmap_next(item)) {
		p_sw = (osm_switch_t *)item;
		p_sw->priv = create_dnup_node(p_sw);
		if (!p_sw->priv) {
			OSM_LOG(&(p_dnup->p_osm->log), OSM_LOG_ERROR, "ERR AE0C: "
				"cannot create dnup node\n");
			OSM_LOG_EXIT(&p_dnup->p_osm->log);
			return -1;
		}
	}


	/* First setup node level nodes */
	for (item = cl_qmap_head(&p_dnup->p_osm->subn.sw_guid_tbl);
	     item != cl_qmap_end(&p_dnup->p_osm->subn.sw_guid_tbl);
	     item = cl_qmap_next(item)) {
		p_sw = (osm_switch_t *)item;

		for (pn = 0; pn < p_sw->num_ports; pn++) {
			osm_node_t *p_remote_node;
			p_remote_node = osm_node_get_remote_node(p_sw->p_node, pn, &pn_rem);
			if(p_remote_node && !p_remote_node->sw) {
				struct dnup_node *u = p_sw->priv;
				u->rank = 0;
				OSM_LOG(&(p_dnup->p_osm->log),
					OSM_LOG_VERBOSE, "(%s) rank 0 leaf switch\n",
					p_sw->p_node->print_desc);
				num_leafs++;
				break;
			}
		}
	}

	if(num_leafs == 0) {
		OSM_LOG(&(p_dnup->p_osm->log),
			OSM_LOG_ERROR, "ERR AE0D: No leaf switches found, DnUp routing failed\n");
		OSM_LOG_EXIT(&p_dnup->p_osm->log);
		return -1;
	}

	ret = dnup_build_lid_matrices(p_dnup);

	for (item = cl_qmap_head(&p_dnup->p_osm->subn.sw_guid_tbl);
	     item != cl_qmap_end(&p_dnup->p_osm->subn.sw_guid_tbl);
	     item = cl_qmap_next(item)) {
		p_sw = (osm_switch_t *) item;
		delete_dnup_node(p_sw->priv);
	}

	OSM_LOG_EXIT(&p_dnup->p_osm->log);
	return ret;
}

static void dnup_delete(void *context)
{
	free(context);
}

int osm_ucast_dnup_setup(struct osm_routing_engine *r, osm_opensm_t *osm)
{
	dnup_t *dnup;

	OSM_LOG_ENTER(&osm->log);

	dnup = malloc(sizeof(dnup_t));
	if (!dnup)
		return -1;
	memset(dnup, 0, sizeof(dnup_t));

	dnup->p_osm = osm;

	r->context = dnup;
	r->destroy = dnup_delete;
	r->build_lid_matrices = dnup_lid_matrices;

	OSM_LOG_EXIT(&osm->log);
	return 0;
}
