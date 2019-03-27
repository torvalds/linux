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
 *    Implementation of osm_node_t.
 * This object represents an Infiniband Node.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <iba/ib_types.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_NODE_C
#include <opensm/osm_node.h>
#include <opensm/osm_madw.h>

void osm_node_init_physp(IN osm_node_t * p_node, uint8_t port_num,
			 IN const osm_madw_t * p_madw)
{
	ib_net64_t port_guid;
	ib_smp_t *p_smp;
	ib_node_info_t *p_ni;

	p_smp = osm_madw_get_smp_ptr(p_madw);

	p_ni = ib_smp_get_payload_ptr(p_smp);
	port_guid = p_ni->port_guid;

	CL_ASSERT(port_num < p_node->physp_tbl_size);

	osm_physp_init(&p_node->physp_table[port_num],
		       port_guid, port_num, p_node,
		       osm_madw_get_bind_handle(p_madw),
		       p_smp->hop_count, p_smp->initial_path);
}

osm_node_t *osm_node_new(IN const osm_madw_t * p_madw)
{
	osm_node_t *p_node;
	ib_smp_t *p_smp;
	ib_node_info_t *p_ni;
	uint8_t i;
	uint32_t size;

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_ni = ib_smp_get_payload_ptr(p_smp);

	/*
	   The node object already contains one physical port object.
	   Therefore, subtract 1 from the number of physical ports
	   used by the switch.  This is not done for CA's since they
	   need to occupy 1 more physp than they physically have since
	   we still reserve room for a "port 0".
	 */
	size = p_ni->num_ports;

	p_node = malloc(sizeof(*p_node) + sizeof(osm_physp_t) * size);
	if (!p_node)
		return NULL;

	memset(p_node, 0, sizeof(*p_node) + sizeof(osm_physp_t) * size);
	p_node->node_info = *p_ni;
	p_node->physp_tbl_size = size + 1;

	p_node->physp_discovered = malloc(sizeof(uint8_t) * p_node->physp_tbl_size);
	if (!p_node->physp_discovered) {
		free(p_node);
		return NULL;
	}
	memset(p_node->physp_discovered, 0, sizeof(uint8_t) * p_node->physp_tbl_size);
	/*
	   Construct Physical Port objects owned by this Node.
	   Then, initialize the Physical Port through with we
	   discovered this port.
	   For switches, all ports have the same GUID.
	   For CAs and routers, each port has a different GUID, so we only
	   know the GUID for the port that responded to our
	   Get(NodeInfo).
	 */
	for (i = 0; i < p_node->physp_tbl_size; i++)
		osm_physp_construct(&p_node->physp_table[i]);

	if (p_ni->node_type == IB_NODE_TYPE_SWITCH)
		for (i = 0; i <= p_ni->num_ports; i++)
			osm_node_init_physp(p_node, i, p_madw);
	else
		osm_node_init_physp(p_node,
				    ib_node_info_get_local_port_num(p_ni),
				    p_madw);
	p_node->print_desc = strdup(OSM_NODE_DESC_UNKNOWN);

	return p_node;
}

static void node_destroy(IN osm_node_t * p_node)
{
	uint16_t i;

	/*
	   Cleanup all physports
	 */
	for (i = 0; i < p_node->physp_tbl_size; i++)
		osm_physp_destroy(&p_node->physp_table[i]);

	/* cleanup printable node_desc field */
	if (p_node->print_desc)
		free(p_node->print_desc);

	/* cleanup physp_discovered array */
	free(p_node->physp_discovered);
}

void osm_node_delete(IN OUT osm_node_t ** p_node)
{
	CL_ASSERT(p_node && *p_node);
	node_destroy(*p_node);
	free(*p_node);
	*p_node = NULL;
}

void osm_node_link(IN osm_node_t * p_node, IN uint8_t port_num,
		   IN osm_node_t * p_remote_node, IN uint8_t remote_port_num)
{
	osm_physp_t *p_physp;
	osm_physp_t *p_remote_physp;

	p_physp = osm_node_get_physp_ptr(p_node, port_num);
	p_remote_physp = osm_node_get_physp_ptr(p_remote_node, remote_port_num);

	if (p_physp->p_remote_physp)
		p_physp->p_remote_physp->p_remote_physp = NULL;
	if (p_remote_physp->p_remote_physp)
		p_remote_physp->p_remote_physp->p_remote_physp = NULL;

	osm_physp_link(p_physp, p_remote_physp);
}

void osm_node_unlink(IN osm_node_t * p_node, IN uint8_t port_num,
		     IN osm_node_t * p_remote_node, IN uint8_t remote_port_num)
{
	osm_physp_t *p_physp;
	osm_physp_t *p_remote_physp;

	CL_ASSERT(port_num < p_node->physp_tbl_size);
	CL_ASSERT(remote_port_num < p_remote_node->physp_tbl_size);

	if (osm_node_link_exists(p_node, port_num,
				 p_remote_node, remote_port_num)) {

		p_physp = osm_node_get_physp_ptr(p_node, port_num);
		p_remote_physp =
		    osm_node_get_physp_ptr(p_remote_node, remote_port_num);

		osm_physp_unlink(p_physp, p_remote_physp);
	}
}

boolean_t osm_node_link_exists(IN osm_node_t * p_node, IN uint8_t port_num,
			       IN osm_node_t * p_remote_node,
			       IN uint8_t remote_port_num)
{
	osm_physp_t *p_physp;
	osm_physp_t *p_remote_physp;

	CL_ASSERT(port_num < p_node->physp_tbl_size);
	CL_ASSERT(remote_port_num < p_remote_node->physp_tbl_size);

	p_physp = osm_node_get_physp_ptr(p_node, port_num);
	p_remote_physp = osm_node_get_physp_ptr(p_remote_node, remote_port_num);

	return osm_physp_link_exists(p_physp, p_remote_physp);
}

boolean_t osm_node_link_has_valid_ports(IN osm_node_t * p_node,
					IN uint8_t port_num,
					IN osm_node_t * p_remote_node,
					IN uint8_t remote_port_num)
{
	osm_physp_t *p_physp;
	osm_physp_t *p_remote_physp;

	CL_ASSERT(port_num < p_node->physp_tbl_size);
	CL_ASSERT(remote_port_num < p_remote_node->physp_tbl_size);

	p_physp = osm_node_get_physp_ptr(p_node, port_num);
	p_remote_physp = osm_node_get_physp_ptr(p_remote_node, remote_port_num);

	return (p_physp && p_remote_physp);
}

boolean_t osm_node_has_any_link(IN osm_node_t * p_node, IN uint8_t port_num)
{
	osm_physp_t *p_physp;
	CL_ASSERT(port_num < p_node->physp_tbl_size);
	p_physp = osm_node_get_physp_ptr(p_node, port_num);
	return osm_physp_has_any_link(p_physp);
}

osm_node_t *osm_node_get_remote_node(IN osm_node_t * p_node,
				     IN uint8_t port_num,
				     OUT uint8_t * p_remote_port_num)
{
	osm_physp_t *p_physp;
	osm_physp_t *p_remote_physp;

	p_physp = osm_node_get_physp_ptr(p_node, port_num);

	if (!p_physp || !osm_physp_has_any_link(p_physp))
		return NULL;

	p_remote_physp = osm_physp_get_remote(p_physp);
	if (p_remote_port_num)
		*p_remote_port_num = osm_physp_get_port_num(p_remote_physp);

	return osm_physp_get_node_ptr(p_remote_physp);
}

/**********************************************************************
 The lock must be held before calling this function.
**********************************************************************/
ib_net16_t osm_node_get_remote_base_lid(IN osm_node_t * p_node,
					IN uint32_t port_num)
{
	osm_physp_t *p_physp;
	osm_physp_t *p_remote_physp;
	CL_ASSERT(port_num < p_node->physp_tbl_size);

	p_physp = osm_node_get_physp_ptr(p_node, port_num);
	if (p_physp) {
		p_remote_physp = osm_physp_get_remote(p_physp);
		return osm_physp_get_base_lid(p_remote_physp);
	}

	return 0;
}
