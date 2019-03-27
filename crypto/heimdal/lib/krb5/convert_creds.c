/*
 * Copyright (c) 1997 - 2004 Kungliga Tekniska Högskolan
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

#include "krb5_locl.h"
#include "krb5-v4compat.h"

#ifndef HEIMDAL_SMALLER

/**
 * Convert the v5 credentials in in_cred to v4-dito in v4creds.  This
 * is done by sending them to the 524 function in the KDC.  If
 * `in_cred' doesn't contain a DES session key, then a new one is
 * gotten from the KDC and stored in the cred cache `ccache'.
 *
 * @param context Kerberos 5 context.
 * @param in_cred the credential to convert
 * @param v4creds the converted credential
 *
 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5_v4compat
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb524_convert_creds_kdc(krb5_context context,
			 krb5_creds *in_cred,
			 struct credentials *v4creds)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    memset(v4creds, 0, sizeof(*v4creds));
    krb5_set_error_message(context, EINVAL,
			   N_("krb524_convert_creds_kdc not supported", ""));
    return EINVAL;
}

/**
 * Convert the v5 credentials in in_cred to v4-dito in v4creds,
 * check the credential cache ccache before checking with the KDC.
 *
 * @param context Kerberos 5 context.
 * @param ccache credential cache used to check for des-ticket.
 * @param in_cred the credential to convert
 * @param v4creds the converted credential
 *
 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5_v4compat
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb524_convert_creds_kdc_ccache(krb5_context context,
				krb5_ccache ccache,
				krb5_creds *in_cred,
				struct credentials *v4creds)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    memset(v4creds, 0, sizeof(*v4creds));
    krb5_set_error_message(context, EINVAL,
			   N_("krb524_convert_creds_kdc_ccache not supported", ""));
    return EINVAL;
}

#endif
