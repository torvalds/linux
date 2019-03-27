/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2012 Mellanox Technologies LTD. All rights reserved.
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
 *    Implementation of osm_mcm_port_t.
 * This object represents the membership of a port in a multicast group.
 * This object is part of the OpenSM family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_MCM_PORT_C
#include <opensm/osm_mcm_port.h>
#include <opensm/osm_multicast.h>

osm_mcm_port_t *osm_mcm_port_new(IN osm_port_t *port, IN osm_mgrp_t *mgrp)
{
	osm_mcm_port_t *p_mcm;

	p_mcm = malloc(sizeof(*p_mcm));
	if (p_mcm) {
		memset(p_mcm, 0, sizeof(*p_mcm));
		p_mcm->port = port;
		p_mcm->mgrp = mgrp;
	}

	return p_mcm;
}

void osm_mcm_port_delete(IN osm_mcm_port_t * p_mcm)
{
	CL_ASSERT(p_mcm);

	free(p_mcm);
}

osm_mcm_alias_guid_t *osm_mcm_alias_guid_new(IN osm_mcm_port_t *p_base_mcm_port,
					     IN ib_member_rec_t *mcmr,
					     IN boolean_t proxy)
{
	osm_mcm_alias_guid_t *p_mcm_alias_guid;

	p_mcm_alias_guid = calloc(1, sizeof(*p_mcm_alias_guid));
	if (p_mcm_alias_guid) {
		p_mcm_alias_guid->alias_guid = mcmr->port_gid.unicast.interface_id;
		p_mcm_alias_guid->p_base_mcm_port = p_base_mcm_port;
		p_mcm_alias_guid->port_gid.unicast.prefix = mcmr->port_gid.unicast.prefix;
		p_mcm_alias_guid->port_gid.unicast.interface_id = mcmr->port_gid.unicast.interface_id;
		p_mcm_alias_guid->scope_state = mcmr->scope_state;
		p_mcm_alias_guid->proxy_join = proxy;
	}

	return p_mcm_alias_guid;
}

void osm_mcm_alias_guid_delete(IN OUT osm_mcm_alias_guid_t ** pp_mcm_alias_guid)
{
	free(*pp_mcm_alias_guid);
	*pp_mcm_alias_guid = NULL;
}
