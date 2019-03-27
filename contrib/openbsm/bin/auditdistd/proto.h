/*-
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef	_PROTO_H_
#define	_PROTO_H_

#include <stdbool.h>	/* bool */
#include <stdlib.h>	/* size_t */

struct proto_conn;

int proto_connect(const char *srcaddr, const char *dstaddr, int timeout,
    struct proto_conn **connp);
int proto_connect_wait(struct proto_conn *conn, int timeout);
int proto_server(const char *addr, struct proto_conn **connp);
int proto_accept(struct proto_conn *conn, struct proto_conn **newconnp);
int proto_send(const struct proto_conn *conn, const void *data, size_t size);
int proto_recv(const struct proto_conn *conn, void *data, size_t size);
int proto_connection_send(const struct proto_conn *conn,
    struct proto_conn *mconn);
int proto_connection_recv(const struct proto_conn *conn, bool client,
    struct proto_conn **newconnp);
int proto_descriptor(const struct proto_conn *conn);
bool proto_address_match(const struct proto_conn *conn, const char *addr);
void proto_local_address(const struct proto_conn *conn, char *addr,
    size_t size);
void proto_remote_address(const struct proto_conn *conn, char *addr,
    size_t size);
int proto_timeout(const struct proto_conn *conn, int timeout);
void proto_close(struct proto_conn *conn);
int proto_exec(int argc, char *argv[]);
int proto_set(const char *name, const char *value);
const char *proto_get(const char *name);

#endif	/* !_PROTO_H_ */
