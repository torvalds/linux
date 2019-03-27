/*
 * Copyright (c) 2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2008,2009 System Fabric Works, Inc. All rights reserved.
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
 *      Declarations for mesh analysis
 */

#ifndef OSM_MESH_H
#define OSM_MESH_H

struct _lash;
struct _switch;

/*
 * per switch to switch link info
 */
typedef struct _link {
	int switch_id;
	int link_id;
	int next_port;
	int num_ports;
	int ports[0];
} link_t;

/*
 * per switch node mesh info
 */
typedef struct _mesh_node {
	int *axes;			/* used to hold and reorder assigned axes */
	int *coord;			/* mesh coordinates of switch */
	int **matrix;			/* distances between adjacant switches */
	int *poly;			/* characteristic polynomial of matrix */
					/* used as an invariant classification */
	int dimension;			/* apparent dimension of mesh around node */
	int temp;			/* temporary holder for distance info */
	int type;			/* index of node type in mesh_info array */
	unsigned int num_links;		/* number of 'links' to adjacent switches */
	link_t *links[0];		/* per link information */
} mesh_node_t;

void osm_mesh_node_delete(struct _lash *p_lash, struct _switch *sw);
int osm_mesh_node_create(struct _lash *p_lash, struct _switch *sw);
int osm_do_mesh_analysis(struct _lash *p_lash);

#endif
