/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2012 Mellanox Technologies LTD. All rights reserved.
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
 *    Declaration of OSM QoS Policy data types and functions.
 *
 * Author:
 *    Yevgeny Kliteynik, Mellanox
 */

#ifndef OSM_QOS_POLICY_H
#define OSM_QOS_POLICY_H

#include <iba/ib_types.h>
#include <complib/cl_list.h>
#include <opensm/st.h>
#include <opensm/osm_port.h>
#include <opensm/osm_partition.h>

#define YYSTYPE char *
#define OSM_QOS_POLICY_MAX_PORTS_ON_SWITCH  128
#define OSM_QOS_POLICY_DEFAULT_LEVEL_NAME   "default"

#define OSM_QOS_POLICY_ULP_SDP_SERVICE_ID   0x0000000000010000ULL
#define OSM_QOS_POLICY_ULP_RDS_SERVICE_ID   0x0000000001060000ULL
#define OSM_QOS_POLICY_ULP_RDS_PORT         0x48CA
#define OSM_QOS_POLICY_ULP_ISER_SERVICE_ID  0x0000000001060000ULL
#define OSM_QOS_POLICY_ULP_ISER_PORT        0x0CBC

#define OSM_QOS_POLICY_NODE_TYPE_CA        (((uint8_t)1)<<IB_NODE_TYPE_CA)
#define OSM_QOS_POLICY_NODE_TYPE_SWITCH    (((uint8_t)1)<<IB_NODE_TYPE_SWITCH)
#define OSM_QOS_POLICY_NODE_TYPE_ROUTER    (((uint8_t)1)<<IB_NODE_TYPE_ROUTER)

/***************************************************/

typedef struct osm_qos_port {
	cl_map_item_t map_item;
	osm_physp_t * p_physp;
} osm_qos_port_t;

typedef struct osm_qos_port_group {
	char *name;			/* single string (this port group name) */
	char *use;			/* single string (description) */
	uint8_t node_types;		/* node types bitmask */
	cl_qmap_t port_map;
} osm_qos_port_group_t;

/***************************************************/

typedef struct osm_qos_vlarb_scope {
	cl_list_t group_list;		/* list of group names (strings) */
	cl_list_t across_list;		/* list of 'across' group names (strings) */
	cl_list_t vlarb_high_list;	/* list of num pairs (n:m,...), 32-bit values */
	cl_list_t vlarb_low_list;	/* list of num pairs (n:m,...), 32-bit values */
	uint32_t vl_high_limit;		/* single integer */
	boolean_t vl_high_limit_set;
} osm_qos_vlarb_scope_t;

/***************************************************/

typedef struct osm_qos_sl2vl_scope {
	cl_list_t group_list;		/* list of strings (port group names) */
	boolean_t from[OSM_QOS_POLICY_MAX_PORTS_ON_SWITCH];
	boolean_t to[OSM_QOS_POLICY_MAX_PORTS_ON_SWITCH];
	cl_list_t across_from_list;	/* list of strings (port group names) */
	cl_list_t across_to_list;	/* list of strings (port group names) */
	uint8_t sl2vl_table[16];	/* array of sl2vl values */
	boolean_t sl2vl_table_set;
} osm_qos_sl2vl_scope_t;

/***************************************************/

typedef struct osm_qos_level {
	char *use;
	char *name;
	uint8_t sl;
	boolean_t sl_set;
	uint8_t mtu_limit;
	boolean_t mtu_limit_set;
	uint8_t rate_limit;
	boolean_t rate_limit_set;
	uint8_t pkt_life;
	boolean_t pkt_life_set;
	uint64_t **path_bits_range_arr;	/* array of bit ranges (real values are 32bits) */
	unsigned path_bits_range_len;	/* num of bit ranges in the array */
	uint64_t **pkey_range_arr;	/* array of PKey ranges (real values are 16bits) */
	unsigned pkey_range_len;
} osm_qos_level_t;


/***************************************************/

typedef struct osm_qos_match_rule {
	char *use;
	cl_list_t source_list;			/* list of strings */
	cl_list_t source_group_list;		/* list of pointers to relevant port-group */
	cl_list_t destination_list;		/* list of strings */
	cl_list_t destination_group_list;	/* list of pointers to relevant port-group */
	char *qos_level_name;
	osm_qos_level_t *p_qos_level;
	uint64_t **service_id_range_arr;	/* array of SID ranges (64-bit values) */
	unsigned service_id_range_len;
	uint64_t **qos_class_range_arr;		/* array of QoS Class ranges (real values are 16bits) */
	unsigned qos_class_range_len;
	uint64_t **pkey_range_arr;		/* array of PKey ranges (real values are 16bits) */
	unsigned pkey_range_len;
} osm_qos_match_rule_t;

/***************************************************/

typedef struct osm_qos_policy {
	cl_list_t port_groups;			/* list of osm_qos_port_group_t */
	cl_list_t sl2vl_tables;			/* list of osm_qos_sl2vl_scope_t */
	cl_list_t vlarb_tables;			/* list of osm_qos_vlarb_scope_t */
	cl_list_t qos_levels;			/* list of osm_qos_level_t */
	cl_list_t qos_match_rules;		/* list of osm_qos_match_rule_t */
	osm_qos_level_t *p_default_qos_level;	/* default QoS level */
	osm_subn_t *p_subn;			/* osm subnet object */
	st_table * p_node_hash;			/* node by name hash */
} osm_qos_policy_t;

/***************************************************/

osm_qos_port_t *osm_qos_policy_port_create(osm_physp_t * p_physp);
osm_qos_port_group_t * osm_qos_policy_port_group_create();
void osm_qos_policy_port_group_destroy(osm_qos_port_group_t * p_port_group);

osm_qos_vlarb_scope_t * osm_qos_policy_vlarb_scope_create();
void osm_qos_policy_vlarb_scope_destroy(osm_qos_vlarb_scope_t * p_vlarb_scope);

osm_qos_sl2vl_scope_t * osm_qos_policy_sl2vl_scope_create();
void osm_qos_policy_sl2vl_scope_destroy(osm_qos_sl2vl_scope_t * p_sl2vl_scope);

osm_qos_level_t * osm_qos_policy_qos_level_create();
void osm_qos_policy_qos_level_destroy(osm_qos_level_t * p_qos_level);

boolean_t osm_qos_level_has_pkey(IN const osm_qos_level_t * p_qos_level,
				 IN ib_net16_t pkey);

ib_net16_t osm_qos_level_get_shared_pkey(IN const osm_qos_level_t * p_qos_level,
					 IN const osm_physp_t * p_src_physp,
					 IN const osm_physp_t * p_dest_physp,
					 IN const boolean_t allow_both_pkeys);

osm_qos_match_rule_t * osm_qos_policy_match_rule_create();
void osm_qos_policy_match_rule_destroy(osm_qos_match_rule_t * p_match_rule);

osm_qos_policy_t * osm_qos_policy_create(osm_subn_t * p_subn);
void osm_qos_policy_destroy(osm_qos_policy_t * p_qos_policy);
int osm_qos_policy_validate(osm_qos_policy_t * p_qos_policy, osm_log_t * p_log);

osm_qos_level_t * osm_qos_policy_get_qos_level_by_pr(
	IN const osm_qos_policy_t * p_qos_policy,
	IN const ib_path_rec_t * p_pr,
	IN const osm_physp_t * p_src_physp,
	IN const osm_physp_t * p_dest_physp,
	IN ib_net64_t comp_mask);

osm_qos_level_t * osm_qos_policy_get_qos_level_by_mpr(
	IN const osm_qos_policy_t * p_qos_policy,
	IN const ib_multipath_rec_t * p_mpr,
	IN const osm_physp_t * p_src_physp,
	IN const osm_physp_t * p_dest_physp,
	IN ib_net64_t comp_mask);

/***************************************************/

int osm_qos_parse_policy_file(IN osm_subn_t * p_subn);

/***************************************************/

#endif				/* ifndef OSM_QOS_POLICY_H */
