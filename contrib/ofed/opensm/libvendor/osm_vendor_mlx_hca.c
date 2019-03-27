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

#include <stdlib.h>
#include <string.h>
#if defined(OSM_VENDOR_INTF_MTL) | defined(OSM_VENDOR_INTF_TS)
#undef IN
#undef OUT
#include <vapi_types.h>
#include <evapi.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_log.h>
#include <stdlib.h>
#include <stdio.h>

/********************************************************************************
 *
 * Provide the functionality for selecting an HCA Port and Obtaining it's guid.
 *
 ********************************************************************************/

typedef struct _osm_ca_info {
	ib_net64_t guid;
	size_t attr_size;
	ib_ca_attr_t *p_attr;
} osm_ca_info_t;

/**********************************************************************
 * Convert the given GID to GUID by copy of it's upper 8 bytes
 **********************************************************************/
ib_api_status_t
__osm_vendor_gid_to_guid(IN u_int8_t * gid, OUT VAPI_gid_t * guid)
{
	memcpy(guid, gid + 8, 8);
	return (IB_SUCCESS);
}

/**********************************************************************
 * Returns a pointer to the port attribute of the specified port
 * owned by this CA.
 ************************************************************************/
static ib_port_attr_t *__osm_ca_info_get_port_attr_ptr(IN const osm_ca_info_t *
						       const p_ca_info,
						       IN const uint8_t index)
{
	return (&p_ca_info->p_attr->p_port_attr[index]);
}

/********************************************************************************
 * get the CA names available on the system
 * NOTE: user of this function needs to deallocate p_hca_ids after usage.
 ********************************************************************************/
static ib_api_status_t
__osm_vendor_get_ca_ids(IN osm_vendor_t * const p_vend,
			IN VAPI_hca_id_t ** const p_hca_ids,
			IN uint32_t * const p_num_guids)
{
	ib_api_status_t status;
	VAPI_ret_t vapi_res;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_hca_ids);
	CL_ASSERT(p_num_guids);

	/* first call is just to get the number */
	vapi_res = EVAPI_list_hcas(0, p_num_guids, NULL);

	/* fail ? */
	if (vapi_res == VAPI_EINVAL_PARAM) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_vendor_get_ca_ids: ERR 3D08: : "
			"Bad parameter in calling: EVAPI_list_hcas. (%d)\n",
			vapi_res);
		status = IB_ERROR;
		goto Exit;
	}

	/* NO HCA ? */
	if (*p_num_guids == 0) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_vendor_get_ca_ids: ERR 3D09: "
			"No available channel adapters.\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	/* allocate and really call - user of this function needs to deallocate it */
	*p_hca_ids =
	    (VAPI_hca_id_t *) malloc(*p_num_guids * sizeof(VAPI_hca_id_t));

	/* now call it really */
	vapi_res = EVAPI_list_hcas(*p_num_guids, p_num_guids, *p_hca_ids);

	/* too many ? */
	if (vapi_res == VAPI_EAGAIN) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_vendor_get_ca_ids: ERR 3D10: "
			"More CA GUIDs than allocated array (%d).\n",
			*p_num_guids);
		status = IB_ERROR;
		goto Exit;
	}

	/* fail ? */
	if (vapi_res != VAPI_OK) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_vendor_get_ca_ids: ERR 3D11: : "
			"Bad parameter in calling: EVAPI_list_hcas.\n");
		status = IB_ERROR;
		goto Exit;
	}

	if (osm_log_is_active(p_vend->p_log, OSM_LOG_DEBUG)) {
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"__osm_vendor_get_ca_ids: "
			"Detected %u local channel adapters.\n", *p_num_guids);
	}

	status = IB_SUCCESS;

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

/**********************************************************************
 * Initialize an Info Struct for the Given HCA by its Id
 **********************************************************************/
static ib_api_status_t
__osm_ca_info_init(IN osm_vendor_t * const p_vend,
		   IN VAPI_hca_id_t ca_id, OUT osm_ca_info_t * const p_ca_info)
{
	ib_api_status_t status = IB_ERROR;
	VAPI_ret_t vapi_res;
	VAPI_hca_hndl_t hca_hndl;
	VAPI_hca_vendor_t hca_vendor;
	VAPI_hca_cap_t hca_cap;
	VAPI_hca_port_t hca_port;
	uint8_t port_num;
	IB_gid_t *p_port_gid;
	uint16_t maxNumGids;

	OSM_LOG_ENTER(p_vend->p_log);

	/* get the HCA handle */
	vapi_res = EVAPI_get_hca_hndl(ca_id, &hca_hndl);
	if (vapi_res != VAPI_OK) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_ca_info_init: ERR 3D05: "
			"Fail to get HCA handle (%u).\n", vapi_res);
		goto Exit;
	}

	if (osm_log_is_active(p_vend->p_log, OSM_LOG_DEBUG)) {
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"__osm_ca_info_init: " "Querying CA %s.\n", ca_id);
	}

	/* query and get the HCA capability */
	vapi_res = VAPI_query_hca_cap(hca_hndl, &hca_vendor, &hca_cap);
	if (vapi_res != VAPI_OK) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_ca_info_init: ERR 3D06: "
			"Fail to get HCA Capabilities (%u).\n", vapi_res);
		goto Exit;
	}

	/* get the guid of the HCA */
	memcpy(&(p_ca_info->guid), hca_cap.node_guid, 8 * sizeof(u_int8_t));
	p_ca_info->attr_size = 1;
	p_ca_info->p_attr = (ib_ca_attr_t *) malloc(sizeof(ib_ca_attr_t));
	memcpy(&(p_ca_info->p_attr->ca_guid), hca_cap.node_guid,
	       8 * sizeof(u_int8_t));

	/* now obtain the attributes of the ports */
	p_ca_info->p_attr->num_ports = hca_cap.phys_port_num;
	p_ca_info->p_attr->p_port_attr =
	    (ib_port_attr_t *) malloc(hca_cap.phys_port_num *
				      sizeof(ib_port_attr_t));

	for (port_num = 0; port_num < p_ca_info->p_attr->num_ports; port_num++) {

		/* query the port attributes */
		vapi_res =
		    VAPI_query_hca_port_prop(hca_hndl, port_num + 1, &hca_port);
		if (vapi_res != VAPI_OK) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"__osm_ca_info_init: ERR 3D07: "
				"Fail to get HCA Port Attributes (%d).\n",
				vapi_res);
			goto Exit;
		}

		/* first call to know the size of the gid table */
		vapi_res =
		    VAPI_query_hca_gid_tbl(hca_hndl, port_num + 1, 0,
					   &maxNumGids, NULL);
		p_port_gid = (IB_gid_t *) malloc(maxNumGids * sizeof(IB_gid_t));

		vapi_res =
		    VAPI_query_hca_gid_tbl(hca_hndl, port_num + 1, maxNumGids,
					   &maxNumGids, p_port_gid);
		if (vapi_res != VAPI_OK) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"__osm_ca_info_init: ERR 3D12: "
				"Fail to get HCA Port GID (%d).\n", vapi_res);
			goto Exit;
		}

		__osm_vendor_gid_to_guid(p_port_gid[0],
					 (IB_gid_t *) & p_ca_info->p_attr->
					 p_port_attr[port_num].port_guid);
		p_ca_info->p_attr->p_port_attr[port_num].lid = hca_port.lid;
		p_ca_info->p_attr->p_port_attr[port_num].link_state =
		    hca_port.state;
		p_ca_info->p_attr->p_port_attr[port_num].sm_lid =
		    hca_port.sm_lid;

		free(p_port_gid);
	}

	status = IB_SUCCESS;
Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

void
osm_ca_info_destroy(IN osm_vendor_t * const p_vend,
		    IN osm_ca_info_t * const p_ca_info, IN uint8_t num_ca)
{
	osm_ca_info_t *p_ca;
	uint8_t i;

	OSM_LOG_ENTER(p_vend->p_log);

	for (i = 0; i < num_ca; i++) {
		p_ca = &p_ca_info[i];

		if (NULL != p_ca->p_attr) {
			if (0 != p_ca->p_attr->num_ports) {
				free(p_ca->p_attr->p_port_attr);
			}

			free(p_ca->p_attr);
		}
	}

	free(p_ca_info);

	OSM_LOG_EXIT(p_vend->p_log);
}

/**********************************************************************
 * Fill in the array of port_attr with all available ports on ALL the
 * avilable CAs on this machine.
 * ALSO -
 * Update the vendor object list of ca_info structs
 **********************************************************************/
ib_api_status_t
osm_vendor_get_all_port_attr(IN osm_vendor_t * const p_vend,
			     IN ib_port_attr_t * const p_attr_array,
			     IN uint32_t * const p_num_ports)
{
	ib_api_status_t status;

	uint32_t ca;
	uint32_t ca_count = 0;
	uint32_t port_count = 0;
	uint8_t port_num;
	uint32_t total_ports = 0;
	VAPI_hca_id_t *p_ca_ids = NULL;
	osm_ca_info_t *p_ca_infos = NULL;
	uint32_t attr_array_sz = *p_num_ports;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vend);

	/* determine the number of CA's */
	status = __osm_vendor_get_ca_ids(p_vend, &p_ca_ids, &ca_count);
	if (status != IB_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_get_all_port_attr: ERR 3D13: "
			"Fail to get CA Ids.\n");
		goto Exit;
	}

	/* Allocate an array big enough to hold the ca info objects */
	p_ca_infos = malloc(ca_count * sizeof(osm_ca_info_t));
	if (p_ca_infos == NULL) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_get_all_port_attr: ERR 3D14: "
			"Unable to allocate CA information array.\n");
		goto Exit;
	}

	memset(p_ca_infos, 0, ca_count * sizeof(osm_ca_info_t));

	/*
	 * For each CA, retrieve the CA info attributes
	 */
	for (ca = 0; ca < ca_count; ca++) {
		status =
		    __osm_ca_info_init(p_vend, p_ca_ids[ca], &p_ca_infos[ca]);
		if (status != IB_SUCCESS) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osm_vendor_get_all_port_attr: ERR 3D15: "
				"Unable to initialize CA Info object (%s).\n",
				ib_get_err_str(status));
			goto Exit;
		}
		total_ports += p_ca_infos[ca].p_attr->num_ports;
	}

	*p_num_ports = total_ports;
	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"osm_vendor_get_all_port_attr: total ports:%u \n", total_ports);

	/*
	 * If the user supplied enough storage, return the port guids,
	 * otherwise, return the appropriate error.
	 */
	if (attr_array_sz >= total_ports) {
		for (ca = 0; ca < ca_count; ca++) {
			uint32_t num_ports;

			num_ports = p_ca_infos[ca].p_attr->num_ports;

			for (port_num = 0; port_num < num_ports; port_num++) {
				p_attr_array[port_count] =
				    *__osm_ca_info_get_port_attr_ptr(&p_ca_infos
								     [ca],
								     port_num);
				port_count++;
			}
		}
	} else {
		status = IB_INSUFFICIENT_MEMORY;
		goto Exit;
	}

	status = IB_SUCCESS;

Exit:
	if (p_ca_ids)
		free(p_ca_ids);

	if (p_ca_infos) {
		osm_ca_info_destroy(p_vend, p_ca_infos, ca_count);
	}

	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

/**********************************************************************
 * Given the vendor obj and a guid
 * return the ca id and port number that have that guid
 **********************************************************************/

ib_api_status_t
osm_vendor_get_guid_ca_and_port(IN osm_vendor_t * const p_vend,
				IN ib_net64_t const guid,
				OUT VAPI_hca_hndl_t * p_hca_hndl,
				OUT VAPI_hca_id_t * p_hca_id,
				OUT uint8_t * p_hca_idx,
				OUT uint32_t * p_port_num)
{

	ib_api_status_t status;
	VAPI_hca_id_t *p_ca_ids = NULL;
	VAPI_ret_t vapi_res;
	VAPI_hca_hndl_t hca_hndl;
	VAPI_hca_vendor_t hca_vendor;
	VAPI_hca_cap_t hca_cap;
	IB_gid_t *p_port_gid = NULL;
	uint16_t maxNumGids;
	ib_net64_t port_guid;
	uint32_t ca, portIdx, ca_count;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vend);

	/*
	 * 1) Determine the number of CA's
	 * 2) Allocate an array big enough to hold the ca info objects.
	 * 3) Call again to retrieve the guids.
	 */
	status = __osm_vendor_get_ca_ids(p_vend, &p_ca_ids, &ca_count);
	if (status != IB_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_get_guid_ca_and_port: ERR 3D16: "
			"Fail to get CA Ids.\n");
		goto Exit;
	}

	/*
	 * For each CA, retrieve the CA info attributes
	 */
	for (ca = 0; ca < ca_count; ca++) {
		/* get the HCA handle */
		vapi_res = EVAPI_get_hca_hndl(p_ca_ids[ca], &hca_hndl);
		if (vapi_res != VAPI_OK) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osm_vendor_get_guid_ca_and_port: ERR 3D17: "
				"Fail to get HCA handle (%u).\n", vapi_res);
			goto Exit;
		}

		/* get the CA attributes - to know how many ports it has: */
		if (osm_log_is_active(p_vend->p_log, OSM_LOG_DEBUG)) {
			osm_log(p_vend->p_log, OSM_LOG_DEBUG,
				"osm_vendor_get_guid_ca_and_port: "
				"Querying CA %s.\n", p_ca_ids[ca]);
		}

		/* query and get the HCA capability */
		vapi_res = VAPI_query_hca_cap(hca_hndl, &hca_vendor, &hca_cap);
		if (vapi_res != VAPI_OK) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osm_vendor_get_guid_ca_and_port: ERR 3D18: "
				"Fail to get HCA Capabilities (%u).\n",
				vapi_res);
			goto Exit;
		}

		/* go over all ports - to obtail their guids */
		for (portIdx = 0; portIdx < hca_cap.phys_port_num; portIdx++) {
			vapi_res =
			    VAPI_query_hca_gid_tbl(hca_hndl, portIdx + 1, 0,
						   &maxNumGids, NULL);
			p_port_gid =
			    (IB_gid_t *) malloc(maxNumGids * sizeof(IB_gid_t));

			/* get the port guid */
			vapi_res =
			    VAPI_query_hca_gid_tbl(hca_hndl, portIdx + 1,
						   maxNumGids, &maxNumGids,
						   p_port_gid);
			if (vapi_res != VAPI_OK) {
				osm_log(p_vend->p_log, OSM_LOG_ERROR,
					"osm_vendor_get_guid_ca_and_port: ERR 3D19: "
					"Fail to get HCA Port GID (%d).\n",
					vapi_res);
				goto Exit;
			}

			/* convert to SF style */
			__osm_vendor_gid_to_guid(p_port_gid[0],
						 (VAPI_gid_t *) & port_guid);

			/* finally did we find it ? */
			if (port_guid == guid) {
				*p_hca_hndl = hca_hndl;
				memcpy(p_hca_id, p_ca_ids[ca],
				       sizeof(VAPI_hca_id_t));
				*p_hca_idx = ca;
				*p_port_num = portIdx + 1;
				status = IB_SUCCESS;
				goto Exit;
			}

			free(p_port_gid);
			p_port_gid = NULL;
		}		/*  ALL PORTS  */
	}			/*  all HCAs */

	osm_log(p_vend->p_log, OSM_LOG_ERROR,
		"osm_vendor_get_guid_ca_and_port: ERR 3D20: "
		"Fail to find HCA and Port for Port Guid 0x%" PRIx64 "\n",
		cl_ntoh64(guid));
	status = IB_INVALID_GUID;

Exit:
	if (p_ca_ids != NULL)
		free(p_ca_ids);
	if (p_port_gid != NULL)
		free(p_port_gid);
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

#endif
