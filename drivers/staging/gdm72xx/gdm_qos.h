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

#if !defined(GDM_QOS_H_20090403)
#define GDM_QOS_H_20090403

#include <linux/types.h>
#include <linux/usb.h>
#include <linux/list.h>

#define QOS_MAX				16
#define IPTYPEOFSERVICE			0x8000
#define	PROTOCOL			0x4000
#define	IPMASKEDSRCADDRESS		0x2000
#define	IPMASKEDDSTADDRESS		0x1000
#define	PROTOCOLSRCPORTRANGE		0x800
#define	PROTOCOLDSTPORTRANGE		0x400
#define	DSTMACADDR			0x200
#define	SRCMACADDR			0x100
#define	ETHERTYPE			0x80
#define	IEEE802_1DUSERPRIORITY		0x40
#define	IEEE802_1QVLANID		0x10

struct gdm_wimax_csr_s {
	bool		enabled;
	u32		SFID;
	u8		qos_buf_count;
	u16		classifier_rule_en;
	u8		ip2s_lo;
	u8		ip2s_hi;
	u8		ip2s_mask;
	u8		protocol;
	u8		ipsrc_addr[16];
	u8		ipsrc_addrmask[16];
	u8		ipdst_addr[16];
	u8		ipdst_addrmask[16];
	u16		srcport_lo;
	u16		srcport_hi;
	u16		dstport_lo;
	u16		dstport_hi;
};

struct qos_entry_s {
	struct list_head	list;
	struct sk_buff		*skb;
	struct net_device	*dev;

};

struct qos_cb_s {
	struct list_head	qos_list[QOS_MAX];
	u32			qos_list_cnt;
	u32			qos_null_idx;
	struct gdm_wimax_csr_s	csr[QOS_MAX];
	spinlock_t		qos_lock;
	u32			qos_limit_size;
};

void gdm_qos_init(void *nic_ptr);
void gdm_qos_release_list(void *nic_ptr);
int gdm_qos_send_hci_pkt(struct sk_buff *skb, struct net_device *dev);
void gdm_recv_qos_hci_packet(void *nic_ptr, u8 *buf, int size);

#endif
