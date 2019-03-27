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
krb5_auth_con_init(krb5_context context,
		   krb5_auth_context *auth_context)
{
    krb5_auth_context p;

    ALLOC(p, 1);
    if(!p) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    memset(p, 0, sizeof(*p));
    ALLOC(p->authenticator, 1);
    if (!p->authenticator) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	free(p);
	return ENOMEM;
    }
    memset (p->authenticator, 0, sizeof(*p->authenticator));
    p->flags = KRB5_AUTH_CONTEXT_DO_TIME;

    p->local_address  = NULL;
    p->remote_address = NULL;
    p->local_port     = 0;
    p->remote_port    = 0;
    p->keytype        = ENCTYPE_NULL;
    p->cksumtype      = CKSUMTYPE_NONE;
    *auth_context     = p;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_free(krb5_context context,
		   krb5_auth_context auth_context)
{
    if (auth_context != NULL) {
	krb5_free_authenticator(context, &auth_context->authenticator);
	if(auth_context->local_address){
	    free_HostAddress(auth_context->local_address);
	    free(auth_context->local_address);
	}
	if(auth_context->remote_address){
	    free_HostAddress(auth_context->remote_address);
	    free(auth_context->remote_address);
	}
	krb5_free_keyblock(context, auth_context->keyblock);
	krb5_free_keyblock(context, auth_context->remote_subkey);
	krb5_free_keyblock(context, auth_context->local_subkey);
	free (auth_context);
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setflags(krb5_context context,
		       krb5_auth_context auth_context,
		       int32_t flags)
{
    auth_context->flags = flags;
    return 0;
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getflags(krb5_context context,
		       krb5_auth_context auth_context,
		       int32_t *flags)
{
    *flags = auth_context->flags;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_addflags(krb5_context context,
		       krb5_auth_context auth_context,
		       int32_t addflags,
		       int32_t *flags)
{
    if (flags)
	*flags = auth_context->flags;
    auth_context->flags |= addflags;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_removeflags(krb5_context context,
			  krb5_auth_context auth_context,
			  int32_t removeflags,
			  int32_t *flags)
{
    if (flags)
	*flags = auth_context->flags;
    auth_context->flags &= ~removeflags;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setaddrs(krb5_context context,
		       krb5_auth_context auth_context,
		       krb5_address *local_addr,
		       krb5_address *remote_addr)
{
    if (local_addr) {
	if (auth_context->local_address)
	    krb5_free_address (context, auth_context->local_address);
	else
	    if ((auth_context->local_address = malloc(sizeof(krb5_address))) == NULL)
		return ENOMEM;
	krb5_copy_address(context, local_addr, auth_context->local_address);
    }
    if (remote_addr) {
	if (auth_context->remote_address)
	    krb5_free_address (context, auth_context->remote_address);
	else
	    if ((auth_context->remote_address = malloc(sizeof(krb5_address))) == NULL)
		return ENOMEM;
	krb5_copy_address(context, remote_addr, auth_context->remote_address);
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_genaddrs(krb5_context context,
		       krb5_auth_context auth_context,
		       krb5_socket_t fd, int flags)
{
    krb5_error_code ret;
    krb5_address local_k_address, remote_k_address;
    krb5_address *lptr = NULL, *rptr = NULL;
    struct sockaddr_storage ss_local, ss_remote;
    struct sockaddr *local  = (struct sockaddr *)&ss_local;
    struct sockaddr *remote = (struct sockaddr *)&ss_remote;
    socklen_t len;

    if(flags & KRB5_AUTH_CONTEXT_GENERATE_LOCAL_ADDR) {
	if (auth_context->local_address == NULL) {
	    len = sizeof(ss_local);
	    if(rk_IS_SOCKET_ERROR(getsockname(fd, local, &len))) {
		char buf[128];
		ret = rk_SOCK_ERRNO;
		rk_strerror_r(ret, buf, sizeof(buf));
		krb5_set_error_message(context, ret, "getsockname: %s", buf);
		goto out;
	    }
	    ret = krb5_sockaddr2address (context, local, &local_k_address);
	    if(ret) goto out;
	    if(flags & KRB5_AUTH_CONTEXT_GENERATE_LOCAL_FULL_ADDR) {
		krb5_sockaddr2port (context, local, &auth_context->local_port);
	    } else
		auth_context->local_port = 0;
	    lptr = &local_k_address;
	}
    }
    if(flags & KRB5_AUTH_CONTEXT_GENERATE_REMOTE_ADDR) {
	len = sizeof(ss_remote);
	if(rk_IS_SOCKET_ERROR(getpeername(fd, remote, &len))) {
	    char buf[128];
	    ret = rk_SOCK_ERRNO;
	    rk_strerror_r(ret, buf, sizeof(buf));
	    krb5_set_error_message(context, ret, "getpeername: %s", buf);
	    goto out;
	}
	ret = krb5_sockaddr2address (context, remote, &remote_k_address);
	if(ret) goto out;
	if(flags & KRB5_AUTH_CONTEXT_GENERATE_REMOTE_FULL_ADDR) {
	    krb5_sockaddr2port (context, remote, &auth_context->remote_port);
	} else
	    auth_context->remote_port = 0;
	rptr = &remote_k_address;
    }
    ret = krb5_auth_con_setaddrs (context,
				  auth_context,
				  lptr,
				  rptr);
  out:
    if (lptr)
	krb5_free_address (context, lptr);
    if (rptr)
	krb5_free_address (context, rptr);
    return ret;

}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setaddrs_from_fd (krb5_context context,
				krb5_auth_context auth_context,
				void *p_fd)
{
    krb5_socket_t fd = *(krb5_socket_t *)p_fd;
    int flags = 0;
    if(auth_context->local_address == NULL)
	flags |= KRB5_AUTH_CONTEXT_GENERATE_LOCAL_FULL_ADDR;
    if(auth_context->remote_address == NULL)
	flags |= KRB5_AUTH_CONTEXT_GENERATE_REMOTE_FULL_ADDR;
    return krb5_auth_con_genaddrs(context, auth_context, fd, flags);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getaddrs(krb5_context context,
		       krb5_auth_context auth_context,
		       krb5_address **local_addr,
		       krb5_address **remote_addr)
{
    if(*local_addr)
	krb5_free_address (context, *local_addr);
    *local_addr = malloc (sizeof(**local_addr));
    if (*local_addr == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    krb5_copy_address(context,
		      auth_context->local_address,
		      *local_addr);

    if(*remote_addr)
	krb5_free_address (context, *remote_addr);
    *remote_addr = malloc (sizeof(**remote_addr));
    if (*remote_addr == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	krb5_free_address (context, *local_addr);
	*local_addr = NULL;
	return ENOMEM;
    }
    krb5_copy_address(context,
		      auth_context->remote_address,
		      *remote_addr);
    return 0;
}

/* coverity[+alloc : arg-*2] */
static krb5_error_code
copy_key(krb5_context context,
	 krb5_keyblock *in,
	 krb5_keyblock **out)
{
    if(in)
	return krb5_copy_keyblock(context, in, out);
    *out = NULL; /* is this right? */
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getkey(krb5_context context,
		     krb5_auth_context auth_context,
		     krb5_keyblock **keyblock)
{
    return copy_key(context, auth_context->keyblock, keyblock);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getlocalsubkey(krb5_context context,
			     krb5_auth_context auth_context,
			     krb5_keyblock **keyblock)
{
    return copy_key(context, auth_context->local_subkey, keyblock);
}

/* coverity[+alloc : arg-*2] */
KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getremotesubkey(krb5_context context,
			      krb5_auth_context auth_context,
			      krb5_keyblock **keyblock)
{
    return copy_key(context, auth_context->remote_subkey, keyblock);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setkey(krb5_context context,
		     krb5_auth_context auth_context,
		     krb5_keyblock *keyblock)
{
    if(auth_context->keyblock)
	krb5_free_keyblock(context, auth_context->keyblock);
    return copy_key(context, keyblock, &auth_context->keyblock);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setlocalsubkey(krb5_context context,
			     krb5_auth_context auth_context,
			     krb5_keyblock *keyblock)
{
    if(auth_context->local_subkey)
	krb5_free_keyblock(context, auth_context->local_subkey);
    return copy_key(context, keyblock, &auth_context->local_subkey);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_generatelocalsubkey(krb5_context context,
				  krb5_auth_context auth_context,
				  krb5_keyblock *key)
{
    krb5_error_code ret;
    krb5_keyblock *subkey;

    ret = krb5_generate_subkey_extended (context, key,
					 auth_context->keytype,
					 &subkey);
    if(ret)
	return ret;
    if(auth_context->local_subkey)
	krb5_free_keyblock(context, auth_context->local_subkey);
    auth_context->local_subkey = subkey;
    return 0;
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setremotesubkey(krb5_context context,
			      krb5_auth_context auth_context,
			      krb5_keyblock *keyblock)
{
    if(auth_context->remote_subkey)
	krb5_free_keyblock(context, auth_context->remote_subkey);
    return copy_key(context, keyblock, &auth_context->remote_subkey);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setcksumtype(krb5_context context,
			   krb5_auth_context auth_context,
			   krb5_cksumtype cksumtype)
{
    auth_context->cksumtype = cksumtype;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getcksumtype(krb5_context context,
			   krb5_auth_context auth_context,
			   krb5_cksumtype *cksumtype)
{
    *cksumtype = auth_context->cksumtype;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setkeytype (krb5_context context,
			  krb5_auth_context auth_context,
			  krb5_keytype keytype)
{
    auth_context->keytype = keytype;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getkeytype (krb5_context context,
			  krb5_auth_context auth_context,
			  krb5_keytype *keytype)
{
    *keytype = auth_context->keytype;
    return 0;
}

#if 0
KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setenctype(krb5_context context,
			 krb5_auth_context auth_context,
			 krb5_enctype etype)
{
    if(auth_context->keyblock)
	krb5_free_keyblock(context, auth_context->keyblock);
    ALLOC(auth_context->keyblock, 1);
    if(auth_context->keyblock == NULL)
	return ENOMEM;
    auth_context->keyblock->keytype = etype;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getenctype(krb5_context context,
			 krb5_auth_context auth_context,
			 krb5_enctype *etype)
{
    krb5_abortx(context, "unimplemented krb5_auth_getenctype called");
}
#endif

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getlocalseqnumber(krb5_context context,
			    krb5_auth_context auth_context,
			    int32_t *seqnumber)
{
  *seqnumber = auth_context->local_seqnumber;
  return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setlocalseqnumber (krb5_context context,
			     krb5_auth_context auth_context,
			     int32_t seqnumber)
{
  auth_context->local_seqnumber = seqnumber;
  return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getremoteseqnumber(krb5_context context,
				 krb5_auth_context auth_context,
				 int32_t *seqnumber)
{
  *seqnumber = auth_context->remote_seqnumber;
  return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setremoteseqnumber (krb5_context context,
			      krb5_auth_context auth_context,
			      int32_t seqnumber)
{
  auth_context->remote_seqnumber = seqnumber;
  return 0;
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getauthenticator(krb5_context context,
			   krb5_auth_context auth_context,
			   krb5_authenticator *authenticator)
{
    *authenticator = malloc(sizeof(**authenticator));
    if (*authenticator == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    copy_Authenticator(auth_context->authenticator,
		       *authenticator);
    return 0;
}


KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_authenticator(krb5_context context,
			krb5_authenticator *authenticator)
{
    free_Authenticator (*authenticator);
    free (*authenticator);
    *authenticator = NULL;
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setuserkey(krb5_context context,
			 krb5_auth_context auth_context,
			 krb5_keyblock *keyblock)
{
    if(auth_context->keyblock)
	krb5_free_keyblock(context, auth_context->keyblock);
    return krb5_copy_keyblock(context, keyblock, &auth_context->keyblock);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getrcache(krb5_context context,
			krb5_auth_context auth_context,
			krb5_rcache *rcache)
{
    *rcache = auth_context->rcache;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setrcache(krb5_context context,
			krb5_auth_context auth_context,
			krb5_rcache rcache)
{
    auth_context->rcache = rcache;
    return 0;
}

#if 0 /* not implemented */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_initivector(krb5_context context,
			  krb5_auth_context auth_context)
{
    krb5_abortx(context, "unimplemented krb5_auth_con_initivector called");
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setivector(krb5_context context,
			 krb5_auth_context auth_context,
			 krb5_pointer ivector)
{
    krb5_abortx(context, "unimplemented krb5_auth_con_setivector called");
}

#endif /* not implemented */
