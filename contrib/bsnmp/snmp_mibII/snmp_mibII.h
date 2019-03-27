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
 * $Begemot: bsnmp/snmp_mibII/snmp_mibII.h,v 1.18 2006/02/14 09:04:19 brandt_h Exp $
 *
 * Implementation of the interfaces and IP groups of MIB-II.
 */
#ifndef snmp_mibII_h_
#define snmp_mibII_h_

/* forward declaration */
struct mibif;

enum mibif_notify {
	MIBIF_NOTIFY_DESTROY
};

typedef void (*mibif_notify_f)(struct mibif *, enum mibif_notify, void *);

/*
 * Interfaces. This structure describes one interface as seen in the MIB.
 * Interfaces are indexed by ifindex. This is not the same as the index
 * used by the system because of the rules in RFC-2863 section 3.1.5. This
 * RFC requires, that an ifindex is not to be re-used for ANOTHER dynamically
 * interfaces once the interface was deleted. The system's ifindex is in
 * sysindex. Mapping is via the mapping table below.
 */
struct mibif {
	TAILQ_ENTRY(mibif) link;
	u_int		flags;
	u_int		index;		/* the logical ifindex */
	u_int		sysindex;
	char		name[IFNAMSIZ];
	char		descr[256];
	struct ifmibdata mib;
	uint64_t	mibtick;
	void		*specmib;
	size_t		specmiblen;
	u_char		*physaddr;
	u_int		physaddrlen;
	int		has_connector;
	int		trap_enable;
	uint64_t	counter_disc;

	/*
	 * This is needed to handle interface type specific information
	 * in sub-modules. It contains a function pointer which handles
	 * notifications and a data pointer to arbitrary data.
	 * Should be set via the mibif_notify function.
	 */
	mibif_notify_f	xnotify;
	void		*xnotify_data;
	const struct lmodule *xnotify_mod;

	/* to be set by ifType specific modules. This is ifSpecific. */
	struct asn_oid	spec_oid;

	char		*alias;
	size_t		alias_size;

	/* private data - don't touch */
	void		*private;
};

/*
 * Interface IP-address table.
 */
struct mibifa {
	TAILQ_ENTRY(mibifa) link;
	struct in_addr	inaddr;
	struct in_addr	inmask;
	struct in_addr	inbcast;
	struct asn_oid	index;		/* index for table search */
	u_int		ifindex;
	u_int		flags;
};

/*
 * Interface receive addresses. Interface link-level multicast, broadcast
 * and hardware addresses are handled automatically.
 */
struct mibrcvaddr {
	TAILQ_ENTRY(mibrcvaddr) link;
	struct asn_oid	index;
	u_int		ifindex;
	u_char		addr[ASN_MAXOIDLEN];
	size_t		addrlen;
	u_int		flags;
};
enum {
	MIBRCVADDR_VOLATILE	= 0x00000001,
	MIBRCVADDR_BCAST	= 0x00000002,
	MIBRCVADDR_HW		= 0x00000004,
};

/* network socket */
extern int mib_netsock;

/* set an interface name to dynamic mode */
void mib_if_set_dyn(const char *);

/* re-read the systems interface list */
void mib_refresh_iflist(void);

/* find interface by index */
struct mibif *mib_find_if(u_int);
struct mibif *mib_find_if_sys(u_int);
struct mibif *mib_find_if_name(const char *);

/* iterate through all interfaces */
struct mibif *mib_first_if(void);
struct mibif *mib_next_if(const struct mibif *);

/* register for interface creations */
int mib_register_newif(int (*)(struct mibif *), const struct lmodule *);
void mib_unregister_newif(const struct lmodule *);

/* get fresh MIB data */
int mib_fetch_ifmib(struct mibif *);

/* change the ADMIN status of an interface and refresh the MIB */
int mib_if_admin(struct mibif *, int up);

/* find interface address by address */
struct mibifa *mib_find_ifa(struct in_addr);

/* find first/next address for a given interface */
struct mibifa *mib_first_ififa(const struct mibif *);
struct mibifa *mib_next_ififa(struct mibifa *);

/* create/delete stacking entries */
int mib_ifstack_create(const struct mibif *lower, const struct mibif *upper);
void mib_ifstack_delete(const struct mibif *lower, const struct mibif *upper);

/* find receive address */
struct mibrcvaddr *mib_find_rcvaddr(u_int, const u_char *, size_t);

/* create/delete receive addresses */
struct mibrcvaddr *mib_rcvaddr_create(struct mibif *, const u_char *, size_t);
void mib_rcvaddr_delete(struct mibrcvaddr *);

/* register for interface notification */
void *mibif_notify(struct mibif *, const struct lmodule *, mibif_notify_f,
    void *);
void mibif_unnotify(void *);

#endif
