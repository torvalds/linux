/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *	   Kendy Kutzner
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
 * $Begemot: bsnmp/lib/snmpclient.h,v 1.19 2005/05/23 11:10:14 brandt_h Exp $
 */
#ifndef _BSNMP_SNMPCLIENT_H
#define _BSNMP_SNMPCLIENT_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <stddef.h>


#define SNMP_STRERROR_LEN 200

#define SNMP_LOCAL_PATH	"/tmp/snmpXXXXXXXXXXXXXX"

/*
 * transport methods
 */
#define	SNMP_TRANS_UDP		0
#define	SNMP_TRANS_LOC_DGRAM	1
#define	SNMP_TRANS_LOC_STREAM	2

/* type of callback function for responses
 * this callback function is responsible for free() any memory associated with
 * any of the PDUs. Therefor it may call snmp_pdu_free() */
typedef void (*snmp_send_cb_f)(struct snmp_pdu *, struct snmp_pdu *, void *);

/* type of callback function for timeouts */
typedef void (*snmp_timeout_cb_f)(void * );

/* timeout start function */
typedef void *(*snmp_timeout_start_f)(struct timeval *timeout,
    snmp_timeout_cb_f callback, void *);

/* timeout stop function */
typedef void (*snmp_timeout_stop_f)(void *timeout_id);

/*
 * Client context.
 */
struct snmp_client {
	enum snmp_version	version;
	int			trans;	/* which transport to use */

	/* these two are read-only for the application */
	char			*cport;	/* port number as string */
	char			*chost;	/* host name or IP address as string */

	char			read_community[SNMP_COMMUNITY_MAXLEN + 1];
	char			write_community[SNMP_COMMUNITY_MAXLEN + 1];

	/* SNMPv3 specific fields */
	int32_t			identifier;
	int32_t			security_model;
	struct snmp_engine	engine;
	struct snmp_user	user;

	/* SNMPv3 Access control - VACM*/
	uint32_t		clen;
	uint8_t			cengine[SNMP_ENGINE_ID_SIZ];
	char			cname[SNMP_CONTEXT_NAME_SIZ];

	struct timeval		timeout;
	u_int			retries;

	int			dump_pdus;

	size_t			txbuflen;
	size_t			rxbuflen;

	int			fd;

	int32_t			next_reqid;
	int32_t			max_reqid;
	int32_t			min_reqid;

	char			error[SNMP_STRERROR_LEN];

	snmp_timeout_start_f	timeout_start;
	snmp_timeout_stop_f	timeout_stop;

	char			local_path[sizeof(SNMP_LOCAL_PATH)];
};

/* the global context */
extern struct snmp_client snmp_client;

/* initizialies a snmp_client structure */
void snmp_client_init(struct snmp_client *);

/* initialize fields */
int snmp_client_set_host(struct snmp_client *, const char *);
int snmp_client_set_port(struct snmp_client *, const char *);

/* open connection to snmp server (hostname or portname can be NULL) */
int snmp_open(const char *_hostname, const char *_portname,
    const char *_read_community, const char *_write_community);

/* close connection */
void snmp_close(void);

/* initialize a snmp_pdu structure */
void snmp_pdu_create(struct snmp_pdu *, u_int _op);

/* add pairs of (struct asn_oid *, enum snmp_syntax) to an existing pdu */
int snmp_add_binding(struct snmp_pdu *, ...);

/* check wheater the answer is valid or not */
int snmp_pdu_check(const struct snmp_pdu *_req, const struct snmp_pdu *_resp);

int32_t snmp_pdu_send(struct snmp_pdu *_pdu, snmp_send_cb_f _func, void *_arg);

/*  append an index to an oid */
int snmp_oid_append(struct asn_oid *_oid, const char *_fmt, ...);

/* receive a packet */
int snmp_receive(int _blocking);

/*
 * This structure is used to describe an SNMP table that is to be fetched.
 * The C-structure that is produced by the fetch function must start with
 * a TAILQ_ENTRY and an u_int64_t.
 */
struct snmp_table {
	/* base OID of the table */
	struct asn_oid		table;
	/* type OID of the LastChange variable for the table if any */
	struct asn_oid		last_change;
	/* maximum number of iterations if table has changed */
	u_int			max_iter;
	/* size of the C-structure */
	size_t			entry_size;
	/* number of index fields */
	u_int			index_size;
	/* bit mask of required fields */
	uint64_t		req_mask;

	/* indexes and columns to fetch. Ended by a NULL syntax entry */
	struct snmp_table_entry {
	    /* the column sub-oid, ignored for index fields */
	    asn_subid_t		subid;
	    /* the syntax of the column or index */
	    enum snmp_syntax	syntax;
	    /* offset of the field into the C-structure. For octet strings
	     * this points to an u_char * followed by a size_t */
	    off_t		offset;
#if defined(__GNUC__) && __GNUC__ < 3
	}			entries[0];
#else
	}			entries[];
#endif
};

/* callback type for table fetch */
typedef void (*snmp_table_cb_f)(void *_list, void *_arg, int _res);

/* fetch a table. The argument points to a TAILQ_HEAD */
int snmp_table_fetch(const struct snmp_table *descr, void *);
int snmp_table_fetch_async(const struct snmp_table *, void *,
    snmp_table_cb_f, void *);

/* send a request and wait for the response */
int snmp_dialog(struct snmp_pdu *_req, struct snmp_pdu *_resp);

/* discover an authorative snmpEngineId */
int snmp_discover_engine(char *);

/* parse a server specification */
int snmp_parse_server(struct snmp_client *, const char *);

#endif /* _BSNMP_SNMPCLIENT_H */
