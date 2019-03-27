/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2006 Mellanox Technologies LTD. All rights reserved.
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
 *    Implementation of osm_mtree_node_t.
 * This file implements the Multicast Tree object.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_MTREE_C
#include <complib/cl_debug.h>
#include <opensm/osm_mtree.h>

osm_mtree_node_t *osm_mtree_node_new(IN const osm_switch_t * p_sw)
{
	osm_mtree_node_t *p_mtn;
	uint32_t i;

	p_mtn = malloc(sizeof(osm_mtree_node_t) +
		       sizeof(void *) * (p_sw->num_ports - 1));
	if (!p_mtn)
		return NULL;

	memset(p_mtn, 0, sizeof(*p_mtn));
	p_mtn->p_sw = p_sw;
	p_mtn->max_children = p_sw->num_ports;
	for (i = 0; i < p_mtn->max_children; i++)
		p_mtn->child_array[i] = NULL;

	return p_mtn;
}

void osm_mtree_destroy(IN osm_mtree_node_t * p_mtn)
{
	uint8_t i;

	if (p_mtn == NULL)
		return;

	for (i = 0; i < p_mtn->max_children; i++)
		if ((p_mtn->child_array[i] != NULL) &&
		    (p_mtn->child_array[i] != OSM_MTREE_LEAF))
			osm_mtree_destroy(p_mtn->child_array[i]);

	free(p_mtn);
}

#if 0
static void mtree_dump(IN osm_mtree_node_t * p_mtn)
{
	uint32_t i;

	if (p_mtn == NULL)
		return;

	printf("GUID:0x%016" PRIx64 " max_children:%u\n",
	       cl_ntoh64(p_mtn->p_sw->p_node->node_info.node_guid),
	       p_mtn->max_children);
	if (p_mtn->child_array != NULL) {
		for (i = 0; i < p_mtn->max_children; i++) {
			printf("i=%d\n", i);
			if ((p_mtn->child_array[i] != NULL)
			    && (p_mtn->child_array[i] != OSM_MTREE_LEAF))
				mtree_dump(p_mtn->child_array[i]);
		}
	}
}
#endif
