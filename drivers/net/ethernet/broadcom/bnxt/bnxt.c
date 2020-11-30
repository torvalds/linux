/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 * Copyright (c) 2016-2019 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>

#include <linux/stringify.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include <asm/page.h>
#include <linux/time.h>
#include <linux/mii.h>
#include <linux/mdio.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/if_bridge.h>
#include <linux/rtc.h>
#include <linux/bpf.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <net/udp_tunnel.h>
#include <linux/workqueue.h>
#include <linux/prefetch.h>
#include <linux/cache.h>
#include <linux/log2.h>
#include <linux/aer.h>
#include <linux/bitmap.h>
#include <linux/cpu_rmap.h>
#include <linux/cpumask.h>
#include <net/pkt_cls.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <net/page_pool.h>

#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_ulp.h"
#include "bnxt_sriov.h"
#include "bnxt_ethtool.h"
#include "bnxt_dcb.h"
#include "bnxt_xdp.h"
#include "bnxt_vfr.h"
#include "bnxt_tc.h"
#include "bnxt_devlink.h"
#include "bnxt_debugfs.h"

#define BNXT_TX_TIMEOUT		(5 * HZ)
#define BNXT_DEF_MSG_ENABLE	(NETIF_MSG_DRV | NETIF_MSG_HW)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Broadcom BCM573xx network driver");

#define BNXT_RX_OFFSET (NET_SKB_PAD + NET_IP_ALIGN)
#define BNXT_RX_DMA_OFFSET NET_SKB_PAD
#define BNXT_RX_COPY_THRESH 256

#define BNXT_TX_PUSH_THRESH 164

enum board_idx {
	BCM57301,
	BCM57302,
	BCM57304,
	BCM57417_NPAR,
	BCM58700,
	BCM57311,
	BCM57312,
	BCM57402,
	BCM57404,
	BCM57406,
	BCM57402_NPAR,
	BCM57407,
	BCM57412,
	BCM57414,
	BCM57416,
	BCM57417,
	BCM57412_NPAR,
	BCM57314,
	BCM57417_SFP,
	BCM57416_SFP,
	BCM57404_NPAR,
	BCM57406_NPAR,
	BCM57407_SFP,
	BCM57407_NPAR,
	BCM57414_NPAR,
	BCM57416_NPAR,
	BCM57452,
	BCM57454,
	BCM5745x_NPAR,
	BCM57508,
	BCM57504,
	BCM57502,
	BCM57508_NPAR,
	BCM57504_NPAR,
	BCM57502_NPAR,
	BCM58802,
	BCM58804,
	BCM58808,
	NETXTREME_E_VF,
	NETXTREME_C_VF,
	NETXTREME_S_VF,
	NETXTREME_E_P5_VF,
};

/* indexed by enum above */
static const struct {
	char *name;
} board_info[] = {
	[BCM57301] = { "Broadcom BCM57301 NetXtreme-C 10Gb Ethernet" },
	[BCM57302] = { "Broadcom BCM57302 NetXtreme-C 10Gb/25Gb Ethernet" },
	[BCM57304] = { "Broadcom BCM57304 NetXtreme-C 10Gb/25Gb/40Gb/50Gb Ethernet" },
	[BCM57417_NPAR] = { "Broadcom BCM57417 NetXtreme-E Ethernet Partition" },
	[BCM58700] = { "Broadcom BCM58700 Nitro 1Gb/2.5Gb/10Gb Ethernet" },
	[BCM57311] = { "Broadcom BCM57311 NetXtreme-C 10Gb Ethernet" },
	[BCM57312] = { "Broadcom BCM57312 NetXtreme-C 10Gb/25Gb Ethernet" },
	[BCM57402] = { "Broadcom BCM57402 NetXtreme-E 10Gb Ethernet" },
	[BCM57404] = { "Broadcom BCM57404 NetXtreme-E 10Gb/25Gb Ethernet" },
	[BCM57406] = { "Broadcom BCM57406 NetXtreme-E 10GBase-T Ethernet" },
	[BCM57402_NPAR] = { "Broadcom BCM57402 NetXtreme-E Ethernet Partition" },
	[BCM57407] = { "Broadcom BCM57407 NetXtreme-E 10GBase-T Ethernet" },
	[BCM57412] = { "Broadcom BCM57412 NetXtreme-E 10Gb Ethernet" },
	[BCM57414] = { "Broadcom BCM57414 NetXtreme-E 10Gb/25Gb Ethernet" },
	[BCM57416] = { "Broadcom BCM57416 NetXtreme-E 10GBase-T Ethernet" },
	[BCM57417] = { "Broadcom BCM57417 NetXtreme-E 10GBase-T Ethernet" },
	[BCM57412_NPAR] = { "Broadcom BCM57412 NetXtreme-E Ethernet Partition" },
	[BCM57314] = { "Broadcom BCM57314 NetXtreme-C 10Gb/25Gb/40Gb/50Gb Ethernet" },
	[BCM57417_SFP] = { "Broadcom BCM57417 NetXtreme-E 10Gb/25Gb Ethernet" },
	[BCM57416_SFP] = { "Broadcom BCM57416 NetXtreme-E 10Gb Ethernet" },
	[BCM57404_NPAR] = { "Broadcom BCM57404 NetXtreme-E Ethernet Partition" },
	[BCM57406_NPAR] = { "Broadcom BCM57406 NetXtreme-E Ethernet Partition" },
	[BCM57407_SFP] = { "Broadcom BCM57407 NetXtreme-E 25Gb Ethernet" },
	[BCM57407_NPAR] = { "Broadcom BCM57407 NetXtreme-E Ethernet Partition" },
	[BCM57414_NPAR] = { "Broadcom BCM57414 NetXtreme-E Ethernet Partition" },
	[BCM57416_NPAR] = { "Broadcom BCM57416 NetXtreme-E Ethernet Partition" },
	[BCM57452] = { "Broadcom BCM57452 NetXtreme-E 10Gb/25Gb/40Gb/50Gb Ethernet" },
	[BCM57454] = { "Broadcom BCM57454 NetXtreme-E 10Gb/25Gb/40Gb/50Gb/100Gb Ethernet" },
	[BCM5745x_NPAR] = { "Broadcom BCM5745x NetXtreme-E Ethernet Partition" },
	[BCM57508] = { "Broadcom BCM57508 NetXtreme-E 10Gb/25Gb/50Gb/100Gb/200Gb Ethernet" },
	[BCM57504] = { "Broadcom BCM57504 NetXtreme-E 10Gb/25Gb/50Gb/100Gb/200Gb Ethernet" },
	[BCM57502] = { "Broadcom BCM57502 NetXtreme-E 10Gb/25Gb/50Gb Ethernet" },
	[BCM57508_NPAR] = { "Broadcom BCM57508 NetXtreme-E Ethernet Partition" },
	[BCM57504_NPAR] = { "Broadcom BCM57504 NetXtreme-E Ethernet Partition" },
	[BCM57502_NPAR] = { "Broadcom BCM57502 NetXtreme-E Ethernet Partition" },
	[BCM58802] = { "Broadcom BCM58802 NetXtreme-S 10Gb/25Gb/40Gb/50Gb Ethernet" },
	[BCM58804] = { "Broadcom BCM58804 NetXtreme-S 10Gb/25Gb/40Gb/50Gb/100Gb Ethernet" },
	[BCM58808] = { "Broadcom BCM58808 NetXtreme-S 10Gb/25Gb/40Gb/50Gb/100Gb Ethernet" },
	[NETXTREME_E_VF] = { "Broadcom NetXtreme-E Ethernet Virtual Function" },
	[NETXTREME_C_VF] = { "Broadcom NetXtreme-C Ethernet Virtual Function" },
	[NETXTREME_S_VF] = { "Broadcom NetXtreme-S Ethernet Virtual Function" },
	[NETXTREME_E_P5_VF] = { "Broadcom BCM5750X NetXtreme-E Ethernet Virtual Function" },
};

static const struct pci_device_id bnxt_pci_tbl[] = {
	{ PCI_VDEVICE(BROADCOM, 0x1604), .driver_data = BCM5745x_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x1605), .driver_data = BCM5745x_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x1614), .driver_data = BCM57454 },
	{ PCI_VDEVICE(BROADCOM, 0x16c0), .driver_data = BCM57417_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x16c8), .driver_data = BCM57301 },
	{ PCI_VDEVICE(BROADCOM, 0x16c9), .driver_data = BCM57302 },
	{ PCI_VDEVICE(BROADCOM, 0x16ca), .driver_data = BCM57304 },
	{ PCI_VDEVICE(BROADCOM, 0x16cc), .driver_data = BCM57417_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x16cd), .driver_data = BCM58700 },
	{ PCI_VDEVICE(BROADCOM, 0x16ce), .driver_data = BCM57311 },
	{ PCI_VDEVICE(BROADCOM, 0x16cf), .driver_data = BCM57312 },
	{ PCI_VDEVICE(BROADCOM, 0x16d0), .driver_data = BCM57402 },
	{ PCI_VDEVICE(BROADCOM, 0x16d1), .driver_data = BCM57404 },
	{ PCI_VDEVICE(BROADCOM, 0x16d2), .driver_data = BCM57406 },
	{ PCI_VDEVICE(BROADCOM, 0x16d4), .driver_data = BCM57402_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x16d5), .driver_data = BCM57407 },
	{ PCI_VDEVICE(BROADCOM, 0x16d6), .driver_data = BCM57412 },
	{ PCI_VDEVICE(BROADCOM, 0x16d7), .driver_data = BCM57414 },
	{ PCI_VDEVICE(BROADCOM, 0x16d8), .driver_data = BCM57416 },
	{ PCI_VDEVICE(BROADCOM, 0x16d9), .driver_data = BCM57417 },
	{ PCI_VDEVICE(BROADCOM, 0x16de), .driver_data = BCM57412_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x16df), .driver_data = BCM57314 },
	{ PCI_VDEVICE(BROADCOM, 0x16e2), .driver_data = BCM57417_SFP },
	{ PCI_VDEVICE(BROADCOM, 0x16e3), .driver_data = BCM57416_SFP },
	{ PCI_VDEVICE(BROADCOM, 0x16e7), .driver_data = BCM57404_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x16e8), .driver_data = BCM57406_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x16e9), .driver_data = BCM57407_SFP },
	{ PCI_VDEVICE(BROADCOM, 0x16ea), .driver_data = BCM57407_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x16eb), .driver_data = BCM57412_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x16ec), .driver_data = BCM57414_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x16ed), .driver_data = BCM57414_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x16ee), .driver_data = BCM57416_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x16ef), .driver_data = BCM57416_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x16f0), .driver_data = BCM58808 },
	{ PCI_VDEVICE(BROADCOM, 0x16f1), .driver_data = BCM57452 },
	{ PCI_VDEVICE(BROADCOM, 0x1750), .driver_data = BCM57508 },
	{ PCI_VDEVICE(BROADCOM, 0x1751), .driver_data = BCM57504 },
	{ PCI_VDEVICE(BROADCOM, 0x1752), .driver_data = BCM57502 },
	{ PCI_VDEVICE(BROADCOM, 0x1800), .driver_data = BCM57508_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x1801), .driver_data = BCM57504_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x1802), .driver_data = BCM57502_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x1803), .driver_data = BCM57508_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x1804), .driver_data = BCM57504_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x1805), .driver_data = BCM57502_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0xd802), .driver_data = BCM58802 },
	{ PCI_VDEVICE(BROADCOM, 0xd804), .driver_data = BCM58804 },
#ifdef CONFIG_BNXT_SRIOV
	{ PCI_VDEVICE(BROADCOM, 0x1606), .driver_data = NETXTREME_E_VF },
	{ PCI_VDEVICE(BROADCOM, 0x1609), .driver_data = NETXTREME_E_VF },
	{ PCI_VDEVICE(BROADCOM, 0x16c1), .driver_data = NETXTREME_E_VF },
	{ PCI_VDEVICE(BROADCOM, 0x16cb), .driver_data = NETXTREME_C_VF },
	{ PCI_VDEVICE(BROADCOM, 0x16d3), .driver_data = NETXTREME_E_VF },
	{ PCI_VDEVICE(BROADCOM, 0x16dc), .driver_data = NETXTREME_E_VF },
	{ PCI_VDEVICE(BROADCOM, 0x16e1), .driver_data = NETXTREME_C_VF },
	{ PCI_VDEVICE(BROADCOM, 0x16e5), .driver_data = NETXTREME_C_VF },
	{ PCI_VDEVICE(BROADCOM, 0x1806), .driver_data = NETXTREME_E_P5_VF },
	{ PCI_VDEVICE(BROADCOM, 0x1807), .driver_data = NETXTREME_E_P5_VF },
	{ PCI_VDEVICE(BROADCOM, 0xd800), .driver_data = NETXTREME_S_VF },
#endif
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, bnxt_pci_tbl);

static const u16 bnxt_vf_req_snif[] = {
	HWRM_FUNC_CFG,
	HWRM_FUNC_VF_CFG,
	HWRM_PORT_PHY_QCFG,
	HWRM_CFA_L2_FILTER_ALLOC,
};

static const u16 bnxt_async_events_arr[] = {
	ASYNC_EVENT_CMPL_EVENT_ID_LINK_STATUS_CHANGE,
	ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CHANGE,
	ASYNC_EVENT_CMPL_EVENT_ID_PF_DRVR_UNLOAD,
	ASYNC_EVENT_CMPL_EVENT_ID_PORT_CONN_NOT_ALLOWED,
	ASYNC_EVENT_CMPL_EVENT_ID_VF_CFG_CHANGE,
	ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CFG_CHANGE,
	ASYNC_EVENT_CMPL_EVENT_ID_PORT_PHY_CFG_CHANGE,
	ASYNC_EVENT_CMPL_EVENT_ID_RESET_NOTIFY,
	ASYNC_EVENT_CMPL_EVENT_ID_ERROR_RECOVERY,
	ASYNC_EVENT_CMPL_EVENT_ID_RING_MONITOR_MSG,
};

static struct workqueue_struct *bnxt_pf_wq;

static bool bnxt_vf_pciid(enum board_idx idx)
{
	return (idx == NETXTREME_C_VF || idx == NETXTREME_E_VF ||
		idx == NETXTREME_S_VF || idx == NETXTREME_E_P5_VF);
}

#define DB_CP_REARM_FLAGS	(DB_KEY_CP | DB_IDX_VALID)
#define DB_CP_FLAGS		(DB_KEY_CP | DB_IDX_VALID | DB_IRQ_DIS)
#define DB_CP_IRQ_DIS_FLAGS	(DB_KEY_CP | DB_IRQ_DIS)

#define BNXT_CP_DB_IRQ_DIS(db)						\
		writel(DB_CP_IRQ_DIS_FLAGS, db)

#define BNXT_DB_CQ(db, idx)						\
	writel(DB_CP_FLAGS | RING_CMP(idx), (db)->doorbell)

#define BNXT_DB_NQ_P5(db, idx)						\
	writeq((db)->db_key64 | DBR_TYPE_NQ | RING_CMP(idx), (db)->doorbell)

#define BNXT_DB_CQ_ARM(db, idx)						\
	writel(DB_CP_REARM_FLAGS | RING_CMP(idx), (db)->doorbell)

#define BNXT_DB_NQ_ARM_P5(db, idx)					\
	writeq((db)->db_key64 | DBR_TYPE_NQ_ARM | RING_CMP(idx), (db)->doorbell)

static void bnxt_db_nq(struct bnxt *bp, struct bnxt_db_info *db, u32 idx)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5)
		BNXT_DB_NQ_P5(db, idx);
	else
		BNXT_DB_CQ(db, idx);
}

static void bnxt_db_nq_arm(struct bnxt *bp, struct bnxt_db_info *db, u32 idx)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5)
		BNXT_DB_NQ_ARM_P5(db, idx);
	else
		BNXT_DB_CQ_ARM(db, idx);
}

static void bnxt_db_cq(struct bnxt *bp, struct bnxt_db_info *db, u32 idx)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5)
		writeq(db->db_key64 | DBR_TYPE_CQ_ARMALL | RING_CMP(idx),
		       db->doorbell);
	else
		BNXT_DB_CQ(db, idx);
}

const u16 bnxt_lhint_arr[] = {
	TX_BD_FLAGS_LHINT_512_AND_SMALLER,
	TX_BD_FLAGS_LHINT_512_TO_1023,
	TX_BD_FLAGS_LHINT_1024_TO_2047,
	TX_BD_FLAGS_LHINT_1024_TO_2047,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
};

static u16 bnxt_xmit_get_cfa_action(struct sk_buff *skb)
{
	struct metadata_dst *md_dst = skb_metadata_dst(skb);

	if (!md_dst || md_dst->type != METADATA_HW_PORT_MUX)
		return 0;

	return md_dst->u.port_info.port_id;
}

static netdev_tx_t bnxt_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);
	struct tx_bd *txbd;
	struct tx_bd_ext *txbd1;
	struct netdev_queue *txq;
	int i;
	dma_addr_t mapping;
	unsigned int length, pad = 0;
	u32 len, free_size, vlan_tag_flags, cfa_action, flags;
	u16 prod, last_frag;
	struct pci_dev *pdev = bp->pdev;
	struct bnxt_tx_ring_info *txr;
	struct bnxt_sw_tx_bd *tx_buf;

	i = skb_get_queue_mapping(skb);
	if (unlikely(i >= bp->tx_nr_rings)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	txq = netdev_get_tx_queue(dev, i);
	txr = &bp->tx_ring[bp->tx_ring_map[i]];
	prod = txr->tx_prod;

	free_size = bnxt_tx_avail(bp, txr);
	if (unlikely(free_size < skb_shinfo(skb)->nr_frags + 2)) {
		netif_tx_stop_queue(txq);
		return NETDEV_TX_BUSY;
	}

	length = skb->len;
	len = skb_headlen(skb);
	last_frag = skb_shinfo(skb)->nr_frags;

	txbd = &txr->tx_desc_ring[TX_RING(prod)][TX_IDX(prod)];

	txbd->tx_bd_opaque = prod;

	tx_buf = &txr->tx_buf_ring[prod];
	tx_buf->skb = skb;
	tx_buf->nr_frags = last_frag;

	vlan_tag_flags = 0;
	cfa_action = bnxt_xmit_get_cfa_action(skb);
	if (skb_vlan_tag_present(skb)) {
		vlan_tag_flags = TX_BD_CFA_META_KEY_VLAN |
				 skb_vlan_tag_get(skb);
		/* Currently supports 8021Q, 8021AD vlan offloads
		 * QINQ1, QINQ2, QINQ3 vlan headers are deprecated
		 */
		if (skb->vlan_proto == htons(ETH_P_8021Q))
			vlan_tag_flags |= 1 << TX_BD_CFA_META_TPID_SHIFT;
	}

	if (free_size == bp->tx_ring_size && length <= bp->tx_push_thresh) {
		struct tx_push_buffer *tx_push_buf = txr->tx_push;
		struct tx_push_bd *tx_push = &tx_push_buf->push_bd;
		struct tx_bd_ext *tx_push1 = &tx_push->txbd2;
		void __iomem *db = txr->tx_db.doorbell;
		void *pdata = tx_push_buf->data;
		u64 *end;
		int j, push_len;

		/* Set COAL_NOW to be ready quickly for the next push */
		tx_push->tx_bd_len_flags_type =
			cpu_to_le32((length << TX_BD_LEN_SHIFT) |
					TX_BD_TYPE_LONG_TX_BD |
					TX_BD_FLAGS_LHINT_512_AND_SMALLER |
					TX_BD_FLAGS_COAL_NOW |
					TX_BD_FLAGS_PACKET_END |
					(2 << TX_BD_FLAGS_BD_CNT_SHIFT));

		if (skb->ip_summed == CHECKSUM_PARTIAL)
			tx_push1->tx_bd_hsize_lflags =
					cpu_to_le32(TX_BD_FLAGS_TCP_UDP_CHKSUM);
		else
			tx_push1->tx_bd_hsize_lflags = 0;

		tx_push1->tx_bd_cfa_meta = cpu_to_le32(vlan_tag_flags);
		tx_push1->tx_bd_cfa_action =
			cpu_to_le32(cfa_action << TX_BD_CFA_ACTION_SHIFT);

		end = pdata + length;
		end = PTR_ALIGN(end, 8) - 1;
		*end = 0;

		skb_copy_from_linear_data(skb, pdata, len);
		pdata += len;
		for (j = 0; j < last_frag; j++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[j];
			void *fptr;

			fptr = skb_frag_address_safe(frag);
			if (!fptr)
				goto normal_tx;

			memcpy(pdata, fptr, skb_frag_size(frag));
			pdata += skb_frag_size(frag);
		}

		txbd->tx_bd_len_flags_type = tx_push->tx_bd_len_flags_type;
		txbd->tx_bd_haddr = txr->data_mapping;
		prod = NEXT_TX(prod);
		txbd = &txr->tx_desc_ring[TX_RING(prod)][TX_IDX(prod)];
		memcpy(txbd, tx_push1, sizeof(*txbd));
		prod = NEXT_TX(prod);
		tx_push->doorbell =
			cpu_to_le32(DB_KEY_TX_PUSH | DB_LONG_TX_PUSH | prod);
		txr->tx_prod = prod;

		tx_buf->is_push = 1;
		netdev_tx_sent_queue(txq, skb->len);
		wmb();	/* Sync is_push and byte queue before pushing data */

		push_len = (length + sizeof(*tx_push) + 7) / 8;
		if (push_len > 16) {
			__iowrite64_copy(db, tx_push_buf, 16);
			__iowrite32_copy(db + 4, tx_push_buf + 1,
					 (push_len - 16) << 1);
		} else {
			__iowrite64_copy(db, tx_push_buf, push_len);
		}

		goto tx_done;
	}

normal_tx:
	if (length < BNXT_MIN_PKT_SIZE) {
		pad = BNXT_MIN_PKT_SIZE - length;
		if (skb_pad(skb, pad)) {
			/* SKB already freed. */
			tx_buf->skb = NULL;
			return NETDEV_TX_OK;
		}
		length = BNXT_MIN_PKT_SIZE;
	}

	mapping = dma_map_single(&pdev->dev, skb->data, len, DMA_TO_DEVICE);

	if (unlikely(dma_mapping_error(&pdev->dev, mapping))) {
		dev_kfree_skb_any(skb);
		tx_buf->skb = NULL;
		return NETDEV_TX_OK;
	}

	dma_unmap_addr_set(tx_buf, mapping, mapping);
	flags = (len << TX_BD_LEN_SHIFT) | TX_BD_TYPE_LONG_TX_BD |
		((last_frag + 2) << TX_BD_FLAGS_BD_CNT_SHIFT);

	txbd->tx_bd_haddr = cpu_to_le64(mapping);

	prod = NEXT_TX(prod);
	txbd1 = (struct tx_bd_ext *)
		&txr->tx_desc_ring[TX_RING(prod)][TX_IDX(prod)];

	txbd1->tx_bd_hsize_lflags = 0;
	if (skb_is_gso(skb)) {
		u32 hdr_len;

		if (skb->encapsulation)
			hdr_len = skb_inner_network_offset(skb) +
				skb_inner_network_header_len(skb) +
				inner_tcp_hdrlen(skb);
		else
			hdr_len = skb_transport_offset(skb) +
				tcp_hdrlen(skb);

		txbd1->tx_bd_hsize_lflags = cpu_to_le32(TX_BD_FLAGS_LSO |
					TX_BD_FLAGS_T_IPID |
					(hdr_len << (TX_BD_HSIZE_SHIFT - 1)));
		length = skb_shinfo(skb)->gso_size;
		txbd1->tx_bd_mss = cpu_to_le32(length);
		length += hdr_len;
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		txbd1->tx_bd_hsize_lflags =
			cpu_to_le32(TX_BD_FLAGS_TCP_UDP_CHKSUM);
		txbd1->tx_bd_mss = 0;
	}

	length >>= 9;
	if (unlikely(length >= ARRAY_SIZE(bnxt_lhint_arr))) {
		dev_warn_ratelimited(&pdev->dev, "Dropped oversize %d bytes TX packet.\n",
				     skb->len);
		i = 0;
		goto tx_dma_error;
	}
	flags |= bnxt_lhint_arr[length];
	txbd->tx_bd_len_flags_type = cpu_to_le32(flags);

	txbd1->tx_bd_cfa_meta = cpu_to_le32(vlan_tag_flags);
	txbd1->tx_bd_cfa_action =
			cpu_to_le32(cfa_action << TX_BD_CFA_ACTION_SHIFT);
	for (i = 0; i < last_frag; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		prod = NEXT_TX(prod);
		txbd = &txr->tx_desc_ring[TX_RING(prod)][TX_IDX(prod)];

		len = skb_frag_size(frag);
		mapping = skb_frag_dma_map(&pdev->dev, frag, 0, len,
					   DMA_TO_DEVICE);

		if (unlikely(dma_mapping_error(&pdev->dev, mapping)))
			goto tx_dma_error;

		tx_buf = &txr->tx_buf_ring[prod];
		dma_unmap_addr_set(tx_buf, mapping, mapping);

		txbd->tx_bd_haddr = cpu_to_le64(mapping);

		flags = len << TX_BD_LEN_SHIFT;
		txbd->tx_bd_len_flags_type = cpu_to_le32(flags);
	}

	flags &= ~TX_BD_LEN;
	txbd->tx_bd_len_flags_type =
		cpu_to_le32(((len + pad) << TX_BD_LEN_SHIFT) | flags |
			    TX_BD_FLAGS_PACKET_END);

	netdev_tx_sent_queue(txq, skb->len);

	/* Sync BD data before updating doorbell */
	wmb();

	prod = NEXT_TX(prod);
	txr->tx_prod = prod;

	if (!netdev_xmit_more() || netif_xmit_stopped(txq))
		bnxt_db_write(bp, &txr->tx_db, prod);

tx_done:

	if (unlikely(bnxt_tx_avail(bp, txr) <= MAX_SKB_FRAGS + 1)) {
		if (netdev_xmit_more() && !tx_buf->is_push)
			bnxt_db_write(bp, &txr->tx_db, prod);

		netif_tx_stop_queue(txq);

		/* netif_tx_stop_queue() must be done before checking
		 * tx index in bnxt_tx_avail() below, because in
		 * bnxt_tx_int(), we update tx index before checking for
		 * netif_tx_queue_stopped().
		 */
		smp_mb();
		if (bnxt_tx_avail(bp, txr) > bp->tx_wake_thresh)
			netif_tx_wake_queue(txq);
	}
	return NETDEV_TX_OK;

tx_dma_error:
	last_frag = i;

	/* start back at beginning and unmap skb */
	prod = txr->tx_prod;
	tx_buf = &txr->tx_buf_ring[prod];
	tx_buf->skb = NULL;
	dma_unmap_single(&pdev->dev, dma_unmap_addr(tx_buf, mapping),
			 skb_headlen(skb), PCI_DMA_TODEVICE);
	prod = NEXT_TX(prod);

	/* unmap remaining mapped pages */
	for (i = 0; i < last_frag; i++) {
		prod = NEXT_TX(prod);
		tx_buf = &txr->tx_buf_ring[prod];
		dma_unmap_page(&pdev->dev, dma_unmap_addr(tx_buf, mapping),
			       skb_frag_size(&skb_shinfo(skb)->frags[i]),
			       PCI_DMA_TODEVICE);
	}

	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static void bnxt_tx_int(struct bnxt *bp, struct bnxt_napi *bnapi, int nr_pkts)
{
	struct bnxt_tx_ring_info *txr = bnapi->tx_ring;
	struct netdev_queue *txq = netdev_get_tx_queue(bp->dev, txr->txq_index);
	u16 cons = txr->tx_cons;
	struct pci_dev *pdev = bp->pdev;
	int i;
	unsigned int tx_bytes = 0;

	for (i = 0; i < nr_pkts; i++) {
		struct bnxt_sw_tx_bd *tx_buf;
		struct sk_buff *skb;
		int j, last;

		tx_buf = &txr->tx_buf_ring[cons];
		cons = NEXT_TX(cons);
		skb = tx_buf->skb;
		tx_buf->skb = NULL;

		if (tx_buf->is_push) {
			tx_buf->is_push = 0;
			goto next_tx_int;
		}

		dma_unmap_single(&pdev->dev, dma_unmap_addr(tx_buf, mapping),
				 skb_headlen(skb), PCI_DMA_TODEVICE);
		last = tx_buf->nr_frags;

		for (j = 0; j < last; j++) {
			cons = NEXT_TX(cons);
			tx_buf = &txr->tx_buf_ring[cons];
			dma_unmap_page(
				&pdev->dev,
				dma_unmap_addr(tx_buf, mapping),
				skb_frag_size(&skb_shinfo(skb)->frags[j]),
				PCI_DMA_TODEVICE);
		}

next_tx_int:
		cons = NEXT_TX(cons);

		tx_bytes += skb->len;
		dev_kfree_skb_any(skb);
	}

	netdev_tx_completed_queue(txq, nr_pkts, tx_bytes);
	txr->tx_cons = cons;

	/* Need to make the tx_cons update visible to bnxt_start_xmit()
	 * before checking for netif_tx_queue_stopped().  Without the
	 * memory barrier, there is a small possibility that bnxt_start_xmit()
	 * will miss it and cause the queue to be stopped forever.
	 */
	smp_mb();

	if (unlikely(netif_tx_queue_stopped(txq)) &&
	    (bnxt_tx_avail(bp, txr) > bp->tx_wake_thresh)) {
		__netif_tx_lock(txq, smp_processor_id());
		if (netif_tx_queue_stopped(txq) &&
		    bnxt_tx_avail(bp, txr) > bp->tx_wake_thresh &&
		    txr->dev_state != BNXT_DEV_STATE_CLOSING)
			netif_tx_wake_queue(txq);
		__netif_tx_unlock(txq);
	}
}

static struct page *__bnxt_alloc_rx_page(struct bnxt *bp, dma_addr_t *mapping,
					 struct bnxt_rx_ring_info *rxr,
					 gfp_t gfp)
{
	struct device *dev = &bp->pdev->dev;
	struct page *page;

	page = page_pool_dev_alloc_pages(rxr->page_pool);
	if (!page)
		return NULL;

	*mapping = dma_map_page_attrs(dev, page, 0, PAGE_SIZE, bp->rx_dir,
				      DMA_ATTR_WEAK_ORDERING);
	if (dma_mapping_error(dev, *mapping)) {
		page_pool_recycle_direct(rxr->page_pool, page);
		return NULL;
	}
	*mapping += bp->rx_dma_offset;
	return page;
}

static inline u8 *__bnxt_alloc_rx_data(struct bnxt *bp, dma_addr_t *mapping,
				       gfp_t gfp)
{
	u8 *data;
	struct pci_dev *pdev = bp->pdev;

	data = kmalloc(bp->rx_buf_size, gfp);
	if (!data)
		return NULL;

	*mapping = dma_map_single_attrs(&pdev->dev, data + bp->rx_dma_offset,
					bp->rx_buf_use_size, bp->rx_dir,
					DMA_ATTR_WEAK_ORDERING);

	if (dma_mapping_error(&pdev->dev, *mapping)) {
		kfree(data);
		data = NULL;
	}
	return data;
}

int bnxt_alloc_rx_data(struct bnxt *bp, struct bnxt_rx_ring_info *rxr,
		       u16 prod, gfp_t gfp)
{
	struct rx_bd *rxbd = &rxr->rx_desc_ring[RX_RING(prod)][RX_IDX(prod)];
	struct bnxt_sw_rx_bd *rx_buf = &rxr->rx_buf_ring[prod];
	dma_addr_t mapping;

	if (BNXT_RX_PAGE_MODE(bp)) {
		struct page *page =
			__bnxt_alloc_rx_page(bp, &mapping, rxr, gfp);

		if (!page)
			return -ENOMEM;

		rx_buf->data = page;
		rx_buf->data_ptr = page_address(page) + bp->rx_offset;
	} else {
		u8 *data = __bnxt_alloc_rx_data(bp, &mapping, gfp);

		if (!data)
			return -ENOMEM;

		rx_buf->data = data;
		rx_buf->data_ptr = data + bp->rx_offset;
	}
	rx_buf->mapping = mapping;

	rxbd->rx_bd_haddr = cpu_to_le64(mapping);
	return 0;
}

void bnxt_reuse_rx_data(struct bnxt_rx_ring_info *rxr, u16 cons, void *data)
{
	u16 prod = rxr->rx_prod;
	struct bnxt_sw_rx_bd *cons_rx_buf, *prod_rx_buf;
	struct rx_bd *cons_bd, *prod_bd;

	prod_rx_buf = &rxr->rx_buf_ring[prod];
	cons_rx_buf = &rxr->rx_buf_ring[cons];

	prod_rx_buf->data = data;
	prod_rx_buf->data_ptr = cons_rx_buf->data_ptr;

	prod_rx_buf->mapping = cons_rx_buf->mapping;

	prod_bd = &rxr->rx_desc_ring[RX_RING(prod)][RX_IDX(prod)];
	cons_bd = &rxr->rx_desc_ring[RX_RING(cons)][RX_IDX(cons)];

	prod_bd->rx_bd_haddr = cons_bd->rx_bd_haddr;
}

static inline u16 bnxt_find_next_agg_idx(struct bnxt_rx_ring_info *rxr, u16 idx)
{
	u16 next, max = rxr->rx_agg_bmap_size;

	next = find_next_zero_bit(rxr->rx_agg_bmap, max, idx);
	if (next >= max)
		next = find_first_zero_bit(rxr->rx_agg_bmap, max);
	return next;
}

static inline int bnxt_alloc_rx_page(struct bnxt *bp,
				     struct bnxt_rx_ring_info *rxr,
				     u16 prod, gfp_t gfp)
{
	struct rx_bd *rxbd =
		&rxr->rx_agg_desc_ring[RX_RING(prod)][RX_IDX(prod)];
	struct bnxt_sw_rx_agg_bd *rx_agg_buf;
	struct pci_dev *pdev = bp->pdev;
	struct page *page;
	dma_addr_t mapping;
	u16 sw_prod = rxr->rx_sw_agg_prod;
	unsigned int offset = 0;

	if (PAGE_SIZE > BNXT_RX_PAGE_SIZE) {
		page = rxr->rx_page;
		if (!page) {
			page = alloc_page(gfp);
			if (!page)
				return -ENOMEM;
			rxr->rx_page = page;
			rxr->rx_page_offset = 0;
		}
		offset = rxr->rx_page_offset;
		rxr->rx_page_offset += BNXT_RX_PAGE_SIZE;
		if (rxr->rx_page_offset == PAGE_SIZE)
			rxr->rx_page = NULL;
		else
			get_page(page);
	} else {
		page = alloc_page(gfp);
		if (!page)
			return -ENOMEM;
	}

	mapping = dma_map_page_attrs(&pdev->dev, page, offset,
				     BNXT_RX_PAGE_SIZE, PCI_DMA_FROMDEVICE,
				     DMA_ATTR_WEAK_ORDERING);
	if (dma_mapping_error(&pdev->dev, mapping)) {
		__free_page(page);
		return -EIO;
	}

	if (unlikely(test_bit(sw_prod, rxr->rx_agg_bmap)))
		sw_prod = bnxt_find_next_agg_idx(rxr, sw_prod);

	__set_bit(sw_prod, rxr->rx_agg_bmap);
	rx_agg_buf = &rxr->rx_agg_ring[sw_prod];
	rxr->rx_sw_agg_prod = NEXT_RX_AGG(sw_prod);

	rx_agg_buf->page = page;
	rx_agg_buf->offset = offset;
	rx_agg_buf->mapping = mapping;
	rxbd->rx_bd_haddr = cpu_to_le64(mapping);
	rxbd->rx_bd_opaque = sw_prod;
	return 0;
}

static struct rx_agg_cmp *bnxt_get_agg(struct bnxt *bp,
				       struct bnxt_cp_ring_info *cpr,
				       u16 cp_cons, u16 curr)
{
	struct rx_agg_cmp *agg;

	cp_cons = RING_CMP(ADV_RAW_CMP(cp_cons, curr));
	agg = (struct rx_agg_cmp *)
		&cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];
	return agg;
}

static struct rx_agg_cmp *bnxt_get_tpa_agg_p5(struct bnxt *bp,
					      struct bnxt_rx_ring_info *rxr,
					      u16 agg_id, u16 curr)
{
	struct bnxt_tpa_info *tpa_info = &rxr->rx_tpa[agg_id];

	return &tpa_info->agg_arr[curr];
}

static void bnxt_reuse_rx_agg_bufs(struct bnxt_cp_ring_info *cpr, u16 idx,
				   u16 start, u32 agg_bufs, bool tpa)
{
	struct bnxt_napi *bnapi = cpr->bnapi;
	struct bnxt *bp = bnapi->bp;
	struct bnxt_rx_ring_info *rxr = bnapi->rx_ring;
	u16 prod = rxr->rx_agg_prod;
	u16 sw_prod = rxr->rx_sw_agg_prod;
	bool p5_tpa = false;
	u32 i;

	if ((bp->flags & BNXT_FLAG_CHIP_P5) && tpa)
		p5_tpa = true;

	for (i = 0; i < agg_bufs; i++) {
		u16 cons;
		struct rx_agg_cmp *agg;
		struct bnxt_sw_rx_agg_bd *cons_rx_buf, *prod_rx_buf;
		struct rx_bd *prod_bd;
		struct page *page;

		if (p5_tpa)
			agg = bnxt_get_tpa_agg_p5(bp, rxr, idx, start + i);
		else
			agg = bnxt_get_agg(bp, cpr, idx, start + i);
		cons = agg->rx_agg_cmp_opaque;
		__clear_bit(cons, rxr->rx_agg_bmap);

		if (unlikely(test_bit(sw_prod, rxr->rx_agg_bmap)))
			sw_prod = bnxt_find_next_agg_idx(rxr, sw_prod);

		__set_bit(sw_prod, rxr->rx_agg_bmap);
		prod_rx_buf = &rxr->rx_agg_ring[sw_prod];
		cons_rx_buf = &rxr->rx_agg_ring[cons];

		/* It is possible for sw_prod to be equal to cons, so
		 * set cons_rx_buf->page to NULL first.
		 */
		page = cons_rx_buf->page;
		cons_rx_buf->page = NULL;
		prod_rx_buf->page = page;
		prod_rx_buf->offset = cons_rx_buf->offset;

		prod_rx_buf->mapping = cons_rx_buf->mapping;

		prod_bd = &rxr->rx_agg_desc_ring[RX_RING(prod)][RX_IDX(prod)];

		prod_bd->rx_bd_haddr = cpu_to_le64(cons_rx_buf->mapping);
		prod_bd->rx_bd_opaque = sw_prod;

		prod = NEXT_RX_AGG(prod);
		sw_prod = NEXT_RX_AGG(sw_prod);
	}
	rxr->rx_agg_prod = prod;
	rxr->rx_sw_agg_prod = sw_prod;
}

static struct sk_buff *bnxt_rx_page_skb(struct bnxt *bp,
					struct bnxt_rx_ring_info *rxr,
					u16 cons, void *data, u8 *data_ptr,
					dma_addr_t dma_addr,
					unsigned int offset_and_len)
{
	unsigned int payload = offset_and_len >> 16;
	unsigned int len = offset_and_len & 0xffff;
	skb_frag_t *frag;
	struct page *page = data;
	u16 prod = rxr->rx_prod;
	struct sk_buff *skb;
	int off, err;

	err = bnxt_alloc_rx_data(bp, rxr, prod, GFP_ATOMIC);
	if (unlikely(err)) {
		bnxt_reuse_rx_data(rxr, cons, data);
		return NULL;
	}
	dma_addr -= bp->rx_dma_offset;
	dma_unmap_page_attrs(&bp->pdev->dev, dma_addr, PAGE_SIZE, bp->rx_dir,
			     DMA_ATTR_WEAK_ORDERING);
	page_pool_release_page(rxr->page_pool, page);

	if (unlikely(!payload))
		payload = eth_get_headlen(bp->dev, data_ptr, len);

	skb = napi_alloc_skb(&rxr->bnapi->napi, payload);
	if (!skb) {
		__free_page(page);
		return NULL;
	}

	off = (void *)data_ptr - page_address(page);
	skb_add_rx_frag(skb, 0, page, off, len, PAGE_SIZE);
	memcpy(skb->data - NET_IP_ALIGN, data_ptr - NET_IP_ALIGN,
	       payload + NET_IP_ALIGN);

	frag = &skb_shinfo(skb)->frags[0];
	skb_frag_size_sub(frag, payload);
	skb_frag_off_add(frag, payload);
	skb->data_len -= payload;
	skb->tail += payload;

	return skb;
}

static struct sk_buff *bnxt_rx_skb(struct bnxt *bp,
				   struct bnxt_rx_ring_info *rxr, u16 cons,
				   void *data, u8 *data_ptr,
				   dma_addr_t dma_addr,
				   unsigned int offset_and_len)
{
	u16 prod = rxr->rx_prod;
	struct sk_buff *skb;
	int err;

	err = bnxt_alloc_rx_data(bp, rxr, prod, GFP_ATOMIC);
	if (unlikely(err)) {
		bnxt_reuse_rx_data(rxr, cons, data);
		return NULL;
	}

	skb = build_skb(data, 0);
	dma_unmap_single_attrs(&bp->pdev->dev, dma_addr, bp->rx_buf_use_size,
			       bp->rx_dir, DMA_ATTR_WEAK_ORDERING);
	if (!skb) {
		kfree(data);
		return NULL;
	}

	skb_reserve(skb, bp->rx_offset);
	skb_put(skb, offset_and_len & 0xffff);
	return skb;
}

static struct sk_buff *bnxt_rx_pages(struct bnxt *bp,
				     struct bnxt_cp_ring_info *cpr,
				     struct sk_buff *skb, u16 idx,
				     u32 agg_bufs, bool tpa)
{
	struct bnxt_napi *bnapi = cpr->bnapi;
	struct pci_dev *pdev = bp->pdev;
	struct bnxt_rx_ring_info *rxr = bnapi->rx_ring;
	u16 prod = rxr->rx_agg_prod;
	bool p5_tpa = false;
	u32 i;

	if ((bp->flags & BNXT_FLAG_CHIP_P5) && tpa)
		p5_tpa = true;

	for (i = 0; i < agg_bufs; i++) {
		u16 cons, frag_len;
		struct rx_agg_cmp *agg;
		struct bnxt_sw_rx_agg_bd *cons_rx_buf;
		struct page *page;
		dma_addr_t mapping;

		if (p5_tpa)
			agg = bnxt_get_tpa_agg_p5(bp, rxr, idx, i);
		else
			agg = bnxt_get_agg(bp, cpr, idx, i);
		cons = agg->rx_agg_cmp_opaque;
		frag_len = (le32_to_cpu(agg->rx_agg_cmp_len_flags_type) &
			    RX_AGG_CMP_LEN) >> RX_AGG_CMP_LEN_SHIFT;

		cons_rx_buf = &rxr->rx_agg_ring[cons];
		skb_fill_page_desc(skb, i, cons_rx_buf->page,
				   cons_rx_buf->offset, frag_len);
		__clear_bit(cons, rxr->rx_agg_bmap);

		/* It is possible for bnxt_alloc_rx_page() to allocate
		 * a sw_prod index that equals the cons index, so we
		 * need to clear the cons entry now.
		 */
		mapping = cons_rx_buf->mapping;
		page = cons_rx_buf->page;
		cons_rx_buf->page = NULL;

		if (bnxt_alloc_rx_page(bp, rxr, prod, GFP_ATOMIC) != 0) {
			struct skb_shared_info *shinfo;
			unsigned int nr_frags;

			shinfo = skb_shinfo(skb);
			nr_frags = --shinfo->nr_frags;
			__skb_frag_set_page(&shinfo->frags[nr_frags], NULL);

			dev_kfree_skb(skb);

			cons_rx_buf->page = page;

			/* Update prod since possibly some pages have been
			 * allocated already.
			 */
			rxr->rx_agg_prod = prod;
			bnxt_reuse_rx_agg_bufs(cpr, idx, i, agg_bufs - i, tpa);
			return NULL;
		}

		dma_unmap_page_attrs(&pdev->dev, mapping, BNXT_RX_PAGE_SIZE,
				     PCI_DMA_FROMDEVICE,
				     DMA_ATTR_WEAK_ORDERING);

		skb->data_len += frag_len;
		skb->len += frag_len;
		skb->truesize += PAGE_SIZE;

		prod = NEXT_RX_AGG(prod);
	}
	rxr->rx_agg_prod = prod;
	return skb;
}

static int bnxt_agg_bufs_valid(struct bnxt *bp, struct bnxt_cp_ring_info *cpr,
			       u8 agg_bufs, u32 *raw_cons)
{
	u16 last;
	struct rx_agg_cmp *agg;

	*raw_cons = ADV_RAW_CMP(*raw_cons, agg_bufs);
	last = RING_CMP(*raw_cons);
	agg = (struct rx_agg_cmp *)
		&cpr->cp_desc_ring[CP_RING(last)][CP_IDX(last)];
	return RX_AGG_CMP_VALID(agg, *raw_cons);
}

static inline struct sk_buff *bnxt_copy_skb(struct bnxt_napi *bnapi, u8 *data,
					    unsigned int len,
					    dma_addr_t mapping)
{
	struct bnxt *bp = bnapi->bp;
	struct pci_dev *pdev = bp->pdev;
	struct sk_buff *skb;

	skb = napi_alloc_skb(&bnapi->napi, len);
	if (!skb)
		return NULL;

	dma_sync_single_for_cpu(&pdev->dev, mapping, bp->rx_copy_thresh,
				bp->rx_dir);

	memcpy(skb->data - NET_IP_ALIGN, data - NET_IP_ALIGN,
	       len + NET_IP_ALIGN);

	dma_sync_single_for_device(&pdev->dev, mapping, bp->rx_copy_thresh,
				   bp->rx_dir);

	skb_put(skb, len);
	return skb;
}

static int bnxt_discard_rx(struct bnxt *bp, struct bnxt_cp_ring_info *cpr,
			   u32 *raw_cons, void *cmp)
{
	struct rx_cmp *rxcmp = cmp;
	u32 tmp_raw_cons = *raw_cons;
	u8 cmp_type, agg_bufs = 0;

	cmp_type = RX_CMP_TYPE(rxcmp);

	if (cmp_type == CMP_TYPE_RX_L2_CMP) {
		agg_bufs = (le32_to_cpu(rxcmp->rx_cmp_misc_v1) &
			    RX_CMP_AGG_BUFS) >>
			   RX_CMP_AGG_BUFS_SHIFT;
	} else if (cmp_type == CMP_TYPE_RX_L2_TPA_END_CMP) {
		struct rx_tpa_end_cmp *tpa_end = cmp;

		if (bp->flags & BNXT_FLAG_CHIP_P5)
			return 0;

		agg_bufs = TPA_END_AGG_BUFS(tpa_end);
	}

	if (agg_bufs) {
		if (!bnxt_agg_bufs_valid(bp, cpr, agg_bufs, &tmp_raw_cons))
			return -EBUSY;
	}
	*raw_cons = tmp_raw_cons;
	return 0;
}

static void bnxt_queue_fw_reset_work(struct bnxt *bp, unsigned long delay)
{
	if (!(test_bit(BNXT_STATE_IN_FW_RESET, &bp->state)))
		return;

	if (BNXT_PF(bp))
		queue_delayed_work(bnxt_pf_wq, &bp->fw_reset_task, delay);
	else
		schedule_delayed_work(&bp->fw_reset_task, delay);
}

static void bnxt_queue_sp_work(struct bnxt *bp)
{
	if (BNXT_PF(bp))
		queue_work(bnxt_pf_wq, &bp->sp_task);
	else
		schedule_work(&bp->sp_task);
}

static void bnxt_sched_reset(struct bnxt *bp, struct bnxt_rx_ring_info *rxr)
{
	if (!rxr->bnapi->in_reset) {
		rxr->bnapi->in_reset = true;
		if (bp->flags & BNXT_FLAG_CHIP_P5)
			set_bit(BNXT_RESET_TASK_SP_EVENT, &bp->sp_event);
		else
			set_bit(BNXT_RST_RING_SP_EVENT, &bp->sp_event);
		bnxt_queue_sp_work(bp);
	}
	rxr->rx_next_cons = 0xffff;
}

static u16 bnxt_alloc_agg_idx(struct bnxt_rx_ring_info *rxr, u16 agg_id)
{
	struct bnxt_tpa_idx_map *map = rxr->rx_tpa_idx_map;
	u16 idx = agg_id & MAX_TPA_P5_MASK;

	if (test_bit(idx, map->agg_idx_bmap))
		idx = find_first_zero_bit(map->agg_idx_bmap,
					  BNXT_AGG_IDX_BMAP_SIZE);
	__set_bit(idx, map->agg_idx_bmap);
	map->agg_id_tbl[agg_id] = idx;
	return idx;
}

static void bnxt_free_agg_idx(struct bnxt_rx_ring_info *rxr, u16 idx)
{
	struct bnxt_tpa_idx_map *map = rxr->rx_tpa_idx_map;

	__clear_bit(idx, map->agg_idx_bmap);
}

static u16 bnxt_lookup_agg_idx(struct bnxt_rx_ring_info *rxr, u16 agg_id)
{
	struct bnxt_tpa_idx_map *map = rxr->rx_tpa_idx_map;

	return map->agg_id_tbl[agg_id];
}

static void bnxt_tpa_start(struct bnxt *bp, struct bnxt_rx_ring_info *rxr,
			   struct rx_tpa_start_cmp *tpa_start,
			   struct rx_tpa_start_cmp_ext *tpa_start1)
{
	struct bnxt_sw_rx_bd *cons_rx_buf, *prod_rx_buf;
	struct bnxt_tpa_info *tpa_info;
	u16 cons, prod, agg_id;
	struct rx_bd *prod_bd;
	dma_addr_t mapping;

	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		agg_id = TPA_START_AGG_ID_P5(tpa_start);
		agg_id = bnxt_alloc_agg_idx(rxr, agg_id);
	} else {
		agg_id = TPA_START_AGG_ID(tpa_start);
	}
	cons = tpa_start->rx_tpa_start_cmp_opaque;
	prod = rxr->rx_prod;
	cons_rx_buf = &rxr->rx_buf_ring[cons];
	prod_rx_buf = &rxr->rx_buf_ring[prod];
	tpa_info = &rxr->rx_tpa[agg_id];

	if (unlikely(cons != rxr->rx_next_cons ||
		     TPA_START_ERROR(tpa_start))) {
		netdev_warn(bp->dev, "TPA cons %x, expected cons %x, error code %x\n",
			    cons, rxr->rx_next_cons,
			    TPA_START_ERROR_CODE(tpa_start1));
		bnxt_sched_reset(bp, rxr);
		return;
	}
	/* Store cfa_code in tpa_info to use in tpa_end
	 * completion processing.
	 */
	tpa_info->cfa_code = TPA_START_CFA_CODE(tpa_start1);
	prod_rx_buf->data = tpa_info->data;
	prod_rx_buf->data_ptr = tpa_info->data_ptr;

	mapping = tpa_info->mapping;
	prod_rx_buf->mapping = mapping;

	prod_bd = &rxr->rx_desc_ring[RX_RING(prod)][RX_IDX(prod)];

	prod_bd->rx_bd_haddr = cpu_to_le64(mapping);

	tpa_info->data = cons_rx_buf->data;
	tpa_info->data_ptr = cons_rx_buf->data_ptr;
	cons_rx_buf->data = NULL;
	tpa_info->mapping = cons_rx_buf->mapping;

	tpa_info->len =
		le32_to_cpu(tpa_start->rx_tpa_start_cmp_len_flags_type) >>
				RX_TPA_START_CMP_LEN_SHIFT;
	if (likely(TPA_START_HASH_VALID(tpa_start))) {
		u32 hash_type = TPA_START_HASH_TYPE(tpa_start);

		tpa_info->hash_type = PKT_HASH_TYPE_L4;
		tpa_info->gso_type = SKB_GSO_TCPV4;
		/* RSS profiles 1 and 3 with extract code 0 for inner 4-tuple */
		if (hash_type == 3 || TPA_START_IS_IPV6(tpa_start1))
			tpa_info->gso_type = SKB_GSO_TCPV6;
		tpa_info->rss_hash =
			le32_to_cpu(tpa_start->rx_tpa_start_cmp_rss_hash);
	} else {
		tpa_info->hash_type = PKT_HASH_TYPE_NONE;
		tpa_info->gso_type = 0;
		if (netif_msg_rx_err(bp))
			netdev_warn(bp->dev, "TPA packet without valid hash\n");
	}
	tpa_info->flags2 = le32_to_cpu(tpa_start1->rx_tpa_start_cmp_flags2);
	tpa_info->metadata = le32_to_cpu(tpa_start1->rx_tpa_start_cmp_metadata);
	tpa_info->hdr_info = le32_to_cpu(tpa_start1->rx_tpa_start_cmp_hdr_info);
	tpa_info->agg_count = 0;

	rxr->rx_prod = NEXT_RX(prod);
	cons = NEXT_RX(cons);
	rxr->rx_next_cons = NEXT_RX(cons);
	cons_rx_buf = &rxr->rx_buf_ring[cons];

	bnxt_reuse_rx_data(rxr, cons, cons_rx_buf->data);
	rxr->rx_prod = NEXT_RX(rxr->rx_prod);
	cons_rx_buf->data = NULL;
}

static void bnxt_abort_tpa(struct bnxt_cp_ring_info *cpr, u16 idx, u32 agg_bufs)
{
	if (agg_bufs)
		bnxt_reuse_rx_agg_bufs(cpr, idx, 0, agg_bufs, true);
}

#ifdef CONFIG_INET
static void bnxt_gro_tunnel(struct sk_buff *skb, __be16 ip_proto)
{
	struct udphdr *uh = NULL;

	if (ip_proto == htons(ETH_P_IP)) {
		struct iphdr *iph = (struct iphdr *)skb->data;

		if (iph->protocol == IPPROTO_UDP)
			uh = (struct udphdr *)(iph + 1);
	} else {
		struct ipv6hdr *iph = (struct ipv6hdr *)skb->data;

		if (iph->nexthdr == IPPROTO_UDP)
			uh = (struct udphdr *)(iph + 1);
	}
	if (uh) {
		if (uh->check)
			skb_shinfo(skb)->gso_type |= SKB_GSO_UDP_TUNNEL_CSUM;
		else
			skb_shinfo(skb)->gso_type |= SKB_GSO_UDP_TUNNEL;
	}
}
#endif

static struct sk_buff *bnxt_gro_func_5731x(struct bnxt_tpa_info *tpa_info,
					   int payload_off, int tcp_ts,
					   struct sk_buff *skb)
{
#ifdef CONFIG_INET
	struct tcphdr *th;
	int len, nw_off;
	u16 outer_ip_off, inner_ip_off, inner_mac_off;
	u32 hdr_info = tpa_info->hdr_info;
	bool loopback = false;

	inner_ip_off = BNXT_TPA_INNER_L3_OFF(hdr_info);
	inner_mac_off = BNXT_TPA_INNER_L2_OFF(hdr_info);
	outer_ip_off = BNXT_TPA_OUTER_L3_OFF(hdr_info);

	/* If the packet is an internal loopback packet, the offsets will
	 * have an extra 4 bytes.
	 */
	if (inner_mac_off == 4) {
		loopback = true;
	} else if (inner_mac_off > 4) {
		__be16 proto = *((__be16 *)(skb->data + inner_ip_off -
					    ETH_HLEN - 2));

		/* We only support inner iPv4/ipv6.  If we don't see the
		 * correct protocol ID, it must be a loopback packet where
		 * the offsets are off by 4.
		 */
		if (proto != htons(ETH_P_IP) && proto != htons(ETH_P_IPV6))
			loopback = true;
	}
	if (loopback) {
		/* internal loopback packet, subtract all offsets by 4 */
		inner_ip_off -= 4;
		inner_mac_off -= 4;
		outer_ip_off -= 4;
	}

	nw_off = inner_ip_off - ETH_HLEN;
	skb_set_network_header(skb, nw_off);
	if (tpa_info->flags2 & RX_TPA_START_CMP_FLAGS2_IP_TYPE) {
		struct ipv6hdr *iph = ipv6_hdr(skb);

		skb_set_transport_header(skb, nw_off + sizeof(struct ipv6hdr));
		len = skb->len - skb_transport_offset(skb);
		th = tcp_hdr(skb);
		th->check = ~tcp_v6_check(len, &iph->saddr, &iph->daddr, 0);
	} else {
		struct iphdr *iph = ip_hdr(skb);

		skb_set_transport_header(skb, nw_off + sizeof(struct iphdr));
		len = skb->len - skb_transport_offset(skb);
		th = tcp_hdr(skb);
		th->check = ~tcp_v4_check(len, iph->saddr, iph->daddr, 0);
	}

	if (inner_mac_off) { /* tunnel */
		__be16 proto = *((__be16 *)(skb->data + outer_ip_off -
					    ETH_HLEN - 2));

		bnxt_gro_tunnel(skb, proto);
	}
#endif
	return skb;
}

static struct sk_buff *bnxt_gro_func_5750x(struct bnxt_tpa_info *tpa_info,
					   int payload_off, int tcp_ts,
					   struct sk_buff *skb)
{
#ifdef CONFIG_INET
	u16 outer_ip_off, inner_ip_off, inner_mac_off;
	u32 hdr_info = tpa_info->hdr_info;
	int iphdr_len, nw_off;

	inner_ip_off = BNXT_TPA_INNER_L3_OFF(hdr_info);
	inner_mac_off = BNXT_TPA_INNER_L2_OFF(hdr_info);
	outer_ip_off = BNXT_TPA_OUTER_L3_OFF(hdr_info);

	nw_off = inner_ip_off - ETH_HLEN;
	skb_set_network_header(skb, nw_off);
	iphdr_len = (tpa_info->flags2 & RX_TPA_START_CMP_FLAGS2_IP_TYPE) ?
		     sizeof(struct ipv6hdr) : sizeof(struct iphdr);
	skb_set_transport_header(skb, nw_off + iphdr_len);

	if (inner_mac_off) { /* tunnel */
		__be16 proto = *((__be16 *)(skb->data + outer_ip_off -
					    ETH_HLEN - 2));

		bnxt_gro_tunnel(skb, proto);
	}
#endif
	return skb;
}

#define BNXT_IPV4_HDR_SIZE	(sizeof(struct iphdr) + sizeof(struct tcphdr))
#define BNXT_IPV6_HDR_SIZE	(sizeof(struct ipv6hdr) + sizeof(struct tcphdr))

static struct sk_buff *bnxt_gro_func_5730x(struct bnxt_tpa_info *tpa_info,
					   int payload_off, int tcp_ts,
					   struct sk_buff *skb)
{
#ifdef CONFIG_INET
	struct tcphdr *th;
	int len, nw_off, tcp_opt_len = 0;

	if (tcp_ts)
		tcp_opt_len = 12;

	if (tpa_info->gso_type == SKB_GSO_TCPV4) {
		struct iphdr *iph;

		nw_off = payload_off - BNXT_IPV4_HDR_SIZE - tcp_opt_len -
			 ETH_HLEN;
		skb_set_network_header(skb, nw_off);
		iph = ip_hdr(skb);
		skb_set_transport_header(skb, nw_off + sizeof(struct iphdr));
		len = skb->len - skb_transport_offset(skb);
		th = tcp_hdr(skb);
		th->check = ~tcp_v4_check(len, iph->saddr, iph->daddr, 0);
	} else if (tpa_info->gso_type == SKB_GSO_TCPV6) {
		struct ipv6hdr *iph;

		nw_off = payload_off - BNXT_IPV6_HDR_SIZE - tcp_opt_len -
			 ETH_HLEN;
		skb_set_network_header(skb, nw_off);
		iph = ipv6_hdr(skb);
		skb_set_transport_header(skb, nw_off + sizeof(struct ipv6hdr));
		len = skb->len - skb_transport_offset(skb);
		th = tcp_hdr(skb);
		th->check = ~tcp_v6_check(len, &iph->saddr, &iph->daddr, 0);
	} else {
		dev_kfree_skb_any(skb);
		return NULL;
	}

	if (nw_off) /* tunnel */
		bnxt_gro_tunnel(skb, skb->protocol);
#endif
	return skb;
}

static inline struct sk_buff *bnxt_gro_skb(struct bnxt *bp,
					   struct bnxt_tpa_info *tpa_info,
					   struct rx_tpa_end_cmp *tpa_end,
					   struct rx_tpa_end_cmp_ext *tpa_end1,
					   struct sk_buff *skb)
{
#ifdef CONFIG_INET
	int payload_off;
	u16 segs;

	segs = TPA_END_TPA_SEGS(tpa_end);
	if (segs == 1)
		return skb;

	NAPI_GRO_CB(skb)->count = segs;
	skb_shinfo(skb)->gso_size =
		le32_to_cpu(tpa_end1->rx_tpa_end_cmp_seg_len);
	skb_shinfo(skb)->gso_type = tpa_info->gso_type;
	if (bp->flags & BNXT_FLAG_CHIP_P5)
		payload_off = TPA_END_PAYLOAD_OFF_P5(tpa_end1);
	else
		payload_off = TPA_END_PAYLOAD_OFF(tpa_end);
	skb = bp->gro_func(tpa_info, payload_off, TPA_END_GRO_TS(tpa_end), skb);
	if (likely(skb))
		tcp_gro_complete(skb);
#endif
	return skb;
}

/* Given the cfa_code of a received packet determine which
 * netdev (vf-rep or PF) the packet is destined to.
 */
static struct net_device *bnxt_get_pkt_dev(struct bnxt *bp, u16 cfa_code)
{
	struct net_device *dev = bnxt_get_vf_rep(bp, cfa_code);

	/* if vf-rep dev is NULL, the must belongs to the PF */
	return dev ? dev : bp->dev;
}

static inline struct sk_buff *bnxt_tpa_end(struct bnxt *bp,
					   struct bnxt_cp_ring_info *cpr,
					   u32 *raw_cons,
					   struct rx_tpa_end_cmp *tpa_end,
					   struct rx_tpa_end_cmp_ext *tpa_end1,
					   u8 *event)
{
	struct bnxt_napi *bnapi = cpr->bnapi;
	struct bnxt_rx_ring_info *rxr = bnapi->rx_ring;
	u8 *data_ptr, agg_bufs;
	unsigned int len;
	struct bnxt_tpa_info *tpa_info;
	dma_addr_t mapping;
	struct sk_buff *skb;
	u16 idx = 0, agg_id;
	void *data;
	bool gro;

	if (unlikely(bnapi->in_reset)) {
		int rc = bnxt_discard_rx(bp, cpr, raw_cons, tpa_end);

		if (rc < 0)
			return ERR_PTR(-EBUSY);
		return NULL;
	}

	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		agg_id = TPA_END_AGG_ID_P5(tpa_end);
		agg_id = bnxt_lookup_agg_idx(rxr, agg_id);
		agg_bufs = TPA_END_AGG_BUFS_P5(tpa_end1);
		tpa_info = &rxr->rx_tpa[agg_id];
		if (unlikely(agg_bufs != tpa_info->agg_count)) {
			netdev_warn(bp->dev, "TPA end agg_buf %d != expected agg_bufs %d\n",
				    agg_bufs, tpa_info->agg_count);
			agg_bufs = tpa_info->agg_count;
		}
		tpa_info->agg_count = 0;
		*event |= BNXT_AGG_EVENT;
		bnxt_free_agg_idx(rxr, agg_id);
		idx = agg_id;
		gro = !!(bp->flags & BNXT_FLAG_GRO);
	} else {
		agg_id = TPA_END_AGG_ID(tpa_end);
		agg_bufs = TPA_END_AGG_BUFS(tpa_end);
		tpa_info = &rxr->rx_tpa[agg_id];
		idx = RING_CMP(*raw_cons);
		if (agg_bufs) {
			if (!bnxt_agg_bufs_valid(bp, cpr, agg_bufs, raw_cons))
				return ERR_PTR(-EBUSY);

			*event |= BNXT_AGG_EVENT;
			idx = NEXT_CMP(idx);
		}
		gro = !!TPA_END_GRO(tpa_end);
	}
	data = tpa_info->data;
	data_ptr = tpa_info->data_ptr;
	prefetch(data_ptr);
	len = tpa_info->len;
	mapping = tpa_info->mapping;

	if (unlikely(agg_bufs > MAX_SKB_FRAGS || TPA_END_ERRORS(tpa_end1))) {
		bnxt_abort_tpa(cpr, idx, agg_bufs);
		if (agg_bufs > MAX_SKB_FRAGS)
			netdev_warn(bp->dev, "TPA frags %d exceeded MAX_SKB_FRAGS %d\n",
				    agg_bufs, (int)MAX_SKB_FRAGS);
		return NULL;
	}

	if (len <= bp->rx_copy_thresh) {
		skb = bnxt_copy_skb(bnapi, data_ptr, len, mapping);
		if (!skb) {
			bnxt_abort_tpa(cpr, idx, agg_bufs);
			return NULL;
		}
	} else {
		u8 *new_data;
		dma_addr_t new_mapping;

		new_data = __bnxt_alloc_rx_data(bp, &new_mapping, GFP_ATOMIC);
		if (!new_data) {
			bnxt_abort_tpa(cpr, idx, agg_bufs);
			return NULL;
		}

		tpa_info->data = new_data;
		tpa_info->data_ptr = new_data + bp->rx_offset;
		tpa_info->mapping = new_mapping;

		skb = build_skb(data, 0);
		dma_unmap_single_attrs(&bp->pdev->dev, mapping,
				       bp->rx_buf_use_size, bp->rx_dir,
				       DMA_ATTR_WEAK_ORDERING);

		if (!skb) {
			kfree(data);
			bnxt_abort_tpa(cpr, idx, agg_bufs);
			return NULL;
		}
		skb_reserve(skb, bp->rx_offset);
		skb_put(skb, len);
	}

	if (agg_bufs) {
		skb = bnxt_rx_pages(bp, cpr, skb, idx, agg_bufs, true);
		if (!skb) {
			/* Page reuse already handled by bnxt_rx_pages(). */
			return NULL;
		}
	}

	skb->protocol =
		eth_type_trans(skb, bnxt_get_pkt_dev(bp, tpa_info->cfa_code));

	if (tpa_info->hash_type != PKT_HASH_TYPE_NONE)
		skb_set_hash(skb, tpa_info->rss_hash, tpa_info->hash_type);

	if ((tpa_info->flags2 & RX_CMP_FLAGS2_META_FORMAT_VLAN) &&
	    (skb->dev->features & BNXT_HW_FEATURE_VLAN_ALL_RX)) {
		u16 vlan_proto = tpa_info->metadata >>
			RX_CMP_FLAGS2_METADATA_TPID_SFT;
		u16 vtag = tpa_info->metadata & RX_CMP_FLAGS2_METADATA_TCI_MASK;

		__vlan_hwaccel_put_tag(skb, htons(vlan_proto), vtag);
	}

	skb_checksum_none_assert(skb);
	if (likely(tpa_info->flags2 & RX_TPA_START_CMP_FLAGS2_L4_CS_CALC)) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->csum_level =
			(tpa_info->flags2 & RX_CMP_FLAGS2_T_L4_CS_CALC) >> 3;
	}

	if (gro)
		skb = bnxt_gro_skb(bp, tpa_info, tpa_end, tpa_end1, skb);

	return skb;
}

static void bnxt_tpa_agg(struct bnxt *bp, struct bnxt_rx_ring_info *rxr,
			 struct rx_agg_cmp *rx_agg)
{
	u16 agg_id = TPA_AGG_AGG_ID(rx_agg);
	struct bnxt_tpa_info *tpa_info;

	agg_id = bnxt_lookup_agg_idx(rxr, agg_id);
	tpa_info = &rxr->rx_tpa[agg_id];
	BUG_ON(tpa_info->agg_count >= MAX_SKB_FRAGS);
	tpa_info->agg_arr[tpa_info->agg_count++] = *rx_agg;
}

static void bnxt_deliver_skb(struct bnxt *bp, struct bnxt_napi *bnapi,
			     struct sk_buff *skb)
{
	if (skb->dev != bp->dev) {
		/* this packet belongs to a vf-rep */
		bnxt_vf_rep_rx(bp, skb);
		return;
	}
	skb_record_rx_queue(skb, bnapi->index);
	napi_gro_receive(&bnapi->napi, skb);
}

/* returns the following:
 * 1       - 1 packet successfully received
 * 0       - successful TPA_START, packet not completed yet
 * -EBUSY  - completion ring does not have all the agg buffers yet
 * -ENOMEM - packet aborted due to out of memory
 * -EIO    - packet aborted due to hw error indicated in BD
 */
static int bnxt_rx_pkt(struct bnxt *bp, struct bnxt_cp_ring_info *cpr,
		       u32 *raw_cons, u8 *event)
{
	struct bnxt_napi *bnapi = cpr->bnapi;
	struct bnxt_rx_ring_info *rxr = bnapi->rx_ring;
	struct net_device *dev = bp->dev;
	struct rx_cmp *rxcmp;
	struct rx_cmp_ext *rxcmp1;
	u32 tmp_raw_cons = *raw_cons;
	u16 cfa_code, cons, prod, cp_cons = RING_CMP(tmp_raw_cons);
	struct bnxt_sw_rx_bd *rx_buf;
	unsigned int len;
	u8 *data_ptr, agg_bufs, cmp_type;
	dma_addr_t dma_addr;
	struct sk_buff *skb;
	void *data;
	int rc = 0;
	u32 misc;

	rxcmp = (struct rx_cmp *)
			&cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

	cmp_type = RX_CMP_TYPE(rxcmp);

	if (cmp_type == CMP_TYPE_RX_TPA_AGG_CMP) {
		bnxt_tpa_agg(bp, rxr, (struct rx_agg_cmp *)rxcmp);
		goto next_rx_no_prod_no_len;
	}

	tmp_raw_cons = NEXT_RAW_CMP(tmp_raw_cons);
	cp_cons = RING_CMP(tmp_raw_cons);
	rxcmp1 = (struct rx_cmp_ext *)
			&cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

	if (!RX_CMP_VALID(rxcmp1, tmp_raw_cons))
		return -EBUSY;

	prod = rxr->rx_prod;

	if (cmp_type == CMP_TYPE_RX_L2_TPA_START_CMP) {
		bnxt_tpa_start(bp, rxr, (struct rx_tpa_start_cmp *)rxcmp,
			       (struct rx_tpa_start_cmp_ext *)rxcmp1);

		*event |= BNXT_RX_EVENT;
		goto next_rx_no_prod_no_len;

	} else if (cmp_type == CMP_TYPE_RX_L2_TPA_END_CMP) {
		skb = bnxt_tpa_end(bp, cpr, &tmp_raw_cons,
				   (struct rx_tpa_end_cmp *)rxcmp,
				   (struct rx_tpa_end_cmp_ext *)rxcmp1, event);

		if (IS_ERR(skb))
			return -EBUSY;

		rc = -ENOMEM;
		if (likely(skb)) {
			bnxt_deliver_skb(bp, bnapi, skb);
			rc = 1;
		}
		*event |= BNXT_RX_EVENT;
		goto next_rx_no_prod_no_len;
	}

	cons = rxcmp->rx_cmp_opaque;
	if (unlikely(cons != rxr->rx_next_cons)) {
		int rc1 = bnxt_discard_rx(bp, cpr, raw_cons, rxcmp);

		/* 0xffff is forced error, don't print it */
		if (rxr->rx_next_cons != 0xffff)
			netdev_warn(bp->dev, "RX cons %x != expected cons %x\n",
				    cons, rxr->rx_next_cons);
		bnxt_sched_reset(bp, rxr);
		return rc1;
	}
	rx_buf = &rxr->rx_buf_ring[cons];
	data = rx_buf->data;
	data_ptr = rx_buf->data_ptr;
	prefetch(data_ptr);

	misc = le32_to_cpu(rxcmp->rx_cmp_misc_v1);
	agg_bufs = (misc & RX_CMP_AGG_BUFS) >> RX_CMP_AGG_BUFS_SHIFT;

	if (agg_bufs) {
		if (!bnxt_agg_bufs_valid(bp, cpr, agg_bufs, &tmp_raw_cons))
			return -EBUSY;

		cp_cons = NEXT_CMP(cp_cons);
		*event |= BNXT_AGG_EVENT;
	}
	*event |= BNXT_RX_EVENT;

	rx_buf->data = NULL;
	if (rxcmp1->rx_cmp_cfa_code_errors_v2 & RX_CMP_L2_ERRORS) {
		u32 rx_err = le32_to_cpu(rxcmp1->rx_cmp_cfa_code_errors_v2);

		bnxt_reuse_rx_data(rxr, cons, data);
		if (agg_bufs)
			bnxt_reuse_rx_agg_bufs(cpr, cp_cons, 0, agg_bufs,
					       false);

		rc = -EIO;
		if (rx_err & RX_CMPL_ERRORS_BUFFER_ERROR_MASK) {
			bnapi->cp_ring.sw_stats.rx.rx_buf_errors++;
			if (!(bp->flags & BNXT_FLAG_CHIP_P5) &&
			    !(bp->fw_cap & BNXT_FW_CAP_RING_MONITOR)) {
				netdev_warn_once(bp->dev, "RX buffer error %x\n",
						 rx_err);
				bnxt_sched_reset(bp, rxr);
			}
		}
		goto next_rx_no_len;
	}

	len = le32_to_cpu(rxcmp->rx_cmp_len_flags_type) >> RX_CMP_LEN_SHIFT;
	dma_addr = rx_buf->mapping;

	if (bnxt_rx_xdp(bp, rxr, cons, data, &data_ptr, &len, event)) {
		rc = 1;
		goto next_rx;
	}

	if (len <= bp->rx_copy_thresh) {
		skb = bnxt_copy_skb(bnapi, data_ptr, len, dma_addr);
		bnxt_reuse_rx_data(rxr, cons, data);
		if (!skb) {
			if (agg_bufs)
				bnxt_reuse_rx_agg_bufs(cpr, cp_cons, 0,
						       agg_bufs, false);
			rc = -ENOMEM;
			goto next_rx;
		}
	} else {
		u32 payload;

		if (rx_buf->data_ptr == data_ptr)
			payload = misc & RX_CMP_PAYLOAD_OFFSET;
		else
			payload = 0;
		skb = bp->rx_skb_func(bp, rxr, cons, data, data_ptr, dma_addr,
				      payload | len);
		if (!skb) {
			rc = -ENOMEM;
			goto next_rx;
		}
	}

	if (agg_bufs) {
		skb = bnxt_rx_pages(bp, cpr, skb, cp_cons, agg_bufs, false);
		if (!skb) {
			rc = -ENOMEM;
			goto next_rx;
		}
	}

	if (RX_CMP_HASH_VALID(rxcmp)) {
		u32 hash_type = RX_CMP_HASH_TYPE(rxcmp);
		enum pkt_hash_types type = PKT_HASH_TYPE_L4;

		/* RSS profiles 1 and 3 with extract code 0 for inner 4-tuple */
		if (hash_type != 1 && hash_type != 3)
			type = PKT_HASH_TYPE_L3;
		skb_set_hash(skb, le32_to_cpu(rxcmp->rx_cmp_rss_hash), type);
	}

	cfa_code = RX_CMP_CFA_CODE(rxcmp1);
	skb->protocol = eth_type_trans(skb, bnxt_get_pkt_dev(bp, cfa_code));

	if ((rxcmp1->rx_cmp_flags2 &
	     cpu_to_le32(RX_CMP_FLAGS2_META_FORMAT_VLAN)) &&
	    (skb->dev->features & BNXT_HW_FEATURE_VLAN_ALL_RX)) {
		u32 meta_data = le32_to_cpu(rxcmp1->rx_cmp_meta_data);
		u16 vtag = meta_data & RX_CMP_FLAGS2_METADATA_TCI_MASK;
		u16 vlan_proto = meta_data >> RX_CMP_FLAGS2_METADATA_TPID_SFT;

		__vlan_hwaccel_put_tag(skb, htons(vlan_proto), vtag);
	}

	skb_checksum_none_assert(skb);
	if (RX_CMP_L4_CS_OK(rxcmp1)) {
		if (dev->features & NETIF_F_RXCSUM) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			skb->csum_level = RX_CMP_ENCAP(rxcmp1);
		}
	} else {
		if (rxcmp1->rx_cmp_cfa_code_errors_v2 & RX_CMP_L4_CS_ERR_BITS) {
			if (dev->features & NETIF_F_RXCSUM)
				bnapi->cp_ring.sw_stats.rx.rx_l4_csum_errors++;
		}
	}

	bnxt_deliver_skb(bp, bnapi, skb);
	rc = 1;

next_rx:
	cpr->rx_packets += 1;
	cpr->rx_bytes += len;

next_rx_no_len:
	rxr->rx_prod = NEXT_RX(prod);
	rxr->rx_next_cons = NEXT_RX(cons);

next_rx_no_prod_no_len:
	*raw_cons = tmp_raw_cons;

	return rc;
}

/* In netpoll mode, if we are using a combined completion ring, we need to
 * discard the rx packets and recycle the buffers.
 */
static int bnxt_force_rx_discard(struct bnxt *bp,
				 struct bnxt_cp_ring_info *cpr,
				 u32 *raw_cons, u8 *event)
{
	u32 tmp_raw_cons = *raw_cons;
	struct rx_cmp_ext *rxcmp1;
	struct rx_cmp *rxcmp;
	u16 cp_cons;
	u8 cmp_type;

	cp_cons = RING_CMP(tmp_raw_cons);
	rxcmp = (struct rx_cmp *)
			&cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

	tmp_raw_cons = NEXT_RAW_CMP(tmp_raw_cons);
	cp_cons = RING_CMP(tmp_raw_cons);
	rxcmp1 = (struct rx_cmp_ext *)
			&cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

	if (!RX_CMP_VALID(rxcmp1, tmp_raw_cons))
		return -EBUSY;

	cmp_type = RX_CMP_TYPE(rxcmp);
	if (cmp_type == CMP_TYPE_RX_L2_CMP) {
		rxcmp1->rx_cmp_cfa_code_errors_v2 |=
			cpu_to_le32(RX_CMPL_ERRORS_CRC_ERROR);
	} else if (cmp_type == CMP_TYPE_RX_L2_TPA_END_CMP) {
		struct rx_tpa_end_cmp_ext *tpa_end1;

		tpa_end1 = (struct rx_tpa_end_cmp_ext *)rxcmp1;
		tpa_end1->rx_tpa_end_cmp_errors_v2 |=
			cpu_to_le32(RX_TPA_END_CMP_ERRORS);
	}
	return bnxt_rx_pkt(bp, cpr, raw_cons, event);
}

u32 bnxt_fw_health_readl(struct bnxt *bp, int reg_idx)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	u32 reg = fw_health->regs[reg_idx];
	u32 reg_type, reg_off, val = 0;

	reg_type = BNXT_FW_HEALTH_REG_TYPE(reg);
	reg_off = BNXT_FW_HEALTH_REG_OFF(reg);
	switch (reg_type) {
	case BNXT_FW_HEALTH_REG_TYPE_CFG:
		pci_read_config_dword(bp->pdev, reg_off, &val);
		break;
	case BNXT_FW_HEALTH_REG_TYPE_GRC:
		reg_off = fw_health->mapped_regs[reg_idx];
		fallthrough;
	case BNXT_FW_HEALTH_REG_TYPE_BAR0:
		val = readl(bp->bar0 + reg_off);
		break;
	case BNXT_FW_HEALTH_REG_TYPE_BAR1:
		val = readl(bp->bar1 + reg_off);
		break;
	}
	if (reg_idx == BNXT_FW_RESET_INPROG_REG)
		val &= fw_health->fw_reset_inprog_reg_mask;
	return val;
}

static u16 bnxt_agg_ring_id_to_grp_idx(struct bnxt *bp, u16 ring_id)
{
	int i;

	for (i = 0; i < bp->rx_nr_rings; i++) {
		u16 grp_idx = bp->rx_ring[i].bnapi->index;
		struct bnxt_ring_grp_info *grp_info;

		grp_info = &bp->grp_info[grp_idx];
		if (grp_info->agg_fw_ring_id == ring_id)
			return grp_idx;
	}
	return INVALID_HW_RING_ID;
}

#define BNXT_GET_EVENT_PORT(data)	\
	((data) &			\
	 ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_EVENT_DATA1_PORT_ID_MASK)

#define BNXT_EVENT_RING_TYPE(data2)	\
	((data2) &			\
	 ASYNC_EVENT_CMPL_RING_MONITOR_MSG_EVENT_DATA2_DISABLE_RING_TYPE_MASK)

#define BNXT_EVENT_RING_TYPE_RX(data2)	\
	(BNXT_EVENT_RING_TYPE(data2) ==	\
	 ASYNC_EVENT_CMPL_RING_MONITOR_MSG_EVENT_DATA2_DISABLE_RING_TYPE_RX)

static int bnxt_async_event_process(struct bnxt *bp,
				    struct hwrm_async_event_cmpl *cmpl)
{
	u16 event_id = le16_to_cpu(cmpl->event_id);
	u32 data1 = le32_to_cpu(cmpl->event_data1);
	u32 data2 = le32_to_cpu(cmpl->event_data2);

	/* TODO CHIMP_FW: Define event id's for link change, error etc */
	switch (event_id) {
	case ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CFG_CHANGE: {
		struct bnxt_link_info *link_info = &bp->link_info;

		if (BNXT_VF(bp))
			goto async_event_process_exit;

		/* print unsupported speed warning in forced speed mode only */
		if (!(link_info->autoneg & BNXT_AUTONEG_SPEED) &&
		    (data1 & 0x20000)) {
			u16 fw_speed = link_info->force_link_speed;
			u32 speed = bnxt_fw_to_ethtool_speed(fw_speed);

			if (speed != SPEED_UNKNOWN)
				netdev_warn(bp->dev, "Link speed %d no longer supported\n",
					    speed);
		}
		set_bit(BNXT_LINK_SPEED_CHNG_SP_EVENT, &bp->sp_event);
	}
		fallthrough;
	case ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CHANGE:
	case ASYNC_EVENT_CMPL_EVENT_ID_PORT_PHY_CFG_CHANGE:
		set_bit(BNXT_LINK_CFG_CHANGE_SP_EVENT, &bp->sp_event);
		fallthrough;
	case ASYNC_EVENT_CMPL_EVENT_ID_LINK_STATUS_CHANGE:
		set_bit(BNXT_LINK_CHNG_SP_EVENT, &bp->sp_event);
		break;
	case ASYNC_EVENT_CMPL_EVENT_ID_PF_DRVR_UNLOAD:
		set_bit(BNXT_HWRM_PF_UNLOAD_SP_EVENT, &bp->sp_event);
		break;
	case ASYNC_EVENT_CMPL_EVENT_ID_PORT_CONN_NOT_ALLOWED: {
		u16 port_id = BNXT_GET_EVENT_PORT(data1);

		if (BNXT_VF(bp))
			break;

		if (bp->pf.port_id != port_id)
			break;

		set_bit(BNXT_HWRM_PORT_MODULE_SP_EVENT, &bp->sp_event);
		break;
	}
	case ASYNC_EVENT_CMPL_EVENT_ID_VF_CFG_CHANGE:
		if (BNXT_PF(bp))
			goto async_event_process_exit;
		set_bit(BNXT_RESET_TASK_SILENT_SP_EVENT, &bp->sp_event);
		break;
	case ASYNC_EVENT_CMPL_EVENT_ID_RESET_NOTIFY:
		if (netif_msg_hw(bp))
			netdev_warn(bp->dev, "Received RESET_NOTIFY event, data1: 0x%x, data2: 0x%x\n",
				    data1, data2);
		if (!bp->fw_health)
			goto async_event_process_exit;

		bp->fw_reset_timestamp = jiffies;
		bp->fw_reset_min_dsecs = cmpl->timestamp_lo;
		if (!bp->fw_reset_min_dsecs)
			bp->fw_reset_min_dsecs = BNXT_DFLT_FW_RST_MIN_DSECS;
		bp->fw_reset_max_dsecs = le16_to_cpu(cmpl->timestamp_hi);
		if (!bp->fw_reset_max_dsecs)
			bp->fw_reset_max_dsecs = BNXT_DFLT_FW_RST_MAX_DSECS;
		if (EVENT_DATA1_RESET_NOTIFY_FATAL(data1)) {
			netdev_warn(bp->dev, "Firmware fatal reset event received\n");
			set_bit(BNXT_STATE_FW_FATAL_COND, &bp->state);
		} else {
			netdev_warn(bp->dev, "Firmware non-fatal reset event received, max wait time %d msec\n",
				    bp->fw_reset_max_dsecs * 100);
		}
		set_bit(BNXT_FW_RESET_NOTIFY_SP_EVENT, &bp->sp_event);
		break;
	case ASYNC_EVENT_CMPL_EVENT_ID_ERROR_RECOVERY: {
		struct bnxt_fw_health *fw_health = bp->fw_health;

		if (!fw_health)
			goto async_event_process_exit;

		fw_health->enabled = EVENT_DATA1_RECOVERY_ENABLED(data1);
		fw_health->master = EVENT_DATA1_RECOVERY_MASTER_FUNC(data1);
		if (!fw_health->enabled)
			break;

		if (netif_msg_drv(bp))
			netdev_info(bp->dev, "Error recovery info: error recovery[%d], master[%d], reset count[0x%x], health status: 0x%x\n",
				    fw_health->enabled, fw_health->master,
				    bnxt_fw_health_readl(bp,
							 BNXT_FW_RESET_CNT_REG),
				    bnxt_fw_health_readl(bp,
							 BNXT_FW_HEALTH_REG));
		fw_health->tmr_multiplier =
			DIV_ROUND_UP(fw_health->polling_dsecs * HZ,
				     bp->current_interval * 10);
		fw_health->tmr_counter = fw_health->tmr_multiplier;
		fw_health->last_fw_heartbeat =
			bnxt_fw_health_readl(bp, BNXT_FW_HEARTBEAT_REG);
		fw_health->last_fw_reset_cnt =
			bnxt_fw_health_readl(bp, BNXT_FW_RESET_CNT_REG);
		goto async_event_process_exit;
	}
	case ASYNC_EVENT_CMPL_EVENT_ID_RING_MONITOR_MSG: {
		struct bnxt_rx_ring_info *rxr;
		u16 grp_idx;

		if (bp->flags & BNXT_FLAG_CHIP_P5)
			goto async_event_process_exit;

		netdev_warn(bp->dev, "Ring monitor event, ring type %lu id 0x%x\n",
			    BNXT_EVENT_RING_TYPE(data2), data1);
		if (!BNXT_EVENT_RING_TYPE_RX(data2))
			goto async_event_process_exit;

		grp_idx = bnxt_agg_ring_id_to_grp_idx(bp, data1);
		if (grp_idx == INVALID_HW_RING_ID) {
			netdev_warn(bp->dev, "Unknown RX agg ring id 0x%x\n",
				    data1);
			goto async_event_process_exit;
		}
		rxr = bp->bnapi[grp_idx]->rx_ring;
		bnxt_sched_reset(bp, rxr);
		goto async_event_process_exit;
	}
	default:
		goto async_event_process_exit;
	}
	bnxt_queue_sp_work(bp);
async_event_process_exit:
	bnxt_ulp_async_events(bp, cmpl);
	return 0;
}

static int bnxt_hwrm_handler(struct bnxt *bp, struct tx_cmp *txcmp)
{
	u16 cmpl_type = TX_CMP_TYPE(txcmp), vf_id, seq_id;
	struct hwrm_cmpl *h_cmpl = (struct hwrm_cmpl *)txcmp;
	struct hwrm_fwd_req_cmpl *fwd_req_cmpl =
				(struct hwrm_fwd_req_cmpl *)txcmp;

	switch (cmpl_type) {
	case CMPL_BASE_TYPE_HWRM_DONE:
		seq_id = le16_to_cpu(h_cmpl->sequence_id);
		if (seq_id == bp->hwrm_intr_seq_id)
			bp->hwrm_intr_seq_id = (u16)~bp->hwrm_intr_seq_id;
		else
			netdev_err(bp->dev, "Invalid hwrm seq id %d\n", seq_id);
		break;

	case CMPL_BASE_TYPE_HWRM_FWD_REQ:
		vf_id = le16_to_cpu(fwd_req_cmpl->source_id);

		if ((vf_id < bp->pf.first_vf_id) ||
		    (vf_id >= bp->pf.first_vf_id + bp->pf.active_vfs)) {
			netdev_err(bp->dev, "Msg contains invalid VF id %x\n",
				   vf_id);
			return -EINVAL;
		}

		set_bit(vf_id - bp->pf.first_vf_id, bp->pf.vf_event_bmap);
		set_bit(BNXT_HWRM_EXEC_FWD_REQ_SP_EVENT, &bp->sp_event);
		bnxt_queue_sp_work(bp);
		break;

	case CMPL_BASE_TYPE_HWRM_ASYNC_EVENT:
		bnxt_async_event_process(bp,
					 (struct hwrm_async_event_cmpl *)txcmp);

	default:
		break;
	}

	return 0;
}

static irqreturn_t bnxt_msix(int irq, void *dev_instance)
{
	struct bnxt_napi *bnapi = dev_instance;
	struct bnxt *bp = bnapi->bp;
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	u32 cons = RING_CMP(cpr->cp_raw_cons);

	cpr->event_ctr++;
	prefetch(&cpr->cp_desc_ring[CP_RING(cons)][CP_IDX(cons)]);
	napi_schedule(&bnapi->napi);
	return IRQ_HANDLED;
}

static inline int bnxt_has_work(struct bnxt *bp, struct bnxt_cp_ring_info *cpr)
{
	u32 raw_cons = cpr->cp_raw_cons;
	u16 cons = RING_CMP(raw_cons);
	struct tx_cmp *txcmp;

	txcmp = &cpr->cp_desc_ring[CP_RING(cons)][CP_IDX(cons)];

	return TX_CMP_VALID(txcmp, raw_cons);
}

static irqreturn_t bnxt_inta(int irq, void *dev_instance)
{
	struct bnxt_napi *bnapi = dev_instance;
	struct bnxt *bp = bnapi->bp;
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	u32 cons = RING_CMP(cpr->cp_raw_cons);
	u32 int_status;

	prefetch(&cpr->cp_desc_ring[CP_RING(cons)][CP_IDX(cons)]);

	if (!bnxt_has_work(bp, cpr)) {
		int_status = readl(bp->bar0 + BNXT_CAG_REG_LEGACY_INT_STATUS);
		/* return if erroneous interrupt */
		if (!(int_status & (0x10000 << cpr->cp_ring_struct.fw_ring_id)))
			return IRQ_NONE;
	}

	/* disable ring IRQ */
	BNXT_CP_DB_IRQ_DIS(cpr->cp_db.doorbell);

	/* Return here if interrupt is shared and is disabled. */
	if (unlikely(atomic_read(&bp->intr_sem) != 0))
		return IRQ_HANDLED;

	napi_schedule(&bnapi->napi);
	return IRQ_HANDLED;
}

static int __bnxt_poll_work(struct bnxt *bp, struct bnxt_cp_ring_info *cpr,
			    int budget)
{
	struct bnxt_napi *bnapi = cpr->bnapi;
	u32 raw_cons = cpr->cp_raw_cons;
	u32 cons;
	int tx_pkts = 0;
	int rx_pkts = 0;
	u8 event = 0;
	struct tx_cmp *txcmp;

	cpr->has_more_work = 0;
	cpr->had_work_done = 1;
	while (1) {
		int rc;

		cons = RING_CMP(raw_cons);
		txcmp = &cpr->cp_desc_ring[CP_RING(cons)][CP_IDX(cons)];

		if (!TX_CMP_VALID(txcmp, raw_cons))
			break;

		/* The valid test of the entry must be done first before
		 * reading any further.
		 */
		dma_rmb();
		if (TX_CMP_TYPE(txcmp) == CMP_TYPE_TX_L2_CMP) {
			tx_pkts++;
			/* return full budget so NAPI will complete. */
			if (unlikely(tx_pkts > bp->tx_wake_thresh)) {
				rx_pkts = budget;
				raw_cons = NEXT_RAW_CMP(raw_cons);
				if (budget)
					cpr->has_more_work = 1;
				break;
			}
		} else if ((TX_CMP_TYPE(txcmp) & 0x30) == 0x10) {
			if (likely(budget))
				rc = bnxt_rx_pkt(bp, cpr, &raw_cons, &event);
			else
				rc = bnxt_force_rx_discard(bp, cpr, &raw_cons,
							   &event);
			if (likely(rc >= 0))
				rx_pkts += rc;
			/* Increment rx_pkts when rc is -ENOMEM to count towards
			 * the NAPI budget.  Otherwise, we may potentially loop
			 * here forever if we consistently cannot allocate
			 * buffers.
			 */
			else if (rc == -ENOMEM && budget)
				rx_pkts++;
			else if (rc == -EBUSY)	/* partial completion */
				break;
		} else if (unlikely((TX_CMP_TYPE(txcmp) ==
				     CMPL_BASE_TYPE_HWRM_DONE) ||
				    (TX_CMP_TYPE(txcmp) ==
				     CMPL_BASE_TYPE_HWRM_FWD_REQ) ||
				    (TX_CMP_TYPE(txcmp) ==
				     CMPL_BASE_TYPE_HWRM_ASYNC_EVENT))) {
			bnxt_hwrm_handler(bp, txcmp);
		}
		raw_cons = NEXT_RAW_CMP(raw_cons);

		if (rx_pkts && rx_pkts == budget) {
			cpr->has_more_work = 1;
			break;
		}
	}

	if (event & BNXT_REDIRECT_EVENT)
		xdp_do_flush_map();

	if (event & BNXT_TX_EVENT) {
		struct bnxt_tx_ring_info *txr = bnapi->tx_ring;
		u16 prod = txr->tx_prod;

		/* Sync BD data before updating doorbell */
		wmb();

		bnxt_db_write_relaxed(bp, &txr->tx_db, prod);
	}

	cpr->cp_raw_cons = raw_cons;
	bnapi->tx_pkts += tx_pkts;
	bnapi->events |= event;
	return rx_pkts;
}

static void __bnxt_poll_work_done(struct bnxt *bp, struct bnxt_napi *bnapi)
{
	if (bnapi->tx_pkts) {
		bnapi->tx_int(bp, bnapi, bnapi->tx_pkts);
		bnapi->tx_pkts = 0;
	}

	if ((bnapi->events & BNXT_RX_EVENT) && !(bnapi->in_reset)) {
		struct bnxt_rx_ring_info *rxr = bnapi->rx_ring;

		if (bnapi->events & BNXT_AGG_EVENT)
			bnxt_db_write(bp, &rxr->rx_agg_db, rxr->rx_agg_prod);
		bnxt_db_write(bp, &rxr->rx_db, rxr->rx_prod);
	}
	bnapi->events = 0;
}

static int bnxt_poll_work(struct bnxt *bp, struct bnxt_cp_ring_info *cpr,
			  int budget)
{
	struct bnxt_napi *bnapi = cpr->bnapi;
	int rx_pkts;

	rx_pkts = __bnxt_poll_work(bp, cpr, budget);

	/* ACK completion ring before freeing tx ring and producing new
	 * buffers in rx/agg rings to prevent overflowing the completion
	 * ring.
	 */
	bnxt_db_cq(bp, &cpr->cp_db, cpr->cp_raw_cons);

	__bnxt_poll_work_done(bp, bnapi);
	return rx_pkts;
}

static int bnxt_poll_nitroa0(struct napi_struct *napi, int budget)
{
	struct bnxt_napi *bnapi = container_of(napi, struct bnxt_napi, napi);
	struct bnxt *bp = bnapi->bp;
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	struct bnxt_rx_ring_info *rxr = bnapi->rx_ring;
	struct tx_cmp *txcmp;
	struct rx_cmp_ext *rxcmp1;
	u32 cp_cons, tmp_raw_cons;
	u32 raw_cons = cpr->cp_raw_cons;
	u32 rx_pkts = 0;
	u8 event = 0;

	while (1) {
		int rc;

		cp_cons = RING_CMP(raw_cons);
		txcmp = &cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

		if (!TX_CMP_VALID(txcmp, raw_cons))
			break;

		if ((TX_CMP_TYPE(txcmp) & 0x30) == 0x10) {
			tmp_raw_cons = NEXT_RAW_CMP(raw_cons);
			cp_cons = RING_CMP(tmp_raw_cons);
			rxcmp1 = (struct rx_cmp_ext *)
			  &cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

			if (!RX_CMP_VALID(rxcmp1, tmp_raw_cons))
				break;

			/* force an error to recycle the buffer */
			rxcmp1->rx_cmp_cfa_code_errors_v2 |=
				cpu_to_le32(RX_CMPL_ERRORS_CRC_ERROR);

			rc = bnxt_rx_pkt(bp, cpr, &raw_cons, &event);
			if (likely(rc == -EIO) && budget)
				rx_pkts++;
			else if (rc == -EBUSY)	/* partial completion */
				break;
		} else if (unlikely(TX_CMP_TYPE(txcmp) ==
				    CMPL_BASE_TYPE_HWRM_DONE)) {
			bnxt_hwrm_handler(bp, txcmp);
		} else {
			netdev_err(bp->dev,
				   "Invalid completion received on special ring\n");
		}
		raw_cons = NEXT_RAW_CMP(raw_cons);

		if (rx_pkts == budget)
			break;
	}

	cpr->cp_raw_cons = raw_cons;
	BNXT_DB_CQ(&cpr->cp_db, cpr->cp_raw_cons);
	bnxt_db_write(bp, &rxr->rx_db, rxr->rx_prod);

	if (event & BNXT_AGG_EVENT)
		bnxt_db_write(bp, &rxr->rx_agg_db, rxr->rx_agg_prod);

	if (!bnxt_has_work(bp, cpr) && rx_pkts < budget) {
		napi_complete_done(napi, rx_pkts);
		BNXT_DB_CQ_ARM(&cpr->cp_db, cpr->cp_raw_cons);
	}
	return rx_pkts;
}

static int bnxt_poll(struct napi_struct *napi, int budget)
{
	struct bnxt_napi *bnapi = container_of(napi, struct bnxt_napi, napi);
	struct bnxt *bp = bnapi->bp;
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	int work_done = 0;

	while (1) {
		work_done += bnxt_poll_work(bp, cpr, budget - work_done);

		if (work_done >= budget) {
			if (!budget)
				BNXT_DB_CQ_ARM(&cpr->cp_db, cpr->cp_raw_cons);
			break;
		}

		if (!bnxt_has_work(bp, cpr)) {
			if (napi_complete_done(napi, work_done))
				BNXT_DB_CQ_ARM(&cpr->cp_db, cpr->cp_raw_cons);
			break;
		}
	}
	if (bp->flags & BNXT_FLAG_DIM) {
		struct dim_sample dim_sample = {};

		dim_update_sample(cpr->event_ctr,
				  cpr->rx_packets,
				  cpr->rx_bytes,
				  &dim_sample);
		net_dim(&cpr->dim, dim_sample);
	}
	return work_done;
}

static int __bnxt_poll_cqs(struct bnxt *bp, struct bnxt_napi *bnapi, int budget)
{
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	int i, work_done = 0;

	for (i = 0; i < 2; i++) {
		struct bnxt_cp_ring_info *cpr2 = cpr->cp_ring_arr[i];

		if (cpr2) {
			work_done += __bnxt_poll_work(bp, cpr2,
						      budget - work_done);
			cpr->has_more_work |= cpr2->has_more_work;
		}
	}
	return work_done;
}

static void __bnxt_poll_cqs_done(struct bnxt *bp, struct bnxt_napi *bnapi,
				 u64 dbr_type)
{
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	int i;

	for (i = 0; i < 2; i++) {
		struct bnxt_cp_ring_info *cpr2 = cpr->cp_ring_arr[i];
		struct bnxt_db_info *db;

		if (cpr2 && cpr2->had_work_done) {
			db = &cpr2->cp_db;
			writeq(db->db_key64 | dbr_type |
			       RING_CMP(cpr2->cp_raw_cons), db->doorbell);
			cpr2->had_work_done = 0;
		}
	}
	__bnxt_poll_work_done(bp, bnapi);
}

static int bnxt_poll_p5(struct napi_struct *napi, int budget)
{
	struct bnxt_napi *bnapi = container_of(napi, struct bnxt_napi, napi);
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	u32 raw_cons = cpr->cp_raw_cons;
	struct bnxt *bp = bnapi->bp;
	struct nqe_cn *nqcmp;
	int work_done = 0;
	u32 cons;

	if (cpr->has_more_work) {
		cpr->has_more_work = 0;
		work_done = __bnxt_poll_cqs(bp, bnapi, budget);
	}
	while (1) {
		cons = RING_CMP(raw_cons);
		nqcmp = &cpr->nq_desc_ring[CP_RING(cons)][CP_IDX(cons)];

		if (!NQ_CMP_VALID(nqcmp, raw_cons)) {
			if (cpr->has_more_work)
				break;

			__bnxt_poll_cqs_done(bp, bnapi, DBR_TYPE_CQ_ARMALL);
			cpr->cp_raw_cons = raw_cons;
			if (napi_complete_done(napi, work_done))
				BNXT_DB_NQ_ARM_P5(&cpr->cp_db,
						  cpr->cp_raw_cons);
			return work_done;
		}

		/* The valid test of the entry must be done first before
		 * reading any further.
		 */
		dma_rmb();

		if (nqcmp->type == cpu_to_le16(NQ_CN_TYPE_CQ_NOTIFICATION)) {
			u32 idx = le32_to_cpu(nqcmp->cq_handle_low);
			struct bnxt_cp_ring_info *cpr2;

			cpr2 = cpr->cp_ring_arr[idx];
			work_done += __bnxt_poll_work(bp, cpr2,
						      budget - work_done);
			cpr->has_more_work |= cpr2->has_more_work;
		} else {
			bnxt_hwrm_handler(bp, (struct tx_cmp *)nqcmp);
		}
		raw_cons = NEXT_RAW_CMP(raw_cons);
	}
	__bnxt_poll_cqs_done(bp, bnapi, DBR_TYPE_CQ);
	if (raw_cons != cpr->cp_raw_cons) {
		cpr->cp_raw_cons = raw_cons;
		BNXT_DB_NQ_P5(&cpr->cp_db, raw_cons);
	}
	return work_done;
}

static void bnxt_free_tx_skbs(struct bnxt *bp)
{
	int i, max_idx;
	struct pci_dev *pdev = bp->pdev;

	if (!bp->tx_ring)
		return;

	max_idx = bp->tx_nr_pages * TX_DESC_CNT;
	for (i = 0; i < bp->tx_nr_rings; i++) {
		struct bnxt_tx_ring_info *txr = &bp->tx_ring[i];
		int j;

		for (j = 0; j < max_idx;) {
			struct bnxt_sw_tx_bd *tx_buf = &txr->tx_buf_ring[j];
			struct sk_buff *skb;
			int k, last;

			if (i < bp->tx_nr_rings_xdp &&
			    tx_buf->action == XDP_REDIRECT) {
				dma_unmap_single(&pdev->dev,
					dma_unmap_addr(tx_buf, mapping),
					dma_unmap_len(tx_buf, len),
					PCI_DMA_TODEVICE);
				xdp_return_frame(tx_buf->xdpf);
				tx_buf->action = 0;
				tx_buf->xdpf = NULL;
				j++;
				continue;
			}

			skb = tx_buf->skb;
			if (!skb) {
				j++;
				continue;
			}

			tx_buf->skb = NULL;

			if (tx_buf->is_push) {
				dev_kfree_skb(skb);
				j += 2;
				continue;
			}

			dma_unmap_single(&pdev->dev,
					 dma_unmap_addr(tx_buf, mapping),
					 skb_headlen(skb),
					 PCI_DMA_TODEVICE);

			last = tx_buf->nr_frags;
			j += 2;
			for (k = 0; k < last; k++, j++) {
				int ring_idx = j & bp->tx_ring_mask;
				skb_frag_t *frag = &skb_shinfo(skb)->frags[k];

				tx_buf = &txr->tx_buf_ring[ring_idx];
				dma_unmap_page(
					&pdev->dev,
					dma_unmap_addr(tx_buf, mapping),
					skb_frag_size(frag), PCI_DMA_TODEVICE);
			}
			dev_kfree_skb(skb);
		}
		netdev_tx_reset_queue(netdev_get_tx_queue(bp->dev, i));
	}
}

static void bnxt_free_one_rx_ring_skbs(struct bnxt *bp, int ring_nr)
{
	struct bnxt_rx_ring_info *rxr = &bp->rx_ring[ring_nr];
	struct pci_dev *pdev = bp->pdev;
	struct bnxt_tpa_idx_map *map;
	int i, max_idx, max_agg_idx;

	max_idx = bp->rx_nr_pages * RX_DESC_CNT;
	max_agg_idx = bp->rx_agg_nr_pages * RX_DESC_CNT;
	if (!rxr->rx_tpa)
		goto skip_rx_tpa_free;

	for (i = 0; i < bp->max_tpa; i++) {
		struct bnxt_tpa_info *tpa_info = &rxr->rx_tpa[i];
		u8 *data = tpa_info->data;

		if (!data)
			continue;

		dma_unmap_single_attrs(&pdev->dev, tpa_info->mapping,
				       bp->rx_buf_use_size, bp->rx_dir,
				       DMA_ATTR_WEAK_ORDERING);

		tpa_info->data = NULL;

		kfree(data);
	}

skip_rx_tpa_free:
	for (i = 0; i < max_idx; i++) {
		struct bnxt_sw_rx_bd *rx_buf = &rxr->rx_buf_ring[i];
		dma_addr_t mapping = rx_buf->mapping;
		void *data = rx_buf->data;

		if (!data)
			continue;

		rx_buf->data = NULL;
		if (BNXT_RX_PAGE_MODE(bp)) {
			mapping -= bp->rx_dma_offset;
			dma_unmap_page_attrs(&pdev->dev, mapping, PAGE_SIZE,
					     bp->rx_dir,
					     DMA_ATTR_WEAK_ORDERING);
			page_pool_recycle_direct(rxr->page_pool, data);
		} else {
			dma_unmap_single_attrs(&pdev->dev, mapping,
					       bp->rx_buf_use_size, bp->rx_dir,
					       DMA_ATTR_WEAK_ORDERING);
			kfree(data);
		}
	}
	for (i = 0; i < max_agg_idx; i++) {
		struct bnxt_sw_rx_agg_bd *rx_agg_buf = &rxr->rx_agg_ring[i];
		struct page *page = rx_agg_buf->page;

		if (!page)
			continue;

		dma_unmap_page_attrs(&pdev->dev, rx_agg_buf->mapping,
				     BNXT_RX_PAGE_SIZE, PCI_DMA_FROMDEVICE,
				     DMA_ATTR_WEAK_ORDERING);

		rx_agg_buf->page = NULL;
		__clear_bit(i, rxr->rx_agg_bmap);

		__free_page(page);
	}
	if (rxr->rx_page) {
		__free_page(rxr->rx_page);
		rxr->rx_page = NULL;
	}
	map = rxr->rx_tpa_idx_map;
	if (map)
		memset(map->agg_idx_bmap, 0, sizeof(map->agg_idx_bmap));
}

static void bnxt_free_rx_skbs(struct bnxt *bp)
{
	int i;

	if (!bp->rx_ring)
		return;

	for (i = 0; i < bp->rx_nr_rings; i++)
		bnxt_free_one_rx_ring_skbs(bp, i);
}

static void bnxt_free_skbs(struct bnxt *bp)
{
	bnxt_free_tx_skbs(bp);
	bnxt_free_rx_skbs(bp);
}

static void bnxt_free_ring(struct bnxt *bp, struct bnxt_ring_mem_info *rmem)
{
	struct pci_dev *pdev = bp->pdev;
	int i;

	for (i = 0; i < rmem->nr_pages; i++) {
		if (!rmem->pg_arr[i])
			continue;

		dma_free_coherent(&pdev->dev, rmem->page_size,
				  rmem->pg_arr[i], rmem->dma_arr[i]);

		rmem->pg_arr[i] = NULL;
	}
	if (rmem->pg_tbl) {
		size_t pg_tbl_size = rmem->nr_pages * 8;

		if (rmem->flags & BNXT_RMEM_USE_FULL_PAGE_FLAG)
			pg_tbl_size = rmem->page_size;
		dma_free_coherent(&pdev->dev, pg_tbl_size,
				  rmem->pg_tbl, rmem->pg_tbl_map);
		rmem->pg_tbl = NULL;
	}
	if (rmem->vmem_size && *rmem->vmem) {
		vfree(*rmem->vmem);
		*rmem->vmem = NULL;
	}
}

static int bnxt_alloc_ring(struct bnxt *bp, struct bnxt_ring_mem_info *rmem)
{
	struct pci_dev *pdev = bp->pdev;
	u64 valid_bit = 0;
	int i;

	if (rmem->flags & (BNXT_RMEM_VALID_PTE_FLAG | BNXT_RMEM_RING_PTE_FLAG))
		valid_bit = PTU_PTE_VALID;
	if ((rmem->nr_pages > 1 || rmem->depth > 0) && !rmem->pg_tbl) {
		size_t pg_tbl_size = rmem->nr_pages * 8;

		if (rmem->flags & BNXT_RMEM_USE_FULL_PAGE_FLAG)
			pg_tbl_size = rmem->page_size;
		rmem->pg_tbl = dma_alloc_coherent(&pdev->dev, pg_tbl_size,
						  &rmem->pg_tbl_map,
						  GFP_KERNEL);
		if (!rmem->pg_tbl)
			return -ENOMEM;
	}

	for (i = 0; i < rmem->nr_pages; i++) {
		u64 extra_bits = valid_bit;

		rmem->pg_arr[i] = dma_alloc_coherent(&pdev->dev,
						     rmem->page_size,
						     &rmem->dma_arr[i],
						     GFP_KERNEL);
		if (!rmem->pg_arr[i])
			return -ENOMEM;

		if (rmem->init_val)
			memset(rmem->pg_arr[i], rmem->init_val,
			       rmem->page_size);
		if (rmem->nr_pages > 1 || rmem->depth > 0) {
			if (i == rmem->nr_pages - 2 &&
			    (rmem->flags & BNXT_RMEM_RING_PTE_FLAG))
				extra_bits |= PTU_PTE_NEXT_TO_LAST;
			else if (i == rmem->nr_pages - 1 &&
				 (rmem->flags & BNXT_RMEM_RING_PTE_FLAG))
				extra_bits |= PTU_PTE_LAST;
			rmem->pg_tbl[i] =
				cpu_to_le64(rmem->dma_arr[i] | extra_bits);
		}
	}

	if (rmem->vmem_size) {
		*rmem->vmem = vzalloc(rmem->vmem_size);
		if (!(*rmem->vmem))
			return -ENOMEM;
	}
	return 0;
}

static void bnxt_free_tpa_info(struct bnxt *bp)
{
	int i;

	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];

		kfree(rxr->rx_tpa_idx_map);
		rxr->rx_tpa_idx_map = NULL;
		if (rxr->rx_tpa) {
			kfree(rxr->rx_tpa[0].agg_arr);
			rxr->rx_tpa[0].agg_arr = NULL;
		}
		kfree(rxr->rx_tpa);
		rxr->rx_tpa = NULL;
	}
}

static int bnxt_alloc_tpa_info(struct bnxt *bp)
{
	int i, j, total_aggs = 0;

	bp->max_tpa = MAX_TPA;
	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		if (!bp->max_tpa_v2)
			return 0;
		bp->max_tpa = max_t(u16, bp->max_tpa_v2, MAX_TPA_P5);
		total_aggs = bp->max_tpa * MAX_SKB_FRAGS;
	}

	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];
		struct rx_agg_cmp *agg;

		rxr->rx_tpa = kcalloc(bp->max_tpa, sizeof(struct bnxt_tpa_info),
				      GFP_KERNEL);
		if (!rxr->rx_tpa)
			return -ENOMEM;

		if (!(bp->flags & BNXT_FLAG_CHIP_P5))
			continue;
		agg = kcalloc(total_aggs, sizeof(*agg), GFP_KERNEL);
		rxr->rx_tpa[0].agg_arr = agg;
		if (!agg)
			return -ENOMEM;
		for (j = 1; j < bp->max_tpa; j++)
			rxr->rx_tpa[j].agg_arr = agg + j * MAX_SKB_FRAGS;
		rxr->rx_tpa_idx_map = kzalloc(sizeof(*rxr->rx_tpa_idx_map),
					      GFP_KERNEL);
		if (!rxr->rx_tpa_idx_map)
			return -ENOMEM;
	}
	return 0;
}

static void bnxt_free_rx_rings(struct bnxt *bp)
{
	int i;

	if (!bp->rx_ring)
		return;

	bnxt_free_tpa_info(bp);
	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];
		struct bnxt_ring_struct *ring;

		if (rxr->xdp_prog)
			bpf_prog_put(rxr->xdp_prog);

		if (xdp_rxq_info_is_reg(&rxr->xdp_rxq))
			xdp_rxq_info_unreg(&rxr->xdp_rxq);

		page_pool_destroy(rxr->page_pool);
		rxr->page_pool = NULL;

		kfree(rxr->rx_agg_bmap);
		rxr->rx_agg_bmap = NULL;

		ring = &rxr->rx_ring_struct;
		bnxt_free_ring(bp, &ring->ring_mem);

		ring = &rxr->rx_agg_ring_struct;
		bnxt_free_ring(bp, &ring->ring_mem);
	}
}

static int bnxt_alloc_rx_page_pool(struct bnxt *bp,
				   struct bnxt_rx_ring_info *rxr)
{
	struct page_pool_params pp = { 0 };

	pp.pool_size = bp->rx_ring_size;
	pp.nid = dev_to_node(&bp->pdev->dev);
	pp.dev = &bp->pdev->dev;
	pp.dma_dir = DMA_BIDIRECTIONAL;

	rxr->page_pool = page_pool_create(&pp);
	if (IS_ERR(rxr->page_pool)) {
		int err = PTR_ERR(rxr->page_pool);

		rxr->page_pool = NULL;
		return err;
	}
	return 0;
}

static int bnxt_alloc_rx_rings(struct bnxt *bp)
{
	int i, rc = 0, agg_rings = 0;

	if (!bp->rx_ring)
		return -ENOMEM;

	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		agg_rings = 1;

	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];
		struct bnxt_ring_struct *ring;

		ring = &rxr->rx_ring_struct;

		rc = bnxt_alloc_rx_page_pool(bp, rxr);
		if (rc)
			return rc;

		rc = xdp_rxq_info_reg(&rxr->xdp_rxq, bp->dev, i);
		if (rc < 0)
			return rc;

		rc = xdp_rxq_info_reg_mem_model(&rxr->xdp_rxq,
						MEM_TYPE_PAGE_POOL,
						rxr->page_pool);
		if (rc) {
			xdp_rxq_info_unreg(&rxr->xdp_rxq);
			return rc;
		}

		rc = bnxt_alloc_ring(bp, &ring->ring_mem);
		if (rc)
			return rc;

		ring->grp_idx = i;
		if (agg_rings) {
			u16 mem_size;

			ring = &rxr->rx_agg_ring_struct;
			rc = bnxt_alloc_ring(bp, &ring->ring_mem);
			if (rc)
				return rc;

			ring->grp_idx = i;
			rxr->rx_agg_bmap_size = bp->rx_agg_ring_mask + 1;
			mem_size = rxr->rx_agg_bmap_size / 8;
			rxr->rx_agg_bmap = kzalloc(mem_size, GFP_KERNEL);
			if (!rxr->rx_agg_bmap)
				return -ENOMEM;
		}
	}
	if (bp->flags & BNXT_FLAG_TPA)
		rc = bnxt_alloc_tpa_info(bp);
	return rc;
}

static void bnxt_free_tx_rings(struct bnxt *bp)
{
	int i;
	struct pci_dev *pdev = bp->pdev;

	if (!bp->tx_ring)
		return;

	for (i = 0; i < bp->tx_nr_rings; i++) {
		struct bnxt_tx_ring_info *txr = &bp->tx_ring[i];
		struct bnxt_ring_struct *ring;

		if (txr->tx_push) {
			dma_free_coherent(&pdev->dev, bp->tx_push_size,
					  txr->tx_push, txr->tx_push_mapping);
			txr->tx_push = NULL;
		}

		ring = &txr->tx_ring_struct;

		bnxt_free_ring(bp, &ring->ring_mem);
	}
}

static int bnxt_alloc_tx_rings(struct bnxt *bp)
{
	int i, j, rc;
	struct pci_dev *pdev = bp->pdev;

	bp->tx_push_size = 0;
	if (bp->tx_push_thresh) {
		int push_size;

		push_size  = L1_CACHE_ALIGN(sizeof(struct tx_push_bd) +
					bp->tx_push_thresh);

		if (push_size > 256) {
			push_size = 0;
			bp->tx_push_thresh = 0;
		}

		bp->tx_push_size = push_size;
	}

	for (i = 0, j = 0; i < bp->tx_nr_rings; i++) {
		struct bnxt_tx_ring_info *txr = &bp->tx_ring[i];
		struct bnxt_ring_struct *ring;
		u8 qidx;

		ring = &txr->tx_ring_struct;

		rc = bnxt_alloc_ring(bp, &ring->ring_mem);
		if (rc)
			return rc;

		ring->grp_idx = txr->bnapi->index;
		if (bp->tx_push_size) {
			dma_addr_t mapping;

			/* One pre-allocated DMA buffer to backup
			 * TX push operation
			 */
			txr->tx_push = dma_alloc_coherent(&pdev->dev,
						bp->tx_push_size,
						&txr->tx_push_mapping,
						GFP_KERNEL);

			if (!txr->tx_push)
				return -ENOMEM;

			mapping = txr->tx_push_mapping +
				sizeof(struct tx_push_bd);
			txr->data_mapping = cpu_to_le64(mapping);
		}
		qidx = bp->tc_to_qidx[j];
		ring->queue_id = bp->q_info[qidx].queue_id;
		if (i < bp->tx_nr_rings_xdp)
			continue;
		if (i % bp->tx_nr_rings_per_tc == (bp->tx_nr_rings_per_tc - 1))
			j++;
	}
	return 0;
}

static void bnxt_free_cp_rings(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr;
		struct bnxt_ring_struct *ring;
		int j;

		if (!bnapi)
			continue;

		cpr = &bnapi->cp_ring;
		ring = &cpr->cp_ring_struct;

		bnxt_free_ring(bp, &ring->ring_mem);

		for (j = 0; j < 2; j++) {
			struct bnxt_cp_ring_info *cpr2 = cpr->cp_ring_arr[j];

			if (cpr2) {
				ring = &cpr2->cp_ring_struct;
				bnxt_free_ring(bp, &ring->ring_mem);
				kfree(cpr2);
				cpr->cp_ring_arr[j] = NULL;
			}
		}
	}
}

static struct bnxt_cp_ring_info *bnxt_alloc_cp_sub_ring(struct bnxt *bp)
{
	struct bnxt_ring_mem_info *rmem;
	struct bnxt_ring_struct *ring;
	struct bnxt_cp_ring_info *cpr;
	int rc;

	cpr = kzalloc(sizeof(*cpr), GFP_KERNEL);
	if (!cpr)
		return NULL;

	ring = &cpr->cp_ring_struct;
	rmem = &ring->ring_mem;
	rmem->nr_pages = bp->cp_nr_pages;
	rmem->page_size = HW_CMPD_RING_SIZE;
	rmem->pg_arr = (void **)cpr->cp_desc_ring;
	rmem->dma_arr = cpr->cp_desc_mapping;
	rmem->flags = BNXT_RMEM_RING_PTE_FLAG;
	rc = bnxt_alloc_ring(bp, rmem);
	if (rc) {
		bnxt_free_ring(bp, rmem);
		kfree(cpr);
		cpr = NULL;
	}
	return cpr;
}

static int bnxt_alloc_cp_rings(struct bnxt *bp)
{
	bool sh = !!(bp->flags & BNXT_FLAG_SHARED_RINGS);
	int i, rc, ulp_base_vec, ulp_msix;

	ulp_msix = bnxt_get_ulp_msix_num(bp);
	ulp_base_vec = bnxt_get_ulp_msix_base(bp);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr;
		struct bnxt_ring_struct *ring;

		if (!bnapi)
			continue;

		cpr = &bnapi->cp_ring;
		cpr->bnapi = bnapi;
		ring = &cpr->cp_ring_struct;

		rc = bnxt_alloc_ring(bp, &ring->ring_mem);
		if (rc)
			return rc;

		if (ulp_msix && i >= ulp_base_vec)
			ring->map_idx = i + ulp_msix;
		else
			ring->map_idx = i;

		if (!(bp->flags & BNXT_FLAG_CHIP_P5))
			continue;

		if (i < bp->rx_nr_rings) {
			struct bnxt_cp_ring_info *cpr2 =
				bnxt_alloc_cp_sub_ring(bp);

			cpr->cp_ring_arr[BNXT_RX_HDL] = cpr2;
			if (!cpr2)
				return -ENOMEM;
			cpr2->bnapi = bnapi;
		}
		if ((sh && i < bp->tx_nr_rings) ||
		    (!sh && i >= bp->rx_nr_rings)) {
			struct bnxt_cp_ring_info *cpr2 =
				bnxt_alloc_cp_sub_ring(bp);

			cpr->cp_ring_arr[BNXT_TX_HDL] = cpr2;
			if (!cpr2)
				return -ENOMEM;
			cpr2->bnapi = bnapi;
		}
	}
	return 0;
}

static void bnxt_init_ring_struct(struct bnxt *bp)
{
	int i;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_ring_mem_info *rmem;
		struct bnxt_cp_ring_info *cpr;
		struct bnxt_rx_ring_info *rxr;
		struct bnxt_tx_ring_info *txr;
		struct bnxt_ring_struct *ring;

		if (!bnapi)
			continue;

		cpr = &bnapi->cp_ring;
		ring = &cpr->cp_ring_struct;
		rmem = &ring->ring_mem;
		rmem->nr_pages = bp->cp_nr_pages;
		rmem->page_size = HW_CMPD_RING_SIZE;
		rmem->pg_arr = (void **)cpr->cp_desc_ring;
		rmem->dma_arr = cpr->cp_desc_mapping;
		rmem->vmem_size = 0;

		rxr = bnapi->rx_ring;
		if (!rxr)
			goto skip_rx;

		ring = &rxr->rx_ring_struct;
		rmem = &ring->ring_mem;
		rmem->nr_pages = bp->rx_nr_pages;
		rmem->page_size = HW_RXBD_RING_SIZE;
		rmem->pg_arr = (void **)rxr->rx_desc_ring;
		rmem->dma_arr = rxr->rx_desc_mapping;
		rmem->vmem_size = SW_RXBD_RING_SIZE * bp->rx_nr_pages;
		rmem->vmem = (void **)&rxr->rx_buf_ring;

		ring = &rxr->rx_agg_ring_struct;
		rmem = &ring->ring_mem;
		rmem->nr_pages = bp->rx_agg_nr_pages;
		rmem->page_size = HW_RXBD_RING_SIZE;
		rmem->pg_arr = (void **)rxr->rx_agg_desc_ring;
		rmem->dma_arr = rxr->rx_agg_desc_mapping;
		rmem->vmem_size = SW_RXBD_AGG_RING_SIZE * bp->rx_agg_nr_pages;
		rmem->vmem = (void **)&rxr->rx_agg_ring;

skip_rx:
		txr = bnapi->tx_ring;
		if (!txr)
			continue;

		ring = &txr->tx_ring_struct;
		rmem = &ring->ring_mem;
		rmem->nr_pages = bp->tx_nr_pages;
		rmem->page_size = HW_RXBD_RING_SIZE;
		rmem->pg_arr = (void **)txr->tx_desc_ring;
		rmem->dma_arr = txr->tx_desc_mapping;
		rmem->vmem_size = SW_TXBD_RING_SIZE * bp->tx_nr_pages;
		rmem->vmem = (void **)&txr->tx_buf_ring;
	}
}

static void bnxt_init_rxbd_pages(struct bnxt_ring_struct *ring, u32 type)
{
	int i;
	u32 prod;
	struct rx_bd **rx_buf_ring;

	rx_buf_ring = (struct rx_bd **)ring->ring_mem.pg_arr;
	for (i = 0, prod = 0; i < ring->ring_mem.nr_pages; i++) {
		int j;
		struct rx_bd *rxbd;

		rxbd = rx_buf_ring[i];
		if (!rxbd)
			continue;

		for (j = 0; j < RX_DESC_CNT; j++, rxbd++, prod++) {
			rxbd->rx_bd_len_flags_type = cpu_to_le32(type);
			rxbd->rx_bd_opaque = prod;
		}
	}
}

static int bnxt_alloc_one_rx_ring(struct bnxt *bp, int ring_nr)
{
	struct bnxt_rx_ring_info *rxr = &bp->rx_ring[ring_nr];
	struct net_device *dev = bp->dev;
	u32 prod;
	int i;

	prod = rxr->rx_prod;
	for (i = 0; i < bp->rx_ring_size; i++) {
		if (bnxt_alloc_rx_data(bp, rxr, prod, GFP_KERNEL)) {
			netdev_warn(dev, "init'ed rx ring %d with %d/%d skbs only\n",
				    ring_nr, i, bp->rx_ring_size);
			break;
		}
		prod = NEXT_RX(prod);
	}
	rxr->rx_prod = prod;

	if (!(bp->flags & BNXT_FLAG_AGG_RINGS))
		return 0;

	prod = rxr->rx_agg_prod;
	for (i = 0; i < bp->rx_agg_ring_size; i++) {
		if (bnxt_alloc_rx_page(bp, rxr, prod, GFP_KERNEL)) {
			netdev_warn(dev, "init'ed rx ring %d with %d/%d pages only\n",
				    ring_nr, i, bp->rx_ring_size);
			break;
		}
		prod = NEXT_RX_AGG(prod);
	}
	rxr->rx_agg_prod = prod;

	if (rxr->rx_tpa) {
		dma_addr_t mapping;
		u8 *data;

		for (i = 0; i < bp->max_tpa; i++) {
			data = __bnxt_alloc_rx_data(bp, &mapping, GFP_KERNEL);
			if (!data)
				return -ENOMEM;

			rxr->rx_tpa[i].data = data;
			rxr->rx_tpa[i].data_ptr = data + bp->rx_offset;
			rxr->rx_tpa[i].mapping = mapping;
		}
	}
	return 0;
}

static int bnxt_init_one_rx_ring(struct bnxt *bp, int ring_nr)
{
	struct bnxt_rx_ring_info *rxr;
	struct bnxt_ring_struct *ring;
	u32 type;

	type = (bp->rx_buf_use_size << RX_BD_LEN_SHIFT) |
		RX_BD_TYPE_RX_PACKET_BD | RX_BD_FLAGS_EOP;

	if (NET_IP_ALIGN == 2)
		type |= RX_BD_FLAGS_SOP;

	rxr = &bp->rx_ring[ring_nr];
	ring = &rxr->rx_ring_struct;
	bnxt_init_rxbd_pages(ring, type);

	if (BNXT_RX_PAGE_MODE(bp) && bp->xdp_prog) {
		bpf_prog_add(bp->xdp_prog, 1);
		rxr->xdp_prog = bp->xdp_prog;
	}
	ring->fw_ring_id = INVALID_HW_RING_ID;

	ring = &rxr->rx_agg_ring_struct;
	ring->fw_ring_id = INVALID_HW_RING_ID;

	if ((bp->flags & BNXT_FLAG_AGG_RINGS)) {
		type = ((u32)BNXT_RX_PAGE_SIZE << RX_BD_LEN_SHIFT) |
			RX_BD_TYPE_RX_AGG_BD | RX_BD_FLAGS_SOP;

		bnxt_init_rxbd_pages(ring, type);
	}

	return bnxt_alloc_one_rx_ring(bp, ring_nr);
}

static void bnxt_init_cp_rings(struct bnxt *bp)
{
	int i, j;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_cp_ring_info *cpr = &bp->bnapi[i]->cp_ring;
		struct bnxt_ring_struct *ring = &cpr->cp_ring_struct;

		ring->fw_ring_id = INVALID_HW_RING_ID;
		cpr->rx_ring_coal.coal_ticks = bp->rx_coal.coal_ticks;
		cpr->rx_ring_coal.coal_bufs = bp->rx_coal.coal_bufs;
		for (j = 0; j < 2; j++) {
			struct bnxt_cp_ring_info *cpr2 = cpr->cp_ring_arr[j];

			if (!cpr2)
				continue;

			ring = &cpr2->cp_ring_struct;
			ring->fw_ring_id = INVALID_HW_RING_ID;
			cpr2->rx_ring_coal.coal_ticks = bp->rx_coal.coal_ticks;
			cpr2->rx_ring_coal.coal_bufs = bp->rx_coal.coal_bufs;
		}
	}
}

static int bnxt_init_rx_rings(struct bnxt *bp)
{
	int i, rc = 0;

	if (BNXT_RX_PAGE_MODE(bp)) {
		bp->rx_offset = NET_IP_ALIGN + XDP_PACKET_HEADROOM;
		bp->rx_dma_offset = XDP_PACKET_HEADROOM;
	} else {
		bp->rx_offset = BNXT_RX_OFFSET;
		bp->rx_dma_offset = BNXT_RX_DMA_OFFSET;
	}

	for (i = 0; i < bp->rx_nr_rings; i++) {
		rc = bnxt_init_one_rx_ring(bp, i);
		if (rc)
			break;
	}

	return rc;
}

static int bnxt_init_tx_rings(struct bnxt *bp)
{
	u16 i;

	bp->tx_wake_thresh = max_t(int, bp->tx_ring_size / 2,
				   MAX_SKB_FRAGS + 1);

	for (i = 0; i < bp->tx_nr_rings; i++) {
		struct bnxt_tx_ring_info *txr = &bp->tx_ring[i];
		struct bnxt_ring_struct *ring = &txr->tx_ring_struct;

		ring->fw_ring_id = INVALID_HW_RING_ID;
	}

	return 0;
}

static void bnxt_free_ring_grps(struct bnxt *bp)
{
	kfree(bp->grp_info);
	bp->grp_info = NULL;
}

static int bnxt_init_ring_grps(struct bnxt *bp, bool irq_re_init)
{
	int i;

	if (irq_re_init) {
		bp->grp_info = kcalloc(bp->cp_nr_rings,
				       sizeof(struct bnxt_ring_grp_info),
				       GFP_KERNEL);
		if (!bp->grp_info)
			return -ENOMEM;
	}
	for (i = 0; i < bp->cp_nr_rings; i++) {
		if (irq_re_init)
			bp->grp_info[i].fw_stats_ctx = INVALID_HW_RING_ID;
		bp->grp_info[i].fw_grp_id = INVALID_HW_RING_ID;
		bp->grp_info[i].rx_fw_ring_id = INVALID_HW_RING_ID;
		bp->grp_info[i].agg_fw_ring_id = INVALID_HW_RING_ID;
		bp->grp_info[i].cp_fw_ring_id = INVALID_HW_RING_ID;
	}
	return 0;
}

static void bnxt_free_vnics(struct bnxt *bp)
{
	kfree(bp->vnic_info);
	bp->vnic_info = NULL;
	bp->nr_vnics = 0;
}

static int bnxt_alloc_vnics(struct bnxt *bp)
{
	int num_vnics = 1;

#ifdef CONFIG_RFS_ACCEL
	if ((bp->flags & (BNXT_FLAG_RFS | BNXT_FLAG_CHIP_P5)) == BNXT_FLAG_RFS)
		num_vnics += bp->rx_nr_rings;
#endif

	if (BNXT_CHIP_TYPE_NITRO_A0(bp))
		num_vnics++;

	bp->vnic_info = kcalloc(num_vnics, sizeof(struct bnxt_vnic_info),
				GFP_KERNEL);
	if (!bp->vnic_info)
		return -ENOMEM;

	bp->nr_vnics = num_vnics;
	return 0;
}

static void bnxt_init_vnics(struct bnxt *bp)
{
	int i;

	for (i = 0; i < bp->nr_vnics; i++) {
		struct bnxt_vnic_info *vnic = &bp->vnic_info[i];
		int j;

		vnic->fw_vnic_id = INVALID_HW_RING_ID;
		for (j = 0; j < BNXT_MAX_CTX_PER_VNIC; j++)
			vnic->fw_rss_cos_lb_ctx[j] = INVALID_HW_RING_ID;

		vnic->fw_l2_ctx_id = INVALID_HW_RING_ID;

		if (bp->vnic_info[i].rss_hash_key) {
			if (i == 0)
				prandom_bytes(vnic->rss_hash_key,
					      HW_HASH_KEY_SIZE);
			else
				memcpy(vnic->rss_hash_key,
				       bp->vnic_info[0].rss_hash_key,
				       HW_HASH_KEY_SIZE);
		}
	}
}

static int bnxt_calc_nr_ring_pages(u32 ring_size, int desc_per_pg)
{
	int pages;

	pages = ring_size / desc_per_pg;

	if (!pages)
		return 1;

	pages++;

	while (pages & (pages - 1))
		pages++;

	return pages;
}

void bnxt_set_tpa_flags(struct bnxt *bp)
{
	bp->flags &= ~BNXT_FLAG_TPA;
	if (bp->flags & BNXT_FLAG_NO_AGG_RINGS)
		return;
	if (bp->dev->features & NETIF_F_LRO)
		bp->flags |= BNXT_FLAG_LRO;
	else if (bp->dev->features & NETIF_F_GRO_HW)
		bp->flags |= BNXT_FLAG_GRO;
}

/* bp->rx_ring_size, bp->tx_ring_size, dev->mtu, BNXT_FLAG_{G|L}RO flags must
 * be set on entry.
 */
void bnxt_set_ring_params(struct bnxt *bp)
{
	u32 ring_size, rx_size, rx_space, max_rx_cmpl;
	u32 agg_factor = 0, agg_ring_size = 0;

	/* 8 for CRC and VLAN */
	rx_size = SKB_DATA_ALIGN(bp->dev->mtu + ETH_HLEN + NET_IP_ALIGN + 8);

	rx_space = rx_size + NET_SKB_PAD +
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	bp->rx_copy_thresh = BNXT_RX_COPY_THRESH;
	ring_size = bp->rx_ring_size;
	bp->rx_agg_ring_size = 0;
	bp->rx_agg_nr_pages = 0;

	if (bp->flags & BNXT_FLAG_TPA)
		agg_factor = min_t(u32, 4, 65536 / BNXT_RX_PAGE_SIZE);

	bp->flags &= ~BNXT_FLAG_JUMBO;
	if (rx_space > PAGE_SIZE && !(bp->flags & BNXT_FLAG_NO_AGG_RINGS)) {
		u32 jumbo_factor;

		bp->flags |= BNXT_FLAG_JUMBO;
		jumbo_factor = PAGE_ALIGN(bp->dev->mtu - 40) >> PAGE_SHIFT;
		if (jumbo_factor > agg_factor)
			agg_factor = jumbo_factor;
	}
	agg_ring_size = ring_size * agg_factor;

	if (agg_ring_size) {
		bp->rx_agg_nr_pages = bnxt_calc_nr_ring_pages(agg_ring_size,
							RX_DESC_CNT);
		if (bp->rx_agg_nr_pages > MAX_RX_AGG_PAGES) {
			u32 tmp = agg_ring_size;

			bp->rx_agg_nr_pages = MAX_RX_AGG_PAGES;
			agg_ring_size = MAX_RX_AGG_PAGES * RX_DESC_CNT - 1;
			netdev_warn(bp->dev, "rx agg ring size %d reduced to %d.\n",
				    tmp, agg_ring_size);
		}
		bp->rx_agg_ring_size = agg_ring_size;
		bp->rx_agg_ring_mask = (bp->rx_agg_nr_pages * RX_DESC_CNT) - 1;
		rx_size = SKB_DATA_ALIGN(BNXT_RX_COPY_THRESH + NET_IP_ALIGN);
		rx_space = rx_size + NET_SKB_PAD +
			SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	}

	bp->rx_buf_use_size = rx_size;
	bp->rx_buf_size = rx_space;

	bp->rx_nr_pages = bnxt_calc_nr_ring_pages(ring_size, RX_DESC_CNT);
	bp->rx_ring_mask = (bp->rx_nr_pages * RX_DESC_CNT) - 1;

	ring_size = bp->tx_ring_size;
	bp->tx_nr_pages = bnxt_calc_nr_ring_pages(ring_size, TX_DESC_CNT);
	bp->tx_ring_mask = (bp->tx_nr_pages * TX_DESC_CNT) - 1;

	max_rx_cmpl = bp->rx_ring_size;
	/* MAX TPA needs to be added because TPA_START completions are
	 * immediately recycled, so the TPA completions are not bound by
	 * the RX ring size.
	 */
	if (bp->flags & BNXT_FLAG_TPA)
		max_rx_cmpl += bp->max_tpa;
	/* RX and TPA completions are 32-byte, all others are 16-byte */
	ring_size = max_rx_cmpl * 2 + agg_ring_size + bp->tx_ring_size;
	bp->cp_ring_size = ring_size;

	bp->cp_nr_pages = bnxt_calc_nr_ring_pages(ring_size, CP_DESC_CNT);
	if (bp->cp_nr_pages > MAX_CP_PAGES) {
		bp->cp_nr_pages = MAX_CP_PAGES;
		bp->cp_ring_size = MAX_CP_PAGES * CP_DESC_CNT - 1;
		netdev_warn(bp->dev, "completion ring size %d reduced to %d.\n",
			    ring_size, bp->cp_ring_size);
	}
	bp->cp_bit = bp->cp_nr_pages * CP_DESC_CNT;
	bp->cp_ring_mask = bp->cp_bit - 1;
}

/* Changing allocation mode of RX rings.
 * TODO: Update when extending xdp_rxq_info to support allocation modes.
 */
int bnxt_set_rx_skb_mode(struct bnxt *bp, bool page_mode)
{
	if (page_mode) {
		if (bp->dev->mtu > BNXT_MAX_PAGE_MODE_MTU)
			return -EOPNOTSUPP;
		bp->dev->max_mtu =
			min_t(u16, bp->max_mtu, BNXT_MAX_PAGE_MODE_MTU);
		bp->flags &= ~BNXT_FLAG_AGG_RINGS;
		bp->flags |= BNXT_FLAG_NO_AGG_RINGS | BNXT_FLAG_RX_PAGE_MODE;
		bp->rx_dir = DMA_BIDIRECTIONAL;
		bp->rx_skb_func = bnxt_rx_page_skb;
		/* Disable LRO or GRO_HW */
		netdev_update_features(bp->dev);
	} else {
		bp->dev->max_mtu = bp->max_mtu;
		bp->flags &= ~BNXT_FLAG_RX_PAGE_MODE;
		bp->rx_dir = DMA_FROM_DEVICE;
		bp->rx_skb_func = bnxt_rx_skb;
	}
	return 0;
}

static void bnxt_free_vnic_attributes(struct bnxt *bp)
{
	int i;
	struct bnxt_vnic_info *vnic;
	struct pci_dev *pdev = bp->pdev;

	if (!bp->vnic_info)
		return;

	for (i = 0; i < bp->nr_vnics; i++) {
		vnic = &bp->vnic_info[i];

		kfree(vnic->fw_grp_ids);
		vnic->fw_grp_ids = NULL;

		kfree(vnic->uc_list);
		vnic->uc_list = NULL;

		if (vnic->mc_list) {
			dma_free_coherent(&pdev->dev, vnic->mc_list_size,
					  vnic->mc_list, vnic->mc_list_mapping);
			vnic->mc_list = NULL;
		}

		if (vnic->rss_table) {
			dma_free_coherent(&pdev->dev, vnic->rss_table_size,
					  vnic->rss_table,
					  vnic->rss_table_dma_addr);
			vnic->rss_table = NULL;
		}

		vnic->rss_hash_key = NULL;
		vnic->flags = 0;
	}
}

static int bnxt_alloc_vnic_attributes(struct bnxt *bp)
{
	int i, rc = 0, size;
	struct bnxt_vnic_info *vnic;
	struct pci_dev *pdev = bp->pdev;
	int max_rings;

	for (i = 0; i < bp->nr_vnics; i++) {
		vnic = &bp->vnic_info[i];

		if (vnic->flags & BNXT_VNIC_UCAST_FLAG) {
			int mem_size = (BNXT_MAX_UC_ADDRS - 1) * ETH_ALEN;

			if (mem_size > 0) {
				vnic->uc_list = kmalloc(mem_size, GFP_KERNEL);
				if (!vnic->uc_list) {
					rc = -ENOMEM;
					goto out;
				}
			}
		}

		if (vnic->flags & BNXT_VNIC_MCAST_FLAG) {
			vnic->mc_list_size = BNXT_MAX_MC_ADDRS * ETH_ALEN;
			vnic->mc_list =
				dma_alloc_coherent(&pdev->dev,
						   vnic->mc_list_size,
						   &vnic->mc_list_mapping,
						   GFP_KERNEL);
			if (!vnic->mc_list) {
				rc = -ENOMEM;
				goto out;
			}
		}

		if (bp->flags & BNXT_FLAG_CHIP_P5)
			goto vnic_skip_grps;

		if (vnic->flags & BNXT_VNIC_RSS_FLAG)
			max_rings = bp->rx_nr_rings;
		else
			max_rings = 1;

		vnic->fw_grp_ids = kcalloc(max_rings, sizeof(u16), GFP_KERNEL);
		if (!vnic->fw_grp_ids) {
			rc = -ENOMEM;
			goto out;
		}
vnic_skip_grps:
		if ((bp->flags & BNXT_FLAG_NEW_RSS_CAP) &&
		    !(vnic->flags & BNXT_VNIC_RSS_FLAG))
			continue;

		/* Allocate rss table and hash key */
		size = L1_CACHE_ALIGN(HW_HASH_INDEX_SIZE * sizeof(u16));
		if (bp->flags & BNXT_FLAG_CHIP_P5)
			size = L1_CACHE_ALIGN(BNXT_MAX_RSS_TABLE_SIZE_P5);

		vnic->rss_table_size = size + HW_HASH_KEY_SIZE;
		vnic->rss_table = dma_alloc_coherent(&pdev->dev,
						     vnic->rss_table_size,
						     &vnic->rss_table_dma_addr,
						     GFP_KERNEL);
		if (!vnic->rss_table) {
			rc = -ENOMEM;
			goto out;
		}

		vnic->rss_hash_key = ((void *)vnic->rss_table) + size;
		vnic->rss_hash_key_dma_addr = vnic->rss_table_dma_addr + size;
	}
	return 0;

out:
	return rc;
}

static void bnxt_free_hwrm_resources(struct bnxt *bp)
{
	struct pci_dev *pdev = bp->pdev;

	if (bp->hwrm_cmd_resp_addr) {
		dma_free_coherent(&pdev->dev, PAGE_SIZE, bp->hwrm_cmd_resp_addr,
				  bp->hwrm_cmd_resp_dma_addr);
		bp->hwrm_cmd_resp_addr = NULL;
	}

	if (bp->hwrm_cmd_kong_resp_addr) {
		dma_free_coherent(&pdev->dev, PAGE_SIZE,
				  bp->hwrm_cmd_kong_resp_addr,
				  bp->hwrm_cmd_kong_resp_dma_addr);
		bp->hwrm_cmd_kong_resp_addr = NULL;
	}
}

static int bnxt_alloc_kong_hwrm_resources(struct bnxt *bp)
{
	struct pci_dev *pdev = bp->pdev;

	if (bp->hwrm_cmd_kong_resp_addr)
		return 0;

	bp->hwrm_cmd_kong_resp_addr =
		dma_alloc_coherent(&pdev->dev, PAGE_SIZE,
				   &bp->hwrm_cmd_kong_resp_dma_addr,
				   GFP_KERNEL);
	if (!bp->hwrm_cmd_kong_resp_addr)
		return -ENOMEM;

	return 0;
}

static int bnxt_alloc_hwrm_resources(struct bnxt *bp)
{
	struct pci_dev *pdev = bp->pdev;

	bp->hwrm_cmd_resp_addr = dma_alloc_coherent(&pdev->dev, PAGE_SIZE,
						   &bp->hwrm_cmd_resp_dma_addr,
						   GFP_KERNEL);
	if (!bp->hwrm_cmd_resp_addr)
		return -ENOMEM;

	return 0;
}

static void bnxt_free_hwrm_short_cmd_req(struct bnxt *bp)
{
	if (bp->hwrm_short_cmd_req_addr) {
		struct pci_dev *pdev = bp->pdev;

		dma_free_coherent(&pdev->dev, bp->hwrm_max_ext_req_len,
				  bp->hwrm_short_cmd_req_addr,
				  bp->hwrm_short_cmd_req_dma_addr);
		bp->hwrm_short_cmd_req_addr = NULL;
	}
}

static int bnxt_alloc_hwrm_short_cmd_req(struct bnxt *bp)
{
	struct pci_dev *pdev = bp->pdev;

	if (bp->hwrm_short_cmd_req_addr)
		return 0;

	bp->hwrm_short_cmd_req_addr =
		dma_alloc_coherent(&pdev->dev, bp->hwrm_max_ext_req_len,
				   &bp->hwrm_short_cmd_req_dma_addr,
				   GFP_KERNEL);
	if (!bp->hwrm_short_cmd_req_addr)
		return -ENOMEM;

	return 0;
}

static void bnxt_free_stats_mem(struct bnxt *bp, struct bnxt_stats_mem *stats)
{
	kfree(stats->hw_masks);
	stats->hw_masks = NULL;
	kfree(stats->sw_stats);
	stats->sw_stats = NULL;
	if (stats->hw_stats) {
		dma_free_coherent(&bp->pdev->dev, stats->len, stats->hw_stats,
				  stats->hw_stats_map);
		stats->hw_stats = NULL;
	}
}

static int bnxt_alloc_stats_mem(struct bnxt *bp, struct bnxt_stats_mem *stats,
				bool alloc_masks)
{
	stats->hw_stats = dma_alloc_coherent(&bp->pdev->dev, stats->len,
					     &stats->hw_stats_map, GFP_KERNEL);
	if (!stats->hw_stats)
		return -ENOMEM;

	stats->sw_stats = kzalloc(stats->len, GFP_KERNEL);
	if (!stats->sw_stats)
		goto stats_mem_err;

	if (alloc_masks) {
		stats->hw_masks = kzalloc(stats->len, GFP_KERNEL);
		if (!stats->hw_masks)
			goto stats_mem_err;
	}
	return 0;

stats_mem_err:
	bnxt_free_stats_mem(bp, stats);
	return -ENOMEM;
}

static void bnxt_fill_masks(u64 *mask_arr, u64 mask, int count)
{
	int i;

	for (i = 0; i < count; i++)
		mask_arr[i] = mask;
}

static void bnxt_copy_hw_masks(u64 *mask_arr, __le64 *hw_mask_arr, int count)
{
	int i;

	for (i = 0; i < count; i++)
		mask_arr[i] = le64_to_cpu(hw_mask_arr[i]);
}

static int bnxt_hwrm_func_qstat_ext(struct bnxt *bp,
				    struct bnxt_stats_mem *stats)
{
	struct hwrm_func_qstats_ext_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_func_qstats_ext_input req = {0};
	__le64 *hw_masks;
	int rc;

	if (!(bp->fw_cap & BNXT_FW_CAP_EXT_HW_STATS_SUPPORTED) ||
	    !(bp->flags & BNXT_FLAG_CHIP_P5))
		return -EOPNOTSUPP;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_QSTATS_EXT, -1, -1);
	req.fid = cpu_to_le16(0xffff);
	req.flags = FUNC_QSTATS_EXT_REQ_FLAGS_COUNTER_MASK;
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		goto qstat_exit;

	hw_masks = &resp->rx_ucast_pkts;
	bnxt_copy_hw_masks(stats->hw_masks, hw_masks, stats->len / 8);

qstat_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_port_qstats(struct bnxt *bp, u8 flags);
static int bnxt_hwrm_port_qstats_ext(struct bnxt *bp, u8 flags);

static void bnxt_init_stats(struct bnxt *bp)
{
	struct bnxt_napi *bnapi = bp->bnapi[0];
	struct bnxt_cp_ring_info *cpr;
	struct bnxt_stats_mem *stats;
	__le64 *rx_stats, *tx_stats;
	int rc, rx_count, tx_count;
	u64 *rx_masks, *tx_masks;
	u64 mask;
	u8 flags;

	cpr = &bnapi->cp_ring;
	stats = &cpr->stats;
	rc = bnxt_hwrm_func_qstat_ext(bp, stats);
	if (rc) {
		if (bp->flags & BNXT_FLAG_CHIP_P5)
			mask = (1ULL << 48) - 1;
		else
			mask = -1ULL;
		bnxt_fill_masks(stats->hw_masks, mask, stats->len / 8);
	}
	if (bp->flags & BNXT_FLAG_PORT_STATS) {
		stats = &bp->port_stats;
		rx_stats = stats->hw_stats;
		rx_masks = stats->hw_masks;
		rx_count = sizeof(struct rx_port_stats) / 8;
		tx_stats = rx_stats + BNXT_TX_PORT_STATS_BYTE_OFFSET / 8;
		tx_masks = rx_masks + BNXT_TX_PORT_STATS_BYTE_OFFSET / 8;
		tx_count = sizeof(struct tx_port_stats) / 8;

		flags = PORT_QSTATS_REQ_FLAGS_COUNTER_MASK;
		rc = bnxt_hwrm_port_qstats(bp, flags);
		if (rc) {
			mask = (1ULL << 40) - 1;

			bnxt_fill_masks(rx_masks, mask, rx_count);
			bnxt_fill_masks(tx_masks, mask, tx_count);
		} else {
			bnxt_copy_hw_masks(rx_masks, rx_stats, rx_count);
			bnxt_copy_hw_masks(tx_masks, tx_stats, tx_count);
			bnxt_hwrm_port_qstats(bp, 0);
		}
	}
	if (bp->flags & BNXT_FLAG_PORT_STATS_EXT) {
		stats = &bp->rx_port_stats_ext;
		rx_stats = stats->hw_stats;
		rx_masks = stats->hw_masks;
		rx_count = sizeof(struct rx_port_stats_ext) / 8;
		stats = &bp->tx_port_stats_ext;
		tx_stats = stats->hw_stats;
		tx_masks = stats->hw_masks;
		tx_count = sizeof(struct tx_port_stats_ext) / 8;

		flags = PORT_QSTATS_EXT_REQ_FLAGS_COUNTER_MASK;
		rc = bnxt_hwrm_port_qstats_ext(bp, flags);
		if (rc) {
			mask = (1ULL << 40) - 1;

			bnxt_fill_masks(rx_masks, mask, rx_count);
			if (tx_stats)
				bnxt_fill_masks(tx_masks, mask, tx_count);
		} else {
			bnxt_copy_hw_masks(rx_masks, rx_stats, rx_count);
			if (tx_stats)
				bnxt_copy_hw_masks(tx_masks, tx_stats,
						   tx_count);
			bnxt_hwrm_port_qstats_ext(bp, 0);
		}
	}
}

static void bnxt_free_port_stats(struct bnxt *bp)
{
	bp->flags &= ~BNXT_FLAG_PORT_STATS;
	bp->flags &= ~BNXT_FLAG_PORT_STATS_EXT;

	bnxt_free_stats_mem(bp, &bp->port_stats);
	bnxt_free_stats_mem(bp, &bp->rx_port_stats_ext);
	bnxt_free_stats_mem(bp, &bp->tx_port_stats_ext);
}

static void bnxt_free_ring_stats(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;

		bnxt_free_stats_mem(bp, &cpr->stats);
	}
}

static int bnxt_alloc_stats(struct bnxt *bp)
{
	u32 size, i;
	int rc;

	size = bp->hw_ring_stats_size;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;

		cpr->stats.len = size;
		rc = bnxt_alloc_stats_mem(bp, &cpr->stats, !i);
		if (rc)
			return rc;

		cpr->hw_stats_ctx_id = INVALID_STATS_CTX_ID;
	}

	if (BNXT_VF(bp) || bp->chip_num == CHIP_NUM_58700)
		return 0;

	if (bp->port_stats.hw_stats)
		goto alloc_ext_stats;

	bp->port_stats.len = BNXT_PORT_STATS_SIZE;
	rc = bnxt_alloc_stats_mem(bp, &bp->port_stats, true);
	if (rc)
		return rc;

	bp->flags |= BNXT_FLAG_PORT_STATS;

alloc_ext_stats:
	/* Display extended statistics only if FW supports it */
	if (bp->hwrm_spec_code < 0x10804 || bp->hwrm_spec_code == 0x10900)
		if (!(bp->fw_cap & BNXT_FW_CAP_EXT_STATS_SUPPORTED))
			return 0;

	if (bp->rx_port_stats_ext.hw_stats)
		goto alloc_tx_ext_stats;

	bp->rx_port_stats_ext.len = sizeof(struct rx_port_stats_ext);
	rc = bnxt_alloc_stats_mem(bp, &bp->rx_port_stats_ext, true);
	/* Extended stats are optional */
	if (rc)
		return 0;

alloc_tx_ext_stats:
	if (bp->tx_port_stats_ext.hw_stats)
		return 0;

	if (bp->hwrm_spec_code >= 0x10902 ||
	    (bp->fw_cap & BNXT_FW_CAP_EXT_STATS_SUPPORTED)) {
		bp->tx_port_stats_ext.len = sizeof(struct tx_port_stats_ext);
		rc = bnxt_alloc_stats_mem(bp, &bp->tx_port_stats_ext, true);
		/* Extended stats are optional */
		if (rc)
			return 0;
	}
	bp->flags |= BNXT_FLAG_PORT_STATS_EXT;
	return 0;
}

static void bnxt_clear_ring_indices(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr;
		struct bnxt_rx_ring_info *rxr;
		struct bnxt_tx_ring_info *txr;

		if (!bnapi)
			continue;

		cpr = &bnapi->cp_ring;
		cpr->cp_raw_cons = 0;

		txr = bnapi->tx_ring;
		if (txr) {
			txr->tx_prod = 0;
			txr->tx_cons = 0;
		}

		rxr = bnapi->rx_ring;
		if (rxr) {
			rxr->rx_prod = 0;
			rxr->rx_agg_prod = 0;
			rxr->rx_sw_agg_prod = 0;
			rxr->rx_next_cons = 0;
		}
	}
}

static void bnxt_free_ntp_fltrs(struct bnxt *bp, bool irq_reinit)
{
#ifdef CONFIG_RFS_ACCEL
	int i;

	/* Under rtnl_lock and all our NAPIs have been disabled.  It's
	 * safe to delete the hash table.
	 */
	for (i = 0; i < BNXT_NTP_FLTR_HASH_SIZE; i++) {
		struct hlist_head *head;
		struct hlist_node *tmp;
		struct bnxt_ntuple_filter *fltr;

		head = &bp->ntp_fltr_hash_tbl[i];
		hlist_for_each_entry_safe(fltr, tmp, head, hash) {
			hlist_del(&fltr->hash);
			kfree(fltr);
		}
	}
	if (irq_reinit) {
		kfree(bp->ntp_fltr_bmap);
		bp->ntp_fltr_bmap = NULL;
	}
	bp->ntp_fltr_count = 0;
#endif
}

static int bnxt_alloc_ntp_fltrs(struct bnxt *bp)
{
#ifdef CONFIG_RFS_ACCEL
	int i, rc = 0;

	if (!(bp->flags & BNXT_FLAG_RFS))
		return 0;

	for (i = 0; i < BNXT_NTP_FLTR_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&bp->ntp_fltr_hash_tbl[i]);

	bp->ntp_fltr_count = 0;
	bp->ntp_fltr_bmap = kcalloc(BITS_TO_LONGS(BNXT_NTP_FLTR_MAX_FLTR),
				    sizeof(long),
				    GFP_KERNEL);

	if (!bp->ntp_fltr_bmap)
		rc = -ENOMEM;

	return rc;
#else
	return 0;
#endif
}

static void bnxt_free_mem(struct bnxt *bp, bool irq_re_init)
{
	bnxt_free_vnic_attributes(bp);
	bnxt_free_tx_rings(bp);
	bnxt_free_rx_rings(bp);
	bnxt_free_cp_rings(bp);
	bnxt_free_ntp_fltrs(bp, irq_re_init);
	if (irq_re_init) {
		bnxt_free_ring_stats(bp);
		if (!(bp->fw_cap & BNXT_FW_CAP_PORT_STATS_NO_RESET) ||
		    test_bit(BNXT_STATE_IN_FW_RESET, &bp->state))
			bnxt_free_port_stats(bp);
		bnxt_free_ring_grps(bp);
		bnxt_free_vnics(bp);
		kfree(bp->tx_ring_map);
		bp->tx_ring_map = NULL;
		kfree(bp->tx_ring);
		bp->tx_ring = NULL;
		kfree(bp->rx_ring);
		bp->rx_ring = NULL;
		kfree(bp->bnapi);
		bp->bnapi = NULL;
	} else {
		bnxt_clear_ring_indices(bp);
	}
}

static int bnxt_alloc_mem(struct bnxt *bp, bool irq_re_init)
{
	int i, j, rc, size, arr_size;
	void *bnapi;

	if (irq_re_init) {
		/* Allocate bnapi mem pointer array and mem block for
		 * all queues
		 */
		arr_size = L1_CACHE_ALIGN(sizeof(struct bnxt_napi *) *
				bp->cp_nr_rings);
		size = L1_CACHE_ALIGN(sizeof(struct bnxt_napi));
		bnapi = kzalloc(arr_size + size * bp->cp_nr_rings, GFP_KERNEL);
		if (!bnapi)
			return -ENOMEM;

		bp->bnapi = bnapi;
		bnapi += arr_size;
		for (i = 0; i < bp->cp_nr_rings; i++, bnapi += size) {
			bp->bnapi[i] = bnapi;
			bp->bnapi[i]->index = i;
			bp->bnapi[i]->bp = bp;
			if (bp->flags & BNXT_FLAG_CHIP_P5) {
				struct bnxt_cp_ring_info *cpr =
					&bp->bnapi[i]->cp_ring;

				cpr->cp_ring_struct.ring_mem.flags =
					BNXT_RMEM_RING_PTE_FLAG;
			}
		}

		bp->rx_ring = kcalloc(bp->rx_nr_rings,
				      sizeof(struct bnxt_rx_ring_info),
				      GFP_KERNEL);
		if (!bp->rx_ring)
			return -ENOMEM;

		for (i = 0; i < bp->rx_nr_rings; i++) {
			struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];

			if (bp->flags & BNXT_FLAG_CHIP_P5) {
				rxr->rx_ring_struct.ring_mem.flags =
					BNXT_RMEM_RING_PTE_FLAG;
				rxr->rx_agg_ring_struct.ring_mem.flags =
					BNXT_RMEM_RING_PTE_FLAG;
			}
			rxr->bnapi = bp->bnapi[i];
			bp->bnapi[i]->rx_ring = &bp->rx_ring[i];
		}

		bp->tx_ring = kcalloc(bp->tx_nr_rings,
				      sizeof(struct bnxt_tx_ring_info),
				      GFP_KERNEL);
		if (!bp->tx_ring)
			return -ENOMEM;

		bp->tx_ring_map = kcalloc(bp->tx_nr_rings, sizeof(u16),
					  GFP_KERNEL);

		if (!bp->tx_ring_map)
			return -ENOMEM;

		if (bp->flags & BNXT_FLAG_SHARED_RINGS)
			j = 0;
		else
			j = bp->rx_nr_rings;

		for (i = 0; i < bp->tx_nr_rings; i++, j++) {
			struct bnxt_tx_ring_info *txr = &bp->tx_ring[i];

			if (bp->flags & BNXT_FLAG_CHIP_P5)
				txr->tx_ring_struct.ring_mem.flags =
					BNXT_RMEM_RING_PTE_FLAG;
			txr->bnapi = bp->bnapi[j];
			bp->bnapi[j]->tx_ring = txr;
			bp->tx_ring_map[i] = bp->tx_nr_rings_xdp + i;
			if (i >= bp->tx_nr_rings_xdp) {
				txr->txq_index = i - bp->tx_nr_rings_xdp;
				bp->bnapi[j]->tx_int = bnxt_tx_int;
			} else {
				bp->bnapi[j]->flags |= BNXT_NAPI_FLAG_XDP;
				bp->bnapi[j]->tx_int = bnxt_tx_int_xdp;
			}
		}

		rc = bnxt_alloc_stats(bp);
		if (rc)
			goto alloc_mem_err;
		bnxt_init_stats(bp);

		rc = bnxt_alloc_ntp_fltrs(bp);
		if (rc)
			goto alloc_mem_err;

		rc = bnxt_alloc_vnics(bp);
		if (rc)
			goto alloc_mem_err;
	}

	bnxt_init_ring_struct(bp);

	rc = bnxt_alloc_rx_rings(bp);
	if (rc)
		goto alloc_mem_err;

	rc = bnxt_alloc_tx_rings(bp);
	if (rc)
		goto alloc_mem_err;

	rc = bnxt_alloc_cp_rings(bp);
	if (rc)
		goto alloc_mem_err;

	bp->vnic_info[0].flags |= BNXT_VNIC_RSS_FLAG | BNXT_VNIC_MCAST_FLAG |
				  BNXT_VNIC_UCAST_FLAG;
	rc = bnxt_alloc_vnic_attributes(bp);
	if (rc)
		goto alloc_mem_err;
	return 0;

alloc_mem_err:
	bnxt_free_mem(bp, true);
	return rc;
}

static void bnxt_disable_int(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
		struct bnxt_ring_struct *ring = &cpr->cp_ring_struct;

		if (ring->fw_ring_id != INVALID_HW_RING_ID)
			bnxt_db_nq(bp, &cpr->cp_db, cpr->cp_raw_cons);
	}
}

static int bnxt_cp_num_to_irq_num(struct bnxt *bp, int n)
{
	struct bnxt_napi *bnapi = bp->bnapi[n];
	struct bnxt_cp_ring_info *cpr;

	cpr = &bnapi->cp_ring;
	return cpr->cp_ring_struct.map_idx;
}

static void bnxt_disable_int_sync(struct bnxt *bp)
{
	int i;

	atomic_inc(&bp->intr_sem);

	bnxt_disable_int(bp);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		int map_idx = bnxt_cp_num_to_irq_num(bp, i);

		synchronize_irq(bp->irq_tbl[map_idx].vector);
	}
}

static void bnxt_enable_int(struct bnxt *bp)
{
	int i;

	atomic_set(&bp->intr_sem, 0);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;

		bnxt_db_nq_arm(bp, &cpr->cp_db, cpr->cp_raw_cons);
	}
}

void bnxt_hwrm_cmd_hdr_init(struct bnxt *bp, void *request, u16 req_type,
			    u16 cmpl_ring, u16 target_id)
{
	struct input *req = request;

	req->req_type = cpu_to_le16(req_type);
	req->cmpl_ring = cpu_to_le16(cmpl_ring);
	req->target_id = cpu_to_le16(target_id);
	if (bnxt_kong_hwrm_message(bp, req))
		req->resp_addr = cpu_to_le64(bp->hwrm_cmd_kong_resp_dma_addr);
	else
		req->resp_addr = cpu_to_le64(bp->hwrm_cmd_resp_dma_addr);
}

static int bnxt_hwrm_to_stderr(u32 hwrm_err)
{
	switch (hwrm_err) {
	case HWRM_ERR_CODE_SUCCESS:
		return 0;
	case HWRM_ERR_CODE_RESOURCE_LOCKED:
		return -EROFS;
	case HWRM_ERR_CODE_RESOURCE_ACCESS_DENIED:
		return -EACCES;
	case HWRM_ERR_CODE_RESOURCE_ALLOC_ERROR:
		return -ENOSPC;
	case HWRM_ERR_CODE_INVALID_PARAMS:
	case HWRM_ERR_CODE_INVALID_FLAGS:
	case HWRM_ERR_CODE_INVALID_ENABLES:
	case HWRM_ERR_CODE_UNSUPPORTED_TLV:
	case HWRM_ERR_CODE_UNSUPPORTED_OPTION_ERR:
		return -EINVAL;
	case HWRM_ERR_CODE_NO_BUFFER:
		return -ENOMEM;
	case HWRM_ERR_CODE_HOT_RESET_PROGRESS:
	case HWRM_ERR_CODE_BUSY:
		return -EAGAIN;
	case HWRM_ERR_CODE_CMD_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	default:
		return -EIO;
	}
}

static int bnxt_hwrm_do_send_msg(struct bnxt *bp, void *msg, u32 msg_len,
				 int timeout, bool silent)
{
	int i, intr_process, rc, tmo_count;
	struct input *req = msg;
	u32 *data = msg;
	u8 *valid;
	u16 cp_ring_id, len = 0;
	struct hwrm_err_output *resp = bp->hwrm_cmd_resp_addr;
	u16 max_req_len = BNXT_HWRM_MAX_REQ_LEN;
	struct hwrm_short_input short_input = {0};
	u32 doorbell_offset = BNXT_GRCPF_REG_CHIMP_COMM_TRIGGER;
	u32 bar_offset = BNXT_GRCPF_REG_CHIMP_COMM;
	u16 dst = BNXT_HWRM_CHNL_CHIMP;

	if (BNXT_NO_FW_ACCESS(bp) &&
	    le16_to_cpu(req->req_type) != HWRM_FUNC_RESET)
		return -EBUSY;

	if (msg_len > BNXT_HWRM_MAX_REQ_LEN) {
		if (msg_len > bp->hwrm_max_ext_req_len ||
		    !bp->hwrm_short_cmd_req_addr)
			return -EINVAL;
	}

	if (bnxt_hwrm_kong_chnl(bp, req)) {
		dst = BNXT_HWRM_CHNL_KONG;
		bar_offset = BNXT_GRCPF_REG_KONG_COMM;
		doorbell_offset = BNXT_GRCPF_REG_KONG_COMM_TRIGGER;
		resp = bp->hwrm_cmd_kong_resp_addr;
	}

	memset(resp, 0, PAGE_SIZE);
	cp_ring_id = le16_to_cpu(req->cmpl_ring);
	intr_process = (cp_ring_id == INVALID_HW_RING_ID) ? 0 : 1;

	req->seq_id = cpu_to_le16(bnxt_get_hwrm_seq_id(bp, dst));
	/* currently supports only one outstanding message */
	if (intr_process)
		bp->hwrm_intr_seq_id = le16_to_cpu(req->seq_id);

	if ((bp->fw_cap & BNXT_FW_CAP_SHORT_CMD) ||
	    msg_len > BNXT_HWRM_MAX_REQ_LEN) {
		void *short_cmd_req = bp->hwrm_short_cmd_req_addr;
		u16 max_msg_len;

		/* Set boundary for maximum extended request length for short
		 * cmd format. If passed up from device use the max supported
		 * internal req length.
		 */
		max_msg_len = bp->hwrm_max_ext_req_len;

		memcpy(short_cmd_req, req, msg_len);
		if (msg_len < max_msg_len)
			memset(short_cmd_req + msg_len, 0,
			       max_msg_len - msg_len);

		short_input.req_type = req->req_type;
		short_input.signature =
				cpu_to_le16(SHORT_REQ_SIGNATURE_SHORT_CMD);
		short_input.size = cpu_to_le16(msg_len);
		short_input.req_addr =
			cpu_to_le64(bp->hwrm_short_cmd_req_dma_addr);

		data = (u32 *)&short_input;
		msg_len = sizeof(short_input);

		/* Sync memory write before updating doorbell */
		wmb();

		max_req_len = BNXT_HWRM_SHORT_REQ_LEN;
	}

	/* Write request msg to hwrm channel */
	__iowrite32_copy(bp->bar0 + bar_offset, data, msg_len / 4);

	for (i = msg_len; i < max_req_len; i += 4)
		writel(0, bp->bar0 + bar_offset + i);

	/* Ring channel doorbell */
	writel(1, bp->bar0 + doorbell_offset);

	if (!pci_is_enabled(bp->pdev))
		return 0;

	if (!timeout)
		timeout = DFLT_HWRM_CMD_TIMEOUT;
	/* convert timeout to usec */
	timeout *= 1000;

	i = 0;
	/* Short timeout for the first few iterations:
	 * number of loops = number of loops for short timeout +
	 * number of loops for standard timeout.
	 */
	tmo_count = HWRM_SHORT_TIMEOUT_COUNTER;
	timeout = timeout - HWRM_SHORT_MIN_TIMEOUT * HWRM_SHORT_TIMEOUT_COUNTER;
	tmo_count += DIV_ROUND_UP(timeout, HWRM_MIN_TIMEOUT);

	if (intr_process) {
		u16 seq_id = bp->hwrm_intr_seq_id;

		/* Wait until hwrm response cmpl interrupt is processed */
		while (bp->hwrm_intr_seq_id != (u16)~seq_id &&
		       i++ < tmo_count) {
			/* Abort the wait for completion if the FW health
			 * check has failed.
			 */
			if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))
				return -EBUSY;
			/* on first few passes, just barely sleep */
			if (i < HWRM_SHORT_TIMEOUT_COUNTER)
				usleep_range(HWRM_SHORT_MIN_TIMEOUT,
					     HWRM_SHORT_MAX_TIMEOUT);
			else
				usleep_range(HWRM_MIN_TIMEOUT,
					     HWRM_MAX_TIMEOUT);
		}

		if (bp->hwrm_intr_seq_id != (u16)~seq_id) {
			if (!silent)
				netdev_err(bp->dev, "Resp cmpl intr err msg: 0x%x\n",
					   le16_to_cpu(req->req_type));
			return -EBUSY;
		}
		len = le16_to_cpu(resp->resp_len);
		valid = ((u8 *)resp) + len - 1;
	} else {
		int j;

		/* Check if response len is updated */
		for (i = 0; i < tmo_count; i++) {
			/* Abort the wait for completion if the FW health
			 * check has failed.
			 */
			if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))
				return -EBUSY;
			len = le16_to_cpu(resp->resp_len);
			if (len)
				break;
			/* on first few passes, just barely sleep */
			if (i < HWRM_SHORT_TIMEOUT_COUNTER)
				usleep_range(HWRM_SHORT_MIN_TIMEOUT,
					     HWRM_SHORT_MAX_TIMEOUT);
			else
				usleep_range(HWRM_MIN_TIMEOUT,
					     HWRM_MAX_TIMEOUT);
		}

		if (i >= tmo_count) {
			if (!silent)
				netdev_err(bp->dev, "Error (timeout: %d) msg {0x%x 0x%x} len:%d\n",
					   HWRM_TOTAL_TIMEOUT(i),
					   le16_to_cpu(req->req_type),
					   le16_to_cpu(req->seq_id), len);
			return -EBUSY;
		}

		/* Last byte of resp contains valid bit */
		valid = ((u8 *)resp) + len - 1;
		for (j = 0; j < HWRM_VALID_BIT_DELAY_USEC; j++) {
			/* make sure we read from updated DMA memory */
			dma_rmb();
			if (*valid)
				break;
			usleep_range(1, 5);
		}

		if (j >= HWRM_VALID_BIT_DELAY_USEC) {
			if (!silent)
				netdev_err(bp->dev, "Error (timeout: %d) msg {0x%x 0x%x} len:%d v:%d\n",
					   HWRM_TOTAL_TIMEOUT(i),
					   le16_to_cpu(req->req_type),
					   le16_to_cpu(req->seq_id), len,
					   *valid);
			return -EBUSY;
		}
	}

	/* Zero valid bit for compatibility.  Valid bit in an older spec
	 * may become a new field in a newer spec.  We must make sure that
	 * a new field not implemented by old spec will read zero.
	 */
	*valid = 0;
	rc = le16_to_cpu(resp->error_code);
	if (rc && !silent)
		netdev_err(bp->dev, "hwrm req_type 0x%x seq id 0x%x error 0x%x\n",
			   le16_to_cpu(resp->req_type),
			   le16_to_cpu(resp->seq_id), rc);
	return bnxt_hwrm_to_stderr(rc);
}

int _hwrm_send_message(struct bnxt *bp, void *msg, u32 msg_len, int timeout)
{
	return bnxt_hwrm_do_send_msg(bp, msg, msg_len, timeout, false);
}

int _hwrm_send_message_silent(struct bnxt *bp, void *msg, u32 msg_len,
			      int timeout)
{
	return bnxt_hwrm_do_send_msg(bp, msg, msg_len, timeout, true);
}

int hwrm_send_message(struct bnxt *bp, void *msg, u32 msg_len, int timeout)
{
	int rc;

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, msg, msg_len, timeout);
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

int hwrm_send_message_silent(struct bnxt *bp, void *msg, u32 msg_len,
			     int timeout)
{
	int rc;

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = bnxt_hwrm_do_send_msg(bp, msg, msg_len, timeout, true);
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

int bnxt_hwrm_func_drv_rgtr(struct bnxt *bp, unsigned long *bmap, int bmap_size,
			    bool async_only)
{
	struct hwrm_func_drv_rgtr_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_func_drv_rgtr_input req = {0};
	DECLARE_BITMAP(async_events_bmap, 256);
	u32 *events = (u32 *)async_events_bmap;
	u32 flags;
	int rc, i;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_DRV_RGTR, -1, -1);

	req.enables =
		cpu_to_le32(FUNC_DRV_RGTR_REQ_ENABLES_OS_TYPE |
			    FUNC_DRV_RGTR_REQ_ENABLES_VER |
			    FUNC_DRV_RGTR_REQ_ENABLES_ASYNC_EVENT_FWD);

	req.os_type = cpu_to_le16(FUNC_DRV_RGTR_REQ_OS_TYPE_LINUX);
	flags = FUNC_DRV_RGTR_REQ_FLAGS_16BIT_VER_MODE;
	if (bp->fw_cap & BNXT_FW_CAP_HOT_RESET)
		flags |= FUNC_DRV_RGTR_REQ_FLAGS_HOT_RESET_SUPPORT;
	if (bp->fw_cap & BNXT_FW_CAP_ERROR_RECOVERY)
		flags |= FUNC_DRV_RGTR_REQ_FLAGS_ERROR_RECOVERY_SUPPORT |
			 FUNC_DRV_RGTR_REQ_FLAGS_MASTER_SUPPORT;
	req.flags = cpu_to_le32(flags);
	req.ver_maj_8b = DRV_VER_MAJ;
	req.ver_min_8b = DRV_VER_MIN;
	req.ver_upd_8b = DRV_VER_UPD;
	req.ver_maj = cpu_to_le16(DRV_VER_MAJ);
	req.ver_min = cpu_to_le16(DRV_VER_MIN);
	req.ver_upd = cpu_to_le16(DRV_VER_UPD);

	if (BNXT_PF(bp)) {
		u32 data[8];
		int i;

		memset(data, 0, sizeof(data));
		for (i = 0; i < ARRAY_SIZE(bnxt_vf_req_snif); i++) {
			u16 cmd = bnxt_vf_req_snif[i];
			unsigned int bit, idx;

			idx = cmd / 32;
			bit = cmd % 32;
			data[idx] |= 1 << bit;
		}

		for (i = 0; i < 8; i++)
			req.vf_req_fwd[i] = cpu_to_le32(data[i]);

		req.enables |=
			cpu_to_le32(FUNC_DRV_RGTR_REQ_ENABLES_VF_REQ_FWD);
	}

	if (bp->fw_cap & BNXT_FW_CAP_OVS_64BIT_HANDLE)
		req.flags |= cpu_to_le32(
			FUNC_DRV_RGTR_REQ_FLAGS_FLOW_HANDLE_64BIT_MODE);

	memset(async_events_bmap, 0, sizeof(async_events_bmap));
	for (i = 0; i < ARRAY_SIZE(bnxt_async_events_arr); i++) {
		u16 event_id = bnxt_async_events_arr[i];

		if (event_id == ASYNC_EVENT_CMPL_EVENT_ID_ERROR_RECOVERY &&
		    !(bp->fw_cap & BNXT_FW_CAP_ERROR_RECOVERY))
			continue;
		__set_bit(bnxt_async_events_arr[i], async_events_bmap);
	}
	if (bmap && bmap_size) {
		for (i = 0; i < bmap_size; i++) {
			if (test_bit(i, bmap))
				__set_bit(i, async_events_bmap);
		}
	}
	for (i = 0; i < 8; i++)
		req.async_event_fwd[i] |= cpu_to_le32(events[i]);

	if (async_only)
		req.enables =
			cpu_to_le32(FUNC_DRV_RGTR_REQ_ENABLES_ASYNC_EVENT_FWD);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		set_bit(BNXT_STATE_DRV_REGISTERED, &bp->state);
		if (resp->flags &
		    cpu_to_le32(FUNC_DRV_RGTR_RESP_FLAGS_IF_CHANGE_SUPPORTED))
			bp->fw_cap |= BNXT_FW_CAP_IF_CHANGE;
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_func_drv_unrgtr(struct bnxt *bp)
{
	struct hwrm_func_drv_unrgtr_input req = {0};

	if (!test_and_clear_bit(BNXT_STATE_DRV_REGISTERED, &bp->state))
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_DRV_UNRGTR, -1, -1);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_hwrm_tunnel_dst_port_free(struct bnxt *bp, u8 tunnel_type)
{
	u32 rc = 0;
	struct hwrm_tunnel_dst_port_free_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_TUNNEL_DST_PORT_FREE, -1, -1);
	req.tunnel_type = tunnel_type;

	switch (tunnel_type) {
	case TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_VXLAN:
		req.tunnel_dst_port_id = cpu_to_le16(bp->vxlan_fw_dst_port_id);
		bp->vxlan_fw_dst_port_id = INVALID_HW_RING_ID;
		break;
	case TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_GENEVE:
		req.tunnel_dst_port_id = cpu_to_le16(bp->nge_fw_dst_port_id);
		bp->nge_fw_dst_port_id = INVALID_HW_RING_ID;
		break;
	default:
		break;
	}

	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		netdev_err(bp->dev, "hwrm_tunnel_dst_port_free failed. rc:%d\n",
			   rc);
	return rc;
}

static int bnxt_hwrm_tunnel_dst_port_alloc(struct bnxt *bp, __be16 port,
					   u8 tunnel_type)
{
	u32 rc = 0;
	struct hwrm_tunnel_dst_port_alloc_input req = {0};
	struct hwrm_tunnel_dst_port_alloc_output *resp = bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_TUNNEL_DST_PORT_ALLOC, -1, -1);

	req.tunnel_type = tunnel_type;
	req.tunnel_dst_port_val = port;

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc) {
		netdev_err(bp->dev, "hwrm_tunnel_dst_port_alloc failed. rc:%d\n",
			   rc);
		goto err_out;
	}

	switch (tunnel_type) {
	case TUNNEL_DST_PORT_ALLOC_REQ_TUNNEL_TYPE_VXLAN:
		bp->vxlan_fw_dst_port_id =
			le16_to_cpu(resp->tunnel_dst_port_id);
		break;
	case TUNNEL_DST_PORT_ALLOC_REQ_TUNNEL_TYPE_GENEVE:
		bp->nge_fw_dst_port_id = le16_to_cpu(resp->tunnel_dst_port_id);
		break;
	default:
		break;
	}

err_out:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_cfa_l2_set_rx_mask(struct bnxt *bp, u16 vnic_id)
{
	struct hwrm_cfa_l2_set_rx_mask_input req = {0};
	struct bnxt_vnic_info *vnic = &bp->vnic_info[vnic_id];

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_L2_SET_RX_MASK, -1, -1);
	req.vnic_id = cpu_to_le32(vnic->fw_vnic_id);

	req.num_mc_entries = cpu_to_le32(vnic->mc_list_count);
	req.mc_tbl_addr = cpu_to_le64(vnic->mc_list_mapping);
	req.mask = cpu_to_le32(vnic->rx_mask);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

#ifdef CONFIG_RFS_ACCEL
static int bnxt_hwrm_cfa_ntuple_filter_free(struct bnxt *bp,
					    struct bnxt_ntuple_filter *fltr)
{
	struct hwrm_cfa_ntuple_filter_free_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_NTUPLE_FILTER_FREE, -1, -1);
	req.ntuple_filter_id = fltr->filter_id;
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

#define BNXT_NTP_FLTR_FLAGS					\
	(CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_L2_FILTER_ID |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_ETHERTYPE |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_MACADDR |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_IPADDR_TYPE |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_IPADDR |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_IPADDR_MASK |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_IPADDR |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_IPADDR_MASK |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_IP_PROTOCOL |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_PORT |		\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_PORT_MASK |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_PORT |		\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_PORT_MASK |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_ID)

#define BNXT_NTP_TUNNEL_FLTR_FLAG				\
		CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_TUNNEL_TYPE

static int bnxt_hwrm_cfa_ntuple_filter_alloc(struct bnxt *bp,
					     struct bnxt_ntuple_filter *fltr)
{
	struct hwrm_cfa_ntuple_filter_alloc_input req = {0};
	struct hwrm_cfa_ntuple_filter_alloc_output *resp;
	struct flow_keys *keys = &fltr->fkeys;
	struct bnxt_vnic_info *vnic;
	u32 flags = 0;
	int rc = 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_NTUPLE_FILTER_ALLOC, -1, -1);
	req.l2_filter_id = bp->vnic_info[0].fw_l2_filter_id[fltr->l2_fltr_idx];

	if (bp->fw_cap & BNXT_FW_CAP_CFA_RFS_RING_TBL_IDX_V2) {
		flags = CFA_NTUPLE_FILTER_ALLOC_REQ_FLAGS_DEST_RFS_RING_IDX;
		req.dst_id = cpu_to_le16(fltr->rxq);
	} else {
		vnic = &bp->vnic_info[fltr->rxq + 1];
		req.dst_id = cpu_to_le16(vnic->fw_vnic_id);
	}
	req.flags = cpu_to_le32(flags);
	req.enables = cpu_to_le32(BNXT_NTP_FLTR_FLAGS);

	req.ethertype = htons(ETH_P_IP);
	memcpy(req.src_macaddr, fltr->src_mac_addr, ETH_ALEN);
	req.ip_addr_type = CFA_NTUPLE_FILTER_ALLOC_REQ_IP_ADDR_TYPE_IPV4;
	req.ip_protocol = keys->basic.ip_proto;

	if (keys->basic.n_proto == htons(ETH_P_IPV6)) {
		int i;

		req.ethertype = htons(ETH_P_IPV6);
		req.ip_addr_type =
			CFA_NTUPLE_FILTER_ALLOC_REQ_IP_ADDR_TYPE_IPV6;
		*(struct in6_addr *)&req.src_ipaddr[0] =
			keys->addrs.v6addrs.src;
		*(struct in6_addr *)&req.dst_ipaddr[0] =
			keys->addrs.v6addrs.dst;
		for (i = 0; i < 4; i++) {
			req.src_ipaddr_mask[i] = cpu_to_be32(0xffffffff);
			req.dst_ipaddr_mask[i] = cpu_to_be32(0xffffffff);
		}
	} else {
		req.src_ipaddr[0] = keys->addrs.v4addrs.src;
		req.src_ipaddr_mask[0] = cpu_to_be32(0xffffffff);
		req.dst_ipaddr[0] = keys->addrs.v4addrs.dst;
		req.dst_ipaddr_mask[0] = cpu_to_be32(0xffffffff);
	}
	if (keys->control.flags & FLOW_DIS_ENCAPSULATION) {
		req.enables |= cpu_to_le32(BNXT_NTP_TUNNEL_FLTR_FLAG);
		req.tunnel_type =
			CFA_NTUPLE_FILTER_ALLOC_REQ_TUNNEL_TYPE_ANYTUNNEL;
	}

	req.src_port = keys->ports.src;
	req.src_port_mask = cpu_to_be16(0xffff);
	req.dst_port = keys->ports.dst;
	req.dst_port_mask = cpu_to_be16(0xffff);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		resp = bnxt_get_hwrm_resp_addr(bp, &req);
		fltr->filter_id = resp->ntuple_filter_id;
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}
#endif

static int bnxt_hwrm_set_vnic_filter(struct bnxt *bp, u16 vnic_id, u16 idx,
				     u8 *mac_addr)
{
	u32 rc = 0;
	struct hwrm_cfa_l2_filter_alloc_input req = {0};
	struct hwrm_cfa_l2_filter_alloc_output *resp = bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_L2_FILTER_ALLOC, -1, -1);
	req.flags = cpu_to_le32(CFA_L2_FILTER_ALLOC_REQ_FLAGS_PATH_RX);
	if (!BNXT_CHIP_TYPE_NITRO_A0(bp))
		req.flags |=
			cpu_to_le32(CFA_L2_FILTER_ALLOC_REQ_FLAGS_OUTERMOST);
	req.dst_id = cpu_to_le16(bp->vnic_info[vnic_id].fw_vnic_id);
	req.enables =
		cpu_to_le32(CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_ADDR |
			    CFA_L2_FILTER_ALLOC_REQ_ENABLES_DST_ID |
			    CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_ADDR_MASK);
	memcpy(req.l2_addr, mac_addr, ETH_ALEN);
	req.l2_addr_mask[0] = 0xff;
	req.l2_addr_mask[1] = 0xff;
	req.l2_addr_mask[2] = 0xff;
	req.l2_addr_mask[3] = 0xff;
	req.l2_addr_mask[4] = 0xff;
	req.l2_addr_mask[5] = 0xff;

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		bp->vnic_info[vnic_id].fw_l2_filter_id[idx] =
							resp->l2_filter_id;
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_clear_vnic_filter(struct bnxt *bp)
{
	u16 i, j, num_of_vnics = 1; /* only vnic 0 supported */
	int rc = 0;

	/* Any associated ntuple filters will also be cleared by firmware. */
	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 0; i < num_of_vnics; i++) {
		struct bnxt_vnic_info *vnic = &bp->vnic_info[i];

		for (j = 0; j < vnic->uc_filter_count; j++) {
			struct hwrm_cfa_l2_filter_free_input req = {0};

			bnxt_hwrm_cmd_hdr_init(bp, &req,
					       HWRM_CFA_L2_FILTER_FREE, -1, -1);

			req.l2_filter_id = vnic->fw_l2_filter_id[j];

			rc = _hwrm_send_message(bp, &req, sizeof(req),
						HWRM_CMD_TIMEOUT);
		}
		vnic->uc_filter_count = 0;
	}
	mutex_unlock(&bp->hwrm_cmd_lock);

	return rc;
}

static int bnxt_hwrm_vnic_set_tpa(struct bnxt *bp, u16 vnic_id, u32 tpa_flags)
{
	struct bnxt_vnic_info *vnic = &bp->vnic_info[vnic_id];
	u16 max_aggs = VNIC_TPA_CFG_REQ_MAX_AGGS_MAX;
	struct hwrm_vnic_tpa_cfg_input req = {0};

	if (vnic->fw_vnic_id == INVALID_HW_RING_ID)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_TPA_CFG, -1, -1);

	if (tpa_flags) {
		u16 mss = bp->dev->mtu - 40;
		u32 nsegs, n, segs = 0, flags;

		flags = VNIC_TPA_CFG_REQ_FLAGS_TPA |
			VNIC_TPA_CFG_REQ_FLAGS_ENCAP_TPA |
			VNIC_TPA_CFG_REQ_FLAGS_RSC_WND_UPDATE |
			VNIC_TPA_CFG_REQ_FLAGS_AGG_WITH_ECN |
			VNIC_TPA_CFG_REQ_FLAGS_AGG_WITH_SAME_GRE_SEQ;
		if (tpa_flags & BNXT_FLAG_GRO)
			flags |= VNIC_TPA_CFG_REQ_FLAGS_GRO;

		req.flags = cpu_to_le32(flags);

		req.enables =
			cpu_to_le32(VNIC_TPA_CFG_REQ_ENABLES_MAX_AGG_SEGS |
				    VNIC_TPA_CFG_REQ_ENABLES_MAX_AGGS |
				    VNIC_TPA_CFG_REQ_ENABLES_MIN_AGG_LEN);

		/* Number of segs are log2 units, and first packet is not
		 * included as part of this units.
		 */
		if (mss <= BNXT_RX_PAGE_SIZE) {
			n = BNXT_RX_PAGE_SIZE / mss;
			nsegs = (MAX_SKB_FRAGS - 1) * n;
		} else {
			n = mss / BNXT_RX_PAGE_SIZE;
			if (mss & (BNXT_RX_PAGE_SIZE - 1))
				n++;
			nsegs = (MAX_SKB_FRAGS - n) / n;
		}

		if (bp->flags & BNXT_FLAG_CHIP_P5) {
			segs = MAX_TPA_SEGS_P5;
			max_aggs = bp->max_tpa;
		} else {
			segs = ilog2(nsegs);
		}
		req.max_agg_segs = cpu_to_le16(segs);
		req.max_aggs = cpu_to_le16(max_aggs);

		req.min_agg_len = cpu_to_le32(512);
	}
	req.vnic_id = cpu_to_le16(vnic->fw_vnic_id);

	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static u16 bnxt_cp_ring_from_grp(struct bnxt *bp, struct bnxt_ring_struct *ring)
{
	struct bnxt_ring_grp_info *grp_info;

	grp_info = &bp->grp_info[ring->grp_idx];
	return grp_info->cp_fw_ring_id;
}

static u16 bnxt_cp_ring_for_rx(struct bnxt *bp, struct bnxt_rx_ring_info *rxr)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		struct bnxt_napi *bnapi = rxr->bnapi;
		struct bnxt_cp_ring_info *cpr;

		cpr = bnapi->cp_ring.cp_ring_arr[BNXT_RX_HDL];
		return cpr->cp_ring_struct.fw_ring_id;
	} else {
		return bnxt_cp_ring_from_grp(bp, &rxr->rx_ring_struct);
	}
}

static u16 bnxt_cp_ring_for_tx(struct bnxt *bp, struct bnxt_tx_ring_info *txr)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		struct bnxt_napi *bnapi = txr->bnapi;
		struct bnxt_cp_ring_info *cpr;

		cpr = bnapi->cp_ring.cp_ring_arr[BNXT_TX_HDL];
		return cpr->cp_ring_struct.fw_ring_id;
	} else {
		return bnxt_cp_ring_from_grp(bp, &txr->tx_ring_struct);
	}
}

static int bnxt_alloc_rss_indir_tbl(struct bnxt *bp)
{
	int entries;

	if (bp->flags & BNXT_FLAG_CHIP_P5)
		entries = BNXT_MAX_RSS_TABLE_ENTRIES_P5;
	else
		entries = HW_HASH_INDEX_SIZE;

	bp->rss_indir_tbl_entries = entries;
	bp->rss_indir_tbl = kmalloc_array(entries, sizeof(*bp->rss_indir_tbl),
					  GFP_KERNEL);
	if (!bp->rss_indir_tbl)
		return -ENOMEM;
	return 0;
}

static void bnxt_set_dflt_rss_indir_tbl(struct bnxt *bp)
{
	u16 max_rings, max_entries, pad, i;

	if (!bp->rx_nr_rings)
		return;

	if (BNXT_CHIP_TYPE_NITRO_A0(bp))
		max_rings = bp->rx_nr_rings - 1;
	else
		max_rings = bp->rx_nr_rings;

	max_entries = bnxt_get_rxfh_indir_size(bp->dev);

	for (i = 0; i < max_entries; i++)
		bp->rss_indir_tbl[i] = ethtool_rxfh_indir_default(i, max_rings);

	pad = bp->rss_indir_tbl_entries - max_entries;
	if (pad)
		memset(&bp->rss_indir_tbl[i], 0, pad * sizeof(u16));
}

static u16 bnxt_get_max_rss_ring(struct bnxt *bp)
{
	u16 i, tbl_size, max_ring = 0;

	if (!bp->rss_indir_tbl)
		return 0;

	tbl_size = bnxt_get_rxfh_indir_size(bp->dev);
	for (i = 0; i < tbl_size; i++)
		max_ring = max(max_ring, bp->rss_indir_tbl[i]);
	return max_ring;
}

int bnxt_get_nr_rss_ctxs(struct bnxt *bp, int rx_rings)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5)
		return DIV_ROUND_UP(rx_rings, BNXT_RSS_TABLE_ENTRIES_P5);
	if (BNXT_CHIP_TYPE_NITRO_A0(bp))
		return 2;
	return 1;
}

static void __bnxt_fill_hw_rss_tbl(struct bnxt *bp, struct bnxt_vnic_info *vnic)
{
	bool no_rss = !(vnic->flags & BNXT_VNIC_RSS_FLAG);
	u16 i, j;

	/* Fill the RSS indirection table with ring group ids */
	for (i = 0, j = 0; i < HW_HASH_INDEX_SIZE; i++) {
		if (!no_rss)
			j = bp->rss_indir_tbl[i];
		vnic->rss_table[i] = cpu_to_le16(vnic->fw_grp_ids[j]);
	}
}

static void __bnxt_fill_hw_rss_tbl_p5(struct bnxt *bp,
				      struct bnxt_vnic_info *vnic)
{
	__le16 *ring_tbl = vnic->rss_table;
	struct bnxt_rx_ring_info *rxr;
	u16 tbl_size, i;

	tbl_size = bnxt_get_rxfh_indir_size(bp->dev);

	for (i = 0; i < tbl_size; i++) {
		u16 ring_id, j;

		j = bp->rss_indir_tbl[i];
		rxr = &bp->rx_ring[j];

		ring_id = rxr->rx_ring_struct.fw_ring_id;
		*ring_tbl++ = cpu_to_le16(ring_id);
		ring_id = bnxt_cp_ring_for_rx(bp, rxr);
		*ring_tbl++ = cpu_to_le16(ring_id);
	}
}

static void bnxt_fill_hw_rss_tbl(struct bnxt *bp, struct bnxt_vnic_info *vnic)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5)
		__bnxt_fill_hw_rss_tbl_p5(bp, vnic);
	else
		__bnxt_fill_hw_rss_tbl(bp, vnic);
}

static int bnxt_hwrm_vnic_set_rss(struct bnxt *bp, u16 vnic_id, bool set_rss)
{
	struct bnxt_vnic_info *vnic = &bp->vnic_info[vnic_id];
	struct hwrm_vnic_rss_cfg_input req = {0};

	if ((bp->flags & BNXT_FLAG_CHIP_P5) ||
	    vnic->fw_rss_cos_lb_ctx[0] == INVALID_HW_RING_ID)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_RSS_CFG, -1, -1);
	if (set_rss) {
		bnxt_fill_hw_rss_tbl(bp, vnic);
		req.hash_type = cpu_to_le32(bp->rss_hash_cfg);
		req.hash_mode_flags = VNIC_RSS_CFG_REQ_HASH_MODE_FLAGS_DEFAULT;
		req.ring_grp_tbl_addr = cpu_to_le64(vnic->rss_table_dma_addr);
		req.hash_key_tbl_addr =
			cpu_to_le64(vnic->rss_hash_key_dma_addr);
	}
	req.rss_ctx_idx = cpu_to_le16(vnic->fw_rss_cos_lb_ctx[0]);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_hwrm_vnic_set_rss_p5(struct bnxt *bp, u16 vnic_id, bool set_rss)
{
	struct bnxt_vnic_info *vnic = &bp->vnic_info[vnic_id];
	struct hwrm_vnic_rss_cfg_input req = {0};
	dma_addr_t ring_tbl_map;
	u32 i, nr_ctxs;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_RSS_CFG, -1, -1);
	req.vnic_id = cpu_to_le16(vnic->fw_vnic_id);
	if (!set_rss) {
		hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
		return 0;
	}
	bnxt_fill_hw_rss_tbl(bp, vnic);
	req.hash_type = cpu_to_le32(bp->rss_hash_cfg);
	req.hash_mode_flags = VNIC_RSS_CFG_REQ_HASH_MODE_FLAGS_DEFAULT;
	req.hash_key_tbl_addr = cpu_to_le64(vnic->rss_hash_key_dma_addr);
	ring_tbl_map = vnic->rss_table_dma_addr;
	nr_ctxs = bnxt_get_nr_rss_ctxs(bp, bp->rx_nr_rings);
	for (i = 0; i < nr_ctxs; ring_tbl_map += BNXT_RSS_TABLE_SIZE_P5, i++) {
		int rc;

		req.ring_grp_tbl_addr = cpu_to_le64(ring_tbl_map);
		req.ring_table_pair_index = i;
		req.rss_ctx_idx = cpu_to_le16(vnic->fw_rss_cos_lb_ctx[i]);
		rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
		if (rc)
			return rc;
	}
	return 0;
}

static int bnxt_hwrm_vnic_set_hds(struct bnxt *bp, u16 vnic_id)
{
	struct bnxt_vnic_info *vnic = &bp->vnic_info[vnic_id];
	struct hwrm_vnic_plcmodes_cfg_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_PLCMODES_CFG, -1, -1);
	req.flags = cpu_to_le32(VNIC_PLCMODES_CFG_REQ_FLAGS_JUMBO_PLACEMENT |
				VNIC_PLCMODES_CFG_REQ_FLAGS_HDS_IPV4 |
				VNIC_PLCMODES_CFG_REQ_FLAGS_HDS_IPV6);
	req.enables =
		cpu_to_le32(VNIC_PLCMODES_CFG_REQ_ENABLES_JUMBO_THRESH_VALID |
			    VNIC_PLCMODES_CFG_REQ_ENABLES_HDS_THRESHOLD_VALID);
	/* thresholds not implemented in firmware yet */
	req.jumbo_thresh = cpu_to_le16(bp->rx_copy_thresh);
	req.hds_threshold = cpu_to_le16(bp->rx_copy_thresh);
	req.vnic_id = cpu_to_le32(vnic->fw_vnic_id);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static void bnxt_hwrm_vnic_ctx_free_one(struct bnxt *bp, u16 vnic_id,
					u16 ctx_idx)
{
	struct hwrm_vnic_rss_cos_lb_ctx_free_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_RSS_COS_LB_CTX_FREE, -1, -1);
	req.rss_cos_lb_ctx_id =
		cpu_to_le16(bp->vnic_info[vnic_id].fw_rss_cos_lb_ctx[ctx_idx]);

	hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	bp->vnic_info[vnic_id].fw_rss_cos_lb_ctx[ctx_idx] = INVALID_HW_RING_ID;
}

static void bnxt_hwrm_vnic_ctx_free(struct bnxt *bp)
{
	int i, j;

	for (i = 0; i < bp->nr_vnics; i++) {
		struct bnxt_vnic_info *vnic = &bp->vnic_info[i];

		for (j = 0; j < BNXT_MAX_CTX_PER_VNIC; j++) {
			if (vnic->fw_rss_cos_lb_ctx[j] != INVALID_HW_RING_ID)
				bnxt_hwrm_vnic_ctx_free_one(bp, i, j);
		}
	}
	bp->rsscos_nr_ctxs = 0;
}

static int bnxt_hwrm_vnic_ctx_alloc(struct bnxt *bp, u16 vnic_id, u16 ctx_idx)
{
	int rc;
	struct hwrm_vnic_rss_cos_lb_ctx_alloc_input req = {0};
	struct hwrm_vnic_rss_cos_lb_ctx_alloc_output *resp =
						bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_RSS_COS_LB_CTX_ALLOC, -1,
			       -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		bp->vnic_info[vnic_id].fw_rss_cos_lb_ctx[ctx_idx] =
			le16_to_cpu(resp->rss_cos_lb_ctx_id);
	mutex_unlock(&bp->hwrm_cmd_lock);

	return rc;
}

static u32 bnxt_get_roce_vnic_mode(struct bnxt *bp)
{
	if (bp->flags & BNXT_FLAG_ROCE_MIRROR_CAP)
		return VNIC_CFG_REQ_FLAGS_ROCE_MIRRORING_CAPABLE_VNIC_MODE;
	return VNIC_CFG_REQ_FLAGS_ROCE_DUAL_VNIC_MODE;
}

int bnxt_hwrm_vnic_cfg(struct bnxt *bp, u16 vnic_id)
{
	unsigned int ring = 0, grp_idx;
	struct bnxt_vnic_info *vnic = &bp->vnic_info[vnic_id];
	struct hwrm_vnic_cfg_input req = {0};
	u16 def_vlan = 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_CFG, -1, -1);

	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[0];

		req.default_rx_ring_id =
			cpu_to_le16(rxr->rx_ring_struct.fw_ring_id);
		req.default_cmpl_ring_id =
			cpu_to_le16(bnxt_cp_ring_for_rx(bp, rxr));
		req.enables =
			cpu_to_le32(VNIC_CFG_REQ_ENABLES_DEFAULT_RX_RING_ID |
				    VNIC_CFG_REQ_ENABLES_DEFAULT_CMPL_RING_ID);
		goto vnic_mru;
	}
	req.enables = cpu_to_le32(VNIC_CFG_REQ_ENABLES_DFLT_RING_GRP);
	/* Only RSS support for now TBD: COS & LB */
	if (vnic->fw_rss_cos_lb_ctx[0] != INVALID_HW_RING_ID) {
		req.rss_rule = cpu_to_le16(vnic->fw_rss_cos_lb_ctx[0]);
		req.enables |= cpu_to_le32(VNIC_CFG_REQ_ENABLES_RSS_RULE |
					   VNIC_CFG_REQ_ENABLES_MRU);
	} else if (vnic->flags & BNXT_VNIC_RFS_NEW_RSS_FLAG) {
		req.rss_rule =
			cpu_to_le16(bp->vnic_info[0].fw_rss_cos_lb_ctx[0]);
		req.enables |= cpu_to_le32(VNIC_CFG_REQ_ENABLES_RSS_RULE |
					   VNIC_CFG_REQ_ENABLES_MRU);
		req.flags |= cpu_to_le32(VNIC_CFG_REQ_FLAGS_RSS_DFLT_CR_MODE);
	} else {
		req.rss_rule = cpu_to_le16(0xffff);
	}

	if (BNXT_CHIP_TYPE_NITRO_A0(bp) &&
	    (vnic->fw_rss_cos_lb_ctx[0] != INVALID_HW_RING_ID)) {
		req.cos_rule = cpu_to_le16(vnic->fw_rss_cos_lb_ctx[1]);
		req.enables |= cpu_to_le32(VNIC_CFG_REQ_ENABLES_COS_RULE);
	} else {
		req.cos_rule = cpu_to_le16(0xffff);
	}

	if (vnic->flags & BNXT_VNIC_RSS_FLAG)
		ring = 0;
	else if (vnic->flags & BNXT_VNIC_RFS_FLAG)
		ring = vnic_id - 1;
	else if ((vnic_id == 1) && BNXT_CHIP_TYPE_NITRO_A0(bp))
		ring = bp->rx_nr_rings - 1;

	grp_idx = bp->rx_ring[ring].bnapi->index;
	req.dflt_ring_grp = cpu_to_le16(bp->grp_info[grp_idx].fw_grp_id);
	req.lb_rule = cpu_to_le16(0xffff);
vnic_mru:
	req.mru = cpu_to_le16(bp->dev->mtu + ETH_HLEN + VLAN_HLEN);

	req.vnic_id = cpu_to_le16(vnic->fw_vnic_id);
#ifdef CONFIG_BNXT_SRIOV
	if (BNXT_VF(bp))
		def_vlan = bp->vf.vlan;
#endif
	if ((bp->flags & BNXT_FLAG_STRIP_VLAN) || def_vlan)
		req.flags |= cpu_to_le32(VNIC_CFG_REQ_FLAGS_VLAN_STRIP_MODE);
	if (!vnic_id && bnxt_ulp_registered(bp->edev, BNXT_ROCE_ULP))
		req.flags |= cpu_to_le32(bnxt_get_roce_vnic_mode(bp));

	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static void bnxt_hwrm_vnic_free_one(struct bnxt *bp, u16 vnic_id)
{
	if (bp->vnic_info[vnic_id].fw_vnic_id != INVALID_HW_RING_ID) {
		struct hwrm_vnic_free_input req = {0};

		bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_FREE, -1, -1);
		req.vnic_id =
			cpu_to_le32(bp->vnic_info[vnic_id].fw_vnic_id);

		hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
		bp->vnic_info[vnic_id].fw_vnic_id = INVALID_HW_RING_ID;
	}
}

static void bnxt_hwrm_vnic_free(struct bnxt *bp)
{
	u16 i;

	for (i = 0; i < bp->nr_vnics; i++)
		bnxt_hwrm_vnic_free_one(bp, i);
}

static int bnxt_hwrm_vnic_alloc(struct bnxt *bp, u16 vnic_id,
				unsigned int start_rx_ring_idx,
				unsigned int nr_rings)
{
	int rc = 0;
	unsigned int i, j, grp_idx, end_idx = start_rx_ring_idx + nr_rings;
	struct hwrm_vnic_alloc_input req = {0};
	struct hwrm_vnic_alloc_output *resp = bp->hwrm_cmd_resp_addr;
	struct bnxt_vnic_info *vnic = &bp->vnic_info[vnic_id];

	if (bp->flags & BNXT_FLAG_CHIP_P5)
		goto vnic_no_ring_grps;

	/* map ring groups to this vnic */
	for (i = start_rx_ring_idx, j = 0; i < end_idx; i++, j++) {
		grp_idx = bp->rx_ring[i].bnapi->index;
		if (bp->grp_info[grp_idx].fw_grp_id == INVALID_HW_RING_ID) {
			netdev_err(bp->dev, "Not enough ring groups avail:%x req:%x\n",
				   j, nr_rings);
			break;
		}
		vnic->fw_grp_ids[j] = bp->grp_info[grp_idx].fw_grp_id;
	}

vnic_no_ring_grps:
	for (i = 0; i < BNXT_MAX_CTX_PER_VNIC; i++)
		vnic->fw_rss_cos_lb_ctx[i] = INVALID_HW_RING_ID;
	if (vnic_id == 0)
		req.flags = cpu_to_le32(VNIC_ALLOC_REQ_FLAGS_DEFAULT);

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_ALLOC, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		vnic->fw_vnic_id = le32_to_cpu(resp->vnic_id);
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_vnic_qcaps(struct bnxt *bp)
{
	struct hwrm_vnic_qcaps_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_vnic_qcaps_input req = {0};
	int rc;

	bp->hw_ring_stats_size = sizeof(struct ctx_hw_stats);
	bp->flags &= ~(BNXT_FLAG_NEW_RSS_CAP | BNXT_FLAG_ROCE_MIRROR_CAP);
	if (bp->hwrm_spec_code < 0x10600)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VNIC_QCAPS, -1, -1);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		u32 flags = le32_to_cpu(resp->flags);

		if (!(bp->flags & BNXT_FLAG_CHIP_P5) &&
		    (flags & VNIC_QCAPS_RESP_FLAGS_RSS_DFLT_CR_CAP))
			bp->flags |= BNXT_FLAG_NEW_RSS_CAP;
		if (flags &
		    VNIC_QCAPS_RESP_FLAGS_ROCE_MIRRORING_CAPABLE_VNIC_CAP)
			bp->flags |= BNXT_FLAG_ROCE_MIRROR_CAP;

		/* Older P5 fw before EXT_HW_STATS support did not set
		 * VLAN_STRIP_CAP properly.
		 */
		if ((flags & VNIC_QCAPS_RESP_FLAGS_VLAN_STRIP_CAP) ||
		    (BNXT_CHIP_P5_THOR(bp) &&
		     !(bp->fw_cap & BNXT_FW_CAP_EXT_HW_STATS_SUPPORTED)))
			bp->fw_cap |= BNXT_FW_CAP_VLAN_RX_STRIP;
		bp->max_tpa_v2 = le16_to_cpu(resp->max_aggs_supported);
		if (bp->max_tpa_v2) {
			if (BNXT_CHIP_P5_THOR(bp))
				bp->hw_ring_stats_size = BNXT_RING_STATS_SIZE_P5;
			else
				bp->hw_ring_stats_size = BNXT_RING_STATS_SIZE_P5_SR2;
		}
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_ring_grp_alloc(struct bnxt *bp)
{
	u16 i;
	u32 rc = 0;

	if (bp->flags & BNXT_FLAG_CHIP_P5)
		return 0;

	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct hwrm_ring_grp_alloc_input req = {0};
		struct hwrm_ring_grp_alloc_output *resp =
					bp->hwrm_cmd_resp_addr;
		unsigned int grp_idx = bp->rx_ring[i].bnapi->index;

		bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_RING_GRP_ALLOC, -1, -1);

		req.cr = cpu_to_le16(bp->grp_info[grp_idx].cp_fw_ring_id);
		req.rr = cpu_to_le16(bp->grp_info[grp_idx].rx_fw_ring_id);
		req.ar = cpu_to_le16(bp->grp_info[grp_idx].agg_fw_ring_id);
		req.sc = cpu_to_le16(bp->grp_info[grp_idx].fw_stats_ctx);

		rc = _hwrm_send_message(bp, &req, sizeof(req),
					HWRM_CMD_TIMEOUT);
		if (rc)
			break;

		bp->grp_info[grp_idx].fw_grp_id =
			le32_to_cpu(resp->ring_group_id);
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static void bnxt_hwrm_ring_grp_free(struct bnxt *bp)
{
	u16 i;
	struct hwrm_ring_grp_free_input req = {0};

	if (!bp->grp_info || (bp->flags & BNXT_FLAG_CHIP_P5))
		return;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_RING_GRP_FREE, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		if (bp->grp_info[i].fw_grp_id == INVALID_HW_RING_ID)
			continue;
		req.ring_group_id =
			cpu_to_le32(bp->grp_info[i].fw_grp_id);

		_hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
		bp->grp_info[i].fw_grp_id = INVALID_HW_RING_ID;
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
}

static int hwrm_ring_alloc_send_msg(struct bnxt *bp,
				    struct bnxt_ring_struct *ring,
				    u32 ring_type, u32 map_index)
{
	int rc = 0, err = 0;
	struct hwrm_ring_alloc_input req = {0};
	struct hwrm_ring_alloc_output *resp = bp->hwrm_cmd_resp_addr;
	struct bnxt_ring_mem_info *rmem = &ring->ring_mem;
	struct bnxt_ring_grp_info *grp_info;
	u16 ring_id;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_RING_ALLOC, -1, -1);

	req.enables = 0;
	if (rmem->nr_pages > 1) {
		req.page_tbl_addr = cpu_to_le64(rmem->pg_tbl_map);
		/* Page size is in log2 units */
		req.page_size = BNXT_PAGE_SHIFT;
		req.page_tbl_depth = 1;
	} else {
		req.page_tbl_addr =  cpu_to_le64(rmem->dma_arr[0]);
	}
	req.fbo = 0;
	/* Association of ring index with doorbell index and MSIX number */
	req.logical_id = cpu_to_le16(map_index);

	switch (ring_type) {
	case HWRM_RING_ALLOC_TX: {
		struct bnxt_tx_ring_info *txr;

		txr = container_of(ring, struct bnxt_tx_ring_info,
				   tx_ring_struct);
		req.ring_type = RING_ALLOC_REQ_RING_TYPE_TX;
		/* Association of transmit ring with completion ring */
		grp_info = &bp->grp_info[ring->grp_idx];
		req.cmpl_ring_id = cpu_to_le16(bnxt_cp_ring_for_tx(bp, txr));
		req.length = cpu_to_le32(bp->tx_ring_mask + 1);
		req.stat_ctx_id = cpu_to_le32(grp_info->fw_stats_ctx);
		req.queue_id = cpu_to_le16(ring->queue_id);
		break;
	}
	case HWRM_RING_ALLOC_RX:
		req.ring_type = RING_ALLOC_REQ_RING_TYPE_RX;
		req.length = cpu_to_le32(bp->rx_ring_mask + 1);
		if (bp->flags & BNXT_FLAG_CHIP_P5) {
			u16 flags = 0;

			/* Association of rx ring with stats context */
			grp_info = &bp->grp_info[ring->grp_idx];
			req.rx_buf_size = cpu_to_le16(bp->rx_buf_use_size);
			req.stat_ctx_id = cpu_to_le32(grp_info->fw_stats_ctx);
			req.enables |= cpu_to_le32(
				RING_ALLOC_REQ_ENABLES_RX_BUF_SIZE_VALID);
			if (NET_IP_ALIGN == 2)
				flags = RING_ALLOC_REQ_FLAGS_RX_SOP_PAD;
			req.flags = cpu_to_le16(flags);
		}
		break;
	case HWRM_RING_ALLOC_AGG:
		if (bp->flags & BNXT_FLAG_CHIP_P5) {
			req.ring_type = RING_ALLOC_REQ_RING_TYPE_RX_AGG;
			/* Association of agg ring with rx ring */
			grp_info = &bp->grp_info[ring->grp_idx];
			req.rx_ring_id = cpu_to_le16(grp_info->rx_fw_ring_id);
			req.rx_buf_size = cpu_to_le16(BNXT_RX_PAGE_SIZE);
			req.stat_ctx_id = cpu_to_le32(grp_info->fw_stats_ctx);
			req.enables |= cpu_to_le32(
				RING_ALLOC_REQ_ENABLES_RX_RING_ID_VALID |
				RING_ALLOC_REQ_ENABLES_RX_BUF_SIZE_VALID);
		} else {
			req.ring_type = RING_ALLOC_REQ_RING_TYPE_RX;
		}
		req.length = cpu_to_le32(bp->rx_agg_ring_mask + 1);
		break;
	case HWRM_RING_ALLOC_CMPL:
		req.ring_type = RING_ALLOC_REQ_RING_TYPE_L2_CMPL;
		req.length = cpu_to_le32(bp->cp_ring_mask + 1);
		if (bp->flags & BNXT_FLAG_CHIP_P5) {
			/* Association of cp ring with nq */
			grp_info = &bp->grp_info[map_index];
			req.nq_ring_id = cpu_to_le16(grp_info->cp_fw_ring_id);
			req.cq_handle = cpu_to_le64(ring->handle);
			req.enables |= cpu_to_le32(
				RING_ALLOC_REQ_ENABLES_NQ_RING_ID_VALID);
		} else if (bp->flags & BNXT_FLAG_USING_MSIX) {
			req.int_mode = RING_ALLOC_REQ_INT_MODE_MSIX;
		}
		break;
	case HWRM_RING_ALLOC_NQ:
		req.ring_type = RING_ALLOC_REQ_RING_TYPE_NQ;
		req.length = cpu_to_le32(bp->cp_ring_mask + 1);
		if (bp->flags & BNXT_FLAG_USING_MSIX)
			req.int_mode = RING_ALLOC_REQ_INT_MODE_MSIX;
		break;
	default:
		netdev_err(bp->dev, "hwrm alloc invalid ring type %d\n",
			   ring_type);
		return -1;
	}

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	err = le16_to_cpu(resp->error_code);
	ring_id = le16_to_cpu(resp->ring_id);
	mutex_unlock(&bp->hwrm_cmd_lock);

	if (rc || err) {
		netdev_err(bp->dev, "hwrm_ring_alloc type %d failed. rc:%x err:%x\n",
			   ring_type, rc, err);
		return -EIO;
	}
	ring->fw_ring_id = ring_id;
	return rc;
}

static int bnxt_hwrm_set_async_event_cr(struct bnxt *bp, int idx)
{
	int rc;

	if (BNXT_PF(bp)) {
		struct hwrm_func_cfg_input req = {0};

		bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_CFG, -1, -1);
		req.fid = cpu_to_le16(0xffff);
		req.enables = cpu_to_le32(FUNC_CFG_REQ_ENABLES_ASYNC_EVENT_CR);
		req.async_event_cr = cpu_to_le16(idx);
		rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	} else {
		struct hwrm_func_vf_cfg_input req = {0};

		bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_VF_CFG, -1, -1);
		req.enables =
			cpu_to_le32(FUNC_VF_CFG_REQ_ENABLES_ASYNC_EVENT_CR);
		req.async_event_cr = cpu_to_le16(idx);
		rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	}
	return rc;
}

static void bnxt_set_db(struct bnxt *bp, struct bnxt_db_info *db, u32 ring_type,
			u32 map_idx, u32 xid)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		if (BNXT_PF(bp))
			db->doorbell = bp->bar1 + DB_PF_OFFSET_P5;
		else
			db->doorbell = bp->bar1 + DB_VF_OFFSET_P5;
		switch (ring_type) {
		case HWRM_RING_ALLOC_TX:
			db->db_key64 = DBR_PATH_L2 | DBR_TYPE_SQ;
			break;
		case HWRM_RING_ALLOC_RX:
		case HWRM_RING_ALLOC_AGG:
			db->db_key64 = DBR_PATH_L2 | DBR_TYPE_SRQ;
			break;
		case HWRM_RING_ALLOC_CMPL:
			db->db_key64 = DBR_PATH_L2;
			break;
		case HWRM_RING_ALLOC_NQ:
			db->db_key64 = DBR_PATH_L2;
			break;
		}
		db->db_key64 |= (u64)xid << DBR_XID_SFT;
	} else {
		db->doorbell = bp->bar1 + map_idx * 0x80;
		switch (ring_type) {
		case HWRM_RING_ALLOC_TX:
			db->db_key32 = DB_KEY_TX;
			break;
		case HWRM_RING_ALLOC_RX:
		case HWRM_RING_ALLOC_AGG:
			db->db_key32 = DB_KEY_RX;
			break;
		case HWRM_RING_ALLOC_CMPL:
			db->db_key32 = DB_KEY_CP;
			break;
		}
	}
}

static int bnxt_hwrm_ring_alloc(struct bnxt *bp)
{
	bool agg_rings = !!(bp->flags & BNXT_FLAG_AGG_RINGS);
	int i, rc = 0;
	u32 type;

	if (bp->flags & BNXT_FLAG_CHIP_P5)
		type = HWRM_RING_ALLOC_NQ;
	else
		type = HWRM_RING_ALLOC_CMPL;
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
		struct bnxt_ring_struct *ring = &cpr->cp_ring_struct;
		u32 map_idx = ring->map_idx;
		unsigned int vector;

		vector = bp->irq_tbl[map_idx].vector;
		disable_irq_nosync(vector);
		rc = hwrm_ring_alloc_send_msg(bp, ring, type, map_idx);
		if (rc) {
			enable_irq(vector);
			goto err_out;
		}
		bnxt_set_db(bp, &cpr->cp_db, type, map_idx, ring->fw_ring_id);
		bnxt_db_nq(bp, &cpr->cp_db, cpr->cp_raw_cons);
		enable_irq(vector);
		bp->grp_info[i].cp_fw_ring_id = ring->fw_ring_id;

		if (!i) {
			rc = bnxt_hwrm_set_async_event_cr(bp, ring->fw_ring_id);
			if (rc)
				netdev_warn(bp->dev, "Failed to set async event completion ring.\n");
		}
	}

	type = HWRM_RING_ALLOC_TX;
	for (i = 0; i < bp->tx_nr_rings; i++) {
		struct bnxt_tx_ring_info *txr = &bp->tx_ring[i];
		struct bnxt_ring_struct *ring;
		u32 map_idx;

		if (bp->flags & BNXT_FLAG_CHIP_P5) {
			struct bnxt_napi *bnapi = txr->bnapi;
			struct bnxt_cp_ring_info *cpr, *cpr2;
			u32 type2 = HWRM_RING_ALLOC_CMPL;

			cpr = &bnapi->cp_ring;
			cpr2 = cpr->cp_ring_arr[BNXT_TX_HDL];
			ring = &cpr2->cp_ring_struct;
			ring->handle = BNXT_TX_HDL;
			map_idx = bnapi->index;
			rc = hwrm_ring_alloc_send_msg(bp, ring, type2, map_idx);
			if (rc)
				goto err_out;
			bnxt_set_db(bp, &cpr2->cp_db, type2, map_idx,
				    ring->fw_ring_id);
			bnxt_db_cq(bp, &cpr2->cp_db, cpr2->cp_raw_cons);
		}
		ring = &txr->tx_ring_struct;
		map_idx = i;
		rc = hwrm_ring_alloc_send_msg(bp, ring, type, map_idx);
		if (rc)
			goto err_out;
		bnxt_set_db(bp, &txr->tx_db, type, map_idx, ring->fw_ring_id);
	}

	type = HWRM_RING_ALLOC_RX;
	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];
		struct bnxt_ring_struct *ring = &rxr->rx_ring_struct;
		struct bnxt_napi *bnapi = rxr->bnapi;
		u32 map_idx = bnapi->index;

		rc = hwrm_ring_alloc_send_msg(bp, ring, type, map_idx);
		if (rc)
			goto err_out;
		bnxt_set_db(bp, &rxr->rx_db, type, map_idx, ring->fw_ring_id);
		/* If we have agg rings, post agg buffers first. */
		if (!agg_rings)
			bnxt_db_write(bp, &rxr->rx_db, rxr->rx_prod);
		bp->grp_info[map_idx].rx_fw_ring_id = ring->fw_ring_id;
		if (bp->flags & BNXT_FLAG_CHIP_P5) {
			struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
			u32 type2 = HWRM_RING_ALLOC_CMPL;
			struct bnxt_cp_ring_info *cpr2;

			cpr2 = cpr->cp_ring_arr[BNXT_RX_HDL];
			ring = &cpr2->cp_ring_struct;
			ring->handle = BNXT_RX_HDL;
			rc = hwrm_ring_alloc_send_msg(bp, ring, type2, map_idx);
			if (rc)
				goto err_out;
			bnxt_set_db(bp, &cpr2->cp_db, type2, map_idx,
				    ring->fw_ring_id);
			bnxt_db_cq(bp, &cpr2->cp_db, cpr2->cp_raw_cons);
		}
	}

	if (agg_rings) {
		type = HWRM_RING_ALLOC_AGG;
		for (i = 0; i < bp->rx_nr_rings; i++) {
			struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];
			struct bnxt_ring_struct *ring =
						&rxr->rx_agg_ring_struct;
			u32 grp_idx = ring->grp_idx;
			u32 map_idx = grp_idx + bp->rx_nr_rings;

			rc = hwrm_ring_alloc_send_msg(bp, ring, type, map_idx);
			if (rc)
				goto err_out;

			bnxt_set_db(bp, &rxr->rx_agg_db, type, map_idx,
				    ring->fw_ring_id);
			bnxt_db_write(bp, &rxr->rx_agg_db, rxr->rx_agg_prod);
			bnxt_db_write(bp, &rxr->rx_db, rxr->rx_prod);
			bp->grp_info[grp_idx].agg_fw_ring_id = ring->fw_ring_id;
		}
	}
err_out:
	return rc;
}

static int hwrm_ring_free_send_msg(struct bnxt *bp,
				   struct bnxt_ring_struct *ring,
				   u32 ring_type, int cmpl_ring_id)
{
	int rc;
	struct hwrm_ring_free_input req = {0};
	struct hwrm_ring_free_output *resp = bp->hwrm_cmd_resp_addr;
	u16 error_code;

	if (BNXT_NO_FW_ACCESS(bp))
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_RING_FREE, cmpl_ring_id, -1);
	req.ring_type = ring_type;
	req.ring_id = cpu_to_le16(ring->fw_ring_id);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	error_code = le16_to_cpu(resp->error_code);
	mutex_unlock(&bp->hwrm_cmd_lock);

	if (rc || error_code) {
		netdev_err(bp->dev, "hwrm_ring_free type %d failed. rc:%x err:%x\n",
			   ring_type, rc, error_code);
		return -EIO;
	}
	return 0;
}

static void bnxt_hwrm_ring_free(struct bnxt *bp, bool close_path)
{
	u32 type;
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->tx_nr_rings; i++) {
		struct bnxt_tx_ring_info *txr = &bp->tx_ring[i];
		struct bnxt_ring_struct *ring = &txr->tx_ring_struct;

		if (ring->fw_ring_id != INVALID_HW_RING_ID) {
			u32 cmpl_ring_id = bnxt_cp_ring_for_tx(bp, txr);

			hwrm_ring_free_send_msg(bp, ring,
						RING_FREE_REQ_RING_TYPE_TX,
						close_path ? cmpl_ring_id :
						INVALID_HW_RING_ID);
			ring->fw_ring_id = INVALID_HW_RING_ID;
		}
	}

	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];
		struct bnxt_ring_struct *ring = &rxr->rx_ring_struct;
		u32 grp_idx = rxr->bnapi->index;

		if (ring->fw_ring_id != INVALID_HW_RING_ID) {
			u32 cmpl_ring_id = bnxt_cp_ring_for_rx(bp, rxr);

			hwrm_ring_free_send_msg(bp, ring,
						RING_FREE_REQ_RING_TYPE_RX,
						close_path ? cmpl_ring_id :
						INVALID_HW_RING_ID);
			ring->fw_ring_id = INVALID_HW_RING_ID;
			bp->grp_info[grp_idx].rx_fw_ring_id =
				INVALID_HW_RING_ID;
		}
	}

	if (bp->flags & BNXT_FLAG_CHIP_P5)
		type = RING_FREE_REQ_RING_TYPE_RX_AGG;
	else
		type = RING_FREE_REQ_RING_TYPE_RX;
	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];
		struct bnxt_ring_struct *ring = &rxr->rx_agg_ring_struct;
		u32 grp_idx = rxr->bnapi->index;

		if (ring->fw_ring_id != INVALID_HW_RING_ID) {
			u32 cmpl_ring_id = bnxt_cp_ring_for_rx(bp, rxr);

			hwrm_ring_free_send_msg(bp, ring, type,
						close_path ? cmpl_ring_id :
						INVALID_HW_RING_ID);
			ring->fw_ring_id = INVALID_HW_RING_ID;
			bp->grp_info[grp_idx].agg_fw_ring_id =
				INVALID_HW_RING_ID;
		}
	}

	/* The completion rings are about to be freed.  After that the
	 * IRQ doorbell will not work anymore.  So we need to disable
	 * IRQ here.
	 */
	bnxt_disable_int_sync(bp);

	if (bp->flags & BNXT_FLAG_CHIP_P5)
		type = RING_FREE_REQ_RING_TYPE_NQ;
	else
		type = RING_FREE_REQ_RING_TYPE_L2_CMPL;
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
		struct bnxt_ring_struct *ring;
		int j;

		for (j = 0; j < 2; j++) {
			struct bnxt_cp_ring_info *cpr2 = cpr->cp_ring_arr[j];

			if (cpr2) {
				ring = &cpr2->cp_ring_struct;
				if (ring->fw_ring_id == INVALID_HW_RING_ID)
					continue;
				hwrm_ring_free_send_msg(bp, ring,
					RING_FREE_REQ_RING_TYPE_L2_CMPL,
					INVALID_HW_RING_ID);
				ring->fw_ring_id = INVALID_HW_RING_ID;
			}
		}
		ring = &cpr->cp_ring_struct;
		if (ring->fw_ring_id != INVALID_HW_RING_ID) {
			hwrm_ring_free_send_msg(bp, ring, type,
						INVALID_HW_RING_ID);
			ring->fw_ring_id = INVALID_HW_RING_ID;
			bp->grp_info[i].cp_fw_ring_id = INVALID_HW_RING_ID;
		}
	}
}

static int bnxt_trim_rings(struct bnxt *bp, int *rx, int *tx, int max,
			   bool shared);

static int bnxt_hwrm_get_rings(struct bnxt *bp)
{
	struct hwrm_func_qcfg_output *resp = bp->hwrm_cmd_resp_addr;
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;
	struct hwrm_func_qcfg_input req = {0};
	int rc;

	if (bp->hwrm_spec_code < 0x10601)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_QCFG, -1, -1);
	req.fid = cpu_to_le16(0xffff);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc) {
		mutex_unlock(&bp->hwrm_cmd_lock);
		return rc;
	}

	hw_resc->resv_tx_rings = le16_to_cpu(resp->alloc_tx_rings);
	if (BNXT_NEW_RM(bp)) {
		u16 cp, stats;

		hw_resc->resv_rx_rings = le16_to_cpu(resp->alloc_rx_rings);
		hw_resc->resv_hw_ring_grps =
			le32_to_cpu(resp->alloc_hw_ring_grps);
		hw_resc->resv_vnics = le16_to_cpu(resp->alloc_vnics);
		cp = le16_to_cpu(resp->alloc_cmpl_rings);
		stats = le16_to_cpu(resp->alloc_stat_ctx);
		hw_resc->resv_irqs = cp;
		if (bp->flags & BNXT_FLAG_CHIP_P5) {
			int rx = hw_resc->resv_rx_rings;
			int tx = hw_resc->resv_tx_rings;

			if (bp->flags & BNXT_FLAG_AGG_RINGS)
				rx >>= 1;
			if (cp < (rx + tx)) {
				bnxt_trim_rings(bp, &rx, &tx, cp, false);
				if (bp->flags & BNXT_FLAG_AGG_RINGS)
					rx <<= 1;
				hw_resc->resv_rx_rings = rx;
				hw_resc->resv_tx_rings = tx;
			}
			hw_resc->resv_irqs = le16_to_cpu(resp->alloc_msix);
			hw_resc->resv_hw_ring_grps = rx;
		}
		hw_resc->resv_cp_rings = cp;
		hw_resc->resv_stat_ctxs = stats;
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return 0;
}

/* Caller must hold bp->hwrm_cmd_lock */
int __bnxt_hwrm_get_tx_rings(struct bnxt *bp, u16 fid, int *tx_rings)
{
	struct hwrm_func_qcfg_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_func_qcfg_input req = {0};
	int rc;

	if (bp->hwrm_spec_code < 0x10601)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_QCFG, -1, -1);
	req.fid = cpu_to_le16(fid);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		*tx_rings = le16_to_cpu(resp->alloc_tx_rings);

	return rc;
}

static bool bnxt_rfs_supported(struct bnxt *bp);

static void
__bnxt_hwrm_reserve_pf_rings(struct bnxt *bp, struct hwrm_func_cfg_input *req,
			     int tx_rings, int rx_rings, int ring_grps,
			     int cp_rings, int stats, int vnics)
{
	u32 enables = 0;

	bnxt_hwrm_cmd_hdr_init(bp, req, HWRM_FUNC_CFG, -1, -1);
	req->fid = cpu_to_le16(0xffff);
	enables |= tx_rings ? FUNC_CFG_REQ_ENABLES_NUM_TX_RINGS : 0;
	req->num_tx_rings = cpu_to_le16(tx_rings);
	if (BNXT_NEW_RM(bp)) {
		enables |= rx_rings ? FUNC_CFG_REQ_ENABLES_NUM_RX_RINGS : 0;
		enables |= stats ? FUNC_CFG_REQ_ENABLES_NUM_STAT_CTXS : 0;
		if (bp->flags & BNXT_FLAG_CHIP_P5) {
			enables |= cp_rings ? FUNC_CFG_REQ_ENABLES_NUM_MSIX : 0;
			enables |= tx_rings + ring_grps ?
				   FUNC_CFG_REQ_ENABLES_NUM_CMPL_RINGS : 0;
			enables |= rx_rings ?
				FUNC_CFG_REQ_ENABLES_NUM_RSSCOS_CTXS : 0;
		} else {
			enables |= cp_rings ?
				   FUNC_CFG_REQ_ENABLES_NUM_CMPL_RINGS : 0;
			enables |= ring_grps ?
				   FUNC_CFG_REQ_ENABLES_NUM_HW_RING_GRPS |
				   FUNC_CFG_REQ_ENABLES_NUM_RSSCOS_CTXS : 0;
		}
		enables |= vnics ? FUNC_CFG_REQ_ENABLES_NUM_VNICS : 0;

		req->num_rx_rings = cpu_to_le16(rx_rings);
		if (bp->flags & BNXT_FLAG_CHIP_P5) {
			req->num_cmpl_rings = cpu_to_le16(tx_rings + ring_grps);
			req->num_msix = cpu_to_le16(cp_rings);
			req->num_rsscos_ctxs =
				cpu_to_le16(DIV_ROUND_UP(ring_grps, 64));
		} else {
			req->num_cmpl_rings = cpu_to_le16(cp_rings);
			req->num_hw_ring_grps = cpu_to_le16(ring_grps);
			req->num_rsscos_ctxs = cpu_to_le16(1);
			if (!(bp->flags & BNXT_FLAG_NEW_RSS_CAP) &&
			    bnxt_rfs_supported(bp))
				req->num_rsscos_ctxs =
					cpu_to_le16(ring_grps + 1);
		}
		req->num_stat_ctxs = cpu_to_le16(stats);
		req->num_vnics = cpu_to_le16(vnics);
	}
	req->enables = cpu_to_le32(enables);
}

static void
__bnxt_hwrm_reserve_vf_rings(struct bnxt *bp,
			     struct hwrm_func_vf_cfg_input *req, int tx_rings,
			     int rx_rings, int ring_grps, int cp_rings,
			     int stats, int vnics)
{
	u32 enables = 0;

	bnxt_hwrm_cmd_hdr_init(bp, req, HWRM_FUNC_VF_CFG, -1, -1);
	enables |= tx_rings ? FUNC_VF_CFG_REQ_ENABLES_NUM_TX_RINGS : 0;
	enables |= rx_rings ? FUNC_VF_CFG_REQ_ENABLES_NUM_RX_RINGS |
			      FUNC_VF_CFG_REQ_ENABLES_NUM_RSSCOS_CTXS : 0;
	enables |= stats ? FUNC_VF_CFG_REQ_ENABLES_NUM_STAT_CTXS : 0;
	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		enables |= tx_rings + ring_grps ?
			   FUNC_VF_CFG_REQ_ENABLES_NUM_CMPL_RINGS : 0;
	} else {
		enables |= cp_rings ?
			   FUNC_VF_CFG_REQ_ENABLES_NUM_CMPL_RINGS : 0;
		enables |= ring_grps ?
			   FUNC_VF_CFG_REQ_ENABLES_NUM_HW_RING_GRPS : 0;
	}
	enables |= vnics ? FUNC_VF_CFG_REQ_ENABLES_NUM_VNICS : 0;
	enables |= FUNC_VF_CFG_REQ_ENABLES_NUM_L2_CTXS;

	req->num_l2_ctxs = cpu_to_le16(BNXT_VF_MAX_L2_CTX);
	req->num_tx_rings = cpu_to_le16(tx_rings);
	req->num_rx_rings = cpu_to_le16(rx_rings);
	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		req->num_cmpl_rings = cpu_to_le16(tx_rings + ring_grps);
		req->num_rsscos_ctxs = cpu_to_le16(DIV_ROUND_UP(ring_grps, 64));
	} else {
		req->num_cmpl_rings = cpu_to_le16(cp_rings);
		req->num_hw_ring_grps = cpu_to_le16(ring_grps);
		req->num_rsscos_ctxs = cpu_to_le16(BNXT_VF_MAX_RSS_CTX);
	}
	req->num_stat_ctxs = cpu_to_le16(stats);
	req->num_vnics = cpu_to_le16(vnics);

	req->enables = cpu_to_le32(enables);
}

static int
bnxt_hwrm_reserve_pf_rings(struct bnxt *bp, int tx_rings, int rx_rings,
			   int ring_grps, int cp_rings, int stats, int vnics)
{
	struct hwrm_func_cfg_input req = {0};
	int rc;

	__bnxt_hwrm_reserve_pf_rings(bp, &req, tx_rings, rx_rings, ring_grps,
				     cp_rings, stats, vnics);
	if (!req.enables)
		return 0;

	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		return rc;

	if (bp->hwrm_spec_code < 0x10601)
		bp->hw_resc.resv_tx_rings = tx_rings;

	return bnxt_hwrm_get_rings(bp);
}

static int
bnxt_hwrm_reserve_vf_rings(struct bnxt *bp, int tx_rings, int rx_rings,
			   int ring_grps, int cp_rings, int stats, int vnics)
{
	struct hwrm_func_vf_cfg_input req = {0};
	int rc;

	if (!BNXT_NEW_RM(bp)) {
		bp->hw_resc.resv_tx_rings = tx_rings;
		return 0;
	}

	__bnxt_hwrm_reserve_vf_rings(bp, &req, tx_rings, rx_rings, ring_grps,
				     cp_rings, stats, vnics);
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		return rc;

	return bnxt_hwrm_get_rings(bp);
}

static int bnxt_hwrm_reserve_rings(struct bnxt *bp, int tx, int rx, int grp,
				   int cp, int stat, int vnic)
{
	if (BNXT_PF(bp))
		return bnxt_hwrm_reserve_pf_rings(bp, tx, rx, grp, cp, stat,
						  vnic);
	else
		return bnxt_hwrm_reserve_vf_rings(bp, tx, rx, grp, cp, stat,
						  vnic);
}

int bnxt_nq_rings_in_use(struct bnxt *bp)
{
	int cp = bp->cp_nr_rings;
	int ulp_msix, ulp_base;

	ulp_msix = bnxt_get_ulp_msix_num(bp);
	if (ulp_msix) {
		ulp_base = bnxt_get_ulp_msix_base(bp);
		cp += ulp_msix;
		if ((ulp_base + ulp_msix) > cp)
			cp = ulp_base + ulp_msix;
	}
	return cp;
}

static int bnxt_cp_rings_in_use(struct bnxt *bp)
{
	int cp;

	if (!(bp->flags & BNXT_FLAG_CHIP_P5))
		return bnxt_nq_rings_in_use(bp);

	cp = bp->tx_nr_rings + bp->rx_nr_rings;
	return cp;
}

static int bnxt_get_func_stat_ctxs(struct bnxt *bp)
{
	int ulp_stat = bnxt_get_ulp_stat_ctxs(bp);
	int cp = bp->cp_nr_rings;

	if (!ulp_stat)
		return cp;

	if (bnxt_nq_rings_in_use(bp) > cp + bnxt_get_ulp_msix_num(bp))
		return bnxt_get_ulp_msix_base(bp) + ulp_stat;

	return cp + ulp_stat;
}

/* Check if a default RSS map needs to be setup.  This function is only
 * used on older firmware that does not require reserving RX rings.
 */
static void bnxt_check_rss_tbl_no_rmgr(struct bnxt *bp)
{
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;

	/* The RSS map is valid for RX rings set to resv_rx_rings */
	if (hw_resc->resv_rx_rings != bp->rx_nr_rings) {
		hw_resc->resv_rx_rings = bp->rx_nr_rings;
		if (!netif_is_rxfh_configured(bp->dev))
			bnxt_set_dflt_rss_indir_tbl(bp);
	}
}

static bool bnxt_need_reserve_rings(struct bnxt *bp)
{
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;
	int cp = bnxt_cp_rings_in_use(bp);
	int nq = bnxt_nq_rings_in_use(bp);
	int rx = bp->rx_nr_rings, stat;
	int vnic = 1, grp = rx;

	if (hw_resc->resv_tx_rings != bp->tx_nr_rings &&
	    bp->hwrm_spec_code >= 0x10601)
		return true;

	/* Old firmware does not need RX ring reservations but we still
	 * need to setup a default RSS map when needed.  With new firmware
	 * we go through RX ring reservations first and then set up the
	 * RSS map for the successfully reserved RX rings when needed.
	 */
	if (!BNXT_NEW_RM(bp)) {
		bnxt_check_rss_tbl_no_rmgr(bp);
		return false;
	}
	if ((bp->flags & BNXT_FLAG_RFS) && !(bp->flags & BNXT_FLAG_CHIP_P5))
		vnic = rx + 1;
	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		rx <<= 1;
	stat = bnxt_get_func_stat_ctxs(bp);
	if (hw_resc->resv_rx_rings != rx || hw_resc->resv_cp_rings != cp ||
	    hw_resc->resv_vnics != vnic || hw_resc->resv_stat_ctxs != stat ||
	    (hw_resc->resv_hw_ring_grps != grp &&
	     !(bp->flags & BNXT_FLAG_CHIP_P5)))
		return true;
	if ((bp->flags & BNXT_FLAG_CHIP_P5) && BNXT_PF(bp) &&
	    hw_resc->resv_irqs != nq)
		return true;
	return false;
}

static int __bnxt_reserve_rings(struct bnxt *bp)
{
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;
	int cp = bnxt_nq_rings_in_use(bp);
	int tx = bp->tx_nr_rings;
	int rx = bp->rx_nr_rings;
	int grp, rx_rings, rc;
	int vnic = 1, stat;
	bool sh = false;

	if (!bnxt_need_reserve_rings(bp))
		return 0;

	if (bp->flags & BNXT_FLAG_SHARED_RINGS)
		sh = true;
	if ((bp->flags & BNXT_FLAG_RFS) && !(bp->flags & BNXT_FLAG_CHIP_P5))
		vnic = rx + 1;
	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		rx <<= 1;
	grp = bp->rx_nr_rings;
	stat = bnxt_get_func_stat_ctxs(bp);

	rc = bnxt_hwrm_reserve_rings(bp, tx, rx, grp, cp, stat, vnic);
	if (rc)
		return rc;

	tx = hw_resc->resv_tx_rings;
	if (BNXT_NEW_RM(bp)) {
		rx = hw_resc->resv_rx_rings;
		cp = hw_resc->resv_irqs;
		grp = hw_resc->resv_hw_ring_grps;
		vnic = hw_resc->resv_vnics;
		stat = hw_resc->resv_stat_ctxs;
	}

	rx_rings = rx;
	if (bp->flags & BNXT_FLAG_AGG_RINGS) {
		if (rx >= 2) {
			rx_rings = rx >> 1;
		} else {
			if (netif_running(bp->dev))
				return -ENOMEM;

			bp->flags &= ~BNXT_FLAG_AGG_RINGS;
			bp->flags |= BNXT_FLAG_NO_AGG_RINGS;
			bp->dev->hw_features &= ~NETIF_F_LRO;
			bp->dev->features &= ~NETIF_F_LRO;
			bnxt_set_ring_params(bp);
		}
	}
	rx_rings = min_t(int, rx_rings, grp);
	cp = min_t(int, cp, bp->cp_nr_rings);
	if (stat > bnxt_get_ulp_stat_ctxs(bp))
		stat -= bnxt_get_ulp_stat_ctxs(bp);
	cp = min_t(int, cp, stat);
	rc = bnxt_trim_rings(bp, &rx_rings, &tx, cp, sh);
	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		rx = rx_rings << 1;
	cp = sh ? max_t(int, tx, rx_rings) : tx + rx_rings;
	bp->tx_nr_rings = tx;

	/* If we cannot reserve all the RX rings, reset the RSS map only
	 * if absolutely necessary
	 */
	if (rx_rings != bp->rx_nr_rings) {
		netdev_warn(bp->dev, "Able to reserve only %d out of %d requested RX rings\n",
			    rx_rings, bp->rx_nr_rings);
		if ((bp->dev->priv_flags & IFF_RXFH_CONFIGURED) &&
		    (bnxt_get_nr_rss_ctxs(bp, bp->rx_nr_rings) !=
		     bnxt_get_nr_rss_ctxs(bp, rx_rings) ||
		     bnxt_get_max_rss_ring(bp) >= rx_rings)) {
			netdev_warn(bp->dev, "RSS table entries reverting to default\n");
			bp->dev->priv_flags &= ~IFF_RXFH_CONFIGURED;
		}
	}
	bp->rx_nr_rings = rx_rings;
	bp->cp_nr_rings = cp;

	if (!tx || !rx || !cp || !grp || !vnic || !stat)
		return -ENOMEM;

	if (!netif_is_rxfh_configured(bp->dev))
		bnxt_set_dflt_rss_indir_tbl(bp);

	return rc;
}

static int bnxt_hwrm_check_vf_rings(struct bnxt *bp, int tx_rings, int rx_rings,
				    int ring_grps, int cp_rings, int stats,
				    int vnics)
{
	struct hwrm_func_vf_cfg_input req = {0};
	u32 flags;

	if (!BNXT_NEW_RM(bp))
		return 0;

	__bnxt_hwrm_reserve_vf_rings(bp, &req, tx_rings, rx_rings, ring_grps,
				     cp_rings, stats, vnics);
	flags = FUNC_VF_CFG_REQ_FLAGS_TX_ASSETS_TEST |
		FUNC_VF_CFG_REQ_FLAGS_RX_ASSETS_TEST |
		FUNC_VF_CFG_REQ_FLAGS_CMPL_ASSETS_TEST |
		FUNC_VF_CFG_REQ_FLAGS_STAT_CTX_ASSETS_TEST |
		FUNC_VF_CFG_REQ_FLAGS_VNIC_ASSETS_TEST |
		FUNC_VF_CFG_REQ_FLAGS_RSSCOS_CTX_ASSETS_TEST;
	if (!(bp->flags & BNXT_FLAG_CHIP_P5))
		flags |= FUNC_VF_CFG_REQ_FLAGS_RING_GRP_ASSETS_TEST;

	req.flags = cpu_to_le32(flags);
	return hwrm_send_message_silent(bp, &req, sizeof(req),
					HWRM_CMD_TIMEOUT);
}

static int bnxt_hwrm_check_pf_rings(struct bnxt *bp, int tx_rings, int rx_rings,
				    int ring_grps, int cp_rings, int stats,
				    int vnics)
{
	struct hwrm_func_cfg_input req = {0};
	u32 flags;

	__bnxt_hwrm_reserve_pf_rings(bp, &req, tx_rings, rx_rings, ring_grps,
				     cp_rings, stats, vnics);
	flags = FUNC_CFG_REQ_FLAGS_TX_ASSETS_TEST;
	if (BNXT_NEW_RM(bp)) {
		flags |= FUNC_CFG_REQ_FLAGS_RX_ASSETS_TEST |
			 FUNC_CFG_REQ_FLAGS_CMPL_ASSETS_TEST |
			 FUNC_CFG_REQ_FLAGS_STAT_CTX_ASSETS_TEST |
			 FUNC_CFG_REQ_FLAGS_VNIC_ASSETS_TEST;
		if (bp->flags & BNXT_FLAG_CHIP_P5)
			flags |= FUNC_CFG_REQ_FLAGS_RSSCOS_CTX_ASSETS_TEST |
				 FUNC_CFG_REQ_FLAGS_NQ_ASSETS_TEST;
		else
			flags |= FUNC_CFG_REQ_FLAGS_RING_GRP_ASSETS_TEST;
	}

	req.flags = cpu_to_le32(flags);
	return hwrm_send_message_silent(bp, &req, sizeof(req),
					HWRM_CMD_TIMEOUT);
}

static int bnxt_hwrm_check_rings(struct bnxt *bp, int tx_rings, int rx_rings,
				 int ring_grps, int cp_rings, int stats,
				 int vnics)
{
	if (bp->hwrm_spec_code < 0x10801)
		return 0;

	if (BNXT_PF(bp))
		return bnxt_hwrm_check_pf_rings(bp, tx_rings, rx_rings,
						ring_grps, cp_rings, stats,
						vnics);

	return bnxt_hwrm_check_vf_rings(bp, tx_rings, rx_rings, ring_grps,
					cp_rings, stats, vnics);
}

static void bnxt_hwrm_coal_params_qcaps(struct bnxt *bp)
{
	struct hwrm_ring_aggint_qcaps_output *resp = bp->hwrm_cmd_resp_addr;
	struct bnxt_coal_cap *coal_cap = &bp->coal_cap;
	struct hwrm_ring_aggint_qcaps_input req = {0};
	int rc;

	coal_cap->cmpl_params = BNXT_LEGACY_COAL_CMPL_PARAMS;
	coal_cap->num_cmpl_dma_aggr_max = 63;
	coal_cap->num_cmpl_dma_aggr_during_int_max = 63;
	coal_cap->cmpl_aggr_dma_tmr_max = 65535;
	coal_cap->cmpl_aggr_dma_tmr_during_int_max = 65535;
	coal_cap->int_lat_tmr_min_max = 65535;
	coal_cap->int_lat_tmr_max_max = 65535;
	coal_cap->num_cmpl_aggr_int_max = 65535;
	coal_cap->timer_units = 80;

	if (bp->hwrm_spec_code < 0x10902)
		return;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_RING_AGGINT_QCAPS, -1, -1);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message_silent(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		coal_cap->cmpl_params = le32_to_cpu(resp->cmpl_params);
		coal_cap->nq_params = le32_to_cpu(resp->nq_params);
		coal_cap->num_cmpl_dma_aggr_max =
			le16_to_cpu(resp->num_cmpl_dma_aggr_max);
		coal_cap->num_cmpl_dma_aggr_during_int_max =
			le16_to_cpu(resp->num_cmpl_dma_aggr_during_int_max);
		coal_cap->cmpl_aggr_dma_tmr_max =
			le16_to_cpu(resp->cmpl_aggr_dma_tmr_max);
		coal_cap->cmpl_aggr_dma_tmr_during_int_max =
			le16_to_cpu(resp->cmpl_aggr_dma_tmr_during_int_max);
		coal_cap->int_lat_tmr_min_max =
			le16_to_cpu(resp->int_lat_tmr_min_max);
		coal_cap->int_lat_tmr_max_max =
			le16_to_cpu(resp->int_lat_tmr_max_max);
		coal_cap->num_cmpl_aggr_int_max =
			le16_to_cpu(resp->num_cmpl_aggr_int_max);
		coal_cap->timer_units = le16_to_cpu(resp->timer_units);
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
}

static u16 bnxt_usec_to_coal_tmr(struct bnxt *bp, u16 usec)
{
	struct bnxt_coal_cap *coal_cap = &bp->coal_cap;

	return usec * 1000 / coal_cap->timer_units;
}

static void bnxt_hwrm_set_coal_params(struct bnxt *bp,
	struct bnxt_coal *hw_coal,
	struct hwrm_ring_cmpl_ring_cfg_aggint_params_input *req)
{
	struct bnxt_coal_cap *coal_cap = &bp->coal_cap;
	u32 cmpl_params = coal_cap->cmpl_params;
	u16 val, tmr, max, flags = 0;

	max = hw_coal->bufs_per_record * 128;
	if (hw_coal->budget)
		max = hw_coal->bufs_per_record * hw_coal->budget;
	max = min_t(u16, max, coal_cap->num_cmpl_aggr_int_max);

	val = clamp_t(u16, hw_coal->coal_bufs, 1, max);
	req->num_cmpl_aggr_int = cpu_to_le16(val);

	val = min_t(u16, val, coal_cap->num_cmpl_dma_aggr_max);
	req->num_cmpl_dma_aggr = cpu_to_le16(val);

	val = clamp_t(u16, hw_coal->coal_bufs_irq, 1,
		      coal_cap->num_cmpl_dma_aggr_during_int_max);
	req->num_cmpl_dma_aggr_during_int = cpu_to_le16(val);

	tmr = bnxt_usec_to_coal_tmr(bp, hw_coal->coal_ticks);
	tmr = clamp_t(u16, tmr, 1, coal_cap->int_lat_tmr_max_max);
	req->int_lat_tmr_max = cpu_to_le16(tmr);

	/* min timer set to 1/2 of interrupt timer */
	if (cmpl_params & RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_INT_LAT_TMR_MIN) {
		val = tmr / 2;
		val = clamp_t(u16, val, 1, coal_cap->int_lat_tmr_min_max);
		req->int_lat_tmr_min = cpu_to_le16(val);
		req->enables |= cpu_to_le16(BNXT_COAL_CMPL_MIN_TMR_ENABLE);
	}

	/* buf timer set to 1/4 of interrupt timer */
	val = clamp_t(u16, tmr / 4, 1, coal_cap->cmpl_aggr_dma_tmr_max);
	req->cmpl_aggr_dma_tmr = cpu_to_le16(val);

	if (cmpl_params &
	    RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_NUM_CMPL_DMA_AGGR_DURING_INT) {
		tmr = bnxt_usec_to_coal_tmr(bp, hw_coal->coal_ticks_irq);
		val = clamp_t(u16, tmr, 1,
			      coal_cap->cmpl_aggr_dma_tmr_during_int_max);
		req->cmpl_aggr_dma_tmr_during_int = cpu_to_le16(val);
		req->enables |=
			cpu_to_le16(BNXT_COAL_CMPL_AGGR_TMR_DURING_INT_ENABLE);
	}

	if (cmpl_params & RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_TIMER_RESET)
		flags |= RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_TIMER_RESET;
	if ((cmpl_params & RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_RING_IDLE) &&
	    hw_coal->idle_thresh && hw_coal->coal_ticks < hw_coal->idle_thresh)
		flags |= RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_RING_IDLE;
	req->flags = cpu_to_le16(flags);
	req->enables |= cpu_to_le16(BNXT_COAL_CMPL_ENABLES);
}

/* Caller holds bp->hwrm_cmd_lock */
static int __bnxt_hwrm_set_coal_nq(struct bnxt *bp, struct bnxt_napi *bnapi,
				   struct bnxt_coal *hw_coal)
{
	struct hwrm_ring_cmpl_ring_cfg_aggint_params_input req = {0};
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	struct bnxt_coal_cap *coal_cap = &bp->coal_cap;
	u32 nq_params = coal_cap->nq_params;
	u16 tmr;

	if (!(nq_params & RING_AGGINT_QCAPS_RESP_NQ_PARAMS_INT_LAT_TMR_MIN))
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_RING_CMPL_RING_CFG_AGGINT_PARAMS,
			       -1, -1);
	req.ring_id = cpu_to_le16(cpr->cp_ring_struct.fw_ring_id);
	req.flags =
		cpu_to_le16(RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_IS_NQ);

	tmr = bnxt_usec_to_coal_tmr(bp, hw_coal->coal_ticks) / 2;
	tmr = clamp_t(u16, tmr, 1, coal_cap->int_lat_tmr_min_max);
	req.int_lat_tmr_min = cpu_to_le16(tmr);
	req.enables |= cpu_to_le16(BNXT_COAL_CMPL_MIN_TMR_ENABLE);
	return _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

int bnxt_hwrm_set_ring_coal(struct bnxt *bp, struct bnxt_napi *bnapi)
{
	struct hwrm_ring_cmpl_ring_cfg_aggint_params_input req_rx = {0};
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	struct bnxt_coal coal;

	/* Tick values in micro seconds.
	 * 1 coal_buf x bufs_per_record = 1 completion record.
	 */
	memcpy(&coal, &bp->rx_coal, sizeof(struct bnxt_coal));

	coal.coal_ticks = cpr->rx_ring_coal.coal_ticks;
	coal.coal_bufs = cpr->rx_ring_coal.coal_bufs;

	if (!bnapi->rx_ring)
		return -ENODEV;

	bnxt_hwrm_cmd_hdr_init(bp, &req_rx,
			       HWRM_RING_CMPL_RING_CFG_AGGINT_PARAMS, -1, -1);

	bnxt_hwrm_set_coal_params(bp, &coal, &req_rx);

	req_rx.ring_id = cpu_to_le16(bnxt_cp_ring_for_rx(bp, bnapi->rx_ring));

	return hwrm_send_message(bp, &req_rx, sizeof(req_rx),
				 HWRM_CMD_TIMEOUT);
}

int bnxt_hwrm_set_coal(struct bnxt *bp)
{
	int i, rc = 0;
	struct hwrm_ring_cmpl_ring_cfg_aggint_params_input req_rx = {0},
							   req_tx = {0}, *req;

	bnxt_hwrm_cmd_hdr_init(bp, &req_rx,
			       HWRM_RING_CMPL_RING_CFG_AGGINT_PARAMS, -1, -1);
	bnxt_hwrm_cmd_hdr_init(bp, &req_tx,
			       HWRM_RING_CMPL_RING_CFG_AGGINT_PARAMS, -1, -1);

	bnxt_hwrm_set_coal_params(bp, &bp->rx_coal, &req_rx);
	bnxt_hwrm_set_coal_params(bp, &bp->tx_coal, &req_tx);

	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_coal *hw_coal;
		u16 ring_id;

		req = &req_rx;
		if (!bnapi->rx_ring) {
			ring_id = bnxt_cp_ring_for_tx(bp, bnapi->tx_ring);
			req = &req_tx;
		} else {
			ring_id = bnxt_cp_ring_for_rx(bp, bnapi->rx_ring);
		}
		req->ring_id = cpu_to_le16(ring_id);

		rc = _hwrm_send_message(bp, req, sizeof(*req),
					HWRM_CMD_TIMEOUT);
		if (rc)
			break;

		if (!(bp->flags & BNXT_FLAG_CHIP_P5))
			continue;

		if (bnapi->rx_ring && bnapi->tx_ring) {
			req = &req_tx;
			ring_id = bnxt_cp_ring_for_tx(bp, bnapi->tx_ring);
			req->ring_id = cpu_to_le16(ring_id);
			rc = _hwrm_send_message(bp, req, sizeof(*req),
						HWRM_CMD_TIMEOUT);
			if (rc)
				break;
		}
		if (bnapi->rx_ring)
			hw_coal = &bp->rx_coal;
		else
			hw_coal = &bp->tx_coal;
		__bnxt_hwrm_set_coal_nq(bp, bnapi, hw_coal);
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static void bnxt_hwrm_stat_ctx_free(struct bnxt *bp)
{
	struct hwrm_stat_ctx_clr_stats_input req0 = {0};
	struct hwrm_stat_ctx_free_input req = {0};
	int i;

	if (!bp->bnapi)
		return;

	if (BNXT_CHIP_TYPE_NITRO_A0(bp))
		return;

	bnxt_hwrm_cmd_hdr_init(bp, &req0, HWRM_STAT_CTX_CLR_STATS, -1, -1);
	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_STAT_CTX_FREE, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;

		if (cpr->hw_stats_ctx_id != INVALID_STATS_CTX_ID) {
			req.stat_ctx_id = cpu_to_le32(cpr->hw_stats_ctx_id);
			if (BNXT_FW_MAJ(bp) <= 20) {
				req0.stat_ctx_id = req.stat_ctx_id;
				_hwrm_send_message(bp, &req0, sizeof(req0),
						   HWRM_CMD_TIMEOUT);
			}
			_hwrm_send_message(bp, &req, sizeof(req),
					   HWRM_CMD_TIMEOUT);

			cpr->hw_stats_ctx_id = INVALID_STATS_CTX_ID;
		}
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
}

static int bnxt_hwrm_stat_ctx_alloc(struct bnxt *bp)
{
	int rc = 0, i;
	struct hwrm_stat_ctx_alloc_input req = {0};
	struct hwrm_stat_ctx_alloc_output *resp = bp->hwrm_cmd_resp_addr;

	if (BNXT_CHIP_TYPE_NITRO_A0(bp))
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_STAT_CTX_ALLOC, -1, -1);

	req.stats_dma_length = cpu_to_le16(bp->hw_ring_stats_size);
	req.update_period_ms = cpu_to_le32(bp->stats_coal_ticks / 1000);

	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;

		req.stats_dma_addr = cpu_to_le64(cpr->stats.hw_stats_map);

		rc = _hwrm_send_message(bp, &req, sizeof(req),
					HWRM_CMD_TIMEOUT);
		if (rc)
			break;

		cpr->hw_stats_ctx_id = le32_to_cpu(resp->stat_ctx_id);

		bp->grp_info[i].fw_stats_ctx = cpr->hw_stats_ctx_id;
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_func_qcfg(struct bnxt *bp)
{
	struct hwrm_func_qcfg_input req = {0};
	struct hwrm_func_qcfg_output *resp = bp->hwrm_cmd_resp_addr;
	u32 min_db_offset = 0;
	u16 flags;
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_QCFG, -1, -1);
	req.fid = cpu_to_le16(0xffff);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		goto func_qcfg_exit;

#ifdef CONFIG_BNXT_SRIOV
	if (BNXT_VF(bp)) {
		struct bnxt_vf_info *vf = &bp->vf;

		vf->vlan = le16_to_cpu(resp->vlan) & VLAN_VID_MASK;
	} else {
		bp->pf.registered_vfs = le16_to_cpu(resp->registered_vfs);
	}
#endif
	flags = le16_to_cpu(resp->flags);
	if (flags & (FUNC_QCFG_RESP_FLAGS_FW_DCBX_AGENT_ENABLED |
		     FUNC_QCFG_RESP_FLAGS_FW_LLDP_AGENT_ENABLED)) {
		bp->fw_cap |= BNXT_FW_CAP_LLDP_AGENT;
		if (flags & FUNC_QCFG_RESP_FLAGS_FW_DCBX_AGENT_ENABLED)
			bp->fw_cap |= BNXT_FW_CAP_DCBX_AGENT;
	}
	if (BNXT_PF(bp) && (flags & FUNC_QCFG_RESP_FLAGS_MULTI_HOST))
		bp->flags |= BNXT_FLAG_MULTI_HOST;
	if (flags & FUNC_QCFG_RESP_FLAGS_RING_MONITOR_ENABLED)
		bp->fw_cap |= BNXT_FW_CAP_RING_MONITOR;

	switch (resp->port_partition_type) {
	case FUNC_QCFG_RESP_PORT_PARTITION_TYPE_NPAR1_0:
	case FUNC_QCFG_RESP_PORT_PARTITION_TYPE_NPAR1_5:
	case FUNC_QCFG_RESP_PORT_PARTITION_TYPE_NPAR2_0:
		bp->port_partition_type = resp->port_partition_type;
		break;
	}
	if (bp->hwrm_spec_code < 0x10707 ||
	    resp->evb_mode == FUNC_QCFG_RESP_EVB_MODE_VEB)
		bp->br_mode = BRIDGE_MODE_VEB;
	else if (resp->evb_mode == FUNC_QCFG_RESP_EVB_MODE_VEPA)
		bp->br_mode = BRIDGE_MODE_VEPA;
	else
		bp->br_mode = BRIDGE_MODE_UNDEF;

	bp->max_mtu = le16_to_cpu(resp->max_mtu_configured);
	if (!bp->max_mtu)
		bp->max_mtu = BNXT_MAX_MTU;

	if (bp->db_size)
		goto func_qcfg_exit;

	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		if (BNXT_PF(bp))
			min_db_offset = DB_PF_OFFSET_P5;
		else
			min_db_offset = DB_VF_OFFSET_P5;
	}
	bp->db_size = PAGE_ALIGN(le16_to_cpu(resp->l2_doorbell_bar_size_kb) *
				 1024);
	if (!bp->db_size || bp->db_size > pci_resource_len(bp->pdev, 2) ||
	    bp->db_size <= min_db_offset)
		bp->db_size = pci_resource_len(bp->pdev, 2);

func_qcfg_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_func_backing_store_qcaps(struct bnxt *bp)
{
	struct hwrm_func_backing_store_qcaps_input req = {0};
	struct hwrm_func_backing_store_qcaps_output *resp =
		bp->hwrm_cmd_resp_addr;
	int rc;

	if (bp->hwrm_spec_code < 0x10902 || BNXT_VF(bp) || bp->ctx)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_BACKING_STORE_QCAPS, -1, -1);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message_silent(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		struct bnxt_ctx_pg_info *ctx_pg;
		struct bnxt_ctx_mem_info *ctx;
		int i, tqm_rings;

		ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
		if (!ctx) {
			rc = -ENOMEM;
			goto ctx_err;
		}
		ctx->qp_max_entries = le32_to_cpu(resp->qp_max_entries);
		ctx->qp_min_qp1_entries = le16_to_cpu(resp->qp_min_qp1_entries);
		ctx->qp_max_l2_entries = le16_to_cpu(resp->qp_max_l2_entries);
		ctx->qp_entry_size = le16_to_cpu(resp->qp_entry_size);
		ctx->srq_max_l2_entries = le16_to_cpu(resp->srq_max_l2_entries);
		ctx->srq_max_entries = le32_to_cpu(resp->srq_max_entries);
		ctx->srq_entry_size = le16_to_cpu(resp->srq_entry_size);
		ctx->cq_max_l2_entries = le16_to_cpu(resp->cq_max_l2_entries);
		ctx->cq_max_entries = le32_to_cpu(resp->cq_max_entries);
		ctx->cq_entry_size = le16_to_cpu(resp->cq_entry_size);
		ctx->vnic_max_vnic_entries =
			le16_to_cpu(resp->vnic_max_vnic_entries);
		ctx->vnic_max_ring_table_entries =
			le16_to_cpu(resp->vnic_max_ring_table_entries);
		ctx->vnic_entry_size = le16_to_cpu(resp->vnic_entry_size);
		ctx->stat_max_entries = le32_to_cpu(resp->stat_max_entries);
		ctx->stat_entry_size = le16_to_cpu(resp->stat_entry_size);
		ctx->tqm_entry_size = le16_to_cpu(resp->tqm_entry_size);
		ctx->tqm_min_entries_per_ring =
			le32_to_cpu(resp->tqm_min_entries_per_ring);
		ctx->tqm_max_entries_per_ring =
			le32_to_cpu(resp->tqm_max_entries_per_ring);
		ctx->tqm_entries_multiple = resp->tqm_entries_multiple;
		if (!ctx->tqm_entries_multiple)
			ctx->tqm_entries_multiple = 1;
		ctx->mrav_max_entries = le32_to_cpu(resp->mrav_max_entries);
		ctx->mrav_entry_size = le16_to_cpu(resp->mrav_entry_size);
		ctx->mrav_num_entries_units =
			le16_to_cpu(resp->mrav_num_entries_units);
		ctx->tim_entry_size = le16_to_cpu(resp->tim_entry_size);
		ctx->tim_max_entries = le32_to_cpu(resp->tim_max_entries);
		ctx->ctx_kind_initializer = resp->ctx_kind_initializer;
		ctx->tqm_fp_rings_count = resp->tqm_fp_rings_count;
		if (!ctx->tqm_fp_rings_count)
			ctx->tqm_fp_rings_count = bp->max_q;

		tqm_rings = ctx->tqm_fp_rings_count + 1;
		ctx_pg = kcalloc(tqm_rings, sizeof(*ctx_pg), GFP_KERNEL);
		if (!ctx_pg) {
			kfree(ctx);
			rc = -ENOMEM;
			goto ctx_err;
		}
		for (i = 0; i < tqm_rings; i++, ctx_pg++)
			ctx->tqm_mem[i] = ctx_pg;
		bp->ctx = ctx;
	} else {
		rc = 0;
	}
ctx_err:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static void bnxt_hwrm_set_pg_attr(struct bnxt_ring_mem_info *rmem, u8 *pg_attr,
				  __le64 *pg_dir)
{
	u8 pg_size = 0;

	if (BNXT_PAGE_SHIFT == 13)
		pg_size = 1 << 4;
	else if (BNXT_PAGE_SIZE == 16)
		pg_size = 2 << 4;

	*pg_attr = pg_size;
	if (rmem->depth >= 1) {
		if (rmem->depth == 2)
			*pg_attr |= 2;
		else
			*pg_attr |= 1;
		*pg_dir = cpu_to_le64(rmem->pg_tbl_map);
	} else {
		*pg_dir = cpu_to_le64(rmem->dma_arr[0]);
	}
}

#define FUNC_BACKING_STORE_CFG_REQ_DFLT_ENABLES			\
	(FUNC_BACKING_STORE_CFG_REQ_ENABLES_QP |		\
	 FUNC_BACKING_STORE_CFG_REQ_ENABLES_SRQ |		\
	 FUNC_BACKING_STORE_CFG_REQ_ENABLES_CQ |		\
	 FUNC_BACKING_STORE_CFG_REQ_ENABLES_VNIC |		\
	 FUNC_BACKING_STORE_CFG_REQ_ENABLES_STAT)

static int bnxt_hwrm_func_backing_store_cfg(struct bnxt *bp, u32 enables)
{
	struct hwrm_func_backing_store_cfg_input req = {0};
	struct bnxt_ctx_mem_info *ctx = bp->ctx;
	struct bnxt_ctx_pg_info *ctx_pg;
	__le32 *num_entries;
	__le64 *pg_dir;
	u32 flags = 0;
	u8 *pg_attr;
	u32 ena;
	int i;

	if (!ctx)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_BACKING_STORE_CFG, -1, -1);
	req.enables = cpu_to_le32(enables);

	if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_QP) {
		ctx_pg = &ctx->qp_mem;
		req.qp_num_entries = cpu_to_le32(ctx_pg->entries);
		req.qp_num_qp1_entries = cpu_to_le16(ctx->qp_min_qp1_entries);
		req.qp_num_l2_entries = cpu_to_le16(ctx->qp_max_l2_entries);
		req.qp_entry_size = cpu_to_le16(ctx->qp_entry_size);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req.qpc_pg_size_qpc_lvl,
				      &req.qpc_page_dir);
	}
	if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_SRQ) {
		ctx_pg = &ctx->srq_mem;
		req.srq_num_entries = cpu_to_le32(ctx_pg->entries);
		req.srq_num_l2_entries = cpu_to_le16(ctx->srq_max_l2_entries);
		req.srq_entry_size = cpu_to_le16(ctx->srq_entry_size);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req.srq_pg_size_srq_lvl,
				      &req.srq_page_dir);
	}
	if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_CQ) {
		ctx_pg = &ctx->cq_mem;
		req.cq_num_entries = cpu_to_le32(ctx_pg->entries);
		req.cq_num_l2_entries = cpu_to_le16(ctx->cq_max_l2_entries);
		req.cq_entry_size = cpu_to_le16(ctx->cq_entry_size);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem, &req.cq_pg_size_cq_lvl,
				      &req.cq_page_dir);
	}
	if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_VNIC) {
		ctx_pg = &ctx->vnic_mem;
		req.vnic_num_vnic_entries =
			cpu_to_le16(ctx->vnic_max_vnic_entries);
		req.vnic_num_ring_table_entries =
			cpu_to_le16(ctx->vnic_max_ring_table_entries);
		req.vnic_entry_size = cpu_to_le16(ctx->vnic_entry_size);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req.vnic_pg_size_vnic_lvl,
				      &req.vnic_page_dir);
	}
	if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_STAT) {
		ctx_pg = &ctx->stat_mem;
		req.stat_num_entries = cpu_to_le32(ctx->stat_max_entries);
		req.stat_entry_size = cpu_to_le16(ctx->stat_entry_size);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req.stat_pg_size_stat_lvl,
				      &req.stat_page_dir);
	}
	if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_MRAV) {
		ctx_pg = &ctx->mrav_mem;
		req.mrav_num_entries = cpu_to_le32(ctx_pg->entries);
		if (ctx->mrav_num_entries_units)
			flags |=
			FUNC_BACKING_STORE_CFG_REQ_FLAGS_MRAV_RESERVATION_SPLIT;
		req.mrav_entry_size = cpu_to_le16(ctx->mrav_entry_size);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req.mrav_pg_size_mrav_lvl,
				      &req.mrav_page_dir);
	}
	if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_TIM) {
		ctx_pg = &ctx->tim_mem;
		req.tim_num_entries = cpu_to_le32(ctx_pg->entries);
		req.tim_entry_size = cpu_to_le16(ctx->tim_entry_size);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req.tim_pg_size_tim_lvl,
				      &req.tim_page_dir);
	}
	for (i = 0, num_entries = &req.tqm_sp_num_entries,
	     pg_attr = &req.tqm_sp_pg_size_tqm_sp_lvl,
	     pg_dir = &req.tqm_sp_page_dir,
	     ena = FUNC_BACKING_STORE_CFG_REQ_ENABLES_TQM_SP;
	     i < 9; i++, num_entries++, pg_attr++, pg_dir++, ena <<= 1) {
		if (!(enables & ena))
			continue;

		req.tqm_entry_size = cpu_to_le16(ctx->tqm_entry_size);
		ctx_pg = ctx->tqm_mem[i];
		*num_entries = cpu_to_le32(ctx_pg->entries);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem, pg_attr, pg_dir);
	}
	req.flags = cpu_to_le32(flags);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_alloc_ctx_mem_blk(struct bnxt *bp,
				  struct bnxt_ctx_pg_info *ctx_pg)
{
	struct bnxt_ring_mem_info *rmem = &ctx_pg->ring_mem;

	rmem->page_size = BNXT_PAGE_SIZE;
	rmem->pg_arr = ctx_pg->ctx_pg_arr;
	rmem->dma_arr = ctx_pg->ctx_dma_arr;
	rmem->flags = BNXT_RMEM_VALID_PTE_FLAG;
	if (rmem->depth >= 1)
		rmem->flags |= BNXT_RMEM_USE_FULL_PAGE_FLAG;
	return bnxt_alloc_ring(bp, rmem);
}

static int bnxt_alloc_ctx_pg_tbls(struct bnxt *bp,
				  struct bnxt_ctx_pg_info *ctx_pg, u32 mem_size,
				  u8 depth, bool use_init_val)
{
	struct bnxt_ring_mem_info *rmem = &ctx_pg->ring_mem;
	int rc;

	if (!mem_size)
		return -EINVAL;

	ctx_pg->nr_pages = DIV_ROUND_UP(mem_size, BNXT_PAGE_SIZE);
	if (ctx_pg->nr_pages > MAX_CTX_TOTAL_PAGES) {
		ctx_pg->nr_pages = 0;
		return -EINVAL;
	}
	if (ctx_pg->nr_pages > MAX_CTX_PAGES || depth > 1) {
		int nr_tbls, i;

		rmem->depth = 2;
		ctx_pg->ctx_pg_tbl = kcalloc(MAX_CTX_PAGES, sizeof(ctx_pg),
					     GFP_KERNEL);
		if (!ctx_pg->ctx_pg_tbl)
			return -ENOMEM;
		nr_tbls = DIV_ROUND_UP(ctx_pg->nr_pages, MAX_CTX_PAGES);
		rmem->nr_pages = nr_tbls;
		rc = bnxt_alloc_ctx_mem_blk(bp, ctx_pg);
		if (rc)
			return rc;
		for (i = 0; i < nr_tbls; i++) {
			struct bnxt_ctx_pg_info *pg_tbl;

			pg_tbl = kzalloc(sizeof(*pg_tbl), GFP_KERNEL);
			if (!pg_tbl)
				return -ENOMEM;
			ctx_pg->ctx_pg_tbl[i] = pg_tbl;
			rmem = &pg_tbl->ring_mem;
			rmem->pg_tbl = ctx_pg->ctx_pg_arr[i];
			rmem->pg_tbl_map = ctx_pg->ctx_dma_arr[i];
			rmem->depth = 1;
			rmem->nr_pages = MAX_CTX_PAGES;
			if (use_init_val)
				rmem->init_val = bp->ctx->ctx_kind_initializer;
			if (i == (nr_tbls - 1)) {
				int rem = ctx_pg->nr_pages % MAX_CTX_PAGES;

				if (rem)
					rmem->nr_pages = rem;
			}
			rc = bnxt_alloc_ctx_mem_blk(bp, pg_tbl);
			if (rc)
				break;
		}
	} else {
		rmem->nr_pages = DIV_ROUND_UP(mem_size, BNXT_PAGE_SIZE);
		if (rmem->nr_pages > 1 || depth)
			rmem->depth = 1;
		if (use_init_val)
			rmem->init_val = bp->ctx->ctx_kind_initializer;
		rc = bnxt_alloc_ctx_mem_blk(bp, ctx_pg);
	}
	return rc;
}

static void bnxt_free_ctx_pg_tbls(struct bnxt *bp,
				  struct bnxt_ctx_pg_info *ctx_pg)
{
	struct bnxt_ring_mem_info *rmem = &ctx_pg->ring_mem;

	if (rmem->depth > 1 || ctx_pg->nr_pages > MAX_CTX_PAGES ||
	    ctx_pg->ctx_pg_tbl) {
		int i, nr_tbls = rmem->nr_pages;

		for (i = 0; i < nr_tbls; i++) {
			struct bnxt_ctx_pg_info *pg_tbl;
			struct bnxt_ring_mem_info *rmem2;

			pg_tbl = ctx_pg->ctx_pg_tbl[i];
			if (!pg_tbl)
				continue;
			rmem2 = &pg_tbl->ring_mem;
			bnxt_free_ring(bp, rmem2);
			ctx_pg->ctx_pg_arr[i] = NULL;
			kfree(pg_tbl);
			ctx_pg->ctx_pg_tbl[i] = NULL;
		}
		kfree(ctx_pg->ctx_pg_tbl);
		ctx_pg->ctx_pg_tbl = NULL;
	}
	bnxt_free_ring(bp, rmem);
	ctx_pg->nr_pages = 0;
}

static void bnxt_free_ctx_mem(struct bnxt *bp)
{
	struct bnxt_ctx_mem_info *ctx = bp->ctx;
	int i;

	if (!ctx)
		return;

	if (ctx->tqm_mem[0]) {
		for (i = 0; i < ctx->tqm_fp_rings_count + 1; i++)
			bnxt_free_ctx_pg_tbls(bp, ctx->tqm_mem[i]);
		kfree(ctx->tqm_mem[0]);
		ctx->tqm_mem[0] = NULL;
	}

	bnxt_free_ctx_pg_tbls(bp, &ctx->tim_mem);
	bnxt_free_ctx_pg_tbls(bp, &ctx->mrav_mem);
	bnxt_free_ctx_pg_tbls(bp, &ctx->stat_mem);
	bnxt_free_ctx_pg_tbls(bp, &ctx->vnic_mem);
	bnxt_free_ctx_pg_tbls(bp, &ctx->cq_mem);
	bnxt_free_ctx_pg_tbls(bp, &ctx->srq_mem);
	bnxt_free_ctx_pg_tbls(bp, &ctx->qp_mem);
	ctx->flags &= ~BNXT_CTX_FLAG_INITED;
}

static int bnxt_alloc_ctx_mem(struct bnxt *bp)
{
	struct bnxt_ctx_pg_info *ctx_pg;
	struct bnxt_ctx_mem_info *ctx;
	u32 mem_size, ena, entries;
	u32 entries_sp, min;
	u32 num_mr, num_ah;
	u32 extra_srqs = 0;
	u32 extra_qps = 0;
	u8 pg_lvl = 1;
	int i, rc;

	rc = bnxt_hwrm_func_backing_store_qcaps(bp);
	if (rc) {
		netdev_err(bp->dev, "Failed querying context mem capability, rc = %d.\n",
			   rc);
		return rc;
	}
	ctx = bp->ctx;
	if (!ctx || (ctx->flags & BNXT_CTX_FLAG_INITED))
		return 0;

	if ((bp->flags & BNXT_FLAG_ROCE_CAP) && !is_kdump_kernel()) {
		pg_lvl = 2;
		extra_qps = 65536;
		extra_srqs = 8192;
	}

	ctx_pg = &ctx->qp_mem;
	ctx_pg->entries = ctx->qp_min_qp1_entries + ctx->qp_max_l2_entries +
			  extra_qps;
	mem_size = ctx->qp_entry_size * ctx_pg->entries;
	rc = bnxt_alloc_ctx_pg_tbls(bp, ctx_pg, mem_size, pg_lvl, true);
	if (rc)
		return rc;

	ctx_pg = &ctx->srq_mem;
	ctx_pg->entries = ctx->srq_max_l2_entries + extra_srqs;
	mem_size = ctx->srq_entry_size * ctx_pg->entries;
	rc = bnxt_alloc_ctx_pg_tbls(bp, ctx_pg, mem_size, pg_lvl, true);
	if (rc)
		return rc;

	ctx_pg = &ctx->cq_mem;
	ctx_pg->entries = ctx->cq_max_l2_entries + extra_qps * 2;
	mem_size = ctx->cq_entry_size * ctx_pg->entries;
	rc = bnxt_alloc_ctx_pg_tbls(bp, ctx_pg, mem_size, pg_lvl, true);
	if (rc)
		return rc;

	ctx_pg = &ctx->vnic_mem;
	ctx_pg->entries = ctx->vnic_max_vnic_entries +
			  ctx->vnic_max_ring_table_entries;
	mem_size = ctx->vnic_entry_size * ctx_pg->entries;
	rc = bnxt_alloc_ctx_pg_tbls(bp, ctx_pg, mem_size, 1, true);
	if (rc)
		return rc;

	ctx_pg = &ctx->stat_mem;
	ctx_pg->entries = ctx->stat_max_entries;
	mem_size = ctx->stat_entry_size * ctx_pg->entries;
	rc = bnxt_alloc_ctx_pg_tbls(bp, ctx_pg, mem_size, 1, true);
	if (rc)
		return rc;

	ena = 0;
	if (!(bp->flags & BNXT_FLAG_ROCE_CAP))
		goto skip_rdma;

	ctx_pg = &ctx->mrav_mem;
	/* 128K extra is needed to accommodate static AH context
	 * allocation by f/w.
	 */
	num_mr = 1024 * 256;
	num_ah = 1024 * 128;
	ctx_pg->entries = num_mr + num_ah;
	mem_size = ctx->mrav_entry_size * ctx_pg->entries;
	rc = bnxt_alloc_ctx_pg_tbls(bp, ctx_pg, mem_size, 2, true);
	if (rc)
		return rc;
	ena = FUNC_BACKING_STORE_CFG_REQ_ENABLES_MRAV;
	if (ctx->mrav_num_entries_units)
		ctx_pg->entries =
			((num_mr / ctx->mrav_num_entries_units) << 16) |
			 (num_ah / ctx->mrav_num_entries_units);

	ctx_pg = &ctx->tim_mem;
	ctx_pg->entries = ctx->qp_mem.entries;
	mem_size = ctx->tim_entry_size * ctx_pg->entries;
	rc = bnxt_alloc_ctx_pg_tbls(bp, ctx_pg, mem_size, 1, false);
	if (rc)
		return rc;
	ena |= FUNC_BACKING_STORE_CFG_REQ_ENABLES_TIM;

skip_rdma:
	min = ctx->tqm_min_entries_per_ring;
	entries_sp = ctx->vnic_max_vnic_entries + ctx->qp_max_l2_entries +
		     2 * (extra_qps + ctx->qp_min_qp1_entries) + min;
	entries_sp = roundup(entries_sp, ctx->tqm_entries_multiple);
	entries = ctx->qp_max_l2_entries + extra_qps + ctx->qp_min_qp1_entries;
	entries = roundup(entries, ctx->tqm_entries_multiple);
	entries = clamp_t(u32, entries, min, ctx->tqm_max_entries_per_ring);
	for (i = 0; i < ctx->tqm_fp_rings_count + 1; i++) {
		ctx_pg = ctx->tqm_mem[i];
		ctx_pg->entries = i ? entries : entries_sp;
		mem_size = ctx->tqm_entry_size * ctx_pg->entries;
		rc = bnxt_alloc_ctx_pg_tbls(bp, ctx_pg, mem_size, 1, false);
		if (rc)
			return rc;
		ena |= FUNC_BACKING_STORE_CFG_REQ_ENABLES_TQM_SP << i;
	}
	ena |= FUNC_BACKING_STORE_CFG_REQ_DFLT_ENABLES;
	rc = bnxt_hwrm_func_backing_store_cfg(bp, ena);
	if (rc) {
		netdev_err(bp->dev, "Failed configuring context mem, rc = %d.\n",
			   rc);
		return rc;
	}
	ctx->flags |= BNXT_CTX_FLAG_INITED;
	return 0;
}

int bnxt_hwrm_func_resc_qcaps(struct bnxt *bp, bool all)
{
	struct hwrm_func_resource_qcaps_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_func_resource_qcaps_input req = {0};
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_RESOURCE_QCAPS, -1, -1);
	req.fid = cpu_to_le16(0xffff);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message_silent(bp, &req, sizeof(req),
				       HWRM_CMD_TIMEOUT);
	if (rc)
		goto hwrm_func_resc_qcaps_exit;

	hw_resc->max_tx_sch_inputs = le16_to_cpu(resp->max_tx_scheduler_inputs);
	if (!all)
		goto hwrm_func_resc_qcaps_exit;

	hw_resc->min_rsscos_ctxs = le16_to_cpu(resp->min_rsscos_ctx);
	hw_resc->max_rsscos_ctxs = le16_to_cpu(resp->max_rsscos_ctx);
	hw_resc->min_cp_rings = le16_to_cpu(resp->min_cmpl_rings);
	hw_resc->max_cp_rings = le16_to_cpu(resp->max_cmpl_rings);
	hw_resc->min_tx_rings = le16_to_cpu(resp->min_tx_rings);
	hw_resc->max_tx_rings = le16_to_cpu(resp->max_tx_rings);
	hw_resc->min_rx_rings = le16_to_cpu(resp->min_rx_rings);
	hw_resc->max_rx_rings = le16_to_cpu(resp->max_rx_rings);
	hw_resc->min_hw_ring_grps = le16_to_cpu(resp->min_hw_ring_grps);
	hw_resc->max_hw_ring_grps = le16_to_cpu(resp->max_hw_ring_grps);
	hw_resc->min_l2_ctxs = le16_to_cpu(resp->min_l2_ctxs);
	hw_resc->max_l2_ctxs = le16_to_cpu(resp->max_l2_ctxs);
	hw_resc->min_vnics = le16_to_cpu(resp->min_vnics);
	hw_resc->max_vnics = le16_to_cpu(resp->max_vnics);
	hw_resc->min_stat_ctxs = le16_to_cpu(resp->min_stat_ctx);
	hw_resc->max_stat_ctxs = le16_to_cpu(resp->max_stat_ctx);

	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		u16 max_msix = le16_to_cpu(resp->max_msix);

		hw_resc->max_nqs = max_msix;
		hw_resc->max_hw_ring_grps = hw_resc->max_rx_rings;
	}

	if (BNXT_PF(bp)) {
		struct bnxt_pf_info *pf = &bp->pf;

		pf->vf_resv_strategy =
			le16_to_cpu(resp->vf_reservation_strategy);
		if (pf->vf_resv_strategy > BNXT_VF_RESV_STRATEGY_MINIMAL_STATIC)
			pf->vf_resv_strategy = BNXT_VF_RESV_STRATEGY_MAXIMAL;
	}
hwrm_func_resc_qcaps_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int __bnxt_hwrm_func_qcaps(struct bnxt *bp)
{
	int rc = 0;
	struct hwrm_func_qcaps_input req = {0};
	struct hwrm_func_qcaps_output *resp = bp->hwrm_cmd_resp_addr;
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;
	u32 flags, flags_ext;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_QCAPS, -1, -1);
	req.fid = cpu_to_le16(0xffff);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		goto hwrm_func_qcaps_exit;

	flags = le32_to_cpu(resp->flags);
	if (flags & FUNC_QCAPS_RESP_FLAGS_ROCE_V1_SUPPORTED)
		bp->flags |= BNXT_FLAG_ROCEV1_CAP;
	if (flags & FUNC_QCAPS_RESP_FLAGS_ROCE_V2_SUPPORTED)
		bp->flags |= BNXT_FLAG_ROCEV2_CAP;
	if (flags & FUNC_QCAPS_RESP_FLAGS_PCIE_STATS_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_PCIE_STATS_SUPPORTED;
	if (flags & FUNC_QCAPS_RESP_FLAGS_HOT_RESET_CAPABLE)
		bp->fw_cap |= BNXT_FW_CAP_HOT_RESET;
	if (flags & FUNC_QCAPS_RESP_FLAGS_EXT_STATS_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_EXT_STATS_SUPPORTED;
	if (flags &  FUNC_QCAPS_RESP_FLAGS_ERROR_RECOVERY_CAPABLE)
		bp->fw_cap |= BNXT_FW_CAP_ERROR_RECOVERY;
	if (flags & FUNC_QCAPS_RESP_FLAGS_ERR_RECOVER_RELOAD)
		bp->fw_cap |= BNXT_FW_CAP_ERR_RECOVER_RELOAD;
	if (!(flags & FUNC_QCAPS_RESP_FLAGS_VLAN_ACCELERATION_TX_DISABLED))
		bp->fw_cap |= BNXT_FW_CAP_VLAN_TX_INSERT;

	flags_ext = le32_to_cpu(resp->flags_ext);
	if (flags_ext & FUNC_QCAPS_RESP_FLAGS_EXT_EXT_HW_STATS_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_EXT_HW_STATS_SUPPORTED;

	bp->tx_push_thresh = 0;
	if ((flags & FUNC_QCAPS_RESP_FLAGS_PUSH_MODE_SUPPORTED) &&
	    BNXT_FW_MAJ(bp) > 217)
		bp->tx_push_thresh = BNXT_TX_PUSH_THRESH;

	hw_resc->max_rsscos_ctxs = le16_to_cpu(resp->max_rsscos_ctx);
	hw_resc->max_cp_rings = le16_to_cpu(resp->max_cmpl_rings);
	hw_resc->max_tx_rings = le16_to_cpu(resp->max_tx_rings);
	hw_resc->max_rx_rings = le16_to_cpu(resp->max_rx_rings);
	hw_resc->max_hw_ring_grps = le32_to_cpu(resp->max_hw_ring_grps);
	if (!hw_resc->max_hw_ring_grps)
		hw_resc->max_hw_ring_grps = hw_resc->max_tx_rings;
	hw_resc->max_l2_ctxs = le16_to_cpu(resp->max_l2_ctxs);
	hw_resc->max_vnics = le16_to_cpu(resp->max_vnics);
	hw_resc->max_stat_ctxs = le16_to_cpu(resp->max_stat_ctx);

	if (BNXT_PF(bp)) {
		struct bnxt_pf_info *pf = &bp->pf;

		pf->fw_fid = le16_to_cpu(resp->fid);
		pf->port_id = le16_to_cpu(resp->port_id);
		memcpy(pf->mac_addr, resp->mac_address, ETH_ALEN);
		pf->first_vf_id = le16_to_cpu(resp->first_vf_id);
		pf->max_vfs = le16_to_cpu(resp->max_vfs);
		pf->max_encap_records = le32_to_cpu(resp->max_encap_records);
		pf->max_decap_records = le32_to_cpu(resp->max_decap_records);
		pf->max_tx_em_flows = le32_to_cpu(resp->max_tx_em_flows);
		pf->max_tx_wm_flows = le32_to_cpu(resp->max_tx_wm_flows);
		pf->max_rx_em_flows = le32_to_cpu(resp->max_rx_em_flows);
		pf->max_rx_wm_flows = le32_to_cpu(resp->max_rx_wm_flows);
		bp->flags &= ~BNXT_FLAG_WOL_CAP;
		if (flags & FUNC_QCAPS_RESP_FLAGS_WOL_MAGICPKT_SUPPORTED)
			bp->flags |= BNXT_FLAG_WOL_CAP;
	} else {
#ifdef CONFIG_BNXT_SRIOV
		struct bnxt_vf_info *vf = &bp->vf;

		vf->fw_fid = le16_to_cpu(resp->fid);
		memcpy(vf->mac_addr, resp->mac_address, ETH_ALEN);
#endif
	}

hwrm_func_qcaps_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_queue_qportcfg(struct bnxt *bp);

static int bnxt_hwrm_func_qcaps(struct bnxt *bp)
{
	int rc;

	rc = __bnxt_hwrm_func_qcaps(bp);
	if (rc)
		return rc;
	rc = bnxt_hwrm_queue_qportcfg(bp);
	if (rc) {
		netdev_err(bp->dev, "hwrm query qportcfg failure rc: %d\n", rc);
		return rc;
	}
	if (bp->hwrm_spec_code >= 0x10803) {
		rc = bnxt_alloc_ctx_mem(bp);
		if (rc)
			return rc;
		rc = bnxt_hwrm_func_resc_qcaps(bp, true);
		if (!rc)
			bp->fw_cap |= BNXT_FW_CAP_NEW_RM;
	}
	return 0;
}

static int bnxt_hwrm_cfa_adv_flow_mgnt_qcaps(struct bnxt *bp)
{
	struct hwrm_cfa_adv_flow_mgnt_qcaps_input req = {0};
	struct hwrm_cfa_adv_flow_mgnt_qcaps_output *resp;
	int rc = 0;
	u32 flags;

	if (!(bp->fw_cap & BNXT_FW_CAP_CFA_ADV_FLOW))
		return 0;

	resp = bp->hwrm_cmd_resp_addr;
	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_ADV_FLOW_MGNT_QCAPS, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		goto hwrm_cfa_adv_qcaps_exit;

	flags = le32_to_cpu(resp->flags);
	if (flags &
	    CFA_ADV_FLOW_MGNT_QCAPS_RESP_FLAGS_RFS_RING_TBL_IDX_V2_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_CFA_RFS_RING_TBL_IDX_V2;

hwrm_cfa_adv_qcaps_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int __bnxt_alloc_fw_health(struct bnxt *bp)
{
	if (bp->fw_health)
		return 0;

	bp->fw_health = kzalloc(sizeof(*bp->fw_health), GFP_KERNEL);
	if (!bp->fw_health)
		return -ENOMEM;

	return 0;
}

static int bnxt_alloc_fw_health(struct bnxt *bp)
{
	int rc;

	if (!(bp->fw_cap & BNXT_FW_CAP_HOT_RESET) &&
	    !(bp->fw_cap & BNXT_FW_CAP_ERROR_RECOVERY))
		return 0;

	rc = __bnxt_alloc_fw_health(bp);
	if (rc) {
		bp->fw_cap &= ~BNXT_FW_CAP_HOT_RESET;
		bp->fw_cap &= ~BNXT_FW_CAP_ERROR_RECOVERY;
		return rc;
	}

	return 0;
}

static void __bnxt_map_fw_health_reg(struct bnxt *bp, u32 reg)
{
	writel(reg & BNXT_GRC_BASE_MASK, bp->bar0 +
					 BNXT_GRCPF_REG_WINDOW_BASE_OUT +
					 BNXT_FW_HEALTH_WIN_MAP_OFF);
}

static void bnxt_try_map_fw_health_reg(struct bnxt *bp)
{
	void __iomem *hs;
	u32 status_loc;
	u32 reg_type;
	u32 sig;

	__bnxt_map_fw_health_reg(bp, HCOMM_STATUS_STRUCT_LOC);
	hs = bp->bar0 + BNXT_FW_HEALTH_WIN_OFF(HCOMM_STATUS_STRUCT_LOC);

	sig = readl(hs + offsetof(struct hcomm_status, sig_ver));
	if ((sig & HCOMM_STATUS_SIGNATURE_MASK) != HCOMM_STATUS_SIGNATURE_VAL) {
		if (bp->fw_health)
			bp->fw_health->status_reliable = false;
		return;
	}

	if (__bnxt_alloc_fw_health(bp)) {
		netdev_warn(bp->dev, "no memory for firmware status checks\n");
		return;
	}

	status_loc = readl(hs + offsetof(struct hcomm_status, fw_status_loc));
	bp->fw_health->regs[BNXT_FW_HEALTH_REG] = status_loc;
	reg_type = BNXT_FW_HEALTH_REG_TYPE(status_loc);
	if (reg_type == BNXT_FW_HEALTH_REG_TYPE_GRC) {
		__bnxt_map_fw_health_reg(bp, status_loc);
		bp->fw_health->mapped_regs[BNXT_FW_HEALTH_REG] =
			BNXT_FW_HEALTH_WIN_OFF(status_loc);
	}

	bp->fw_health->status_reliable = true;
}

static int bnxt_map_fw_health_regs(struct bnxt *bp)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	u32 reg_base = 0xffffffff;
	int i;

	/* Only pre-map the monitoring GRC registers using window 3 */
	for (i = 0; i < 4; i++) {
		u32 reg = fw_health->regs[i];

		if (BNXT_FW_HEALTH_REG_TYPE(reg) != BNXT_FW_HEALTH_REG_TYPE_GRC)
			continue;
		if (reg_base == 0xffffffff)
			reg_base = reg & BNXT_GRC_BASE_MASK;
		if ((reg & BNXT_GRC_BASE_MASK) != reg_base)
			return -ERANGE;
		fw_health->mapped_regs[i] = BNXT_FW_HEALTH_WIN_OFF(reg);
	}
	if (reg_base == 0xffffffff)
		return 0;

	__bnxt_map_fw_health_reg(bp, reg_base);
	return 0;
}

static int bnxt_hwrm_error_recovery_qcfg(struct bnxt *bp)
{
	struct hwrm_error_recovery_qcfg_output *resp = bp->hwrm_cmd_resp_addr;
	struct bnxt_fw_health *fw_health = bp->fw_health;
	struct hwrm_error_recovery_qcfg_input req = {0};
	int rc, i;

	if (!(bp->fw_cap & BNXT_FW_CAP_ERROR_RECOVERY))
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_ERROR_RECOVERY_QCFG, -1, -1);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		goto err_recovery_out;
	fw_health->flags = le32_to_cpu(resp->flags);
	if ((fw_health->flags & ERROR_RECOVERY_QCFG_RESP_FLAGS_CO_CPU) &&
	    !(bp->fw_cap & BNXT_FW_CAP_KONG_MB_CHNL)) {
		rc = -EINVAL;
		goto err_recovery_out;
	}
	fw_health->polling_dsecs = le32_to_cpu(resp->driver_polling_freq);
	fw_health->master_func_wait_dsecs =
		le32_to_cpu(resp->master_func_wait_period);
	fw_health->normal_func_wait_dsecs =
		le32_to_cpu(resp->normal_func_wait_period);
	fw_health->post_reset_wait_dsecs =
		le32_to_cpu(resp->master_func_wait_period_after_reset);
	fw_health->post_reset_max_wait_dsecs =
		le32_to_cpu(resp->max_bailout_time_after_reset);
	fw_health->regs[BNXT_FW_HEALTH_REG] =
		le32_to_cpu(resp->fw_health_status_reg);
	fw_health->regs[BNXT_FW_HEARTBEAT_REG] =
		le32_to_cpu(resp->fw_heartbeat_reg);
	fw_health->regs[BNXT_FW_RESET_CNT_REG] =
		le32_to_cpu(resp->fw_reset_cnt_reg);
	fw_health->regs[BNXT_FW_RESET_INPROG_REG] =
		le32_to_cpu(resp->reset_inprogress_reg);
	fw_health->fw_reset_inprog_reg_mask =
		le32_to_cpu(resp->reset_inprogress_reg_mask);
	fw_health->fw_reset_seq_cnt = resp->reg_array_cnt;
	if (fw_health->fw_reset_seq_cnt >= 16) {
		rc = -EINVAL;
		goto err_recovery_out;
	}
	for (i = 0; i < fw_health->fw_reset_seq_cnt; i++) {
		fw_health->fw_reset_seq_regs[i] =
			le32_to_cpu(resp->reset_reg[i]);
		fw_health->fw_reset_seq_vals[i] =
			le32_to_cpu(resp->reset_reg_val[i]);
		fw_health->fw_reset_seq_delay_msec[i] =
			resp->delay_after_reset[i];
	}
err_recovery_out:
	mutex_unlock(&bp->hwrm_cmd_lock);
	if (!rc)
		rc = bnxt_map_fw_health_regs(bp);
	if (rc)
		bp->fw_cap &= ~BNXT_FW_CAP_ERROR_RECOVERY;
	return rc;
}

static int bnxt_hwrm_func_reset(struct bnxt *bp)
{
	struct hwrm_func_reset_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_RESET, -1, -1);
	req.enables = 0;

	return hwrm_send_message(bp, &req, sizeof(req), HWRM_RESET_TIMEOUT);
}

static void bnxt_nvm_cfg_ver_get(struct bnxt *bp)
{
	struct hwrm_nvm_get_dev_info_output nvm_info;

	if (!bnxt_hwrm_nvm_get_dev_info(bp, &nvm_info))
		snprintf(bp->nvm_cfg_ver, FW_VER_STR_LEN, "%d.%d.%d",
			 nvm_info.nvm_cfg_ver_maj, nvm_info.nvm_cfg_ver_min,
			 nvm_info.nvm_cfg_ver_upd);
}

static int bnxt_hwrm_queue_qportcfg(struct bnxt *bp)
{
	int rc = 0;
	struct hwrm_queue_qportcfg_input req = {0};
	struct hwrm_queue_qportcfg_output *resp = bp->hwrm_cmd_resp_addr;
	u8 i, j, *qptr;
	bool no_rdma;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_QUEUE_QPORTCFG, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		goto qportcfg_exit;

	if (!resp->max_configurable_queues) {
		rc = -EINVAL;
		goto qportcfg_exit;
	}
	bp->max_tc = resp->max_configurable_queues;
	bp->max_lltc = resp->max_configurable_lossless_queues;
	if (bp->max_tc > BNXT_MAX_QUEUE)
		bp->max_tc = BNXT_MAX_QUEUE;

	no_rdma = !(bp->flags & BNXT_FLAG_ROCE_CAP);
	qptr = &resp->queue_id0;
	for (i = 0, j = 0; i < bp->max_tc; i++) {
		bp->q_info[j].queue_id = *qptr;
		bp->q_ids[i] = *qptr++;
		bp->q_info[j].queue_profile = *qptr++;
		bp->tc_to_qidx[j] = j;
		if (!BNXT_CNPQ(bp->q_info[j].queue_profile) ||
		    (no_rdma && BNXT_PF(bp)))
			j++;
	}
	bp->max_q = bp->max_tc;
	bp->max_tc = max_t(u8, j, 1);

	if (resp->queue_cfg_info & QUEUE_QPORTCFG_RESP_QUEUE_CFG_INFO_ASYM_CFG)
		bp->max_tc = 1;

	if (bp->max_lltc > bp->max_tc)
		bp->max_lltc = bp->max_tc;

qportcfg_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int __bnxt_hwrm_ver_get(struct bnxt *bp, bool silent)
{
	struct hwrm_ver_get_input req = {0};
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_VER_GET, -1, -1);
	req.hwrm_intf_maj = HWRM_VERSION_MAJOR;
	req.hwrm_intf_min = HWRM_VERSION_MINOR;
	req.hwrm_intf_upd = HWRM_VERSION_UPDATE;

	rc = bnxt_hwrm_do_send_msg(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT,
				   silent);
	return rc;
}

static int bnxt_hwrm_ver_get(struct bnxt *bp)
{
	struct hwrm_ver_get_output *resp = bp->hwrm_cmd_resp_addr;
	u16 fw_maj, fw_min, fw_bld, fw_rsv;
	u32 dev_caps_cfg, hwrm_ver;
	int rc, len;

	bp->hwrm_max_req_len = HWRM_MAX_REQ_LEN;
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = __bnxt_hwrm_ver_get(bp, false);
	if (rc)
		goto hwrm_ver_get_exit;

	memcpy(&bp->ver_resp, resp, sizeof(struct hwrm_ver_get_output));

	bp->hwrm_spec_code = resp->hwrm_intf_maj_8b << 16 |
			     resp->hwrm_intf_min_8b << 8 |
			     resp->hwrm_intf_upd_8b;
	if (resp->hwrm_intf_maj_8b < 1) {
		netdev_warn(bp->dev, "HWRM interface %d.%d.%d is older than 1.0.0.\n",
			    resp->hwrm_intf_maj_8b, resp->hwrm_intf_min_8b,
			    resp->hwrm_intf_upd_8b);
		netdev_warn(bp->dev, "Please update firmware with HWRM interface 1.0.0 or newer.\n");
	}

	hwrm_ver = HWRM_VERSION_MAJOR << 16 | HWRM_VERSION_MINOR << 8 |
			HWRM_VERSION_UPDATE;

	if (bp->hwrm_spec_code > hwrm_ver)
		snprintf(bp->hwrm_ver_supp, FW_VER_STR_LEN, "%d.%d.%d",
			 HWRM_VERSION_MAJOR, HWRM_VERSION_MINOR,
			 HWRM_VERSION_UPDATE);
	else
		snprintf(bp->hwrm_ver_supp, FW_VER_STR_LEN, "%d.%d.%d",
			 resp->hwrm_intf_maj_8b, resp->hwrm_intf_min_8b,
			 resp->hwrm_intf_upd_8b);

	fw_maj = le16_to_cpu(resp->hwrm_fw_major);
	if (bp->hwrm_spec_code > 0x10803 && fw_maj) {
		fw_min = le16_to_cpu(resp->hwrm_fw_minor);
		fw_bld = le16_to_cpu(resp->hwrm_fw_build);
		fw_rsv = le16_to_cpu(resp->hwrm_fw_patch);
		len = FW_VER_STR_LEN;
	} else {
		fw_maj = resp->hwrm_fw_maj_8b;
		fw_min = resp->hwrm_fw_min_8b;
		fw_bld = resp->hwrm_fw_bld_8b;
		fw_rsv = resp->hwrm_fw_rsvd_8b;
		len = BC_HWRM_STR_LEN;
	}
	bp->fw_ver_code = BNXT_FW_VER_CODE(fw_maj, fw_min, fw_bld, fw_rsv);
	snprintf(bp->fw_ver_str, len, "%d.%d.%d.%d", fw_maj, fw_min, fw_bld,
		 fw_rsv);

	if (strlen(resp->active_pkg_name)) {
		int fw_ver_len = strlen(bp->fw_ver_str);

		snprintf(bp->fw_ver_str + fw_ver_len,
			 FW_VER_STR_LEN - fw_ver_len - 1, "/pkg %s",
			 resp->active_pkg_name);
		bp->fw_cap |= BNXT_FW_CAP_PKG_VER;
	}

	bp->hwrm_cmd_timeout = le16_to_cpu(resp->def_req_timeout);
	if (!bp->hwrm_cmd_timeout)
		bp->hwrm_cmd_timeout = DFLT_HWRM_CMD_TIMEOUT;

	if (resp->hwrm_intf_maj_8b >= 1) {
		bp->hwrm_max_req_len = le16_to_cpu(resp->max_req_win_len);
		bp->hwrm_max_ext_req_len = le16_to_cpu(resp->max_ext_req_len);
	}
	if (bp->hwrm_max_ext_req_len < HWRM_MAX_REQ_LEN)
		bp->hwrm_max_ext_req_len = HWRM_MAX_REQ_LEN;

	bp->chip_num = le16_to_cpu(resp->chip_num);
	bp->chip_rev = resp->chip_rev;
	if (bp->chip_num == CHIP_NUM_58700 && !resp->chip_rev &&
	    !resp->chip_metal)
		bp->flags |= BNXT_FLAG_CHIP_NITRO_A0;

	dev_caps_cfg = le32_to_cpu(resp->dev_caps_cfg);
	if ((dev_caps_cfg & VER_GET_RESP_DEV_CAPS_CFG_SHORT_CMD_SUPPORTED) &&
	    (dev_caps_cfg & VER_GET_RESP_DEV_CAPS_CFG_SHORT_CMD_REQUIRED))
		bp->fw_cap |= BNXT_FW_CAP_SHORT_CMD;

	if (dev_caps_cfg & VER_GET_RESP_DEV_CAPS_CFG_KONG_MB_CHNL_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_KONG_MB_CHNL;

	if (dev_caps_cfg &
	    VER_GET_RESP_DEV_CAPS_CFG_FLOW_HANDLE_64BIT_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_OVS_64BIT_HANDLE;

	if (dev_caps_cfg &
	    VER_GET_RESP_DEV_CAPS_CFG_TRUSTED_VF_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_TRUSTED_VF;

	if (dev_caps_cfg &
	    VER_GET_RESP_DEV_CAPS_CFG_CFA_ADV_FLOW_MGNT_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_CFA_ADV_FLOW;

hwrm_ver_get_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

int bnxt_hwrm_fw_set_time(struct bnxt *bp)
{
	struct hwrm_fw_set_time_input req = {0};
	struct tm tm;
	time64_t now = ktime_get_real_seconds();

	if ((BNXT_VF(bp) && bp->hwrm_spec_code < 0x10901) ||
	    bp->hwrm_spec_code < 0x10400)
		return -EOPNOTSUPP;

	time64_to_tm(now, 0, &tm);
	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FW_SET_TIME, -1, -1);
	req.year = cpu_to_le16(1900 + tm.tm_year);
	req.month = 1 + tm.tm_mon;
	req.day = tm.tm_mday;
	req.hour = tm.tm_hour;
	req.minute = tm.tm_min;
	req.second = tm.tm_sec;
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static void bnxt_add_one_ctr(u64 hw, u64 *sw, u64 mask)
{
	u64 sw_tmp;

	hw &= mask;
	sw_tmp = (*sw & ~mask) | hw;
	if (hw < (*sw & mask))
		sw_tmp += mask + 1;
	WRITE_ONCE(*sw, sw_tmp);
}

static void __bnxt_accumulate_stats(__le64 *hw_stats, u64 *sw_stats, u64 *masks,
				    int count, bool ignore_zero)
{
	int i;

	for (i = 0; i < count; i++) {
		u64 hw = le64_to_cpu(READ_ONCE(hw_stats[i]));

		if (ignore_zero && !hw)
			continue;

		if (masks[i] == -1ULL)
			sw_stats[i] = hw;
		else
			bnxt_add_one_ctr(hw, &sw_stats[i], masks[i]);
	}
}

static void bnxt_accumulate_stats(struct bnxt_stats_mem *stats)
{
	if (!stats->hw_stats)
		return;

	__bnxt_accumulate_stats(stats->hw_stats, stats->sw_stats,
				stats->hw_masks, stats->len / 8, false);
}

static void bnxt_accumulate_all_stats(struct bnxt *bp)
{
	struct bnxt_stats_mem *ring0_stats;
	bool ignore_zero = false;
	int i;

	/* Chip bug.  Counter intermittently becomes 0. */
	if (bp->flags & BNXT_FLAG_CHIP_P5)
		ignore_zero = true;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr;
		struct bnxt_stats_mem *stats;

		cpr = &bnapi->cp_ring;
		stats = &cpr->stats;
		if (!i)
			ring0_stats = stats;
		__bnxt_accumulate_stats(stats->hw_stats, stats->sw_stats,
					ring0_stats->hw_masks,
					ring0_stats->len / 8, ignore_zero);
	}
	if (bp->flags & BNXT_FLAG_PORT_STATS) {
		struct bnxt_stats_mem *stats = &bp->port_stats;
		__le64 *hw_stats = stats->hw_stats;
		u64 *sw_stats = stats->sw_stats;
		u64 *masks = stats->hw_masks;
		int cnt;

		cnt = sizeof(struct rx_port_stats) / 8;
		__bnxt_accumulate_stats(hw_stats, sw_stats, masks, cnt, false);

		hw_stats += BNXT_TX_PORT_STATS_BYTE_OFFSET / 8;
		sw_stats += BNXT_TX_PORT_STATS_BYTE_OFFSET / 8;
		masks += BNXT_TX_PORT_STATS_BYTE_OFFSET / 8;
		cnt = sizeof(struct tx_port_stats) / 8;
		__bnxt_accumulate_stats(hw_stats, sw_stats, masks, cnt, false);
	}
	if (bp->flags & BNXT_FLAG_PORT_STATS_EXT) {
		bnxt_accumulate_stats(&bp->rx_port_stats_ext);
		bnxt_accumulate_stats(&bp->tx_port_stats_ext);
	}
}

static int bnxt_hwrm_port_qstats(struct bnxt *bp, u8 flags)
{
	struct bnxt_pf_info *pf = &bp->pf;
	struct hwrm_port_qstats_input req = {0};

	if (!(bp->flags & BNXT_FLAG_PORT_STATS))
		return 0;

	if (flags && !(bp->fw_cap & BNXT_FW_CAP_EXT_HW_STATS_SUPPORTED))
		return -EOPNOTSUPP;

	req.flags = flags;
	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_QSTATS, -1, -1);
	req.port_id = cpu_to_le16(pf->port_id);
	req.tx_stat_host_addr = cpu_to_le64(bp->port_stats.hw_stats_map +
					    BNXT_TX_PORT_STATS_BYTE_OFFSET);
	req.rx_stat_host_addr = cpu_to_le64(bp->port_stats.hw_stats_map);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_hwrm_port_qstats_ext(struct bnxt *bp, u8 flags)
{
	struct hwrm_port_qstats_ext_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_queue_pri2cos_qcfg_input req2 = {0};
	struct hwrm_port_qstats_ext_input req = {0};
	struct bnxt_pf_info *pf = &bp->pf;
	u32 tx_stat_size;
	int rc;

	if (!(bp->flags & BNXT_FLAG_PORT_STATS_EXT))
		return 0;

	if (flags && !(bp->fw_cap & BNXT_FW_CAP_EXT_HW_STATS_SUPPORTED))
		return -EOPNOTSUPP;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_QSTATS_EXT, -1, -1);
	req.flags = flags;
	req.port_id = cpu_to_le16(pf->port_id);
	req.rx_stat_size = cpu_to_le16(sizeof(struct rx_port_stats_ext));
	req.rx_stat_host_addr = cpu_to_le64(bp->rx_port_stats_ext.hw_stats_map);
	tx_stat_size = bp->tx_port_stats_ext.hw_stats ?
		       sizeof(struct tx_port_stats_ext) : 0;
	req.tx_stat_size = cpu_to_le16(tx_stat_size);
	req.tx_stat_host_addr = cpu_to_le64(bp->tx_port_stats_ext.hw_stats_map);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		bp->fw_rx_stats_ext_size = le16_to_cpu(resp->rx_stat_size) / 8;
		bp->fw_tx_stats_ext_size = tx_stat_size ?
			le16_to_cpu(resp->tx_stat_size) / 8 : 0;
	} else {
		bp->fw_rx_stats_ext_size = 0;
		bp->fw_tx_stats_ext_size = 0;
	}
	if (flags)
		goto qstats_done;

	if (bp->fw_tx_stats_ext_size <=
	    offsetof(struct tx_port_stats_ext, pfc_pri0_tx_duration_us) / 8) {
		mutex_unlock(&bp->hwrm_cmd_lock);
		bp->pri2cos_valid = 0;
		return rc;
	}

	bnxt_hwrm_cmd_hdr_init(bp, &req2, HWRM_QUEUE_PRI2COS_QCFG, -1, -1);
	req2.flags = cpu_to_le32(QUEUE_PRI2COS_QCFG_REQ_FLAGS_IVLAN);

	rc = _hwrm_send_message(bp, &req2, sizeof(req2), HWRM_CMD_TIMEOUT);
	if (!rc) {
		struct hwrm_queue_pri2cos_qcfg_output *resp2;
		u8 *pri2cos;
		int i, j;

		resp2 = bp->hwrm_cmd_resp_addr;
		pri2cos = &resp2->pri0_cos_queue_id;
		for (i = 0; i < 8; i++) {
			u8 queue_id = pri2cos[i];
			u8 queue_idx;

			/* Per port queue IDs start from 0, 10, 20, etc */
			queue_idx = queue_id % 10;
			if (queue_idx > BNXT_MAX_QUEUE) {
				bp->pri2cos_valid = false;
				goto qstats_done;
			}
			for (j = 0; j < bp->max_q; j++) {
				if (bp->q_ids[j] == queue_id)
					bp->pri2cos_idx[i] = queue_idx;
			}
		}
		bp->pri2cos_valid = 1;
	}
qstats_done:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static void bnxt_hwrm_free_tunnel_ports(struct bnxt *bp)
{
	if (bp->vxlan_fw_dst_port_id != INVALID_HW_RING_ID)
		bnxt_hwrm_tunnel_dst_port_free(
			bp, TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_VXLAN);
	if (bp->nge_fw_dst_port_id != INVALID_HW_RING_ID)
		bnxt_hwrm_tunnel_dst_port_free(
			bp, TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_GENEVE);
}

static int bnxt_set_tpa(struct bnxt *bp, bool set_tpa)
{
	int rc, i;
	u32 tpa_flags = 0;

	if (set_tpa)
		tpa_flags = bp->flags & BNXT_FLAG_TPA;
	else if (BNXT_NO_FW_ACCESS(bp))
		return 0;
	for (i = 0; i < bp->nr_vnics; i++) {
		rc = bnxt_hwrm_vnic_set_tpa(bp, i, tpa_flags);
		if (rc) {
			netdev_err(bp->dev, "hwrm vnic set tpa failure rc for vnic %d: %x\n",
				   i, rc);
			return rc;
		}
	}
	return 0;
}

static void bnxt_hwrm_clear_vnic_rss(struct bnxt *bp)
{
	int i;

	for (i = 0; i < bp->nr_vnics; i++)
		bnxt_hwrm_vnic_set_rss(bp, i, false);
}

static void bnxt_clear_vnic(struct bnxt *bp)
{
	if (!bp->vnic_info)
		return;

	bnxt_hwrm_clear_vnic_filter(bp);
	if (!(bp->flags & BNXT_FLAG_CHIP_P5)) {
		/* clear all RSS setting before free vnic ctx */
		bnxt_hwrm_clear_vnic_rss(bp);
		bnxt_hwrm_vnic_ctx_free(bp);
	}
	/* before free the vnic, undo the vnic tpa settings */
	if (bp->flags & BNXT_FLAG_TPA)
		bnxt_set_tpa(bp, false);
	bnxt_hwrm_vnic_free(bp);
	if (bp->flags & BNXT_FLAG_CHIP_P5)
		bnxt_hwrm_vnic_ctx_free(bp);
}

static void bnxt_hwrm_resource_free(struct bnxt *bp, bool close_path,
				    bool irq_re_init)
{
	bnxt_clear_vnic(bp);
	bnxt_hwrm_ring_free(bp, close_path);
	bnxt_hwrm_ring_grp_free(bp);
	if (irq_re_init) {
		bnxt_hwrm_stat_ctx_free(bp);
		bnxt_hwrm_free_tunnel_ports(bp);
	}
}

static int bnxt_hwrm_set_br_mode(struct bnxt *bp, u16 br_mode)
{
	struct hwrm_func_cfg_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_CFG, -1, -1);
	req.fid = cpu_to_le16(0xffff);
	req.enables = cpu_to_le32(FUNC_CFG_REQ_ENABLES_EVB_MODE);
	if (br_mode == BRIDGE_MODE_VEB)
		req.evb_mode = FUNC_CFG_REQ_EVB_MODE_VEB;
	else if (br_mode == BRIDGE_MODE_VEPA)
		req.evb_mode = FUNC_CFG_REQ_EVB_MODE_VEPA;
	else
		return -EINVAL;
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_hwrm_set_cache_line_size(struct bnxt *bp, int size)
{
	struct hwrm_func_cfg_input req = {0};

	if (BNXT_VF(bp) || bp->hwrm_spec_code < 0x10803)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_CFG, -1, -1);
	req.fid = cpu_to_le16(0xffff);
	req.enables = cpu_to_le32(FUNC_CFG_REQ_ENABLES_CACHE_LINESIZE);
	req.options = FUNC_CFG_REQ_OPTIONS_CACHE_LINESIZE_SIZE_64;
	if (size == 128)
		req.options = FUNC_CFG_REQ_OPTIONS_CACHE_LINESIZE_SIZE_128;

	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int __bnxt_setup_vnic(struct bnxt *bp, u16 vnic_id)
{
	struct bnxt_vnic_info *vnic = &bp->vnic_info[vnic_id];
	int rc;

	if (vnic->flags & BNXT_VNIC_RFS_NEW_RSS_FLAG)
		goto skip_rss_ctx;

	/* allocate context for vnic */
	rc = bnxt_hwrm_vnic_ctx_alloc(bp, vnic_id, 0);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic %d alloc failure rc: %x\n",
			   vnic_id, rc);
		goto vnic_setup_err;
	}
	bp->rsscos_nr_ctxs++;

	if (BNXT_CHIP_TYPE_NITRO_A0(bp)) {
		rc = bnxt_hwrm_vnic_ctx_alloc(bp, vnic_id, 1);
		if (rc) {
			netdev_err(bp->dev, "hwrm vnic %d cos ctx alloc failure rc: %x\n",
				   vnic_id, rc);
			goto vnic_setup_err;
		}
		bp->rsscos_nr_ctxs++;
	}

skip_rss_ctx:
	/* configure default vnic, ring grp */
	rc = bnxt_hwrm_vnic_cfg(bp, vnic_id);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic %d cfg failure rc: %x\n",
			   vnic_id, rc);
		goto vnic_setup_err;
	}

	/* Enable RSS hashing on vnic */
	rc = bnxt_hwrm_vnic_set_rss(bp, vnic_id, true);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic %d set rss failure rc: %x\n",
			   vnic_id, rc);
		goto vnic_setup_err;
	}

	if (bp->flags & BNXT_FLAG_AGG_RINGS) {
		rc = bnxt_hwrm_vnic_set_hds(bp, vnic_id);
		if (rc) {
			netdev_err(bp->dev, "hwrm vnic %d set hds failure rc: %x\n",
				   vnic_id, rc);
		}
	}

vnic_setup_err:
	return rc;
}

static int __bnxt_setup_vnic_p5(struct bnxt *bp, u16 vnic_id)
{
	int rc, i, nr_ctxs;

	nr_ctxs = bnxt_get_nr_rss_ctxs(bp, bp->rx_nr_rings);
	for (i = 0; i < nr_ctxs; i++) {
		rc = bnxt_hwrm_vnic_ctx_alloc(bp, vnic_id, i);
		if (rc) {
			netdev_err(bp->dev, "hwrm vnic %d ctx %d alloc failure rc: %x\n",
				   vnic_id, i, rc);
			break;
		}
		bp->rsscos_nr_ctxs++;
	}
	if (i < nr_ctxs)
		return -ENOMEM;

	rc = bnxt_hwrm_vnic_set_rss_p5(bp, vnic_id, true);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic %d set rss failure rc: %d\n",
			   vnic_id, rc);
		return rc;
	}
	rc = bnxt_hwrm_vnic_cfg(bp, vnic_id);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic %d cfg failure rc: %x\n",
			   vnic_id, rc);
		return rc;
	}
	if (bp->flags & BNXT_FLAG_AGG_RINGS) {
		rc = bnxt_hwrm_vnic_set_hds(bp, vnic_id);
		if (rc) {
			netdev_err(bp->dev, "hwrm vnic %d set hds failure rc: %x\n",
				   vnic_id, rc);
		}
	}
	return rc;
}

static int bnxt_setup_vnic(struct bnxt *bp, u16 vnic_id)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5)
		return __bnxt_setup_vnic_p5(bp, vnic_id);
	else
		return __bnxt_setup_vnic(bp, vnic_id);
}

static int bnxt_alloc_rfs_vnics(struct bnxt *bp)
{
#ifdef CONFIG_RFS_ACCEL
	int i, rc = 0;

	if (bp->flags & BNXT_FLAG_CHIP_P5)
		return 0;

	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_vnic_info *vnic;
		u16 vnic_id = i + 1;
		u16 ring_id = i;

		if (vnic_id >= bp->nr_vnics)
			break;

		vnic = &bp->vnic_info[vnic_id];
		vnic->flags |= BNXT_VNIC_RFS_FLAG;
		if (bp->flags & BNXT_FLAG_NEW_RSS_CAP)
			vnic->flags |= BNXT_VNIC_RFS_NEW_RSS_FLAG;
		rc = bnxt_hwrm_vnic_alloc(bp, vnic_id, ring_id, 1);
		if (rc) {
			netdev_err(bp->dev, "hwrm vnic %d alloc failure rc: %x\n",
				   vnic_id, rc);
			break;
		}
		rc = bnxt_setup_vnic(bp, vnic_id);
		if (rc)
			break;
	}
	return rc;
#else
	return 0;
#endif
}

/* Allow PF and VF with default VLAN to be in promiscuous mode */
static bool bnxt_promisc_ok(struct bnxt *bp)
{
#ifdef CONFIG_BNXT_SRIOV
	if (BNXT_VF(bp) && !bp->vf.vlan)
		return false;
#endif
	return true;
}

static int bnxt_setup_nitroa0_vnic(struct bnxt *bp)
{
	unsigned int rc = 0;

	rc = bnxt_hwrm_vnic_alloc(bp, 1, bp->rx_nr_rings - 1, 1);
	if (rc) {
		netdev_err(bp->dev, "Cannot allocate special vnic for NS2 A0: %x\n",
			   rc);
		return rc;
	}

	rc = bnxt_hwrm_vnic_cfg(bp, 1);
	if (rc) {
		netdev_err(bp->dev, "Cannot allocate special vnic for NS2 A0: %x\n",
			   rc);
		return rc;
	}
	return rc;
}

static int bnxt_cfg_rx_mode(struct bnxt *);
static bool bnxt_mc_list_updated(struct bnxt *, u32 *);

static int bnxt_init_chip(struct bnxt *bp, bool irq_re_init)
{
	struct bnxt_vnic_info *vnic = &bp->vnic_info[0];
	int rc = 0;
	unsigned int rx_nr_rings = bp->rx_nr_rings;

	if (irq_re_init) {
		rc = bnxt_hwrm_stat_ctx_alloc(bp);
		if (rc) {
			netdev_err(bp->dev, "hwrm stat ctx alloc failure rc: %x\n",
				   rc);
			goto err_out;
		}
	}

	rc = bnxt_hwrm_ring_alloc(bp);
	if (rc) {
		netdev_err(bp->dev, "hwrm ring alloc failure rc: %x\n", rc);
		goto err_out;
	}

	rc = bnxt_hwrm_ring_grp_alloc(bp);
	if (rc) {
		netdev_err(bp->dev, "hwrm_ring_grp alloc failure: %x\n", rc);
		goto err_out;
	}

	if (BNXT_CHIP_TYPE_NITRO_A0(bp))
		rx_nr_rings--;

	/* default vnic 0 */
	rc = bnxt_hwrm_vnic_alloc(bp, 0, 0, rx_nr_rings);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic alloc failure rc: %x\n", rc);
		goto err_out;
	}

	rc = bnxt_setup_vnic(bp, 0);
	if (rc)
		goto err_out;

	if (bp->flags & BNXT_FLAG_RFS) {
		rc = bnxt_alloc_rfs_vnics(bp);
		if (rc)
			goto err_out;
	}

	if (bp->flags & BNXT_FLAG_TPA) {
		rc = bnxt_set_tpa(bp, true);
		if (rc)
			goto err_out;
	}

	if (BNXT_VF(bp))
		bnxt_update_vf_mac(bp);

	/* Filter for default vnic 0 */
	rc = bnxt_hwrm_set_vnic_filter(bp, 0, 0, bp->dev->dev_addr);
	if (rc) {
		netdev_err(bp->dev, "HWRM vnic filter failure rc: %x\n", rc);
		goto err_out;
	}
	vnic->uc_filter_count = 1;

	vnic->rx_mask = 0;
	if (bp->dev->flags & IFF_BROADCAST)
		vnic->rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_BCAST;

	if ((bp->dev->flags & IFF_PROMISC) && bnxt_promisc_ok(bp))
		vnic->rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS;

	if (bp->dev->flags & IFF_ALLMULTI) {
		vnic->rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST;
		vnic->mc_list_count = 0;
	} else {
		u32 mask = 0;

		bnxt_mc_list_updated(bp, &mask);
		vnic->rx_mask |= mask;
	}

	rc = bnxt_cfg_rx_mode(bp);
	if (rc)
		goto err_out;

	rc = bnxt_hwrm_set_coal(bp);
	if (rc)
		netdev_warn(bp->dev, "HWRM set coalescing failure rc: %x\n",
				rc);

	if (BNXT_CHIP_TYPE_NITRO_A0(bp)) {
		rc = bnxt_setup_nitroa0_vnic(bp);
		if (rc)
			netdev_err(bp->dev, "Special vnic setup failure for NS2 A0 rc: %x\n",
				   rc);
	}

	if (BNXT_VF(bp)) {
		bnxt_hwrm_func_qcfg(bp);
		netdev_update_features(bp->dev);
	}

	return 0;

err_out:
	bnxt_hwrm_resource_free(bp, 0, true);

	return rc;
}

static int bnxt_shutdown_nic(struct bnxt *bp, bool irq_re_init)
{
	bnxt_hwrm_resource_free(bp, 1, irq_re_init);
	return 0;
}

static int bnxt_init_nic(struct bnxt *bp, bool irq_re_init)
{
	bnxt_init_cp_rings(bp);
	bnxt_init_rx_rings(bp);
	bnxt_init_tx_rings(bp);
	bnxt_init_ring_grps(bp, irq_re_init);
	bnxt_init_vnics(bp);

	return bnxt_init_chip(bp, irq_re_init);
}

static int bnxt_set_real_num_queues(struct bnxt *bp)
{
	int rc;
	struct net_device *dev = bp->dev;

	rc = netif_set_real_num_tx_queues(dev, bp->tx_nr_rings -
					  bp->tx_nr_rings_xdp);
	if (rc)
		return rc;

	rc = netif_set_real_num_rx_queues(dev, bp->rx_nr_rings);
	if (rc)
		return rc;

#ifdef CONFIG_RFS_ACCEL
	if (bp->flags & BNXT_FLAG_RFS)
		dev->rx_cpu_rmap = alloc_irq_cpu_rmap(bp->rx_nr_rings);
#endif

	return rc;
}

static int bnxt_trim_rings(struct bnxt *bp, int *rx, int *tx, int max,
			   bool shared)
{
	int _rx = *rx, _tx = *tx;

	if (shared) {
		*rx = min_t(int, _rx, max);
		*tx = min_t(int, _tx, max);
	} else {
		if (max < 2)
			return -ENOMEM;

		while (_rx + _tx > max) {
			if (_rx > _tx && _rx > 1)
				_rx--;
			else if (_tx > 1)
				_tx--;
		}
		*rx = _rx;
		*tx = _tx;
	}
	return 0;
}

static void bnxt_setup_msix(struct bnxt *bp)
{
	const int len = sizeof(bp->irq_tbl[0].name);
	struct net_device *dev = bp->dev;
	int tcs, i;

	tcs = netdev_get_num_tc(dev);
	if (tcs) {
		int i, off, count;

		for (i = 0; i < tcs; i++) {
			count = bp->tx_nr_rings_per_tc;
			off = i * count;
			netdev_set_tc_queue(dev, i, count, off);
		}
	}

	for (i = 0; i < bp->cp_nr_rings; i++) {
		int map_idx = bnxt_cp_num_to_irq_num(bp, i);
		char *attr;

		if (bp->flags & BNXT_FLAG_SHARED_RINGS)
			attr = "TxRx";
		else if (i < bp->rx_nr_rings)
			attr = "rx";
		else
			attr = "tx";

		snprintf(bp->irq_tbl[map_idx].name, len, "%s-%s-%d", dev->name,
			 attr, i);
		bp->irq_tbl[map_idx].handler = bnxt_msix;
	}
}

static void bnxt_setup_inta(struct bnxt *bp)
{
	const int len = sizeof(bp->irq_tbl[0].name);

	if (netdev_get_num_tc(bp->dev))
		netdev_reset_tc(bp->dev);

	snprintf(bp->irq_tbl[0].name, len, "%s-%s-%d", bp->dev->name, "TxRx",
		 0);
	bp->irq_tbl[0].handler = bnxt_inta;
}

static int bnxt_setup_int_mode(struct bnxt *bp)
{
	int rc;

	if (bp->flags & BNXT_FLAG_USING_MSIX)
		bnxt_setup_msix(bp);
	else
		bnxt_setup_inta(bp);

	rc = bnxt_set_real_num_queues(bp);
	return rc;
}

#ifdef CONFIG_RFS_ACCEL
static unsigned int bnxt_get_max_func_rss_ctxs(struct bnxt *bp)
{
	return bp->hw_resc.max_rsscos_ctxs;
}

static unsigned int bnxt_get_max_func_vnics(struct bnxt *bp)
{
	return bp->hw_resc.max_vnics;
}
#endif

unsigned int bnxt_get_max_func_stat_ctxs(struct bnxt *bp)
{
	return bp->hw_resc.max_stat_ctxs;
}

unsigned int bnxt_get_max_func_cp_rings(struct bnxt *bp)
{
	return bp->hw_resc.max_cp_rings;
}

static unsigned int bnxt_get_max_func_cp_rings_for_en(struct bnxt *bp)
{
	unsigned int cp = bp->hw_resc.max_cp_rings;

	if (!(bp->flags & BNXT_FLAG_CHIP_P5))
		cp -= bnxt_get_ulp_msix_num(bp);

	return cp;
}

static unsigned int bnxt_get_max_func_irqs(struct bnxt *bp)
{
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;

	if (bp->flags & BNXT_FLAG_CHIP_P5)
		return min_t(unsigned int, hw_resc->max_irqs, hw_resc->max_nqs);

	return min_t(unsigned int, hw_resc->max_irqs, hw_resc->max_cp_rings);
}

static void bnxt_set_max_func_irqs(struct bnxt *bp, unsigned int max_irqs)
{
	bp->hw_resc.max_irqs = max_irqs;
}

unsigned int bnxt_get_avail_cp_rings_for_en(struct bnxt *bp)
{
	unsigned int cp;

	cp = bnxt_get_max_func_cp_rings_for_en(bp);
	if (bp->flags & BNXT_FLAG_CHIP_P5)
		return cp - bp->rx_nr_rings - bp->tx_nr_rings;
	else
		return cp - bp->cp_nr_rings;
}

unsigned int bnxt_get_avail_stat_ctxs_for_en(struct bnxt *bp)
{
	return bnxt_get_max_func_stat_ctxs(bp) - bnxt_get_func_stat_ctxs(bp);
}

int bnxt_get_avail_msix(struct bnxt *bp, int num)
{
	int max_cp = bnxt_get_max_func_cp_rings(bp);
	int max_irq = bnxt_get_max_func_irqs(bp);
	int total_req = bp->cp_nr_rings + num;
	int max_idx, avail_msix;

	max_idx = bp->total_irqs;
	if (!(bp->flags & BNXT_FLAG_CHIP_P5))
		max_idx = min_t(int, bp->total_irqs, max_cp);
	avail_msix = max_idx - bp->cp_nr_rings;
	if (!BNXT_NEW_RM(bp) || avail_msix >= num)
		return avail_msix;

	if (max_irq < total_req) {
		num = max_irq - bp->cp_nr_rings;
		if (num <= 0)
			return 0;
	}
	return num;
}

static int bnxt_get_num_msix(struct bnxt *bp)
{
	if (!BNXT_NEW_RM(bp))
		return bnxt_get_max_func_irqs(bp);

	return bnxt_nq_rings_in_use(bp);
}

static int bnxt_init_msix(struct bnxt *bp)
{
	int i, total_vecs, max, rc = 0, min = 1, ulp_msix;
	struct msix_entry *msix_ent;

	total_vecs = bnxt_get_num_msix(bp);
	max = bnxt_get_max_func_irqs(bp);
	if (total_vecs > max)
		total_vecs = max;

	if (!total_vecs)
		return 0;

	msix_ent = kcalloc(total_vecs, sizeof(struct msix_entry), GFP_KERNEL);
	if (!msix_ent)
		return -ENOMEM;

	for (i = 0; i < total_vecs; i++) {
		msix_ent[i].entry = i;
		msix_ent[i].vector = 0;
	}

	if (!(bp->flags & BNXT_FLAG_SHARED_RINGS))
		min = 2;

	total_vecs = pci_enable_msix_range(bp->pdev, msix_ent, min, total_vecs);
	ulp_msix = bnxt_get_ulp_msix_num(bp);
	if (total_vecs < 0 || total_vecs < ulp_msix) {
		rc = -ENODEV;
		goto msix_setup_exit;
	}

	bp->irq_tbl = kcalloc(total_vecs, sizeof(struct bnxt_irq), GFP_KERNEL);
	if (bp->irq_tbl) {
		for (i = 0; i < total_vecs; i++)
			bp->irq_tbl[i].vector = msix_ent[i].vector;

		bp->total_irqs = total_vecs;
		/* Trim rings based upon num of vectors allocated */
		rc = bnxt_trim_rings(bp, &bp->rx_nr_rings, &bp->tx_nr_rings,
				     total_vecs - ulp_msix, min == 1);
		if (rc)
			goto msix_setup_exit;

		bp->cp_nr_rings = (min == 1) ?
				  max_t(int, bp->tx_nr_rings, bp->rx_nr_rings) :
				  bp->tx_nr_rings + bp->rx_nr_rings;

	} else {
		rc = -ENOMEM;
		goto msix_setup_exit;
	}
	bp->flags |= BNXT_FLAG_USING_MSIX;
	kfree(msix_ent);
	return 0;

msix_setup_exit:
	netdev_err(bp->dev, "bnxt_init_msix err: %x\n", rc);
	kfree(bp->irq_tbl);
	bp->irq_tbl = NULL;
	pci_disable_msix(bp->pdev);
	kfree(msix_ent);
	return rc;
}

static int bnxt_init_inta(struct bnxt *bp)
{
	bp->irq_tbl = kcalloc(1, sizeof(struct bnxt_irq), GFP_KERNEL);
	if (!bp->irq_tbl)
		return -ENOMEM;

	bp->total_irqs = 1;
	bp->rx_nr_rings = 1;
	bp->tx_nr_rings = 1;
	bp->cp_nr_rings = 1;
	bp->flags |= BNXT_FLAG_SHARED_RINGS;
	bp->irq_tbl[0].vector = bp->pdev->irq;
	return 0;
}

static int bnxt_init_int_mode(struct bnxt *bp)
{
	int rc = 0;

	if (bp->flags & BNXT_FLAG_MSIX_CAP)
		rc = bnxt_init_msix(bp);

	if (!(bp->flags & BNXT_FLAG_USING_MSIX) && BNXT_PF(bp)) {
		/* fallback to INTA */
		rc = bnxt_init_inta(bp);
	}
	return rc;
}

static void bnxt_clear_int_mode(struct bnxt *bp)
{
	if (bp->flags & BNXT_FLAG_USING_MSIX)
		pci_disable_msix(bp->pdev);

	kfree(bp->irq_tbl);
	bp->irq_tbl = NULL;
	bp->flags &= ~BNXT_FLAG_USING_MSIX;
}

int bnxt_reserve_rings(struct bnxt *bp, bool irq_re_init)
{
	int tcs = netdev_get_num_tc(bp->dev);
	bool irq_cleared = false;
	int rc;

	if (!bnxt_need_reserve_rings(bp))
		return 0;

	if (irq_re_init && BNXT_NEW_RM(bp) &&
	    bnxt_get_num_msix(bp) != bp->total_irqs) {
		bnxt_ulp_irq_stop(bp);
		bnxt_clear_int_mode(bp);
		irq_cleared = true;
	}
	rc = __bnxt_reserve_rings(bp);
	if (irq_cleared) {
		if (!rc)
			rc = bnxt_init_int_mode(bp);
		bnxt_ulp_irq_restart(bp, rc);
	}
	if (rc) {
		netdev_err(bp->dev, "ring reservation/IRQ init failure rc: %d\n", rc);
		return rc;
	}
	if (tcs && (bp->tx_nr_rings_per_tc * tcs != bp->tx_nr_rings)) {
		netdev_err(bp->dev, "tx ring reservation failure\n");
		netdev_reset_tc(bp->dev);
		bp->tx_nr_rings_per_tc = bp->tx_nr_rings;
		return -ENOMEM;
	}
	return 0;
}

static void bnxt_free_irq(struct bnxt *bp)
{
	struct bnxt_irq *irq;
	int i;

#ifdef CONFIG_RFS_ACCEL
	free_irq_cpu_rmap(bp->dev->rx_cpu_rmap);
	bp->dev->rx_cpu_rmap = NULL;
#endif
	if (!bp->irq_tbl || !bp->bnapi)
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		int map_idx = bnxt_cp_num_to_irq_num(bp, i);

		irq = &bp->irq_tbl[map_idx];
		if (irq->requested) {
			if (irq->have_cpumask) {
				irq_set_affinity_hint(irq->vector, NULL);
				free_cpumask_var(irq->cpu_mask);
				irq->have_cpumask = 0;
			}
			free_irq(irq->vector, bp->bnapi[i]);
		}

		irq->requested = 0;
	}
}

static int bnxt_request_irq(struct bnxt *bp)
{
	int i, j, rc = 0;
	unsigned long flags = 0;
#ifdef CONFIG_RFS_ACCEL
	struct cpu_rmap *rmap;
#endif

	rc = bnxt_setup_int_mode(bp);
	if (rc) {
		netdev_err(bp->dev, "bnxt_setup_int_mode err: %x\n",
			   rc);
		return rc;
	}
#ifdef CONFIG_RFS_ACCEL
	rmap = bp->dev->rx_cpu_rmap;
#endif
	if (!(bp->flags & BNXT_FLAG_USING_MSIX))
		flags = IRQF_SHARED;

	for (i = 0, j = 0; i < bp->cp_nr_rings; i++) {
		int map_idx = bnxt_cp_num_to_irq_num(bp, i);
		struct bnxt_irq *irq = &bp->irq_tbl[map_idx];

#ifdef CONFIG_RFS_ACCEL
		if (rmap && bp->bnapi[i]->rx_ring) {
			rc = irq_cpu_rmap_add(rmap, irq->vector);
			if (rc)
				netdev_warn(bp->dev, "failed adding irq rmap for ring %d\n",
					    j);
			j++;
		}
#endif
		rc = request_irq(irq->vector, irq->handler, flags, irq->name,
				 bp->bnapi[i]);
		if (rc)
			break;

		irq->requested = 1;

		if (zalloc_cpumask_var(&irq->cpu_mask, GFP_KERNEL)) {
			int numa_node = dev_to_node(&bp->pdev->dev);

			irq->have_cpumask = 1;
			cpumask_set_cpu(cpumask_local_spread(i, numa_node),
					irq->cpu_mask);
			rc = irq_set_affinity_hint(irq->vector, irq->cpu_mask);
			if (rc) {
				netdev_warn(bp->dev,
					    "Set affinity failed, IRQ = %d\n",
					    irq->vector);
				break;
			}
		}
	}
	return rc;
}

static void bnxt_del_napi(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];

		__netif_napi_del(&bnapi->napi);
	}
	/* We called __netif_napi_del(), we need
	 * to respect an RCU grace period before freeing napi structures.
	 */
	synchronize_net();
}

static void bnxt_init_napi(struct bnxt *bp)
{
	int i;
	unsigned int cp_nr_rings = bp->cp_nr_rings;
	struct bnxt_napi *bnapi;

	if (bp->flags & BNXT_FLAG_USING_MSIX) {
		int (*poll_fn)(struct napi_struct *, int) = bnxt_poll;

		if (bp->flags & BNXT_FLAG_CHIP_P5)
			poll_fn = bnxt_poll_p5;
		else if (BNXT_CHIP_TYPE_NITRO_A0(bp))
			cp_nr_rings--;
		for (i = 0; i < cp_nr_rings; i++) {
			bnapi = bp->bnapi[i];
			netif_napi_add(bp->dev, &bnapi->napi, poll_fn, 64);
		}
		if (BNXT_CHIP_TYPE_NITRO_A0(bp)) {
			bnapi = bp->bnapi[cp_nr_rings];
			netif_napi_add(bp->dev, &bnapi->napi,
				       bnxt_poll_nitroa0, 64);
		}
	} else {
		bnapi = bp->bnapi[0];
		netif_napi_add(bp->dev, &bnapi->napi, bnxt_poll, 64);
	}
}

static void bnxt_disable_napi(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_cp_ring_info *cpr = &bp->bnapi[i]->cp_ring;

		if (bp->bnapi[i]->rx_ring)
			cancel_work_sync(&cpr->dim.work);

		napi_disable(&bp->bnapi[i]->napi);
	}
}

static void bnxt_enable_napi(struct bnxt *bp)
{
	int i;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr;

		cpr = &bnapi->cp_ring;
		if (bnapi->in_reset)
			cpr->sw_stats.rx.rx_resets++;
		bnapi->in_reset = false;

		if (bnapi->rx_ring) {
			INIT_WORK(&cpr->dim.work, bnxt_dim_work);
			cpr->dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;
		}
		napi_enable(&bnapi->napi);
	}
}

void bnxt_tx_disable(struct bnxt *bp)
{
	int i;
	struct bnxt_tx_ring_info *txr;

	if (bp->tx_ring) {
		for (i = 0; i < bp->tx_nr_rings; i++) {
			txr = &bp->tx_ring[i];
			txr->dev_state = BNXT_DEV_STATE_CLOSING;
		}
	}
	/* Stop all TX queues */
	netif_tx_disable(bp->dev);
	netif_carrier_off(bp->dev);
}

void bnxt_tx_enable(struct bnxt *bp)
{
	int i;
	struct bnxt_tx_ring_info *txr;

	for (i = 0; i < bp->tx_nr_rings; i++) {
		txr = &bp->tx_ring[i];
		txr->dev_state = 0;
	}
	netif_tx_wake_all_queues(bp->dev);
	if (bp->link_info.link_up)
		netif_carrier_on(bp->dev);
}

static char *bnxt_report_fec(struct bnxt_link_info *link_info)
{
	u8 active_fec = link_info->active_fec_sig_mode &
			PORT_PHY_QCFG_RESP_ACTIVE_FEC_MASK;

	switch (active_fec) {
	default:
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_NONE_ACTIVE:
		return "None";
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_CLAUSE74_ACTIVE:
		return "Clause 74 BaseR";
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_CLAUSE91_ACTIVE:
		return "Clause 91 RS(528,514)";
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_RS544_1XN_ACTIVE:
		return "Clause 91 RS544_1XN";
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_RS544_IEEE_ACTIVE:
		return "Clause 91 RS(544,514)";
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_RS272_1XN_ACTIVE:
		return "Clause 91 RS272_1XN";
	case PORT_PHY_QCFG_RESP_ACTIVE_FEC_FEC_RS272_IEEE_ACTIVE:
		return "Clause 91 RS(272,257)";
	}
}

static void bnxt_report_link(struct bnxt *bp)
{
	if (bp->link_info.link_up) {
		const char *duplex;
		const char *flow_ctrl;
		u32 speed;
		u16 fec;

		netif_carrier_on(bp->dev);
		speed = bnxt_fw_to_ethtool_speed(bp->link_info.link_speed);
		if (speed == SPEED_UNKNOWN) {
			netdev_info(bp->dev, "NIC Link is Up, speed unknown\n");
			return;
		}
		if (bp->link_info.duplex == BNXT_LINK_DUPLEX_FULL)
			duplex = "full";
		else
			duplex = "half";
		if (bp->link_info.pause == BNXT_LINK_PAUSE_BOTH)
			flow_ctrl = "ON - receive & transmit";
		else if (bp->link_info.pause == BNXT_LINK_PAUSE_TX)
			flow_ctrl = "ON - transmit";
		else if (bp->link_info.pause == BNXT_LINK_PAUSE_RX)
			flow_ctrl = "ON - receive";
		else
			flow_ctrl = "none";
		netdev_info(bp->dev, "NIC Link is Up, %u Mbps %s duplex, Flow control: %s\n",
			    speed, duplex, flow_ctrl);
		if (bp->flags & BNXT_FLAG_EEE_CAP)
			netdev_info(bp->dev, "EEE is %s\n",
				    bp->eee.eee_active ? "active" :
							 "not active");
		fec = bp->link_info.fec_cfg;
		if (!(fec & PORT_PHY_QCFG_RESP_FEC_CFG_FEC_NONE_SUPPORTED))
			netdev_info(bp->dev, "FEC autoneg %s encoding: %s\n",
				    (fec & BNXT_FEC_AUTONEG) ? "on" : "off",
				    bnxt_report_fec(&bp->link_info));
	} else {
		netif_carrier_off(bp->dev);
		netdev_err(bp->dev, "NIC Link is Down\n");
	}
}

static bool bnxt_phy_qcaps_no_speed(struct hwrm_port_phy_qcaps_output *resp)
{
	if (!resp->supported_speeds_auto_mode &&
	    !resp->supported_speeds_force_mode &&
	    !resp->supported_pam4_speeds_auto_mode &&
	    !resp->supported_pam4_speeds_force_mode)
		return true;
	return false;
}

static int bnxt_hwrm_phy_qcaps(struct bnxt *bp)
{
	int rc = 0;
	struct hwrm_port_phy_qcaps_input req = {0};
	struct hwrm_port_phy_qcaps_output *resp = bp->hwrm_cmd_resp_addr;
	struct bnxt_link_info *link_info = &bp->link_info;

	bp->flags &= ~BNXT_FLAG_EEE_CAP;
	if (bp->test_info)
		bp->test_info->flags &= ~(BNXT_TEST_FL_EXT_LPBK |
					  BNXT_TEST_FL_AN_PHY_LPBK);
	if (bp->hwrm_spec_code < 0x10201)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_PHY_QCAPS, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		goto hwrm_phy_qcaps_exit;

	if (resp->flags & PORT_PHY_QCAPS_RESP_FLAGS_EEE_SUPPORTED) {
		struct ethtool_eee *eee = &bp->eee;
		u16 fw_speeds = le16_to_cpu(resp->supported_speeds_eee_mode);

		bp->flags |= BNXT_FLAG_EEE_CAP;
		eee->supported = _bnxt_fw_to_ethtool_adv_spds(fw_speeds, 0);
		bp->lpi_tmr_lo = le32_to_cpu(resp->tx_lpi_timer_low) &
				 PORT_PHY_QCAPS_RESP_TX_LPI_TIMER_LOW_MASK;
		bp->lpi_tmr_hi = le32_to_cpu(resp->valid_tx_lpi_timer_high) &
				 PORT_PHY_QCAPS_RESP_TX_LPI_TIMER_HIGH_MASK;
	}
	if (resp->flags & PORT_PHY_QCAPS_RESP_FLAGS_EXTERNAL_LPBK_SUPPORTED) {
		if (bp->test_info)
			bp->test_info->flags |= BNXT_TEST_FL_EXT_LPBK;
	}
	if (resp->flags & PORT_PHY_QCAPS_RESP_FLAGS_AUTONEG_LPBK_SUPPORTED) {
		if (bp->test_info)
			bp->test_info->flags |= BNXT_TEST_FL_AN_PHY_LPBK;
	}
	if (resp->flags & PORT_PHY_QCAPS_RESP_FLAGS_SHARED_PHY_CFG_SUPPORTED) {
		if (BNXT_PF(bp))
			bp->fw_cap |= BNXT_FW_CAP_SHARED_PORT_CFG;
	}
	if (resp->flags & PORT_PHY_QCAPS_RESP_FLAGS_CUMULATIVE_COUNTERS_ON_RESET)
		bp->fw_cap |= BNXT_FW_CAP_PORT_STATS_NO_RESET;

	if (bp->hwrm_spec_code >= 0x10a01) {
		if (bnxt_phy_qcaps_no_speed(resp)) {
			link_info->phy_state = BNXT_PHY_STATE_DISABLED;
			netdev_warn(bp->dev, "Ethernet link disabled\n");
		} else if (link_info->phy_state == BNXT_PHY_STATE_DISABLED) {
			link_info->phy_state = BNXT_PHY_STATE_ENABLED;
			netdev_info(bp->dev, "Ethernet link enabled\n");
			/* Phy re-enabled, reprobe the speeds */
			link_info->support_auto_speeds = 0;
			link_info->support_pam4_auto_speeds = 0;
		}
	}
	if (resp->supported_speeds_auto_mode)
		link_info->support_auto_speeds =
			le16_to_cpu(resp->supported_speeds_auto_mode);
	if (resp->supported_pam4_speeds_auto_mode)
		link_info->support_pam4_auto_speeds =
			le16_to_cpu(resp->supported_pam4_speeds_auto_mode);

	bp->port_count = resp->port_cnt;

hwrm_phy_qcaps_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static bool bnxt_support_dropped(u16 advertising, u16 supported)
{
	u16 diff = advertising ^ supported;

	return ((supported | diff) != supported);
}

int bnxt_update_link(struct bnxt *bp, bool chng_link_state)
{
	int rc = 0;
	struct bnxt_link_info *link_info = &bp->link_info;
	struct hwrm_port_phy_qcfg_input req = {0};
	struct hwrm_port_phy_qcfg_output *resp = bp->hwrm_cmd_resp_addr;
	u8 link_up = link_info->link_up;
	bool support_changed = false;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_PHY_QCFG, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc) {
		mutex_unlock(&bp->hwrm_cmd_lock);
		return rc;
	}

	memcpy(&link_info->phy_qcfg_resp, resp, sizeof(*resp));
	link_info->phy_link_status = resp->link;
	link_info->duplex = resp->duplex_cfg;
	if (bp->hwrm_spec_code >= 0x10800)
		link_info->duplex = resp->duplex_state;
	link_info->pause = resp->pause;
	link_info->auto_mode = resp->auto_mode;
	link_info->auto_pause_setting = resp->auto_pause;
	link_info->lp_pause = resp->link_partner_adv_pause;
	link_info->force_pause_setting = resp->force_pause;
	link_info->duplex_setting = resp->duplex_cfg;
	if (link_info->phy_link_status == BNXT_LINK_LINK)
		link_info->link_speed = le16_to_cpu(resp->link_speed);
	else
		link_info->link_speed = 0;
	link_info->force_link_speed = le16_to_cpu(resp->force_link_speed);
	link_info->force_pam4_link_speed =
		le16_to_cpu(resp->force_pam4_link_speed);
	link_info->support_speeds = le16_to_cpu(resp->support_speeds);
	link_info->support_pam4_speeds = le16_to_cpu(resp->support_pam4_speeds);
	link_info->auto_link_speeds = le16_to_cpu(resp->auto_link_speed_mask);
	link_info->auto_pam4_link_speeds =
		le16_to_cpu(resp->auto_pam4_link_speed_mask);
	link_info->lp_auto_link_speeds =
		le16_to_cpu(resp->link_partner_adv_speeds);
	link_info->lp_auto_pam4_link_speeds =
		resp->link_partner_pam4_adv_speeds;
	link_info->preemphasis = le32_to_cpu(resp->preemphasis);
	link_info->phy_ver[0] = resp->phy_maj;
	link_info->phy_ver[1] = resp->phy_min;
	link_info->phy_ver[2] = resp->phy_bld;
	link_info->media_type = resp->media_type;
	link_info->phy_type = resp->phy_type;
	link_info->transceiver = resp->xcvr_pkg_type;
	link_info->phy_addr = resp->eee_config_phy_addr &
			      PORT_PHY_QCFG_RESP_PHY_ADDR_MASK;
	link_info->module_status = resp->module_status;

	if (bp->flags & BNXT_FLAG_EEE_CAP) {
		struct ethtool_eee *eee = &bp->eee;
		u16 fw_speeds;

		eee->eee_active = 0;
		if (resp->eee_config_phy_addr &
		    PORT_PHY_QCFG_RESP_EEE_CONFIG_EEE_ACTIVE) {
			eee->eee_active = 1;
			fw_speeds = le16_to_cpu(
				resp->link_partner_adv_eee_link_speed_mask);
			eee->lp_advertised =
				_bnxt_fw_to_ethtool_adv_spds(fw_speeds, 0);
		}

		/* Pull initial EEE config */
		if (!chng_link_state) {
			if (resp->eee_config_phy_addr &
			    PORT_PHY_QCFG_RESP_EEE_CONFIG_EEE_ENABLED)
				eee->eee_enabled = 1;

			fw_speeds = le16_to_cpu(resp->adv_eee_link_speed_mask);
			eee->advertised =
				_bnxt_fw_to_ethtool_adv_spds(fw_speeds, 0);

			if (resp->eee_config_phy_addr &
			    PORT_PHY_QCFG_RESP_EEE_CONFIG_EEE_TX_LPI) {
				__le32 tmr;

				eee->tx_lpi_enabled = 1;
				tmr = resp->xcvr_identifier_type_tx_lpi_timer;
				eee->tx_lpi_timer = le32_to_cpu(tmr) &
					PORT_PHY_QCFG_RESP_TX_LPI_TIMER_MASK;
			}
		}
	}

	link_info->fec_cfg = PORT_PHY_QCFG_RESP_FEC_CFG_FEC_NONE_SUPPORTED;
	if (bp->hwrm_spec_code >= 0x10504) {
		link_info->fec_cfg = le16_to_cpu(resp->fec_cfg);
		link_info->active_fec_sig_mode = resp->active_fec_signal_mode;
	}
	/* TODO: need to add more logic to report VF link */
	if (chng_link_state) {
		if (link_info->phy_link_status == BNXT_LINK_LINK)
			link_info->link_up = 1;
		else
			link_info->link_up = 0;
		if (link_up != link_info->link_up)
			bnxt_report_link(bp);
	} else {
		/* alwasy link down if not require to update link state */
		link_info->link_up = 0;
	}
	mutex_unlock(&bp->hwrm_cmd_lock);

	if (!BNXT_PHY_CFG_ABLE(bp))
		return 0;

	/* Check if any advertised speeds are no longer supported. The caller
	 * holds the link_lock mutex, so we can modify link_info settings.
	 */
	if (bnxt_support_dropped(link_info->advertising,
				 link_info->support_auto_speeds)) {
		link_info->advertising = link_info->support_auto_speeds;
		support_changed = true;
	}
	if (bnxt_support_dropped(link_info->advertising_pam4,
				 link_info->support_pam4_auto_speeds)) {
		link_info->advertising_pam4 = link_info->support_pam4_auto_speeds;
		support_changed = true;
	}
	if (support_changed && (link_info->autoneg & BNXT_AUTONEG_SPEED))
		bnxt_hwrm_set_link_setting(bp, true, false);
	return 0;
}

static void bnxt_get_port_module_status(struct bnxt *bp)
{
	struct bnxt_link_info *link_info = &bp->link_info;
	struct hwrm_port_phy_qcfg_output *resp = &link_info->phy_qcfg_resp;
	u8 module_status;

	if (bnxt_update_link(bp, true))
		return;

	module_status = link_info->module_status;
	switch (module_status) {
	case PORT_PHY_QCFG_RESP_MODULE_STATUS_DISABLETX:
	case PORT_PHY_QCFG_RESP_MODULE_STATUS_PWRDOWN:
	case PORT_PHY_QCFG_RESP_MODULE_STATUS_WARNINGMSG:
		netdev_warn(bp->dev, "Unqualified SFP+ module detected on port %d\n",
			    bp->pf.port_id);
		if (bp->hwrm_spec_code >= 0x10201) {
			netdev_warn(bp->dev, "Module part number %s\n",
				    resp->phy_vendor_partnumber);
		}
		if (module_status == PORT_PHY_QCFG_RESP_MODULE_STATUS_DISABLETX)
			netdev_warn(bp->dev, "TX is disabled\n");
		if (module_status == PORT_PHY_QCFG_RESP_MODULE_STATUS_PWRDOWN)
			netdev_warn(bp->dev, "SFP+ module is shutdown\n");
	}
}

static void
bnxt_hwrm_set_pause_common(struct bnxt *bp, struct hwrm_port_phy_cfg_input *req)
{
	if (bp->link_info.autoneg & BNXT_AUTONEG_FLOW_CTRL) {
		if (bp->hwrm_spec_code >= 0x10201)
			req->auto_pause =
				PORT_PHY_CFG_REQ_AUTO_PAUSE_AUTONEG_PAUSE;
		if (bp->link_info.req_flow_ctrl & BNXT_LINK_PAUSE_RX)
			req->auto_pause |= PORT_PHY_CFG_REQ_AUTO_PAUSE_RX;
		if (bp->link_info.req_flow_ctrl & BNXT_LINK_PAUSE_TX)
			req->auto_pause |= PORT_PHY_CFG_REQ_AUTO_PAUSE_TX;
		req->enables |=
			cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_AUTO_PAUSE);
	} else {
		if (bp->link_info.req_flow_ctrl & BNXT_LINK_PAUSE_RX)
			req->force_pause |= PORT_PHY_CFG_REQ_FORCE_PAUSE_RX;
		if (bp->link_info.req_flow_ctrl & BNXT_LINK_PAUSE_TX)
			req->force_pause |= PORT_PHY_CFG_REQ_FORCE_PAUSE_TX;
		req->enables |=
			cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_FORCE_PAUSE);
		if (bp->hwrm_spec_code >= 0x10201) {
			req->auto_pause = req->force_pause;
			req->enables |= cpu_to_le32(
				PORT_PHY_CFG_REQ_ENABLES_AUTO_PAUSE);
		}
	}
}

static void bnxt_hwrm_set_link_common(struct bnxt *bp, struct hwrm_port_phy_cfg_input *req)
{
	if (bp->link_info.autoneg & BNXT_AUTONEG_SPEED) {
		req->auto_mode |= PORT_PHY_CFG_REQ_AUTO_MODE_SPEED_MASK;
		if (bp->link_info.advertising) {
			req->enables |= cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_AUTO_LINK_SPEED_MASK);
			req->auto_link_speed_mask = cpu_to_le16(bp->link_info.advertising);
		}
		if (bp->link_info.advertising_pam4) {
			req->enables |=
				cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_AUTO_PAM4_LINK_SPEED_MASK);
			req->auto_link_pam4_speed_mask =
				cpu_to_le16(bp->link_info.advertising_pam4);
		}
		req->enables |= cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_AUTO_MODE);
		req->flags |= cpu_to_le32(PORT_PHY_CFG_REQ_FLAGS_RESTART_AUTONEG);
	} else {
		req->flags |= cpu_to_le32(PORT_PHY_CFG_REQ_FLAGS_FORCE);
		if (bp->link_info.req_signal_mode == BNXT_SIG_MODE_PAM4) {
			req->force_pam4_link_speed = cpu_to_le16(bp->link_info.req_link_speed);
			req->enables |= cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_FORCE_PAM4_LINK_SPEED);
		} else {
			req->force_link_speed = cpu_to_le16(bp->link_info.req_link_speed);
		}
	}

	/* tell chimp that the setting takes effect immediately */
	req->flags |= cpu_to_le32(PORT_PHY_CFG_REQ_FLAGS_RESET_PHY);
}

int bnxt_hwrm_set_pause(struct bnxt *bp)
{
	struct hwrm_port_phy_cfg_input req = {0};
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_PHY_CFG, -1, -1);
	bnxt_hwrm_set_pause_common(bp, &req);

	if ((bp->link_info.autoneg & BNXT_AUTONEG_FLOW_CTRL) ||
	    bp->link_info.force_link_chng)
		bnxt_hwrm_set_link_common(bp, &req);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc && !(bp->link_info.autoneg & BNXT_AUTONEG_FLOW_CTRL)) {
		/* since changing of pause setting doesn't trigger any link
		 * change event, the driver needs to update the current pause
		 * result upon successfully return of the phy_cfg command
		 */
		bp->link_info.pause =
		bp->link_info.force_pause_setting = bp->link_info.req_flow_ctrl;
		bp->link_info.auto_pause_setting = 0;
		if (!bp->link_info.force_link_chng)
			bnxt_report_link(bp);
	}
	bp->link_info.force_link_chng = false;
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static void bnxt_hwrm_set_eee(struct bnxt *bp,
			      struct hwrm_port_phy_cfg_input *req)
{
	struct ethtool_eee *eee = &bp->eee;

	if (eee->eee_enabled) {
		u16 eee_speeds;
		u32 flags = PORT_PHY_CFG_REQ_FLAGS_EEE_ENABLE;

		if (eee->tx_lpi_enabled)
			flags |= PORT_PHY_CFG_REQ_FLAGS_EEE_TX_LPI_ENABLE;
		else
			flags |= PORT_PHY_CFG_REQ_FLAGS_EEE_TX_LPI_DISABLE;

		req->flags |= cpu_to_le32(flags);
		eee_speeds = bnxt_get_fw_auto_link_speeds(eee->advertised);
		req->eee_link_speed_mask = cpu_to_le16(eee_speeds);
		req->tx_lpi_timer = cpu_to_le32(eee->tx_lpi_timer);
	} else {
		req->flags |= cpu_to_le32(PORT_PHY_CFG_REQ_FLAGS_EEE_DISABLE);
	}
}

int bnxt_hwrm_set_link_setting(struct bnxt *bp, bool set_pause, bool set_eee)
{
	struct hwrm_port_phy_cfg_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_PHY_CFG, -1, -1);
	if (set_pause)
		bnxt_hwrm_set_pause_common(bp, &req);

	bnxt_hwrm_set_link_common(bp, &req);

	if (set_eee)
		bnxt_hwrm_set_eee(bp, &req);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_hwrm_shutdown_link(struct bnxt *bp)
{
	struct hwrm_port_phy_cfg_input req = {0};

	if (!BNXT_SINGLE_PF(bp))
		return 0;

	if (pci_num_vf(bp->pdev))
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_PHY_CFG, -1, -1);
	req.flags = cpu_to_le32(PORT_PHY_CFG_REQ_FLAGS_FORCE_LINK_DWN);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static int bnxt_fw_init_one(struct bnxt *bp);

static int bnxt_hwrm_if_change(struct bnxt *bp, bool up)
{
	struct hwrm_func_drv_if_change_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_func_drv_if_change_input req = {0};
	bool resc_reinit = false, fw_reset = false;
	u32 flags = 0;
	int rc;

	if (!(bp->fw_cap & BNXT_FW_CAP_IF_CHANGE))
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_DRV_IF_CHANGE, -1, -1);
	if (up)
		req.flags = cpu_to_le32(FUNC_DRV_IF_CHANGE_REQ_FLAGS_UP);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		flags = le32_to_cpu(resp->flags);
	mutex_unlock(&bp->hwrm_cmd_lock);
	if (rc)
		return rc;

	if (!up)
		return 0;

	if (flags & FUNC_DRV_IF_CHANGE_RESP_FLAGS_RESC_CHANGE)
		resc_reinit = true;
	if (flags & FUNC_DRV_IF_CHANGE_RESP_FLAGS_HOT_FW_RESET_DONE)
		fw_reset = true;

	if (test_bit(BNXT_STATE_IN_FW_RESET, &bp->state) && !fw_reset) {
		netdev_err(bp->dev, "RESET_DONE not set during FW reset.\n");
		return -ENODEV;
	}
	if (resc_reinit || fw_reset) {
		if (fw_reset) {
			if (!test_bit(BNXT_STATE_IN_FW_RESET, &bp->state))
				bnxt_ulp_stop(bp);
			bnxt_free_ctx_mem(bp);
			kfree(bp->ctx);
			bp->ctx = NULL;
			bnxt_dcb_free(bp);
			rc = bnxt_fw_init_one(bp);
			if (rc) {
				set_bit(BNXT_STATE_ABORT_ERR, &bp->state);
				return rc;
			}
			bnxt_clear_int_mode(bp);
			rc = bnxt_init_int_mode(bp);
			if (rc) {
				netdev_err(bp->dev, "init int mode failed\n");
				return rc;
			}
			set_bit(BNXT_STATE_FW_RESET_DET, &bp->state);
		}
		if (BNXT_NEW_RM(bp)) {
			struct bnxt_hw_resc *hw_resc = &bp->hw_resc;

			rc = bnxt_hwrm_func_resc_qcaps(bp, true);
			hw_resc->resv_cp_rings = 0;
			hw_resc->resv_stat_ctxs = 0;
			hw_resc->resv_irqs = 0;
			hw_resc->resv_tx_rings = 0;
			hw_resc->resv_rx_rings = 0;
			hw_resc->resv_hw_ring_grps = 0;
			hw_resc->resv_vnics = 0;
			if (!fw_reset) {
				bp->tx_nr_rings = 0;
				bp->rx_nr_rings = 0;
			}
		}
	}
	return 0;
}

static int bnxt_hwrm_port_led_qcaps(struct bnxt *bp)
{
	struct hwrm_port_led_qcaps_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_port_led_qcaps_input req = {0};
	struct bnxt_pf_info *pf = &bp->pf;
	int rc;

	bp->num_leds = 0;
	if (BNXT_VF(bp) || bp->hwrm_spec_code < 0x10601)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_LED_QCAPS, -1, -1);
	req.port_id = cpu_to_le16(pf->port_id);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc) {
		mutex_unlock(&bp->hwrm_cmd_lock);
		return rc;
	}
	if (resp->num_leds > 0 && resp->num_leds < BNXT_MAX_LED) {
		int i;

		bp->num_leds = resp->num_leds;
		memcpy(bp->leds, &resp->led0_id, sizeof(bp->leds[0]) *
						 bp->num_leds);
		for (i = 0; i < bp->num_leds; i++) {
			struct bnxt_led_info *led = &bp->leds[i];
			__le16 caps = led->led_state_caps;

			if (!led->led_group_id ||
			    !BNXT_LED_ALT_BLINK_CAP(caps)) {
				bp->num_leds = 0;
				break;
			}
		}
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return 0;
}

int bnxt_hwrm_alloc_wol_fltr(struct bnxt *bp)
{
	struct hwrm_wol_filter_alloc_input req = {0};
	struct hwrm_wol_filter_alloc_output *resp = bp->hwrm_cmd_resp_addr;
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_WOL_FILTER_ALLOC, -1, -1);
	req.port_id = cpu_to_le16(bp->pf.port_id);
	req.wol_type = WOL_FILTER_ALLOC_REQ_WOL_TYPE_MAGICPKT;
	req.enables = cpu_to_le32(WOL_FILTER_ALLOC_REQ_ENABLES_MAC_ADDRESS);
	memcpy(req.mac_address, bp->dev->dev_addr, ETH_ALEN);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		bp->wol_filter_id = resp->wol_filter_id;
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

int bnxt_hwrm_free_wol_fltr(struct bnxt *bp)
{
	struct hwrm_wol_filter_free_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_WOL_FILTER_FREE, -1, -1);
	req.port_id = cpu_to_le16(bp->pf.port_id);
	req.enables = cpu_to_le32(WOL_FILTER_FREE_REQ_ENABLES_WOL_FILTER_ID);
	req.wol_filter_id = bp->wol_filter_id;
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

static u16 bnxt_hwrm_get_wol_fltrs(struct bnxt *bp, u16 handle)
{
	struct hwrm_wol_filter_qcfg_input req = {0};
	struct hwrm_wol_filter_qcfg_output *resp = bp->hwrm_cmd_resp_addr;
	u16 next_handle = 0;
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_WOL_FILTER_QCFG, -1, -1);
	req.port_id = cpu_to_le16(bp->pf.port_id);
	req.handle = cpu_to_le16(handle);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		next_handle = le16_to_cpu(resp->next_handle);
		if (next_handle != 0) {
			if (resp->wol_type ==
			    WOL_FILTER_ALLOC_REQ_WOL_TYPE_MAGICPKT) {
				bp->wol = 1;
				bp->wol_filter_id = resp->wol_filter_id;
			}
		}
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return next_handle;
}

static void bnxt_get_wol_settings(struct bnxt *bp)
{
	u16 handle = 0;

	bp->wol = 0;
	if (!BNXT_PF(bp) || !(bp->flags & BNXT_FLAG_WOL_CAP))
		return;

	do {
		handle = bnxt_hwrm_get_wol_fltrs(bp, handle);
	} while (handle && handle != 0xffff);
}

#ifdef CONFIG_BNXT_HWMON
static ssize_t bnxt_show_temp(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct hwrm_temp_monitor_query_input req = {0};
	struct hwrm_temp_monitor_query_output *resp;
	struct bnxt *bp = dev_get_drvdata(dev);
	u32 len = 0;
	int rc;

	resp = bp->hwrm_cmd_resp_addr;
	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_TEMP_MONITOR_QUERY, -1, -1);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		len = sprintf(buf, "%u\n", resp->temp * 1000); /* display millidegree */
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc ?: len;
}
static SENSOR_DEVICE_ATTR(temp1_input, 0444, bnxt_show_temp, NULL, 0);

static struct attribute *bnxt_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(bnxt);

static void bnxt_hwmon_close(struct bnxt *bp)
{
	if (bp->hwmon_dev) {
		hwmon_device_unregister(bp->hwmon_dev);
		bp->hwmon_dev = NULL;
	}
}

static void bnxt_hwmon_open(struct bnxt *bp)
{
	struct hwrm_temp_monitor_query_input req = {0};
	struct pci_dev *pdev = bp->pdev;
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_TEMP_MONITOR_QUERY, -1, -1);
	rc = hwrm_send_message_silent(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc == -EACCES || rc == -EOPNOTSUPP) {
		bnxt_hwmon_close(bp);
		return;
	}

	if (bp->hwmon_dev)
		return;

	bp->hwmon_dev = hwmon_device_register_with_groups(&pdev->dev,
							  DRV_MODULE_NAME, bp,
							  bnxt_groups);
	if (IS_ERR(bp->hwmon_dev)) {
		bp->hwmon_dev = NULL;
		dev_warn(&pdev->dev, "Cannot register hwmon device\n");
	}
}
#else
static void bnxt_hwmon_close(struct bnxt *bp)
{
}

static void bnxt_hwmon_open(struct bnxt *bp)
{
}
#endif

static bool bnxt_eee_config_ok(struct bnxt *bp)
{
	struct ethtool_eee *eee = &bp->eee;
	struct bnxt_link_info *link_info = &bp->link_info;

	if (!(bp->flags & BNXT_FLAG_EEE_CAP))
		return true;

	if (eee->eee_enabled) {
		u32 advertising =
			_bnxt_fw_to_ethtool_adv_spds(link_info->advertising, 0);

		if (!(link_info->autoneg & BNXT_AUTONEG_SPEED)) {
			eee->eee_enabled = 0;
			return false;
		}
		if (eee->advertised & ~advertising) {
			eee->advertised = advertising & eee->supported;
			return false;
		}
	}
	return true;
}

static int bnxt_update_phy_setting(struct bnxt *bp)
{
	int rc;
	bool update_link = false;
	bool update_pause = false;
	bool update_eee = false;
	struct bnxt_link_info *link_info = &bp->link_info;

	rc = bnxt_update_link(bp, true);
	if (rc) {
		netdev_err(bp->dev, "failed to update link (rc: %x)\n",
			   rc);
		return rc;
	}
	if (!BNXT_SINGLE_PF(bp))
		return 0;

	if ((link_info->autoneg & BNXT_AUTONEG_FLOW_CTRL) &&
	    (link_info->auto_pause_setting & BNXT_LINK_PAUSE_BOTH) !=
	    link_info->req_flow_ctrl)
		update_pause = true;
	if (!(link_info->autoneg & BNXT_AUTONEG_FLOW_CTRL) &&
	    link_info->force_pause_setting != link_info->req_flow_ctrl)
		update_pause = true;
	if (!(link_info->autoneg & BNXT_AUTONEG_SPEED)) {
		if (BNXT_AUTO_MODE(link_info->auto_mode))
			update_link = true;
		if (link_info->req_signal_mode == BNXT_SIG_MODE_NRZ &&
		    link_info->req_link_speed != link_info->force_link_speed)
			update_link = true;
		else if (link_info->req_signal_mode == BNXT_SIG_MODE_PAM4 &&
			 link_info->req_link_speed != link_info->force_pam4_link_speed)
			update_link = true;
		if (link_info->req_duplex != link_info->duplex_setting)
			update_link = true;
	} else {
		if (link_info->auto_mode == BNXT_LINK_AUTO_NONE)
			update_link = true;
		if (link_info->advertising != link_info->auto_link_speeds ||
		    link_info->advertising_pam4 != link_info->auto_pam4_link_speeds)
			update_link = true;
	}

	/* The last close may have shutdown the link, so need to call
	 * PHY_CFG to bring it back up.
	 */
	if (!bp->link_info.link_up)
		update_link = true;

	if (!bnxt_eee_config_ok(bp))
		update_eee = true;

	if (update_link)
		rc = bnxt_hwrm_set_link_setting(bp, update_pause, update_eee);
	else if (update_pause)
		rc = bnxt_hwrm_set_pause(bp);
	if (rc) {
		netdev_err(bp->dev, "failed to update phy setting (rc: %x)\n",
			   rc);
		return rc;
	}

	return rc;
}

/* Common routine to pre-map certain register block to different GRC window.
 * A PF has 16 4K windows and a VF has 4 4K windows. However, only 15 windows
 * in PF and 3 windows in VF that can be customized to map in different
 * register blocks.
 */
static void bnxt_preset_reg_win(struct bnxt *bp)
{
	if (BNXT_PF(bp)) {
		/* CAG registers map to GRC window #4 */
		writel(BNXT_CAG_REG_BASE,
		       bp->bar0 + BNXT_GRCPF_REG_WINDOW_BASE_OUT + 12);
	}
}

static int bnxt_init_dflt_ring_mode(struct bnxt *bp);

static int __bnxt_open_nic(struct bnxt *bp, bool irq_re_init, bool link_re_init)
{
	int rc = 0;

	bnxt_preset_reg_win(bp);
	netif_carrier_off(bp->dev);
	if (irq_re_init) {
		/* Reserve rings now if none were reserved at driver probe. */
		rc = bnxt_init_dflt_ring_mode(bp);
		if (rc) {
			netdev_err(bp->dev, "Failed to reserve default rings at open\n");
			return rc;
		}
	}
	rc = bnxt_reserve_rings(bp, irq_re_init);
	if (rc)
		return rc;
	if ((bp->flags & BNXT_FLAG_RFS) &&
	    !(bp->flags & BNXT_FLAG_USING_MSIX)) {
		/* disable RFS if falling back to INTA */
		bp->dev->hw_features &= ~NETIF_F_NTUPLE;
		bp->flags &= ~BNXT_FLAG_RFS;
	}

	rc = bnxt_alloc_mem(bp, irq_re_init);
	if (rc) {
		netdev_err(bp->dev, "bnxt_alloc_mem err: %x\n", rc);
		goto open_err_free_mem;
	}

	if (irq_re_init) {
		bnxt_init_napi(bp);
		rc = bnxt_request_irq(bp);
		if (rc) {
			netdev_err(bp->dev, "bnxt_request_irq err: %x\n", rc);
			goto open_err_irq;
		}
	}

	rc = bnxt_init_nic(bp, irq_re_init);
	if (rc) {
		netdev_err(bp->dev, "bnxt_init_nic err: %x\n", rc);
		goto open_err_irq;
	}

	bnxt_enable_napi(bp);
	bnxt_debug_dev_init(bp);

	if (link_re_init) {
		mutex_lock(&bp->link_lock);
		rc = bnxt_update_phy_setting(bp);
		mutex_unlock(&bp->link_lock);
		if (rc) {
			netdev_warn(bp->dev, "failed to update phy settings\n");
			if (BNXT_SINGLE_PF(bp)) {
				bp->link_info.phy_retry = true;
				bp->link_info.phy_retry_expires =
					jiffies + 5 * HZ;
			}
		}
	}

	if (irq_re_init)
		udp_tunnel_nic_reset_ntf(bp->dev);

	set_bit(BNXT_STATE_OPEN, &bp->state);
	bnxt_enable_int(bp);
	/* Enable TX queues */
	bnxt_tx_enable(bp);
	mod_timer(&bp->timer, jiffies + bp->current_interval);
	/* Poll link status and check for SFP+ module status */
	bnxt_get_port_module_status(bp);

	/* VF-reps may need to be re-opened after the PF is re-opened */
	if (BNXT_PF(bp))
		bnxt_vf_reps_open(bp);
	return 0;

open_err_irq:
	bnxt_del_napi(bp);

open_err_free_mem:
	bnxt_free_skbs(bp);
	bnxt_free_irq(bp);
	bnxt_free_mem(bp, true);
	return rc;
}

/* rtnl_lock held */
int bnxt_open_nic(struct bnxt *bp, bool irq_re_init, bool link_re_init)
{
	int rc = 0;

	if (test_bit(BNXT_STATE_ABORT_ERR, &bp->state))
		rc = -EIO;
	if (!rc)
		rc = __bnxt_open_nic(bp, irq_re_init, link_re_init);
	if (rc) {
		netdev_err(bp->dev, "nic open fail (rc: %x)\n", rc);
		dev_close(bp->dev);
	}
	return rc;
}

/* rtnl_lock held, open the NIC half way by allocating all resources, but
 * NAPI, IRQ, and TX are not enabled.  This is mainly used for offline
 * self tests.
 */
int bnxt_half_open_nic(struct bnxt *bp)
{
	int rc = 0;

	rc = bnxt_alloc_mem(bp, false);
	if (rc) {
		netdev_err(bp->dev, "bnxt_alloc_mem err: %x\n", rc);
		goto half_open_err;
	}
	rc = bnxt_init_nic(bp, false);
	if (rc) {
		netdev_err(bp->dev, "bnxt_init_nic err: %x\n", rc);
		goto half_open_err;
	}
	return 0;

half_open_err:
	bnxt_free_skbs(bp);
	bnxt_free_mem(bp, false);
	dev_close(bp->dev);
	return rc;
}

/* rtnl_lock held, this call can only be made after a previous successful
 * call to bnxt_half_open_nic().
 */
void bnxt_half_close_nic(struct bnxt *bp)
{
	bnxt_hwrm_resource_free(bp, false, false);
	bnxt_free_skbs(bp);
	bnxt_free_mem(bp, false);
}

static void bnxt_reenable_sriov(struct bnxt *bp)
{
	if (BNXT_PF(bp)) {
		struct bnxt_pf_info *pf = &bp->pf;
		int n = pf->active_vfs;

		if (n)
			bnxt_cfg_hw_sriov(bp, &n, true);
	}
}

static int bnxt_open(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc;

	if (test_bit(BNXT_STATE_ABORT_ERR, &bp->state)) {
		netdev_err(bp->dev, "A previous firmware reset did not complete, aborting\n");
		return -ENODEV;
	}

	rc = bnxt_hwrm_if_change(bp, true);
	if (rc)
		return rc;
	rc = __bnxt_open_nic(bp, true, true);
	if (rc) {
		bnxt_hwrm_if_change(bp, false);
	} else {
		if (test_and_clear_bit(BNXT_STATE_FW_RESET_DET, &bp->state)) {
			if (!test_bit(BNXT_STATE_IN_FW_RESET, &bp->state)) {
				bnxt_ulp_start(bp, 0);
				bnxt_reenable_sriov(bp);
			}
		}
		bnxt_hwmon_open(bp);
	}

	return rc;
}

static bool bnxt_drv_busy(struct bnxt *bp)
{
	return (test_bit(BNXT_STATE_IN_SP_TASK, &bp->state) ||
		test_bit(BNXT_STATE_READ_STATS, &bp->state));
}

static void bnxt_get_ring_stats(struct bnxt *bp,
				struct rtnl_link_stats64 *stats);

static void __bnxt_close_nic(struct bnxt *bp, bool irq_re_init,
			     bool link_re_init)
{
	/* Close the VF-reps before closing PF */
	if (BNXT_PF(bp))
		bnxt_vf_reps_close(bp);

	/* Change device state to avoid TX queue wake up's */
	bnxt_tx_disable(bp);

	clear_bit(BNXT_STATE_OPEN, &bp->state);
	smp_mb__after_atomic();
	while (bnxt_drv_busy(bp))
		msleep(20);

	/* Flush rings and and disable interrupts */
	bnxt_shutdown_nic(bp, irq_re_init);

	/* TODO CHIMP_FW: Link/PHY related cleanup if (link_re_init) */

	bnxt_debug_dev_exit(bp);
	bnxt_disable_napi(bp);
	del_timer_sync(&bp->timer);
	bnxt_free_skbs(bp);

	/* Save ring stats before shutdown */
	if (bp->bnapi && irq_re_init)
		bnxt_get_ring_stats(bp, &bp->net_stats_prev);
	if (irq_re_init) {
		bnxt_free_irq(bp);
		bnxt_del_napi(bp);
	}
	bnxt_free_mem(bp, irq_re_init);
}

int bnxt_close_nic(struct bnxt *bp, bool irq_re_init, bool link_re_init)
{
	int rc = 0;

	if (test_bit(BNXT_STATE_IN_FW_RESET, &bp->state)) {
		/* If we get here, it means firmware reset is in progress
		 * while we are trying to close.  We can safely proceed with
		 * the close because we are holding rtnl_lock().  Some firmware
		 * messages may fail as we proceed to close.  We set the
		 * ABORT_ERR flag here so that the FW reset thread will later
		 * abort when it gets the rtnl_lock() and sees the flag.
		 */
		netdev_warn(bp->dev, "FW reset in progress during close, FW reset will be aborted\n");
		set_bit(BNXT_STATE_ABORT_ERR, &bp->state);
	}

#ifdef CONFIG_BNXT_SRIOV
	if (bp->sriov_cfg) {
		rc = wait_event_interruptible_timeout(bp->sriov_cfg_wait,
						      !bp->sriov_cfg,
						      BNXT_SRIOV_CFG_WAIT_TMO);
		if (rc)
			netdev_warn(bp->dev, "timeout waiting for SRIOV config operation to complete!\n");
	}
#endif
	__bnxt_close_nic(bp, irq_re_init, link_re_init);
	return rc;
}

static int bnxt_close(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);

	bnxt_hwmon_close(bp);
	bnxt_close_nic(bp, true, true);
	bnxt_hwrm_shutdown_link(bp);
	bnxt_hwrm_if_change(bp, false);
	return 0;
}

static int bnxt_hwrm_port_phy_read(struct bnxt *bp, u16 phy_addr, u16 reg,
				   u16 *val)
{
	struct hwrm_port_phy_mdio_read_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_port_phy_mdio_read_input req = {0};
	int rc;

	if (bp->hwrm_spec_code < 0x10a00)
		return -EOPNOTSUPP;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_PHY_MDIO_READ, -1, -1);
	req.port_id = cpu_to_le16(bp->pf.port_id);
	req.phy_addr = phy_addr;
	req.reg_addr = cpu_to_le16(reg & 0x1f);
	if (mdio_phy_id_is_c45(phy_addr)) {
		req.cl45_mdio = 1;
		req.phy_addr = mdio_phy_id_prtad(phy_addr);
		req.dev_addr = mdio_phy_id_devad(phy_addr);
		req.reg_addr = cpu_to_le16(reg);
	}

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		*val = le16_to_cpu(resp->reg_data);
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_port_phy_write(struct bnxt *bp, u16 phy_addr, u16 reg,
				    u16 val)
{
	struct hwrm_port_phy_mdio_write_input req = {0};

	if (bp->hwrm_spec_code < 0x10a00)
		return -EOPNOTSUPP;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_PHY_MDIO_WRITE, -1, -1);
	req.port_id = cpu_to_le16(bp->pf.port_id);
	req.phy_addr = phy_addr;
	req.reg_addr = cpu_to_le16(reg & 0x1f);
	if (mdio_phy_id_is_c45(phy_addr)) {
		req.cl45_mdio = 1;
		req.phy_addr = mdio_phy_id_prtad(phy_addr);
		req.dev_addr = mdio_phy_id_devad(phy_addr);
		req.reg_addr = cpu_to_le16(reg);
	}
	req.reg_data = cpu_to_le16(val);

	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

/* rtnl_lock held */
static int bnxt_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mii_ioctl_data *mdio = if_mii(ifr);
	struct bnxt *bp = netdev_priv(dev);
	int rc;

	switch (cmd) {
	case SIOCGMIIPHY:
		mdio->phy_id = bp->link_info.phy_addr;

		fallthrough;
	case SIOCGMIIREG: {
		u16 mii_regval = 0;

		if (!netif_running(dev))
			return -EAGAIN;

		rc = bnxt_hwrm_port_phy_read(bp, mdio->phy_id, mdio->reg_num,
					     &mii_regval);
		mdio->val_out = mii_regval;
		return rc;
	}

	case SIOCSMIIREG:
		if (!netif_running(dev))
			return -EAGAIN;

		return bnxt_hwrm_port_phy_write(bp, mdio->phy_id, mdio->reg_num,
						mdio->val_in);

	default:
		/* do nothing */
		break;
	}
	return -EOPNOTSUPP;
}

static void bnxt_get_ring_stats(struct bnxt *bp,
				struct rtnl_link_stats64 *stats)
{
	int i;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
		u64 *sw = cpr->stats.sw_stats;

		stats->rx_packets += BNXT_GET_RING_STATS64(sw, rx_ucast_pkts);
		stats->rx_packets += BNXT_GET_RING_STATS64(sw, rx_mcast_pkts);
		stats->rx_packets += BNXT_GET_RING_STATS64(sw, rx_bcast_pkts);

		stats->tx_packets += BNXT_GET_RING_STATS64(sw, tx_ucast_pkts);
		stats->tx_packets += BNXT_GET_RING_STATS64(sw, tx_mcast_pkts);
		stats->tx_packets += BNXT_GET_RING_STATS64(sw, tx_bcast_pkts);

		stats->rx_bytes += BNXT_GET_RING_STATS64(sw, rx_ucast_bytes);
		stats->rx_bytes += BNXT_GET_RING_STATS64(sw, rx_mcast_bytes);
		stats->rx_bytes += BNXT_GET_RING_STATS64(sw, rx_bcast_bytes);

		stats->tx_bytes += BNXT_GET_RING_STATS64(sw, tx_ucast_bytes);
		stats->tx_bytes += BNXT_GET_RING_STATS64(sw, tx_mcast_bytes);
		stats->tx_bytes += BNXT_GET_RING_STATS64(sw, tx_bcast_bytes);

		stats->rx_missed_errors +=
			BNXT_GET_RING_STATS64(sw, rx_discard_pkts);

		stats->multicast += BNXT_GET_RING_STATS64(sw, rx_mcast_pkts);

		stats->tx_dropped += BNXT_GET_RING_STATS64(sw, tx_error_pkts);
	}
}

static void bnxt_add_prev_stats(struct bnxt *bp,
				struct rtnl_link_stats64 *stats)
{
	struct rtnl_link_stats64 *prev_stats = &bp->net_stats_prev;

	stats->rx_packets += prev_stats->rx_packets;
	stats->tx_packets += prev_stats->tx_packets;
	stats->rx_bytes += prev_stats->rx_bytes;
	stats->tx_bytes += prev_stats->tx_bytes;
	stats->rx_missed_errors += prev_stats->rx_missed_errors;
	stats->multicast += prev_stats->multicast;
	stats->tx_dropped += prev_stats->tx_dropped;
}

static void
bnxt_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct bnxt *bp = netdev_priv(dev);

	set_bit(BNXT_STATE_READ_STATS, &bp->state);
	/* Make sure bnxt_close_nic() sees that we are reading stats before
	 * we check the BNXT_STATE_OPEN flag.
	 */
	smp_mb__after_atomic();
	if (!test_bit(BNXT_STATE_OPEN, &bp->state)) {
		clear_bit(BNXT_STATE_READ_STATS, &bp->state);
		*stats = bp->net_stats_prev;
		return;
	}

	bnxt_get_ring_stats(bp, stats);
	bnxt_add_prev_stats(bp, stats);

	if (bp->flags & BNXT_FLAG_PORT_STATS) {
		u64 *rx = bp->port_stats.sw_stats;
		u64 *tx = bp->port_stats.sw_stats +
			  BNXT_TX_PORT_STATS_BYTE_OFFSET / 8;

		stats->rx_crc_errors =
			BNXT_GET_RX_PORT_STATS64(rx, rx_fcs_err_frames);
		stats->rx_frame_errors =
			BNXT_GET_RX_PORT_STATS64(rx, rx_align_err_frames);
		stats->rx_length_errors =
			BNXT_GET_RX_PORT_STATS64(rx, rx_undrsz_frames) +
			BNXT_GET_RX_PORT_STATS64(rx, rx_ovrsz_frames) +
			BNXT_GET_RX_PORT_STATS64(rx, rx_runt_frames);
		stats->rx_errors =
			BNXT_GET_RX_PORT_STATS64(rx, rx_false_carrier_frames) +
			BNXT_GET_RX_PORT_STATS64(rx, rx_jbr_frames);
		stats->collisions =
			BNXT_GET_TX_PORT_STATS64(tx, tx_total_collisions);
		stats->tx_fifo_errors =
			BNXT_GET_TX_PORT_STATS64(tx, tx_fifo_underruns);
		stats->tx_errors = BNXT_GET_TX_PORT_STATS64(tx, tx_err);
	}
	clear_bit(BNXT_STATE_READ_STATS, &bp->state);
}

static bool bnxt_mc_list_updated(struct bnxt *bp, u32 *rx_mask)
{
	struct net_device *dev = bp->dev;
	struct bnxt_vnic_info *vnic = &bp->vnic_info[0];
	struct netdev_hw_addr *ha;
	u8 *haddr;
	int mc_count = 0;
	bool update = false;
	int off = 0;

	netdev_for_each_mc_addr(ha, dev) {
		if (mc_count >= BNXT_MAX_MC_ADDRS) {
			*rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST;
			vnic->mc_list_count = 0;
			return false;
		}
		haddr = ha->addr;
		if (!ether_addr_equal(haddr, vnic->mc_list + off)) {
			memcpy(vnic->mc_list + off, haddr, ETH_ALEN);
			update = true;
		}
		off += ETH_ALEN;
		mc_count++;
	}
	if (mc_count)
		*rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_MCAST;

	if (mc_count != vnic->mc_list_count) {
		vnic->mc_list_count = mc_count;
		update = true;
	}
	return update;
}

static bool bnxt_uc_list_updated(struct bnxt *bp)
{
	struct net_device *dev = bp->dev;
	struct bnxt_vnic_info *vnic = &bp->vnic_info[0];
	struct netdev_hw_addr *ha;
	int off = 0;

	if (netdev_uc_count(dev) != (vnic->uc_filter_count - 1))
		return true;

	netdev_for_each_uc_addr(ha, dev) {
		if (!ether_addr_equal(ha->addr, vnic->uc_list + off))
			return true;

		off += ETH_ALEN;
	}
	return false;
}

static void bnxt_set_rx_mode(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_vnic_info *vnic;
	bool mc_update = false;
	bool uc_update;
	u32 mask;

	if (!test_bit(BNXT_STATE_OPEN, &bp->state))
		return;

	vnic = &bp->vnic_info[0];
	mask = vnic->rx_mask;
	mask &= ~(CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS |
		  CFA_L2_SET_RX_MASK_REQ_MASK_MCAST |
		  CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST |
		  CFA_L2_SET_RX_MASK_REQ_MASK_BCAST);

	if ((dev->flags & IFF_PROMISC) && bnxt_promisc_ok(bp))
		mask |= CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS;

	uc_update = bnxt_uc_list_updated(bp);

	if (dev->flags & IFF_BROADCAST)
		mask |= CFA_L2_SET_RX_MASK_REQ_MASK_BCAST;
	if (dev->flags & IFF_ALLMULTI) {
		mask |= CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST;
		vnic->mc_list_count = 0;
	} else {
		mc_update = bnxt_mc_list_updated(bp, &mask);
	}

	if (mask != vnic->rx_mask || uc_update || mc_update) {
		vnic->rx_mask = mask;

		set_bit(BNXT_RX_MASK_SP_EVENT, &bp->sp_event);
		bnxt_queue_sp_work(bp);
	}
}

static int bnxt_cfg_rx_mode(struct bnxt *bp)
{
	struct net_device *dev = bp->dev;
	struct bnxt_vnic_info *vnic = &bp->vnic_info[0];
	struct netdev_hw_addr *ha;
	int i, off = 0, rc;
	bool uc_update;

	netif_addr_lock_bh(dev);
	uc_update = bnxt_uc_list_updated(bp);
	netif_addr_unlock_bh(dev);

	if (!uc_update)
		goto skip_uc;

	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 1; i < vnic->uc_filter_count; i++) {
		struct hwrm_cfa_l2_filter_free_input req = {0};

		bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_L2_FILTER_FREE, -1,
				       -1);

		req.l2_filter_id = vnic->fw_l2_filter_id[i];

		rc = _hwrm_send_message(bp, &req, sizeof(req),
					HWRM_CMD_TIMEOUT);
	}
	mutex_unlock(&bp->hwrm_cmd_lock);

	vnic->uc_filter_count = 1;

	netif_addr_lock_bh(dev);
	if (netdev_uc_count(dev) > (BNXT_MAX_UC_ADDRS - 1)) {
		vnic->rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS;
	} else {
		netdev_for_each_uc_addr(ha, dev) {
			memcpy(vnic->uc_list + off, ha->addr, ETH_ALEN);
			off += ETH_ALEN;
			vnic->uc_filter_count++;
		}
	}
	netif_addr_unlock_bh(dev);

	for (i = 1, off = 0; i < vnic->uc_filter_count; i++, off += ETH_ALEN) {
		rc = bnxt_hwrm_set_vnic_filter(bp, 0, i, vnic->uc_list + off);
		if (rc) {
			netdev_err(bp->dev, "HWRM vnic filter failure rc: %x\n",
				   rc);
			vnic->uc_filter_count = i;
			return rc;
		}
	}

skip_uc:
	rc = bnxt_hwrm_cfa_l2_set_rx_mask(bp, 0);
	if (rc && vnic->mc_list_count) {
		netdev_info(bp->dev, "Failed setting MC filters rc: %d, turning on ALL_MCAST mode\n",
			    rc);
		vnic->rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST;
		vnic->mc_list_count = 0;
		rc = bnxt_hwrm_cfa_l2_set_rx_mask(bp, 0);
	}
	if (rc)
		netdev_err(bp->dev, "HWRM cfa l2 rx mask failure rc: %d\n",
			   rc);

	return rc;
}

static bool bnxt_can_reserve_rings(struct bnxt *bp)
{
#ifdef CONFIG_BNXT_SRIOV
	if (BNXT_NEW_RM(bp) && BNXT_VF(bp)) {
		struct bnxt_hw_resc *hw_resc = &bp->hw_resc;

		/* No minimum rings were provisioned by the PF.  Don't
		 * reserve rings by default when device is down.
		 */
		if (hw_resc->min_tx_rings || hw_resc->resv_tx_rings)
			return true;

		if (!netif_running(bp->dev))
			return false;
	}
#endif
	return true;
}

/* If the chip and firmware supports RFS */
static bool bnxt_rfs_supported(struct bnxt *bp)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		if (bp->fw_cap & BNXT_FW_CAP_CFA_RFS_RING_TBL_IDX_V2)
			return true;
		return false;
	}
	if (BNXT_PF(bp) && !BNXT_CHIP_TYPE_NITRO_A0(bp))
		return true;
	if (bp->flags & BNXT_FLAG_NEW_RSS_CAP)
		return true;
	return false;
}

/* If runtime conditions support RFS */
static bool bnxt_rfs_capable(struct bnxt *bp)
{
#ifdef CONFIG_RFS_ACCEL
	int vnics, max_vnics, max_rss_ctxs;

	if (bp->flags & BNXT_FLAG_CHIP_P5)
		return bnxt_rfs_supported(bp);
	if (!(bp->flags & BNXT_FLAG_MSIX_CAP) || !bnxt_can_reserve_rings(bp))
		return false;

	vnics = 1 + bp->rx_nr_rings;
	max_vnics = bnxt_get_max_func_vnics(bp);
	max_rss_ctxs = bnxt_get_max_func_rss_ctxs(bp);

	/* RSS contexts not a limiting factor */
	if (bp->flags & BNXT_FLAG_NEW_RSS_CAP)
		max_rss_ctxs = max_vnics;
	if (vnics > max_vnics || vnics > max_rss_ctxs) {
		if (bp->rx_nr_rings > 1)
			netdev_warn(bp->dev,
				    "Not enough resources to support NTUPLE filters, enough resources for up to %d rx rings\n",
				    min(max_rss_ctxs - 1, max_vnics - 1));
		return false;
	}

	if (!BNXT_NEW_RM(bp))
		return true;

	if (vnics == bp->hw_resc.resv_vnics)
		return true;

	bnxt_hwrm_reserve_rings(bp, 0, 0, 0, 0, 0, vnics);
	if (vnics <= bp->hw_resc.resv_vnics)
		return true;

	netdev_warn(bp->dev, "Unable to reserve resources to support NTUPLE filters.\n");
	bnxt_hwrm_reserve_rings(bp, 0, 0, 0, 0, 0, 1);
	return false;
#else
	return false;
#endif
}

static netdev_features_t bnxt_fix_features(struct net_device *dev,
					   netdev_features_t features)
{
	struct bnxt *bp = netdev_priv(dev);
	netdev_features_t vlan_features;

	if ((features & NETIF_F_NTUPLE) && !bnxt_rfs_capable(bp))
		features &= ~NETIF_F_NTUPLE;

	if (bp->flags & BNXT_FLAG_NO_AGG_RINGS)
		features &= ~(NETIF_F_LRO | NETIF_F_GRO_HW);

	if (!(features & NETIF_F_GRO))
		features &= ~NETIF_F_GRO_HW;

	if (features & NETIF_F_GRO_HW)
		features &= ~NETIF_F_LRO;

	/* Both CTAG and STAG VLAN accelaration on the RX side have to be
	 * turned on or off together.
	 */
	vlan_features = features & BNXT_HW_FEATURE_VLAN_ALL_RX;
	if (vlan_features != BNXT_HW_FEATURE_VLAN_ALL_RX) {
		if (dev->features & BNXT_HW_FEATURE_VLAN_ALL_RX)
			features &= ~BNXT_HW_FEATURE_VLAN_ALL_RX;
		else if (vlan_features)
			features |= BNXT_HW_FEATURE_VLAN_ALL_RX;
	}
#ifdef CONFIG_BNXT_SRIOV
	if (BNXT_VF(bp) && bp->vf.vlan)
		features &= ~BNXT_HW_FEATURE_VLAN_ALL_RX;
#endif
	return features;
}

static int bnxt_set_features(struct net_device *dev, netdev_features_t features)
{
	struct bnxt *bp = netdev_priv(dev);
	u32 flags = bp->flags;
	u32 changes;
	int rc = 0;
	bool re_init = false;
	bool update_tpa = false;

	flags &= ~BNXT_FLAG_ALL_CONFIG_FEATS;
	if (features & NETIF_F_GRO_HW)
		flags |= BNXT_FLAG_GRO;
	else if (features & NETIF_F_LRO)
		flags |= BNXT_FLAG_LRO;

	if (bp->flags & BNXT_FLAG_NO_AGG_RINGS)
		flags &= ~BNXT_FLAG_TPA;

	if (features & BNXT_HW_FEATURE_VLAN_ALL_RX)
		flags |= BNXT_FLAG_STRIP_VLAN;

	if (features & NETIF_F_NTUPLE)
		flags |= BNXT_FLAG_RFS;

	changes = flags ^ bp->flags;
	if (changes & BNXT_FLAG_TPA) {
		update_tpa = true;
		if ((bp->flags & BNXT_FLAG_TPA) == 0 ||
		    (flags & BNXT_FLAG_TPA) == 0 ||
		    (bp->flags & BNXT_FLAG_CHIP_P5))
			re_init = true;
	}

	if (changes & ~BNXT_FLAG_TPA)
		re_init = true;

	if (flags != bp->flags) {
		u32 old_flags = bp->flags;

		if (!test_bit(BNXT_STATE_OPEN, &bp->state)) {
			bp->flags = flags;
			if (update_tpa)
				bnxt_set_ring_params(bp);
			return rc;
		}

		if (re_init) {
			bnxt_close_nic(bp, false, false);
			bp->flags = flags;
			if (update_tpa)
				bnxt_set_ring_params(bp);

			return bnxt_open_nic(bp, false, false);
		}
		if (update_tpa) {
			bp->flags = flags;
			rc = bnxt_set_tpa(bp,
					  (flags & BNXT_FLAG_TPA) ?
					  true : false);
			if (rc)
				bp->flags = old_flags;
		}
	}
	return rc;
}

int bnxt_dbg_hwrm_rd_reg(struct bnxt *bp, u32 reg_off, u16 num_words,
			 u32 *reg_buf)
{
	struct hwrm_dbg_read_direct_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_dbg_read_direct_input req = {0};
	__le32 *dbg_reg_buf;
	dma_addr_t mapping;
	int rc, i;

	dbg_reg_buf = dma_alloc_coherent(&bp->pdev->dev, num_words * 4,
					 &mapping, GFP_KERNEL);
	if (!dbg_reg_buf)
		return -ENOMEM;
	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_DBG_READ_DIRECT, -1, -1);
	req.host_dest_addr = cpu_to_le64(mapping);
	req.read_addr = cpu_to_le32(reg_off + CHIMP_REG_VIEW_ADDR);
	req.read_len32 = cpu_to_le32(num_words);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc || resp->error_code) {
		rc = -EIO;
		goto dbg_rd_reg_exit;
	}
	for (i = 0; i < num_words; i++)
		reg_buf[i] = le32_to_cpu(dbg_reg_buf[i]);

dbg_rd_reg_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	dma_free_coherent(&bp->pdev->dev, num_words * 4, dbg_reg_buf, mapping);
	return rc;
}

static int bnxt_dbg_hwrm_ring_info_get(struct bnxt *bp, u8 ring_type,
				       u32 ring_id, u32 *prod, u32 *cons)
{
	struct hwrm_dbg_ring_info_get_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_dbg_ring_info_get_input req = {0};
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_DBG_RING_INFO_GET, -1, -1);
	req.ring_type = ring_type;
	req.fw_ring_id = cpu_to_le32(ring_id);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		*prod = le32_to_cpu(resp->producer_index);
		*cons = le32_to_cpu(resp->consumer_index);
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static void bnxt_dump_tx_sw_state(struct bnxt_napi *bnapi)
{
	struct bnxt_tx_ring_info *txr = bnapi->tx_ring;
	int i = bnapi->index;

	if (!txr)
		return;

	netdev_info(bnapi->bp->dev, "[%d]: tx{fw_ring: %d prod: %x cons: %x}\n",
		    i, txr->tx_ring_struct.fw_ring_id, txr->tx_prod,
		    txr->tx_cons);
}

static void bnxt_dump_rx_sw_state(struct bnxt_napi *bnapi)
{
	struct bnxt_rx_ring_info *rxr = bnapi->rx_ring;
	int i = bnapi->index;

	if (!rxr)
		return;

	netdev_info(bnapi->bp->dev, "[%d]: rx{fw_ring: %d prod: %x} rx_agg{fw_ring: %d agg_prod: %x sw_agg_prod: %x}\n",
		    i, rxr->rx_ring_struct.fw_ring_id, rxr->rx_prod,
		    rxr->rx_agg_ring_struct.fw_ring_id, rxr->rx_agg_prod,
		    rxr->rx_sw_agg_prod);
}

static void bnxt_dump_cp_sw_state(struct bnxt_napi *bnapi)
{
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	int i = bnapi->index;

	netdev_info(bnapi->bp->dev, "[%d]: cp{fw_ring: %d raw_cons: %x}\n",
		    i, cpr->cp_ring_struct.fw_ring_id, cpr->cp_raw_cons);
}

static void bnxt_dbg_dump_states(struct bnxt *bp)
{
	int i;
	struct bnxt_napi *bnapi;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		bnapi = bp->bnapi[i];
		if (netif_msg_drv(bp)) {
			bnxt_dump_tx_sw_state(bnapi);
			bnxt_dump_rx_sw_state(bnapi);
			bnxt_dump_cp_sw_state(bnapi);
		}
	}
}

static int bnxt_hwrm_rx_ring_reset(struct bnxt *bp, int ring_nr)
{
	struct bnxt_rx_ring_info *rxr = &bp->rx_ring[ring_nr];
	struct hwrm_ring_reset_input req = {0};
	struct bnxt_napi *bnapi = rxr->bnapi;
	struct bnxt_cp_ring_info *cpr;
	u16 cp_ring_id;

	cpr = &bnapi->cp_ring;
	cp_ring_id = cpr->cp_ring_struct.fw_ring_id;
	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_RING_RESET, cp_ring_id, -1);
	req.ring_type = RING_RESET_REQ_RING_TYPE_RX_RING_GRP;
	req.ring_id = cpu_to_le16(bp->grp_info[bnapi->index].fw_grp_id);
	return hwrm_send_message_silent(bp, &req, sizeof(req),
					HWRM_CMD_TIMEOUT);
}

static void bnxt_reset_task(struct bnxt *bp, bool silent)
{
	if (!silent)
		bnxt_dbg_dump_states(bp);
	if (netif_running(bp->dev)) {
		int rc;

		if (silent) {
			bnxt_close_nic(bp, false, false);
			bnxt_open_nic(bp, false, false);
		} else {
			bnxt_ulp_stop(bp);
			bnxt_close_nic(bp, true, false);
			rc = bnxt_open_nic(bp, true, false);
			bnxt_ulp_start(bp, rc);
		}
	}
}

static void bnxt_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct bnxt *bp = netdev_priv(dev);

	netdev_err(bp->dev,  "TX timeout detected, starting reset task!\n");
	set_bit(BNXT_RESET_TASK_SP_EVENT, &bp->sp_event);
	bnxt_queue_sp_work(bp);
}

static void bnxt_fw_health_check(struct bnxt *bp)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	u32 val;

	if (!fw_health->enabled || test_bit(BNXT_STATE_IN_FW_RESET, &bp->state))
		return;

	if (fw_health->tmr_counter) {
		fw_health->tmr_counter--;
		return;
	}

	val = bnxt_fw_health_readl(bp, BNXT_FW_HEARTBEAT_REG);
	if (val == fw_health->last_fw_heartbeat)
		goto fw_reset;

	fw_health->last_fw_heartbeat = val;

	val = bnxt_fw_health_readl(bp, BNXT_FW_RESET_CNT_REG);
	if (val != fw_health->last_fw_reset_cnt)
		goto fw_reset;

	fw_health->tmr_counter = fw_health->tmr_multiplier;
	return;

fw_reset:
	set_bit(BNXT_FW_EXCEPTION_SP_EVENT, &bp->sp_event);
	bnxt_queue_sp_work(bp);
}

static void bnxt_timer(struct timer_list *t)
{
	struct bnxt *bp = from_timer(bp, t, timer);
	struct net_device *dev = bp->dev;

	if (!netif_running(dev) || !test_bit(BNXT_STATE_OPEN, &bp->state))
		return;

	if (atomic_read(&bp->intr_sem) != 0)
		goto bnxt_restart_timer;

	if (bp->fw_cap & BNXT_FW_CAP_ERROR_RECOVERY)
		bnxt_fw_health_check(bp);

	if (bp->link_info.link_up && bp->stats_coal_ticks) {
		set_bit(BNXT_PERIODIC_STATS_SP_EVENT, &bp->sp_event);
		bnxt_queue_sp_work(bp);
	}

	if (bnxt_tc_flower_enabled(bp)) {
		set_bit(BNXT_FLOW_STATS_SP_EVENT, &bp->sp_event);
		bnxt_queue_sp_work(bp);
	}

#ifdef CONFIG_RFS_ACCEL
	if ((bp->flags & BNXT_FLAG_RFS) && bp->ntp_fltr_count) {
		set_bit(BNXT_RX_NTP_FLTR_SP_EVENT, &bp->sp_event);
		bnxt_queue_sp_work(bp);
	}
#endif /*CONFIG_RFS_ACCEL*/

	if (bp->link_info.phy_retry) {
		if (time_after(jiffies, bp->link_info.phy_retry_expires)) {
			bp->link_info.phy_retry = false;
			netdev_warn(bp->dev, "failed to update phy settings after maximum retries.\n");
		} else {
			set_bit(BNXT_UPDATE_PHY_SP_EVENT, &bp->sp_event);
			bnxt_queue_sp_work(bp);
		}
	}

	if ((bp->flags & BNXT_FLAG_CHIP_P5) && !bp->chip_rev &&
	    netif_carrier_ok(dev)) {
		set_bit(BNXT_RING_COAL_NOW_SP_EVENT, &bp->sp_event);
		bnxt_queue_sp_work(bp);
	}
bnxt_restart_timer:
	mod_timer(&bp->timer, jiffies + bp->current_interval);
}

static void bnxt_rtnl_lock_sp(struct bnxt *bp)
{
	/* We are called from bnxt_sp_task which has BNXT_STATE_IN_SP_TASK
	 * set.  If the device is being closed, bnxt_close() may be holding
	 * rtnl() and waiting for BNXT_STATE_IN_SP_TASK to clear.  So we
	 * must clear BNXT_STATE_IN_SP_TASK before holding rtnl().
	 */
	clear_bit(BNXT_STATE_IN_SP_TASK, &bp->state);
	rtnl_lock();
}

static void bnxt_rtnl_unlock_sp(struct bnxt *bp)
{
	set_bit(BNXT_STATE_IN_SP_TASK, &bp->state);
	rtnl_unlock();
}

/* Only called from bnxt_sp_task() */
static void bnxt_reset(struct bnxt *bp, bool silent)
{
	bnxt_rtnl_lock_sp(bp);
	if (test_bit(BNXT_STATE_OPEN, &bp->state))
		bnxt_reset_task(bp, silent);
	bnxt_rtnl_unlock_sp(bp);
}

/* Only called from bnxt_sp_task() */
static void bnxt_rx_ring_reset(struct bnxt *bp)
{
	int i;

	bnxt_rtnl_lock_sp(bp);
	if (!test_bit(BNXT_STATE_OPEN, &bp->state)) {
		bnxt_rtnl_unlock_sp(bp);
		return;
	}
	/* Disable and flush TPA before resetting the RX ring */
	if (bp->flags & BNXT_FLAG_TPA)
		bnxt_set_tpa(bp, false);
	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];
		struct bnxt_cp_ring_info *cpr;
		int rc;

		if (!rxr->bnapi->in_reset)
			continue;

		rc = bnxt_hwrm_rx_ring_reset(bp, i);
		if (rc) {
			if (rc == -EINVAL || rc == -EOPNOTSUPP)
				netdev_info_once(bp->dev, "RX ring reset not supported by firmware, falling back to global reset\n");
			else
				netdev_warn(bp->dev, "RX ring reset failed, rc = %d, falling back to global reset\n",
					    rc);
			bnxt_reset_task(bp, true);
			break;
		}
		bnxt_free_one_rx_ring_skbs(bp, i);
		rxr->rx_prod = 0;
		rxr->rx_agg_prod = 0;
		rxr->rx_sw_agg_prod = 0;
		rxr->rx_next_cons = 0;
		rxr->bnapi->in_reset = false;
		bnxt_alloc_one_rx_ring(bp, i);
		cpr = &rxr->bnapi->cp_ring;
		cpr->sw_stats.rx.rx_resets++;
		if (bp->flags & BNXT_FLAG_AGG_RINGS)
			bnxt_db_write(bp, &rxr->rx_agg_db, rxr->rx_agg_prod);
		bnxt_db_write(bp, &rxr->rx_db, rxr->rx_prod);
	}
	if (bp->flags & BNXT_FLAG_TPA)
		bnxt_set_tpa(bp, true);
	bnxt_rtnl_unlock_sp(bp);
}

static void bnxt_fw_reset_close(struct bnxt *bp)
{
	bnxt_ulp_stop(bp);
	/* When firmware is fatal state, disable PCI device to prevent
	 * any potential bad DMAs before freeing kernel memory.
	 */
	if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))
		pci_disable_device(bp->pdev);
	__bnxt_close_nic(bp, true, false);
	bnxt_clear_int_mode(bp);
	bnxt_hwrm_func_drv_unrgtr(bp);
	if (pci_is_enabled(bp->pdev))
		pci_disable_device(bp->pdev);
	bnxt_free_ctx_mem(bp);
	kfree(bp->ctx);
	bp->ctx = NULL;
}

static bool is_bnxt_fw_ok(struct bnxt *bp)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	bool no_heartbeat = false, has_reset = false;
	u32 val;

	val = bnxt_fw_health_readl(bp, BNXT_FW_HEARTBEAT_REG);
	if (val == fw_health->last_fw_heartbeat)
		no_heartbeat = true;

	val = bnxt_fw_health_readl(bp, BNXT_FW_RESET_CNT_REG);
	if (val != fw_health->last_fw_reset_cnt)
		has_reset = true;

	if (!no_heartbeat && has_reset)
		return true;

	return false;
}

/* rtnl_lock is acquired before calling this function */
static void bnxt_force_fw_reset(struct bnxt *bp)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	u32 wait_dsecs;

	if (!test_bit(BNXT_STATE_OPEN, &bp->state) ||
	    test_bit(BNXT_STATE_IN_FW_RESET, &bp->state))
		return;

	set_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
	bnxt_fw_reset_close(bp);
	wait_dsecs = fw_health->master_func_wait_dsecs;
	if (fw_health->master) {
		if (fw_health->flags & ERROR_RECOVERY_QCFG_RESP_FLAGS_CO_CPU)
			wait_dsecs = 0;
		bp->fw_reset_state = BNXT_FW_RESET_STATE_RESET_FW;
	} else {
		bp->fw_reset_timestamp = jiffies + wait_dsecs * HZ / 10;
		wait_dsecs = fw_health->normal_func_wait_dsecs;
		bp->fw_reset_state = BNXT_FW_RESET_STATE_ENABLE_DEV;
	}

	bp->fw_reset_min_dsecs = fw_health->post_reset_wait_dsecs;
	bp->fw_reset_max_dsecs = fw_health->post_reset_max_wait_dsecs;
	bnxt_queue_fw_reset_work(bp, wait_dsecs * HZ / 10);
}

void bnxt_fw_exception(struct bnxt *bp)
{
	netdev_warn(bp->dev, "Detected firmware fatal condition, initiating reset\n");
	set_bit(BNXT_STATE_FW_FATAL_COND, &bp->state);
	bnxt_rtnl_lock_sp(bp);
	bnxt_force_fw_reset(bp);
	bnxt_rtnl_unlock_sp(bp);
}

/* Returns the number of registered VFs, or 1 if VF configuration is pending, or
 * < 0 on error.
 */
static int bnxt_get_registered_vfs(struct bnxt *bp)
{
#ifdef CONFIG_BNXT_SRIOV
	int rc;

	if (!BNXT_PF(bp))
		return 0;

	rc = bnxt_hwrm_func_qcfg(bp);
	if (rc) {
		netdev_err(bp->dev, "func_qcfg cmd failed, rc = %d\n", rc);
		return rc;
	}
	if (bp->pf.registered_vfs)
		return bp->pf.registered_vfs;
	if (bp->sriov_cfg)
		return 1;
#endif
	return 0;
}

void bnxt_fw_reset(struct bnxt *bp)
{
	bnxt_rtnl_lock_sp(bp);
	if (test_bit(BNXT_STATE_OPEN, &bp->state) &&
	    !test_bit(BNXT_STATE_IN_FW_RESET, &bp->state)) {
		int n = 0, tmo;

		set_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
		if (bp->pf.active_vfs &&
		    !test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))
			n = bnxt_get_registered_vfs(bp);
		if (n < 0) {
			netdev_err(bp->dev, "Firmware reset aborted, rc = %d\n",
				   n);
			clear_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
			dev_close(bp->dev);
			goto fw_reset_exit;
		} else if (n > 0) {
			u16 vf_tmo_dsecs = n * 10;

			if (bp->fw_reset_max_dsecs < vf_tmo_dsecs)
				bp->fw_reset_max_dsecs = vf_tmo_dsecs;
			bp->fw_reset_state =
				BNXT_FW_RESET_STATE_POLL_VF;
			bnxt_queue_fw_reset_work(bp, HZ / 10);
			goto fw_reset_exit;
		}
		bnxt_fw_reset_close(bp);
		if (bp->fw_cap & BNXT_FW_CAP_ERR_RECOVER_RELOAD) {
			bp->fw_reset_state = BNXT_FW_RESET_STATE_POLL_FW_DOWN;
			tmo = HZ / 10;
		} else {
			bp->fw_reset_state = BNXT_FW_RESET_STATE_ENABLE_DEV;
			tmo = bp->fw_reset_min_dsecs * HZ / 10;
		}
		bnxt_queue_fw_reset_work(bp, tmo);
	}
fw_reset_exit:
	bnxt_rtnl_unlock_sp(bp);
}

static void bnxt_chk_missed_irq(struct bnxt *bp)
{
	int i;

	if (!(bp->flags & BNXT_FLAG_CHIP_P5))
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr;
		u32 fw_ring_id;
		int j;

		if (!bnapi)
			continue;

		cpr = &bnapi->cp_ring;
		for (j = 0; j < 2; j++) {
			struct bnxt_cp_ring_info *cpr2 = cpr->cp_ring_arr[j];
			u32 val[2];

			if (!cpr2 || cpr2->has_more_work ||
			    !bnxt_has_work(bp, cpr2))
				continue;

			if (cpr2->cp_raw_cons != cpr2->last_cp_raw_cons) {
				cpr2->last_cp_raw_cons = cpr2->cp_raw_cons;
				continue;
			}
			fw_ring_id = cpr2->cp_ring_struct.fw_ring_id;
			bnxt_dbg_hwrm_ring_info_get(bp,
				DBG_RING_INFO_GET_REQ_RING_TYPE_L2_CMPL,
				fw_ring_id, &val[0], &val[1]);
			cpr->sw_stats.cmn.missed_irqs++;
		}
	}
}

static void bnxt_cfg_ntp_filters(struct bnxt *);

static void bnxt_init_ethtool_link_settings(struct bnxt *bp)
{
	struct bnxt_link_info *link_info = &bp->link_info;

	if (BNXT_AUTO_MODE(link_info->auto_mode)) {
		link_info->autoneg = BNXT_AUTONEG_SPEED;
		if (bp->hwrm_spec_code >= 0x10201) {
			if (link_info->auto_pause_setting &
			    PORT_PHY_CFG_REQ_AUTO_PAUSE_AUTONEG_PAUSE)
				link_info->autoneg |= BNXT_AUTONEG_FLOW_CTRL;
		} else {
			link_info->autoneg |= BNXT_AUTONEG_FLOW_CTRL;
		}
		link_info->advertising = link_info->auto_link_speeds;
		link_info->advertising_pam4 = link_info->auto_pam4_link_speeds;
	} else {
		link_info->req_link_speed = link_info->force_link_speed;
		link_info->req_signal_mode = BNXT_SIG_MODE_NRZ;
		if (link_info->force_pam4_link_speed) {
			link_info->req_link_speed =
				link_info->force_pam4_link_speed;
			link_info->req_signal_mode = BNXT_SIG_MODE_PAM4;
		}
		link_info->req_duplex = link_info->duplex_setting;
	}
	if (link_info->autoneg & BNXT_AUTONEG_FLOW_CTRL)
		link_info->req_flow_ctrl =
			link_info->auto_pause_setting & BNXT_LINK_PAUSE_BOTH;
	else
		link_info->req_flow_ctrl = link_info->force_pause_setting;
}

static void bnxt_sp_task(struct work_struct *work)
{
	struct bnxt *bp = container_of(work, struct bnxt, sp_task);

	set_bit(BNXT_STATE_IN_SP_TASK, &bp->state);
	smp_mb__after_atomic();
	if (!test_bit(BNXT_STATE_OPEN, &bp->state)) {
		clear_bit(BNXT_STATE_IN_SP_TASK, &bp->state);
		return;
	}

	if (test_and_clear_bit(BNXT_RX_MASK_SP_EVENT, &bp->sp_event))
		bnxt_cfg_rx_mode(bp);

	if (test_and_clear_bit(BNXT_RX_NTP_FLTR_SP_EVENT, &bp->sp_event))
		bnxt_cfg_ntp_filters(bp);
	if (test_and_clear_bit(BNXT_HWRM_EXEC_FWD_REQ_SP_EVENT, &bp->sp_event))
		bnxt_hwrm_exec_fwd_req(bp);
	if (test_and_clear_bit(BNXT_PERIODIC_STATS_SP_EVENT, &bp->sp_event)) {
		bnxt_hwrm_port_qstats(bp, 0);
		bnxt_hwrm_port_qstats_ext(bp, 0);
		bnxt_accumulate_all_stats(bp);
	}

	if (test_and_clear_bit(BNXT_LINK_CHNG_SP_EVENT, &bp->sp_event)) {
		int rc;

		mutex_lock(&bp->link_lock);
		if (test_and_clear_bit(BNXT_LINK_SPEED_CHNG_SP_EVENT,
				       &bp->sp_event))
			bnxt_hwrm_phy_qcaps(bp);

		rc = bnxt_update_link(bp, true);
		if (rc)
			netdev_err(bp->dev, "SP task can't update link (rc: %x)\n",
				   rc);

		if (test_and_clear_bit(BNXT_LINK_CFG_CHANGE_SP_EVENT,
				       &bp->sp_event))
			bnxt_init_ethtool_link_settings(bp);
		mutex_unlock(&bp->link_lock);
	}
	if (test_and_clear_bit(BNXT_UPDATE_PHY_SP_EVENT, &bp->sp_event)) {
		int rc;

		mutex_lock(&bp->link_lock);
		rc = bnxt_update_phy_setting(bp);
		mutex_unlock(&bp->link_lock);
		if (rc) {
			netdev_warn(bp->dev, "update phy settings retry failed\n");
		} else {
			bp->link_info.phy_retry = false;
			netdev_info(bp->dev, "update phy settings retry succeeded\n");
		}
	}
	if (test_and_clear_bit(BNXT_HWRM_PORT_MODULE_SP_EVENT, &bp->sp_event)) {
		mutex_lock(&bp->link_lock);
		bnxt_get_port_module_status(bp);
		mutex_unlock(&bp->link_lock);
	}

	if (test_and_clear_bit(BNXT_FLOW_STATS_SP_EVENT, &bp->sp_event))
		bnxt_tc_flow_stats_work(bp);

	if (test_and_clear_bit(BNXT_RING_COAL_NOW_SP_EVENT, &bp->sp_event))
		bnxt_chk_missed_irq(bp);

	/* These functions below will clear BNXT_STATE_IN_SP_TASK.  They
	 * must be the last functions to be called before exiting.
	 */
	if (test_and_clear_bit(BNXT_RESET_TASK_SP_EVENT, &bp->sp_event))
		bnxt_reset(bp, false);

	if (test_and_clear_bit(BNXT_RESET_TASK_SILENT_SP_EVENT, &bp->sp_event))
		bnxt_reset(bp, true);

	if (test_and_clear_bit(BNXT_RST_RING_SP_EVENT, &bp->sp_event))
		bnxt_rx_ring_reset(bp);

	if (test_and_clear_bit(BNXT_FW_RESET_NOTIFY_SP_EVENT, &bp->sp_event))
		bnxt_devlink_health_report(bp, BNXT_FW_RESET_NOTIFY_SP_EVENT);

	if (test_and_clear_bit(BNXT_FW_EXCEPTION_SP_EVENT, &bp->sp_event)) {
		if (!is_bnxt_fw_ok(bp))
			bnxt_devlink_health_report(bp,
						   BNXT_FW_EXCEPTION_SP_EVENT);
	}

	smp_mb__before_atomic();
	clear_bit(BNXT_STATE_IN_SP_TASK, &bp->state);
}

/* Under rtnl_lock */
int bnxt_check_rings(struct bnxt *bp, int tx, int rx, bool sh, int tcs,
		     int tx_xdp)
{
	int max_rx, max_tx, tx_sets = 1;
	int tx_rings_needed, stats;
	int rx_rings = rx;
	int cp, vnics, rc;

	if (tcs)
		tx_sets = tcs;

	rc = bnxt_get_max_rings(bp, &max_rx, &max_tx, sh);
	if (rc)
		return rc;

	if (max_rx < rx)
		return -ENOMEM;

	tx_rings_needed = tx * tx_sets + tx_xdp;
	if (max_tx < tx_rings_needed)
		return -ENOMEM;

	vnics = 1;
	if ((bp->flags & (BNXT_FLAG_RFS | BNXT_FLAG_CHIP_P5)) == BNXT_FLAG_RFS)
		vnics += rx_rings;

	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		rx_rings <<= 1;
	cp = sh ? max_t(int, tx_rings_needed, rx) : tx_rings_needed + rx;
	stats = cp;
	if (BNXT_NEW_RM(bp)) {
		cp += bnxt_get_ulp_msix_num(bp);
		stats += bnxt_get_ulp_stat_ctxs(bp);
	}
	return bnxt_hwrm_check_rings(bp, tx_rings_needed, rx_rings, rx, cp,
				     stats, vnics);
}

static void bnxt_unmap_bars(struct bnxt *bp, struct pci_dev *pdev)
{
	if (bp->bar2) {
		pci_iounmap(pdev, bp->bar2);
		bp->bar2 = NULL;
	}

	if (bp->bar1) {
		pci_iounmap(pdev, bp->bar1);
		bp->bar1 = NULL;
	}

	if (bp->bar0) {
		pci_iounmap(pdev, bp->bar0);
		bp->bar0 = NULL;
	}
}

static void bnxt_cleanup_pci(struct bnxt *bp)
{
	bnxt_unmap_bars(bp, bp->pdev);
	pci_release_regions(bp->pdev);
	if (pci_is_enabled(bp->pdev))
		pci_disable_device(bp->pdev);
}

static void bnxt_init_dflt_coal(struct bnxt *bp)
{
	struct bnxt_coal *coal;

	/* Tick values in micro seconds.
	 * 1 coal_buf x bufs_per_record = 1 completion record.
	 */
	coal = &bp->rx_coal;
	coal->coal_ticks = 10;
	coal->coal_bufs = 30;
	coal->coal_ticks_irq = 1;
	coal->coal_bufs_irq = 2;
	coal->idle_thresh = 50;
	coal->bufs_per_record = 2;
	coal->budget = 64;		/* NAPI budget */

	coal = &bp->tx_coal;
	coal->coal_ticks = 28;
	coal->coal_bufs = 30;
	coal->coal_ticks_irq = 2;
	coal->coal_bufs_irq = 2;
	coal->bufs_per_record = 1;

	bp->stats_coal_ticks = BNXT_DEF_STATS_COAL_TICKS;
}

static int bnxt_fw_reset_via_optee(struct bnxt *bp)
{
#ifdef CONFIG_TEE_BNXT_FW
	int rc = tee_bnxt_fw_load();

	if (rc)
		netdev_err(bp->dev, "Failed FW reset via OP-TEE, rc=%d\n", rc);

	return rc;
#else
	netdev_err(bp->dev, "OP-TEE not supported\n");
	return -ENODEV;
#endif
}

static int bnxt_fw_init_one_p1(struct bnxt *bp)
{
	int rc;

	bp->fw_cap = 0;
	rc = bnxt_hwrm_ver_get(bp);
	bnxt_try_map_fw_health_reg(bp);
	if (rc) {
		if (bp->fw_health && bp->fw_health->status_reliable) {
			u32 sts = bnxt_fw_health_readl(bp, BNXT_FW_HEALTH_REG);

			netdev_err(bp->dev,
				   "Firmware not responding, status: 0x%x\n",
				   sts);
			if (sts & FW_STATUS_REG_CRASHED_NO_MASTER) {
				netdev_warn(bp->dev, "Firmware recover via OP-TEE requested\n");
				rc = bnxt_fw_reset_via_optee(bp);
				if (!rc)
					rc = bnxt_hwrm_ver_get(bp);
			}
		}
		if (rc)
			return rc;
	}

	if (bp->fw_cap & BNXT_FW_CAP_KONG_MB_CHNL) {
		rc = bnxt_alloc_kong_hwrm_resources(bp);
		if (rc)
			bp->fw_cap &= ~BNXT_FW_CAP_KONG_MB_CHNL;
	}

	if ((bp->fw_cap & BNXT_FW_CAP_SHORT_CMD) ||
	    bp->hwrm_max_ext_req_len > BNXT_HWRM_MAX_REQ_LEN) {
		rc = bnxt_alloc_hwrm_short_cmd_req(bp);
		if (rc)
			return rc;
	}
	bnxt_nvm_cfg_ver_get(bp);

	rc = bnxt_hwrm_func_reset(bp);
	if (rc)
		return -ENODEV;

	bnxt_hwrm_fw_set_time(bp);
	return 0;
}

static int bnxt_fw_init_one_p2(struct bnxt *bp)
{
	int rc;

	/* Get the MAX capabilities for this function */
	rc = bnxt_hwrm_func_qcaps(bp);
	if (rc) {
		netdev_err(bp->dev, "hwrm query capability failure rc: %x\n",
			   rc);
		return -ENODEV;
	}

	rc = bnxt_hwrm_cfa_adv_flow_mgnt_qcaps(bp);
	if (rc)
		netdev_warn(bp->dev, "hwrm query adv flow mgnt failure rc: %d\n",
			    rc);

	if (bnxt_alloc_fw_health(bp)) {
		netdev_warn(bp->dev, "no memory for firmware error recovery\n");
	} else {
		rc = bnxt_hwrm_error_recovery_qcfg(bp);
		if (rc)
			netdev_warn(bp->dev, "hwrm query error recovery failure rc: %d\n",
				    rc);
	}

	rc = bnxt_hwrm_func_drv_rgtr(bp, NULL, 0, false);
	if (rc)
		return -ENODEV;

	bnxt_hwrm_func_qcfg(bp);
	bnxt_hwrm_vnic_qcaps(bp);
	bnxt_hwrm_port_led_qcaps(bp);
	bnxt_ethtool_init(bp);
	bnxt_dcb_init(bp);
	return 0;
}

static void bnxt_set_dflt_rss_hash_type(struct bnxt *bp)
{
	bp->flags &= ~BNXT_FLAG_UDP_RSS_CAP;
	bp->rss_hash_cfg = VNIC_RSS_CFG_REQ_HASH_TYPE_IPV4 |
			   VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV4 |
			   VNIC_RSS_CFG_REQ_HASH_TYPE_IPV6 |
			   VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV6;
	if (BNXT_CHIP_P4_PLUS(bp) && bp->hwrm_spec_code >= 0x10501) {
		bp->flags |= BNXT_FLAG_UDP_RSS_CAP;
		bp->rss_hash_cfg |= VNIC_RSS_CFG_REQ_HASH_TYPE_UDP_IPV4 |
				    VNIC_RSS_CFG_REQ_HASH_TYPE_UDP_IPV6;
	}
}

static void bnxt_set_dflt_rfs(struct bnxt *bp)
{
	struct net_device *dev = bp->dev;

	dev->hw_features &= ~NETIF_F_NTUPLE;
	dev->features &= ~NETIF_F_NTUPLE;
	bp->flags &= ~BNXT_FLAG_RFS;
	if (bnxt_rfs_supported(bp)) {
		dev->hw_features |= NETIF_F_NTUPLE;
		if (bnxt_rfs_capable(bp)) {
			bp->flags |= BNXT_FLAG_RFS;
			dev->features |= NETIF_F_NTUPLE;
		}
	}
}

static void bnxt_fw_init_one_p3(struct bnxt *bp)
{
	struct pci_dev *pdev = bp->pdev;

	bnxt_set_dflt_rss_hash_type(bp);
	bnxt_set_dflt_rfs(bp);

	bnxt_get_wol_settings(bp);
	if (bp->flags & BNXT_FLAG_WOL_CAP)
		device_set_wakeup_enable(&pdev->dev, bp->wol);
	else
		device_set_wakeup_capable(&pdev->dev, false);

	bnxt_hwrm_set_cache_line_size(bp, cache_line_size());
	bnxt_hwrm_coal_params_qcaps(bp);
}

static int bnxt_fw_init_one(struct bnxt *bp)
{
	int rc;

	rc = bnxt_fw_init_one_p1(bp);
	if (rc) {
		netdev_err(bp->dev, "Firmware init phase 1 failed\n");
		return rc;
	}
	rc = bnxt_fw_init_one_p2(bp);
	if (rc) {
		netdev_err(bp->dev, "Firmware init phase 2 failed\n");
		return rc;
	}
	rc = bnxt_approve_mac(bp, bp->dev->dev_addr, false);
	if (rc)
		return rc;

	/* In case fw capabilities have changed, destroy the unneeded
	 * reporters and create newly capable ones.
	 */
	bnxt_dl_fw_reporters_destroy(bp, false);
	bnxt_dl_fw_reporters_create(bp);
	bnxt_fw_init_one_p3(bp);
	return 0;
}

static void bnxt_fw_reset_writel(struct bnxt *bp, int reg_idx)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	u32 reg = fw_health->fw_reset_seq_regs[reg_idx];
	u32 val = fw_health->fw_reset_seq_vals[reg_idx];
	u32 reg_type, reg_off, delay_msecs;

	delay_msecs = fw_health->fw_reset_seq_delay_msec[reg_idx];
	reg_type = BNXT_FW_HEALTH_REG_TYPE(reg);
	reg_off = BNXT_FW_HEALTH_REG_OFF(reg);
	switch (reg_type) {
	case BNXT_FW_HEALTH_REG_TYPE_CFG:
		pci_write_config_dword(bp->pdev, reg_off, val);
		break;
	case BNXT_FW_HEALTH_REG_TYPE_GRC:
		writel(reg_off & BNXT_GRC_BASE_MASK,
		       bp->bar0 + BNXT_GRCPF_REG_WINDOW_BASE_OUT + 4);
		reg_off = (reg_off & BNXT_GRC_OFFSET_MASK) + 0x2000;
		fallthrough;
	case BNXT_FW_HEALTH_REG_TYPE_BAR0:
		writel(val, bp->bar0 + reg_off);
		break;
	case BNXT_FW_HEALTH_REG_TYPE_BAR1:
		writel(val, bp->bar1 + reg_off);
		break;
	}
	if (delay_msecs) {
		pci_read_config_dword(bp->pdev, 0, &val);
		msleep(delay_msecs);
	}
}

static void bnxt_reset_all(struct bnxt *bp)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	int i, rc;

	if (bp->fw_cap & BNXT_FW_CAP_ERR_RECOVER_RELOAD) {
		bnxt_fw_reset_via_optee(bp);
		bp->fw_reset_timestamp = jiffies;
		return;
	}

	if (fw_health->flags & ERROR_RECOVERY_QCFG_RESP_FLAGS_HOST) {
		for (i = 0; i < fw_health->fw_reset_seq_cnt; i++)
			bnxt_fw_reset_writel(bp, i);
	} else if (fw_health->flags & ERROR_RECOVERY_QCFG_RESP_FLAGS_CO_CPU) {
		struct hwrm_fw_reset_input req = {0};

		bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FW_RESET, -1, -1);
		req.resp_addr = cpu_to_le64(bp->hwrm_cmd_kong_resp_dma_addr);
		req.embedded_proc_type = FW_RESET_REQ_EMBEDDED_PROC_TYPE_CHIP;
		req.selfrst_status = FW_RESET_REQ_SELFRST_STATUS_SELFRSTASAP;
		req.flags = FW_RESET_REQ_FLAGS_RESET_GRACEFUL;
		rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
		if (rc)
			netdev_warn(bp->dev, "Unable to reset FW rc=%d\n", rc);
	}
	bp->fw_reset_timestamp = jiffies;
}

static void bnxt_fw_reset_task(struct work_struct *work)
{
	struct bnxt *bp = container_of(work, struct bnxt, fw_reset_task.work);
	int rc;

	if (!test_bit(BNXT_STATE_IN_FW_RESET, &bp->state)) {
		netdev_err(bp->dev, "bnxt_fw_reset_task() called when not in fw reset mode!\n");
		return;
	}

	switch (bp->fw_reset_state) {
	case BNXT_FW_RESET_STATE_POLL_VF: {
		int n = bnxt_get_registered_vfs(bp);
		int tmo;

		if (n < 0) {
			netdev_err(bp->dev, "Firmware reset aborted, subsequent func_qcfg cmd failed, rc = %d, %d msecs since reset timestamp\n",
				   n, jiffies_to_msecs(jiffies -
				   bp->fw_reset_timestamp));
			goto fw_reset_abort;
		} else if (n > 0) {
			if (time_after(jiffies, bp->fw_reset_timestamp +
				       (bp->fw_reset_max_dsecs * HZ / 10))) {
				clear_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
				bp->fw_reset_state = 0;
				netdev_err(bp->dev, "Firmware reset aborted, bnxt_get_registered_vfs() returns %d\n",
					   n);
				return;
			}
			bnxt_queue_fw_reset_work(bp, HZ / 10);
			return;
		}
		bp->fw_reset_timestamp = jiffies;
		rtnl_lock();
		bnxt_fw_reset_close(bp);
		if (bp->fw_cap & BNXT_FW_CAP_ERR_RECOVER_RELOAD) {
			bp->fw_reset_state = BNXT_FW_RESET_STATE_POLL_FW_DOWN;
			tmo = HZ / 10;
		} else {
			bp->fw_reset_state = BNXT_FW_RESET_STATE_ENABLE_DEV;
			tmo = bp->fw_reset_min_dsecs * HZ / 10;
		}
		rtnl_unlock();
		bnxt_queue_fw_reset_work(bp, tmo);
		return;
	}
	case BNXT_FW_RESET_STATE_POLL_FW_DOWN: {
		u32 val;

		val = bnxt_fw_health_readl(bp, BNXT_FW_HEALTH_REG);
		if (!(val & BNXT_FW_STATUS_SHUTDOWN) &&
		    !time_after(jiffies, bp->fw_reset_timestamp +
		    (bp->fw_reset_max_dsecs * HZ / 10))) {
			bnxt_queue_fw_reset_work(bp, HZ / 5);
			return;
		}

		if (!bp->fw_health->master) {
			u32 wait_dsecs = bp->fw_health->normal_func_wait_dsecs;

			bp->fw_reset_state = BNXT_FW_RESET_STATE_ENABLE_DEV;
			bnxt_queue_fw_reset_work(bp, wait_dsecs * HZ / 10);
			return;
		}
		bp->fw_reset_state = BNXT_FW_RESET_STATE_RESET_FW;
	}
		fallthrough;
	case BNXT_FW_RESET_STATE_RESET_FW:
		bnxt_reset_all(bp);
		bp->fw_reset_state = BNXT_FW_RESET_STATE_ENABLE_DEV;
		bnxt_queue_fw_reset_work(bp, bp->fw_reset_min_dsecs * HZ / 10);
		return;
	case BNXT_FW_RESET_STATE_ENABLE_DEV:
		if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state)) {
			u32 val;

			val = bnxt_fw_health_readl(bp,
						   BNXT_FW_RESET_INPROG_REG);
			if (val)
				netdev_warn(bp->dev, "FW reset inprog %x after min wait time.\n",
					    val);
		}
		clear_bit(BNXT_STATE_FW_FATAL_COND, &bp->state);
		if (pci_enable_device(bp->pdev)) {
			netdev_err(bp->dev, "Cannot re-enable PCI device\n");
			goto fw_reset_abort;
		}
		pci_set_master(bp->pdev);
		bp->fw_reset_state = BNXT_FW_RESET_STATE_POLL_FW;
		fallthrough;
	case BNXT_FW_RESET_STATE_POLL_FW:
		bp->hwrm_cmd_timeout = SHORT_HWRM_CMD_TIMEOUT;
		rc = __bnxt_hwrm_ver_get(bp, true);
		if (rc) {
			if (time_after(jiffies, bp->fw_reset_timestamp +
				       (bp->fw_reset_max_dsecs * HZ / 10))) {
				netdev_err(bp->dev, "Firmware reset aborted\n");
				goto fw_reset_abort_status;
			}
			bnxt_queue_fw_reset_work(bp, HZ / 5);
			return;
		}
		bp->hwrm_cmd_timeout = DFLT_HWRM_CMD_TIMEOUT;
		bp->fw_reset_state = BNXT_FW_RESET_STATE_OPENING;
		fallthrough;
	case BNXT_FW_RESET_STATE_OPENING:
		while (!rtnl_trylock()) {
			bnxt_queue_fw_reset_work(bp, HZ / 10);
			return;
		}
		rc = bnxt_open(bp->dev);
		if (rc) {
			netdev_err(bp->dev, "bnxt_open_nic() failed\n");
			clear_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
			dev_close(bp->dev);
		}

		bp->fw_reset_state = 0;
		/* Make sure fw_reset_state is 0 before clearing the flag */
		smp_mb__before_atomic();
		clear_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
		bnxt_ulp_start(bp, rc);
		if (!rc)
			bnxt_reenable_sriov(bp);
		bnxt_dl_health_recovery_done(bp);
		bnxt_dl_health_status_update(bp, true);
		rtnl_unlock();
		break;
	}
	return;

fw_reset_abort_status:
	if (bp->fw_health->status_reliable ||
	    (bp->fw_cap & BNXT_FW_CAP_ERROR_RECOVERY)) {
		u32 sts = bnxt_fw_health_readl(bp, BNXT_FW_HEALTH_REG);

		netdev_err(bp->dev, "fw_health_status 0x%x\n", sts);
	}
fw_reset_abort:
	clear_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
	if (bp->fw_reset_state != BNXT_FW_RESET_STATE_POLL_VF)
		bnxt_dl_health_status_update(bp, false);
	bp->fw_reset_state = 0;
	rtnl_lock();
	dev_close(bp->dev);
	rtnl_unlock();
}

static int bnxt_init_board(struct pci_dev *pdev, struct net_device *dev)
{
	int rc;
	struct bnxt *bp = netdev_priv(dev);

	SET_NETDEV_DEV(dev, &pdev->dev);

	/* enable device (incl. PCI PM wakeup), and bus-mastering */
	rc = pci_enable_device(pdev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot enable PCI device, aborting\n");
		goto init_err;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev,
			"Cannot find PCI device base address, aborting\n");
		rc = -ENODEV;
		goto init_err_disable;
	}

	rc = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (rc) {
		dev_err(&pdev->dev, "Cannot obtain PCI resources, aborting\n");
		goto init_err_disable;
	}

	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64)) != 0 &&
	    dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32)) != 0) {
		dev_err(&pdev->dev, "System does not support DMA, aborting\n");
		rc = -EIO;
		goto init_err_release;
	}

	pci_set_master(pdev);

	bp->dev = dev;
	bp->pdev = pdev;

	/* Doorbell BAR bp->bar1 is mapped after bnxt_fw_init_one_p2()
	 * determines the BAR size.
	 */
	bp->bar0 = pci_ioremap_bar(pdev, 0);
	if (!bp->bar0) {
		dev_err(&pdev->dev, "Cannot map device registers, aborting\n");
		rc = -ENOMEM;
		goto init_err_release;
	}

	bp->bar2 = pci_ioremap_bar(pdev, 4);
	if (!bp->bar2) {
		dev_err(&pdev->dev, "Cannot map bar4 registers, aborting\n");
		rc = -ENOMEM;
		goto init_err_release;
	}

	pci_enable_pcie_error_reporting(pdev);

	INIT_WORK(&bp->sp_task, bnxt_sp_task);
	INIT_DELAYED_WORK(&bp->fw_reset_task, bnxt_fw_reset_task);

	spin_lock_init(&bp->ntp_fltr_lock);
#if BITS_PER_LONG == 32
	spin_lock_init(&bp->db_lock);
#endif

	bp->rx_ring_size = BNXT_DEFAULT_RX_RING_SIZE;
	bp->tx_ring_size = BNXT_DEFAULT_TX_RING_SIZE;

	bnxt_init_dflt_coal(bp);

	timer_setup(&bp->timer, bnxt_timer, 0);
	bp->current_interval = BNXT_TIMER_INTERVAL;

	bp->vxlan_fw_dst_port_id = INVALID_HW_RING_ID;
	bp->nge_fw_dst_port_id = INVALID_HW_RING_ID;

	clear_bit(BNXT_STATE_OPEN, &bp->state);
	return 0;

init_err_release:
	bnxt_unmap_bars(bp, pdev);
	pci_release_regions(pdev);

init_err_disable:
	pci_disable_device(pdev);

init_err:
	return rc;
}

/* rtnl_lock held */
static int bnxt_change_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct bnxt *bp = netdev_priv(dev);
	int rc = 0;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (ether_addr_equal(addr->sa_data, dev->dev_addr))
		return 0;

	rc = bnxt_approve_mac(bp, addr->sa_data, true);
	if (rc)
		return rc;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	if (netif_running(dev)) {
		bnxt_close_nic(bp, false, false);
		rc = bnxt_open_nic(bp, false, false);
	}

	return rc;
}

/* rtnl_lock held */
static int bnxt_change_mtu(struct net_device *dev, int new_mtu)
{
	struct bnxt *bp = netdev_priv(dev);

	if (netif_running(dev))
		bnxt_close_nic(bp, true, false);

	dev->mtu = new_mtu;
	bnxt_set_ring_params(bp);

	if (netif_running(dev))
		return bnxt_open_nic(bp, true, false);

	return 0;
}

int bnxt_setup_mq_tc(struct net_device *dev, u8 tc)
{
	struct bnxt *bp = netdev_priv(dev);
	bool sh = false;
	int rc;

	if (tc > bp->max_tc) {
		netdev_err(dev, "Too many traffic classes requested: %d. Max supported is %d.\n",
			   tc, bp->max_tc);
		return -EINVAL;
	}

	if (netdev_get_num_tc(dev) == tc)
		return 0;

	if (bp->flags & BNXT_FLAG_SHARED_RINGS)
		sh = true;

	rc = bnxt_check_rings(bp, bp->tx_nr_rings_per_tc, bp->rx_nr_rings,
			      sh, tc, bp->tx_nr_rings_xdp);
	if (rc)
		return rc;

	/* Needs to close the device and do hw resource re-allocations */
	if (netif_running(bp->dev))
		bnxt_close_nic(bp, true, false);

	if (tc) {
		bp->tx_nr_rings = bp->tx_nr_rings_per_tc * tc;
		netdev_set_num_tc(dev, tc);
	} else {
		bp->tx_nr_rings = bp->tx_nr_rings_per_tc;
		netdev_reset_tc(dev);
	}
	bp->tx_nr_rings += bp->tx_nr_rings_xdp;
	bp->cp_nr_rings = sh ? max_t(int, bp->tx_nr_rings, bp->rx_nr_rings) :
			       bp->tx_nr_rings + bp->rx_nr_rings;

	if (netif_running(bp->dev))
		return bnxt_open_nic(bp, true, false);

	return 0;
}

static int bnxt_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
				  void *cb_priv)
{
	struct bnxt *bp = cb_priv;

	if (!bnxt_tc_flower_enabled(bp) ||
	    !tc_cls_can_offload_and_chain0(bp->dev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return bnxt_tc_setup_flower(bp, bp->pf.fw_fid, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

LIST_HEAD(bnxt_block_cb_list);

static int bnxt_setup_tc(struct net_device *dev, enum tc_setup_type type,
			 void *type_data)
{
	struct bnxt *bp = netdev_priv(dev);

	switch (type) {
	case TC_SETUP_BLOCK:
		return flow_block_cb_setup_simple(type_data,
						  &bnxt_block_cb_list,
						  bnxt_setup_tc_block_cb,
						  bp, bp, true);
	case TC_SETUP_QDISC_MQPRIO: {
		struct tc_mqprio_qopt *mqprio = type_data;

		mqprio->hw = TC_MQPRIO_HW_OFFLOAD_TCS;

		return bnxt_setup_mq_tc(dev, mqprio->num_tc);
	}
	default:
		return -EOPNOTSUPP;
	}
}

#ifdef CONFIG_RFS_ACCEL
static bool bnxt_fltr_match(struct bnxt_ntuple_filter *f1,
			    struct bnxt_ntuple_filter *f2)
{
	struct flow_keys *keys1 = &f1->fkeys;
	struct flow_keys *keys2 = &f2->fkeys;

	if (keys1->basic.n_proto != keys2->basic.n_proto ||
	    keys1->basic.ip_proto != keys2->basic.ip_proto)
		return false;

	if (keys1->basic.n_proto == htons(ETH_P_IP)) {
		if (keys1->addrs.v4addrs.src != keys2->addrs.v4addrs.src ||
		    keys1->addrs.v4addrs.dst != keys2->addrs.v4addrs.dst)
			return false;
	} else {
		if (memcmp(&keys1->addrs.v6addrs.src, &keys2->addrs.v6addrs.src,
			   sizeof(keys1->addrs.v6addrs.src)) ||
		    memcmp(&keys1->addrs.v6addrs.dst, &keys2->addrs.v6addrs.dst,
			   sizeof(keys1->addrs.v6addrs.dst)))
			return false;
	}

	if (keys1->ports.ports == keys2->ports.ports &&
	    keys1->control.flags == keys2->control.flags &&
	    ether_addr_equal(f1->src_mac_addr, f2->src_mac_addr) &&
	    ether_addr_equal(f1->dst_mac_addr, f2->dst_mac_addr))
		return true;

	return false;
}

static int bnxt_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
			      u16 rxq_index, u32 flow_id)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_ntuple_filter *fltr, *new_fltr;
	struct flow_keys *fkeys;
	struct ethhdr *eth = (struct ethhdr *)skb_mac_header(skb);
	int rc = 0, idx, bit_id, l2_idx = 0;
	struct hlist_head *head;
	u32 flags;

	if (!ether_addr_equal(dev->dev_addr, eth->h_dest)) {
		struct bnxt_vnic_info *vnic = &bp->vnic_info[0];
		int off = 0, j;

		netif_addr_lock_bh(dev);
		for (j = 0; j < vnic->uc_filter_count; j++, off += ETH_ALEN) {
			if (ether_addr_equal(eth->h_dest,
					     vnic->uc_list + off)) {
				l2_idx = j + 1;
				break;
			}
		}
		netif_addr_unlock_bh(dev);
		if (!l2_idx)
			return -EINVAL;
	}
	new_fltr = kzalloc(sizeof(*new_fltr), GFP_ATOMIC);
	if (!new_fltr)
		return -ENOMEM;

	fkeys = &new_fltr->fkeys;
	if (!skb_flow_dissect_flow_keys(skb, fkeys, 0)) {
		rc = -EPROTONOSUPPORT;
		goto err_free;
	}

	if ((fkeys->basic.n_proto != htons(ETH_P_IP) &&
	     fkeys->basic.n_proto != htons(ETH_P_IPV6)) ||
	    ((fkeys->basic.ip_proto != IPPROTO_TCP) &&
	     (fkeys->basic.ip_proto != IPPROTO_UDP))) {
		rc = -EPROTONOSUPPORT;
		goto err_free;
	}
	if (fkeys->basic.n_proto == htons(ETH_P_IPV6) &&
	    bp->hwrm_spec_code < 0x10601) {
		rc = -EPROTONOSUPPORT;
		goto err_free;
	}
	flags = fkeys->control.flags;
	if (((flags & FLOW_DIS_ENCAPSULATION) &&
	     bp->hwrm_spec_code < 0x10601) || (flags & FLOW_DIS_IS_FRAGMENT)) {
		rc = -EPROTONOSUPPORT;
		goto err_free;
	}

	memcpy(new_fltr->dst_mac_addr, eth->h_dest, ETH_ALEN);
	memcpy(new_fltr->src_mac_addr, eth->h_source, ETH_ALEN);

	idx = skb_get_hash_raw(skb) & BNXT_NTP_FLTR_HASH_MASK;
	head = &bp->ntp_fltr_hash_tbl[idx];
	rcu_read_lock();
	hlist_for_each_entry_rcu(fltr, head, hash) {
		if (bnxt_fltr_match(fltr, new_fltr)) {
			rcu_read_unlock();
			rc = 0;
			goto err_free;
		}
	}
	rcu_read_unlock();

	spin_lock_bh(&bp->ntp_fltr_lock);
	bit_id = bitmap_find_free_region(bp->ntp_fltr_bmap,
					 BNXT_NTP_FLTR_MAX_FLTR, 0);
	if (bit_id < 0) {
		spin_unlock_bh(&bp->ntp_fltr_lock);
		rc = -ENOMEM;
		goto err_free;
	}

	new_fltr->sw_id = (u16)bit_id;
	new_fltr->flow_id = flow_id;
	new_fltr->l2_fltr_idx = l2_idx;
	new_fltr->rxq = rxq_index;
	hlist_add_head_rcu(&new_fltr->hash, head);
	bp->ntp_fltr_count++;
	spin_unlock_bh(&bp->ntp_fltr_lock);

	set_bit(BNXT_RX_NTP_FLTR_SP_EVENT, &bp->sp_event);
	bnxt_queue_sp_work(bp);

	return new_fltr->sw_id;

err_free:
	kfree(new_fltr);
	return rc;
}

static void bnxt_cfg_ntp_filters(struct bnxt *bp)
{
	int i;

	for (i = 0; i < BNXT_NTP_FLTR_HASH_SIZE; i++) {
		struct hlist_head *head;
		struct hlist_node *tmp;
		struct bnxt_ntuple_filter *fltr;
		int rc;

		head = &bp->ntp_fltr_hash_tbl[i];
		hlist_for_each_entry_safe(fltr, tmp, head, hash) {
			bool del = false;

			if (test_bit(BNXT_FLTR_VALID, &fltr->state)) {
				if (rps_may_expire_flow(bp->dev, fltr->rxq,
							fltr->flow_id,
							fltr->sw_id)) {
					bnxt_hwrm_cfa_ntuple_filter_free(bp,
									 fltr);
					del = true;
				}
			} else {
				rc = bnxt_hwrm_cfa_ntuple_filter_alloc(bp,
								       fltr);
				if (rc)
					del = true;
				else
					set_bit(BNXT_FLTR_VALID, &fltr->state);
			}

			if (del) {
				spin_lock_bh(&bp->ntp_fltr_lock);
				hlist_del_rcu(&fltr->hash);
				bp->ntp_fltr_count--;
				spin_unlock_bh(&bp->ntp_fltr_lock);
				synchronize_rcu();
				clear_bit(fltr->sw_id, bp->ntp_fltr_bmap);
				kfree(fltr);
			}
		}
	}
	if (test_and_clear_bit(BNXT_HWRM_PF_UNLOAD_SP_EVENT, &bp->sp_event))
		netdev_info(bp->dev, "Receive PF driver unload event!\n");
}

#else

static void bnxt_cfg_ntp_filters(struct bnxt *bp)
{
}

#endif /* CONFIG_RFS_ACCEL */

static int bnxt_udp_tunnel_sync(struct net_device *netdev, unsigned int table)
{
	struct bnxt *bp = netdev_priv(netdev);
	struct udp_tunnel_info ti;
	unsigned int cmd;

	udp_tunnel_nic_get_port(netdev, table, 0, &ti);
	if (ti.type == UDP_TUNNEL_TYPE_VXLAN)
		cmd = TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_VXLAN;
	else
		cmd = TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_GENEVE;

	if (ti.port)
		return bnxt_hwrm_tunnel_dst_port_alloc(bp, ti.port, cmd);

	return bnxt_hwrm_tunnel_dst_port_free(bp, cmd);
}

static const struct udp_tunnel_nic_info bnxt_udp_tunnels = {
	.sync_table	= bnxt_udp_tunnel_sync,
	.flags		= UDP_TUNNEL_NIC_INFO_MAY_SLEEP |
			  UDP_TUNNEL_NIC_INFO_OPEN_ONLY,
	.tables		= {
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_VXLAN,  },
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_GENEVE, },
	},
};

static int bnxt_bridge_getlink(struct sk_buff *skb, u32 pid, u32 seq,
			       struct net_device *dev, u32 filter_mask,
			       int nlflags)
{
	struct bnxt *bp = netdev_priv(dev);

	return ndo_dflt_bridge_getlink(skb, pid, seq, dev, bp->br_mode, 0, 0,
				       nlflags, filter_mask, NULL);
}

static int bnxt_bridge_setlink(struct net_device *dev, struct nlmsghdr *nlh,
			       u16 flags, struct netlink_ext_ack *extack)
{
	struct bnxt *bp = netdev_priv(dev);
	struct nlattr *attr, *br_spec;
	int rem, rc = 0;

	if (bp->hwrm_spec_code < 0x10708 || !BNXT_SINGLE_PF(bp))
		return -EOPNOTSUPP;

	br_spec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_AF_SPEC);
	if (!br_spec)
		return -EINVAL;

	nla_for_each_nested(attr, br_spec, rem) {
		u16 mode;

		if (nla_type(attr) != IFLA_BRIDGE_MODE)
			continue;

		if (nla_len(attr) < sizeof(mode))
			return -EINVAL;

		mode = nla_get_u16(attr);
		if (mode == bp->br_mode)
			break;

		rc = bnxt_hwrm_set_br_mode(bp, mode);
		if (!rc)
			bp->br_mode = mode;
		break;
	}
	return rc;
}

int bnxt_get_port_parent_id(struct net_device *dev,
			    struct netdev_phys_item_id *ppid)
{
	struct bnxt *bp = netdev_priv(dev);

	if (bp->eswitch_mode != DEVLINK_ESWITCH_MODE_SWITCHDEV)
		return -EOPNOTSUPP;

	/* The PF and it's VF-reps only support the switchdev framework */
	if (!BNXT_PF(bp) || !(bp->flags & BNXT_FLAG_DSN_VALID))
		return -EOPNOTSUPP;

	ppid->id_len = sizeof(bp->dsn);
	memcpy(ppid->id, bp->dsn, ppid->id_len);

	return 0;
}

static struct devlink_port *bnxt_get_devlink_port(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);

	return &bp->dl_port;
}

static const struct net_device_ops bnxt_netdev_ops = {
	.ndo_open		= bnxt_open,
	.ndo_start_xmit		= bnxt_start_xmit,
	.ndo_stop		= bnxt_close,
	.ndo_get_stats64	= bnxt_get_stats64,
	.ndo_set_rx_mode	= bnxt_set_rx_mode,
	.ndo_do_ioctl		= bnxt_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= bnxt_change_mac_addr,
	.ndo_change_mtu		= bnxt_change_mtu,
	.ndo_fix_features	= bnxt_fix_features,
	.ndo_set_features	= bnxt_set_features,
	.ndo_tx_timeout		= bnxt_tx_timeout,
#ifdef CONFIG_BNXT_SRIOV
	.ndo_get_vf_config	= bnxt_get_vf_config,
	.ndo_set_vf_mac		= bnxt_set_vf_mac,
	.ndo_set_vf_vlan	= bnxt_set_vf_vlan,
	.ndo_set_vf_rate	= bnxt_set_vf_bw,
	.ndo_set_vf_link_state	= bnxt_set_vf_link_state,
	.ndo_set_vf_spoofchk	= bnxt_set_vf_spoofchk,
	.ndo_set_vf_trust	= bnxt_set_vf_trust,
#endif
	.ndo_setup_tc           = bnxt_setup_tc,
#ifdef CONFIG_RFS_ACCEL
	.ndo_rx_flow_steer	= bnxt_rx_flow_steer,
#endif
	.ndo_udp_tunnel_add	= udp_tunnel_nic_add_port,
	.ndo_udp_tunnel_del	= udp_tunnel_nic_del_port,
	.ndo_bpf		= bnxt_xdp,
	.ndo_xdp_xmit		= bnxt_xdp_xmit,
	.ndo_bridge_getlink	= bnxt_bridge_getlink,
	.ndo_bridge_setlink	= bnxt_bridge_setlink,
	.ndo_get_devlink_port	= bnxt_get_devlink_port,
};

static void bnxt_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnxt *bp = netdev_priv(dev);

	if (BNXT_PF(bp))
		bnxt_sriov_disable(bp);

	if (BNXT_PF(bp))
		devlink_port_type_clear(&bp->dl_port);
	pci_disable_pcie_error_reporting(pdev);
	unregister_netdev(dev);
	clear_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
	/* Flush any pending tasks */
	cancel_work_sync(&bp->sp_task);
	cancel_delayed_work_sync(&bp->fw_reset_task);
	bp->sp_event = 0;

	bnxt_dl_fw_reporters_destroy(bp, true);
	bnxt_dl_unregister(bp);
	bnxt_shutdown_tc(bp);

	bnxt_clear_int_mode(bp);
	bnxt_hwrm_func_drv_unrgtr(bp);
	bnxt_free_hwrm_resources(bp);
	bnxt_free_hwrm_short_cmd_req(bp);
	bnxt_ethtool_free(bp);
	bnxt_dcb_free(bp);
	kfree(bp->edev);
	bp->edev = NULL;
	kfree(bp->fw_health);
	bp->fw_health = NULL;
	bnxt_cleanup_pci(bp);
	bnxt_free_ctx_mem(bp);
	kfree(bp->ctx);
	bp->ctx = NULL;
	kfree(bp->rss_indir_tbl);
	bp->rss_indir_tbl = NULL;
	bnxt_free_port_stats(bp);
	free_netdev(dev);
}

static int bnxt_probe_phy(struct bnxt *bp, bool fw_dflt)
{
	int rc = 0;
	struct bnxt_link_info *link_info = &bp->link_info;

	rc = bnxt_hwrm_phy_qcaps(bp);
	if (rc) {
		netdev_err(bp->dev, "Probe phy can't get phy capabilities (rc: %x)\n",
			   rc);
		return rc;
	}
	if (!fw_dflt)
		return 0;

	rc = bnxt_update_link(bp, false);
	if (rc) {
		netdev_err(bp->dev, "Probe phy can't update link (rc: %x)\n",
			   rc);
		return rc;
	}

	/* Older firmware does not have supported_auto_speeds, so assume
	 * that all supported speeds can be autonegotiated.
	 */
	if (link_info->auto_link_speeds && !link_info->support_auto_speeds)
		link_info->support_auto_speeds = link_info->support_speeds;

	bnxt_init_ethtool_link_settings(bp);
	return 0;
}

static int bnxt_get_max_irq(struct pci_dev *pdev)
{
	u16 ctrl;

	if (!pdev->msix_cap)
		return 1;

	pci_read_config_word(pdev, pdev->msix_cap + PCI_MSIX_FLAGS, &ctrl);
	return (ctrl & PCI_MSIX_FLAGS_QSIZE) + 1;
}

static void _bnxt_get_max_rings(struct bnxt *bp, int *max_rx, int *max_tx,
				int *max_cp)
{
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;
	int max_ring_grps = 0, max_irq;

	*max_tx = hw_resc->max_tx_rings;
	*max_rx = hw_resc->max_rx_rings;
	*max_cp = bnxt_get_max_func_cp_rings_for_en(bp);
	max_irq = min_t(int, bnxt_get_max_func_irqs(bp) -
			bnxt_get_ulp_msix_num(bp),
			hw_resc->max_stat_ctxs - bnxt_get_ulp_stat_ctxs(bp));
	if (!(bp->flags & BNXT_FLAG_CHIP_P5))
		*max_cp = min_t(int, *max_cp, max_irq);
	max_ring_grps = hw_resc->max_hw_ring_grps;
	if (BNXT_CHIP_TYPE_NITRO_A0(bp) && BNXT_PF(bp)) {
		*max_cp -= 1;
		*max_rx -= 2;
	}
	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		*max_rx >>= 1;
	if (bp->flags & BNXT_FLAG_CHIP_P5) {
		bnxt_trim_rings(bp, max_rx, max_tx, *max_cp, false);
		/* On P5 chips, max_cp output param should be available NQs */
		*max_cp = max_irq;
	}
	*max_rx = min_t(int, *max_rx, max_ring_grps);
}

int bnxt_get_max_rings(struct bnxt *bp, int *max_rx, int *max_tx, bool shared)
{
	int rx, tx, cp;

	_bnxt_get_max_rings(bp, &rx, &tx, &cp);
	*max_rx = rx;
	*max_tx = tx;
	if (!rx || !tx || !cp)
		return -ENOMEM;

	return bnxt_trim_rings(bp, max_rx, max_tx, cp, shared);
}

static int bnxt_get_dflt_rings(struct bnxt *bp, int *max_rx, int *max_tx,
			       bool shared)
{
	int rc;

	rc = bnxt_get_max_rings(bp, max_rx, max_tx, shared);
	if (rc && (bp->flags & BNXT_FLAG_AGG_RINGS)) {
		/* Not enough rings, try disabling agg rings. */
		bp->flags &= ~BNXT_FLAG_AGG_RINGS;
		rc = bnxt_get_max_rings(bp, max_rx, max_tx, shared);
		if (rc) {
			/* set BNXT_FLAG_AGG_RINGS back for consistency */
			bp->flags |= BNXT_FLAG_AGG_RINGS;
			return rc;
		}
		bp->flags |= BNXT_FLAG_NO_AGG_RINGS;
		bp->dev->hw_features &= ~(NETIF_F_LRO | NETIF_F_GRO_HW);
		bp->dev->features &= ~(NETIF_F_LRO | NETIF_F_GRO_HW);
		bnxt_set_ring_params(bp);
	}

	if (bp->flags & BNXT_FLAG_ROCE_CAP) {
		int max_cp, max_stat, max_irq;

		/* Reserve minimum resources for RoCE */
		max_cp = bnxt_get_max_func_cp_rings(bp);
		max_stat = bnxt_get_max_func_stat_ctxs(bp);
		max_irq = bnxt_get_max_func_irqs(bp);
		if (max_cp <= BNXT_MIN_ROCE_CP_RINGS ||
		    max_irq <= BNXT_MIN_ROCE_CP_RINGS ||
		    max_stat <= BNXT_MIN_ROCE_STAT_CTXS)
			return 0;

		max_cp -= BNXT_MIN_ROCE_CP_RINGS;
		max_irq -= BNXT_MIN_ROCE_CP_RINGS;
		max_stat -= BNXT_MIN_ROCE_STAT_CTXS;
		max_cp = min_t(int, max_cp, max_irq);
		max_cp = min_t(int, max_cp, max_stat);
		rc = bnxt_trim_rings(bp, max_rx, max_tx, max_cp, shared);
		if (rc)
			rc = 0;
	}
	return rc;
}

/* In initial default shared ring setting, each shared ring must have a
 * RX/TX ring pair.
 */
static void bnxt_trim_dflt_sh_rings(struct bnxt *bp)
{
	bp->cp_nr_rings = min_t(int, bp->tx_nr_rings_per_tc, bp->rx_nr_rings);
	bp->rx_nr_rings = bp->cp_nr_rings;
	bp->tx_nr_rings_per_tc = bp->cp_nr_rings;
	bp->tx_nr_rings = bp->tx_nr_rings_per_tc;
}

static int bnxt_set_dflt_rings(struct bnxt *bp, bool sh)
{
	int dflt_rings, max_rx_rings, max_tx_rings, rc;

	if (!bnxt_can_reserve_rings(bp))
		return 0;

	if (sh)
		bp->flags |= BNXT_FLAG_SHARED_RINGS;
	dflt_rings = is_kdump_kernel() ? 1 : netif_get_num_default_rss_queues();
	/* Reduce default rings on multi-port cards so that total default
	 * rings do not exceed CPU count.
	 */
	if (bp->port_count > 1) {
		int max_rings =
			max_t(int, num_online_cpus() / bp->port_count, 1);

		dflt_rings = min_t(int, dflt_rings, max_rings);
	}
	rc = bnxt_get_dflt_rings(bp, &max_rx_rings, &max_tx_rings, sh);
	if (rc)
		return rc;
	bp->rx_nr_rings = min_t(int, dflt_rings, max_rx_rings);
	bp->tx_nr_rings_per_tc = min_t(int, dflt_rings, max_tx_rings);
	if (sh)
		bnxt_trim_dflt_sh_rings(bp);
	else
		bp->cp_nr_rings = bp->tx_nr_rings_per_tc + bp->rx_nr_rings;
	bp->tx_nr_rings = bp->tx_nr_rings_per_tc;

	rc = __bnxt_reserve_rings(bp);
	if (rc)
		netdev_warn(bp->dev, "Unable to reserve tx rings\n");
	bp->tx_nr_rings_per_tc = bp->tx_nr_rings;
	if (sh)
		bnxt_trim_dflt_sh_rings(bp);

	/* Rings may have been trimmed, re-reserve the trimmed rings. */
	if (bnxt_need_reserve_rings(bp)) {
		rc = __bnxt_reserve_rings(bp);
		if (rc)
			netdev_warn(bp->dev, "2nd rings reservation failed.\n");
		bp->tx_nr_rings_per_tc = bp->tx_nr_rings;
	}
	if (BNXT_CHIP_TYPE_NITRO_A0(bp)) {
		bp->rx_nr_rings++;
		bp->cp_nr_rings++;
	}
	if (rc) {
		bp->tx_nr_rings = 0;
		bp->rx_nr_rings = 0;
	}
	return rc;
}

static int bnxt_init_dflt_ring_mode(struct bnxt *bp)
{
	int rc;

	if (bp->tx_nr_rings)
		return 0;

	bnxt_ulp_irq_stop(bp);
	bnxt_clear_int_mode(bp);
	rc = bnxt_set_dflt_rings(bp, true);
	if (rc) {
		netdev_err(bp->dev, "Not enough rings available.\n");
		goto init_dflt_ring_err;
	}
	rc = bnxt_init_int_mode(bp);
	if (rc)
		goto init_dflt_ring_err;

	bp->tx_nr_rings_per_tc = bp->tx_nr_rings;
	if (bnxt_rfs_supported(bp) && bnxt_rfs_capable(bp)) {
		bp->flags |= BNXT_FLAG_RFS;
		bp->dev->features |= NETIF_F_NTUPLE;
	}
init_dflt_ring_err:
	bnxt_ulp_irq_restart(bp, rc);
	return rc;
}

int bnxt_restore_pf_fw_resources(struct bnxt *bp)
{
	int rc;

	ASSERT_RTNL();
	bnxt_hwrm_func_qcaps(bp);

	if (netif_running(bp->dev))
		__bnxt_close_nic(bp, true, false);

	bnxt_ulp_irq_stop(bp);
	bnxt_clear_int_mode(bp);
	rc = bnxt_init_int_mode(bp);
	bnxt_ulp_irq_restart(bp, rc);

	if (netif_running(bp->dev)) {
		if (rc)
			dev_close(bp->dev);
		else
			rc = bnxt_open_nic(bp, true, false);
	}

	return rc;
}

static int bnxt_init_mac_addr(struct bnxt *bp)
{
	int rc = 0;

	if (BNXT_PF(bp)) {
		memcpy(bp->dev->dev_addr, bp->pf.mac_addr, ETH_ALEN);
	} else {
#ifdef CONFIG_BNXT_SRIOV
		struct bnxt_vf_info *vf = &bp->vf;
		bool strict_approval = true;

		if (is_valid_ether_addr(vf->mac_addr)) {
			/* overwrite netdev dev_addr with admin VF MAC */
			memcpy(bp->dev->dev_addr, vf->mac_addr, ETH_ALEN);
			/* Older PF driver or firmware may not approve this
			 * correctly.
			 */
			strict_approval = false;
		} else {
			eth_hw_addr_random(bp->dev);
		}
		rc = bnxt_approve_mac(bp, bp->dev->dev_addr, strict_approval);
#endif
	}
	return rc;
}

#define BNXT_VPD_LEN	512
static void bnxt_vpd_read_info(struct bnxt *bp)
{
	struct pci_dev *pdev = bp->pdev;
	int i, len, pos, ro_size, size;
	ssize_t vpd_size;
	u8 *vpd_data;

	vpd_data = kmalloc(BNXT_VPD_LEN, GFP_KERNEL);
	if (!vpd_data)
		return;

	vpd_size = pci_read_vpd(pdev, 0, BNXT_VPD_LEN, vpd_data);
	if (vpd_size <= 0) {
		netdev_err(bp->dev, "Unable to read VPD\n");
		goto exit;
	}

	i = pci_vpd_find_tag(vpd_data, 0, vpd_size, PCI_VPD_LRDT_RO_DATA);
	if (i < 0) {
		netdev_err(bp->dev, "VPD READ-Only not found\n");
		goto exit;
	}

	ro_size = pci_vpd_lrdt_size(&vpd_data[i]);
	i += PCI_VPD_LRDT_TAG_SIZE;
	if (i + ro_size > vpd_size)
		goto exit;

	pos = pci_vpd_find_info_keyword(vpd_data, i, ro_size,
					PCI_VPD_RO_KEYWORD_PARTNO);
	if (pos < 0)
		goto read_sn;

	len = pci_vpd_info_field_size(&vpd_data[pos]);
	pos += PCI_VPD_INFO_FLD_HDR_SIZE;
	if (len + pos > vpd_size)
		goto read_sn;

	size = min(len, BNXT_VPD_FLD_LEN - 1);
	memcpy(bp->board_partno, &vpd_data[pos], size);

read_sn:
	pos = pci_vpd_find_info_keyword(vpd_data, i, ro_size,
					PCI_VPD_RO_KEYWORD_SERIALNO);
	if (pos < 0)
		goto exit;

	len = pci_vpd_info_field_size(&vpd_data[pos]);
	pos += PCI_VPD_INFO_FLD_HDR_SIZE;
	if (len + pos > vpd_size)
		goto exit;

	size = min(len, BNXT_VPD_FLD_LEN - 1);
	memcpy(bp->board_serialno, &vpd_data[pos], size);
exit:
	kfree(vpd_data);
}

static int bnxt_pcie_dsn_get(struct bnxt *bp, u8 dsn[])
{
	struct pci_dev *pdev = bp->pdev;
	u64 qword;

	qword = pci_get_dsn(pdev);
	if (!qword) {
		netdev_info(bp->dev, "Unable to read adapter's DSN\n");
		return -EOPNOTSUPP;
	}

	put_unaligned_le64(qword, dsn);

	bp->flags |= BNXT_FLAG_DSN_VALID;
	return 0;
}

static int bnxt_map_db_bar(struct bnxt *bp)
{
	if (!bp->db_size)
		return -ENODEV;
	bp->bar1 = pci_iomap(bp->pdev, 2, bp->db_size);
	if (!bp->bar1)
		return -ENOMEM;
	return 0;
}

static int bnxt_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct bnxt *bp;
	int rc, max_irqs;

	if (pci_is_bridge(pdev))
		return -ENODEV;

	/* Clear any pending DMA transactions from crash kernel
	 * while loading driver in capture kernel.
	 */
	if (is_kdump_kernel()) {
		pci_clear_master(pdev);
		pcie_flr(pdev);
	}

	max_irqs = bnxt_get_max_irq(pdev);
	dev = alloc_etherdev_mq(sizeof(*bp), max_irqs);
	if (!dev)
		return -ENOMEM;

	bp = netdev_priv(dev);
	bp->msg_enable = BNXT_DEF_MSG_ENABLE;
	bnxt_set_max_func_irqs(bp, max_irqs);

	if (bnxt_vf_pciid(ent->driver_data))
		bp->flags |= BNXT_FLAG_VF;

	if (pdev->msix_cap)
		bp->flags |= BNXT_FLAG_MSIX_CAP;

	rc = bnxt_init_board(pdev, dev);
	if (rc < 0)
		goto init_err_free;

	dev->netdev_ops = &bnxt_netdev_ops;
	dev->watchdog_timeo = BNXT_TX_TIMEOUT;
	dev->ethtool_ops = &bnxt_ethtool_ops;
	pci_set_drvdata(pdev, dev);

	if (BNXT_PF(bp))
		bnxt_vpd_read_info(bp);

	rc = bnxt_alloc_hwrm_resources(bp);
	if (rc)
		goto init_err_pci_clean;

	mutex_init(&bp->hwrm_cmd_lock);
	mutex_init(&bp->link_lock);

	rc = bnxt_fw_init_one_p1(bp);
	if (rc)
		goto init_err_pci_clean;

	if (BNXT_CHIP_P5(bp)) {
		bp->flags |= BNXT_FLAG_CHIP_P5;
		if (BNXT_CHIP_SR2(bp))
			bp->flags |= BNXT_FLAG_CHIP_SR2;
	}

	rc = bnxt_alloc_rss_indir_tbl(bp);
	if (rc)
		goto init_err_pci_clean;

	rc = bnxt_fw_init_one_p2(bp);
	if (rc)
		goto init_err_pci_clean;

	rc = bnxt_map_db_bar(bp);
	if (rc) {
		dev_err(&pdev->dev, "Cannot map doorbell BAR rc = %d, aborting\n",
			rc);
		goto init_err_pci_clean;
	}

	dev->hw_features = NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_SG |
			   NETIF_F_TSO | NETIF_F_TSO6 |
			   NETIF_F_GSO_UDP_TUNNEL | NETIF_F_GSO_GRE |
			   NETIF_F_GSO_IPXIP4 |
			   NETIF_F_GSO_UDP_TUNNEL_CSUM | NETIF_F_GSO_GRE_CSUM |
			   NETIF_F_GSO_PARTIAL | NETIF_F_RXHASH |
			   NETIF_F_RXCSUM | NETIF_F_GRO;

	if (BNXT_SUPPORTS_TPA(bp))
		dev->hw_features |= NETIF_F_LRO;

	dev->hw_enc_features =
			NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_SG |
			NETIF_F_TSO | NETIF_F_TSO6 |
			NETIF_F_GSO_UDP_TUNNEL | NETIF_F_GSO_GRE |
			NETIF_F_GSO_UDP_TUNNEL_CSUM | NETIF_F_GSO_GRE_CSUM |
			NETIF_F_GSO_IPXIP4 | NETIF_F_GSO_PARTIAL;
	dev->udp_tunnel_nic_info = &bnxt_udp_tunnels;

	dev->gso_partial_features = NETIF_F_GSO_UDP_TUNNEL_CSUM |
				    NETIF_F_GSO_GRE_CSUM;
	dev->vlan_features = dev->hw_features | NETIF_F_HIGHDMA;
	if (bp->fw_cap & BNXT_FW_CAP_VLAN_RX_STRIP)
		dev->hw_features |= BNXT_HW_FEATURE_VLAN_ALL_RX;
	if (bp->fw_cap & BNXT_FW_CAP_VLAN_TX_INSERT)
		dev->hw_features |= BNXT_HW_FEATURE_VLAN_ALL_TX;
	if (BNXT_SUPPORTS_TPA(bp))
		dev->hw_features |= NETIF_F_GRO_HW;
	dev->features |= dev->hw_features | NETIF_F_HIGHDMA;
	if (dev->features & NETIF_F_GRO_HW)
		dev->features &= ~NETIF_F_LRO;
	dev->priv_flags |= IFF_UNICAST_FLT;

#ifdef CONFIG_BNXT_SRIOV
	init_waitqueue_head(&bp->sriov_cfg_wait);
	mutex_init(&bp->sriov_lock);
#endif
	if (BNXT_SUPPORTS_TPA(bp)) {
		bp->gro_func = bnxt_gro_func_5730x;
		if (BNXT_CHIP_P4(bp))
			bp->gro_func = bnxt_gro_func_5731x;
		else if (BNXT_CHIP_P5(bp))
			bp->gro_func = bnxt_gro_func_5750x;
	}
	if (!BNXT_CHIP_P4_PLUS(bp))
		bp->flags |= BNXT_FLAG_DOUBLE_DB;

	bp->ulp_probe = bnxt_ulp_probe;

	rc = bnxt_init_mac_addr(bp);
	if (rc) {
		dev_err(&pdev->dev, "Unable to initialize mac address.\n");
		rc = -EADDRNOTAVAIL;
		goto init_err_pci_clean;
	}

	if (BNXT_PF(bp)) {
		/* Read the adapter's DSN to use as the eswitch switch_id */
		rc = bnxt_pcie_dsn_get(bp, bp->dsn);
	}

	/* MTU range: 60 - FW defined max */
	dev->min_mtu = ETH_ZLEN;
	dev->max_mtu = bp->max_mtu;

	rc = bnxt_probe_phy(bp, true);
	if (rc)
		goto init_err_pci_clean;

	bnxt_set_rx_skb_mode(bp, false);
	bnxt_set_tpa_flags(bp);
	bnxt_set_ring_params(bp);
	rc = bnxt_set_dflt_rings(bp, true);
	if (rc) {
		netdev_err(bp->dev, "Not enough rings available.\n");
		rc = -ENOMEM;
		goto init_err_pci_clean;
	}

	bnxt_fw_init_one_p3(bp);

	if (dev->hw_features & BNXT_HW_FEATURE_VLAN_ALL_RX)
		bp->flags |= BNXT_FLAG_STRIP_VLAN;

	rc = bnxt_init_int_mode(bp);
	if (rc)
		goto init_err_pci_clean;

	/* No TC has been set yet and rings may have been trimmed due to
	 * limited MSIX, so we re-initialize the TX rings per TC.
	 */
	bp->tx_nr_rings_per_tc = bp->tx_nr_rings;

	if (BNXT_PF(bp)) {
		if (!bnxt_pf_wq) {
			bnxt_pf_wq =
				create_singlethread_workqueue("bnxt_pf_wq");
			if (!bnxt_pf_wq) {
				dev_err(&pdev->dev, "Unable to create workqueue.\n");
				rc = -ENOMEM;
				goto init_err_pci_clean;
			}
		}
		rc = bnxt_init_tc(bp);
		if (rc)
			netdev_err(dev, "Failed to initialize TC flower offload, err = %d.\n",
				   rc);
	}

	bnxt_dl_register(bp);

	rc = register_netdev(dev);
	if (rc)
		goto init_err_cleanup;

	if (BNXT_PF(bp))
		devlink_port_type_eth_set(&bp->dl_port, bp->dev);
	bnxt_dl_fw_reporters_create(bp);

	netdev_info(dev, "%s found at mem %lx, node addr %pM\n",
		    board_info[ent->driver_data].name,
		    (long)pci_resource_start(pdev, 0), dev->dev_addr);
	pcie_print_link_status(pdev);

	pci_save_state(pdev);
	return 0;

init_err_cleanup:
	bnxt_dl_unregister(bp);
	bnxt_shutdown_tc(bp);
	bnxt_clear_int_mode(bp);

init_err_pci_clean:
	bnxt_hwrm_func_drv_unrgtr(bp);
	bnxt_free_hwrm_short_cmd_req(bp);
	bnxt_free_hwrm_resources(bp);
	kfree(bp->fw_health);
	bp->fw_health = NULL;
	bnxt_cleanup_pci(bp);
	bnxt_free_ctx_mem(bp);
	kfree(bp->ctx);
	bp->ctx = NULL;
	kfree(bp->rss_indir_tbl);
	bp->rss_indir_tbl = NULL;

init_err_free:
	free_netdev(dev);
	return rc;
}

static void bnxt_shutdown(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnxt *bp;

	if (!dev)
		return;

	rtnl_lock();
	bp = netdev_priv(dev);
	if (!bp)
		goto shutdown_exit;

	if (netif_running(dev))
		dev_close(dev);

	bnxt_ulp_shutdown(bp);
	bnxt_clear_int_mode(bp);
	pci_disable_device(pdev);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, bp->wol);
		pci_set_power_state(pdev, PCI_D3hot);
	}

shutdown_exit:
	rtnl_unlock();
}

#ifdef CONFIG_PM_SLEEP
static int bnxt_suspend(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);
	struct bnxt *bp = netdev_priv(dev);
	int rc = 0;

	rtnl_lock();
	bnxt_ulp_stop(bp);
	if (netif_running(dev)) {
		netif_device_detach(dev);
		rc = bnxt_close(dev);
	}
	bnxt_hwrm_func_drv_unrgtr(bp);
	pci_disable_device(bp->pdev);
	bnxt_free_ctx_mem(bp);
	kfree(bp->ctx);
	bp->ctx = NULL;
	rtnl_unlock();
	return rc;
}

static int bnxt_resume(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);
	struct bnxt *bp = netdev_priv(dev);
	int rc = 0;

	rtnl_lock();
	rc = pci_enable_device(bp->pdev);
	if (rc) {
		netdev_err(dev, "Cannot re-enable PCI device during resume, err = %d\n",
			   rc);
		goto resume_exit;
	}
	pci_set_master(bp->pdev);
	if (bnxt_hwrm_ver_get(bp)) {
		rc = -ENODEV;
		goto resume_exit;
	}
	rc = bnxt_hwrm_func_reset(bp);
	if (rc) {
		rc = -EBUSY;
		goto resume_exit;
	}

	rc = bnxt_hwrm_func_qcaps(bp);
	if (rc)
		goto resume_exit;

	if (bnxt_hwrm_func_drv_rgtr(bp, NULL, 0, false)) {
		rc = -ENODEV;
		goto resume_exit;
	}

	bnxt_get_wol_settings(bp);
	if (netif_running(dev)) {
		rc = bnxt_open(dev);
		if (!rc)
			netif_device_attach(dev);
	}

resume_exit:
	bnxt_ulp_start(bp, rc);
	if (!rc)
		bnxt_reenable_sriov(bp);
	rtnl_unlock();
	return rc;
}

static SIMPLE_DEV_PM_OPS(bnxt_pm_ops, bnxt_suspend, bnxt_resume);
#define BNXT_PM_OPS (&bnxt_pm_ops)

#else

#define BNXT_PM_OPS NULL

#endif /* CONFIG_PM_SLEEP */

/**
 * bnxt_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t bnxt_io_error_detected(struct pci_dev *pdev,
					       pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct bnxt *bp = netdev_priv(netdev);

	netdev_info(netdev, "PCI I/O error detected\n");

	rtnl_lock();
	netif_device_detach(netdev);

	bnxt_ulp_stop(bp);

	if (state == pci_channel_io_perm_failure) {
		rtnl_unlock();
		return PCI_ERS_RESULT_DISCONNECT;
	}

	if (state == pci_channel_io_frozen)
		set_bit(BNXT_STATE_PCI_CHANNEL_IO_FROZEN, &bp->state);

	if (netif_running(netdev))
		bnxt_close(netdev);

	pci_disable_device(pdev);
	bnxt_free_ctx_mem(bp);
	kfree(bp->ctx);
	bp->ctx = NULL;
	rtnl_unlock();

	/* Request a slot slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * bnxt_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot.
 * At this point, the card has exprienced a hard reset,
 * followed by fixups by BIOS, and has its config space
 * set up identically to what it was at cold boot.
 */
static pci_ers_result_t bnxt_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct bnxt *bp = netdev_priv(netdev);
	int err = 0, off;
	pci_ers_result_t result = PCI_ERS_RESULT_DISCONNECT;

	netdev_info(bp->dev, "PCI Slot Reset\n");

	rtnl_lock();

	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev,
			"Cannot re-enable PCI device after reset.\n");
	} else {
		pci_set_master(pdev);
		/* Upon fatal error, our device internal logic that latches to
		 * BAR value is getting reset and will restore only upon
		 * rewritting the BARs.
		 *
		 * As pci_restore_state() does not re-write the BARs if the
		 * value is same as saved value earlier, driver needs to
		 * write the BARs to 0 to force restore, in case of fatal error.
		 */
		if (test_and_clear_bit(BNXT_STATE_PCI_CHANNEL_IO_FROZEN,
				       &bp->state)) {
			for (off = PCI_BASE_ADDRESS_0;
			     off <= PCI_BASE_ADDRESS_5; off += 4)
				pci_write_config_dword(bp->pdev, off, 0);
		}
		pci_restore_state(pdev);
		pci_save_state(pdev);

		err = bnxt_hwrm_func_reset(bp);
		if (!err) {
			err = bnxt_hwrm_func_qcaps(bp);
			if (!err && netif_running(netdev))
				err = bnxt_open(netdev);
		}
		bnxt_ulp_start(bp, err);
		if (!err) {
			bnxt_reenable_sriov(bp);
			result = PCI_ERS_RESULT_RECOVERED;
		}
	}

	if (result != PCI_ERS_RESULT_RECOVERED) {
		if (netif_running(netdev))
			dev_close(netdev);
		pci_disable_device(pdev);
	}

	rtnl_unlock();

	return result;
}

/**
 * bnxt_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells
 * us that its OK to resume normal operation.
 */
static void bnxt_io_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);

	rtnl_lock();

	netif_device_attach(netdev);

	rtnl_unlock();
}

static const struct pci_error_handlers bnxt_err_handler = {
	.error_detected	= bnxt_io_error_detected,
	.slot_reset	= bnxt_io_slot_reset,
	.resume		= bnxt_io_resume
};

static struct pci_driver bnxt_pci_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= bnxt_pci_tbl,
	.probe		= bnxt_init_one,
	.remove		= bnxt_remove_one,
	.shutdown	= bnxt_shutdown,
	.driver.pm	= BNXT_PM_OPS,
	.err_handler	= &bnxt_err_handler,
#if defined(CONFIG_BNXT_SRIOV)
	.sriov_configure = bnxt_sriov_configure,
#endif
};

static int __init bnxt_init(void)
{
	bnxt_debug_init();
	return pci_register_driver(&bnxt_pci_driver);
}

static void __exit bnxt_exit(void)
{
	pci_unregister_driver(&bnxt_pci_driver);
	if (bnxt_pf_wq)
		destroy_workqueue(bnxt_pf_wq);
	bnxt_debug_exit();
}

module_init(bnxt_init);
module_exit(bnxt_exit);
