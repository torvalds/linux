// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved. */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/in6.h>
#include <linux/tcp.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <net/ndisc.h>

#include "gdm_lte.h"
#include "netlink_k.h"
#include "hci.h"
#include "hci_packet.h"
#include "gdm_endian.h"

/*
 * Netlink protocol number
 */
#define NETLINK_LTE 30

/*
 * Default MTU Size
 */
#define DEFAULT_MTU_SIZE 1500

#define IP_VERSION_4	4
#define IP_VERSION_6	6

static struct {
	int ref_cnt;
	struct sock *sock;
} lte_event;

static struct device_type wwan_type = {
	.name   = "wwan",
};

static int gdm_lte_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

static int gdm_lte_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static int gdm_lte_set_config(struct net_device *dev, struct ifmap *map)
{
	if (dev->flags & IFF_UP)
		return -EBUSY;
	return 0;
}

static void tx_complete(void *arg)
{
	struct nic *nic = arg;

	if (netif_queue_stopped(nic->netdev))
		netif_wake_queue(nic->netdev);
}

static int gdm_lte_rx(struct sk_buff *skb, struct nic *nic, int nic_type)
{
	int ret, len;

	len = skb->len + ETH_HLEN;
	ret = netif_rx(skb);
	if (ret == NET_RX_DROP) {
		nic->stats.rx_dropped++;
	} else {
		nic->stats.rx_packets++;
		nic->stats.rx_bytes += len;
	}

	return 0;
}

static int gdm_lte_emulate_arp(struct sk_buff *skb_in, u32 nic_type)
{
	struct nic *nic = netdev_priv(skb_in->dev);
	struct sk_buff *skb_out;
	struct ethhdr eth;
	struct vlan_ethhdr vlan_eth;
	struct arphdr *arp_in;
	struct arphdr *arp_out;
	struct arpdata {
		u8 ar_sha[ETH_ALEN];
		u8 ar_sip[4];
		u8 ar_tha[ETH_ALEN];
		u8 ar_tip[4];
	};
	struct arpdata *arp_data_in;
	struct arpdata *arp_data_out;
	u8 arp_temp[60];
	void *mac_header_data;
	u32 mac_header_len;

	/* Check for skb->len, discard if empty */
	if (skb_in->len == 0)
		return -ENODATA;

	/* Format the mac header so that it can be put to skb */
	if (ntohs(((struct ethhdr *)skb_in->data)->h_proto) == ETH_P_8021Q) {
		memcpy(&vlan_eth, skb_in->data, sizeof(struct vlan_ethhdr));
		mac_header_data = &vlan_eth;
		mac_header_len = VLAN_ETH_HLEN;
	} else {
		memcpy(&eth, skb_in->data, sizeof(struct ethhdr));
		mac_header_data = &eth;
		mac_header_len = ETH_HLEN;
	}

	/* Get the pointer of the original request */
	arp_in = (struct arphdr *)(skb_in->data + mac_header_len);
	arp_data_in = (struct arpdata *)(skb_in->data + mac_header_len +
					sizeof(struct arphdr));

	/* Get the pointer of the outgoing response */
	arp_out = (struct arphdr *)arp_temp;
	arp_data_out = (struct arpdata *)(arp_temp + sizeof(struct arphdr));

	/* Copy the arp header */
	memcpy(arp_out, arp_in, sizeof(struct arphdr));
	arp_out->ar_op = htons(ARPOP_REPLY);

	/* Copy the arp payload: based on 2 bytes of mac and fill the IP */
	arp_data_out->ar_sha[0] = arp_data_in->ar_sha[0];
	arp_data_out->ar_sha[1] = arp_data_in->ar_sha[1];
	memcpy(&arp_data_out->ar_sha[2], &arp_data_in->ar_tip[0], 4);
	memcpy(&arp_data_out->ar_sip[0], &arp_data_in->ar_tip[0], 4);
	memcpy(&arp_data_out->ar_tha[0], &arp_data_in->ar_sha[0], 6);
	memcpy(&arp_data_out->ar_tip[0], &arp_data_in->ar_sip[0], 4);

	/* Fill the destination mac with source mac of the received packet */
	memcpy(mac_header_data, mac_header_data + ETH_ALEN, ETH_ALEN);
	/* Fill the source mac with nic's source mac */
	memcpy(mac_header_data + ETH_ALEN, nic->src_mac_addr, ETH_ALEN);

	/* Alloc skb and reserve align */
	skb_out = dev_alloc_skb(skb_in->len);
	if (!skb_out)
		return -ENOMEM;
	skb_reserve(skb_out, NET_IP_ALIGN);

	skb_put_data(skb_out, mac_header_data, mac_header_len);
	skb_put_data(skb_out, arp_out, sizeof(struct arphdr));
	skb_put_data(skb_out, arp_data_out, sizeof(struct arpdata));

	skb_out->protocol = ((struct ethhdr *)mac_header_data)->h_proto;
	skb_out->dev = skb_in->dev;
	skb_reset_mac_header(skb_out);
	skb_pull(skb_out, ETH_HLEN);

	gdm_lte_rx(skb_out, nic, nic_type);

	return 0;
}

static __sum16 icmp6_checksum(struct ipv6hdr *ipv6, u16 *ptr, int len)
{
	unsigned short *w;
	__wsum sum = 0;
	int i;
	u16 pa;

	union {
		struct {
			u8 ph_src[16];
			u8 ph_dst[16];
			u32 ph_len;
			u8 ph_zero[3];
			u8 ph_nxt;
		} ph __packed;
		u16 pa[20];
	} pseudo_header;

	memset(&pseudo_header, 0, sizeof(pseudo_header));
	memcpy(&pseudo_header.ph.ph_src, &ipv6->saddr.in6_u.u6_addr8, 16);
	memcpy(&pseudo_header.ph.ph_dst, &ipv6->daddr.in6_u.u6_addr8, 16);
	pseudo_header.ph.ph_len = be16_to_cpu(ipv6->payload_len);
	pseudo_header.ph.ph_nxt = ipv6->nexthdr;

	for (i = 0; i < ARRAY_SIZE(pseudo_header.pa); i++) {
		pa = pseudo_header.pa[i];
		sum = csum_add(sum, csum_unfold((__force __sum16)pa));
	}

	w = ptr;
	while (len > 1) {
		sum = csum_add(sum, csum_unfold((__force __sum16)*w++));
		len -= 2;
	}

	return csum_fold(sum);
}

static int gdm_lte_emulate_ndp(struct sk_buff *skb_in, u32 nic_type)
{
	struct nic *nic = netdev_priv(skb_in->dev);
	struct sk_buff *skb_out;
	struct ethhdr eth;
	struct vlan_ethhdr vlan_eth;
	struct neighbour_advertisement {
		u8 target_address[16];
		u8 type;
		u8 length;
		u8 link_layer_address[6];
	};
	struct neighbour_advertisement na;
	struct neighbour_solicitation {
		u8 target_address[16];
	};
	struct neighbour_solicitation *ns;
	struct ipv6hdr *ipv6_in;
	struct ipv6hdr ipv6_out;
	struct icmp6hdr *icmp6_in;
	struct icmp6hdr icmp6_out;

	void *mac_header_data;
	u32 mac_header_len;

	/* Format the mac header so that it can be put to skb */
	if (ntohs(((struct ethhdr *)skb_in->data)->h_proto) == ETH_P_8021Q) {
		memcpy(&vlan_eth, skb_in->data, sizeof(struct vlan_ethhdr));
		if (ntohs(vlan_eth.h_vlan_encapsulated_proto) != ETH_P_IPV6)
			return -EPROTONOSUPPORT;
		mac_header_data = &vlan_eth;
		mac_header_len = VLAN_ETH_HLEN;
	} else {
		memcpy(&eth, skb_in->data, sizeof(struct ethhdr));
		if (ntohs(eth.h_proto) != ETH_P_IPV6)
			return -EPROTONOSUPPORT;
		mac_header_data = &eth;
		mac_header_len = ETH_HLEN;
	}

	/* Check if this is IPv6 ICMP packet */
	ipv6_in = (struct ipv6hdr *)(skb_in->data + mac_header_len);
	if (ipv6_in->version != 6 || ipv6_in->nexthdr != IPPROTO_ICMPV6)
		return -EPROTONOSUPPORT;

	/* Check if this is NDP packet */
	icmp6_in = (struct icmp6hdr *)(skb_in->data + mac_header_len +
					sizeof(struct ipv6hdr));
	if (icmp6_in->icmp6_type == NDISC_ROUTER_SOLICITATION) { /* Check RS */
		return -EPROTONOSUPPORT;
	} else if (icmp6_in->icmp6_type == NDISC_NEIGHBOUR_SOLICITATION) {
		/* Check NS */
		u8 icmp_na[sizeof(struct icmp6hdr) +
			sizeof(struct neighbour_advertisement)];
		u8 zero_addr8[16] = {0,};

		if (memcmp(ipv6_in->saddr.in6_u.u6_addr8, zero_addr8, 16) == 0)
			/* Duplicate Address Detection: Source IP is all zero */
			return 0;

		icmp6_out.icmp6_type = NDISC_NEIGHBOUR_ADVERTISEMENT;
		icmp6_out.icmp6_code = 0;
		icmp6_out.icmp6_cksum = 0;
		/* R=0, S=1, O=1 */
		icmp6_out.icmp6_dataun.un_data32[0] = htonl(0x60000000);

		ns = (struct neighbour_solicitation *)
			(skb_in->data + mac_header_len +
			 sizeof(struct ipv6hdr) + sizeof(struct icmp6hdr));
		memcpy(&na.target_address, ns->target_address, 16);
		na.type = 0x02;
		na.length = 1;
		na.link_layer_address[0] = 0x00;
		na.link_layer_address[1] = 0x0a;
		na.link_layer_address[2] = 0x3b;
		na.link_layer_address[3] = 0xaf;
		na.link_layer_address[4] = 0x63;
		na.link_layer_address[5] = 0xc7;

		memcpy(&ipv6_out, ipv6_in, sizeof(struct ipv6hdr));
		memcpy(ipv6_out.saddr.in6_u.u6_addr8, &na.target_address, 16);
		memcpy(ipv6_out.daddr.in6_u.u6_addr8,
		       ipv6_in->saddr.in6_u.u6_addr8, 16);
		ipv6_out.payload_len = htons(sizeof(struct icmp6hdr) +
				sizeof(struct neighbour_advertisement));

		memcpy(icmp_na, &icmp6_out, sizeof(struct icmp6hdr));
		memcpy(icmp_na + sizeof(struct icmp6hdr), &na,
		       sizeof(struct neighbour_advertisement));

		icmp6_out.icmp6_cksum = icmp6_checksum(&ipv6_out,
						       (u16 *)icmp_na,
						       sizeof(icmp_na));
	} else {
		return -EINVAL;
	}

	/* Fill the destination mac with source mac of the received packet */
	memcpy(mac_header_data, mac_header_data + ETH_ALEN, ETH_ALEN);
	/* Fill the source mac with nic's source mac */
	memcpy(mac_header_data + ETH_ALEN, nic->src_mac_addr, ETH_ALEN);

	/* Alloc skb and reserve align */
	skb_out = dev_alloc_skb(skb_in->len);
	if (!skb_out)
		return -ENOMEM;
	skb_reserve(skb_out, NET_IP_ALIGN);

	skb_put_data(skb_out, mac_header_data, mac_header_len);
	skb_put_data(skb_out, &ipv6_out, sizeof(struct ipv6hdr));
	skb_put_data(skb_out, &icmp6_out, sizeof(struct icmp6hdr));
	skb_put_data(skb_out, &na, sizeof(struct neighbour_advertisement));

	skb_out->protocol = ((struct ethhdr *)mac_header_data)->h_proto;
	skb_out->dev = skb_in->dev;
	skb_reset_mac_header(skb_out);
	skb_pull(skb_out, ETH_HLEN);

	gdm_lte_rx(skb_out, nic, nic_type);

	return 0;
}

static s32 gdm_lte_tx_nic_type(struct net_device *dev, struct sk_buff *skb)
{
	struct nic *nic = netdev_priv(dev);
	struct ethhdr *eth;
	struct vlan_ethhdr *vlan_eth;
	struct iphdr *ip;
	struct ipv6hdr *ipv6;
	int mac_proto;
	void *network_data;
	u32 nic_type;

	/* NIC TYPE is based on the nic_id of this net_device */
	nic_type = 0x00000010 | nic->nic_id;

	/* Get ethernet protocol */
	eth = (struct ethhdr *)skb->data;
	if (ntohs(eth->h_proto) == ETH_P_8021Q) {
		vlan_eth = skb_vlan_eth_hdr(skb);
		mac_proto = ntohs(vlan_eth->h_vlan_encapsulated_proto);
		network_data = skb->data + VLAN_ETH_HLEN;
		nic_type |= NIC_TYPE_F_VLAN;
	} else {
		mac_proto = ntohs(eth->h_proto);
		network_data = skb->data + ETH_HLEN;
	}

	/* Process packet for nic type */
	switch (mac_proto) {
	case ETH_P_ARP:
		nic_type |= NIC_TYPE_ARP;
		break;
	case ETH_P_IP:
		nic_type |= NIC_TYPE_F_IPV4;
		ip = network_data;

		/* Check DHCPv4 */
		if (ip->protocol == IPPROTO_UDP) {
			struct udphdr *udp =
					network_data + sizeof(struct iphdr);
			if (ntohs(udp->dest) == 67 || ntohs(udp->dest) == 68)
				nic_type |= NIC_TYPE_F_DHCP;
		}
		break;
	case ETH_P_IPV6:
		nic_type |= NIC_TYPE_F_IPV6;
		ipv6 = network_data;

		if (ipv6->nexthdr == IPPROTO_ICMPV6) /* Check NDP request */ {
			struct icmp6hdr *icmp6 =
					network_data + sizeof(struct ipv6hdr);
			if (icmp6->icmp6_type == NDISC_NEIGHBOUR_SOLICITATION)
				nic_type |= NIC_TYPE_ICMPV6;
		} else if (ipv6->nexthdr == IPPROTO_UDP) /* Check DHCPv6 */ {
			struct udphdr *udp =
					network_data + sizeof(struct ipv6hdr);
			if (ntohs(udp->dest) == 546 || ntohs(udp->dest) == 547)
				nic_type |= NIC_TYPE_F_DHCP;
		}
		break;
	default:
		break;
	}

	return nic_type;
}

static netdev_tx_t gdm_lte_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct nic *nic = netdev_priv(dev);
	u32 nic_type;
	void *data_buf;
	int data_len;
	int idx;
	int ret = 0;

	nic_type = gdm_lte_tx_nic_type(dev, skb);
	if (nic_type == 0) {
		netdev_err(dev, "tx - invalid nic_type\n");
		return -EMEDIUMTYPE;
	}

	if (nic_type & NIC_TYPE_ARP) {
		if (gdm_lte_emulate_arp(skb, nic_type) == 0) {
			dev_kfree_skb(skb);
			return 0;
		}
	}

	if (nic_type & NIC_TYPE_ICMPV6) {
		if (gdm_lte_emulate_ndp(skb, nic_type) == 0) {
			dev_kfree_skb(skb);
			return 0;
		}
	}

	/*
	 * Need byte shift (that is, remove VLAN tag) if there is one
	 * For the case of ARP, this breaks the offset as vlan_ethhdr+4
	 * is treated as ethhdr	However, it shouldn't be a problem as
	 * the response starts from arp_hdr and ethhdr is created by this
	 * driver based on the NIC mac
	 */
	if (nic_type & NIC_TYPE_F_VLAN) {
		struct vlan_ethhdr *vlan_eth = skb_vlan_eth_hdr(skb);

		nic->vlan_id = ntohs(vlan_eth->h_vlan_TCI) & VLAN_VID_MASK;
		data_buf = skb->data + (VLAN_ETH_HLEN - ETH_HLEN);
		data_len = skb->len - (VLAN_ETH_HLEN - ETH_HLEN);
	} else {
		nic->vlan_id = 0;
		data_buf = skb->data;
		data_len = skb->len;
	}

	/* If it is a ICMPV6 packet, clear all the other bits :
	 * for backward compatibility with the firmware
	 */
	if (nic_type & NIC_TYPE_ICMPV6)
		nic_type = NIC_TYPE_ICMPV6;

	/* If it is not a dhcp packet, clear all the flag bits :
	 * original NIC, otherwise the special flag (IPVX | DHCP)
	 */
	if (!(nic_type & NIC_TYPE_F_DHCP))
		nic_type &= NIC_TYPE_MASK;

	ret = sscanf(dev->name, "lte%d", &idx);
	if (ret != 1) {
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	ret = nic->phy_dev->send_sdu_func(nic->phy_dev->priv_dev,
					  data_buf, data_len,
					  nic->pdn_table.dft_eps_id, 0,
					  tx_complete, nic, idx,
					  nic_type);

	if (ret == TX_NO_BUFFER || ret == TX_NO_SPC) {
		netif_stop_queue(dev);
		if (ret == TX_NO_BUFFER)
			ret = 0;
		else
			ret = -ENOSPC;
	} else if (ret == TX_NO_DEV) {
		ret = -ENODEV;
	}

	/* Updates tx stats */
	if (ret) {
		nic->stats.tx_dropped++;
	} else {
		nic->stats.tx_packets++;
		nic->stats.tx_bytes += data_len;
	}
	dev_kfree_skb(skb);

	return 0;
}

static struct net_device_stats *gdm_lte_stats(struct net_device *dev)
{
	struct nic *nic = netdev_priv(dev);

	return &nic->stats;
}

static int gdm_lte_event_send(struct net_device *dev, char *buf, int len)
{
	struct phy_dev *phy_dev = ((struct nic *)netdev_priv(dev))->phy_dev;
	struct hci_packet *hci = (struct hci_packet *)buf;
	int length;
	int idx;
	int ret;

	ret = sscanf(dev->name, "lte%d", &idx);
	if (ret != 1)
		return -EINVAL;

	length = gdm_dev16_to_cpu(phy_dev->get_endian(phy_dev->priv_dev),
				  hci->len) + HCI_HEADER_SIZE;
	return netlink_send(lte_event.sock, idx, 0, buf, length, dev);
}

static void gdm_lte_event_rcv(struct net_device *dev, u16 type,
			      void *msg, int len)
{
	struct nic *nic = netdev_priv(dev);

	nic->phy_dev->send_hci_func(nic->phy_dev->priv_dev, msg, len, NULL,
				    NULL);
}

int gdm_lte_event_init(void)
{
	if (lte_event.ref_cnt == 0)
		lte_event.sock = netlink_init(NETLINK_LTE, gdm_lte_event_rcv);

	if (lte_event.sock) {
		lte_event.ref_cnt++;
		return 0;
	}

	pr_err("event init failed\n");
	return -ENODATA;
}

void gdm_lte_event_exit(void)
{
	if (lte_event.sock && --lte_event.ref_cnt == 0) {
		sock_release(lte_event.sock->sk_socket);
		lte_event.sock = NULL;
	}
}

static int find_dev_index(u32 nic_type)
{
	u8 index;

	index = (u8)(nic_type & 0x0000000f);
	if (index >= MAX_NIC_TYPE)
		return -EINVAL;

	return index;
}

static void gdm_lte_netif_rx(struct net_device *dev, char *buf,
			     int len, int flagged_nic_type)
{
	u32 nic_type;
	struct nic *nic;
	struct sk_buff *skb;
	struct ethhdr eth;
	struct vlan_ethhdr vlan_eth;
	void *mac_header_data;
	u32 mac_header_len;
	char ip_version = 0;

	nic_type = flagged_nic_type & NIC_TYPE_MASK;
	nic = netdev_priv(dev);

	if (flagged_nic_type & NIC_TYPE_F_DHCP) {
		/* Change the destination mac address
		 * with the one requested the IP
		 */
		if (flagged_nic_type & NIC_TYPE_F_IPV4) {
			struct dhcp_packet {
				u8 op;      /* BOOTREQUEST or BOOTREPLY */
				u8 htype;   /* hardware address type.
					     * 1 = 10mb ethernet
					     */
				u8 hlen;    /* hardware address length */
				u8 hops;    /* used by relay agents only */
				u32 xid;    /* unique id */
				u16 secs;   /* elapsed since client began
					     * acquisition/renewal
					     */
				u16 flags;  /* only one flag so far: */
				#define BROADCAST_FLAG 0x8000
				/* "I need broadcast replies" */
				u32 ciaddr; /* client IP (if client is in
					     * BOUND, RENEW or REBINDING state)
					     */
				u32 yiaddr; /* 'your' (client) IP address */
				/* IP address of next server to use in
				 * bootstrap, returned in DHCPOFFER,
				 * DHCPACK by server
				 */
				u32 siaddr_nip;
				u32 gateway_nip; /* relay agent IP address */
				u8 chaddr[16];   /* link-layer client hardware
						  * address (MAC)
						  */
				u8 sname[64];    /* server host name (ASCIZ) */
				u8 file[128];    /* boot file name (ASCIZ) */
				u32 cookie;      /* fixed first four option
						  * bytes (99,130,83,99 dec)
						  */
			} __packed;
			int offset = sizeof(struct iphdr) +
				     sizeof(struct udphdr) +
				     offsetof(struct dhcp_packet, chaddr);
			if (offset + ETH_ALEN > len)
				return;
			ether_addr_copy(nic->dest_mac_addr, buf + offset);
		}
	}

	if (nic->vlan_id > 0) {
		mac_header_data = (void *)&vlan_eth;
		mac_header_len = VLAN_ETH_HLEN;
	} else {
		mac_header_data = (void *)&eth;
		mac_header_len = ETH_HLEN;
	}

	/* Format the data so that it can be put to skb */
	ether_addr_copy(mac_header_data, nic->dest_mac_addr);
	memcpy(mac_header_data + ETH_ALEN, nic->src_mac_addr, ETH_ALEN);

	vlan_eth.h_vlan_TCI = htons(nic->vlan_id);
	vlan_eth.h_vlan_proto = htons(ETH_P_8021Q);

	if (nic_type == NIC_TYPE_ARP) {
		/* Should be response: Only happens because
		 * there was a request from the host
		 */
		eth.h_proto = htons(ETH_P_ARP);
		vlan_eth.h_vlan_encapsulated_proto = htons(ETH_P_ARP);
	} else {
		ip_version = buf[0] >> 4;
		if (ip_version == IP_VERSION_4) {
			eth.h_proto = htons(ETH_P_IP);
			vlan_eth.h_vlan_encapsulated_proto = htons(ETH_P_IP);
		} else if (ip_version == IP_VERSION_6) {
			eth.h_proto = htons(ETH_P_IPV6);
			vlan_eth.h_vlan_encapsulated_proto = htons(ETH_P_IPV6);
		} else {
			netdev_err(dev, "Unknown IP version %d\n", ip_version);
			return;
		}
	}

	/* Alloc skb and reserve align */
	skb = dev_alloc_skb(len + mac_header_len + NET_IP_ALIGN);
	if (!skb)
		return;
	skb_reserve(skb, NET_IP_ALIGN);

	skb_put_data(skb, mac_header_data, mac_header_len);
	skb_put_data(skb, buf, len);

	skb->protocol = ((struct ethhdr *)mac_header_data)->h_proto;
	skb->dev = dev;
	skb_reset_mac_header(skb);
	skb_pull(skb, ETH_HLEN);

	gdm_lte_rx(skb, nic, nic_type);
}

static void gdm_lte_multi_sdu_pkt(struct phy_dev *phy_dev, char *buf, int len)
{
	struct net_device *dev;
	struct multi_sdu *multi_sdu = (struct multi_sdu *)buf;
	struct sdu *sdu = NULL;
	u8 endian = phy_dev->get_endian(phy_dev->priv_dev);
	u8 *data = (u8 *)multi_sdu->data;
	int copied;
	u16 i = 0;
	u16 num_packet;
	u16 hci_len;
	u16 cmd_evt;
	u32 nic_type;
	int index;

	num_packet = gdm_dev16_to_cpu(endian, multi_sdu->num_packet);

	for (i = 0; i < num_packet; i++) {
		copied = data - multi_sdu->data;
		if (len < copied + sizeof(*sdu)) {
			pr_err("rx prevent buffer overflow");
			return;
		}

		sdu = (struct sdu *)data;

		cmd_evt  = gdm_dev16_to_cpu(endian, sdu->cmd_evt);
		hci_len  = gdm_dev16_to_cpu(endian, sdu->len);
		nic_type = gdm_dev32_to_cpu(endian, sdu->nic_type);

		if (cmd_evt != LTE_RX_SDU) {
			pr_err("rx sdu wrong hci %04x\n", cmd_evt);
			return;
		}
		if (hci_len < 12 ||
		    len < copied + sizeof(*sdu) + (hci_len - 12)) {
			pr_err("rx sdu invalid len %d\n", hci_len);
			return;
		}

		index = find_dev_index(nic_type);
		if (index < 0) {
			pr_err("rx sdu invalid nic_type :%x\n", nic_type);
			return;
		}
		dev = phy_dev->dev[index];
		gdm_lte_netif_rx(dev, (char *)sdu->data,
				 (int)(hci_len - 12), nic_type);

		data += ((hci_len + 3) & 0xfffc) + HCI_HEADER_SIZE;
	}
}

static void gdm_lte_pdn_table(struct net_device *dev, char *buf, int len)
{
	struct nic *nic = netdev_priv(dev);
	struct hci_pdn_table_ind *pdn_table = (struct hci_pdn_table_ind *)buf;
	u8 ed = nic->phy_dev->get_endian(nic->phy_dev->priv_dev);

	if (!pdn_table->activate) {
		memset(&nic->pdn_table, 0x00, sizeof(struct pdn_table));
		netdev_info(dev, "pdn deactivated\n");

		return;
	}

	nic->pdn_table.activate = pdn_table->activate;
	nic->pdn_table.dft_eps_id = gdm_dev32_to_cpu(ed, pdn_table->dft_eps_id);
	nic->pdn_table.nic_type = gdm_dev32_to_cpu(ed, pdn_table->nic_type);

	netdev_info(dev, "pdn activated, nic_type=0x%x\n",
		    nic->pdn_table.nic_type);
}

static int gdm_lte_receive_pkt(struct phy_dev *phy_dev, char *buf, int len)
{
	struct hci_packet *hci = (struct hci_packet *)buf;
	struct hci_pdn_table_ind *pdn_table = (struct hci_pdn_table_ind *)buf;
	struct sdu *sdu;
	struct net_device *dev;
	u8 endian = phy_dev->get_endian(phy_dev->priv_dev);
	int ret = 0;
	u16 cmd_evt;
	u32 nic_type;
	int index;

	if (!len)
		return ret;

	cmd_evt = gdm_dev16_to_cpu(endian, hci->cmd_evt);

	dev = phy_dev->dev[0];
	if (!dev)
		return 0;

	switch (cmd_evt) {
	case LTE_RX_SDU:
		sdu = (struct sdu *)hci->data;
		nic_type = gdm_dev32_to_cpu(endian, sdu->nic_type);
		index = find_dev_index(nic_type);
		if (index < 0)
			return index;
		dev = phy_dev->dev[index];
		gdm_lte_netif_rx(dev, hci->data, len, nic_type);
		break;
	case LTE_RX_MULTI_SDU:
		gdm_lte_multi_sdu_pkt(phy_dev, buf, len);
		break;
	case LTE_LINK_ON_OFF_INDICATION:
		netdev_info(dev, "link %s\n",
			    ((struct hci_connect_ind *)buf)->connect
			    ? "on" : "off");
		break;
	case LTE_PDN_TABLE_IND:
		pdn_table = (struct hci_pdn_table_ind *)buf;
		nic_type = gdm_dev32_to_cpu(endian, pdn_table->nic_type);
		index = find_dev_index(nic_type);
		if (index < 0)
			return index;
		dev = phy_dev->dev[index];
		gdm_lte_pdn_table(dev, buf, len);
		fallthrough;
	default:
		ret = gdm_lte_event_send(dev, buf, len);
		break;
	}

	return ret;
}

static int rx_complete(void *arg, void *data, int len, int context)
{
	struct phy_dev *phy_dev = arg;

	return gdm_lte_receive_pkt(phy_dev, data, len);
}

void start_rx_proc(struct phy_dev *phy_dev)
{
	int i;

	for (i = 0; i < MAX_RX_SUBMIT_COUNT; i++)
		phy_dev->rcv_func(phy_dev->priv_dev,
				rx_complete, phy_dev, USB_COMPLETE);
}

static const struct net_device_ops gdm_netdev_ops = {
	.ndo_open			= gdm_lte_open,
	.ndo_stop			= gdm_lte_close,
	.ndo_set_config			= gdm_lte_set_config,
	.ndo_start_xmit			= gdm_lte_tx,
	.ndo_get_stats			= gdm_lte_stats,
};

static u8 gdm_lte_macaddr[ETH_ALEN] = {0x00, 0x0a, 0x3b, 0x00, 0x00, 0x00};

static void form_mac_address(u8 *dev_addr, u8 *nic_src, u8 *nic_dest,
			     u8 *mac_address, u8 index)
{
	/* Form the dev_addr */
	if (!mac_address)
		ether_addr_copy(dev_addr, gdm_lte_macaddr);
	else
		ether_addr_copy(dev_addr, mac_address);

	/* The last byte of the mac address
	 * should be less than or equal to 0xFC
	 */
	dev_addr[ETH_ALEN - 1] += index;

	/* Create random nic src and copy the first
	 * 3 bytes to be the same as dev_addr
	 */
	eth_random_addr(nic_src);
	memcpy(nic_src, dev_addr, 3);

	/* Copy the nic_dest from dev_addr*/
	ether_addr_copy(nic_dest, dev_addr);
}

static void validate_mac_address(u8 *mac_address)
{
	/* if zero address or multicast bit set, restore the default value */
	if (is_zero_ether_addr(mac_address) || (mac_address[0] & 0x01)) {
		pr_err("MAC invalid, restoring default\n");
		memcpy(mac_address, gdm_lte_macaddr, 6);
	}
}

int register_lte_device(struct phy_dev *phy_dev,
			struct device *dev, u8 *mac_address)
{
	struct nic *nic;
	struct net_device *net;
	char pdn_dev_name[16];
	u8 addr[ETH_ALEN];
	int ret = 0;
	u8 index;

	validate_mac_address(mac_address);

	for (index = 0; index < MAX_NIC_TYPE; index++) {
		/* Create device name lteXpdnX */
		sprintf(pdn_dev_name, "lte%%dpdn%d", index);

		/* Allocate netdev */
		net = alloc_netdev(sizeof(struct nic), pdn_dev_name,
				   NET_NAME_UNKNOWN, ether_setup);
		if (!net) {
			ret = -ENOMEM;
			goto err;
		}
		net->netdev_ops = &gdm_netdev_ops;
		net->flags &= ~IFF_MULTICAST;
		net->mtu = DEFAULT_MTU_SIZE;

		nic = netdev_priv(net);
		memset(nic, 0, sizeof(struct nic));
		nic->netdev = net;
		nic->phy_dev = phy_dev;
		nic->nic_id = index;

		form_mac_address(addr,
				 nic->src_mac_addr,
				 nic->dest_mac_addr,
				 mac_address,
				 index);
		eth_hw_addr_set(net, addr);

		SET_NETDEV_DEV(net, dev);
		SET_NETDEV_DEVTYPE(net, &wwan_type);

		ret = register_netdev(net);
		if (ret)
			goto err;

		netif_carrier_on(net);

		phy_dev->dev[index] = net;
	}

	return 0;

err:
	unregister_lte_device(phy_dev);

	return ret;
}

void unregister_lte_device(struct phy_dev *phy_dev)
{
	struct net_device *net;
	int index;

	for (index = 0; index < MAX_NIC_TYPE; index++) {
		net = phy_dev->dev[index];
		if (!net)
			continue;

		unregister_netdev(net);
		free_netdev(net);
	}
}
