/*
 * Copyright (c) 1997-2004 Kungliga Tekniska Högskolan
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

/*
 * $Id$
 */

#ifndef __ADMIN_LOCL_H__
#define __ADMIN_LOCL_H__

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
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

#ifdef HAVE_UTIL_H
#include <util.h>
#endif
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#include <err.h>
#include <roken.h>
#include <krb5.h>
#include <krb5_locl.h>
#include <hdb.h>
#include <hdb_err.h>
#include <hex.h>
#include <kadm5/admin.h>
#include <kadm5/private.h>
#include <kadm5/kadm5_err.h>
#include <parse_time.h>
#include <getarg.h>

extern krb5_context context;
extern void * kadm_handle;

#undef ALLOC
#define ALLOC(X) ((X) = malloc(sizeof(*(X))))

/* util.c */

void attributes2str(krb5_flags, char *, size_t);
int  str2attributes(const char *, krb5_flags *);
int  parse_attributes (const char *, krb5_flags *, int *, int);
int  edit_attributes (const char *, krb5_flags *, int *, int);

void time_t2str(time_t, char *, size_t, int);
int  str2time_t (const char *, time_t *);
int  parse_timet (const char *, krb5_timestamp *, int *, int);
int  edit_timet (const char *, krb5_timestamp *, int *,
		 int);

void deltat2str(unsigned, char *, size_t);
int  str2deltat(const char *, krb5_deltat *);
int  parse_deltat (const char *, krb5_deltat *, int *, int);
int  edit_deltat (const char *, krb5_deltat *, int *, int);

int edit_entry(kadm5_principal_ent_t, int *, kadm5_principal_ent_t, int);
void set_defaults(kadm5_principal_ent_t, int *, kadm5_principal_ent_t, int);
int set_entry(krb5_context, kadm5_principal_ent_t, int *,
	      const char *, const char *, const char *,
	      const char *, const char *);
int
foreach_principal(const char *, int (*)(krb5_principal, void*),
		  const char *, void *);

int parse_des_key (const char *, krb5_key_data *, const char **);

/* random_password.c */

void
random_password(char *, size_t);

/* kadm_conn.c */

extern sig_atomic_t term_flag, doing_useful_work;

void parse_ports(krb5_context, const char*);
void start_server(krb5_context, const char*);

/* server.c */

krb5_error_code
kadmind_loop (krb5_context, krb5_keytab, int);

/* rpc.c */

int
handle_mit(krb5_context, void *, size_t, int);


#endif /* __ADMIN_LOCL_H__ */
