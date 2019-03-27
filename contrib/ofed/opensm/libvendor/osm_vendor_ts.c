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

#undef __init
#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <vendor/osm_vendor_ts.h>
#include <vendor/osm_vendor_api.h>
#include <vendor/osm_ts_useraccess.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_opensm.h>

/*
  Since a race can accure on requests. Meaning - a response is received before
  the send_callback is called - we will save both the madw_p and the fact
  whether or not it is a response. A race can occure only on requests that did
  not fail, and then the madw_p will be put back in the pool before the
  callback.
*/
uint64_t __osm_set_wrid_by_p_madw(IN osm_madw_t * p_madw)
{
	uint64_t wrid = 0;

	CL_ASSERT(p_madw->p_mad);

	memcpy(&wrid, &p_madw, sizeof(osm_madw_t *));
	wrid = (wrid << 1) |
	    ib_mad_is_response(p_madw->p_mad);
	return wrid;
}

void
__osm_set_p_madw_and_resp_by_wrid(IN uint64_t wrid,
				  OUT uint8_t * is_resp,
				  OUT osm_madw_t ** pp_madw)
{
	*is_resp = wrid & 0x0000000000000001;
	wrid = wrid >> 1;
	memcpy(pp_madw, &wrid, sizeof(osm_madw_t *));
}

/**********************************************************************
 * TS MAD to OSM ADDRESS VECTOR
 **********************************************************************/
void
__osm_ts_conv_mad_rcv_desc_to_osm_addr(IN osm_vendor_t * const p_vend,
				       IN struct ib_mad *p_mad,
				       IN uint8_t is_smi,
				       OUT osm_mad_addr_t * p_mad_addr)
{
	p_mad_addr->dest_lid = cl_hton16(p_mad->slid);
	p_mad_addr->static_rate = 0;	/*  HACK - we do not know the rate ! */
	p_mad_addr->path_bits = 0;	/*  HACK - no way to know in TS */
	if (is_smi) {
		/* SMI */
		p_mad_addr->addr_type.smi.source_lid = cl_hton16(p_mad->slid);
		p_mad_addr->addr_type.smi.port_num = p_mad->port;
	} else {
		/* GSI */
		p_mad_addr->addr_type.gsi.remote_qp = p_mad->sqpn;
		p_mad_addr->addr_type.gsi.remote_qkey = IB_QP1_WELL_KNOWN_Q_KEY;
		p_mad_addr->addr_type.gsi.pkey_ix = p_mad->pkey_index;
		p_mad_addr->addr_type.gsi.service_level = 0;	/*  HACK no way to know */

		p_mad_addr->addr_type.gsi.global_route = FALSE;	/*  HACK no way to know */
		/* copy the GRH data if relevant */
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

/**********************************************************************
 * OSM ADDR VECTOR TO TS MAD:
 **********************************************************************/
void
__osm_ts_conv_osm_addr_to_ts_addr(IN osm_mad_addr_t * p_mad_addr,
				  IN uint8_t is_smi, OUT struct ib_mad *p_mad)
{

	/* For global destination or Multicast address: */
	p_mad->dlid = cl_ntoh16(p_mad_addr->dest_lid);
	p_mad->sl = 0;
	if (is_smi) {
		p_mad->sqpn = 0;
		p_mad->dqpn = 0;
	} else {
		p_mad->sqpn = 1;
		p_mad->dqpn = p_mad_addr->addr_type.gsi.remote_qp;
	}
}

void __osm_vendor_clear_sm(IN osm_bind_handle_t h_bind)
{
	osm_ts_bind_info_t *p_bind = (osm_ts_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;
	VAPI_ret_t status;
	VAPI_hca_attr_t attr_mod;
	VAPI_hca_attr_mask_t attr_mask;

	OSM_LOG_ENTER(p_vend->p_log);

	memset(&attr_mod, 0, sizeof(attr_mod));
	memset(&attr_mask, 0, sizeof(attr_mask));

	attr_mod.is_sm = FALSE;
	attr_mask = HCA_ATTR_IS_SM;

	status =
	    VAPI_modify_hca_attr(p_bind->hca_hndl, p_bind->port_num, &attr_mod,
				 &attr_mask);
	if (status != VAPI_OK) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_vendor_clear_sm: ERR 5021: "
			"Unable set 'IS_SM' bit in port attributes (%d).\n",
			status);
	}

	OSM_LOG_EXIT(p_vend->p_log);
}

/**********************************************************************
 * ANY CONSTRUCTION OF THE osm_vendor_t OBJECT
 **********************************************************************/
void osm_vendor_construct(IN osm_vendor_t * const p_vend)
{
	memset(p_vend, 0, sizeof(*p_vend));
	cl_thread_construct(&(p_vend->smi_bind.poller));
	cl_thread_construct(&(p_vend->gsi_bind.poller));
}

/**********************************************************************
 * DEALOCATE osm_vendor_t
 **********************************************************************/
void osm_vendor_destroy(IN osm_vendor_t * const p_vend)
{
	OSM_LOG_ENTER(p_vend->p_log);
	osm_transaction_mgr_destroy(p_vend);

	/* Destroy the poller threads */
	/* HACK: can you destroy an un-initialized thread ? */
	pthread_cancel(p_vend->smi_bind.poller.osd.id);
	pthread_cancel(p_vend->gsi_bind.poller.osd.id);
	cl_thread_destroy(&(p_vend->smi_bind.poller));
	cl_thread_destroy(&(p_vend->gsi_bind.poller));
	OSM_LOG_EXIT(p_vend->p_log);
}

/**********************************************************************
DEALLOCATE A POINTER TO osm_vendor_t
**********************************************************************/
void osm_vendor_delete(IN osm_vendor_t ** const pp_vend)
{
	CL_ASSERT(pp_vend);

	osm_vendor_destroy(*pp_vend);
	free(*pp_vend);
	*pp_vend = NULL;
}

/**********************************************************************
 Initializes the vendor:
**********************************************************************/

ib_api_status_t
osm_vendor_init(IN osm_vendor_t * const p_vend,
		IN osm_log_t * const p_log, IN const uint32_t timeout)
{
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_log);

	p_vend->p_log = p_log;
	p_vend->p_transaction_mgr = NULL;
	osm_transaction_mgr_init(p_vend);
	p_vend->timeout = timeout;

	/* we use the file handle to track the binding */
	p_vend->smi_bind.ul_dev_fd = -1;
	p_vend->gsi_bind.ul_dev_fd = -1;

	OSM_LOG_EXIT(p_log);
	return (status);
}

/**********************************************************************
 *  Create and Initialize osm_vendor_t Object
 **********************************************************************/
osm_vendor_t *osm_vendor_new(IN osm_log_t * const p_log,
			     IN const uint32_t timeout)
{
	ib_api_status_t status;
	osm_vendor_t *p_vend;

	OSM_LOG_ENTER(p_log);

	CL_ASSERT(p_log);

	p_vend = malloc(sizeof(*p_vend));
	if (p_vend != NULL) {
		memset(p_vend, 0, sizeof(*p_vend));

		status = osm_vendor_init(p_vend, p_log, timeout);
		if (status != IB_SUCCESS) {
			osm_vendor_delete(&p_vend);
		}
	} else {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_new: ERR 5007: "
			"Fail to allocate vendor object.\n");
	}

	OSM_LOG_EXIT(p_log);
	return (p_vend);
}

/**********************************************************************
 * TS RCV Thread callback
 * HACK: - we need to make this support arbitrary size mads.
 **********************************************************************/
void
__osm_ts_rcv_callback(IN osm_ts_bind_info_t * p_bind,
		      IN osm_mad_addr_t * p_mad_addr,
		      IN uint32_t mad_size, IN void *p_mad)
{
	ib_api_status_t status;
	osm_madw_t *p_req_madw = NULL;
	osm_madw_t *p_madw;
	osm_vend_wrap_t *p_new_vw;
	ib_mad_t *p_mad_buf;
	osm_log_t *const p_log = p_bind->p_vend->p_log;

	OSM_LOG_ENTER(p_log);

	/* if it is a response MAD we mustbe able to get the request */
	if (ib_mad_is_response((ib_mad_t *) p_mad)) {
		/* can we find a matching madw by this payload TID */
		status =
		    osm_transaction_mgr_get_madw_for_tid(p_bind->p_vend,
							 (ib_mad_t *) p_mad,
							 &p_req_madw);
		if (status != IB_SUCCESS) {
			osm_log(p_log, OSM_LOG_ERROR,
				"__osm_ts_rcv_callback: ERR 5008: "
				"Error obtaining request madw by TID (%d).\n",
				status);
			p_req_madw = NULL;
		}

		if (p_req_madw == NULL) {
			osm_log(p_log, OSM_LOG_ERROR,
				"__osm_ts_rcv_callback: ERR 5009:  "
				"Fail to obtain request madw for receined MAD. Aborting CB.\n");
			goto Exit;
		}
	}

	/* do we have a request ??? */
	if (p_req_madw == NULL) {

		/* if not - get new osm_madw and arrange it. */
		/* create the new madw in the pool */
		p_madw = osm_mad_pool_get(p_bind->p_osm_pool,
					  (osm_bind_handle_t) p_bind,
					  mad_size, p_mad_addr);
		if (p_madw == NULL) {
			osm_log(p_log, OSM_LOG_ERROR,
				"__osm_ts_rcv_callback: ERR 5010: "
				"Error request for a new madw.\n");
			goto Exit;
		}
		/* HACK: we cust to avoid the const ??? */
		p_mad_buf = (void *)p_madw->p_mad;
	} else {
		/* we have the madw defined during the send and stored in the vend_wrap */
		/* we need to make sure the wrapper is correctly init there */
		CL_ASSERT(p_req_madw->vend_wrap.p_resp_madw != 0);
		p_madw = p_req_madw->vend_wrap.p_resp_madw;

		CL_ASSERT(p_madw->h_bind);
		p_mad_buf =
		    osm_vendor_get(p_madw->h_bind, mad_size,
				   &p_madw->vend_wrap);

		if (p_mad_buf == NULL) {
			osm_log(p_log, OSM_LOG_ERROR,
				"__osm_ts_rcv_callback: ERR 5011: "
				"Unable to acquire wire MAD.\n");

			goto Exit;
		}

		/*
		   Finally, attach the wire MAD to this wrapper.
		 */
		osm_madw_set_mad(p_madw, p_mad_buf);
	}

	/* init some fields of the vendor wrapper */
	p_new_vw = osm_madw_get_vend_ptr(p_madw);
	p_new_vw->h_bind = p_bind;
	p_new_vw->size = mad_size;
	p_new_vw->p_resp_madw = NULL;
	p_new_vw->p_mad_buf = p_mad_buf;

	memcpy(p_new_vw->p_mad_buf, p_mad, mad_size);

	/* attach the buffer to the wrapper */
	p_madw->p_mad = p_mad_buf;

	/* we can also make sure we marked the size and bind on the returned madw */
	p_madw->h_bind = p_new_vw->h_bind;

	/* call the CB */
	(*(osm_vend_mad_recv_callback_t) p_bind->rcv_callback)
	    (p_madw, p_bind->client_context, p_req_madw);

Exit:
	OSM_LOG_EXIT(p_log);
}

/**********************************************************************
 * TS Send callback : invoked after each send
 *
 **********************************************************************/
void
__osm_ts_send_callback(IN osm_ts_bind_info_t * bind_info_p,
		       IN boolean_t is_resp,
		       IN osm_madw_t * madw_p, IN IB_comp_status_t status)
{
	osm_log_t *const p_log = bind_info_p->p_vend->p_log;
	osm_vend_wrap_t *p_vw;

	OSM_LOG_ENTER(p_log);

	osm_log(p_log, OSM_LOG_DEBUG,
		"__osm_ts_send_callback: INFO 1008: "
		"Handling Send of MADW:%p Is Resp:%d.\n", madw_p, is_resp);

	/* we need to handle requests and responses differently */
	if (is_resp) {
		if (status != IB_COMP_SUCCESS) {
			osm_log(p_log, OSM_LOG_ERROR,
				"__osm_ts_send_callback: ERR 5012: "
				"Error Sending Response MADW:%p.\n", madw_p);
		} else {
			osm_log(p_log, OSM_LOG_DEBUG,
				"__osm_ts_send_callback: DBG 1008: "
				"Completed Sending Response MADW:%p.\n",
				madw_p);
		}

		/* if we are a response - we need to clean it up */
		osm_mad_pool_put(bind_info_p->p_osm_pool, madw_p);
	} else {

		/* this call back is invoked on completion of send - error or not */
		if (status != IB_COMP_SUCCESS) {

			osm_log(p_log, OSM_LOG_ERROR,
				"__osm_ts_send_callback: ERR 5013: "
				"Received an Error from IB_MGT Send (%d).\n",
				status);

			p_vw = osm_madw_get_vend_ptr(madw_p);
			CL_ASSERT(p_vw);

			/*
			   Return any wrappers to the pool that may have been
			   pre-emptively allocated to handle a receive.
			 */
			if (p_vw->p_resp_madw) {
				osm_mad_pool_put(bind_info_p->p_osm_pool,
						 p_vw->p_resp_madw);
				p_vw->p_resp_madw = NULL;
			}

			/* invoke the CB */
			(*(osm_vend_mad_send_err_callback_t) bind_info_p->
			 send_err_callback)
			    (bind_info_p->client_context, madw_p);
		} else {
			/* successful request send - do nothing - the response will need the
			   out mad */
			osm_log(p_log, OSM_LOG_DEBUG,
				"__osm_ts_send_callback: DBG 1008: "
				"Completed Sending Request MADW:%p.\n", madw_p);
		}
	}

	OSM_LOG_EXIT(p_log);
}

/**********************************************************************
 * Poller thread:
 * Always receive 256byte mads from the devcie file
 **********************************************************************/
void __osm_vendor_ts_poller(IN void *p_ptr)
{
	int ts_ret_code;
	struct ib_mad mad;
	osm_mad_addr_t mad_addr;
	osm_ts_bind_info_t *const p_bind = (osm_ts_bind_info_t *) p_ptr;

	OSM_LOG_ENTER(p_bind->p_vend->p_log);
	/* we set the type of cancelation for this thread */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while (1) {
		/* we read one mad at a time and pass it to the read callback function */
		ts_ret_code = read(p_bind->ul_dev_fd, &mad, sizeof(mad));
		if (ts_ret_code != sizeof(mad)) {
			osm_log(p_bind->p_vend->p_log, OSM_LOG_ERROR,
				"__osm_vendor_ts_poller: ERR 5003: "
				"error with read, bytes = %d, errno = %d\n",
				ts_ret_code, errno);
		} else {
			osm_log(p_bind->p_vend->p_log, OSM_LOG_DEBUG,
				"__osm_vendor_ts_poller: "
				"MAD QPN:%d SLID:0x%04x class:0x%02x "
				"__osm_vendor_ts_poller:0x%02x attr:0x%04x status:0x%04x "
				"__osm_vendor_ts_poller:0x%016" PRIx64 "\n",
				cl_ntoh32(mad.dqpn),
				cl_ntoh16(mad.slid),
				mad.mgmt_class,
				mad.r_method,
				cl_ntoh16(mad.attribute_id),
				cl_ntoh16(mad.status),
				cl_ntoh64(mad.transaction_id));

			/* first arrange an address */
			__osm_ts_conv_mad_rcv_desc_to_osm_addr(p_bind->p_vend,
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
			/* HACK: this should be replaced with a call to the RMPP Assembly ... */
			__osm_ts_rcv_callback(p_bind, &mad_addr, 256, &mad);
		}
	}

	OSM_LOG_EXIT(p_bind->p_vend->p_log);
}

/**********************************************************************
 * BINDs a callback (rcv and send error) for a given class and method
 * defined by the given:  osm_bind_info_t
 **********************************************************************/
osm_bind_handle_t
osm_vendor_bind(IN osm_vendor_t * const p_vend,
		IN osm_bind_info_t * const p_user_bind,
		IN osm_mad_pool_t * const p_mad_pool,
		IN osm_vend_mad_recv_callback_t mad_recv_callback,
		IN osm_vend_mad_send_err_callback_t send_err_callback,
		IN void *context)
{
	ib_net64_t port_guid;
	osm_ts_bind_info_t *p_bind = NULL;
	VAPI_hca_hndl_t hca_hndl;
	VAPI_hca_id_t hca_id;
	uint32_t port_num;
	ib_api_status_t status;
	int device_fd;
	char device_file[16];
	osm_ts_user_mad_filter filter;
	int ts_ioctl_ret;
	int qpn;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_mad_pool);

	port_guid = p_user_bind->port_guid;

	osm_log(p_vend->p_log, OSM_LOG_INFO,
		"osm_vendor_bind: "
		"Binding to port 0x%" PRIx64 ".\n", cl_ntoh64(port_guid));

	switch (p_user_bind->mad_class) {
	case IB_MCLASS_SUBN_LID:
	case IB_MCLASS_SUBN_DIR:
		p_bind = &(p_vend->smi_bind);
		qpn = 0;
		break;

	case IB_MCLASS_SUBN_ADM:
	default:
		p_bind = &(p_vend->gsi_bind);
		qpn = 1;
		break;
	}

	/* Make sure we did not previously opened the file */
	if (p_bind->ul_dev_fd >= 0) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 5004: "
			"Already binded to port %u\n", p_bind->port_num);
		goto Exit;
	}

	/*
	   We need to figure out what is the TS file name to attach to.
	   I guess it is following the index of the port in the table of
	   ports.
	 */

	/* obtain the hca name and port num from the guid */
	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"osm_vendor_bind: "
		"Finding CA and Port that owns port guid 0x%" PRIx64 ".\n",
		cl_ntoh64(port_guid));
	status =
	    osm_vendor_get_guid_ca_and_port(p_vend, port_guid, &hca_hndl,
					    &hca_id, &port_num);
	if (status != IB_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 5005: "
			"Fail to find port number of port guid:0x%016" PRIx64
			"\n", port_guid);
		goto Exit;
	}

	/* the file name is just /dev/ts_ua0: */
	strcpy(device_file, "/dev/ts_ua0");

	osm_log(p_vend->p_log, OSM_LOG_ERROR,
		"osm_vendor_bind: " "Opening TS UL dev file:%s\n", device_file);

	/* Open the file ... */
	device_fd = open(device_file, O_RDWR);
	if (device_fd < 0) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 5006: "
			"Fail to open TS UL dev file:%s\n", device_file);
		goto Exit;
	}

	/* track this bind request info */
	p_bind->ul_dev_fd = device_fd;
	p_bind->port_num = port_num;
	p_bind->p_vend = p_vend;
	p_bind->client_context = context;
	p_bind->rcv_callback = mad_recv_callback;
	p_bind->send_err_callback = send_err_callback;
	p_bind->p_osm_pool = p_mad_pool;
	p_bind->hca_hndl = hca_hndl;

	/*
	 * Create the MAD filter on this file handle.
	 */
	filter.port = port_num;

	filter.qpn = qpn;
	filter.mgmt_class = p_user_bind->mad_class;
	filter.direction = TS_IB_MAD_DIRECTION_IN;
	filter.mask =
	    TS_IB_MAD_FILTER_DIRECTION |
	    TS_IB_MAD_FILTER_PORT |
	    TS_IB_MAD_FILTER_QPN | TS_IB_MAD_FILTER_MGMT_CLASS;

	ts_ioctl_ret = ioctl(device_fd, TS_IB_IOCSMADFILTADD, &filter);
	if (ts_ioctl_ret < 0) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 5014: "
			"Fail to register MAD filter with err:%u\n",
			ts_ioctl_ret);
		goto Exit;
	}

	/* Initialize the listener thread for this port */
	status = cl_thread_init(&p_bind->poller,
				__osm_vendor_ts_poller, p_bind,
				"osm ts poller");
	if (status != IB_SUCCESS)
		goto Exit;

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return ((osm_bind_handle_t) p_bind);
}

/**********************************************************************
Get a mad from the lower level.
The osm_vend_wrap_t is a wrapper used to connect the mad to the response.
**********************************************************************/
ib_mad_t *osm_vendor_get(IN osm_bind_handle_t h_bind,
			 IN const uint32_t mad_size,
			 IN osm_vend_wrap_t * const p_vw)
{
	ib_mad_t *p_mad;
	osm_ts_bind_info_t *p_bind = (osm_ts_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vw);

	p_vw->size = mad_size;

	/* allocate it */
	p_mad = (ib_mad_t *) malloc(p_vw->size);
	if (p_mad == NULL) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_get: ERR 5022: "
			"Error Obtaining MAD buffer.\n");
		goto Exit;
	}

	memset(p_mad, 0, p_vw->size);

	/* track locally */
	p_vw->p_mad_buf = p_mad;
	p_vw->h_bind = h_bind;
	p_vw->p_resp_madw = NULL;

	if (osm_log_get_level(p_vend->p_log) >= OSM_LOG_DEBUG) {
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"osm_vendor_get: "
			"Acquired MAD %p, size = %u.\n", p_mad, p_vw->size);
	}

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (p_mad);
}

/**********************************************************************
 * Return a MAD by providing it's wrapper object.
 **********************************************************************/
void
osm_vendor_put(IN osm_bind_handle_t h_bind, IN osm_vend_wrap_t * const p_vw)
{
	osm_ts_bind_info_t *p_bind = (osm_ts_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;
	osm_madw_t *p_madw;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vw);
	CL_ASSERT(p_vw->p_mad_buf);

	if (osm_log_get_level(p_vend->p_log) >= OSM_LOG_DEBUG) {
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"osm_vendor_put: " "Retiring MAD %p.\n",
			p_vw->p_mad_buf);
	}

	/*
	 * We moved the removal of the transaction to immediatly after
	 * it was looked up.
	 */

	/* free the mad but the wrapper is part of the madw object */
	free(p_vw->p_mad_buf);
	p_vw->p_mad_buf = NULL;
	p_madw = PARENT_STRUCT(p_vw, osm_madw_t, vend_wrap);
	p_madw->p_mad = NULL;

	OSM_LOG_EXIT(p_vend->p_log);
}

/**********************************************************************
Actually Send a MAD

MADs are buffers of type: struct ib_mad - so they are limited by size.
This is for internal use by osm_vendor_send and the transaction mgr
retry too.
**********************************************************************/
ib_api_status_t
osm_ts_send_mad(IN osm_ts_bind_info_t * p_bind, IN osm_madw_t * const p_madw)
{
	osm_vendor_t *const p_vend = p_bind->p_vend;
	osm_mad_addr_t *const p_mad_addr = osm_madw_get_mad_addr_ptr(p_madw);
	ib_mad_t *const p_mad = osm_madw_get_mad_ptr(p_madw);
	struct ib_mad ts_mad;
	int ret;
	ib_api_status_t status;

	OSM_LOG_ENTER(p_vend->p_log);

	/*
	 * Copy the MAD over to the sent mad
	 */
	memcpy(&ts_mad, p_mad, 256);

	/*
	 * For all sends other than directed route SM MADs,
	 * acquire an address vector for the destination.
	 */
	if (p_mad->mgmt_class != IB_MCLASS_SUBN_DIR) {
		__osm_ts_conv_osm_addr_to_ts_addr(p_mad_addr,
						  p_mad->mgmt_class ==
						  IB_MCLASS_SUBN_LID, &ts_mad);
	} else {
		/* is a directed route - we need to construct a permissive address */
		/* we do not need port number since it is part of the mad_hndl */
		ts_mad.dlid = IB_LID_PERMISSIVE;
		ts_mad.slid = IB_LID_PERMISSIVE;
	}
	if ((p_mad->mgmt_class == IB_MCLASS_SUBN_DIR) ||
	    (p_mad->mgmt_class == IB_MCLASS_SUBN_LID)) {
		ts_mad.sqpn = 0;
		ts_mad.dqpn = 0;
	} else {
		ts_mad.sqpn = 1;
		ts_mad.dqpn = 1;
	}
	ts_mad.port = p_bind->port_num;

	/* send it */
	ret = write(p_bind->ul_dev_fd, &ts_mad, sizeof(ts_mad));

	if (ret != sizeof(ts_mad)) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_ts_send_mad: ERR 5026: "
			"Error sending mad (%d).\n", ret);
		status = IB_ERROR;
		goto Exit;
	}

	status = IB_SUCCESS;

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

/**********************************************************************
Send a MAD through.

What is unclear to me is the need for the setting of all the MAD Wrapper
fields. Seems like the OSM uses these values during it's processing...
**********************************************************************/
ib_api_status_t
osm_vendor_send(IN osm_bind_handle_t h_bind,
		IN osm_madw_t * const p_madw, IN boolean_t const resp_expected)
{
	osm_ts_bind_info_t *p_bind = (osm_ts_bind_info_t *) h_bind;
	osm_vendor_t *const p_vend = p_bind->p_vend;
	osm_vend_wrap_t *const p_vw = osm_madw_get_vend_ptr(p_madw);
	ib_api_status_t status;

	OSM_LOG_ENTER(p_vend->p_log);

	/*
	 * If a response is expected to this MAD, then preallocate
	 * a mad wrapper to contain the wire MAD received in the
	 * response.  Allocating a wrapper here allows for easier
	 * failure paths than after we already received the wire mad.
	 */
	if (resp_expected == TRUE) {
		/* we track it in the vendor wrapper */
		p_vw->p_resp_madw =
		    osm_mad_pool_get_wrapper_raw(p_bind->p_osm_pool);
		if (p_vw->p_resp_madw == NULL) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osm_vendor_send: ERR 5024: "
				"Unable to allocate MAD wrapper.\n");
			status = IB_INSUFFICIENT_RESOURCES;
			goto Exit;
		}

		/* put some minimal info on that wrapper */
		((osm_madw_t *) (p_vw->p_resp_madw))->h_bind = h_bind;

		/* we also want to track it in the TID based map */
		status = osm_transaction_mgr_insert_madw((osm_bind_handle_t *)
							 p_bind, p_madw);
		if (status != IB_SUCCESS) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osm_vendor_send: ERR 5025: "
				"Error inserting request madw by TID (%d).\n",
				status);
		}
	} else
		p_vw->p_resp_madw = NULL;

	/* do the actual send */
	/* HACK: to be replaced by call to RMPP Segmentation */
	status = osm_ts_send_mad(p_bind, p_madw);

	/* we do not get an asycn callback so call it ourselves */
	/* this will handle all cleanup if neccessary */
	__osm_ts_send_callback(p_bind, !resp_expected, p_madw, status);

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

/**********************************************************************
 * the idea here is to change the content of the bind such that it
 * will hold the local address used for sending directed route by the SMA.
 **********************************************************************/
ib_api_status_t osm_vendor_local_lid_change(IN osm_bind_handle_t h_bind)
{
	osm_vendor_t *p_vend = ((osm_ts_bind_info_t *) h_bind)->p_vend;

	OSM_LOG_ENTER(p_vend->p_log);

	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"osm_vendor_local_lid_change: DEBUG 2202: " "Change of LID.\n");

	OSM_LOG_EXIT(p_vend->p_log);

	return (IB_SUCCESS);
}

void osm_vendor_set_sm(IN osm_bind_handle_t h_bind, IN boolean_t is_sm_val)
{
	osm_ts_bind_info_t *p_bind = (osm_ts_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;
	VAPI_ret_t status;
	VAPI_hca_attr_t attr_mod;
	VAPI_hca_attr_mask_t attr_mask;

	OSM_LOG_ENTER(p_vend->p_log);

	memset(&attr_mod, 0, sizeof(attr_mod));
	memset(&attr_mask, 0, sizeof(attr_mask));

	attr_mod.is_sm = is_sm_val;
	attr_mask = HCA_ATTR_IS_SM;

	status =
	    VAPI_modify_hca_attr(p_bind->hca_hndl, p_bind->port_num, &attr_mod,
				 &attr_mask);
	if (status != VAPI_OK) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_set_sm: ERR 5027: "
			"Unable set 'IS_SM' bit to:%u in port attributes (%d).\n",
			is_sm_val, status);
	}

	OSM_LOG_EXIT(p_vend->p_log);
}

void osm_vendor_set_debug(IN osm_vendor_t * const p_vend, IN int32_t level)
{

}
