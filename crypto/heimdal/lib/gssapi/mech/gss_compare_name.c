/*-
 * Copyright (c) 2005 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD: src/lib/libgssapi/gss_compare_name.c,v 1.1 2005/12/29 14:40:20 dfr Exp $
 */

#include "mech_locl.h"

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_compare_name(OM_uint32 *minor_status,
    const gss_name_t name1_arg,
    const gss_name_t name2_arg,
    int *name_equal)
{
	struct _gss_name *name1 = (struct _gss_name *) name1_arg;
	struct _gss_name *name2 = (struct _gss_name *) name2_arg;

	/*
	 * First check the implementation-independant name if both
	 * names have one. Otherwise, try to find common mechanism
	 * names and compare them.
	 */
	if (name1->gn_value.value && name2->gn_value.value) {
		*name_equal = 1;
		if (!gss_oid_equal(&name1->gn_type, &name2->gn_type)) {
			*name_equal = 0;
		} else if (name1->gn_value.length != name2->gn_value.length ||
		    memcmp(name1->gn_value.value, name1->gn_value.value,
			name1->gn_value.length)) {
			*name_equal = 0;
		}
	} else {
		struct _gss_mechanism_name *mn1;
		struct _gss_mechanism_name *mn2;

		HEIM_SLIST_FOREACH(mn1, &name1->gn_mn, gmn_link) {
			OM_uint32 major_status;

			major_status = _gss_find_mn(minor_status, name2,
						    mn1->gmn_mech_oid, &mn2);
			if (major_status == GSS_S_COMPLETE) {
				return (mn1->gmn_mech->gm_compare_name(
						minor_status,
						mn1->gmn_name,
						mn2->gmn_name,
						name_equal));
			}
		}
		*name_equal = 0;
	}

	*minor_status = 0;
	return (GSS_S_COMPLETE);
}
