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
 *	$FreeBSD: src/lib/libgssapi/gss_duplicate_name.c,v 1.1 2005/12/29 14:40:20 dfr Exp $
 */

#include "mech_locl.h"

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_duplicate_name(OM_uint32 *minor_status,
    const gss_name_t src_name,
    gss_name_t *dest_name)
{
	OM_uint32		major_status;
	struct _gss_name	*name = (struct _gss_name *) src_name;
	struct _gss_name	*new_name;
	struct _gss_mechanism_name *mn;

	*minor_status = 0;
	*dest_name = GSS_C_NO_NAME;

	/*
	 * If this name has a value (i.e. it didn't come from
	 * gss_canonicalize_name(), we re-import the thing. Otherwise,
	 * we make copy of each mech names.
	 */
	if (name->gn_value.value) {
		major_status = gss_import_name(minor_status,
		    &name->gn_value, &name->gn_type, dest_name);
		if (major_status != GSS_S_COMPLETE)
			return (major_status);
		new_name = (struct _gss_name *) *dest_name;

		HEIM_SLIST_FOREACH(mn, &name->gn_mn, gmn_link) {
		    struct _gss_mechanism_name *mn2;
		    _gss_find_mn(minor_status, new_name,
				 mn->gmn_mech_oid, &mn2);
		}
	} else {
		new_name = malloc(sizeof(struct _gss_name));
		if (!new_name) {
			*minor_status = ENOMEM;
			return (GSS_S_FAILURE);
		}
		memset(new_name, 0, sizeof(struct _gss_name));
		HEIM_SLIST_INIT(&new_name->gn_mn);
		*dest_name = (gss_name_t) new_name;

		HEIM_SLIST_FOREACH(mn, &name->gn_mn, gmn_link) {
			struct _gss_mechanism_name *new_mn;

			new_mn = malloc(sizeof(*new_mn));
			if (!new_mn) {
				*minor_status = ENOMEM;
				return GSS_S_FAILURE;
			}
			new_mn->gmn_mech = mn->gmn_mech;
			new_mn->gmn_mech_oid = mn->gmn_mech_oid;

			major_status =
			    mn->gmn_mech->gm_duplicate_name(minor_status,
				mn->gmn_name, &new_mn->gmn_name);
			if (major_status != GSS_S_COMPLETE) {
				free(new_mn);
				continue;
			}
			HEIM_SLIST_INSERT_HEAD(&new_name->gn_mn, new_mn, gmn_link);
		}

	}

	return (GSS_S_COMPLETE);
}
