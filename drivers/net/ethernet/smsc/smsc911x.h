/***************************************************************************
 *
 * Copyright (C) 2004-2008 SMSC
 * Copyright (C) 2005-2008 ARM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 ***************************************************************************/
#ifndef __SMSC911X_H__
#define __SMSC911X_H__

/*Chip ID*/
#define LAN9115	0x01150000
#define LAN9116	0x01160000
#define LAN9117	0x01170000
#define LAN9118	0x01180000
#define LAN9215	0x115A0000
#define LAN9216	0x116A0000
#define LAN9217	0x117A0000
#define LAN9218	0x118A0000
#define LAN9210	0x92100000
#define LAN9211	0x92110000
#define LAN9220	0x92200000
#define LAN9221	0x92210000
#define LAN9250	0x92500000
#define LAN89218	0x218A0000

#define TX_FIFO_LOW_THRESHOLD	((u32)1600)
#define SMSC911X_EEPROM_SIZE	((u32)128)
#define USE_DEBUG		0

/* This is the maximum number of packets to be received every
 * NAPI poll */
#define SMSC_NAPI_WEIGHT	16

/* implements a PHY loopback test at initialisation time, to ensure a packet
 * can be successfully looped back */
#define USE_PHY_WORK_AROUND

#if USE_DEBUG >= 1
#define SMSC_WARN(pdata, nlevel, fmt, args...)			\
	netif_warn(pdata, nlevel, (pdata)->dev,			\
		   "%s: " fmt "\n", __func__, ##args)
#else
#define SMSC_WARN(pdata, nlevel, fmt, args...)			\
	no_printk(fmt "\n", ##args)
#endif

#if USE_DEBUG >= 2
#define SMSC_TRACE(pdata, nlevel, fmt, args...)			\
	netif_info(pdata, nlevel, pdata->dev, fmt "\n", ##args)
#else
#define SMSC_TRACE(pdata, nlevel, fmt, args...)			\
	no_printk(fmt "\n", ##args)
#endif

#ifdef CONFIG_DEBUG_SPINLOCK
#define SMSC_ASSERT_MAC_LOCK(pdata) \
		WARN_ON_SMP(!spin_is_locked(&pdata->mac_lock))
#else
#define SMSC_ASSERT_MAC_LOCK(pdata) do {} while (0)
#endif				/* CONFIG_DEBUG_SPINLOCK */

/* SMSC911x registers and bitfields */
#define RX_DATA_FIFO			0x00

#define TX_DATA_FIFO			0x20
#define TX_CMD_A_ON_COMP_		0x80000000
#define TX_CMD_A_BUF_END_ALGN_		0x03000000
#define TX_CMD_A_4_BYTE_ALGN_		0x00000000
#define TX_CMD_A_16_BYTE_ALGN_		0x01000000
#define TX_CMD_A_32_BYTE_ALGN_		0x02000000
#define TX_CMD_A_DATA_OFFSET_		0x001F0000
#define TX_CMD_A_FIRST_SEG_		0x00002000
#define TX_CMD_A_LAST_SEG_		0x00001000
#define TX_CMD_A_BUF_SIZE_		0x000007FF
#define TX_CMD_B_PKT_TAG_		0xFFFF0000
#define TX_CMD_B_ADD_CRC_DISABLE_	0x00002000
#define TX_CMD_B_DISABLE_PADDING_	0x00001000
#define TX_CMD_B_PKT_BYTE_LENGTH_	0x000007FF

#define RX_STATUS_FIFO			0x40
#define RX_STS_ES_			0x00008000
#define RX_STS_LENGTH_ERR_		0x00001000
#define RX_STS_MCAST_			0x00000400
#define RX_STS_FRAME_TYPE_		0x00000020
#define RX_STS_CRC_ERR_			0x00000002

#define RX_STATUS_FIFO_PEEK		0x44

#define TX_STATUS_FIFO			0x48
#define TX_STS_ES_			0x00008000
#define TX_STS_LOST_CARRIER_		0x00000800
#define TX_STS_NO_CARRIER_		0x00000400
#define TX_STS_LATE_COL_		0x00000200
#define TX_STS_EXCESS_COL_		0x00000100

#define TX_STATUS_FIFO_PEEK		0x4C

#define ID_REV				0x50
#define ID_REV_CHIP_ID_			0xFFFF0000
#define ID_REV_REV_ID_			0x0000FFFF

#define INT_CFG				0x54
#define INT_CFG_INT_DEAS_		0xFF000000
#define INT_CFG_INT_DEAS_CLR_		0x00004000
#define INT_CFG_INT_DEAS_STS_		0x00002000
#define INT_CFG_IRQ_INT_		0x00001000
#define INT_CFG_IRQ_EN_			0x00000100
#define INT_CFG_IRQ_POL_		0x00000010
#define INT_CFG_IRQ_TYPE_		0x00000001

#define INT_STS				0x58
#define INT_STS_SW_INT_			0x80000000
#define INT_STS_TXSTOP_INT_		0x02000000
#define INT_STS_RXSTOP_INT_		0x01000000
#define INT_STS_RXDFH_INT_		0x00800000
#define INT_STS_RXDF_INT_		0x00400000
#define INT_STS_TX_IOC_			0x00200000
#define INT_STS_RXD_INT_		0x00100000
#define INT_STS_GPT_INT_		0x00080000
#define INT_STS_PHY_INT_		0x00040000
#define INT_STS_PME_INT_		0x00020000
#define INT_STS_TXSO_			0x00010000
#define INT_STS_RWT_			0x00008000
#define INT_STS_RXE_			0x00004000
#define INT_STS_TXE_			0x00002000
#define INT_STS_TDFU_			0x00000800
#define INT_STS_TDFO_			0x00000400
#define INT_STS_TDFA_			0x00000200
#define INT_STS_TSFF_			0x00000100
#define INT_STS_TSFL_			0x00000080
#define INT_STS_RXDF_			0x00000040
#define INT_STS_RDFL_			0x00000020
#define INT_STS_RSFF_			0x00000010
#define INT_STS_RSFL_			0x00000008
#define INT_STS_GPIO2_INT_		0x00000004
#define INT_STS_GPIO1_INT_		0x00000002
#define INT_STS_GPIO0_INT_		0x00000001

#define INT_EN				0x5C
#define INT_EN_SW_INT_EN_		0x80000000
#define INT_EN_TXSTOP_INT_EN_		0x02000000
#define INT_EN_RXSTOP_INT_EN_		0x01000000
#define INT_EN_RXDFH_INT_EN_		0x00800000
#define INT_EN_TIOC_INT_EN_		0x00200000
#define INT_EN_RXD_INT_EN_		0x00100000
#define INT_EN_GPT_INT_EN_		0x00080000
#define INT_EN_PHY_INT_EN_		0x00040000
#define INT_EN_PME_INT_EN_		0x00020000
#define INT_EN_TXSO_EN_			0x00010000
#define INT_EN_RWT_EN_			0x00008000
#define INT_EN_RXE_EN_			0x00004000
#define INT_EN_TXE_EN_			0x00002000
#define INT_EN_TDFU_EN_			0x00000800
#define INT_EN_TDFO_EN_			0x00000400
#define INT_EN_TDFA_EN_			0x00000200
#define INT_EN_TSFF_EN_			0x00000100
#define INT_EN_TSFL_EN_			0x00000080
#define INT_EN_RXDF_EN_			0x00000040
#define INT_EN_RDFL_EN_			0x00000020
#define INT_EN_RSFF_EN_			0x00000010
#define INT_EN_RSFL_EN_			0x00000008
#define INT_EN_GPIO2_INT_		0x00000004
#define INT_EN_GPIO1_INT_		0x00000002
#define INT_EN_GPIO0_INT_		0x00000001

#define BYTE_TEST			0x64

#define FIFO_INT			0x68
#define FIFO_INT_TX_AVAIL_LEVEL_	0xFF000000
#define FIFO_INT_TX_STS_LEVEL_		0x00FF0000
#define FIFO_INT_RX_AVAIL_LEVEL_	0x0000FF00
#define FIFO_INT_RX_STS_LEVEL_		0x000000FF

#define RX_CFG				0x6C
#define RX_CFG_RX_END_ALGN_		0xC0000000
#define RX_CFG_RX_END_ALGN4_		0x00000000
#define RX_CFG_RX_END_ALGN16_		0x40000000
#define RX_CFG_RX_END_ALGN32_		0x80000000
#define RX_CFG_RX_DMA_CNT_		0x0FFF0000
#define RX_CFG_RX_DUMP_			0x00008000
#define RX_CFG_RXDOFF_			0x00001F00

#define TX_CFG				0x70
#define TX_CFG_TXS_DUMP_		0x00008000
#define TX_CFG_TXD_DUMP_		0x00004000
#define TX_CFG_TXSAO_			0x00000004
#define TX_CFG_TX_ON_			0x00000002
#define TX_CFG_STOP_TX_			0x00000001

#define HW_CFG				0x74
#define HW_CFG_TTM_			0x00200000
#define HW_CFG_SF_			0x00100000
#define HW_CFG_TX_FIF_SZ_		0x000F0000
#define HW_CFG_TR_			0x00003000
#define HW_CFG_SRST_			0x00000001

/* only available on 115/117 */
#define HW_CFG_PHY_CLK_SEL_		0x00000060
#define HW_CFG_PHY_CLK_SEL_INT_PHY_	0x00000000
#define HW_CFG_PHY_CLK_SEL_EXT_PHY_	0x00000020
#define HW_CFG_PHY_CLK_SEL_CLK_DIS_	0x00000040
#define HW_CFG_SMI_SEL_		 	0x00000010
#define HW_CFG_EXT_PHY_DET_		0x00000008
#define HW_CFG_EXT_PHY_EN_		0x00000004
#define HW_CFG_SRST_TO_			0x00000002

/* only available  on 116/118 */
#define HW_CFG_32_16_BIT_MODE_		0x00000004

#define RX_DP_CTRL			0x78
#define RX_DP_CTRL_RX_FFWD_		0x80000000

#define RX_FIFO_INF			0x7C
#define RX_FIFO_INF_RXSUSED_		0x00FF0000
#define RX_FIFO_INF_RXDUSED_		0x0000FFFF

#define TX_FIFO_INF			0x80
#define TX_FIFO_INF_TSUSED_		0x00FF0000
#define TX_FIFO_INF_TDFREE_		0x0000FFFF

#define PMT_CTRL			0x84
#define PMT_CTRL_PM_MODE_		0x00003000
#define PMT_CTRL_PM_MODE_D0_		0x00000000
#define PMT_CTRL_PM_MODE_D1_		0x00001000
#define PMT_CTRL_PM_MODE_D2_		0x00002000
#define PMT_CTRL_PM_MODE_D3_		0x00003000
#define PMT_CTRL_PHY_RST_		0x00000400
#define PMT_CTRL_WOL_EN_		0x00000200
#define PMT_CTRL_ED_EN_			0x00000100
#define PMT_CTRL_PME_TYPE_		0x00000040
#define PMT_CTRL_WUPS_			0x00000030
#define PMT_CTRL_WUPS_NOWAKE_		0x00000000
#define PMT_CTRL_WUPS_ED_		0x00000010
#define PMT_CTRL_WUPS_WOL_		0x00000020
#define PMT_CTRL_WUPS_MULTI_		0x00000030
#define PMT_CTRL_PME_IND_		0x00000008
#define PMT_CTRL_PME_POL_		0x00000004
#define PMT_CTRL_PME_EN_		0x00000002
#define PMT_CTRL_READY_			0x00000001

#define GPIO_CFG			0x88
#define GPIO_CFG_LED3_EN_		0x40000000
#define GPIO_CFG_LED2_EN_		0x20000000
#define GPIO_CFG_LED1_EN_		0x10000000
#define GPIO_CFG_GPIO2_INT_POL_		0x04000000
#define GPIO_CFG_GPIO1_INT_POL_		0x02000000
#define GPIO_CFG_GPIO0_INT_POL_		0x01000000
#define GPIO_CFG_EEPR_EN_		0x00700000
#define GPIO_CFG_GPIOBUF2_		0x00040000
#define GPIO_CFG_GPIOBUF1_		0x00020000
#define GPIO_CFG_GPIOBUF0_		0x00010000
#define GPIO_CFG_GPIODIR2_		0x00000400
#define GPIO_CFG_GPIODIR1_		0x00000200
#define GPIO_CFG_GPIODIR0_		0x00000100
#define GPIO_CFG_GPIOD4_		0x00000020
#define GPIO_CFG_GPIOD3_		0x00000010
#define GPIO_CFG_GPIOD2_		0x00000004
#define GPIO_CFG_GPIOD1_		0x00000002
#define GPIO_CFG_GPIOD0_		0x00000001

#define GPT_CFG				0x8C
#define GPT_CFG_TIMER_EN_		0x20000000
#define GPT_CFG_GPT_LOAD_		0x0000FFFF

#define GPT_CNT				0x90
#define GPT_CNT_GPT_CNT_		0x0000FFFF

#define WORD_SWAP			0x98

#define FREE_RUN			0x9C

#define RX_DROP				0xA0

#define MAC_CSR_CMD			0xA4
#define MAC_CSR_CMD_CSR_BUSY_		0x80000000
#define MAC_CSR_CMD_R_NOT_W_		0x40000000
#define MAC_CSR_CMD_CSR_ADDR_		0x000000FF

#define MAC_CSR_DATA			0xA8

#define AFC_CFG				0xAC
#define AFC_CFG_AFC_HI_			0x00FF0000
#define AFC_CFG_AFC_LO_			0x0000FF00
#define AFC_CFG_BACK_DUR_		0x000000F0
#define AFC_CFG_FCMULT_			0x00000008
#define AFC_CFG_FCBRD_			0x00000004
#define AFC_CFG_FCADD_			0x00000002
#define AFC_CFG_FCANY_			0x00000001

#define E2P_CMD				0xB0
#define E2P_CMD_EPC_BUSY_		0x80000000
#define E2P_CMD_EPC_CMD_		0x70000000
#define E2P_CMD_EPC_CMD_READ_		0x00000000
#define E2P_CMD_EPC_CMD_EWDS_		0x10000000
#define E2P_CMD_EPC_CMD_EWEN_		0x20000000
#define E2P_CMD_EPC_CMD_WRITE_		0x30000000
#define E2P_CMD_EPC_CMD_WRAL_		0x40000000
#define E2P_CMD_EPC_CMD_ERASE_		0x50000000
#define E2P_CMD_EPC_CMD_ERAL_		0x60000000
#define E2P_CMD_EPC_CMD_RELOAD_		0x70000000
#define E2P_CMD_EPC_TIMEOUT_		0x00000200
#define E2P_CMD_MAC_ADDR_LOADED_	0x00000100
#define E2P_CMD_EPC_ADDR_		0x000000FF

#define E2P_DATA			0xB4
#define E2P_DATA_EEPROM_DATA_		0x000000FF
#define LAN_REGISTER_EXTENT		0x00000100

#define RESET_CTL			0x1F8
#define RESET_CTL_DIGITAL_RST_		0x00000001

/*
 * MAC Control and Status Register (Indirect Address)
 * Offset (through the MAC_CSR CMD and DATA port)
 */
#define MAC_CR				0x01
#define MAC_CR_RXALL_			0x80000000
#define MAC_CR_HBDIS_			0x10000000
#define MAC_CR_RCVOWN_			0x00800000
#define MAC_CR_LOOPBK_			0x00200000
#define MAC_CR_FDPX_			0x00100000
#define MAC_CR_MCPAS_			0x00080000
#define MAC_CR_PRMS_			0x00040000
#define MAC_CR_INVFILT_			0x00020000
#define MAC_CR_PASSBAD_			0x00010000
#define MAC_CR_HFILT_			0x00008000
#define MAC_CR_HPFILT_			0x00002000
#define MAC_CR_LCOLL_			0x00001000
#define MAC_CR_BCAST_			0x00000800
#define MAC_CR_DISRTY_			0x00000400
#define MAC_CR_PADSTR_			0x00000100
#define MAC_CR_BOLMT_MASK_		0x000000C0
#define MAC_CR_DFCHK_			0x00000020
#define MAC_CR_TXEN_			0x00000008
#define MAC_CR_RXEN_			0x00000004

#define ADDRH				0x02

#define ADDRL				0x03

#define HASHH				0x04

#define HASHL				0x05

#define MII_ACC				0x06
#define MII_ACC_PHY_ADDR_		0x0000F800
#define MII_ACC_MIIRINDA_		0x000007C0
#define MII_ACC_MII_WRITE_		0x00000002
#define MII_ACC_MII_BUSY_		0x00000001

#define MII_DATA			0x07

#define FLOW				0x08
#define FLOW_FCPT_			0xFFFF0000
#define FLOW_FCPASS_			0x00000004
#define FLOW_FCEN_			0x00000002
#define FLOW_FCBSY_			0x00000001

#define VLAN1				0x09

#define VLAN2				0x0A

#define WUFF				0x0B

#define WUCSR				0x0C
#define WUCSR_GUE_			0x00000200
#define WUCSR_WUFR_			0x00000040
#define WUCSR_MPR_			0x00000020
#define WUCSR_WAKE_EN_			0x00000004
#define WUCSR_MPEN_			0x00000002

/*
 * Phy definitions (vendor-specific)
 */
#define LAN9118_PHY_ID			0x00C0001C

#define MII_INTSTS			0x1D

#define MII_INTMSK			0x1E
#define PHY_INTMSK_AN_RCV_		(1 << 1)
#define PHY_INTMSK_PDFAULT_		(1 << 2)
#define PHY_INTMSK_AN_ACK_		(1 << 3)
#define PHY_INTMSK_LNKDOWN_		(1 << 4)
#define PHY_INTMSK_RFAULT_		(1 << 5)
#define PHY_INTMSK_AN_COMP_		(1 << 6)
#define PHY_INTMSK_ENERGYON_		(1 << 7)
#define PHY_INTMSK_DEFAULT_		(PHY_INTMSK_ENERGYON_ | \
					 PHY_INTMSK_AN_COMP_ | \
					 PHY_INTMSK_RFAULT_ | \
					 PHY_INTMSK_LNKDOWN_)

#define ADVERTISE_PAUSE_ALL		(ADVERTISE_PAUSE_CAP | \
					 ADVERTISE_PAUSE_ASYM)

#define LPA_PAUSE_ALL			(LPA_PAUSE_CAP | \
					 LPA_PAUSE_ASYM)

/*
 * Provide hooks to let the arch add to the initialisation procedure
 * and to override the source of the MAC address.
 */
#define SMSC_INITIALIZE()		do {} while (0)
#define smsc_get_mac(dev)		smsc911x_read_mac_address((dev))

#ifdef CONFIG_SMSC911X_ARCH_HOOKS
#include <asm/smsc911x.h>
#endif

#include <linux/smscphy.h>

#endif				/* __SMSC911X_H__ */
