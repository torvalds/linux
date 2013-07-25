/*
 * Copyright (c) 2013 Johannes Berg <johannes@sipsolutions.net>
 *
 *  This file is free software: you may copy, redistribute and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation, either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This file is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 * Copyright (c) 2012 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef ALX_REG_H
#define ALX_REG_H

#define ALX_DEV_ID_AR8161				0x1091
#define ALX_DEV_ID_E2200				0xe091
#define ALX_DEV_ID_AR8162				0x1090
#define ALX_DEV_ID_AR8171				0x10A1
#define ALX_DEV_ID_AR8172				0x10A0

/* rev definition,
 * bit(0): with xD support
 * bit(1): with Card Reader function
 * bit(7:2): real revision
 */
#define ALX_PCI_REVID_SHIFT				3
#define ALX_REV_A0					0
#define ALX_REV_A1					1
#define ALX_REV_B0					2
#define ALX_REV_C0					3

#define ALX_DEV_CTRL					0x0060
#define ALX_DEV_CTRL_MAXRRS_MIN				2

#define ALX_MSIX_MASK					0x0090

#define ALX_UE_SVRT					0x010C
#define ALX_UE_SVRT_FCPROTERR				BIT(13)
#define ALX_UE_SVRT_DLPROTERR				BIT(4)

/* eeprom & flash load register */
#define ALX_EFLD					0x0204
#define ALX_EFLD_F_EXIST				BIT(10)
#define ALX_EFLD_E_EXIST				BIT(9)
#define ALX_EFLD_STAT					BIT(5)
#define ALX_EFLD_START					BIT(0)

/* eFuse load register */
#define ALX_SLD						0x0218
#define ALX_SLD_STAT					BIT(12)
#define ALX_SLD_START					BIT(11)
#define ALX_SLD_MAX_TO					100

#define ALX_PDLL_TRNS1					0x1104
#define ALX_PDLL_TRNS1_D3PLLOFF_EN			BIT(11)

#define ALX_PMCTRL					0x12F8
#define ALX_PMCTRL_HOTRST_WTEN				BIT(31)
/* bit30: L0s/L1 controlled by MAC based on throughput(setting in 15A0) */
#define ALX_PMCTRL_ASPM_FCEN				BIT(30)
#define ALX_PMCTRL_SADLY_EN				BIT(29)
#define ALX_PMCTRL_LCKDET_TIMER_MASK			0xF
#define ALX_PMCTRL_LCKDET_TIMER_SHIFT			24
#define ALX_PMCTRL_LCKDET_TIMER_DEF			0xC
/* bit[23:20] if pm_request_l1 time > @, then enter L0s not L1 */
#define ALX_PMCTRL_L1REQ_TO_MASK			0xF
#define ALX_PMCTRL_L1REQ_TO_SHIFT			20
#define ALX_PMCTRL_L1REG_TO_DEF				0xF
#define ALX_PMCTRL_TXL1_AFTER_L0S			BIT(19)
#define ALX_PMCTRL_L1_TIMER_MASK			0x7
#define ALX_PMCTRL_L1_TIMER_SHIFT			16
#define ALX_PMCTRL_L1_TIMER_16US			4
#define ALX_PMCTRL_RCVR_WT_1US				BIT(15)
/* bit13: enable pcie clk switch in L1 state */
#define ALX_PMCTRL_L1_CLKSW_EN				BIT(13)
#define ALX_PMCTRL_L0S_EN				BIT(12)
#define ALX_PMCTRL_RXL1_AFTER_L0S			BIT(11)
#define ALX_PMCTRL_L1_BUFSRX_EN				BIT(7)
/* bit6: power down serdes RX */
#define ALX_PMCTRL_L1_SRDSRX_PWD			BIT(6)
#define ALX_PMCTRL_L1_SRDSPLL_EN			BIT(5)
#define ALX_PMCTRL_L1_SRDS_EN				BIT(4)
#define ALX_PMCTRL_L1_EN				BIT(3)

/*******************************************************/
/* following registers are mapped only to memory space */
/*******************************************************/

#define ALX_MASTER					0x1400
/* bit12: 1:alwys select pclk from serdes, not sw to 25M */
#define ALX_MASTER_PCLKSEL_SRDS				BIT(12)
/* bit11: irq moduration for rx */
#define ALX_MASTER_IRQMOD2_EN				BIT(11)
/* bit10: irq moduration for tx/rx */
#define ALX_MASTER_IRQMOD1_EN				BIT(10)
#define ALX_MASTER_SYSALVTIMER_EN			BIT(7)
#define ALX_MASTER_OOB_DIS				BIT(6)
/* bit5: wakeup without pcie clk */
#define ALX_MASTER_WAKEN_25M				BIT(5)
/* bit0: MAC & DMA reset */
#define ALX_MASTER_DMA_MAC_RST				BIT(0)
#define ALX_DMA_MAC_RST_TO				50

#define ALX_IRQ_MODU_TIMER				0x1408
#define ALX_IRQ_MODU_TIMER1_MASK			0xFFFF
#define ALX_IRQ_MODU_TIMER1_SHIFT			0

#define ALX_PHY_CTRL					0x140C
#define ALX_PHY_CTRL_100AB_EN				BIT(17)
/* bit14: affect MAC & PHY, go to low power sts */
#define ALX_PHY_CTRL_POWER_DOWN				BIT(14)
/* bit13: 1:pll always ON, 0:can switch in lpw */
#define ALX_PHY_CTRL_PLL_ON				BIT(13)
#define ALX_PHY_CTRL_RST_ANALOG				BIT(12)
#define ALX_PHY_CTRL_HIB_PULSE				BIT(11)
#define ALX_PHY_CTRL_HIB_EN				BIT(10)
#define ALX_PHY_CTRL_IDDQ				BIT(7)
#define ALX_PHY_CTRL_GATE_25M				BIT(5)
#define ALX_PHY_CTRL_LED_MODE				BIT(2)
/* bit0: out of dsp RST state */
#define ALX_PHY_CTRL_DSPRST_OUT				BIT(0)
#define ALX_PHY_CTRL_DSPRST_TO				80
#define ALX_PHY_CTRL_CLS	(ALX_PHY_CTRL_LED_MODE | \
				 ALX_PHY_CTRL_100AB_EN | \
				 ALX_PHY_CTRL_PLL_ON)

#define ALX_MAC_STS					0x1410
#define ALX_MAC_STS_TXQ_BUSY				BIT(3)
#define ALX_MAC_STS_RXQ_BUSY				BIT(2)
#define ALX_MAC_STS_TXMAC_BUSY				BIT(1)
#define ALX_MAC_STS_RXMAC_BUSY				BIT(0)
#define ALX_MAC_STS_IDLE	(ALX_MAC_STS_TXQ_BUSY | \
				 ALX_MAC_STS_RXQ_BUSY | \
				 ALX_MAC_STS_TXMAC_BUSY | \
				 ALX_MAC_STS_RXMAC_BUSY)

#define ALX_MDIO					0x1414
#define ALX_MDIO_MODE_EXT				BIT(30)
#define ALX_MDIO_BUSY					BIT(27)
#define ALX_MDIO_CLK_SEL_MASK				0x7
#define ALX_MDIO_CLK_SEL_SHIFT				24
#define ALX_MDIO_CLK_SEL_25MD4				0
#define ALX_MDIO_CLK_SEL_25MD128			7
#define ALX_MDIO_START					BIT(23)
#define ALX_MDIO_SPRES_PRMBL				BIT(22)
/* bit21: 1:read,0:write */
#define ALX_MDIO_OP_READ				BIT(21)
#define ALX_MDIO_REG_MASK				0x1F
#define ALX_MDIO_REG_SHIFT				16
#define ALX_MDIO_DATA_MASK				0xFFFF
#define ALX_MDIO_DATA_SHIFT				0
#define ALX_MDIO_MAX_AC_TO				120

#define ALX_MDIO_EXTN					0x1448
#define ALX_MDIO_EXTN_DEVAD_MASK			0x1F
#define ALX_MDIO_EXTN_DEVAD_SHIFT			16
#define ALX_MDIO_EXTN_REG_MASK				0xFFFF
#define ALX_MDIO_EXTN_REG_SHIFT				0

#define ALX_SERDES					0x1424
#define ALX_SERDES_PHYCLK_SLWDWN			BIT(18)
#define ALX_SERDES_MACCLK_SLWDWN			BIT(17)

#define ALX_LPI_CTRL					0x1440
#define ALX_LPI_CTRL_EN					BIT(0)

/* for B0+, bit[13..] for C0+ */
#define ALX_HRTBT_EXT_CTRL				0x1AD0
#define L1F_HRTBT_EXT_CTRL_PERIOD_HIGH_MASK		0x3F
#define L1F_HRTBT_EXT_CTRL_PERIOD_HIGH_SHIFT		24
#define L1F_HRTBT_EXT_CTRL_SWOI_STARTUP_PKT_EN		BIT(23)
#define L1F_HRTBT_EXT_CTRL_IOAC_2_FRAGMENTED		BIT(22)
#define L1F_HRTBT_EXT_CTRL_IOAC_1_FRAGMENTED		BIT(21)
#define L1F_HRTBT_EXT_CTRL_IOAC_1_KEEPALIVE_EN		BIT(20)
#define L1F_HRTBT_EXT_CTRL_IOAC_1_HAS_VLAN		BIT(19)
#define L1F_HRTBT_EXT_CTRL_IOAC_1_IS_8023		BIT(18)
#define L1F_HRTBT_EXT_CTRL_IOAC_1_IS_IPV6		BIT(17)
#define L1F_HRTBT_EXT_CTRL_IOAC_2_KEEPALIVE_EN		BIT(16)
#define L1F_HRTBT_EXT_CTRL_IOAC_2_HAS_VLAN		BIT(15)
#define L1F_HRTBT_EXT_CTRL_IOAC_2_IS_8023		BIT(14)
#define L1F_HRTBT_EXT_CTRL_IOAC_2_IS_IPV6		BIT(13)
#define ALX_HRTBT_EXT_CTRL_NS_EN			BIT(12)
#define ALX_HRTBT_EXT_CTRL_FRAG_LEN_MASK		0xFF
#define ALX_HRTBT_EXT_CTRL_FRAG_LEN_SHIFT		4
#define ALX_HRTBT_EXT_CTRL_IS_8023			BIT(3)
#define ALX_HRTBT_EXT_CTRL_IS_IPV6			BIT(2)
#define ALX_HRTBT_EXT_CTRL_WAKEUP_EN			BIT(1)
#define ALX_HRTBT_EXT_CTRL_ARP_EN			BIT(0)

#define ALX_HRTBT_REM_IPV4_ADDR				0x1AD4
#define ALX_HRTBT_HOST_IPV4_ADDR			0x1478
#define ALX_HRTBT_REM_IPV6_ADDR3			0x1AD8
#define ALX_HRTBT_REM_IPV6_ADDR2			0x1ADC
#define ALX_HRTBT_REM_IPV6_ADDR1			0x1AE0
#define ALX_HRTBT_REM_IPV6_ADDR0			0x1AE4

/* 1B8C ~ 1B94 for C0+ */
#define ALX_SWOI_ACER_CTRL				0x1B8C
#define ALX_SWOI_ORIG_ACK_NAK_EN			BIT(20)
#define ALX_SWOI_ORIG_ACK_NAK_PKT_LEN_MASK		0XFF
#define ALX_SWOI_ORIG_ACK_NAK_PKT_LEN_SHIFT		12
#define ALX_SWOI_ORIG_ACK_ADDR_MASK			0XFFF
#define ALX_SWOI_ORIG_ACK_ADDR_SHIFT			0

#define ALX_SWOI_IOAC_CTRL_2				0x1B90
#define ALX_SWOI_IOAC_CTRL_2_SWOI_1_FRAG_LEN_MASK	0xFF
#define ALX_SWOI_IOAC_CTRL_2_SWOI_1_FRAG_LEN_SHIFT	24
#define ALX_SWOI_IOAC_CTRL_2_SWOI_1_PKT_LEN_MASK	0xFFF
#define ALX_SWOI_IOAC_CTRL_2_SWOI_1_PKT_LEN_SHIFT	12
#define ALX_SWOI_IOAC_CTRL_2_SWOI_1_HDR_ADDR_MASK	0xFFF
#define ALX_SWOI_IOAC_CTRL_2_SWOI_1_HDR_ADDR_SHIFT	0

#define ALX_SWOI_IOAC_CTRL_3				0x1B94
#define ALX_SWOI_IOAC_CTRL_3_SWOI_2_FRAG_LEN_MASK	0xFF
#define ALX_SWOI_IOAC_CTRL_3_SWOI_2_FRAG_LEN_SHIFT	24
#define ALX_SWOI_IOAC_CTRL_3_SWOI_2_PKT_LEN_MASK	0xFFF
#define ALX_SWOI_IOAC_CTRL_3_SWOI_2_PKT_LEN_SHIFT	12
#define ALX_SWOI_IOAC_CTRL_3_SWOI_2_HDR_ADDR_MASK	0xFFF
#define ALX_SWOI_IOAC_CTRL_3_SWOI_2_HDR_ADDR_SHIFT	0

/* for B0 */
#define ALX_IDLE_DECISN_TIMER				0x1474
/* 1ms */
#define ALX_IDLE_DECISN_TIMER_DEF			0x400

#define ALX_MAC_CTRL					0x1480
#define ALX_MAC_CTRL_FAST_PAUSE				BIT(31)
#define ALX_MAC_CTRL_WOLSPED_SWEN			BIT(30)
/* bit29: 1:legacy(hi5b), 0:marvl(lo5b)*/
#define ALX_MAC_CTRL_MHASH_ALG_HI5B			BIT(29)
#define ALX_MAC_CTRL_BRD_EN				BIT(26)
#define ALX_MAC_CTRL_MULTIALL_EN			BIT(25)
#define ALX_MAC_CTRL_SPEED_MASK				0x3
#define ALX_MAC_CTRL_SPEED_SHIFT			20
#define ALX_MAC_CTRL_SPEED_10_100			1
#define ALX_MAC_CTRL_SPEED_1000				2
#define ALX_MAC_CTRL_PROMISC_EN				BIT(15)
#define ALX_MAC_CTRL_VLANSTRIP				BIT(14)
#define ALX_MAC_CTRL_PRMBLEN_MASK			0xF
#define ALX_MAC_CTRL_PRMBLEN_SHIFT			10
#define ALX_MAC_CTRL_PCRCE				BIT(7)
#define ALX_MAC_CTRL_CRCE				BIT(6)
#define ALX_MAC_CTRL_FULLD				BIT(5)
#define ALX_MAC_CTRL_RXFC_EN				BIT(3)
#define ALX_MAC_CTRL_TXFC_EN				BIT(2)
#define ALX_MAC_CTRL_RX_EN				BIT(1)
#define ALX_MAC_CTRL_TX_EN				BIT(0)

#define ALX_STAD0					0x1488
#define ALX_STAD1					0x148C

#define ALX_HASH_TBL0					0x1490
#define ALX_HASH_TBL1					0x1494

#define ALX_MTU						0x149C
#define ALX_MTU_JUMBO_TH				1514
#define ALX_MTU_STD_ALGN				1536

#define ALX_SRAM5					0x1524
#define ALX_SRAM_RXF_LEN_MASK				0xFFF
#define ALX_SRAM_RXF_LEN_SHIFT				0
#define ALX_SRAM_RXF_LEN_8K				(8*1024)

#define ALX_SRAM9					0x1534
#define ALX_SRAM_LOAD_PTR				BIT(0)

#define ALX_RX_BASE_ADDR_HI				0x1540

#define ALX_TX_BASE_ADDR_HI				0x1544

#define ALX_RFD_ADDR_LO					0x1550
#define ALX_RFD_RING_SZ					0x1560
#define ALX_RFD_BUF_SZ					0x1564

#define ALX_RRD_ADDR_LO					0x1568
#define ALX_RRD_RING_SZ					0x1578

/* pri3: highest, pri0: lowest */
#define ALX_TPD_PRI3_ADDR_LO				0x14E4
#define ALX_TPD_PRI2_ADDR_LO				0x14E0
#define ALX_TPD_PRI1_ADDR_LO				0x157C
#define ALX_TPD_PRI0_ADDR_LO				0x1580

/* producer index is 16bit */
#define ALX_TPD_PRI3_PIDX				0x1618
#define ALX_TPD_PRI2_PIDX				0x161A
#define ALX_TPD_PRI1_PIDX				0x15F0
#define ALX_TPD_PRI0_PIDX				0x15F2

/* consumer index is 16bit */
#define ALX_TPD_PRI3_CIDX				0x161C
#define ALX_TPD_PRI2_CIDX				0x161E
#define ALX_TPD_PRI1_CIDX				0x15F4
#define ALX_TPD_PRI0_CIDX				0x15F6

#define ALX_TPD_RING_SZ					0x1584

#define ALX_TXQ0					0x1590
#define ALX_TXQ0_TXF_BURST_PREF_MASK			0xFFFF
#define ALX_TXQ0_TXF_BURST_PREF_SHIFT			16
#define ALX_TXQ_TXF_BURST_PREF_DEF			0x200
#define ALX_TXQ0_LSO_8023_EN				BIT(7)
#define ALX_TXQ0_MODE_ENHANCE				BIT(6)
#define ALX_TXQ0_EN					BIT(5)
#define ALX_TXQ0_SUPT_IPOPT				BIT(4)
#define ALX_TXQ0_TPD_BURSTPREF_MASK			0xF
#define ALX_TXQ0_TPD_BURSTPREF_SHIFT			0
#define ALX_TXQ_TPD_BURSTPREF_DEF			5

#define ALX_TXQ1					0x1594
/* bit11:  drop large packet, len > (rfd buf) */
#define ALX_TXQ1_ERRLGPKT_DROP_EN			BIT(11)
#define ALX_TXQ1_JUMBO_TSO_TH				(7*1024)

#define ALX_RXQ0					0x15A0
#define ALX_RXQ0_EN					BIT(31)
#define ALX_RXQ0_RSS_HASH_EN				BIT(29)
#define ALX_RXQ0_RSS_MODE_MASK				0x3
#define ALX_RXQ0_RSS_MODE_SHIFT				26
#define ALX_RXQ0_RSS_MODE_DIS				0
#define ALX_RXQ0_RSS_MODE_MQMI				3
#define ALX_RXQ0_NUM_RFD_PREF_MASK			0x3F
#define ALX_RXQ0_NUM_RFD_PREF_SHIFT			20
#define ALX_RXQ0_NUM_RFD_PREF_DEF			8
#define ALX_RXQ0_IDT_TBL_SIZE_MASK			0x1FF
#define ALX_RXQ0_IDT_TBL_SIZE_SHIFT			8
#define ALX_RXQ0_IDT_TBL_SIZE_DEF			0x100
#define ALX_RXQ0_IDT_TBL_SIZE_NORMAL			128
#define ALX_RXQ0_IPV6_PARSE_EN				BIT(7)
#define ALX_RXQ0_RSS_HSTYP_MASK				0xF
#define ALX_RXQ0_RSS_HSTYP_SHIFT			2
#define ALX_RXQ0_RSS_HSTYP_IPV6_TCP_EN			BIT(5)
#define ALX_RXQ0_RSS_HSTYP_IPV6_EN			BIT(4)
#define ALX_RXQ0_RSS_HSTYP_IPV4_TCP_EN			BIT(3)
#define ALX_RXQ0_RSS_HSTYP_IPV4_EN			BIT(2)
#define ALX_RXQ0_RSS_HSTYP_ALL		(ALX_RXQ0_RSS_HSTYP_IPV6_TCP_EN | \
					 ALX_RXQ0_RSS_HSTYP_IPV4_TCP_EN | \
					 ALX_RXQ0_RSS_HSTYP_IPV6_EN | \
					 ALX_RXQ0_RSS_HSTYP_IPV4_EN)
#define ALX_RXQ0_ASPM_THRESH_MASK			0x3
#define ALX_RXQ0_ASPM_THRESH_SHIFT			0
#define ALX_RXQ0_ASPM_THRESH_100M			3

#define ALX_RXQ2					0x15A8
#define ALX_RXQ2_RXF_XOFF_THRESH_MASK			0xFFF
#define ALX_RXQ2_RXF_XOFF_THRESH_SHIFT			16
#define ALX_RXQ2_RXF_XON_THRESH_MASK			0xFFF
#define ALX_RXQ2_RXF_XON_THRESH_SHIFT			0
/* Size = tx-packet(1522) + IPG(12) + SOF(8) + 64(Pause) + IPG(12) + SOF(8) +
 *        rx-packet(1522) + delay-of-link(64)
 *      = 3212.
 */
#define ALX_RXQ2_RXF_FLOW_CTRL_RSVD			3212

#define ALX_DMA						0x15C0
#define ALX_DMA_RCHNL_SEL_MASK				0x3
#define ALX_DMA_RCHNL_SEL_SHIFT				26
#define ALX_DMA_WDLY_CNT_MASK				0xF
#define ALX_DMA_WDLY_CNT_SHIFT				16
#define ALX_DMA_WDLY_CNT_DEF				4
#define ALX_DMA_RDLY_CNT_MASK				0x1F
#define ALX_DMA_RDLY_CNT_SHIFT				11
#define ALX_DMA_RDLY_CNT_DEF				15
/* bit10: 0:tpd with pri, 1: data */
#define ALX_DMA_RREQ_PRI_DATA				BIT(10)
#define ALX_DMA_RREQ_BLEN_MASK				0x7
#define ALX_DMA_RREQ_BLEN_SHIFT				4
#define ALX_DMA_RORDER_MODE_MASK			0x7
#define ALX_DMA_RORDER_MODE_SHIFT			0
#define ALX_DMA_RORDER_MODE_OUT				4

#define ALX_WOL0					0x14A0
#define ALX_WOL0_PME_LINK				BIT(5)
#define ALX_WOL0_LINK_EN				BIT(4)
#define ALX_WOL0_PME_MAGIC_EN				BIT(3)
#define ALX_WOL0_MAGIC_EN				BIT(2)

#define ALX_RFD_PIDX					0x15E0

#define ALX_RFD_CIDX					0x15F8

/* MIB */
#define ALX_MIB_BASE					0x1700
#define ALX_MIB_RX_OK					(ALX_MIB_BASE + 0)
#define ALX_MIB_RX_ERRADDR				(ALX_MIB_BASE + 92)
#define ALX_MIB_TX_OK					(ALX_MIB_BASE + 96)
#define ALX_MIB_TX_MCCNT				(ALX_MIB_BASE + 192)

#define ALX_RX_STATS_BIN				ALX_MIB_RX_OK
#define ALX_RX_STATS_END				ALX_MIB_RX_ERRADDR
#define ALX_TX_STATS_BIN				ALX_MIB_TX_OK
#define ALX_TX_STATS_END				ALX_MIB_TX_MCCNT

#define ALX_ISR						0x1600
#define ALX_ISR_DIS					BIT(31)
#define ALX_ISR_RX_Q7					BIT(30)
#define ALX_ISR_RX_Q6					BIT(29)
#define ALX_ISR_RX_Q5					BIT(28)
#define ALX_ISR_RX_Q4					BIT(27)
#define ALX_ISR_PCIE_LNKDOWN				BIT(26)
#define ALX_ISR_RX_Q3					BIT(19)
#define ALX_ISR_RX_Q2					BIT(18)
#define ALX_ISR_RX_Q1					BIT(17)
#define ALX_ISR_RX_Q0					BIT(16)
#define ALX_ISR_TX_Q0					BIT(15)
#define ALX_ISR_PHY					BIT(12)
#define ALX_ISR_DMAW					BIT(10)
#define ALX_ISR_DMAR					BIT(9)
#define ALX_ISR_TXF_UR					BIT(8)
#define ALX_ISR_TX_Q3					BIT(7)
#define ALX_ISR_TX_Q2					BIT(6)
#define ALX_ISR_TX_Q1					BIT(5)
#define ALX_ISR_RFD_UR					BIT(4)
#define ALX_ISR_RXF_OV					BIT(3)
#define ALX_ISR_MANU					BIT(2)
#define ALX_ISR_TIMER					BIT(1)
#define ALX_ISR_SMB					BIT(0)

#define ALX_IMR						0x1604

/* re-send assert msg if SW no response */
#define ALX_INT_RETRIG					0x1608
/* 40ms */
#define ALX_INT_RETRIG_TO				20000

#define ALX_SMB_TIMER					0x15C4

#define ALX_TINT_TPD_THRSHLD				0x15C8

#define ALX_TINT_TIMER					0x15CC

#define ALX_CLK_GATE					0x1814
#define ALX_CLK_GATE_RXMAC				BIT(5)
#define ALX_CLK_GATE_TXMAC				BIT(4)
#define ALX_CLK_GATE_RXQ				BIT(3)
#define ALX_CLK_GATE_TXQ				BIT(2)
#define ALX_CLK_GATE_DMAR				BIT(1)
#define ALX_CLK_GATE_DMAW				BIT(0)
#define ALX_CLK_GATE_ALL		(ALX_CLK_GATE_RXMAC | \
					 ALX_CLK_GATE_TXMAC | \
					 ALX_CLK_GATE_RXQ | \
					 ALX_CLK_GATE_TXQ | \
					 ALX_CLK_GATE_DMAR | \
					 ALX_CLK_GATE_DMAW)

/* interop between drivers */
#define ALX_DRV						0x1804
#define ALX_DRV_PHY_AUTO				BIT(28)
#define ALX_DRV_PHY_1000				BIT(27)
#define ALX_DRV_PHY_100					BIT(26)
#define ALX_DRV_PHY_10					BIT(25)
#define ALX_DRV_PHY_DUPLEX				BIT(24)
/* bit23: adv Pause */
#define ALX_DRV_PHY_PAUSE				BIT(23)
/* bit22: adv Asym Pause */
#define ALX_DRV_PHY_MASK				0xFF
#define ALX_DRV_PHY_SHIFT				21
#define ALX_DRV_PHY_UNKNOWN				0

/* flag of phy inited */
#define ALX_PHY_INITED					0x003F

/* reg 1830 ~ 186C for C0+, 16 bit map patterns and wake packet detection */
#define ALX_WOL_CTRL2					0x1830
#define ALX_WOL_CTRL2_DATA_STORE			BIT(3)
#define ALX_WOL_CTRL2_PTRN_EVT				BIT(2)
#define ALX_WOL_CTRL2_PME_PTRN_EN			BIT(1)
#define ALX_WOL_CTRL2_PTRN_EN				BIT(0)

#define ALX_WOL_CTRL3					0x1834
#define ALX_WOL_CTRL3_PTRN_ADDR_MASK			0xFFFFF
#define ALX_WOL_CTRL3_PTRN_ADDR_SHIFT			0

#define ALX_WOL_CTRL4					0x1838
#define ALX_WOL_CTRL4_PT15_MATCH			BIT(31)
#define ALX_WOL_CTRL4_PT14_MATCH			BIT(30)
#define ALX_WOL_CTRL4_PT13_MATCH			BIT(29)
#define ALX_WOL_CTRL4_PT12_MATCH			BIT(28)
#define ALX_WOL_CTRL4_PT11_MATCH			BIT(27)
#define ALX_WOL_CTRL4_PT10_MATCH			BIT(26)
#define ALX_WOL_CTRL4_PT9_MATCH				BIT(25)
#define ALX_WOL_CTRL4_PT8_MATCH				BIT(24)
#define ALX_WOL_CTRL4_PT7_MATCH				BIT(23)
#define ALX_WOL_CTRL4_PT6_MATCH				BIT(22)
#define ALX_WOL_CTRL4_PT5_MATCH				BIT(21)
#define ALX_WOL_CTRL4_PT4_MATCH				BIT(20)
#define ALX_WOL_CTRL4_PT3_MATCH				BIT(19)
#define ALX_WOL_CTRL4_PT2_MATCH				BIT(18)
#define ALX_WOL_CTRL4_PT1_MATCH				BIT(17)
#define ALX_WOL_CTRL4_PT0_MATCH				BIT(16)
#define ALX_WOL_CTRL4_PT15_EN				BIT(15)
#define ALX_WOL_CTRL4_PT14_EN				BIT(14)
#define ALX_WOL_CTRL4_PT13_EN				BIT(13)
#define ALX_WOL_CTRL4_PT12_EN				BIT(12)
#define ALX_WOL_CTRL4_PT11_EN				BIT(11)
#define ALX_WOL_CTRL4_PT10_EN				BIT(10)
#define ALX_WOL_CTRL4_PT9_EN				BIT(9)
#define ALX_WOL_CTRL4_PT8_EN				BIT(8)
#define ALX_WOL_CTRL4_PT7_EN				BIT(7)
#define ALX_WOL_CTRL4_PT6_EN				BIT(6)
#define ALX_WOL_CTRL4_PT5_EN				BIT(5)
#define ALX_WOL_CTRL4_PT4_EN				BIT(4)
#define ALX_WOL_CTRL4_PT3_EN				BIT(3)
#define ALX_WOL_CTRL4_PT2_EN				BIT(2)
#define ALX_WOL_CTRL4_PT1_EN				BIT(1)
#define ALX_WOL_CTRL4_PT0_EN				BIT(0)

#define ALX_WOL_CTRL5					0x183C
#define ALX_WOL_CTRL5_PT3_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT3_LEN_SHIFT			24
#define ALX_WOL_CTRL5_PT2_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT2_LEN_SHIFT			16
#define ALX_WOL_CTRL5_PT1_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT1_LEN_SHIFT			8
#define ALX_WOL_CTRL5_PT0_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT0_LEN_SHIFT			0

#define ALX_WOL_CTRL6					0x1840
#define ALX_WOL_CTRL5_PT7_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT7_LEN_SHIFT			24
#define ALX_WOL_CTRL5_PT6_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT6_LEN_SHIFT			16
#define ALX_WOL_CTRL5_PT5_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT5_LEN_SHIFT			8
#define ALX_WOL_CTRL5_PT4_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT4_LEN_SHIFT			0

#define ALX_WOL_CTRL7					0x1844
#define ALX_WOL_CTRL5_PT11_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT11_LEN_SHIFT			24
#define ALX_WOL_CTRL5_PT10_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT10_LEN_SHIFT			16
#define ALX_WOL_CTRL5_PT9_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT9_LEN_SHIFT			8
#define ALX_WOL_CTRL5_PT8_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT8_LEN_SHIFT			0

#define ALX_WOL_CTRL8					0x1848
#define ALX_WOL_CTRL5_PT15_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT15_LEN_SHIFT			24
#define ALX_WOL_CTRL5_PT14_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT14_LEN_SHIFT			16
#define ALX_WOL_CTRL5_PT13_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT13_LEN_SHIFT			8
#define ALX_WOL_CTRL5_PT12_LEN_MASK			0xFF
#define ALX_WOL_CTRL5_PT12_LEN_SHIFT			0

#define ALX_ACER_FIXED_PTN0				0x1850
#define ALX_ACER_FIXED_PTN0_MASK			0xFFFFFFFF
#define ALX_ACER_FIXED_PTN0_SHIFT			0

#define ALX_ACER_FIXED_PTN1				0x1854
#define ALX_ACER_FIXED_PTN1_MASK			0xFFFF
#define ALX_ACER_FIXED_PTN1_SHIFT			0

#define ALX_ACER_RANDOM_NUM0				0x1858
#define ALX_ACER_RANDOM_NUM0_MASK			0xFFFFFFFF
#define ALX_ACER_RANDOM_NUM0_SHIFT			0

#define ALX_ACER_RANDOM_NUM1				0x185C
#define ALX_ACER_RANDOM_NUM1_MASK			0xFFFFFFFF
#define ALX_ACER_RANDOM_NUM1_SHIFT			0

#define ALX_ACER_RANDOM_NUM2				0x1860
#define ALX_ACER_RANDOM_NUM2_MASK			0xFFFFFFFF
#define ALX_ACER_RANDOM_NUM2_SHIFT			0

#define ALX_ACER_RANDOM_NUM3				0x1864
#define ALX_ACER_RANDOM_NUM3_MASK			0xFFFFFFFF
#define ALX_ACER_RANDOM_NUM3_SHIFT			0

#define ALX_ACER_MAGIC					0x1868
#define ALX_ACER_MAGIC_EN				BIT(31)
#define ALX_ACER_MAGIC_PME_EN				BIT(30)
#define ALX_ACER_MAGIC_MATCH				BIT(29)
#define ALX_ACER_MAGIC_FF_CHECK				BIT(10)
#define ALX_ACER_MAGIC_RAN_LEN_MASK			0x1F
#define ALX_ACER_MAGIC_RAN_LEN_SHIFT			5
#define ALX_ACER_MAGIC_FIX_LEN_MASK			0x1F
#define ALX_ACER_MAGIC_FIX_LEN_SHIFT			0

#define ALX_ACER_TIMER					0x186C
#define ALX_ACER_TIMER_EN				BIT(31)
#define ALX_ACER_TIMER_PME_EN				BIT(30)
#define ALX_ACER_TIMER_MATCH				BIT(29)
#define ALX_ACER_TIMER_THRES_MASK			0x1FFFF
#define ALX_ACER_TIMER_THRES_SHIFT			0
#define ALX_ACER_TIMER_THRES_DEF			1

/* RSS definitions */
#define ALX_RSS_KEY0					0x14B0
#define ALX_RSS_KEY1					0x14B4
#define ALX_RSS_KEY2					0x14B8
#define ALX_RSS_KEY3					0x14BC
#define ALX_RSS_KEY4					0x14C0
#define ALX_RSS_KEY5					0x14C4
#define ALX_RSS_KEY6					0x14C8
#define ALX_RSS_KEY7					0x14CC
#define ALX_RSS_KEY8					0x14D0
#define ALX_RSS_KEY9					0x14D4

#define ALX_RSS_IDT_TBL0				0x1B00

#define ALX_MSI_MAP_TBL1				0x15D0
#define ALX_MSI_MAP_TBL1_TXQ1_SHIFT			20
#define ALX_MSI_MAP_TBL1_TXQ0_SHIFT			16
#define ALX_MSI_MAP_TBL1_RXQ3_SHIFT			12
#define ALX_MSI_MAP_TBL1_RXQ2_SHIFT			8
#define ALX_MSI_MAP_TBL1_RXQ1_SHIFT			4
#define ALX_MSI_MAP_TBL1_RXQ0_SHIFT			0

#define ALX_MSI_MAP_TBL2				0x15D8
#define ALX_MSI_MAP_TBL2_TXQ3_SHIFT			20
#define ALX_MSI_MAP_TBL2_TXQ2_SHIFT			16
#define ALX_MSI_MAP_TBL2_RXQ7_SHIFT			12
#define ALX_MSI_MAP_TBL2_RXQ6_SHIFT			8
#define ALX_MSI_MAP_TBL2_RXQ5_SHIFT			4
#define ALX_MSI_MAP_TBL2_RXQ4_SHIFT			0

#define ALX_MSI_ID_MAP					0x15D4

#define ALX_MSI_RETRANS_TIMER				0x1920
/* bit16: 1:line,0:standard */
#define ALX_MSI_MASK_SEL_LINE				BIT(16)
#define ALX_MSI_RETRANS_TM_MASK				0xFFFF
#define ALX_MSI_RETRANS_TM_SHIFT			0

/* CR DMA ctrl */

/* TX QoS */
#define ALX_WRR						0x1938
#define ALX_WRR_PRI_MASK				0x3
#define ALX_WRR_PRI_SHIFT				29
#define ALX_WRR_PRI_RESTRICT_NONE			3
#define ALX_WRR_PRI3_MASK				0x1F
#define ALX_WRR_PRI3_SHIFT				24
#define ALX_WRR_PRI2_MASK				0x1F
#define ALX_WRR_PRI2_SHIFT				16
#define ALX_WRR_PRI1_MASK				0x1F
#define ALX_WRR_PRI1_SHIFT				8
#define ALX_WRR_PRI0_MASK				0x1F
#define ALX_WRR_PRI0_SHIFT				0

#define ALX_HQTPD					0x193C
#define ALX_HQTPD_BURST_EN				BIT(31)
#define ALX_HQTPD_Q3_NUMPREF_MASK			0xF
#define ALX_HQTPD_Q3_NUMPREF_SHIFT			8
#define ALX_HQTPD_Q2_NUMPREF_MASK			0xF
#define ALX_HQTPD_Q2_NUMPREF_SHIFT			4
#define ALX_HQTPD_Q1_NUMPREF_MASK			0xF
#define ALX_HQTPD_Q1_NUMPREF_SHIFT			0

#define ALX_MISC					0x19C0
#define ALX_MISC_PSW_OCP_MASK				0x7
#define ALX_MISC_PSW_OCP_SHIFT				21
#define ALX_MISC_PSW_OCP_DEF				0x7
#define ALX_MISC_ISO_EN					BIT(12)
#define ALX_MISC_INTNLOSC_OPEN				BIT(3)

#define ALX_MSIC2					0x19C8
#define ALX_MSIC2_CALB_START				BIT(0)

#define ALX_MISC3					0x19CC
/* bit1: 1:Software control 25M */
#define ALX_MISC3_25M_BY_SW				BIT(1)
/* bit0: 25M switch to intnl OSC */
#define ALX_MISC3_25M_NOTO_INTNL			BIT(0)

/* MSIX tbl in memory space */
#define ALX_MSIX_ENTRY_BASE				0x2000

/********************* PHY regs definition ***************************/

/* PHY Specific Status Register */
#define ALX_MII_GIGA_PSSR				0x11
#define ALX_GIGA_PSSR_SPD_DPLX_RESOLVED			0x0800
#define ALX_GIGA_PSSR_DPLX				0x2000
#define ALX_GIGA_PSSR_SPEED				0xC000
#define ALX_GIGA_PSSR_10MBS				0x0000
#define ALX_GIGA_PSSR_100MBS				0x4000
#define ALX_GIGA_PSSR_1000MBS				0x8000

/* PHY Interrupt Enable Register */
#define ALX_MII_IER					0x12
#define ALX_IER_LINK_UP					0x0400
#define ALX_IER_LINK_DOWN				0x0800

/* PHY Interrupt Status Register */
#define ALX_MII_ISR					0x13

#define ALX_MII_DBG_ADDR				0x1D
#define ALX_MII_DBG_DATA				0x1E

/***************************** debug port *************************************/

#define ALX_MIIDBG_ANACTRL				0x00
#define ALX_ANACTRL_DEF					0x02EF

#define ALX_MIIDBG_SYSMODCTRL				0x04
/* en half bias */
#define ALX_SYSMODCTRL_IECHOADJ_DEF			0xBB8B

#define ALX_MIIDBG_SRDSYSMOD				0x05
#define ALX_SRDSYSMOD_DEEMP_EN				0x0040
#define ALX_SRDSYSMOD_DEF				0x2C46

#define ALX_MIIDBG_HIBNEG				0x0B
#define ALX_HIBNEG_PSHIB_EN				0x8000
#define ALX_HIBNEG_HIB_PSE				0x1000
#define ALX_HIBNEG_DEF					0xBC40
#define ALX_HIBNEG_NOHIB	(ALX_HIBNEG_DEF & \
				 ~(ALX_HIBNEG_PSHIB_EN | ALX_HIBNEG_HIB_PSE))

#define ALX_MIIDBG_TST10BTCFG				0x12
#define ALX_TST10BTCFG_DEF				0x4C04

#define ALX_MIIDBG_AZ_ANADECT				0x15
#define ALX_AZ_ANADECT_DEF				0x3220
#define ALX_AZ_ANADECT_LONG				0x3210

#define ALX_MIIDBG_MSE16DB				0x18
#define ALX_MSE16DB_UP					0x05EA
#define ALX_MSE16DB_DOWN				0x02EA

#define ALX_MIIDBG_MSE20DB				0x1C
#define ALX_MSE20DB_TH_MASK				0x7F
#define ALX_MSE20DB_TH_SHIFT				2
#define ALX_MSE20DB_TH_DEF				0x2E
#define ALX_MSE20DB_TH_HI				0x54

#define ALX_MIIDBG_AGC					0x23
#define ALX_AGC_2_VGA_MASK				0x3FU
#define ALX_AGC_2_VGA_SHIFT				8
#define ALX_AGC_LONG1G_LIMT				40
#define ALX_AGC_LONG100M_LIMT				44

#define ALX_MIIDBG_LEGCYPS				0x29
#define ALX_LEGCYPS_EN					0x8000
#define ALX_LEGCYPS_DEF					0x129D

#define ALX_MIIDBG_TST100BTCFG				0x36
#define ALX_TST100BTCFG_DEF				0xE12C

#define ALX_MIIDBG_GREENCFG				0x3B
#define ALX_GREENCFG_DEF				0x7078

#define ALX_MIIDBG_GREENCFG2				0x3D
#define ALX_GREENCFG2_BP_GREEN				0x8000
#define ALX_GREENCFG2_GATE_DFSE_EN			0x0080

/******* dev 3 *********/
#define ALX_MIIEXT_PCS					3

#define ALX_MIIEXT_CLDCTRL3				0x8003
#define ALX_CLDCTRL3_BP_CABLE1TH_DET_GT			0x8000

#define ALX_MIIEXT_CLDCTRL5				0x8005
#define ALX_CLDCTRL5_BP_VD_HLFBIAS			0x4000

#define ALX_MIIEXT_CLDCTRL6				0x8006
#define ALX_CLDCTRL6_CAB_LEN_MASK			0xFF
#define ALX_CLDCTRL6_CAB_LEN_SHIFT			0
#define ALX_CLDCTRL6_CAB_LEN_SHORT1G			116
#define ALX_CLDCTRL6_CAB_LEN_SHORT100M			152

#define ALX_MIIEXT_VDRVBIAS				0x8062
#define ALX_VDRVBIAS_DEF				0x3

/********* dev 7 **********/
#define ALX_MIIEXT_ANEG					7

#define ALX_MIIEXT_LOCAL_EEEADV				0x3C
#define ALX_LOCAL_EEEADV_1000BT				0x0004
#define ALX_LOCAL_EEEADV_100BT				0x0002

#define ALX_MIIEXT_AFE					0x801A
#define ALX_AFE_10BT_100M_TH				0x0040

#define ALX_MIIEXT_S3DIG10				0x8023
/* bit0: 1:bypass 10BT rx fifo, 0:original 10BT rx */
#define ALX_MIIEXT_S3DIG10_SL				0x0001
#define ALX_MIIEXT_S3DIG10_DEF				0

#define ALX_MIIEXT_NLP78				0x8027
#define ALX_MIIEXT_NLP78_120M_DEF			0x8A05

#endif
