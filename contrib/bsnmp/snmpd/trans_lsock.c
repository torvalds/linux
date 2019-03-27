/*
 * Copyright (c) 2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Begemot: bsnmp/snmpd/trans_lsock.c,v 1.6 2005/02/25 11:50:25 brandt_h Exp $
 *
 * Local domain socket transport
 */
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/ucred.h>
#include <sys/un.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "snmpmod.h"
#include "snmpd.h"
#include "trans_lsock.h"
#include "tree.h"
#include "oid.h"

static const struct asn_oid
	oid_begemotSnmpdLocalPortTable = OIDX_begemotSnmpdLocalPortTable;

static int lsock_start(void);
static int lsock_stop(int);
static void lsock_close_port(struct tport *);
static int lsock_init_port(struct tport *);
static ssize_t lsock_send(struct tport *, const u_char *, size_t,
    const struct sockaddr *, size_t);
static ssize_t lsock_recv(struct tport *, struct port_input *);

/* exported */
const struct transport_def lsock_trans = {
	"lsock",
	OIDX_begemotSnmpdTransLsock,
	lsock_start,
	lsock_stop,
	lsock_close_port,
	lsock_init_port,
	lsock_send,
	lsock_recv
};
static struct transport *my_trans;

static int
lsock_remove(struct tport *tp, intptr_t arg __unused)
{
	struct lsock_port *port = (struct lsock_port *)tp;

	(void)remove(port->name);

	return (-1);
}

static int
lsock_stop(int force)
{

	if (my_trans != NULL) {
		if (!force && trans_first_port(my_trans) != NULL)
			return (SNMP_ERR_GENERR);
		trans_iter_port(my_trans, lsock_remove, 0);
		return (trans_unregister(my_trans));
	}
	return (SNMP_ERR_NOERROR);
}

static int
lsock_start(void)
{
	return (trans_register(&lsock_trans, &my_trans));
}

/*
 * Open a local port. If this is a datagram socket create also the
 * one and only peer.
 */
static int
lsock_open_port(u_char *name, size_t namelen, struct lsock_port **pp,
    int type)
{
	struct lsock_port *port;
	struct lsock_peer *peer = NULL;
	int is_stream, need_cred;
	size_t u;
	int err;
	struct sockaddr_un sa;

	if (namelen == 0 || namelen + 1 > sizeof(sa.sun_path))
		return (SNMP_ERR_BADVALUE);

	switch (type) {
	  case LOCP_DGRAM_UNPRIV:
		is_stream = 0;
		need_cred = 0;
		break;

	  case LOCP_DGRAM_PRIV:
		is_stream = 0;
		need_cred = 1;
		break;

	  case LOCP_STREAM_UNPRIV:
		is_stream = 1;
		need_cred = 0;
		break;

	  case LOCP_STREAM_PRIV:
		is_stream = 1;
		need_cred = 1;
		break;

	  default:
		return (SNMP_ERR_BADVALUE);
	}

	if ((port = calloc(1, sizeof(*port))) == NULL)
		return (SNMP_ERR_GENERR);

	if (!is_stream) {
		if ((peer = calloc(1, sizeof(*peer))) == NULL) {
			free(port);
			return (SNMP_ERR_GENERR);
		}
	}
	if ((port->name = malloc(namelen + 1)) == NULL) {
		free(port);
		if (!is_stream)
			free(peer);
		return (SNMP_ERR_GENERR);
	}
	strncpy(port->name, name, namelen);
	port->name[namelen] = '\0';

	port->type = type;
	port->str_sock = -1;
	LIST_INIT(&port->peers);

	port->tport.index.len = namelen + 1;
	port->tport.index.subs[0] = namelen;
	for (u = 0; u < namelen; u++)
		port->tport.index.subs[u + 1] = name[u];

	if (peer != NULL) {
		LIST_INSERT_HEAD(&port->peers, peer, link);

		peer->port = port;

		peer->input.fd = -1;
		peer->input.id = NULL;
		peer->input.stream = is_stream;
		peer->input.cred = need_cred;
		peer->input.peer = (struct sockaddr *)&peer->peer;
	}

	trans_insert_port(my_trans, &port->tport);

	if (community != COMM_INITIALIZE &&
	    (err = lsock_init_port(&port->tport)) != SNMP_ERR_NOERROR) {
		lsock_close_port(&port->tport);
		return (err);
	}

	*pp = port;

	return (SNMP_ERR_NOERROR);
}

/*
 * Close a local domain peer
 */
static void
lsock_peer_close(struct lsock_peer *peer)
{

	LIST_REMOVE(peer, link);
	snmpd_input_close(&peer->input);
	free(peer);
}

/*
 * Close a local port
 */
static void
lsock_close_port(struct tport *tp)
{
	struct lsock_port *port = (struct lsock_port *)tp;
	struct lsock_peer *peer;

	if (port->str_id != NULL)
		fd_deselect(port->str_id);
	if (port->str_sock >= 0)
		(void)close(port->str_sock);
	(void)remove(port->name);

	trans_remove_port(tp);

	while ((peer = LIST_FIRST(&port->peers)) != NULL)
		lsock_peer_close(peer);

	free(port->name);
	free(port);
}

/*
 * Input on a local socket (either datagram or stream)
 */
static void
lsock_input(int fd __unused, void *udata)
{
	struct lsock_peer *peer = udata;
	struct lsock_port *p = peer->port;

	peer->input.peerlen = sizeof(peer->peer);
	if (snmpd_input(&peer->input, &p->tport) == -1 && peer->input.stream)
		/* framing or other input error */
		lsock_peer_close(peer);
}

/*
 * A UNIX domain listening socket is ready. This means we have a peer
 * that we need to accept
 */
static void
lsock_listen_input(int fd, void *udata)
{
	struct lsock_port *p = udata;
	struct lsock_peer *peer;

	if ((peer = calloc(1, sizeof(*peer))) == NULL) {
		syslog(LOG_WARNING, "%s: peer malloc failed", p->name);
		(void)close(accept(fd, NULL, NULL));
		return;
	}

	peer->port = p;

	peer->input.stream = 1;
	peer->input.cred = (p->type == LOCP_DGRAM_PRIV ||
	    p->type == LOCP_STREAM_PRIV);
	peer->input.peerlen = sizeof(peer->peer);
	peer->input.peer = (struct sockaddr *)&peer->peer;

	peer->input.fd = accept(fd, peer->input.peer, &peer->input.peerlen);
	if (peer->input.fd == -1) {
		syslog(LOG_WARNING, "%s: accept failed: %m", p->name);
		free(peer);
		return;
	}

	if ((peer->input.id = fd_select(peer->input.fd, lsock_input,
	    peer, NULL)) == NULL) {
		close(peer->input.fd);
		free(peer);
		return;
	}

	LIST_INSERT_HEAD(&p->peers, peer, link);
}

/*
 * Create a local socket
 */
static int
lsock_init_port(struct tport *tp)
{
	struct lsock_port *p = (struct lsock_port *)tp;
	struct sockaddr_un sa;

	if (p->type == LOCP_STREAM_PRIV || p->type == LOCP_STREAM_UNPRIV) {
		if ((p->str_sock = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0) {
			syslog(LOG_ERR, "creating local socket: %m");
			return (SNMP_ERR_RES_UNAVAIL);
		}

		strlcpy(sa.sun_path, p->name, sizeof(sa.sun_path));
		sa.sun_family = AF_LOCAL;
		sa.sun_len = SUN_LEN(&sa);

		(void)remove(p->name);

		if (bind(p->str_sock, (struct sockaddr *)&sa, sizeof(sa))) {
			if (errno == EADDRNOTAVAIL) {
				close(p->str_sock);
				p->str_sock = -1;
				return (SNMP_ERR_INCONS_NAME);
			}
			syslog(LOG_ERR, "bind: %s %m", p->name);
			close(p->str_sock);
			p->str_sock = -1;
			return (SNMP_ERR_GENERR);
		}
		if (chmod(p->name, 0666) == -1)
			syslog(LOG_WARNING, "chmod(%s,0666): %m", p->name);

		if (listen(p->str_sock, 10) == -1) {
			syslog(LOG_ERR, "listen: %s %m", p->name);
			(void)remove(p->name);
			close(p->str_sock);
			p->str_sock = -1;
			return (SNMP_ERR_GENERR);
		}

		p->str_id = fd_select(p->str_sock, lsock_listen_input, p, NULL);
		if (p->str_id == NULL) {
			(void)remove(p->name);
			close(p->str_sock);
			p->str_sock = -1;
			return (SNMP_ERR_GENERR);
		}
	} else {
		struct lsock_peer *peer;
		const int on = 1;

		peer = LIST_FIRST(&p->peers);

		if ((peer->input.fd = socket(PF_LOCAL, SOCK_DGRAM, 0)) < 0) {
			syslog(LOG_ERR, "creating local socket: %m");
			return (SNMP_ERR_RES_UNAVAIL);
		}

		if (setsockopt(peer->input.fd, 0, LOCAL_CREDS, &on,
		    sizeof(on)) == -1) {
			syslog(LOG_ERR, "setsockopt(LOCAL_CREDS): %m");
			close(peer->input.fd);
			peer->input.fd = -1;
			return (SNMP_ERR_GENERR);
		}

		strlcpy(sa.sun_path, p->name, sizeof(sa.sun_path));
		sa.sun_family = AF_LOCAL;
		sa.sun_len = SUN_LEN(&sa);

		(void)remove(p->name);

		if (bind(peer->input.fd, (struct sockaddr *)&sa, sizeof(sa))) {
			if (errno == EADDRNOTAVAIL) {
				close(peer->input.fd);
				peer->input.fd = -1;
				return (SNMP_ERR_INCONS_NAME);
			}
			syslog(LOG_ERR, "bind: %s %m", p->name);
			close(peer->input.fd);
			peer->input.fd = -1;
			return (SNMP_ERR_GENERR);
		}
		if (chmod(p->name, 0666) == -1)
			syslog(LOG_WARNING, "chmod(%s,0666): %m", p->name);

		peer->input.id = fd_select(peer->input.fd, lsock_input,
		    peer, NULL);
		if (peer->input.id == NULL) {
			(void)remove(p->name);
			close(peer->input.fd);
			peer->input.fd = -1;
			return (SNMP_ERR_GENERR);
		}
	}
	return (SNMP_ERR_NOERROR);
}

/*
 * Send something
 */
static ssize_t
lsock_send(struct tport *tp, const u_char *buf, size_t len,
    const struct sockaddr *addr, size_t addrlen)
{
	struct lsock_port *p = (struct lsock_port *)tp;
	struct lsock_peer *peer;

	if (p->type == LOCP_DGRAM_PRIV || p->type == LOCP_DGRAM_UNPRIV) {
		peer = LIST_FIRST(&p->peers);

	} else {
		/* search for the peer */
		LIST_FOREACH(peer, &p->peers, link)
			if (peer->input.peerlen == addrlen &&
			    memcmp(peer->input.peer, addr, addrlen) == 0)
				break;
		if (peer == NULL) {
			errno = ENOTCONN;
			return (-1);
		}
	}

	return (sendto(peer->input.fd, buf, len, 0, addr, addrlen));
}

static void
check_priv_stream(struct port_input *pi)
{
	struct xucred ucred;
	socklen_t ucredlen;

	/* obtain the accept time credentials */
	ucredlen = sizeof(ucred);

	if (getsockopt(pi->fd, 0, LOCAL_PEERCRED, &ucred, &ucredlen) == 0 &&
	    ucredlen >= sizeof(ucred) && ucred.cr_version == XUCRED_VERSION)
		pi->priv = (ucred.cr_uid == 0);
	else
		pi->priv = 0;
}

/*
 * Receive something
 */
static ssize_t
lsock_recv(struct tport *tp __unused, struct port_input *pi)
{
	struct msghdr msg;
	struct iovec iov[1];
	ssize_t len;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	if (pi->buf == NULL) {
		/* no buffer yet - allocate one */
		if ((pi->buf = buf_alloc(0)) == NULL) {
			/* ups - could not get buffer. Return an error
			 * the caller must close the transport. */
			return (-1);
		}
		pi->buflen = buf_size(0);
		pi->consumed = 0;
		pi->length = 0;
	}

	/* try to get a message */
	msg.msg_name = pi->peer;
	msg.msg_namelen = pi->peerlen;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	iov[0].iov_base = pi->buf + pi->length;
	iov[0].iov_len = pi->buflen - pi->length;

	len = recvmsg(pi->fd, &msg, 0);

	if (len == -1 || len == 0)
		/* receive error */
		return (-1);

	pi->length += len;

	if (pi->cred)
		check_priv_stream(pi);

	return (0);
}

/*
 * Dependency to create a lsock port
 */
struct lsock_dep {
	struct snmp_dependency dep;

	/* index (path name) */
	u_char *path;
	size_t pathlen;

	/* the port */
	struct lsock_port *port;

	/* which of the fields are set */
	u_int set;

	/* type of the port */
	int type;

	/* status */
	int status;
};
#define	LD_TYPE		0x01
#define	LD_STATUS	0x02
#define	LD_CREATE	0x04	/* rollback create */
#define	LD_DELETE	0x08	/* rollback delete */

/*
 * dependency handler for lsock ports
 */
static int
lsock_func(struct snmp_context *ctx, struct snmp_dependency *dep,
    enum snmp_depop op)
{
	struct lsock_dep *ld = (struct lsock_dep *)(void *)dep;
	int err = SNMP_ERR_NOERROR;

	switch (op) {

	  case SNMP_DEPOP_COMMIT:
		if (!(ld->set & LD_STATUS))
			err = SNMP_ERR_BADVALUE;
		else if (ld->port == NULL) {
			if (!ld->status)
				err = SNMP_ERR_BADVALUE;

			else {
				/* create */
				err = lsock_open_port(ld->path, ld->pathlen,
				    &ld->port, ld->type);
				if (err == SNMP_ERR_NOERROR)
					ld->set |= LD_CREATE;
			}
		} else if (!ld->status) {
			/* delete - hard to roll back so defer to finalizer */
			ld->set |= LD_DELETE;
		} else
			/* modify - read-only */
			err = SNMP_ERR_READONLY;

		return (err);

	  case SNMP_DEPOP_ROLLBACK:
		if (ld->set & LD_CREATE) {
			/* was create */
			lsock_close_port(&ld->port->tport);
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_DEPOP_FINISH:
		if ((ld->set & LD_DELETE) && ctx->code == SNMP_RET_OK)
			lsock_close_port(&ld->port->tport);
		free(ld->path);
		return (SNMP_ERR_NOERROR);
	}
	abort();
}

/*
 * Local port table
 */
int
op_lsock_port(struct snmp_context *ctx, struct snmp_value *value,
    u_int sub, u_int iidx, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub-1];
	struct lsock_port *p;
	u_char *name;
	size_t namelen;
	struct lsock_dep *ld;
	struct asn_oid didx;

	switch (op) {

	  case SNMP_OP_GETNEXT:
		if ((p = (struct lsock_port *)trans_next_port(my_trans,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		index_append(&value->var, sub, &p->tport.index);
		break;

	  case SNMP_OP_GET:
		if ((p = (struct lsock_port *)trans_find_port(my_trans,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		p = (struct lsock_port *)trans_find_port(my_trans,
		    &value->var, sub);

		if (index_decode(&value->var, sub, iidx, &name, &namelen))
			return (SNMP_ERR_NO_CREATION);

		asn_slice_oid(&didx, &value->var, sub, value->var.len);
		if ((ld = (struct lsock_dep *)(void *)snmp_dep_lookup(ctx,
		    &oid_begemotSnmpdLocalPortTable, &didx, sizeof(*ld),
		    lsock_func)) == NULL) {
			free(name);
			return (SNMP_ERR_GENERR);
		}

		if (ld->path == NULL) {
			ld->path = name;
			ld->pathlen = namelen;
		} else {
			free(name);
		}
		ld->port = p;

		switch (which) {

		  case LEAF_begemotSnmpdLocalPortStatus:
			if (ld->set & LD_STATUS)
				return (SNMP_ERR_INCONS_VALUE);
			if (!TRUTH_OK(value->v.integer))
				return (SNMP_ERR_WRONG_VALUE);

			ld->status = TRUTH_GET(value->v.integer);
			ld->set |= LD_STATUS;
			break;

		  case LEAF_begemotSnmpdLocalPortType:
			if (ld->set & LD_TYPE)
				return (SNMP_ERR_INCONS_VALUE);
			if (value->v.integer < 1 || value->v.integer > 4)
				return (SNMP_ERR_WRONG_VALUE);

			ld->type = value->v.integer;
			ld->set |= LD_TYPE;
			break;
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_ROLLBACK:
	  case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);

	  default:
		abort();
	}

	/*
	 * Come here to fetch the value
	 */
	switch (which) {

	  case LEAF_begemotSnmpdLocalPortStatus:
		value->v.integer = 1;
		break;

	  case LEAF_begemotSnmpdLocalPortType:
		value->v.integer = p->type;
		break;

	  default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}
