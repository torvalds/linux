/*
 * Copyright (c) 1997 Kungliga Tekniska Högskolan
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
#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
_krb5_s4u2self_to_checksumdata(krb5_context context,
			       const PA_S4U2Self *self,
			       krb5_data *data)
{
    krb5_error_code ret;
    krb5_ssize_t ssize;
    krb5_storage *sp;
    size_t size;
    size_t i;

    sp = krb5_storage_emem();
    if (sp == NULL) {
	krb5_clear_error_message(context);
	return ENOMEM;
    }
    krb5_storage_set_flags(sp, KRB5_STORAGE_BYTEORDER_LE);
    ret = krb5_store_int32(sp, self->name.name_type);
    if (ret)
	goto out;
    for (i = 0; i < self->name.name_string.len; i++) {
	size = strlen(self->name.name_string.val[i]);
	ssize = krb5_storage_write(sp, self->name.name_string.val[i], size);
	if (ssize != (krb5_ssize_t)size) {
	    ret = ENOMEM;
	    goto out;
	}
    }
    size = strlen(self->realm);
    ssize = krb5_storage_write(sp, self->realm, size);
    if (ssize != (krb5_ssize_t)size) {
	ret = ENOMEM;
	goto out;
    }
    size = strlen(self->auth);
    ssize = krb5_storage_write(sp, self->auth, size);
    if (ssize != (krb5_ssize_t)size) {
	ret = ENOMEM;
	goto out;
    }

    ret = krb5_storage_to_data(sp, data);
    krb5_storage_free(sp);
    return ret;

out:
    krb5_clear_error_message(context);
    return ret;
}

krb5_error_code
krb5_enomem(krb5_context context)
{
    krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
    return ENOMEM;
}

void
_krb5_debug_backtrace(krb5_context context)
{
#if defined(HAVE_BACKTRACE) && !defined(HEIMDAL_SMALLER)
    void *stack[128];
    char **strs = NULL;
    int i, frames = backtrace(stack, sizeof(stack) / sizeof(stack[0]));
    if (frames > 0)
	strs = backtrace_symbols(stack, frames);
    if (strs) {
	for (i = 0; i < frames; i++)
	    _krb5_debug(context, 10, "frame %d: %s", i, strs[i]);
	free(strs);
    }
#endif
}

krb5_error_code
_krb5_einval(krb5_context context, const char *func, unsigned long argn)
{
#ifndef HEIMDAL_SMALLER
    krb5_set_error_message(context, EINVAL,
			   N_("programmer error: invalid argument to %s argument %lu",
			      "function:line"),
			   func, argn);
    if (_krb5_have_debug(context, 10)) {
	_krb5_debug(context, 10, "invalid argument to function %s argument %lu",
		    func, argn);
	_krb5_debug_backtrace(context);
    }
#endif
    return EINVAL;
}
