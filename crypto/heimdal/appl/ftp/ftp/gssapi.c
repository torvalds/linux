/*
 * Copyright (c) 1998 - 2005 Kungliga Tekniska Högskolan
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

#ifdef FTP_SERVER
#include "ftpd_locl.h"
#else
#include "ftp_locl.h"
#endif
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>
#include <krb5_err.h>

RCSID("$Id$");

int ftp_do_gss_bindings = 0;
int ftp_do_gss_delegate = 1;

struct gssapi_data {
    gss_ctx_id_t context_hdl;
    gss_name_t client_name;
    gss_cred_id_t delegated_cred_handle;
    void *mech_data;
};

static int
gss_init(void *app_data)
{
    struct gssapi_data *d = app_data;
    d->context_hdl = GSS_C_NO_CONTEXT;
    d->delegated_cred_handle = GSS_C_NO_CREDENTIAL;
#if defined(FTP_SERVER)
    return 0;
#else
    /* XXX Check the gss mechanism; with  gss_indicate_mechs() ? */
#ifdef KRB5
    return !use_kerberos;
#else
    return 0;
#endif /* KRB5 */
#endif /* FTP_SERVER */
}

static int
gss_check_prot(void *app_data, int level)
{
    if(level == prot_confidential)
	return -1;
    return 0;
}

static int
gss_decode(void *app_data, void *buf, int len, int level)
{
    OM_uint32 maj_stat, min_stat;
    gss_buffer_desc input, output;
    gss_qop_t qop_state;
    int conf_state;
    struct gssapi_data *d = app_data;
    size_t ret_len;

    input.length = len;
    input.value = buf;
    maj_stat = gss_unwrap (&min_stat,
			   d->context_hdl,
			   &input,
			   &output,
			   &conf_state,
			   &qop_state);
    if(GSS_ERROR(maj_stat))
	return -1;
    memmove(buf, output.value, output.length);
    ret_len = output.length;
    gss_release_buffer(&min_stat, &output);
    return ret_len;
}

static int
gss_overhead(void *app_data, int level, int len)
{
    return 100; /* dunno? */
}


static int
gss_encode(void *app_data, void *from, int length, int level, void **to)
{
    OM_uint32 maj_stat, min_stat;
    gss_buffer_desc input, output;
    int conf_state;
    struct gssapi_data *d = app_data;

    input.length = length;
    input.value = from;
    maj_stat = gss_wrap (&min_stat,
			 d->context_hdl,
			 level == prot_private,
			 GSS_C_QOP_DEFAULT,
			 &input,
			 &conf_state,
			 &output);
    *to = output.value;
    return output.length;
}

static void
sockaddr_to_gss_address (struct sockaddr *sa,
			 OM_uint32 *addr_type,
			 gss_buffer_desc *gss_addr)
{
    switch (sa->sa_family) {
#ifdef HAVE_IPV6
    case AF_INET6 : {
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

	gss_addr->length = 16;
	gss_addr->value  = &sin6->sin6_addr;
	*addr_type       = GSS_C_AF_INET6;
	break;
    }
#endif
    case AF_INET : {
	struct sockaddr_in *sin4 = (struct sockaddr_in *)sa;

	gss_addr->length = 4;
	gss_addr->value  = &sin4->sin_addr;
	*addr_type       = GSS_C_AF_INET;
	break;
    }
    default :
	errx (1, "unknown address family %d", sa->sa_family);

    }
}

/* end common stuff */

#ifdef FTP_SERVER

static int
gss_adat(void *app_data, void *buf, size_t len)
{
    char *p = NULL;
    gss_buffer_desc input_token, output_token;
    OM_uint32 maj_stat, min_stat;
    gss_name_t client_name;
    struct gssapi_data *d = app_data;
    gss_channel_bindings_t bindings;

    if (ftp_do_gss_bindings) {
	bindings = malloc(sizeof(*bindings));
	if (bindings == NULL)
	    errx(1, "out of memory");

	sockaddr_to_gss_address (his_addr,
				 &bindings->initiator_addrtype,
				 &bindings->initiator_address);
	sockaddr_to_gss_address (ctrl_addr,
				 &bindings->acceptor_addrtype,
				 &bindings->acceptor_address);

	bindings->application_data.length = 0;
	bindings->application_data.value = NULL;
    } else
	bindings = GSS_C_NO_CHANNEL_BINDINGS;

    input_token.value = buf;
    input_token.length = len;

    maj_stat = gss_accept_sec_context (&min_stat,
				       &d->context_hdl,
				       GSS_C_NO_CREDENTIAL,
				       &input_token,
				       bindings,
				       &client_name,
				       NULL,
				       &output_token,
				       NULL,
				       NULL,
				       &d->delegated_cred_handle);

    if (bindings != GSS_C_NO_CHANNEL_BINDINGS)
	free(bindings);

    if(output_token.length) {
	if(base64_encode(output_token.value, output_token.length, &p) < 0) {
	    reply(535, "Out of memory base64-encoding.");
	    return -1;
	}
	gss_release_buffer(&min_stat, &output_token);
    }
    if(maj_stat == GSS_S_COMPLETE){
	d->client_name = client_name;
	client_name = GSS_C_NO_NAME;
	if(p)
	    reply(235, "ADAT=%s", p);
	else
	    reply(235, "ADAT Complete");
	sec_complete = 1;

    } else if(maj_stat == GSS_S_CONTINUE_NEEDED) {
	if(p)
	    reply(335, "ADAT=%s", p);
	else
	    reply(335, "OK, need more data");
    } else {
	OM_uint32 new_stat;
	OM_uint32 msg_ctx = 0;
	gss_buffer_desc status_string;
	gss_display_status(&new_stat,
			   min_stat,
			   GSS_C_MECH_CODE,
			   GSS_C_NO_OID,
			   &msg_ctx,
			   &status_string);
	syslog(LOG_ERR, "gss_accept_sec_context: %.*s",
	       (int)status_string.length,
	       (char*)status_string.value);
	gss_release_buffer(&new_stat, &status_string);
	reply(431, "Security resource unavailable");
    }

    if (client_name)
	gss_release_name(&min_stat, &client_name);
    free(p);
    return 0;
}

int gssapi_userok(void*, char*);
int gssapi_session(void*, char*);

struct sec_server_mech gss_server_mech = {
    "GSSAPI",
    sizeof(struct gssapi_data),
    gss_init, /* init */
    NULL, /* end */
    gss_check_prot,
    gss_overhead,
    gss_encode,
    gss_decode,
    /* */
    NULL,
    gss_adat,
    NULL, /* pbsz */
    NULL, /* ccc */
    gssapi_userok,
    gssapi_session
};

#else /* FTP_SERVER */

extern struct sockaddr *hisctladdr, *myctladdr;

static int
import_name(const char *kname, const char *host, gss_name_t *target_name)
{
    OM_uint32 maj_stat, min_stat;
    gss_buffer_desc name;
    char *str;

    name.length = asprintf(&str, "%s@%s", kname, host);
    if (str == NULL) {
	printf("Out of memory\n");
	return AUTH_ERROR;
    }
    name.value = str;

    maj_stat = gss_import_name(&min_stat,
			       &name,
			       GSS_C_NT_HOSTBASED_SERVICE,
			       target_name);
    if (GSS_ERROR(maj_stat)) {
	OM_uint32 new_stat;
	OM_uint32 msg_ctx = 0;
	gss_buffer_desc status_string;

	gss_display_status(&new_stat,
			   min_stat,
			   GSS_C_MECH_CODE,
			   GSS_C_NO_OID,
			   &msg_ctx,
			   &status_string);
	printf("Error importing name %.*s: %.*s\n",
	       (int)name.length,
	       (char *)name.value,
	       (int)status_string.length,
	       (char *)status_string.value);
	free(name.value);
	gss_release_buffer(&new_stat, &status_string);
	return AUTH_ERROR;
    }
    free(name.value);
    return 0;
}

static int
gss_auth(void *app_data, char *host)
{

    OM_uint32 maj_stat, min_stat;
    gss_name_t target_name;
    gss_buffer_desc input, output_token;
    int context_established = 0;
    char *p;
    int n;
    gss_channel_bindings_t bindings;
    struct gssapi_data *d = app_data;
    OM_uint32 mech_flags = GSS_C_MUTUAL_FLAG | GSS_C_SEQUENCE_FLAG;

    const char *knames[] = { "ftp", "host", NULL }, **kname = knames;


    if(import_name(*kname++, host, &target_name))
	return AUTH_ERROR;

    input.length = 0;
    input.value = NULL;

    if (ftp_do_gss_bindings) {
	bindings = malloc(sizeof(*bindings));
	if (bindings == NULL)
	    errx(1, "out of memory");

	sockaddr_to_gss_address (myctladdr,
				 &bindings->initiator_addrtype,
				 &bindings->initiator_address);
	sockaddr_to_gss_address (hisctladdr,
				 &bindings->acceptor_addrtype,
				 &bindings->acceptor_address);

	bindings->application_data.length = 0;
	bindings->application_data.value = NULL;
    } else
	bindings = GSS_C_NO_CHANNEL_BINDINGS;

    if (ftp_do_gss_delegate)
	mech_flags |= GSS_C_DELEG_FLAG;

    while(!context_established) {
	maj_stat = gss_init_sec_context(&min_stat,
					GSS_C_NO_CREDENTIAL,
					&d->context_hdl,
					target_name,
					GSS_C_NO_OID,
                                        mech_flags,
					0,
					bindings,
					&input,
					NULL,
					&output_token,
					NULL,
					NULL);
	if (GSS_ERROR(maj_stat)) {
	    OM_uint32 new_stat;
	    OM_uint32 msg_ctx = 0;
	    gss_buffer_desc status_string;

	    d->context_hdl = GSS_C_NO_CONTEXT;

	    gss_release_name(&min_stat, &target_name);

	    if(*kname != NULL) {

		if(import_name(*kname++, host, &target_name)) {
		    if (bindings != GSS_C_NO_CHANNEL_BINDINGS)
			free(bindings);
		    return AUTH_ERROR;
		}
		continue;
	    }

	    if (bindings != GSS_C_NO_CHANNEL_BINDINGS)
		free(bindings);

	    gss_display_status(&new_stat,
			       min_stat,
			       GSS_C_MECH_CODE,
			       GSS_C_NO_OID,
			       &msg_ctx,
			       &status_string);
	    printf("Error initializing security context: %.*s\n",
		   (int)status_string.length,
		   (char*)status_string.value);
	    gss_release_buffer(&new_stat, &status_string);
	    return AUTH_CONTINUE;
	}

	if (input.value) {
	    free(input.value);
	    input.value = NULL;
	    input.length = 0;
	}
	if (output_token.length != 0) {
	    base64_encode(output_token.value, output_token.length, &p);
	    gss_release_buffer(&min_stat, &output_token);
	    n = command("ADAT %s", p);
	    free(p);
	}
	if (GSS_ERROR(maj_stat)) {
	    if (d->context_hdl != GSS_C_NO_CONTEXT)
		gss_delete_sec_context (&min_stat,
					&d->context_hdl,
					GSS_C_NO_BUFFER);
	    break;
	}
	if (maj_stat & GSS_S_CONTINUE_NEEDED) {
	    p = strstr(reply_string, "ADAT=");
	    if(p == NULL){
		printf("Error: expected ADAT in reply. got: %s\n",
		       reply_string);
		if (bindings != GSS_C_NO_CHANNEL_BINDINGS)
		    free(bindings);
		return AUTH_ERROR;
	    } else {
		p+=5;
		input.value = malloc(strlen(p));
		input.length = base64_decode(p, input.value);
	    }
	} else {
	    if(code != 235) {
		printf("Unrecognized response code: %d\n", code);
		if (bindings != GSS_C_NO_CHANNEL_BINDINGS)
		    free(bindings);
		return AUTH_ERROR;
	    }
	    context_established = 1;
	}
    }

    gss_release_name(&min_stat, &target_name);

    if (bindings != GSS_C_NO_CHANNEL_BINDINGS)
	free(bindings);
    if (input.value)
	free(input.value);

    {
	gss_name_t targ_name;

	maj_stat = gss_inquire_context(&min_stat,
				       d->context_hdl,
				       NULL,
				       &targ_name,
				       NULL,
				       NULL,
				       NULL,
				       NULL,
				       NULL);
	if (GSS_ERROR(maj_stat) == 0) {
	    gss_buffer_desc name;
	    maj_stat = gss_display_name (&min_stat,
					 targ_name,
					 &name,
					 NULL);
	    if (GSS_ERROR(maj_stat) == 0) {
		printf("Authenticated to <%.*s>\n",
			(int)name.length,
			(char *)name.value);
		gss_release_buffer(&min_stat, &name);
	    }
	    gss_release_name(&min_stat, &targ_name);
	} else
	    printf("Failed to get gss name of peer.\n");
    }


    return AUTH_OK;
}

struct sec_client_mech gss_client_mech = {
    "GSSAPI",
    sizeof(struct gssapi_data),
    gss_init,
    gss_auth,
    NULL, /* end */
    gss_check_prot,
    gss_overhead,
    gss_encode,
    gss_decode,
};

#endif /* FTP_SERVER */
