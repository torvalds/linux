/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2009 HNR Consulting.  All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>
#include "mad_internal.h"

#undef DEBUG
#define DEBUG 	if (ibdebug)	IBWARN

int ib_resolve_smlid_via(ib_portid_t * sm_id, int timeout,
			 const struct ibmad_port *srcport)
{
	umad_port_t port;
	int ret;

	memset(sm_id, 0, sizeof(*sm_id));

	ret = umad_get_port(srcport->ca_name, srcport->portnum, &port);
	if (ret)
		return -1;

	if (!IB_LID_VALID(port.sm_lid)) {
		errno = ENXIO;
		return -1;
	}
	sm_id->sl = port.sm_sl;

	ret = ib_portid_set(sm_id, port.sm_lid, 0, 0);
	umad_release_port(&port);
	return ret;
}

int ib_resolve_smlid(ib_portid_t * sm_id, int timeout)
{
	return ib_resolve_smlid_via(sm_id, timeout, ibmp);
}

int ib_resolve_gid_via(ib_portid_t * portid, ibmad_gid_t gid,
		       ib_portid_t * sm_id, int timeout,
		       const struct ibmad_port *srcport)
{
	ib_portid_t sm_portid = { 0 };
	char buf[IB_SA_DATA_SIZE] = { 0 };

	if (!sm_id)
		sm_id = &sm_portid;

	if (!IB_LID_VALID(sm_id->lid)) {
		if (ib_resolve_smlid_via(sm_id, timeout, srcport) < 0)
			return -1;
	}

	if ((portid->lid =
	     ib_path_query_via(srcport, gid, gid, sm_id, buf)) < 0)
		return -1;

	return 0;
}

int ib_resolve_guid_via(ib_portid_t * portid, uint64_t * guid,
			ib_portid_t * sm_id, int timeout,
			const struct ibmad_port *srcport)
{
	ib_portid_t sm_portid = { 0 };
	uint8_t buf[IB_SA_DATA_SIZE] = { 0 };
	uint64_t prefix;
	ibmad_gid_t selfgid;
	umad_port_t port;

	if (!sm_id)
		sm_id = &sm_portid;

	if (!IB_LID_VALID(sm_id->lid)) {
		if (ib_resolve_smlid_via(sm_id, timeout, srcport) < 0)
			return -1;
	}

	if (umad_get_port(srcport->ca_name, srcport->portnum, &port))
		return -1;

	mad_set_field64(selfgid, 0, IB_GID_PREFIX_F, ntohll(port.gid_prefix));
	mad_set_field64(selfgid, 0, IB_GID_GUID_F, ntohll(port.port_guid));
	umad_release_port(&port);

	memcpy(&prefix, portid->gid, sizeof(prefix));
	if (!prefix)
		mad_set_field64(portid->gid, 0, IB_GID_PREFIX_F,
				IB_DEFAULT_SUBN_PREFIX);
	if (guid)
		mad_set_field64(portid->gid, 0, IB_GID_GUID_F, *guid);

	if ((portid->lid =
	     ib_path_query_via(srcport, selfgid, portid->gid, sm_id, buf)) < 0)
		return -1;

	mad_decode_field(buf, IB_SA_PR_SL_F, &portid->sl);
	return 0;
}

int ib_resolve_portid_str_via(ib_portid_t * portid, char *addr_str,
			      enum MAD_DEST dest_type, ib_portid_t * sm_id,
			      const struct ibmad_port *srcport)
{
	ibmad_gid_t gid;
	uint64_t guid;
	int lid;
	char *routepath;
	ib_portid_t selfportid = { 0 };
	int selfport = 0;

	memset(portid, 0, sizeof *portid);

	switch (dest_type) {
	case IB_DEST_LID:
		lid = strtol(addr_str, 0, 0);
		if (!IB_LID_VALID(lid)) {
			errno = EINVAL;
			return -1;
		}
		return ib_portid_set(portid, lid, 0, 0);

	case IB_DEST_DRPATH:
		if (str2drpath(&portid->drpath, addr_str, 0, 0) < 0) {
			errno = EINVAL;
			return -1;
		}
		return 0;

	case IB_DEST_GUID:
		if (!(guid = strtoull(addr_str, 0, 0))) {
			errno = EINVAL;
			return -1;
		}

		/* keep guid in portid? */
		return ib_resolve_guid_via(portid, &guid, sm_id, 0, srcport);

	case IB_DEST_DRSLID:
		lid = strtol(addr_str, &routepath, 0);
		routepath++;
		if (!IB_LID_VALID(lid)) {
			errno = EINVAL;
			return -1;
		}
		ib_portid_set(portid, lid, 0, 0);

		/* handle DR parsing and set DrSLID to local lid */
		if (ib_resolve_self_via(&selfportid, &selfport, 0, srcport) < 0)
			return -1;
		if (str2drpath(&portid->drpath, routepath, selfportid.lid, 0) <
		    0) {
			errno = EINVAL;
			return -1;
		}
		return 0;

	case IB_DEST_GID:
		if (inet_pton(AF_INET6, addr_str, &gid) <= 0)
			return -1;
		return ib_resolve_gid_via(portid, gid, sm_id, 0, srcport);
	default:
		IBWARN("bad dest_type %d", dest_type);
		errno = EINVAL;
	}

	return -1;
}

int ib_resolve_portid_str(ib_portid_t * portid, char *addr_str,
			  enum MAD_DEST dest_type, ib_portid_t * sm_id)
{
	return ib_resolve_portid_str_via(portid, addr_str, dest_type,
					 sm_id, ibmp);
}

int ib_resolve_self_via(ib_portid_t * portid, int *portnum, ibmad_gid_t * gid,
			const struct ibmad_port *srcport)
{
	ib_portid_t self = { 0 };
	uint8_t portinfo[64];
	uint8_t nodeinfo[64];
	uint64_t guid, prefix;

	if (!smp_query_via(nodeinfo, &self, IB_ATTR_NODE_INFO, 0, 0, srcport))
		return -1;

	if (!smp_query_via(portinfo, &self, IB_ATTR_PORT_INFO, 0, 0, srcport))
		return -1;

	mad_decode_field(portinfo, IB_PORT_LID_F, &portid->lid);
	mad_decode_field(portinfo, IB_PORT_SMSL_F, &portid->sl);
	mad_decode_field(portinfo, IB_PORT_GID_PREFIX_F, &prefix);
	mad_decode_field(nodeinfo, IB_NODE_PORT_GUID_F, &guid);

	if (portnum)
		mad_decode_field(nodeinfo, IB_NODE_LOCAL_PORT_F, portnum);
	if (gid) {
		mad_encode_field(*gid, IB_GID_PREFIX_F, &prefix);
		mad_encode_field(*gid, IB_GID_GUID_F, &guid);
	}
	return 0;
}

int ib_resolve_self(ib_portid_t * portid, int *portnum, ibmad_gid_t * gid)
{
	return ib_resolve_self_via(portid, portnum, gid, ibmp);
}
