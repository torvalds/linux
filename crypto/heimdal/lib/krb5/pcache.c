/***********************************************************************
 * Copyright (c) 2010, Secure Endpoints Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 **********************************************************************/

#include "krb5_locl.h"
#include "ccache_plugin.h"
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif
#include <assert.h>

krb5_error_code
_krb5_load_ccache_plugins(krb5_context context)
{
    struct krb5_plugin * plist = NULL;
    struct krb5_plugin *p;
    krb5_error_code code;

    code = _krb5_plugin_find(context, PLUGIN_TYPE_DATA, KRB5_PLUGIN_CCACHE,
                             &plist);
    if (code)
        return code;

    for (p = plist; p != NULL; p = _krb5_plugin_get_next(p)) {
        krb5_cc_ops * ccops;
        krb5_error_code c_load;

        ccops = _krb5_plugin_get_symbol(p);
        if (ccops != NULL && ccops->version == KRB5_CC_OPS_VERSION) {
            c_load = krb5_cc_register(context, ccops, TRUE);
            if (c_load != 0)
                code = c_load;
        }
    }

    _krb5_plugin_free(plist);

    return code;
}
