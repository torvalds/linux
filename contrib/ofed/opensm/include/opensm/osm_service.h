/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
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

#ifndef _OSM_SVCR_H_
#define _OSM_SVCR_H_

/*
 * Abstract:
 * 	Declaration of osm_service_rec_t.
 *	This object represents an IBA Service Record.
 *	This object is part of the OpenSM family of objects.
 */

#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_spinlock.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_log.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Service Record
* NAME
*	Service Record
*
* DESCRIPTION
*	The service record encapsulates the information needed by the
*	SA to manage service registrations.
*
*	The service records is not thread safe, thus callers must provide
*	serialization.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Anil S Keshavamurthy, Intel
*
*********/
/****s* OpenSM: Service Record/osm_svcr_t
* NAME
*	osm_svcr_t
*
* DESCRIPTION
*	Service Record structure.
*
*	The osm_svcr_t object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_svcr {
	cl_list_item_t list_item;
	ib_service_record_t service_record;
	uint32_t modified_time;
	uint32_t lease_period;
} osm_svcr_t;
/*
* FIELDS
*	map_item
*		Map Item for qmap linkage.  Must be first element!!
*
*	svc_rec
*		IB Service record structure
*
*	modified_time
*		Last modified time of this record in milliseconds
*
*	lease_period
*		Remaining lease period for this record
*
*
* SEE ALSO
*********/

/****f* OpenSM: Service Record/osm_svcr_new
* NAME
*	osm_svcr_new
*
* DESCRIPTION
*	Allocates and initializes a Service Record for use.
*
* SYNOPSIS
*/
osm_svcr_t *osm_svcr_new(IN const ib_service_record_t * p_svc_rec);
/*
* PARAMETERS
*	p_svc_rec
*		[in] Pointer to IB Service Record
*
* RETURN VALUES
*	pointer to osm_svcr_t structure.
*
* NOTES
*	Allows calling other service record methods.
*
* SEE ALSO
*	Service Record, osm_svcr_delete
*********/

/****f* OpenSM: Service Record/osm_svcr_init
* NAME
*	osm_svcr_init
*
* DESCRIPTION
*	Initializes the osm_svcr_t structure.
*
* SYNOPSIS
*/
void osm_svcr_init(IN osm_svcr_t * p_svcr,
		   IN const ib_service_record_t * p_svc_rec);
/*
* PARAMETERS
*	p_svc_rec
*		[in] Pointer to osm_svcr_t structure
*	p_svc_rec
*		[in] Pointer to the ib_service_record_t
*
* SEE ALSO
*	Service Record
*********/

/****f* OpenSM: Service Record/osm_svcr_delete
* NAME
*	osm_svcr_delete
*
* DESCRIPTION
*	Deallocates the osm_svcr_t structure.
*
* SYNOPSIS
*/
void osm_svcr_delete(IN osm_svcr_t * p_svcr);
/*
* PARAMETERS
*	p_svc_rec
*		[in] Pointer to osm_svcr_t structure
*
* SEE ALSO
*	Service Record, osm_svcr_new
*********/

osm_svcr_t *osm_svcr_get_by_rid(IN osm_subn_t const *p_subn,
				IN osm_log_t * p_log,
				IN ib_service_record_t * p_svc_rec);

void osm_svcr_insert_to_db(IN osm_subn_t * p_subn, IN osm_log_t * p_log,
			   IN osm_svcr_t * p_svcr);
void osm_svcr_remove_from_db(IN osm_subn_t * p_subn, IN osm_log_t * p_log,
			     IN osm_svcr_t * p_svcr);

END_C_DECLS
#endif				/* _OSM_SVCR_H_ */
