/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
 * Copyright (c) 2009 Sun Microsystems, Inc. All rights reserved.
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
 *    Implementation of osm_vendor_t (for umad).
 * This object represents the OpenIB vendor layer.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#ifdef OSM_VENDOR_INTF_OPENIB

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#include <iba/ib_types.h>
#include <complib/cl_qlist.h>
#include <complib/cl_math.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_VENDOR_IBUMAD_C
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_helper.h>
#include <vendor/osm_vendor_api.h>

/****s* OpenSM: Vendor UMAD/osm_umad_bind_info_t
 * NAME
 *   osm_umad_bind_info_t
 *
 * DESCRIPTION
 *    Structure containing bind information.
 *
 * SYNOPSIS
 */
typedef struct _osm_umad_bind_info {
	osm_vendor_t *p_vend;
	void *client_context;
	osm_mad_pool_t *p_mad_pool;
	osm_vend_mad_recv_callback_t mad_recv_callback;
	osm_vend_mad_send_err_callback_t send_err_callback;
	ib_net64_t port_guid;
	int port_id;
	int agent_id;
	int agent_id1;		/* SMI requires two agents */
	int timeout;
	int max_retries;
} osm_umad_bind_info_t;

typedef struct _umad_receiver {
	pthread_t tid;
	osm_vendor_t *p_vend;
	osm_log_t *p_log;
} umad_receiver_t;

static void osm_vendor_close_port(osm_vendor_t * const p_vend);

static void log_send_error(osm_vendor_t * const p_vend, osm_madw_t *p_madw)
{
	if (p_madw->p_mad->mgmt_class != IB_MCLASS_SUBN_DIR) {
		/* LID routed */
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5410: "
			"Send completed with error (%s) -- dropping\n"
			"\t\t\tClass 0x%x, Method 0x%X, Attr 0x%X, "
			"TID 0x%" PRIx64 ", LID %u\n",
			ib_get_err_str(p_madw->status),
			p_madw->p_mad->mgmt_class, p_madw->p_mad->method,
			cl_ntoh16(p_madw->p_mad->attr_id),
			cl_ntoh64(p_madw->p_mad->trans_id),
			cl_ntoh16(p_madw->mad_addr.dest_lid));
	} else {
		ib_smp_t *p_smp;

		/* Direct routed SMP */
		p_smp = osm_madw_get_smp_ptr(p_madw);
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5411: "
			"DR SMP Send completed with error (%s) -- dropping\n"
			"\t\t\tMethod 0x%X, Attr 0x%X, TID 0x%" PRIx64 "\n",
			ib_get_err_str(p_madw->status),
			p_madw->p_mad->method,
			cl_ntoh16(p_madw->p_mad->attr_id),
			cl_ntoh64(p_madw->p_mad->trans_id));
		osm_dump_smp_dr_path(p_vend->p_log, p_smp, OSM_LOG_ERROR);
	}
}

static void clear_madw(osm_vendor_t * p_vend)
{
	umad_match_t *m, *e, *old_m;
	ib_net64_t old_tid;
	uint8_t old_mgmt_class;

	OSM_LOG_ENTER(p_vend->p_log);
	pthread_mutex_lock(&p_vend->match_tbl_mutex);
	for (m = p_vend->mtbl.tbl, e = m + p_vend->mtbl.max; m < e; m++) {
		if (m->tid) {
			old_m = m;
			old_tid = m->tid;
			old_mgmt_class = m->mgmt_class;
			m->tid = 0;
			osm_mad_pool_put(((osm_umad_bind_info_t
					   *) ((osm_madw_t *) m->v)->h_bind)->
					 p_mad_pool, m->v);
			pthread_mutex_unlock(&p_vend->match_tbl_mutex);
			OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5401: "
				"evicting entry %p (tid was 0x%" PRIx64
				" mgmt class 0x%x)\n",
				old_m, cl_ntoh64(old_tid), old_mgmt_class);
			goto Exit;
		}
	}
	pthread_mutex_unlock(&p_vend->match_tbl_mutex);

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
}

static osm_madw_t *get_madw(osm_vendor_t * p_vend, ib_net64_t * tid,
			    uint8_t mgmt_class)
{
	umad_match_t *m, *e;
	ib_net64_t mtid = (*tid & CL_HTON64(0x00000000ffffffffULL));
	osm_madw_t *res;

	/*
	 * Since mtid == 0 is the empty key, we should not
	 * waste time looking for it
	 */
	if (mtid == 0 || mgmt_class == 0)
		return 0;

	pthread_mutex_lock(&p_vend->match_tbl_mutex);
	for (m = p_vend->mtbl.tbl, e = m + p_vend->mtbl.max; m < e; m++) {
		if (m->tid == mtid && m->mgmt_class == mgmt_class) {
			m->tid = 0;
			m->mgmt_class = 0;
			*tid = mtid;
			res = m->v;
			pthread_mutex_unlock(&p_vend->match_tbl_mutex);
			return res;
		}
	}

	pthread_mutex_unlock(&p_vend->match_tbl_mutex);
	return 0;
}

/*
 * If match table full, evict LRU (least recently used) transaction.
 * Maintain 2 LRUs: one for SMPs, and one for others (GS).
 * Evict LRU GS transaction if one is available and only evict LRU SMP
 * transaction if no other choice.
 */
static void
put_madw(osm_vendor_t * p_vend, osm_madw_t * p_madw, ib_net64_t tid,
	 uint8_t mgmt_class)
{
	umad_match_t *m, *e, *old_lru, *lru = 0, *lru_smp = 0;
	osm_madw_t *p_req_madw;
	osm_umad_bind_info_t *p_bind;
	ib_net64_t old_tid;
	uint32_t oldest = ~0, oldest_smp = ~0;
	uint8_t old_mgmt_class;

	pthread_mutex_lock(&p_vend->match_tbl_mutex);
	for (m = p_vend->mtbl.tbl, e = m + p_vend->mtbl.max; m < e; m++) {
		if (m->tid == 0 && m->mgmt_class == 0) {
			m->tid = tid;
			m->mgmt_class = mgmt_class;
			m->v = p_madw;
			m->version =
			    cl_atomic_inc((atomic32_t *) & p_vend->mtbl.
					  last_version);
			pthread_mutex_unlock(&p_vend->match_tbl_mutex);
			return;
		}
		if (m->mgmt_class == IB_MCLASS_SUBN_DIR ||
		    m->mgmt_class == IB_MCLASS_SUBN_LID) {
			if (oldest_smp >= m->version) {
				oldest_smp = m->version;
				lru_smp = m;
			}
		} else {
			if (oldest >= m->version) {
				oldest = m->version;
				lru = m;
			}
		}
	}

	if (oldest != ~0) {
		old_lru = lru;
		old_tid = lru->tid;
		old_mgmt_class = lru->mgmt_class;
	} else {
		CL_ASSERT(oldest_smp != ~0);
		old_lru = lru_smp;
		old_tid = lru_smp->tid;
		old_mgmt_class = lru_smp->mgmt_class;
	}
	p_req_madw = old_lru->v;
	p_bind = p_req_madw->h_bind;
	p_req_madw->status = IB_CANCELED;
	log_send_error(p_vend, p_req_madw);
	pthread_mutex_lock(&p_vend->cb_mutex);
	(*p_bind->send_err_callback) (p_bind->client_context, p_req_madw);
	pthread_mutex_unlock(&p_vend->cb_mutex);
	if (mgmt_class == IB_MCLASS_SUBN_DIR ||
	    mgmt_class == IB_MCLASS_SUBN_LID) {
		lru_smp->tid = tid;
		lru_smp->mgmt_class = mgmt_class;
		lru_smp->v = p_madw;
		lru_smp->version =
		    cl_atomic_inc((atomic32_t *) & p_vend->mtbl.last_version);
	} else {
		lru->tid = tid;
		lru->mgmt_class = mgmt_class;
		lru->v = p_madw;
		lru->version =
		    cl_atomic_inc((atomic32_t *) & p_vend->mtbl.last_version);
	}
	pthread_mutex_unlock(&p_vend->match_tbl_mutex);
	OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5402: "
		"evicting entry %p (tid was 0x%" PRIx64
		" mgmt class 0x%x)\n", old_lru,
		cl_ntoh64(old_tid), old_mgmt_class);
}

static void
ib_mad_addr_conv(ib_user_mad_t * umad, osm_mad_addr_t * osm_mad_addr,
		 int is_smi)
{
	ib_mad_addr_t *ib_mad_addr = umad_get_mad_addr(umad);

	memset(osm_mad_addr, 0, sizeof(osm_mad_addr_t));
	osm_mad_addr->dest_lid = ib_mad_addr->lid;
	osm_mad_addr->path_bits = ib_mad_addr->path_bits;

	if (is_smi) {
		osm_mad_addr->addr_type.smi.source_lid = osm_mad_addr->dest_lid;
		osm_mad_addr->addr_type.smi.port_num = 255;	/* not used */
		return;
	}

	osm_mad_addr->addr_type.gsi.remote_qp = ib_mad_addr->qpn;
	osm_mad_addr->addr_type.gsi.remote_qkey = ib_mad_addr->qkey;
	osm_mad_addr->addr_type.gsi.pkey_ix = umad_get_pkey(umad);
	osm_mad_addr->addr_type.gsi.service_level = ib_mad_addr->sl;
	if (ib_mad_addr->grh_present) {
		osm_mad_addr->addr_type.gsi.global_route = 1;
		osm_mad_addr->addr_type.gsi.grh_info.hop_limit = ib_mad_addr->hop_limit;
		osm_mad_addr->addr_type.gsi.grh_info.ver_class_flow =
			ib_grh_set_ver_class_flow(6,	/* GRH version */
						  ib_mad_addr->traffic_class,
						  ib_mad_addr->flow_label);
		memcpy(&osm_mad_addr->addr_type.gsi.grh_info.dest_gid,
		       &ib_mad_addr->gid, 16);
	}
}

static void *swap_mad_bufs(osm_madw_t * p_madw, void *umad)
{
	void *old;

	old = p_madw->vend_wrap.umad;
	p_madw->vend_wrap.umad = umad;
	p_madw->p_mad = umad_get_mad(umad);

	return old;
}

static void unlock_mutex(void *arg)
{
	pthread_mutex_unlock(arg);
}

static void *umad_receiver(void *p_ptr)
{
	umad_receiver_t *const p_ur = (umad_receiver_t *) p_ptr;
	osm_vendor_t *p_vend = p_ur->p_vend;
	osm_umad_bind_info_t *p_bind;
	osm_mad_addr_t osm_addr;
	osm_madw_t *p_madw, *p_req_madw;
	ib_mad_t *p_mad, *p_req_mad;
	void *umad = 0;
	int mad_agent, length;

	OSM_LOG_ENTER(p_ur->p_log);

	for (;;) {
		if (!umad &&
		    !(umad = umad_alloc(1, umad_size() + MAD_BLOCK_SIZE))) {
			OSM_LOG(p_ur->p_log, OSM_LOG_ERROR, "ERR 5403: "
				"can't alloc MAD sized umad\n");
			break;
		}

		length = MAD_BLOCK_SIZE;
		if ((mad_agent = umad_recv(p_vend->umad_port_id, umad,
					   &length, -1)) < 0) {
			if (length <= MAD_BLOCK_SIZE) {
				OSM_LOG(p_ur->p_log, OSM_LOG_ERROR, "ERR 5404: "
					"recv error on MAD sized umad (%m)\n");
				continue;
			} else {
				umad_free(umad);
				/* Need a larger buffer for RMPP */
				umad = umad_alloc(1, umad_size() + length);
				if (!umad) {
					OSM_LOG(p_ur->p_log, OSM_LOG_ERROR,
						"ERR 5405: "
						"can't alloc umad length %d\n",
						length);
					continue;
				}

				if ((mad_agent = umad_recv(p_vend->umad_port_id,
							   umad, &length,
							   -1)) < 0) {
					OSM_LOG(p_ur->p_log, OSM_LOG_ERROR,
						"ERR 5406: "
						"recv error on umad length %d (%m)\n",
						length);
					continue;
				}
			}
		}

		if (mad_agent >= OSM_UMAD_MAX_AGENTS ||
		    !(p_bind = p_vend->agents[mad_agent])) {
			OSM_LOG(p_ur->p_log, OSM_LOG_ERROR, "ERR 5407: "
				"invalid mad agent %d - dropping\n", mad_agent);
			continue;
		}

		p_mad = (ib_mad_t *) umad_get_mad(umad);

		ib_mad_addr_conv(umad, &osm_addr,
				 p_mad->mgmt_class == IB_MCLASS_SUBN_LID ||
				 p_mad->mgmt_class == IB_MCLASS_SUBN_DIR);

		if (!(p_madw = osm_mad_pool_get(p_bind->p_mad_pool,
						(osm_bind_handle_t) p_bind,
						MAX(length, MAD_BLOCK_SIZE),
						&osm_addr))) {
			OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5408: "
				"request for a new madw failed -- dropping packet\n");
			continue;
		}

		/* Need to fix up MAD size if short RMPP packet */
		if (length < MAD_BLOCK_SIZE)
			p_madw->mad_size = length;

		/*
		 * Avoid copying by swapping mad buf pointers.
		 * Do not use umad after this line of code.
		 */
		umad = swap_mad_bufs(p_madw, umad);

		/* if status != 0 then we are handling recv timeout on send */
		if (umad_status(p_madw->vend_wrap.umad)) {
			if (!(p_req_madw = get_madw(p_vend, &p_mad->trans_id,
						    p_mad->mgmt_class))) {
				OSM_LOG(p_vend->p_log, OSM_LOG_ERROR,
					"ERR 5412: "
					"Failed to obtain request madw for timed out MAD"
					" (class=0x%X method=0x%X attr=0x%X tid=0x%"PRIx64") -- dropping\n",
					p_mad->mgmt_class, p_mad->method,
					cl_ntoh16(p_mad->attr_id),
					cl_ntoh64(p_mad->trans_id));
			} else {
				p_req_madw->status = IB_TIMEOUT;
				log_send_error(p_vend, p_req_madw);
				/* cb frees req_madw */
				pthread_mutex_lock(&p_vend->cb_mutex);
				pthread_cleanup_push(unlock_mutex,
						     &p_vend->cb_mutex);
				(*p_bind->send_err_callback) (p_bind->
							      client_context,
							      p_req_madw);
				pthread_cleanup_pop(1);
			}

			osm_mad_pool_put(p_bind->p_mad_pool, p_madw);
			continue;
		}

		p_req_madw = 0;
		if (ib_mad_is_response(p_mad)) {
			p_req_madw = get_madw(p_vend, &p_mad->trans_id,
					      p_mad->mgmt_class);
			if (PF(!p_req_madw)) {
				OSM_LOG(p_vend->p_log, OSM_LOG_ERROR,
					"ERR 5413: Failed to obtain request "
					"madw for received MAD "
					"(class=0x%X method=0x%X attr=0x%X "
					"tid=0x%"PRIx64") -- dropping\n",
					p_mad->mgmt_class, p_mad->method,
					cl_ntoh16(p_mad->attr_id),
					cl_ntoh64(p_mad->trans_id));
				osm_mad_pool_put(p_bind->p_mad_pool, p_madw);
				continue;
			}

			/*
			 * Check that request MAD was really a request,
			 * and make sure that attribute ID, attribute
			 * modifier and transaction ID are the same in
			 * request and response.
			 *
			 * Exception for o15-0.2-1.11:
			 * SA response to a SubnAdmGetMulti() containing a
			 * MultiPathRecord shall have PathRecord attribute ID.
			 */
			p_req_mad = osm_madw_get_mad_ptr(p_req_madw);
			if (PF(ib_mad_is_response(p_req_mad) ||
			       (p_mad->attr_id != p_req_mad->attr_id &&
                                !(p_mad->mgmt_class == IB_MCLASS_SUBN_ADM &&
                                  p_req_mad->attr_id ==
					IB_MAD_ATTR_MULTIPATH_RECORD &&
                                  p_mad->attr_id == IB_MAD_ATTR_PATH_RECORD)) ||
			       p_mad->attr_mod != p_req_mad->attr_mod ||
			       p_mad->trans_id != p_req_mad->trans_id)) {
				OSM_LOG(p_vend->p_log, OSM_LOG_ERROR,
					"ERR 541A: "
					"Response MAD validation failed "
					"(request attr=0x%X modif=0x%X "
					"tid=0x%"PRIx64", "
					"response attr=0x%X modif=0x%X "
					"tid=0x%"PRIx64") -- dropping\n",
					cl_ntoh16(p_req_mad->attr_id),
					cl_ntoh32(p_req_mad->attr_mod),
					cl_ntoh64(p_req_mad->trans_id),
					cl_ntoh16(p_mad->attr_id),
					cl_ntoh32(p_mad->attr_mod),
					cl_ntoh64(p_mad->trans_id));
				osm_mad_pool_put(p_bind->p_mad_pool, p_madw);
				continue;
			}
		}

#ifndef VENDOR_RMPP_SUPPORT
		if ((p_mad->mgmt_class != IB_MCLASS_SUBN_DIR) &&
		    (p_mad->mgmt_class != IB_MCLASS_SUBN_LID) &&
		    (ib_rmpp_is_flag_set((ib_rmpp_mad_t *) p_mad,
					 IB_RMPP_FLAG_ACTIVE))) {
			OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5414: "
				"class 0x%x method 0x%x RMPP version %d type "
				"%d flags 0x%x received -- dropping\n",
				p_mad->mgmt_class, p_mad->method,
				((ib_rmpp_mad_t *) p_mad)->rmpp_version,
				((ib_rmpp_mad_t *) p_mad)->rmpp_type,
				((ib_rmpp_mad_t *) p_mad)->rmpp_flags);
			osm_mad_pool_put(p_bind->p_mad_pool, p_madw);
			continue;
		}
#endif

		/* call the CB */
		pthread_mutex_lock(&p_vend->cb_mutex);
		pthread_cleanup_push(unlock_mutex, &p_vend->cb_mutex);
		(*p_bind->mad_recv_callback) (p_madw, p_bind->client_context,
					      p_req_madw);
		pthread_cleanup_pop(1);
	}

	OSM_LOG_EXIT(p_vend->p_log);
	return NULL;
}

static int umad_receiver_start(osm_vendor_t * p_vend)
{
	umad_receiver_t *p_ur = p_vend->receiver;

	p_ur->p_vend = p_vend;
	p_ur->p_log = p_vend->p_log;

	if (pthread_create(&p_ur->tid, NULL, umad_receiver, p_ur) != 0)
		return -1;

	return 0;
}

static void umad_receiver_stop(umad_receiver_t * p_ur)
{
	pthread_cancel(p_ur->tid);
	pthread_join(p_ur->tid, NULL);
	p_ur->tid = 0;
	p_ur->p_vend = NULL;
	p_ur->p_log = NULL;
}

ib_api_status_t
osm_vendor_init(IN osm_vendor_t * const p_vend,
		IN osm_log_t * const p_log, IN const uint32_t timeout)
{
	char *max = NULL;
	int r, n_cas;

	OSM_LOG_ENTER(p_log);

	p_vend->p_log = p_log;
	p_vend->timeout = timeout;
	p_vend->max_retries = OSM_DEFAULT_RETRY_COUNT;
	pthread_mutex_init(&p_vend->cb_mutex, NULL);
	pthread_mutex_init(&p_vend->match_tbl_mutex, NULL);
	p_vend->umad_port_id = -1;
	p_vend->issmfd = -1;

	/*
	 * Open our instance of UMAD.
	 */
	if ((r = umad_init()) < 0) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR,
			"ERR 5415: Error opening UMAD\n");
	}

	if ((n_cas = umad_get_cas_names(p_vend->ca_names,
					OSM_UMAD_MAX_CAS)) < 0) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR,
			"ERR 5416: umad_get_cas_names failed\n");
		r = n_cas;
		goto Exit;
	}

	p_vend->ca_count = n_cas;
	p_vend->mtbl.max = DEFAULT_OSM_UMAD_MAX_PENDING;

	if ((max = getenv("OSM_UMAD_MAX_PENDING")) != NULL) {
		int tmp = strtol(max, NULL, 0);
		if (tmp > 0)
			p_vend->mtbl.max = tmp;
		else
			OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "Error:"
				"OSM_UMAD_MAX_PENDING=%d is invalid\n",
				tmp);
	}

	OSM_LOG(p_vend->p_log, OSM_LOG_INFO, "%d pending umads specified\n",
		p_vend->mtbl.max);

	p_vend->mtbl.tbl = calloc(p_vend->mtbl.max, sizeof(*(p_vend->mtbl.tbl)));
	if (!p_vend->mtbl.tbl) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "Error:"
			"failed to allocate vendor match table\n");
		r = IB_INSUFFICIENT_MEMORY;
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(p_log);
	return (r);
}

osm_vendor_t *osm_vendor_new(IN osm_log_t * const p_log,
			     IN const uint32_t timeout)
{
	osm_vendor_t *p_vend = NULL;

	OSM_LOG_ENTER(p_log);

	if (!timeout) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 5433: "
			"transaction timeout cannot be 0\n");
		goto Exit;
	}

	p_vend = malloc(sizeof(*p_vend));
	if (p_vend == NULL) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 5417: "
			"Unable to allocate vendor object\n");
		goto Exit;
	}

	memset(p_vend, 0, sizeof(*p_vend));

	if (osm_vendor_init(p_vend, p_log, timeout) != IB_SUCCESS) {
		free(p_vend);
		p_vend = NULL;
	}

Exit:
	OSM_LOG_EXIT(p_log);
	return (p_vend);
}

void osm_vendor_delete(IN osm_vendor_t ** const pp_vend)
{
	osm_vendor_close_port(*pp_vend);

	clear_madw(*pp_vend);
	/* make sure all ports are closed */
	umad_done();

	pthread_mutex_destroy(&(*pp_vend)->cb_mutex);
	pthread_mutex_destroy(&(*pp_vend)->match_tbl_mutex);
	free((*pp_vend)->mtbl.tbl);
	free(*pp_vend);
	*pp_vend = NULL;
}

ib_api_status_t
osm_vendor_get_all_port_attr(IN osm_vendor_t * const p_vend,
			     IN ib_port_attr_t * const p_attr_array,
			     IN uint32_t * const p_num_ports)
{
	umad_ca_t ca;
	ib_port_attr_t *attr = p_attr_array;
	unsigned done = 0;
	int r = 0, i, j, k;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vend && p_num_ports);

	if (!*p_num_ports) {
		r = IB_INVALID_PARAMETER;
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5418: "
			"Ports in should be > 0\n");
		goto Exit;
	}

	if (!p_attr_array) {
		r = IB_INSUFFICIENT_MEMORY;
		*p_num_ports = 0;
		goto Exit;
	}

	for (i = 0; i < p_vend->ca_count && !done; i++) {
		/* For each CA, retrieve the port attributes */
		if (umad_get_ca(p_vend->ca_names[i], &ca) == 0) {
			if (ca.node_type < 1 || ca.node_type > 3)
				continue;
			for (j = 0; j <= ca.numports; j++) {
				if (!ca.ports[j])
					continue;
				attr->port_guid = ca.ports[j]->port_guid;
				attr->lid = ca.ports[j]->base_lid;
				attr->port_num = ca.ports[j]->portnum;
				attr->sm_lid = ca.ports[j]->sm_lid;
				attr->sm_sl = ca.ports[j]->sm_sl;
				attr->link_state = ca.ports[j]->state;
				if (attr->num_pkeys && attr->p_pkey_table) {
					if (attr->num_pkeys > ca.ports[j]->pkeys_size)
						attr->num_pkeys = ca.ports[j]->pkeys_size;
					for (k = 0; k < attr->num_pkeys; k++)
						attr->p_pkey_table[k] =
							cl_hton16(ca.ports[j]->pkeys[k]);
				}
				attr->num_pkeys = ca.ports[j]->pkeys_size;
				if (attr->num_gids && attr->p_gid_table) {
					attr->p_gid_table[0].unicast.prefix = cl_hton64(ca.ports[j]->gid_prefix);
					attr->p_gid_table[0].unicast.interface_id = cl_hton64(ca.ports[j]->port_guid);
					attr->num_gids = 1;
				}
				attr++;
				if (attr - p_attr_array > *p_num_ports) {
					done = 1;
					break;
				}
			}
			umad_release_ca(&ca);
		}
	}

	*p_num_ports = attr - p_attr_array;

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return r;
}

static int
osm_vendor_open_port(IN osm_vendor_t * const p_vend,
		     IN const ib_net64_t port_guid)
{
	ib_net64_t portguids[OSM_UMAD_MAX_PORTS_PER_CA + 1];
	umad_ca_t umad_ca;
	int i = 0, umad_port_id = -1;
	char *name;
	int ca, r;

	CL_ASSERT(p_vend);

	OSM_LOG_ENTER(p_vend->p_log);

	if (p_vend->umad_port_id >= 0) {
		umad_port_id = p_vend->umad_port_id;
		goto Exit;
	}

	if (!port_guid) {
		name = NULL;
		i = 0;
		goto _found;
	}

	for (ca = 0; ca < p_vend->ca_count; ca++) {
		if ((r = umad_get_ca_portguids(p_vend->ca_names[ca], portguids,
					       OSM_UMAD_MAX_PORTS_PER_CA + 1)) < 0) {
#ifdef __WIN__
			OSM_LOG(p_vend->p_log, OSM_LOG_VERBOSE,
#else
			OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5421: "
#endif
				"Unable to get CA %s port guids (%s)\n",
				p_vend->ca_names[ca], strerror(r));
			continue;
		}
		for (i = 0; i < r; i++)
			if (port_guid == portguids[i]) {
				name = p_vend->ca_names[ca];
				goto _found;
			}
	}

	/*
	 * No local CA owns this guid!
	 */
	OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5422: "
		"Unable to find requested CA guid 0x%" PRIx64 "\n",
		cl_ntoh64(port_guid));
	goto Exit;

_found:
	/* Validate that node is an IB node type (not iWARP) */
	if (umad_get_ca(name, &umad_ca) < 0) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 542A: "
			"umad_get_ca() failed\n");
		goto Exit;
	}

	if (umad_ca.node_type < 1 || umad_ca.node_type > 3) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 542D: "
			"Type %d of node \'%s\' is not an IB node type\n",
			umad_ca.node_type, umad_ca.ca_name);
		fprintf(stderr,
			"Type %d of node \'%s\' is not an IB node type\n",
			umad_ca.node_type, umad_ca.ca_name);
		umad_release_ca(&umad_ca);
		goto Exit;
	}
	umad_release_ca(&umad_ca);

	/* Port found, try to open it */
	if (umad_get_port(name, i, &p_vend->umad_port) < 0) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 542B: "
			"umad_get_port() failed\n");
		goto Exit;
	}

	if ((umad_port_id = umad_open_port(p_vend->umad_port.ca_name,
					   p_vend->umad_port.portnum)) < 0) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 542C: "
			"umad_open_port() failed\n");
		goto Exit;
	}

	p_vend->umad_port_id = umad_port_id;

	/* start receiver thread */
	if (!(p_vend->receiver = calloc(1, sizeof(umad_receiver_t)))) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5423: "
			"Unable to alloc receiver struct\n");
		umad_close_port(umad_port_id);
		umad_release_port(&p_vend->umad_port);
		p_vend->umad_port.port_guid = 0;
		p_vend->umad_port_id = umad_port_id = -1;
		goto Exit;
	}
	if (umad_receiver_start(p_vend) != 0) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5420: "
			"umad_receiver_init failed\n");
		umad_close_port(umad_port_id);
		umad_release_port(&p_vend->umad_port);
		p_vend->umad_port.port_guid = 0;
		p_vend->umad_port_id = umad_port_id = -1;
	}

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return umad_port_id;
}

static void osm_vendor_close_port(osm_vendor_t * const p_vend)
{
	umad_receiver_t *p_ur;
	int i;

	p_ur = p_vend->receiver;
	p_vend->receiver = NULL;
	if (p_ur) {
		umad_receiver_stop(p_ur);
		free(p_ur);
	}

	if (p_vend->umad_port_id >= 0) {
		for (i = 0; i < OSM_UMAD_MAX_AGENTS; i++)
			if (p_vend->agents[i])
				umad_unregister(p_vend->umad_port_id, i);
		umad_close_port(p_vend->umad_port_id);
		umad_release_port(&p_vend->umad_port);
		p_vend->umad_port.port_guid = 0;
		p_vend->umad_port_id = -1;
	}
}

static int set_bit(int nr, void *method_mask)
{
	long mask, *addr = method_mask;
	int retval;

	addr += nr / (8 * sizeof(long));
	mask = 1L << (nr % (8 * sizeof(long)));
	retval = (mask & *addr) != 0;
	*addr |= mask;
	return retval;
}

osm_bind_handle_t
osm_vendor_bind(IN osm_vendor_t * const p_vend,
		IN osm_bind_info_t * const p_user_bind,
		IN osm_mad_pool_t * const p_mad_pool,
		IN osm_vend_mad_recv_callback_t mad_recv_callback,
		IN osm_vend_mad_send_err_callback_t send_err_callback,
		IN void *context)
{
	ib_net64_t port_guid;
	osm_umad_bind_info_t *p_bind = 0;
	long method_mask[16 / sizeof(long)];
	int umad_port_id;
	uint8_t rmpp_version;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_user_bind);
	CL_ASSERT(p_mad_pool);
	CL_ASSERT(mad_recv_callback);
	CL_ASSERT(send_err_callback);

	port_guid = p_user_bind->port_guid;

	OSM_LOG(p_vend->p_log, OSM_LOG_INFO,
		"Mgmt class 0x%02x binding to port GUID 0x%" PRIx64 "\n",
		p_user_bind->mad_class, cl_ntoh64(port_guid));

	if ((umad_port_id = osm_vendor_open_port(p_vend, port_guid)) < 0) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5424: "
			"Unable to open port 0x%" PRIx64 "\n",
			cl_ntoh64(port_guid));
		goto Exit;
	}

	if (umad_get_issm_path(p_vend->umad_port.ca_name,
			       p_vend->umad_port.portnum,
			       p_vend->issm_path,
			       sizeof(p_vend->issm_path)) < 0) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 542E: "
			"Cannot resolve issm path for port %s:%u\n",
			p_vend->umad_port.ca_name, p_vend->umad_port.portnum);
		goto Exit;
	}

	if (!(p_bind = malloc(sizeof(*p_bind)))) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5425: "
			"Unable to allocate internal bind object\n");
		goto Exit;
	}

	memset(p_bind, 0, sizeof(*p_bind));
	p_bind->p_vend = p_vend;
	p_bind->port_id = umad_port_id;
	p_bind->client_context = context;
	p_bind->mad_recv_callback = mad_recv_callback;
	p_bind->send_err_callback = send_err_callback;
	p_bind->p_mad_pool = p_mad_pool;
	p_bind->port_guid = port_guid;
	p_bind->timeout = p_user_bind->timeout ? p_user_bind->timeout :
			  p_vend->timeout;
	p_bind->max_retries = p_user_bind->retries ? p_user_bind->retries :
			      p_vend->max_retries;

	memset(method_mask, 0, sizeof method_mask);
	if (p_user_bind->is_responder) {
		set_bit(IB_MAD_METHOD_GET, &method_mask);
		set_bit(IB_MAD_METHOD_SET, &method_mask);
		if (p_user_bind->mad_class == IB_MCLASS_SUBN_ADM) {
			set_bit(IB_MAD_METHOD_GETTABLE, &method_mask);
			set_bit(IB_MAD_METHOD_DELETE, &method_mask);
#ifdef DUAL_SIDED_RMPP
			set_bit(IB_MAD_METHOD_GETMULTI, &method_mask);
#endif
			/* Add in IB_MAD_METHOD_GETTRACETABLE */
			/* when supported by OpenSM */
		}
	}
	if (p_user_bind->is_report_processor)
		set_bit(IB_MAD_METHOD_REPORT, &method_mask);
	if (p_user_bind->is_trap_processor) {
		set_bit(IB_MAD_METHOD_TRAP, &method_mask);
		set_bit(IB_MAD_METHOD_TRAP_REPRESS, &method_mask);
	}
#ifndef VENDOR_RMPP_SUPPORT
	rmpp_version = 0;
#else
	/* If SA class, set rmpp_version */
	if (p_user_bind->mad_class == IB_MCLASS_SUBN_ADM)
		rmpp_version = 1;
	else
		rmpp_version = 0;
#endif

	if ((p_bind->agent_id = umad_register(p_vend->umad_port_id,
					      p_user_bind->mad_class,
					      p_user_bind->class_version,
					      rmpp_version, method_mask)) < 0) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5426: "
			"Unable to register class %u version %u\n",
			p_user_bind->mad_class, p_user_bind->class_version);
		free(p_bind);
		p_bind = 0;
		goto Exit;
	}

	if (p_bind->agent_id >= OSM_UMAD_MAX_AGENTS ||
	    p_vend->agents[p_bind->agent_id]) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5427: "
			"bad agent id %u or duplicate agent for class %u vers %u\n",
			p_bind->agent_id, p_user_bind->mad_class,
			p_user_bind->class_version);
		free(p_bind);
		p_bind = 0;
		goto Exit;
	}

	p_vend->agents[p_bind->agent_id] = p_bind;

	/* If Subn Directed Route class, register Subn LID routed class */
	if (p_user_bind->mad_class == IB_MCLASS_SUBN_DIR) {
		if ((p_bind->agent_id1 = umad_register(p_vend->umad_port_id,
						       IB_MCLASS_SUBN_LID,
						       p_user_bind->
						       class_version, 0,
						       method_mask)) < 0) {
			OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5428: "
				"Unable to register class 1 version %u\n",
				p_user_bind->class_version);
			free(p_bind);
			p_bind = 0;
			goto Exit;
		}

		if (p_bind->agent_id1 >= OSM_UMAD_MAX_AGENTS ||
		    p_vend->agents[p_bind->agent_id1]) {
			OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5429: "
				"bad agent id %u or duplicate agent for class 1 vers %u\n",
				p_bind->agent_id1, p_user_bind->class_version);
			free(p_bind);
			p_bind = 0;
			goto Exit;
		}

		p_vend->agents[p_bind->agent_id1] = p_bind;
	}

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return ((osm_bind_handle_t) p_bind);
}

static void
__osm_vendor_recv_dummy_cb(IN osm_madw_t * p_madw,
			   IN void *bind_context, IN osm_madw_t * p_req_madw)
{
#ifdef _DEBUG_
	fprintf(stderr,
		"__osm_vendor_recv_dummy_cb: Ignoring received MAD after osm_vendor_unbind\n");
#endif
}

static void
__osm_vendor_send_err_dummy_cb(IN void *bind_context,
			       IN osm_madw_t * p_req_madw)
{
#ifdef _DEBUG_
	fprintf(stderr,
		"__osm_vendor_send_err_dummy_cb: Ignoring send error after osm_vendor_unbind\n");
#endif
}

void osm_vendor_unbind(IN osm_bind_handle_t h_bind)
{
	osm_umad_bind_info_t *p_bind = (osm_umad_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;

	OSM_LOG_ENTER(p_vend->p_log);

	pthread_mutex_lock(&p_vend->cb_mutex);
	p_bind->mad_recv_callback = __osm_vendor_recv_dummy_cb;
	p_bind->send_err_callback = __osm_vendor_send_err_dummy_cb;
	pthread_mutex_unlock(&p_vend->cb_mutex);

	OSM_LOG_EXIT(p_vend->p_log);
}

ib_mad_t *osm_vendor_get(IN osm_bind_handle_t h_bind,
			 IN const uint32_t mad_size,
			 IN osm_vend_wrap_t * const p_vw)
{
	osm_umad_bind_info_t *p_bind = (osm_umad_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;

	OSM_LOG_ENTER(p_vend->p_log);

	OSM_LOG(p_vend->p_log, OSM_LOG_DEBUG,
		"Acquiring UMAD for p_madw = %p, size = %u\n", p_vw, mad_size);
	CL_ASSERT(p_vw);
	p_vw->size = mad_size;
	p_vw->umad = umad_alloc(1, mad_size + umad_size());

	/* track locally */
	p_vw->h_bind = h_bind;

	OSM_LOG(p_vend->p_log, OSM_LOG_DEBUG,
		"Acquired UMAD %p, size = %u\n", p_vw->umad, p_vw->size);

	OSM_LOG_EXIT(p_vend->p_log);
	return (p_vw->umad ? umad_get_mad(p_vw->umad) : NULL);
}

void
osm_vendor_put(IN osm_bind_handle_t h_bind, IN osm_vend_wrap_t * const p_vw)
{
	osm_umad_bind_info_t *p_bind = (osm_umad_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;
	osm_madw_t *p_madw;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vw);

	OSM_LOG(p_vend->p_log, OSM_LOG_DEBUG, "Retiring UMAD %p\n", p_vw->umad);

	/*
	 * We moved the removal of the transaction to immediately after
	 * it was looked up.
	 */

	/* free the mad but the wrapper is part of the madw object */
	umad_free(p_vw->umad);
	p_vw->umad = 0;
	p_madw = PARENT_STRUCT(p_vw, osm_madw_t, vend_wrap);
	p_madw->p_mad = NULL;

	OSM_LOG_EXIT(p_vend->p_log);
}

ib_api_status_t
osm_vendor_send(IN osm_bind_handle_t h_bind,
		IN osm_madw_t * const p_madw, IN boolean_t const resp_expected)
{
	osm_umad_bind_info_t *const p_bind = h_bind;
	osm_vendor_t *const p_vend = p_bind->p_vend;
	osm_vend_wrap_t *const p_vw = osm_madw_get_vend_ptr(p_madw);
	osm_mad_addr_t *const p_mad_addr = osm_madw_get_mad_addr_ptr(p_madw);
	ib_mad_t *const p_mad = osm_madw_get_mad_ptr(p_madw);
	ib_sa_mad_t *const p_sa = (ib_sa_mad_t *) p_mad;
	ib_mad_addr_t mad_addr;
	int ret = -1;
	int __attribute__((__unused__)) is_rmpp = 0;
	uint32_t sent_mad_size;
	uint64_t tid;
#ifndef VENDOR_RMPP_SUPPORT
	uint32_t paylen = 0;
#endif

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vw->h_bind == h_bind);
	CL_ASSERT(p_mad == umad_get_mad(p_vw->umad));

	if (p_mad->mgmt_class == IB_MCLASS_SUBN_DIR) {
		umad_set_addr_net(p_vw->umad, 0xffff, 0, 0, 0);
		umad_set_grh(p_vw->umad, NULL);
		goto Resp;
	}
	if (p_mad->mgmt_class == IB_MCLASS_SUBN_LID) {
		umad_set_addr_net(p_vw->umad, p_mad_addr->dest_lid, 0, 0, 0);
		umad_set_grh(p_vw->umad, NULL);
		goto Resp;
	}
	/* GS classes */
	umad_set_addr_net(p_vw->umad, p_mad_addr->dest_lid,
			  p_mad_addr->addr_type.gsi.remote_qp,
			  p_mad_addr->addr_type.gsi.service_level,
			  IB_QP1_WELL_KNOWN_Q_KEY);
	if (p_mad_addr->addr_type.gsi.global_route) {
		mad_addr.grh_present = 1;
		mad_addr.gid_index = 0;
		mad_addr.hop_limit = p_mad_addr->addr_type.gsi.grh_info.hop_limit;
		ib_grh_get_ver_class_flow(p_mad_addr->addr_type.gsi.grh_info.ver_class_flow,
					  NULL, &mad_addr.traffic_class,
					  &mad_addr.flow_label);
		memcpy(&mad_addr.gid, &p_mad_addr->addr_type.gsi.grh_info.dest_gid, 16);
		umad_set_grh(p_vw->umad, &mad_addr);
	} else
		umad_set_grh(p_vw->umad, NULL);
	umad_set_pkey(p_vw->umad, p_mad_addr->addr_type.gsi.pkey_ix);
	if (ib_class_is_rmpp(p_mad->mgmt_class)) {	/* RMPP GS classes */
		if (!ib_rmpp_is_flag_set((ib_rmpp_mad_t *) p_sa,
					 IB_RMPP_FLAG_ACTIVE)) {
			/* Clear RMPP header when RMPP not ACTIVE */
			p_sa->rmpp_version = 0;
			p_sa->rmpp_type = 0;
			p_sa->rmpp_flags = 0;
			p_sa->rmpp_status = 0;
#ifdef VENDOR_RMPP_SUPPORT
		} else
			is_rmpp = 1;
		OSM_LOG(p_vend->p_log, OSM_LOG_DEBUG, "RMPP %d length %d\n",
			ib_rmpp_is_flag_set((ib_rmpp_mad_t *) p_sa,
					    IB_RMPP_FLAG_ACTIVE),
			p_madw->mad_size);
#else
		} else {
			p_sa->rmpp_version = 1;
			p_sa->seg_num = cl_ntoh32(1);	/* first DATA is seg 1 */
			p_sa->rmpp_flags |= (uint8_t) 0x70;	/* RRespTime of 14 (high 5 bits) */
			p_sa->rmpp_status = 0;
			paylen = p_madw->mad_size - IB_SA_MAD_HDR_SIZE;
			paylen += (IB_SA_MAD_HDR_SIZE - MAD_RMPP_HDR_SIZE);
			p_sa->paylen_newwin = cl_ntoh32(paylen);
		}
#endif
	}

Resp:
	if (resp_expected)
		put_madw(p_vend, p_madw, p_mad->trans_id, p_mad->mgmt_class);

#ifdef VENDOR_RMPP_SUPPORT
	sent_mad_size = p_madw->mad_size;
#else
	sent_mad_size = is_rmpp ? p_madw->mad_size - IB_SA_MAD_HDR_SIZE :
	    p_madw->mad_size;
#endif
	tid = cl_ntoh64(p_mad->trans_id);
	if ((ret = umad_send(p_bind->port_id, p_bind->agent_id, p_vw->umad,
			     sent_mad_size,
			     resp_expected ? p_bind->timeout : 0,
			     p_bind->max_retries)) < 0) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5430: "
			"Send p_madw = %p of size %d, Class 0x%x, Method 0x%X, "
			"Attr 0x%X, TID 0x%" PRIx64 " failed %d (%m)\n",
			p_madw, sent_mad_size, p_mad->mgmt_class,
			p_mad->method, cl_ntoh16(p_mad->attr_id), tid, ret);
		if (resp_expected) {
			get_madw(p_vend, &p_mad->trans_id,
				 p_mad->mgmt_class);	/* remove from aging table */
			p_madw->status = IB_ERROR;
			pthread_mutex_lock(&p_vend->cb_mutex);
			(*p_bind->send_err_callback) (p_bind->client_context, p_madw);	/* cb frees madw */
			pthread_mutex_unlock(&p_vend->cb_mutex);
		} else
			osm_mad_pool_put(p_bind->p_mad_pool, p_madw);
		goto Exit;
	}

	if (!resp_expected)
		osm_mad_pool_put(p_bind->p_mad_pool, p_madw);

	OSM_LOG(p_vend->p_log, OSM_LOG_DEBUG, "Completed sending %s TID 0x%" PRIx64 "\n",
		resp_expected ? "request" : "response or unsolicited", tid);
Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (ret);
}

ib_api_status_t osm_vendor_local_lid_change(IN osm_bind_handle_t h_bind)
{
	osm_umad_bind_info_t *p_bind = (osm_umad_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;

	OSM_LOG_ENTER(p_vend->p_log);
	;
	OSM_LOG_EXIT(p_vend->p_log);
	return (0);
}

void osm_vendor_set_sm(IN osm_bind_handle_t h_bind, IN boolean_t is_sm_val)
{
	osm_umad_bind_info_t *p_bind = (osm_umad_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;

	OSM_LOG_ENTER(p_vend->p_log);
	if (TRUE == is_sm_val) {
		p_vend->issmfd = open(p_vend->issm_path, O_NONBLOCK);
		if (p_vend->issmfd < 0) {
			OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5431: "
				"setting IS_SM capmask: cannot open file "
				"\'%s\': %s\n",
				p_vend->issm_path, strerror(errno));
			p_vend->issmfd = -1;
		}
	} else if (p_vend->issmfd != -1) {
		if (0 != close(p_vend->issmfd))
			OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 5432: "
				"clearing IS_SM capmask: cannot close: %s\n",
				strerror(errno));
		p_vend->issmfd = -1;
	}
	OSM_LOG_EXIT(p_vend->p_log);
}

void osm_vendor_set_debug(IN osm_vendor_t * const p_vend, IN int32_t level)
{
	umad_debug(level);
}

#endif				/* OSM_VENDOR_INTF_OPENIB */
