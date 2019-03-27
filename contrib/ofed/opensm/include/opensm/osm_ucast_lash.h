/*
 * Copyright (c) 2008,2009 System Fabric Works, Inc. All rights reserved.
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2006 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2007      Simula Research Laboratory. All rights reserved.
 * Copyright (c) 2007      Silicon Graphics Inc. All rights reserved.
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
 *      Declarations for LASH algorithm
 */

#ifndef OSM_UCAST_LASH_H
#define OSM_UCAST_LASH_H

#include <opensm/osm_mesh.h>

enum {
	UNQUEUED,
	Q_MEMBER,
	MST_MEMBER,
	MAX_INT = 9999,
	NONE = MAX_INT
};

typedef struct _cdg_vertex {
	int from;
	int to;
	int seen;
	int temp;
	int visiting_number;
	struct _cdg_vertex *next;
	int num_temp_depend;
	int num_using_vertex;
	int num_deps;
	struct vertex_deps {
		struct _cdg_vertex *v;
		int num_used;
	} deps[0];
} cdg_vertex_t;

typedef struct _switch {
	osm_switch_t *p_sw;
	int id;
	int used_channels;
	int *dij_channels;
	int q_state;
	mesh_node_t *node;
	struct routing_table {
		unsigned out_link;
		unsigned lane;
	} routing_table[0];
} switch_t;

typedef struct _lash {
	osm_opensm_t *p_osm;
	int num_switches;
	uint8_t vl_min;
	int balance_limit;
	switch_t **switches;
	cdg_vertex_t ****cdg_vertex_matrix;
	int num_mst_in_lane[IB_MAX_NUM_VLS];
	int ***virtual_location;
} lash_t;

#endif
