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
 * $Begemot: bsnmp/snmpd/trans_udp.c,v 1.5 2005/10/04 08:46:56 brandt_h Exp $
 *
 * UDP transport
 */
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/ucred.h>

#include <stdbool.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include "snmpmod.h"
#include "snmpd.h"
#include "trans_udp.h"
#include "tree.h"
#include "oid.h"

static int udp_start(void);
static int udp_stop(int);
static void udp_close_port(struct tport *);
static int udp_init_port(struct tport *);
static ssize_t udp_send(struct tport *, const u_char *, size_t,
    const struct sockaddr *, size_t);
static ssize_t udp_recv(struct tport *, struct port_input *);

/* exported */
const struct transport_def udp_trans = {
	"udp",
	OIDX_begemotSnmpdTransUdp,
	udp_start,
	udp_stop,
	udp_close_port,
	udp_init_port,
	udp_send,
	udp_recv
};
static struct transport *my_trans;

static int
udp_start(void)
{
	return (trans_register(&udp_trans, &my_trans));
}

static int
udp_stop(int force __unused)
{
	if (my_trans != NULL)
		if (trans_unregister(my_trans) != 0)
			return (SNMP_ERR_GENERR);
	return (SNMP_ERR_NOERROR);
}

/*
 * A UDP port is ready
 */
static void
udp_input(int fd __unused, void *udata)
{
	struct udp_port *p = udata;

	p->input.peerlen = sizeof(p->ret);
	snmpd_input(&p->input, &p->tport);
}

/*
 * Create a UDP socket and bind it to the given port
 */
static int
udp_init_port(struct tport *tp)
{
	struct udp_port *p = (struct udp_port *)tp;
	struct sockaddr_in addr;
	u_int32_t ip;
	const int on = 1;

	if ((p->input.fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		syslog(LOG_ERR, "creating UDP socket: %m");
		return (SNMP_ERR_RES_UNAVAIL);
	}
	ip = (p->addr[0] << 24) | (p->addr[1] << 16) | (p->addr[2] << 8) |
	    p->addr[3];
	memset(&addr, 0, sizeof(addr));
	addr.sin_addr.s_addr = htonl(ip);
	addr.sin_port = htons(p->port);
	addr.sin_family = AF_INET;
	addr.sin_len = sizeof(addr);
	if (addr.sin_addr.s_addr == INADDR_ANY) {
		if (setsockopt(p->input.fd, IPPROTO_IP, IP_RECVDSTADDR, &on,
		    sizeof(on)) == -1) {
			syslog(LOG_ERR, "setsockopt(IP_RECVDSTADDR): %m");
			close(p->input.fd);
			p->input.fd = -1;
			return (SNMP_ERR_GENERR);
		}
		p->recvdstaddr = true;
	}
	if (bind(p->input.fd, (struct sockaddr *)&addr, sizeof(addr))) {
		if (errno == EADDRNOTAVAIL) {
			close(p->input.fd);
			p->input.fd = -1;
			return (SNMP_ERR_INCONS_NAME);
		}
		syslog(LOG_ERR, "bind: %s:%u %m", inet_ntoa(addr.sin_addr),
		    p->port);
		close(p->input.fd);
		p->input.fd = -1;
		return (SNMP_ERR_GENERR);
	}
	if ((p->input.id = fd_select(p->input.fd, udp_input,
	    p, NULL)) == NULL) {
		close(p->input.fd);
		p->input.fd = -1;
		return (SNMP_ERR_GENERR);
	}
	return (SNMP_ERR_NOERROR);
}

/*
 * Create a new SNMP Port object and start it, if we are not
 * in initialization mode. The arguments are in host byte order.
 */
static int
udp_open_port(u_int8_t *addr, u_int32_t udp_port, struct udp_port **pp)
{
	struct udp_port *port;
	int err;

	if (udp_port > 0xffff)
		return (SNMP_ERR_NO_CREATION);
	if ((port = malloc(sizeof(*port))) == NULL)
		return (SNMP_ERR_GENERR);
	memset(port, 0, sizeof(*port));

	/* initialize common part */
	port->tport.index.len = 5;
	port->tport.index.subs[0] = addr[0];
	port->tport.index.subs[1] = addr[1];
	port->tport.index.subs[2] = addr[2];
	port->tport.index.subs[3] = addr[3];
	port->tport.index.subs[4] = udp_port;

	port->addr[0] = addr[0];
	port->addr[1] = addr[1];
	port->addr[2] = addr[2];
	port->addr[3] = addr[3];
	port->port = udp_port;

	port->input.fd = -1;
	port->input.id = NULL;
	port->input.stream = 0;
	port->input.cred = 0;
	port->input.peer = (struct sockaddr *)&port->ret;
	port->input.peerlen = sizeof(port->ret);

	trans_insert_port(my_trans, &port->tport);

	if (community != COMM_INITIALIZE &&
	    (err = udp_init_port(&port->tport)) != SNMP_ERR_NOERROR) {
		udp_close_port(&port->tport);
		return (err);
	}
	*pp = port;
	return (SNMP_ERR_NOERROR);
}

/*
 * Close an SNMP port
 */
static void
udp_close_port(struct tport *tp)
{
	struct udp_port *port = (struct udp_port *)tp;

	snmpd_input_close(&port->input);
	trans_remove_port(tp);
	free(port);
}

/*
 * Send something
 */
static ssize_t
udp_send(struct tport *tp, const u_char *buf, size_t len,
    const struct sockaddr *addr, size_t addrlen)
{
	struct udp_port *p = (struct udp_port *)tp;
	struct cmsghdr *cmsg;
	struct msghdr msg;
	char cbuf[CMSG_SPACE(sizeof(struct in_addr))];
	struct iovec iov;

	iov.iov_base = __DECONST(void*, buf);
	iov.iov_len = len;

	msg.msg_flags = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = __DECONST(void *, addr);
	msg.msg_namelen = addrlen;

	if (p->recvdstaddr) {
		msg.msg_control = cbuf;
		msg.msg_controllen = sizeof(cbuf);

		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level = IPPROTO_IP;
		cmsg->cmsg_type = IP_SENDSRCADDR;
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_addr));
		memcpy(CMSG_DATA(cmsg), &p->dstaddr, sizeof(struct in_addr));
	} else {
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
	}

	return (sendmsg(p->input.fd, &msg, 0));
}

static void
check_priv_dgram(struct port_input *pi, struct sockcred *cred)
{

	/* process explicitly sends credentials */
	if (cred)
		pi->priv = (cred->sc_euid == 0);
	else
		pi->priv = 0;
}

/*
 * Input from a datagram socket.
 * Each receive should return one datagram.
 */
static ssize_t
udp_recv(struct tport *tp, struct port_input *pi)
{
	u_char embuf[1000];
	char cbuf[CMSG_SPACE(SOCKCREDSIZE(CMGROUP_MAX)) +
	    CMSG_SPACE(sizeof(struct in_addr))];
	struct udp_port *p = (struct udp_port *)tp;
	struct msghdr msg;
	struct iovec iov[1];
	ssize_t len;
	struct cmsghdr *cmsg;
	struct sockcred *cred = NULL;

	if (pi->buf == NULL) {
		/* no buffer yet - allocate one */
		if ((pi->buf = buf_alloc(0)) == NULL) {
			/* ups - could not get buffer. Read away input
			 * and drop it */
			(void)recvfrom(pi->fd, embuf, sizeof(embuf),
			    0, NULL, NULL);
			/* return error */
			return (-1);
		}
		pi->buflen = buf_size(0);
	}

	/* try to get a message */
	msg.msg_name = pi->peer;
	msg.msg_namelen = pi->peerlen;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	memset(cbuf, 0, sizeof(cbuf));
	msg.msg_control = cbuf;
	msg.msg_controllen = sizeof(cbuf);
	msg.msg_flags = 0;

	iov[0].iov_base = pi->buf;
	iov[0].iov_len = pi->buflen;

	len = recvmsg(pi->fd, &msg, 0);

	if (len == -1 || len == 0)
		/* receive error */
		return (-1);

	if (msg.msg_flags & MSG_TRUNC) {
		/* truncated - drop */
		snmpd_stats.silentDrops++;
		snmpd_stats.inTooLong++;
		return (-1);
	}

	pi->length = (size_t)len;

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == IPPROTO_IP &&
		    cmsg->cmsg_type == IP_RECVDSTADDR)
			memcpy(&p->dstaddr, CMSG_DATA(cmsg),
			    sizeof(struct in_addr));
		if (cmsg->cmsg_level == SOL_SOCKET &&
		    cmsg->cmsg_type == SCM_CREDS)
			cred = (struct sockcred *)(void *)CMSG_DATA(cmsg);
	}

	if (pi->cred)
		check_priv_dgram(pi, cred);

	return (0);
}

/*
 * Port table
 */
int
op_snmp_port(struct snmp_context *ctx, struct snmp_value *value,
    u_int sub, u_int iidx, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub-1];
	struct udp_port *p;
	u_int8_t addr[4];
	u_int32_t port;

	switch (op) {

	  case SNMP_OP_GETNEXT:
		if ((p = (struct udp_port *)trans_next_port(my_trans,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		index_append(&value->var, sub, &p->tport.index);
		break;

	  case SNMP_OP_GET:
		if ((p = (struct udp_port *)trans_find_port(my_trans,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		p = (struct udp_port *)trans_find_port(my_trans,
		    &value->var, sub);
		ctx->scratch->int1 = (p != NULL);

		if (which != LEAF_begemotSnmpdPortStatus)
			abort();
		if (!TRUTH_OK(value->v.integer))
			return (SNMP_ERR_WRONG_VALUE);

		ctx->scratch->int2 = TRUTH_GET(value->v.integer);

		if (ctx->scratch->int2) {
			/* open an SNMP port */
			if (p != NULL)
				/* already open - do nothing */
				return (SNMP_ERR_NOERROR);

			if (index_decode(&value->var, sub, iidx, addr, &port))
				return (SNMP_ERR_NO_CREATION);
			return (udp_open_port(addr, port, &p));

		} else {
			/* close SNMP port - do in commit */
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_ROLLBACK:
		p = (struct udp_port *)trans_find_port(my_trans,
		    &value->var, sub);
		if (ctx->scratch->int1 == 0) {
			/* did not exist */
			if (ctx->scratch->int2 == 1) {
				/* created */
				if (p != NULL)
					udp_close_port(&p->tport);
			}
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_COMMIT:
		p = (struct udp_port *)trans_find_port(my_trans,
		    &value->var, sub);
		if (ctx->scratch->int1 == 1) {
			/* did exist */
			if (ctx->scratch->int2 == 0) {
				/* delete */
				if (p != NULL)
					udp_close_port(&p->tport);
			}
		}
		return (SNMP_ERR_NOERROR);

	  default:
		abort();
	}

	/*
	 * Come here to fetch the value
	 */
	switch (which) {

	  case LEAF_begemotSnmpdPortStatus:
		value->v.integer = 1;
		break;

	  default:
		abort();
	}

	return (SNMP_ERR_NOERROR);
}
