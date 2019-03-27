/*
 * Copyright (c) 2004-2006 Voltaire, Inc. All rights reserved.
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
 * 	Specification of the OpenSM SA Client API. This API uses the basic osm
 *    vendor API to provide SA Client interface.
 */

#ifndef _OSM_VENDOR_SA_API_H_
#define _OSM_VENDOR_SA_API_H_

#include <iba/ib_types.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****d* OpenSM Vendor SA Client/osmv_flags_t
* NAME
*	osmv_flags_t
*
* DESCRIPTION
*	Access layer flags used to direct the operation of various calls.
*
* SYNOPSIS
*/
typedef uint32_t osmv_flags_t;
#define OSM_SA_FLAGS_SYNC				0x00000001
/*
* VALUES
*	OSM_SA_FLAGS_SYNC
*		Indicates that the given operation should be performed synchronously.
*		The call will block until it completes.  Callbacks will still be
*		invoked.
*
* SEE ALSO
*  osmv_query_sa
*****/

/****d* OpenSM Vendor SA Client/osmv_query_type_t
* NAME
*	osmv_query_type_t
*
* DESCRIPTION
*	Abstracted queries supported by the access layer.
*
* SYNOPSIS
*/
typedef enum _osmv_query_type {
	OSMV_QUERY_USER_DEFINED,

	OSMV_QUERY_ALL_SVC_RECS,
	OSMV_QUERY_SVC_REC_BY_NAME,
	OSMV_QUERY_SVC_REC_BY_ID,

	OSMV_QUERY_CLASS_PORT_INFO,

	OSMV_QUERY_NODE_REC_BY_NODE_GUID,
	OSMV_QUERY_PORT_REC_BY_LID,
	OSMV_QUERY_PORT_REC_BY_LID_AND_NUM,

	OSMV_QUERY_VLARB_BY_LID_PORT_BLOCK,
	OSMV_QUERY_SLVL_BY_LID_AND_PORTS,

	OSMV_QUERY_PATH_REC_BY_PORT_GUIDS,
	OSMV_QUERY_PATH_REC_BY_GIDS,
	OSMV_QUERY_PATH_REC_BY_LIDS,

	OSMV_QUERY_UD_MULTICAST_SET,
	OSMV_QUERY_UD_MULTICAST_DELETE,

	OSMV_QUERY_MULTIPATH_REC,

} osmv_query_type_t;
/*
* VALUES
*	OSMV_QUERY_USER_DEFINED
*		Query the SA based on user-defined input.  Queries of this type
*		should reference an osmv_user_query_t structure as input to the
*		query.
*
*	OSMV_QUERY_SVC_REC_BY_NAME
*		Query for service records based on the service name.  Queries of
*		this type should reference an ib_svc_name_t structure as input
*		to the query.
*
*	OSMV_QUERY_SVC_REC_BY_ID
*		Query for service records based on the service ID.  Queries of
*		this type should reference an ib_net64_t value that indicates
*		the ID of the service being requested.
*
*	OSMV_QUERY_NODE_REC_BY_NODE_GUID
*		Query for node information based on the node's GUID.  Queries of
*		this type should reference an ib_net64_t value that indicates
*		the GUID of the node being requested.
*
*	OSMV_QUERY_PORT_REC_BY_LID
*		Query for port information based on the port's base LID. Queries
*		of this type should reference an ib_net16_t value that indicates
*		the base LID of the port being requested.
*
*	OSMV_QUERY_PORT_REC_BY_LID_AND_NUM
*		Query for port information based on the port's LID and port num.
*		Queries of this type should reference an osmv_user_query_t
*		structure as input to the query. The port num and lid should
*		be provided by it.
*
*	OSMV_QUERY_PATH_REC_BY_PORT_GUIDS
*		Query for path records between the specified pair of port GUIDs.
*		Queries of this type should reference an osmv_guid_pair_t
*		structure that indicates the GUIDs of the path being requested.
*
*	OSMV_QUERY_PATH_REC_BY_GIDS
*		Query for path records between the specified pair of port GIDs.
*		Queries of this type should reference an osmv_gid_pair_t
*		structure that indicates the GIDs of the path being requested.
*
*	OSMV_QUERY_PATH_REC_BY_LIDS
*		Query for path records between the specified pair of port LIDs.
*		Queries of this type should reference an osmv_lid_pair_t
*		structure that indicates the LIDs of the path being requested.
*
* NOTES
*	This enum is used to define abstracted queries provided by the access
*	layer.  Users may issue queries not listed here by sending MADs directly
*	to subnet administration or a class manager.  These queries are
*	intended to represent those most often used by clients.
*
* SEE ALSO
*	osmv_query, osmv_query_req_t, osmv_user_query_t, osmv_gid_pair_t,
*	osmv_lid_pair_t osmv_guid_pair_t
*****/

/****s* OpenSM Vendor SA Client/osmv_user_query_t
* NAME
*	osmv_user_query_t
*
* DESCRIPTION
*	User-defined query information.
*
* SYNOPSIS
*/
typedef struct _osmv_user_query {
	uint8_t method;
	ib_net16_t attr_id;
	ib_net16_t attr_offset;
	ib_net32_t attr_mod;
	ib_net64_t comp_mask;
	void *p_attr;
} osmv_user_query_t;
/*
* FIELDS
*
*	method
*		Method to be used
*
*	attr_id
*		Attribute identifier of query data.
*
*	attr_offset
*		Size of the query attribute, in 8-byte words.  Users can set
*		this value by passing in the sizeof( attribute ) into the
*		ib_get_attr_offset() routine.
*
*	attr_mod
*		Attribute modifier for query request.
*
*	comp_mask
*		Indicates the attribute components that are specified for the
*		query.
*
*	p_attr
*		References the attribute structure used as input into the query.
*		This field is ignored if comp_mask is set to 0.
*
* NOTES
*	This structure is used to describe a user-defined query.  The attribute
*	ID, attribute offset, component mask, and attribute structure must match
*	those defined by the IBA specification.  Users should refer to chapter
*	15 of the IBA specification for additional details.
*
* SEE ALSO
*	osmv_query_type_t, ib_get_attr_offset, ib_get_attr_size, osmv_query_sa
*****/

/****s* OpenSM Vendor SA Client/osmv_gid_pair_t
* NAME
*	osmv_gid_pair_t
*
* DESCRIPTION
*	Source and destination GIDs.
*
* SYNOPSIS
*/
typedef struct _osmv_gid_pair {
	ib_gid_t src_gid;
	ib_gid_t dest_gid;
} osmv_gid_pair_t;
/*
* FIELDS
*	src_gid
*		Source GID of a path.
*
*	dest_gid
*		Destination GID of a path.
*
* NOTES
*	This structure is used to describe the endpoints of a path.
*
* SEE ALSO
*	ib_gid_t
*****/

/****s* OpenSM Vendor SA Client/osmv_lid_pair_t
* NAME
*	osmv_lid_pair_t
*
* DESCRIPTION
*	Source and destination LIDs.
*
* SYNOPSIS
*/
typedef struct _osmv_lid_pair {
	ib_net16_t src_lid;
	ib_net16_t dest_lid;
} osmv_lid_pair_t;
/*
* FIELDS
*	src_lid
*		Source LID of a path.
*
*	dest_lid
*		Destination LID of a path.
*
* NOTES
*	This structure is used to describe the endpoints of a path.
*****/

/****s* OpenSM Vendor SA Client/osmv_guid_pair_t
* NAME
*	osmv_guid_pair_t
*
* DESCRIPTION
*	Source and destination GUIDs.  These may be port or channel adapter
*	GUIDs, depending on the context in which this structure is used.
*
* SYNOPSIS
*/
typedef struct _osmv_guid_pair {
	ib_net64_t src_guid;
	ib_net64_t dest_guid;
} osmv_guid_pair_t;
/*
* FIELDS
*	src_guid
*		Source GUID of a path.
*
*	dest_guid
*		Destination GUID of a path.
*
* NOTES
*	This structure is used to describe the endpoints of a path.  The given
*	GUID pair may belong to either ports or channel adapters.
*
* SEE ALSO
*	ib_guid_t
*****/

/****s* OpenSM Vendor SA Client/osmv_multipath_req_t
* NAME
*       osmv_multipath_req_t
*
* DESCRIPTION
*       Fields from which to generate a MultiPathRecord request.
*
* SYNOPSIS
*/
typedef struct _osmv_multipath_req_t {
	ib_net64_t comp_mask;
	uint16_t pkey;
	boolean_t reversible;
	uint8_t num_path;
	uint8_t sl;
	uint8_t independence;
	uint8_t sgid_count;
	uint8_t dgid_count;
	ib_gid_t gids[IB_MULTIPATH_MAX_GIDS];
} osmv_multipath_req_t;
/*
* FIELDS
*
* NOTES
*       This structure is used to describe a multipath request.
*
* SEE ALSO
*****/

/****s* OpenSM Vendor SA Client/osmv_query_res_t
* NAME
*	osmv_query_res_t
*
* DESCRIPTION
*	Contains the results of a subnet administration query.
*
* SYNOPSIS
*/
typedef struct _osmv_query_res {
	const void *query_context;
	ib_api_status_t status;
	osmv_query_type_t query_type;
	uint32_t result_cnt;
	osm_madw_t *p_result_madw;
} osmv_query_res_t;
/*
* FIELDS
*	query_context
*		User-defined context information associated with the query
*		through the osm_vendor_query_sa call.
*
*	status
*		Indicates the success of the query operation.
*
*	query_type
*		Indicates the type of query for which the results are being
*		returned.  This matches the query_type specified through the
*               osm_vendor_query_sa call.
*
*	result_cnt
*		The number of result structures that were returned by the query.
*
*	p_result_madw
*		For queries returning IB_SUCCESS or IB_REMOTE_ERROR, this
*		references the MAD wrapper returned by subnet administration
*		containing the list of results or the returned error code.
*
* NOTES
*	A query result structure is returned to a client through their
*	osmv_pfn_query_cb_t routine to notify them of the results of a subnet
*	administration query.  If the query was successful or received an error
*	from subnet administration, p_result_madw will reference a MAD wrapper
*	containing the results.  The MAD referenced by p_result_madw is owned by
*	the user and remains available even after their callback returns.  Users
*	must call osm_mad_pool_put() to return the MAD wrapper back to the
*	mad pool when they are done accessing the results.
*
*	To retrieve individual result structures from the p_result_madw, users
*	may call osmv_get_query_result().
*
* SEE ALSO
*	osmv_query_sa, osmv_pfn_query_cb_t, ib_api_status_t,
*	osmv_query_status_t, osmv_query_type_t,
*	osmv_get_query_result
*****/

/****f* OpenSM Vendor SA Client/osmv_get_query_result
* NAME
*	osmv_get_query_result
*
* DESCRIPTION
*	Retrieves a result structure from a MADW returned by a call to
*	osmv_query_sa().
*
* SYNOPSIS
*/
static inline void *osmv_get_query_result(IN osm_madw_t * p_result_madw,
					  IN uint32_t result_index)
{
	ib_sa_mad_t *p_sa_mad;

	CL_ASSERT(p_result_madw);
	p_sa_mad = (ib_sa_mad_t *) osm_madw_get_mad_ptr(p_result_madw);
	CL_ASSERT(p_sa_mad);
	CL_ASSERT(ib_get_attr_size(p_sa_mad->attr_offset) * (result_index + 1) +
		  IB_SA_MAD_HDR_SIZE <= p_result_madw->mad_size);

	return (p_sa_mad->data +
		(ib_get_attr_size(p_sa_mad->attr_offset) * result_index));
}

/*
* PARAMETERS
*	p_result_madw
*		[in] This is a reference to the MAD returned as a result of the
*		query.
*
*	result_index
*		[in] A zero-based index indicating which result to return.
*
* NOTES
*	This call returns a pointer to the start of a result structure from a
*	call to osmv_query_sa().  The type of result structure must be known to
*	the user either through the user's context or the query_type returned as
*	part of the osmv_query_res_t structure.
*
* SEE ALSO
*	osmv_query_res_t, osm_madw_t
*****/

/****f* OpenSM Vendor SA Client/osmv_get_query_path_rec
* NAME
*	osmv_get_query_path_rec
*
* DESCRIPTION
*	Retrieves a path record result from a MAD returned by a call to
*	osmv_query_sa().
*
* SYNOPSIS
*/
static inline ib_path_rec_t *osmv_get_query_path_rec(IN osm_madw_t *
						     p_result_madw,
						     IN uint32_t result_index)
{
	ib_sa_mad_t __attribute__((__unused__)) *p_sa_mad;

	CL_ASSERT(p_result_madw);
	p_sa_mad = (ib_sa_mad_t *) osm_madw_get_mad_ptr(p_result_madw);
	CL_ASSERT(p_sa_mad && p_sa_mad->attr_id == IB_MAD_ATTR_PATH_RECORD);

	return ((ib_path_rec_t *)
		osmv_get_query_result(p_result_madw, result_index));
}

/*
* PARAMETERS
*	p_result_madw
*		[in] This is a reference to the MAD returned as a result of the
*		query.
*
*	result_index
*		[in] A zero-based index indicating which result to return.
*
* NOTES
*	This call returns a pointer to the start of a path record result from
*	a call to osmv_query_sa().
*
* SEE ALSO
*	osmv_query_res_t, osm_madw_t, osmv_get_query_result, ib_path_rec_t
*****/

/****f* OpenSM Vendor SA Client/osmv_get_query_portinfo_rec
* NAME
*	osmv_get_query_portinfo_rec
*
* DESCRIPTION
*	Retrieves a port info record result from a MAD returned by a call to
*	osmv_query_sa().
*
* SYNOPSIS
*/
static inline ib_portinfo_record_t *osmv_get_query_portinfo_rec(IN osm_madw_t *
								p_result_madw,
								IN uint32_t
								result_index)
{
	ib_sa_mad_t __attribute__((__unused__)) *p_sa_mad;

	CL_ASSERT(p_result_madw);
	p_sa_mad = (ib_sa_mad_t *) osm_madw_get_mad_ptr(p_result_madw);
	CL_ASSERT(p_sa_mad && p_sa_mad->attr_id == IB_MAD_ATTR_PORTINFO_RECORD);

	return ((ib_portinfo_record_t *) osmv_get_query_result(p_result_madw,
							       result_index));
}

/*
* PARAMETERS
*	p_result_madw
*		[in] This is a reference to the MAD returned as a result of the
*		query.
*
*	result_index
*		[in] A zero-based index indicating which result to return.
*
* NOTES
*	This call returns a pointer to the start of a port info record result
*	from a call to osmv_query_sa().
*
* SEE ALSO
*	osmv_query_res_t, osm_madw_t, osmv_get_query_result, ib_portinfo_record_t
*****/

/****f* OpenSM Vendor SA Client/osmv_get_query_node_rec
* NAME
*	osmv_get_query_node_rec
*
* DESCRIPTION
*	Retrieves a node record result from a MAD returned by a call to
*	osmv_query_sa().
*
* SYNOPSIS
*/
static inline ib_node_record_t *osmv_get_query_node_rec(IN osm_madw_t *
							p_result_madw,
							IN uint32_t
							result_index)
{
	ib_sa_mad_t __attribute__((__unused__)) *p_sa_mad;

	CL_ASSERT(p_result_madw);
	p_sa_mad = (ib_sa_mad_t *) osm_madw_get_mad_ptr(p_result_madw);
	CL_ASSERT(p_sa_mad && p_sa_mad->attr_id == IB_MAD_ATTR_NODE_RECORD);

	return ((ib_node_record_t *) osmv_get_query_result(p_result_madw,
							   result_index));
}

/*
* PARAMETERS
*	p_result_madw
*		[in] This is a reference to the MAD returned as a result of the
*		query.
*
*	result_index
*		[in] A zero-based index indicating which result to return.
*
* NOTES
*	This call returns a pointer to the start of a node record result from
*	a call to osmv_query_sa().
*
* SEE ALSO
*	osmv_query_res_t, osm_madw_t, osmv_get_query_result, ib_node_record_t
*****/

/****f* OpenSM Vendor SA Client/osmv_get_query_svc_rec
* NAME
*	osmv_get_query_svc_rec
*
* DESCRIPTION
*	Retrieves a service record result from a MAD returned by a call to
*	osmv_query_sa().
*
* SYNOPSIS
*/
static inline ib_service_record_t *osmv_get_query_svc_rec(IN osm_madw_t *
							  p_result_madw,
							  IN uint32_t
							  result_index)
{
	ib_sa_mad_t __attribute__((__unused__)) *p_sa_mad;

	CL_ASSERT(p_result_madw);
	p_sa_mad = (ib_sa_mad_t *) osm_madw_get_mad_ptr(p_result_madw);
	CL_ASSERT(p_sa_mad && p_sa_mad->attr_id == IB_MAD_ATTR_SERVICE_RECORD);

	return ((ib_service_record_t *) osmv_get_query_result(p_result_madw,
							      result_index));
}

/*
* PARAMETERS
*	p_result_madw
*		[in] This is a reference to the MAD returned as a result of the
*		query.
*
*	result_index
*		[in] A zero-based index indicating which result to return.
*
* NOTES
*	This call returns a pointer to the start of a service record result from
*	a call to osmv_query_sa().
*
* SEE ALSO
*	osmv_query_res_t, osm_madw_t, osmv_get_query_result, ib_service_record_t
*****/

/****f* OpenSM Vendor SA Client/osmv_get_query_mc_rec
* NAME
*	osmv_get_query_mc_rec
*
* DESCRIPTION
*	Retrieves a multicast record result from a MAD returned by a call to
*	osmv_query_sa().
*
* SYNOPSIS
*/
static inline ib_member_rec_t *osmv_get_query_mc_rec(IN osm_madw_t *
						     p_result_madw,
						     IN uint32_t result_index)
{
	ib_sa_mad_t __attribute__((__unused__)) *p_sa_mad;

	CL_ASSERT(p_result_madw);
	p_sa_mad = (ib_sa_mad_t *) osm_madw_get_mad_ptr(p_result_madw);
	CL_ASSERT(p_sa_mad && p_sa_mad->attr_id == IB_MAD_ATTR_MCMEMBER_RECORD);

	return ((ib_member_rec_t *) osmv_get_query_result(p_result_madw,
							  result_index));
}

/*
* PARAMETERS
*	p_result_madw
*		[in] This is a reference to the MAD returned as a result of the
*		query.
*
*	result_index
*		[in] A zero-based index indicating which result to return.
*
* NOTES
*	This call returns a pointer to the start of a service record result from
*	a call to osmv_query_sa().
*
* SEE ALSO
*	osmv_query_res_t, osm_madw_t, osmv_get_query_result, ib_member_rec_t
*****/

/****f* OpenSM Vendor SA Client/osmv_get_query_inform_info_rec
* NAME
*	osmv_get_query_inform_info_rec
*
* DESCRIPTION
*	Retrieves an InformInfo record result from a MAD returned by
*	a call to osmv_query_sa().
*
* SYNOPSIS
*/
static inline ib_inform_info_record_t *osmv_get_query_inform_info_rec(IN
								      osm_madw_t
								      *
								      p_result_madw,
								      IN
								      uint32_t
								      result_index)
{
	ib_sa_mad_t __attribute__((__unused__)) *p_sa_mad;

	CL_ASSERT(p_result_madw);
	p_sa_mad = (ib_sa_mad_t *) osm_madw_get_mad_ptr(p_result_madw);
	CL_ASSERT(p_sa_mad
		  && p_sa_mad->attr_id == IB_MAD_ATTR_INFORM_INFO_RECORD);

	return ((ib_inform_info_record_t *) osmv_get_query_result(p_result_madw,
								  result_index));
}

/*
* PARAMETERS
*	p_result_madw
*		[in] This is a reference to the MAD returned as a result of the
*		query.
*
*	result_index
*		[in] A zero-based index indicating which result to return.
*
* NOTES
*	This call returns a pointer to the start of a service record result from
*	a call to osmv_query_sa().
*
* SEE ALSO
*	osmv_query_res_t, osm_madw_t, osmv_get_query_result, ib_inform_info_record_t
*****/

/****f* OpenSM Vendor SA Client/osmv_pfn_query_cb_t
* NAME
*	osmv_pfn_query_cb_t
*
* DESCRIPTION
*	User-defined callback invoked on completion of subnet administration
*	query.
*
* SYNOPSIS
*/
typedef void
 (*osmv_pfn_query_cb_t) (IN osmv_query_res_t * p_query_res);
/*
* PARAMETERS
*	p_query_res
*		[in] This is a reference to a structure containing the result of
*		the query.
*
* NOTES
*	This routine is invoked to notify a client of the result of a subnet
*	administration query.  The p_query_rec parameter references the result
*	of the query and, in the case of a successful query, any information
*	returned by subnet administration.
*
*	In the kernel, this callback is usually invoked using a tasklet,
*	dependent on the implementation of the underlying verbs provider driver.
*
* SEE ALSO
*	osmv_query_res_t
*****/

/****s* OpenSM Vendor SA Client/osmv_query_req_t
* NAME
*	osmv_query_req_t
*
* DESCRIPTION
*	Information used to request an access layer provided query of subnet
*	administration.
*
* SYNOPSIS
*/
typedef struct _osmv_query_req {
	osmv_query_type_t query_type;
	const void *p_query_input;
	ib_net64_t sm_key;

	uint32_t timeout_ms;
	uint32_t retry_cnt;
	osmv_flags_t flags;

	const void *query_context;
	osmv_pfn_query_cb_t pfn_query_cb;
	int with_grh;
	ib_gid_t gid;
} osmv_query_req_t;
/*
* FIELDS
*	query_type
*		Indicates the type of query that the access layer should
*		perform.
*
*	p_query_input
*		A pointer to the input for the query.  The data referenced by
*		this structure is dependent on the type of query being requested
*		and is determined by the specified query_type.
*
*	sm_key
*		The SM_Key to be provided with the SA MAD for authentication.
*		Normally 0 is used.
*
*	timeout_ms
*		Specifies the number of milliseconds to wait for a response for
*		this query until retrying or timing out the request.
*
*	retry_cnt
*		Specifies the number of times that the query will be retried
*		before failing the request.
*
*	flags
*		Used to describe the mode of operation.  Set to IB_FLAGS_SYNC to
*		process the called routine synchronously.
*
*	query_context
*		User-defined context information associated with this query.
*		The context data is returned to the user as a part of their
*		query callback.
*
*	pfn_query_cb
*		A user-defined callback that is invoked upon completion of the
*		query.
*
*	with_grh
*		Indicates that SA queries should be sent with GRH.
*
*	gid
*		Used to store the SM/SA GID.
*
* NOTES
*	This structure is used when requesting an osm vendor provided query
*	of subnet administration.  Clients specify the type of query through
*	the query_type field.  Based on the type of query, the p_query_input
*	field is set to reference the appropriate data structure.
*
*	The information referenced by the p_query_input field is one of the
*	following:
*
*		-- a NULL terminated service name
*		-- a service id
*		-- a single GUID
*		-- a pair of GUIDs specified through an osmv_guid_pair_t structure
*		-- a pair of GIDs specified through an osmv_gid_pair_t structure
*
* SEE ALSO
*	osmv_query_type_t, osmv_pfn_query_cb_t, osmv_guid_pair_t,
*	osmv_gid_pair_t
*****/

/****f* OpenSM Vendor SA Client/osmv_bind_sa
* NAME
*   osmv_bind_sa
*
* DESCRIPTION
*	Bind to the SA service and return a handle to be used for later
*  queries.
*
*
* SYNOPSIS
*/
osm_bind_handle_t
osmv_bind_sa(IN osm_vendor_t * const p_vend,
	     IN osm_mad_pool_t * const p_mad_pool, IN ib_net64_t port_guid);
/*
* PARAMETERS
*   p_vend
*	[in] an osm_vendor object to work with
*
*   p_mad_pool
*	[in] mad pool to obtain madw from
*
*   port_guid
*	[in] the port guid to attach to.
*
* RETURN VALUE
*	Bind handle to be used for later SA queries or OSM_BIND_INVALID_HANDLE
*
* NOTES
*
* SEE ALSO
* osmv_query_sa
*********/

/****f* OpenSM Vendor SA Client/osmv_query_sa
* NAME
*   osmv_query_sa
*
* DESCRIPTION
*   Query the SA given an SA query request (similar to IBAL ib_query).
*
* SYNOPSIS
*/
ib_api_status_t
osmv_query_sa(IN osm_bind_handle_t h_bind,
	      IN const osmv_query_req_t * const p_query_req);
/*
* PARAMETERS
*   h_bind
*	[in] bind handle for this port. Should be previously
*       obtained by calling osmv_bind_sa
*
*   p_query_req
*	[in] an SA query request structure.
*
* RETURN VALUE
*	IB_SUCCESS if completed successfuly (or in ASYNC mode
*	if the request was sent).
*
* NOTES
*
* SEE ALSO
* osmv_bind_sa
*********/

END_C_DECLS
#endif				/* _OSM_VENDOR_SA_API_H_ */
