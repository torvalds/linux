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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#ifdef OSM_VENDOR_INTF_MTL

#include <stdlib.h>
#include <string.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_log.h>
/* HACK - I do not know how to prevent complib from loading kernel H files */
#undef __init
#include <vendor/osm_vendor_mtl.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_opensm.h>
#include <vendor/osm_vendor_mtl_transaction_mgr.h>
#include <vendor/osm_mtl_bind.h>

/*
  Since a race can accure on requests. Meaning - a response is received before
  the send_callback is called - we will save both the madw_p and the fact
  whether or not it is a response. A race can occure only on requests that did
  not fail, and then the madw_p will be put back in the pool before the callback.
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
 * IB_MGT to OSM ADDRESS VECTOR
 **********************************************************************/
void
__osm_mtl_conv_ibmgt_rcv_desc_to_osm_addr(IN osm_vendor_t * const p_vend,
					  IN IB_MGT_mad_rcv_desc_t * p_rcv_desc,
					  IN uint8_t is_smi,
					  OUT osm_mad_addr_t * p_mad_addr)
{
	/*  p_mad_addr->dest_lid = p_osm->subn.sm_base_lid; - for resp we use the dest lid ... */
	p_mad_addr->dest_lid = cl_hton16(p_rcv_desc->remote_lid);
	p_mad_addr->static_rate = 0;	/*  HACK - we do not  know the rate ! */
	p_mad_addr->path_bits = p_rcv_desc->local_path_bits;
	if (is_smi) {
		/* SMI */
		p_mad_addr->addr_type.smi.source_lid =
		    cl_hton16(p_rcv_desc->remote_lid);
		p_mad_addr->addr_type.smi.port_num = 99;	/*  HACK - if used - should fail */
	} else {
		/* GSI */
		/* seems to me there is a IBMGT bug reversing the QPN ... */
		/* Does IBMGT supposed to provide the QPN is network or HOST ? */
		p_mad_addr->addr_type.gsi.remote_qp = cl_hton32(p_rcv_desc->qp);

		p_mad_addr->addr_type.gsi.remote_qkey = IB_QP1_WELL_KNOWN_Q_KEY;
		/*  we do have the p_mad_addr->pkey_ix but how to get the PKey by index ? */
		/*  the only way seems to be to use VAPI_query_hca_pkey_tbl and obtain */
		/*  the full PKey table - than go by the index. */
		/*  since this does not seem reasonable to me I simply use the default */
		/*  There is a TAVOR limitation that only one P_KEY is supported per  */
		/*  QP - so QP1 must use IB_DEFAULT_PKEY */
		p_mad_addr->addr_type.gsi.pkey_ix = 0;
		p_mad_addr->addr_type.gsi.service_level = p_rcv_desc->sl;

		p_mad_addr->addr_type.gsi.global_route = p_rcv_desc->grh_flag;
		/* copy the GRH data if relevant */
		if (p_mad_addr->addr_type.gsi.global_route) {
			p_mad_addr->addr_type.gsi.grh_info.ver_class_flow =
			    ib_grh_set_ver_class_flow(p_rcv_desc->grh.
						      IP_version,
						      p_rcv_desc->grh.
						      traffic_class,
						      p_rcv_desc->grh.
						      flow_label);
			p_mad_addr->addr_type.gsi.grh_info.hop_limit =
			    p_rcv_desc->grh.hop_limit;
			memcpy(&p_mad_addr->addr_type.gsi.grh_info.src_gid.raw,
			       &p_rcv_desc->grh.sgid, sizeof(ib_net64_t));
			memcpy(&p_mad_addr->addr_type.gsi.grh_info.dest_gid.raw,
			       p_rcv_desc->grh.dgid, sizeof(ib_net64_t));
		}
	}
}

/**********************************************************************
 * OSM ADDR VECTOR TO IB_MGT
 **********************************************************************/
void
__osm_mtl_conv_osm_addr_to_ibmgt_addr(IN osm_mad_addr_t * p_mad_addr,
				      IN uint8_t is_smi, OUT IB_ud_av_t * p_av)
{

	/* For global destination or Multicast address: */
	u_int8_t ver;

	memset(p_av, 0, sizeof(IB_ud_av_t));

	p_av->src_path_bits = p_mad_addr->path_bits;
	p_av->static_rate = p_mad_addr->static_rate;
	p_av->dlid = cl_ntoh16(p_mad_addr->dest_lid);

	if (is_smi) {
		p_av->sl = 0;	/*  Just to note we use 0 here. */
	} else {
		p_av->sl = p_mad_addr->addr_type.gsi.service_level;
		p_av->grh_flag = p_mad_addr->addr_type.gsi.global_route;

		if (p_mad_addr->addr_type.gsi.global_route) {
			ib_grh_get_ver_class_flow(p_mad_addr->addr_type.gsi.
						  grh_info.ver_class_flow, &ver,
						  &p_av->traffic_class,
						  &p_av->flow_label);
			p_av->hop_limit =
			    p_mad_addr->addr_type.gsi.grh_info.hop_limit;
			p_av->sgid_index = 0;	/*  we always use source GID 0 */
			memcpy(&p_av->dgid,
			       &p_mad_addr->addr_type.gsi.grh_info.dest_gid.raw,
			       sizeof(ib_net64_t));

		}
	}
}

void __osm_vendor_clear_sm(IN osm_bind_handle_t h_bind)
{
	osm_mtl_bind_info_t *p_bind = (osm_mtl_bind_info_t *) h_bind;
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
			"__osm_vendor_clear_sm: ERR 3C21: "
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
}

/**********************************************************************
 * DEALOCATE osm_vendor_t
 **********************************************************************/
void osm_vendor_destroy(IN osm_vendor_t * const p_vend)
{
	osm_vendor_mgt_bind_t *vendor_mgt_bind_p;
	IB_MGT_ret_t mgt_ret;
	OSM_LOG_ENTER(p_vend->p_log);

	if (p_vend->h_al != NULL) {
		vendor_mgt_bind_p = (osm_vendor_mgt_bind_t *) p_vend->h_al;
		if (vendor_mgt_bind_p->gsi_init) {

			/* un register the class */
			/* HACK WE ASSUME WE ONLY GOT SA CLASS REGISTERD ON GSI !!! */
			mgt_ret =
			    IB_MGT_unbind_gsi_class(vendor_mgt_bind_p->
						    gsi_mads_hdl,
						    IB_MCLASS_SUBN_ADM);
			if (mgt_ret != IB_MGT_OK) {
				osm_log(p_vend->p_log, OSM_LOG_ERROR,
					"osm_vendor_destroy: ERR 3C03: "
					"Fail to unbind the SA class.\n");
			}

			/* un bind the handle */
			if (IB_MGT_release_handle
			    (vendor_mgt_bind_p->gsi_mads_hdl) != IB_MGT_OK) {
				osm_log(p_vend->p_log, OSM_LOG_ERROR,
					"osm_vendor_destroy: ERR 3C02: "
					"Fail to unbind the SA GSI handle.\n");
			}
			osm_log(p_vend->p_log, OSM_LOG_DEBUG,
				"osm_vendor_destroy: DBG 1002: "
				"Unbind the GSI handles.\n");
		}
		if (vendor_mgt_bind_p->smi_init) {
			/* first - clear the IS_SM in the capability mask */
			__osm_vendor_clear_sm((osm_bind_handle_t)
					      (vendor_mgt_bind_p->smi_p_bind));

			/* un register the class */
			mgt_ret =
			    IB_MGT_unbind_sm(vendor_mgt_bind_p->smi_mads_hdl);
			if (mgt_ret != IB_MGT_OK) {
				osm_log(p_vend->p_log, OSM_LOG_ERROR,
					"osm_vendor_destroy: ERR 3C04: "
					"Fail to unbind the SM class.\n");
			}

			/* un bind the handle */
			if (IB_MGT_release_handle
			    (vendor_mgt_bind_p->smi_mads_hdl) != IB_MGT_OK) {
				osm_log(p_vend->p_log, OSM_LOG_ERROR,
					"osm_vendor_destroy: ERR 3C05: "
					"Fail to unbind the SMI handle.\n");
			}
			osm_log(p_vend->p_log, OSM_LOG_DEBUG,
				"osm_vendor_destroy: DBG 1003: "
				"Unbind the SMI handles.\n");

		}
	}
	osm_transaction_mgr_destroy(p_vend);
	/*  __osm_mtl_destroy_tid_mad_map( p_vend ); */
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
 * This proc actuall binds the handle to the lower level.
 *
 * We might have here as a result a casting of our struct to the ib_al_handle_t
 *
 * Q: Do we need 2 of those - one for MSI and one for GSI ?
 * A: Yes! We should be able to do the SA too. So we need a struct!
 *
 **********************************************************************/

ib_api_status_t
osm_vendor_init(IN osm_vendor_t * const p_vend,
		IN osm_log_t * const p_log, IN const uint32_t timeout)
{
	osm_vendor_mgt_bind_t *ib_mgt_hdl_p;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_log);

	p_vend->p_log = p_log;

	/*
	 * HACK: We need no handle. Assuming the driver is up.
	 */
	ib_mgt_hdl_p = (osm_vendor_mgt_bind_t *)
	    malloc(sizeof(osm_vendor_mgt_bind_t));
	if (ib_mgt_hdl_p == NULL) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_init: ERR 3C06: "
			"Fail to allocate vendor mgt handle.\n");
		goto Exit;
	}

	ib_mgt_hdl_p->smi_init = FALSE;
	ib_mgt_hdl_p->gsi_init = FALSE;
	/* cast it into the ib_al_handle_t h_al */
	p_vend->h_al = (ib_al_handle_t) ib_mgt_hdl_p;
	p_vend->p_transaction_mgr = NULL;
	osm_transaction_mgr_init(p_vend);
	/*  p_vend->madw_by_tid_map_p = NULL; */
	/*  __osm_mtl_init_tid_mad_map( p_vend ); */
	p_vend->timeout = timeout;

Exit:
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
			"osm_vendor_new: ERR 3C07: "
			"Fail to allocate vendor object.\n");
	}

	OSM_LOG_EXIT(p_log);
	return (p_vend);
}

/**********************************************************************
 * IB_MGT RCV callback
 *
 **********************************************************************/
void
__osm_mtl_rcv_callback(IN IB_MGT_mad_hndl_t mad_hndl,
		       IN void *private_ctx_p,
		       IN void *payload_p,
		       IN IB_MGT_mad_rcv_desc_t * rcv_remote_info_p)
{
	IB_MGT_ret_t status;
	osm_mtl_bind_info_t *bind_info_p = private_ctx_p;
	osm_madw_t *req_madw_p = NULL;
	osm_madw_t *madw_p;
	osm_vend_wrap_t *p_new_vw;
	osm_mad_addr_t mad_addr;
	ib_mad_t *mad_buf_p;
	osm_log_t *const p_log = bind_info_p->p_vend->p_log;

	OSM_LOG_ENTER(p_log);

	/* if it is a response MAD we mustbe able to get the request */
	if (ib_mad_is_response((ib_mad_t *) payload_p)) {
		/* can we find a matching madw by this payload TID */
		status =
		    osm_transaction_mgr_get_madw_for_tid(bind_info_p->p_vend,
							 (ib_mad_t *) payload_p,
							 &req_madw_p);
		if (status != IB_MGT_OK) {
			osm_log(p_log, OSM_LOG_ERROR,
				"__osm_mtl_rcv_callback: ERR 3C08: "
				"Error obtaining request madw by TID (%d).\n",
				status);
			req_madw_p = NULL;
		}

		if (req_madw_p == NULL) {
			osm_log(p_log, OSM_LOG_ERROR,
				"__osm_mtl_rcv_callback: ERR 3C09:  "
				"Fail to obtain request madw for received MAD.(method=%X attr=%X) Aborting CB.\n",
				((ib_mad_t *) payload_p)->method,
				cl_ntoh16(((ib_mad_t *) payload_p)->attr_id)

			    );
			goto Exit;
		}
	}

	/* do we have a request ??? */
	if (req_madw_p == NULL) {

		/* first arrange an address */
		__osm_mtl_conv_ibmgt_rcv_desc_to_osm_addr(bind_info_p->p_vend,
							  rcv_remote_info_p,
							  (((ib_mad_t *)
							    payload_p)->
							   mgmt_class ==
							   IB_MCLASS_SUBN_LID)
							  || (((ib_mad_t *)
							       payload_p)->
							      mgmt_class ==
							      IB_MCLASS_SUBN_DIR),
							  &mad_addr);

		osm_log(p_log, OSM_LOG_ERROR,
			"__osm_mtl_rcv_callback: : "
			"Received MAD from QP:%X.\n",
			cl_ntoh32(mad_addr.addr_type.gsi.remote_qp)
		    );

		/* if not - get new osm_madw and arrange it. */
		/* create the new madw in the pool */
		madw_p = osm_mad_pool_get(bind_info_p->p_osm_pool,
					  (osm_bind_handle_t) bind_info_p,
					  MAD_BLOCK_SIZE, &mad_addr);
		if (madw_p == NULL) {
			osm_log(p_log, OSM_LOG_ERROR,
				"__osm_mtl_rcv_callback: ERR 3C10: "
				"Error request for a new madw.\n");
			goto Exit;
		}
		/* HACK: we cust to avoid the const ??? */
		mad_buf_p = (void *)madw_p->p_mad;
	} else {
		/* we have the madw defined during the send and stored in the vend_wrap */
		/* we need to make sure the wrapper is correctly init there */
		CL_ASSERT(req_madw_p->vend_wrap.p_resp_madw != 0);
		madw_p = req_madw_p->vend_wrap.p_resp_madw;

		/* HACK: we do not Support RMPP */
		CL_ASSERT(madw_p->h_bind);
		mad_buf_p =
		    osm_vendor_get(madw_p->h_bind, MAD_BLOCK_SIZE,
				   &madw_p->vend_wrap);

		if (mad_buf_p == NULL) {
			osm_log(p_log, OSM_LOG_ERROR,
				"__osm_mtl_rcv_callback: ERR 3C11: "
				"Unable to acquire wire MAD.\n");

			goto Exit;
		}

		/*
		   Finally, attach the wire MAD to this wrapper.
		 */
		osm_madw_set_mad(madw_p, mad_buf_p);

		/* also we need to handle the size of the mad since we did not init ... */
		madw_p->mad_size = MAD_BLOCK_SIZE;
	}

	/* init some fields of the vendor wrapper */
	p_new_vw = osm_madw_get_vend_ptr(madw_p);
	p_new_vw->h_bind = bind_info_p;
	p_new_vw->size = MAD_BLOCK_SIZE;
	p_new_vw->p_resp_madw = NULL;
	p_new_vw->mad_buf_p = mad_buf_p;

	/* HACK: We do not support RMPP in receiving MADS */
	memcpy(p_new_vw->mad_buf_p, payload_p, MAD_BLOCK_SIZE);

	/* attach the buffer to the wrapper */
	madw_p->p_mad = mad_buf_p;

	/* we can also make sure we marked the size and bind on the returned madw */
	madw_p->h_bind = p_new_vw->h_bind;

	/* call the CB */
	(*bind_info_p->rcv_callback) (madw_p, bind_info_p->client_context,
				      req_madw_p);

Exit:
	OSM_LOG_EXIT(p_log);
}

/**********************************************************************
 * IB_MGT Send callback : invoked after each send
 *
 **********************************************************************/
void
__osm_mtl_send_callback(IN IB_MGT_mad_hndl_t mad_hndl,
			IN u_int64_t wrid,
			IN IB_comp_status_t status, IN void *private_ctx_p)
{
	osm_madw_t *madw_p;
	osm_mtl_bind_info_t *bind_info_p =
	    (osm_mtl_bind_info_t *) private_ctx_p;
	osm_log_t *const p_log = bind_info_p->p_vend->p_log;
	osm_vend_wrap_t *p_vw;
	uint8_t is_resp;

	OSM_LOG_ENTER(p_log);

	/* obtain the madp from the wrid */
	__osm_set_p_madw_and_resp_by_wrid(wrid, &is_resp, &madw_p);

	osm_log(p_log, OSM_LOG_DEBUG,
		"__osm_mtl_send_callback: INFO 1008: "
		"Handling Send of MADW:%p Is Resp:%d.\n", madw_p, is_resp);

	/* we need to handle requests and responses differently */
	if (is_resp) {
		if (status != IB_COMP_SUCCESS) {
			osm_log(p_log, OSM_LOG_ERROR,
				"__osm_mtl_send_callback: ERR 3C12: "
				"Error Sending Response MADW:%p.\n", madw_p);
		} else {
			osm_log(p_log, OSM_LOG_DEBUG,
				"__osm_mtl_send_callback: DBG 1008: "
				"Completed Sending Response MADW:%p.\n",
				madw_p);
		}

		/* if we are a response - we need to clean it up */
		osm_mad_pool_put(bind_info_p->p_osm_pool, madw_p);
	} else {

		/* this call back is invoked on completion of send - error or not */
		if (status != IB_COMP_SUCCESS) {

			osm_log(p_log, OSM_LOG_ERROR,
				"__osm_mtl_send_callback: ERR 3C13: "
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
			(*bind_info_p->send_err_callback) (bind_info_p->
							   client_context,
							   madw_p);
		} else {
			/* successful request send - do nothing - the response will need the
			   out mad */
			osm_log(p_log, OSM_LOG_DEBUG,
				"__osm_mtl_send_callback: DBG 1008: "
				"Completed Sending Request MADW:%p.\n", madw_p);
		}
	}

	OSM_LOG_EXIT(p_log);
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
	osm_mtl_bind_info_t *p_bind = NULL;
	VAPI_hca_hndl_t hca_hndl;
	VAPI_hca_id_t hca_id;
	IB_MGT_mad_type_t mad_type;
	uint32_t port_num;
	osm_vendor_mgt_bind_t *ib_mgt_hdl_p;
	IB_MGT_ret_t mgt_ret;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_user_bind);
	CL_ASSERT(p_mad_pool);
	CL_ASSERT(mad_recv_callback);
	CL_ASSERT(send_err_callback);

	/* cast back the AL handle to vendor mgt bind */
	ib_mgt_hdl_p = (osm_vendor_mgt_bind_t *) p_vend->h_al;

	port_guid = p_user_bind->port_guid;

	osm_log(p_vend->p_log, OSM_LOG_INFO,
		"osm_vendor_bind: "
		"Binding to port 0x%" PRIx64 ".\n", cl_ntoh64(port_guid));

	/* obtain the hca name and port num from the guid */
	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"osm_vendor_bind: "
		"Finding CA and Port that owns port guid 0x%" PRIx64 ".\n",
		port_guid);

	mgt_ret =
	    osm_vendor_get_guid_ca_and_port(p_vend, port_guid, &hca_hndl,
					    &hca_id, &port_num);
	if (mgt_ret != IB_MGT_OK) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 3C14: "
			"Unable to obtain CA and port (%d).\n");
		goto Exit;
	}

	/* create the bind object tracking this binding */
	p_bind = (osm_mtl_bind_info_t *) malloc(sizeof(osm_mtl_bind_info_t));
	memset(p_bind, 0, sizeof(osm_mtl_bind_info_t));
	if (p_bind == NULL) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 3C15: "
			"Unable to allocate internal bind object.\n");
		goto Exit;
	}

	/* track this bind request info */
	memcpy(p_bind->hca_id, hca_id, sizeof(VAPI_hca_id_t));
	p_bind->port_num = port_num;
	p_bind->p_vend = p_vend;
	p_bind->client_context = context;
	p_bind->rcv_callback = mad_recv_callback;
	p_bind->send_err_callback = send_err_callback;
	p_bind->p_osm_pool = p_mad_pool;

	CL_ASSERT(p_bind->port_num);

	/*
	 * Get the proper CLASS
	 */

	switch (p_user_bind->mad_class) {
	case IB_MCLASS_SUBN_LID:
	case IB_MCLASS_SUBN_DIR:
		mad_type = IB_MGT_SMI;
		break;

	case IB_MCLASS_SUBN_ADM:
	default:
		mad_type = IB_MGT_GSI;
		break;
	}

	/* we split here - based on the type of MADS GSI / SMI */
	/* HACK: we only support one class registration per SMI/GSI !!! */
	if (mad_type == IB_MGT_SMI) {
		/*
		 *  SMI CASE
		 */

		/* we do not need to bind the handle if already available */
		if (ib_mgt_hdl_p->smi_init == FALSE) {

			/* First we have to reg and get the handle for the mad */
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osm_vendor_bind: "
				"Binding to IB_MGT SMI of %s port %u\n", hca_id,
				port_num);

			mgt_ret =
			    IB_MGT_get_handle(hca_id, port_num, IB_MGT_SMI,
					      &(ib_mgt_hdl_p->smi_mads_hdl));
			if (IB_MGT_OK != mgt_ret) {
				free(p_bind);
				p_bind = NULL;
				osm_log(p_vend->p_log, OSM_LOG_ERROR,
					"osm_vendor_bind: ERR 3C16: "
					"Error obtaining IB_MGT handle to SMI.\n");
				goto Exit;
			}

			/* bind it */
			mgt_ret = IB_MGT_bind_sm(ib_mgt_hdl_p->smi_mads_hdl);
			if (IB_MGT_OK != mgt_ret) {
				free(p_bind);
				p_bind = NULL;
				osm_log(p_vend->p_log, OSM_LOG_ERROR,
					"osm_vendor_bind: ERR 3C17: "
					"Error binding IB_MGT handle to SM.\n");
				goto Exit;
			}

			ib_mgt_hdl_p->smi_init = TRUE;

		}

		/* attach to this bind info */
		p_bind->mad_hndl = ib_mgt_hdl_p->smi_mads_hdl;
		ib_mgt_hdl_p->smi_p_bind = p_bind;

		/* now register the callback */
		mgt_ret = IB_MGT_reg_cb(p_bind->mad_hndl,
					&__osm_mtl_rcv_callback,
					p_bind,
					&__osm_mtl_send_callback,
					p_bind,
					IB_MGT_RCV_CB_MASK |
					IB_MGT_SEND_CB_MASK);

	} else {
		/*
		 *  GSI CASE
		 */

		if (ib_mgt_hdl_p->gsi_init == FALSE) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osm_vendor_bind: " "Binding to IB_MGT GSI\n");

			/* First we have to reg and get the handle for the mad */
			mgt_ret =
			    IB_MGT_get_handle(hca_id, port_num, IB_MGT_GSI,
					      &(ib_mgt_hdl_p->gsi_mads_hdl));
			if (IB_MGT_OK != mgt_ret) {
				free(p_bind);
				p_bind = NULL;
				osm_log(p_vend->p_log, OSM_LOG_ERROR,
					"osm_vendor_bind: ERR 3C20: "
					"Error obtaining IB_MGT handle to GSI.\n");
				goto Exit;
			}

			/* bind it */
			mgt_ret =
			    IB_MGT_bind_gsi_class(ib_mgt_hdl_p->gsi_mads_hdl,
						  p_user_bind->mad_class);
			if (IB_MGT_OK != mgt_ret) {
				free(p_bind);
				p_bind = NULL;
				osm_log(p_vend->p_log, OSM_LOG_ERROR,
					"osm_vendor_bind: ERR 3C22: "
					"Error binding IB_MGT handle to GSI.\n");
				goto Exit;
			}

			ib_mgt_hdl_p->gsi_init = TRUE;

			/* attach to this bind info */
			p_bind->mad_hndl = ib_mgt_hdl_p->gsi_mads_hdl;

			/* now register the callback */
			mgt_ret = IB_MGT_reg_cb(p_bind->mad_hndl,
						&__osm_mtl_rcv_callback,
						p_bind,
						&__osm_mtl_send_callback,
						p_bind,
						IB_MGT_RCV_CB_MASK |
						IB_MGT_SEND_CB_MASK);

		} else {
			/* we can use the existing handle */
			p_bind->mad_hndl = ib_mgt_hdl_p->gsi_mads_hdl;
			mgt_ret = IB_MGT_OK;
		}

	}

	if (IB_MGT_OK != mgt_ret) {
		free(p_bind);
		p_bind = NULL;
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 3C23: "
			"Error binding IB_MGT CB (%d).\n", mgt_ret);
		goto Exit;
	}

	/* HACK: Do we need to initialize an address vector ???? */

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
	ib_mad_t *mad_p;
	osm_mtl_bind_info_t *p_bind = (osm_mtl_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vw);
	/* HACK: We know we can not send through IB_MGT */
	CL_ASSERT(mad_size <= MAD_BLOCK_SIZE);

	/* IB_MGT assumes it is 256 - we must follow */
	p_vw->size = MAD_BLOCK_SIZE;

	/* allocate it */
	mad_p = (ib_mad_t *) malloc(p_vw->size);
	if (mad_p == NULL) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_get: ERR 3C24: "
			"Error Obtaining MAD buffer.\n");
		goto Exit;
	}

	memset(mad_p, 0, p_vw->size);

	/* track locally */
	p_vw->mad_buf_p = mad_p;
	p_vw->h_bind = h_bind;
	p_vw->p_resp_madw = NULL;

	if (osm_log_get_level(p_vend->p_log) >= OSM_LOG_DEBUG) {
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"osm_vendor_get: "
			"Acquired MAD %p, size = %u.\n", mad_p, p_vw->size);
	}

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (mad_p);
}

/**********************************************************************
 * Return a MAD by providing it's wrapper object.
 **********************************************************************/
void
osm_vendor_put(IN osm_bind_handle_t h_bind, IN osm_vend_wrap_t * const p_vw)
{
	osm_mtl_bind_info_t *p_bind = (osm_mtl_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;
	osm_madw_t *p_madw;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vw);
	CL_ASSERT(p_vw->mad_buf_p);

	if (osm_log_get_level(p_vend->p_log) >= OSM_LOG_DEBUG) {
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"osm_vendor_put: " "Retiring MAD %p.\n",
			p_vw->mad_buf_p);
	}

	/*
	 * We moved the removal of the transaction to immediatly after
	 * it was looked up.
	 */

	/* free the mad but the wrapper is part of the madw object */
	free(p_vw->mad_buf_p);
	p_vw->mad_buf_p = NULL;
	p_madw = PARENT_STRUCT(p_vw, osm_madw_t, vend_wrap);
	p_madw->p_mad = NULL;

	OSM_LOG_EXIT(p_vend->p_log);
}

/**********************************************************************
Actually Send a MAD

This is for internal use by osm_vendor_send and the transaction mgr
retry too.
**********************************************************************/
ib_api_status_t
osm_mtl_send_mad(IN osm_mtl_bind_info_t * p_bind, IN osm_madw_t * const p_madw)
{
	osm_vendor_t *const p_vend = p_bind->p_vend;
	osm_vend_wrap_t *const p_vw = osm_madw_get_vend_ptr(p_madw);
	osm_mad_addr_t *const p_mad_addr = osm_madw_get_mad_addr_ptr(p_madw);
	ib_mad_t *const p_mad = osm_madw_get_mad_ptr(p_madw);
	ib_api_status_t status;
	IB_MGT_ret_t mgt_res;
	IB_ud_av_t av;
	uint64_t wrid;
	uint32_t qpn;

	OSM_LOG_ENTER(p_vend->p_log);

	/*
	 * For all sends other than directed route SM MADs,
	 * acquire an address vector for the destination.
	 */
	if (p_mad->mgmt_class != IB_MCLASS_SUBN_DIR) {
		__osm_mtl_conv_osm_addr_to_ibmgt_addr(p_mad_addr,
						      p_mad->mgmt_class ==
						      IB_MCLASS_SUBN_LID, &av);
	} else {
		/* is a directed route - we need to construct a permissive address */
		memset(&av, 0, sizeof(av));
		/* we do not need port number since it is part of the mad_hndl */
		av.dlid = IB_LID_PERMISSIVE;
	}

	wrid = __osm_set_wrid_by_p_madw(p_madw);

	/* send it */
	if ((p_mad->mgmt_class == IB_MCLASS_SUBN_DIR) ||
	    (p_mad->mgmt_class == IB_MCLASS_SUBN_LID)) {

		/* SMI CASE */
		if (osm_log_is_active(p_vend->p_log, OSM_LOG_DEBUG)) {
			osm_log(p_vend->p_log, OSM_LOG_DEBUG,
				"osm_mtl_send_mad: "
				"av.dlid 0x%X, "
				"av.static_rate %d, "
				"av.path_bits %d.\n",
				cl_ntoh16(av.dlid), av.static_rate,
				av.src_path_bits);
		}

		mgt_res = IB_MGT_send_mad(p_bind->mad_hndl, p_mad,	/*  actual payload */
					  &av,	/*  address vector */
					  wrid,	/*  casting the mad wrapper pointer for err cb */
					  p_vend->timeout);

	} else {
		/* GSI CASE - Support Remote QP */
		if (osm_log_is_active(p_vend->p_log, OSM_LOG_DEBUG)) {
			osm_log(p_vend->p_log, OSM_LOG_DEBUG,
				"osm_mtl_send_mad: "
				"av.dlid 0x%X, av.static_rate %d, "
				"av.path_bits %d, remote qp: 0x%06X \n",
				av.dlid,
				av.static_rate,
				av.src_path_bits,
				cl_ntoh32(p_mad_addr->addr_type.gsi.remote_qp)
			    );
		}

		/* IBMGT have a bug sending to a QP not 1 -
		   the QPN must be in network order except when it qpn 1 ... */
		qpn = cl_ntoh32(p_mad_addr->addr_type.gsi.remote_qp);

		mgt_res = IB_MGT_send_mad_to_qp(p_bind->mad_hndl, p_mad,	/*  actual payload */
						&av,	/* address vector */
						wrid,	/* casting the mad wrapper pointer for err cb */
						p_vend->timeout, qpn);
	}

	if (mgt_res != IB_MGT_OK) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_mtl_send_mad: ERR 3C26: "
			"Error sending mad (%d).\n", mgt_res);
		if (p_vw->p_resp_madw)
			osm_mad_pool_put(p_bind->p_osm_pool, p_vw->p_resp_madw);
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
	osm_mtl_bind_info_t *const p_bind = (osm_mtl_bind_info_t *) h_bind;
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
				"osm_vendor_send: ERR 3C27: "
				"Unable to allocate MAD wrapper.\n");
			status = IB_INSUFFICIENT_RESOURCES;
			goto Exit;
		}

		/* put some minimal info on that wrapper */
		((osm_madw_t *) (p_vw->p_resp_madw))->h_bind = h_bind;

		/* we also want to track it in the TID based map */
		status = osm_transaction_mgr_insert_madw((osm_bind_handle_t)
							 p_bind, p_madw);
		if (status != IB_SUCCESS) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osm_vendor_send: ERR 3C25: "
				"Error inserting request madw by TID (%d).\n",
				status);
		}

	} else
		p_vw->p_resp_madw = NULL;

	/* do the actual send */
	status = osm_mtl_send_mad(p_bind, p_madw);

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
	osm_vendor_t *p_vend = ((osm_mtl_bind_info_t *) h_bind)->p_vend;

	OSM_LOG_ENTER(p_vend->p_log);

	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"osm_vendor_local_lid_change: DEBUG 2202: " "Change of LID.\n");

	OSM_LOG_EXIT(p_vend->p_log);

	return (IB_SUCCESS);
}

void osm_vendor_set_sm(IN osm_bind_handle_t h_bind, IN boolean_t is_sm_val)
{
	osm_mtl_bind_info_t *p_bind = (osm_mtl_bind_info_t *) h_bind;
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
			"osm_vendor_set_sm: ERR 3C28: "
			"Unable set 'IS_SM' bit to:%u in port attributes (%d).\n",
			is_sm_val, status);
	}

	OSM_LOG_EXIT(p_vend->p_log);
}

void osm_vendor_set_debug(IN osm_vendor_t * const p_vend, IN int32_t level)
{

}

#endif				/* OSM_VENDOR_INTF_TEST */
