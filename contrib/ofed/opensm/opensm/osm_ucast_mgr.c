/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2015 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
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
 *    Implementation of osm_ucast_mgr_t.
 * This file implements the Unicast Manager object.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_debug.h>
#include <complib/cl_qlist.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_UCAST_MGR_C
#include <opensm/osm_ucast_mgr.h>
#include <opensm/osm_sm.h>
#include <opensm/osm_log.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_opensm.h>

void osm_ucast_mgr_construct(IN osm_ucast_mgr_t * p_mgr)
{
	memset(p_mgr, 0, sizeof(*p_mgr));
}

void osm_ucast_mgr_destroy(IN osm_ucast_mgr_t * p_mgr)
{
	CL_ASSERT(p_mgr);

	OSM_LOG_ENTER(p_mgr->p_log);

	if (p_mgr->cache_valid)
		osm_ucast_cache_invalidate(p_mgr);

	OSM_LOG_EXIT(p_mgr->p_log);
}

ib_api_status_t osm_ucast_mgr_init(IN osm_ucast_mgr_t * p_mgr, IN osm_sm_t * sm)
{
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sm->p_log);

	osm_ucast_mgr_construct(p_mgr);

	p_mgr->sm = sm;
	p_mgr->p_log = sm->p_log;
	p_mgr->p_subn = sm->p_subn;
	p_mgr->p_lock = sm->p_lock;

	if (sm->p_subn->opt.use_ucast_cache)
		cl_qmap_init(&p_mgr->cache_sw_tbl);

	OSM_LOG_EXIT(p_mgr->p_log);
	return status;
}

/**********************************************************************
 Add each switch's own and neighbor LIDs to its LID matrix
**********************************************************************/
static void ucast_mgr_process_hop_0_1(IN cl_map_item_t * p_map_item,
				      IN void *context)
{
	osm_switch_t * p_sw = (osm_switch_t *) p_map_item;
	osm_node_t *p_remote_node;
	uint16_t lid, remote_lid;
	uint8_t i;

	lid = cl_ntoh16(osm_node_get_base_lid(p_sw->p_node, 0));
	osm_switch_set_hops(p_sw, lid, 0, 0);

	for (i = 1; i < p_sw->num_ports; i++) {
		osm_physp_t *p = osm_node_get_physp_ptr(p_sw->p_node, i);
		p_remote_node = (p && p->p_remote_physp) ?
		    p->p_remote_physp->p_node : NULL;

		if (p_remote_node && p_remote_node->sw &&
		    p_remote_node != p_sw->p_node) {
			remote_lid = osm_node_get_base_lid(p_remote_node, 0);
			remote_lid = cl_ntoh16(remote_lid);
			osm_switch_set_hops(p_sw, remote_lid, i, p->hop_wf);
		}
	}
}

static void ucast_mgr_process_neighbor(IN osm_ucast_mgr_t * p_mgr,
				       IN osm_switch_t * p_this_sw,
				       IN osm_switch_t * p_remote_sw,
				       IN uint8_t port_num,
				       IN uint8_t remote_port_num)
{
	osm_switch_t *p_sw;
	cl_map_item_t *item;
	uint16_t lid_ho;
	uint16_t hops;
	osm_physp_t *p;

	OSM_LOG_ENTER(p_mgr->p_log);

	OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
		"Node 0x%" PRIx64 ", remote node 0x%" PRIx64
		", port %u, remote port %u\n",
		cl_ntoh64(osm_node_get_node_guid(p_this_sw->p_node)),
		cl_ntoh64(osm_node_get_node_guid(p_remote_sw->p_node)),
		port_num, remote_port_num);

	p = osm_node_get_physp_ptr(p_this_sw->p_node, port_num);

	for (item = cl_qmap_head(&p_mgr->p_subn->sw_guid_tbl);
	     item != cl_qmap_end(&p_mgr->p_subn->sw_guid_tbl);
	     item = cl_qmap_next(item)) {
		p_sw = (osm_switch_t *) item;
		lid_ho = cl_ntoh16(osm_node_get_base_lid(p_sw->p_node, 0));
		hops = osm_switch_get_least_hops(p_remote_sw, lid_ho);
		if (hops == OSM_NO_PATH)
			continue;
		hops += p->hop_wf;
		if (hops <
		    osm_switch_get_hop_count(p_this_sw, lid_ho, port_num)) {
			if (osm_switch_set_hops
			    (p_this_sw, lid_ho, port_num, (uint8_t) hops) != 0)
				OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR, "ERR 3A03: "
					"cannot set hops for lid %u at switch 0x%"
					PRIx64 "\n", lid_ho,
					cl_ntoh64(osm_node_get_node_guid
						  (p_this_sw->p_node)));
			p_mgr->some_hop_count_set = TRUE;
		}
	}

	OSM_LOG_EXIT(p_mgr->p_log);
}

static struct osm_remote_node *find_and_add_remote_sys(osm_switch_t * sw,
						       uint8_t port,
						       boolean_t dor, struct
						       osm_remote_guids_count
						       *r)
{
	unsigned i;
	osm_physp_t *p = osm_node_get_physp_ptr(sw->p_node, port);
	osm_node_t *node = p->p_remote_physp->p_node;
	uint8_t rem_port = osm_physp_get_port_num(p->p_remote_physp);

	for (i = 0; i < r->count; i++)
		if (r->guids[i].node == node)
			if (!dor || (r->guids[i].port == rem_port))
				return &r->guids[i];

	r->guids[i].node = node;
	r->guids[i].forwarded_to = 0;
	r->guids[i].port = rem_port;
	r->count++;
	return &r->guids[i];
}

static void ucast_mgr_process_port(IN osm_ucast_mgr_t * p_mgr,
				   IN osm_switch_t * p_sw,
				   IN osm_port_t * p_port,
				   IN unsigned lid_offset)
{
	uint16_t min_lid_ho;
	uint16_t max_lid_ho;
	uint16_t lid_ho;
	uint8_t port;
	boolean_t is_ignored_by_port_prof;
	ib_net64_t node_guid;
	unsigned start_from = 1;

	OSM_LOG_ENTER(p_mgr->p_log);

	osm_port_get_lid_range_ho(p_port, &min_lid_ho, &max_lid_ho);

	/* If the lids are zero - then there was some problem with
	 * the initialization. Don't handle this port. */
	if (min_lid_ho == 0 || max_lid_ho == 0) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR, "ERR 3A04: "
			"Port 0x%" PRIx64 " (%s port %d) has LID 0. An "
			"initialization error occurred. Ignoring port\n",
			cl_ntoh64(osm_port_get_guid(p_port)),
			p_port->p_node->print_desc,
			p_port->p_physp->port_num);
		goto Exit;
	}

	lid_ho = min_lid_ho + lid_offset;

	if (lid_ho > max_lid_ho)
		goto Exit;

	if (lid_offset && !p_mgr->is_dor)
		/* ignore potential overflow - it is handled in osm_switch.c */
		start_from =
		    osm_switch_get_port_by_lid(p_sw, lid_ho - 1, OSM_NEW_LFT) + 1;

	OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
		"Processing port 0x%" PRIx64
		" (\'%s\' port %u), LID %u [%u,%u]\n",
		cl_ntoh64(osm_port_get_guid(p_port)),
		p_port->p_node->print_desc, p_port->p_physp->port_num, lid_ho,
		min_lid_ho, max_lid_ho);

	/* TODO - This should be runtime error, not a CL_ASSERT() */
	CL_ASSERT(max_lid_ho <= IB_LID_UCAST_END_HO);

	node_guid = osm_node_get_node_guid(p_sw->p_node);

	/*
	   The lid matrix contains the number of hops to each
	   lid from each port.  From this information we determine
	   how best to distribute the LID range across the ports
	   that can reach those LIDs.
	 */
	port = osm_switch_recommend_path(p_sw, p_port, lid_ho, start_from,
					 p_mgr->p_subn->ignore_existing_lfts,
					 p_mgr->p_subn->opt.lmc,
					 p_mgr->is_dor,
					 p_mgr->p_subn->opt.port_shifting,
					 !lid_offset && p_port->use_scatter,
					 OSM_LFT);

	if (port == OSM_NO_PATH) {
		/* do not try to overwrite the ppro of non existing port ... */
		is_ignored_by_port_prof = TRUE;

		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"No path to get to LID %u from switch 0x%" PRIx64 "\n",
			lid_ho, cl_ntoh64(node_guid));
	} else {
		osm_physp_t *p = osm_node_get_physp_ptr(p_sw->p_node, port);
		if (!p)
			goto Exit;

		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Routing LID %u to port %u for switch 0x%" PRIx64 "\n",
			lid_ho, port, cl_ntoh64(node_guid));

		/*
		   we would like to optionally ignore this port in equalization
		   as in the case of the Mellanox Anafa Internal PCI TCA port
		 */
		is_ignored_by_port_prof = p->is_prof_ignored;

		/*
		   We also would ignore this route if the target lid is of
		   a switch and the port_profile_switch_node is not TRUE
		 */
		if (!p_mgr->p_subn->opt.port_profile_switch_nodes)
			is_ignored_by_port_prof |=
			    (osm_node_get_type(p_port->p_node) ==
			     IB_NODE_TYPE_SWITCH);
	}

	/*
	   We have selected the port for this LID.
	   Write it to the forwarding tables.
	 */
	p_sw->new_lft[lid_ho] = port;
	if (!is_ignored_by_port_prof) {
		struct osm_remote_node *rem_node_used;
		osm_switch_count_path(p_sw, port);
		if (port > 0 && p_port->priv &&
		    (rem_node_used = find_and_add_remote_sys(p_sw, port,
							     p_mgr->is_dor,
							     p_port->priv)))
			rem_node_used->forwarded_to++;
	}

Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
}

static void alloc_ports_priv(osm_ucast_mgr_t * mgr)
{
	cl_qmap_t *port_tbl = &mgr->p_subn->port_guid_tbl;
	struct osm_remote_guids_count *r;
	osm_port_t *port;
	cl_map_item_t *item;
	unsigned lmc;

	for (item = cl_qmap_head(port_tbl); item != cl_qmap_end(port_tbl);
	     item = cl_qmap_next(item)) {
		port = (osm_port_t *) item;
		lmc = ib_port_info_get_lmc(&port->p_physp->port_info);
		r = malloc(sizeof(*r) + sizeof(r->guids[0]) * (1 << lmc));
		if (!r) {
			OSM_LOG(mgr->p_log, OSM_LOG_ERROR, "ERR 3A09: "
				"cannot allocate memory to track remote"
				" systems for lmc > 0\n");
			port->priv = NULL;
			continue;
		}
		memset(r, 0, sizeof(*r) + sizeof(r->guids[0]) * (1 << lmc));
		port->priv = r;
	}
}

static void free_ports_priv(osm_ucast_mgr_t * mgr)
{
	cl_qmap_t *port_tbl = &mgr->p_subn->port_guid_tbl;
	osm_port_t *port;
	cl_map_item_t *item;
	for (item = cl_qmap_head(port_tbl); item != cl_qmap_end(port_tbl);
	     item = cl_qmap_next(item)) {
		port = (osm_port_t *) item;
		if (port->priv) {
			free(port->priv);
			port->priv = NULL;
		}
	}
}

static void ucast_mgr_process_tbl(IN cl_map_item_t * p_map_item,
				  IN void *context)
{
	osm_ucast_mgr_t *p_mgr = context;
	osm_switch_t * p_sw = (osm_switch_t *) p_map_item;
	unsigned i, lids_per_port;

	OSM_LOG_ENTER(p_mgr->p_log);

	CL_ASSERT(p_sw && p_sw->p_node);

	OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
		"Processing switch 0x%" PRIx64 "\n",
		cl_ntoh64(osm_node_get_node_guid(p_sw->p_node)));

	/* Initialize LIDs in buffer to invalid port number. */
	memset(p_sw->new_lft, OSM_NO_PATH, p_sw->max_lid_ho + 1);

	alloc_ports_priv(p_mgr);

	/*
	   Iterate through every port setting LID routes for each
	   port based on base LID and LMC value.
	 */
	lids_per_port = 1 << p_mgr->p_subn->opt.lmc;
	for (i = 0; i < lids_per_port; i++) {
		cl_qlist_t *list = &p_mgr->port_order_list;
		cl_list_item_t *item;
		for (item = cl_qlist_head(list); item != cl_qlist_end(list);
		     item = cl_qlist_next(item)) {
			osm_port_t *port = cl_item_obj(item, port, list_item);
			ucast_mgr_process_port(p_mgr, p_sw, port, i);
		}
	}

	free_ports_priv(p_mgr);

	OSM_LOG_EXIT(p_mgr->p_log);
}

static void ucast_mgr_process_neighbors(IN cl_map_item_t * p_map_item,
					IN void *context)
{
	osm_switch_t * p_sw = (osm_switch_t *) p_map_item;
	osm_ucast_mgr_t * p_mgr = context;
	osm_node_t *p_node;
	osm_node_t *p_remote_node;
	uint32_t port_num;
	uint8_t remote_port_num;
	uint32_t num_ports;
	osm_physp_t *p_physp;

	OSM_LOG_ENTER(p_mgr->p_log);

	p_node = p_sw->p_node;

	CL_ASSERT(p_node);
	CL_ASSERT(osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH);

	OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
		"Processing switch with GUID 0x%" PRIx64 "\n",
		cl_ntoh64(osm_node_get_node_guid(p_node)));

	num_ports = osm_node_get_num_physp(p_node);

	/*
	   Start with port 1 to skip the switch's management port.
	 */
	for (port_num = 1; port_num < num_ports; port_num++) {
		p_remote_node = osm_node_get_remote_node(p_node,
							 (uint8_t) port_num,
							 &remote_port_num);
		if (p_remote_node && p_remote_node->sw
		    && (p_remote_node != p_node)) {
			/* make sure the link is healthy. If it is not - don't
			   propagate through it. */
			p_physp = osm_node_get_physp_ptr(p_node, port_num);
			if (!p_physp || !osm_link_is_healthy(p_physp))
				continue;

			ucast_mgr_process_neighbor(p_mgr, p_sw,
						   p_remote_node->sw,
						   (uint8_t) port_num,
						   remote_port_num);
		}
	}

	OSM_LOG_EXIT(p_mgr->p_log);
}

static int set_hop_wf(void *ctx, uint64_t guid, char *p)
{
	osm_ucast_mgr_t *m = ctx;
	osm_node_t *node = osm_get_node_by_guid(m->p_subn, cl_hton64(guid));
	osm_physp_t *physp;
	unsigned port, hop_wf;
	char *e;

	if (!node || !node->sw) {
		OSM_LOG(m->p_log, OSM_LOG_DEBUG,
			"switch with guid 0x%016" PRIx64 " is not found\n",
			guid);
		return 0;
	}

	if (!p || !*p || !(port = strtoul(p, &e, 0)) || (p == e) ||
	    port >= node->sw->num_ports) {
		OSM_LOG(m->p_log, OSM_LOG_DEBUG,
			"bad port specified for guid 0x%016" PRIx64 "\n", guid);
		return 0;
	}

	p = e + 1;

	if (!*p || !(hop_wf = strtoul(p, &e, 0)) || p == e || hop_wf >= 0x100) {
		OSM_LOG(m->p_log, OSM_LOG_DEBUG,
			"bad hop weight factor specified for guid 0x%016" PRIx64
			"port %u\n", guid, port);
		return 0;
	}

	physp = osm_node_get_physp_ptr(node, port);
	if (!physp)
		return 0;

	physp->hop_wf = hop_wf;

	return 0;
}

static void set_default_hop_wf(cl_map_item_t * p_map_item, void *ctx)
{
	osm_switch_t *sw = (osm_switch_t *) p_map_item;
	int i;

	for (i = 1; i < sw->num_ports; i++) {
		osm_physp_t *p = osm_node_get_physp_ptr(sw->p_node, i);
		if (p)
			p->hop_wf = 1;
	}
}

static int set_search_ordering_ports(void *ctx, uint64_t guid, char *p)
{
	osm_subn_t *p_subn = ctx;
	osm_node_t *node = osm_get_node_by_guid(p_subn, cl_hton64(guid));
	osm_switch_t *sw;
	uint8_t *search_ordering_ports = NULL;
	uint8_t port;
	unsigned int *ports = NULL;
	const int bpw = sizeof(*ports)*8;
	int words;
	int i = 1; /* port 0 maps to port 0 */

	if (!node || !(sw = node->sw)) {
		OSM_LOG(&p_subn->p_osm->log, OSM_LOG_VERBOSE,
			"switch with guid 0x%016" PRIx64 " is not found\n",
			guid);
		return 0;
	}

	if (sw->search_ordering_ports) {
		OSM_LOG(&p_subn->p_osm->log, OSM_LOG_VERBOSE,
			"switch with guid 0x%016" PRIx64 " already listed\n",
			guid);
		return 0;
	}

	search_ordering_ports = malloc(sizeof(*search_ordering_ports)*sw->num_ports);
	if (!search_ordering_ports) {
		OSM_LOG(&p_subn->p_osm->log, OSM_LOG_ERROR,
			"ERR 3A07: cannot allocate memory for search_ordering_ports\n");
		return -1;
	}
	memset(search_ordering_ports, 0, sizeof(*search_ordering_ports)*sw->num_ports);

	/* the ports array is for record keeping of which ports have
	 * been seen */
	words = (sw->num_ports + bpw - 1)/bpw;
	ports = malloc(words*sizeof(*ports));
	if (!ports) {
		OSM_LOG(&p_subn->p_osm->log, OSM_LOG_ERROR,
			"ERR 3A08: cannot allocate memory for ports\n");
		free(search_ordering_ports);
		return -1;
	}
	memset(ports, 0, words*sizeof(*ports));

	while ((*p != '\0') && (*p != '#')) {
		char *e;

		port = strtoul(p, &e, 0);
		if ((p == e) || (port == 0) || (port >= sw->num_ports) ||
		    !osm_node_get_physp_ptr(node, port)) {
			OSM_LOG(&p_subn->p_osm->log, OSM_LOG_VERBOSE,
				"bad port %d specified for guid 0x%016" PRIx64 "\n",
				port, guid);
			free(search_ordering_ports);
			free(ports);
			return 0;
		}

		if (ports[port/bpw] & (1u << (port%bpw))) {
			OSM_LOG(&p_subn->p_osm->log, OSM_LOG_VERBOSE,
				"port %d already specified for guid 0x%016" PRIx64 "\n",
				port, guid);
			free(search_ordering_ports);
			free(ports);
			return 0;
		}

		ports[port/bpw] |= (1u << (port%bpw));
		search_ordering_ports[i++] = port;

		p = e;
		while (isspace(*p)) {
			p++;
		}
	}

	if (i > 1) {
		for (port = 1; port < sw->num_ports; port++) {
			/* fill out the rest of the search_ordering_ports array
			 * in sequence using the remaining unspecified
			 * ports.
			 */
			if (!(ports[port/bpw] & (1u << (port%bpw)))) {
				search_ordering_ports[i++] = port;
			}
		}
		sw->search_ordering_ports = search_ordering_ports;
	} else {
		free(search_ordering_ports);
	}

	free(ports);
	return 0;
}

int osm_ucast_mgr_build_lid_matrices(IN osm_ucast_mgr_t * p_mgr)
{
	uint32_t i;
	uint32_t iteration_max;
	cl_qmap_t *p_sw_guid_tbl;

	p_sw_guid_tbl = &p_mgr->p_subn->sw_guid_tbl;

	OSM_LOG(p_mgr->p_log, OSM_LOG_VERBOSE,
		"Starting switches' Min Hop Table Assignment\n");

	/*
	   Set up the weighting factors for the routing.
	 */
	cl_qmap_apply_func(p_sw_guid_tbl, set_default_hop_wf, NULL);
	if (p_mgr->p_subn->opt.hop_weights_file) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Fetching hop weight factor file \'%s\'\n",
			p_mgr->p_subn->opt.hop_weights_file);
		if (parse_node_map(p_mgr->p_subn->opt.hop_weights_file,
				   set_hop_wf, p_mgr)) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR, "ERR 3A05: "
				"cannot parse hop_weights_file \'%s\'\n",
				p_mgr->p_subn->opt.hop_weights_file);
		}
	}

	/*
	   Set the switch matrices for each switch's own port 0 LID(s)
	   then set the lid matrices for the each switch's leaf nodes.
	 */
	cl_qmap_apply_func(p_sw_guid_tbl, ucast_mgr_process_hop_0_1, p_mgr);

	/*
	   Get the switch matrices for each switch's neighbors.
	   This process requires a number of iterations equal to
	   the number of switches in the subnet minus 1.

	   In each iteration, a switch learns the lid/port/hop
	   information (as contained by a switch's lid matrix) from
	   its immediate neighbors.  After each iteration, a switch
	   (and it's neighbors) know more routing information than
	   it did on the previous iteration.
	   Thus, by repeatedly absorbing the routing information of
	   neighbor switches, every switch eventually learns how to
	   route all LIDs on the subnet.

	   Note that there may not be any switches in the subnet if
	   we are in simple p2p configuration.
	 */
	iteration_max = cl_qmap_count(p_sw_guid_tbl);

	/*
	   If there are switches in the subnet, iterate until the lid
	   matrix has been constructed.  Otherwise, just immediately
	   indicate we're done if no switches exist.
	 */
	if (iteration_max) {
		iteration_max--;

		/*
		   we need to find out when the propagation of
		   hop counts has relaxed. So this global variable
		   is preset to 0 on each iteration and if
		   if non of the switches was set will exit the
		   while loop
		 */
		p_mgr->some_hop_count_set = TRUE;
		for (i = 0; (i < iteration_max) && p_mgr->some_hop_count_set;
		     i++) {
			p_mgr->some_hop_count_set = FALSE;
			cl_qmap_apply_func(p_sw_guid_tbl,
					   ucast_mgr_process_neighbors, p_mgr);
		}
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Min-hop propagated in %d steps\n", i);
	}

	return 0;
}

static int ucast_mgr_setup_all_switches(osm_subn_t * p_subn)
{
	osm_switch_t *p_sw;
	uint16_t lids;

	lids = (uint16_t) cl_ptr_vector_get_size(&p_subn->port_lid_tbl);
	lids = lids ? lids - 1 : 0;

	for (p_sw = (osm_switch_t *) cl_qmap_head(&p_subn->sw_guid_tbl);
	     p_sw != (osm_switch_t *) cl_qmap_end(&p_subn->sw_guid_tbl);
	     p_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item)) {
		if (osm_switch_prepare_path_rebuild(p_sw, lids)) {
			OSM_LOG(&p_subn->p_osm->log, OSM_LOG_ERROR, "ERR 3A0B: "
				"cannot setup switch 0x%016" PRIx64 "\n",
				cl_ntoh64(osm_node_get_node_guid
					  (p_sw->p_node)));
			return -1;
		}
		if (p_sw->search_ordering_ports) {
			free(p_sw->search_ordering_ports);
			p_sw->search_ordering_ports = NULL;
		}
	}

	if (p_subn->opt.port_search_ordering_file) {
		OSM_LOG(&p_subn->p_osm->log, OSM_LOG_DEBUG,
			"Fetching dimension ports file \'%s\'\n",
			p_subn->opt.port_search_ordering_file);
		if (parse_node_map(p_subn->opt.port_search_ordering_file,
				   set_search_ordering_ports, p_subn)) {
			OSM_LOG(&p_subn->p_osm->log, OSM_LOG_ERROR, "ERR 3A0F: "
				"cannot parse port_search_ordering_file \'%s\'\n",
				p_subn->opt.port_search_ordering_file);
		}
	}

	return 0;
}

static int add_guid_to_order_list(void *ctx, uint64_t guid, char *p)
{
	osm_ucast_mgr_t *m = ctx;
	osm_port_t *port = osm_get_port_by_guid(m->p_subn, cl_hton64(guid));

	if (!port) {
		OSM_LOG(m->p_log, OSM_LOG_DEBUG,
			"port guid not found: 0x%016" PRIx64 "\n", guid);
		return 0;
	}

	if (port->flag) {
		OSM_LOG(m->p_log, OSM_LOG_DEBUG,
			"port guid specified multiple times 0x%016" PRIx64 "\n",
			guid);
		return 0;
	}

	cl_qlist_insert_tail(&m->port_order_list, &port->list_item);
	port->flag = 1;
	port->use_scatter =  (m->p_subn->opt.guid_routing_order_no_scatter == TRUE) ? 0 : m->p_subn->opt.scatter_ports;

	return 0;
}

static void add_port_to_order_list(cl_map_item_t * p_map_item, void *ctx)
{
	osm_port_t *port = (osm_port_t *) p_map_item;
	osm_ucast_mgr_t *m = ctx;

	if (!port->flag) {
		port->use_scatter = m->p_subn->opt.scatter_ports;
		cl_qlist_insert_tail(&m->port_order_list, &port->list_item);
	} else
		port->flag = 0;
}

static int mark_ignored_port(void *ctx, uint64_t guid, char *p)
{
	osm_ucast_mgr_t *m = ctx;
	osm_node_t *node = osm_get_node_by_guid(m->p_subn, cl_hton64(guid));
	osm_physp_t *physp;
	unsigned port;

	if (!node || !node->sw) {
		OSM_LOG(m->p_log, OSM_LOG_DEBUG,
			"switch with guid 0x%016" PRIx64 " is not found\n",
			guid);
		return 0;
	}

	if (!p || !*p || !(port = strtoul(p, NULL, 0)) ||
	    port >= node->sw->num_ports) {
		OSM_LOG(m->p_log, OSM_LOG_DEBUG,
			"bad port specified for guid 0x%016" PRIx64 "\n", guid);
		return 0;
	}

	physp = osm_node_get_physp_ptr(node, port);
	if (!physp)
		return 0;

	physp->is_prof_ignored = 1;

	return 0;
}

static void clear_prof_ignore_flag(cl_map_item_t * p_map_item, void *ctx)
{
	osm_switch_t *sw = (osm_switch_t *) p_map_item;
	int i;

	for (i = 1; i < sw->num_ports; i++) {
		osm_physp_t *p = osm_node_get_physp_ptr(sw->p_node, i);
		if (p)
			p->is_prof_ignored = 0;
	}
}

static void add_sw_endports_to_order_list(osm_switch_t * sw,
					  osm_ucast_mgr_t * m)
{
	osm_port_t *port;
	osm_physp_t *p;
	int i;

	for (i = 1; i < sw->num_ports; i++) {
		p = osm_node_get_physp_ptr(sw->p_node, i);
		if (p && p->p_remote_physp && !p->p_remote_physp->p_node->sw) {
			port = osm_get_port_by_guid(m->p_subn,
						    p->p_remote_physp->
						    port_guid);
			if (!port || port->flag)
				continue;
			cl_qlist_insert_tail(&m->port_order_list,
					     &port->list_item);
			port->flag = 1;
			port->use_scatter = m->p_subn->opt.scatter_ports;
		}
	}
}

static void sw_count_endport_links(osm_switch_t * sw)
{
	osm_physp_t *p;
	int i;

	sw->endport_links = 0;
	for (i = 1; i < sw->num_ports; i++) {
		p = osm_node_get_physp_ptr(sw->p_node, i);
		if (p && p->p_remote_physp && !p->p_remote_physp->p_node->sw)
			sw->endport_links++;
	}
}

static int compar_sw_load(const void *s1, const void *s2)
{
#define get_sw_endport_links(s) (*(osm_switch_t **)s)->endport_links
	return get_sw_endport_links(s2) - get_sw_endport_links(s1);
}

static void sort_ports_by_switch_load(osm_ucast_mgr_t * m)
{
	int i, num = cl_qmap_count(&m->p_subn->sw_guid_tbl);
	void **s = malloc(num * sizeof(*s));
	if (!s) {
		OSM_LOG(m->p_log, OSM_LOG_ERROR, "ERR 3A0C: "
			"No memory, skip by switch load sorting.\n");
		return;
	}
	s[0] = cl_qmap_head(&m->p_subn->sw_guid_tbl);
	for (i = 1; i < num; i++)
		s[i] = cl_qmap_next(s[i - 1]);

	for (i = 0; i < num; i++)
		sw_count_endport_links(s[i]);

	qsort(s, num, sizeof(*s), compar_sw_load);

	for (i = 0; i < num; i++)
		add_sw_endports_to_order_list(s[i], m);
	free(s);
}

static int ucast_mgr_build_lfts(osm_ucast_mgr_t * p_mgr)
{
	cl_qlist_init(&p_mgr->port_order_list);

	if (p_mgr->p_subn->opt.guid_routing_order_file) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Fetching guid routing order file \'%s\'\n",
			p_mgr->p_subn->opt.guid_routing_order_file);

		if (parse_node_map(p_mgr->p_subn->opt.guid_routing_order_file,
				   add_guid_to_order_list, p_mgr))
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR, "ERR 3A0D: "
				"cannot parse guid routing order file \'%s\'\n",
				p_mgr->p_subn->opt.guid_routing_order_file);
	}
	sort_ports_by_switch_load(p_mgr);

	if (p_mgr->p_subn->opt.port_prof_ignore_file) {
		cl_qmap_apply_func(&p_mgr->p_subn->sw_guid_tbl,
				   clear_prof_ignore_flag, NULL);
		if (parse_node_map(p_mgr->p_subn->opt.port_prof_ignore_file,
				   mark_ignored_port, p_mgr)) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR, "ERR 3A0E: "
				"cannot parse port prof ignore file \'%s\'\n",
				p_mgr->p_subn->opt.port_prof_ignore_file);
		}
	}

	cl_qmap_apply_func(&p_mgr->p_subn->port_guid_tbl,
			   add_port_to_order_list, p_mgr);

	cl_qmap_apply_func(&p_mgr->p_subn->sw_guid_tbl, ucast_mgr_process_tbl,
			   p_mgr);

	cl_qlist_remove_all(&p_mgr->port_order_list);

	return 0;
}

static void ucast_mgr_set_fwd_top(IN cl_map_item_t * p_map_item,
				  IN void *cxt)
{
	osm_ucast_mgr_t *p_mgr = cxt;
	osm_switch_t * p_sw = (osm_switch_t *) p_map_item;
	osm_node_t *p_node;
	osm_physp_t *p_physp;
	osm_dr_path_t *p_path;
	osm_madw_context_t context;
	ib_api_status_t status;
	ib_switch_info_t si;
	boolean_t set_swinfo_require = FALSE;
	uint16_t lin_top;
	uint8_t life_state;

	CL_ASSERT(p_mgr);

	OSM_LOG_ENTER(p_mgr->p_log);

	CL_ASSERT(p_sw && p_sw->max_lid_ho);

	p_node = p_sw->p_node;

	CL_ASSERT(p_node);

	if (p_mgr->max_lid < p_sw->max_lid_ho)
		p_mgr->max_lid = p_sw->max_lid_ho;

	p_physp = osm_node_get_physp_ptr(p_node, 0);

	CL_ASSERT(p_physp);

	p_path = osm_physp_get_dr_path_ptr(p_physp);

	/*
	   Set the top of the unicast forwarding table.
	 */
	si = p_sw->switch_info;
	lin_top = cl_hton16(p_sw->max_lid_ho);
	if (lin_top != si.lin_top) {
		set_swinfo_require = TRUE;
		si.lin_top = lin_top;
		context.si_context.lft_top_change = TRUE;
	} else
		context.si_context.lft_top_change = FALSE;

	life_state = si.life_state;
	ib_switch_info_set_life_time(&si, p_mgr->p_subn->opt.packet_life_time);

	if (life_state != si.life_state)
		set_swinfo_require = TRUE;

	if (set_swinfo_require) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Setting switch FT top to LID %u\n", p_sw->max_lid_ho);

		context.si_context.light_sweep = FALSE;
		context.si_context.node_guid = osm_node_get_node_guid(p_node);
		context.si_context.set_method = TRUE;

		status = osm_req_set(p_mgr->sm, p_path, (uint8_t *) & si,
				     sizeof(si), IB_MAD_ATTR_SWITCH_INFO,
				     0, FALSE,
				     ib_port_info_get_m_key(&p_physp->port_info),
				     CL_DISP_MSGID_NONE, &context);

		if (status != IB_SUCCESS)
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR, "ERR 3A06: "
				"Sending SwitchInfo attribute failed (%s)\n",
				ib_get_err_str(status));
	}

	OSM_LOG_EXIT(p_mgr->p_log);
}

static int set_lft_block(IN osm_switch_t *p_sw, IN osm_ucast_mgr_t *p_mgr,
			 IN uint16_t block_id_ho)
{
	osm_madw_context_t context;
	osm_dr_path_t *p_path;
	osm_physp_t *p_physp;
	ib_api_status_t status;

	/*
	   Send linear forwarding table blocks to the switch
	   as long as the switch indicates it has blocks needing
	   configuration.
	 */
	if (!p_sw->new_lft) {
		/* any routing should provide the new_lft */
		CL_ASSERT(p_mgr->p_subn->opt.use_ucast_cache &&
			  p_mgr->cache_valid && !p_sw->need_update);
		return -1;
	}

	p_physp = osm_node_get_physp_ptr(p_sw->p_node, 0);
	if (!p_physp)
		return -1;

	p_path = osm_physp_get_dr_path_ptr(p_physp);

	context.lft_context.node_guid = osm_node_get_node_guid(p_sw->p_node);
	context.lft_context.set_method = TRUE;

	if (!p_sw->need_update && !p_mgr->p_subn->need_update &&
	    !memcmp(p_sw->new_lft + block_id_ho * IB_SMP_DATA_SIZE,
		    p_sw->lft + block_id_ho * IB_SMP_DATA_SIZE,
		    IB_SMP_DATA_SIZE))
		return 0;

	/*
	 * Zero the stored LFT block, so in case the MAD will end up
	 * with error, we will resend it in the next sweep.
	 */
	memset(p_sw->lft + block_id_ho * IB_SMP_DATA_SIZE, 0,
	       IB_SMP_DATA_SIZE);

	OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
		"Writing FT block %u to switch 0x%" PRIx64 "\n", block_id_ho,
		cl_ntoh64(context.lft_context.node_guid));

	status = osm_req_set(p_mgr->sm, p_path,
			     p_sw->new_lft + block_id_ho * IB_SMP_DATA_SIZE,
			     IB_SMP_DATA_SIZE, IB_MAD_ATTR_LIN_FWD_TBL,
			     cl_hton32(block_id_ho), FALSE,
			     ib_port_info_get_m_key(&p_physp->port_info),
			     CL_DISP_MSGID_NONE, &context);

	if (status != IB_SUCCESS) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR, "ERR 3A10: "
			"Sending linear fwd. tbl. block failed (%s)\n",
			ib_get_err_str(status));
		return -1;
	}

	return 0;
}

static void ucast_mgr_pipeline_fwd_tbl(osm_ucast_mgr_t * p_mgr)
{
	cl_qmap_t *tbl;
	cl_map_item_t *item;
	unsigned i, max_block = p_mgr->max_lid / IB_SMP_DATA_SIZE + 1;

	tbl = &p_mgr->p_subn->sw_guid_tbl;
	for (i = 0; i < max_block; i++)
		for (item = cl_qmap_head(tbl); item != cl_qmap_end(tbl);
		     item = cl_qmap_next(item))
			set_lft_block((osm_switch_t *)item, p_mgr, i);
}

void osm_ucast_mgr_set_fwd_tables(osm_ucast_mgr_t * p_mgr)
{
	p_mgr->max_lid = 0;

	cl_qmap_apply_func(&p_mgr->p_subn->sw_guid_tbl, ucast_mgr_set_fwd_top,
			   p_mgr);

	ucast_mgr_pipeline_fwd_tbl(p_mgr);
}

static int ucast_mgr_route(struct osm_routing_engine *r, osm_opensm_t * osm)
{
	int ret;

	OSM_LOG(&osm->log, OSM_LOG_VERBOSE,
		"building routing with \'%s\' routing algorithm...\n", r->name);

	/* Set the before each lft build to keep the routes in place between sweeps */
	if (osm->subn.opt.scatter_ports)
		srandom(osm->subn.opt.scatter_ports);

	if (!r->build_lid_matrices ||
	    (ret = r->build_lid_matrices(r->context)) > 0)
		ret = osm_ucast_mgr_build_lid_matrices(&osm->sm.ucast_mgr);

	if (ret < 0) {
		OSM_LOG(&osm->log, OSM_LOG_ERROR,
			"%s: cannot build lid matrices\n", r->name);
		return ret;
	}

	if (!r->ucast_build_fwd_tables ||
	    (ret = r->ucast_build_fwd_tables(r->context)) > 0)
		ret = ucast_mgr_build_lfts(&osm->sm.ucast_mgr);

	if (ret < 0) {
		OSM_LOG(&osm->log, OSM_LOG_ERROR,
			"%s: cannot build fwd tables\n", r->name);
		return ret;
	}

	osm->routing_engine_used = r;

	osm_ucast_mgr_set_fwd_tables(&osm->sm.ucast_mgr);

	return 0;
}

int osm_ucast_mgr_process(IN osm_ucast_mgr_t * p_mgr)
{
	osm_opensm_t *p_osm;
	struct osm_routing_engine *p_routing_eng;
	cl_qmap_t *p_sw_guid_tbl;
	int failed = 0;

	OSM_LOG_ENTER(p_mgr->p_log);

	p_sw_guid_tbl = &p_mgr->p_subn->sw_guid_tbl;
	p_osm = p_mgr->p_subn->p_osm;
	p_routing_eng = p_osm->routing_engine_list;

	CL_PLOCK_EXCL_ACQUIRE(p_mgr->p_lock);

	/*
	   If there are no switches in the subnet, we are done.
	 */
	if (cl_qmap_count(p_sw_guid_tbl) == 0 ||
	    ucast_mgr_setup_all_switches(p_mgr->p_subn) < 0)
		goto Exit;

	failed = -1;
	p_osm->routing_engine_used = NULL;
	while (p_routing_eng) {
		failed = ucast_mgr_route(p_routing_eng, p_osm);
		if (!failed)
			break;
		p_routing_eng = p_routing_eng->next;
	}

	if (!p_osm->routing_engine_used &&
	    p_osm->no_fallback_routing_engine != TRUE) {
		/* If configured routing algorithm failed, use default MinHop */
		failed = ucast_mgr_route(p_osm->default_routing_engine, p_osm);
	}

	if (p_osm->routing_engine_used) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_INFO,
			"%s tables configured on all switches\n",
			osm_routing_engine_type_str(p_osm->
						    routing_engine_used->type));

		if (p_mgr->p_subn->opt.use_ucast_cache)
			p_mgr->cache_valid = TRUE;
	} else {
		p_mgr->p_subn->subnet_initialization_error = TRUE;
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
			"No routing engine able to successfully configure "
			" switch tables on current fabric\n");
	}
Exit:
	CL_PLOCK_RELEASE(p_mgr->p_lock);
	OSM_LOG_EXIT(p_mgr->p_log);
	return failed;
}

static int ucast_build_lid_matrices(void *context)
{
	return osm_ucast_mgr_build_lid_matrices(context);
}

static int ucast_build_lfts(void *context)
{
	return ucast_mgr_build_lfts(context);
}

int osm_ucast_minhop_setup(struct osm_routing_engine *r, osm_opensm_t * osm)
{
	r->context = &osm->sm.ucast_mgr;
	r->build_lid_matrices = ucast_build_lid_matrices;
	r->ucast_build_fwd_tables = ucast_build_lfts;
	return 0;
}

static int ucast_dor_build_lfts(void *context)
{
	osm_ucast_mgr_t *mgr = context;
	int ret;

	mgr->is_dor = 1;
	ret = ucast_mgr_build_lfts(mgr);
	mgr->is_dor = 0;

	return ret;
}

int osm_ucast_dor_setup(struct osm_routing_engine *r, osm_opensm_t * osm)
{
	r->context = &osm->sm.ucast_mgr;
	r->build_lid_matrices = ucast_build_lid_matrices;
	r->ucast_build_fwd_tables = ucast_dor_build_lfts;
	return 0;
}

int ucast_dummy_build_lid_matrices(void *context)
{
	return 0;
}
