// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2007, 2009
 *    Author(s): Utz Bacher <utz.bacher@de.ibm.com>,
 *		 Frank Pavlic <fpavlic@de.ibm.com>,
 *		 Thomas Spatzier <tspat@de.ibm.com>,
 *		 Frank Blaschka <frank.blaschka@de.ibm.com>
 */

#define KMSG_COMPONENT "qeth"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/ipv6.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/slab.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/skbuff.h>

#include <net/ip.h>
#include <net/arp.h>
#include <net/route.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/ip6_fib.h>
#include <net/ip6_checksum.h>
#include <net/iucv/af_iucv.h>
#include <linux/hashtable.h>

#include "qeth_l3.h"


static int qeth_l3_set_offline(struct ccwgroup_device *);
static int qeth_l3_stop(struct net_device *);
static void qeth_l3_set_rx_mode(struct net_device *dev);
static int qeth_l3_register_addr_entry(struct qeth_card *,
		struct qeth_ipaddr *);
static int qeth_l3_deregister_addr_entry(struct qeth_card *,
		struct qeth_ipaddr *);

static void qeth_l3_ipaddr4_to_string(const __u8 *addr, char *buf)
{
	sprintf(buf, "%pI4", addr);
}

static void qeth_l3_ipaddr6_to_string(const __u8 *addr, char *buf)
{
	sprintf(buf, "%pI6", addr);
}

void qeth_l3_ipaddr_to_string(enum qeth_prot_versions proto, const __u8 *addr,
				char *buf)
{
	if (proto == QETH_PROT_IPV4)
		qeth_l3_ipaddr4_to_string(addr, buf);
	else if (proto == QETH_PROT_IPV6)
		qeth_l3_ipaddr6_to_string(addr, buf);
}

static struct qeth_ipaddr *qeth_l3_get_addr_buffer(enum qeth_prot_versions prot)
{
	struct qeth_ipaddr *addr = kmalloc(sizeof(*addr), GFP_ATOMIC);

	if (addr)
		qeth_l3_init_ipaddr(addr, QETH_IP_TYPE_NORMAL, prot);
	return addr;
}

static struct qeth_ipaddr *qeth_l3_find_addr_by_ip(struct qeth_card *card,
						   struct qeth_ipaddr *query)
{
	u64 key = qeth_l3_ipaddr_hash(query);
	struct qeth_ipaddr *addr;

	if (query->is_multicast) {
		hash_for_each_possible(card->ip_mc_htable, addr, hnode, key)
			if (qeth_l3_addr_match_ip(addr, query))
				return addr;
	} else {
		hash_for_each_possible(card->ip_htable,  addr, hnode, key)
			if (qeth_l3_addr_match_ip(addr, query))
				return addr;
	}
	return NULL;
}

static void qeth_l3_convert_addr_to_bits(u8 *addr, u8 *bits, int len)
{
	int i, j;
	u8 octet;

	for (i = 0; i < len; ++i) {
		octet = addr[i];
		for (j = 7; j >= 0; --j) {
			bits[i*8 + j] = octet & 1;
			octet >>= 1;
		}
	}
}

static bool qeth_l3_is_addr_covered_by_ipato(struct qeth_card *card,
					     struct qeth_ipaddr *addr)
{
	struct qeth_ipato_entry *ipatoe;
	u8 addr_bits[128] = {0, };
	u8 ipatoe_bits[128] = {0, };
	int rc = 0;

	if (!card->ipato.enabled)
		return 0;
	if (addr->type != QETH_IP_TYPE_NORMAL)
		return 0;

	qeth_l3_convert_addr_to_bits((u8 *) &addr->u, addr_bits,
				  (addr->proto == QETH_PROT_IPV4)? 4:16);
	list_for_each_entry(ipatoe, &card->ipato.entries, entry) {
		if (addr->proto != ipatoe->proto)
			continue;
		qeth_l3_convert_addr_to_bits(ipatoe->addr, ipatoe_bits,
					  (ipatoe->proto == QETH_PROT_IPV4) ?
					  4 : 16);
		if (addr->proto == QETH_PROT_IPV4)
			rc = !memcmp(addr_bits, ipatoe_bits,
				     min(32, ipatoe->mask_bits));
		else
			rc = !memcmp(addr_bits, ipatoe_bits,
				     min(128, ipatoe->mask_bits));
		if (rc)
			break;
	}
	/* invert? */
	if ((addr->proto == QETH_PROT_IPV4) && card->ipato.invert4)
		rc = !rc;
	else if ((addr->proto == QETH_PROT_IPV6) && card->ipato.invert6)
		rc = !rc;

	return rc;
}

static int qeth_l3_delete_ip(struct qeth_card *card,
			     struct qeth_ipaddr *tmp_addr)
{
	int rc = 0;
	struct qeth_ipaddr *addr;

	if (tmp_addr->type == QETH_IP_TYPE_RXIP)
		QETH_CARD_TEXT(card, 2, "delrxip");
	else if (tmp_addr->type == QETH_IP_TYPE_VIPA)
		QETH_CARD_TEXT(card, 2, "delvipa");
	else
		QETH_CARD_TEXT(card, 2, "delip");

	if (tmp_addr->proto == QETH_PROT_IPV4)
		QETH_CARD_HEX(card, 4, &tmp_addr->u.a4.addr, 4);
	else {
		QETH_CARD_HEX(card, 4, &tmp_addr->u.a6.addr, 8);
		QETH_CARD_HEX(card, 4, ((char *)&tmp_addr->u.a6.addr) + 8, 8);
	}

	addr = qeth_l3_find_addr_by_ip(card, tmp_addr);
	if (!addr || !qeth_l3_addr_match_all(addr, tmp_addr))
		return -ENOENT;

	addr->ref_counter--;
	if (addr->type == QETH_IP_TYPE_NORMAL && addr->ref_counter > 0)
		return rc;
	if (addr->in_progress)
		return -EINPROGRESS;

	if (qeth_card_hw_is_reachable(card))
		rc = qeth_l3_deregister_addr_entry(card, addr);

	hash_del(&addr->hnode);
	kfree(addr);

	return rc;
}

static int qeth_l3_add_ip(struct qeth_card *card, struct qeth_ipaddr *tmp_addr)
{
	int rc = 0;
	struct qeth_ipaddr *addr;
	char buf[40];

	if (tmp_addr->type == QETH_IP_TYPE_RXIP)
		QETH_CARD_TEXT(card, 2, "addrxip");
	else if (tmp_addr->type == QETH_IP_TYPE_VIPA)
		QETH_CARD_TEXT(card, 2, "addvipa");
	else
		QETH_CARD_TEXT(card, 2, "addip");

	if (tmp_addr->proto == QETH_PROT_IPV4)
		QETH_CARD_HEX(card, 4, &tmp_addr->u.a4.addr, 4);
	else {
		QETH_CARD_HEX(card, 4, &tmp_addr->u.a6.addr, 8);
		QETH_CARD_HEX(card, 4, ((char *)&tmp_addr->u.a6.addr) + 8, 8);
	}

	addr = qeth_l3_find_addr_by_ip(card, tmp_addr);
	if (addr) {
		if (tmp_addr->type != QETH_IP_TYPE_NORMAL)
			return -EADDRINUSE;
		if (qeth_l3_addr_match_all(addr, tmp_addr)) {
			addr->ref_counter++;
			return 0;
		}
		qeth_l3_ipaddr_to_string(tmp_addr->proto, (u8 *)&tmp_addr->u,
					 buf);
		dev_warn(&card->gdev->dev,
			 "Registering IP address %s failed\n", buf);
		return -EADDRINUSE;
	} else {
		addr = qeth_l3_get_addr_buffer(tmp_addr->proto);
		if (!addr)
			return -ENOMEM;

		memcpy(addr, tmp_addr, sizeof(struct qeth_ipaddr));
		addr->ref_counter = 1;

		if (qeth_l3_is_addr_covered_by_ipato(card, addr)) {
			QETH_CARD_TEXT(card, 2, "tkovaddr");
			addr->ipato = 1;
		}
		hash_add(card->ip_htable, &addr->hnode,
				qeth_l3_ipaddr_hash(addr));

		if (!qeth_card_hw_is_reachable(card)) {
			addr->disp_flag = QETH_DISP_ADDR_ADD;
			return 0;
		}

		/* qeth_l3_register_addr_entry can go to sleep
		 * if we add a IPV4 addr. It is caused by the reason
		 * that SETIP ipa cmd starts ARP staff for IPV4 addr.
		 * Thus we should unlock spinlock, and make a protection
		 * using in_progress variable to indicate that there is
		 * an hardware operation with this IPV4 address
		 */
		if (addr->proto == QETH_PROT_IPV4) {
			addr->in_progress = 1;
			spin_unlock_bh(&card->ip_lock);
			rc = qeth_l3_register_addr_entry(card, addr);
			spin_lock_bh(&card->ip_lock);
			addr->in_progress = 0;
		} else
			rc = qeth_l3_register_addr_entry(card, addr);

		if (!rc || (rc == IPA_RC_DUPLICATE_IP_ADDRESS) ||
				(rc == IPA_RC_LAN_OFFLINE)) {
			addr->disp_flag = QETH_DISP_ADDR_DO_NOTHING;
			if (addr->ref_counter < 1) {
				qeth_l3_deregister_addr_entry(card, addr);
				hash_del(&addr->hnode);
				kfree(addr);
			}
		} else {
			hash_del(&addr->hnode);
			kfree(addr);
		}
	}
	return rc;
}

static void qeth_l3_clear_ip_htable(struct qeth_card *card, int recover)
{
	struct qeth_ipaddr *addr;
	struct hlist_node *tmp;
	int i;

	QETH_CARD_TEXT(card, 4, "clearip");

	if (recover && card->options.sniffer)
		return;

	spin_lock_bh(&card->ip_lock);

	hash_for_each_safe(card->ip_htable, i, tmp, addr, hnode) {
		if (!recover) {
			hash_del(&addr->hnode);
			kfree(addr);
			continue;
		}
		addr->disp_flag = QETH_DISP_ADDR_ADD;
	}

	spin_unlock_bh(&card->ip_lock);

	spin_lock_bh(&card->mclock);

	hash_for_each_safe(card->ip_mc_htable, i, tmp, addr, hnode) {
		hash_del(&addr->hnode);
		kfree(addr);
	}

	spin_unlock_bh(&card->mclock);


}
static void qeth_l3_recover_ip(struct qeth_card *card)
{
	struct qeth_ipaddr *addr;
	struct hlist_node *tmp;
	int i;
	int rc;

	QETH_CARD_TEXT(card, 4, "recovrip");

	spin_lock_bh(&card->ip_lock);

	hash_for_each_safe(card->ip_htable, i, tmp, addr, hnode) {
		if (addr->disp_flag == QETH_DISP_ADDR_ADD) {
			if (addr->proto == QETH_PROT_IPV4) {
				addr->in_progress = 1;
				spin_unlock_bh(&card->ip_lock);
				rc = qeth_l3_register_addr_entry(card, addr);
				spin_lock_bh(&card->ip_lock);
				addr->in_progress = 0;
			} else
				rc = qeth_l3_register_addr_entry(card, addr);

			if (!rc) {
				addr->disp_flag = QETH_DISP_ADDR_DO_NOTHING;
				if (addr->ref_counter < 1)
					qeth_l3_delete_ip(card, addr);
			} else {
				hash_del(&addr->hnode);
				kfree(addr);
			}
		}
	}

	spin_unlock_bh(&card->ip_lock);

}

static int qeth_l3_send_setdelmc(struct qeth_card *card,
			struct qeth_ipaddr *addr, int ipacmd)
{
	int rc;
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	QETH_CARD_TEXT(card, 4, "setdelmc");

	iob = qeth_get_ipacmd_buffer(card, ipacmd, addr->proto);
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);
	ether_addr_copy(cmd->data.setdelipm.mac, addr->mac);
	if (addr->proto == QETH_PROT_IPV6)
		memcpy(cmd->data.setdelipm.ip6, &addr->u.a6.addr,
		       sizeof(struct in6_addr));
	else
		memcpy(&cmd->data.setdelipm.ip4, &addr->u.a4.addr, 4);

	rc = qeth_send_ipa_cmd(card, iob, NULL, NULL);

	return rc;
}

static void qeth_l3_fill_netmask(u8 *netmask, unsigned int len)
{
	int i, j;
	for (i = 0; i < 16; i++) {
		j = (len) - (i * 8);
		if (j >= 8)
			netmask[i] = 0xff;
		else if (j > 0)
			netmask[i] = (u8)(0xFF00 >> j);
		else
			netmask[i] = 0;
	}
}

static u32 qeth_l3_get_setdelip_flags(struct qeth_ipaddr *addr, bool set)
{
	switch (addr->type) {
	case QETH_IP_TYPE_RXIP:
		return (set) ? QETH_IPA_SETIP_TAKEOVER_FLAG : 0;
	case QETH_IP_TYPE_VIPA:
		return (set) ? QETH_IPA_SETIP_VIPA_FLAG :
			       QETH_IPA_DELIP_VIPA_FLAG;
	default:
		return (set && addr->ipato) ? QETH_IPA_SETIP_TAKEOVER_FLAG : 0;
	}
}

static int qeth_l3_send_setdelip(struct qeth_card *card,
				 struct qeth_ipaddr *addr,
				 enum qeth_ipa_cmds ipacmd)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;
	__u8 netmask[16];
	u32 flags;

	QETH_CARD_TEXT(card, 4, "setdelip");

	iob = qeth_get_ipacmd_buffer(card, ipacmd, addr->proto);
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);

	flags = qeth_l3_get_setdelip_flags(addr, ipacmd == IPA_CMD_SETIP);
	QETH_CARD_TEXT_(card, 4, "flags%02X", flags);

	if (addr->proto == QETH_PROT_IPV6) {
		memcpy(cmd->data.setdelip6.ip_addr, &addr->u.a6.addr,
		       sizeof(struct in6_addr));
		qeth_l3_fill_netmask(netmask, addr->u.a6.pfxlen);
		memcpy(cmd->data.setdelip6.mask, netmask,
		       sizeof(struct in6_addr));
		cmd->data.setdelip6.flags = flags;
	} else {
		memcpy(cmd->data.setdelip4.ip_addr, &addr->u.a4.addr, 4);
		memcpy(cmd->data.setdelip4.mask, &addr->u.a4.mask, 4);
		cmd->data.setdelip4.flags = flags;
	}

	return qeth_send_ipa_cmd(card, iob, NULL, NULL);
}

static int qeth_l3_send_setrouting(struct qeth_card *card,
	enum qeth_routing_types type, enum qeth_prot_versions prot)
{
	int rc;
	struct qeth_ipa_cmd *cmd;
	struct qeth_cmd_buffer *iob;

	QETH_CARD_TEXT(card, 4, "setroutg");
	iob = qeth_get_ipacmd_buffer(card, IPA_CMD_SETRTG, prot);
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);
	cmd->data.setrtg.type = (type);
	rc = qeth_send_ipa_cmd(card, iob, NULL, NULL);

	return rc;
}

static int qeth_l3_correct_routing_type(struct qeth_card *card,
		enum qeth_routing_types *type, enum qeth_prot_versions prot)
{
	if (card->info.type == QETH_CARD_TYPE_IQD) {
		switch (*type) {
		case NO_ROUTER:
		case PRIMARY_CONNECTOR:
		case SECONDARY_CONNECTOR:
		case MULTICAST_ROUTER:
			return 0;
		default:
			goto out_inval;
		}
	} else {
		switch (*type) {
		case NO_ROUTER:
		case PRIMARY_ROUTER:
		case SECONDARY_ROUTER:
			return 0;
		case MULTICAST_ROUTER:
			if (qeth_is_ipafunc_supported(card, prot,
						      IPA_OSA_MC_ROUTER))
				return 0;
		default:
			goto out_inval;
		}
	}
out_inval:
	*type = NO_ROUTER;
	return -EINVAL;
}

int qeth_l3_setrouting_v4(struct qeth_card *card)
{
	int rc;

	QETH_CARD_TEXT(card, 3, "setrtg4");

	rc = qeth_l3_correct_routing_type(card, &card->options.route4.type,
				  QETH_PROT_IPV4);
	if (rc)
		return rc;

	rc = qeth_l3_send_setrouting(card, card->options.route4.type,
				  QETH_PROT_IPV4);
	if (rc) {
		card->options.route4.type = NO_ROUTER;
		QETH_DBF_MESSAGE(2, "Error (0x%04x) while setting routing type"
			" on %s. Type set to 'no router'.\n", rc,
			QETH_CARD_IFNAME(card));
	}
	return rc;
}

int qeth_l3_setrouting_v6(struct qeth_card *card)
{
	int rc = 0;

	QETH_CARD_TEXT(card, 3, "setrtg6");

	if (!qeth_is_supported(card, IPA_IPV6))
		return 0;
	rc = qeth_l3_correct_routing_type(card, &card->options.route6.type,
				  QETH_PROT_IPV6);
	if (rc)
		return rc;

	rc = qeth_l3_send_setrouting(card, card->options.route6.type,
				  QETH_PROT_IPV6);
	if (rc) {
		card->options.route6.type = NO_ROUTER;
		QETH_DBF_MESSAGE(2, "Error (0x%04x) while setting routing type"
			" on %s. Type set to 'no router'.\n", rc,
			QETH_CARD_IFNAME(card));
	}
	return rc;
}

/*
 * IP address takeover related functions
 */

/**
 * qeth_l3_update_ipato() - Update 'takeover' property, for all NORMAL IPs.
 *
 * Caller must hold ip_lock.
 */
void qeth_l3_update_ipato(struct qeth_card *card)
{
	struct qeth_ipaddr *addr;
	unsigned int i;

	hash_for_each(card->ip_htable, i, addr, hnode) {
		if (addr->type != QETH_IP_TYPE_NORMAL)
			continue;
		addr->ipato = qeth_l3_is_addr_covered_by_ipato(card, addr);
	}
}

static void qeth_l3_clear_ipato_list(struct qeth_card *card)
{
	struct qeth_ipato_entry *ipatoe, *tmp;

	spin_lock_bh(&card->ip_lock);

	list_for_each_entry_safe(ipatoe, tmp, &card->ipato.entries, entry) {
		list_del(&ipatoe->entry);
		kfree(ipatoe);
	}

	qeth_l3_update_ipato(card);
	spin_unlock_bh(&card->ip_lock);
}

int qeth_l3_add_ipato_entry(struct qeth_card *card,
				struct qeth_ipato_entry *new)
{
	struct qeth_ipato_entry *ipatoe;
	int rc = 0;

	QETH_CARD_TEXT(card, 2, "addipato");

	spin_lock_bh(&card->ip_lock);

	list_for_each_entry(ipatoe, &card->ipato.entries, entry) {
		if (ipatoe->proto != new->proto)
			continue;
		if (!memcmp(ipatoe->addr, new->addr,
			    (ipatoe->proto == QETH_PROT_IPV4)? 4:16) &&
		    (ipatoe->mask_bits == new->mask_bits)) {
			rc = -EEXIST;
			break;
		}
	}

	if (!rc) {
		list_add_tail(&new->entry, &card->ipato.entries);
		qeth_l3_update_ipato(card);
	}

	spin_unlock_bh(&card->ip_lock);

	return rc;
}

int qeth_l3_del_ipato_entry(struct qeth_card *card,
			    enum qeth_prot_versions proto, u8 *addr,
			    int mask_bits)
{
	struct qeth_ipato_entry *ipatoe, *tmp;
	int rc = -ENOENT;

	QETH_CARD_TEXT(card, 2, "delipato");

	spin_lock_bh(&card->ip_lock);

	list_for_each_entry_safe(ipatoe, tmp, &card->ipato.entries, entry) {
		if (ipatoe->proto != proto)
			continue;
		if (!memcmp(ipatoe->addr, addr,
			    (proto == QETH_PROT_IPV4)? 4:16) &&
		    (ipatoe->mask_bits == mask_bits)) {
			list_del(&ipatoe->entry);
			qeth_l3_update_ipato(card);
			kfree(ipatoe);
			rc = 0;
		}
	}

	spin_unlock_bh(&card->ip_lock);
	return rc;
}

int qeth_l3_modify_rxip_vipa(struct qeth_card *card, bool add, const u8 *ip,
			     enum qeth_ip_types type,
			     enum qeth_prot_versions proto)
{
	struct qeth_ipaddr addr;
	int rc;

	qeth_l3_init_ipaddr(&addr, type, proto);
	if (proto == QETH_PROT_IPV4)
		memcpy(&addr.u.a4.addr, ip, 4);
	else
		memcpy(&addr.u.a6.addr, ip, 16);

	spin_lock_bh(&card->ip_lock);
	rc = add ? qeth_l3_add_ip(card, &addr) : qeth_l3_delete_ip(card, &addr);
	spin_unlock_bh(&card->ip_lock);
	return rc;
}

int qeth_l3_modify_hsuid(struct qeth_card *card, bool add)
{
	struct qeth_ipaddr addr;
	int rc, i;

	qeth_l3_init_ipaddr(&addr, QETH_IP_TYPE_NORMAL, QETH_PROT_IPV6);
	addr.u.a6.addr.s6_addr[0] = 0xfe;
	addr.u.a6.addr.s6_addr[1] = 0x80;
	for (i = 0; i < 8; i++)
		addr.u.a6.addr.s6_addr[8+i] = card->options.hsuid[i];

	spin_lock_bh(&card->ip_lock);
	rc = add ? qeth_l3_add_ip(card, &addr) : qeth_l3_delete_ip(card, &addr);
	spin_unlock_bh(&card->ip_lock);
	return rc;
}

static int qeth_l3_register_addr_entry(struct qeth_card *card,
				struct qeth_ipaddr *addr)
{
	char buf[50];
	int rc = 0;
	int cnt = 3;


	if (addr->proto == QETH_PROT_IPV4) {
		QETH_CARD_TEXT(card, 2, "setaddr4");
		QETH_CARD_HEX(card, 3, &addr->u.a4.addr, sizeof(int));
	} else if (addr->proto == QETH_PROT_IPV6) {
		QETH_CARD_TEXT(card, 2, "setaddr6");
		QETH_CARD_HEX(card, 3, &addr->u.a6.addr, 8);
		QETH_CARD_HEX(card, 3, ((char *)&addr->u.a6.addr) + 8, 8);
	} else {
		QETH_CARD_TEXT(card, 2, "setaddr?");
		QETH_CARD_HEX(card, 3, addr, sizeof(struct qeth_ipaddr));
	}
	do {
		if (addr->is_multicast)
			rc =  qeth_l3_send_setdelmc(card, addr, IPA_CMD_SETIPM);
		else
			rc = qeth_l3_send_setdelip(card, addr, IPA_CMD_SETIP);
		if (rc)
			QETH_CARD_TEXT(card, 2, "failed");
	} while ((--cnt > 0) && rc);
	if (rc) {
		QETH_CARD_TEXT(card, 2, "FAILED");
		qeth_l3_ipaddr_to_string(addr->proto, (u8 *)&addr->u, buf);
		dev_warn(&card->gdev->dev,
			"Registering IP address %s failed\n", buf);
	}
	return rc;
}

static int qeth_l3_deregister_addr_entry(struct qeth_card *card,
						struct qeth_ipaddr *addr)
{
	int rc = 0;

	if (addr->proto == QETH_PROT_IPV4) {
		QETH_CARD_TEXT(card, 2, "deladdr4");
		QETH_CARD_HEX(card, 3, &addr->u.a4.addr, sizeof(int));
	} else if (addr->proto == QETH_PROT_IPV6) {
		QETH_CARD_TEXT(card, 2, "deladdr6");
		QETH_CARD_HEX(card, 3, &addr->u.a6.addr, 8);
		QETH_CARD_HEX(card, 3, ((char *)&addr->u.a6.addr) + 8, 8);
	} else {
		QETH_CARD_TEXT(card, 2, "deladdr?");
		QETH_CARD_HEX(card, 3, addr, sizeof(struct qeth_ipaddr));
	}
	if (addr->is_multicast)
		rc = qeth_l3_send_setdelmc(card, addr, IPA_CMD_DELIPM);
	else
		rc = qeth_l3_send_setdelip(card, addr, IPA_CMD_DELIP);
	if (rc)
		QETH_CARD_TEXT(card, 2, "failed");

	return rc;
}

static int qeth_l3_setadapter_parms(struct qeth_card *card)
{
	int rc = 0;

	QETH_DBF_TEXT(SETUP, 2, "setadprm");

	if (qeth_adp_supported(card, IPA_SETADP_ALTER_MAC_ADDRESS)) {
		rc = qeth_setadpparms_change_macaddr(card);
		if (rc)
			dev_warn(&card->gdev->dev, "Reading the adapter MAC"
				" address failed\n");
	}

	return rc;
}

static int qeth_l3_start_ipa_arp_processing(struct qeth_card *card)
{
	int rc;

	QETH_CARD_TEXT(card, 3, "ipaarp");

	if (!qeth_is_supported(card, IPA_ARP_PROCESSING)) {
		dev_info(&card->gdev->dev,
			"ARP processing not supported on %s!\n",
			QETH_CARD_IFNAME(card));
		return 0;
	}
	rc = qeth_send_simple_setassparms(card, IPA_ARP_PROCESSING,
					  IPA_CMD_ASS_START, 0);
	if (rc) {
		dev_warn(&card->gdev->dev,
			"Starting ARP processing support for %s failed\n",
			QETH_CARD_IFNAME(card));
	}
	return rc;
}

static int qeth_l3_start_ipa_source_mac(struct qeth_card *card)
{
	int rc;

	QETH_CARD_TEXT(card, 3, "stsrcmac");

	if (!qeth_is_supported(card, IPA_SOURCE_MAC)) {
		dev_info(&card->gdev->dev,
			"Inbound source MAC-address not supported on %s\n",
			QETH_CARD_IFNAME(card));
		return -EOPNOTSUPP;
	}

	rc = qeth_send_simple_setassparms(card, IPA_SOURCE_MAC,
					  IPA_CMD_ASS_START, 0);
	if (rc)
		dev_warn(&card->gdev->dev,
			"Starting source MAC-address support for %s failed\n",
			QETH_CARD_IFNAME(card));
	return rc;
}

static int qeth_l3_start_ipa_vlan(struct qeth_card *card)
{
	int rc = 0;

	QETH_CARD_TEXT(card, 3, "strtvlan");

	if (!qeth_is_supported(card, IPA_FULL_VLAN)) {
		dev_info(&card->gdev->dev,
			"VLAN not supported on %s\n", QETH_CARD_IFNAME(card));
		return -EOPNOTSUPP;
	}

	rc = qeth_send_simple_setassparms(card, IPA_VLAN_PRIO,
					  IPA_CMD_ASS_START, 0);
	if (rc) {
		dev_warn(&card->gdev->dev,
			"Starting VLAN support for %s failed\n",
			QETH_CARD_IFNAME(card));
	} else {
		dev_info(&card->gdev->dev, "VLAN enabled\n");
	}
	return rc;
}

static int qeth_l3_start_ipa_multicast(struct qeth_card *card)
{
	int rc;

	QETH_CARD_TEXT(card, 3, "stmcast");

	if (!qeth_is_supported(card, IPA_MULTICASTING)) {
		dev_info(&card->gdev->dev,
			"Multicast not supported on %s\n",
			QETH_CARD_IFNAME(card));
		return -EOPNOTSUPP;
	}

	rc = qeth_send_simple_setassparms(card, IPA_MULTICASTING,
					  IPA_CMD_ASS_START, 0);
	if (rc) {
		dev_warn(&card->gdev->dev,
			"Starting multicast support for %s failed\n",
			QETH_CARD_IFNAME(card));
	} else {
		dev_info(&card->gdev->dev, "Multicast enabled\n");
		card->dev->flags |= IFF_MULTICAST;
	}
	return rc;
}

static int qeth_l3_softsetup_ipv6(struct qeth_card *card)
{
	int rc;

	QETH_CARD_TEXT(card, 3, "softipv6");

	if (card->info.type == QETH_CARD_TYPE_IQD)
		goto out;

	rc = qeth_send_simple_setassparms(card, IPA_IPV6,
					  IPA_CMD_ASS_START, 3);
	if (rc) {
		dev_err(&card->gdev->dev,
			"Activating IPv6 support for %s failed\n",
			QETH_CARD_IFNAME(card));
		return rc;
	}
	rc = qeth_send_simple_setassparms_v6(card, IPA_IPV6,
					     IPA_CMD_ASS_START, 0);
	if (rc) {
		dev_err(&card->gdev->dev,
			"Activating IPv6 support for %s failed\n",
			 QETH_CARD_IFNAME(card));
		return rc;
	}
	rc = qeth_send_simple_setassparms_v6(card, IPA_PASSTHRU,
					     IPA_CMD_ASS_START, 0);
	if (rc) {
		dev_warn(&card->gdev->dev,
			"Enabling the passthrough mode for %s failed\n",
			QETH_CARD_IFNAME(card));
		return rc;
	}
out:
	dev_info(&card->gdev->dev, "IPV6 enabled\n");
	return 0;
}

static int qeth_l3_start_ipa_ipv6(struct qeth_card *card)
{
	QETH_CARD_TEXT(card, 3, "strtipv6");

	if (!qeth_is_supported(card, IPA_IPV6)) {
		dev_info(&card->gdev->dev,
			"IPv6 not supported on %s\n", QETH_CARD_IFNAME(card));
		return 0;
	}
	return qeth_l3_softsetup_ipv6(card);
}

static int qeth_l3_start_ipa_broadcast(struct qeth_card *card)
{
	int rc;

	QETH_CARD_TEXT(card, 3, "stbrdcst");
	card->info.broadcast_capable = 0;
	if (!qeth_is_supported(card, IPA_FILTERING)) {
		dev_info(&card->gdev->dev,
			"Broadcast not supported on %s\n",
			QETH_CARD_IFNAME(card));
		rc = -EOPNOTSUPP;
		goto out;
	}
	rc = qeth_send_simple_setassparms(card, IPA_FILTERING,
					  IPA_CMD_ASS_START, 0);
	if (rc) {
		dev_warn(&card->gdev->dev, "Enabling broadcast filtering for "
			"%s failed\n", QETH_CARD_IFNAME(card));
		goto out;
	}

	rc = qeth_send_simple_setassparms(card, IPA_FILTERING,
					  IPA_CMD_ASS_CONFIGURE, 1);
	if (rc) {
		dev_warn(&card->gdev->dev,
			"Setting up broadcast filtering for %s failed\n",
			QETH_CARD_IFNAME(card));
		goto out;
	}
	card->info.broadcast_capable = QETH_BROADCAST_WITH_ECHO;
	dev_info(&card->gdev->dev, "Broadcast enabled\n");
	rc = qeth_send_simple_setassparms(card, IPA_FILTERING,
					  IPA_CMD_ASS_ENABLE, 1);
	if (rc) {
		dev_warn(&card->gdev->dev, "Setting up broadcast echo "
			"filtering for %s failed\n", QETH_CARD_IFNAME(card));
		goto out;
	}
	card->info.broadcast_capable = QETH_BROADCAST_WITHOUT_ECHO;
out:
	if (card->info.broadcast_capable)
		card->dev->flags |= IFF_BROADCAST;
	else
		card->dev->flags &= ~IFF_BROADCAST;
	return rc;
}

static int qeth_l3_start_ipassists(struct qeth_card *card)
{
	QETH_CARD_TEXT(card, 3, "strtipas");

	if (qeth_set_access_ctrl_online(card, 0))
		return -EIO;
	qeth_l3_start_ipa_arp_processing(card);	/* go on*/
	qeth_l3_start_ipa_source_mac(card);	/* go on*/
	qeth_l3_start_ipa_vlan(card);		/* go on*/
	qeth_l3_start_ipa_multicast(card);		/* go on*/
	qeth_l3_start_ipa_ipv6(card);		/* go on*/
	qeth_l3_start_ipa_broadcast(card);		/* go on*/
	return 0;
}

static int qeth_l3_iqd_read_initial_mac_cb(struct qeth_card *card,
		struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->hdr.return_code == 0)
		ether_addr_copy(card->dev->dev_addr,
				cmd->data.create_destroy_addr.unique_id);
	else
		eth_random_addr(card->dev->dev_addr);

	return 0;
}

static int qeth_l3_iqd_read_initial_mac(struct qeth_card *card)
{
	int rc = 0;
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(SETUP, 2, "hsrmac");

	iob = qeth_get_ipacmd_buffer(card, IPA_CMD_CREATE_ADDR,
				     QETH_PROT_IPV6);
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);
	*((__u16 *) &cmd->data.create_destroy_addr.unique_id[6]) =
			card->info.unique_id;

	rc = qeth_send_ipa_cmd(card, iob, qeth_l3_iqd_read_initial_mac_cb,
				NULL);
	return rc;
}

static int qeth_l3_get_unique_id_cb(struct qeth_card *card,
		struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->hdr.return_code == 0)
		card->info.unique_id = *((__u16 *)
				&cmd->data.create_destroy_addr.unique_id[6]);
	else {
		card->info.unique_id =  UNIQUE_ID_IF_CREATE_ADDR_FAILED |
					UNIQUE_ID_NOT_BY_CARD;
		dev_warn(&card->gdev->dev, "The network adapter failed to "
			"generate a unique ID\n");
	}
	return 0;
}

static int qeth_l3_get_unique_id(struct qeth_card *card)
{
	int rc = 0;
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(SETUP, 2, "guniqeid");

	if (!qeth_is_supported(card, IPA_IPV6)) {
		card->info.unique_id =  UNIQUE_ID_IF_CREATE_ADDR_FAILED |
					UNIQUE_ID_NOT_BY_CARD;
		return 0;
	}

	iob = qeth_get_ipacmd_buffer(card, IPA_CMD_CREATE_ADDR,
				     QETH_PROT_IPV6);
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);
	*((__u16 *) &cmd->data.create_destroy_addr.unique_id[6]) =
			card->info.unique_id;

	rc = qeth_send_ipa_cmd(card, iob, qeth_l3_get_unique_id_cb, NULL);
	return rc;
}

static int
qeth_diags_trace_cb(struct qeth_card *card, struct qeth_reply *reply,
			    unsigned long data)
{
	struct qeth_ipa_cmd	   *cmd;
	__u16 rc;

	QETH_DBF_TEXT(SETUP, 2, "diastrcb");

	cmd = (struct qeth_ipa_cmd *)data;
	rc = cmd->hdr.return_code;
	if (rc)
		QETH_CARD_TEXT_(card, 2, "dxter%x", rc);
	switch (cmd->data.diagass.action) {
	case QETH_DIAGS_CMD_TRACE_QUERY:
		break;
	case QETH_DIAGS_CMD_TRACE_DISABLE:
		switch (rc) {
		case 0:
		case IPA_RC_INVALID_SUBCMD:
			card->info.promisc_mode = SET_PROMISC_MODE_OFF;
			dev_info(&card->gdev->dev, "The HiperSockets network "
				"traffic analyzer is deactivated\n");
			break;
		default:
			break;
		}
		break;
	case QETH_DIAGS_CMD_TRACE_ENABLE:
		switch (rc) {
		case 0:
			card->info.promisc_mode = SET_PROMISC_MODE_ON;
			dev_info(&card->gdev->dev, "The HiperSockets network "
				"traffic analyzer is activated\n");
			break;
		case IPA_RC_HARDWARE_AUTH_ERROR:
			dev_warn(&card->gdev->dev, "The device is not "
				"authorized to run as a HiperSockets network "
				"traffic analyzer\n");
			break;
		case IPA_RC_TRACE_ALREADY_ACTIVE:
			dev_warn(&card->gdev->dev, "A HiperSockets "
				"network traffic analyzer is already "
				"active in the HiperSockets LAN\n");
			break;
		default:
			break;
		}
		break;
	default:
		QETH_DBF_MESSAGE(2, "Unknown sniffer action (0x%04x) on %s\n",
			cmd->data.diagass.action, QETH_CARD_IFNAME(card));
	}

	return 0;
}

static int
qeth_diags_trace(struct qeth_card *card, enum qeth_diags_trace_cmds diags_cmd)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd    *cmd;

	QETH_DBF_TEXT(SETUP, 2, "diagtrac");

	iob = qeth_get_ipacmd_buffer(card, IPA_CMD_SET_DIAG_ASS, 0);
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);
	cmd->data.diagass.subcmd_len = 16;
	cmd->data.diagass.subcmd = QETH_DIAGS_CMD_TRACE;
	cmd->data.diagass.type = QETH_DIAGS_TYPE_HIPERSOCKET;
	cmd->data.diagass.action = diags_cmd;
	return qeth_send_ipa_cmd(card, iob, qeth_diags_trace_cb, NULL);
}

static void
qeth_l3_add_mc_to_hash(struct qeth_card *card, struct in_device *in4_dev)
{
	struct ip_mc_list *im4;
	struct qeth_ipaddr *tmp, *ipm;

	QETH_CARD_TEXT(card, 4, "addmc");

	tmp = qeth_l3_get_addr_buffer(QETH_PROT_IPV4);
	if (!tmp)
		return;

	for (im4 = rcu_dereference(in4_dev->mc_list); im4 != NULL;
	     im4 = rcu_dereference(im4->next_rcu)) {
		ip_eth_mc_map(im4->multiaddr, tmp->mac);
		tmp->u.a4.addr = be32_to_cpu(im4->multiaddr);
		tmp->is_multicast = 1;

		ipm = qeth_l3_find_addr_by_ip(card, tmp);
		if (ipm) {
			/* for mcast, by-IP match means full match */
			ipm->disp_flag = QETH_DISP_ADDR_DO_NOTHING;
		} else {
			ipm = qeth_l3_get_addr_buffer(QETH_PROT_IPV4);
			if (!ipm)
				continue;
			ether_addr_copy(ipm->mac, tmp->mac);
			ipm->u.a4.addr = be32_to_cpu(im4->multiaddr);
			ipm->is_multicast = 1;
			ipm->disp_flag = QETH_DISP_ADDR_ADD;
			hash_add(card->ip_mc_htable,
					&ipm->hnode, qeth_l3_ipaddr_hash(ipm));
		}
	}

	kfree(tmp);
}

/* called with rcu_read_lock */
static void qeth_l3_add_vlan_mc(struct qeth_card *card)
{
	struct in_device *in_dev;
	u16 vid;

	QETH_CARD_TEXT(card, 4, "addmcvl");

	if (!qeth_is_supported(card, IPA_FULL_VLAN))
		return;

	for_each_set_bit(vid, card->active_vlans, VLAN_N_VID) {
		struct net_device *netdev;

		netdev = __vlan_find_dev_deep_rcu(card->dev, htons(ETH_P_8021Q),
					      vid);
		if (netdev == NULL ||
		    !(netdev->flags & IFF_UP))
			continue;
		in_dev = __in_dev_get_rcu(netdev);
		if (!in_dev)
			continue;
		qeth_l3_add_mc_to_hash(card, in_dev);
	}
}

static void qeth_l3_add_multicast_ipv4(struct qeth_card *card)
{
	struct in_device *in4_dev;

	QETH_CARD_TEXT(card, 4, "chkmcv4");

	rcu_read_lock();
	in4_dev = __in_dev_get_rcu(card->dev);
	if (in4_dev == NULL)
		goto unlock;
	qeth_l3_add_mc_to_hash(card, in4_dev);
	qeth_l3_add_vlan_mc(card);
unlock:
	rcu_read_unlock();
}

static void qeth_l3_add_mc6_to_hash(struct qeth_card *card,
				    struct inet6_dev *in6_dev)
{
	struct qeth_ipaddr *ipm;
	struct ifmcaddr6 *im6;
	struct qeth_ipaddr *tmp;

	QETH_CARD_TEXT(card, 4, "addmc6");

	tmp = qeth_l3_get_addr_buffer(QETH_PROT_IPV6);
	if (!tmp)
		return;

	for (im6 = in6_dev->mc_list; im6 != NULL; im6 = im6->next) {
		ipv6_eth_mc_map(&im6->mca_addr, tmp->mac);
		memcpy(&tmp->u.a6.addr, &im6->mca_addr.s6_addr,
		       sizeof(struct in6_addr));
		tmp->is_multicast = 1;

		ipm = qeth_l3_find_addr_by_ip(card, tmp);
		if (ipm) {
			/* for mcast, by-IP match means full match */
			ipm->disp_flag = QETH_DISP_ADDR_DO_NOTHING;
			continue;
		}

		ipm = qeth_l3_get_addr_buffer(QETH_PROT_IPV6);
		if (!ipm)
			continue;

		ether_addr_copy(ipm->mac, tmp->mac);
		memcpy(&ipm->u.a6.addr, &im6->mca_addr.s6_addr,
		       sizeof(struct in6_addr));
		ipm->is_multicast = 1;
		ipm->disp_flag = QETH_DISP_ADDR_ADD;
		hash_add(card->ip_mc_htable,
				&ipm->hnode, qeth_l3_ipaddr_hash(ipm));

	}
	kfree(tmp);
}

/* called with rcu_read_lock */
static void qeth_l3_add_vlan_mc6(struct qeth_card *card)
{
	struct inet6_dev *in_dev;
	u16 vid;

	QETH_CARD_TEXT(card, 4, "admc6vl");

	if (!qeth_is_supported(card, IPA_FULL_VLAN))
		return;

	for_each_set_bit(vid, card->active_vlans, VLAN_N_VID) {
		struct net_device *netdev;

		netdev = __vlan_find_dev_deep_rcu(card->dev, htons(ETH_P_8021Q),
					      vid);
		if (netdev == NULL ||
		    !(netdev->flags & IFF_UP))
			continue;
		in_dev = in6_dev_get(netdev);
		if (!in_dev)
			continue;
		read_lock_bh(&in_dev->lock);
		qeth_l3_add_mc6_to_hash(card, in_dev);
		read_unlock_bh(&in_dev->lock);
		in6_dev_put(in_dev);
	}
}

static void qeth_l3_add_multicast_ipv6(struct qeth_card *card)
{
	struct inet6_dev *in6_dev;

	QETH_CARD_TEXT(card, 4, "chkmcv6");

	if (!qeth_is_supported(card, IPA_IPV6))
		return ;
	in6_dev = in6_dev_get(card->dev);
	if (!in6_dev)
		return;

	rcu_read_lock();
	read_lock_bh(&in6_dev->lock);
	qeth_l3_add_mc6_to_hash(card, in6_dev);
	qeth_l3_add_vlan_mc6(card);
	read_unlock_bh(&in6_dev->lock);
	rcu_read_unlock();
	in6_dev_put(in6_dev);
}

static int qeth_l3_vlan_rx_add_vid(struct net_device *dev,
				   __be16 proto, u16 vid)
{
	struct qeth_card *card = dev->ml_priv;

	set_bit(vid, card->active_vlans);
	return 0;
}

static int qeth_l3_vlan_rx_kill_vid(struct net_device *dev,
				    __be16 proto, u16 vid)
{
	struct qeth_card *card = dev->ml_priv;

	QETH_CARD_TEXT_(card, 4, "kid:%d", vid);

	if (qeth_wait_for_threads(card, QETH_RECOVER_THREAD)) {
		QETH_CARD_TEXT(card, 3, "kidREC");
		return 0;
	}
	clear_bit(vid, card->active_vlans);
	qeth_l3_set_rx_mode(dev);
	return 0;
}

static void qeth_l3_rebuild_skb(struct qeth_card *card, struct sk_buff *skb,
				struct qeth_hdr *hdr)
{
	if (!(hdr->hdr.l3.flags & QETH_HDR_PASSTHRU)) {
		u16 prot = (hdr->hdr.l3.flags & QETH_HDR_IPV6) ? ETH_P_IPV6 :
								 ETH_P_IP;
		unsigned char tg_addr[ETH_ALEN];

		skb_reset_network_header(skb);
		switch (hdr->hdr.l3.flags & QETH_HDR_CAST_MASK) {
		case QETH_CAST_MULTICAST:
			if (prot == ETH_P_IP)
				ip_eth_mc_map(ip_hdr(skb)->daddr, tg_addr);
			else
				ipv6_eth_mc_map(&ipv6_hdr(skb)->daddr, tg_addr);

			card->stats.multicast++;
			break;
		case QETH_CAST_BROADCAST:
			ether_addr_copy(tg_addr, card->dev->broadcast);
			card->stats.multicast++;
			break;
		default:
			if (card->options.sniffer)
				skb->pkt_type = PACKET_OTHERHOST;
			ether_addr_copy(tg_addr, card->dev->dev_addr);
		}

		if (hdr->hdr.l3.ext_flags & QETH_HDR_EXT_SRC_MAC_ADDR)
			card->dev->header_ops->create(skb, card->dev, prot,
				tg_addr, &hdr->hdr.l3.next_hop.rx.src_mac,
				skb->len);
		else
			card->dev->header_ops->create(skb, card->dev, prot,
				tg_addr, "FAKELL", skb->len);
	}

	skb->protocol = eth_type_trans(skb, card->dev);

	/* copy VLAN tag from hdr into skb */
	if (!card->options.sniffer &&
	    (hdr->hdr.l3.ext_flags & (QETH_HDR_EXT_VLAN_FRAME |
				      QETH_HDR_EXT_INCLUDE_VLAN_TAG))) {
		u16 tag = (hdr->hdr.l3.ext_flags & QETH_HDR_EXT_VLAN_FRAME) ?
				hdr->hdr.l3.vlan_id :
				hdr->hdr.l3.next_hop.rx.vlan_id;
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), tag);
	}

	qeth_rx_csum(card, skb, hdr->hdr.l3.ext_flags);
}

static int qeth_l3_process_inbound_buffer(struct qeth_card *card,
				int budget, int *done)
{
	int work_done = 0;
	struct sk_buff *skb;
	struct qeth_hdr *hdr;
	unsigned int len;
	__u16 magic;

	*done = 0;
	WARN_ON_ONCE(!budget);
	while (budget) {
		skb = qeth_core_get_next_skb(card,
			&card->qdio.in_q->bufs[card->rx.b_index],
			&card->rx.b_element, &card->rx.e_offset, &hdr);
		if (!skb) {
			*done = 1;
			break;
		}
		switch (hdr->hdr.l3.id) {
		case QETH_HEADER_TYPE_LAYER3:
			magic = *(__u16 *)skb->data;
			if ((card->info.type == QETH_CARD_TYPE_IQD) &&
			    (magic == ETH_P_AF_IUCV)) {
				skb->protocol = cpu_to_be16(ETH_P_AF_IUCV);
				len = skb->len;
				card->dev->header_ops->create(skb, card->dev, 0,
					card->dev->dev_addr, "FAKELL", len);
				skb_reset_mac_header(skb);
				netif_receive_skb(skb);
			} else {
				qeth_l3_rebuild_skb(card, skb, hdr);
				len = skb->len;
				napi_gro_receive(&card->napi, skb);
			}
			break;
		case QETH_HEADER_TYPE_LAYER2: /* for HiperSockets sniffer */
			skb->protocol = eth_type_trans(skb, skb->dev);
			len = skb->len;
			netif_receive_skb(skb);
			break;
		default:
			dev_kfree_skb_any(skb);
			QETH_CARD_TEXT(card, 3, "inbunkno");
			QETH_DBF_HEX(CTRL, 3, hdr, QETH_DBF_CTRL_LEN);
			continue;
		}
		work_done++;
		budget--;
		card->stats.rx_packets++;
		card->stats.rx_bytes += len;
	}
	return work_done;
}

static void qeth_l3_stop_card(struct qeth_card *card, int recovery_mode)
{
	QETH_DBF_TEXT(SETUP, 2, "stopcard");
	QETH_DBF_HEX(SETUP, 2, &card, sizeof(void *));

	qeth_set_allowed_threads(card, 0, 1);
	if (card->options.sniffer &&
	    (card->info.promisc_mode == SET_PROMISC_MODE_ON))
		qeth_diags_trace(card, QETH_DIAGS_CMD_TRACE_DISABLE);
	if (card->read.state == CH_STATE_UP &&
	    card->write.state == CH_STATE_UP &&
	    (card->state == CARD_STATE_UP)) {
		if (recovery_mode)
			qeth_l3_stop(card->dev);
		else {
			rtnl_lock();
			dev_close(card->dev);
			rtnl_unlock();
		}
		card->state = CARD_STATE_SOFTSETUP;
	}
	if (card->state == CARD_STATE_SOFTSETUP) {
		qeth_l3_clear_ip_htable(card, 1);
		qeth_clear_ipacmd_list(card);
		card->state = CARD_STATE_HARDSETUP;
	}
	if (card->state == CARD_STATE_HARDSETUP) {
		qeth_qdio_clear_card(card, 0);
		qeth_clear_qdio_buffers(card);
		qeth_clear_working_pool_list(card);
		card->state = CARD_STATE_DOWN;
	}
	if (card->state == CARD_STATE_DOWN) {
		qeth_clear_cmd_buffers(&card->read);
		qeth_clear_cmd_buffers(&card->write);
	}
}

/*
 * test for and Switch promiscuous mode (on or off)
 *  either for guestlan or HiperSocket Sniffer
 */
static void
qeth_l3_handle_promisc_mode(struct qeth_card *card)
{
	struct net_device *dev = card->dev;

	if (((dev->flags & IFF_PROMISC) &&
	     (card->info.promisc_mode == SET_PROMISC_MODE_ON)) ||
	    (!(dev->flags & IFF_PROMISC) &&
	     (card->info.promisc_mode == SET_PROMISC_MODE_OFF)))
		return;

	if (card->info.guestlan) {		/* Guestlan trace */
		if (qeth_adp_supported(card, IPA_SETADP_SET_PROMISC_MODE))
			qeth_setadp_promisc_mode(card);
	} else if (card->options.sniffer &&	/* HiperSockets trace */
		   qeth_adp_supported(card, IPA_SETADP_SET_DIAG_ASSIST)) {
		if (dev->flags & IFF_PROMISC) {
			QETH_CARD_TEXT(card, 3, "+promisc");
			qeth_diags_trace(card, QETH_DIAGS_CMD_TRACE_ENABLE);
		} else {
			QETH_CARD_TEXT(card, 3, "-promisc");
			qeth_diags_trace(card, QETH_DIAGS_CMD_TRACE_DISABLE);
		}
	}
}

static void qeth_l3_set_rx_mode(struct net_device *dev)
{
	struct qeth_card *card = dev->ml_priv;
	struct qeth_ipaddr *addr;
	struct hlist_node *tmp;
	int i, rc;

	QETH_CARD_TEXT(card, 3, "setmulti");
	if (qeth_threads_running(card, QETH_RECOVER_THREAD) &&
	    (card->state != CARD_STATE_UP))
		return;
	if (!card->options.sniffer) {
		spin_lock_bh(&card->mclock);

		qeth_l3_add_multicast_ipv4(card);
		qeth_l3_add_multicast_ipv6(card);

		hash_for_each_safe(card->ip_mc_htable, i, tmp, addr, hnode) {
			switch (addr->disp_flag) {
			case QETH_DISP_ADDR_DELETE:
				rc = qeth_l3_deregister_addr_entry(card, addr);
				if (!rc || rc == IPA_RC_MC_ADDR_NOT_FOUND) {
					hash_del(&addr->hnode);
					kfree(addr);
				}
				break;
			case QETH_DISP_ADDR_ADD:
				rc = qeth_l3_register_addr_entry(card, addr);
				if (rc && rc != IPA_RC_LAN_OFFLINE) {
					hash_del(&addr->hnode);
					kfree(addr);
					break;
				}
				addr->ref_counter = 1;
				/* fall through */
			default:
				/* for next call to set_rx_mode(): */
				addr->disp_flag = QETH_DISP_ADDR_DELETE;
			}
		}

		spin_unlock_bh(&card->mclock);

		if (!qeth_adp_supported(card, IPA_SETADP_SET_PROMISC_MODE))
			return;
	}
	qeth_l3_handle_promisc_mode(card);
}

static const char *qeth_l3_arp_get_error_cause(int *rc)
{
	switch (*rc) {
	case QETH_IPA_ARP_RC_FAILED:
		*rc = -EIO;
		return "operation failed";
	case QETH_IPA_ARP_RC_NOTSUPP:
		*rc = -EOPNOTSUPP;
		return "operation not supported";
	case QETH_IPA_ARP_RC_OUT_OF_RANGE:
		*rc = -EINVAL;
		return "argument out of range";
	case QETH_IPA_ARP_RC_Q_NOTSUPP:
		*rc = -EOPNOTSUPP;
		return "query operation not supported";
	case QETH_IPA_ARP_RC_Q_NO_DATA:
		*rc = -ENOENT;
		return "no query data available";
	default:
		return "unknown error";
	}
}

static int qeth_l3_arp_set_no_entries(struct qeth_card *card, int no_entries)
{
	int tmp;
	int rc;

	QETH_CARD_TEXT(card, 3, "arpstnoe");

	/*
	 * currently GuestLAN only supports the ARP assist function
	 * IPA_CMD_ASS_ARP_QUERY_INFO, but not IPA_CMD_ASS_ARP_SET_NO_ENTRIES;
	 * thus we say EOPNOTSUPP for this ARP function
	 */
	if (card->info.guestlan)
		return -EOPNOTSUPP;
	if (!qeth_is_supported(card, IPA_ARP_PROCESSING)) {
		return -EOPNOTSUPP;
	}
	rc = qeth_send_simple_setassparms(card, IPA_ARP_PROCESSING,
					  IPA_CMD_ASS_ARP_SET_NO_ENTRIES,
					  no_entries);
	if (rc) {
		tmp = rc;
		QETH_DBF_MESSAGE(2, "Could not set number of ARP entries on "
			"%s: %s (0x%x/%d)\n", QETH_CARD_IFNAME(card),
			qeth_l3_arp_get_error_cause(&rc), tmp, tmp);
	}
	return rc;
}

static __u32 get_arp_entry_size(struct qeth_card *card,
			struct qeth_arp_query_data *qdata,
			struct qeth_arp_entrytype *type, __u8 strip_entries)
{
	__u32 rc;
	__u8 is_hsi;

	is_hsi = qdata->reply_bits == 5;
	if (type->ip == QETHARP_IP_ADDR_V4) {
		QETH_CARD_TEXT(card, 4, "arpev4");
		if (strip_entries) {
			rc = is_hsi ? sizeof(struct qeth_arp_qi_entry5_short) :
				sizeof(struct qeth_arp_qi_entry7_short);
		} else {
			rc = is_hsi ? sizeof(struct qeth_arp_qi_entry5) :
				sizeof(struct qeth_arp_qi_entry7);
		}
	} else if (type->ip == QETHARP_IP_ADDR_V6) {
		QETH_CARD_TEXT(card, 4, "arpev6");
		if (strip_entries) {
			rc = is_hsi ?
				sizeof(struct qeth_arp_qi_entry5_short_ipv6) :
				sizeof(struct qeth_arp_qi_entry7_short_ipv6);
		} else {
			rc = is_hsi ?
				sizeof(struct qeth_arp_qi_entry5_ipv6) :
				sizeof(struct qeth_arp_qi_entry7_ipv6);
		}
	} else {
		QETH_CARD_TEXT(card, 4, "arpinv");
		rc = 0;
	}

	return rc;
}

static int arpentry_matches_prot(struct qeth_arp_entrytype *type, __u16 prot)
{
	return (type->ip == QETHARP_IP_ADDR_V4 && prot == QETH_PROT_IPV4) ||
		(type->ip == QETHARP_IP_ADDR_V6 && prot == QETH_PROT_IPV6);
}

static int qeth_l3_arp_query_cb(struct qeth_card *card,
		struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_arp_query_data *qdata;
	struct qeth_arp_query_info *qinfo;
	int i;
	int e;
	int entrybytes_done;
	int stripped_bytes;
	__u8 do_strip_entries;

	QETH_CARD_TEXT(card, 3, "arpquecb");

	qinfo = (struct qeth_arp_query_info *) reply->param;
	cmd = (struct qeth_ipa_cmd *) data;
	QETH_CARD_TEXT_(card, 4, "%i", cmd->hdr.prot_version);
	if (cmd->hdr.return_code) {
		QETH_CARD_TEXT(card, 4, "arpcberr");
		QETH_CARD_TEXT_(card, 4, "%i", cmd->hdr.return_code);
		return 0;
	}
	if (cmd->data.setassparms.hdr.return_code) {
		cmd->hdr.return_code = cmd->data.setassparms.hdr.return_code;
		QETH_CARD_TEXT(card, 4, "setaperr");
		QETH_CARD_TEXT_(card, 4, "%i", cmd->hdr.return_code);
		return 0;
	}
	qdata = &cmd->data.setassparms.data.query_arp;
	QETH_CARD_TEXT_(card, 4, "anoen%i", qdata->no_entries);

	do_strip_entries = (qinfo->mask_bits & QETH_QARP_STRIP_ENTRIES) > 0;
	stripped_bytes = do_strip_entries ? QETH_QARP_MEDIASPECIFIC_BYTES : 0;
	entrybytes_done = 0;
	for (e = 0; e < qdata->no_entries; ++e) {
		char *cur_entry;
		__u32 esize;
		struct qeth_arp_entrytype *etype;

		cur_entry = &qdata->data + entrybytes_done;
		etype = &((struct qeth_arp_qi_entry5 *) cur_entry)->type;
		if (!arpentry_matches_prot(etype, cmd->hdr.prot_version)) {
			QETH_CARD_TEXT(card, 4, "pmis");
			QETH_CARD_TEXT_(card, 4, "%i", etype->ip);
			break;
		}
		esize = get_arp_entry_size(card, qdata, etype,
			do_strip_entries);
		QETH_CARD_TEXT_(card, 5, "esz%i", esize);
		if (!esize)
			break;

		if ((qinfo->udata_len - qinfo->udata_offset) < esize) {
			QETH_CARD_TEXT_(card, 4, "qaer3%i", -ENOMEM);
			cmd->hdr.return_code = IPA_RC_ENOMEM;
			goto out_error;
		}

		memcpy(qinfo->udata + qinfo->udata_offset,
			&qdata->data + entrybytes_done + stripped_bytes,
			esize);
		entrybytes_done += esize + stripped_bytes;
		qinfo->udata_offset += esize;
		++qinfo->no_entries;
	}
	/* check if all replies received ... */
	if (cmd->data.setassparms.hdr.seq_no <
	    cmd->data.setassparms.hdr.number_of_replies)
		return 1;
	QETH_CARD_TEXT_(card, 4, "nove%i", qinfo->no_entries);
	memcpy(qinfo->udata, &qinfo->no_entries, 4);
	/* keep STRIP_ENTRIES flag so the user program can distinguish
	 * stripped entries from normal ones */
	if (qinfo->mask_bits & QETH_QARP_STRIP_ENTRIES)
		qdata->reply_bits |= QETH_QARP_STRIP_ENTRIES;
	memcpy(qinfo->udata + QETH_QARP_MASK_OFFSET, &qdata->reply_bits, 2);
	QETH_CARD_TEXT_(card, 4, "rc%i", 0);
	return 0;
out_error:
	i = 0;
	memcpy(qinfo->udata, &i, 4);
	return 0;
}

static int qeth_l3_send_ipa_arp_cmd(struct qeth_card *card,
		struct qeth_cmd_buffer *iob, int len,
		int (*reply_cb)(struct qeth_card *, struct qeth_reply *,
			unsigned long),
		void *reply_param)
{
	QETH_CARD_TEXT(card, 4, "sendarp");

	memcpy(iob->data, IPA_PDU_HEADER, IPA_PDU_HEADER_SIZE);
	memcpy(QETH_IPA_CMD_DEST_ADDR(iob->data),
	       &card->token.ulp_connection_r, QETH_MPC_TOKEN_LENGTH);
	return qeth_send_control_data(card, IPA_PDU_HEADER_SIZE + len, iob,
				      reply_cb, reply_param);
}

static int qeth_l3_query_arp_cache_info(struct qeth_card *card,
	enum qeth_prot_versions prot,
	struct qeth_arp_query_info *qinfo)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;
	int tmp;
	int rc;

	QETH_CARD_TEXT_(card, 3, "qarpipv%i", prot);

	iob = qeth_get_setassparms_cmd(card, IPA_ARP_PROCESSING,
				       IPA_CMD_ASS_ARP_QUERY_INFO,
				       sizeof(struct qeth_arp_query_data)
						- sizeof(char),
				       prot);
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);
	cmd->data.setassparms.data.query_arp.request_bits = 0x000F;
	cmd->data.setassparms.data.query_arp.reply_bits = 0;
	cmd->data.setassparms.data.query_arp.no_entries = 0;
	rc = qeth_l3_send_ipa_arp_cmd(card, iob,
			   QETH_SETASS_BASE_LEN+QETH_ARP_CMD_LEN,
			   qeth_l3_arp_query_cb, (void *)qinfo);
	if (rc) {
		tmp = rc;
		QETH_DBF_MESSAGE(2,
			"Error while querying ARP cache on %s: %s "
			"(0x%x/%d)\n", QETH_CARD_IFNAME(card),
			qeth_l3_arp_get_error_cause(&rc), tmp, tmp);
	}

	return rc;
}

static int qeth_l3_arp_query(struct qeth_card *card, char __user *udata)
{
	struct qeth_arp_query_info qinfo = {0, };
	int rc;

	QETH_CARD_TEXT(card, 3, "arpquery");

	if (!qeth_is_supported(card,/*IPA_QUERY_ARP_ADDR_INFO*/
			       IPA_ARP_PROCESSING)) {
		QETH_CARD_TEXT(card, 3, "arpqnsup");
		rc = -EOPNOTSUPP;
		goto out;
	}
	/* get size of userspace buffer and mask_bits -> 6 bytes */
	if (copy_from_user(&qinfo, udata, 6)) {
		rc = -EFAULT;
		goto out;
	}
	qinfo.udata = kzalloc(qinfo.udata_len, GFP_KERNEL);
	if (!qinfo.udata) {
		rc = -ENOMEM;
		goto out;
	}
	qinfo.udata_offset = QETH_QARP_ENTRIES_OFFSET;
	rc = qeth_l3_query_arp_cache_info(card, QETH_PROT_IPV4, &qinfo);
	if (rc) {
		if (copy_to_user(udata, qinfo.udata, 4))
			rc = -EFAULT;
		goto free_and_out;
	}
	if (qinfo.mask_bits & QETH_QARP_WITH_IPV6) {
		/* fails in case of GuestLAN QDIO mode */
		qeth_l3_query_arp_cache_info(card, QETH_PROT_IPV6, &qinfo);
	}
	if (copy_to_user(udata, qinfo.udata, qinfo.udata_len)) {
		QETH_CARD_TEXT(card, 4, "qactf");
		rc = -EFAULT;
		goto free_and_out;
	}
	QETH_CARD_TEXT(card, 4, "qacts");

free_and_out:
	kfree(qinfo.udata);
out:
	return rc;
}

static int qeth_l3_arp_add_entry(struct qeth_card *card,
				struct qeth_arp_cache_entry *entry)
{
	struct qeth_cmd_buffer *iob;
	char buf[16];
	int tmp;
	int rc;

	QETH_CARD_TEXT(card, 3, "arpadent");

	/*
	 * currently GuestLAN only supports the ARP assist function
	 * IPA_CMD_ASS_ARP_QUERY_INFO, but not IPA_CMD_ASS_ARP_ADD_ENTRY;
	 * thus we say EOPNOTSUPP for this ARP function
	 */
	if (card->info.guestlan)
		return -EOPNOTSUPP;
	if (!qeth_is_supported(card, IPA_ARP_PROCESSING)) {
		return -EOPNOTSUPP;
	}

	iob = qeth_get_setassparms_cmd(card, IPA_ARP_PROCESSING,
				       IPA_CMD_ASS_ARP_ADD_ENTRY,
				       sizeof(struct qeth_arp_cache_entry),
				       QETH_PROT_IPV4);
	if (!iob)
		return -ENOMEM;
	rc = qeth_send_setassparms(card, iob,
				   sizeof(struct qeth_arp_cache_entry),
				   (unsigned long) entry,
				   qeth_setassparms_cb, NULL);
	if (rc) {
		tmp = rc;
		qeth_l3_ipaddr4_to_string((u8 *)entry->ipaddr, buf);
		QETH_DBF_MESSAGE(2, "Could not add ARP entry for address %s "
			"on %s: %s (0x%x/%d)\n", buf, QETH_CARD_IFNAME(card),
			qeth_l3_arp_get_error_cause(&rc), tmp, tmp);
	}
	return rc;
}

static int qeth_l3_arp_remove_entry(struct qeth_card *card,
				struct qeth_arp_cache_entry *entry)
{
	struct qeth_cmd_buffer *iob;
	char buf[16] = {0, };
	int tmp;
	int rc;

	QETH_CARD_TEXT(card, 3, "arprment");

	/*
	 * currently GuestLAN only supports the ARP assist function
	 * IPA_CMD_ASS_ARP_QUERY_INFO, but not IPA_CMD_ASS_ARP_REMOVE_ENTRY;
	 * thus we say EOPNOTSUPP for this ARP function
	 */
	if (card->info.guestlan)
		return -EOPNOTSUPP;
	if (!qeth_is_supported(card, IPA_ARP_PROCESSING)) {
		return -EOPNOTSUPP;
	}
	memcpy(buf, entry, 12);
	iob = qeth_get_setassparms_cmd(card, IPA_ARP_PROCESSING,
				       IPA_CMD_ASS_ARP_REMOVE_ENTRY,
				       12,
				       QETH_PROT_IPV4);
	if (!iob)
		return -ENOMEM;
	rc = qeth_send_setassparms(card, iob,
				   12, (unsigned long)buf,
				   qeth_setassparms_cb, NULL);
	if (rc) {
		tmp = rc;
		memset(buf, 0, 16);
		qeth_l3_ipaddr4_to_string((u8 *)entry->ipaddr, buf);
		QETH_DBF_MESSAGE(2, "Could not delete ARP entry for address %s"
			" on %s: %s (0x%x/%d)\n", buf, QETH_CARD_IFNAME(card),
			qeth_l3_arp_get_error_cause(&rc), tmp, tmp);
	}
	return rc;
}

static int qeth_l3_arp_flush_cache(struct qeth_card *card)
{
	int rc;
	int tmp;

	QETH_CARD_TEXT(card, 3, "arpflush");

	/*
	 * currently GuestLAN only supports the ARP assist function
	 * IPA_CMD_ASS_ARP_QUERY_INFO, but not IPA_CMD_ASS_ARP_FLUSH_CACHE;
	 * thus we say EOPNOTSUPP for this ARP function
	*/
	if (card->info.guestlan || (card->info.type == QETH_CARD_TYPE_IQD))
		return -EOPNOTSUPP;
	if (!qeth_is_supported(card, IPA_ARP_PROCESSING)) {
		return -EOPNOTSUPP;
	}
	rc = qeth_send_simple_setassparms(card, IPA_ARP_PROCESSING,
					  IPA_CMD_ASS_ARP_FLUSH_CACHE, 0);
	if (rc) {
		tmp = rc;
		QETH_DBF_MESSAGE(2, "Could not flush ARP cache on %s: %s "
			"(0x%x/%d)\n", QETH_CARD_IFNAME(card),
			qeth_l3_arp_get_error_cause(&rc), tmp, tmp);
	}
	return rc;
}

static int qeth_l3_do_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct qeth_card *card = dev->ml_priv;
	struct qeth_arp_cache_entry arp_entry;
	int rc = 0;

	switch (cmd) {
	case SIOC_QETH_ARP_SET_NO_ENTRIES:
		if (!capable(CAP_NET_ADMIN)) {
			rc = -EPERM;
			break;
		}
		rc = qeth_l3_arp_set_no_entries(card, rq->ifr_ifru.ifru_ivalue);
		break;
	case SIOC_QETH_ARP_QUERY_INFO:
		if (!capable(CAP_NET_ADMIN)) {
			rc = -EPERM;
			break;
		}
		rc = qeth_l3_arp_query(card, rq->ifr_ifru.ifru_data);
		break;
	case SIOC_QETH_ARP_ADD_ENTRY:
		if (!capable(CAP_NET_ADMIN)) {
			rc = -EPERM;
			break;
		}
		if (copy_from_user(&arp_entry, rq->ifr_ifru.ifru_data,
				   sizeof(struct qeth_arp_cache_entry)))
			rc = -EFAULT;
		else
			rc = qeth_l3_arp_add_entry(card, &arp_entry);
		break;
	case SIOC_QETH_ARP_REMOVE_ENTRY:
		if (!capable(CAP_NET_ADMIN)) {
			rc = -EPERM;
			break;
		}
		if (copy_from_user(&arp_entry, rq->ifr_ifru.ifru_data,
				   sizeof(struct qeth_arp_cache_entry)))
			rc = -EFAULT;
		else
			rc = qeth_l3_arp_remove_entry(card, &arp_entry);
		break;
	case SIOC_QETH_ARP_FLUSH_CACHE:
		if (!capable(CAP_NET_ADMIN)) {
			rc = -EPERM;
			break;
		}
		rc = qeth_l3_arp_flush_cache(card);
		break;
	default:
		rc = -EOPNOTSUPP;
	}
	return rc;
}

static int qeth_l3_get_cast_type(struct sk_buff *skb)
{
	struct neighbour *n = NULL;
	struct dst_entry *dst;

	rcu_read_lock();
	dst = skb_dst(skb);
	if (dst)
		n = dst_neigh_lookup_skb(dst, skb);
	if (n) {
		int cast_type = n->type;

		rcu_read_unlock();
		neigh_release(n);
		if ((cast_type == RTN_BROADCAST) ||
		    (cast_type == RTN_MULTICAST) ||
		    (cast_type == RTN_ANYCAST))
			return cast_type;
		return RTN_UNICAST;
	}
	rcu_read_unlock();

	/* no neighbour (eg AF_PACKET), fall back to target's IP address ... */
	if (be16_to_cpu(skb->protocol) == ETH_P_IPV6)
		return ipv6_addr_is_multicast(&ipv6_hdr(skb)->daddr) ?
				RTN_MULTICAST : RTN_UNICAST;
	else if (be16_to_cpu(skb->protocol) == ETH_P_IP)
		return ipv4_is_multicast(ip_hdr(skb)->daddr) ?
				RTN_MULTICAST : RTN_UNICAST;

	/* ... and MAC address */
	if (ether_addr_equal_64bits(eth_hdr(skb)->h_dest, skb->dev->broadcast))
		return RTN_BROADCAST;
	if (is_multicast_ether_addr(eth_hdr(skb)->h_dest))
		return RTN_MULTICAST;

	/* default to unicast */
	return RTN_UNICAST;
}

static void qeth_l3_fill_af_iucv_hdr(struct qeth_hdr *hdr, struct sk_buff *skb,
				     unsigned int data_len)
{
	char daddr[16];
	struct af_iucv_trans_hdr *iucv_hdr;

	memset(hdr, 0, sizeof(struct qeth_hdr));
	hdr->hdr.l3.id = QETH_HEADER_TYPE_LAYER3;
	hdr->hdr.l3.length = data_len;
	hdr->hdr.l3.flags = QETH_HDR_IPV6 | QETH_CAST_UNICAST;

	iucv_hdr = (struct af_iucv_trans_hdr *)(skb_mac_header(skb) + ETH_HLEN);
	memset(daddr, 0, sizeof(daddr));
	daddr[0] = 0xfe;
	daddr[1] = 0x80;
	memcpy(&daddr[8], iucv_hdr->destUserID, 8);
	memcpy(hdr->hdr.l3.next_hop.ipv6_addr, daddr, 16);
}

static u8 qeth_l3_cast_type_to_flag(int cast_type)
{
	if (cast_type == RTN_MULTICAST)
		return QETH_CAST_MULTICAST;
	if (cast_type == RTN_ANYCAST)
		return QETH_CAST_ANYCAST;
	if (cast_type == RTN_BROADCAST)
		return QETH_CAST_BROADCAST;
	return QETH_CAST_UNICAST;
}

static void qeth_l3_fill_header(struct qeth_card *card, struct qeth_hdr *hdr,
				struct sk_buff *skb, int ipv, int cast_type,
				unsigned int data_len)
{
	memset(hdr, 0, sizeof(struct qeth_hdr));
	hdr->hdr.l3.id = QETH_HEADER_TYPE_LAYER3;
	hdr->hdr.l3.length = data_len;

	/*
	 * before we're going to overwrite this location with next hop ip.
	 * v6 uses passthrough, v4 sets the tag in the QDIO header.
	 */
	if (skb_vlan_tag_present(skb)) {
		if ((ipv == 4) || (card->info.type == QETH_CARD_TYPE_IQD))
			hdr->hdr.l3.ext_flags = QETH_HDR_EXT_VLAN_FRAME;
		else
			hdr->hdr.l3.ext_flags = QETH_HDR_EXT_INCLUDE_VLAN_TAG;
		hdr->hdr.l3.vlan_id = skb_vlan_tag_get(skb);
	}

	if (!skb_is_gso(skb) && skb->ip_summed == CHECKSUM_PARTIAL) {
		qeth_tx_csum(skb, &hdr->hdr.l3.ext_flags, ipv);
		if (card->options.performance_stats)
			card->perf_stats.tx_csum++;
	}

	/* OSA only: */
	if (!ipv) {
		hdr->hdr.l3.flags = QETH_HDR_PASSTHRU;
		if (ether_addr_equal_64bits(eth_hdr(skb)->h_dest,
					    skb->dev->broadcast))
			hdr->hdr.l3.flags |= QETH_CAST_BROADCAST;
		else
			hdr->hdr.l3.flags |= (cast_type == RTN_MULTICAST) ?
				QETH_CAST_MULTICAST : QETH_CAST_UNICAST;
		return;
	}

	hdr->hdr.l3.flags = qeth_l3_cast_type_to_flag(cast_type);
	rcu_read_lock();
	if (ipv == 4) {
		struct rtable *rt = skb_rtable(skb);

		*((__be32 *) &hdr->hdr.l3.next_hop.ipv4.addr) = (rt) ?
				rt_nexthop(rt, ip_hdr(skb)->daddr) :
				ip_hdr(skb)->daddr;
	} else {
		/* IPv6 */
		const struct rt6_info *rt = skb_rt6_info(skb);
		const struct in6_addr *next_hop;

		if (rt && !ipv6_addr_any(&rt->rt6i_gateway))
			next_hop = &rt->rt6i_gateway;
		else
			next_hop = &ipv6_hdr(skb)->daddr;
		memcpy(hdr->hdr.l3.next_hop.ipv6_addr, next_hop, 16);

		hdr->hdr.l3.flags |= QETH_HDR_IPV6;
		if (card->info.type != QETH_CARD_TYPE_IQD)
			hdr->hdr.l3.flags |= QETH_HDR_PASSTHRU;
	}
	rcu_read_unlock();
}

static void qeth_tso_fill_header(struct qeth_card *card,
		struct qeth_hdr *qhdr, struct sk_buff *skb)
{
	struct qeth_hdr_tso *hdr = (struct qeth_hdr_tso *)qhdr;
	struct tcphdr *tcph = tcp_hdr(skb);
	struct iphdr *iph = ip_hdr(skb);
	struct ipv6hdr *ip6h = ipv6_hdr(skb);

	/*fix header to TSO values ...*/
	hdr->hdr.hdr.l3.id = QETH_HEADER_TYPE_TSO;
	/*set values which are fix for the first approach ...*/
	hdr->ext.hdr_tot_len = (__u16) sizeof(struct qeth_hdr_ext_tso);
	hdr->ext.imb_hdr_no  = 1;
	hdr->ext.hdr_type    = 1;
	hdr->ext.hdr_version = 1;
	hdr->ext.hdr_len     = 28;
	/*insert non-fix values */
	hdr->ext.mss = skb_shinfo(skb)->gso_size;
	hdr->ext.dg_hdr_len = (__u16)(ip_hdrlen(skb) + tcp_hdrlen(skb));
	hdr->ext.payload_len = (__u16)(skb->len - hdr->ext.dg_hdr_len -
				       sizeof(struct qeth_hdr_tso));
	tcph->check = 0;
	if (be16_to_cpu(skb->protocol) == ETH_P_IPV6) {
		ip6h->payload_len = 0;
		tcph->check = ~csum_ipv6_magic(&ip6h->saddr, &ip6h->daddr,
					       0, IPPROTO_TCP, 0);
	} else {
		/*OSA want us to set these values ...*/
		tcph->check = ~csum_tcpudp_magic(iph->saddr, iph->daddr,
					 0, IPPROTO_TCP, 0);
		iph->tot_len = 0;
		iph->check = 0;
	}
}

/**
 * qeth_l3_get_elements_no_tso() - find number of SBALEs for skb data for tso
 * @card:			   qeth card structure, to check max. elems.
 * @skb:			   SKB address
 * @extra_elems:		   extra elems needed, to check against max.
 *
 * Returns the number of pages, and thus QDIO buffer elements, needed to cover
 * skb data, including linear part and fragments, but excluding TCP header.
 * (Exclusion of TCP header distinguishes it from qeth_get_elements_no().)
 * Checks if the result plus extra_elems fits under the limit for the card.
 * Returns 0 if it does not.
 * Note: extra_elems is not included in the returned result.
 */
static int qeth_l3_get_elements_no_tso(struct qeth_card *card,
			struct sk_buff *skb, int extra_elems)
{
	addr_t start = (addr_t)tcp_hdr(skb) + tcp_hdrlen(skb);
	addr_t end = (addr_t)skb->data + skb_headlen(skb);
	int elements = qeth_get_elements_for_frags(skb);

	if (start != end)
		elements += qeth_get_elements_for_range(start, end);

	if ((elements + extra_elems) > QETH_MAX_BUFFER_ELEMENTS(card)) {
		QETH_DBF_MESSAGE(2,
	"Invalid size of TSO IP packet (Number=%d / Length=%d). Discarded.\n",
				elements + extra_elems, skb->len);
		return 0;
	}
	return elements;
}

static int qeth_l3_xmit_offload(struct qeth_card *card, struct sk_buff *skb,
				struct qeth_qdio_out_q *queue, int ipv,
				int cast_type)
{
	const unsigned int hw_hdr_len = sizeof(struct qeth_hdr);
	unsigned char eth_hdr[ETH_HLEN];
	unsigned int hdr_elements = 0;
	struct qeth_hdr *hdr = NULL;
	int elements, push_len, rc;
	unsigned int hd_len = 0;
	unsigned int frame_len;
	bool is_sg;

	/* compress skb to fit into one IO buffer: */
	if (!qeth_get_elements_no(card, skb, 0, 0)) {
		rc = skb_linearize(skb);

		if (card->options.performance_stats) {
			if (rc)
				card->perf_stats.tx_linfail++;
			else
				card->perf_stats.tx_lin++;
		}
		if (rc)
			return rc;
	}

	/* re-use the L2 header area for the HW header: */
	rc = skb_cow_head(skb, hw_hdr_len - ETH_HLEN);
	if (rc)
		return rc;
	skb_copy_from_linear_data(skb, eth_hdr, ETH_HLEN);
	skb_pull(skb, ETH_HLEN);
	frame_len = skb->len;

	push_len = qeth_push_hdr(skb, &hdr, hw_hdr_len);
	if (push_len < 0)
		return push_len;
	if (!push_len) {
		/* hdr was added discontiguous from skb->data */
		hd_len = hw_hdr_len;
		hdr_elements = 1;
	}

	elements = qeth_get_elements_no(card, skb, hdr_elements, 0);
	if (!elements) {
		rc = -E2BIG;
		goto out;
	}
	elements += hdr_elements;

	if (skb->protocol == htons(ETH_P_AF_IUCV))
		qeth_l3_fill_af_iucv_hdr(hdr, skb, frame_len);
	else
		qeth_l3_fill_header(card, hdr, skb, ipv, cast_type, frame_len);

	is_sg = skb_is_nonlinear(skb);
	if (IS_IQD(card)) {
		rc = qeth_do_send_packet_fast(queue, skb, hdr, 0, hd_len);
	} else {
		/* TODO: drop skb_orphan() once TX completion is fast enough */
		skb_orphan(skb);
		rc = qeth_do_send_packet(card, queue, skb, hdr, 0, hd_len,
					 elements);
	}
out:
	if (!rc) {
		if (card->options.performance_stats) {
			card->perf_stats.buf_elements_sent += elements;
			if (is_sg)
				card->perf_stats.sg_skbs_sent++;
		}
	} else {
		if (!push_len)
			kmem_cache_free(qeth_core_header_cache, hdr);
		if (rc == -EBUSY) {
			/* roll back to ETH header */
			skb_pull(skb, push_len);
			skb_push(skb, ETH_HLEN);
			skb_copy_to_linear_data(skb, eth_hdr, ETH_HLEN);
		}
	}
	return rc;
}

static int qeth_l3_xmit(struct qeth_card *card, struct sk_buff *skb,
			struct qeth_qdio_out_q *queue, int ipv, int cast_type)
{
	int elements, len, rc;
	__be16 *tag;
	struct qeth_hdr *hdr = NULL;
	int hdr_elements = 0;
	struct sk_buff *new_skb = NULL;
	int tx_bytes = skb->len;
	unsigned int hd_len;
	bool use_tso, is_sg;

	/* Ignore segment size from skb_is_gso(), 1 page is always used. */
	use_tso = skb_is_gso(skb) &&
		  (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4);

	/* create a clone with writeable headroom */
	new_skb = skb_realloc_headroom(skb, sizeof(struct qeth_hdr_tso) +
					    VLAN_HLEN);
	if (!new_skb)
		return -ENOMEM;

	if (ipv == 4) {
		skb_pull(new_skb, ETH_HLEN);
	} else if (skb_vlan_tag_present(new_skb)) {
		skb_push(new_skb, VLAN_HLEN);
		skb_copy_to_linear_data(new_skb, new_skb->data + 4, 4);
		skb_copy_to_linear_data_offset(new_skb, 4,
					       new_skb->data + 8, 4);
		skb_copy_to_linear_data_offset(new_skb, 8,
					       new_skb->data + 12, 4);
		tag = (__be16 *)(new_skb->data + 12);
		*tag = cpu_to_be16(ETH_P_8021Q);
		*(tag + 1) = cpu_to_be16(skb_vlan_tag_get(new_skb));
	}

	/* fix hardware limitation: as long as we do not have sbal
	 * chaining we can not send long frag lists
	 */
	if ((use_tso && !qeth_l3_get_elements_no_tso(card, new_skb, 1)) ||
	    (!use_tso && !qeth_get_elements_no(card, new_skb, 0, 0))) {
		rc = skb_linearize(new_skb);

		if (card->options.performance_stats) {
			if (rc)
				card->perf_stats.tx_linfail++;
			else
				card->perf_stats.tx_lin++;
		}
		if (rc)
			goto out;
	}

	if (use_tso) {
		hdr = skb_push(new_skb, sizeof(struct qeth_hdr_tso));
		memset(hdr, 0, sizeof(struct qeth_hdr_tso));
		qeth_l3_fill_header(card, hdr, new_skb, ipv, cast_type,
				    new_skb->len - sizeof(struct qeth_hdr_tso));
		qeth_tso_fill_header(card, hdr, new_skb);
		hdr_elements++;
	} else {
		hdr = skb_push(new_skb, sizeof(struct qeth_hdr));
		qeth_l3_fill_header(card, hdr, new_skb, ipv, cast_type,
				    new_skb->len - sizeof(struct qeth_hdr));
	}

	elements = use_tso ?
		   qeth_l3_get_elements_no_tso(card, new_skb, hdr_elements) :
		   qeth_get_elements_no(card, new_skb, hdr_elements, 0);
	if (!elements) {
		rc = -E2BIG;
		goto out;
	}
	elements += hdr_elements;

	if (use_tso) {
		hd_len = sizeof(struct qeth_hdr_tso) +
			 ip_hdrlen(new_skb) + tcp_hdrlen(new_skb);
		len = hd_len;
	} else {
		hd_len = 0;
		len = sizeof(struct qeth_hdr_layer3);
	}

	if (qeth_hdr_chk_and_bounce(new_skb, &hdr, len)) {
		rc = -EINVAL;
		goto out;
	}

	is_sg = skb_is_nonlinear(new_skb);
	rc = qeth_do_send_packet(card, queue, new_skb, hdr, hd_len, hd_len,
				 elements);
out:
	if (!rc) {
		if (new_skb != skb)
			dev_kfree_skb_any(skb);
		if (card->options.performance_stats) {
			card->perf_stats.buf_elements_sent += elements;
			if (is_sg)
				card->perf_stats.sg_skbs_sent++;
			if (use_tso) {
				card->perf_stats.large_send_bytes += tx_bytes;
				card->perf_stats.large_send_cnt++;
			}
		}
	} else {
		if (new_skb != skb)
			dev_kfree_skb_any(new_skb);
	}
	return rc;
}

static netdev_tx_t qeth_l3_hard_start_xmit(struct sk_buff *skb,
					   struct net_device *dev)
{
	int cast_type = qeth_l3_get_cast_type(skb);
	struct qeth_card *card = dev->ml_priv;
	int ipv = qeth_get_ip_version(skb);
	struct qeth_qdio_out_q *queue;
	int tx_bytes = skb->len;
	int rc;

	if (IS_IQD(card)) {
		if (card->options.sniffer)
			goto tx_drop;
		if ((card->options.cq != QETH_CQ_ENABLED && !ipv) ||
		    (card->options.cq == QETH_CQ_ENABLED &&
		     skb->protocol != htons(ETH_P_AF_IUCV)))
			goto tx_drop;
	}

	if (card->state != CARD_STATE_UP || !card->lan_online) {
		card->stats.tx_carrier_errors++;
		goto tx_drop;
	}

	if (cast_type == RTN_BROADCAST && !card->info.broadcast_capable)
		goto tx_drop;

	queue = qeth_get_tx_queue(card, skb, ipv, cast_type);

	if (card->options.performance_stats) {
		card->perf_stats.outbound_cnt++;
		card->perf_stats.outbound_start_time = qeth_get_micros();
	}
	netif_stop_queue(dev);

	if (IS_IQD(card) || (!skb_is_gso(skb) && ipv == 4))
		rc = qeth_l3_xmit_offload(card, skb, queue, ipv, cast_type);
	else
		rc = qeth_l3_xmit(card, skb, queue, ipv, cast_type);

	if (!rc) {
		card->stats.tx_packets++;
		card->stats.tx_bytes += tx_bytes;
		if (card->options.performance_stats)
			card->perf_stats.outbound_time += qeth_get_micros() -
				card->perf_stats.outbound_start_time;
		netif_wake_queue(dev);
		return NETDEV_TX_OK;
	} else if (rc == -EBUSY) {
		return NETDEV_TX_BUSY;
	} /* else fall through */

tx_drop:
	card->stats.tx_dropped++;
	card->stats.tx_errors++;
	dev_kfree_skb_any(skb);
	netif_wake_queue(dev);
	return NETDEV_TX_OK;
}

static int __qeth_l3_open(struct net_device *dev)
{
	struct qeth_card *card = dev->ml_priv;
	int rc = 0;

	QETH_CARD_TEXT(card, 4, "qethopen");
	if (card->state == CARD_STATE_UP)
		return rc;
	if (card->state != CARD_STATE_SOFTSETUP)
		return -ENODEV;
	card->data.state = CH_STATE_UP;
	card->state = CARD_STATE_UP;
	netif_start_queue(dev);

	if (qdio_stop_irq(card->data.ccwdev, 0) >= 0) {
		napi_enable(&card->napi);
		napi_schedule(&card->napi);
	} else
		rc = -EIO;
	return rc;
}

static int qeth_l3_open(struct net_device *dev)
{
	struct qeth_card *card = dev->ml_priv;

	QETH_CARD_TEXT(card, 5, "qethope_");
	if (qeth_wait_for_threads(card, QETH_RECOVER_THREAD)) {
		QETH_CARD_TEXT(card, 3, "openREC");
		return -ERESTARTSYS;
	}
	return __qeth_l3_open(dev);
}

static int qeth_l3_stop(struct net_device *dev)
{
	struct qeth_card *card = dev->ml_priv;

	QETH_CARD_TEXT(card, 4, "qethstop");
	netif_tx_disable(dev);
	if (card->state == CARD_STATE_UP) {
		card->state = CARD_STATE_SOFTSETUP;
		napi_disable(&card->napi);
	}
	return 0;
}

static const struct ethtool_ops qeth_l3_ethtool_ops = {
	.get_link = ethtool_op_get_link,
	.get_strings = qeth_core_get_strings,
	.get_ethtool_stats = qeth_core_get_ethtool_stats,
	.get_sset_count = qeth_core_get_sset_count,
	.get_drvinfo = qeth_core_get_drvinfo,
	.get_link_ksettings = qeth_core_ethtool_get_link_ksettings,
};

/*
 * we need NOARP for IPv4 but we want neighbor solicitation for IPv6. Setting
 * NOARP on the netdevice is no option because it also turns off neighbor
 * solicitation. For IPv4 we install a neighbor_setup function. We don't want
 * arp resolution but we want the hard header (packet socket will work
 * e.g. tcpdump)
 */
static int qeth_l3_neigh_setup_noarp(struct neighbour *n)
{
	n->nud_state = NUD_NOARP;
	memcpy(n->ha, "FAKELL", 6);
	n->output = n->ops->connected_output;
	return 0;
}

static int
qeth_l3_neigh_setup(struct net_device *dev, struct neigh_parms *np)
{
	if (np->tbl->family == AF_INET)
		np->neigh_setup = qeth_l3_neigh_setup_noarp;

	return 0;
}

static const struct net_device_ops qeth_l3_netdev_ops = {
	.ndo_open		= qeth_l3_open,
	.ndo_stop		= qeth_l3_stop,
	.ndo_get_stats		= qeth_get_stats,
	.ndo_start_xmit		= qeth_l3_hard_start_xmit,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= qeth_l3_set_rx_mode,
	.ndo_do_ioctl		= qeth_do_ioctl,
	.ndo_fix_features	= qeth_fix_features,
	.ndo_set_features	= qeth_set_features,
	.ndo_vlan_rx_add_vid	= qeth_l3_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid   = qeth_l3_vlan_rx_kill_vid,
	.ndo_tx_timeout		= qeth_tx_timeout,
};

static const struct net_device_ops qeth_l3_osa_netdev_ops = {
	.ndo_open		= qeth_l3_open,
	.ndo_stop		= qeth_l3_stop,
	.ndo_get_stats		= qeth_get_stats,
	.ndo_start_xmit		= qeth_l3_hard_start_xmit,
	.ndo_features_check	= qeth_features_check,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= qeth_l3_set_rx_mode,
	.ndo_do_ioctl		= qeth_do_ioctl,
	.ndo_fix_features	= qeth_fix_features,
	.ndo_set_features	= qeth_set_features,
	.ndo_vlan_rx_add_vid	= qeth_l3_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid   = qeth_l3_vlan_rx_kill_vid,
	.ndo_tx_timeout		= qeth_tx_timeout,
	.ndo_neigh_setup	= qeth_l3_neigh_setup,
};

static int qeth_l3_setup_netdev(struct qeth_card *card)
{
	int rc;

	if (card->dev->netdev_ops)
		return 0;

	if (card->info.type == QETH_CARD_TYPE_OSD ||
	    card->info.type == QETH_CARD_TYPE_OSX) {
		if ((card->info.link_type == QETH_LINK_TYPE_LANE_TR) ||
		    (card->info.link_type == QETH_LINK_TYPE_HSTR)) {
			pr_info("qeth_l3: ignoring TR device\n");
			return -ENODEV;
		}

		card->dev->netdev_ops = &qeth_l3_osa_netdev_ops;

		/*IPv6 address autoconfiguration stuff*/
		qeth_l3_get_unique_id(card);
		if (!(card->info.unique_id & UNIQUE_ID_NOT_BY_CARD))
			card->dev->dev_id = card->info.unique_id & 0xffff;

		if (!card->info.guestlan) {
			card->dev->features |= NETIF_F_SG;
			card->dev->hw_features |= NETIF_F_TSO |
				NETIF_F_RXCSUM | NETIF_F_IP_CSUM;
			card->dev->vlan_features |= NETIF_F_TSO |
				NETIF_F_RXCSUM | NETIF_F_IP_CSUM;
		}

		if (qeth_is_supported6(card, IPA_OUTBOUND_CHECKSUM_V6)) {
			card->dev->hw_features |= NETIF_F_IPV6_CSUM;
			card->dev->vlan_features |= NETIF_F_IPV6_CSUM;
		}
	} else if (card->info.type == QETH_CARD_TYPE_IQD) {
		card->dev->flags |= IFF_NOARP;
		card->dev->netdev_ops = &qeth_l3_netdev_ops;

		rc = qeth_l3_iqd_read_initial_mac(card);
		if (rc)
			goto out;

		if (card->options.hsuid[0])
			memcpy(card->dev->perm_addr, card->options.hsuid, 9);
	} else
		return -ENODEV;

	card->dev->ethtool_ops = &qeth_l3_ethtool_ops;
	card->dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	card->dev->needed_headroom = sizeof(struct qeth_hdr) - ETH_HLEN;
	card->dev->features |=	NETIF_F_HW_VLAN_CTAG_TX |
				NETIF_F_HW_VLAN_CTAG_RX |
				NETIF_F_HW_VLAN_CTAG_FILTER;
	card->dev->hw_features |= NETIF_F_SG;
	card->dev->vlan_features |= NETIF_F_SG;

	netif_keep_dst(card->dev);
	if (card->dev->hw_features & NETIF_F_TSO)
		netif_set_gso_max_size(card->dev,
				       PAGE_SIZE * (QETH_MAX_BUFFER_ELEMENTS(card) - 1));

	netif_napi_add(card->dev, &card->napi, qeth_poll, QETH_NAPI_WEIGHT);
	rc = register_netdev(card->dev);
out:
	if (rc)
		card->dev->netdev_ops = NULL;
	return rc;
}

static const struct device_type qeth_l3_devtype = {
	.name = "qeth_layer3",
	.groups = qeth_l3_attr_groups,
};

static int qeth_l3_probe_device(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	int rc;

	if (gdev->dev.type == &qeth_generic_devtype) {
		rc = qeth_l3_create_device_attributes(&gdev->dev);
		if (rc)
			return rc;
	}
	hash_init(card->ip_htable);
	hash_init(card->ip_mc_htable);
	card->options.layer2 = 0;
	card->info.hwtrap = 0;
	return 0;
}

static void qeth_l3_remove_device(struct ccwgroup_device *cgdev)
{
	struct qeth_card *card = dev_get_drvdata(&cgdev->dev);

	if (cgdev->dev.type == &qeth_generic_devtype)
		qeth_l3_remove_device_attributes(&cgdev->dev);

	qeth_set_allowed_threads(card, 0, 1);
	wait_event(card->wait_q, qeth_threads_running(card, 0xffffffff) == 0);

	if (cgdev->state == CCWGROUP_ONLINE)
		qeth_l3_set_offline(cgdev);

	unregister_netdev(card->dev);
	qeth_l3_clear_ip_htable(card, 0);
	qeth_l3_clear_ipato_list(card);
}

static int __qeth_l3_set_online(struct ccwgroup_device *gdev, int recovery_mode)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	int rc = 0;
	enum qeth_card_states recover_flag;

	mutex_lock(&card->discipline_mutex);
	mutex_lock(&card->conf_mutex);
	QETH_DBF_TEXT(SETUP, 2, "setonlin");
	QETH_DBF_HEX(SETUP, 2, &card, sizeof(void *));

	recover_flag = card->state;
	rc = qeth_core_hardsetup_card(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "2err%04x", rc);
		rc = -ENODEV;
		goto out_remove;
	}

	rc = qeth_l3_setup_netdev(card);
	if (rc)
		goto out_remove;

	if (qeth_is_diagass_supported(card, QETH_DIAGS_CMD_TRAP)) {
		if (card->info.hwtrap &&
		    qeth_hw_trap(card, QETH_DIAGS_TRAP_ARM))
			card->info.hwtrap = 0;
	} else
		card->info.hwtrap = 0;

	card->state = CARD_STATE_HARDSETUP;
	qeth_print_status_message(card);

	/* softsetup */
	QETH_DBF_TEXT(SETUP, 2, "softsetp");

	rc = qeth_l3_setadapter_parms(card);
	if (rc)
		QETH_DBF_TEXT_(SETUP, 2, "2err%04x", rc);
	if (!card->options.sniffer) {
		rc = qeth_l3_start_ipassists(card);
		if (rc) {
			QETH_DBF_TEXT_(SETUP, 2, "3err%d", rc);
			goto out_remove;
		}
		rc = qeth_l3_setrouting_v4(card);
		if (rc)
			QETH_DBF_TEXT_(SETUP, 2, "4err%04x", rc);
		rc = qeth_l3_setrouting_v6(card);
		if (rc)
			QETH_DBF_TEXT_(SETUP, 2, "5err%04x", rc);
	}
	netif_tx_disable(card->dev);

	rc = qeth_init_qdio_queues(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "6err%d", rc);
		rc = -ENODEV;
		goto out_remove;
	}
	card->state = CARD_STATE_SOFTSETUP;

	qeth_set_allowed_threads(card, 0xffffffff, 0);
	qeth_l3_recover_ip(card);
	if (card->lan_online)
		netif_carrier_on(card->dev);
	else
		netif_carrier_off(card->dev);

	qeth_enable_hw_features(card->dev);
	if (recover_flag == CARD_STATE_RECOVER) {
		rtnl_lock();
		if (recovery_mode) {
			__qeth_l3_open(card->dev);
			qeth_l3_set_rx_mode(card->dev);
		} else {
			dev_open(card->dev);
		}
		rtnl_unlock();
	}
	qeth_trace_features(card);
	/* let user_space know that device is online */
	kobject_uevent(&gdev->dev.kobj, KOBJ_CHANGE);
	mutex_unlock(&card->conf_mutex);
	mutex_unlock(&card->discipline_mutex);
	return 0;
out_remove:
	qeth_l3_stop_card(card, 0);
	ccw_device_set_offline(CARD_DDEV(card));
	ccw_device_set_offline(CARD_WDEV(card));
	ccw_device_set_offline(CARD_RDEV(card));
	qdio_free(CARD_DDEV(card));
	if (recover_flag == CARD_STATE_RECOVER)
		card->state = CARD_STATE_RECOVER;
	else
		card->state = CARD_STATE_DOWN;
	mutex_unlock(&card->conf_mutex);
	mutex_unlock(&card->discipline_mutex);
	return rc;
}

static int qeth_l3_set_online(struct ccwgroup_device *gdev)
{
	return __qeth_l3_set_online(gdev, 0);
}

static int __qeth_l3_set_offline(struct ccwgroup_device *cgdev,
			int recovery_mode)
{
	struct qeth_card *card = dev_get_drvdata(&cgdev->dev);
	int rc = 0, rc2 = 0, rc3 = 0;
	enum qeth_card_states recover_flag;

	mutex_lock(&card->discipline_mutex);
	mutex_lock(&card->conf_mutex);
	QETH_DBF_TEXT(SETUP, 3, "setoffl");
	QETH_DBF_HEX(SETUP, 3, &card, sizeof(void *));

	netif_carrier_off(card->dev);
	recover_flag = card->state;
	if ((!recovery_mode && card->info.hwtrap) || card->info.hwtrap == 2) {
		qeth_hw_trap(card, QETH_DIAGS_TRAP_DISARM);
		card->info.hwtrap = 1;
	}
	qeth_l3_stop_card(card, recovery_mode);
	if ((card->options.cq == QETH_CQ_ENABLED) && card->dev) {
		rtnl_lock();
		call_netdevice_notifiers(NETDEV_REBOOT, card->dev);
		rtnl_unlock();
	}
	rc  = ccw_device_set_offline(CARD_DDEV(card));
	rc2 = ccw_device_set_offline(CARD_WDEV(card));
	rc3 = ccw_device_set_offline(CARD_RDEV(card));
	if (!rc)
		rc = (rc2) ? rc2 : rc3;
	if (rc)
		QETH_DBF_TEXT_(SETUP, 2, "1err%d", rc);
	qdio_free(CARD_DDEV(card));
	if (recover_flag == CARD_STATE_UP)
		card->state = CARD_STATE_RECOVER;
	/* let user_space know that device is offline */
	kobject_uevent(&cgdev->dev.kobj, KOBJ_CHANGE);
	mutex_unlock(&card->conf_mutex);
	mutex_unlock(&card->discipline_mutex);
	return 0;
}

static int qeth_l3_set_offline(struct ccwgroup_device *cgdev)
{
	return __qeth_l3_set_offline(cgdev, 0);
}

static int qeth_l3_recover(void *ptr)
{
	struct qeth_card *card;
	int rc = 0;

	card = (struct qeth_card *) ptr;
	QETH_CARD_TEXT(card, 2, "recover1");
	QETH_CARD_HEX(card, 2, &card, sizeof(void *));
	if (!qeth_do_run_thread(card, QETH_RECOVER_THREAD))
		return 0;
	QETH_CARD_TEXT(card, 2, "recover2");
	dev_warn(&card->gdev->dev,
		"A recovery process has been started for the device\n");
	qeth_set_recovery_task(card);
	__qeth_l3_set_offline(card->gdev, 1);
	rc = __qeth_l3_set_online(card->gdev, 1);
	if (!rc)
		dev_info(&card->gdev->dev,
			"Device successfully recovered!\n");
	else {
		qeth_close_dev(card);
		dev_warn(&card->gdev->dev, "The qeth device driver "
				"failed to recover an error on the device\n");
	}
	qeth_clear_recovery_task(card);
	qeth_clear_thread_start_bit(card, QETH_RECOVER_THREAD);
	qeth_clear_thread_running_bit(card, QETH_RECOVER_THREAD);
	return 0;
}

static int qeth_l3_pm_suspend(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);

	netif_device_detach(card->dev);
	qeth_set_allowed_threads(card, 0, 1);
	wait_event(card->wait_q, qeth_threads_running(card, 0xffffffff) == 0);
	if (gdev->state == CCWGROUP_OFFLINE)
		return 0;
	if (card->state == CARD_STATE_UP) {
		if (card->info.hwtrap)
			qeth_hw_trap(card, QETH_DIAGS_TRAP_DISARM);
		__qeth_l3_set_offline(card->gdev, 1);
	} else
		__qeth_l3_set_offline(card->gdev, 0);
	return 0;
}

static int qeth_l3_pm_resume(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	int rc = 0;

	if (gdev->state == CCWGROUP_OFFLINE)
		goto out;

	if (card->state == CARD_STATE_RECOVER) {
		rc = __qeth_l3_set_online(card->gdev, 1);
		if (rc) {
			rtnl_lock();
			dev_close(card->dev);
			rtnl_unlock();
		}
	} else
		rc = __qeth_l3_set_online(card->gdev, 0);
out:
	qeth_set_allowed_threads(card, 0xffffffff, 0);
	netif_device_attach(card->dev);
	if (rc)
		dev_warn(&card->gdev->dev, "The qeth device driver "
			"failed to recover an error on the device\n");
	return rc;
}

/* Returns zero if the command is successfully "consumed" */
static int qeth_l3_control_event(struct qeth_card *card,
					struct qeth_ipa_cmd *cmd)
{
	return 1;
}

struct qeth_discipline qeth_l3_discipline = {
	.devtype = &qeth_l3_devtype,
	.process_rx_buffer = qeth_l3_process_inbound_buffer,
	.recover = qeth_l3_recover,
	.setup = qeth_l3_probe_device,
	.remove = qeth_l3_remove_device,
	.set_online = qeth_l3_set_online,
	.set_offline = qeth_l3_set_offline,
	.freeze = qeth_l3_pm_suspend,
	.thaw = qeth_l3_pm_resume,
	.restore = qeth_l3_pm_resume,
	.do_ioctl = qeth_l3_do_ioctl,
	.control_event_handler = qeth_l3_control_event,
};
EXPORT_SYMBOL_GPL(qeth_l3_discipline);

static int qeth_l3_handle_ip_event(struct qeth_card *card,
				   struct qeth_ipaddr *addr,
				   unsigned long event)
{
	switch (event) {
	case NETDEV_UP:
		spin_lock_bh(&card->ip_lock);
		qeth_l3_add_ip(card, addr);
		spin_unlock_bh(&card->ip_lock);
		return NOTIFY_OK;
	case NETDEV_DOWN:
		spin_lock_bh(&card->ip_lock);
		qeth_l3_delete_ip(card, addr);
		spin_unlock_bh(&card->ip_lock);
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

static struct qeth_card *qeth_l3_get_card_from_dev(struct net_device *dev)
{
	if (is_vlan_dev(dev))
		dev = vlan_dev_real_dev(dev);
	if (dev->netdev_ops == &qeth_l3_osa_netdev_ops ||
	    dev->netdev_ops == &qeth_l3_netdev_ops)
		return (struct qeth_card *) dev->ml_priv;
	return NULL;
}

static int qeth_l3_ip_event(struct notifier_block *this,
			    unsigned long event, void *ptr)
{

	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct net_device *dev = ifa->ifa_dev->dev;
	struct qeth_ipaddr addr;
	struct qeth_card *card;

	if (dev_net(dev) != &init_net)
		return NOTIFY_DONE;

	card = qeth_l3_get_card_from_dev(dev);
	if (!card)
		return NOTIFY_DONE;
	QETH_CARD_TEXT(card, 3, "ipevent");

	qeth_l3_init_ipaddr(&addr, QETH_IP_TYPE_NORMAL, QETH_PROT_IPV4);
	addr.u.a4.addr = be32_to_cpu(ifa->ifa_address);
	addr.u.a4.mask = be32_to_cpu(ifa->ifa_mask);

	return qeth_l3_handle_ip_event(card, &addr, event);
}

static struct notifier_block qeth_l3_ip_notifier = {
	qeth_l3_ip_event,
	NULL,
};

static int qeth_l3_ip6_event(struct notifier_block *this,
			     unsigned long event, void *ptr)
{
	struct inet6_ifaddr *ifa = (struct inet6_ifaddr *)ptr;
	struct net_device *dev = ifa->idev->dev;
	struct qeth_ipaddr addr;
	struct qeth_card *card;

	card = qeth_l3_get_card_from_dev(dev);
	if (!card)
		return NOTIFY_DONE;
	QETH_CARD_TEXT(card, 3, "ip6event");
	if (!qeth_is_supported(card, IPA_IPV6))
		return NOTIFY_DONE;

	qeth_l3_init_ipaddr(&addr, QETH_IP_TYPE_NORMAL, QETH_PROT_IPV6);
	addr.u.a6.addr = ifa->addr;
	addr.u.a6.pfxlen = ifa->prefix_len;

	return qeth_l3_handle_ip_event(card, &addr, event);
}

static struct notifier_block qeth_l3_ip6_notifier = {
	qeth_l3_ip6_event,
	NULL,
};

static int qeth_l3_register_notifiers(void)
{
	int rc;

	QETH_DBF_TEXT(SETUP, 5, "regnotif");
	rc = register_inetaddr_notifier(&qeth_l3_ip_notifier);
	if (rc)
		return rc;
	rc = register_inet6addr_notifier(&qeth_l3_ip6_notifier);
	if (rc) {
		unregister_inetaddr_notifier(&qeth_l3_ip_notifier);
		return rc;
	}
	return 0;
}

static void qeth_l3_unregister_notifiers(void)
{
	QETH_DBF_TEXT(SETUP, 5, "unregnot");
	WARN_ON(unregister_inetaddr_notifier(&qeth_l3_ip_notifier));
	WARN_ON(unregister_inet6addr_notifier(&qeth_l3_ip6_notifier));
}

static int __init qeth_l3_init(void)
{
	pr_info("register layer 3 discipline\n");
	return qeth_l3_register_notifiers();
}

static void __exit qeth_l3_exit(void)
{
	qeth_l3_unregister_notifiers();
	pr_info("unregister layer 3 discipline\n");
}

module_init(qeth_l3_init);
module_exit(qeth_l3_exit);
MODULE_AUTHOR("Frank Blaschka <frank.blaschka@de.ibm.com>");
MODULE_DESCRIPTION("qeth layer 3 discipline");
MODULE_LICENSE("GPL");
