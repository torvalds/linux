/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2006 Mellanox Technologies LTD. All rights reserved.
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
 *    Implementation of service record functions.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <complib/cl_debug.h>
#include <complib/cl_timer.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_SERVICE_C
#include <opensm/osm_service.h>
#include <opensm/osm_opensm.h>

void osm_svcr_delete(IN osm_svcr_t * p_svcr)
{
	free(p_svcr);
}

void osm_svcr_init(IN osm_svcr_t * p_svcr,
		   IN const ib_service_record_t * p_svc_rec)
{
	CL_ASSERT(p_svcr);

	p_svcr->modified_time = cl_get_time_stamp_sec();

	/* We track the time left for this service in
	   an external field to avoid extra cl_ntoh/hton
	   required for working with the MAD field */
	p_svcr->lease_period = cl_ntoh32(p_svc_rec->service_lease);
	p_svcr->service_record = *p_svc_rec;
}

osm_svcr_t *osm_svcr_new(IN const ib_service_record_t * p_svc_rec)
{
	osm_svcr_t *p_svcr;

	CL_ASSERT(p_svc_rec);

	p_svcr = (osm_svcr_t *) malloc(sizeof(*p_svcr));
	if (p_svcr) {
		memset(p_svcr, 0, sizeof(*p_svcr));
		osm_svcr_init(p_svcr, p_svc_rec);
	}

	return p_svcr;
}

static cl_status_t match_rid_of_svc_rec(IN const cl_list_item_t * p_list_item,
					IN void *context)
{
	ib_service_record_t *p_svc_rec = (ib_service_record_t *) context;
	osm_svcr_t *p_svcr = (osm_svcr_t *) p_list_item;

	if (memcmp(&p_svcr->service_record, p_svc_rec,
		   sizeof(p_svc_rec->service_id) +
		   sizeof(p_svc_rec->service_gid) +
		   sizeof(p_svc_rec->service_pkey)))
		return CL_NOT_FOUND;
	else
		return CL_SUCCESS;
}

osm_svcr_t *osm_svcr_get_by_rid(IN osm_subn_t const *p_subn,
				IN osm_log_t * p_log,
				IN ib_service_record_t * p_svc_rec)
{
	cl_list_item_t *p_list_item;

	OSM_LOG_ENTER(p_log);

	p_list_item = cl_qlist_find_from_head(&p_subn->sa_sr_list,
					      match_rid_of_svc_rec, p_svc_rec);
	if (p_list_item == cl_qlist_end(&p_subn->sa_sr_list))
		p_list_item = NULL;

	OSM_LOG_EXIT(p_log);
	return (osm_svcr_t *) p_list_item;
}

void osm_svcr_insert_to_db(IN osm_subn_t * p_subn, IN osm_log_t * p_log,
			   IN osm_svcr_t * p_svcr)
{
	OSM_LOG_ENTER(p_log);

	OSM_LOG(p_log, OSM_LOG_DEBUG,
		"Inserting new Service Record into Database\n");

	cl_qlist_insert_head(&p_subn->sa_sr_list, &p_svcr->list_item);
	p_subn->p_osm->sa.dirty = TRUE;

	OSM_LOG_EXIT(p_log);
}

void osm_svcr_remove_from_db(IN osm_subn_t * p_subn, IN osm_log_t * p_log,
			     IN osm_svcr_t * p_svcr)
{
	OSM_LOG_ENTER(p_log);

	OSM_LOG(p_log, OSM_LOG_DEBUG,
		"Removing Service Record Name:%s ID:0x%016" PRIx64
		" from Database\n", p_svcr->service_record.service_name,
		cl_ntoh64(p_svcr->service_record.service_id));

	cl_qlist_remove_item(&p_subn->sa_sr_list, &p_svcr->list_item);
	p_subn->p_osm->sa.dirty = TRUE;

	OSM_LOG_EXIT(p_log);
}
