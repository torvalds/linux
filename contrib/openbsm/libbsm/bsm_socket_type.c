/*-
 * Copyright (c) 2008 Apple Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE. 
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <config/config.h>

#include <bsm/audit_socket_type.h>
#include <bsm/libbsm.h>

struct bsm_socket_type {
	u_short	bst_bsm_socket_type;
	int	bst_local_socket_type;
};

#define	ST_NO_LOCAL_MAPPING	-600

static const struct bsm_socket_type bsm_socket_types[] = {
	{ BSM_SOCK_DGRAM, SOCK_DGRAM },
	{ BSM_SOCK_STREAM, SOCK_STREAM },
	{ BSM_SOCK_RAW, SOCK_RAW },
	{ BSM_SOCK_RDM, SOCK_RDM },
	{ BSM_SOCK_SEQPACKET, SOCK_SEQPACKET },
};
static const int bsm_socket_types_count = sizeof(bsm_socket_types) /
	    sizeof(bsm_socket_types[0]);

static const struct bsm_socket_type *
bsm_lookup_local_socket_type(int local_socket_type)
{
	int i;

	for (i = 0; i < bsm_socket_types_count; i++) {
		if (bsm_socket_types[i].bst_local_socket_type ==
		    local_socket_type)
			return (&bsm_socket_types[i]);
	}
	return (NULL);
}

u_short
au_socket_type_to_bsm(int local_socket_type)
{
	const struct bsm_socket_type *bstp;

	bstp = bsm_lookup_local_socket_type(local_socket_type);
	if (bstp == NULL)
		return (BSM_SOCK_UNKNOWN);
	return (bstp->bst_bsm_socket_type);
}

static const struct bsm_socket_type *
bsm_lookup_bsm_socket_type(u_short bsm_socket_type)
{
	int i;

	for (i = 0; i < bsm_socket_types_count; i++) {
		if (bsm_socket_types[i].bst_bsm_socket_type ==
		    bsm_socket_type)
			return (&bsm_socket_types[i]);
	}
	return (NULL);
}

int
au_bsm_to_socket_type(u_short bsm_socket_type, int *local_socket_typep)
{
	const struct bsm_socket_type *bstp;

	bstp = bsm_lookup_bsm_socket_type(bsm_socket_type);
	if (bstp == NULL || bstp->bst_local_socket_type)
		return (-1);
	*local_socket_typep = bstp->bst_local_socket_type;
	return (0);
}
