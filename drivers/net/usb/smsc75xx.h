 /***************************************************************************
 *
 * Copyright (C) 2007-2010 SMSC
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
 *****************************************************************************/

#ifndef _SMSC75XX_H
#define _SMSC75XX_H

/* Tx command words */
#define TX_CMD_A_LSO			(0x08000000)
#define TX_CMD_A_IPE			(0x04000000)
#define TX_CMD_A_TPE			(0x02000000)
#define TX_CMD_A_IVTG			(0x01000000)
#define TX_CMD_A_RVTG			(0x00800000)
#define TX_CMD_A_FCS			(0x00400000)
#define TX_CMD_A_LEN			(0x000FFFFF)

#define TX_CMD_B_MSS			(0x3FFF0000)
#define TX_CMD_B_MSS_SHIFT		(16)
#define TX_MSS_MIN			((u16)8)
#define TX_CMD_B_VTAG			(0x0000FFFF)

/* Rx command words */
#define RX_CMD_A_ICE			(0x80000000)
#define RX_CMD_A_TCE			(0x40000000)
#define RX_CMD_A_IPV			(0x20000000)
#define RX_CMD_A_PID			(0x18000000)
#define RX_CMD_A_PID_NIP		(0x00000000)
#define RX_CMD_A_PID_TCP		(0x08000000)
#define RX_CMD_A_PID_UDP		(0x10000000)
#define RX_CMD_A_PID_PP			(0x18000000)
#define RX_CMD_A_PFF			(0x04000000)
#define RX_CMD_A_BAM			(0x02000000)
#define RX_CMD_A_MAM			(0x01000000)
#define RX_CMD_A_FVTG			(0x00800000)
#define RX_CMD_A_RED			(0x00400000)
#define RX_CMD_A_RWT			(0x00200000)
#define RX_CMD_A_RUNT			(0x00100000)
#define RX_CMD_A_LONG			(0x00080000)
#define RX_CMD_A_RXE			(0x00040000)
#define RX_CMD_A_DRB			(0x00020000)
#define RX_CMD_A_FCS			(0x00010000)
#define RX_CMD_A_UAM			(0x00008000)
#define RX_CMD_A_LCSM			(0x00004000)
#define RX_CMD_A_LEN			(0x00003FFF)

#define RX_CMD_B_CSUM			(0xFFFF0000)
#define RX_CMD_B_CSUM_SHIFT		(16)
#define RX_CMD_B_VTAG			(0x0000FFFF)

/* SCSRs */
#define ID_REV				(0x0000)

#define FPGA_REV			(0x0004)

#define BOND_CTL			(0x0008)

#define INT_STS				(0x000C)
#define INT_STS_RDFO_INT		(0x00400000)
#define INT_STS_TXE_INT			(0x00200000)
#define INT_STS_MACRTO_INT		(0x00100000)
#define INT_STS_TX_DIS_INT		(0x00080000)
#define INT_STS_RX_DIS_INT		(0x00040000)
#define INT_STS_PHY_INT_		(0x00020000)
#define INT_STS_MAC_ERR_INT		(0x00008000)
#define INT_STS_TDFU			(0x00004000)
#define INT_STS_TDFO			(0x00002000)
#define INT_STS_GPIOS			(0x00000FFF)
#define INT_STS_CLEAR_ALL		(0xFFFFFFFF)

#define HW_CFG				(0x0010)
#define HW_CFG_SMDET_STS		(0x00008000)
#define HW_CFG_SMDET_EN			(0x00004000)
#define HW_CFG_EEM			(0x00002000)
#define HW_CFG_RST_PROTECT		(0x00001000)
#define HW_CFG_PORT_SWAP		(0x00000800)
#define HW_CFG_PHY_BOOST		(0x00000600)
#define HW_CFG_PHY_BOOST_NORMAL		(0x00000000)
#define HW_CFG_PHY_BOOST_4		(0x00002000)
#define HW_CFG_PHY_BOOST_8		(0x00004000)
#define HW_CFG_PHY_BOOST_12		(0x00006000)
#define HW_CFG_LEDB			(0x00000100)
#define HW_CFG_BIR			(0x00000080)
#define HW_CFG_SBP			(0x00000040)
#define HW_CFG_IME			(0x00000020)
#define HW_CFG_MEF			(0x00000010)
#define HW_CFG_ETC			(0x00000008)
#define HW_CFG_BCE			(0x00000004)
#define HW_CFG_LRST			(0x00000002)
#define HW_CFG_SRST			(0x00000001)

#define PMT_CTL				(0x0014)
#define PMT_CTL_PHY_PWRUP		(0x00000400)
#define PMT_CTL_RES_CLR_WKP_EN		(0x00000100)
#define PMT_CTL_DEV_RDY			(0x00000080)
#define PMT_CTL_SUS_MODE		(0x00000060)
#define PMT_CTL_SUS_MODE_0		(0x00000000)
#define PMT_CTL_SUS_MODE_1		(0x00000020)
#define PMT_CTL_SUS_MODE_2		(0x00000040)
#define PMT_CTL_SUS_MODE_3		(0x00000060)
#define PMT_CTL_PHY_RST			(0x00000010)
#define PMT_CTL_WOL_EN			(0x00000008)
#define PMT_CTL_ED_EN			(0x00000004)
#define PMT_CTL_WUPS			(0x00000003)
#define PMT_CTL_WUPS_NO			(0x00000000)
#define PMT_CTL_WUPS_ED			(0x00000001)
#define PMT_CTL_WUPS_WOL		(0x00000002)
#define PMT_CTL_WUPS_MULTI		(0x00000003)

#define LED_GPIO_CFG			(0x0018)
#define LED_GPIO_CFG_LED2_FUN_SEL	(0x80000000)
#define LED_GPIO_CFG_LED10_FUN_SEL	(0x40000000)
#define LED_GPIO_CFG_LEDGPIO_EN		(0x0000F000)
#define LED_GPIO_CFG_LEDGPIO_EN_0	(0x00001000)
#define LED_GPIO_CFG_LEDGPIO_EN_1	(0x00002000)
#define LED_GPIO_CFG_LEDGPIO_EN_2	(0x00004000)
#define LED_GPIO_CFG_LEDGPIO_EN_3	(0x00008000)
#define LED_GPIO_CFG_GPBUF		(0x00000F00)
#define LED_GPIO_CFG_GPBUF_0		(0x00000100)
#define LED_GPIO_CFG_GPBUF_1		(0x00000200)
#define LED_GPIO_CFG_GPBUF_2		(0x00000400)
#define LED_GPIO_CFG_GPBUF_3		(0x00000800)
#define LED_GPIO_CFG_GPDIR		(0x000000F0)
#define LED_GPIO_CFG_GPDIR_0		(0x00000010)
#define LED_GPIO_CFG_GPDIR_1		(0x00000020)
#define LED_GPIO_CFG_GPDIR_2		(0x00000040)
#define LED_GPIO_CFG_GPDIR_3		(0x00000080)
#define LED_GPIO_CFG_GPDATA		(0x0000000F)
#define LED_GPIO_CFG_GPDATA_0		(0x00000001)
#define LED_GPIO_CFG_GPDATA_1		(0x00000002)
#define LED_GPIO_CFG_GPDATA_2		(0x00000004)
#define LED_GPIO_CFG_GPDATA_3		(0x00000008)

#define GPIO_CFG			(0x001C)
#define GPIO_CFG_SHIFT			(24)
#define GPIO_CFG_GPEN			(0xFF000000)
#define GPIO_CFG_GPBUF			(0x00FF0000)
#define GPIO_CFG_GPDIR			(0x0000FF00)
#define GPIO_CFG_GPDATA			(0x000000FF)

#define GPIO_WAKE			(0x0020)
#define GPIO_WAKE_PHY_LINKUP_EN		(0x80000000)
#define GPIO_WAKE_POL			(0x0FFF0000)
#define GPIO_WAKE_POL_SHIFT		(16)
#define GPIO_WAKE_WK			(0x00000FFF)

#define DP_SEL				(0x0024)
#define DP_SEL_DPRDY			(0x80000000)
#define DP_SEL_RSEL			(0x0000000F)
#define DP_SEL_URX			(0x00000000)
#define DP_SEL_VHF			(0x00000001)
#define DP_SEL_VHF_HASH_LEN		(16)
#define DP_SEL_VHF_VLAN_LEN		(128)
#define DP_SEL_LSO_HEAD			(0x00000002)
#define DP_SEL_FCT_RX			(0x00000003)
#define DP_SEL_FCT_TX			(0x00000004)
#define DP_SEL_DESCRIPTOR		(0x00000005)
#define DP_SEL_WOL			(0x00000006)

#define DP_CMD				(0x0028)
#define DP_CMD_WRITE			(0x01)
#define DP_CMD_READ			(0x00)

#define DP_ADDR				(0x002C)

#define DP_DATA				(0x0030)

#define BURST_CAP			(0x0034)
#define BURST_CAP_MASK			(0x0000000F)

#define INT_EP_CTL			(0x0038)
#define INT_EP_CTL_INTEP_ON		(0x80000000)
#define INT_EP_CTL_RDFO_EN		(0x00400000)
#define INT_EP_CTL_TXE_EN		(0x00200000)
#define INT_EP_CTL_MACROTO_EN		(0x00100000)
#define INT_EP_CTL_TX_DIS_EN		(0x00080000)
#define INT_EP_CTL_RX_DIS_EN		(0x00040000)
#define INT_EP_CTL_PHY_EN_		(0x00020000)
#define INT_EP_CTL_MAC_ERR_EN		(0x00008000)
#define INT_EP_CTL_TDFU_EN		(0x00004000)
#define INT_EP_CTL_TDFO_EN		(0x00002000)
#define INT_EP_CTL_RX_FIFO_EN		(0x00001000)
#define INT_EP_CTL_GPIOX_EN		(0x00000FFF)

#define BULK_IN_DLY			(0x003C)
#define BULK_IN_DLY_MASK		(0xFFFF)

#define E2P_CMD				(0x0040)
#define E2P_CMD_BUSY			(0x80000000)
#define E2P_CMD_MASK			(0x70000000)
#define E2P_CMD_READ			(0x00000000)
#define E2P_CMD_EWDS			(0x10000000)
#define E2P_CMD_EWEN			(0x20000000)
#define E2P_CMD_WRITE			(0x30000000)
#define E2P_CMD_WRAL			(0x40000000)
#define E2P_CMD_ERASE			(0x50000000)
#define E2P_CMD_ERAL			(0x60000000)
#define E2P_CMD_RELOAD			(0x70000000)
#define E2P_CMD_TIMEOUT			(0x00000400)
#define E2P_CMD_LOADED			(0x00000200)
#define E2P_CMD_ADDR			(0x000001FF)

#define MAX_EEPROM_SIZE			(512)

#define E2P_DATA			(0x0044)
#define E2P_DATA_MASK_			(0x000000FF)

#define RFE_CTL				(0x0060)
#define RFE_CTL_TCPUDP_CKM		(0x00001000)
#define RFE_CTL_IP_CKM			(0x00000800)
#define RFE_CTL_AB			(0x00000400)
#define RFE_CTL_AM			(0x00000200)
#define RFE_CTL_AU			(0x00000100)
#define RFE_CTL_VS			(0x00000080)
#define RFE_CTL_UF			(0x00000040)
#define RFE_CTL_VF			(0x00000020)
#define RFE_CTL_SPF			(0x00000010)
#define RFE_CTL_MHF			(0x00000008)
#define RFE_CTL_DHF			(0x00000004)
#define RFE_CTL_DPF			(0x00000002)
#define RFE_CTL_RST_RF			(0x00000001)

#define VLAN_TYPE			(0x0064)
#define VLAN_TYPE_MASK			(0x0000FFFF)

#define FCT_RX_CTL			(0x0090)
#define FCT_RX_CTL_EN			(0x80000000)
#define FCT_RX_CTL_RST			(0x40000000)
#define FCT_RX_CTL_SBF			(0x02000000)
#define FCT_RX_CTL_OVERFLOW		(0x01000000)
#define FCT_RX_CTL_FRM_DROP		(0x00800000)
#define FCT_RX_CTL_RX_NOT_EMPTY		(0x00400000)
#define FCT_RX_CTL_RX_EMPTY		(0x00200000)
#define FCT_RX_CTL_RX_DISABLED		(0x00100000)
#define FCT_RX_CTL_RXUSED		(0x0000FFFF)

#define FCT_TX_CTL			(0x0094)
#define FCT_TX_CTL_EN			(0x80000000)
#define FCT_TX_CTL_RST			(0x40000000)
#define FCT_TX_CTL_TX_NOT_EMPTY		(0x00400000)
#define FCT_TX_CTL_TX_EMPTY		(0x00200000)
#define FCT_TX_CTL_TX_DISABLED		(0x00100000)
#define FCT_TX_CTL_TXUSED		(0x0000FFFF)

#define FCT_RX_FIFO_END			(0x0098)
#define FCT_RX_FIFO_END_MASK		(0x0000007F)

#define FCT_TX_FIFO_END			(0x009C)
#define FCT_TX_FIFO_END_MASK		(0x0000003F)

#define FCT_FLOW			(0x00A0)
#define FCT_FLOW_THRESHOLD_OFF		(0x00007F00)
#define FCT_FLOW_THRESHOLD_OFF_SHIFT	(8)
#define FCT_FLOW_THRESHOLD_ON		(0x0000007F)

/* MAC CSRs */
#define MAC_CR				(0x100)
#define MAC_CR_ADP			(0x00002000)
#define MAC_CR_ADD			(0x00001000)
#define MAC_CR_ASD			(0x00000800)
#define MAC_CR_INT_LOOP			(0x00000400)
#define MAC_CR_BOLMT			(0x000000C0)
#define MAC_CR_FDPX			(0x00000008)
#define MAC_CR_CFG			(0x00000006)
#define MAC_CR_CFG_10			(0x00000000)
#define MAC_CR_CFG_100			(0x00000002)
#define MAC_CR_CFG_1000			(0x00000004)
#define MAC_CR_RST			(0x00000001)

#define MAC_RX				(0x104)
#define MAC_RX_MAX_SIZE			(0x3FFF0000)
#define MAC_RX_MAX_SIZE_SHIFT		(16)
#define MAC_RX_FCS_STRIP		(0x00000010)
#define MAC_RX_FSE			(0x00000004)
#define MAC_RX_RXD			(0x00000002)
#define MAC_RX_RXEN			(0x00000001)

#define MAC_TX				(0x108)
#define MAC_TX_BFCS			(0x00000004)
#define MAC_TX_TXD			(0x00000002)
#define MAC_TX_TXEN			(0x00000001)

#define FLOW				(0x10C)
#define FLOW_FORCE_FC			(0x80000000)
#define FLOW_TX_FCEN			(0x40000000)
#define FLOW_RX_FCEN			(0x20000000)
#define FLOW_FPF			(0x10000000)
#define FLOW_PAUSE_TIME			(0x0000FFFF)

#define RAND_SEED			(0x110)
#define RAND_SEED_MASK			(0x0000FFFF)

#define ERR_STS				(0x114)
#define ERR_STS_FCS_ERR			(0x00000100)
#define ERR_STS_LFRM_ERR		(0x00000080)
#define ERR_STS_RUNT_ERR		(0x00000040)
#define ERR_STS_COLLISION_ERR		(0x00000010)
#define ERR_STS_ALIGN_ERR		(0x00000008)
#define ERR_STS_URUN_ERR		(0x00000004)

#define RX_ADDRH			(0x118)
#define RX_ADDRH_MASK			(0x0000FFFF)

#define RX_ADDRL			(0x11C)

#define MII_ACCESS			(0x120)
#define MII_ACCESS_PHY_ADDR		(0x0000F800)
#define MII_ACCESS_PHY_ADDR_SHIFT	(11)
#define MII_ACCESS_REG_ADDR		(0x000007C0)
#define MII_ACCESS_REG_ADDR_SHIFT	(6)
#define MII_ACCESS_READ			(0x00000000)
#define MII_ACCESS_WRITE		(0x00000002)
#define MII_ACCESS_BUSY			(0x00000001)

#define MII_DATA			(0x124)
#define MII_DATA_MASK			(0x0000FFFF)

#define WUCSR				(0x140)
#define WUCSR_PFDA_FR			(0x00000080)
#define WUCSR_WUFR			(0x00000040)
#define WUCSR_MPR			(0x00000020)
#define WUCSR_BCAST_FR			(0x00000010)
#define WUCSR_PFDA_EN			(0x00000008)
#define WUCSR_WUEN			(0x00000004)
#define WUCSR_MPEN			(0x00000002)
#define WUCSR_BCST_EN			(0x00000001)

#define WUF_CFGX			(0x144)
#define WUF_CFGX_EN			(0x80000000)
#define WUF_CFGX_ATYPE			(0x03000000)
#define WUF_CFGX_ATYPE_UNICAST		(0x00000000)
#define WUF_CFGX_ATYPE_MULTICAST	(0x02000000)
#define WUF_CFGX_ATYPE_ALL		(0x03000000)
#define WUF_CFGX_PATTERN_OFFSET		(0x007F0000)
#define WUF_CFGX_PATTERN_OFFSET_SHIFT	(16)
#define WUF_CFGX_CRC16			(0x0000FFFF)
#define WUF_NUM				(8)

#define WUF_MASKX			(0x170)
#define WUF_MASKX_AVALID		(0x80000000)
#define WUF_MASKX_ATYPE			(0x40000000)

#define ADDR_FILTX			(0x300)
#define ADDR_FILTX_FB_VALID		(0x80000000)
#define ADDR_FILTX_FB_TYPE		(0x40000000)
#define ADDR_FILTX_FB_ADDRHI		(0x0000FFFF)
#define ADDR_FILTX_SB_ADDRLO		(0xFFFFFFFF)

#define WUCSR2				(0x500)
#define WUCSR2_NS_RCD			(0x00000040)
#define WUCSR2_ARP_RCD			(0x00000020)
#define WUCSR2_TCPSYN_RCD		(0x00000010)
#define WUCSR2_NS_OFFLOAD		(0x00000004)
#define WUCSR2_ARP_OFFLOAD		(0x00000002)
#define WUCSR2_TCPSYN_OFFLOAD		(0x00000001)

#define WOL_FIFO_STS			(0x504)

#define IPV6_ADDRX			(0x510)

#define IPV4_ADDRX			(0x590)


/* Vendor-specific PHY Definitions */

/* Mode Control/Status Register */
#define PHY_MODE_CTRL_STS		(17)
#define MODE_CTRL_STS_EDPWRDOWN		((u16)0x2000)
#define MODE_CTRL_STS_ENERGYON		((u16)0x0002)

#define PHY_INT_SRC			(29)
#define PHY_INT_SRC_ENERGY_ON		((u16)0x0080)
#define PHY_INT_SRC_ANEG_COMP		((u16)0x0040)
#define PHY_INT_SRC_REMOTE_FAULT	((u16)0x0020)
#define PHY_INT_SRC_LINK_DOWN		((u16)0x0010)
#define PHY_INT_SRC_CLEAR_ALL		((u16)0xffff)

#define PHY_INT_MASK			(30)
#define PHY_INT_MASK_ENERGY_ON		((u16)0x0080)
#define PHY_INT_MASK_ANEG_COMP		((u16)0x0040)
#define PHY_INT_MASK_REMOTE_FAULT	((u16)0x0020)
#define PHY_INT_MASK_LINK_DOWN		((u16)0x0010)
#define PHY_INT_MASK_DEFAULT		(PHY_INT_MASK_ANEG_COMP | \
					 PHY_INT_MASK_LINK_DOWN)

#define PHY_SPECIAL			(31)
#define PHY_SPECIAL_SPD			((u16)0x001C)
#define PHY_SPECIAL_SPD_10HALF		((u16)0x0004)
#define PHY_SPECIAL_SPD_10FULL		((u16)0x0014)
#define PHY_SPECIAL_SPD_100HALF		((u16)0x0008)
#define PHY_SPECIAL_SPD_100FULL		((u16)0x0018)

/* USB Vendor Requests */
#define USB_VENDOR_REQUEST_WRITE_REGISTER	0xA0
#define USB_VENDOR_REQUEST_READ_REGISTER	0xA1
#define USB_VENDOR_REQUEST_GET_STATS		0xA2

/* Interrupt Endpoint status word bitfields */
#define INT_ENP_RDFO_INT		((u32)BIT(22))
#define INT_ENP_TXE_INT			((u32)BIT(21))
#define INT_ENP_TX_DIS_INT		((u32)BIT(19))
#define INT_ENP_RX_DIS_INT		((u32)BIT(18))
#define INT_ENP_PHY_INT			((u32)BIT(17))
#define INT_ENP_MAC_ERR_INT		((u32)BIT(15))
#define INT_ENP_RX_FIFO_DATA_INT	((u32)BIT(12))

#endif /* _SMSC75XX_H */
