/* bnx2x.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2012 Broadcom Corporation
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
#include <linux/netdevice.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>

/* compilation time flags */

/* define this to make the driver freeze on error to allow getting debug info
 * (you will need to reboot afterwards) */
/* #define BNX2X_STOP_ON_ERROR */

#define DRV_MODULE_VERSION      "1.72.50-0"
#define DRV_MODULE_RELDATE      "2012/04/23"
#define BNX2X_BC_VER            0x040200

#if defined(CONFIG_DCB)
#define BCM_DCBNL
#endif


#include "bnx2x_hsi.h"

#if defined(CONFIG_CNIC) || defined(CONFIG_CNIC_MODULE)
#define BCM_CNIC 1
#include "../cnic_if.h"
#endif

#ifdef BCM_CNIC
#define BNX2X_MIN_MSIX_VEC_CNT 3
#define BNX2X_MSIX_VEC_FP_START 2
#else
#define BNX2X_MIN_MSIX_VEC_CNT 2
#define BNX2X_MSIX_VEC_FP_START 1
#endif

#include <linux/mdio.h>

#include "bnx2x_reg.h"
#include "bnx2x_fw_defs.h"
#include "bnx2x_hsi.h"
#include "bnx2x_link.h"
#include "bnx2x_sp.h"
#include "bnx2x_dcb.h"
#include "bnx2x_stats.h"

/* error/debug prints */

#define DRV_MODULE_NAME		"bnx2x"

/* for messages that are currently off */
#define BNX2X_MSG_OFF			0x0
#define BNX2X_MSG_MCP			0x0010000 /* was: NETIF_MSG_HW */
#define BNX2X_MSG_STATS			0x0020000 /* was: NETIF_MSG_TIMER */
#define BNX2X_MSG_NVM			0x0040000 /* was: NETIF_MSG_HW */
#define BNX2X_MSG_DMAE			0x0080000 /* was: NETIF_MSG_HW */
#define BNX2X_MSG_SP			0x0100000 /* was: NETIF_MSG_INTR */
#define BNX2X_MSG_FP			0x0200000 /* was: NETIF_MSG_INTR */
#define BNX2X_MSG_IOV			0x0800000
#define BNX2X_MSG_IDLE			0x2000000 /* used for idle check*/
#define BNX2X_MSG_ETHTOOL		0x4000000
#define BNX2X_MSG_DCB			0x8000000

/* regular debug print */
#define DP(__mask, fmt, ...)					\
do {								\
	if (unlikely(bp->msg_enable & (__mask)))		\
		pr_notice("[%s:%d(%s)]" fmt,			\
			  __func__, __LINE__,			\
			  bp->dev ? (bp->dev->name) : "?",	\
			  ##__VA_ARGS__);			\
} while (0)

#define DP_CONT(__mask, fmt, ...)				\
do {								\
	if (unlikely(bp->msg_enable & (__mask)))		\
		pr_cont(fmt, ##__VA_ARGS__);			\
} while (0)

/* errors debug print */
#define BNX2X_DBG_ERR(fmt, ...)					\
do {								\
	if (unlikely(netif_msg_probe(bp)))			\
		pr_err("[%s:%d(%s)]" fmt,			\
		       __func__, __LINE__,			\
		       bp->dev ? (bp->dev->name) : "?",		\
		       ##__VA_ARGS__);				\
} while (0)

/* for errors (never masked) */
#define BNX2X_ERR(fmt, ...)					\
do {								\
	pr_err("[%s:%d(%s)]" fmt,				\
	       __func__, __LINE__,				\
	       bp->dev ? (bp->dev->name) : "?",			\
	       ##__VA_ARGS__);					\
} while (0)

#define BNX2X_ERROR(fmt, ...)					\
	pr_err("[%s:%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)


/* before we have a dev->name use dev_info() */
#define BNX2X_DEV_INFO(fmt, ...)				 \
do {								 \
	if (unlikely(netif_msg_probe(bp)))			 \
		dev_info(&bp->pdev->dev, fmt, ##__VA_ARGS__);	 \
} while (0)

#ifdef BNX2X_STOP_ON_ERROR
void bnx2x_int_disable(struct bnx2x *bp);
#define bnx2x_panic()				\
do {						\
	bp->panic = 1;				\
	BNX2X_ERR("driver assert\n");		\
	bnx2x_int_disable(bp);			\
	bnx2x_panic_dump(bp);			\
} while (0)
#else
#define bnx2x_panic()				\
do {						\
	bp->panic = 1;				\
	BNX2X_ERR("driver assert\n");		\
	bnx2x_panic_dump(bp);			\
} while (0)
#endif

#define bnx2x_mc_addr(ha)      ((ha)->addr)
#define bnx2x_uc_addr(ha)      ((ha)->addr)

#define U64_LO(x)			(u32)(((u64)(x)) & 0xffffffff)
#define U64_HI(x)			(u32)(((u64)(x)) >> 32)
#define HILO_U64(hi, lo)		((((u64)(hi)) << 32) + (lo))


#define REG_ADDR(bp, offset)		((bp->regview) + (offset))

#define REG_RD(bp, offset)		readl(REG_ADDR(bp, offset))
#define REG_RD8(bp, offset)		readb(REG_ADDR(bp, offset))
#define REG_RD16(bp, offset)		readw(REG_ADDR(bp, offset))

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

#define REG_WR_DMAE_LEN(bp, offset, valp, len32) \
	REG_WR_DMAE(bp, offset, valp, len32)

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
#define MF_CFG_ADDR(bp, field)		(bp->common.mf_cfg_base + \
					 offsetof(struct mf_cfg, field))
#define MF2_CFG_ADDR(bp, field)		(bp->common.mf2_cfg_base + \
					 offsetof(struct mf2_cfg, field))

#define MF_CFG_RD(bp, field)		REG_RD(bp, MF_CFG_ADDR(bp, field))
#define MF_CFG_WR(bp, field, val)	REG_WR(bp,\
					       MF_CFG_ADDR(bp, field), (val))
#define MF2_CFG_RD(bp, field)		REG_RD(bp, MF2_CFG_ADDR(bp, field))

#define SHMEM2_HAS(bp, field)		((bp)->common.shmem2_base &&	\
					 (SHMEM2_RD((bp), size) >	\
					 offsetof(struct shmem2_region, field)))

#define EMAC_RD(bp, reg)		REG_RD(bp, emac_base + reg)
#define EMAC_WR(bp, reg, val)		REG_WR(bp, emac_base + reg, val)

/* SP SB indices */

/* General SP events - stats query, cfc delete, etc  */
#define HC_SP_INDEX_ETH_DEF_CONS		3

/* EQ completions */
#define HC_SP_INDEX_EQ_CONS			7

/* FCoE L2 connection completions */
#define HC_SP_INDEX_ETH_FCOE_TX_CQ_CONS		6
#define HC_SP_INDEX_ETH_FCOE_RX_CQ_CONS		4
/* iSCSI L2 */
#define HC_SP_INDEX_ETH_ISCSI_CQ_CONS		5
#define HC_SP_INDEX_ETH_ISCSI_RX_CQ_CONS	1

/* Special clients parameters */

/* SB indices */
/* FCoE L2 */
#define BNX2X_FCOE_L2_RX_INDEX \
	(&bp->def_status_blk->sp_sb.\
	index_values[HC_SP_INDEX_ETH_FCOE_RX_CQ_CONS])

#define BNX2X_FCOE_L2_TX_INDEX \
	(&bp->def_status_blk->sp_sb.\
	index_values[HC_SP_INDEX_ETH_FCOE_TX_CQ_CONS])

/**
 *  CIDs and CLIDs:
 *  CLIDs below is a CLID for func 0, then the CLID for other
 *  functions will be calculated by the formula:
 *
 *  FUNC_N_CLID_X = N * NUM_SPECIAL_CLIENTS + FUNC_0_CLID_X
 *
 */
enum {
	BNX2X_ISCSI_ETH_CL_ID_IDX,
	BNX2X_FCOE_ETH_CL_ID_IDX,
	BNX2X_MAX_CNIC_ETH_CL_ID_IDX,
};

#define BNX2X_CNIC_START_ETH_CID	48
enum {
	/* iSCSI L2 */
	BNX2X_ISCSI_ETH_CID = BNX2X_CNIC_START_ETH_CID,
	/* FCoE L2 */
	BNX2X_FCOE_ETH_CID,
};

/** Additional rings budgeting */
#ifdef BCM_CNIC
#define CNIC_PRESENT			1
#define FCOE_PRESENT			1
#else
#define CNIC_PRESENT			0
#define FCOE_PRESENT			0
#endif /* BCM_CNIC */
#define NON_ETH_CONTEXT_USE	(FCOE_PRESENT)

#define AEU_IN_ATTN_BITS_PXPPCICLOCKCLIENT_PARITY_ERROR \
	AEU_INPUTS_ATTN_BITS_PXPPCICLOCKCLIENT_PARITY_ERROR

#define SM_RX_ID			0
#define SM_TX_ID			1

/* defines for multiple tx priority indices */
#define FIRST_TX_ONLY_COS_INDEX		1
#define FIRST_TX_COS_INDEX		0

/* defines for decodeing the fastpath index and the cos index out of the
 * transmission queue index
 */
#define MAX_TXQS_PER_COS	FP_SB_MAX_E1x

#define TXQ_TO_FP(txq_index)	((txq_index) % MAX_TXQS_PER_COS)
#define TXQ_TO_COS(txq_index)	((txq_index) / MAX_TXQS_PER_COS)

/* rules for calculating the cids of tx-only connections */
#define CID_TO_FP(cid)		((cid) % MAX_TXQS_PER_COS)
#define CID_COS_TO_TX_ONLY_CID(cid, cos)	(cid + cos * MAX_TXQS_PER_COS)

/* fp index inside class of service range */
#define FP_COS_TO_TXQ(fp, cos)    ((fp)->index + cos * MAX_TXQS_PER_COS)

/*
 * 0..15 eth cos0
 * 16..31 eth cos1 if applicable
 * 32..47 eth cos2 If applicable
 * fcoe queue follows eth queues (16, 32, 48 depending on cos)
 */
#define MAX_ETH_TXQ_IDX(bp)	(MAX_TXQS_PER_COS * (bp)->max_cos)
#define FCOE_TXQ_IDX(bp)	(MAX_ETH_TXQ_IDX(bp))

/* fast path */
/*
 * This driver uses new build_skb() API :
 * RX ring buffer contains pointer to kmalloc() data only,
 * skb are built only after Hardware filled the frame.
 */
struct sw_rx_bd {
	u8		*data;
	DEFINE_DMA_UNMAP_ADDR(mapping);
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
	DEFINE_DMA_UNMAP_ADDR(mapping);
};

union db_prod {
	struct doorbell_set_prod data;
	u32		raw;
};

/* dropless fc FW/HW related params */
#define BRB_SIZE(bp)		(CHIP_IS_E3(bp) ? 1024 : 512)
#define MAX_AGG_QS(bp)		(CHIP_IS_E1(bp) ? \
					ETH_MAX_AGGREGATION_QUEUES_E1 :\
					ETH_MAX_AGGREGATION_QUEUES_E1H_E2)
#define FW_DROP_LEVEL(bp)	(3 + MAX_SPQ_PENDING + MAX_AGG_QS(bp))
#define FW_PREFETCH_CNT		16
#define DROPLESS_FC_HEADROOM	100

/* MC hsi */
#define BCM_PAGE_SHIFT		12
#define BCM_PAGE_SIZE		(1 << BCM_PAGE_SHIFT)
#define BCM_PAGE_MASK		(~(BCM_PAGE_SIZE - 1))
#define BCM_PAGE_ALIGN(addr)	(((addr) + BCM_PAGE_SIZE - 1) & BCM_PAGE_MASK)

#define PAGES_PER_SGE_SHIFT	0
#define PAGES_PER_SGE		(1 << PAGES_PER_SGE_SHIFT)
#define SGE_PAGE_SIZE		PAGE_SIZE
#define SGE_PAGE_SHIFT		PAGE_SHIFT
#define SGE_PAGE_ALIGN(addr)	PAGE_ALIGN((typeof(PAGE_SIZE))(addr))

/* SGE ring related macros */
#define NUM_RX_SGE_PAGES	2
#define RX_SGE_CNT		(BCM_PAGE_SIZE / sizeof(struct eth_rx_sge))
#define NEXT_PAGE_SGE_DESC_CNT	2
#define MAX_RX_SGE_CNT		(RX_SGE_CNT - NEXT_PAGE_SGE_DESC_CNT)
/* RX_SGE_CNT is promised to be a power of 2 */
#define RX_SGE_MASK		(RX_SGE_CNT - 1)
#define NUM_RX_SGE		(RX_SGE_CNT * NUM_RX_SGE_PAGES)
#define MAX_RX_SGE		(NUM_RX_SGE - 1)
#define NEXT_SGE_IDX(x)		((((x) & RX_SGE_MASK) == \
				  (MAX_RX_SGE_CNT - 1)) ? \
					(x) + 1 + NEXT_PAGE_SGE_DESC_CNT : \
					(x) + 1)
#define RX_SGE(x)		((x) & MAX_RX_SGE)

/*
 * Number of required  SGEs is the sum of two:
 * 1. Number of possible opened aggregations (next packet for
 *    these aggregations will probably consume SGE immidiatelly)
 * 2. Rest of BRB blocks divided by 2 (block will consume new SGE only
 *    after placement on BD for new TPA aggregation)
 *
 * Takes into account NEXT_PAGE_SGE_DESC_CNT "next" elements on each page
 */
#define NUM_SGE_REQ		(MAX_AGG_QS(bp) + \
					(BRB_SIZE(bp) - MAX_AGG_QS(bp)) / 2)
#define NUM_SGE_PG_REQ		((NUM_SGE_REQ + MAX_RX_SGE_CNT - 1) / \
						MAX_RX_SGE_CNT)
#define SGE_TH_LO(bp)		(NUM_SGE_REQ + \
				 NUM_SGE_PG_REQ * NEXT_PAGE_SGE_DESC_CNT)
#define SGE_TH_HI(bp)		(SGE_TH_LO(bp) + DROPLESS_FC_HEADROOM)

/* Manipulate a bit vector defined as an array of u64 */

/* Number of bits in one sge_mask array element */
#define BIT_VEC64_ELEM_SZ		64
#define BIT_VEC64_ELEM_SHIFT		6
#define BIT_VEC64_ELEM_MASK		((u64)BIT_VEC64_ELEM_SZ - 1)


#define __BIT_VEC64_SET_BIT(el, bit) \
	do { \
		el = ((el) | ((u64)0x1 << (bit))); \
	} while (0)

#define __BIT_VEC64_CLEAR_BIT(el, bit) \
	do { \
		el = ((el) & (~((u64)0x1 << (bit)))); \
	} while (0)


#define BIT_VEC64_SET_BIT(vec64, idx) \
	__BIT_VEC64_SET_BIT((vec64)[(idx) >> BIT_VEC64_ELEM_SHIFT], \
			   (idx) & BIT_VEC64_ELEM_MASK)

#define BIT_VEC64_CLEAR_BIT(vec64, idx) \
	__BIT_VEC64_CLEAR_BIT((vec64)[(idx) >> BIT_VEC64_ELEM_SHIFT], \
			     (idx) & BIT_VEC64_ELEM_MASK)

#define BIT_VEC64_TEST_BIT(vec64, idx) \
	(((vec64)[(idx) >> BIT_VEC64_ELEM_SHIFT] >> \
	((idx) & BIT_VEC64_ELEM_MASK)) & 0x1)

/* Creates a bitmask of all ones in less significant bits.
   idx - index of the most significant bit in the created mask */
#define BIT_VEC64_ONES_MASK(idx) \
		(((u64)0x1 << (((idx) & BIT_VEC64_ELEM_MASK) + 1)) - 1)
#define BIT_VEC64_ELEM_ONE_MASK	((u64)(~0))

/*******************************************************/



/* Number of u64 elements in SGE mask array */
#define RX_SGE_MASK_LEN			(NUM_RX_SGE / BIT_VEC64_ELEM_SZ)
#define RX_SGE_MASK_LEN_MASK		(RX_SGE_MASK_LEN - 1)
#define NEXT_SGE_MASK_ELEM(el)		(((el) + 1) & RX_SGE_MASK_LEN_MASK)

union host_hc_status_block {
	/* pointer to fp status block e1x */
	struct host_hc_status_block_e1x *e1x_sb;
	/* pointer to fp status block e2 */
	struct host_hc_status_block_e2  *e2_sb;
};

struct bnx2x_agg_info {
	/*
	 * First aggregation buffer is a data buffer, the following - are pages.
	 * We will preallocate the data buffer for each aggregation when
	 * we open the interface and will replace the BD at the consumer
	 * with this one when we receive the TPA_START CQE in order to
	 * keep the Rx BD ring consistent.
	 */
	struct sw_rx_bd		first_buf;
	u8			tpa_state;
#define BNX2X_TPA_START			1
#define BNX2X_TPA_STOP			2
#define BNX2X_TPA_ERROR			3
	u8			placement_offset;
	u16			parsing_flags;
	u16			vlan_tag;
	u16			len_on_bd;
	u32			rxhash;
	u16			gro_size;
	u16			full_page;
};

#define Q_STATS_OFFSET32(stat_name) \
			(offsetof(struct bnx2x_eth_q_stats, stat_name) / 4)

struct bnx2x_fp_txdata {

	struct sw_tx_bd		*tx_buf_ring;

	union eth_tx_bd_types	*tx_desc_ring;
	dma_addr_t		tx_desc_mapping;

	u32			cid;

	union db_prod		tx_db;

	u16			tx_pkt_prod;
	u16			tx_pkt_cons;
	u16			tx_bd_prod;
	u16			tx_bd_cons;

	unsigned long		tx_pkt;

	__le16			*tx_cons_sb;

	int			txq_index;
};

enum bnx2x_tpa_mode_t {
	TPA_MODE_LRO,
	TPA_MODE_GRO
};

struct bnx2x_fastpath {
	struct bnx2x		*bp; /* parent */

#define BNX2X_NAPI_WEIGHT       128
	struct napi_struct	napi;
	union host_hc_status_block	status_blk;
	/* chip independed shortcuts into sb structure */
	__le16			*sb_index_values;
	__le16			*sb_running_index;
	/* chip independed shortcut into rx_prods_offset memory */
	u32			ustorm_rx_prods_offset;

	u32			rx_buf_size;

	dma_addr_t		status_blk_mapping;

	enum bnx2x_tpa_mode_t	mode;

	u8			max_cos; /* actual number of active tx coses */
	struct bnx2x_fp_txdata	txdata[BNX2X_MULTI_TX_COS];

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

	u32			cid;

	__le16			fp_hc_idx;

	u8			index;		/* number in fp array */
	u8			rx_queue;	/* index for skb_record */
	u8			cl_id;		/* eth client id */
	u8			cl_qzone_id;
	u8			fw_sb_id;	/* status block number in FW */
	u8			igu_sb_id;	/* status block number in HW */

	u16			rx_bd_prod;
	u16			rx_bd_cons;
	u16			rx_comp_prod;
	u16			rx_comp_cons;
	u16			rx_sge_prod;
	/* The last maximal completed SGE */
	u16			last_max_sge;
	__le16			*rx_cons_sb;
	unsigned long		rx_pkt,
				rx_calls;

	/* TPA related */
	struct bnx2x_agg_info	tpa_info[ETH_MAX_AGGREGATION_QUEUES_E1H_E2];
	u8			disable_tpa;
#ifdef BNX2X_STOP_ON_ERROR
	u64			tpa_queue_used;
#endif

	struct tstorm_per_queue_stats old_tclient;
	struct ustorm_per_queue_stats old_uclient;
	struct xstorm_per_queue_stats old_xclient;
	struct bnx2x_eth_q_stats eth_q_stats;
	struct bnx2x_eth_q_stats_old eth_q_stats_old;

	/* The size is calculated using the following:
	     sizeof name field from netdev structure +
	     4 ('-Xx-' string) +
	     4 (for the digits and to make it DWORD aligned) */
#define FP_NAME_SIZE		(sizeof(((struct net_device *)0)->name) + 8)
	char			name[FP_NAME_SIZE];

	/* MACs object */
	struct bnx2x_vlan_mac_obj mac_obj;

	/* Queue State object */
	struct bnx2x_queue_sp_obj q_obj;

};

#define bnx2x_fp(bp, nr, var)		(bp->fp[nr].var)

/* Use 2500 as a mini-jumbo MTU for FCoE */
#define BNX2X_FCOE_MINI_JUMBO_MTU	2500

/* FCoE L2 `fastpath' entry is right after the eth entries */
#define FCOE_IDX			BNX2X_NUM_ETH_QUEUES(bp)
#define bnx2x_fcoe_fp(bp)		(&bp->fp[FCOE_IDX])
#define bnx2x_fcoe(bp, var)		(bnx2x_fcoe_fp(bp)->var)
#define bnx2x_fcoe_tx(bp, var)		(bnx2x_fcoe_fp(bp)-> \
						txdata[FIRST_TX_COS_INDEX].var)


#define IS_ETH_FP(fp)			(fp->index < \
					 BNX2X_NUM_ETH_QUEUES(fp->bp))
#ifdef BCM_CNIC
#define IS_FCOE_FP(fp)			(fp->index == FCOE_IDX)
#define IS_FCOE_IDX(idx)		((idx) == FCOE_IDX)
#else
#define IS_FCOE_FP(fp)		false
#define IS_FCOE_IDX(idx)	false
#endif


/* MC hsi */
#define MAX_FETCH_BD		13	/* HW max BDs per packet */
#define RX_COPY_THRESH		92

#define NUM_TX_RINGS		16
#define TX_DESC_CNT		(BCM_PAGE_SIZE / sizeof(union eth_tx_bd_types))
#define NEXT_PAGE_TX_DESC_CNT	1
#define MAX_TX_DESC_CNT		(TX_DESC_CNT - NEXT_PAGE_TX_DESC_CNT)
#define NUM_TX_BD		(TX_DESC_CNT * NUM_TX_RINGS)
#define MAX_TX_BD		(NUM_TX_BD - 1)
#define MAX_TX_AVAIL		(MAX_TX_DESC_CNT * NUM_TX_RINGS - 2)
#define NEXT_TX_IDX(x)		((((x) & MAX_TX_DESC_CNT) == \
				  (MAX_TX_DESC_CNT - 1)) ? \
					(x) + 1 + NEXT_PAGE_TX_DESC_CNT : \
					(x) + 1)
#define TX_BD(x)		((x) & MAX_TX_BD)
#define TX_BD_POFF(x)		((x) & MAX_TX_DESC_CNT)

/* The RX BD ring is special, each bd is 8 bytes but the last one is 16 */
#define NUM_RX_RINGS		8
#define RX_DESC_CNT		(BCM_PAGE_SIZE / sizeof(struct eth_rx_bd))
#define NEXT_PAGE_RX_DESC_CNT	2
#define MAX_RX_DESC_CNT		(RX_DESC_CNT - NEXT_PAGE_RX_DESC_CNT)
#define RX_DESC_MASK		(RX_DESC_CNT - 1)
#define NUM_RX_BD		(RX_DESC_CNT * NUM_RX_RINGS)
#define MAX_RX_BD		(NUM_RX_BD - 1)
#define MAX_RX_AVAIL		(MAX_RX_DESC_CNT * NUM_RX_RINGS - 2)

/* dropless fc calculations for BDs
 *
 * Number of BDs should as number of buffers in BRB:
 * Low threshold takes into account NEXT_PAGE_RX_DESC_CNT
 * "next" elements on each page
 */
#define NUM_BD_REQ		BRB_SIZE(bp)
#define NUM_BD_PG_REQ		((NUM_BD_REQ + MAX_RX_DESC_CNT - 1) / \
					      MAX_RX_DESC_CNT)
#define BD_TH_LO(bp)		(NUM_BD_REQ + \
				 NUM_BD_PG_REQ * NEXT_PAGE_RX_DESC_CNT + \
				 FW_DROP_LEVEL(bp))
#define BD_TH_HI(bp)		(BD_TH_LO(bp) + DROPLESS_FC_HEADROOM)

#define MIN_RX_AVAIL		((bp)->dropless_fc ? BD_TH_HI(bp) + 128 : 128)

#define MIN_RX_SIZE_TPA_HW	(CHIP_IS_E1(bp) ? \
					ETH_MIN_RX_CQES_WITH_TPA_E1 : \
					ETH_MIN_RX_CQES_WITH_TPA_E1H_E2)
#define MIN_RX_SIZE_NONTPA_HW   ETH_MIN_RX_CQES_WITHOUT_TPA
#define MIN_RX_SIZE_TPA		(max_t(u32, MIN_RX_SIZE_TPA_HW, MIN_RX_AVAIL))
#define MIN_RX_SIZE_NONTPA	(max_t(u32, MIN_RX_SIZE_NONTPA_HW,\
								MIN_RX_AVAIL))

#define NEXT_RX_IDX(x)		((((x) & RX_DESC_MASK) == \
				  (MAX_RX_DESC_CNT - 1)) ? \
					(x) + 1 + NEXT_PAGE_RX_DESC_CNT : \
					(x) + 1)
#define RX_BD(x)		((x) & MAX_RX_BD)

/*
 * As long as CQE is X times bigger than BD entry we have to allocate X times
 * more pages for CQ ring in order to keep it balanced with BD ring
 */
#define CQE_BD_REL	(sizeof(union eth_rx_cqe) / sizeof(struct eth_rx_bd))
#define NUM_RCQ_RINGS		(NUM_RX_RINGS * CQE_BD_REL)
#define RCQ_DESC_CNT		(BCM_PAGE_SIZE / sizeof(union eth_rx_cqe))
#define NEXT_PAGE_RCQ_DESC_CNT	1
#define MAX_RCQ_DESC_CNT	(RCQ_DESC_CNT - NEXT_PAGE_RCQ_DESC_CNT)
#define NUM_RCQ_BD		(RCQ_DESC_CNT * NUM_RCQ_RINGS)
#define MAX_RCQ_BD		(NUM_RCQ_BD - 1)
#define MAX_RCQ_AVAIL		(MAX_RCQ_DESC_CNT * NUM_RCQ_RINGS - 2)
#define NEXT_RCQ_IDX(x)		((((x) & MAX_RCQ_DESC_CNT) == \
				  (MAX_RCQ_DESC_CNT - 1)) ? \
					(x) + 1 + NEXT_PAGE_RCQ_DESC_CNT : \
					(x) + 1)
#define RCQ_BD(x)		((x) & MAX_RCQ_BD)

/* dropless fc calculations for RCQs
 *
 * Number of RCQs should be as number of buffers in BRB:
 * Low threshold takes into account NEXT_PAGE_RCQ_DESC_CNT
 * "next" elements on each page
 */
#define NUM_RCQ_REQ		BRB_SIZE(bp)
#define NUM_RCQ_PG_REQ		((NUM_BD_REQ + MAX_RCQ_DESC_CNT - 1) / \
					      MAX_RCQ_DESC_CNT)
#define RCQ_TH_LO(bp)		(NUM_RCQ_REQ + \
				 NUM_RCQ_PG_REQ * NEXT_PAGE_RCQ_DESC_CNT + \
				 FW_DROP_LEVEL(bp))
#define RCQ_TH_HI(bp)		(RCQ_TH_LO(bp) + DROPLESS_FC_HEADROOM)


/* This is needed for determining of last_max */
#define SUB_S16(a, b)		(s16)((s16)(a) - (s16)(b))
#define SUB_S32(a, b)		(s32)((s32)(a) - (s32)(b))


#define BNX2X_SWCID_SHIFT	17
#define BNX2X_SWCID_MASK	((0x1 << BNX2X_SWCID_SHIFT) - 1)

/* used on a CID received from the HW */
#define SW_CID(x)			(le32_to_cpu(x) & BNX2X_SWCID_MASK)
#define CQE_CMD(x)			(le32_to_cpu(x) >> \
					COMMON_RAMROD_ETH_RX_CQE_CMD_ID_SHIFT)

#define BD_UNMAP_ADDR(bd)		HILO_U64(le32_to_cpu((bd)->addr_hi), \
						 le32_to_cpu((bd)->addr_lo))
#define BD_UNMAP_LEN(bd)		(le16_to_cpu((bd)->nbytes))

#define BNX2X_DB_MIN_SHIFT		3	/* 8 bytes */
#define BNX2X_DB_SHIFT			7	/* 128 bytes*/
#if (BNX2X_DB_SHIFT < BNX2X_DB_MIN_SHIFT)
#error "Min DB doorbell stride is 8"
#endif
#define DPM_TRIGER_TYPE			0x40
#define DOORBELL(bp, cid, val) \
	do { \
		writel((u32)(val), bp->doorbells + (bp->db_size * (cid)) + \
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
#define CQE_TYPE(cqe_fp_flags)	 ((cqe_fp_flags) & ETH_FAST_PATH_RX_CQE_TYPE)
#define CQE_TYPE_START(cqe_type) ((cqe_type) == RX_ETH_CQE_TYPE_ETH_START_AGG)
#define CQE_TYPE_STOP(cqe_type)  ((cqe_type) == RX_ETH_CQE_TYPE_ETH_STOP_AGG)
#define CQE_TYPE_SLOW(cqe_type)  ((cqe_type) == RX_ETH_CQE_TYPE_ETH_RAMROD)
#define CQE_TYPE_FAST(cqe_type)  ((cqe_type) == RX_ETH_CQE_TYPE_ETH_FASTPATH)

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


#define FP_USB_FUNC_OFF	\
			offsetof(struct cstorm_status_block_u, func)
#define FP_CSB_FUNC_OFF	\
			offsetof(struct cstorm_status_block_c, func)

#define HC_INDEX_ETH_RX_CQ_CONS		1

#define HC_INDEX_OOO_TX_CQ_CONS		4

#define HC_INDEX_ETH_TX_CQ_CONS_COS0	5

#define HC_INDEX_ETH_TX_CQ_CONS_COS1	6

#define HC_INDEX_ETH_TX_CQ_CONS_COS2	7

#define HC_INDEX_ETH_FIRST_TX_CQ_CONS	HC_INDEX_ETH_TX_CQ_CONS_COS0

#define BNX2X_RX_SB_INDEX \
	(&fp->sb_index_values[HC_INDEX_ETH_RX_CQ_CONS])

#define BNX2X_TX_SB_INDEX_BASE BNX2X_TX_SB_INDEX_COS0

#define BNX2X_TX_SB_INDEX_COS0 \
	(&fp->sb_index_values[HC_INDEX_ETH_TX_CQ_CONS_COS0])

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
#define CHIP_NUM_57712			0x1662
#define CHIP_NUM_57712_MF		0x1663
#define CHIP_NUM_57713			0x1651
#define CHIP_NUM_57713E			0x1652
#define CHIP_NUM_57800			0x168a
#define CHIP_NUM_57800_MF		0x16a5
#define CHIP_NUM_57810			0x168e
#define CHIP_NUM_57810_MF		0x16ae
#define CHIP_NUM_57811			0x163d
#define CHIP_NUM_57811_MF		0x163e
#define CHIP_NUM_57840			0x168d
#define CHIP_NUM_57840_MF		0x16ab
#define CHIP_IS_E1(bp)			(CHIP_NUM(bp) == CHIP_NUM_57710)
#define CHIP_IS_57711(bp)		(CHIP_NUM(bp) == CHIP_NUM_57711)
#define CHIP_IS_57711E(bp)		(CHIP_NUM(bp) == CHIP_NUM_57711E)
#define CHIP_IS_57712(bp)		(CHIP_NUM(bp) == CHIP_NUM_57712)
#define CHIP_IS_57712_MF(bp)		(CHIP_NUM(bp) == CHIP_NUM_57712_MF)
#define CHIP_IS_57800(bp)		(CHIP_NUM(bp) == CHIP_NUM_57800)
#define CHIP_IS_57800_MF(bp)		(CHIP_NUM(bp) == CHIP_NUM_57800_MF)
#define CHIP_IS_57810(bp)		(CHIP_NUM(bp) == CHIP_NUM_57810)
#define CHIP_IS_57810_MF(bp)		(CHIP_NUM(bp) == CHIP_NUM_57810_MF)
#define CHIP_IS_57811(bp)		(CHIP_NUM(bp) == CHIP_NUM_57811)
#define CHIP_IS_57811_MF(bp)		(CHIP_NUM(bp) == CHIP_NUM_57811_MF)
#define CHIP_IS_57840(bp)		(CHIP_NUM(bp) == CHIP_NUM_57840)
#define CHIP_IS_57840_MF(bp)		(CHIP_NUM(bp) == CHIP_NUM_57840_MF)
#define CHIP_IS_E1H(bp)			(CHIP_IS_57711(bp) || \
					 CHIP_IS_57711E(bp))
#define CHIP_IS_E2(bp)			(CHIP_IS_57712(bp) || \
					 CHIP_IS_57712_MF(bp))
#define CHIP_IS_E3(bp)			(CHIP_IS_57800(bp) || \
					 CHIP_IS_57800_MF(bp) || \
					 CHIP_IS_57810(bp) || \
					 CHIP_IS_57810_MF(bp) || \
					 CHIP_IS_57811(bp) || \
					 CHIP_IS_57811_MF(bp) || \
					 CHIP_IS_57840(bp) || \
					 CHIP_IS_57840_MF(bp))
#define CHIP_IS_E1x(bp)			(CHIP_IS_E1((bp)) || CHIP_IS_E1H((bp)))
#define USES_WARPCORE(bp)		(CHIP_IS_E3(bp))
#define IS_E1H_OFFSET			(!CHIP_IS_E1(bp))

#define CHIP_REV_SHIFT			12
#define CHIP_REV_MASK			(0xF << CHIP_REV_SHIFT)
#define CHIP_REV_VAL(bp)		(bp->common.chip_id & CHIP_REV_MASK)
#define CHIP_REV_Ax			(0x0 << CHIP_REV_SHIFT)
#define CHIP_REV_Bx			(0x1 << CHIP_REV_SHIFT)
/* assume maximum 5 revisions */
#define CHIP_REV_IS_SLOW(bp)		(CHIP_REV_VAL(bp) > 0x00005000)
/* Emul versions are A=>0xe, B=>0xc, C=>0xa, D=>8, E=>6 */
#define CHIP_REV_IS_EMUL(bp)		((CHIP_REV_IS_SLOW(bp)) && \
					 !(CHIP_REV_VAL(bp) & 0x00001000))
/* FPGA versions are A=>0xf, B=>0xd, C=>0xb, D=>9, E=>7 */
#define CHIP_REV_IS_FPGA(bp)		((CHIP_REV_IS_SLOW(bp)) && \
					 (CHIP_REV_VAL(bp) & 0x00001000))

#define CHIP_TIME(bp)			((CHIP_REV_IS_EMUL(bp)) ? 2000 : \
					((CHIP_REV_IS_FPGA(bp)) ? 200 : 1))

#define CHIP_METAL(bp)			(bp->common.chip_id & 0x00000ff0)
#define CHIP_BOND_ID(bp)		(bp->common.chip_id & 0x0000000f)
#define CHIP_REV_SIM(bp)		(((CHIP_REV_MASK - CHIP_REV_VAL(bp)) >>\
					   (CHIP_REV_SHIFT + 1)) \
						<< CHIP_REV_SHIFT)
#define CHIP_REV(bp)			(CHIP_REV_IS_SLOW(bp) ? \
						CHIP_REV_SIM(bp) :\
						CHIP_REV_VAL(bp))
#define CHIP_IS_E3B0(bp)		(CHIP_IS_E3(bp) && \
					 (CHIP_REV(bp) == CHIP_REV_Bx))
#define CHIP_IS_E3A0(bp)		(CHIP_IS_E3(bp) && \
					 (CHIP_REV(bp) == CHIP_REV_Ax))

	int			flash_size;
#define BNX2X_NVRAM_1MB_SIZE			0x20000	/* 1M bit in bytes */
#define BNX2X_NVRAM_TIMEOUT_COUNT		30000
#define BNX2X_NVRAM_PAGE_SIZE			256

	u32			shmem_base;
	u32			shmem2_base;
	u32			mf_cfg_base;
	u32			mf2_cfg_base;

	u32			hw_config;

	u32			bc_ver;

	u8			int_block;
#define INT_BLOCK_HC			0
#define INT_BLOCK_IGU			1
#define INT_BLOCK_MODE_NORMAL		0
#define INT_BLOCK_MODE_BW_COMP		2
#define CHIP_INT_MODE_IS_NBC(bp)		\
			(!CHIP_IS_E1x(bp) &&	\
			!((bp)->common.int_block & INT_BLOCK_MODE_BW_COMP))
#define CHIP_INT_MODE_IS_BC(bp) (!CHIP_INT_MODE_IS_NBC(bp))

	u8			chip_port_mode;
#define CHIP_4_PORT_MODE			0x0
#define CHIP_2_PORT_MODE			0x1
#define CHIP_PORT_MODE_NONE			0x2
#define CHIP_MODE(bp)			(bp->common.chip_port_mode)
#define CHIP_MODE_IS_4_PORT(bp) (CHIP_MODE(bp) == CHIP_4_PORT_MODE)

	u32			boot_mode;
};

/* IGU MSIX STATISTICS on 57712: 64 for VFs; 4 for PFs; 4 for Attentions */
#define BNX2X_IGU_STAS_MSG_VF_CNT 64
#define BNX2X_IGU_STAS_MSG_PF_CNT 4

/* end of common */

/* port */

struct bnx2x_port {
	u32			pmf;

	u32			link_config[LINK_CONFIG_SIZE];

	u32			supported[LINK_CONFIG_SIZE];
/* link settings - missing defines */
#define SUPPORTED_2500baseX_Full	(1 << 15)

	u32			advertising[LINK_CONFIG_SIZE];
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

#define STATS_OFFSET32(stat_name) \
			(offsetof(struct bnx2x_eth_stats, stat_name) / 4)

/* slow path */

/* slow path work-queue */
extern struct workqueue_struct *bnx2x_wq;

#define BNX2X_MAX_NUM_OF_VFS	64
#define BNX2X_VF_ID_INVALID	0xFF

/*
 * The total number of L2 queues, MSIX vectors and HW contexts (CIDs) is
 * control by the number of fast-path status blocks supported by the
 * device (HW/FW). Each fast-path status block (FP-SB) aka non-default
 * status block represents an independent interrupts context that can
 * serve a regular L2 networking queue. However special L2 queues such
 * as the FCoE queue do not require a FP-SB and other components like
 * the CNIC may consume FP-SB reducing the number of possible L2 queues
 *
 * If the maximum number of FP-SB available is X then:
 * a. If CNIC is supported it consumes 1 FP-SB thus the max number of
 *    regular L2 queues is Y=X-1
 * b. in MF mode the actual number of L2 queues is Y= (X-1/MF_factor)
 * c. If the FCoE L2 queue is supported the actual number of L2 queues
 *    is Y+1
 * d. The number of irqs (MSIX vectors) is either Y+1 (one extra for
 *    slow-path interrupts) or Y+2 if CNIC is supported (one additional
 *    FP interrupt context for the CNIC).
 * e. The number of HW context (CID count) is always X or X+1 if FCoE
 *    L2 queue is supported. the cid for the FCoE L2 queue is always X.
 */

/* fast-path interrupt contexts E1x */
#define FP_SB_MAX_E1x		16
/* fast-path interrupt contexts E2 */
#define FP_SB_MAX_E2		HC_SB_MAX_SB_E2

union cdu_context {
	struct eth_context eth;
	char pad[1024];
};

/* CDU host DB constants */
#define CDU_ILT_PAGE_SZ_HW	3
#define CDU_ILT_PAGE_SZ		(8192 << CDU_ILT_PAGE_SZ_HW) /* 64K */
#define ILT_PAGE_CIDS		(CDU_ILT_PAGE_SZ / sizeof(union cdu_context))

#ifdef BCM_CNIC
#define CNIC_ISCSI_CID_MAX	256
#define CNIC_FCOE_CID_MAX	2048
#define CNIC_CID_MAX		(CNIC_ISCSI_CID_MAX + CNIC_FCOE_CID_MAX)
#define CNIC_ILT_LINES		DIV_ROUND_UP(CNIC_CID_MAX, ILT_PAGE_CIDS)
#endif

#define QM_ILT_PAGE_SZ_HW	0
#define QM_ILT_PAGE_SZ		(4096 << QM_ILT_PAGE_SZ_HW) /* 4K */
#define QM_CID_ROUND		1024

#ifdef BCM_CNIC
/* TM (timers) host DB constants */
#define TM_ILT_PAGE_SZ_HW	0
#define TM_ILT_PAGE_SZ		(4096 << TM_ILT_PAGE_SZ_HW) /* 4K */
/* #define TM_CONN_NUM		(CNIC_STARTING_CID+CNIC_ISCSI_CXT_MAX) */
#define TM_CONN_NUM		1024
#define TM_ILT_SZ		(8 * TM_CONN_NUM)
#define TM_ILT_LINES		DIV_ROUND_UP(TM_ILT_SZ, TM_ILT_PAGE_SZ)

/* SRC (Searcher) host DB constants */
#define SRC_ILT_PAGE_SZ_HW	0
#define SRC_ILT_PAGE_SZ		(4096 << SRC_ILT_PAGE_SZ_HW) /* 4K */
#define SRC_HASH_BITS		10
#define SRC_CONN_NUM		(1 << SRC_HASH_BITS) /* 1024 */
#define SRC_ILT_SZ		(sizeof(struct src_ent) * SRC_CONN_NUM)
#define SRC_T2_SZ		SRC_ILT_SZ
#define SRC_ILT_LINES		DIV_ROUND_UP(SRC_ILT_SZ, SRC_ILT_PAGE_SZ)

#endif

#define MAX_DMAE_C		8

/* DMA memory not used in fastpath */
struct bnx2x_slowpath {
	union {
		struct mac_configuration_cmd		e1x;
		struct eth_classify_rules_ramrod_data	e2;
	} mac_rdata;


	union {
		struct tstorm_eth_mac_filter_config	e1x;
		struct eth_filter_rules_ramrod_data	e2;
	} rx_mode_rdata;

	union {
		struct mac_configuration_cmd		e1;
		struct eth_multicast_rules_ramrod_data  e2;
	} mcast_rdata;

	struct eth_rss_update_ramrod_data	rss_rdata;

	/* Queue State related ramrods are always sent under rtnl_lock */
	union {
		struct client_init_ramrod_data  init_data;
		struct client_update_ramrod_data update_data;
	} q_rdata;

	union {
		struct function_start_data	func_start;
		/* pfc configuration for DCBX ramrod */
		struct flow_control_configuration pfc_config;
	} func_rdata;

	/* afex ramrod can not be a part of func_rdata union because these
	 * events might arrive in parallel to other events from func_rdata.
	 * Therefore, if they would have been defined in the same union,
	 * data can get corrupted.
	 */
	struct afex_vif_list_ramrod_data func_afex_rdata;

	/* used by dmae command executer */
	struct dmae_command		dmae[MAX_DMAE_C];

	u32				stats_comp;
	union mac_stats			mac_stats;
	struct nig_stats		nig_stats;
	struct host_port_stats		port_stats;
	struct host_func_stats		func_stats;

	u32				wb_comp;
	u32				wb_data[4];

	union drv_info_to_mcp		drv_info_to_mcp;
};

#define bnx2x_sp(bp, var)		(&bp->slowpath->var)
#define bnx2x_sp_mapping(bp, var) \
		(bp->slowpath_mapping + offsetof(struct bnx2x_slowpath, var))


/* attn group wiring */
#define MAX_DYNAMIC_ATTN_GRPS		8

struct attn_route {
	u32 sig[5];
};

struct iro {
	u32 base;
	u16 m1;
	u16 m2;
	u16 m3;
	u16 size;
};

struct hw_context {
	union cdu_context *vcxt;
	dma_addr_t cxt_mapping;
	size_t size;
};

/* forward */
struct bnx2x_ilt;


enum bnx2x_recovery_state {
	BNX2X_RECOVERY_DONE,
	BNX2X_RECOVERY_INIT,
	BNX2X_RECOVERY_WAIT,
	BNX2X_RECOVERY_FAILED,
	BNX2X_RECOVERY_NIC_LOADING
};

/*
 * Event queue (EQ or event ring) MC hsi
 * NUM_EQ_PAGES and EQ_DESC_CNT_PAGE must be power of 2
 */
#define NUM_EQ_PAGES		1
#define EQ_DESC_CNT_PAGE	(BCM_PAGE_SIZE / sizeof(union event_ring_elem))
#define EQ_DESC_MAX_PAGE	(EQ_DESC_CNT_PAGE - 1)
#define NUM_EQ_DESC		(EQ_DESC_CNT_PAGE * NUM_EQ_PAGES)
#define EQ_DESC_MASK		(NUM_EQ_DESC - 1)
#define MAX_EQ_AVAIL		(EQ_DESC_MAX_PAGE * NUM_EQ_PAGES - 2)

/* depends on EQ_DESC_CNT_PAGE being a power of 2 */
#define NEXT_EQ_IDX(x)		((((x) & EQ_DESC_MAX_PAGE) == \
				  (EQ_DESC_MAX_PAGE - 1)) ? (x) + 2 : (x) + 1)

/* depends on the above and on NUM_EQ_PAGES being a power of 2 */
#define EQ_DESC(x)		((x) & EQ_DESC_MASK)

#define BNX2X_EQ_INDEX \
	(&bp->def_status_blk->sp_sb.\
	index_values[HC_SP_INDEX_EQ_CONS])

/* This is a data that will be used to create a link report message.
 * We will keep the data used for the last link report in order
 * to prevent reporting the same link parameters twice.
 */
struct bnx2x_link_report_data {
	u16 line_speed;			/* Effective line speed */
	unsigned long link_report_flags;/* BNX2X_LINK_REPORT_XXX flags */
};

enum {
	BNX2X_LINK_REPORT_FD,		/* Full DUPLEX */
	BNX2X_LINK_REPORT_LINK_DOWN,
	BNX2X_LINK_REPORT_RX_FC_ON,
	BNX2X_LINK_REPORT_TX_FC_ON,
};

enum {
	BNX2X_PORT_QUERY_IDX,
	BNX2X_PF_QUERY_IDX,
	BNX2X_FCOE_QUERY_IDX,
	BNX2X_FIRST_QUEUE_QUERY_IDX,
};

struct bnx2x_fw_stats_req {
	struct stats_query_header hdr;
	struct stats_query_entry query[FP_SB_MAX_E1x+
		BNX2X_FIRST_QUEUE_QUERY_IDX];
};

struct bnx2x_fw_stats_data {
	struct stats_counter	storm_counters;
	struct per_port_stats	port;
	struct per_pf_stats	pf;
	struct fcoe_statistics_params	fcoe;
	struct per_queue_stats  queue_stats[1];
};

/* Public slow path states */
enum {
	BNX2X_SP_RTNL_SETUP_TC,
	BNX2X_SP_RTNL_TX_TIMEOUT,
	BNX2X_SP_RTNL_AFEX_F_UPDATE,
	BNX2X_SP_RTNL_FAN_FAILURE,
};


struct bnx2x_prev_path_list {
	u8 bus;
	u8 slot;
	u8 path;
	struct list_head list;
};

struct bnx2x {
	/* Fields used in the tx and intr/napi performance paths
	 * are grouped together in the beginning of the structure
	 */
	struct bnx2x_fastpath	*fp;
	void __iomem		*regview;
	void __iomem		*doorbells;
	u16			db_size;

	u8			pf_num;	/* absolute PF number */
	u8			pfid;	/* per-path PF number */
	int			base_fw_ndsb; /**/
#define BP_PATH(bp)			(CHIP_IS_E1x(bp) ? 0 : (bp->pf_num & 1))
#define BP_PORT(bp)			(bp->pfid & 1)
#define BP_FUNC(bp)			(bp->pfid)
#define BP_ABS_FUNC(bp)			(bp->pf_num)
#define BP_VN(bp)			((bp)->pfid >> 1)
#define BP_MAX_VN_NUM(bp)		(CHIP_MODE_IS_4_PORT(bp) ? 2 : 4)
#define BP_L_ID(bp)			(BP_VN(bp) << 2)
#define BP_FW_MB_IDX_VN(bp, vn)		(BP_PORT(bp) +\
	  (vn) * ((CHIP_IS_E1x(bp) || (CHIP_MODE_IS_4_PORT(bp))) ? 2  : 1))
#define BP_FW_MB_IDX(bp)		BP_FW_MB_IDX_VN(bp, BP_VN(bp))

	struct net_device	*dev;
	struct pci_dev		*pdev;

	const struct iro	*iro_arr;
#define IRO (bp->iro_arr)

	enum bnx2x_recovery_state recovery_state;
	int			is_leader;
	struct msix_entry	*msix_table;

	int			tx_ring_size;

/* L2 header size + 2*VLANs (8 bytes) + LLC SNAP (8 bytes) */
#define ETH_OVREHEAD		(ETH_HLEN + 8 + 8)
#define ETH_MIN_PACKET_SIZE		60
#define ETH_MAX_PACKET_SIZE		1500
#define ETH_MAX_JUMBO_PACKET_SIZE	9600
/* TCP with Timestamp Option (32) + IPv6 (40) */
#define ETH_MAX_TPA_HEADER_SIZE		72

	/* Max supported alignment is 256 (8 shift) */
#define BNX2X_RX_ALIGN_SHIFT		min(8, L1_CACHE_SHIFT)

	/* FW uses 2 Cache lines Alignment for start packet and size
	 *
	 * We assume skb_build() uses sizeof(struct skb_shared_info) bytes
	 * at the end of skb->data, to avoid wasting a full cache line.
	 * This reduces memory use (skb->truesize).
	 */
#define BNX2X_FW_RX_ALIGN_START	(1UL << BNX2X_RX_ALIGN_SHIFT)

#define BNX2X_FW_RX_ALIGN_END					\
	max(1UL << BNX2X_RX_ALIGN_SHIFT, 			\
	    SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

#define BNX2X_PXP_DRAM_ALIGN		(BNX2X_RX_ALIGN_SHIFT - 5)

	struct host_sp_status_block *def_status_blk;
#define DEF_SB_IGU_ID			16
#define DEF_SB_ID			HC_SP_SB_ID
	__le16			def_idx;
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
	atomic_t		cq_spq_left; /* ETH_XXX ramrods credit */
	/* used to synchronize spq accesses */
	spinlock_t		spq_lock;

	/* event queue */
	union event_ring_elem	*eq_ring;
	dma_addr_t		eq_mapping;
	u16			eq_prod;
	u16			eq_cons;
	__le16			*eq_cons_sb;
	atomic_t		eq_spq_left; /* COMMON_XXX ramrods credit */



	/* Counter for marking that there is a STAT_QUERY ramrod pending */
	u16			stats_pending;
	/*  Counter for completed statistics ramrods */
	u16			stats_comp;

	/* End of fields used in the performance code paths */

	int			panic;
	int			msg_enable;

	u32			flags;
#define PCIX_FLAG			(1 << 0)
#define PCI_32BIT_FLAG			(1 << 1)
#define ONE_PORT_FLAG			(1 << 2)
#define NO_WOL_FLAG			(1 << 3)
#define USING_DAC_FLAG			(1 << 4)
#define USING_MSIX_FLAG			(1 << 5)
#define USING_MSI_FLAG			(1 << 6)
#define DISABLE_MSI_FLAG		(1 << 7)
#define TPA_ENABLE_FLAG			(1 << 8)
#define NO_MCP_FLAG			(1 << 9)

#define BP_NOMCP(bp)			(bp->flags & NO_MCP_FLAG)
#define GRO_ENABLE_FLAG			(1 << 10)
#define MF_FUNC_DIS			(1 << 11)
#define OWN_CNIC_IRQ			(1 << 12)
#define NO_ISCSI_OOO_FLAG		(1 << 13)
#define NO_ISCSI_FLAG			(1 << 14)
#define NO_FCOE_FLAG			(1 << 15)
#define BC_SUPPORTS_PFC_STATS		(1 << 17)
#define USING_SINGLE_MSIX_FLAG		(1 << 20)

#define NO_ISCSI(bp)		((bp)->flags & NO_ISCSI_FLAG)
#define NO_ISCSI_OOO(bp)	((bp)->flags & NO_ISCSI_OOO_FLAG)
#define NO_FCOE(bp)		((bp)->flags & NO_FCOE_FLAG)

	int			pm_cap;
	int			mrrs;

	struct delayed_work	sp_task;
	struct delayed_work	sp_rtnl_task;

	struct delayed_work	period_task;
	struct timer_list	timer;
	int			current_interval;

	u16			fw_seq;
	u16			fw_drv_pulse_wr_seq;
	u32			func_stx;

	struct link_params	link_params;
	struct link_vars	link_vars;
	u32			link_cnt;
	struct bnx2x_link_report_data last_reported_link;

	struct mdio_if_info	mdio;

	struct bnx2x_common	common;
	struct bnx2x_port	port;

	struct cmng_init	cmng;

	u32			mf_config[E1HVN_MAX];
	u32			mf_ext_config;
	u32			path_has_ovlan; /* E3 */
	u16			mf_ov;
	u8			mf_mode;
#define IS_MF(bp)		(bp->mf_mode != 0)
#define IS_MF_SI(bp)		(bp->mf_mode == MULTI_FUNCTION_SI)
#define IS_MF_SD(bp)		(bp->mf_mode == MULTI_FUNCTION_SD)
#define IS_MF_AFEX(bp)		(bp->mf_mode == MULTI_FUNCTION_AFEX)

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
/* Maximal coalescing timeout in us */
#define BNX2X_MAX_COALESCE_TOUT		(0xf0*12)

	u32			lin_cnt;

	u16			state;
#define BNX2X_STATE_CLOSED		0
#define BNX2X_STATE_OPENING_WAIT4_LOAD	0x1000
#define BNX2X_STATE_OPENING_WAIT4_PORT	0x2000
#define BNX2X_STATE_OPEN		0x3000
#define BNX2X_STATE_CLOSING_WAIT4_HALT	0x4000
#define BNX2X_STATE_CLOSING_WAIT4_DELETE 0x5000

#define BNX2X_STATE_DIAG		0xe000
#define BNX2X_STATE_ERROR		0xf000

#define BNX2X_MAX_PRIORITY		8
#define BNX2X_MAX_ENTRIES_PER_PRI	16
#define BNX2X_MAX_COS			3
#define BNX2X_MAX_TX_COS		2
	int			num_queues;
	int			disable_tpa;

	u32			rx_mode;
#define BNX2X_RX_MODE_NONE		0
#define BNX2X_RX_MODE_NORMAL		1
#define BNX2X_RX_MODE_ALLMULTI		2
#define BNX2X_RX_MODE_PROMISC		3
#define BNX2X_MAX_MULTICAST		64

	u8			igu_dsb_id;
	u8			igu_base_sb;
	u8			igu_sb_cnt;
	dma_addr_t		def_status_blk_mapping;

	struct bnx2x_slowpath	*slowpath;
	dma_addr_t		slowpath_mapping;

	/* Total number of FW statistics requests */
	u8			fw_stats_num;

	/*
	 * This is a memory buffer that will contain both statistics
	 * ramrod request and data.
	 */
	void			*fw_stats;
	dma_addr_t		fw_stats_mapping;

	/*
	 * FW statistics request shortcut (points at the
	 * beginning of fw_stats buffer).
	 */
	struct bnx2x_fw_stats_req	*fw_stats_req;
	dma_addr_t			fw_stats_req_mapping;
	int				fw_stats_req_sz;

	/*
	 * FW statistics data shortcut (points at the begining of
	 * fw_stats buffer + fw_stats_req_sz).
	 */
	struct bnx2x_fw_stats_data	*fw_stats_data;
	dma_addr_t			fw_stats_data_mapping;
	int				fw_stats_data_sz;

	struct hw_context	context;

	struct bnx2x_ilt	*ilt;
#define BP_ILT(bp)		((bp)->ilt)
#define ILT_MAX_LINES		256
/*
 * Maximum supported number of RSS queues: number of IGU SBs minus one that goes
 * to CNIC.
 */
#define BNX2X_MAX_RSS_COUNT(bp)	((bp)->igu_sb_cnt - CNIC_PRESENT)

/*
 * Maximum CID count that might be required by the bnx2x:
 * Max Tss * Max_Tx_Multi_Cos + CNIC L2 Clients (FCoE and iSCSI related)
 */
#define BNX2X_L2_CID_COUNT(bp)	(MAX_TXQS_PER_COS * BNX2X_MULTI_TX_COS +\
					NON_ETH_CONTEXT_USE + CNIC_PRESENT)
#define L2_ILT_LINES(bp)	(DIV_ROUND_UP(BNX2X_L2_CID_COUNT(bp),\
					ILT_PAGE_CIDS))
#define BNX2X_DB_SIZE(bp)	(BNX2X_L2_CID_COUNT(bp) * (1 << BNX2X_DB_SHIFT))

	int			qm_cid_count;

	int			dropless_fc;

#ifdef BCM_CNIC
	u32			cnic_flags;
#define BNX2X_CNIC_FLAG_MAC_SET		1
	void			*t2;
	dma_addr_t		t2_mapping;
	struct cnic_ops	__rcu	*cnic_ops;
	void			*cnic_data;
	u32			cnic_tag;
	struct cnic_eth_dev	cnic_eth_dev;
	union host_hc_status_block cnic_sb;
	dma_addr_t		cnic_sb_mapping;
	struct eth_spe		*cnic_kwq;
	struct eth_spe		*cnic_kwq_prod;
	struct eth_spe		*cnic_kwq_cons;
	struct eth_spe		*cnic_kwq_last;
	u16			cnic_kwq_pending;
	u16			cnic_spq_pending;
	u8			fip_mac[ETH_ALEN];
	struct mutex		cnic_mutex;
	struct bnx2x_vlan_mac_obj iscsi_l2_mac_obj;

	/* Start index of the "special" (CNIC related) L2 cleints */
	u8				cnic_base_cl_id;
#endif

	int			dmae_ready;
	/* used to synchronize dmae accesses */
	spinlock_t		dmae_lock;

	/* used to protect the FW mail box */
	struct mutex		fw_mb_mutex;

	/* used to synchronize stats collecting */
	int			stats_state;

	/* used for synchronization of concurrent threads statistics handling */
	spinlock_t		stats_lock;

	/* used by dmae command loader */
	struct dmae_command	stats_dmae;
	int			executer_idx;

	u16			stats_counter;
	struct bnx2x_eth_stats	eth_stats;
	struct host_func_stats		func_stats;
	struct bnx2x_eth_stats_old	eth_stats_old;
	struct bnx2x_net_stats_old	net_stats_old;
	struct bnx2x_fw_port_stats_old	fw_stats_old;
	bool			stats_init;

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
	u32			init_mode_flags;
#define INIT_MODE_FLAGS(bp)	(bp->init_mode_flags)
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

#define PHY_FW_VER_LEN			20
	char			fw_ver[32];
	const struct firmware	*firmware;

	/* DCB support on/off */
	u16 dcb_state;
#define BNX2X_DCB_STATE_OFF			0
#define BNX2X_DCB_STATE_ON			1

	/* DCBX engine mode */
	int dcbx_enabled;
#define BNX2X_DCBX_ENABLED_OFF			0
#define BNX2X_DCBX_ENABLED_ON_NEG_OFF		1
#define BNX2X_DCBX_ENABLED_ON_NEG_ON		2
#define BNX2X_DCBX_ENABLED_INVALID		(-1)

	bool dcbx_mode_uset;

	struct bnx2x_config_dcbx_params		dcbx_config_params;
	struct bnx2x_dcbx_port_params		dcbx_port_params;
	int					dcb_version;

	/* CAM credit pools */
	struct bnx2x_credit_pool_obj		macs_pool;

	/* RX_MODE object */
	struct bnx2x_rx_mode_obj		rx_mode_obj;

	/* MCAST object */
	struct bnx2x_mcast_obj			mcast_obj;

	/* RSS configuration object */
	struct bnx2x_rss_config_obj		rss_conf_obj;

	/* Function State controlling object */
	struct bnx2x_func_sp_obj		func_obj;

	unsigned long				sp_state;

	/* operation indication for the sp_rtnl task */
	unsigned long				sp_rtnl_state;

	/* DCBX Negotation results */
	struct dcbx_features			dcbx_local_feat;
	u32					dcbx_error;

#ifdef BCM_DCBNL
	struct dcbx_features			dcbx_remote_feat;
	u32					dcbx_remote_flags;
#endif
	/* AFEX: store default vlan used */
	int					afex_def_vlan_tag;
	enum mf_cfg_afex_vlan_mode		afex_vlan_mode;
	u32					pending_max;

	/* multiple tx classes of service */
	u8					max_cos;

	/* priority to cos mapping */
	u8					prio_to_cos[8];
};

/* Tx queues may be less or equal to Rx queues */
extern int num_queues;
#define BNX2X_NUM_QUEUES(bp)	(bp->num_queues)
#define BNX2X_NUM_ETH_QUEUES(bp) (BNX2X_NUM_QUEUES(bp) - NON_ETH_CONTEXT_USE)
#define BNX2X_NUM_RX_QUEUES(bp)	BNX2X_NUM_QUEUES(bp)

#define is_multi(bp)		(BNX2X_NUM_QUEUES(bp) > 1)

#define BNX2X_MAX_QUEUES(bp)	BNX2X_MAX_RSS_COUNT(bp)
/* #define is_eth_multi(bp)	(BNX2X_NUM_ETH_QUEUES(bp) > 1) */

#define RSS_IPV4_CAP_MASK						\
	TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_CAPABILITY

#define RSS_IPV4_TCP_CAP_MASK						\
	TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_TCP_CAPABILITY

#define RSS_IPV6_CAP_MASK						\
	TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_CAPABILITY

#define RSS_IPV6_TCP_CAP_MASK						\
	TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_TCP_CAPABILITY

/* func init flags */
#define FUNC_FLG_RSS		0x0001
#define FUNC_FLG_STATS		0x0002
/* removed  FUNC_FLG_UNMATCHED	0x0004 */
#define FUNC_FLG_TPA		0x0008
#define FUNC_FLG_SPQ		0x0010
#define FUNC_FLG_LEADING	0x0020	/* PF only */


struct bnx2x_func_init_params {
	/* dma */
	dma_addr_t	fw_stat_map;	/* valid iff FUNC_FLG_STATS */
	dma_addr_t	spq_map;	/* valid iff FUNC_FLG_SPQ */

	u16		func_flgs;
	u16		func_id;	/* abs fid */
	u16		pf_id;
	u16		spq_prod;	/* valid iff FUNC_FLG_SPQ */
};

#define for_each_eth_queue(bp, var) \
	for ((var) = 0; (var) < BNX2X_NUM_ETH_QUEUES(bp); (var)++)

#define for_each_nondefault_eth_queue(bp, var) \
	for ((var) = 1; (var) < BNX2X_NUM_ETH_QUEUES(bp); (var)++)

#define for_each_queue(bp, var) \
	for ((var) = 0; (var) < BNX2X_NUM_QUEUES(bp); (var)++) \
		if (skip_queue(bp, var))	\
			continue;		\
		else

/* Skip forwarding FP */
#define for_each_rx_queue(bp, var) \
	for ((var) = 0; (var) < BNX2X_NUM_QUEUES(bp); (var)++) \
		if (skip_rx_queue(bp, var))	\
			continue;		\
		else

/* Skip OOO FP */
#define for_each_tx_queue(bp, var) \
	for ((var) = 0; (var) < BNX2X_NUM_QUEUES(bp); (var)++) \
		if (skip_tx_queue(bp, var))	\
			continue;		\
		else

#define for_each_nondefault_queue(bp, var) \
	for ((var) = 1; (var) < BNX2X_NUM_QUEUES(bp); (var)++) \
		if (skip_queue(bp, var))	\
			continue;		\
		else

#define for_each_cos_in_tx_queue(fp, var) \
	for ((var) = 0; (var) < (fp)->max_cos; (var)++)

/* skip rx queue
 * if FCOE l2 support is disabled and this is the fcoe L2 queue
 */
#define skip_rx_queue(bp, idx)	(NO_FCOE(bp) && IS_FCOE_IDX(idx))

/* skip tx queue
 * if FCOE l2 support is disabled and this is the fcoe L2 queue
 */
#define skip_tx_queue(bp, idx)	(NO_FCOE(bp) && IS_FCOE_IDX(idx))

#define skip_queue(bp, idx)	(NO_FCOE(bp) && IS_FCOE_IDX(idx))




/**
 * bnx2x_set_mac_one - configure a single MAC address
 *
 * @bp:			driver handle
 * @mac:		MAC to configure
 * @obj:		MAC object handle
 * @set:		if 'true' add a new MAC, otherwise - delete
 * @mac_type:		the type of the MAC to configure (e.g. ETH, UC list)
 * @ramrod_flags:	RAMROD_XXX flags (e.g. RAMROD_CONT, RAMROD_COMP_WAIT)
 *
 * Configures one MAC according to provided parameters or continues the
 * execution of previously scheduled commands if RAMROD_CONT is set in
 * ramrod_flags.
 *
 * Returns zero if operation has successfully completed, a positive value if the
 * operation has been successfully scheduled and a negative - if a requested
 * operations has failed.
 */
int bnx2x_set_mac_one(struct bnx2x *bp, u8 *mac,
		      struct bnx2x_vlan_mac_obj *obj, bool set,
		      int mac_type, unsigned long *ramrod_flags);
/**
 * Deletes all MACs configured for the specific MAC object.
 *
 * @param bp Function driver instance
 * @param mac_obj MAC object to cleanup
 *
 * @return zero if all MACs were cleaned
 */

/**
 * bnx2x_del_all_macs - delete all MACs configured for the specific MAC object
 *
 * @bp:			driver handle
 * @mac_obj:		MAC object handle
 * @mac_type:		type of the MACs to clear (BNX2X_XXX_MAC)
 * @wait_for_comp:	if 'true' block until completion
 *
 * Deletes all MACs of the specific type (e.g. ETH, UC list).
 *
 * Returns zero if operation has successfully completed, a positive value if the
 * operation has been successfully scheduled and a negative - if a requested
 * operations has failed.
 */
int bnx2x_del_all_macs(struct bnx2x *bp,
		       struct bnx2x_vlan_mac_obj *mac_obj,
		       int mac_type, bool wait_for_comp);

/* Init Function API  */
void bnx2x_func_init(struct bnx2x *bp, struct bnx2x_func_init_params *p);
int bnx2x_get_gpio(struct bnx2x *bp, int gpio_num, u8 port);
int bnx2x_set_gpio(struct bnx2x *bp, int gpio_num, u32 mode, u8 port);
int bnx2x_set_mult_gpio(struct bnx2x *bp, u8 pins, u32 mode);
int bnx2x_set_gpio_int(struct bnx2x *bp, int gpio_num, u32 mode, u8 port);
void bnx2x_read_mf_cfg(struct bnx2x *bp);


/* dmae */
void bnx2x_read_dmae(struct bnx2x *bp, u32 src_addr, u32 len32);
void bnx2x_write_dmae(struct bnx2x *bp, dma_addr_t dma_addr, u32 dst_addr,
		      u32 len32);
void bnx2x_post_dmae(struct bnx2x *bp, struct dmae_command *dmae, int idx);
u32 bnx2x_dmae_opcode_add_comp(u32 opcode, u8 comp_type);
u32 bnx2x_dmae_opcode_clr_src_reset(u32 opcode);
u32 bnx2x_dmae_opcode(struct bnx2x *bp, u8 src_type, u8 dst_type,
		      bool with_comp, u8 comp_type);


void bnx2x_calc_fc_adv(struct bnx2x *bp);
int bnx2x_sp_post(struct bnx2x *bp, int command, int cid,
		  u32 data_hi, u32 data_lo, int cmd_type);
void bnx2x_update_coalesce(struct bnx2x *bp);
int bnx2x_get_cur_phy_idx(struct bnx2x *bp);

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

#define BNX2X_ILT_ZALLOC(x, y, size) \
	do { \
		x = dma_alloc_coherent(&bp->pdev->dev, size, y, GFP_KERNEL); \
		if (x) \
			memset(x, 0, size); \
	} while (0)

#define BNX2X_ILT_FREE(x, y, size) \
	do { \
		if (x) { \
			dma_free_coherent(&bp->pdev->dev, size, x, y); \
			x = NULL; \
			y = 0; \
		} \
	} while (0)

#define ILOG2(x)	(ilog2((x)))

#define ILT_NUM_PAGE_ENTRIES	(3072)
/* In 57710/11 we use whole table since we have 8 func
 * In 57712 we have only 4 func, but use same size per func, then only half of
 * the table in use
 */
#define ILT_PER_FUNC		(ILT_NUM_PAGE_ENTRIES/8)

#define FUNC_ILT_BASE(func)	(func * ILT_PER_FUNC)
/*
 * the phys address is shifted right 12 bits and has an added
 * 1=valid bit added to the 53rd bit
 * then since this is a wide register(TM)
 * we split it into two 32 bit writes
 */
#define ONCHIP_ADDR1(x)		((u32)(((u64)x >> 12) & 0xFFFFFFFF))
#define ONCHIP_ADDR2(x)		((u32)((1 << 20) | ((u64)x >> 44)))

/* load/unload mode */
#define LOAD_NORMAL			0
#define LOAD_OPEN			1
#define LOAD_DIAG			2
#define UNLOAD_NORMAL			0
#define UNLOAD_CLOSE			1
#define UNLOAD_RECOVERY			2


/* DMAE command defines */
#define DMAE_TIMEOUT			-1
#define DMAE_PCI_ERROR			-2	/* E2 and onward */
#define DMAE_NOT_RDY			-3
#define DMAE_PCI_ERR_FLAG		0x80000000

#define DMAE_SRC_PCI			0
#define DMAE_SRC_GRC			1

#define DMAE_DST_NONE			0
#define DMAE_DST_PCI			1
#define DMAE_DST_GRC			2

#define DMAE_COMP_PCI			0
#define DMAE_COMP_GRC			1

/* E2 and onward - PCI error handling in the completion */

#define DMAE_COMP_REGULAR		0
#define DMAE_COM_SET_ERR		1

#define DMAE_CMD_SRC_PCI		(DMAE_SRC_PCI << \
						DMAE_COMMAND_SRC_SHIFT)
#define DMAE_CMD_SRC_GRC		(DMAE_SRC_GRC << \
						DMAE_COMMAND_SRC_SHIFT)

#define DMAE_CMD_DST_PCI		(DMAE_DST_PCI << \
						DMAE_COMMAND_DST_SHIFT)
#define DMAE_CMD_DST_GRC		(DMAE_DST_GRC << \
						DMAE_COMMAND_DST_SHIFT)

#define DMAE_CMD_C_DST_PCI		(DMAE_COMP_PCI << \
						DMAE_COMMAND_C_DST_SHIFT)
#define DMAE_CMD_C_DST_GRC		(DMAE_COMP_GRC << \
						DMAE_COMMAND_C_DST_SHIFT)

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

#define DMAE_SRC_PF			0
#define DMAE_SRC_VF			1

#define DMAE_DST_PF			0
#define DMAE_DST_VF			1

#define DMAE_C_SRC			0
#define DMAE_C_DST			1

#define DMAE_LEN32_RD_MAX		0x80
#define DMAE_LEN32_WR_MAX(bp)		(CHIP_IS_E1(bp) ? 0x400 : 0x2000)

#define DMAE_COMP_VAL			0x60d0d0ae /* E2 and on - upper bit
							indicates eror */

#define MAX_DMAE_C_PER_PORT		8
#define INIT_DMAE_C(bp)			(BP_PORT(bp) * MAX_DMAE_C_PER_PORT + \
					 BP_VN(bp))
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
					 (BP_VN(bp) << BNX2X_SWCID_SHIFT) | \
					 (x))

#define SP_DESC_CNT		(BCM_PAGE_SIZE / sizeof(struct eth_spe))
#define MAX_SP_DESC_CNT			(SP_DESC_CNT - 1)


#define BNX2X_BTR			4
#define MAX_SPQ_PENDING			8

/* CMNG constants, as derived from system spec calculations */
/* default MIN rate in case VNIC min rate is configured to zero - 100Mbps */
#define DEF_MIN_RATE					100
/* resolution of the rate shaping timer - 400 usec */
#define RS_PERIODIC_TIMEOUT_USEC			400
/* number of bytes in single QM arbitration cycle -
 * coefficient for calculating the fairness timer */
#define QM_ARB_BYTES					160000
/* resolution of Min algorithm 1:100 */
#define MIN_RES						100
/* how many bytes above threshold for the minimal credit of Min algorithm*/
#define MIN_ABOVE_THRESH				32768
/* Fairness algorithm integration time coefficient -
 * for calculating the actual Tfair */
#define T_FAIR_COEF	((MIN_ABOVE_THRESH +  QM_ARB_BYTES) * 8 * MIN_RES)
/* Memory of fairness algorithm . 2 cycles */
#define FAIR_MEM					2


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
				 AEU_INPUTS_ATTN_BITS_PBCLIENT_HW_INTERRUPT)
#define HW_PRTY_ASSERT_SET_0	(AEU_INPUTS_ATTN_BITS_BRB_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_PARSER_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_TSDM_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_SEARCHER_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_TSEMI_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_TCM_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_PBCLIENT_PARITY_ERROR)
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
#define HW_PRTY_ASSERT_SET_1	(AEU_INPUTS_ATTN_BITS_PBF_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_QM_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_TIMERS_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_XSDM_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_XCM_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_XSEMI_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_DOORBELLQ_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_NIG_PARITY_ERROR |\
			     AEU_INPUTS_ATTN_BITS_VAUX_PCI_CORE_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_DEBUG_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_USDM_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_UCM_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_USEMI_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_UPB_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_CSDM_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_CCM_PARITY_ERROR)
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
				 AEU_INPUTS_ATTN_BITS_DMAE_PARITY_ERROR |\
				 AEU_INPUTS_ATTN_BITS_IGU_PARITY_ERROR | \
				 AEU_INPUTS_ATTN_BITS_MISC_PARITY_ERROR)

#define HW_PRTY_ASSERT_SET_3 (AEU_INPUTS_ATTN_BITS_MCP_LATCHED_ROM_PARITY | \
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_RX_PARITY | \
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_TX_PARITY | \
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY)

#define HW_PRTY_ASSERT_SET_4 (AEU_INPUTS_ATTN_BITS_PGLUE_PARITY_ERROR | \
			      AEU_INPUTS_ATTN_BITS_ATC_PARITY_ERROR)

#define MULTI_MASK			0x7f


#define DEF_USB_FUNC_OFF	offsetof(struct cstorm_def_status_block_u, func)
#define DEF_CSB_FUNC_OFF	offsetof(struct cstorm_def_status_block_c, func)
#define DEF_XSB_FUNC_OFF	offsetof(struct xstorm_def_status_block, func)
#define DEF_TSB_FUNC_OFF	offsetof(struct tstorm_def_status_block, func)

#define DEF_USB_IGU_INDEX_OFF \
			offsetof(struct cstorm_def_status_block_u, igu_index)
#define DEF_CSB_IGU_INDEX_OFF \
			offsetof(struct cstorm_def_status_block_c, igu_index)
#define DEF_XSB_IGU_INDEX_OFF \
			offsetof(struct xstorm_def_status_block, igu_index)
#define DEF_TSB_IGU_INDEX_OFF \
			offsetof(struct tstorm_def_status_block, igu_index)

#define DEF_USB_SEGMENT_OFF \
			offsetof(struct cstorm_def_status_block_u, segment)
#define DEF_CSB_SEGMENT_OFF \
			offsetof(struct cstorm_def_status_block_c, segment)
#define DEF_XSB_SEGMENT_OFF \
			offsetof(struct xstorm_def_status_block, segment)
#define DEF_TSB_SEGMENT_OFF \
			offsetof(struct tstorm_def_status_block, segment)

#define BNX2X_SP_DSB_INDEX \
		(&bp->def_status_blk->sp_sb.\
					index_values[HC_SP_INDEX_ETH_DEF_CONS])

#define SET_FLAG(value, mask, flag) \
	do {\
		(value) &= ~(mask);\
		(value) |= ((flag) << (mask##_SHIFT));\
	} while (0)

#define GET_FLAG(value, mask) \
	(((value) & (mask)) >> (mask##_SHIFT))

#define GET_FIELD(value, fname) \
	(((value) & (fname##_MASK)) >> (fname##_SHIFT))

#define CAM_IS_INVALID(x) \
	(GET_FLAG(x.flags, \
	MAC_CONFIGURATION_ENTRY_ACTION_TYPE) == \
	(T_ETH_MAC_COMMAND_INVALIDATE))

/* Number of u32 elements in MC hash array */
#define MC_HASH_SIZE			8
#define MC_HASH_OFFSET(bp, i)		(BAR_TSTRORM_INTMEM + \
	TSTORM_APPROXIMATE_MATCH_MULTICAST_FILTERING_OFFSET(BP_FUNC(bp)) + i*4)


#ifndef PXP2_REG_PXP2_INT_STS
#define PXP2_REG_PXP2_INT_STS		PXP2_REG_PXP2_INT_STS_0
#endif

#ifndef ETH_MAX_RX_CLIENTS_E2
#define ETH_MAX_RX_CLIENTS_E2		ETH_MAX_RX_CLIENTS_E1H
#endif

#define BNX2X_VPD_LEN			128
#define VENDOR_ID_LEN			4

/* Congestion management fairness mode */
#define CMNG_FNS_NONE		0
#define CMNG_FNS_MINMAX		1

#define HC_SEG_ACCESS_DEF		0   /*Driver decision 0-3*/
#define HC_SEG_ACCESS_ATTN		4
#define HC_SEG_ACCESS_NORM		0   /*Driver decision 0-1*/

static const u32 dmae_reg_go_c[] = {
	DMAE_REG_GO_C0, DMAE_REG_GO_C1, DMAE_REG_GO_C2, DMAE_REG_GO_C3,
	DMAE_REG_GO_C4, DMAE_REG_GO_C5, DMAE_REG_GO_C6, DMAE_REG_GO_C7,
	DMAE_REG_GO_C8, DMAE_REG_GO_C9, DMAE_REG_GO_C10, DMAE_REG_GO_C11,
	DMAE_REG_GO_C12, DMAE_REG_GO_C13, DMAE_REG_GO_C14, DMAE_REG_GO_C15
};

void bnx2x_set_ethtool_ops(struct net_device *netdev);
void bnx2x_notify_link_changed(struct bnx2x *bp);


#define BNX2X_MF_SD_PROTOCOL(bp) \
	((bp)->mf_config[BP_VN(bp)] & FUNC_MF_CFG_PROTOCOL_MASK)

#ifdef BCM_CNIC
#define BNX2X_IS_MF_SD_PROTOCOL_ISCSI(bp) \
	(BNX2X_MF_SD_PROTOCOL(bp) == FUNC_MF_CFG_PROTOCOL_ISCSI)

#define BNX2X_IS_MF_SD_PROTOCOL_FCOE(bp) \
	(BNX2X_MF_SD_PROTOCOL(bp) == FUNC_MF_CFG_PROTOCOL_FCOE)

#define IS_MF_ISCSI_SD(bp) (IS_MF_SD(bp) && BNX2X_IS_MF_SD_PROTOCOL_ISCSI(bp))
#define IS_MF_FCOE_SD(bp) (IS_MF_SD(bp) && BNX2X_IS_MF_SD_PROTOCOL_FCOE(bp))

#define BNX2X_MF_EXT_PROTOCOL_FCOE(bp)  ((bp)->mf_ext_config & \
					 MACP_FUNC_CFG_FLAGS_FCOE_OFFLOAD)

#define IS_MF_FCOE_AFEX(bp) (IS_MF_AFEX(bp) && BNX2X_MF_EXT_PROTOCOL_FCOE(bp))
#define IS_MF_STORAGE_SD(bp) (IS_MF_SD(bp) && \
				(BNX2X_IS_MF_SD_PROTOCOL_ISCSI(bp) || \
				 BNX2X_IS_MF_SD_PROTOCOL_FCOE(bp)))
#else
#define IS_MF_FCOE_AFEX(bp)	false
#endif


#endif /* bnx2x.h */
