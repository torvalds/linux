/*
 * Copyright (c) 1997-2005 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

#include "kcm_locl.h"

void
kcm_service(void *ctx, const heim_idata *req,
	    const heim_icred cred,
	    heim_ipc_complete complete,
	    heim_sipc_call cctx)
{
    kcm_client peercred;
    krb5_error_code ret;
    krb5_data request, rep;
    unsigned char *buf;
    size_t len;

    krb5_data_zero(&rep);

    peercred.uid = heim_ipc_cred_get_uid(cred);
    peercred.gid = heim_ipc_cred_get_gid(cred);
    peercred.pid = heim_ipc_cred_get_pid(cred);
    peercred.session = heim_ipc_cred_get_session(cred);

    if (req->length < 4) {
	kcm_log(1, "malformed request from process %d (too short)",
		peercred.pid);
	(*complete)(cctx, EINVAL, NULL);
	return;
    }

    buf = req->data;
    len = req->length;

    if (buf[0] != KCM_PROTOCOL_VERSION_MAJOR ||
	buf[1] != KCM_PROTOCOL_VERSION_MINOR) {
	kcm_log(1, "incorrect protocol version %d.%d from process %d",
		buf[0], buf[1], peercred.pid);
	(*complete)(cctx, EINVAL, NULL);
	return;
    }

    request.data = buf + 2;
    request.length = len - 2;

    /* buf is now pointing at opcode */

    ret = kcm_dispatch(kcm_context, &peercred, &request, &rep);

    (*complete)(cctx, ret, &rep);
    krb5_data_free(&rep);
}
