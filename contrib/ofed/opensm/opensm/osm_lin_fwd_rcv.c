/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005,2008 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2013 Oracle and/or its affiliates. All rights reserved.
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
 *    Implementation of osm_lft_rcv_t.
 * This object represents the Linear Forwarding Table Receiver object.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_LIN_FWD_RCV_C
#include <opensm/osm_switch.h>
#include <opensm/osm_sm.h>
#include <opensm/osm_event_plugin.h>
#include <opensm/osm_opensm.h>

void osm_lft_rcv_process(IN void *context, IN void *data)
{
	osm_sm_t *sm = context;
	osm_madw_t *p_madw = data;
	ib_smp_t *p_smp;
	uint32_t block_num;
	osm_switch_t *p_sw;
	osm_lft_context_t *p_lft_context;
	uint8_t *p_block;
	ib_net64_t node_guid;
	osm_epi_lft_change_event_t lft_change;
	ib_api_status_t status;

	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_madw);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_block = ib_smp_get_payload_ptr(p_smp);
	block_num = cl_ntoh32(p_smp->attr_mod);

	/*
	   Acquire the switch object for this switch.
	 */
	p_lft_context = osm_madw_get_lft_context_ptr(p_madw);
	node_guid = p_lft_context->node_guid;

	if (ib_smp_get_status(p_smp)) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"MAD status 0x%x received\n",
			cl_ntoh16(ib_smp_get_status(p_smp)));
		goto Exit;
	}

	CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);
	p_sw = osm_get_switch_by_guid(sm->p_subn, node_guid);

	if (!p_sw) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0401: "
			"LFT received for nonexistent node "
			"0x%" PRIx64 "\n", cl_ntoh64(node_guid));
	} else {
		status = osm_switch_set_lft_block(p_sw, p_block, block_num);
		if (status == IB_SUCCESS) {
			if (sm->p_subn->first_time_master_sweep == FALSE) {
				lft_change.p_sw = p_sw;
				lft_change.flags = LFT_CHANGED_BLOCK;
				lft_change.lft_top = 0;
				lft_change.block_num = block_num;
				osm_opensm_report_event(sm->p_subn->p_osm,
							OSM_EVENT_ID_LFT_CHANGE,
							&lft_change);
			}
		} else {
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0402: "
				"Setting forwarding table block %u failed (%s)"
				", Switch 0x%" PRIx64 " %s\n", block_num,
				ib_get_err_str(status), cl_ntoh64(node_guid),
				p_sw->p_node->print_desc);
		}
	}

	CL_PLOCK_RELEASE(sm->p_lock);
Exit:
	OSM_LOG_EXIT(sm->p_log);
}
