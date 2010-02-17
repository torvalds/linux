/* bnx2x.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2010 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Eliezer Tamir
 * Based on code from Michael Chan's bnx2 driver
 */

#ifndef BNX2X_H
#define BNX2X_H

/* compilation time flags */

/* define this to make the driver freeze on error to allow getting debug info
 * (you will need to reboot afterwards) */
/* #define BNX2X_STOP_ON_ERROR */

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
#define BCM_VLAN			1
#endif

#if defined(CONFIG_CNIC) || defined(CONFIG_CNIC_MODULE)
#define BCM_CNIC 1
#include "cnic_if.h"
#endif

#define BNX2X_MULTI_QUEUE

#define BNX2X_NEW_NAPI



#include <linux/mdio.h>
#include "bnx2x_reg.h"
#include "bnx2x_fw_defs.h"
#include "bnx2x_hsi.h"
#include "bnx2x_link.h"

/* error/debug prints */

#define DRV_MODULE_NAME		"bnx2x"

/* for messages that are currently off */
#define BNX2X_MSG_OFF			0
#define BNX2X_MSG_MCP			0x010000 /* was: NETIF_MSG_HW */
#define BNX2X_MSG_STATS			0x020000 /* was: NETIF_MSG_TIMER */
#define BNX2X_MSG_NVM			0x040000 /* was: NETIF_MSG_HW */
#define BNX2X_MSG_DMAE			0x080000 /* was: NETIF_MSG_HW */
#define BNX2X_MSG_SP			0x100000 /* was: NETIF_MSG_INTR */
#define BNX2X_MSG_FP			0x200000 /* was: NETIF_MSG_INTR */

#define DP_LEVEL			KERN_NOTICE	/* was: KERN_DEBUG */

/* regular debug print */
#define DP(__mask, __fmt, __args...)				\
do {								\
	if (bp->msg_enable & (__mask))				\
		printk(DP_LEVEL "[%s:%d(%s)]" __fmt,		\
		       __func__, __LINE__,			\
		       bp->dev ? (bp->dev->name) : "?",		\
		       ##__args);				\
} while (0)

/* errors debug print */
#define BNX2X_DBG_ERR(__fmt, __args...)				\
do {								\
	if (netif_msg_probe(bp))				\
		pr_err("[%s:%d(%s)]" __fmt,			\
		       __func__, __LINE__,			\
		       bp->dev ? (bp->dev->name) : "?",		\
		       ##__args);				\
} while (0)

/* for errors (never masked) */
#define BNX2X_ERR(__fmt, __args...)				\
do {								\
	pr_err("[%s:%d(%s)]" __fmt,				\
	       __func__, __LINE__,				\
	       bp->dev ? (bp->dev->name) : "?",			\
	       ##__args);					\
} while (0)

/* before we have a dev->name use dev_info() */
#define BNX2X_DEV_INFO(__fmt, __args...)			 \
do {								 \
	if (netif_msg_probe(bp))				 \
		dev_info(&bp->pdev->dev, __fmt, ##__args);	 \
} while (0)


#ifdef BNX2X_STOP_ON_ERROR
#define bnx2x_panic() do { \
		bp->panic = 1; \
		BNX2X_ERR("driver assert\n"); \
		bnx2x_int_disable(bp); \
		bnx2x_panic_dump(bp); \
	} while (0)
#else
#define bnx2x_panic() do { \
		bp->panic = 1; \
		BNX2X_ERR("driver assert\n"); \
		bnx2x_panic_dump(bp); \
	} while (0)
#endif


#define U64_LO(x)			(u32)(((u64)(x)) & 0xffffffff)
#define U64_HI(x)			(u32)(((u64)(x)) >> 32)
#define HILO_U64(hi, lo)		((((u64)(hi)) << 32) + (lo))


#define REG_ADDR(bp, offset)		(bp->regview + offset)

#define REG_RD(bp, offset)		readl(REG_ADDR(bp, offset))
#define REG_RD8(bp, offset)		readb(REG_ADDR(bp, offset))

#define REG_WR(bp, offset, val)		writel((u32)val, REG_ADDR(bp, offset))
#define REG_WR8(bp, offset, val)	writeb((u8)val, REG_ADDR(bp, offset))
#define REG_WR16(bp, offset, val)	writew((u16)val, REG_ADDR(bp, offset))

#define REG_RD_IND(bp, offset)		bnx2x_reg_rd_ind(bp, offset)
#define REG_WR_IND(bp, offset, val)	bnx2x_reg_wr_ind(bp, offset, val)

#define REG_RD_DMAE(bp, offset, valp, len32) \
	do { \
		bnx2x_read_dmae(bp, offset, len32);\
		memcpy(valp, bnx2x_sp(bp, wb_data[0]), (len32) * 4); \
	} while (0)

#define REG_WR_DMAE(bp, offset, valp, len32) \
	do { \
		memcpy(bnx2x_sp(bp, wb_data[0]), valp, (len32) * 4); \
		bnx2x_write_dmae(bp, bnx2x_sp_mapping(bp, wb_data), \
				 offset, len32); \
	} while (0)

#define VIRT_WR_DMAE_LEN(bp, data, addr, len32, le32_swap) \
	do { \
		memcpy(GUNZIP_BUF(bp), data, (len32) * 4); \
		bnx2x_write_big_buf_wb(bp, addr, len32); \
	} while (0)

#define SHMEM_ADDR(bp, field)		(bp->common.shmem_base + \
					 offsetof(struct shmem_region, field))
#define SHMEM_RD(bp, field)		REG_RD(bp, SHMEM_ADDR(bp, field))
#define SHMEM_WR(bp, field, val)	REG_WR(bp, SHMEM_ADDR(bp, field), val)

#define SHMEM2_ADDR(bp, field)		(bp->common.shmem2_base + \
					 offsetof(struct shmem2_region, field))
#define SHMEM2_RD(bp, field)		REG_RD(bp, SHMEM2_ADDR(bp, field))
#define SHMEM2_WR(bp, field, val)	REG_WR(bp, SHMEM2_ADDR(bp, field), val)

#define EMAC_RD(bp, reg)		REG_RD(bp, emac_base + reg)
#define EMAC_WR(bp, reg, val)		REG_WR(bp, emac_base + reg, val)


/* fast path */

struct sw_rx_bd {
	struct sk_buff	*skb;
	DECLARE_PCI_UNMAP_ADDR(mapping)
};

struct sw_tx_bd {
	struct sk_buff	*skb;
	u16		first_bd;
	u8		flags;
/* Set on the first BD descriptor when there is a split BD */
#define BNX2X_TSO_SPLIT_BD		(1<<0)
};

struct sw_rx_page {
	struct page	*page;
	DECLARE_PCI_UNMAP_ADDR(mapping)
};

union db_prod {
	struct doorbell_set_prod data;
	u32		raw;
};


/* MC hsi */
#define BCM_PAGE_SHIFT			12
#define BCM_PAGE_SIZE			(1 << BCM_PAGE_SHIFT)
#define BCM_PAGE_MASK			(~(BCM_PAGE_SIZE - 1))
#define BCM_PAGE_ALIGN(addr)	(((addr) + BCM_PAGE_SIZE - 1) & BCM_PAGE_MASK)

#define PAGES_PER_SGE_SHIFT		0
#define PAGES_PER_SGE			(1 << PAGES_PER_SGE_SHIFT)
#define SGE_PAGE_SIZE			PAGE_SIZE
#define SGE_PAGE_SHIFT			PAGE_SHIFT
#define SGE_PAGE_ALIGN(addr)		PAGE_ALIGN((typeof(PAGE_SIZE))(addr))

/* SGE ring related macros */
#define NUM_RX_SGE_PAGES		2
#define RX_SGE_CNT		(BCM_PAGE_SIZE / sizeof(struct eth_rx_sge))
#define MAX_RX_SGE_CNT			(RX_SGE_CNT - 2)
/* RX_SGE_CNT is promised to be a power of 2 */
#define RX_SGE_MASK			(RX_SGE_CNT - 1)
#define NUM_RX_SGE			(RX_SGE_CNT * NUM_RX_SGE_PAGES)
#define MAX_RX_SGE			(NUM_RX_SGE - 1)
#define NEXT_SGE_IDX(x)		((((x) & RX_SGE_MASK) == \
				  (MAX_RX_SGE_CNT - 1)) ? (x) + 3 : (x) + 1)
#define RX_SGE(x)			((x) & MAX_RX_SGE)

/* SGE producer mask related macros */
/* Number of bits in one sge_mask array element */
#define RX_SGE_MASK_ELEM_SZ		64
#define RX_SGE_MASK_ELEM_SHIFT		6
#define RX_SGE_MASK_ELEM_MASK		((u64)RX_SGE_MASK_ELEM_SZ - 1)

/* Creates a bitmask of all ones in less significant bits.
   idx - index of the most significant bit in the created mask */
#define RX_SGE_ONES_MASK(idx) \
		(((u64)0x1 << (((idx) & RX_SGE_MASK_ELEM_MASK) + 1)) - 1)
#define RX_SGE_MASK_ELEM_ONE_MASK	((u64)(~0))

/* Number of u64 elements in SGE mask array */
#define RX_SGE_MASK_LEN			((NUM_RX_SGE_PAGES * RX_SGE_CNT) / \
					 RX_SGE_MASK_ELEM_SZ)
#define RX_SGE_MASK_LEN_MASK		(RX_SGE_MASK_LEN - 1)
#define NEXT_SGE_MASK_ELEM(el)		(((el) + 1) & RX_SGE_MASK_LEN_MASK)


struct bnx2x_eth_q_stats {
	u32 total_bytes_received_hi;
	u32 total_bytes_received_lo;
	u32 total_bytes_transmitted_hi;
	u32 total_bytes_transmitted_lo;
	u32 total_unicast_packets_received_hi;
	u32 total_unicast_packets_received_lo;
	u32 total_multicast_packets_received_hi;
	u32 total_multicast_packets_received_lo;
	u32 total_broadcast_packets_received_hi;
	u32 total_broadcast_packets_received_lo;
	u32 total_unicast_packets_transmitted_hi;
	u32 total_unicast_packets_transmitted_lo;
	u32 total_multicast_packets_transmitted_hi;
	u32 total_multicast_packets_transmitted_lo;
	u32 total_broadcast_packets_transmitted_hi;
	u32 total_broadcast_packets_transmitted_lo;
	u32 valid_bytes_received_hi;
	u32 valid_bytes_received_lo;

	u32 error_bytes_received_hi;
	u32 error_bytes_received_lo;
	u32 etherstatsoverrsizepkts_hi;
	u32 etherstatsoverrsizepkts_lo;
	u32 no_buff_discard_hi;
	u32 no_buff_discard_lo;

	u32 driver_xoff;
	u32 rx_err_discard_pkt;
	u32 rx_skb_alloc_failed;
	u32 hw_csum_err;
};

#define BNX2X_NUM_Q_STATS		11
#define Q_STATS_OFFSET32(stat_name) \
			(offsetof(struct bnx2x_eth_q_stats, stat_name) / 4)

struct bnx2x_fastpath {

	struct napi_struct	napi;
	struct host_status_block *status_blk;
	dma_addr_t		status_blk_mapping;

	struct sw_tx_bd		*tx_buf_ring;

	union eth_tx_bd_types	*tx_desc_ring;
	dma_addr_t		tx_desc_mapping;

	struct sw_rx_bd		*rx_buf_ring;	/* BDs mappings ring */
	struct sw_rx_page	*rx_page_ring;	/* SGE pages mappings ring */

	struct eth_rx_bd	*rx_desc_ring;
	dma_addr_t		rx_desc_mapping;

	union eth_rx_cqe	*rx_comp_ring;
	dma_addr_t		rx_comp_mapping;

	/* SGE ring */
	struct eth_rx_sge	*rx_sge_ring;
	dma_addr_t		rx_sge_mapping;

	u64			sge_mask[RX_SGE_MASK_LEN];

	int			state;
#define BNX2X_FP_STATE_CLOSED		0
#define BNX2X_FP_STATE_IRQ		0x80000
#define BNX2X_FP_STATE_OPENING		0x90000
#define BNX2X_FP_STATE_OPEN		0xa0000
#define BNX2X_FP_STATE_HALTING		0xb0000
#define BNX2X_FP_STATE_HALTED		0xc0000

	u8			index;	/* number in fp array */
	u8			cl_id;	/* eth client id */
	u8			sb_id;	/* status block number in HW */

	union db_prod		tx_db;

	u16			tx_pkt_prod;
	u16			tx_pkt_cons;
	u16			tx_bd_prod;
	u16			tx_bd_cons;
	__le16			*tx_cons_sb;

	__le16			fp_c_idx;
	__le16			fp_u_idx;

	u16			rx_bd_prod;
	u16			rx_bd_cons;
	u16			rx_comp_prod;
	u16			rx_comp_cons;
	u16			rx_sge_prod;
	/* The last maximal completed SGE */
	u16			last_max_sge;
	__le16			*rx_cons_sb;
	__le16			*rx_bd_cons_sb;


	unsigned long		tx_pkt,
				rx_pkt,
				rx_calls;

	/* TPA related */
	struct sw_rx_bd		tpa_pool[ETH_MAX_AGGREGATION_QUEUES_E1H];
	u8			tpa_state[ETH_MAX_AGGREGATION_QUEUES_E1H];
#define BNX2X_TPA_START			1
#define BNX2X_TPA_STOP			2
	u8			disable_tpa;
#ifdef BNX2X_STOP_ON_ERROR
	u64			tpa_queue_used;
#endif

	struct tstorm_per_client_stats old_tclient;
	struct ustorm_per_client_stats old_uclient;
	struct xstorm_per_client_stats old_xclient;
	struct bnx2x_eth_q_stats eth_q_stats;

	/* The size is calculated using the following:
	     sizeof name field from netdev structure +
	     4 ('-Xx-' string) +
	     4 (for the digits and to make it DWORD aligned) */
#define FP_NAME_SIZE		(sizeof(((struct net_device *)0)->name) + 8)
	char			name[FP_NAME_SIZE];
	struct bnx2x		*bp; /* parent */
};

#define bnx2x_fp(bp, nr, var)		(bp->fp[nr].var)


/* MC hsi */
#define MAX_FETCH_BD			13	/* HW max BDs per packet */
#define RX_COPY_THRESH			92

#define NUM_TX_RINGS			16
#define TX_DESC_CNT		(BCM_PAGE_SIZE / sizeof(union eth_tx_bd_types))
#define MAX_TX_DESC_CNT			(TX_DESC_CNT - 1)
#define NUM_TX_BD			(TX_DESC_CNT * NUM_TX_RINGS)
#define MAX_TX_BD			(NUM_TX_BD - 1)
#define MAX_TX_AVAIL			(MAX_TX_DESC_CNT * NUM_TX_RINGS - 2)
#define NEXT_TX_IDX(x)		((((x) & MAX_TX_DESC_CNT) == \
				  (MAX_TX_DESC_CNT - 1)) ? (x) + 2 : (x) + 1)
#define TX_BD(x)			((x) & MAX_TX_BD)
#define TX_BD_POFF(x)			((x) & MAX_TX_DESC_CNT)

/* The RX BD ring is special, each bd is 8 bytes but the last one is 16 */
#define NUM_RX_RINGS			8
#define RX_DESC_CNT		(BCM_PAGE_SIZE / sizeof(struct eth_rx_bd))
#define MAX_RX_DESC_CNT			(RX_DESC_CNT - 2)
#define RX_DESC_MASK			(RX_DESC_CNT - 1)
#define NUM_RX_BD			(RX_DESC_CNT * NUM_RX_RINGS)
#define MAX_RX_BD			(NUM_RX_BD - 1)
#define MAX_RX_AVAIL			(MAX_RX_DESC_CNT * NUM_RX_RINGS - 2)
#define NEXT_RX_IDX(x)		((((x) & RX_DESC_MASK) == \
				  (MAX_RX_DESC_CNT - 1)) ? (x) + 3 : (x) + 1)
#define RX_BD(x)			((x) & MAX_RX_BD)

/* As long as CQE is 4 times bigger than BD entry we have to allocate
   4 times more pages for CQ ring in order to keep it balanced with
   BD ring */
#define NUM_RCQ_RINGS			(NUM_RX_RINGS * 4)
#define RCQ_DESC_CNT		(BCM_PAGE_SIZE / sizeof(union eth_rx_cqe))
#define MAX_RCQ_DESC_CNT		(RCQ_DESC_CNT - 1)
#define NUM_RCQ_BD			(RCQ_DESC_CNT * NUM_RCQ_RINGS)
#define MAX_RCQ_BD			(NUM_RCQ_BD - 1)
#define MAX_RCQ_AVAIL			(MAX_RCQ_DESC_CNT * NUM_RCQ_RINGS - 2)
#define NEXT_RCQ_IDX(x)		((((x) & MAX_RCQ_DESC_CNT) == \
				  (MAX_RCQ_DESC_CNT - 1)) ? (x) + 2 : (x) + 1)
#define RCQ_BD(x)			((x) & MAX_RCQ_BD)


/* This is needed for determining of last_max */
#define SUB_S16(a, b)			(s16)((s16)(a) - (s16)(b))

#define __SGE_MASK_SET_BIT(el, bit) \
	do { \
		el = ((el) | ((u64)0x1 << (bit))); \
	} while (0)

#define __SGE_MASK_CLEAR_BIT(el, bit) \
	do { \
		el = ((el) & (~((u64)0x1 << (bit)))); \
	} while (0)

#define SGE_MASK_SET_BIT(fp, idx) \
	__SGE_MASK_SET_BIT(fp->sge_mask[(idx) >> RX_SGE_MASK_ELEM_SHIFT], \
			   ((idx) & RX_SGE_MASK_ELEM_MASK))

#define SGE_MASK_CLEAR_BIT(fp, idx) \
	__SGE_MASK_CLEAR_BIT(fp->sge_mask[(idx) >> RX_SGE_MASK_ELEM_SHIFT], \
			     ((idx) & RX_SGE_MASK_ELEM_MASK))


/* used on a CID received from the HW */
#define SW_CID(x)			(le32_to_cpu(x) & \
					 (COMMON_RAMROD_ETH_RX_CQE_CID >> 7))
#define CQE_CMD(x)			(le32_to_cpu(x) >> \
					COMMON_RAMROD_ETH_RX_CQE_CMD_ID_SHIFT)

#define BD_UNMAP_ADDR(bd)		HILO_U64(le32_to_cpu((bd)->addr_hi), \
						 le32_to_cpu((bd)->addr_lo))
#define BD_UNMAP_LEN(bd)		(le16_to_cpu((bd)->nbytes))


#define DPM_TRIGER_TYPE			0x40
#define DOORBELL(bp, cid, val) \
	do { \
		writel((u32)(val), bp->doorbells + (BCM_PAGE_SIZE * (cid)) + \
		       DPM_TRIGER_TYPE); \
	} while (0)


/* TX CSUM helpers */
#define SKB_CS_OFF(skb)		(offsetof(struct tcphdr, check) - \
				 skb->csum_offset)
#define SKB_CS(skb)		(*(u16 *)(skb_transport_header(skb) + \
					  skb->csum_offset))

#define pbd_tcp_flags(skb)	(ntohl(tcp_flag_word(tcp_hdr(skb)))>>16 & 0xff)

#define XMIT_PLAIN			0
#define XMIT_CSUM_V4			0x1
#define XMIT_CSUM_V6			0x2
#define XMIT_CSUM_TCP			0x4
#define XMIT_GSO_V4			0x8
#define XMIT_GSO_V6			0x10

#define XMIT_CSUM			(XMIT_CSUM_V4 | XMIT_CSUM_V6)
#define XMIT_GSO			(XMIT_GSO_V4 | XMIT_GSO_V6)


/* stuff added to make the code fit 80Col */

#define CQE_TYPE(cqe_fp_flags)	((cqe_fp_flags) & ETH_FAST_PATH_RX_CQE_TYPE)

#define TPA_TYPE_START			ETH_FAST_PATH_RX_CQE_START_FLG
#define TPA_TYPE_END			ETH_FAST_PATH_RX_CQE_END_FLG
#define TPA_TYPE(cqe_fp_flags)		((cqe_fp_flags) & \
					 (TPA_TYPE_START | TPA_TYPE_END))

#define ETH_RX_ERROR_FALGS		ETH_FAST_PATH_RX_CQE_PHY_DECODE_ERR_FLG

#define BNX2X_IP_CSUM_ERR(cqe) \
			(!((cqe)->fast_path_cqe.status_flags & \
			   ETH_FAST_PATH_RX_CQE_IP_XSUM_NO_VALIDATION_FLG) && \
			 ((cqe)->fast_path_cqe.type_error_flags & \
			  ETH_FAST_PATH_RX_CQE_IP_BAD_XSUM_FLG))

#define BNX2X_L4_CSUM_ERR(cqe) \
			(!((cqe)->fast_path_cqe.status_flags & \
			   ETH_FAST_PATH_RX_CQE_L4_XSUM_NO_VALIDATION_FLG) && \
			 ((cqe)->fast_path_cqe.type_error_flags & \
			  ETH_FAST_PATH_RX_CQE_L4_BAD_XSUM_FLG))

#define BNX2X_RX_CSUM_OK(cqe) \
			(!(BNX2X_L4_CSUM_ERR(cqe) || BNX2X_IP_CSUM_ERR(cqe)))

#define BNX2X_PRS_FLAG_OVERETH_IPV4(flags) \
				(((le16_to_cpu(flags) & \
				   PARSING_FLAGS_OVER_ETHERNET_PROTOCOL) >> \
				  PARSING_FLAGS_OVER_ETHERNET_PROTOCOL_SHIFT) \
				 == PRS_FLAG_OVERETH_IPV4)
#define BNX2X_RX_SUM_FIX(cqe) \
	BNX2X_PRS_FLAG_OVERETH_IPV4(cqe->fast_path_cqe.pars_flags.flags)


#define FP_USB_FUNC_OFF			(2 + 2*HC_USTORM_SB_NUM_INDICES)
#define FP_CSB_FUNC_OFF			(2 + 2*HC_CSTORM_SB_NUM_INDICES)

#define U_SB_ETH_RX_CQ_INDEX		HC_INDEX_U_ETH_RX_CQ_CONS
#define U_SB_ETH_RX_BD_INDEX		HC_INDEX_U_ETH_RX_BD_CONS
#define C_SB_ETH_TX_CQ_INDEX		HC_INDEX_C_ETH_TX_CQ_CONS

#define BNX2X_RX_SB_INDEX \
	(&fp->status_blk->u_status_block.index_values[U_SB_ETH_RX_CQ_INDEX])

#define BNX2X_RX_SB_BD_INDEX \
	(&fp->status_blk->u_status_block.index_values[U_SB_ETH_RX_BD_INDEX])

#define BNX2X_RX_SB_INDEX_NUM \
		(((U_SB_ETH_RX_CQ_INDEX << \
		   USTORM_ETH_ST_CONTEXT_CONFIG_CQE_SB_INDEX_NUMBER_SHIFT) & \
		  USTORM_ETH_ST_CONTEXT_CONFIG_CQE_SB_INDEX_NUMBER) | \
		 ((U_SB_ETH_RX_BD_INDEX << \
		   USTORM_ETH_ST_CONTEXT_CONFIG_BD_SB_INDEX_NUMBER_SHIFT) & \
		  USTORM_ETH_ST_CONTEXT_CONFIG_BD_SB_INDEX_NUMBER))

#define BNX2X_TX_SB_INDEX \
	(&fp->status_blk->c_status_block.index_values[C_SB_ETH_TX_CQ_INDEX])


/* end of fast path */

/* common */

struct bnx2x_common {

	u32			chip_id;
/* chip num:16-31, rev:12-15, metal:4-11, bond_id:0-3 */
#define CHIP_ID(bp)			(bp->common.chip_id & 0xfffffff0)

#define CHIP_NUM(bp)			(bp->common.chip_id >> 16)
#define CHIP_NUM_57710			0x164e
#define CHIP_NUM_57711			0x164f
#define CHIP_NUM_57711E			0x1650
#define CHIP_IS_E1(bp)			(CHIP_NUM(bp) == CHIP_NUM_57710)
#define CHIP_IS_57711(bp)		(CHIP_NUM(bp) == CHIP_NUM_57711)
#define CHIP_IS_57711E(bp)		(CHIP_NUM(bp) == CHIP_NUM_57711E)
#define CHIP_IS_E1H(bp)			(CHIP_IS_57711(bp) || \
					 CHIP_IS_57711E(bp))
#define IS_E1H_OFFSET			CHIP_IS_E1H(bp)

#define CHIP_REV(bp)			(bp->common.chip_id & 0x0000f000)
#define CHIP_REV_Ax			0x00000000
/* assume maximum 5 revisions */
#define CHIP_REV_IS_SLOW(bp)		(CHIP_REV(bp) > 0x00005000)
/* Emul versions are A=>0xe, B=>0xc, C=>0xa, D=>8, E=>6 */
#define CHIP_REV_IS_EMUL(bp)		((CHIP_REV_IS_SLOW(bp)) && \
					 !(CHIP_REV(bp) & 0x00001000))
/* FPGA versions are A=>0xf, B=>0xd, C=>0xb, D=>9, E=>7 */
#define CHIP_REV_IS_FPGA(bp)		((CHIP_REV_IS_SLOW(bp)) && \
					 (CHIP_REV(bp) & 0x00001000))

#define CHIP_TIME(bp)			((CHIP_REV_IS_EMUL(bp)) ? 2000 : \
					((CHIP_REV_IS_FPGA(bp)) ? 200 : 1))

#define CHIP_METAL(bp)			(bp->common.chip_id & 0x00000ff0)
#define CHIP_BOND_ID(bp)		(bp->common.chip_id & 0x0000000f)

	int			flash_size;
#define NVRAM_1MB_SIZE			0x20000	/* 1M bit in bytes */
#define NVRAM_TIMEOUT_COUNT		30000
#define NVRAM_PAGE_SIZE			256

	u32			shmem_base;
	u32			shmem2_base;

	u32			hw_config;

	u32			bc_ver;
};


/* end of common */

/* port */

struct nig_stats {
	u32 brb_discard;
	u32 brb_packet;
	u32 brb_truncate;
	u32 flow_ctrl_discard;
	u32 flow_ctrl_octets;
	u32 flow_ctrl_packet;
	u32 mng_discard;
	u32 mng_octet_inp;
	u32 mng_octet_out;
	u32 mng_packet_inp;
	u32 mng_packet_out;
	u32 pbf_octets;
	u32 pbf_packet;
	u32 safc_inp;
	u32 egress_mac_pkt0_lo;
	u32 egress_mac_pkt0_hi;
	u32 egress_mac_pkt1_lo;
	u32 egress_mac_pkt1_hi;
};

struct bnx2x_port {
	u32			pmf;

	u32			link_config;

	u32			supported;
/* link settings - missing defines */
#define SUPPORTED_2500baseX_Full	(1 << 15)

	u32			advertising;
/* link settings - missing defines */
#define ADVERTISED_2500baseX_Full	(1 << 15)

	u32			phy_addr;

	/* used to synchronize phy accesses */
	struct mutex		phy_mutex;
	int			need_hw_lock;

	u32			port_stx;

	struct nig_stats	old_nig_stats;
};

/* end of port */


enum bnx2x_stats_event {
	STATS_EVENT_PMF = 0,
	STATS_EVENT_LINK_UP,
	STATS_EVENT_UPDATE,
	STATS_EVENT_STOP,
	STATS_EVENT_MAX
};

enum bnx2x_stats_state {
	STATS_STATE_DISABLED = 0,
	STATS_STATE_ENABLED,
	STATS_STATE_MAX
};

struct bnx2x_eth_stats {
	u32 total_bytes_received_hi;
	u32 total_bytes_received_lo;
	u32 total_bytes_transmitted_hi;
	u32 total_bytes_transmitted_lo;
	u32 total_unicast_packets_received_hi;
	u32 total_unicast_packets_received_lo;
	u32 total_multicast_packets_received_hi;
	u32 total_multicast_packets_received_lo;
	u32 total_broadcast_packets_received_hi;
	u32 total_broadcast_packets_received_lo;
	u32 total_unicast_packets_transmitted_hi;
	u32 total_unicast_packets_transmitted_lo;
	u32 total_multicast_packets_transmitted_hi;
	u32 total_multicast_packets_transmitted_lo;
	u32 total_broadcast_packets_transmitted_hi;
	u32 total_broadcast_packets_transmitted_lo;
	u32 valid_bytes_received_hi;
	u32 valid_bytes_received_lo;

	u32 error_bytes_received_hi;
	u32 error_bytes_received_lo;
	u32 etherstatsoverrsizepkts_hi;
	u32 etherstatsoverrsizepkts_lo;
	u32 no_buff_discard_hi;
	u32 no_buff_discard_lo;

	u32 rx_stat_ifhcinbadoctets_hi;
	u32 rx_stat_ifhcinbadoctets_lo;
	u32 tx_stat_ifhcoutbadoctets_hi;
	u32 tx_stat_ifhcoutbadoctets_lo;
	u32 rx_stat_dot3statsfcserrors_hi;
	u32 rx_stat_dot3statsfcserrors_lo;
	u32 rx_stat_dot3statsalignmenterrors_hi;
	u32 rx_stat_dot3statsalignmenterrors_lo;
	u32 rx_stat_dot3statscarriersenseerrors_hi;
	u32 rx_stat_dot3statscarriersenseerrors_lo;
	u32 rx_stat_falsecarriererrors_hi;
	u32 rx_stat_falsecarriererrors_lo;
	u32 rx_stat_etherstatsundersizepkts_hi;
	u32 rx_stat_etherstatsundersizepkts_lo;
	u32 rx_stat_dot3statsframestoolong_hi;
	u32 rx_stat_dot3statsframestoolong_lo;
	u32 rx_stat_etherstatsfragments_hi;
	u32 rx_stat_etherstatsfragments_lo;
	u32 rx_stat_etherstatsjabbers_hi;
	u32 rx_stat_etherstatsjabbers_lo;
	u32 rx_stat_maccontrolframesreceived_hi;
	u32 rx_stat_maccontrolframesreceived_lo;
	u32 rx_stat_bmac_xpf_hi;
	u32 rx_stat_bmac_xpf_lo;
	u32 rx_stat_bmac_xcf_hi;
	u32 rx_stat_bmac_xcf_lo;
	u32 rx_stat_xoffstateentered_hi;
	u32 rx_stat_xoffstateentered_lo;
	u32 rx_stat_xonpauseframesreceived_hi;
	u32 rx_stat_xonpauseframesreceived_lo;
	u32 rx_stat_xoffpauseframesreceived_hi;
	u32 rx_stat_xoffpauseframesreceived_lo;
	u32 tx_stat_outxonsent_hi;
	u32 tx_stat_outxonsent_lo;
	u32 tx_stat_outxoffsent_hi;
	u32 tx_stat_outxoffsent_lo;
	u32 tx_stat_flowcontroldone_hi;
	u32 tx_stat_flowcontroldone_lo;
	u32 tx_stat_etherstatscollisions_hi;
	u32 tx_stat_etherstatscollisions_lo;
	u32 tx_stat_dot3statssinglecollisionframes_hi;
	u32 tx_stat_dot3statssinglecollisionframes_lo;
	u32 tx_stat_dot3statsmultiplecollisionframes_hi;
	u32 tx_stat_dot3statsmultiplecollisionframes_lo;
	u32 tx_stat_dot3statsdeferredtransmissions_hi;
	u32 tx_stat_dot3statsdeferredtransmissions_lo;
	u32 tx_stat_dot3statsexcessivecollisions_hi;
	u32 tx_stat_dot3statsexcessivecollisions_lo;
	u32 tx_stat_dot3statslatecollisions_hi;
	u32 tx_stat_dot3statslatecollisions_lo;
	u32 tx_stat_etherstatspkts64octets_hi;
	u32 tx_stat_etherstatspkts64octets_lo;
	u32 tx_stat_etherstatspkts65octetsto127octets_hi;
	u32 tx_stat_etherstatspkts65octetsto127octets_lo;
	u32 tx_stat_etherstatspkts128octetsto255octets_hi;
	u32 tx_stat_etherstatspkts128octetsto255octets_lo;
	u32 tx_stat_etherstatspkts256octetsto511octets_hi;
	u32 tx_stat_etherstatspkts256octetsto511octets_lo;
	u32 tx_stat_etherstatspkts512octetsto1023octets_hi;
	u32 tx_stat_etherstatspkts512octetsto1023octets_lo;
	u32 tx_stat_etherstatspkts1024octetsto1522octets_hi;
	u32 tx_stat_etherstatspkts1024octetsto1522octets_lo;
	u32 tx_stat_etherstatspktsover1522octets_hi;
	u32 tx_stat_etherstatspktsover1522octets_lo;
	u32 tx_stat_bmac_2047_hi;
	u32 tx_stat_bmac_2047_lo;
	u32 tx_stat_bmac_4095_hi;
	u32 tx_stat_bmac_4095_lo;
	u32 tx_stat_bmac_9216_hi;
	u32 tx_stat_bmac_9216_lo;
	u32 tx_stat_bmac_16383_hi;
	u32 tx_stat_bmac_16383_lo;
	u32 tx_stat_dot3statsinternalmactransmiterrors_hi;
	u32 tx_stat_dot3statsinternalmactransmiterrors_lo;
	u32 tx_stat_bmac_ufl_hi;
	u32 tx_stat_bmac_ufl_lo;

	u32 pause_frames_received_hi;
	u32 pause_frames_received_lo;
	u32 pause_frames_sent_hi;
	u32 pause_frames_sent_lo;

	u32 etherstatspkts1024octetsto1522octets_hi;
	u32 etherstatspkts1024octetsto1522octets_lo;
	u32 etherstatspktsover1522octets_hi;
	u32 etherstatspktsover1522octets_lo;

	u32 brb_drop_hi;
	u32 brb_drop_lo;
	u32 brb_truncate_hi;
	u32 brb_truncate_lo;

	u32 mac_filter_discard;
	u32 xxoverflow_discard;
	u32 brb_truncate_discard;
	u32 mac_discard;

	u32 driver_xoff;
	u32 rx_err_discard_pkt;
	u32 rx_skb_alloc_failed;
	u32 hw_csum_err;

	u32 nig_timer_max;
};

#define BNX2X_NUM_STATS			41
#define STATS_OFFSET32(stat_name) \
			(offsetof(struct bnx2x_eth_stats, stat_name) / 4)


#ifdef BCM_CNIC
#define MAX_CONTEXT			15
#else
#define MAX_CONTEXT			16
#endif

union cdu_context {
	struct eth_context eth;
	char pad[1024];
};

#define MAX_DMAE_C			8

/* DMA memory not used in fastpath */
struct bnx2x_slowpath {
	union cdu_context		context[MAX_CONTEXT];
	struct eth_stats_query		fw_stats;
	struct mac_configuration_cmd	mac_config;
	struct mac_configuration_cmd	mcast_config;

	/* used by dmae command executer */
	struct dmae_command		dmae[MAX_DMAE_C];

	u32				stats_comp;
	union mac_stats			mac_stats;
	struct nig_stats		nig_stats;
	struct host_port_stats		port_stats;
	struct host_func_stats		func_stats;
	struct host_func_stats		func_stats_base;

	u32				wb_comp;
	u32				wb_data[4];
};

#define bnx2x_sp(bp, var)		(&bp->slowpath->var)
#define bnx2x_sp_mapping(bp, var) \
		(bp->slowpath_mapping + offsetof(struct bnx2x_slowpath, var))


/* attn group wiring */
#define MAX_DYNAMIC_ATTN_GRPS		8

struct attn_route {
	u32	sig[4];
};

struct bnx2x {
	/* Fields used in the tx and intr/napi performance paths
	 * are grouped together in the beginning of the structure
	 */
	struct bnx2x_fastpath	fp[MAX_CONTEXT];
	void __iomem		*regview;
	void __iomem		*doorbells;
#ifdef BCM_CNIC
#define BNX2X_DB_SIZE		(18*BCM_PAGE_SIZE)
#else
#define BNX2X_DB_SIZE		(16*BCM_PAGE_SIZE)
#endif

	struct net_device	*dev;
	struct pci_dev		*pdev;

	atomic_t		intr_sem;
#ifdef BCM_CNIC
	struct msix_entry	msix_table[MAX_CONTEXT+2];
#else
	struct msix_entry	msix_table[MAX_CONTEXT+1];
#endif
#define INT_MODE_INTx			1
#define INT_MODE_MSI			2
#define INT_MODE_MSIX			3

	int			tx_ring_size;

#ifdef BCM_VLAN
	struct vlan_group	*vlgrp;
#endif

	u32			rx_csum;
	u32			rx_buf_size;
#define ETH_OVREHEAD			(ETH_HLEN + 8)	/* 8 for CRC + VLAN */
#define ETH_MIN_PACKET_SIZE		60
#define ETH_MAX_PACKET_SIZE		1500
#define ETH_MAX_JUMBO_PACKET_SIZE	9600

	/* Max supported alignment is 256 (8 shift) */
#define BNX2X_RX_ALIGN_SHIFT		((L1_CACHE_SHIFT < 8) ? \
					 L1_CACHE_SHIFT : 8)
#define BNX2X_RX_ALIGN			(1 << BNX2X_RX_ALIGN_SHIFT)

	struct host_def_status_block *def_status_blk;
#define DEF_SB_ID			16
	__le16			def_c_idx;
	__le16			def_u_idx;
	__le16			def_x_idx;
	__le16			def_t_idx;
	__le16			def_att_idx;
	u32			attn_state;
	struct attn_route	attn_group[MAX_DYNAMIC_ATTN_GRPS];

	/* slow path ring */
	struct eth_spe		*spq;
	dma_addr_t		spq_mapping;
	u16			spq_prod_idx;
	struct eth_spe		*spq_prod_bd;
	struct eth_spe		*spq_last_bd;
	__le16			*dsb_sp_prod;
	u16			spq_left; /* serialize spq */
	/* used to synchronize spq accesses */
	spinlock_t		spq_lock;

	/* Flags for marking that there is a STAT_QUERY or
	   SET_MAC ramrod pending */
	int			stats_pending;
	int			set_mac_pending;

	/* End of fields used in the performance code paths */

	int			panic;
	int			msg_enable;

	u32			flags;
#define PCIX_FLAG			1
#define PCI_32BIT_FLAG			2
#define ONE_PORT_FLAG			4
#define NO_WOL_FLAG			8
#define USING_DAC_FLAG			0x10
#define USING_MSIX_FLAG			0x20
#define USING_MSI_FLAG			0x40
#define TPA_ENABLE_FLAG			0x80
#define NO_MCP_FLAG			0x100
#define BP_NOMCP(bp)			(bp->flags & NO_MCP_FLAG)
#define HW_VLAN_TX_FLAG			0x400
#define HW_VLAN_RX_FLAG			0x800
#define MF_FUNC_DIS			0x1000

	int			func;
#define BP_PORT(bp)			(bp->func % PORT_MAX)
#define BP_FUNC(bp)			(bp->func)
#define BP_E1HVN(bp)			(bp->func >> 1)
#define BP_L_ID(bp)			(BP_E1HVN(bp) << 2)

#ifdef BCM_CNIC
#define BCM_CNIC_CID_START		16
#define BCM_ISCSI_ETH_CL_ID		17
#endif

	int			pm_cap;
	int			pcie_cap;
	int			mrrs;

	struct delayed_work	sp_task;
	struct work_struct	reset_task;

	struct timer_list	timer;
	int			current_interval;

	u16			fw_seq;
	u16			fw_drv_pulse_wr_seq;
	u32			func_stx;

	struct link_params	link_params;
	struct link_vars	link_vars;
	struct mdio_if_info	mdio;

	struct bnx2x_common	common;
	struct bnx2x_port	port;

	struct cmng_struct_per_port cmng;
	u32			vn_weight_sum;

	u32			mf_config;
	u16			e1hov;
	u8			e1hmf;
#define IS_E1HMF(bp)			(bp->e1hmf != 0)

	u8			wol;

	int			rx_ring_size;

	u16			tx_quick_cons_trip_int;
	u16			tx_quick_cons_trip;
	u16			tx_ticks_int;
	u16			tx_ticks;

	u16			rx_quick_cons_trip_int;
	u16			rx_quick_cons_trip;
	u16			rx_ticks_int;
	u16			rx_ticks;

	u32			lin_cnt;

	int			state;
#define BNX2X_STATE_CLOSED		0
#define BNX2X_STATE_OPENING_WAIT4_LOAD	0x1000
#define BNX2X_STATE_OPENING_WAIT4_PORT	0x2000
#define BNX2X_STATE_OPEN		0x3000
#define BNX2X_STATE_CLOSING_WAIT4_HALT	0x4000
#define BNX2X_STATE_CLOSING_WAIT4_DELETE 0x5000
#define BNX2X_STATE_CLOSING_WAIT4_UNLOAD 0x6000
#define BNX2X_STATE_DIAG		0xe000
#define BNX2X_STATE_ERROR		0xf000

	int			multi_mode;
	int			num_queues;

	u32			rx_mode;
#define BNX2X_RX_MODE_NONE		0
#define BNX2X_RX_MODE_NORMAL		1
#define BNX2X_RX_MODE_ALLMULTI		2
#define BNX2X_RX_MODE_PROMISC		3
#define BNX2X_MAX_MULTICAST		64
#define BNX2X_MAX_EMUL_MULTI		16

	u32 			rx_mode_cl_mask;

	dma_addr_t		def_status_blk_mapping;

	struct bnx2x_slowpath	*slowpath;
	dma_addr_t		slowpath_mapping;

	int			dropless_fc;

#ifdef BCM_CNIC
	u32			cnic_flags;
#define BNX2X_CNIC_FLAG_MAC_SET		1

	void			*t1;
	dma_addr_t		t1_mapping;
	void			*t2;
	dma_addr_t		t2_mapping;
	void			*timers;
	dma_addr_t		timers_mapping;
	void			*qm;
	dma_addr_t		qm_mapping;
	struct cnic_ops		*cnic_ops;
	void			*cnic_data;
	u32			cnic_tag;
	struct cnic_eth_dev	cnic_eth_dev;
	struct host_status_block *cnic_sb;
	dma_addr_t		cnic_sb_mapping;
#define CNIC_SB_ID(bp)			BP_L_ID(bp)
	struct eth_spe		*cnic_kwq;
	struct eth_spe		*cnic_kwq_prod;
	struct eth_spe		*cnic_kwq_cons;
	struct eth_spe		*cnic_kwq_last;
	u16			cnic_kwq_pending;
	u16			cnic_spq_pending;
	struct mutex		cnic_mutex;
	u8			iscsi_mac[6];
#endif

	int			dmae_ready;
	/* used to synchronize dmae accesses */
	struct mutex		dmae_mutex;

	/* used to protect the FW mail box */
	struct mutex		fw_mb_mutex;

	/* used to synchronize stats collecting */
	int			stats_state;
	/* used by dmae command loader */
	struct dmae_command	stats_dmae;
	int			executer_idx;

	u16			stats_counter;
	struct bnx2x_eth_stats	eth_stats;

	struct z_stream_s	*strm;
	void			*gunzip_buf;
	dma_addr_t		gunzip_mapping;
	int			gunzip_outlen;
#define FW_BUF_SIZE			0x8000
#define GUNZIP_BUF(bp)			(bp->gunzip_buf)
#define GUNZIP_PHYS(bp)			(bp->gunzip_mapping)
#define GUNZIP_OUTLEN(bp)		(bp->gunzip_outlen)

	struct raw_op		*init_ops;
	/* Init blocks offsets inside init_ops */
	u16			*init_ops_offsets;
	/* Data blob - has 32 bit granularity */
	u32			*init_data;
	/* Zipped PRAM blobs - raw data */
	const u8		*tsem_int_table_data;
	const u8		*tsem_pram_data;
	const u8		*usem_int_table_data;
	const u8		*usem_pram_data;
	const u8		*xsem_int_table_data;
	const u8		*xsem_pram_data;
	const u8		*csem_int_table_data;
	const u8		*csem_pram_data;
#define INIT_OPS(bp)			(bp->init_ops)
#define INIT_OPS_OFFSETS(bp)		(bp->init_ops_offsets)
#define INIT_DATA(bp)			(bp->init_data)
#define INIT_TSEM_INT_TABLE_DATA(bp)	(bp->tsem_int_table_data)
#define INIT_TSEM_PRAM_DATA(bp)		(bp->tsem_pram_data)
#define INIT_USEM_INT_TABLE_DATA(bp)	(bp->usem_int_table_data)
#define INIT_USEM_PRAM_DATA(bp)		(bp->usem_pram_data)
#define INIT_XSEM_INT_TABLE_DATA(bp)	(bp->xsem_int_table_data)
#define INIT_XSEM_PRAM_DATA(bp)		(bp->xsem_pram_data)
#define INIT_CSEM_INT_TABLE_DATA(bp)	(bp->csem_int_table_data)
#define INIT_CSEM_PRAM_DATA(bp)		(bp->csem_pram_data)

	const struct firmware	*firmware;
};


#define BNX2X_MAX_QUEUES(bp)	(IS_E1HMF(bp) ? (MAX_CONTEXT/E1HVN_MAX) \
					      : MAX_CONTEXT)
#define BNX2X_NUM_QUEUES(bp)	(bp->num_queues)
#define is_multi(bp)		(BNX2X_NUM_QUEUES(bp) > 1)

#define for_each_queue(bp, var) \
			for (var = 0; var < BNX2X_NUM_QUEUES(bp); var++)
#define for_each_nondefault_queue(bp, var) \
			for (var = 1; var < BNX2X_NUM_QUEUES(bp); var++)


void bnx2x_read_dmae(struct bnx2x *bp, u32 src_addr, u32 len32);
void bnx2x_write_dmae(struct bnx2x *bp, dma_addr_t dma_addr, u32 dst_addr,
		      u32 len32);
int bnx2x_get_gpio(struct bnx2x *bp, int gpio_num, u8 port);
int bnx2x_set_gpio(struct bnx2x *bp, int gpio_num, u32 mode, u8 port);
int bnx2x_set_gpio_int(struct bnx2x *bp, int gpio_num, u32 mode, u8 port);
u32 bnx2x_fw_command(struct bnx2x *bp, u32 command);
void bnx2x_reg_wr_ind(struct bnx2x *bp, u32 addr, u32 val);
void bnx2x_write_dmae_phys_len(struct bnx2x *bp, dma_addr_t phys_addr,
			       u32 addr, u32 len);

static inline u32 reg_poll(struct bnx2x *bp, u32 reg, u32 expected, int ms,
			   int wait)
{
	u32 val;

	do {
		val = REG_RD(bp, reg);
		if (val == expected)
			break;
		ms -= wait;
		msleep(wait);

	} while (ms > 0);

	return val;
}


/* load/unload mode */
#define LOAD_NORMAL			0
#define LOAD_OPEN			1
#define LOAD_DIAG			2
#define UNLOAD_NORMAL			0
#define UNLOAD_CLOSE			1


/* DMAE command defines */
#define DMAE_CMD_SRC_PCI		0
#define DMAE_CMD_SRC_GRC		DMAE_COMMAND_SRC

#define DMAE_CMD_DST_PCI		(1 << DMAE_COMMAND_DST_SHIFT)
#define DMAE_CMD_DST_GRC		(2 << DMAE_COMMAND_DST_SHIFT)

#define DMAE_CMD_C_DST_PCI		0
#define DMAE_CMD_C_DST_GRC		(1 << DMAE_COMMAND_C_DST_SHIFT)

#define DMAE_CMD_C_ENABLE		DMAE_COMMAND_C_TYPE_ENABLE

#define DMAE_CMD_ENDIANITY_NO_SWAP	(0 << DMAE_COMMAND_ENDIANITY_SHIFT)
#define DMAE_CMD_ENDIANITY_B_SWAP	(1 << DMAE_COMMAND_ENDIANITY_SHIFT)
#define DMAE_CMD_ENDIANITY_DW_SWAP	(2 << DMAE_COMMAND_ENDIANITY_SHIFT)
#define DMAE_CMD_ENDIANITY_B_DW_SWAP	(3 << DMAE_COMMAND_ENDIANITY_SHIFT)

#define DMAE_CMD_PORT_0			0
#define DMAE_CMD_PORT_1			DMAE_COMMAND_PORT

#define DMAE_CMD_SRC_RESET		DMAE_COMMAND_SRC_RESET
#define DMAE_CMD_DST_RESET		DMAE_COMMAND_DST_RESET
#define DMAE_CMD_E1HVN_SHIFT		DMAE_COMMAND_E1HVN_SHIFT

#define DMAE_LEN32_RD_MAX		0x80
#define DMAE_LEN32_WR_MAX		0x400

#define DMAE_COMP_VAL			0xe0d0d0ae

#define MAX_DMAE_C_PER_PORT		8
#define INIT_DMAE_C(bp)			(BP_PORT(bp) * MAX_DMAE_C_PER_PORT + \
					 BP_E1HVN(bp))
#define PMF_DMAE_C(bp)			(BP_PORT(bp) * MAX_DMAE_C_PER_PORT + \
					 E1HVN_MAX)


/* PCIE link and speed */
#define PCICFG_LINK_WIDTH		0x1f00000
#define PCICFG_LINK_WIDTH_SHIFT		20
#define PCICFG_LINK_SPEED		0xf0000
#define PCICFG_LINK_SPEED_SHIFT		16


#define BNX2X_NUM_TESTS			7

#define BNX2X_PHY_LOOPBACK		0
#define BNX2X_MAC_LOOPBACK		1
#define BNX2X_PHY_LOOPBACK_FAILED	1
#define BNX2X_MAC_LOOPBACK_FAILED	2
#define BNX2X_LOOPBACK_FAILED		(BNX2X_MAC_LOOPBACK_FAILED | \
					 BNX2X_PHY_LOOPBACK_FAILED)


#define STROM_ASSERT_ARRAY_SIZE		50


/* must be used on a CID before placing it on a HW ring */
#define HW_CID(bp, x)			((BP_PORT(bp) << 23) | \
					 (BP_E1HVN(bp) << 17) | (x))

#define SP_DESC_CNT		(BCM_PAGE_SIZE / sizeof(struct eth_spe))
#define MAX_SP_DESC_CNT			(SP_DESC_CNT - 1)


#define BNX2X_BTR			1
#define MAX_SPQ_PENDING			8


/* CMNG constants
   derived from lab experiments, and not from system spec calculations !!! */
#define DEF_MIN_RATE			100
/* resolution of the rate shaping timer - 100 usec */
#define RS_PERIODIC_TIMEOUT_USEC	100
/* resolution of fairness algorithm in usecs -
   coefficient for calculating the actual t fair */
#define T_FAIR_COEF			10000000
/* number of bytes in single QM arbitration cycle -
   coefficient for calculating the fairness timer */
#define QM_ARB_BYTES			40000
#define FAIR_MEM			2


#define ATTN_NIG_FOR_FUNC		(1L << 8)
#define ATTN_SW_TIMER_4_FUNC		(1L << 9)
#define GPIO_2_FUNC			(1L << 10)
#define GPIO_3_FUNC			(1L << 11)
#define GPIO_4_FUNC			(1L << 12)
#define ATTN_GENERAL_ATTN_1		(1L << 13)
#define ATTN_GENERAL_ATTN_2		(1L << 14)
#define ATTN_GENERAL_ATTN_3		(1L << 15)
#define ATTN_GENERAL_ATTN_4		(1L << 13)
#define ATTN_GENERAL_ATTN_5		(1L << 14)
#define ATTN_GENERAL_ATTN_6		(1L << 15)

#define ATTN_HARD_WIRED_MASK		0xff00
#define ATTENTION_ID			4


/* stuff added to make the code fit 80Col */

#define BNX2X_PMF_LINK_ASSERT \
	GENERAL_ATTEN_OFFSET(LINK_SYNC_ATTENTION_BIT_FUNC_0 + BP_FUNC(bp))

#define BNX2X_MC_ASSERT_BITS \
	(GENERAL_ATTEN_OFFSET(TSTORM_FATAL_ASSERT_ATTENTION_BIT) | \
	 GENERAL_ATTEN_OFFSET(USTORM_FATAL_ASSERT_ATTENTION_BIT) | \
	 GENERAL_ATTEN_OFFSET(CSTORM_FATAL_ASSERT_ATTENTION_BIT) | \
	 GENERAL_ATTEN_OFFSET(XSTORM_FATAL_ASSERT_ATTENTION_BIT))

#define BNX2X_MCP_ASSERT \
	GENERAL_ATTEN_OFFSET(MCP_FATAL_ASSERT_ATTENTION_BIT)

#define BNX2X_GRC_TIMEOUT	GENERAL_ATTEN_OFFSET(LATCHED_ATTN_TIMEOUT_GRC)
#define BNX2X_GRC_RSV		(GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCR) | \
				 GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCT) | \
				 GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCN) | \
				 GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCU) | \
				 GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCP) | \
				 GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RSVD_GRC))

#define HW_INTERRUT_ASSERT_SET_0 \
				(AEU_INPUTS_ATTN_BITS_TSDM_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_TCM_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_TSEMI_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_PBF_HW_INTERRUPT)
#define HW_PRTY_ASSERT_SET_0	(AEU_INPUTS_ATTN_BITS_BRB_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_PARSER_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_TSDM_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_SEARCHER_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_TSEMI_PARITY_ERROR)
#define HW_INTERRUT_ASSERT_SET_1 \
				(AEU_INPUTS_ATTN_BITS_QM_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_TIMERS_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_XSDM_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_XCM_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_XSEMI_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_USDM_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_UCM_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_USEMI_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_UPB_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_CSDM_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_CCM_HW_INTERRUPT)
#define HW_PRTY_ASSERT_SET_1	(AEU_INPUTS_ATTN_BITS_PBCLIENT_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_QM_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_XSDM_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_XSEMI_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_DOORBELLQ_PARITY_ERROR |\
			     AEU_INPUTS_ATTN_BITS_VAUX_PCI_CORE_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_DEBUG_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_USDM_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_USEMI_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_UPB_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_CSDM_PARITY_ERROR)
#define HW_INTERRUT_ASSERT_SET_2 \
				(AEU_INPUTS_ATTN_BITS_CSEMI_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_CDU_HW_INTERRUPT | \
				 AEU_INPUTS_ATTN_BITS_DMAE_HW_INTERRUPT | \
			AEU_INPUTS_ATTN_BITS_PXPPCICLOCKCLIENT_HW_INTERRUPT |\
				 AEU_INPUTS_ATTN_BITS_MISC_HW_INTERRUPT)
#define HW_PRTY_ASSERT_SET_2	(AEU_INPUTS_ATTN_BITS_CSEMI_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_PXP_PARITY_ERROR | \
			AEU_INPUTS_ATTN_BITS_PXPPCICLOCKCLIENT_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_CFC_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_CDU_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_IGU_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_MISC_PARITY_ERROR)


#define MULTI_FLAGS(bp) \
		(TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_CAPABILITY | \
		 TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_TCP_CAPABILITY | \
		 TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_CAPABILITY | \
		 TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_TCP_CAPABILITY | \
		 (bp->multi_mode << \
		  TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_MODE_SHIFT))
#define MULTI_MASK			0x7f


#define DEF_USB_FUNC_OFF		(2 + 2*HC_USTORM_DEF_SB_NUM_INDICES)
#define DEF_CSB_FUNC_OFF		(2 + 2*HC_CSTORM_DEF_SB_NUM_INDICES)
#define DEF_XSB_FUNC_OFF		(2 + 2*HC_XSTORM_DEF_SB_NUM_INDICES)
#define DEF_TSB_FUNC_OFF		(2 + 2*HC_TSTORM_DEF_SB_NUM_INDICES)

#define C_DEF_SB_SP_INDEX		HC_INDEX_DEF_C_ETH_SLOW_PATH

#define BNX2X_SP_DSB_INDEX \
(&bp->def_status_blk->c_def_status_block.index_values[C_DEF_SB_SP_INDEX])


#define CAM_IS_INVALID(x) \
(x.target_table_entry.flags == TSTORM_CAM_TARGET_TABLE_ENTRY_ACTION_TYPE)

#define CAM_INVALIDATE(x) \
	(x.target_table_entry.flags = TSTORM_CAM_TARGET_TABLE_ENTRY_ACTION_TYPE)


/* Number of u32 elements in MC hash array */
#define MC_HASH_SIZE			8
#define MC_HASH_OFFSET(bp, i)		(BAR_TSTRORM_INTMEM + \
	TSTORM_APPROXIMATE_MATCH_MULTICAST_FILTERING_OFFSET(BP_FUNC(bp)) + i*4)


#ifndef PXP2_REG_PXP2_INT_STS
#define PXP2_REG_PXP2_INT_STS		PXP2_REG_PXP2_INT_STS_0
#endif

/* MISC_REG_RESET_REG - this is here for the hsi to work don't touch */

#endif /* bnx2x.h */
