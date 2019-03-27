/*
 * Copyright (c) 2007 The Regents of the University of California.
 * Copyright (c) 2007-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2009,2010 HNR Consulting. All rights reserved.
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

#ifndef _OSM_PERFMGR_H_
#define _OSM_PERFMGR_H_

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#ifdef ENABLE_OSM_PERF_MGR

#include <iba/ib_types.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_event.h>
#include <complib/cl_timer.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_log.h>
#include <opensm/osm_perfmgr_db.h>
#include <opensm/osm_sm.h>
#include <opensm/osm_base.h>
#include <opensm/osm_event_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif				/* __cplusplus */

/****h* OpenSM/PerfMgr
* NAME
*	PerfMgr
*
* DESCRIPTION
*       Performance manager thread which takes care of polling the fabric for
*       Port counters values.
*
*	The PerfMgr object is thread safe.
*
* AUTHOR
*	Ira Weiny, LLNL
*
*********/

#define OSM_PERFMGR_DEFAULT_SWEEP_TIME_S 180
#define OSM_PERFMGR_DEFAULT_DUMP_FILE "opensm_port_counters.log"
#define OSM_PERFMGR_DEFAULT_MAX_OUTSTANDING_QUERIES 500
#define OSM_PERFMGR_DEFAULT_XMIT_WAIT_THRESHOLD 0x0000FFFF

/****s* OpenSM: PerfMgr/osm_perfmgr_state_t */
typedef enum {
	PERFMGR_STATE_DISABLE,
	PERFMGR_STATE_ENABLED,
	PERFMGR_STATE_NO_DB
} osm_perfmgr_state_t;

/****s* OpenSM: PerfMgr/osm_perfmgr_sweep_state_t */
typedef enum {
	PERFMGR_SWEEP_SLEEP,
	PERFMGR_SWEEP_ACTIVE,
	PERFMGR_SWEEP_SUSPENDED,
	PERFMGR_SWEEP_POST_PROCESSING
} osm_perfmgr_sweep_state_t;

typedef struct monitored_port {
	uint16_t pkey_ix;
	ib_net16_t orig_lid;
	boolean_t redirection;
	boolean_t valid;
	/* Redirection fields from ClassPortInfo */
	ib_gid_t gid;
	ib_net16_t lid;
	ib_net16_t pkey;
	ib_net32_t qp;
	/* ClassPortInfo fields */
	boolean_t cpi_valid;
	ib_net16_t cap_mask;
	/* Remote end connected to */
	boolean_t remote_valid;
	uint64_t remote_guid;
	char *remote_name;
	uint8_t remote_port;
} monitored_port_t;

/* Node to store information about nodes being monitored */
typedef struct monitored_node {
	cl_map_item_t map_item;
	struct monitored_node *next;
	uint64_t guid;
	uint8_t node_type;
	boolean_t esp0;
	char *name;
	uint32_t num_ports;
	monitored_port_t port[1];
} monitored_node_t;

struct osm_opensm;

/****s* OpenSM: PerfMgr/osm_perfmgr_t
*  This object should be treated as opaque and should
*  be manipulated only through the provided functions.
*/
typedef struct osm_perfmgr {
	cl_timer_t sweep_timer;
	struct osm_opensm *osm;
	osm_subn_t *subn;
	osm_sm_t *sm;
	osm_log_t *log;
	osm_mad_pool_t *mad_pool;
	atomic32_t trans_id;
	osm_vendor_t *vendor;
	osm_bind_handle_t bind_handle;
	cl_disp_reg_handle_t pc_disp_h;
	osm_perfmgr_state_t state;
	osm_perfmgr_sweep_state_t sweep_state;
	cl_spinlock_t lock;
	uint16_t sweep_time_s;
	perfmgr_db_t *db;
	atomic32_t outstanding_queries;	/* this along with sig_query */
	cl_event_t sig_query;	/* will throttle our queries */
	uint32_t max_outstanding_queries;
	boolean_t ignore_cas;
	cl_qmap_t monitored_map;	/* map the nodes being tracked */
	monitored_node_t *remove_list;
	ib_net64_t port_guid;
	int16_t local_port;
	int rm_nodes;
	boolean_t query_cpi;
	boolean_t xmit_wait_log;
	uint32_t xmit_wait_threshold;
} osm_perfmgr_t;
/*
* FIELDS
*	subn
*	      Subnet object for this subnet.
*
*	log
*	      Pointer to the log object.
*
*	mad_pool
*	      Pointer to the MAD pool.
*
*	mad_ctrl
*	      Mad Controller
*********/

/****f* OpenSM: Creation Functions */
void osm_perfmgr_shutdown(osm_perfmgr_t * p_perfmgr);
void osm_perfmgr_destroy(osm_perfmgr_t * p_perfmgr);

/****f* OpenSM: Inline accessor functions */
inline static void osm_perfmgr_set_state(osm_perfmgr_t * p_perfmgr,
					 osm_perfmgr_state_t state)
{
	p_perfmgr->state = state;
	if (state == PERFMGR_STATE_ENABLED) {
		cl_timer_start(&p_perfmgr->sweep_timer, p_perfmgr->sweep_time_s * 1000);
	} else {
		cl_timer_stop(&p_perfmgr->sweep_timer);
	}
}

inline static osm_perfmgr_state_t osm_perfmgr_get_state(osm_perfmgr_t * perfmgr)
{
	return perfmgr->state;
}

inline static void osm_perfmgr_set_rm_nodes(osm_perfmgr_t *perfmgr,
					    int rm_nodes)
{
	perfmgr->rm_nodes = rm_nodes;
}

inline static int osm_perfmgr_get_rm_nodes(osm_perfmgr_t *perfmgr)
{
	return perfmgr->rm_nodes;
}

inline static void osm_perfmgr_set_query_cpi(osm_perfmgr_t *perfmgr,
					    int query_cpi)
{
	perfmgr->query_cpi = query_cpi;
}

inline static int osm_perfmgr_get_query_cpi(osm_perfmgr_t *perfmgr)
{
	return perfmgr->query_cpi;
}

inline static const char *osm_perfmgr_get_state_str(osm_perfmgr_t * p_perfmgr)
{
	switch (p_perfmgr->state) {
	case PERFMGR_STATE_DISABLE:
		return "Disabled";
		break;
	case PERFMGR_STATE_ENABLED:
		return "Enabled";
		break;
	case PERFMGR_STATE_NO_DB:
		return "No Database";
		break;
	}
	return "UNKNOWN";
}

inline static const char *osm_perfmgr_get_sweep_state_str(osm_perfmgr_t * perfmgr)
{
	switch (perfmgr->sweep_state) {
	case PERFMGR_SWEEP_SLEEP:
		return "Sleeping";
		break;
	case PERFMGR_SWEEP_ACTIVE:
		return "Active";
		break;
	case PERFMGR_SWEEP_SUSPENDED:
		return "Suspended";
		break;
	case PERFMGR_SWEEP_POST_PROCESSING:
		return "PostProcessing";
		break;
	}
	return "UNKNOWN";
}

inline static void osm_perfmgr_set_sweep_time_s(osm_perfmgr_t * p_perfmgr,
						uint16_t time_s)
{
	p_perfmgr->sweep_time_s = time_s;
	osm_sm_signal(p_perfmgr->sm, OSM_SIGNAL_PERFMGR_SWEEP);
}

inline static uint16_t osm_perfmgr_get_sweep_time_s(osm_perfmgr_t * p_perfmgr)
{
	return p_perfmgr->sweep_time_s;
}

inline static unsigned osm_perfmgr_delete_inactive(osm_perfmgr_t * pm)
{
	unsigned rc;
	perfmgr_db_delete_inactive(pm->db, &rc);
	return (rc);
}

void osm_perfmgr_clear_counters(osm_perfmgr_t * p_perfmgr);
void osm_perfmgr_dump_counters(osm_perfmgr_t * p_perfmgr,
			       perfmgr_db_dump_t dump_type);
void osm_perfmgr_print_counters(osm_perfmgr_t *pm, char *nodename, FILE *fp,
				char *port, int err_only);
void osm_perfmgr_update_nodename(osm_perfmgr_t *pm, uint64_t node_guid,
				char *nodename);

ib_api_status_t osm_perfmgr_bind(osm_perfmgr_t * p_perfmgr,
				 ib_net64_t port_guid);

void osm_perfmgr_process(osm_perfmgr_t * pm);

/****f* OpenSM: PerfMgr/osm_perfmgr_init */
ib_api_status_t osm_perfmgr_init(osm_perfmgr_t * perfmgr,
				 struct osm_opensm *osm,
				 const osm_subn_opt_t * p_opt);
/*
* PARAMETERS
*	perfmgr
*		[in] Pointer to an osm_perfmgr_t object to initialize.
*
*	osm
*		[in] Pointer to the OpenSM object.
*
*	p_opt
*		[in] Pointer to the subnet options structure.
*
* RETURN VALUES
*	IB_SUCCESS if the PerfMgr object was initialized successfully.
*********/

#ifdef __cplusplus
}
#endif				/* __cplusplus */

#endif				/* ENABLE_OSM_PERF_MGR */

#endif				/* _OSM_PERFMGR_H_ */
