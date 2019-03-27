/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_DT_PRINTF_H
#define	_DT_PRINTF_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <libctf.h>
#include <dtrace.h>
#include <stdio.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dt_node;
struct dt_ident;

struct dt_pfconv;
struct dt_pfargv;
struct dt_pfargd;

typedef int dt_pfcheck_f(struct dt_pfargv *,
    struct dt_pfargd *, struct dt_node *);
typedef int dt_pfprint_f(dtrace_hdl_t *, FILE *, const char *,
    const struct dt_pfargd *, const void *, size_t, uint64_t);

typedef struct dt_pfconv {
	const char *pfc_name;		/* string name of input conversion */
	const char *pfc_ofmt;		/* string name of output conversion */
	const char *pfc_tstr;		/* string name for conversion type */
	dt_pfcheck_f *pfc_check;	/* function to use for type checking */
	dt_pfprint_f *pfc_print;	/* function to use for formatting */
	ctf_file_t *pfc_cctfp;		/* CTF container for "C" defn of type */
	ctf_id_t pfc_ctype;		/* CTF type ID for "C" defn of type */
	ctf_file_t *pfc_dctfp;		/* CTF container for "D" defn of type */
	ctf_id_t pfc_dtype;		/* CTF type ID for "D" defn of type */
	struct dt_pfconv *pfc_next;	/* next conversion in hash chain */
} dt_pfconv_t;

typedef struct dt_pfdict {
	dt_pfconv_t **pdi_buckets;	/* hash bucket array */
	uint_t pdi_nbuckets;		/* size of hash bucket array */
} dt_pfdict_t;

typedef struct dt_pfargd {
	const char *pfd_prefix;		/* prefix string pointer (or NULL) */
	size_t pfd_preflen;		/* length of prefix in bytes */
	char pfd_fmt[8];		/* output format name to use */
	uint_t pfd_flags;		/* format flags (see below) */
	int pfd_width;			/* field width (or 0) */
	int pfd_dynwidth;		/* dynamic field width (or 0) */
	int pfd_prec;			/* field precision (or 0) */
	const dt_pfconv_t *pfd_conv;	/* conversion specification */
	const dtrace_recdesc_t *pfd_rec; /* pointer to current record */
	struct dt_pfargd *pfd_next;	/* pointer to next arg descriptor */
} dt_pfargd_t;

#define	DT_PFCONV_ALT		0x0001	/* alternate print format (%#) */
#define	DT_PFCONV_ZPAD		0x0002	/* zero-pad integer field (%0) */
#define	DT_PFCONV_LEFT		0x0004	/* left-align field (%-) */
#define	DT_PFCONV_SPOS		0x0008	/* sign positive values (%+) */
#define	DT_PFCONV_DYNWIDTH	0x0010	/* dynamic width (%*.) */
#define	DT_PFCONV_DYNPREC	0x0020	/* dynamic precision (%.*) */
#define	DT_PFCONV_GROUP		0x0040	/* group thousands (%') */
#define	DT_PFCONV_SPACE		0x0080	/* insert leading space (% ) */
#define	DT_PFCONV_AGG		0x0100	/* use aggregation result (%@) */
#define	DT_PFCONV_SIGNED	0x0200	/* arg is a signed integer */

typedef struct dt_pfargv {
	dtrace_hdl_t *pfv_dtp;		/* libdtrace client handle */
	char *pfv_format;		/* format string pointer */
	dt_pfargd_t *pfv_argv;		/* list of argument descriptors */
	uint_t pfv_argc;		/* number of argument descriptors */
	uint_t pfv_flags;		/* flags used for validation */
} dt_pfargv_t;

typedef struct dt_pfwalk {
	const dt_pfargv_t *pfw_argv;	/* argument description list */
	uint_t pfw_aid;			/* aggregation variable identifier */
	FILE *pfw_fp;			/* file pointer to use for output */
	int pfw_err;			/* error status code */
} dt_pfwalk_t;

extern int dt_pfdict_create(dtrace_hdl_t *);
extern void dt_pfdict_destroy(dtrace_hdl_t *);

extern dt_pfargv_t *dt_printf_create(dtrace_hdl_t *, const char *);
extern void dt_printf_destroy(dt_pfargv_t *);

#define	DT_PRINTF_EXACTLEN	0x1	/* do not permit extra arguments */
#define	DT_PRINTF_AGGREGATION	0x2	/* enable aggregation conversion */

extern void dt_printf_validate(dt_pfargv_t *, uint_t,
    struct dt_ident *, int, dtrace_actkind_t, struct dt_node *);

extern void dt_printa_validate(struct dt_node *, struct dt_node *);

extern int dt_print_stack(dtrace_hdl_t *, FILE *,
    const char *, caddr_t, int, int);
extern int dt_print_ustack(dtrace_hdl_t *, FILE *,
    const char *, caddr_t, uint64_t);
extern int dt_print_mod(dtrace_hdl_t *, FILE *, const char *, caddr_t);
extern int dt_print_umod(dtrace_hdl_t *, FILE *, const char *, caddr_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _DT_PRINTF_H */
