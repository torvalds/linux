/*
 * Copyright (c) 1995, 1996, 1997, 1998, 1999 Kungliga Tekniska Högskolan
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

/* $Id$ */

#ifndef __KAFS_LOCL_H__
#define __KAFS_LOCL_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#if defined(HAVE_SYS_IOCTL_H) && SunOS != 40
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#ifdef HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif

#ifdef HAVE_SYS_SYSCALL_H
#include <sys/syscall.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_IN6_H
#include <netinet/in6.h>
#endif
#ifdef HAVE_NETINET6_IN6_H
#include <netinet6/in6.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_ARPA_NAMESER_H
#include <arpa/nameser.h>
#endif
#ifdef HAVE_RESOLV_H
#include <resolv.h>
#endif
#include <roken.h>

#ifdef KRB5
#include <krb5.h>
#endif
#ifdef KRB5
#include "crypto-headers.h"
#include <krb5-v4compat.h>
typedef struct credentials CREDENTIALS;
#endif /* KRB5 */
#include <kafs.h>

#include <resolve.h>

#include "afssysdefs.h"

struct kafs_data;
struct kafs_token;
typedef int (*afslog_uid_func_t)(struct kafs_data *,
				 const char *,
				 const char *,
				 uid_t,
				 const char *);

typedef int (*get_cred_func_t)(struct kafs_data*, const char*, const char*,
			       const char*, uid_t, struct kafs_token *);

typedef char* (*get_realm_func_t)(struct kafs_data*, const char*);

struct kafs_data {
    const char *name;
    afslog_uid_func_t afslog_uid;
    get_cred_func_t get_cred;
    get_realm_func_t get_realm;
    const char *(*get_error)(struct kafs_data *, int);
    void (*free_error)(struct kafs_data *, const char *);
    void *data;
};

struct kafs_token {
    struct ClearToken ct;
    void *ticket;
    size_t ticket_len;
};

void _kafs_foldup(char *, const char *);

int _kafs_afslog_all_local_cells(struct kafs_data*, uid_t, const char*);

int _kafs_get_cred(struct kafs_data*, const char*, const char*, const char *,
		   uid_t, struct kafs_token *);

int
_kafs_realm_of_cell(struct kafs_data *, const char *, char **);

int
_kafs_v4_to_kt(CREDENTIALS *, uid_t, struct kafs_token *);

void
_kafs_fixup_viceid(struct ClearToken *, uid_t);

#ifdef _AIX
int aix_pioctl(char*, int, struct ViceIoctl*, int);
int aix_setpag(void);
#endif

#endif /* __KAFS_LOCL_H__ */
