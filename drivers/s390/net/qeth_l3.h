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

enum qeth_ip_types {
	QETH_IP_TYPE_NORMAL,
	QETH_IP_TYPE_VIPA,
	QETH_IP_TYPE_RXIP,
};

struct qeth_ipaddr {
	struct hlist_node hnode;
	enum qeth_ip_types type;
	u8 is_multicast:1;
	u8 in_progress:1;
	u8 disp_flag:2;
	u8 ipato:1;			/* ucast only */

	/* is changed only for normal ip addresses
	 * for non-normal addresses it always is  1
	 */
	int  ref_counter;
	enum qeth_prot_versions proto;
	union {
		struct {
			__be32 addr;
			unsigned int mask;
		} a4;
		struct {
			struct in6_addr addr;
			unsigned int pfxlen;
		} a6;
	} u;
};

static inline void qeth_l3_init_ipaddr(struct qeth_ipaddr *addr,
				       enum qeth_ip_types type,
				       enum qeth_prot_versions proto)
{
	memset(addr, 0, sizeof(*addr));
	addr->type = type;
	addr->proto = proto;
	addr->disp_flag = QETH_DISP_ADDR_DO_NOTHING;
	addr->ref_counter = 1;
}

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
	 * -	'mask'/'pfxlen' for RXIP/VIPA is always 0. For NORMAL, matching
	 *	values are required to avoid mixups in takeover eligibility.
	 *
	 * For mcast,
	 * -	'mask'/'pfxlen' is always 0.
	 */
	if (a1->type != a2->type)
		return false;
	if (a1->proto == QETH_PROT_IPV6)
		return a1->u.a6.pfxlen == a2->u.a6.pfxlen;
	return a1->u.a4.mask == a2->u.a4.mask;
}

static inline u32 qeth_l3_ipaddr_hash(struct qeth_ipaddr *addr)
{
	if (addr->proto == QETH_PROT_IPV6)
		return ipv6_addr_hash(&addr->u.a6.addr);
	else
		return ipv4_addr_hash(addr->u.a4.addr);
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
void qeth_l3_update_ipato(struct qeth_card *card);
int qeth_l3_modify_hsuid(struct qeth_card *card, bool add);
int qeth_l3_modify_rxip_vipa(struct qeth_card *card, bool add, const u8 *ip,
			     enum qeth_ip_types type,
			     enum qeth_prot_versions proto);

#endif /* __QETH_L3_H__ */
