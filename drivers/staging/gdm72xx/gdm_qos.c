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
#include <linux/tcp.h>
#include <linux/if_ether.h>

#include "gdm_wimax.h"
#include "hci.h"
#include "gdm_qos.h"

#define B2H(x)		__be16_to_cpu(x)

#define MAX_FREE_LIST_CNT		32
static struct {
	struct list_head head;
	int cnt;
	spinlock_t lock;
} qos_free_list;

static void init_qos_entry_list(void)
{
	qos_free_list.cnt = 0;
	INIT_LIST_HEAD(&qos_free_list.head);
	spin_lock_init(&qos_free_list.lock);
}

static void *alloc_qos_entry(void)
{
	struct qos_entry_s *entry;
	unsigned long flags;

	spin_lock_irqsave(&qos_free_list.lock, flags);
	if (qos_free_list.cnt) {
		entry = list_entry(qos_free_list.head.prev, struct qos_entry_s,
					list);
		list_del(&entry->list);
		qos_free_list.cnt--;
		spin_unlock_irqrestore(&qos_free_list.lock, flags);
		return entry;
	}
	spin_unlock_irqrestore(&qos_free_list.lock, flags);

	entry = kmalloc(sizeof(struct qos_entry_s), GFP_ATOMIC);
	return entry;
}

static void free_qos_entry(void *entry)
{
	struct qos_entry_s *qentry = (struct qos_entry_s *) entry;
	unsigned long flags;

	spin_lock_irqsave(&qos_free_list.lock, flags);
	if (qos_free_list.cnt < MAX_FREE_LIST_CNT) {
		list_add(&qentry->list, &qos_free_list.head);
		qos_free_list.cnt++;
		spin_unlock_irqrestore(&qos_free_list.lock, flags);
		return;
	}
	spin_unlock_irqrestore(&qos_free_list.lock, flags);

	kfree(entry);
}

static void free_qos_entry_list(struct list_head *free_list)
{
	struct qos_entry_s *entry, *n;
	int total_free = 0;

	list_for_each_entry_safe(entry, n, free_list, list) {
		list_del(&entry->list);
		kfree(entry);
		total_free++;
	}

	pr_debug("%s: total_free_cnt=%d\n", __func__, total_free);
}

void gdm_qos_init(void *nic_ptr)
{
	struct nic *nic = nic_ptr;
	struct qos_cb_s *qcb = &nic->qos;
	int i;

	for (i = 0; i < QOS_MAX; i++) {
		INIT_LIST_HEAD(&qcb->qos_list[i]);
		qcb->csr[i].qos_buf_count = 0;
		qcb->csr[i].enabled = 0;
	}

	qcb->qos_list_cnt = 0;
	qcb->qos_null_idx = QOS_MAX-1;
	qcb->qos_limit_size = 255;

	spin_lock_init(&qcb->qos_lock);

	init_qos_entry_list();
}

void gdm_qos_release_list(void *nic_ptr)
{
	struct nic *nic = nic_ptr;
	struct qos_cb_s *qcb = &nic->qos;
	unsigned long flags;
	struct qos_entry_s *entry, *n;
	struct list_head free_list;
	int i;

	INIT_LIST_HEAD(&free_list);

	spin_lock_irqsave(&qcb->qos_lock, flags);

	for (i = 0; i < QOS_MAX; i++) {
		qcb->csr[i].qos_buf_count = 0;
		qcb->csr[i].enabled = 0;
	}

	qcb->qos_list_cnt = 0;
	qcb->qos_null_idx = QOS_MAX-1;

	for (i = 0; i < QOS_MAX; i++) {
		list_for_each_entry_safe(entry, n, &qcb->qos_list[i], list) {
			list_move_tail(&entry->list, &free_list);
		}
	}
	spin_unlock_irqrestore(&qcb->qos_lock, flags);
	free_qos_entry_list(&free_list);
}

static u32 chk_ipv4_rule(struct gdm_wimax_csr_s *csr, u8 *Stream, u8 *port)
{
	int i;

	if (csr->classifier_rule_en&IPTYPEOFSERVICE) {
		if (((Stream[1] & csr->ip2s_mask) < csr->ip2s_lo) ||
		((Stream[1] & csr->ip2s_mask) > csr->ip2s_hi))
			return 1;
	}

	if (csr->classifier_rule_en&PROTOCOL) {
		if (Stream[9] != csr->protocol)
			return 1;
	}

	if (csr->classifier_rule_en&IPMASKEDSRCADDRESS) {
		for (i = 0; i < 4; i++) {
			if ((Stream[12 + i] & csr->ipsrc_addrmask[i]) !=
			(csr->ipsrc_addr[i] & csr->ipsrc_addrmask[i]))
				return 1;
		}
	}

	if (csr->classifier_rule_en&IPMASKEDDSTADDRESS) {
		for (i = 0; i < 4; i++) {
			if ((Stream[16 + i] & csr->ipdst_addrmask[i]) !=
			(csr->ipdst_addr[i] & csr->ipdst_addrmask[i]))
				return 1;
		}
	}

	if (csr->classifier_rule_en&PROTOCOLSRCPORTRANGE) {
		i = ((port[0]<<8)&0xff00)+port[1];
		if ((i < csr->srcport_lo) || (i > csr->srcport_hi))
			return 1;
	}

	if (csr->classifier_rule_en&PROTOCOLDSTPORTRANGE) {
		i = ((port[2]<<8)&0xff00)+port[3];
		if ((i < csr->dstport_lo) || (i > csr->dstport_hi))
			return 1;
	}

	return 0;
}

static u32 get_qos_index(struct nic *nic, u8 *iph, u8 *tcpudph)
{
	u32	IP_Ver, Header_Len, i;
	struct qos_cb_s *qcb = &nic->qos;

	if (iph == NULL || tcpudph == NULL)
		return -1;

	IP_Ver = (iph[0]>>4)&0xf;
	Header_Len = iph[0]&0xf;

	if (IP_Ver == 4) {
		for (i = 0; i < QOS_MAX; i++) {
			if (qcb->csr[i].enabled) {
				if (qcb->csr[i].classifier_rule_en) {
					if (chk_ipv4_rule(&qcb->csr[i], iph,
					tcpudph) == 0)
						return i;
				}
			}
		}
	}

	return -1;
}

static u32 extract_qos_list(struct nic *nic, struct list_head *head)
{
	struct qos_cb_s *qcb = &nic->qos;
	struct qos_entry_s *entry;
	int i;

	INIT_LIST_HEAD(head);

	for (i = 0; i < QOS_MAX; i++) {
		if (qcb->csr[i].enabled) {
			if (qcb->csr[i].qos_buf_count < qcb->qos_limit_size) {
				if (!list_empty(&qcb->qos_list[i])) {
					entry = list_entry(
					qcb->qos_list[i].prev,
					struct qos_entry_s, list);
					list_move_tail(&entry->list, head);
					qcb->csr[i].qos_buf_count++;

					if (!list_empty(&qcb->qos_list[i]))
						netdev_warn(nic->netdev,
							    "Index(%d) is piled!!\n",
							    i);
				}
			}
		}
	}

	return 0;
}

static void send_qos_list(struct nic *nic, struct list_head *head)
{
	struct qos_entry_s *entry, *n;

	list_for_each_entry_safe(entry, n, head, list) {
		list_del(&entry->list);
		gdm_wimax_send_tx(entry->skb, entry->dev);
		free_qos_entry(entry);
	}
}

int gdm_qos_send_hci_pkt(struct sk_buff *skb, struct net_device *dev)
{
	struct nic *nic = netdev_priv(dev);
	int index;
	struct qos_cb_s *qcb = &nic->qos;
	unsigned long flags;
	struct ethhdr *ethh = (struct ethhdr *) (skb->data + HCI_HEADER_SIZE);
	struct iphdr *iph = (struct iphdr *) ((char *) ethh + ETH_HLEN);
	struct tcphdr *tcph;
	struct qos_entry_s *entry = NULL;
	struct list_head send_list;
	int ret = 0;

	tcph = (struct tcphdr *) iph + iph->ihl*4;

	if (B2H(ethh->h_proto) == ETH_P_IP) {
		if (qcb->qos_list_cnt && !qos_free_list.cnt) {
			entry = alloc_qos_entry();
			entry->skb = skb;
			entry->dev = dev;
			netdev_dbg(dev, "qcb->qos_list_cnt=%d\n",
				   qcb->qos_list_cnt);
		}

		spin_lock_irqsave(&qcb->qos_lock, flags);
		if (qcb->qos_list_cnt) {
			index = get_qos_index(nic, (u8 *)iph, (u8 *) tcph);
			if (index == -1)
				index = qcb->qos_null_idx;

			if (!entry) {
				entry = alloc_qos_entry();
				entry->skb = skb;
				entry->dev = dev;
			}

			list_add_tail(&entry->list, &qcb->qos_list[index]);
			extract_qos_list(nic, &send_list);
			spin_unlock_irqrestore(&qcb->qos_lock, flags);
			send_qos_list(nic, &send_list);
			goto out;
		}
		spin_unlock_irqrestore(&qcb->qos_lock, flags);
		if (entry)
			free_qos_entry(entry);
	}

	ret = gdm_wimax_send_tx(skb, dev);
out:
	return ret;
}

static u32 get_csr(struct qos_cb_s *qcb, u32 SFID, int mode)
{
	int i;

	for (i = 0; i < qcb->qos_list_cnt; i++) {
		if (qcb->csr[i].SFID == SFID)
			return i;
	}

	if (mode) {
		for (i = 0; i < QOS_MAX; i++) {
			if (qcb->csr[i].enabled == 0) {
				qcb->csr[i].enabled = 1;
				qcb->qos_list_cnt++;
				return i;
			}
		}
	}
	return -1;
}

#define QOS_CHANGE_DEL	0xFC
#define QOS_ADD		0xFD
#define QOS_REPORT	0xFE

void gdm_recv_qos_hci_packet(void *nic_ptr, u8 *buf, int size)
{
	struct nic *nic = nic_ptr;
	u32 i, SFID, index, pos;
	u8 subCmdEvt;
	struct qos_cb_s *qcb = &nic->qos;
	struct qos_entry_s *entry, *n;
	struct list_head send_list;
	struct list_head free_list;
	unsigned long flags;

	subCmdEvt = (u8)buf[4];

	if (subCmdEvt == QOS_REPORT) {
		spin_lock_irqsave(&qcb->qos_lock, flags);
		for (i = 0; i < qcb->qos_list_cnt; i++) {
			SFID = ((buf[(i*5)+6]<<24)&0xff000000);
			SFID += ((buf[(i*5)+7]<<16)&0xff0000);
			SFID += ((buf[(i*5)+8]<<8)&0xff00);
			SFID += (buf[(i*5)+9]);
			index = get_csr(qcb, SFID, 0);
			if (index == -1) {
				spin_unlock_irqrestore(&qcb->qos_lock, flags);
				netdev_err(nic->netdev, "QoS ERROR: No SF\n");
				return;
			}
			qcb->csr[index].qos_buf_count = buf[(i*5)+10];
		}

		extract_qos_list(nic, &send_list);
		spin_unlock_irqrestore(&qcb->qos_lock, flags);
		send_qos_list(nic, &send_list);
		return;
	}

	/* subCmdEvt == QOS_ADD || subCmdEvt == QOS_CHANG_DEL */
	pos = 6;
	SFID = ((buf[pos++]<<24)&0xff000000);
	SFID += ((buf[pos++]<<16)&0xff0000);
	SFID += ((buf[pos++]<<8)&0xff00);
	SFID += (buf[pos++]);

	index = get_csr(qcb, SFID, 1);
	if (index == -1) {
		netdev_err(nic->netdev,
			   "QoS ERROR: csr Update Error / Wrong index (%d) \n",
			   index);
		return;
	}

	if (subCmdEvt == QOS_ADD) {
		netdev_dbg(nic->netdev, "QOS_ADD SFID = 0x%x, index=%d\n",
			   SFID, index);

		spin_lock_irqsave(&qcb->qos_lock, flags);
		qcb->csr[index].SFID = SFID;
		qcb->csr[index].classifier_rule_en = ((buf[pos++]<<8)&0xff00);
		qcb->csr[index].classifier_rule_en += buf[pos++];
		if (qcb->csr[index].classifier_rule_en == 0)
			qcb->qos_null_idx = index;
		qcb->csr[index].ip2s_mask = buf[pos++];
		qcb->csr[index].ip2s_lo = buf[pos++];
		qcb->csr[index].ip2s_hi = buf[pos++];
		qcb->csr[index].protocol = buf[pos++];
		qcb->csr[index].ipsrc_addrmask[0] = buf[pos++];
		qcb->csr[index].ipsrc_addrmask[1] = buf[pos++];
		qcb->csr[index].ipsrc_addrmask[2] = buf[pos++];
		qcb->csr[index].ipsrc_addrmask[3] = buf[pos++];
		qcb->csr[index].ipsrc_addr[0] = buf[pos++];
		qcb->csr[index].ipsrc_addr[1] = buf[pos++];
		qcb->csr[index].ipsrc_addr[2] = buf[pos++];
		qcb->csr[index].ipsrc_addr[3] = buf[pos++];
		qcb->csr[index].ipdst_addrmask[0] = buf[pos++];
		qcb->csr[index].ipdst_addrmask[1] = buf[pos++];
		qcb->csr[index].ipdst_addrmask[2] = buf[pos++];
		qcb->csr[index].ipdst_addrmask[3] = buf[pos++];
		qcb->csr[index].ipdst_addr[0] = buf[pos++];
		qcb->csr[index].ipdst_addr[1] = buf[pos++];
		qcb->csr[index].ipdst_addr[2] = buf[pos++];
		qcb->csr[index].ipdst_addr[3] = buf[pos++];
		qcb->csr[index].srcport_lo = ((buf[pos++]<<8)&0xff00);
		qcb->csr[index].srcport_lo += buf[pos++];
		qcb->csr[index].srcport_hi = ((buf[pos++]<<8)&0xff00);
		qcb->csr[index].srcport_hi += buf[pos++];
		qcb->csr[index].dstport_lo = ((buf[pos++]<<8)&0xff00);
		qcb->csr[index].dstport_lo += buf[pos++];
		qcb->csr[index].dstport_hi = ((buf[pos++]<<8)&0xff00);
		qcb->csr[index].dstport_hi += buf[pos++];

		qcb->qos_limit_size = 254/qcb->qos_list_cnt;
		spin_unlock_irqrestore(&qcb->qos_lock, flags);
	} else if (subCmdEvt == QOS_CHANGE_DEL) {
		netdev_dbg(nic->netdev, "QOS_CHANGE_DEL SFID = 0x%x, index=%d\n",
			   SFID, index);

		INIT_LIST_HEAD(&free_list);

		spin_lock_irqsave(&qcb->qos_lock, flags);
		qcb->csr[index].enabled = 0;
		qcb->qos_list_cnt--;
		qcb->qos_limit_size = 254/qcb->qos_list_cnt;

		list_for_each_entry_safe(entry, n, &qcb->qos_list[index],
					list) {
			list_move_tail(&entry->list, &free_list);
		}
		spin_unlock_irqrestore(&qcb->qos_lock, flags);
		free_qos_entry_list(&free_list);
	}
}
