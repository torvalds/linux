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

/*
 * Abstract:
 *    Implementation of osm_req_t.
 * This object represents the generic attribute requester.
 * This object is part of the opensm family of objects.
 *
 */

/*
  Next available error code: 0x300
*/

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#ifdef OSM_VENDOR_INTF_UMADT

#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <string.h>

#include <complib/cl_qlist.h>
#include <complib/cl_thread.h>
#include <complib/cl_timer.h>
#include <iba/ib_types.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_mad_pool.h>

#include <vendor/osm_vendor_umadt.h>
#include <vendor/osm_umadt.h>

/*  GEN1 includes */
#include "umadt_so.h"
#include "ibt.h"
#include "statustext.h"

/* //////////////////////////////////////////////////////////////////////// */
/* //////////////////////////////////////////////////////////////////////// */
/* //////////////////////////////////////////////////////////////////////// */
/* //////////////////////////////////////////////////////////////////////// */
/* //////////////////////////////////////////////////////////////////////// */
/*  */
/*      VENDOR_MAD_INTF */
/*  */
/* //////////////////////////////////////////////////////////////////////// */
/* //////////////////////////////////////////////////////////////////////// */
/* //////////////////////////////////////////////////////////////////////// */
/* //////////////////////////////////////////////////////////////////////// */
/* //////////////////////////////////////////////////////////////////////// */

/* //////////////////// */
/*  Globals        // */
/* //////////////////// */
typedef struct _ib_sa_mad_vM3 {
	uint8_t base_ver;
	uint8_t mgmt_class;
	uint8_t class_ver;
	uint8_t method;
	ib_net16_t status;
	ib_net16_t resv;
	ib_net64_t trans_id;
	ib_net16_t attr_id;
	ib_net16_t resv1;
	ib_net32_t attr_mod;
	ib_net64_t resv2;
	ib_net64_t sm_key;

	ib_net32_t seg_num;
	ib_net32_t payload_len;
	uint8_t frag_flag;
	uint8_t edit_mod;
	ib_net16_t window;
	ib_net16_t attr_offset;
	ib_net16_t resv3;

	ib_net64_t comp_mask;

	uint8_t data[IB_SA_DATA_SIZE];
} ib_sa_mad_t_vM3;
#define  DEFAULT_TIMER_INTERVAL_MSEC   500	/*  500msec timer interval */

void __mad_recv_processor(void *context);

boolean_t __valid_mad_handle(IN mad_bind_info_t * p_mad_bind_info);

cl_status_t
__match_tid_context(const cl_list_item_t * const p_list_item, void *context);
void __osm_vendor_timer_callback(IN void *context);

osm_vendor_t *osm_vendor_new(IN osm_log_t * const p_log,
			     IN const uint32_t timeout)
{
	ib_api_status_t status;
	umadt_obj_t *p_umadt_obj;

	OSM_LOG_ENTER(p_log);

	p_umadt_obj = malloc(sizeof(umadt_obj_t));
	if (p_umadt_obj) {
		memset(p_umadt_obj, 0, sizeof(umadt_obj_t));

		status = osm_vendor_init((osm_vendor_t *) p_umadt_obj, p_log,
					 timeout);
		if (status != IB_SUCCESS) {
			osm_vendor_delete((osm_vendor_t **) & p_umadt_obj);
		}
	} else {
		printf
		    ("osm_vendor_construct: ERROR! Unable to create Umadt object!\n");
	}

	OSM_LOG_EXIT(p_log);

	return ((osm_vendor_t *) p_umadt_obj);
}

void osm_vendor_delete(IN osm_vendor_t ** const pp_vend)
{
	umadt_obj_t *p_umadt_obj = (umadt_obj_t *) * pp_vend;
	cl_list_item_t *p_list_item;
	uint32_t count, i;
	mad_bind_info_t *p_mad_bind_info;

	OSM_LOG_ENTER(p_umadt_obj->p_log);

	cl_spinlock_acquire(&p_umadt_obj->register_lock);
	p_mad_bind_info =
	    (mad_bind_info_t *) cl_qlist_head(&p_umadt_obj->register_list);
	count = cl_qlist_count(&p_umadt_obj->register_list);
	cl_spinlock_release(&p_umadt_obj->register_lock);
	for (i = 0; i < count; i++) {
		cl_spinlock_acquire(&p_umadt_obj->register_lock);
		p_list_item = cl_qlist_next(&p_mad_bind_info->list_item);
		cl_spinlock_release(&p_umadt_obj->register_lock);
		/*  Unbind this handle */
		/*  osm_vendor_ubind also removesd the item from the list */
		/*  osm_vendor_unbind takes the list lock so release it here */
		osm_vendor_unbind((osm_bind_handle_t) p_mad_bind_info);
		p_mad_bind_info = (mad_bind_info_t *) p_list_item;
	}
	dlclose(p_umadt_obj->umadt_handle);
	free(p_umadt_obj);
	*pp_vend = NULL;

	OSM_LOG_EXIT(p_umadt_obj->p_log);
}

/* //////////////////////////////////////////////////////////////////////// */
/*  See VendorAbstractMadIntf.h for info */
/* //////////////////////////////////////////////////////////////////////// */
/*  */
ib_api_status_t
osm_vendor_init(IN osm_vendor_t * const p_vend,
		IN osm_log_t * const p_log, IN const uint32_t timeout)
{
	FSTATUS Status;
	PUMADT_GET_INTERFACE uMadtGetInterface;
	char *error;
	umadt_obj_t *p_umadt_obj = (umadt_obj_t *) p_vend;

	OSM_LOG_ENTER(p_log);

	p_umadt_obj->p_log = p_log;
	p_umadt_obj->timeout = timeout;

	p_umadt_obj->umadt_handle = dlopen("libibt.so", RTLD_NOW);

	if (!p_umadt_obj->umadt_handle) {
		printf("Could not load libibt.so <%s>\n", dlerror());
		return IB_ERROR;
	}
	uMadtGetInterface =
	    dlsym(p_umadt_obj->umadt_handle, "uMadtGetInterface");
	if ((error = dlerror()) != NULL) {
		printf("Could not resolve symbol uMadtGetInterface ERROR<%s>\n",
		       error);
		return IB_ERROR;
	}

	Status = (*uMadtGetInterface) (&p_umadt_obj->uMadtInterface);
	if (Status != FSUCCESS) {
		printf(" Error in getting uMADT interface ERROR<%d>\n", Status);
		return IB_ERROR;
	}

	/*  Initialize the register list and register list lock */
	cl_qlist_init(&p_umadt_obj->register_list);

	cl_spinlock_construct(&p_umadt_obj->register_lock);
	CL_ASSERT(cl_spinlock_init(&p_umadt_obj->register_lock) == CL_SUCCESS);
	p_umadt_obj->init_done = TRUE;
	printf("*****SUCCESS*****\n");

	OSM_LOG_EXIT(p_log);
	return IB_SUCCESS;

}

/* //////////////////////////////////////////////////////////////////////// */
/*  See VendorAbstractMadIntf.h for info */
/* //////////////////////////////////////////////////////////////////////// */
ib_api_status_t
osm_vendor_get_ports(IN osm_vendor_t * const p_vend,
		     IN ib_net64_t * const p_guids,
		     IN uint32_t * const p_num_guids)
{
	char *error = NULL;
	PIBT_GET_INTERFACE pfnIbtGetInterface;
	PIBT_INIT pfnIbtInitFunc;

	FSTATUS Status;
	uint32_t caCount, caGuidCount;
	IB_CA_ATTRIBUTES caAttributes;
	IB_HANDLE caHandle;
	uint32_t i;
	IB_PORT_ATTRIBUTES *pPortAttributesList;
	EUI64 CaGuidArray[8];
	void *context;
	uint64_t *p_port_guid;
	uint32_t free_guids;

	umadt_obj_t *p_umadt_obj = (umadt_obj_t *) p_vend;

	OSM_LOG_ENTER(p_umadt_obj->p_log);

	CL_ASSERT(p_guids);
	CL_ASSERT(p_num_guids);

	pfnIbtInitFunc =
	    (PIBT_INIT) dlsym(p_umadt_obj->umadt_handle, "IbtInit");

	if (!pfnIbtInitFunc) {
		printf("Error getting IbtInit function address.\n");
		return IB_ERROR;
	}

	(*pfnIbtInitFunc) ();

	pfnIbtGetInterface =
	    (PIBT_GET_INTERFACE) dlsym(p_umadt_obj->umadt_handle,
				       "IbtGetInterface");

	if (!pfnIbtGetInterface || (error = dlerror()) != NULL) {
		printf("Error getting IbtGetInterface function address.<%s>\n",
		       error);
		return FALSE;
	}
	(*pfnIbtGetInterface) (&p_umadt_obj->IbtInterface);

	caGuidCount = 8;
	Status =
	    p_umadt_obj->IbtInterface.GetCaGuidArray(&caGuidCount,
						     &CaGuidArray[0]);

	if ((Status != FSUCCESS) || (caGuidCount == 0)) {
		return FALSE;
	}

	free_guids = *p_num_guids;
	p_port_guid = p_guids;

	/* query each ca & copy its info into callers buffer */
	for (caCount = 0; caCount < caGuidCount; caCount++) {
		memset(&caAttributes, 0, sizeof(IB_CA_ATTRIBUTES));

		/* Open the CA */
		Status = p_umadt_obj->IbtInterface.Vpi.OpenCA(CaGuidArray[caCount], NULL,	/*  CACompletionCallback */
							      NULL,	/*  AsyncEventCallback */
							      NULL, &caHandle);
		if (Status != FSUCCESS) {
			return IB_ERROR;
		}

		Status = p_umadt_obj->IbtInterface.Vpi.QueryCA(caHandle,
							       &caAttributes,
							       &context);

		if (Status != FSUCCESS) {
			p_umadt_obj->IbtInterface.Vpi.CloseCA(caHandle);
			return IB_ERROR;
		}

		if (caAttributes.Ports > free_guids) {
			*p_num_guids = 0;
			memset(p_guids, 0, (*p_num_guids) * sizeof(uint64_t));
			return IB_INSUFFICIENT_MEMORY;
		}

		pPortAttributesList =
		    (IB_PORT_ATTRIBUTES *) malloc(caAttributes.
						  PortAttributesListSize);

		if (pPortAttributesList == NULL) {
			p_umadt_obj->IbtInterface.Vpi.CloseCA(caHandle);
			*p_num_guids = 0;
			memset(p_guids, 0, (*p_num_guids) * sizeof(uint64_t));
			return IB_INSUFFICIENT_MEMORY;
		}

		memset(pPortAttributesList, 0,
		       caAttributes.PortAttributesListSize);

		caAttributes.PortAttributesList = pPortAttributesList;

		Status = p_umadt_obj->IbtInterface.Vpi.QueryCA(caHandle,
							       &caAttributes,
							       &context);

		if (Status != FSUCCESS) {
			p_umadt_obj->IbtInterface.Vpi.CloseCA(caHandle);
			*p_num_guids = 0;
			memset(p_guids, 0, (*p_num_guids) * sizeof(uint64_t));
			return IB_ERROR;
		}

		pPortAttributesList = caAttributes.PortAttributesList;

		for (i = 0; i < caAttributes.Ports; i++) {
			*(p_port_guid) =
			    cl_hton64((uint64_t) pPortAttributesList->GUID);
			pPortAttributesList = pPortAttributesList->Next;
			p_port_guid++;
		}
		free(caAttributes.PortAttributesList);
		p_umadt_obj->IbtInterface.Vpi.CloseCA(caHandle);

		free_guids = free_guids - caAttributes.Ports;

	}
	*p_num_guids = *p_num_guids - free_guids;
	return IB_SUCCESS;
}

/* //////////////////////////////////////////////////////////////////////// */
/*  See VendorAbstractMadIntf.h for info */
/* //////////////////////////////////////////////////////////////////////// */
ib_mad_t *osm_vendor_get(IN osm_bind_handle_t h_bind,
			 IN const uint32_t mad_size,
			 IN osm_vend_wrap_t * p_vend_wrap)
{
	/* FSTATUS Status; */
	/* uint32_t mad_count = 0; */
	/* MadtStruct *p_madt_struct; */
	mad_bind_info_t *p_mad_bind_info = (mad_bind_info_t *) h_bind;
	umadt_obj_t *p_umadt_obj = p_mad_bind_info->p_umadt_obj;
	ib_mad_t *p_mad;
	OSM_LOG_ENTER(p_umadt_obj->p_log);

	CL_ASSERT(h_bind);

	p_umadt_obj = p_mad_bind_info->p_umadt_obj;

	/*  Sanity check */
	CL_ASSERT(p_umadt_obj->init_done);
	CL_ASSERT(p_vend_wrap);
	CL_ASSERT(__valid_mad_handle(p_mad_bind_info));

#if 0
	mad_count = 1;
	Status =
	    p_umadt_obj->uMadtInterface.uMadtGetSendMad(p_mad_bind_info->
							umadt_handle,
							&mad_count,
							&p_madt_struct);

	if (Status != FSUCCESS || p_madt_struct == NULL) {
		p_vend_wrap->p_madt_struct = NULL;
		return NULL;
	}
	p_vend_wrap->p_madt_struct = p_madt_struct;
	p_vend_wrap->direction = SEND;
	return ((ib_mad_t *) & p_madt_struct->IBMad);
#endif				/*  0 */
	p_mad = (ib_mad_t *) malloc(mad_size);
	if (!p_mad) {
		p_vend_wrap->p_madt_struct = NULL;
		return NULL;
	}

	memset(p_mad, 0, mad_size);

	p_vend_wrap->p_madt_struct = NULL;
	p_vend_wrap->direction = SEND;
	p_vend_wrap->size = mad_size;
	return (p_mad);

}

/* //////////////////////////////////////////////////////////////////////// */
/*  See VendorAbstractMadIntf.h for info */
/* //////////////////////////////////////////////////////////////////////// */
void
osm_vendor_put(IN osm_bind_handle_t h_bind,
	       IN osm_vend_wrap_t * const p_vend_wrap,
	       IN ib_mad_t * const p_mad)
{

	FSTATUS Status;

	mad_bind_info_t *p_mad_bind_info;
	umadt_obj_t *p_umadt_obj;

	/*  */
	/*  Validate the vendor mad transport handle */
	/*  */
	CL_ASSERT(h_bind);
	p_mad_bind_info = (mad_bind_info_t *) h_bind;
	p_umadt_obj = p_mad_bind_info->p_umadt_obj;

	/*  sanity check */
	CL_ASSERT(p_umadt_obj->init_done);
	CL_ASSERT(h_bind);
	CL_ASSERT(__valid_mad_handle(p_mad_bind_info));
	CL_ASSERT(p_vend_wrap);
	/* CL_ASSERT( (ib_mad_t*)&p_vend_wrap->p_madt_struct->IBMad == p_mad ); */

	/*  Release the MAD based on the direction of the MAD */
	if (p_vend_wrap->direction == SEND) {
		/*  */
		/* For a send the PostSend released the MAD with Umadt. Simply dealloacte the */
		/* local memory that was allocated on the osm_vendor_get() call. */
		/*  */
		free(p_mad);
#if 0
		Status =
		    p_umadt_obj->uMadtInterface.
		    uMadtReleaseSendMad(p_mad_bind_info->umadt_handle,
					p_vend_wrap->p_madt_struct);
		if (Status != FSUCCESS) {
			/* printf("uMadtReleaseSendMad: Status  = <%d>\n", Status); */
			return;
		}
#endif
	} else if (p_vend_wrap->direction == RECEIVE) {
		CL_ASSERT((ib_mad_t *) & p_vend_wrap->p_madt_struct->IBMad ==
			  p_mad);
		Status =
		    p_umadt_obj->uMadtInterface.
		    uMadtReleaseRecvMad(p_mad_bind_info->umadt_handle,
					p_vend_wrap->p_madt_struct);
		if (Status != FSUCCESS) {
			/* printf("uMadtReleaseRecvMad Status=<%d>\n", Status); */
			return;
		}
	} else {
		return;
	}
	return;
}

/* //////////////////////////////////////////////////////////////////////// */
/*  See VendorAbstractMadIntf.h for info */
/* //////////////////////////////////////////////////////////////////////// */
ib_api_status_t
osm_vendor_send(IN osm_bind_handle_t h_bind,
		IN osm_vend_wrap_t * const p_vend_wrap,
		IN osm_mad_addr_t * const p_mad_addr,
		IN ib_mad_t * const p_mad,
		IN void *transaction_context, IN boolean_t const resp_expected)
{
	FSTATUS Status;

	MadAddrStruct destAddr = { 0 };

	mad_bind_info_t *p_mad_bind_info;
	trans_context_t *p_trans_context;

	umadt_obj_t *p_umadt_obj = NULL;

	uint32_t mad_count = 0;
	MadtStruct *p_madt_struct = NULL;
	uint32_t i;
	uint32_t num_mads = 0;
	uint32_t seg_num = 0;
	uint8_t *p_frag_data = NULL;
	ib_sa_mad_t_vM3 *p_sa_mad = NULL;

	CL_ASSERT(h_bind);
	p_mad_bind_info = (mad_bind_info_t *) h_bind;
	p_umadt_obj = p_mad_bind_info->p_umadt_obj;

	/*  sanity check */
	CL_ASSERT(p_umadt_obj);
	CL_ASSERT(p_umadt_obj->init_done);
	CL_ASSERT(__valid_mad_handle(p_mad_bind_info));
	CL_ASSERT(p_vend_wrap);
	CL_ASSERT(p_mad_addr);
	CL_ASSERT(p_mad);
	/* CL_ASSERT( (ib_mad_t*)&p_vend_wrap->p_madt_struct->IBMad == p_mad ); */

	/*  */
	/*  based on the class, fill out the address info */
	/*  */
	destAddr.DestLid = p_mad_addr->dest_lid;
	destAddr.PathBits = p_mad_addr->path_bits;
	destAddr.StaticRate = p_mad_addr->static_rate;

	if (p_mad_bind_info->umadt_reg_class.ClassId == IB_MCLASS_SUBN_LID ||
	    p_mad_bind_info->umadt_reg_class.ClassId == IB_MCLASS_SUBN_DIR) {
		CL_ASSERT(p_mad_addr->addr_type.smi.source_lid);
		destAddr.AddrType.Smi.SourceLid =
		    p_mad_addr->addr_type.smi.source_lid;
	} else {
		destAddr.AddrType.Gsi.RemoteQpNumber =
		    p_mad_addr->addr_type.gsi.remote_qp;
		destAddr.AddrType.Gsi.RemoteQkey =
		    p_mad_addr->addr_type.gsi.remote_qkey;
		destAddr.AddrType.Gsi.PKey = OSM_DEFAULT_PKEY;
		destAddr.AddrType.Gsi.ServiceLevel =
		    p_mad_addr->addr_type.gsi.service_level;
		destAddr.AddrType.Gsi.GlobalRoute =
		    p_mad_addr->addr_type.gsi.global_route;
		/* destAddr.AddrType.Gsi.GRHInfo = p_mad_addr->addr_type.gsi.grh_info; */
	}
	p_mad->trans_id = cl_ntoh64(p_mad->trans_id) << 24;

	/*  */
	/*  Create a transaction context for this send and save the TID and client context. */
	/*  */

	if (resp_expected) {
		p_trans_context = malloc(sizeof(trans_context_t));
		CL_ASSERT(p_trans_context);

		memset(p_trans_context, 0, sizeof(trans_context_t));
		p_trans_context->trans_id = p_mad->trans_id;
		p_trans_context->context = transaction_context;
		p_trans_context->sent_time = cl_get_time_stamp();

		cl_spinlock_acquire(&p_mad_bind_info->trans_ctxt_lock);
		cl_qlist_insert_tail(&p_mad_bind_info->trans_ctxt_list,
				     &p_trans_context->list_item);
		cl_spinlock_release(&p_mad_bind_info->trans_ctxt_lock);
	}

	if (p_mad_bind_info->umadt_reg_class.ClassId == IB_MCLASS_SUBN_LID ||
	    p_mad_bind_info->umadt_reg_class.ClassId == IB_MCLASS_SUBN_DIR) {
		/*  Get one mad from uMadt */
		mad_count = 1;
		Status =
		    p_umadt_obj->uMadtInterface.
		    uMadtGetSendMad(p_mad_bind_info->umadt_handle, &mad_count,
				    &p_madt_struct);

		if (Status != FSUCCESS || p_madt_struct == NULL) {
			return IB_ERROR;
		}

		/*  No Segmentation required */
		memcpy(&p_madt_struct->IBMad, p_mad, MAD_BLOCK_SIZE);

		/*  Post the MAD */

		Status =
		    p_umadt_obj->uMadtInterface.uMadtPostSend(p_mad_bind_info->
							      umadt_handle,
							      p_madt_struct,
							      &destAddr);
		if (Status != FSUCCESS) {
			printf("uMadtPostSendMad: Status  = <%d>\n", Status);
			return IB_ERROR;
		}

		/*  Release send MAD */
		Status =
		    p_umadt_obj->uMadtInterface.
		    uMadtReleaseSendMad(p_mad_bind_info->umadt_handle,
					p_madt_struct);
		if (Status != FSUCCESS) {
			printf("uMadtReleaseSendMad: Status  = <%d>\n", Status);
			return IB_ERROR;
		}
	} else {

		/*  */
		/*  Segment the MAD, get the required send mads from uMadt and post the MADs. */
		/*  */
		uint32_t payload_len;

		payload_len =
		    cl_ntoh32(((ib_sa_mad_t_vM3 *) p_mad)->payload_len);
		num_mads = payload_len / IB_SA_DATA_SIZE;
		if (payload_len % IB_SA_DATA_SIZE != 0) {
			num_mads++;	/*  Get one additional mad for the remainder */
		}
		for (i = 0; i < num_mads; i++) {
			/*  Get one mad from uMadt */
			mad_count = 1;
			Status =
			    p_umadt_obj->uMadtInterface.
			    uMadtGetSendMad(p_mad_bind_info->umadt_handle,
					    &mad_count, &p_madt_struct);

			if (Status != FSUCCESS || p_madt_struct == NULL) {
				return IB_ERROR;
			}
			/*  Copy client MAD into uMadt's MAD. */
			if (i == 0) {	/*  First Packet */
				/*  Since this is the first MAD, copy the entire MAD_SIZE */
				memcpy(&p_madt_struct->IBMad, p_mad,
				       MAD_BLOCK_SIZE);

				p_frag_data =
				    (uint8_t *) p_mad + MAD_BLOCK_SIZE;

				p_sa_mad =
				    (ib_sa_mad_t_vM3 *) & p_madt_struct->IBMad;
				if (num_mads == 1) {	/*  Only one Packet */
					p_sa_mad->seg_num = 0;
					p_sa_mad->frag_flag = 5;	/*  Set bit 0 for first pkt and b4 for last pkt */
					/*  the payload length gets copied with the mad header above */
				} else {	/*  More than one packet in this response */

					seg_num = 1;
					p_sa_mad->seg_num =
					    cl_ntoh32(seg_num++);
					p_sa_mad->frag_flag = 1;	/*  Set bit 0 for first pkt */
					/*  the payload length gets copied with the mad header above */
				}

			} else if (i < num_mads - 1) {	/*  Not last packet */
				/*  First copy only the header */
				memcpy(&p_madt_struct->IBMad, p_mad,
				       IB_SA_MAD_HDR_SIZE);
				/*  Set the relevant fields in the SA_MAD_HEADER */
				p_sa_mad =
				    (ib_sa_mad_t_vM3 *) & p_madt_struct->IBMad;
				p_sa_mad->payload_len =
				    cl_ntoh32(IB_SA_DATA_SIZE);
				p_sa_mad->seg_num = cl_ntoh32(seg_num++);
				p_sa_mad->frag_flag = 0;
				/*  Now copy the fragmented data */
				memcpy(((uint8_t *) & p_madt_struct->IBMad) +
				       IB_SA_MAD_HDR_SIZE, p_frag_data,
				       IB_SA_DATA_SIZE);
				p_frag_data = p_frag_data + IB_SA_DATA_SIZE;

			} else if (i == num_mads - 1) {	/*  Last packet */
				/*  First copy only the header */
				memcpy(&p_madt_struct->IBMad, p_mad,
				       IB_SA_MAD_HDR_SIZE);
				/*  Set the relevant fields in the SA_MAD_HEADER */
				p_sa_mad =
				    (ib_sa_mad_t_vM3 *) & p_madt_struct->IBMad;
				p_sa_mad->seg_num = cl_ntoh32(seg_num++);
				p_sa_mad->frag_flag = 4;	/*  Set Bit 2 for last pkt */
				p_sa_mad->payload_len =
				    cl_ntoh32(cl_ntoh32
					      (((ib_sa_mad_t_vM3 *) p_mad)->
					       payload_len) % IB_SA_DATA_SIZE);
				/*  Now copy the fragmented data */
				memcpy((((uint8_t *) & p_madt_struct->IBMad)) +
				       IB_SA_MAD_HDR_SIZE, p_frag_data,
				       cl_ntoh32(p_sa_mad->payload_len));
				p_frag_data = p_frag_data + IB_SA_DATA_SIZE;

			}
			/*  Post the MAD */
			Status =
			    p_umadt_obj->uMadtInterface.
			    uMadtPostSend(p_mad_bind_info->umadt_handle,
					  p_madt_struct, &destAddr);
			if (Status != FSUCCESS) {
				printf("uMadtPostSendMad: Status  = <%d>\n",
				       Status);
				return IB_ERROR;
			}

			/*  Release send MAD */
			Status =
			    p_umadt_obj->uMadtInterface.
			    uMadtReleaseSendMad(p_mad_bind_info->umadt_handle,
						p_madt_struct);
			if (Status != FSUCCESS) {
				printf("uMadtReleaseSendMad: Status  = <%d>\n",
				       Status);
				return IB_ERROR;
			}
		}
	}
	return (IB_SUCCESS);
}

/* //////////////////////////////////////////////////////////////////////// */
/*  See VendorAbstractMadIntf.h for info */
/* //////////////////////////////////////////////////////////////////////// */

osm_bind_handle_t
osm_vendor_bind(IN osm_vendor_t * const p_vend,
		IN osm_bind_info_t * const p_osm_bind_info,
		IN osm_mad_pool_t * const p_mad_pool,
		IN osm_vend_mad_recv_callback_t mad_recv_callback,
		IN void *context)
{
	cl_status_t cl_status;
	FSTATUS Status;		/*  GEN1 Status for Umadt */

	mad_bind_info_t *p_mad_bind_info;
	RegisterClassStruct *p_umadt_reg_class;

	umadt_obj_t *p_umadt_obj;
	OSM_LOG_ENTER(((umadt_obj_t *) p_vend)->p_log);

	CL_ASSERT(p_vend);

	p_umadt_obj = (umadt_obj_t *) p_vend;

	/*  Sanity check */
	CL_ASSERT(p_umadt_obj->init_done);
	CL_ASSERT(p_osm_bind_info);
	CL_ASSERT(p_mad_pool);
	CL_ASSERT(mad_recv_callback);

	/*  Allocate memory for registering the handle. */
	p_mad_bind_info = (mad_bind_info_t *) malloc(sizeof(*p_mad_bind_info));
	if (p_mad_bind_info) {
		memset(p_mad_bind_info, 0, sizeof(*p_mad_bind_info));
		p_umadt_reg_class = &p_mad_bind_info->umadt_reg_class;
	}
	p_umadt_reg_class->PortGuid = cl_ntoh64(p_osm_bind_info->port_guid);
	p_umadt_reg_class->ClassId = p_osm_bind_info->mad_class;
	p_umadt_reg_class->ClassVersion = p_osm_bind_info->class_version;
	p_umadt_reg_class->isResponder = p_osm_bind_info->is_responder;
	p_umadt_reg_class->isTrapProcessor = p_osm_bind_info->is_trap_processor;
	p_umadt_reg_class->isReportProcessor =
	    p_osm_bind_info->is_report_processor;
	p_umadt_reg_class->SendQueueSize = p_osm_bind_info->send_q_size;
	p_umadt_reg_class->RecvQueueSize = p_osm_bind_info->recv_q_size;
	p_umadt_reg_class->NotifySendCompletion = TRUE;

	p_mad_bind_info->p_umadt_obj = p_umadt_obj;
	p_mad_bind_info->p_mad_pool = p_mad_pool;
	p_mad_bind_info->mad_recv_callback = mad_recv_callback;
	p_mad_bind_info->client_context = context;

	/*  register with Umadt for MAD interface */
	Status = p_umadt_obj->uMadtInterface.uMadtRegister(p_umadt_reg_class,
							   &p_mad_bind_info->
							   umadt_handle);
	if (Status != FSUCCESS) {
		free(p_mad_bind_info);
		OSM_LOG_EXIT(p_umadt_obj->p_log);
		return (OSM_BIND_INVALID_HANDLE);
	}
	CL_ASSERT(p_mad_bind_info->umadt_handle);
	/*  */
	/*  Start a worker thread to process receives. */
	/*  */
	cl_thread_construct(&p_mad_bind_info->recv_processor_thread);
	cl_status = cl_thread_init(&p_mad_bind_info->recv_processor_thread,
				   __mad_recv_processor,
				   (void *)p_mad_bind_info, "mad_recv_worker");
	CL_ASSERT(cl_status == CL_SUCCESS);

	cl_qlist_init(&p_mad_bind_info->trans_ctxt_list);
	cl_spinlock_construct(&p_mad_bind_info->trans_ctxt_lock);
	cl_spinlock_init(&p_mad_bind_info->trans_ctxt_lock);
	cl_spinlock_construct(&p_mad_bind_info->timeout_list_lock);
	cl_spinlock_init(&p_mad_bind_info->timeout_list_lock);

	cl_status = cl_timer_init(&p_mad_bind_info->timeout_timer,
				  __osm_vendor_timer_callback,
				  (void *)p_mad_bind_info);
	CL_ASSERT(cl_status == CL_SUCCESS);
	cl_qlist_init(&p_mad_bind_info->timeout_list);
	/*  */
	/*  Insert the mad_reg_struct in list and return pointer to it as the handle */
	/*  */
	cl_spinlock_acquire(&p_umadt_obj->register_lock);

	cl_qlist_insert_head(&p_umadt_obj->register_list,
			     &p_mad_bind_info->list_item);

	cl_spinlock_release(&p_umadt_obj->register_lock);

	/*
	   A timeout value of 0 means disable timeouts.
	 */
	if (p_umadt_obj->timeout) {
		cl_timer_start(&p_mad_bind_info->timeout_timer,
			       DEFAULT_TIMER_INTERVAL_MSEC);
	}

	OSM_LOG_EXIT(p_umadt_obj->p_log);
	return ((osm_bind_handle_t) p_mad_bind_info);
}

void osm_vendor_unbind(IN osm_bind_handle_t h_bind)
{
	mad_bind_info_t *p_mad_bind_info;
	umadt_obj_t *p_umadt_obj;
	cl_list_item_t *p_list_item, *p_next_list_item;

	CL_ASSERT(h_bind);
	p_mad_bind_info = (mad_bind_info_t *) h_bind;
	p_umadt_obj = p_mad_bind_info->p_umadt_obj;

	/*  sanity check */
	CL_ASSERT(p_umadt_obj);
	CL_ASSERT(p_umadt_obj->init_done);
	CL_ASSERT(__valid_mad_handle(p_mad_bind_info));

	p_umadt_obj->uMadtInterface.uMadtDestroy(&p_mad_bind_info->
						 umadt_handle);
	cl_timer_destroy(&p_mad_bind_info->timeout_timer);
	cl_thread_destroy(&p_mad_bind_info->recv_processor_thread);

	cl_spinlock_acquire(&p_mad_bind_info->trans_ctxt_lock);
	p_list_item = cl_qlist_head(&p_mad_bind_info->trans_ctxt_list);
	while (p_list_item != cl_qlist_end(&p_mad_bind_info->trans_ctxt_list)) {
		p_next_list_item = cl_qlist_next(p_list_item);
		cl_qlist_remove_item(&p_mad_bind_info->trans_ctxt_list,
				     p_list_item);
		free(p_list_item);
		p_list_item = p_next_list_item;
	}
	cl_spinlock_release(&p_mad_bind_info->trans_ctxt_lock);

	cl_spinlock_acquire(&p_mad_bind_info->timeout_list_lock);
	p_list_item = cl_qlist_head(&p_mad_bind_info->timeout_list);
	while (p_list_item != cl_qlist_end(&p_mad_bind_info->timeout_list)) {
		p_next_list_item = cl_qlist_next(p_list_item);
		cl_qlist_remove_item(&p_mad_bind_info->timeout_list,
				     p_list_item);
		free(p_list_item);
		p_list_item = p_next_list_item;
	}
	cl_spinlock_release(&p_mad_bind_info->timeout_list_lock);

	free(p_mad_bind_info);
}

void __mad_recv_processor(IN void *context)
{
	mad_bind_info_t *p_mad_bind_info = (mad_bind_info_t *) context;
	umadt_obj_t *p_umadt_obj;
	osm_madw_t *p_osm_madw = NULL;
	osm_vend_wrap_t *p_vend_wrap = NULL;
	osm_mad_addr_t osm_mad_addr = { 0 };
	cl_list_item_t *p_list_item;
	void *transaction_context;

	FSTATUS Status;
	MadtStruct *pRecvMad = NULL;
	MadWorkCompletion *pRecvCmp = NULL;

	CL_ASSERT(context);

	p_mad_bind_info = (mad_bind_info_t *) context;
	p_umadt_obj = p_mad_bind_info->p_umadt_obj;
	/*  PollFor a completion */
	/*  if FNOTFOND, then wait for a completion then again poll and return the MAD */
	while (1) {
		Status =
		    p_umadt_obj->uMadtInterface.
		    uMadtPollForRecvCompletion(p_mad_bind_info->umadt_handle,
					       &pRecvMad, &pRecvCmp);
		if (Status != FSUCCESS) {
			if (Status == FNOT_FOUND) {
				/* Wait for a completion */
				Status = p_umadt_obj->uMadtInterface.uMadtWaitForAnyCompletion(p_mad_bind_info->umadt_handle, RECV_COMPLETION, 0x5000);	/* 5 sec timeout */

				if (Status == FTIMEOUT) {
					continue;
				}
				CL_ASSERT(Status == FSUCCESS);

				Status =
				    p_umadt_obj->uMadtInterface.
				    uMadtPollForRecvCompletion(p_mad_bind_info->
							       umadt_handle,
							       &pRecvMad,
							       &pRecvCmp);
				if (Status != FSUCCESS) {
					printf
					    (" mad_recv_worker: Error in PollForRecv returning <%x>\n",
					     Status);
					CL_ASSERT(0);
				}
			} else {
				printf
				    ("uMadtPollForRecvCompletion Status=<%x>\n",
				     Status);
				CL_ASSERT(0);
			}
		}
		CL_ASSERT(pRecvMad);
		CL_ASSERT(pRecvCmp);

		if (((ib_sa_mad_t_vM3 *) (&pRecvMad->IBMad))->frag_flag & 0x20) {
			/*  Ignore the ACK packet */
			Status =
			    p_umadt_obj->uMadtInterface.
			    uMadtReleaseRecvMad(p_mad_bind_info->umadt_handle,
						pRecvMad);
			continue;
		}
		/*  */
		/*  Extract the return address to pass it on to the client */
		/*  */
		osm_mad_addr.dest_lid = pRecvCmp->AddressInfo.DestLid;
		osm_mad_addr.path_bits = pRecvCmp->AddressInfo.PathBits;
		osm_mad_addr.static_rate = pRecvCmp->AddressInfo.StaticRate;

		if (p_mad_bind_info->umadt_reg_class.ClassId ==
		    IB_MCLASS_SUBN_LID
		    || p_mad_bind_info->umadt_reg_class.ClassId ==
		    IB_MCLASS_SUBN_DIR) {
			osm_mad_addr.addr_type.smi.source_lid =
			    pRecvCmp->AddressInfo.AddrType.Smi.SourceLid;
			/* osm_mad_addr.addr_type.smi.port_num = pRecvCmp->AddressInfo.AddrType.Smi.PortNumber; */
		} else {
			osm_mad_addr.addr_type.gsi.remote_qp =
			    pRecvCmp->AddressInfo.AddrType.Gsi.RemoteQpNumber;
			osm_mad_addr.addr_type.gsi.remote_qkey =
			    pRecvCmp->AddressInfo.AddrType.Gsi.RemoteQkey;
			osm_mad_addr.addr_type.gsi.pkey_ix = 0;
			osm_mad_addr.addr_type.gsi.service_level =
			    pRecvCmp->AddressInfo.AddrType.Gsi.ServiceLevel;
			osm_mad_addr.addr_type.gsi.global_route =
			    pRecvCmp->AddressInfo.AddrType.Gsi.GlobalRoute;
			/* osm_mad_addr.addr_type.gsi.grh_info = pRecvCmp->AddressInfo.AddrType.Gsi.GRHInfo; */
		}
		p_osm_madw =
		    osm_mad_pool_get_wrapper(p_mad_bind_info->p_mad_pool,
					     p_mad_bind_info, MAD_BLOCK_SIZE,
					     (ib_mad_t *) & pRecvMad->IBMad,
					     &osm_mad_addr);
		CL_ASSERT(p_osm_madw);
		p_vend_wrap = osm_madw_get_vend_ptr(p_osm_madw);
		CL_ASSERT(p_vend_wrap);
		p_vend_wrap->p_madt_struct = pRecvMad;
		p_vend_wrap->direction = RECEIVE;

		osm_log(p_mad_bind_info->p_umadt_obj->p_log, OSM_LOG_DEBUG,
			"__mad_recv_processor: "
			"Received data p_osm_madw[0x%p].\n", p_osm_madw);

		/*  */
		/*  Do TID Processing. */
		/*  */
		/*  If R bit is set swap the TID */

		cl_spinlock_acquire(&p_mad_bind_info->trans_ctxt_lock);
		p_list_item =
		    cl_qlist_find_from_head(&p_mad_bind_info->trans_ctxt_list,
					    __match_tid_context,
					    &p_osm_madw->p_mad->trans_id);

		if (p_list_item ==
		    cl_qlist_end(&p_mad_bind_info->trans_ctxt_list)) {
			transaction_context = NULL;
		} else {
			transaction_context =
			    ((trans_context_t *) p_list_item)->context;
			cl_qlist_remove_item(&p_mad_bind_info->trans_ctxt_list,
					     p_list_item);
			free(p_list_item);
		}
		cl_spinlock_release(&p_mad_bind_info->trans_ctxt_lock);
		((ib_mad_t *) p_osm_madw->p_mad)->trans_id =
		    cl_ntoh64(p_osm_madw->p_mad->trans_id >> 24);
		osm_log(p_mad_bind_info->p_umadt_obj->p_log, OSM_LOG_DEBUG,
			"__mad_recv_processor: "
			"Received data p_osm_madw [0x%p]" "\n\t\t\t\tTID[0x%"
			PRIx64 ", context[%p]. \n", p_osm_madw,
			((ib_mad_t *) p_osm_madw->p_mad)->trans_id,
			transaction_context);

		(*(p_mad_bind_info->mad_recv_callback)) (p_osm_madw,
							 p_mad_bind_info->
							 client_context,
							 transaction_context);

	}
}

cl_status_t
__match_tid_context(const cl_list_item_t * const p_list_item, void *context)
{
	if (((trans_context_t *) p_list_item)->trans_id ==
	    *((uint64_t *) context))
		return CL_SUCCESS;
	return CL_NOT_FOUND;
}

boolean_t __valid_mad_handle(IN mad_bind_info_t * p_mad_bind_info)
{

	umadt_obj_t *p_umadt_obj;

	p_umadt_obj = p_mad_bind_info->p_umadt_obj;

	cl_spinlock_acquire(&p_umadt_obj->register_lock);
	if (!cl_is_item_in_qlist(&p_umadt_obj->register_list,
				 &p_mad_bind_info->list_item)) {
		cl_spinlock_release(&p_umadt_obj->register_lock);
		return FALSE;
	}
	cl_spinlock_release(&p_umadt_obj->register_lock);
	return TRUE;
}

void __osm_vendor_timer_callback(IN void *context)
{
	uint64_t current_time;
	mad_bind_info_t *p_mad_bind_info;
	umadt_obj_t *p_umadt_obj;
	uint32_t timeout;

	cl_list_item_t *p_list_item, *p_next_list_item;

	CL_ASSERT(context);

	p_mad_bind_info = (mad_bind_info_t *) context;
	p_umadt_obj = p_mad_bind_info->p_umadt_obj;
	timeout = p_umadt_obj->timeout * 1000;

	current_time = cl_get_time_stamp();

	cl_spinlock_acquire(&p_mad_bind_info->trans_ctxt_lock);

	p_list_item = cl_qlist_head(&p_mad_bind_info->trans_ctxt_list);
	while (p_list_item != cl_qlist_end(&p_mad_bind_info->trans_ctxt_list)) {

		p_next_list_item = cl_qlist_next(p_list_item);

		/*  DEFAULT_PKT_TIMEOUT is in milli seconds */
		if (current_time - ((trans_context_t *) p_list_item)->sent_time
		    > timeout) {
			/*  Add this transaction to the timeout_list */
			cl_qlist_remove_item(&p_mad_bind_info->trans_ctxt_list,
					     p_list_item);
			cl_qlist_insert_tail(&p_mad_bind_info->timeout_list,
					     p_list_item);
		}

		p_list_item = p_next_list_item;
	}

	cl_spinlock_release(&p_mad_bind_info->trans_ctxt_lock);

	p_list_item = cl_qlist_head(&p_mad_bind_info->timeout_list);
	while (p_list_item != cl_qlist_end(&p_mad_bind_info->timeout_list)) {
		osm_log(p_mad_bind_info->p_umadt_obj->p_log, OSM_LOG_DEBUG,
			"__osm_vendor_timer_callback: "
			"Timing out transaction context [0x%p].\n",
			((trans_context_t *) p_list_item)->context);

		(*(p_mad_bind_info->mad_recv_callback)) (NULL,
							 p_mad_bind_info->
							 client_context,
							 ((trans_context_t *)
							  p_list_item)->
							 context);

		p_next_list_item = cl_qlist_next(p_list_item);
		cl_qlist_remove_item(&p_mad_bind_info->timeout_list,
				     p_list_item);
		free(p_list_item);
		p_list_item = p_next_list_item;
	}

	cl_timer_start(&p_mad_bind_info->timeout_timer,
		       DEFAULT_TIMER_INTERVAL_MSEC);

}

#endif				/* OSM_VENDOR_INTF_UMADT */
