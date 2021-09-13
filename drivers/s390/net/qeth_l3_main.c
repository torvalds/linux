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
#include <net/iucv/af_iucv.h>
#include <linux/hashtable.h>

#include "qeth_l3.h"

static int qeth_l3_register_addr_entry(struct qeth_card *,
		struct qeth_ipaddr *);
static int qeth_l3_deregister_addr_entry(struct qeth_card *,
		struct qeth_ipaddr *);

int qeth_l3_ipaddr_to_string(enum qeth_prot_versions proto, const u8 *addr,
			     char *buf)
{
	if (proto == QETH_PROT_IPV4)
		return sprintf(buf, "%pI4", addr);
	else
		return sprintf(buf, "%pI6", addr);
}

static struct qeth_ipaddr *qeth_l3_find_addr_by_ip(struct qeth_card *card,
						   struct qeth_ipaddr *query)
{
	u32 key = qeth_l3_ipaddr_hash(query);
	struct qeth_ipaddr *addr;

	if (query->is_multicast) {
		hash_for_each_possible(card->rx_mode_addrs, addr, hnode, key)
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
		return false;
	if (addr->type != QETH_IP_TYPE_NORMAL)
		return false;

	qeth_l3_convert_addr_to_bits((u8 *) &addr->u, addr_bits,
				     (addr->proto == QETH_PROT_IPV4) ? 4 : 16);
	list_for_each_entry(ipatoe, &card->ipato.entries, entry) {
		if (addr->proto != ipatoe->proto)
			continue;
		qeth_l3_convert_addr_to_bits(ipatoe->addr, ipatoe_bits,
					  (ipatoe->proto == QETH_PROT_IPV4) ?
					  4 : 16);
		rc = !memcmp(addr_bits, ipatoe_bits, ipatoe->mask_bits);
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
		addr = kmemdup(tmp_addr, sizeof(*tmp_addr), GFP_KERNEL);
		if (!addr)
			return -ENOMEM;

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

		rc = qeth_l3_register_addr_entry(card, addr);

		if (!rc || rc == -EADDRINUSE || rc == -ENETDOWN) {
			addr->disp_flag = QETH_DISP_ADDR_DO_NOTHING;
		} else {
			hash_del(&addr->hnode);
			kfree(addr);
		}
	}
	return rc;
}

static int qeth_l3_modify_ip(struct qeth_card *card, struct qeth_ipaddr *addr,
			     bool add)
{
	int rc;

	mutex_lock(&card->ip_lock);
	rc = add ? qeth_l3_add_ip(card, addr) : qeth_l3_delete_ip(card, addr);
	mutex_unlock(&card->ip_lock);

	return rc;
}

static void qeth_l3_drain_rx_mode_cache(struct qeth_card *card)
{
	struct qeth_ipaddr *addr;
	struct hlist_node *tmp;
	int i;

	hash_for_each_safe(card->rx_mode_addrs, i, tmp, addr, hnode) {
		hash_del(&addr->hnode);
		kfree(addr);
	}
}

static void qeth_l3_clear_ip_htable(struct qeth_card *card, int recover)
{
	struct qeth_ipaddr *addr;
	struct hlist_node *tmp;
	int i;

	QETH_CARD_TEXT(card, 4, "clearip");

	mutex_lock(&card->ip_lock);

	hash_for_each_safe(card->ip_htable, i, tmp, addr, hnode) {
		if (!recover) {
			hash_del(&addr->hnode);
			kfree(addr);
			continue;
		}
		addr->disp_flag = QETH_DISP_ADDR_ADD;
	}

	mutex_unlock(&card->ip_lock);
}

static void qeth_l3_recover_ip(struct qeth_card *card)
{
	struct qeth_ipaddr *addr;
	struct hlist_node *tmp;
	int i;
	int rc;

	QETH_CARD_TEXT(card, 4, "recovrip");

	mutex_lock(&card->ip_lock);

	hash_for_each_safe(card->ip_htable, i, tmp, addr, hnode) {
		if (addr->disp_flag == QETH_DISP_ADDR_ADD) {
			rc = qeth_l3_register_addr_entry(card, addr);

			if (!rc) {
				addr->disp_flag = QETH_DISP_ADDR_DO_NOTHING;
			} else {
				hash_del(&addr->hnode);
				kfree(addr);
			}
		}
	}

	mutex_unlock(&card->ip_lock);
}

static int qeth_l3_setdelip_cb(struct qeth_card *card, struct qeth_reply *reply,
			       unsigned long data)
{
	struct qeth_ipa_cmd *cmd = (struct qeth_ipa_cmd *) data;

	switch (cmd->hdr.return_code) {
	case IPA_RC_SUCCESS:
		return 0;
	case IPA_RC_DUPLICATE_IP_ADDRESS:
		return -EADDRINUSE;
	case IPA_RC_MC_ADDR_NOT_FOUND:
		return -ENOENT;
	case IPA_RC_LAN_OFFLINE:
		return -ENETDOWN;
	default:
		return -EIO;
	}
}

static int qeth_l3_send_setdelmc(struct qeth_card *card,
				 struct qeth_ipaddr *addr,
				 enum qeth_ipa_cmds ipacmd)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	QETH_CARD_TEXT(card, 4, "setdelmc");

	iob = qeth_ipa_alloc_cmd(card, ipacmd, addr->proto,
				 IPA_DATA_SIZEOF(setdelipm));
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);
	if (addr->proto == QETH_PROT_IPV6) {
		cmd->data.setdelipm.ip = addr->u.a6.addr;
		ipv6_eth_mc_map(&addr->u.a6.addr, cmd->data.setdelipm.mac);
	} else {
		cmd->data.setdelipm.ip.s6_addr32[3] = addr->u.a4.addr;
		ip_eth_mc_map(addr->u.a4.addr, cmd->data.setdelipm.mac);
	}

	return qeth_send_ipa_cmd(card, iob, qeth_l3_setdelip_cb, NULL);
}

static void qeth_l3_set_ipv6_prefix(struct in6_addr *prefix, unsigned int len)
{
	unsigned int i = 0;

	while (len && i < 4) {
		int mask_len = min_t(int, len, 32);

		prefix->s6_addr32[i] = inet_make_mask(mask_len);
		len -= mask_len;
		i++;
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
	u32 flags;

	QETH_CARD_TEXT(card, 4, "setdelip");

	iob = qeth_ipa_alloc_cmd(card, ipacmd, addr->proto,
				 IPA_DATA_SIZEOF(setdelip6));
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);

	flags = qeth_l3_get_setdelip_flags(addr, ipacmd == IPA_CMD_SETIP);
	QETH_CARD_TEXT_(card, 4, "flags%02X", flags);

	if (addr->proto == QETH_PROT_IPV6) {
		cmd->data.setdelip6.addr = addr->u.a6.addr;
		qeth_l3_set_ipv6_prefix(&cmd->data.setdelip6.prefix,
					addr->u.a6.pfxlen);
		cmd->data.setdelip6.flags = flags;
	} else {
		cmd->data.setdelip4.addr = addr->u.a4.addr;
		cmd->data.setdelip4.mask = addr->u.a4.mask;
		cmd->data.setdelip4.flags = flags;
	}

	return qeth_send_ipa_cmd(card, iob, qeth_l3_setdelip_cb, NULL);
}

static int qeth_l3_send_setrouting(struct qeth_card *card,
	enum qeth_routing_types type, enum qeth_prot_versions prot)
{
	int rc;
	struct qeth_ipa_cmd *cmd;
	struct qeth_cmd_buffer *iob;

	QETH_CARD_TEXT(card, 4, "setroutg");
	iob = qeth_ipa_alloc_cmd(card, IPA_CMD_SETRTG, prot,
				 IPA_DATA_SIZEOF(setrtg));
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
	if (IS_IQD(card)) {
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
			goto out_inval;
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
		QETH_DBF_MESSAGE(2, "Error (%#06x) while setting routing type on device %x. Type set to 'no router'.\n",
				 rc, CARD_DEVID(card));
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
		QETH_DBF_MESSAGE(2, "Error (%#06x) while setting routing type on device %x. Type set to 'no router'.\n",
				 rc, CARD_DEVID(card));
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

	mutex_lock(&card->ip_lock);

	list_for_each_entry_safe(ipatoe, tmp, &card->ipato.entries, entry) {
		list_del(&ipatoe->entry);
		kfree(ipatoe);
	}

	qeth_l3_update_ipato(card);
	mutex_unlock(&card->ip_lock);
}

int qeth_l3_add_ipato_entry(struct qeth_card *card,
				struct qeth_ipato_entry *new)
{
	struct qeth_ipato_entry *ipatoe;
	int rc = 0;

	QETH_CARD_TEXT(card, 2, "addipato");

	mutex_lock(&card->ip_lock);

	list_for_each_entry(ipatoe, &card->ipato.entries, entry) {
		if (ipatoe->proto != new->proto)
			continue;
		if (!memcmp(ipatoe->addr, new->addr,
			    (ipatoe->proto == QETH_PROT_IPV4) ? 4 : 16) &&
		    (ipatoe->mask_bits == new->mask_bits)) {
			rc = -EEXIST;
			break;
		}
	}

	if (!rc) {
		list_add_tail(&new->entry, &card->ipato.entries);
		qeth_l3_update_ipato(card);
	}

	mutex_unlock(&card->ip_lock);

	return rc;
}

int qeth_l3_del_ipato_entry(struct qeth_card *card,
			    enum qeth_prot_versions proto, u8 *addr,
			    unsigned int mask_bits)
{
	struct qeth_ipato_entry *ipatoe, *tmp;
	int rc = -ENOENT;

	QETH_CARD_TEXT(card, 2, "delipato");

	mutex_lock(&card->ip_lock);

	list_for_each_entry_safe(ipatoe, tmp, &card->ipato.entries, entry) {
		if (ipatoe->proto != proto)
			continue;
		if (!memcmp(ipatoe->addr, addr,
			    (proto == QETH_PROT_IPV4) ? 4 : 16) &&
		    (ipatoe->mask_bits == mask_bits)) {
			list_del(&ipatoe->entry);
			qeth_l3_update_ipato(card);
			kfree(ipatoe);
			rc = 0;
		}
	}

	mutex_unlock(&card->ip_lock);

	return rc;
}

int qeth_l3_modify_rxip_vipa(struct qeth_card *card, bool add, const u8 *ip,
			     enum qeth_ip_types type,
			     enum qeth_prot_versions proto)
{
	struct qeth_ipaddr addr;

	qeth_l3_init_ipaddr(&addr, type, proto);
	if (proto == QETH_PROT_IPV4)
		memcpy(&addr.u.a4.addr, ip, 4);
	else
		memcpy(&addr.u.a6.addr, ip, 16);

	return qeth_l3_modify_ip(card, &addr, add);
}

int qeth_l3_modify_hsuid(struct qeth_card *card, bool add)
{
	struct qeth_ipaddr addr;
	unsigned int i;

	qeth_l3_init_ipaddr(&addr, QETH_IP_TYPE_NORMAL, QETH_PROT_IPV6);
	addr.u.a6.addr.s6_addr[0] = 0xfe;
	addr.u.a6.addr.s6_addr[1] = 0x80;
	for (i = 0; i < 8; i++)
		addr.u.a6.addr.s6_addr[8+i] = card->options.hsuid[i];

	return qeth_l3_modify_ip(card, &addr, add);
}

static int qeth_l3_register_addr_entry(struct qeth_card *card,
				struct qeth_ipaddr *addr)
{
	char buf[50];
	int rc = 0;
	int cnt = 3;

	if (card->options.sniffer)
		return 0;

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

	if (card->options.sniffer)
		return 0;

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

	QETH_CARD_TEXT(card, 2, "setadprm");

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
			 netdev_name(card->dev));
		return 0;
	}
	rc = qeth_send_simple_setassparms(card, IPA_ARP_PROCESSING,
					  IPA_CMD_ASS_START, NULL);
	if (rc) {
		dev_warn(&card->gdev->dev,
			 "Starting ARP processing support for %s failed\n",
			 netdev_name(card->dev));
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
			 netdev_name(card->dev));
		return -EOPNOTSUPP;
	}

	rc = qeth_send_simple_setassparms(card, IPA_SOURCE_MAC,
					  IPA_CMD_ASS_START, NULL);
	if (rc)
		dev_warn(&card->gdev->dev,
			 "Starting source MAC-address support for %s failed\n",
			 netdev_name(card->dev));
	return rc;
}

static int qeth_l3_start_ipa_vlan(struct qeth_card *card)
{
	int rc = 0;

	QETH_CARD_TEXT(card, 3, "strtvlan");

	if (!qeth_is_supported(card, IPA_FULL_VLAN)) {
		dev_info(&card->gdev->dev,
			 "VLAN not supported on %s\n", netdev_name(card->dev));
		return -EOPNOTSUPP;
	}

	rc = qeth_send_simple_setassparms(card, IPA_VLAN_PRIO,
					  IPA_CMD_ASS_START, NULL);
	if (rc) {
		dev_warn(&card->gdev->dev,
			 "Starting VLAN support for %s failed\n",
			 netdev_name(card->dev));
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
			 netdev_name(card->dev));
		return -EOPNOTSUPP;
	}

	rc = qeth_send_simple_setassparms(card, IPA_MULTICASTING,
					  IPA_CMD_ASS_START, NULL);
	if (rc) {
		dev_warn(&card->gdev->dev,
			 "Starting multicast support for %s failed\n",
			 netdev_name(card->dev));
	} else {
		dev_info(&card->gdev->dev, "Multicast enabled\n");
		card->dev->flags |= IFF_MULTICAST;
	}
	return rc;
}

static int qeth_l3_softsetup_ipv6(struct qeth_card *card)
{
	u32 ipv6_data = 3;
	int rc;

	QETH_CARD_TEXT(card, 3, "softipv6");

	if (IS_IQD(card))
		goto out;

	rc = qeth_send_simple_setassparms(card, IPA_IPV6, IPA_CMD_ASS_START,
					  &ipv6_data);
	if (rc) {
		dev_err(&card->gdev->dev,
			"Activating IPv6 support for %s failed\n",
			netdev_name(card->dev));
		return rc;
	}
	rc = qeth_send_simple_setassparms_v6(card, IPA_IPV6, IPA_CMD_ASS_START,
					     NULL);
	if (rc) {
		dev_err(&card->gdev->dev,
			"Activating IPv6 support for %s failed\n",
			 netdev_name(card->dev));
		return rc;
	}
	rc = qeth_send_simple_setassparms_v6(card, IPA_PASSTHRU,
					     IPA_CMD_ASS_START, NULL);
	if (rc) {
		dev_warn(&card->gdev->dev,
			 "Enabling the passthrough mode for %s failed\n",
			 netdev_name(card->dev));
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
			 "IPv6 not supported on %s\n", netdev_name(card->dev));
		return 0;
	}
	return qeth_l3_softsetup_ipv6(card);
}

static int qeth_l3_start_ipa_broadcast(struct qeth_card *card)
{
	u32 filter_data = 1;
	int rc;

	QETH_CARD_TEXT(card, 3, "stbrdcst");
	card->info.broadcast_capable = 0;
	if (!qeth_is_supported(card, IPA_FILTERING)) {
		dev_info(&card->gdev->dev,
			 "Broadcast not supported on %s\n",
			 netdev_name(card->dev));
		rc = -EOPNOTSUPP;
		goto out;
	}
	rc = qeth_send_simple_setassparms(card, IPA_FILTERING,
					  IPA_CMD_ASS_START, NULL);
	if (rc) {
		dev_warn(&card->gdev->dev,
			 "Enabling broadcast filtering for %s failed\n",
			 netdev_name(card->dev));
		goto out;
	}

	rc = qeth_send_simple_setassparms(card, IPA_FILTERING,
					  IPA_CMD_ASS_CONFIGURE, &filter_data);
	if (rc) {
		dev_warn(&card->gdev->dev,
			 "Setting up broadcast filtering for %s failed\n",
			 netdev_name(card->dev));
		goto out;
	}
	card->info.broadcast_capable = QETH_BROADCAST_WITH_ECHO;
	dev_info(&card->gdev->dev, "Broadcast enabled\n");
	rc = qeth_send_simple_setassparms(card, IPA_FILTERING,
					  IPA_CMD_ASS_ENABLE, &filter_data);
	if (rc) {
		dev_warn(&card->gdev->dev,
			 "Setting up broadcast echo filtering for %s failed\n",
			 netdev_name(card->dev));
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

static void qeth_l3_start_ipassists(struct qeth_card *card)
{
	QETH_CARD_TEXT(card, 3, "strtipas");

	qeth_l3_start_ipa_arp_processing(card);	/* go on*/
	qeth_l3_start_ipa_source_mac(card);	/* go on*/
	qeth_l3_start_ipa_vlan(card);		/* go on*/
	qeth_l3_start_ipa_multicast(card);		/* go on*/
	qeth_l3_start_ipa_ipv6(card);		/* go on*/
	qeth_l3_start_ipa_broadcast(card);		/* go on*/
}

static int qeth_l3_iqd_read_initial_mac_cb(struct qeth_card *card,
		struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd = (struct qeth_ipa_cmd *) data;

	if (cmd->hdr.return_code)
		return -EIO;
	if (!is_valid_ether_addr(cmd->data.create_destroy_addr.mac_addr))
		return -EADDRNOTAVAIL;

	ether_addr_copy(card->dev->dev_addr,
			cmd->data.create_destroy_addr.mac_addr);
	return 0;
}

static int qeth_l3_iqd_read_initial_mac(struct qeth_card *card)
{
	int rc = 0;
	struct qeth_cmd_buffer *iob;

	QETH_CARD_TEXT(card, 2, "hsrmac");

	iob = qeth_ipa_alloc_cmd(card, IPA_CMD_CREATE_ADDR, QETH_PROT_IPV6,
				 IPA_DATA_SIZEOF(create_destroy_addr));
	if (!iob)
		return -ENOMEM;

	rc = qeth_send_ipa_cmd(card, iob, qeth_l3_iqd_read_initial_mac_cb,
				NULL);
	return rc;
}

static int qeth_l3_get_unique_id_cb(struct qeth_card *card,
		struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd = (struct qeth_ipa_cmd *) data;
	u16 *uid = reply->param;

	if (cmd->hdr.return_code == 0) {
		*uid = cmd->data.create_destroy_addr.uid;
		return 0;
	}

	dev_warn(&card->gdev->dev, "The network adapter failed to generate a unique ID\n");
	return -EIO;
}

static u16 qeth_l3_get_unique_id(struct qeth_card *card, u16 uid)
{
	struct qeth_cmd_buffer *iob;

	QETH_CARD_TEXT(card, 2, "guniqeid");

	if (!qeth_is_supported(card, IPA_IPV6))
		goto out;

	iob = qeth_ipa_alloc_cmd(card, IPA_CMD_CREATE_ADDR, QETH_PROT_IPV6,
				 IPA_DATA_SIZEOF(create_destroy_addr));
	if (!iob)
		goto out;

	__ipa_cmd(iob)->data.create_destroy_addr.uid = uid;
	qeth_send_ipa_cmd(card, iob, qeth_l3_get_unique_id_cb, &uid);

out:
	return uid;
}

static int
qeth_diags_trace_cb(struct qeth_card *card, struct qeth_reply *reply,
			    unsigned long data)
{
	struct qeth_ipa_cmd	   *cmd;
	__u16 rc;

	QETH_CARD_TEXT(card, 2, "diastrcb");

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
		QETH_DBF_MESSAGE(2, "Unknown sniffer action (%#06x) on device %x\n",
				 cmd->data.diagass.action, CARD_DEVID(card));
	}

	return rc ? -EIO : 0;
}

static int
qeth_diags_trace(struct qeth_card *card, enum qeth_diags_trace_cmds diags_cmd)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd    *cmd;

	QETH_CARD_TEXT(card, 2, "diagtrac");

	iob = qeth_get_diag_cmd(card, QETH_DIAGS_CMD_TRACE, 0);
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);
	cmd->data.diagass.type = QETH_DIAGS_TYPE_HIPERSOCKET;
	cmd->data.diagass.action = diags_cmd;
	return qeth_send_ipa_cmd(card, iob, qeth_diags_trace_cb, NULL);
}

static int qeth_l3_add_mcast_rtnl(struct net_device *dev, int vid, void *arg)
{
	struct qeth_card *card = arg;
	struct inet6_dev *in6_dev;
	struct in_device *in4_dev;
	struct qeth_ipaddr *ipm;
	struct qeth_ipaddr tmp;
	struct ip_mc_list *im4;
	struct ifmcaddr6 *im6;

	QETH_CARD_TEXT(card, 4, "addmc");

	if (!dev || !(dev->flags & IFF_UP))
		goto out;

	in4_dev = __in_dev_get_rtnl(dev);
	if (!in4_dev)
		goto walk_ipv6;

	qeth_l3_init_ipaddr(&tmp, QETH_IP_TYPE_NORMAL, QETH_PROT_IPV4);
	tmp.disp_flag = QETH_DISP_ADDR_ADD;
	tmp.is_multicast = 1;

	for (im4 = rtnl_dereference(in4_dev->mc_list); im4 != NULL;
	     im4 = rtnl_dereference(im4->next_rcu)) {
		tmp.u.a4.addr = im4->multiaddr;

		ipm = qeth_l3_find_addr_by_ip(card, &tmp);
		if (ipm) {
			/* for mcast, by-IP match means full match */
			ipm->disp_flag = QETH_DISP_ADDR_DO_NOTHING;
			continue;
		}

		ipm = kmemdup(&tmp, sizeof(tmp), GFP_KERNEL);
		if (!ipm)
			continue;

		hash_add(card->rx_mode_addrs, &ipm->hnode,
			 qeth_l3_ipaddr_hash(ipm));
	}

walk_ipv6:
	if (!qeth_is_supported(card, IPA_IPV6))
		goto out;

	in6_dev = __in6_dev_get(dev);
	if (!in6_dev)
		goto out;

	qeth_l3_init_ipaddr(&tmp, QETH_IP_TYPE_NORMAL, QETH_PROT_IPV6);
	tmp.disp_flag = QETH_DISP_ADDR_ADD;
	tmp.is_multicast = 1;

	for (im6 = rtnl_dereference(in6_dev->mc_list);
	     im6;
	     im6 = rtnl_dereference(im6->next)) {
		tmp.u.a6.addr = im6->mca_addr;

		ipm = qeth_l3_find_addr_by_ip(card, &tmp);
		if (ipm) {
			/* for mcast, by-IP match means full match */
			ipm->disp_flag = QETH_DISP_ADDR_DO_NOTHING;
			continue;
		}

		ipm = kmemdup(&tmp, sizeof(tmp), GFP_ATOMIC);
		if (!ipm)
			continue;

		hash_add(card->rx_mode_addrs, &ipm->hnode,
			 qeth_l3_ipaddr_hash(ipm));

	}

out:
	return 0;
}

static void qeth_l3_set_promisc_mode(struct qeth_card *card)
{
	bool enable = card->dev->flags & IFF_PROMISC;

	if (card->info.promisc_mode == enable)
		return;

	if (IS_VM_NIC(card)) {		/* Guestlan trace */
		if (qeth_adp_supported(card, IPA_SETADP_SET_PROMISC_MODE))
			qeth_setadp_promisc_mode(card, enable);
	} else if (card->options.sniffer &&	/* HiperSockets trace */
		   qeth_adp_supported(card, IPA_SETADP_SET_DIAG_ASSIST)) {
		if (enable) {
			QETH_CARD_TEXT(card, 3, "+promisc");
			qeth_diags_trace(card, QETH_DIAGS_CMD_TRACE_ENABLE);
		} else {
			QETH_CARD_TEXT(card, 3, "-promisc");
			qeth_diags_trace(card, QETH_DIAGS_CMD_TRACE_DISABLE);
		}
	}
}

static void qeth_l3_rx_mode_work(struct work_struct *work)
{
	struct qeth_card *card = container_of(work, struct qeth_card,
					      rx_mode_work);
	struct qeth_ipaddr *addr;
	struct hlist_node *tmp;
	int i, rc;

	QETH_CARD_TEXT(card, 3, "setmulti");

	if (!card->options.sniffer) {
		rtnl_lock();
		qeth_l3_add_mcast_rtnl(card->dev, 0, card);
		if (qeth_is_supported(card, IPA_FULL_VLAN))
			vlan_for_each(card->dev, qeth_l3_add_mcast_rtnl, card);
		rtnl_unlock();

		hash_for_each_safe(card->rx_mode_addrs, i, tmp, addr, hnode) {
			switch (addr->disp_flag) {
			case QETH_DISP_ADDR_DELETE:
				rc = qeth_l3_deregister_addr_entry(card, addr);
				if (!rc || rc == -ENOENT) {
					hash_del(&addr->hnode);
					kfree(addr);
				}
				break;
			case QETH_DISP_ADDR_ADD:
				rc = qeth_l3_register_addr_entry(card, addr);
				if (rc && rc != -ENETDOWN) {
					hash_del(&addr->hnode);
					kfree(addr);
					break;
				}
				fallthrough;
			default:
				/* for next call to set_rx_mode(): */
				addr->disp_flag = QETH_DISP_ADDR_DELETE;
			}
		}
	}

	qeth_l3_set_promisc_mode(card);
}

static int qeth_l3_arp_makerc(u16 rc)
{
	switch (rc) {
	case IPA_RC_SUCCESS:
		return 0;
	case QETH_IPA_ARP_RC_NOTSUPP:
	case QETH_IPA_ARP_RC_Q_NOTSUPP:
		return -EOPNOTSUPP;
	case QETH_IPA_ARP_RC_OUT_OF_RANGE:
		return -EINVAL;
	case QETH_IPA_ARP_RC_Q_NO_DATA:
		return -ENOENT;
	default:
		return -EIO;
	}
}

static int qeth_l3_arp_cmd_cb(struct qeth_card *card, struct qeth_reply *reply,
			      unsigned long data)
{
	struct qeth_ipa_cmd *cmd = (struct qeth_ipa_cmd *) data;

	qeth_setassparms_cb(card, reply, data);
	return qeth_l3_arp_makerc(cmd->hdr.return_code);
}

static int qeth_l3_arp_set_no_entries(struct qeth_card *card, int no_entries)
{
	struct qeth_cmd_buffer *iob;
	int rc;

	QETH_CARD_TEXT(card, 3, "arpstnoe");

	/*
	 * currently GuestLAN only supports the ARP assist function
	 * IPA_CMD_ASS_ARP_QUERY_INFO, but not IPA_CMD_ASS_ARP_SET_NO_ENTRIES;
	 * thus we say EOPNOTSUPP for this ARP function
	 */
	if (IS_VM_NIC(card))
		return -EOPNOTSUPP;
	if (!qeth_is_supported(card, IPA_ARP_PROCESSING)) {
		return -EOPNOTSUPP;
	}

	iob = qeth_get_setassparms_cmd(card, IPA_ARP_PROCESSING,
				       IPA_CMD_ASS_ARP_SET_NO_ENTRIES,
				       SETASS_DATA_SIZEOF(flags_32bit),
				       QETH_PROT_IPV4);
	if (!iob)
		return -ENOMEM;

	__ipa_cmd(iob)->data.setassparms.data.flags_32bit = (u32) no_entries;
	rc = qeth_send_ipa_cmd(card, iob, qeth_l3_arp_cmd_cb, NULL);
	if (rc)
		QETH_DBF_MESSAGE(2, "Could not set number of ARP entries on device %x: %#x\n",
				 CARD_DEVID(card), rc);
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
		return qeth_l3_arp_makerc(cmd->hdr.return_code);
	}
	if (cmd->data.setassparms.hdr.return_code) {
		cmd->hdr.return_code = cmd->data.setassparms.hdr.return_code;
		QETH_CARD_TEXT(card, 4, "setaperr");
		QETH_CARD_TEXT_(card, 4, "%i", cmd->hdr.return_code);
		return qeth_l3_arp_makerc(cmd->hdr.return_code);
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
			QETH_CARD_TEXT_(card, 4, "qaer3%i", -ENOSPC);
			memset(qinfo->udata, 0, 4);
			return -ENOSPC;
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
}

static int qeth_l3_query_arp_cache_info(struct qeth_card *card,
	enum qeth_prot_versions prot,
	struct qeth_arp_query_info *qinfo)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;
	int rc;

	QETH_CARD_TEXT_(card, 3, "qarpipv%i", prot);

	iob = qeth_get_setassparms_cmd(card, IPA_ARP_PROCESSING,
				       IPA_CMD_ASS_ARP_QUERY_INFO,
				       SETASS_DATA_SIZEOF(query_arp), prot);
	if (!iob)
		return -ENOMEM;
	cmd = __ipa_cmd(iob);
	cmd->data.setassparms.data.query_arp.request_bits = 0x000F;
	rc = qeth_send_ipa_cmd(card, iob, qeth_l3_arp_query_cb, qinfo);
	if (rc)
		QETH_DBF_MESSAGE(2, "Error while querying ARP cache on device %x: %#x\n",
				 CARD_DEVID(card), rc);
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

static int qeth_l3_arp_modify_entry(struct qeth_card *card,
				    struct qeth_arp_cache_entry *entry,
				    enum qeth_arp_process_subcmds arp_cmd)
{
	struct qeth_arp_cache_entry *cmd_entry;
	struct qeth_cmd_buffer *iob;
	int rc;

	if (arp_cmd == IPA_CMD_ASS_ARP_ADD_ENTRY)
		QETH_CARD_TEXT(card, 3, "arpadd");
	else
		QETH_CARD_TEXT(card, 3, "arpdel");

	/*
	 * currently GuestLAN only supports the ARP assist function
	 * IPA_CMD_ASS_ARP_QUERY_INFO, but not IPA_CMD_ASS_ARP_ADD_ENTRY;
	 * thus we say EOPNOTSUPP for this ARP function
	 */
	if (IS_VM_NIC(card))
		return -EOPNOTSUPP;
	if (!qeth_is_supported(card, IPA_ARP_PROCESSING)) {
		return -EOPNOTSUPP;
	}

	iob = qeth_get_setassparms_cmd(card, IPA_ARP_PROCESSING, arp_cmd,
				       SETASS_DATA_SIZEOF(arp_entry),
				       QETH_PROT_IPV4);
	if (!iob)
		return -ENOMEM;

	cmd_entry = &__ipa_cmd(iob)->data.setassparms.data.arp_entry;
	ether_addr_copy(cmd_entry->macaddr, entry->macaddr);
	memcpy(cmd_entry->ipaddr, entry->ipaddr, 4);
	rc = qeth_send_ipa_cmd(card, iob, qeth_l3_arp_cmd_cb, NULL);
	if (rc)
		QETH_DBF_MESSAGE(2, "Could not modify (cmd: %#x) ARP entry on device %x: %#x\n",
				 arp_cmd, CARD_DEVID(card), rc);
	return rc;
}

static int qeth_l3_arp_flush_cache(struct qeth_card *card)
{
	struct qeth_cmd_buffer *iob;
	int rc;

	QETH_CARD_TEXT(card, 3, "arpflush");

	/*
	 * currently GuestLAN only supports the ARP assist function
	 * IPA_CMD_ASS_ARP_QUERY_INFO, but not IPA_CMD_ASS_ARP_FLUSH_CACHE;
	 * thus we say EOPNOTSUPP for this ARP function
	*/
	if (IS_VM_NIC(card) || IS_IQD(card))
		return -EOPNOTSUPP;
	if (!qeth_is_supported(card, IPA_ARP_PROCESSING)) {
		return -EOPNOTSUPP;
	}

	iob = qeth_get_setassparms_cmd(card, IPA_ARP_PROCESSING,
				       IPA_CMD_ASS_ARP_FLUSH_CACHE, 0,
				       QETH_PROT_IPV4);
	if (!iob)
		return -ENOMEM;

	rc = qeth_send_ipa_cmd(card, iob, qeth_l3_arp_cmd_cb, NULL);
	if (rc)
		QETH_DBF_MESSAGE(2, "Could not flush ARP cache on device %x: %#x\n",
				 CARD_DEVID(card), rc);
	return rc;
}

static int qeth_l3_do_ioctl(struct net_device *dev, struct ifreq *rq, void __user *data, int cmd)
{
	struct qeth_card *card = dev->ml_priv;
	struct qeth_arp_cache_entry arp_entry;
	enum qeth_arp_process_subcmds arp_cmd;
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
		rc = qeth_l3_arp_query(card, data);
		break;
	case SIOC_QETH_ARP_ADD_ENTRY:
	case SIOC_QETH_ARP_REMOVE_ENTRY:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (copy_from_user(&arp_entry, data, sizeof(arp_entry)))
			return -EFAULT;

		arp_cmd = (cmd == SIOC_QETH_ARP_ADD_ENTRY) ?
				IPA_CMD_ASS_ARP_ADD_ENTRY :
				IPA_CMD_ASS_ARP_REMOVE_ENTRY;
		return qeth_l3_arp_modify_entry(card, &arp_entry, arp_cmd);
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

static int qeth_l3_get_cast_type_rcu(struct sk_buff *skb, struct dst_entry *dst,
				     __be16 proto)
{
	struct neighbour *n = NULL;

	if (dst)
		n = dst_neigh_lookup_skb(dst, skb);

	if (n) {
		int cast_type = n->type;

		neigh_release(n);
		if ((cast_type == RTN_BROADCAST) ||
		    (cast_type == RTN_MULTICAST) ||
		    (cast_type == RTN_ANYCAST))
			return cast_type;
		return RTN_UNICAST;
	}

	/* no neighbour (eg AF_PACKET), fall back to target's IP address ... */
	switch (proto) {
	case htons(ETH_P_IP):
		if (ipv4_is_lbcast(ip_hdr(skb)->daddr))
			return RTN_BROADCAST;
		return ipv4_is_multicast(ip_hdr(skb)->daddr) ?
				RTN_MULTICAST : RTN_UNICAST;
	case htons(ETH_P_IPV6):
		return ipv6_addr_is_multicast(&ipv6_hdr(skb)->daddr) ?
				RTN_MULTICAST : RTN_UNICAST;
	case htons(ETH_P_AF_IUCV):
		return RTN_UNICAST;
	default:
		/* OSA only: ... and MAC address */
		return qeth_get_ether_cast_type(skb);
	}
}

static int qeth_l3_get_cast_type(struct sk_buff *skb, __be16 proto)
{
	struct dst_entry *dst;
	int cast_type;

	rcu_read_lock();
	dst = qeth_dst_check_rcu(skb, proto);
	cast_type = qeth_l3_get_cast_type_rcu(skb, dst, proto);
	rcu_read_unlock();

	return cast_type;
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

static void qeth_l3_fill_header(struct qeth_qdio_out_q *queue,
				struct qeth_hdr *hdr, struct sk_buff *skb,
				__be16 proto, unsigned int data_len)
{
	struct qeth_hdr_layer3 *l3_hdr = &hdr->hdr.l3;
	struct vlan_ethhdr *veth = vlan_eth_hdr(skb);
	struct qeth_card *card = queue->card;
	struct dst_entry *dst;
	int cast_type;

	hdr->hdr.l3.length = data_len;

	if (skb_is_gso(skb)) {
		hdr->hdr.l3.id = QETH_HEADER_TYPE_L3_TSO;
	} else {
		hdr->hdr.l3.id = QETH_HEADER_TYPE_LAYER3;

		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			qeth_tx_csum(skb, &hdr->hdr.l3.ext_flags, proto);
			/* some HW requires combined L3+L4 csum offload: */
			if (proto == htons(ETH_P_IP))
				hdr->hdr.l3.ext_flags |= QETH_HDR_EXT_CSUM_HDR_REQ;
		}
	}

	if (proto == htons(ETH_P_IP) || IS_IQD(card)) {
		/* NETIF_F_HW_VLAN_CTAG_TX */
		if (skb_vlan_tag_present(skb)) {
			hdr->hdr.l3.ext_flags |= QETH_HDR_EXT_VLAN_FRAME;
			hdr->hdr.l3.vlan_id = skb_vlan_tag_get(skb);
		}
	} else if (veth->h_vlan_proto == htons(ETH_P_8021Q)) {
		hdr->hdr.l3.ext_flags |= QETH_HDR_EXT_INCLUDE_VLAN_TAG;
		hdr->hdr.l3.vlan_id = ntohs(veth->h_vlan_TCI);
	}

	rcu_read_lock();
	dst = qeth_dst_check_rcu(skb, proto);

	if (IS_IQD(card) && skb_get_queue_mapping(skb) != QETH_IQD_MCAST_TXQ)
		cast_type = RTN_UNICAST;
	else
		cast_type = qeth_l3_get_cast_type_rcu(skb, dst, proto);
	l3_hdr->flags |= qeth_l3_cast_type_to_flag(cast_type);

	switch (proto) {
	case htons(ETH_P_IP):
		l3_hdr->next_hop.addr.s6_addr32[3] =
					qeth_next_hop_v4_rcu(skb, dst);
		break;
	case htons(ETH_P_IPV6):
		l3_hdr->next_hop.addr = *qeth_next_hop_v6_rcu(skb, dst);

		hdr->hdr.l3.flags |= QETH_HDR_IPV6;
		if (!IS_IQD(card))
			hdr->hdr.l3.flags |= QETH_HDR_PASSTHRU;
		break;
	case htons(ETH_P_AF_IUCV):
		l3_hdr->next_hop.addr.s6_addr16[0] = htons(0xfe80);
		memcpy(&l3_hdr->next_hop.addr.s6_addr32[2],
		       iucv_trans_hdr(skb)->destUserID, 8);
		l3_hdr->flags |= QETH_HDR_IPV6;
		break;
	default:
		/* OSA only: */
		l3_hdr->flags |= QETH_HDR_PASSTHRU;
	}
	rcu_read_unlock();
}

static void qeth_l3_fixup_headers(struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);

	/* this is safe, IPv6 traffic takes a different path */
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		iph->check = 0;
	if (skb_is_gso(skb)) {
		iph->tot_len = 0;
		tcp_hdr(skb)->check = ~tcp_v4_check(0, iph->saddr,
						    iph->daddr, 0);
	}
}

static int qeth_l3_xmit(struct qeth_card *card, struct sk_buff *skb,
			struct qeth_qdio_out_q *queue, __be16 proto)
{
	unsigned int hw_hdr_len;
	int rc;

	/* re-use the L2 header area for the HW header: */
	hw_hdr_len = skb_is_gso(skb) ? sizeof(struct qeth_hdr_tso) :
				       sizeof(struct qeth_hdr);
	rc = skb_cow_head(skb, hw_hdr_len - ETH_HLEN);
	if (rc)
		return rc;
	skb_pull(skb, ETH_HLEN);

	qeth_l3_fixup_headers(skb);
	return qeth_xmit(card, skb, queue, proto, qeth_l3_fill_header);
}

static netdev_tx_t qeth_l3_hard_start_xmit(struct sk_buff *skb,
					   struct net_device *dev)
{
	struct qeth_card *card = dev->ml_priv;
	__be16 proto = vlan_get_protocol(skb);
	u16 txq = skb_get_queue_mapping(skb);
	struct qeth_qdio_out_q *queue;
	int rc;

	if (!skb_is_gso(skb))
		qdisc_skb_cb(skb)->pkt_len = skb->len;
	if (IS_IQD(card)) {
		queue = card->qdio.out_qs[qeth_iqd_translate_txq(dev, txq)];

		if (card->options.sniffer)
			goto tx_drop;

		switch (proto) {
		case htons(ETH_P_AF_IUCV):
			if (card->options.cq != QETH_CQ_ENABLED)
				goto tx_drop;
			break;
		case htons(ETH_P_IP):
		case htons(ETH_P_IPV6):
			if (card->options.cq == QETH_CQ_ENABLED)
				goto tx_drop;
			break;
		default:
			goto tx_drop;
		}
	} else {
		queue = card->qdio.out_qs[txq];
	}

	if (!(dev->flags & IFF_BROADCAST) &&
	    qeth_l3_get_cast_type(skb, proto) == RTN_BROADCAST)
		goto tx_drop;

	if (proto == htons(ETH_P_IP) || IS_IQD(card))
		rc = qeth_l3_xmit(card, skb, queue, proto);
	else
		rc = qeth_xmit(card, skb, queue, proto, qeth_l3_fill_header);

	if (!rc)
		return NETDEV_TX_OK;

tx_drop:
	QETH_TXQ_STAT_INC(queue, tx_dropped);
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static void qeth_l3_set_rx_mode(struct net_device *dev)
{
	struct qeth_card *card = dev->ml_priv;

	schedule_work(&card->rx_mode_work);
}

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

static netdev_features_t qeth_l3_osa_features_check(struct sk_buff *skb,
						    struct net_device *dev,
						    netdev_features_t features)
{
	if (vlan_get_protocol(skb) != htons(ETH_P_IP))
		features &= ~NETIF_F_HW_VLAN_CTAG_TX;
	return qeth_features_check(skb, dev, features);
}

static u16 qeth_l3_iqd_select_queue(struct net_device *dev, struct sk_buff *skb,
				    struct net_device *sb_dev)
{
	__be16 proto = vlan_get_protocol(skb);

	return qeth_iqd_select_queue(dev, skb,
				     qeth_l3_get_cast_type(skb, proto), sb_dev);
}

static u16 qeth_l3_osa_select_queue(struct net_device *dev, struct sk_buff *skb,
				    struct net_device *sb_dev)
{
	struct qeth_card *card = dev->ml_priv;

	if (qeth_uses_tx_prio_queueing(card))
		return qeth_get_priority_queue(card, skb);

	return netdev_pick_tx(dev, skb, sb_dev);
}

static const struct net_device_ops qeth_l3_netdev_ops = {
	.ndo_open		= qeth_open,
	.ndo_stop		= qeth_stop,
	.ndo_get_stats64	= qeth_get_stats64,
	.ndo_start_xmit		= qeth_l3_hard_start_xmit,
	.ndo_select_queue	= qeth_l3_iqd_select_queue,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= qeth_l3_set_rx_mode,
	.ndo_eth_ioctl		= qeth_do_ioctl,
	.ndo_siocdevprivate	= qeth_siocdevprivate,
	.ndo_fix_features	= qeth_fix_features,
	.ndo_set_features	= qeth_set_features,
	.ndo_tx_timeout		= qeth_tx_timeout,
};

static const struct net_device_ops qeth_l3_osa_netdev_ops = {
	.ndo_open		= qeth_open,
	.ndo_stop		= qeth_stop,
	.ndo_get_stats64	= qeth_get_stats64,
	.ndo_start_xmit		= qeth_l3_hard_start_xmit,
	.ndo_features_check	= qeth_l3_osa_features_check,
	.ndo_select_queue	= qeth_l3_osa_select_queue,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= qeth_l3_set_rx_mode,
	.ndo_eth_ioctl		= qeth_do_ioctl,
	.ndo_siocdevprivate	= qeth_siocdevprivate,
	.ndo_fix_features	= qeth_fix_features,
	.ndo_set_features	= qeth_set_features,
	.ndo_tx_timeout		= qeth_tx_timeout,
	.ndo_neigh_setup	= qeth_l3_neigh_setup,
};

static int qeth_l3_setup_netdev(struct qeth_card *card)
{
	struct net_device *dev = card->dev;
	unsigned int headroom;
	int rc;

	if (IS_OSD(card) || IS_OSX(card)) {
		card->dev->netdev_ops = &qeth_l3_osa_netdev_ops;

		/*IPv6 address autoconfiguration stuff*/
		dev->dev_id = qeth_l3_get_unique_id(card, dev->dev_id);

		if (!IS_VM_NIC(card)) {
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
		if (qeth_is_supported6(card, IPA_OUTBOUND_TSO)) {
			card->dev->hw_features |= NETIF_F_TSO6;
			card->dev->vlan_features |= NETIF_F_TSO6;
		}

		/* allow for de-acceleration of NETIF_F_HW_VLAN_CTAG_TX: */
		if (card->dev->hw_features & NETIF_F_TSO6)
			headroom = sizeof(struct qeth_hdr_tso) + VLAN_HLEN;
		else if (card->dev->hw_features & NETIF_F_TSO)
			headroom = sizeof(struct qeth_hdr_tso);
		else
			headroom = sizeof(struct qeth_hdr) + VLAN_HLEN;
	} else if (IS_IQD(card)) {
		card->dev->flags |= IFF_NOARP;
		card->dev->netdev_ops = &qeth_l3_netdev_ops;
		headroom = sizeof(struct qeth_hdr) - ETH_HLEN;

		rc = qeth_l3_iqd_read_initial_mac(card);
		if (rc)
			return rc;
	} else
		return -ENODEV;

	card->dev->needed_headroom = headroom;
	card->dev->features |=	NETIF_F_HW_VLAN_CTAG_TX |
				NETIF_F_HW_VLAN_CTAG_RX;

	netif_keep_dst(card->dev);
	if (card->dev->hw_features & (NETIF_F_TSO | NETIF_F_TSO6))
		netif_set_gso_max_size(card->dev,
				       PAGE_SIZE * (QETH_MAX_BUFFER_ELEMENTS(card) - 1));

	netif_napi_add(card->dev, &card->napi, qeth_poll, QETH_NAPI_WEIGHT);
	return register_netdev(card->dev);
}

static const struct device_type qeth_l3_devtype = {
	.name = "qeth_layer3",
	.groups = qeth_l3_attr_groups,
};

static int qeth_l3_probe_device(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	int rc;

	hash_init(card->ip_htable);
	mutex_init(&card->ip_lock);
	card->cmd_wq = alloc_ordered_workqueue("%s_cmd", 0,
					       dev_name(&gdev->dev));
	if (!card->cmd_wq)
		return -ENOMEM;

	if (gdev->dev.type) {
		rc = device_add_groups(&gdev->dev, qeth_l3_attr_groups);
		if (rc) {
			destroy_workqueue(card->cmd_wq);
			return rc;
		}
	} else {
		gdev->dev.type = &qeth_l3_devtype;
	}

	INIT_WORK(&card->rx_mode_work, qeth_l3_rx_mode_work);
	return 0;
}

static void qeth_l3_remove_device(struct ccwgroup_device *cgdev)
{
	struct qeth_card *card = dev_get_drvdata(&cgdev->dev);

	if (cgdev->dev.type != &qeth_l3_devtype)
		device_remove_groups(&cgdev->dev, qeth_l3_attr_groups);

	qeth_set_allowed_threads(card, 0, 1);
	wait_event(card->wait_q, qeth_threads_running(card, 0xffffffff) == 0);

	if (cgdev->state == CCWGROUP_ONLINE)
		qeth_set_offline(card, card->discipline, false);

	cancel_work_sync(&card->close_dev_work);
	if (card->dev->reg_state == NETREG_REGISTERED)
		unregister_netdev(card->dev);

	flush_workqueue(card->cmd_wq);
	destroy_workqueue(card->cmd_wq);
	qeth_l3_clear_ip_htable(card, 0);
	qeth_l3_clear_ipato_list(card);
}

static int qeth_l3_set_online(struct qeth_card *card, bool carrier_ok)
{
	struct net_device *dev = card->dev;
	int rc = 0;

	/* softsetup */
	QETH_CARD_TEXT(card, 2, "softsetp");

	rc = qeth_l3_setadapter_parms(card);
	if (rc)
		QETH_CARD_TEXT_(card, 2, "2err%04x", rc);
	if (!card->options.sniffer) {
		qeth_l3_start_ipassists(card);

		rc = qeth_l3_setrouting_v4(card);
		if (rc)
			QETH_CARD_TEXT_(card, 2, "4err%04x", rc);
		rc = qeth_l3_setrouting_v6(card);
		if (rc)
			QETH_CARD_TEXT_(card, 2, "5err%04x", rc);
	}

	card->state = CARD_STATE_SOFTSETUP;

	qeth_set_allowed_threads(card, 0xffffffff, 0);
	qeth_l3_recover_ip(card);

	if (dev->reg_state != NETREG_REGISTERED) {
		rc = qeth_l3_setup_netdev(card);
		if (rc)
			goto err_setup;

		if (carrier_ok)
			netif_carrier_on(dev);
	} else {
		rtnl_lock();
		rc = qeth_set_real_num_tx_queues(card,
						 qeth_tx_actual_queues(card));
		if (rc) {
			rtnl_unlock();
			goto err_set_queues;
		}

		if (carrier_ok)
			netif_carrier_on(dev);
		else
			netif_carrier_off(dev);

		netif_device_attach(dev);
		qeth_enable_hw_features(dev);

		if (card->info.open_when_online) {
			card->info.open_when_online = 0;
			dev_open(dev, NULL);
		}
		rtnl_unlock();
	}
	return 0;

err_set_queues:
err_setup:
	qeth_set_allowed_threads(card, 0, 1);
	card->state = CARD_STATE_DOWN;
	qeth_l3_clear_ip_htable(card, 1);
	return rc;
}

static void qeth_l3_set_offline(struct qeth_card *card)
{
	qeth_set_allowed_threads(card, 0, 1);
	qeth_l3_drain_rx_mode_cache(card);

	if (card->options.sniffer &&
	    (card->info.promisc_mode == SET_PROMISC_MODE_ON))
		qeth_diags_trace(card, QETH_DIAGS_CMD_TRACE_DISABLE);

	if (card->state == CARD_STATE_SOFTSETUP) {
		card->state = CARD_STATE_DOWN;
		qeth_l3_clear_ip_htable(card, 1);
	}
}

/* Returns zero if the command is successfully "consumed" */
static int qeth_l3_control_event(struct qeth_card *card,
					struct qeth_ipa_cmd *cmd)
{
	return 1;
}

const struct qeth_discipline qeth_l3_discipline = {
	.setup = qeth_l3_probe_device,
	.remove = qeth_l3_remove_device,
	.set_online = qeth_l3_set_online,
	.set_offline = qeth_l3_set_offline,
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
		qeth_l3_modify_ip(card, addr, true);
		return NOTIFY_OK;
	case NETDEV_DOWN:
		qeth_l3_modify_ip(card, addr, false);
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

struct qeth_l3_ip_event_work {
	struct work_struct work;
	struct qeth_card *card;
	struct qeth_ipaddr addr;
};

#define to_ip_work(w) container_of((w), struct qeth_l3_ip_event_work, work)

static void qeth_l3_add_ip_worker(struct work_struct *work)
{
	struct qeth_l3_ip_event_work *ip_work = to_ip_work(work);

	qeth_l3_modify_ip(ip_work->card, &ip_work->addr, true);
	kfree(work);
}

static void qeth_l3_delete_ip_worker(struct work_struct *work)
{
	struct qeth_l3_ip_event_work *ip_work = to_ip_work(work);

	qeth_l3_modify_ip(ip_work->card, &ip_work->addr, false);
	kfree(work);
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

	card = qeth_l3_get_card_from_dev(dev);
	if (!card)
		return NOTIFY_DONE;
	QETH_CARD_TEXT(card, 3, "ipevent");

	qeth_l3_init_ipaddr(&addr, QETH_IP_TYPE_NORMAL, QETH_PROT_IPV4);
	addr.u.a4.addr = ifa->ifa_address;
	addr.u.a4.mask = ifa->ifa_mask;

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
	struct qeth_l3_ip_event_work *ip_work;
	struct qeth_card *card;

	if (event != NETDEV_UP && event != NETDEV_DOWN)
		return NOTIFY_DONE;

	card = qeth_l3_get_card_from_dev(dev);
	if (!card)
		return NOTIFY_DONE;
	QETH_CARD_TEXT(card, 3, "ip6event");
	if (!qeth_is_supported(card, IPA_IPV6))
		return NOTIFY_DONE;

	ip_work = kmalloc(sizeof(*ip_work), GFP_ATOMIC);
	if (!ip_work)
		return NOTIFY_DONE;

	if (event == NETDEV_UP)
		INIT_WORK(&ip_work->work, qeth_l3_add_ip_worker);
	else
		INIT_WORK(&ip_work->work, qeth_l3_delete_ip_worker);

	ip_work->card = card;
	qeth_l3_init_ipaddr(&ip_work->addr, QETH_IP_TYPE_NORMAL,
			    QETH_PROT_IPV6);
	ip_work->addr.u.a6.addr = ifa->addr;
	ip_work->addr.u.a6.pfxlen = ifa->prefix_len;

	queue_work(card->cmd_wq, &ip_work->work);
	return NOTIFY_OK;
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
