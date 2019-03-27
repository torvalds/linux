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

#ifndef	_DT_PROVIDER_H
#define	_DT_PROVIDER_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <dt_impl.h>
#include <dt_ident.h>
#include <dt_list.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct dt_provider {
	dt_list_t pv_list;		/* list forward/back pointers */
	struct dt_provider *pv_next;	/* pointer to next provider in hash */
	dtrace_providerdesc_t pv_desc;	/* provider name and attributes */
	dt_idhash_t *pv_probes;		/* probe defs (if user-declared) */
	dt_node_t *pv_nodes;		/* parse node allocation list */
	ulong_t *pv_xrefs;		/* translator reference bitmap */
	ulong_t pv_xrmax;		/* number of valid bits in pv_xrefs */
	ulong_t pv_gen;			/* generation # that created me */
	dtrace_hdl_t *pv_hdl;		/* pointer to containing dtrace_hdl */
	uint_t pv_flags;		/* flags (see below) */
} dt_provider_t;

#define	DT_PROVIDER_INTF	0x1	/* provider interface declaration */
#define	DT_PROVIDER_IMPL	0x2	/* provider implementation is loaded */

typedef struct dt_probe_iter {
	dtrace_probedesc_t pit_desc;	/* description storage */
	dtrace_hdl_t *pit_hdl;		/* libdtrace handle */
	dt_provider_t *pit_pvp;		/* current provider */
	const char *pit_pat;		/* caller's name pattern (or NULL) */
	dtrace_probe_f *pit_func;	/* caller's function */
	void *pit_arg;			/* caller's argument */
	uint_t pit_matches;		/* number of matches */
} dt_probe_iter_t;

typedef struct dt_probe_instance {
	char *pi_fname;			/* function name */
	char *pi_rname;			/* mangled relocation name */
	uint32_t *pi_offs;		/* offsets into the function */
	uint32_t *pi_enoffs;		/* is-enabled offsets */
	uint_t pi_noffs;		/* number of offsets */
	uint_t pi_maxoffs;		/* size of pi_offs allocation */
	uint_t pi_nenoffs;		/* number of is-enabled offsets */
	uint_t pi_maxenoffs;		/* size of pi_enoffs allocation */
	struct dt_probe_instance *pi_next; /* next instance in the list */
} dt_probe_instance_t;

typedef struct dt_probe {
	dt_provider_t *pr_pvp;		/* pointer to containing provider */
	dt_ident_t *pr_ident;		/* pointer to probe identifier */
	const char *pr_name;		/* pointer to name component */
	dt_node_t *pr_nargs;		/* native argument list */
	dt_node_t **pr_nargv;		/* native argument vector */
	uint_t pr_nargc;		/* native argument count */
	dt_node_t *pr_xargs;		/* translated argument list */
	dt_node_t **pr_xargv;		/* translated argument vector */
	uint_t pr_xargc;		/* translated argument count */
	uint8_t *pr_mapping;		/* translated argument mapping */
	dt_probe_instance_t *pr_inst;	/* list of functions and offsets */
	dtrace_typeinfo_t *pr_argv;	/* output argument types */
	int pr_argc;			/* output argument count */
} dt_probe_t;

extern dt_provider_t *dt_provider_lookup(dtrace_hdl_t *, const char *);
extern dt_provider_t *dt_provider_create(dtrace_hdl_t *, const char *);
extern void dt_provider_destroy(dtrace_hdl_t *, dt_provider_t *);
extern int dt_provider_xref(dtrace_hdl_t *, dt_provider_t *, id_t);

extern dt_probe_t *dt_probe_create(dtrace_hdl_t *, dt_ident_t *, int,
    dt_node_t *, uint_t, dt_node_t *, uint_t);

extern dt_probe_t *dt_probe_info(dtrace_hdl_t *,
    const dtrace_probedesc_t *, dtrace_probeinfo_t *);

extern dt_probe_t *dt_probe_lookup(dt_provider_t *, const char *);
extern void dt_probe_declare(dt_provider_t *, dt_probe_t *);
extern void dt_probe_destroy(dt_probe_t *);

extern int dt_probe_define(dt_provider_t *, dt_probe_t *,
    const char *, const char *, uint32_t, int);

extern dt_node_t *dt_probe_tag(dt_probe_t *, uint_t, dt_node_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _DT_PROVIDER_H */
