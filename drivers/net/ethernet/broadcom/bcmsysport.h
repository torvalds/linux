/*
 * Broadcom BCM7xxx System Port Ethernet MAC driver
 *
 * Copyright (C) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BCM_SYSPORT_H
#define __BCM_SYSPORT_H

#include <linux/if_vlan.h>

/* Receive/transmit descriptor format */
#define DESC_ADDR_HI_STATUS_LEN	0x00
#define  DESC_ADDR_HI_SHIFT	0
#define  DESC_ADDR_HI_MASK	0xff
#define  DESC_STATUS_SHIFT	8
#define  DESC_STATUS_MASK	0x3ff
#define  DESC_LEN_SHIFT		18
#define  DESC_LEN_MASK		0x7fff
#define DESC_ADDR_LO		0x04

/* HW supports 40-bit addressing hence the */
#define DESC_SIZE		(WORDS_PER_DESC * sizeof(u32))

/* Default RX buffer allocation size */
#define RX_BUF_LENGTH		2048

/* Body(1500) + EH_SIZE(14) + VLANTAG(4) + BRCMTAG(4) + FCS(4) = 1526.
 * 1536 is multiple of 256 bytes
 */
#define ENET_BRCM_TAG_LEN	4
#define ENET_PAD		10
#define UMAC_MAX_MTU_SIZE	(ETH_DATA_LEN + ETH_HLEN + VLAN_HLEN + \
				 ENET_BRCM_TAG_LEN + ETH_FCS_LEN + ENET_PAD)

/* Transmit status block */
struct bcm_tsb {
	u32 pcp_dei_vid;
#define PCP_DEI_MASK		0xf
#define VID_SHIFT		4
#define VID_MASK		0xfff
	u32 l4_ptr_dest_map;
#define L4_CSUM_PTR_MASK	0x1ff
#define L4_PTR_SHIFT		9
#define L4_PTR_MASK		0x1ff
#define L4_UDP			(1 << 18)
#define L4_LENGTH_VALID		(1 << 19)
#define DEST_MAP_SHIFT		20
#define DEST_MAP_MASK		0x1ff
};

/* Receive status block uses the same
 * definitions as the DMA descriptor
 */
struct bcm_rsb {
	u32 rx_status_len;
	u32 brcm_egress_tag;
};

/* Common Receive/Transmit status bits */
#define DESC_L4_CSUM		(1 << 7)
#define DESC_SOP		(1 << 8)
#define DESC_EOP		(1 << 9)

/* Receive Status bits */
#define RX_STATUS_UCAST			0
#define RX_STATUS_BCAST			0x04
#define RX_STATUS_MCAST			0x08
#define RX_STATUS_L2_MCAST		0x0c
#define RX_STATUS_ERR			(1 << 4)
#define RX_STATUS_OVFLOW		(1 << 5)
#define RX_STATUS_PARSE_FAIL		(1 << 6)

/* Transmit Status bits */
#define TX_STATUS_VLAN_NO_ACT		0x00
#define TX_STATUS_VLAN_PCP_TSB		0x01
#define TX_STATUS_VLAN_QUEUE		0x02
#define TX_STATUS_VLAN_VID_TSB		0x03
#define TX_STATUS_OWR_CRC		(1 << 2)
#define TX_STATUS_APP_CRC		(1 << 3)
#define TX_STATUS_BRCM_TAG_NO_ACT	0
#define TX_STATUS_BRCM_TAG_ZERO		0x10
#define TX_STATUS_BRCM_TAG_ONE_QUEUE	0x20
#define TX_STATUS_BRCM_TAG_ONE_TSB	0x30
#define TX_STATUS_SKIP_BYTES		(1 << 6)

/* Specific register definitions */
#define SYS_PORT_TOPCTRL_OFFSET		0
#define REV_CNTL			0x00
#define  REV_MASK			0xffff

#define RX_FLUSH_CNTL			0x04
#define  RX_FLUSH			(1 << 0)

#define TX_FLUSH_CNTL			0x08
#define  TX_FLUSH			(1 << 0)

#define MISC_CNTL			0x0c
#define  SYS_CLK_SEL			(1 << 0)
#define  TDMA_EOP_SEL			(1 << 1)

/* Level-2 Interrupt controller offsets and defines */
#define SYS_PORT_INTRL2_0_OFFSET	0x200
#define SYS_PORT_INTRL2_1_OFFSET	0x240
#define INTRL2_CPU_STATUS		0x00
#define INTRL2_CPU_SET			0x04
#define INTRL2_CPU_CLEAR		0x08
#define INTRL2_CPU_MASK_STATUS		0x0c
#define INTRL2_CPU_MASK_SET		0x10
#define INTRL2_CPU_MASK_CLEAR		0x14

/* Level-2 instance 0 interrupt bits */
#define INTRL2_0_GISB_ERR		(1 << 0)
#define INTRL2_0_RBUF_OVFLOW		(1 << 1)
#define INTRL2_0_TBUF_UNDFLOW		(1 << 2)
#define INTRL2_0_MPD			(1 << 3)
#define INTRL2_0_BRCM_MATCH_TAG		(1 << 4)
#define INTRL2_0_RDMA_MBDONE		(1 << 5)
#define INTRL2_0_OVER_MAX_THRESH	(1 << 6)
#define INTRL2_0_BELOW_HYST_THRESH	(1 << 7)
#define INTRL2_0_FREE_LIST_EMPTY	(1 << 8)
#define INTRL2_0_TX_RING_FULL		(1 << 9)
#define INTRL2_0_DESC_ALLOC_ERR		(1 << 10)
#define INTRL2_0_UNEXP_PKTSIZE_ACK	(1 << 11)

/* SYSTEMPORT Lite groups the TX queues interrupts on instance 0 */
#define INTRL2_0_TDMA_MBDONE_SHIFT	12
#define INTRL2_0_TDMA_MBDONE_MASK	(0xffff << INTRL2_0_TDMA_MBDONE_SHIFT)

/* RXCHK offset and defines */
#define SYS_PORT_RXCHK_OFFSET		0x300

#define RXCHK_CONTROL			0x00
#define  RXCHK_EN			(1 << 0)
#define  RXCHK_SKIP_FCS			(1 << 1)
#define  RXCHK_BAD_CSUM_DIS		(1 << 2)
#define  RXCHK_BRCM_TAG_EN		(1 << 3)
#define  RXCHK_BRCM_TAG_MATCH_SHIFT	4
#define  RXCHK_BRCM_TAG_MATCH_MASK	0xff
#define  RXCHK_PARSE_TNL		(1 << 12)
#define  RXCHK_VIOL_EN			(1 << 13)
#define  RXCHK_VIOL_DIS			(1 << 14)
#define  RXCHK_INCOM_PKT		(1 << 15)
#define  RXCHK_V6_DUPEXT_EN		(1 << 16)
#define  RXCHK_V6_DUPEXT_DIS		(1 << 17)
#define  RXCHK_ETHERTYPE_DIS		(1 << 18)
#define  RXCHK_L2_HDR_DIS		(1 << 19)
#define  RXCHK_L3_HDR_DIS		(1 << 20)
#define  RXCHK_MAC_RX_ERR_DIS		(1 << 21)
#define  RXCHK_PARSE_AUTH		(1 << 22)

#define RXCHK_BRCM_TAG0			0x04
#define RXCHK_BRCM_TAG(i)		((i) * RXCHK_BRCM_TAG0)
#define RXCHK_BRCM_TAG0_MASK		0x24
#define RXCHK_BRCM_TAG_MASK(i)		((i) * RXCHK_BRCM_TAG0_MASK)
#define RXCHK_BRCM_TAG_MATCH_STATUS	0x44
#define RXCHK_ETHERTYPE			0x48
#define RXCHK_BAD_CSUM_CNTR		0x4C
#define RXCHK_OTHER_DISC_CNTR		0x50

/* TXCHCK offsets and defines */
#define SYS_PORT_TXCHK_OFFSET		0x380
#define TXCHK_PKT_RDY_THRESH		0x00

/* Receive buffer offset and defines */
#define SYS_PORT_RBUF_OFFSET		0x400

#define RBUF_CONTROL			0x00
#define  RBUF_RSB_EN			(1 << 0)
#define  RBUF_4B_ALGN			(1 << 1)
#define  RBUF_BRCM_TAG_STRIP		(1 << 2)
#define  RBUF_BAD_PKT_DISC		(1 << 3)
#define  RBUF_RESUME_THRESH_SHIFT	4
#define  RBUF_RESUME_THRESH_MASK	0xff
#define  RBUF_OK_TO_SEND_SHIFT		12
#define  RBUF_OK_TO_SEND_MASK		0xff
#define  RBUF_CRC_REPLACE		(1 << 20)
#define  RBUF_OK_TO_SEND_MODE		(1 << 21)
/* SYSTEMPORT Lite uses two bits here */
#define  RBUF_RSB_SWAP0			(1 << 22)
#define  RBUF_RSB_SWAP1			(1 << 23)
#define  RBUF_ACPI_EN			(1 << 23)

#define RBUF_PKT_RDY_THRESH		0x04

#define RBUF_STATUS			0x08
#define  RBUF_WOL_MODE			(1 << 0)
#define  RBUF_MPD			(1 << 1)
#define  RBUF_ACPI			(1 << 2)

#define RBUF_OVFL_DISC_CNTR		0x0c
#define RBUF_ERR_PKT_CNTR		0x10

/* Transmit buffer offset and defines */
#define SYS_PORT_TBUF_OFFSET		0x600

#define TBUF_CONTROL			0x00
#define  TBUF_BP_EN			(1 << 0)
#define  TBUF_MAX_PKT_THRESH_SHIFT	1
#define  TBUF_MAX_PKT_THRESH_MASK	0x1f
#define  TBUF_FULL_THRESH_SHIFT		8
#define  TBUF_FULL_THRESH_MASK		0x1f

/* UniMAC offset and defines */
#define SYS_PORT_UMAC_OFFSET		0x800

#define UMAC_CMD			0x008
#define  CMD_TX_EN			(1 << 0)
#define  CMD_RX_EN			(1 << 1)
#define  CMD_SPEED_SHIFT		2
#define  CMD_SPEED_10			0
#define  CMD_SPEED_100			1
#define  CMD_SPEED_1000			2
#define  CMD_SPEED_2500			3
#define  CMD_SPEED_MASK			3
#define  CMD_PROMISC			(1 << 4)
#define  CMD_PAD_EN			(1 << 5)
#define  CMD_CRC_FWD			(1 << 6)
#define  CMD_PAUSE_FWD			(1 << 7)
#define  CMD_RX_PAUSE_IGNORE		(1 << 8)
#define  CMD_TX_ADDR_INS		(1 << 9)
#define  CMD_HD_EN			(1 << 10)
#define  CMD_SW_RESET			(1 << 13)
#define  CMD_LCL_LOOP_EN		(1 << 15)
#define  CMD_AUTO_CONFIG		(1 << 22)
#define  CMD_CNTL_FRM_EN		(1 << 23)
#define  CMD_NO_LEN_CHK			(1 << 24)
#define  CMD_RMT_LOOP_EN		(1 << 25)
#define  CMD_PRBL_EN			(1 << 27)
#define  CMD_TX_PAUSE_IGNORE		(1 << 28)
#define  CMD_TX_RX_EN			(1 << 29)
#define  CMD_RUNT_FILTER_DIS		(1 << 30)

#define UMAC_MAC0			0x00c
#define UMAC_MAC1			0x010
#define UMAC_MAX_FRAME_LEN		0x014

#define UMAC_TX_FLUSH			0x334

#define UMAC_MIB_START			0x400

/* There is a 0xC gap between the end of RX and beginning of TX stats and then
 * between the end of TX stats and the beginning of the RX RUNT
 */
#define UMAC_MIB_STAT_OFFSET		0xc

#define UMAC_MIB_CTRL			0x580
#define  MIB_RX_CNT_RST			(1 << 0)
#define  MIB_RUNT_CNT_RST		(1 << 1)
#define  MIB_TX_CNT_RST			(1 << 2)

/* These offsets are valid for SYSTEMPORT and SYSTEMPORT Lite */
#define UMAC_MPD_CTRL			0x620
#define  MPD_EN				(1 << 0)
#define  MSEQ_LEN_SHIFT			16
#define  MSEQ_LEN_MASK			0xff
#define  PSW_EN				(1 << 27)

#define UMAC_PSW_MS			0x624
#define UMAC_PSW_LS			0x628
#define UMAC_MDF_CTRL			0x650
#define UMAC_MDF_ADDR			0x654

/* Only valid on SYSTEMPORT Lite */
#define SYS_PORT_GIB_OFFSET		0x1000

#define GIB_CONTROL			0x00
#define  GIB_TX_EN			(1 << 0)
#define  GIB_RX_EN			(1 << 1)
#define  GIB_TX_FLUSH			(1 << 2)
#define  GIB_RX_FLUSH			(1 << 3)
#define  GIB_GTX_CLK_SEL_SHIFT		4
#define  GIB_GTX_CLK_EXT_CLK		(0 << GIB_GTX_CLK_SEL_SHIFT)
#define  GIB_GTX_CLK_125MHZ		(1 << GIB_GTX_CLK_SEL_SHIFT)
#define  GIB_GTX_CLK_250MHZ		(2 << GIB_GTX_CLK_SEL_SHIFT)
#define  GIB_FCS_STRIP_SHIFT		6
#define  GIB_FCS_STRIP			(1 << GIB_FCS_STRIP_SHIFT)
#define  GIB_LCL_LOOP_EN		(1 << 7)
#define  GIB_LCL_LOOP_TXEN		(1 << 8)
#define  GIB_RMT_LOOP_EN		(1 << 9)
#define  GIB_RMT_LOOP_RXEN		(1 << 10)
#define  GIB_RX_PAUSE_EN		(1 << 11)
#define  GIB_PREAMBLE_LEN_SHIFT		12
#define  GIB_PREAMBLE_LEN_MASK		0xf
#define  GIB_IPG_LEN_SHIFT		16
#define  GIB_IPG_LEN_MASK		0x3f
#define  GIB_PAD_EXTENSION_SHIFT	22
#define  GIB_PAD_EXTENSION_MASK		0x3f

#define GIB_MAC1			0x08
#define GIB_MAC0			0x0c

/* Receive DMA offset and defines */
#define SYS_PORT_RDMA_OFFSET		0x2000

#define RDMA_CONTROL			0x1000
#define  RDMA_EN			(1 << 0)
#define  RDMA_RING_CFG			(1 << 1)
#define  RDMA_DISC_EN			(1 << 2)
#define  RDMA_BUF_DATA_OFFSET_SHIFT	4
#define  RDMA_BUF_DATA_OFFSET_MASK	0x3ff

#define RDMA_STATUS			0x1004
#define  RDMA_DISABLED			(1 << 0)
#define  RDMA_DESC_RAM_INIT_BUSY	(1 << 1)
#define  RDMA_BP_STATUS			(1 << 2)

#define RDMA_SCB_BURST_SIZE		0x1008

#define RDMA_RING_BUF_SIZE		0x100c
#define  RDMA_RING_SIZE_SHIFT		16

#define RDMA_WRITE_PTR_HI		0x1010
#define RDMA_WRITE_PTR_LO		0x1014
#define RDMA_PROD_INDEX			0x1018
#define  RDMA_PROD_INDEX_MASK		0xffff

#define RDMA_CONS_INDEX			0x101c
#define  RDMA_CONS_INDEX_MASK		0xffff

#define RDMA_START_ADDR_HI		0x1020
#define RDMA_START_ADDR_LO		0x1024
#define RDMA_END_ADDR_HI		0x1028
#define RDMA_END_ADDR_LO		0x102c

#define RDMA_MBDONE_INTR		0x1030
#define  RDMA_INTR_THRESH_MASK		0x1ff
#define  RDMA_TIMEOUT_SHIFT		16
#define  RDMA_TIMEOUT_MASK		0xffff

#define RDMA_XON_XOFF_THRESH		0x1034
#define  RDMA_XON_XOFF_THRESH_MASK	0xffff
#define  RDMA_XOFF_THRESH_SHIFT		16

#define RDMA_READ_PTR_HI		0x1038
#define RDMA_READ_PTR_LO		0x103c

#define RDMA_OVERRIDE			0x1040
#define  RDMA_LE_MODE			(1 << 0)
#define  RDMA_REG_MODE			(1 << 1)

#define RDMA_TEST			0x1044
#define  RDMA_TP_OUT_SEL		(1 << 0)
#define  RDMA_MEM_SEL			(1 << 1)

#define RDMA_DEBUG			0x1048

/* Transmit DMA offset and defines */
#define TDMA_NUM_RINGS			32	/* rings = queues */
#define TDMA_PORT_SIZE			DESC_SIZE /* two 32-bits words */

#define SYS_PORT_TDMA_OFFSET		0x4000
#define TDMA_WRITE_PORT_OFFSET		0x0000
#define TDMA_WRITE_PORT_HI(i)		(TDMA_WRITE_PORT_OFFSET + \
					(i) * TDMA_PORT_SIZE)
#define TDMA_WRITE_PORT_LO(i)		(TDMA_WRITE_PORT_OFFSET + \
					sizeof(u32) + (i) * TDMA_PORT_SIZE)

#define TDMA_READ_PORT_OFFSET		(TDMA_WRITE_PORT_OFFSET + \
					(TDMA_NUM_RINGS * TDMA_PORT_SIZE))
#define TDMA_READ_PORT_HI(i)		(TDMA_READ_PORT_OFFSET + \
					(i) * TDMA_PORT_SIZE)
#define TDMA_READ_PORT_LO(i)		(TDMA_READ_PORT_OFFSET + \
					sizeof(u32) + (i) * TDMA_PORT_SIZE)

#define TDMA_READ_PORT_CMD_OFFSET	(TDMA_READ_PORT_OFFSET + \
					(TDMA_NUM_RINGS * TDMA_PORT_SIZE))
#define TDMA_READ_PORT_CMD(i)		(TDMA_READ_PORT_CMD_OFFSET + \
					(i) * sizeof(u32))

#define TDMA_DESC_RING_00_BASE		(TDMA_READ_PORT_CMD_OFFSET + \
					(TDMA_NUM_RINGS * sizeof(u32)))

/* Register offsets and defines relatives to a specific ring number */
#define RING_HEAD_TAIL_PTR		0x00
#define  RING_HEAD_MASK			0x7ff
#define  RING_TAIL_SHIFT		11
#define  RING_TAIL_MASK			0x7ff
#define  RING_FLUSH			(1 << 24)
#define  RING_EN			(1 << 25)

#define RING_COUNT			0x04
#define  RING_COUNT_MASK		0x7ff
#define  RING_BUFF_DONE_SHIFT		11
#define  RING_BUFF_DONE_MASK		0x7ff

#define RING_MAX_HYST			0x08
#define  RING_MAX_THRESH_MASK		0x7ff
#define  RING_HYST_THRESH_SHIFT		11
#define  RING_HYST_THRESH_MASK		0x7ff

#define RING_INTR_CONTROL		0x0c
#define  RING_INTR_THRESH_MASK		0x7ff
#define  RING_EMPTY_INTR_EN		(1 << 15)
#define  RING_TIMEOUT_SHIFT		16
#define  RING_TIMEOUT_MASK		0xffff

#define RING_PROD_CONS_INDEX		0x10
#define  RING_PROD_INDEX_MASK		0xffff
#define  RING_CONS_INDEX_SHIFT		16
#define  RING_CONS_INDEX_MASK		0xffff

#define RING_MAPPING			0x14
#define  RING_QID_MASK			0x3
#define  RING_PORT_ID_SHIFT		3
#define  RING_PORT_ID_MASK		0x7
#define  RING_IGNORE_STATUS		(1 << 6)
#define  RING_FAILOVER_EN		(1 << 7)
#define  RING_CREDIT_SHIFT		8
#define  RING_CREDIT_MASK		0xffff

#define RING_PCP_DEI_VID		0x18
#define  RING_VID_MASK			0x7ff
#define  RING_DEI			(1 << 12)
#define  RING_PCP_SHIFT			13
#define  RING_PCP_MASK			0x7
#define  RING_PKT_SIZE_ADJ_SHIFT	16
#define  RING_PKT_SIZE_ADJ_MASK		0xf

#define TDMA_DESC_RING_SIZE		28

/* Defininition for a given TX ring base address */
#define TDMA_DESC_RING_BASE(i)		(TDMA_DESC_RING_00_BASE + \
					((i) * TDMA_DESC_RING_SIZE))

/* Ring indexed register addreses */
#define TDMA_DESC_RING_HEAD_TAIL_PTR(i)	(TDMA_DESC_RING_BASE(i) + \
					RING_HEAD_TAIL_PTR)
#define TDMA_DESC_RING_COUNT(i)		(TDMA_DESC_RING_BASE(i) + \
					RING_COUNT)
#define TDMA_DESC_RING_MAX_HYST(i)	(TDMA_DESC_RING_BASE(i) + \
					RING_MAX_HYST)
#define TDMA_DESC_RING_INTR_CONTROL(i)	(TDMA_DESC_RING_BASE(i) + \
					RING_INTR_CONTROL)
#define TDMA_DESC_RING_PROD_CONS_INDEX(i) \
					(TDMA_DESC_RING_BASE(i) + \
					RING_PROD_CONS_INDEX)
#define TDMA_DESC_RING_MAPPING(i)	(TDMA_DESC_RING_BASE(i) + \
					RING_MAPPING)
#define TDMA_DESC_RING_PCP_DEI_VID(i)	(TDMA_DESC_RING_BASE(i) + \
					RING_PCP_DEI_VID)

#define TDMA_CONTROL			0x600
#define  TDMA_EN			0
#define  TSB_EN				1
/* Uses 2 bits on SYSTEMPORT Lite and shifts everything by 1 bit, we
 * keep the SYSTEMPORT layout here and adjust with tdma_control_bit()
 */
#define  TSB_SWAP0			2
#define  TSB_SWAP1			3
#define  ACB_ALGO			3
#define  BUF_DATA_OFFSET_SHIFT		4
#define  BUF_DATA_OFFSET_MASK		0x3ff
#define  VLAN_EN			14
#define  SW_BRCM_TAG			15
#define  WNC_KPT_SIZE_UPDATE		16
#define  SYNC_PKT_SIZE			17
#define  ACH_TXDONE_DELAY_SHIFT		18
#define  ACH_TXDONE_DELAY_MASK		0xff

#define TDMA_STATUS			0x604
#define  TDMA_DISABLED			(1 << 0)
#define  TDMA_LL_RAM_INIT_BUSY		(1 << 1)

#define TDMA_SCB_BURST_SIZE		0x608
#define TDMA_OVER_MAX_THRESH_STATUS	0x60c
#define TDMA_OVER_HYST_THRESH_STATUS	0x610
#define TDMA_TPID			0x614

#define TDMA_FREE_LIST_HEAD_TAIL_PTR	0x618
#define  TDMA_FREE_HEAD_MASK		0x7ff
#define  TDMA_FREE_TAIL_SHIFT		11
#define  TDMA_FREE_TAIL_MASK		0x7ff

#define TDMA_FREE_LIST_COUNT		0x61c
#define  TDMA_FREE_LIST_COUNT_MASK	0x7ff

#define TDMA_TIER2_ARB_CTRL		0x620
#define  TDMA_ARB_MODE_RR		0
#define  TDMA_ARB_MODE_WEIGHT_RR	0x1
#define  TDMA_ARB_MODE_STRICT		0x2
#define  TDMA_ARB_MODE_DEFICIT_RR	0x3
#define  TDMA_CREDIT_SHIFT		4
#define  TDMA_CREDIT_MASK		0xffff

#define TDMA_TIER1_ARB_0_CTRL		0x624
#define  TDMA_ARB_EN			(1 << 0)

#define TDMA_TIER1_ARB_0_QUEUE_EN	0x628
#define TDMA_TIER1_ARB_1_CTRL		0x62c
#define TDMA_TIER1_ARB_1_QUEUE_EN	0x630
#define TDMA_TIER1_ARB_2_CTRL		0x634
#define TDMA_TIER1_ARB_2_QUEUE_EN	0x638
#define TDMA_TIER1_ARB_3_CTRL		0x63c
#define TDMA_TIER1_ARB_3_QUEUE_EN	0x640

#define TDMA_SCB_ENDIAN_OVERRIDE	0x644
#define  TDMA_LE_MODE			(1 << 0)
#define  TDMA_REG_MODE			(1 << 1)

#define TDMA_TEST			0x648
#define  TDMA_TP_OUT_SEL		(1 << 0)
#define  TDMA_MEM_TM			(1 << 1)

#define TDMA_DEBUG			0x64c

/* Transmit/Receive descriptor */
struct dma_desc {
	u32	addr_status_len;
	u32	addr_lo;
};

/* Number of Receive hardware descriptor words */
#define SP_NUM_HW_RX_DESC_WORDS		1024
#define SP_LT_NUM_HW_RX_DESC_WORDS	256

/* Internal linked-list RAM size */
#define SP_NUM_TX_DESC			1536
#define SP_LT_NUM_TX_DESC		256

#define WORDS_PER_DESC			(sizeof(struct dma_desc) / sizeof(u32))

/* Rx/Tx common counter group.*/
struct bcm_sysport_pkt_counters {
	u32	cnt_64;		/* RO Received/Transmited 64 bytes packet */
	u32	cnt_127;	/* RO Rx/Tx 127 bytes packet */
	u32	cnt_255;	/* RO Rx/Tx 65-255 bytes packet */
	u32	cnt_511;	/* RO Rx/Tx 256-511 bytes packet */
	u32	cnt_1023;	/* RO Rx/Tx 512-1023 bytes packet */
	u32	cnt_1518;	/* RO Rx/Tx 1024-1518 bytes packet */
	u32	cnt_mgv;	/* RO Rx/Tx 1519-1522 good VLAN packet */
	u32	cnt_2047;	/* RO Rx/Tx 1522-2047 bytes packet*/
	u32	cnt_4095;	/* RO Rx/Tx 2048-4095 bytes packet*/
	u32	cnt_9216;	/* RO Rx/Tx 4096-9216 bytes packet*/
};

/* RSV, Receive Status Vector */
struct bcm_sysport_rx_counters {
	struct  bcm_sysport_pkt_counters pkt_cnt;
	u32	pkt;		/* RO (0x428) Received pkt count*/
	u32	bytes;		/* RO Received byte count */
	u32	mca;		/* RO # of Received multicast pkt */
	u32	bca;		/* RO # of Receive broadcast pkt */
	u32	fcs;		/* RO # of Received FCS error  */
	u32	cf;		/* RO # of Received control frame pkt*/
	u32	pf;		/* RO # of Received pause frame pkt */
	u32	uo;		/* RO # of unknown op code pkt */
	u32	aln;		/* RO # of alignment error count */
	u32	flr;		/* RO # of frame length out of range count */
	u32	cde;		/* RO # of code error pkt */
	u32	fcr;		/* RO # of carrier sense error pkt */
	u32	ovr;		/* RO # of oversize pkt*/
	u32	jbr;		/* RO # of jabber count */
	u32	mtue;		/* RO # of MTU error pkt*/
	u32	pok;		/* RO # of Received good pkt */
	u32	uc;		/* RO # of unicast pkt */
	u32	ppp;		/* RO # of PPP pkt */
	u32	rcrc;		/* RO (0x470),# of CRC match pkt */
};

/* TSV, Transmit Status Vector */
struct bcm_sysport_tx_counters {
	struct bcm_sysport_pkt_counters pkt_cnt;
	u32	pkts;		/* RO (0x4a8) Transmited pkt */
	u32	mca;		/* RO # of xmited multicast pkt */
	u32	bca;		/* RO # of xmited broadcast pkt */
	u32	pf;		/* RO # of xmited pause frame count */
	u32	cf;		/* RO # of xmited control frame count */
	u32	fcs;		/* RO # of xmited FCS error count */
	u32	ovr;		/* RO # of xmited oversize pkt */
	u32	drf;		/* RO # of xmited deferral pkt */
	u32	edf;		/* RO # of xmited Excessive deferral pkt*/
	u32	scl;		/* RO # of xmited single collision pkt */
	u32	mcl;		/* RO # of xmited multiple collision pkt*/
	u32	lcl;		/* RO # of xmited late collision pkt */
	u32	ecl;		/* RO # of xmited excessive collision pkt*/
	u32	frg;		/* RO # of xmited fragments pkt*/
	u32	ncl;		/* RO # of xmited total collision count */
	u32	jbr;		/* RO # of xmited jabber count*/
	u32	bytes;		/* RO # of xmited byte count */
	u32	pok;		/* RO # of xmited good pkt */
	u32	uc;		/* RO (0x4f0) # of xmited unicast pkt */
};

struct bcm_sysport_mib {
	struct bcm_sysport_rx_counters rx;
	struct bcm_sysport_tx_counters tx;
	u32 rx_runt_cnt;
	u32 rx_runt_fcs;
	u32 rx_runt_fcs_align;
	u32 rx_runt_bytes;
	u32 rxchk_bad_csum;
	u32 rxchk_other_pkt_disc;
	u32 rbuf_ovflow_cnt;
	u32 rbuf_err_cnt;
	u32 alloc_rx_buff_failed;
	u32 rx_dma_failed;
	u32 tx_dma_failed;
};

/* HW maintains a large list of counters */
enum bcm_sysport_stat_type {
	BCM_SYSPORT_STAT_NETDEV = -1,
	BCM_SYSPORT_STAT_NETDEV64,
	BCM_SYSPORT_STAT_MIB_RX,
	BCM_SYSPORT_STAT_MIB_TX,
	BCM_SYSPORT_STAT_RUNT,
	BCM_SYSPORT_STAT_RXCHK,
	BCM_SYSPORT_STAT_RBUF,
	BCM_SYSPORT_STAT_SOFT,
};

/* Macros to help define ethtool statistics */
#define STAT_NETDEV(m) { \
	.stat_string = __stringify(m), \
	.stat_sizeof = sizeof(((struct net_device_stats *)0)->m), \
	.stat_offset = offsetof(struct net_device_stats, m), \
	.type = BCM_SYSPORT_STAT_NETDEV, \
}

#define STAT_NETDEV64(m) { \
	.stat_string = __stringify(m), \
	.stat_sizeof = sizeof(((struct bcm_sysport_stats64 *)0)->m), \
	.stat_offset = offsetof(struct bcm_sysport_stats64, m), \
	.type = BCM_SYSPORT_STAT_NETDEV64, \
}

#define STAT_MIB(str, m, _type) { \
	.stat_string = str, \
	.stat_sizeof = sizeof(((struct bcm_sysport_priv *)0)->m), \
	.stat_offset = offsetof(struct bcm_sysport_priv, m), \
	.type = _type, \
}

#define STAT_MIB_RX(str, m) STAT_MIB(str, m, BCM_SYSPORT_STAT_MIB_RX)
#define STAT_MIB_TX(str, m) STAT_MIB(str, m, BCM_SYSPORT_STAT_MIB_TX)
#define STAT_RUNT(str, m) STAT_MIB(str, m, BCM_SYSPORT_STAT_RUNT)
#define STAT_MIB_SOFT(str, m) STAT_MIB(str, m, BCM_SYSPORT_STAT_SOFT)

#define STAT_RXCHK(str, m, ofs) { \
	.stat_string = str, \
	.stat_sizeof = sizeof(((struct bcm_sysport_priv *)0)->m), \
	.stat_offset = offsetof(struct bcm_sysport_priv, m), \
	.type = BCM_SYSPORT_STAT_RXCHK, \
	.reg_offset = ofs, \
}

#define STAT_RBUF(str, m, ofs) { \
	.stat_string = str, \
	.stat_sizeof = sizeof(((struct bcm_sysport_priv *)0)->m), \
	.stat_offset = offsetof(struct bcm_sysport_priv, m), \
	.type = BCM_SYSPORT_STAT_RBUF, \
	.reg_offset = ofs, \
}

/* TX bytes and packets */
#define NUM_SYSPORT_TXQ_STAT	2

struct bcm_sysport_stats {
	char stat_string[ETH_GSTRING_LEN];
	int stat_sizeof;
	int stat_offset;
	enum bcm_sysport_stat_type type;
	/* reg offset from UMAC base for misc counters */
	u16 reg_offset;
};

struct bcm_sysport_stats64 {
	/* 64bit stats on 32bit/64bit Machine */
	u64	rx_packets;
	u64	rx_bytes;
	u64	tx_packets;
	u64	tx_bytes;
};

/* Software house keeping helper structure */
struct bcm_sysport_cb {
	struct sk_buff	*skb;		/* SKB for RX packets */
	void __iomem	*bd_addr;	/* Buffer descriptor PHYS addr */

	DEFINE_DMA_UNMAP_ADDR(dma_addr);
	DEFINE_DMA_UNMAP_LEN(dma_len);
};

enum bcm_sysport_type {
	SYSTEMPORT = 0,
	SYSTEMPORT_LITE,
};

struct bcm_sysport_hw_params {
	bool		is_lite;
	unsigned int	num_rx_desc_words;
};

/* Software view of the TX ring */
struct bcm_sysport_tx_ring {
	spinlock_t	lock;		/* Ring lock for tx reclaim/xmit */
	struct napi_struct napi;	/* NAPI per tx queue */
	dma_addr_t	desc_dma;	/* DMA cookie */
	unsigned int	index;		/* Ring index */
	unsigned int	size;		/* Ring current size */
	unsigned int	alloc_size;	/* Ring one-time allocated size */
	unsigned int	desc_count;	/* Number of descriptors */
	unsigned int	curr_desc;	/* Current descriptor */
	unsigned int	c_index;	/* Last consumer index */
	unsigned int	clean_index;	/* Current clean index */
	struct bcm_sysport_cb *cbs;	/* Transmit control blocks */
	struct dma_desc	*desc_cpu;	/* CPU view of the descriptor */
	struct bcm_sysport_priv *priv;	/* private context backpointer */
	unsigned long	packets;	/* packets statistics */
	unsigned long	bytes;		/* bytes statistics */
};

/* Driver private structure */
struct bcm_sysport_priv {
	void __iomem		*base;
	u32			irq0_stat;
	u32			irq0_mask;
	u32			irq1_stat;
	u32			irq1_mask;
	bool			is_lite;
	unsigned int		num_rx_desc_words;
	struct napi_struct	napi ____cacheline_aligned;
	struct net_device	*netdev;
	struct platform_device	*pdev;
	int			irq0;
	int			irq1;
	int			wol_irq;

	/* Transmit rings */
	struct bcm_sysport_tx_ring *tx_rings;

	/* Receive queue */
	void __iomem		*rx_bds;
	struct bcm_sysport_cb	*rx_cbs;
	unsigned int		num_rx_bds;
	unsigned int		rx_read_ptr;
	unsigned int		rx_c_index;

	/* PHY device */
	struct device_node	*phy_dn;
	phy_interface_t		phy_interface;
	int			old_pause;
	int			old_link;
	int			old_duplex;

	/* Misc fields */
	unsigned int		rx_chk_en:1;
	unsigned int		tsb_en:1;
	unsigned int		crc_fwd:1;
	u16			rev;
	u32			wolopts;
	unsigned int		wol_irq_disabled:1;

	/* MIB related fields */
	struct bcm_sysport_mib	mib;

	/* Ethtool */
	u32			msg_enable;

	struct bcm_sysport_stats64	stats64;

	/* For atomic update generic 64bit value on 32bit Machine */
	struct u64_stats_sync	syncp;
};
#endif /* __BCM_SYSPORT_H */
