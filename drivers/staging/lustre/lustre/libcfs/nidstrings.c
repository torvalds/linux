/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/libcfs/nidstrings.c
 *
 * Author: Phil Schwan <phil@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LNET

#include <linux/libcfs/libcfs.h>
#include <linux/lnet/lnet.h>

/* CAVEAT VENDITOR! Keep the canonical string representation of nets/nids
 * consistent in all conversion functions.  Some code fragments are copied
 * around for the sake of clarity...
 */

/* CAVEAT EMPTOR! Racey temporary buffer allocation!
 * Choose the number of nidstrings to support the MAXIMUM expected number of
 * concurrent users.  If there are more, the returned string will be volatile.
 * NB this number must allow for a process to be descheduled for a timeslice
 * between getting its string and using it.
 */

static char      libcfs_nidstrings[LNET_NIDSTR_COUNT][LNET_NIDSTR_SIZE];
static int       libcfs_nidstring_idx = 0;

static spinlock_t libcfs_nidstring_lock;

void libcfs_init_nidstrings (void)
{
	spin_lock_init(&libcfs_nidstring_lock);
}

# define NIDSTR_LOCK(f)   spin_lock_irqsave(&libcfs_nidstring_lock, f)
# define NIDSTR_UNLOCK(f) spin_unlock_irqrestore(&libcfs_nidstring_lock, f)

static char *
libcfs_next_nidstring (void)
{
	char	  *str;
	unsigned long  flags;

	NIDSTR_LOCK(flags);

	str = libcfs_nidstrings[libcfs_nidstring_idx++];
	if (libcfs_nidstring_idx ==
	    sizeof(libcfs_nidstrings)/sizeof(libcfs_nidstrings[0]))
		libcfs_nidstring_idx = 0;

	NIDSTR_UNLOCK(flags);
	return str;
}

static int  libcfs_lo_str2addr(const char *str, int nob, __u32 *addr);
static void libcfs_ip_addr2str(__u32 addr, char *str);
static int  libcfs_ip_str2addr(const char *str, int nob, __u32 *addr);
static void libcfs_decnum_addr2str(__u32 addr, char *str);
static void libcfs_hexnum_addr2str(__u32 addr, char *str);
static int  libcfs_num_str2addr(const char *str, int nob, __u32 *addr);
static int  libcfs_num_parse(char *str, int len, struct list_head *list);
static int  libcfs_num_match(__u32 addr, struct list_head *list);

struct netstrfns {
	int	  nf_type;
	char	*nf_name;
	char	*nf_modname;
	void       (*nf_addr2str)(__u32 addr, char *str);
	int	(*nf_str2addr)(const char *str, int nob, __u32 *addr);
	int	(*nf_parse_addrlist)(char *str, int len,
					struct list_head *list);
	int	(*nf_match_addr)(__u32 addr, struct list_head *list);
};

static struct netstrfns  libcfs_netstrfns[] = {
	{/* .nf_type      */  LOLND,
	 /* .nf_name      */  "lo",
	 /* .nf_modname   */  "klolnd",
	 /* .nf_addr2str  */  libcfs_decnum_addr2str,
	 /* .nf_str2addr  */  libcfs_lo_str2addr,
	 /* .nf_parse_addr*/  libcfs_num_parse,
	 /* .nf_match_addr*/  libcfs_num_match},
	{/* .nf_type      */  SOCKLND,
	 /* .nf_name      */  "tcp",
	 /* .nf_modname   */  "ksocklnd",
	 /* .nf_addr2str  */  libcfs_ip_addr2str,
	 /* .nf_str2addr  */  libcfs_ip_str2addr,
	 /* .nf_parse_addrlist*/  cfs_ip_addr_parse,
	 /* .nf_match_addr*/  cfs_ip_addr_match},
	{/* .nf_type      */  O2IBLND,
	 /* .nf_name      */  "o2ib",
	 /* .nf_modname   */  "ko2iblnd",
	 /* .nf_addr2str  */  libcfs_ip_addr2str,
	 /* .nf_str2addr  */  libcfs_ip_str2addr,
	 /* .nf_parse_addrlist*/  cfs_ip_addr_parse,
	 /* .nf_match_addr*/  cfs_ip_addr_match},
	{/* .nf_type      */  CIBLND,
	 /* .nf_name      */  "cib",
	 /* .nf_modname   */  "kciblnd",
	 /* .nf_addr2str  */  libcfs_ip_addr2str,
	 /* .nf_str2addr  */  libcfs_ip_str2addr,
	 /* .nf_parse_addrlist*/  cfs_ip_addr_parse,
	 /* .nf_match_addr*/  cfs_ip_addr_match},
	{/* .nf_type      */  OPENIBLND,
	 /* .nf_name      */  "openib",
	 /* .nf_modname   */  "kopeniblnd",
	 /* .nf_addr2str  */  libcfs_ip_addr2str,
	 /* .nf_str2addr  */  libcfs_ip_str2addr,
	 /* .nf_parse_addrlist*/  cfs_ip_addr_parse,
	 /* .nf_match_addr*/  cfs_ip_addr_match},
	{/* .nf_type      */  IIBLND,
	 /* .nf_name      */  "iib",
	 /* .nf_modname   */  "kiiblnd",
	 /* .nf_addr2str  */  libcfs_ip_addr2str,
	 /* .nf_str2addr  */  libcfs_ip_str2addr,
	 /* .nf_parse_addrlist*/  cfs_ip_addr_parse,
	 /* .nf_match_addr*/  cfs_ip_addr_match},
	{/* .nf_type      */  VIBLND,
	 /* .nf_name      */  "vib",
	 /* .nf_modname   */  "kviblnd",
	 /* .nf_addr2str  */  libcfs_ip_addr2str,
	 /* .nf_str2addr  */  libcfs_ip_str2addr,
	 /* .nf_parse_addrlist*/  cfs_ip_addr_parse,
	 /* .nf_match_addr*/  cfs_ip_addr_match},
	{/* .nf_type      */  RALND,
	 /* .nf_name      */  "ra",
	 /* .nf_modname   */  "kralnd",
	 /* .nf_addr2str  */  libcfs_ip_addr2str,
	 /* .nf_str2addr  */  libcfs_ip_str2addr,
	 /* .nf_parse_addrlist*/  cfs_ip_addr_parse,
	 /* .nf_match_addr*/  cfs_ip_addr_match},
	{/* .nf_type      */  QSWLND,
	 /* .nf_name      */  "elan",
	 /* .nf_modname   */  "kqswlnd",
	 /* .nf_addr2str  */  libcfs_decnum_addr2str,
	 /* .nf_str2addr  */  libcfs_num_str2addr,
	 /* .nf_parse_addrlist*/  libcfs_num_parse,
	 /* .nf_match_addr*/  libcfs_num_match},
	{/* .nf_type      */  GMLND,
	 /* .nf_name      */  "gm",
	 /* .nf_modname   */  "kgmlnd",
	 /* .nf_addr2str  */  libcfs_hexnum_addr2str,
	 /* .nf_str2addr  */  libcfs_num_str2addr,
	 /* .nf_parse_addrlist*/  libcfs_num_parse,
	 /* .nf_match_addr*/  libcfs_num_match},
	{/* .nf_type      */  MXLND,
	 /* .nf_name      */  "mx",
	 /* .nf_modname   */  "kmxlnd",
	 /* .nf_addr2str  */  libcfs_ip_addr2str,
	 /* .nf_str2addr  */  libcfs_ip_str2addr,
	 /* .nf_parse_addrlist*/  cfs_ip_addr_parse,
	 /* .nf_match_addr*/  cfs_ip_addr_match},
	{/* .nf_type      */  PTLLND,
	 /* .nf_name      */  "ptl",
	 /* .nf_modname   */  "kptllnd",
	 /* .nf_addr2str  */  libcfs_decnum_addr2str,
	 /* .nf_str2addr  */  libcfs_num_str2addr,
	 /* .nf_parse_addrlist*/  libcfs_num_parse,
	 /* .nf_match_addr*/  libcfs_num_match},
	{/* .nf_type      */  GNILND,
	 /* .nf_name      */  "gni",
	 /* .nf_modname   */  "kgnilnd",
	 /* .nf_addr2str  */  libcfs_decnum_addr2str,
	 /* .nf_str2addr  */  libcfs_num_str2addr,
	 /* .nf_parse_addrlist*/  libcfs_num_parse,
	 /* .nf_match_addr*/  libcfs_num_match},
	/* placeholder for net0 alias.  It MUST BE THE LAST ENTRY */
	{/* .nf_type      */  -1},
};

const int libcfs_nnetstrfns = sizeof(libcfs_netstrfns)/sizeof(libcfs_netstrfns[0]);

int
libcfs_lo_str2addr(const char *str, int nob, __u32 *addr)
{
	*addr = 0;
	return 1;
}

void
libcfs_ip_addr2str(__u32 addr, char *str)
{
#if 0   /* never lookup */
#endif
	snprintf(str, LNET_NIDSTR_SIZE, "%u.%u.%u.%u",
		 (addr >> 24) & 0xff, (addr >> 16) & 0xff,
		 (addr >> 8) & 0xff, addr & 0xff);
}

/* CAVEAT EMPTOR XscanfX
 * I use "%n" at the end of a sscanf format to detect trailing junk.  However
 * sscanf may return immediately if it sees the terminating '0' in a string, so
 * I initialise the %n variable to the expected length.  If sscanf sets it;
 * fine, if it doesn't, then the scan ended at the end of the string, which is
 * fine too :) */

int
libcfs_ip_str2addr(const char *str, int nob, __u32 *addr)
{
	int   a;
	int   b;
	int   c;
	int   d;
	int   n = nob;			  /* XscanfX */

	/* numeric IP? */
	if (sscanf(str, "%u.%u.%u.%u%n", &a, &b, &c, &d, &n) >= 4 &&
	    n == nob &&
	    (a & ~0xff) == 0 && (b & ~0xff) == 0 &&
	    (c & ~0xff) == 0 && (d & ~0xff) == 0) {
		*addr = ((a<<24)|(b<<16)|(c<<8)|d);
		return 1;
	}

	return 0;
}

void
libcfs_decnum_addr2str(__u32 addr, char *str)
{
	snprintf(str, LNET_NIDSTR_SIZE, "%u", addr);
}

void
libcfs_hexnum_addr2str(__u32 addr, char *str)
{
	snprintf(str, LNET_NIDSTR_SIZE, "0x%x", addr);
}

int
libcfs_num_str2addr(const char *str, int nob, __u32 *addr)
{
	int     n;

	n = nob;
	if (sscanf(str, "0x%x%n", addr, &n) >= 1 && n == nob)
		return 1;

	n = nob;
	if (sscanf(str, "0X%x%n", addr, &n) >= 1 && n == nob)
		return 1;

	n = nob;
	if (sscanf(str, "%u%n", addr, &n) >= 1 && n == nob)
		return 1;

	return 0;
}

struct netstrfns *
libcfs_lnd2netstrfns(int lnd)
{
	int    i;

	if (lnd >= 0)
		for (i = 0; i < libcfs_nnetstrfns; i++)
			if (lnd == libcfs_netstrfns[i].nf_type)
				return &libcfs_netstrfns[i];

	return NULL;
}

struct netstrfns *
libcfs_namenum2netstrfns(const char *name)
{
	struct netstrfns *nf;
	int	       i;

	for (i = 0; i < libcfs_nnetstrfns; i++) {
		nf = &libcfs_netstrfns[i];
		if (nf->nf_type >= 0 &&
		    !strncmp(name, nf->nf_name, strlen(nf->nf_name)))
			return nf;
	}
	return NULL;
}

struct netstrfns *
libcfs_name2netstrfns(const char *name)
{
	int    i;

	for (i = 0; i < libcfs_nnetstrfns; i++)
		if (libcfs_netstrfns[i].nf_type >= 0 &&
		    !strcmp(libcfs_netstrfns[i].nf_name, name))
			return &libcfs_netstrfns[i];

	return NULL;
}

int
libcfs_isknown_lnd(int type)
{
	return libcfs_lnd2netstrfns(type) != NULL;
}

char *
libcfs_lnd2modname(int lnd)
{
	struct netstrfns *nf = libcfs_lnd2netstrfns(lnd);

	return (nf == NULL) ? NULL : nf->nf_modname;
}

char *
libcfs_lnd2str(int lnd)
{
	char	   *str;
	struct netstrfns *nf = libcfs_lnd2netstrfns(lnd);

	if (nf != NULL)
		return nf->nf_name;

	str = libcfs_next_nidstring();
	snprintf(str, LNET_NIDSTR_SIZE, "?%u?", lnd);
	return str;
}

int
libcfs_str2lnd(const char *str)
{
	struct netstrfns *nf = libcfs_name2netstrfns(str);

	if (nf != NULL)
		return nf->nf_type;

	return -1;
}

char *
libcfs_net2str(__u32 net)
{
	int	       lnd = LNET_NETTYP(net);
	int	       num = LNET_NETNUM(net);
	struct netstrfns *nf  = libcfs_lnd2netstrfns(lnd);
	char	     *str = libcfs_next_nidstring();

	if (nf == NULL)
		snprintf(str, LNET_NIDSTR_SIZE, "<%u:%u>", lnd, num);
	else if (num == 0)
		snprintf(str, LNET_NIDSTR_SIZE, "%s", nf->nf_name);
	else
		snprintf(str, LNET_NIDSTR_SIZE, "%s%u", nf->nf_name, num);

	return str;
}

char *
libcfs_nid2str(lnet_nid_t nid)
{
	__u32	     addr = LNET_NIDADDR(nid);
	__u32	     net = LNET_NIDNET(nid);
	int	       lnd = LNET_NETTYP(net);
	int	       nnum = LNET_NETNUM(net);
	struct netstrfns *nf;
	char	     *str;
	int	       nob;

	if (nid == LNET_NID_ANY)
		return "<?>";

	nf = libcfs_lnd2netstrfns(lnd);
	str = libcfs_next_nidstring();

	if (nf == NULL)
		snprintf(str, LNET_NIDSTR_SIZE, "%x@<%u:%u>", addr, lnd, nnum);
	else {
		nf->nf_addr2str(addr, str);
		nob = strlen(str);
		if (nnum == 0)
			snprintf(str + nob, LNET_NIDSTR_SIZE - nob, "@%s",
				 nf->nf_name);
		else
			snprintf(str + nob, LNET_NIDSTR_SIZE - nob, "@%s%u",
				 nf->nf_name, nnum);
	}

	return str;
}

static struct netstrfns *
libcfs_str2net_internal(const char *str, __u32 *net)
{
	struct netstrfns *uninitialized_var(nf);
	int	       nob;
	int	       netnum;
	int	       i;

	for (i = 0; i < libcfs_nnetstrfns; i++) {
		nf = &libcfs_netstrfns[i];
		if (nf->nf_type >= 0 &&
		    !strncmp(str, nf->nf_name, strlen(nf->nf_name)))
			break;
	}

	if (i == libcfs_nnetstrfns)
		return NULL;

	nob = strlen(nf->nf_name);

	if (strlen(str) == (unsigned int)nob) {
		netnum = 0;
	} else {
		if (nf->nf_type == LOLND) /* net number not allowed */
			return NULL;

		str += nob;
		i = strlen(str);
		if (sscanf(str, "%u%n", &netnum, &i) < 1 ||
		    i != (int)strlen(str))
			return NULL;
	}

	*net = LNET_MKNET(nf->nf_type, netnum);
	return nf;
}

__u32
libcfs_str2net(const char *str)
{
	__u32  net;

	if (libcfs_str2net_internal(str, &net) != NULL)
		return net;

	return LNET_NIDNET(LNET_NID_ANY);
}

lnet_nid_t
libcfs_str2nid(const char *str)
{
	const char       *sep = strchr(str, '@');
	struct netstrfns *nf;
	__u32	     net;
	__u32	     addr;

	if (sep != NULL) {
		nf = libcfs_str2net_internal(sep + 1, &net);
		if (nf == NULL)
			return LNET_NID_ANY;
	} else {
		sep = str + strlen(str);
		net = LNET_MKNET(SOCKLND, 0);
		nf = libcfs_lnd2netstrfns(SOCKLND);
		LASSERT (nf != NULL);
	}

	if (!nf->nf_str2addr(str, (int)(sep - str), &addr))
		return LNET_NID_ANY;

	return LNET_MKNID(net, addr);
}

char *
libcfs_id2str(lnet_process_id_t id)
{
	char *str = libcfs_next_nidstring();

	if (id.pid == LNET_PID_ANY) {
		snprintf(str, LNET_NIDSTR_SIZE,
			 "LNET_PID_ANY-%s", libcfs_nid2str(id.nid));
		return str;
	}

	snprintf(str, LNET_NIDSTR_SIZE, "%s%u-%s",
		 ((id.pid & LNET_PID_USERFLAG) != 0) ? "U" : "",
		 (id.pid & ~LNET_PID_USERFLAG), libcfs_nid2str(id.nid));
	return str;
}

int
libcfs_str2anynid(lnet_nid_t *nidp, const char *str)
{
	if (!strcmp(str, "*")) {
		*nidp = LNET_NID_ANY;
		return 1;
	}

	*nidp = libcfs_str2nid(str);
	return *nidp != LNET_NID_ANY;
}

/**
 * Nid range list syntax.
 * \verbatim
 *
 * <nidlist>	 :== <nidrange> [ ' ' <nidrange> ]
 * <nidrange>	:== <addrrange> '@' <net>
 * <addrrange>       :== '*' |
 *		       <ipaddr_range> |
 *			 <cfs_expr_list>
 * <ipaddr_range>    :== <cfs_expr_list>.<cfs_expr_list>.<cfs_expr_list>.
 *			 <cfs_expr_list>
 * <cfs_expr_list>   :== <number> |
 *		       <expr_list>
 * <expr_list>       :== '[' <range_expr> [ ',' <range_expr>] ']'
 * <range_expr>      :== <number> |
 *		       <number> '-' <number> |
 *		       <number> '-' <number> '/' <number>
 * <net>	     :== <netname> | <netname><number>
 * <netname>	 :== "lo" | "tcp" | "o2ib" | "cib" | "openib" | "iib" |
 *		       "vib" | "ra" | "elan" | "mx" | "ptl"
 * \endverbatim
 */

/**
 * Structure to represent \<nidrange\> token of the syntax.
 *
 * One of this is created for each \<net\> parsed.
 */
struct nidrange {
	/**
	 * Link to list of this structures which is built on nid range
	 * list parsing.
	 */
	struct list_head nr_link;
	/**
	 * List head for addrrange::ar_link.
	 */
	struct list_head nr_addrranges;
	/**
	 * Flag indicating that *@<net> is found.
	 */
	int nr_all;
	/**
	 * Pointer to corresponding element of libcfs_netstrfns.
	 */
	struct netstrfns *nr_netstrfns;
	/**
	 * Number of network. E.g. 5 if \<net\> is "elan5".
	 */
	int nr_netnum;
};

/**
 * Structure to represent \<addrrange\> token of the syntax.
 */
struct addrrange {
	/**
	 * Link to nidrange::nr_addrranges.
	 */
	struct list_head ar_link;
	/**
	 * List head for cfs_expr_list::el_list.
	 */
	struct list_head ar_numaddr_ranges;
};

/**
 * Nf_parse_addrlist method for networks using numeric addresses.
 *
 * Examples of such networks are gm and elan.
 *
 * \retval 0 if \a str parsed to numeric address
 * \retval errno otherwise
 */
static int
libcfs_num_parse(char *str, int len, struct list_head *list)
{
	struct cfs_expr_list *el;
	int	rc;

	rc = cfs_expr_list_parse(str, len, 0, MAX_NUMERIC_VALUE, &el);
	if (rc == 0)
		list_add_tail(&el->el_link, list);

	return rc;
}

/**
 * Parses \<addrrange\> token on the syntax.
 *
 * Allocates struct addrrange and links to \a nidrange via
 * (nidrange::nr_addrranges)
 *
 * \retval 1 if \a src parses to '*' | \<ipaddr_range\> | \<cfs_expr_list\>
 * \retval 0 otherwise
 */
static int
parse_addrange(const struct cfs_lstr *src, struct nidrange *nidrange)
{
	struct addrrange *addrrange;

	if (src->ls_len == 1 && src->ls_str[0] == '*') {
		nidrange->nr_all = 1;
		return 1;
	}

	LIBCFS_ALLOC(addrrange, sizeof(struct addrrange));
	if (addrrange == NULL)
		return 0;
	list_add_tail(&addrrange->ar_link, &nidrange->nr_addrranges);
	INIT_LIST_HEAD(&addrrange->ar_numaddr_ranges);

	return nidrange->nr_netstrfns->nf_parse_addrlist(src->ls_str,
						src->ls_len,
						&addrrange->ar_numaddr_ranges);
}

/**
 * Finds or creates struct nidrange.
 *
 * Checks if \a src is a valid network name, looks for corresponding
 * nidrange on the ist of nidranges (\a nidlist), creates new struct
 * nidrange if it is not found.
 *
 * \retval pointer to struct nidrange matching network specified via \a src
 * \retval NULL if \a src does not match any network
 */
static struct nidrange *
add_nidrange(const struct cfs_lstr *src,
	     struct list_head *nidlist)
{
	struct netstrfns *nf;
	struct nidrange *nr;
	int endlen;
	unsigned netnum;

	if (src->ls_len >= LNET_NIDSTR_SIZE)
		return NULL;

	nf = libcfs_namenum2netstrfns(src->ls_str);
	if (nf == NULL)
		return NULL;
	endlen = src->ls_len - strlen(nf->nf_name);
	if (endlen == 0)
		/* network name only, e.g. "elan" or "tcp" */
		netnum = 0;
	else {
		/* e.g. "elan25" or "tcp23", refuse to parse if
		 * network name is not appended with decimal or
		 * hexadecimal number */
		if (!cfs_str2num_check(src->ls_str + strlen(nf->nf_name),
				       endlen, &netnum, 0, MAX_NUMERIC_VALUE))
			return NULL;
	}

	list_for_each_entry(nr, nidlist, nr_link) {
		if (nr->nr_netstrfns != nf)
			continue;
		if (nr->nr_netnum != netnum)
			continue;
		return nr;
	}

	LIBCFS_ALLOC(nr, sizeof(struct nidrange));
	if (nr == NULL)
		return NULL;
	list_add_tail(&nr->nr_link, nidlist);
	INIT_LIST_HEAD(&nr->nr_addrranges);
	nr->nr_netstrfns = nf;
	nr->nr_all = 0;
	nr->nr_netnum = netnum;

	return nr;
}

/**
 * Parses \<nidrange\> token of the syntax.
 *
 * \retval 1 if \a src parses to \<addrrange\> '@' \<net\>
 * \retval 0 otherwise
 */
static int
parse_nidrange(struct cfs_lstr *src, struct list_head *nidlist)
{
	struct cfs_lstr addrrange;
	struct cfs_lstr net;
	struct cfs_lstr tmp;
	struct nidrange *nr;

	tmp = *src;
	if (cfs_gettok(src, '@', &addrrange) == 0)
		goto failed;

	if (cfs_gettok(src, '@', &net) == 0 || src->ls_str != NULL)
		goto failed;

	nr = add_nidrange(&net, nidlist);
	if (nr == NULL)
		goto failed;

	if (parse_addrange(&addrrange, nr) != 0)
		goto failed;

	return 1;
 failed:
	CWARN("can't parse nidrange: \"%.*s\"\n", tmp.ls_len, tmp.ls_str);
	return 0;
}

/**
 * Frees addrrange structures of \a list.
 *
 * For each struct addrrange structure found on \a list it frees
 * cfs_expr_list list attached to it and frees the addrrange itself.
 *
 * \retval none
 */
static void
free_addrranges(struct list_head *list)
{
	while (!list_empty(list)) {
		struct addrrange *ar;

		ar = list_entry(list->next, struct addrrange, ar_link);

		cfs_expr_list_free_list(&ar->ar_numaddr_ranges);
		list_del(&ar->ar_link);
		LIBCFS_FREE(ar, sizeof(struct addrrange));
	}
}

/**
 * Frees nidrange strutures of \a list.
 *
 * For each struct nidrange structure found on \a list it frees
 * addrrange list attached to it and frees the nidrange itself.
 *
 * \retval none
 */
void
cfs_free_nidlist(struct list_head *list)
{
	struct list_head *pos, *next;
	struct nidrange *nr;

	list_for_each_safe(pos, next, list) {
		nr = list_entry(pos, struct nidrange, nr_link);
		free_addrranges(&nr->nr_addrranges);
		list_del(pos);
		LIBCFS_FREE(nr, sizeof(struct nidrange));
	}
}

/**
 * Parses nid range list.
 *
 * Parses with rigorous syntax and overflow checking \a str into
 * \<nidrange\> [ ' ' \<nidrange\> ], compiles \a str into set of
 * structures and links that structure to \a nidlist. The resulting
 * list can be used to match a NID againts set of NIDS defined by \a
 * str.
 * \see cfs_match_nid
 *
 * \retval 1 on success
 * \retval 0 otherwise
 */
int
cfs_parse_nidlist(char *str, int len, struct list_head *nidlist)
{
	struct cfs_lstr src;
	struct cfs_lstr res;
	int rc;
	ENTRY;

	src.ls_str = str;
	src.ls_len = len;
	INIT_LIST_HEAD(nidlist);
	while (src.ls_str) {
		rc = cfs_gettok(&src, ' ', &res);
		if (rc == 0) {
			cfs_free_nidlist(nidlist);
			RETURN(0);
		}
		rc = parse_nidrange(&res, nidlist);
		if (rc == 0) {
			cfs_free_nidlist(nidlist);
			RETURN(0);
		}
	}
	RETURN(1);
}

/*
 * Nf_match_addr method for networks using numeric addresses
 *
 * \retval 1 on match
 * \retval 0 otherwise
 */
static int
libcfs_num_match(__u32 addr, struct list_head *numaddr)
{
	struct cfs_expr_list *el;

	LASSERT(!list_empty(numaddr));
	el = list_entry(numaddr->next, struct cfs_expr_list, el_link);

	return cfs_expr_list_match(addr, el);
}

/**
 * Matches a nid (\a nid) against the compiled list of nidranges (\a nidlist).
 *
 * \see cfs_parse_nidlist()
 *
 * \retval 1 on match
 * \retval 0  otherwises
 */
int cfs_match_nid(lnet_nid_t nid, struct list_head *nidlist)
{
	struct nidrange *nr;
	struct addrrange *ar;
	ENTRY;

	list_for_each_entry(nr, nidlist, nr_link) {
		if (nr->nr_netstrfns->nf_type != LNET_NETTYP(LNET_NIDNET(nid)))
			continue;
		if (nr->nr_netnum != LNET_NETNUM(LNET_NIDNET(nid)))
			continue;
		if (nr->nr_all)
			RETURN(1);
		list_for_each_entry(ar, &nr->nr_addrranges, ar_link)
			if (nr->nr_netstrfns->nf_match_addr(LNET_NIDADDR(nid),
						       &ar->ar_numaddr_ranges))
				RETURN(1);
	}
	RETURN(0);
}


EXPORT_SYMBOL(libcfs_isknown_lnd);
EXPORT_SYMBOL(libcfs_lnd2modname);
EXPORT_SYMBOL(libcfs_lnd2str);
EXPORT_SYMBOL(libcfs_str2lnd);
EXPORT_SYMBOL(libcfs_net2str);
EXPORT_SYMBOL(libcfs_nid2str);
EXPORT_SYMBOL(libcfs_str2net);
EXPORT_SYMBOL(libcfs_str2nid);
EXPORT_SYMBOL(libcfs_id2str);
EXPORT_SYMBOL(libcfs_str2anynid);
EXPORT_SYMBOL(cfs_free_nidlist);
EXPORT_SYMBOL(cfs_parse_nidlist);
EXPORT_SYMBOL(cfs_match_nid);
