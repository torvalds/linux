/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
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
 * 	Provides interface over VAPI for obtaining the local ports guids or from guid
 *    obtaining the HCA and port number.
 */

#ifndef _OSM_VENDOR_HCA_GUID_H_
#define _OSM_VENDOR_HCA_GUID_H_

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****s* OpenSM: Vendor AL/osm_ca_info_t
* NAME
*   osm_ca_info_t
*
* DESCRIPTION
* 	Structure containing information about local Channle Adapters.
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

/****f* OpenSM: CA Info/osm_ca_info_get_port_guid
* NAME
*	osm_ca_info_get_port_guid
*
* DESCRIPTION
*	Returns the port GUID of the specified port owned by this CA.
*
* SYNOPSIS
*/
static inline ib_net64_t
osm_ca_info_get_port_guid(IN const osm_ca_info_t * const p_ca_info,
			  IN const uint8_t index)
{
	return (p_ca_info->p_attr->p_port_attr[index].port_guid);
}

/*
* PARAMETERS
*	p_ca_info
*		[in] Pointer to a CA Info object.
*
*	index
*		[in] Port "index" for which to retrieve the port GUID.
*		The index is the offset into the ca's internal array
*		of port attributes.
*
* RETURN VALUE
*	Returns the port GUID of the specified port owned by this CA.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: SM Vendor/osm_vendor_get_guid_ca_and_port
 * NAME
 *	osm_vendor_get_guid_ca_and_port
 *
 * DESCRIPTION
 * Given the vendor obj and a guid
 * return the ca id and port number that have that guid
 *
 * SYNOPSIS
 */
ib_api_status_t
osm_vendor_get_guid_ca_and_port(IN osm_vendor_t * const p_vend,
				IN ib_net64_t const guid,
				OUT VAPI_hca_id_t * p_hca_id,
				OUT uint32_t * p_port_num);

/*
* PARAMETERS
*	p_vend
*		[in] Pointer to an osm_vendor_t object.
*
*	guid
*		[in] The guid to search for.
*
*	p_hca_id
*		[out] The HCA Id (VAPI_hca_id_t *) that the port is found on.
*
*	p_port_num
*		[out] Pointer to a port number arg to be filled with the port number with the given guid.
*
* RETURN VALUES
*	IB_SUCCESS on SUCCESS
*  IB_INVALID_GUID if the guid is notfound on any Local HCA Port
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: SM Vendor/osm_vendor_get_all_port_attr
 * NAME
 *	osm_vendor_get_all_port_attr
 *
 * DESCRIPTION
 * Fill in the array of port_attr with all available ports on ALL the
 * avilable CAs on this machine.
 * ALSO -
 * UPDATE THE VENDOR OBJECT LIST OF CA_INFO STRUCTS
 *
 * SYNOPSIS
 */
ib_api_status_t osm_vendor_get_all_port_attr(IN osm_vendor_t * const p_vend,
					     IN ib_port_attr_t *
					     const p_attr_array,
					     IN uint32_t * const p_num_ports);

/*
* PARAMETERS
*	p_vend
*		[in] Pointer to an osm_vendor_t object.
*
*	p_attr_array
*		[out] Pre-allocated array of port attributes to be filled in
*
*	p_num_ports
*		[out] The size of the given array. Filled in by the actual numberof ports found.
*
* RETURN VALUES
*	IB_SUCCESS if OK
*  IB_INSUFFICIENT_MEMORY if not enough place for all ports was provided.
*
* NOTES
*
* SEE ALSO
*********/

END_C_DECLS
#endif				/*  _OSM_VENDOR_HCA_GUID_H_ */
