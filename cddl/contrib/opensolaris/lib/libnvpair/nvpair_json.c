/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */
/*
 * Copyright (c) 2014, Joyent, Inc.
 * Copyright (c) 2017 by Delphix. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <wchar.h>
#include <sys/debug.h>

#include "libnvpair.h"

#define	FPRINTF(fp, ...)				\
	do {						\
		if (fprintf(fp, __VA_ARGS__) < 0)	\
			return (-1);			\
	} while (0)

/*
 * When formatting a string for JSON output we must escape certain characters,
 * as described in RFC4627.  This applies to both member names and
 * DATA_TYPE_STRING values.
 *
 * This function will only operate correctly if the following conditions are
 * met:
 *
 *       1. The input String is encoded in the current locale.
 *
 *       2. The current locale includes the Basic Multilingual Plane (plane 0)
 *          as defined in the Unicode standard.
 *
 * The output will be entirely 7-bit ASCII (as a subset of UTF-8) with all
 * representable Unicode characters included in their escaped numeric form.
 */
static int
nvlist_print_json_string(FILE *fp, const char *input)
{
	mbstate_t mbr;
	wchar_t c;
	size_t sz;

	bzero(&mbr, sizeof (mbr));

	FPRINTF(fp, "\"");
	while ((sz = mbrtowc(&c, input, MB_CUR_MAX, &mbr)) > 0) {
		switch (c) {
		case '"':
			FPRINTF(fp, "\\\"");
			break;
		case '\n':
			FPRINTF(fp, "\\n");
			break;
		case '\r':
			FPRINTF(fp, "\\r");
			break;
		case '\\':
			FPRINTF(fp, "\\\\");
			break;
		case '\f':
			FPRINTF(fp, "\\f");
			break;
		case '\t':
			FPRINTF(fp, "\\t");
			break;
		case '\b':
			FPRINTF(fp, "\\b");
			break;
		default:
			if ((c >= 0x00 && c <= 0x1f) ||
			    (c > 0x7f && c <= 0xffff)) {
				/*
				 * Render both Control Characters and Unicode
				 * characters in the Basic Multilingual Plane
				 * as JSON-escaped multibyte characters.
				 */
				FPRINTF(fp, "\\u%04x", (int)(0xffff & c));
			} else if (c >= 0x20 && c <= 0x7f) {
				/*
				 * Render other 7-bit ASCII characters directly
				 * and drop other, unrepresentable characters.
				 */
				FPRINTF(fp, "%c", (int)(0xff & c));
			}
			break;
		}
		input += sz;
	}

	if (sz == (size_t)-1 || sz == (size_t)-2) {
		/*
		 * We last read an invalid multibyte character sequence,
		 * so return an error.
		 */
		return (-1);
	}

	FPRINTF(fp, "\"");
	return (0);
}

/*
 * Dump a JSON-formatted representation of an nvlist to the provided FILE *.
 * This routine does not output any new-lines or additional whitespace other
 * than that contained in strings, nor does it call fflush(3C).
 */
int
nvlist_print_json(FILE *fp, nvlist_t *nvl)
{
	nvpair_t *curr;
	boolean_t first = B_TRUE;

	FPRINTF(fp, "{");

	for (curr = nvlist_next_nvpair(nvl, NULL); curr;
	    curr = nvlist_next_nvpair(nvl, curr)) {
		data_type_t type = nvpair_type(curr);

		if (!first)
			FPRINTF(fp, ",");
		else
			first = B_FALSE;

		if (nvlist_print_json_string(fp, nvpair_name(curr)) == -1)
			return (-1);
		FPRINTF(fp, ":");

		switch (type) {
		case DATA_TYPE_STRING: {
			char *string = fnvpair_value_string(curr);
			if (nvlist_print_json_string(fp, string) == -1)
				return (-1);
			break;
		}

		case DATA_TYPE_BOOLEAN: {
			FPRINTF(fp, "true");
			break;
		}

		case DATA_TYPE_BOOLEAN_VALUE: {
			FPRINTF(fp, "%s", fnvpair_value_boolean_value(curr) ==
			    B_TRUE ? "true" : "false");
			break;
		}

		case DATA_TYPE_BYTE: {
			FPRINTF(fp, "%hhu", fnvpair_value_byte(curr));
			break;
		}

		case DATA_TYPE_INT8: {
			FPRINTF(fp, "%hhd", fnvpair_value_int8(curr));
			break;
		}

		case DATA_TYPE_UINT8: {
			FPRINTF(fp, "%hhu", fnvpair_value_uint8_t(curr));
			break;
		}

		case DATA_TYPE_INT16: {
			FPRINTF(fp, "%hd", fnvpair_value_int16(curr));
			break;
		}

		case DATA_TYPE_UINT16: {
			FPRINTF(fp, "%hu", fnvpair_value_uint16(curr));
			break;
		}

		case DATA_TYPE_INT32: {
			FPRINTF(fp, "%d", fnvpair_value_int32(curr));
			break;
		}

		case DATA_TYPE_UINT32: {
			FPRINTF(fp, "%u", fnvpair_value_uint32(curr));
			break;
		}

		case DATA_TYPE_INT64: {
			FPRINTF(fp, "%lld",
			    (long long)fnvpair_value_int64(curr));
			break;
		}

		case DATA_TYPE_UINT64: {
			FPRINTF(fp, "%llu",
			    (unsigned long long)fnvpair_value_uint64(curr));
			break;
		}

		case DATA_TYPE_HRTIME: {
			hrtime_t val;
			VERIFY0(nvpair_value_hrtime(curr, &val));
			FPRINTF(fp, "%llu", (unsigned long long)val);
			break;
		}

		case DATA_TYPE_DOUBLE: {
			double val;
			VERIFY0(nvpair_value_double(curr, &val));
			FPRINTF(fp, "%f", val);
			break;
		}

		case DATA_TYPE_NVLIST: {
			if (nvlist_print_json(fp,
			    fnvpair_value_nvlist(curr)) == -1)
				return (-1);
			break;
		}

		case DATA_TYPE_STRING_ARRAY: {
			char **val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_string_array(curr, &val, &valsz));
			FPRINTF(fp, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					FPRINTF(fp, ",");
				if (nvlist_print_json_string(fp, val[i]) == -1)
					return (-1);
			}
			FPRINTF(fp, "]");
			break;
		}

		case DATA_TYPE_NVLIST_ARRAY: {
			nvlist_t **val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_nvlist_array(curr, &val, &valsz));
			FPRINTF(fp, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					FPRINTF(fp, ",");
				if (nvlist_print_json(fp, val[i]) == -1)
					return (-1);
			}
			FPRINTF(fp, "]");
			break;
		}

		case DATA_TYPE_BOOLEAN_ARRAY: {
			boolean_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_boolean_array(curr, &val, &valsz));
			FPRINTF(fp, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					FPRINTF(fp, ",");
				FPRINTF(fp, val[i] == B_TRUE ?
				    "true" : "false");
			}
			FPRINTF(fp, "]");
			break;
		}

		case DATA_TYPE_BYTE_ARRAY: {
			uchar_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_byte_array(curr, &val, &valsz));
			FPRINTF(fp, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					FPRINTF(fp, ",");
				FPRINTF(fp, "%hhu", val[i]);
			}
			FPRINTF(fp, "]");
			break;
		}

		case DATA_TYPE_UINT8_ARRAY: {
			uint8_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_uint8_array(curr, &val, &valsz));
			FPRINTF(fp, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					FPRINTF(fp, ",");
				FPRINTF(fp, "%hhu", val[i]);
			}
			FPRINTF(fp, "]");
			break;
		}

		case DATA_TYPE_INT8_ARRAY: {
			int8_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_int8_array(curr, &val, &valsz));
			FPRINTF(fp, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					FPRINTF(fp, ",");
				FPRINTF(fp, "%hhd", val[i]);
			}
			FPRINTF(fp, "]");
			break;
		}

		case DATA_TYPE_UINT16_ARRAY: {
			uint16_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_uint16_array(curr, &val, &valsz));
			FPRINTF(fp, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					FPRINTF(fp, ",");
				FPRINTF(fp, "%hu", val[i]);
			}
			FPRINTF(fp, "]");
			break;
		}

		case DATA_TYPE_INT16_ARRAY: {
			int16_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_int16_array(curr, &val, &valsz));
			FPRINTF(fp, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					FPRINTF(fp, ",");
				FPRINTF(fp, "%hd", val[i]);
			}
			FPRINTF(fp, "]");
			break;
		}

		case DATA_TYPE_UINT32_ARRAY: {
			uint32_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_uint32_array(curr, &val, &valsz));
			FPRINTF(fp, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					FPRINTF(fp, ",");
				FPRINTF(fp, "%u", val[i]);
			}
			FPRINTF(fp, "]");
			break;
		}

		case DATA_TYPE_INT32_ARRAY: {
			int32_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_int32_array(curr, &val, &valsz));
			FPRINTF(fp, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					FPRINTF(fp, ",");
				FPRINTF(fp, "%d", val[i]);
			}
			FPRINTF(fp, "]");
			break;
		}

		case DATA_TYPE_UINT64_ARRAY: {
			uint64_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_uint64_array(curr, &val, &valsz));
			FPRINTF(fp, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					FPRINTF(fp, ",");
				FPRINTF(fp, "%llu",
				    (unsigned long long)val[i]);
			}
			FPRINTF(fp, "]");
			break;
		}

		case DATA_TYPE_INT64_ARRAY: {
			int64_t *val;
			uint_t valsz, i;
			VERIFY0(nvpair_value_int64_array(curr, &val, &valsz));
			FPRINTF(fp, "[");
			for (i = 0; i < valsz; i++) {
				if (i > 0)
					FPRINTF(fp, ",");
				FPRINTF(fp, "%lld", (long long)val[i]);
			}
			FPRINTF(fp, "]");
			break;
		}

		case DATA_TYPE_UNKNOWN:
		case DATA_TYPE_DONTCARE:
			return (-1);
		}

	}

	FPRINTF(fp, "}");
	return (0);
}
