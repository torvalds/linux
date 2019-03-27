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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_LIBUUTIL_H
#define	_LIBUUTIL_H

#include <solaris.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Standard flags codes.
 */
#define	UU_DEFAULT		0

/*
 * Standard error codes.
 */
#define	UU_ERROR_NONE		0	/* no error */
#define	UU_ERROR_INVALID_ARGUMENT 1	/* invalid argument */
#define	UU_ERROR_UNKNOWN_FLAG	2	/* passed flag invalid */
#define	UU_ERROR_NO_MEMORY	3	/* out of memory */
#define	UU_ERROR_CALLBACK_FAILED 4	/* callback-initiated error */
#define	UU_ERROR_NOT_SUPPORTED	5	/* operation not supported */
#define	UU_ERROR_EMPTY		6	/* no value provided */
#define	UU_ERROR_UNDERFLOW	7	/* value is too small */
#define	UU_ERROR_OVERFLOW	8	/* value is too value */
#define	UU_ERROR_INVALID_CHAR	9	/* value contains unexpected char */
#define	UU_ERROR_INVALID_DIGIT	10	/* value contains digit not in base */

#define	UU_ERROR_SYSTEM		99	/* underlying system error */
#define	UU_ERROR_UNKNOWN	100	/* error status not known */

/*
 * Standard program exit codes.
 */
#define	UU_EXIT_OK	(*(uu_exit_ok()))
#define	UU_EXIT_FATAL	(*(uu_exit_fatal()))
#define	UU_EXIT_USAGE	(*(uu_exit_usage()))

/*
 * Exit status profiles.
 */
#define	UU_PROFILE_DEFAULT	0
#define	UU_PROFILE_LAUNCHER	1

/*
 * Error reporting functions.
 */
uint32_t uu_error(void);
const char *uu_strerror(uint32_t);

/*
 * Program notification functions.
 */
extern void uu_alt_exit(int);
extern const char *uu_setpname(char *);
extern const char *uu_getpname(void);
/*PRINTFLIKE1*/
extern void uu_warn(const char *, ...);
extern void uu_vwarn(const char *, va_list);
/*PRINTFLIKE1*/
extern void uu_die(const char *, ...) __NORETURN;
extern void uu_vdie(const char *, va_list) __NORETURN;
/*PRINTFLIKE2*/
extern void uu_xdie(int, const char *, ...) __NORETURN;
extern void uu_vxdie(int, const char *, va_list) __NORETURN;

/*
 * Exit status functions (not to be used directly)
 */
extern int *uu_exit_ok(void);
extern int *uu_exit_fatal(void);
extern int *uu_exit_usage(void);

/*
 * string->number conversions
 */
extern int uu_strtoint(const char *, void *, size_t, int, int64_t, int64_t);
extern int uu_strtouint(const char *, void *, size_t, int, uint64_t, uint64_t);

/*
 * Debug print facility functions.
 */
typedef struct uu_dprintf uu_dprintf_t;

typedef enum {
	UU_DPRINTF_SILENT,
	UU_DPRINTF_FATAL,
	UU_DPRINTF_WARNING,
	UU_DPRINTF_NOTICE,
	UU_DPRINTF_INFO,
	UU_DPRINTF_DEBUG
} uu_dprintf_severity_t;

extern uu_dprintf_t *uu_dprintf_create(const char *, uu_dprintf_severity_t,
    uint_t);
/*PRINTFLIKE3*/
extern void uu_dprintf(uu_dprintf_t *, uu_dprintf_severity_t,
    const char *, ...);
extern void uu_dprintf_destroy(uu_dprintf_t *);
extern const char *uu_dprintf_getname(uu_dprintf_t *);

/*
 * Identifier test flags and function.
 */
#define	UU_NAME_DOMAIN		0x1	/* allow SUNW, or com.sun, prefix */
#define	UU_NAME_PATH		0x2	/* allow '/'-delimited paths */

int uu_check_name(const char *, uint_t);

/*
 * File creation functions.
 */
extern int uu_open_tmp(const char *dir, uint_t uflags);

/*
 * Convenience functions.
 */
#define	UU_NELEM(a)	(sizeof (a) / sizeof ((a)[0]))

/*PRINTFLIKE1*/
extern char *uu_msprintf(const char *format, ...);
extern void *uu_zalloc(size_t);
extern char *uu_strdup(const char *);
extern void uu_free(void *);

extern boolean_t uu_strcaseeq(const char *a, const char *b);
extern boolean_t uu_streq(const char *a, const char *b);
extern char *uu_strndup(const char *s, size_t n);
extern boolean_t uu_strbw(const char *a, const char *b);
extern void *uu_memdup(const void *buf, size_t sz);
extern void uu_dump(FILE *out, const char *prefix, const void *buf, size_t len);

/*
 * Comparison function type definition.
 *   Developers should be careful in their use of the _private argument. If you
 *   break interface guarantees, you get undefined behavior.
 */
typedef int uu_compare_fn_t(const void *__left, const void *__right,
    void *__private);

/*
 * Walk variant flags.
 *   A data structure need not provide support for all variants and
 *   combinations.  Refer to the appropriate documentation.
 */
#define	UU_WALK_ROBUST		0x00000001	/* walk can survive removes */
#define	UU_WALK_REVERSE		0x00000002	/* reverse walk order */

#define	UU_WALK_PREORDER	0x00000010	/* walk tree in pre-order */
#define	UU_WALK_POSTORDER	0x00000020	/* walk tree in post-order */

/*
 * Walk callback function return codes.
 */
#define	UU_WALK_ERROR		-1
#define	UU_WALK_NEXT		0
#define	UU_WALK_DONE		1

/*
 * Walk callback function type definition.
 */
typedef int uu_walk_fn_t(void *_elem, void *_private);

/*
 * lists: opaque structures
 */
typedef struct uu_list_pool uu_list_pool_t;
typedef struct uu_list uu_list_t;

typedef struct uu_list_node {
	uintptr_t uln_opaque[2];
} uu_list_node_t;

typedef struct uu_list_walk uu_list_walk_t;

typedef uintptr_t uu_list_index_t;

/*
 * lists: interface
 *
 * basic usage:
 *	typedef struct foo {
 *		...
 *		uu_list_node_t foo_node;
 *		...
 *	} foo_t;
 *
 *	static int
 *	foo_compare(void *l_arg, void *r_arg, void *private)
 *	{
 *		foo_t *l = l_arg;
 *		foo_t *r = r_arg;
 *
 *		if (... l greater than r ...)
 *			return (1);
 *		if (... l less than r ...)
 *			return (-1);
 *		return (0);
 *	}
 *
 *	...
 *		// at initialization time
 *		foo_pool = uu_list_pool_create("foo_pool",
 *		    sizeof (foo_t), offsetof(foo_t, foo_node), foo_compare,
 *		    debugging? 0 : UU_AVL_POOL_DEBUG);
 *	...
 */
uu_list_pool_t *uu_list_pool_create(const char *, size_t, size_t,
    uu_compare_fn_t *, uint32_t);
#define	UU_LIST_POOL_DEBUG	0x00000001

void uu_list_pool_destroy(uu_list_pool_t *);

/*
 * usage:
 *
 *	foo_t *a;
 *	a = malloc(sizeof(*a));
 *	uu_list_node_init(a, &a->foo_list, pool);
 *	...
 *	uu_list_node_fini(a, &a->foo_list, pool);
 *	free(a);
 */
void uu_list_node_init(void *, uu_list_node_t *, uu_list_pool_t *);
void uu_list_node_fini(void *, uu_list_node_t *, uu_list_pool_t *);

uu_list_t *uu_list_create(uu_list_pool_t *, void *_parent, uint32_t);
#define	UU_LIST_DEBUG	0x00000001
#define	UU_LIST_SORTED	0x00000002	/* list is sorted */

void uu_list_destroy(uu_list_t *);	/* list must be empty */

size_t uu_list_numnodes(uu_list_t *);

void *uu_list_first(uu_list_t *);
void *uu_list_last(uu_list_t *);

void *uu_list_next(uu_list_t *, void *);
void *uu_list_prev(uu_list_t *, void *);

int uu_list_walk(uu_list_t *, uu_walk_fn_t *, void *, uint32_t);

uu_list_walk_t *uu_list_walk_start(uu_list_t *, uint32_t);
void *uu_list_walk_next(uu_list_walk_t *);
void uu_list_walk_end(uu_list_walk_t *);

void *uu_list_find(uu_list_t *, void *, void *, uu_list_index_t *);
void uu_list_insert(uu_list_t *, void *, uu_list_index_t);

void *uu_list_nearest_next(uu_list_t *, uu_list_index_t);
void *uu_list_nearest_prev(uu_list_t *, uu_list_index_t);

void *uu_list_teardown(uu_list_t *, void **);

void uu_list_remove(uu_list_t *, void *);

/*
 * lists: interfaces for non-sorted lists only
 */
int uu_list_insert_before(uu_list_t *, void *_target, void *_elem);
int uu_list_insert_after(uu_list_t *, void *_target, void *_elem);

/*
 * avl trees: opaque structures
 */
typedef struct uu_avl_pool uu_avl_pool_t;
typedef struct uu_avl uu_avl_t;

typedef struct uu_avl_node {
#ifdef _LP64
	uintptr_t uan_opaque[3];
#else
	uintptr_t uan_opaque[4];
#endif
} uu_avl_node_t;

typedef struct uu_avl_walk uu_avl_walk_t;

typedef uintptr_t uu_avl_index_t;

/*
 * avl trees: interface
 *
 * basic usage:
 *	typedef struct foo {
 *		...
 *		uu_avl_node_t foo_node;
 *		...
 *	} foo_t;
 *
 *	static int
 *	foo_compare(void *l_arg, void *r_arg, void *private)
 *	{
 *		foo_t *l = l_arg;
 *		foo_t *r = r_arg;
 *
 *		if (... l greater than r ...)
 *			return (1);
 *		if (... l less than r ...)
 *			return (-1);
 *		return (0);
 *	}
 *
 *	...
 *		// at initialization time
 *		foo_pool = uu_avl_pool_create("foo_pool",
 *		    sizeof (foo_t), offsetof(foo_t, foo_node), foo_compare,
 *		    debugging? 0 : UU_AVL_POOL_DEBUG);
 *	...
 */
uu_avl_pool_t *uu_avl_pool_create(const char *, size_t, size_t,
    uu_compare_fn_t *, uint32_t);
#define	UU_AVL_POOL_DEBUG	0x00000001

void uu_avl_pool_destroy(uu_avl_pool_t *);

/*
 * usage:
 *
 *	foo_t *a;
 *	a = malloc(sizeof(*a));
 *	uu_avl_node_init(a, &a->foo_avl, pool);
 *	...
 *	uu_avl_node_fini(a, &a->foo_avl, pool);
 *	free(a);
 */
void uu_avl_node_init(void *, uu_avl_node_t *, uu_avl_pool_t *);
void uu_avl_node_fini(void *, uu_avl_node_t *, uu_avl_pool_t *);

uu_avl_t *uu_avl_create(uu_avl_pool_t *, void *_parent, uint32_t);
#define	UU_AVL_DEBUG	0x00000001

void uu_avl_destroy(uu_avl_t *);	/* list must be empty */

size_t uu_avl_numnodes(uu_avl_t *);

void *uu_avl_first(uu_avl_t *);
void *uu_avl_last(uu_avl_t *);

void *uu_avl_next(uu_avl_t *, void *);
void *uu_avl_prev(uu_avl_t *, void *);

int uu_avl_walk(uu_avl_t *, uu_walk_fn_t *, void *, uint32_t);

uu_avl_walk_t *uu_avl_walk_start(uu_avl_t *, uint32_t);
void *uu_avl_walk_next(uu_avl_walk_t *);
void uu_avl_walk_end(uu_avl_walk_t *);

void *uu_avl_find(uu_avl_t *, void *, void *, uu_avl_index_t *);
void uu_avl_insert(uu_avl_t *, void *, uu_avl_index_t);

void *uu_avl_nearest_next(uu_avl_t *, uu_avl_index_t);
void *uu_avl_nearest_prev(uu_avl_t *, uu_avl_index_t);

void *uu_avl_teardown(uu_avl_t *, void **);

void uu_avl_remove(uu_avl_t *, void *);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBUUTIL_H */
