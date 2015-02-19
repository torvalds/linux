 /***************************************************************************
 *
 * Copyright (C) 2007,2008  SMSC
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
 ***************************************************************************
 */

#ifndef _SMSC9420_H
#define _SMSC9420_H

#define TX_RING_SIZE			(32)
#define RX_RING_SIZE			(128)

/* interrupt deassertion in multiples of 10us */
#define INT_DEAS_TIME			(50)

#define NAPI_WEIGHT			(64)
#define SMSC_BAR			(3)

#ifdef __BIG_ENDIAN
/* Register set is duplicated for BE at an offset of 0x200 */
#define LAN9420_CPSR_ENDIAN_OFFSET	(0x200)
#else
#define LAN9420_CPSR_ENDIAN_OFFSET	(0)
#endif

#define PCI_VENDOR_ID_9420		(0x1055)
#define PCI_DEVICE_ID_9420		(0xE420)

#define LAN_REGISTER_EXTENT		(0x400)

#define SMSC9420_EEPROM_SIZE		((u32)11)
#define SMSC9420_EEPROM_MAGIC		(0x9420)

#define PKT_BUF_SZ			(VLAN_ETH_FRAME_LEN + NET_IP_ALIGN + 4)

/***********************************************/
/* DMA Controller Control and Status Registers */
/***********************************************/
#define BUS_MODE			(0x00)
#define BUS_MODE_SWR_			(BIT(0))
#define BUS_MODE_DMA_BURST_LENGTH_1	(BIT(8))
#define BUS_MODE_DMA_BURST_LENGTH_2	(BIT(9))
#define BUS_MODE_DMA_BURST_LENGTH_4	(BIT(10))
#define BUS_MODE_DMA_BURST_LENGTH_8	(BIT(11))
#define BUS_MODE_DMA_BURST_LENGTH_16	(BIT(12))
#define BUS_MODE_DMA_BURST_LENGTH_32	(BIT(13))
#define BUS_MODE_DBO_			(BIT(20))

#define TX_POLL_DEMAND			(0x04)

#define RX_POLL_DEMAND			(0x08)

#define RX_BASE_ADDR			(0x0C)

#define TX_BASE_ADDR			(0x10)

#define DMAC_STATUS			(0x14)
#define DMAC_STS_TS_			(7 << 20)
#define DMAC_STS_RS_ 			(7 << 17)
#define DMAC_STS_NIS_			(BIT(16))
#define DMAC_STS_AIS_			(BIT(15))
#define DMAC_STS_RWT_			(BIT(9))
#define DMAC_STS_RXPS_			(BIT(8))
#define DMAC_STS_RXBU_			(BIT(7))
#define DMAC_STS_RX_			(BIT(6))
#define DMAC_STS_TXUNF_			(BIT(5))
#define DMAC_STS_TXBU_			(BIT(2))
#define DMAC_STS_TXPS_			(BIT(1))
#define DMAC_STS_TX_			(BIT(0))

#define DMAC_CONTROL			(0x18)
#define DMAC_CONTROL_TTM_		(BIT(22))
#define DMAC_CONTROL_SF_		(BIT(21))
#define DMAC_CONTROL_ST_		(BIT(13))
#define DMAC_CONTROL_OSF_		(BIT(2))
#define DMAC_CONTROL_SR_		(BIT(1))

#define DMAC_INTR_ENA			(0x1C)
#define DMAC_INTR_ENA_NIS_		(BIT(16))
#define DMAC_INTR_ENA_AIS_		(BIT(15))
#define DMAC_INTR_ENA_RWT_		(BIT(9))
#define DMAC_INTR_ENA_RXPS_		(BIT(8))
#define DMAC_INTR_ENA_RXBU_		(BIT(7))
#define DMAC_INTR_ENA_RX_		(BIT(6))
#define DMAC_INTR_ENA_TXBU_		(BIT(2))
#define DMAC_INTR_ENA_TXPS_		(BIT(1))
#define DMAC_INTR_ENA_TX_		(BIT(0))

#define MISS_FRAME_CNTR			(0x20)

#define TX_BUFF_ADDR			(0x50)

#define RX_BUFF_ADDR			(0x54)

/* Transmit Descriptor Bit Defs */
#define TDES0_OWN_			(0x80000000)
#define TDES0_ERROR_SUMMARY_		(0x00008000)
#define TDES0_LOSS_OF_CARRIER_		(0x00000800)
#define TDES0_NO_CARRIER_		(0x00000400)
#define TDES0_LATE_COLLISION_		(0x00000200)
#define TDES0_EXCESSIVE_COLLISIONS_	(0x00000100)
#define TDES0_HEARTBEAT_FAIL_		(0x00000080)
#define TDES0_COLLISION_COUNT_MASK_	(0x00000078)
#define TDES0_COLLISION_COUNT_SHFT_	(3)
#define TDES0_EXCESSIVE_DEFERRAL_	(0x00000004)
#define TDES0_DEFERRED_			(0x00000001)

#define TDES1_IC_			0x80000000
#define TDES1_LS_			0x40000000
#define TDES1_FS_			0x20000000
#define TDES1_TXCSEN_			0x08000000
#define TDES1_TER_			(BIT(25))
#define TDES1_TCH_			0x01000000

/* Receive Descriptor 0 Bit Defs */
#define RDES0_OWN_			(0x80000000)
#define RDES0_FRAME_LENGTH_MASK_	(0x07FF0000)
#define RDES0_FRAME_LENGTH_SHFT_	(16)
#define RDES0_ERROR_SUMMARY_		(0x00008000)
#define RDES0_DESCRIPTOR_ERROR_		(0x00004000)
#define RDES0_LENGTH_ERROR_		(0x00001000)
#define RDES0_RUNT_FRAME_		(0x00000800)
#define RDES0_MULTICAST_FRAME_		(0x00000400)
#define RDES0_FIRST_DESCRIPTOR_		(0x00000200)
#define RDES0_LAST_DESCRIPTOR_		(0x00000100)
#define RDES0_FRAME_TOO_LONG_		(0x00000080)
#define RDES0_COLLISION_SEEN_		(0x00000040)
#define RDES0_FRAME_TYPE_		(0x00000020)
#define RDES0_WATCHDOG_TIMEOUT_		(0x00000010)
#define RDES0_MII_ERROR_		(0x00000008)
#define RDES0_DRIBBLING_BIT_		(0x00000004)
#define RDES0_CRC_ERROR_		(0x00000002)

/* Receive Descriptor 1 Bit Defs */
#define RDES1_RER_			(0x02000000)

/***********************************************/
/*       MAC Control and Status Registers      */
/***********************************************/
#define MAC_CR				(0x80)
#define MAC_CR_RXALL_			(0x80000000)
#define MAC_CR_DIS_RXOWN_		(0x00800000)
#define MAC_CR_LOOPBK_			(0x00200000)
#define MAC_CR_FDPX_			(0x00100000)
#define MAC_CR_MCPAS_			(0x00080000)
#define MAC_CR_PRMS_			(0x00040000)
#define MAC_CR_INVFILT_			(0x00020000)
#define MAC_CR_PASSBAD_			(0x00010000)
#define MAC_CR_HFILT_			(0x00008000)
#define MAC_CR_HPFILT_			(0x00002000)
#define MAC_CR_LCOLL_			(0x00001000)
#define MAC_CR_DIS_BCAST_		(0x00000800)
#define MAC_CR_DIS_RTRY_		(0x00000400)
#define MAC_CR_PADSTR_			(0x00000100)
#define MAC_CR_BOLMT_MSK		(0x000000C0)
#define MAC_CR_MFCHK_			(0x00000020)
#define MAC_CR_TXEN_			(0x00000008)
#define MAC_CR_RXEN_			(0x00000004)

#define ADDRH				(0x84)

#define ADDRL				(0x88)

#define HASHH				(0x8C)

#define HASHL				(0x90)

#define MII_ACCESS			(0x94)
#define MII_ACCESS_MII_BUSY_		(0x00000001)
#define MII_ACCESS_MII_WRITE_		(0x00000002)
#define MII_ACCESS_MII_READ_		(0x00000000)
#define MII_ACCESS_INDX_MSK_		(0x000007C0)
#define MII_ACCESS_PHYADDR_MSK_		(0x0000F8C0)
#define MII_ACCESS_INDX_SHFT_CNT	(6)
#define MII_ACCESS_PHYADDR_SHFT_CNT	(11)

#define MII_DATA			(0x98)

#define FLOW				(0x9C)

#define VLAN1				(0xA0)

#define VLAN2				(0xA4)

#define WUFF				(0xA8)

#define WUCSR				(0xAC)

#define COE_CR				(0xB0)
#define TX_COE_EN			(0x00010000)
#define RX_COE_MODE			(0x00000002)
#define RX_COE_EN			(0x00000001)

/***********************************************/
/*     System Control and Status Registers     */
/***********************************************/
#define ID_REV				(0xC0)

#define INT_CTL				(0xC4)
#define INT_CTL_SW_INT_EN_		(0x00008000)
#define INT_CTL_SBERR_INT_EN_		(1 << 12)
#define INT_CTL_MBERR_INT_EN_		(1 << 13)
#define INT_CTL_GPT_INT_EN_		(0x00000008)
#define INT_CTL_PHY_INT_EN_		(0x00000004)
#define INT_CTL_WAKE_INT_EN_		(0x00000002)

#define INT_STAT			(0xC8)
#define INT_STAT_SW_INT_		(1 << 15)
#define INT_STAT_MBERR_INT_		(1 << 13)
#define INT_STAT_SBERR_INT_		(1 << 12)
#define INT_STAT_GPT_INT_		(1 << 3)
#define INT_STAT_PHY_INT_		(0x00000004)
#define INT_STAT_WAKE_INT_		(0x00000002)
#define INT_STAT_DMAC_INT_		(0x00000001)

#define INT_CFG				(0xCC)
#define INT_CFG_IRQ_INT_		(0x00080000)
#define INT_CFG_IRQ_EN_			(0x00040000)
#define INT_CFG_INT_DEAS_CLR_		(0x00000200)
#define INT_CFG_INT_DEAS_MASK		(0x000000FF)

#define GPIO_CFG			(0xD0)
#define GPIO_CFG_LED_3_			(0x40000000)
#define GPIO_CFG_LED_2_			(0x20000000)
#define GPIO_CFG_LED_1_			(0x10000000)
#define GPIO_CFG_EEPR_EN_		(0x00700000)

#define GPT_CFG				(0xD4)
#define GPT_CFG_TIMER_EN_		(0x20000000)

#define GPT_CNT				(0xD8)

#define BUS_CFG				(0xDC)
#define BUS_CFG_RXTXWEIGHT_1_1		(0 << 25)
#define BUS_CFG_RXTXWEIGHT_2_1		(1 << 25)
#define BUS_CFG_RXTXWEIGHT_3_1		(2 << 25)
#define BUS_CFG_RXTXWEIGHT_4_1		(3 << 25)

#define PMT_CTRL			(0xE0)

#define FREE_RUN			(0xF4)

#define E2P_CMD				(0xF8)
#define E2P_CMD_EPC_BUSY_		(0x80000000)
#define E2P_CMD_EPC_CMD_		(0x70000000)
#define E2P_CMD_EPC_CMD_READ_		(0x00000000)
#define E2P_CMD_EPC_CMD_EWDS_		(0x10000000)
#define E2P_CMD_EPC_CMD_EWEN_		(0x20000000)
#define E2P_CMD_EPC_CMD_WRITE_		(0x30000000)
#define E2P_CMD_EPC_CMD_WRAL_		(0x40000000)
#define E2P_CMD_EPC_CMD_ERASE_		(0x50000000)
#define E2P_CMD_EPC_CMD_ERAL_		(0x60000000)
#define E2P_CMD_EPC_CMD_RELOAD_		(0x70000000)
#define E2P_CMD_EPC_TIMEOUT_		(0x00000200)
#define E2P_CMD_MAC_ADDR_LOADED_	(0x00000100)
#define E2P_CMD_EPC_ADDR_		(0x000000FF)

#define E2P_DATA			(0xFC)
#define E2P_DATA_EEPROM_DATA_		(0x000000FF)

#endif /* _SMSC9420_H */
