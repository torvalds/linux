// SPDX-License-Identifier: GPL-2.0
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
 * http://www.gnu.org/licenses/gpl-2.0.html
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
 * lnet/lnet/nidstrings.c
 *
 * Author: Phil Schwan <phil@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LNET

#include <linux/libcfs/libcfs.h>
#include <uapi/linux/lnet/nidstr.h>

/* max value for numeric network address */
#define MAX_NUMERIC_VALUE 0xffffffff

#define IPSTRING_LENGTH 16

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
static int       libcfs_nidstring_idx;

static DEFINE_SPINLOCK(libcfs_nidstring_lock);

static struct netstrfns *libcfs_namenum2netstrfns(const char *name);

char *
libcfs_next_nidstring(void)
{
	char *str;
	unsigned long flags;

	spin_lock_irqsave(&libcfs_nidstring_lock, flags);

	str = libcfs_nidstrings[libcfs_nidstring_idx++];
	if (libcfs_nidstring_idx == ARRAY_SIZE(libcfs_nidstrings))
		libcfs_nidstring_idx = 0;

	spin_unlock_irqrestore(&libcfs_nidstring_lock, flags);
	return str;
}
EXPORT_SYMBOL(libcfs_next_nidstring);

/**
 * Nid range list syntax.
 * \verbatim
 *
 * <nidlist>         :== <nidrange> [ ' ' <nidrange> ]
 * <nidrange>        :== <addrrange> '@' <net>
 * <addrrange>       :== '*' |
 *                       <ipaddr_range> |
 *			 <cfs_expr_list>
 * <ipaddr_range>    :== <cfs_expr_list>.<cfs_expr_list>.<cfs_expr_list>.
 *			 <cfs_expr_list>
 * <cfs_expr_list>   :== <number> |
 *                       <expr_list>
 * <expr_list>       :== '[' <range_expr> [ ',' <range_expr>] ']'
 * <range_expr>      :== <number> |
 *                       <number> '-' <number> |
 *                       <number> '-' <number> '/' <number>
 * <net>             :== <netname> | <netname><number>
 * <netname>         :== "lo" | "tcp" | "o2ib" | "cib" | "openib" | "iib" |
 *                       "vib" | "ra" | "elan" | "mx" | "ptl"
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
 * Parses \<addrrange\> token on the syntax.
 *
 * Allocates struct addrrange and links to \a nidrange via
 * (nidrange::nr_addrranges)
 *
 * \retval 0 if \a src parses to '*' | \<ipaddr_range\> | \<cfs_expr_list\>
 * \retval -errno otherwise
 */
static int
parse_addrange(const struct cfs_lstr *src, struct nidrange *nidrange)
{
	struct addrrange *addrrange;

	if (src->ls_len == 1 && src->ls_str[0] == '*') {
		nidrange->nr_all = 1;
		return 0;
	}

	addrrange = kzalloc(sizeof(struct addrrange), GFP_NOFS);
	if (!addrrange)
		return -ENOMEM;
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
	unsigned int netnum;

	if (src->ls_len >= LNET_NIDSTR_SIZE)
		return NULL;

	nf = libcfs_namenum2netstrfns(src->ls_str);
	if (!nf)
		return NULL;
	endlen = src->ls_len - strlen(nf->nf_name);
	if (!endlen)
		/* network name only, e.g. "elan" or "tcp" */
		netnum = 0;
	else {
		/*
		 * e.g. "elan25" or "tcp23", refuse to parse if
		 * network name is not appended with decimal or
		 * hexadecimal number
		 */
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

	nr = kzalloc(sizeof(struct nidrange), GFP_NOFS);
	if (!nr)
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
	struct nidrange *nr;

	if (!cfs_gettok(src, '@', &addrrange))
		goto failed;

	if (!cfs_gettok(src, '@', &net) || src->ls_str)
		goto failed;

	nr = add_nidrange(&net, nidlist);
	if (!nr)
		goto failed;

	if (parse_addrange(&addrrange, nr))
		goto failed;

	return 1;
failed:
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
		kfree(ar);
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
		kfree(nr);
	}
}
EXPORT_SYMBOL(cfs_free_nidlist);

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

	src.ls_str = str;
	src.ls_len = len;
	INIT_LIST_HEAD(nidlist);
	while (src.ls_str) {
		rc = cfs_gettok(&src, ' ', &res);
		if (!rc) {
			cfs_free_nidlist(nidlist);
			return 0;
		}
		rc = parse_nidrange(&res, nidlist);
		if (!rc) {
			cfs_free_nidlist(nidlist);
			return 0;
		}
	}
	return 1;
}
EXPORT_SYMBOL(cfs_parse_nidlist);

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

	list_for_each_entry(nr, nidlist, nr_link) {
		if (nr->nr_netstrfns->nf_type != LNET_NETTYP(LNET_NIDNET(nid)))
			continue;
		if (nr->nr_netnum != LNET_NETNUM(LNET_NIDNET(nid)))
			continue;
		if (nr->nr_all)
			return 1;
		list_for_each_entry(ar, &nr->nr_addrranges, ar_link)
			if (nr->nr_netstrfns->nf_match_addr(LNET_NIDADDR(nid),
							    &ar->ar_numaddr_ranges))
				return 1;
	}
	return 0;
}
EXPORT_SYMBOL(cfs_match_nid);

/**
 * Print the network part of the nidrange \a nr into the specified \a buffer.
 *
 * \retval number of characters written
 */
static int
cfs_print_network(char *buffer, int count, struct nidrange *nr)
{
	struct netstrfns *nf = nr->nr_netstrfns;

	if (!nr->nr_netnum)
		return scnprintf(buffer, count, "@%s", nf->nf_name);
	else
		return scnprintf(buffer, count, "@%s%u",
				 nf->nf_name, nr->nr_netnum);
}

/**
 * Print a list of addrrange (\a addrranges) into the specified \a buffer.
 * At max \a count characters can be printed into \a buffer.
 *
 * \retval number of characters written
 */
static int
cfs_print_addrranges(char *buffer, int count, struct list_head *addrranges,
		     struct nidrange *nr)
{
	int i = 0;
	struct addrrange *ar;
	struct netstrfns *nf = nr->nr_netstrfns;

	list_for_each_entry(ar, addrranges, ar_link) {
		if (i)
			i += scnprintf(buffer + i, count - i, " ");
		i += nf->nf_print_addrlist(buffer + i, count - i,
					   &ar->ar_numaddr_ranges);
		i += cfs_print_network(buffer + i, count - i, nr);
	}
	return i;
}

/**
 * Print a list of nidranges (\a nidlist) into the specified \a buffer.
 * At max \a count characters can be printed into \a buffer.
 * Nidranges are separated by a space character.
 *
 * \retval number of characters written
 */
int cfs_print_nidlist(char *buffer, int count, struct list_head *nidlist)
{
	int i = 0;
	struct nidrange *nr;

	if (count <= 0)
		return 0;

	list_for_each_entry(nr, nidlist, nr_link) {
		if (i)
			i += scnprintf(buffer + i, count - i, " ");

		if (nr->nr_all) {
			LASSERT(list_empty(&nr->nr_addrranges));
			i += scnprintf(buffer + i, count - i, "*");
			i += cfs_print_network(buffer + i, count - i, nr);
		} else {
			i += cfs_print_addrranges(buffer + i, count - i,
						  &nr->nr_addrranges, nr);
		}
	}
	return i;
}
EXPORT_SYMBOL(cfs_print_nidlist);

/**
 * Determines minimum and maximum addresses for a single
 * numeric address range
 *
 * \param	ar
 * \param	min_nid
 * \param	max_nid
 */
static void cfs_ip_ar_min_max(struct addrrange *ar, __u32 *min_nid,
			      __u32 *max_nid)
{
	struct cfs_expr_list *el;
	struct cfs_range_expr *re;
	__u32 tmp_ip_addr = 0;
	unsigned int min_ip[4] = {0};
	unsigned int max_ip[4] = {0};
	int re_count = 0;

	list_for_each_entry(el, &ar->ar_numaddr_ranges, el_link) {
		list_for_each_entry(re, &el->el_exprs, re_link) {
			min_ip[re_count] = re->re_lo;
			max_ip[re_count] = re->re_hi;
			re_count++;
		}
	}

	tmp_ip_addr = ((min_ip[0] << 24) | (min_ip[1] << 16) |
		       (min_ip[2] << 8) | min_ip[3]);

	if (min_nid)
		*min_nid = tmp_ip_addr;

	tmp_ip_addr = ((max_ip[0] << 24) | (max_ip[1] << 16) |
		       (max_ip[2] << 8) | max_ip[3]);

	if (max_nid)
		*max_nid = tmp_ip_addr;
}

/**
 * Determines minimum and maximum addresses for a single
 * numeric address range
 *
 * \param	ar
 * \param	min_nid
 * \param	max_nid
 */
static void cfs_num_ar_min_max(struct addrrange *ar, __u32 *min_nid,
			       __u32 *max_nid)
{
	struct cfs_expr_list *el;
	struct cfs_range_expr *re;
	unsigned int min_addr = 0;
	unsigned int max_addr = 0;

	list_for_each_entry(el, &ar->ar_numaddr_ranges, el_link) {
		list_for_each_entry(re, &el->el_exprs, re_link) {
			if (re->re_lo < min_addr || !min_addr)
				min_addr = re->re_lo;
			if (re->re_hi > max_addr)
				max_addr = re->re_hi;
		}
	}

	if (min_nid)
		*min_nid = min_addr;
	if (max_nid)
		*max_nid = max_addr;
}

/**
 * Determines whether an expression list in an nidrange contains exactly
 * one contiguous address range. Calls the correct netstrfns for the LND
 *
 * \param	*nidlist
 *
 * \retval	true if contiguous
 * \retval	false if not contiguous
 */
bool cfs_nidrange_is_contiguous(struct list_head *nidlist)
{
	struct nidrange *nr;
	struct netstrfns *nf = NULL;
	char *lndname = NULL;
	int netnum = -1;

	list_for_each_entry(nr, nidlist, nr_link) {
		nf = nr->nr_netstrfns;
		if (!lndname)
			lndname = nf->nf_name;
		if (netnum == -1)
			netnum = nr->nr_netnum;

		if (strcmp(lndname, nf->nf_name) ||
		    netnum != nr->nr_netnum)
			return false;
	}

	if (!nf)
		return false;

	if (!nf->nf_is_contiguous(nidlist))
		return false;

	return true;
}
EXPORT_SYMBOL(cfs_nidrange_is_contiguous);

/**
 * Determines whether an expression list in an num nidrange contains exactly
 * one contiguous address range.
 *
 * \param	*nidlist
 *
 * \retval	true if contiguous
 * \retval	false if not contiguous
 */
static bool cfs_num_is_contiguous(struct list_head *nidlist)
{
	struct nidrange *nr;
	struct addrrange *ar;
	struct cfs_expr_list *el;
	struct cfs_range_expr *re;
	int last_hi = 0;
	__u32 last_end_nid = 0;
	__u32 current_start_nid = 0;
	__u32 current_end_nid = 0;

	list_for_each_entry(nr, nidlist, nr_link) {
		list_for_each_entry(ar, &nr->nr_addrranges, ar_link) {
			cfs_num_ar_min_max(ar, &current_start_nid,
					   &current_end_nid);
			if (last_end_nid &&
			    (current_start_nid - last_end_nid != 1))
				return false;
			last_end_nid = current_end_nid;
			list_for_each_entry(el, &ar->ar_numaddr_ranges,
					    el_link) {
				list_for_each_entry(re, &el->el_exprs,
						    re_link) {
					if (re->re_stride > 1)
						return false;
					else if (last_hi &&
						 re->re_hi - last_hi != 1)
						return false;
					last_hi = re->re_hi;
				}
			}
		}
	}

	return true;
}

/**
 * Determines whether an expression list in an ip nidrange contains exactly
 * one contiguous address range.
 *
 * \param	*nidlist
 *
 * \retval	true if contiguous
 * \retval	false if not contiguous
 */
static bool cfs_ip_is_contiguous(struct list_head *nidlist)
{
	struct nidrange *nr;
	struct addrrange *ar;
	struct cfs_expr_list *el;
	struct cfs_range_expr *re;
	int expr_count;
	int last_hi = 255;
	int last_diff = 0;
	__u32 last_end_nid = 0;
	__u32 current_start_nid = 0;
	__u32 current_end_nid = 0;

	list_for_each_entry(nr, nidlist, nr_link) {
		list_for_each_entry(ar, &nr->nr_addrranges, ar_link) {
			last_hi = 255;
			last_diff = 0;
			cfs_ip_ar_min_max(ar, &current_start_nid,
					  &current_end_nid);
			if (last_end_nid &&
			    (current_start_nid - last_end_nid != 1))
				return false;
			last_end_nid = current_end_nid;
			list_for_each_entry(el, &ar->ar_numaddr_ranges,
					    el_link) {
				expr_count = 0;
				list_for_each_entry(re, &el->el_exprs,
						    re_link) {
					expr_count++;
					if (re->re_stride > 1 ||
					    (last_diff > 0 && last_hi != 255) ||
					    (last_diff > 0 && last_hi == 255 &&
					     re->re_lo > 0))
						return false;
					last_hi = re->re_hi;
					last_diff = re->re_hi - re->re_lo;
				}
			}
		}
	}

	return true;
}

/**
 * Takes a linked list of nidrange expressions, determines the minimum
 * and maximum nid and creates appropriate nid structures
 *
 * \param	*nidlist
 * \param	*min_nid
 * \param	*max_nid
 */
void cfs_nidrange_find_min_max(struct list_head *nidlist, char *min_nid,
			       char *max_nid, size_t nidstr_length)
{
	struct nidrange *nr;
	struct netstrfns *nf = NULL;
	int netnum = -1;
	__u32 min_addr;
	__u32 max_addr;
	char *lndname = NULL;
	char min_addr_str[IPSTRING_LENGTH];
	char max_addr_str[IPSTRING_LENGTH];

	list_for_each_entry(nr, nidlist, nr_link) {
		nf = nr->nr_netstrfns;
		lndname = nf->nf_name;
		if (netnum == -1)
			netnum = nr->nr_netnum;

		nf->nf_min_max(nidlist, &min_addr, &max_addr);
	}
	nf->nf_addr2str(min_addr, min_addr_str, sizeof(min_addr_str));
	nf->nf_addr2str(max_addr, max_addr_str, sizeof(max_addr_str));

	snprintf(min_nid, nidstr_length, "%s@%s%d", min_addr_str, lndname,
		 netnum);
	snprintf(max_nid, nidstr_length, "%s@%s%d", max_addr_str, lndname,
		 netnum);
}
EXPORT_SYMBOL(cfs_nidrange_find_min_max);

/**
 * Determines the min and max NID values for num LNDs
 *
 * \param	*nidlist
 * \param	*min_nid
 * \param	*max_nid
 */
static void cfs_num_min_max(struct list_head *nidlist, __u32 *min_nid,
			    __u32 *max_nid)
{
	struct nidrange	*nr;
	struct addrrange *ar;
	unsigned int tmp_min_addr = 0;
	unsigned int tmp_max_addr = 0;
	unsigned int min_addr = 0;
	unsigned int max_addr = 0;

	list_for_each_entry(nr, nidlist, nr_link) {
		list_for_each_entry(ar, &nr->nr_addrranges, ar_link) {
			cfs_num_ar_min_max(ar, &tmp_min_addr,
					   &tmp_max_addr);
			if (tmp_min_addr < min_addr || !min_addr)
				min_addr = tmp_min_addr;
			if (tmp_max_addr > max_addr)
				max_addr = tmp_min_addr;
		}
	}
	*max_nid = max_addr;
	*min_nid = min_addr;
}

/**
 * Takes an nidlist and determines the minimum and maximum
 * ip addresses.
 *
 * \param	*nidlist
 * \param	*min_nid
 * \param	*max_nid
 */
static void cfs_ip_min_max(struct list_head *nidlist, __u32 *min_nid,
			   __u32 *max_nid)
{
	struct nidrange *nr;
	struct addrrange *ar;
	__u32 tmp_min_ip_addr = 0;
	__u32 tmp_max_ip_addr = 0;
	__u32 min_ip_addr = 0;
	__u32 max_ip_addr = 0;

	list_for_each_entry(nr, nidlist, nr_link) {
		list_for_each_entry(ar, &nr->nr_addrranges, ar_link) {
			cfs_ip_ar_min_max(ar, &tmp_min_ip_addr,
					  &tmp_max_ip_addr);
			if (tmp_min_ip_addr < min_ip_addr || !min_ip_addr)
				min_ip_addr = tmp_min_ip_addr;
			if (tmp_max_ip_addr > max_ip_addr)
				max_ip_addr = tmp_max_ip_addr;
		}
	}

	if (min_nid)
		*min_nid = min_ip_addr;
	if (max_nid)
		*max_nid = max_ip_addr;
}

static int
libcfs_lo_str2addr(const char *str, int nob, __u32 *addr)
{
	*addr = 0;
	return 1;
}

static void
libcfs_ip_addr2str(__u32 addr, char *str, size_t size)
{
	snprintf(str, size, "%u.%u.%u.%u",
		 (addr >> 24) & 0xff, (addr >> 16) & 0xff,
		 (addr >> 8) & 0xff, addr & 0xff);
}

/*
 * CAVEAT EMPTOR XscanfX
 * I use "%n" at the end of a sscanf format to detect trailing junk.  However
 * sscanf may return immediately if it sees the terminating '0' in a string, so
 * I initialise the %n variable to the expected length.  If sscanf sets it;
 * fine, if it doesn't, then the scan ended at the end of the string, which is
 * fine too :)
 */
static int
libcfs_ip_str2addr(const char *str, int nob, __u32 *addr)
{
	unsigned int	a;
	unsigned int	b;
	unsigned int	c;
	unsigned int	d;
	int		n = nob; /* XscanfX */

	/* numeric IP? */
	if (sscanf(str, "%u.%u.%u.%u%n", &a, &b, &c, &d, &n) >= 4 &&
	    n == nob &&
	    !(a & ~0xff) && !(b & ~0xff) &&
	    !(c & ~0xff) && !(d & ~0xff)) {
		*addr = ((a << 24) | (b << 16) | (c << 8) | d);
		return 1;
	}

	return 0;
}

/* Used by lnet/config.c so it can't be static */
int
cfs_ip_addr_parse(char *str, int len, struct list_head *list)
{
	struct cfs_expr_list *el;
	struct cfs_lstr src;
	int rc;
	int i;

	src.ls_str = str;
	src.ls_len = len;
	i = 0;

	while (src.ls_str) {
		struct cfs_lstr res;

		if (!cfs_gettok(&src, '.', &res)) {
			rc = -EINVAL;
			goto out;
		}

		rc = cfs_expr_list_parse(res.ls_str, res.ls_len, 0, 255, &el);
		if (rc)
			goto out;

		list_add_tail(&el->el_link, list);
		i++;
	}

	if (i == 4)
		return 0;

	rc = -EINVAL;
out:
	cfs_expr_list_free_list(list);

	return rc;
}

static int
libcfs_ip_addr_range_print(char *buffer, int count, struct list_head *list)
{
	int i = 0, j = 0;
	struct cfs_expr_list *el;

	list_for_each_entry(el, list, el_link) {
		LASSERT(j++ < 4);
		if (i)
			i += scnprintf(buffer + i, count - i, ".");
		i += cfs_expr_list_print(buffer + i, count - i, el);
	}
	return i;
}

/**
 * Matches address (\a addr) against address set encoded in \a list.
 *
 * \retval 1 if \a addr matches
 * \retval 0 otherwise
 */
int
cfs_ip_addr_match(__u32 addr, struct list_head *list)
{
	struct cfs_expr_list *el;
	int i = 0;

	list_for_each_entry_reverse(el, list, el_link) {
		if (!cfs_expr_list_match(addr & 0xff, el))
			return 0;
		addr >>= 8;
		i++;
	}

	return i == 4;
}

static void
libcfs_decnum_addr2str(__u32 addr, char *str, size_t size)
{
	snprintf(str, size, "%u", addr);
}

static int
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
	if (!rc)
		list_add_tail(&el->el_link, list);

	return rc;
}

static int
libcfs_num_addr_range_print(char *buffer, int count, struct list_head *list)
{
	int i = 0, j = 0;
	struct cfs_expr_list *el;

	list_for_each_entry(el, list, el_link) {
		LASSERT(j++ < 1);
		i += cfs_expr_list_print(buffer + i, count - i, el);
	}
	return i;
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

static struct netstrfns libcfs_netstrfns[] = {
	{ .nf_type		= LOLND,
	  .nf_name		= "lo",
	  .nf_modname		= "klolnd",
	  .nf_addr2str		= libcfs_decnum_addr2str,
	  .nf_str2addr		= libcfs_lo_str2addr,
	  .nf_parse_addrlist	= libcfs_num_parse,
	  .nf_print_addrlist	= libcfs_num_addr_range_print,
	  .nf_match_addr	= libcfs_num_match,
	  .nf_is_contiguous	= cfs_num_is_contiguous,
	  .nf_min_max		= cfs_num_min_max },
	{ .nf_type		= SOCKLND,
	  .nf_name		= "tcp",
	  .nf_modname		= "ksocklnd",
	  .nf_addr2str		= libcfs_ip_addr2str,
	  .nf_str2addr		= libcfs_ip_str2addr,
	  .nf_parse_addrlist	= cfs_ip_addr_parse,
	  .nf_print_addrlist	= libcfs_ip_addr_range_print,
	  .nf_match_addr	= cfs_ip_addr_match,
	  .nf_is_contiguous	= cfs_ip_is_contiguous,
	  .nf_min_max		= cfs_ip_min_max },
	{ .nf_type		= O2IBLND,
	  .nf_name		= "o2ib",
	  .nf_modname		= "ko2iblnd",
	  .nf_addr2str		= libcfs_ip_addr2str,
	  .nf_str2addr		= libcfs_ip_str2addr,
	  .nf_parse_addrlist	= cfs_ip_addr_parse,
	  .nf_print_addrlist	= libcfs_ip_addr_range_print,
	  .nf_match_addr	= cfs_ip_addr_match,
	  .nf_is_contiguous	= cfs_ip_is_contiguous,
	  .nf_min_max		= cfs_ip_min_max },
	{ .nf_type		= GNILND,
	  .nf_name		= "gni",
	  .nf_modname		= "kgnilnd",
	  .nf_addr2str		= libcfs_decnum_addr2str,
	  .nf_str2addr		= libcfs_num_str2addr,
	  .nf_parse_addrlist	= libcfs_num_parse,
	  .nf_print_addrlist	= libcfs_num_addr_range_print,
	  .nf_match_addr	= libcfs_num_match,
	  .nf_is_contiguous	= cfs_num_is_contiguous,
	  .nf_min_max		= cfs_num_min_max },
	{ .nf_type		= GNIIPLND,
	  .nf_name		= "gip",
	  .nf_modname		= "kgnilnd",
	  .nf_addr2str		= libcfs_ip_addr2str,
	  .nf_str2addr		= libcfs_ip_str2addr,
	  .nf_parse_addrlist	= cfs_ip_addr_parse,
	  .nf_print_addrlist	= libcfs_ip_addr_range_print,
	  .nf_match_addr	= cfs_ip_addr_match,
	  .nf_is_contiguous	= cfs_ip_is_contiguous,
	  .nf_min_max		= cfs_ip_min_max },
};

static const size_t libcfs_nnetstrfns = ARRAY_SIZE(libcfs_netstrfns);

static struct netstrfns *
libcfs_lnd2netstrfns(__u32 lnd)
{
	int i;

	for (i = 0; i < libcfs_nnetstrfns; i++)
		if (lnd == libcfs_netstrfns[i].nf_type)
			return &libcfs_netstrfns[i];

	return NULL;
}

static struct netstrfns *
libcfs_namenum2netstrfns(const char *name)
{
	struct netstrfns *nf;
	int i;

	for (i = 0; i < libcfs_nnetstrfns; i++) {
		nf = &libcfs_netstrfns[i];
		if (!strncmp(name, nf->nf_name, strlen(nf->nf_name)))
			return nf;
	}
	return NULL;
}

static struct netstrfns *
libcfs_name2netstrfns(const char *name)
{
	int    i;

	for (i = 0; i < libcfs_nnetstrfns; i++)
		if (!strcmp(libcfs_netstrfns[i].nf_name, name))
			return &libcfs_netstrfns[i];

	return NULL;
}

int
libcfs_isknown_lnd(__u32 lnd)
{
	return !!libcfs_lnd2netstrfns(lnd);
}
EXPORT_SYMBOL(libcfs_isknown_lnd);

char *
libcfs_lnd2modname(__u32 lnd)
{
	struct netstrfns *nf = libcfs_lnd2netstrfns(lnd);

	return nf ? nf->nf_modname : NULL;
}
EXPORT_SYMBOL(libcfs_lnd2modname);

int
libcfs_str2lnd(const char *str)
{
	struct netstrfns *nf = libcfs_name2netstrfns(str);

	if (nf)
		return nf->nf_type;

	return -ENXIO;
}
EXPORT_SYMBOL(libcfs_str2lnd);

char *
libcfs_lnd2str_r(__u32 lnd, char *buf, size_t buf_size)
{
	struct netstrfns *nf;

	nf = libcfs_lnd2netstrfns(lnd);
	if (!nf)
		snprintf(buf, buf_size, "?%u?", lnd);
	else
		snprintf(buf, buf_size, "%s", nf->nf_name);

	return buf;
}
EXPORT_SYMBOL(libcfs_lnd2str_r);

char *
libcfs_net2str_r(__u32 net, char *buf, size_t buf_size)
{
	__u32 nnum = LNET_NETNUM(net);
	__u32 lnd = LNET_NETTYP(net);
	struct netstrfns *nf;

	nf = libcfs_lnd2netstrfns(lnd);
	if (!nf)
		snprintf(buf, buf_size, "<%u:%u>", lnd, nnum);
	else if (!nnum)
		snprintf(buf, buf_size, "%s", nf->nf_name);
	else
		snprintf(buf, buf_size, "%s%u", nf->nf_name, nnum);

	return buf;
}
EXPORT_SYMBOL(libcfs_net2str_r);

char *
libcfs_nid2str_r(lnet_nid_t nid, char *buf, size_t buf_size)
{
	__u32 addr = LNET_NIDADDR(nid);
	__u32 net = LNET_NIDNET(nid);
	__u32 nnum = LNET_NETNUM(net);
	__u32 lnd = LNET_NETTYP(net);
	struct netstrfns *nf;

	if (nid == LNET_NID_ANY) {
		strncpy(buf, "<?>", buf_size);
		buf[buf_size - 1] = '\0';
		return buf;
	}

	nf = libcfs_lnd2netstrfns(lnd);
	if (!nf) {
		snprintf(buf, buf_size, "%x@<%u:%u>", addr, lnd, nnum);
	} else {
		size_t addr_len;

		nf->nf_addr2str(addr, buf, buf_size);
		addr_len = strlen(buf);
		if (!nnum)
			snprintf(buf + addr_len, buf_size - addr_len, "@%s",
				 nf->nf_name);
		else
			snprintf(buf + addr_len, buf_size - addr_len, "@%s%u",
				 nf->nf_name, nnum);
	}

	return buf;
}
EXPORT_SYMBOL(libcfs_nid2str_r);

static struct netstrfns *
libcfs_str2net_internal(const char *str, __u32 *net)
{
	struct netstrfns *nf = NULL;
	int nob;
	unsigned int netnum;
	int i;

	for (i = 0; i < libcfs_nnetstrfns; i++) {
		nf = &libcfs_netstrfns[i];
		if (!strncmp(str, nf->nf_name, strlen(nf->nf_name)))
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

	if (libcfs_str2net_internal(str, &net))
		return net;

	return LNET_NIDNET(LNET_NID_ANY);
}
EXPORT_SYMBOL(libcfs_str2net);

lnet_nid_t
libcfs_str2nid(const char *str)
{
	const char *sep = strchr(str, '@');
	struct netstrfns *nf;
	__u32 net;
	__u32 addr;

	if (sep) {
		nf = libcfs_str2net_internal(sep + 1, &net);
		if (!nf)
			return LNET_NID_ANY;
	} else {
		sep = str + strlen(str);
		net = LNET_MKNET(SOCKLND, 0);
		nf = libcfs_lnd2netstrfns(SOCKLND);
		LASSERT(nf);
	}

	if (!nf->nf_str2addr(str, (int)(sep - str), &addr))
		return LNET_NID_ANY;

	return LNET_MKNID(net, addr);
}
EXPORT_SYMBOL(libcfs_str2nid);

char *
libcfs_id2str(struct lnet_process_id id)
{
	char *str = libcfs_next_nidstring();

	if (id.pid == LNET_PID_ANY) {
		snprintf(str, LNET_NIDSTR_SIZE,
			 "LNET_PID_ANY-%s", libcfs_nid2str(id.nid));
		return str;
	}

	snprintf(str, LNET_NIDSTR_SIZE, "%s%u-%s",
		 id.pid & LNET_PID_USERFLAG ? "U" : "",
		 id.pid & ~LNET_PID_USERFLAG, libcfs_nid2str(id.nid));
	return str;
}
EXPORT_SYMBOL(libcfs_id2str);

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
EXPORT_SYMBOL(libcfs_str2anynid);
