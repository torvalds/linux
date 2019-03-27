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

#ifndef	_DT_IDENT_H
#define	_DT_IDENT_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <libctf.h>
#include <dtrace.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include <dt_list.h>

struct dt_node;
struct dt_ident;
struct dt_idhash;
struct dt_irlist;
struct dt_regset;

typedef struct dt_idsig {
	int dis_varargs;	/* argument index of start of varargs (or -1) */
	int dis_optargs;	/* argument index of start of optargs (or -1) */
	int dis_argc;		/* number of types in this signature */
	struct dt_node *dis_args; /* array of nodes representing formal types */
	uint64_t dis_auxinfo;	/* auxiliary signature information, if any */
} dt_idsig_t;

typedef struct dt_idnode {
	struct dt_node *din_list; /* allocation list for parse tree nodes */
	struct dt_node *din_root; /* root of this identifier's parse tree */
	struct dt_idhash *din_hash; /* identifiers private to this subtree */
	struct dt_ident **din_argv; /* identifiers in din_hash for arguments */
	int din_argc;		  /* length of din_argv[] array */
} dt_idnode_t;

typedef struct dt_idops {
	void (*di_cook)(struct dt_node *, struct dt_ident *,
	    int, struct dt_node *);
	void (*di_dtor)(struct dt_ident *);
	size_t (*di_size)(struct dt_ident *);
} dt_idops_t;

typedef struct dt_ident {
	char *di_name;		/* identifier name */
	ushort_t di_kind;	/* identifier kind (see below) */
	ushort_t di_flags;	/* identifier flags (see below) */
	uint_t di_id;		/* variable or subr id (see <sys/dtrace.h>) */
	dtrace_attribute_t di_attr; /* identifier stability attributes */
	uint_t di_vers;		/* identifier version number (dt_version_t) */
	const dt_idops_t *di_ops; /* identifier's class-specific ops vector */
	void *di_iarg;		/* initial argument pointer for ops vector */
	void *di_data;		/* private data pointer for ops vector */
	ctf_file_t *di_ctfp;	/* CTF container for the variable data type */
	ctf_id_t di_type;	/* CTF identifier for the variable data type */
	struct dt_ident *di_next; /* pointer to next ident in hash chain */
	ulong_t di_gen;		/* generation number (pass that created me) */
	int di_lineno;		/* line number that defined this identifier */
} dt_ident_t;

#define	DT_IDENT_ARRAY	0	/* identifier is an array variable */
#define	DT_IDENT_SCALAR	1	/* identifier is a scalar variable */
#define	DT_IDENT_PTR	2	/* identifier is a magic pointer */
#define	DT_IDENT_FUNC	3	/* identifier is a built-in function */
#define	DT_IDENT_AGG	4	/* identifier is an aggregation */
#define	DT_IDENT_AGGFUNC 5	/* identifier is an aggregating function */
#define	DT_IDENT_ACTFUNC 6	/* identifier is an action function */
#define	DT_IDENT_XLSOU	7	/* identifier is a translated struct or union */
#define	DT_IDENT_XLPTR	8	/* identifier is a translated pointer */
#define	DT_IDENT_SYMBOL	9	/* identifier is an external symbol */
#define	DT_IDENT_ENUM	10	/* identifier is an enumerator */
#define	DT_IDENT_PRAGAT	11	/* identifier is #pragma attributes */
#define	DT_IDENT_PRAGBN	12	/* identifier is #pragma binding */
#define	DT_IDENT_PROBE	13	/* identifier is a probe definition */

#define	DT_IDFLG_TLS	0x0001	/* variable is thread-local storage */
#define	DT_IDFLG_LOCAL	0x0002	/* variable is local storage */
#define	DT_IDFLG_WRITE	0x0004	/* variable is writable (can be modified) */
#define	DT_IDFLG_INLINE	0x0008	/* variable is an inline definition */
#define	DT_IDFLG_REF	0x0010	/* variable is referenced by this program */
#define	DT_IDFLG_MOD	0x0020	/* variable is modified by this program */
#define	DT_IDFLG_DIFR	0x0040	/* variable is referenced by current DIFO */
#define	DT_IDFLG_DIFW	0x0080	/* variable is modified by current DIFO */
#define	DT_IDFLG_CGREG	0x0100	/* variable is inlined by code generator */
#define	DT_IDFLG_USER	0x0200	/* variable is associated with userland */
#define	DT_IDFLG_PRIM	0x0400	/* variable is associated with primary object */
#define	DT_IDFLG_DECL	0x0800	/* variable is associated with explicit decl */
#define	DT_IDFLG_ORPHAN	0x1000	/* variable is in a dt_node and not dt_idhash */

typedef struct dt_idhash {
	dt_list_t dh_list;	/* list prev/next pointers for dt_idstack */
	const char *dh_name;	/* name of this hash table */
	void (*dh_defer)(struct dt_idhash *, dt_ident_t *); /* defer callback */
	const dt_ident_t *dh_tmpl; /* template for initial ident population */
	uint_t dh_nextid;	/* next id to be returned by idhash_nextid() */
	uint_t dh_minid;	/* min id to be returned by idhash_nextid() */
	uint_t dh_maxid;	/* max id to be returned by idhash_nextid() */
	ulong_t dh_nelems;	/* number of identifiers in hash table */
	ulong_t dh_hashsz;	/* number of entries in dh_buckets array */
	dt_ident_t *dh_hash[1];	/* array of hash table bucket pointers */
} dt_idhash_t;

typedef struct dt_idstack {
	dt_list_t dids_list;	/* list meta-data for dt_idhash_t stack */
} dt_idstack_t;

extern const dt_idops_t dt_idops_assc;	/* associative array or aggregation */
extern const dt_idops_t dt_idops_func;	/* function call built-in */
extern const dt_idops_t dt_idops_args;	/* args[] built-in */
extern const dt_idops_t dt_idops_regs;	/* regs[]/uregs[] built-in */
extern const dt_idops_t dt_idops_type;	/* predefined type name string */
extern const dt_idops_t dt_idops_thaw;	/* prefrozen type identifier */
extern const dt_idops_t dt_idops_inline; /* inline variable */
extern const dt_idops_t dt_idops_probe;	/* probe definition */

extern dt_idhash_t *dt_idhash_create(const char *, const dt_ident_t *,
    uint_t, uint_t);
extern void dt_idhash_destroy(dt_idhash_t *);
extern void dt_idhash_update(dt_idhash_t *);
extern dt_ident_t *dt_idhash_lookup(dt_idhash_t *, const char *);
extern int dt_idhash_nextid(dt_idhash_t *, uint_t *);
extern ulong_t dt_idhash_size(const dt_idhash_t *);
extern const char *dt_idhash_name(const dt_idhash_t *);

extern dt_ident_t *dt_idhash_insert(dt_idhash_t *, const char *, ushort_t,
    ushort_t, uint_t, dtrace_attribute_t, uint_t,
    const dt_idops_t *, void *, ulong_t);

extern void dt_idhash_xinsert(dt_idhash_t *, dt_ident_t *);
extern void dt_idhash_delete(dt_idhash_t *, dt_ident_t *);

typedef int dt_idhash_f(dt_idhash_t *, dt_ident_t *, void *);
extern int dt_idhash_iter(dt_idhash_t *, dt_idhash_f *, void *);

extern dt_ident_t *dt_idstack_lookup(dt_idstack_t *, const char *);
extern void dt_idstack_push(dt_idstack_t *, dt_idhash_t *);
extern void dt_idstack_pop(dt_idstack_t *, dt_idhash_t *);

extern dt_ident_t *dt_ident_create(const char *, ushort_t, ushort_t, uint_t,
    dtrace_attribute_t, uint_t, const dt_idops_t *, void *, ulong_t);
extern void dt_ident_destroy(dt_ident_t *);
extern void dt_ident_morph(dt_ident_t *, ushort_t, const dt_idops_t *, void *);
extern dtrace_attribute_t dt_ident_cook(struct dt_node *,
    dt_ident_t *, struct dt_node **);

extern void dt_ident_type_assign(dt_ident_t *, ctf_file_t *, ctf_id_t);
extern dt_ident_t *dt_ident_resolve(dt_ident_t *);
extern size_t dt_ident_size(dt_ident_t *);
extern int dt_ident_unref(const dt_ident_t *);

extern const char *dt_idkind_name(uint_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _DT_IDENT_H */
