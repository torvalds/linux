/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTW_BR_EXT_C_

#include <linux/if_arp.h>
#include <net/ip.h>
#include <net/ipx.h>
#include <linux/atalk.h>
#include <linux/udp.h>
#include <linux/if_pppox.h>

#include <drv_types.h>
#include "rtw_br_ext.h"
#include <usb_osintf.h>
#include <recv_osdep.h>

#ifndef csum_ipv6_magic
#include <net/ip6_checksum.h>
#endif

#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <net/ndisc.h>
#include <net/checksum.h>

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
static inline unsigned char *__nat25_find_pppoe_tag(struct pppoe_hdr *ph, unsigned short type)
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


static inline int __nat25_add_pppoe_tag(struct sk_buff *skb, struct pppoe_tag *tag)
{
	struct pppoe_hdr *ph = (struct pppoe_hdr *)(skb->data + ETH_HLEN);
	int data_len;

	data_len = be16_to_cpu(tag->tag_len) + TAG_HDR_LEN;
	if (skb_tailroom(skb) < data_len) {
		_DEBUG_ERR("skb_tailroom() failed in add SID tag!\n");
		return -1;
	}

	skb_put(skb, data_len);
	/*  have a room for new tag */
	memmove(((unsigned char *)ph->tag + data_len), (unsigned char *)ph->tag, ntohs(ph->length));
	ph->length = htons(ntohs(ph->length) + data_len);
	memcpy((unsigned char *)ph->tag, tag, data_len);
	return data_len;
}

static inline unsigned long __nat25_timeout(struct adapter *priv)
{
	unsigned long timeout;

	timeout = jiffies - NAT25_AGEING_TIME*HZ;

	return timeout;
}


static inline int  __nat25_has_expired(struct adapter *priv,
				struct nat25_network_db_entry *fdb)
{
	if (time_before_eq(fdb->ageing_timer, __nat25_timeout(priv)))
		return 1;

	return 0;
}


static inline void __nat25_generate_ipv4_network_addr(unsigned char *networkAddr,
				unsigned int *ipAddr)
{
	memset(networkAddr, 0, MAX_NETWORK_ADDR_LEN);

	networkAddr[0] = NAT25_IPV4;
	memcpy(networkAddr+7, (unsigned char *)ipAddr, 4);
}


static inline void __nat25_generate_ipx_network_addr_with_node(unsigned char *networkAddr,
				__be32 *ipxNetAddr, unsigned char *ipxNodeAddr)
{
	memset(networkAddr, 0, MAX_NETWORK_ADDR_LEN);

	networkAddr[0] = NAT25_IPX;
	memcpy(networkAddr+1, (unsigned char *)ipxNetAddr, 4);
	memcpy(networkAddr+5, ipxNodeAddr, 6);
}


static inline void __nat25_generate_ipx_network_addr_with_socket(unsigned char *networkAddr,
				__be32 *ipxNetAddr, __be16 *ipxSocketAddr)
{
	memset(networkAddr, 0, MAX_NETWORK_ADDR_LEN);

	networkAddr[0] = NAT25_IPX;
	memcpy(networkAddr+1, (unsigned char *)ipxNetAddr, 4);
	memcpy(networkAddr+5, (unsigned char *)ipxSocketAddr, 2);
}


static inline void __nat25_generate_apple_network_addr(unsigned char *networkAddr,
				__be16 *network, unsigned char *node)
{
	memset(networkAddr, 0, MAX_NETWORK_ADDR_LEN);

	networkAddr[0] = NAT25_APPLE;
	memcpy(networkAddr+1, (unsigned char *)network, 2);
	networkAddr[3] = *node;
}

static inline void __nat25_generate_pppoe_network_addr(unsigned char *networkAddr,
				unsigned char *ac_mac, __be16 *sid)
{
	memset(networkAddr, 0, MAX_NETWORK_ADDR_LEN);

	networkAddr[0] = NAT25_PPPOE;
	memcpy(networkAddr+1, (unsigned char *)sid, 2);
	memcpy(networkAddr+3, (unsigned char *)ac_mac, 6);
}

static inline int __nat25_network_hash(unsigned char *networkAddr)
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

static inline void __network_hash_link(struct adapter *priv,
				struct nat25_network_db_entry *ent, int hash)
{
	/*  Caller must spin_lock_bh already! */
	ent->next_hash = priv->nethash[hash];
	if (ent->next_hash != NULL)
		ent->next_hash->pprev_hash = &ent->next_hash;
	priv->nethash[hash] = ent;
	ent->pprev_hash = &priv->nethash[hash];
}

static inline void __network_hash_unlink(struct nat25_network_db_entry *ent)
{
	/*  Caller must spin_lock_bh already! */
	*(ent->pprev_hash) = ent->next_hash;
	if (ent->next_hash != NULL)
		ent->next_hash->pprev_hash = ent->pprev_hash;
	ent->next_hash = NULL;
	ent->pprev_hash = NULL;
}

/*
 *	NAT2.5 interface
 */

void nat25_db_expire(struct adapter *priv)
{
	int i;
	spin_lock_bh(&priv->br_ext_lock);

	for (i = 0; i < NAT25_HASH_SIZE; i++) {
		struct nat25_network_db_entry *f;
		f = priv->nethash[i];

		while (f != NULL) {
			struct nat25_network_db_entry *g;
			g = f->next_hash;

			if (__nat25_has_expired(priv, f)) {
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

#define SERVER_PORT			67
#define CLIENT_PORT			68
#define DHCP_MAGIC			0x63825363
#define BROADCAST_FLAG		0x8000

struct dhcpMessage {
	u_int8_t op;
	u_int8_t htype;
	u_int8_t hlen;
	u_int8_t hops;
	__be32 xid;
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
	if (skb == NULL)
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

							DEBUG_INFO("DHCP: change flag of DHCP request to broadcast.\n");
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


void *scdb_findEntry(struct adapter *priv, unsigned char *macAddr,
				unsigned char *ipAddr)
{
	unsigned char networkAddr[MAX_NETWORK_ADDR_LEN];
	struct nat25_network_db_entry *db;
	int hash;

	__nat25_generate_ipv4_network_addr(networkAddr, (unsigned int *)ipAddr);
	hash = __nat25_network_hash(networkAddr);
	db = priv->nethash[hash];
	while (db != NULL) {
		if (!memcmp(db->networkAddr, networkAddr, MAX_NETWORK_ADDR_LEN))
			return (void *)db;

		db = db->next_hash;
	}

	return NULL;
}
