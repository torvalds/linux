/*
 * Copyright (c) 1997-2004 Kungliga Tekniska Högskolan
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

#include "ktutil_locl.h"

RCSID("$Id$");

int
kt_remove(struct remove_options *opt, int argc, char **argv)
{
    krb5_error_code ret = 0;
    krb5_keytab_entry entry;
    krb5_keytab keytab;
    krb5_principal principal = NULL;
    krb5_enctype enctype = 0;

    if(opt->principal_string) {
	ret = krb5_parse_name(context, opt->principal_string, &principal);
	if(ret) {
	    krb5_warn(context, ret, "%s", opt->principal_string);
	    return 1;
	}
    }
    if(opt->enctype_string) {
	ret = krb5_string_to_enctype(context, opt->enctype_string, &enctype);
	if(ret) {
	    int t;
	    if(sscanf(opt->enctype_string, "%d", &t) == 1)
		enctype = t;
	    else {
		krb5_warn(context, ret, "%s", opt->enctype_string);
		if(principal)
		    krb5_free_principal(context, principal);
		return 1;
	    }
	}
    }
    if (!principal && !enctype && !opt->kvno_integer) {
	krb5_warnx(context,
		   "You must give at least one of "
		   "principal, enctype or kvno.");
	ret = EINVAL;
	goto out;
    }

    if((keytab = ktutil_open_keytab()) == NULL) {
	ret = 1;
	goto out;
    }

    entry.principal = principal;
    entry.keyblock.keytype = enctype;
    entry.vno = opt->kvno_integer;
    ret = krb5_kt_remove_entry(context, keytab, &entry);
    krb5_kt_close(context, keytab);
    if(ret)
	krb5_warn(context, ret, "remove");
 out:
    if(principal)
	krb5_free_principal(context, principal);
    return ret != 0;
}

