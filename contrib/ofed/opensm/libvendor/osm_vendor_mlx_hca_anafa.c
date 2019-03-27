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

#if defined(OSM_VENDOR_INTF_ANAFA)
#undef IN
#undef OUT

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include <vendor/osm_vendor_api.h>
#include <opensm/osm_log.h>
#include <sys/ioctl.h>

#include <vendor/osm_vendor_mlx_transport_anafa.h>
#include <vendor/osm_ts_useraccess.h>

/********************************************************************************
 *
 * Provide the functionality for selecting an HCA Port and Obtaining it's guid.
 *
 ********************************************************************************/

typedef struct _osm_ca_info {
	/* ib_net64_t guid; ?? */
	/* size_t attr_size; ?? */
	ib_ca_attr_t attr;
} osm_ca_info_t;

/**********************************************************************
 * Convert the given GID to GUID by copy of it's upper 8 bytes
 **********************************************************************/
ib_api_status_t
__osm_vendor_gid_to_guid(IN tTS_IB_GID gid, OUT ib_net64_t * p_guid)
{
	memcpy(p_guid, gid + 8, 8);
	return (IB_SUCCESS);
}

/**********************************************************************
 * Initialize an Info Struct for the Given HCA by its Id
 **********************************************************************/
static ib_api_status_t
__osm_ca_info_init(IN osm_vendor_t * const p_vend,
		   OUT osm_ca_info_t * const p_ca_info)
{
	ib_api_status_t status = IB_ERROR;
	int ioctl_ret = 0;
	osmv_TOPSPIN_ANAFA_transport_info_t *p_tpot_info =
	    p_vend->p_transport_info;
	osm_ts_gid_entry_ioctl gid_ioctl;
	osm_ts_get_port_info_ioctl port_info;
	struct ib_get_dev_info_ioctl dev_info;

	OSM_LOG_ENTER(p_vend->p_log);

	/* query HCA guid */
	ioctl_ret = ioctl(p_tpot_info->device_fd, TS_IB_IOCGDEVINFO, &dev_info);
	if (ioctl_ret != 0) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_ca_info_init: ERR 7001: "
			"Fail to get HCA Capabilities (%d).\n", ioctl_ret);
		goto Exit;
	}

	memcpy(&(p_ca_info->attr.ca_guid), dev_info.dev_info.node_guid,
	       8 * sizeof(uint8_t));

/* now obtain the attributes of the ports - on our case port 1*/

	p_ca_info->attr.num_ports = 1;
	p_ca_info->attr.p_port_attr =
	    (ib_port_attr_t *) malloc(1 * sizeof(ib_port_attr_t));

	port_info.port = 1;
	ioctl_ret =
	    ioctl(p_tpot_info->device_fd, TS_IB_IOCGPORTINFO, &port_info);
	if (ioctl_ret) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_ca_info_init: ERR 7002: "
			"Fail to get HCA Port Attributes (%d).\n", ioctl_ret);
		goto Exit;
	}

	gid_ioctl.port = 1;
	gid_ioctl.index = 0;
	ioctl_ret =
	    ioctl(p_tpot_info->device_fd, TS_IB_IOCGGIDENTRY, &gid_ioctl);
	if (ioctl_ret) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_ca_info_init: ERR 7003: "
			"Fail to get HCA Port GID (%d).\n", ioctl_ret);
		goto Exit;
	}

	__osm_vendor_gid_to_guid(gid_ioctl.gid_entry,
				 &(p_ca_info->attr.p_port_attr[0].port_guid));
	p_ca_info->attr.p_port_attr[0].lid = port_info.port_info.lid;
	p_ca_info->attr.p_port_attr[0].link_state =
	    port_info.port_info.port_state;
	p_ca_info->attr.p_port_attr[0].sm_lid = port_info.port_info.sm_lid;

	status = IB_SUCCESS;
Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

/**********************************************************************
 * Fill in port_attr
 * ALSO -
 * Update the vendor object list of ca_info structs
 **********************************************************************/
ib_api_status_t
osm_vendor_get_all_port_attr(IN osm_vendor_t * const p_vend,
			     IN ib_port_attr_t * const p_attr_array,
			     IN uint32_t * const p_num_ports)
{
	ib_api_status_t status;
	osm_ca_info_t ca_info;
	uint32_t attr_array_sz = *p_num_ports;

	OSM_LOG_ENTER(p_vend->p_log);
	CL_ASSERT(p_vend);

	/* anafa has one port - the user didnt supply enough storage space */
	if (attr_array_sz < 1) {
		status = IB_INSUFFICIENT_MEMORY;
		goto Exit;
	}

	/*
	 * retrieve the CA info attributes
	 */
	status = __osm_ca_info_init(p_vend, &ca_info);
	if (status != IB_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_get_all_port_attr: ERR 7004: "
			"Unable to initialize CA Info object (%s).\n",
			ib_get_err_str(status));
		goto Exit;
	}

	*p_num_ports = 1;

	p_attr_array[0] = ca_info.attr.p_port_attr[0];	/* anafa has only one port */
	status = IB_SUCCESS;

Exit:

	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

#endif
