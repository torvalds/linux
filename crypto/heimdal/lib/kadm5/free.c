/*
 * Copyright (c) 1997 - 1999 Kungliga Tekniska Högskolan
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

#include "kadm5_locl.h"

RCSID("$Id$");

void
kadm5_free_key_data(void *server_handle,
		    int16_t *n_key_data,
		    krb5_key_data *key_data)
{
    int i;
    for(i = 0; i < *n_key_data; i++){
	if(key_data[i].key_data_contents[0]){
	    memset(key_data[i].key_data_contents[0],
		   0,
		   key_data[i].key_data_length[0]);
	    free(key_data[i].key_data_contents[0]);
	}
	if(key_data[i].key_data_contents[1])
	    free(key_data[i].key_data_contents[1]);
    }
    *n_key_data = 0;
}


void
kadm5_free_principal_ent(void *server_handle,
			 kadm5_principal_ent_t princ)
{
    kadm5_server_context *context = server_handle;
    if(princ->principal)
	krb5_free_principal(context->context, princ->principal);
    if(princ->mod_name)
	krb5_free_principal(context->context, princ->mod_name);
    kadm5_free_key_data(server_handle, &princ->n_key_data, princ->key_data);
    while(princ->n_tl_data && princ->tl_data) {
	krb5_tl_data *tp;
	tp = princ->tl_data;
	princ->tl_data = tp->tl_data_next;
	princ->n_tl_data--;
	memset(tp->tl_data_contents, 0, tp->tl_data_length);
	free(tp->tl_data_contents);
	free(tp);
    }
    if (princ->key_data != NULL)
	free (princ->key_data);
}

void
kadm5_free_name_list(void *server_handle,
		     char **names,
		     int *count)
{
    int i;
    for(i = 0; i < *count; i++)
	free(names[i]);
    free(names);
    *count = 0;
}
