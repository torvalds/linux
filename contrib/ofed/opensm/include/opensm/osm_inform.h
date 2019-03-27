/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
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
 * 	Declaration of osm_inform_rec_t.
 *	This object represents an IBA Inform Record.
 *	This object is part of the OpenSM family of objects.
 *
 * Author:
 *    Eitan Zahavi, Mellanox
 */

#ifndef _OSM_INFR_H_
#define _OSM_INFR_H_

#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_spinlock.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_sa.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Inform Record
* NAME
*	Inform Record
*
* DESCRIPTION
*	The Inform record encapsulates the information needed by the
*	SA to manage InformInfo registrations and sending Reports(Notice)
*	when SM receives Traps for registered LIDs.
*
*	The inform records is not thread safe, thus callers must provide
*	serialization.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*    Eitan Zahavi, Mellanox
*
*********/
/****s* OpenSM: Inform Record/osm_infr_t
* NAME
*	osm_infr_t
*
* DESCRIPTION
*	Inform Record structure.
*
*	The osm_infr_t object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_infr {
	cl_list_item_t list_item;
	osm_bind_handle_t h_bind;
	osm_sa_t *sa;
	osm_mad_addr_t report_addr;
	ib_inform_info_record_t inform_record;
} osm_infr_t;
/*
* FIELDS
*	list_item
*		List Item for qlist linkage.  Must be first element!!
*
*	h_bind
*		A handle of lower level mad srvc
*
*	sa
*		A pointer to osm_sa object
*
*	report_addr
*		Report address
*
*	inform_record
*		The Inform Info Record
*
* SEE ALSO
*********/

/****f* OpenSM: Inform Record/osm_infr_new
* NAME
*	osm_infr_new
*
* DESCRIPTION
*	Allocates and initializes a Inform Record for use.
*
* SYNOPSIS
*/
osm_infr_t *osm_infr_new(IN const osm_infr_t * p_infr_rec);
/*
* PARAMETERS
*	p_inf_rec
*		[in] Pointer to IB Inform Record
*
* RETURN VALUES
*	pointer to osm_infr_t structure.
*
* NOTES
*	Allows calling other inform record methods.
*
* SEE ALSO
*	Inform Record, osm_infr_delete
*********/

/****f* OpenSM: Inform Record/osm_infr_delete
* NAME
*	osm_infr_delete
*
* DESCRIPTION
*	Destroys and deallocates the osm_infr_t structure.
*
* SYNOPSIS
*/
void osm_infr_delete(IN osm_infr_t * p_infr);
/*
* PARAMETERS
*	p_infr
*		[in] Pointer to osm_infr_t structure
*
* SEE ALSO
*	Inform Record, osm_infr_new
*********/

/****f* OpenSM: Inform Record/osm_infr_get_by_rec
* NAME
*	osm_infr_get_by_rec
*
* DESCRIPTION
*	Find a matching osm_infr_t in the subnet DB by inform_info_record
*
* SYNOPSIS
*/
osm_infr_t *osm_infr_get_by_rec(IN osm_subn_t const *p_subn,
				IN osm_log_t * p_log,
				IN osm_infr_t * p_infr_rec);
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to the subnet object
*
*	p_log
*		[in] Pointer to the log object
*
*	p_inf_rec
*		[in] Pointer to an inform_info record
*
* RETURN
*	The matching osm_infr_t
* SEE ALSO
*	Inform Record, osm_infr_new, osm_infr_delete
*********/

void osm_infr_insert_to_db(IN osm_subn_t * p_subn, IN osm_log_t * p_log,
			   IN osm_infr_t * p_infr);

void osm_infr_remove_from_db(IN osm_subn_t * p_subn, IN osm_log_t * p_log,
			     IN osm_infr_t * p_infr);

/****f* OpenSM: Inform Record/osm_infr_remove_subscriptions
* NAME
*	osm_infr_remove_subscriptions
*
* DESCRIPTION
*	Remove all event subscriptions of a port
*
* SYNOPSIS
*/
ib_api_status_t
osm_infr_remove_subscriptions(IN osm_subn_t * p_subn, IN osm_log_t * p_log,
			      IN ib_net64_t port_guid);
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to the subnet object
*
*	p_log
*		[in] Pointer to the log object
*
*	port_guid
*		[in] PortGUID of the subscriber that should be removed
*
* RETURN
*	CL_SUCCESS if port_guid had any subscriptions being removed
*	CL_NOT_FOUND if port_guid did not have any active subscriptions
* SEE ALSO
*********/

/****f* OpenSM: Inform Record/osm_report_notice
* NAME
*	osm_report_notice
*
* DESCRIPTION
* Once a Trap was received by the osm_trap_rcv, or a Trap sourced in
* the SM was sent (Traps 64-67) this routine is called with a copy of
* the notice data.
* Given a notice attribute - compare and see if it matches the InformInfo
* Element and if it does - call the Report(Notice) for the
* target QP registered by the address stored in the InformInfo element
*
* SYNOPSIS
*/
ib_api_status_t osm_report_notice(IN osm_log_t * p_log, IN osm_subn_t * p_subn,
				  IN ib_mad_notice_attr_t * p_ntc);
/*
* PARAMETERS
*	p_rcv
*		[in] Pointer to the trap receiver
*
*	p_ntc
*		[in] Pointer to a copy of the incoming trap notice attribute.
*
* RETURN
*	IB_SUCCESS on good completion
*
* SEE ALSO
*	Inform Record, osm_trap_rcv
*********/

END_C_DECLS
#endif				/* _OSM_INFR_H_ */
