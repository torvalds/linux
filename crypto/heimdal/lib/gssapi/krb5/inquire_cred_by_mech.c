/*
 * Copyright (c) 2003, 2006, 2007 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "gsskrb5_locl.h"

OM_uint32 GSSAPI_CALLCONV _gsskrb5_inquire_cred_by_mech (
    OM_uint32 * minor_status,
	const gss_cred_id_t cred_handle,
	const gss_OID mech_type,
	gss_name_t * name,
	OM_uint32 * initiator_lifetime,
	OM_uint32 * acceptor_lifetime,
	gss_cred_usage_t * cred_usage
    )
{
    gss_cred_usage_t usage;
    OM_uint32 maj_stat;
    OM_uint32 lifetime;

    maj_stat =
	_gsskrb5_inquire_cred (minor_status, cred_handle,
			       name, &lifetime, &usage, NULL);
    if (maj_stat)
	return maj_stat;

    if (initiator_lifetime) {
	if (usage == GSS_C_INITIATE || usage == GSS_C_BOTH)
	    *initiator_lifetime = lifetime;
	else
	    *initiator_lifetime = 0;
    }

    if (acceptor_lifetime) {
	if (usage == GSS_C_ACCEPT || usage == GSS_C_BOTH)
	    *acceptor_lifetime = lifetime;
	else
	    *acceptor_lifetime = 0;
    }

    if (cred_usage)
	*cred_usage = usage;

    return GSS_S_COMPLETE;
}
