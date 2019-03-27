/*
 * Copyright (c) 2006-2009 Voltaire, Inc. All rights reserved.
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
 *    Implementation of osm_guid_mgr_t.
 * This object implements the GUID manager object.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_debug.h>
#include <complib/cl_qlist.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_GUID_MGR_C
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_guid.h>
#include <opensm/osm_opensm.h>

static void guidinfo_set(IN osm_sa_t *sa, IN osm_port_t *p_port,
			 IN uint8_t block_num)
{
	uint8_t payload[IB_SMP_DATA_SIZE];
	osm_madw_context_t context;
	ib_api_status_t status;

	memcpy(payload,
	       &((*p_port->p_physp->p_guids)[block_num * GUID_TABLE_MAX_ENTRIES]),
	       sizeof(ib_guid_info_t));

	context.gi_context.node_guid = osm_node_get_node_guid(p_port->p_node);
	context.gi_context.port_guid = osm_physp_get_port_guid(p_port->p_physp);
	context.gi_context.set_method = TRUE;
	context.gi_context.port_num = osm_physp_get_port_num(p_port->p_physp);

	status = osm_req_set(sa->sm, osm_physp_get_dr_path_ptr(p_port->p_physp),
			     payload, sizeof(payload), IB_MAD_ATTR_GUID_INFO,
			     cl_hton32((uint32_t)block_num), FALSE,
			     ib_port_info_get_m_key(&p_port->p_physp->port_info),
					            CL_DISP_MSGID_NONE, &context);
	if (status != IB_SUCCESS)
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5109: "
			"Failure initiating GUIDInfo request (%s)\n",
			ib_get_err_str(status));
}

osm_guidinfo_work_obj_t *osm_guid_work_obj_new(IN osm_port_t * p_port,
					       IN uint8_t block_num)
{
	osm_guidinfo_work_obj_t *p_obj;

	/*
	   clean allocated memory to avoid assertion when trying to insert to
	   qlist.
	   see cl_qlist_insert_tail(): CL_ASSERT(p_list_item->p_list != p_list)
	*/
	p_obj = calloc(1, sizeof(*p_obj));
	if (p_obj) {
		p_obj->p_port = p_port;
		p_obj->block_num = block_num;
	}

	return p_obj;
}

void osm_guid_work_obj_delete(IN osm_guidinfo_work_obj_t * p_wobj)
{
	free(p_wobj);
}

int osm_queue_guidinfo(IN osm_sa_t *sa, IN osm_port_t *p_port,
		      IN uint8_t block_num)
{
	osm_guidinfo_work_obj_t *p_obj;
	int status = 1;

	p_obj = osm_guid_work_obj_new(p_port, block_num);
	if (p_obj)
		cl_qlist_insert_tail(&sa->p_subn->alias_guid_list,
				     &p_obj->list_item);
	else {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 510F: "
			"Memory allocation of guid work object failed\n");
		status = 0;
	}

	return status;
}

void osm_guid_mgr_process(IN osm_sm_t * sm) {
	osm_guidinfo_work_obj_t *p_obj;

	OSM_LOG_ENTER(sm->p_log);

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG, "Processing alias guid list\n");

	CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);
	while (cl_qlist_count(&sm->p_subn->alias_guid_list)) {
		p_obj = (osm_guidinfo_work_obj_t *) cl_qlist_remove_head(&sm->p_subn->alias_guid_list);
		guidinfo_set(&sm->p_subn->p_osm->sa, p_obj->p_port,
			     p_obj->block_num);
		osm_guid_work_obj_delete(p_obj);
	}

	CL_PLOCK_RELEASE(sm->p_lock);
	OSM_LOG_EXIT(sm->p_log);
}
