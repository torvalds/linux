/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Copyright IBM Corp. 2007
 *    Author(s): Utz Bacher <utz.bacher@de.ibm.com>,
 *		 Frank Pavlic <fpavlic@de.ibm.com>,
 *		 Thomas Spatzier <tspat@de.ibm.com>,
 *		 Frank Blaschka <frank.blaschka@de.ibm.com>
 */

#ifndef __QETH_L3_H__
#define __QETH_L3_H__

#include "qeth_core.h"
#include <linux/hashtable.h>

#define QETH_SNIFF_AVAIL	0x0008

struct qeth_ipaddr {
	struct hlist_node hnode;
	enum qeth_ip_types type;
	enum qeth_ipa_setdelip_flags set_flags;
	enum qeth_ipa_setdelip_flags del_flags;
	u8 is_multicast:1;
	u8 in_progress:1;
	u8 disp_flag:2;

	/* is changed only for normal ip addresses
	 * for non-normal addresses it always is  1
	 */
	int  ref_counter;
	enum qeth_prot_versions proto;
	unsigned char mac[ETH_ALEN];
	union {
		struct {
			unsigned int addr;
			unsigned int mask;
		} a4;
		struct {
			struct in6_addr addr;
			unsigned int pfxlen;
		} a6;
	} u;
};

static inline bool qeth_l3_addr_match_ip(struct qeth_ipaddr *a1,
					 struct qeth_ipaddr *a2)
{
	if (a1->proto != a2->proto)
		return false;
	if (a1->proto == QETH_PROT_IPV6)
		return ipv6_addr_equal(&a1->u.a6.addr, &a2->u.a6.addr);
	return a1->u.a4.addr == a2->u.a4.addr;
}

static inline bool qeth_l3_addr_match_all(struct qeth_ipaddr *a1,
					  struct qeth_ipaddr *a2)
{
	/* Assumes that the pair was obtained via qeth_l3_addr_find_by_ip(),
	 * so 'proto' and 'addr' match for sure.
	 *
	 * For ucast:
	 * -	'mac' is always 0.
	 * -	'mask'/'pfxlen' for RXIP/VIPA is always 0. For NORMAL, matching
	 *	values are required to avoid mixups in takeover eligibility.
	 *
	 * For mcast,
	 * -	'mac' is mapped from the IP, and thus always matches.
	 * -	'mask'/'pfxlen' is always 0.
	 */
	if (a1->type != a2->type)
		return false;
	if (a1->proto == QETH_PROT_IPV6)
		return a1->u.a6.pfxlen == a2->u.a6.pfxlen;
	return a1->u.a4.mask == a2->u.a4.mask;
}

static inline  u64 qeth_l3_ipaddr_hash(struct qeth_ipaddr *addr)
{
	u64  ret = 0;
	u8 *point;

	if (addr->proto == QETH_PROT_IPV6) {
		point = (u8 *) &addr->u.a6.addr;
		ret = get_unaligned((u64 *)point) ^
			get_unaligned((u64 *) (point + 8));
	}
	if (addr->proto == QETH_PROT_IPV4) {
		point = (u8 *) &addr->u.a4.addr;
		ret = get_unaligned((u32 *) point);
	}
	return ret;
}

struct qeth_ipato_entry {
	struct list_head entry;
	enum qeth_prot_versions proto;
	char addr[16];
	int mask_bits;
};

extern const struct attribute_group *qeth_l3_attr_groups[];

void qeth_l3_ipaddr_to_string(enum qeth_prot_versions, const __u8 *, char *);
int qeth_l3_create_device_attributes(struct device *);
void qeth_l3_remove_device_attributes(struct device *);
int qeth_l3_setrouting_v4(struct qeth_card *);
int qeth_l3_setrouting_v6(struct qeth_card *);
int qeth_l3_add_ipato_entry(struct qeth_card *, struct qeth_ipato_entry *);
int qeth_l3_del_ipato_entry(struct qeth_card *card,
			    enum qeth_prot_versions proto, u8 *addr,
			    int mask_bits);
int qeth_l3_add_vipa(struct qeth_card *, enum qeth_prot_versions, const u8 *);
int qeth_l3_del_vipa(struct qeth_card *card, enum qeth_prot_versions proto,
		     const u8 *addr);
int qeth_l3_add_rxip(struct qeth_card *, enum qeth_prot_versions, const u8 *);
int qeth_l3_del_rxip(struct qeth_card *card, enum qeth_prot_versions proto,
		     const u8 *addr);
void qeth_l3_update_ipato(struct qeth_card *card);
struct qeth_ipaddr *qeth_l3_get_addr_buffer(enum qeth_prot_versions);
int qeth_l3_add_ip(struct qeth_card *, struct qeth_ipaddr *);
int qeth_l3_delete_ip(struct qeth_card *, struct qeth_ipaddr *);

#endif /* __QETH_L3_H__ */
