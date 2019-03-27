/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2007 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
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
 *    Implementation of osm_lr_rcv_t.
 * This object represents the LinkRecord Receiver object.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_SA_LINK_RECORD_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_pkey.h>
#include <opensm/osm_sa.h>

#define SA_LR_RESP_SIZE SA_ITEM_RESP_SIZE(link_rec)

static void lr_rcv_build_physp_link(IN osm_sa_t * sa, IN ib_net16_t from_lid,
				    IN ib_net16_t to_lid, IN uint8_t from_port,
				    IN uint8_t to_port, IN cl_qlist_t * p_list)
{
	osm_sa_item_t *p_lr_item;

	p_lr_item = malloc(SA_LR_RESP_SIZE);
	if (p_lr_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1801: "
			"Unable to acquire link record\n"
			"\t\t\t\tFrom port %u\n" "\t\t\t\tTo port   %u\n"
			"\t\t\t\tFrom lid  %u\n" "\t\t\t\tTo lid    %u\n",
			from_port, to_port,
			cl_ntoh16(from_lid), cl_ntoh16(to_lid));
		return;
	}
	memset(p_lr_item, 0, SA_LR_RESP_SIZE);

	p_lr_item->resp.link_rec.from_port_num = from_port;
	p_lr_item->resp.link_rec.to_port_num = to_port;
	p_lr_item->resp.link_rec.to_lid = to_lid;
	p_lr_item->resp.link_rec.from_lid = from_lid;

	cl_qlist_insert_tail(p_list, &p_lr_item->list_item);
}

static ib_net16_t get_base_lid(IN const osm_physp_t * p_physp)
{
	if (p_physp->p_node->node_info.node_type == IB_NODE_TYPE_SWITCH)
		p_physp = osm_node_get_physp_ptr(p_physp->p_node, 0);
	return osm_physp_get_base_lid(p_physp);
}

static void lr_rcv_get_physp_link(IN osm_sa_t * sa,
				  IN const ib_link_record_t * p_lr,
				  IN const osm_physp_t * p_src_physp,
				  IN const osm_physp_t * p_dest_physp,
				  IN const ib_net64_t comp_mask,
				  IN cl_qlist_t * p_list,
				  IN const osm_physp_t * p_req_physp)
{
	uint8_t src_port_num;
	uint8_t dest_port_num;
	ib_net16_t from_base_lid;
	ib_net16_t to_base_lid;
	ib_net16_t lmc_mask;

	OSM_LOG_ENTER(sa->p_log);

	/*
	   If only one end of the link is specified, determine
	   the other side.
	 */
	if (p_src_physp) {
		if (p_dest_physp) {
			/*
			   Ensure the two physp's are actually connected.
			   If not, bail out.
			 */
			if (osm_physp_get_remote(p_src_physp) != p_dest_physp)
				goto Exit;
		} else {
			p_dest_physp = osm_physp_get_remote(p_src_physp);
			if (p_dest_physp == NULL)
				goto Exit;
		}
	} else {
		if (p_dest_physp) {
			p_src_physp = osm_physp_get_remote(p_dest_physp);
			if (p_src_physp == NULL)
				goto Exit;
		} else
			goto Exit;	/* no physp's, so nothing to do */
	}

	/* Check that the p_src_physp, p_dest_physp and p_req_physp
	   all share a pkey (doesn't have to be the same p_key). */
	if (!osm_physp_share_pkey(sa->p_log, p_src_physp, p_dest_physp,
				  sa->p_subn->opt.allow_both_pkeys)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Source and Dest PhysPorts do not share PKey\n");
		goto Exit;
	}
	if (!osm_physp_share_pkey(sa->p_log, p_src_physp, p_req_physp,
				  sa->p_subn->opt.allow_both_pkeys)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Source and Requester PhysPorts do not share PKey\n");
		goto Exit;
	}
	if (!osm_physp_share_pkey(sa->p_log, p_req_physp, p_dest_physp,
				  sa->p_subn->opt.allow_both_pkeys)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Requester and Dest PhysPorts do not share PKey\n");
		goto Exit;
	}

	src_port_num = osm_physp_get_port_num(p_src_physp);
	dest_port_num = osm_physp_get_port_num(p_dest_physp);

	if (comp_mask & IB_LR_COMPMASK_FROM_PORT)
		if (src_port_num != p_lr->from_port_num)
			goto Exit;

	if (comp_mask & IB_LR_COMPMASK_TO_PORT)
		if (dest_port_num != p_lr->to_port_num)
			goto Exit;

	from_base_lid = get_base_lid(p_src_physp);
	to_base_lid = get_base_lid(p_dest_physp);

	lmc_mask = ~((1 << sa->p_subn->opt.lmc) - 1);
	lmc_mask = cl_hton16(lmc_mask);

	if (comp_mask & IB_LR_COMPMASK_FROM_LID)
		if (from_base_lid != (p_lr->from_lid & lmc_mask))
			goto Exit;

	if (comp_mask & IB_LR_COMPMASK_TO_LID)
		if (to_base_lid != (p_lr->to_lid & lmc_mask))
			goto Exit;

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Acquiring link record\n"
		"\t\t\t\tsrc port 0x%" PRIx64 " (port %u)"
		", dest port 0x%" PRIx64 " (port %u)\n",
		cl_ntoh64(osm_physp_get_port_guid(p_src_physp)), src_port_num,
		cl_ntoh64(osm_physp_get_port_guid(p_dest_physp)),
		dest_port_num);

	lr_rcv_build_physp_link(sa, from_base_lid, to_base_lid, src_port_num,
				dest_port_num, p_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

static void lr_rcv_get_port_links(IN osm_sa_t * sa,
				  IN const ib_link_record_t * p_lr,
				  IN const osm_port_t * p_src_port,
				  IN const osm_port_t * p_dest_port,
				  IN const ib_net64_t comp_mask,
				  IN cl_qlist_t * p_list,
				  IN const osm_physp_t * p_req_physp)
{
	const osm_physp_t *p_src_physp;
	const osm_physp_t *p_dest_physp;
	const cl_qmap_t *p_node_tbl;
	osm_node_t *p_node;
	uint8_t port_num;
	uint8_t num_ports;
	uint8_t dest_num_ports;
	uint8_t dest_port_num;

	OSM_LOG_ENTER(sa->p_log);

	if (p_src_port) {
		if (p_dest_port) {
			/*
			   Build an LR for every link connected between both ports.
			   The inner function will discard physp combinations
			   that do not actually connect.  Don't bother screening
			   for that here.
			 */
			num_ports = osm_node_get_num_physp(p_src_port->p_node);
			dest_num_ports =
			    osm_node_get_num_physp(p_dest_port->p_node);
			for (port_num = 1; port_num < num_ports; port_num++) {
				p_src_physp =
				    osm_node_get_physp_ptr(p_src_port->p_node,
							   port_num);
				for (dest_port_num = 1;
				     dest_port_num < dest_num_ports;
				     dest_port_num++) {
					p_dest_physp =
					    osm_node_get_physp_ptr(p_dest_port->
								   p_node,
								   dest_port_num);
					/* both physical ports should be with data */
					if (p_src_physp && p_dest_physp)
						lr_rcv_get_physp_link
						    (sa, p_lr, p_src_physp,
						     p_dest_physp, comp_mask,
						     p_list, p_req_physp);
				}
			}
		} else {
			/*
			   Build an LR for every link connected from the source port.
			 */
			if (comp_mask & IB_LR_COMPMASK_FROM_PORT) {
				port_num = p_lr->from_port_num;
				/* If the port number is out of the range of the p_src_port, then
				   this couldn't be a relevant record. */
				if (port_num <
				    p_src_port->p_node->physp_tbl_size) {
					p_src_physp =
					    osm_node_get_physp_ptr(p_src_port->
								   p_node,
								   port_num);
					if (p_src_physp)
						lr_rcv_get_physp_link
						    (sa, p_lr, p_src_physp,
						     NULL, comp_mask, p_list,
						     p_req_physp);
				}
			} else {
				num_ports =
				    osm_node_get_num_physp(p_src_port->p_node);
				for (port_num = 1; port_num < num_ports;
				     port_num++) {
					p_src_physp =
					    osm_node_get_physp_ptr(p_src_port->
								   p_node,
								   port_num);
					if (p_src_physp)
						lr_rcv_get_physp_link
						    (sa, p_lr, p_src_physp,
						     NULL, comp_mask, p_list,
						     p_req_physp);
				}
			}
		}
	} else {
		if (p_dest_port) {
			/*
			   Build an LR for every link connected to the dest port.
			 */
			if (comp_mask & IB_LR_COMPMASK_TO_PORT) {
				port_num = p_lr->to_port_num;
				/* If the port number is out of the range of the p_dest_port, then
				   this couldn't be a relevant record. */
				if (port_num <
				    p_dest_port->p_node->physp_tbl_size) {
					p_dest_physp =
					    osm_node_get_physp_ptr(p_dest_port->
								   p_node,
								   port_num);
					if (p_dest_physp)
						lr_rcv_get_physp_link
						    (sa, p_lr, NULL,
						     p_dest_physp, comp_mask,
						     p_list, p_req_physp);
				}
			} else {
				num_ports =
				    osm_node_get_num_physp(p_dest_port->p_node);
				for (port_num = 1; port_num < num_ports;
				     port_num++) {
					p_dest_physp =
					    osm_node_get_physp_ptr(p_dest_port->
								   p_node,
								   port_num);
					if (p_dest_physp)
						lr_rcv_get_physp_link
						    (sa, p_lr, NULL,
						     p_dest_physp, comp_mask,
						     p_list, p_req_physp);
				}
			}
		} else {
			/*
			   Process the world (recurse once back into this function).
			 */
			p_node_tbl = &sa->p_subn->node_guid_tbl;
			p_node = (osm_node_t *) cl_qmap_head(p_node_tbl);

			while (p_node != (osm_node_t *) cl_qmap_end(p_node_tbl)) {
				num_ports = osm_node_get_num_physp(p_node);
				for (port_num = 1; port_num < num_ports;
				     port_num++) {
					p_src_physp =
					    osm_node_get_physp_ptr(p_node,
								   port_num);
					if (p_src_physp)
						lr_rcv_get_physp_link
						    (sa, p_lr, p_src_physp,
						     NULL, comp_mask, p_list,
						     p_req_physp);
				}
				p_node = (osm_node_t *) cl_qmap_next(&p_node->
								     map_item);
			}
		}
	}

	OSM_LOG_EXIT(sa->p_log);
}

/**********************************************************************
 Returns the SA status to return to the client.
 **********************************************************************/
static ib_net16_t lr_rcv_get_end_points(IN osm_sa_t * sa,
					IN const osm_madw_t * p_madw,
					OUT const osm_port_t ** pp_src_port,
					OUT const osm_port_t ** pp_dest_port)
{
	const ib_link_record_t *p_lr;
	const ib_sa_mad_t *p_sa_mad;
	ib_net64_t comp_mask;
	ib_net16_t sa_status = IB_SA_MAD_STATUS_SUCCESS;

	OSM_LOG_ENTER(sa->p_log);

	/*
	   Determine what fields are valid and then get a pointer
	   to the source and destination port objects, if possible.
	 */
	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_lr = (ib_link_record_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	comp_mask = p_sa_mad->comp_mask;
	*pp_src_port = NULL;
	*pp_dest_port = NULL;

	if (comp_mask & IB_LR_COMPMASK_FROM_LID) {
		*pp_src_port = osm_get_port_by_lid(sa->p_subn, p_lr->from_lid);
		if (!*pp_src_port) {
			/*
			   This 'error' is the client's fault (bad lid) so
			   don't enter it as an error in our own log.
			   Return an error response to the client.
			 */
			OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
				"No source port with LID %u\n",
				cl_ntoh16(p_lr->from_lid));

			sa_status = IB_SA_MAD_STATUS_NO_RECORDS;
			goto Exit;
		}
	}

	if (comp_mask & IB_LR_COMPMASK_TO_LID) {
		*pp_dest_port = osm_get_port_by_lid(sa->p_subn, p_lr->to_lid);
		if (!*pp_dest_port) {
			/*
			   This 'error' is the client's fault (bad lid) so
			   don't enter it as an error in our own log.
			   Return an error response to the client.
			 */
			OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
				"No dest port with LID %u\n",
				cl_ntoh16(p_lr->to_lid));

			sa_status = IB_SA_MAD_STATUS_NO_RECORDS;
			goto Exit;
		}
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return sa_status;
}

void osm_lr_rcv_process(IN void *context, IN void *data)
{
	osm_sa_t *sa = context;
	osm_madw_t *p_madw = data;
	const ib_link_record_t *p_lr;
	const ib_sa_mad_t *p_sa_mad;
	const osm_port_t *p_src_port;
	const osm_port_t *p_dest_port;
	cl_qlist_t lr_list;
	ib_net16_t status;
	osm_physp_t *p_req_physp;

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_lr = ib_sa_mad_get_payload_ptr(p_sa_mad);

	CL_ASSERT(p_sa_mad->attr_id == IB_MAD_ATTR_LINK_RECORD);

	/* we only support SubnAdmGet and SubnAdmGetTable methods */
	if (p_sa_mad->method != IB_MAD_METHOD_GET &&
	    p_sa_mad->method != IB_MAD_METHOD_GETTABLE) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1804: "
			"Unsupported Method (%s) for LinkRecord request\n",
			ib_get_sa_method_str(p_sa_mad->method));
		osm_sa_send_error(sa, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		goto Exit;
	}

	cl_plock_acquire(sa->p_lock);

	/* update the requester physical port */
	p_req_physp = osm_get_physp_by_mad_addr(sa->p_log, sa->p_subn,
						osm_madw_get_mad_addr_ptr
						(p_madw));
	if (p_req_physp == NULL) {
		cl_plock_release(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1805: "
			"Cannot find requester physical port\n");
		goto Exit;
	}

	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Requester port GUID 0x%" PRIx64 "\n",
			cl_ntoh64(osm_physp_get_port_guid(p_req_physp)));
		osm_dump_link_record_v2(sa->p_log, p_lr, FILE_ID, OSM_LOG_DEBUG);
	}

	cl_qlist_init(&lr_list);

	/*
	   Most SA functions (including this one) are read-only on the
	   subnet object, so we grab the lock non-exclusively.
	 */
	status = lr_rcv_get_end_points(sa, p_madw, &p_src_port, &p_dest_port);

	if (status == IB_SA_MAD_STATUS_SUCCESS)
		lr_rcv_get_port_links(sa, p_lr, p_src_port, p_dest_port,
				      p_sa_mad->comp_mask, &lr_list,
				      p_req_physp);

	cl_plock_release(sa->p_lock);

	osm_sa_respond(sa, p_madw, sizeof(ib_link_record_t), &lr_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}
