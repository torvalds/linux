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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "pjdlog.h"
#include "proto.h"
#include "proto_impl.h"

#define	PROTO_CONN_MAGIC	0x907041c
struct proto_conn {
	int		 pc_magic;
	struct proto	*pc_proto;
	void		*pc_ctx;
	int		 pc_side;
#define	PROTO_SIDE_CLIENT		0
#define	PROTO_SIDE_SERVER_LISTEN	1
#define	PROTO_SIDE_SERVER_WORK		2
};

static TAILQ_HEAD(, proto) protos = TAILQ_HEAD_INITIALIZER(protos);

void
proto_register(struct proto *proto, bool isdefault)
{
	static bool seen_default = false;

	if (!isdefault)
		TAILQ_INSERT_HEAD(&protos, proto, prt_next);
	else {
		PJDLOG_ASSERT(!seen_default);
		seen_default = true;
		TAILQ_INSERT_TAIL(&protos, proto, prt_next);
	}
}

static struct proto_conn *
proto_alloc(struct proto *proto, int side)
{
	struct proto_conn *conn;

	PJDLOG_ASSERT(proto != NULL);
	PJDLOG_ASSERT(side == PROTO_SIDE_CLIENT ||
	    side == PROTO_SIDE_SERVER_LISTEN ||
	    side == PROTO_SIDE_SERVER_WORK);

	conn = malloc(sizeof(*conn));
	if (conn != NULL) {
		conn->pc_proto = proto;
		conn->pc_side = side;
		conn->pc_magic = PROTO_CONN_MAGIC;
	}
	return (conn);
}

static void
proto_free(struct proto_conn *conn)
{

	PJDLOG_ASSERT(conn != NULL);
	PJDLOG_ASSERT(conn->pc_magic == PROTO_CONN_MAGIC);
	PJDLOG_ASSERT(conn->pc_side == PROTO_SIDE_CLIENT ||
	    conn->pc_side == PROTO_SIDE_SERVER_LISTEN ||
	    conn->pc_side == PROTO_SIDE_SERVER_WORK);
	PJDLOG_ASSERT(conn->pc_proto != NULL);

	bzero(conn, sizeof(*conn));
	free(conn);
}

static int
proto_common_setup(const char *srcaddr, const char *dstaddr, int timeout,
    int side, struct proto_conn **connp)
{
	struct proto *proto;
	struct proto_conn *conn;
	void *ctx;
	int ret;

	PJDLOG_ASSERT(side == PROTO_SIDE_CLIENT ||
	    side == PROTO_SIDE_SERVER_LISTEN);

	TAILQ_FOREACH(proto, &protos, prt_next) {
		if (side == PROTO_SIDE_CLIENT) {
			if (proto->prt_connect == NULL) {
				ret = -1;
			} else {
				ret = proto->prt_connect(srcaddr, dstaddr,
				    timeout, &ctx);
			}
		} else /* if (side == PROTO_SIDE_SERVER_LISTEN) */ {
			if (proto->prt_server == NULL)
				ret = -1;
			else
				ret = proto->prt_server(dstaddr, &ctx);
		}
		/*
		 * ret == 0  - success
		 * ret == -1 - dstaddr is not for this protocol
		 * ret > 0   - right protocol, but an error occured
		 */
		if (ret >= 0)
			break;
	}
	if (proto == NULL) {
		/* Unrecognized address. */
		errno = EINVAL;
		return (-1);
	}
	if (ret > 0) {
		/* An error occured. */
		errno = ret;
		return (-1);
	}
	conn = proto_alloc(proto, side);
	if (conn == NULL) {
		if (proto->prt_close != NULL)
			proto->prt_close(ctx);
		errno = ENOMEM;
		return (-1);
	}
	conn->pc_ctx = ctx;
	*connp = conn;

	return (0);
}

int
proto_connect(const char *srcaddr, const char *dstaddr, int timeout,
    struct proto_conn **connp)
{

	PJDLOG_ASSERT(srcaddr == NULL || srcaddr[0] != '\0');
	PJDLOG_ASSERT(dstaddr != NULL);
	PJDLOG_ASSERT(timeout >= -1);

	return (proto_common_setup(srcaddr, dstaddr, timeout,
	    PROTO_SIDE_CLIENT, connp));
}

int
proto_connect_wait(struct proto_conn *conn, int timeout)
{
	int error;

	PJDLOG_ASSERT(conn != NULL);
	PJDLOG_ASSERT(conn->pc_magic == PROTO_CONN_MAGIC);
	PJDLOG_ASSERT(conn->pc_side == PROTO_SIDE_CLIENT);
	PJDLOG_ASSERT(conn->pc_proto != NULL);
	PJDLOG_ASSERT(conn->pc_proto->prt_connect_wait != NULL);
	PJDLOG_ASSERT(timeout >= 0);

	error = conn->pc_proto->prt_connect_wait(conn->pc_ctx, timeout);
	if (error != 0) {
		errno = error;
		return (-1);
	}

	return (0);
}

int
proto_server(const char *addr, struct proto_conn **connp)
{

	PJDLOG_ASSERT(addr != NULL);

	return (proto_common_setup(NULL, addr, -1, PROTO_SIDE_SERVER_LISTEN,
	    connp));
}

int
proto_accept(struct proto_conn *conn, struct proto_conn **newconnp)
{
	struct proto_conn *newconn;
	int error;

	PJDLOG_ASSERT(conn != NULL);
	PJDLOG_ASSERT(conn->pc_magic == PROTO_CONN_MAGIC);
	PJDLOG_ASSERT(conn->pc_side == PROTO_SIDE_SERVER_LISTEN);
	PJDLOG_ASSERT(conn->pc_proto != NULL);
	PJDLOG_ASSERT(conn->pc_proto->prt_accept != NULL);

	newconn = proto_alloc(conn->pc_proto, PROTO_SIDE_SERVER_WORK);
	if (newconn == NULL)
		return (-1);

	error = conn->pc_proto->prt_accept(conn->pc_ctx, &newconn->pc_ctx);
	if (error != 0) {
		proto_free(newconn);
		errno = error;
		return (-1);
	}

	*newconnp = newconn;

	return (0);
}

int
proto_send(const struct proto_conn *conn, const void *data, size_t size)
{
	int error;

	PJDLOG_ASSERT(conn != NULL);
	PJDLOG_ASSERT(conn->pc_magic == PROTO_CONN_MAGIC);
	PJDLOG_ASSERT(conn->pc_proto != NULL);
	PJDLOG_ASSERT(conn->pc_proto->prt_send != NULL);

	error = conn->pc_proto->prt_send(conn->pc_ctx, data, size, -1);
	if (error != 0) {
		errno = error;
		return (-1);
	}
	return (0);
}

int
proto_recv(const struct proto_conn *conn, void *data, size_t size)
{
	int error;

	PJDLOG_ASSERT(conn != NULL);
	PJDLOG_ASSERT(conn->pc_magic == PROTO_CONN_MAGIC);
	PJDLOG_ASSERT(conn->pc_proto != NULL);
	PJDLOG_ASSERT(conn->pc_proto->prt_recv != NULL);

	error = conn->pc_proto->prt_recv(conn->pc_ctx, data, size, NULL);
	if (error != 0) {
		errno = error;
		return (-1);
	}
	return (0);
}

int
proto_connection_send(const struct proto_conn *conn, struct proto_conn *mconn)
{
	const char *protoname;
	int error, fd;

	PJDLOG_ASSERT(conn != NULL);
	PJDLOG_ASSERT(conn->pc_magic == PROTO_CONN_MAGIC);
	PJDLOG_ASSERT(conn->pc_proto != NULL);
	PJDLOG_ASSERT(conn->pc_proto->prt_send != NULL);
	PJDLOG_ASSERT(mconn != NULL);
	PJDLOG_ASSERT(mconn->pc_magic == PROTO_CONN_MAGIC);
	PJDLOG_ASSERT(mconn->pc_proto != NULL);
	fd = proto_descriptor(mconn);
	PJDLOG_ASSERT(fd >= 0);
	protoname = mconn->pc_proto->prt_name;
	PJDLOG_ASSERT(protoname != NULL);

	error = conn->pc_proto->prt_send(conn->pc_ctx,
	    (const unsigned char *)protoname, strlen(protoname) + 1, fd);
	proto_close(mconn);
	if (error != 0) {
		errno = error;
		return (-1);
	}
	return (0);
}

int
proto_wrap(const char *protoname, bool client, int fd,
    struct proto_conn **newconnp)
{
	struct proto *proto;
	struct proto_conn *newconn;
	int error;

	TAILQ_FOREACH(proto, &protos, prt_next) {
		if (strcmp(proto->prt_name, protoname) == 0)
			break;
	}
	if (proto == NULL) {
		errno = EINVAL;
		return (-1);
	}

	newconn = proto_alloc(proto,
	    client ? PROTO_SIDE_CLIENT : PROTO_SIDE_SERVER_WORK);
	if (newconn == NULL)
		return (-1);
	PJDLOG_ASSERT(newconn->pc_proto->prt_wrap != NULL);
	error = newconn->pc_proto->prt_wrap(fd, client, &newconn->pc_ctx);
	if (error != 0) {
		proto_free(newconn);
		errno = error;
		return (-1);
	}

	*newconnp = newconn;

	return (0);
}

int
proto_connection_recv(const struct proto_conn *conn, bool client,
    struct proto_conn **newconnp)
{
	char protoname[128];
	int error, fd;

	PJDLOG_ASSERT(conn != NULL);
	PJDLOG_ASSERT(conn->pc_magic == PROTO_CONN_MAGIC);
	PJDLOG_ASSERT(conn->pc_proto != NULL);
	PJDLOG_ASSERT(conn->pc_proto->prt_recv != NULL);
	PJDLOG_ASSERT(newconnp != NULL);

	bzero(protoname, sizeof(protoname));

	error = conn->pc_proto->prt_recv(conn->pc_ctx,
	    (unsigned char *)protoname, sizeof(protoname) - 1, &fd);
	if (error != 0) {
		errno = error;
		return (-1);
	}

	PJDLOG_ASSERT(fd >= 0);

	return (proto_wrap(protoname, client, fd, newconnp));
}

int
proto_descriptor(const struct proto_conn *conn)
{

	PJDLOG_ASSERT(conn != NULL);
	PJDLOG_ASSERT(conn->pc_magic == PROTO_CONN_MAGIC);
	PJDLOG_ASSERT(conn->pc_proto != NULL);
	PJDLOG_ASSERT(conn->pc_proto->prt_descriptor != NULL);

	return (conn->pc_proto->prt_descriptor(conn->pc_ctx));
}

bool
proto_address_match(const struct proto_conn *conn, const char *addr)
{

	PJDLOG_ASSERT(conn != NULL);
	PJDLOG_ASSERT(conn->pc_magic == PROTO_CONN_MAGIC);
	PJDLOG_ASSERT(conn->pc_proto != NULL);
	PJDLOG_ASSERT(conn->pc_proto->prt_address_match != NULL);

	return (conn->pc_proto->prt_address_match(conn->pc_ctx, addr));
}

void
proto_local_address(const struct proto_conn *conn, char *addr, size_t size)
{

	PJDLOG_ASSERT(conn != NULL);
	PJDLOG_ASSERT(conn->pc_magic == PROTO_CONN_MAGIC);
	PJDLOG_ASSERT(conn->pc_proto != NULL);
	PJDLOG_ASSERT(conn->pc_proto->prt_local_address != NULL);

	conn->pc_proto->prt_local_address(conn->pc_ctx, addr, size);
}

void
proto_remote_address(const struct proto_conn *conn, char *addr, size_t size)
{

	PJDLOG_ASSERT(conn != NULL);
	PJDLOG_ASSERT(conn->pc_magic == PROTO_CONN_MAGIC);
	PJDLOG_ASSERT(conn->pc_proto != NULL);
	PJDLOG_ASSERT(conn->pc_proto->prt_remote_address != NULL);

	conn->pc_proto->prt_remote_address(conn->pc_ctx, addr, size);
}

int
proto_timeout(const struct proto_conn *conn, int timeout)
{
	struct timeval tv;
	int fd;

	PJDLOG_ASSERT(conn != NULL);
	PJDLOG_ASSERT(conn->pc_magic == PROTO_CONN_MAGIC);
	PJDLOG_ASSERT(conn->pc_proto != NULL);

	fd = proto_descriptor(conn);
	if (fd < 0)
		return (-1);

	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0)
		return (-1);
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
		return (-1);

	return (0);
}

void
proto_close(struct proto_conn *conn)
{

	PJDLOG_ASSERT(conn != NULL);
	PJDLOG_ASSERT(conn->pc_magic == PROTO_CONN_MAGIC);
	PJDLOG_ASSERT(conn->pc_proto != NULL);
	PJDLOG_ASSERT(conn->pc_proto->prt_close != NULL);

	conn->pc_proto->prt_close(conn->pc_ctx);
	proto_free(conn);
}

int
proto_exec(int argc, char *argv[])
{
	struct proto *proto;
	int error;

	if (argc == 0) {
		errno = EINVAL;
		return (-1);
	}
	TAILQ_FOREACH(proto, &protos, prt_next) {
		if (strcmp(proto->prt_name, argv[0]) == 0)
			break;
	}
	if (proto == NULL) {
		errno = EINVAL;
		return (-1);
	}
	if (proto->prt_exec == NULL) {
		errno = EOPNOTSUPP;
		return (-1);
	}
	error = proto->prt_exec(argc, argv);
	if (error != 0) {
		errno = error;
		return (-1);
	}
	/* NOTREACHED */
	return (0);
}

struct proto_nvpair {
	char	*pnv_name;
	char	*pnv_value;
	TAILQ_ENTRY(proto_nvpair) pnv_next;
};

static TAILQ_HEAD(, proto_nvpair) proto_nvpairs =
    TAILQ_HEAD_INITIALIZER(proto_nvpairs);

int
proto_set(const char *name, const char *value)
{
	struct proto_nvpair *pnv;

	TAILQ_FOREACH(pnv, &proto_nvpairs, pnv_next) {
		if (strcmp(pnv->pnv_name, name) == 0)
			break;
	}
	if (pnv != NULL) {
		TAILQ_REMOVE(&proto_nvpairs, pnv, pnv_next);
		free(pnv->pnv_value);
	} else {
		pnv = malloc(sizeof(*pnv));
		if (pnv == NULL)
			return (-1);
		pnv->pnv_name = strdup(name);
		if (pnv->pnv_name == NULL) {
			free(pnv);
			return (-1);
		}
	}
	pnv->pnv_value = strdup(value);
	if (pnv->pnv_value == NULL) {
		free(pnv->pnv_name);
		free(pnv);
		return (-1);
	}
	TAILQ_INSERT_TAIL(&proto_nvpairs, pnv, pnv_next);
	return (0);
}

const char *
proto_get(const char *name)
{
	struct proto_nvpair *pnv;

	TAILQ_FOREACH(pnv, &proto_nvpairs, pnv_next) {
		if (strcmp(pnv->pnv_name, name) == 0)
			break;
	}
	if (pnv != NULL)
		return (pnv->pnv_value);
	return (NULL);
}
