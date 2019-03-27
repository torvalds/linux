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

#include <stdlib.h>
#include <string.h>
#include <ib_mgt.h>
#include <complib/cl_event.h>
#include <vendor/osm_vendor_mlx_transport.h>
#include <vendor/osm_vendor_mlx_dispatcher.h>
#include <opensm/osm_log.h>

typedef struct _osmv_IBMGT_transport_mgr_ {
	IB_MGT_mad_type_t mad_type;
	uint8_t mgmt_class;	/* for gsi */
	/* for communication between send call back and send mad */
	boolean_t is_send_ok;
	cl_event_t send_done;
} osmv_IBMGT_transport_mgr_t;

typedef struct _osmv_IBMGT_transport_info_ {
	IB_MGT_mad_hndl_t smi_h;
	cl_qlist_t *p_smi_list;

	IB_MGT_mad_hndl_t gsi_h;
	/* holds bind object list for every binded mgmt class */
	cl_qlist_t *gsi_mgmt_lists[15];
} osmv_IBMGT_transport_info_t;

static void
__osmv_IBMGT_rcv_desc_to_osm_addr(IN IB_MGT_mad_rcv_desc_t * p_rcv_desc,
				  IN uint8_t is_smi,
				  OUT osm_mad_addr_t * p_mad_addr);

static void
__osmv_IBMGT_osm_addr_to_ibmgt_addr(IN const osm_mad_addr_t * p_mad_addr,
				    IN uint8_t is_smi, OUT IB_ud_av_t * p_av);

void
__osmv_IBMGT_send_cb(IN IB_MGT_mad_hndl_t mad_hndl,
		     IN u_int64_t wrid,
		     IN IB_comp_status_t status, IN void *private_ctx_p);

void
__osmv_IBMGT_rcv_cb(IN IB_MGT_mad_hndl_t mad_hndl,
		    IN void *private_ctx_p,
		    IN void *payload_p,
		    IN IB_MGT_mad_rcv_desc_t * rcv_remote_info_p);

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
	ib_api_status_t st = IB_SUCCESS;
	IB_MGT_ret_t ret;
	IB_MGT_mad_type_t mad_type;
	osmv_IBMGT_transport_mgr_t *p_mgr;
	osmv_IBMGT_transport_info_t *p_tpot_info;
	cl_list_obj_t *p_obj = NULL;
	osm_log_t *p_log = p_bo->p_vendor->p_log;
	int i;

	UNUSED_PARAM(hca_idx);

	/* if first bind, allocate tranport_info at vendor */
	if (NULL == p_bo->p_vendor->p_transport_info) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"osmv_transport_init: first bind() for the vendor\n");
		p_bo->p_vendor->p_transport_info
		    = (osmv_IBMGT_transport_info_t *)
		    malloc(sizeof(osmv_IBMGT_transport_info_t));
		if (NULL == p_bo->p_vendor->p_transport_info) {
			return IB_INSUFFICIENT_MEMORY;
		}
		memset(p_bo->p_vendor->p_transport_info, 0,
		       sizeof(osmv_IBMGT_transport_info_t));
		p_tpot_info =
		    (osmv_IBMGT_transport_info_t *) (p_bo->p_vendor->
						     p_transport_info);

		p_tpot_info->smi_h = 0xffffffff;
		p_tpot_info->p_smi_list = NULL;

		p_tpot_info->gsi_h = 0xffffffff;
		for (i = 0; i < 15; i++) {

			p_tpot_info->gsi_mgmt_lists[i] = NULL;
		}

	} else {

		p_tpot_info =
		    (osmv_IBMGT_transport_info_t *) (p_bo->p_vendor->
						     p_transport_info);
	}

	/* Initialize the magic_ptr to the pointer of the p_bo info.
	   This will be used to signal when the object is being destroyed, so no
	   real action will be done then. */
	p_bo->magic_ptr = p_bo;

	/* allocate transport mgr */
	p_mgr = malloc(sizeof(osmv_IBMGT_transport_mgr_t));
	if (NULL == p_mgr) {
		free(p_tpot_info);
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"osmv_transport_init: ERR 7201: " "alloc failed \n");
		return IB_INSUFFICIENT_MEMORY;
	}

	memset(p_mgr, 0, sizeof(osmv_IBMGT_transport_mgr_t));

	p_bo->p_transp_mgr = p_mgr;

	switch (p_info->mad_class) {
	case IB_MCLASS_SUBN_LID:
	case IB_MCLASS_SUBN_DIR:
		mad_type = IB_MGT_SMI;
		break;

	case IB_MCLASS_SUBN_ADM:
	default:
		mad_type = IB_MGT_GSI;
		break;
	}

	/* we only support one class registration per SMI/GSI !!! */
	switch (mad_type) {
	case IB_MGT_SMI:
		/* we do not need to bind the handle if already available */
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"osmv_transport_init: SMI bind\n");

		if (p_tpot_info->smi_h == 0xffffffff) {
			ret = IB_MGT_get_handle(hca_id,
						p_bo->port_num,
						IB_MGT_SMI,
						&(p_tpot_info->smi_h));
			if (IB_MGT_OK != ret) {
				osm_log(p_log, OSM_LOG_ERROR,
					"osmv_transport_init: ERR 7202: "
					"IB_MGT_get_handle for smi failed \n");
				st = IB_ERROR;
				free(p_mgr);
				goto Exit;
			}

			osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
				"osmv_transport_init: got smi handle:%d \n",
				p_tpot_info->smi_h);

			ret = IB_MGT_bind_sm(p_tpot_info->smi_h);
			if (IB_MGT_OK != ret) {
				osm_log(p_log, OSM_LOG_ERROR,
					"osmv_transport_init: ERR 7203: "
					"IB_MGT_bind_sm failed \n");
				st = IB_ERROR;
				free(p_mgr);
				goto Exit;
			}

			/* init smi list */
			p_tpot_info->p_smi_list = malloc(sizeof(cl_qlist_t));
			if (NULL == p_tpot_info->p_smi_list) {
				osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
					"osmv_transport_init: ERR 7204: "
					"alloc failed \n");
				IB_MGT_unbind_sm(p_tpot_info->smi_h);
				IB_MGT_release_handle(p_tpot_info->smi_h);
				free(p_mgr);
				return IB_INSUFFICIENT_MEMORY;
			}
			memset(p_tpot_info->p_smi_list, 0, sizeof(cl_qlist_t));
			cl_qlist_init(p_tpot_info->p_smi_list);

			osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
				"osmv_transport_init: before reg_cb\n");
			ret = IB_MGT_reg_cb(p_tpot_info->smi_h,
					    &__osmv_IBMGT_rcv_cb,
					    p_bo,
					    &__osmv_IBMGT_send_cb,
					    p_tpot_info->p_smi_list,
					    IB_MGT_RCV_CB_MASK |
					    IB_MGT_SEND_CB_MASK);
			if (ret != IB_SUCCESS) {
				osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
					"osmv_transport_init: ERR 7205: "
					"reg_cb failed with return code:%x \n",
					ret);
				IB_MGT_unbind_sm(p_tpot_info->smi_h);
				IB_MGT_release_handle(p_tpot_info->smi_h);
				free(p_tpot_info->p_smi_list);
				free(p_mgr);
				st = IB_ERROR;
				goto Exit;
			}

		}
		/* insert to list of smi's - for raising callbacks later on */
		p_obj = malloc(sizeof(cl_list_obj_t));
		if (p_obj)
			memset(p_obj, 0, sizeof(cl_list_obj_t));
		cl_qlist_set_obj(p_obj, p_bo);
		cl_qlist_insert_tail(p_tpot_info->p_smi_list,
				     &p_obj->list_item);

		break;

	case IB_MGT_GSI:
		/* we do not need to bind the handle if already available */
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
			"osmv_transport_init: ERR 7206: GSI bind\n");
		if (p_tpot_info->gsi_h == 0xffffffff) {
			ret = IB_MGT_get_handle(hca_id,
						p_bo->port_num,
						IB_MGT_GSI,
						&(p_tpot_info->gsi_h));
			if (IB_MGT_OK != ret) {
				osm_log(p_log, OSM_LOG_ERROR,
					"osmv_transport_init: ERR 7207: "
					"IB_MGT_get_handle for gsi failed \n");
				st = IB_ERROR;
				free(p_mgr);
				goto Exit;
			}
		}

		/* this mgmt class was not binded yet */
		if (p_tpot_info->gsi_mgmt_lists[p_info->mad_class] == NULL) {
			ret =
			    IB_MGT_bind_gsi_class(p_tpot_info->gsi_h,
						  p_info->mad_class);
			if (IB_MGT_OK != ret) {
				osm_log(p_log, OSM_LOG_ERROR,
					"osmv_transport_init: ERR 7208: "
					"IB_MGT_bind_gsi_class failed \n");
				st = IB_ERROR;
				free(p_mgr);
				goto Exit;
			}

			p_tpot_info->gsi_mgmt_lists[p_info->mad_class] =
			    malloc(sizeof(cl_qlist_t));
			if (NULL ==
			    p_tpot_info->gsi_mgmt_lists[p_info->mad_class]) {
				IB_MGT_unbind_gsi_class(p_tpot_info->gsi_h,
							p_info->mad_class);
				free(p_mgr);
				return IB_INSUFFICIENT_MEMORY;
			}
			memset(p_tpot_info->gsi_mgmt_lists[p_info->mad_class],
			       0, sizeof(cl_qlist_t));
			cl_qlist_init(p_tpot_info->
				      gsi_mgmt_lists[p_info->mad_class]);
		}
		/* insert to list of smi's - for raising callbacks later on */
		p_obj = malloc(sizeof(cl_list_obj_t));
		if (p_obj)
			memset(p_obj, 0, sizeof(cl_list_obj_t));
		cl_qlist_set_obj(p_obj, p_bo);
		cl_qlist_insert_tail(p_tpot_info->
				     gsi_mgmt_lists[p_info->mad_class],
				     &p_obj->list_item);

		p_mgr->mgmt_class = p_info->mad_class;
		ret = IB_MGT_reg_cb(p_tpot_info->gsi_h,
				    &__osmv_IBMGT_rcv_cb,
				    p_bo,
				    &__osmv_IBMGT_send_cb,
				    p_bo,
				    IB_MGT_RCV_CB_MASK | IB_MGT_SEND_CB_MASK);

		if (ret != IB_SUCCESS) {
			IB_MGT_unbind_gsi_class(p_tpot_info->gsi_h,
						p_mgr->mgmt_class);
			free(p_tpot_info->gsi_mgmt_lists[p_mgr->mgmt_class]);
			free(p_mgr);
			st = IB_ERROR;
			goto Exit;
		}

		break;

	default:
		osm_log(p_log, OSM_LOG_ERROR,
			"osmv_transport_init: ERR 7209: unrecognized mgmt class \n");
		st = IB_ERROR;
		free(p_mgr);
		goto Exit;
	}

	osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
		"osmv_transport_init: GSI bind\n");
	cl_event_construct(&p_mgr->send_done);
	cl_event_init(&p_mgr->send_done, TRUE);
	p_mgr->is_send_ok = FALSE;
	p_mgr->mad_type = mad_type;

Exit:
	/* OSM_LOG_EXIT(p_log ); */
	return (ib_api_status_t) st;
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
			IN void *p_ib_mad, IN const osm_mad_addr_t * p_mad_addr)
{

	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osmv_IBMGT_transport_info_t *p_tpot_info =
	    (osmv_IBMGT_transport_info_t *) (p_bo->p_vendor->p_transport_info);
	osm_vendor_t const *p_vend = p_bo->p_vendor;
	ib_api_status_t status;
	IB_ud_av_t av;
	IB_MGT_ret_t ret;
	ib_mad_t *p_mad = p_ib_mad;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_bo->p_vendor->p_transport_info);

	/*
	 * For all sends other than directed route SM MADs,
	 * acquire an address vector for the destination.
	 */
	if (p_mad->mgmt_class != IB_MCLASS_SUBN_DIR) {
		__osmv_IBMGT_osm_addr_to_ibmgt_addr(p_mad_addr,
						    p_mad->mgmt_class ==
						    IB_MCLASS_SUBN_LID, &av);
	} else {
		/* is a directed route - we need to construct a permissive address */
		memset(&av, 0, sizeof(av));
		/* we do not need port number since it is part of the mad_hndl */
		av.dlid = IB_LID_PERMISSIVE;
	}

	/* send it */
	if ((p_mad->mgmt_class == IB_MCLASS_SUBN_DIR) ||
	    (p_mad->mgmt_class == IB_MCLASS_SUBN_LID)) {

		/* SMI CASE */
		if (osm_log_is_active(p_vend->p_log, OSM_LOG_DEBUG)) {
			osm_log(p_vend->p_log, OSM_LOG_DEBUG,
				"osmv_transport_mad_send: "
				"av.dlid:0x%X, "
				"av.static_rate:%d, "
				"av.path_bits:%d.\n",
				cl_ntoh16(av.dlid), av.static_rate,
				av.src_path_bits);
		}

		ret = IB_MGT_send_mad(p_tpot_info->smi_h, p_mad,	/*  actual payload */
				      &av,	/*  address vector */
				      (u_int64_t) CAST_P2LONG(p_bo),
				      IB_MGT_DEFAULT_SEND_TIME);
	} else {
		/* GSI CASE - Support Remote QP */
		if (osm_log_is_active(p_vend->p_log, OSM_LOG_DEBUG)) {
			osm_log(p_vend->p_log, OSM_LOG_DEBUG,
				"osmv_transport_mad_send: "
				"av.dlid:0x%X, av.static_rate:%d, av.path_bits:%d, remote qp:%d \n",
				cl_ntoh16(av.dlid), av.static_rate,
				av.src_path_bits,
				cl_ntoh32(p_mad_addr->addr_type.gsi.remote_qp)
			    );
		}

		ret = IB_MGT_send_mad_to_qp(p_tpot_info->gsi_h, p_mad,	/*  actual payload */
					    &av,	/*  address vector */
					    (u_int64_t) CAST_P2LONG(p_bo),
					    IB_MGT_DEFAULT_SEND_TIME,
					    cl_ntoh32(p_mad_addr->addr_type.gsi.
						      remote_qp));

	}

	status = IB_SUCCESS;
	if (ret != IB_MGT_OK) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osmv_transport_mad_send: ERR 7210: "
			"Error sending mad (%d).\n", ret);
		status = IB_ERROR;
	} else {
		osmv_IBMGT_transport_mgr_t *p_mgr =
		    (osmv_IBMGT_transport_mgr_t *) (p_bo->p_transp_mgr);

		/* Let the others work when I am sleeping ... */
		osmv_txn_unlock(p_bo);

		cl_event_wait_on(&(p_mgr->send_done), 0xffffffff, TRUE);

		/* Re-acquire the lock */
		osmv_txn_lock(p_bo);

		if (TRUE == p_bo->is_closing) {

			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osmv_transport_mad_send: ERR 7211: "
				"The handle %p is being unbound, cannot send.\n",
				h_bind);
			status = IB_ERROR;
		}

		if (p_mgr->is_send_ok == FALSE) {
			status = IB_ERROR;
		}
	}

	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

void osmv_transport_done(IN const osm_bind_handle_t h_bind)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osm_log_t *p_log = p_bo->p_vendor->p_log;
	osmv_IBMGT_transport_mgr_t *p_mgr;
	osmv_IBMGT_transport_info_t *p_tpot_info;
	IB_MGT_ret_t ret;
	cl_list_obj_t *p_obj = NULL;
	cl_list_item_t *p_item, *p_item_tmp;
	int i;
	cl_qlist_t *p_list = NULL;

	OSM_LOG_ENTER(p_log);

	CL_ASSERT(p_bo);

	/* First of all - zero out the magic_ptr, so if a callback is called -
	   it'll know that we are currently closing down, and will not handle the
	   mad. */
	p_bo->magic_ptr = 0;

	p_mgr = (osmv_IBMGT_transport_mgr_t *) (p_bo->p_transp_mgr);
	p_tpot_info =
	    (osmv_IBMGT_transport_info_t *) (p_bo->p_vendor->p_transport_info);

	switch (p_mgr->mad_type) {
	case IB_MGT_SMI:
		p_list = p_tpot_info->p_smi_list;

		/* remove from the bindings list */
		p_item = cl_qlist_head(p_list);
		while (p_item != cl_qlist_end(p_list)) {
			p_obj = PARENT_STRUCT(p_item, cl_list_obj_t, list_item);
			if (cl_qlist_obj(p_obj) == h_bind) {
				break;
			}
			p_item_tmp = cl_qlist_next(p_item);
			p_item = p_item_tmp;
		}

		CL_ASSERT(p_item != cl_qlist_end(p_list));
		cl_qlist_remove_item(p_list, p_item);
		if (p_obj)
			free(p_obj);

		/* no one is binded to smi anymore - we can free the list, unbind & realease the hndl */
		if (cl_is_qlist_empty(p_list) == TRUE) {
			free(p_list);
			p_list = NULL;

			ret = IB_MGT_unbind_sm(p_tpot_info->smi_h);
			if (ret != IB_MGT_OK) {
				osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
					"osmv_transport_done: ERR 7212: "
					"Failed to unbind sm\n");
			}

			ret = IB_MGT_release_handle(p_tpot_info->smi_h);
			if (ret != IB_MGT_OK) {
				osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
					"osmv_transport_done: ERR 7213: "
					"Failed to release smi handle\n");
			}
			p_tpot_info->smi_h = 0xffffffff;
		}
		break;

	case IB_MGT_GSI:
		p_list = p_tpot_info->gsi_mgmt_lists[p_mgr->mgmt_class];
		/* remove from the bindings list */
		p_item = cl_qlist_head(p_list);
		while (p_item != cl_qlist_end(p_list)) {
			p_obj = PARENT_STRUCT(p_item, cl_list_obj_t, list_item);
			if (cl_qlist_obj(p_obj) == h_bind) {
				break;
			}
			p_item_tmp = cl_qlist_next(p_item);
			p_item = p_item_tmp;
		}

		CL_ASSERT(p_item != cl_qlist_end(p_list));
		cl_qlist_remove_item(p_list, p_item);
		if (p_obj)
			free(p_obj);

		/* no one is binded to this class anymore - we can free the list and unbind this class */
		if (cl_is_qlist_empty(p_list) == TRUE) {
			free(p_list);
			p_list = NULL;

			ret =
			    IB_MGT_unbind_gsi_class(p_tpot_info->gsi_h,
						    p_mgr->mgmt_class);
			if (ret != IB_MGT_OK) {
				osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
					"osmv_transport_done: ERR 7214: "
					"Failed to unbind gsi class\n");
			}
		}

		/* all the mgmt classes are unbinded - release gsi handle */
		for (i = 0; i < 15; i++) {
			if (p_tpot_info->gsi_mgmt_lists[i] != NULL) {
				break;
			}
		}

		if (i == 15) {
			ret = IB_MGT_release_handle(p_tpot_info->gsi_h);
			if (ret != IB_MGT_OK) {
				osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
					"osmv_transport_done: ERR 7215: "
					"Failed to release gsi handle\n");
			}
			p_tpot_info->gsi_h = 0xffffffff;
		}
	}			/* end switch */

	free(p_mgr);
}

/**********************************************************************
 * IB_MGT Receive callback : invoked after each receive
 **********************************************************************/
void
__osmv_IBMGT_rcv_cb(IN IB_MGT_mad_hndl_t mad_hndl,
		    IN void *private_ctx_p,
		    IN void *payload_p,
		    IN IB_MGT_mad_rcv_desc_t * rcv_remote_info_p)
{
	osmv_bind_obj_t *p_bo;
	osm_mad_addr_t mad_addr;
	cl_list_item_t *p_item;
	cl_list_obj_t *p_obj;
	cl_qlist_t *p_list;
	ib_mad_t *p_mad = (ib_mad_t *) payload_p;
	osm_vendor_t *p_vendor;
	osmv_IBMGT_transport_info_t *p_tinfo;

	__osmv_IBMGT_rcv_desc_to_osm_addr(rcv_remote_info_p,
					  ((p_mad->mgmt_class ==
					    IB_MCLASS_SUBN_LID)
					   || (p_mad->mgmt_class ==
					       IB_MCLASS_SUBN_DIR)), &mad_addr);

	/* different handling of SMI and GSI */
	if ((p_mad->mgmt_class == IB_MCLASS_SUBN_DIR) ||
	    (p_mad->mgmt_class == IB_MCLASS_SUBN_LID)) {
		/* SMI CASE */
		p_bo = (osmv_bind_obj_t *) private_ctx_p;
		/* Make sure the p_bo object is still relevant */
		if ((p_bo->magic_ptr != p_bo) || p_bo->is_closing)
			return;

		p_vendor = p_bo->p_vendor;
		p_tinfo =
		    (osmv_IBMGT_transport_info_t *) p_vendor->p_transport_info;
		p_list = p_tinfo->p_smi_list;
	} else {
		/* GSI CASE */
		p_bo = (osmv_bind_obj_t *) private_ctx_p;
		/* Make sure the p_bo object is still relevant */
		if ((p_bo->magic_ptr != p_bo) || p_bo->is_closing)
			return;

		p_vendor = p_bo->p_vendor;
		p_tinfo =
		    (osmv_IBMGT_transport_info_t *) p_vendor->p_transport_info;
		p_list = p_tinfo->gsi_mgmt_lists[p_mad->mgmt_class];
	}

	/* go over the bindings list and send the mad, one of them will accept it,
	   the others will drope
	 */
	p_item = cl_qlist_head(p_list);
	while (p_item != cl_qlist_end(p_list)) {
		p_obj = PARENT_STRUCT(p_item, cl_list_obj_t, list_item);
		p_bo = cl_qlist_obj(p_obj);
		/* give upper layer the mad */
		osmv_dispatch_mad((osm_bind_handle_t) p_bo, payload_p,
				  &mad_addr);
		/* Make sure the p_bo object is still relevant */
		if ((p_bo->magic_ptr != p_bo) || p_bo->is_closing)
			return;

		p_item = cl_qlist_next(p_item);
	}
}

/**********************************************************************
 * IB_MGT Send callback : invoked after each send
 **********************************************************************/
void
__osmv_IBMGT_send_cb(IN IB_MGT_mad_hndl_t mad_hndl,
		     IN u_int64_t wrid,
		     IN IB_comp_status_t status, IN void *private_ctx_p)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) CAST_P2LONG(wrid);

	osmv_IBMGT_transport_mgr_t *p_mgr =
	    (osmv_IBMGT_transport_mgr_t *) p_bo->p_transp_mgr;

	/* Make sure the p_bo object is still relevant */
	if (p_bo->magic_ptr != p_bo)
		return;

	/* we assume that each send on a bind object is synchronized, and no paralel sends
	   from diffrent threads with same object can be made */
	if (status == IB_COMP_SUCCESS) {
		p_mgr->is_send_ok = TRUE;
	} else
		p_mgr->is_send_ok = FALSE;
	cl_event_signal(&p_mgr->send_done);

}

/**********************************************************************
 * IB_MGT to OSM ADDRESS VECTOR
 **********************************************************************/
static void
__osmv_IBMGT_rcv_desc_to_osm_addr(IN IB_MGT_mad_rcv_desc_t * p_rcv_desc,
				  IN uint8_t is_smi,
				  OUT osm_mad_addr_t * p_mad_addr)
{
	/*  p_mad_addr->dest_lid = p_osm->subn.sm_base_lid; - for resp we use the dest lid ... */
	p_mad_addr->dest_lid = cl_hton16(p_rcv_desc->remote_lid);
	p_mad_addr->static_rate = 0;	/*  HACK - we do not  know the rate ! */
	p_mad_addr->path_bits = p_rcv_desc->local_path_bits;
	/* Clear the grh any way to avoid unset fields */
	memset(&p_mad_addr->addr_type.gsi.grh_info, 0,
	       sizeof(p_mad_addr->addr_type.gsi.grh_info));

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
__osmv_IBMGT_osm_addr_to_ibmgt_addr(IN const osm_mad_addr_t * p_mad_addr,
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
