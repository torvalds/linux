/* atlx_hw.h -- common hardware definitions for Attansic network drivers
 *
 * Copyright(c) 2005 - 2006 Attansic Corporation. All rights reserved.
 * Copyright(c) 2006 - 2007 Chris Snook <csnook@redhat.com>
 * Copyright(c) 2006 Jay Cliburn <jcliburn@gmail.com>
 * Copyright(c) 2007 Atheros Corporation. All rights reserved.
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

#ifndef ATLX_H
#define ATLX_H

#include <linux/module.h>
#include <linux/types.h>

#define ATLX_DRIVER_VERSION "2.1.1"
MODULE_AUTHOR("Xiong Huang <xiong.huang@atheros.com>, \
	Chris Snook <csnook@redhat.com>, Jay Cliburn <jcliburn@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION(ATLX_DRIVER_VERSION);

#define ATLX_ERR_PHY			2
#define ATLX_ERR_PHY_SPEED		7
#define ATLX_ERR_PHY_RES		8

#define SPEED_0				0xffff
#define SPEED_10			10
#define SPEED_100			100
#define SPEED_1000			1000
#define HALF_DUPLEX			1
#define FULL_DUPLEX			2

#define MEDIA_TYPE_AUTO_SENSOR		0

/* register definitions */
#define REG_PM_CTRLSTAT			0x44

#define REG_PCIE_CAP_LIST		0x58

#define REG_VPD_CAP			0x6C
#define VPD_CAP_ID_MASK			0xFF
#define VPD_CAP_ID_SHIFT		0
#define VPD_CAP_NEXT_PTR_MASK		0xFF
#define VPD_CAP_NEXT_PTR_SHIFT		8
#define VPD_CAP_VPD_ADDR_MASK		0x7FFF
#define VPD_CAP_VPD_ADDR_SHIFT		16
#define VPD_CAP_VPD_FLAG		0x80000000

#define REG_VPD_DATA			0x70

#define REG_SPI_FLASH_CTRL		0x200
#define SPI_FLASH_CTRL_STS_NON_RDY	0x1
#define SPI_FLASH_CTRL_STS_WEN		0x2
#define SPI_FLASH_CTRL_STS_WPEN		0x80
#define SPI_FLASH_CTRL_DEV_STS_MASK	0xFF
#define SPI_FLASH_CTRL_DEV_STS_SHIFT	0
#define SPI_FLASH_CTRL_INS_MASK		0x7
#define SPI_FLASH_CTRL_INS_SHIFT	8
#define SPI_FLASH_CTRL_START		0x800
#define SPI_FLASH_CTRL_EN_VPD		0x2000
#define SPI_FLASH_CTRL_LDSTART		0x8000
#define SPI_FLASH_CTRL_CS_HI_MASK	0x3
#define SPI_FLASH_CTRL_CS_HI_SHIFT	16
#define SPI_FLASH_CTRL_CS_HOLD_MASK	0x3
#define SPI_FLASH_CTRL_CS_HOLD_SHIFT	18
#define SPI_FLASH_CTRL_CLK_LO_MASK	0x3
#define SPI_FLASH_CTRL_CLK_LO_SHIFT	20
#define SPI_FLASH_CTRL_CLK_HI_MASK	0x3
#define SPI_FLASH_CTRL_CLK_HI_SHIFT	22
#define SPI_FLASH_CTRL_CS_SETUP_MASK	0x3
#define SPI_FLASH_CTRL_CS_SETUP_SHIFT	24
#define SPI_FLASH_CTRL_EROM_PGSZ_MASK	0x3
#define SPI_FLASH_CTRL_EROM_PGSZ_SHIFT	26
#define SPI_FLASH_CTRL_WAIT_READY	0x10000000

#define REG_SPI_ADDR			0x204

#define REG_SPI_DATA			0x208

#define REG_SPI_FLASH_CONFIG		0x20C
#define SPI_FLASH_CONFIG_LD_ADDR_MASK	0xFFFFFF
#define SPI_FLASH_CONFIG_LD_ADDR_SHIFT	0
#define SPI_FLASH_CONFIG_VPD_ADDR_MASK	0x3
#define SPI_FLASH_CONFIG_VPD_ADDR_SHIFT	24
#define SPI_FLASH_CONFIG_LD_EXIST	0x4000000

#define REG_SPI_FLASH_OP_PROGRAM	0x210
#define REG_SPI_FLASH_OP_SC_ERASE	0x211
#define REG_SPI_FLASH_OP_CHIP_ERASE	0x212
#define REG_SPI_FLASH_OP_RDID		0x213
#define REG_SPI_FLASH_OP_WREN		0x214
#define REG_SPI_FLASH_OP_RDSR		0x215
#define REG_SPI_FLASH_OP_WRSR		0x216
#define REG_SPI_FLASH_OP_READ		0x217

#define REG_TWSI_CTRL			0x218
#define TWSI_CTRL_LD_OFFSET_MASK	0xFF
#define TWSI_CTRL_LD_OFFSET_SHIFT	0
#define TWSI_CTRL_LD_SLV_ADDR_MASK	0x7
#define TWSI_CTRL_LD_SLV_ADDR_SHIFT	8
#define TWSI_CTRL_SW_LDSTART		0x800
#define TWSI_CTRL_HW_LDSTART		0x1000
#define TWSI_CTRL_SMB_SLV_ADDR_MASK	0x7F
#define TWSI_CTRL_SMB_SLV_ADDR_SHIFT	15
#define TWSI_CTRL_LD_EXIST		0x400000
#define TWSI_CTRL_READ_FREQ_SEL_MASK	0x3
#define TWSI_CTRL_READ_FREQ_SEL_SHIFT	23
#define TWSI_CTRL_FREQ_SEL_100K		0
#define TWSI_CTRL_FREQ_SEL_200K		1
#define TWSI_CTRL_FREQ_SEL_300K		2
#define TWSI_CTRL_FREQ_SEL_400K		3
#define TWSI_CTRL_SMB_SLV_ADDR		/* FIXME: define or remove */
#define TWSI_CTRL_WRITE_FREQ_SEL_MASK	0x3
#define TWSI_CTRL_WRITE_FREQ_SEL_SHIFT	24

#define REG_PCIE_DEV_MISC_CTRL			0x21C
#define PCIE_DEV_MISC_CTRL_EXT_PIPE		0x2
#define PCIE_DEV_MISC_CTRL_RETRY_BUFDIS		0x1
#define PCIE_DEV_MISC_CTRL_SPIROM_EXIST		0x4
#define PCIE_DEV_MISC_CTRL_SERDES_ENDIAN	0x8
#define PCIE_DEV_MISC_CTRL_SERDES_SEL_DIN	0x10

#define REG_PCIE_PHYMISC		0x1000
#define PCIE_PHYMISC_FORCE_RCV_DET	0x4

#define REG_PCIE_DLL_TX_CTRL1		0x1104
#define PCIE_DLL_TX_CTRL1_SEL_NOR_CLK	0x400
#define PCIE_DLL_TX_CTRL1_DEF		0x568

#define REG_LTSSM_TEST_MODE		0x12FC
#define LTSSM_TEST_MODE_DEF		0x6500

/* Master Control Register */
#define REG_MASTER_CTRL			0x1400
#define MASTER_CTRL_SOFT_RST		0x1
#define MASTER_CTRL_MTIMER_EN		0x2
#define MASTER_CTRL_ITIMER_EN		0x4
#define MASTER_CTRL_MANUAL_INT		0x8
#define MASTER_CTRL_REV_NUM_SHIFT	16
#define MASTER_CTRL_REV_NUM_MASK	0xFF
#define MASTER_CTRL_DEV_ID_SHIFT	24
#define MASTER_CTRL_DEV_ID_MASK		0xFF

/* Timer Initial Value Register */
#define REG_MANUAL_TIMER_INIT		0x1404

/* IRQ Moderator Timer Initial Value Register */
#define REG_IRQ_MODU_TIMER_INIT		0x1408

#define REG_PHY_ENABLE			0x140C

/* IRQ Anti-Lost Timer Initial Value Register */
#define REG_CMBDISDMA_TIMER		0x140E

/* Block IDLE Status Register */
#define REG_IDLE_STATUS			0x1410

/* MDIO Control Register */
#define REG_MDIO_CTRL			0x1414
#define MDIO_DATA_MASK			0xFFFF
#define MDIO_DATA_SHIFT			0
#define MDIO_REG_ADDR_MASK		0x1F
#define MDIO_REG_ADDR_SHIFT		16
#define MDIO_RW				0x200000
#define MDIO_SUP_PREAMBLE		0x400000
#define MDIO_START			0x800000
#define MDIO_CLK_SEL_SHIFT		24
#define MDIO_CLK_25_4			0
#define MDIO_CLK_25_6			2
#define MDIO_CLK_25_8			3
#define MDIO_CLK_25_10			4
#define MDIO_CLK_25_14			5
#define MDIO_CLK_25_20			6
#define MDIO_CLK_25_28			7
#define MDIO_BUSY			0x8000000

/* MII PHY Status Register */
#define REG_PHY_STATUS			0x1418

/* BIST Control and Status Register0 (for the Packet Memory) */
#define REG_BIST0_CTRL			0x141C
#define BIST0_NOW			0x1
#define BIST0_SRAM_FAIL			0x2
#define BIST0_FUSE_FLAG			0x4
#define REG_BIST1_CTRL			0x1420
#define BIST1_NOW			0x1
#define BIST1_SRAM_FAIL			0x2
#define BIST1_FUSE_FLAG			0x4

/* SerDes Lock Detect Control and Status Register */
#define REG_SERDES_LOCK			0x1424
#define SERDES_LOCK_DETECT		1
#define SERDES_LOCK_DETECT_EN		2

/* MAC Control Register */
#define REG_MAC_CTRL			0x1480
#define MAC_CTRL_TX_EN			1
#define MAC_CTRL_RX_EN			2
#define MAC_CTRL_TX_FLOW		4
#define MAC_CTRL_RX_FLOW		8
#define MAC_CTRL_LOOPBACK		0x10
#define MAC_CTRL_DUPLX			0x20
#define MAC_CTRL_ADD_CRC		0x40
#define MAC_CTRL_PAD			0x80
#define MAC_CTRL_LENCHK			0x100
#define MAC_CTRL_HUGE_EN		0x200
#define MAC_CTRL_PRMLEN_SHIFT		10
#define MAC_CTRL_PRMLEN_MASK		0xF
#define MAC_CTRL_RMV_VLAN		0x4000
#define MAC_CTRL_PROMIS_EN		0x8000
#define MAC_CTRL_MC_ALL_EN		0x2000000
#define MAC_CTRL_BC_EN			0x4000000

/* MAC IPG/IFG Control Register */
#define REG_MAC_IPG_IFG			0x1484
#define MAC_IPG_IFG_IPGT_SHIFT		0
#define MAC_IPG_IFG_IPGT_MASK		0x7F
#define MAC_IPG_IFG_MIFG_SHIFT		8
#define MAC_IPG_IFG_MIFG_MASK		0xFF
#define MAC_IPG_IFG_IPGR1_SHIFT		16
#define MAC_IPG_IFG_IPGR1_MASK		0x7F
#define MAC_IPG_IFG_IPGR2_SHIFT		24
#define MAC_IPG_IFG_IPGR2_MASK		0x7F

/* MAC STATION ADDRESS */
#define REG_MAC_STA_ADDR		0x1488

/* Hash table for multicast address */
#define REG_RX_HASH_TABLE		0x1490

/* MAC Half-Duplex Control Register */
#define REG_MAC_HALF_DUPLX_CTRL			0x1498
#define MAC_HALF_DUPLX_CTRL_LCOL_SHIFT		0
#define MAC_HALF_DUPLX_CTRL_LCOL_MASK		0x3FF
#define MAC_HALF_DUPLX_CTRL_RETRY_SHIFT		12
#define MAC_HALF_DUPLX_CTRL_RETRY_MASK		0xF
#define MAC_HALF_DUPLX_CTRL_EXC_DEF_EN		0x10000
#define MAC_HALF_DUPLX_CTRL_NO_BACK_C		0x20000
#define MAC_HALF_DUPLX_CTRL_NO_BACK_P		0x40000
#define MAC_HALF_DUPLX_CTRL_ABEBE		0x80000
#define MAC_HALF_DUPLX_CTRL_ABEBT_SHIFT		20
#define MAC_HALF_DUPLX_CTRL_ABEBT_MASK		0xF
#define MAC_HALF_DUPLX_CTRL_JAMIPG_SHIFT	24
#define MAC_HALF_DUPLX_CTRL_JAMIPG_MASK		0xF

/* Maximum Frame Length Control Register */
#define REG_MTU				0x149C

/* Wake-On-Lan control register */
#define REG_WOL_CTRL			0x14A0
#define WOL_PATTERN_EN			0x1
#define WOL_PATTERN_PME_EN		0x2
#define WOL_MAGIC_EN			0x4
#define WOL_MAGIC_PME_EN		0x8
#define WOL_LINK_CHG_EN			0x10
#define WOL_LINK_CHG_PME_EN		0x20
#define WOL_PATTERN_ST			0x100
#define WOL_MAGIC_ST			0x200
#define WOL_LINKCHG_ST			0x400
#define WOL_PT0_EN			0x10000
#define WOL_PT1_EN			0x20000
#define WOL_PT2_EN			0x40000
#define WOL_PT3_EN			0x80000
#define WOL_PT4_EN			0x100000
#define WOL_PT0_MATCH			0x1000000
#define WOL_PT1_MATCH			0x2000000
#define WOL_PT2_MATCH			0x4000000
#define WOL_PT3_MATCH			0x8000000
#define WOL_PT4_MATCH			0x10000000

/* Internal SRAM Partition Register, high 32 bits */
#define REG_SRAM_RFD_ADDR		0x1500

/* Descriptor Control register, high 32 bits */
#define REG_DESC_BASE_ADDR_HI		0x1540

/* Interrupt Status Register */
#define REG_ISR				0x1600
#define ISR_UR_DETECTED			0x1000000
#define ISR_FERR_DETECTED		0x2000000
#define ISR_NFERR_DETECTED		0x4000000
#define ISR_CERR_DETECTED		0x8000000
#define ISR_PHY_LINKDOWN		0x10000000
#define ISR_DIS_INT			0x80000000

/* Interrupt Mask Register */
#define REG_IMR				0x1604

#define REG_RFD_RRD_IDX			0x1800
#define REG_TPD_IDX			0x1804

/* MII definitions */

/* PHY Common Register */
#define MII_ATLX_CR			0x09
#define MII_ATLX_SR			0x0A
#define MII_ATLX_ESR			0x0F
#define MII_ATLX_PSCR			0x10
#define MII_ATLX_PSSR			0x11

/* PHY Control Register */
#define MII_CR_SPEED_SELECT_MSB		0x0040	/* bits 6,13: 10=1000, 01=100,
						 * 00=10
						 */
#define MII_CR_COLL_TEST_ENABLE		0x0080	/* Collision test enable */
#define MII_CR_FULL_DUPLEX		0x0100	/* FDX =1, half duplex =0 */
#define MII_CR_RESTART_AUTO_NEG		0x0200	/* Restart auto negotiation */
#define MII_CR_ISOLATE			0x0400	/* Isolate PHY from MII */
#define MII_CR_POWER_DOWN		0x0800	/* Power down */
#define MII_CR_AUTO_NEG_EN		0x1000	/* Auto Neg Enable */
#define MII_CR_SPEED_SELECT_LSB		0x2000	/* bits 6,13: 10=1000, 01=100,
						 * 00=10
						 */
#define MII_CR_LOOPBACK			0x4000	/* 0 = normal, 1 = loopback */
#define MII_CR_RESET			0x8000	/* 0 = normal, 1 = PHY reset */
#define MII_CR_SPEED_MASK		0x2040
#define MII_CR_SPEED_1000		0x0040
#define MII_CR_SPEED_100		0x2000
#define MII_CR_SPEED_10			0x0000

/* PHY Status Register */
#define MII_SR_EXTENDED_CAPS		0x0001	/* Ext register capabilities */
#define MII_SR_JABBER_DETECT		0x0002	/* Jabber Detected */
#define MII_SR_LINK_STATUS		0x0004	/* Link Status 1 = link */
#define MII_SR_AUTONEG_CAPS		0x0008	/* Auto Neg Capable */
#define MII_SR_REMOTE_FAULT		0x0010	/* Remote Fault Detect */
#define MII_SR_AUTONEG_COMPLETE		0x0020	/* Auto Neg Complete */
#define MII_SR_PREAMBLE_SUPPRESS	0x0040	/* Preamble may be suppressed */
#define MII_SR_EXTENDED_STATUS		0x0100	/* Ext stat info in Reg 0x0F */
#define MII_SR_100T2_HD_CAPS		0x0200	/* 100T2 Half Duplex Capable */
#define MII_SR_100T2_FD_CAPS		0x0400	/* 100T2 Full Duplex Capable */
#define MII_SR_10T_HD_CAPS		0x0800	/* 10T   Half Duplex Capable */
#define MII_SR_10T_FD_CAPS		0x1000	/* 10T   Full Duplex Capable */
#define MII_SR_100X_HD_CAPS		0x2000	/* 100X  Half Duplex Capable */
#define MII_SR_100X_FD_CAPS		0x4000	/* 100X  Full Duplex Capable */
#define MII_SR_100T4_CAPS		0x8000	/* 100T4 Capable */

/* Link partner ability register */
#define MII_LPA_SLCT			0x001f	/* Same as advertise selector */
#define MII_LPA_10HALF			0x0020	/* Can do 10mbps half-duplex */
#define MII_LPA_10FULL			0x0040	/* Can do 10mbps full-duplex */
#define MII_LPA_100HALF			0x0080	/* Can do 100mbps half-duplex */
#define MII_LPA_100FULL			0x0100	/* Can do 100mbps full-duplex */
#define MII_LPA_100BASE4		0x0200	/* 100BASE-T4 */
#define MII_LPA_PAUSE			0x0400	/* PAUSE */
#define MII_LPA_ASYPAUSE		0x0800	/* Asymmetrical PAUSE */
#define MII_LPA_RFAULT			0x2000	/* Link partner faulted */
#define MII_LPA_LPACK			0x4000	/* Link partner acked us */
#define MII_LPA_NPAGE			0x8000	/* Next page bit */

/* Autoneg Advertisement Register */
#define MII_AR_SELECTOR_FIELD		0x0001	/* IEEE 802.3 CSMA/CD */
#define MII_AR_10T_HD_CAPS		0x0020	/* 10T   Half Duplex Capable */
#define MII_AR_10T_FD_CAPS		0x0040	/* 10T   Full Duplex Capable */
#define MII_AR_100TX_HD_CAPS		0x0080	/* 100TX Half Duplex Capable */
#define MII_AR_100TX_FD_CAPS		0x0100	/* 100TX Full Duplex Capable */
#define MII_AR_100T4_CAPS		0x0200	/* 100T4 Capable */
#define MII_AR_PAUSE			0x0400	/* Pause operation desired */
#define MII_AR_ASM_DIR			0x0800	/* Asymmetric Pause Dir bit */
#define MII_AR_REMOTE_FAULT		0x2000	/* Remote Fault detected */
#define MII_AR_NEXT_PAGE		0x8000	/* Next Page ability support */
#define MII_AR_SPEED_MASK		0x01E0
#define MII_AR_DEFAULT_CAP_MASK		0x0DE0

/* 1000BASE-T Control Register */
#define MII_ATLX_CR_1000T_HD_CAPS	0x0100	/* Adv 1000T HD cap */
#define MII_ATLX_CR_1000T_FD_CAPS	0x0200	/* Adv 1000T FD cap */
#define MII_ATLX_CR_1000T_REPEATER_DTE	0x0400	/* 1=Repeater/switch device,
						 * 0=DTE device */
#define MII_ATLX_CR_1000T_MS_VALUE	0x0800	/* 1=Config PHY as Master,
						 * 0=Configure PHY as Slave */
#define MII_ATLX_CR_1000T_MS_ENABLE	0x1000	/* 1=Man Master/Slave config,
						 * 0=Auto Master/Slave config
						 */
#define MII_ATLX_CR_1000T_TEST_MODE_NORMAL	0x0000	/* Normal Operation */
#define MII_ATLX_CR_1000T_TEST_MODE_1	0x2000	/* Transmit Waveform test */
#define MII_ATLX_CR_1000T_TEST_MODE_2	0x4000	/* Master Xmit Jitter test */
#define MII_ATLX_CR_1000T_TEST_MODE_3	0x6000	/* Slave Xmit Jitter test */
#define MII_ATLX_CR_1000T_TEST_MODE_4	0x8000	/* Xmitter Distortion test */
#define MII_ATLX_CR_1000T_SPEED_MASK	0x0300
#define MII_ATLX_CR_1000T_DEFAULT_CAP_MASK	0x0300

/* 1000BASE-T Status Register */
#define MII_ATLX_SR_1000T_LP_HD_CAPS	0x0400	/* LP is 1000T HD capable */
#define MII_ATLX_SR_1000T_LP_FD_CAPS	0x0800	/* LP is 1000T FD capable */
#define MII_ATLX_SR_1000T_REMOTE_RX_STATUS	0x1000	/* Remote receiver OK */
#define MII_ATLX_SR_1000T_LOCAL_RX_STATUS	0x2000	/* Local receiver OK */
#define MII_ATLX_SR_1000T_MS_CONFIG_RES		0x4000	/* 1=Local TX is Master
							 * 0=Slave
							 */
#define MII_ATLX_SR_1000T_MS_CONFIG_FAULT	0x8000	/* Master/Slave config
							 * fault */
#define MII_ATLX_SR_1000T_REMOTE_RX_STATUS_SHIFT	12
#define MII_ATLX_SR_1000T_LOCAL_RX_STATUS_SHIFT		13

/* Extended Status Register */
#define MII_ATLX_ESR_1000T_HD_CAPS	0x1000	/* 1000T HD capable */
#define MII_ATLX_ESR_1000T_FD_CAPS	0x2000	/* 1000T FD capable */
#define MII_ATLX_ESR_1000X_HD_CAPS	0x4000	/* 1000X HD capable */
#define MII_ATLX_ESR_1000X_FD_CAPS	0x8000	/* 1000X FD capable */

/* ATLX PHY Specific Control Register */
#define MII_ATLX_PSCR_JABBER_DISABLE	0x0001	/* 1=Jabber Func disabled */
#define MII_ATLX_PSCR_POLARITY_REVERSAL	0x0002	/* 1=Polarity Reversal enbld */
#define MII_ATLX_PSCR_SQE_TEST		0x0004	/* 1=SQE Test enabled */
#define MII_ATLX_PSCR_MAC_POWERDOWN	0x0008
#define MII_ATLX_PSCR_CLK125_DISABLE	0x0010	/* 1=CLK125 low
						 * 0=CLK125 toggling
						 */
#define MII_ATLX_PSCR_MDI_MANUAL_MODE	0x0000	/* MDI Crossover Mode bits 6:5,
						 * Manual MDI configuration
						 */
#define MII_ATLX_PSCR_MDIX_MANUAL_MODE	0x0020	/* Manual MDIX configuration */
#define MII_ATLX_PSCR_AUTO_X_1000T	0x0040	/* 1000BASE-T: Auto crossover
						 * 100BASE-TX/10BASE-T: MDI
						 * Mode */
#define MII_ATLX_PSCR_AUTO_X_MODE	0x0060	/* Auto crossover enabled
						 * all speeds.
						 */
#define MII_ATLX_PSCR_10BT_EXT_DIST_ENABLE	0x0080	/* 1=Enable Extended
							 * 10BASE-T distance
							 * (Lower 10BASE-T RX
							 * Threshold)
							 * 0=Normal 10BASE-T RX
							 * Threshold
							 */
#define MII_ATLX_PSCR_MII_5BIT_ENABLE	0x0100	/* 1=5-Bit interface in
						 * 100BASE-TX
						 * 0=MII interface in
						 * 100BASE-TX
						 */
#define MII_ATLX_PSCR_SCRAMBLER_DISABLE	0x0200	/* 1=Scrambler dsbl */
#define MII_ATLX_PSCR_FORCE_LINK_GOOD	0x0400	/* 1=Force link good */
#define MII_ATLX_PSCR_ASSERT_CRS_ON_TX	0x0800	/* 1=Assert CRS on Transmit */
#define MII_ATLX_PSCR_POLARITY_REVERSAL_SHIFT		1
#define MII_ATLX_PSCR_AUTO_X_MODE_SHIFT			5
#define MII_ATLX_PSCR_10BT_EXT_DIST_ENABLE_SHIFT	7

/* ATLX PHY Specific Status Register */
#define MII_ATLX_PSSR_SPD_DPLX_RESOLVED	0x0800	/* 1=Speed & Duplex resolved */
#define MII_ATLX_PSSR_DPLX		0x2000	/* 1=Duplex 0=Half Duplex */
#define MII_ATLX_PSSR_SPEED		0xC000	/* Speed, bits 14:15 */
#define MII_ATLX_PSSR_10MBS		0x0000	/* 00=10Mbs */
#define MII_ATLX_PSSR_100MBS		0x4000	/* 01=100Mbs */
#define MII_ATLX_PSSR_1000MBS		0x8000	/* 10=1000Mbs */

/* PCI Command Register Bit Definitions */
#define PCI_REG_COMMAND			0x04	/* PCI Command Register */
#define CMD_IO_SPACE			0x0001
#define CMD_MEMORY_SPACE		0x0002
#define CMD_BUS_MASTER			0x0004

/* Wake Up Filter Control */
#define ATLX_WUFC_LNKC	0x00000001	/* Link Status Change Wakeup Enable */
#define ATLX_WUFC_MAG	0x00000002	/* Magic Packet Wakeup Enable */
#define ATLX_WUFC_EX	0x00000004	/* Directed Exact Wakeup Enable */
#define ATLX_WUFC_MC	0x00000008	/* Multicast Wakeup Enable */
#define ATLX_WUFC_BC	0x00000010	/* Broadcast Wakeup Enable */

#define ADVERTISE_10_HALF		0x0001
#define ADVERTISE_10_FULL		0x0002
#define ADVERTISE_100_HALF		0x0004
#define ADVERTISE_100_FULL		0x0008
#define ADVERTISE_1000_HALF		0x0010
#define ADVERTISE_1000_FULL		0x0020
#define AUTONEG_ADVERTISE_10_100_ALL	0x000F	/* All 10/100 speeds */
#define AUTONEG_ADVERTISE_10_ALL	0x0003	/* 10Mbps Full & Half speeds */

#define PHY_AUTO_NEG_TIME		45	/* 4.5 Seconds */
#define PHY_FORCE_TIME			20	/* 2.0 Seconds */

/* For checksumming, the sum of all words in the EEPROM should equal 0xBABA */
#define EEPROM_SUM			0xBABA
#define NODE_ADDRESS_SIZE		6

struct atlx_spi_flash_dev {
	const char *manu_name;	/* manufacturer id */
	/* op-code */
	u8 cmd_wrsr;
	u8 cmd_read;
	u8 cmd_program;
	u8 cmd_wren;
	u8 cmd_wrdi;
	u8 cmd_rdsr;
	u8 cmd_rdid;
	u8 cmd_sector_erase;
	u8 cmd_chip_erase;
};

#endif /* ATLX_H */
