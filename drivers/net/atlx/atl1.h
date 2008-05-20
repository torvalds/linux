/*
 * Copyright(c) 2005 - 2006 Attansic Corporation. All rights reserved.
 * Copyright(c) 2006 - 2007 Chris Snook <csnook@redhat.com>
 * Copyright(c) 2006 - 2008 Jay Cliburn <jcliburn@gmail.com>
 *
 * Derived from Intel e1000 driver
 * Copyright(c) 1999 - 2005 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef ATL1_H
#define ATL1_H

#include <linux/compiler.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "atlx.h"

#define ATLX_DRIVER_NAME "atl1"

MODULE_DESCRIPTION("Atheros L1 Gigabit Ethernet Driver");

#define atlx_adapter		atl1_adapter
#define atlx_check_for_link	atl1_check_for_link
#define atlx_check_link		atl1_check_link
#define atlx_hash_mc_addr	atl1_hash_mc_addr
#define atlx_hash_set		atl1_hash_set
#define atlx_hw			atl1_hw
#define atlx_mii_ioctl		atl1_mii_ioctl
#define atlx_read_phy_reg	atl1_read_phy_reg
#define atlx_set_mac		atl1_set_mac
#define atlx_set_mac_addr	atl1_set_mac_addr

struct atl1_adapter;
struct atl1_hw;

/* function prototypes needed by multiple files */
u32 atl1_hash_mc_addr(struct atl1_hw *hw, u8 *mc_addr);
void atl1_hash_set(struct atl1_hw *hw, u32 hash_value);
s32 atl1_read_phy_reg(struct atl1_hw *hw, u16 reg_addr, u16 *phy_data);
void atl1_set_mac_addr(struct atl1_hw *hw);
static int atl1_mii_ioctl(struct net_device *netdev, struct ifreq *ifr,
	int cmd);
static u32 atl1_check_link(struct atl1_adapter *adapter);

extern const struct ethtool_ops atl1_ethtool_ops;

/* hardware definitions specific to L1 */

/* Block IDLE Status Register */
#define IDLE_STATUS_RXMAC			0x1
#define IDLE_STATUS_TXMAC			0x2
#define IDLE_STATUS_RXQ				0x4
#define IDLE_STATUS_TXQ				0x8
#define IDLE_STATUS_DMAR			0x10
#define IDLE_STATUS_DMAW			0x20
#define IDLE_STATUS_SMB				0x40
#define IDLE_STATUS_CMB				0x80

/* MDIO Control Register */
#define MDIO_WAIT_TIMES				30

/* MAC Control Register */
#define MAC_CTRL_TX_PAUSE			0x10000
#define MAC_CTRL_SCNT				0x20000
#define MAC_CTRL_SRST_TX			0x40000
#define MAC_CTRL_TX_SIMURST			0x80000
#define MAC_CTRL_SPEED_SHIFT			20
#define MAC_CTRL_SPEED_MASK			0x300000
#define MAC_CTRL_SPEED_1000			0x2
#define MAC_CTRL_SPEED_10_100			0x1
#define MAC_CTRL_DBG_TX_BKPRESURE		0x400000
#define MAC_CTRL_TX_HUGE			0x800000
#define MAC_CTRL_RX_CHKSUM_EN			0x1000000
#define MAC_CTRL_DBG				0x8000000

/* Wake-On-Lan control register */
#define WOL_CLK_SWITCH_EN			0x8000
#define WOL_PT5_EN				0x200000
#define WOL_PT6_EN				0x400000
#define WOL_PT5_MATCH				0x8000000
#define WOL_PT6_MATCH				0x10000000

/* WOL Length ( 2 DWORD ) */
#define REG_WOL_PATTERN_LEN			0x14A4
#define WOL_PT_LEN_MASK				0x7F
#define WOL_PT0_LEN_SHIFT			0
#define WOL_PT1_LEN_SHIFT			8
#define WOL_PT2_LEN_SHIFT			16
#define WOL_PT3_LEN_SHIFT			24
#define WOL_PT4_LEN_SHIFT			0
#define WOL_PT5_LEN_SHIFT			8
#define WOL_PT6_LEN_SHIFT			16

/* Internal SRAM Partition Registers, low 32 bits */
#define REG_SRAM_RFD_LEN			0x1504
#define REG_SRAM_RRD_ADDR			0x1508
#define REG_SRAM_RRD_LEN			0x150C
#define REG_SRAM_TPD_ADDR			0x1510
#define REG_SRAM_TPD_LEN			0x1514
#define REG_SRAM_TRD_ADDR			0x1518
#define REG_SRAM_TRD_LEN			0x151C
#define REG_SRAM_RXF_ADDR			0x1520
#define REG_SRAM_RXF_LEN			0x1524
#define REG_SRAM_TXF_ADDR			0x1528
#define REG_SRAM_TXF_LEN			0x152C
#define REG_SRAM_TCPH_PATH_ADDR			0x1530
#define SRAM_TCPH_ADDR_MASK			0xFFF
#define SRAM_TCPH_ADDR_SHIFT			0
#define SRAM_PATH_ADDR_MASK			0xFFF
#define SRAM_PATH_ADDR_SHIFT			16

/* Load Ptr Register */
#define REG_LOAD_PTR				0x1534

/* Descriptor Control registers, low 32 bits */
#define REG_DESC_RFD_ADDR_LO			0x1544
#define REG_DESC_RRD_ADDR_LO			0x1548
#define REG_DESC_TPD_ADDR_LO			0x154C
#define REG_DESC_CMB_ADDR_LO			0x1550
#define REG_DESC_SMB_ADDR_LO			0x1554
#define REG_DESC_RFD_RRD_RING_SIZE		0x1558
#define DESC_RFD_RING_SIZE_MASK			0x7FF
#define DESC_RFD_RING_SIZE_SHIFT		0
#define DESC_RRD_RING_SIZE_MASK			0x7FF
#define DESC_RRD_RING_SIZE_SHIFT		16
#define REG_DESC_TPD_RING_SIZE			0x155C
#define DESC_TPD_RING_SIZE_MASK			0x3FF
#define DESC_TPD_RING_SIZE_SHIFT		0

/* TXQ Control Register */
#define REG_TXQ_CTRL				0x1580
#define TXQ_CTRL_TPD_BURST_NUM_SHIFT		0
#define TXQ_CTRL_TPD_BURST_NUM_MASK		0x1F
#define TXQ_CTRL_EN				0x20
#define TXQ_CTRL_ENH_MODE			0x40
#define TXQ_CTRL_TPD_FETCH_TH_SHIFT		8
#define TXQ_CTRL_TPD_FETCH_TH_MASK		0x3F
#define TXQ_CTRL_TXF_BURST_NUM_SHIFT		16
#define TXQ_CTRL_TXF_BURST_NUM_MASK		0xFFFF

/* Jumbo packet Threshold for task offload */
#define REG_TX_JUMBO_TASK_TH_TPD_IPG		0x1584
#define TX_JUMBO_TASK_TH_MASK			0x7FF
#define TX_JUMBO_TASK_TH_SHIFT			0
#define TX_TPD_MIN_IPG_MASK			0x1F
#define TX_TPD_MIN_IPG_SHIFT			16

/* RXQ Control Register */
#define REG_RXQ_CTRL				0x15A0
#define RXQ_CTRL_RFD_BURST_NUM_SHIFT		0
#define RXQ_CTRL_RFD_BURST_NUM_MASK		0xFF
#define RXQ_CTRL_RRD_BURST_THRESH_SHIFT		8
#define RXQ_CTRL_RRD_BURST_THRESH_MASK		0xFF
#define RXQ_CTRL_RFD_PREF_MIN_IPG_SHIFT		16
#define RXQ_CTRL_RFD_PREF_MIN_IPG_MASK		0x1F
#define RXQ_CTRL_CUT_THRU_EN			0x40000000
#define RXQ_CTRL_EN				0x80000000

/* Rx jumbo packet threshold and rrd  retirement timer */
#define REG_RXQ_JMBOSZ_RRDTIM			0x15A4
#define RXQ_JMBOSZ_TH_MASK			0x7FF
#define RXQ_JMBOSZ_TH_SHIFT			0
#define RXQ_JMBO_LKAH_MASK			0xF
#define RXQ_JMBO_LKAH_SHIFT			11
#define RXQ_RRD_TIMER_MASK			0xFFFF
#define RXQ_RRD_TIMER_SHIFT			16

/* RFD flow control register */
#define REG_RXQ_RXF_PAUSE_THRESH		0x15A8
#define RXQ_RXF_PAUSE_TH_HI_SHIFT		16
#define RXQ_RXF_PAUSE_TH_HI_MASK		0xFFF
#define RXQ_RXF_PAUSE_TH_LO_SHIFT		0
#define RXQ_RXF_PAUSE_TH_LO_MASK		0xFFF

/* RRD flow control register */
#define REG_RXQ_RRD_PAUSE_THRESH		0x15AC
#define RXQ_RRD_PAUSE_TH_HI_SHIFT		0
#define RXQ_RRD_PAUSE_TH_HI_MASK		0xFFF
#define RXQ_RRD_PAUSE_TH_LO_SHIFT		16
#define RXQ_RRD_PAUSE_TH_LO_MASK		0xFFF

/* DMA Engine Control Register */
#define REG_DMA_CTRL				0x15C0
#define DMA_CTRL_DMAR_IN_ORDER			0x1
#define DMA_CTRL_DMAR_ENH_ORDER			0x2
#define DMA_CTRL_DMAR_OUT_ORDER			0x4
#define DMA_CTRL_RCB_VALUE			0x8
#define DMA_CTRL_DMAR_BURST_LEN_SHIFT		4
#define DMA_CTRL_DMAR_BURST_LEN_MASK		7
#define DMA_CTRL_DMAW_BURST_LEN_SHIFT		7
#define DMA_CTRL_DMAW_BURST_LEN_MASK		7
#define DMA_CTRL_DMAR_EN			0x400
#define DMA_CTRL_DMAW_EN			0x800

/* CMB/SMB Control Register */
#define REG_CSMB_CTRL				0x15D0
#define CSMB_CTRL_CMB_NOW			1
#define CSMB_CTRL_SMB_NOW			2
#define CSMB_CTRL_CMB_EN			4
#define CSMB_CTRL_SMB_EN			8

/* CMB DMA Write Threshold Register */
#define REG_CMB_WRITE_TH			0x15D4
#define CMB_RRD_TH_SHIFT			0
#define CMB_RRD_TH_MASK				0x7FF
#define CMB_TPD_TH_SHIFT			16
#define CMB_TPD_TH_MASK				0x7FF

/* RX/TX count-down timer to trigger CMB-write. 2us resolution. */
#define REG_CMB_WRITE_TIMER			0x15D8
#define CMB_RX_TM_SHIFT				0
#define CMB_RX_TM_MASK				0xFFFF
#define CMB_TX_TM_SHIFT				16
#define CMB_TX_TM_MASK				0xFFFF

/* Number of packet received since last CMB write */
#define REG_CMB_RX_PKT_CNT			0x15DC

/* Number of packet transmitted since last CMB write */
#define REG_CMB_TX_PKT_CNT			0x15E0

/* SMB auto DMA timer register */
#define REG_SMB_TIMER				0x15E4

/* Mailbox Register */
#define REG_MAILBOX				0x15F0
#define MB_RFD_PROD_INDX_SHIFT			0
#define MB_RFD_PROD_INDX_MASK			0x7FF
#define MB_RRD_CONS_INDX_SHIFT			11
#define MB_RRD_CONS_INDX_MASK			0x7FF
#define MB_TPD_PROD_INDX_SHIFT			22
#define MB_TPD_PROD_INDX_MASK			0x3FF

/* Interrupt Status Register */
#define ISR_SMB					0x1
#define ISR_TIMER				0x2
#define ISR_MANUAL				0x4
#define ISR_RXF_OV				0x8
#define ISR_RFD_UNRUN				0x10
#define ISR_RRD_OV				0x20
#define ISR_TXF_UNRUN				0x40
#define ISR_LINK				0x80
#define ISR_HOST_RFD_UNRUN			0x100
#define ISR_HOST_RRD_OV				0x200
#define ISR_DMAR_TO_RST				0x400
#define ISR_DMAW_TO_RST				0x800
#define ISR_GPHY				0x1000
#define ISR_RX_PKT				0x10000
#define ISR_TX_PKT				0x20000
#define ISR_TX_DMA				0x40000
#define ISR_RX_DMA				0x80000
#define ISR_CMB_RX				0x100000
#define ISR_CMB_TX				0x200000
#define ISR_MAC_RX				0x400000
#define ISR_MAC_TX				0x800000
#define ISR_DIS_SMB				0x20000000
#define ISR_DIS_DMA				0x40000000

/* Normal Interrupt mask  */
#define IMR_NORMAL_MASK	(\
	ISR_SMB		|\
	ISR_GPHY	|\
	ISR_PHY_LINKDOWN|\
	ISR_DMAR_TO_RST	|\
	ISR_DMAW_TO_RST	|\
	ISR_CMB_TX	|\
	ISR_CMB_RX)

/* Debug Interrupt Mask  (enable all interrupt) */
#define IMR_DEBUG_MASK	(\
	ISR_SMB		|\
	ISR_TIMER	|\
	ISR_MANUAL	|\
	ISR_RXF_OV	|\
	ISR_RFD_UNRUN	|\
	ISR_RRD_OV	|\
	ISR_TXF_UNRUN	|\
	ISR_LINK	|\
	ISR_CMB_TX	|\
	ISR_CMB_RX	|\
	ISR_RX_PKT	|\
	ISR_TX_PKT	|\
	ISR_MAC_RX	|\
	ISR_MAC_TX)

#define MEDIA_TYPE_1000M_FULL			1
#define MEDIA_TYPE_100M_FULL			2
#define MEDIA_TYPE_100M_HALF			3
#define MEDIA_TYPE_10M_FULL			4
#define MEDIA_TYPE_10M_HALF			5

#define AUTONEG_ADVERTISE_SPEED_DEFAULT		0x002F	/* All but 1000-Half */

#define MAX_JUMBO_FRAME_SIZE			10240

#define ATL1_EEDUMP_LEN				48

/* Statistics counters collected by the MAC */
struct stats_msg_block {
	/* rx */
	u32 rx_ok;		/* good RX packets */
	u32 rx_bcast;		/* good RX broadcast packets */
	u32 rx_mcast;		/* good RX multicast packets */
	u32 rx_pause;		/* RX pause frames */
	u32 rx_ctrl;		/* RX control packets other than pause frames */
	u32 rx_fcs_err;		/* RX packets with bad FCS */
	u32 rx_len_err;		/* RX packets with length != actual size */
	u32 rx_byte_cnt;	/* good bytes received. FCS is NOT included */
	u32 rx_runt;		/* RX packets < 64 bytes with good FCS */
	u32 rx_frag;		/* RX packets < 64 bytes with bad FCS */
	u32 rx_sz_64;		/* 64 byte RX packets */
	u32 rx_sz_65_127;
	u32 rx_sz_128_255;
	u32 rx_sz_256_511;
	u32 rx_sz_512_1023;
	u32 rx_sz_1024_1518;
	u32 rx_sz_1519_max;	/* 1519 byte to MTU RX packets */
	u32 rx_sz_ov;		/* truncated RX packets > MTU */
	u32 rx_rxf_ov;		/* frames dropped due to RX FIFO overflow */
	u32 rx_rrd_ov;		/* frames dropped due to RRD overflow */
	u32 rx_align_err;	/* alignment errors */
	u32 rx_bcast_byte_cnt;	/* RX broadcast bytes, excluding FCS */
	u32 rx_mcast_byte_cnt;	/* RX multicast bytes, excluding FCS */
	u32 rx_err_addr;	/* packets dropped due to address filtering */

	/* tx */
	u32 tx_ok;		/* good TX packets */
	u32 tx_bcast;		/* good TX broadcast packets */
	u32 tx_mcast;		/* good TX multicast packets */
	u32 tx_pause;		/* TX pause frames */
	u32 tx_exc_defer;	/* TX packets deferred excessively */
	u32 tx_ctrl;		/* TX control frames, excluding pause frames */
	u32 tx_defer;		/* TX packets deferred */
	u32 tx_byte_cnt;	/* bytes transmitted, FCS is NOT included */
	u32 tx_sz_64;		/* 64 byte TX packets */
	u32 tx_sz_65_127;
	u32 tx_sz_128_255;
	u32 tx_sz_256_511;
	u32 tx_sz_512_1023;
	u32 tx_sz_1024_1518;
	u32 tx_sz_1519_max;	/* 1519 byte to MTU TX packets */
	u32 tx_1_col;		/* packets TX after a single collision */
	u32 tx_2_col;		/* packets TX after multiple collisions */
	u32 tx_late_col;	/* TX packets with late collisions */
	u32 tx_abort_col;	/* TX packets aborted w/excessive collisions */
	u32 tx_underrun;	/* TX packets aborted due to TX FIFO underrun
				 * or TRD FIFO underrun */
	u32 tx_rd_eop;		/* reads beyond the EOP into the next frame
				 * when TRD was not written timely */
	u32 tx_len_err;		/* TX packets where length != actual size */
	u32 tx_trunc;		/* TX packets truncated due to size > MTU */
	u32 tx_bcast_byte;	/* broadcast bytes transmitted, excluding FCS */
	u32 tx_mcast_byte;	/* multicast bytes transmitted, excluding FCS */
	u32 smb_updated;	/* 1: SMB Updated. This is used by software to
				 * indicate the statistics update. Software
				 * should clear this bit after retrieving the
				 * statistics information. */
};

/* Coalescing Message Block */
struct coals_msg_block {
	u32 int_stats;		/* interrupt status */
	u16 rrd_prod_idx;	/* TRD Producer Index. */
	u16 rfd_cons_idx;	/* RFD Consumer Index. */
	u16 update;		/* Selene sets this bit every time it DMAs the
				 * CMB to host memory. Software should clear
				 * this bit when CMB info is processed. */
	u16 tpd_cons_idx;	/* TPD Consumer Index. */
};

/* RRD descriptor */
struct rx_return_desc {
	u8 num_buf;	/* Number of RFD buffers used by the received packet */
	u8 resved;
	u16 buf_indx;	/* RFD Index of the first buffer */
	union {
		u32 valid;
		struct {
			u16 rx_chksum;
			u16 pkt_size;
		} xsum_sz;
	} xsz;

	u16 pkt_flg;	/* Packet flags */
	u16 err_flg;	/* Error flags */
	u16 resved2;
	u16 vlan_tag;	/* VLAN TAG */
};

#define PACKET_FLAG_ETH_TYPE	0x0080
#define PACKET_FLAG_VLAN_INS	0x0100
#define PACKET_FLAG_ERR		0x0200
#define PACKET_FLAG_IPV4	0x0400
#define PACKET_FLAG_UDP		0x0800
#define PACKET_FLAG_TCP		0x1000
#define PACKET_FLAG_BCAST	0x2000
#define PACKET_FLAG_MCAST	0x4000
#define PACKET_FLAG_PAUSE	0x8000

#define ERR_FLAG_CRC		0x0001
#define ERR_FLAG_CODE		0x0002
#define ERR_FLAG_DRIBBLE	0x0004
#define ERR_FLAG_RUNT		0x0008
#define ERR_FLAG_OV		0x0010
#define ERR_FLAG_TRUNC		0x0020
#define ERR_FLAG_IP_CHKSUM	0x0040
#define ERR_FLAG_L4_CHKSUM	0x0080
#define ERR_FLAG_LEN		0x0100
#define ERR_FLAG_DES_ADDR	0x0200

/* RFD descriptor */
struct rx_free_desc {
	__le64 buffer_addr;	/* Address of the descriptor's data buffer */
	__le16 buf_len;		/* Size of the receive buffer in host memory */
	u16 coalese;		/* Update consumer index to host after the
				 * reception of this frame */
	/* __attribute__ ((packed)) is required */
} __attribute__ ((packed));

/*
 * The L1 transmit packet descriptor is comprised of four 32-bit words.
 *
 *	31					0
 *	+---------------------------------------+
 *      |	Word 0: Buffer addr lo 		|
 *      +---------------------------------------+
 *      |	Word 1: Buffer addr hi		|
 *      +---------------------------------------+
 *      |		Word 2			|
 *      +---------------------------------------+
 *      |		Word 3			|
 *      +---------------------------------------+
 *
 * Words 0 and 1 combine to form a 64-bit buffer address.
 *
 * Word 2 is self explanatory in the #define block below.
 *
 * Word 3 has two forms, depending upon the state of bits 3 and 4.
 * If bits 3 and 4 are both zero, then bits 14:31 are unused by the
 * hardware.  Otherwise, if either bit 3 or 4 is set, the definition
 * of bits 14:31 vary according to the following depiction.
 *
 *	0	End of packet			0	End of packet
 *	1	Coalesce			1	Coalesce
 *	2	Insert VLAN tag			2	Insert VLAN tag
 *	3	Custom csum enable = 0		3	Custom csum enable = 1
 *	4	Segment enable = 1		4	Segment enable = 0
 *	5	Generate IP checksum		5	Generate IP checksum
 *	6	Generate TCP checksum		6	Generate TCP checksum
 *	7	Generate UDP checksum		7	Generate UDP checksum
 *	8	VLAN tagged			8	VLAN tagged
 *	9	Ethernet frame type		9	Ethernet frame type
 *	10-+ 					10-+
 *	11 |	IP hdr length (10:13)		11 |	IP hdr length (10:13)
 *	12 |	(num 32-bit words)		12 |	(num 32-bit words)
 *	13-+					13-+
 *	14-+					14	Unused
 *	15 |	TCP hdr length (14:17)		15	Unused
 *	16 |	(num 32-bit words)		16-+
 *	17-+					17 |
 *	18	Header TPD flag			18 |
 *	19-+					19 |	Payload offset
 *	20 |					20 |	    (16:23)
 *	21 |					21 |
 *	22 |					22 |
 *	23 |					23-+
 *	24 |					24-+
 *	25 |	MSS (19:31)			25 |
 *	26 |					26 |
 *	27 |					27 |	Custom csum offset
 *	28 |					28 |	     (24:31)
 *	29 |					29 |
 *	30 |					30 |
 *	31-+					31-+
 */

/* tpd word 2 */
#define TPD_BUFLEN_MASK		0x3FFF
#define TPD_BUFLEN_SHIFT	0
#define TPD_DMAINT_MASK		0x0001
#define TPD_DMAINT_SHIFT	14
#define TPD_PKTNT_MASK		0x0001
#define TPD_PKTINT_SHIFT	15
#define TPD_VLANTAG_MASK	0xFFFF
#define TPD_VLAN_SHIFT		16

/* tpd word 3 bits 0:13 */
#define TPD_EOP_MASK		0x0001
#define TPD_EOP_SHIFT		0
#define TPD_COALESCE_MASK	0x0001
#define TPD_COALESCE_SHIFT	1
#define TPD_INS_VL_TAG_MASK	0x0001
#define TPD_INS_VL_TAG_SHIFT	2
#define TPD_CUST_CSUM_EN_MASK	0x0001
#define TPD_CUST_CSUM_EN_SHIFT	3
#define TPD_SEGMENT_EN_MASK	0x0001
#define TPD_SEGMENT_EN_SHIFT	4
#define TPD_IP_CSUM_MASK	0x0001
#define TPD_IP_CSUM_SHIFT	5
#define TPD_TCP_CSUM_MASK	0x0001
#define TPD_TCP_CSUM_SHIFT	6
#define TPD_UDP_CSUM_MASK	0x0001
#define TPD_UDP_CSUM_SHIFT	7
#define TPD_VL_TAGGED_MASK	0x0001
#define TPD_VL_TAGGED_SHIFT	8
#define TPD_ETHTYPE_MASK	0x0001
#define TPD_ETHTYPE_SHIFT	9
#define TPD_IPHL_MASK		0x000F
#define TPD_IPHL_SHIFT		10

/* tpd word 3 bits 14:31 if segment enabled */
#define TPD_TCPHDRLEN_MASK	0x000F
#define TPD_TCPHDRLEN_SHIFT	14
#define TPD_HDRFLAG_MASK	0x0001
#define TPD_HDRFLAG_SHIFT	18
#define TPD_MSS_MASK		0x1FFF
#define TPD_MSS_SHIFT		19

/* tpd word 3 bits 16:31 if custom csum enabled */
#define TPD_PLOADOFFSET_MASK	0x00FF
#define TPD_PLOADOFFSET_SHIFT	16
#define TPD_CCSUMOFFSET_MASK	0x00FF
#define TPD_CCSUMOFFSET_SHIFT	24

struct tx_packet_desc {
	__le64 buffer_addr;
	__le32 word2;
	__le32 word3;
};

/* DMA Order Settings */
enum atl1_dma_order {
	atl1_dma_ord_in = 1,
	atl1_dma_ord_enh = 2,
	atl1_dma_ord_out = 4
};

enum atl1_dma_rcb {
	atl1_rcb_64 = 0,
	atl1_rcb_128 = 1
};

enum atl1_dma_req_block {
	atl1_dma_req_128 = 0,
	atl1_dma_req_256 = 1,
	atl1_dma_req_512 = 2,
	atl1_dma_req_1024 = 3,
	atl1_dma_req_2048 = 4,
	atl1_dma_req_4096 = 5
};

#define ATL1_MAX_INTR		3
#define ATL1_MAX_TX_BUF_LEN	0x3000	/* 12288 bytes */

#define ATL1_DEFAULT_TPD	256
#define ATL1_MAX_TPD		1024
#define ATL1_MIN_TPD		64
#define ATL1_DEFAULT_RFD	512
#define ATL1_MIN_RFD		128
#define ATL1_MAX_RFD		2048
#define ATL1_REG_COUNT		1538

#define ATL1_GET_DESC(R, i, type)	(&(((type *)((R)->desc))[i]))
#define ATL1_RFD_DESC(R, i)	ATL1_GET_DESC(R, i, struct rx_free_desc)
#define ATL1_TPD_DESC(R, i)	ATL1_GET_DESC(R, i, struct tx_packet_desc)
#define ATL1_RRD_DESC(R, i)	ATL1_GET_DESC(R, i, struct rx_return_desc)

/*
 * atl1_ring_header represents a single, contiguous block of DMA space
 * mapped for the three descriptor rings (tpd, rfd, rrd) and the two
 * message blocks (cmb, smb) described below
 */
struct atl1_ring_header {
	void *desc;		/* virtual address */
	dma_addr_t dma;		/* physical address*/
	unsigned int size;	/* length in bytes */
};

/*
 * atl1_buffer is wrapper around a pointer to a socket buffer
 * so a DMA handle can be stored along with the skb
 */
struct atl1_buffer {
	struct sk_buff *skb;	/* socket buffer */
	u16 length;		/* rx buffer length */
	u16 alloced;		/* 1 if skb allocated */
	dma_addr_t dma;
};

/* transmit packet descriptor (tpd) ring */
struct atl1_tpd_ring {
	void *desc;		/* descriptor ring virtual address */
	dma_addr_t dma;		/* descriptor ring physical address */
	u16 size;		/* descriptor ring length in bytes */
	u16 count;		/* number of descriptors in the ring */
	u16 hw_idx;		/* hardware index */
	atomic_t next_to_clean;
	atomic_t next_to_use;
	struct atl1_buffer *buffer_info;
};

/* receive free descriptor (rfd) ring */
struct atl1_rfd_ring {
	void *desc;		/* descriptor ring virtual address */
	dma_addr_t dma;		/* descriptor ring physical address */
	u16 size;		/* descriptor ring length in bytes */
	u16 count;		/* number of descriptors in the ring */
	atomic_t next_to_use;
	u16 next_to_clean;
	struct atl1_buffer *buffer_info;
};

/* receive return descriptor (rrd) ring */
struct atl1_rrd_ring {
	void *desc;		/* descriptor ring virtual address */
	dma_addr_t dma;		/* descriptor ring physical address */
	unsigned int size;	/* descriptor ring length in bytes */
	u16 count;		/* number of descriptors in the ring */
	u16 next_to_use;
	atomic_t next_to_clean;
};

/* coalescing message block (cmb) */
struct atl1_cmb {
	struct coals_msg_block *cmb;
	dma_addr_t dma;
};

/* statistics message block (smb) */
struct atl1_smb {
	struct stats_msg_block *smb;
	dma_addr_t dma;
};

/* Statistics counters */
struct atl1_sft_stats {
	u64 rx_packets;
	u64 tx_packets;
	u64 rx_bytes;
	u64 tx_bytes;
	u64 multicast;
	u64 collisions;
	u64 rx_errors;
	u64 rx_length_errors;
	u64 rx_crc_errors;
	u64 rx_frame_errors;
	u64 rx_fifo_errors;
	u64 rx_missed_errors;
	u64 tx_errors;
	u64 tx_fifo_errors;
	u64 tx_aborted_errors;
	u64 tx_window_errors;
	u64 tx_carrier_errors;
	u64 tx_pause;		/* TX pause frames */
	u64 excecol;		/* TX packets w/ excessive collisions */
	u64 deffer;		/* TX packets deferred */
	u64 scc;		/* packets TX after a single collision */
	u64 mcc;		/* packets TX after multiple collisions */
	u64 latecol;		/* TX packets w/ late collisions */
	u64 tx_underun;		/* TX packets aborted due to TX FIFO underrun
				 * or TRD FIFO underrun */
	u64 tx_trunc;		/* TX packets truncated due to size > MTU */
	u64 rx_pause;		/* num Pause packets received. */
	u64 rx_rrd_ov;
	u64 rx_trunc;
};

/* hardware structure */
struct atl1_hw {
	u8 __iomem *hw_addr;
	struct atl1_adapter *back;
	enum atl1_dma_order dma_ord;
	enum atl1_dma_rcb rcb_value;
	enum atl1_dma_req_block dmar_block;
	enum atl1_dma_req_block dmaw_block;
	u8 preamble_len;
	u8 max_retry;
	u8 jam_ipg;		/* IPG to start JAM for collision based flow
				 * control in half-duplex mode. In units of
				 * 8-bit time */
	u8 ipgt;		/* Desired back to back inter-packet gap.
				 * The default is 96-bit time */
	u8 min_ifg;		/* Minimum number of IFG to enforce in between
				 * receive frames. Frame gap below such IFP
				 * is dropped */
	u8 ipgr1;		/* 64bit Carrier-Sense window */
	u8 ipgr2;		/* 96-bit IPG window */
	u8 tpd_burst;		/* Number of TPD to prefetch in cache-aligned
				 * burst. Each TPD is 16 bytes long */
	u8 rfd_burst;		/* Number of RFD to prefetch in cache-aligned
				 * burst. Each RFD is 12 bytes long */
	u8 rfd_fetch_gap;
	u8 rrd_burst;		/* Threshold number of RRDs that can be retired
				 * in a burst. Each RRD is 16 bytes long */
	u8 tpd_fetch_th;
	u8 tpd_fetch_gap;
	u16 tx_jumbo_task_th;
	u16 txf_burst;		/* Number of data bytes to read in a cache-
				 * aligned burst. Each SRAM entry is 8 bytes */
	u16 rx_jumbo_th;	/* Jumbo packet size for non-VLAN packet. VLAN
				 * packets should add 4 bytes */
	u16 rx_jumbo_lkah;
	u16 rrd_ret_timer;	/* RRD retirement timer. Decrement by 1 after
				 * every 512ns passes. */
	u16 lcol;		/* Collision Window */

	u16 cmb_tpd;
	u16 cmb_rrd;
	u16 cmb_rx_timer;
	u16 cmb_tx_timer;
	u32 smb_timer;
	u16 media_type;
	u16 autoneg_advertised;

	u16 mii_autoneg_adv_reg;
	u16 mii_1000t_ctrl_reg;

	u32 max_frame_size;
	u32 min_frame_size;

	u16 dev_rev;

	/* spi flash */
	u8 flash_vendor;

	u8 mac_addr[ETH_ALEN];
	u8 perm_mac_addr[ETH_ALEN];

	bool phy_configured;
};

struct atl1_adapter {
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct net_device_stats net_stats;
	struct atl1_sft_stats soft_stats;
	struct vlan_group *vlgrp;
	u32 rx_buffer_len;
	u32 wol;
	u16 link_speed;
	u16 link_duplex;
	spinlock_t lock;
	struct work_struct tx_timeout_task;
	struct work_struct link_chg_task;
	struct work_struct pcie_dma_to_rst_task;
	struct timer_list watchdog_timer;
	struct timer_list phy_config_timer;
	bool phy_timer_pending;

	/* all descriptor rings' memory */
	struct atl1_ring_header ring_header;

	/* TX */
	struct atl1_tpd_ring tpd_ring;
	spinlock_t mb_lock;

	/* RX */
	struct atl1_rfd_ring rfd_ring;
	struct atl1_rrd_ring rrd_ring;
	u64 hw_csum_err;
	u64 hw_csum_good;
	u32 msg_enable;
	u16 imt;		/* interrupt moderator timer (2us resolution) */
	u16 ict;		/* interrupt clear timer (2us resolution */
	struct mii_if_info mii;	/* MII interface info */

	u32 bd_number;		/* board number */
	bool pci_using_64;
	struct atl1_hw hw;
	struct atl1_smb smb;
	struct atl1_cmb cmb;
};

#endif /* ATL1_H */
