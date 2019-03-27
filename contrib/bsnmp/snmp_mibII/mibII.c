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
 * $Begemot: mibII.c 516 2006-10-27 15:54:02Z brandt_h $
 *
 * Implementation of the standard interfaces and ip MIB.
 */
#include "mibII.h"
#include "mibII_oid.h"
#include <net/if.h>
#include <net/if_types.h>


/*****************************/

/* our module */
static struct lmodule *module;

/* routing socket */
static int route;
static void *route_fd;

/* if-index allocator */
static uint32_t next_if_index = 1;

/* currently fetching the arp table */
static int in_update_arp;

/* OR registrations */
static u_int ifmib_reg;
static u_int ipmib_reg;
static u_int tcpmib_reg;
static u_int udpmib_reg;
static u_int ipForward_reg;

/*****************************/

/* list of all IP addresses */
struct mibifa_list mibifa_list = TAILQ_HEAD_INITIALIZER(mibifa_list);

/* list of all interfaces */
struct mibif_list mibif_list = TAILQ_HEAD_INITIALIZER(mibif_list);

/* list of dynamic interface names */
struct mibdynif_list mibdynif_list = SLIST_HEAD_INITIALIZER(mibdynif_list);

/* list of all interface index mappings */
struct mibindexmap_list mibindexmap_list = STAILQ_HEAD_INITIALIZER(mibindexmap_list);

/* list of all stacking entries */
struct mibifstack_list mibifstack_list = TAILQ_HEAD_INITIALIZER(mibifstack_list);

/* list of all receive addresses */
struct mibrcvaddr_list mibrcvaddr_list = TAILQ_HEAD_INITIALIZER(mibrcvaddr_list);

/* list of all NetToMedia entries */
struct mibarp_list mibarp_list = TAILQ_HEAD_INITIALIZER(mibarp_list);

/* number of interfaces */
int32_t mib_if_number;

/* last change of table */
uint64_t mib_iftable_last_change;

/* last change of stack table */
uint64_t mib_ifstack_last_change;

/* if this is set, one of our lists may be bad. refresh them when idle */
int mib_iflist_bad;

/* network socket */
int mib_netsock;

/* last time refreshed */
uint64_t mibarpticks;

/* info on system clocks */
struct clockinfo clockinfo;

/* list of all New if registrations */
static struct newifreg_list newifreg_list = TAILQ_HEAD_INITIALIZER(newifreg_list);

/* baud rate of fastest interface */
uint64_t mibif_maxspeed;

/* user-forced update interval */
u_int mibif_force_hc_update_interval;

/* current update interval */
u_int mibif_hc_update_interval;

/* HC update timer handle */
static void *hc_update_timer;

/* Idle poll timer */
static void *mibII_poll_timer;

/* interfaces' data poll interval */
u_int mibII_poll_ticks;

/* Idle poll hook */
static void mibII_idle(void *arg __unused);

/*****************************/

static const struct asn_oid oid_ifMIB = OIDX_ifMIB;
static const struct asn_oid oid_ipMIB = OIDX_ipMIB;
static const struct asn_oid oid_tcpMIB = OIDX_tcpMIB;
static const struct asn_oid oid_udpMIB = OIDX_udpMIB;
static const struct asn_oid oid_ipForward = OIDX_ipForward;
static const struct asn_oid oid_linkDown = OIDX_linkDown;
static const struct asn_oid oid_linkUp = OIDX_linkUp;
static const struct asn_oid oid_ifIndex = OIDX_ifIndex;

/*****************************/

/*
 * Find an interface
 */
struct mibif *
mib_find_if(u_int idx)
{
	struct mibif *ifp;

	TAILQ_FOREACH(ifp, &mibif_list, link)
		if (ifp->index == idx)
			return (ifp);
	return (NULL);
}

struct mibif *
mib_find_if_sys(u_int sysindex)
{
	struct mibif *ifp;

	TAILQ_FOREACH(ifp, &mibif_list, link)
		if (ifp->sysindex == sysindex)
			return (ifp);
	return (NULL);
}

struct mibif *
mib_find_if_name(const char *name)
{
	struct mibif *ifp;

	TAILQ_FOREACH(ifp, &mibif_list, link)
		if (strcmp(ifp->name, name) == 0)
			return (ifp);
	return (NULL);
}

/*
 * Check whether an interface is dynamic. The argument may include the
 * unit number. This assumes, that the name part does NOT contain digits.
 */
int
mib_if_is_dyn(const char *name)
{
	size_t len;
	struct mibdynif *d;

	for (len = 0; name[len] != '\0' && isalpha(name[len]) ; len++)
		;
	SLIST_FOREACH(d, &mibdynif_list, link)
		if (strlen(d->name) == len && strncmp(d->name, name, len) == 0)
			return (1);
	return (0);
}

/* set an interface name to dynamic mode */
void
mib_if_set_dyn(const char *name)
{
	struct mibdynif *d;

	SLIST_FOREACH(d, &mibdynif_list, link)
		if (strcmp(name, d->name) == 0)
			return;
	if ((d = malloc(sizeof(*d))) == NULL)
		err(1, NULL);
	strlcpy(d->name, name, sizeof(d->name));
	SLIST_INSERT_HEAD(&mibdynif_list, d, link);
}

/*
 * register for interface creations
 */
int
mib_register_newif(int (*func)(struct mibif *), const struct lmodule *mod)
{
	struct newifreg *reg;

	TAILQ_FOREACH(reg, &newifreg_list, link)
		if (reg->mod == mod) {
			reg->func = func;
			return (0);
		}
	if ((reg = malloc(sizeof(*reg))) == NULL) {
		syslog(LOG_ERR, "newifreg: %m");
		return (-1);
	}
	reg->mod = mod;
	reg->func = func;
	TAILQ_INSERT_TAIL(&newifreg_list, reg, link);

	return (0);
}

void
mib_unregister_newif(const struct lmodule *mod)
{
	struct newifreg *reg;

	TAILQ_FOREACH(reg, &newifreg_list, link)
		if (reg->mod == mod) {
			TAILQ_REMOVE(&newifreg_list, reg, link);
			free(reg);
			return;
		}

}

struct mibif *
mib_first_if(void)
{
	return (TAILQ_FIRST(&mibif_list));
}
struct mibif *
mib_next_if(const struct mibif *ifp)
{
	return (TAILQ_NEXT(ifp, link));
}

/*
 * Change the admin status of an interface
 */
int
mib_if_admin(struct mibif *ifp, int up)
{
	struct ifreq ifr;

	strlcpy(ifr.ifr_name, ifp->name, sizeof(ifr.ifr_name));
	if (ioctl(mib_netsock, SIOCGIFFLAGS, &ifr) == -1) {
		syslog(LOG_ERR, "SIOCGIFFLAGS(%s): %m", ifp->name);
		return (-1);
	}
	if (up)
		ifr.ifr_flags |= IFF_UP;
	else
		ifr.ifr_flags &= ~IFF_UP;
	if (ioctl(mib_netsock, SIOCSIFFLAGS, &ifr) == -1) {
		syslog(LOG_ERR, "SIOCSIFFLAGS(%s): %m", ifp->name);
		return (-1);
	}

	(void)mib_fetch_ifmib(ifp);

	return (0);
}

/*
 * Generate a link up/down trap
 */
static void
link_trap(struct mibif *ifp, int up)
{
	struct snmp_value ifindex;

	ifindex.var = oid_ifIndex;
	ifindex.var.subs[ifindex.var.len++] = ifp->index;
	ifindex.syntax = SNMP_SYNTAX_INTEGER;
	ifindex.v.integer = ifp->index;

	snmp_send_trap(up ? &oid_linkUp : &oid_linkDown, &ifindex,
	    (struct snmp_value *)NULL);
}

/**
 * Fetch the GENERIC IFMIB and update the HC counters
 */
static int
fetch_generic_mib(struct mibif *ifp, const struct ifmibdata *old)
{
	int name[6];
	size_t len;
	struct mibif_private *p = ifp->private;

	name[0] = CTL_NET;
	name[1] = PF_LINK;
	name[2] = NETLINK_GENERIC;
	name[3] = IFMIB_IFDATA;
	name[4] = ifp->sysindex;
	name[5] = IFDATA_GENERAL;

	len = sizeof(ifp->mib);
	if (sysctl(name, nitems(name), &ifp->mib, &len, NULL, 0) == -1) {
		if (errno != ENOENT)
			syslog(LOG_WARNING, "sysctl(ifmib, %s) failed %m",
			    ifp->name);
		return (-1);
	}

	/*
	 * Assume that one of the two following compounds is optimized away
	 */
	if (ULONG_MAX >= 0xffffffffffffffffULL) {
		p->hc_inoctets = ifp->mib.ifmd_data.ifi_ibytes;
		p->hc_outoctets = ifp->mib.ifmd_data.ifi_obytes;
		p->hc_omcasts = ifp->mib.ifmd_data.ifi_omcasts;
		p->hc_opackets = ifp->mib.ifmd_data.ifi_opackets;
		p->hc_imcasts = ifp->mib.ifmd_data.ifi_imcasts;
		p->hc_ipackets = ifp->mib.ifmd_data.ifi_ipackets;

	} else if (ULONG_MAX >= 0xffffffff) {

#define	UPDATE(HC, MIB)							\
		if (old->ifmd_data.MIB > ifp->mib.ifmd_data.MIB)	\
			p->HC += (0x100000000ULL +			\
			    ifp->mib.ifmd_data.MIB) -			\
			    old->ifmd_data.MIB;				\
		else							\
			p->HC += ifp->mib.ifmd_data.MIB -		\
			    old->ifmd_data.MIB;

		UPDATE(hc_inoctets, ifi_ibytes)
		UPDATE(hc_outoctets, ifi_obytes)
		UPDATE(hc_omcasts, ifi_omcasts)
		UPDATE(hc_opackets, ifi_opackets)
		UPDATE(hc_imcasts, ifi_imcasts)
		UPDATE(hc_ipackets, ifi_ipackets)

#undef	UPDATE
	} else
		abort();
	return (0);
}

/**
 * Update the 64-bit interface counters
 */
static void
update_hc_counters(void *arg __unused)
{
	struct mibif *ifp;
	struct ifmibdata oldmib;

	TAILQ_FOREACH(ifp, &mibif_list, link) {
		oldmib = ifp->mib;
		(void)fetch_generic_mib(ifp, &oldmib);
	}
}

/**
 * Recompute the poll timer for the HC counters
 */
void
mibif_reset_hc_timer(void)
{
	u_int ticks;

	if ((ticks = mibif_force_hc_update_interval) == 0) {
		if (mibif_maxspeed <= IF_Mbps(10)) {
			/* at 10Mbps overflow needs 3436 seconds */
			ticks = 3000 * 100;	/* 50 minutes */
		} else if (mibif_maxspeed <= IF_Mbps(100)) {
			/* at 100Mbps overflow needs 343 seconds */
			ticks = 300 * 100;	/* 5 minutes */
		} else if (mibif_maxspeed < IF_Mbps(622)) {
			/* at 622Mbps overflow needs 53 seconds */
			ticks = 40 * 100;	/* 40 seconds */
		} else if (mibif_maxspeed <= IF_Mbps(1000)) {
			/* at 1Gbps overflow needs  34 seconds */
			ticks = 20 * 100;	/* 20 seconds */
		} else {
			/* at 10Gbps overflow needs 3.4 seconds */
			ticks = 100;		/* 1 seconds */
		}
	}

	if (ticks == mibif_hc_update_interval)
		return;

	if (hc_update_timer != NULL) {
		timer_stop(hc_update_timer);
		hc_update_timer = NULL;
	}
	update_hc_counters(NULL);
	if ((hc_update_timer = timer_start_repeat(ticks, ticks,
	    update_hc_counters, NULL, module)) == NULL) {
		syslog(LOG_ERR, "timer_start(%u): %m", ticks);
		return;
	}
	mibif_hc_update_interval = ticks;
}

/**
 * Restart the idle poll timer.
 */
void
mibif_restart_mibII_poll_timer(void)
{
	if (mibII_poll_timer != NULL)
		timer_stop(mibII_poll_timer);

	if ((mibII_poll_timer = timer_start_repeat(mibII_poll_ticks * 10,
	    mibII_poll_ticks * 10, mibII_idle, NULL, module)) == NULL)
		syslog(LOG_ERR, "timer_start(%u): %m", mibII_poll_ticks);
}

/*
 * Fetch new MIB data.
 */
int
mib_fetch_ifmib(struct mibif *ifp)
{
	static int kmib[2] = { -1, 0 }; /* for sysctl net.ifdescr_maxlen */

	int name[6];
	size_t kmiblen = nitems(kmib);
	size_t len;
	void *newmib;
	struct ifmibdata oldmib = ifp->mib;
	struct ifreq irr;
	unsigned int alias_maxlen = MIBIF_ALIAS_SIZE_MAX;

	if (fetch_generic_mib(ifp, &oldmib) == -1)
		return (-1);

	/*
	 * Quoting RFC2863, 3.1.15: "... LinkUp and linkDown traps are
	 * generated just after ifOperStatus leaves, or just before it
	 * enters, the down state, respectively;"
	 */
	if (ifp->trap_enable && ifp->mib.ifmd_data.ifi_link_state !=
	    oldmib.ifmd_data.ifi_link_state &&
	    (ifp->mib.ifmd_data.ifi_link_state == LINK_STATE_DOWN ||
	    oldmib.ifmd_data.ifi_link_state == LINK_STATE_DOWN))
		link_trap(ifp, ifp->mib.ifmd_data.ifi_link_state ==
		    LINK_STATE_UP ? 1 : 0);

	ifp->flags &= ~(MIBIF_HIGHSPEED | MIBIF_VERYHIGHSPEED);
	if (ifp->mib.ifmd_data.ifi_baudrate > 20000000) {
		ifp->flags |= MIBIF_HIGHSPEED;
		if (ifp->mib.ifmd_data.ifi_baudrate > 650000000)
			ifp->flags |= MIBIF_VERYHIGHSPEED;
	}
	if (ifp->mib.ifmd_data.ifi_baudrate > mibif_maxspeed) {
		mibif_maxspeed = ifp->mib.ifmd_data.ifi_baudrate;
		mibif_reset_hc_timer();
	}

	/*
	 * linkspecific MIB
	 */
	name[0] = CTL_NET;
	name[1] = PF_LINK;
	name[2] = NETLINK_GENERIC;
	name[3] = IFMIB_IFDATA;
	name[4] = ifp->sysindex;
	name[5] = IFDATA_LINKSPECIFIC;
	if (sysctl(name, nitems(name), NULL, &len, NULL, 0) == -1) {
		syslog(LOG_WARNING, "sysctl linkmib estimate (%s): %m",
		    ifp->name);
		if (ifp->specmib != NULL) {
			ifp->specmib = NULL;
			ifp->specmiblen = 0;
		}
		goto out;
	}
	if (len == 0) {
		if (ifp->specmib != NULL) {
			ifp->specmib = NULL;
			ifp->specmiblen = 0;
		}
		goto out;
	}

	if (ifp->specmiblen != len) {
		if ((newmib = realloc(ifp->specmib, len)) == NULL) {
			ifp->specmib = NULL;
			ifp->specmiblen = 0;
			goto out;
		}
		ifp->specmib = newmib;
		ifp->specmiblen = len;
	}
	if (sysctl(name, nitems(name), ifp->specmib, &len, NULL, 0) == -1) {
		syslog(LOG_WARNING, "sysctl linkmib (%s): %m", ifp->name);
		if (ifp->specmib != NULL) {
			ifp->specmib = NULL;
			ifp->specmiblen = 0;
		}
	}

  out:

	/*
	 * Find sysctl mib for net.ifdescr_maxlen (one time).
	 * kmib[0] == -1 at first call to mib_fetch_ifmib().
	 * Then kmib[0] > 0 if we found sysctl mib for net.ifdescr_maxlen.
	 * Else, kmib[0] == 0 (unexpected error from a kernel).
	 */
	if (kmib[0] < 0 &&
	    sysctlnametomib("net.ifdescr_maxlen", kmib, &kmiblen) < 0) {
		kmib[0] = 0;
		syslog(LOG_WARNING, "sysctlnametomib net.ifdescr_maxlen: %m");
	}

	/*
	 * Fetch net.ifdescr_maxlen value every time to catch up with changes.
	 */
	len = sizeof(alias_maxlen);
	if (kmib[0] > 0 && sysctl(kmib, 2, &alias_maxlen, &len, NULL, 0) < 0) {
		/* unexpected error from the kernel, use default value */
		alias_maxlen = MIBIF_ALIAS_SIZE_MAX;
		syslog(LOG_WARNING, "sysctl net.ifdescr_maxlen: %m");
	}

	/*
	 * Kernel limit might be decreased after interfaces got
	 * their descriptions assigned. Try to obtain them anyway.
	 */
	if (alias_maxlen == 0)
		alias_maxlen = MIBIF_ALIAS_SIZE_MAX;

	/*
	 * Allocate maximum memory for a buffer and later reallocate
	 * to free extra memory.
	 */
	if ((ifp->alias = malloc(alias_maxlen)) == NULL) {
		syslog(LOG_WARNING, "malloc(%d) failed: %m", (int)alias_maxlen);
		goto fin;
	}

	strlcpy(irr.ifr_name, ifp->name, sizeof(irr.ifr_name));
	irr.ifr_buffer.buffer = ifp->alias;
	irr.ifr_buffer.length = alias_maxlen;
	if (ioctl(mib_netsock, SIOCGIFDESCR, &irr) == -1) {
		free(ifp->alias);
		ifp->alias = NULL;
		if (errno != ENOMSG)
			syslog(LOG_WARNING, "SIOCGIFDESCR (%s): %m", ifp->name);
	} else if (irr.ifr_buffer.buffer == NULL) {
		free(ifp->alias);
		ifp->alias = NULL;
		syslog(LOG_WARNING, "SIOCGIFDESCR (%s): too long (%zu)",
		    ifp->name, irr.ifr_buffer.length);
	} else {
		ifp->alias_size = strnlen(ifp->alias, alias_maxlen) + 1;

		if (ifp->alias_size > MIBIF_ALIAS_SIZE)
		    ifp->alias_size = MIBIF_ALIAS_SIZE; 

		if (ifp->alias_size < alias_maxlen)
			ifp->alias = realloc(ifp->alias, ifp->alias_size);
	}

fin:
	ifp->mibtick = get_ticks();
	return (0);
}

/* find first/next address for a given interface */
struct mibifa *
mib_first_ififa(const struct mibif *ifp)
{
	struct mibifa *ifa;

	TAILQ_FOREACH(ifa, &mibifa_list, link)
		if (ifp->index == ifa->ifindex)
			return (ifa);
	return (NULL);
}

struct mibifa *
mib_next_ififa(struct mibifa *ifa0)
{
	struct mibifa *ifa;

	ifa = ifa0;
	while ((ifa = TAILQ_NEXT(ifa, link)) != NULL)
		if (ifa->ifindex == ifa0->ifindex)
			return (ifa);
	return (NULL);
}

/*
 * Allocate a new IFA
 */
static struct mibifa *
alloc_ifa(u_int ifindex, struct in_addr addr)
{
	struct mibifa *ifa;
	uint32_t ha;

	if ((ifa = malloc(sizeof(struct mibifa))) == NULL) {
		syslog(LOG_ERR, "ifa: %m");
		return (NULL);
	}
	ifa->inaddr = addr;
	ifa->ifindex = ifindex;

	ha = ntohl(ifa->inaddr.s_addr);
	ifa->index.len = 4;
	ifa->index.subs[0] = (ha >> 24) & 0xff;
	ifa->index.subs[1] = (ha >> 16) & 0xff;
	ifa->index.subs[2] = (ha >>  8) & 0xff;
	ifa->index.subs[3] = (ha >>  0) & 0xff;

	ifa->flags = 0;
	ifa->inbcast.s_addr = 0;
	ifa->inmask.s_addr = 0xffffffff;

	INSERT_OBJECT_OID(ifa, &mibifa_list);

	return (ifa);
}

/*
 * Delete an interface address
 */
static void
destroy_ifa(struct mibifa *ifa)
{
	TAILQ_REMOVE(&mibifa_list, ifa, link);
	free(ifa);
}


/*
 * Helper routine to extract the sockaddr structures from a routing
 * socket message.
 */
void
mib_extract_addrs(int addrs, u_char *info, struct sockaddr **out)
{
	u_int i;

	for (i = 0; i < RTAX_MAX; i++) {
		if ((addrs & (1 << i)) != 0) {
			*out = (struct sockaddr *)(void *)info;
			info += roundup((*out)->sa_len, sizeof(long));
		} else
			*out = NULL;
		out++;
	}
}

/*
 * save the phys address of an interface. Handle receive address entries here.
 */
static void
get_physaddr(struct mibif *ifp, struct sockaddr_dl *sdl, u_char *ptr)
{
	u_char *np;
	struct mibrcvaddr *rcv;

	if (sdl->sdl_alen == 0) {
		/* no address */
		if (ifp->physaddrlen != 0) {
			if ((rcv = mib_find_rcvaddr(ifp->index, ifp->physaddr,
			    ifp->physaddrlen)) != NULL)
				mib_rcvaddr_delete(rcv);
			free(ifp->physaddr);
			ifp->physaddr = NULL;
			ifp->physaddrlen = 0;
		}
		return;
	}

	if (ifp->physaddrlen != sdl->sdl_alen) {
		/* length changed */
		if (ifp->physaddrlen) {
			/* delete olf receive address */
			if ((rcv = mib_find_rcvaddr(ifp->index, ifp->physaddr,
			    ifp->physaddrlen)) != NULL)
				mib_rcvaddr_delete(rcv);
		}
		if ((np = realloc(ifp->physaddr, sdl->sdl_alen)) == NULL) {
			free(ifp->physaddr);
			ifp->physaddr = NULL;
			ifp->physaddrlen = 0;
			return;
		}
		ifp->physaddr = np;
		ifp->physaddrlen = sdl->sdl_alen;

	} else if (memcmp(ifp->physaddr, ptr, ifp->physaddrlen) == 0) {
		/* no change */
		return;

	} else {
		/* address changed */

		/* delete olf receive address */
		if ((rcv = mib_find_rcvaddr(ifp->index, ifp->physaddr,
		    ifp->physaddrlen)) != NULL)
			mib_rcvaddr_delete(rcv);
	}

	memcpy(ifp->physaddr, ptr, ifp->physaddrlen);

	/* make new receive address */
	if ((rcv = mib_rcvaddr_create(ifp, ifp->physaddr, ifp->physaddrlen)) != NULL)
		rcv->flags |= MIBRCVADDR_HW;
}

/*
 * Free an interface
 */
static void
mibif_free(struct mibif *ifp)
{
	struct mibif *ifp1;
	struct mibindexmap *map;
	struct mibifa *ifa, *ifa1;
	struct mibrcvaddr *rcv, *rcv1;
	struct mibarp *at, *at1;

	if (ifp->xnotify != NULL)
		(*ifp->xnotify)(ifp, MIBIF_NOTIFY_DESTROY, ifp->xnotify_data);

	(void)mib_ifstack_delete(ifp, NULL);
	(void)mib_ifstack_delete(NULL, ifp);

	TAILQ_REMOVE(&mibif_list, ifp, link);

	/* if this was the fastest interface - recompute this */
	if (ifp->mib.ifmd_data.ifi_baudrate == mibif_maxspeed) {
		mibif_maxspeed = ifp->mib.ifmd_data.ifi_baudrate;
		TAILQ_FOREACH(ifp1, &mibif_list, link)
			if (ifp1->mib.ifmd_data.ifi_baudrate > mibif_maxspeed)
				mibif_maxspeed =
				    ifp1->mib.ifmd_data.ifi_baudrate;
		mibif_reset_hc_timer();
	}

	if (ifp->alias != NULL) {
		free(ifp->alias);
		ifp->alias = NULL;
	}
	free(ifp->private);
	ifp->private = NULL;
	free(ifp->physaddr);
	ifp->physaddr = NULL;
	free(ifp->specmib);
	ifp->specmib = NULL;

	STAILQ_FOREACH(map, &mibindexmap_list, link)
		if (map->mibif == ifp) {
			map->mibif = NULL;
			break;
		}

	/* purge interface addresses */
	ifa = TAILQ_FIRST(&mibifa_list);
	while (ifa != NULL) {
		ifa1 = TAILQ_NEXT(ifa, link);
		if (ifa->ifindex == ifp->index)
			destroy_ifa(ifa);
		ifa = ifa1;
	}

	/* purge receive addresses */
	rcv = TAILQ_FIRST(&mibrcvaddr_list);
	while (rcv != NULL) {
		rcv1 = TAILQ_NEXT(rcv, link);
		if (rcv->ifindex == ifp->index)
			mib_rcvaddr_delete(rcv);
		rcv = rcv1;
	}

	/* purge ARP entries */
	at = TAILQ_FIRST(&mibarp_list);
	while (at != NULL) {
		at1 = TAILQ_NEXT(at, link);
		if (at->index.subs[0] == ifp->index)
			mib_arp_delete(at);
		at = at1;
	}

	free(ifp);
	ifp = NULL;
	mib_if_number--;
	mib_iftable_last_change = this_tick;
}

/*
 * Create a new interface
 */
static struct mibif *
mibif_create(u_int sysindex, const char *name)
{
	struct mibif *ifp;
	struct mibindexmap *map;

	if ((ifp = malloc(sizeof(*ifp))) == NULL) {
		syslog(LOG_WARNING, "%s: %m", __func__);
		return (NULL);
	}
	memset(ifp, 0, sizeof(*ifp));
	if ((ifp->private = malloc(sizeof(struct mibif_private))) == NULL) {
		syslog(LOG_WARNING, "%s: %m", __func__);
		free(ifp);
		return (NULL);
	}
	memset(ifp->private, 0, sizeof(struct mibif_private));

	ifp->sysindex = sysindex;
	strlcpy(ifp->name, name, sizeof(ifp->name));
	strlcpy(ifp->descr, name, sizeof(ifp->descr));
	ifp->spec_oid = oid_zeroDotZero;

	map = NULL;
	if (!mib_if_is_dyn(ifp->name)) {
		/* non-dynamic. look whether we know the interface */
		STAILQ_FOREACH(map, &mibindexmap_list, link)
			if (strcmp(map->name, ifp->name) == 0) {
				ifp->index = map->ifindex;
				map->mibif = ifp;
				break;
			}
		/* assume it has a connector if it is not dynamic */
		ifp->has_connector = 1;
		ifp->trap_enable = 1;
	}
	if (map == NULL) {
		/* new interface - get new index */
		if (next_if_index > 0x7fffffff)
			errx(1, "ifindex wrap");

		if ((map = malloc(sizeof(*map))) == NULL) {
			syslog(LOG_ERR, "ifmap: %m");
			free(ifp);
			return (NULL);
		}
		map->ifindex = next_if_index++;
		map->sysindex = ifp->sysindex;
		strcpy(map->name, ifp->name);
		map->mibif = ifp;
		STAILQ_INSERT_TAIL(&mibindexmap_list, map, link);
	} else {
		/* re-instantiate. Introduce a counter discontinuity */
		ifp->counter_disc = get_ticks();
	}
	ifp->index = map->ifindex;
	ifp->mib.ifmd_data.ifi_link_state = LINK_STATE_UNKNOWN;

	INSERT_OBJECT_INT(ifp, &mibif_list);
	mib_if_number++;
	mib_iftable_last_change = this_tick;

	/* instantiate default ifStack entries */
	(void)mib_ifstack_create(ifp, NULL);
	(void)mib_ifstack_create(NULL, ifp);

	return (ifp);
}

/*
 * Inform all interested parties about a new interface
 */
static void
notify_newif(struct mibif *ifp)
{
	struct newifreg *reg;

	TAILQ_FOREACH(reg, &newifreg_list, link)
		if ((*reg->func)(ifp))
			return;
}

/*
 * This is called for new interfaces after we have fetched the interface
 * MIB. If this is a broadcast interface try to guess the broadcast address
 * depending on the interface type.
 */
static void
check_llbcast(struct mibif *ifp)
{
	static u_char ether_bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	struct mibrcvaddr *rcv;

	if (!(ifp->mib.ifmd_flags & IFF_BROADCAST))
		return;

	switch (ifp->mib.ifmd_data.ifi_type) {

	  case IFT_ETHER:
	  case IFT_FDDI:
	  case IFT_ISO88025:
	  case IFT_L2VLAN:
		if (mib_find_rcvaddr(ifp->index, ether_bcast, 6) == NULL &&
		    (rcv = mib_rcvaddr_create(ifp, ether_bcast, 6)) != NULL)
			rcv->flags |= MIBRCVADDR_BCAST;
		break;
	}
}


/*
 * Retrieve the current interface list from the system.
 */
void
mib_refresh_iflist(void)
{
	struct mibif *ifp, *ifp1;
	size_t len;
	u_short idx;
	int name[6];
	int count;
	struct ifmibdata mib;

	TAILQ_FOREACH(ifp, &mibif_list, link)
		ifp->flags &= ~MIBIF_FOUND;

	len = sizeof(count);
	if (sysctlbyname("net.link.generic.system.ifcount", &count, &len,
	    NULL, 0) == -1) {
		syslog(LOG_ERR, "ifcount: %m");
		return;
	}
	name[0] = CTL_NET;
	name[1] = PF_LINK;
	name[2] = NETLINK_GENERIC;
	name[3] = IFMIB_IFDATA;
	name[5] = IFDATA_GENERAL;
	for (idx = 1; idx <= count; idx++) {
		name[4] = idx;
		len = sizeof(mib);
		if (sysctl(name, nitems(name), &mib, &len, NULL, 0) == -1) {
			if (errno == ENOENT)
				continue;
			syslog(LOG_ERR, "ifmib(%u): %m", idx);
			return;
		}
		if ((ifp = mib_find_if_sys(idx)) != NULL) {
			ifp->flags |= MIBIF_FOUND;
			continue;
		}
		/* Unknown interface - create */
		if ((ifp = mibif_create(idx, mib.ifmd_name)) != NULL) {
			ifp->flags |= MIBIF_FOUND;
			(void)mib_fetch_ifmib(ifp);
			check_llbcast(ifp);
			notify_newif(ifp);
		}
	}

	/*
	 * Purge interfaces that disappeared
	 */
	ifp = TAILQ_FIRST(&mibif_list);
	while (ifp != NULL) {
		ifp1 = TAILQ_NEXT(ifp, link);
		if (!(ifp->flags & MIBIF_FOUND))
			mibif_free(ifp);
		ifp = ifp1;
	}
}

/*
 * Find an interface address
 */
struct mibifa *
mib_find_ifa(struct in_addr addr)
{
	struct mibifa *ifa;

	TAILQ_FOREACH(ifa, &mibifa_list, link)
		if (ifa->inaddr.s_addr == addr.s_addr)
			return (ifa);
	return (NULL);
}

/*
 * Process a new ARP entry
 */
static void
process_arp(const struct rt_msghdr *rtm, const struct sockaddr_dl *sdl,
    const struct sockaddr_in *sa)
{
	struct mibif *ifp;
	struct mibarp *at;

	/* IP arp table entry */
	if (sdl->sdl_alen == 0)
		return;
	if ((ifp = mib_find_if_sys(sdl->sdl_index)) == NULL)
		return;
	/* have a valid entry */
	if ((at = mib_find_arp(ifp, sa->sin_addr)) == NULL &&
	    (at = mib_arp_create(ifp, sa->sin_addr,
	    sdl->sdl_data + sdl->sdl_nlen, sdl->sdl_alen)) == NULL)
		return;

	if (rtm->rtm_rmx.rmx_expire == 0)
		at->flags |= MIBARP_PERM;
	else
		at->flags &= ~MIBARP_PERM;
	at->flags |= MIBARP_FOUND;
}

/*
 * Handle a routing socket message.
 */
static void
handle_rtmsg(struct rt_msghdr *rtm)
{
	struct sockaddr *addrs[RTAX_MAX];
	struct if_msghdr *ifm;
	struct ifa_msghdr ifam, *ifamp;
	struct ifma_msghdr *ifmam;
#ifdef RTM_IFANNOUNCE
	struct if_announcemsghdr *ifan;
#endif
	struct mibif *ifp;
	struct sockaddr_dl *sdl;
	struct sockaddr_in *sa;
	struct mibifa *ifa;
	struct mibrcvaddr *rcv;
	u_char *ptr;

	if (rtm->rtm_version != RTM_VERSION) {
		syslog(LOG_ERR, "Bogus RTM version %u", rtm->rtm_version);
		return;
	}

	switch (rtm->rtm_type) {

	  case RTM_NEWADDR:
		ifamp = (struct ifa_msghdr *)rtm;
		memcpy(&ifam, ifamp, sizeof(ifam));
		mib_extract_addrs(ifam.ifam_addrs, (u_char *)(ifamp + 1), addrs);
		if (addrs[RTAX_IFA] == NULL || addrs[RTAX_NETMASK] == NULL)
			break;

		sa = (struct sockaddr_in *)(void *)addrs[RTAX_IFA];
		if ((ifa = mib_find_ifa(sa->sin_addr)) == NULL) {
			/* unknown address */
		    	if ((ifp = mib_find_if_sys(ifam.ifam_index)) == NULL) {
				syslog(LOG_WARNING, "RTM_NEWADDR for unknown "
				    "interface %u", ifam.ifam_index);
				break;
			}
		     	if ((ifa = alloc_ifa(ifp->index, sa->sin_addr)) == NULL)
				break;
		}
		sa = (struct sockaddr_in *)(void *)addrs[RTAX_NETMASK];
		ifa->inmask = sa->sin_addr;

		if (addrs[RTAX_BRD] != NULL) {
			sa = (struct sockaddr_in *)(void *)addrs[RTAX_BRD];
			ifa->inbcast = sa->sin_addr;
		}
		ifa->flags |= MIBIFA_FOUND;
		break;

	  case RTM_DELADDR:
		ifamp = (struct ifa_msghdr *)rtm;
		memcpy(&ifam, ifamp, sizeof(ifam));
		mib_extract_addrs(ifam.ifam_addrs, (u_char *)(ifamp + 1), addrs);
		if (addrs[RTAX_IFA] == NULL)
			break;

		sa = (struct sockaddr_in *)(void *)addrs[RTAX_IFA];
		if ((ifa = mib_find_ifa(sa->sin_addr)) != NULL) {
			ifa->flags |= MIBIFA_FOUND;
			if (!(ifa->flags & MIBIFA_DESTROYED))
				destroy_ifa(ifa);
		}
		break;

	  case RTM_NEWMADDR:
		ifmam = (struct ifma_msghdr *)rtm;
		mib_extract_addrs(ifmam->ifmam_addrs, (u_char *)(ifmam + 1), addrs);
		if (addrs[RTAX_IFA] == NULL ||
		    addrs[RTAX_IFA]->sa_family != AF_LINK)
			break;
		sdl = (struct sockaddr_dl *)(void *)addrs[RTAX_IFA];
		if ((rcv = mib_find_rcvaddr(sdl->sdl_index,
		    sdl->sdl_data + sdl->sdl_nlen, sdl->sdl_alen)) == NULL) {
			/* unknown address */
		    	if ((ifp = mib_find_if_sys(sdl->sdl_index)) == NULL) {
				syslog(LOG_WARNING, "RTM_NEWMADDR for unknown "
				    "interface %u", sdl->sdl_index);
				break;
			}
		     	if ((rcv = mib_rcvaddr_create(ifp,
			    sdl->sdl_data + sdl->sdl_nlen, sdl->sdl_alen)) == NULL)
				break;
			rcv->flags |= MIBRCVADDR_VOLATILE;
		}
		rcv->flags |= MIBRCVADDR_FOUND;
		break;

	  case RTM_DELMADDR:
		ifmam = (struct ifma_msghdr *)rtm;
		mib_extract_addrs(ifmam->ifmam_addrs, (u_char *)(ifmam + 1), addrs);
		if (addrs[RTAX_IFA] == NULL ||
		    addrs[RTAX_IFA]->sa_family != AF_LINK)
			break;
		sdl = (struct sockaddr_dl *)(void *)addrs[RTAX_IFA];
		if ((rcv = mib_find_rcvaddr(sdl->sdl_index,
		    sdl->sdl_data + sdl->sdl_nlen, sdl->sdl_alen)) != NULL)
			mib_rcvaddr_delete(rcv);
		break;

	  case RTM_IFINFO:
		ifm = (struct if_msghdr *)(void *)rtm;
		mib_extract_addrs(ifm->ifm_addrs, (u_char *)(ifm + 1), addrs);
		if ((ifp = mib_find_if_sys(ifm->ifm_index)) == NULL)
			break;
		if (addrs[RTAX_IFP] != NULL &&
		    addrs[RTAX_IFP]->sa_family == AF_LINK) {
			sdl = (struct sockaddr_dl *)(void *)addrs[RTAX_IFP];
			ptr = sdl->sdl_data + sdl->sdl_nlen;
			get_physaddr(ifp, sdl, ptr);
		}
		(void)mib_fetch_ifmib(ifp);
		break;

#ifdef RTM_IFANNOUNCE
	  case RTM_IFANNOUNCE:
		ifan = (struct if_announcemsghdr *)rtm;
		ifp = mib_find_if_sys(ifan->ifan_index);

		switch (ifan->ifan_what) {

		  case IFAN_ARRIVAL:
			if (ifp == NULL && (ifp = mibif_create(ifan->ifan_index,
			    ifan->ifan_name)) != NULL) {
				(void)mib_fetch_ifmib(ifp);
				check_llbcast(ifp);
				notify_newif(ifp);
			}
			break;

		  case IFAN_DEPARTURE:
			if (ifp != NULL)
				mibif_free(ifp);
			break;
		}
		break;
#endif
	  case RTM_GET:
	  case RTM_ADD:
		mib_extract_addrs(rtm->rtm_addrs, (u_char *)(rtm + 1), addrs);
		if (rtm->rtm_flags & RTF_LLINFO) {
			if (addrs[RTAX_DST] == NULL ||
			    addrs[RTAX_GATEWAY] == NULL ||
			    addrs[RTAX_DST]->sa_family != AF_INET ||
			    addrs[RTAX_GATEWAY]->sa_family != AF_LINK)
				break;
			process_arp(rtm,
			    (struct sockaddr_dl *)(void *)addrs[RTAX_GATEWAY],
			    (struct sockaddr_in *)(void *)addrs[RTAX_DST]);
		} else {
			if (rtm->rtm_errno == 0 && (rtm->rtm_flags & RTF_UP))
				mib_sroute_process(rtm, addrs[RTAX_GATEWAY],
				    addrs[RTAX_DST], addrs[RTAX_NETMASK]);
		}
		break;

	  case RTM_DELETE:
		mib_extract_addrs(rtm->rtm_addrs, (u_char *)(rtm + 1), addrs);

		if (rtm->rtm_errno == 0 && (rtm->rtm_flags & RTF_UP))
			mib_sroute_process(rtm, addrs[RTAX_GATEWAY],
			    addrs[RTAX_DST], addrs[RTAX_NETMASK]);
		break;
	}
}

/*
 * send a routing message
 */
void
mib_send_rtmsg(struct rt_msghdr *rtm, struct sockaddr *gw,
    struct sockaddr *dst, struct sockaddr *mask)
{
	size_t len;
	struct rt_msghdr *msg;
	char *cp;
	ssize_t sent;

	len = sizeof(*rtm) + SA_SIZE(gw) + SA_SIZE(dst) + SA_SIZE(mask);
	if ((msg = malloc(len)) == NULL) {
		syslog(LOG_ERR, "%s: %m", __func__);
		return;
	}
	cp = (char *)(msg + 1);

	memset(msg, 0, sizeof(*msg));
	msg->rtm_flags = 0;
	msg->rtm_version = RTM_VERSION;
	msg->rtm_addrs = RTA_DST | RTA_GATEWAY;

	memcpy(cp, dst, SA_SIZE(dst));
	cp += SA_SIZE(dst);
	memcpy(cp, gw, SA_SIZE(gw));
	cp += SA_SIZE(gw);
	if (mask != NULL) {
		memcpy(cp, mask, SA_SIZE(mask));
		cp += SA_SIZE(mask);
		msg->rtm_addrs |= RTA_NETMASK;
	}
	msg->rtm_msglen = cp - (char *)msg;
	msg->rtm_type = RTM_GET;
	if ((sent = write(route, msg, msg->rtm_msglen)) == -1) {
		syslog(LOG_ERR, "%s: write: %m", __func__);
		free(msg);
		return;
	}
	if (sent != msg->rtm_msglen) {
		syslog(LOG_ERR, "%s: short write", __func__);
		free(msg);
		return;
	}
	free(msg);
}

/*
 * Fetch the routing table via sysctl
 */
u_char *
mib_fetch_rtab(int af, int info, int arg, size_t *lenp)
{
	int name[6];
	u_char *buf, *newbuf;

	name[0] = CTL_NET;
	name[1] = PF_ROUTE;
	name[2] = 0;
	name[3] = af;
	name[4] = info;
	name[5] = arg;

	*lenp = 0;

	/* initial estimate */
	if (sysctl(name, nitems(name), NULL, lenp, NULL, 0) == -1) {
		syslog(LOG_ERR, "sysctl estimate (%d,%d,%d,%d,%d,%d): %m",
		    name[0], name[1], name[2], name[3], name[4], name[5]);
		return (NULL);
	}
	if (*lenp == 0)
		return (NULL);

	buf = NULL;
	for (;;) {
		if ((newbuf = realloc(buf, *lenp)) == NULL) {
			syslog(LOG_ERR, "sysctl buffer: %m");
			free(buf);
			return (NULL);
		}
		buf = newbuf;

		if (sysctl(name, nitems(name), buf, lenp, NULL, 0) == 0)
			break;

		if (errno != ENOMEM) {
			syslog(LOG_ERR, "sysctl get: %m");
			free(buf);
			return (NULL);
		}
		*lenp += *lenp / 8 + 1;
	}

	return (buf);
}

/*
 * Update the following info: interface, interface addresses, interface
 * receive addresses, arp-table.
 * This does not change the interface list itself.
 */
static void
update_ifa_info(void)
{
	u_char *buf, *next;
	struct rt_msghdr *rtm;
	struct mibifa *ifa, *ifa1;
	struct mibrcvaddr *rcv, *rcv1;
	size_t needed;
	static const int infos[][3] = {
		{ 0, NET_RT_IFLIST, 0 },
#ifdef NET_RT_IFMALIST
		{ AF_LINK, NET_RT_IFMALIST, 0 },
#endif
	};
	u_int i;

	TAILQ_FOREACH(ifa, &mibifa_list, link)
		ifa->flags &= ~MIBIFA_FOUND;
	TAILQ_FOREACH(rcv, &mibrcvaddr_list, link)
		rcv->flags &= ~MIBRCVADDR_FOUND;

	for (i = 0; i < sizeof(infos) / sizeof(infos[0]); i++) {
		if ((buf = mib_fetch_rtab(infos[i][0], infos[i][1], infos[i][2],
		   &needed)) == NULL)
			continue;

		next = buf;
		while (next < buf + needed) {
			rtm = (struct rt_msghdr *)(void *)next;
			next += rtm->rtm_msglen;
			handle_rtmsg(rtm);
		}
		free(buf);
	}

	/*
	 * Purge the address list of unused entries. These may happen for
	 * interface aliases that are on the same subnet. We don't receive
	 * routing socket messages for them.
	 */
	ifa = TAILQ_FIRST(&mibifa_list);
	while (ifa != NULL) {
		ifa1 = TAILQ_NEXT(ifa, link);
		if (!(ifa->flags & MIBIFA_FOUND))
			destroy_ifa(ifa);
		ifa = ifa1;
	}

	rcv = TAILQ_FIRST(&mibrcvaddr_list);
	while (rcv != NULL) {
		rcv1 = TAILQ_NEXT(rcv, link);
		if (!(rcv->flags & (MIBRCVADDR_FOUND | MIBRCVADDR_BCAST |
		    MIBRCVADDR_HW)))
			mib_rcvaddr_delete(rcv);
		rcv = rcv1;
	}
}

/*
 * Update arp table
 */
void
mib_arp_update(void)
{
	struct mibarp *at, *at1;
	size_t needed;
	u_char *buf, *next;
	struct rt_msghdr *rtm;

	if (in_update_arp)
		return;		/* Aaargh */
	in_update_arp = 1;

	TAILQ_FOREACH(at, &mibarp_list, link)
		at->flags &= ~MIBARP_FOUND;

	if ((buf = mib_fetch_rtab(AF_INET, NET_RT_FLAGS, 0, &needed)) == NULL) {
		in_update_arp = 0;
		return;
	}

	next = buf;
	while (next < buf + needed) {
		rtm = (struct rt_msghdr *)(void *)next;
		next += rtm->rtm_msglen;
		handle_rtmsg(rtm);
	}
	free(buf);

	at = TAILQ_FIRST(&mibarp_list);
	while (at != NULL) {
		at1 = TAILQ_NEXT(at, link);
		if (!(at->flags & MIBARP_FOUND))
			mib_arp_delete(at);
		at = at1;
	}
	mibarpticks = get_ticks();
	in_update_arp = 0;
}


/*
 * Input on the routing socket.
 */
static void
route_input(int fd, void *udata __unused)
{
	u_char	buf[1024 * 16];
	ssize_t n;
	struct rt_msghdr *rtm;

	if ((n = read(fd, buf, sizeof(buf))) == -1)
		err(1, "read(rt_socket)");

	if (n == 0)
		errx(1, "EOF on rt_socket");

	rtm = (struct rt_msghdr *)(void *)buf;
	if ((size_t)n != rtm->rtm_msglen)
		errx(1, "n=%zu, rtm_msglen=%u", (size_t)n, rtm->rtm_msglen);

	handle_rtmsg(rtm);
}

/*
 * execute and SIOCAIFADDR
 */
static int
siocaifaddr(char *ifname, struct in_addr addr, struct in_addr mask,
    struct in_addr bcast)
{
	struct ifaliasreq addreq;
	struct sockaddr_in *sa;

	memset(&addreq, 0, sizeof(addreq));
	strlcpy(addreq.ifra_name, ifname, sizeof(addreq.ifra_name));

	sa = (struct sockaddr_in *)(void *)&addreq.ifra_addr;
	sa->sin_family = AF_INET;
	sa->sin_len = sizeof(*sa);
	sa->sin_addr = addr;

	sa = (struct sockaddr_in *)(void *)&addreq.ifra_mask;
	sa->sin_family = AF_INET;
	sa->sin_len = sizeof(*sa);
	sa->sin_addr = mask;

	sa = (struct sockaddr_in *)(void *)&addreq.ifra_broadaddr;
	sa->sin_family = AF_INET;
	sa->sin_len = sizeof(*sa);
	sa->sin_addr = bcast;

	return (ioctl(mib_netsock, SIOCAIFADDR, &addreq));
}

/*
 * Exececute a SIOCDIFADDR
 */
static int
siocdifaddr(const char *ifname, struct in_addr addr)
{
	struct ifreq delreq;
	struct sockaddr_in *sa;

	memset(&delreq, 0, sizeof(delreq));
	strlcpy(delreq.ifr_name, ifname, sizeof(delreq.ifr_name));
	sa = (struct sockaddr_in *)(void *)&delreq.ifr_addr;
	sa->sin_family = AF_INET;
	sa->sin_len = sizeof(*sa);
	sa->sin_addr = addr;

	return (ioctl(mib_netsock, SIOCDIFADDR, &delreq));
}

/*
 * Verify an interface address without fetching the entire list
 */
static int
verify_ifa(const char *name, struct mibifa *ifa)
{
	struct ifreq req;
	struct sockaddr_in *sa;

	memset(&req, 0, sizeof(req));
	strlcpy(req.ifr_name, name, sizeof(req.ifr_name));
	sa = (struct sockaddr_in *)(void *)&req.ifr_addr;
	sa->sin_family = AF_INET;
	sa->sin_len = sizeof(*sa);
	sa->sin_addr = ifa->inaddr;

	if (ioctl(mib_netsock, SIOCGIFADDR, &req) == -1)
		return (-1);
	if (ifa->inaddr.s_addr != sa->sin_addr.s_addr) {
		syslog(LOG_ERR, "%s: address mismatch", __func__);
		return (-1);
	}

	if (ioctl(mib_netsock, SIOCGIFNETMASK, &req) == -1)
		return (-1);
	if (ifa->inmask.s_addr != sa->sin_addr.s_addr) {
		syslog(LOG_ERR, "%s: netmask mismatch", __func__);
		return (-1);
	}
	return (0);
}

/*
 * Restore a deleted interface address. Don't wait for the routing socket
 * to update us.
 */
void
mib_undestroy_ifa(struct mibifa *ifa)
{
	struct mibif *ifp;

	if ((ifp = mib_find_if(ifa->ifindex)) == NULL)
		/* keep it destroyed */
		return;

	if (siocaifaddr(ifp->name, ifa->inaddr, ifa->inmask, ifa->inbcast))
		/* keep it destroyed */
		return;

	ifa->flags &= ~MIBIFA_DESTROYED;
}

/*
 * Destroy an interface address
 */
int
mib_destroy_ifa(struct mibifa *ifa)
{
	struct mibif *ifp;

	if ((ifp = mib_find_if(ifa->ifindex)) == NULL) {
		/* ups. */
		mib_iflist_bad = 1;
		return (-1);
	}
	if (siocdifaddr(ifp->name, ifa->inaddr)) {
		/* ups. */
		syslog(LOG_ERR, "SIOCDIFADDR: %m");
		mib_iflist_bad = 1;
		return (-1);
	}
	ifa->flags |= MIBIFA_DESTROYED;
	return (0);
}

/*
 * Rollback the modification of an address. Don't bother to wait for
 * the routing socket.
 */
void
mib_unmodify_ifa(struct mibifa *ifa)
{
	struct mibif *ifp;

	if ((ifp = mib_find_if(ifa->ifindex)) == NULL) {
		/* ups. */
		mib_iflist_bad = 1;
		return;
	}

	if (siocaifaddr(ifp->name, ifa->inaddr, ifa->inmask, ifa->inbcast)) {
		/* ups. */
		mib_iflist_bad = 1;
		return;
	}
}

/*
 * Modify an IFA.
 */
int
mib_modify_ifa(struct mibifa *ifa)
{
	struct mibif *ifp;

	if ((ifp = mib_find_if(ifa->ifindex)) == NULL) {
		/* ups. */
		mib_iflist_bad = 1;
		return (-1);
	}

	if (siocaifaddr(ifp->name, ifa->inaddr, ifa->inmask, ifa->inbcast)) {
		/* ups. */
		mib_iflist_bad = 1;
		return (-1);
	}

	if (verify_ifa(ifp->name, ifa)) {
		/* ups. */
		mib_iflist_bad = 1;
		return (-1);
	}

	return (0);
}

/*
 * Destroy a freshly created interface address. Don't bother to wait for
 * the routing socket.
 */
void
mib_uncreate_ifa(struct mibifa *ifa)
{
	struct mibif *ifp;

	if ((ifp = mib_find_if(ifa->ifindex)) == NULL) {
		/* ups. */
		mib_iflist_bad = 1;
		return;
	}
	if (siocdifaddr(ifp->name, ifa->inaddr)) {
		/* ups. */
		mib_iflist_bad = 1;
		return;
	}

	destroy_ifa(ifa);
}

/*
 * Create a new ifa and verify it
 */
struct mibifa *
mib_create_ifa(u_int ifindex, struct in_addr addr, struct in_addr mask,
    struct in_addr bcast)
{
	struct mibif *ifp;
	struct mibifa *ifa;

	if ((ifp = mib_find_if(ifindex)) == NULL)
		return (NULL);
	if ((ifa = alloc_ifa(ifindex, addr)) == NULL)
		return (NULL);
	ifa->inmask = mask;
	ifa->inbcast = bcast;

	if (siocaifaddr(ifp->name, ifa->inaddr, ifa->inmask, ifa->inbcast)) {
		syslog(LOG_ERR, "%s: %m", __func__);
		destroy_ifa(ifa);
		return (NULL);
	}
	if (verify_ifa(ifp->name, ifa)) {
		destroy_ifa(ifa);
		return (NULL);
	}
	return (ifa);
}

/*
 * Get all cloning interfaces and make them dynamic.
 * Hah! Whe should probably do this on a periodic basis (XXX).
 */
static void
get_cloners(void)
{
	struct if_clonereq req;
	char *buf, *cp;
	int i;

	memset(&req, 0, sizeof(req));
	if (ioctl(mib_netsock, SIOCIFGCLONERS, &req) == -1) {
		syslog(LOG_ERR, "get cloners: %m");
		return;
	}
	if ((buf = malloc(req.ifcr_total * IFNAMSIZ)) == NULL) {
		syslog(LOG_ERR, "%m");
		return;
	}
	req.ifcr_count = req.ifcr_total;
	req.ifcr_buffer = buf;
	if (ioctl(mib_netsock, SIOCIFGCLONERS, &req) == -1) {
		syslog(LOG_ERR, "get cloners: %m");
		free(buf);
		return;
	}
	for (cp = buf, i = 0; i < req.ifcr_total; i++, cp += IFNAMSIZ)
		mib_if_set_dyn(cp);
	free(buf);
}

/*
 * Idle function
 */
static void
mibII_idle(void *arg __unused)
{
	struct mibifa *ifa;

	if (mib_iflist_bad) {
		TAILQ_FOREACH(ifa, &mibifa_list, link)
			ifa->flags &= ~MIBIFA_DESTROYED;

		/* assume, that all cloning interfaces are dynamic */
		get_cloners();

		mib_refresh_iflist();
		update_ifa_info();
		mib_arp_update();
		mib_iflist_bad = 0;
	}

	mib_arp_update();
}


/*
 * Start the module
 */
static void
mibII_start(void)
{
	if ((route_fd = fd_select(route, route_input, NULL, module)) == NULL) {
		syslog(LOG_ERR, "fd_select(route): %m");
		return;
	}
	mib_refresh_iflist();
	update_ifa_info();
	mib_arp_update();
	(void)mib_fetch_route();
	mib_iftable_last_change = 0;
	mib_ifstack_last_change = 0;

	ifmib_reg = or_register(&oid_ifMIB,
	    "The MIB module to describe generic objects for network interface"
	    " sub-layers.", module);

	ipmib_reg = or_register(&oid_ipMIB,
	   "The MIB module for managing IP and ICMP implementations, but "
	   "excluding their management of IP routes.", module);

	tcpmib_reg = or_register(&oid_tcpMIB,
	   "The MIB module for managing TCP implementations.", module);

	udpmib_reg = or_register(&oid_udpMIB,
	   "The MIB module for managing UDP implementations.", module);

	ipForward_reg = or_register(&oid_ipForward,
	   "The MIB module for the display of CIDR multipath IP Routes.",
	   module);

	mibII_poll_timer = NULL;
	mibII_poll_ticks = MIBII_POLL_TICKS;
	mibif_restart_mibII_poll_timer();
}

/*
 * Initialize the module
 */
static int
mibII_init(struct lmodule *mod, int argc __unused, char *argv[] __unused)
{
	size_t len;

	module = mod;

	len = sizeof(clockinfo);
	if (sysctlbyname("kern.clockrate", &clockinfo, &len, NULL, 0) == -1) {
		syslog(LOG_ERR, "kern.clockrate: %m");
		return (-1);
	}
	if (len != sizeof(clockinfo)) {
		syslog(LOG_ERR, "kern.clockrate: wrong size");
		return (-1);
	}

	if ((route = socket(PF_ROUTE, SOCK_RAW, AF_UNSPEC)) == -1) {
		syslog(LOG_ERR, "PF_ROUTE: %m");
		return (-1);
	}

	if ((mib_netsock = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
		syslog(LOG_ERR, "PF_INET: %m");
		(void)close(route);
		return (-1);
	}
	(void)shutdown(mib_netsock, SHUT_RDWR);

	/* assume, that all cloning interfaces are dynamic */
	get_cloners();

	return (0);
}

static int
mibII_fini(void)
{
	if (mibII_poll_timer != NULL ) {
		timer_stop(mibII_poll_timer);
		mibII_poll_timer = NULL;
	}

	if (route_fd != NULL)
		fd_deselect(route_fd);
	if (route != -1)
		(void)close(route);
	if (mib_netsock != -1)
		(void)close(mib_netsock);
	/* XXX free memory */

	or_unregister(ipForward_reg);
	or_unregister(udpmib_reg);
	or_unregister(tcpmib_reg);
	or_unregister(ipmib_reg);
	or_unregister(ifmib_reg);

	return (0);
}

static void
mibII_loading(const struct lmodule *mod, int loaded)
{
	struct mibif *ifp;

	if (loaded == 1)
		return;

	TAILQ_FOREACH(ifp, &mibif_list, link)
		if (ifp->xnotify_mod == mod) {
			ifp->xnotify_mod = NULL;
			ifp->xnotify_data = NULL;
			ifp->xnotify = NULL;
		}

	mib_unregister_newif(mod);
}

extern const struct snmp_module config;
const struct snmp_module config = {
	"This module implements the interface and ip groups.",
	mibII_init,
	mibII_fini,
	NULL,		/* idle */
	NULL,		/* dump */
	NULL,		/* config */
	mibII_start,
	NULL,
	mibII_ctree,
	mibII_CTREE_SIZE,
	mibII_loading
};

/*
 * Should have a list of these attached to each interface.
 */
void *
mibif_notify(struct mibif *ifp, const struct lmodule *mod,
    mibif_notify_f func, void *data)
{
	ifp->xnotify = func;
	ifp->xnotify_data = data;
	ifp->xnotify_mod = mod;

	return (ifp);
}

void
mibif_unnotify(void *arg)
{
	struct mibif *ifp = arg;

	ifp->xnotify = NULL;
	ifp->xnotify_data = NULL;
	ifp->xnotify_mod = NULL;
}
