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
 * $Begemot: bsnmp/snmp_mibII/mibII.h,v 1.16 2006/02/14 09:04:19 brandt_h Exp $
 *
 * Implementation of the interfaces and IP groups of MIB-II.
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/syslog.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <err.h>
#include <ctype.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_mib.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "asn1.h"
#include "snmp.h"
#include "snmpmod.h"
#include "snmp_mibII.h"
#include "mibII_tree.h"

/* maximum size of the interface alias unless overridden with net.ifdescr_maxlen */
#define	MIBIF_ALIAS_SIZE	(64 + 1)
#define	MIBIF_ALIAS_SIZE_MAX	1024

/*
 * Interface list and flags.
 */
TAILQ_HEAD(mibif_list, mibif);
enum {
	MIBIF_FOUND		= 0x0001,
	MIBIF_HIGHSPEED		= 0x0002,
	MIBIF_VERYHIGHSPEED	= 0x0004,
};

/*
 * Private mibif data - hang off from the mibif.
 */
struct mibif_private {
	uint64_t	hc_inoctets;
	uint64_t	hc_outoctets;
	uint64_t	hc_omcasts;
	uint64_t	hc_opackets;
	uint64_t	hc_imcasts;
	uint64_t	hc_ipackets;

};
#define	MIBIF_PRIV(IFP) ((struct mibif_private *)((IFP)->private))

/*
 * Interface addresses.
 */
TAILQ_HEAD(mibifa_list, mibifa);
enum {
	MIBIFA_FOUND	 = 0x0001,
	MIBIFA_DESTROYED = 0x0002,
};

/*
 * Receive addresses
 */
TAILQ_HEAD(mibrcvaddr_list, mibrcvaddr);
enum {
	MIBRCVADDR_FOUND	= 0x00010000,
};

/*
 * Interface index mapping. The problem here is, that if the same interface
 * is reinstantiated (for examble by unloading and loading the hardware driver)
 * we must use the same index for this interface. For dynamic interfaces
 * (clip, lane) we must use a fresh index, each time a new interface is created.
 * To differentiate between these types of interfaces we use the following table
 * which contains an entry for each dynamic interface type. All other interface
 * types are supposed to be static. The mibindexmap contains an entry for
 * all interfaces. The mibif pointer is NULL, if the interface doesn't exist
 * anymore.
 */
struct mibdynif {
	SLIST_ENTRY(mibdynif) link;
	char	name[IFNAMSIZ];
};
SLIST_HEAD(mibdynif_list, mibdynif);

struct mibindexmap {
	STAILQ_ENTRY(mibindexmap) link;
	u_short		sysindex;
	u_int		ifindex;
	struct mibif	*mibif;		/* may be NULL */
	char		name[IFNAMSIZ];
};
STAILQ_HEAD(mibindexmap_list, mibindexmap);

/*
 * Interface stacking. The generic code cannot know how the interfaces stack.
 * For this reason it instantiates only the x.0 and 0.x table elements. All
 * others have to be instantiated by the interface specific modules.
 * The table is read-only.
 */
struct mibifstack {
	TAILQ_ENTRY(mibifstack) link;
	struct asn_oid index;
};
TAILQ_HEAD(mibifstack_list, mibifstack);

/*
 * NetToMediaTable (ArpTable)
 */
struct mibarp {
	TAILQ_ENTRY(mibarp) link;
	struct asn_oid	index;		/* contains both the ifindex and addr */
	u_char		phys[128];	/* the physical address */
	u_int		physlen;	/* and its length */
	u_int		flags;
};
TAILQ_HEAD(mibarp_list, mibarp);
enum {
	MIBARP_FOUND	= 0x00010000,
	MIBARP_PERM	= 0x00000001,
};

/*
 * New if registrations
 */
struct newifreg {
	TAILQ_ENTRY(newifreg) link;
	const struct lmodule *mod;
	int	(*func)(struct mibif *);
};
TAILQ_HEAD(newifreg_list, newifreg);

/* list of all IP addresses */
extern struct mibifa_list mibifa_list;

/* list of all interfaces */
extern struct mibif_list mibif_list;

/* list of dynamic interface names */
extern struct mibdynif_list mibdynif_list;

/* list of all interface index mappings */
extern struct mibindexmap_list mibindexmap_list;

/* list of all stacking entries */
extern struct mibifstack_list mibifstack_list;

/* list of all receive addresses */
extern struct mibrcvaddr_list mibrcvaddr_list;

/* list of all NetToMedia entries */
extern struct mibarp_list mibarp_list;

/* number of interfaces */
extern int32_t mib_if_number;

/* last change of interface table */
extern uint64_t mib_iftable_last_change;

/* last change of stack table */
extern uint64_t mib_ifstack_last_change;

/* if this is set, one of our lists may be bad. refresh them when idle */
extern int mib_iflist_bad;

/* last time refreshed */
extern uint64_t mibarpticks;

/* info on system clocks */
extern struct clockinfo clockinfo;

/* baud rate of fastest interface */
extern uint64_t mibif_maxspeed;

/* user-forced update interval */
extern u_int mibif_force_hc_update_interval;

/* current update interval */
extern u_int mibif_hc_update_interval;

/* re-compute update interval */
void mibif_reset_hc_timer(void);

/* interfaces' data poll interval */
extern u_int mibII_poll_ticks;

/* restart the data poll timer */
void mibif_restart_mibII_poll_timer(void);

#define MIBII_POLL_TICKS	100

/* get interfaces and interface addresses. */
void mib_fetch_interfaces(void);

/* check whether this interface(type) is dynamic */
int mib_if_is_dyn(const char *name);

/* destroy an interface address */
int mib_destroy_ifa(struct mibifa *);

/* restituate a deleted interface address */
void mib_undestroy_ifa(struct mibifa *);

/* change interface address */
int mib_modify_ifa(struct mibifa *);

/* undo if address modification */
void mib_unmodify_ifa(struct mibifa *);

/* create an interface address */
struct mibifa * mib_create_ifa(u_int ifindex, struct in_addr addr, struct in_addr mask, struct in_addr bcast);

/* delete a freshly created address */
void mib_uncreate_ifa(struct mibifa *);

/* create/delete arp entries */
struct mibarp *mib_arp_create(const struct mibif *, struct in_addr, const u_char *, size_t);
void mib_arp_delete(struct mibarp *);

/* find arp entry */
struct mibarp *mib_find_arp(const struct mibif *, struct in_addr);

/* update arp table */
void mib_arp_update(void);

/* fetch routing table */
u_char *mib_fetch_rtab(int af, int info, int arg, size_t *lenp);

/* process routing message */
void mib_sroute_process(struct rt_msghdr *, struct sockaddr *,
    struct sockaddr *, struct sockaddr *);

/* send a routing message */
void mib_send_rtmsg(struct rt_msghdr *, struct sockaddr *,
    struct sockaddr *, struct sockaddr *);

/* extract addresses from routing message */
void mib_extract_addrs(int, u_char *, struct sockaddr **);

/* fetch routing table */
int mib_fetch_route(void);
