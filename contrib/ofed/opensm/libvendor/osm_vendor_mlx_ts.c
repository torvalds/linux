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
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <vendor/osm_vendor_api.h>
#include <vendor/osm_vendor_mlx_transport.h>
#include <vendor/osm_vendor_mlx_dispatcher.h>
#include <vendor/osm_vendor_mlx_svc.h>
#include <vendor/osm_ts_useraccess.h>

typedef struct _osmv_TOPSPIN_transport_mgr_ {
	int device_fd;
	osm_ts_user_mad_filter filter;
	cl_thread_t receiver;
} osmv_TOPSPIN_transport_mgr_t;

static void
__osmv_TOPSPIN_mad_addr_to_osm_addr(IN osm_vendor_t const *p_vend,
				    IN struct ib_mad *p_mad,
				    IN uint8_t is_smi,
				    OUT osm_mad_addr_t * p_mad_addr);

static void
__osmv_TOPSPIN_osm_addr_to_mad_addr(IN const osm_mad_addr_t * p_mad_addr,
				    IN uint8_t is_smi,
				    OUT struct ib_mad *p_mad);

void __osmv_TOPSPIN_receiver_thr(void *p_ctx)
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
		    read(((osmv_TOPSPIN_transport_mgr_t *) (p_bo->
							    p_transp_mgr))->
			 device_fd, &mad, sizeof(mad));
		/* Make sure the p_bo object is still relevant */
		if ((p_bo->magic_ptr != p_bo) || p_bo->is_closing)
			return;

		if (ts_ret_code != sizeof(mad)) {
			osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
				"__osmv_TOPSPIN_receiver_thr: ERR 6803: "
				"error with read, bytes = %d, errno = %d\n",
				ts_ret_code, errno);
			break;
		} else {
			osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
				"__osmv_TOPSPIN_receiver_thr: "
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
			__osmv_TOPSPIN_mad_addr_to_osm_addr(p_bo->p_vendor,
							    &mad,
							    (((ib_mad_t *) &
							      mad)->
							     mgmt_class ==
							     IB_MCLASS_SUBN_LID)
							    ||
							    (((ib_mad_t *) &
							      mad)->
							     mgmt_class ==
							     IB_MCLASS_SUBN_DIR),
							    &mad_addr);

			/* call the receiver callback */

			status =
			    osmv_dispatch_mad((osm_bind_handle_t) p_bo,
					      (void *)&mad, &mad_addr);

			/* Make sure the p_bo object is still relevant */
			if ((p_bo->magic_ptr != p_bo) || p_bo->is_closing)
				return;

			if (IB_INTERRUPTED == status) {

				osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
					"__osmv_TOPSPIN_receiver_thr: "
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
	char device_file[16];
	int device_fd;
	int ts_ioctl_ret;
	osmv_TOPSPIN_transport_mgr_t *p_mgr =
	    malloc(sizeof(osmv_TOPSPIN_transport_mgr_t));
	int qpn;

	if (!p_mgr) {
		return IB_INSUFFICIENT_MEMORY;
	}

	memset(p_mgr, 0, sizeof(osmv_TOPSPIN_transport_mgr_t));

	/* open TopSpin file device */
	/* HACK: assume last char in hostid is the HCA index */
	sprintf(device_file, "/dev/ts_ua%u", hca_idx);
	device_fd = open(device_file, O_RDWR);
	if (device_fd < 0) {
		fprintf(stderr, "Fatal: Fail to open the file:%s err:%d\n",
			device_file, errno);
		return IB_ERROR;
	}

	/*
	 * Create the MAD filter on this file handle.
	 */

	p_mgr->filter.port = p_bo->port_num;
	p_mgr->filter.direction = TS_IB_MAD_DIRECTION_IN;
	p_mgr->filter.mask =
	    TS_IB_MAD_FILTER_DIRECTION |
	    TS_IB_MAD_FILTER_PORT |
	    TS_IB_MAD_FILTER_QPN | TS_IB_MAD_FILTER_MGMT_CLASS;

	switch (p_info->mad_class) {
	case IB_MCLASS_SUBN_LID:
	case IB_MCLASS_SUBN_DIR:
		qpn = 0;
		p_mgr->filter.qpn = qpn;
		p_mgr->filter.mgmt_class = IB_MCLASS_SUBN_LID;
		ts_ioctl_ret =
		    ioctl(device_fd, TS_IB_IOCSMADFILTADD, &p_mgr->filter);
		if (ts_ioctl_ret < 0) {
			return IB_ERROR;
		}

		p_mgr->filter.mgmt_class = IB_MCLASS_SUBN_DIR;
		ts_ioctl_ret =
		    ioctl(device_fd, TS_IB_IOCSMADFILTADD, &p_mgr->filter);
		if (ts_ioctl_ret < 0) {
			return IB_ERROR;
		}

		break;

	case IB_MCLASS_SUBN_ADM:
	default:
		qpn = 1;
		p_mgr->filter.qpn = qpn;
		p_mgr->filter.mgmt_class = p_info->mad_class;
		ts_ioctl_ret =
		    ioctl(device_fd, TS_IB_IOCSMADFILTADD, &p_mgr->filter);
		if (ts_ioctl_ret < 0) {
			return IB_ERROR;
		}
		break;
	}

	p_mgr->device_fd = device_fd;

	p_bo->p_transp_mgr = p_mgr;

	/* Initialize the magic_ptr to the pointer of the p_bo info.
	   This will be used to signal when the object is being destroyed, so no
	   real action will be done then. */
	p_bo->magic_ptr = p_bo;

	/* init receiver thread */
	cl_st =
	    cl_thread_init(&p_mgr->receiver, __osmv_TOPSPIN_receiver_thr,
			   (void *)p_bo, "osmv TOPSPIN rcv thr");

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
	struct ib_mad ts_mad;
	int ret;
	ib_api_status_t status;

	const ib_mad_t *p_mad_hdr = p_mad;

	OSM_LOG_ENTER(p_vend->p_log);

	memset(&ts_mad, 0, sizeof(ts_mad));

	/* Make sure the p_bo object is still relevant */
	if ((p_bo->magic_ptr != p_bo) || p_bo->is_closing)
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

		__osmv_TOPSPIN_osm_addr_to_mad_addr(p_mad_addr,
						    p_mad_hdr->mgmt_class ==
						    IB_MCLASS_SUBN_LID,
						    &ts_mad);
	} else {
		/* is a directed route - we need to construct a permissive address */
		/* we do not need port number since it is part of the mad_hndl */
		ts_mad.dlid = IB_LID_PERMISSIVE;
		ts_mad.slid = IB_LID_PERMISSIVE;
		ts_mad.sqpn = 0;
		ts_mad.dqpn = 0;
	}

	ts_mad.port = p_bo->port_num;

	osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
		"osmv_transport_mad_send: "
		"Sending QPN:%d DLID:0x%04x class:0x%02x "
		"method:0x%02x attr:0x%04x status:0x%04x "
		"tid:0x%016" PRIx64 "\n",
		ts_mad.dqpn,
		cl_ntoh16(ts_mad.dlid),
		ts_mad.mgmt_class,
		ts_mad.r_method,
		cl_ntoh16(ts_mad.attribute_id),
		cl_ntoh16(ts_mad.status), cl_ntoh64(ts_mad.transaction_id)
	    );

	/* send it */
	ret =
	    write(((osmv_TOPSPIN_transport_mgr_t *) (p_bo->p_transp_mgr))->
		  device_fd, &ts_mad, sizeof(ts_mad));

	if (ret != sizeof(ts_mad)) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osmv_transport_mad_send: ERR 6804: "
			"Error sending mad (%d).\n", ret);
		status = IB_ERROR;
		goto Exit;
	}

	status = IB_SUCCESS;

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

/*
   register a new mad type to the opened device file
   and send a mad through - the main idea is to make
   the filter catch it such that the read unblocks
*/
void __osm_transport_gen_dummy_mad(osmv_bind_obj_t * p_bo)
{
	struct ib_mad ts_mad;
	osmv_TOPSPIN_transport_mgr_t *p_mgr =
	    (osmv_TOPSPIN_transport_mgr_t *) (p_bo->p_transp_mgr);
	struct ib_get_port_info_ioctl port_data;
	int ts_ioctl_ret;

	/* prepare the mad fields following the stored filter on the bind */
	memset(&ts_mad, 0, sizeof(ts_mad));
	ts_mad.format_version = 1;
	ts_mad.mgmt_class = p_mgr->filter.mgmt_class;
	ts_mad.attribute_id = 0x2;
	ts_mad.class_version = 1;
	ts_mad.r_method = cl_ntoh16(0x2);
	ts_mad.port = p_bo->port_num;
	ts_mad.sqpn = p_mgr->filter.qpn;
	ts_mad.dqpn = p_mgr->filter.qpn;
	ts_mad.slid = 0xffff;
	/* we must send to our local lid ... */
	port_data.port = p_bo->port_num;
	ts_ioctl_ret = ioctl(p_mgr->device_fd, TS_IB_IOCGPORTINFO, &port_data);
	ts_mad.dlid = port_data.port_info.lid;
	ts_mad.transaction_id = 0x9999;
	write(p_mgr->device_fd, &ts_mad, sizeof(ts_mad));
}

void osmv_transport_done(IN const osm_bind_handle_t h_bind)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osmv_TOPSPIN_transport_mgr_t *p_tpot_mgr =
	    (osmv_TOPSPIN_transport_mgr_t *) (p_bo->p_transp_mgr);

	CL_ASSERT(p_bo);

	/* First of all - zero out the magic_ptr, so if a callback is called -
	   it'll know that we are currently closing down, and will not handle the
	   mad. */
	p_bo->magic_ptr = 0;
	/* usleep(3000000); */

	/* seems the only way to abort a blocking read is to make it read something */
	__osm_transport_gen_dummy_mad(p_bo);
	cl_thread_destroy(&(p_tpot_mgr->receiver));
	free(p_tpot_mgr);
}

static void
__osmv_TOPSPIN_osm_addr_to_mad_addr(IN const osm_mad_addr_t * p_mad_addr,
				    IN uint8_t is_smi, OUT struct ib_mad *p_mad)
{

	/* For global destination or Multicast address: */
	p_mad->dlid = cl_ntoh16(p_mad_addr->dest_lid);
	p_mad->sl = p_mad_addr->addr_type.gsi.service_level;
	if (is_smi) {
		p_mad->sqpn = 0;
		p_mad->dqpn = 0;
	} else {
		p_mad->sqpn = 1;
		p_mad->dqpn = cl_ntoh32(p_mad_addr->addr_type.gsi.remote_qp);
	}
	/*
	   HACK we limit to the first PKey Index assuming it will
	   always be the default PKey
	 */
	p_mad->pkey_index = 0;
}

static void
__osmv_TOPSPIN_mad_addr_to_osm_addr(IN osm_vendor_t const *p_vend,
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
		p_mad_addr->addr_type.gsi.remote_qp = cl_ntoh32(p_mad->sqpn);
		p_mad_addr->addr_type.gsi.remote_qkey = IB_QP1_WELL_KNOWN_Q_KEY;
		/*  There is a TAVOR limitation that only one P_KEY is supported per */
		/*  QP - so QP1 must use IB_DEFAULT_PKEY */
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

/*
 *  NAME            osm_vendor_set_sm
 *
 *  DESCRIPTION     Modifies the port info for the bound port to set the "IS_SM" bit
 *                  according to the value given (TRUE or FALSE).
 */
#if (defined(OSM_VENDOR_INTF_TS_NO_VAPI) || defined(OSM_VENDOR_INTF_TS))

void osm_vendor_set_sm(IN osm_bind_handle_t h_bind, IN boolean_t is_sm_val)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osm_vendor_t const *p_vend = p_bo->p_vendor;
	int ts_ioctl_ret;
	int device_fd =
	    ((osmv_TOPSPIN_transport_mgr_t *) (p_bo->p_transp_mgr))->device_fd;
	struct ib_set_port_info_ioctl set_port_data;

	OSM_LOG_ENTER(p_vend->p_log);

	memset(&set_port_data, 0, sizeof(set_port_data));

	set_port_data.port = p_bo->port_num;
	set_port_data.port_info.valid_fields = IB_PORT_IS_SM;
	set_port_data.port_info.is_sm = is_sm_val;
	ts_ioctl_ret = ioctl(device_fd, TS_IB_IOCSPORTINFO, &set_port_data);
	if (ts_ioctl_ret < 0) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_set_sm: ERR 6805: "
			"Unable set 'IS_SM' bit to:%u in port attributes (%d).\n",
			is_sm_val, ts_ioctl_ret);
	}

	OSM_LOG_EXIT(p_vend->p_log);
}

#endif
