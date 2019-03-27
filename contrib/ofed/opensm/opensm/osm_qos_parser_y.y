%{
/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2008 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2008 HNR Consulting. All rights reserved.
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
 *    Grammar of OSM QoS parser.
 *
 * Environment:
 *    Linux User Mode
 *
 * Author:
 *    Yevgeny Kliteynik, Mellanox
 */

#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_QOS_PARSER_Y_Y
#include <opensm/osm_opensm.h>
#include <opensm/osm_qos_policy.h>

#define OSM_QOS_POLICY_MAX_LINE_LEN         1024*10
#define OSM_QOS_POLICY_SL2VL_TABLE_LEN      IB_MAX_NUM_VLS
#define OSM_QOS_POLICY_MAX_VL_NUM           IB_MAX_NUM_VLS
#define OSM_QOS_POLICY_MAX_RATE             IB_MAX_RATE
#define OSM_QOS_POLICY_MIN_RATE             IB_MIN_RATE
#define OSM_QOS_POLICY_MAX_MTU              IB_MAX_MTU
#define OSM_QOS_POLICY_MIN_MTU              IB_MIN_MTU

typedef struct tmp_parser_struct_t_ {
    char       str[OSM_QOS_POLICY_MAX_LINE_LEN];
    uint64_t   num_pair[2];
    cl_list_t  str_list;
    cl_list_t  num_list;
    cl_list_t  num_pair_list;
} tmp_parser_struct_t;

static void __parser_tmp_struct_init();
static void __parser_tmp_struct_reset();
static void __parser_tmp_struct_destroy();

static char * __parser_strip_white(char * str);

static void __parser_str2uint64(uint64_t * p_val, char * str);

static void __parser_port_group_start();
static int __parser_port_group_end();

static void __parser_sl2vl_scope_start();
static int __parser_sl2vl_scope_end();

static void __parser_vlarb_scope_start();
static int __parser_vlarb_scope_end();

static void __parser_qos_level_start();
static int __parser_qos_level_end();

static void __parser_match_rule_start();
static int __parser_match_rule_end();

static void __parser_ulp_match_rule_start();
static int __parser_ulp_match_rule_end();

static void __pkey_rangelist2rangearr(
    cl_list_t    * p_list,
    uint64_t  ** * p_arr,
    unsigned     * p_arr_len);

static void __rangelist2rangearr(
    cl_list_t    * p_list,
    uint64_t  ** * p_arr,
    unsigned     * p_arr_len);

static void __merge_rangearr(
    uint64_t  **   range_arr_1,
    unsigned       range_len_1,
    uint64_t  **   range_arr_2,
    unsigned       range_len_2,
    uint64_t  ** * p_arr,
    unsigned     * p_arr_len );

static void __parser_add_port_to_port_map(
    cl_qmap_t   * p_map,
    osm_physp_t * p_physp);

static void __parser_add_guid_range_to_port_map(
    cl_qmap_t  * p_map,
    uint64_t  ** range_arr,
    unsigned     range_len);

static void __parser_add_pkey_range_to_port_map(
    cl_qmap_t  * p_map,
    uint64_t  ** range_arr,
    unsigned     range_len);

static void __parser_add_partition_list_to_port_map(
    cl_qmap_t  * p_map,
    cl_list_t  * p_list);

static void __parser_add_map_to_port_map(
    cl_qmap_t * p_dmap,
    cl_map_t  * p_smap);

static int __validate_pkeys(
    uint64_t ** range_arr,
    unsigned    range_len,
    boolean_t   is_ipoib);

static void __setup_simple_qos_levels();
static void __clear_simple_qos_levels();
static void __setup_ulp_match_rules();
static void __process_ulp_match_rules();
static void yyerror(const char *format, ...);

extern char * yytext;
extern int yylex (void);
extern FILE * yyin;
extern int errno;
extern void yyrestart(FILE *input_file);
int yyparse();

#define RESET_BUFFER  __parser_tmp_struct_reset()

tmp_parser_struct_t tmp_parser_struct;

int column_num;
int line_num;

osm_qos_policy_t       * p_qos_policy = NULL;
osm_qos_port_group_t   * p_current_port_group = NULL;
osm_qos_sl2vl_scope_t  * p_current_sl2vl_scope = NULL;
osm_qos_vlarb_scope_t  * p_current_vlarb_scope = NULL;
osm_qos_level_t        * p_current_qos_level = NULL;
osm_qos_match_rule_t   * p_current_qos_match_rule = NULL;
osm_log_t              * p_qos_parser_osm_log;

/* 16 Simple QoS Levels - one for each SL */
static osm_qos_level_t osm_qos_policy_simple_qos_levels[16];

/* Default Simple QoS Level */
osm_qos_level_t __default_simple_qos_level;

/*
 * List of match rules that will be generated by the
 * qos-ulp section. These rules are concatenated to
 * the end of the usual matching rules list at the
 * end of parsing.
 */
static cl_list_t __ulp_match_rules;

/***************************************************/

%}

%token TK_NUMBER
%token TK_DASH
%token TK_DOTDOT
%token TK_COMMA
%token TK_ASTERISK
%token TK_TEXT

%token TK_QOS_ULPS_START
%token TK_QOS_ULPS_END

%token TK_PORT_GROUPS_START
%token TK_PORT_GROUPS_END
%token TK_PORT_GROUP_START
%token TK_PORT_GROUP_END

%token TK_QOS_SETUP_START
%token TK_QOS_SETUP_END
%token TK_VLARB_TABLES_START
%token TK_VLARB_TABLES_END
%token TK_VLARB_SCOPE_START
%token TK_VLARB_SCOPE_END

%token TK_SL2VL_TABLES_START
%token TK_SL2VL_TABLES_END
%token TK_SL2VL_SCOPE_START
%token TK_SL2VL_SCOPE_END

%token TK_QOS_LEVELS_START
%token TK_QOS_LEVELS_END
%token TK_QOS_LEVEL_START
%token TK_QOS_LEVEL_END

%token TK_QOS_MATCH_RULES_START
%token TK_QOS_MATCH_RULES_END
%token TK_QOS_MATCH_RULE_START
%token TK_QOS_MATCH_RULE_END

%token TK_NAME
%token TK_USE
%token TK_PORT_GUID
%token TK_PORT_NAME
%token TK_PARTITION
%token TK_NODE_TYPE
%token TK_GROUP
%token TK_ACROSS
%token TK_VLARB_HIGH
%token TK_VLARB_LOW
%token TK_VLARB_HIGH_LIMIT
%token TK_TO
%token TK_FROM
%token TK_ACROSS_TO
%token TK_ACROSS_FROM
%token TK_SL2VL_TABLE
%token TK_SL
%token TK_MTU_LIMIT
%token TK_RATE_LIMIT
%token TK_PACKET_LIFE
%token TK_PATH_BITS
%token TK_QOS_CLASS
%token TK_SOURCE
%token TK_DESTINATION
%token TK_SERVICE_ID
%token TK_QOS_LEVEL_NAME
%token TK_PKEY

%token TK_NODE_TYPE_ROUTER
%token TK_NODE_TYPE_CA
%token TK_NODE_TYPE_SWITCH
%token TK_NODE_TYPE_SELF
%token TK_NODE_TYPE_ALL

%token TK_ULP_DEFAULT
%token TK_ULP_ANY_SERVICE_ID
%token TK_ULP_ANY_PKEY
%token TK_ULP_ANY_TARGET_PORT_GUID
%token TK_ULP_ANY_SOURCE_PORT_GUID
%token TK_ULP_ANY_SOURCE_TARGET_PORT_GUID
%token TK_ULP_SDP_DEFAULT
%token TK_ULP_SDP_PORT
%token TK_ULP_RDS_DEFAULT
%token TK_ULP_RDS_PORT
%token TK_ULP_ISER_DEFAULT
%token TK_ULP_ISER_PORT
%token TK_ULP_SRP_GUID
%token TK_ULP_IPOIB_DEFAULT
%token TK_ULP_IPOIB_PKEY

%start head

%%

head:               qos_policy_entries
                    ;

qos_policy_entries: /* empty */
                    | qos_policy_entries qos_policy_entry
                    ;

qos_policy_entry:     qos_ulps_section
                    | port_groups_section
                    | qos_setup_section
                    | qos_levels_section
                    | qos_match_rules_section
                    ;

    /*
     * Parsing qos-ulps:
     * -------------------
     *  qos-ulps
     *      default                       : 0 #default SL
     *      sdp, port-num 30000           : 1 #SL for SDP when destination port is 30000
     *      sdp, port-num 10000-20000     : 2
     *      sdp                           : 0 #default SL for SDP
     *      srp, target-port-guid 0x1234  : 2
     *      rds, port-num 25000           : 2 #SL for RDS when destination port is 25000
     *      rds,                          : 0 #default SL for RDS
     *      iser, port-num 900            : 5 #SL for iSER where target port is 900
     *      iser                          : 4 #default SL for iSER
     *      ipoib, pkey 0x0001            : 5 #SL for IPoIB on partition with pkey 0x0001
     *      ipoib                         : 6 #default IPoIB partition - pkey=0x7FFF
     *      any, service-id 0x6234        : 2
     *      any, pkey 0x0ABC              : 3
     *      any, target-port-guid 0x0ABC-0xFFFFF : 6
     *      any, source-port-guid 0x1234  : 7
     *      any, source-target-port-guid 0x5678 : 8
     *  end-qos-ulps
     */

qos_ulps_section: TK_QOS_ULPS_START qos_ulps TK_QOS_ULPS_END
                     ;

qos_ulps:             qos_ulp
                    | qos_ulps qos_ulp
                    ;

    /*
     * Parsing port groups:
     * -------------------
     *  port-groups
     *       port-group
     *          name: Storage
     *          use: our SRP storage targets
     *          port-guid: 0x1000000000000001,0x1000000000000002
     *          ...
     *          port-name: vs1 HCA-1/P1
     *          port-name: node_description/P2
     *          ...
     *          pkey: 0x00FF-0x0FFF
     *          ...
     *          partition: Part1
     *          ...
     *          node-type: ROUTER,CA,SWITCH,SELF,ALL
     *          ...
     *      end-port-group
     *      port-group
     *          ...
     *      end-port-group
     *  end-port-groups
     */


port_groups_section: TK_PORT_GROUPS_START port_groups TK_PORT_GROUPS_END
                     ;

port_groups:        port_group
                    | port_groups port_group
                    ;

port_group:         port_group_start port_group_entries port_group_end
                    ;

port_group_start:   TK_PORT_GROUP_START {
                        __parser_port_group_start();
                    }
                    ;

port_group_end:     TK_PORT_GROUP_END {
                        if ( __parser_port_group_end() )
                            return 1;
                    }
                    ;

port_group_entries: /* empty */
                    | port_group_entries port_group_entry
                    ;

port_group_entry:     port_group_name
                    | port_group_use
                    | port_group_port_guid
                    | port_group_port_name
                    | port_group_pkey
                    | port_group_partition
                    | port_group_node_type
                    ;


    /*
     * Parsing qos setup:
     * -----------------
     *  qos-setup
     *      vlarb-tables
     *          vlarb-scope
     *              ...
     *          end-vlarb-scope
     *          vlarb-scope
     *              ...
     *          end-vlarb-scope
     *     end-vlarb-tables
     *     sl2vl-tables
     *          sl2vl-scope
     *              ...
     *         end-sl2vl-scope
     *         sl2vl-scope
     *              ...
     *          end-sl2vl-scope
     *     end-sl2vl-tables
     *  end-qos-setup
     */

qos_setup_section:  TK_QOS_SETUP_START qos_setup_items TK_QOS_SETUP_END
                    ;

qos_setup_items:    /* empty */
                    | qos_setup_items vlarb_tables
                    | qos_setup_items sl2vl_tables
                    ;

    /* Parsing vlarb-tables */

vlarb_tables:       TK_VLARB_TABLES_START vlarb_scope_items TK_VLARB_TABLES_END
                    ;

vlarb_scope_items:  /* empty */
                    | vlarb_scope_items vlarb_scope
                    ;

vlarb_scope:        vlarb_scope_start vlarb_scope_entries vlarb_scope_end
                    ;

vlarb_scope_start:  TK_VLARB_SCOPE_START {
                        __parser_vlarb_scope_start();
                    }
                    ;

vlarb_scope_end:    TK_VLARB_SCOPE_END {
                        if ( __parser_vlarb_scope_end() )
                            return 1;
                    }
                    ;

vlarb_scope_entries:/* empty */
                    | vlarb_scope_entries vlarb_scope_entry
                    ;

    /*
     *          vlarb-scope
     *              group: Storage
     *              ...
     *              across: Storage
     *              ...
     *              vlarb-high: 0:255,1:127,2:63,3:31,4:15,5:7,6:3,7:1
     *              vlarb-low: 8:255,9:127,10:63,11:31,12:15,13:7,14:3
     *              vl-high-limit: 10
     *          end-vlarb-scope
     */

vlarb_scope_entry:    vlarb_scope_group
                    | vlarb_scope_across
                    | vlarb_scope_vlarb_high
                    | vlarb_scope_vlarb_low
                    | vlarb_scope_vlarb_high_limit
                    ;

    /* Parsing sl2vl-tables */

sl2vl_tables:       TK_SL2VL_TABLES_START sl2vl_scope_items TK_SL2VL_TABLES_END
                    ;

sl2vl_scope_items:  /* empty */
                    | sl2vl_scope_items sl2vl_scope
                    ;

sl2vl_scope:        sl2vl_scope_start sl2vl_scope_entries sl2vl_scope_end
                    ;

sl2vl_scope_start:  TK_SL2VL_SCOPE_START {
                        __parser_sl2vl_scope_start();
                    }
                    ;

sl2vl_scope_end:    TK_SL2VL_SCOPE_END {
                        if ( __parser_sl2vl_scope_end() )
                            return 1;
                    }
                    ;

sl2vl_scope_entries:/* empty */
                    | sl2vl_scope_entries sl2vl_scope_entry
                    ;

    /*
     *          sl2vl-scope
     *              group: Part1
     *              ...
     *              from: *
     *              ...
     *              to: *
     *              ...
     *              across-to: Storage2
     *              ...
     *              across-from: Storage1
     *              ...
     *              sl2vl-table: 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,7
     *          end-sl2vl-scope
     */

sl2vl_scope_entry:    sl2vl_scope_group
                    | sl2vl_scope_across
                    | sl2vl_scope_across_from
                    | sl2vl_scope_across_to
                    | sl2vl_scope_from
                    | sl2vl_scope_to
                    | sl2vl_scope_sl2vl_table
                    ;

    /*
     * Parsing qos-levels:
     * ------------------
     *  qos-levels
     *      qos-level
     *          name: qos_level_1
     *          use: for the lowest priority communication
     *          sl: 15
     *          mtu-limit: 1
     *          rate-limit: 1
     *          packet-life: 12
     *          path-bits: 2,4,8-32
     *          pkey: 0x00FF-0x0FFF
     *      end-qos-level
     *          ...
     *      qos-level
     *    end-qos-level
     *  end-qos-levels
     */


qos_levels_section: TK_QOS_LEVELS_START qos_levels TK_QOS_LEVELS_END
                    ;

qos_levels:         /* empty */
                    | qos_levels qos_level
                    ;

qos_level:          qos_level_start qos_level_entries qos_level_end
                    ;

qos_level_start:    TK_QOS_LEVEL_START {
                        __parser_qos_level_start();
                    }
                    ;

qos_level_end:      TK_QOS_LEVEL_END {
                        if ( __parser_qos_level_end() )
                            return 1;
                    }
                    ;

qos_level_entries:  /* empty */
                    | qos_level_entries qos_level_entry
                    ;

qos_level_entry:      qos_level_name
                    | qos_level_use
                    | qos_level_sl
                    | qos_level_mtu_limit
                    | qos_level_rate_limit
                    | qos_level_packet_life
                    | qos_level_path_bits
                    | qos_level_pkey
                    ;

    /*
     * Parsing qos-match-rules:
     * -----------------------
     *  qos-match-rules
     *      qos-match-rule
     *          use: low latency by class 7-9 or 11 and bla bla
     *          qos-class: 7-9,11
     *          qos-level-name: default
     *          source: Storage
     *          destination: Storage
     *          service-id: 22,4719-5000
     *          pkey: 0x00FF-0x0FFF
     *      end-qos-match-rule
     *      qos-match-rule
     *          ...
     *      end-qos-match-rule
     *  end-qos-match-rules
     */

qos_match_rules_section: TK_QOS_MATCH_RULES_START qos_match_rules TK_QOS_MATCH_RULES_END
                    ;

qos_match_rules:    /* empty */
                    | qos_match_rules qos_match_rule
                    ;

qos_match_rule:     qos_match_rule_start qos_match_rule_entries qos_match_rule_end
                    ;

qos_match_rule_start: TK_QOS_MATCH_RULE_START {
                        __parser_match_rule_start();
                    }
                    ;

qos_match_rule_end: TK_QOS_MATCH_RULE_END {
                        if ( __parser_match_rule_end() )
                            return 1;
                    }
                    ;

qos_match_rule_entries: /* empty */
                    | qos_match_rule_entries qos_match_rule_entry
                    ;

qos_match_rule_entry: qos_match_rule_use
                    | qos_match_rule_qos_class
                    | qos_match_rule_qos_level_name
                    | qos_match_rule_source
                    | qos_match_rule_destination
                    | qos_match_rule_service_id
                    | qos_match_rule_pkey
                    ;


    /*
     * Parsing qos-ulps:
     * -----------------
     *   default
     *   sdp
     *   sdp with port-num
     *   rds
     *   rds with port-num
     *   srp with target-port-guid
     *   iser
     *   iser with port-num
     *   ipoib
     *   ipoib with pkey
     *   any with service-id
     *   any with pkey
     *   any with target-port-guid
     *   any with source-port-guid
     *   any with source-target-port-guid
     */

qos_ulp:            TK_ULP_DEFAULT single_number {
                        /* parsing default ulp rule: "default: num" */
                        cl_list_iterator_t    list_iterator;
                        uint64_t            * p_tmp_num;

                        list_iterator = cl_list_head(&tmp_parser_struct.num_list);
                        p_tmp_num = (uint64_t*)cl_list_obj(list_iterator);
                        if (*p_tmp_num > 15)
                        {
                            yyerror("illegal SL value");
                            return 1;
                        }
                        __default_simple_qos_level.sl = (uint8_t)(*p_tmp_num);
                        __default_simple_qos_level.sl_set = TRUE;
                        free(p_tmp_num);
                        cl_list_remove_all(&tmp_parser_struct.num_list);
                    }

                    | qos_ulp_type_any_service list_of_ranges TK_DOTDOT {
                        /* "any, service-id ... : sl" - one instance of list of ranges */
                        uint64_t ** range_arr;
                        unsigned    range_len;

                        if (!cl_list_count(&tmp_parser_struct.num_pair_list))
                        {
                            yyerror("ULP rule doesn't have service ids");
                            return 1;
                        }

                        /* get all the service id ranges */
                        __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                              &range_arr,
                                              &range_len );

                        p_current_qos_match_rule->service_id_range_arr = range_arr;
                        p_current_qos_match_rule->service_id_range_len = range_len;

                    } qos_ulp_sl

                    | qos_ulp_type_any_pkey list_of_ranges TK_DOTDOT {
                        /* "any, pkey ... : sl" - one instance of list of ranges */
                        uint64_t ** range_arr;
                        unsigned    range_len;

                        if (!cl_list_count(&tmp_parser_struct.num_pair_list))
                        {
                            yyerror("ULP rule doesn't have pkeys");
                            return 1;
                        }

                        /* get all the pkey ranges */
                        __pkey_rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                              &range_arr,
                                              &range_len );

                        p_current_qos_match_rule->pkey_range_arr = range_arr;
                        p_current_qos_match_rule->pkey_range_len = range_len;

                    } qos_ulp_sl

                    | qos_ulp_type_any_target_port_guid list_of_ranges TK_DOTDOT {
                        /* any, target-port-guid ... : sl */
                        uint64_t ** range_arr;
                        unsigned    range_len;

                        if (!cl_list_count(&tmp_parser_struct.num_pair_list))
                        {
                            yyerror("ULP rule doesn't have port guids");
                            return 1;
                        }

                        /* create a new port group with these ports */
                        __parser_port_group_start();

                        p_current_port_group->name = strdup("_ULP_Targets_");
                        p_current_port_group->use = strdup("Generated from ULP rules");

                        __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                              &range_arr,
                                              &range_len );

                        __parser_add_guid_range_to_port_map(
                                              &p_current_port_group->port_map,
                                              range_arr,
                                              range_len);

                        /* add this port group to the destination
                           groups of the current match rule */
                        cl_list_insert_tail(&p_current_qos_match_rule->destination_group_list,
                                            p_current_port_group);

                        __parser_port_group_end();

                    } qos_ulp_sl

		    | qos_ulp_type_any_source_port_guid list_of_ranges TK_DOTDOT {
			/* any, source-port-guid ... : sl */
			uint64_t ** range_arr;
			unsigned    range_len;

			if (!cl_list_count(&tmp_parser_struct.num_pair_list))
			{
				yyerror("ULP rule doesn't have port guids");
				return 1;
			}

                        /* create a new port group with these ports */
                        __parser_port_group_start();

                        p_current_port_group->name = strdup("_ULP_Sources_");
                        p_current_port_group->use = strdup("Generated from ULP rules");

                        __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                              &range_arr,
                                              &range_len );

                        __parser_add_guid_range_to_port_map(
                                              &p_current_port_group->port_map,
                                              range_arr,
                                              range_len);

                        /* add this port group to the source
                           groups of the current match rule */
                        cl_list_insert_tail(&p_current_qos_match_rule->source_group_list,
                                            p_current_port_group);

                        __parser_port_group_end();

		    } qos_ulp_sl

		    | qos_ulp_type_any_source_target_port_guid list_of_ranges TK_DOTDOT {
			/* any, source-target-port-guid ... : sl */
			uint64_t ** range_arr;
			unsigned    range_len;

			if (!cl_list_count(&tmp_parser_struct.num_pair_list))
			{
				yyerror("ULP rule doesn't have port guids");
				return 1;
			}

                        /* create a new port group with these ports */
                        __parser_port_group_start();

                        p_current_port_group->name = strdup("_ULP_Sources_Targets_");
                        p_current_port_group->use = strdup("Generated from ULP rules");

                        __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                              &range_arr,
                                              &range_len );

                        __parser_add_guid_range_to_port_map(
                                              &p_current_port_group->port_map,
                                              range_arr,
                                              range_len);

                        /* add this port group to the source and destination
                           groups of the current match rule */
                        cl_list_insert_tail(&p_current_qos_match_rule->source_group_list,
                                            p_current_port_group);

                        cl_list_insert_tail(&p_current_qos_match_rule->destination_group_list,
                                            p_current_port_group);

                        __parser_port_group_end();

		    } qos_ulp_sl

                    | qos_ulp_type_sdp_default {
                        /* "sdp : sl" - default SL for SDP */
                        uint64_t ** range_arr =
                               (uint64_t **)malloc(sizeof(uint64_t *));
                        range_arr[0] = (uint64_t *)malloc(2*sizeof(uint64_t));
                        range_arr[0][0] = OSM_QOS_POLICY_ULP_SDP_SERVICE_ID;
                        range_arr[0][1] = OSM_QOS_POLICY_ULP_SDP_SERVICE_ID + 0xFFFF;

                        p_current_qos_match_rule->service_id_range_arr = range_arr;
                        p_current_qos_match_rule->service_id_range_len = 1;

                    } qos_ulp_sl

                    | qos_ulp_type_sdp_port list_of_ranges TK_DOTDOT {
                        /* sdp with port numbers */
                        uint64_t ** range_arr;
                        unsigned    range_len;
                        unsigned    i;

                        if (!cl_list_count(&tmp_parser_struct.num_pair_list))
                        {
                            yyerror("SDP ULP rule doesn't have port numbers");
                            return 1;
                        }

                        /* get all the port ranges */
                        __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                              &range_arr,
                                              &range_len );
                        /* now translate these port numbers into service ids */
                        for (i = 0; i < range_len; i++)
                        {
                            if (range_arr[i][0] > 0xFFFF || range_arr[i][1] > 0xFFFF)
                            {
                                yyerror("SDP port number out of range");
				free(range_arr);
                                return 1;
                            }
                            range_arr[i][0] += OSM_QOS_POLICY_ULP_SDP_SERVICE_ID;
                            range_arr[i][1] += OSM_QOS_POLICY_ULP_SDP_SERVICE_ID;
                        }

                        p_current_qos_match_rule->service_id_range_arr = range_arr;
                        p_current_qos_match_rule->service_id_range_len = range_len;

                    } qos_ulp_sl

                    | qos_ulp_type_rds_default {
                        /* "rds : sl" - default SL for RDS */
                        uint64_t ** range_arr =
                               (uint64_t **)malloc(sizeof(uint64_t *));
                        range_arr[0] = (uint64_t *)malloc(2*sizeof(uint64_t));
                        range_arr[0][0] = range_arr[0][1] =
                           OSM_QOS_POLICY_ULP_RDS_SERVICE_ID + OSM_QOS_POLICY_ULP_RDS_PORT;

                        p_current_qos_match_rule->service_id_range_arr = range_arr;
                        p_current_qos_match_rule->service_id_range_len = 1;

                    } qos_ulp_sl

                    | qos_ulp_type_rds_port list_of_ranges TK_DOTDOT {
                        /* rds with port numbers */
                        uint64_t ** range_arr;
                        unsigned    range_len;
                        unsigned    i;

                        if (!cl_list_count(&tmp_parser_struct.num_pair_list))
                        {
                            yyerror("RDS ULP rule doesn't have port numbers");
                            return 1;
                        }

                        /* get all the port ranges */
                        __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                              &range_arr,
                                              &range_len );
                        /* now translate these port numbers into service ids */
                        for (i = 0; i < range_len; i++)
                        {
                            if (range_arr[i][0] > 0xFFFF || range_arr[i][1] > 0xFFFF)
                            {
                                yyerror("SDP port number out of range");
				free(range_arr);
                                return 1;
                            }
                            range_arr[i][0] += OSM_QOS_POLICY_ULP_RDS_SERVICE_ID;
                            range_arr[i][1] += OSM_QOS_POLICY_ULP_RDS_SERVICE_ID;
                        }

                        p_current_qos_match_rule->service_id_range_arr = range_arr;
                        p_current_qos_match_rule->service_id_range_len = range_len;

                    } qos_ulp_sl

                    | qos_ulp_type_iser_default {
                        /* "iSER : sl" - default SL for iSER */
                        uint64_t ** range_arr =
                               (uint64_t **)malloc(sizeof(uint64_t *));
                        range_arr[0] = (uint64_t *)malloc(2*sizeof(uint64_t));
                        range_arr[0][0] = range_arr[0][1] =
                           OSM_QOS_POLICY_ULP_ISER_SERVICE_ID + OSM_QOS_POLICY_ULP_ISER_PORT;

                        p_current_qos_match_rule->service_id_range_arr = range_arr;
                        p_current_qos_match_rule->service_id_range_len = 1;

                    } qos_ulp_sl

                    | qos_ulp_type_iser_port list_of_ranges TK_DOTDOT {
                        /* iser with port numbers */
                        uint64_t ** range_arr;
                        unsigned    range_len;
                        unsigned    i;

                        if (!cl_list_count(&tmp_parser_struct.num_pair_list))
                        {
                            yyerror("iSER ULP rule doesn't have port numbers");
                            return 1;
                        }

                        /* get all the port ranges */
                        __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                              &range_arr,
                                              &range_len );
                        /* now translate these port numbers into service ids */
                        for (i = 0; i < range_len; i++)
                        {
                            if (range_arr[i][0] > 0xFFFF || range_arr[i][1] > 0xFFFF)
                            {
                                yyerror("SDP port number out of range");
				free(range_arr);
                                return 1;
                            }
                            range_arr[i][0] += OSM_QOS_POLICY_ULP_ISER_SERVICE_ID;
                            range_arr[i][1] += OSM_QOS_POLICY_ULP_ISER_SERVICE_ID;
                        }

                        p_current_qos_match_rule->service_id_range_arr = range_arr;
                        p_current_qos_match_rule->service_id_range_len = range_len;

                    } qos_ulp_sl

                    | qos_ulp_type_srp_guid list_of_ranges TK_DOTDOT {
                        /* srp with target guids - this rule is similar
                           to writing 'any' ulp with target port guids */
                        uint64_t ** range_arr;
                        unsigned    range_len;

                        if (!cl_list_count(&tmp_parser_struct.num_pair_list))
                        {
                            yyerror("SRP ULP rule doesn't have port guids");
                            return 1;
                        }

                        /* create a new port group with these ports */
                        __parser_port_group_start();

                        p_current_port_group->name = strdup("_SRP_Targets_");
                        p_current_port_group->use = strdup("Generated from ULP rules");

                        __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                              &range_arr,
                                              &range_len );

                        __parser_add_guid_range_to_port_map(
                                              &p_current_port_group->port_map,
                                              range_arr,
                                              range_len);

                        /* add this port group to the destination
                           groups of the current match rule */
                        cl_list_insert_tail(&p_current_qos_match_rule->destination_group_list,
                                            p_current_port_group);

                        __parser_port_group_end();

                    } qos_ulp_sl

                    | qos_ulp_type_ipoib_default {
                        /* ipoib w/o any pkeys (default pkey) */
                        uint64_t ** range_arr =
                               (uint64_t **)malloc(sizeof(uint64_t *));
                        range_arr[0] = (uint64_t *)malloc(2*sizeof(uint64_t));
                        range_arr[0][0] = range_arr[0][1] = 0x7fff;

                        /*
                         * Although we know that the default partition exists,
                         * we still need to validate it by checking that it has
                         * at least two full members. Otherwise IPoIB won't work.
                         */
                        if (__validate_pkeys(range_arr, 1, TRUE))
                            return 1;

                        p_current_qos_match_rule->pkey_range_arr = range_arr;
                        p_current_qos_match_rule->pkey_range_len = 1;

                    } qos_ulp_sl

                    | qos_ulp_type_ipoib_pkey list_of_ranges TK_DOTDOT {
                        /* ipoib with pkeys */
                        uint64_t ** range_arr;
                        unsigned    range_len;

                        if (!cl_list_count(&tmp_parser_struct.num_pair_list))
                        {
                            yyerror("IPoIB ULP rule doesn't have pkeys");
                            return 1;
                        }

                        /* get all the pkey ranges */
                        __pkey_rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                              &range_arr,
                                              &range_len );

                        /*
                         * Validate pkeys.
                         * For IPoIB pkeys the validation is strict.
                         * If some problem would be found, parsing will
                         * be aborted with a proper error messages.
                         */
			if (__validate_pkeys(range_arr, range_len, TRUE)) {
			    free(range_arr);
                            return 1;
			}

                        p_current_qos_match_rule->pkey_range_arr = range_arr;
                        p_current_qos_match_rule->pkey_range_len = range_len;

                    } qos_ulp_sl
                    ;

qos_ulp_type_any_service: TK_ULP_ANY_SERVICE_ID
                    { __parser_ulp_match_rule_start(); };

qos_ulp_type_any_pkey: TK_ULP_ANY_PKEY
                    { __parser_ulp_match_rule_start(); };

qos_ulp_type_any_target_port_guid: TK_ULP_ANY_TARGET_PORT_GUID
                    { __parser_ulp_match_rule_start(); };

qos_ulp_type_any_source_port_guid: TK_ULP_ANY_SOURCE_PORT_GUID
                    { __parser_ulp_match_rule_start(); };

qos_ulp_type_any_source_target_port_guid: TK_ULP_ANY_SOURCE_TARGET_PORT_GUID
                    { __parser_ulp_match_rule_start(); };

qos_ulp_type_sdp_default: TK_ULP_SDP_DEFAULT
                    { __parser_ulp_match_rule_start(); };

qos_ulp_type_sdp_port: TK_ULP_SDP_PORT
                    { __parser_ulp_match_rule_start(); };

qos_ulp_type_rds_default: TK_ULP_RDS_DEFAULT
                    { __parser_ulp_match_rule_start(); };

qos_ulp_type_rds_port: TK_ULP_RDS_PORT
                    { __parser_ulp_match_rule_start(); };

qos_ulp_type_iser_default: TK_ULP_ISER_DEFAULT
                    { __parser_ulp_match_rule_start(); };

qos_ulp_type_iser_port: TK_ULP_ISER_PORT
                    { __parser_ulp_match_rule_start(); };

qos_ulp_type_srp_guid: TK_ULP_SRP_GUID
                    { __parser_ulp_match_rule_start(); };

qos_ulp_type_ipoib_default: TK_ULP_IPOIB_DEFAULT
                    { __parser_ulp_match_rule_start(); };

qos_ulp_type_ipoib_pkey: TK_ULP_IPOIB_PKEY
                    { __parser_ulp_match_rule_start(); };


qos_ulp_sl:   single_number {
                        /* get the SL for ULP rules */
                        cl_list_iterator_t  list_iterator;
                        uint64_t          * p_tmp_num;
                        uint8_t             sl;

                        list_iterator = cl_list_head(&tmp_parser_struct.num_list);
                        p_tmp_num = (uint64_t*)cl_list_obj(list_iterator);
                        if (*p_tmp_num > 15)
                        {
                            yyerror("illegal SL value");
                            return 1;
                        }

                        sl = (uint8_t)(*p_tmp_num);
                        free(p_tmp_num);
                        cl_list_remove_all(&tmp_parser_struct.num_list);

                        p_current_qos_match_rule->p_qos_level =
                                 &osm_qos_policy_simple_qos_levels[sl];
                        p_current_qos_match_rule->qos_level_name =
                                 strdup(osm_qos_policy_simple_qos_levels[sl].name);

                        if (__parser_ulp_match_rule_end())
                            return 1;
                    }
                    ;

    /*
     *  port_group_entry values:
     *      port_group_name
     *      port_group_use
     *      port_group_port_guid
     *      port_group_port_name
     *      port_group_pkey
     *      port_group_partition
     *      port_group_node_type
     */

port_group_name:        port_group_name_start single_string {
                            /* 'name' of 'port-group' - one instance */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            if (p_current_port_group->name)
                            {
                                yyerror("port-group has multiple 'name' tags");
                                cl_list_remove_all(&tmp_parser_struct.str_list);
                                return 1;
                            }

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            if ( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    p_current_port_group->name = tmp_str;
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

port_group_name_start:  TK_NAME {
                            RESET_BUFFER;
                        }
                        ;

port_group_use:         port_group_use_start single_string {
                            /* 'use' of 'port-group' - one instance */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            if (p_current_port_group->use)
                            {
                                yyerror("port-group has multiple 'use' tags");
                                cl_list_remove_all(&tmp_parser_struct.str_list);
                                return 1;
                            }

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            if ( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    p_current_port_group->use = tmp_str;
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

port_group_use_start:   TK_USE {
                            RESET_BUFFER;
                        }
                        ;

port_group_port_name:   port_group_port_name_start string_list {
                            /* 'port-name' in 'port-group' - any num of instances */
                            cl_list_iterator_t list_iterator;
                            osm_node_t * p_node;
                            osm_physp_t * p_physp;
                            unsigned port_num;
                            char * tmp_str;
                            char * port_str;

                            /* parsing port name strings */
                            for (list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                                 list_iterator != cl_list_end(&tmp_parser_struct.str_list);
                                 list_iterator = cl_list_next(list_iterator))
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                {
                                    /* last slash in port name string is a separator
                                       between node name and port number */
                                    port_str = strrchr(tmp_str, '/');
                                    if (!port_str || (strlen(port_str) < 3) ||
                                        (port_str[1] != 'p' && port_str[1] != 'P')) {
                                        yyerror("'%s' - illegal port name",
                                                           tmp_str);
                                        free(tmp_str);
                                        cl_list_remove_all(&tmp_parser_struct.str_list);
                                        return 1;
                                    }

                                    if (!(port_num = strtoul(&port_str[2],NULL,0))) {
                                        yyerror(
                                               "'%s' - illegal port number in port name",
                                               tmp_str);
                                        free(tmp_str);
                                        cl_list_remove_all(&tmp_parser_struct.str_list);
                                        return 1;
                                    }

                                    /* separate node name from port number */
                                    port_str[0] = '\0';

                                    if (st_lookup(p_qos_policy->p_node_hash,
                                                  (st_data_t)tmp_str,
                                                  (void *)&p_node))
                                    {
                                        /* we found the node, now get the right port */
                                        p_physp = osm_node_get_physp_ptr(p_node, port_num);
                                        if (!p_physp) {
                                            yyerror(
                                                   "'%s' - port number out of range in port name",
                                                   tmp_str);
                                            free(tmp_str);
                                            cl_list_remove_all(&tmp_parser_struct.str_list);
                                            return 1;
                                        }
                                        /* we found the port, now add it to guid table */
                                        __parser_add_port_to_port_map(&p_current_port_group->port_map,
                                                                      p_physp);
                                    }
                                    free(tmp_str);
                                }
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

port_group_port_name_start: TK_PORT_NAME {
                            RESET_BUFFER;
                        }
                        ;

port_group_port_guid:   port_group_port_guid_start list_of_ranges {
                            /* 'port-guid' in 'port-group' - any num of instances */
                            /* list of guid ranges */
                            if (cl_list_count(&tmp_parser_struct.num_pair_list))
                            {
                                uint64_t ** range_arr;
                                unsigned range_len;

                                __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                                      &range_arr,
                                                      &range_len );

                                __parser_add_guid_range_to_port_map(
                                                      &p_current_port_group->port_map,
                                                      range_arr,
                                                      range_len);
                            }
                        }
                        ;

port_group_port_guid_start: TK_PORT_GUID {
                            RESET_BUFFER;
                        }
                        ;

port_group_pkey:        port_group_pkey_start list_of_ranges {
                            /* 'pkey' in 'port-group' - any num of instances */
                            /* list of pkey ranges */
                            if (cl_list_count(&tmp_parser_struct.num_pair_list))
                            {
                                uint64_t ** range_arr;
                                unsigned range_len;

                                __pkey_rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                                      &range_arr,
                                                      &range_len );

                                __parser_add_pkey_range_to_port_map(
                                                      &p_current_port_group->port_map,
                                                      range_arr,
                                                      range_len);
                            }
                        }
                        ;

port_group_pkey_start:  TK_PKEY {
                            RESET_BUFFER;
                        }
                        ;

port_group_partition:  port_group_partition_start string_list {
                            /* 'partition' in 'port-group' - any num of instances */
                            __parser_add_partition_list_to_port_map(
                                               &p_current_port_group->port_map,
                                               &tmp_parser_struct.str_list);
                        }
                        ;

port_group_partition_start: TK_PARTITION {
                            RESET_BUFFER;
                        }
                        ;

port_group_node_type:   port_group_node_type_start port_group_node_type_list {
                            /* 'node-type' in 'port-group' - any num of instances */
                        }
                        ;

port_group_node_type_start: TK_NODE_TYPE {
                            RESET_BUFFER;
                        }
                        ;

port_group_node_type_list:  node_type_item
                        |   port_group_node_type_list TK_COMMA node_type_item
                        ;

node_type_item:           node_type_ca
                        | node_type_switch
                        | node_type_router
                        | node_type_all
                        | node_type_self
                        ;

node_type_ca:           TK_NODE_TYPE_CA {
                            p_current_port_group->node_types |=
                               OSM_QOS_POLICY_NODE_TYPE_CA;
                        }
                        ;

node_type_switch:       TK_NODE_TYPE_SWITCH {
                            p_current_port_group->node_types |=
                               OSM_QOS_POLICY_NODE_TYPE_SWITCH;
                        }
                        ;

node_type_router:       TK_NODE_TYPE_ROUTER {
                            p_current_port_group->node_types |=
                               OSM_QOS_POLICY_NODE_TYPE_ROUTER;
                        }
                        ;

node_type_all:          TK_NODE_TYPE_ALL {
                            p_current_port_group->node_types |=
                               (OSM_QOS_POLICY_NODE_TYPE_CA |
                                OSM_QOS_POLICY_NODE_TYPE_SWITCH |
                                OSM_QOS_POLICY_NODE_TYPE_ROUTER);
                        }
                        ;

node_type_self:         TK_NODE_TYPE_SELF {
                            osm_port_t * p_osm_port =
                                osm_get_port_by_guid(p_qos_policy->p_subn,
                                     p_qos_policy->p_subn->sm_port_guid);
                            if (p_osm_port)
                                __parser_add_port_to_port_map(
                                   &p_current_port_group->port_map,
                                   p_osm_port->p_physp);
                        }
                        ;

    /*
     *  vlarb_scope_entry values:
     *      vlarb_scope_group
     *      vlarb_scope_across
     *      vlarb_scope_vlarb_high
     *      vlarb_scope_vlarb_low
     *      vlarb_scope_vlarb_high_limit
     */



vlarb_scope_group:      vlarb_scope_group_start string_list {
                            /* 'group' in 'vlarb-scope' - any num of instances */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    cl_list_insert_tail(&p_current_vlarb_scope->group_list,tmp_str);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

vlarb_scope_group_start: TK_GROUP {
                            RESET_BUFFER;
                        }
                        ;

vlarb_scope_across: vlarb_scope_across_start string_list {
                            /* 'across' in 'vlarb-scope' - any num of instances */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    cl_list_insert_tail(&p_current_vlarb_scope->across_list,tmp_str);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

vlarb_scope_across_start: TK_ACROSS {
                            RESET_BUFFER;
                        }
                        ;

vlarb_scope_vlarb_high_limit:  vlarb_scope_vlarb_high_limit_start single_number {
                            /* 'vl-high-limit' in 'vlarb-scope' - one instance of one number */
                            cl_list_iterator_t    list_iterator;
                            uint64_t            * p_tmp_num;

                            list_iterator = cl_list_head(&tmp_parser_struct.num_list);
                            p_tmp_num = (uint64_t*)cl_list_obj(list_iterator);
                            if (p_tmp_num)
                            {
                                p_current_vlarb_scope->vl_high_limit = (uint32_t)(*p_tmp_num);
                                p_current_vlarb_scope->vl_high_limit_set = TRUE;
                                free(p_tmp_num);
                            }

                            cl_list_remove_all(&tmp_parser_struct.num_list);
                        }
                        ;

vlarb_scope_vlarb_high_limit_start: TK_VLARB_HIGH_LIMIT {
                            RESET_BUFFER;
                        }
                        ;

vlarb_scope_vlarb_high: vlarb_scope_vlarb_high_start num_list_with_dotdot {
                            /* 'vlarb-high' in 'vlarb-scope' - list of pairs of numbers with ':' and ',' */
                            cl_list_iterator_t    list_iterator;
                            uint64_t            * num_pair;

                            list_iterator = cl_list_head(&tmp_parser_struct.num_pair_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.num_pair_list) )
                            {
                                num_pair = (uint64_t*)cl_list_obj(list_iterator);
                                if (num_pair)
                                    cl_list_insert_tail(&p_current_vlarb_scope->vlarb_high_list,num_pair);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.num_pair_list);
                        }
                        ;

vlarb_scope_vlarb_high_start: TK_VLARB_HIGH {
                            RESET_BUFFER;
                        }
                        ;

vlarb_scope_vlarb_low:  vlarb_scope_vlarb_low_start num_list_with_dotdot {
                            /* 'vlarb-low' in 'vlarb-scope' - list of pairs of numbers with ':' and ',' */
                            cl_list_iterator_t    list_iterator;
                            uint64_t            * num_pair;

                            list_iterator = cl_list_head(&tmp_parser_struct.num_pair_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.num_pair_list) )
                            {
                                num_pair = (uint64_t*)cl_list_obj(list_iterator);
                                if (num_pair)
                                    cl_list_insert_tail(&p_current_vlarb_scope->vlarb_low_list,num_pair);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.num_pair_list);
                        }
                        ;

vlarb_scope_vlarb_low_start: TK_VLARB_LOW {
                            RESET_BUFFER;
                        }
                        ;

    /*
     *  sl2vl_scope_entry values:
     *      sl2vl_scope_group
     *      sl2vl_scope_across
     *      sl2vl_scope_across_from
     *      sl2vl_scope_across_to
     *      sl2vl_scope_from
     *      sl2vl_scope_to
     *      sl2vl_scope_sl2vl_table
     */

sl2vl_scope_group:      sl2vl_scope_group_start string_list {
                            /* 'group' in 'sl2vl-scope' - any num of instances */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    cl_list_insert_tail(&p_current_sl2vl_scope->group_list,tmp_str);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

sl2vl_scope_group_start: TK_GROUP {
                            RESET_BUFFER;
                        }
                        ;

sl2vl_scope_across:     sl2vl_scope_across_start string_list {
                            /* 'across' in 'sl2vl-scope' - any num of instances */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str) {
                                    cl_list_insert_tail(&p_current_sl2vl_scope->across_from_list,tmp_str);
                                    cl_list_insert_tail(&p_current_sl2vl_scope->across_to_list,strdup(tmp_str));
                                }
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

sl2vl_scope_across_start: TK_ACROSS {
                            RESET_BUFFER;
                        }
                        ;

sl2vl_scope_across_from:  sl2vl_scope_across_from_start string_list {
                            /* 'across-from' in 'sl2vl-scope' - any num of instances */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    cl_list_insert_tail(&p_current_sl2vl_scope->across_from_list,tmp_str);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

sl2vl_scope_across_from_start: TK_ACROSS_FROM {
                            RESET_BUFFER;
                        }
                        ;

sl2vl_scope_across_to:  sl2vl_scope_across_to_start string_list {
                            /* 'across-to' in 'sl2vl-scope' - any num of instances */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str) {
                                    cl_list_insert_tail(&p_current_sl2vl_scope->across_to_list,tmp_str);
                                }
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

sl2vl_scope_across_to_start: TK_ACROSS_TO {
                            RESET_BUFFER;
                        }
                        ;

sl2vl_scope_from:       sl2vl_scope_from_start sl2vl_scope_from_list_or_asterisk {
                            /* 'from' in 'sl2vl-scope' - any num of instances */
                        }
                        ;

sl2vl_scope_from_start: TK_FROM {
                            RESET_BUFFER;
                        }
                        ;

sl2vl_scope_to:         sl2vl_scope_to_start sl2vl_scope_to_list_or_asterisk {
                            /* 'to' in 'sl2vl-scope' - any num of instances */
                        }
                        ;

sl2vl_scope_to_start:   TK_TO {
                            RESET_BUFFER;
                        }
                        ;

sl2vl_scope_from_list_or_asterisk:  sl2vl_scope_from_asterisk
                                  | sl2vl_scope_from_list_of_ranges
                                  ;

sl2vl_scope_from_asterisk: TK_ASTERISK {
                            int i;
                            for (i = 0; i < OSM_QOS_POLICY_MAX_PORTS_ON_SWITCH; i++)
                                p_current_sl2vl_scope->from[i] = TRUE;
                        }
                        ;

sl2vl_scope_to_list_or_asterisk:  sl2vl_scope_to_asterisk
                                | sl2vl_scope_to_list_of_ranges
                                  ;

sl2vl_scope_to_asterisk: TK_ASTERISK {
                            int i;
                            for (i = 0; i < OSM_QOS_POLICY_MAX_PORTS_ON_SWITCH; i++)
                                p_current_sl2vl_scope->to[i] = TRUE;
                        }
                        ;

sl2vl_scope_from_list_of_ranges: list_of_ranges {
                            int i;
                            cl_list_iterator_t    list_iterator;
                            uint64_t            * num_pair;
                            uint8_t               num1, num2;

                            list_iterator = cl_list_head(&tmp_parser_struct.num_pair_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.num_pair_list) )
                            {
                                num_pair = (uint64_t*)cl_list_obj(list_iterator);
                                if (num_pair)
                                {
                                    if ( num_pair[1] >= OSM_QOS_POLICY_MAX_PORTS_ON_SWITCH )
                                    {
                                        yyerror("port number out of range 'from' list");
                                        free(num_pair);
                                        cl_list_remove_all(&tmp_parser_struct.num_pair_list);
                                        return 1;
                                    }
                                    num1 = (uint8_t)num_pair[0];
                                    num2 = (uint8_t)num_pair[1];
                                    free(num_pair);
                                    for (i = num1; i <= num2; i++)
                                        p_current_sl2vl_scope->from[i] = TRUE;
                                }
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.num_pair_list);
                        }
                        ;

sl2vl_scope_to_list_of_ranges: list_of_ranges {
                            int i;
                            cl_list_iterator_t    list_iterator;
                            uint64_t            * num_pair;
                            uint8_t               num1, num2;

                            list_iterator = cl_list_head(&tmp_parser_struct.num_pair_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.num_pair_list) )
                            {
                                num_pair = (uint64_t*)cl_list_obj(list_iterator);
                                if (num_pair)
                                {
                                    if ( num_pair[1] >= OSM_QOS_POLICY_MAX_PORTS_ON_SWITCH )
                                    {
                                        yyerror("port number out of range 'to' list");
                                        free(num_pair);
                                        cl_list_remove_all(&tmp_parser_struct.num_pair_list);
                                        return 1;
                                    }
                                    num1 = (uint8_t)num_pair[0];
                                    num2 = (uint8_t)num_pair[1];
                                    free(num_pair);
                                    for (i = num1; i <= num2; i++)
                                        p_current_sl2vl_scope->to[i] = TRUE;
                                }
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.num_pair_list);
                        }
                        ;


sl2vl_scope_sl2vl_table:  sl2vl_scope_sl2vl_table_start num_list {
                            /* 'sl2vl-table' - one instance of exactly
                               OSM_QOS_POLICY_SL2VL_TABLE_LEN numbers */
                            cl_list_iterator_t    list_iterator;
                            uint64_t              num;
                            uint64_t            * p_num;
                            int                   i = 0;

                            if (p_current_sl2vl_scope->sl2vl_table_set)
                            {
                                yyerror("sl2vl-scope has more than one sl2vl-table");
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }

                            if (cl_list_count(&tmp_parser_struct.num_list) != OSM_QOS_POLICY_SL2VL_TABLE_LEN)
                            {
                                yyerror("wrong number of values in 'sl2vl-table' (should be 16)");
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }

                            list_iterator = cl_list_head(&tmp_parser_struct.num_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.num_list) )
                            {
                                p_num = (uint64_t*)cl_list_obj(list_iterator);
                                num = *p_num;
                                free(p_num);
                                if (num >= OSM_QOS_POLICY_MAX_VL_NUM)
                                {
                                    yyerror("wrong VL value in 'sl2vl-table' (should be 0 to 15)");
                                    cl_list_remove_all(&tmp_parser_struct.num_list);
                                    return 1;
                                }

                                p_current_sl2vl_scope->sl2vl_table[i++] = (uint8_t)num;
                                list_iterator = cl_list_next(list_iterator);
                            }
                            p_current_sl2vl_scope->sl2vl_table_set = TRUE;
                            cl_list_remove_all(&tmp_parser_struct.num_list);
                        }
                        ;

sl2vl_scope_sl2vl_table_start: TK_SL2VL_TABLE {
                            RESET_BUFFER;
                        }
                        ;

    /*
     *  qos_level_entry values:
     *      qos_level_name
     *      qos_level_use
     *      qos_level_sl
     *      qos_level_mtu_limit
     *      qos_level_rate_limit
     *      qos_level_packet_life
     *      qos_level_path_bits
     *      qos_level_pkey
     */

qos_level_name:         qos_level_name_start single_string {
                            /* 'name' of 'qos-level' - one instance */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            if (p_current_qos_level->name)
                            {
                                yyerror("qos-level has multiple 'name' tags");
                                cl_list_remove_all(&tmp_parser_struct.str_list);
                                return 1;
                            }

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            if ( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    p_current_qos_level->name = tmp_str;
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

qos_level_name_start:   TK_NAME {
                            RESET_BUFFER;
                        }
                        ;

qos_level_use:          qos_level_use_start single_string {
                            /* 'use' of 'qos-level' - one instance */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            if (p_current_qos_level->use)
                            {
                                yyerror("qos-level has multiple 'use' tags");
                                cl_list_remove_all(&tmp_parser_struct.str_list);
                                return 1;
                            }

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            if ( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    p_current_qos_level->use = tmp_str;
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

qos_level_use_start:    TK_USE {
                            RESET_BUFFER;
                        }
                        ;

qos_level_sl:           qos_level_sl_start single_number {
                            /* 'sl' in 'qos-level' - one instance */
                            cl_list_iterator_t   list_iterator;
                            uint64_t           * p_num;

                            if (p_current_qos_level->sl_set)
                            {
                                yyerror("'qos-level' has multiple 'sl' tags");
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }
                            list_iterator = cl_list_head(&tmp_parser_struct.num_list);
                            p_num = (uint64_t*)cl_list_obj(list_iterator);
                            p_current_qos_level->sl = (uint8_t)(*p_num);
                            free(p_num);
                            p_current_qos_level->sl_set = TRUE;
                            cl_list_remove_all(&tmp_parser_struct.num_list);
                        }
                        ;

qos_level_sl_start:     TK_SL {
                            RESET_BUFFER;
                        }
                        ;

qos_level_mtu_limit:    qos_level_mtu_limit_start single_number {
                            /* 'mtu-limit' in 'qos-level' - one instance */
                            cl_list_iterator_t   list_iterator;
                            uint64_t           * p_num;

                            if (p_current_qos_level->mtu_limit_set)
                            {
                                yyerror("'qos-level' has multiple 'mtu-limit' tags");
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }
                            list_iterator = cl_list_head(&tmp_parser_struct.num_list);
                            p_num = (uint64_t*)cl_list_obj(list_iterator);
                            if (*p_num > OSM_QOS_POLICY_MAX_MTU || *p_num < OSM_QOS_POLICY_MIN_MTU)
                            {
                                yyerror("mtu limit is out of range, value: %d", *p_num);
                                free(p_num);
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }
                            p_current_qos_level->mtu_limit = (uint8_t)(*p_num);
                            free(p_num);
                            p_current_qos_level->mtu_limit_set = TRUE;
                            cl_list_remove_all(&tmp_parser_struct.num_list);
                        }
                        ;

qos_level_mtu_limit_start: TK_MTU_LIMIT {
                            /* 'mtu-limit' in 'qos-level' - one instance */
                            RESET_BUFFER;
                        }
                        ;

qos_level_rate_limit:    qos_level_rate_limit_start single_number {
                            /* 'rate-limit' in 'qos-level' - one instance */
                            cl_list_iterator_t   list_iterator;
                            uint64_t           * p_num;

                            if (p_current_qos_level->rate_limit_set)
                            {
                                yyerror("'qos-level' has multiple 'rate-limit' tags");
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }
                            list_iterator = cl_list_head(&tmp_parser_struct.num_list);
                            p_num = (uint64_t*)cl_list_obj(list_iterator);
                            if (*p_num > OSM_QOS_POLICY_MAX_RATE || *p_num < OSM_QOS_POLICY_MIN_RATE)
                            {
                                yyerror("rate limit is out of range, value: %d", *p_num);
                                free(p_num);
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }
                            p_current_qos_level->rate_limit = (uint8_t)(*p_num);
                            free(p_num);
                            p_current_qos_level->rate_limit_set = TRUE;
                            cl_list_remove_all(&tmp_parser_struct.num_list);
                        }
                        ;

qos_level_rate_limit_start: TK_RATE_LIMIT {
                            /* 'rate-limit' in 'qos-level' - one instance */
                            RESET_BUFFER;
                        }
                        ;

qos_level_packet_life:  qos_level_packet_life_start single_number {
                            /* 'packet-life' in 'qos-level' - one instance */
                            cl_list_iterator_t   list_iterator;
                            uint64_t           * p_num;

                            if (p_current_qos_level->pkt_life_set)
                            {
                                yyerror("'qos-level' has multiple 'packet-life' tags");
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }
                            list_iterator = cl_list_head(&tmp_parser_struct.num_list);
                            p_num = (uint64_t*)cl_list_obj(list_iterator);
                            p_current_qos_level->pkt_life = (uint8_t)(*p_num);
                            free(p_num);
                            p_current_qos_level->pkt_life_set= TRUE;
                            cl_list_remove_all(&tmp_parser_struct.num_list);
                        }
                        ;

qos_level_packet_life_start: TK_PACKET_LIFE {
                            /* 'packet-life' in 'qos-level' - one instance */
                            RESET_BUFFER;
                        }
                        ;

qos_level_path_bits:    qos_level_path_bits_start list_of_ranges {
                            /* 'path-bits' in 'qos-level' - any num of instances */
                            /* list of path bit ranges */

                            if (cl_list_count(&tmp_parser_struct.num_pair_list))
                            {
                                uint64_t ** range_arr;
                                unsigned range_len;

                                __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                                      &range_arr,
                                                      &range_len );

                                if ( !p_current_qos_level->path_bits_range_len )
                                {
                                    p_current_qos_level->path_bits_range_arr = range_arr;
                                    p_current_qos_level->path_bits_range_len = range_len;
                                }
                                else
                                {
                                    uint64_t ** new_range_arr;
                                    unsigned new_range_len;
                                    __merge_rangearr( p_current_qos_level->path_bits_range_arr,
                                                      p_current_qos_level->path_bits_range_len,
                                                      range_arr,
                                                      range_len,
                                                      &new_range_arr,
                                                      &new_range_len );
                                    p_current_qos_level->path_bits_range_arr = new_range_arr;
                                    p_current_qos_level->path_bits_range_len = new_range_len;
                                }
                            }
                        }
                        ;

qos_level_path_bits_start: TK_PATH_BITS {
                            RESET_BUFFER;
                        }
                        ;

qos_level_pkey:         qos_level_pkey_start list_of_ranges {
                            /* 'pkey' in 'qos-level' - num of instances of list of ranges */
                            if (cl_list_count(&tmp_parser_struct.num_pair_list))
                            {
                                uint64_t ** range_arr;
                                unsigned range_len;

                                __pkey_rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                                      &range_arr,
                                                      &range_len );

                                if ( !p_current_qos_level->pkey_range_len )
                                {
                                    p_current_qos_level->pkey_range_arr = range_arr;
                                    p_current_qos_level->pkey_range_len = range_len;
                                }
                                else
                                {
                                    uint64_t ** new_range_arr;
                                    unsigned new_range_len;
                                    __merge_rangearr( p_current_qos_level->pkey_range_arr,
                                                      p_current_qos_level->pkey_range_len,
                                                      range_arr,
                                                      range_len,
                                                      &new_range_arr,
                                                      &new_range_len );
                                    p_current_qos_level->pkey_range_arr = new_range_arr;
                                    p_current_qos_level->pkey_range_len = new_range_len;
                                }
                            }
                        }
                        ;

qos_level_pkey_start:   TK_PKEY {
                            RESET_BUFFER;
                        }
                        ;

    /*
     *  qos_match_rule_entry values:
     *      qos_match_rule_use
     *      qos_match_rule_qos_class
     *      qos_match_rule_qos_level_name
     *      qos_match_rule_source
     *      qos_match_rule_destination
     *      qos_match_rule_service_id
     *      qos_match_rule_pkey
     */


qos_match_rule_use:     qos_match_rule_use_start single_string {
                            /* 'use' of 'qos-match-rule' - one instance */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            if (p_current_qos_match_rule->use)
                            {
                                yyerror("'qos-match-rule' has multiple 'use' tags");
                                cl_list_remove_all(&tmp_parser_struct.str_list);
                                return 1;
                            }

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            if ( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    p_current_qos_match_rule->use = tmp_str;
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

qos_match_rule_use_start: TK_USE {
                            RESET_BUFFER;
                        }
                        ;

qos_match_rule_qos_class: qos_match_rule_qos_class_start list_of_ranges {
                            /* 'qos-class' in 'qos-match-rule' - num of instances of list of ranges */
                            /* list of class ranges (QoS Class is 12-bit value) */
                            if (cl_list_count(&tmp_parser_struct.num_pair_list))
                            {
                                uint64_t ** range_arr;
                                unsigned range_len;

                                __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                                      &range_arr,
                                                      &range_len );

                                if ( !p_current_qos_match_rule->qos_class_range_len )
                                {
                                    p_current_qos_match_rule->qos_class_range_arr = range_arr;
                                    p_current_qos_match_rule->qos_class_range_len = range_len;
                                }
                                else
                                {
                                    uint64_t ** new_range_arr;
                                    unsigned new_range_len;
                                    __merge_rangearr( p_current_qos_match_rule->qos_class_range_arr,
                                                      p_current_qos_match_rule->qos_class_range_len,
                                                      range_arr,
                                                      range_len,
                                                      &new_range_arr,
                                                      &new_range_len );
                                    p_current_qos_match_rule->qos_class_range_arr = new_range_arr;
                                    p_current_qos_match_rule->qos_class_range_len = new_range_len;
                                }
                            }
                        }
                        ;

qos_match_rule_qos_class_start: TK_QOS_CLASS {
                            RESET_BUFFER;
                        }
                        ;

qos_match_rule_source:  qos_match_rule_source_start string_list {
                            /* 'source' in 'qos-match-rule' - text */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    cl_list_insert_tail(&p_current_qos_match_rule->source_list,tmp_str);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

qos_match_rule_source_start: TK_SOURCE {
                            RESET_BUFFER;
                        }
                        ;

qos_match_rule_destination: qos_match_rule_destination_start string_list {
                            /* 'destination' in 'qos-match-rule' - text */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    cl_list_insert_tail(&p_current_qos_match_rule->destination_list,tmp_str);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

qos_match_rule_destination_start: TK_DESTINATION {
                            RESET_BUFFER;
                        }
                        ;

qos_match_rule_qos_level_name:  qos_match_rule_qos_level_name_start single_string {
                            /* 'qos-level-name' in 'qos-match-rule' - single string */
                            cl_list_iterator_t   list_iterator;
                            char               * tmp_str;

                            if (p_current_qos_match_rule->qos_level_name)
                            {
                                yyerror("qos-match-rule has multiple 'qos-level-name' tags");
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            if ( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    p_current_qos_match_rule->qos_level_name = tmp_str;
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

qos_match_rule_qos_level_name_start: TK_QOS_LEVEL_NAME {
                            RESET_BUFFER;
                        }
                        ;

qos_match_rule_service_id: qos_match_rule_service_id_start list_of_ranges {
                            /* 'service-id' in 'qos-match-rule' - num of instances of list of ranges */
                            if (cl_list_count(&tmp_parser_struct.num_pair_list))
                            {
                                uint64_t ** range_arr;
                                unsigned range_len;

                                __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                                      &range_arr,
                                                      &range_len );

                                if ( !p_current_qos_match_rule->service_id_range_len )
                                {
                                    p_current_qos_match_rule->service_id_range_arr = range_arr;
                                    p_current_qos_match_rule->service_id_range_len = range_len;
                                }
                                else
                                {
                                    uint64_t ** new_range_arr;
                                    unsigned new_range_len;
                                    __merge_rangearr( p_current_qos_match_rule->service_id_range_arr,
                                                      p_current_qos_match_rule->service_id_range_len,
                                                      range_arr,
                                                      range_len,
                                                      &new_range_arr,
                                                      &new_range_len );
                                    p_current_qos_match_rule->service_id_range_arr = new_range_arr;
                                    p_current_qos_match_rule->service_id_range_len = new_range_len;
                                }
                            }
                        }
                        ;

qos_match_rule_service_id_start: TK_SERVICE_ID {
                            RESET_BUFFER;
                        }
                        ;

qos_match_rule_pkey:    qos_match_rule_pkey_start list_of_ranges {
                            /* 'pkey' in 'qos-match-rule' - num of instances of list of ranges */
                            if (cl_list_count(&tmp_parser_struct.num_pair_list))
                            {
                                uint64_t ** range_arr;
                                unsigned range_len;

                                __pkey_rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                                      &range_arr,
                                                      &range_len );

                                if ( !p_current_qos_match_rule->pkey_range_len )
                                {
                                    p_current_qos_match_rule->pkey_range_arr = range_arr;
                                    p_current_qos_match_rule->pkey_range_len = range_len;
                                }
                                else
                                {
                                    uint64_t ** new_range_arr;
                                    unsigned new_range_len;
                                    __merge_rangearr( p_current_qos_match_rule->pkey_range_arr,
                                                      p_current_qos_match_rule->pkey_range_len,
                                                      range_arr,
                                                      range_len,
                                                      &new_range_arr,
                                                      &new_range_len );
                                    p_current_qos_match_rule->pkey_range_arr = new_range_arr;
                                    p_current_qos_match_rule->pkey_range_len = new_range_len;
                                }
                            }
                        }
                        ;

qos_match_rule_pkey_start: TK_PKEY {
                            RESET_BUFFER;
                        }
                        ;


    /*
     * Common part
     */


single_string:      single_string_elems {
                        cl_list_insert_tail(&tmp_parser_struct.str_list,
                                            strdup(__parser_strip_white(tmp_parser_struct.str)));
                        tmp_parser_struct.str[0] = '\0';
                    }
                    ;

single_string_elems:  single_string_element
                    | single_string_elems single_string_element
                    ;

single_string_element: TK_TEXT {
                        strcat(tmp_parser_struct.str,$1);
                        free($1);
                    }
                    ;


string_list:        single_string
                    | string_list TK_COMMA single_string
                    ;



single_number:      number
                    ;

num_list:             number
                    | num_list TK_COMMA number
                    ;

number:             TK_NUMBER {
                        uint64_t * p_num = (uint64_t*)malloc(sizeof(uint64_t));
                        __parser_str2uint64(p_num,$1);
                        free($1);
                        cl_list_insert_tail(&tmp_parser_struct.num_list, p_num);
                    }
                    ;

num_list_with_dotdot: number_from_pair_1 TK_DOTDOT number_from_pair_2 {
                        uint64_t * num_pair = (uint64_t*)malloc(sizeof(uint64_t)*2);
                        num_pair[0] = tmp_parser_struct.num_pair[0];
                        num_pair[1] = tmp_parser_struct.num_pair[1];
                        cl_list_insert_tail(&tmp_parser_struct.num_pair_list, num_pair);
                    }
                    | num_list_with_dotdot TK_COMMA number_from_pair_1 TK_DOTDOT number_from_pair_2 {
                        uint64_t * num_pair = (uint64_t*)malloc(sizeof(uint64_t)*2);
                        num_pair[0] = tmp_parser_struct.num_pair[0];
                        num_pair[1] = tmp_parser_struct.num_pair[1];
                        cl_list_insert_tail(&tmp_parser_struct.num_pair_list, num_pair);
                    }
                    ;

number_from_pair_1:   TK_NUMBER {
                        __parser_str2uint64(&tmp_parser_struct.num_pair[0],$1);
                        free($1);
                    }
                    ;

number_from_pair_2:   TK_NUMBER {
                        __parser_str2uint64(&tmp_parser_struct.num_pair[1],$1);
                        free($1);
                    }
                    ;

list_of_ranges:     num_list_with_dash
                    ;

num_list_with_dash:   single_number_from_range {
                        uint64_t * num_pair = (uint64_t*)malloc(sizeof(uint64_t)*2);
                        num_pair[0] = tmp_parser_struct.num_pair[0];
                        num_pair[1] = tmp_parser_struct.num_pair[1];
                        cl_list_insert_tail(&tmp_parser_struct.num_pair_list, num_pair);
                    }
                    | number_from_range_1 TK_DASH number_from_range_2 {
                        uint64_t * num_pair = (uint64_t*)malloc(sizeof(uint64_t)*2);
                        if (tmp_parser_struct.num_pair[0] <= tmp_parser_struct.num_pair[1]) {
                            num_pair[0] = tmp_parser_struct.num_pair[0];
                            num_pair[1] = tmp_parser_struct.num_pair[1];
                        }
                        else {
                            num_pair[1] = tmp_parser_struct.num_pair[0];
                            num_pair[0] = tmp_parser_struct.num_pair[1];
                        }
                        cl_list_insert_tail(&tmp_parser_struct.num_pair_list, num_pair);
                    }
                    | num_list_with_dash TK_COMMA number_from_range_1 TK_DASH number_from_range_2 {
                        uint64_t * num_pair = (uint64_t*)malloc(sizeof(uint64_t)*2);
                        if (tmp_parser_struct.num_pair[0] <= tmp_parser_struct.num_pair[1]) {
                            num_pair[0] = tmp_parser_struct.num_pair[0];
                            num_pair[1] = tmp_parser_struct.num_pair[1];
                        }
                        else {
                            num_pair[1] = tmp_parser_struct.num_pair[0];
                            num_pair[0] = tmp_parser_struct.num_pair[1];
                        }
                        cl_list_insert_tail(&tmp_parser_struct.num_pair_list, num_pair);
                    }
                    | num_list_with_dash TK_COMMA single_number_from_range {
                        uint64_t * num_pair = (uint64_t*)malloc(sizeof(uint64_t)*2);
                        num_pair[0] = tmp_parser_struct.num_pair[0];
                        num_pair[1] = tmp_parser_struct.num_pair[1];
                        cl_list_insert_tail(&tmp_parser_struct.num_pair_list, num_pair);
                    }
                    ;

single_number_from_range:  TK_NUMBER {
                        __parser_str2uint64(&tmp_parser_struct.num_pair[0],$1);
                        __parser_str2uint64(&tmp_parser_struct.num_pair[1],$1);
                        free($1);
                    }
                    ;

number_from_range_1:  TK_NUMBER {
                        __parser_str2uint64(&tmp_parser_struct.num_pair[0],$1);
                        free($1);
                    }
                    ;

number_from_range_2:  TK_NUMBER {
                        __parser_str2uint64(&tmp_parser_struct.num_pair[1],$1);
                        free($1);
                    }
                    ;

%%

/***************************************************
 ***************************************************/

int osm_qos_parse_policy_file(IN osm_subn_t * p_subn)
{
    int res = 0;
    static boolean_t first_time = TRUE;
    p_qos_parser_osm_log = &p_subn->p_osm->log;

    OSM_LOG_ENTER(p_qos_parser_osm_log);

    osm_qos_policy_destroy(p_subn->p_qos_policy);
    p_subn->p_qos_policy = NULL;

    yyin = fopen (p_subn->opt.qos_policy_file, "r");
    if (!yyin)
    {
        if (strcmp(p_subn->opt.qos_policy_file,OSM_DEFAULT_QOS_POLICY_FILE)) {
            OSM_LOG(p_qos_parser_osm_log, OSM_LOG_ERROR, "ERR AC01: "
                    "Failed opening QoS policy file %s - %s\n",
                    p_subn->opt.qos_policy_file, strerror(errno));
            res = 1;
        }
        else
            OSM_LOG(p_qos_parser_osm_log, OSM_LOG_VERBOSE,
                    "QoS policy file not found (%s)\n",
                    p_subn->opt.qos_policy_file);

        goto Exit;
    }

    if (first_time)
    {
        first_time = FALSE;
        __setup_simple_qos_levels();
        __setup_ulp_match_rules();
        OSM_LOG(p_qos_parser_osm_log, OSM_LOG_INFO,
		"Loading QoS policy file (%s)\n",
                p_subn->opt.qos_policy_file);
    }
    else
        /*
         * ULP match rules list was emptied at the end of
         * previous parsing iteration.
         * What's left is to clear simple QoS levels.
         */
        __clear_simple_qos_levels();

    column_num = 1;
    line_num = 1;

    p_subn->p_qos_policy = osm_qos_policy_create(p_subn);

    __parser_tmp_struct_init();
    p_qos_policy = p_subn->p_qos_policy;

    res = yyparse();

    __parser_tmp_struct_destroy();

    if (res != 0)
    {
        OSM_LOG(p_qos_parser_osm_log, OSM_LOG_ERROR, "ERR AC03: "
                "Failed parsing QoS policy file (%s)\n",
                p_subn->opt.qos_policy_file);
        osm_qos_policy_destroy(p_subn->p_qos_policy);
        p_subn->p_qos_policy = NULL;
        res = 1;
        goto Exit;
    }

    /* add generated ULP match rules to the usual match rules */
    __process_ulp_match_rules();

    if (osm_qos_policy_validate(p_subn->p_qos_policy,p_qos_parser_osm_log))
    {
        OSM_LOG(p_qos_parser_osm_log, OSM_LOG_ERROR, "ERR AC04: "
                "Error(s) in QoS policy file (%s)\n",
                p_subn->opt.qos_policy_file);
        fprintf(stderr, "Error(s) in QoS policy file (%s)\n",
                p_subn->opt.qos_policy_file);
        osm_qos_policy_destroy(p_subn->p_qos_policy);
        p_subn->p_qos_policy = NULL;
        res = 1;
        goto Exit;
    }

  Exit:
    if (yyin)
    {
        yyrestart(yyin);
        fclose(yyin);
    }
    OSM_LOG_EXIT(p_qos_parser_osm_log);
    return res;
}

/***************************************************
 ***************************************************/

int yywrap()
{
    return(1);
}

/***************************************************
 ***************************************************/

static void yyerror(const char *format, ...)
{
    char s[256];
    va_list pvar;

    OSM_LOG_ENTER(p_qos_parser_osm_log);

    va_start(pvar, format);
    vsnprintf(s, sizeof(s), format, pvar);
    va_end(pvar);

    OSM_LOG(p_qos_parser_osm_log, OSM_LOG_ERROR, "ERR AC05: "
            "Syntax error (line %d:%d): %s\n",
            line_num, column_num, s);
    fprintf(stderr, "Error in QoS Policy File (line %d:%d): %s.\n",
            line_num, column_num, s);
    OSM_LOG_EXIT(p_qos_parser_osm_log);
}

/***************************************************
 ***************************************************/

static char * __parser_strip_white(char * str)
{
	char *p;

	while (isspace(*str))
		str++;
	if (!*str)
		return str;
	p = str + strlen(str) - 1;
	while (isspace(*p))
		*p-- = '\0';

	return str;
}

/***************************************************
 ***************************************************/

static void __parser_str2uint64(uint64_t * p_val, char * str)
{
   *p_val = strtoull(str, NULL, 0);
}

/***************************************************
 ***************************************************/

static void __parser_port_group_start()
{
    p_current_port_group = osm_qos_policy_port_group_create();
}

/***************************************************
 ***************************************************/

static int __parser_port_group_end()
{
    if(!p_current_port_group->name)
    {
        yyerror("port-group validation failed - no port group name specified");
        return -1;
    }

    cl_list_insert_tail(&p_qos_policy->port_groups,
                        p_current_port_group);
    p_current_port_group = NULL;
    return 0;
}

/***************************************************
 ***************************************************/

static void __parser_vlarb_scope_start()
{
    p_current_vlarb_scope = osm_qos_policy_vlarb_scope_create();
}

/***************************************************
 ***************************************************/

static int __parser_vlarb_scope_end()
{
    if ( !cl_list_count(&p_current_vlarb_scope->group_list) &&
         !cl_list_count(&p_current_vlarb_scope->across_list) )
    {
        yyerror("vlarb-scope validation failed - no port groups specified by 'group' or by 'across'");
        return -1;
    }

    cl_list_insert_tail(&p_qos_policy->vlarb_tables,
                        p_current_vlarb_scope);
    p_current_vlarb_scope = NULL;
    return 0;
}

/***************************************************
 ***************************************************/

static void __parser_sl2vl_scope_start()
{
    p_current_sl2vl_scope = osm_qos_policy_sl2vl_scope_create();
}

/***************************************************
 ***************************************************/

static int __parser_sl2vl_scope_end()
{
    if (!p_current_sl2vl_scope->sl2vl_table_set)
    {
        yyerror("sl2vl-scope validation failed - no sl2vl table specified");
        return -1;
    }
    if ( !cl_list_count(&p_current_sl2vl_scope->group_list) &&
         !cl_list_count(&p_current_sl2vl_scope->across_to_list) &&
         !cl_list_count(&p_current_sl2vl_scope->across_from_list) )
    {
        yyerror("sl2vl-scope validation failed - no port groups specified by 'group', 'across-to' or 'across-from'");
        return -1;
    }

    cl_list_insert_tail(&p_qos_policy->sl2vl_tables,
                        p_current_sl2vl_scope);
    p_current_sl2vl_scope = NULL;
    return 0;
}

/***************************************************
 ***************************************************/

static void __parser_qos_level_start()
{
    p_current_qos_level = osm_qos_policy_qos_level_create();
}

/***************************************************
 ***************************************************/

static int __parser_qos_level_end()
{
    if (!p_current_qos_level->sl_set)
    {
        yyerror("qos-level validation failed - no 'sl' specified");
        return -1;
    }
    if (!p_current_qos_level->name)
    {
        yyerror("qos-level validation failed - no 'name' specified");
        return -1;
    }

    cl_list_insert_tail(&p_qos_policy->qos_levels,
                        p_current_qos_level);
    p_current_qos_level = NULL;
    return 0;
}

/***************************************************
 ***************************************************/

static void __parser_match_rule_start()
{
    p_current_qos_match_rule = osm_qos_policy_match_rule_create();
}

/***************************************************
 ***************************************************/

static int __parser_match_rule_end()
{
    if (!p_current_qos_match_rule->qos_level_name)
    {
        yyerror("match-rule validation failed - no 'qos-level-name' specified");
        return -1;
    }

    cl_list_insert_tail(&p_qos_policy->qos_match_rules,
                        p_current_qos_match_rule);
    p_current_qos_match_rule = NULL;
    return 0;
}

/***************************************************
 ***************************************************/

static void __parser_ulp_match_rule_start()
{
    p_current_qos_match_rule = osm_qos_policy_match_rule_create();
}

/***************************************************
 ***************************************************/

static int __parser_ulp_match_rule_end()
{
    CL_ASSERT(p_current_qos_match_rule->p_qos_level);
    cl_list_insert_tail(&__ulp_match_rules,
                        p_current_qos_match_rule);
    p_current_qos_match_rule = NULL;
    return 0;
}

/***************************************************
 ***************************************************/

static void __parser_tmp_struct_init()
{
    tmp_parser_struct.str[0] = '\0';
    cl_list_construct(&tmp_parser_struct.str_list);
    cl_list_init(&tmp_parser_struct.str_list, 10);
    cl_list_construct(&tmp_parser_struct.num_list);
    cl_list_init(&tmp_parser_struct.num_list, 10);
    cl_list_construct(&tmp_parser_struct.num_pair_list);
    cl_list_init(&tmp_parser_struct.num_pair_list, 10);
}

/***************************************************
 ***************************************************/

/*
 * Do NOT free objects from the temp struct.
 * Either they are inserted into the parse tree data
 * structure, or they are already freed when copying
 * their values to the parse tree data structure.
 */
static void __parser_tmp_struct_reset()
{
    tmp_parser_struct.str[0] = '\0';
    cl_list_remove_all(&tmp_parser_struct.str_list);
    cl_list_remove_all(&tmp_parser_struct.num_list);
    cl_list_remove_all(&tmp_parser_struct.num_pair_list);
}

/***************************************************
 ***************************************************/

static void __parser_tmp_struct_destroy()
{
    __parser_tmp_struct_reset();
    cl_list_destroy(&tmp_parser_struct.str_list);
    cl_list_destroy(&tmp_parser_struct.num_list);
    cl_list_destroy(&tmp_parser_struct.num_pair_list);
}

/***************************************************
 ***************************************************/

#define __SIMPLE_QOS_LEVEL_NAME "SimpleQoSLevel_SL"
#define __SIMPLE_QOS_LEVEL_DEFAULT_NAME "SimpleQoSLevel_DEFAULT"

static void __setup_simple_qos_levels()
{
    uint8_t i;
    char tmp_buf[30];
    memset(osm_qos_policy_simple_qos_levels, 0,
           sizeof(osm_qos_policy_simple_qos_levels));
    for (i = 0; i < 16; i++)
    {
        osm_qos_policy_simple_qos_levels[i].sl = i;
        osm_qos_policy_simple_qos_levels[i].sl_set = TRUE;
        sprintf(tmp_buf, "%s%u", __SIMPLE_QOS_LEVEL_NAME, i);
        osm_qos_policy_simple_qos_levels[i].name = strdup(tmp_buf);
    }

    memset(&__default_simple_qos_level, 0,
           sizeof(__default_simple_qos_level));
    __default_simple_qos_level.name =
           strdup(__SIMPLE_QOS_LEVEL_DEFAULT_NAME);
}

/***************************************************
 ***************************************************/

static void __clear_simple_qos_levels()
{
    /*
     * Simple QoS levels are static.
     * What's left is to invalidate default simple QoS level.
     */
    __default_simple_qos_level.sl_set = FALSE;
}

/***************************************************
 ***************************************************/

static void __setup_ulp_match_rules()
{
    cl_list_construct(&__ulp_match_rules);
    cl_list_init(&__ulp_match_rules, 10);
}

/***************************************************
 ***************************************************/

static void __process_ulp_match_rules()
{
    cl_list_iterator_t list_iterator;
    osm_qos_match_rule_t *p_qos_match_rule = NULL;

    list_iterator = cl_list_head(&__ulp_match_rules);
    while (list_iterator != cl_list_end(&__ulp_match_rules))
    {
        p_qos_match_rule = (osm_qos_match_rule_t *) cl_list_obj(list_iterator);
        if (p_qos_match_rule)
            cl_list_insert_tail(&p_qos_policy->qos_match_rules,
                                p_qos_match_rule);
        list_iterator = cl_list_next(list_iterator);
    }
    cl_list_remove_all(&__ulp_match_rules);
}

/***************************************************
 ***************************************************/

static int __cmp_num_range(const void * p1, const void * p2)
{
    uint64_t * pair1 = *((uint64_t **)p1);
    uint64_t * pair2 = *((uint64_t **)p2);

    if (pair1[0] < pair2[0])
        return -1;
    if (pair1[0] > pair2[0])
        return 1;

    if (pair1[1] < pair2[1])
        return -1;
    if (pair1[1] > pair2[1])
        return 1;

    return 0;
}

/***************************************************
 ***************************************************/

static void __sort_reduce_rangearr(
    uint64_t  **   arr,
    unsigned       arr_len,
    uint64_t  ** * p_res_arr,
    unsigned     * p_res_arr_len )
{
    unsigned i = 0;
    unsigned j = 0;
    unsigned last_valid_ind = 0;
    unsigned valid_cnt = 0;
    uint64_t ** res_arr;
    boolean_t * is_valid_arr;

    *p_res_arr = NULL;
    *p_res_arr_len = 0;

    qsort(arr, arr_len, sizeof(uint64_t*), __cmp_num_range);

    is_valid_arr = (boolean_t *)malloc(arr_len * sizeof(boolean_t));
    is_valid_arr[last_valid_ind] = TRUE;
    valid_cnt++;
    for (i = 1; i < arr_len; i++)
    {
        if (arr[i][0] <= arr[last_valid_ind][1])
        {
            if (arr[i][1] > arr[last_valid_ind][1])
                arr[last_valid_ind][1] = arr[i][1];
            free(arr[i]);
            arr[i] = NULL;
            is_valid_arr[i] = FALSE;
        }
        else if ((arr[i][0] - 1) == arr[last_valid_ind][1])
        {
            arr[last_valid_ind][1] = arr[i][1];
            free(arr[i]);
            arr[i] = NULL;
            is_valid_arr[i] = FALSE;
        }
        else
        {
            is_valid_arr[i] = TRUE;
            last_valid_ind = i;
            valid_cnt++;
        }
    }

    res_arr = (uint64_t **)malloc(valid_cnt * sizeof(uint64_t *));
    for (i = 0; i < arr_len; i++)
    {
        if (is_valid_arr[i])
            res_arr[j++] = arr[i];
    }
    free(is_valid_arr);
    free(arr);

    *p_res_arr = res_arr;
    *p_res_arr_len = valid_cnt;
}

/***************************************************
 ***************************************************/

static void __pkey_rangelist2rangearr(
    cl_list_t    * p_list,
    uint64_t  ** * p_arr,
    unsigned     * p_arr_len)
{
    uint64_t   tmp_pkey;
    uint64_t * p_pkeys;
    cl_list_iterator_t list_iterator;

    list_iterator= cl_list_head(p_list);
    while( list_iterator != cl_list_end(p_list) )
    {
       p_pkeys = (uint64_t *)cl_list_obj(list_iterator);
       p_pkeys[0] &= 0x7fff;
       p_pkeys[1] &= 0x7fff;
       if (p_pkeys[0] > p_pkeys[1])
       {
           tmp_pkey = p_pkeys[1];
           p_pkeys[1] = p_pkeys[0];
           p_pkeys[0] = tmp_pkey;
       }
       list_iterator = cl_list_next(list_iterator);
    }

    __rangelist2rangearr(p_list, p_arr, p_arr_len);
}

/***************************************************
 ***************************************************/

static void __rangelist2rangearr(
    cl_list_t    * p_list,
    uint64_t  ** * p_arr,
    unsigned     * p_arr_len)
{
    cl_list_iterator_t list_iterator;
    unsigned len = cl_list_count(p_list);
    unsigned i = 0;
    uint64_t ** tmp_arr;
    uint64_t ** res_arr = NULL;
    unsigned res_arr_len = 0;

    tmp_arr = (uint64_t **)malloc(len * sizeof(uint64_t *));

    list_iterator = cl_list_head(p_list);
    while( list_iterator != cl_list_end(p_list) )
    {
       tmp_arr[i++] = (uint64_t *)cl_list_obj(list_iterator);
       list_iterator = cl_list_next(list_iterator);
    }
    cl_list_remove_all(p_list);

    __sort_reduce_rangearr( tmp_arr,
                            len,
                            &res_arr,
                            &res_arr_len );
    *p_arr = res_arr;
    *p_arr_len = res_arr_len;
}

/***************************************************
 ***************************************************/

static void __merge_rangearr(
    uint64_t  **   range_arr_1,
    unsigned       range_len_1,
    uint64_t  **   range_arr_2,
    unsigned       range_len_2,
    uint64_t  ** * p_arr,
    unsigned     * p_arr_len )
{
    unsigned i = 0;
    unsigned j = 0;
    unsigned len = range_len_1 + range_len_2;
    uint64_t ** tmp_arr;
    uint64_t ** res_arr = NULL;
    unsigned res_arr_len = 0;

    *p_arr = NULL;
    *p_arr_len = 0;

    tmp_arr = (uint64_t **)malloc(len * sizeof(uint64_t *));

    for (i = 0; i < range_len_1; i++)
       tmp_arr[j++] = range_arr_1[i];
    for (i = 0; i < range_len_2; i++)
       tmp_arr[j++] = range_arr_2[i];
    free(range_arr_1);
    free(range_arr_2);

    __sort_reduce_rangearr( tmp_arr,
                            len,
                            &res_arr,
                            &res_arr_len );
    *p_arr = res_arr;
    *p_arr_len = res_arr_len;
}

/***************************************************
 ***************************************************/

static void __parser_add_port_to_port_map(
    cl_qmap_t   * p_map,
    osm_physp_t * p_physp)
{
    if (cl_qmap_get(p_map, cl_ntoh64(osm_physp_get_port_guid(p_physp))) ==
        cl_qmap_end(p_map))
    {
        osm_qos_port_t * p_port = osm_qos_policy_port_create(p_physp);
        if (p_port)
            cl_qmap_insert(p_map,
                           cl_ntoh64(osm_physp_get_port_guid(p_physp)),
                           &p_port->map_item);
    }
}

/***************************************************
 ***************************************************/

static void __parser_add_guid_range_to_port_map(
    cl_qmap_t  * p_map,
    uint64_t  ** range_arr,
    unsigned     range_len)
{
    unsigned i;
    uint64_t guid_ho;
    osm_port_t * p_osm_port;

    if (!range_arr || !range_len)
        return;

    for (i = 0; i < range_len; i++) {
         for (guid_ho = range_arr[i][0]; guid_ho <= range_arr[i][1]; guid_ho++) {
             p_osm_port =
                osm_get_port_by_guid(p_qos_policy->p_subn, cl_hton64(guid_ho));
             if (p_osm_port)
                 __parser_add_port_to_port_map(p_map, p_osm_port->p_physp);
         }
         free(range_arr[i]);
    }
    free(range_arr);
}

/***************************************************
 ***************************************************/

static void __parser_add_pkey_range_to_port_map(
    cl_qmap_t  * p_map,
    uint64_t  ** range_arr,
    unsigned     range_len)
{
    unsigned i;
    uint64_t pkey_64;
    ib_net16_t pkey;
    osm_prtn_t * p_prtn;

    if (!range_arr || !range_len)
        return;

    for (i = 0; i < range_len; i++) {
         for (pkey_64 = range_arr[i][0]; pkey_64 <= range_arr[i][1]; pkey_64++) {
             pkey = cl_hton16((uint16_t)(pkey_64 & 0x7fff));
             p_prtn = (osm_prtn_t *)
                 cl_qmap_get(&p_qos_policy->p_subn->prtn_pkey_tbl, pkey);
             if (p_prtn != (osm_prtn_t *)cl_qmap_end(
                   &p_qos_policy->p_subn->prtn_pkey_tbl)) {
                 __parser_add_map_to_port_map(p_map, &p_prtn->part_guid_tbl);
                 __parser_add_map_to_port_map(p_map, &p_prtn->full_guid_tbl);
             }
         }
         free(range_arr[i]);
    }
    free(range_arr);
}

/***************************************************
 ***************************************************/

static void __parser_add_partition_list_to_port_map(
    cl_qmap_t  * p_map,
    cl_list_t  * p_list)
{
    cl_list_iterator_t    list_iterator;
    char                * tmp_str;
    osm_prtn_t          * p_prtn;

    /* extract all the ports from the partition
       to the port map of this port group */
    list_iterator = cl_list_head(p_list);
    while(list_iterator != cl_list_end(p_list)) {
        tmp_str = (char*)cl_list_obj(list_iterator);
        if (tmp_str) {
            p_prtn = osm_prtn_find_by_name(p_qos_policy->p_subn, tmp_str);
            if (p_prtn) {
                __parser_add_map_to_port_map(p_map, &p_prtn->part_guid_tbl);
                __parser_add_map_to_port_map(p_map, &p_prtn->full_guid_tbl);
            }
            free(tmp_str);
        }
        list_iterator = cl_list_next(list_iterator);
    }
    cl_list_remove_all(p_list);
}

/***************************************************
 ***************************************************/

static void __parser_add_map_to_port_map(
    cl_qmap_t * p_dmap,
    cl_map_t  * p_smap)
{
    cl_map_iterator_t map_iterator;
    osm_physp_t * p_physp;

    if (!p_dmap || !p_smap)
        return;

    map_iterator = cl_map_head(p_smap);
    while (map_iterator != cl_map_end(p_smap)) {
        p_physp = (osm_physp_t*)cl_map_obj(map_iterator);
        __parser_add_port_to_port_map(p_dmap, p_physp);
        map_iterator = cl_map_next(map_iterator);
    }
}

/***************************************************
 ***************************************************/

static int __validate_pkeys( uint64_t ** range_arr,
                             unsigned    range_len,
                             boolean_t   is_ipoib)
{
    unsigned i;
    uint64_t pkey_64;
    ib_net16_t pkey;
    osm_prtn_t * p_prtn;

    if (!range_arr || !range_len)
        return 0;

    for (i = 0; i < range_len; i++) {
        for (pkey_64 = range_arr[i][0]; pkey_64 <= range_arr[i][1]; pkey_64++) {
            pkey = cl_hton16((uint16_t)(pkey_64 & 0x7fff));
            p_prtn = (osm_prtn_t *)
                cl_qmap_get(&p_qos_policy->p_subn->prtn_pkey_tbl, pkey);

            if (p_prtn == (osm_prtn_t *)cl_qmap_end(
                  &p_qos_policy->p_subn->prtn_pkey_tbl))
                p_prtn = NULL;

            if (is_ipoib) {
                /*
                 * Be very strict for IPoIB partition:
                 *  - the partition for the pkey have to exist
                 *  - it has to have at least 2 full members
                 */
                if (!p_prtn) {
                    yyerror("IPoIB partition, pkey 0x%04X - "
                                       "partition doesn't exist",
                                       cl_ntoh16(pkey));
                    return 1;
                }
                else if (cl_map_count(&p_prtn->full_guid_tbl) < 2) {
                    yyerror("IPoIB partition, pkey 0x%04X - "
                                       "partition has less than two full members",
                                       cl_ntoh16(pkey));
                    return 1;
                }
            }
            else if (!p_prtn) {
                /*
                 * For non-IPoIB pkey we just want to check that
                 * the relevant partition exists.
                 * And even if it doesn't, don't exit - just print
                 * error message and continue.
                 */
                 OSM_LOG(p_qos_parser_osm_log, OSM_LOG_ERROR, "ERR AC02: "
			 "pkey 0x%04X - partition doesn't exist",
                         cl_ntoh16(pkey));
            }
        }
    }
    return 0;
}

/***************************************************
 ***************************************************/
