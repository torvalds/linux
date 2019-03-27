/*
 * Copyright (c) 2009 Simula Research Laboratory. All rights reserved.
 * Copyright (c) 2009 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2011 Mellanox Technologies LTD. All rights reserved.
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
 *    Implementation of OpenSM FatTree routing
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
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_UCAST_FTREE_C
#include <opensm/osm_opensm.h>
#include <opensm/osm_switch.h>

/*
 * FatTree rank is bounded between 2 and 8:
 *  - Tree of rank 1 has only trivial routing paths,
 *    so no need to use FatTree routing.
 *  - Why maximum rank is 8:
 *    Each node (switch) is assigned a unique tuple.
 *    Switches are stored in two cl_qmaps - one is
 *    ordered by guid, and the other by a key that is
 *    generated from tuple. Since cl_qmap supports only
 *    a 64-bit key, the maximal tuple length is 8 bytes.
 *    which means that maximal tree rank is 8.
 * Note that the above also implies that each switch
 * can have at max 255 up/down ports.
 */

#define FAT_TREE_MIN_RANK 2
#define FAT_TREE_MAX_RANK 8

typedef enum {
	FTREE_DIRECTION_DOWN = -1,
	FTREE_DIRECTION_SAME,
	FTREE_DIRECTION_UP
} ftree_direction_t;

/***************************************************
 **
 **  Forward references
 **
 ***************************************************/
struct ftree_sw_t_;
struct ftree_hca_t_;
struct ftree_port_t_;
struct ftree_port_group_t_;
struct ftree_fabric_t_;

/***************************************************
 **
 **  ftree_tuple_t definition
 **
 ***************************************************/

#define FTREE_TUPLE_BUFF_LEN 1024
#define FTREE_TUPLE_LEN 8

typedef uint8_t ftree_tuple_t[FTREE_TUPLE_LEN];
typedef uint64_t ftree_tuple_key_t;

/***************************************************
 **
 **  ftree_sw_table_element_t definition
 **
 ***************************************************/

typedef struct {
	cl_map_item_t map_item;
	struct ftree_sw_t_ *p_sw;
} ftree_sw_tbl_element_t;

/***************************************************
 **
 **  ftree_port_t definition
 **
 ***************************************************/

typedef struct ftree_port_t_ {
	cl_map_item_t map_item;
	uint8_t port_num;	/* port number on the current node */
	uint8_t remote_port_num;	/* port number on the remote node */
	uint32_t counter_up;	/* number of allocated routes upwards */
	uint32_t counter_down;	/* number of allocated routes downwards */
} ftree_port_t;

/***************************************************
 **
 **  ftree_port_group_t definition
 **
 ***************************************************/

typedef union ftree_hca_or_sw_ {
	struct ftree_hca_t_ *p_hca;
	struct ftree_sw_t_ *p_sw;
} ftree_hca_or_sw;

typedef struct ftree_port_group_t_ {
	cl_map_item_t map_item;
	uint16_t lid;	/* lid of the current node */
	uint16_t remote_lid;	/* lid of the remote node */
	ib_net64_t port_guid;	/* port guid of this port */
	ib_net64_t node_guid;	/* this node's guid */
	uint8_t node_type;	/* this node's type */
	ib_net64_t remote_port_guid;	/* port guid of the remote port */
	ib_net64_t remote_node_guid;	/* node guid of the remote node */
	uint8_t remote_node_type;	/* IB_NODE_TYPE_{CA,SWITCH,ROUTER,...} */
	ftree_hca_or_sw hca_or_sw;	/* pointer to this hca/switch */
	ftree_hca_or_sw remote_hca_or_sw;	/* pointer to remote hca/switch */
	cl_ptr_vector_t ports;	/* vector of ports to the same lid */
	boolean_t is_cn;	/* whether this port is a compute node */
	boolean_t is_io;	/* whether this port is an I/O node */
	uint32_t counter_down;	/* number of allocated routes downwards */
	uint32_t counter_up;	/* number of allocated routes upwards */
} ftree_port_group_t;

/***************************************************
 **
 **  ftree_sw_t definition
 **
 ***************************************************/

typedef struct ftree_sw_t_ {
	cl_map_item_t map_item;
	osm_switch_t *p_osm_sw;
	uint32_t rank;
	ftree_tuple_t tuple;
	uint16_t lid;
	ftree_port_group_t **down_port_groups;
	uint8_t down_port_groups_num;
	ftree_port_group_t **sibling_port_groups;
	uint8_t sibling_port_groups_num;
	ftree_port_group_t **up_port_groups;
	uint8_t up_port_groups_num;
	boolean_t is_leaf;
	unsigned down_port_groups_idx;
	uint8_t *hops;
	uint32_t min_counter_down;
	boolean_t counter_up_changed;
} ftree_sw_t;

/***************************************************
 **
 **  ftree_hca_t definition
 **
 ***************************************************/

typedef struct ftree_hca_t_ {
	cl_map_item_t map_item;
	osm_node_t *p_osm_node;
	ftree_port_group_t **up_port_groups;
	uint8_t *disconnected_ports;
	uint16_t up_port_groups_num;
	unsigned cn_num;
} ftree_hca_t;

/***************************************************
 **
 **  ftree_fabric_t definition
 **
 ***************************************************/

typedef struct ftree_fabric_t_ {
	osm_opensm_t *p_osm;
	osm_subn_t *p_subn;
	cl_qmap_t hca_tbl;
	cl_qmap_t sw_tbl;
	cl_qmap_t sw_by_tuple_tbl;
	cl_qmap_t cn_guid_tbl;
	cl_qmap_t io_guid_tbl;
	unsigned cn_num;
	unsigned ca_ports;
	uint8_t leaf_switch_rank;
	uint8_t max_switch_rank;
	ftree_sw_t **leaf_switches;
	uint32_t leaf_switches_num;
	uint16_t max_cn_per_leaf;
	uint16_t lft_max_lid;
	boolean_t fabric_built;
} ftree_fabric_t;

static inline osm_subn_t *ftree_get_subnet(IN ftree_fabric_t * p_ftree)
{
	return p_ftree->p_subn;
}

/***************************************************
 **
 ** comparators
 **
 ***************************************************/

static int compare_switches_by_index(IN const void *p1, IN const void *p2)
{
	ftree_sw_t **pp_sw1 = (ftree_sw_t **) p1;
	ftree_sw_t **pp_sw2 = (ftree_sw_t **) p2;

	uint16_t i;
	for (i = 0; i < FTREE_TUPLE_LEN; i++) {
		if ((*pp_sw1)->tuple[i] > (*pp_sw2)->tuple[i])
			return 1;
		if ((*pp_sw1)->tuple[i] < (*pp_sw2)->tuple[i])
			return -1;
	}
	return 0;
}

/***************************************************/

static int
compare_port_groups_by_remote_switch_index(IN const void *p1, IN const void *p2)
{
	ftree_port_group_t **pp_g1 = (ftree_port_group_t **) p1;
	ftree_port_group_t **pp_g2 = (ftree_port_group_t **) p2;

	return
	    compare_switches_by_index(&((*pp_g1)->remote_hca_or_sw.p_sw),
				      &((*pp_g2)->remote_hca_or_sw.p_sw));
}

/***************************************************
 **
 ** ftree_tuple_t functions
 **
 ***************************************************/

static void tuple_init(IN ftree_tuple_t tuple)
{
	memset(tuple, 0xFF, FTREE_TUPLE_LEN);
}

/***************************************************/

static inline boolean_t tuple_assigned(IN ftree_tuple_t tuple)
{
	return (tuple[0] != 0xFF);
}

/***************************************************/

#define FTREE_TUPLE_BUFFERS_NUM 6

static const char *tuple_to_str(IN ftree_tuple_t tuple)
{
	static char buffer[FTREE_TUPLE_BUFFERS_NUM][FTREE_TUPLE_BUFF_LEN];
	static uint8_t ind = 0;
	char *ret_buffer;
	uint32_t i;

	if (!tuple_assigned(tuple))
		return "INDEX.NOT.ASSIGNED";

	buffer[ind][0] = '\0';

	for (i = 0; (i < FTREE_TUPLE_LEN) && (tuple[i] != 0xFF); i++) {
		if ((strlen(buffer[ind]) + 10) > FTREE_TUPLE_BUFF_LEN)
			return "INDEX.TOO.LONG";
		if (i != 0)
			strcat(buffer[ind], ".");
		sprintf(&buffer[ind][strlen(buffer[ind])], "%u", tuple[i]);
	}

	ret_buffer = buffer[ind];
	ind = (ind + 1) % FTREE_TUPLE_BUFFERS_NUM;
	return ret_buffer;
}				/* tuple_to_str() */

/***************************************************/

static inline ftree_tuple_key_t tuple_to_key(IN ftree_tuple_t tuple)
{
	ftree_tuple_key_t key;
	memcpy(&key, tuple, FTREE_TUPLE_LEN);
	return key;
}

/***************************************************/

static inline void tuple_from_key(IN ftree_tuple_t tuple,
				  IN ftree_tuple_key_t key)
{
	memcpy(tuple, &key, FTREE_TUPLE_LEN);
}

/***************************************************
 **
 ** ftree_sw_tbl_element_t functions
 **
 ***************************************************/

static ftree_sw_tbl_element_t *sw_tbl_element_create(IN ftree_sw_t * p_sw)
{
	ftree_sw_tbl_element_t *p_element =
	    (ftree_sw_tbl_element_t *) malloc(sizeof(ftree_sw_tbl_element_t));
	if (!p_element)
		return NULL;
	memset(p_element, 0, sizeof(ftree_sw_tbl_element_t));

	p_element->p_sw = p_sw;
	return p_element;
}

/***************************************************/

static void sw_tbl_element_destroy(IN ftree_sw_tbl_element_t * p_element)
{
	free(p_element);
}

/***************************************************
 **
 ** ftree_port_t functions
 **
 ***************************************************/

static ftree_port_t *port_create(IN uint8_t port_num,
				 IN uint8_t remote_port_num)
{
	ftree_port_t *p_port = (ftree_port_t *) malloc(sizeof(ftree_port_t));
	if (!p_port)
		return NULL;
	memset(p_port, 0, sizeof(ftree_port_t));

	p_port->port_num = port_num;
	p_port->remote_port_num = remote_port_num;

	return p_port;
}

/***************************************************/

static void port_destroy(IN ftree_port_t * p_port)
{
	free(p_port);
}

/***************************************************
 **
 ** ftree_port_group_t functions
 **
 ***************************************************/

static ftree_port_group_t *port_group_create(IN uint16_t lid,
					     IN uint16_t remote_lid,
					     IN ib_net64_t port_guid,
					     IN ib_net64_t node_guid,
					     IN uint8_t node_type,
					     IN void *p_hca_or_sw,
					     IN ib_net64_t remote_port_guid,
					     IN ib_net64_t remote_node_guid,
					     IN uint8_t remote_node_type,
					     IN void *p_remote_hca_or_sw,
					     IN boolean_t is_cn,
					     IN boolean_t is_io)
{
	ftree_port_group_t *p_group =
	    (ftree_port_group_t *) malloc(sizeof(ftree_port_group_t));
	if (p_group == NULL)
		return NULL;
	memset(p_group, 0, sizeof(ftree_port_group_t));

	p_group->lid = lid;
	p_group->remote_lid = remote_lid;
	memcpy(&p_group->port_guid, &port_guid, sizeof(ib_net64_t));
	memcpy(&p_group->node_guid, &node_guid, sizeof(ib_net64_t));
	memcpy(&p_group->remote_port_guid, &remote_port_guid,
	       sizeof(ib_net64_t));
	memcpy(&p_group->remote_node_guid, &remote_node_guid,
	       sizeof(ib_net64_t));

	p_group->node_type = node_type;
	switch (node_type) {
	case IB_NODE_TYPE_CA:
		p_group->hca_or_sw.p_hca = (ftree_hca_t *) p_hca_or_sw;
		break;
	case IB_NODE_TYPE_SWITCH:
		p_group->hca_or_sw.p_sw = (ftree_sw_t *) p_hca_or_sw;
		break;
	default:
		/* we shouldn't get here - port is created only in hca or switch */
		CL_ASSERT(0);
	}

	p_group->remote_node_type = remote_node_type;
	switch (remote_node_type) {
	case IB_NODE_TYPE_CA:
		p_group->remote_hca_or_sw.p_hca =
		    (ftree_hca_t *) p_remote_hca_or_sw;
		break;
	case IB_NODE_TYPE_SWITCH:
		p_group->remote_hca_or_sw.p_sw =
		    (ftree_sw_t *) p_remote_hca_or_sw;
		break;
	default:
		/* we shouldn't get here - port is created only in hca or switch */
		CL_ASSERT(0);
	}

	cl_ptr_vector_init(&p_group->ports, 0,	/* min size */
			   8);	/* grow size */
	p_group->is_cn = is_cn;
	p_group->is_io = is_io;
	return p_group;
}				/* port_group_create() */

/***************************************************/

static void port_group_destroy(IN ftree_port_group_t * p_group)
{
	uint32_t i;
	uint32_t size;
	ftree_port_t *p_port;

	if (!p_group)
		return;

	/* remove all the elements of p_group->ports vector */
	size = cl_ptr_vector_get_size(&p_group->ports);
	for (i = 0; i < size; i++)
		if (cl_ptr_vector_at(&p_group->ports, i, (void *)&p_port) == CL_SUCCESS)
			port_destroy(p_port);

	cl_ptr_vector_destroy(&p_group->ports);
	free(p_group);
}				/* port_group_destroy() */

/***************************************************/

static void port_group_dump(IN ftree_fabric_t * p_ftree,
			    IN ftree_port_group_t * p_group,
			    IN ftree_direction_t direction)
{
	ftree_port_t *p_port;
	uint32_t size;
	uint32_t i;
	char *buff;

	if (!p_group)
		return;

	if (!OSM_LOG_IS_ACTIVE_V2(&p_ftree->p_osm->log, OSM_LOG_DEBUG))
		return;

	size = cl_ptr_vector_get_size(&p_group->ports);

	buff = calloc(10, 1024);
	if (!buff) {
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR, "ERR AB33: "
			"Failed to allocate buffer\n");
		return;
	}

	for (i = 0; i < size; i++) {
		cl_ptr_vector_at(&p_group->ports, i, (void *)&p_port);
		CL_ASSERT(p_port);

		if (i != 0)
			strcat(buff, ", ");
		sprintf(buff + strlen(buff), "%u", p_port->port_num);
	}

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
		"    Port Group of size %u, port(s): %s, direction: %s\n"
		"                  Local <--> Remote GUID (LID):"
		"0x%016" PRIx64 " (0x%04x) <--> 0x%016" PRIx64 " (0x%04x)\n",
		size, buff,
		(direction == FTREE_DIRECTION_DOWN) ? "DOWN" : (direction ==
								FTREE_DIRECTION_SAME)
		? "SIBLING" : "UP", cl_ntoh64(p_group->port_guid),
		p_group->lid, cl_ntoh64(p_group->remote_port_guid),
		p_group->remote_lid);

	free(buff);

}				/* port_group_dump() */

/***************************************************/

static void port_group_add_port(IN ftree_port_group_t * p_group,
				IN uint8_t port_num, IN uint8_t remote_port_num)
{
	uint16_t i;
	ftree_port_t *p_port;

	for (i = 0; i < cl_ptr_vector_get_size(&p_group->ports); i++) {
		cl_ptr_vector_at(&p_group->ports, i, (void *)&p_port);
		if (p_port->port_num == port_num)
			return;
	}

	p_port = port_create(port_num, remote_port_num);
	CL_ASSERT(p_port);
	cl_ptr_vector_insert(&p_group->ports, p_port, NULL);
}

/***************************************************
 **
 ** ftree_sw_t functions
 **
 ***************************************************/

static ftree_sw_t *sw_create(IN osm_switch_t * p_osm_sw)
{
	ftree_sw_t *p_sw;
	uint8_t ports_num;

	/* make sure that the switch has ports */
	if (p_osm_sw->num_ports == 1)
		return NULL;

	p_sw = (ftree_sw_t *) malloc(sizeof(ftree_sw_t));
	if (p_sw == NULL)
		return NULL;
	memset(p_sw, 0, sizeof(ftree_sw_t));

	p_sw->p_osm_sw = p_osm_sw;
	p_sw->rank = 0xFFFFFFFF;
	tuple_init(p_sw->tuple);

	p_sw->lid =
	    cl_ntoh16(osm_node_get_base_lid(p_sw->p_osm_sw->p_node, 0));

	ports_num = osm_node_get_num_physp(p_sw->p_osm_sw->p_node);
	p_sw->down_port_groups =
	    (ftree_port_group_t **) malloc(ports_num *
					   sizeof(ftree_port_group_t *));
	if (p_sw->down_port_groups == NULL)
		goto FREE_P_SW;
	memset(p_sw->down_port_groups, 0, ports_num * sizeof(ftree_port_group_t *));

	p_sw->up_port_groups =
	    (ftree_port_group_t **) malloc(ports_num *
					   sizeof(ftree_port_group_t *));
	if (p_sw->up_port_groups == NULL)
		goto FREE_DOWN;
	memset(p_sw->up_port_groups, 0, ports_num * sizeof(ftree_port_group_t *));

	p_sw->sibling_port_groups =
	    (ftree_port_group_t **) malloc(ports_num *
					   sizeof(ftree_port_group_t *));
	if (p_sw->sibling_port_groups == NULL)
		goto FREE_UP;
	memset(p_sw->sibling_port_groups, 0, ports_num * sizeof(ftree_port_group_t *));

	/* initialize lft buffer */
	memset(p_osm_sw->new_lft, OSM_NO_PATH, p_osm_sw->lft_size);
	p_sw->hops = malloc((p_osm_sw->max_lid_ho + 1) * sizeof(*(p_sw->hops)));
	if (p_sw->hops == NULL)
		goto FREE_SIBLING;

	memset(p_sw->hops, OSM_NO_PATH, p_osm_sw->max_lid_ho + 1);

	return p_sw;

FREE_SIBLING:
	free(p_sw->sibling_port_groups);
FREE_UP:
	free(p_sw->up_port_groups);
FREE_DOWN:
	free(p_sw->down_port_groups);
FREE_P_SW:
	free(p_sw);
	return NULL;
}				/* sw_create() */

/***************************************************/

static void sw_destroy(IN ftree_sw_t * p_sw)
{
	uint8_t i;

	if (!p_sw)
		return;
	free(p_sw->hops);

	for (i = 0; i < p_sw->down_port_groups_num; i++)
		port_group_destroy(p_sw->down_port_groups[i]);
	for (i = 0; i < p_sw->sibling_port_groups_num; i++)
		port_group_destroy(p_sw->sibling_port_groups[i]);
	for (i = 0; i < p_sw->up_port_groups_num; i++)
		port_group_destroy(p_sw->up_port_groups[i]);
	free(p_sw->down_port_groups);
	free(p_sw->sibling_port_groups);
	free(p_sw->up_port_groups);

	free(p_sw);
}				/* sw_destroy() */

/***************************************************/

static uint64_t sw_get_guid_no(IN ftree_sw_t * p_sw)
{
	if (!p_sw)
		return 0;
	return osm_node_get_node_guid(p_sw->p_osm_sw->p_node);
}

/***************************************************/

static uint64_t sw_get_guid_ho(IN ftree_sw_t * p_sw)
{
	return cl_ntoh64(sw_get_guid_no(p_sw));
}

/***************************************************/

static void sw_dump(IN ftree_fabric_t * p_ftree, IN ftree_sw_t * p_sw)
{
	uint32_t i;

	if (!p_sw)
		return;

	if (!OSM_LOG_IS_ACTIVE_V2(&p_ftree->p_osm->log, OSM_LOG_DEBUG))
		return;

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
		"Switch index: %s, GUID: 0x%016" PRIx64
		", Ports: %u DOWN, %u SIBLINGS, %u UP\n",
		tuple_to_str(p_sw->tuple), sw_get_guid_ho(p_sw),
		p_sw->down_port_groups_num, p_sw->sibling_port_groups_num,
		p_sw->up_port_groups_num);

	for (i = 0; i < p_sw->down_port_groups_num; i++)
		port_group_dump(p_ftree, p_sw->down_port_groups[i],
				FTREE_DIRECTION_DOWN);
	for (i = 0; i < p_sw->sibling_port_groups_num; i++)
		port_group_dump(p_ftree, p_sw->sibling_port_groups[i],
				FTREE_DIRECTION_SAME);
	for (i = 0; i < p_sw->up_port_groups_num; i++)
		port_group_dump(p_ftree, p_sw->up_port_groups[i],
				FTREE_DIRECTION_UP);

}				/* sw_dump() */

/***************************************************/

static boolean_t sw_ranked(IN ftree_sw_t * p_sw)
{
	return (p_sw->rank != 0xFFFFFFFF);
}

/***************************************************/

static ftree_port_group_t *sw_get_port_group_by_remote_lid(IN ftree_sw_t * p_sw,
							   IN uint16_t
							   remote_lid,
							   IN ftree_direction_t
							   direction)
{
	uint32_t i;
	uint32_t size;
	ftree_port_group_t **port_groups;

	if (direction == FTREE_DIRECTION_UP) {
		port_groups = p_sw->up_port_groups;
		size = p_sw->up_port_groups_num;
	} else if (direction == FTREE_DIRECTION_SAME) {
		port_groups = p_sw->sibling_port_groups;
		size = p_sw->sibling_port_groups_num;
	} else {
		port_groups = p_sw->down_port_groups;
		size = p_sw->down_port_groups_num;
	}

	for (i = 0; i < size; i++)
		if (remote_lid == port_groups[i]->remote_lid)
			return port_groups[i];

	return NULL;
}				/* sw_get_port_group_by_remote_lid() */

/***************************************************/

static void sw_add_port(IN ftree_sw_t * p_sw, IN uint8_t port_num,
			IN uint8_t remote_port_num, IN uint16_t lid,
			IN uint16_t remote_lid, IN ib_net64_t port_guid,
			IN ib_net64_t remote_port_guid,
			IN ib_net64_t remote_node_guid,
			IN uint8_t remote_node_type,
			IN void *p_remote_hca_or_sw,
			IN ftree_direction_t direction)
{
	ftree_port_group_t *p_group =
	    sw_get_port_group_by_remote_lid(p_sw, remote_lid, direction);

	if (!p_group) {
		p_group = port_group_create(lid, remote_lid,
					    port_guid, sw_get_guid_no(p_sw),
					    IB_NODE_TYPE_SWITCH, p_sw,
					    remote_port_guid, remote_node_guid,
					    remote_node_type,
					    p_remote_hca_or_sw, FALSE, FALSE);
		CL_ASSERT(p_group);

		if (direction == FTREE_DIRECTION_UP) {
			p_sw->up_port_groups[p_sw->up_port_groups_num++] =
			    p_group;
		} else if (direction == FTREE_DIRECTION_SAME) {
			p_sw->
			    sibling_port_groups[p_sw->sibling_port_groups_num++]
			    = p_group;
		} else
			p_sw->down_port_groups[p_sw->down_port_groups_num++] =
			    p_group;
	}
	port_group_add_port(p_group, port_num, remote_port_num);

}				/* sw_add_port() */

/***************************************************/

static inline cl_status_t sw_set_hops(IN ftree_sw_t * p_sw, IN uint16_t lid,
				      IN uint8_t port_num, IN uint8_t hops,
				      IN boolean_t is_target_sw)
{
	/* set local min hop table(LID) */
	p_sw->hops[lid] = hops;
	if (is_target_sw)
		return osm_switch_set_hops(p_sw->p_osm_sw, lid, port_num, hops);
	return 0;
}

/***************************************************/

static int set_hops_on_remote_sw(IN ftree_port_group_t * p_group,
				 IN uint16_t target_lid, IN uint8_t hops,
				 IN boolean_t is_target_sw)
{
	ftree_port_t *p_port;
	uint8_t i, ports_num;
	ftree_sw_t *p_remote_sw = p_group->remote_hca_or_sw.p_sw;

	/* if lid is a switch, we set the min hop table in the osm_switch struct */
	CL_ASSERT(p_group->remote_node_type == IB_NODE_TYPE_SWITCH);
	p_remote_sw->hops[target_lid] = hops;

	/* If target lid is a switch we set the min hop table values
	 * for each port on the associated osm_sw struct */
	if (!is_target_sw)
		return 0;

	ports_num = (uint8_t) cl_ptr_vector_get_size(&p_group->ports);
	for (i = 0; i < ports_num; i++) {
		cl_ptr_vector_at(&p_group->ports, i, (void *)&p_port);
		if (sw_set_hops(p_remote_sw, target_lid,
				p_port->remote_port_num, hops, is_target_sw))
			return -1;
	}
	return 0;
}

/***************************************************/

static inline uint8_t
sw_get_least_hops(IN ftree_sw_t * p_sw, IN uint16_t target_lid)
{
	CL_ASSERT(p_sw->hops != NULL);
	return p_sw->hops[target_lid];
}

/***************************************************
 **
 ** ftree_hca_t functions
 **
 ***************************************************/

static ftree_hca_t *hca_create(IN osm_node_t * p_osm_node)
{
	ftree_hca_t *p_hca = (ftree_hca_t *) malloc(sizeof(ftree_hca_t));
	if (p_hca == NULL)
		return NULL;
	memset(p_hca, 0, sizeof(ftree_hca_t));

	p_hca->p_osm_node = p_osm_node;
	p_hca->up_port_groups = (ftree_port_group_t **)
	    malloc(osm_node_get_num_physp(p_hca->p_osm_node) *
		   sizeof(ftree_port_group_t *));
	if (!p_hca->up_port_groups) {
		free(p_hca);
		return NULL;
	}
	memset(p_hca->up_port_groups, 0, osm_node_get_num_physp(p_hca->p_osm_node) *
	       sizeof(ftree_port_group_t *));

	p_hca->disconnected_ports = (uint8_t *)
	    calloc(osm_node_get_num_physp(p_hca->p_osm_node) + 1, sizeof(uint8_t));
	if (!p_hca->disconnected_ports) {
		free(p_hca->up_port_groups);
		free(p_hca);
		return NULL;
	}
	p_hca->up_port_groups_num = 0;
	return p_hca;
}

/***************************************************/

static void hca_destroy(IN ftree_hca_t * p_hca)
{
	uint32_t i;

	if (!p_hca)
		return;

	for (i = 0; i < p_hca->up_port_groups_num; i++)
		port_group_destroy(p_hca->up_port_groups[i]);

	free(p_hca->up_port_groups);
	free(p_hca->disconnected_ports);

	free(p_hca);
}

/***************************************************/

static uint64_t hca_get_guid_no(IN ftree_hca_t * p_hca)
{
	if (!p_hca)
		return 0;
	return osm_node_get_node_guid(p_hca->p_osm_node);
}

/***************************************************/

static uint64_t hca_get_guid_ho(IN ftree_hca_t * p_hca)
{
	return cl_ntoh64(hca_get_guid_no(p_hca));
}

/***************************************************/

static void hca_dump(IN ftree_fabric_t * p_ftree, IN ftree_hca_t * p_hca)
{
	uint32_t i;

	if (!p_hca)
		return;

	if (!OSM_LOG_IS_ACTIVE_V2(&p_ftree->p_osm->log, OSM_LOG_DEBUG))
		return;

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
		"CA GUID: 0x%016" PRIx64 ", Ports: %u UP\n",
		hca_get_guid_ho(p_hca), p_hca->up_port_groups_num);

	for (i = 0; i < p_hca->up_port_groups_num; i++)
		port_group_dump(p_ftree, p_hca->up_port_groups[i],
				FTREE_DIRECTION_UP);
}

static ftree_port_group_t *hca_get_port_group_by_lid(IN ftree_hca_t *
						     p_hca,
						     IN uint16_t
						     lid)
{
	uint32_t i;
	for (i = 0; i < p_hca->up_port_groups_num; i++)
		if (lid ==
		    p_hca->up_port_groups[i]->lid)
			return p_hca->up_port_groups[i];

	return NULL;
}
/***************************************************/

static void hca_add_port(IN ftree_fabric_t * p_ftree,
			 IN ftree_hca_t * p_hca, IN uint8_t port_num,
			 IN uint8_t remote_port_num, IN uint16_t lid,
			 IN uint16_t remote_lid, IN ib_net64_t port_guid,
			 IN ib_net64_t remote_port_guid,
			 IN ib_net64_t remote_node_guid,
			 IN uint8_t remote_node_type,
			 IN void *p_remote_hca_or_sw, IN boolean_t is_cn,
			 IN boolean_t is_io)
{
	ftree_port_group_t *p_group;

	/* this function is supposed to be called only for adding ports
	   in hca's that lead to switches */
	CL_ASSERT(remote_node_type == IB_NODE_TYPE_SWITCH);

	p_group = hca_get_port_group_by_lid(p_hca, lid);

	if (!p_group) {
		p_group = port_group_create(lid, remote_lid,
					    port_guid, hca_get_guid_no(p_hca),
					    IB_NODE_TYPE_CA, p_hca,
					    remote_port_guid, remote_node_guid,
					    remote_node_type,
					    p_remote_hca_or_sw, is_cn, is_io);
		CL_ASSERT(p_group);
		p_hca->up_port_groups[p_hca->up_port_groups_num++] = p_group;
		port_group_add_port(p_group, port_num, remote_port_num);
	} else
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
			"ERR AB32: Duplicated LID for CA GUID: 0x%016" PRIx64 "\n",
			cl_ntoh64(port_guid));
}				/* hca_add_port() */

/***************************************************
 **
 ** ftree_fabric_t functions
 **
 ***************************************************/

static ftree_fabric_t *fabric_create()
{
	ftree_fabric_t *p_ftree =
	    (ftree_fabric_t *) malloc(sizeof(ftree_fabric_t));
	if (p_ftree == NULL)
		return NULL;

	memset(p_ftree, 0, sizeof(ftree_fabric_t));

	cl_qmap_init(&p_ftree->hca_tbl);
	cl_qmap_init(&p_ftree->sw_tbl);
	cl_qmap_init(&p_ftree->sw_by_tuple_tbl);
	cl_qmap_init(&p_ftree->cn_guid_tbl);
	cl_qmap_init(&p_ftree->io_guid_tbl);

	return p_ftree;
}

/***************************************************/

static void fabric_clear(ftree_fabric_t * p_ftree)
{
	ftree_hca_t *p_hca;
	ftree_hca_t *p_next_hca;
	ftree_sw_t *p_sw;
	ftree_sw_t *p_next_sw;
	ftree_sw_tbl_element_t *p_element;
	ftree_sw_tbl_element_t *p_next_element;
	name_map_item_t *p_guid_element, *p_next_guid_element;

	if (!p_ftree)
		return;

	/* remove all the elements of hca_tbl */

	p_next_hca = (ftree_hca_t *) cl_qmap_head(&p_ftree->hca_tbl);
	while (p_next_hca != (ftree_hca_t *) cl_qmap_end(&p_ftree->hca_tbl)) {
		p_hca = p_next_hca;
		p_next_hca = (ftree_hca_t *) cl_qmap_next(&p_hca->map_item);
		hca_destroy(p_hca);
	}
	cl_qmap_remove_all(&p_ftree->hca_tbl);

	/* remove all the elements of sw_tbl */

	p_next_sw = (ftree_sw_t *) cl_qmap_head(&p_ftree->sw_tbl);
	while (p_next_sw != (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (ftree_sw_t *) cl_qmap_next(&p_sw->map_item);
		sw_destroy(p_sw);
	}
	cl_qmap_remove_all(&p_ftree->sw_tbl);

	/* remove all the elements of sw_by_tuple_tbl */

	p_next_element =
	    (ftree_sw_tbl_element_t *) cl_qmap_head(&p_ftree->sw_by_tuple_tbl);
	while (p_next_element != (ftree_sw_tbl_element_t *)
	       cl_qmap_end(&p_ftree->sw_by_tuple_tbl)) {
		p_element = p_next_element;
		p_next_element = (ftree_sw_tbl_element_t *)
		    cl_qmap_next(&p_element->map_item);
		sw_tbl_element_destroy(p_element);
	}
	cl_qmap_remove_all(&p_ftree->sw_by_tuple_tbl);

	/* remove all the elements of cn_guid_tbl */
	p_next_guid_element =
	    (name_map_item_t *) cl_qmap_head(&p_ftree->cn_guid_tbl);
	while (p_next_guid_element !=
	       (name_map_item_t *) cl_qmap_end(&p_ftree->cn_guid_tbl)) {
		p_guid_element = p_next_guid_element;
		p_next_guid_element =
		    (name_map_item_t *) cl_qmap_next(&p_guid_element->item);
		free(p_guid_element);
	}
	cl_qmap_remove_all(&p_ftree->cn_guid_tbl);

	/* remove all the elements of io_guid_tbl */
	p_next_guid_element =
	    (name_map_item_t *) cl_qmap_head(&p_ftree->io_guid_tbl);
	while (p_next_guid_element !=
	       (name_map_item_t *) cl_qmap_end(&p_ftree->io_guid_tbl)) {
		p_guid_element = p_next_guid_element;
		p_next_guid_element =
		    (name_map_item_t *) cl_qmap_next(&p_guid_element->item);
		free(p_guid_element);
	}
	cl_qmap_remove_all(&p_ftree->io_guid_tbl);

	/* free the leaf switches array */
	if ((p_ftree->leaf_switches_num > 0) && (p_ftree->leaf_switches))
		free(p_ftree->leaf_switches);

	p_ftree->leaf_switches_num = 0;
	p_ftree->cn_num = 0;
	p_ftree->ca_ports = 0;
	p_ftree->leaf_switch_rank = 0;
	p_ftree->max_switch_rank = 0;
	p_ftree->max_cn_per_leaf = 0;
	p_ftree->lft_max_lid = 0;
	p_ftree->leaf_switches = NULL;
	p_ftree->fabric_built = FALSE;

}				/* fabric_destroy() */

/***************************************************/

static void fabric_destroy(ftree_fabric_t * p_ftree)
{
	if (!p_ftree)
		return;
	fabric_clear(p_ftree);
	free(p_ftree);
}

/***************************************************/

static uint8_t fabric_get_rank(ftree_fabric_t * p_ftree)
{
	return p_ftree->leaf_switch_rank + 1;
}

/***************************************************/

static void fabric_add_hca(ftree_fabric_t * p_ftree, osm_node_t * p_osm_node)
{
	ftree_hca_t *p_hca;

	CL_ASSERT(osm_node_get_type(p_osm_node) == IB_NODE_TYPE_CA);

	p_hca = hca_create(p_osm_node);
	if (!p_hca)
		return;

	cl_qmap_insert(&p_ftree->hca_tbl, p_osm_node->node_info.node_guid,
		       &p_hca->map_item);
}

/***************************************************/

static void fabric_add_sw(ftree_fabric_t * p_ftree, osm_switch_t * p_osm_sw)
{
	ftree_sw_t *p_sw;

	CL_ASSERT(osm_node_get_type(p_osm_sw->p_node) == IB_NODE_TYPE_SWITCH);

	p_sw = sw_create(p_osm_sw);
	if (!p_sw)
		return;

	cl_qmap_insert(&p_ftree->sw_tbl, p_osm_sw->p_node->node_info.node_guid,
		       &p_sw->map_item);

	/* track the max lid (in host order) that exists in the fabric */
	if (p_sw->lid > p_ftree->lft_max_lid)
		p_ftree->lft_max_lid = p_sw->lid;
}

/***************************************************/

static void fabric_add_sw_by_tuple(IN ftree_fabric_t * p_ftree,
				   IN ftree_sw_t * p_sw)
{
	CL_ASSERT(tuple_assigned(p_sw->tuple));

	cl_qmap_insert(&p_ftree->sw_by_tuple_tbl, tuple_to_key(p_sw->tuple),
		       &sw_tbl_element_create(p_sw)->map_item);
}

/***************************************************/

static ftree_sw_t *fabric_get_sw_by_tuple(IN ftree_fabric_t * p_ftree,
					  IN ftree_tuple_t tuple)
{
	ftree_sw_tbl_element_t *p_element;

	CL_ASSERT(tuple_assigned(tuple));

	tuple_to_key(tuple);

	p_element =
	    (ftree_sw_tbl_element_t *) cl_qmap_get(&p_ftree->sw_by_tuple_tbl,
						   tuple_to_key(tuple));
	if (p_element ==
	    (ftree_sw_tbl_element_t *) cl_qmap_end(&p_ftree->sw_by_tuple_tbl))
		return NULL;

	return p_element->p_sw;
}

/***************************************************/

static ftree_sw_t *fabric_get_sw_by_guid(IN ftree_fabric_t * p_ftree,
					 IN uint64_t guid)
{
	ftree_sw_t *p_sw;
	p_sw = (ftree_sw_t *) cl_qmap_get(&p_ftree->sw_tbl, guid);
	if (p_sw == (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl))
		return NULL;
	return p_sw;
}

/***************************************************/

static ftree_hca_t *fabric_get_hca_by_guid(IN ftree_fabric_t * p_ftree,
					   IN uint64_t guid)
{
	ftree_hca_t *p_hca;
	p_hca = (ftree_hca_t *) cl_qmap_get(&p_ftree->hca_tbl, guid);
	if (p_hca == (ftree_hca_t *) cl_qmap_end(&p_ftree->hca_tbl))
		return NULL;
	return p_hca;
}

/***************************************************/

static void fabric_dump(ftree_fabric_t * p_ftree)
{
	uint32_t i;
	ftree_hca_t *p_hca;
	ftree_sw_t *p_sw;

	if (!OSM_LOG_IS_ACTIVE_V2(&p_ftree->p_osm->log, OSM_LOG_DEBUG))
		return;

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG, "\n"
		"                       |-------------------------------|\n"
		"                       |-  Full fabric topology dump  -|\n"
		"                       |-------------------------------|\n\n");

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG, "-- CAs:\n");

	for (p_hca = (ftree_hca_t *) cl_qmap_head(&p_ftree->hca_tbl);
	     p_hca != (ftree_hca_t *) cl_qmap_end(&p_ftree->hca_tbl);
	     p_hca = (ftree_hca_t *) cl_qmap_next(&p_hca->map_item)) {
		hca_dump(p_ftree, p_hca);
	}

	for (i = 0; i <= p_ftree->max_switch_rank; i++) {
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
			"-- Rank %u switches\n", i);
		for (p_sw = (ftree_sw_t *) cl_qmap_head(&p_ftree->sw_tbl);
		     p_sw != (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl);
		     p_sw = (ftree_sw_t *) cl_qmap_next(&p_sw->map_item)) {
			if (p_sw->rank == i)
				sw_dump(p_ftree, p_sw);
		}
	}

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG, "\n"
		"                       |---------------------------------------|\n"
		"                       |- Full fabric topology dump completed -|\n"
		"                       |---------------------------------------|\n\n");
}				/* fabric_dump() */

/***************************************************/

static void fabric_dump_general_info(IN ftree_fabric_t * p_ftree)
{
	uint32_t i, j;
	ftree_sw_t *p_sw;

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_INFO,
		"General fabric topology info\n");
	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_INFO,
		"============================\n");

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_INFO,
		"  - FatTree rank (roots to leaf switches): %u\n",
		p_ftree->leaf_switch_rank + 1);
	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_INFO,
		"  - FatTree max switch rank: %u\n", p_ftree->max_switch_rank);
	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_INFO,
		"  - Fabric has %u CAs, %u CA ports (%u of them CNs), %u switches\n",
		cl_qmap_count(&p_ftree->hca_tbl), p_ftree->ca_ports,
		p_ftree->cn_num, cl_qmap_count(&p_ftree->sw_tbl));

	CL_ASSERT(p_ftree->ca_ports >= p_ftree->cn_num);

	for (i = 0; i <= p_ftree->max_switch_rank; i++) {
		j = 0;
		for (p_sw = (ftree_sw_t *) cl_qmap_head(&p_ftree->sw_tbl);
		     p_sw != (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl);
		     p_sw = (ftree_sw_t *) cl_qmap_next(&p_sw->map_item)) {
			if (p_sw->rank == i)
				j++;
		}
		if (i == 0)
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_INFO,
				"  - Fabric has %u switches at rank %u (roots)\n",
				j, i);
		else if (i == p_ftree->leaf_switch_rank)
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_INFO,
				"  - Fabric has %u switches at rank %u (%u of them leafs)\n",
				j, i, p_ftree->leaf_switches_num);
		else
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_INFO,
				"  - Fabric has %u switches at rank %u\n", j,
				i);
	}

	if (OSM_LOG_IS_ACTIVE_V2(&p_ftree->p_osm->log, OSM_LOG_VERBOSE)) {
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
			"  - Root switches:\n");
		for (p_sw = (ftree_sw_t *) cl_qmap_head(&p_ftree->sw_tbl);
		     p_sw != (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl);
		     p_sw = (ftree_sw_t *) cl_qmap_next(&p_sw->map_item)) {
			if (p_sw->rank == 0)
				OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
					"      GUID: 0x%016" PRIx64
					", LID: %u, Index %s\n",
					sw_get_guid_ho(p_sw),
					p_sw->lid,
					tuple_to_str(p_sw->tuple));
		}

		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
			"  - Leaf switches (sorted by index):\n");
		for (i = 0; i < p_ftree->leaf_switches_num; i++) {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
				"      GUID: 0x%016" PRIx64
				", LID: %u, Index %s\n",
				sw_get_guid_ho(p_ftree->leaf_switches[i]),
				p_ftree->leaf_switches[i]->lid,
				tuple_to_str(p_ftree->leaf_switches[i]->tuple));
		}
	}
}				/* fabric_dump_general_info() */

/***************************************************/

static void fabric_dump_hca_ordering(IN ftree_fabric_t * p_ftree)
{
	ftree_hca_t *p_hca;
	ftree_sw_t *p_sw;
	ftree_port_group_t *p_group_on_sw;
	ftree_port_group_t *p_group_on_hca;
	int rename_status = 0;
	uint32_t i;
	uint32_t j;
	unsigned printed_hcas_on_leaf;

	char path[1024], path_tmp[1032];
	FILE *p_hca_ordering_file;
	const char *filename = "opensm-ftree-ca-order.dump";

	snprintf(path, sizeof(path), "%s/%s",
		 p_ftree->p_osm->subn.opt.dump_files_dir, filename);

	snprintf(path_tmp, sizeof(path_tmp), "%s.tmp", path);

	p_hca_ordering_file = fopen(path_tmp, "w");
	if (!p_hca_ordering_file) {
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR, "ERR AB01: "
			"cannot open file \'%s\': %s\n", path_tmp,
			strerror(errno));
		return;
	}

	/* for each leaf switch (in indexing order) */
	for (i = 0; i < p_ftree->leaf_switches_num; i++) {
		p_sw = p_ftree->leaf_switches[i];
		printed_hcas_on_leaf = 0;

		/* for each real CA (CNs and not) connected to this switch */
		for (j = 0; j < p_sw->down_port_groups_num; j++) {
			p_group_on_sw = p_sw->down_port_groups[j];

			if (p_group_on_sw->remote_node_type != IB_NODE_TYPE_CA)
				continue;

			p_hca = p_group_on_sw->remote_hca_or_sw.p_hca;
			p_group_on_hca =
			    hca_get_port_group_by_lid(p_hca,
						      p_group_on_sw->
						      remote_lid);

			/* treat non-compute nodes as dummies */
			if (!p_group_on_hca->is_cn)
				continue;

			fprintf(p_hca_ordering_file, "0x%04x\t%s\n",
				p_group_on_hca->lid,
				p_hca->p_osm_node->print_desc);

			printed_hcas_on_leaf++;
		}

		/* now print missing HCAs */
		for (j = 0;
		     j < (p_ftree->max_cn_per_leaf - printed_hcas_on_leaf); j++)
			fprintf(p_hca_ordering_file, "0xFFFF\tDUMMY\n");

	}
	/* done going through all the leaf switches */

	fclose(p_hca_ordering_file);

	rename_status = rename(path_tmp, path);
	if (rename_status) {
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR, "ERR AB03: "
			"cannot rename file \'%s\': %s\n", path_tmp,
			strerror(errno));
	}
}				/* fabric_dump_hca_ordering() */

/***************************************************/

static void fabric_assign_tuple(IN ftree_fabric_t * p_ftree,
				IN ftree_sw_t * p_sw,
				IN ftree_tuple_t new_tuple)
{
	memcpy(p_sw->tuple, new_tuple, FTREE_TUPLE_LEN);
	fabric_add_sw_by_tuple(p_ftree, p_sw);
}

/***************************************************/

static void fabric_assign_first_tuple(IN ftree_fabric_t * p_ftree,
				      IN ftree_sw_t * p_sw,
				      IN unsigned int subtree)
{
	uint8_t i;
	ftree_tuple_t new_tuple;

	if (p_ftree->leaf_switch_rank >= FTREE_TUPLE_LEN)
		return;

	tuple_init(new_tuple);
	new_tuple[0] = (uint8_t) p_sw->rank;

	for (i = 1; i <= p_ftree->leaf_switch_rank; i++)
		new_tuple[i] = 0;

	if (p_sw->rank == 0) {
		if (p_ftree->leaf_switch_rank > 1)
			new_tuple[p_ftree->leaf_switch_rank] = subtree;

		for (i = 0; i < 0xFF; i++) {
			new_tuple[1] = i;
			if (fabric_get_sw_by_tuple(p_ftree, new_tuple) == NULL)
				break;
		}
		if (i == 0xFF) {
			/* new tuple not found - there are more than 255 ports in one direction */
			return;
		}
	}
	fabric_assign_tuple(p_ftree, p_sw, new_tuple);
}

/***************************************************/

static void fabric_get_new_tuple(IN ftree_fabric_t * p_ftree,
				 OUT ftree_tuple_t new_tuple,
				 IN ftree_tuple_t from_tuple,
				 IN ftree_direction_t direction)
{
	ftree_sw_t *p_sw;
	ftree_tuple_t temp_tuple;
	uint8_t var_index;
	uint8_t i;

	tuple_init(new_tuple);
	memcpy(temp_tuple, from_tuple, FTREE_TUPLE_LEN);

	if (direction == FTREE_DIRECTION_DOWN) {
		temp_tuple[0]++;
		var_index = from_tuple[0] + 1;
	} else {
		temp_tuple[0]--;
		var_index = from_tuple[0];
	}

	for (i = 0; i < 0xFF; i++) {
		temp_tuple[var_index] = i;
		p_sw = fabric_get_sw_by_tuple(p_ftree, temp_tuple);
		if (p_sw == NULL)	/* found free tuple */
			break;
	}

	if (i == 0xFF) {
		/* new tuple not found - there are more than 255 ports in one direction */
		return;
	}
	memcpy(new_tuple, temp_tuple, FTREE_TUPLE_LEN);

}				/* fabric_get_new_tuple() */

/***************************************************/

static inline boolean_t fabric_roots_provided(IN ftree_fabric_t * p_ftree)
{
	return (p_ftree->p_osm->subn.opt.root_guid_file != NULL);
}

/***************************************************/

static inline boolean_t fabric_cns_provided(IN ftree_fabric_t * p_ftree)
{
	return (p_ftree->p_osm->subn.opt.cn_guid_file != NULL);
}

/***************************************************/

static inline boolean_t fabric_ios_provided(IN ftree_fabric_t * p_ftree)
{
	return (p_ftree->p_osm->subn.opt.io_guid_file != NULL);
}

/***************************************************/

static int fabric_mark_leaf_switches(IN ftree_fabric_t * p_ftree)
{
	ftree_sw_t *p_sw;
	ftree_hca_t *p_hca;
	ftree_hca_t *p_next_hca;
	unsigned i;
	int res = 0;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"Marking leaf switches in fabric\n");

	/* Scan all the CAs, if they have CNs - find CN port and mark switch
	   that is connected to this port as leaf switch.
	   Also, ensure that this marked leaf has rank of p_ftree->leaf_switch_rank. */
	p_next_hca = (ftree_hca_t *) cl_qmap_head(&p_ftree->hca_tbl);
	while (p_next_hca != (ftree_hca_t *) cl_qmap_end(&p_ftree->hca_tbl)) {
		p_hca = p_next_hca;
		p_next_hca = (ftree_hca_t *) cl_qmap_next(&p_hca->map_item);
		if (!p_hca->cn_num)
			continue;

		for (i = 0; i < p_hca->up_port_groups_num; i++) {
			if (!p_hca->up_port_groups[i]->is_cn)
				continue;

			/* In CAs, port group alway has one port, and since this
			   port group is CN, we know that this port is compute node */
			CL_ASSERT(p_hca->up_port_groups[i]->remote_node_type ==
				  IB_NODE_TYPE_SWITCH);
			p_sw = p_hca->up_port_groups[i]->remote_hca_or_sw.p_sw;

			/* check if this switch was already processed */
			if (p_sw->is_leaf)
				continue;
			p_sw->is_leaf = TRUE;

			/* ensure that this leaf switch is at the correct tree level */
			if (p_sw->rank != p_ftree->leaf_switch_rank) {
				OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
					"ERR AB26: CN port 0x%" PRIx64
					" is connected to switch 0x%" PRIx64
					" with rank %u, "
					"while FatTree leaf rank is %u\n",
					cl_ntoh64(p_hca->
						  up_port_groups[i]->port_guid),
					sw_get_guid_ho(p_sw), p_sw->rank,
					p_ftree->leaf_switch_rank);
				res = -1;
				goto Exit;

			}
		}
	}

Exit:
	OSM_LOG_EXIT(&p_ftree->p_osm->log);
	return res;
}				/* fabric_mark_leaf_switches() */

/***************************************************/
static void bfs_fabric_indexing(IN ftree_fabric_t * p_ftree,
				IN ftree_sw_t *p_first_sw)
{
	ftree_sw_t *p_remote_sw;
	ftree_sw_t *p_sw = NULL;
	ftree_tuple_t new_tuple;
	uint32_t i;
	cl_list_t bfs_list;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);
	cl_list_init(&bfs_list, cl_qmap_count(&p_ftree->sw_tbl));
	/*
	 * Now run BFS and assign indexes to all switches
	 * Pseudo code of the algorithm is as follows:
	 *
	 *  * Add first switch to BFS queue
	 *  * While (BFS queue not empty)
	 *      - Pop the switch from the head of the queue
	 *      - Scan all the downward and upward ports
	 *      - For each port
	 *          + Get the remote switch
	 *          + Assign index to the remote switch
	 *          + Add remote switch to the BFS queue
	 */

	cl_list_insert_tail(&bfs_list, p_first_sw);

	while (!cl_is_list_empty(&bfs_list)) {
		p_sw = (ftree_sw_t *) cl_list_remove_head(&bfs_list);

		/* Discover all the nodes from ports that are pointing down */

		if (p_sw->rank >= p_ftree->leaf_switch_rank) {
			/* whether downward ports are pointing to CAs or switches,
			   we don't assign indexes to switches that are located
			   lower than leaf switches */
		} else {
			/* This is not the leaf switch */
			for (i = 0; i < p_sw->down_port_groups_num; i++) {
				/* Work with port groups that are pointing to switches only.
				   No need to assign indexing to HCAs */
				if (p_sw->
				    down_port_groups[i]->remote_node_type !=
				    IB_NODE_TYPE_SWITCH)
					continue;

				p_remote_sw =
				    p_sw->down_port_groups[i]->
				    remote_hca_or_sw.p_sw;
				if (tuple_assigned(p_remote_sw->tuple)) {
					/* this switch has been already indexed */
					continue;
				}
				/* allocate new tuple */
				fabric_get_new_tuple(p_ftree, new_tuple,
						     p_sw->tuple,
						     FTREE_DIRECTION_DOWN);
				/* Assign the new tuple to the remote switch.
				   This fuction also adds the switch into the switch_by_tuple table. */
				fabric_assign_tuple(p_ftree, p_remote_sw,
						    new_tuple);

				/* add the newly discovered switch to the BFS queue */
				cl_list_insert_tail(&bfs_list, p_remote_sw);
			}
			/* Done assigning indexes to all the remote switches
			   that are pointed by the downgoing ports.
			   Now sort port groups according to remote index. */
			qsort(p_sw->down_port_groups,	/* array */
			      p_sw->down_port_groups_num,	/* number of elements */
			      sizeof(ftree_port_group_t *),	/* size of each element */
			      compare_port_groups_by_remote_switch_index);	/* comparator */
		}

		/* Done indexing switches from ports that go down.
		   Now do the same with ports that are pointing up.
		   if we started from root (rank == 0), the leaf is bsf termination point */

		if (p_sw->rank != 0 && (p_first_sw->rank != 0 || !p_sw->is_leaf)) {
			/* This is not the root switch, which means that all the ports
			   that are pointing up are taking us to another switches. */
			for (i = 0; i < p_sw->up_port_groups_num; i++) {
				p_remote_sw =
				    p_sw->up_port_groups[i]->
				    remote_hca_or_sw.p_sw;
				if (tuple_assigned(p_remote_sw->tuple))
					continue;
				/* allocate new tuple */
				fabric_get_new_tuple(p_ftree, new_tuple,
						     p_sw->tuple,
						     FTREE_DIRECTION_UP);
				/* Assign the new tuple to the remote switch.
				   This fuction also adds the switch to the
				   switch_by_tuple table. */
				fabric_assign_tuple(p_ftree,
						    p_remote_sw, new_tuple);
				/* add the newly discovered switch to the BFS queue */
				cl_list_insert_tail(&bfs_list, p_remote_sw);
			}
			/* Done assigning indexes to all the remote switches
			   that are pointed by the upgoing ports.
			   Now sort port groups according to remote index. */
			qsort(p_sw->up_port_groups,	/* array */
			      p_sw->up_port_groups_num,	/* number of elements */
			      sizeof(ftree_port_group_t *),	/* size of each element */
			      compare_port_groups_by_remote_switch_index);	/* comparator */
		}
		/* Done assigning indexes to all the switches that are directly connected
		   to the current switch - go to the next switch in the BFS queue */
	}
	cl_list_destroy(&bfs_list);

	OSM_LOG_EXIT(&p_ftree->p_osm->log);
}

static void fabric_make_indexing(IN ftree_fabric_t * p_ftree)
{
	ftree_sw_t *p_sw = NULL;
	unsigned int subtree = 0;
	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"Starting FatTree indexing\n");

	/* using the first switch as a starting point for indexing algorithm. */
	for (p_sw = (ftree_sw_t *) cl_qmap_head(&p_ftree->sw_tbl);
	     p_sw != (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl);
	     p_sw = (ftree_sw_t *) cl_qmap_next(&p_sw->map_item)) {
		if (ftree_get_subnet(p_ftree)->opt.quasi_ftree_indexing) {
			/* find first root switch */
			if (p_sw->rank != 0)
				continue;
		} else {
			/* find first leaf switch */
			if (!p_sw->is_leaf)
				continue;
		}
		/* Assign the first tuple to the switch that is used as BFS starting point
		   in the subtree.
		   The tuple will be as follows: [rank].0...0.subtree
		   This fuction also adds the switch it into the switch_by_tuple table. */
		if (!tuple_assigned(p_sw->tuple)) {
			fabric_assign_first_tuple(p_ftree, p_sw, subtree++);
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
			"Indexing starting point:\n"
			"                                            - Switch rank  : %u\n"
			"                                            - Switch index : %s\n"
			"                                            - Node LID     : %u\n"
			"                                            - Node GUID    : 0x%016"
			PRIx64 "\n", p_sw->rank, tuple_to_str(p_sw->tuple),
			p_sw->lid, sw_get_guid_ho(p_sw));
		}

		bfs_fabric_indexing(p_ftree, p_sw);

		if (ftree_get_subnet(p_ftree)->opt.quasi_ftree_indexing == FALSE)
			goto Exit;
	}
	p_sw = (ftree_sw_t *) cl_qmap_head(&p_ftree->sw_tbl);
	while (p_sw != (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl)) {
		if (p_sw->is_leaf) {
			qsort(p_sw->up_port_groups,	/* array */
			      p_sw->up_port_groups_num,	/* number of elements */
			      sizeof(ftree_port_group_t *),	/* size of each element */
			      compare_port_groups_by_remote_switch_index);	/* comparator */
		}
		p_sw = (ftree_sw_t *) cl_qmap_next(&p_sw->map_item);

	}
Exit:
	OSM_LOG_EXIT(&p_ftree->p_osm->log);
}				/* fabric_make_indexing() */
/***************************************************/

static int fabric_create_leaf_switch_array(IN ftree_fabric_t * p_ftree)
{
	ftree_sw_t *p_sw;
	ftree_sw_t *p_next_sw;
	ftree_sw_t **all_switches_at_leaf_level;
	unsigned i;
	unsigned all_leaf_idx = 0;
	unsigned first_leaf_idx;
	unsigned last_leaf_idx;
	int res = 0;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	/* create array of ALL the switches that have leaf rank */
	all_switches_at_leaf_level = (ftree_sw_t **)
	    malloc(cl_qmap_count(&p_ftree->sw_tbl) * sizeof(ftree_sw_t *));
	if (!all_switches_at_leaf_level) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_SYS, FILE_ID,
			   "Fat-tree routing: Memory allocation failed\n");
		res = -1;
		goto Exit;
	}
	memset(all_switches_at_leaf_level, 0,
	       cl_qmap_count(&p_ftree->sw_tbl) * sizeof(ftree_sw_t *));

	p_next_sw = (ftree_sw_t *) cl_qmap_head(&p_ftree->sw_tbl);
	while (p_next_sw != (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (ftree_sw_t *) cl_qmap_next(&p_sw->map_item);
		if (p_sw->rank == p_ftree->leaf_switch_rank) {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
				"Adding switch 0x%" PRIx64
				" to full leaf switch array\n",
				sw_get_guid_ho(p_sw));
			all_switches_at_leaf_level[all_leaf_idx++] = p_sw;
		}
	}

	/* quick-sort array of leaf switches by index */
	qsort(all_switches_at_leaf_level,	/* array */
	      all_leaf_idx,	/* number of elements */
	      sizeof(ftree_sw_t *),	/* size of each element */
	      compare_switches_by_index);	/* comparator */

	/* check the first and the last REAL leaf (the one
	   that has CNs) in the array of all the leafs */

	first_leaf_idx = all_leaf_idx;
	last_leaf_idx = 0;
	for (i = 0; i < all_leaf_idx; i++) {
		if (all_switches_at_leaf_level[i]->is_leaf) {
			if (i < first_leaf_idx)
				first_leaf_idx = i;
			last_leaf_idx = i;
		}
	}

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
		"Full leaf array info: first_leaf_idx = %u, last_leaf_idx = %u\n",
		first_leaf_idx, last_leaf_idx);

	if (first_leaf_idx >= last_leaf_idx) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_INFO, FILE_ID,
			   "Failed to find leaf switches - topology is not "
			   "fat-tree\n");
		res = -1;
		goto Exit;
	}

	/* Create array of REAL leaf switches, sorted by index.
	   This array may contain switches at the same rank w/o CNs,
	   in case this is the order of indexing. */
	p_ftree->leaf_switches_num = last_leaf_idx - first_leaf_idx + 1;
	p_ftree->leaf_switches = (ftree_sw_t **)
	    malloc(p_ftree->leaf_switches_num * sizeof(ftree_sw_t *));
	if (!p_ftree->leaf_switches) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_SYS, FILE_ID,
			   "Fat-tree routing: Memory allocation failed\n");
		res = -1;
		goto Exit;
	}

	memcpy(p_ftree->leaf_switches,
	       &(all_switches_at_leaf_level[first_leaf_idx]),
	       p_ftree->leaf_switches_num * sizeof(ftree_sw_t *));

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
		"Created array of %u leaf switches\n",
		p_ftree->leaf_switches_num);

Exit:
	free(all_switches_at_leaf_level);
	OSM_LOG_EXIT(&p_ftree->p_osm->log);
	return res;
}				/* fabric_create_leaf_switch_array() */

/***************************************************/

static void fabric_set_max_cn_per_leaf(IN ftree_fabric_t * p_ftree)
{
	unsigned i;
	unsigned j;
	unsigned cns_on_this_leaf;
	ftree_sw_t *p_sw;
	ftree_port_group_t *p_group, *p_up_group;
	ftree_hca_t *p_hca;

	for (i = 0; i < p_ftree->leaf_switches_num; i++) {
		p_sw = p_ftree->leaf_switches[i];
		cns_on_this_leaf = 0;
		for (j = 0; j < p_sw->down_port_groups_num; j++) {
			p_group = p_sw->down_port_groups[j];
			if (p_group->remote_node_type != IB_NODE_TYPE_CA)
				continue;
			p_hca = p_group->remote_hca_or_sw.p_hca;
			/*
			 * Get the hca port group corresponding
			 * to the LID of remote HCA port
			 */
			p_up_group = hca_get_port_group_by_lid(p_hca,
				     p_group->remote_lid);

			CL_ASSERT(p_up_group);

			if (p_up_group->is_cn)
				cns_on_this_leaf++;
		}
		if (cns_on_this_leaf > p_ftree->max_cn_per_leaf)
			p_ftree->max_cn_per_leaf = cns_on_this_leaf;
	}
}				/* fabric_set_max_cn_per_leaf() */

/***************************************************/

static boolean_t fabric_validate_topology(IN ftree_fabric_t * p_ftree)
{
	ftree_port_group_t *p_group;
	ftree_port_group_t *p_ref_group;
	ftree_sw_t *p_sw;
	ftree_sw_t *p_next_sw;
	ftree_sw_t **reference_sw_arr;
	uint16_t tree_rank = fabric_get_rank(p_ftree);
	boolean_t res = TRUE;
	uint8_t i;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"Validating fabric topology\n");

	reference_sw_arr =
	    (ftree_sw_t **) malloc(tree_rank * sizeof(ftree_sw_t *));
	if (reference_sw_arr == NULL) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_SYS, FILE_ID,
			   "Fat-tree routing: Memory allocation failed\n");
		return FALSE;
	}
	memset(reference_sw_arr, 0, tree_rank * sizeof(ftree_sw_t *));

	p_next_sw = (ftree_sw_t *) cl_qmap_head(&p_ftree->sw_tbl);
	while (res && p_next_sw != (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (ftree_sw_t *) cl_qmap_next(&p_sw->map_item);

		if (!reference_sw_arr[p_sw->rank])
			/* This is the first switch in the current level that
			   we're checking - use it as a reference */
			reference_sw_arr[p_sw->rank] = p_sw;
		else {
			/* compare this switch properties to the reference switch */

			if (reference_sw_arr[p_sw->rank]->up_port_groups_num !=
			    p_sw->up_port_groups_num) {
				OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
					"ERR AB09: Different number of upward port groups on switches:\n"
					"       GUID 0x%016" PRIx64
					", LID %u, Index %s - %u groups\n"
					"       GUID 0x%016" PRIx64
					", LID %u, Index %s - %u groups\n",
					sw_get_guid_ho
					(reference_sw_arr[p_sw->rank]),
					reference_sw_arr[p_sw->rank]->lid,
					tuple_to_str
					(reference_sw_arr[p_sw->rank]->tuple),
					reference_sw_arr[p_sw->
							 rank]->
					up_port_groups_num,
					sw_get_guid_ho(p_sw), p_sw->lid,
					tuple_to_str(p_sw->tuple),
					p_sw->up_port_groups_num);
				res = FALSE;
				break;
			}

			if (p_sw->rank != (tree_rank - 1) &&
			    reference_sw_arr[p_sw->
					     rank]->down_port_groups_num !=
			    p_sw->down_port_groups_num) {
				/* we're allowing some hca's to be missing */
				OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
					"ERR AB0A: Different number of downward port groups on switches:\n"
					"       GUID 0x%016" PRIx64
					", LID %u, Index %s - %u port groups\n"
					"       GUID 0x%016" PRIx64
					", LID %u, Index %s - %u port groups\n",
					sw_get_guid_ho
					(reference_sw_arr[p_sw->rank]),
					reference_sw_arr[p_sw->rank]->lid,
					tuple_to_str
					(reference_sw_arr[p_sw->rank]->tuple),
					reference_sw_arr[p_sw->
							 rank]->
					down_port_groups_num,
					sw_get_guid_ho(p_sw), p_sw->lid,
					tuple_to_str(p_sw->tuple),
					p_sw->down_port_groups_num);
				res = FALSE;
				break;
			}

			if (reference_sw_arr[p_sw->rank]->up_port_groups_num !=
			    0) {
				p_ref_group =
				    reference_sw_arr[p_sw->
						     rank]->up_port_groups[0];
				for (i = 0; i < p_sw->up_port_groups_num; i++) {
					p_group = p_sw->up_port_groups[i];
					if (cl_ptr_vector_get_size
					    (&p_ref_group->ports) !=
					    cl_ptr_vector_get_size
					    (&p_group->ports)) {
						OSM_LOG(&p_ftree->p_osm->log,
							OSM_LOG_ERROR,
							"ERR AB0B: Different number of ports in an upward port group on switches:\n"
							"       GUID 0x%016"
							PRIx64
							", LID %u, Index %s - %u ports\n"
							"       GUID 0x%016"
							PRIx64
							", LID %u, Index %s - %u ports\n",
							sw_get_guid_ho
							(reference_sw_arr
							 [p_sw->rank]),
							reference_sw_arr[p_sw->
									 rank]->
							lid,
							tuple_to_str
							(reference_sw_arr
							 [p_sw->rank]->tuple),
							cl_ptr_vector_get_size
							(&p_ref_group->ports),
							sw_get_guid_ho(p_sw),
							p_sw->lid,
							tuple_to_str(p_sw->
								     tuple),
							cl_ptr_vector_get_size
							(&p_group->ports));
						res = FALSE;
						break;
					}
				}
			}
			if (reference_sw_arr[p_sw->rank]->down_port_groups_num
			    != 0 && p_sw->rank != (tree_rank - 1)) {
				/* we're allowing some hca's to be missing */
				p_ref_group =
				    reference_sw_arr[p_sw->
						     rank]->down_port_groups[0];
				for (i = 0; i < p_sw->down_port_groups_num; i++) {
					p_group = p_sw->down_port_groups[0];
					if (cl_ptr_vector_get_size
					    (&p_ref_group->ports) !=
					    cl_ptr_vector_get_size
					    (&p_group->ports)) {
						OSM_LOG(&p_ftree->p_osm->log,
							OSM_LOG_ERROR,
							"ERR AB0C: Different number of ports in an downward port group on switches:\n"
							"       GUID 0x%016"
							PRIx64
							", LID %u, Index %s - %u ports\n"
							"       GUID 0x%016"
							PRIx64
							", LID %u, Index %s - %u ports\n",
							sw_get_guid_ho
							(reference_sw_arr
							 [p_sw->rank]),
							reference_sw_arr[p_sw->
									 rank]->
							lid,
							tuple_to_str
							(reference_sw_arr
							 [p_sw->rank]->tuple),
							cl_ptr_vector_get_size
							(&p_ref_group->ports),
							sw_get_guid_ho(p_sw),
							p_sw->lid,
							tuple_to_str(p_sw->
								     tuple),
							cl_ptr_vector_get_size
							(&p_group->ports));
						res = FALSE;
						break;
					}
				}
			}
		}		/* end of else */
	}			/* end of while */

	if (res == TRUE)
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
			"Fabric topology has been identified as FatTree\n");
	else
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
			"ERR AB0D: Fabric topology hasn't been identified as FatTree\n");

	free(reference_sw_arr);
	OSM_LOG_EXIT(&p_ftree->p_osm->log);
	return res;
}				/* fabric_validate_topology() */

/***************************************************
 ***************************************************/

static void set_sw_fwd_table(IN cl_map_item_t * const p_map_item,
			     IN void *context)
{
	ftree_sw_t *p_sw = (ftree_sw_t * const)p_map_item;
	ftree_fabric_t *p_ftree = (ftree_fabric_t *) context;

	p_sw->p_osm_sw->max_lid_ho = p_ftree->lft_max_lid;
}

/***************************************************
 ***************************************************/

/*
 * Function: Finds the least loaded port group and stores its counter
 * Given   : A switch
 */
static inline void recalculate_min_counter_down(ftree_sw_t * p_sw)
{
	uint32_t min = (1 << 30);
	uint32_t i;
	for (i = 0; i < p_sw->down_port_groups_num; i++) {
		if (p_sw->down_port_groups[i]->counter_down < min) {
			min = p_sw->down_port_groups[i]->counter_down;
		}
	}
	p_sw->min_counter_down = min;
	return;
}

/*
 * Function: Return the counter value of the least loaded down port group
 * Given   : A switch
 */
static inline uint32_t find_lowest_loaded_group_on_sw(ftree_sw_t * p_sw)
{
	return p_sw->min_counter_down;
}

/*
 * Function: Compare the load of two port groups and return which is the least loaded
 * Given   : Two port groups with remote switch
 * When both port groups are equally loaded, it picks the one whom
 * remote switch down ports are least loaded.
 * This way, it prefers the switch from where it will be easier to go down (creating upward routes).
 * If both are equal, it picks the lowest INDEX to be deterministic.
 */
static inline int port_group_compare_load_down(const ftree_port_group_t * p1,
					       const ftree_port_group_t * p2)
{
	int temp = p1->counter_down - p2->counter_down;
	if (temp > 0)
		return 1;
	if (temp < 0)
		return -1;

	/* Find the less loaded remote sw and choose this one */
	do {
		uint32_t load1 =
		    find_lowest_loaded_group_on_sw(p1->remote_hca_or_sw.p_sw);
		uint32_t load2 =
		    find_lowest_loaded_group_on_sw(p2->remote_hca_or_sw.p_sw);
		temp = load1 - load2;
		if (temp > 0)
			return 1;
	} while (0);
	/* If they are both equal, choose the lowest index */
	return compare_port_groups_by_remote_switch_index(&p1, &p2);
}

static inline int port_group_compare_load_up(const ftree_port_group_t * p1,
                                             const ftree_port_group_t * p2)
{
        int temp = p1->counter_up - p2->counter_up;
        if (temp > 0)
                return 1;
        if (temp < 0)
                return -1;

        /* If they are both equal, choose the lowest index */
        return compare_port_groups_by_remote_switch_index (&p1,&p2);
}

/*
 * Function: Sorts an array of port group by up load order
 * Given   : A port group array and its length
 * As the list is mostly sorted, we used a bubble sort instead of qsort
 * as it is much faster.
 *
 * Important note:
 * This function and bubble_sort_down must NOT be factorized.
 * Although most of the code is the same and a function pointer could be used
 * for the compareason function, it would prevent the compareason function to be inlined
 * and cost a great deal to performances.
 */
static inline void
bubble_sort_up(ftree_port_group_t ** p_group_array, uint32_t nmemb)
{
	uint32_t i = 0;
	uint32_t j = 0;
	ftree_port_group_t *tmp = p_group_array[0];

	/* As this function is a great number of times, we only go into the loop
	 * if one of the port counters has changed, thus saving some tests */
	if (tmp->hca_or_sw.p_sw->counter_up_changed == FALSE) {
		return;
	}
	/* While we did modifications on the array order */
	/* i may grew above array length but next loop will fail and tmp will be null for the next time
	 * this way we save a test i < nmemb for each pass through the loop */
	for (i = 0; tmp; i++) {
		/* Assume the array is orderd */
		tmp = NULL;
		/* Comparing elements j and j-1 */
		for (j = 1; j < (nmemb - i); j++) {
			/* If they are the wrong way around */
			if (port_group_compare_load_up(p_group_array[j],
						       p_group_array[j - 1]) < 0) {
				/* We invert them */
				tmp = p_group_array[j - 1];
				p_group_array[j - 1] = p_group_array[j];
				p_group_array[j] = tmp;
				/* This sets tmp != NULL so the main loop will make another pass */
			}
		}
	}

	/* We have reordered the array so as long noone changes the counter
	 * it's not necessary to do it again */
	p_group_array[0]->hca_or_sw.p_sw->counter_up_changed = FALSE;
}

static inline void
bubble_sort_siblings(ftree_port_group_t ** p_group_array, uint32_t nmemb)
{
	uint32_t i = 0;
	uint32_t j = 0;
	ftree_port_group_t *tmp = p_group_array[0];

	/* While we did modifications on the array order */
	/* i may grew above array length but next loop will fail and tmp will be null for the next time
	 * this way we save a test i < nmemb for each pass through the loop */
	for (i = 0; tmp != NULL; i++) {
		/* Assume the array is orderd */
		tmp = NULL;
		/* Comparing elements j and j-1 */
		for (j = 1; j < (nmemb - i); j++) {
			/* If they are the wrong way around */
			if (port_group_compare_load_up(p_group_array[j],
						       p_group_array[j - 1]) < 0) {
				/* We invert them */
				tmp = p_group_array[j - 1];
				p_group_array[j - 1] = p_group_array[j];
				p_group_array[j] = tmp;
			}
		}
	}
}

/*
 * Function: Sorts an array of port group. Order is decide through
 * port_group_compare_load_down ( up counters, least load remote switch, biggest GUID)
 * Given   : A port group array and its length. Each port group points to a remote switch (not a HCA)
 * As the list is mostly sorted, we used a bubble sort instead of qsort
 * as it is much faster.
 *
 * Important note:
 * This function and bubble_sort_up must NOT be factorized.
 * Although most of the code is the same and a function pointer could be used
 * for the compareason function, it would prevent the compareason function to be inlined
 * and cost a great deal to performances.
 */
static inline void
bubble_sort_down(ftree_port_group_t ** p_group_array, uint32_t nmemb)
{
	uint32_t i = 0;
	uint32_t j = 0;
	ftree_port_group_t *tmp = p_group_array[0];

	/* While we did modifications on the array order */
	/* i may grew above array length but next loop will fail and tmp will be null for the next time
	 * this way we save a test i < nmemb for each pass through the loop */
	for (i = 0; tmp; i++) {
		/* Assume the array is orderd */
		tmp = NULL;
		/* Comparing elements j and j-1 */
		for (j = 1; j < (nmemb - i); j++) {
			/* If they are the wrong way around */
			if (port_group_compare_load_down
			    (p_group_array[j], p_group_array[j - 1]) < 0) {
				/* We invert them */
				tmp = p_group_array[j - 1];
				p_group_array[j - 1] = p_group_array[j];
				p_group_array[j] = tmp;

			}
		}
	}
}

/***************************************************
 ***************************************************/

/*
 * Function: assign-up-going-port-by-descending-down
 * Given   : a switch and a LID
 * Pseudo code:
 *    foreach down-going-port-group (in indexing order)
 *        skip this group if the LFT(LID) port is part of this group
 *        find the least loaded port of the group (scan in indexing order)
 *        r-port is the remote port connected to it
 *        assign the remote switch node LFT(LID) to r-port
 *        increase r-port usage counter
 *        assign-up-going-port-by-descending-down to r-port node (recursion)
 */

static boolean_t
fabric_route_upgoing_by_going_down(IN ftree_fabric_t * p_ftree,
				   IN ftree_sw_t * p_sw,
				   IN ftree_sw_t * p_prev_sw,
				   IN uint16_t target_lid,
				   IN boolean_t is_main_path,
				   IN boolean_t is_target_a_sw,
				   IN uint8_t current_hops)
{
	ftree_sw_t *p_remote_sw;
	uint16_t ports_num;
	ftree_port_group_t *p_group;
	ftree_port_t *p_port;
	ftree_port_t *p_min_port;
	uint16_t j;
	uint16_t k;
	boolean_t created_route = FALSE;
	boolean_t routed = 0;
	uint8_t least_hops;

	/* if there is no down-going ports */
	if (p_sw->down_port_groups_num == 0)
		return FALSE;

	/* foreach down-going port group (in load order) */
	bubble_sort_up(p_sw->down_port_groups, p_sw->down_port_groups_num);

	if (p_sw->sibling_port_groups_num > 0)
		bubble_sort_siblings(p_sw->sibling_port_groups,
				     p_sw->sibling_port_groups_num);

	for (k = 0;
	     k <
	     (p_sw->down_port_groups_num +
	      ((target_lid != 0) ? p_sw->sibling_port_groups_num : 0)); k++) {

		if (k < p_sw->down_port_groups_num) {
			p_group = p_sw->down_port_groups[k];
		} else {
			p_group =
			    p_sw->sibling_port_groups[k -
						      p_sw->
						      down_port_groups_num];
		}

		/* If this port group doesn't point to a switch, mark
		   that the route was created and skip to the next group */
		if (p_group->remote_node_type != IB_NODE_TYPE_SWITCH) {
			created_route = TRUE;
			continue;
		}

		if (p_prev_sw
		    && p_group->remote_lid == p_prev_sw->lid) {
			/* This port group has a port that was used when we entered this switch,
			   which means that the current group points to the switch where we were
			   at the previous step of the algorithm (before going up).
			   Skipping this group. */
			continue;
		}

		/* find the least loaded port of the group (in indexing order) */
		p_min_port = NULL;
		ports_num = (uint16_t) cl_ptr_vector_get_size(&p_group->ports);
		if(ports_num == 0)
			continue;

		for (j = 0; j < ports_num; j++) {
			cl_ptr_vector_at(&p_group->ports, j, (void *)&p_port);
			/* first port that we're checking - set as port with the lowest load */
			/* or this port is less loaded - use it as min */
			if (!p_min_port ||
			    p_port->counter_up < p_min_port->counter_up)
				p_min_port = p_port;
		}
		/* At this point we have selected a port in this group with the
		   lowest load of upgoing routes.
		   Set on the remote switch how to get to the target_lid -
		   set LFT(target_lid) on the remote switch to the remote port */
		p_remote_sw = p_group->remote_hca_or_sw.p_sw;
		least_hops = sw_get_least_hops(p_remote_sw, target_lid);

		if (least_hops != OSM_NO_PATH) {
			/* Loop in the fabric - we already routed the remote switch
			   on our way UP, and now we see it again on our way DOWN */
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
				"Loop of length %d in the fabric:\n                             "
				"Switch %s (LID %u) closes loop through switch %s (LID %u)\n",
				current_hops,
				tuple_to_str(p_remote_sw->tuple),
				p_group->lid,
				tuple_to_str(p_sw->tuple),
				p_group->remote_lid);
			/* We skip only if we have come through a longer path */
			if (current_hops + 1 >= least_hops)
				continue;
		}

		/* Four possible cases:
		 *
		 *  1. is_main_path == TRUE:
		 *      - going DOWN(TRUE,TRUE) through ALL the groups
		 *         + promoting port counter
		 *         + setting path in remote switch fwd tbl
		 *         + setting hops in remote switch on all the ports of each group
		 *
		 *  2. is_main_path == FALSE:
		 *      - going DOWN(TRUE,FALSE) through ALL the groups but only if
		 *        the remote (lower) switch hasn't been already configured
		 *        for this target LID (or with a longer path)
		 *         + promoting port counter
		 *         + setting path in remote switch fwd tbl if it hasn't been set yet
		 *         + setting hops in remote switch on all the ports of each group
		 *           if it hasn't been set yet
		 */

		/* setting fwd tbl port only */
		p_remote_sw->p_osm_sw->new_lft[target_lid] =
			    p_min_port->remote_port_num;
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
				"Switch %s: set path to CA LID %u through port %u\n",
				tuple_to_str(p_remote_sw->tuple),
				target_lid, p_min_port->remote_port_num);

		/* On the remote switch that is pointed by the p_group,
			set hops for ALL the ports in the remote group. */

		set_hops_on_remote_sw(p_group, target_lid,
				      current_hops + 1, is_target_a_sw);

		/* Recursion step:
		   Assign upgoing ports by stepping down, starting on REMOTE switch */
		routed = fabric_route_upgoing_by_going_down(p_ftree, p_remote_sw,	/* remote switch - used as a route-upgoing alg. start point */
							    NULL,	/* prev. position - NULL to mark that we went down and not up */
							    target_lid,	/* LID that we're routing to */
							    is_main_path,	/* whether this is path to HCA that should by tracked by counters */
							    is_target_a_sw,	/* Whether target lid is a switch or not */
							    current_hops + 1);	/* Number of hops done to this point */
		created_route |= routed;
		/* Counters are promoted only if a route toward a node is created */
		if (routed) {
			p_min_port->counter_up++;
			p_group->counter_up++;
			p_group->hca_or_sw.p_sw->counter_up_changed = TRUE;
		}
	}
	/* done scanning all the down-going port groups */

	/* if the route was created, promote the index that
	   indicates which group should we start with when
	   going through all the downgoing groups */
	if (created_route)
		p_sw->down_port_groups_idx = (p_sw->down_port_groups_idx + 1)
		    % p_sw->down_port_groups_num;

	return created_route;
}				/* fabric_route_upgoing_by_going_down() */

/***************************************************/

/*
 * Function: assign-down-going-port-by-ascending-up
 * Given   : a switch and a LID
 * Pseudo code:
 *    find the least loaded port of all the upgoing groups (scan in indexing order)
 *    assign the LFT(LID) of remote switch to that port
 *    track that port usage
 *    assign-up-going-port-by-descending-down on CURRENT switch
 *    assign-down-going-port-by-ascending-up on REMOTE switch (recursion)
 */

static boolean_t
fabric_route_downgoing_by_going_up(IN ftree_fabric_t * p_ftree,
				   IN ftree_sw_t * p_sw,
				   IN ftree_sw_t * p_prev_sw,
				   IN uint16_t target_lid,
				   IN boolean_t is_main_path,
				   IN boolean_t is_target_a_sw,
				   IN uint16_t reverse_hop_credit,
				   IN uint16_t reverse_hops,
				   IN uint8_t current_hops)
{
	ftree_sw_t *p_remote_sw;
	uint16_t ports_num;
	ftree_port_group_t *p_group;
	ftree_port_t *p_port;
	ftree_port_group_t *p_min_group;
	ftree_port_t *p_min_port;
	uint16_t i;
	uint16_t j;
	boolean_t created_route = FALSE;
	boolean_t routed = FALSE;


	/* Assign upgoing ports by stepping down, starting on THIS switch */
	created_route = fabric_route_upgoing_by_going_down(p_ftree, p_sw,	/* local switch - used as a route-upgoing alg. start point */
							   p_prev_sw,	/* switch that we went up from (NULL means that we went down) */
							   target_lid,	/* LID that we're routing to */
							   is_main_path,	/* whether this path to HCA should by tracked by counters */
							   is_target_a_sw,	/* Whether target lid is a switch or not */
							   current_hops);	/* Number of hops done up to this point */

	/* recursion stop condition - if it's a root switch, */
	if (p_sw->rank == 0) {
		if (reverse_hop_credit > 0) {
			/* We go up by going down as we have some reverse_hop_credit left */
			/* We use the index to scatter a bit the reverse up routes */
			p_sw->down_port_groups_idx =
			    (p_sw->down_port_groups_idx +
			     1) % p_sw->down_port_groups_num;
			i = p_sw->down_port_groups_idx;
			for (j = 0; j < p_sw->down_port_groups_num; j++) {

				p_group = p_sw->down_port_groups[i];
				i = (i + 1) % p_sw->down_port_groups_num;

				/* Skip this port group unless it points to a switch */
				if (p_group->remote_node_type !=
				    IB_NODE_TYPE_SWITCH)
					continue;
				p_remote_sw = p_group->remote_hca_or_sw.p_sw;

				created_route |= fabric_route_downgoing_by_going_up(p_ftree, p_remote_sw,	/* remote switch - used as a route-downgoing alg. next step point */
										    p_sw,	/* this switch - prev. position switch for the function */
										    target_lid,	/* LID that we're routing to */
										    is_main_path,	/* whether this is path to HCA that should by tracked by counters */
										    is_target_a_sw,	/* Whether target lid is a switch or not */
										    reverse_hop_credit - 1,	/* Remaining reverse_hops allowed */
										    reverse_hops + 1,	/* Number of reverse_hops done up to this point */
										    current_hops
										    +
										    1);
			}

		}
		return created_route;
	}

	/* We should generate a list of port sorted by load so we can find easily the least
	 * going port and explore the other pots on secondary routes more easily (and quickly) */
	bubble_sort_down(p_sw->up_port_groups, p_sw->up_port_groups_num);

	p_min_group = p_sw->up_port_groups[0];
	/* Find the least loaded upgoing port in the selected group */
	p_min_port = NULL;
	ports_num = (uint16_t) cl_ptr_vector_get_size(&p_min_group->ports);
	for (j = 0; j < ports_num; j++) {
		cl_ptr_vector_at(&p_min_group->ports, j, (void *)&p_port);
		if (!p_min_port) {
			/* first port that we're checking - use
			   it as a port with the lowest load */
			p_min_port = p_port;
		} else if (p_port->counter_down < p_min_port->counter_down) {
			/* this port is less loaded - use it as min */
			p_min_port = p_port;
		}
	}

	/* At this point we have selected a group and port with the
	   lowest load of downgoing routes.
	   Set on the remote switch how to get to the target_lid -
	   set LFT(target_lid) on the remote switch to the remote port */
	p_remote_sw = p_min_group->remote_hca_or_sw.p_sw;

	/* Four possible cases:
	 *
	 *  1. is_main_path == TRUE:
	 *      - going UP(TRUE,TRUE) on selected min_group and min_port
	 *         + promoting port counter
	 *         + setting path in remote switch fwd tbl
	 *         + setting hops in remote switch on all the ports of selected group
	 *      - going UP(TRUE,FALSE) on rest of the groups, each time on port 0
	 *         + NOT promoting port counter
	 *         + setting path in remote switch fwd tbl if it hasn't been set yet
	 *         + setting hops in remote switch on all the ports of each group
	 *           if it hasn't been set yet
	 *
	 *  2. is_main_path == FALSE:
	 *      - going UP(TRUE,FALSE) on ALL the groups, each time on port 0,
	 *        but only if the remote (upper) switch hasn't been already
	 *        configured for this target LID
	 *         + NOT promoting port counter
	 *         + setting path in remote switch fwd tbl if it hasn't been set yet
	 *         + setting hops in remote switch on all the ports of each group
	 *           if it hasn't been set yet
	 */

	/* covering first half of case 1, and case 3 */
	if (is_main_path) {
		if (p_sw->is_leaf) {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
				" - Routing MAIN path for %s CA LID %u: %s --> %s\n",
				(target_lid != 0) ? "real" : "DUMMY",
				target_lid,
				tuple_to_str(p_sw->tuple),
				tuple_to_str(p_remote_sw->tuple));
		}
		/* The number of downgoing routes is tracked in the
		   p_group->counter_down p_port->counter_down counters of the
		   group and port that belong to the lower side of the link
		   (on switch with higher rank) */
		p_min_group->counter_down++;
		p_min_port->counter_down++;
		if (p_min_group->counter_down ==
		    (p_min_group->remote_hca_or_sw.p_sw->min_counter_down +
		     1)) {
			recalculate_min_counter_down
			    (p_min_group->remote_hca_or_sw.p_sw);
		}

		/* This LID may already be in the LFT in the reverse_hop feature is used */
		/* We update the LFT only if this LID isn't already present. */

		/* skip if target lid has been already set on remote switch fwd tbl (with a bigger hop count) */
		if ((p_remote_sw->p_osm_sw->new_lft[target_lid] == OSM_NO_PATH)
		    ||
		    (current_hops + 1 <
		     sw_get_least_hops(p_remote_sw, target_lid))) {

			p_remote_sw->p_osm_sw->new_lft[target_lid] =
				p_min_port->remote_port_num;
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
					"Switch %s: set path to CA LID %u through port %u\n",
					tuple_to_str(p_remote_sw->tuple),
					target_lid,
					p_min_port->remote_port_num);

			/* On the remote switch that is pointed by the min_group,
			set hops for ALL the ports in the remote group. */

			set_hops_on_remote_sw(p_min_group, target_lid,
						      current_hops + 1,
						      is_target_a_sw);
		}
	/* Recursion step: Assign downgoing ports by stepping up, starting on REMOTE switch. */
	created_route |= fabric_route_downgoing_by_going_up(p_ftree,
							    p_remote_sw,	/* remote switch - used as a route-downgoing alg. next step point */
							    p_sw,		/* this switch - prev. position switch for the function */
							    target_lid,		/* LID that we're routing to */
							    is_main_path,	/* whether this is path to HCA that should by tracked by counters */
							    is_target_a_sw,	/* Whether target lid is a switch or not */
							    reverse_hop_credit,	/* Remaining reverse_hops allowed */
							    reverse_hops,	/* Number of reverse_hops done up to this point */
							    current_hops + 1);
	}

	/* What's left to do at this point:
	 *
	 *  1. is_main_path == TRUE:
	 *      - going UP(TRUE,FALSE) on rest of the groups, each time on port 0,
	 *        but only if the remote (upper) switch hasn't been already
	 *        configured for this target LID
	 *         + NOT promoting port counter
	 *         + setting path in remote switch fwd tbl if it hasn't been set yet
	 *         + setting hops in remote switch on all the ports of each group
	 *           if it hasn't been set yet
	 *
	 *  2. is_main_path == FALSE:
	 *      - going UP(TRUE,FALSE) on ALL the groups, each time on port 0,
	 *        but only if the remote (upper) switch hasn't been already
	 *        configured for this target LID
	 *         + NOT promoting port counter
	 *         + setting path in remote switch fwd tbl if it hasn't been set yet
	 *         + setting hops in remote switch on all the ports of each group
	 *           if it hasn't been set yet
	 *
	 *  These two rules can be rephrased this way:
	 *   - foreach UP port group
	 *      + if remote switch has been set with the target LID
	 *         - skip this port group
	 *      + else
	 *         - select port 0
	 *         - do NOT promote port counter
	 *         - set path in remote switch fwd tbl
	 *         - set hops in remote switch on all the ports of this group
	 *         - go UP(TRUE,FALSE) to the remote switch
	 */

	for (i = is_main_path ? 1 : 0; i < p_sw->up_port_groups_num; i++) {
		p_group = p_sw->up_port_groups[i];
		p_remote_sw = p_group->remote_hca_or_sw.p_sw;

		/* skip if target lid has been already set on remote switch fwd tbl (with a bigger hop count) */
		if (p_remote_sw->p_osm_sw->new_lft[target_lid] != OSM_NO_PATH)
			if (current_hops + 1 >=
			    sw_get_least_hops(p_remote_sw, target_lid))
				continue;

		if (p_sw->is_leaf) {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
				" - Routing SECONDARY path for LID %u: %s --> %s\n",
				target_lid,
				tuple_to_str(p_sw->tuple),
				tuple_to_str(p_remote_sw->tuple));
		}

		/* Routing REAL lids on SECONDARY path means routing
		   switch-to-switch or switch-to-CA paths.
		   We can safely assume that switch will initiate very
		   few traffic, so there's no point wasting runtime on
		   trying to balance these routes - always pick port 0. */
		p_min_port = NULL;
		ports_num = (uint16_t) cl_ptr_vector_get_size(&p_group->ports);
		if(ports_num == 0)
			continue;
		for (j = 0; j < ports_num; j++) {
			cl_ptr_vector_at(&p_group->ports, j, (void *)&p_port);
			if (!p_min_port) {
				/* first port that we're checking - use
				   it as a port with the lowest load */
				p_min_port = p_port;
			} else if (p_port->counter_down <
				   p_min_port->counter_down) {
				/* this port is less loaded - use it as min */
				p_min_port = p_port;
			}
		}

		p_port = p_min_port;
		p_remote_sw->p_osm_sw->new_lft[target_lid] =
		    p_port->remote_port_num;

		/* On the remote switch that is pointed by the p_group,
		   set hops for ALL the ports in the remote group. */

		set_hops_on_remote_sw(p_group, target_lid,
				      current_hops + 1, is_target_a_sw);

		/* Recursion step:
		   Assign downgoing ports by stepping up, starting on REMOTE switch. */
		routed = fabric_route_downgoing_by_going_up(p_ftree, p_remote_sw,	/* remote switch - used as a route-downgoing alg. next step point */
							    p_sw,	/* this switch - prev. position switch for the function */
							    target_lid,	/* LID that we're routing to */
							    FALSE,	/* whether this is path to HCA that should by tracked by counters */
							    is_target_a_sw,	/* Whether target lid is a switch or not */
							    reverse_hop_credit,	/* Remaining reverse_hops allowed */
							    reverse_hops,	/* Number of reverse_hops done up to this point */
							    current_hops + 1);
		created_route |= routed;
	}

	/* Now doing the same thing with horizontal links */
	if (p_sw->sibling_port_groups_num > 0)
		bubble_sort_down(p_sw->sibling_port_groups,
				 p_sw->sibling_port_groups_num);

	for (i = 0; i < p_sw->sibling_port_groups_num; i++) {
		p_group = p_sw->sibling_port_groups[i];
		p_remote_sw = p_group->remote_hca_or_sw.p_sw;

		/* skip if target lid has been already set on remote switch fwd tbl (with a bigger hop count) */
		if (p_remote_sw->p_osm_sw->new_lft[target_lid] != OSM_NO_PATH)
			if (current_hops + 1 >=
			    sw_get_least_hops(p_remote_sw, target_lid))
				continue;

		if (p_sw->is_leaf) {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
				" - Routing SECONDARY path for LID %u: %s --> %s\n",
				target_lid,
				tuple_to_str(p_sw->tuple),
				tuple_to_str(p_remote_sw->tuple));
		}

		/* Routing REAL lids on SECONDARY path means routing
		   switch-to-switch or switch-to-CA paths.
		   We can safely assume that switch will initiate very
		   few traffic, so there's no point wasting runtime on
		   trying to balance these routes - always pick port 0. */

		p_min_port = NULL;
		ports_num = (uint16_t) cl_ptr_vector_get_size(&p_group->ports);
		for (j = 0; j < ports_num; j++) {
			cl_ptr_vector_at(&p_group->ports, j, (void *)&p_port);
			if (!p_min_port) {
				/* first port that we're checking - use
				   it as a port with the lowest load */
				p_min_port = p_port;
			} else if (p_port->counter_down <
				   p_min_port->counter_down) {
				/* this port is less loaded - use it as min */
				p_min_port = p_port;
			}
		}

		p_port = p_min_port;
		p_remote_sw->p_osm_sw->new_lft[target_lid] =
		    p_port->remote_port_num;

		/* On the remote switch that is pointed by the p_group,
		   set hops for ALL the ports in the remote group. */

		set_hops_on_remote_sw(p_group, target_lid,
				      current_hops + 1, is_target_a_sw);

		/* Recursion step:
		   Assign downgoing ports by stepping up, starting on REMOTE switch. */
		routed = fabric_route_downgoing_by_going_up(p_ftree, p_remote_sw,	/* remote switch - used as a route-downgoing alg. next step point */
							    p_sw,	/* this switch - prev. position switch for the function */
							    target_lid,	/* LID that we're routing to */
							    FALSE,	/* whether this is path to HCA that should by tracked by counters */
							    is_target_a_sw,	/* Whether target lid is a switch or not */
							    reverse_hop_credit,	/* Remaining reverse_hops allowed */
							    reverse_hops,	/* Number of reverse_hops done up to this point */
							    current_hops + 1);
		created_route |= routed;
		if (routed) {
			p_min_group->counter_down++;
			p_min_port->counter_down++;
		}
	}

	/* If we don't have any reverse hop credits, we are done */
	if (reverse_hop_credit == 0)
		return created_route;

	if (p_sw->is_leaf)
		return created_route;

	/* We explore all the down group ports */
	/* We try to reverse jump for each of them */
	/* They already have a route to us from the upgoing_by_going_down started earlier */
	/* This is only so it'll continue exploring up, after this step backwards */
	for (i = 0; i < p_sw->down_port_groups_num; i++) {
		p_group = p_sw->down_port_groups[i];
		p_remote_sw = p_group->remote_hca_or_sw.p_sw;

		/* Skip this port group unless it points to a switch */
		if (p_group->remote_node_type != IB_NODE_TYPE_SWITCH)
			continue;

		/* Recursion step:
		   Assign downgoing ports by stepping up, fter doing one step down starting on REMOTE switch. */
		created_route |= fabric_route_downgoing_by_going_up(p_ftree, p_remote_sw,	/* remote switch - used as a route-downgoing alg. next step point */
								    p_sw,	/* this switch - prev. position switch for the function */
								    target_lid,	/* LID that we're routing to */
								    TRUE,	/* whether this is path to HCA that should by tracked by counters */
								    is_target_a_sw,	/* Whether target lid is a switch or not */
								    reverse_hop_credit - 1,	/* Remaining reverse_hops allowed */
								    reverse_hops + 1,	/* Number of reverse_hops done up to this point */
								    current_hops
								    + 1);
	}
	return created_route;

}				/* ftree_fabric_route_downgoing_by_going_up() */

/***************************************************/

/*
 * Pseudo code:
 *    foreach leaf switch (in indexing order)
 *       for each compute node (in indexing order)
 *          obtain the LID of the compute node
 *          set local LFT(LID) of the port connecting to compute node
 *          call assign-down-going-port-by-ascending-up(TRUE,TRUE) on CURRENT switch
 *       for each MISSING compute node
 *          call assign-down-going-port-by-ascending-up(FALSE,TRUE) on CURRENT switch
 */

static void fabric_route_to_cns(IN ftree_fabric_t * p_ftree)
{
	ftree_sw_t *p_sw;
	ftree_hca_t *p_hca;
	ftree_port_group_t *p_leaf_port_group;
	ftree_port_group_t *p_hca_port_group;
	ftree_port_t *p_port;
	unsigned int i, j;
	uint16_t hca_lid;
	unsigned routed_targets_on_leaf;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	/* for each leaf switch (in indexing order) */
	for (i = 0; i < p_ftree->leaf_switches_num; i++) {
		p_sw = p_ftree->leaf_switches[i];
		routed_targets_on_leaf = 0;

		/* for each HCA connected to this switch */
		for (j = 0; j < p_sw->down_port_groups_num; j++) {
			p_leaf_port_group = p_sw->down_port_groups[j];

			/* work with this port group only if the remote node is CA */
			if (p_leaf_port_group->remote_node_type !=
			    IB_NODE_TYPE_CA)
				continue;

			p_hca = p_leaf_port_group->remote_hca_or_sw.p_hca;

			/* work with this port group only if remote HCA has CNs */
			if (!p_hca->cn_num)
				continue;

			p_hca_port_group =
			    hca_get_port_group_by_lid(p_hca,
						      p_leaf_port_group->
						      remote_lid);
			CL_ASSERT(p_hca_port_group);

			/* work with this port group only if remote port is CN */
			if (!p_hca_port_group->is_cn)
				continue;

			/* obtain the LID of HCA port */
			hca_lid = p_leaf_port_group->remote_lid;

			/* set local LFT(LID) to the port that is connected to HCA */
			cl_ptr_vector_at(&p_leaf_port_group->ports, 0,
					 (void *)&p_port);
			p_sw->p_osm_sw->new_lft[hca_lid] = p_port->port_num;

			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
				"Switch %s: set path to CN LID %u through port %u\n",
				tuple_to_str(p_sw->tuple),
				hca_lid, p_port->port_num);

			/* set local min hop table(LID) to route to the CA */
			sw_set_hops(p_sw, hca_lid, p_port->port_num, 1, FALSE);

			/* Assign downgoing ports by stepping up.
			   Since we're routing here only CNs, we're routing it as REAL
			   LID and updating fat-tree balancing counters. */
			fabric_route_downgoing_by_going_up(p_ftree, p_sw,	/* local switch - used as a route-downgoing alg. start point */
							   NULL,	/* prev. position switch */
							   hca_lid,	/* LID that we're routing to */
							   TRUE,	/* whether this path to HCA should by tracked by counters */
							   FALSE,	/* whether target lid is a switch or not */
							   0,	/* Number of reverse hops allowed */
							   0,	/* Number of reverse hops done yet */
							   1);	/* Number of hops done yet */

			/* count how many real targets have been routed from this leaf switch */
			routed_targets_on_leaf++;
		}

		/* We're done with the real targets (all CNs) of this leaf switch.
		   Now route the dummy HCAs that are missing or that are non-CNs.
		   When routing to dummy HCAs we don't fill lid matrices. */
		if (p_ftree->max_cn_per_leaf > routed_targets_on_leaf) {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
				"Routing %u dummy CAs\n",
				p_ftree->max_cn_per_leaf -
				p_sw->down_port_groups_num);
			for (j = 0; j <
			     p_ftree->max_cn_per_leaf - routed_targets_on_leaf;
			     j++) {
				ftree_sw_t *p_next_sw, *p_ftree_sw;
				sw_set_hops(p_sw, 0, 0xFF, 1, FALSE);
				/* assign downgoing ports by stepping up */
				fabric_route_downgoing_by_going_up(p_ftree, p_sw,	/* local switch - used as a route-downgoing alg. start point */
								   NULL,	/* prev. position switch */
								   0,	/* LID that we're routing to - ignored for dummy HCA */
								   TRUE,	/* whether this path to HCA should by tracked by counters */
								   FALSE,	/* Whether the target LID is a switch or not */
								   0,	/* Number of reverse hops allowed */
								   0,	/* Number of reverse hops done yet */
								   1);	/* Number of hops done yet */

				p_next_sw = (ftree_sw_t *) cl_qmap_head(&p_ftree->sw_tbl);
				/* need to clean the LID 0 hops for dummy node */
				while (p_next_sw != (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl)) {
					p_ftree_sw = p_next_sw;
					p_next_sw = (ftree_sw_t *) cl_qmap_next(&p_ftree_sw->map_item);
					p_ftree_sw->hops[0] = OSM_NO_PATH;
					p_ftree_sw->p_osm_sw->new_lft[0] = OSM_NO_PATH;
				}

			}
		}
	}
	/* done going through all the leaf switches */
	OSM_LOG_EXIT(&p_ftree->p_osm->log);
}				/* fabric_route_to_cns() */

/***************************************************/

/*
 * Pseudo code:
 *    foreach HCA non-CN port in fabric
 *       obtain the LID of the HCA port
 *       get switch that is connected to this HCA port
 *       set switch LFT(LID) to the port connected to the HCA port
 *       call assign-down-going-port-by-ascending-up(TRUE,TRUE) on the switch
 *
 * Routing to these HCAs is routing a REAL hca lid on MAIN path.
 * We want to allow load-leveling of the traffic to the non-CNs,
 * because such nodes may include IO nodes with heavy usage
 *   - we should set fwd tables
 *   - we should update port counters
 * Routing to non-CNs is done after routing to CNs, so updated port
 * counters will not affect CN-to-CN routing.
 */

static void fabric_route_to_non_cns(IN ftree_fabric_t * p_ftree)
{
	ftree_sw_t *p_sw;
	ftree_hca_t *p_hca;
	ftree_hca_t *p_next_hca;
	ftree_port_t *p_hca_port;
	ftree_port_group_t *p_hca_port_group;
	uint16_t hca_lid;
	unsigned port_num_on_switch;
	unsigned i;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	p_next_hca = (ftree_hca_t *) cl_qmap_head(&p_ftree->hca_tbl);
	while (p_next_hca != (ftree_hca_t *) cl_qmap_end(&p_ftree->hca_tbl)) {
		p_hca = p_next_hca;
		p_next_hca = (ftree_hca_t *) cl_qmap_next(&p_hca->map_item);

		for (i = 0; i < p_hca->up_port_groups_num; i++) {
			p_hca_port_group = p_hca->up_port_groups[i];

			/* skip this port if it's CN, in which case it has been already routed */
			if (p_hca_port_group->is_cn)
				continue;

			/* skip this port if it is not connected to switch */
			if (p_hca_port_group->remote_node_type !=
			    IB_NODE_TYPE_SWITCH)
				continue;

			p_sw = p_hca_port_group->remote_hca_or_sw.p_sw;
			hca_lid = p_hca_port_group->lid;

			/* set switches  LFT(LID) to the port that is connected to HCA */
			cl_ptr_vector_at(&p_hca_port_group->ports, 0,
					 (void *)&p_hca_port);
			port_num_on_switch = p_hca_port->remote_port_num;
			p_sw->p_osm_sw->new_lft[hca_lid] = port_num_on_switch;

			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
				"Switch %s: set path to non-CN HCA LID %u through port %u\n",
				tuple_to_str(p_sw->tuple),
				hca_lid, port_num_on_switch);

			/* set local min hop table(LID) to route to the CA */
			sw_set_hops(p_sw, hca_lid, port_num_on_switch,	/* port num */
				    1, FALSE);	/* hops */

			/* Assign downgoing ports by stepping up.
			   We're routing REAL targets. They are not CNs and not included
			   in the leafs array, but we treat them as MAIN path to allow load
			   leveling, which means that the counters will be updated. */
			fabric_route_downgoing_by_going_up(p_ftree, p_sw,	/* local switch - used as a route-downgoing alg. start point */
							   NULL,	/* prev. position switch */
							   hca_lid,	/* LID that we're routing to */
							   TRUE,	/* whether this path to HCA should by tracked by counters */
							   FALSE,	/* Whether the target LID is a switch or not */
							   p_hca_port_group->is_io ? p_ftree->p_osm->subn.opt.max_reverse_hops : 0,	/* Number or reverse hops allowed */
							   0,	/* Number or reverse hops done yet */
							   1);	/* Number of hops done yet */
		}
		/* done with all the port groups of this HCA - go to next HCA */
	}

	OSM_LOG_EXIT(&p_ftree->p_osm->log);
}				/* fabric_route_to_non_cns() */

/***************************************************/

/*
 * Pseudo code:
 *    foreach switch in fabric
 *       obtain its LID
 *       set local LFT(LID) to port 0
 *       call assign-down-going-port-by-ascending-up(TRUE,FALSE) on CURRENT switch
 *
 * Routing to switch is similar to routing a REAL hca lid on SECONDARY path:
 *   - we should set fwd tables
 *   - we should NOT update port counters
 */

static void fabric_route_to_switches(IN ftree_fabric_t * p_ftree)
{
	ftree_sw_t *p_sw;
	ftree_sw_t *p_next_sw;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	p_next_sw = (ftree_sw_t *) cl_qmap_head(&p_ftree->sw_tbl);
	while (p_next_sw != (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (ftree_sw_t *) cl_qmap_next(&p_sw->map_item);

		/* set local LFT(LID) to 0 (route to itself) */
		p_sw->p_osm_sw->new_lft[p_sw->lid] = 0;

		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
			"Switch %s (LID %u): routing switch-to-switch paths\n",
			tuple_to_str(p_sw->tuple), p_sw->lid);

		/* set min hop table of the switch to itself */
		sw_set_hops(p_sw, p_sw->lid, 0,	/* port_num */
			    0, TRUE);	/* hops     */

		fabric_route_downgoing_by_going_up(p_ftree, p_sw,	/* local switch - used as a route-downgoing alg. start point */
						   NULL,	/* prev. position switch */
						   p_sw->lid,	/* LID that we're routing to */
						   FALSE,	/* whether this path to HCA should by tracked by counters */
						   TRUE,	/* Whether the target LID is a switch or not */
						   0,	/* Number of reverse hops allowed */
						   0,	/* Number of reverse hops done yet */
						   0);	/* Number of hops done yet */
	}

	OSM_LOG_EXIT(&p_ftree->p_osm->log);
}				/* fabric_route_to_switches() */

/***************************************************
 ***************************************************/

static void fabric_route_roots(IN ftree_fabric_t * p_ftree)
{
	uint16_t lid;
	uint8_t port_num;
	osm_port_t *p_port;
	ftree_sw_t *p_sw;
	ftree_sw_t *p_leaf_sw;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	/*
	 * We need a switch that will accomodate all the down/up turns in
	 * the fabric. Having these turn in a single place in the fabric
	 * will not create credit loops.
	 * So we need to select this switch.
	 * The idea here is to chose leaf with the highest index. I don't
	 * have any theory to back me up on this. It's just a general thought
	 * that this way the switch that might be a bottleneck for many mcast
	 * groups will be far away from the OpenSM, so it will draw the
	 * multicast traffic away from the SM.
	 */

	p_leaf_sw = p_ftree->leaf_switches[p_ftree->leaf_switches_num-1];

	/*
	 * Now go over all the switches in the fabric that
	 * have lower rank, and route the missing LIDs to
	 * the selected leaf switch.
	 * In short, this leaf switch now poses a target
	 * for all those missing LIDs.
	 */

	for (p_sw = (ftree_sw_t *) cl_qmap_head(&p_ftree->sw_tbl);
	     p_sw != (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl);
	     p_sw = (ftree_sw_t *) cl_qmap_next(&p_sw->map_item)) {

		if (p_sw->rank >= p_ftree->leaf_switch_rank)
			continue;

		for (lid = 1; lid <= p_leaf_sw->p_osm_sw->max_lid_ho; lid ++) {

			if (p_sw->p_osm_sw->new_lft[lid] != OSM_NO_PATH ||
			    p_leaf_sw->hops[lid] == OSM_NO_PATH)
				continue;

			p_port = osm_get_port_by_lid_ho(&p_ftree->p_osm->subn,
							lid);

			/* we're interested only in switches */
			if (!p_port || !p_port->p_node->sw)
				continue;

			/*
			 * the missing LID will be routed through the same
			 * port that routes to the selected leaf switch
			 */
			port_num = p_sw->p_osm_sw->new_lft[p_leaf_sw->lid];

			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
				"Switch %s: setting path to LID %u "
				"through port %u\n",
				tuple_to_str(p_sw->tuple), lid, port_num);

			/* set local lft */
			p_sw->p_osm_sw->new_lft[lid] = port_num;

			/*
			 * Set local min hop table.
			 * The distance to the target LID is a distance
			 * to the selected leaf switch plus the distance
			 * from the leaf to the target LID.
			 */
			sw_set_hops(p_sw, lid, port_num,
				p_sw->hops[p_leaf_sw->lid] +
				p_leaf_sw->hops[lid], TRUE);
		}
	}

	OSM_LOG_EXIT(&p_ftree->p_osm->log);
}				/* fabric_route_roots() */

/***************************************************/

static int fabric_populate_nodes(IN ftree_fabric_t * p_ftree)
{
	osm_node_t *p_osm_node;
	osm_node_t *p_next_osm_node;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	p_next_osm_node =
	    (osm_node_t *) cl_qmap_head(&p_ftree->p_osm->subn.node_guid_tbl);
	while (p_next_osm_node !=
	       (osm_node_t *) cl_qmap_end(&p_ftree->p_osm->
					  subn.node_guid_tbl)) {
		p_osm_node = p_next_osm_node;
		p_next_osm_node =
		    (osm_node_t *) cl_qmap_next(&p_osm_node->map_item);
		switch (osm_node_get_type(p_osm_node)) {
		case IB_NODE_TYPE_CA:
			fabric_add_hca(p_ftree, p_osm_node);
			break;
		case IB_NODE_TYPE_ROUTER:
			break;
		case IB_NODE_TYPE_SWITCH:
			fabric_add_sw(p_ftree, p_osm_node->sw);
			break;
		default:
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
				"ERR AB0E: " "Node GUID 0x%016" PRIx64
				" - Unknown node type: %s\n",
				cl_ntoh64(osm_node_get_node_guid(p_osm_node)),
				ib_get_node_type_str(osm_node_get_type
						     (p_osm_node)));
			OSM_LOG_EXIT(&p_ftree->p_osm->log);
			return -1;
		}
	}

	OSM_LOG_EXIT(&p_ftree->p_osm->log);
	return 0;
}				/* fabric_populate_nodes() */

/***************************************************
 ***************************************************/

static boolean_t sw_update_rank(IN ftree_sw_t * p_sw, IN uint32_t new_rank)
{
	if (sw_ranked(p_sw) && p_sw->rank <= new_rank)
		return FALSE;
	p_sw->rank = new_rank;
	return TRUE;

}

/***************************************************/

static void rank_switches_from_leafs(IN ftree_fabric_t * p_ftree,
				     IN cl_list_t * p_ranking_bfs_list)
{
	ftree_sw_t *p_sw;
	ftree_sw_t *p_remote_sw;
	osm_node_t *p_node;
	osm_node_t *p_remote_node;
	osm_physp_t *p_osm_port;
	uint8_t i;
	unsigned max_rank = 0;

	while (!cl_is_list_empty(p_ranking_bfs_list)) {
		p_sw = (ftree_sw_t *) cl_list_remove_head(p_ranking_bfs_list);
		p_node = p_sw->p_osm_sw->p_node;

		/* note: skipping port 0 on switches */
		for (i = 1; i < osm_node_get_num_physp(p_node); i++) {
			p_osm_port = osm_node_get_physp_ptr(p_node, i);
			if (!p_osm_port || !osm_link_is_healthy(p_osm_port))
				continue;

			p_remote_node =
			    osm_node_get_remote_node(p_node, i, NULL);
			if (!p_remote_node)
				continue;
			if (osm_node_get_type(p_remote_node) !=
			    IB_NODE_TYPE_SWITCH)
				continue;

			p_remote_sw = fabric_get_sw_by_guid(p_ftree,
							    osm_node_get_node_guid
							    (p_remote_node));
			if (!p_remote_sw) {
				/* remote node is not a switch */
				continue;
			}

			/* if needed, rank the remote switch and add it to the BFS list */
			if (sw_update_rank(p_remote_sw, p_sw->rank + 1)) {
				max_rank = p_remote_sw->rank;
				cl_list_insert_tail(p_ranking_bfs_list,
						    p_remote_sw);
			}
		}
	}

	/* set FatTree maximal switch rank */
	p_ftree->max_switch_rank = max_rank;

}				/* rank_switches_from_leafs() */

/***************************************************/

static int rank_leaf_switches(IN ftree_fabric_t * p_ftree,
			      IN ftree_hca_t * p_hca,
			      IN cl_list_t * p_ranking_bfs_list)
{
	ftree_sw_t *p_sw;
	osm_node_t *p_osm_node = p_hca->p_osm_node;
	osm_node_t *p_remote_osm_node;
	osm_physp_t *p_osm_port;
	static uint8_t i = 0;
	int res = 0;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	for (i = 0; i < osm_node_get_num_physp(p_osm_node); i++) {
		p_osm_port = osm_node_get_physp_ptr(p_osm_node, i);
		if (!p_osm_port || !osm_link_is_healthy(p_osm_port))
			continue;

		p_remote_osm_node =
		    osm_node_get_remote_node(p_osm_node, i, NULL);
		if (!p_remote_osm_node)
			continue;

		switch (osm_node_get_type(p_remote_osm_node)) {
		case IB_NODE_TYPE_CA:
			/* HCA connected directly to another HCA - not FatTree */
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
				"ERR AB0F: "
				"CA conected directly to another CA: " "0x%016"
				PRIx64 " <---> 0x%016" PRIx64 "\n",
				hca_get_guid_ho(p_hca),
				cl_ntoh64(osm_node_get_node_guid
					  (p_remote_osm_node)));
			res = -1;
			goto Exit;

		case IB_NODE_TYPE_ROUTER:
			/* leaving this port - proceeding to the next one */
			continue;

		case IB_NODE_TYPE_SWITCH:
			/* continue with this port */
			break;

		default:
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
				"ERR AB10: Node GUID 0x%016" PRIx64
				" - Unknown node type: %s\n",
				cl_ntoh64(osm_node_get_node_guid
					  (p_remote_osm_node)),
				ib_get_node_type_str(osm_node_get_type
						     (p_remote_osm_node)));
			res = -1;
			goto Exit;
		}

		/* remote node is switch */

		p_sw = fabric_get_sw_by_guid(p_ftree,
					     osm_node_get_node_guid
					     (p_osm_port->p_remote_physp->
					      p_node));
		CL_ASSERT(p_sw);

		/* if needed, rank the remote switch and add it to the BFS list */

		if (!sw_update_rank(p_sw, 0))
			continue;
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
			"Marking rank of switch that is directly connected to CA:\n"
			"                                            - CA guid    : 0x%016"
			PRIx64 "\n"
			"                                            - Switch guid: 0x%016"
			PRIx64 "\n"
			"                                            - Switch LID : %u\n",
			hca_get_guid_ho(p_hca),
			sw_get_guid_ho(p_sw), p_sw->lid);
		cl_list_insert_tail(p_ranking_bfs_list, p_sw);
	}

Exit:
	OSM_LOG_EXIT(&p_ftree->p_osm->log);
	return res;
}				/* rank_leaf_switches() */

/***************************************************/

static void sw_reverse_rank(IN cl_map_item_t * const p_map_item,
			    IN void *context)
{
	ftree_fabric_t *p_ftree = (ftree_fabric_t *) context;
	ftree_sw_t *p_sw = (ftree_sw_t * const)p_map_item;
	if (p_sw->rank != 0xFFFFFFFF)
		p_sw->rank = p_ftree->max_switch_rank - p_sw->rank;
}

/***************************************************
 ***************************************************/

static int
fabric_construct_hca_ports(IN ftree_fabric_t * p_ftree, IN ftree_hca_t * p_hca)
{
	ftree_sw_t *p_remote_sw;
	osm_node_t *p_node = p_hca->p_osm_node;
	osm_node_t *p_remote_node;
	uint8_t remote_node_type;
	ib_net64_t remote_node_guid;
	osm_physp_t *p_remote_osm_port;
	uint8_t i;
	uint8_t remote_port_num;
	boolean_t is_cn;
	boolean_t is_in_cn_file;
	boolean_t is_io;
	boolean_t is_cns_file_provided = fabric_cns_provided(p_ftree);
	boolean_t is_ios_file_provided = fabric_ios_provided(p_ftree);
	int res = 0;

	for (i = 0; i < osm_node_get_num_physp(p_node); i++) {
		osm_physp_t *p_osm_port = osm_node_get_physp_ptr(p_node, i);
		is_io = FALSE;
		is_cn = TRUE;
		is_in_cn_file = FALSE;

		if (!p_osm_port || !osm_link_is_healthy(p_osm_port))
			continue;

		if (p_hca->disconnected_ports[i])
			continue;

		p_remote_osm_port = osm_physp_get_remote(p_osm_port);
		p_remote_node =
		    osm_node_get_remote_node(p_node, i, &remote_port_num);

		if (!p_remote_osm_port || !p_remote_node)
			continue;

		remote_node_type = osm_node_get_type(p_remote_node);
		remote_node_guid = osm_node_get_node_guid(p_remote_node);

		switch (remote_node_type) {
		case IB_NODE_TYPE_ROUTER:
			/* leaving this port - proceeding to the next one */
			continue;

		case IB_NODE_TYPE_CA:
			/* HCA connected directly to another HCA - not FatTree */
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
				"ERR AB11: "
				"CA conected directly to another CA: " "0x%016"
				PRIx64 " <---> 0x%016" PRIx64 "\n",
				cl_ntoh64(osm_node_get_node_guid(p_node)),
				cl_ntoh64(remote_node_guid));
			res = -1;
			goto Exit;

		case IB_NODE_TYPE_SWITCH:
			/* continue with this port */
			break;

		default:
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
				"ERR AB12: Node GUID 0x%016" PRIx64
				" - Unknown node type: %s\n",
				cl_ntoh64(remote_node_guid),
				ib_get_node_type_str(remote_node_type));
			res = -1;
			goto Exit;
		}

		/* remote node is switch */

		p_remote_sw = fabric_get_sw_by_guid(p_ftree, remote_node_guid);
		CL_ASSERT(p_remote_sw);

		/* If CN file is not supplied, then all the CAs considered as Compute Nodes.
		   Otherwise all the CAs are not CNs, and only guids that are present in the
		   CN file will be marked as compute nodes. */
		if (is_cns_file_provided == TRUE) {
			name_map_item_t *p_elem = (name_map_item_t *)
			cl_qmap_get(&p_ftree->cn_guid_tbl,
				    cl_ntoh64(osm_physp_get_port_guid
					     (p_osm_port)));
			if (p_elem == (name_map_item_t *)
				cl_qmap_end(&p_ftree->cn_guid_tbl))
				is_cn = FALSE;
			else
				is_in_cn_file = TRUE;
		}
		if (is_in_cn_file == FALSE && is_ios_file_provided == TRUE) {
			name_map_item_t *p_elem = (name_map_item_t *)
			cl_qmap_get(&p_ftree->io_guid_tbl,
				    cl_ntoh64(osm_physp_get_port_guid
					     (p_osm_port)));
			if (p_elem != (name_map_item_t *)
				cl_qmap_end(&p_ftree->io_guid_tbl)) {
				is_io = TRUE;
				is_cn = FALSE;
			}
		}

		if (is_cn) {
			p_ftree->cn_num++;
			p_hca->cn_num++;
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
				"Marking CN port GUID 0x%016" PRIx64 "\n",
				cl_ntoh64(osm_physp_get_port_guid(p_osm_port)));
		} else if (is_io) {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
				"Marking I/O port GUID 0x%016" PRIx64 "\n",
				cl_ntoh64(osm_physp_get_port_guid(p_osm_port)));
		} else {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
				"Marking non-CN port GUID 0x%016" PRIx64 "\n",
				cl_ntoh64(osm_physp_get_port_guid(p_osm_port)));
		}
		p_ftree->ca_ports++;

		hca_add_port(p_ftree,
			     p_hca,	/* local ftree_hca object */
			     i,	/* local port number */
			     remote_port_num,	/* remote port number */
			     cl_ntoh16(osm_node_get_base_lid(p_node, i)),	/* local lid */
			     cl_ntoh16(osm_node_get_base_lid(p_remote_node, 0)),	/* remote lid */
			     osm_physp_get_port_guid(p_osm_port),	/* local port guid */
			     osm_physp_get_port_guid(p_remote_osm_port),	/* remote port guid */
			     remote_node_guid,	/* remote node guid */
			     remote_node_type,	/* remote node type */
			     (void *)p_remote_sw,	/* remote ftree_hca/sw object */
			     is_cn, is_io);	/* whether this port is compute node */
	}

Exit:
	return res;
}				/* fabric_construct_hca_ports() */

/***************************************************
 ***************************************************/

static int fabric_construct_sw_ports(IN ftree_fabric_t * p_ftree,
				     IN ftree_sw_t * p_sw)
{
	ftree_hca_t *p_remote_hca;
	ftree_sw_t *p_remote_sw;
	osm_node_t *p_node = p_sw->p_osm_sw->p_node;
	osm_node_t *p_remote_node;
	uint16_t remote_lid;
	uint8_t remote_node_type;
	ib_net64_t remote_node_guid;
	osm_physp_t *p_remote_osm_port;
	ftree_direction_t direction;
	void *p_remote_hca_or_sw;
	uint8_t i;
	uint8_t remote_port_num;
	int res = 0;

	CL_ASSERT(osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH);

	for (i = 1; i < osm_node_get_num_physp(p_node); i++) {
		osm_physp_t *p_osm_port = osm_node_get_physp_ptr(p_node, i);
		if (!p_osm_port || !osm_link_is_healthy(p_osm_port))
			continue;

		p_remote_osm_port = osm_physp_get_remote(p_osm_port);
		if (!p_remote_osm_port)
			continue;

		p_remote_node =
		    osm_node_get_remote_node(p_node, i, &remote_port_num);
		if (!p_remote_node)
			continue;

		/* ignore any loopback connection on switch */
		if (p_node == p_remote_node) {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
				"Ignoring loopback on switch GUID 0x%016" PRIx64
				", LID %u, rank %u\n",
				sw_get_guid_ho(p_sw),
				p_sw->lid, p_sw->rank);
			continue;
		}

		remote_node_type = osm_node_get_type(p_remote_node);
		remote_node_guid = osm_node_get_node_guid(p_remote_node);

		switch (remote_node_type) {
		case IB_NODE_TYPE_ROUTER:
			/* leaving this port - proceeding to the next one */
			continue;

		case IB_NODE_TYPE_CA:
			/* switch connected to hca */

			p_remote_hca =
			    fabric_get_hca_by_guid(p_ftree, remote_node_guid);
			CL_ASSERT(p_remote_hca);

			p_remote_hca_or_sw = (void *)p_remote_hca;
			direction = FTREE_DIRECTION_DOWN;

			remote_lid =
			    cl_ntoh16(osm_physp_get_base_lid(p_remote_osm_port));
			break;

		case IB_NODE_TYPE_SWITCH:
			/* switch connected to another switch */

			p_remote_sw =
			    fabric_get_sw_by_guid(p_ftree, remote_node_guid);
			CL_ASSERT(p_remote_sw);

			p_remote_hca_or_sw = (void *)p_remote_sw;

			if (p_sw->rank > p_remote_sw->rank) {
				direction = FTREE_DIRECTION_UP;
			} else if (p_sw->rank == p_remote_sw->rank) {
				direction = FTREE_DIRECTION_SAME;
			} else
				direction = FTREE_DIRECTION_DOWN;

			/* switch LID is only in port 0 port_info structure */
			remote_lid =
			    cl_ntoh16(osm_node_get_base_lid(p_remote_node, 0));

			break;

		default:
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
				"ERR AB13: Node GUID 0x%016" PRIx64
				" - Unknown node type: %s\n",
				cl_ntoh64(remote_node_guid),
				ib_get_node_type_str(remote_node_type));
			res = -1;
			goto Exit;
		}
		sw_add_port(p_sw,	/* local ftree_sw object */
			    i,	/* local port number */
			    remote_port_num,	/* remote port number */
			    p_sw->lid,	/* local lid */
			    remote_lid,	/* remote lid */
			    osm_physp_get_port_guid(p_osm_port),	/* local port guid */
			    osm_physp_get_port_guid(p_remote_osm_port),	/* remote port guid */
			    remote_node_guid,	/* remote node guid */
			    remote_node_type,	/* remote node type */
			    p_remote_hca_or_sw,	/* remote ftree_hca/sw object */
			    direction);	/* port direction (up or down) */

		/* Track the max lid (in host order) that exists in the fabric */
		if (remote_lid > p_ftree->lft_max_lid)
			p_ftree->lft_max_lid = remote_lid;
	}

Exit:
	return res;
}				/* fabric_construct_sw_ports() */

/***************************************************
 ***************************************************/
struct rank_root_cxt {
	ftree_fabric_t *fabric;
	cl_list_t *list;
};
/***************************************************
 ***************************************************/
static int rank_root_sw_by_guid(void *cxt, uint64_t guid, char *p)
{
	struct rank_root_cxt *c = cxt;
	ftree_sw_t *sw;

	sw = fabric_get_sw_by_guid(c->fabric, cl_hton64(guid));
	if (!sw) {
		/* the specified root guid wasn't found in the fabric */
		OSM_LOG(&c->fabric->p_osm->log, OSM_LOG_ERROR, "ERR AB24: "
			"Root switch GUID 0x%" PRIx64 " not found\n", guid);
		return 0;
	}

	OSM_LOG(&c->fabric->p_osm->log, OSM_LOG_DEBUG,
		"Ranking root switch with GUID 0x%" PRIx64 "\n", guid);
	sw->rank = 0;
	cl_list_insert_tail(c->list, sw);

	return 0;
}
/***************************************************
 ***************************************************/
static boolean_t fabric_load_roots(IN ftree_fabric_t * p_ftree,
				   IN cl_list_t* p_ranking_bfs_list)
{
	struct rank_root_cxt context;
	unsigned num_roots;

	if (p_ranking_bfs_list) {

		/* Rank all the roots and add them to list */
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
			"Fetching root nodes from file %s\n",
			p_ftree->p_osm->subn.opt.root_guid_file);

		context.fabric = p_ftree;
		context.list = p_ranking_bfs_list;
		if (parse_node_map(p_ftree->p_osm->subn.opt.root_guid_file,
				   rank_root_sw_by_guid, &context)) {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR, "ERR AB2A: "
				"cannot parse root guids file \'%s\'\n",
				p_ftree->p_osm->subn.opt.root_guid_file);
			return FALSE;
		}

		num_roots = cl_list_count(p_ranking_bfs_list);
		if (!num_roots) {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR, "ERR AB25: "
				"No valid roots supplied\n");
			return FALSE;
		}

		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
			"Ranked %u valid root switches\n", num_roots);
	}
	return TRUE;
}
/***************************************************
 ***************************************************/
static int fabric_rank_from_roots(IN ftree_fabric_t * p_ftree,
				  IN cl_list_t* p_ranking_bfs_list)
{
	osm_node_t *p_osm_node;
	osm_node_t *p_remote_osm_node;
	osm_physp_t *p_osm_physp;
	ftree_sw_t *p_sw;
	ftree_sw_t *p_remote_sw;
	int res = 0;
	unsigned max_rank = 0;
	unsigned i;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	if (!p_ranking_bfs_list) {
		res = -1;
		goto Exit;
	}
	while (!cl_is_list_empty(p_ranking_bfs_list)) {
		p_sw = (ftree_sw_t *) cl_list_remove_head(p_ranking_bfs_list);
		p_osm_node = p_sw->p_osm_sw->p_node;

		/* note: skipping port 0 on switches */
		for (i = 1; i < osm_node_get_num_physp(p_osm_node); i++) {
			p_osm_physp = osm_node_get_physp_ptr(p_osm_node, i);
			if (!p_osm_physp || !osm_link_is_healthy(p_osm_physp))
				continue;

			p_remote_osm_node =
			    osm_node_get_remote_node(p_osm_node, i, NULL);
			if (!p_remote_osm_node)
				continue;

			if (osm_node_get_type(p_remote_osm_node) !=
			    IB_NODE_TYPE_SWITCH)
				continue;

			p_remote_sw = fabric_get_sw_by_guid(p_ftree,
							    osm_node_get_node_guid
							    (p_remote_osm_node));
			CL_ASSERT(p_remote_sw);

			/* if needed, rank the remote switch and add it to the BFS list */
			if (sw_update_rank(p_remote_sw, p_sw->rank + 1)) {
				OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
					"Ranking switch 0x%" PRIx64
					" with rank %u\n",
					sw_get_guid_ho(p_remote_sw),
					p_remote_sw->rank);
				max_rank = p_remote_sw->rank;
				cl_list_insert_tail(p_ranking_bfs_list,
						    p_remote_sw);
			}
		}
		/* done with ports of this switch - go to the next switch in the list */
	}

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"Subnet ranking completed. Max Node Rank = %u\n", max_rank);

	/* set FatTree maximal switch rank */
	p_ftree->max_switch_rank = max_rank;

Exit:
	OSM_LOG_EXIT(&p_ftree->p_osm->log);
	return res;
}				/* fabric_rank_from_roots() */

/***************************************************
 ***************************************************/

static int fabric_rank_from_hcas(IN ftree_fabric_t * p_ftree)
{
	ftree_hca_t *p_hca;
	ftree_hca_t *p_next_hca;
	cl_list_t ranking_bfs_list;
	int res = 0;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	cl_list_init(&ranking_bfs_list, 10);

	/* Mark REVERSED rank of all the switches in the subnet.
	   Start from switches that are connected to hca's, and
	   scan all the switches in the subnet. */
	p_next_hca = (ftree_hca_t *) cl_qmap_head(&p_ftree->hca_tbl);
	while (p_next_hca != (ftree_hca_t *) cl_qmap_end(&p_ftree->hca_tbl)) {
		p_hca = p_next_hca;
		p_next_hca = (ftree_hca_t *) cl_qmap_next(&p_hca->map_item);
		if (rank_leaf_switches(p_ftree, p_hca, &ranking_bfs_list) != 0) {
			res = -1;
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
				"ERR AB14: "
				"Subnet ranking failed - subnet is not FatTree");
			goto Exit;
		}
	}

	/* Now rank rest of the switches in the fabric, while the
	   list already contains all the ranked leaf switches */
	rank_switches_from_leafs(p_ftree, &ranking_bfs_list);

	/* fix ranking of the switches by reversing the ranking direction */
	cl_qmap_apply_func(&p_ftree->sw_tbl, sw_reverse_rank, (void *)p_ftree);

Exit:
	cl_list_destroy(&ranking_bfs_list);
	OSM_LOG_EXIT(&p_ftree->p_osm->log);
	return res;
}				/* fabric_rank_from_hcas() */

/***************************************************
 * After ranking from HCA's we want to re-rank using
 * the roots
 ***************************************************/
static int fabric_rerank_using_root(IN ftree_fabric_t * p_ftree,
				    IN cl_list_t* p_ranking_bfs_list)
{
	ftree_sw_t *p_sw = NULL;
	ftree_sw_t *p_next_sw;
	int res;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	p_next_sw = (ftree_sw_t *) cl_qmap_head(&p_ftree->sw_tbl);
	while (p_next_sw != (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (ftree_sw_t *) cl_qmap_next(&p_sw->map_item);
		if (p_sw->rank == 0)
			cl_list_insert_tail(p_ranking_bfs_list, p_sw);
		else
			p_sw->rank = 0xFFFFFFFF;
	}
	res = fabric_rank_from_roots(p_ftree, p_ranking_bfs_list);
	OSM_LOG_EXIT(&p_ftree->p_osm->log);
	return res;
}
/***************************************************
 ***************************************************/
static int fabric_rank(IN ftree_fabric_t * p_ftree)
{
	int res = -1;
	cl_list_t ranking_bfs_list;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);
	cl_list_init(&ranking_bfs_list, 10);

	if (fabric_roots_provided(p_ftree) &&
	    fabric_load_roots(p_ftree, &ranking_bfs_list))
		res = fabric_rank_from_roots(p_ftree, &ranking_bfs_list);
	else {
		res = fabric_rank_from_hcas(p_ftree);
		if (!res)
			res = fabric_rerank_using_root(p_ftree, &ranking_bfs_list);
	}

	if (res)
		goto Exit;

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"FatTree max switch rank is %u\n", p_ftree->max_switch_rank);

Exit:
	cl_list_destroy(&ranking_bfs_list);
	OSM_LOG_EXIT(&p_ftree->p_osm->log);
	return res;
}				/* fabric_rank() */

/***************************************************
 ***************************************************/

static void fabric_set_leaf_rank(IN ftree_fabric_t * p_ftree)
{
	unsigned i;
	ftree_sw_t *p_sw;
	ftree_hca_t *p_hca = NULL;
	ftree_hca_t *p_next_hca;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	if (!fabric_roots_provided(p_ftree)) {
		/* If root file is not provided, the fabric has to be pure fat-tree
		   in terms of ranking. Thus, leaf switches rank is the max rank. */
		p_ftree->leaf_switch_rank = p_ftree->max_switch_rank;
	} else {
		/* Find the first CN and set the leaf_switch_rank to the rank
		   of the switch that is connected to this CN. Later we will
		   ensure that all the leaf switches have the same rank. */
		p_next_hca = (ftree_hca_t *) cl_qmap_head(&p_ftree->hca_tbl);
		while (p_next_hca !=
		       (ftree_hca_t *) cl_qmap_end(&p_ftree->hca_tbl)) {
			p_hca = p_next_hca;
			if (p_hca->cn_num)
				break;
			p_next_hca =
			    (ftree_hca_t *) cl_qmap_next(&p_hca->map_item);
		}
		/* we know that there are CNs in the fabric, so just to be sure... */
		CL_ASSERT(p_next_hca !=
			  (ftree_hca_t *) cl_qmap_end(&p_ftree->hca_tbl));

		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
			"Selected CN port GUID 0x%" PRIx64 "\n",
			hca_get_guid_ho(p_hca));

		for (i = 0; (i < p_hca->up_port_groups_num)
		     && (!p_hca->up_port_groups[i]->is_cn); i++)
			;
		CL_ASSERT(i < p_hca->up_port_groups_num);
		CL_ASSERT(p_hca->up_port_groups[i]->remote_node_type ==
			  IB_NODE_TYPE_SWITCH);

		p_sw = p_hca->up_port_groups[i]->remote_hca_or_sw.p_sw;
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
			"Selected leaf switch GUID 0x%" PRIx64 ", rank %u\n",
			sw_get_guid_ho(p_sw), p_sw->rank);
		p_ftree->leaf_switch_rank = p_sw->rank;
	}

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"FatTree leaf switch rank is %u\n", p_ftree->leaf_switch_rank);
	OSM_LOG_EXIT(&p_ftree->p_osm->log);
}				/* fabric_set_leaf_rank() */

/***************************************************
 ***************************************************/

static int fabric_populate_ports(IN ftree_fabric_t * p_ftree)
{
	ftree_hca_t *p_hca;
	ftree_hca_t *p_next_hca;
	ftree_sw_t *p_sw;
	ftree_sw_t *p_next_sw;
	int res = 0;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	p_next_hca = (ftree_hca_t *) cl_qmap_head(&p_ftree->hca_tbl);
	while (p_next_hca != (ftree_hca_t *) cl_qmap_end(&p_ftree->hca_tbl)) {
		p_hca = p_next_hca;
		p_next_hca = (ftree_hca_t *) cl_qmap_next(&p_hca->map_item);
		if (fabric_construct_hca_ports(p_ftree, p_hca) != 0) {
			res = -1;
			goto Exit;
		}
	}

	p_next_sw = (ftree_sw_t *) cl_qmap_head(&p_ftree->sw_tbl);
	while (p_next_sw != (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (ftree_sw_t *) cl_qmap_next(&p_sw->map_item);
		if (fabric_construct_sw_ports(p_ftree, p_sw) != 0) {
			res = -1;
			goto Exit;
		}
	}
Exit:
	OSM_LOG_EXIT(&p_ftree->p_osm->log);
	return res;
}				/* fabric_populate_ports() */

/***************************************************
 ***************************************************/
static int add_guid_item_to_map(void *cxt, uint64_t guid, char *p)
{
	cl_qmap_t *map = cxt;
	name_map_item_t *item;
	name_map_item_t *inserted_item;

	item = malloc(sizeof(*item));
	if (!item)
		return -1;

	item->guid = guid;
	inserted_item = (name_map_item_t *) cl_qmap_insert(map, guid, &item->item);
	if (inserted_item != item)
                free(item);

	return 0;
}

static int fabric_read_guid_files(IN ftree_fabric_t * p_ftree)
{
	int status = 0;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	if (fabric_cns_provided(p_ftree)) {
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
			"Fetching compute nodes from file %s\n",
			p_ftree->p_osm->subn.opt.cn_guid_file);

		if (parse_node_map(p_ftree->p_osm->subn.opt.cn_guid_file,
				   add_guid_item_to_map,
				   &p_ftree->cn_guid_tbl)) {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
				"ERR AB23: " "Problem parsing CN guid file\n");
			status = -1;
			goto Exit;
		}

		if (!cl_qmap_count(&p_ftree->cn_guid_tbl)) {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
				"ERR AB27: "
				"Compute node guids file has no valid guids\n");
			status = -1;
			goto Exit;
		}
	}

	if (fabric_ios_provided(p_ftree)) {
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
			"Fetching I/O nodes from file %s\n",
			p_ftree->p_osm->subn.opt.io_guid_file);

		if (parse_node_map(p_ftree->p_osm->subn.opt.io_guid_file,
				   add_guid_item_to_map,
				   &p_ftree->io_guid_tbl)) {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
				"ERR AB28: Problem parsing I/O guid file\n");
			status = -1;
			goto Exit;
		}

		if (!cl_qmap_count(&p_ftree->io_guid_tbl)) {
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
				"ERR AB29: "
				"I/O node guids file has no valid guids\n");
			status = -1;
			goto Exit;
		}
	}
Exit:
	OSM_LOG_EXIT(&p_ftree->p_osm->log);
	return status;
}				/*fabric_read_guid_files() */

/***************************************************
 ***************************************************/
/* Get a Sw and remove all depended HCA's, meaning all
 * HCA's which this is the only switch they are connected
 * to	*/
static int remove_depended_hca(IN ftree_fabric_t *p_ftree, IN ftree_sw_t *p_sw)
{
	ftree_hca_t *p_hca;
	int counter = 0;
	int port_num;
	uint8_t remote_port_num;
	osm_physp_t* physp;
	osm_node_t* sw_node;
	uint64_t remote_hca_guid;

	sw_node = p_sw->p_osm_sw->p_node;
	for (port_num = 0; port_num < sw_node->physp_tbl_size; port_num++) {
		physp = osm_node_get_physp_ptr(sw_node, port_num);
		if (physp && physp->p_remote_physp) {
			if (osm_node_get_type(physp->p_remote_physp->p_node) == IB_NODE_TYPE_CA) {
				remote_hca_guid =
				    osm_node_get_node_guid(physp->p_remote_physp->p_node);
				p_hca = fabric_get_hca_by_guid(p_ftree, remote_hca_guid);
				if (!p_hca)
					continue;

				remote_port_num =
				    osm_physp_get_port_num(physp->p_remote_physp);
				p_hca->disconnected_ports[remote_port_num] = 1;
			}
		}
	}
	return counter;
}
/***************************************************
 ***************************************************/
static void fabric_remove_unranked_sw(IN ftree_fabric_t *p_ftree)
{
	ftree_sw_t *p_sw = NULL;
	ftree_sw_t *p_next_sw;
	int removed_hca;
	int count = 0;

	p_next_sw = (ftree_sw_t *) cl_qmap_head(&p_ftree->sw_tbl);
	while (p_next_sw != (ftree_sw_t *) cl_qmap_end(&p_ftree->sw_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (ftree_sw_t *) cl_qmap_next(&p_sw->map_item);
		if (!sw_ranked(p_sw)) {
			cl_qmap_remove_item(&p_ftree->sw_tbl,&p_sw->map_item);
			removed_hca = remove_depended_hca(p_ftree, p_sw);
			OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
				"Removing Unranked sw 0x%" PRIx64 " (with %d dependent hca's)\n",
				sw_get_guid_ho(p_sw),removed_hca);
			sw_destroy(p_sw);
			count++;
		}
	}
	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
		"Removed %d invalid switches\n", count);
}
/***************************************************
 ***************************************************/
static int construct_fabric(IN void *context)
{
	ftree_fabric_t *p_ftree = context;
	int status = 0;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	fabric_clear(p_ftree);

	if (p_ftree->p_osm->subn.opt.lmc > 0) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_INFO, FILE_ID,
			   "LMC > 0 is not supported by fat-tree routing.\n"
			   "Falling back to default routing\n");
		status = -1;
		goto Exit;
	}

	if (cl_qmap_count(&p_ftree->p_osm->subn.sw_guid_tbl) < 2) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_INFO, FILE_ID,
			   "Fabric has %u switches - topology is not fat-tree.\n"
			   "Falling back to default routing\n",
			   cl_qmap_count(&p_ftree->p_osm->subn.sw_guid_tbl));
		status = -1;
		goto Exit;
	}

	if ((cl_qmap_count(&p_ftree->p_osm->subn.node_guid_tbl) -
	     cl_qmap_count(&p_ftree->p_osm->subn.sw_guid_tbl)) < 2) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_INFO, FILE_ID,
			   "Fabric has %u nodes (%u switches) - topology is not fat-tree.\n"
			   "Falling back to default routing\n",
			   cl_qmap_count(&p_ftree->p_osm->subn.node_guid_tbl),
			   cl_qmap_count(&p_ftree->p_osm->subn.sw_guid_tbl));
		status = -1;
		goto Exit;
	}

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE, "\n"
		"                       |----------------------------------------|\n"
		"                       |- Starting FatTree fabric construction -|\n"
		"                       |----------------------------------------|\n\n");

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"Populating FatTree Switch and CA tables\n");
	if (fabric_populate_nodes(p_ftree) != 0) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_INFO, FILE_ID,
			   "Fabric topology is not fat-tree - "
			   "falling back to default routing\n");
		status = -1;
		goto Exit;
	}

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"Reading guid files provided by user\n");
	if (fabric_read_guid_files(p_ftree) != 0) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_INFO, FILE_ID,
			   "Failed reading guid files - "
			   "falling back to default routing\n");
		status = -1;
		goto Exit;
	}

	if (cl_qmap_count(&p_ftree->hca_tbl) < 2) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_INFO, FILE_ID,
			   "Fabric has %u CAs - topology is not fat-tree.\n"
			   "Falling back to default routing\n",
			   cl_qmap_count(&p_ftree->hca_tbl));
		status = -1;
		goto Exit;
	}

	/* Rank all the switches in the fabric.
	   After that we will know only fabric max switch rank.
	   We will be able to check leaf switches rank and the
	   whole tree rank after filling ports and marking CNs. */
	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE, "Ranking FatTree\n");
	if (fabric_rank(p_ftree) != 0) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_INFO, FILE_ID,
			   "Failed ranking the tree\n");
		status = -1;
		goto Exit;
	}
	fabric_remove_unranked_sw(p_ftree);

	if (p_ftree->max_switch_rank == 0 &&
	    cl_qmap_count(&p_ftree->sw_tbl) > 1) {
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_ERROR,
			"ERR AB2B: Found more than one root on fabric with "
			"maximum rank 0\n");
		status = -1;
		goto Exit;
	}

	/* For each hca and switch, construct array of ports.
	   This is done after the whole FatTree data structure is ready,
	   because we want the ports to have pointers to ftree_{sw,hca}_t
	   objects, and we need the switches to be already ranked because
	   that's how the port direction is determined. */
	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"Populating CA & switch ports\n");
	if (fabric_populate_ports(p_ftree) != 0) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_INFO, FILE_ID,
			   "Fabric topology is not a fat-tree\n");
		status = -1;
		goto Exit;
	} else if (p_ftree->cn_num == 0) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_INFO, FILE_ID,
			   "Fabric has no valid compute nodes\n");
		status = -1;
		goto Exit;
	}

	/* Now that the CA ports have been created and CNs were marked,
	   we can complete the fabric ranking - set leaf switches rank. */
	fabric_set_leaf_rank(p_ftree);

	if (fabric_get_rank(p_ftree) > FAT_TREE_MAX_RANK ||
	    fabric_get_rank(p_ftree) < FAT_TREE_MIN_RANK) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_INFO, FILE_ID,
			   "Fabric rank is %u (should be between %u and %u)\n",
			   fabric_get_rank(p_ftree), FAT_TREE_MIN_RANK,
			   FAT_TREE_MAX_RANK);
		status = -1;
		goto Exit;
	}

	/* Mark all the switches in the fabric with rank equal to
	   p_ftree->leaf_switch_rank and that are also connected to CNs.
	   As a by-product, this function also runs basic topology
	   validation - it checks that all the CNs are at the same rank. */
	if (fabric_mark_leaf_switches(p_ftree)) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_INFO, FILE_ID,
			   "Fabric topology is not a fat-tree\n");
		status = -1;
		goto Exit;
	}

	/* Assign index to all the switches in the fabric.
	   This function also sorts leaf switch array by the switch index,
	   sorts all the port arrays of the indexed switches by remote
	   switch index, and creates switch-by-tuple table (sw_by_tuple_tbl) */
	fabric_make_indexing(p_ftree);

	/* Create leaf switch array sorted by index.
	   This array contains switches with rank equal to p_ftree->leaf_switch_rank
	   and that are also connected to CNs (REAL leafs), and it may contain
	   switches at the same leaf rank w/o CNs, if this is the order of indexing.
	   In any case, the first and the last switches in the array are REAL leafs. */
	if (fabric_create_leaf_switch_array(p_ftree)) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_INFO, FILE_ID,
			   "Fabric topology is not a fat-tree\n");
		status = -1;
		goto Exit;
	}

	/* calculate and set ftree.max_cn_per_leaf field */
	fabric_set_max_cn_per_leaf(p_ftree);

	/* print general info about fabric topology */
	fabric_dump_general_info(p_ftree);

	/* dump full tree topology */
	if (OSM_LOG_IS_ACTIVE_V2(&p_ftree->p_osm->log, OSM_LOG_DEBUG))
		fabric_dump(p_ftree);

	/* the fabric is required to be PURE fat-tree only if the root
	   guid file hasn't been provided by user */
	if (!fabric_roots_provided(p_ftree) &&
	    !fabric_validate_topology(p_ftree)) {
		osm_log_v2(&p_ftree->p_osm->log, OSM_LOG_INFO, FILE_ID,
			   "Fabric topology is not a fat-tree\n");
		status = -1;
		goto Exit;
	}

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"Max LID in switch LFTs: %u\n", p_ftree->lft_max_lid);

	/* Build the full lid matrices needed for multicast routing */
	osm_ucast_mgr_build_lid_matrices(&p_ftree->p_osm->sm.ucast_mgr);

Exit:
	if (status != 0) {
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
			"Clearing FatTree Fabric data structures\n");
		fabric_clear(p_ftree);
	} else
		p_ftree->fabric_built = TRUE;

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE, "\n"
		"                       |--------------------------------------------------|\n"
		"                       |- Done constructing FatTree fabric (status = %d) -|\n"
		"                       |--------------------------------------------------|\n\n",
		status);

	OSM_LOG_EXIT(&p_ftree->p_osm->log);
	return status;
}				/* construct_fabric() */

/***************************************************
 ***************************************************/

static int do_routing(IN void *context)
{
	ftree_fabric_t *p_ftree = context;
	int status = 0;

	OSM_LOG_ENTER(&p_ftree->p_osm->log);

	if (!p_ftree->fabric_built) {
		status = -1;
		goto Exit;
	}

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"Starting FatTree routing\n");

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"Filling switch forwarding tables for Compute Nodes\n");
	fabric_route_to_cns(p_ftree);

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"Filling switch forwarding tables for non-CN targets\n");
	fabric_route_to_non_cns(p_ftree);

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"Filling switch forwarding tables for switch-to-switch paths\n");
	fabric_route_to_switches(p_ftree);

	if (p_ftree->p_osm->subn.opt.connect_roots) {
		OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
			"Connecting switches that are unreachable within "
			"Up/Down rules\n");
		fabric_route_roots(p_ftree);
	}

	/* for each switch, set its fwd table */
	cl_qmap_apply_func(&p_ftree->sw_tbl, set_sw_fwd_table, (void *)p_ftree);

	/* write out hca ordering file */
	fabric_dump_hca_ordering(p_ftree);

	OSM_LOG(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
		"FatTree routing is done\n");

Exit:
	OSM_LOG_EXIT(&p_ftree->p_osm->log);
	return status;
}

/***************************************************
 ***************************************************/

static void delete(IN void *context)
{
	if (!context)
		return;
	fabric_destroy((ftree_fabric_t *) context);
}

/***************************************************
 ***************************************************/

int osm_ucast_ftree_setup(struct osm_routing_engine *r, osm_opensm_t * p_osm)
{
	ftree_fabric_t *p_ftree = fabric_create();
	if (!p_ftree)
		return -1;

	p_ftree->p_osm = p_osm;
	p_ftree->p_subn = p_osm->sm.ucast_mgr.p_subn;

	r->context = (void *)p_ftree;
	r->build_lid_matrices = construct_fabric;
	r->ucast_build_fwd_tables = do_routing;
	r->destroy = delete;

	return 0;
}
