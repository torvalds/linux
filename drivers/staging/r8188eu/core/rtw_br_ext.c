// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. i*/

#define _RTW_BR_EXT_C_

#include "../include/linux/if_arp.h"
#include "../include/net/ip.h"
#include "../include/linux/atalk.h"
#include "../include/linux/udp.h"
#include "../include/linux/if_pppox.h"

#include "../include/drv_types.h"
#include "../include/rtw_br_ext.h"
#include "../include/usb_osintf.h"
#include "../include/recv_osdep.h"

#ifndef csum_ipv6_magic
#include "../include/net/ip6_checksum.h"
#endif

#include "../include/linux/ipv6.h"
#include "../include/linux/icmpv6.h"
#include "../include/net/ndisc.h"
#include "../include/net/checksum.h"

#define NAT25_IPV4		01
#define NAT25_IPV6		02
#define NAT25_IPX		03
#define NAT25_APPLE		04
#define NAT25_PPPOE		05

#define RTL_RELAY_TAG_LEN (ETH_ALEN)
#define TAG_HDR_LEN		4

#define MAGIC_CODE		0x8186
#define MAGIC_CODE_LEN	2
#define WAIT_TIME_PPPOE	5	/*  waiting time for pppoe server in sec */

/*-----------------------------------------------------------------
  How database records network address:
	   0    1    2    3    4    5    6    7    8    9   10
	|----|----|----|----|----|----|----|----|----|----|----|
  IPv4  |type|                             |      IP addr      |
  IPX   |type|      Net addr     |          Node addr          |
  IPX   |type|      Net addr     |Sckt addr|
  Apple |type| Network |node|
  PPPoE |type|   SID   |           AC MAC            |
-----------------------------------------------------------------*/

/* Find a tag in pppoe frame and return the pointer */
static unsigned char *__nat25_find_pppoe_tag(struct pppoe_hdr *ph, unsigned short type)
{
	unsigned char *cur_ptr, *start_ptr;
	unsigned short tagLen, tagType;

	start_ptr = cur_ptr = (unsigned char *)ph->tag;
	while ((cur_ptr - start_ptr) < ntohs(ph->length)) {
		/*  prevent un-alignment access */
		tagType = (unsigned short)((cur_ptr[0] << 8) + cur_ptr[1]);
		tagLen  = (unsigned short)((cur_ptr[2] << 8) + cur_ptr[3]);
		if (tagType == type)
			return cur_ptr;
		cur_ptr = cur_ptr + TAG_HDR_LEN + tagLen;
	}
	return NULL;
}

static int __nat25_add_pppoe_tag(struct sk_buff *skb, struct pppoe_tag *tag)
{
	struct pppoe_hdr *ph = (struct pppoe_hdr *)(skb->data + ETH_HLEN);
	int data_len;

	data_len = be16_to_cpu(tag->tag_len) + TAG_HDR_LEN;
	if (skb_tailroom(skb) < data_len)
		return -1;

	skb_put(skb, data_len);
	/*  have a room for new tag */
	memmove(((unsigned char *)ph->tag + data_len), (unsigned char *)ph->tag, ntohs(ph->length));
	ph->length = htons(ntohs(ph->length) + data_len);
	memcpy((unsigned char *)ph->tag, tag, data_len);
	return data_len;
}

static int skb_pull_and_merge(struct sk_buff *skb, unsigned char *src, int len)
{
	int tail_len;
	unsigned long end, tail;

	if ((src+len) > skb_tail_pointer(skb) || skb->len < len)
		return -1;

	tail = (unsigned long)skb_tail_pointer(skb);
	end = (unsigned long)src+len;
	if (tail < end)
		return -1;

	tail_len = (int)(tail-end);
	if (tail_len > 0)
		memmove(src, src+len, tail_len);

	skb_trim(skb, skb->len-len);
	return 0;
}

static int  __nat25_has_expired(struct nat25_network_db_entry *fdb)
{
	if (time_before_eq(fdb->ageing_timer, jiffies - NAT25_AGEING_TIME * HZ))
		return 1;

	return 0;
}

static void __nat25_generate_ipv4_network_addr(unsigned char *networkAddr,
				unsigned int *ipAddr)
{
	memset(networkAddr, 0, MAX_NETWORK_ADDR_LEN);

	networkAddr[0] = NAT25_IPV4;
	memcpy(networkAddr+7, (unsigned char *)ipAddr, 4);
}

static void __nat25_generate_pppoe_network_addr(unsigned char *networkAddr,
				unsigned char *ac_mac, __be16 *sid)
{
	memset(networkAddr, 0, MAX_NETWORK_ADDR_LEN);

	networkAddr[0] = NAT25_PPPOE;
	memcpy(networkAddr+1, (unsigned char *)sid, 2);
	memcpy(networkAddr+3, (unsigned char *)ac_mac, 6);
}

static  void __nat25_generate_ipv6_network_addr(unsigned char *networkAddr,
				unsigned int *ipAddr)
{
	memset(networkAddr, 0, MAX_NETWORK_ADDR_LEN);

	networkAddr[0] = NAT25_IPV6;
	memcpy(networkAddr+1, (unsigned char *)ipAddr, 16);
}

static unsigned char *scan_tlv(unsigned char *data, int len, unsigned char tag, unsigned char len8b)
{
	while (len > 0) {
		if (*data == tag && *(data+1) == len8b && len >= len8b*8)
			return data+2;

		len -= (*(data+1))*8;
		data += (*(data+1))*8;
	}
	return NULL;
}

static int update_nd_link_layer_addr(unsigned char *data, int len, unsigned char *replace_mac)
{
	struct icmp6hdr *icmphdr = (struct icmp6hdr *)data;
	unsigned char *mac;

	if (icmphdr->icmp6_type == NDISC_ROUTER_SOLICITATION) {
		if (len >= 8) {
			mac = scan_tlv(&data[8], len-8, 1, 1);
			if (mac) {
				memcpy(mac, replace_mac, 6);
				return 1;
			}
		}
	} else if (icmphdr->icmp6_type == NDISC_ROUTER_ADVERTISEMENT) {
		if (len >= 16) {
			mac = scan_tlv(&data[16], len-16, 1, 1);
			if (mac) {
				memcpy(mac, replace_mac, 6);
				return 1;
			}
		}
	} else if (icmphdr->icmp6_type == NDISC_NEIGHBOUR_SOLICITATION) {
		if (len >= 24) {
			mac = scan_tlv(&data[24], len-24, 1, 1);
			if (mac) {
				memcpy(mac, replace_mac, 6);
				return 1;
			}
		}
	} else if (icmphdr->icmp6_type == NDISC_NEIGHBOUR_ADVERTISEMENT) {
		if (len >= 24) {
			mac = scan_tlv(&data[24], len-24, 2, 1);
			if (mac) {
				memcpy(mac, replace_mac, 6);
				return 1;
			}
		}
	} else if (icmphdr->icmp6_type == NDISC_REDIRECT) {
		if (len >= 40) {
			mac = scan_tlv(&data[40], len-40, 2, 1);
			if (mac) {
				memcpy(mac, replace_mac, 6);
				return 1;
			}
		}
	}
	return 0;
}

static int __nat25_network_hash(unsigned char *networkAddr)
{
	if (networkAddr[0] == NAT25_IPV4) {
		unsigned long x;

		x = networkAddr[7] ^ networkAddr[8] ^ networkAddr[9] ^ networkAddr[10];

		return x & (NAT25_HASH_SIZE - 1);
	} else if (networkAddr[0] == NAT25_IPX) {
		unsigned long x;

		x = networkAddr[1] ^ networkAddr[2] ^ networkAddr[3] ^ networkAddr[4] ^ networkAddr[5] ^
			networkAddr[6] ^ networkAddr[7] ^ networkAddr[8] ^ networkAddr[9] ^ networkAddr[10];

		return x & (NAT25_HASH_SIZE - 1);
	} else if (networkAddr[0] == NAT25_APPLE) {
		unsigned long x;

		x = networkAddr[1] ^ networkAddr[2] ^ networkAddr[3];

		return x & (NAT25_HASH_SIZE - 1);
	} else if (networkAddr[0] == NAT25_PPPOE) {
		unsigned long x;

		x = networkAddr[0] ^ networkAddr[1] ^ networkAddr[2] ^ networkAddr[3] ^ networkAddr[4] ^ networkAddr[5] ^ networkAddr[6] ^ networkAddr[7] ^ networkAddr[8];

		return x & (NAT25_HASH_SIZE - 1);
	} else if (networkAddr[0] == NAT25_IPV6) {
		unsigned long x;

		x = networkAddr[1] ^ networkAddr[2] ^ networkAddr[3] ^ networkAddr[4] ^ networkAddr[5] ^
			networkAddr[6] ^ networkAddr[7] ^ networkAddr[8] ^ networkAddr[9] ^ networkAddr[10] ^
			networkAddr[11] ^ networkAddr[12] ^ networkAddr[13] ^ networkAddr[14] ^ networkAddr[15] ^
			networkAddr[16];

		return x & (NAT25_HASH_SIZE - 1);
	} else {
		unsigned long x = 0;
		int i;

		for (i = 0; i < MAX_NETWORK_ADDR_LEN; i++)
			x ^= networkAddr[i];

		return x & (NAT25_HASH_SIZE - 1);
	}
}

static void __network_hash_link(struct adapter *priv,
				struct nat25_network_db_entry *ent, int hash)
{
	/*  Caller must spin_lock already! */
	ent->next_hash = priv->nethash[hash];
	if (ent->next_hash)
		ent->next_hash->pprev_hash = &ent->next_hash;
	priv->nethash[hash] = ent;
	ent->pprev_hash = &priv->nethash[hash];
}

static void __network_hash_unlink(struct nat25_network_db_entry *ent)
{
	/*  Caller must spin_lock already! */
	*ent->pprev_hash = ent->next_hash;
	if (ent->next_hash)
		ent->next_hash->pprev_hash = ent->pprev_hash;
	ent->next_hash = NULL;
	ent->pprev_hash = NULL;
}

static void __nat25_db_network_insert(struct adapter *priv,
				unsigned char *macAddr, unsigned char *networkAddr)
{
	struct nat25_network_db_entry *db;
	int hash;

	spin_lock_bh(&priv->br_ext_lock);
	hash = __nat25_network_hash(networkAddr);
	db = priv->nethash[hash];
	while (db) {
		if (!memcmp(db->networkAddr, networkAddr, MAX_NETWORK_ADDR_LEN)) {
			memcpy(db->macAddr, macAddr, ETH_ALEN);
			db->ageing_timer = jiffies;
			spin_unlock_bh(&priv->br_ext_lock);
			return;
		}
		db = db->next_hash;
	}
	db = kmalloc(sizeof(*db), GFP_ATOMIC);
	if (!db) {
		spin_unlock_bh(&priv->br_ext_lock);
		return;
	}
	memcpy(db->networkAddr, networkAddr, MAX_NETWORK_ADDR_LEN);
	memcpy(db->macAddr, macAddr, ETH_ALEN);
	atomic_set(&db->use_count, 1);
	db->ageing_timer = jiffies;

	__network_hash_link(priv, db, hash);

	spin_unlock_bh(&priv->br_ext_lock);
}

/*
 *	NAT2.5 interface
 */

void nat25_db_cleanup(struct adapter *priv)
{
	int i;

	spin_lock_bh(&priv->br_ext_lock);

	for (i = 0; i < NAT25_HASH_SIZE; i++) {
		struct nat25_network_db_entry *f;
		f = priv->nethash[i];
		while (f) {
			struct nat25_network_db_entry *g;

			g = f->next_hash;
			if (priv->scdb_entry == f) {
				memset(priv->scdb_mac, 0, ETH_ALEN);
				memset(priv->scdb_ip, 0, 4);
				priv->scdb_entry = NULL;
			}
			__network_hash_unlink(f);
			kfree(f);
			f = g;
		}
	}
	spin_unlock_bh(&priv->br_ext_lock);
}

void nat25_db_expire(struct adapter *priv)
{
	int i;

	spin_lock_bh(&priv->br_ext_lock);

	for (i = 0; i < NAT25_HASH_SIZE; i++) {
		struct nat25_network_db_entry *f;
		f = priv->nethash[i];

		while (f) {
			struct nat25_network_db_entry *g;
			g = f->next_hash;

			if (__nat25_has_expired(f)) {
				if (atomic_dec_and_test(&f->use_count)) {
					if (priv->scdb_entry == f) {
						memset(priv->scdb_mac, 0, ETH_ALEN);
						memset(priv->scdb_ip, 0, 4);
						priv->scdb_entry = NULL;
					}
					__network_hash_unlink(f);
					kfree(f);
				}
			}
			f = g;
		}
	}
	spin_unlock_bh(&priv->br_ext_lock);
}

int nat25_db_handle(struct adapter *priv, struct sk_buff *skb, int method)
{
	unsigned short protocol;
	unsigned char networkAddr[MAX_NETWORK_ADDR_LEN];
	unsigned int tmp;

	if (!skb)
		return -1;

	if ((method <= NAT25_MIN) || (method >= NAT25_MAX))
		return -1;

	protocol = be16_to_cpu(*((__be16 *)(skb->data + 2 * ETH_ALEN)));

	/*---------------------------------------------------*/
	/*                 Handle IP frame                   */
	/*---------------------------------------------------*/
	if (protocol == ETH_P_IP) {
		struct iphdr *iph = (struct iphdr *)(skb->data + ETH_HLEN);

		if (((unsigned char *)(iph) + (iph->ihl << 2)) >= (skb->data + ETH_HLEN + skb->len))
			return -1;

		switch (method) {
		case NAT25_CHECK:
			return -1;
		case NAT25_INSERT:
			/* some multicast with source IP is all zero, maybe other case is illegal */
			/* in class A, B, C, host address is all zero or all one is illegal */
			if (iph->saddr == 0)
				return 0;
			tmp = be32_to_cpu(iph->saddr);
			__nat25_generate_ipv4_network_addr(networkAddr, &tmp);
			/* record source IP address and , source mac address into db */
			__nat25_db_network_insert(priv, skb->data+ETH_ALEN, networkAddr);
			return 0;
		default:
			return -1;
		}
	} else if (protocol == ETH_P_ARP) {
		/*---------------------------------------------------*/
		/*                 Handle ARP frame                  */
		/*---------------------------------------------------*/
		struct arphdr *arp = (struct arphdr *)(skb->data + ETH_HLEN);
		unsigned char *arp_ptr = (unsigned char *)(arp + 1);
		unsigned int *sender;

		if (arp->ar_pro != htons(ETH_P_IP))
			return -1;

		switch (method) {
		case NAT25_CHECK:
			return 0;	/*  skb_copy for all ARP frame */
		case NAT25_INSERT:
			/*  change to ARP sender mac address to wlan STA address */
			memcpy(arp_ptr, GET_MY_HWADDR(priv), ETH_ALEN);
			arp_ptr += arp->ar_hln;
			sender = (unsigned int *)arp_ptr;
			__nat25_generate_ipv4_network_addr(networkAddr, sender);
			__nat25_db_network_insert(priv, skb->data+ETH_ALEN, networkAddr);
			return 0;
		default:
			return -1;
		}
	} else if ((protocol == ETH_P_PPP_DISC) ||
		   (protocol == ETH_P_PPP_SES)) {
		/*---------------------------------------------------*/
		/*                Handle PPPoE frame                 */
		/*---------------------------------------------------*/
		struct pppoe_hdr *ph = (struct pppoe_hdr *)(skb->data + ETH_HLEN);
		unsigned short *pMagic;

		switch (method) {
		case NAT25_CHECK:
			if (ph->sid == 0)
				return 0;
			return 1;
		case NAT25_INSERT:
			if (ph->sid == 0) {	/*  Discovery phase according to tag */
				if (ph->code == PADI_CODE || ph->code == PADR_CODE) {
					if (priv->ethBrExtInfo.addPPPoETag) {
						struct pppoe_tag *tag, *pOldTag;
						unsigned char tag_buf[40];
						int old_tag_len = 0;

						tag = (struct pppoe_tag *)tag_buf;
						pOldTag = (struct pppoe_tag *)__nat25_find_pppoe_tag(ph, ntohs(PTT_RELAY_SID));
						if (pOldTag) { /*  if SID existed, copy old value and delete it */
							old_tag_len = ntohs(pOldTag->tag_len);
							if (old_tag_len +
							    TAG_HDR_LEN +
							    MAGIC_CODE_LEN +
							    RTL_RELAY_TAG_LEN >
							    sizeof(tag_buf))
								return -1;

							memcpy(tag->tag_data+MAGIC_CODE_LEN+RTL_RELAY_TAG_LEN,
								pOldTag->tag_data, old_tag_len);

							if (skb_pull_and_merge(skb, (unsigned char *)pOldTag, TAG_HDR_LEN+old_tag_len) < 0)
								return -1;

							ph->length = htons(ntohs(ph->length)-TAG_HDR_LEN-old_tag_len);
						}

						tag->tag_type = PTT_RELAY_SID;
						tag->tag_len = htons(MAGIC_CODE_LEN+RTL_RELAY_TAG_LEN+old_tag_len);

						/*  insert the magic_code+client mac in relay tag */
						pMagic = (unsigned short *)tag->tag_data;
						*pMagic = htons(MAGIC_CODE);
						memcpy(tag->tag_data+MAGIC_CODE_LEN, skb->data+ETH_ALEN, ETH_ALEN);

						/* Add relay tag */
						if (__nat25_add_pppoe_tag(skb, tag) < 0)
							return -1;
					} else { /*  not add relay tag */
						if (priv->pppoe_connection_in_progress &&
						    memcmp(skb->data + ETH_ALEN,
							   priv->pppoe_addr,
							   ETH_ALEN))
							return -2;

						if (priv->pppoe_connection_in_progress == 0)
							memcpy(priv->pppoe_addr, skb->data+ETH_ALEN, ETH_ALEN);

						priv->pppoe_connection_in_progress = WAIT_TIME_PPPOE;
					}
				} else {
					return -1;
				}
			} else {	/*  session phase */
				__nat25_generate_pppoe_network_addr(networkAddr, skb->data, &ph->sid);

				__nat25_db_network_insert(priv, skb->data+ETH_ALEN, networkAddr);

				if (!priv->ethBrExtInfo.addPPPoETag &&
				    priv->pppoe_connection_in_progress &&
				    !memcmp(skb->data+ETH_ALEN, priv->pppoe_addr, ETH_ALEN))
					priv->pppoe_connection_in_progress = 0;
			}
			return 0;
		default:
			return -1;
		}
	} else if (protocol == 0x888e) {
		/*---------------------------------------------------*/
		/*                 Handle EAP frame                  */
		/*---------------------------------------------------*/
		switch (method) {
		case NAT25_CHECK:
			return -1;
		case NAT25_INSERT:
			return 0;
		default:
			return -1;
		}
	} else if ((protocol == 0xe2ae) || (protocol == 0xe2af)) {
		/*---------------------------------------------------*/
		/*         Handle C-Media proprietary frame          */
		/*---------------------------------------------------*/
		switch (method) {
		case NAT25_CHECK:
			return -1;
		case NAT25_INSERT:
			return 0;
		default:
			return -1;
		}
	} else if (protocol == ETH_P_IPV6) {
		/*------------------------------------------------*/
		/*         Handle IPV6 frame			  */
		/*------------------------------------------------*/
		struct ipv6hdr *iph = (struct ipv6hdr *)(skb->data + ETH_HLEN);

		if (sizeof(*iph) >= (skb->len - ETH_HLEN))
			return -1;

		switch (method) {
		case NAT25_CHECK:
			if (skb->data[0] & 1)
				return 0;
			return -1;
		case NAT25_INSERT:
			if (memcmp(&iph->saddr, "\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0", 16)) {
				__nat25_generate_ipv6_network_addr(networkAddr, (unsigned int *)&iph->saddr);
				__nat25_db_network_insert(priv, skb->data+ETH_ALEN, networkAddr);

				if (iph->nexthdr == IPPROTO_ICMPV6 &&
						skb->len > (ETH_HLEN +  sizeof(*iph) + 4)) {
					if (update_nd_link_layer_addr(skb->data + ETH_HLEN + sizeof(*iph),
								      skb->len - ETH_HLEN - sizeof(*iph), GET_MY_HWADDR(priv))) {
						struct icmp6hdr  *hdr = (struct icmp6hdr *)(skb->data + ETH_HLEN + sizeof(*iph));
						hdr->icmp6_cksum = 0;
						hdr->icmp6_cksum = csum_ipv6_magic(&iph->saddr, &iph->daddr,
										iph->payload_len,
										IPPROTO_ICMPV6,
										csum_partial((__u8 *)hdr, iph->payload_len, 0));
					}
				}
			}
			return 0;
		default:
			return -1;
		}
	}
	return -1;
}

#define SERVER_PORT			67
#define CLIENT_PORT			68
#define DHCP_MAGIC			0x63825363
#define BROADCAST_FLAG		0x8000

struct dhcpMessage {
	u_int8_t op;
	u_int8_t htype;
	u_int8_t hlen;
	u_int8_t hops;
	u_int32_t xid;
	__be16 secs;
	__be16 flags;
	__be32 ciaddr;
	__be32 yiaddr;
	__be32 siaddr;
	__be32 giaddr;
	u_int8_t chaddr[16];
	u_int8_t sname[64];
	u_int8_t file[128];
	__be32 cookie;
	u_int8_t options[308]; /* 312 - cookie */
};

void dhcp_flag_bcast(struct adapter *priv, struct sk_buff *skb)
{
	if (!skb)
		return;

	if (!priv->ethBrExtInfo.dhcp_bcst_disable) {
		__be16 protocol = *((__be16 *)(skb->data + 2 * ETH_ALEN));

		if (protocol == __constant_htons(ETH_P_IP)) { /*  IP */
			struct iphdr *iph = (struct iphdr *)(skb->data + ETH_HLEN);

			if (iph->protocol == IPPROTO_UDP) { /*  UDP */
				struct udphdr *udph = (struct udphdr *)((size_t)iph + (iph->ihl << 2));

				if ((udph->source == __constant_htons(CLIENT_PORT)) &&
				    (udph->dest == __constant_htons(SERVER_PORT))) { /*  DHCP request */
					struct dhcpMessage *dhcph =
						(struct dhcpMessage *)((size_t)udph + sizeof(struct udphdr));
					u32 cookie = be32_to_cpu((__be32)dhcph->cookie);

					if (cookie == DHCP_MAGIC) { /*  match magic word */
						if (!(dhcph->flags & htons(BROADCAST_FLAG))) {
							/*  if not broadcast */
							register int sum = 0;

							/*  or BROADCAST flag */
							dhcph->flags |= htons(BROADCAST_FLAG);
							/*  recalculate checksum */
							sum = ~(udph->check) & 0xffff;
							sum += be16_to_cpu(dhcph->flags);
							while (sum >> 16)
								sum = (sum & 0xffff) + (sum >> 16);
							udph->check = ~sum;
						}
					}
				}
			}
		}
	}
}

void *scdb_findEntry(struct adapter *priv, unsigned char *ipAddr)
{
	unsigned char networkAddr[MAX_NETWORK_ADDR_LEN];
	struct nat25_network_db_entry *db;
	int hash;

	__nat25_generate_ipv4_network_addr(networkAddr, (unsigned int *)ipAddr);
	hash = __nat25_network_hash(networkAddr);
	db = priv->nethash[hash];
	while (db) {
		if (!memcmp(db->networkAddr, networkAddr, MAX_NETWORK_ADDR_LEN)) {
			return (void *)db;
		}

		db = db->next_hash;
	}

	return NULL;
}
