/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2012 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
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
 * 	Declaration of osm_mad_wrapper_t.
 *	This object represents the context wrapper for OpenSM MAD processing.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_MADW_H_
#define _OSM_MADW_H_

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qlist.h>
#include <complib/cl_dispatcher.h>
#include <opensm/osm_base.h>
#include <vendor/osm_vendor.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****s* OpenSM: MAD Wrapper/osm_bind_info_t
* NAME
*   osm_bind_info_t
*
* DESCRIPTION
*
* SYNOPSIS
*/
typedef struct osm_bind_info {
	ib_net64_t port_guid;
	uint8_t mad_class;
	uint8_t class_version;
	boolean_t is_responder;
	boolean_t is_trap_processor;
	boolean_t is_report_processor;
	uint32_t send_q_size;
	uint32_t recv_q_size;
	uint32_t timeout;
	uint32_t retries;
} osm_bind_info_t;
/*
* FIELDS
*	portguid
*		PortGuid of local port
*
*	mad_class
*		Mgmt Class ID
*
*	class_version
*		Mgmt Class version
*
*	is_responder
*		True if this is a GSI Agent
*
*	is_trap_processor
*		True if GSI Trap msgs are handled
*
*	is_report_processor
*		True if GSI Report msgs are handled
*
*	send_q_size
*		SendQueueSize
*
*	recv_q_size
*		Receive Queue Size
*
*	timeout
*		Transaction timeout
*
*	retries
*		Number of retries for transaction
*
* SEE ALSO
*********/

/****h* OpenSM/MAD Wrapper
* NAME
*	MAD Wrapper
*
* DESCRIPTION
*	The MAD Wrapper object encapsulates the information needed by the
*	OpenSM to manage individual MADs.  The OpenSM allocates one MAD Wrapper
*	per MAD.
*
*	The MAD Wrapper is not thread safe, thus callers must provide
*	serialization.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/

/****s* OpenSM: MAD Wrapper/osm_ni_context_t
* NAME
*	osm_ni_context_t
*
* DESCRIPTION
*	Context needed by recipient of NodeInfo attribute.
*
* SYNOPSIS
*/
typedef struct osm_ni_context {
	ib_net64_t node_guid;
	uint8_t port_num;
	ib_net64_t dup_node_guid;
	uint8_t dup_port_num;
	unsigned dup_count;
} osm_ni_context_t;
/*
* FIELDS
*	p_node
*		Pointer to the node thru which we got to this node.
*
*	p_sw
*		Pointer to the switch object (if any) of the switch
*		thru which we got to this node.
*
*	port_num
*		Port number on the node or switch thru which we got
*		to this node.
*
* SEE ALSO
*********/

/****s* OpenSM: MAD Wrapper/osm_pi_context_t
* NAME
*	osm_pi_context_t
*
* DESCRIPTION
*	Context needed by recipient of PortInfo attribute.
*
* SYNOPSIS
*/
typedef struct osm_pi_context {
	ib_net64_t node_guid;
	ib_net64_t port_guid;
	boolean_t set_method;
	boolean_t light_sweep;
	boolean_t active_transition;
	boolean_t client_rereg;
} osm_pi_context_t;
/*********/

/****s* OpenSM: MAD Wrapper/osm_gi_context_t
* NAME
*	osm_gi_context_t
*
* DESCRIPTION
*	Context needed by recipient of GUIDInfo attribute.
*
* SYNOPSIS
*/
typedef struct osm_gi_context {
	ib_net64_t node_guid;
	ib_net64_t port_guid;
	boolean_t set_method;
	uint8_t port_num;
} osm_gi_context_t;
/*********/

/****s* OpenSM: MAD Wrapper/osm_nd_context_t
* NAME
*	osm_nd_context_t
*
* DESCRIPTION
*	Context needed by recipient of NodeDescription attribute.
*
* SYNOPSIS
*/
typedef struct osm_nd_context {
	ib_net64_t node_guid;
} osm_nd_context_t;
/*********/

/****s* OpenSM: MAD Wrapper/osm_si_context_t
* NAME
*	osm_si_context_t
*
* DESCRIPTION
*	Context needed by recipient of SwitchInfo attribute.
*
* SYNOPSIS
*/
typedef struct osm_si_context {
	ib_net64_t node_guid;
	boolean_t set_method;
	boolean_t light_sweep;
	boolean_t lft_top_change;
} osm_si_context_t;
/*********/

/****s* OpenSM: MAD Wrapper/osm_lft_context_t
* NAME
*	osm_lft_context_t
*
* DESCRIPTION
*	Context needed by recipient of LinearForwardingTable attribute.
*
* SYNOPSIS
*/
typedef struct osm_lft_context {
	ib_net64_t node_guid;
	boolean_t set_method;
} osm_lft_context_t;
/*********/

/****s* OpenSM: MAD Wrapper/osm_mft_context_t
* NAME
*	osm_mft_context_t
*
* DESCRIPTION
*	Context needed by recipient of MulticastForwardingTable attribute.
*
* SYNOPSIS
*/
typedef struct osm_mft_context {
	ib_net64_t node_guid;
	boolean_t set_method;
} osm_mft_context_t;
/*********/

/****s* OpenSM: MAD Wrapper/osm_smi_context_t
* NAME
*	osm_smi_context_t
*
* DESCRIPTION
*	Context needed by recipient of SMInfo attribute.
*
* SYNOPSIS
*/
typedef struct osm_smi_context {
	ib_net64_t port_guid;
	boolean_t set_method;
	boolean_t light_sweep;
} osm_smi_context_t;
/*********/

/****s* OpenSM: MAD Wrapper/osm_pkey_context_t
* NAME
*	osm_pkey_context_t
*
* DESCRIPTION
*	Context needed by recipient of P_Key attribute.
*
* SYNOPSIS
*/
typedef struct osm_pkey_context {
	ib_net64_t node_guid;
	ib_net64_t port_guid;
	boolean_t set_method;
} osm_pkey_context_t;
/*********/

/****s* OpenSM: MAD Wrapper/osm_slvl_context_t
* NAME
*	osm_slvl_context_t
*
* DESCRIPTION
*	Context needed by recipient of PortInfo attribute.
*
* SYNOPSIS
*/
typedef struct osm_slvl_context {
	ib_net64_t node_guid;
	ib_net64_t port_guid;
	boolean_t set_method;
} osm_slvl_context_t;
/*********/

/****s* OpenSM: MAD Wrapper/osm_vla_context_t
* NAME
*	osm_vla_context_t
*
* DESCRIPTION
*	Context needed by recipient of VL Arb attribute.
*
* SYNOPSIS
*/
typedef struct osm_vla_context {
	ib_net64_t node_guid;
	ib_net64_t port_guid;
	boolean_t set_method;
} osm_vla_context_t;
/*********/

/****s* OpenSM: MAD Wrapper/osm_perfmgr_context_t
* DESCRIPTION
*	Context for Performance manager queries
*/
typedef struct osm_perfmgr_context {
	uint64_t node_guid;
	uint16_t port;
	uint8_t mad_method;	/* was this a get or a set */
	ib_net16_t mad_attr_id;
#ifdef ENABLE_OSM_PERF_MGR_PROFILE
	struct timeval query_start;
#endif
} osm_perfmgr_context_t;
/*********/

/****s* OpenSM: MAD Wrapper/osm_cc_context_t
* DESCRIPTION
*	Context for Congestion Control MADs
*/
typedef struct osm_cc_context {
	ib_net64_t node_guid;
	ib_net64_t port_guid;
	uint8_t port;
	uint8_t mad_method;	/* was this a get or a set */
	ib_net32_t attr_mod;
} osm_cc_context_t;
/*********/

#ifndef OSM_VENDOR_INTF_OPENIB
/****s* OpenSM: MAD Wrapper/osm_arbitrary_context_t
* NAME
*	osm_arbitrary_context_t
*
* DESCRIPTION
*	Context needed by arbitrary recipient.
*
* SYNOPSIS
*/
typedef struct osm_arbitrary_context {
	void *context1;
	void *context2;
} osm_arbitrary_context_t;
/*********/
#endif

/****s* OpenSM: MAD Wrapper/osm_madw_context_t
* NAME
*	osm_madw_context_t
*
* DESCRIPTION
*	Context needed by recipients of MAD responses.
*
* SYNOPSIS
*/
typedef union _osm_madw_context {
	osm_ni_context_t ni_context;
	osm_pi_context_t pi_context;
	osm_gi_context_t gi_context;
	osm_nd_context_t nd_context;
	osm_si_context_t si_context;
	osm_lft_context_t lft_context;
	osm_mft_context_t mft_context;
	osm_smi_context_t smi_context;
	osm_slvl_context_t slvl_context;
	osm_pkey_context_t pkey_context;
	osm_vla_context_t vla_context;
	osm_perfmgr_context_t perfmgr_context;
	osm_cc_context_t cc_context;
#ifndef OSM_VENDOR_INTF_OPENIB
	osm_arbitrary_context_t arb_context;
#endif
} osm_madw_context_t;
/*********/

/****s* OpenSM: MAD Wrapper/osm_mad_addr_t
* NAME
*   osm_mad_addr_t
*
* DESCRIPTION
*
* SYNOPSIS
*/
typedef struct osm_mad_addr {
	ib_net16_t dest_lid;
	uint8_t path_bits;
	uint8_t static_rate;
	union addr_type {
		struct _smi {
			ib_net16_t source_lid;
			uint8_t port_num;
		} smi;

		struct _gsi {
			ib_net32_t remote_qp;
			ib_net32_t remote_qkey;
			uint16_t pkey_ix;
			uint8_t service_level;
			boolean_t global_route;
			ib_grh_t grh_info;
		} gsi;
	} addr_type;
} osm_mad_addr_t;
/*
* FIELDS
*
* SEE ALSO
*********/

/****s* OpenSM: MAD Wrapper/osm_madw_t
* NAME
*	osm_madw_t
*
* DESCRIPTION
*	Context needed for processing individual MADs
*
* SYNOPSIS
*/
typedef struct osm_madw {
	cl_list_item_t list_item;
	osm_bind_handle_t h_bind;
	osm_vend_wrap_t vend_wrap;
	osm_mad_addr_t mad_addr;
	osm_bind_info_t bind_info;
	osm_madw_context_t context;
	uint32_t mad_size;
	ib_api_status_t status;
	cl_disp_msgid_t fail_msg;
	boolean_t resp_expected;
	const ib_mad_t *p_mad;
} osm_madw_t;
/*
* FIELDS
*	list_item
*		List linkage for lists.  MUST BE FIRST MEMBER!
*
*	h_bind
*		Bind handle for the port on which this MAD will be sent
*		or was received.
*
*	vend_wrap
*		Transport vendor specific context.  This structure is not
*		used outside MAD transport vendor specific code.
*
*	context
*		Union of controller specific contexts needed for this MAD.
*		This structure allows controllers to indirectly communicate
*		with each other through the dispatcher.
*
*	mad_size
*		Size of this MAD in bytes.
*
*	status
*		Status of completed operation on the MAD.
*		CL_SUCCESS if the operation was successful.
*
*	fail_msg
*		Dispatcher message with which to post this MAD on failure.
*		This value is set by the originator of the MAD.
*		If an operation on this MAD fails, for example due to a timeout,
*		then the transport layer will dispose of the MAD by sending
*		it through the Dispatcher with this message type.  Presumably,
*		there is a controller listening for the failure message that can
*		properly clean up.
*
*	resp_expected
*		TRUE if a response is expected to this MAD.
*		FALSE otherwise.
*
*	p_mad
*		Pointer to the wire MAD.  The MAD itself cannot be part of the
*		wrapper, since wire MADs typically reside in special memory
*		registered with the local HCA.
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_init
* NAME
*	osm_madw_init
*
* DESCRIPTION
*	Initializes a MAD Wrapper object for use.
*
* SYNOPSIS
*/
static inline void osm_madw_init(IN osm_madw_t * p_madw,
				 IN osm_bind_handle_t h_bind,
				 IN uint32_t mad_size,
				 IN const osm_mad_addr_t * p_mad_addr)
{
	memset(p_madw, 0, sizeof(*p_madw));
	p_madw->h_bind = h_bind;
	p_madw->fail_msg = CL_DISP_MSGID_NONE;
	p_madw->mad_size = mad_size;
	if (p_mad_addr)
		p_madw->mad_addr = *p_mad_addr;
	p_madw->resp_expected = FALSE;
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object to initialize.
*
*	h_bind
*		[in] Pointer to the wire MAD.
*
*	p_mad_addr
*		[in] Pointer to the MAD address structure.  This parameter may
*		be NULL for directed route MADs.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_smp_ptr
* NAME
*	osm_madw_get_smp_ptr
*
* DESCRIPTION
*	Gets a pointer to the SMP in this MAD.
*
* SYNOPSIS
*/
static inline ib_smp_t *osm_madw_get_smp_ptr(IN const osm_madw_t * p_madw)
{
	return ((ib_smp_t *) p_madw->p_mad);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object to initialize.
*
* RETURN VALUES
*	Pointer to the start of the SMP MAD.
*
* NOTES
*
* SEE ALSO
*	MAD Wrapper object
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_sa_mad_ptr
* NAME
*	osm_madw_get_sa_mad_ptr
*
* DESCRIPTION
*	Gets a pointer to the SA MAD in this MAD wrapper.
*
* SYNOPSIS
*/
static inline ib_sa_mad_t *osm_madw_get_sa_mad_ptr(IN const osm_madw_t * p_madw)
{
	return ((ib_sa_mad_t *) p_madw->p_mad);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the SA MAD.
*
* NOTES
*
* SEE ALSO
*	MAD Wrapper object
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_perfmgt_mad_ptr
* DESCRIPTION
*	Gets a pointer to the PerfMgt MAD in this MAD wrapper.
*
* SYNOPSIS
*/
static inline ib_perfmgt_mad_t *osm_madw_get_perfmgt_mad_ptr(IN const osm_madw_t
							     * p_madw)
{
	return ((ib_perfmgt_mad_t *) p_madw->p_mad);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the PerfMgt MAD.
*
* NOTES
*
* SEE ALSO
*	MAD Wrapper object
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_cc_mad_ptr
* DESCRIPTION
*	Gets a pointer to the Congestion Control MAD in this MAD wrapper.
*
* SYNOPSIS
*/
static inline ib_cc_mad_t *osm_madw_get_cc_mad_ptr(IN const osm_madw_t
						   * p_madw)
{
	return ((ib_cc_mad_t *) p_madw->p_mad);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the Congestion Control MAD.
*
* NOTES
*
* SEE ALSO
*	MAD Wrapper object
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_ni_context_ptr
* NAME
*	osm_madw_get_ni_context_ptr
*
* DESCRIPTION
*	Gets a pointer to the NodeInfo context in this MAD.
*
* SYNOPSIS
*/
static inline osm_ni_context_t *osm_madw_get_ni_context_ptr(IN const osm_madw_t
							    * p_madw)
{
	return ((osm_ni_context_t *) & p_madw->context);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the context structure.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_pi_context_ptr
* NAME
*	osm_madw_get_pi_context_ptr
*
* DESCRIPTION
*	Gets a pointer to the PortInfo context in this MAD.
*
* SYNOPSIS
*/
static inline osm_pi_context_t *osm_madw_get_pi_context_ptr(IN const osm_madw_t
							    * p_madw)
{
	return ((osm_pi_context_t *) & p_madw->context);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the context structure.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_gi_context_ptr
* NAME
*	osm_madw_get_gi_context_ptr
*
* DESCRIPTION
*	Gets a pointer to the GUIDInfo context in this MAD.
*
* SYNOPSIS
*/
static inline osm_gi_context_t *osm_madw_get_gi_context_ptr(IN const osm_madw_t
							    * p_madw)
{
	return ((osm_gi_context_t *) & p_madw->context);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the context structure.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_nd_context_ptr
* NAME
*	osm_madw_get_nd_context_ptr
*
* DESCRIPTION
*	Gets a pointer to the NodeDescription context in this MAD.
*
* SYNOPSIS
*/
static inline osm_nd_context_t *osm_madw_get_nd_context_ptr(IN const osm_madw_t
							    * p_madw)
{
	return ((osm_nd_context_t *) & p_madw->context);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the context structure.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_lft_context_ptr
* NAME
*	osm_madw_get_lft_context_ptr
*
* DESCRIPTION
*	Gets a pointer to the LFT context in this MAD.
*
* SYNOPSIS
*/
static inline osm_lft_context_t *osm_madw_get_lft_context_ptr(IN const
							      osm_madw_t *
							      p_madw)
{
	return ((osm_lft_context_t *) & p_madw->context);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the context structure.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_mft_context_ptr
* NAME
*	osm_madw_get_mft_context_ptr
*
* DESCRIPTION
*	Gets a pointer to the MFT context in this MAD.
*
* SYNOPSIS
*/
static inline osm_mft_context_t *osm_madw_get_mft_context_ptr(IN const
							      osm_madw_t *
							      p_madw)
{
	return ((osm_mft_context_t *) & p_madw->context);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the context structure.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_si_context_ptr
* NAME
*	osm_madw_get_si_context_ptr
*
* DESCRIPTION
*	Gets a pointer to the SwitchInfo context in this MAD.
*
* SYNOPSIS
*/
static inline osm_si_context_t *osm_madw_get_si_context_ptr(IN const osm_madw_t
							    * p_madw)
{
	return ((osm_si_context_t *) & p_madw->context);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the context structure.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_smi_context_ptr
* NAME
*	osm_madw_get_smi_context_ptr
*
* DESCRIPTION
*	Gets a pointer to the SMInfo context in this MAD.
*
* SYNOPSIS
*/
static inline osm_smi_context_t *osm_madw_get_smi_context_ptr(IN const
							      osm_madw_t *
							      p_madw)
{
	return ((osm_smi_context_t *) & p_madw->context);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the context structure.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_pkey_context_ptr
* NAME
*	osm_madw_get_pkey_context_ptr
*
* DESCRIPTION
*	Gets a pointer to the P_Key context in this MAD.
*
* SYNOPSIS
*/
static inline osm_pkey_context_t *osm_madw_get_pkey_context_ptr(IN const
								osm_madw_t *
								p_madw)
{
	return ((osm_pkey_context_t *) & p_madw->context);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the context structure.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_slvl_context_ptr
* NAME
*	osm_madw_get_slvl_context_ptr
*
* DESCRIPTION
*	Gets a pointer to the PortInfo context in this MAD.
*
* SYNOPSIS
*/
static inline osm_slvl_context_t *osm_madw_get_slvl_context_ptr(IN const
								osm_madw_t *
								p_madw)
{
	return ((osm_slvl_context_t *) & p_madw->context);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the context structure.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_vla_context_ptr
* NAME
*	osm_madw_get_vla_context_ptr
*
* DESCRIPTION
*	Gets a pointer to the Vl Arb context in this MAD.
*
* SYNOPSIS
*/
static inline osm_vla_context_t *osm_madw_get_vla_context_ptr(IN const
							      osm_madw_t *
							      p_madw)
{
	return ((osm_vla_context_t *) & p_madw->context);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the context structure.
*
* NOTES
*
* SEE ALSO
*********/

#ifndef OSM_VENDOR_INTF_OPENIB
/****f* OpenSM: MAD Wrapper/osm_madw_get_arbitrary_context_ptr
* NAME
*	osm_madw_get_arbitrary_context_ptr
*
* DESCRIPTION
*	Gets a pointer to the arbitrary context in this MAD.
*
* SYNOPSIS
*/
static inline osm_arbitrary_context_t *osm_madw_get_arbitrary_context_ptr(IN
									  const
									  osm_madw_t
									  *
									  const
									  p_madw)
{
	return ((osm_arbitrary_context_t *) & p_madw->context);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Pointer to the start of the context structure.
*
* NOTES
*
* SEE ALSO
*********/
#endif

/****f* OpenSM: MAD Wrapper/osm_madw_get_vend_ptr
* NAME
*	osm_madw_get_vend_ptr
*
* DESCRIPTION
*	Gets a pointer to the vendor specific MAD wrapper component.
*
* SYNOPSIS
*/
static inline osm_vend_wrap_t *osm_madw_get_vend_ptr(IN const osm_madw_t *
						     p_madw)
{
	return ((osm_vend_wrap_t *) & p_madw->vend_wrap);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Gets a pointer to the vendor specific MAD wrapper component.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_bind_handle
* NAME
*	osm_madw_get_bind_handle
*
* DESCRIPTION
*	Returns the bind handle associated with this MAD.
*
* SYNOPSIS
*/
static inline osm_bind_handle_t
osm_madw_get_bind_handle(IN const osm_madw_t * p_madw)
{
	return ((osm_bind_handle_t) p_madw->h_bind);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Returns the bind handle associated with this MAD.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_mad_addr_ptr
* NAME
*	osm_madw_get_mad_addr_ptr
*
* DESCRIPTION
*	Returns the mad address structure associated with this MAD.
*
* SYNOPSIS
*/
static inline osm_mad_addr_t *osm_madw_get_mad_addr_ptr(IN const osm_madw_t *
							p_madw)
{
	return ((osm_mad_addr_t *) & p_madw->mad_addr);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Returns the mad address structure associated with this MAD.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_mad_ptr
* NAME
*	osm_madw_get_mad_ptr
*
* DESCRIPTION
*	Returns the mad address structure associated with this MAD.
*
* SYNOPSIS
*/
static inline ib_mad_t *osm_madw_get_mad_ptr(IN const osm_madw_t * p_madw)
{
	return ((ib_mad_t *) p_madw->p_mad);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Returns the mad address structure associated with this MAD.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_get_err_msg
* NAME
*	osm_madw_get_err_msg
*
* DESCRIPTION
*	Returns the message with which to post this mad wrapper if
*	an error occurs during processing the mad.
*
* SYNOPSIS
*/
static inline cl_disp_msgid_t osm_madw_get_err_msg(IN const osm_madw_t * p_madw)
{
	return ((cl_disp_msgid_t) p_madw->fail_msg);
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
* RETURN VALUES
*	Returns the message with which to post this mad wrapper if
*	an error occurs during processing the mad.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_set_mad
* NAME
*	osm_madw_set_mad
*
* DESCRIPTION
*	Associates a wire MAD with this MAD Wrapper object.
*
* SYNOPSIS
*/
static inline void osm_madw_set_mad(IN osm_madw_t * p_madw,
				    IN const ib_mad_t * p_mad)
{
	p_madw->p_mad = p_mad;
}

/*
* PARAMETERS
*	p_madw
*		[in] Pointer to an osm_madw_t object.
*
*	p_mad
*		[in] Pointer to the wire MAD to attach to this wrapper.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: MAD Wrapper/osm_madw_copy_context
* NAME
*	osm_madw_copy_context
*
* DESCRIPTION
*	Copies the controller context from one MAD Wrapper to another.
*
* SYNOPSIS
*/
static inline void osm_madw_copy_context(IN osm_madw_t * p_dest,
					 IN const osm_madw_t * p_src)
{
	p_dest->context = p_src->context;
}

/*
* PARAMETERS
*	p_dest
*		[in] Pointer to the destination osm_madw_t object.
*
*	p_src
*		[in] Pointer to the source osm_madw_t object.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

END_C_DECLS
#endif				/* _OSM_MADW_H_ */
