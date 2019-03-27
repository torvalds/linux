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
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#include <solaris.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <libintl.h>
#include <stdarg.h>
#include "libnvpair.h"

/*
 * libnvpair - A tools library for manipulating <name, value> pairs.
 *
 *	This library provides routines packing an unpacking nv pairs
 *	for transporting data across process boundaries, transporting
 *	between kernel and userland, and possibly saving onto disk files.
 */

/*
 * Print control structure.
 */

#define	DEFINEOP(opname, vtype) \
	struct { \
		int (*op)(struct nvlist_prtctl *, void *, nvlist_t *, \
		    const char *, vtype); \
		void *arg; \
	} opname

#define	DEFINEARROP(opname, vtype) \
	struct { \
		int (*op)(struct nvlist_prtctl *, void *, nvlist_t *, \
		    const char *, vtype, uint_t); \
		void *arg; \
	} opname

struct nvlist_printops {
	DEFINEOP(print_boolean, int);
	DEFINEOP(print_boolean_value, boolean_t);
	DEFINEOP(print_byte, uchar_t);
	DEFINEOP(print_int8, int8_t);
	DEFINEOP(print_uint8, uint8_t);
	DEFINEOP(print_int16, int16_t);
	DEFINEOP(print_uint16, uint16_t);
	DEFINEOP(print_int32, int32_t);
	DEFINEOP(print_uint32, uint32_t);
	DEFINEOP(print_int64, int64_t);
	DEFINEOP(print_uint64, uint64_t);
	DEFINEOP(print_double, double);
	DEFINEOP(print_string, char *);
	DEFINEOP(print_hrtime, hrtime_t);
	DEFINEOP(print_nvlist, nvlist_t *);
	DEFINEARROP(print_boolean_array, boolean_t *);
	DEFINEARROP(print_byte_array, uchar_t *);
	DEFINEARROP(print_int8_array, int8_t *);
	DEFINEARROP(print_uint8_array, uint8_t *);
	DEFINEARROP(print_int16_array, int16_t *);
	DEFINEARROP(print_uint16_array, uint16_t *);
	DEFINEARROP(print_int32_array, int32_t *);
	DEFINEARROP(print_uint32_array, uint32_t *);
	DEFINEARROP(print_int64_array, int64_t *);
	DEFINEARROP(print_uint64_array, uint64_t *);
	DEFINEARROP(print_string_array, char **);
	DEFINEARROP(print_nvlist_array, nvlist_t **);
};

struct nvlist_prtctl {
	FILE *nvprt_fp;			/* output destination */
	enum nvlist_indent_mode nvprt_indent_mode; /* see above */
	int nvprt_indent;		/* absolute indent, or tab depth */
	int nvprt_indentinc;		/* indent or tab increment */
	const char *nvprt_nmfmt;	/* member name format, max one %s */
	const char *nvprt_eomfmt;	/* after member format, e.g. "\n" */
	const char *nvprt_btwnarrfmt;	/* between array members */
	int nvprt_btwnarrfmt_nl;	/* nvprt_eoamfmt includes newline? */
	struct nvlist_printops *nvprt_dfltops;
	struct nvlist_printops *nvprt_custops;
};

#define	DFLTPRTOP(pctl, type) \
	((pctl)->nvprt_dfltops->print_##type.op)

#define	DFLTPRTOPARG(pctl, type) \
	((pctl)->nvprt_dfltops->print_##type.arg)

#define	CUSTPRTOP(pctl, type) \
	((pctl)->nvprt_custops->print_##type.op)

#define	CUSTPRTOPARG(pctl, type) \
	((pctl)->nvprt_custops->print_##type.arg)

#define	RENDER(pctl, type, nvl, name, val) \
	{ \
		int done = 0; \
		if ((pctl)->nvprt_custops && CUSTPRTOP(pctl, type)) { \
			done = CUSTPRTOP(pctl, type)(pctl, \
			    CUSTPRTOPARG(pctl, type), nvl, name, val); \
		} \
		if (!done) { \
			(void) DFLTPRTOP(pctl, type)(pctl, \
			    DFLTPRTOPARG(pctl, type), nvl, name, val); \
		} \
		(void) fprintf(pctl->nvprt_fp, pctl->nvprt_eomfmt); \
	}

#define	ARENDER(pctl, type, nvl, name, arrp, count) \
	{ \
		int done = 0; \
		if ((pctl)->nvprt_custops && CUSTPRTOP(pctl, type)) { \
			done = CUSTPRTOP(pctl, type)(pctl, \
			    CUSTPRTOPARG(pctl, type), nvl, name, arrp, count); \
		} \
		if (!done) { \
			(void) DFLTPRTOP(pctl, type)(pctl, \
			    DFLTPRTOPARG(pctl, type), nvl, name, arrp, count); \
		} \
		(void) fprintf(pctl->nvprt_fp, pctl->nvprt_eomfmt); \
	}

static void nvlist_print_with_indent(nvlist_t *, nvlist_prtctl_t);

/*
 * ======================================================================
 * |									|
 * | Indentation							|
 * |									|
 * ======================================================================
 */

static void
indent(nvlist_prtctl_t pctl, int onemore)
{
	int depth;

	switch (pctl->nvprt_indent_mode) {
	case NVLIST_INDENT_ABS:
		(void) fprintf(pctl->nvprt_fp, "%*s",
		    pctl->nvprt_indent + onemore * pctl->nvprt_indentinc, "");
		break;

	case NVLIST_INDENT_TABBED:
		depth = pctl->nvprt_indent + onemore;
		while (depth-- > 0)
			(void) fprintf(pctl->nvprt_fp, "\t");
	}
}

/*
 * ======================================================================
 * |									|
 * | Default nvlist member rendering functions.				|
 * |									|
 * ======================================================================
 */

/*
 * Generate functions to print single-valued nvlist members.
 *
 * type_and_variant - suffix to form function name
 * vtype - C type for the member value
 * ptype - C type to cast value to for printing
 * vfmt - format string for pair value, e.g "%d" or "0x%llx"
 */

#define	NVLIST_PRTFUNC(type_and_variant, vtype, ptype, vfmt) \
static int \
nvprint_##type_and_variant(nvlist_prtctl_t pctl, void *private, \
    nvlist_t *nvl, const char *name, vtype value) \
{ \
	FILE *fp = pctl->nvprt_fp; \
	NOTE(ARGUNUSED(private)) \
	NOTE(ARGUNUSED(nvl)) \
	indent(pctl, 1); \
	(void) fprintf(fp, pctl->nvprt_nmfmt, name); \
	(void) fprintf(fp, vfmt, (ptype)value); \
	return (1); \
}

NVLIST_PRTFUNC(boolean, int, int, "%d")
NVLIST_PRTFUNC(boolean_value, boolean_t, int, "%d")
NVLIST_PRTFUNC(byte, uchar_t, uchar_t, "0x%2.2x")
NVLIST_PRTFUNC(int8, int8_t, int, "%d")
NVLIST_PRTFUNC(uint8, uint8_t, uint8_t, "0x%x")
NVLIST_PRTFUNC(int16, int16_t, int16_t, "%d")
NVLIST_PRTFUNC(uint16, uint16_t, uint16_t, "0x%x")
NVLIST_PRTFUNC(int32, int32_t, int32_t, "%d")
NVLIST_PRTFUNC(uint32, uint32_t, uint32_t, "0x%x")
NVLIST_PRTFUNC(int64, int64_t, longlong_t, "%lld")
NVLIST_PRTFUNC(uint64, uint64_t, u_longlong_t, "0x%llx")
NVLIST_PRTFUNC(double, double, double, "0x%f")
NVLIST_PRTFUNC(string, char *, char *, "%s")
NVLIST_PRTFUNC(hrtime, hrtime_t, hrtime_t, "0x%llx")

/*
 * Generate functions to print array-valued nvlist members.
 */

#define	NVLIST_ARRPRTFUNC(type_and_variant, vtype, ptype, vfmt) \
static int \
nvaprint_##type_and_variant(nvlist_prtctl_t pctl, void *private, \
    nvlist_t *nvl, const char *name, vtype *valuep, uint_t count) \
{ \
	FILE *fp = pctl->nvprt_fp; \
	uint_t i; \
	NOTE(ARGUNUSED(private)) \
	NOTE(ARGUNUSED(nvl)) \
	for (i = 0; i < count; i++) { \
		if (i == 0 || pctl->nvprt_btwnarrfmt_nl) { \
			indent(pctl, 1); \
			(void) fprintf(fp, pctl->nvprt_nmfmt, name); \
			if (pctl->nvprt_btwnarrfmt_nl) \
				(void) fprintf(fp, "[%d]: ", i); \
		} \
		if (i != 0) \
			(void) fprintf(fp, pctl->nvprt_btwnarrfmt); \
		(void) fprintf(fp, vfmt, (ptype)valuep[i]); \
	} \
	return (1); \
}

NVLIST_ARRPRTFUNC(boolean_array, boolean_t, boolean_t, "%d")
NVLIST_ARRPRTFUNC(byte_array, uchar_t, uchar_t, "0x%2.2x")
NVLIST_ARRPRTFUNC(int8_array, int8_t, int8_t, "%d")
NVLIST_ARRPRTFUNC(uint8_array, uint8_t, uint8_t, "0x%x")
NVLIST_ARRPRTFUNC(int16_array, int16_t, int16_t, "%d")
NVLIST_ARRPRTFUNC(uint16_array, uint16_t, uint16_t, "0x%x")
NVLIST_ARRPRTFUNC(int32_array, int32_t, int32_t, "%d")
NVLIST_ARRPRTFUNC(uint32_array, uint32_t, uint32_t, "0x%x")
NVLIST_ARRPRTFUNC(int64_array, int64_t, longlong_t, "%lld")
NVLIST_ARRPRTFUNC(uint64_array, uint64_t, u_longlong_t, "0x%llx")
NVLIST_ARRPRTFUNC(string_array, char *, char *, "%s")

/*ARGSUSED*/
static int
nvprint_nvlist(nvlist_prtctl_t pctl, void *private,
    nvlist_t *nvl, const char *name, nvlist_t *value)
{
	FILE *fp = pctl->nvprt_fp;

	indent(pctl, 1);
	(void) fprintf(fp, "%s = (embedded nvlist)\n", name);

	pctl->nvprt_indent += pctl->nvprt_indentinc;
	nvlist_print_with_indent(value, pctl);
	pctl->nvprt_indent -= pctl->nvprt_indentinc;

	indent(pctl, 1);
	(void) fprintf(fp, "(end %s)\n", name);

	return (1);
}

/*ARGSUSED*/
static int
nvaprint_nvlist_array(nvlist_prtctl_t pctl, void *private,
    nvlist_t *nvl, const char *name, nvlist_t **valuep, uint_t count)
{
	FILE *fp = pctl->nvprt_fp;
	uint_t i;

	indent(pctl, 1);
	(void) fprintf(fp, "%s = (array of embedded nvlists)\n", name);

	for (i = 0; i < count; i++) {
		indent(pctl, 1);
		(void) fprintf(fp, "(start %s[%d])\n", name, i);

		pctl->nvprt_indent += pctl->nvprt_indentinc;
		nvlist_print_with_indent(valuep[i], pctl);
		pctl->nvprt_indent -= pctl->nvprt_indentinc;

		indent(pctl, 1);
		(void) fprintf(fp, "(end %s[%d])\n", name, i);
	}

	return (1);
}

/*
 * ======================================================================
 * |									|
 * | Interfaces that allow control over formatting.			|
 * |									|
 * ======================================================================
 */

void
nvlist_prtctl_setdest(nvlist_prtctl_t pctl, FILE *fp)
{
	pctl->nvprt_fp = fp;
}

FILE *
nvlist_prtctl_getdest(nvlist_prtctl_t pctl)
{
	return (pctl->nvprt_fp);
}


void
nvlist_prtctl_setindent(nvlist_prtctl_t pctl, enum nvlist_indent_mode mode,
    int start, int inc)
{
	if (mode < NVLIST_INDENT_ABS || mode > NVLIST_INDENT_TABBED)
		mode = NVLIST_INDENT_TABBED;

	if (start < 0)
		start = 0;

	if (inc < 0)
		inc = 1;

	pctl->nvprt_indent_mode = mode;
	pctl->nvprt_indent = start;
	pctl->nvprt_indentinc = inc;
}

void
nvlist_prtctl_doindent(nvlist_prtctl_t pctl, int onemore)
{
	indent(pctl, onemore);
}


void
nvlist_prtctl_setfmt(nvlist_prtctl_t pctl, enum nvlist_prtctl_fmt which,
    const char *fmt)
{
	switch (which) {
	case NVLIST_FMT_MEMBER_NAME:
		if (fmt == NULL)
			fmt = "%s = ";
		pctl->nvprt_nmfmt = fmt;
		break;

	case NVLIST_FMT_MEMBER_POSTAMBLE:
		if (fmt == NULL)
			fmt = "\n";
		pctl->nvprt_eomfmt = fmt;
		break;

	case NVLIST_FMT_BTWN_ARRAY:
		if (fmt == NULL) {
			pctl->nvprt_btwnarrfmt = " ";
			pctl->nvprt_btwnarrfmt_nl = 0;
		} else {
			pctl->nvprt_btwnarrfmt = fmt;
			pctl->nvprt_btwnarrfmt_nl = (strstr(fmt, "\n") != NULL);
		}
		break;

	default:
		break;
	}
}


void
nvlist_prtctl_dofmt(nvlist_prtctl_t pctl, enum nvlist_prtctl_fmt which, ...)
{
	FILE *fp = pctl->nvprt_fp;
	va_list ap;
	char *name;

	va_start(ap, which);

	switch (which) {
	case NVLIST_FMT_MEMBER_NAME:
		name = va_arg(ap, char *);
		(void) fprintf(fp, pctl->nvprt_nmfmt, name);
		break;

	case NVLIST_FMT_MEMBER_POSTAMBLE:
		(void) fprintf(fp, pctl->nvprt_eomfmt);
		break;

	case NVLIST_FMT_BTWN_ARRAY:
		(void) fprintf(fp, pctl->nvprt_btwnarrfmt); \
		break;

	default:
		break;
	}

	va_end(ap);
}

/*
 * ======================================================================
 * |									|
 * | Interfaces to allow appointment of replacement rendering functions.|
 * |									|
 * ======================================================================
 */

#define	NVLIST_PRINTCTL_REPLACE(type, vtype) \
void \
nvlist_prtctlop_##type(nvlist_prtctl_t pctl, \
    int (*func)(nvlist_prtctl_t, void *, nvlist_t *, const char *, vtype), \
    void *private) \
{ \
	CUSTPRTOP(pctl, type) = func; \
	CUSTPRTOPARG(pctl, type) = private; \
}

NVLIST_PRINTCTL_REPLACE(boolean, int)
NVLIST_PRINTCTL_REPLACE(boolean_value, boolean_t)
NVLIST_PRINTCTL_REPLACE(byte, uchar_t)
NVLIST_PRINTCTL_REPLACE(int8, int8_t)
NVLIST_PRINTCTL_REPLACE(uint8, uint8_t)
NVLIST_PRINTCTL_REPLACE(int16, int16_t)
NVLIST_PRINTCTL_REPLACE(uint16, uint16_t)
NVLIST_PRINTCTL_REPLACE(int32, int32_t)
NVLIST_PRINTCTL_REPLACE(uint32, uint32_t)
NVLIST_PRINTCTL_REPLACE(int64, int64_t)
NVLIST_PRINTCTL_REPLACE(uint64, uint64_t)
NVLIST_PRINTCTL_REPLACE(double, double)
NVLIST_PRINTCTL_REPLACE(string, char *)
NVLIST_PRINTCTL_REPLACE(hrtime, hrtime_t)
NVLIST_PRINTCTL_REPLACE(nvlist, nvlist_t *)

#define	NVLIST_PRINTCTL_AREPLACE(type, vtype) \
void \
nvlist_prtctlop_##type(nvlist_prtctl_t pctl, \
    int (*func)(nvlist_prtctl_t, void *, nvlist_t *, const char *, vtype, \
    uint_t), void *private) \
{ \
	CUSTPRTOP(pctl, type) = func; \
	CUSTPRTOPARG(pctl, type) = private; \
}

NVLIST_PRINTCTL_AREPLACE(boolean_array, boolean_t *)
NVLIST_PRINTCTL_AREPLACE(byte_array, uchar_t *)
NVLIST_PRINTCTL_AREPLACE(int8_array, int8_t *)
NVLIST_PRINTCTL_AREPLACE(uint8_array, uint8_t *)
NVLIST_PRINTCTL_AREPLACE(int16_array, int16_t *)
NVLIST_PRINTCTL_AREPLACE(uint16_array, uint16_t *)
NVLIST_PRINTCTL_AREPLACE(int32_array, int32_t *)
NVLIST_PRINTCTL_AREPLACE(uint32_array, uint32_t *)
NVLIST_PRINTCTL_AREPLACE(int64_array, int64_t *)
NVLIST_PRINTCTL_AREPLACE(uint64_array, uint64_t *)
NVLIST_PRINTCTL_AREPLACE(string_array, char **)
NVLIST_PRINTCTL_AREPLACE(nvlist_array, nvlist_t **)

/*
 * ======================================================================
 * |									|
 * | Interfaces to manage nvlist_prtctl_t cookies.			|
 * |									|
 * ======================================================================
 */


static const struct nvlist_printops defprtops = {
	{ nvprint_boolean, NULL },
	{ nvprint_boolean_value, NULL },
	{ nvprint_byte, NULL },
	{ nvprint_int8, NULL },
	{ nvprint_uint8, NULL },
	{ nvprint_int16, NULL },
	{ nvprint_uint16, NULL },
	{ nvprint_int32, NULL },
	{ nvprint_uint32, NULL },
	{ nvprint_int64, NULL },
	{ nvprint_uint64, NULL },
	{ nvprint_double, NULL },
	{ nvprint_string, NULL },
	{ nvprint_hrtime, NULL },
	{ nvprint_nvlist, NULL },
	{ nvaprint_boolean_array, NULL },
	{ nvaprint_byte_array, NULL },
	{ nvaprint_int8_array, NULL },
	{ nvaprint_uint8_array, NULL },
	{ nvaprint_int16_array, NULL },
	{ nvaprint_uint16_array, NULL },
	{ nvaprint_int32_array, NULL },
	{ nvaprint_uint32_array, NULL },
	{ nvaprint_int64_array, NULL },
	{ nvaprint_uint64_array, NULL },
	{ nvaprint_string_array, NULL },
	{ nvaprint_nvlist_array, NULL },
};

static void
prtctl_defaults(FILE *fp, struct nvlist_prtctl *pctl,
    struct nvlist_printops *ops)
{
	pctl->nvprt_fp = fp;
	pctl->nvprt_indent_mode = NVLIST_INDENT_TABBED;
	pctl->nvprt_indent = 0;
	pctl->nvprt_indentinc = 1;
	pctl->nvprt_nmfmt = "%s = ";
	pctl->nvprt_eomfmt = "\n";
	pctl->nvprt_btwnarrfmt = " ";
	pctl->nvprt_btwnarrfmt_nl = 0;

	pctl->nvprt_dfltops = (struct nvlist_printops *)&defprtops;
	pctl->nvprt_custops = ops;
}

nvlist_prtctl_t
nvlist_prtctl_alloc(void)
{
	struct nvlist_prtctl *pctl;
	struct nvlist_printops *ops;

	if ((pctl = malloc(sizeof (*pctl))) == NULL)
		return (NULL);

	if ((ops = calloc(1, sizeof (*ops))) == NULL) {
		free(pctl);
		return (NULL);
	}

	prtctl_defaults(stdout, pctl, ops);

	return (pctl);
}

void
nvlist_prtctl_free(nvlist_prtctl_t pctl)
{
	if (pctl != NULL) {
		free(pctl->nvprt_custops);
		free(pctl);
	}
}

/*
 * ======================================================================
 * |									|
 * | Top-level print request interfaces.				|
 * |									|
 * ======================================================================
 */

/*
 * nvlist_print - Prints elements in an event buffer
 */
static void
nvlist_print_with_indent(nvlist_t *nvl, nvlist_prtctl_t pctl)
{
	FILE *fp = pctl->nvprt_fp;
	char *name;
	uint_t nelem;
	nvpair_t *nvp;

	if (nvl == NULL)
		return;

	indent(pctl, 0);
	(void) fprintf(fp, "nvlist version: %d\n", NVL_VERSION(nvl));

	nvp = nvlist_next_nvpair(nvl, NULL);

	while (nvp) {
		data_type_t type = nvpair_type(nvp);

		name = nvpair_name(nvp);
		nelem = 0;

		switch (type) {
		case DATA_TYPE_BOOLEAN: {
			RENDER(pctl, boolean, nvl, name, 1);
			break;
		}
		case DATA_TYPE_BOOLEAN_VALUE: {
			boolean_t val;
			(void) nvpair_value_boolean_value(nvp, &val);
			RENDER(pctl, boolean_value, nvl, name, val);
			break;
		}
		case DATA_TYPE_BYTE: {
			uchar_t val;
			(void) nvpair_value_byte(nvp, &val);
			RENDER(pctl, byte, nvl, name, val);
			break;
		}
		case DATA_TYPE_INT8: {
			int8_t val;
			(void) nvpair_value_int8(nvp, &val);
			RENDER(pctl, int8, nvl, name, val);
			break;
		}
		case DATA_TYPE_UINT8: {
			uint8_t val;
			(void) nvpair_value_uint8(nvp, &val);
			RENDER(pctl, uint8, nvl, name, val);
			break;
		}
		case DATA_TYPE_INT16: {
			int16_t val;
			(void) nvpair_value_int16(nvp, &val);
			RENDER(pctl, int16, nvl, name, val);
			break;
		}
		case DATA_TYPE_UINT16: {
			uint16_t val;
			(void) nvpair_value_uint16(nvp, &val);
			RENDER(pctl, uint16, nvl, name, val);
			break;
		}
		case DATA_TYPE_INT32: {
			int32_t val;
			(void) nvpair_value_int32(nvp, &val);
			RENDER(pctl, int32, nvl, name, val);
			break;
		}
		case DATA_TYPE_UINT32: {
			uint32_t val;
			(void) nvpair_value_uint32(nvp, &val);
			RENDER(pctl, uint32, nvl, name, val);
			break;
		}
		case DATA_TYPE_INT64: {
			int64_t val;
			(void) nvpair_value_int64(nvp, &val);
			RENDER(pctl, int64, nvl, name, val);
			break;
		}
		case DATA_TYPE_UINT64: {
			uint64_t val;
			(void) nvpair_value_uint64(nvp, &val);
			RENDER(pctl, uint64, nvl, name, val);
			break;
		}
		case DATA_TYPE_DOUBLE: {
			double val;
			(void) nvpair_value_double(nvp, &val);
			RENDER(pctl, double, nvl, name, val);
			break;
		}
		case DATA_TYPE_STRING: {
			char *val;
			(void) nvpair_value_string(nvp, &val);
			RENDER(pctl, string, nvl, name, val);
			break;
		}
		case DATA_TYPE_BOOLEAN_ARRAY: {
			boolean_t *val;
			(void) nvpair_value_boolean_array(nvp, &val, &nelem);
			ARENDER(pctl, boolean_array, nvl, name, val, nelem);
			break;
		}
		case DATA_TYPE_BYTE_ARRAY: {
			uchar_t *val;
			(void) nvpair_value_byte_array(nvp, &val, &nelem);
			ARENDER(pctl, byte_array, nvl, name, val, nelem);
			break;
		}
		case DATA_TYPE_INT8_ARRAY: {
			int8_t *val;
			(void) nvpair_value_int8_array(nvp, &val, &nelem);
			ARENDER(pctl, int8_array, nvl, name, val, nelem);
			break;
		}
		case DATA_TYPE_UINT8_ARRAY: {
			uint8_t *val;
			(void) nvpair_value_uint8_array(nvp, &val, &nelem);
			ARENDER(pctl, uint8_array, nvl, name, val, nelem);
			break;
		}
		case DATA_TYPE_INT16_ARRAY: {
			int16_t *val;
			(void) nvpair_value_int16_array(nvp, &val, &nelem);
			ARENDER(pctl, int16_array, nvl, name, val, nelem);
			break;
		}
		case DATA_TYPE_UINT16_ARRAY: {
			uint16_t *val;
			(void) nvpair_value_uint16_array(nvp, &val, &nelem);
			ARENDER(pctl, uint16_array, nvl, name, val, nelem);
			break;
		}
		case DATA_TYPE_INT32_ARRAY: {
			int32_t *val;
			(void) nvpair_value_int32_array(nvp, &val, &nelem);
			ARENDER(pctl, int32_array, nvl, name, val, nelem);
			break;
		}
		case DATA_TYPE_UINT32_ARRAY: {
			uint32_t *val;
			(void) nvpair_value_uint32_array(nvp, &val, &nelem);
			ARENDER(pctl, uint32_array, nvl, name, val, nelem);
			break;
		}
		case DATA_TYPE_INT64_ARRAY: {
			int64_t *val;
			(void) nvpair_value_int64_array(nvp, &val, &nelem);
			ARENDER(pctl, int64_array, nvl, name, val, nelem);
			break;
		}
		case DATA_TYPE_UINT64_ARRAY: {
			uint64_t *val;
			(void) nvpair_value_uint64_array(nvp, &val, &nelem);
			ARENDER(pctl, uint64_array, nvl, name, val, nelem);
			break;
		}
		case DATA_TYPE_STRING_ARRAY: {
			char **val;
			(void) nvpair_value_string_array(nvp, &val, &nelem);
			ARENDER(pctl, string_array, nvl, name, val, nelem);
			break;
		}
		case DATA_TYPE_HRTIME: {
			hrtime_t val;
			(void) nvpair_value_hrtime(nvp, &val);
			RENDER(pctl, hrtime, nvl, name, val);
			break;
		}
		case DATA_TYPE_NVLIST: {
			nvlist_t *val;
			(void) nvpair_value_nvlist(nvp, &val);
			RENDER(pctl, nvlist, nvl, name, val);
			break;
		}
		case DATA_TYPE_NVLIST_ARRAY: {
			nvlist_t **val;
			(void) nvpair_value_nvlist_array(nvp, &val, &nelem);
			ARENDER(pctl, nvlist_array, nvl, name, val, nelem);
			break;
		}
		default:
			(void) fprintf(fp, " unknown data type (%d)", type);
			break;
		}
		nvp = nvlist_next_nvpair(nvl, nvp);
	}
}

void
nvlist_print(FILE *fp, nvlist_t *nvl)
{
	struct nvlist_prtctl pc;

	prtctl_defaults(fp, &pc, NULL);
	nvlist_print_with_indent(nvl, &pc);
}

void
nvlist_prt(nvlist_t *nvl, nvlist_prtctl_t pctl)
{
	nvlist_print_with_indent(nvl, pctl);
}

#define	NVP(elem, type, vtype, ptype, format) { \
	vtype	value; \
\
	(void) nvpair_value_##type(elem, &value); \
	(void) printf("%*s%s: " format "\n", indent, "", \
	    nvpair_name(elem), (ptype)value); \
}

#define	NVPA(elem, type, vtype, ptype, format) { \
	uint_t	i, count; \
	vtype	*value;  \
\
	(void) nvpair_value_##type(elem, &value, &count); \
	for (i = 0; i < count; i++) { \
		(void) printf("%*s%s[%d]: " format "\n", indent, "", \
		    nvpair_name(elem), i, (ptype)value[i]); \
	} \
}

/*
 * Similar to nvlist_print() but handles arrays slightly differently.
 */
void
dump_nvlist(nvlist_t *list, int indent)
{
	nvpair_t	*elem = NULL;
	boolean_t	bool_value;
	boolean_t	*bool_array_value;
	nvlist_t	*nvlist_value;
	nvlist_t	**nvlist_array_value;
	uint_t		i, count;

	if (list == NULL) {
		return;
	}

	while ((elem = nvlist_next_nvpair(list, elem)) != NULL) {
		switch (nvpair_type(elem)) {
		case DATA_TYPE_BOOLEAN:
			(void) printf("%*s%s\n", indent, "", nvpair_name(elem));
			break;

		case DATA_TYPE_BOOLEAN_VALUE:
			(void) nvpair_value_boolean_value(elem, &bool_value);
			(void) printf("%*s%s: %s\n", indent, "",
			    nvpair_name(elem), bool_value ? "true" : "false");
			break;

		case DATA_TYPE_BYTE:
			NVP(elem, byte, uchar_t, int, "%u");
			break;

		case DATA_TYPE_INT8:
			NVP(elem, int8, int8_t, int, "%d");
			break;

		case DATA_TYPE_UINT8:
			NVP(elem, uint8, uint8_t, int, "%u");
			break;

		case DATA_TYPE_INT16:
			NVP(elem, int16, int16_t, int, "%d");
			break;

		case DATA_TYPE_UINT16:
			NVP(elem, uint16, uint16_t, int, "%u");
			break;

		case DATA_TYPE_INT32:
			NVP(elem, int32, int32_t, long, "%ld");
			break;

		case DATA_TYPE_UINT32:
			NVP(elem, uint32, uint32_t, ulong_t, "%lu");
			break;

		case DATA_TYPE_INT64:
			NVP(elem, int64, int64_t, longlong_t, "%lld");
			break;

		case DATA_TYPE_UINT64:
			NVP(elem, uint64, uint64_t, u_longlong_t, "%llu");
			break;

		case DATA_TYPE_STRING:
			NVP(elem, string, char *, char *, "'%s'");
			break;

		case DATA_TYPE_BOOLEAN_ARRAY:
			(void) nvpair_value_boolean_array(elem,
			    &bool_array_value, &count);
			for (i = 0; i < count; i++) {
				(void) printf("%*s%s[%d]: %s\n", indent, "",
				    nvpair_name(elem), i,
				    bool_array_value[i] ? "true" : "false");
			}
			break;

		case DATA_TYPE_BYTE_ARRAY:
			NVPA(elem, byte_array, uchar_t, int, "%u");
			break;

		case DATA_TYPE_INT8_ARRAY:
			NVPA(elem, int8_array, int8_t, int, "%d");
			break;

		case DATA_TYPE_UINT8_ARRAY:
			NVPA(elem, uint8_array, uint8_t, int, "%u");
			break;

		case DATA_TYPE_INT16_ARRAY:
			NVPA(elem, int16_array, int16_t, int, "%d");
			break;

		case DATA_TYPE_UINT16_ARRAY:
			NVPA(elem, uint16_array, uint16_t, int, "%u");
			break;

		case DATA_TYPE_INT32_ARRAY:
			NVPA(elem, int32_array, int32_t, long, "%ld");
			break;

		case DATA_TYPE_UINT32_ARRAY:
			NVPA(elem, uint32_array, uint32_t, ulong_t, "%lu");
			break;

		case DATA_TYPE_INT64_ARRAY:
			NVPA(elem, int64_array, int64_t, longlong_t, "%lld");
			break;

		case DATA_TYPE_UINT64_ARRAY:
			NVPA(elem, uint64_array, uint64_t, u_longlong_t,
			    "%llu");
			break;

		case DATA_TYPE_STRING_ARRAY:
			NVPA(elem, string_array, char *, char *, "'%s'");
			break;

		case DATA_TYPE_NVLIST:
			(void) nvpair_value_nvlist(elem, &nvlist_value);
			(void) printf("%*s%s:\n", indent, "",
			    nvpair_name(elem));
			dump_nvlist(nvlist_value, indent + 4);
			break;

		case DATA_TYPE_NVLIST_ARRAY:
			(void) nvpair_value_nvlist_array(elem,
			    &nvlist_array_value, &count);
			for (i = 0; i < count; i++) {
				(void) printf("%*s%s[%u]:\n", indent, "",
				    nvpair_name(elem), i);
				dump_nvlist(nvlist_array_value[i], indent + 4);
			}
			break;

		default:
			(void) printf(dgettext(TEXT_DOMAIN, "bad config type "
			    "%d for %s\n"), nvpair_type(elem),
			    nvpair_name(elem));
		}
	}
}

/*
 * ======================================================================
 * |									|
 * | Misc private interface.						|
 * |									|
 * ======================================================================
 */

/*
 * Determine if string 'value' matches 'nvp' value.  The 'value' string is
 * converted, depending on the type of 'nvp', prior to match.  For numeric
 * types, a radix independent sscanf conversion of 'value' is used. If 'nvp'
 * is an array type, 'ai' is the index into the array against which we are
 * checking for match. If nvp is of DATA_TYPE_STRING*, the caller can pass
 * in a regex_t compilation of value in 'value_regex' to trigger regular
 * expression string match instead of simple strcmp().
 *
 * Return 1 on match, 0 on no-match, and -1 on error.  If the error is
 * related to value syntax error and 'ep' is non-NULL, *ep will point into
 * the 'value' string at the location where the error exists.
 *
 * NOTE: It may be possible to move the non-regex_t version of this into
 * common code used by library/kernel/boot.
 */
int
nvpair_value_match_regex(nvpair_t *nvp, int ai,
    char *value, regex_t *value_regex, char **ep)
{
	char	*evalue;
	uint_t	a_len;
	int	sr;

	if (ep)
		*ep = NULL;

	if ((nvp == NULL) || (value == NULL))
		return (-1);		/* error fail match - invalid args */

	/* make sure array and index combination make sense */
	if ((nvpair_type_is_array(nvp) && (ai < 0)) ||
	    (!nvpair_type_is_array(nvp) && (ai >= 0)))
		return (-1);		/* error fail match - bad index */

	/* non-string values should be single 'chunk' */
	if ((nvpair_type(nvp) != DATA_TYPE_STRING) &&
	    (nvpair_type(nvp) != DATA_TYPE_STRING_ARRAY)) {
		value += strspn(value, " \t");
		evalue = value + strcspn(value, " \t");
		if (*evalue) {
			if (ep)
				*ep = evalue;
			return (-1);	/* error fail match - syntax */
		}
	}

	sr = EOF;
	switch (nvpair_type(nvp)) {
	case DATA_TYPE_STRING: {
		char	*val;

		/* check string value for match */
		if (nvpair_value_string(nvp, &val) == 0) {
			if (value_regex) {
				if (regexec(value_regex, val,
				    (size_t)0, NULL, 0) == 0)
					return (1);	/* match */
			} else {
				if (strcmp(value, val) == 0)
					return (1);	/* match */
			}
		}
		break;
	}
	case DATA_TYPE_STRING_ARRAY: {
		char **val_array;

		/* check indexed string value of array for match */
		if ((nvpair_value_string_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len)) {
			if (value_regex) {
				if (regexec(value_regex, val_array[ai],
				    (size_t)0, NULL, 0) == 0)
					return (1);
			} else {
				if (strcmp(value, val_array[ai]) == 0)
					return (1);
			}
		}
		break;
	}
	case DATA_TYPE_BYTE: {
		uchar_t val, val_arg;

		/* scanf uchar_t from value and check for match */
		sr = sscanf(value, "%c", &val_arg);
		if ((sr == 1) && (nvpair_value_byte(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_BYTE_ARRAY: {
		uchar_t *val_array, val_arg;


		/* check indexed value of array for match */
		sr = sscanf(value, "%c", &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_byte_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT8: {
		int8_t val, val_arg;

		/* scanf int8_t from value and check for match */
		sr = sscanf(value, "%"SCNi8, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int8(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT8_ARRAY: {
		int8_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi8, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int8_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_UINT8: {
		uint8_t val, val_arg;

		/* scanf uint8_t from value and check for match */
		sr = sscanf(value, "%"SCNi8, (int8_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint8(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_UINT8_ARRAY: {
		uint8_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi8, (int8_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint8_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT16: {
		int16_t val, val_arg;

		/* scanf int16_t from value and check for match */
		sr = sscanf(value, "%"SCNi16, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int16(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT16_ARRAY: {
		int16_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi16, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int16_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_UINT16: {
		uint16_t val, val_arg;

		/* scanf uint16_t from value and check for match */
		sr = sscanf(value, "%"SCNi16, (int16_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint16(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_UINT16_ARRAY: {
		uint16_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi16, (int16_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint16_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT32: {
		int32_t val, val_arg;

		/* scanf int32_t from value and check for match */
		sr = sscanf(value, "%"SCNi32, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int32(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT32_ARRAY: {
		int32_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi32, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int32_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_UINT32: {
		uint32_t val, val_arg;

		/* scanf uint32_t from value and check for match */
		sr = sscanf(value, "%"SCNi32, (int32_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint32(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_UINT32_ARRAY: {
		uint32_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi32, (int32_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint32_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT64: {
		int64_t val, val_arg;

		/* scanf int64_t from value and check for match */
		sr = sscanf(value, "%"SCNi64, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int64(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_INT64_ARRAY: {
		int64_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi64, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_int64_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
				return (1);
		break;
	}
	case DATA_TYPE_UINT64: {
		uint64_t val_arg, val;

		/* scanf uint64_t from value and check for match */
		sr = sscanf(value, "%"SCNi64, (int64_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint64(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_UINT64_ARRAY: {
		uint64_t *val_array, val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi64, (int64_t *)&val_arg);
		if ((sr == 1) &&
		    (nvpair_value_uint64_array(nvp, &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_BOOLEAN_VALUE: {
		int32_t val_arg;
		boolean_t val;

		/* scanf boolean_t from value and check for match */
		sr = sscanf(value, "%"SCNi32, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_boolean_value(nvp, &val) == 0) &&
		    (val == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_BOOLEAN_ARRAY: {
		boolean_t *val_array;
		int32_t val_arg;

		/* check indexed value of array for match */
		sr = sscanf(value, "%"SCNi32, &val_arg);
		if ((sr == 1) &&
		    (nvpair_value_boolean_array(nvp,
		    &val_array, &a_len) == 0) &&
		    (ai < a_len) &&
		    (val_array[ai] == val_arg))
			return (1);
		break;
	}
	case DATA_TYPE_HRTIME:
	case DATA_TYPE_NVLIST:
	case DATA_TYPE_NVLIST_ARRAY:
	case DATA_TYPE_BOOLEAN:
	case DATA_TYPE_DOUBLE:
	case DATA_TYPE_UNKNOWN:
	default:
		/*
		 * unknown/unsupported data type
		 */
		return (-1);		/* error fail match */
	}

	/*
	 * check to see if sscanf failed conversion, return approximate
	 * pointer to problem
	 */
	if (sr != 1) {
		if (ep)
			*ep = value;
		return (-1);		/* error fail match  - syntax */
	}

	return (0);			/* fail match */
}

int
nvpair_value_match(nvpair_t *nvp, int ai, char *value, char **ep)
{
	return (nvpair_value_match_regex(nvp, ai, value, NULL, ep));
}
