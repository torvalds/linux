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
	unsigned char mac[OSA_ADDR_LEN];
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


void qeth_l3_ipaddr_to_string(enum qeth_prot_versions, const __u8 *, char *);
int qeth_l3_string_to_ipaddr(const char *, enum qeth_prot_versions, __u8 *);
int qeth_l3_create_device_attributes(struct device *);
void qeth_l3_remove_device_attributes(struct device *);
int qeth_l3_setrouting_v4(struct qeth_card *);
int qeth_l3_setrouting_v6(struct qeth_card *);
int qeth_l3_add_ipato_entry(struct qeth_card *, struct qeth_ipato_entry *);
void qeth_l3_del_ipato_entry(struct qeth_card *, enum qeth_prot_versions,
			u8 *, int);
int qeth_l3_add_vipa(struct qeth_card *, enum qeth_prot_versions, const u8 *);
void qeth_l3_del_vipa(struct qeth_card *, enum qeth_prot_versions, const u8 *);
int qeth_l3_add_rxip(struct qeth_card *, enum qeth_prot_versions, const u8 *);
void qeth_l3_del_rxip(struct qeth_card *card, enum qeth_prot_versions,
			const u8 *);
int qeth_l3_is_addr_covered_by_ipato(struct qeth_card *, struct qeth_ipaddr *);
struct qeth_ipaddr *qeth_l3_get_addr_buffer(enum qeth_prot_versions);
int qeth_l3_add_ip(struct qeth_card *, struct qeth_ipaddr *);
int qeth_l3_delete_ip(struct qeth_card *, struct qeth_ipaddr *);

#endif /* __QETH_L3_H__ */
