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
 * $Begemot: bsnmp/snmpd/trans_lsock.h,v 1.3 2004/08/06 08:47:15 brandt Exp $
 *
 * Local domain socket transport
 */

enum locp {
	LOCP_DGRAM_UNPRIV	= 1,
	LOCP_DGRAM_PRIV		= 2,
	LOCP_STREAM_UNPRIV	= 3,
	LOCP_STREAM_PRIV	= 4,
};
struct lsock_peer {
	LIST_ENTRY(lsock_peer) link;
	struct port_input input;
	struct sockaddr_un peer;
	struct lsock_port *port;	/* parent port */
};

struct lsock_port {
	struct tport	tport;		/* must begin with this */

	char		*name;		/* unix path name */
	enum locp	type;		/* type of port */

	int		str_sock;	/* stream socket */
	void		*str_id;	/* select handle */

	LIST_HEAD(, lsock_peer) peers;
};

extern const struct transport_def lsock_trans;
