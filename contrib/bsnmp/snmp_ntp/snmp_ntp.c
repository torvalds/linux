/*
 * Copyright (c) 2005,2018
 *	Hartmut Brandt.
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE AND DOCUMENTATION IS PROVIDED BY FRAUNHOFER FOKUS
 * AND ITS CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * FRAUNHOFER FOKUS OR ITS CONTRIBUTORS  BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Begemot: bsnmp/snmp_ntp/snmp_ntp.c,v 1.9 2005/10/06 07:15:01 brandt_h Exp $
 *
 * NTP interface for SNMPd.
 */

#include <sys/queue.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#elif defined(HAVE_INTTYPES_H)
#include <inttypes.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "support.h"
#include "snmpmod.h"

#define	SNMPTREE_TYPES
#include "ntp_tree.h"
#include "ntp_oid.h"

#define	NTPC_MAX	576
#define	NTPC_VERSION	3
#define	NTPC_MODE	6
#define	NTPC_DMAX	468

#define	NTPC_BIT_RESP	0x80
#define	NTPC_BIT_ERROR	0x40
#define	NTPC_BIT_MORE	0x20

#define	NTPC_OPMASK	0x1f
#define	NTPC_OP_READSTAT	1
#define	NTPC_OP_READVAR		2

/* our module handle */
static struct lmodule *module;

/* debug flag */
static uint32_t ntp_debug;
#define DBG_DUMP_PKTS	0x01
#define	DBG_DUMP_VARS	0x02

/* OIDs */
static const struct asn_oid oid_ntpMIB = OIDX_ntpMIB;

/* the Object Resource registration index */
static u_int reg_index;

/* last time we've fetch the system variables */
static uint64_t sysinfo_tick;

/* cached system variables */
static int32_t	sys_leap;
static int	sysb_leap;
static int32_t	sys_stratum;
static int	sysb_stratum;
static int32_t	sys_precision;
static int	sysb_precision;
static char	*sys_rootdelay;
static char	*sys_rootdispersion;
static char	*sys_refid;
static char	sys_reftime[8];
static int	sysb_reftime;
static int32_t	sys_poll;
static int	sysb_poll;
static uint32_t	sys_peer;
static int	sysb_peer;
static u_char	sys_clock[8];
static int	sysb_clock;
static char	*sys_system;
static char	*sys_processor;
static int	sysb_jitter;
static double	sys_jitter;
static int	sysb_stability;
static double	sys_stability;

/* last time we've fetch the peer list */
static uint64_t peers_tick;

/* request sequence number generator */
static uint16_t	seqno;

/* NTPD socket */
static int ntpd_sock;
static void *ntpd_fd;

struct peer {
	/* required entries for macros */
	uint32_t	index;
	TAILQ_ENTRY(peer) link;

	int32_t		config;		/* config bit */
	u_char		srcadr[4];	/* PeerAddress */
	uint32_t	srcport;	/* PeerPort */
	u_char		dstadr[4];	/* HostAddress */
	uint32_t	dstport;	/* HostPort */
	int32_t		leap;		/* Leap */
	int32_t		hmode;		/* Mode */
	int32_t		stratum;	/* Stratum */
	int32_t		ppoll;		/* PeerPoll */
	int32_t		hpoll;		/* HostPoll */
	int32_t		precision;	/* Precision */
	char		*rootdelay;	/* RootDelay */
	char		*rootdispersion;/* RootDispersion */
	char		*refid;		/* RefId */
	u_char		reftime[8];	/* RefTime */
	u_char		orgtime[8];	/* OrgTime */
	u_char		rcvtime[8];	/* ReceiveTime */
	u_char		xmttime[8];	/* TransmitTime */
	u_int32_t	reach;		/* Reach */
	int32_t		timer;		/* Timer */
	char		*offset;	/* Offset */
	char		*delay;		/* Delay */
	char		*dispersion;	/* Dispersion */
	int32_t		filt_entries;
};
TAILQ_HEAD(peer_list, peer);

/* list of peers */
static struct peer_list peers = TAILQ_HEAD_INITIALIZER(peers);

struct filt {
	/* required fields */
	struct asn_oid	index;
	TAILQ_ENTRY(filt) link;

	char		*offset;
	char		*delay;
	char		*dispersion;
};
TAILQ_HEAD(filt_list, filt);

/* list of filters */
static struct filt_list filts = TAILQ_HEAD_INITIALIZER(filts);

/* configuration */
static u_char *ntp_host;
static u_char *ntp_port;
static uint32_t ntp_timeout;

static void ntpd_input(int, void *);
static int open_socket(void);

/* the initialization function */
static int
ntp_init(struct lmodule *mod, int argc, char *argv[] __unused)
{

	module = mod;

	if (argc != 0) {
		syslog(LOG_ERR, "bad number of arguments for %s", __func__);
		return (EINVAL);
	}

	ntp_host = strdup("localhost");
	ntp_port = strdup("ntp");
	ntp_timeout = 50;		/* 0.5sec */

	return (0);
}

/*
 * Module is started
 */
static void
ntp_start(void)
{

	if (open_socket() != -1) {
		ntpd_fd = fd_select(ntpd_sock, ntpd_input, NULL, module);
		if (ntpd_fd == NULL) {
			syslog(LOG_ERR, "fd_select failed on ntpd socket: %m");
			return;
		}
	}
	reg_index = or_register(&oid_ntpMIB, "The MIB for NTP.", module);
}

/*
 * Called, when the module is to be unloaded after it was successfully loaded
 */
static int
ntp_fini(void)
{

	or_unregister(reg_index);
	fd_deselect(ntpd_fd);

	return (0);
}

const struct snmp_module config = {
	.comment =	"This module implements the NTP MIB",
	.init =		ntp_init,
	.start =	ntp_start,
	.fini =		ntp_fini,
	.tree =		ntp_ctree,
	.tree_size =	ntp_CTREE_SIZE,
};

/*
 * Open the NTPD socket
 */
static int
open_socket(void)
{
	struct addrinfo hints, *res, *res0;
	int	error;
	const char *cause;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	error = getaddrinfo(ntp_host, ntp_port, &hints, &res0);
	if (error) {
		syslog(LOG_ERR, "%s(%s): %s", ntp_host, ntp_port,
		    gai_strerror(error));
		return (-1);
	}

	ntpd_sock = -1;
	cause = "no address";
	errno = EADDRNOTAVAIL;
	for (res = res0; res != NULL; res = res->ai_next) {
		ntpd_sock = socket(res->ai_family, res->ai_socktype,
		    res->ai_protocol);
		if (ntpd_sock == -1) {
			cause = "socket";
			continue;
		}
		if (connect(ntpd_sock, res->ai_addr, res->ai_addrlen) == -1) {
			cause = "connect";
			(void)close(ntpd_sock);
			ntpd_sock = -1;
			continue;
		}
		break;
	}
	if (ntpd_sock == -1) {
		syslog(LOG_ERR, "%s: %m", cause);
		return (-1);
	}
	freeaddrinfo(res0);
	return (0);
}

/*
 * Dump a packet
 */
static void
dump_packet(const u_char *pkt, size_t ret)
{
	char buf[8 * 3 + 1];
	size_t i, j;

	for (i = 0; i < ret; i += 8) {
		buf[0] = '\0';
		for (j = 0; i + j < (size_t)ret && j < 8; j++)
			sprintf(buf + strlen(buf), " %02x", pkt[i + j]);
		syslog(LOG_INFO, "%04zu:%s", i, buf);
	}
}

/*
 * Execute an NTP request.
 */
static int
ntpd_request(u_int op, u_int associd, const char *vars)
{
	u_char	*rpkt;
	u_char	*ptr;
	size_t	vlen;
	ssize_t	ret;

	if ((rpkt = malloc(NTPC_MAX)) == NULL) {
		syslog(LOG_ERR, "%m");
		return (-1);
	}
	memset(rpkt, 0, NTPC_MAX);

	ptr = rpkt;
	*ptr++ = (NTPC_VERSION << 3) | NTPC_MODE;
	*ptr++ = op;

	if (++seqno == 0)
		seqno++;
	*ptr++ = seqno >> 8;
	*ptr++ = seqno;

	/* skip status */
	ptr += 2;

	*ptr++ = associd >> 8;
	*ptr++ = associd;

	/* skip offset */
	ptr += 2;

	if (vars != NULL) {
		vlen = strlen(vars);
		if (vlen > NTPC_DMAX) {
			syslog(LOG_ERR, "NTP request too long (%zu)", vlen);
			free(rpkt);
			return (-1);
		}
		*ptr++ = vlen >> 8;
		*ptr++ = vlen;

		memcpy(ptr, vars, vlen);
		ptr += vlen;
	} else
		/* skip data length (is already zero) */
		ptr += 2;

	while ((ptr - rpkt) % 4 != 0)
		*ptr++ = 0;

	if (ntp_debug & DBG_DUMP_PKTS) {
		syslog(LOG_INFO, "sending %zd bytes", ptr - rpkt);
		dump_packet(rpkt, ptr - rpkt);
	}

	ret = send(ntpd_sock, rpkt, ptr - rpkt, 0);
	if (ret == -1) {
		syslog(LOG_ERR, "cannot send to ntpd: %m");
		free(rpkt);
		return (-1);
	}
	return (0);
}

/*
 * Callback if packet arrived from NTPD
 */
static int
ntpd_read(uint16_t *op, uint16_t *associd, u_char **data, size_t *datalen)
{
	u_char	pkt[NTPC_MAX + 1];
	u_char	*ptr, *nptr;
	u_int	n;
	ssize_t	ret;
	size_t	z;
	u_int	offset;		/* current offset */
	int	more;		/* more flag */
	int	sel;
	struct timeval inc, end, rem;
	fd_set	iset;

	*datalen = 0;
	*data = NULL;
	offset = 0;

	inc.tv_sec = ntp_timeout / 100;
	inc.tv_usec = (ntp_timeout % 100) * 1000;

	(void)gettimeofday(&end, NULL);
	timeradd(&end, &inc, &end);

  next:
	/* compute remaining time */
	(void)gettimeofday(&rem, NULL);
	if (timercmp(&rem, &end, >=)) {
		/* do a poll */
		rem.tv_sec = 0;
		rem.tv_usec = 0;
	} else {
		timersub(&end, &rem, &rem);
	}

	/* select */
	FD_ZERO(&iset);
	FD_SET(ntpd_sock, &iset);
	sel = select(ntpd_sock + 1, &iset, NULL, NULL, &rem);
	if (sel == -1) {
		if (errno == EINTR)
			goto next;
		syslog(LOG_ERR, "select ntpd_sock: %m");
		free(*data);
		return (-1);
	}
	if (sel == 0) {
		syslog(LOG_ERR, "timeout on NTP connection");
		free(*data);
		return (-1);
	}

	/* now read it */
	ret = recv(ntpd_sock, pkt, sizeof(pkt), 0);
	if (ret == -1) {
		syslog(LOG_ERR, "error reading from ntpd: %m");
		free(*data);
		return (-1);
	}

	if (ntp_debug & DBG_DUMP_PKTS) {
		syslog(LOG_INFO, "got %zd bytes", ret);
		dump_packet(pkt, (size_t)ret);
	}

	ptr = pkt;
	if ((*ptr & 0x3f) != ((NTPC_VERSION << 3) | NTPC_MODE)) {
		syslog(LOG_ERR, "unexpected packet version 0x%x", *ptr);
		free(*data);
		return (-1);
	}
	ptr++;

	if (!(*ptr & NTPC_BIT_RESP)) {
		syslog(LOG_ERR, "not a response packet");
		return (-1);
	}
	if (*ptr & NTPC_BIT_ERROR) {
		z = *datalen - 12;
		if (z > NTPC_DMAX)
			z = NTPC_DMAX;
		syslog(LOG_ERR, "error response: %.*s", (int)z, pkt + 12);
		free(*data);
		return (-1);
	}
	more = (*ptr & NTPC_BIT_MORE);

	*op = *ptr++ & NTPC_OPMASK;

	/* seqno */
	n = *ptr++ << 8;
	n |= *ptr++;

	if (n != seqno) {
		syslog(LOG_ERR, "expecting seqno %u, got %u", seqno, n);
		free(*data);
		return (-1);
	}

	/* status */
	n = *ptr++ << 8;
	n |= *ptr++;

	/* associd */
	*associd = *ptr++ << 8;
	*associd |= *ptr++;

	/* offset */
	n = *ptr++ << 8;
	n |= *ptr++;

	if (n != offset) {
		syslog(LOG_ERR, "offset: expecting %u, got %u", offset, n);
		free(*data);
		return (-1);
	}

	/* count */
	n = *ptr++ << 8;
	n |= *ptr++;

	if ((size_t)ret < 12 + n) {
		syslog(LOG_ERR, "packet too short");
		return (-1);
	}

	nptr = realloc(*data, *datalen + n);
	if (nptr == NULL) {
		syslog(LOG_ERR, "cannot allocate memory: %m");
		free(*data);
		return (-1);
	}
	*data = nptr;

	memcpy(*data + offset, ptr, n);
	*datalen += n;

	if (!more)
		return (0);

	offset += n;
	goto next;
}

/*
 * Send a request and wait for the response
 */
static int
ntpd_dialog(u_int op, u_int associd, const char *vars, u_char **data,
    size_t *datalen)
{
	uint16_t rassocid;
	uint16_t rop;

	if (ntpd_request(op, associd, vars) == -1)
		return (-1);
	if (ntpd_read(&rop, &rassocid, data, datalen) == -1)
		return (-1);

	if (rop != op) {
		syslog(LOG_ERR, "bad response op 0x%x", rop);
		free(data);
		return (-1);
	}

	if (associd != rassocid) {
		syslog(LOG_ERR, "response for wrong associd");
		free(data);
		return (-1);
	}
	return (0);
}

/*
 * Callback if packet arrived from NTPD
 */
static void
ntpd_input(int fd __unused, void *arg __unused)
{
	uint16_t associd;
	uint16_t op;
	u_char	*data;
	size_t	datalen;

	if (ntpd_read(&op, &associd, &data, &datalen) == -1)
		return;

	free(data);
}

/*
 * Find the value of a variable
 */
static int
ntpd_parse(u_char **data, size_t *datalen, char **namep, char **valp)
{
	u_char *ptr = *data;
	u_char *end = ptr + *datalen;
	char *ptr1;
	char endc;

	/* skip leading spaces */
	while (ptr < end && isspace((int)*ptr))
		ptr++;

	if (ptr == end)
		return (0);

	*namep = ptr;

	/* skip to space or '=' or ','*/
	while (ptr < end && !isspace((int)*ptr) && *ptr != '=' && *ptr != ',')
		ptr++;
	endc = *ptr;
	*ptr++ = '\0';

	/* skip space */
	while (ptr < end && isspace((int)*ptr))
		ptr++;

	if (ptr == end || endc == ',') {
		/* no value */
		*valp = NULL;
		*datalen -= ptr - *data;
		*data = ptr;
		return (1);
	}

	if (*ptr == '"') {
		/* quoted */
		ptr++;
		*valp = ptr;
		while (ptr < end && *ptr != '"')
			ptr++;
		if (ptr == end)
			return (0);

		*ptr++ = '\0';

		/* find comma */
		while (ptr < end && isspace((int)*ptr) && *ptr == ',')
			ptr++;
	} else {
		*valp = ptr;

		/* skip to end of value */
		while (ptr < end && *ptr != ',')
			ptr++;

		/* remove trailing blanks */
		for (ptr1 = ptr; ptr1 > *valp; ptr1--)
			if (!isspace((int)ptr1[-1]))
				break;
		*ptr1 = '\0';

		if (ptr < end)
			ptr++;
	}

	*datalen -= ptr - *data;
	*data = ptr;

	return (1);
}

/*
 * Parse an int32 value
 */
static int
val_parse_int32(const char *val, int32_t *p, int32_t min, int32_t max, int base)
{
	long n;
	char *end;

	errno = 0;
	n = strtol(val, &end, base);
	if (errno != 0 || *end != '\0')
		return (0);
	if (n < min || n > max)
		return (0);
	*p = (int32_t)n;
	return (1);
}

/*
 * Parse an uint32 value
 */
static int
val_parse_uint32(const char *val, uint32_t *p, uint32_t min, uint32_t max,
    int base)
{
	u_long n;
	char *end;

	errno = 0;
	n = strtoul(val, &end, base);
	if (errno != 0 || *end != '\0')
		return (0);
	if (n < min || n > max)
		return (0);
	*p = (uint32_t)n;
	return (1);
}

/*
 * Parse a double
 */
static int
val_parse_double(const char *val, double *p)
{
	char *end;

	errno = 0;
	*p = strtod(val, &end);
	if (errno != 0 || *end != '\0')
		return (0);
	return (1);
}

static int
val_parse_ts(const char *val, char *buf)
{
	int r, n;
	u_int i, f;

	if (strlen(val) > 2 && val[0] == '0' && val[1] == 'x') {
		/* hex format */
		r = sscanf(val + 2, "%x.%x%n", &i, &f, &n);
		if (r != 2 || (size_t)n != strlen(val + 2))
			return (0);
	} else {
		/* probably decimal */
		r = sscanf(val, "%d.%d%n", &i, &f, &n);
		if (r != 2 || (size_t)n != strlen(val))
			return (0);
	}
	buf[0] = i >> 24;
	buf[1] = i >> 16;
	buf[2] = i >>  8;
	buf[3] = i >>  0;
	buf[4] = f >> 24;
	buf[5] = f >> 16;
	buf[6] = f >>  8;
	buf[7] = f >>  0;
	return (1);
}

/*
 * Parse an IP address. This resolves non-numeric names.
 */
static int
val_parse_ip(const char *val, u_char ip[4])
{
	int r, n, error;
	struct addrinfo hints, *res0;
	struct sockaddr_in *sin_local;

	r = sscanf(val, "%hhd.%hhd.%hhd.%hhd%n",
	    &ip[0], &ip[1], &ip[2], &ip[3], &n);
	if (n == 4 && (size_t)n == strlen(val))
		return (0);

	memset(ip, 0, 4);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	error = getaddrinfo(val, NULL, &hints, &res0);
	if (error) {
		syslog(LOG_ERR, "%s: %s", val, gai_strerror(error));
		return (-1);
	}
	if (res0 == NULL) {
		syslog(LOG_ERR, "%s: no address", val);
		return (-1);
	}

	sin_local = (struct sockaddr_in *)(void *)res0->ai_addr;
	ip[3] = sin_local->sin_addr.s_addr >> 24;
	ip[2] = sin_local->sin_addr.s_addr >> 16;
	ip[1] = sin_local->sin_addr.s_addr >>  8;
	ip[0] = sin_local->sin_addr.s_addr >>  0;

	freeaddrinfo(res0);
	return (0);
}

/*
 * Fetch system info
 */
static int
fetch_sysinfo(void)
{
	u_char *data;
	u_char *ptr;
	size_t datalen;
	char *name;
	char *val;

	if (ntpd_dialog(NTPC_OP_READVAR, 0,
	    "leap,stratum,precision,rootdelay,rootdispersion,refid,reftime,"
	    "poll,peer,clock,system,processor,jitter,stability",
	    &data, &datalen))
		return (-1);

	/* clear info */
	sysb_leap = 0;
	sysb_stratum = 0;
	sysb_precision = 0;
	free(sys_rootdelay);
	sys_rootdelay = NULL;
	free(sys_rootdispersion);
	sys_rootdispersion = NULL;
	free(sys_refid);
	sys_refid = NULL;
	sysb_reftime = 0;
	sysb_poll = 0;
	sysb_peer = 0;
	sysb_clock = 0;
	free(sys_system);
	sys_system = NULL;
	free(sys_processor);
	sys_processor = NULL;
	sysb_jitter = 0;
	sysb_stability = 0;

	ptr = data;
	while (ntpd_parse(&ptr, &datalen, &name, &val)) {
		if (ntp_debug & DBG_DUMP_VARS)
			syslog(LOG_DEBUG, "%s: '%s'='%s'", __func__, name, val);
		if (strcmp(name, "leap") == 0 ||
		    strcmp(name, "sys.leap") == 0) {
			sysb_leap = val_parse_int32(val, &sys_leap,
			    0, 3, 2);

		} else if (strcmp(name, "stratum") == 0 ||
		    strcmp(name, "sys.stratum") == 0) {
			sysb_stratum = val_parse_int32(val, &sys_stratum,
			    0, 255, 0);

		} else if (strcmp(name, "precision") == 0 ||
		    strcmp(name, "sys.precision") == 0) {
			sysb_precision = val_parse_int32(val, &sys_precision,
			    INT32_MIN, INT32_MAX, 0);

		} else if (strcmp(name, "rootdelay") == 0 ||
		    strcmp(name, "sys.rootdelay") == 0) {
			sys_rootdelay = strdup(val);

		} else if (strcmp(name, "rootdispersion") == 0 ||
		    strcmp(name, "sys.rootdispersion") == 0) {
			sys_rootdispersion = strdup(val);

		} else if (strcmp(name, "refid") == 0 ||
		    strcmp(name, "sys.refid") == 0) {
			sys_refid = strdup(val);

		} else if (strcmp(name, "reftime") == 0 ||
		    strcmp(name, "sys.reftime") == 0) {
			sysb_reftime = val_parse_ts(val, sys_reftime);

		} else if (strcmp(name, "poll") == 0 ||
		    strcmp(name, "sys.poll") == 0) {
			sysb_poll = val_parse_int32(val, &sys_poll,
			    INT32_MIN, INT32_MAX, 0);

		} else if (strcmp(name, "peer") == 0 ||
		    strcmp(name, "sys.peer") == 0) {
			sysb_peer = val_parse_uint32(val, &sys_peer,
			    0, UINT32_MAX, 0);

		} else if (strcmp(name, "clock") == 0 ||
		    strcmp(name, "sys.clock") == 0) {
			sysb_clock = val_parse_ts(val, sys_clock);

		} else if (strcmp(name, "system") == 0 ||
		    strcmp(name, "sys.system") == 0) {
			sys_system = strdup(val);

		} else if (strcmp(name, "processor") == 0 ||
		    strcmp(name, "sys.processor") == 0) {
			sys_processor = strdup(val);

		} else if (strcmp(name, "jitter") == 0 ||
		    strcmp(name, "sys.jitter") == 0) {
			sysb_jitter = val_parse_double(val, &sys_jitter);

		} else if (strcmp(name, "stability") == 0 ||
		    strcmp(name, "sys.stability") == 0) {
			sysb_stability = val_parse_double(val, &sys_stability);
		}
	}

	free(data);
	return (0);
}

static int
parse_filt(char *val, uint16_t associd, int which)
{
	char *w;
	int cnt;
	struct filt *f;

	cnt = 0;
	for (w = strtok(val, " \t"); w != NULL; w = strtok(NULL, " \t")) {
		TAILQ_FOREACH(f, &filts, link)
			if (f->index.subs[0] == associd &&
			    f->index.subs[1] == (asn_subid_t)(cnt + 1))
				break;
		if (f == NULL) {
			f = malloc(sizeof(*f));
			memset(f, 0, sizeof(*f));
			f->index.len = 2;
			f->index.subs[0] = associd;
			f->index.subs[1] = cnt + 1;

			INSERT_OBJECT_OID(f, &filts);
		}

		switch (which) {

		  case 0:
			f->offset = strdup(w);
			break;

		  case 1:
			f->delay = strdup(w);
			break;

		  case 2:
			f->dispersion = strdup(w);
			break;

		  default:
			abort();
		}
		cnt++;
	}
	return (cnt);
}

/*
 * Fetch the complete peer list
 */
static int
fetch_peers(void)
{
	u_char *data, *pdata, *ptr;
	size_t datalen, pdatalen;
	int i;
	struct peer *p;
	struct filt *f;
	uint16_t associd;
	char *name, *val;

	/* free the old list */
	while ((p = TAILQ_FIRST(&peers)) != NULL) {
		TAILQ_REMOVE(&peers, p, link);
		free(p->rootdelay);
		free(p->rootdispersion);
		free(p->refid);
		free(p->offset);
		free(p->delay);
		free(p->dispersion);
		free(p);
	}
	while ((f = TAILQ_FIRST(&filts)) != NULL) {
		TAILQ_REMOVE(&filts, f, link);
		free(f->offset);
		free(f->delay);
		free(f->dispersion);
		free(f);
	}

	/* fetch the list of associations */
	if (ntpd_dialog(NTPC_OP_READSTAT, 0, NULL, &data, &datalen))
		return (-1);

	for (i = 0; i < (int)(datalen / 4); i++) {
		associd  = data[4 * i + 0] << 8;
		associd |= data[4 * i + 1] << 0;

		/* ask for the association variables */
		if (ntpd_dialog(NTPC_OP_READVAR, associd,
		    "config,srcadr,srcport,dstadr,dstport,leap,hmode,stratum,"
		    "hpoll,ppoll,precision,rootdelay,rootdispersion,refid,"
		    "reftime,org,rec,xmt,reach,timer,offset,delay,dispersion,"
		    "filtdelay,filtoffset,filtdisp",
		    &pdata, &pdatalen)) {
			free(data);
			return (-1);
		}

		/* now save and parse the data */
		p = malloc(sizeof(*p));
		if (p == NULL) {
			free(data);
			syslog(LOG_ERR, "%m");
			return (-1);
		}
		memset(p, 0, sizeof(*p));
		p->index = associd;
		INSERT_OBJECT_INT(p, &peers);

		ptr = pdata;
		while (ntpd_parse(&ptr, &pdatalen, &name, &val)) {
			if (ntp_debug & DBG_DUMP_VARS)
				syslog(LOG_DEBUG, "%s: '%s'='%s'",
				    __func__, name, val);
			if (strcmp(name, "config") == 0 ||
			    strcmp(name, "peer.config") == 0) {
				val_parse_int32(val, &p->config, 0, 1, 0);

			} else if (strcmp(name, "srcadr") == 0 ||
			    strcmp(name, "peer.srcadr") == 0) {
				val_parse_ip(val, p->srcadr);

			} else if (strcmp(name, "srcport") == 0 ||
			    strcmp(name, "peer.srcport") == 0) {
				val_parse_uint32(val, &p->srcport,
				    1, 65535, 0);

			} else if (strcmp(name, "dstadr") == 0 ||
			    strcmp(name, "peer.dstadr") == 0) {
				val_parse_ip(val, p->dstadr);

			} else if (strcmp(name, "dstport") == 0 ||
			    strcmp(name, "peer.dstport") == 0) {
				val_parse_uint32(val, &p->dstport,
				    1, 65535, 0);

			} else if (strcmp(name, "leap") == 0 ||
			    strcmp(name, "peer.leap") == 0) {
				val_parse_int32(val, &p->leap, 0, 3, 2);

			} else if (strcmp(name, "hmode") == 0 ||
			    strcmp(name, "peer.hmode") == 0) {
				val_parse_int32(val, &p->hmode, 0, 7, 0);

			} else if (strcmp(name, "stratum") == 0 ||
			    strcmp(name, "peer.stratum") == 0) {
				val_parse_int32(val, &p->stratum, 0, 255, 0);

			} else if (strcmp(name, "ppoll") == 0 ||
			    strcmp(name, "peer.ppoll") == 0) {
				val_parse_int32(val, &p->ppoll,
				    INT32_MIN, INT32_MAX, 0);

			} else if (strcmp(name, "hpoll") == 0 ||
			    strcmp(name, "peer.hpoll") == 0) {
				val_parse_int32(val, &p->hpoll,
				    INT32_MIN, INT32_MAX, 0);

			} else if (strcmp(name, "precision") == 0 ||
			    strcmp(name, "peer.precision") == 0) {
				val_parse_int32(val, &p->hpoll,
				    INT32_MIN, INT32_MAX, 0);

			} else if (strcmp(name, "rootdelay") == 0 ||
			    strcmp(name, "peer.rootdelay") == 0) {
				p->rootdelay = strdup(val);

			} else if (strcmp(name, "rootdispersion") == 0 ||
			    strcmp(name, "peer.rootdispersion") == 0) {
				p->rootdispersion = strdup(val);

			} else if (strcmp(name, "refid") == 0 ||
			    strcmp(name, "peer.refid") == 0) {
				p->refid = strdup(val);

			} else if (strcmp(name, "reftime") == 0 ||
			    strcmp(name, "sys.reftime") == 0) {
				val_parse_ts(val, p->reftime);

			} else if (strcmp(name, "org") == 0 ||
			    strcmp(name, "sys.org") == 0) {
				val_parse_ts(val, p->orgtime);

			} else if (strcmp(name, "rec") == 0 ||
			    strcmp(name, "sys.rec") == 0) {
				val_parse_ts(val, p->rcvtime);

			} else if (strcmp(name, "xmt") == 0 ||
			    strcmp(name, "sys.xmt") == 0) {
				val_parse_ts(val, p->xmttime);

			} else if (strcmp(name, "reach") == 0 ||
			    strcmp(name, "peer.reach") == 0) {
				val_parse_uint32(val, &p->reach,
				    0, 65535, 0);

			} else if (strcmp(name, "timer") == 0 ||
			    strcmp(name, "peer.timer") == 0) {
				val_parse_int32(val, &p->timer,
				    INT32_MIN, INT32_MAX, 0);

			} else if (strcmp(name, "offset") == 0 ||
			    strcmp(name, "peer.offset") == 0) {
				p->offset = strdup(val);

			} else if (strcmp(name, "delay") == 0 ||
			    strcmp(name, "peer.delay") == 0) {
				p->delay = strdup(val);

			} else if (strcmp(name, "dispersion") == 0 ||
			    strcmp(name, "peer.dispersion") == 0) {
				p->dispersion = strdup(val);

			} else if (strcmp(name, "filtdelay") == 0 ||
			    strcmp(name, "peer.filtdelay") == 0) {
				p->filt_entries = parse_filt(val, associd, 0);

			} else if (strcmp(name, "filtoffset") == 0 ||
			    strcmp(name, "peer.filtoffset") == 0) {
				p->filt_entries = parse_filt(val, associd, 1);

			} else if (strcmp(name, "filtdisp") == 0 ||
			    strcmp(name, "peer.filtdisp") == 0) {
				p->filt_entries = parse_filt(val, associd, 2);
			}
		}
		free(pdata);
	}

	free(data);
	return (0);
}

/*
 * System variables - read-only scalars only.
 */
int
op_ntpSystem(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub - 1];

	switch (op) {

	  case SNMP_OP_GETNEXT:
		abort();

	  case SNMP_OP_GET:
		if (this_tick > sysinfo_tick) {
			if (fetch_sysinfo() == -1)
				return (SNMP_ERR_GENERR);
			sysinfo_tick = this_tick;
		}

		switch (which) {

		  case LEAF_ntpSysLeap:
			if (!sysb_leap)
				return (SNMP_ERR_NOSUCHNAME);
			value->v.integer = sys_leap;
			break;

		  case LEAF_ntpSysStratum:
			if (!sysb_stratum)
				return (SNMP_ERR_NOSUCHNAME);
			value->v.integer = sys_stratum;
			break;

		  case LEAF_ntpSysPrecision:
			if (!sysb_precision)
				return (SNMP_ERR_NOSUCHNAME);
			value->v.integer = sys_precision;
			break;

		  case LEAF_ntpSysRootDelay:
			if (sys_rootdelay == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			return (string_get(value, sys_rootdelay, -1));

		  case LEAF_ntpSysRootDispersion:
			if (sys_rootdispersion == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			return (string_get(value, sys_rootdispersion, -1));

		  case LEAF_ntpSysRefId:
			if (sys_refid == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			return (string_get(value, sys_refid, -1));

		  case LEAF_ntpSysRefTime:
			if (sysb_reftime == 0)
				return (SNMP_ERR_NOSUCHNAME);
			return (string_get(value, sys_reftime, 8));

		  case LEAF_ntpSysPoll:
			if (sysb_poll == 0)
				return (SNMP_ERR_NOSUCHNAME);
			value->v.integer = sys_poll;
			break;

		  case LEAF_ntpSysPeer:
			if (sysb_peer == 0)
				return (SNMP_ERR_NOSUCHNAME);
			value->v.uint32 = sys_peer;
			break;

		  case LEAF_ntpSysClock:
			if (sysb_clock == 0)
				return (SNMP_ERR_NOSUCHNAME);
			return (string_get(value, sys_clock, 8));

		  case LEAF_ntpSysSystem:
			if (sys_system == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			return (string_get(value, sys_system, -1));

		  case LEAF_ntpSysProcessor:
			if (sys_processor == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			return (string_get(value, sys_processor, -1));

		  default:
			abort();
		}
		return (SNMP_ERR_NOERROR);

	  case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);

	  case SNMP_OP_COMMIT:
	  case SNMP_OP_ROLLBACK:
		abort();
	}
	abort();
}

int
op_ntpPeersVarTable(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub - 1];
	uint32_t peer;
	struct peer *t;

	if (this_tick > peers_tick) {
		if (fetch_peers() == -1)
			return (SNMP_ERR_GENERR);
		peers_tick = this_tick;
	}

	switch (op) {

	  case SNMP_OP_GETNEXT:
		t = NEXT_OBJECT_INT(&peers, &value->var, sub);
		if (t == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = t->index;
		break;

	  case SNMP_OP_GET:
		t = FIND_OBJECT_INT(&peers, &value->var, sub);
		if (t == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		if (index_decode(&value->var, sub, iidx, &peer))
			return (SNMP_ERR_NO_CREATION);
		t = FIND_OBJECT_INT(&peers, &value->var, sub);
		if (t != NULL)
			return (SNMP_ERR_NOT_WRITEABLE);
		return (SNMP_ERR_NO_CREATION);

	  case SNMP_OP_COMMIT:
	  case SNMP_OP_ROLLBACK:
	  default:
		abort();
	}

	/*
	 * Come here for GET and COMMIT
	 */
	switch (which) {

	  case LEAF_ntpPeersConfigured:
		value->v.integer = t->config;
		break;

	  case LEAF_ntpPeersPeerAddress:
		return (ip_get(value, t->srcadr));

	  case LEAF_ntpPeersPeerPort:
		value->v.uint32 = t->srcport;
		break;

	  case LEAF_ntpPeersHostAddress:
		return (ip_get(value, t->dstadr));

	  case LEAF_ntpPeersHostPort:
		value->v.uint32 = t->dstport;
		break;

	  case LEAF_ntpPeersLeap:
		value->v.integer = t->leap;
		break;

	  case LEAF_ntpPeersMode:
		value->v.integer = t->hmode;
		break;

	  case LEAF_ntpPeersStratum:
		value->v.integer = t->stratum;
		break;

	  case LEAF_ntpPeersPeerPoll:
		value->v.integer = t->ppoll;
		break;

	  case LEAF_ntpPeersHostPoll:
		value->v.integer = t->hpoll;
		break;

	  case LEAF_ntpPeersPrecision:
		value->v.integer = t->precision;
		break;

	  case LEAF_ntpPeersRootDelay:
		return (string_get(value, t->rootdelay, -1));

	  case LEAF_ntpPeersRootDispersion:
		return (string_get(value, t->rootdispersion, -1));

	  case LEAF_ntpPeersRefId:
		return (string_get(value, t->refid, -1));

	  case LEAF_ntpPeersRefTime:
		return (string_get(value, t->reftime, 8));

	  case LEAF_ntpPeersOrgTime:
		return (string_get(value, t->orgtime, 8));

	  case LEAF_ntpPeersReceiveTime:
		return (string_get(value, t->rcvtime, 8));

	  case LEAF_ntpPeersTransmitTime:
		return (string_get(value, t->xmttime, 8));

	  case LEAF_ntpPeersReach:
		value->v.uint32 = t->reach;
		break;

	  case LEAF_ntpPeersTimer:
		value->v.uint32 = t->timer;
		break;

	  case LEAF_ntpPeersOffset:
		return (string_get(value, t->offset, -1));

	  case LEAF_ntpPeersDelay:
		return (string_get(value, t->delay, -1));

	  case LEAF_ntpPeersDispersion:
		return (string_get(value, t->dispersion, -1));

	  default:
		abort();
	}
	return (SNMP_ERR_NOERROR);
}


int
op_ntpFilterPeersVarTable(struct snmp_context *ctx __unused,
    struct snmp_value *value, u_int sub, u_int iidx, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub - 1];
	uint32_t peer;
	struct peer *t;

	if (this_tick > peers_tick) {
		if (fetch_peers() == -1)
			return (SNMP_ERR_GENERR);
		peers_tick = this_tick;
	}

	switch (op) {

	  case SNMP_OP_GETNEXT:
		t = NEXT_OBJECT_INT(&peers, &value->var, sub);
		if (t == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = t->index;
		break;

	  case SNMP_OP_GET:
		t = FIND_OBJECT_INT(&peers, &value->var, sub);
		if (t == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		if (index_decode(&value->var, sub, iidx, &peer))
			return (SNMP_ERR_NO_CREATION);
		t = FIND_OBJECT_INT(&peers, &value->var, sub);
		if (t != NULL)
			return (SNMP_ERR_NOT_WRITEABLE);
		return (SNMP_ERR_NO_CREATION);

	  case SNMP_OP_COMMIT:
	  case SNMP_OP_ROLLBACK:
	  default:
		abort();
	}

	/*
	 * Come here for GET and COMMIT
	 */
	switch (which) {

	  case LEAF_ntpFilterValidEntries:
		value->v.integer = t->filt_entries;
		break;

	  default:
		abort();
	}
	return (SNMP_ERR_NOERROR);
}

int
op_ntpFilterRegisterTable(struct snmp_context *ctx __unused, struct snmp_value *value __unused,
    u_int sub __unused, u_int iidx __unused, enum snmp_op op __unused)
{
	asn_subid_t which = value->var.subs[sub - 1];
	uint32_t peer;
	uint32_t filt;
	struct filt *t;

	if (this_tick > peers_tick) {
		if (fetch_peers() == -1)
			return (SNMP_ERR_GENERR);
		peers_tick = this_tick;
	}

	switch (op) {

	  case SNMP_OP_GETNEXT:
		t = NEXT_OBJECT_OID(&filts, &value->var, sub);
		if (t == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		index_append(&value->var, sub, &t->index);
		break;

	  case SNMP_OP_GET:
		t = FIND_OBJECT_OID(&filts, &value->var, sub);
		if (t == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	  case SNMP_OP_SET:
		if (index_decode(&value->var, sub, iidx, &peer, &filt))
			return (SNMP_ERR_NO_CREATION);
		t = FIND_OBJECT_OID(&filts, &value->var, sub);
		if (t != NULL)
			return (SNMP_ERR_NOT_WRITEABLE);
		return (SNMP_ERR_NO_CREATION);

	  case SNMP_OP_COMMIT:
	  case SNMP_OP_ROLLBACK:
	  default:
		abort();
	}

	/*
	 * Come here for GET and COMMIT
	 */
	switch (which) {

	  case LEAF_ntpFilterPeersOffset:
		return (string_get(value, t->offset, -1));

	  case LEAF_ntpFilterPeersDelay:
		return (string_get(value, t->delay, -1));

	  case LEAF_ntpFilterPeersDispersion:
		return (string_get(value, t->dispersion, -1));

	  default:
		abort();
	}
	return (SNMP_ERR_NOERROR);
}

/*
 * System variables - read-only scalars only.
 */
int
op_begemot_ntp(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	asn_subid_t which = value->var.subs[sub - 1];
	int ret;

	switch (op) {

	  case SNMP_OP_GETNEXT:
		abort();

	  case SNMP_OP_GET:
		switch (which) {

		  case LEAF_begemotNtpHost:
			return (string_get(value, ntp_host, -1));

		  case LEAF_begemotNtpPort:
			return (string_get(value, ntp_port, -1));

		  case LEAF_begemotNtpTimeout:
			value->v.uint32 = ntp_timeout;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNtpDebug:
			value->v.uint32 = ntp_debug;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNtpJitter:
			if (this_tick > sysinfo_tick) {
				if (fetch_sysinfo() == -1)
					return (SNMP_ERR_GENERR);
				sysinfo_tick = this_tick;
			}
			if (!sysb_jitter)
				return (SNMP_ERR_NOSUCHNAME);
			value->v.counter64 = sys_jitter / 1000 * (1ULL << 32);
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNtpStability:
			if (this_tick > sysinfo_tick) {
				if (fetch_sysinfo() == -1)
					return (SNMP_ERR_GENERR);
				sysinfo_tick = this_tick;
			}
			if (!sysb_stability)
				return (SNMP_ERR_NOSUCHNAME);
			value->v.counter64 = sys_stability * (1ULL << 32);
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_SET:
		switch (which) {

		  case LEAF_begemotNtpHost:
			/* only at initialization */
			if (community != COMM_INITIALIZE)
				return (SNMP_ERR_NOT_WRITEABLE);

			if ((ret = string_save(value, ctx, -1, &ntp_host))
			    != SNMP_ERR_NOERROR)
				return (ret);
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNtpPort:
			/* only at initialization */
			if (community != COMM_INITIALIZE)
				return (SNMP_ERR_NOT_WRITEABLE);

			if ((ret = string_save(value, ctx, -1, &ntp_port))
			    != SNMP_ERR_NOERROR)
				return (ret);
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNtpTimeout:
			ctx->scratch->int1 = ntp_timeout;
			if (value->v.uint32 < 1)
				return (SNMP_ERR_WRONG_VALUE);
			ntp_timeout = value->v.integer;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNtpDebug:
			ctx->scratch->int1 = ntp_debug;
			ntp_debug = value->v.integer;
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_ROLLBACK:
		switch (which) {

		  case LEAF_begemotNtpHost:
			string_rollback(ctx, &ntp_host);
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNtpPort:
			string_rollback(ctx, &ntp_port);
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNtpTimeout:
			ntp_timeout = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNtpDebug:
			ntp_debug = ctx->scratch->int1;
			return (SNMP_ERR_NOERROR);
		}
		abort();

	  case SNMP_OP_COMMIT:
		switch (which) {

		  case LEAF_begemotNtpHost:
			string_commit(ctx);
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNtpPort:
			string_commit(ctx);
			return (SNMP_ERR_NOERROR);

		  case LEAF_begemotNtpTimeout:
		  case LEAF_begemotNtpDebug:
			return (SNMP_ERR_NOERROR);
		}
		abort();
	}
	abort();
}
