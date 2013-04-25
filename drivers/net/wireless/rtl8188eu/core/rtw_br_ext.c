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

#ifdef __KERNEL__
#include <linux/if_arp.h>
#include <net/ip.h>
#include <net/ipx.h>
#include <linux/atalk.h>
#include <linux/udp.h>
#include <linux/if_pppox.h>
#endif

#if 1	// rtw_wifi_driver
#include <drv_conf.h>
#include <drv_types.h>
#include "rtw_br_ext.h"
#else	// rtw_wifi_driver
#include "./8192cd_cfg.h"

#ifndef __KERNEL__
#include "./sys-support.h"
#endif

#include "./8192cd.h"
#include "./8192cd_headers.h"
#include "./8192cd_br_ext.h"
#include "./8192cd_debug.h"
#endif	// rtw_wifi_driver

#ifdef CL_IPV6_PASS
#ifdef __KERNEL__
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <net/ndisc.h>
#include <net/checksum.h>
#endif
#endif

#ifdef CONFIG_BR_EXT

//#define BR_EXT_DEBUG

#define NAT25_IPV4		01
#define NAT25_IPV6		02
#define NAT25_IPX		03
#define NAT25_APPLE		04
#define NAT25_PPPOE		05

#define RTL_RELAY_TAG_LEN (ETH_ALEN)
#define TAG_HDR_LEN		4

#define MAGIC_CODE		0x8186
#define MAGIC_CODE_LEN	2
#define WAIT_TIME_PPPOE	5	// waiting time for pppoe server in sec

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


//Find a tag in pppoe frame and return the pointer
static __inline__ unsigned char *__nat25_find_pppoe_tag(struct pppoe_hdr *ph, unsigned short type)
{
	unsigned char *cur_ptr, *start_ptr;
	unsigned short tagLen, tagType;

	start_ptr = cur_ptr = (unsigned char *)ph->tag;
	while((cur_ptr - start_ptr) < ntohs(ph->length)) {
		// prevent un-alignment access
		tagType = (unsigned short)((cur_ptr[0] << 8) + cur_ptr[1]);
		tagLen  = (unsigned short)((cur_ptr[2] << 8) + cur_ptr[3]);
		if(tagType == type)
			return cur_ptr;
		cur_ptr = cur_ptr + TAG_HDR_LEN + tagLen;
	}
	return 0;
}


static __inline__ int __nat25_add_pppoe_tag(struct sk_buff *skb, struct pppoe_tag *tag)
{
	struct pppoe_hdr *ph = (struct pppoe_hdr *)(skb->data + ETH_HLEN);
	int data_len;

	data_len = tag->tag_len + TAG_HDR_LEN;
	if (skb_tailroom(skb) < data_len) {
		_DEBUG_ERR("skb_tailroom() failed in add SID tag!\n");
		return -1;
	}

	skb_put(skb, data_len);
	// have a room for new tag
	memmove(((unsigned char *)ph->tag + data_len), (unsigned char *)ph->tag, ntohs(ph->length));
	ph->length = htons(ntohs(ph->length) + data_len);
	memcpy((unsigned char *)ph->tag, tag, data_len);
	return data_len;
}

static int skb_pull_and_merge(struct sk_buff *skb, unsigned char *src, int len)
{
	int tail_len;
	unsigned long end, tail;

	if ((src+len) > skb->tail || skb->len < len)
		return -1;

	tail = (unsigned long)skb->tail;
	end = (unsigned long)src+len;
	if (tail < end)
		return -1;

	tail_len = (int)(tail-end);
	if (tail_len > 0)
		memmove(src, src+len, tail_len);

	skb_trim(skb, skb->len-len);
	return 0;
}

static __inline__ unsigned long __nat25_timeout(_adapter *priv)
{
	unsigned long timeout;

	timeout = jiffies - NAT25_AGEING_TIME*HZ;

	return timeout;
}


static __inline__ int  __nat25_has_expired(_adapter *priv,
				struct nat25_network_db_entry *fdb)
{
	if(time_before_eq(fdb->ageing_timer, __nat25_timeout(priv)))
		return 1;

	return 0;
}


static __inline__ void __nat25_generate_ipv4_network_addr(unsigned char *networkAddr,
				unsigned int *ipAddr)
{
	memset(networkAddr, 0, MAX_NETWORK_ADDR_LEN);

	networkAddr[0] = NAT25_IPV4;
	memcpy(networkAddr+7, (unsigned char *)ipAddr, 4);
}


static __inline__ void __nat25_generate_ipx_network_addr_with_node(unsigned char *networkAddr,
				unsigned int *ipxNetAddr, unsigned char *ipxNodeAddr)
{
	memset(networkAddr, 0, MAX_NETWORK_ADDR_LEN);

	networkAddr[0] = NAT25_IPX;
	memcpy(networkAddr+1, (unsigned char *)ipxNetAddr, 4);
	memcpy(networkAddr+5, ipxNodeAddr, 6);
}


static __inline__ void __nat25_generate_ipx_network_addr_with_socket(unsigned char *networkAddr,
				unsigned int *ipxNetAddr, unsigned short *ipxSocketAddr)
{
	memset(networkAddr, 0, MAX_NETWORK_ADDR_LEN);

	networkAddr[0] = NAT25_IPX;
	memcpy(networkAddr+1, (unsigned char *)ipxNetAddr, 4);
	memcpy(networkAddr+5, (unsigned char *)ipxSocketAddr, 2);
}


static __inline__ void __nat25_generate_apple_network_addr(unsigned char *networkAddr,
				unsigned short *network, unsigned char *node)
{
	memset(networkAddr, 0, MAX_NETWORK_ADDR_LEN);

	networkAddr[0] = NAT25_APPLE;
	memcpy(networkAddr+1, (unsigned char *)network, 2);
	networkAddr[3] = *node;
}


static __inline__ void __nat25_generate_pppoe_network_addr(unsigned char *networkAddr,
				unsigned char *ac_mac, unsigned short *sid)
{
	memset(networkAddr, 0, MAX_NETWORK_ADDR_LEN);

	networkAddr[0] = NAT25_PPPOE;
	memcpy(networkAddr+1, (unsigned char *)sid, 2);
	memcpy(networkAddr+3, (unsigned char *)ac_mac, 6);
}


#ifdef CL_IPV6_PASS
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
				_DEBUG_INFO("Router Solicitation, replace MAC From: %02x:%02x:%02x:%02x:%02x:%02x, To: %02x:%02x:%02x:%02x:%02x:%02x\n",
					mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],					
					replace_mac[0],replace_mac[1],replace_mac[2],replace_mac[3],replace_mac[4],replace_mac[5]);
				memcpy(mac, replace_mac, 6);	
				return 1;				
			}
		}
	}
	else if (icmphdr->icmp6_type == NDISC_ROUTER_ADVERTISEMENT) {
		if (len >= 16) {
			mac = scan_tlv(&data[16], len-16, 1, 1);
			if (mac) {
				_DEBUG_INFO("Router Advertisement, replace MAC From: %02x:%02x:%02x:%02x:%02x:%02x, To: %02x:%02x:%02x:%02x:%02x:%02x\n",
					mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],					
					replace_mac[0],replace_mac[1],replace_mac[2],replace_mac[3],replace_mac[4],replace_mac[5]);
				memcpy(mac, replace_mac, 6);			
				return 1;				
			}
		}		
	}
	else if (icmphdr->icmp6_type == NDISC_NEIGHBOUR_SOLICITATION) {
		if (len >= 24) {
			mac = scan_tlv(&data[24], len-24, 1, 1);
			if (mac) {		
				_DEBUG_INFO("Neighbor Solicitation, replace MAC From: %02x:%02x:%02x:%02x:%02x:%02x, To: %02x:%02x:%02x:%02x:%02x:%02x\n",
					mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],					
					replace_mac[0],replace_mac[1],replace_mac[2],replace_mac[3],replace_mac[4],replace_mac[5]);
				memcpy(mac, replace_mac, 6);	
				return 1;								
			}
		}		
	}
	else if (icmphdr->icmp6_type == NDISC_NEIGHBOUR_ADVERTISEMENT) {
		if (len >= 24) {
			mac = scan_tlv(&data[24], len-24, 2, 1);
			if (mac) {
				_DEBUG_INFO("Neighbor Advertisement, replace MAC From: %02x:%02x:%02x:%02x:%02x:%02x, To: %02x:%02x:%02x:%02x:%02x:%02x\n",
					mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],					
					replace_mac[0],replace_mac[1],replace_mac[2],replace_mac[3],replace_mac[4],replace_mac[5]);
				memcpy(mac, replace_mac, 6);		
				return 1;				
			}
		}		
	}
	else if (icmphdr->icmp6_type == NDISC_REDIRECT) {
		if (len >= 40) {
			mac = scan_tlv(&data[40], len-40, 2, 1);
			if (mac) {				
				_DEBUG_INFO("Redirect,  replace MAC From: %02x:%02x:%02x:%02x:%02x:%02x, To: %02x:%02x:%02x:%02x:%02x:%02x\n",
					mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],					
					replace_mac[0],replace_mac[1],replace_mac[2],replace_mac[3],replace_mac[4],replace_mac[5]);
				memcpy(mac, replace_mac, 6);	
				return 1;				
			}
		}		
	}	
	return 0;
}


static void convert_ipv6_mac_to_mc(struct sk_buff *skb)
{	
	struct ipv6hdr *iph = (struct ipv6hdr *)(skb->data + ETH_HLEN);
	unsigned char *dst_mac = skb->data;

	//dst_mac[0] = 0xff;
	//dst_mac[1] = 0xff;
	/*modified by qinjunjie,ipv6 multicast address ix 0x33-33-xx-xx-xx-xx*/
	dst_mac[0] = 0x33;
	dst_mac[1] = 0x33;
	memcpy(&dst_mac[2], &iph->daddr.s6_addr32[3], 4);
	#if defined(__LINUX_2_6__) 
	/*modified by qinjunjie,warning:should not remove next line*/
	skb->pkt_type = PACKET_MULTICAST;
	#endif
}
#endif /* CL_IPV6_PASS */


static __inline__ int __nat25_network_hash(unsigned char *networkAddr)
{
	if(networkAddr[0] == NAT25_IPV4)
	{
		unsigned long x;

		x = networkAddr[7] ^ networkAddr[8] ^ networkAddr[9] ^ networkAddr[10];

		return x & (NAT25_HASH_SIZE - 1);
	}
	else if(networkAddr[0] == NAT25_IPX)
	{
		unsigned long x;

		x = networkAddr[1] ^ networkAddr[2] ^ networkAddr[3] ^ networkAddr[4] ^ networkAddr[5] ^
			networkAddr[6] ^ networkAddr[7] ^ networkAddr[8] ^ networkAddr[9] ^ networkAddr[10];

		return x & (NAT25_HASH_SIZE - 1);
	}
	else if(networkAddr[0] == NAT25_APPLE)
	{
		unsigned long x;

		x = networkAddr[1] ^ networkAddr[2] ^ networkAddr[3];

		return x & (NAT25_HASH_SIZE - 1);
	}
	else if(networkAddr[0] == NAT25_PPPOE)
	{
		unsigned long x;

		x = networkAddr[0] ^ networkAddr[1] ^ networkAddr[2] ^ networkAddr[3] ^ networkAddr[4] ^ networkAddr[5] ^ networkAddr[6] ^ networkAddr[7] ^ networkAddr[8];

		return x & (NAT25_HASH_SIZE - 1);
	}
#ifdef CL_IPV6_PASS
	else if(networkAddr[0] == NAT25_IPV6)
	{
		unsigned long x;

		x = networkAddr[1] ^ networkAddr[2] ^ networkAddr[3] ^ networkAddr[4] ^ networkAddr[5] ^
			networkAddr[6] ^ networkAddr[7] ^ networkAddr[8] ^ networkAddr[9] ^ networkAddr[10] ^
			networkAddr[11] ^ networkAddr[12] ^ networkAddr[13] ^ networkAddr[14] ^ networkAddr[15] ^
			networkAddr[16];
	
		return x & (NAT25_HASH_SIZE - 1);
	}
#endif	
	else
	{
		unsigned long x = 0;
		int i;

		for (i=0; i<MAX_NETWORK_ADDR_LEN; i++)
			x ^= networkAddr[i];

		return x & (NAT25_HASH_SIZE - 1);
	}
}


static __inline__ void __network_hash_link(_adapter *priv,
				struct nat25_network_db_entry *ent, int hash)
{
	// Caller must _enter_critical_bh already!
	//_irqL irqL;
	//_enter_critical_bh(&priv->br_ext_lock, &irqL);

	ent->next_hash = priv->nethash[hash];
	if(ent->next_hash != NULL)
		ent->next_hash->pprev_hash = &ent->next_hash;
	priv->nethash[hash] = ent;
	ent->pprev_hash = &priv->nethash[hash];

	//_exit_critical_bh(&priv->br_ext_lock, &irqL);
}


static __inline__ void __network_hash_unlink(struct nat25_network_db_entry *ent)
{
	// Caller must _enter_critical_bh already!
	//_irqL irqL;
	//_enter_critical_bh(&priv->br_ext_lock, &irqL);

	*(ent->pprev_hash) = ent->next_hash;
	if(ent->next_hash != NULL)
		ent->next_hash->pprev_hash = ent->pprev_hash;
	ent->next_hash = NULL;
	ent->pprev_hash = NULL;

	//_exit_critical_bh(&priv->br_ext_lock, &irqL);
}


static int __nat25_db_network_lookup_and_replace(_adapter *priv,
				struct sk_buff *skb, unsigned char *networkAddr)
{
	struct nat25_network_db_entry *db;
	_irqL irqL;
	_enter_critical_bh(&priv->br_ext_lock, &irqL);

	db = priv->nethash[__nat25_network_hash(networkAddr)];
	while (db != NULL)
	{
		if(!memcmp(db->networkAddr, networkAddr, MAX_NETWORK_ADDR_LEN))
		{
			if(!__nat25_has_expired(priv, db))
			{
				// replace the destination mac address
				memcpy(skb->data, db->macAddr, ETH_ALEN);
				atomic_inc(&db->use_count);

#ifdef CL_IPV6_PASS
				DEBUG_INFO("NAT25: Lookup M:%02x%02x%02x%02x%02x%02x N:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"	
							"%02x%02x%02x%02x%02x%02x\n",				
					db->macAddr[0],
					db->macAddr[1],
					db->macAddr[2],
					db->macAddr[3],
					db->macAddr[4],
					db->macAddr[5],
					db->networkAddr[0],
					db->networkAddr[1],
					db->networkAddr[2],
					db->networkAddr[3],
					db->networkAddr[4],
					db->networkAddr[5],
					db->networkAddr[6],
					db->networkAddr[7],
					db->networkAddr[8],
					db->networkAddr[9],
					db->networkAddr[10],
					db->networkAddr[11],
					db->networkAddr[12],
					db->networkAddr[13],
					db->networkAddr[14],
					db->networkAddr[15],
					db->networkAddr[16]);
#else				
				DEBUG_INFO("NAT25: Lookup M:%02x%02x%02x%02x%02x%02x N:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
					db->macAddr[0],
					db->macAddr[1],
					db->macAddr[2],
					db->macAddr[3],
					db->macAddr[4],
					db->macAddr[5],
					db->networkAddr[0],
					db->networkAddr[1],
					db->networkAddr[2],
					db->networkAddr[3],
					db->networkAddr[4],
					db->networkAddr[5],
					db->networkAddr[6],
					db->networkAddr[7],
					db->networkAddr[8],
					db->networkAddr[9],
					db->networkAddr[10]);
#endif
			}
			_exit_critical_bh(&priv->br_ext_lock, &irqL);
			return 1;
		}

		db = db->next_hash;
	}

	_exit_critical_bh(&priv->br_ext_lock, &irqL);
	return 0;
}


static void __nat25_db_network_insert(_adapter *priv,
				unsigned char *macAddr, unsigned char *networkAddr)
{
	struct nat25_network_db_entry *db;
	int hash;
	_irqL irqL;
	_enter_critical_bh(&priv->br_ext_lock, &irqL);

	hash = __nat25_network_hash(networkAddr);
	db = priv->nethash[hash];
	while (db != NULL)
	{
		if(!memcmp(db->networkAddr, networkAddr, MAX_NETWORK_ADDR_LEN))
		{
			memcpy(db->macAddr, macAddr, ETH_ALEN);
			db->ageing_timer = jiffies;
			_exit_critical_bh(&priv->br_ext_lock, &irqL);
			return;
		}

		db = db->next_hash;
	}

	db = (struct nat25_network_db_entry *) rtw_malloc(sizeof(*db));
	if(db == NULL) {
		_exit_critical_bh(&priv->br_ext_lock, &irqL);
		return;
	}

	memcpy(db->networkAddr, networkAddr, MAX_NETWORK_ADDR_LEN);
	memcpy(db->macAddr, macAddr, ETH_ALEN);
	atomic_set(&db->use_count, 1);
	db->ageing_timer = jiffies;

	__network_hash_link(priv, db, hash);

	_exit_critical_bh(&priv->br_ext_lock, &irqL);
}


static void __nat25_db_print(_adapter *priv)
{
	_irqL irqL;
	_enter_critical_bh(&priv->br_ext_lock, &irqL);

#ifdef BR_EXT_DEBUG
	static int counter = 0;
	int i, j;
	struct nat25_network_db_entry *db;

	counter++;
	if((counter % 16) != 0)
		return;

	for(i=0, j=0; i<NAT25_HASH_SIZE; i++)
	{
		db = priv->nethash[i];

		while (db != NULL)
		{
#ifdef CL_IPV6_PASS
			panic_printk("NAT25: DB(%d) H(%02d) C(%d) M:%02x%02x%02x%02x%02x%02x N:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
					"%02x%02x%02x%02x%02x%02x\n",
				j,
				i,
				atomic_read(&db->use_count),
				db->macAddr[0],
				db->macAddr[1],
				db->macAddr[2],
				db->macAddr[3],
				db->macAddr[4],
				db->macAddr[5],
				db->networkAddr[0],
				db->networkAddr[1],
				db->networkAddr[2],
				db->networkAddr[3],
				db->networkAddr[4],
				db->networkAddr[5],
				db->networkAddr[6],
				db->networkAddr[7],
				db->networkAddr[8],
				db->networkAddr[9],
				db->networkAddr[10],
				db->networkAddr[11],
				db->networkAddr[12],
				db->networkAddr[13],
				db->networkAddr[14],
				db->networkAddr[15],
				db->networkAddr[16]);
#else
			panic_printk("NAT25: DB(%d) H(%02d) C(%d) M:%02x%02x%02x%02x%02x%02x N:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
				j,
				i,
				atomic_read(&db->use_count),
				db->macAddr[0],
				db->macAddr[1],
				db->macAddr[2],
				db->macAddr[3],
				db->macAddr[4],
				db->macAddr[5],
				db->networkAddr[0],
				db->networkAddr[1],
				db->networkAddr[2],
				db->networkAddr[3],
				db->networkAddr[4],
				db->networkAddr[5],
				db->networkAddr[6],
				db->networkAddr[7],
				db->networkAddr[8],
				db->networkAddr[9],
				db->networkAddr[10]);
#endif
			j++;

			db = db->next_hash;
		}
	}
#endif

	_exit_critical_bh(&priv->br_ext_lock, &irqL);
}




/*
 *	NAT2.5 interface
 */

void nat25_db_cleanup(_adapter *priv)
{
	int i;
	_irqL irqL;
	_enter_critical_bh(&priv->br_ext_lock, &irqL);
	
	for(i=0; i<NAT25_HASH_SIZE; i++)
	{
		struct nat25_network_db_entry *f;
		f = priv->nethash[i];
		while (f != NULL) {
			struct nat25_network_db_entry *g;

			g = f->next_hash;
			if(priv->scdb_entry == f)
			{
				memset(priv->scdb_mac, 0, ETH_ALEN);
				memset(priv->scdb_ip, 0, 4);
				priv->scdb_entry = NULL;
			}
			__network_hash_unlink(f);
			rtw_mfree((u8 *) f, sizeof(struct nat25_network_db_entry));

			f = g;
		}
	}

	_exit_critical_bh(&priv->br_ext_lock, &irqL);
}


void nat25_db_expire(_adapter *priv)
{
	int i;
	_irqL irqL;
	_enter_critical_bh(&priv->br_ext_lock, &irqL);
	
	//if(!priv->ethBrExtInfo.nat25_disable)
	{
		for (i=0; i<NAT25_HASH_SIZE; i++)
		{
			struct nat25_network_db_entry *f;
			f = priv->nethash[i];

			while (f != NULL)
			{
				struct nat25_network_db_entry *g;
				g = f->next_hash;

				if(__nat25_has_expired(priv, f))
				{
					if(atomic_dec_and_test(&f->use_count))
					{
#ifdef BR_EXT_DEBUG
#ifdef CL_IPV6_PASS
						panic_printk("NAT25 Expire H(%02d) M:%02x%02x%02x%02x%02x%02x N:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
								"%02x%02x%02x%02x%02x%02x\n",
							i,
							f->macAddr[0],
							f->macAddr[1],
							f->macAddr[2],
							f->macAddr[3],
							f->macAddr[4],
							f->macAddr[5],
							f->networkAddr[0],
							f->networkAddr[1],
							f->networkAddr[2],
							f->networkAddr[3],
							f->networkAddr[4],
							f->networkAddr[5],
							f->networkAddr[6],
							f->networkAddr[7],
							f->networkAddr[8],
							f->networkAddr[9],
							f->networkAddr[10],
							f->networkAddr[11],
							f->networkAddr[12],
							f->networkAddr[13],
							f->networkAddr[14],
							f->networkAddr[15],
							f->networkAddr[16]);
#else

						panic_printk("NAT25 Expire H(%02d) M:%02x%02x%02x%02x%02x%02x N:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
							i,
							f->macAddr[0],
							f->macAddr[1],
							f->macAddr[2],
							f->macAddr[3],
							f->macAddr[4],
							f->macAddr[5],
							f->networkAddr[0],
							f->networkAddr[1],
							f->networkAddr[2],
							f->networkAddr[3],
							f->networkAddr[4],
							f->networkAddr[5],
							f->networkAddr[6],
							f->networkAddr[7],
							f->networkAddr[8],
							f->networkAddr[9],
							f->networkAddr[10]);
#endif
#endif
						if(priv->scdb_entry == f)
						{
							memset(priv->scdb_mac, 0, ETH_ALEN);
							memset(priv->scdb_ip, 0, 4);
							priv->scdb_entry = NULL;
						}
						__network_hash_unlink(f);
						rtw_mfree((u8 *) f, sizeof(struct nat25_network_db_entry));
					}
				}

				f = g;
			}
		}
	}

	_exit_critical_bh(&priv->br_ext_lock, &irqL);
}


#ifdef SUPPORT_TX_MCAST2UNI
static int checkIPMcAndReplace(_adapter *priv, struct sk_buff *skb, unsigned int *dst_ip)
{
	struct stat_info	*pstat;
	struct list_head	*phead, *plist;
	int i;

	phead = &priv->asoc_list;
	plist = phead->next;

	while (plist != phead) {
		pstat = list_entry(plist, struct stat_info, asoc_list);
		plist = plist->next;

		if (pstat->ipmc_num == 0)
			continue;

		for (i=0; i<MAX_IP_MC_ENTRY; i++) {
			if (pstat->ipmc[i].used && !memcmp(&pstat->ipmc[i].mcmac[3], ((unsigned char *)dst_ip)+1, 3)) {
				memcpy(skb->data, pstat->ipmc[i].mcmac, ETH_ALEN);
				return 1;
			}
		}
	}
	return 0;
}
#endif

int nat25_db_handle(_adapter *priv, struct sk_buff *skb, int method)
{
	unsigned short protocol;
	unsigned char networkAddr[MAX_NETWORK_ADDR_LEN];

	if(skb == NULL)
		return -1;

	if((method <= NAT25_MIN) || (method >= NAT25_MAX))
		return -1;

	protocol = *((unsigned short *)(skb->data + 2 * ETH_ALEN));

	/*---------------------------------------------------*/
	/*                 Handle IP frame                   */
	/*---------------------------------------------------*/
	if(protocol == __constant_htons(ETH_P_IP))
	{
		struct iphdr* iph = (struct iphdr *)(skb->data + ETH_HLEN);

		if(((unsigned char*)(iph) + (iph->ihl<<2)) >= (skb->data + ETH_HLEN + skb->len))
		{
			DEBUG_WARN("NAT25: malformed IP packet !\n");
			return -1;
		}

		switch(method)
		{
			case NAT25_CHECK:
				return -1;

			case NAT25_INSERT:
				{
					//some muticast with source IP is all zero, maybe other case is illegal
					//in class A, B, C, host address is all zero or all one is illegal
					if (iph->saddr == 0)
						return 0;
					DEBUG_INFO("NAT25: Insert IP, SA=%08x, DA=%08x\n", iph->saddr, iph->daddr);
					__nat25_generate_ipv4_network_addr(networkAddr, &iph->saddr);
					//record source IP address and , source mac address into db
					__nat25_db_network_insert(priv, skb->data+ETH_ALEN, networkAddr);

					__nat25_db_print(priv);
				}
				return 0;

			case NAT25_LOOKUP:
				{
					DEBUG_INFO("NAT25: Lookup IP, SA=%08x, DA=%08x\n", iph->saddr, iph->daddr);
#ifdef SUPPORT_TX_MCAST2UNI
					if (priv->pshare->rf_ft_var.mc2u_disable ||
							((((OPMODE & (WIFI_STATION_STATE|WIFI_ASOC_STATE))
							== (WIFI_STATION_STATE|WIFI_ASOC_STATE)) &&
							!checkIPMcAndReplace(priv, skb, &iph->daddr)) ||
							(OPMODE & WIFI_ADHOC_STATE)))
#endif
					{
						__nat25_generate_ipv4_network_addr(networkAddr, &iph->daddr);

						if (!__nat25_db_network_lookup_and_replace(priv, skb, networkAddr)) {
							if (*((unsigned char *)&iph->daddr + 3) == 0xff) {
								// L2 is unicast but L3 is broadcast, make L2 bacome broadcast
								DEBUG_INFO("NAT25: Set DA as boardcast\n");
								memset(skb->data, 0xff, ETH_ALEN);
							}
							else {
								// forward unknow IP packet to upper TCP/IP
								DEBUG_INFO("NAT25: Replace DA with BR's MAC\n");
								memcpy(skb->data, priv->br_mac, ETH_ALEN);
							}
						}
					}
				}
				return 0;

			default:
				return -1;
		}
	}

	/*---------------------------------------------------*/
	/*                 Handle ARP frame                  */
	/*---------------------------------------------------*/
	else if(protocol == __constant_htons(ETH_P_ARP))
	{
		struct arphdr *arp = (struct arphdr *)(skb->data + ETH_HLEN);
		unsigned char *arp_ptr = (unsigned char *)(arp + 1);
		unsigned int *sender, *target;

		if(arp->ar_pro != __constant_htons(ETH_P_IP))
		{
			DEBUG_WARN("NAT25: arp protocol unknown (%4x)!\n", htons(arp->ar_pro));
			return -1;
		}

		switch(method)
		{
			case NAT25_CHECK:
				return 0;	// skb_copy for all ARP frame

			case NAT25_INSERT:
				{
					DEBUG_INFO("NAT25: Insert ARP, MAC=%02x%02x%02x%02x%02x%02x\n", arp_ptr[0],
						arp_ptr[1], arp_ptr[2], arp_ptr[3], arp_ptr[4], arp_ptr[5]);

					// change to ARP sender mac address to wlan STA address
                                        memcpy(arp_ptr, GET_MY_HWADDR(priv), ETH_ALEN);

					arp_ptr += arp->ar_hln;
					sender = (unsigned int *)arp_ptr;

					__nat25_generate_ipv4_network_addr(networkAddr, sender);

					__nat25_db_network_insert(priv, skb->data+ETH_ALEN, networkAddr);

					__nat25_db_print(priv);
				}
				return 0;

			case NAT25_LOOKUP:
				{
					DEBUG_INFO("NAT25: Lookup ARP\n");

					arp_ptr += arp->ar_hln;
					sender = (unsigned int *)arp_ptr;
					arp_ptr += (arp->ar_hln + arp->ar_pln);
					target = (unsigned int *)arp_ptr;

					__nat25_generate_ipv4_network_addr(networkAddr, target);

					__nat25_db_network_lookup_and_replace(priv, skb, networkAddr);

					// change to ARP target mac address to Lookup result
					arp_ptr = (unsigned char *)(arp + 1);
					arp_ptr += (arp->ar_hln + arp->ar_pln);
					memcpy(arp_ptr, skb->data, ETH_ALEN);
				}
				return 0;

			default:
				return -1;
		}
	}

	/*---------------------------------------------------*/
	/*         Handle IPX and Apple Talk frame           */
	/*---------------------------------------------------*/
	else if((protocol == __constant_htons(ETH_P_IPX)) ||
		(protocol <= __constant_htons(ETH_FRAME_LEN)))
	{
		unsigned char ipx_header[2] = {0xFF, 0xFF};
		struct ipxhdr	*ipx = NULL;
		struct elapaarp	*ea = NULL;
		struct ddpehdr	*ddp = NULL;
		unsigned char *framePtr = skb->data + ETH_HLEN;

		if(protocol == __constant_htons(ETH_P_IPX))
		{
			DEBUG_INFO("NAT25: Protocol=IPX (Ethernet II)\n");
			ipx = (struct ipxhdr *)framePtr;
		}
		else if(protocol <= __constant_htons(ETH_FRAME_LEN))
		{
			if(!memcmp(ipx_header, framePtr, 2))
			{
				DEBUG_INFO("NAT25: Protocol=IPX (Ethernet 802.3)\n");
				ipx = (struct ipxhdr *)framePtr;
			}
			else
			{
				unsigned char ipx_8022_type =  0xE0;
				unsigned char snap_8022_type = 0xAA;

				if(*framePtr == snap_8022_type)
				{
					unsigned char ipx_snap_id[5] = {0x0, 0x0, 0x0, 0x81, 0x37};		// IPX SNAP ID
					unsigned char aarp_snap_id[5] = {0x00, 0x00, 0x00, 0x80, 0xF3};	// Apple Talk AARP SNAP ID
					unsigned char ddp_snap_id[5] = {0x08, 0x00, 0x07, 0x80, 0x9B};	// Apple Talk DDP SNAP ID

					framePtr += 3;	// eliminate the 802.2 header

					if(!memcmp(ipx_snap_id, framePtr, 5))
					{
						framePtr += 5;	// eliminate the SNAP header

						DEBUG_INFO("NAT25: Protocol=IPX (Ethernet SNAP)\n");
						ipx = (struct ipxhdr *)framePtr;
					}
					else if(!memcmp(aarp_snap_id, framePtr, 5))
					{
						framePtr += 5;	// eliminate the SNAP header

						ea = (struct elapaarp *)framePtr;
					}
					else if(!memcmp(ddp_snap_id, framePtr, 5))
					{
						framePtr += 5;	// eliminate the SNAP header

						ddp = (struct ddpehdr *)framePtr;
					}
					else
					{
						DEBUG_WARN("NAT25: Protocol=Ethernet SNAP %02x%02x%02x%02x%02x\n", framePtr[0],
							framePtr[1], framePtr[2], framePtr[3], framePtr[4]);
						return -1;
					}
				}
				else if(*framePtr == ipx_8022_type)
				{
					framePtr += 3;	// eliminate the 802.2 header

					if(!memcmp(ipx_header, framePtr, 2))
					{
						DEBUG_INFO("NAT25: Protocol=IPX (Ethernet 802.2)\n");
						ipx = (struct ipxhdr *)framePtr;
					}
					else
						return -1;
				}
				else
					return -1;
			}
		}
		else
			return -1;

		/*   IPX   */
		if(ipx != NULL)
		{
			switch(method)
			{
				case NAT25_CHECK:
					if(!memcmp(skb->data+ETH_ALEN, ipx->ipx_source.node, ETH_ALEN))
					{
						DEBUG_INFO("NAT25: Check IPX skb_copy\n");
						return 0;
					}
					return -1;

				case NAT25_INSERT:
					{
						DEBUG_INFO("NAT25: Insert IPX, Dest=%08x,%02x%02x%02x%02x%02x%02x,%04x Source=%08x,%02x%02x%02x%02x%02x%02x,%04x\n",
							ipx->ipx_dest.net,
							ipx->ipx_dest.node[0],
							ipx->ipx_dest.node[1],
							ipx->ipx_dest.node[2],
							ipx->ipx_dest.node[3],
							ipx->ipx_dest.node[4],
							ipx->ipx_dest.node[5],
							ipx->ipx_dest.sock,
							ipx->ipx_source.net,
							ipx->ipx_source.node[0],
							ipx->ipx_source.node[1],
							ipx->ipx_source.node[2],
							ipx->ipx_source.node[3],
							ipx->ipx_source.node[4],
							ipx->ipx_source.node[5],
							ipx->ipx_source.sock);

						if(!memcmp(skb->data+ETH_ALEN, ipx->ipx_source.node, ETH_ALEN))
						{
							DEBUG_INFO("NAT25: Use IPX Net, and Socket as network addr\n");

							__nat25_generate_ipx_network_addr_with_socket(networkAddr, &ipx->ipx_source.net, &ipx->ipx_source.sock);

							// change IPX source node addr to wlan STA address
                                                        memcpy(ipx->ipx_source.node, GET_MY_HWADDR(priv), ETH_ALEN);
						}
						else
						{
							__nat25_generate_ipx_network_addr_with_node(networkAddr, &ipx->ipx_source.net, ipx->ipx_source.node);
						}

						__nat25_db_network_insert(priv, skb->data+ETH_ALEN, networkAddr);

						__nat25_db_print(priv);
					}
					return 0;

				case NAT25_LOOKUP:
					{
                                                if(!memcmp(GET_MY_HWADDR(priv), ipx->ipx_dest.node, ETH_ALEN))
						{
							DEBUG_INFO("NAT25: Lookup IPX, Modify Destination IPX Node addr\n");

							__nat25_generate_ipx_network_addr_with_socket(networkAddr, &ipx->ipx_dest.net, &ipx->ipx_dest.sock);

							__nat25_db_network_lookup_and_replace(priv, skb, networkAddr);

							// replace IPX destination node addr with Lookup destination MAC addr
							memcpy(ipx->ipx_dest.node, skb->data, ETH_ALEN);
						}
						else
						{
							__nat25_generate_ipx_network_addr_with_node(networkAddr, &ipx->ipx_dest.net, ipx->ipx_dest.node);

							__nat25_db_network_lookup_and_replace(priv, skb, networkAddr);
						}
					}
					return 0;

				default:
					return -1;
			}
		}

		/*   AARP   */
		else if(ea != NULL)
		{
			/* Sanity check fields. */
			if(ea->hw_len != ETH_ALEN || ea->pa_len != AARP_PA_ALEN)
			{
				DEBUG_WARN("NAT25: Appletalk AARP Sanity check fail!\n");
				return -1;
			}

			switch(method)
			{
				case NAT25_CHECK:
					return 0;

				case NAT25_INSERT:
					{
						// change to AARP source mac address to wlan STA address
                                                memcpy(ea->hw_src, GET_MY_HWADDR(priv), ETH_ALEN);

						DEBUG_INFO("NAT25: Insert AARP, Source=%d,%d Destination=%d,%d\n",
							ea->pa_src_net,
							ea->pa_src_node,
							ea->pa_dst_net,
							ea->pa_dst_node);

						__nat25_generate_apple_network_addr(networkAddr, &ea->pa_src_net, &ea->pa_src_node);

						__nat25_db_network_insert(priv, skb->data+ETH_ALEN, networkAddr);

						__nat25_db_print(priv);
					}
					return 0;

				case NAT25_LOOKUP:
					{
						DEBUG_INFO("NAT25: Lookup AARP, Source=%d,%d Destination=%d,%d\n",
							ea->pa_src_net,
							ea->pa_src_node,
							ea->pa_dst_net,
							ea->pa_dst_node);

						__nat25_generate_apple_network_addr(networkAddr, &ea->pa_dst_net, &ea->pa_dst_node);

						__nat25_db_network_lookup_and_replace(priv, skb, networkAddr);

						// change to AARP destination mac address to Lookup result
						memcpy(ea->hw_dst, skb->data, ETH_ALEN);
					}
					return 0;

				default:
					return -1;
			}
		}

		/*   DDP   */
		else if(ddp != NULL)
		{
			switch(method)
			{
				case NAT25_CHECK:
					return -1;

				case NAT25_INSERT:
					{
						DEBUG_INFO("NAT25: Insert DDP, Source=%d,%d Destination=%d,%d\n",
							ddp->deh_snet,
							ddp->deh_snode,
							ddp->deh_dnet,
							ddp->deh_dnode);

						__nat25_generate_apple_network_addr(networkAddr, &ddp->deh_snet, &ddp->deh_snode);

						__nat25_db_network_insert(priv, skb->data+ETH_ALEN, networkAddr);

						__nat25_db_print(priv);
					}
					return 0;

				case NAT25_LOOKUP:
					{
						DEBUG_INFO("NAT25: Lookup DDP, Source=%d,%d Destination=%d,%d\n",
							ddp->deh_snet,
							ddp->deh_snode,
							ddp->deh_dnet,
							ddp->deh_dnode);

						__nat25_generate_apple_network_addr(networkAddr, &ddp->deh_dnet, &ddp->deh_dnode);

						__nat25_db_network_lookup_and_replace(priv, skb, networkAddr);
					}
					return 0;

				default:
					return -1;
			}
		}

		return -1;
	}

	/*---------------------------------------------------*/
	/*                Handle PPPoE frame                 */
	/*---------------------------------------------------*/
	else if((protocol == __constant_htons(ETH_P_PPP_DISC)) ||
		(protocol == __constant_htons(ETH_P_PPP_SES)))
	{
		struct pppoe_hdr *ph = (struct pppoe_hdr *)(skb->data + ETH_HLEN);
		unsigned short *pMagic;

		switch(method)
		{
			case NAT25_CHECK:
				if (ph->sid == 0)
					return 0;
				return 1;

			case NAT25_INSERT:
				if(ph->sid == 0)	// Discovery phase according to tag
				{
					if(ph->code == PADI_CODE || ph->code == PADR_CODE)
					{
						if (priv->ethBrExtInfo.addPPPoETag) {
							struct pppoe_tag *tag, *pOldTag;
							unsigned char tag_buf[40];
							int old_tag_len=0;

							tag = (struct pppoe_tag *)tag_buf;
							pOldTag = (struct pppoe_tag *)__nat25_find_pppoe_tag(ph, ntohs(PTT_RELAY_SID));
							if (pOldTag) { // if SID existed, copy old value and delete it
								old_tag_len = ntohs(pOldTag->tag_len);
								if (old_tag_len+TAG_HDR_LEN+MAGIC_CODE_LEN+RTL_RELAY_TAG_LEN > sizeof(tag_buf)) {
									DEBUG_ERR("SID tag length too long!\n");
									return -1;
								}

								memcpy(tag->tag_data+MAGIC_CODE_LEN+RTL_RELAY_TAG_LEN,
									pOldTag->tag_data, old_tag_len);

								if (skb_pull_and_merge(skb, (unsigned char *)pOldTag, TAG_HDR_LEN+old_tag_len) < 0) {
									DEBUG_ERR("call skb_pull_and_merge() failed in PADI/R packet!\n");
									return -1;
								}
								ph->length = htons(ntohs(ph->length)-TAG_HDR_LEN-old_tag_len);
							}

							tag->tag_type = PTT_RELAY_SID;
							tag->tag_len = htons(MAGIC_CODE_LEN+RTL_RELAY_TAG_LEN+old_tag_len);

							// insert the magic_code+client mac in relay tag
							pMagic = (unsigned short *)tag->tag_data;
							*pMagic = htons(MAGIC_CODE);
							memcpy(tag->tag_data+MAGIC_CODE_LEN, skb->data+ETH_ALEN, ETH_ALEN);

							//Add relay tag
							if(__nat25_add_pppoe_tag(skb, tag) < 0)
								return -1;

							DEBUG_INFO("NAT25: Insert PPPoE, forward %s packet\n",
											(ph->code == PADI_CODE ? "PADI" : "PADR"));
						}
						else { // not add relay tag
							if (priv->pppoe_connection_in_progress &&
									memcmp(skb->data+ETH_ALEN, priv->pppoe_addr, ETH_ALEN))	 {
								DEBUG_ERR("Discard PPPoE packet due to another PPPoE connection is in progress!\n");
								return -2;
							}

							if (priv->pppoe_connection_in_progress == 0)
								memcpy(priv->pppoe_addr, skb->data+ETH_ALEN, ETH_ALEN);

							priv->pppoe_connection_in_progress = WAIT_TIME_PPPOE;
						}
					}
					else
						return -1;
				}
				else	// session phase
				{
						DEBUG_INFO("NAT25: Insert PPPoE, insert session packet to %s\n", skb->dev->name);

						__nat25_generate_pppoe_network_addr(networkAddr, skb->data, &(ph->sid));

						__nat25_db_network_insert(priv, skb->data+ETH_ALEN, networkAddr);

						__nat25_db_print(priv);

						if (!priv->ethBrExtInfo.addPPPoETag &&
								priv->pppoe_connection_in_progress &&
									!memcmp(skb->data+ETH_ALEN, priv->pppoe_addr, ETH_ALEN))
							priv->pppoe_connection_in_progress = 0;
				}
				return 0;

			case NAT25_LOOKUP:
				if(ph->code == PADO_CODE || ph->code == PADS_CODE)
				{
					if (priv->ethBrExtInfo.addPPPoETag) {
						struct pppoe_tag *tag;
						unsigned char *ptr;
						unsigned short tagType, tagLen;
						int offset=0;

						if((ptr = __nat25_find_pppoe_tag(ph, ntohs(PTT_RELAY_SID))) == 0) {
							DEBUG_ERR("Fail to find PTT_RELAY_SID in FADO!\n");
							return -1;
						}

						tag = (struct pppoe_tag *)ptr;
						tagType = (unsigned short)((ptr[0] << 8) + ptr[1]);
						tagLen = (unsigned short)((ptr[2] << 8) + ptr[3]);

						if((tagType != ntohs(PTT_RELAY_SID)) || (tagLen < (MAGIC_CODE_LEN+RTL_RELAY_TAG_LEN))) {
							DEBUG_ERR("Invalid PTT_RELAY_SID tag length [%d]!\n", tagLen);
							return -1;
						}

						pMagic = (unsigned short *)tag->tag_data;
						if (ntohs(*pMagic) != MAGIC_CODE) {
							DEBUG_ERR("Can't find MAGIC_CODE in %s packet!\n",
								(ph->code == PADO_CODE ? "PADO" : "PADS"));
							return -1;
						}

						memcpy(skb->data, tag->tag_data+MAGIC_CODE_LEN, ETH_ALEN);

						if (tagLen > MAGIC_CODE_LEN+RTL_RELAY_TAG_LEN)
							offset = TAG_HDR_LEN;

						if (skb_pull_and_merge(skb, ptr+offset, TAG_HDR_LEN+MAGIC_CODE_LEN+RTL_RELAY_TAG_LEN-offset) < 0) {
							DEBUG_ERR("call skb_pull_and_merge() failed in PADO packet!\n");
							return -1;
						}
						ph->length = htons(ntohs(ph->length)-(TAG_HDR_LEN+MAGIC_CODE_LEN+RTL_RELAY_TAG_LEN-offset));
						if (offset > 0)
							tag->tag_len = htons(tagLen-MAGIC_CODE_LEN-RTL_RELAY_TAG_LEN);

						DEBUG_INFO("NAT25: Lookup PPPoE, forward %s Packet from %s\n",
							(ph->code == PADO_CODE ? "PADO" : "PADS"),	skb->dev->name);
					}
					else { // not add relay tag
						if (!priv->pppoe_connection_in_progress) {
							DEBUG_ERR("Discard PPPoE packet due to no connection in progresss!\n");
							return -1;
						}
						memcpy(skb->data, priv->pppoe_addr, ETH_ALEN);
						priv->pppoe_connection_in_progress = WAIT_TIME_PPPOE;
					}
				}
				else {
					if(ph->sid != 0)
					{
						DEBUG_INFO("NAT25: Lookup PPPoE, lookup session packet from %s\n", skb->dev->name);
						__nat25_generate_pppoe_network_addr(networkAddr, skb->data+ETH_ALEN, &(ph->sid));

						__nat25_db_network_lookup_and_replace(priv, skb, networkAddr);

						__nat25_db_print(priv);
					}
					else
						return -1;

				}
				return 0;

			default:
				return -1;
		}
	}

	/*---------------------------------------------------*/
	/*                 Handle EAP frame                  */
	/*---------------------------------------------------*/
	else if(protocol == __constant_htons(0x888e))
	{
		switch(method)
		{
			case NAT25_CHECK:
				return -1;

			case NAT25_INSERT:
				return 0;

			case NAT25_LOOKUP:
				return 0;

			default:
				return -1;
		}
	}

	/*---------------------------------------------------*/
	/*         Handle C-Media proprietary frame          */
	/*---------------------------------------------------*/
	else if((protocol == __constant_htons(0xe2ae)) ||
		(protocol == __constant_htons(0xe2af)))
	{
		switch(method)
		{
			case NAT25_CHECK:
				return -1;

			case NAT25_INSERT:
				return 0;

			case NAT25_LOOKUP:
				return 0;

			default:
				return -1;
		}
	}

	/*---------------------------------------------------*/
	/*         Handle IPV6 frame      							  */
	/*---------------------------------------------------*/
#ifdef CL_IPV6_PASS
	else if(protocol == __constant_htons(ETH_P_IPV6))
	{
		struct ipv6hdr *iph = (struct ipv6hdr *)(skb->data + ETH_HLEN);

		if (sizeof(*iph) >= (skb->len - ETH_HLEN))
		{
			DEBUG_WARN("NAT25: malformed IPv6 packet !\n");
			return -1;
		}

		switch(method)
		{
			case NAT25_CHECK:
				if (skb->data[0] & 1)
					return 0;				
				return -1;

			case NAT25_INSERT:
				{
					DEBUG_INFO("NAT25: Insert IP, SA=%4x:%4x:%4x:%4x:%4x:%4x:%4x:%4x,"
									" DA=%4x:%4x:%4x:%4x:%4x:%4x:%4x:%4x\n", 
						iph->saddr.s6_addr16[0],iph->saddr.s6_addr16[1],iph->saddr.s6_addr16[2],iph->saddr.s6_addr16[3],
						iph->saddr.s6_addr16[4],iph->saddr.s6_addr16[5],iph->saddr.s6_addr16[6],iph->saddr.s6_addr16[7],
						iph->daddr.s6_addr16[0],iph->daddr.s6_addr16[1],iph->daddr.s6_addr16[2],iph->daddr.s6_addr16[3],
						iph->daddr.s6_addr16[4],iph->daddr.s6_addr16[5],iph->daddr.s6_addr16[6],iph->daddr.s6_addr16[7]);

					if (memcmp(&iph->saddr, "\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0\x0", 16)) {
						__nat25_generate_ipv6_network_addr(networkAddr, (unsigned int *)&iph->saddr);
						__nat25_db_network_insert(priv, skb->data+ETH_ALEN, networkAddr);
						__nat25_db_print(priv);

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
				}
				return 0;

			case NAT25_LOOKUP:
				DEBUG_INFO("NAT25: Lookup IP, SA=%4x:%4x:%4x:%4x:%4x:%4x:%4x:%4x,"
								" DA=%4x:%4x:%4x:%4x:%4x:%4x:%4x:%4x\n", 
						iph->saddr.s6_addr16[0],iph->saddr.s6_addr16[1],iph->saddr.s6_addr16[2],iph->saddr.s6_addr16[3],
						iph->saddr.s6_addr16[4],iph->saddr.s6_addr16[5],iph->saddr.s6_addr16[6],iph->saddr.s6_addr16[7],
						iph->daddr.s6_addr16[0],iph->daddr.s6_addr16[1],iph->daddr.s6_addr16[2],iph->daddr.s6_addr16[3],
						iph->daddr.s6_addr16[4],iph->daddr.s6_addr16[5],iph->daddr.s6_addr16[6],iph->daddr.s6_addr16[7]);
			

				__nat25_generate_ipv6_network_addr(networkAddr, (unsigned int *)&iph->daddr);
				if (!__nat25_db_network_lookup_and_replace(priv, skb, networkAddr)) {
#ifdef SUPPORT_RX_UNI2MCAST							
					if (iph->daddr.s6_addr[0] == 0xff)
						convert_ipv6_mac_to_mc(skb);	
#endif											
				}
				return 0;

			default:
				return -1;
		}
	}
#endif	// CL_IPV6_PASS

	return -1;
}


int nat25_handle_frame(_adapter *priv, struct sk_buff *skb)
{
#ifdef BR_EXT_DEBUG
	if((!priv->ethBrExtInfo.nat25_disable) && (!(skb->data[0] & 1)))
	{
		panic_printk("NAT25: Input Frame: DA=%02x%02x%02x%02x%02x%02x SA=%02x%02x%02x%02x%02x%02x\n",
			skb->data[0],
			skb->data[1],
			skb->data[2],
			skb->data[3],
			skb->data[4],
			skb->data[5],
			skb->data[6],
			skb->data[7],
			skb->data[8],
			skb->data[9],
			skb->data[10],
			skb->data[11]);
	}
#endif

	if(!(skb->data[0] & 1))
	{
		int is_vlan_tag=0, i, retval=0;
		unsigned short vlan_hdr=0;

		if (*((unsigned short *)(skb->data+ETH_ALEN*2)) == __constant_htons(ETH_P_8021Q)) {
			is_vlan_tag = 1;
			vlan_hdr = *((unsigned short *)(skb->data+ETH_ALEN*2+2));
			for (i=0; i<6; i++)
				*((unsigned short *)(skb->data+ETH_ALEN*2+2-i*2)) = *((unsigned short *)(skb->data+ETH_ALEN*2-2-i*2));
			skb_pull(skb, 4);
		}

		if (!priv->ethBrExtInfo.nat25_disable)
		{
			_irqL irqL;
			_enter_critical_bh(&priv->br_ext_lock, &irqL);
			/*
			 *	This function look up the destination network address from
			 *	the NAT2.5 database. Return value = -1 means that the
			 *	corresponding network protocol is NOT support.
			 */
			if (!priv->ethBrExtInfo.nat25sc_disable &&
				(*((unsigned short *)(skb->data+ETH_ALEN*2)) == __constant_htons(ETH_P_IP)) &&
				!memcmp(priv->scdb_ip, skb->data+ETH_HLEN+16, 4)) {
				memcpy(skb->data, priv->scdb_mac, ETH_ALEN);
				
				_exit_critical_bh(&priv->br_ext_lock, &irqL);
			}
			else {
				_exit_critical_bh(&priv->br_ext_lock, &irqL);
				
				retval = nat25_db_handle(priv, skb, NAT25_LOOKUP);
			}
		}
		else {
			if (((*((unsigned short *)(skb->data+ETH_ALEN*2)) == __constant_htons(ETH_P_IP)) &&
					!memcmp(priv->br_ip, skb->data+ETH_HLEN+16, 4)) ||
				((*((unsigned short *)(skb->data+ETH_ALEN*2)) == __constant_htons(ETH_P_ARP)) &&
					!memcmp(priv->br_ip, skb->data+ETH_HLEN+24, 4))) {
				// for traffic to upper TCP/IP
				retval = nat25_db_handle(priv, skb, NAT25_LOOKUP);
			}
		}

		if (is_vlan_tag) {
			skb_push(skb, 4);
			for (i=0; i<6; i++)
				*((unsigned short *)(skb->data+i*2)) = *((unsigned short *)(skb->data+4+i*2));
			*((unsigned short *)(skb->data+ETH_ALEN*2)) = __constant_htons(ETH_P_8021Q);
			*((unsigned short *)(skb->data+ETH_ALEN*2+2)) = vlan_hdr;
		}

		if(retval == -1) {
			//DEBUG_ERR("NAT25: Lookup fail!\n");
			return -1;
		}
	}

	return 0;
}

#if 0
void mac_clone(_adapter *priv, unsigned char *addr)
{
	struct sockaddr sa;

	memcpy(sa.sa_data, addr, ETH_ALEN);
	DEBUG_INFO("MAC Clone: Addr=%02x%02x%02x%02x%02x%02x\n",
		addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
	rtl8192cd_set_hwaddr(priv->dev, &sa);
}


int mac_clone_handle_frame(_adapter *priv, struct sk_buff *skb)
{
	if(priv->ethBrExtInfo.macclone_enable && !priv->macclone_completed)
	{
		if(!(skb->data[ETH_ALEN] & 1))	//// check any other particular MAC add
		{
                        if(memcmp(skb->data+ETH_ALEN, GET_MY_HWADDR(priv), ETH_ALEN) &&
				((priv->dev->br_port) &&
				 memcmp(skb->data+ETH_ALEN, priv->br_mac, ETH_ALEN)))
			{
				mac_clone(priv, skb->data+ETH_ALEN);
				priv->macclone_completed = 1;
			}
		}
	}

	return 0;
}
#endif // 0

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
	u_int16_t secs;
	u_int16_t flags;
	u_int32_t ciaddr;
	u_int32_t yiaddr;
	u_int32_t siaddr;
	u_int32_t giaddr;
	u_int8_t chaddr[16];
	u_int8_t sname[64];
	u_int8_t file[128];
	u_int32_t cookie;
	u_int8_t options[308]; /* 312 - cookie */
};

void dhcp_flag_bcast(_adapter *priv, struct sk_buff *skb)
{
	if(skb == NULL)
		return;

	if(!priv->ethBrExtInfo.dhcp_bcst_disable)
	{
		unsigned short protocol = *((unsigned short *)(skb->data + 2 * ETH_ALEN));

		if(protocol == __constant_htons(ETH_P_IP)) // IP
		{
			struct iphdr* iph = (struct iphdr *)(skb->data + ETH_HLEN);

			if(iph->protocol == IPPROTO_UDP) // UDP
			{
				struct udphdr *udph = (struct udphdr *)((unsigned int)iph + (iph->ihl << 2));

				if((udph->source == __constant_htons(CLIENT_PORT))
					&& (udph->dest == __constant_htons(SERVER_PORT))) // DHCP request
				{
					struct dhcpMessage *dhcph =
						(struct dhcpMessage *)((unsigned int)udph + sizeof(struct udphdr));

					if(dhcph->cookie == __constant_htonl(DHCP_MAGIC)) // match magic word
					{
						if(!(dhcph->flags & htons(BROADCAST_FLAG))) // if not broadcast
						{
							register int sum = 0;

							DEBUG_INFO("DHCP: change flag of DHCP request to broadcast.\n");
							// or BROADCAST flag
							dhcph->flags |= htons(BROADCAST_FLAG);
							// recalculate checksum
							sum = ~(udph->check) & 0xffff;
							sum += dhcph->flags;
							while(sum >> 16)
								sum = (sum & 0xffff) + (sum >> 16);
							udph->check = ~sum;
						}
					}
				}
			}
		}
	}
}


void *scdb_findEntry(_adapter *priv, unsigned char *macAddr,
				unsigned char *ipAddr)
{
	unsigned char networkAddr[MAX_NETWORK_ADDR_LEN];
	struct nat25_network_db_entry *db;
	int hash;
	//_irqL irqL;
	//_enter_critical_bh(&priv->br_ext_lock, &irqL);

	__nat25_generate_ipv4_network_addr(networkAddr, (unsigned int *)ipAddr);
	hash = __nat25_network_hash(networkAddr);
	db = priv->nethash[hash];
	while (db != NULL)
	{
		if(!memcmp(db->networkAddr, networkAddr, MAX_NETWORK_ADDR_LEN)) {
			//_exit_critical_bh(&priv->br_ext_lock, &irqL);
			return (void *)db;
		}

		db = db->next_hash;
	}

	//_exit_critical_bh(&priv->br_ext_lock, &irqL);
	return NULL;
}

#endif	// CONFIG_BR_EXT

