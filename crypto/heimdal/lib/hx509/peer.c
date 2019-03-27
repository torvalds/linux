/*
 * Copyright (c) 2006 - 2007 Kungliga Tekniska Högskolan
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

#include "hx_locl.h"

/**
 * @page page_peer Hx509 crypto selecting functions
 *
 * Peer info structures are used togeter with hx509_crypto_select() to
 * select the best avaible crypto algorithm to use.
 *
 * See the library functions here: @ref hx509_peer
 */

/**
 * Allocate a new peer info structure an init it to default values.
 *
 * @param context A hx509 context.
 * @param peer return an allocated peer, free with hx509_peer_info_free().
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_peer
 */

int
hx509_peer_info_alloc(hx509_context context, hx509_peer_info *peer)
{
    *peer = calloc(1, sizeof(**peer));
    if (*peer == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }
    return 0;
}


static void
free_cms_alg(hx509_peer_info peer)
{
    if (peer->val) {
	size_t i;
	for (i = 0; i < peer->len; i++)
	    free_AlgorithmIdentifier(&peer->val[i]);
	free(peer->val);
	peer->val = NULL;
	peer->len = 0;
    }
}

/**
 * Free a peer info structure.
 *
 * @param peer peer info to be freed.
 *
 * @ingroup hx509_peer
 */

void
hx509_peer_info_free(hx509_peer_info peer)
{
    if (peer == NULL)
	return;
    if (peer->cert)
	hx509_cert_free(peer->cert);
    free_cms_alg(peer);
    memset(peer, 0, sizeof(*peer));
    free(peer);
}

/**
 * Set the certificate that remote peer is using.
 *
 * @param peer peer info to update
 * @param cert cerificate of the remote peer.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_peer
 */

int
hx509_peer_info_set_cert(hx509_peer_info peer,
			 hx509_cert cert)
{
    if (peer->cert)
	hx509_cert_free(peer->cert);
    peer->cert = hx509_cert_ref(cert);
    return 0;
}

/**
 * Add an additional algorithm that the peer supports.
 *
 * @param context A hx509 context.
 * @param peer the peer to set the new algorithms for
 * @param val an AlgorithmsIdentier to add
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_peer
 */

int
hx509_peer_info_add_cms_alg(hx509_context context,
			    hx509_peer_info peer,
			    const AlgorithmIdentifier *val)
{
    void *ptr;
    int ret;

    ptr = realloc(peer->val, sizeof(peer->val[0]) * (peer->len + 1));
    if (ptr == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }
    peer->val = ptr;
    ret = copy_AlgorithmIdentifier(val, &peer->val[peer->len]);
    if (ret == 0)
	peer->len += 1;
    else
	hx509_set_error_string(context, 0, ret, "out of memory");
    return ret;
}

/**
 * Set the algorithms that the peer supports.
 *
 * @param context A hx509 context.
 * @param peer the peer to set the new algorithms for
 * @param val array of supported AlgorithmsIdentiers
 * @param len length of array val.
 *
 * @return An hx509 error code, see hx509_get_error_string().
 *
 * @ingroup hx509_peer
 */

int
hx509_peer_info_set_cms_algs(hx509_context context,
			     hx509_peer_info peer,
			     const AlgorithmIdentifier *val,
			     size_t len)
{
    size_t i;

    free_cms_alg(peer);

    peer->val = calloc(len, sizeof(*peer->val));
    if (peer->val == NULL) {
	peer->len = 0;
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }
    peer->len = len;
    for (i = 0; i < len; i++) {
	int ret;
	ret = copy_AlgorithmIdentifier(&val[i], &peer->val[i]);
	if (ret) {
	    hx509_clear_error_string(context);
	    free_cms_alg(peer);
	    return ret;
	}
    }
    return 0;
}

#if 0

/*
 * S/MIME
 */

int
hx509_peer_info_parse_smime(hx509_peer_info peer,
			    const heim_octet_string *data)
{
    return 0;
}

int
hx509_peer_info_unparse_smime(hx509_peer_info peer,
			      heim_octet_string *data)
{
    return 0;
}

/*
 * For storing hx509_peer_info to be able to cache them.
 */

int
hx509_peer_info_parse(hx509_peer_info peer,
		      const heim_octet_string *data)
{
    return 0;
}

int
hx509_peer_info_unparse(hx509_peer_info peer,
			heim_octet_string *data)
{
    return 0;
}
#endif
