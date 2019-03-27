/*
 * Copyright (c) 2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2007 The Regents of the University of California.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
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

#ifndef _PERFMGR_EVENT_DB_H_
#define _PERFMGR_EVENT_DB_H_

#ifdef ENABLE_OSM_PERF_MGR

#include <stdio.h>
#include <time.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS

struct osm_perfmgr;
/****h* OpenSM/PerfMgr Event Database
* DESCRIPTION
*       Database interface to record subnet events
*
*       Implementations of this object _MUST_ be thread safe.
*
* AUTHOR
*	Ira Weiny, LLNL
*
*********/
typedef enum {
	PERFMGR_EVENT_DB_SUCCESS = 0,
	PERFMGR_EVENT_DB_FAIL,
	PERFMGR_EVENT_DB_NOMEM,
	PERFMGR_EVENT_DB_GUIDNOTFOUND,
	PERFMGR_EVENT_DB_PORTNOTFOUND,
	PERFMGR_EVENT_DB_NOT_IMPL
} perfmgr_db_err_t;

/** =========================================================================
 * Port error reading
 */
typedef struct {
	uint64_t symbol_err_cnt;
	uint64_t link_err_recover;
	uint64_t link_downed;
	uint64_t rcv_err;
	uint64_t rcv_rem_phys_err;
	uint64_t rcv_switch_relay_err;
	uint64_t xmit_discards;
	uint64_t xmit_constraint_err;
	uint64_t rcv_constraint_err;
	uint64_t link_integrity;
	uint64_t buffer_overrun;
	uint64_t vl15_dropped;
	uint64_t xmit_wait;
	time_t time;
} perfmgr_db_err_reading_t;

/** =========================================================================
 * Port data count reading
 */
typedef struct {
	uint64_t xmit_data;	/* can be used for std or extended */
	uint64_t rcv_data;	/* can be used for std or extended */
	uint64_t xmit_pkts;	/* can be used for std or extended */
	uint64_t rcv_pkts;	/* can be used for std or extended */
	uint64_t unicast_xmit_pkts;
	uint64_t unicast_rcv_pkts;
	uint64_t multicast_xmit_pkts;
	uint64_t multicast_rcv_pkts;
	time_t time;
} perfmgr_db_data_cnt_reading_t;

/** =========================================================================
 * Dump output options
 */
typedef enum {
	PERFMGR_EVENT_DB_DUMP_HR = 0,	/* Human readable */
	PERFMGR_EVENT_DB_DUMP_MR	/* Machine readable */
} perfmgr_db_dump_t;

/** =========================================================================
 * Port counter object.
 * Store all the port counters for a single port.
 */
typedef struct db_port {
	perfmgr_db_err_reading_t err_total;
	perfmgr_db_err_reading_t err_previous;
	perfmgr_db_data_cnt_reading_t dc_total;
	perfmgr_db_data_cnt_reading_t dc_previous;
	time_t last_reset;
	boolean_t valid;
} db_port_t;

/** =========================================================================
 * group port counters for ports into the nodes
 */
#define NODE_NAME_SIZE (IB_NODE_DESCRIPTION_SIZE + 1)
typedef struct db_node {
	cl_map_item_t map_item;	/* must be first */
	uint64_t node_guid;
	boolean_t active;       /* activly being monitored */
	boolean_t esp0;
	db_port_t *ports;
	uint8_t num_ports;
	char node_name[NODE_NAME_SIZE];
} db_node_t;

/** =========================================================================
 * all nodes in the subnet.
 */
typedef struct perfmgr_db {
	cl_qmap_t pc_data;	/* stores type (db_node_t *) */
	cl_plock_t lock;
	struct osm_perfmgr *perfmgr;
} perfmgr_db_t;

/**
 * functions
 */
perfmgr_db_t *perfmgr_db_construct(struct osm_perfmgr *perfmgr);
void perfmgr_db_destroy(perfmgr_db_t * db);

perfmgr_db_err_t perfmgr_db_create_entry(perfmgr_db_t * db, uint64_t guid,
					 boolean_t esp0, uint8_t num_ports,
					 char *node_name);
perfmgr_db_err_t perfmgr_db_delete_entry(perfmgr_db_t * db, uint64_t guid);
perfmgr_db_err_t perfmgr_db_delete_inactive(perfmgr_db_t * db, unsigned *cnt);

perfmgr_db_err_t perfmgr_db_update_name(perfmgr_db_t * db, uint64_t node_guid,
					char *name);

perfmgr_db_err_t perfmgr_db_add_err_reading(perfmgr_db_t * db, uint64_t guid,
					    uint8_t port,
					    perfmgr_db_err_reading_t * reading);
perfmgr_db_err_t perfmgr_db_get_prev_err(perfmgr_db_t * db, uint64_t guid,
					 uint8_t port,
					 perfmgr_db_err_reading_t * reading);
perfmgr_db_err_t perfmgr_db_clear_prev_err(perfmgr_db_t * db, uint64_t guid,
					   uint8_t port);

perfmgr_db_err_t perfmgr_db_add_dc_reading(perfmgr_db_t * db, uint64_t guid,
					   uint8_t port,
					   perfmgr_db_data_cnt_reading_t *
					   reading,
					   int ietf_sup);
perfmgr_db_err_t perfmgr_db_get_prev_dc(perfmgr_db_t * db, uint64_t guid,
					uint8_t port,
					perfmgr_db_data_cnt_reading_t *
					reading);
perfmgr_db_err_t perfmgr_db_clear_prev_dc(perfmgr_db_t * db, uint64_t guid,
					  uint8_t port);

perfmgr_db_err_t perfmgr_db_mark_active(perfmgr_db_t *db, uint64_t guid,
					boolean_t active);

void perfmgr_db_clear_counters(perfmgr_db_t * db);
perfmgr_db_err_t perfmgr_db_dump(perfmgr_db_t * db, char *file,
				 perfmgr_db_dump_t dump_type);
void perfmgr_db_print_all(perfmgr_db_t * db, FILE *fp, int err_only);
void perfmgr_db_print_by_name(perfmgr_db_t * db, char *nodename, FILE *fp,
			      char *port, int err_only);
void perfmgr_db_print_by_guid(perfmgr_db_t * db, uint64_t guid, FILE *fp,
			      char *port, int err_only);

/** =========================================================================
 * helper functions to fill in the various db objects from wire objects
 */

void perfmgr_db_fill_err_read(ib_port_counters_t * wire_read,
			      perfmgr_db_err_reading_t * reading,
			      boolean_t xmit_wait_sup);
void perfmgr_db_fill_data_cnt_read_pc(ib_port_counters_t * wire_read,
				      perfmgr_db_data_cnt_reading_t * reading);
void perfmgr_db_fill_data_cnt_read_pce(ib_port_counters_ext_t * wire_read,
				       perfmgr_db_data_cnt_reading_t * reading,
				       int ietf_sup);

END_C_DECLS

#endif				/* ENABLE_OSM_PERF_MGR */

#endif				/* _PERFMGR_PM_DB_H_ */
