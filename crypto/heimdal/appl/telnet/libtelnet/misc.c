/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>

RCSID("$Id$");

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <roken.h>
#ifdef SOCKS
#include <socks.h>
#endif
#include "misc.h"
#include "auth.h"
#include "encrypt.h"


const char *RemoteHostName;
const char *LocalHostName;
char *UserNameRequested = 0;
int ConnectedCount = 0;

void
auth_encrypt_init(const char *local, const char *remote, const char *name,
		  int server)
{
    RemoteHostName = remote;
    LocalHostName = local;
#ifdef AUTHENTICATION
    auth_init(name, server);
#endif
#ifdef ENCRYPTION
    encrypt_init(name, server);
#endif
    if (UserNameRequested) {
	free(UserNameRequested);
	UserNameRequested = 0;
    }
}

void
auth_encrypt_user(const char *name)
{
    if (UserNameRequested)
	free(UserNameRequested);
    UserNameRequested = name ? strdup(name) : 0;
}

void
auth_encrypt_connect(int cnt)
{
}

void
printd(const unsigned char *data, int cnt)
{
    if (cnt > 16)
	cnt = 16;
    while (cnt-- > 0) {
	printf(" %02x", *data);
	++data;
    }
}
