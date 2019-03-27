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

/*
 * Abstract:
 * 	Declaration of osm_stats_t.
 *	This object represents the OpenSM statistics object.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_STATS_H_
#define _OSM_STATS_H_

#ifdef HAVE_LIBPTHREAD
#include <pthread.h>
#else
#include <complib/cl_event.h>
#endif
#include <complib/cl_atomic.h>
#include <opensm/osm_base.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Statistics
* NAME
*	OpenSM
*
* DESCRIPTION
*	The OpenSM object encapsulates the information needed by the
*	OpenSM to track interesting traffic and internal statistics.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Statistics/osm_stats_t
* NAME
*	osm_stats_t
*
* DESCRIPTION
*	OpenSM statistics block.
*
* SYNOPSIS
*/
typedef struct osm_stats {
	atomic32_t qp0_mads_outstanding;
	atomic32_t qp0_mads_outstanding_on_wire;
	atomic32_t qp0_mads_rcvd;
	atomic32_t qp0_mads_sent;
	atomic32_t qp0_unicasts_sent;
	atomic32_t qp0_mads_rcvd_unknown;
	atomic32_t sa_mads_outstanding;
	atomic32_t sa_mads_rcvd;
	atomic32_t sa_mads_sent;
	atomic32_t sa_mads_rcvd_unknown;
	atomic32_t sa_mads_ignored;
#ifdef HAVE_LIBPTHREAD
	pthread_mutex_t mutex;
	pthread_cond_t cond;
#else
	cl_event_t event;
#endif
} osm_stats_t;
/*
* FIELDS
*	qp0_mads_outstanding
*		Contains the number of MADs outstanding on QP0.
*		When this value reaches zero, OpenSM has discovered all
*		nodes on the subnet, and finished retrieving attributes.
*		At that time, subnet configuration may begin.
*		This variable must be manipulated using atomic instructions.
*
*	qp0_mads_outstanding_on_wire
*		The number of MADs outstanding on the wire at any moment.
*
*	qp0_mads_rcvd
*		Total number of QP0 MADs received.
*
*	qp0_mads_sent
*		Total number of QP0 MADs sent.
*
*	qp0_unicasts_sent
*		Total number of response-less MADs sent on the wire.  This count
*		includes getresp(), send() and trap() methods.
*
*	qp0_mads_rcvd_unknown
*		Total number of unknown QP0 MADs received. This includes
*		unrecognized attribute IDs and methods.
*
*	sa_mads_outstanding
*		Contains the number of SA MADs outstanding on QP1.
*
*	sa_mads_rcvd
*		Total number of SA MADs received.
*
*	sa_mads_sent
*		Total number of SA MADs sent.
*
*	sa_mads_rcvd_unknown
*		Total number of unknown SA MADs received. This includes
*		unrecognized attribute IDs and methods.
*
*	sa_mads_ignored
*		Total number of SA MADs received because SM is not
*		master or SM is in first time sweep.
*
* SEE ALSO
***************/

static inline uint32_t osm_stats_inc_qp0_outstanding(osm_stats_t *stats)
{
	uint32_t outstanding;

#ifdef HAVE_LIBPTHREAD
	pthread_mutex_lock(&stats->mutex);
	outstanding = ++stats->qp0_mads_outstanding;
	pthread_mutex_unlock(&stats->mutex);
#else
	outstanding = cl_atomic_inc(&stats->qp0_mads_outstanding);
#endif

	return outstanding;
}

static inline uint32_t osm_stats_dec_qp0_outstanding(osm_stats_t *stats)
{
	uint32_t outstanding;

#ifdef HAVE_LIBPTHREAD
	pthread_mutex_lock(&stats->mutex);
	outstanding = --stats->qp0_mads_outstanding;
	if (!outstanding)
		pthread_cond_signal(&stats->cond);
	pthread_mutex_unlock(&stats->mutex);
#else
	outstanding = cl_atomic_dec(&stats->qp0_mads_outstanding);
	if (!outstanding)
		cl_event_signal(&stats->event);
#endif

	return outstanding;
}

END_C_DECLS
#endif				/* _OSM_STATS_H_ */
