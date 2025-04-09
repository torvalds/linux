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
#include <net/gro.h>
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
#include <linux/bitmap.h>
#include <linux/cpu_rmap.h>
#include <linux/cpumask.h>
#include <net/pkt_cls.h>
#include <net/page_pool/helpers.h>
#include <linux/align.h>
#include <net/netdev_lock.h>
#include <net/netdev_queues.h>
#include <net/netdev_rx_queue.h>
#include <linux/pci-tph.h>

#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_hwrm.h"
#include "bnxt_ulp.h"
#include "bnxt_sriov.h"
#include "bnxt_ethtool.h"
#include "bnxt_dcb.h"
#include "bnxt_xdp.h"
#include "bnxt_ptp.h"
#include "bnxt_vfr.h"
#include "bnxt_tc.h"
#include "bnxt_devlink.h"
#include "bnxt_debugfs.h"
#include "bnxt_coredump.h"
#include "bnxt_hwmon.h"

#define BNXT_TX_TIMEOUT		(5 * HZ)
#define BNXT_DEF_MSG_ENABLE	(NETIF_MSG_DRV | NETIF_MSG_HW | \
				 NETIF_MSG_TX_ERR)

MODULE_IMPORT_NS("NETDEV_INTERNAL");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Broadcom NetXtreme network driver");

#define BNXT_RX_OFFSET (NET_SKB_PAD + NET_IP_ALIGN)
#define BNXT_RX_DMA_OFFSET NET_SKB_PAD

#define BNXT_TX_PUSH_THRESH 164

/* indexed by enum board_idx */
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
	[BCM57608] = { "Broadcom BCM57608 NetXtreme-E 10Gb/25Gb/50Gb/100Gb/200Gb/400Gb Ethernet" },
	[BCM57604] = { "Broadcom BCM57604 NetXtreme-E 10Gb/25Gb/50Gb/100Gb/200Gb Ethernet" },
	[BCM57602] = { "Broadcom BCM57602 NetXtreme-E 10Gb/25Gb/50Gb/100Gb Ethernet" },
	[BCM57601] = { "Broadcom BCM57601 NetXtreme-E 10Gb/25Gb/50Gb/100Gb/200Gb/400Gb Ethernet" },
	[BCM57508_NPAR] = { "Broadcom BCM57508 NetXtreme-E Ethernet Partition" },
	[BCM57504_NPAR] = { "Broadcom BCM57504 NetXtreme-E Ethernet Partition" },
	[BCM57502_NPAR] = { "Broadcom BCM57502 NetXtreme-E Ethernet Partition" },
	[BCM58802] = { "Broadcom BCM58802 NetXtreme-S 10Gb/25Gb/40Gb/50Gb Ethernet" },
	[BCM58804] = { "Broadcom BCM58804 NetXtreme-S 10Gb/25Gb/40Gb/50Gb/100Gb Ethernet" },
	[BCM58808] = { "Broadcom BCM58808 NetXtreme-S 10Gb/25Gb/40Gb/50Gb/100Gb Ethernet" },
	[NETXTREME_E_VF] = { "Broadcom NetXtreme-E Ethernet Virtual Function" },
	[NETXTREME_C_VF] = { "Broadcom NetXtreme-C Ethernet Virtual Function" },
	[NETXTREME_S_VF] = { "Broadcom NetXtreme-S Ethernet Virtual Function" },
	[NETXTREME_C_VF_HV] = { "Broadcom NetXtreme-C Virtual Function for Hyper-V" },
	[NETXTREME_E_VF_HV] = { "Broadcom NetXtreme-E Virtual Function for Hyper-V" },
	[NETXTREME_E_P5_VF] = { "Broadcom BCM5750X NetXtreme-E Ethernet Virtual Function" },
	[NETXTREME_E_P5_VF_HV] = { "Broadcom BCM5750X NetXtreme-E Virtual Function for Hyper-V" },
	[NETXTREME_E_P7_VF] = { "Broadcom BCM5760X Virtual Function" },
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
	{ PCI_VDEVICE(BROADCOM, 0x1760), .driver_data = BCM57608 },
	{ PCI_VDEVICE(BROADCOM, 0x1761), .driver_data = BCM57604 },
	{ PCI_VDEVICE(BROADCOM, 0x1762), .driver_data = BCM57602 },
	{ PCI_VDEVICE(BROADCOM, 0x1763), .driver_data = BCM57601 },
	{ PCI_VDEVICE(BROADCOM, 0x1800), .driver_data = BCM57502_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x1801), .driver_data = BCM57504_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x1802), .driver_data = BCM57508_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x1803), .driver_data = BCM57502_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x1804), .driver_data = BCM57504_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0x1805), .driver_data = BCM57508_NPAR },
	{ PCI_VDEVICE(BROADCOM, 0xd802), .driver_data = BCM58802 },
	{ PCI_VDEVICE(BROADCOM, 0xd804), .driver_data = BCM58804 },
#ifdef CONFIG_BNXT_SRIOV
	{ PCI_VDEVICE(BROADCOM, 0x1606), .driver_data = NETXTREME_E_VF },
	{ PCI_VDEVICE(BROADCOM, 0x1607), .driver_data = NETXTREME_E_VF_HV },
	{ PCI_VDEVICE(BROADCOM, 0x1608), .driver_data = NETXTREME_E_VF_HV },
	{ PCI_VDEVICE(BROADCOM, 0x1609), .driver_data = NETXTREME_E_VF },
	{ PCI_VDEVICE(BROADCOM, 0x16bd), .driver_data = NETXTREME_E_VF_HV },
	{ PCI_VDEVICE(BROADCOM, 0x16c1), .driver_data = NETXTREME_E_VF },
	{ PCI_VDEVICE(BROADCOM, 0x16c2), .driver_data = NETXTREME_C_VF_HV },
	{ PCI_VDEVICE(BROADCOM, 0x16c3), .driver_data = NETXTREME_C_VF_HV },
	{ PCI_VDEVICE(BROADCOM, 0x16c4), .driver_data = NETXTREME_E_VF_HV },
	{ PCI_VDEVICE(BROADCOM, 0x16c5), .driver_data = NETXTREME_E_VF_HV },
	{ PCI_VDEVICE(BROADCOM, 0x16cb), .driver_data = NETXTREME_C_VF },
	{ PCI_VDEVICE(BROADCOM, 0x16d3), .driver_data = NETXTREME_E_VF },
	{ PCI_VDEVICE(BROADCOM, 0x16dc), .driver_data = NETXTREME_E_VF },
	{ PCI_VDEVICE(BROADCOM, 0x16e1), .driver_data = NETXTREME_C_VF },
	{ PCI_VDEVICE(BROADCOM, 0x16e5), .driver_data = NETXTREME_C_VF },
	{ PCI_VDEVICE(BROADCOM, 0x16e6), .driver_data = NETXTREME_C_VF_HV },
	{ PCI_VDEVICE(BROADCOM, 0x1806), .driver_data = NETXTREME_E_P5_VF },
	{ PCI_VDEVICE(BROADCOM, 0x1807), .driver_data = NETXTREME_E_P5_VF },
	{ PCI_VDEVICE(BROADCOM, 0x1808), .driver_data = NETXTREME_E_P5_VF_HV },
	{ PCI_VDEVICE(BROADCOM, 0x1809), .driver_data = NETXTREME_E_P5_VF_HV },
	{ PCI_VDEVICE(BROADCOM, 0x1819), .driver_data = NETXTREME_E_P7_VF },
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
	ASYNC_EVENT_CMPL_EVENT_ID_DEBUG_NOTIFICATION,
	ASYNC_EVENT_CMPL_EVENT_ID_DEFERRED_RESPONSE,
	ASYNC_EVENT_CMPL_EVENT_ID_RING_MONITOR_MSG,
	ASYNC_EVENT_CMPL_EVENT_ID_ECHO_REQUEST,
	ASYNC_EVENT_CMPL_EVENT_ID_PPS_TIMESTAMP,
	ASYNC_EVENT_CMPL_EVENT_ID_ERROR_REPORT,
	ASYNC_EVENT_CMPL_EVENT_ID_PHC_UPDATE,
	ASYNC_EVENT_CMPL_EVENT_ID_DBG_BUF_PRODUCER,
};

const u16 bnxt_bstore_to_trace[] = {
	[BNXT_CTX_SRT]		= DBG_LOG_BUFFER_FLUSH_REQ_TYPE_SRT_TRACE,
	[BNXT_CTX_SRT2]		= DBG_LOG_BUFFER_FLUSH_REQ_TYPE_SRT2_TRACE,
	[BNXT_CTX_CRT]		= DBG_LOG_BUFFER_FLUSH_REQ_TYPE_CRT_TRACE,
	[BNXT_CTX_CRT2]		= DBG_LOG_BUFFER_FLUSH_REQ_TYPE_CRT2_TRACE,
	[BNXT_CTX_RIGP0]	= DBG_LOG_BUFFER_FLUSH_REQ_TYPE_RIGP0_TRACE,
	[BNXT_CTX_L2HWRM]	= DBG_LOG_BUFFER_FLUSH_REQ_TYPE_L2_HWRM_TRACE,
	[BNXT_CTX_REHWRM]	= DBG_LOG_BUFFER_FLUSH_REQ_TYPE_ROCE_HWRM_TRACE,
	[BNXT_CTX_CA0]		= DBG_LOG_BUFFER_FLUSH_REQ_TYPE_CA0_TRACE,
	[BNXT_CTX_CA1]		= DBG_LOG_BUFFER_FLUSH_REQ_TYPE_CA1_TRACE,
	[BNXT_CTX_CA2]		= DBG_LOG_BUFFER_FLUSH_REQ_TYPE_CA2_TRACE,
	[BNXT_CTX_RIGP1]	= DBG_LOG_BUFFER_FLUSH_REQ_TYPE_RIGP1_TRACE,
};

static struct workqueue_struct *bnxt_pf_wq;

#define BNXT_IPV6_MASK_ALL {{{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, \
			       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }}}
#define BNXT_IPV6_MASK_NONE {{{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }}}

const struct bnxt_flow_masks BNXT_FLOW_MASK_NONE = {
	.ports = {
		.src = 0,
		.dst = 0,
	},
	.addrs = {
		.v6addrs = {
			.src = BNXT_IPV6_MASK_NONE,
			.dst = BNXT_IPV6_MASK_NONE,
		},
	},
};

const struct bnxt_flow_masks BNXT_FLOW_IPV6_MASK_ALL = {
	.ports = {
		.src = cpu_to_be16(0xffff),
		.dst = cpu_to_be16(0xffff),
	},
	.addrs = {
		.v6addrs = {
			.src = BNXT_IPV6_MASK_ALL,
			.dst = BNXT_IPV6_MASK_ALL,
		},
	},
};

const struct bnxt_flow_masks BNXT_FLOW_IPV4_MASK_ALL = {
	.ports = {
		.src = cpu_to_be16(0xffff),
		.dst = cpu_to_be16(0xffff),
	},
	.addrs = {
		.v4addrs = {
			.src = cpu_to_be32(0xffffffff),
			.dst = cpu_to_be32(0xffffffff),
		},
	},
};

static bool bnxt_vf_pciid(enum board_idx idx)
{
	return (idx == NETXTREME_C_VF || idx == NETXTREME_E_VF ||
		idx == NETXTREME_S_VF || idx == NETXTREME_C_VF_HV ||
		idx == NETXTREME_E_VF_HV || idx == NETXTREME_E_P5_VF ||
		idx == NETXTREME_E_P5_VF_HV || idx == NETXTREME_E_P7_VF);
}

#define DB_CP_REARM_FLAGS	(DB_KEY_CP | DB_IDX_VALID)
#define DB_CP_FLAGS		(DB_KEY_CP | DB_IDX_VALID | DB_IRQ_DIS)

#define BNXT_DB_CQ(db, idx)						\
	writel(DB_CP_FLAGS | DB_RING_IDX(db, idx), (db)->doorbell)

#define BNXT_DB_NQ_P5(db, idx)						\
	bnxt_writeq(bp, (db)->db_key64 | DBR_TYPE_NQ | DB_RING_IDX(db, idx),\
		    (db)->doorbell)

#define BNXT_DB_NQ_P7(db, idx)						\
	bnxt_writeq(bp, (db)->db_key64 | DBR_TYPE_NQ_MASK |		\
		    DB_RING_IDX(db, idx), (db)->doorbell)

#define BNXT_DB_CQ_ARM(db, idx)						\
	writel(DB_CP_REARM_FLAGS | DB_RING_IDX(db, idx), (db)->doorbell)

#define BNXT_DB_NQ_ARM_P5(db, idx)					\
	bnxt_writeq(bp, (db)->db_key64 | DBR_TYPE_NQ_ARM |		\
		    DB_RING_IDX(db, idx), (db)->doorbell)

static void bnxt_db_nq(struct bnxt *bp, struct bnxt_db_info *db, u32 idx)
{
	if (bp->flags & BNXT_FLAG_CHIP_P7)
		BNXT_DB_NQ_P7(db, idx);
	else if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		BNXT_DB_NQ_P5(db, idx);
	else
		BNXT_DB_CQ(db, idx);
}

static void bnxt_db_nq_arm(struct bnxt *bp, struct bnxt_db_info *db, u32 idx)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		BNXT_DB_NQ_ARM_P5(db, idx);
	else
		BNXT_DB_CQ_ARM(db, idx);
}

static void bnxt_db_cq(struct bnxt *bp, struct bnxt_db_info *db, u32 idx)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		bnxt_writeq(bp, db->db_key64 | DBR_TYPE_CQ_ARMALL |
			    DB_RING_IDX(db, idx), db->doorbell);
	else
		BNXT_DB_CQ(db, idx);
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

static void __bnxt_queue_sp_work(struct bnxt *bp)
{
	if (BNXT_PF(bp))
		queue_work(bnxt_pf_wq, &bp->sp_task);
	else
		schedule_work(&bp->sp_task);
}

static void bnxt_queue_sp_work(struct bnxt *bp, unsigned int event)
{
	set_bit(event, &bp->sp_event);
	__bnxt_queue_sp_work(bp);
}

static void bnxt_sched_reset_rxr(struct bnxt *bp, struct bnxt_rx_ring_info *rxr)
{
	if (!rxr->bnapi->in_reset) {
		rxr->bnapi->in_reset = true;
		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
			set_bit(BNXT_RESET_TASK_SP_EVENT, &bp->sp_event);
		else
			set_bit(BNXT_RST_RING_SP_EVENT, &bp->sp_event);
		__bnxt_queue_sp_work(bp);
	}
	rxr->rx_next_cons = 0xffff;
}

void bnxt_sched_reset_txr(struct bnxt *bp, struct bnxt_tx_ring_info *txr,
			  u16 curr)
{
	struct bnxt_napi *bnapi = txr->bnapi;

	if (bnapi->tx_fault)
		return;

	netdev_err(bp->dev, "Invalid Tx completion (ring:%d tx_hw_cons:%u cons:%u prod:%u curr:%u)",
		   txr->txq_index, txr->tx_hw_cons,
		   txr->tx_cons, txr->tx_prod, curr);
	WARN_ON_ONCE(1);
	bnapi->tx_fault = 1;
	bnxt_queue_sp_work(bp, BNXT_RESET_TASK_SP_EVENT);
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

static void bnxt_txr_db_kick(struct bnxt *bp, struct bnxt_tx_ring_info *txr,
			     u16 prod)
{
	/* Sync BD data before updating doorbell */
	wmb();
	bnxt_db_write(bp, &txr->tx_db, prod);
	txr->kick_pending = 0;
}

static netdev_tx_t bnxt_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);
	struct tx_bd *txbd, *txbd0;
	struct tx_bd_ext *txbd1;
	struct netdev_queue *txq;
	int i;
	dma_addr_t mapping;
	unsigned int length, pad = 0;
	u32 len, free_size, vlan_tag_flags, cfa_action, flags;
	struct bnxt_ptp_cfg *ptp = bp->ptp_cfg;
	struct pci_dev *pdev = bp->pdev;
	u16 prod, last_frag, txts_prod;
	struct bnxt_tx_ring_info *txr;
	struct bnxt_sw_tx_bd *tx_buf;
	__le32 lflags = 0;

	i = skb_get_queue_mapping(skb);
	if (unlikely(i >= bp->tx_nr_rings)) {
		dev_kfree_skb_any(skb);
		dev_core_stats_tx_dropped_inc(dev);
		return NETDEV_TX_OK;
	}

	txq = netdev_get_tx_queue(dev, i);
	txr = &bp->tx_ring[bp->tx_ring_map[i]];
	prod = txr->tx_prod;

#if (MAX_SKB_FRAGS > TX_MAX_FRAGS)
	if (skb_shinfo(skb)->nr_frags > TX_MAX_FRAGS) {
		netdev_warn_once(dev, "SKB has too many (%d) fragments, max supported is %d.  SKB will be linearized.\n",
				 skb_shinfo(skb)->nr_frags, TX_MAX_FRAGS);
		if (skb_linearize(skb)) {
			dev_kfree_skb_any(skb);
			dev_core_stats_tx_dropped_inc(dev);
			return NETDEV_TX_OK;
		}
	}
#endif
	free_size = bnxt_tx_avail(bp, txr);
	if (unlikely(free_size < skb_shinfo(skb)->nr_frags + 2)) {
		/* We must have raced with NAPI cleanup */
		if (net_ratelimit() && txr->kick_pending)
			netif_warn(bp, tx_err, dev,
				   "bnxt: ring busy w/ flush pending!\n");
		if (!netif_txq_try_stop(txq, bnxt_tx_avail(bp, txr),
					bp->tx_wake_thresh))
			return NETDEV_TX_BUSY;
	}

	if (unlikely(ipv6_hopopt_jumbo_remove(skb)))
		goto tx_free;

	length = skb->len;
	len = skb_headlen(skb);
	last_frag = skb_shinfo(skb)->nr_frags;

	txbd = &txr->tx_desc_ring[TX_RING(bp, prod)][TX_IDX(prod)];

	tx_buf = &txr->tx_buf_ring[RING_TX(bp, prod)];
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

	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) && ptp &&
	    ptp->tx_tstamp_en) {
		if (bp->fw_cap & BNXT_FW_CAP_TX_TS_CMP) {
			lflags |= cpu_to_le32(TX_BD_FLAGS_STAMP);
			tx_buf->is_ts_pkt = 1;
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		} else if (!skb_is_gso(skb)) {
			u16 seq_id, hdr_off;

			if (!bnxt_ptp_parse(skb, &seq_id, &hdr_off) &&
			    !bnxt_ptp_get_txts_prod(ptp, &txts_prod)) {
				if (vlan_tag_flags)
					hdr_off += VLAN_HLEN;
				lflags |= cpu_to_le32(TX_BD_FLAGS_STAMP);
				tx_buf->is_ts_pkt = 1;
				skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

				ptp->txts_req[txts_prod].tx_seqid = seq_id;
				ptp->txts_req[txts_prod].tx_hdr_off = hdr_off;
				tx_buf->txts_prod = txts_prod;
			}
		}
	}
	if (unlikely(skb->no_fcs))
		lflags |= cpu_to_le32(TX_BD_FLAGS_NO_CRC);

	if (free_size == bp->tx_ring_size && length <= bp->tx_push_thresh &&
	    !lflags) {
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
					TX_BD_CNT(2));

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
		txbd->tx_bd_opaque = SET_TX_OPAQUE(bp, txr, prod, 2);
		prod = NEXT_TX(prod);
		tx_push->tx_bd_opaque = txbd->tx_bd_opaque;
		txbd = &txr->tx_desc_ring[TX_RING(bp, prod)][TX_IDX(prod)];
		memcpy(txbd, tx_push1, sizeof(*txbd));
		prod = NEXT_TX(prod);
		tx_push->doorbell =
			cpu_to_le32(DB_KEY_TX_PUSH | DB_LONG_TX_PUSH |
				    DB_RING_IDX(&txr->tx_db, prod));
		WRITE_ONCE(txr->tx_prod, prod);

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
		if (skb_pad(skb, pad))
			/* SKB already freed. */
			goto tx_kick_pending;
		length = BNXT_MIN_PKT_SIZE;
	}

	mapping = dma_map_single(&pdev->dev, skb->data, len, DMA_TO_DEVICE);

	if (unlikely(dma_mapping_error(&pdev->dev, mapping)))
		goto tx_free;

	dma_unmap_addr_set(tx_buf, mapping, mapping);
	flags = (len << TX_BD_LEN_SHIFT) | TX_BD_TYPE_LONG_TX_BD |
		TX_BD_CNT(last_frag + 2);

	txbd->tx_bd_haddr = cpu_to_le64(mapping);
	txbd->tx_bd_opaque = SET_TX_OPAQUE(bp, txr, prod, 2 + last_frag);

	prod = NEXT_TX(prod);
	txbd1 = (struct tx_bd_ext *)
		&txr->tx_desc_ring[TX_RING(bp, prod)][TX_IDX(prod)];

	txbd1->tx_bd_hsize_lflags = lflags;
	if (skb_is_gso(skb)) {
		bool udp_gso = !!(skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4);
		u32 hdr_len;

		if (skb->encapsulation) {
			if (udp_gso)
				hdr_len = skb_inner_transport_offset(skb) +
					  sizeof(struct udphdr);
			else
				hdr_len = skb_inner_tcp_all_headers(skb);
		} else if (udp_gso) {
			hdr_len = skb_transport_offset(skb) +
				  sizeof(struct udphdr);
		} else {
			hdr_len = skb_tcp_all_headers(skb);
		}

		txbd1->tx_bd_hsize_lflags |= cpu_to_le32(TX_BD_FLAGS_LSO |
					TX_BD_FLAGS_T_IPID |
					(hdr_len << (TX_BD_HSIZE_SHIFT - 1)));
		length = skb_shinfo(skb)->gso_size;
		txbd1->tx_bd_mss = cpu_to_le32(length);
		length += hdr_len;
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		txbd1->tx_bd_hsize_lflags |=
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
	txbd0 = txbd;
	for (i = 0; i < last_frag; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		prod = NEXT_TX(prod);
		txbd = &txr->tx_desc_ring[TX_RING(bp, prod)][TX_IDX(prod)];

		len = skb_frag_size(frag);
		mapping = skb_frag_dma_map(&pdev->dev, frag, 0, len,
					   DMA_TO_DEVICE);

		if (unlikely(dma_mapping_error(&pdev->dev, mapping)))
			goto tx_dma_error;

		tx_buf = &txr->tx_buf_ring[RING_TX(bp, prod)];
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

	skb_tx_timestamp(skb);

	prod = NEXT_TX(prod);
	WRITE_ONCE(txr->tx_prod, prod);

	if (!netdev_xmit_more() || netif_xmit_stopped(txq)) {
		bnxt_txr_db_kick(bp, txr, prod);
	} else {
		if (free_size >= bp->tx_wake_thresh)
			txbd0->tx_bd_len_flags_type |=
				cpu_to_le32(TX_BD_FLAGS_NO_CMPL);
		txr->kick_pending = 1;
	}

tx_done:

	if (unlikely(bnxt_tx_avail(bp, txr) <= MAX_SKB_FRAGS + 1)) {
		if (netdev_xmit_more() && !tx_buf->is_push) {
			txbd0->tx_bd_len_flags_type &=
				cpu_to_le32(~TX_BD_FLAGS_NO_CMPL);
			bnxt_txr_db_kick(bp, txr, prod);
		}

		netif_txq_try_stop(txq, bnxt_tx_avail(bp, txr),
				   bp->tx_wake_thresh);
	}
	return NETDEV_TX_OK;

tx_dma_error:
	last_frag = i;

	/* start back at beginning and unmap skb */
	prod = txr->tx_prod;
	tx_buf = &txr->tx_buf_ring[RING_TX(bp, prod)];
	dma_unmap_single(&pdev->dev, dma_unmap_addr(tx_buf, mapping),
			 skb_headlen(skb), DMA_TO_DEVICE);
	prod = NEXT_TX(prod);

	/* unmap remaining mapped pages */
	for (i = 0; i < last_frag; i++) {
		prod = NEXT_TX(prod);
		tx_buf = &txr->tx_buf_ring[RING_TX(bp, prod)];
		dma_unmap_page(&pdev->dev, dma_unmap_addr(tx_buf, mapping),
			       skb_frag_size(&skb_shinfo(skb)->frags[i]),
			       DMA_TO_DEVICE);
	}

tx_free:
	dev_kfree_skb_any(skb);
tx_kick_pending:
	if (BNXT_TX_PTP_IS_SET(lflags)) {
		txr->tx_buf_ring[txr->tx_prod].is_ts_pkt = 0;
		atomic64_inc(&bp->ptp_cfg->stats.ts_err);
		if (!(bp->fw_cap & BNXT_FW_CAP_TX_TS_CMP))
			/* set SKB to err so PTP worker will clean up */
			ptp->txts_req[txts_prod].tx_skb = ERR_PTR(-EIO);
	}
	if (txr->kick_pending)
		bnxt_txr_db_kick(bp, txr, txr->tx_prod);
	txr->tx_buf_ring[txr->tx_prod].skb = NULL;
	dev_core_stats_tx_dropped_inc(dev);
	return NETDEV_TX_OK;
}

/* Returns true if some remaining TX packets not processed. */
static bool __bnxt_tx_int(struct bnxt *bp, struct bnxt_tx_ring_info *txr,
			  int budget)
{
	struct netdev_queue *txq = netdev_get_tx_queue(bp->dev, txr->txq_index);
	struct pci_dev *pdev = bp->pdev;
	u16 hw_cons = txr->tx_hw_cons;
	unsigned int tx_bytes = 0;
	u16 cons = txr->tx_cons;
	int tx_pkts = 0;
	bool rc = false;

	while (RING_TX(bp, cons) != hw_cons) {
		struct bnxt_sw_tx_bd *tx_buf;
		struct sk_buff *skb;
		bool is_ts_pkt;
		int j, last;

		tx_buf = &txr->tx_buf_ring[RING_TX(bp, cons)];
		skb = tx_buf->skb;

		if (unlikely(!skb)) {
			bnxt_sched_reset_txr(bp, txr, cons);
			return rc;
		}

		is_ts_pkt = tx_buf->is_ts_pkt;
		if (is_ts_pkt && (bp->fw_cap & BNXT_FW_CAP_TX_TS_CMP)) {
			rc = true;
			break;
		}

		cons = NEXT_TX(cons);
		tx_pkts++;
		tx_bytes += skb->len;
		tx_buf->skb = NULL;
		tx_buf->is_ts_pkt = 0;

		if (tx_buf->is_push) {
			tx_buf->is_push = 0;
			goto next_tx_int;
		}

		dma_unmap_single(&pdev->dev, dma_unmap_addr(tx_buf, mapping),
				 skb_headlen(skb), DMA_TO_DEVICE);
		last = tx_buf->nr_frags;

		for (j = 0; j < last; j++) {
			cons = NEXT_TX(cons);
			tx_buf = &txr->tx_buf_ring[RING_TX(bp, cons)];
			dma_unmap_page(
				&pdev->dev,
				dma_unmap_addr(tx_buf, mapping),
				skb_frag_size(&skb_shinfo(skb)->frags[j]),
				DMA_TO_DEVICE);
		}
		if (unlikely(is_ts_pkt)) {
			if (BNXT_CHIP_P5(bp)) {
				/* PTP worker takes ownership of the skb */
				bnxt_get_tx_ts_p5(bp, skb, tx_buf->txts_prod);
				skb = NULL;
			}
		}

next_tx_int:
		cons = NEXT_TX(cons);

		dev_consume_skb_any(skb);
	}

	WRITE_ONCE(txr->tx_cons, cons);

	__netif_txq_completed_wake(txq, tx_pkts, tx_bytes,
				   bnxt_tx_avail(bp, txr), bp->tx_wake_thresh,
				   READ_ONCE(txr->dev_state) == BNXT_DEV_STATE_CLOSING);

	return rc;
}

static void bnxt_tx_int(struct bnxt *bp, struct bnxt_napi *bnapi, int budget)
{
	struct bnxt_tx_ring_info *txr;
	bool more = false;
	int i;

	bnxt_for_each_napi_tx(i, bnapi, txr) {
		if (txr->tx_hw_cons != RING_TX(bp, txr->tx_cons))
			more |= __bnxt_tx_int(bp, txr, budget);
	}
	if (!more)
		bnapi->events &= ~BNXT_TX_CMP_EVENT;
}

static bool bnxt_separate_head_pool(void)
{
	return PAGE_SIZE > BNXT_RX_PAGE_SIZE;
}

static struct page *__bnxt_alloc_rx_page(struct bnxt *bp, dma_addr_t *mapping,
					 struct bnxt_rx_ring_info *rxr,
					 unsigned int *offset,
					 gfp_t gfp)
{
	struct page *page;

	if (PAGE_SIZE > BNXT_RX_PAGE_SIZE) {
		page = page_pool_dev_alloc_frag(rxr->page_pool, offset,
						BNXT_RX_PAGE_SIZE);
	} else {
		page = page_pool_dev_alloc_pages(rxr->page_pool);
		*offset = 0;
	}
	if (!page)
		return NULL;

	*mapping = page_pool_get_dma_addr(page) + *offset;
	return page;
}

static inline u8 *__bnxt_alloc_rx_frag(struct bnxt *bp, dma_addr_t *mapping,
				       struct bnxt_rx_ring_info *rxr,
				       gfp_t gfp)
{
	unsigned int offset;
	struct page *page;

	page = page_pool_alloc_frag(rxr->head_pool, &offset,
				    bp->rx_buf_size, gfp);
	if (!page)
		return NULL;

	*mapping = page_pool_get_dma_addr(page) + bp->rx_dma_offset + offset;
	return page_address(page) + offset;
}

int bnxt_alloc_rx_data(struct bnxt *bp, struct bnxt_rx_ring_info *rxr,
		       u16 prod, gfp_t gfp)
{
	struct rx_bd *rxbd = &rxr->rx_desc_ring[RX_RING(bp, prod)][RX_IDX(prod)];
	struct bnxt_sw_rx_bd *rx_buf = &rxr->rx_buf_ring[RING_RX(bp, prod)];
	dma_addr_t mapping;

	if (BNXT_RX_PAGE_MODE(bp)) {
		unsigned int offset;
		struct page *page =
			__bnxt_alloc_rx_page(bp, &mapping, rxr, &offset, gfp);

		if (!page)
			return -ENOMEM;

		mapping += bp->rx_dma_offset;
		rx_buf->data = page;
		rx_buf->data_ptr = page_address(page) + offset + bp->rx_offset;
	} else {
		u8 *data = __bnxt_alloc_rx_frag(bp, &mapping, rxr, gfp);

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
	struct bnxt *bp = rxr->bnapi->bp;
	struct rx_bd *cons_bd, *prod_bd;

	prod_rx_buf = &rxr->rx_buf_ring[RING_RX(bp, prod)];
	cons_rx_buf = &rxr->rx_buf_ring[cons];

	prod_rx_buf->data = data;
	prod_rx_buf->data_ptr = cons_rx_buf->data_ptr;

	prod_rx_buf->mapping = cons_rx_buf->mapping;

	prod_bd = &rxr->rx_desc_ring[RX_RING(bp, prod)][RX_IDX(prod)];
	cons_bd = &rxr->rx_desc_ring[RX_RING(bp, cons)][RX_IDX(cons)];

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
		&rxr->rx_agg_desc_ring[RX_AGG_RING(bp, prod)][RX_IDX(prod)];
	struct bnxt_sw_rx_agg_bd *rx_agg_buf;
	struct page *page;
	dma_addr_t mapping;
	u16 sw_prod = rxr->rx_sw_agg_prod;
	unsigned int offset = 0;

	page = __bnxt_alloc_rx_page(bp, &mapping, rxr, &offset, gfp);

	if (!page)
		return -ENOMEM;

	if (unlikely(test_bit(sw_prod, rxr->rx_agg_bmap)))
		sw_prod = bnxt_find_next_agg_idx(rxr, sw_prod);

	__set_bit(sw_prod, rxr->rx_agg_bmap);
	rx_agg_buf = &rxr->rx_agg_ring[sw_prod];
	rxr->rx_sw_agg_prod = RING_RX_AGG(bp, NEXT_RX_AGG(sw_prod));

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

	if ((bp->flags & BNXT_FLAG_CHIP_P5_PLUS) && tpa)
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

		prod_bd = &rxr->rx_agg_desc_ring[RX_AGG_RING(bp, prod)][RX_IDX(prod)];

		prod_bd->rx_bd_haddr = cpu_to_le64(cons_rx_buf->mapping);
		prod_bd->rx_bd_opaque = sw_prod;

		prod = NEXT_RX_AGG(prod);
		sw_prod = RING_RX_AGG(bp, NEXT_RX_AGG(sw_prod));
	}
	rxr->rx_agg_prod = prod;
	rxr->rx_sw_agg_prod = sw_prod;
}

static struct sk_buff *bnxt_rx_multi_page_skb(struct bnxt *bp,
					      struct bnxt_rx_ring_info *rxr,
					      u16 cons, void *data, u8 *data_ptr,
					      dma_addr_t dma_addr,
					      unsigned int offset_and_len)
{
	unsigned int len = offset_and_len & 0xffff;
	struct page *page = data;
	u16 prod = rxr->rx_prod;
	struct sk_buff *skb;
	int err;

	err = bnxt_alloc_rx_data(bp, rxr, prod, GFP_ATOMIC);
	if (unlikely(err)) {
		bnxt_reuse_rx_data(rxr, cons, data);
		return NULL;
	}
	dma_addr -= bp->rx_dma_offset;
	dma_sync_single_for_cpu(&bp->pdev->dev, dma_addr, BNXT_RX_PAGE_SIZE,
				bp->rx_dir);
	skb = napi_build_skb(data_ptr - bp->rx_offset, BNXT_RX_PAGE_SIZE);
	if (!skb) {
		page_pool_recycle_direct(rxr->page_pool, page);
		return NULL;
	}
	skb_mark_for_recycle(skb);
	skb_reserve(skb, bp->rx_offset);
	__skb_put(skb, len);

	return skb;
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
	dma_sync_single_for_cpu(&bp->pdev->dev, dma_addr, BNXT_RX_PAGE_SIZE,
				bp->rx_dir);

	if (unlikely(!payload))
		payload = eth_get_headlen(bp->dev, data_ptr, len);

	skb = napi_alloc_skb(&rxr->bnapi->napi, payload);
	if (!skb) {
		page_pool_recycle_direct(rxr->page_pool, page);
		return NULL;
	}

	skb_mark_for_recycle(skb);
	off = (void *)data_ptr - page_address(page);
	skb_add_rx_frag(skb, 0, page, off, len, BNXT_RX_PAGE_SIZE);
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

	skb = napi_build_skb(data, bp->rx_buf_size);
	dma_sync_single_for_cpu(&bp->pdev->dev, dma_addr, bp->rx_buf_use_size,
				bp->rx_dir);
	if (!skb) {
		page_pool_free_va(rxr->head_pool, data, true);
		return NULL;
	}

	skb_mark_for_recycle(skb);
	skb_reserve(skb, bp->rx_offset);
	skb_put(skb, offset_and_len & 0xffff);
	return skb;
}

static u32 __bnxt_rx_agg_pages(struct bnxt *bp,
			       struct bnxt_cp_ring_info *cpr,
			       struct skb_shared_info *shinfo,
			       u16 idx, u32 agg_bufs, bool tpa,
			       struct xdp_buff *xdp)
{
	struct bnxt_napi *bnapi = cpr->bnapi;
	struct pci_dev *pdev = bp->pdev;
	struct bnxt_rx_ring_info *rxr = bnapi->rx_ring;
	u16 prod = rxr->rx_agg_prod;
	u32 i, total_frag_len = 0;
	bool p5_tpa = false;

	if ((bp->flags & BNXT_FLAG_CHIP_P5_PLUS) && tpa)
		p5_tpa = true;

	for (i = 0; i < agg_bufs; i++) {
		skb_frag_t *frag = &shinfo->frags[i];
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
		skb_frag_fill_page_desc(frag, cons_rx_buf->page,
					cons_rx_buf->offset, frag_len);
		shinfo->nr_frags = i + 1;
		__clear_bit(cons, rxr->rx_agg_bmap);

		/* It is possible for bnxt_alloc_rx_page() to allocate
		 * a sw_prod index that equals the cons index, so we
		 * need to clear the cons entry now.
		 */
		mapping = cons_rx_buf->mapping;
		page = cons_rx_buf->page;
		cons_rx_buf->page = NULL;

		if (xdp && page_is_pfmemalloc(page))
			xdp_buff_set_frag_pfmemalloc(xdp);

		if (bnxt_alloc_rx_page(bp, rxr, prod, GFP_ATOMIC) != 0) {
			--shinfo->nr_frags;
			cons_rx_buf->page = page;

			/* Update prod since possibly some pages have been
			 * allocated already.
			 */
			rxr->rx_agg_prod = prod;
			bnxt_reuse_rx_agg_bufs(cpr, idx, i, agg_bufs - i, tpa);
			return 0;
		}

		dma_sync_single_for_cpu(&pdev->dev, mapping, BNXT_RX_PAGE_SIZE,
					bp->rx_dir);

		total_frag_len += frag_len;
		prod = NEXT_RX_AGG(prod);
	}
	rxr->rx_agg_prod = prod;
	return total_frag_len;
}

static struct sk_buff *bnxt_rx_agg_pages_skb(struct bnxt *bp,
					     struct bnxt_cp_ring_info *cpr,
					     struct sk_buff *skb, u16 idx,
					     u32 agg_bufs, bool tpa)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	u32 total_frag_len = 0;

	total_frag_len = __bnxt_rx_agg_pages(bp, cpr, shinfo, idx,
					     agg_bufs, tpa, NULL);
	if (!total_frag_len) {
		skb_mark_for_recycle(skb);
		dev_kfree_skb(skb);
		return NULL;
	}

	skb->data_len += total_frag_len;
	skb->len += total_frag_len;
	skb->truesize += BNXT_RX_PAGE_SIZE * agg_bufs;
	return skb;
}

static u32 bnxt_rx_agg_pages_xdp(struct bnxt *bp,
				 struct bnxt_cp_ring_info *cpr,
				 struct xdp_buff *xdp, u16 idx,
				 u32 agg_bufs, bool tpa)
{
	struct skb_shared_info *shinfo = xdp_get_shared_info_from_buff(xdp);
	u32 total_frag_len = 0;

	if (!xdp_buff_has_frags(xdp))
		shinfo->nr_frags = 0;

	total_frag_len = __bnxt_rx_agg_pages(bp, cpr, shinfo,
					     idx, agg_bufs, tpa, xdp);
	if (total_frag_len) {
		xdp_buff_set_frags_flag(xdp);
		shinfo->nr_frags = agg_bufs;
		shinfo->xdp_frags_size = total_frag_len;
	}
	return total_frag_len;
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

static struct sk_buff *bnxt_copy_data(struct bnxt_napi *bnapi, u8 *data,
				      unsigned int len,
				      dma_addr_t mapping)
{
	struct bnxt *bp = bnapi->bp;
	struct pci_dev *pdev = bp->pdev;
	struct sk_buff *skb;

	skb = napi_alloc_skb(&bnapi->napi, len);
	if (!skb)
		return NULL;

	dma_sync_single_for_cpu(&pdev->dev, mapping, bp->rx_copybreak,
				bp->rx_dir);

	memcpy(skb->data - NET_IP_ALIGN, data - NET_IP_ALIGN,
	       len + NET_IP_ALIGN);

	dma_sync_single_for_device(&pdev->dev, mapping, bp->rx_copybreak,
				   bp->rx_dir);

	skb_put(skb, len);

	return skb;
}

static struct sk_buff *bnxt_copy_skb(struct bnxt_napi *bnapi, u8 *data,
				     unsigned int len,
				     dma_addr_t mapping)
{
	return bnxt_copy_data(bnapi, data, len, mapping);
}

static struct sk_buff *bnxt_copy_xdp(struct bnxt_napi *bnapi,
				     struct xdp_buff *xdp,
				     unsigned int len,
				     dma_addr_t mapping)
{
	unsigned int metasize = 0;
	u8 *data = xdp->data;
	struct sk_buff *skb;

	len = xdp->data_end - xdp->data_meta;
	metasize = xdp->data - xdp->data_meta;
	data = xdp->data_meta;

	skb = bnxt_copy_data(bnapi, data, len, mapping);
	if (!skb)
		return skb;

	if (metasize) {
		skb_metadata_set(skb, metasize);
		__skb_pull(skb, metasize);
	}

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

		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
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

static void bnxt_tpa_metadata(struct bnxt_tpa_info *tpa_info,
			      struct rx_tpa_start_cmp *tpa_start,
			      struct rx_tpa_start_cmp_ext *tpa_start1)
{
	tpa_info->cfa_code_valid = 1;
	tpa_info->cfa_code = TPA_START_CFA_CODE(tpa_start1);
	tpa_info->vlan_valid = 0;
	if (tpa_info->flags2 & RX_CMP_FLAGS2_META_FORMAT_VLAN) {
		tpa_info->vlan_valid = 1;
		tpa_info->metadata =
			le32_to_cpu(tpa_start1->rx_tpa_start_cmp_metadata);
	}
}

static void bnxt_tpa_metadata_v2(struct bnxt_tpa_info *tpa_info,
				 struct rx_tpa_start_cmp *tpa_start,
				 struct rx_tpa_start_cmp_ext *tpa_start1)
{
	tpa_info->vlan_valid = 0;
	if (TPA_START_VLAN_VALID(tpa_start)) {
		u32 tpid_sel = TPA_START_VLAN_TPID_SEL(tpa_start);
		u32 vlan_proto = ETH_P_8021Q;

		tpa_info->vlan_valid = 1;
		if (tpid_sel == RX_TPA_START_METADATA1_TPID_8021AD)
			vlan_proto = ETH_P_8021AD;
		tpa_info->metadata = vlan_proto << 16 |
				     TPA_START_METADATA0_TCI(tpa_start1);
	}
}

static void bnxt_tpa_start(struct bnxt *bp, struct bnxt_rx_ring_info *rxr,
			   u8 cmp_type, struct rx_tpa_start_cmp *tpa_start,
			   struct rx_tpa_start_cmp_ext *tpa_start1)
{
	struct bnxt_sw_rx_bd *cons_rx_buf, *prod_rx_buf;
	struct bnxt_tpa_info *tpa_info;
	u16 cons, prod, agg_id;
	struct rx_bd *prod_bd;
	dma_addr_t mapping;

	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
		agg_id = TPA_START_AGG_ID_P5(tpa_start);
		agg_id = bnxt_alloc_agg_idx(rxr, agg_id);
	} else {
		agg_id = TPA_START_AGG_ID(tpa_start);
	}
	cons = tpa_start->rx_tpa_start_cmp_opaque;
	prod = rxr->rx_prod;
	cons_rx_buf = &rxr->rx_buf_ring[cons];
	prod_rx_buf = &rxr->rx_buf_ring[RING_RX(bp, prod)];
	tpa_info = &rxr->rx_tpa[agg_id];

	if (unlikely(cons != rxr->rx_next_cons ||
		     TPA_START_ERROR(tpa_start))) {
		netdev_warn(bp->dev, "TPA cons %x, expected cons %x, error code %x\n",
			    cons, rxr->rx_next_cons,
			    TPA_START_ERROR_CODE(tpa_start1));
		bnxt_sched_reset_rxr(bp, rxr);
		return;
	}
	prod_rx_buf->data = tpa_info->data;
	prod_rx_buf->data_ptr = tpa_info->data_ptr;

	mapping = tpa_info->mapping;
	prod_rx_buf->mapping = mapping;

	prod_bd = &rxr->rx_desc_ring[RX_RING(bp, prod)][RX_IDX(prod)];

	prod_bd->rx_bd_haddr = cpu_to_le64(mapping);

	tpa_info->data = cons_rx_buf->data;
	tpa_info->data_ptr = cons_rx_buf->data_ptr;
	cons_rx_buf->data = NULL;
	tpa_info->mapping = cons_rx_buf->mapping;

	tpa_info->len =
		le32_to_cpu(tpa_start->rx_tpa_start_cmp_len_flags_type) >>
				RX_TPA_START_CMP_LEN_SHIFT;
	if (likely(TPA_START_HASH_VALID(tpa_start))) {
		tpa_info->hash_type = PKT_HASH_TYPE_L4;
		tpa_info->gso_type = SKB_GSO_TCPV4;
		if (TPA_START_IS_IPV6(tpa_start1))
			tpa_info->gso_type = SKB_GSO_TCPV6;
		/* RSS profiles 1 and 3 with extract code 0 for inner 4-tuple */
		else if (!BNXT_CHIP_P4_PLUS(bp) &&
			 TPA_START_HASH_TYPE(tpa_start) == 3)
			tpa_info->gso_type = SKB_GSO_TCPV6;
		tpa_info->rss_hash =
			le32_to_cpu(tpa_start->rx_tpa_start_cmp_rss_hash);
	} else {
		tpa_info->hash_type = PKT_HASH_TYPE_NONE;
		tpa_info->gso_type = 0;
		netif_warn(bp, rx_err, bp->dev, "TPA packet without valid hash\n");
	}
	tpa_info->flags2 = le32_to_cpu(tpa_start1->rx_tpa_start_cmp_flags2);
	tpa_info->hdr_info = le32_to_cpu(tpa_start1->rx_tpa_start_cmp_hdr_info);
	if (cmp_type == CMP_TYPE_RX_L2_TPA_START_CMP)
		bnxt_tpa_metadata(tpa_info, tpa_start, tpa_start1);
	else
		bnxt_tpa_metadata_v2(tpa_info, tpa_start, tpa_start1);
	tpa_info->agg_count = 0;

	rxr->rx_prod = NEXT_RX(prod);
	cons = RING_RX(bp, NEXT_RX(cons));
	rxr->rx_next_cons = RING_RX(bp, NEXT_RX(cons));
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
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
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
	struct net_device *dev = bp->dev;
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

	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
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

	if (len <= bp->rx_copybreak) {
		skb = bnxt_copy_skb(bnapi, data_ptr, len, mapping);
		if (!skb) {
			bnxt_abort_tpa(cpr, idx, agg_bufs);
			cpr->sw_stats->rx.rx_oom_discards += 1;
			return NULL;
		}
	} else {
		u8 *new_data;
		dma_addr_t new_mapping;

		new_data = __bnxt_alloc_rx_frag(bp, &new_mapping, rxr,
						GFP_ATOMIC);
		if (!new_data) {
			bnxt_abort_tpa(cpr, idx, agg_bufs);
			cpr->sw_stats->rx.rx_oom_discards += 1;
			return NULL;
		}

		tpa_info->data = new_data;
		tpa_info->data_ptr = new_data + bp->rx_offset;
		tpa_info->mapping = new_mapping;

		skb = napi_build_skb(data, bp->rx_buf_size);
		dma_sync_single_for_cpu(&bp->pdev->dev, mapping,
					bp->rx_buf_use_size, bp->rx_dir);

		if (!skb) {
			page_pool_free_va(rxr->head_pool, data, true);
			bnxt_abort_tpa(cpr, idx, agg_bufs);
			cpr->sw_stats->rx.rx_oom_discards += 1;
			return NULL;
		}
		skb_mark_for_recycle(skb);
		skb_reserve(skb, bp->rx_offset);
		skb_put(skb, len);
	}

	if (agg_bufs) {
		skb = bnxt_rx_agg_pages_skb(bp, cpr, skb, idx, agg_bufs, true);
		if (!skb) {
			/* Page reuse already handled by bnxt_rx_pages(). */
			cpr->sw_stats->rx.rx_oom_discards += 1;
			return NULL;
		}
	}

	if (tpa_info->cfa_code_valid)
		dev = bnxt_get_pkt_dev(bp, tpa_info->cfa_code);
	skb->protocol = eth_type_trans(skb, dev);

	if (tpa_info->hash_type != PKT_HASH_TYPE_NONE)
		skb_set_hash(skb, tpa_info->rss_hash, tpa_info->hash_type);

	if (tpa_info->vlan_valid &&
	    (dev->features & BNXT_HW_FEATURE_VLAN_ALL_RX)) {
		__be16 vlan_proto = htons(tpa_info->metadata >>
					  RX_CMP_FLAGS2_METADATA_TPID_SFT);
		u16 vtag = tpa_info->metadata & RX_CMP_FLAGS2_METADATA_TCI_MASK;

		if (eth_type_vlan(vlan_proto)) {
			__vlan_hwaccel_put_tag(skb, vlan_proto, vtag);
		} else {
			dev_kfree_skb(skb);
			return NULL;
		}
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
	skb_mark_for_recycle(skb);

	if (skb->dev != bp->dev) {
		/* this packet belongs to a vf-rep */
		bnxt_vf_rep_rx(bp, skb);
		return;
	}
	skb_record_rx_queue(skb, bnapi->index);
	napi_gro_receive(&bnapi->napi, skb);
}

static bool bnxt_rx_ts_valid(struct bnxt *bp, u32 flags,
			     struct rx_cmp_ext *rxcmp1, u32 *cmpl_ts)
{
	u32 ts = le32_to_cpu(rxcmp1->rx_cmp_timestamp);

	if (BNXT_PTP_RX_TS_VALID(flags))
		goto ts_valid;
	if (!bp->ptp_all_rx_tstamp || !ts || !BNXT_ALL_RX_TS_VALID(flags))
		return false;

ts_valid:
	*cmpl_ts = ts;
	return true;
}

static struct sk_buff *bnxt_rx_vlan(struct sk_buff *skb, u8 cmp_type,
				    struct rx_cmp *rxcmp,
				    struct rx_cmp_ext *rxcmp1)
{
	__be16 vlan_proto;
	u16 vtag;

	if (cmp_type == CMP_TYPE_RX_L2_CMP) {
		__le32 flags2 = rxcmp1->rx_cmp_flags2;
		u32 meta_data;

		if (!(flags2 & cpu_to_le32(RX_CMP_FLAGS2_META_FORMAT_VLAN)))
			return skb;

		meta_data = le32_to_cpu(rxcmp1->rx_cmp_meta_data);
		vtag = meta_data & RX_CMP_FLAGS2_METADATA_TCI_MASK;
		vlan_proto = htons(meta_data >> RX_CMP_FLAGS2_METADATA_TPID_SFT);
		if (eth_type_vlan(vlan_proto))
			__vlan_hwaccel_put_tag(skb, vlan_proto, vtag);
		else
			goto vlan_err;
	} else if (cmp_type == CMP_TYPE_RX_L2_V3_CMP) {
		if (RX_CMP_VLAN_VALID(rxcmp)) {
			u32 tpid_sel = RX_CMP_VLAN_TPID_SEL(rxcmp);

			if (tpid_sel == RX_CMP_METADATA1_TPID_8021Q)
				vlan_proto = htons(ETH_P_8021Q);
			else if (tpid_sel == RX_CMP_METADATA1_TPID_8021AD)
				vlan_proto = htons(ETH_P_8021AD);
			else
				goto vlan_err;
			vtag = RX_CMP_METADATA0_TCI(rxcmp1);
			__vlan_hwaccel_put_tag(skb, vlan_proto, vtag);
		}
	}
	return skb;
vlan_err:
	dev_kfree_skb(skb);
	return NULL;
}

static enum pkt_hash_types bnxt_rss_ext_op(struct bnxt *bp,
					   struct rx_cmp *rxcmp)
{
	u8 ext_op;

	ext_op = RX_CMP_V3_HASH_TYPE(bp, rxcmp);
	switch (ext_op) {
	case EXT_OP_INNER_4:
	case EXT_OP_OUTER_4:
	case EXT_OP_INNFL_3:
	case EXT_OP_OUTFL_3:
		return PKT_HASH_TYPE_L4;
	default:
		return PKT_HASH_TYPE_L3;
	}
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
	u16 cons, prod, cp_cons = RING_CMP(tmp_raw_cons);
	struct skb_shared_info *sinfo;
	struct bnxt_sw_rx_bd *rx_buf;
	unsigned int len;
	u8 *data_ptr, agg_bufs, cmp_type;
	bool xdp_active = false;
	dma_addr_t dma_addr;
	struct sk_buff *skb;
	struct xdp_buff xdp;
	u32 flags, misc;
	u32 cmpl_ts;
	void *data;
	int rc = 0;

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

	/* The valid test of the entry must be done first before
	 * reading any further.
	 */
	dma_rmb();
	prod = rxr->rx_prod;

	if (cmp_type == CMP_TYPE_RX_L2_TPA_START_CMP ||
	    cmp_type == CMP_TYPE_RX_L2_TPA_START_V3_CMP) {
		bnxt_tpa_start(bp, rxr, cmp_type,
			       (struct rx_tpa_start_cmp *)rxcmp,
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
		int rc1 = bnxt_discard_rx(bp, cpr, &tmp_raw_cons, rxcmp);

		/* 0xffff is forced error, don't print it */
		if (rxr->rx_next_cons != 0xffff)
			netdev_warn(bp->dev, "RX cons %x != expected cons %x\n",
				    cons, rxr->rx_next_cons);
		bnxt_sched_reset_rxr(bp, rxr);
		if (rc1)
			return rc1;
		goto next_rx_no_prod_no_len;
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
			bnapi->cp_ring.sw_stats->rx.rx_buf_errors++;
			if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS) &&
			    !(bp->fw_cap & BNXT_FW_CAP_RING_MONITOR)) {
				netdev_warn_once(bp->dev, "RX buffer error %x\n",
						 rx_err);
				bnxt_sched_reset_rxr(bp, rxr);
			}
		}
		goto next_rx_no_len;
	}

	flags = le32_to_cpu(rxcmp->rx_cmp_len_flags_type);
	len = flags >> RX_CMP_LEN_SHIFT;
	dma_addr = rx_buf->mapping;

	if (bnxt_xdp_attached(bp, rxr)) {
		bnxt_xdp_buff_init(bp, rxr, cons, data_ptr, len, &xdp);
		if (agg_bufs) {
			u32 frag_len = bnxt_rx_agg_pages_xdp(bp, cpr, &xdp,
							     cp_cons, agg_bufs,
							     false);
			if (!frag_len)
				goto oom_next_rx;

		}
		xdp_active = true;
	}

	if (xdp_active) {
		if (bnxt_rx_xdp(bp, rxr, cons, &xdp, data, &data_ptr, &len, event)) {
			rc = 1;
			goto next_rx;
		}
		if (xdp_buff_has_frags(&xdp)) {
			sinfo = xdp_get_shared_info_from_buff(&xdp);
			agg_bufs = sinfo->nr_frags;
		} else {
			agg_bufs = 0;
		}
	}

	if (len <= bp->rx_copybreak) {
		if (!xdp_active)
			skb = bnxt_copy_skb(bnapi, data_ptr, len, dma_addr);
		else
			skb = bnxt_copy_xdp(bnapi, &xdp, len, dma_addr);
		bnxt_reuse_rx_data(rxr, cons, data);
		if (!skb) {
			if (agg_bufs) {
				if (!xdp_active)
					bnxt_reuse_rx_agg_bufs(cpr, cp_cons, 0,
							       agg_bufs, false);
				else
					bnxt_xdp_buff_frags_free(rxr, &xdp);
			}
			goto oom_next_rx;
		}
	} else {
		u32 payload;

		if (rx_buf->data_ptr == data_ptr)
			payload = misc & RX_CMP_PAYLOAD_OFFSET;
		else
			payload = 0;
		skb = bp->rx_skb_func(bp, rxr, cons, data, data_ptr, dma_addr,
				      payload | len);
		if (!skb)
			goto oom_next_rx;
	}

	if (agg_bufs) {
		if (!xdp_active) {
			skb = bnxt_rx_agg_pages_skb(bp, cpr, skb, cp_cons, agg_bufs, false);
			if (!skb)
				goto oom_next_rx;
		} else {
			skb = bnxt_xdp_build_skb(bp, skb, agg_bufs,
						 rxr->page_pool, &xdp);
			if (!skb) {
				/* we should be able to free the old skb here */
				bnxt_xdp_buff_frags_free(rxr, &xdp);
				goto oom_next_rx;
			}
		}
	}

	if (RX_CMP_HASH_VALID(rxcmp)) {
		enum pkt_hash_types type;

		if (cmp_type == CMP_TYPE_RX_L2_V3_CMP) {
			type = bnxt_rss_ext_op(bp, rxcmp);
		} else {
			u32 itypes = RX_CMP_ITYPES(rxcmp);

			if (itypes == RX_CMP_FLAGS_ITYPE_TCP ||
			    itypes == RX_CMP_FLAGS_ITYPE_UDP)
				type = PKT_HASH_TYPE_L4;
			else
				type = PKT_HASH_TYPE_L3;
		}
		skb_set_hash(skb, le32_to_cpu(rxcmp->rx_cmp_rss_hash), type);
	}

	if (cmp_type == CMP_TYPE_RX_L2_CMP)
		dev = bnxt_get_pkt_dev(bp, RX_CMP_CFA_CODE(rxcmp1));
	skb->protocol = eth_type_trans(skb, dev);

	if (skb->dev->features & BNXT_HW_FEATURE_VLAN_ALL_RX) {
		skb = bnxt_rx_vlan(skb, cmp_type, rxcmp, rxcmp1);
		if (!skb)
			goto next_rx;
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
				bnapi->cp_ring.sw_stats->rx.rx_l4_csum_errors++;
		}
	}

	if (bnxt_rx_ts_valid(bp, flags, rxcmp1, &cmpl_ts)) {
		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
			u64 ns, ts;

			if (!bnxt_get_rx_ts_p5(bp, &ts, cmpl_ts)) {
				struct bnxt_ptp_cfg *ptp = bp->ptp_cfg;

				ns = bnxt_timecounter_cyc2time(ptp, ts);
				memset(skb_hwtstamps(skb), 0,
				       sizeof(*skb_hwtstamps(skb)));
				skb_hwtstamps(skb)->hwtstamp = ns_to_ktime(ns);
			}
		}
	}
	bnxt_deliver_skb(bp, bnapi, skb);
	rc = 1;

next_rx:
	cpr->rx_packets += 1;
	cpr->rx_bytes += len;

next_rx_no_len:
	rxr->rx_prod = NEXT_RX(prod);
	rxr->rx_next_cons = RING_RX(bp, NEXT_RX(cons));

next_rx_no_prod_no_len:
	*raw_cons = tmp_raw_cons;

	return rc;

oom_next_rx:
	cpr->sw_stats->rx.rx_oom_discards += 1;
	rc = -ENOMEM;
	goto next_rx;
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
	int rc;

	cp_cons = RING_CMP(tmp_raw_cons);
	rxcmp = (struct rx_cmp *)
			&cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

	tmp_raw_cons = NEXT_RAW_CMP(tmp_raw_cons);
	cp_cons = RING_CMP(tmp_raw_cons);
	rxcmp1 = (struct rx_cmp_ext *)
			&cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

	if (!RX_CMP_VALID(rxcmp1, tmp_raw_cons))
		return -EBUSY;

	/* The valid test of the entry must be done first before
	 * reading any further.
	 */
	dma_rmb();
	cmp_type = RX_CMP_TYPE(rxcmp);
	if (cmp_type == CMP_TYPE_RX_L2_CMP ||
	    cmp_type == CMP_TYPE_RX_L2_V3_CMP) {
		rxcmp1->rx_cmp_cfa_code_errors_v2 |=
			cpu_to_le32(RX_CMPL_ERRORS_CRC_ERROR);
	} else if (cmp_type == CMP_TYPE_RX_L2_TPA_END_CMP) {
		struct rx_tpa_end_cmp_ext *tpa_end1;

		tpa_end1 = (struct rx_tpa_end_cmp_ext *)rxcmp1;
		tpa_end1->rx_tpa_end_cmp_errors_v2 |=
			cpu_to_le32(RX_TPA_END_CMP_ERRORS);
	}
	rc = bnxt_rx_pkt(bp, cpr, raw_cons, event);
	if (rc && rc != -EBUSY)
		cpr->sw_stats->rx.rx_netpoll_discards += 1;
	return rc;
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

static u16 bnxt_get_force_speed(struct bnxt_link_info *link_info)
{
	struct bnxt *bp = container_of(link_info, struct bnxt, link_info);

	if (bp->phy_flags & BNXT_PHY_FL_SPEEDS2)
		return link_info->force_link_speed2;
	if (link_info->req_signal_mode == BNXT_SIG_MODE_PAM4)
		return link_info->force_pam4_link_speed;
	return link_info->force_link_speed;
}

static void bnxt_set_force_speed(struct bnxt_link_info *link_info)
{
	struct bnxt *bp = container_of(link_info, struct bnxt, link_info);

	if (bp->phy_flags & BNXT_PHY_FL_SPEEDS2) {
		link_info->req_link_speed = link_info->force_link_speed2;
		link_info->req_signal_mode = BNXT_SIG_MODE_NRZ;
		switch (link_info->req_link_speed) {
		case BNXT_LINK_SPEED_50GB_PAM4:
		case BNXT_LINK_SPEED_100GB_PAM4:
		case BNXT_LINK_SPEED_200GB_PAM4:
		case BNXT_LINK_SPEED_400GB_PAM4:
			link_info->req_signal_mode = BNXT_SIG_MODE_PAM4;
			break;
		case BNXT_LINK_SPEED_100GB_PAM4_112:
		case BNXT_LINK_SPEED_200GB_PAM4_112:
		case BNXT_LINK_SPEED_400GB_PAM4_112:
			link_info->req_signal_mode = BNXT_SIG_MODE_PAM4_112;
			break;
		default:
			link_info->req_signal_mode = BNXT_SIG_MODE_NRZ;
		}
		return;
	}
	link_info->req_link_speed = link_info->force_link_speed;
	link_info->req_signal_mode = BNXT_SIG_MODE_NRZ;
	if (link_info->force_pam4_link_speed) {
		link_info->req_link_speed = link_info->force_pam4_link_speed;
		link_info->req_signal_mode = BNXT_SIG_MODE_PAM4;
	}
}

static void bnxt_set_auto_speed(struct bnxt_link_info *link_info)
{
	struct bnxt *bp = container_of(link_info, struct bnxt, link_info);

	if (bp->phy_flags & BNXT_PHY_FL_SPEEDS2) {
		link_info->advertising = link_info->auto_link_speeds2;
		return;
	}
	link_info->advertising = link_info->auto_link_speeds;
	link_info->advertising_pam4 = link_info->auto_pam4_link_speeds;
}

static bool bnxt_force_speed_updated(struct bnxt_link_info *link_info)
{
	struct bnxt *bp = container_of(link_info, struct bnxt, link_info);

	if (bp->phy_flags & BNXT_PHY_FL_SPEEDS2) {
		if (link_info->req_link_speed != link_info->force_link_speed2)
			return true;
		return false;
	}
	if (link_info->req_signal_mode == BNXT_SIG_MODE_NRZ &&
	    link_info->req_link_speed != link_info->force_link_speed)
		return true;
	if (link_info->req_signal_mode == BNXT_SIG_MODE_PAM4 &&
	    link_info->req_link_speed != link_info->force_pam4_link_speed)
		return true;
	return false;
}

static bool bnxt_auto_speed_updated(struct bnxt_link_info *link_info)
{
	struct bnxt *bp = container_of(link_info, struct bnxt, link_info);

	if (bp->phy_flags & BNXT_PHY_FL_SPEEDS2) {
		if (link_info->advertising != link_info->auto_link_speeds2)
			return true;
		return false;
	}
	if (link_info->advertising != link_info->auto_link_speeds ||
	    link_info->advertising_pam4 != link_info->auto_pam4_link_speeds)
		return true;
	return false;
}

bool bnxt_bs_trace_avail(struct bnxt *bp, u16 type)
{
	u32 flags = bp->ctx->ctx_arr[type].flags;

	return (flags & BNXT_CTX_MEM_TYPE_VALID) &&
		((flags & BNXT_CTX_MEM_FW_TRACE) ||
		 (flags & BNXT_CTX_MEM_FW_BIN_TRACE));
}

static void bnxt_bs_trace_init(struct bnxt *bp, struct bnxt_ctx_mem_type *ctxm)
{
	u32 mem_size, pages, rem_bytes, magic_byte_offset;
	u16 trace_type = bnxt_bstore_to_trace[ctxm->type];
	struct bnxt_ctx_pg_info *ctx_pg = ctxm->pg_info;
	struct bnxt_ring_mem_info *rmem, *rmem_pg_tbl;
	struct bnxt_bs_trace_info *bs_trace;
	int last_pg;

	if (ctxm->instance_bmap && ctxm->instance_bmap > 1)
		return;

	mem_size = ctxm->max_entries * ctxm->entry_size;
	rem_bytes = mem_size % BNXT_PAGE_SIZE;
	pages = DIV_ROUND_UP(mem_size, BNXT_PAGE_SIZE);

	last_pg = (pages - 1) & (MAX_CTX_PAGES - 1);
	magic_byte_offset = (rem_bytes ? rem_bytes : BNXT_PAGE_SIZE) - 1;

	rmem = &ctx_pg[0].ring_mem;
	bs_trace = &bp->bs_trace[trace_type];
	bs_trace->ctx_type = ctxm->type;
	bs_trace->trace_type = trace_type;
	if (pages > MAX_CTX_PAGES) {
		int last_pg_dir = rmem->nr_pages - 1;

		rmem_pg_tbl = &ctx_pg[0].ctx_pg_tbl[last_pg_dir]->ring_mem;
		bs_trace->magic_byte = rmem_pg_tbl->pg_arr[last_pg];
	} else {
		bs_trace->magic_byte = rmem->pg_arr[last_pg];
	}
	bs_trace->magic_byte += magic_byte_offset;
	*bs_trace->magic_byte = BNXT_TRACE_BUF_MAGIC_BYTE;
}

#define BNXT_EVENT_BUF_PRODUCER_TYPE(data1)				\
	(((data1) & ASYNC_EVENT_CMPL_DBG_BUF_PRODUCER_EVENT_DATA1_TYPE_MASK) >>\
	 ASYNC_EVENT_CMPL_DBG_BUF_PRODUCER_EVENT_DATA1_TYPE_SFT)

#define BNXT_EVENT_BUF_PRODUCER_OFFSET(data2)				\
	(((data2) &							\
	  ASYNC_EVENT_CMPL_DBG_BUF_PRODUCER_EVENT_DATA2_CURR_OFF_MASK) >>\
	 ASYNC_EVENT_CMPL_DBG_BUF_PRODUCER_EVENT_DATA2_CURR_OFF_SFT)

#define BNXT_EVENT_THERMAL_CURRENT_TEMP(data2)				\
	((data2) &							\
	  ASYNC_EVENT_CMPL_ERROR_REPORT_THERMAL_EVENT_DATA2_CURRENT_TEMP_MASK)

#define BNXT_EVENT_THERMAL_THRESHOLD_TEMP(data2)			\
	(((data2) &							\
	  ASYNC_EVENT_CMPL_ERROR_REPORT_THERMAL_EVENT_DATA2_THRESHOLD_TEMP_MASK) >>\
	 ASYNC_EVENT_CMPL_ERROR_REPORT_THERMAL_EVENT_DATA2_THRESHOLD_TEMP_SFT)

#define EVENT_DATA1_THERMAL_THRESHOLD_TYPE(data1)			\
	((data1) &							\
	 ASYNC_EVENT_CMPL_ERROR_REPORT_THERMAL_EVENT_DATA1_THRESHOLD_TYPE_MASK)

#define EVENT_DATA1_THERMAL_THRESHOLD_DIR_INCREASING(data1)		\
	(((data1) &							\
	  ASYNC_EVENT_CMPL_ERROR_REPORT_THERMAL_EVENT_DATA1_TRANSITION_DIR) ==\
	 ASYNC_EVENT_CMPL_ERROR_REPORT_THERMAL_EVENT_DATA1_TRANSITION_DIR_INCREASING)

/* Return true if the workqueue has to be scheduled */
static bool bnxt_event_error_report(struct bnxt *bp, u32 data1, u32 data2)
{
	u32 err_type = BNXT_EVENT_ERROR_REPORT_TYPE(data1);

	switch (err_type) {
	case ASYNC_EVENT_CMPL_ERROR_REPORT_BASE_EVENT_DATA1_ERROR_TYPE_INVALID_SIGNAL:
		netdev_err(bp->dev, "1PPS: Received invalid signal on pin%lu from the external source. Please fix the signal and reconfigure the pin\n",
			   BNXT_EVENT_INVALID_SIGNAL_DATA(data2));
		break;
	case ASYNC_EVENT_CMPL_ERROR_REPORT_BASE_EVENT_DATA1_ERROR_TYPE_PAUSE_STORM:
		netdev_warn(bp->dev, "Pause Storm detected!\n");
		break;
	case ASYNC_EVENT_CMPL_ERROR_REPORT_BASE_EVENT_DATA1_ERROR_TYPE_DOORBELL_DROP_THRESHOLD:
		netdev_warn(bp->dev, "One or more MMIO doorbells dropped by the device!\n");
		break;
	case ASYNC_EVENT_CMPL_ERROR_REPORT_BASE_EVENT_DATA1_ERROR_TYPE_THERMAL_THRESHOLD: {
		u32 type = EVENT_DATA1_THERMAL_THRESHOLD_TYPE(data1);
		char *threshold_type;
		bool notify = false;
		char *dir_str;

		switch (type) {
		case ASYNC_EVENT_CMPL_ERROR_REPORT_THERMAL_EVENT_DATA1_THRESHOLD_TYPE_WARN:
			threshold_type = "warning";
			break;
		case ASYNC_EVENT_CMPL_ERROR_REPORT_THERMAL_EVENT_DATA1_THRESHOLD_TYPE_CRITICAL:
			threshold_type = "critical";
			break;
		case ASYNC_EVENT_CMPL_ERROR_REPORT_THERMAL_EVENT_DATA1_THRESHOLD_TYPE_FATAL:
			threshold_type = "fatal";
			break;
		case ASYNC_EVENT_CMPL_ERROR_REPORT_THERMAL_EVENT_DATA1_THRESHOLD_TYPE_SHUTDOWN:
			threshold_type = "shutdown";
			break;
		default:
			netdev_err(bp->dev, "Unknown Thermal threshold type event\n");
			return false;
		}
		if (EVENT_DATA1_THERMAL_THRESHOLD_DIR_INCREASING(data1)) {
			dir_str = "above";
			notify = true;
		} else {
			dir_str = "below";
		}
		netdev_warn(bp->dev, "Chip temperature has gone %s the %s thermal threshold!\n",
			    dir_str, threshold_type);
		netdev_warn(bp->dev, "Temperature (In Celsius), Current: %lu, threshold: %lu\n",
			    BNXT_EVENT_THERMAL_CURRENT_TEMP(data2),
			    BNXT_EVENT_THERMAL_THRESHOLD_TEMP(data2));
		if (notify) {
			bp->thermal_threshold_type = type;
			set_bit(BNXT_THERMAL_THRESHOLD_SP_EVENT, &bp->sp_event);
			return true;
		}
		return false;
	}
	case ASYNC_EVENT_CMPL_ERROR_REPORT_BASE_EVENT_DATA1_ERROR_TYPE_DUAL_DATA_RATE_NOT_SUPPORTED:
		netdev_warn(bp->dev, "Speed change not supported with dual rate transceivers on this board\n");
		break;
	default:
		netdev_err(bp->dev, "FW reported unknown error type %u\n",
			   err_type);
		break;
	}
	return false;
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

#define BNXT_EVENT_PHC_EVENT_TYPE(data1)	\
	(((data1) & ASYNC_EVENT_CMPL_PHC_UPDATE_EVENT_DATA1_FLAGS_MASK) >>\
	 ASYNC_EVENT_CMPL_PHC_UPDATE_EVENT_DATA1_FLAGS_SFT)

#define BNXT_EVENT_PHC_RTC_UPDATE(data1)	\
	(((data1) & ASYNC_EVENT_CMPL_PHC_UPDATE_EVENT_DATA1_PHC_TIME_MSB_MASK) >>\
	 ASYNC_EVENT_CMPL_PHC_UPDATE_EVENT_DATA1_PHC_TIME_MSB_SFT)

#define BNXT_PHC_BITS	48

static int bnxt_async_event_process(struct bnxt *bp,
				    struct hwrm_async_event_cmpl *cmpl)
{
	u16 event_id = le16_to_cpu(cmpl->event_id);
	u32 data1 = le32_to_cpu(cmpl->event_data1);
	u32 data2 = le32_to_cpu(cmpl->event_data2);

	netdev_dbg(bp->dev, "hwrm event 0x%x {0x%x, 0x%x}\n",
		   event_id, data1, data2);

	/* TODO CHIMP_FW: Define event id's for link change, error etc */
	switch (event_id) {
	case ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CFG_CHANGE: {
		struct bnxt_link_info *link_info = &bp->link_info;

		if (BNXT_VF(bp))
			goto async_event_process_exit;

		/* print unsupported speed warning in forced speed mode only */
		if (!(link_info->autoneg & BNXT_AUTONEG_SPEED) &&
		    (data1 & 0x20000)) {
			u16 fw_speed = bnxt_get_force_speed(link_info);
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
	case ASYNC_EVENT_CMPL_EVENT_ID_RESET_NOTIFY: {
		char *type_str = "Solicited";

		if (!bp->fw_health)
			goto async_event_process_exit;

		bp->fw_reset_timestamp = jiffies;
		bp->fw_reset_min_dsecs = cmpl->timestamp_lo;
		if (!bp->fw_reset_min_dsecs)
			bp->fw_reset_min_dsecs = BNXT_DFLT_FW_RST_MIN_DSECS;
		bp->fw_reset_max_dsecs = le16_to_cpu(cmpl->timestamp_hi);
		if (!bp->fw_reset_max_dsecs)
			bp->fw_reset_max_dsecs = BNXT_DFLT_FW_RST_MAX_DSECS;
		if (EVENT_DATA1_RESET_NOTIFY_FW_ACTIVATION(data1)) {
			set_bit(BNXT_STATE_FW_ACTIVATE_RESET, &bp->state);
		} else if (EVENT_DATA1_RESET_NOTIFY_FATAL(data1)) {
			type_str = "Fatal";
			bp->fw_health->fatalities++;
			set_bit(BNXT_STATE_FW_FATAL_COND, &bp->state);
		} else if (data2 && BNXT_FW_STATUS_HEALTHY !=
			   EVENT_DATA2_RESET_NOTIFY_FW_STATUS_CODE(data2)) {
			type_str = "Non-fatal";
			bp->fw_health->survivals++;
			set_bit(BNXT_STATE_FW_NON_FATAL_COND, &bp->state);
		}
		netif_warn(bp, hw, bp->dev,
			   "%s firmware reset event, data1: 0x%x, data2: 0x%x, min wait %u ms, max wait %u ms\n",
			   type_str, data1, data2,
			   bp->fw_reset_min_dsecs * 100,
			   bp->fw_reset_max_dsecs * 100);
		set_bit(BNXT_FW_RESET_NOTIFY_SP_EVENT, &bp->sp_event);
		break;
	}
	case ASYNC_EVENT_CMPL_EVENT_ID_ERROR_RECOVERY: {
		struct bnxt_fw_health *fw_health = bp->fw_health;
		char *status_desc = "healthy";
		u32 status;

		if (!fw_health)
			goto async_event_process_exit;

		if (!EVENT_DATA1_RECOVERY_ENABLED(data1)) {
			fw_health->enabled = false;
			netif_info(bp, drv, bp->dev, "Driver recovery watchdog is disabled\n");
			break;
		}
		fw_health->primary = EVENT_DATA1_RECOVERY_MASTER_FUNC(data1);
		fw_health->tmr_multiplier =
			DIV_ROUND_UP(fw_health->polling_dsecs * HZ,
				     bp->current_interval * 10);
		fw_health->tmr_counter = fw_health->tmr_multiplier;
		if (!fw_health->enabled)
			fw_health->last_fw_heartbeat =
				bnxt_fw_health_readl(bp, BNXT_FW_HEARTBEAT_REG);
		fw_health->last_fw_reset_cnt =
			bnxt_fw_health_readl(bp, BNXT_FW_RESET_CNT_REG);
		status = bnxt_fw_health_readl(bp, BNXT_FW_HEALTH_REG);
		if (status != BNXT_FW_STATUS_HEALTHY)
			status_desc = "unhealthy";
		netif_info(bp, drv, bp->dev,
			   "Driver recovery watchdog, role: %s, firmware status: 0x%x (%s), resets: %u\n",
			   fw_health->primary ? "primary" : "backup", status,
			   status_desc, fw_health->last_fw_reset_cnt);
		if (!fw_health->enabled) {
			/* Make sure tmr_counter is set and visible to
			 * bnxt_health_check() before setting enabled to true.
			 */
			smp_wmb();
			fw_health->enabled = true;
		}
		goto async_event_process_exit;
	}
	case ASYNC_EVENT_CMPL_EVENT_ID_DEBUG_NOTIFICATION:
		netif_notice(bp, hw, bp->dev,
			     "Received firmware debug notification, data1: 0x%x, data2: 0x%x\n",
			     data1, data2);
		goto async_event_process_exit;
	case ASYNC_EVENT_CMPL_EVENT_ID_RING_MONITOR_MSG: {
		struct bnxt_rx_ring_info *rxr;
		u16 grp_idx;

		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
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
		bnxt_sched_reset_rxr(bp, rxr);
		goto async_event_process_exit;
	}
	case ASYNC_EVENT_CMPL_EVENT_ID_ECHO_REQUEST: {
		struct bnxt_fw_health *fw_health = bp->fw_health;

		netif_notice(bp, hw, bp->dev,
			     "Received firmware echo request, data1: 0x%x, data2: 0x%x\n",
			     data1, data2);
		if (fw_health) {
			fw_health->echo_req_data1 = data1;
			fw_health->echo_req_data2 = data2;
			set_bit(BNXT_FW_ECHO_REQUEST_SP_EVENT, &bp->sp_event);
			break;
		}
		goto async_event_process_exit;
	}
	case ASYNC_EVENT_CMPL_EVENT_ID_PPS_TIMESTAMP: {
		bnxt_ptp_pps_event(bp, data1, data2);
		goto async_event_process_exit;
	}
	case ASYNC_EVENT_CMPL_EVENT_ID_ERROR_REPORT: {
		if (bnxt_event_error_report(bp, data1, data2))
			break;
		goto async_event_process_exit;
	}
	case ASYNC_EVENT_CMPL_EVENT_ID_PHC_UPDATE: {
		switch (BNXT_EVENT_PHC_EVENT_TYPE(data1)) {
		case ASYNC_EVENT_CMPL_PHC_UPDATE_EVENT_DATA1_FLAGS_PHC_RTC_UPDATE:
			if (BNXT_PTP_USE_RTC(bp)) {
				struct bnxt_ptp_cfg *ptp = bp->ptp_cfg;
				unsigned long flags;
				u64 ns;

				if (!ptp)
					goto async_event_process_exit;

				bnxt_ptp_update_current_time(bp);
				ns = (((u64)BNXT_EVENT_PHC_RTC_UPDATE(data1) <<
				       BNXT_PHC_BITS) | ptp->current_time);
				write_seqlock_irqsave(&ptp->ptp_lock, flags);
				bnxt_ptp_rtc_timecounter_init(ptp, ns);
				write_sequnlock_irqrestore(&ptp->ptp_lock, flags);
			}
			break;
		}
		goto async_event_process_exit;
	}
	case ASYNC_EVENT_CMPL_EVENT_ID_DEFERRED_RESPONSE: {
		u16 seq_id = le32_to_cpu(cmpl->event_data2) & 0xffff;

		hwrm_update_token(bp, seq_id, BNXT_HWRM_DEFERRED);
		goto async_event_process_exit;
	}
	case ASYNC_EVENT_CMPL_EVENT_ID_DBG_BUF_PRODUCER: {
		u16 type = (u16)BNXT_EVENT_BUF_PRODUCER_TYPE(data1);
		u32 offset =  BNXT_EVENT_BUF_PRODUCER_OFFSET(data2);

		bnxt_bs_trace_check_wrap(&bp->bs_trace[type], offset);
		goto async_event_process_exit;
	}
	default:
		goto async_event_process_exit;
	}
	__bnxt_queue_sp_work(bp);
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
		hwrm_update_token(bp, seq_id, BNXT_HWRM_COMPLETE);
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
		bnxt_queue_sp_work(bp, BNXT_HWRM_EXEC_FWD_REQ_SP_EVENT);
		break;

	case CMPL_BASE_TYPE_HWRM_ASYNC_EVENT:
		bnxt_async_event_process(bp,
					 (struct hwrm_async_event_cmpl *)txcmp);
		break;

	default:
		break;
	}

	return 0;
}

static bool bnxt_vnic_is_active(struct bnxt *bp)
{
	struct bnxt_vnic_info *vnic = &bp->vnic_info[0];

	return vnic->fw_vnic_id != INVALID_HW_RING_ID && vnic->mru > 0;
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

static int __bnxt_poll_work(struct bnxt *bp, struct bnxt_cp_ring_info *cpr,
			    int budget)
{
	struct bnxt_napi *bnapi = cpr->bnapi;
	u32 raw_cons = cpr->cp_raw_cons;
	u32 cons;
	int rx_pkts = 0;
	u8 event = 0;
	struct tx_cmp *txcmp;

	cpr->has_more_work = 0;
	cpr->had_work_done = 1;
	while (1) {
		u8 cmp_type;
		int rc;

		cons = RING_CMP(raw_cons);
		txcmp = &cpr->cp_desc_ring[CP_RING(cons)][CP_IDX(cons)];

		if (!TX_CMP_VALID(txcmp, raw_cons))
			break;

		/* The valid test of the entry must be done first before
		 * reading any further.
		 */
		dma_rmb();
		cmp_type = TX_CMP_TYPE(txcmp);
		if (cmp_type == CMP_TYPE_TX_L2_CMP ||
		    cmp_type == CMP_TYPE_TX_L2_COAL_CMP) {
			u32 opaque = txcmp->tx_cmp_opaque;
			struct bnxt_tx_ring_info *txr;
			u16 tx_freed;

			txr = bnapi->tx_ring[TX_OPAQUE_RING(opaque)];
			event |= BNXT_TX_CMP_EVENT;
			if (cmp_type == CMP_TYPE_TX_L2_COAL_CMP)
				txr->tx_hw_cons = TX_CMP_SQ_CONS_IDX(txcmp);
			else
				txr->tx_hw_cons = TX_OPAQUE_PROD(bp, opaque);
			tx_freed = (txr->tx_hw_cons - txr->tx_cons) &
				   bp->tx_ring_mask;
			/* return full budget so NAPI will complete. */
			if (unlikely(tx_freed >= bp->tx_wake_thresh)) {
				rx_pkts = budget;
				raw_cons = NEXT_RAW_CMP(raw_cons);
				if (budget)
					cpr->has_more_work = 1;
				break;
			}
		} else if (cmp_type == CMP_TYPE_TX_L2_PKT_TS_CMP) {
			bnxt_tx_ts_cmp(bp, bnapi, (struct tx_ts_cmp *)txcmp);
		} else if (cmp_type >= CMP_TYPE_RX_L2_CMP &&
			   cmp_type <= CMP_TYPE_RX_L2_TPA_START_V3_CMP) {
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
		} else if (unlikely(cmp_type == CMPL_BASE_TYPE_HWRM_DONE ||
				    cmp_type == CMPL_BASE_TYPE_HWRM_FWD_REQ ||
				    cmp_type == CMPL_BASE_TYPE_HWRM_ASYNC_EVENT)) {
			bnxt_hwrm_handler(bp, txcmp);
		}
		raw_cons = NEXT_RAW_CMP(raw_cons);

		if (rx_pkts && rx_pkts == budget) {
			cpr->has_more_work = 1;
			break;
		}
	}

	if (event & BNXT_REDIRECT_EVENT) {
		xdp_do_flush();
		event &= ~BNXT_REDIRECT_EVENT;
	}

	if (event & BNXT_TX_EVENT) {
		struct bnxt_tx_ring_info *txr = bnapi->tx_ring[0];
		u16 prod = txr->tx_prod;

		/* Sync BD data before updating doorbell */
		wmb();

		bnxt_db_write_relaxed(bp, &txr->tx_db, prod);
		event &= ~BNXT_TX_EVENT;
	}

	cpr->cp_raw_cons = raw_cons;
	bnapi->events |= event;
	return rx_pkts;
}

static void __bnxt_poll_work_done(struct bnxt *bp, struct bnxt_napi *bnapi,
				  int budget)
{
	if ((bnapi->events & BNXT_TX_CMP_EVENT) && !bnapi->tx_fault)
		bnapi->tx_int(bp, bnapi, budget);

	if ((bnapi->events & BNXT_RX_EVENT) && !(bnapi->in_reset)) {
		struct bnxt_rx_ring_info *rxr = bnapi->rx_ring;

		bnxt_db_write(bp, &rxr->rx_db, rxr->rx_prod);
		bnapi->events &= ~BNXT_RX_EVENT;
	}
	if (bnapi->events & BNXT_AGG_EVENT) {
		struct bnxt_rx_ring_info *rxr = bnapi->rx_ring;

		bnxt_db_write(bp, &rxr->rx_agg_db, rxr->rx_agg_prod);
		bnapi->events &= ~BNXT_AGG_EVENT;
	}
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

	__bnxt_poll_work_done(bp, bnapi, budget);
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
	bool flush_xdp = false;
	u32 rx_pkts = 0;
	u8 event = 0;

	while (1) {
		int rc;

		cp_cons = RING_CMP(raw_cons);
		txcmp = &cpr->cp_desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

		if (!TX_CMP_VALID(txcmp, raw_cons))
			break;

		/* The valid test of the entry must be done first before
		 * reading any further.
		 */
		dma_rmb();
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
			if (event & BNXT_REDIRECT_EVENT)
				flush_xdp = true;
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
	if (flush_xdp)
		xdp_do_flush();

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

	if (unlikely(test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))) {
		napi_complete(napi);
		return 0;
	}
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
	if ((bp->flags & BNXT_FLAG_DIM) && bnxt_vnic_is_active(bp)) {
		struct dim_sample dim_sample = {};

		dim_update_sample(cpr->event_ctr,
				  cpr->rx_packets,
				  cpr->rx_bytes,
				  &dim_sample);
		net_dim(&cpr->dim, &dim_sample);
	}
	return work_done;
}

static int __bnxt_poll_cqs(struct bnxt *bp, struct bnxt_napi *bnapi, int budget)
{
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	int i, work_done = 0;

	for (i = 0; i < cpr->cp_ring_count; i++) {
		struct bnxt_cp_ring_info *cpr2 = &cpr->cp_ring_arr[i];

		if (cpr2->had_nqe_notify) {
			work_done += __bnxt_poll_work(bp, cpr2,
						      budget - work_done);
			cpr->has_more_work |= cpr2->has_more_work;
		}
	}
	return work_done;
}

static void __bnxt_poll_cqs_done(struct bnxt *bp, struct bnxt_napi *bnapi,
				 u64 dbr_type, int budget)
{
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	int i;

	for (i = 0; i < cpr->cp_ring_count; i++) {
		struct bnxt_cp_ring_info *cpr2 = &cpr->cp_ring_arr[i];
		struct bnxt_db_info *db;

		if (cpr2->had_work_done) {
			u32 tgl = 0;

			if (dbr_type == DBR_TYPE_CQ_ARMALL) {
				cpr2->had_nqe_notify = 0;
				tgl = cpr2->toggle;
			}
			db = &cpr2->cp_db;
			bnxt_writeq(bp,
				    db->db_key64 | dbr_type | DB_TOGGLE(tgl) |
				    DB_RING_IDX(db, cpr2->cp_raw_cons),
				    db->doorbell);
			cpr2->had_work_done = 0;
		}
	}
	__bnxt_poll_work_done(bp, bnapi, budget);
}

static int bnxt_poll_p5(struct napi_struct *napi, int budget)
{
	struct bnxt_napi *bnapi = container_of(napi, struct bnxt_napi, napi);
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	struct bnxt_cp_ring_info *cpr_rx;
	u32 raw_cons = cpr->cp_raw_cons;
	struct bnxt *bp = bnapi->bp;
	struct nqe_cn *nqcmp;
	int work_done = 0;
	u32 cons;

	if (unlikely(test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))) {
		napi_complete(napi);
		return 0;
	}
	if (cpr->has_more_work) {
		cpr->has_more_work = 0;
		work_done = __bnxt_poll_cqs(bp, bnapi, budget);
	}
	while (1) {
		u16 type;

		cons = RING_CMP(raw_cons);
		nqcmp = &cpr->nq_desc_ring[CP_RING(cons)][CP_IDX(cons)];

		if (!NQ_CMP_VALID(nqcmp, raw_cons)) {
			if (cpr->has_more_work)
				break;

			__bnxt_poll_cqs_done(bp, bnapi, DBR_TYPE_CQ_ARMALL,
					     budget);
			cpr->cp_raw_cons = raw_cons;
			if (napi_complete_done(napi, work_done))
				BNXT_DB_NQ_ARM_P5(&cpr->cp_db,
						  cpr->cp_raw_cons);
			goto poll_done;
		}

		/* The valid test of the entry must be done first before
		 * reading any further.
		 */
		dma_rmb();

		type = le16_to_cpu(nqcmp->type);
		if (NQE_CN_TYPE(type) == NQ_CN_TYPE_CQ_NOTIFICATION) {
			u32 idx = le32_to_cpu(nqcmp->cq_handle_low);
			u32 cq_type = BNXT_NQ_HDL_TYPE(idx);
			struct bnxt_cp_ring_info *cpr2;

			/* No more budget for RX work */
			if (budget && work_done >= budget &&
			    cq_type == BNXT_NQ_HDL_TYPE_RX)
				break;

			idx = BNXT_NQ_HDL_IDX(idx);
			cpr2 = &cpr->cp_ring_arr[idx];
			cpr2->had_nqe_notify = 1;
			cpr2->toggle = NQE_CN_TOGGLE(type);
			work_done += __bnxt_poll_work(bp, cpr2,
						      budget - work_done);
			cpr->has_more_work |= cpr2->has_more_work;
		} else {
			bnxt_hwrm_handler(bp, (struct tx_cmp *)nqcmp);
		}
		raw_cons = NEXT_RAW_CMP(raw_cons);
	}
	__bnxt_poll_cqs_done(bp, bnapi, DBR_TYPE_CQ, budget);
	if (raw_cons != cpr->cp_raw_cons) {
		cpr->cp_raw_cons = raw_cons;
		BNXT_DB_NQ_P5(&cpr->cp_db, raw_cons);
	}
poll_done:
	cpr_rx = &cpr->cp_ring_arr[0];
	if (cpr_rx->cp_ring_type == BNXT_NQ_HDL_TYPE_RX &&
	    (bp->flags & BNXT_FLAG_DIM) && bnxt_vnic_is_active(bp)) {
		struct dim_sample dim_sample = {};

		dim_update_sample(cpr->event_ctr,
				  cpr_rx->rx_packets,
				  cpr_rx->rx_bytes,
				  &dim_sample);
		net_dim(&cpr->dim, &dim_sample);
	}
	return work_done;
}

static void bnxt_free_one_tx_ring_skbs(struct bnxt *bp,
				       struct bnxt_tx_ring_info *txr, int idx)
{
	int i, max_idx;
	struct pci_dev *pdev = bp->pdev;

	max_idx = bp->tx_nr_pages * TX_DESC_CNT;

	for (i = 0; i < max_idx;) {
		struct bnxt_sw_tx_bd *tx_buf = &txr->tx_buf_ring[i];
		struct sk_buff *skb;
		int j, last;

		if (idx  < bp->tx_nr_rings_xdp &&
		    tx_buf->action == XDP_REDIRECT) {
			dma_unmap_single(&pdev->dev,
					 dma_unmap_addr(tx_buf, mapping),
					 dma_unmap_len(tx_buf, len),
					 DMA_TO_DEVICE);
			xdp_return_frame(tx_buf->xdpf);
			tx_buf->action = 0;
			tx_buf->xdpf = NULL;
			i++;
			continue;
		}

		skb = tx_buf->skb;
		if (!skb) {
			i++;
			continue;
		}

		tx_buf->skb = NULL;

		if (tx_buf->is_push) {
			dev_kfree_skb(skb);
			i += 2;
			continue;
		}

		dma_unmap_single(&pdev->dev,
				 dma_unmap_addr(tx_buf, mapping),
				 skb_headlen(skb),
				 DMA_TO_DEVICE);

		last = tx_buf->nr_frags;
		i += 2;
		for (j = 0; j < last; j++, i++) {
			int ring_idx = i & bp->tx_ring_mask;
			skb_frag_t *frag = &skb_shinfo(skb)->frags[j];

			tx_buf = &txr->tx_buf_ring[ring_idx];
			dma_unmap_page(&pdev->dev,
				       dma_unmap_addr(tx_buf, mapping),
				       skb_frag_size(frag), DMA_TO_DEVICE);
		}
		dev_kfree_skb(skb);
	}
	netdev_tx_reset_queue(netdev_get_tx_queue(bp->dev, idx));
}

static void bnxt_free_tx_skbs(struct bnxt *bp)
{
	int i;

	if (!bp->tx_ring)
		return;

	for (i = 0; i < bp->tx_nr_rings; i++) {
		struct bnxt_tx_ring_info *txr = &bp->tx_ring[i];

		if (!txr->tx_buf_ring)
			continue;

		bnxt_free_one_tx_ring_skbs(bp, txr, i);
	}
}

static void bnxt_free_one_rx_ring(struct bnxt *bp, struct bnxt_rx_ring_info *rxr)
{
	int i, max_idx;

	max_idx = bp->rx_nr_pages * RX_DESC_CNT;

	for (i = 0; i < max_idx; i++) {
		struct bnxt_sw_rx_bd *rx_buf = &rxr->rx_buf_ring[i];
		void *data = rx_buf->data;

		if (!data)
			continue;

		rx_buf->data = NULL;
		if (BNXT_RX_PAGE_MODE(bp))
			page_pool_recycle_direct(rxr->page_pool, data);
		else
			page_pool_free_va(rxr->head_pool, data, true);
	}
}

static void bnxt_free_one_rx_agg_ring(struct bnxt *bp, struct bnxt_rx_ring_info *rxr)
{
	int i, max_idx;

	max_idx = bp->rx_agg_nr_pages * RX_DESC_CNT;

	for (i = 0; i < max_idx; i++) {
		struct bnxt_sw_rx_agg_bd *rx_agg_buf = &rxr->rx_agg_ring[i];
		struct page *page = rx_agg_buf->page;

		if (!page)
			continue;

		rx_agg_buf->page = NULL;
		__clear_bit(i, rxr->rx_agg_bmap);

		page_pool_recycle_direct(rxr->page_pool, page);
	}
}

static void bnxt_free_one_tpa_info_data(struct bnxt *bp,
					struct bnxt_rx_ring_info *rxr)
{
	int i;

	for (i = 0; i < bp->max_tpa; i++) {
		struct bnxt_tpa_info *tpa_info = &rxr->rx_tpa[i];
		u8 *data = tpa_info->data;

		if (!data)
			continue;

		tpa_info->data = NULL;
		page_pool_free_va(rxr->head_pool, data, false);
	}
}

static void bnxt_free_one_rx_ring_skbs(struct bnxt *bp,
				       struct bnxt_rx_ring_info *rxr)
{
	struct bnxt_tpa_idx_map *map;

	if (!rxr->rx_tpa)
		goto skip_rx_tpa_free;

	bnxt_free_one_tpa_info_data(bp, rxr);

skip_rx_tpa_free:
	if (!rxr->rx_buf_ring)
		goto skip_rx_buf_free;

	bnxt_free_one_rx_ring(bp, rxr);

skip_rx_buf_free:
	if (!rxr->rx_agg_ring)
		goto skip_rx_agg_free;

	bnxt_free_one_rx_agg_ring(bp, rxr);

skip_rx_agg_free:
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
		bnxt_free_one_rx_ring_skbs(bp, &bp->rx_ring[i]);
}

static void bnxt_free_skbs(struct bnxt *bp)
{
	bnxt_free_tx_skbs(bp);
	bnxt_free_rx_skbs(bp);
}

static void bnxt_init_ctx_mem(struct bnxt_ctx_mem_type *ctxm, void *p, int len)
{
	u8 init_val = ctxm->init_value;
	u16 offset = ctxm->init_offset;
	u8 *p2 = p;
	int i;

	if (!init_val)
		return;
	if (offset == BNXT_CTX_INIT_INVALID_OFFSET) {
		memset(p, init_val, len);
		return;
	}
	for (i = 0; i < len; i += ctxm->entry_size)
		*(p2 + i + offset) = init_val;
}

static size_t __bnxt_copy_ring(struct bnxt *bp, struct bnxt_ring_mem_info *rmem,
			       void *buf, size_t offset, size_t head,
			       size_t tail)
{
	int i, head_page, start_idx, source_offset;
	size_t len, rem_len, total_len, max_bytes;

	head_page = head / rmem->page_size;
	source_offset = head % rmem->page_size;
	total_len = (tail - head) & MAX_CTX_BYTES_MASK;
	if (!total_len)
		total_len = MAX_CTX_BYTES;
	start_idx = head_page % MAX_CTX_PAGES;
	max_bytes = (rmem->nr_pages - start_idx) * rmem->page_size -
		    source_offset;
	total_len = min(total_len, max_bytes);
	rem_len = total_len;

	for (i = start_idx; rem_len; i++, source_offset = 0) {
		len = min((size_t)(rmem->page_size - source_offset), rem_len);
		if (buf)
			memcpy(buf + offset, rmem->pg_arr[i] + source_offset,
			       len);
		offset += len;
		rem_len -= len;
	}
	return total_len;
}

static void bnxt_free_ring(struct bnxt *bp, struct bnxt_ring_mem_info *rmem)
{
	struct pci_dev *pdev = bp->pdev;
	int i;

	if (!rmem->pg_arr)
		goto skip_pages;

	for (i = 0; i < rmem->nr_pages; i++) {
		if (!rmem->pg_arr[i])
			continue;

		dma_free_coherent(&pdev->dev, rmem->page_size,
				  rmem->pg_arr[i], rmem->dma_arr[i]);

		rmem->pg_arr[i] = NULL;
	}
skip_pages:
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

		if (rmem->ctx_mem)
			bnxt_init_ctx_mem(rmem->ctx_mem, rmem->pg_arr[i],
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

static void bnxt_free_one_tpa_info(struct bnxt *bp,
				   struct bnxt_rx_ring_info *rxr)
{
	int i;

	kfree(rxr->rx_tpa_idx_map);
	rxr->rx_tpa_idx_map = NULL;
	if (rxr->rx_tpa) {
		for (i = 0; i < bp->max_tpa; i++) {
			kfree(rxr->rx_tpa[i].agg_arr);
			rxr->rx_tpa[i].agg_arr = NULL;
		}
	}
	kfree(rxr->rx_tpa);
	rxr->rx_tpa = NULL;
}

static void bnxt_free_tpa_info(struct bnxt *bp)
{
	int i;

	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];

		bnxt_free_one_tpa_info(bp, rxr);
	}
}

static int bnxt_alloc_one_tpa_info(struct bnxt *bp,
				   struct bnxt_rx_ring_info *rxr)
{
	struct rx_agg_cmp *agg;
	int i;

	rxr->rx_tpa = kcalloc(bp->max_tpa, sizeof(struct bnxt_tpa_info),
			      GFP_KERNEL);
	if (!rxr->rx_tpa)
		return -ENOMEM;

	if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
		return 0;
	for (i = 0; i < bp->max_tpa; i++) {
		agg = kcalloc(MAX_SKB_FRAGS, sizeof(*agg), GFP_KERNEL);
		if (!agg)
			return -ENOMEM;
		rxr->rx_tpa[i].agg_arr = agg;
	}
	rxr->rx_tpa_idx_map = kzalloc(sizeof(*rxr->rx_tpa_idx_map),
				      GFP_KERNEL);
	if (!rxr->rx_tpa_idx_map)
		return -ENOMEM;

	return 0;
}

static int bnxt_alloc_tpa_info(struct bnxt *bp)
{
	int i, rc;

	bp->max_tpa = MAX_TPA;
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
		if (!bp->max_tpa_v2)
			return 0;
		bp->max_tpa = max_t(u16, bp->max_tpa_v2, MAX_TPA_P5);
	}

	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];

		rc = bnxt_alloc_one_tpa_info(bp, rxr);
		if (rc)
			return rc;
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
		if (bnxt_separate_head_pool())
			page_pool_destroy(rxr->head_pool);
		rxr->page_pool = rxr->head_pool = NULL;

		kfree(rxr->rx_agg_bmap);
		rxr->rx_agg_bmap = NULL;

		ring = &rxr->rx_ring_struct;
		bnxt_free_ring(bp, &ring->ring_mem);

		ring = &rxr->rx_agg_ring_struct;
		bnxt_free_ring(bp, &ring->ring_mem);
	}
}

static int bnxt_alloc_rx_page_pool(struct bnxt *bp,
				   struct bnxt_rx_ring_info *rxr,
				   int numa_node)
{
	struct page_pool_params pp = { 0 };
	struct page_pool *pool;

	pp.pool_size = bp->rx_agg_ring_size;
	if (BNXT_RX_PAGE_MODE(bp))
		pp.pool_size += bp->rx_ring_size;
	pp.nid = numa_node;
	pp.napi = &rxr->bnapi->napi;
	pp.netdev = bp->dev;
	pp.dev = &bp->pdev->dev;
	pp.dma_dir = bp->rx_dir;
	pp.max_len = PAGE_SIZE;
	pp.flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV;

	pool = page_pool_create(&pp);
	if (IS_ERR(pool))
		return PTR_ERR(pool);
	rxr->page_pool = pool;

	if (bnxt_separate_head_pool()) {
		pp.pool_size = max(bp->rx_ring_size, 1024);
		pool = page_pool_create(&pp);
		if (IS_ERR(pool))
			goto err_destroy_pp;
	}
	rxr->head_pool = pool;

	return 0;

err_destroy_pp:
	page_pool_destroy(rxr->page_pool);
	rxr->page_pool = NULL;
	return PTR_ERR(pool);
}

static int bnxt_alloc_rx_agg_bmap(struct bnxt *bp, struct bnxt_rx_ring_info *rxr)
{
	u16 mem_size;

	rxr->rx_agg_bmap_size = bp->rx_agg_ring_mask + 1;
	mem_size = rxr->rx_agg_bmap_size / 8;
	rxr->rx_agg_bmap = kzalloc(mem_size, GFP_KERNEL);
	if (!rxr->rx_agg_bmap)
		return -ENOMEM;

	return 0;
}

static int bnxt_alloc_rx_rings(struct bnxt *bp)
{
	int numa_node = dev_to_node(&bp->pdev->dev);
	int i, rc = 0, agg_rings = 0, cpu;

	if (!bp->rx_ring)
		return -ENOMEM;

	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		agg_rings = 1;

	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];
		struct bnxt_ring_struct *ring;
		int cpu_node;

		ring = &rxr->rx_ring_struct;

		cpu = cpumask_local_spread(i, numa_node);
		cpu_node = cpu_to_node(cpu);
		netdev_dbg(bp->dev, "Allocating page pool for rx_ring[%d] on numa_node: %d\n",
			   i, cpu_node);
		rc = bnxt_alloc_rx_page_pool(bp, rxr, cpu_node);
		if (rc)
			return rc;

		rc = xdp_rxq_info_reg(&rxr->xdp_rxq, bp->dev, i, 0);
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
			ring = &rxr->rx_agg_ring_struct;
			rc = bnxt_alloc_ring(bp, &ring->ring_mem);
			if (rc)
				return rc;

			ring->grp_idx = i;
			rc = bnxt_alloc_rx_agg_bmap(bp, rxr);
			if (rc)
				return rc;
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

#define BNXT_TC_TO_RING_BASE(bp, tc)	\
	((tc) * (bp)->tx_nr_rings_per_tc)

#define BNXT_RING_TO_TC_OFF(bp, tx)	\
	((tx) % (bp)->tx_nr_rings_per_tc)

#define BNXT_RING_TO_TC(bp, tx)		\
	((tx) / (bp)->tx_nr_rings_per_tc)

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
		spin_lock_init(&txr->xdp_tx_lock);
		if (i < bp->tx_nr_rings_xdp)
			continue;
		if (BNXT_RING_TO_TC_OFF(bp, i) == (bp->tx_nr_rings_per_tc - 1))
			j++;
	}
	return 0;
}

static void bnxt_free_cp_arrays(struct bnxt_cp_ring_info *cpr)
{
	struct bnxt_ring_struct *ring = &cpr->cp_ring_struct;

	kfree(cpr->cp_desc_ring);
	cpr->cp_desc_ring = NULL;
	ring->ring_mem.pg_arr = NULL;
	kfree(cpr->cp_desc_mapping);
	cpr->cp_desc_mapping = NULL;
	ring->ring_mem.dma_arr = NULL;
}

static int bnxt_alloc_cp_arrays(struct bnxt_cp_ring_info *cpr, int n)
{
	cpr->cp_desc_ring = kcalloc(n, sizeof(*cpr->cp_desc_ring), GFP_KERNEL);
	if (!cpr->cp_desc_ring)
		return -ENOMEM;
	cpr->cp_desc_mapping = kcalloc(n, sizeof(*cpr->cp_desc_mapping),
				       GFP_KERNEL);
	if (!cpr->cp_desc_mapping)
		return -ENOMEM;
	return 0;
}

static void bnxt_free_all_cp_arrays(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi)
		return;
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];

		if (!bnapi)
			continue;
		bnxt_free_cp_arrays(&bnapi->cp_ring);
	}
}

static int bnxt_alloc_all_cp_arrays(struct bnxt *bp)
{
	int i, n = bp->cp_nr_pages;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		int rc;

		if (!bnapi)
			continue;
		rc = bnxt_alloc_cp_arrays(&bnapi->cp_ring, n);
		if (rc)
			return rc;
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

		if (!cpr->cp_ring_arr)
			continue;

		for (j = 0; j < cpr->cp_ring_count; j++) {
			struct bnxt_cp_ring_info *cpr2 = &cpr->cp_ring_arr[j];

			ring = &cpr2->cp_ring_struct;
			bnxt_free_ring(bp, &ring->ring_mem);
			bnxt_free_cp_arrays(cpr2);
		}
		kfree(cpr->cp_ring_arr);
		cpr->cp_ring_arr = NULL;
		cpr->cp_ring_count = 0;
	}
}

static int bnxt_alloc_cp_sub_ring(struct bnxt *bp,
				  struct bnxt_cp_ring_info *cpr)
{
	struct bnxt_ring_mem_info *rmem;
	struct bnxt_ring_struct *ring;
	int rc;

	rc = bnxt_alloc_cp_arrays(cpr, bp->cp_nr_pages);
	if (rc) {
		bnxt_free_cp_arrays(cpr);
		return -ENOMEM;
	}
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
		bnxt_free_cp_arrays(cpr);
	}
	return rc;
}

static int bnxt_alloc_cp_rings(struct bnxt *bp)
{
	bool sh = !!(bp->flags & BNXT_FLAG_SHARED_RINGS);
	int i, j, rc, ulp_msix;
	int tcs = bp->num_tc;

	if (!tcs)
		tcs = 1;
	ulp_msix = bnxt_get_ulp_msix_num(bp);
	for (i = 0, j = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr, *cpr2;
		struct bnxt_ring_struct *ring;
		int cp_count = 0, k;
		int rx = 0, tx = 0;

		if (!bnapi)
			continue;

		cpr = &bnapi->cp_ring;
		cpr->bnapi = bnapi;
		ring = &cpr->cp_ring_struct;

		rc = bnxt_alloc_ring(bp, &ring->ring_mem);
		if (rc)
			return rc;

		ring->map_idx = ulp_msix + i;

		if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
			continue;

		if (i < bp->rx_nr_rings) {
			cp_count++;
			rx = 1;
		}
		if (i < bp->tx_nr_rings_xdp) {
			cp_count++;
			tx = 1;
		} else if ((sh && i < bp->tx_nr_rings) ||
			 (!sh && i >= bp->rx_nr_rings)) {
			cp_count += tcs;
			tx = 1;
		}

		cpr->cp_ring_arr = kcalloc(cp_count, sizeof(*cpr),
					   GFP_KERNEL);
		if (!cpr->cp_ring_arr)
			return -ENOMEM;
		cpr->cp_ring_count = cp_count;

		for (k = 0; k < cp_count; k++) {
			cpr2 = &cpr->cp_ring_arr[k];
			rc = bnxt_alloc_cp_sub_ring(bp, cpr2);
			if (rc)
				return rc;
			cpr2->bnapi = bnapi;
			cpr2->sw_stats = cpr->sw_stats;
			cpr2->cp_idx = k;
			if (!k && rx) {
				bp->rx_ring[i].rx_cpr = cpr2;
				cpr2->cp_ring_type = BNXT_NQ_HDL_TYPE_RX;
			} else {
				int n, tc = k - rx;

				n = BNXT_TC_TO_RING_BASE(bp, tc) + j;
				bp->tx_ring[n].tx_cpr = cpr2;
				cpr2->cp_ring_type = BNXT_NQ_HDL_TYPE_TX;
			}
		}
		if (tx)
			j++;
	}
	return 0;
}

static void bnxt_init_rx_ring_struct(struct bnxt *bp,
				     struct bnxt_rx_ring_info *rxr)
{
	struct bnxt_ring_mem_info *rmem;
	struct bnxt_ring_struct *ring;

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
}

static void bnxt_reset_rx_ring_struct(struct bnxt *bp,
				      struct bnxt_rx_ring_info *rxr)
{
	struct bnxt_ring_mem_info *rmem;
	struct bnxt_ring_struct *ring;
	int i;

	rxr->page_pool->p.napi = NULL;
	rxr->page_pool = NULL;
	memset(&rxr->xdp_rxq, 0, sizeof(struct xdp_rxq_info));

	ring = &rxr->rx_ring_struct;
	rmem = &ring->ring_mem;
	rmem->pg_tbl = NULL;
	rmem->pg_tbl_map = 0;
	for (i = 0; i < rmem->nr_pages; i++) {
		rmem->pg_arr[i] = NULL;
		rmem->dma_arr[i] = 0;
	}
	*rmem->vmem = NULL;

	ring = &rxr->rx_agg_ring_struct;
	rmem = &ring->ring_mem;
	rmem->pg_tbl = NULL;
	rmem->pg_tbl_map = 0;
	for (i = 0; i < rmem->nr_pages; i++) {
		rmem->pg_arr[i] = NULL;
		rmem->dma_arr[i] = 0;
	}
	*rmem->vmem = NULL;
}

static void bnxt_init_ring_struct(struct bnxt *bp)
{
	int i, j;

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
		bnxt_for_each_napi_tx(j, bnapi, txr) {
			ring = &txr->tx_ring_struct;
			rmem = &ring->ring_mem;
			rmem->nr_pages = bp->tx_nr_pages;
			rmem->page_size = HW_TXBD_RING_SIZE;
			rmem->pg_arr = (void **)txr->tx_desc_ring;
			rmem->dma_arr = txr->tx_desc_mapping;
			rmem->vmem_size = SW_TXBD_RING_SIZE * bp->tx_nr_pages;
			rmem->vmem = (void **)&txr->tx_buf_ring;
		}
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

static void bnxt_alloc_one_rx_ring_skb(struct bnxt *bp,
				       struct bnxt_rx_ring_info *rxr,
				       int ring_nr)
{
	u32 prod;
	int i;

	prod = rxr->rx_prod;
	for (i = 0; i < bp->rx_ring_size; i++) {
		if (bnxt_alloc_rx_data(bp, rxr, prod, GFP_KERNEL)) {
			netdev_warn(bp->dev, "init'ed rx ring %d with %d/%d skbs only\n",
				    ring_nr, i, bp->rx_ring_size);
			break;
		}
		prod = NEXT_RX(prod);
	}
	rxr->rx_prod = prod;
}

static void bnxt_alloc_one_rx_ring_page(struct bnxt *bp,
					struct bnxt_rx_ring_info *rxr,
					int ring_nr)
{
	u32 prod;
	int i;

	prod = rxr->rx_agg_prod;
	for (i = 0; i < bp->rx_agg_ring_size; i++) {
		if (bnxt_alloc_rx_page(bp, rxr, prod, GFP_KERNEL)) {
			netdev_warn(bp->dev, "init'ed rx ring %d with %d/%d pages only\n",
				    ring_nr, i, bp->rx_ring_size);
			break;
		}
		prod = NEXT_RX_AGG(prod);
	}
	rxr->rx_agg_prod = prod;
}

static int bnxt_alloc_one_tpa_info_data(struct bnxt *bp,
					struct bnxt_rx_ring_info *rxr)
{
	dma_addr_t mapping;
	u8 *data;
	int i;

	for (i = 0; i < bp->max_tpa; i++) {
		data = __bnxt_alloc_rx_frag(bp, &mapping, rxr,
					    GFP_KERNEL);
		if (!data)
			return -ENOMEM;

		rxr->rx_tpa[i].data = data;
		rxr->rx_tpa[i].data_ptr = data + bp->rx_offset;
		rxr->rx_tpa[i].mapping = mapping;
	}

	return 0;
}

static int bnxt_alloc_one_rx_ring(struct bnxt *bp, int ring_nr)
{
	struct bnxt_rx_ring_info *rxr = &bp->rx_ring[ring_nr];
	int rc;

	bnxt_alloc_one_rx_ring_skb(bp, rxr, ring_nr);

	if (!(bp->flags & BNXT_FLAG_AGG_RINGS))
		return 0;

	bnxt_alloc_one_rx_ring_page(bp, rxr, ring_nr);

	if (rxr->rx_tpa) {
		rc = bnxt_alloc_one_tpa_info_data(bp, rxr);
		if (rc)
			return rc;
	}
	return 0;
}

static void bnxt_init_one_rx_ring_rxbd(struct bnxt *bp,
				       struct bnxt_rx_ring_info *rxr)
{
	struct bnxt_ring_struct *ring;
	u32 type;

	type = (bp->rx_buf_use_size << RX_BD_LEN_SHIFT) |
		RX_BD_TYPE_RX_PACKET_BD | RX_BD_FLAGS_EOP;

	if (NET_IP_ALIGN == 2)
		type |= RX_BD_FLAGS_SOP;

	ring = &rxr->rx_ring_struct;
	bnxt_init_rxbd_pages(ring, type);
	ring->fw_ring_id = INVALID_HW_RING_ID;
}

static void bnxt_init_one_rx_agg_ring_rxbd(struct bnxt *bp,
					   struct bnxt_rx_ring_info *rxr)
{
	struct bnxt_ring_struct *ring;
	u32 type;

	ring = &rxr->rx_agg_ring_struct;
	ring->fw_ring_id = INVALID_HW_RING_ID;
	if ((bp->flags & BNXT_FLAG_AGG_RINGS)) {
		type = ((u32)BNXT_RX_PAGE_SIZE << RX_BD_LEN_SHIFT) |
			RX_BD_TYPE_RX_AGG_BD | RX_BD_FLAGS_SOP;

		bnxt_init_rxbd_pages(ring, type);
	}
}

static int bnxt_init_one_rx_ring(struct bnxt *bp, int ring_nr)
{
	struct bnxt_rx_ring_info *rxr;

	rxr = &bp->rx_ring[ring_nr];
	bnxt_init_one_rx_ring_rxbd(bp, rxr);

	netif_queue_set_napi(bp->dev, ring_nr, NETDEV_QUEUE_TYPE_RX,
			     &rxr->bnapi->napi);

	if (BNXT_RX_PAGE_MODE(bp) && bp->xdp_prog) {
		bpf_prog_add(bp->xdp_prog, 1);
		rxr->xdp_prog = bp->xdp_prog;
	}

	bnxt_init_one_rx_agg_ring_rxbd(bp, rxr);

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
		if (!cpr->cp_ring_arr)
			continue;
		for (j = 0; j < cpr->cp_ring_count; j++) {
			struct bnxt_cp_ring_info *cpr2 = &cpr->cp_ring_arr[j];

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
				   BNXT_MIN_TX_DESC_CNT);

	for (i = 0; i < bp->tx_nr_rings; i++) {
		struct bnxt_tx_ring_info *txr = &bp->tx_ring[i];
		struct bnxt_ring_struct *ring = &txr->tx_ring_struct;

		ring->fw_ring_id = INVALID_HW_RING_ID;

		if (i >= bp->tx_nr_rings_xdp)
			netif_queue_set_napi(bp->dev, i - bp->tx_nr_rings_xdp,
					     NETDEV_QUEUE_TYPE_TX,
					     &txr->bnapi->napi);
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
	if (bp->flags & BNXT_FLAG_RFS) {
		if (BNXT_SUPPORTS_NTUPLE_VNIC(bp))
			num_vnics++;
		else if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
			num_vnics += bp->rx_nr_rings;
	}
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
	struct bnxt_vnic_info *vnic0 = &bp->vnic_info[BNXT_VNIC_DEFAULT];
	int i;

	for (i = 0; i < bp->nr_vnics; i++) {
		struct bnxt_vnic_info *vnic = &bp->vnic_info[i];
		int j;

		vnic->fw_vnic_id = INVALID_HW_RING_ID;
		vnic->vnic_id = i;
		for (j = 0; j < BNXT_MAX_CTX_PER_VNIC; j++)
			vnic->fw_rss_cos_lb_ctx[j] = INVALID_HW_RING_ID;

		vnic->fw_l2_ctx_id = INVALID_HW_RING_ID;

		if (bp->vnic_info[i].rss_hash_key) {
			if (i == BNXT_VNIC_DEFAULT) {
				u8 *key = (void *)vnic->rss_hash_key;
				int k;

				if (!bp->rss_hash_key_valid &&
				    !bp->rss_hash_key_updated) {
					get_random_bytes(bp->rss_hash_key,
							 HW_HASH_KEY_SIZE);
					bp->rss_hash_key_updated = true;
				}

				memcpy(vnic->rss_hash_key, bp->rss_hash_key,
				       HW_HASH_KEY_SIZE);

				if (!bp->rss_hash_key_updated)
					continue;

				bp->rss_hash_key_updated = false;
				bp->rss_hash_key_valid = true;

				bp->toeplitz_prefix = 0;
				for (k = 0; k < 8; k++) {
					bp->toeplitz_prefix <<= 8;
					bp->toeplitz_prefix |= key[k];
				}
			} else {
				memcpy(vnic->rss_hash_key, vnic0->rss_hash_key,
				       HW_HASH_KEY_SIZE);
			}
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

static void bnxt_init_ring_params(struct bnxt *bp)
{
	unsigned int rx_size;

	bp->rx_copybreak = BNXT_DEFAULT_RX_COPYBREAK;
	/* Try to fit 4 chunks into a 4k page */
	rx_size = SZ_1K -
		NET_SKB_PAD - SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	bp->dev->cfg->hds_thresh = max(BNXT_DEFAULT_RX_COPYBREAK, rx_size);
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

	rx_space = rx_size + ALIGN(max(NET_SKB_PAD, XDP_PACKET_HEADROOM), 8) +
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	ring_size = bp->rx_ring_size;
	bp->rx_agg_ring_size = 0;
	bp->rx_agg_nr_pages = 0;

	if (bp->flags & BNXT_FLAG_TPA || bp->flags & BNXT_FLAG_HDS)
		agg_factor = min_t(u32, 4, 65536 / BNXT_RX_PAGE_SIZE);

	bp->flags &= ~BNXT_FLAG_JUMBO;
	if (rx_space > PAGE_SIZE && !(bp->flags & BNXT_FLAG_NO_AGG_RINGS)) {
		u32 jumbo_factor;

		bp->flags |= BNXT_FLAG_JUMBO;
		jumbo_factor = PAGE_ALIGN(bp->dev->mtu - 40) >> PAGE_SHIFT;
		if (jumbo_factor > agg_factor)
			agg_factor = jumbo_factor;
	}
	if (agg_factor) {
		if (ring_size > BNXT_MAX_RX_DESC_CNT_JUM_ENA) {
			ring_size = BNXT_MAX_RX_DESC_CNT_JUM_ENA;
			netdev_warn(bp->dev, "RX ring size reduced from %d to %d because the jumbo ring is now enabled\n",
				    bp->rx_ring_size, ring_size);
			bp->rx_ring_size = ring_size;
		}
		agg_ring_size = ring_size * agg_factor;

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

		if (BNXT_RX_PAGE_MODE(bp)) {
			rx_space = PAGE_SIZE;
			rx_size = PAGE_SIZE -
				  ALIGN(max(NET_SKB_PAD, XDP_PACKET_HEADROOM), 8) -
				  SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
		} else {
			rx_size = max3(BNXT_DEFAULT_RX_COPYBREAK,
				       bp->rx_copybreak,
				       bp->dev->cfg_pending->hds_thresh);
			rx_size = SKB_DATA_ALIGN(rx_size + NET_IP_ALIGN);
			rx_space = rx_size + NET_SKB_PAD +
				SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
		}
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
static void __bnxt_set_rx_skb_mode(struct bnxt *bp, bool page_mode)
{
	struct net_device *dev = bp->dev;

	if (page_mode) {
		bp->flags &= ~(BNXT_FLAG_AGG_RINGS | BNXT_FLAG_NO_AGG_RINGS);
		bp->flags |= BNXT_FLAG_RX_PAGE_MODE;

		if (bp->xdp_prog->aux->xdp_has_frags)
			dev->max_mtu = min_t(u16, bp->max_mtu, BNXT_MAX_MTU);
		else
			dev->max_mtu =
				min_t(u16, bp->max_mtu, BNXT_MAX_PAGE_MODE_MTU);
		if (dev->mtu > BNXT_MAX_PAGE_MODE_MTU) {
			bp->flags |= BNXT_FLAG_JUMBO;
			bp->rx_skb_func = bnxt_rx_multi_page_skb;
		} else {
			bp->flags |= BNXT_FLAG_NO_AGG_RINGS;
			bp->rx_skb_func = bnxt_rx_page_skb;
		}
		bp->rx_dir = DMA_BIDIRECTIONAL;
	} else {
		dev->max_mtu = bp->max_mtu;
		bp->flags &= ~BNXT_FLAG_RX_PAGE_MODE;
		bp->rx_dir = DMA_FROM_DEVICE;
		bp->rx_skb_func = bnxt_rx_skb;
	}
}

void bnxt_set_rx_skb_mode(struct bnxt *bp, bool page_mode)
{
	__bnxt_set_rx_skb_mode(bp, page_mode);

	if (!page_mode) {
		int rx, tx;

		bnxt_get_max_rings(bp, &rx, &tx, true);
		if (rx > 1) {
			bp->flags &= ~BNXT_FLAG_NO_AGG_RINGS;
			bp->dev->hw_features |= NETIF_F_LRO;
		}
	}

	/* Update LRO and GRO_HW availability */
	netdev_update_features(bp->dev);
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

		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
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
		if ((bp->rss_cap & BNXT_RSS_CAP_NEW_RSS_CAP) &&
		    !(vnic->flags & BNXT_VNIC_RSS_FLAG))
			continue;

		/* Allocate rss table and hash key */
		size = L1_CACHE_ALIGN(HW_HASH_INDEX_SIZE * sizeof(u16));
		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
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
	struct bnxt_hwrm_wait_token *token;

	dma_pool_destroy(bp->hwrm_dma_pool);
	bp->hwrm_dma_pool = NULL;

	rcu_read_lock();
	hlist_for_each_entry_rcu(token, &bp->hwrm_pending_list, node)
		WRITE_ONCE(token->state, BNXT_HWRM_CANCELLED);
	rcu_read_unlock();
}

static int bnxt_alloc_hwrm_resources(struct bnxt *bp)
{
	bp->hwrm_dma_pool = dma_pool_create("bnxt_hwrm", &bp->pdev->dev,
					    BNXT_HWRM_DMA_SIZE,
					    BNXT_HWRM_DMA_ALIGN, 0);
	if (!bp->hwrm_dma_pool)
		return -ENOMEM;

	INIT_HLIST_HEAD(&bp->hwrm_pending_list);

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
	struct hwrm_func_qstats_ext_output *resp;
	struct hwrm_func_qstats_ext_input *req;
	__le64 *hw_masks;
	int rc;

	if (!(bp->fw_cap & BNXT_FW_CAP_EXT_HW_STATS_SUPPORTED) ||
	    !(bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
		return -EOPNOTSUPP;

	rc = hwrm_req_init(bp, req, HWRM_FUNC_QSTATS_EXT);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(0xffff);
	req->flags = FUNC_QSTATS_EXT_REQ_FLAGS_COUNTER_MASK;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc) {
		hw_masks = &resp->rx_ucast_pkts;
		bnxt_copy_hw_masks(stats->hw_masks, hw_masks, stats->len / 8);
	}
	hwrm_req_drop(bp, req);
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
		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
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

		kfree(cpr->sw_stats);
		cpr->sw_stats = NULL;
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

		cpr->sw_stats = kzalloc(sizeof(*cpr->sw_stats), GFP_KERNEL);
		if (!cpr->sw_stats)
			return -ENOMEM;

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
	int i, j;

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

		bnxt_for_each_napi_tx(j, bnapi, txr) {
			txr->tx_prod = 0;
			txr->tx_cons = 0;
			txr->tx_hw_cons = 0;
		}

		rxr = bnapi->rx_ring;
		if (rxr) {
			rxr->rx_prod = 0;
			rxr->rx_agg_prod = 0;
			rxr->rx_sw_agg_prod = 0;
			rxr->rx_next_cons = 0;
		}
		bnapi->events = 0;
	}
}

void bnxt_insert_usr_fltr(struct bnxt *bp, struct bnxt_filter_base *fltr)
{
	u8 type = fltr->type, flags = fltr->flags;

	INIT_LIST_HEAD(&fltr->list);
	if ((type == BNXT_FLTR_TYPE_L2 && flags & BNXT_ACT_RING_DST) ||
	    (type == BNXT_FLTR_TYPE_NTUPLE && flags & BNXT_ACT_NO_AGING))
		list_add_tail(&fltr->list, &bp->usr_fltr_list);
}

void bnxt_del_one_usr_fltr(struct bnxt *bp, struct bnxt_filter_base *fltr)
{
	if (!list_empty(&fltr->list))
		list_del_init(&fltr->list);
}

static void bnxt_clear_usr_fltrs(struct bnxt *bp, bool all)
{
	struct bnxt_filter_base *usr_fltr, *tmp;

	list_for_each_entry_safe(usr_fltr, tmp, &bp->usr_fltr_list, list) {
		if (!all && usr_fltr->type == BNXT_FLTR_TYPE_L2)
			continue;
		bnxt_del_one_usr_fltr(bp, usr_fltr);
	}
}

static void bnxt_del_fltr(struct bnxt *bp, struct bnxt_filter_base *fltr)
{
	hlist_del(&fltr->hash);
	bnxt_del_one_usr_fltr(bp, fltr);
	if (fltr->flags) {
		clear_bit(fltr->sw_id, bp->ntp_fltr_bmap);
		bp->ntp_fltr_count--;
	}
	kfree(fltr);
}

static void bnxt_free_ntp_fltrs(struct bnxt *bp, bool all)
{
	int i;

	netdev_assert_locked(bp->dev);

	/* Under netdev instance lock and all our NAPIs have been disabled.
	 * It's safe to delete the hash table.
	 */
	for (i = 0; i < BNXT_NTP_FLTR_HASH_SIZE; i++) {
		struct hlist_head *head;
		struct hlist_node *tmp;
		struct bnxt_ntuple_filter *fltr;

		head = &bp->ntp_fltr_hash_tbl[i];
		hlist_for_each_entry_safe(fltr, tmp, head, base.hash) {
			bnxt_del_l2_filter(bp, fltr->l2_fltr);
			if (!all && ((fltr->base.flags & BNXT_ACT_FUNC_DST) ||
				     !list_empty(&fltr->base.list)))
				continue;
			bnxt_del_fltr(bp, &fltr->base);
		}
	}
	if (!all)
		return;

	bitmap_free(bp->ntp_fltr_bmap);
	bp->ntp_fltr_bmap = NULL;
	bp->ntp_fltr_count = 0;
}

static int bnxt_alloc_ntp_fltrs(struct bnxt *bp)
{
	int i, rc = 0;

	if (!(bp->flags & BNXT_FLAG_RFS) || bp->ntp_fltr_bmap)
		return 0;

	for (i = 0; i < BNXT_NTP_FLTR_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&bp->ntp_fltr_hash_tbl[i]);

	bp->ntp_fltr_count = 0;
	bp->ntp_fltr_bmap = bitmap_zalloc(bp->max_fltr, GFP_KERNEL);

	if (!bp->ntp_fltr_bmap)
		rc = -ENOMEM;

	return rc;
}

static void bnxt_free_l2_filters(struct bnxt *bp, bool all)
{
	int i;

	for (i = 0; i < BNXT_L2_FLTR_HASH_SIZE; i++) {
		struct hlist_head *head;
		struct hlist_node *tmp;
		struct bnxt_l2_filter *fltr;

		head = &bp->l2_fltr_hash_tbl[i];
		hlist_for_each_entry_safe(fltr, tmp, head, base.hash) {
			if (!all && ((fltr->base.flags & BNXT_ACT_FUNC_DST) ||
				     !list_empty(&fltr->base.list)))
				continue;
			bnxt_del_fltr(bp, &fltr->base);
		}
	}
}

static void bnxt_init_l2_fltr_tbl(struct bnxt *bp)
{
	int i;

	for (i = 0; i < BNXT_L2_FLTR_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&bp->l2_fltr_hash_tbl[i]);
	get_random_bytes(&bp->hash_seed, sizeof(bp->hash_seed));
}

static void bnxt_free_mem(struct bnxt *bp, bool irq_re_init)
{
	bnxt_free_vnic_attributes(bp);
	bnxt_free_tx_rings(bp);
	bnxt_free_rx_rings(bp);
	bnxt_free_cp_rings(bp);
	bnxt_free_all_cp_arrays(bp);
	bnxt_free_ntp_fltrs(bp, false);
	bnxt_free_l2_filters(bp, false);
	if (irq_re_init) {
		bnxt_free_ring_stats(bp);
		if (!(bp->phy_flags & BNXT_PHY_FL_PORT_STATS_NO_RESET) ||
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
			if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
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

			if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
				rxr->rx_ring_struct.ring_mem.flags =
					BNXT_RMEM_RING_PTE_FLAG;
				rxr->rx_agg_ring_struct.ring_mem.flags =
					BNXT_RMEM_RING_PTE_FLAG;
			} else {
				rxr->rx_cpr =  &bp->bnapi[i]->cp_ring;
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

		for (i = 0; i < bp->tx_nr_rings; i++) {
			struct bnxt_tx_ring_info *txr = &bp->tx_ring[i];
			struct bnxt_napi *bnapi2;

			if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
				txr->tx_ring_struct.ring_mem.flags =
					BNXT_RMEM_RING_PTE_FLAG;
			bp->tx_ring_map[i] = bp->tx_nr_rings_xdp + i;
			if (i >= bp->tx_nr_rings_xdp) {
				int k = j + BNXT_RING_TO_TC_OFF(bp, i);

				bnapi2 = bp->bnapi[k];
				txr->txq_index = i - bp->tx_nr_rings_xdp;
				txr->tx_napi_idx =
					BNXT_RING_TO_TC(bp, txr->txq_index);
				bnapi2->tx_ring[txr->tx_napi_idx] = txr;
				bnapi2->tx_int = bnxt_tx_int;
			} else {
				bnapi2 = bp->bnapi[j];
				bnapi2->flags |= BNXT_NAPI_FLAG_XDP;
				bnapi2->tx_ring[0] = txr;
				bnapi2->tx_int = bnxt_tx_int_xdp;
				j++;
			}
			txr->bnapi = bnapi2;
			if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
				txr->tx_cpr = &bnapi2->cp_ring;
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

	rc = bnxt_alloc_all_cp_arrays(bp);
	if (rc)
		goto alloc_mem_err;

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

	bp->vnic_info[BNXT_VNIC_DEFAULT].flags |= BNXT_VNIC_RSS_FLAG |
						  BNXT_VNIC_MCAST_FLAG |
						  BNXT_VNIC_UCAST_FLAG;
	if (BNXT_SUPPORTS_NTUPLE_VNIC(bp) && (bp->flags & BNXT_FLAG_RFS))
		bp->vnic_info[BNXT_VNIC_NTUPLE].flags |=
			BNXT_VNIC_RSS_FLAG | BNXT_VNIC_NTUPLE_FLAG;

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

	if (!bp->irq_tbl)
		return;

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

int bnxt_hwrm_func_drv_rgtr(struct bnxt *bp, unsigned long *bmap, int bmap_size,
			    bool async_only)
{
	DECLARE_BITMAP(async_events_bmap, 256);
	u32 *events = (u32 *)async_events_bmap;
	struct hwrm_func_drv_rgtr_output *resp;
	struct hwrm_func_drv_rgtr_input *req;
	u32 flags;
	int rc, i;

	rc = hwrm_req_init(bp, req, HWRM_FUNC_DRV_RGTR);
	if (rc)
		return rc;

	req->enables = cpu_to_le32(FUNC_DRV_RGTR_REQ_ENABLES_OS_TYPE |
				   FUNC_DRV_RGTR_REQ_ENABLES_VER |
				   FUNC_DRV_RGTR_REQ_ENABLES_ASYNC_EVENT_FWD);

	req->os_type = cpu_to_le16(FUNC_DRV_RGTR_REQ_OS_TYPE_LINUX);
	flags = FUNC_DRV_RGTR_REQ_FLAGS_16BIT_VER_MODE;
	if (bp->fw_cap & BNXT_FW_CAP_HOT_RESET)
		flags |= FUNC_DRV_RGTR_REQ_FLAGS_HOT_RESET_SUPPORT;
	if (bp->fw_cap & BNXT_FW_CAP_ERROR_RECOVERY)
		flags |= FUNC_DRV_RGTR_REQ_FLAGS_ERROR_RECOVERY_SUPPORT |
			 FUNC_DRV_RGTR_REQ_FLAGS_MASTER_SUPPORT;
	if (bp->fw_cap & BNXT_FW_CAP_NPAR_1_2)
		flags |= FUNC_DRV_RGTR_REQ_FLAGS_NPAR_1_2_SUPPORT;
	req->flags = cpu_to_le32(flags);
	req->ver_maj_8b = DRV_VER_MAJ;
	req->ver_min_8b = DRV_VER_MIN;
	req->ver_upd_8b = DRV_VER_UPD;
	req->ver_maj = cpu_to_le16(DRV_VER_MAJ);
	req->ver_min = cpu_to_le16(DRV_VER_MIN);
	req->ver_upd = cpu_to_le16(DRV_VER_UPD);

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
			req->vf_req_fwd[i] = cpu_to_le32(data[i]);

		req->enables |=
			cpu_to_le32(FUNC_DRV_RGTR_REQ_ENABLES_VF_REQ_FWD);
	}

	if (bp->fw_cap & BNXT_FW_CAP_OVS_64BIT_HANDLE)
		req->flags |= cpu_to_le32(
			FUNC_DRV_RGTR_REQ_FLAGS_FLOW_HANDLE_64BIT_MODE);

	memset(async_events_bmap, 0, sizeof(async_events_bmap));
	for (i = 0; i < ARRAY_SIZE(bnxt_async_events_arr); i++) {
		u16 event_id = bnxt_async_events_arr[i];

		if (event_id == ASYNC_EVENT_CMPL_EVENT_ID_ERROR_RECOVERY &&
		    !(bp->fw_cap & BNXT_FW_CAP_ERROR_RECOVERY))
			continue;
		if (event_id == ASYNC_EVENT_CMPL_EVENT_ID_PHC_UPDATE &&
		    !bp->ptp_cfg)
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
		req->async_event_fwd[i] |= cpu_to_le32(events[i]);

	if (async_only)
		req->enables =
			cpu_to_le32(FUNC_DRV_RGTR_REQ_ENABLES_ASYNC_EVENT_FWD);

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc) {
		set_bit(BNXT_STATE_DRV_REGISTERED, &bp->state);
		if (resp->flags &
		    cpu_to_le32(FUNC_DRV_RGTR_RESP_FLAGS_IF_CHANGE_SUPPORTED))
			bp->fw_cap |= BNXT_FW_CAP_IF_CHANGE;
	}
	hwrm_req_drop(bp, req);
	return rc;
}

int bnxt_hwrm_func_drv_unrgtr(struct bnxt *bp)
{
	struct hwrm_func_drv_unrgtr_input *req;
	int rc;

	if (!test_and_clear_bit(BNXT_STATE_DRV_REGISTERED, &bp->state))
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_FUNC_DRV_UNRGTR);
	if (rc)
		return rc;
	return hwrm_req_send(bp, req);
}

static int bnxt_set_tpa(struct bnxt *bp, bool set_tpa);

static int bnxt_hwrm_tunnel_dst_port_free(struct bnxt *bp, u8 tunnel_type)
{
	struct hwrm_tunnel_dst_port_free_input *req;
	int rc;

	if (tunnel_type == TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_VXLAN &&
	    bp->vxlan_fw_dst_port_id == INVALID_HW_RING_ID)
		return 0;
	if (tunnel_type == TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_GENEVE &&
	    bp->nge_fw_dst_port_id == INVALID_HW_RING_ID)
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_TUNNEL_DST_PORT_FREE);
	if (rc)
		return rc;

	req->tunnel_type = tunnel_type;

	switch (tunnel_type) {
	case TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_VXLAN:
		req->tunnel_dst_port_id = cpu_to_le16(bp->vxlan_fw_dst_port_id);
		bp->vxlan_port = 0;
		bp->vxlan_fw_dst_port_id = INVALID_HW_RING_ID;
		break;
	case TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_GENEVE:
		req->tunnel_dst_port_id = cpu_to_le16(bp->nge_fw_dst_port_id);
		bp->nge_port = 0;
		bp->nge_fw_dst_port_id = INVALID_HW_RING_ID;
		break;
	case TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_VXLAN_GPE:
		req->tunnel_dst_port_id = cpu_to_le16(bp->vxlan_gpe_fw_dst_port_id);
		bp->vxlan_gpe_port = 0;
		bp->vxlan_gpe_fw_dst_port_id = INVALID_HW_RING_ID;
		break;
	default:
		break;
	}

	rc = hwrm_req_send(bp, req);
	if (rc)
		netdev_err(bp->dev, "hwrm_tunnel_dst_port_free failed. rc:%d\n",
			   rc);
	if (bp->flags & BNXT_FLAG_TPA)
		bnxt_set_tpa(bp, true);
	return rc;
}

static int bnxt_hwrm_tunnel_dst_port_alloc(struct bnxt *bp, __be16 port,
					   u8 tunnel_type)
{
	struct hwrm_tunnel_dst_port_alloc_output *resp;
	struct hwrm_tunnel_dst_port_alloc_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_TUNNEL_DST_PORT_ALLOC);
	if (rc)
		return rc;

	req->tunnel_type = tunnel_type;
	req->tunnel_dst_port_val = port;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (rc) {
		netdev_err(bp->dev, "hwrm_tunnel_dst_port_alloc failed. rc:%d\n",
			   rc);
		goto err_out;
	}

	switch (tunnel_type) {
	case TUNNEL_DST_PORT_ALLOC_REQ_TUNNEL_TYPE_VXLAN:
		bp->vxlan_port = port;
		bp->vxlan_fw_dst_port_id =
			le16_to_cpu(resp->tunnel_dst_port_id);
		break;
	case TUNNEL_DST_PORT_ALLOC_REQ_TUNNEL_TYPE_GENEVE:
		bp->nge_port = port;
		bp->nge_fw_dst_port_id = le16_to_cpu(resp->tunnel_dst_port_id);
		break;
	case TUNNEL_DST_PORT_ALLOC_REQ_TUNNEL_TYPE_VXLAN_GPE:
		bp->vxlan_gpe_port = port;
		bp->vxlan_gpe_fw_dst_port_id =
			le16_to_cpu(resp->tunnel_dst_port_id);
		break;
	default:
		break;
	}
	if (bp->flags & BNXT_FLAG_TPA)
		bnxt_set_tpa(bp, true);

err_out:
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_hwrm_cfa_l2_set_rx_mask(struct bnxt *bp, u16 vnic_id)
{
	struct hwrm_cfa_l2_set_rx_mask_input *req;
	struct bnxt_vnic_info *vnic = &bp->vnic_info[vnic_id];
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_CFA_L2_SET_RX_MASK);
	if (rc)
		return rc;

	req->vnic_id = cpu_to_le32(vnic->fw_vnic_id);
	if (vnic->rx_mask & CFA_L2_SET_RX_MASK_REQ_MASK_MCAST) {
		req->num_mc_entries = cpu_to_le32(vnic->mc_list_count);
		req->mc_tbl_addr = cpu_to_le64(vnic->mc_list_mapping);
	}
	req->mask = cpu_to_le32(vnic->rx_mask);
	return hwrm_req_send_silent(bp, req);
}

void bnxt_del_l2_filter(struct bnxt *bp, struct bnxt_l2_filter *fltr)
{
	if (!atomic_dec_and_test(&fltr->refcnt))
		return;
	spin_lock_bh(&bp->ntp_fltr_lock);
	if (!test_and_clear_bit(BNXT_FLTR_INSERTED, &fltr->base.state)) {
		spin_unlock_bh(&bp->ntp_fltr_lock);
		return;
	}
	hlist_del_rcu(&fltr->base.hash);
	bnxt_del_one_usr_fltr(bp, &fltr->base);
	if (fltr->base.flags) {
		clear_bit(fltr->base.sw_id, bp->ntp_fltr_bmap);
		bp->ntp_fltr_count--;
	}
	spin_unlock_bh(&bp->ntp_fltr_lock);
	kfree_rcu(fltr, base.rcu);
}

static struct bnxt_l2_filter *__bnxt_lookup_l2_filter(struct bnxt *bp,
						      struct bnxt_l2_key *key,
						      u32 idx)
{
	struct hlist_head *head = &bp->l2_fltr_hash_tbl[idx];
	struct bnxt_l2_filter *fltr;

	hlist_for_each_entry_rcu(fltr, head, base.hash) {
		struct bnxt_l2_key *l2_key = &fltr->l2_key;

		if (ether_addr_equal(l2_key->dst_mac_addr, key->dst_mac_addr) &&
		    l2_key->vlan == key->vlan)
			return fltr;
	}
	return NULL;
}

static struct bnxt_l2_filter *bnxt_lookup_l2_filter(struct bnxt *bp,
						    struct bnxt_l2_key *key,
						    u32 idx)
{
	struct bnxt_l2_filter *fltr = NULL;

	rcu_read_lock();
	fltr = __bnxt_lookup_l2_filter(bp, key, idx);
	if (fltr)
		atomic_inc(&fltr->refcnt);
	rcu_read_unlock();
	return fltr;
}

#define BNXT_IPV4_4TUPLE(bp, fkeys)					\
	(((fkeys)->basic.ip_proto == IPPROTO_TCP &&			\
	  (bp)->rss_hash_cfg & VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV4) ||	\
	 ((fkeys)->basic.ip_proto == IPPROTO_UDP &&			\
	  (bp)->rss_hash_cfg & VNIC_RSS_CFG_REQ_HASH_TYPE_UDP_IPV4))

#define BNXT_IPV6_4TUPLE(bp, fkeys)					\
	(((fkeys)->basic.ip_proto == IPPROTO_TCP &&			\
	  (bp)->rss_hash_cfg & VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV6) ||	\
	 ((fkeys)->basic.ip_proto == IPPROTO_UDP &&			\
	  (bp)->rss_hash_cfg & VNIC_RSS_CFG_REQ_HASH_TYPE_UDP_IPV6))

static u32 bnxt_get_rss_flow_tuple_len(struct bnxt *bp, struct flow_keys *fkeys)
{
	if (fkeys->basic.n_proto == htons(ETH_P_IP)) {
		if (BNXT_IPV4_4TUPLE(bp, fkeys))
			return sizeof(fkeys->addrs.v4addrs) +
			       sizeof(fkeys->ports);

		if (bp->rss_hash_cfg & VNIC_RSS_CFG_REQ_HASH_TYPE_IPV4)
			return sizeof(fkeys->addrs.v4addrs);
	}

	if (fkeys->basic.n_proto == htons(ETH_P_IPV6)) {
		if (BNXT_IPV6_4TUPLE(bp, fkeys))
			return sizeof(fkeys->addrs.v6addrs) +
			       sizeof(fkeys->ports);

		if (bp->rss_hash_cfg & VNIC_RSS_CFG_REQ_HASH_TYPE_IPV6)
			return sizeof(fkeys->addrs.v6addrs);
	}

	return 0;
}

static u32 bnxt_toeplitz(struct bnxt *bp, struct flow_keys *fkeys,
			 const unsigned char *key)
{
	u64 prefix = bp->toeplitz_prefix, hash = 0;
	struct bnxt_ipv4_tuple tuple4;
	struct bnxt_ipv6_tuple tuple6;
	int i, j, len = 0;
	u8 *four_tuple;

	len = bnxt_get_rss_flow_tuple_len(bp, fkeys);
	if (!len)
		return 0;

	if (fkeys->basic.n_proto == htons(ETH_P_IP)) {
		tuple4.v4addrs = fkeys->addrs.v4addrs;
		tuple4.ports = fkeys->ports;
		four_tuple = (unsigned char *)&tuple4;
	} else {
		tuple6.v6addrs = fkeys->addrs.v6addrs;
		tuple6.ports = fkeys->ports;
		four_tuple = (unsigned char *)&tuple6;
	}

	for (i = 0, j = 8; i < len; i++, j++) {
		u8 byte = four_tuple[i];
		int bit;

		for (bit = 0; bit < 8; bit++, prefix <<= 1, byte <<= 1) {
			if (byte & 0x80)
				hash ^= prefix;
		}
		prefix |= (j < HW_HASH_KEY_SIZE) ? key[j] : 0;
	}

	/* The valid part of the hash is in the upper 32 bits. */
	return (hash >> 32) & BNXT_NTP_FLTR_HASH_MASK;
}

#ifdef CONFIG_RFS_ACCEL
static struct bnxt_l2_filter *
bnxt_lookup_l2_filter_from_key(struct bnxt *bp, struct bnxt_l2_key *key)
{
	struct bnxt_l2_filter *fltr;
	u32 idx;

	idx = jhash2(&key->filter_key, BNXT_L2_KEY_SIZE, bp->hash_seed) &
	      BNXT_L2_FLTR_HASH_MASK;
	fltr = bnxt_lookup_l2_filter(bp, key, idx);
	return fltr;
}
#endif

static int bnxt_init_l2_filter(struct bnxt *bp, struct bnxt_l2_filter *fltr,
			       struct bnxt_l2_key *key, u32 idx)
{
	struct hlist_head *head;

	ether_addr_copy(fltr->l2_key.dst_mac_addr, key->dst_mac_addr);
	fltr->l2_key.vlan = key->vlan;
	fltr->base.type = BNXT_FLTR_TYPE_L2;
	if (fltr->base.flags) {
		int bit_id;

		bit_id = bitmap_find_free_region(bp->ntp_fltr_bmap,
						 bp->max_fltr, 0);
		if (bit_id < 0)
			return -ENOMEM;
		fltr->base.sw_id = (u16)bit_id;
		bp->ntp_fltr_count++;
	}
	head = &bp->l2_fltr_hash_tbl[idx];
	hlist_add_head_rcu(&fltr->base.hash, head);
	bnxt_insert_usr_fltr(bp, &fltr->base);
	set_bit(BNXT_FLTR_INSERTED, &fltr->base.state);
	atomic_set(&fltr->refcnt, 1);
	return 0;
}

static struct bnxt_l2_filter *bnxt_alloc_l2_filter(struct bnxt *bp,
						   struct bnxt_l2_key *key,
						   gfp_t gfp)
{
	struct bnxt_l2_filter *fltr;
	u32 idx;
	int rc;

	idx = jhash2(&key->filter_key, BNXT_L2_KEY_SIZE, bp->hash_seed) &
	      BNXT_L2_FLTR_HASH_MASK;
	fltr = bnxt_lookup_l2_filter(bp, key, idx);
	if (fltr)
		return fltr;

	fltr = kzalloc(sizeof(*fltr), gfp);
	if (!fltr)
		return ERR_PTR(-ENOMEM);
	spin_lock_bh(&bp->ntp_fltr_lock);
	rc = bnxt_init_l2_filter(bp, fltr, key, idx);
	spin_unlock_bh(&bp->ntp_fltr_lock);
	if (rc) {
		bnxt_del_l2_filter(bp, fltr);
		fltr = ERR_PTR(rc);
	}
	return fltr;
}

struct bnxt_l2_filter *bnxt_alloc_new_l2_filter(struct bnxt *bp,
						struct bnxt_l2_key *key,
						u16 flags)
{
	struct bnxt_l2_filter *fltr;
	u32 idx;
	int rc;

	idx = jhash2(&key->filter_key, BNXT_L2_KEY_SIZE, bp->hash_seed) &
	      BNXT_L2_FLTR_HASH_MASK;
	spin_lock_bh(&bp->ntp_fltr_lock);
	fltr = __bnxt_lookup_l2_filter(bp, key, idx);
	if (fltr) {
		fltr = ERR_PTR(-EEXIST);
		goto l2_filter_exit;
	}
	fltr = kzalloc(sizeof(*fltr), GFP_ATOMIC);
	if (!fltr) {
		fltr = ERR_PTR(-ENOMEM);
		goto l2_filter_exit;
	}
	fltr->base.flags = flags;
	rc = bnxt_init_l2_filter(bp, fltr, key, idx);
	if (rc) {
		spin_unlock_bh(&bp->ntp_fltr_lock);
		bnxt_del_l2_filter(bp, fltr);
		return ERR_PTR(rc);
	}

l2_filter_exit:
	spin_unlock_bh(&bp->ntp_fltr_lock);
	return fltr;
}

static u16 bnxt_vf_target_id(struct bnxt_pf_info *pf, u16 vf_idx)
{
#ifdef CONFIG_BNXT_SRIOV
	struct bnxt_vf_info *vf = &pf->vf[vf_idx];

	return vf->fw_fid;
#else
	return INVALID_HW_RING_ID;
#endif
}

int bnxt_hwrm_l2_filter_free(struct bnxt *bp, struct bnxt_l2_filter *fltr)
{
	struct hwrm_cfa_l2_filter_free_input *req;
	u16 target_id = 0xffff;
	int rc;

	if (fltr->base.flags & BNXT_ACT_FUNC_DST) {
		struct bnxt_pf_info *pf = &bp->pf;

		if (fltr->base.vf_idx >= pf->active_vfs)
			return -EINVAL;

		target_id = bnxt_vf_target_id(pf, fltr->base.vf_idx);
		if (target_id == INVALID_HW_RING_ID)
			return -EINVAL;
	}

	rc = hwrm_req_init(bp, req, HWRM_CFA_L2_FILTER_FREE);
	if (rc)
		return rc;

	req->target_id = cpu_to_le16(target_id);
	req->l2_filter_id = fltr->base.filter_id;
	return hwrm_req_send(bp, req);
}

int bnxt_hwrm_l2_filter_alloc(struct bnxt *bp, struct bnxt_l2_filter *fltr)
{
	struct hwrm_cfa_l2_filter_alloc_output *resp;
	struct hwrm_cfa_l2_filter_alloc_input *req;
	u16 target_id = 0xffff;
	int rc;

	if (fltr->base.flags & BNXT_ACT_FUNC_DST) {
		struct bnxt_pf_info *pf = &bp->pf;

		if (fltr->base.vf_idx >= pf->active_vfs)
			return -EINVAL;

		target_id = bnxt_vf_target_id(pf, fltr->base.vf_idx);
	}
	rc = hwrm_req_init(bp, req, HWRM_CFA_L2_FILTER_ALLOC);
	if (rc)
		return rc;

	req->target_id = cpu_to_le16(target_id);
	req->flags = cpu_to_le32(CFA_L2_FILTER_ALLOC_REQ_FLAGS_PATH_RX);

	if (!BNXT_CHIP_TYPE_NITRO_A0(bp))
		req->flags |=
			cpu_to_le32(CFA_L2_FILTER_ALLOC_REQ_FLAGS_OUTERMOST);
	req->dst_id = cpu_to_le16(fltr->base.fw_vnic_id);
	req->enables =
		cpu_to_le32(CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_ADDR |
			    CFA_L2_FILTER_ALLOC_REQ_ENABLES_DST_ID |
			    CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_ADDR_MASK);
	ether_addr_copy(req->l2_addr, fltr->l2_key.dst_mac_addr);
	eth_broadcast_addr(req->l2_addr_mask);

	if (fltr->l2_key.vlan) {
		req->enables |=
			cpu_to_le32(CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_IVLAN |
				CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_IVLAN_MASK |
				CFA_L2_FILTER_ALLOC_REQ_ENABLES_NUM_VLANS);
		req->num_vlans = 1;
		req->l2_ivlan = cpu_to_le16(fltr->l2_key.vlan);
		req->l2_ivlan_mask = cpu_to_le16(0xfff);
	}

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc) {
		fltr->base.filter_id = resp->l2_filter_id;
		set_bit(BNXT_FLTR_VALID, &fltr->base.state);
	}
	hwrm_req_drop(bp, req);
	return rc;
}

int bnxt_hwrm_cfa_ntuple_filter_free(struct bnxt *bp,
				     struct bnxt_ntuple_filter *fltr)
{
	struct hwrm_cfa_ntuple_filter_free_input *req;
	int rc;

	set_bit(BNXT_FLTR_FW_DELETED, &fltr->base.state);
	rc = hwrm_req_init(bp, req, HWRM_CFA_NTUPLE_FILTER_FREE);
	if (rc)
		return rc;

	req->ntuple_filter_id = fltr->base.filter_id;
	return hwrm_req_send(bp, req);
}

#define BNXT_NTP_FLTR_FLAGS					\
	(CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_L2_FILTER_ID |	\
	 CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_ETHERTYPE |	\
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

void bnxt_fill_ipv6_mask(__be32 mask[4])
{
	int i;

	for (i = 0; i < 4; i++)
		mask[i] = cpu_to_be32(~0);
}

static void
bnxt_cfg_rfs_ring_tbl_idx(struct bnxt *bp,
			  struct hwrm_cfa_ntuple_filter_alloc_input *req,
			  struct bnxt_ntuple_filter *fltr)
{
	u16 rxq = fltr->base.rxq;

	if (fltr->base.flags & BNXT_ACT_RSS_CTX) {
		struct ethtool_rxfh_context *ctx;
		struct bnxt_rss_ctx *rss_ctx;
		struct bnxt_vnic_info *vnic;

		ctx = xa_load(&bp->dev->ethtool->rss_ctx,
			      fltr->base.fw_vnic_id);
		if (ctx) {
			rss_ctx = ethtool_rxfh_context_priv(ctx);
			vnic = &rss_ctx->vnic;

			req->dst_id = cpu_to_le16(vnic->fw_vnic_id);
		}
		return;
	}
	if (BNXT_SUPPORTS_NTUPLE_VNIC(bp)) {
		struct bnxt_vnic_info *vnic;
		u32 enables;

		vnic = &bp->vnic_info[BNXT_VNIC_NTUPLE];
		req->dst_id = cpu_to_le16(vnic->fw_vnic_id);
		enables = CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_RFS_RING_TBL_IDX;
		req->enables |= cpu_to_le32(enables);
		req->rfs_ring_tbl_idx = cpu_to_le16(rxq);
	} else {
		u32 flags;

		flags = CFA_NTUPLE_FILTER_ALLOC_REQ_FLAGS_DEST_RFS_RING_IDX;
		req->flags |= cpu_to_le32(flags);
		req->dst_id = cpu_to_le16(rxq);
	}
}

int bnxt_hwrm_cfa_ntuple_filter_alloc(struct bnxt *bp,
				      struct bnxt_ntuple_filter *fltr)
{
	struct hwrm_cfa_ntuple_filter_alloc_output *resp;
	struct hwrm_cfa_ntuple_filter_alloc_input *req;
	struct bnxt_flow_masks *masks = &fltr->fmasks;
	struct flow_keys *keys = &fltr->fkeys;
	struct bnxt_l2_filter *l2_fltr;
	struct bnxt_vnic_info *vnic;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_CFA_NTUPLE_FILTER_ALLOC);
	if (rc)
		return rc;

	l2_fltr = fltr->l2_fltr;
	req->l2_filter_id = l2_fltr->base.filter_id;

	if (fltr->base.flags & BNXT_ACT_DROP) {
		req->flags =
			cpu_to_le32(CFA_NTUPLE_FILTER_ALLOC_REQ_FLAGS_DROP);
	} else if (bp->fw_cap & BNXT_FW_CAP_CFA_RFS_RING_TBL_IDX_V2) {
		bnxt_cfg_rfs_ring_tbl_idx(bp, req, fltr);
	} else {
		vnic = &bp->vnic_info[fltr->base.rxq + 1];
		req->dst_id = cpu_to_le16(vnic->fw_vnic_id);
	}
	req->enables |= cpu_to_le32(BNXT_NTP_FLTR_FLAGS);

	req->ethertype = htons(ETH_P_IP);
	req->ip_addr_type = CFA_NTUPLE_FILTER_ALLOC_REQ_IP_ADDR_TYPE_IPV4;
	req->ip_protocol = keys->basic.ip_proto;

	if (keys->basic.n_proto == htons(ETH_P_IPV6)) {
		req->ethertype = htons(ETH_P_IPV6);
		req->ip_addr_type =
			CFA_NTUPLE_FILTER_ALLOC_REQ_IP_ADDR_TYPE_IPV6;
		*(struct in6_addr *)&req->src_ipaddr[0] = keys->addrs.v6addrs.src;
		*(struct in6_addr *)&req->src_ipaddr_mask[0] = masks->addrs.v6addrs.src;
		*(struct in6_addr *)&req->dst_ipaddr[0] = keys->addrs.v6addrs.dst;
		*(struct in6_addr *)&req->dst_ipaddr_mask[0] = masks->addrs.v6addrs.dst;
	} else {
		req->src_ipaddr[0] = keys->addrs.v4addrs.src;
		req->src_ipaddr_mask[0] = masks->addrs.v4addrs.src;
		req->dst_ipaddr[0] = keys->addrs.v4addrs.dst;
		req->dst_ipaddr_mask[0] = masks->addrs.v4addrs.dst;
	}
	if (keys->control.flags & FLOW_DIS_ENCAPSULATION) {
		req->enables |= cpu_to_le32(BNXT_NTP_TUNNEL_FLTR_FLAG);
		req->tunnel_type =
			CFA_NTUPLE_FILTER_ALLOC_REQ_TUNNEL_TYPE_ANYTUNNEL;
	}

	req->src_port = keys->ports.src;
	req->src_port_mask = masks->ports.src;
	req->dst_port = keys->ports.dst;
	req->dst_port_mask = masks->ports.dst;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc)
		fltr->base.filter_id = resp->ntuple_filter_id;
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_hwrm_set_vnic_filter(struct bnxt *bp, u16 vnic_id, u16 idx,
				     const u8 *mac_addr)
{
	struct bnxt_l2_filter *fltr;
	struct bnxt_l2_key key;
	int rc;

	ether_addr_copy(key.dst_mac_addr, mac_addr);
	key.vlan = 0;
	fltr = bnxt_alloc_l2_filter(bp, &key, GFP_KERNEL);
	if (IS_ERR(fltr))
		return PTR_ERR(fltr);

	fltr->base.fw_vnic_id = bp->vnic_info[vnic_id].fw_vnic_id;
	rc = bnxt_hwrm_l2_filter_alloc(bp, fltr);
	if (rc)
		bnxt_del_l2_filter(bp, fltr);
	else
		bp->vnic_info[vnic_id].l2_filters[idx] = fltr;
	return rc;
}

static void bnxt_hwrm_clear_vnic_filter(struct bnxt *bp)
{
	u16 i, j, num_of_vnics = 1; /* only vnic 0 supported */

	/* Any associated ntuple filters will also be cleared by firmware. */
	for (i = 0; i < num_of_vnics; i++) {
		struct bnxt_vnic_info *vnic = &bp->vnic_info[i];

		for (j = 0; j < vnic->uc_filter_count; j++) {
			struct bnxt_l2_filter *fltr = vnic->l2_filters[j];

			bnxt_hwrm_l2_filter_free(bp, fltr);
			bnxt_del_l2_filter(bp, fltr);
		}
		vnic->uc_filter_count = 0;
	}
}

#define BNXT_DFLT_TUNL_TPA_BMAP				\
	(VNIC_TPA_CFG_REQ_TNL_TPA_EN_BITMAP_GRE |	\
	 VNIC_TPA_CFG_REQ_TNL_TPA_EN_BITMAP_IPV4 |	\
	 VNIC_TPA_CFG_REQ_TNL_TPA_EN_BITMAP_IPV6)

static void bnxt_hwrm_vnic_update_tunl_tpa(struct bnxt *bp,
					   struct hwrm_vnic_tpa_cfg_input *req)
{
	u32 tunl_tpa_bmap = BNXT_DFLT_TUNL_TPA_BMAP;

	if (!(bp->fw_cap & BNXT_FW_CAP_VNIC_TUNNEL_TPA))
		return;

	if (bp->vxlan_port)
		tunl_tpa_bmap |= VNIC_TPA_CFG_REQ_TNL_TPA_EN_BITMAP_VXLAN;
	if (bp->vxlan_gpe_port)
		tunl_tpa_bmap |= VNIC_TPA_CFG_REQ_TNL_TPA_EN_BITMAP_VXLAN_GPE;
	if (bp->nge_port)
		tunl_tpa_bmap |= VNIC_TPA_CFG_REQ_TNL_TPA_EN_BITMAP_GENEVE;

	req->enables |= cpu_to_le32(VNIC_TPA_CFG_REQ_ENABLES_TNL_TPA_EN);
	req->tnl_tpa_en_bitmap = cpu_to_le32(tunl_tpa_bmap);
}

int bnxt_hwrm_vnic_set_tpa(struct bnxt *bp, struct bnxt_vnic_info *vnic,
			   u32 tpa_flags)
{
	u16 max_aggs = VNIC_TPA_CFG_REQ_MAX_AGGS_MAX;
	struct hwrm_vnic_tpa_cfg_input *req;
	int rc;

	if (vnic->fw_vnic_id == INVALID_HW_RING_ID)
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_VNIC_TPA_CFG);
	if (rc)
		return rc;

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

		req->flags = cpu_to_le32(flags);

		req->enables =
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

		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
			segs = MAX_TPA_SEGS_P5;
			max_aggs = bp->max_tpa;
		} else {
			segs = ilog2(nsegs);
		}
		req->max_agg_segs = cpu_to_le16(segs);
		req->max_aggs = cpu_to_le16(max_aggs);

		req->min_agg_len = cpu_to_le32(512);
		bnxt_hwrm_vnic_update_tunl_tpa(bp, req);
	}
	req->vnic_id = cpu_to_le16(vnic->fw_vnic_id);

	return hwrm_req_send(bp, req);
}

static u16 bnxt_cp_ring_from_grp(struct bnxt *bp, struct bnxt_ring_struct *ring)
{
	struct bnxt_ring_grp_info *grp_info;

	grp_info = &bp->grp_info[ring->grp_idx];
	return grp_info->cp_fw_ring_id;
}

static u16 bnxt_cp_ring_for_rx(struct bnxt *bp, struct bnxt_rx_ring_info *rxr)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		return rxr->rx_cpr->cp_ring_struct.fw_ring_id;
	else
		return bnxt_cp_ring_from_grp(bp, &rxr->rx_ring_struct);
}

static u16 bnxt_cp_ring_for_tx(struct bnxt *bp, struct bnxt_tx_ring_info *txr)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		return txr->tx_cpr->cp_ring_struct.fw_ring_id;
	else
		return bnxt_cp_ring_from_grp(bp, &txr->tx_ring_struct);
}

static int bnxt_alloc_rss_indir_tbl(struct bnxt *bp)
{
	int entries;

	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		entries = BNXT_MAX_RSS_TABLE_ENTRIES_P5;
	else
		entries = HW_HASH_INDEX_SIZE;

	bp->rss_indir_tbl_entries = entries;
	bp->rss_indir_tbl =
		kmalloc_array(entries, sizeof(*bp->rss_indir_tbl), GFP_KERNEL);
	if (!bp->rss_indir_tbl)
		return -ENOMEM;

	return 0;
}

void bnxt_set_dflt_rss_indir_tbl(struct bnxt *bp,
				 struct ethtool_rxfh_context *rss_ctx)
{
	u16 max_rings, max_entries, pad, i;
	u32 *rss_indir_tbl;

	if (!bp->rx_nr_rings)
		return;

	if (BNXT_CHIP_TYPE_NITRO_A0(bp))
		max_rings = bp->rx_nr_rings - 1;
	else
		max_rings = bp->rx_nr_rings;

	max_entries = bnxt_get_rxfh_indir_size(bp->dev);
	if (rss_ctx)
		rss_indir_tbl = ethtool_rxfh_context_indir(rss_ctx);
	else
		rss_indir_tbl = &bp->rss_indir_tbl[0];

	for (i = 0; i < max_entries; i++)
		rss_indir_tbl[i] = ethtool_rxfh_indir_default(i, max_rings);

	pad = bp->rss_indir_tbl_entries - max_entries;
	if (pad)
		memset(&rss_indir_tbl[i], 0, pad * sizeof(*rss_indir_tbl));
}

static u16 bnxt_get_max_rss_ring(struct bnxt *bp)
{
	u32 i, tbl_size, max_ring = 0;

	if (!bp->rss_indir_tbl)
		return 0;

	tbl_size = bnxt_get_rxfh_indir_size(bp->dev);
	for (i = 0; i < tbl_size; i++)
		max_ring = max(max_ring, bp->rss_indir_tbl[i]);
	return max_ring;
}

int bnxt_get_nr_rss_ctxs(struct bnxt *bp, int rx_rings)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
		if (!rx_rings)
			return 0;
		return bnxt_calc_nr_ring_pages(rx_rings - 1,
					       BNXT_RSS_TABLE_ENTRIES_P5);
	}
	if (BNXT_CHIP_TYPE_NITRO_A0(bp))
		return 2;
	return 1;
}

static void bnxt_fill_hw_rss_tbl(struct bnxt *bp, struct bnxt_vnic_info *vnic)
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

static void bnxt_fill_hw_rss_tbl_p5(struct bnxt *bp,
				    struct bnxt_vnic_info *vnic)
{
	__le16 *ring_tbl = vnic->rss_table;
	struct bnxt_rx_ring_info *rxr;
	u16 tbl_size, i;

	tbl_size = bnxt_get_rxfh_indir_size(bp->dev);

	for (i = 0; i < tbl_size; i++) {
		u16 ring_id, j;

		if (vnic->flags & BNXT_VNIC_NTUPLE_FLAG)
			j = ethtool_rxfh_indir_default(i, bp->rx_nr_rings);
		else if (vnic->flags & BNXT_VNIC_RSSCTX_FLAG)
			j = ethtool_rxfh_context_indir(vnic->rss_ctx)[i];
		else
			j = bp->rss_indir_tbl[i];
		rxr = &bp->rx_ring[j];

		ring_id = rxr->rx_ring_struct.fw_ring_id;
		*ring_tbl++ = cpu_to_le16(ring_id);
		ring_id = bnxt_cp_ring_for_rx(bp, rxr);
		*ring_tbl++ = cpu_to_le16(ring_id);
	}
}

static void
__bnxt_hwrm_vnic_set_rss(struct bnxt *bp, struct hwrm_vnic_rss_cfg_input *req,
			 struct bnxt_vnic_info *vnic)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
		bnxt_fill_hw_rss_tbl_p5(bp, vnic);
		if (bp->flags & BNXT_FLAG_CHIP_P7)
			req->flags |= VNIC_RSS_CFG_REQ_FLAGS_IPSEC_HASH_TYPE_CFG_SUPPORT;
	} else {
		bnxt_fill_hw_rss_tbl(bp, vnic);
	}

	if (bp->rss_hash_delta) {
		req->hash_type = cpu_to_le32(bp->rss_hash_delta);
		if (bp->rss_hash_cfg & bp->rss_hash_delta)
			req->flags |= VNIC_RSS_CFG_REQ_FLAGS_HASH_TYPE_INCLUDE;
		else
			req->flags |= VNIC_RSS_CFG_REQ_FLAGS_HASH_TYPE_EXCLUDE;
	} else {
		req->hash_type = cpu_to_le32(bp->rss_hash_cfg);
	}
	req->hash_mode_flags = VNIC_RSS_CFG_REQ_HASH_MODE_FLAGS_DEFAULT;
	req->ring_grp_tbl_addr = cpu_to_le64(vnic->rss_table_dma_addr);
	req->hash_key_tbl_addr = cpu_to_le64(vnic->rss_hash_key_dma_addr);
}

static int bnxt_hwrm_vnic_set_rss(struct bnxt *bp, struct bnxt_vnic_info *vnic,
				  bool set_rss)
{
	struct hwrm_vnic_rss_cfg_input *req;
	int rc;

	if ((bp->flags & BNXT_FLAG_CHIP_P5_PLUS) ||
	    vnic->fw_rss_cos_lb_ctx[0] == INVALID_HW_RING_ID)
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_VNIC_RSS_CFG);
	if (rc)
		return rc;

	if (set_rss)
		__bnxt_hwrm_vnic_set_rss(bp, req, vnic);
	req->rss_ctx_idx = cpu_to_le16(vnic->fw_rss_cos_lb_ctx[0]);
	return hwrm_req_send(bp, req);
}

static int bnxt_hwrm_vnic_set_rss_p5(struct bnxt *bp,
				     struct bnxt_vnic_info *vnic, bool set_rss)
{
	struct hwrm_vnic_rss_cfg_input *req;
	dma_addr_t ring_tbl_map;
	u32 i, nr_ctxs;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_VNIC_RSS_CFG);
	if (rc)
		return rc;

	req->vnic_id = cpu_to_le16(vnic->fw_vnic_id);
	if (!set_rss)
		return hwrm_req_send(bp, req);

	__bnxt_hwrm_vnic_set_rss(bp, req, vnic);
	ring_tbl_map = vnic->rss_table_dma_addr;
	nr_ctxs = bnxt_get_nr_rss_ctxs(bp, bp->rx_nr_rings);

	hwrm_req_hold(bp, req);
	for (i = 0; i < nr_ctxs; ring_tbl_map += BNXT_RSS_TABLE_SIZE_P5, i++) {
		req->ring_grp_tbl_addr = cpu_to_le64(ring_tbl_map);
		req->ring_table_pair_index = i;
		req->rss_ctx_idx = cpu_to_le16(vnic->fw_rss_cos_lb_ctx[i]);
		rc = hwrm_req_send(bp, req);
		if (rc)
			goto exit;
	}

exit:
	hwrm_req_drop(bp, req);
	return rc;
}

static void bnxt_hwrm_update_rss_hash_cfg(struct bnxt *bp)
{
	struct bnxt_vnic_info *vnic = &bp->vnic_info[BNXT_VNIC_DEFAULT];
	struct hwrm_vnic_rss_qcfg_output *resp;
	struct hwrm_vnic_rss_qcfg_input *req;

	if (hwrm_req_init(bp, req, HWRM_VNIC_RSS_QCFG))
		return;

	req->vnic_id = cpu_to_le16(vnic->fw_vnic_id);
	/* all contexts configured to same hash_type, zero always exists */
	req->rss_ctx_idx = cpu_to_le16(vnic->fw_rss_cos_lb_ctx[0]);
	resp = hwrm_req_hold(bp, req);
	if (!hwrm_req_send(bp, req)) {
		bp->rss_hash_cfg = le32_to_cpu(resp->hash_type) ?: bp->rss_hash_cfg;
		bp->rss_hash_delta = 0;
	}
	hwrm_req_drop(bp, req);
}

static int bnxt_hwrm_vnic_set_hds(struct bnxt *bp, struct bnxt_vnic_info *vnic)
{
	u16 hds_thresh = (u16)bp->dev->cfg_pending->hds_thresh;
	struct hwrm_vnic_plcmodes_cfg_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_VNIC_PLCMODES_CFG);
	if (rc)
		return rc;

	req->flags = cpu_to_le32(VNIC_PLCMODES_CFG_REQ_FLAGS_JUMBO_PLACEMENT);
	req->enables = cpu_to_le32(VNIC_PLCMODES_CFG_REQ_ENABLES_JUMBO_THRESH_VALID);
	req->jumbo_thresh = cpu_to_le16(bp->rx_buf_use_size);

	if (!BNXT_RX_PAGE_MODE(bp) && (bp->flags & BNXT_FLAG_AGG_RINGS)) {
		req->flags |= cpu_to_le32(VNIC_PLCMODES_CFG_REQ_FLAGS_HDS_IPV4 |
					  VNIC_PLCMODES_CFG_REQ_FLAGS_HDS_IPV6);
		req->enables |=
			cpu_to_le32(VNIC_PLCMODES_CFG_REQ_ENABLES_HDS_THRESHOLD_VALID);
		req->hds_threshold = cpu_to_le16(hds_thresh);
	}
	req->vnic_id = cpu_to_le32(vnic->fw_vnic_id);
	return hwrm_req_send(bp, req);
}

static void bnxt_hwrm_vnic_ctx_free_one(struct bnxt *bp,
					struct bnxt_vnic_info *vnic,
					u16 ctx_idx)
{
	struct hwrm_vnic_rss_cos_lb_ctx_free_input *req;

	if (hwrm_req_init(bp, req, HWRM_VNIC_RSS_COS_LB_CTX_FREE))
		return;

	req->rss_cos_lb_ctx_id =
		cpu_to_le16(vnic->fw_rss_cos_lb_ctx[ctx_idx]);

	hwrm_req_send(bp, req);
	vnic->fw_rss_cos_lb_ctx[ctx_idx] = INVALID_HW_RING_ID;
}

static void bnxt_hwrm_vnic_ctx_free(struct bnxt *bp)
{
	int i, j;

	for (i = 0; i < bp->nr_vnics; i++) {
		struct bnxt_vnic_info *vnic = &bp->vnic_info[i];

		for (j = 0; j < BNXT_MAX_CTX_PER_VNIC; j++) {
			if (vnic->fw_rss_cos_lb_ctx[j] != INVALID_HW_RING_ID)
				bnxt_hwrm_vnic_ctx_free_one(bp, vnic, j);
		}
	}
	bp->rsscos_nr_ctxs = 0;
}

static int bnxt_hwrm_vnic_ctx_alloc(struct bnxt *bp,
				    struct bnxt_vnic_info *vnic, u16 ctx_idx)
{
	struct hwrm_vnic_rss_cos_lb_ctx_alloc_output *resp;
	struct hwrm_vnic_rss_cos_lb_ctx_alloc_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_VNIC_RSS_COS_LB_CTX_ALLOC);
	if (rc)
		return rc;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc)
		vnic->fw_rss_cos_lb_ctx[ctx_idx] =
			le16_to_cpu(resp->rss_cos_lb_ctx_id);
	hwrm_req_drop(bp, req);

	return rc;
}

static u32 bnxt_get_roce_vnic_mode(struct bnxt *bp)
{
	if (bp->flags & BNXT_FLAG_ROCE_MIRROR_CAP)
		return VNIC_CFG_REQ_FLAGS_ROCE_MIRRORING_CAPABLE_VNIC_MODE;
	return VNIC_CFG_REQ_FLAGS_ROCE_DUAL_VNIC_MODE;
}

int bnxt_hwrm_vnic_cfg(struct bnxt *bp, struct bnxt_vnic_info *vnic)
{
	struct bnxt_vnic_info *vnic0 = &bp->vnic_info[BNXT_VNIC_DEFAULT];
	struct hwrm_vnic_cfg_input *req;
	unsigned int ring = 0, grp_idx;
	u16 def_vlan = 0;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_VNIC_CFG);
	if (rc)
		return rc;

	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[0];

		req->default_rx_ring_id =
			cpu_to_le16(rxr->rx_ring_struct.fw_ring_id);
		req->default_cmpl_ring_id =
			cpu_to_le16(bnxt_cp_ring_for_rx(bp, rxr));
		req->enables =
			cpu_to_le32(VNIC_CFG_REQ_ENABLES_DEFAULT_RX_RING_ID |
				    VNIC_CFG_REQ_ENABLES_DEFAULT_CMPL_RING_ID);
		goto vnic_mru;
	}
	req->enables = cpu_to_le32(VNIC_CFG_REQ_ENABLES_DFLT_RING_GRP);
	/* Only RSS support for now TBD: COS & LB */
	if (vnic->fw_rss_cos_lb_ctx[0] != INVALID_HW_RING_ID) {
		req->rss_rule = cpu_to_le16(vnic->fw_rss_cos_lb_ctx[0]);
		req->enables |= cpu_to_le32(VNIC_CFG_REQ_ENABLES_RSS_RULE |
					   VNIC_CFG_REQ_ENABLES_MRU);
	} else if (vnic->flags & BNXT_VNIC_RFS_NEW_RSS_FLAG) {
		req->rss_rule = cpu_to_le16(vnic0->fw_rss_cos_lb_ctx[0]);
		req->enables |= cpu_to_le32(VNIC_CFG_REQ_ENABLES_RSS_RULE |
					   VNIC_CFG_REQ_ENABLES_MRU);
		req->flags |= cpu_to_le32(VNIC_CFG_REQ_FLAGS_RSS_DFLT_CR_MODE);
	} else {
		req->rss_rule = cpu_to_le16(0xffff);
	}

	if (BNXT_CHIP_TYPE_NITRO_A0(bp) &&
	    (vnic->fw_rss_cos_lb_ctx[0] != INVALID_HW_RING_ID)) {
		req->cos_rule = cpu_to_le16(vnic->fw_rss_cos_lb_ctx[1]);
		req->enables |= cpu_to_le32(VNIC_CFG_REQ_ENABLES_COS_RULE);
	} else {
		req->cos_rule = cpu_to_le16(0xffff);
	}

	if (vnic->flags & BNXT_VNIC_RSS_FLAG)
		ring = 0;
	else if (vnic->flags & BNXT_VNIC_RFS_FLAG)
		ring = vnic->vnic_id - 1;
	else if ((vnic->vnic_id == 1) && BNXT_CHIP_TYPE_NITRO_A0(bp))
		ring = bp->rx_nr_rings - 1;

	grp_idx = bp->rx_ring[ring].bnapi->index;
	req->dflt_ring_grp = cpu_to_le16(bp->grp_info[grp_idx].fw_grp_id);
	req->lb_rule = cpu_to_le16(0xffff);
vnic_mru:
	vnic->mru = bp->dev->mtu + ETH_HLEN + VLAN_HLEN;
	req->mru = cpu_to_le16(vnic->mru);

	req->vnic_id = cpu_to_le16(vnic->fw_vnic_id);
#ifdef CONFIG_BNXT_SRIOV
	if (BNXT_VF(bp))
		def_vlan = bp->vf.vlan;
#endif
	if ((bp->flags & BNXT_FLAG_STRIP_VLAN) || def_vlan)
		req->flags |= cpu_to_le32(VNIC_CFG_REQ_FLAGS_VLAN_STRIP_MODE);
	if (vnic->vnic_id == BNXT_VNIC_DEFAULT && bnxt_ulp_registered(bp->edev))
		req->flags |= cpu_to_le32(bnxt_get_roce_vnic_mode(bp));

	return hwrm_req_send(bp, req);
}

static void bnxt_hwrm_vnic_free_one(struct bnxt *bp,
				    struct bnxt_vnic_info *vnic)
{
	if (vnic->fw_vnic_id != INVALID_HW_RING_ID) {
		struct hwrm_vnic_free_input *req;

		if (hwrm_req_init(bp, req, HWRM_VNIC_FREE))
			return;

		req->vnic_id = cpu_to_le32(vnic->fw_vnic_id);

		hwrm_req_send(bp, req);
		vnic->fw_vnic_id = INVALID_HW_RING_ID;
	}
}

static void bnxt_hwrm_vnic_free(struct bnxt *bp)
{
	u16 i;

	for (i = 0; i < bp->nr_vnics; i++)
		bnxt_hwrm_vnic_free_one(bp, &bp->vnic_info[i]);
}

int bnxt_hwrm_vnic_alloc(struct bnxt *bp, struct bnxt_vnic_info *vnic,
			 unsigned int start_rx_ring_idx,
			 unsigned int nr_rings)
{
	unsigned int i, j, grp_idx, end_idx = start_rx_ring_idx + nr_rings;
	struct hwrm_vnic_alloc_output *resp;
	struct hwrm_vnic_alloc_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_VNIC_ALLOC);
	if (rc)
		return rc;

	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
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
	if (vnic->vnic_id == BNXT_VNIC_DEFAULT)
		req->flags = cpu_to_le32(VNIC_ALLOC_REQ_FLAGS_DEFAULT);

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc)
		vnic->fw_vnic_id = le32_to_cpu(resp->vnic_id);
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_hwrm_vnic_qcaps(struct bnxt *bp)
{
	struct hwrm_vnic_qcaps_output *resp;
	struct hwrm_vnic_qcaps_input *req;
	int rc;

	bp->hw_ring_stats_size = sizeof(struct ctx_hw_stats);
	bp->flags &= ~BNXT_FLAG_ROCE_MIRROR_CAP;
	bp->rss_cap &= ~BNXT_RSS_CAP_NEW_RSS_CAP;
	if (bp->hwrm_spec_code < 0x10600)
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_VNIC_QCAPS);
	if (rc)
		return rc;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc) {
		u32 flags = le32_to_cpu(resp->flags);

		if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS) &&
		    (flags & VNIC_QCAPS_RESP_FLAGS_RSS_DFLT_CR_CAP))
			bp->rss_cap |= BNXT_RSS_CAP_NEW_RSS_CAP;
		if (flags &
		    VNIC_QCAPS_RESP_FLAGS_ROCE_MIRRORING_CAPABLE_VNIC_CAP)
			bp->flags |= BNXT_FLAG_ROCE_MIRROR_CAP;

		/* Older P5 fw before EXT_HW_STATS support did not set
		 * VLAN_STRIP_CAP properly.
		 */
		if ((flags & VNIC_QCAPS_RESP_FLAGS_VLAN_STRIP_CAP) ||
		    (BNXT_CHIP_P5(bp) &&
		     !(bp->fw_cap & BNXT_FW_CAP_EXT_HW_STATS_SUPPORTED)))
			bp->fw_cap |= BNXT_FW_CAP_VLAN_RX_STRIP;
		if (flags & VNIC_QCAPS_RESP_FLAGS_RSS_HASH_TYPE_DELTA_CAP)
			bp->rss_cap |= BNXT_RSS_CAP_RSS_HASH_TYPE_DELTA;
		if (flags & VNIC_QCAPS_RESP_FLAGS_RSS_PROF_TCAM_MODE_ENABLED)
			bp->rss_cap |= BNXT_RSS_CAP_RSS_TCAM;
		bp->max_tpa_v2 = le16_to_cpu(resp->max_aggs_supported);
		if (bp->max_tpa_v2) {
			if (BNXT_CHIP_P5(bp))
				bp->hw_ring_stats_size = BNXT_RING_STATS_SIZE_P5;
			else
				bp->hw_ring_stats_size = BNXT_RING_STATS_SIZE_P7;
		}
		if (flags & VNIC_QCAPS_RESP_FLAGS_HW_TUNNEL_TPA_CAP)
			bp->fw_cap |= BNXT_FW_CAP_VNIC_TUNNEL_TPA;
		if (flags & VNIC_QCAPS_RESP_FLAGS_RSS_IPSEC_AH_SPI_IPV4_CAP)
			bp->rss_cap |= BNXT_RSS_CAP_AH_V4_RSS_CAP;
		if (flags & VNIC_QCAPS_RESP_FLAGS_RSS_IPSEC_AH_SPI_IPV6_CAP)
			bp->rss_cap |= BNXT_RSS_CAP_AH_V6_RSS_CAP;
		if (flags & VNIC_QCAPS_RESP_FLAGS_RSS_IPSEC_ESP_SPI_IPV4_CAP)
			bp->rss_cap |= BNXT_RSS_CAP_ESP_V4_RSS_CAP;
		if (flags & VNIC_QCAPS_RESP_FLAGS_RSS_IPSEC_ESP_SPI_IPV6_CAP)
			bp->rss_cap |= BNXT_RSS_CAP_ESP_V6_RSS_CAP;
		if (flags & VNIC_QCAPS_RESP_FLAGS_RE_FLUSH_CAP)
			bp->fw_cap |= BNXT_FW_CAP_VNIC_RE_FLUSH;
	}
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_hwrm_ring_grp_alloc(struct bnxt *bp)
{
	struct hwrm_ring_grp_alloc_output *resp;
	struct hwrm_ring_grp_alloc_input *req;
	int rc;
	u16 i;

	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_RING_GRP_ALLOC);
	if (rc)
		return rc;

	resp = hwrm_req_hold(bp, req);
	for (i = 0; i < bp->rx_nr_rings; i++) {
		unsigned int grp_idx = bp->rx_ring[i].bnapi->index;

		req->cr = cpu_to_le16(bp->grp_info[grp_idx].cp_fw_ring_id);
		req->rr = cpu_to_le16(bp->grp_info[grp_idx].rx_fw_ring_id);
		req->ar = cpu_to_le16(bp->grp_info[grp_idx].agg_fw_ring_id);
		req->sc = cpu_to_le16(bp->grp_info[grp_idx].fw_stats_ctx);

		rc = hwrm_req_send(bp, req);

		if (rc)
			break;

		bp->grp_info[grp_idx].fw_grp_id =
			le32_to_cpu(resp->ring_group_id);
	}
	hwrm_req_drop(bp, req);
	return rc;
}

static void bnxt_hwrm_ring_grp_free(struct bnxt *bp)
{
	struct hwrm_ring_grp_free_input *req;
	u16 i;

	if (!bp->grp_info || (bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
		return;

	if (hwrm_req_init(bp, req, HWRM_RING_GRP_FREE))
		return;

	hwrm_req_hold(bp, req);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		if (bp->grp_info[i].fw_grp_id == INVALID_HW_RING_ID)
			continue;
		req->ring_group_id =
			cpu_to_le32(bp->grp_info[i].fw_grp_id);

		hwrm_req_send(bp, req);
		bp->grp_info[i].fw_grp_id = INVALID_HW_RING_ID;
	}
	hwrm_req_drop(bp, req);
}

static void bnxt_set_rx_ring_params_p5(struct bnxt *bp, u32 ring_type,
				       struct hwrm_ring_alloc_input *req,
				       struct bnxt_ring_struct *ring)
{
	struct bnxt_ring_grp_info *grp_info = &bp->grp_info[ring->grp_idx];
	u32 enables = RING_ALLOC_REQ_ENABLES_RX_BUF_SIZE_VALID |
		      RING_ALLOC_REQ_ENABLES_NQ_RING_ID_VALID;

	if (ring_type == HWRM_RING_ALLOC_AGG) {
		req->ring_type = RING_ALLOC_REQ_RING_TYPE_RX_AGG;
		req->rx_ring_id = cpu_to_le16(grp_info->rx_fw_ring_id);
		req->rx_buf_size = cpu_to_le16(BNXT_RX_PAGE_SIZE);
		enables |= RING_ALLOC_REQ_ENABLES_RX_RING_ID_VALID;
	} else {
		req->rx_buf_size = cpu_to_le16(bp->rx_buf_use_size);
		if (NET_IP_ALIGN == 2)
			req->flags =
				cpu_to_le16(RING_ALLOC_REQ_FLAGS_RX_SOP_PAD);
	}
	req->stat_ctx_id = cpu_to_le32(grp_info->fw_stats_ctx);
	req->nq_ring_id = cpu_to_le16(grp_info->cp_fw_ring_id);
	req->enables |= cpu_to_le32(enables);
}

static int hwrm_ring_alloc_send_msg(struct bnxt *bp,
				    struct bnxt_ring_struct *ring,
				    u32 ring_type, u32 map_index)
{
	struct hwrm_ring_alloc_output *resp;
	struct hwrm_ring_alloc_input *req;
	struct bnxt_ring_mem_info *rmem = &ring->ring_mem;
	struct bnxt_ring_grp_info *grp_info;
	int rc, err = 0;
	u16 ring_id;

	rc = hwrm_req_init(bp, req, HWRM_RING_ALLOC);
	if (rc)
		goto exit;

	req->enables = 0;
	if (rmem->nr_pages > 1) {
		req->page_tbl_addr = cpu_to_le64(rmem->pg_tbl_map);
		/* Page size is in log2 units */
		req->page_size = BNXT_PAGE_SHIFT;
		req->page_tbl_depth = 1;
	} else {
		req->page_tbl_addr =  cpu_to_le64(rmem->dma_arr[0]);
	}
	req->fbo = 0;
	/* Association of ring index with doorbell index and MSIX number */
	req->logical_id = cpu_to_le16(map_index);

	switch (ring_type) {
	case HWRM_RING_ALLOC_TX: {
		struct bnxt_tx_ring_info *txr;
		u16 flags = 0;

		txr = container_of(ring, struct bnxt_tx_ring_info,
				   tx_ring_struct);
		req->ring_type = RING_ALLOC_REQ_RING_TYPE_TX;
		/* Association of transmit ring with completion ring */
		grp_info = &bp->grp_info[ring->grp_idx];
		req->cmpl_ring_id = cpu_to_le16(bnxt_cp_ring_for_tx(bp, txr));
		req->length = cpu_to_le32(bp->tx_ring_mask + 1);
		req->stat_ctx_id = cpu_to_le32(grp_info->fw_stats_ctx);
		req->queue_id = cpu_to_le16(ring->queue_id);
		if (bp->flags & BNXT_FLAG_TX_COAL_CMPL)
			req->cmpl_coal_cnt =
				RING_ALLOC_REQ_CMPL_COAL_CNT_COAL_64;
		if ((bp->fw_cap & BNXT_FW_CAP_TX_TS_CMP) && bp->ptp_cfg)
			flags |= RING_ALLOC_REQ_FLAGS_TX_PKT_TS_CMPL_ENABLE;
		req->flags = cpu_to_le16(flags);
		break;
	}
	case HWRM_RING_ALLOC_RX:
	case HWRM_RING_ALLOC_AGG:
		req->ring_type = RING_ALLOC_REQ_RING_TYPE_RX;
		req->length = (ring_type == HWRM_RING_ALLOC_RX) ?
			      cpu_to_le32(bp->rx_ring_mask + 1) :
			      cpu_to_le32(bp->rx_agg_ring_mask + 1);
		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
			bnxt_set_rx_ring_params_p5(bp, ring_type, req, ring);
		break;
	case HWRM_RING_ALLOC_CMPL:
		req->ring_type = RING_ALLOC_REQ_RING_TYPE_L2_CMPL;
		req->length = cpu_to_le32(bp->cp_ring_mask + 1);
		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
			/* Association of cp ring with nq */
			grp_info = &bp->grp_info[map_index];
			req->nq_ring_id = cpu_to_le16(grp_info->cp_fw_ring_id);
			req->cq_handle = cpu_to_le64(ring->handle);
			req->enables |= cpu_to_le32(
				RING_ALLOC_REQ_ENABLES_NQ_RING_ID_VALID);
		} else {
			req->int_mode = RING_ALLOC_REQ_INT_MODE_MSIX;
		}
		break;
	case HWRM_RING_ALLOC_NQ:
		req->ring_type = RING_ALLOC_REQ_RING_TYPE_NQ;
		req->length = cpu_to_le32(bp->cp_ring_mask + 1);
		req->int_mode = RING_ALLOC_REQ_INT_MODE_MSIX;
		break;
	default:
		netdev_err(bp->dev, "hwrm alloc invalid ring type %d\n",
			   ring_type);
		return -1;
	}

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	err = le16_to_cpu(resp->error_code);
	ring_id = le16_to_cpu(resp->ring_id);
	hwrm_req_drop(bp, req);

exit:
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
		struct hwrm_func_cfg_input *req;

		rc = bnxt_hwrm_func_cfg_short_req_init(bp, &req);
		if (rc)
			return rc;

		req->fid = cpu_to_le16(0xffff);
		req->enables = cpu_to_le32(FUNC_CFG_REQ_ENABLES_ASYNC_EVENT_CR);
		req->async_event_cr = cpu_to_le16(idx);
		return hwrm_req_send(bp, req);
	} else {
		struct hwrm_func_vf_cfg_input *req;

		rc = hwrm_req_init(bp, req, HWRM_FUNC_VF_CFG);
		if (rc)
			return rc;

		req->enables =
			cpu_to_le32(FUNC_VF_CFG_REQ_ENABLES_ASYNC_EVENT_CR);
		req->async_event_cr = cpu_to_le16(idx);
		return hwrm_req_send(bp, req);
	}
}

static void bnxt_set_db_mask(struct bnxt *bp, struct bnxt_db_info *db,
			     u32 ring_type)
{
	switch (ring_type) {
	case HWRM_RING_ALLOC_TX:
		db->db_ring_mask = bp->tx_ring_mask;
		break;
	case HWRM_RING_ALLOC_RX:
		db->db_ring_mask = bp->rx_ring_mask;
		break;
	case HWRM_RING_ALLOC_AGG:
		db->db_ring_mask = bp->rx_agg_ring_mask;
		break;
	case HWRM_RING_ALLOC_CMPL:
	case HWRM_RING_ALLOC_NQ:
		db->db_ring_mask = bp->cp_ring_mask;
		break;
	}
	if (bp->flags & BNXT_FLAG_CHIP_P7) {
		db->db_epoch_mask = db->db_ring_mask + 1;
		db->db_epoch_shift = DBR_EPOCH_SFT - ilog2(db->db_epoch_mask);
	}
}

static void bnxt_set_db(struct bnxt *bp, struct bnxt_db_info *db, u32 ring_type,
			u32 map_idx, u32 xid)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
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

		if (bp->flags & BNXT_FLAG_CHIP_P7)
			db->db_key64 |= DBR_VALID;

		db->doorbell = bp->bar1 + bp->db_offset;
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
	bnxt_set_db_mask(bp, db, ring_type);
}

static int bnxt_hwrm_rx_ring_alloc(struct bnxt *bp,
				   struct bnxt_rx_ring_info *rxr)
{
	struct bnxt_ring_struct *ring = &rxr->rx_ring_struct;
	struct bnxt_napi *bnapi = rxr->bnapi;
	u32 type = HWRM_RING_ALLOC_RX;
	u32 map_idx = bnapi->index;
	int rc;

	rc = hwrm_ring_alloc_send_msg(bp, ring, type, map_idx);
	if (rc)
		return rc;

	bnxt_set_db(bp, &rxr->rx_db, type, map_idx, ring->fw_ring_id);
	bp->grp_info[map_idx].rx_fw_ring_id = ring->fw_ring_id;

	return 0;
}

static int bnxt_hwrm_rx_agg_ring_alloc(struct bnxt *bp,
				       struct bnxt_rx_ring_info *rxr)
{
	struct bnxt_ring_struct *ring = &rxr->rx_agg_ring_struct;
	u32 type = HWRM_RING_ALLOC_AGG;
	u32 grp_idx = ring->grp_idx;
	u32 map_idx;
	int rc;

	map_idx = grp_idx + bp->rx_nr_rings;
	rc = hwrm_ring_alloc_send_msg(bp, ring, type, map_idx);
	if (rc)
		return rc;

	bnxt_set_db(bp, &rxr->rx_agg_db, type, map_idx,
		    ring->fw_ring_id);
	bnxt_db_write(bp, &rxr->rx_agg_db, rxr->rx_agg_prod);
	bnxt_db_write(bp, &rxr->rx_db, rxr->rx_prod);
	bp->grp_info[grp_idx].agg_fw_ring_id = ring->fw_ring_id;

	return 0;
}

static int bnxt_hwrm_cp_ring_alloc_p5(struct bnxt *bp,
				      struct bnxt_cp_ring_info *cpr)
{
	const u32 type = HWRM_RING_ALLOC_CMPL;
	struct bnxt_napi *bnapi = cpr->bnapi;
	struct bnxt_ring_struct *ring;
	u32 map_idx = bnapi->index;
	int rc;

	ring = &cpr->cp_ring_struct;
	ring->handle = BNXT_SET_NQ_HDL(cpr);
	rc = hwrm_ring_alloc_send_msg(bp, ring, type, map_idx);
	if (rc)
		return rc;
	bnxt_set_db(bp, &cpr->cp_db, type, map_idx, ring->fw_ring_id);
	bnxt_db_cq(bp, &cpr->cp_db, cpr->cp_raw_cons);
	return 0;
}

static int bnxt_hwrm_tx_ring_alloc(struct bnxt *bp,
				   struct bnxt_tx_ring_info *txr, u32 tx_idx)
{
	struct bnxt_ring_struct *ring = &txr->tx_ring_struct;
	const u32 type = HWRM_RING_ALLOC_TX;
	int rc;

	rc = hwrm_ring_alloc_send_msg(bp, ring, type, tx_idx);
	if (rc)
		return rc;
	bnxt_set_db(bp, &txr->tx_db, type, tx_idx, ring->fw_ring_id);
	return 0;
}

static int bnxt_hwrm_ring_alloc(struct bnxt *bp)
{
	bool agg_rings = !!(bp->flags & BNXT_FLAG_AGG_RINGS);
	int i, rc = 0;
	u32 type;

	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
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

	for (i = 0; i < bp->tx_nr_rings; i++) {
		struct bnxt_tx_ring_info *txr = &bp->tx_ring[i];

		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
			rc = bnxt_hwrm_cp_ring_alloc_p5(bp, txr->tx_cpr);
			if (rc)
				goto err_out;
		}
		rc = bnxt_hwrm_tx_ring_alloc(bp, txr, i);
		if (rc)
			goto err_out;
	}

	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];

		rc = bnxt_hwrm_rx_ring_alloc(bp, rxr);
		if (rc)
			goto err_out;
		/* If we have agg rings, post agg buffers first. */
		if (!agg_rings)
			bnxt_db_write(bp, &rxr->rx_db, rxr->rx_prod);
		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
			rc = bnxt_hwrm_cp_ring_alloc_p5(bp, rxr->rx_cpr);
			if (rc)
				goto err_out;
		}
	}

	if (agg_rings) {
		for (i = 0; i < bp->rx_nr_rings; i++) {
			rc = bnxt_hwrm_rx_agg_ring_alloc(bp, &bp->rx_ring[i]);
			if (rc)
				goto err_out;
		}
	}
err_out:
	return rc;
}

static void bnxt_cancel_dim(struct bnxt *bp)
{
	int i;

	/* DIM work is initialized in bnxt_enable_napi().  Proceed only
	 * if NAPI is enabled.
	 */
	if (!bp->bnapi || test_bit(BNXT_STATE_NAPI_DISABLED, &bp->state))
		return;

	/* Make sure NAPI sees that the VNIC is disabled */
	synchronize_net();
	for (i = 0; i < bp->rx_nr_rings; i++) {
		struct bnxt_rx_ring_info *rxr = &bp->rx_ring[i];
		struct bnxt_napi *bnapi = rxr->bnapi;

		cancel_work_sync(&bnapi->cp_ring.dim.work);
	}
}

static int hwrm_ring_free_send_msg(struct bnxt *bp,
				   struct bnxt_ring_struct *ring,
				   u32 ring_type, int cmpl_ring_id)
{
	struct hwrm_ring_free_output *resp;
	struct hwrm_ring_free_input *req;
	u16 error_code = 0;
	int rc;

	if (BNXT_NO_FW_ACCESS(bp))
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_RING_FREE);
	if (rc)
		goto exit;

	req->cmpl_ring = cpu_to_le16(cmpl_ring_id);
	req->ring_type = ring_type;
	req->ring_id = cpu_to_le16(ring->fw_ring_id);

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	error_code = le16_to_cpu(resp->error_code);
	hwrm_req_drop(bp, req);
exit:
	if (rc || error_code) {
		netdev_err(bp->dev, "hwrm_ring_free type %d failed. rc:%x err:%x\n",
			   ring_type, rc, error_code);
		return -EIO;
	}
	return 0;
}

static void bnxt_hwrm_tx_ring_free(struct bnxt *bp,
				   struct bnxt_tx_ring_info *txr,
				   bool close_path)
{
	struct bnxt_ring_struct *ring = &txr->tx_ring_struct;
	u32 cmpl_ring_id;

	if (ring->fw_ring_id == INVALID_HW_RING_ID)
		return;

	cmpl_ring_id = close_path ? bnxt_cp_ring_for_tx(bp, txr) :
		       INVALID_HW_RING_ID;
	hwrm_ring_free_send_msg(bp, ring, RING_FREE_REQ_RING_TYPE_TX,
				cmpl_ring_id);
	ring->fw_ring_id = INVALID_HW_RING_ID;
}

static void bnxt_hwrm_rx_ring_free(struct bnxt *bp,
				   struct bnxt_rx_ring_info *rxr,
				   bool close_path)
{
	struct bnxt_ring_struct *ring = &rxr->rx_ring_struct;
	u32 grp_idx = rxr->bnapi->index;
	u32 cmpl_ring_id;

	if (ring->fw_ring_id == INVALID_HW_RING_ID)
		return;

	cmpl_ring_id = bnxt_cp_ring_for_rx(bp, rxr);
	hwrm_ring_free_send_msg(bp, ring,
				RING_FREE_REQ_RING_TYPE_RX,
				close_path ? cmpl_ring_id :
				INVALID_HW_RING_ID);
	ring->fw_ring_id = INVALID_HW_RING_ID;
	bp->grp_info[grp_idx].rx_fw_ring_id = INVALID_HW_RING_ID;
}

static void bnxt_hwrm_rx_agg_ring_free(struct bnxt *bp,
				       struct bnxt_rx_ring_info *rxr,
				       bool close_path)
{
	struct bnxt_ring_struct *ring = &rxr->rx_agg_ring_struct;
	u32 grp_idx = rxr->bnapi->index;
	u32 type, cmpl_ring_id;

	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		type = RING_FREE_REQ_RING_TYPE_RX_AGG;
	else
		type = RING_FREE_REQ_RING_TYPE_RX;

	if (ring->fw_ring_id == INVALID_HW_RING_ID)
		return;

	cmpl_ring_id = bnxt_cp_ring_for_rx(bp, rxr);
	hwrm_ring_free_send_msg(bp, ring, type,
				close_path ? cmpl_ring_id :
				INVALID_HW_RING_ID);
	ring->fw_ring_id = INVALID_HW_RING_ID;
	bp->grp_info[grp_idx].agg_fw_ring_id = INVALID_HW_RING_ID;
}

static void bnxt_hwrm_cp_ring_free(struct bnxt *bp,
				   struct bnxt_cp_ring_info *cpr)
{
	struct bnxt_ring_struct *ring;

	ring = &cpr->cp_ring_struct;
	if (ring->fw_ring_id == INVALID_HW_RING_ID)
		return;

	hwrm_ring_free_send_msg(bp, ring, RING_FREE_REQ_RING_TYPE_L2_CMPL,
				INVALID_HW_RING_ID);
	ring->fw_ring_id = INVALID_HW_RING_ID;
}

static void bnxt_clear_one_cp_ring(struct bnxt *bp, struct bnxt_cp_ring_info *cpr)
{
	struct bnxt_ring_struct *ring = &cpr->cp_ring_struct;
	int i, size = ring->ring_mem.page_size;

	cpr->cp_raw_cons = 0;
	cpr->toggle = 0;

	for (i = 0; i < bp->cp_nr_pages; i++)
		if (cpr->cp_desc_ring[i])
			memset(cpr->cp_desc_ring[i], 0, size);
}

static void bnxt_hwrm_ring_free(struct bnxt *bp, bool close_path)
{
	u32 type;
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->tx_nr_rings; i++)
		bnxt_hwrm_tx_ring_free(bp, &bp->tx_ring[i], close_path);

	bnxt_cancel_dim(bp);
	for (i = 0; i < bp->rx_nr_rings; i++) {
		bnxt_hwrm_rx_ring_free(bp, &bp->rx_ring[i], close_path);
		bnxt_hwrm_rx_agg_ring_free(bp, &bp->rx_ring[i], close_path);
	}

	/* The completion rings are about to be freed.  After that the
	 * IRQ doorbell will not work anymore.  So we need to disable
	 * IRQ here.
	 */
	bnxt_disable_int_sync(bp);

	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		type = RING_FREE_REQ_RING_TYPE_NQ;
	else
		type = RING_FREE_REQ_RING_TYPE_L2_CMPL;
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
		struct bnxt_ring_struct *ring;
		int j;

		for (j = 0; j < cpr->cp_ring_count && cpr->cp_ring_arr; j++)
			bnxt_hwrm_cp_ring_free(bp, &cpr->cp_ring_arr[j]);

		ring = &cpr->cp_ring_struct;
		if (ring->fw_ring_id != INVALID_HW_RING_ID) {
			hwrm_ring_free_send_msg(bp, ring, type,
						INVALID_HW_RING_ID);
			ring->fw_ring_id = INVALID_HW_RING_ID;
			bp->grp_info[i].cp_fw_ring_id = INVALID_HW_RING_ID;
		}
	}
}

static int __bnxt_trim_rings(struct bnxt *bp, int *rx, int *tx, int max,
			     bool shared);
static int bnxt_trim_rings(struct bnxt *bp, int *rx, int *tx, int max,
			   bool shared);

static int bnxt_hwrm_get_rings(struct bnxt *bp)
{
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;
	struct hwrm_func_qcfg_output *resp;
	struct hwrm_func_qcfg_input *req;
	int rc;

	if (bp->hwrm_spec_code < 0x10601)
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_FUNC_QCFG);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(0xffff);
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (rc) {
		hwrm_req_drop(bp, req);
		return rc;
	}

	hw_resc->resv_tx_rings = le16_to_cpu(resp->alloc_tx_rings);
	if (BNXT_NEW_RM(bp)) {
		u16 cp, stats;

		hw_resc->resv_rx_rings = le16_to_cpu(resp->alloc_rx_rings);
		hw_resc->resv_hw_ring_grps =
			le32_to_cpu(resp->alloc_hw_ring_grps);
		hw_resc->resv_vnics = le16_to_cpu(resp->alloc_vnics);
		hw_resc->resv_rsscos_ctxs = le16_to_cpu(resp->alloc_rsscos_ctx);
		cp = le16_to_cpu(resp->alloc_cmpl_rings);
		stats = le16_to_cpu(resp->alloc_stat_ctx);
		hw_resc->resv_irqs = cp;
		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
			int rx = hw_resc->resv_rx_rings;
			int tx = hw_resc->resv_tx_rings;

			if (bp->flags & BNXT_FLAG_AGG_RINGS)
				rx >>= 1;
			if (cp < (rx + tx)) {
				rc = __bnxt_trim_rings(bp, &rx, &tx, cp, false);
				if (rc)
					goto get_rings_exit;
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
get_rings_exit:
	hwrm_req_drop(bp, req);
	return rc;
}

int __bnxt_hwrm_get_tx_rings(struct bnxt *bp, u16 fid, int *tx_rings)
{
	struct hwrm_func_qcfg_output *resp;
	struct hwrm_func_qcfg_input *req;
	int rc;

	if (bp->hwrm_spec_code < 0x10601)
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_FUNC_QCFG);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(fid);
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc)
		*tx_rings = le16_to_cpu(resp->alloc_tx_rings);

	hwrm_req_drop(bp, req);
	return rc;
}

static bool bnxt_rfs_supported(struct bnxt *bp);

static struct hwrm_func_cfg_input *
__bnxt_hwrm_reserve_pf_rings(struct bnxt *bp, struct bnxt_hw_rings *hwr)
{
	struct hwrm_func_cfg_input *req;
	u32 enables = 0;

	if (bnxt_hwrm_func_cfg_short_req_init(bp, &req))
		return NULL;

	req->fid = cpu_to_le16(0xffff);
	enables |= hwr->tx ? FUNC_CFG_REQ_ENABLES_NUM_TX_RINGS : 0;
	req->num_tx_rings = cpu_to_le16(hwr->tx);
	if (BNXT_NEW_RM(bp)) {
		enables |= hwr->rx ? FUNC_CFG_REQ_ENABLES_NUM_RX_RINGS : 0;
		enables |= hwr->stat ? FUNC_CFG_REQ_ENABLES_NUM_STAT_CTXS : 0;
		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
			enables |= hwr->cp ? FUNC_CFG_REQ_ENABLES_NUM_MSIX : 0;
			enables |= hwr->cp_p5 ?
				   FUNC_CFG_REQ_ENABLES_NUM_CMPL_RINGS : 0;
		} else {
			enables |= hwr->cp ?
				   FUNC_CFG_REQ_ENABLES_NUM_CMPL_RINGS : 0;
			enables |= hwr->grp ?
				   FUNC_CFG_REQ_ENABLES_NUM_HW_RING_GRPS : 0;
		}
		enables |= hwr->vnic ? FUNC_CFG_REQ_ENABLES_NUM_VNICS : 0;
		enables |= hwr->rss_ctx ? FUNC_CFG_REQ_ENABLES_NUM_RSSCOS_CTXS :
					  0;
		req->num_rx_rings = cpu_to_le16(hwr->rx);
		req->num_rsscos_ctxs = cpu_to_le16(hwr->rss_ctx);
		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
			req->num_cmpl_rings = cpu_to_le16(hwr->cp_p5);
			req->num_msix = cpu_to_le16(hwr->cp);
		} else {
			req->num_cmpl_rings = cpu_to_le16(hwr->cp);
			req->num_hw_ring_grps = cpu_to_le16(hwr->grp);
		}
		req->num_stat_ctxs = cpu_to_le16(hwr->stat);
		req->num_vnics = cpu_to_le16(hwr->vnic);
	}
	req->enables = cpu_to_le32(enables);
	return req;
}

static struct hwrm_func_vf_cfg_input *
__bnxt_hwrm_reserve_vf_rings(struct bnxt *bp, struct bnxt_hw_rings *hwr)
{
	struct hwrm_func_vf_cfg_input *req;
	u32 enables = 0;

	if (hwrm_req_init(bp, req, HWRM_FUNC_VF_CFG))
		return NULL;

	enables |= hwr->tx ? FUNC_VF_CFG_REQ_ENABLES_NUM_TX_RINGS : 0;
	enables |= hwr->rx ? FUNC_VF_CFG_REQ_ENABLES_NUM_RX_RINGS |
			     FUNC_VF_CFG_REQ_ENABLES_NUM_RSSCOS_CTXS : 0;
	enables |= hwr->stat ? FUNC_VF_CFG_REQ_ENABLES_NUM_STAT_CTXS : 0;
	enables |= hwr->rss_ctx ? FUNC_VF_CFG_REQ_ENABLES_NUM_RSSCOS_CTXS : 0;
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
		enables |= hwr->cp_p5 ?
			   FUNC_VF_CFG_REQ_ENABLES_NUM_CMPL_RINGS : 0;
	} else {
		enables |= hwr->cp ? FUNC_VF_CFG_REQ_ENABLES_NUM_CMPL_RINGS : 0;
		enables |= hwr->grp ?
			   FUNC_VF_CFG_REQ_ENABLES_NUM_HW_RING_GRPS : 0;
	}
	enables |= hwr->vnic ? FUNC_VF_CFG_REQ_ENABLES_NUM_VNICS : 0;
	enables |= FUNC_VF_CFG_REQ_ENABLES_NUM_L2_CTXS;

	req->num_l2_ctxs = cpu_to_le16(BNXT_VF_MAX_L2_CTX);
	req->num_tx_rings = cpu_to_le16(hwr->tx);
	req->num_rx_rings = cpu_to_le16(hwr->rx);
	req->num_rsscos_ctxs = cpu_to_le16(hwr->rss_ctx);
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
		req->num_cmpl_rings = cpu_to_le16(hwr->cp_p5);
	} else {
		req->num_cmpl_rings = cpu_to_le16(hwr->cp);
		req->num_hw_ring_grps = cpu_to_le16(hwr->grp);
	}
	req->num_stat_ctxs = cpu_to_le16(hwr->stat);
	req->num_vnics = cpu_to_le16(hwr->vnic);

	req->enables = cpu_to_le32(enables);
	return req;
}

static int
bnxt_hwrm_reserve_pf_rings(struct bnxt *bp, struct bnxt_hw_rings *hwr)
{
	struct hwrm_func_cfg_input *req;
	int rc;

	req = __bnxt_hwrm_reserve_pf_rings(bp, hwr);
	if (!req)
		return -ENOMEM;

	if (!req->enables) {
		hwrm_req_drop(bp, req);
		return 0;
	}

	rc = hwrm_req_send(bp, req);
	if (rc)
		return rc;

	if (bp->hwrm_spec_code < 0x10601)
		bp->hw_resc.resv_tx_rings = hwr->tx;

	return bnxt_hwrm_get_rings(bp);
}

static int
bnxt_hwrm_reserve_vf_rings(struct bnxt *bp, struct bnxt_hw_rings *hwr)
{
	struct hwrm_func_vf_cfg_input *req;
	int rc;

	if (!BNXT_NEW_RM(bp)) {
		bp->hw_resc.resv_tx_rings = hwr->tx;
		return 0;
	}

	req = __bnxt_hwrm_reserve_vf_rings(bp, hwr);
	if (!req)
		return -ENOMEM;

	rc = hwrm_req_send(bp, req);
	if (rc)
		return rc;

	return bnxt_hwrm_get_rings(bp);
}

static int bnxt_hwrm_reserve_rings(struct bnxt *bp, struct bnxt_hw_rings *hwr)
{
	if (BNXT_PF(bp))
		return bnxt_hwrm_reserve_pf_rings(bp, hwr);
	else
		return bnxt_hwrm_reserve_vf_rings(bp, hwr);
}

int bnxt_nq_rings_in_use(struct bnxt *bp)
{
	return bp->cp_nr_rings + bnxt_get_ulp_msix_num(bp);
}

static int bnxt_cp_rings_in_use(struct bnxt *bp)
{
	int cp;

	if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
		return bnxt_nq_rings_in_use(bp);

	cp = bp->tx_nr_rings + bp->rx_nr_rings;
	return cp;
}

static int bnxt_get_func_stat_ctxs(struct bnxt *bp)
{
	return bp->cp_nr_rings + bnxt_get_ulp_stat_ctxs(bp);
}

static int bnxt_get_total_rss_ctxs(struct bnxt *bp, struct bnxt_hw_rings *hwr)
{
	if (!hwr->grp)
		return 0;
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
		int rss_ctx = bnxt_get_nr_rss_ctxs(bp, hwr->grp);

		if (BNXT_SUPPORTS_NTUPLE_VNIC(bp))
			rss_ctx *= hwr->vnic;
		return rss_ctx;
	}
	if (BNXT_VF(bp))
		return BNXT_VF_MAX_RSS_CTX;
	if (!(bp->rss_cap & BNXT_RSS_CAP_NEW_RSS_CAP) && bnxt_rfs_supported(bp))
		return hwr->grp + 1;
	return 1;
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
			bnxt_set_dflt_rss_indir_tbl(bp, NULL);
	}
}

static int bnxt_get_total_vnics(struct bnxt *bp, int rx_rings)
{
	if (bp->flags & BNXT_FLAG_RFS) {
		if (BNXT_SUPPORTS_NTUPLE_VNIC(bp))
			return 2 + bp->num_rss_ctx;
		if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
			return rx_rings + 1;
	}
	return 1;
}

static bool bnxt_need_reserve_rings(struct bnxt *bp)
{
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;
	int cp = bnxt_cp_rings_in_use(bp);
	int nq = bnxt_nq_rings_in_use(bp);
	int rx = bp->rx_nr_rings, stat;
	int vnic, grp = rx;

	/* Old firmware does not need RX ring reservations but we still
	 * need to setup a default RSS map when needed.  With new firmware
	 * we go through RX ring reservations first and then set up the
	 * RSS map for the successfully reserved RX rings when needed.
	 */
	if (!BNXT_NEW_RM(bp))
		bnxt_check_rss_tbl_no_rmgr(bp);

	if (hw_resc->resv_tx_rings != bp->tx_nr_rings &&
	    bp->hwrm_spec_code >= 0x10601)
		return true;

	if (!BNXT_NEW_RM(bp))
		return false;

	vnic = bnxt_get_total_vnics(bp, rx);

	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		rx <<= 1;
	stat = bnxt_get_func_stat_ctxs(bp);
	if (hw_resc->resv_rx_rings != rx || hw_resc->resv_cp_rings != cp ||
	    hw_resc->resv_vnics != vnic || hw_resc->resv_stat_ctxs != stat ||
	    (hw_resc->resv_hw_ring_grps != grp &&
	     !(bp->flags & BNXT_FLAG_CHIP_P5_PLUS)))
		return true;
	if ((bp->flags & BNXT_FLAG_CHIP_P5_PLUS) && BNXT_PF(bp) &&
	    hw_resc->resv_irqs != nq)
		return true;
	return false;
}

static void bnxt_copy_reserved_rings(struct bnxt *bp, struct bnxt_hw_rings *hwr)
{
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;

	hwr->tx = hw_resc->resv_tx_rings;
	if (BNXT_NEW_RM(bp)) {
		hwr->rx = hw_resc->resv_rx_rings;
		hwr->cp = hw_resc->resv_irqs;
		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
			hwr->cp_p5 = hw_resc->resv_cp_rings;
		hwr->grp = hw_resc->resv_hw_ring_grps;
		hwr->vnic = hw_resc->resv_vnics;
		hwr->stat = hw_resc->resv_stat_ctxs;
		hwr->rss_ctx = hw_resc->resv_rsscos_ctxs;
	}
}

static bool bnxt_rings_ok(struct bnxt *bp, struct bnxt_hw_rings *hwr)
{
	return hwr->tx && hwr->rx && hwr->cp && hwr->grp && hwr->vnic &&
	       hwr->stat && (hwr->cp_p5 || !(bp->flags & BNXT_FLAG_CHIP_P5_PLUS));
}

static int bnxt_get_avail_msix(struct bnxt *bp, int num);

static int __bnxt_reserve_rings(struct bnxt *bp)
{
	struct bnxt_hw_rings hwr = {0};
	int rx_rings, old_rx_rings, rc;
	int cp = bp->cp_nr_rings;
	int ulp_msix = 0;
	bool sh = false;
	int tx_cp;

	if (!bnxt_need_reserve_rings(bp))
		return 0;

	if (BNXT_NEW_RM(bp) && !bnxt_ulp_registered(bp->edev)) {
		ulp_msix = bnxt_get_avail_msix(bp, bp->ulp_num_msix_want);
		if (!ulp_msix)
			bnxt_set_ulp_stat_ctxs(bp, 0);

		if (ulp_msix > bp->ulp_num_msix_want)
			ulp_msix = bp->ulp_num_msix_want;
		hwr.cp = cp + ulp_msix;
	} else {
		hwr.cp = bnxt_nq_rings_in_use(bp);
	}

	hwr.tx = bp->tx_nr_rings;
	hwr.rx = bp->rx_nr_rings;
	if (bp->flags & BNXT_FLAG_SHARED_RINGS)
		sh = true;
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		hwr.cp_p5 = hwr.rx + hwr.tx;

	hwr.vnic = bnxt_get_total_vnics(bp, hwr.rx);

	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		hwr.rx <<= 1;
	hwr.grp = bp->rx_nr_rings;
	hwr.rss_ctx = bnxt_get_total_rss_ctxs(bp, &hwr);
	hwr.stat = bnxt_get_func_stat_ctxs(bp);
	old_rx_rings = bp->hw_resc.resv_rx_rings;

	rc = bnxt_hwrm_reserve_rings(bp, &hwr);
	if (rc)
		return rc;

	bnxt_copy_reserved_rings(bp, &hwr);

	rx_rings = hwr.rx;
	if (bp->flags & BNXT_FLAG_AGG_RINGS) {
		if (hwr.rx >= 2) {
			rx_rings = hwr.rx >> 1;
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
	rx_rings = min_t(int, rx_rings, hwr.grp);
	hwr.cp = min_t(int, hwr.cp, bp->cp_nr_rings);
	if (hwr.stat > bnxt_get_ulp_stat_ctxs(bp))
		hwr.stat -= bnxt_get_ulp_stat_ctxs(bp);
	hwr.cp = min_t(int, hwr.cp, hwr.stat);
	rc = bnxt_trim_rings(bp, &rx_rings, &hwr.tx, hwr.cp, sh);
	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		hwr.rx = rx_rings << 1;
	tx_cp = bnxt_num_tx_to_cp(bp, hwr.tx);
	hwr.cp = sh ? max_t(int, tx_cp, rx_rings) : tx_cp + rx_rings;
	bp->tx_nr_rings = hwr.tx;

	/* If we cannot reserve all the RX rings, reset the RSS map only
	 * if absolutely necessary
	 */
	if (rx_rings != bp->rx_nr_rings) {
		netdev_warn(bp->dev, "Able to reserve only %d out of %d requested RX rings\n",
			    rx_rings, bp->rx_nr_rings);
		if (netif_is_rxfh_configured(bp->dev) &&
		    (bnxt_get_nr_rss_ctxs(bp, bp->rx_nr_rings) !=
		     bnxt_get_nr_rss_ctxs(bp, rx_rings) ||
		     bnxt_get_max_rss_ring(bp) >= rx_rings)) {
			netdev_warn(bp->dev, "RSS table entries reverting to default\n");
			bp->dev->priv_flags &= ~IFF_RXFH_CONFIGURED;
		}
	}
	bp->rx_nr_rings = rx_rings;
	bp->cp_nr_rings = hwr.cp;

	if (!bnxt_rings_ok(bp, &hwr))
		return -ENOMEM;

	if (old_rx_rings != bp->hw_resc.resv_rx_rings &&
	    !netif_is_rxfh_configured(bp->dev))
		bnxt_set_dflt_rss_indir_tbl(bp, NULL);

	if (!bnxt_ulp_registered(bp->edev) && BNXT_NEW_RM(bp)) {
		int resv_msix, resv_ctx, ulp_ctxs;
		struct bnxt_hw_resc *hw_resc;

		hw_resc = &bp->hw_resc;
		resv_msix = hw_resc->resv_irqs - bp->cp_nr_rings;
		ulp_msix = min_t(int, resv_msix, ulp_msix);
		bnxt_set_ulp_msix_num(bp, ulp_msix);
		resv_ctx = hw_resc->resv_stat_ctxs  - bp->cp_nr_rings;
		ulp_ctxs = min(resv_ctx, bnxt_get_ulp_stat_ctxs(bp));
		bnxt_set_ulp_stat_ctxs(bp, ulp_ctxs);
	}

	return rc;
}

static int bnxt_hwrm_check_vf_rings(struct bnxt *bp, struct bnxt_hw_rings *hwr)
{
	struct hwrm_func_vf_cfg_input *req;
	u32 flags;

	if (!BNXT_NEW_RM(bp))
		return 0;

	req = __bnxt_hwrm_reserve_vf_rings(bp, hwr);
	flags = FUNC_VF_CFG_REQ_FLAGS_TX_ASSETS_TEST |
		FUNC_VF_CFG_REQ_FLAGS_RX_ASSETS_TEST |
		FUNC_VF_CFG_REQ_FLAGS_CMPL_ASSETS_TEST |
		FUNC_VF_CFG_REQ_FLAGS_STAT_CTX_ASSETS_TEST |
		FUNC_VF_CFG_REQ_FLAGS_VNIC_ASSETS_TEST |
		FUNC_VF_CFG_REQ_FLAGS_RSSCOS_CTX_ASSETS_TEST;
	if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
		flags |= FUNC_VF_CFG_REQ_FLAGS_RING_GRP_ASSETS_TEST;

	req->flags = cpu_to_le32(flags);
	return hwrm_req_send_silent(bp, req);
}

static int bnxt_hwrm_check_pf_rings(struct bnxt *bp, struct bnxt_hw_rings *hwr)
{
	struct hwrm_func_cfg_input *req;
	u32 flags;

	req = __bnxt_hwrm_reserve_pf_rings(bp, hwr);
	flags = FUNC_CFG_REQ_FLAGS_TX_ASSETS_TEST;
	if (BNXT_NEW_RM(bp)) {
		flags |= FUNC_CFG_REQ_FLAGS_RX_ASSETS_TEST |
			 FUNC_CFG_REQ_FLAGS_CMPL_ASSETS_TEST |
			 FUNC_CFG_REQ_FLAGS_STAT_CTX_ASSETS_TEST |
			 FUNC_CFG_REQ_FLAGS_VNIC_ASSETS_TEST;
		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
			flags |= FUNC_CFG_REQ_FLAGS_RSSCOS_CTX_ASSETS_TEST |
				 FUNC_CFG_REQ_FLAGS_NQ_ASSETS_TEST;
		else
			flags |= FUNC_CFG_REQ_FLAGS_RING_GRP_ASSETS_TEST;
	}

	req->flags = cpu_to_le32(flags);
	return hwrm_req_send_silent(bp, req);
}

static int bnxt_hwrm_check_rings(struct bnxt *bp, struct bnxt_hw_rings *hwr)
{
	if (bp->hwrm_spec_code < 0x10801)
		return 0;

	if (BNXT_PF(bp))
		return bnxt_hwrm_check_pf_rings(bp, hwr);

	return bnxt_hwrm_check_vf_rings(bp, hwr);
}

static void bnxt_hwrm_coal_params_qcaps(struct bnxt *bp)
{
	struct bnxt_coal_cap *coal_cap = &bp->coal_cap;
	struct hwrm_ring_aggint_qcaps_output *resp;
	struct hwrm_ring_aggint_qcaps_input *req;
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

	if (hwrm_req_init(bp, req, HWRM_RING_AGGINT_QCAPS))
		return;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send_silent(bp, req);
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
	hwrm_req_drop(bp, req);
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
	u16 val, tmr, max, flags = hw_coal->flags;
	u32 cmpl_params = coal_cap->cmpl_params;

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

	if ((cmpl_params & RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_RING_IDLE) &&
	    hw_coal->idle_thresh && hw_coal->coal_ticks < hw_coal->idle_thresh)
		flags |= RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_RING_IDLE;
	req->flags = cpu_to_le16(flags);
	req->enables |= cpu_to_le16(BNXT_COAL_CMPL_ENABLES);
}

static int __bnxt_hwrm_set_coal_nq(struct bnxt *bp, struct bnxt_napi *bnapi,
				   struct bnxt_coal *hw_coal)
{
	struct hwrm_ring_cmpl_ring_cfg_aggint_params_input *req;
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	struct bnxt_coal_cap *coal_cap = &bp->coal_cap;
	u32 nq_params = coal_cap->nq_params;
	u16 tmr;
	int rc;

	if (!(nq_params & RING_AGGINT_QCAPS_RESP_NQ_PARAMS_INT_LAT_TMR_MIN))
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_RING_CMPL_RING_CFG_AGGINT_PARAMS);
	if (rc)
		return rc;

	req->ring_id = cpu_to_le16(cpr->cp_ring_struct.fw_ring_id);
	req->flags =
		cpu_to_le16(RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_IS_NQ);

	tmr = bnxt_usec_to_coal_tmr(bp, hw_coal->coal_ticks) / 2;
	tmr = clamp_t(u16, tmr, 1, coal_cap->int_lat_tmr_min_max);
	req->int_lat_tmr_min = cpu_to_le16(tmr);
	req->enables |= cpu_to_le16(BNXT_COAL_CMPL_MIN_TMR_ENABLE);
	return hwrm_req_send(bp, req);
}

int bnxt_hwrm_set_ring_coal(struct bnxt *bp, struct bnxt_napi *bnapi)
{
	struct hwrm_ring_cmpl_ring_cfg_aggint_params_input *req_rx;
	struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;
	struct bnxt_coal coal;
	int rc;

	/* Tick values in micro seconds.
	 * 1 coal_buf x bufs_per_record = 1 completion record.
	 */
	memcpy(&coal, &bp->rx_coal, sizeof(struct bnxt_coal));

	coal.coal_ticks = cpr->rx_ring_coal.coal_ticks;
	coal.coal_bufs = cpr->rx_ring_coal.coal_bufs;

	if (!bnapi->rx_ring)
		return -ENODEV;

	rc = hwrm_req_init(bp, req_rx, HWRM_RING_CMPL_RING_CFG_AGGINT_PARAMS);
	if (rc)
		return rc;

	bnxt_hwrm_set_coal_params(bp, &coal, req_rx);

	req_rx->ring_id = cpu_to_le16(bnxt_cp_ring_for_rx(bp, bnapi->rx_ring));

	return hwrm_req_send(bp, req_rx);
}

static int
bnxt_hwrm_set_rx_coal(struct bnxt *bp, struct bnxt_napi *bnapi,
		      struct hwrm_ring_cmpl_ring_cfg_aggint_params_input *req)
{
	u16 ring_id = bnxt_cp_ring_for_rx(bp, bnapi->rx_ring);

	req->ring_id = cpu_to_le16(ring_id);
	return hwrm_req_send(bp, req);
}

static int
bnxt_hwrm_set_tx_coal(struct bnxt *bp, struct bnxt_napi *bnapi,
		      struct hwrm_ring_cmpl_ring_cfg_aggint_params_input *req)
{
	struct bnxt_tx_ring_info *txr;
	int i, rc;

	bnxt_for_each_napi_tx(i, bnapi, txr) {
		u16 ring_id;

		ring_id = bnxt_cp_ring_for_tx(bp, txr);
		req->ring_id = cpu_to_le16(ring_id);
		rc = hwrm_req_send(bp, req);
		if (rc)
			return rc;
		if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
			return 0;
	}
	return 0;
}

int bnxt_hwrm_set_coal(struct bnxt *bp)
{
	struct hwrm_ring_cmpl_ring_cfg_aggint_params_input *req_rx, *req_tx;
	int i, rc;

	rc = hwrm_req_init(bp, req_rx, HWRM_RING_CMPL_RING_CFG_AGGINT_PARAMS);
	if (rc)
		return rc;

	rc = hwrm_req_init(bp, req_tx, HWRM_RING_CMPL_RING_CFG_AGGINT_PARAMS);
	if (rc) {
		hwrm_req_drop(bp, req_rx);
		return rc;
	}

	bnxt_hwrm_set_coal_params(bp, &bp->rx_coal, req_rx);
	bnxt_hwrm_set_coal_params(bp, &bp->tx_coal, req_tx);

	hwrm_req_hold(bp, req_rx);
	hwrm_req_hold(bp, req_tx);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_coal *hw_coal;

		if (!bnapi->rx_ring)
			rc = bnxt_hwrm_set_tx_coal(bp, bnapi, req_tx);
		else
			rc = bnxt_hwrm_set_rx_coal(bp, bnapi, req_rx);
		if (rc)
			break;

		if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
			continue;

		if (bnapi->rx_ring && bnapi->tx_ring[0]) {
			rc = bnxt_hwrm_set_tx_coal(bp, bnapi, req_tx);
			if (rc)
				break;
		}
		if (bnapi->rx_ring)
			hw_coal = &bp->rx_coal;
		else
			hw_coal = &bp->tx_coal;
		__bnxt_hwrm_set_coal_nq(bp, bnapi, hw_coal);
	}
	hwrm_req_drop(bp, req_rx);
	hwrm_req_drop(bp, req_tx);
	return rc;
}

static void bnxt_hwrm_stat_ctx_free(struct bnxt *bp)
{
	struct hwrm_stat_ctx_clr_stats_input *req0 = NULL;
	struct hwrm_stat_ctx_free_input *req;
	int i;

	if (!bp->bnapi)
		return;

	if (BNXT_CHIP_TYPE_NITRO_A0(bp))
		return;

	if (hwrm_req_init(bp, req, HWRM_STAT_CTX_FREE))
		return;
	if (BNXT_FW_MAJ(bp) <= 20) {
		if (hwrm_req_init(bp, req0, HWRM_STAT_CTX_CLR_STATS)) {
			hwrm_req_drop(bp, req);
			return;
		}
		hwrm_req_hold(bp, req0);
	}
	hwrm_req_hold(bp, req);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;

		if (cpr->hw_stats_ctx_id != INVALID_STATS_CTX_ID) {
			req->stat_ctx_id = cpu_to_le32(cpr->hw_stats_ctx_id);
			if (req0) {
				req0->stat_ctx_id = req->stat_ctx_id;
				hwrm_req_send(bp, req0);
			}
			hwrm_req_send(bp, req);

			cpr->hw_stats_ctx_id = INVALID_STATS_CTX_ID;
		}
	}
	hwrm_req_drop(bp, req);
	if (req0)
		hwrm_req_drop(bp, req0);
}

static int bnxt_hwrm_stat_ctx_alloc(struct bnxt *bp)
{
	struct hwrm_stat_ctx_alloc_output *resp;
	struct hwrm_stat_ctx_alloc_input *req;
	int rc, i;

	if (BNXT_CHIP_TYPE_NITRO_A0(bp))
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_STAT_CTX_ALLOC);
	if (rc)
		return rc;

	req->stats_dma_length = cpu_to_le16(bp->hw_ring_stats_size);
	req->update_period_ms = cpu_to_le32(bp->stats_coal_ticks / 1000);

	resp = hwrm_req_hold(bp, req);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr = &bnapi->cp_ring;

		req->stats_dma_addr = cpu_to_le64(cpr->stats.hw_stats_map);

		rc = hwrm_req_send(bp, req);
		if (rc)
			break;

		cpr->hw_stats_ctx_id = le32_to_cpu(resp->stat_ctx_id);

		bp->grp_info[i].fw_stats_ctx = cpr->hw_stats_ctx_id;
	}
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_hwrm_func_qcfg(struct bnxt *bp)
{
	struct hwrm_func_qcfg_output *resp;
	struct hwrm_func_qcfg_input *req;
	u16 flags;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_FUNC_QCFG);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(0xffff);
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (rc)
		goto func_qcfg_exit;

	flags = le16_to_cpu(resp->flags);
#ifdef CONFIG_BNXT_SRIOV
	if (BNXT_VF(bp)) {
		struct bnxt_vf_info *vf = &bp->vf;

		vf->vlan = le16_to_cpu(resp->vlan) & VLAN_VID_MASK;
		if (flags & FUNC_QCFG_RESP_FLAGS_TRUSTED_VF)
			vf->flags |= BNXT_VF_TRUST;
		else
			vf->flags &= ~BNXT_VF_TRUST;
	} else {
		bp->pf.registered_vfs = le16_to_cpu(resp->registered_vfs);
	}
#endif
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

	if (flags & FUNC_QCFG_RESP_FLAGS_ENABLE_RDMA_SRIOV)
		bp->fw_cap |= BNXT_FW_CAP_ENABLE_RDMA_SRIOV;

	switch (resp->port_partition_type) {
	case FUNC_QCFG_RESP_PORT_PARTITION_TYPE_NPAR1_0:
	case FUNC_QCFG_RESP_PORT_PARTITION_TYPE_NPAR1_2:
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

	bp->db_offset = le16_to_cpu(resp->legacy_l2_db_size_kb) * 1024;
	if (BNXT_CHIP_P5(bp)) {
		if (BNXT_PF(bp))
			bp->db_offset = DB_PF_OFFSET_P5;
		else
			bp->db_offset = DB_VF_OFFSET_P5;
	}
	bp->db_size = PAGE_ALIGN(le16_to_cpu(resp->l2_doorbell_bar_size_kb) *
				 1024);
	if (!bp->db_size || bp->db_size > pci_resource_len(bp->pdev, 2) ||
	    bp->db_size <= bp->db_offset)
		bp->db_size = pci_resource_len(bp->pdev, 2);

func_qcfg_exit:
	hwrm_req_drop(bp, req);
	return rc;
}

static void bnxt_init_ctx_initializer(struct bnxt_ctx_mem_type *ctxm,
				      u8 init_val, u8 init_offset,
				      bool init_mask_set)
{
	ctxm->init_value = init_val;
	ctxm->init_offset = BNXT_CTX_INIT_INVALID_OFFSET;
	if (init_mask_set)
		ctxm->init_offset = init_offset * 4;
	else
		ctxm->init_value = 0;
}

static int bnxt_alloc_all_ctx_pg_info(struct bnxt *bp, int ctx_max)
{
	struct bnxt_ctx_mem_info *ctx = bp->ctx;
	u16 type;

	for (type = 0; type < ctx_max; type++) {
		struct bnxt_ctx_mem_type *ctxm = &ctx->ctx_arr[type];
		int n = 1;

		if (!ctxm->max_entries || ctxm->pg_info)
			continue;

		if (ctxm->instance_bmap)
			n = hweight32(ctxm->instance_bmap);
		ctxm->pg_info = kcalloc(n, sizeof(*ctxm->pg_info), GFP_KERNEL);
		if (!ctxm->pg_info)
			return -ENOMEM;
	}
	return 0;
}

static void bnxt_free_one_ctx_mem(struct bnxt *bp,
				  struct bnxt_ctx_mem_type *ctxm, bool force);

#define BNXT_CTX_INIT_VALID(flags)	\
	(!!((flags) &			\
	    FUNC_BACKING_STORE_QCAPS_V2_RESP_FLAGS_ENABLE_CTX_KIND_INIT))

static int bnxt_hwrm_func_backing_store_qcaps_v2(struct bnxt *bp)
{
	struct hwrm_func_backing_store_qcaps_v2_output *resp;
	struct hwrm_func_backing_store_qcaps_v2_input *req;
	struct bnxt_ctx_mem_info *ctx = bp->ctx;
	u16 type;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_FUNC_BACKING_STORE_QCAPS_V2);
	if (rc)
		return rc;

	if (!ctx) {
		ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
		if (!ctx)
			return -ENOMEM;
		bp->ctx = ctx;
	}

	resp = hwrm_req_hold(bp, req);

	for (type = 0; type < BNXT_CTX_V2_MAX; ) {
		struct bnxt_ctx_mem_type *ctxm = &ctx->ctx_arr[type];
		u8 init_val, init_off, i;
		u32 max_entries;
		u16 entry_size;
		__le32 *p;
		u32 flags;

		req->type = cpu_to_le16(type);
		rc = hwrm_req_send(bp, req);
		if (rc)
			goto ctx_done;
		flags = le32_to_cpu(resp->flags);
		type = le16_to_cpu(resp->next_valid_type);
		if (!(flags & BNXT_CTX_MEM_TYPE_VALID)) {
			bnxt_free_one_ctx_mem(bp, ctxm, true);
			continue;
		}
		entry_size = le16_to_cpu(resp->entry_size);
		max_entries = le32_to_cpu(resp->max_num_entries);
		if (ctxm->mem_valid) {
			if (!(flags & BNXT_CTX_MEM_PERSIST) ||
			    ctxm->entry_size != entry_size ||
			    ctxm->max_entries != max_entries)
				bnxt_free_one_ctx_mem(bp, ctxm, true);
			else
				continue;
		}
		ctxm->type = le16_to_cpu(resp->type);
		ctxm->entry_size = entry_size;
		ctxm->flags = flags;
		ctxm->instance_bmap = le32_to_cpu(resp->instance_bit_map);
		ctxm->entry_multiple = resp->entry_multiple;
		ctxm->max_entries = max_entries;
		ctxm->min_entries = le32_to_cpu(resp->min_num_entries);
		init_val = resp->ctx_init_value;
		init_off = resp->ctx_init_offset;
		bnxt_init_ctx_initializer(ctxm, init_val, init_off,
					  BNXT_CTX_INIT_VALID(flags));
		ctxm->split_entry_cnt = min_t(u8, resp->subtype_valid_cnt,
					      BNXT_MAX_SPLIT_ENTRY);
		for (i = 0, p = &resp->split_entry_0; i < ctxm->split_entry_cnt;
		     i++, p++)
			ctxm->split[i] = le32_to_cpu(*p);
	}
	rc = bnxt_alloc_all_ctx_pg_info(bp, BNXT_CTX_V2_MAX);

ctx_done:
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_hwrm_func_backing_store_qcaps(struct bnxt *bp)
{
	struct hwrm_func_backing_store_qcaps_output *resp;
	struct hwrm_func_backing_store_qcaps_input *req;
	int rc;

	if (bp->hwrm_spec_code < 0x10902 || BNXT_VF(bp) ||
	    (bp->ctx && bp->ctx->flags & BNXT_CTX_FLAG_INITED))
		return 0;

	if (bp->fw_cap & BNXT_FW_CAP_BACKING_STORE_V2)
		return bnxt_hwrm_func_backing_store_qcaps_v2(bp);

	rc = hwrm_req_init(bp, req, HWRM_FUNC_BACKING_STORE_QCAPS);
	if (rc)
		return rc;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send_silent(bp, req);
	if (!rc) {
		struct bnxt_ctx_mem_type *ctxm;
		struct bnxt_ctx_mem_info *ctx;
		u8 init_val, init_idx = 0;
		u16 init_mask;

		ctx = bp->ctx;
		if (!ctx) {
			ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
			if (!ctx) {
				rc = -ENOMEM;
				goto ctx_err;
			}
			bp->ctx = ctx;
		}
		init_val = resp->ctx_kind_initializer;
		init_mask = le16_to_cpu(resp->ctx_init_mask);

		ctxm = &ctx->ctx_arr[BNXT_CTX_QP];
		ctxm->max_entries = le32_to_cpu(resp->qp_max_entries);
		ctxm->qp_qp1_entries = le16_to_cpu(resp->qp_min_qp1_entries);
		ctxm->qp_l2_entries = le16_to_cpu(resp->qp_max_l2_entries);
		ctxm->qp_fast_qpmd_entries = le16_to_cpu(resp->fast_qpmd_qp_num_entries);
		ctxm->entry_size = le16_to_cpu(resp->qp_entry_size);
		bnxt_init_ctx_initializer(ctxm, init_val, resp->qp_init_offset,
					  (init_mask & (1 << init_idx++)) != 0);

		ctxm = &ctx->ctx_arr[BNXT_CTX_SRQ];
		ctxm->srq_l2_entries = le16_to_cpu(resp->srq_max_l2_entries);
		ctxm->max_entries = le32_to_cpu(resp->srq_max_entries);
		ctxm->entry_size = le16_to_cpu(resp->srq_entry_size);
		bnxt_init_ctx_initializer(ctxm, init_val, resp->srq_init_offset,
					  (init_mask & (1 << init_idx++)) != 0);

		ctxm = &ctx->ctx_arr[BNXT_CTX_CQ];
		ctxm->cq_l2_entries = le16_to_cpu(resp->cq_max_l2_entries);
		ctxm->max_entries = le32_to_cpu(resp->cq_max_entries);
		ctxm->entry_size = le16_to_cpu(resp->cq_entry_size);
		bnxt_init_ctx_initializer(ctxm, init_val, resp->cq_init_offset,
					  (init_mask & (1 << init_idx++)) != 0);

		ctxm = &ctx->ctx_arr[BNXT_CTX_VNIC];
		ctxm->vnic_entries = le16_to_cpu(resp->vnic_max_vnic_entries);
		ctxm->max_entries = ctxm->vnic_entries +
			le16_to_cpu(resp->vnic_max_ring_table_entries);
		ctxm->entry_size = le16_to_cpu(resp->vnic_entry_size);
		bnxt_init_ctx_initializer(ctxm, init_val,
					  resp->vnic_init_offset,
					  (init_mask & (1 << init_idx++)) != 0);

		ctxm = &ctx->ctx_arr[BNXT_CTX_STAT];
		ctxm->max_entries = le32_to_cpu(resp->stat_max_entries);
		ctxm->entry_size = le16_to_cpu(resp->stat_entry_size);
		bnxt_init_ctx_initializer(ctxm, init_val,
					  resp->stat_init_offset,
					  (init_mask & (1 << init_idx++)) != 0);

		ctxm = &ctx->ctx_arr[BNXT_CTX_STQM];
		ctxm->entry_size = le16_to_cpu(resp->tqm_entry_size);
		ctxm->min_entries = le32_to_cpu(resp->tqm_min_entries_per_ring);
		ctxm->max_entries = le32_to_cpu(resp->tqm_max_entries_per_ring);
		ctxm->entry_multiple = resp->tqm_entries_multiple;
		if (!ctxm->entry_multiple)
			ctxm->entry_multiple = 1;

		memcpy(&ctx->ctx_arr[BNXT_CTX_FTQM], ctxm, sizeof(*ctxm));

		ctxm = &ctx->ctx_arr[BNXT_CTX_MRAV];
		ctxm->max_entries = le32_to_cpu(resp->mrav_max_entries);
		ctxm->entry_size = le16_to_cpu(resp->mrav_entry_size);
		ctxm->mrav_num_entries_units =
			le16_to_cpu(resp->mrav_num_entries_units);
		bnxt_init_ctx_initializer(ctxm, init_val,
					  resp->mrav_init_offset,
					  (init_mask & (1 << init_idx++)) != 0);

		ctxm = &ctx->ctx_arr[BNXT_CTX_TIM];
		ctxm->entry_size = le16_to_cpu(resp->tim_entry_size);
		ctxm->max_entries = le32_to_cpu(resp->tim_max_entries);

		ctx->tqm_fp_rings_count = resp->tqm_fp_rings_count;
		if (!ctx->tqm_fp_rings_count)
			ctx->tqm_fp_rings_count = bp->max_q;
		else if (ctx->tqm_fp_rings_count > BNXT_MAX_TQM_FP_RINGS)
			ctx->tqm_fp_rings_count = BNXT_MAX_TQM_FP_RINGS;

		ctxm = &ctx->ctx_arr[BNXT_CTX_FTQM];
		memcpy(ctxm, &ctx->ctx_arr[BNXT_CTX_STQM], sizeof(*ctxm));
		ctxm->instance_bmap = (1 << ctx->tqm_fp_rings_count) - 1;

		rc = bnxt_alloc_all_ctx_pg_info(bp, BNXT_CTX_MAX);
	} else {
		rc = 0;
	}
ctx_err:
	hwrm_req_drop(bp, req);
	return rc;
}

static void bnxt_hwrm_set_pg_attr(struct bnxt_ring_mem_info *rmem, u8 *pg_attr,
				  __le64 *pg_dir)
{
	if (!rmem->nr_pages)
		return;

	BNXT_SET_CTX_PAGE_ATTR(*pg_attr);
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
	struct hwrm_func_backing_store_cfg_input *req;
	struct bnxt_ctx_mem_info *ctx = bp->ctx;
	struct bnxt_ctx_pg_info *ctx_pg;
	struct bnxt_ctx_mem_type *ctxm;
	void **__req = (void **)&req;
	u32 req_len = sizeof(*req);
	__le32 *num_entries;
	__le64 *pg_dir;
	u32 flags = 0;
	u8 *pg_attr;
	u32 ena;
	int rc;
	int i;

	if (!ctx)
		return 0;

	if (req_len > bp->hwrm_max_ext_req_len)
		req_len = BNXT_BACKING_STORE_CFG_LEGACY_LEN;
	rc = __hwrm_req_init(bp, __req, HWRM_FUNC_BACKING_STORE_CFG, req_len);
	if (rc)
		return rc;

	req->enables = cpu_to_le32(enables);
	if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_QP) {
		ctxm = &ctx->ctx_arr[BNXT_CTX_QP];
		ctx_pg = ctxm->pg_info;
		req->qp_num_entries = cpu_to_le32(ctx_pg->entries);
		req->qp_num_qp1_entries = cpu_to_le16(ctxm->qp_qp1_entries);
		req->qp_num_l2_entries = cpu_to_le16(ctxm->qp_l2_entries);
		req->qp_entry_size = cpu_to_le16(ctxm->entry_size);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req->qpc_pg_size_qpc_lvl,
				      &req->qpc_page_dir);

		if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_QP_FAST_QPMD)
			req->qp_num_fast_qpmd_entries = cpu_to_le16(ctxm->qp_fast_qpmd_entries);
	}
	if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_SRQ) {
		ctxm = &ctx->ctx_arr[BNXT_CTX_SRQ];
		ctx_pg = ctxm->pg_info;
		req->srq_num_entries = cpu_to_le32(ctx_pg->entries);
		req->srq_num_l2_entries = cpu_to_le16(ctxm->srq_l2_entries);
		req->srq_entry_size = cpu_to_le16(ctxm->entry_size);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req->srq_pg_size_srq_lvl,
				      &req->srq_page_dir);
	}
	if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_CQ) {
		ctxm = &ctx->ctx_arr[BNXT_CTX_CQ];
		ctx_pg = ctxm->pg_info;
		req->cq_num_entries = cpu_to_le32(ctx_pg->entries);
		req->cq_num_l2_entries = cpu_to_le16(ctxm->cq_l2_entries);
		req->cq_entry_size = cpu_to_le16(ctxm->entry_size);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req->cq_pg_size_cq_lvl,
				      &req->cq_page_dir);
	}
	if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_VNIC) {
		ctxm = &ctx->ctx_arr[BNXT_CTX_VNIC];
		ctx_pg = ctxm->pg_info;
		req->vnic_num_vnic_entries = cpu_to_le16(ctxm->vnic_entries);
		req->vnic_num_ring_table_entries =
			cpu_to_le16(ctxm->max_entries - ctxm->vnic_entries);
		req->vnic_entry_size = cpu_to_le16(ctxm->entry_size);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req->vnic_pg_size_vnic_lvl,
				      &req->vnic_page_dir);
	}
	if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_STAT) {
		ctxm = &ctx->ctx_arr[BNXT_CTX_STAT];
		ctx_pg = ctxm->pg_info;
		req->stat_num_entries = cpu_to_le32(ctxm->max_entries);
		req->stat_entry_size = cpu_to_le16(ctxm->entry_size);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req->stat_pg_size_stat_lvl,
				      &req->stat_page_dir);
	}
	if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_MRAV) {
		u32 units;

		ctxm = &ctx->ctx_arr[BNXT_CTX_MRAV];
		ctx_pg = ctxm->pg_info;
		req->mrav_num_entries = cpu_to_le32(ctx_pg->entries);
		units = ctxm->mrav_num_entries_units;
		if (units) {
			u32 num_mr, num_ah = ctxm->mrav_av_entries;
			u32 entries;

			num_mr = ctx_pg->entries - num_ah;
			entries = ((num_mr / units) << 16) | (num_ah / units);
			req->mrav_num_entries = cpu_to_le32(entries);
			flags |= FUNC_BACKING_STORE_CFG_REQ_FLAGS_MRAV_RESERVATION_SPLIT;
		}
		req->mrav_entry_size = cpu_to_le16(ctxm->entry_size);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req->mrav_pg_size_mrav_lvl,
				      &req->mrav_page_dir);
	}
	if (enables & FUNC_BACKING_STORE_CFG_REQ_ENABLES_TIM) {
		ctxm = &ctx->ctx_arr[BNXT_CTX_TIM];
		ctx_pg = ctxm->pg_info;
		req->tim_num_entries = cpu_to_le32(ctx_pg->entries);
		req->tim_entry_size = cpu_to_le16(ctxm->entry_size);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req->tim_pg_size_tim_lvl,
				      &req->tim_page_dir);
	}
	ctxm = &ctx->ctx_arr[BNXT_CTX_STQM];
	for (i = 0, num_entries = &req->tqm_sp_num_entries,
	     pg_attr = &req->tqm_sp_pg_size_tqm_sp_lvl,
	     pg_dir = &req->tqm_sp_page_dir,
	     ena = FUNC_BACKING_STORE_CFG_REQ_ENABLES_TQM_SP,
	     ctx_pg = ctxm->pg_info;
	     i < BNXT_MAX_TQM_RINGS;
	     ctx_pg = &ctx->ctx_arr[BNXT_CTX_FTQM].pg_info[i],
	     i++, num_entries++, pg_attr++, pg_dir++, ena <<= 1) {
		if (!(enables & ena))
			continue;

		req->tqm_entry_size = cpu_to_le16(ctxm->entry_size);
		*num_entries = cpu_to_le32(ctx_pg->entries);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem, pg_attr, pg_dir);
	}
	req->flags = cpu_to_le32(flags);
	return hwrm_req_send(bp, req);
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
				  u8 depth, struct bnxt_ctx_mem_type *ctxm)
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
			rmem->ctx_mem = ctxm;
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
		rmem->ctx_mem = ctxm;
		rc = bnxt_alloc_ctx_mem_blk(bp, ctx_pg);
	}
	return rc;
}

static size_t bnxt_copy_ctx_pg_tbls(struct bnxt *bp,
				    struct bnxt_ctx_pg_info *ctx_pg,
				    void *buf, size_t offset, size_t head,
				    size_t tail)
{
	struct bnxt_ring_mem_info *rmem = &ctx_pg->ring_mem;
	size_t nr_pages = ctx_pg->nr_pages;
	int page_size = rmem->page_size;
	size_t len = 0, total_len = 0;
	u16 depth = rmem->depth;

	tail %= nr_pages * page_size;
	do {
		if (depth > 1) {
			int i = head / (page_size * MAX_CTX_PAGES);
			struct bnxt_ctx_pg_info *pg_tbl;

			pg_tbl = ctx_pg->ctx_pg_tbl[i];
			rmem = &pg_tbl->ring_mem;
		}
		len = __bnxt_copy_ring(bp, rmem, buf, offset, head, tail);
		head += len;
		offset += len;
		total_len += len;
		if (head >= nr_pages * page_size)
			head = 0;
	} while (head != tail);
	return total_len;
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

static int bnxt_setup_ctxm_pg_tbls(struct bnxt *bp,
				   struct bnxt_ctx_mem_type *ctxm, u32 entries,
				   u8 pg_lvl)
{
	struct bnxt_ctx_pg_info *ctx_pg = ctxm->pg_info;
	int i, rc = 0, n = 1;
	u32 mem_size;

	if (!ctxm->entry_size || !ctx_pg)
		return -EINVAL;
	if (ctxm->instance_bmap)
		n = hweight32(ctxm->instance_bmap);
	if (ctxm->entry_multiple)
		entries = roundup(entries, ctxm->entry_multiple);
	entries = clamp_t(u32, entries, ctxm->min_entries, ctxm->max_entries);
	mem_size = entries * ctxm->entry_size;
	for (i = 0; i < n && !rc; i++) {
		ctx_pg[i].entries = entries;
		rc = bnxt_alloc_ctx_pg_tbls(bp, &ctx_pg[i], mem_size, pg_lvl,
					    ctxm->init_value ? ctxm : NULL);
	}
	if (!rc)
		ctxm->mem_valid = 1;
	return rc;
}

static int bnxt_hwrm_func_backing_store_cfg_v2(struct bnxt *bp,
					       struct bnxt_ctx_mem_type *ctxm,
					       bool last)
{
	struct hwrm_func_backing_store_cfg_v2_input *req;
	u32 instance_bmap = ctxm->instance_bmap;
	int i, j, rc = 0, n = 1;
	__le32 *p;

	if (!(ctxm->flags & BNXT_CTX_MEM_TYPE_VALID) || !ctxm->pg_info)
		return 0;

	if (instance_bmap)
		n = hweight32(ctxm->instance_bmap);
	else
		instance_bmap = 1;

	rc = hwrm_req_init(bp, req, HWRM_FUNC_BACKING_STORE_CFG_V2);
	if (rc)
		return rc;
	hwrm_req_hold(bp, req);
	req->type = cpu_to_le16(ctxm->type);
	req->entry_size = cpu_to_le16(ctxm->entry_size);
	if ((ctxm->flags & BNXT_CTX_MEM_PERSIST) &&
	    bnxt_bs_trace_avail(bp, ctxm->type)) {
		struct bnxt_bs_trace_info *bs_trace;
		u32 enables;

		enables = FUNC_BACKING_STORE_CFG_V2_REQ_ENABLES_NEXT_BS_OFFSET;
		req->enables = cpu_to_le32(enables);
		bs_trace = &bp->bs_trace[bnxt_bstore_to_trace[ctxm->type]];
		req->next_bs_offset = cpu_to_le32(bs_trace->last_offset);
	}
	req->subtype_valid_cnt = ctxm->split_entry_cnt;
	for (i = 0, p = &req->split_entry_0; i < ctxm->split_entry_cnt; i++)
		p[i] = cpu_to_le32(ctxm->split[i]);
	for (i = 0, j = 0; j < n && !rc; i++) {
		struct bnxt_ctx_pg_info *ctx_pg;

		if (!(instance_bmap & (1 << i)))
			continue;
		req->instance = cpu_to_le16(i);
		ctx_pg = &ctxm->pg_info[j++];
		if (!ctx_pg->entries)
			continue;
		req->num_entries = cpu_to_le32(ctx_pg->entries);
		bnxt_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req->page_size_pbl_level,
				      &req->page_dir);
		if (last && j == n)
			req->flags =
				cpu_to_le32(FUNC_BACKING_STORE_CFG_V2_REQ_FLAGS_BS_CFG_ALL_DONE);
		rc = hwrm_req_send(bp, req);
	}
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_backing_store_cfg_v2(struct bnxt *bp, u32 ena)
{
	struct bnxt_ctx_mem_info *ctx = bp->ctx;
	struct bnxt_ctx_mem_type *ctxm;
	u16 last_type = BNXT_CTX_INV;
	int rc = 0;
	u16 type;

	for (type = BNXT_CTX_SRT; type <= BNXT_CTX_RIGP1; type++) {
		ctxm = &ctx->ctx_arr[type];
		if (!bnxt_bs_trace_avail(bp, type))
			continue;
		if (!ctxm->mem_valid) {
			rc = bnxt_setup_ctxm_pg_tbls(bp, ctxm,
						     ctxm->max_entries, 1);
			if (rc) {
				netdev_warn(bp->dev, "Unable to setup ctx page for type:0x%x.\n",
					    type);
				continue;
			}
			bnxt_bs_trace_init(bp, ctxm);
		}
		last_type = type;
	}

	if (last_type == BNXT_CTX_INV) {
		if (!ena)
			return 0;
		else if (ena & FUNC_BACKING_STORE_CFG_REQ_ENABLES_TIM)
			last_type = BNXT_CTX_MAX - 1;
		else
			last_type = BNXT_CTX_L2_MAX - 1;
	}
	ctx->ctx_arr[last_type].last = 1;

	for (type = 0 ; type < BNXT_CTX_V2_MAX; type++) {
		ctxm = &ctx->ctx_arr[type];

		if (!ctxm->mem_valid)
			continue;
		rc = bnxt_hwrm_func_backing_store_cfg_v2(bp, ctxm, ctxm->last);
		if (rc)
			return rc;
	}
	return 0;
}

/**
 * __bnxt_copy_ctx_mem - copy host context memory
 * @bp: The driver context
 * @ctxm: The pointer to the context memory type
 * @buf: The destination buffer or NULL to just obtain the length
 * @offset: The buffer offset to copy the data to
 * @head: The head offset of context memory to copy from
 * @tail: The tail offset (last byte + 1) of context memory to end the copy
 *
 * This function is called for debugging purposes to dump the host context
 * used by the chip.
 *
 * Return: Length of memory copied
 */
static size_t __bnxt_copy_ctx_mem(struct bnxt *bp,
				  struct bnxt_ctx_mem_type *ctxm, void *buf,
				  size_t offset, size_t head, size_t tail)
{
	struct bnxt_ctx_pg_info *ctx_pg = ctxm->pg_info;
	size_t len = 0, total_len = 0;
	int i, n = 1;

	if (!ctx_pg)
		return 0;

	if (ctxm->instance_bmap)
		n = hweight32(ctxm->instance_bmap);
	for (i = 0; i < n; i++) {
		len = bnxt_copy_ctx_pg_tbls(bp, &ctx_pg[i], buf, offset, head,
					    tail);
		offset += len;
		total_len += len;
	}
	return total_len;
}

size_t bnxt_copy_ctx_mem(struct bnxt *bp, struct bnxt_ctx_mem_type *ctxm,
			 void *buf, size_t offset)
{
	size_t tail = ctxm->max_entries * ctxm->entry_size;

	return __bnxt_copy_ctx_mem(bp, ctxm, buf, offset, 0, tail);
}

static void bnxt_free_one_ctx_mem(struct bnxt *bp,
				  struct bnxt_ctx_mem_type *ctxm, bool force)
{
	struct bnxt_ctx_pg_info *ctx_pg;
	int i, n = 1;

	ctxm->last = 0;

	if (ctxm->mem_valid && !force && (ctxm->flags & BNXT_CTX_MEM_PERSIST))
		return;

	ctx_pg = ctxm->pg_info;
	if (ctx_pg) {
		if (ctxm->instance_bmap)
			n = hweight32(ctxm->instance_bmap);
		for (i = 0; i < n; i++)
			bnxt_free_ctx_pg_tbls(bp, &ctx_pg[i]);

		kfree(ctx_pg);
		ctxm->pg_info = NULL;
		ctxm->mem_valid = 0;
	}
	memset(ctxm, 0, sizeof(*ctxm));
}

void bnxt_free_ctx_mem(struct bnxt *bp, bool force)
{
	struct bnxt_ctx_mem_info *ctx = bp->ctx;
	u16 type;

	if (!ctx)
		return;

	for (type = 0; type < BNXT_CTX_V2_MAX; type++)
		bnxt_free_one_ctx_mem(bp, &ctx->ctx_arr[type], force);

	ctx->flags &= ~BNXT_CTX_FLAG_INITED;
	if (force) {
		kfree(ctx);
		bp->ctx = NULL;
	}
}

static int bnxt_alloc_ctx_mem(struct bnxt *bp)
{
	struct bnxt_ctx_mem_type *ctxm;
	struct bnxt_ctx_mem_info *ctx;
	u32 l2_qps, qp1_qps, max_qps;
	u32 ena, entries_sp, entries;
	u32 srqs, max_srqs, min;
	u32 num_mr, num_ah;
	u32 extra_srqs = 0;
	u32 extra_qps = 0;
	u32 fast_qpmd_qps;
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

	ctxm = &ctx->ctx_arr[BNXT_CTX_QP];
	l2_qps = ctxm->qp_l2_entries;
	qp1_qps = ctxm->qp_qp1_entries;
	fast_qpmd_qps = ctxm->qp_fast_qpmd_entries;
	max_qps = ctxm->max_entries;
	ctxm = &ctx->ctx_arr[BNXT_CTX_SRQ];
	srqs = ctxm->srq_l2_entries;
	max_srqs = ctxm->max_entries;
	ena = 0;
	if ((bp->flags & BNXT_FLAG_ROCE_CAP) && !is_kdump_kernel()) {
		pg_lvl = 2;
		if (BNXT_SW_RES_LMT(bp)) {
			extra_qps = max_qps - l2_qps - qp1_qps;
			extra_srqs = max_srqs - srqs;
		} else {
			extra_qps = min_t(u32, 65536,
					  max_qps - l2_qps - qp1_qps);
			/* allocate extra qps if fw supports RoCE fast qp
			 * destroy feature
			 */
			extra_qps += fast_qpmd_qps;
			extra_srqs = min_t(u32, 8192, max_srqs - srqs);
		}
		if (fast_qpmd_qps)
			ena |= FUNC_BACKING_STORE_CFG_REQ_ENABLES_QP_FAST_QPMD;
	}

	ctxm = &ctx->ctx_arr[BNXT_CTX_QP];
	rc = bnxt_setup_ctxm_pg_tbls(bp, ctxm, l2_qps + qp1_qps + extra_qps,
				     pg_lvl);
	if (rc)
		return rc;

	ctxm = &ctx->ctx_arr[BNXT_CTX_SRQ];
	rc = bnxt_setup_ctxm_pg_tbls(bp, ctxm, srqs + extra_srqs, pg_lvl);
	if (rc)
		return rc;

	ctxm = &ctx->ctx_arr[BNXT_CTX_CQ];
	rc = bnxt_setup_ctxm_pg_tbls(bp, ctxm, ctxm->cq_l2_entries +
				     extra_qps * 2, pg_lvl);
	if (rc)
		return rc;

	ctxm = &ctx->ctx_arr[BNXT_CTX_VNIC];
	rc = bnxt_setup_ctxm_pg_tbls(bp, ctxm, ctxm->max_entries, 1);
	if (rc)
		return rc;

	ctxm = &ctx->ctx_arr[BNXT_CTX_STAT];
	rc = bnxt_setup_ctxm_pg_tbls(bp, ctxm, ctxm->max_entries, 1);
	if (rc)
		return rc;

	if (!(bp->flags & BNXT_FLAG_ROCE_CAP))
		goto skip_rdma;

	ctxm = &ctx->ctx_arr[BNXT_CTX_MRAV];
	if (BNXT_SW_RES_LMT(bp) &&
	    ctxm->split_entry_cnt == BNXT_CTX_MRAV_AV_SPLIT_ENTRY + 1) {
		num_ah = ctxm->mrav_av_entries;
		num_mr = ctxm->max_entries - num_ah;
	} else {
		/* 128K extra is needed to accommodate static AH context
		 * allocation by f/w.
		 */
		num_mr = min_t(u32, ctxm->max_entries / 2, 1024 * 256);
		num_ah = min_t(u32, num_mr, 1024 * 128);
		ctxm->split_entry_cnt = BNXT_CTX_MRAV_AV_SPLIT_ENTRY + 1;
		if (!ctxm->mrav_av_entries || ctxm->mrav_av_entries > num_ah)
			ctxm->mrav_av_entries = num_ah;
	}

	rc = bnxt_setup_ctxm_pg_tbls(bp, ctxm, num_mr + num_ah, 2);
	if (rc)
		return rc;
	ena |= FUNC_BACKING_STORE_CFG_REQ_ENABLES_MRAV;

	ctxm = &ctx->ctx_arr[BNXT_CTX_TIM];
	rc = bnxt_setup_ctxm_pg_tbls(bp, ctxm, l2_qps + qp1_qps + extra_qps, 1);
	if (rc)
		return rc;
	ena |= FUNC_BACKING_STORE_CFG_REQ_ENABLES_TIM;

skip_rdma:
	ctxm = &ctx->ctx_arr[BNXT_CTX_STQM];
	min = ctxm->min_entries;
	entries_sp = ctx->ctx_arr[BNXT_CTX_VNIC].vnic_entries + l2_qps +
		     2 * (extra_qps + qp1_qps) + min;
	rc = bnxt_setup_ctxm_pg_tbls(bp, ctxm, entries_sp, 2);
	if (rc)
		return rc;

	ctxm = &ctx->ctx_arr[BNXT_CTX_FTQM];
	entries = l2_qps + 2 * (extra_qps + qp1_qps);
	rc = bnxt_setup_ctxm_pg_tbls(bp, ctxm, entries, 2);
	if (rc)
		return rc;
	for (i = 0; i < ctx->tqm_fp_rings_count + 1; i++)
		ena |= FUNC_BACKING_STORE_CFG_REQ_ENABLES_TQM_SP << i;
	ena |= FUNC_BACKING_STORE_CFG_REQ_DFLT_ENABLES;

	if (bp->fw_cap & BNXT_FW_CAP_BACKING_STORE_V2)
		rc = bnxt_backing_store_cfg_v2(bp, ena);
	else
		rc = bnxt_hwrm_func_backing_store_cfg(bp, ena);
	if (rc) {
		netdev_err(bp->dev, "Failed configuring context mem, rc = %d.\n",
			   rc);
		return rc;
	}
	ctx->flags |= BNXT_CTX_FLAG_INITED;
	return 0;
}

static int bnxt_hwrm_crash_dump_mem_cfg(struct bnxt *bp)
{
	struct hwrm_dbg_crashdump_medium_cfg_input *req;
	u16 page_attr;
	int rc;

	if (!(bp->fw_dbg_cap & DBG_QCAPS_RESP_FLAGS_CRASHDUMP_HOST_DDR))
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_DBG_CRASHDUMP_MEDIUM_CFG);
	if (rc)
		return rc;

	if (BNXT_PAGE_SIZE == 0x2000)
		page_attr = DBG_CRASHDUMP_MEDIUM_CFG_REQ_PG_SIZE_PG_8K;
	else if (BNXT_PAGE_SIZE == 0x10000)
		page_attr = DBG_CRASHDUMP_MEDIUM_CFG_REQ_PG_SIZE_PG_64K;
	else
		page_attr = DBG_CRASHDUMP_MEDIUM_CFG_REQ_PG_SIZE_PG_4K;
	req->pg_size_lvl = cpu_to_le16(page_attr |
				       bp->fw_crash_mem->ring_mem.depth);
	req->pbl = cpu_to_le64(bp->fw_crash_mem->ring_mem.pg_tbl_map);
	req->size = cpu_to_le32(bp->fw_crash_len);
	req->output_dest_flags = cpu_to_le16(BNXT_DBG_CR_DUMP_MDM_CFG_DDR);
	return hwrm_req_send(bp, req);
}

static void bnxt_free_crash_dump_mem(struct bnxt *bp)
{
	if (bp->fw_crash_mem) {
		bnxt_free_ctx_pg_tbls(bp, bp->fw_crash_mem);
		kfree(bp->fw_crash_mem);
		bp->fw_crash_mem = NULL;
	}
}

static int bnxt_alloc_crash_dump_mem(struct bnxt *bp)
{
	u32 mem_size = 0;
	int rc;

	if (!(bp->fw_dbg_cap & DBG_QCAPS_RESP_FLAGS_CRASHDUMP_HOST_DDR))
		return 0;

	rc = bnxt_hwrm_get_dump_len(bp, BNXT_DUMP_CRASH, &mem_size);
	if (rc)
		return rc;

	mem_size = round_up(mem_size, 4);

	/* keep and use the existing pages */
	if (bp->fw_crash_mem &&
	    mem_size <= bp->fw_crash_mem->nr_pages * BNXT_PAGE_SIZE)
		goto alloc_done;

	if (bp->fw_crash_mem)
		bnxt_free_ctx_pg_tbls(bp, bp->fw_crash_mem);
	else
		bp->fw_crash_mem = kzalloc(sizeof(*bp->fw_crash_mem),
					   GFP_KERNEL);
	if (!bp->fw_crash_mem)
		return -ENOMEM;

	rc = bnxt_alloc_ctx_pg_tbls(bp, bp->fw_crash_mem, mem_size, 1, NULL);
	if (rc) {
		bnxt_free_crash_dump_mem(bp);
		return rc;
	}

alloc_done:
	bp->fw_crash_len = mem_size;
	return 0;
}

int bnxt_hwrm_func_resc_qcaps(struct bnxt *bp, bool all)
{
	struct hwrm_func_resource_qcaps_output *resp;
	struct hwrm_func_resource_qcaps_input *req;
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_FUNC_RESOURCE_QCAPS);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(0xffff);
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send_silent(bp, req);
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

	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
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
	hwrm_req_drop(bp, req);
	return rc;
}

static int __bnxt_hwrm_ptp_qcfg(struct bnxt *bp)
{
	struct hwrm_port_mac_ptp_qcfg_output *resp;
	struct hwrm_port_mac_ptp_qcfg_input *req;
	struct bnxt_ptp_cfg *ptp = bp->ptp_cfg;
	u8 flags;
	int rc;

	if (bp->hwrm_spec_code < 0x10801 || !BNXT_CHIP_P5_PLUS(bp)) {
		rc = -ENODEV;
		goto no_ptp;
	}

	rc = hwrm_req_init(bp, req, HWRM_PORT_MAC_PTP_QCFG);
	if (rc)
		goto no_ptp;

	req->port_id = cpu_to_le16(bp->pf.port_id);
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (rc)
		goto exit;

	flags = resp->flags;
	if (BNXT_CHIP_P5_AND_MINUS(bp) &&
	    !(flags & PORT_MAC_PTP_QCFG_RESP_FLAGS_HWRM_ACCESS)) {
		rc = -ENODEV;
		goto exit;
	}
	if (!ptp) {
		ptp = kzalloc(sizeof(*ptp), GFP_KERNEL);
		if (!ptp) {
			rc = -ENOMEM;
			goto exit;
		}
		ptp->bp = bp;
		bp->ptp_cfg = ptp;
	}

	if (flags &
	    (PORT_MAC_PTP_QCFG_RESP_FLAGS_PARTIAL_DIRECT_ACCESS_REF_CLOCK |
	     PORT_MAC_PTP_QCFG_RESP_FLAGS_64B_PHC_TIME)) {
		ptp->refclk_regs[0] = le32_to_cpu(resp->ts_ref_clock_reg_lower);
		ptp->refclk_regs[1] = le32_to_cpu(resp->ts_ref_clock_reg_upper);
	} else if (BNXT_CHIP_P5(bp)) {
		ptp->refclk_regs[0] = BNXT_TS_REG_TIMESYNC_TS0_LOWER;
		ptp->refclk_regs[1] = BNXT_TS_REG_TIMESYNC_TS0_UPPER;
	} else {
		rc = -ENODEV;
		goto exit;
	}
	ptp->rtc_configured =
		(flags & PORT_MAC_PTP_QCFG_RESP_FLAGS_RTC_CONFIGURED) != 0;
	rc = bnxt_ptp_init(bp);
	if (rc)
		netdev_warn(bp->dev, "PTP initialization failed.\n");
exit:
	hwrm_req_drop(bp, req);
	if (!rc)
		return 0;

no_ptp:
	bnxt_ptp_clear(bp);
	kfree(ptp);
	bp->ptp_cfg = NULL;
	return rc;
}

static int __bnxt_hwrm_func_qcaps(struct bnxt *bp)
{
	struct hwrm_func_qcaps_output *resp;
	struct hwrm_func_qcaps_input *req;
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;
	u32 flags, flags_ext, flags_ext2;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_FUNC_QCAPS);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(0xffff);
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
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
	if (flags & FUNC_QCAPS_RESP_FLAGS_DBG_QCAPS_CMD_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_DBG_QCAPS;

	flags_ext = le32_to_cpu(resp->flags_ext);
	if (flags_ext & FUNC_QCAPS_RESP_FLAGS_EXT_EXT_HW_STATS_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_EXT_HW_STATS_SUPPORTED;
	if (BNXT_PF(bp) && (flags_ext & FUNC_QCAPS_RESP_FLAGS_EXT_PTP_PPS_SUPPORTED))
		bp->fw_cap |= BNXT_FW_CAP_PTP_PPS;
	if (flags_ext & FUNC_QCAPS_RESP_FLAGS_EXT_PTP_64BIT_RTC_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_PTP_RTC;
	if (BNXT_PF(bp) && (flags_ext & FUNC_QCAPS_RESP_FLAGS_EXT_HOT_RESET_IF_SUPPORT))
		bp->fw_cap |= BNXT_FW_CAP_HOT_RESET_IF;
	if (BNXT_PF(bp) && (flags_ext & FUNC_QCAPS_RESP_FLAGS_EXT_FW_LIVEPATCH_SUPPORTED))
		bp->fw_cap |= BNXT_FW_CAP_LIVEPATCH;
	if (flags_ext & FUNC_QCAPS_RESP_FLAGS_EXT_NPAR_1_2_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_NPAR_1_2;
	if (BNXT_PF(bp) && (flags_ext & FUNC_QCAPS_RESP_FLAGS_EXT_DFLT_VLAN_TPID_PCP_SUPPORTED))
		bp->fw_cap |= BNXT_FW_CAP_DFLT_VLAN_TPID_PCP;
	if (flags_ext & FUNC_QCAPS_RESP_FLAGS_EXT_BS_V2_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_BACKING_STORE_V2;
	if (flags_ext & FUNC_QCAPS_RESP_FLAGS_EXT_TX_COAL_CMPL_CAP)
		bp->flags |= BNXT_FLAG_TX_COAL_CMPL;

	flags_ext2 = le32_to_cpu(resp->flags_ext2);
	if (flags_ext2 & FUNC_QCAPS_RESP_FLAGS_EXT2_RX_ALL_PKTS_TIMESTAMPS_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_RX_ALL_PKT_TS;
	if (flags_ext2 & FUNC_QCAPS_RESP_FLAGS_EXT2_UDP_GSO_SUPPORTED)
		bp->flags |= BNXT_FLAG_UDP_GSO_CAP;
	if (flags_ext2 & FUNC_QCAPS_RESP_FLAGS_EXT2_TX_PKT_TS_CMPL_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_TX_TS_CMP;
	if (flags_ext2 &
	    FUNC_QCAPS_RESP_FLAGS_EXT2_SW_MAX_RESOURCE_LIMITS_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_SW_MAX_RESOURCE_LIMITS;
	if (BNXT_PF(bp) &&
	    (flags_ext2 & FUNC_QCAPS_RESP_FLAGS_EXT2_ROCE_VF_RESOURCE_MGMT_SUPPORTED))
		bp->fw_cap |= BNXT_FW_CAP_ROCE_VF_RESC_MGMT_SUPPORTED;

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

	hw_resc->max_encap_records = le32_to_cpu(resp->max_encap_records);
	hw_resc->max_decap_records = le32_to_cpu(resp->max_decap_records);
	hw_resc->max_tx_em_flows = le32_to_cpu(resp->max_tx_em_flows);
	hw_resc->max_tx_wm_flows = le32_to_cpu(resp->max_tx_wm_flows);
	hw_resc->max_rx_em_flows = le32_to_cpu(resp->max_rx_em_flows);
	hw_resc->max_rx_wm_flows = le32_to_cpu(resp->max_rx_wm_flows);

	if (BNXT_PF(bp)) {
		struct bnxt_pf_info *pf = &bp->pf;

		pf->fw_fid = le16_to_cpu(resp->fid);
		pf->port_id = le16_to_cpu(resp->port_id);
		memcpy(pf->mac_addr, resp->mac_address, ETH_ALEN);
		pf->first_vf_id = le16_to_cpu(resp->first_vf_id);
		pf->max_vfs = le16_to_cpu(resp->max_vfs);
		bp->flags &= ~BNXT_FLAG_WOL_CAP;
		if (flags & FUNC_QCAPS_RESP_FLAGS_WOL_MAGICPKT_SUPPORTED)
			bp->flags |= BNXT_FLAG_WOL_CAP;
		if (flags & FUNC_QCAPS_RESP_FLAGS_PTP_SUPPORTED) {
			bp->fw_cap |= BNXT_FW_CAP_PTP;
		} else {
			bnxt_ptp_clear(bp);
			kfree(bp->ptp_cfg);
			bp->ptp_cfg = NULL;
		}
	} else {
#ifdef CONFIG_BNXT_SRIOV
		struct bnxt_vf_info *vf = &bp->vf;

		vf->fw_fid = le16_to_cpu(resp->fid);
		memcpy(vf->mac_addr, resp->mac_address, ETH_ALEN);
#endif
	}
	bp->tso_max_segs = le16_to_cpu(resp->max_tso_segs);

hwrm_func_qcaps_exit:
	hwrm_req_drop(bp, req);
	return rc;
}

static void bnxt_hwrm_dbg_qcaps(struct bnxt *bp)
{
	struct hwrm_dbg_qcaps_output *resp;
	struct hwrm_dbg_qcaps_input *req;
	int rc;

	bp->fw_dbg_cap = 0;
	if (!(bp->fw_cap & BNXT_FW_CAP_DBG_QCAPS))
		return;

	rc = hwrm_req_init(bp, req, HWRM_DBG_QCAPS);
	if (rc)
		return;

	req->fid = cpu_to_le16(0xffff);
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (rc)
		goto hwrm_dbg_qcaps_exit;

	bp->fw_dbg_cap = le32_to_cpu(resp->flags);

hwrm_dbg_qcaps_exit:
	hwrm_req_drop(bp, req);
}

static int bnxt_hwrm_queue_qportcfg(struct bnxt *bp);

int bnxt_hwrm_func_qcaps(struct bnxt *bp)
{
	int rc;

	rc = __bnxt_hwrm_func_qcaps(bp);
	if (rc)
		return rc;

	bnxt_hwrm_dbg_qcaps(bp);

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
	struct hwrm_cfa_adv_flow_mgnt_qcaps_output *resp;
	struct hwrm_cfa_adv_flow_mgnt_qcaps_input *req;
	u32 flags;
	int rc;

	if (!(bp->fw_cap & BNXT_FW_CAP_CFA_ADV_FLOW))
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_CFA_ADV_FLOW_MGNT_QCAPS);
	if (rc)
		return rc;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (rc)
		goto hwrm_cfa_adv_qcaps_exit;

	flags = le32_to_cpu(resp->flags);
	if (flags &
	    CFA_ADV_FLOW_MGNT_QCAPS_RESP_FLAGS_RFS_RING_TBL_IDX_V2_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_CFA_RFS_RING_TBL_IDX_V2;

	if (flags &
	    CFA_ADV_FLOW_MGNT_QCAPS_RESP_FLAGS_RFS_RING_TBL_IDX_V3_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_CFA_RFS_RING_TBL_IDX_V3;

	if (flags &
	    CFA_ADV_FLOW_MGNT_QCAPS_RESP_FLAGS_NTUPLE_FLOW_RX_EXT_IP_PROTO_SUPPORTED)
		bp->fw_cap |= BNXT_FW_CAP_CFA_NTUPLE_RX_EXT_IP_PROTO;

hwrm_cfa_adv_qcaps_exit:
	hwrm_req_drop(bp, req);
	return rc;
}

static int __bnxt_alloc_fw_health(struct bnxt *bp)
{
	if (bp->fw_health)
		return 0;

	bp->fw_health = kzalloc(sizeof(*bp->fw_health), GFP_KERNEL);
	if (!bp->fw_health)
		return -ENOMEM;

	mutex_init(&bp->fw_health->lock);
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

static void bnxt_inv_fw_health_reg(struct bnxt *bp)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	u32 reg_type;

	if (!fw_health)
		return;

	reg_type = BNXT_FW_HEALTH_REG_TYPE(fw_health->regs[BNXT_FW_HEALTH_REG]);
	if (reg_type == BNXT_FW_HEALTH_REG_TYPE_GRC)
		fw_health->status_reliable = false;

	reg_type = BNXT_FW_HEALTH_REG_TYPE(fw_health->regs[BNXT_FW_RESET_CNT_REG]);
	if (reg_type == BNXT_FW_HEALTH_REG_TYPE_GRC)
		fw_health->resets_reliable = false;
}

static void bnxt_try_map_fw_health_reg(struct bnxt *bp)
{
	void __iomem *hs;
	u32 status_loc;
	u32 reg_type;
	u32 sig;

	if (bp->fw_health)
		bp->fw_health->status_reliable = false;

	__bnxt_map_fw_health_reg(bp, HCOMM_STATUS_STRUCT_LOC);
	hs = bp->bar0 + BNXT_FW_HEALTH_WIN_OFF(HCOMM_STATUS_STRUCT_LOC);

	sig = readl(hs + offsetof(struct hcomm_status, sig_ver));
	if ((sig & HCOMM_STATUS_SIGNATURE_MASK) != HCOMM_STATUS_SIGNATURE_VAL) {
		if (!bp->chip_num) {
			__bnxt_map_fw_health_reg(bp, BNXT_GRC_REG_BASE);
			bp->chip_num = readl(bp->bar0 +
					     BNXT_FW_HEALTH_WIN_BASE +
					     BNXT_GRC_REG_CHIP_NUM);
		}
		if (!BNXT_CHIP_P5_PLUS(bp))
			return;

		status_loc = BNXT_GRC_REG_STATUS_P5 |
			     BNXT_FW_HEALTH_REG_TYPE_BAR0;
	} else {
		status_loc = readl(hs + offsetof(struct hcomm_status,
						 fw_status_loc));
	}

	if (__bnxt_alloc_fw_health(bp)) {
		netdev_warn(bp->dev, "no memory for firmware status checks\n");
		return;
	}

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

	bp->fw_health->status_reliable = false;
	bp->fw_health->resets_reliable = false;
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
	bp->fw_health->status_reliable = true;
	bp->fw_health->resets_reliable = true;
	if (reg_base == 0xffffffff)
		return 0;

	__bnxt_map_fw_health_reg(bp, reg_base);
	return 0;
}

static void bnxt_remap_fw_health_regs(struct bnxt *bp)
{
	if (!bp->fw_health)
		return;

	if (bp->fw_cap & BNXT_FW_CAP_ERROR_RECOVERY) {
		bp->fw_health->status_reliable = true;
		bp->fw_health->resets_reliable = true;
	} else {
		bnxt_try_map_fw_health_reg(bp);
	}
}

static int bnxt_hwrm_error_recovery_qcfg(struct bnxt *bp)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	struct hwrm_error_recovery_qcfg_output *resp;
	struct hwrm_error_recovery_qcfg_input *req;
	int rc, i;

	if (!(bp->fw_cap & BNXT_FW_CAP_ERROR_RECOVERY))
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_ERROR_RECOVERY_QCFG);
	if (rc)
		return rc;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
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
	hwrm_req_drop(bp, req);
	if (!rc)
		rc = bnxt_map_fw_health_regs(bp);
	if (rc)
		bp->fw_cap &= ~BNXT_FW_CAP_ERROR_RECOVERY;
	return rc;
}

static int bnxt_hwrm_func_reset(struct bnxt *bp)
{
	struct hwrm_func_reset_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_FUNC_RESET);
	if (rc)
		return rc;

	req->enables = 0;
	hwrm_req_timeout(bp, req, HWRM_RESET_TIMEOUT);
	return hwrm_req_send(bp, req);
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
	struct hwrm_queue_qportcfg_output *resp;
	struct hwrm_queue_qportcfg_input *req;
	u8 i, j, *qptr;
	bool no_rdma;
	int rc = 0;

	rc = hwrm_req_init(bp, req, HWRM_QUEUE_QPORTCFG);
	if (rc)
		return rc;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
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
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_hwrm_poll(struct bnxt *bp)
{
	struct hwrm_ver_get_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_VER_GET);
	if (rc)
		return rc;

	req->hwrm_intf_maj = HWRM_VERSION_MAJOR;
	req->hwrm_intf_min = HWRM_VERSION_MINOR;
	req->hwrm_intf_upd = HWRM_VERSION_UPDATE;

	hwrm_req_flags(bp, req, BNXT_HWRM_CTX_SILENT | BNXT_HWRM_FULL_WAIT);
	rc = hwrm_req_send(bp, req);
	return rc;
}

static int bnxt_hwrm_ver_get(struct bnxt *bp)
{
	struct hwrm_ver_get_output *resp;
	struct hwrm_ver_get_input *req;
	u16 fw_maj, fw_min, fw_bld, fw_rsv;
	u32 dev_caps_cfg, hwrm_ver;
	int rc, len;

	rc = hwrm_req_init(bp, req, HWRM_VER_GET);
	if (rc)
		return rc;

	hwrm_req_flags(bp, req, BNXT_HWRM_FULL_WAIT);
	bp->hwrm_max_req_len = HWRM_MAX_REQ_LEN;
	req->hwrm_intf_maj = HWRM_VERSION_MAJOR;
	req->hwrm_intf_min = HWRM_VERSION_MINOR;
	req->hwrm_intf_upd = HWRM_VERSION_UPDATE;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
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
	bp->hwrm_cmd_max_timeout = le16_to_cpu(resp->max_req_timeout) * 1000;
	if (!bp->hwrm_cmd_max_timeout)
		bp->hwrm_cmd_max_timeout = HWRM_CMD_MAX_TIMEOUT;
	else if (bp->hwrm_cmd_max_timeout > HWRM_CMD_MAX_TIMEOUT)
		netdev_warn(bp->dev, "Device requests max timeout of %d seconds, may trigger hung task watchdog\n",
			    bp->hwrm_cmd_max_timeout / 1000);

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
	hwrm_req_drop(bp, req);
	return rc;
}

int bnxt_hwrm_fw_set_time(struct bnxt *bp)
{
	struct hwrm_fw_set_time_input *req;
	struct tm tm;
	time64_t now = ktime_get_real_seconds();
	int rc;

	if ((BNXT_VF(bp) && bp->hwrm_spec_code < 0x10901) ||
	    bp->hwrm_spec_code < 0x10400)
		return -EOPNOTSUPP;

	time64_to_tm(now, 0, &tm);
	rc = hwrm_req_init(bp, req, HWRM_FW_SET_TIME);
	if (rc)
		return rc;

	req->year = cpu_to_le16(1900 + tm.tm_year);
	req->month = 1 + tm.tm_mon;
	req->day = tm.tm_mday;
	req->hour = tm.tm_hour;
	req->minute = tm.tm_min;
	req->second = tm.tm_sec;
	return hwrm_req_send(bp, req);
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
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
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
	struct hwrm_port_qstats_input *req;
	struct bnxt_pf_info *pf = &bp->pf;
	int rc;

	if (!(bp->flags & BNXT_FLAG_PORT_STATS))
		return 0;

	if (flags && !(bp->fw_cap & BNXT_FW_CAP_EXT_HW_STATS_SUPPORTED))
		return -EOPNOTSUPP;

	rc = hwrm_req_init(bp, req, HWRM_PORT_QSTATS);
	if (rc)
		return rc;

	req->flags = flags;
	req->port_id = cpu_to_le16(pf->port_id);
	req->tx_stat_host_addr = cpu_to_le64(bp->port_stats.hw_stats_map +
					    BNXT_TX_PORT_STATS_BYTE_OFFSET);
	req->rx_stat_host_addr = cpu_to_le64(bp->port_stats.hw_stats_map);
	return hwrm_req_send(bp, req);
}

static int bnxt_hwrm_port_qstats_ext(struct bnxt *bp, u8 flags)
{
	struct hwrm_queue_pri2cos_qcfg_output *resp_qc;
	struct hwrm_queue_pri2cos_qcfg_input *req_qc;
	struct hwrm_port_qstats_ext_output *resp_qs;
	struct hwrm_port_qstats_ext_input *req_qs;
	struct bnxt_pf_info *pf = &bp->pf;
	u32 tx_stat_size;
	int rc;

	if (!(bp->flags & BNXT_FLAG_PORT_STATS_EXT))
		return 0;

	if (flags && !(bp->fw_cap & BNXT_FW_CAP_EXT_HW_STATS_SUPPORTED))
		return -EOPNOTSUPP;

	rc = hwrm_req_init(bp, req_qs, HWRM_PORT_QSTATS_EXT);
	if (rc)
		return rc;

	req_qs->flags = flags;
	req_qs->port_id = cpu_to_le16(pf->port_id);
	req_qs->rx_stat_size = cpu_to_le16(sizeof(struct rx_port_stats_ext));
	req_qs->rx_stat_host_addr = cpu_to_le64(bp->rx_port_stats_ext.hw_stats_map);
	tx_stat_size = bp->tx_port_stats_ext.hw_stats ?
		       sizeof(struct tx_port_stats_ext) : 0;
	req_qs->tx_stat_size = cpu_to_le16(tx_stat_size);
	req_qs->tx_stat_host_addr = cpu_to_le64(bp->tx_port_stats_ext.hw_stats_map);
	resp_qs = hwrm_req_hold(bp, req_qs);
	rc = hwrm_req_send(bp, req_qs);
	if (!rc) {
		bp->fw_rx_stats_ext_size =
			le16_to_cpu(resp_qs->rx_stat_size) / 8;
		if (BNXT_FW_MAJ(bp) < 220 &&
		    bp->fw_rx_stats_ext_size > BNXT_RX_STATS_EXT_NUM_LEGACY)
			bp->fw_rx_stats_ext_size = BNXT_RX_STATS_EXT_NUM_LEGACY;

		bp->fw_tx_stats_ext_size = tx_stat_size ?
			le16_to_cpu(resp_qs->tx_stat_size) / 8 : 0;
	} else {
		bp->fw_rx_stats_ext_size = 0;
		bp->fw_tx_stats_ext_size = 0;
	}
	hwrm_req_drop(bp, req_qs);

	if (flags)
		return rc;

	if (bp->fw_tx_stats_ext_size <=
	    offsetof(struct tx_port_stats_ext, pfc_pri0_tx_duration_us) / 8) {
		bp->pri2cos_valid = 0;
		return rc;
	}

	rc = hwrm_req_init(bp, req_qc, HWRM_QUEUE_PRI2COS_QCFG);
	if (rc)
		return rc;

	req_qc->flags = cpu_to_le32(QUEUE_PRI2COS_QCFG_REQ_FLAGS_IVLAN);

	resp_qc = hwrm_req_hold(bp, req_qc);
	rc = hwrm_req_send(bp, req_qc);
	if (!rc) {
		u8 *pri2cos;
		int i, j;

		pri2cos = &resp_qc->pri0_cos_queue_id;
		for (i = 0; i < 8; i++) {
			u8 queue_id = pri2cos[i];
			u8 queue_idx;

			/* Per port queue IDs start from 0, 10, 20, etc */
			queue_idx = queue_id % 10;
			if (queue_idx > BNXT_MAX_QUEUE) {
				bp->pri2cos_valid = false;
				hwrm_req_drop(bp, req_qc);
				return rc;
			}
			for (j = 0; j < bp->max_q; j++) {
				if (bp->q_ids[j] == queue_id)
					bp->pri2cos_idx[i] = queue_idx;
			}
		}
		bp->pri2cos_valid = true;
	}
	hwrm_req_drop(bp, req_qc);

	return rc;
}

static void bnxt_hwrm_free_tunnel_ports(struct bnxt *bp)
{
	bnxt_hwrm_tunnel_dst_port_free(bp,
		TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_VXLAN);
	bnxt_hwrm_tunnel_dst_port_free(bp,
		TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_GENEVE);
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
		rc = bnxt_hwrm_vnic_set_tpa(bp, &bp->vnic_info[i], tpa_flags);
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
		bnxt_hwrm_vnic_set_rss(bp, &bp->vnic_info[i], false);
}

static void bnxt_clear_vnic(struct bnxt *bp)
{
	if (!bp->vnic_info)
		return;

	bnxt_hwrm_clear_vnic_filter(bp);
	if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS)) {
		/* clear all RSS setting before free vnic ctx */
		bnxt_hwrm_clear_vnic_rss(bp);
		bnxt_hwrm_vnic_ctx_free(bp);
	}
	/* before free the vnic, undo the vnic tpa settings */
	if (bp->flags & BNXT_FLAG_TPA)
		bnxt_set_tpa(bp, false);
	bnxt_hwrm_vnic_free(bp);
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
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
	struct hwrm_func_cfg_input *req;
	u8 evb_mode;
	int rc;

	if (br_mode == BRIDGE_MODE_VEB)
		evb_mode = FUNC_CFG_REQ_EVB_MODE_VEB;
	else if (br_mode == BRIDGE_MODE_VEPA)
		evb_mode = FUNC_CFG_REQ_EVB_MODE_VEPA;
	else
		return -EINVAL;

	rc = bnxt_hwrm_func_cfg_short_req_init(bp, &req);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(0xffff);
	req->enables = cpu_to_le32(FUNC_CFG_REQ_ENABLES_EVB_MODE);
	req->evb_mode = evb_mode;
	return hwrm_req_send(bp, req);
}

static int bnxt_hwrm_set_cache_line_size(struct bnxt *bp, int size)
{
	struct hwrm_func_cfg_input *req;
	int rc;

	if (BNXT_VF(bp) || bp->hwrm_spec_code < 0x10803)
		return 0;

	rc = bnxt_hwrm_func_cfg_short_req_init(bp, &req);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(0xffff);
	req->enables = cpu_to_le32(FUNC_CFG_REQ_ENABLES_CACHE_LINESIZE);
	req->options = FUNC_CFG_REQ_OPTIONS_CACHE_LINESIZE_SIZE_64;
	if (size == 128)
		req->options = FUNC_CFG_REQ_OPTIONS_CACHE_LINESIZE_SIZE_128;

	return hwrm_req_send(bp, req);
}

static int __bnxt_setup_vnic(struct bnxt *bp, struct bnxt_vnic_info *vnic)
{
	int rc;

	if (vnic->flags & BNXT_VNIC_RFS_NEW_RSS_FLAG)
		goto skip_rss_ctx;

	/* allocate context for vnic */
	rc = bnxt_hwrm_vnic_ctx_alloc(bp, vnic, 0);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic %d alloc failure rc: %x\n",
			   vnic->vnic_id, rc);
		goto vnic_setup_err;
	}
	bp->rsscos_nr_ctxs++;

	if (BNXT_CHIP_TYPE_NITRO_A0(bp)) {
		rc = bnxt_hwrm_vnic_ctx_alloc(bp, vnic, 1);
		if (rc) {
			netdev_err(bp->dev, "hwrm vnic %d cos ctx alloc failure rc: %x\n",
				   vnic->vnic_id, rc);
			goto vnic_setup_err;
		}
		bp->rsscos_nr_ctxs++;
	}

skip_rss_ctx:
	/* configure default vnic, ring grp */
	rc = bnxt_hwrm_vnic_cfg(bp, vnic);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic %d cfg failure rc: %x\n",
			   vnic->vnic_id, rc);
		goto vnic_setup_err;
	}

	/* Enable RSS hashing on vnic */
	rc = bnxt_hwrm_vnic_set_rss(bp, vnic, true);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic %d set rss failure rc: %x\n",
			   vnic->vnic_id, rc);
		goto vnic_setup_err;
	}

	if (bp->flags & BNXT_FLAG_AGG_RINGS) {
		rc = bnxt_hwrm_vnic_set_hds(bp, vnic);
		if (rc) {
			netdev_err(bp->dev, "hwrm vnic %d set hds failure rc: %x\n",
				   vnic->vnic_id, rc);
		}
	}

vnic_setup_err:
	return rc;
}

int bnxt_hwrm_vnic_update(struct bnxt *bp, struct bnxt_vnic_info *vnic,
			  u8 valid)
{
	struct hwrm_vnic_update_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_VNIC_UPDATE);
	if (rc)
		return rc;

	req->vnic_id = cpu_to_le32(vnic->fw_vnic_id);

	if (valid & VNIC_UPDATE_REQ_ENABLES_MRU_VALID)
		req->mru = cpu_to_le16(vnic->mru);

	req->enables = cpu_to_le32(valid);

	return hwrm_req_send(bp, req);
}

int bnxt_hwrm_vnic_rss_cfg_p5(struct bnxt *bp, struct bnxt_vnic_info *vnic)
{
	int rc;

	rc = bnxt_hwrm_vnic_set_rss_p5(bp, vnic, true);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic %d set rss failure rc: %d\n",
			   vnic->vnic_id, rc);
		return rc;
	}
	rc = bnxt_hwrm_vnic_cfg(bp, vnic);
	if (rc)
		netdev_err(bp->dev, "hwrm vnic %d cfg failure rc: %x\n",
			   vnic->vnic_id, rc);
	return rc;
}

int __bnxt_setup_vnic_p5(struct bnxt *bp, struct bnxt_vnic_info *vnic)
{
	int rc, i, nr_ctxs;

	nr_ctxs = bnxt_get_nr_rss_ctxs(bp, bp->rx_nr_rings);
	for (i = 0; i < nr_ctxs; i++) {
		rc = bnxt_hwrm_vnic_ctx_alloc(bp, vnic, i);
		if (rc) {
			netdev_err(bp->dev, "hwrm vnic %d ctx %d alloc failure rc: %x\n",
				   vnic->vnic_id, i, rc);
			break;
		}
		bp->rsscos_nr_ctxs++;
	}
	if (i < nr_ctxs)
		return -ENOMEM;

	rc = bnxt_hwrm_vnic_rss_cfg_p5(bp, vnic);
	if (rc)
		return rc;

	if (bp->flags & BNXT_FLAG_AGG_RINGS) {
		rc = bnxt_hwrm_vnic_set_hds(bp, vnic);
		if (rc) {
			netdev_err(bp->dev, "hwrm vnic %d set hds failure rc: %x\n",
				   vnic->vnic_id, rc);
		}
	}
	return rc;
}

static int bnxt_setup_vnic(struct bnxt *bp, struct bnxt_vnic_info *vnic)
{
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		return __bnxt_setup_vnic_p5(bp, vnic);
	else
		return __bnxt_setup_vnic(bp, vnic);
}

static int bnxt_alloc_and_setup_vnic(struct bnxt *bp,
				     struct bnxt_vnic_info *vnic,
				     u16 start_rx_ring_idx, int rx_rings)
{
	int rc;

	rc = bnxt_hwrm_vnic_alloc(bp, vnic, start_rx_ring_idx, rx_rings);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic %d alloc failure rc: %x\n",
			   vnic->vnic_id, rc);
		return rc;
	}
	return bnxt_setup_vnic(bp, vnic);
}

static int bnxt_alloc_rfs_vnics(struct bnxt *bp)
{
	struct bnxt_vnic_info *vnic;
	int i, rc = 0;

	if (BNXT_SUPPORTS_NTUPLE_VNIC(bp)) {
		vnic = &bp->vnic_info[BNXT_VNIC_NTUPLE];
		return bnxt_alloc_and_setup_vnic(bp, vnic, 0, bp->rx_nr_rings);
	}

	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		return 0;

	for (i = 0; i < bp->rx_nr_rings; i++) {
		u16 vnic_id = i + 1;
		u16 ring_id = i;

		if (vnic_id >= bp->nr_vnics)
			break;

		vnic = &bp->vnic_info[vnic_id];
		vnic->flags |= BNXT_VNIC_RFS_FLAG;
		if (bp->rss_cap & BNXT_RSS_CAP_NEW_RSS_CAP)
			vnic->flags |= BNXT_VNIC_RFS_NEW_RSS_FLAG;
		if (bnxt_alloc_and_setup_vnic(bp, &bp->vnic_info[vnic_id], ring_id, 1))
			break;
	}
	return rc;
}

void bnxt_del_one_rss_ctx(struct bnxt *bp, struct bnxt_rss_ctx *rss_ctx,
			  bool all)
{
	struct bnxt_vnic_info *vnic = &rss_ctx->vnic;
	struct bnxt_filter_base *usr_fltr, *tmp;
	struct bnxt_ntuple_filter *ntp_fltr;
	int i;

	if (netif_running(bp->dev)) {
		bnxt_hwrm_vnic_free_one(bp, &rss_ctx->vnic);
		for (i = 0; i < BNXT_MAX_CTX_PER_VNIC; i++) {
			if (vnic->fw_rss_cos_lb_ctx[i] != INVALID_HW_RING_ID)
				bnxt_hwrm_vnic_ctx_free_one(bp, vnic, i);
		}
	}
	if (!all)
		return;

	list_for_each_entry_safe(usr_fltr, tmp, &bp->usr_fltr_list, list) {
		if ((usr_fltr->flags & BNXT_ACT_RSS_CTX) &&
		    usr_fltr->fw_vnic_id == rss_ctx->index) {
			ntp_fltr = container_of(usr_fltr,
						struct bnxt_ntuple_filter,
						base);
			bnxt_hwrm_cfa_ntuple_filter_free(bp, ntp_fltr);
			bnxt_del_ntp_filter(bp, ntp_fltr);
			bnxt_del_one_usr_fltr(bp, usr_fltr);
		}
	}

	if (vnic->rss_table)
		dma_free_coherent(&bp->pdev->dev, vnic->rss_table_size,
				  vnic->rss_table,
				  vnic->rss_table_dma_addr);
	bp->num_rss_ctx--;
}

static void bnxt_hwrm_realloc_rss_ctx_vnic(struct bnxt *bp)
{
	bool set_tpa = !!(bp->flags & BNXT_FLAG_TPA);
	struct ethtool_rxfh_context *ctx;
	unsigned long context;

	xa_for_each(&bp->dev->ethtool->rss_ctx, context, ctx) {
		struct bnxt_rss_ctx *rss_ctx = ethtool_rxfh_context_priv(ctx);
		struct bnxt_vnic_info *vnic = &rss_ctx->vnic;

		if (bnxt_hwrm_vnic_alloc(bp, vnic, 0, bp->rx_nr_rings) ||
		    bnxt_hwrm_vnic_set_tpa(bp, vnic, set_tpa) ||
		    __bnxt_setup_vnic_p5(bp, vnic)) {
			netdev_err(bp->dev, "Failed to restore RSS ctx %d\n",
				   rss_ctx->index);
			bnxt_del_one_rss_ctx(bp, rss_ctx, true);
			ethtool_rxfh_context_lost(bp->dev, rss_ctx->index);
		}
	}
}

static void bnxt_clear_rss_ctxs(struct bnxt *bp)
{
	struct ethtool_rxfh_context *ctx;
	unsigned long context;

	xa_for_each(&bp->dev->ethtool->rss_ctx, context, ctx) {
		struct bnxt_rss_ctx *rss_ctx = ethtool_rxfh_context_priv(ctx);

		bnxt_del_one_rss_ctx(bp, rss_ctx, false);
	}
}

/* Allow PF, trusted VFs and VFs with default VLAN to be in promiscuous mode */
static bool bnxt_promisc_ok(struct bnxt *bp)
{
#ifdef CONFIG_BNXT_SRIOV
	if (BNXT_VF(bp) && !bp->vf.vlan && !bnxt_is_trusted_vf(bp, &bp->vf))
		return false;
#endif
	return true;
}

static int bnxt_setup_nitroa0_vnic(struct bnxt *bp)
{
	struct bnxt_vnic_info *vnic = &bp->vnic_info[1];
	unsigned int rc = 0;

	rc = bnxt_hwrm_vnic_alloc(bp, vnic, bp->rx_nr_rings - 1, 1);
	if (rc) {
		netdev_err(bp->dev, "Cannot allocate special vnic for NS2 A0: %x\n",
			   rc);
		return rc;
	}

	rc = bnxt_hwrm_vnic_cfg(bp, vnic);
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
	struct bnxt_vnic_info *vnic = &bp->vnic_info[BNXT_VNIC_DEFAULT];
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
	rc = bnxt_hwrm_vnic_alloc(bp, vnic, 0, rx_nr_rings);
	if (rc) {
		netdev_err(bp->dev, "hwrm vnic alloc failure rc: %x\n", rc);
		goto err_out;
	}

	if (BNXT_VF(bp))
		bnxt_hwrm_func_qcfg(bp);

	rc = bnxt_setup_vnic(bp, vnic);
	if (rc)
		goto err_out;
	if (bp->rss_cap & BNXT_RSS_CAP_RSS_HASH_TYPE_DELTA)
		bnxt_hwrm_update_rss_hash_cfg(bp);

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
		if (BNXT_VF(bp) && rc == -ENODEV)
			netdev_err(bp->dev, "Cannot configure L2 filter while PF is unavailable\n");
		else
			netdev_err(bp->dev, "HWRM vnic filter failure rc: %x\n", rc);
		goto err_out;
	}
	vnic->uc_filter_count = 1;

	vnic->rx_mask = 0;
	if (test_bit(BNXT_STATE_HALF_OPEN, &bp->state))
		goto skip_rx_mask;

	if (bp->dev->flags & IFF_BROADCAST)
		vnic->rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_BCAST;

	if (bp->dev->flags & IFF_PROMISC)
		vnic->rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS;

	if (bp->dev->flags & IFF_ALLMULTI) {
		vnic->rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST;
		vnic->mc_list_count = 0;
	} else if (bp->dev->flags & IFF_MULTICAST) {
		u32 mask = 0;

		bnxt_mc_list_updated(bp, &mask);
		vnic->rx_mask |= mask;
	}

	rc = bnxt_cfg_rx_mode(bp);
	if (rc)
		goto err_out;

skip_rx_mask:
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

static int __bnxt_trim_rings(struct bnxt *bp, int *rx, int *tx, int max,
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

static int __bnxt_num_tx_to_cp(struct bnxt *bp, int tx, int tx_sets, int tx_xdp)
{
	return (tx - tx_xdp) / tx_sets + tx_xdp;
}

int bnxt_num_tx_to_cp(struct bnxt *bp, int tx)
{
	int tcs = bp->num_tc;

	if (!tcs)
		tcs = 1;
	return __bnxt_num_tx_to_cp(bp, tx, tcs, bp->tx_nr_rings_xdp);
}

static int bnxt_num_cp_to_tx(struct bnxt *bp, int tx_cp)
{
	int tcs = bp->num_tc;

	return (tx_cp - bp->tx_nr_rings_xdp) * tcs +
	       bp->tx_nr_rings_xdp;
}

static int bnxt_trim_rings(struct bnxt *bp, int *rx, int *tx, int max,
			   bool sh)
{
	int tx_cp = bnxt_num_tx_to_cp(bp, *tx);

	if (tx_cp != *tx) {
		int tx_saved = tx_cp, rc;

		rc = __bnxt_trim_rings(bp, rx, &tx_cp, max, sh);
		if (rc)
			return rc;
		if (tx_cp != tx_saved)
			*tx = bnxt_num_cp_to_tx(bp, tx_cp);
		return 0;
	}
	return __bnxt_trim_rings(bp, rx, tx, max, sh);
}

static void bnxt_setup_msix(struct bnxt *bp)
{
	const int len = sizeof(bp->irq_tbl[0].name);
	struct net_device *dev = bp->dev;
	int tcs, i;

	tcs = bp->num_tc;
	if (tcs) {
		int i, off, count;

		for (i = 0; i < tcs; i++) {
			count = bp->tx_nr_rings_per_tc;
			off = BNXT_TC_TO_RING_BASE(bp, i);
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

static int bnxt_init_int_mode(struct bnxt *bp);

static int bnxt_change_msix(struct bnxt *bp, int total)
{
	struct msi_map map;
	int i;

	/* add MSIX to the end if needed */
	for (i = bp->total_irqs; i < total; i++) {
		map = pci_msix_alloc_irq_at(bp->pdev, i, NULL);
		if (map.index < 0)
			return bp->total_irqs;
		bp->irq_tbl[i].vector = map.virq;
		bp->total_irqs++;
	}

	/* trim MSIX from the end if needed */
	for (i = bp->total_irqs; i > total; i--) {
		map.index = i - 1;
		map.virq = bp->irq_tbl[i - 1].vector;
		pci_msix_free_irq(bp->pdev, map);
		bp->total_irqs--;
	}
	return bp->total_irqs;
}

static int bnxt_setup_int_mode(struct bnxt *bp)
{
	int rc;

	if (!bp->irq_tbl) {
		rc = bnxt_init_int_mode(bp);
		if (rc || !bp->irq_tbl)
			return rc ?: -ENODEV;
	}

	bnxt_setup_msix(bp);

	rc = bnxt_set_real_num_queues(bp);
	return rc;
}

static unsigned int bnxt_get_max_func_rss_ctxs(struct bnxt *bp)
{
	return bp->hw_resc.max_rsscos_ctxs;
}

static unsigned int bnxt_get_max_func_vnics(struct bnxt *bp)
{
	return bp->hw_resc.max_vnics;
}

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

	if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
		cp -= bnxt_get_ulp_msix_num(bp);

	return cp;
}

static unsigned int bnxt_get_max_func_irqs(struct bnxt *bp)
{
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;

	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
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
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		return cp - bp->rx_nr_rings - bp->tx_nr_rings;
	else
		return cp - bp->cp_nr_rings;
}

unsigned int bnxt_get_avail_stat_ctxs_for_en(struct bnxt *bp)
{
	return bnxt_get_max_func_stat_ctxs(bp) - bnxt_get_func_stat_ctxs(bp);
}

static int bnxt_get_avail_msix(struct bnxt *bp, int num)
{
	int max_irq = bnxt_get_max_func_irqs(bp);
	int total_req = bp->cp_nr_rings + num;

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

static int bnxt_init_int_mode(struct bnxt *bp)
{
	int i, total_vecs, max, rc = 0, min = 1, ulp_msix, tx_cp, tbl_size;

	total_vecs = bnxt_get_num_msix(bp);
	max = bnxt_get_max_func_irqs(bp);
	if (total_vecs > max)
		total_vecs = max;

	if (!total_vecs)
		return 0;

	if (!(bp->flags & BNXT_FLAG_SHARED_RINGS))
		min = 2;

	total_vecs = pci_alloc_irq_vectors(bp->pdev, min, total_vecs,
					   PCI_IRQ_MSIX);
	ulp_msix = bnxt_get_ulp_msix_num(bp);
	if (total_vecs < 0 || total_vecs < ulp_msix) {
		rc = -ENODEV;
		goto msix_setup_exit;
	}

	tbl_size = total_vecs;
	if (pci_msix_can_alloc_dyn(bp->pdev))
		tbl_size = max;
	bp->irq_tbl = kcalloc(tbl_size, sizeof(*bp->irq_tbl), GFP_KERNEL);
	if (bp->irq_tbl) {
		for (i = 0; i < total_vecs; i++)
			bp->irq_tbl[i].vector = pci_irq_vector(bp->pdev, i);

		bp->total_irqs = total_vecs;
		/* Trim rings based upon num of vectors allocated */
		rc = bnxt_trim_rings(bp, &bp->rx_nr_rings, &bp->tx_nr_rings,
				     total_vecs - ulp_msix, min == 1);
		if (rc)
			goto msix_setup_exit;

		tx_cp = bnxt_num_tx_to_cp(bp, bp->tx_nr_rings);
		bp->cp_nr_rings = (min == 1) ?
				  max_t(int, tx_cp, bp->rx_nr_rings) :
				  tx_cp + bp->rx_nr_rings;

	} else {
		rc = -ENOMEM;
		goto msix_setup_exit;
	}
	return 0;

msix_setup_exit:
	netdev_err(bp->dev, "bnxt_init_int_mode err: %x\n", rc);
	kfree(bp->irq_tbl);
	bp->irq_tbl = NULL;
	pci_free_irq_vectors(bp->pdev);
	return rc;
}

static void bnxt_clear_int_mode(struct bnxt *bp)
{
	pci_free_irq_vectors(bp->pdev);

	kfree(bp->irq_tbl);
	bp->irq_tbl = NULL;
}

int bnxt_reserve_rings(struct bnxt *bp, bool irq_re_init)
{
	bool irq_cleared = false;
	bool irq_change = false;
	int tcs = bp->num_tc;
	int irqs_required;
	int rc;

	if (!bnxt_need_reserve_rings(bp))
		return 0;

	if (BNXT_NEW_RM(bp) && !bnxt_ulp_registered(bp->edev)) {
		int ulp_msix = bnxt_get_avail_msix(bp, bp->ulp_num_msix_want);

		if (ulp_msix > bp->ulp_num_msix_want)
			ulp_msix = bp->ulp_num_msix_want;
		irqs_required = ulp_msix + bp->cp_nr_rings;
	} else {
		irqs_required = bnxt_get_num_msix(bp);
	}

	if (irq_re_init && BNXT_NEW_RM(bp) && irqs_required != bp->total_irqs) {
		irq_change = true;
		if (!pci_msix_can_alloc_dyn(bp->pdev)) {
			bnxt_ulp_irq_stop(bp);
			bnxt_clear_int_mode(bp);
			irq_cleared = true;
		}
	}
	rc = __bnxt_reserve_rings(bp);
	if (irq_cleared) {
		if (!rc)
			rc = bnxt_init_int_mode(bp);
		bnxt_ulp_irq_restart(bp, rc);
	} else if (irq_change && !rc) {
		if (bnxt_change_msix(bp, irqs_required) != irqs_required)
			rc = -ENOSPC;
	}
	if (rc) {
		netdev_err(bp->dev, "ring reservation/IRQ init failure rc: %d\n", rc);
		return rc;
	}
	if (tcs && (bp->tx_nr_rings_per_tc * tcs !=
		    bp->tx_nr_rings - bp->tx_nr_rings_xdp)) {
		netdev_err(bp->dev, "tx ring reservation failure\n");
		netdev_reset_tc(bp->dev);
		bp->num_tc = 0;
		if (bp->tx_nr_rings_xdp)
			bp->tx_nr_rings_per_tc = bp->tx_nr_rings_xdp;
		else
			bp->tx_nr_rings_per_tc = bp->tx_nr_rings;
		return -ENOMEM;
	}
	return 0;
}

static void bnxt_tx_queue_stop(struct bnxt *bp, int idx)
{
	struct bnxt_tx_ring_info *txr;
	struct netdev_queue *txq;
	struct bnxt_napi *bnapi;
	int i;

	bnapi = bp->bnapi[idx];
	bnxt_for_each_napi_tx(i, bnapi, txr) {
		WRITE_ONCE(txr->dev_state, BNXT_DEV_STATE_CLOSING);
		synchronize_net();

		if (!(bnapi->flags & BNXT_NAPI_FLAG_XDP)) {
			txq = netdev_get_tx_queue(bp->dev, txr->txq_index);
			if (txq) {
				__netif_tx_lock_bh(txq);
				netif_tx_stop_queue(txq);
				__netif_tx_unlock_bh(txq);
			}
		}

		if (!bp->tph_mode)
			continue;

		bnxt_hwrm_tx_ring_free(bp, txr, true);
		bnxt_hwrm_cp_ring_free(bp, txr->tx_cpr);
		bnxt_free_one_tx_ring_skbs(bp, txr, txr->txq_index);
		bnxt_clear_one_cp_ring(bp, txr->tx_cpr);
	}
}

static int bnxt_tx_queue_start(struct bnxt *bp, int idx)
{
	struct bnxt_tx_ring_info *txr;
	struct netdev_queue *txq;
	struct bnxt_napi *bnapi;
	int rc, i;

	bnapi = bp->bnapi[idx];
	/* All rings have been reserved and previously allocated.
	 * Reallocating with the same parameters should never fail.
	 */
	bnxt_for_each_napi_tx(i, bnapi, txr) {
		if (!bp->tph_mode)
			goto start_tx;

		rc = bnxt_hwrm_cp_ring_alloc_p5(bp, txr->tx_cpr);
		if (rc)
			return rc;

		rc = bnxt_hwrm_tx_ring_alloc(bp, txr, false);
		if (rc)
			return rc;

		txr->tx_prod = 0;
		txr->tx_cons = 0;
		txr->tx_hw_cons = 0;
start_tx:
		WRITE_ONCE(txr->dev_state, 0);
		synchronize_net();

		if (bnapi->flags & BNXT_NAPI_FLAG_XDP)
			continue;

		txq = netdev_get_tx_queue(bp->dev, txr->txq_index);
		if (txq)
			netif_tx_start_queue(txq);
	}

	return 0;
}

static void bnxt_irq_affinity_notify(struct irq_affinity_notify *notify,
				     const cpumask_t *mask)
{
	struct bnxt_irq *irq;
	u16 tag;
	int err;

	irq = container_of(notify, struct bnxt_irq, affinity_notify);

	if (!irq->bp->tph_mode)
		return;

	cpumask_copy(irq->cpu_mask, mask);

	if (irq->ring_nr >= irq->bp->rx_nr_rings)
		return;

	if (pcie_tph_get_cpu_st(irq->bp->pdev, TPH_MEM_TYPE_VM,
				cpumask_first(irq->cpu_mask), &tag))
		return;

	if (pcie_tph_set_st_entry(irq->bp->pdev, irq->msix_nr, tag))
		return;

	netdev_lock(irq->bp->dev);
	if (netif_running(irq->bp->dev)) {
		err = netdev_rx_queue_restart(irq->bp->dev, irq->ring_nr);
		if (err)
			netdev_err(irq->bp->dev,
				   "RX queue restart failed: err=%d\n", err);
	}
	netdev_unlock(irq->bp->dev);
}

static void bnxt_irq_affinity_release(struct kref *ref)
{
	struct irq_affinity_notify *notify =
		container_of(ref, struct irq_affinity_notify, kref);
	struct bnxt_irq *irq;

	irq = container_of(notify, struct bnxt_irq, affinity_notify);

	if (!irq->bp->tph_mode)
		return;

	if (pcie_tph_set_st_entry(irq->bp->pdev, irq->msix_nr, 0)) {
		netdev_err(irq->bp->dev,
			   "Setting ST=0 for MSIX entry %d failed\n",
			   irq->msix_nr);
		return;
	}
}

static void bnxt_release_irq_notifier(struct bnxt_irq *irq)
{
	irq_set_affinity_notifier(irq->vector, NULL);
}

static void bnxt_register_irq_notifier(struct bnxt *bp, struct bnxt_irq *irq)
{
	struct irq_affinity_notify *notify;

	irq->bp = bp;

	/* Nothing to do if TPH is not enabled */
	if (!bp->tph_mode)
		return;

	/* Register IRQ affinity notifier */
	notify = &irq->affinity_notify;
	notify->irq = irq->vector;
	notify->notify = bnxt_irq_affinity_notify;
	notify->release = bnxt_irq_affinity_release;

	irq_set_affinity_notifier(irq->vector, notify);
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
				irq_update_affinity_hint(irq->vector, NULL);
				free_cpumask_var(irq->cpu_mask);
				irq->have_cpumask = 0;
			}

			bnxt_release_irq_notifier(irq);

			free_irq(irq->vector, bp->bnapi[i]);
		}

		irq->requested = 0;
	}

	/* Disable TPH support */
	pcie_disable_tph(bp->pdev);
	bp->tph_mode = 0;
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

	/* Enable TPH support as part of IRQ request */
	rc = pcie_enable_tph(bp->pdev, PCI_TPH_ST_IV_MODE);
	if (!rc)
		bp->tph_mode = PCI_TPH_ST_IV_MODE;

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

		netif_napi_set_irq_locked(&bp->bnapi[i]->napi, irq->vector);
		irq->requested = 1;

		if (zalloc_cpumask_var(&irq->cpu_mask, GFP_KERNEL)) {
			int numa_node = dev_to_node(&bp->pdev->dev);
			u16 tag;

			irq->have_cpumask = 1;
			irq->msix_nr = map_idx;
			irq->ring_nr = i;
			cpumask_set_cpu(cpumask_local_spread(i, numa_node),
					irq->cpu_mask);
			rc = irq_update_affinity_hint(irq->vector, irq->cpu_mask);
			if (rc) {
				netdev_warn(bp->dev,
					    "Update affinity hint failed, IRQ = %d\n",
					    irq->vector);
				break;
			}

			bnxt_register_irq_notifier(bp, irq);

			/* Init ST table entry */
			if (pcie_tph_get_cpu_st(irq->bp->pdev, TPH_MEM_TYPE_VM,
						cpumask_first(irq->cpu_mask),
						&tag))
				continue;

			pcie_tph_set_st_entry(irq->bp->pdev, irq->msix_nr, tag);
		}
	}
	return rc;
}

static void bnxt_del_napi(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi)
		return;

	for (i = 0; i < bp->rx_nr_rings; i++)
		netif_queue_set_napi(bp->dev, i, NETDEV_QUEUE_TYPE_RX, NULL);
	for (i = 0; i < bp->tx_nr_rings - bp->tx_nr_rings_xdp; i++)
		netif_queue_set_napi(bp->dev, i, NETDEV_QUEUE_TYPE_TX, NULL);

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];

		__netif_napi_del_locked(&bnapi->napi);
	}
	/* We called __netif_napi_del_locked(), we need
	 * to respect an RCU grace period before freeing napi structures.
	 */
	synchronize_net();
}

static void bnxt_init_napi(struct bnxt *bp)
{
	int (*poll_fn)(struct napi_struct *, int) = bnxt_poll;
	unsigned int cp_nr_rings = bp->cp_nr_rings;
	struct bnxt_napi *bnapi;
	int i;

	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		poll_fn = bnxt_poll_p5;
	else if (BNXT_CHIP_TYPE_NITRO_A0(bp))
		cp_nr_rings--;
	for (i = 0; i < cp_nr_rings; i++) {
		bnapi = bp->bnapi[i];
		netif_napi_add_config_locked(bp->dev, &bnapi->napi, poll_fn,
					     bnapi->index);
	}
	if (BNXT_CHIP_TYPE_NITRO_A0(bp)) {
		bnapi = bp->bnapi[cp_nr_rings];
		netif_napi_add_locked(bp->dev, &bnapi->napi, bnxt_poll_nitroa0);
	}
}

static void bnxt_disable_napi(struct bnxt *bp)
{
	int i;

	if (!bp->bnapi ||
	    test_and_set_bit(BNXT_STATE_NAPI_DISABLED, &bp->state))
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr;

		cpr = &bnapi->cp_ring;
		if (bnapi->tx_fault)
			cpr->sw_stats->tx.tx_resets++;
		if (bnapi->in_reset)
			cpr->sw_stats->rx.rx_resets++;
		napi_disable_locked(&bnapi->napi);
	}
}

static void bnxt_enable_napi(struct bnxt *bp)
{
	int i;

	clear_bit(BNXT_STATE_NAPI_DISABLED, &bp->state);
	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr;

		bnapi->tx_fault = 0;

		cpr = &bnapi->cp_ring;
		bnapi->in_reset = false;

		if (bnapi->rx_ring) {
			INIT_WORK(&cpr->dim.work, bnxt_dim_work);
			cpr->dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;
		}
		napi_enable_locked(&bnapi->napi);
	}
}

void bnxt_tx_disable(struct bnxt *bp)
{
	int i;
	struct bnxt_tx_ring_info *txr;

	if (bp->tx_ring) {
		for (i = 0; i < bp->tx_nr_rings; i++) {
			txr = &bp->tx_ring[i];
			WRITE_ONCE(txr->dev_state, BNXT_DEV_STATE_CLOSING);
		}
	}
	/* Make sure napi polls see @dev_state change */
	synchronize_net();
	/* Drop carrier first to prevent TX timeout */
	netif_carrier_off(bp->dev);
	/* Stop all TX queues */
	netif_tx_disable(bp->dev);
}

void bnxt_tx_enable(struct bnxt *bp)
{
	int i;
	struct bnxt_tx_ring_info *txr;

	for (i = 0; i < bp->tx_nr_rings; i++) {
		txr = &bp->tx_ring[i];
		WRITE_ONCE(txr->dev_state, 0);
	}
	/* Make sure napi polls see @dev_state change */
	synchronize_net();
	netif_tx_wake_all_queues(bp->dev);
	if (BNXT_LINK_IS_UP(bp))
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

void bnxt_report_link(struct bnxt *bp)
{
	if (BNXT_LINK_IS_UP(bp)) {
		const char *signal = "";
		const char *flow_ctrl;
		const char *duplex;
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
		if (bp->link_info.phy_qcfg_resp.option_flags &
		    PORT_PHY_QCFG_RESP_OPTION_FLAGS_SIGNAL_MODE_KNOWN) {
			u8 sig_mode = bp->link_info.active_fec_sig_mode &
				      PORT_PHY_QCFG_RESP_SIGNAL_MODE_MASK;
			switch (sig_mode) {
			case PORT_PHY_QCFG_RESP_SIGNAL_MODE_NRZ:
				signal = "(NRZ) ";
				break;
			case PORT_PHY_QCFG_RESP_SIGNAL_MODE_PAM4:
				signal = "(PAM4 56Gbps) ";
				break;
			case PORT_PHY_QCFG_RESP_SIGNAL_MODE_PAM4_112:
				signal = "(PAM4 112Gbps) ";
				break;
			default:
				break;
			}
		}
		netdev_info(bp->dev, "NIC Link is Up, %u Mbps %s%s duplex, Flow control: %s\n",
			    speed, signal, duplex, flow_ctrl);
		if (bp->phy_flags & BNXT_PHY_FL_EEE_CAP)
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
	    !resp->supported_pam4_speeds_force_mode &&
	    !resp->supported_speeds2_auto_mode &&
	    !resp->supported_speeds2_force_mode)
		return true;
	return false;
}

static int bnxt_hwrm_phy_qcaps(struct bnxt *bp)
{
	struct bnxt_link_info *link_info = &bp->link_info;
	struct hwrm_port_phy_qcaps_output *resp;
	struct hwrm_port_phy_qcaps_input *req;
	int rc = 0;

	if (bp->hwrm_spec_code < 0x10201)
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_PORT_PHY_QCAPS);
	if (rc)
		return rc;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (rc)
		goto hwrm_phy_qcaps_exit;

	bp->phy_flags = resp->flags | (le16_to_cpu(resp->flags2) << 8);
	if (resp->flags & PORT_PHY_QCAPS_RESP_FLAGS_EEE_SUPPORTED) {
		struct ethtool_keee *eee = &bp->eee;
		u16 fw_speeds = le16_to_cpu(resp->supported_speeds_eee_mode);

		_bnxt_fw_to_linkmode(eee->supported, fw_speeds);
		bp->lpi_tmr_lo = le32_to_cpu(resp->tx_lpi_timer_low) &
				 PORT_PHY_QCAPS_RESP_TX_LPI_TIMER_LOW_MASK;
		bp->lpi_tmr_hi = le32_to_cpu(resp->valid_tx_lpi_timer_high) &
				 PORT_PHY_QCAPS_RESP_TX_LPI_TIMER_HIGH_MASK;
	}

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
			link_info->support_auto_speeds2 = 0;
		}
	}
	if (resp->supported_speeds_auto_mode)
		link_info->support_auto_speeds =
			le16_to_cpu(resp->supported_speeds_auto_mode);
	if (resp->supported_pam4_speeds_auto_mode)
		link_info->support_pam4_auto_speeds =
			le16_to_cpu(resp->supported_pam4_speeds_auto_mode);
	if (resp->supported_speeds2_auto_mode)
		link_info->support_auto_speeds2 =
			le16_to_cpu(resp->supported_speeds2_auto_mode);

	bp->port_count = resp->port_cnt;

hwrm_phy_qcaps_exit:
	hwrm_req_drop(bp, req);
	return rc;
}

static void bnxt_hwrm_mac_qcaps(struct bnxt *bp)
{
	struct hwrm_port_mac_qcaps_output *resp;
	struct hwrm_port_mac_qcaps_input *req;
	int rc;

	if (bp->hwrm_spec_code < 0x10a03)
		return;

	rc = hwrm_req_init(bp, req, HWRM_PORT_MAC_QCAPS);
	if (rc)
		return;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send_silent(bp, req);
	if (!rc)
		bp->mac_flags = resp->flags;
	hwrm_req_drop(bp, req);
}

static bool bnxt_support_dropped(u16 advertising, u16 supported)
{
	u16 diff = advertising ^ supported;

	return ((supported | diff) != supported);
}

static bool bnxt_support_speed_dropped(struct bnxt_link_info *link_info)
{
	struct bnxt *bp = container_of(link_info, struct bnxt, link_info);

	/* Check if any advertised speeds are no longer supported. The caller
	 * holds the link_lock mutex, so we can modify link_info settings.
	 */
	if (bp->phy_flags & BNXT_PHY_FL_SPEEDS2) {
		if (bnxt_support_dropped(link_info->advertising,
					 link_info->support_auto_speeds2)) {
			link_info->advertising = link_info->support_auto_speeds2;
			return true;
		}
		return false;
	}
	if (bnxt_support_dropped(link_info->advertising,
				 link_info->support_auto_speeds)) {
		link_info->advertising = link_info->support_auto_speeds;
		return true;
	}
	if (bnxt_support_dropped(link_info->advertising_pam4,
				 link_info->support_pam4_auto_speeds)) {
		link_info->advertising_pam4 = link_info->support_pam4_auto_speeds;
		return true;
	}
	return false;
}

int bnxt_update_link(struct bnxt *bp, bool chng_link_state)
{
	struct bnxt_link_info *link_info = &bp->link_info;
	struct hwrm_port_phy_qcfg_output *resp;
	struct hwrm_port_phy_qcfg_input *req;
	u8 link_state = link_info->link_state;
	bool support_changed;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_PORT_PHY_QCFG);
	if (rc)
		return rc;

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (rc) {
		hwrm_req_drop(bp, req);
		if (BNXT_VF(bp) && rc == -ENODEV) {
			netdev_warn(bp->dev, "Cannot obtain link state while PF unavailable.\n");
			rc = 0;
		}
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
	if (link_info->phy_link_status == BNXT_LINK_LINK) {
		link_info->link_speed = le16_to_cpu(resp->link_speed);
		if (bp->phy_flags & BNXT_PHY_FL_SPEEDS2)
			link_info->active_lanes = resp->active_lanes;
	} else {
		link_info->link_speed = 0;
		link_info->active_lanes = 0;
	}
	link_info->force_link_speed = le16_to_cpu(resp->force_link_speed);
	link_info->force_pam4_link_speed =
		le16_to_cpu(resp->force_pam4_link_speed);
	link_info->force_link_speed2 = le16_to_cpu(resp->force_link_speeds2);
	link_info->support_speeds = le16_to_cpu(resp->support_speeds);
	link_info->support_pam4_speeds = le16_to_cpu(resp->support_pam4_speeds);
	link_info->support_speeds2 = le16_to_cpu(resp->support_speeds2);
	link_info->auto_link_speeds = le16_to_cpu(resp->auto_link_speed_mask);
	link_info->auto_pam4_link_speeds =
		le16_to_cpu(resp->auto_pam4_link_speed_mask);
	link_info->auto_link_speeds2 = le16_to_cpu(resp->auto_link_speeds2);
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

	if (bp->phy_flags & BNXT_PHY_FL_EEE_CAP) {
		struct ethtool_keee *eee = &bp->eee;
		u16 fw_speeds;

		eee->eee_active = 0;
		if (resp->eee_config_phy_addr &
		    PORT_PHY_QCFG_RESP_EEE_CONFIG_EEE_ACTIVE) {
			eee->eee_active = 1;
			fw_speeds = le16_to_cpu(
				resp->link_partner_adv_eee_link_speed_mask);
			_bnxt_fw_to_linkmode(eee->lp_advertised, fw_speeds);
		}

		/* Pull initial EEE config */
		if (!chng_link_state) {
			if (resp->eee_config_phy_addr &
			    PORT_PHY_QCFG_RESP_EEE_CONFIG_EEE_ENABLED)
				eee->eee_enabled = 1;

			fw_speeds = le16_to_cpu(resp->adv_eee_link_speed_mask);
			_bnxt_fw_to_linkmode(eee->advertised, fw_speeds);

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
			link_info->link_state = BNXT_LINK_STATE_UP;
		else
			link_info->link_state = BNXT_LINK_STATE_DOWN;
		if (link_state != link_info->link_state)
			bnxt_report_link(bp);
	} else {
		/* always link down if not require to update link state */
		link_info->link_state = BNXT_LINK_STATE_DOWN;
	}
	hwrm_req_drop(bp, req);

	if (!BNXT_PHY_CFG_ABLE(bp))
		return 0;

	support_changed = bnxt_support_speed_dropped(link_info);
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
		if (bp->phy_flags & BNXT_PHY_FL_SPEEDS2) {
			req->enables |=
				cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_AUTO_LINK_SPEEDS2_MASK);
			req->auto_link_speeds2_mask = cpu_to_le16(bp->link_info.advertising);
		} else if (bp->link_info.advertising) {
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
		if (bp->phy_flags & BNXT_PHY_FL_SPEEDS2) {
			req->force_link_speeds2 = cpu_to_le16(bp->link_info.req_link_speed);
			req->enables |= cpu_to_le32(PORT_PHY_CFG_REQ_ENABLES_FORCE_LINK_SPEEDS2);
			netif_info(bp, link, bp->dev, "Forcing FW speed2: %d\n",
				   (u32)bp->link_info.req_link_speed);
		} else if (bp->link_info.req_signal_mode == BNXT_SIG_MODE_PAM4) {
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
	struct hwrm_port_phy_cfg_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_PORT_PHY_CFG);
	if (rc)
		return rc;

	bnxt_hwrm_set_pause_common(bp, req);

	if ((bp->link_info.autoneg & BNXT_AUTONEG_FLOW_CTRL) ||
	    bp->link_info.force_link_chng)
		bnxt_hwrm_set_link_common(bp, req);

	rc = hwrm_req_send(bp, req);
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
	return rc;
}

static void bnxt_hwrm_set_eee(struct bnxt *bp,
			      struct hwrm_port_phy_cfg_input *req)
{
	struct ethtool_keee *eee = &bp->eee;

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
	struct hwrm_port_phy_cfg_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_PORT_PHY_CFG);
	if (rc)
		return rc;

	if (set_pause)
		bnxt_hwrm_set_pause_common(bp, req);

	bnxt_hwrm_set_link_common(bp, req);

	if (set_eee)
		bnxt_hwrm_set_eee(bp, req);
	return hwrm_req_send(bp, req);
}

static int bnxt_hwrm_shutdown_link(struct bnxt *bp)
{
	struct hwrm_port_phy_cfg_input *req;
	int rc;

	if (!BNXT_SINGLE_PF(bp))
		return 0;

	if (pci_num_vf(bp->pdev) &&
	    !(bp->phy_flags & BNXT_PHY_FL_FW_MANAGED_LKDN))
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_PORT_PHY_CFG);
	if (rc)
		return rc;

	req->flags = cpu_to_le32(PORT_PHY_CFG_REQ_FLAGS_FORCE_LINK_DWN);
	rc = hwrm_req_send(bp, req);
	if (!rc) {
		mutex_lock(&bp->link_lock);
		/* Device is not obliged link down in certain scenarios, even
		 * when forced. Setting the state unknown is consistent with
		 * driver startup and will force link state to be reported
		 * during subsequent open based on PORT_PHY_QCFG.
		 */
		bp->link_info.link_state = BNXT_LINK_STATE_UNKNOWN;
		mutex_unlock(&bp->link_lock);
	}
	return rc;
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

static int bnxt_try_recover_fw(struct bnxt *bp)
{
	if (bp->fw_health && bp->fw_health->status_reliable) {
		int retry = 0, rc;
		u32 sts;

		do {
			sts = bnxt_fw_health_readl(bp, BNXT_FW_HEALTH_REG);
			rc = bnxt_hwrm_poll(bp);
			if (!BNXT_FW_IS_BOOTING(sts) &&
			    !BNXT_FW_IS_RECOVERING(sts))
				break;
			retry++;
		} while (rc == -EBUSY && retry < BNXT_FW_RETRY);

		if (!BNXT_FW_IS_HEALTHY(sts)) {
			netdev_err(bp->dev,
				   "Firmware not responding, status: 0x%x\n",
				   sts);
			rc = -ENODEV;
		}
		if (sts & FW_STATUS_REG_CRASHED_NO_MASTER) {
			netdev_warn(bp->dev, "Firmware recover via OP-TEE requested\n");
			return bnxt_fw_reset_via_optee(bp);
		}
		return rc;
	}

	return -ENODEV;
}

static void bnxt_clear_reservations(struct bnxt *bp, bool fw_reset)
{
	struct bnxt_hw_resc *hw_resc = &bp->hw_resc;

	if (!BNXT_NEW_RM(bp))
		return; /* no resource reservations required */

	hw_resc->resv_cp_rings = 0;
	hw_resc->resv_stat_ctxs = 0;
	hw_resc->resv_irqs = 0;
	hw_resc->resv_tx_rings = 0;
	hw_resc->resv_rx_rings = 0;
	hw_resc->resv_hw_ring_grps = 0;
	hw_resc->resv_vnics = 0;
	hw_resc->resv_rsscos_ctxs = 0;
	if (!fw_reset) {
		bp->tx_nr_rings = 0;
		bp->rx_nr_rings = 0;
	}
}

int bnxt_cancel_reservations(struct bnxt *bp, bool fw_reset)
{
	int rc;

	if (!BNXT_NEW_RM(bp))
		return 0; /* no resource reservations required */

	rc = bnxt_hwrm_func_resc_qcaps(bp, true);
	if (rc)
		netdev_err(bp->dev, "resc_qcaps failed\n");

	bnxt_clear_reservations(bp, fw_reset);

	return rc;
}

static int bnxt_hwrm_if_change(struct bnxt *bp, bool up)
{
	struct hwrm_func_drv_if_change_output *resp;
	struct hwrm_func_drv_if_change_input *req;
	bool fw_reset = !bp->irq_tbl;
	bool resc_reinit = false;
	bool caps_change = false;
	int rc, retry = 0;
	u32 flags = 0;

	if (!(bp->fw_cap & BNXT_FW_CAP_IF_CHANGE))
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_FUNC_DRV_IF_CHANGE);
	if (rc)
		return rc;

	if (up)
		req->flags = cpu_to_le32(FUNC_DRV_IF_CHANGE_REQ_FLAGS_UP);
	resp = hwrm_req_hold(bp, req);

	hwrm_req_flags(bp, req, BNXT_HWRM_FULL_WAIT);
	while (retry < BNXT_FW_IF_RETRY) {
		rc = hwrm_req_send(bp, req);
		if (rc != -EAGAIN)
			break;

		msleep(50);
		retry++;
	}

	if (rc == -EAGAIN) {
		hwrm_req_drop(bp, req);
		return rc;
	} else if (!rc) {
		flags = le32_to_cpu(resp->flags);
	} else if (up) {
		rc = bnxt_try_recover_fw(bp);
		fw_reset = true;
	}
	hwrm_req_drop(bp, req);
	if (rc)
		return rc;

	if (!up) {
		bnxt_inv_fw_health_reg(bp);
		return 0;
	}

	if (flags & FUNC_DRV_IF_CHANGE_RESP_FLAGS_RESC_CHANGE)
		resc_reinit = true;
	if (flags & FUNC_DRV_IF_CHANGE_RESP_FLAGS_HOT_FW_RESET_DONE ||
	    test_bit(BNXT_STATE_FW_RESET_DET, &bp->state))
		fw_reset = true;
	else
		bnxt_remap_fw_health_regs(bp);

	if (test_bit(BNXT_STATE_IN_FW_RESET, &bp->state) && !fw_reset) {
		netdev_err(bp->dev, "RESET_DONE not set during FW reset.\n");
		set_bit(BNXT_STATE_ABORT_ERR, &bp->state);
		return -ENODEV;
	}
	if (flags & FUNC_DRV_IF_CHANGE_RESP_FLAGS_CAPS_CHANGE)
		caps_change = true;

	if (resc_reinit || fw_reset || caps_change) {
		if (fw_reset || caps_change) {
			set_bit(BNXT_STATE_FW_RESET_DET, &bp->state);
			if (!test_bit(BNXT_STATE_IN_FW_RESET, &bp->state))
				bnxt_ulp_irq_stop(bp);
			bnxt_free_ctx_mem(bp, false);
			bnxt_dcb_free(bp);
			rc = bnxt_fw_init_one(bp);
			if (rc) {
				clear_bit(BNXT_STATE_FW_RESET_DET, &bp->state);
				set_bit(BNXT_STATE_ABORT_ERR, &bp->state);
				return rc;
			}
			bnxt_clear_int_mode(bp);
			rc = bnxt_init_int_mode(bp);
			if (rc) {
				clear_bit(BNXT_STATE_FW_RESET_DET, &bp->state);
				netdev_err(bp->dev, "init int mode failed\n");
				return rc;
			}
		}
		rc = bnxt_cancel_reservations(bp, fw_reset);
	}
	return rc;
}

static int bnxt_hwrm_port_led_qcaps(struct bnxt *bp)
{
	struct hwrm_port_led_qcaps_output *resp;
	struct hwrm_port_led_qcaps_input *req;
	struct bnxt_pf_info *pf = &bp->pf;
	int rc;

	bp->num_leds = 0;
	if (BNXT_VF(bp) || bp->hwrm_spec_code < 0x10601)
		return 0;

	rc = hwrm_req_init(bp, req, HWRM_PORT_LED_QCAPS);
	if (rc)
		return rc;

	req->port_id = cpu_to_le16(pf->port_id);
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (rc) {
		hwrm_req_drop(bp, req);
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
	hwrm_req_drop(bp, req);
	return 0;
}

int bnxt_hwrm_alloc_wol_fltr(struct bnxt *bp)
{
	struct hwrm_wol_filter_alloc_output *resp;
	struct hwrm_wol_filter_alloc_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_WOL_FILTER_ALLOC);
	if (rc)
		return rc;

	req->port_id = cpu_to_le16(bp->pf.port_id);
	req->wol_type = WOL_FILTER_ALLOC_REQ_WOL_TYPE_MAGICPKT;
	req->enables = cpu_to_le32(WOL_FILTER_ALLOC_REQ_ENABLES_MAC_ADDRESS);
	memcpy(req->mac_address, bp->dev->dev_addr, ETH_ALEN);

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc)
		bp->wol_filter_id = resp->wol_filter_id;
	hwrm_req_drop(bp, req);
	return rc;
}

int bnxt_hwrm_free_wol_fltr(struct bnxt *bp)
{
	struct hwrm_wol_filter_free_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_WOL_FILTER_FREE);
	if (rc)
		return rc;

	req->port_id = cpu_to_le16(bp->pf.port_id);
	req->enables = cpu_to_le32(WOL_FILTER_FREE_REQ_ENABLES_WOL_FILTER_ID);
	req->wol_filter_id = bp->wol_filter_id;

	return hwrm_req_send(bp, req);
}

static u16 bnxt_hwrm_get_wol_fltrs(struct bnxt *bp, u16 handle)
{
	struct hwrm_wol_filter_qcfg_output *resp;
	struct hwrm_wol_filter_qcfg_input *req;
	u16 next_handle = 0;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_WOL_FILTER_QCFG);
	if (rc)
		return rc;

	req->port_id = cpu_to_le16(bp->pf.port_id);
	req->handle = cpu_to_le16(handle);
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
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
	hwrm_req_drop(bp, req);
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

static bool bnxt_eee_config_ok(struct bnxt *bp)
{
	struct ethtool_keee *eee = &bp->eee;
	struct bnxt_link_info *link_info = &bp->link_info;

	if (!(bp->phy_flags & BNXT_PHY_FL_EEE_CAP))
		return true;

	if (eee->eee_enabled) {
		__ETHTOOL_DECLARE_LINK_MODE_MASK(advertising);
		__ETHTOOL_DECLARE_LINK_MODE_MASK(tmp);

		_bnxt_fw_to_linkmode(advertising, link_info->advertising);

		if (!(link_info->autoneg & BNXT_AUTONEG_SPEED)) {
			eee->eee_enabled = 0;
			return false;
		}
		if (linkmode_andnot(tmp, eee->advertised, advertising)) {
			linkmode_and(eee->advertised, advertising,
				     eee->supported);
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
		if (bnxt_force_speed_updated(link_info))
			update_link = true;
		if (link_info->req_duplex != link_info->duplex_setting)
			update_link = true;
	} else {
		if (link_info->auto_mode == BNXT_LINK_AUTO_NONE)
			update_link = true;
		if (bnxt_auto_speed_updated(link_info))
			update_link = true;
	}

	/* The last close may have shutdown the link, so need to call
	 * PHY_CFG to bring it back up.
	 */
	if (!BNXT_LINK_IS_UP(bp))
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

static int bnxt_init_dflt_ring_mode(struct bnxt *bp);

static int bnxt_reinit_after_abort(struct bnxt *bp)
{
	int rc;

	if (test_bit(BNXT_STATE_IN_FW_RESET, &bp->state))
		return -EBUSY;

	if (bp->dev->reg_state == NETREG_UNREGISTERED)
		return -ENODEV;

	rc = bnxt_fw_init_one(bp);
	if (!rc) {
		bnxt_clear_int_mode(bp);
		rc = bnxt_init_int_mode(bp);
		if (!rc) {
			clear_bit(BNXT_STATE_ABORT_ERR, &bp->state);
			set_bit(BNXT_STATE_FW_RESET_DET, &bp->state);
		}
	}
	return rc;
}

static void bnxt_cfg_one_usr_fltr(struct bnxt *bp, struct bnxt_filter_base *fltr)
{
	struct bnxt_ntuple_filter *ntp_fltr;
	struct bnxt_l2_filter *l2_fltr;

	if (list_empty(&fltr->list))
		return;

	if (fltr->type == BNXT_FLTR_TYPE_NTUPLE) {
		ntp_fltr = container_of(fltr, struct bnxt_ntuple_filter, base);
		l2_fltr = bp->vnic_info[BNXT_VNIC_DEFAULT].l2_filters[0];
		atomic_inc(&l2_fltr->refcnt);
		ntp_fltr->l2_fltr = l2_fltr;
		if (bnxt_hwrm_cfa_ntuple_filter_alloc(bp, ntp_fltr)) {
			bnxt_del_ntp_filter(bp, ntp_fltr);
			netdev_err(bp->dev, "restoring previously configured ntuple filter id %d failed\n",
				   fltr->sw_id);
		}
	} else if (fltr->type == BNXT_FLTR_TYPE_L2) {
		l2_fltr = container_of(fltr, struct bnxt_l2_filter, base);
		if (bnxt_hwrm_l2_filter_alloc(bp, l2_fltr)) {
			bnxt_del_l2_filter(bp, l2_fltr);
			netdev_err(bp->dev, "restoring previously configured l2 filter id %d failed\n",
				   fltr->sw_id);
		}
	}
}

static void bnxt_cfg_usr_fltrs(struct bnxt *bp)
{
	struct bnxt_filter_base *usr_fltr, *tmp;

	list_for_each_entry_safe(usr_fltr, tmp, &bp->usr_fltr_list, list)
		bnxt_cfg_one_usr_fltr(bp, usr_fltr);
}

static int bnxt_set_xps_mapping(struct bnxt *bp)
{
	int numa_node = dev_to_node(&bp->pdev->dev);
	unsigned int q_idx, map_idx, cpu, i;
	const struct cpumask *cpu_mask_ptr;
	int nr_cpus = num_online_cpus();
	cpumask_t *q_map;
	int rc = 0;

	q_map = kcalloc(bp->tx_nr_rings_per_tc, sizeof(*q_map), GFP_KERNEL);
	if (!q_map)
		return -ENOMEM;

	/* Create CPU mask for all TX queues across MQPRIO traffic classes.
	 * Each TC has the same number of TX queues. The nth TX queue for each
	 * TC will have the same CPU mask.
	 */
	for (i = 0; i < nr_cpus; i++) {
		map_idx = i % bp->tx_nr_rings_per_tc;
		cpu = cpumask_local_spread(i, numa_node);
		cpu_mask_ptr = get_cpu_mask(cpu);
		cpumask_or(&q_map[map_idx], &q_map[map_idx], cpu_mask_ptr);
	}

	/* Register CPU mask for each TX queue except the ones marked for XDP */
	for (q_idx = 0; q_idx < bp->dev->real_num_tx_queues; q_idx++) {
		map_idx = q_idx % bp->tx_nr_rings_per_tc;
		rc = netif_set_xps_queue(bp->dev, &q_map[map_idx], q_idx);
		if (rc) {
			netdev_warn(bp->dev, "Error setting XPS for q:%d\n",
				    q_idx);
			break;
		}
	}

	kfree(q_map);

	return rc;
}

static int __bnxt_open_nic(struct bnxt *bp, bool irq_re_init, bool link_re_init)
{
	int rc = 0;

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

	if (irq_re_init) {
		udp_tunnel_nic_reset_ntf(bp->dev);
		rc = bnxt_set_xps_mapping(bp);
		if (rc)
			netdev_warn(bp->dev, "failed to set xps mapping\n");
	}

	if (bp->tx_nr_rings_xdp < num_possible_cpus()) {
		if (!static_key_enabled(&bnxt_xdp_locking_key))
			static_branch_enable(&bnxt_xdp_locking_key);
	} else if (static_key_enabled(&bnxt_xdp_locking_key)) {
		static_branch_disable(&bnxt_xdp_locking_key);
	}
	set_bit(BNXT_STATE_OPEN, &bp->state);
	bnxt_enable_int(bp);
	/* Enable TX queues */
	bnxt_tx_enable(bp);
	mod_timer(&bp->timer, jiffies + bp->current_interval);
	/* Poll link status and check for SFP+ module status */
	mutex_lock(&bp->link_lock);
	bnxt_get_port_module_status(bp);
	mutex_unlock(&bp->link_lock);

	/* VF-reps may need to be re-opened after the PF is re-opened */
	if (BNXT_PF(bp))
		bnxt_vf_reps_open(bp);
	if (bp->ptp_cfg && !(bp->fw_cap & BNXT_FW_CAP_TX_TS_CMP))
		WRITE_ONCE(bp->ptp_cfg->tx_avail, BNXT_MAX_TX_TS);
	bnxt_ptp_init_rtc(bp, true);
	bnxt_ptp_cfg_tstamp_filters(bp);
	if (BNXT_SUPPORTS_MULTI_RSS_CTX(bp))
		bnxt_hwrm_realloc_rss_ctx_vnic(bp);
	bnxt_cfg_usr_fltrs(bp);
	return 0;

open_err_irq:
	bnxt_del_napi(bp);

open_err_free_mem:
	bnxt_free_skbs(bp);
	bnxt_free_irq(bp);
	bnxt_free_mem(bp, true);
	return rc;
}

int bnxt_open_nic(struct bnxt *bp, bool irq_re_init, bool link_re_init)
{
	int rc = 0;

	if (test_bit(BNXT_STATE_ABORT_ERR, &bp->state))
		rc = -EIO;
	if (!rc)
		rc = __bnxt_open_nic(bp, irq_re_init, link_re_init);
	if (rc) {
		netdev_err(bp->dev, "nic open fail (rc: %x)\n", rc);
		netif_close(bp->dev);
	}
	return rc;
}

/* netdev instance lock held, open the NIC half way by allocating all
 * resources, but NAPI, IRQ, and TX are not enabled.  This is mainly used
 * for offline self tests.
 */
int bnxt_half_open_nic(struct bnxt *bp)
{
	int rc = 0;

	if (test_bit(BNXT_STATE_ABORT_ERR, &bp->state)) {
		netdev_err(bp->dev, "A previous firmware reset has not completed, aborting half open\n");
		rc = -ENODEV;
		goto half_open_err;
	}

	rc = bnxt_alloc_mem(bp, true);
	if (rc) {
		netdev_err(bp->dev, "bnxt_alloc_mem err: %x\n", rc);
		goto half_open_err;
	}
	bnxt_init_napi(bp);
	set_bit(BNXT_STATE_HALF_OPEN, &bp->state);
	rc = bnxt_init_nic(bp, true);
	if (rc) {
		clear_bit(BNXT_STATE_HALF_OPEN, &bp->state);
		bnxt_del_napi(bp);
		netdev_err(bp->dev, "bnxt_init_nic err: %x\n", rc);
		goto half_open_err;
	}
	return 0;

half_open_err:
	bnxt_free_skbs(bp);
	bnxt_free_mem(bp, true);
	netif_close(bp->dev);
	return rc;
}

/* netdev instance lock held, this call can only be made after a previous
 * successful call to bnxt_half_open_nic().
 */
void bnxt_half_close_nic(struct bnxt *bp)
{
	bnxt_hwrm_resource_free(bp, false, true);
	bnxt_del_napi(bp);
	bnxt_free_skbs(bp);
	bnxt_free_mem(bp, true);
	clear_bit(BNXT_STATE_HALF_OPEN, &bp->state);
}

void bnxt_reenable_sriov(struct bnxt *bp)
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
		rc = bnxt_reinit_after_abort(bp);
		if (rc) {
			if (rc == -EBUSY)
				netdev_err(bp->dev, "A previous firmware reset has not completed, aborting\n");
			else
				netdev_err(bp->dev, "Failed to reinitialize after aborted firmware reset\n");
			return -ENODEV;
		}
	}

	rc = bnxt_hwrm_if_change(bp, true);
	if (rc)
		return rc;

	rc = __bnxt_open_nic(bp, true, true);
	if (rc) {
		bnxt_hwrm_if_change(bp, false);
	} else {
		if (test_and_clear_bit(BNXT_STATE_FW_RESET_DET, &bp->state)) {
			if (!test_bit(BNXT_STATE_IN_FW_RESET, &bp->state))
				bnxt_queue_sp_work(bp,
						   BNXT_RESTART_ULP_SP_EVENT);
		}
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

	if (BNXT_SUPPORTS_MULTI_RSS_CTX(bp))
		bnxt_clear_rss_ctxs(bp);
	/* Flush rings and disable interrupts */
	bnxt_shutdown_nic(bp, irq_re_init);

	/* TODO CHIMP_FW: Link/PHY related cleanup if (link_re_init) */

	bnxt_debug_dev_exit(bp);
	bnxt_disable_napi(bp);
	timer_delete_sync(&bp->timer);
	bnxt_free_skbs(bp);

	/* Save ring stats before shutdown */
	if (bp->bnapi && irq_re_init) {
		bnxt_get_ring_stats(bp, &bp->net_stats_prev);
		bnxt_get_ring_err_stats(bp, &bp->ring_err_stats_prev);
	}
	if (irq_re_init) {
		bnxt_free_irq(bp);
		bnxt_del_napi(bp);
	}
	bnxt_free_mem(bp, irq_re_init);
}

void bnxt_close_nic(struct bnxt *bp, bool irq_re_init, bool link_re_init)
{
	if (test_bit(BNXT_STATE_IN_FW_RESET, &bp->state)) {
		/* If we get here, it means firmware reset is in progress
		 * while we are trying to close.  We can safely proceed with
		 * the close because we are holding netdev instance lock.
		 * Some firmware messages may fail as we proceed to close.
		 * We set the ABORT_ERR flag here so that the FW reset thread
		 * will later abort when it gets the netdev instance lock
		 * and sees the flag.
		 */
		netdev_warn(bp->dev, "FW reset in progress during close, FW reset will be aborted\n");
		set_bit(BNXT_STATE_ABORT_ERR, &bp->state);
	}

#ifdef CONFIG_BNXT_SRIOV
	if (bp->sriov_cfg) {
		int rc;

		rc = wait_event_interruptible_timeout(bp->sriov_cfg_wait,
						      !bp->sriov_cfg,
						      BNXT_SRIOV_CFG_WAIT_TMO);
		if (!rc)
			netdev_warn(bp->dev, "timeout waiting for SRIOV config operation to complete, proceeding to close!\n");
		else if (rc < 0)
			netdev_warn(bp->dev, "SRIOV config operation interrupted, proceeding to close!\n");
	}
#endif
	__bnxt_close_nic(bp, irq_re_init, link_re_init);
}

static int bnxt_close(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);

	bnxt_close_nic(bp, true, true);
	bnxt_hwrm_shutdown_link(bp);
	bnxt_hwrm_if_change(bp, false);
	return 0;
}

static int bnxt_hwrm_port_phy_read(struct bnxt *bp, u16 phy_addr, u16 reg,
				   u16 *val)
{
	struct hwrm_port_phy_mdio_read_output *resp;
	struct hwrm_port_phy_mdio_read_input *req;
	int rc;

	if (bp->hwrm_spec_code < 0x10a00)
		return -EOPNOTSUPP;

	rc = hwrm_req_init(bp, req, HWRM_PORT_PHY_MDIO_READ);
	if (rc)
		return rc;

	req->port_id = cpu_to_le16(bp->pf.port_id);
	req->phy_addr = phy_addr;
	req->reg_addr = cpu_to_le16(reg & 0x1f);
	if (mdio_phy_id_is_c45(phy_addr)) {
		req->cl45_mdio = 1;
		req->phy_addr = mdio_phy_id_prtad(phy_addr);
		req->dev_addr = mdio_phy_id_devad(phy_addr);
		req->reg_addr = cpu_to_le16(reg);
	}

	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc)
		*val = le16_to_cpu(resp->reg_data);
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_hwrm_port_phy_write(struct bnxt *bp, u16 phy_addr, u16 reg,
				    u16 val)
{
	struct hwrm_port_phy_mdio_write_input *req;
	int rc;

	if (bp->hwrm_spec_code < 0x10a00)
		return -EOPNOTSUPP;

	rc = hwrm_req_init(bp, req, HWRM_PORT_PHY_MDIO_WRITE);
	if (rc)
		return rc;

	req->port_id = cpu_to_le16(bp->pf.port_id);
	req->phy_addr = phy_addr;
	req->reg_addr = cpu_to_le16(reg & 0x1f);
	if (mdio_phy_id_is_c45(phy_addr)) {
		req->cl45_mdio = 1;
		req->phy_addr = mdio_phy_id_prtad(phy_addr);
		req->dev_addr = mdio_phy_id_devad(phy_addr);
		req->reg_addr = cpu_to_le16(reg);
	}
	req->reg_data = cpu_to_le16(val);

	return hwrm_req_send(bp, req);
}

/* netdev instance lock held */
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

	case SIOCSHWTSTAMP:
		return bnxt_hwtstamp_set(dev, ifr);

	case SIOCGHWTSTAMP:
		return bnxt_hwtstamp_get(dev, ifr);

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

		stats->rx_dropped +=
			cpr->sw_stats->rx.rx_netpoll_discards +
			cpr->sw_stats->rx.rx_oom_discards;
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
	stats->rx_dropped += prev_stats->rx_dropped;
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

static void bnxt_get_one_ring_err_stats(struct bnxt *bp,
					struct bnxt_total_ring_err_stats *stats,
					struct bnxt_cp_ring_info *cpr)
{
	struct bnxt_sw_stats *sw_stats = cpr->sw_stats;
	u64 *hw_stats = cpr->stats.sw_stats;

	stats->rx_total_l4_csum_errors += sw_stats->rx.rx_l4_csum_errors;
	stats->rx_total_resets += sw_stats->rx.rx_resets;
	stats->rx_total_buf_errors += sw_stats->rx.rx_buf_errors;
	stats->rx_total_oom_discards += sw_stats->rx.rx_oom_discards;
	stats->rx_total_netpoll_discards += sw_stats->rx.rx_netpoll_discards;
	stats->rx_total_ring_discards +=
		BNXT_GET_RING_STATS64(hw_stats, rx_discard_pkts);
	stats->tx_total_resets += sw_stats->tx.tx_resets;
	stats->tx_total_ring_discards +=
		BNXT_GET_RING_STATS64(hw_stats, tx_discard_pkts);
	stats->total_missed_irqs += sw_stats->cmn.missed_irqs;
}

void bnxt_get_ring_err_stats(struct bnxt *bp,
			     struct bnxt_total_ring_err_stats *stats)
{
	int i;

	for (i = 0; i < bp->cp_nr_rings; i++)
		bnxt_get_one_ring_err_stats(bp, stats, &bp->bnapi[i]->cp_ring);
}

static bool bnxt_mc_list_updated(struct bnxt *bp, u32 *rx_mask)
{
	struct bnxt_vnic_info *vnic = &bp->vnic_info[BNXT_VNIC_DEFAULT];
	struct net_device *dev = bp->dev;
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
	struct bnxt_vnic_info *vnic = &bp->vnic_info[BNXT_VNIC_DEFAULT];
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

	vnic = &bp->vnic_info[BNXT_VNIC_DEFAULT];
	mask = vnic->rx_mask;
	mask &= ~(CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS |
		  CFA_L2_SET_RX_MASK_REQ_MASK_MCAST |
		  CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST |
		  CFA_L2_SET_RX_MASK_REQ_MASK_BCAST);

	if (dev->flags & IFF_PROMISC)
		mask |= CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS;

	uc_update = bnxt_uc_list_updated(bp);

	if (dev->flags & IFF_BROADCAST)
		mask |= CFA_L2_SET_RX_MASK_REQ_MASK_BCAST;
	if (dev->flags & IFF_ALLMULTI) {
		mask |= CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST;
		vnic->mc_list_count = 0;
	} else if (dev->flags & IFF_MULTICAST) {
		mc_update = bnxt_mc_list_updated(bp, &mask);
	}

	if (mask != vnic->rx_mask || uc_update || mc_update) {
		vnic->rx_mask = mask;

		bnxt_queue_sp_work(bp, BNXT_RX_MASK_SP_EVENT);
	}
}

static int bnxt_cfg_rx_mode(struct bnxt *bp)
{
	struct net_device *dev = bp->dev;
	struct bnxt_vnic_info *vnic = &bp->vnic_info[BNXT_VNIC_DEFAULT];
	struct netdev_hw_addr *ha;
	int i, off = 0, rc;
	bool uc_update;

	netif_addr_lock_bh(dev);
	uc_update = bnxt_uc_list_updated(bp);
	netif_addr_unlock_bh(dev);

	if (!uc_update)
		goto skip_uc;

	for (i = 1; i < vnic->uc_filter_count; i++) {
		struct bnxt_l2_filter *fltr = vnic->l2_filters[i];

		bnxt_hwrm_l2_filter_free(bp, fltr);
		bnxt_del_l2_filter(bp, fltr);
	}

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
			if (BNXT_VF(bp) && rc == -ENODEV) {
				if (!test_and_set_bit(BNXT_STATE_L2_FILTER_RETRY, &bp->state))
					netdev_warn(bp->dev, "Cannot configure L2 filters while PF is unavailable, will retry\n");
				else
					netdev_dbg(bp->dev, "PF still unavailable while configuring L2 filters.\n");
				rc = 0;
			} else {
				netdev_err(bp->dev, "HWRM vnic filter failure rc: %x\n", rc);
			}
			vnic->uc_filter_count = i;
			return rc;
		}
	}
	if (test_and_clear_bit(BNXT_STATE_L2_FILTER_RETRY, &bp->state))
		netdev_notice(bp->dev, "Retry of L2 filter configuration successful.\n");

skip_uc:
	if ((vnic->rx_mask & CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS) &&
	    !bnxt_promisc_ok(bp))
		vnic->rx_mask &= ~CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS;
	rc = bnxt_hwrm_cfa_l2_set_rx_mask(bp, 0);
	if (rc && (vnic->rx_mask & CFA_L2_SET_RX_MASK_REQ_MASK_MCAST)) {
		netdev_info(bp->dev, "Failed setting MC filters rc: %d, turning on ALL_MCAST mode\n",
			    rc);
		vnic->rx_mask &= ~CFA_L2_SET_RX_MASK_REQ_MASK_MCAST;
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
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
		if (bp->fw_cap & BNXT_FW_CAP_CFA_RFS_RING_TBL_IDX_V2)
			return true;
		return false;
	}
	/* 212 firmware is broken for aRFS */
	if (BNXT_FW_MAJ(bp) == 212)
		return false;
	if (BNXT_PF(bp) && !BNXT_CHIP_TYPE_NITRO_A0(bp))
		return true;
	if (bp->rss_cap & BNXT_RSS_CAP_NEW_RSS_CAP)
		return true;
	return false;
}

/* If runtime conditions support RFS */
bool bnxt_rfs_capable(struct bnxt *bp, bool new_rss_ctx)
{
	struct bnxt_hw_rings hwr = {0};
	int max_vnics, max_rss_ctxs;

	if ((bp->flags & BNXT_FLAG_CHIP_P5_PLUS) &&
	    !BNXT_SUPPORTS_NTUPLE_VNIC(bp))
		return bnxt_rfs_supported(bp);

	if (!bnxt_can_reserve_rings(bp) || !bp->rx_nr_rings)
		return false;

	hwr.grp = bp->rx_nr_rings;
	hwr.vnic = bnxt_get_total_vnics(bp, bp->rx_nr_rings);
	if (new_rss_ctx)
		hwr.vnic++;
	hwr.rss_ctx = bnxt_get_total_rss_ctxs(bp, &hwr);
	max_vnics = bnxt_get_max_func_vnics(bp);
	max_rss_ctxs = bnxt_get_max_func_rss_ctxs(bp);

	if (hwr.vnic > max_vnics || hwr.rss_ctx > max_rss_ctxs) {
		if (bp->rx_nr_rings > 1)
			netdev_warn(bp->dev,
				    "Not enough resources to support NTUPLE filters, enough resources for up to %d rx rings\n",
				    min(max_rss_ctxs - 1, max_vnics - 1));
		return false;
	}

	if (!BNXT_NEW_RM(bp))
		return true;

	/* Do not reduce VNIC and RSS ctx reservations.  There is a FW
	 * issue that will mess up the default VNIC if we reduce the
	 * reservations.
	 */
	if (hwr.vnic <= bp->hw_resc.resv_vnics &&
	    hwr.rss_ctx <= bp->hw_resc.resv_rsscos_ctxs)
		return true;

	bnxt_hwrm_reserve_rings(bp, &hwr);
	if (hwr.vnic <= bp->hw_resc.resv_vnics &&
	    hwr.rss_ctx <= bp->hw_resc.resv_rsscos_ctxs)
		return true;

	netdev_warn(bp->dev, "Unable to reserve resources to support NTUPLE filters.\n");
	hwr.vnic = 1;
	hwr.rss_ctx = 0;
	bnxt_hwrm_reserve_rings(bp, &hwr);
	return false;
}

static netdev_features_t bnxt_fix_features(struct net_device *dev,
					   netdev_features_t features)
{
	struct bnxt *bp = netdev_priv(dev);
	netdev_features_t vlan_features;

	if ((features & NETIF_F_NTUPLE) && !bnxt_rfs_capable(bp, false))
		features &= ~NETIF_F_NTUPLE;

	if ((bp->flags & BNXT_FLAG_NO_AGG_RINGS) || bp->xdp_prog)
		features &= ~(NETIF_F_LRO | NETIF_F_GRO_HW);

	if (!(features & NETIF_F_GRO))
		features &= ~NETIF_F_GRO_HW;

	if (features & NETIF_F_GRO_HW)
		features &= ~NETIF_F_LRO;

	/* Both CTAG and STAG VLAN acceleration on the RX side have to be
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

static int bnxt_reinit_features(struct bnxt *bp, bool irq_re_init,
				bool link_re_init, u32 flags, bool update_tpa)
{
	bnxt_close_nic(bp, irq_re_init, link_re_init);
	bp->flags = flags;
	if (update_tpa)
		bnxt_set_ring_params(bp);
	return bnxt_open_nic(bp, irq_re_init, link_re_init);
}

static int bnxt_set_features(struct net_device *dev, netdev_features_t features)
{
	bool update_tpa = false, update_ntuple = false;
	struct bnxt *bp = netdev_priv(dev);
	u32 flags = bp->flags;
	u32 changes;
	int rc = 0;
	bool re_init = false;

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
	else
		bnxt_clear_usr_fltrs(bp, true);

	changes = flags ^ bp->flags;
	if (changes & BNXT_FLAG_TPA) {
		update_tpa = true;
		if ((bp->flags & BNXT_FLAG_TPA) == 0 ||
		    (flags & BNXT_FLAG_TPA) == 0 ||
		    (bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
			re_init = true;
	}

	if (changes & ~BNXT_FLAG_TPA)
		re_init = true;

	if (changes & BNXT_FLAG_RFS)
		update_ntuple = true;

	if (flags != bp->flags) {
		u32 old_flags = bp->flags;

		if (!test_bit(BNXT_STATE_OPEN, &bp->state)) {
			bp->flags = flags;
			if (update_tpa)
				bnxt_set_ring_params(bp);
			return rc;
		}

		if (update_ntuple)
			return bnxt_reinit_features(bp, true, false, flags, update_tpa);

		if (re_init)
			return bnxt_reinit_features(bp, false, false, flags, update_tpa);

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

static bool bnxt_exthdr_check(struct bnxt *bp, struct sk_buff *skb, int nw_off,
			      u8 **nextp)
{
	struct ipv6hdr *ip6h = (struct ipv6hdr *)(skb->data + nw_off);
	struct hop_jumbo_hdr *jhdr;
	int hdr_count = 0;
	u8 *nexthdr;
	int start;

	/* Check that there are at most 2 IPv6 extension headers, no
	 * fragment header, and each is <= 64 bytes.
	 */
	start = nw_off + sizeof(*ip6h);
	nexthdr = &ip6h->nexthdr;
	while (ipv6_ext_hdr(*nexthdr)) {
		struct ipv6_opt_hdr *hp;
		int hdrlen;

		if (hdr_count >= 3 || *nexthdr == NEXTHDR_NONE ||
		    *nexthdr == NEXTHDR_FRAGMENT)
			return false;
		hp = __skb_header_pointer(NULL, start, sizeof(*hp), skb->data,
					  skb_headlen(skb), NULL);
		if (!hp)
			return false;
		if (*nexthdr == NEXTHDR_AUTH)
			hdrlen = ipv6_authlen(hp);
		else
			hdrlen = ipv6_optlen(hp);

		if (hdrlen > 64)
			return false;

		/* The ext header may be a hop-by-hop header inserted for
		 * big TCP purposes. This will be removed before sending
		 * from NIC, so do not count it.
		 */
		if (*nexthdr == NEXTHDR_HOP) {
			if (likely(skb->len <= GRO_LEGACY_MAX_SIZE))
				goto increment_hdr;

			jhdr = (struct hop_jumbo_hdr *)hp;
			if (jhdr->tlv_type != IPV6_TLV_JUMBO || jhdr->hdrlen != 0 ||
			    jhdr->nexthdr != IPPROTO_TCP)
				goto increment_hdr;

			goto next_hdr;
		}
increment_hdr:
		hdr_count++;
next_hdr:
		nexthdr = &hp->nexthdr;
		start += hdrlen;
	}
	if (nextp) {
		/* Caller will check inner protocol */
		if (skb->encapsulation) {
			*nextp = nexthdr;
			return true;
		}
		*nextp = NULL;
	}
	/* Only support TCP/UDP for non-tunneled ipv6 and inner ipv6 */
	return *nexthdr == IPPROTO_TCP || *nexthdr == IPPROTO_UDP;
}

/* For UDP, we can only handle 1 Vxlan port and 1 Geneve port. */
static bool bnxt_udp_tunl_check(struct bnxt *bp, struct sk_buff *skb)
{
	struct udphdr *uh = udp_hdr(skb);
	__be16 udp_port = uh->dest;

	if (udp_port != bp->vxlan_port && udp_port != bp->nge_port &&
	    udp_port != bp->vxlan_gpe_port)
		return false;
	if (skb->inner_protocol == htons(ETH_P_TEB)) {
		struct ethhdr *eh = inner_eth_hdr(skb);

		switch (eh->h_proto) {
		case htons(ETH_P_IP):
			return true;
		case htons(ETH_P_IPV6):
			return bnxt_exthdr_check(bp, skb,
						 skb_inner_network_offset(skb),
						 NULL);
		}
	} else if (skb->inner_protocol == htons(ETH_P_IP)) {
		return true;
	} else if (skb->inner_protocol == htons(ETH_P_IPV6)) {
		return bnxt_exthdr_check(bp, skb, skb_inner_network_offset(skb),
					 NULL);
	}
	return false;
}

static bool bnxt_tunl_check(struct bnxt *bp, struct sk_buff *skb, u8 l4_proto)
{
	switch (l4_proto) {
	case IPPROTO_UDP:
		return bnxt_udp_tunl_check(bp, skb);
	case IPPROTO_IPIP:
		return true;
	case IPPROTO_GRE: {
		switch (skb->inner_protocol) {
		default:
			return false;
		case htons(ETH_P_IP):
			return true;
		case htons(ETH_P_IPV6):
			fallthrough;
		}
	}
	case IPPROTO_IPV6:
		/* Check ext headers of inner ipv6 */
		return bnxt_exthdr_check(bp, skb, skb_inner_network_offset(skb),
					 NULL);
	}
	return false;
}

static netdev_features_t bnxt_features_check(struct sk_buff *skb,
					     struct net_device *dev,
					     netdev_features_t features)
{
	struct bnxt *bp = netdev_priv(dev);
	u8 *l4_proto;

	features = vlan_features_check(skb, features);
	switch (vlan_get_protocol(skb)) {
	case htons(ETH_P_IP):
		if (!skb->encapsulation)
			return features;
		l4_proto = &ip_hdr(skb)->protocol;
		if (bnxt_tunl_check(bp, skb, *l4_proto))
			return features;
		break;
	case htons(ETH_P_IPV6):
		if (!bnxt_exthdr_check(bp, skb, skb_network_offset(skb),
				       &l4_proto))
			break;
		if (!l4_proto || bnxt_tunl_check(bp, skb, *l4_proto))
			return features;
		break;
	}
	return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
}

int bnxt_dbg_hwrm_rd_reg(struct bnxt *bp, u32 reg_off, u16 num_words,
			 u32 *reg_buf)
{
	struct hwrm_dbg_read_direct_output *resp;
	struct hwrm_dbg_read_direct_input *req;
	__le32 *dbg_reg_buf;
	dma_addr_t mapping;
	int rc, i;

	rc = hwrm_req_init(bp, req, HWRM_DBG_READ_DIRECT);
	if (rc)
		return rc;

	dbg_reg_buf = hwrm_req_dma_slice(bp, req, num_words * 4,
					 &mapping);
	if (!dbg_reg_buf) {
		rc = -ENOMEM;
		goto dbg_rd_reg_exit;
	}

	req->host_dest_addr = cpu_to_le64(mapping);

	resp = hwrm_req_hold(bp, req);
	req->read_addr = cpu_to_le32(reg_off + CHIMP_REG_VIEW_ADDR);
	req->read_len32 = cpu_to_le32(num_words);

	rc = hwrm_req_send(bp, req);
	if (rc || resp->error_code) {
		rc = -EIO;
		goto dbg_rd_reg_exit;
	}
	for (i = 0; i < num_words; i++)
		reg_buf[i] = le32_to_cpu(dbg_reg_buf[i]);

dbg_rd_reg_exit:
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_dbg_hwrm_ring_info_get(struct bnxt *bp, u8 ring_type,
				       u32 ring_id, u32 *prod, u32 *cons)
{
	struct hwrm_dbg_ring_info_get_output *resp;
	struct hwrm_dbg_ring_info_get_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_DBG_RING_INFO_GET);
	if (rc)
		return rc;

	req->ring_type = ring_type;
	req->fw_ring_id = cpu_to_le32(ring_id);
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc) {
		*prod = le32_to_cpu(resp->producer_index);
		*cons = le32_to_cpu(resp->consumer_index);
	}
	hwrm_req_drop(bp, req);
	return rc;
}

static void bnxt_dump_tx_sw_state(struct bnxt_napi *bnapi)
{
	struct bnxt_tx_ring_info *txr;
	int i = bnapi->index, j;

	bnxt_for_each_napi_tx(j, bnapi, txr)
		netdev_info(bnapi->bp->dev, "[%d.%d]: tx{fw_ring: %d prod: %x cons: %x}\n",
			    i, j, txr->tx_ring_struct.fw_ring_id, txr->tx_prod,
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
	struct hwrm_ring_reset_input *req;
	struct bnxt_napi *bnapi = rxr->bnapi;
	struct bnxt_cp_ring_info *cpr;
	u16 cp_ring_id;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_RING_RESET);
	if (rc)
		return rc;

	cpr = &bnapi->cp_ring;
	cp_ring_id = cpr->cp_ring_struct.fw_ring_id;
	req->cmpl_ring = cpu_to_le16(cp_ring_id);
	req->ring_type = RING_RESET_REQ_RING_TYPE_RX_RING_GRP;
	req->ring_id = cpu_to_le16(bp->grp_info[bnapi->index].fw_grp_id);
	return hwrm_req_send_silent(bp, req);
}

static void bnxt_reset_task(struct bnxt *bp, bool silent)
{
	if (!silent)
		bnxt_dbg_dump_states(bp);
	if (netif_running(bp->dev)) {
		bnxt_close_nic(bp, !silent, false);
		bnxt_open_nic(bp, !silent, false);
	}
}

static void bnxt_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct bnxt *bp = netdev_priv(dev);

	netdev_err(bp->dev,  "TX timeout detected, starting reset task!\n");
	bnxt_queue_sp_work(bp, BNXT_RESET_TASK_SP_EVENT);
}

static void bnxt_fw_health_check(struct bnxt *bp)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	struct pci_dev *pdev = bp->pdev;
	u32 val;

	if (!fw_health->enabled || test_bit(BNXT_STATE_IN_FW_RESET, &bp->state))
		return;

	/* Make sure it is enabled before checking the tmr_counter. */
	smp_rmb();
	if (fw_health->tmr_counter) {
		fw_health->tmr_counter--;
		return;
	}

	val = bnxt_fw_health_readl(bp, BNXT_FW_HEARTBEAT_REG);
	if (val == fw_health->last_fw_heartbeat && pci_device_is_present(pdev)) {
		fw_health->arrests++;
		goto fw_reset;
	}

	fw_health->last_fw_heartbeat = val;

	val = bnxt_fw_health_readl(bp, BNXT_FW_RESET_CNT_REG);
	if (val != fw_health->last_fw_reset_cnt && pci_device_is_present(pdev)) {
		fw_health->discoveries++;
		goto fw_reset;
	}

	fw_health->tmr_counter = fw_health->tmr_multiplier;
	return;

fw_reset:
	bnxt_queue_sp_work(bp, BNXT_FW_EXCEPTION_SP_EVENT);
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

	if (BNXT_LINK_IS_UP(bp) && bp->stats_coal_ticks)
		bnxt_queue_sp_work(bp, BNXT_PERIODIC_STATS_SP_EVENT);

	if (bnxt_tc_flower_enabled(bp))
		bnxt_queue_sp_work(bp, BNXT_FLOW_STATS_SP_EVENT);

#ifdef CONFIG_RFS_ACCEL
	if ((bp->flags & BNXT_FLAG_RFS) && bp->ntp_fltr_count)
		bnxt_queue_sp_work(bp, BNXT_RX_NTP_FLTR_SP_EVENT);
#endif /*CONFIG_RFS_ACCEL*/

	if (bp->link_info.phy_retry) {
		if (time_after(jiffies, bp->link_info.phy_retry_expires)) {
			bp->link_info.phy_retry = false;
			netdev_warn(bp->dev, "failed to update phy settings after maximum retries.\n");
		} else {
			bnxt_queue_sp_work(bp, BNXT_UPDATE_PHY_SP_EVENT);
		}
	}

	if (test_bit(BNXT_STATE_L2_FILTER_RETRY, &bp->state))
		bnxt_queue_sp_work(bp, BNXT_RX_MASK_SP_EVENT);

	if ((BNXT_CHIP_P5(bp)) && !bp->chip_rev && netif_carrier_ok(dev))
		bnxt_queue_sp_work(bp, BNXT_RING_COAL_NOW_SP_EVENT);

bnxt_restart_timer:
	mod_timer(&bp->timer, jiffies + bp->current_interval);
}

static void bnxt_lock_sp(struct bnxt *bp)
{
	/* We are called from bnxt_sp_task which has BNXT_STATE_IN_SP_TASK
	 * set.  If the device is being closed, bnxt_close() may be holding
	 * netdev instance lock and waiting for BNXT_STATE_IN_SP_TASK to clear.
	 * So we must clear BNXT_STATE_IN_SP_TASK before holding netdev
	 * instance lock.
	 */
	clear_bit(BNXT_STATE_IN_SP_TASK, &bp->state);
	netdev_lock(bp->dev);
}

static void bnxt_unlock_sp(struct bnxt *bp)
{
	set_bit(BNXT_STATE_IN_SP_TASK, &bp->state);
	netdev_unlock(bp->dev);
}

/* Only called from bnxt_sp_task() */
static void bnxt_reset(struct bnxt *bp, bool silent)
{
	bnxt_lock_sp(bp);
	if (test_bit(BNXT_STATE_OPEN, &bp->state))
		bnxt_reset_task(bp, silent);
	bnxt_unlock_sp(bp);
}

/* Only called from bnxt_sp_task() */
static void bnxt_rx_ring_reset(struct bnxt *bp)
{
	int i;

	bnxt_lock_sp(bp);
	if (!test_bit(BNXT_STATE_OPEN, &bp->state)) {
		bnxt_unlock_sp(bp);
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
		bnxt_free_one_rx_ring_skbs(bp, rxr);
		rxr->rx_prod = 0;
		rxr->rx_agg_prod = 0;
		rxr->rx_sw_agg_prod = 0;
		rxr->rx_next_cons = 0;
		rxr->bnapi->in_reset = false;
		bnxt_alloc_one_rx_ring(bp, i);
		cpr = &rxr->bnapi->cp_ring;
		cpr->sw_stats->rx.rx_resets++;
		if (bp->flags & BNXT_FLAG_AGG_RINGS)
			bnxt_db_write(bp, &rxr->rx_agg_db, rxr->rx_agg_prod);
		bnxt_db_write(bp, &rxr->rx_db, rxr->rx_prod);
	}
	if (bp->flags & BNXT_FLAG_TPA)
		bnxt_set_tpa(bp, true);
	bnxt_unlock_sp(bp);
}

static void bnxt_fw_fatal_close(struct bnxt *bp)
{
	bnxt_tx_disable(bp);
	bnxt_disable_napi(bp);
	bnxt_disable_int_sync(bp);
	bnxt_free_irq(bp);
	bnxt_clear_int_mode(bp);
	pci_disable_device(bp->pdev);
}

static void bnxt_fw_reset_close(struct bnxt *bp)
{
	/* When firmware is in fatal state, quiesce device and disable
	 * bus master to prevent any potential bad DMAs before freeing
	 * kernel memory.
	 */
	if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state)) {
		u16 val = 0;

		pci_read_config_word(bp->pdev, PCI_SUBSYSTEM_ID, &val);
		if (val == 0xffff)
			bp->fw_reset_min_dsecs = 0;
		bnxt_fw_fatal_close(bp);
	}
	__bnxt_close_nic(bp, true, false);
	bnxt_vf_reps_free(bp);
	bnxt_clear_int_mode(bp);
	bnxt_hwrm_func_drv_unrgtr(bp);
	if (pci_is_enabled(bp->pdev))
		pci_disable_device(bp->pdev);
	bnxt_free_ctx_mem(bp, false);
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

/* netdev instance lock is acquired before calling this function */
static void bnxt_force_fw_reset(struct bnxt *bp)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	struct bnxt_ptp_cfg *ptp = bp->ptp_cfg;
	u32 wait_dsecs;

	if (!test_bit(BNXT_STATE_OPEN, &bp->state) ||
	    test_bit(BNXT_STATE_IN_FW_RESET, &bp->state))
		return;

	/* we have to serialize with bnxt_refclk_read()*/
	if (ptp) {
		unsigned long flags;

		write_seqlock_irqsave(&ptp->ptp_lock, flags);
		set_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
		write_sequnlock_irqrestore(&ptp->ptp_lock, flags);
	} else {
		set_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
	}
	bnxt_fw_reset_close(bp);
	wait_dsecs = fw_health->master_func_wait_dsecs;
	if (fw_health->primary) {
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
	bnxt_ulp_stop(bp);
	bnxt_lock_sp(bp);
	bnxt_force_fw_reset(bp);
	bnxt_unlock_sp(bp);
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
	bnxt_ulp_stop(bp);
	bnxt_lock_sp(bp);
	if (test_bit(BNXT_STATE_OPEN, &bp->state) &&
	    !test_bit(BNXT_STATE_IN_FW_RESET, &bp->state)) {
		struct bnxt_ptp_cfg *ptp = bp->ptp_cfg;
		int n = 0, tmo;

		/* we have to serialize with bnxt_refclk_read()*/
		if (ptp) {
			unsigned long flags;

			write_seqlock_irqsave(&ptp->ptp_lock, flags);
			set_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
			write_sequnlock_irqrestore(&ptp->ptp_lock, flags);
		} else {
			set_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
		}
		if (bp->pf.active_vfs &&
		    !test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))
			n = bnxt_get_registered_vfs(bp);
		if (n < 0) {
			netdev_err(bp->dev, "Firmware reset aborted, rc = %d\n",
				   n);
			clear_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
			netif_close(bp->dev);
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
	bnxt_unlock_sp(bp);
}

static void bnxt_chk_missed_irq(struct bnxt *bp)
{
	int i;

	if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
		return;

	for (i = 0; i < bp->cp_nr_rings; i++) {
		struct bnxt_napi *bnapi = bp->bnapi[i];
		struct bnxt_cp_ring_info *cpr;
		u32 fw_ring_id;
		int j;

		if (!bnapi)
			continue;

		cpr = &bnapi->cp_ring;
		for (j = 0; j < cpr->cp_ring_count; j++) {
			struct bnxt_cp_ring_info *cpr2 = &cpr->cp_ring_arr[j];
			u32 val[2];

			if (cpr2->has_more_work || !bnxt_has_work(bp, cpr2))
				continue;

			if (cpr2->cp_raw_cons != cpr2->last_cp_raw_cons) {
				cpr2->last_cp_raw_cons = cpr2->cp_raw_cons;
				continue;
			}
			fw_ring_id = cpr2->cp_ring_struct.fw_ring_id;
			bnxt_dbg_hwrm_ring_info_get(bp,
				DBG_RING_INFO_GET_REQ_RING_TYPE_L2_CMPL,
				fw_ring_id, &val[0], &val[1]);
			cpr->sw_stats->cmn.missed_irqs++;
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
		bnxt_set_auto_speed(link_info);
	} else {
		bnxt_set_force_speed(link_info);
		link_info->req_duplex = link_info->duplex_setting;
	}
	if (link_info->autoneg & BNXT_AUTONEG_FLOW_CTRL)
		link_info->req_flow_ctrl =
			link_info->auto_pause_setting & BNXT_LINK_PAUSE_BOTH;
	else
		link_info->req_flow_ctrl = link_info->force_pause_setting;
}

static void bnxt_fw_echo_reply(struct bnxt *bp)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	struct hwrm_func_echo_response_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_FUNC_ECHO_RESPONSE);
	if (rc)
		return;
	req->event_data1 = cpu_to_le32(fw_health->echo_req_data1);
	req->event_data2 = cpu_to_le32(fw_health->echo_req_data2);
	hwrm_req_send(bp, req);
}

static void bnxt_ulp_restart(struct bnxt *bp)
{
	bnxt_ulp_stop(bp);
	bnxt_ulp_start(bp, 0);
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

	if (test_and_clear_bit(BNXT_RESTART_ULP_SP_EVENT, &bp->sp_event)) {
		bnxt_ulp_restart(bp);
		bnxt_reenable_sriov(bp);
	}

	if (test_and_clear_bit(BNXT_RX_MASK_SP_EVENT, &bp->sp_event))
		bnxt_cfg_rx_mode(bp);

	if (test_and_clear_bit(BNXT_RX_NTP_FLTR_SP_EVENT, &bp->sp_event))
		bnxt_cfg_ntp_filters(bp);
	if (test_and_clear_bit(BNXT_HWRM_EXEC_FWD_REQ_SP_EVENT, &bp->sp_event))
		bnxt_hwrm_exec_fwd_req(bp);
	if (test_and_clear_bit(BNXT_HWRM_PF_UNLOAD_SP_EVENT, &bp->sp_event))
		netdev_info(bp->dev, "Receive PF driver unload event!\n");
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

	if (test_and_clear_bit(BNXT_FW_ECHO_REQUEST_SP_EVENT, &bp->sp_event))
		bnxt_fw_echo_reply(bp);

	if (test_and_clear_bit(BNXT_THERMAL_THRESHOLD_SP_EVENT, &bp->sp_event))
		bnxt_hwmon_notify_event(bp);

	/* These functions below will clear BNXT_STATE_IN_SP_TASK.  They
	 * must be the last functions to be called before exiting.
	 */
	if (test_and_clear_bit(BNXT_RESET_TASK_SP_EVENT, &bp->sp_event))
		bnxt_reset(bp, false);

	if (test_and_clear_bit(BNXT_RESET_TASK_SILENT_SP_EVENT, &bp->sp_event))
		bnxt_reset(bp, true);

	if (test_and_clear_bit(BNXT_RST_RING_SP_EVENT, &bp->sp_event))
		bnxt_rx_ring_reset(bp);

	if (test_and_clear_bit(BNXT_FW_RESET_NOTIFY_SP_EVENT, &bp->sp_event)) {
		if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state) ||
		    test_bit(BNXT_STATE_FW_NON_FATAL_COND, &bp->state))
			bnxt_devlink_health_fw_report(bp);
		else
			bnxt_fw_reset(bp);
	}

	if (test_and_clear_bit(BNXT_FW_EXCEPTION_SP_EVENT, &bp->sp_event)) {
		if (!is_bnxt_fw_ok(bp))
			bnxt_devlink_health_fw_report(bp);
	}

	smp_mb__before_atomic();
	clear_bit(BNXT_STATE_IN_SP_TASK, &bp->state);
}

static void _bnxt_get_max_rings(struct bnxt *bp, int *max_rx, int *max_tx,
				int *max_cp);

/* Under netdev instance lock */
int bnxt_check_rings(struct bnxt *bp, int tx, int rx, bool sh, int tcs,
		     int tx_xdp)
{
	int max_rx, max_tx, max_cp, tx_sets = 1, tx_cp;
	struct bnxt_hw_rings hwr = {0};
	int rx_rings = rx;
	int rc;

	if (tcs)
		tx_sets = tcs;

	_bnxt_get_max_rings(bp, &max_rx, &max_tx, &max_cp);

	if (max_rx < rx_rings)
		return -ENOMEM;

	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		rx_rings <<= 1;

	hwr.rx = rx_rings;
	hwr.tx = tx * tx_sets + tx_xdp;
	if (max_tx < hwr.tx)
		return -ENOMEM;

	hwr.vnic = bnxt_get_total_vnics(bp, rx);

	tx_cp = __bnxt_num_tx_to_cp(bp, hwr.tx, tx_sets, tx_xdp);
	hwr.cp = sh ? max_t(int, tx_cp, rx) : tx_cp + rx;
	if (max_cp < hwr.cp)
		return -ENOMEM;
	hwr.stat = hwr.cp;
	if (BNXT_NEW_RM(bp)) {
		hwr.cp += bnxt_get_ulp_msix_num_in_use(bp);
		hwr.stat += bnxt_get_ulp_stat_ctxs_in_use(bp);
		hwr.grp = rx;
		hwr.rss_ctx = bnxt_get_total_rss_ctxs(bp, &hwr);
	}
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
		hwr.cp_p5 = hwr.tx + rx;
	rc = bnxt_hwrm_check_rings(bp, &hwr);
	if (!rc && pci_msix_can_alloc_dyn(bp->pdev)) {
		if (!bnxt_ulp_registered(bp->edev)) {
			hwr.cp += bnxt_get_ulp_msix_num(bp);
			hwr.cp = min_t(int, hwr.cp, bnxt_get_max_func_irqs(bp));
		}
		if (hwr.cp > bp->total_irqs) {
			int total_msix = bnxt_change_msix(bp, hwr.cp);

			if (total_msix < hwr.cp) {
				netdev_warn(bp->dev, "Unable to allocate %d MSIX vectors, maximum available %d\n",
					    hwr.cp, total_msix);
				rc = -ENOSPC;
			}
		}
	}
	return rc;
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
	struct bnxt_coal_cap *coal_cap = &bp->coal_cap;
	struct bnxt_coal *coal;
	u16 flags = 0;

	if (coal_cap->cmpl_params &
	    RING_AGGINT_QCAPS_RESP_CMPL_PARAMS_TIMER_RESET)
		flags |= RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_TIMER_RESET;

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
	coal->flags = flags;

	coal = &bp->tx_coal;
	coal->coal_ticks = 28;
	coal->coal_bufs = 30;
	coal->coal_ticks_irq = 2;
	coal->coal_bufs_irq = 2;
	coal->bufs_per_record = 1;
	coal->flags = flags;

	bp->stats_coal_ticks = BNXT_DEF_STATS_COAL_TICKS;
}

/* FW that pre-reserves 1 VNIC per function */
static bool bnxt_fw_pre_resv_vnics(struct bnxt *bp)
{
	u16 fw_maj = BNXT_FW_MAJ(bp), fw_bld = BNXT_FW_BLD(bp);

	if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS) &&
	    (fw_maj > 218 || (fw_maj == 218 && fw_bld >= 18)))
		return true;
	if ((bp->flags & BNXT_FLAG_CHIP_P5_PLUS) &&
	    (fw_maj > 216 || (fw_maj == 216 && fw_bld >= 172)))
		return true;
	return false;
}

static int bnxt_fw_init_one_p1(struct bnxt *bp)
{
	int rc;

	bp->fw_cap = 0;
	rc = bnxt_hwrm_ver_get(bp);
	/* FW may be unresponsive after FLR. FLR must complete within 100 msec
	 * so wait before continuing with recovery.
	 */
	if (rc)
		msleep(100);
	bnxt_try_map_fw_health_reg(bp);
	if (rc) {
		rc = bnxt_try_recover_fw(bp);
		if (rc)
			return rc;
		rc = bnxt_hwrm_ver_get(bp);
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

	rc = bnxt_alloc_crash_dump_mem(bp);
	if (rc)
		netdev_warn(bp->dev, "crash dump mem alloc failure rc: %d\n",
			    rc);
	if (!rc) {
		rc = bnxt_hwrm_crash_dump_mem_cfg(bp);
		if (rc) {
			bnxt_free_crash_dump_mem(bp);
			netdev_warn(bp->dev,
				    "hwrm crash dump mem failure rc: %d\n", rc);
		}
	}

	if (bnxt_fw_pre_resv_vnics(bp))
		bp->fw_cap |= BNXT_FW_CAP_PRE_RESV_VNICS;

	bnxt_hwrm_func_qcfg(bp);
	bnxt_hwrm_vnic_qcaps(bp);
	bnxt_hwrm_port_led_qcaps(bp);
	bnxt_ethtool_init(bp);
	if (bp->fw_cap & BNXT_FW_CAP_PTP)
		__bnxt_hwrm_ptp_qcfg(bp);
	bnxt_dcb_init(bp);
	bnxt_hwmon_init(bp);
	return 0;
}

static void bnxt_set_dflt_rss_hash_type(struct bnxt *bp)
{
	bp->rss_cap &= ~BNXT_RSS_CAP_UDP_RSS_CAP;
	bp->rss_hash_cfg = VNIC_RSS_CFG_REQ_HASH_TYPE_IPV4 |
			   VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV4 |
			   VNIC_RSS_CFG_REQ_HASH_TYPE_IPV6 |
			   VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV6;
	if (bp->rss_cap & BNXT_RSS_CAP_RSS_HASH_TYPE_DELTA)
		bp->rss_hash_delta = bp->rss_hash_cfg;
	if (BNXT_CHIP_P4_PLUS(bp) && bp->hwrm_spec_code >= 0x10501) {
		bp->rss_cap |= BNXT_RSS_CAP_UDP_RSS_CAP;
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
		if (bnxt_rfs_capable(bp, false)) {
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

static int bnxt_probe_phy(struct bnxt *bp, bool fw_dflt);

int bnxt_fw_init_one(struct bnxt *bp)
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
	rc = bnxt_probe_phy(bp, false);
	if (rc)
		return rc;
	rc = bnxt_approve_mac(bp, bp->dev->dev_addr, false);
	if (rc)
		return rc;

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

bool bnxt_hwrm_reset_permitted(struct bnxt *bp)
{
	struct hwrm_func_qcfg_output *resp;
	struct hwrm_func_qcfg_input *req;
	bool result = true; /* firmware will enforce if unknown */

	if (~bp->fw_cap & BNXT_FW_CAP_HOT_RESET_IF)
		return result;

	if (hwrm_req_init(bp, req, HWRM_FUNC_QCFG))
		return result;

	req->fid = cpu_to_le16(0xffff);
	resp = hwrm_req_hold(bp, req);
	if (!hwrm_req_send(bp, req))
		result = !!(le16_to_cpu(resp->flags) &
			    FUNC_QCFG_RESP_FLAGS_HOT_RESET_ALLOWED);
	hwrm_req_drop(bp, req);
	return result;
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
		struct hwrm_fw_reset_input *req;

		rc = hwrm_req_init(bp, req, HWRM_FW_RESET);
		if (!rc) {
			req->target_id = cpu_to_le16(HWRM_TARGET_ID_KONG);
			req->embedded_proc_type = FW_RESET_REQ_EMBEDDED_PROC_TYPE_CHIP;
			req->selfrst_status = FW_RESET_REQ_SELFRST_STATUS_SELFRSTASAP;
			req->flags = FW_RESET_REQ_FLAGS_RESET_GRACEFUL;
			rc = hwrm_req_send(bp, req);
		}
		if (rc != -ENODEV)
			netdev_warn(bp->dev, "Unable to reset FW rc=%d\n", rc);
	}
	bp->fw_reset_timestamp = jiffies;
}

static bool bnxt_fw_reset_timeout(struct bnxt *bp)
{
	return time_after(jiffies, bp->fw_reset_timestamp +
			  (bp->fw_reset_max_dsecs * HZ / 10));
}

static void bnxt_fw_reset_abort(struct bnxt *bp, int rc)
{
	clear_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
	if (bp->fw_reset_state != BNXT_FW_RESET_STATE_POLL_VF)
		bnxt_dl_health_fw_status_update(bp, false);
	bp->fw_reset_state = 0;
	netif_close(bp->dev);
}

static void bnxt_fw_reset_task(struct work_struct *work)
{
	struct bnxt *bp = container_of(work, struct bnxt, fw_reset_task.work);
	int rc = 0;

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
			if (bnxt_fw_reset_timeout(bp)) {
				clear_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
				bp->fw_reset_state = 0;
				netdev_err(bp->dev, "Firmware reset aborted, bnxt_get_registered_vfs() returns %d\n",
					   n);
				goto ulp_start;
			}
			bnxt_queue_fw_reset_work(bp, HZ / 10);
			return;
		}
		bp->fw_reset_timestamp = jiffies;
		netdev_lock(bp->dev);
		if (test_bit(BNXT_STATE_ABORT_ERR, &bp->state)) {
			bnxt_fw_reset_abort(bp, rc);
			netdev_unlock(bp->dev);
			goto ulp_start;
		}
		bnxt_fw_reset_close(bp);
		if (bp->fw_cap & BNXT_FW_CAP_ERR_RECOVER_RELOAD) {
			bp->fw_reset_state = BNXT_FW_RESET_STATE_POLL_FW_DOWN;
			tmo = HZ / 10;
		} else {
			bp->fw_reset_state = BNXT_FW_RESET_STATE_ENABLE_DEV;
			tmo = bp->fw_reset_min_dsecs * HZ / 10;
		}
		netdev_unlock(bp->dev);
		bnxt_queue_fw_reset_work(bp, tmo);
		return;
	}
	case BNXT_FW_RESET_STATE_POLL_FW_DOWN: {
		u32 val;

		val = bnxt_fw_health_readl(bp, BNXT_FW_HEALTH_REG);
		if (!(val & BNXT_FW_STATUS_SHUTDOWN) &&
		    !bnxt_fw_reset_timeout(bp)) {
			bnxt_queue_fw_reset_work(bp, HZ / 5);
			return;
		}

		if (!bp->fw_health->primary) {
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
		bnxt_inv_fw_health_reg(bp);
		if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state) &&
		    !bp->fw_reset_min_dsecs) {
			u16 val;

			pci_read_config_word(bp->pdev, PCI_SUBSYSTEM_ID, &val);
			if (val == 0xffff) {
				if (bnxt_fw_reset_timeout(bp)) {
					netdev_err(bp->dev, "Firmware reset aborted, PCI config space invalid\n");
					rc = -ETIMEDOUT;
					goto fw_reset_abort;
				}
				bnxt_queue_fw_reset_work(bp, HZ / 1000);
				return;
			}
		}
		clear_bit(BNXT_STATE_FW_FATAL_COND, &bp->state);
		clear_bit(BNXT_STATE_FW_NON_FATAL_COND, &bp->state);
		if (test_and_clear_bit(BNXT_STATE_FW_ACTIVATE_RESET, &bp->state) &&
		    !test_bit(BNXT_STATE_FW_ACTIVATE, &bp->state))
			bnxt_dl_remote_reload(bp);
		if (pci_enable_device(bp->pdev)) {
			netdev_err(bp->dev, "Cannot re-enable PCI device\n");
			rc = -ENODEV;
			goto fw_reset_abort;
		}
		pci_set_master(bp->pdev);
		bp->fw_reset_state = BNXT_FW_RESET_STATE_POLL_FW;
		fallthrough;
	case BNXT_FW_RESET_STATE_POLL_FW:
		bp->hwrm_cmd_timeout = SHORT_HWRM_CMD_TIMEOUT;
		rc = bnxt_hwrm_poll(bp);
		if (rc) {
			if (bnxt_fw_reset_timeout(bp)) {
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
		while (!netdev_trylock(bp->dev)) {
			bnxt_queue_fw_reset_work(bp, HZ / 10);
			return;
		}
		rc = bnxt_open(bp->dev);
		if (rc) {
			netdev_err(bp->dev, "bnxt_open() failed during FW reset\n");
			bnxt_fw_reset_abort(bp, rc);
			netdev_unlock(bp->dev);
			goto ulp_start;
		}

		if ((bp->fw_cap & BNXT_FW_CAP_ERROR_RECOVERY) &&
		    bp->fw_health->enabled) {
			bp->fw_health->last_fw_reset_cnt =
				bnxt_fw_health_readl(bp, BNXT_FW_RESET_CNT_REG);
		}
		bp->fw_reset_state = 0;
		/* Make sure fw_reset_state is 0 before clearing the flag */
		smp_mb__before_atomic();
		clear_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
		bnxt_ptp_reapply_pps(bp);
		clear_bit(BNXT_STATE_FW_ACTIVATE, &bp->state);
		if (test_and_clear_bit(BNXT_STATE_RECOVER, &bp->state)) {
			bnxt_dl_health_fw_recovery_done(bp);
			bnxt_dl_health_fw_status_update(bp, true);
		}
		netdev_unlock(bp->dev);
		bnxt_ulp_start(bp, 0);
		bnxt_reenable_sriov(bp);
		netdev_lock(bp->dev);
		bnxt_vf_reps_alloc(bp);
		bnxt_vf_reps_open(bp);
		netdev_unlock(bp->dev);
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
	netdev_lock(bp->dev);
	bnxt_fw_reset_abort(bp, rc);
	netdev_unlock(bp->dev);
ulp_start:
	bnxt_ulp_start(bp, rc);
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

	INIT_WORK(&bp->sp_task, bnxt_sp_task);
	INIT_DELAYED_WORK(&bp->fw_reset_task, bnxt_fw_reset_task);

	spin_lock_init(&bp->ntp_fltr_lock);
#if BITS_PER_LONG == 32
	spin_lock_init(&bp->db_lock);
#endif

	bp->rx_ring_size = BNXT_DEFAULT_RX_RING_SIZE;
	bp->tx_ring_size = BNXT_DEFAULT_TX_RING_SIZE;

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

static int bnxt_change_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct bnxt *bp = netdev_priv(dev);
	int rc = 0;

	netdev_assert_locked(dev);

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (ether_addr_equal(addr->sa_data, dev->dev_addr))
		return 0;

	rc = bnxt_approve_mac(bp, addr->sa_data, true);
	if (rc)
		return rc;

	eth_hw_addr_set(dev, addr->sa_data);
	bnxt_clear_usr_fltrs(bp, true);
	if (netif_running(dev)) {
		bnxt_close_nic(bp, false, false);
		rc = bnxt_open_nic(bp, false, false);
	}

	return rc;
}

static int bnxt_change_mtu(struct net_device *dev, int new_mtu)
{
	struct bnxt *bp = netdev_priv(dev);

	netdev_assert_locked(dev);

	if (netif_running(dev))
		bnxt_close_nic(bp, true, false);

	WRITE_ONCE(dev->mtu, new_mtu);

	/* MTU change may change the AGG ring settings if an XDP multi-buffer
	 * program is attached.  We need to set the AGG rings settings and
	 * rx_skb_func accordingly.
	 */
	if (READ_ONCE(bp->xdp_prog))
		bnxt_set_rx_skb_mode(bp, true);

	bnxt_set_ring_params(bp);

	if (netif_running(dev))
		return bnxt_open_nic(bp, true, false);

	return 0;
}

int bnxt_setup_mq_tc(struct net_device *dev, u8 tc)
{
	struct bnxt *bp = netdev_priv(dev);
	bool sh = false;
	int rc, tx_cp;

	if (tc > bp->max_tc) {
		netdev_err(dev, "Too many traffic classes requested: %d. Max supported is %d.\n",
			   tc, bp->max_tc);
		return -EINVAL;
	}

	if (bp->num_tc == tc)
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
		bp->num_tc = tc;
	} else {
		bp->tx_nr_rings = bp->tx_nr_rings_per_tc;
		netdev_reset_tc(dev);
		bp->num_tc = 0;
	}
	bp->tx_nr_rings += bp->tx_nr_rings_xdp;
	tx_cp = bnxt_num_tx_to_cp(bp, bp->tx_nr_rings);
	bp->cp_nr_rings = sh ? max_t(int, tx_cp, bp->rx_nr_rings) :
			       tx_cp + bp->rx_nr_rings;

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

u32 bnxt_get_ntp_filter_idx(struct bnxt *bp, struct flow_keys *fkeys,
			    const struct sk_buff *skb)
{
	struct bnxt_vnic_info *vnic;

	if (skb)
		return skb_get_hash_raw(skb) & BNXT_NTP_FLTR_HASH_MASK;

	vnic = &bp->vnic_info[BNXT_VNIC_DEFAULT];
	return bnxt_toeplitz(bp, fkeys, (void *)vnic->rss_hash_key);
}

int bnxt_insert_ntp_filter(struct bnxt *bp, struct bnxt_ntuple_filter *fltr,
			   u32 idx)
{
	struct hlist_head *head;
	int bit_id;

	spin_lock_bh(&bp->ntp_fltr_lock);
	bit_id = bitmap_find_free_region(bp->ntp_fltr_bmap, bp->max_fltr, 0);
	if (bit_id < 0) {
		spin_unlock_bh(&bp->ntp_fltr_lock);
		return -ENOMEM;
	}

	fltr->base.sw_id = (u16)bit_id;
	fltr->base.type = BNXT_FLTR_TYPE_NTUPLE;
	fltr->base.flags |= BNXT_ACT_RING_DST;
	head = &bp->ntp_fltr_hash_tbl[idx];
	hlist_add_head_rcu(&fltr->base.hash, head);
	set_bit(BNXT_FLTR_INSERTED, &fltr->base.state);
	bnxt_insert_usr_fltr(bp, &fltr->base);
	bp->ntp_fltr_count++;
	spin_unlock_bh(&bp->ntp_fltr_lock);
	return 0;
}

static bool bnxt_fltr_match(struct bnxt_ntuple_filter *f1,
			    struct bnxt_ntuple_filter *f2)
{
	struct bnxt_flow_masks *masks1 = &f1->fmasks;
	struct bnxt_flow_masks *masks2 = &f2->fmasks;
	struct flow_keys *keys1 = &f1->fkeys;
	struct flow_keys *keys2 = &f2->fkeys;

	if (keys1->basic.n_proto != keys2->basic.n_proto ||
	    keys1->basic.ip_proto != keys2->basic.ip_proto)
		return false;

	if (keys1->basic.n_proto == htons(ETH_P_IP)) {
		if (keys1->addrs.v4addrs.src != keys2->addrs.v4addrs.src ||
		    masks1->addrs.v4addrs.src != masks2->addrs.v4addrs.src ||
		    keys1->addrs.v4addrs.dst != keys2->addrs.v4addrs.dst ||
		    masks1->addrs.v4addrs.dst != masks2->addrs.v4addrs.dst)
			return false;
	} else {
		if (!ipv6_addr_equal(&keys1->addrs.v6addrs.src,
				     &keys2->addrs.v6addrs.src) ||
		    !ipv6_addr_equal(&masks1->addrs.v6addrs.src,
				     &masks2->addrs.v6addrs.src) ||
		    !ipv6_addr_equal(&keys1->addrs.v6addrs.dst,
				     &keys2->addrs.v6addrs.dst) ||
		    !ipv6_addr_equal(&masks1->addrs.v6addrs.dst,
				     &masks2->addrs.v6addrs.dst))
			return false;
	}

	return keys1->ports.src == keys2->ports.src &&
	       masks1->ports.src == masks2->ports.src &&
	       keys1->ports.dst == keys2->ports.dst &&
	       masks1->ports.dst == masks2->ports.dst &&
	       keys1->control.flags == keys2->control.flags &&
	       f1->l2_fltr == f2->l2_fltr;
}

struct bnxt_ntuple_filter *
bnxt_lookup_ntp_filter_from_idx(struct bnxt *bp,
				struct bnxt_ntuple_filter *fltr, u32 idx)
{
	struct bnxt_ntuple_filter *f;
	struct hlist_head *head;

	head = &bp->ntp_fltr_hash_tbl[idx];
	hlist_for_each_entry_rcu(f, head, base.hash) {
		if (bnxt_fltr_match(f, fltr))
			return f;
	}
	return NULL;
}

#ifdef CONFIG_RFS_ACCEL
static int bnxt_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
			      u16 rxq_index, u32 flow_id)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_ntuple_filter *fltr, *new_fltr;
	struct flow_keys *fkeys;
	struct ethhdr *eth = (struct ethhdr *)skb_mac_header(skb);
	struct bnxt_l2_filter *l2_fltr;
	int rc = 0, idx;
	u32 flags;

	if (ether_addr_equal(dev->dev_addr, eth->h_dest)) {
		l2_fltr = bp->vnic_info[BNXT_VNIC_DEFAULT].l2_filters[0];
		atomic_inc(&l2_fltr->refcnt);
	} else {
		struct bnxt_l2_key key;

		ether_addr_copy(key.dst_mac_addr, eth->h_dest);
		key.vlan = 0;
		l2_fltr = bnxt_lookup_l2_filter_from_key(bp, &key);
		if (!l2_fltr)
			return -EINVAL;
		if (l2_fltr->base.flags & BNXT_ACT_FUNC_DST) {
			bnxt_del_l2_filter(bp, l2_fltr);
			return -EINVAL;
		}
	}
	new_fltr = kzalloc(sizeof(*new_fltr), GFP_ATOMIC);
	if (!new_fltr) {
		bnxt_del_l2_filter(bp, l2_fltr);
		return -ENOMEM;
	}

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
	new_fltr->fmasks = BNXT_FLOW_IPV4_MASK_ALL;
	if (fkeys->basic.n_proto == htons(ETH_P_IPV6)) {
		if (bp->hwrm_spec_code < 0x10601) {
			rc = -EPROTONOSUPPORT;
			goto err_free;
		}
		new_fltr->fmasks = BNXT_FLOW_IPV6_MASK_ALL;
	}
	flags = fkeys->control.flags;
	if (((flags & FLOW_DIS_ENCAPSULATION) &&
	     bp->hwrm_spec_code < 0x10601) || (flags & FLOW_DIS_IS_FRAGMENT)) {
		rc = -EPROTONOSUPPORT;
		goto err_free;
	}
	new_fltr->l2_fltr = l2_fltr;

	idx = bnxt_get_ntp_filter_idx(bp, fkeys, skb);
	rcu_read_lock();
	fltr = bnxt_lookup_ntp_filter_from_idx(bp, new_fltr, idx);
	if (fltr) {
		rc = fltr->base.sw_id;
		rcu_read_unlock();
		goto err_free;
	}
	rcu_read_unlock();

	new_fltr->flow_id = flow_id;
	new_fltr->base.rxq = rxq_index;
	rc = bnxt_insert_ntp_filter(bp, new_fltr, idx);
	if (!rc) {
		bnxt_queue_sp_work(bp, BNXT_RX_NTP_FLTR_SP_EVENT);
		return new_fltr->base.sw_id;
	}

err_free:
	bnxt_del_l2_filter(bp, l2_fltr);
	kfree(new_fltr);
	return rc;
}
#endif

void bnxt_del_ntp_filter(struct bnxt *bp, struct bnxt_ntuple_filter *fltr)
{
	spin_lock_bh(&bp->ntp_fltr_lock);
	if (!test_and_clear_bit(BNXT_FLTR_INSERTED, &fltr->base.state)) {
		spin_unlock_bh(&bp->ntp_fltr_lock);
		return;
	}
	hlist_del_rcu(&fltr->base.hash);
	bnxt_del_one_usr_fltr(bp, &fltr->base);
	bp->ntp_fltr_count--;
	spin_unlock_bh(&bp->ntp_fltr_lock);
	bnxt_del_l2_filter(bp, fltr->l2_fltr);
	clear_bit(fltr->base.sw_id, bp->ntp_fltr_bmap);
	kfree_rcu(fltr, base.rcu);
}

static void bnxt_cfg_ntp_filters(struct bnxt *bp)
{
#ifdef CONFIG_RFS_ACCEL
	int i;

	for (i = 0; i < BNXT_NTP_FLTR_HASH_SIZE; i++) {
		struct hlist_head *head;
		struct hlist_node *tmp;
		struct bnxt_ntuple_filter *fltr;
		int rc;

		head = &bp->ntp_fltr_hash_tbl[i];
		hlist_for_each_entry_safe(fltr, tmp, head, base.hash) {
			bool del = false;

			if (test_bit(BNXT_FLTR_VALID, &fltr->base.state)) {
				if (fltr->base.flags & BNXT_ACT_NO_AGING)
					continue;
				if (rps_may_expire_flow(bp->dev, fltr->base.rxq,
							fltr->flow_id,
							fltr->base.sw_id)) {
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
					set_bit(BNXT_FLTR_VALID, &fltr->base.state);
			}

			if (del)
				bnxt_del_ntp_filter(bp, fltr);
		}
	}
#endif
}

static int bnxt_udp_tunnel_set_port(struct net_device *netdev, unsigned int table,
				    unsigned int entry, struct udp_tunnel_info *ti)
{
	struct bnxt *bp = netdev_priv(netdev);
	unsigned int cmd;

	if (ti->type == UDP_TUNNEL_TYPE_VXLAN)
		cmd = TUNNEL_DST_PORT_ALLOC_REQ_TUNNEL_TYPE_VXLAN;
	else if (ti->type == UDP_TUNNEL_TYPE_GENEVE)
		cmd = TUNNEL_DST_PORT_ALLOC_REQ_TUNNEL_TYPE_GENEVE;
	else
		cmd = TUNNEL_DST_PORT_ALLOC_REQ_TUNNEL_TYPE_VXLAN_GPE;

	return bnxt_hwrm_tunnel_dst_port_alloc(bp, ti->port, cmd);
}

static int bnxt_udp_tunnel_unset_port(struct net_device *netdev, unsigned int table,
				      unsigned int entry, struct udp_tunnel_info *ti)
{
	struct bnxt *bp = netdev_priv(netdev);
	unsigned int cmd;

	if (ti->type == UDP_TUNNEL_TYPE_VXLAN)
		cmd = TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_VXLAN;
	else if (ti->type == UDP_TUNNEL_TYPE_GENEVE)
		cmd = TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_GENEVE;
	else
		cmd = TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_VXLAN_GPE;

	return bnxt_hwrm_tunnel_dst_port_free(bp, cmd);
}

static const struct udp_tunnel_nic_info bnxt_udp_tunnels = {
	.set_port	= bnxt_udp_tunnel_set_port,
	.unset_port	= bnxt_udp_tunnel_unset_port,
	.flags		= UDP_TUNNEL_NIC_INFO_MAY_SLEEP |
			  UDP_TUNNEL_NIC_INFO_OPEN_ONLY,
	.tables		= {
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_VXLAN,  },
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_GENEVE, },
	},
}, bnxt_udp_tunnels_p7 = {
	.set_port	= bnxt_udp_tunnel_set_port,
	.unset_port	= bnxt_udp_tunnel_unset_port,
	.flags		= UDP_TUNNEL_NIC_INFO_MAY_SLEEP |
			  UDP_TUNNEL_NIC_INFO_OPEN_ONLY,
	.tables		= {
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_VXLAN,  },
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_GENEVE, },
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_VXLAN_GPE, },
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

	nla_for_each_nested_type(attr, IFLA_BRIDGE_MODE, br_spec, rem) {
		u16 mode;

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

static const struct net_device_ops bnxt_netdev_ops = {
	.ndo_open		= bnxt_open,
	.ndo_start_xmit		= bnxt_start_xmit,
	.ndo_stop		= bnxt_close,
	.ndo_get_stats64	= bnxt_get_stats64,
	.ndo_set_rx_mode	= bnxt_set_rx_mode,
	.ndo_eth_ioctl		= bnxt_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= bnxt_change_mac_addr,
	.ndo_change_mtu		= bnxt_change_mtu,
	.ndo_fix_features	= bnxt_fix_features,
	.ndo_set_features	= bnxt_set_features,
	.ndo_features_check	= bnxt_features_check,
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
	.ndo_bpf		= bnxt_xdp,
	.ndo_xdp_xmit		= bnxt_xdp_xmit,
	.ndo_bridge_getlink	= bnxt_bridge_getlink,
	.ndo_bridge_setlink	= bnxt_bridge_setlink,
};

static void bnxt_get_queue_stats_rx(struct net_device *dev, int i,
				    struct netdev_queue_stats_rx *stats)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_cp_ring_info *cpr;
	u64 *sw;

	if (!bp->bnapi)
		return;

	cpr = &bp->bnapi[i]->cp_ring;
	sw = cpr->stats.sw_stats;

	stats->packets = 0;
	stats->packets += BNXT_GET_RING_STATS64(sw, rx_ucast_pkts);
	stats->packets += BNXT_GET_RING_STATS64(sw, rx_mcast_pkts);
	stats->packets += BNXT_GET_RING_STATS64(sw, rx_bcast_pkts);

	stats->bytes = 0;
	stats->bytes += BNXT_GET_RING_STATS64(sw, rx_ucast_bytes);
	stats->bytes += BNXT_GET_RING_STATS64(sw, rx_mcast_bytes);
	stats->bytes += BNXT_GET_RING_STATS64(sw, rx_bcast_bytes);

	stats->alloc_fail = cpr->sw_stats->rx.rx_oom_discards;
}

static void bnxt_get_queue_stats_tx(struct net_device *dev, int i,
				    struct netdev_queue_stats_tx *stats)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_napi *bnapi;
	u64 *sw;

	if (!bp->tx_ring)
		return;

	bnapi = bp->tx_ring[bp->tx_ring_map[i]].bnapi;
	sw = bnapi->cp_ring.stats.sw_stats;

	stats->packets = 0;
	stats->packets += BNXT_GET_RING_STATS64(sw, tx_ucast_pkts);
	stats->packets += BNXT_GET_RING_STATS64(sw, tx_mcast_pkts);
	stats->packets += BNXT_GET_RING_STATS64(sw, tx_bcast_pkts);

	stats->bytes = 0;
	stats->bytes += BNXT_GET_RING_STATS64(sw, tx_ucast_bytes);
	stats->bytes += BNXT_GET_RING_STATS64(sw, tx_mcast_bytes);
	stats->bytes += BNXT_GET_RING_STATS64(sw, tx_bcast_bytes);
}

static void bnxt_get_base_stats(struct net_device *dev,
				struct netdev_queue_stats_rx *rx,
				struct netdev_queue_stats_tx *tx)
{
	struct bnxt *bp = netdev_priv(dev);

	rx->packets = bp->net_stats_prev.rx_packets;
	rx->bytes = bp->net_stats_prev.rx_bytes;
	rx->alloc_fail = bp->ring_err_stats_prev.rx_total_oom_discards;

	tx->packets = bp->net_stats_prev.tx_packets;
	tx->bytes = bp->net_stats_prev.tx_bytes;
}

static const struct netdev_stat_ops bnxt_stat_ops = {
	.get_queue_stats_rx	= bnxt_get_queue_stats_rx,
	.get_queue_stats_tx	= bnxt_get_queue_stats_tx,
	.get_base_stats		= bnxt_get_base_stats,
};

static int bnxt_queue_mem_alloc(struct net_device *dev, void *qmem, int idx)
{
	struct bnxt_rx_ring_info *rxr, *clone;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_ring_struct *ring;
	int rc;

	if (!bp->rx_ring)
		return -ENETDOWN;

	rxr = &bp->rx_ring[idx];
	clone = qmem;
	memcpy(clone, rxr, sizeof(*rxr));
	bnxt_init_rx_ring_struct(bp, clone);
	bnxt_reset_rx_ring_struct(bp, clone);

	clone->rx_prod = 0;
	clone->rx_agg_prod = 0;
	clone->rx_sw_agg_prod = 0;
	clone->rx_next_cons = 0;

	rc = bnxt_alloc_rx_page_pool(bp, clone, rxr->page_pool->p.nid);
	if (rc)
		return rc;

	rc = xdp_rxq_info_reg(&clone->xdp_rxq, bp->dev, idx, 0);
	if (rc < 0)
		goto err_page_pool_destroy;

	rc = xdp_rxq_info_reg_mem_model(&clone->xdp_rxq,
					MEM_TYPE_PAGE_POOL,
					clone->page_pool);
	if (rc)
		goto err_rxq_info_unreg;

	ring = &clone->rx_ring_struct;
	rc = bnxt_alloc_ring(bp, &ring->ring_mem);
	if (rc)
		goto err_free_rx_ring;

	if (bp->flags & BNXT_FLAG_AGG_RINGS) {
		ring = &clone->rx_agg_ring_struct;
		rc = bnxt_alloc_ring(bp, &ring->ring_mem);
		if (rc)
			goto err_free_rx_agg_ring;

		rc = bnxt_alloc_rx_agg_bmap(bp, clone);
		if (rc)
			goto err_free_rx_agg_ring;
	}

	if (bp->flags & BNXT_FLAG_TPA) {
		rc = bnxt_alloc_one_tpa_info(bp, clone);
		if (rc)
			goto err_free_tpa_info;
	}

	bnxt_init_one_rx_ring_rxbd(bp, clone);
	bnxt_init_one_rx_agg_ring_rxbd(bp, clone);

	bnxt_alloc_one_rx_ring_skb(bp, clone, idx);
	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		bnxt_alloc_one_rx_ring_page(bp, clone, idx);
	if (bp->flags & BNXT_FLAG_TPA)
		bnxt_alloc_one_tpa_info_data(bp, clone);

	return 0;

err_free_tpa_info:
	bnxt_free_one_tpa_info(bp, clone);
err_free_rx_agg_ring:
	bnxt_free_ring(bp, &clone->rx_agg_ring_struct.ring_mem);
err_free_rx_ring:
	bnxt_free_ring(bp, &clone->rx_ring_struct.ring_mem);
err_rxq_info_unreg:
	xdp_rxq_info_unreg(&clone->xdp_rxq);
err_page_pool_destroy:
	page_pool_destroy(clone->page_pool);
	if (bnxt_separate_head_pool())
		page_pool_destroy(clone->head_pool);
	clone->page_pool = NULL;
	clone->head_pool = NULL;
	return rc;
}

static void bnxt_queue_mem_free(struct net_device *dev, void *qmem)
{
	struct bnxt_rx_ring_info *rxr = qmem;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_ring_struct *ring;

	bnxt_free_one_rx_ring_skbs(bp, rxr);
	bnxt_free_one_tpa_info(bp, rxr);

	xdp_rxq_info_unreg(&rxr->xdp_rxq);

	page_pool_destroy(rxr->page_pool);
	if (bnxt_separate_head_pool())
		page_pool_destroy(rxr->head_pool);
	rxr->page_pool = NULL;
	rxr->head_pool = NULL;

	ring = &rxr->rx_ring_struct;
	bnxt_free_ring(bp, &ring->ring_mem);

	ring = &rxr->rx_agg_ring_struct;
	bnxt_free_ring(bp, &ring->ring_mem);

	kfree(rxr->rx_agg_bmap);
	rxr->rx_agg_bmap = NULL;
}

static void bnxt_copy_rx_ring(struct bnxt *bp,
			      struct bnxt_rx_ring_info *dst,
			      struct bnxt_rx_ring_info *src)
{
	struct bnxt_ring_mem_info *dst_rmem, *src_rmem;
	struct bnxt_ring_struct *dst_ring, *src_ring;
	int i;

	dst_ring = &dst->rx_ring_struct;
	dst_rmem = &dst_ring->ring_mem;
	src_ring = &src->rx_ring_struct;
	src_rmem = &src_ring->ring_mem;

	WARN_ON(dst_rmem->nr_pages != src_rmem->nr_pages);
	WARN_ON(dst_rmem->page_size != src_rmem->page_size);
	WARN_ON(dst_rmem->flags != src_rmem->flags);
	WARN_ON(dst_rmem->depth != src_rmem->depth);
	WARN_ON(dst_rmem->vmem_size != src_rmem->vmem_size);
	WARN_ON(dst_rmem->ctx_mem != src_rmem->ctx_mem);

	dst_rmem->pg_tbl = src_rmem->pg_tbl;
	dst_rmem->pg_tbl_map = src_rmem->pg_tbl_map;
	*dst_rmem->vmem = *src_rmem->vmem;
	for (i = 0; i < dst_rmem->nr_pages; i++) {
		dst_rmem->pg_arr[i] = src_rmem->pg_arr[i];
		dst_rmem->dma_arr[i] = src_rmem->dma_arr[i];
	}

	if (!(bp->flags & BNXT_FLAG_AGG_RINGS))
		return;

	dst_ring = &dst->rx_agg_ring_struct;
	dst_rmem = &dst_ring->ring_mem;
	src_ring = &src->rx_agg_ring_struct;
	src_rmem = &src_ring->ring_mem;

	WARN_ON(dst_rmem->nr_pages != src_rmem->nr_pages);
	WARN_ON(dst_rmem->page_size != src_rmem->page_size);
	WARN_ON(dst_rmem->flags != src_rmem->flags);
	WARN_ON(dst_rmem->depth != src_rmem->depth);
	WARN_ON(dst_rmem->vmem_size != src_rmem->vmem_size);
	WARN_ON(dst_rmem->ctx_mem != src_rmem->ctx_mem);
	WARN_ON(dst->rx_agg_bmap_size != src->rx_agg_bmap_size);

	dst_rmem->pg_tbl = src_rmem->pg_tbl;
	dst_rmem->pg_tbl_map = src_rmem->pg_tbl_map;
	*dst_rmem->vmem = *src_rmem->vmem;
	for (i = 0; i < dst_rmem->nr_pages; i++) {
		dst_rmem->pg_arr[i] = src_rmem->pg_arr[i];
		dst_rmem->dma_arr[i] = src_rmem->dma_arr[i];
	}

	dst->rx_agg_bmap = src->rx_agg_bmap;
}

static int bnxt_queue_start(struct net_device *dev, void *qmem, int idx)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_rx_ring_info *rxr, *clone;
	struct bnxt_cp_ring_info *cpr;
	struct bnxt_vnic_info *vnic;
	struct bnxt_napi *bnapi;
	int i, rc;

	rxr = &bp->rx_ring[idx];
	clone = qmem;

	rxr->rx_prod = clone->rx_prod;
	rxr->rx_agg_prod = clone->rx_agg_prod;
	rxr->rx_sw_agg_prod = clone->rx_sw_agg_prod;
	rxr->rx_next_cons = clone->rx_next_cons;
	rxr->rx_tpa = clone->rx_tpa;
	rxr->rx_tpa_idx_map = clone->rx_tpa_idx_map;
	rxr->page_pool = clone->page_pool;
	rxr->head_pool = clone->head_pool;
	rxr->xdp_rxq = clone->xdp_rxq;

	bnxt_copy_rx_ring(bp, rxr, clone);

	bnapi = rxr->bnapi;
	cpr = &bnapi->cp_ring;

	/* All rings have been reserved and previously allocated.
	 * Reallocating with the same parameters should never fail.
	 */
	rc = bnxt_hwrm_rx_ring_alloc(bp, rxr);
	if (rc)
		goto err_reset;

	if (bp->tph_mode) {
		rc = bnxt_hwrm_cp_ring_alloc_p5(bp, rxr->rx_cpr);
		if (rc)
			goto err_reset;
	}

	rc = bnxt_hwrm_rx_agg_ring_alloc(bp, rxr);
	if (rc)
		goto err_reset;

	bnxt_db_write(bp, &rxr->rx_db, rxr->rx_prod);
	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		bnxt_db_write(bp, &rxr->rx_agg_db, rxr->rx_agg_prod);

	if (bp->flags & BNXT_FLAG_SHARED_RINGS) {
		rc = bnxt_tx_queue_start(bp, idx);
		if (rc)
			goto err_reset;
	}

	napi_enable_locked(&bnapi->napi);
	bnxt_db_nq_arm(bp, &cpr->cp_db, cpr->cp_raw_cons);

	for (i = 0; i < bp->nr_vnics; i++) {
		vnic = &bp->vnic_info[i];

		rc = bnxt_hwrm_vnic_set_rss_p5(bp, vnic, true);
		if (rc) {
			netdev_err(bp->dev, "hwrm vnic %d set rss failure rc: %d\n",
				   vnic->vnic_id, rc);
			return rc;
		}
		vnic->mru = bp->dev->mtu + ETH_HLEN + VLAN_HLEN;
		bnxt_hwrm_vnic_update(bp, vnic,
				      VNIC_UPDATE_REQ_ENABLES_MRU_VALID);
	}

	return 0;

err_reset:
	netdev_err(bp->dev, "Unexpected HWRM error during queue start rc: %d\n",
		   rc);
	napi_enable_locked(&bnapi->napi);
	bnxt_db_nq_arm(bp, &cpr->cp_db, cpr->cp_raw_cons);
	bnxt_reset_task(bp, true);
	return rc;
}

static int bnxt_queue_stop(struct net_device *dev, void *qmem, int idx)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_rx_ring_info *rxr;
	struct bnxt_cp_ring_info *cpr;
	struct bnxt_vnic_info *vnic;
	struct bnxt_napi *bnapi;
	int i;

	for (i = 0; i < bp->nr_vnics; i++) {
		vnic = &bp->vnic_info[i];
		vnic->mru = 0;
		bnxt_hwrm_vnic_update(bp, vnic,
				      VNIC_UPDATE_REQ_ENABLES_MRU_VALID);
	}
	/* Make sure NAPI sees that the VNIC is disabled */
	synchronize_net();
	rxr = &bp->rx_ring[idx];
	bnapi = rxr->bnapi;
	cpr = &bnapi->cp_ring;
	cancel_work_sync(&cpr->dim.work);
	bnxt_hwrm_rx_ring_free(bp, rxr, false);
	bnxt_hwrm_rx_agg_ring_free(bp, rxr, false);
	page_pool_disable_direct_recycling(rxr->page_pool);
	if (bnxt_separate_head_pool())
		page_pool_disable_direct_recycling(rxr->head_pool);

	if (bp->flags & BNXT_FLAG_SHARED_RINGS)
		bnxt_tx_queue_stop(bp, idx);

	/* Disable NAPI now after freeing the rings because HWRM_RING_FREE
	 * completion is handled in NAPI to guarantee no more DMA on that ring
	 * after seeing the completion.
	 */
	napi_disable_locked(&bnapi->napi);

	if (bp->tph_mode) {
		bnxt_hwrm_cp_ring_free(bp, rxr->rx_cpr);
		bnxt_clear_one_cp_ring(bp, rxr->rx_cpr);
	}
	bnxt_db_nq(bp, &cpr->cp_db, cpr->cp_raw_cons);

	memcpy(qmem, rxr, sizeof(*rxr));
	bnxt_init_rx_ring_struct(bp, qmem);

	return 0;
}

static const struct netdev_queue_mgmt_ops bnxt_queue_mgmt_ops = {
	.ndo_queue_mem_size	= sizeof(struct bnxt_rx_ring_info),
	.ndo_queue_mem_alloc	= bnxt_queue_mem_alloc,
	.ndo_queue_mem_free	= bnxt_queue_mem_free,
	.ndo_queue_start	= bnxt_queue_start,
	.ndo_queue_stop		= bnxt_queue_stop,
};

static void bnxt_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnxt *bp = netdev_priv(dev);

	if (BNXT_PF(bp))
		bnxt_sriov_disable(bp);

	bnxt_rdma_aux_device_del(bp);

	bnxt_ptp_clear(bp);
	unregister_netdev(dev);

	bnxt_rdma_aux_device_uninit(bp);

	bnxt_free_l2_filters(bp, true);
	bnxt_free_ntp_fltrs(bp, true);
	WARN_ON(bp->num_rss_ctx);
	clear_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
	/* Flush any pending tasks */
	cancel_work_sync(&bp->sp_task);
	cancel_delayed_work_sync(&bp->fw_reset_task);
	bp->sp_event = 0;

	bnxt_dl_fw_reporters_destroy(bp);
	bnxt_dl_unregister(bp);
	bnxt_shutdown_tc(bp);

	bnxt_clear_int_mode(bp);
	bnxt_hwrm_func_drv_unrgtr(bp);
	bnxt_free_hwrm_resources(bp);
	bnxt_hwmon_uninit(bp);
	bnxt_ethtool_free(bp);
	bnxt_dcb_free(bp);
	kfree(bp->ptp_cfg);
	bp->ptp_cfg = NULL;
	kfree(bp->fw_health);
	bp->fw_health = NULL;
	bnxt_cleanup_pci(bp);
	bnxt_free_ctx_mem(bp, true);
	bnxt_free_crash_dump_mem(bp);
	kfree(bp->rss_indir_tbl);
	bp->rss_indir_tbl = NULL;
	bnxt_free_port_stats(bp);
	free_netdev(dev);
}

static int bnxt_probe_phy(struct bnxt *bp, bool fw_dflt)
{
	int rc = 0;
	struct bnxt_link_info *link_info = &bp->link_info;

	bp->phy_flags = 0;
	rc = bnxt_hwrm_phy_qcaps(bp);
	if (rc) {
		netdev_err(bp->dev, "Probe phy can't get phy capabilities (rc: %x)\n",
			   rc);
		return rc;
	}
	if (bp->phy_flags & BNXT_PHY_FL_NO_FCS)
		bp->dev->priv_flags |= IFF_SUPP_NOFCS;
	else
		bp->dev->priv_flags &= ~IFF_SUPP_NOFCS;

	bp->mac_flags = 0;
	bnxt_hwrm_mac_qcaps(bp);

	if (!fw_dflt)
		return 0;

	mutex_lock(&bp->link_lock);
	rc = bnxt_update_link(bp, false);
	if (rc) {
		mutex_unlock(&bp->link_lock);
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
	mutex_unlock(&bp->link_lock);
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
			bnxt_get_ulp_msix_num_in_use(bp),
			hw_resc->max_stat_ctxs -
			bnxt_get_ulp_stat_ctxs_in_use(bp));
	if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS))
		*max_cp = min_t(int, *max_cp, max_irq);
	max_ring_grps = hw_resc->max_hw_ring_grps;
	if (BNXT_CHIP_TYPE_NITRO_A0(bp) && BNXT_PF(bp)) {
		*max_cp -= 1;
		*max_rx -= 2;
	}
	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		*max_rx >>= 1;
	if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS) {
		int rc;

		rc = __bnxt_trim_rings(bp, max_rx, max_tx, *max_cp, false);
		if (rc) {
			*max_rx = 0;
			*max_tx = 0;
		}
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
	int avail_msix;

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

	avail_msix = bnxt_get_max_func_irqs(bp) - bp->cp_nr_rings;
	if (avail_msix >= BNXT_MIN_ROCE_CP_RINGS) {
		int ulp_num_msix = min(avail_msix, bp->ulp_num_msix_want);

		bnxt_set_ulp_msix_num(bp, ulp_num_msix);
		bnxt_set_dflt_ulp_stat_ctxs(bp);
	}

	rc = __bnxt_reserve_rings(bp);
	if (rc && rc != -ENODEV)
		netdev_warn(bp->dev, "Unable to reserve tx rings\n");
	bp->tx_nr_rings_per_tc = bp->tx_nr_rings;
	if (sh)
		bnxt_trim_dflt_sh_rings(bp);

	/* Rings may have been trimmed, re-reserve the trimmed rings. */
	if (bnxt_need_reserve_rings(bp)) {
		rc = __bnxt_reserve_rings(bp);
		if (rc && rc != -ENODEV)
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
		if (BNXT_VF(bp) && rc == -ENODEV)
			netdev_err(bp->dev, "Cannot configure VF rings while PF is unavailable.\n");
		else
			netdev_err(bp->dev, "Not enough rings available.\n");
		goto init_dflt_ring_err;
	}
	rc = bnxt_init_int_mode(bp);
	if (rc)
		goto init_dflt_ring_err;

	bp->tx_nr_rings_per_tc = bp->tx_nr_rings;

	bnxt_set_dflt_rfs(bp);

init_dflt_ring_err:
	bnxt_ulp_irq_restart(bp, rc);
	return rc;
}

int bnxt_restore_pf_fw_resources(struct bnxt *bp)
{
	int rc;

	netdev_ops_assert_locked(bp->dev);
	bnxt_hwrm_func_qcaps(bp);

	if (netif_running(bp->dev))
		__bnxt_close_nic(bp, true, false);

	bnxt_ulp_irq_stop(bp);
	bnxt_clear_int_mode(bp);
	rc = bnxt_init_int_mode(bp);
	bnxt_ulp_irq_restart(bp, rc);

	if (netif_running(bp->dev)) {
		if (rc)
			netif_close(bp->dev);
		else
			rc = bnxt_open_nic(bp, true, false);
	}

	return rc;
}

static int bnxt_init_mac_addr(struct bnxt *bp)
{
	int rc = 0;

	if (BNXT_PF(bp)) {
		eth_hw_addr_set(bp->dev, bp->pf.mac_addr);
	} else {
#ifdef CONFIG_BNXT_SRIOV
		struct bnxt_vf_info *vf = &bp->vf;
		bool strict_approval = true;

		if (is_valid_ether_addr(vf->mac_addr)) {
			/* overwrite netdev dev_addr with admin VF MAC */
			eth_hw_addr_set(bp->dev, vf->mac_addr);
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

static void bnxt_vpd_read_info(struct bnxt *bp)
{
	struct pci_dev *pdev = bp->pdev;
	unsigned int vpd_size, kw_len;
	int pos, size;
	u8 *vpd_data;

	vpd_data = pci_vpd_alloc(pdev, &vpd_size);
	if (IS_ERR(vpd_data)) {
		pci_warn(pdev, "Unable to read VPD\n");
		return;
	}

	pos = pci_vpd_find_ro_info_keyword(vpd_data, vpd_size,
					   PCI_VPD_RO_KEYWORD_PARTNO, &kw_len);
	if (pos < 0)
		goto read_sn;

	size = min_t(int, kw_len, BNXT_VPD_FLD_LEN - 1);
	memcpy(bp->board_partno, &vpd_data[pos], size);

read_sn:
	pos = pci_vpd_find_ro_info_keyword(vpd_data, vpd_size,
					   PCI_VPD_RO_KEYWORD_SERIALNO,
					   &kw_len);
	if (pos < 0)
		goto exit;

	size = min_t(int, kw_len, BNXT_VPD_FLD_LEN - 1);
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

void bnxt_print_device_info(struct bnxt *bp)
{
	netdev_info(bp->dev, "%s found at mem %lx, node addr %pM\n",
		    board_info[bp->board_idx].name,
		    (long)pci_resource_start(bp->pdev, 0), bp->dev->dev_addr);

	pcie_print_link_status(bp->pdev);
}

static int bnxt_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct bnxt_hw_resc *hw_resc;
	struct net_device *dev;
	struct bnxt *bp;
	int rc, max_irqs;

	if (pci_is_bridge(pdev))
		return -ENODEV;

	if (!pdev->msix_cap) {
		dev_err(&pdev->dev, "MSIX capability not found, aborting\n");
		return -ENODEV;
	}

	/* Clear any pending DMA transactions from crash kernel
	 * while loading driver in capture kernel.
	 */
	if (is_kdump_kernel()) {
		pci_clear_master(pdev);
		pcie_flr(pdev);
	}

	max_irqs = bnxt_get_max_irq(pdev);
	dev = alloc_etherdev_mqs(sizeof(*bp), max_irqs * BNXT_MAX_QUEUE,
				 max_irqs);
	if (!dev)
		return -ENOMEM;

	bp = netdev_priv(dev);
	bp->board_idx = ent->driver_data;
	bp->msg_enable = BNXT_DEF_MSG_ENABLE;
	bnxt_set_max_func_irqs(bp, max_irqs);

	if (bnxt_vf_pciid(bp->board_idx))
		bp->flags |= BNXT_FLAG_VF;

	/* No devlink port registration in case of a VF */
	if (BNXT_PF(bp))
		SET_NETDEV_DEVLINK_PORT(dev, &bp->dl_port);

	rc = bnxt_init_board(pdev, dev);
	if (rc < 0)
		goto init_err_free;

	dev->netdev_ops = &bnxt_netdev_ops;
	dev->stat_ops = &bnxt_stat_ops;
	dev->watchdog_timeo = BNXT_TX_TIMEOUT;
	dev->ethtool_ops = &bnxt_ethtool_ops;
	pci_set_drvdata(pdev, dev);

	rc = bnxt_alloc_hwrm_resources(bp);
	if (rc)
		goto init_err_pci_clean;

	mutex_init(&bp->hwrm_cmd_lock);
	mutex_init(&bp->link_lock);

	rc = bnxt_fw_init_one_p1(bp);
	if (rc)
		goto init_err_pci_clean;

	if (BNXT_PF(bp))
		bnxt_vpd_read_info(bp);

	if (BNXT_CHIP_P5_PLUS(bp)) {
		bp->flags |= BNXT_FLAG_CHIP_P5_PLUS;
		if (BNXT_CHIP_P7(bp))
			bp->flags |= BNXT_FLAG_CHIP_P7;
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
	if (bp->flags & BNXT_FLAG_UDP_GSO_CAP)
		dev->hw_features |= NETIF_F_GSO_UDP_L4;

	if (BNXT_SUPPORTS_TPA(bp))
		dev->hw_features |= NETIF_F_LRO;

	dev->hw_enc_features =
			NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_SG |
			NETIF_F_TSO | NETIF_F_TSO6 |
			NETIF_F_GSO_UDP_TUNNEL | NETIF_F_GSO_GRE |
			NETIF_F_GSO_UDP_TUNNEL_CSUM | NETIF_F_GSO_GRE_CSUM |
			NETIF_F_GSO_IPXIP4 | NETIF_F_GSO_PARTIAL;
	if (bp->flags & BNXT_FLAG_UDP_GSO_CAP)
		dev->hw_enc_features |= NETIF_F_GSO_UDP_L4;
	if (bp->flags & BNXT_FLAG_CHIP_P7)
		dev->udp_tunnel_nic_info = &bnxt_udp_tunnels_p7;
	else
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

	netif_set_tso_max_size(dev, GSO_MAX_SIZE);
	if (bp->tso_max_segs)
		netif_set_tso_max_segs(dev, bp->tso_max_segs);

	dev->xdp_features = NETDEV_XDP_ACT_BASIC | NETDEV_XDP_ACT_REDIRECT |
			    NETDEV_XDP_ACT_RX_SG;

#ifdef CONFIG_BNXT_SRIOV
	init_waitqueue_head(&bp->sriov_cfg_wait);
#endif
	if (BNXT_SUPPORTS_TPA(bp)) {
		bp->gro_func = bnxt_gro_func_5730x;
		if (BNXT_CHIP_P4(bp))
			bp->gro_func = bnxt_gro_func_5731x;
		else if (BNXT_CHIP_P5_PLUS(bp))
			bp->gro_func = bnxt_gro_func_5750x;
	}
	if (!BNXT_CHIP_P4_PLUS(bp))
		bp->flags |= BNXT_FLAG_DOUBLE_DB;

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

	hw_resc = &bp->hw_resc;
	bp->max_fltr = hw_resc->max_rx_em_flows + hw_resc->max_rx_wm_flows +
		       BNXT_L2_FLTR_MAX_FLTR;
	/* Older firmware may not report these filters properly */
	if (bp->max_fltr < BNXT_MAX_FLTR)
		bp->max_fltr = BNXT_MAX_FLTR;
	bnxt_init_l2_fltr_tbl(bp);
	__bnxt_set_rx_skb_mode(bp, false);
	bnxt_set_tpa_flags(bp);
	bnxt_init_ring_params(bp);
	bnxt_set_ring_params(bp);
	bnxt_rdma_aux_device_init(bp);
	rc = bnxt_set_dflt_rings(bp, true);
	if (rc) {
		if (BNXT_VF(bp) && rc == -ENODEV) {
			netdev_err(bp->dev, "Cannot configure VF rings while PF is unavailable.\n");
		} else {
			netdev_err(bp->dev, "Not enough rings available.\n");
			rc = -ENOMEM;
		}
		goto init_err_pci_clean;
	}

	bnxt_fw_init_one_p3(bp);

	bnxt_init_dflt_coal(bp);

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

	bnxt_inv_fw_health_reg(bp);
	rc = bnxt_dl_register(bp);
	if (rc)
		goto init_err_dl;

	INIT_LIST_HEAD(&bp->usr_fltr_list);

	if (BNXT_SUPPORTS_NTUPLE_VNIC(bp))
		bp->rss_cap |= BNXT_RSS_CAP_MULTI_RSS_CTX;
	if (BNXT_SUPPORTS_QUEUE_API(bp))
		dev->queue_mgmt_ops = &bnxt_queue_mgmt_ops;
	dev->request_ops_lock = true;

	rc = register_netdev(dev);
	if (rc)
		goto init_err_cleanup;

	bnxt_dl_fw_reporters_create(bp);

	bnxt_rdma_aux_device_add(bp);

	bnxt_print_device_info(bp);

	pci_save_state(pdev);

	return 0;
init_err_cleanup:
	bnxt_rdma_aux_device_uninit(bp);
	bnxt_dl_unregister(bp);
init_err_dl:
	bnxt_shutdown_tc(bp);
	bnxt_clear_int_mode(bp);

init_err_pci_clean:
	bnxt_hwrm_func_drv_unrgtr(bp);
	bnxt_free_hwrm_resources(bp);
	bnxt_hwmon_uninit(bp);
	bnxt_ethtool_free(bp);
	bnxt_ptp_clear(bp);
	kfree(bp->ptp_cfg);
	bp->ptp_cfg = NULL;
	kfree(bp->fw_health);
	bp->fw_health = NULL;
	bnxt_cleanup_pci(bp);
	bnxt_free_ctx_mem(bp, true);
	bnxt_free_crash_dump_mem(bp);
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
	netdev_lock(dev);
	bp = netdev_priv(dev);
	if (!bp)
		goto shutdown_exit;

	if (netif_running(dev))
		netif_close(dev);

	bnxt_ptp_clear(bp);
	bnxt_clear_int_mode(bp);
	pci_disable_device(pdev);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, bp->wol);
		pci_set_power_state(pdev, PCI_D3hot);
	}

shutdown_exit:
	netdev_unlock(dev);
	rtnl_unlock();
}

#ifdef CONFIG_PM_SLEEP
static int bnxt_suspend(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);
	struct bnxt *bp = netdev_priv(dev);
	int rc = 0;

	bnxt_ulp_stop(bp);

	netdev_lock(dev);
	if (netif_running(dev)) {
		netif_device_detach(dev);
		rc = bnxt_close(dev);
	}
	bnxt_hwrm_func_drv_unrgtr(bp);
	bnxt_ptp_clear(bp);
	pci_disable_device(bp->pdev);
	bnxt_free_ctx_mem(bp, false);
	netdev_unlock(dev);
	return rc;
}

static int bnxt_resume(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);
	struct bnxt *bp = netdev_priv(dev);
	int rc = 0;

	netdev_lock(dev);
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

	bnxt_clear_reservations(bp, true);

	if (bnxt_hwrm_func_drv_rgtr(bp, NULL, 0, false)) {
		rc = -ENODEV;
		goto resume_exit;
	}
	if (bp->fw_crash_mem)
		bnxt_hwrm_crash_dump_mem_cfg(bp);

	if (bnxt_ptp_init(bp)) {
		kfree(bp->ptp_cfg);
		bp->ptp_cfg = NULL;
	}
	bnxt_get_wol_settings(bp);
	if (netif_running(dev)) {
		rc = bnxt_open(dev);
		if (!rc)
			netif_device_attach(dev);
	}

resume_exit:
	netdev_unlock(bp->dev);
	bnxt_ulp_start(bp, rc);
	if (!rc)
		bnxt_reenable_sriov(bp);
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
	bool abort = false;

	netdev_info(netdev, "PCI I/O error detected\n");

	bnxt_ulp_stop(bp);

	netdev_lock(netdev);
	netif_device_detach(netdev);

	if (test_and_set_bit(BNXT_STATE_IN_FW_RESET, &bp->state)) {
		netdev_err(bp->dev, "Firmware reset already in progress\n");
		abort = true;
	}

	if (abort || state == pci_channel_io_perm_failure) {
		netdev_unlock(netdev);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	/* Link is not reliable anymore if state is pci_channel_io_frozen
	 * so we disable bus master to prevent any potential bad DMAs before
	 * freeing kernel memory.
	 */
	if (state == pci_channel_io_frozen) {
		set_bit(BNXT_STATE_PCI_CHANNEL_IO_FROZEN, &bp->state);
		bnxt_fw_fatal_close(bp);
	}

	if (netif_running(netdev))
		__bnxt_close_nic(bp, true, true);

	if (pci_is_enabled(pdev))
		pci_disable_device(pdev);
	bnxt_free_ctx_mem(bp, false);
	netdev_unlock(netdev);

	/* Request a slot slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * bnxt_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot.
 * At this point, the card has experienced a hard reset,
 * followed by fixups by BIOS, and has its config space
 * set up identically to what it was at cold boot.
 */
static pci_ers_result_t bnxt_io_slot_reset(struct pci_dev *pdev)
{
	pci_ers_result_t result = PCI_ERS_RESULT_DISCONNECT;
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct bnxt *bp = netdev_priv(netdev);
	int retry = 0;
	int err = 0;
	int off;

	netdev_info(bp->dev, "PCI Slot Reset\n");

	if (!(bp->flags & BNXT_FLAG_CHIP_P5_PLUS) &&
	    test_bit(BNXT_STATE_PCI_CHANNEL_IO_FROZEN, &bp->state))
		msleep(900);

	netdev_lock(netdev);

	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev,
			"Cannot re-enable PCI device after reset.\n");
	} else {
		pci_set_master(pdev);
		/* Upon fatal error, our device internal logic that latches to
		 * BAR value is getting reset and will restore only upon
		 * rewriting the BARs.
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

		bnxt_inv_fw_health_reg(bp);
		bnxt_try_map_fw_health_reg(bp);

		/* In some PCIe AER scenarios, firmware may take up to
		 * 10 seconds to become ready in the worst case.
		 */
		do {
			err = bnxt_try_recover_fw(bp);
			if (!err)
				break;
			retry++;
		} while (retry < BNXT_FW_SLOT_RESET_RETRY);

		if (err) {
			dev_err(&pdev->dev, "Firmware not ready\n");
			goto reset_exit;
		}

		err = bnxt_hwrm_func_reset(bp);
		if (!err)
			result = PCI_ERS_RESULT_RECOVERED;

		bnxt_ulp_irq_stop(bp);
		bnxt_clear_int_mode(bp);
		err = bnxt_init_int_mode(bp);
		bnxt_ulp_irq_restart(bp, err);
	}

reset_exit:
	clear_bit(BNXT_STATE_IN_FW_RESET, &bp->state);
	bnxt_clear_reservations(bp, true);
	netdev_unlock(netdev);

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
	struct bnxt *bp = netdev_priv(netdev);
	int err;

	netdev_info(bp->dev, "PCI Slot Resume\n");
	netdev_lock(netdev);

	err = bnxt_hwrm_func_qcaps(bp);
	if (!err) {
		if (netif_running(netdev))
			err = bnxt_open(netdev);
		else
			err = bnxt_reserve_rings(bp, true);
	}

	if (!err)
		netif_device_attach(netdev);

	netdev_unlock(netdev);
	bnxt_ulp_start(bp, err);
	if (!err)
		bnxt_reenable_sriov(bp);
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
	int err;

	bnxt_debug_init();
	err = pci_register_driver(&bnxt_pci_driver);
	if (err) {
		bnxt_debug_exit();
		return err;
	}

	return 0;
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
