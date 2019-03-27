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
 * Copyright (c) 2000, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 */

#ifndef	_LIBNVPAIR_H
#define	_LIBNVPAIR_H

#include <sys/nvpair.h>
#include <stdlib.h>
#include <stdio.h>
#include <regex.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * All interfaces described in this file are private to Solaris, and
 * are subject to change at any time and without notice.  The public
 * nvlist/nvpair interfaces, as documented in manpage sections 3NVPAIR,
 * are all imported from <sys/nvpair.h> included above.
 */

extern int nvpair_value_match(nvpair_t *, int, char *, char **);
extern int nvpair_value_match_regex(nvpair_t *, int, char *, regex_t *,
    char **);

extern void nvlist_print(FILE *, nvlist_t *);
extern int nvlist_print_json(FILE *, nvlist_t *);
extern void dump_nvlist(nvlist_t *, int);

/*
 * Private nvlist printing interface that allows the caller some control
 * over output rendering (as opposed to nvlist_print and dump_nvlist).
 *
 * Obtain an opaque nvlist_prtctl_t cookie using nvlist_prtctl_alloc
 * (NULL on failure);  on return the cookie is set up for default formatting
 * and rendering.  Quote the cookie in subsequent customisation functions and
 * then pass the cookie to nvlist_prt to render the nvlist.  Finally,
 * use nvlist_prtctl_free to release the cookie.
 *
 * For all nvlist_lookup_xxx and nvlist_lookup_xxx_array functions
 * we have a corresponding brace of functions that appoint replacement
 * rendering functions:
 *
 *	extern void nvlist_prtctl_xxx(nvlist_prtctl_t,
 *	    void (*)(nvlist_prtctl_t ctl, void *private, const char *name,
 *	    xxxtype value))
 *
 *	and
 *
 *	extern void nvlist_prtctl_xxx_array(nvlist_prtctl_t,
 *	    void (*)(nvlist_prtctl_t ctl, void *private, const char *name,
 *	    xxxtype value, uint_t count))
 *
 * where xxxtype is the C datatype corresponding to xxx, eg int8_t for "int8"
 * and char * for "string".  The function that is appointed to render the
 * specified datatype receives as arguments the cookie, the nvlist
 * member name, the value of that member (or a pointer for array function),
 * and (for array rendering functions) a count of the number of elements.
 */

typedef struct nvlist_prtctl *nvlist_prtctl_t;	/* opaque */

enum nvlist_indent_mode {
	NVLIST_INDENT_ABS,	/* Absolute indentation */
	NVLIST_INDENT_TABBED	/* Indent with tabstops */
};

extern nvlist_prtctl_t nvlist_prtctl_alloc(void);
extern void nvlist_prtctl_free(nvlist_prtctl_t);
extern void nvlist_prt(nvlist_t *, nvlist_prtctl_t);

/* Output stream */
extern void nvlist_prtctl_setdest(nvlist_prtctl_t, FILE *);
extern FILE *nvlist_prtctl_getdest(nvlist_prtctl_t);

/* Indentation mode, start indent, indent increment; default tabbed/0/1 */
extern void nvlist_prtctl_setindent(nvlist_prtctl_t, enum nvlist_indent_mode,
    int, int);
extern void nvlist_prtctl_doindent(nvlist_prtctl_t, int);

enum nvlist_prtctl_fmt {
	NVLIST_FMT_MEMBER_NAME,		/* name fmt; default "%s = " */
	NVLIST_FMT_MEMBER_POSTAMBLE,	/* after nvlist member; default "\n" */
	NVLIST_FMT_BTWN_ARRAY		/* between array members; default " " */
};

extern void nvlist_prtctl_setfmt(nvlist_prtctl_t, enum nvlist_prtctl_fmt,
    const char *);
extern void nvlist_prtctl_dofmt(nvlist_prtctl_t, enum nvlist_prtctl_fmt, ...);

/*
 * Function prototypes for interfaces that appoint a new rendering function
 * for single-valued nvlist members.
 *
 * A replacement function receives arguments as follows:
 *
 *	nvlist_prtctl_t	Print control structure; do not change preferences
 *			for this object from a print callback function.
 *
 *	void *		The function-private cookie argument registered
 *			when the replacement function was appointed.
 *
 *	nvlist_t *	The full nvlist that is being processed.  The
 *			rendering function is called to render a single
 *			member (name and value passed as below) but it may
 *			want to reference or incorporate other aspects of
 *			the full nvlist.
 *
 *	const char *	Member name to render
 *
 *	valtype		Value of the member to render
 *
 * The function must return non-zero if it has rendered output for this
 * member, or 0 if it wants to default to standard rendering for this
 * one member.
 */

#define	NVLIST_PRINTCTL_SVDECL(funcname, valtype) \
    extern void funcname(nvlist_prtctl_t, \
    int (*)(nvlist_prtctl_t, void *, nvlist_t *, const char *, valtype), \
    void *)

NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_boolean, int);
NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_boolean_value, boolean_t);
NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_byte, uchar_t);
NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_int8, int8_t);
NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_uint8, uint8_t);
NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_int16, int16_t);
NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_uint16, uint16_t);
NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_int32, int32_t);
NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_uint32, uint32_t);
NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_int64, int64_t);
NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_uint64, uint64_t);
NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_double, double);
NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_string, char *);
NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_hrtime, hrtime_t);
NVLIST_PRINTCTL_SVDECL(nvlist_prtctlop_nvlist, nvlist_t *);

#undef	NVLIST_PRINTCTL_SVDECL	/* was just for "clarity" above */

/*
 * Function prototypes for interfaces that appoint a new rendering function
 * for array-valued nvlist members.
 *
 * One additional argument is taken: uint_t for the number of array elements
 *
 * Return values as above.
 */
#define	NVLIST_PRINTCTL_AVDECL(funcname, vtype) \
    extern void funcname(nvlist_prtctl_t, \
    int (*)(nvlist_prtctl_t, void *, nvlist_t *, const char *, vtype, uint_t), \
    void *)

NVLIST_PRINTCTL_AVDECL(nvlist_prtctlop_boolean_array, boolean_t *);
NVLIST_PRINTCTL_AVDECL(nvlist_prtctlop_byte_array, uchar_t *);
NVLIST_PRINTCTL_AVDECL(nvlist_prtctlop_int8_array, int8_t *);
NVLIST_PRINTCTL_AVDECL(nvlist_prtctlop_uint8_array, uint8_t *);
NVLIST_PRINTCTL_AVDECL(nvlist_prtctlop_int16_array, int16_t *);
NVLIST_PRINTCTL_AVDECL(nvlist_prtctlop_uint16_array, uint16_t *);
NVLIST_PRINTCTL_AVDECL(nvlist_prtctlop_int32_array, int32_t *);
NVLIST_PRINTCTL_AVDECL(nvlist_prtctlop_uint32_array, uint32_t *);
NVLIST_PRINTCTL_AVDECL(nvlist_prtctlop_int64_array, int64_t *);
NVLIST_PRINTCTL_AVDECL(nvlist_prtctlop_uint64_array, uint64_t *);
NVLIST_PRINTCTL_AVDECL(nvlist_prtctlop_string_array, char **);
NVLIST_PRINTCTL_AVDECL(nvlist_prtctlop_nvlist_array, nvlist_t **);

#undef	NVLIST_PRINTCTL_AVDECL	/* was just for "clarity" above */

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBNVPAIR_H */
