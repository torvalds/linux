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

/*
 * Abstract:
 *    Implementation of osm_mad_pool_t.
 * This object represents a pool of management datagram (MAD) objects.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_MAD_POOL_C
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_madw.h>
#include <vendor/osm_vendor_api.h>

void osm_mad_pool_construct(IN osm_mad_pool_t * p_pool)
{
	CL_ASSERT(p_pool);

	memset(p_pool, 0, sizeof(*p_pool));
}

void osm_mad_pool_destroy(IN osm_mad_pool_t * p_pool)
{
	CL_ASSERT(p_pool);
}

ib_api_status_t osm_mad_pool_init(IN osm_mad_pool_t * p_pool)
{
	p_pool->mads_out = 0;

	return IB_SUCCESS;
}

osm_madw_t *osm_mad_pool_get(IN osm_mad_pool_t * p_pool,
			     IN osm_bind_handle_t h_bind,
			     IN uint32_t total_size,
			     IN const osm_mad_addr_t * p_mad_addr)
{
	osm_madw_t *p_madw;
	ib_mad_t *p_mad;

	CL_ASSERT(h_bind != OSM_BIND_INVALID_HANDLE);
	CL_ASSERT(total_size);

	/*
	   First, acquire a mad wrapper from the mad wrapper pool.
	 */
	p_madw = malloc(sizeof(*p_madw));
	if (p_madw == NULL)
		goto Exit;

	osm_madw_init(p_madw, h_bind, total_size, p_mad_addr);

	/*
	   Next, acquire a wire mad of the specified size.
	 */
	p_mad = osm_vendor_get(h_bind, total_size, &p_madw->vend_wrap);
	if (p_mad == NULL) {
		/* Don't leak wrappers! */
		free(p_madw);
		p_madw = NULL;
		goto Exit;
	}

	cl_atomic_inc(&p_pool->mads_out);
	/*
	   Finally, attach the wire MAD to this wrapper.
	 */
	osm_madw_set_mad(p_madw, p_mad);

Exit:
	return p_madw;
}

osm_madw_t *osm_mad_pool_get_wrapper(IN osm_mad_pool_t * p_pool,
				     IN osm_bind_handle_t h_bind,
				     IN uint32_t total_size,
				     IN const ib_mad_t * p_mad,
				     IN const osm_mad_addr_t * p_mad_addr)
{
	osm_madw_t *p_madw;

	CL_ASSERT(h_bind != OSM_BIND_INVALID_HANDLE);
	CL_ASSERT(total_size);
	CL_ASSERT(p_mad);

	/*
	   First, acquire a mad wrapper from the mad wrapper pool.
	 */
	p_madw = malloc(sizeof(*p_madw));
	if (p_madw == NULL)
		goto Exit;

	/*
	   Finally, initialize the wrapper object.
	 */
	cl_atomic_inc(&p_pool->mads_out);
	osm_madw_init(p_madw, h_bind, total_size, p_mad_addr);
	osm_madw_set_mad(p_madw, p_mad);

Exit:
	return p_madw;
}

osm_madw_t *osm_mad_pool_get_wrapper_raw(IN osm_mad_pool_t * p_pool)
{
	osm_madw_t *p_madw;

	p_madw = malloc(sizeof(*p_madw));
	if (!p_madw)
		return NULL;

	osm_madw_init(p_madw, 0, 0, 0);
	osm_madw_set_mad(p_madw, 0);
	cl_atomic_inc(&p_pool->mads_out);

	return p_madw;
}

void osm_mad_pool_put(IN osm_mad_pool_t * p_pool, IN osm_madw_t * p_madw)
{
	CL_ASSERT(p_madw);

	/*
	   First, return the wire mad to the pool
	 */
	if (p_madw->p_mad)
		osm_vendor_put(p_madw->h_bind, &p_madw->vend_wrap);

	/*
	   Return the mad wrapper to the wrapper pool
	 */
	free(p_madw);
	cl_atomic_dec(&p_pool->mads_out);
}
