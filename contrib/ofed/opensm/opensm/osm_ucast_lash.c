/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2015 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2007      Simula Research Laboratory. All rights reserved.
 * Copyright (c) 2007      Silicon Graphics Inc. All rights reserved.
 * Copyright (c) 2008,2009 System Fabric Works, Inc. All rights reserved.
 * Copyright (c) 2009      HNR Consulting. All rights reserved.
 * Copyright (c) 2009-2011 ZIH, TU Dresden, Federal Republic of Germany. All rights reserved.
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
 *      Implementation of LASH algorithm Calculation functions
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <complib/cl_debug.h>
#include <complib/cl_qmap.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_UCAST_LASH_C
#include <opensm/osm_switch.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_log.h>
#include <opensm/osm_mesh.h>
#include <opensm/osm_ucast_lash.h>

typedef struct _reachable_dest {
	int switch_id;
	struct _reachable_dest *next;
} reachable_dest_t;

static void connect_switches(lash_t * p_lash, int sw1, int sw2, int phy_port_1)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	unsigned num = p_lash->switches[sw1]->node->num_links;
	switch_t *s1 = p_lash->switches[sw1];
	mesh_node_t *node = s1->node;
	switch_t *s2;
	link_t *l;
	unsigned int i;

	/*
	 * if doing mesh analysis:
	 *  - do not consider connections to self
	 *  - collapse multiple connections between
	 *    pair of switches to a single locical link
	 */
	if (p_lash->p_osm->subn.opt.do_mesh_analysis) {
		if (sw1 == sw2)
			return;

		/* see if we are already linked to sw2 */
		for (i = 0; i < num; i++) {
			l = node->links[i];

			if (node->links[i]->switch_id == sw2) {
				l->ports[l->num_ports++] = phy_port_1;
				return;
			}
		}
	}

	l = node->links[num];
	l->switch_id = sw2;
	l->link_id = -1;
	l->ports[l->num_ports++] = phy_port_1;

	s2 = p_lash->switches[sw2];
	for (i = 0; i < s2->node->num_links; i++) {
		if (s2->node->links[i]->switch_id == sw1) {
			s2->node->links[i]->link_id = num;
			l->link_id = i;
			break;
		}
	}

	node->num_links++;

	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"LASH connect: %d, %d, %d\n", sw1, sw2, phy_port_1);
}

static osm_switch_t *get_osm_switch_from_port(const osm_port_t * port)
{
	osm_physp_t *p = port->p_physp;
	if (p->p_node->sw)
		return p->p_node->sw;
	else if (p->p_remote_physp && p->p_remote_physp->p_node->sw)
		return p->p_remote_physp->p_node->sw;
	return NULL;
}

static int cycle_exists(cdg_vertex_t * start, cdg_vertex_t * current,
			cdg_vertex_t * prev, int visit_num)
{
	int i, new_visit_num;
	int cycle_found = 0;

	if (current != NULL && current->visiting_number > 0) {
		if (visit_num > current->visiting_number && current->seen == 0) {
			cycle_found = 1;
		}
	} else {
		if (current == NULL) {
			current = start;
			CL_ASSERT(prev == NULL);
		}

		current->visiting_number = visit_num;

		if (prev != NULL) {
			prev->next = current;
			CL_ASSERT(prev->to == current->from);
			CL_ASSERT(prev->visiting_number > 0);
		}

		new_visit_num = visit_num + 1;

		for (i = 0; i < current->num_deps; i++) {
			cycle_found =
			    cycle_exists(start, current->deps[i].v, current,
					 new_visit_num);
			if (cycle_found == 1)
				i = current->num_deps;
		}

		current->seen = 1;
		if (prev != NULL)
			prev->next = NULL;
	}

	return cycle_found;
}

static inline int get_next_switch(lash_t *p_lash, int sw, int link)
{
	return p_lash->switches[sw]->node->links[link]->switch_id;
}

static void remove_semipermanent_depend_for_sp(lash_t * p_lash, int sw,
					       int dest_switch, int lane)
{
	switch_t **switches = p_lash->switches;
	cdg_vertex_t ****cdg_vertex_matrix = p_lash->cdg_vertex_matrix;
	int i_next_switch, output_link, i, next_link, i_next_next_switch,
	    depend = 0;
	cdg_vertex_t *v;
	int __attribute__((unused)) found;

	output_link = switches[sw]->routing_table[dest_switch].out_link;
	i_next_switch = get_next_switch(p_lash, sw, output_link);

	while (sw != dest_switch) {
		v = cdg_vertex_matrix[lane][sw][i_next_switch];
		CL_ASSERT(v != NULL);

		if (v->num_using_vertex == 1) {

			cdg_vertex_matrix[lane][sw][i_next_switch] = NULL;

			free(v);
		} else {
			v->num_using_vertex--;
			if (i_next_switch != dest_switch) {
				next_link =
				    switches[i_next_switch]->routing_table[dest_switch].out_link;
				i_next_next_switch = get_next_switch(p_lash, i_next_switch, next_link);
				found = 0;

				for (i = 0; i < v->num_deps; i++)
					if (v->deps[i].v ==
					    cdg_vertex_matrix[lane][i_next_switch]
					    [i_next_next_switch]) {
						found = 1;
						depend = i;
					}

				CL_ASSERT(found);

				if (v->deps[depend].num_used == 1) {
					for (i = depend;
					     i < v->num_deps - 1; i++) {
						v->deps[i].v = v->deps[i + 1].v;
						v->deps[i].num_used =
						    v->deps[i + 1].num_used;
					}

					v->num_deps--;
				} else
					v->deps[depend].num_used--;
			}
		}

		sw = i_next_switch;
		output_link = switches[sw]->routing_table[dest_switch].out_link;

		if (sw != dest_switch)
			i_next_switch = get_next_switch(p_lash, sw, output_link);
	}
}

inline static void enqueue(cl_list_t * bfsq, switch_t * sw)
{
	CL_ASSERT(sw->q_state == UNQUEUED);
	sw->q_state = Q_MEMBER;
	cl_list_insert_tail(bfsq, sw);
}

inline static void dequeue(cl_list_t * bfsq, switch_t ** sw)
{
	*sw = (switch_t *) cl_list_remove_head(bfsq);
	CL_ASSERT((*sw)->q_state == Q_MEMBER);
	(*sw)->q_state = MST_MEMBER;
}

static int get_phys_connection(switch_t *sw, int switch_to)
{
	unsigned int i;

	for (i = 0; i < sw->node->num_links; i++)
		if (sw->node->links[i]->switch_id == switch_to)
			return i;
	return i;
}

static void shortest_path(lash_t * p_lash, int ir)
{
	switch_t **switches = p_lash->switches, *sw, *swi;
	unsigned int i;
	cl_list_t bfsq;

	cl_list_construct(&bfsq);
	cl_list_init(&bfsq, 20);

	enqueue(&bfsq, switches[ir]);

	while (!cl_is_list_empty(&bfsq)) {
		dequeue(&bfsq, &sw);
		for (i = 0; i < sw->node->num_links; i++) {
			swi = switches[sw->node->links[i]->switch_id];
			if (swi->q_state == UNQUEUED) {
				enqueue(&bfsq, swi);
				sw->dij_channels[sw->used_channels++] = swi->id;
			}
		}
	}

	cl_list_destroy(&bfsq);
}

static int generate_routing_func_for_mst(lash_t * p_lash, int sw_id,
					 reachable_dest_t ** destinations)
{
	int i, next_switch;
	switch_t *sw = p_lash->switches[sw_id];
	int num_channels = sw->used_channels;
	reachable_dest_t *dest, *i_dest, *concat_dest = NULL, *prev;

	for (i = 0; i < num_channels; i++) {
		next_switch = sw->dij_channels[i];
		if (generate_routing_func_for_mst(p_lash, next_switch, &dest))
			return -1;

		i_dest = dest;
		prev = i_dest;

		while (i_dest != NULL) {
			if (sw->routing_table[i_dest->switch_id].out_link ==
			    NONE)
				sw->routing_table[i_dest->switch_id].out_link =
				    get_phys_connection(sw, next_switch);

			prev = i_dest;
			i_dest = i_dest->next;
		}

		CL_ASSERT(prev->next == NULL);
		prev->next = concat_dest;
		concat_dest = dest;
	}

	i_dest = (reachable_dest_t *) malloc(sizeof(reachable_dest_t));
	if (!i_dest)
		return -1;
	i_dest->switch_id = sw->id;
	i_dest->next = concat_dest;
	*destinations = i_dest;
	return 0;
}

static int generate_cdg_for_sp(lash_t * p_lash, int sw, int dest_switch,
			       int lane)
{
	unsigned num_switches = p_lash->num_switches;
	switch_t **switches = p_lash->switches;
	cdg_vertex_t ****cdg_vertex_matrix = p_lash->cdg_vertex_matrix;
	int next_switch, output_link, j, exists;
	cdg_vertex_t *v, *prev = NULL;

	output_link = switches[sw]->routing_table[dest_switch].out_link;
	next_switch = get_next_switch(p_lash, sw, output_link);

	while (sw != dest_switch) {

		if (cdg_vertex_matrix[lane][sw][next_switch] == NULL) {
			v = calloc(1, sizeof(*v) + (num_switches - 1) * sizeof(v->deps[0]));
			if (!v)
				return -1;
			v->from = sw;
			v->to = next_switch;
			v->temp = 1;
			cdg_vertex_matrix[lane][sw][next_switch] = v;
		} else
			v = cdg_vertex_matrix[lane][sw][next_switch];

		v->num_using_vertex++;

		if (prev != NULL) {
			exists = 0;

			for (j = 0; j < prev->num_deps; j++)
				if (prev->deps[j].v == v) {
					exists = 1;
					prev->deps[j].num_used++;
				}

			if (exists == 0) {
				prev->deps[prev->num_deps].v = v;
				prev->deps[prev->num_deps].num_used++;
				prev->num_deps++;

				CL_ASSERT(prev->num_deps < (int)num_switches);

				if (prev->temp == 0)
					prev->num_temp_depend++;

			}
		}

		sw = next_switch;
		output_link = switches[sw]->routing_table[dest_switch].out_link;

		if (sw != dest_switch) {
			CL_ASSERT(output_link != NONE);
			next_switch = get_next_switch(p_lash, sw, output_link);
		}

		prev = v;
	}
	return 0;
}

static void set_temp_depend_to_permanent_for_sp(lash_t * p_lash, int sw,
						int dest_switch, int lane)
{
	switch_t **switches = p_lash->switches;
	cdg_vertex_t ****cdg_vertex_matrix = p_lash->cdg_vertex_matrix;
	int next_switch, output_link;
	cdg_vertex_t *v;

	output_link = switches[sw]->routing_table[dest_switch].out_link;
	next_switch = get_next_switch(p_lash, sw, output_link);

	while (sw != dest_switch) {
		v = cdg_vertex_matrix[lane][sw][next_switch];
		CL_ASSERT(v != NULL);

		if (v->temp == 1)
			v->temp = 0;
		else
			v->num_temp_depend = 0;

		sw = next_switch;
		output_link = switches[sw]->routing_table[dest_switch].out_link;

		if (sw != dest_switch)
			next_switch = get_next_switch(p_lash, sw, output_link);
	}

}

static void remove_temp_depend_for_sp(lash_t * p_lash, int sw, int dest_switch,
				      int lane)
{
	switch_t **switches = p_lash->switches;
	cdg_vertex_t ****cdg_vertex_matrix = p_lash->cdg_vertex_matrix;
	int next_switch, output_link, i;
	cdg_vertex_t *v;

	output_link = switches[sw]->routing_table[dest_switch].out_link;
	next_switch = get_next_switch(p_lash, sw, output_link);

	while (sw != dest_switch) {
		v = cdg_vertex_matrix[lane][sw][next_switch];
		CL_ASSERT(v != NULL);

		if (v->temp == 1) {
			cdg_vertex_matrix[lane][sw][next_switch] = NULL;
			free(v);
		} else {
			CL_ASSERT(v->num_temp_depend <= v->num_deps);
			v->num_deps = v->num_deps - v->num_temp_depend;
			v->num_temp_depend = 0;
			v->num_using_vertex--;

			for (i = v->num_deps; i < p_lash->num_switches - 1; i++)
				v->deps[i].num_used = 0;
		}

		sw = next_switch;
		output_link = switches[sw]->routing_table[dest_switch].out_link;

		if (sw != dest_switch)
			next_switch = get_next_switch(p_lash, sw, output_link);

	}
}

static int balance_virtual_lanes(lash_t * p_lash, unsigned lanes_needed)
{
	unsigned num_switches = p_lash->num_switches;
	cdg_vertex_t ****cdg_vertex_matrix = p_lash->cdg_vertex_matrix;
	int *num_mst_in_lane = p_lash->num_mst_in_lane;
	int ***virtual_location = p_lash->virtual_location;
	int min_filled_lane, max_filled_lane, trials;
	int old_min_filled_lane, old_max_filled_lane, new_num_min_lane,
	    new_num_max_lane;
	unsigned int i, j;
	int src, dest, start, next_switch, output_link;
	int next_switch2, output_link2;
	int stop = 0, cycle_found;
	int cycle_found2;
	unsigned start_vl = p_lash->p_osm->subn.opt.lash_start_vl;

	max_filled_lane = 0;
	min_filled_lane = lanes_needed - 1;

	trials = num_mst_in_lane[max_filled_lane];
	if (lanes_needed == 1)
		stop = 1;

	while (stop == 0) {
		src = abs(rand()) % (num_switches);
		dest = abs(rand()) % (num_switches);

		while (virtual_location[src][dest][max_filled_lane] != 1) {
			start = dest;
			if (dest == num_switches - 1)
				dest = 0;
			else
				dest++;

			while (dest != start
			       && virtual_location[src][dest][max_filled_lane]
			       != 1) {
				if (dest == num_switches - 1)
					dest = 0;
				else
					dest++;
			}

			if (virtual_location[src][dest][max_filled_lane] != 1) {
				if (src == num_switches - 1)
					src = 0;
				else
					src++;
			}
		}

		if (generate_cdg_for_sp(p_lash, src, dest, min_filled_lane) ||
		    generate_cdg_for_sp(p_lash, dest, src, min_filled_lane))
			return -1;

		output_link = p_lash->switches[src]->routing_table[dest].out_link;
		next_switch = get_next_switch(p_lash, src, output_link);

		output_link2 = p_lash->switches[dest]->routing_table[src].out_link;
		next_switch2 = get_next_switch(p_lash, dest, output_link2);

		CL_ASSERT(cdg_vertex_matrix[min_filled_lane][src][next_switch] != NULL);
		CL_ASSERT(cdg_vertex_matrix[min_filled_lane][dest][next_switch2] != NULL);

		cycle_found =
		    cycle_exists(cdg_vertex_matrix[min_filled_lane][src][next_switch], NULL, NULL,
				 1);
		cycle_found2 =
		    cycle_exists(cdg_vertex_matrix[min_filled_lane][dest][next_switch2], NULL, NULL,
				 1);

		for (i = 0; i < num_switches; i++)
			for (j = 0; j < num_switches; j++)
				if (cdg_vertex_matrix[min_filled_lane][i][j] != NULL) {
					cdg_vertex_matrix[min_filled_lane][i][j]->visiting_number =
					    0;
					cdg_vertex_matrix[min_filled_lane][i][j]->seen = 0;
				}

		if (cycle_found == 1 || cycle_found2 == 1) {
			remove_temp_depend_for_sp(p_lash, src, dest, min_filled_lane);
			remove_temp_depend_for_sp(p_lash, dest, src, min_filled_lane);

			virtual_location[src][dest][max_filled_lane] = 2;
			virtual_location[dest][src][max_filled_lane] = 2;
			trials--;
			trials--;
		} else {
			set_temp_depend_to_permanent_for_sp(p_lash, src, dest, min_filled_lane);
			set_temp_depend_to_permanent_for_sp(p_lash, dest, src, min_filled_lane);

			num_mst_in_lane[max_filled_lane]--;
			num_mst_in_lane[max_filled_lane]--;
			num_mst_in_lane[min_filled_lane]++;
			num_mst_in_lane[min_filled_lane]++;

			remove_semipermanent_depend_for_sp(p_lash, src, dest, max_filled_lane);
			remove_semipermanent_depend_for_sp(p_lash, dest, src, max_filled_lane);
			virtual_location[src][dest][max_filled_lane] = 0;
			virtual_location[dest][src][max_filled_lane] = 0;
			virtual_location[src][dest][min_filled_lane] = 1;
			virtual_location[dest][src][min_filled_lane] = 1;
			p_lash->switches[src]->routing_table[dest].lane = min_filled_lane + start_vl;
			p_lash->switches[dest]->routing_table[src].lane = min_filled_lane + start_vl;
		}

		if (trials == 0)
			stop = 1;
		else {
			if (num_mst_in_lane[max_filled_lane] - num_mst_in_lane[min_filled_lane] <
			    p_lash->balance_limit)
				stop = 1;
		}

		old_min_filled_lane = min_filled_lane;
		old_max_filled_lane = max_filled_lane;

		new_num_min_lane = MAX_INT;
		new_num_max_lane = 0;

		for (i = 0; i < lanes_needed; i++) {

			if (num_mst_in_lane[i] < new_num_min_lane) {
				new_num_min_lane = num_mst_in_lane[i];
				min_filled_lane = i;
			}

			if (num_mst_in_lane[i] > new_num_max_lane) {
				new_num_max_lane = num_mst_in_lane[i];
				max_filled_lane = i;
			}
		}

		if (old_min_filled_lane != min_filled_lane) {
			trials = num_mst_in_lane[max_filled_lane];
			for (i = 0; i < num_switches; i++)
				for (j = 0; j < num_switches; j++)
					if (virtual_location[i][j][max_filled_lane] == 2)
						virtual_location[i][j][max_filled_lane] = 1;
		}

		if (old_max_filled_lane != max_filled_lane) {
			trials = num_mst_in_lane[max_filled_lane];
			for (i = 0; i < num_switches; i++)
				for (j = 0; j < num_switches; j++)
					if (virtual_location[i][j][old_max_filled_lane] == 2)
						virtual_location[i][j][old_max_filled_lane] = 1;
		}
	}
	return 0;
}

static switch_t *switch_create(lash_t * p_lash, unsigned id, osm_switch_t * p_sw)
{
	unsigned num_switches = p_lash->num_switches;
	unsigned num_ports = p_sw->num_ports;
	switch_t *sw;
	unsigned int i;

	sw = malloc(sizeof(*sw) + num_switches * sizeof(sw->routing_table[0]));
	if (!sw)
		return NULL;

	memset(sw, 0, sizeof(*sw));
	for (i = 0; i < num_switches; i++) {
		sw->routing_table[i].out_link = NONE;
		sw->routing_table[i].lane = NONE;
	}

	sw->id = id;
	sw->dij_channels = malloc(num_ports * sizeof(int));
	if (!sw->dij_channels) {
		free(sw);
		return NULL;
	}

	sw->p_sw = p_sw;
	p_sw->priv = sw;

	if (osm_mesh_node_create(p_lash, sw)) {
		free(sw->dij_channels);
		free(sw);
		return NULL;
	}

	return sw;
}

static void switch_delete(lash_t *p_lash, switch_t * sw)
{
	if (sw->dij_channels)
		free(sw->dij_channels);
	free(sw);
}

static void delete_mesh_switches(lash_t *p_lash)
{
	if (p_lash->switches) {
		unsigned id;
		for (id = 0; ((int)id) < p_lash->num_switches; id++)
			if (p_lash->switches[id])
				osm_mesh_node_delete(p_lash,
						     p_lash->switches[id]);
	}
}

static void free_lash_structures(lash_t * p_lash)
{
	unsigned int i, j, k;
	unsigned num_switches = p_lash->num_switches;
	osm_log_t *p_log = &p_lash->p_osm->log;

	OSM_LOG_ENTER(p_log);

	delete_mesh_switches(p_lash);

	/* free cdg_vertex_matrix */
	for (i = 0; i < p_lash->vl_min; i++) {
		for (j = 0; j < num_switches; j++) {
			for (k = 0; k < num_switches; k++)
				if (p_lash->cdg_vertex_matrix[i][j][k])
					free(p_lash->cdg_vertex_matrix[i][j][k]);
			if (p_lash->cdg_vertex_matrix[i][j])
				free(p_lash->cdg_vertex_matrix[i][j]);
		}
		if (p_lash->cdg_vertex_matrix[i])
			free(p_lash->cdg_vertex_matrix[i]);
	}

	if (p_lash->cdg_vertex_matrix)
		free(p_lash->cdg_vertex_matrix);

	/* free virtual_location */
	for (i = 0; i < num_switches; i++) {
		for (j = 0; j < num_switches; j++) {
			if (p_lash->virtual_location[i][j])
				free(p_lash->virtual_location[i][j]);
		}
		if (p_lash->virtual_location[i])
			free(p_lash->virtual_location[i]);
	}
	if (p_lash->virtual_location)
		free(p_lash->virtual_location);

	OSM_LOG_EXIT(p_log);
}

static int init_lash_structures(lash_t * p_lash)
{
	unsigned vl_min = p_lash->vl_min;
	unsigned num_switches = p_lash->num_switches;
	osm_log_t *p_log = &p_lash->p_osm->log;
	int status = 0;
	unsigned int i, j, k;

	OSM_LOG_ENTER(p_log);

	/* initialise cdg_vertex_matrix[num_switches][num_switches][num_switches] */
	p_lash->cdg_vertex_matrix =
	    (cdg_vertex_t ****) malloc(vl_min * sizeof(cdg_vertex_t ***));
	if (p_lash->cdg_vertex_matrix == NULL)
		goto Exit_Mem_Error;
	for (i = 0; i < vl_min; i++) {
		p_lash->cdg_vertex_matrix[i] =
		    (cdg_vertex_t ***) malloc(num_switches *
					      sizeof(cdg_vertex_t **));

		if (p_lash->cdg_vertex_matrix[i] == NULL)
			goto Exit_Mem_Error;
	}

	for (i = 0; i < vl_min; i++) {
		for (j = 0; j < num_switches; j++) {
			p_lash->cdg_vertex_matrix[i][j] =
			    (cdg_vertex_t **) malloc(num_switches *
						     sizeof(cdg_vertex_t *));
			if (p_lash->cdg_vertex_matrix[i][j] == NULL)
				goto Exit_Mem_Error;

			for (k = 0; k < num_switches; k++)
				p_lash->cdg_vertex_matrix[i][j][k] = NULL;
		}
	}

	/*
	 * initialise virtual_location[num_switches][num_switches][num_layers],
	 * default value = 0
	 */
	p_lash->virtual_location =
	    (int ***)malloc(num_switches * sizeof(int ***));
	if (p_lash->virtual_location == NULL)
		goto Exit_Mem_Error;

	for (i = 0; i < num_switches; i++) {
		p_lash->virtual_location[i] =
		    (int **)malloc(num_switches * sizeof(int **));
		if (p_lash->virtual_location[i] == NULL)
			goto Exit_Mem_Error;
	}

	for (i = 0; i < num_switches; i++) {
		for (j = 0; j < num_switches; j++) {
			p_lash->virtual_location[i][j] =
			    (int *)malloc(vl_min * sizeof(int *));
			if (p_lash->virtual_location[i][j] == NULL)
				goto Exit_Mem_Error;
			for (k = 0; k < vl_min; k++)
				p_lash->virtual_location[i][j][k] = 0;
		}
	}

	/* initialise num_mst_in_lane[num_switches], default 0 */
	memset(p_lash->num_mst_in_lane, 0,
	       IB_MAX_NUM_VLS * sizeof(p_lash->num_mst_in_lane[0]));

	goto Exit;

Exit_Mem_Error:
	status = -1;
	OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 4D01: "
		"Could not allocate required memory for LASH errno %d, errno %d for lack of memory\n",
		errno, ENOMEM);

Exit:
	OSM_LOG_EXIT(p_log);
	return status;
}

static int lash_core(lash_t * p_lash)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	unsigned num_switches = p_lash->num_switches;
	switch_t **switches = p_lash->switches;
	unsigned lanes_needed = 1;
	unsigned int i, j, k, dest_switch = 0;
	reachable_dest_t *dests, *idest;
	int cycle_found = 0;
	unsigned v_lane;
	int stop = 0, output_link, i_next_switch;
	int output_link2, i_next_switch2;
	int cycle_found2 = 0;
	int status = -1;
	int *switch_bitmap = NULL;	/* Bitmap to check if we have processed this pair */
	unsigned start_vl = p_lash->p_osm->subn.opt.lash_start_vl;

	OSM_LOG_ENTER(p_log);

	if (p_lash->p_osm->subn.opt.do_mesh_analysis && osm_do_mesh_analysis(p_lash)) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 4D05: Mesh analysis failed\n");
		goto Exit;
	}

	for (i = 0; i < num_switches; i++) {

		shortest_path(p_lash, i);
		if (generate_routing_func_for_mst(p_lash, i, &dests)) {
			OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 4D06: "
				"generate_routing_func_for_mst failed\n");
			goto Exit;
		}

		idest = dests;
		while (idest != NULL) {
			dests = dests->next;
			free(idest);
			idest = dests;
		}

		for (j = 0; j < num_switches; j++) {
			switches[j]->used_channels = 0;
			switches[j]->q_state = UNQUEUED;
		}
	}

	switch_bitmap = calloc(num_switches * num_switches, sizeof(int));
	if (!switch_bitmap) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 4D04: "
			"Failed allocating switch_bitmap - out of memory\n");
		goto Exit;
	}

	for (i = 0; i < num_switches; i++) {
		for (dest_switch = 0; dest_switch < num_switches; dest_switch++)
			if (dest_switch != i && switch_bitmap[i * num_switches + dest_switch] == 0) {
				v_lane = 0;
				stop = 0;
				while (v_lane < lanes_needed && stop == 0) {
					if (generate_cdg_for_sp(p_lash, i, dest_switch, v_lane) ||
					    generate_cdg_for_sp(p_lash, dest_switch, i, v_lane)) {
						OSM_LOG(p_log, OSM_LOG_ERROR,
							"ERR 4D07: generate_cdg_for_sp failed\n");
						goto Exit;
					}

					output_link =
					    switches[i]->routing_table[dest_switch].out_link;
					output_link2 =
					    switches[dest_switch]->routing_table[i].out_link;

					i_next_switch = get_next_switch(p_lash, i, output_link);
					i_next_switch2 = get_next_switch(p_lash, dest_switch, output_link2);

					CL_ASSERT(p_lash->
						  cdg_vertex_matrix[v_lane][i][i_next_switch] !=
						  NULL);
					CL_ASSERT(p_lash->
						  cdg_vertex_matrix[v_lane][dest_switch]
						  [i_next_switch2] != NULL);

					cycle_found =
					    cycle_exists(p_lash->
							 cdg_vertex_matrix[v_lane][i]
							 [i_next_switch], NULL, NULL, 1);
					cycle_found2 =
					    cycle_exists(p_lash->
							 cdg_vertex_matrix[v_lane][dest_switch]
							 [i_next_switch2], NULL, NULL, 1);

					for (j = 0; j < num_switches; j++)
						for (k = 0; k < num_switches; k++)
							if (p_lash->
							    cdg_vertex_matrix[v_lane][j][k] !=
							    NULL) {
								p_lash->
								    cdg_vertex_matrix[v_lane][j]
								    [k]->visiting_number = 0;
								p_lash->
								    cdg_vertex_matrix[v_lane][j]
								    [k]->seen = 0;
							}

					if (cycle_found == 1 || cycle_found2 == 1) {
						remove_temp_depend_for_sp(p_lash, i, dest_switch,
									  v_lane);
						remove_temp_depend_for_sp(p_lash, dest_switch, i,
									  v_lane);
						v_lane++;
					} else {
						set_temp_depend_to_permanent_for_sp(p_lash, i,
										    dest_switch,
										    v_lane);
						set_temp_depend_to_permanent_for_sp(p_lash,
										    dest_switch, i,
										    v_lane);
						stop = 1;
						p_lash->num_mst_in_lane[v_lane]++;
						p_lash->num_mst_in_lane[v_lane]++;
					}
				}

				switches[i]->routing_table[dest_switch].lane = v_lane + start_vl;
				switches[dest_switch]->routing_table[i].lane = v_lane + start_vl;

				if (cycle_found == 1 || cycle_found2 == 1) {
					if (++lanes_needed > p_lash->vl_min)
						goto Error_Not_Enough_Lanes;

					if (generate_cdg_for_sp(p_lash, i, dest_switch, v_lane) ||
					    generate_cdg_for_sp(p_lash, dest_switch, i, v_lane)) {
						OSM_LOG(p_log, OSM_LOG_ERROR,
							"ERR 4D08: generate_cdg_for_sp failed\n");
						goto Exit;
					}

					set_temp_depend_to_permanent_for_sp(p_lash, i, dest_switch,
									    v_lane);
					set_temp_depend_to_permanent_for_sp(p_lash, dest_switch, i,
									    v_lane);

					p_lash->num_mst_in_lane[v_lane]++;
					p_lash->num_mst_in_lane[v_lane]++;
				}
				p_lash->virtual_location[i][dest_switch][v_lane] = 1;
				p_lash->virtual_location[dest_switch][i][v_lane] = 1;

				switch_bitmap[i * num_switches + dest_switch] = 1;
				switch_bitmap[dest_switch * num_switches + i] = 1;
			}
	}

	for (i = 0; i < lanes_needed; i++)
		OSM_LOG(p_log, OSM_LOG_INFO, "Lanes in layer %d: %d\n",
			i, p_lash->num_mst_in_lane[i]);

	OSM_LOG(p_log, OSM_LOG_INFO,
		"Lanes needed: %d, Balancing\n", lanes_needed);

	if (balance_virtual_lanes(p_lash, lanes_needed)) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 4D09: Balancing failed\n");
		goto Exit;
	}

	for (i = 0; i < lanes_needed; i++)
		OSM_LOG(p_log, OSM_LOG_INFO, "Lanes in layer %d: %d\n",
			i, p_lash->num_mst_in_lane[i]);

	status = 0;
	goto Exit;

Error_Not_Enough_Lanes:
	OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 4D02: "
		"Lane requirements (%d) exceed available lanes (%d)"
		" with starting lane (%d)\n",
		lanes_needed, p_lash->vl_min, start_vl);
Exit:
	if (switch_bitmap)
		free(switch_bitmap);
	OSM_LOG_EXIT(p_log);
	return status;
}

static unsigned get_lash_id(osm_switch_t * p_sw)
{
	return ((switch_t *) p_sw->priv)->id;
}

static int get_next_port(switch_t *sw, int link)
{
	link_t *l = sw->node->links[link];
	int port = l->next_port++;

	/*
	 * note if not doing mesh analysis
	 * then num_ports is always 1
	 */
	if (l->next_port >= l->num_ports)
		l->next_port = 0;

	return l->ports[port];
}

static void populate_fwd_tbls(lash_t * p_lash)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	osm_subn_t *p_subn = &p_lash->p_osm->subn;
	osm_switch_t *p_sw, *p_next_sw, *p_dst_sw;
	osm_port_t *port;
	uint16_t max_lid_ho, lid;

	OSM_LOG_ENTER(p_log);

	p_next_sw = (osm_switch_t *) cl_qmap_head(&p_subn->sw_guid_tbl);

	/* Go through each switch individually */
	while (p_next_sw != (osm_switch_t *) cl_qmap_end(&p_subn->sw_guid_tbl)) {
		uint64_t current_guid;
		switch_t *sw;
		p_sw = p_next_sw;
		p_next_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item);

		max_lid_ho = p_sw->max_lid_ho;
		current_guid = p_sw->p_node->node_info.port_guid;
		sw = p_sw->priv;

		memset(p_sw->new_lft, OSM_NO_PATH, p_sw->lft_size);

		for (lid = 1; lid <= max_lid_ho; lid++) {
			port = osm_get_port_by_lid_ho(p_subn, lid);
			if (!port)
				continue;

			p_dst_sw = get_osm_switch_from_port(port);
			if (p_dst_sw == p_sw) {
				uint8_t egress_port = port->p_node->sw ? 0 :
					port->p_physp->p_remote_physp->port_num;
				p_sw->new_lft[lid] = egress_port;
				OSM_LOG(p_log, OSM_LOG_VERBOSE,
					"LASH fwd MY SRC SRC GUID 0x%016" PRIx64
					" src lash id (%d), src lid no (%u) src lash port (%d) "
					"DST GUID 0x%016" PRIx64
					" src lash id (%d), src lash port (%d)\n",
					cl_ntoh64(current_guid), -1, lid,
					egress_port, cl_ntoh64(current_guid),
					-1, egress_port);
			} else if (p_dst_sw) {
				unsigned dst_lash_switch_id =
				    get_lash_id(p_dst_sw);
				uint8_t lash_egress_port =
				    (uint8_t) sw->
				    routing_table[dst_lash_switch_id].out_link;
				uint8_t physical_egress_port =
					get_next_port(sw, lash_egress_port);

				p_sw->new_lft[lid] = physical_egress_port;
				OSM_LOG(p_log, OSM_LOG_VERBOSE,
					"LASH fwd SRC GUID 0x%016" PRIx64
					" src lash id (%d), "
					"src lid no (%u) src lash port (%d) "
					"DST GUID 0x%016" PRIx64
					" src lash id (%d), src lash port (%d)\n",
					cl_ntoh64(current_guid), sw->id, lid,
					lash_egress_port,
					cl_ntoh64(p_dst_sw->p_node->node_info.
						  port_guid),
					dst_lash_switch_id,
					physical_egress_port);
			}
		}		/* for */
	}
	OSM_LOG_EXIT(p_log);
}

static void osm_lash_process_switch(lash_t * p_lash, osm_switch_t * p_sw)
{
	osm_log_t *p_log = &p_lash->p_osm->log;
	int i, port_count;
	osm_physp_t *p_current_physp, *p_remote_physp;
	unsigned switch_a_lash_id, switch_b_lash_id;

	OSM_LOG_ENTER(p_log);

	switch_a_lash_id = get_lash_id(p_sw);
	port_count = osm_node_get_num_physp(p_sw->p_node);

	/* starting at port 1, ignoring management port on switch */
	for (i = 1; i < port_count; i++) {

		p_current_physp = osm_node_get_physp_ptr(p_sw->p_node, i);
		if (p_current_physp) {
			p_remote_physp = p_current_physp->p_remote_physp;
			if (p_remote_physp && p_remote_physp->p_node->sw) {
				int physical_port_a_num =
				    osm_physp_get_port_num(p_current_physp);
				int physical_port_b_num =
				    osm_physp_get_port_num(p_remote_physp);
				switch_b_lash_id =
				    get_lash_id(p_remote_physp->p_node->sw);

				connect_switches(p_lash, switch_a_lash_id,
						 switch_b_lash_id,
						 physical_port_a_num);
				OSM_LOG(p_log, OSM_LOG_VERBOSE,
					"LASH SUCCESS connected G 0x%016" PRIx64
					" , lash_id(%u), P(%u) " " to G 0x%016"
					PRIx64 " , lash_id(%u) , P(%u)\n",
					cl_ntoh64(osm_physp_get_port_guid
						  (p_current_physp)),
					switch_a_lash_id, physical_port_a_num,
					cl_ntoh64(osm_physp_get_port_guid
						  (p_remote_physp)),
					switch_b_lash_id, physical_port_b_num);
			}
		}
	}

	OSM_LOG_EXIT(p_log);
}

static void lash_cleanup(lash_t * p_lash)
{
	osm_subn_t *p_subn = &p_lash->p_osm->subn;
	osm_switch_t *p_next_sw, *p_sw;

	/* drop any existing references to old lash switches */
	p_next_sw = (osm_switch_t *) cl_qmap_head(&p_subn->sw_guid_tbl);
	while (p_next_sw != (osm_switch_t *) cl_qmap_end(&p_subn->sw_guid_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item);
		p_sw->priv = NULL;
	}

	if (p_lash->switches) {
		unsigned id;
		for (id = 0; ((int)id) < p_lash->num_switches; id++)
			if (p_lash->switches[id])
				switch_delete(p_lash, p_lash->switches[id]);
		free(p_lash->switches);
	}
	p_lash->switches = NULL;
}

/*
  static int  discover_network_properties()
  Traverse the topology of the network in order to determine
   - the maximum number of switches,
   - the minimum number of virtual layers
*/

static int discover_network_properties(lash_t * p_lash)
{
	int i, id = 0;
	uint8_t vl_min;
	osm_subn_t *p_subn = &p_lash->p_osm->subn;
	osm_switch_t *p_next_sw, *p_sw;
	osm_log_t *p_log = &p_lash->p_osm->log;

	p_lash->num_switches = cl_qmap_count(&p_subn->sw_guid_tbl);

	p_lash->switches = calloc(p_lash->num_switches, sizeof(switch_t *));
	if (!p_lash->switches)
		return -1;

	vl_min = 5;		/* set to a high value */

	p_next_sw = (osm_switch_t *) cl_qmap_head(&p_subn->sw_guid_tbl);
	while (p_next_sw != (osm_switch_t *) cl_qmap_end(&p_subn->sw_guid_tbl)) {
		uint16_t port_count;
		p_sw = p_next_sw;
		p_next_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item);

		p_lash->switches[id] = switch_create(p_lash, id, p_sw);
		if (!p_lash->switches[id])
			return -1;
		id++;

		port_count = osm_node_get_num_physp(p_sw->p_node);

		/* Note, ignoring port 0. management port */
		for (i = 1; i < port_count; i++) {
			osm_physp_t *p_current_physp =
			    osm_node_get_physp_ptr(p_sw->p_node, i);

			if (p_current_physp
			    && p_current_physp->p_remote_physp) {

				ib_port_info_t *p_port_info =
				    &p_current_physp->port_info;
				uint8_t port_vl_min =
				    ib_port_info_get_op_vls(p_port_info);
				if (port_vl_min && port_vl_min < vl_min)
					vl_min = port_vl_min;
			}
		}		/* for */
	}			/* while */

	vl_min = 1 << (vl_min - 1);
	if (vl_min > 15)
		vl_min = 15;

	if (p_lash->p_osm->subn.opt.lash_start_vl >= vl_min) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 4D03: "
			"Start VL(%d) too high for min operational vl(%d)\n",
			p_lash->p_osm->subn.opt.lash_start_vl, vl_min);
		return -1;
	}

	p_lash->vl_min = vl_min - p_lash->p_osm->subn.opt.lash_start_vl;

	OSM_LOG(p_log, OSM_LOG_INFO,
		"min operational vl(%d) start vl(%d) max_switches(%d)\n",
		p_lash->vl_min, p_lash->p_osm->subn.opt.lash_start_vl,
		p_lash->num_switches);
	return 0;
}

static void process_switches(lash_t * p_lash)
{
	osm_switch_t *p_sw, *p_next_sw;
	osm_subn_t *p_subn = &p_lash->p_osm->subn;

	/* Go through each switch and process it. i.e build the connection
	   structure required by LASH */
	p_next_sw = (osm_switch_t *) cl_qmap_head(&p_subn->sw_guid_tbl);
	while (p_next_sw != (osm_switch_t *) cl_qmap_end(&p_subn->sw_guid_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item);

		osm_lash_process_switch(p_lash, p_sw);
	}
}

static int lash_process(void *context)
{
	lash_t *p_lash = context;
	osm_log_t *p_log = &p_lash->p_osm->log;
	int status = 0;

	OSM_LOG_ENTER(p_log);

	p_lash->balance_limit = 6;

	/* everything starts here */
	lash_cleanup(p_lash);

	status = discover_network_properties(p_lash);
	if (status)
		goto Exit;

	status = init_lash_structures(p_lash);
	if (status)
		goto Exit;

	process_switches(p_lash);

	status = lash_core(p_lash);
	if (status)
		goto Exit;

	populate_fwd_tbls(p_lash);

Exit:
	if (p_lash->vl_min)
		free_lash_structures(p_lash);
	OSM_LOG_EXIT(p_log);

	return status;
}

static lash_t *lash_create(osm_opensm_t * p_osm)
{
	lash_t *p_lash;

	p_lash = calloc(1, sizeof(lash_t));
	if (!p_lash)
		return NULL;

	p_lash->p_osm = p_osm;

	return p_lash;
}

static void lash_delete(void *context)
{
	lash_t *p_lash = context;

	if (p_lash->switches) {
		unsigned id;
		for (id = 0; ((int)id) < p_lash->num_switches; id++)
			if (p_lash->switches[id])
				switch_delete(p_lash, p_lash->switches[id]);
		free(p_lash->switches);
	}

	free(p_lash);
}

static uint8_t get_lash_sl(void *context, uint8_t path_sl_hint,
			   const ib_net16_t slid, const ib_net16_t dlid)
{
	unsigned dst_id;
	unsigned src_id;
	osm_port_t *p_src_port, *p_dst_port;
	osm_switch_t *p_sw;
	lash_t *p_lash = context;
	osm_opensm_t *p_osm = p_lash->p_osm;

	if (!(p_osm->routing_engine_used &&
	      p_osm->routing_engine_used->type == OSM_ROUTING_ENGINE_TYPE_LASH))
		return OSM_DEFAULT_SL;

	p_src_port = osm_get_port_by_lid(&p_osm->subn, slid);
	if (!p_src_port)
		return OSM_DEFAULT_SL;

	p_dst_port = osm_get_port_by_lid(&p_osm->subn, dlid);
	if (!p_dst_port)
		return OSM_DEFAULT_SL;

	p_sw = get_osm_switch_from_port(p_dst_port);
	if (!p_sw || !p_sw->priv)
		return OSM_DEFAULT_SL;
	dst_id = get_lash_id(p_sw);

	p_sw = get_osm_switch_from_port(p_src_port);
	if (!p_sw || !p_sw->priv)
		return OSM_DEFAULT_SL;

	src_id = get_lash_id(p_sw);
	if (src_id == dst_id)
		return p_osm->subn.opt.lash_start_vl;

	return (uint8_t) ((switch_t *) p_sw->priv)->routing_table[dst_id].lane;
}

int osm_ucast_lash_setup(struct osm_routing_engine *r, osm_opensm_t *p_osm)
{
	lash_t *p_lash = lash_create(p_osm);
	if (!p_lash)
		return -1;

	r->context = p_lash;
	r->ucast_build_fwd_tables = lash_process;
	r->path_sl = get_lash_sl;
	r->destroy = lash_delete;

	return 0;
}
