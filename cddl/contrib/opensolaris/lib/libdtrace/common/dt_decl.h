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
/*
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright (c) 2013 Joyent, Inc. All rights reserved.
 */

#ifndef	_DT_DECL_H
#define	_DT_DECL_H

#include <sys/types.h>
#include <libctf.h>
#include <dtrace.h>
#include <stdio.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dt_node;				/* forward declaration of dt_node_t */

typedef struct dt_decl {
	ushort_t dd_kind;		/* declaration kind (CTF_K_* kind) */
	ushort_t dd_attr;		/* attributes (DT_DA_* flags) */
	ctf_file_t *dd_ctfp;		/* CTF container for decl's type */
	ctf_id_t dd_type;		/* CTF identifier for decl's type */
	char *dd_name;			/* string name of this decl (or NULL) */
	struct dt_node *dd_node;	/* node for array size or parm list */
	struct dt_decl *dd_next;	/* next declaration in list */
} dt_decl_t;

#define	DT_DA_SIGNED	0x0001		/* signed integer value */
#define	DT_DA_UNSIGNED	0x0002		/* unsigned integer value */
#define	DT_DA_SHORT	0x0004		/* short integer value */
#define	DT_DA_LONG	0x0008		/* long integer or double */
#define	DT_DA_LONGLONG	0x0010		/* long long integer value */
#define	DT_DA_CONST	0x0020		/* qualify type as const */
#define	DT_DA_RESTRICT	0x0040		/* qualify type as restrict */
#define	DT_DA_VOLATILE	0x0080		/* qualify type as volatile */
#define	DT_DA_PAREN	0x0100		/* parenthesis tag */
#define	DT_DA_USER	0x0200		/* user-land type specifier */

typedef enum dt_dclass {
	DT_DC_DEFAULT,			/* no storage class specified */
	DT_DC_AUTO,			/* automatic storage */
	DT_DC_REGISTER,			/* register storage */
	DT_DC_STATIC,			/* static storage */
	DT_DC_EXTERN,			/* extern storage */
	DT_DC_TYPEDEF,			/* type definition */
	DT_DC_SELF,			/* thread-local storage */
	DT_DC_THIS			/* clause-local storage */
} dt_dclass_t;

typedef struct dt_scope {
	dt_decl_t *ds_decl;		/* pointer to top of decl stack */
	struct dt_scope *ds_next;	/* pointer to next scope */
	char *ds_ident;			/* identifier for this scope (if any) */
	ctf_file_t *ds_ctfp;		/* CTF container for this scope */
	ctf_id_t ds_type;		/* CTF id of enclosing type */
	dt_dclass_t ds_class;		/* declaration class for this scope */
	int ds_enumval;			/* most recent enumerator value */
} dt_scope_t;

extern dt_decl_t *dt_decl_alloc(ushort_t, char *);
extern void dt_decl_free(dt_decl_t *);
extern void dt_decl_reset(void);
extern dt_decl_t *dt_decl_push(dt_decl_t *);
extern dt_decl_t *dt_decl_pop(void);
extern dt_decl_t *dt_decl_pop_param(char **);
extern dt_decl_t *dt_decl_top(void);

extern dt_decl_t *dt_decl_ident(char *);
extern void dt_decl_class(dt_dclass_t);

#define	DT_DP_VARARGS	0x1		/* permit varargs in prototype */
#define	DT_DP_DYNAMIC	0x2		/* permit dynamic type in prototype */
#define	DT_DP_VOID	0x4		/* permit void type in prototype */
#define	DT_DP_ANON	0x8		/* permit anonymous parameters */

extern int dt_decl_prototype(struct dt_node *, struct dt_node *,
    const char *, uint_t);

extern dt_decl_t *dt_decl_spec(ushort_t, char *);
extern dt_decl_t *dt_decl_attr(ushort_t);
extern dt_decl_t *dt_decl_array(struct dt_node *);
extern dt_decl_t *dt_decl_func(dt_decl_t *, struct dt_node *);
extern dt_decl_t *dt_decl_ptr(void);

extern dt_decl_t *dt_decl_sou(uint_t, char *);
extern void dt_decl_member(struct dt_node *);

extern dt_decl_t *dt_decl_enum(char *);
extern void dt_decl_enumerator(char *, struct dt_node *);

extern int dt_decl_type(dt_decl_t *, dtrace_typeinfo_t *);

extern void dt_scope_create(dt_scope_t *);
extern void dt_scope_destroy(dt_scope_t *);
extern void dt_scope_push(ctf_file_t *, ctf_id_t);
extern dt_decl_t *dt_scope_pop(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _DT_DECL_H */
