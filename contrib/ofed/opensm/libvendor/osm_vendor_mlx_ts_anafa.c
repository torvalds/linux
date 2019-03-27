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

/*  AUTHOR                 Edward Bortnikov
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
#include <stddef.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <vendor/osm_vendor_api.h>
#include <vendor/osm_vendor_mlx_transport.h>
#include <vendor/osm_vendor_mlx_transport_anafa.h>
#include <vendor/osm_vendor_mlx_dispatcher.h>
#include <vendor/osm_vendor_mlx_svc.h>
#include <vendor/osm_ts_useraccess.h>

static void
__osmv_TOPSPIN_ANAFA_mad_addr_to_osm_addr(IN osm_vendor_t const *p_vend,
					  IN struct ib_mad *p_mad,
					  IN uint8_t is_smi,
					  OUT osm_mad_addr_t * p_mad_addr);

static void
__osmv_TOPSPIN_ANAFA_osm_addr_to_mad_addr(IN const osm_mad_addr_t *
					  p_mad_addr, IN uint8_t is_smi,
					  OUT struct ib_mad *p_mad);

void __osmv_TOPSPIN_ANAFA_receiver_thr(void *p_ctx)
{
	int ts_ret_code;
	struct ib_mad mad;
	osm_mad_addr_t mad_addr;
	osmv_bind_obj_t *const p_bo = (osmv_bind_obj_t *) p_ctx;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	/* Make sure the p_bo object is still relevant */
	if ((p_bo->magic_ptr != p_bo) || p_bo->is_closing)
		return;

	/* we set the type of cancelation for this thread */
	/* pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL); */

	while (1) {
		/* Make sure the p_bo object is still relevant */
		if ((p_bo->magic_ptr != p_bo) || p_bo->is_closing)
			return;

		/* we read one mad at a time and pass it to the read callback function */
		ts_ret_code =
		    read(((osmv_TOPSPIN_ANAFA_transport_mgr_t *) (p_bo->
								  p_transp_mgr))->
			 device_fd, &mad, sizeof(mad));

		/* Make sure the p_bo object is still relevant */
		if ((p_bo->magic_ptr != p_bo) || p_bo->is_closing)
			return;

		if (ts_ret_code != sizeof(mad)) {
			osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
				"__osmv_TOPSPIN_ANAFA_receiver_thr: ERR 6903: "
				"error with read, bytes = %d\n", ts_ret_code);
			break;
		} else {
			osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
				"__osmv_TOPSPIN_ANAFA_receiver_thr: "
				"MAD QPN:%d SLID:0x%04x class:0x%02x "
				"method:0x%02x attr:0x%04x status:0x%04x "
				"tid:0x%016" PRIx64 "\n",
				mad.dqpn,
				cl_ntoh16(mad.slid),
				mad.mgmt_class,
				mad.r_method,
				cl_ntoh16(mad.attribute_id),
				cl_ntoh16(mad.status),
				cl_ntoh64(mad.transaction_id));

			/* first arrange an address */
			__osmv_TOPSPIN_ANAFA_mad_addr_to_osm_addr
			    (p_bo->p_vendor, &mad,
			     (((ib_mad_t *) & mad)->mgmt_class ==
			      IB_MCLASS_SUBN_LID)
			     || (((ib_mad_t *) & mad)->mgmt_class ==
				 IB_MCLASS_SUBN_DIR), &mad_addr);

			/* call the receiver callback */

			status =
			    osmv_dispatch_mad((osm_bind_handle_t) p_bo,
					      (void *)&mad, &mad_addr);

			/* Make sure the p_bo object is still relevant */
			if (p_bo->magic_ptr != p_bo)
				return;

			if (IB_INTERRUPTED == status) {

				osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
					"__osmv_TOPSPIN_ANAFA_receiver_thr: "
					"The bind handle %p is being closed. "
					"Breaking the loop.\n", p_bo);
				break;
			}
		}
	}

	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
}

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
	cl_status_t cl_st;

	int ts_ioctl_ret;
	int device_fd;
	char *device_file = "/dev/ts_ua0";
	osm_ts_user_mad_filter filter;
	osmv_TOPSPIN_ANAFA_transport_mgr_t *p_mgr;
	osmv_TOPSPIN_ANAFA_transport_info_t *p_tpot_info;
	p_tpot_info =
	    (osmv_TOPSPIN_ANAFA_transport_info_t *) p_bo->p_vendor->
	    p_transport_info;

	p_mgr = malloc(sizeof(osmv_TOPSPIN_ANAFA_transport_mgr_t));
	if (!p_mgr) {
		return IB_INSUFFICIENT_MEMORY;
	}

	memset(p_mgr, 0, sizeof(osmv_TOPSPIN_ANAFA_transport_mgr_t));

	/* open TopSpin file device */
	device_fd = open(device_file, O_RDWR);
	if (device_fd < 0) {
		fprintf(stderr, "Fatal: Fail to open the file:%s err:%d\n",
			device_file, errno);
		return IB_ERROR;
	}
	p_mgr->device_fd = device_fd;

	/*
	 * Create the MAD filter on this file handle.
	 */

	filter.port = 0;	/* Victor */
	filter.direction = TS_IB_MAD_DIRECTION_IN;
	filter.mask =
	    TS_IB_MAD_FILTER_DIRECTION |
	    TS_IB_MAD_FILTER_PORT |
	    TS_IB_MAD_FILTER_QPN | TS_IB_MAD_FILTER_MGMT_CLASS;

	switch (p_info->mad_class) {
	case IB_MCLASS_SUBN_LID:
	case IB_MCLASS_SUBN_DIR:
		filter.qpn = 0;
		filter.mgmt_class = IB_MCLASS_SUBN_LID;
		ts_ioctl_ret = ioctl(device_fd, TS_IB_IOCSMADFILTADD, &filter);
		if (ts_ioctl_ret < 0) {
			return IB_ERROR;
		}

		filter.mgmt_class = IB_MCLASS_SUBN_DIR;
		ts_ioctl_ret = ioctl(device_fd, TS_IB_IOCSMADFILTADD, &filter);
		if (ts_ioctl_ret < 0) {
			return IB_ERROR;
		}

		break;

	case IB_MCLASS_SUBN_ADM:
	default:
		filter.qpn = 1;
		filter.mgmt_class = p_info->mad_class;
		ts_ioctl_ret = ioctl(device_fd, TS_IB_IOCSMADFILTADD, &filter);
		if (ts_ioctl_ret < 0) {
			return IB_ERROR;
		}
		break;
	}

	p_bo->p_transp_mgr = p_mgr;

	/* Initialize the magic_ptr to the pointer of the p_bo info.
	   This will be used to signal when the object is being destroyed, so no
	   real action will be done then. */
	p_bo->magic_ptr = p_bo;

	/* init receiver thread */
	cl_st =
	    cl_thread_init(&p_mgr->receiver, __osmv_TOPSPIN_ANAFA_receiver_thr,
			   (void *)p_bo, "osmv TOPSPIN_ANAFA rcv thr");

	return (ib_api_status_t) cl_st;
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
	struct ib_mad ts_mad = { 0 };
	int ret;
	ib_api_status_t status;

	const ib_mad_t *p_mad_hdr = p_mad;

	OSM_LOG_ENTER(p_vend->p_log);

	/* Make sure the p_bo object is still relevant */
	if (p_bo->magic_ptr != p_bo)
		return IB_INVALID_CALLBACK;

	/*
	 * Copy the MAD over to the sent mad
	 */
	memcpy(&ts_mad, p_mad_hdr, MAD_BLOCK_SIZE);

	/*
	 * For all sends other than directed route SM MADs,
	 * acquire an address vector for the destination.
	 */
	if (p_mad_hdr->mgmt_class != IB_MCLASS_SUBN_DIR) {

		__osmv_TOPSPIN_ANAFA_osm_addr_to_mad_addr(p_mad_addr,
							  p_mad_hdr->
							  mgmt_class ==
							  IB_MCLASS_SUBN_LID,
							  &ts_mad);
	} else {
		/* is a directed route - we need to construct a permissive address */
		/* we do not need port number since it is part of the mad_hndl */
		ts_mad.dlid = IB_LID_PERMISSIVE;
		ts_mad.slid = IB_LID_PERMISSIVE;
	}
	if ((p_mad_hdr->mgmt_class == IB_MCLASS_SUBN_DIR) ||
	    (p_mad_hdr->mgmt_class == IB_MCLASS_SUBN_LID)) {
		ts_mad.sqpn = 0;
		ts_mad.dqpn = 0;
	} else {
		ts_mad.sqpn = 1;
		ts_mad.dqpn = 1;
	}

	/* ts_mad.port = p_bo->port_num; */
	ts_mad.port = 0;	/* Victor */

	/* send it */
	ret =
	    write(((osmv_TOPSPIN_ANAFA_transport_mgr_t *) (p_bo->
							   p_transp_mgr))->
		  device_fd, &ts_mad, sizeof(ts_mad));

	if (ret != sizeof(ts_mad)) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osmv_transport_mad_send: ERR 6904: "
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
	osmv_TOPSPIN_ANAFA_transport_mgr_t *p_tpot_mgr =
	    (osmv_TOPSPIN_ANAFA_transport_mgr_t *) (p_bo->p_transp_mgr);

	CL_ASSERT(p_bo);

	/* First of all - zero out the magic_ptr, so if a callback is called -
	   it'll know that we are currently closing down, and will not handle the
	   mad. */
	p_bo->magic_ptr = 0;

	/* usleep(3000000); */

	/* pthread_cancel (p_tpot_mgr->receiver.osd.id); */
	cl_thread_destroy(&(p_tpot_mgr->receiver));
	free(p_tpot_mgr);
}

static void
__osmv_TOPSPIN_ANAFA_osm_addr_to_mad_addr(IN const osm_mad_addr_t * p_mad_addr,
					  IN uint8_t is_smi,
					  OUT struct ib_mad *p_mad)
{

	/* For global destination or Multicast address: */
	p_mad->dlid = cl_ntoh16(p_mad_addr->dest_lid);
	p_mad->sl = p_mad_addr->addr_type.gsi.service_level;
	if (is_smi) {
		p_mad->sqpn = 0;
		p_mad->dqpn = 0;
	} else {
		p_mad->sqpn = 1;
		p_mad->dqpn = p_mad_addr->addr_type.gsi.remote_qp;
	}
	/*
	   HACK we limit to the first PKey Index assuming it will
	   always be the default PKey
	 */
	p_mad->pkey_index = 0;
}

static void
__osmv_TOPSPIN_ANAFA_mad_addr_to_osm_addr(IN osm_vendor_t const *p_vend,
					  IN struct ib_mad *p_mad,
					  IN uint8_t is_smi,
					  OUT osm_mad_addr_t * p_mad_addr)
{
	p_mad_addr->dest_lid = cl_hton16(p_mad->slid);
	p_mad_addr->static_rate = 0;
	p_mad_addr->path_bits = 0;
	if (is_smi) {
		/* SMI */
		p_mad_addr->addr_type.smi.source_lid = cl_hton16(p_mad->slid);
		p_mad_addr->addr_type.smi.port_num = p_mad->port;
	} else {
		/* GSI */
		p_mad_addr->addr_type.gsi.remote_qp = p_mad->sqpn;
		p_mad_addr->addr_type.gsi.remote_qkey = IB_QP1_WELL_KNOWN_Q_KEY;
		p_mad_addr->addr_type.gsi.pkey_ix = p_mad->pkey_index;
		p_mad_addr->addr_type.gsi.service_level = p_mad->sl;

		p_mad_addr->addr_type.gsi.global_route = FALSE;
		/* copy the GRH data if relevant - TopSpin imp doesnt relate to GRH!!! */
		/*
		   if (p_mad_addr->addr_type.gsi.global_route)
		   {
		   p_mad_addr->addr_type.gsi.grh_info.ver_class_flow =
		   ib_grh_set_ver_class_flow(p_rcv_desc->grh.IP_version,
		   p_rcv_desc->grh.traffic_class,
		   p_rcv_desc->grh.flow_label);
		   p_mad_addr->addr_type.gsi.grh_info.hop_limit =  p_rcv_desc->grh.hop_limit;
		   memcpy(&p_mad_addr->addr_type.gsi.grh_info.src_gid.raw,
		   &p_rcv_desc->grh.sgid, sizeof(ib_net64_t));
		   memcpy(&p_mad_addr->addr_type.gsi.grh_info.dest_gid.raw,
		   p_rcv_desc->grh.dgid,  sizeof(ib_net64_t));
		   }
		 */
	}
}
