/*
 * Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/etherdevice.h>
#include <asm/byteorder.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <linux/in.h>

#include "gdm_wimax.h"
#include "hci.h"
#include "wm_ioctl.h"
#include "netlink_k.h"

#define gdm_wimax_send(n, d, l)	\
	(n->phy_dev->send_func)(n->phy_dev->priv_dev, d, l, NULL, NULL)
#define gdm_wimax_send_with_cb(n, d, l, c, b)	\
	(n->phy_dev->send_func)(n->phy_dev->priv_dev, d, l, c, b)
#define gdm_wimax_rcv_with_cb(n, c, b)	\
	(n->phy_dev->rcv_func)(n->phy_dev->priv_dev, c, b)

#define EVT_MAX_SIZE	2048

struct evt_entry {
	struct	list_head list;
	struct	net_device *dev;
	char	evt_data[EVT_MAX_SIZE];
	int	size;
};

static void __gdm_wimax_event_send(struct work_struct *work);
static inline struct evt_entry *alloc_event_entry(void);
static inline void free_event_entry(struct evt_entry *e);
static struct evt_entry *get_event_entry(void);
static void put_event_entry(struct evt_entry *e);

static struct {
	int ref_cnt;
	struct sock *sock;
	struct list_head evtq;
	spinlock_t evt_lock;
	struct list_head freeq;
	struct work_struct ws;
} wm_event;

static u8 gdm_wimax_macaddr[6] = {0x00, 0x0a, 0x3b, 0xf0, 0x01, 0x30};

static void gdm_wimax_ind_fsm_update(struct net_device *dev, struct fsm_s *fsm);
static void gdm_wimax_ind_if_updown(struct net_device *dev, int if_up);

static const char *get_protocol_name(u16 protocol)
{
	static char buf[32];
	const char *name = "-";

	switch (protocol) {
	case ETH_P_ARP:
		name = "ARP";
		break;
	case ETH_P_IP:
		name = "IP";
		break;
	case ETH_P_IPV6:
		name = "IPv6";
		break;
	}

	sprintf(buf, "0x%04x(%s)", protocol, name);
	return buf;
}

static const char *get_ip_protocol_name(u8 ip_protocol)
{
	static char buf[32];
	const char *name = "-";

	switch (ip_protocol) {
	case IPPROTO_TCP:
		name = "TCP";
		break;
	case IPPROTO_UDP:
		name = "UDP";
		break;
	case IPPROTO_ICMP:
		name = "ICMP";
		break;
	}

	sprintf(buf, "%u(%s)", ip_protocol, name);
	return buf;
}

static const char *get_port_name(u16 port)
{
	static char buf[32];
	const char *name = "-";

	switch (port) {
	case 67:
		name = "DHCP-Server";
		break;
	case 68:
		name = "DHCP-Client";
		break;
	case 69:
		name = "TFTP";
		break;
	}

	sprintf(buf, "%u(%s)", port, name);
	return buf;
}

static void dump_eth_packet(struct net_device *dev, const char *title,
			    u8 *data, int len)
{
	struct iphdr *ih = NULL;
	struct udphdr *uh = NULL;
	u16 protocol = 0;
	u8 ip_protocol = 0;
	u16 port = 0;

	protocol = (data[12]<<8) | data[13];
	ih = (struct iphdr *)(data+ETH_HLEN);

	if (protocol == ETH_P_IP) {
		uh = (struct udphdr *)((char *)ih + sizeof(struct iphdr));
		ip_protocol = ih->protocol;
		port = ntohs(uh->dest);
	} else if (protocol == ETH_P_IPV6) {
		struct ipv6hdr *i6h = (struct ipv6hdr *)data;

		uh = (struct udphdr *)((char *)i6h + sizeof(struct ipv6hdr));
		ip_protocol = i6h->nexthdr;
		port = ntohs(uh->dest);
	}

	netdev_dbg(dev, "[%s] len=%d, %s, %s, %s\n", title, len,
		   get_protocol_name(protocol),
		   get_ip_protocol_name(ip_protocol),
		   get_port_name(port));

	if (!(data[0] == 0xff && data[1] == 0xff)) {
		if (protocol == ETH_P_IP)
			netdev_dbg(dev, "     src=%pI4\n", &ih->saddr);
		else if (protocol == ETH_P_IPV6)
			netdev_dbg(dev, "     src=%pI6\n", &ih->saddr);
	}

	print_hex_dump_debug("", DUMP_PREFIX_NONE, 16, 1, data, len, false);
}

static inline int gdm_wimax_header(struct sk_buff **pskb)
{
	u16 buf[HCI_HEADER_SIZE / sizeof(u16)];
	struct hci_s *hci = (struct hci_s *)buf;
	struct sk_buff *skb = *pskb;

	if (unlikely(skb_headroom(skb) < HCI_HEADER_SIZE)) {
		struct sk_buff *skb2;

		skb2 = skb_realloc_headroom(skb, HCI_HEADER_SIZE);
		if (skb2 == NULL)
			return -ENOMEM;
		if (skb->sk)
			skb_set_owner_w(skb2, skb->sk);
		kfree_skb(skb);
		skb = skb2;
	}

	skb_push(skb, HCI_HEADER_SIZE);
	hci->cmd_evt = cpu_to_be16(WIMAX_TX_SDU);
	hci->length = cpu_to_be16(skb->len - HCI_HEADER_SIZE);
	memcpy(skb->data, buf, HCI_HEADER_SIZE);

	*pskb = skb;
	return 0;
}

static void gdm_wimax_event_rcv(struct net_device *dev, u16 type, void *msg,
				int len)
{
	struct nic *nic = netdev_priv(dev);

	u8 *buf = (u8 *)msg;
	u16 hci_cmd =  (buf[0]<<8) | buf[1];
	u16 hci_len = (buf[2]<<8) | buf[3];

	netdev_dbg(dev, "H=>D: 0x%04x(%d)\n", hci_cmd, hci_len);

	gdm_wimax_send(nic, msg, len);
}

static int gdm_wimax_event_init(void)
{
	if (!wm_event.ref_cnt) {
		wm_event.sock = netlink_init(NETLINK_WIMAX,
						gdm_wimax_event_rcv);
		if (wm_event.sock) {
			INIT_LIST_HEAD(&wm_event.evtq);
			INIT_LIST_HEAD(&wm_event.freeq);
			INIT_WORK(&wm_event.ws, __gdm_wimax_event_send);
			spin_lock_init(&wm_event.evt_lock);
		}
	}

	if (wm_event.sock) {
		wm_event.ref_cnt++;
		return 0;
	}

	pr_err("Creating WiMax Event netlink is failed\n");
	return -1;
}

static void gdm_wimax_event_exit(void)
{
	if (wm_event.sock && --wm_event.ref_cnt == 0) {
		struct evt_entry *e, *temp;
		unsigned long flags;

		spin_lock_irqsave(&wm_event.evt_lock, flags);

		list_for_each_entry_safe(e, temp, &wm_event.evtq, list) {
			list_del(&e->list);
			free_event_entry(e);
		}
		list_for_each_entry_safe(e, temp, &wm_event.freeq, list) {
			list_del(&e->list);
			free_event_entry(e);
		}

		spin_unlock_irqrestore(&wm_event.evt_lock, flags);
		netlink_exit(wm_event.sock);
		wm_event.sock = NULL;
	}
}

static inline struct evt_entry *alloc_event_entry(void)
{
	return kmalloc(sizeof(struct evt_entry), GFP_ATOMIC);
}

static inline void free_event_entry(struct evt_entry *e)
{
	kfree(e);
}

static struct evt_entry *get_event_entry(void)
{
	struct evt_entry *e;

	if (list_empty(&wm_event.freeq)) {
		e = alloc_event_entry();
	} else {
		e = list_entry(wm_event.freeq.next, struct evt_entry, list);
		list_del(&e->list);
	}

	return e;
}

static void put_event_entry(struct evt_entry *e)
{
	BUG_ON(!e);

	list_add_tail(&e->list, &wm_event.freeq);
}

static void __gdm_wimax_event_send(struct work_struct *work)
{
	int idx;
	unsigned long flags;
	struct evt_entry *e;

	spin_lock_irqsave(&wm_event.evt_lock, flags);

	while (!list_empty(&wm_event.evtq)) {
		e = list_entry(wm_event.evtq.next, struct evt_entry, list);
		spin_unlock_irqrestore(&wm_event.evt_lock, flags);

		if (sscanf(e->dev->name, "wm%d", &idx) == 1)
			netlink_send(wm_event.sock, idx, 0, e->evt_data,
				     e->size);

		spin_lock_irqsave(&wm_event.evt_lock, flags);
		list_del(&e->list);
		put_event_entry(e);
	}

	spin_unlock_irqrestore(&wm_event.evt_lock, flags);
}

static int gdm_wimax_event_send(struct net_device *dev, char *buf, int size)
{
	struct evt_entry *e;
	unsigned long flags;

	u16 hci_cmd =  ((u8)buf[0]<<8) | (u8)buf[1];
	u16 hci_len = ((u8)buf[2]<<8) | (u8)buf[3];

	netdev_dbg(dev, "D=>H: 0x%04x(%d)\n", hci_cmd, hci_len);

	spin_lock_irqsave(&wm_event.evt_lock, flags);

	e = get_event_entry();
	if (!e) {
		netdev_err(dev, "%s: No memory for event\n", __func__);
		spin_unlock_irqrestore(&wm_event.evt_lock, flags);
		return -ENOMEM;
	}

	e->dev = dev;
	e->size = size;
	memcpy(e->evt_data, buf, size);

	list_add_tail(&e->list, &wm_event.evtq);
	spin_unlock_irqrestore(&wm_event.evt_lock, flags);

	schedule_work(&wm_event.ws);

	return 0;
}

static void tx_complete(void *arg)
{
	struct nic *nic = arg;

	if (netif_queue_stopped(nic->netdev))
		netif_wake_queue(nic->netdev);
}

int gdm_wimax_send_tx(struct sk_buff *skb, struct net_device *dev)
{
	int ret = 0;
	struct nic *nic = netdev_priv(dev);

	ret = gdm_wimax_send_with_cb(nic, skb->data, skb->len, tx_complete,
				     nic);
	if (ret == -ENOSPC) {
		netif_stop_queue(dev);
		ret = 0;
	}

	if (ret) {
		skb_pull(skb, HCI_HEADER_SIZE);
		return ret;
	}

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len - HCI_HEADER_SIZE;
	kfree_skb(skb);
	return ret;
}

static int gdm_wimax_tx(struct sk_buff *skb, struct net_device *dev)
{
	int ret = 0;

	dump_eth_packet(dev, "TX", skb->data, skb->len);

	ret = gdm_wimax_header(&skb);
	if (ret < 0) {
		skb_pull(skb, HCI_HEADER_SIZE);
		return ret;
	}

#if defined(CONFIG_WIMAX_GDM72XX_QOS)
	ret = gdm_qos_send_hci_pkt(skb, dev);
#else
	ret = gdm_wimax_send_tx(skb, dev);
#endif
	return ret;
}

static int gdm_wimax_set_config(struct net_device *dev, struct ifmap *map)
{
	if (dev->flags & IFF_UP)
		return -EBUSY;

	return 0;
}

static void __gdm_wimax_set_mac_addr(struct net_device *dev, char *mac_addr)
{
	u16 hci_pkt_buf[32 / sizeof(u16)];
	struct hci_s *hci = (struct hci_s *)hci_pkt_buf;
	struct nic *nic = netdev_priv(dev);

	/* Since dev is registered as a ethernet device,
	 * ether_setup has made dev->addr_len to be ETH_ALEN
	 */
	memcpy(dev->dev_addr, mac_addr, dev->addr_len);

	/* Let lower layer know of this change by sending
	 * SetInformation(MAC Address)
	 */
	hci->cmd_evt = cpu_to_be16(WIMAX_SET_INFO);
	hci->length = cpu_to_be16(8);
	hci->data[0] = 0; /* T */
	hci->data[1] = 6; /* L */
	memcpy(&hci->data[2], mac_addr, dev->addr_len); /* V */

	gdm_wimax_send(nic, hci, HCI_HEADER_SIZE + 8);
}

/* A driver function */
static int gdm_wimax_set_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;

	if (netif_running(dev))
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	__gdm_wimax_set_mac_addr(dev, addr->sa_data);

	return 0;
}

static int gdm_wimax_open(struct net_device *dev)
{
	struct nic *nic = netdev_priv(dev);
	struct fsm_s *fsm = (struct fsm_s *)nic->sdk_data[SIOC_DATA_FSM].buf;

	netif_start_queue(dev);

	if (fsm && fsm->m_status != M_INIT)
		gdm_wimax_ind_if_updown(dev, 1);
	return 0;
}

static int gdm_wimax_close(struct net_device *dev)
{
	struct nic *nic = netdev_priv(dev);
	struct fsm_s *fsm = (struct fsm_s *)nic->sdk_data[SIOC_DATA_FSM].buf;

	netif_stop_queue(dev);

	if (fsm && fsm->m_status != M_INIT)
		gdm_wimax_ind_if_updown(dev, 0);
	return 0;
}

static void kdelete(void **buf)
{
	if (buf && *buf) {
		kfree(*buf);
		*buf = NULL;
	}
}

static int gdm_wimax_ioctl_get_data(struct data_s *dst, struct data_s *src)
{
	int size;

	size = dst->size < src->size ? dst->size : src->size;

	dst->size = size;
	if (src->size) {
		if (!dst->buf)
			return -EINVAL;
		if (copy_to_user((void __user *)dst->buf, src->buf, size))
			return -EFAULT;
	}
	return 0;
}

static int gdm_wimax_ioctl_set_data(struct data_s *dst, struct data_s *src)
{
	if (!src->size) {
		dst->size = 0;
		return 0;
	}

	if (!src->buf)
		return -EINVAL;

	if (!(dst->buf && dst->size == src->size)) {
		kdelete(&dst->buf);
		dst->buf = kmalloc(src->size, GFP_KERNEL);
		if (dst->buf == NULL)
			return -ENOMEM;
	}

	if (copy_from_user(dst->buf, (void __user *)src->buf, src->size)) {
		kdelete(&dst->buf);
		return -EFAULT;
	}
	dst->size = src->size;
	return 0;
}

static void gdm_wimax_cleanup_ioctl(struct net_device *dev)
{
	struct nic *nic = netdev_priv(dev);
	int i;

	for (i = 0; i < SIOC_DATA_MAX; i++)
		kdelete(&nic->sdk_data[i].buf);
}

static void gdm_update_fsm(struct net_device *dev, struct fsm_s *new_fsm)
{
	struct nic *nic = netdev_priv(dev);
	struct fsm_s *cur_fsm = (struct fsm_s *)
					nic->sdk_data[SIOC_DATA_FSM].buf;

	if (!cur_fsm)
		return;

	if (cur_fsm->m_status != new_fsm->m_status ||
	    cur_fsm->c_status != new_fsm->c_status) {
		if (new_fsm->m_status == M_CONNECTED) {
			netif_carrier_on(dev);
		} else if (cur_fsm->m_status == M_CONNECTED) {
			netif_carrier_off(dev);
			#if defined(CONFIG_WIMAX_GDM72XX_QOS)
			gdm_qos_release_list(nic);
			#endif
		}
		gdm_wimax_ind_fsm_update(dev, new_fsm);
	}
}

static int gdm_wimax_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct wm_req_s *req = (struct wm_req_s *)ifr;
	struct nic *nic = netdev_priv(dev);
	int ret;

	if (cmd != SIOCWMIOCTL)
		return -EOPNOTSUPP;

	switch (req->cmd) {
	case SIOCG_DATA:
	case SIOCS_DATA:
		if (req->data_id >= SIOC_DATA_MAX) {
			netdev_err(dev, "%s error: data-index(%d) is invalid!!\n",
				   __func__, req->data_id);
			return -EOPNOTSUPP;
		}
		if (req->cmd == SIOCG_DATA) {
			ret = gdm_wimax_ioctl_get_data(
				&req->data, &nic->sdk_data[req->data_id]);
			if (ret < 0)
				return ret;
		} else if (req->cmd == SIOCS_DATA) {
			if (req->data_id == SIOC_DATA_FSM) {
				/* NOTE: gdm_update_fsm should be called
				 * before gdm_wimax_ioctl_set_data is called.
				 */
				gdm_update_fsm(dev,
					       (struct fsm_s *)req->data.buf);
			}
			ret = gdm_wimax_ioctl_set_data(
				&nic->sdk_data[req->data_id], &req->data);
			if (ret < 0)
				return ret;
		}
		break;
	default:
		netdev_err(dev, "%s: %x unknown ioctl\n", __func__, cmd);
		return -EOPNOTSUPP;
	}

	return 0;
}

static void gdm_wimax_prepare_device(struct net_device *dev)
{
	struct nic *nic = netdev_priv(dev);
	u16 buf[32 / sizeof(u16)];
	struct hci_s *hci = (struct hci_s *)buf;
	u16 len = 0;
	u32 val = 0;
	__be32 val_be32;

	#define BIT_MULTI_CS	0
	#define BIT_WIMAX		1
	#define BIT_QOS			2
	#define BIT_AGGREGATION	3

	/* GetInformation mac address */
	len = 0;
	hci->cmd_evt = cpu_to_be16(WIMAX_GET_INFO);
	hci->data[len++] = TLV_T(T_MAC_ADDRESS);
	hci->length = cpu_to_be16(len);
	gdm_wimax_send(nic, hci, HCI_HEADER_SIZE+len);

	val = (1<<BIT_WIMAX) | (1<<BIT_MULTI_CS);
	#if defined(CONFIG_WIMAX_GDM72XX_QOS)
	val |= (1<<BIT_QOS);
	#endif
	#if defined(CONFIG_WIMAX_GDM72XX_WIMAX2)
	val |= (1<<BIT_AGGREGATION);
	#endif

	/* Set capability */
	len = 0;
	hci->cmd_evt = cpu_to_be16(WIMAX_SET_INFO);
	hci->data[len++] = TLV_T(T_CAPABILITY);
	hci->data[len++] = TLV_L(T_CAPABILITY);
	val_be32 = cpu_to_be32(val);
	memcpy(&hci->data[len], &val_be32, TLV_L(T_CAPABILITY));
	len += TLV_L(T_CAPABILITY);
	hci->length = cpu_to_be16(len);
	gdm_wimax_send(nic, hci, HCI_HEADER_SIZE+len);

	netdev_info(dev, "GDM WiMax Set CAPABILITY: 0x%08X\n", val);
}

static int gdm_wimax_hci_get_tlv(u8 *buf, u8 *T, u16 *L, u8 **V)
{
	#define __U82U16(b) ((u16)((u8 *)(b))[0] | ((u16)((u8 *)(b))[1] << 8))
	int next_pos;

	*T = buf[0];
	if (buf[1] == 0x82) {
		*L = be16_to_cpu(__U82U16(&buf[2]));
		next_pos = 1/*type*/+3/*len*/;
	} else {
		*L = buf[1];
		next_pos = 1/*type*/+1/*len*/;
	}
	*V = &buf[next_pos];

	next_pos += *L/*length of val*/;
	return next_pos;
}

static int gdm_wimax_get_prepared_info(struct net_device *dev, char *buf,
				       int len)
{
	u8 T, *V;
	u16 L;
	u16 cmd_evt, cmd_len;
	int pos = HCI_HEADER_SIZE;

	cmd_evt = be16_to_cpup((const __be16 *)&buf[0]);
	cmd_len = be16_to_cpup((const __be16 *)&buf[2]);

	if (len < cmd_len + HCI_HEADER_SIZE) {
		netdev_err(dev, "%s: invalid length [%d/%d]\n", __func__,
			   cmd_len + HCI_HEADER_SIZE, len);
		return -1;
	}

	if (cmd_evt == WIMAX_GET_INFO_RESULT) {
		if (cmd_len < 2) {
			netdev_err(dev, "%s: len is too short [%x/%d]\n",
				   __func__, cmd_evt, len);
			return -1;
		}

		pos += gdm_wimax_hci_get_tlv(&buf[pos], &T, &L, &V);
		if (T == TLV_T(T_MAC_ADDRESS)) {
			if (L != dev->addr_len) {
				netdev_err(dev,
					   "%s Invalid inofrmation result T/L [%x/%d]\n",
					   __func__, T, L);
				return -1;
			}
			netdev_info(dev, "MAC change [%pM]->[%pM]\n",
				    dev->dev_addr, V);
			memcpy(dev->dev_addr, V, dev->addr_len);
			return 1;
		}
	}

	gdm_wimax_event_send(dev, buf, len);
	return 0;
}

static void gdm_wimax_netif_rx(struct net_device *dev, char *buf, int len)
{
	struct sk_buff *skb;
	int ret;

	dump_eth_packet(dev, "RX", buf, len);

	skb = dev_alloc_skb(len + 2);
	if (!skb) {
		netdev_err(dev, "%s: dev_alloc_skb failed!\n", __func__);
		return;
	}
	skb_reserve(skb, 2);

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += len;

	memcpy(skb_put(skb, len), buf, len);

	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev); /* what will happen? */

	ret = in_interrupt() ? netif_rx(skb) : netif_rx_ni(skb);
	if (ret == NET_RX_DROP)
		netdev_err(dev, "%s skb dropped\n", __func__);
}

static void gdm_wimax_transmit_aggr_pkt(struct net_device *dev, char *buf,
					int len)
{
	#define HCI_PADDING_BYTE	4
	#define HCI_RESERVED_BYTE	4
	struct hci_s *hci;
	int length;

	while (len > 0) {
		hci = (struct hci_s *)buf;

		if (hci->cmd_evt != cpu_to_be16(WIMAX_RX_SDU)) {
			netdev_err(dev, "Wrong cmd_evt(0x%04X)\n",
				   be16_to_cpu(hci->cmd_evt));
			break;
		}

		length = be16_to_cpu(hci->length);
		gdm_wimax_netif_rx(dev, hci->data, length);

		if (length & 0x3) {
			/* Add padding size */
			length += HCI_PADDING_BYTE - (length & 0x3);
		}

		length += HCI_HEADER_SIZE + HCI_RESERVED_BYTE;
		len -= length;
		buf += length;
	}
}

static void gdm_wimax_transmit_pkt(struct net_device *dev, char *buf, int len)
{
	#if defined(CONFIG_WIMAX_GDM72XX_QOS)
	struct nic *nic = netdev_priv(dev);
	#endif
	u16 cmd_evt, cmd_len;

	/* This code is added for certain rx packet to be ignored. */
	if (len == 0)
		return;

	cmd_evt = be16_to_cpup((const __be16 *)&buf[0]);
	cmd_len = be16_to_cpup((const __be16 *)&buf[2]);

	if (len < cmd_len + HCI_HEADER_SIZE) {
		if (len)
			netdev_err(dev, "%s: invalid length [%d/%d]\n",
				   __func__, cmd_len + HCI_HEADER_SIZE, len);
		return;
	}

	switch (cmd_evt) {
	case WIMAX_RX_SDU_AGGR:
		gdm_wimax_transmit_aggr_pkt(dev, &buf[HCI_HEADER_SIZE],
					    cmd_len);
		break;
	case WIMAX_RX_SDU:
		gdm_wimax_netif_rx(dev, &buf[HCI_HEADER_SIZE], cmd_len);
		break;
	#if defined(CONFIG_WIMAX_GDM72XX_QOS)
	case WIMAX_EVT_MODEM_REPORT:
		gdm_recv_qos_hci_packet(nic, buf, len);
		break;
	#endif
	case WIMAX_SDU_TX_FLOW:
		if (buf[4] == 0) {
			if (!netif_queue_stopped(dev))
				netif_stop_queue(dev);
		} else if (buf[4] == 1) {
			if (netif_queue_stopped(dev))
				netif_wake_queue(dev);
		}
		break;
	default:
		gdm_wimax_event_send(dev, buf, len);
		break;
	}
}

static void gdm_wimax_ind_fsm_update(struct net_device *dev, struct fsm_s *fsm)
{
	u16 buf[32 / sizeof(u16)];
	struct hci_s *hci = (struct hci_s *)buf;

	/* Indicate updating fsm */
	hci->cmd_evt = cpu_to_be16(WIMAX_FSM_UPDATE);
	hci->length = cpu_to_be16(sizeof(struct fsm_s));
	memcpy(&hci->data[0], fsm, sizeof(struct fsm_s));

	gdm_wimax_event_send(dev, (char *)hci,
			     HCI_HEADER_SIZE + sizeof(struct fsm_s));
}

static void gdm_wimax_ind_if_updown(struct net_device *dev, int if_up)
{
	u16 buf[32 / sizeof(u16)];
	struct hci_s *hci = (struct hci_s *)buf;
	unsigned char up_down;

	up_down = if_up ? WIMAX_IF_UP : WIMAX_IF_DOWN;

	/* Indicate updating fsm */
	hci->cmd_evt = cpu_to_be16(WIMAX_IF_UPDOWN);
	hci->length = cpu_to_be16(sizeof(up_down));
	hci->data[0] = up_down;

	gdm_wimax_event_send(dev, (char *)hci, HCI_HEADER_SIZE+sizeof(up_down));
}

static void rx_complete(void *arg, void *data, int len)
{
	struct nic *nic = arg;

	gdm_wimax_transmit_pkt(nic->netdev, data, len);
	gdm_wimax_rcv_with_cb(nic, rx_complete, nic);
}

static void prepare_rx_complete(void *arg, void *data, int len)
{
	struct nic *nic = arg;
	int ret;

	ret = gdm_wimax_get_prepared_info(nic->netdev, data, len);
	if (ret == 1) {
		gdm_wimax_rcv_with_cb(nic, rx_complete, nic);
	} else {
		if (ret < 0)
			netdev_err(nic->netdev,
				   "get_prepared_info failed(%d)\n", ret);
		gdm_wimax_rcv_with_cb(nic, prepare_rx_complete, nic);
	}
}

static void start_rx_proc(struct nic *nic)
{
	gdm_wimax_rcv_with_cb(nic, prepare_rx_complete, nic);
}

static struct net_device_ops gdm_netdev_ops = {
	.ndo_open		= gdm_wimax_open,
	.ndo_stop		= gdm_wimax_close,
	.ndo_set_config		= gdm_wimax_set_config,
	.ndo_start_xmit		= gdm_wimax_tx,
	.ndo_set_mac_address	= gdm_wimax_set_mac_addr,
	.ndo_do_ioctl		= gdm_wimax_ioctl,
};

int register_wimax_device(struct phy_dev *phy_dev, struct device *pdev)
{
	struct nic *nic = NULL;
	struct net_device *dev;
	int ret;

	dev = alloc_netdev(sizeof(*nic), "wm%d", ether_setup);

	if (dev == NULL) {
		pr_err("alloc_etherdev failed\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(dev, pdev);
	dev->mtu = 1400;
	dev->netdev_ops = &gdm_netdev_ops;
	dev->flags &= ~IFF_MULTICAST;
	memcpy(dev->dev_addr, gdm_wimax_macaddr, sizeof(gdm_wimax_macaddr));

	nic = netdev_priv(dev);
	nic->netdev = dev;
	nic->phy_dev = phy_dev;
	phy_dev->netdev = dev;

	/* event socket init */
	ret = gdm_wimax_event_init();
	if (ret < 0) {
		pr_err("Cannot create event.\n");
		goto cleanup;
	}

	ret = register_netdev(dev);
	if (ret)
		goto cleanup;

	netif_carrier_off(dev);

#ifdef CONFIG_WIMAX_GDM72XX_QOS
	gdm_qos_init(nic);
#endif

	start_rx_proc(nic);

	/* Prepare WiMax device */
	gdm_wimax_prepare_device(dev);

	return 0;

cleanup:
	pr_err("register_netdev failed\n");
	free_netdev(dev);
	return ret;
}

void unregister_wimax_device(struct phy_dev *phy_dev)
{
	struct nic *nic = netdev_priv(phy_dev->netdev);
	struct fsm_s *fsm = (struct fsm_s *)nic->sdk_data[SIOC_DATA_FSM].buf;

	if (fsm)
		fsm->m_status = M_INIT;
	unregister_netdev(nic->netdev);

	gdm_wimax_event_exit();

#if defined(CONFIG_WIMAX_GDM72XX_QOS)
	gdm_qos_release_list(nic);
#endif

	gdm_wimax_cleanup_ioctl(phy_dev->netdev);

	free_netdev(nic->netdev);
}
