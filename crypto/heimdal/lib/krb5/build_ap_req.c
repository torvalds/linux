/*
 * Copyright (c) 1997 - 2002 Kungliga Tekniska Högskolan
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

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_build_ap_req (krb5_context context,
		   krb5_enctype enctype,
		   krb5_creds *cred,
		   krb5_flags ap_options,
		   krb5_data authenticator,
		   krb5_data *retdata)
{
  krb5_error_code ret = 0;
  AP_REQ ap;
  Ticket t;
  size_t len;

  ap.pvno = 5;
  ap.msg_type = krb_ap_req;
  memset(&ap.ap_options, 0, sizeof(ap.ap_options));
  ap.ap_options.use_session_key = (ap_options & AP_OPTS_USE_SESSION_KEY) > 0;
  ap.ap_options.mutual_required = (ap_options & AP_OPTS_MUTUAL_REQUIRED) > 0;

  ap.ticket.tkt_vno = 5;
  copy_Realm(&cred->server->realm, &ap.ticket.realm);
  copy_PrincipalName(&cred->server->name, &ap.ticket.sname);

  decode_Ticket(cred->ticket.data, cred->ticket.length, &t, &len);
  copy_EncryptedData(&t.enc_part, &ap.ticket.enc_part);
  free_Ticket(&t);

  ap.authenticator.etype = enctype;
  ap.authenticator.kvno  = NULL;
  ap.authenticator.cipher = authenticator;

  ASN1_MALLOC_ENCODE(AP_REQ, retdata->data, retdata->length,
		     &ap, &len, ret);
  if(ret == 0 && retdata->length != len)
      krb5_abortx(context, "internal error in ASN.1 encoder");
  free_AP_REQ(&ap);
  return ret;

}
