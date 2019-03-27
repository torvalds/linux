/*
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
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

/*  AUTHOR                 Eitan Zahavi
 *
 *  DESCRIPTION
 *     The lower-level MAD transport interface implementation
 *     that allows sending a single MAD/receiving a callback
 *     when a single MAD is received.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <vendor/osm_vendor_api.h>
#include <vendor/osm_vendor_mlx_transport.h>
#include <vendor/osm_vendor_mlx_dispatcher.h>
#include <vendor/osm_vendor_mlx_svc.h>
#include <complib/cl_thread.h>

/* the simulator messages definition */
#include <ibmgtsim/ibms_client_api.h>

typedef struct _osmv_ibms_transport_mgr {
	ibms_conn_handle_t conHdl;	/* the connection handle we talk to */
	ibms_bind_msg_t filter;	/* the bind message defining the filtering */
	cl_thread_t receiver;	/* the thread waiting for incomming messages */
} osmv_ibms_transport_mgr_t;

static void
__osmv_ibms_mad_addr_to_osm_addr(IN osm_vendor_t const *p_vend,
				 IN struct _ibms_mad_addr *p_ibms_addr,
				 IN uint8_t is_smi,
				 OUT osm_mad_addr_t * p_osm_addr);

static void
__osmv_ibms_osm_addr_to_mad_addr(IN const osm_mad_addr_t * p_osm_addr,
				 IN uint8_t is_smi,
				 OUT struct _ibms_mad_addr *p_ibms_addr);

/* this is the callback function the "server" will call on incoming
   messages */
void __osmv_ibms_receiver_callback(void *p_ctx, ibms_mad_msg_t * p_mad)
{
	osm_mad_addr_t mad_addr;
	osmv_bind_obj_t *const p_bo = (osmv_bind_obj_t *) p_ctx;
	ib_api_status_t status = IB_SUCCESS;

	/* Make sure the p_bo object is still relevant */
	if ((p_bo->magic_ptr != p_bo) || p_bo->is_closing)
		return;

	{
		OSM_LOG_ENTER(p_bo->p_vendor->p_log);

		/* some logging */
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"__osmv_ibms_receiver_callback: "
			"MAD QPN:%d SLID:0x%04x class:0x%02x "
			"method:0x%02x attr:0x%04x status:0x%04x "
			"tid:0x%016" PRIx64 "\n",
			p_mad->addr.dqpn,
			cl_ntoh16(p_mad->addr.slid),
			p_mad->header.mgmt_class,
			p_mad->header.method,
			cl_ntoh16(p_mad->header.attr_id),
			cl_ntoh16(p_mad->header.status),
			cl_ntoh64(p_mad->header.trans_id));

		/* first arrange an address */
		__osmv_ibms_mad_addr_to_osm_addr(p_bo->p_vendor,
						 &p_mad->addr,
						 (((ib_mad_t *) & p_mad->
						   header)->mgmt_class ==
						  IB_MCLASS_SUBN_LID)
						 ||
						 (((ib_mad_t *) & p_mad->
						   header)->mgmt_class ==
						  IB_MCLASS_SUBN_DIR),
						 &mad_addr);

		/* call the receiver callback */

		status =
		    osmv_dispatch_mad((osm_bind_handle_t) p_bo,
				      (void *)&p_mad->header, &mad_addr);

		OSM_LOG_EXIT(p_bo->p_vendor->p_log);
	}
}

ib_api_status_t
osm_vendor_get_guid_by_ca_and_port(IN osm_vendor_t * const p_vend,
				   IN char *hca_id,
				   IN uint32_t port_num,
				   OUT uint64_t * p_port_guid);

/*
 * NAME
 *   osmv_transport_init
 *
 * DESCRIPTION
 *   Setup the MAD transport infrastructure (filters, callbacks etc).
 */

ib_api_status_t
osmv_transport_init(IN osm_bind_info_t * p_info,
		    IN char hca_id[VENDOR_HCA_MAXNAMES],
		    IN uint8_t hca_idx, IN osmv_bind_obj_t * p_bo)
{
	ibms_conn_handle_t conHdl;	/* the connection we talk to the simulator through */
	osmv_ibms_transport_mgr_t *p_mgr =
	    malloc(sizeof(osmv_ibms_transport_mgr_t));
	int qpn;
	int ibms_status;
	uint64_t port_guid;

	if (!p_mgr) {
		return IB_INSUFFICIENT_MEMORY;
	}

	memset(p_mgr, 0, sizeof(osmv_ibms_transport_mgr_t));

	/* create the client socket connected to the simulator */
	/* also perform the "connect" message - such that we
	   validate the target guid */
	if (osm_vendor_get_guid_by_ca_and_port
	    (p_bo->p_vendor, hca_id, p_bo->port_num, &port_guid)) {
		return IB_INVALID_GUID;
	}

	conHdl =
	    ibms_connect(port_guid, __osmv_ibms_receiver_callback,
			 (void *)p_bo);
	if (!conHdl) {
		printf("fail to connect to the server.\n");
		exit(1);
	}

	/*
	 * Create the MAD filter on this file handle.
	 */

	p_mgr->filter.port = p_bo->port_num;
	p_mgr->filter.only_input = 1;
	p_mgr->filter.mask =
	    IBMS_BIND_MASK_PORT |
	    IBMS_BIND_MASK_INPUT | IBMS_BIND_MASK_QP | IBMS_BIND_MASK_CLASS;

	switch (p_info->mad_class) {
	case IB_MCLASS_SUBN_LID:
	case IB_MCLASS_SUBN_DIR:
		qpn = 0;
		p_mgr->filter.qpn = qpn;
		p_mgr->filter.mgt_class = IB_MCLASS_SUBN_LID;
		ibms_status = ibms_bind(conHdl, &p_mgr->filter);
		if (ibms_status) {
			return IB_ERROR;
		}

		p_mgr->filter.mgt_class = IB_MCLASS_SUBN_DIR;
		ibms_status = ibms_bind(conHdl, &p_mgr->filter);
		if (ibms_status) {
			return IB_ERROR;
		}

		break;

	case IB_MCLASS_SUBN_ADM:
	default:
		qpn = 1;
		p_mgr->filter.qpn = qpn;
		p_mgr->filter.mgt_class = p_info->mad_class;
		ibms_status = ibms_bind(conHdl, &p_mgr->filter);
		if (ibms_status) {
			return IB_ERROR;
		}
		break;
	}

	p_mgr->conHdl = conHdl;

	p_bo->p_transp_mgr = p_mgr;

	/* Initialize the magic_ptr to the pointer of the p_bo info.
	   This will be used to signal when the object is being destroyed, so no
	   real action will be done then. */
	p_bo->magic_ptr = p_bo;

	return IB_SUCCESS;
}

/*
 * NAME
 *   osmv_transport_send_mad
 *
 * DESCRIPTION
 *   Send a single MAD (256 byte)
 */

ib_api_status_t
osmv_transport_mad_send(IN const osm_bind_handle_t h_bind,
			IN void *p_mad, IN const osm_mad_addr_t * p_mad_addr)
{

	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osm_vendor_t const *p_vend = p_bo->p_vendor;
	int ret;
	ibms_mad_msg_t mad_msg;
	ib_api_status_t status;

	const ib_mad_t *p_mad_hdr = p_mad;

	OSM_LOG_ENTER(p_vend->p_log);

	memset(&mad_msg, 0, sizeof(mad_msg));

	/* Make sure the p_bo object is still relevant */
	if ((p_bo->magic_ptr != p_bo) || p_bo->is_closing)
		return IB_INVALID_CALLBACK;

	/*
	 * Copy the MAD over to the sent mad
	 */
	memcpy(&mad_msg.header, p_mad_hdr, MAD_BLOCK_SIZE);

	/*
	 * For all sends other than directed route SM MADs,
	 * acquire an address vector for the destination.
	 */
	if (p_mad_hdr->mgmt_class != IB_MCLASS_SUBN_DIR) {

		__osmv_ibms_osm_addr_to_mad_addr(p_mad_addr,
						 p_mad_hdr->mgmt_class ==
						 IB_MCLASS_SUBN_LID,
						 &mad_msg.addr);
	} else {
		/* is a directed route - we need to construct a permissive address */
		/* we do not need port number since it is part of the mad_hndl */
		mad_msg.addr.dlid = IB_LID_PERMISSIVE;
		mad_msg.addr.slid = IB_LID_PERMISSIVE;
		mad_msg.addr.sqpn = 0;
		mad_msg.addr.dqpn = 0;
	}

	osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
		"osmv_transport_mad_send: "
		"Sending QPN:%d DLID:0x%04x class:0x%02x "
		"method:0x%02x attr:0x%04x status:0x%04x "
		"tid:0x%016" PRIx64 "\n",
		mad_msg.addr.dqpn,
		cl_ntoh16(mad_msg.addr.dlid),
		mad_msg.header.mgmt_class,
		mad_msg.header.method,
		cl_ntoh16(mad_msg.header.attr_id),
		cl_ntoh16(mad_msg.header.status),
		cl_ntoh64(mad_msg.header.trans_id)
	    );

	/* send it */
	ret =
	    ibms_send(((osmv_ibms_transport_mgr_t *) (p_bo->p_transp_mgr))->
		      conHdl, &mad_msg);
	if (ret) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osmv_transport_mad_send: ERR 5304: "
			"Error sending mad (%d).\n", ret);
		status = IB_ERROR;
		goto Exit;
	}

	status = IB_SUCCESS;

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

void osmv_transport_done(IN const osm_bind_handle_t h_bind)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osmv_ibms_transport_mgr_t *p_tpot_mgr =
	    (osmv_ibms_transport_mgr_t *) (p_bo->p_transp_mgr);

	CL_ASSERT(p_bo);

	/* First of all - zero out the magic_ptr, so if a callback is called -
	   it'll know that we are currently closing down, and will not handle the
	   mad. */
	p_bo->magic_ptr = 0;
	/* usleep(3000000); */

	ibms_disconnect(p_tpot_mgr->conHdl);

	/* seems the only way to abort a blocking read is to make it read something */
	free(p_tpot_mgr);
}

static void
__osmv_ibms_osm_addr_to_mad_addr(IN const osm_mad_addr_t * p_osm_addr,
				 IN uint8_t is_smi,
				 OUT struct _ibms_mad_addr *p_ibms_addr)
{

	/* For global destination or Multicast address: */
	p_ibms_addr->dlid = cl_ntoh16(p_osm_addr->dest_lid);
	p_ibms_addr->sl = p_osm_addr->addr_type.gsi.service_level;
	if (is_smi) {
		p_ibms_addr->sqpn = 0;
		p_ibms_addr->dqpn = 0;
	} else {
		p_ibms_addr->sqpn = 1;
		p_ibms_addr->dqpn =
		    cl_ntoh32(p_osm_addr->addr_type.gsi.remote_qp);
	}
	/*
	   HACK we limit to the first PKey Index assuming it will
	   always be the default PKey
	 */
	p_ibms_addr->pkey_index = 0;
}

static void
__osmv_ibms_mad_addr_to_osm_addr(IN osm_vendor_t const *p_vend,
				 IN struct _ibms_mad_addr *p_ibms_addr,
				 IN uint8_t is_smi,
				 OUT osm_mad_addr_t * p_osm_addr)
{
	memset(p_osm_addr, 0, sizeof(osm_mad_addr_t));
	p_osm_addr->dest_lid = cl_hton16(p_ibms_addr->slid);
	p_osm_addr->static_rate = 0;
	p_osm_addr->path_bits = 0;
	if (is_smi) {
		/* SMI */
		p_osm_addr->addr_type.smi.source_lid =
		    cl_hton16(p_ibms_addr->slid);
		p_osm_addr->addr_type.smi.port_num = 1;	/* TODO add if required p_ibms_addr->port; */
	} else {
		/* GSI */
		p_osm_addr->addr_type.gsi.remote_qp =
		    cl_ntoh32(p_ibms_addr->sqpn);
		p_osm_addr->addr_type.gsi.remote_qkey = IB_QP1_WELL_KNOWN_Q_KEY;
		p_osm_addr->addr_type.gsi.pkey_ix = p_ibms_addr->pkey_index;
		p_osm_addr->addr_type.gsi.service_level = p_ibms_addr->sl;

		p_osm_addr->addr_type.gsi.global_route = FALSE;
		/* copy the GRH data if relevant - TopSpin imp doesnt relate to GRH!!! */
		/*
		   if (p_osm_addr->addr_type.gsi.global_route)
		   {
		   p_osm_addr->addr_type.gsi.grh_info.ver_class_flow =
		   ib_grh_set_ver_class_flow(p_rcv_desc->grh.IP_version,
		   p_rcv_desc->grh.traffic_class,
		   p_rcv_desc->grh.flow_label);
		   p_osm_addr->addr_type.gsi.grh_info.hop_limit =  p_rcv_desc->grh.hop_limit;
		   memcpy(&p_osm_addr->addr_type.gsi.grh_info.src_gid.raw,
		   &p_rcv_desc->grh.sgid, sizeof(ib_net64_t));
		   memcpy(&p_osm_addr->addr_type.gsi.grh_info.dest_gid.raw,
		   p_rcv_desc->grh.dgid,  sizeof(ib_net64_t));
		   }
		 */
	}
}

/*
 *  NAME            osm_vendor_set_sm
 *
 *  DESCRIPTION     Modifies the port info for the bound port to set the "IS_SM" bit
 *                  according to the value given (TRUE or FALSE).
 */

void osm_vendor_set_sm(IN osm_bind_handle_t h_bind, IN boolean_t is_sm_val)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osm_vendor_t const *p_vend = p_bo->p_vendor;
	int ret;
	ibms_cap_msg_t cap_msg;

	OSM_LOG_ENTER(p_vend->p_log);

	cap_msg.mask = IB_PORT_CAP_IS_SM;
	if (is_sm_val)
		cap_msg.capabilities = IB_PORT_CAP_IS_SM;
	else
		cap_msg.capabilities = 0;

	ret = ibms_set_cap(((osmv_ibms_transport_mgr_t *) (p_bo->
							   p_transp_mgr))->
			   conHdl, &cap_msg);

	if (ret) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_set_sm: ERR 5312: "
			"Unable set 'IS_SM' bit to:%u in port attributes.\n",
			is_sm_val);
	}
	OSM_LOG_EXIT(p_vend->p_log);
}
