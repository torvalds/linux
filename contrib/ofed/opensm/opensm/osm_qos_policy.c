/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2012 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2008 Xsigo Systems Inc.  All rights reserved.
 * Copyright (c) 2009 HNR Consulting.  All rights reserved.
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
 *    OSM QoS Policy functions.
 *
 * Author:
 *    Yevgeny Kliteynik, Mellanox
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_QOS_POLICY_C
#include <opensm/osm_log.h>
#include <opensm/osm_node.h>
#include <opensm/osm_port.h>
#include <opensm/osm_partition.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_qos_policy.h>

extern osm_qos_level_t __default_simple_qos_level;

/***************************************************
 ***************************************************/

static void
__build_nodebyname_hash(osm_qos_policy_t * p_qos_policy)
{
	osm_node_t * p_node;
	cl_qmap_t  * p_node_guid_tbl = &p_qos_policy->p_subn->node_guid_tbl;

	p_qos_policy->p_node_hash = st_init_strtable();
	CL_ASSERT(p_qos_policy->p_node_hash);

	if (!p_node_guid_tbl || !cl_qmap_count(p_node_guid_tbl))
		return;

	for (p_node = (osm_node_t *) cl_qmap_head(p_node_guid_tbl);
	     p_node != (osm_node_t *) cl_qmap_end(p_node_guid_tbl);
	     p_node = (osm_node_t *) cl_qmap_next(&p_node->map_item)) {
		if (!st_lookup(p_qos_policy->p_node_hash,
			      (st_data_t)p_node->print_desc, NULL))
			st_insert(p_qos_policy->p_node_hash,
				  (st_data_t)p_node->print_desc,
				  (st_data_t)p_node);
	}
}

/***************************************************
 ***************************************************/

static boolean_t
__is_num_in_range_arr(uint64_t ** range_arr,
		  unsigned range_arr_len, uint64_t num)
{
	unsigned ind_1 = 0;
	unsigned ind_2 = range_arr_len - 1;
	unsigned ind_mid;

	if (!range_arr || !range_arr_len)
		return FALSE;

	while (ind_1 <= ind_2) {
	    if (num < range_arr[ind_1][0] || num > range_arr[ind_2][1])
		return FALSE;
	    else if (num <= range_arr[ind_1][1] || num >= range_arr[ind_2][0])
		return TRUE;

	    ind_mid = ind_1 + (ind_2 - ind_1 + 1)/2;

	    if (num < range_arr[ind_mid][0])
		ind_2 = ind_mid;
	    else if (num > range_arr[ind_mid][1])
		ind_1 = ind_mid;
	    else
		return TRUE;

	    ind_1++;
	    ind_2--;
	}

	return FALSE;
}

/***************************************************
 ***************************************************/

static void __free_single_element(void *p_element, void *context)
{
	if (p_element)
		free(p_element);
}

/***************************************************
 ***************************************************/

osm_qos_port_t *osm_qos_policy_port_create(osm_physp_t *p_physp)
{
	osm_qos_port_t *p =
	    (osm_qos_port_t *) calloc(1, sizeof(osm_qos_port_t));
	if (p)
		p->p_physp = p_physp;
	return p;
}

/***************************************************
 ***************************************************/

osm_qos_port_group_t *osm_qos_policy_port_group_create()
{
	osm_qos_port_group_t *p =
	    (osm_qos_port_group_t *) calloc(1, sizeof(osm_qos_port_group_t));
	if (p)
		cl_qmap_init(&p->port_map);
	return p;
}

/***************************************************
 ***************************************************/

void osm_qos_policy_port_group_destroy(osm_qos_port_group_t * p)
{
	osm_qos_port_t * p_port;
	osm_qos_port_t * p_old_port;

	if (!p)
		return;

	if (p->name)
		free(p->name);
	if (p->use)
		free(p->use);

	p_port = (osm_qos_port_t *) cl_qmap_head(&p->port_map);
	while (p_port != (osm_qos_port_t *) cl_qmap_end(&p->port_map))
	{
		p_old_port = p_port;
		p_port = (osm_qos_port_t *) cl_qmap_next(&p_port->map_item);
		free(p_old_port);
	}
	cl_qmap_remove_all(&p->port_map);

	free(p);
}

/***************************************************
 ***************************************************/

osm_qos_vlarb_scope_t *osm_qos_policy_vlarb_scope_create()
{
	osm_qos_vlarb_scope_t *p =
	    (osm_qos_vlarb_scope_t *) calloc(1, sizeof(osm_qos_vlarb_scope_t));
	if (p) {
		cl_list_init(&p->group_list, 10);
		cl_list_init(&p->across_list, 10);
		cl_list_init(&p->vlarb_high_list, 10);
		cl_list_init(&p->vlarb_low_list, 10);
	}
	return p;
}

/***************************************************
 ***************************************************/

void osm_qos_policy_vlarb_scope_destroy(osm_qos_vlarb_scope_t * p)
{
	if (!p)
		return;

	cl_list_apply_func(&p->group_list, __free_single_element, NULL);
	cl_list_apply_func(&p->across_list, __free_single_element, NULL);
	cl_list_apply_func(&p->vlarb_high_list, __free_single_element, NULL);
	cl_list_apply_func(&p->vlarb_low_list, __free_single_element, NULL);

	cl_list_remove_all(&p->group_list);
	cl_list_remove_all(&p->across_list);
	cl_list_remove_all(&p->vlarb_high_list);
	cl_list_remove_all(&p->vlarb_low_list);

	cl_list_destroy(&p->group_list);
	cl_list_destroy(&p->across_list);
	cl_list_destroy(&p->vlarb_high_list);
	cl_list_destroy(&p->vlarb_low_list);

	free(p);
}

/***************************************************
 ***************************************************/

osm_qos_sl2vl_scope_t *osm_qos_policy_sl2vl_scope_create()
{
	osm_qos_sl2vl_scope_t *p =
	    (osm_qos_sl2vl_scope_t *) calloc(1, sizeof(osm_qos_sl2vl_scope_t));
	if (p) {
		cl_list_init(&p->group_list, 10);
		cl_list_init(&p->across_from_list, 10);
		cl_list_init(&p->across_to_list, 10);
	}
	return p;
}

/***************************************************
 ***************************************************/

void osm_qos_policy_sl2vl_scope_destroy(osm_qos_sl2vl_scope_t * p)
{
	if (!p)
		return;

	cl_list_apply_func(&p->group_list, __free_single_element, NULL);
	cl_list_apply_func(&p->across_from_list, __free_single_element, NULL);
	cl_list_apply_func(&p->across_to_list, __free_single_element, NULL);

	cl_list_remove_all(&p->group_list);
	cl_list_remove_all(&p->across_from_list);
	cl_list_remove_all(&p->across_to_list);

	cl_list_destroy(&p->group_list);
	cl_list_destroy(&p->across_from_list);
	cl_list_destroy(&p->across_to_list);

	free(p);
}

/***************************************************
 ***************************************************/

osm_qos_level_t *osm_qos_policy_qos_level_create()
{
	osm_qos_level_t *p =
	    (osm_qos_level_t *) calloc(1, sizeof(osm_qos_level_t));
	return p;
}

/***************************************************
 ***************************************************/

void osm_qos_policy_qos_level_destroy(osm_qos_level_t * p)
{
	unsigned i;

	if (!p)
		return;

	free(p->name);
	free(p->use);

	for (i = 0; i < p->path_bits_range_len; i++)
		free(p->path_bits_range_arr[i]);
	free(p->path_bits_range_arr);

	for(i = 0; i < p->pkey_range_len; i++)
		free((p->pkey_range_arr[i]));
	free(p->pkey_range_arr);

	free(p);
}

/***************************************************
 ***************************************************/

boolean_t osm_qos_level_has_pkey(IN const osm_qos_level_t * p_qos_level,
				 IN ib_net16_t pkey)
{
	if (!p_qos_level || !p_qos_level->pkey_range_len)
		return FALSE;
	return __is_num_in_range_arr(p_qos_level->pkey_range_arr,
				     p_qos_level->pkey_range_len,
				     cl_ntoh16(ib_pkey_get_base(pkey)));
}

/***************************************************
 ***************************************************/

ib_net16_t osm_qos_level_get_shared_pkey(IN const osm_qos_level_t * p_qos_level,
					 IN const osm_physp_t * p_src_physp,
					 IN const osm_physp_t * p_dest_physp,
					 IN const boolean_t allow_both_pkeys)
{
	unsigned i;
	uint16_t pkey_ho = 0;

	if (!p_qos_level || !p_qos_level->pkey_range_len)
		return 0;

	/*
	 * ToDo: This approach is not optimal.
	 *       Think how to find shared pkey that also exists
	 *       in QoS level in less runtime.
	 */

	for (i = 0; i < p_qos_level->pkey_range_len; i++) {
		for (pkey_ho = p_qos_level->pkey_range_arr[i][0];
		     pkey_ho <= p_qos_level->pkey_range_arr[i][1]; pkey_ho++) {
			if (osm_physp_share_this_pkey
			    (p_src_physp, p_dest_physp, cl_hton16(pkey_ho),
			     allow_both_pkeys))
				return cl_hton16(pkey_ho);
		}
	}

	return 0;
}

/***************************************************
 ***************************************************/

osm_qos_match_rule_t *osm_qos_policy_match_rule_create()
{
	osm_qos_match_rule_t *p =
	    (osm_qos_match_rule_t *) calloc(1, sizeof(osm_qos_match_rule_t));
	if (p) {
		cl_list_init(&p->source_list, 10);
		cl_list_init(&p->source_group_list, 10);
		cl_list_init(&p->destination_list, 10);
		cl_list_init(&p->destination_group_list, 10);
	}
	return p;
}

/***************************************************
 ***************************************************/

void osm_qos_policy_match_rule_destroy(osm_qos_match_rule_t * p)
{
	unsigned i;

	if (!p)
		return;

	if (p->qos_level_name)
		free(p->qos_level_name);
	if (p->use)
		free(p->use);

	if (p->service_id_range_arr) {
		for (i = 0; i < p->service_id_range_len; i++)
			free(p->service_id_range_arr[i]);
		free(p->service_id_range_arr);
	}

	if (p->qos_class_range_arr) {
		for (i = 0; i < p->qos_class_range_len; i++)
			free(p->qos_class_range_arr[i]);
		free(p->qos_class_range_arr);
	}

	if (p->pkey_range_arr) {
		for (i = 0; i < p->pkey_range_len; i++)
			free(p->pkey_range_arr[i]);
		free(p->pkey_range_arr);
	}

	cl_list_apply_func(&p->source_list, __free_single_element, NULL);
	cl_list_remove_all(&p->source_list);
	cl_list_destroy(&p->source_list);

	cl_list_remove_all(&p->source_group_list);
	cl_list_destroy(&p->source_group_list);

	cl_list_apply_func(&p->destination_list, __free_single_element, NULL);
	cl_list_remove_all(&p->destination_list);
	cl_list_destroy(&p->destination_list);

	cl_list_remove_all(&p->destination_group_list);
	cl_list_destroy(&p->destination_group_list);

	free(p);
}

/***************************************************
 ***************************************************/

osm_qos_policy_t * osm_qos_policy_create(osm_subn_t * p_subn)
{
	osm_qos_policy_t * p_qos_policy = (osm_qos_policy_t *)calloc(1, sizeof(osm_qos_policy_t));
	if (!p_qos_policy)
		return NULL;

	cl_list_construct(&p_qos_policy->port_groups);
	cl_list_init(&p_qos_policy->port_groups, 10);

	cl_list_construct(&p_qos_policy->vlarb_tables);
	cl_list_init(&p_qos_policy->vlarb_tables, 10);

	cl_list_construct(&p_qos_policy->sl2vl_tables);
	cl_list_init(&p_qos_policy->sl2vl_tables, 10);

	cl_list_construct(&p_qos_policy->qos_levels);
	cl_list_init(&p_qos_policy->qos_levels, 10);

	cl_list_construct(&p_qos_policy->qos_match_rules);
	cl_list_init(&p_qos_policy->qos_match_rules, 10);

	p_qos_policy->p_subn = p_subn;
	__build_nodebyname_hash(p_qos_policy);

	return p_qos_policy;
}

/***************************************************
 ***************************************************/

void osm_qos_policy_destroy(osm_qos_policy_t * p_qos_policy)
{
	cl_list_iterator_t list_iterator;
	osm_qos_port_group_t *p_port_group = NULL;
	osm_qos_vlarb_scope_t *p_vlarb_scope = NULL;
	osm_qos_sl2vl_scope_t *p_sl2vl_scope = NULL;
	osm_qos_level_t *p_qos_level = NULL;
	osm_qos_match_rule_t *p_qos_match_rule = NULL;

	if (!p_qos_policy)
		return;

	list_iterator = cl_list_head(&p_qos_policy->port_groups);
	while (list_iterator != cl_list_end(&p_qos_policy->port_groups)) {
		p_port_group =
		    (osm_qos_port_group_t *) cl_list_obj(list_iterator);
		if (p_port_group)
			osm_qos_policy_port_group_destroy(p_port_group);
		list_iterator = cl_list_next(list_iterator);
	}
	cl_list_remove_all(&p_qos_policy->port_groups);
	cl_list_destroy(&p_qos_policy->port_groups);

	list_iterator = cl_list_head(&p_qos_policy->vlarb_tables);
	while (list_iterator != cl_list_end(&p_qos_policy->vlarb_tables)) {
		p_vlarb_scope =
		    (osm_qos_vlarb_scope_t *) cl_list_obj(list_iterator);
		if (p_vlarb_scope)
			osm_qos_policy_vlarb_scope_destroy(p_vlarb_scope);
		list_iterator = cl_list_next(list_iterator);
	}
	cl_list_remove_all(&p_qos_policy->vlarb_tables);
	cl_list_destroy(&p_qos_policy->vlarb_tables);

	list_iterator = cl_list_head(&p_qos_policy->sl2vl_tables);
	while (list_iterator != cl_list_end(&p_qos_policy->sl2vl_tables)) {
		p_sl2vl_scope =
		    (osm_qos_sl2vl_scope_t *) cl_list_obj(list_iterator);
		if (p_sl2vl_scope)
			osm_qos_policy_sl2vl_scope_destroy(p_sl2vl_scope);
		list_iterator = cl_list_next(list_iterator);
	}
	cl_list_remove_all(&p_qos_policy->sl2vl_tables);
	cl_list_destroy(&p_qos_policy->sl2vl_tables);

	list_iterator = cl_list_head(&p_qos_policy->qos_levels);
	while (list_iterator != cl_list_end(&p_qos_policy->qos_levels)) {
		p_qos_level = (osm_qos_level_t *) cl_list_obj(list_iterator);
		if (p_qos_level)
			osm_qos_policy_qos_level_destroy(p_qos_level);
		list_iterator = cl_list_next(list_iterator);
	}
	cl_list_remove_all(&p_qos_policy->qos_levels);
	cl_list_destroy(&p_qos_policy->qos_levels);

	list_iterator = cl_list_head(&p_qos_policy->qos_match_rules);
	while (list_iterator != cl_list_end(&p_qos_policy->qos_match_rules)) {
		p_qos_match_rule =
		    (osm_qos_match_rule_t *) cl_list_obj(list_iterator);
		if (p_qos_match_rule)
			osm_qos_policy_match_rule_destroy(p_qos_match_rule);
		list_iterator = cl_list_next(list_iterator);
	}
	cl_list_remove_all(&p_qos_policy->qos_match_rules);
	cl_list_destroy(&p_qos_policy->qos_match_rules);

	if (p_qos_policy->p_node_hash)
		st_free_table(p_qos_policy->p_node_hash);

	free(p_qos_policy);

	p_qos_policy = NULL;
}

/***************************************************
 ***************************************************/

static boolean_t
__qos_policy_is_port_in_group(osm_subn_t * p_subn,
			      const osm_physp_t * p_physp,
			      osm_qos_port_group_t * p_port_group)
{
	osm_node_t *p_node = osm_physp_get_node_ptr(p_physp);
	ib_net64_t port_guid = osm_physp_get_port_guid(p_physp);
	uint64_t port_guid_ho = cl_ntoh64(port_guid);

	/* check whether this port's type matches any of group's types */

	if ( p_port_group->node_types &
	     (((uint8_t)1)<<osm_node_get_type(p_node)) )
		return TRUE;

	/* check whether this port's guid is in group's port map */

	if (cl_qmap_get(&p_port_group->port_map, port_guid_ho) !=
	    cl_qmap_end(&p_port_group->port_map))
		return TRUE;

	return FALSE;
}				/* __qos_policy_is_port_in_group() */

/***************************************************
 ***************************************************/

static boolean_t
__qos_policy_is_port_in_group_list(const osm_qos_policy_t * p_qos_policy,
				   const osm_physp_t * p_physp,
				   cl_list_t * p_port_group_list)
{
	osm_qos_port_group_t *p_port_group;
	cl_list_iterator_t list_iterator;

	list_iterator = cl_list_head(p_port_group_list);
	while (list_iterator != cl_list_end(p_port_group_list)) {
		p_port_group =
		    (osm_qos_port_group_t *) cl_list_obj(list_iterator);
		if (p_port_group) {
			if (__qos_policy_is_port_in_group
			    (p_qos_policy->p_subn, p_physp, p_port_group))
				return TRUE;
		}
		list_iterator = cl_list_next(list_iterator);
	}
	return FALSE;
}

/***************************************************
 ***************************************************/

static osm_qos_match_rule_t *__qos_policy_get_match_rule_by_params(
			 const osm_qos_policy_t * p_qos_policy,
			 uint64_t service_id,
			 uint16_t qos_class,
			 uint16_t pkey,
			 const osm_physp_t * p_src_physp,
			 const osm_physp_t * p_dest_physp,
			 ib_net64_t comp_mask)
{
	osm_qos_match_rule_t *p_qos_match_rule = NULL;
	cl_list_iterator_t list_iterator;
	osm_log_t * p_log = &p_qos_policy->p_subn->p_osm->log;

	boolean_t matched_by_sguid = FALSE,
		  matched_by_dguid = FALSE,
		  matched_by_sordguid = FALSE,
		  matched_by_class = FALSE,
		  matched_by_sid = FALSE,
		  matched_by_pkey = FALSE;

	if (!cl_list_count(&p_qos_policy->qos_match_rules))
		return NULL;

	OSM_LOG_ENTER(p_log);

	/* Go over all QoS match rules and find the one that matches the request */

	list_iterator = cl_list_head(&p_qos_policy->qos_match_rules);
	while (list_iterator != cl_list_end(&p_qos_policy->qos_match_rules)) {
		p_qos_match_rule =
		    (osm_qos_match_rule_t *) cl_list_obj(list_iterator);
		if (!p_qos_match_rule) {
			list_iterator = cl_list_next(list_iterator);
			continue;
		}

		/* If a match rule has Source groups and no Destination groups,
		 * PR request source has to be in this list */

		if (cl_list_count(&p_qos_match_rule->source_group_list)
		    && !cl_list_count(&p_qos_match_rule->destination_group_list)) {
			if (!__qos_policy_is_port_in_group_list(p_qos_policy,
								p_src_physp,
								&p_qos_match_rule->
								source_group_list))
			{
				list_iterator = cl_list_next(list_iterator);
				continue;
			}
			matched_by_sguid = TRUE;
		}

		/* If a match rule has Destination groups and no Source groups,
		 * PR request dest. has to be in this list */

		if (cl_list_count(&p_qos_match_rule->destination_group_list)
		    && !cl_list_count(&p_qos_match_rule->source_group_list)) {
			if (!__qos_policy_is_port_in_group_list(p_qos_policy,
								p_dest_physp,
								&p_qos_match_rule->
								destination_group_list))
			{
				list_iterator = cl_list_next(list_iterator);
				continue;
			}
			matched_by_dguid = TRUE;
		}

		/* If a match rule has both Source and Destination groups,
		 * PR request source or dest. must be in respective list
		 */
		if (cl_list_count(&p_qos_match_rule->source_group_list)
		    && cl_list_count(&p_qos_match_rule->destination_group_list)) {
			if (__qos_policy_is_port_in_group_list(p_qos_policy,
							       p_src_physp,
							       &p_qos_match_rule->
							       source_group_list)
			    && __qos_policy_is_port_in_group_list(p_qos_policy,
								  p_dest_physp,
								  &p_qos_match_rule->
								  destination_group_list))
				matched_by_sordguid = TRUE;
			else {
				list_iterator = cl_list_next(list_iterator);
				continue;
			}
		}

		/* If a match rule has QoS classes, PR request HAS
		   to have a matching QoS class to match the rule */

		if (p_qos_match_rule->qos_class_range_len) {
			if (!(comp_mask & IB_PR_COMPMASK_QOS_CLASS)) {
				list_iterator = cl_list_next(list_iterator);
				continue;
			}

			if (!__is_num_in_range_arr
			    (p_qos_match_rule->qos_class_range_arr,
			     p_qos_match_rule->qos_class_range_len,
			     qos_class)) {
				list_iterator = cl_list_next(list_iterator);
				continue;
			}
			matched_by_class = TRUE;
		}

		/* If a match rule has Service IDs, PR request HAS
		   to have a matching Service ID to match the rule */

		if (p_qos_match_rule->service_id_range_len) {
			if (!(comp_mask & IB_PR_COMPMASK_SERVICEID_MSB) ||
			    !(comp_mask & IB_PR_COMPMASK_SERVICEID_LSB)) {
				list_iterator = cl_list_next(list_iterator);
				continue;
			}

			if (!__is_num_in_range_arr
			    (p_qos_match_rule->service_id_range_arr,
			     p_qos_match_rule->service_id_range_len,
			     service_id)) {
				list_iterator = cl_list_next(list_iterator);
				continue;
			}
			matched_by_sid = TRUE;
		}

		/* If a match rule has PKeys, PR request HAS
		   to have a matching PKey to match the rule */

		if (p_qos_match_rule->pkey_range_len) {
			if (!(comp_mask & IB_PR_COMPMASK_PKEY)) {
				list_iterator = cl_list_next(list_iterator);
				continue;
			}

			if (!__is_num_in_range_arr
			    (p_qos_match_rule->pkey_range_arr,
			     p_qos_match_rule->pkey_range_len,
			     pkey & 0x7FFF)) {
				list_iterator = cl_list_next(list_iterator);
				continue;
			}
			matched_by_pkey = TRUE;
		}

		/* if we got here, then this match-rule matched this PR request */
		break;
	}

	if (list_iterator == cl_list_end(&p_qos_policy->qos_match_rules))
		p_qos_match_rule = NULL;

	if (p_qos_match_rule)
		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"request matched rule (%s) by:%s%s%s%s%s%s\n",
			(p_qos_match_rule->use) ?
				p_qos_match_rule->use : "no description",
			(matched_by_sguid) ? " SGUID" : "",
			(matched_by_dguid) ? " DGUID" : "",
			(matched_by_sordguid) ? "SorDGUID" : "",
			(matched_by_class) ? " QoS_Class" : "",
			(matched_by_sid)   ? " ServiceID" : "",
			(matched_by_pkey)  ? " PKey" : "");
	else
		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"request not matched any rule\n");

	OSM_LOG_EXIT(p_log);
	return p_qos_match_rule;
}				/* __qos_policy_get_match_rule_by_params() */

/***************************************************
 ***************************************************/

static osm_qos_level_t *__qos_policy_get_qos_level_by_name(
		const osm_qos_policy_t * p_qos_policy,
		const char *name)
{
	osm_qos_level_t *p_qos_level = NULL;
	cl_list_iterator_t list_iterator;

	list_iterator = cl_list_head(&p_qos_policy->qos_levels);
	while (list_iterator != cl_list_end(&p_qos_policy->qos_levels)) {
		p_qos_level = (osm_qos_level_t *) cl_list_obj(list_iterator);
		if (!p_qos_level)
			continue;

		/* names are case INsensitive */
		if (strcasecmp(name, p_qos_level->name) == 0)
			return p_qos_level;

		list_iterator = cl_list_next(list_iterator);
	}

	return NULL;
}

/***************************************************
 ***************************************************/

static osm_qos_port_group_t *__qos_policy_get_port_group_by_name(
		const osm_qos_policy_t * p_qos_policy,
		const char *const name)
{
	osm_qos_port_group_t *p_port_group = NULL;
	cl_list_iterator_t list_iterator;

	list_iterator = cl_list_head(&p_qos_policy->port_groups);
	while (list_iterator != cl_list_end(&p_qos_policy->port_groups)) {
		p_port_group =
		    (osm_qos_port_group_t *) cl_list_obj(list_iterator);
		if (!p_port_group)
			continue;

		/* names are case INsensitive */
		if (strcasecmp(name, p_port_group->name) == 0)
			return p_port_group;

		list_iterator = cl_list_next(list_iterator);
	}

	return NULL;
}

/***************************************************
 ***************************************************/

static void __qos_policy_validate_pkey(
			osm_qos_policy_t * p_qos_policy,
			osm_qos_match_rule_t * p_qos_match_rule,
			osm_prtn_t * p_prtn)
{
	if (!p_qos_policy || !p_qos_match_rule || !p_prtn)
		return;

	if (!p_qos_match_rule->p_qos_level->sl_set ||
	    p_prtn->sl == p_qos_match_rule->p_qos_level->sl)
		return;

	OSM_LOG(&p_qos_policy->p_subn->p_osm->log, OSM_LOG_VERBOSE,
		"QoS Level SL (%u) for Pkey 0x%04X in match rule "
		"differs from  partition SL (%u)\n",
		p_qos_match_rule->p_qos_level->sl,
		cl_ntoh16(p_prtn->pkey), p_prtn->sl);
}

/***************************************************
 ***************************************************/

int osm_qos_policy_validate(osm_qos_policy_t * p_qos_policy,
			    osm_log_t *p_log)
{
	cl_list_iterator_t match_rules_list_iterator;
	cl_list_iterator_t list_iterator;
	osm_qos_port_group_t *p_port_group = NULL;
	osm_qos_match_rule_t *p_qos_match_rule = NULL;
	char *str;
	unsigned i, j;
	int res = 0;
	uint64_t pkey_64;
	ib_net16_t pkey;
	osm_prtn_t * p_prtn;

	OSM_LOG_ENTER(p_log);

	/* set default qos level */

	p_qos_policy->p_default_qos_level =
	    __qos_policy_get_qos_level_by_name(p_qos_policy, OSM_QOS_POLICY_DEFAULT_LEVEL_NAME);
	if (!p_qos_policy->p_default_qos_level) {
		/* There's no default QoS level in the usual qos-level section.
		   Check whether the 'simple' default QoS level that can be
		   defined in the qos-ulp section exists */
		if (__default_simple_qos_level.sl_set) {
			p_qos_policy->p_default_qos_level = &__default_simple_qos_level;
		}
		else {
			OSM_LOG(p_log, OSM_LOG_ERROR, "ERR AC10: "
				"Default qos-level (%s) not defined.\n",
				OSM_QOS_POLICY_DEFAULT_LEVEL_NAME);
			res = 1;
			goto Exit;
		}
	}

	/* scan all the match rules, and fill the lists of pointers to
	   relevant qos levels and port groups to speed up PR matching */

	i = 1;
	match_rules_list_iterator =
	    cl_list_head(&p_qos_policy->qos_match_rules);
	while (match_rules_list_iterator !=
	       cl_list_end(&p_qos_policy->qos_match_rules)) {
		p_qos_match_rule =
		    (osm_qos_match_rule_t *)
		    cl_list_obj(match_rules_list_iterator);
		CL_ASSERT(p_qos_match_rule);

		/* find the matching qos-level for each match-rule */

		if (!p_qos_match_rule->p_qos_level)
			p_qos_match_rule->p_qos_level =
				__qos_policy_get_qos_level_by_name(p_qos_policy,
					       p_qos_match_rule->qos_level_name);

		if (!p_qos_match_rule->p_qos_level) {
			OSM_LOG(p_log, OSM_LOG_ERROR, "ERR AC11: "
				"qos-match-rule num %u: qos-level '%s' not found\n",
				i, p_qos_match_rule->qos_level_name);
			res = 1;
			goto Exit;
		}

		/* find the matching port-group for element of source_list */

		if (cl_list_count(&p_qos_match_rule->source_list)) {
			list_iterator =
			    cl_list_head(&p_qos_match_rule->source_list);
			while (list_iterator !=
			       cl_list_end(&p_qos_match_rule->source_list)) {
				str = (char *)cl_list_obj(list_iterator);
				CL_ASSERT(str);

				p_port_group =
				    __qos_policy_get_port_group_by_name(p_qos_policy, str);
				if (!p_port_group) {
					OSM_LOG(p_log, OSM_LOG_ERROR, "ERR AC12: "
						"qos-match-rule num %u: source port-group '%s' not found\n",
						i, str);
					res = 1;
					goto Exit;
				}

				cl_list_insert_tail(&p_qos_match_rule->
						    source_group_list,
						    p_port_group);

				list_iterator = cl_list_next(list_iterator);
			}
		}

		/* find the matching port-group for element of destination_list */

		if (cl_list_count(&p_qos_match_rule->destination_list)) {
			list_iterator =
			    cl_list_head(&p_qos_match_rule->destination_list);
			while (list_iterator !=
			       cl_list_end(&p_qos_match_rule->
					   destination_list)) {
				str = (char *)cl_list_obj(list_iterator);
				CL_ASSERT(str);

				p_port_group =
				    __qos_policy_get_port_group_by_name(p_qos_policy,str);
				if (!p_port_group) {
					OSM_LOG(p_log, OSM_LOG_ERROR, "ERR AC13: "
						"qos-match-rule num %u: destination port-group '%s' not found\n",
						i, str);
					res = 1;
					goto Exit;
				}

				cl_list_insert_tail(&p_qos_match_rule->
						    destination_group_list,
						    p_port_group);

				list_iterator = cl_list_next(list_iterator);
			}
		}

		/*
		 * Scan all the pkeys in matching rule, and if the
		 * partition for these pkeys exists, set the SL
		 * according to the QoS Level.
		 * Warn if there's mismatch between QoS level SL
		 * and Partition SL.
		 */

		for (j = 0; j < p_qos_match_rule->pkey_range_len; j++) {
			for ( pkey_64 = p_qos_match_rule->pkey_range_arr[j][0];
			      pkey_64 <= p_qos_match_rule->pkey_range_arr[j][1];
			      pkey_64++) {
                                pkey = cl_hton16((uint16_t)(pkey_64 & 0x7fff));
				p_prtn = (osm_prtn_t *)cl_qmap_get(
					&p_qos_policy->p_subn->prtn_pkey_tbl, pkey);

				if (p_prtn == (osm_prtn_t *)cl_qmap_end(
					&p_qos_policy->p_subn->prtn_pkey_tbl))
					/* partition for this pkey not found */
					OSM_LOG(p_log, OSM_LOG_ERROR, "ERR AC14: "
						"pkey 0x%04X in match rule - "
						"partition doesn't exist\n",
						cl_ntoh16(pkey));
				else
					__qos_policy_validate_pkey(p_qos_policy,
							p_qos_match_rule,
							p_prtn);
			}
		}

		/* done with the current match-rule */

		match_rules_list_iterator =
		    cl_list_next(match_rules_list_iterator);
		i++;
	}

Exit:
	OSM_LOG_EXIT(p_log);
	return res;
}				/* osm_qos_policy_validate() */

/***************************************************
 ***************************************************/

static osm_qos_level_t * __qos_policy_get_qos_level_by_params(
	IN const osm_qos_policy_t * p_qos_policy,
	IN const osm_physp_t * p_src_physp,
	IN const osm_physp_t * p_dest_physp,
	IN uint64_t service_id,
	IN uint16_t qos_class,
	IN uint16_t pkey,
	IN ib_net64_t comp_mask)
{
	osm_qos_match_rule_t *p_qos_match_rule = NULL;

	if (!p_qos_policy)
		return NULL;

	p_qos_match_rule = __qos_policy_get_match_rule_by_params(
		p_qos_policy, service_id, qos_class, pkey,
		p_src_physp, p_dest_physp, comp_mask);

	return p_qos_match_rule ? p_qos_match_rule->p_qos_level :
		p_qos_policy->p_default_qos_level;
}				/* __qos_policy_get_qos_level_by_params() */

/***************************************************
 ***************************************************/

osm_qos_level_t * osm_qos_policy_get_qos_level_by_pr(
	IN const osm_qos_policy_t * p_qos_policy,
	IN const ib_path_rec_t * p_pr,
	IN const osm_physp_t * p_src_physp,
	IN const osm_physp_t * p_dest_physp,
	IN ib_net64_t comp_mask)
{
	return __qos_policy_get_qos_level_by_params(
		p_qos_policy, p_src_physp, p_dest_physp,
		cl_ntoh64(p_pr->service_id), ib_path_rec_qos_class(p_pr),
		cl_ntoh16(p_pr->pkey), comp_mask);
}

/***************************************************
 ***************************************************/

osm_qos_level_t * osm_qos_policy_get_qos_level_by_mpr(
	IN const osm_qos_policy_t * p_qos_policy,
	IN const ib_multipath_rec_t * p_mpr,
	IN const osm_physp_t * p_src_physp,
	IN const osm_physp_t * p_dest_physp,
	IN ib_net64_t comp_mask)
{
	ib_net64_t pr_comp_mask = 0;

	if (!p_qos_policy)
		return NULL;

	/*
	 * Converting MultiPathRecord compmask to the PathRecord
	 * compmask. Note that only relevant bits are set.
	 */
	pr_comp_mask =
		((comp_mask & IB_MPR_COMPMASK_QOS_CLASS) ?
		 IB_PR_COMPMASK_QOS_CLASS : 0) |
		((comp_mask & IB_MPR_COMPMASK_PKEY) ?
		 IB_PR_COMPMASK_PKEY : 0) |
		((comp_mask & IB_MPR_COMPMASK_SERVICEID_MSB) ?
		 IB_PR_COMPMASK_SERVICEID_MSB : 0) |
		((comp_mask & IB_MPR_COMPMASK_SERVICEID_LSB) ?
		 IB_PR_COMPMASK_SERVICEID_LSB : 0);

	return __qos_policy_get_qos_level_by_params(
		p_qos_policy, p_src_physp, p_dest_physp,
		cl_ntoh64(ib_multipath_rec_service_id(p_mpr)),
		ib_multipath_rec_qos_class(p_mpr),
		cl_ntoh16(p_mpr->pkey), pr_comp_mask);
}

/***************************************************
 ***************************************************/
