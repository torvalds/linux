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
 *    Implementation of osm_physp_t.
 * This object represents an Infiniband Port.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <complib/cl_debug.h>
#include <iba/ib_types.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_PORT_C
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_db_pack.h>
#include <opensm/osm_sm.h>

void osm_physp_construct(IN osm_physp_t * p_physp)
{
	memset(p_physp, 0, sizeof(*p_physp));
	osm_dr_path_construct(&p_physp->dr_path);
	cl_ptr_vector_construct(&p_physp->slvl_by_port);
	osm_pkey_tbl_construct(&p_physp->pkeys);
}

void osm_physp_destroy(IN osm_physp_t * p_physp)
{
	size_t num_slvl, i;

	/* the physp might be uninitialized */
	if (p_physp->port_guid) {
		if (p_physp->p_guids)
			free(p_physp->p_guids);

		/* free the SL2VL Tables */
		num_slvl = cl_ptr_vector_get_size(&p_physp->slvl_by_port);
		for (i = 0; i < num_slvl; i++)
			free(cl_ptr_vector_get(&p_physp->slvl_by_port, i));
		cl_ptr_vector_destroy(&p_physp->slvl_by_port);

		/* free the P_Key Tables */
		osm_pkey_tbl_destroy(&p_physp->pkeys);

		memset(p_physp, 0, sizeof(*p_physp));
		osm_dr_path_construct(&p_physp->dr_path);	/* clear dr_path */
	}
}

void osm_physp_init(IN osm_physp_t * p_physp, IN ib_net64_t port_guid,
		    IN uint8_t port_num, IN const struct osm_node *p_node,
		    IN osm_bind_handle_t h_bind, IN uint8_t hop_count,
		    IN const uint8_t * p_initial_path)
{
	uint16_t num_slvl, i;
	ib_slvl_table_t *p_slvl;

	CL_ASSERT(p_node);

	osm_physp_construct(p_physp);
	p_physp->port_guid = port_guid;
	p_physp->port_num = port_num;
	p_physp->healthy = TRUE;
	p_physp->need_update = 2;
	p_physp->p_node = (struct osm_node *)p_node;

	osm_dr_path_init(&p_physp->dr_path, hop_count, p_initial_path);

	/* allocate enough SL2VL tables */
	if (osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH)
		/* we need node num ports + 1 SL2VL tables */
		num_slvl = osm_node_get_num_physp(p_node) + 1;
	else
		/* An end node - we need only one SL2VL */
		num_slvl = 1;

	cl_ptr_vector_init(&p_physp->slvl_by_port, num_slvl, 1);
	for (i = 0; i < num_slvl; i++) {
		p_slvl = (ib_slvl_table_t *) malloc(sizeof(ib_slvl_table_t));
		if (!p_slvl)
			break;
		memset(p_slvl, 0, sizeof(ib_slvl_table_t));
		cl_ptr_vector_set(&p_physp->slvl_by_port, i, p_slvl);
	}

	/* initialize the pkey table */
	osm_pkey_tbl_init(&p_physp->pkeys);
}

void osm_port_delete(IN OUT osm_port_t ** pp_port)
{
	free(*pp_port);
	*pp_port = NULL;
}

osm_port_t *osm_port_new(IN const ib_node_info_t * p_ni,
			 IN osm_node_t * p_parent_node)
{
	osm_port_t *p_port;
	ib_net64_t port_guid;
	osm_physp_t *p_physp;
	uint8_t port_num;

	p_port = malloc(sizeof(*p_port));
	if (!p_port)
		return NULL;

	memset(p_port, 0, sizeof(*p_port));
	cl_qlist_init(&p_port->mcm_list);
	p_port->p_node = (struct osm_node *)p_parent_node;
	port_guid = p_ni->port_guid;
	p_port->guid = port_guid;
	port_num = p_ni->node_type == IB_NODE_TYPE_SWITCH ?
	    0 : ib_node_info_get_local_port_num(p_ni);

	/*
	   Get the pointers to the physical node objects "owned" by this
	   logical port GUID.
	   For switches, port '0' is owned; for HCA's and routers,
	   only the singular part that has this GUID is owned.
	 */
	p_physp = osm_node_get_physp_ptr(p_parent_node, port_num);
	if (!p_physp)
		return NULL;

	CL_ASSERT(port_guid == osm_physp_get_port_guid(p_physp));
	p_port->p_physp = p_physp;

	return p_port;
}

void osm_port_get_lid_range_ho(IN const osm_port_t * p_port,
			       IN uint16_t * p_min_lid, IN uint16_t * p_max_lid)
{
	uint8_t lmc;

	*p_min_lid = cl_ntoh16(osm_port_get_base_lid(p_port));
	lmc = osm_port_get_lmc(p_port);
	*p_max_lid = (uint16_t) (*p_min_lid + (1 << lmc) - 1);
}

uint8_t osm_physp_calc_link_mtu(IN osm_log_t * p_log,
				IN const osm_physp_t * p_physp,
				IN uint8_t current_mtu)
{
	const osm_physp_t *p_remote_physp;
	uint8_t mtu;
	uint8_t remote_mtu;

	OSM_LOG_ENTER(p_log);

	p_remote_physp = osm_physp_get_remote(p_physp);
	if (p_remote_physp) {
		/* use the available MTU */
		mtu = ib_port_info_get_mtu_cap(&p_physp->port_info);

		remote_mtu =
		    ib_port_info_get_mtu_cap(&p_remote_physp->port_info);

		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"Remote port 0x%016" PRIx64 " port = %u : "
			"MTU = %u. This Port MTU: %u\n",
			cl_ntoh64(osm_physp_get_port_guid(p_remote_physp)),
			osm_physp_get_port_num(p_remote_physp),
			remote_mtu, mtu);

		if (mtu != remote_mtu) {
			if (mtu > remote_mtu)
				mtu = remote_mtu;
			if (mtu != current_mtu)
				OSM_LOG(p_log, OSM_LOG_VERBOSE,
					"MTU mismatch between ports."
					"\n\t\t\t\tPort 0x%016" PRIx64 ", port %u"
					" and port 0x%016" PRIx64 ", port %u."
					"\n\t\t\t\tUsing lower MTU of %u\n",
					cl_ntoh64(osm_physp_get_port_guid(p_physp)),
					osm_physp_get_port_num(p_physp),
					cl_ntoh64(osm_physp_get_port_guid
						  (p_remote_physp)),
					osm_physp_get_port_num(p_remote_physp), mtu);
		}
	} else
		mtu = ib_port_info_get_neighbor_mtu(&p_physp->port_info);

	if (mtu == 0) {
		OSM_LOG(p_log, OSM_LOG_DEBUG, "ERR 4101: "
			"Invalid MTU = 0. Forcing correction to 256\n");
		mtu = 1;
	}

	OSM_LOG_EXIT(p_log);
	return mtu;
}

uint8_t osm_physp_calc_link_op_vls(IN osm_log_t * p_log,
				   IN const osm_subn_t * p_subn,
				   IN const osm_physp_t * p_physp,
				   IN uint8_t current_op_vls)
{
	const osm_physp_t *p_remote_physp;
	uint8_t op_vls;
	uint8_t remote_op_vls;

	OSM_LOG_ENTER(p_log);

	p_remote_physp = osm_physp_get_remote(p_physp);
	if (p_remote_physp) {
		/* use the available VLCap */
		op_vls = ib_port_info_get_vl_cap(&p_physp->port_info);

		remote_op_vls =
		    ib_port_info_get_vl_cap(&p_remote_physp->port_info);

		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"Remote port 0x%016" PRIx64 " port = 0x%X : "
			"VL_CAP = %u. This port VL_CAP = %u\n",
			cl_ntoh64(osm_physp_get_port_guid(p_remote_physp)),
			osm_physp_get_port_num(p_remote_physp),
			remote_op_vls, op_vls);

		if (op_vls != remote_op_vls) {
			if (op_vls > remote_op_vls)
				op_vls = remote_op_vls;
			if (op_vls != current_op_vls)
				OSM_LOG(p_log, OSM_LOG_VERBOSE,
					"OP_VLS mismatch between ports."
					"\n\t\t\t\tPort 0x%016" PRIx64 ", port 0x%X"
					" and port 0x%016" PRIx64 ", port 0x%X."
					"\n\t\t\t\tUsing lower OP_VLS of %u\n",
					cl_ntoh64(osm_physp_get_port_guid(p_physp)),
					osm_physp_get_port_num(p_physp),
					cl_ntoh64(osm_physp_get_port_guid
						  (p_remote_physp)),
					osm_physp_get_port_num(p_remote_physp), op_vls);
		}
	} else
		op_vls = ib_port_info_get_op_vls(&p_physp->port_info);

	if (op_vls == 0) {
		/* for non compliant implementations */
		OSM_LOG(p_log, OSM_LOG_VERBOSE,
			"Invalid OP_VLS = 0. Forcing correction to 1 (VL0)\n");
		op_vls = 1;
	}

	/* support user limitation of max_op_vls */
	if (op_vls > p_subn->opt.max_op_vls)
		op_vls = p_subn->opt.max_op_vls;

	OSM_LOG_EXIT(p_log);
	return op_vls;
}

static inline uint64_t ptr_to_key(void const *p)
{
	uint64_t k = 0;

	memcpy(&k, p, sizeof(void *));
	return k;
}

static inline void *key_to_ptr(uint64_t k)
{
	void *p = 0;

	memcpy(&p, &k, sizeof(void *));
	return p;
}

/**********************************************************************
 Traverse the fabric from the SM node following the DR path given and
 add every phys port traversed to the map. Avoid tracking the first and
 last phys ports (going into the first switch and into the target port).
 **********************************************************************/
static cl_status_t physp_get_dr_physp_set(IN osm_log_t * p_log,
					  IN osm_subn_t const *p_subn,
					  IN osm_dr_path_t const *p_path,
					  OUT cl_map_t * p_physp_map)
{
	osm_port_t *p_port;
	osm_physp_t *p_physp;
	osm_node_t *p_node;
	uint8_t hop;
	cl_status_t status = CL_SUCCESS;

	OSM_LOG_ENTER(p_log);

	/* find the OSM node */
	p_port = osm_get_port_by_guid(p_subn, p_subn->sm_port_guid);
	if (!p_port) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 4103: "
			"Failed to find the SM own port by guid\n");
		status = CL_ERROR;
		goto Exit;
	}

	/* get the node of the SM */
	p_node = p_port->p_node;

	/*
	   traverse the path adding the nodes to the table
	   start after the first dummy hop and stop just before the
	   last one
	 */
	for (hop = 1; hop < p_path->hop_count - 1; hop++) {
		/* go out using the phys port of the path */
		p_physp = osm_node_get_physp_ptr(p_node, p_path->path[hop]);

		/* make sure we got a valid port and it has a remote port */
		if (!p_physp) {
			OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 4104: "
				"DR Traversal stopped on invalid port at hop:%u\n",
				hop);
			status = CL_ERROR;
			goto Exit;
		}

		/* we track the ports we go out along the path */
		if (hop > 1)
			cl_map_insert(p_physp_map, ptr_to_key(p_physp), NULL);

		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"Traversed through node: 0x%016" PRIx64
			" port:%u\n",
			cl_ntoh64(p_node->node_info.node_guid),
			p_path->path[hop]);

		if (!(p_physp = osm_physp_get_remote(p_physp))) {
			OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 4106: "
				"DR Traversal stopped on missing remote physp at hop:%u\n",
				hop);
			status = CL_ERROR;
			goto Exit;
		}

		p_node = osm_physp_get_node_ptr(p_physp);
	}

Exit:
	OSM_LOG_EXIT(p_log);
	return status;
}

static void physp_update_new_dr_path(IN osm_physp_t const *p_dest_physp,
				     IN cl_map_t * p_visited_map,
				     IN osm_bind_handle_t * h_bind)
{
	cl_list_t tmpPortsList;
	osm_physp_t *p_physp, *p_src_physp = NULL;
	uint8_t path_array[IB_SUBNET_PATH_HOPS_MAX];
	uint8_t i = 0;
	osm_dr_path_t *p_dr_path;

	cl_list_construct(&tmpPortsList);
	cl_list_init(&tmpPortsList, 10);

	cl_list_insert_head(&tmpPortsList, p_dest_physp);
	/* get the output port where we need to come from */
	p_physp = (osm_physp_t *) cl_map_get(p_visited_map,
					     ptr_to_key(p_dest_physp));
	while (p_physp != NULL) {
		cl_list_insert_head(&tmpPortsList, p_physp);
		/* get the input port through where we reached the output port */
		p_src_physp = p_physp;
		p_physp = (osm_physp_t *) cl_map_get(p_visited_map,
						     ptr_to_key(p_physp));
		/* if we reached a null p_physp - this means we are at the begining
		   of the path. Break. */
		if (p_physp == NULL)
			break;
		/* get the output port */
		p_physp = (osm_physp_t *) cl_map_get(p_visited_map,
						     ptr_to_key(p_physp));
	}

	memset(path_array, 0, sizeof(path_array));
	p_physp = (osm_physp_t *) cl_list_remove_head(&tmpPortsList);
	while (p_physp != NULL) {
		i++;
		path_array[i] = p_physp->port_num;
		p_physp = (osm_physp_t *) cl_list_remove_head(&tmpPortsList);
	}
	if (p_src_physp) {
		p_dr_path = osm_physp_get_dr_path_ptr(p_src_physp);
		osm_dr_path_init(p_dr_path, i, path_array);
	}

	cl_list_destroy(&tmpPortsList);
}

void osm_physp_replace_dr_path_with_alternate_dr_path(IN osm_log_t * p_log,
						      IN osm_subn_t const
						      *p_subn, IN osm_physp_t const
						      *p_dest_physp,
						      IN osm_bind_handle_t *
						      h_bind)
{
	cl_map_t physp_map;
	cl_map_t visited_map;
	osm_dr_path_t *p_dr_path;
	cl_list_t *p_currPortsList;
	cl_list_t *p_nextPortsList;
	osm_port_t *p_port;
	osm_physp_t *p_physp, *p_remote_physp;
	ib_net64_t port_guid;
	boolean_t next_list_is_full = TRUE, reached_dest = FALSE;
	uint8_t num_ports, port_num;

	p_nextPortsList = (cl_list_t *) malloc(sizeof(cl_list_t));
	if (!p_nextPortsList)
		return;

	/*
	   initialize the map of all port participating in current dr path
	   not including first and last switches
	 */
	cl_map_construct(&physp_map);
	cl_map_init(&physp_map, 4);
	cl_map_construct(&visited_map);
	cl_map_init(&visited_map, 4);
	p_dr_path = osm_physp_get_dr_path_ptr(p_dest_physp);
	physp_get_dr_physp_set(p_log, p_subn, p_dr_path, &physp_map);

	/*
	   BFS from OSM port until we find the target physp but avoid
	   going through mapped ports
	 */
	cl_list_construct(p_nextPortsList);
	cl_list_init(p_nextPortsList, 10);

	port_guid = p_subn->sm_port_guid;

	CL_ASSERT(port_guid);

	p_port = osm_get_port_by_guid(p_subn, port_guid);
	if (!p_port) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 4105: No SM port object\n");
		goto Exit;
	}

	/*
	   HACK: We are assuming SM is running on HCA, so when getting the default
	   port we'll get the port connected to the rest of the subnet. If SM is
	   running on SWITCH - we should try to get a dr path from all switch ports.
	 */
	p_physp = p_port->p_physp;

	CL_ASSERT(p_physp);

	cl_list_insert_tail(p_nextPortsList, p_physp);

	while (next_list_is_full == TRUE) {
		next_list_is_full = FALSE;
		p_currPortsList = p_nextPortsList;
		p_nextPortsList = (cl_list_t *) malloc(sizeof(cl_list_t));
		if (!p_nextPortsList) {
			p_nextPortsList = p_currPortsList;
			goto Exit;
		}
		cl_list_construct(p_nextPortsList);
		cl_list_init(p_nextPortsList, 10);
		p_physp = (osm_physp_t *) cl_list_remove_head(p_currPortsList);
		while (p_physp != NULL) {
			/* If we are in a switch - need to go out through all
			   the other physical ports of the switch */
			num_ports = osm_node_get_num_physp(p_physp->p_node);

			for (port_num = 1; port_num < num_ports; port_num++) {
				if (osm_node_get_type(p_physp->p_node) ==
				    IB_NODE_TYPE_SWITCH)
					p_remote_physp =
					    osm_node_get_physp_ptr(p_physp->
								   p_node,
								   port_num);
				else
					/* this is HCA or router - the remote port is just the port connected
					   on the other side */
					p_remote_physp =
					    p_physp->p_remote_physp;

				/*
				   make sure that all of the following occurred:
				   1. The port isn't NULL
				   2. This is not the port we came from
				   3. The port is not in the physp_map
				   4. This port haven't been visited before
				 */
				if (p_remote_physp &&
				    p_remote_physp != p_physp &&
				    cl_map_get(&physp_map,
					       ptr_to_key(p_remote_physp))
				    == NULL
				    && cl_map_get(&visited_map,
						  ptr_to_key
						  (p_remote_physp)) == NULL) {
					/* Insert the port into the visited_map, and save its source port */
					cl_map_insert(&visited_map,
						      ptr_to_key
						      (p_remote_physp),
						      p_physp);

					/* Is this the p_dest_physp? */
					if (p_remote_physp == p_dest_physp) {
						/* update the new dr path */
						physp_update_new_dr_path
						    (p_dest_physp, &visited_map,
						     h_bind);
						reached_dest = TRUE;
						break;
					}

					/* add the p_remote_physp to the nextPortsList */
					cl_list_insert_tail(p_nextPortsList,
							    p_remote_physp);
					next_list_is_full = TRUE;
				}
			}

			p_physp = (osm_physp_t *)
			    cl_list_remove_head(p_currPortsList);
			if (reached_dest == TRUE) {
				/* free the rest of the currPortsList */
				while (p_physp != NULL)
					p_physp = (osm_physp_t *)
					    cl_list_remove_head
					    (p_currPortsList);
				/* free the nextPortsList, if items were added to it */
				p_physp = (osm_physp_t *)
				    cl_list_remove_head(p_nextPortsList);
				while (p_physp != NULL)
					p_physp = (osm_physp_t *)
					    cl_list_remove_head
					    (p_nextPortsList);
				next_list_is_full = FALSE;
			}
		}
		cl_list_destroy(p_currPortsList);
		free(p_currPortsList);
	}

	/* cleanup */
Exit:
	cl_list_destroy(p_nextPortsList);
	free(p_nextPortsList);
	cl_map_destroy(&physp_map);
	cl_map_destroy(&visited_map);
}

boolean_t osm_link_is_healthy(IN const osm_physp_t * p_physp)
{
	osm_physp_t *p_remote_physp;

	CL_ASSERT(p_physp);
	p_remote_physp = p_physp->p_remote_physp;
	if (p_remote_physp != NULL)
		return ((p_physp->healthy) & (p_remote_physp->healthy));
	/* the other side is not known - consider the link as healthy */
	return TRUE;
}

void osm_physp_set_pkey_tbl(IN osm_log_t * p_log, IN const osm_subn_t * p_subn,
			    IN osm_physp_t * p_physp,
			    IN ib_pkey_table_t * p_pkey_tbl,
			    IN uint16_t block_num,
			    IN boolean_t is_set)
{
	uint16_t max_blocks;

	CL_ASSERT(p_pkey_tbl);
	/*
	   (14.2.5.7) - the block number valid values are 0-2047, and are
	   further limited by the size of the P_Key table specified by
	   the PartitionCap on the node.
	 */
	if (!p_physp->p_node->sw || p_physp->port_num == 0)
		/*
		   The maximum blocks is defined in the node info: partition cap
		   for CA, router, and switch management ports.
		 */
		max_blocks =
		    (cl_ntoh16(p_physp->p_node->node_info.partition_cap) +
		     IB_NUM_PKEY_ELEMENTS_IN_BLOCK - 1)
		    / IB_NUM_PKEY_ELEMENTS_IN_BLOCK;
	else
		/*
		   This is a switch, and not a management port. The maximum
		   blocks is defined in the switch info: partition enforcement
		   cap.
		 */
		max_blocks =
		    (cl_ntoh16(p_physp->p_node->sw->switch_info.enforce_cap) +
		     IB_NUM_PKEY_ELEMENTS_IN_BLOCK -
		     1) / IB_NUM_PKEY_ELEMENTS_IN_BLOCK;

	if (block_num >= max_blocks) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 4108: "
			"Got illegal update for block number:%u max:%u "
			"for GUID: %" PRIx64 " port number:%u\n",
			block_num, max_blocks,
			cl_ntoh64(p_physp->p_node->node_info.node_guid),
			p_physp->port_num);
		return;
	}

	/* decrement block received counter */
	if(!is_set)
		p_physp->pkeys.rcv_blocks_cnt--;
	osm_pkey_tbl_set(&p_physp->pkeys, block_num, p_pkey_tbl,
			 p_subn->opt.allow_both_pkeys);
}

osm_alias_guid_t *osm_alias_guid_new(IN const ib_net64_t alias_guid,
				     IN osm_port_t *p_base_port)
{
	osm_alias_guid_t *p_alias_guid;

	p_alias_guid = calloc(1, sizeof(*p_alias_guid));
	if (p_alias_guid) {
		p_alias_guid->alias_guid = alias_guid;
		p_alias_guid->p_base_port = p_base_port;
	}
	return p_alias_guid;
}

void osm_alias_guid_delete(IN OUT osm_alias_guid_t ** pp_alias_guid)
{
	free(*pp_alias_guid);
	*pp_alias_guid = NULL;
}

void osm_physp_set_port_info(IN osm_physp_t * p_physp,
					   IN const ib_port_info_t * p_pi,
					   IN const struct osm_sm * p_sm)
{
	CL_ASSERT(p_pi);
	CL_ASSERT(osm_physp_is_valid(p_physp));

	if (ib_port_info_get_port_state(p_pi) == IB_LINK_DOWN) {
		/* If PortState is down, only copy PortState */
		/* and PortPhysicalState per C14-24-2.1 */
		ib_port_info_set_port_state(&p_physp->port_info, IB_LINK_DOWN);
		ib_port_info_set_port_phys_state
		    (ib_port_info_get_port_phys_state(p_pi),
		     &p_physp->port_info);
	} else {
		p_physp->port_info = *p_pi;

		/* The MKey in p_pi can only be considered valid if it's
		 * for a HCA/router or switch port 0, and it's either
		 * non-zero or the MKeyProtect bits are also zero.
		 */
		if ((osm_node_get_type(p_physp->p_node) !=
		     IB_NODE_TYPE_SWITCH || p_physp->port_num == 0) &&
		    (p_pi->m_key != 0 || ib_port_info_get_mpb(p_pi) == 0))
			osm_db_guid2mkey_set(p_sm->p_subn->p_g2m,
					     cl_ntoh64(p_physp->port_guid),
					     cl_ntoh64(p_pi->m_key));
	}
}
