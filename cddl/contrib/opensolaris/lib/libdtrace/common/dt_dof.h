/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_DT_DOF_H
#define	_DT_DOF_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <dtrace.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include <dt_buf.h>

typedef struct dt_dof {
	dtrace_hdl_t *ddo_hdl;		/* libdtrace handle */
	dtrace_prog_t *ddo_pgp;		/* current program */
	uint_t ddo_nsecs;		/* number of sections */
	dof_secidx_t ddo_strsec; 	/* global strings section index */
	dof_secidx_t *ddo_xlimport;	/* imported xlator section indices */
	dof_secidx_t *ddo_xlexport;	/* exported xlator section indices */
	dt_buf_t ddo_secs;		/* section headers */
	dt_buf_t ddo_strs;		/* global strings */
	dt_buf_t ddo_ldata;		/* loadable section data */
	dt_buf_t ddo_udata;		/* unloadable section data */
	dt_buf_t ddo_probes;		/* probe section data */
	dt_buf_t ddo_args;		/* probe arguments section data */
	dt_buf_t ddo_offs;		/* probe offsets section data */
	dt_buf_t ddo_enoffs;		/* is-enabled offsets section data */
	dt_buf_t ddo_rels;		/* probe relocation section data */
	dt_buf_t ddo_xlms;		/* xlate members section data */
} dt_dof_t;

extern void dt_dof_init(dtrace_hdl_t *);
extern void dt_dof_fini(dtrace_hdl_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _DT_DOF_H */
