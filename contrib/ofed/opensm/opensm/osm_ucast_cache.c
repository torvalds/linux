/*
 * Copyright (c) 2008-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2008-2009 Mellanox Technologies LTD. All rights reserved.
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
 *    Implementation of OpenSM Cached Unicast Routing
 *
 * Environment:
 *    Linux User Mode
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_pool.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_UCAST_CACHE_C
#include <opensm/osm_opensm.h>
#include <opensm/osm_ucast_mgr.h>
#include <opensm/osm_ucast_cache.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_node.h>
#include <opensm/osm_port.h>

typedef struct cache_port {
	boolean_t is_leaf;
	uint16_t remote_lid_ho;
} cache_port_t;

typedef struct cache_switch {
	cl_map_item_t map_item;
	boolean_t dropped;
	uint16_t max_lid_ho;
	uint16_t num_hops;
	uint8_t **hops;
	uint8_t *lft;
	uint8_t num_ports;
	cache_port_t ports[0];
} cache_switch_t;

static uint16_t cache_sw_get_base_lid_ho(cache_switch_t * p_sw)
{
	return p_sw->ports[0].remote_lid_ho;
}

static boolean_t cache_sw_is_leaf(cache_switch_t * p_sw)
{
	return p_sw->ports[0].is_leaf;
}

static void cache_sw_set_leaf(cache_switch_t * p_sw)
{
	p_sw->ports[0].is_leaf = TRUE;
}

static cache_switch_t *cache_sw_new(uint16_t lid_ho, unsigned num_ports)
{
	cache_switch_t *p_cache_sw = malloc(sizeof(cache_switch_t) +
					    num_ports * sizeof(cache_port_t));
	if (!p_cache_sw)
		return NULL;

	memset(p_cache_sw, 0,
	       sizeof(*p_cache_sw) + num_ports * sizeof(cache_port_t));

	p_cache_sw->num_ports = num_ports;

	/* port[0] fields represent this switch details - lid and type */
	p_cache_sw->ports[0].remote_lid_ho = lid_ho;
	p_cache_sw->ports[0].is_leaf = FALSE;

	return p_cache_sw;
}

static void cache_sw_destroy(cache_switch_t * p_sw)
{
	unsigned i;

	if (!p_sw)
		return;

	if (p_sw->lft)
		free(p_sw->lft);
	if (p_sw->hops) {
		for (i = 0; i < p_sw->num_hops; i++)
			if (p_sw->hops[i])
				free(p_sw->hops[i]);
		free(p_sw->hops);
	}
	free(p_sw);
}

static cache_switch_t *cache_get_sw(osm_ucast_mgr_t * p_mgr, uint16_t lid_ho)
{
	cache_switch_t *p_cache_sw = (cache_switch_t *)
	    cl_qmap_get(&p_mgr->cache_sw_tbl, lid_ho);
	if (p_cache_sw == (cache_switch_t *)
	    cl_qmap_end(&p_mgr->cache_sw_tbl))
		p_cache_sw = NULL;

	return p_cache_sw;
}

static void cache_add_sw_link(osm_ucast_mgr_t * p_mgr, osm_physp_t *p,
			      uint16_t remote_lid_ho, boolean_t is_ca)
{
	cache_switch_t *p_cache_sw;
	uint16_t lid_ho = cl_ntoh16(osm_node_get_base_lid(p->p_node, 0));

	OSM_LOG_ENTER(p_mgr->p_log);

	if (!lid_ho || !remote_lid_ho || !p->port_num)
		goto Exit;

	OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
		"Caching switch port: lid %u [port %u] -> lid %u (%s)\n",
		lid_ho, p->port_num, remote_lid_ho, (is_ca) ? "CA/RTR" : "SW");

	p_cache_sw = cache_get_sw(p_mgr, lid_ho);
	if (!p_cache_sw) {
		p_cache_sw = cache_sw_new(lid_ho, p->p_node->sw->num_ports);
		if (!p_cache_sw) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
				"ERR AD01: Out of memory - cache is invalid\n");
			osm_ucast_cache_invalidate(p_mgr);
			goto Exit;
		}
		cl_qmap_insert(&p_mgr->cache_sw_tbl, lid_ho,
			       &p_cache_sw->map_item);
	}

	if (p->port_num >= p_cache_sw->num_ports) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
			"ERR AD02: Wrong switch? - cache is invalid\n");
		osm_ucast_cache_invalidate(p_mgr);
		goto Exit;
	}

	if (is_ca)
		cache_sw_set_leaf(p_cache_sw);

	if (p_cache_sw->ports[p->port_num].remote_lid_ho == 0) {
		/* cache this link only if it hasn't been already cached */
		p_cache_sw->ports[p->port_num].remote_lid_ho = remote_lid_ho;
		p_cache_sw->ports[p->port_num].is_leaf = is_ca;
	}
Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
}

static void cache_cleanup_switches(osm_ucast_mgr_t * p_mgr)
{
	cache_switch_t *p_sw;
	cache_switch_t *p_next_sw;
	unsigned port_num;
	boolean_t found_port;

	if (!p_mgr->cache_valid)
		return;

	p_next_sw = (cache_switch_t *) cl_qmap_head(&p_mgr->cache_sw_tbl);
	while (p_next_sw !=
	       (cache_switch_t *) cl_qmap_end(&p_mgr->cache_sw_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (cache_switch_t *) cl_qmap_next(&p_sw->map_item);

		found_port = FALSE;
		for (port_num = 1; port_num < p_sw->num_ports; port_num++)
			if (p_sw->ports[port_num].remote_lid_ho)
				found_port = TRUE;

		if (!found_port) {
			cl_qmap_remove_item(&p_mgr->cache_sw_tbl,
					    &p_sw->map_item);
			cache_sw_destroy(p_sw);
		}
	}
}

static void
cache_check_link_change(osm_ucast_mgr_t * p_mgr,
			osm_physp_t * p_physp_1, osm_physp_t * p_physp_2)
{
	OSM_LOG_ENTER(p_mgr->p_log);
	CL_ASSERT(p_physp_1 && p_physp_2);

	if (!p_mgr->cache_valid)
		goto Exit;

	if (!p_physp_1->p_remote_physp && !p_physp_2->p_remote_physp)
		/* both ports were down - new link */
		goto Exit;

	/* unicast cache cannot tolerate any link location change */

	if ((p_physp_1->p_remote_physp &&
	     p_physp_1->p_remote_physp->p_remote_physp) ||
	    (p_physp_2->p_remote_physp &&
	     p_physp_2->p_remote_physp->p_remote_physp)) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Link location change discovered\n");
		osm_ucast_cache_invalidate(p_mgr);
		goto Exit;
	}
Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
}

static void cache_remove_port(osm_ucast_mgr_t * p_mgr, uint16_t lid_ho,
			      uint8_t port_num, uint16_t remote_lid_ho,
			      boolean_t is_ca)
{
	cache_switch_t *p_cache_sw;

	OSM_LOG_ENTER(p_mgr->p_log);

	if (!p_mgr->cache_valid)
		goto Exit;

	p_cache_sw = cache_get_sw(p_mgr, lid_ho);
	if (!p_cache_sw) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Found uncached switch/link (lid %u, port %u)\n",
			lid_ho, port_num);
		osm_ucast_cache_invalidate(p_mgr);
		goto Exit;
	}

	if (port_num >= p_cache_sw->num_ports ||
	    !p_cache_sw->ports[port_num].remote_lid_ho) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Found uncached switch link (lid %u, port %u)\n",
			lid_ho, port_num);
		osm_ucast_cache_invalidate(p_mgr);
		goto Exit;
	}

	if (p_cache_sw->ports[port_num].remote_lid_ho != remote_lid_ho) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Remote lid change on switch lid %u, port %u "
			"(was %u, now %u)\n", lid_ho, port_num,
			p_cache_sw->ports[port_num].remote_lid_ho,
			remote_lid_ho);
		osm_ucast_cache_invalidate(p_mgr);
		goto Exit;
	}

	if ((p_cache_sw->ports[port_num].is_leaf && !is_ca) ||
	    (!p_cache_sw->ports[port_num].is_leaf && is_ca)) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Remote node type change on switch lid %u, port %u\n",
			lid_ho, port_num);
		osm_ucast_cache_invalidate(p_mgr);
		goto Exit;
	}

	OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
		"New link from lid %u, port %u to lid %u - "
		"found in cache\n", lid_ho, port_num, remote_lid_ho);

	/* the new link was cached - clean it from the cache */

	p_cache_sw->ports[port_num].remote_lid_ho = 0;
	p_cache_sw->ports[port_num].is_leaf = FALSE;
Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
}				/* cache_remove_port() */

static void
cache_restore_ucast_info(osm_ucast_mgr_t * p_mgr,
			 cache_switch_t * p_cache_sw, osm_switch_t * p_sw)
{
	if (!p_mgr->cache_valid)
		return;

	/* when seting unicast info, the cached port
	   should have all the required info */
	CL_ASSERT(p_cache_sw->max_lid_ho && p_cache_sw->lft &&
		  p_cache_sw->num_hops && p_cache_sw->hops);

	p_sw->max_lid_ho = p_cache_sw->max_lid_ho;

	if (p_sw->new_lft)
		free(p_sw->new_lft);
	p_sw->new_lft = p_cache_sw->lft;
	p_cache_sw->lft = NULL;

	p_sw->num_hops = p_cache_sw->num_hops;
	p_cache_sw->num_hops = 0;
	if (p_sw->hops)
		free(p_sw->hops);
	p_sw->hops = p_cache_sw->hops;
	p_cache_sw->hops = NULL;

	p_sw->need_update = 2;
}

static void ucast_cache_dump(osm_ucast_mgr_t * p_mgr)
{
	cache_switch_t *p_sw;
	unsigned i;

	OSM_LOG_ENTER(p_mgr->p_log);

	if (!OSM_LOG_IS_ACTIVE_V2(p_mgr->p_log, OSM_LOG_DEBUG))
		goto Exit;

	OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
		"Dumping missing nodes/links as logged by unicast cache:\n");
	for (p_sw = (cache_switch_t *) cl_qmap_head(&p_mgr->cache_sw_tbl);
	     p_sw != (cache_switch_t *) cl_qmap_end(&p_mgr->cache_sw_tbl);
	     p_sw = (cache_switch_t *) cl_qmap_next(&p_sw->map_item)) {

		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"\t Switch lid %u %s%s\n",
			cache_sw_get_base_lid_ho(p_sw),
			(cache_sw_is_leaf(p_sw)) ? "[leaf switch] " : "",
			(p_sw->dropped) ? "[whole switch missing]" : "");

		for (i = 1; i < p_sw->num_ports; i++)
			if (p_sw->ports[i].remote_lid_ho > 0)
				OSM_LOG(p_mgr->p_log,
					OSM_LOG_DEBUG,
					"\t     - port %u -> lid %u %s\n",
					i, p_sw->ports[i].remote_lid_ho,
					(p_sw->ports[i].is_leaf) ?
					"[remote node is leaf]" : "");
	}
Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
}

void osm_ucast_cache_invalidate(osm_ucast_mgr_t * p_mgr)
{
	cache_switch_t *p_sw;
	cache_switch_t *p_next_sw;

	OSM_LOG_ENTER(p_mgr->p_log);

	if (!p_mgr->cache_valid)
		goto Exit;

	p_mgr->cache_valid = FALSE;

	p_next_sw = (cache_switch_t *) cl_qmap_head(&p_mgr->cache_sw_tbl);
	while (p_next_sw !=
	       (cache_switch_t *) cl_qmap_end(&p_mgr->cache_sw_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (cache_switch_t *) cl_qmap_next(&p_sw->map_item);
		cache_sw_destroy(p_sw);
	}
	cl_qmap_remove_all(&p_mgr->cache_sw_tbl);

	OSM_LOG(p_mgr->p_log, OSM_LOG_VERBOSE, "Unicast Cache invalidated\n");
Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
}

static void ucast_cache_validate(osm_ucast_mgr_t * p_mgr)
{
	cache_switch_t *p_cache_sw;
	cache_switch_t *p_remote_cache_sw;
	unsigned port_num;
	unsigned max_ports;
	uint8_t remote_node_type;
	uint16_t lid_ho;
	uint16_t remote_lid_ho;
	osm_switch_t *p_sw;
	osm_switch_t *p_remote_sw;
	osm_node_t *p_node;
	osm_physp_t *p_physp;
	osm_physp_t *p_remote_physp;
	osm_port_t *p_remote_port;
	cl_qmap_t *p_sw_tbl;

	OSM_LOG_ENTER(p_mgr->p_log);
	if (!p_mgr->cache_valid)
		goto Exit;

	/* If there are no switches in the subnet, we are done */
	p_sw_tbl = &p_mgr->p_subn->sw_guid_tbl;
	if (cl_qmap_count(p_sw_tbl) == 0) {
		osm_ucast_cache_invalidate(p_mgr);
		goto Exit;
	}

	/*
	 * Scan all the physical switch ports in the subnet.
	 * If the port need_update flag is on, check whether
	 * it's just some node/port reset or a cached topology
	 * change. Otherwise the cache is invalid.
	 */
	for (p_sw = (osm_switch_t *) cl_qmap_head(p_sw_tbl);
	     p_sw != (osm_switch_t *) cl_qmap_end(p_sw_tbl);
	     p_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item)) {

		p_node = p_sw->p_node;

		lid_ho = cl_ntoh16(osm_node_get_base_lid(p_node, 0));
		p_cache_sw = cache_get_sw(p_mgr, lid_ho);

		max_ports = osm_node_get_num_physp(p_node);

		/* skip port 0 */
		for (port_num = 1; port_num < max_ports; port_num++) {

			p_physp = osm_node_get_physp_ptr(p_node, port_num);

			if (!p_physp || !p_physp->p_remote_physp ||
			    !osm_physp_link_exists(p_physp,
						   p_physp->p_remote_physp))
				/* no valid link */
				continue;

			/*
			 * While scanning all the physical ports in the subnet,
			 * mark corresponding leaf switches in the cache.
			 */
			if (p_cache_sw &&
			    !p_cache_sw->dropped &&
			    !cache_sw_is_leaf(p_cache_sw) &&
			    p_physp->p_remote_physp->p_node &&
			    osm_node_get_type(p_physp->p_remote_physp->
					      p_node) != IB_NODE_TYPE_SWITCH)
				cache_sw_set_leaf(p_cache_sw);

			if (!p_physp->need_update)
				continue;

			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Checking switch lid %u, port %u\n",
				lid_ho, port_num);

			p_remote_physp = osm_physp_get_remote(p_physp);
			remote_node_type =
			    osm_node_get_type(p_remote_physp->p_node);

			if (remote_node_type == IB_NODE_TYPE_SWITCH)
				remote_lid_ho =
				    cl_ntoh16(osm_node_get_base_lid
					      (p_remote_physp->p_node, 0));
			else
				remote_lid_ho =
				    cl_ntoh16(osm_node_get_base_lid
					      (p_remote_physp->p_node,
					       osm_physp_get_port_num
					       (p_remote_physp)));

			if (!p_cache_sw ||
			    port_num >= p_cache_sw->num_ports ||
			    !p_cache_sw->ports[port_num].remote_lid_ho) {
				/*
				 * There is some uncached change on the port.
				 * In general, the reasons might be as follows:
				 *  - switch reset
				 *  - port reset (or port down/up)
				 *  - quick connection location change
				 *  - new link (or new switch)
				 *
				 * First two reasons allow cache usage, while
				 * the last two reasons should invalidate cache.
				 *
				 * In case of quick connection location change,
				 * cache would have been invalidated by
				 * osm_ucast_cache_check_new_link() function.
				 *
				 * In case of new link between two known nodes,
				 * cache also would have been invalidated by
				 * osm_ucast_cache_check_new_link() function.
				 *
				 * Another reason is cached link between two
				 * known switches went back. In this case the
				 * osm_ucast_cache_check_new_link() function would
				 * clear both sides of the link from the cache
				 * during the discovery process, so effectively
				 * this would be equivalent to port reset.
				 *
				 * So three possible reasons remain:
				 *  - switch reset
				 *  - port reset (or port down/up)
				 *  - link of a new switch
				 *
				 * To validate cache, we need to check only the
				 * third reason - link of a new node/switch:
				 *  - If this is the local switch that is new,
				 *    then it should have (p_sw->need_update == 2).
				 *  - If the remote node is switch and it's new,
				 *    then it also should have
				 *    (p_sw->need_update == 2).
				 *  - If the remote node is CA/RTR and it's new,
				 *    then its port should have is_new flag on.
				 */
				if (p_sw->need_update == 2) {
					OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
						"New switch found (lid %u)\n",
						lid_ho);
					osm_ucast_cache_invalidate(p_mgr);
					goto Exit;
				}

				if (remote_node_type == IB_NODE_TYPE_SWITCH) {

					p_remote_sw =
					    p_remote_physp->p_node->sw;
					if (p_remote_sw->need_update == 2) {
						/* this could also be case of
						   switch coming back with an
						   additional link that it
						   didn't have before */
						OSM_LOG(p_mgr->p_log,
							OSM_LOG_DEBUG,
							"New switch/link found (lid %u)\n",
							remote_lid_ho);
						osm_ucast_cache_invalidate
						    (p_mgr);
						goto Exit;
					}
				} else {
					/*
					 * Remote node is CA/RTR.
					 * Get p_port of the remote node and
					 * check its p_port->is_new flag.
					 */
					p_remote_port =
					    osm_get_port_by_guid(p_mgr->p_subn,
								 osm_physp_get_port_guid
								 (p_remote_physp));
					if (!p_remote_port) {
						OSM_LOG(p_mgr->p_log,
							OSM_LOG_ERROR,
							"ERR AD04: No port was found for "
							"port GUID 0x%" PRIx64 "\n",
							cl_ntoh64(osm_physp_get_port_guid
								      (p_remote_physp)));
						osm_ucast_cache_invalidate
						    (p_mgr);
						goto Exit;
					}
					if (p_remote_port->is_new) {
						OSM_LOG(p_mgr->p_log,
							OSM_LOG_DEBUG,
							"New CA/RTR found (lid %u)\n",
							remote_lid_ho);
						osm_ucast_cache_invalidate
						    (p_mgr);
						goto Exit;
					}
				}
			} else {
				/*
				 * The change on the port is cached.
				 * In general, the reasons might be as follows:
				 *  - link between two known nodes went back
				 *  - one or more nodes went back, causing all
				 *    the links to reappear
				 *
				 * If it was link that went back, then this case
				 * would have been taken care of during the
				 * discovery by osm_ucast_cache_check_new_link(),
				 * so it's some node that went back.
				 */
				if ((p_cache_sw->ports[port_num].is_leaf &&
				     remote_node_type == IB_NODE_TYPE_SWITCH) ||
				    (!p_cache_sw->ports[port_num].is_leaf &&
				     remote_node_type != IB_NODE_TYPE_SWITCH)) {
					OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
						"Remote node type change on switch lid %u, port %u\n",
						lid_ho, port_num);
					osm_ucast_cache_invalidate(p_mgr);
					goto Exit;
				}

				if (p_cache_sw->ports[port_num].remote_lid_ho !=
				    remote_lid_ho) {
					OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
						"Remote lid change on switch lid %u, port %u"
						"(was %u, now %u)\n",
						lid_ho, port_num,
						p_cache_sw->ports[port_num].
						remote_lid_ho, remote_lid_ho);
					osm_ucast_cache_invalidate(p_mgr);
					goto Exit;
				}

				/*
				 * We don't care who is the node that has
				 * reappeared in the subnet (local or remote).
				 * What's important that the cached link matches
				 * the real fabrics link.
				 * Just clean it from cache.
				 */

				p_cache_sw->ports[port_num].remote_lid_ho = 0;
				p_cache_sw->ports[port_num].is_leaf = FALSE;
				if (p_cache_sw->dropped) {
					cache_restore_ucast_info(p_mgr,
								 p_cache_sw,
								 p_sw);
					p_cache_sw->dropped = FALSE;
				}

				OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
					"Restored link from cache: lid %u, port %u to lid %u\n",
					lid_ho, port_num, remote_lid_ho);
			}
		}
	}

	/* Remove all the cached switches that
	   have all their ports restored */
	cache_cleanup_switches(p_mgr);

	/*
	 * Done scanning all the physical switch ports in the subnet.
	 * Now we need to check the other side:
	 * Scan all the cached switches and their ports:
	 *  - If the cached switch is missing in the subnet
	 *    (dropped flag is on), check that it's a leaf switch.
	 *    If it's not a leaf, the cache is invalid, because
	 *    cache can tolerate only leaf switch removal.
	 *  - If the cached switch exists in fabric, check all
	 *    its cached ports. These cached ports represent
	 *    missing link in the fabric.
	 *    The missing links that can be tolerated are:
	 *      + link to missing CA/RTR
	 *      + link to missing leaf switch
	 */
	for (p_cache_sw = (cache_switch_t *) cl_qmap_head(&p_mgr->cache_sw_tbl);
	     p_cache_sw != (cache_switch_t *) cl_qmap_end(&p_mgr->cache_sw_tbl);
	     p_cache_sw =
	     (cache_switch_t *) cl_qmap_next(&p_cache_sw->map_item)) {

		if (p_cache_sw->dropped) {
			if (!cache_sw_is_leaf(p_cache_sw)) {
				OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
					"Missing non-leaf switch (lid %u)\n",
					cache_sw_get_base_lid_ho(p_cache_sw));
				osm_ucast_cache_invalidate(p_mgr);
				goto Exit;
			}

			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Missing leaf switch (lid %u) - "
				"continuing validation\n",
				cache_sw_get_base_lid_ho(p_cache_sw));
			continue;
		}

		for (port_num = 1; port_num < p_cache_sw->num_ports; port_num++) {
			if (!p_cache_sw->ports[port_num].remote_lid_ho)
				continue;

			if (p_cache_sw->ports[port_num].is_leaf) {
				CL_ASSERT(cache_sw_is_leaf(p_cache_sw));
				OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
					"Switch lid %u, port %u: missing link to CA/RTR - "
					"continuing validation\n",
					cache_sw_get_base_lid_ho(p_cache_sw),
					port_num);
				continue;
			}

			p_remote_cache_sw = cache_get_sw(p_mgr,
							 p_cache_sw->
							 ports[port_num].
							 remote_lid_ho);

			if (!p_remote_cache_sw || !p_remote_cache_sw->dropped) {
				OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
					"Switch lid %u, port %u: missing link to existing switch\n",
					cache_sw_get_base_lid_ho(p_cache_sw),
					port_num);
				osm_ucast_cache_invalidate(p_mgr);
				goto Exit;
			}

			if (!cache_sw_is_leaf(p_remote_cache_sw)) {
				OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
					"Switch lid %u, port %u: missing link to non-leaf switch\n",
					cache_sw_get_base_lid_ho(p_cache_sw),
					port_num);
				osm_ucast_cache_invalidate(p_mgr);
				goto Exit;
			}

			/*
			 * At this point we know that the missing link is to
			 * a leaf switch. However, one case deserves a special
			 * treatment. If there was a link between two leaf
			 * switches, then missing leaf switch might break
			 * routing. It is possible that there are routes
			 * that use leaf switches to get from switch to switch
			 * and not just to get to the CAs behind the leaf switch.
			 */
			if (cache_sw_is_leaf(p_cache_sw) &&
			    cache_sw_is_leaf(p_remote_cache_sw)) {
				OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
					"Switch lid %u, port %u: missing leaf-2-leaf link\n",
					cache_sw_get_base_lid_ho(p_cache_sw),
					port_num);
				osm_ucast_cache_invalidate(p_mgr);
				goto Exit;
			}

			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Switch lid %u, port %u: missing remote leaf switch - "
				"continuing validation\n",
				cache_sw_get_base_lid_ho(p_cache_sw),
				port_num);
		}
	}

	OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG, "Unicast cache is valid\n");
	ucast_cache_dump(p_mgr);
Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
}				/* osm_ucast_cache_validate() */

void osm_ucast_cache_check_new_link(osm_ucast_mgr_t * p_mgr,
				    osm_node_t * p_node_1, uint8_t port_num_1,
				    osm_node_t * p_node_2, uint8_t port_num_2)
{
	uint16_t lid_ho_1;
	uint16_t lid_ho_2;

	OSM_LOG_ENTER(p_mgr->p_log);

	if (!p_mgr->cache_valid)
		goto Exit;

	cache_check_link_change(p_mgr,
				osm_node_get_physp_ptr(p_node_1, port_num_1),
				osm_node_get_physp_ptr(p_node_2, port_num_2));

	if (!p_mgr->cache_valid)
		goto Exit;

	if (osm_node_get_type(p_node_1) != IB_NODE_TYPE_SWITCH &&
	    osm_node_get_type(p_node_2) != IB_NODE_TYPE_SWITCH) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG, "Found CA-2-CA link\n");
		osm_ucast_cache_invalidate(p_mgr);
		goto Exit;
	}

	/* for code simplicity, we want the first node to be switch */
	if (osm_node_get_type(p_node_1) != IB_NODE_TYPE_SWITCH) {
		osm_node_t *tmp_node = p_node_1;
		uint8_t tmp_port_num = port_num_1;
		p_node_1 = p_node_2;
		port_num_1 = port_num_2;
		p_node_2 = tmp_node;
		port_num_2 = tmp_port_num;
	}

	lid_ho_1 = cl_ntoh16(osm_node_get_base_lid(p_node_1, 0));

	if (osm_node_get_type(p_node_2) == IB_NODE_TYPE_SWITCH)
		lid_ho_2 = cl_ntoh16(osm_node_get_base_lid(p_node_2, 0));
	else
		lid_ho_2 =
		    cl_ntoh16(osm_node_get_base_lid(p_node_2, port_num_2));

	if (!lid_ho_1 || !lid_ho_2) {
		/*
		 * No lid assigned, which means that one of the nodes is new.
		 * Need to wait for lid manager to process this node.
		 * The switches and their links will be checked later when
		 * the whole cache validity will be verified.
		 */
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Link port %u <-> %u reveals new node - cache will "
			"be validated later\n", port_num_1, port_num_2);
		goto Exit;
	}

	cache_remove_port(p_mgr, lid_ho_1, port_num_1, lid_ho_2,
			  (osm_node_get_type(p_node_2) !=
			  IB_NODE_TYPE_SWITCH));

	/* if node_2 is a switch, the link should be cleaned from its cache */

	if (osm_node_get_type(p_node_2) == IB_NODE_TYPE_SWITCH)
		cache_remove_port(p_mgr, lid_ho_2,
				  port_num_2, lid_ho_1, FALSE);

Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
}				/* osm_ucast_cache_check_new_link() */

void osm_ucast_cache_add_link(osm_ucast_mgr_t * p_mgr,
			      osm_physp_t * p_physp1, osm_physp_t * p_physp2)
{
	osm_node_t *p_node_1 = p_physp1->p_node, *p_node_2 = p_physp2->p_node;
	uint16_t lid_ho_1, lid_ho_2;

	OSM_LOG_ENTER(p_mgr->p_log);

	if (!p_mgr->cache_valid)
		goto Exit;

	if (osm_node_get_type(p_node_1) != IB_NODE_TYPE_SWITCH &&
	    osm_node_get_type(p_node_2) != IB_NODE_TYPE_SWITCH) {
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG, "Dropping CA-2-CA link\n");
		osm_ucast_cache_invalidate(p_mgr);
		goto Exit;
	}

	if ((osm_node_get_type(p_node_1) == IB_NODE_TYPE_SWITCH &&
	     !osm_node_get_physp_ptr(p_node_1, 0)) ||
	    (osm_node_get_type(p_node_2) == IB_NODE_TYPE_SWITCH &&
	     !osm_node_get_physp_ptr(p_node_2, 0))) {
		/* we're caching a link when one of the nodes
		   has already been dropped and cached */
		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Port %u <-> port %u: port0 on one of the nodes "
			"has already been dropped and cached\n",
			p_physp1->port_num, p_physp2->port_num);
		goto Exit;
	}

	/* One of the nodes is switch. Just for code
	   simplicity, make sure that it's the first node. */

	if (osm_node_get_type(p_node_1) != IB_NODE_TYPE_SWITCH) {
		osm_physp_t *tmp = p_physp1;
		p_physp1 = p_physp2;
		p_physp2 = tmp;
		p_node_1 = p_physp1->p_node;
		p_node_2 = p_physp2->p_node;
	}

	if (!p_node_1->sw) {
		/* something is wrong - we'd better not use cache */
		osm_ucast_cache_invalidate(p_mgr);
		goto Exit;
	}

	lid_ho_1 = cl_ntoh16(osm_node_get_base_lid(p_node_1, 0));

	if (osm_node_get_type(p_node_2) == IB_NODE_TYPE_SWITCH) {

		if (!p_node_2->sw) {
			/* something is wrong - we'd better not use cache */
			osm_ucast_cache_invalidate(p_mgr);
			goto Exit;
		}

		lid_ho_2 = cl_ntoh16(osm_node_get_base_lid(p_node_2, 0));

		/* lost switch-2-switch link - cache both sides */
		cache_add_sw_link(p_mgr, p_physp1, lid_ho_2, FALSE);
		cache_add_sw_link(p_mgr, p_physp2, lid_ho_1, FALSE);
	} else {
		lid_ho_2 = cl_ntoh16(osm_physp_get_base_lid(p_physp2));

		/* lost link to CA/RTR - cache only switch side */
		cache_add_sw_link(p_mgr, p_physp1, lid_ho_2, TRUE);
	}

Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
}				/* osm_ucast_cache_add_link() */

void osm_ucast_cache_add_node(osm_ucast_mgr_t * p_mgr, osm_node_t * p_node)
{
	uint16_t lid_ho;
	uint8_t max_ports;
	uint8_t port_num;
	osm_physp_t *p_physp;
	cache_switch_t *p_cache_sw;

	OSM_LOG_ENTER(p_mgr->p_log);

	if (!p_mgr->cache_valid)
		goto Exit;

	if (osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH) {

		lid_ho = cl_ntoh16(osm_node_get_base_lid(p_node, 0));

		if (!lid_ho) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_VERBOSE,
				"Skip caching. Switch dropped before "
				"it gets a valid lid.\n");
			osm_ucast_cache_invalidate(p_mgr);
			goto Exit;
		}

		OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
			"Caching dropped switch lid %u\n", lid_ho);

		if (!p_node->sw) {
			/* something is wrong - forget about cache */
			OSM_LOG(p_mgr->p_log, OSM_LOG_ERROR,
				"ERR AD03: no switch info for node lid %u - "
				"clearing cache\n", lid_ho);
			osm_ucast_cache_invalidate(p_mgr);
			goto Exit;
		}

		/* unlink (add to cache) all the ports of this switch */
		max_ports = osm_node_get_num_physp(p_node);
		for (port_num = 1; port_num < max_ports; port_num++) {

			p_physp = osm_node_get_physp_ptr(p_node, port_num);
			if (!p_physp || !p_physp->p_remote_physp)
				continue;

			osm_ucast_cache_add_link(p_mgr, p_physp,
						 p_physp->p_remote_physp);
		}

		/*
		 * All the ports have been dropped (cached).
		 * If one of the ports was connected to CA/RTR,
		 * then the cached switch would be marked as leaf.
		 * If it isn't, then the dropped switch isn't a leaf,
		 * and cache can't handle it.
		 */

		p_cache_sw = cache_get_sw(p_mgr, lid_ho);

		/* p_cache_sw could be NULL if it has no remote phys ports */
		if (!p_cache_sw || !cache_sw_is_leaf(p_cache_sw)) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"Dropped non-leaf switch (lid %u)\n", lid_ho);
			osm_ucast_cache_invalidate(p_mgr);
			goto Exit;
		}

		p_cache_sw->dropped = TRUE;

		if (!p_node->sw->num_hops || !p_node->sw->hops) {
			OSM_LOG(p_mgr->p_log, OSM_LOG_DEBUG,
				"No LID matrices for switch lid %u\n", lid_ho);
			osm_ucast_cache_invalidate(p_mgr);
			goto Exit;
		}

		/* lid matrices */

		p_cache_sw->num_hops = p_node->sw->num_hops;
		p_node->sw->num_hops = 0;
		p_cache_sw->hops = p_node->sw->hops;
		p_node->sw->hops = NULL;

		/* linear forwarding table */

		if (p_node->sw->new_lft) {
			/* LFT buffer exists - we use it, because
			   it is more updated than the switch's LFT */
			p_cache_sw->lft = p_node->sw->new_lft;
			p_node->sw->new_lft = NULL;
		} else {
			/* no LFT buffer, so we use the switch's LFT */
			p_cache_sw->lft = p_node->sw->lft;
			p_node->sw->lft = NULL;
			p_node->sw->lft_size = 0;
		}
		p_cache_sw->max_lid_ho = p_node->sw->max_lid_ho;
	} else {
		/* dropping CA/RTR: add to cache all the ports of this node */
		max_ports = osm_node_get_num_physp(p_node);
		for (port_num = 1; port_num < max_ports; port_num++) {

			p_physp = osm_node_get_physp_ptr(p_node, port_num);
			if (!p_physp || !p_physp->p_remote_physp)
				continue;

			CL_ASSERT(osm_node_get_type
				  (p_physp->p_remote_physp->p_node) ==
				  IB_NODE_TYPE_SWITCH);

			osm_ucast_cache_add_link(p_mgr,
						 p_physp->p_remote_physp,
						 p_physp);
		}
	}
Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
}				/* osm_ucast_cache_add_node() */

int osm_ucast_cache_process(osm_ucast_mgr_t * p_mgr)
{
	cl_qmap_t *tbl = &p_mgr->p_subn->sw_guid_tbl;
	cl_map_item_t *item;
	osm_switch_t *p_sw;
	uint16_t lft_size;

	if (!p_mgr->p_subn->opt.use_ucast_cache)
		return 1;

	ucast_cache_validate(p_mgr);
	if (!p_mgr->cache_valid)
		return 1;

	OSM_LOG(p_mgr->p_log, OSM_LOG_INFO,
		"Configuring switch tables using cached routing\n");

	for (item = cl_qmap_head(tbl); item != cl_qmap_end(tbl);
	     item = cl_qmap_next(item)) {
		p_sw = (osm_switch_t *) item;
		CL_ASSERT(p_sw->new_lft);
		if (!p_sw->lft) {
			lft_size = (p_sw->max_lid_ho / IB_SMP_DATA_SIZE + 1)
				   * IB_SMP_DATA_SIZE;
			p_sw->lft = malloc(lft_size);
			if (!p_sw->lft)
				return IB_INSUFFICIENT_MEMORY;
			p_sw->lft_size = lft_size;
			memset(p_sw->lft, OSM_NO_PATH, p_sw->lft_size);
		}

	}

	osm_ucast_mgr_set_fwd_tables(p_mgr);

	return 0;
}
