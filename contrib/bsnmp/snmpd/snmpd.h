/*
 * Copyright (c) 2001-2003
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
 * $Begemot: bsnmp/snmpd/snmpd.h,v 1.24 2004/08/06 08:47:13 brandt Exp $
 *
 * Private SNMPd data and functions.
 */

#ifdef USE_LIBBEGEMOT
#include <rpoll.h>
#else
#include <isc/eventlib.h>
#endif

#define PATH_SYSCONFIG "/etc:/usr/etc:/usr/local/etc"

#ifdef USE_LIBBEGEMOT
#define	evTimerID	int
#define	evFileID	int
#endif

/*************************************************************
 *
 * Communities
 */
struct community {
	struct lmodule *owner;	/* who created the community */
	u_int		private;/* private name for the module */
	u_int		value;	/* value of this community */
	u_char *	string;	/* the community string */
	const u_char *	descr;	/* description */
	TAILQ_ENTRY(community) link;

	struct asn_oid	index;
};
/* list of all known communities */
extern TAILQ_HEAD(community_list, community) community_list;

/*************************************************************
 *
 * Request IDs.
 */
struct idrange {
	u_int		type;	/* type id */
	int32_t		base;	/* base of this range */
	int32_t		size;	/* size of this range */
	int32_t		next;	/* generator */
	struct lmodule *owner;	/* owner module */
	TAILQ_ENTRY(idrange) link;
};

/* list of all known ranges */
extern TAILQ_HEAD(idrange_list, idrange) idrange_list;

/* identifier generator */
extern u_int next_idrange;

/* request id generator for traps */
extern u_int trap_reqid;

/*************************************************************
 *
 * Timers
 */
struct timer {
	void	(*func)(void *);/* user function */
	void	*udata;		/* user data */
	evTimerID id;		/* timer id */
	struct lmodule *owner;	/* owner of the timer */
	LIST_ENTRY(timer) link;
};

/* list of all current timers */
extern LIST_HEAD(timer_list, timer) timer_list;


/*************************************************************
 *
 * File descriptors
 */
struct fdesc {
	int	fd;		/* the file descriptor */
	void	(*func)(int, void *);/* user function */
	void	*udata;		/* user data */
	evFileID id;		/* file id */
	struct lmodule *owner;	/* owner module of the file */
	LIST_ENTRY(fdesc) link;
};

/* list of all current selected files */
extern LIST_HEAD(fdesc_list, fdesc) fdesc_list;

/*************************************************************
 *
 * Loadable modules
 */
# define LM_SECTION_MAX	14
struct lmodule {
	char		section[LM_SECTION_MAX + 1]; /* and index */
	char		*path;
	u_int		flags;
	void		*handle;
	const struct snmp_module *config;

	TAILQ_ENTRY(lmodule) link;
	TAILQ_ENTRY(lmodule) start;

	struct asn_oid	index;
};
#define LM_STARTED	0x0001
#define LM_ONSTARTLIST	0x0002

extern TAILQ_HEAD(lmodules, lmodule) lmodules;

struct lmodule *lm_load(const char *, const char *);
void lm_unload(struct lmodule *);
void lm_start(struct lmodule *);

/*************************************************************
 *
 * SNMP ports
 */
/*
 * Common input stuff
 */
struct port_input {
	int		fd;		/* socket */
	void		*id;		/* evSelect handle */

	int		stream : 1;	/* stream socket */
	int		cred : 1;	/* want credentials */

	struct sockaddr	*peer;		/* last received packet */
	socklen_t	peerlen;
	int		priv : 1;	/* peer is privileged */

	u_char		*buf;		/* receive buffer */
	size_t		buflen;		/* buffer length */
	size_t		length;		/* received length */
	size_t		consumed;	/* how many bytes used */
};

struct tport {
	struct asn_oid	index;		/* table index of this tp point */
	TAILQ_ENTRY(tport) link;	/* table link */
	struct transport *transport;	/* who handles this */
};
TAILQ_HEAD(tport_list, tport);

int snmpd_input(struct port_input *, struct tport *);
void snmpd_input_close(struct port_input *);


/*
 * Transport domain
 */
#define TRANS_NAMELEN	64

struct transport_def {
	const char	*name;		/* name of this transport */
	struct asn_oid	id;		/* OBJID of this transport */

	int		(*start)(void);
	int		(*stop)(int);

	void		(*close_port)(struct tport *);
	int		(*init_port)(struct tport *);

	ssize_t		(*send)(struct tport *, const u_char *, size_t,
			    const struct sockaddr *, size_t);
	ssize_t         (*recv)(struct tport *, struct port_input *);
};
struct transport {
	struct asn_oid	index;		/* transport table index */
	TAILQ_ENTRY(transport) link;	/* ... and link */
	u_int		or_index;	/* registration index */

	struct tport_list table;	/* list of open ports */

	const struct transport_def *vtab;
};

TAILQ_HEAD(transport_list, transport);
extern struct transport_list transport_list;

void trans_insert_port(struct transport *, struct tport *);
void trans_remove_port(struct tport *);
struct tport *trans_find_port(struct transport *,
    const struct asn_oid *, u_int);
struct tport *trans_next_port(struct transport *,
    const struct asn_oid *, u_int);
struct tport *trans_first_port(struct transport *);
struct tport *trans_iter_port(struct transport *,
    int (*)(struct tport *, intptr_t), intptr_t);

int trans_register(const struct transport_def *, struct transport **);
int trans_unregister(struct transport *);

/*************************************************************
 *
 * SNMPd scalar configuration.
 */
struct snmpd {
	/* transmit buffer size */
	u_int32_t	txbuf;

	/* receive buffer size */
	u_int32_t	rxbuf;

	/* disable community table */
	int		comm_dis;

	/* authentication traps */
	int		auth_traps;

	/* source address for V1 traps */
	u_char		trap1addr[4];

	/* version enable flags */
	uint32_t	version_enable;
};
extern struct snmpd snmpd;

#define	VERS_ENABLE_V1	0x00000001
#define	VERS_ENABLE_V2C	0x00000002
#define	VERS_ENABLE_V3	0x00000004
#define	VERS_ENABLE_ALL	(VERS_ENABLE_V1 | VERS_ENABLE_V2C | VERS_ENABLE_V3)

/*
 * The debug group
 */
struct debug {
	u_int		dump_pdus;
	u_int		logpri;
	u_int		evdebug;
};
extern struct debug debug;


/*
 * SNMPd statistics table
 */
struct snmpd_stats {
	u_int32_t	inPkts;		/* total packets received */
	u_int32_t	inBadVersions;	/* unknown version number */
	u_int32_t	inASNParseErrs;	/* fatal parse errors */
	u_int32_t	inBadCommunityNames;
	u_int32_t	inBadCommunityUses;
	u_int32_t	proxyDrops;	/* dropped by proxy function */
	u_int32_t	silentDrops;

	u_int32_t	inBadPduTypes;
	u_int32_t	inTooLong;
	u_int32_t	noTxbuf;
	u_int32_t	noRxbuf;
};
extern struct snmpd_stats snmpd_stats;

/*
 * SNMPd Engine
 */
extern struct snmp_engine snmpd_engine;

/*
 * OR Table
 */
struct objres {
	TAILQ_ENTRY(objres) link;
	u_int		index;
	struct asn_oid	oid;	/* the resource OID */
	char		descr[256];
	u_int32_t	uptime;
	struct lmodule	*module;
};
TAILQ_HEAD(objres_list, objres);
extern struct objres_list objres_list;

/*
 * Trap Sink Table
 */
struct trapsink {
	TAILQ_ENTRY(trapsink) link;
	struct asn_oid	index;
	u_int		status;
	int		socket;
	u_char		comm[SNMP_COMMUNITY_MAXLEN + 1];
	int		version;
};
enum {
	TRAPSINK_ACTIVE		= 1,
	TRAPSINK_NOT_IN_SERVICE	= 2,
	TRAPSINK_NOT_READY	= 3,
	TRAPSINK_DESTROY	= 6,

	TRAPSINK_V1		= 1,
	TRAPSINK_V2		= 2,
};
TAILQ_HEAD(trapsink_list, trapsink);
extern struct trapsink_list trapsink_list;

extern const char *syspath;

/* snmpSerialNo */
extern int32_t snmp_serial_no;

int init_actvals(void);

extern char engine_file[];
int init_snmpd_engine(void);
int set_snmpd_engine(void);
void update_snmpd_engine_time(void);

int read_config(const char *, struct lmodule *);
int define_macro(const char *name, const char *value);

#define	LOG_ASN1_ERRORS	0x10000000
#define	LOG_SNMP_ERRORS	0x20000000
