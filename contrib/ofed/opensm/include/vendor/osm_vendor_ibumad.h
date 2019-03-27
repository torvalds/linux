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

#ifndef _OSM_VENDOR_UMAD_H_
#define _OSM_VENDOR_UMAD_H_

#include <iba/ib_types.h>
#include <complib/cl_qlist.h>
#include <opensm/osm_base.h>
#include <opensm/osm_log.h>

#include <infiniband/umad.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Vendor Access Layer (UMAD)
* NAME
*	Vendor UMAD
*
* DESCRIPTION
*	This file is the vendor specific file for the UMAD Infiniband API.
*
* AUTHOR
*
*
*********/
#define OSM_DEFAULT_RETRY_COUNT 3
#define OSM_UMAD_MAX_CAS	UMAD_MAX_DEVICES
#define OSM_UMAD_MAX_PORTS_PER_CA	2
#define OSM_UMAD_MAX_AGENTS	32

/****s* OpenSM: Vendor UMAD/osm_ca_info_t
* NAME
*   osm_ca_info_t
*
* DESCRIPTION
* 	Structure containing information about local Channel Adapters.
*
* SYNOPSIS
*/
typedef struct _osm_ca_info {
	ib_net64_t guid;
	size_t attr_size;
	ib_ca_attr_t *p_attr;
} osm_ca_info_t;
/*
* FIELDS
*	guid
*		Node GUID of the local CA.
*
*	attr_size
*		Size of the CA attributes for this CA.
*
*	p_attr
*		Pointer to dynamicly allocated CA Attribute structure.
*
* SEE ALSO
*********/

/****f* OpenSM: CA Info/osm_ca_info_get_num_ports
* NAME
*	osm_ca_info_get_num_ports
*
* DESCRIPTION
*	Returns the number of ports owned by this CA.
*
* SYNOPSIS
*/
static inline uint8_t
osm_ca_info_get_num_ports(IN const osm_ca_info_t * const p_ca_info)
{
	return (p_ca_info->p_attr->num_ports);
}

/*
* PARAMETERS
*	p_ca_info
*		[in] Pointer to a CA Info object.
*
* RETURN VUMADUE
*	Returns the number of ports owned by this CA.
*
* NOTES
*
* SEE ALSO
*********/

/****s* OpenSM: Vendor UMAD/osm_bind_handle_t
* NAME
*   osm_bind_handle_t
*
* DESCRIPTION
* 	handle returned by the vendor transport bind call.
*
* SYNOPSIS
*/
typedef void *osm_bind_handle_t;
/***********/

typedef struct _umad_match {
	ib_net64_t tid;
	void *v;
	uint32_t version;
	uint8_t mgmt_class;
} umad_match_t;

#define DEFAULT_OSM_UMAD_MAX_PENDING	1000

typedef struct vendor_match_tbl {
	uint32_t last_version;
	int max;
	umad_match_t *tbl;
} vendor_match_tbl_t;

typedef struct _osm_vendor {
	osm_log_t *p_log;
	uint32_t ca_count;
	osm_ca_info_t *p_ca_info;
	uint32_t timeout;
	int max_retries;
	osm_bind_handle_t agents[OSM_UMAD_MAX_AGENTS];
	char ca_names[OSM_UMAD_MAX_CAS][UMAD_CA_NAME_LEN];
	vendor_match_tbl_t mtbl;
	umad_port_t umad_port;
	pthread_mutex_t cb_mutex;
	pthread_mutex_t match_tbl_mutex;
	int umad_port_id;
	void *receiver;
	int issmfd;
	char issm_path[256];
} osm_vendor_t;

#define OSM_BIND_INVALID_HANDLE 0

typedef struct _osm_vend_wrap {
	int agent;
	int size;
	int retries;
	void *umad;
	osm_bind_handle_t h_bind;
} osm_vend_wrap_t;

END_C_DECLS
#endif				/* _OSM_VENDOR_UMAD_H_ */
