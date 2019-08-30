/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright(c) 2008 - 2009 Atheros Corporation. All rights reserved.
 *
 * Derived from Intel e1000 driver
 * Copyright(c) 1999 - 2005 Intel Corporation. All rights reserved.
 */

#ifndef _ATL1C_HW_H_
#define _ATL1C_HW_H_

#include <linux/types.h>
#include <linux/mii.h>

#define FIELD_GETX(_x, _name)   ((_x) >> (_name##_SHIFT) & (_name##_MASK))
#define FIELD_SETX(_x, _name, _v) \
(((_x) & ~((_name##_MASK) << (_name##_SHIFT))) |\
(((_v) & (_name##_MASK)) << (_name##_SHIFT)))
#define FIELDX(_name, _v) (((_v) & (_name##_MASK)) << (_name##_SHIFT))

struct atl1c_adapter;
struct atl1c_hw;

/* function prototype */
void atl1c_phy_disable(struct atl1c_hw *hw);
void atl1c_hw_set_mac_addr(struct atl1c_hw *hw, u8 *mac_addr);
int atl1c_phy_reset(struct atl1c_hw *hw);
int atl1c_read_mac_addr(struct atl1c_hw *hw);
int atl1c_get_speed_and_duplex(struct atl1c_hw *hw, u16 *speed, u16 *duplex);
u32 atl1c_hash_mc_addr(struct atl1c_hw *hw, u8 *mc_addr);
void atl1c_hash_set(struct atl1c_hw *hw, u32 hash_value);
int atl1c_read_phy_reg(struct atl1c_hw *hw, u16 reg_addr, u16 *phy_data);
int atl1c_write_phy_reg(struct atl1c_hw *hw, u32 reg_addr, u16 phy_data);
bool atl1c_read_eeprom(struct atl1c_hw *hw, u32 offset, u32 *p_value);
int atl1c_phy_init(struct atl1c_hw *hw);
int atl1c_check_eeprom_exist(struct atl1c_hw *hw);
int atl1c_restart_autoneg(struct atl1c_hw *hw);
int atl1c_phy_to_ps_link(struct atl1c_hw *hw);
int atl1c_power_saving(struct atl1c_hw *hw, u32 wufc);
bool atl1c_wait_mdio_idle(struct atl1c_hw *hw);
void atl1c_stop_phy_polling(struct atl1c_hw *hw);
void atl1c_start_phy_polling(struct atl1c_hw *hw, u16 clk_sel);
int atl1c_read_phy_core(struct atl1c_hw *hw, bool ext, u8 dev,
			u16 reg, u16 *phy_data);
int atl1c_write_phy_core(struct atl1c_hw *hw, bool ext, u8 dev,
			u16 reg, u16 phy_data);
int atl1c_read_phy_ext(struct atl1c_hw *hw, u8 dev_addr,
			u16 reg_addr, u16 *phy_data);
int atl1c_write_phy_ext(struct atl1c_hw *hw, u8 dev_addr,
			u16 reg_addr, u16 phy_data);
int atl1c_read_phy_dbg(struct atl1c_hw *hw, u16 reg_addr, u16 *phy_data);
int atl1c_write_phy_dbg(struct atl1c_hw *hw, u16 reg_addr, u16 phy_data);
void atl1c_post_phy_linkchg(struct atl1c_hw *hw, u16 link_speed);

/* hw-ids */
#define PCI_DEVICE_ID_ATTANSIC_L2C      0x1062
#define PCI_DEVICE_ID_ATTANSIC_L1C      0x1063
#define PCI_DEVICE_ID_ATHEROS_L2C_B	0x2060 /* AR8152 v1.1 Fast 10/100 */
#define PCI_DEVICE_ID_ATHEROS_L2C_B2	0x2062 /* AR8152 v2.0 Fast 10/100 */
#define PCI_DEVICE_ID_ATHEROS_L1D	0x1073 /* AR8151 v1.0 Gigabit 1000 */
#define PCI_DEVICE_ID_ATHEROS_L1D_2_0	0x1083 /* AR8151 v2.0 Gigabit 1000 */
#define L2CB_V10			0xc0
#define L2CB_V11			0xc1
#define L2CB_V20			0xc0
#define L2CB_V21			0xc1

/* register definition */
#define REG_DEVICE_CAP              	0x5C
#define DEVICE_CAP_MAX_PAYLOAD_MASK     0x7
#define DEVICE_CAP_MAX_PAYLOAD_SHIFT    0

#define DEVICE_CTRL_MAXRRS_MIN		2

#define REG_LINK_CTRL			0x68
#define LINK_CTRL_L0S_EN		0x01
#define LINK_CTRL_L1_EN			0x02
#define LINK_CTRL_EXT_SYNC		0x80

#define REG_PCIE_IND_ACC_ADDR		0x80
#define REG_PCIE_IND_ACC_DATA		0x84

#define REG_DEV_SERIALNUM_CTRL		0x200
#define REG_DEV_MAC_SEL_MASK		0x0 /* 0:EUI; 1:MAC */
#define REG_DEV_MAC_SEL_SHIFT		0
#define REG_DEV_SERIAL_NUM_EN_MASK	0x1
#define REG_DEV_SERIAL_NUM_EN_SHIFT	1

#define REG_TWSI_CTRL               	0x218
#define TWSI_CTLR_FREQ_MASK		0x3UL
#define TWSI_CTRL_FREQ_SHIFT		24
#define TWSI_CTRL_FREQ_100K		0
#define TWSI_CTRL_FREQ_200K		1
#define TWSI_CTRL_FREQ_300K		2
#define TWSI_CTRL_FREQ_400K		3
#define TWSI_CTRL_LD_EXIST		BIT(23)
#define TWSI_CTRL_HW_LDSTAT		BIT(12)	/* 0:finish,1:in progress */
#define TWSI_CTRL_SW_LDSTART            BIT(11)
#define TWSI_CTRL_LD_OFFSET_MASK        0xFF
#define TWSI_CTRL_LD_OFFSET_SHIFT       0

#define REG_PCIE_DEV_MISC_CTRL      	0x21C
#define PCIE_DEV_MISC_EXT_PIPE     	0x2
#define PCIE_DEV_MISC_RETRY_BUFDIS 	0x1
#define PCIE_DEV_MISC_SPIROM_EXIST 	0x4
#define PCIE_DEV_MISC_SERDES_ENDIAN    	0x8
#define PCIE_DEV_MISC_SERDES_SEL_DIN   	0x10

#define REG_PCIE_PHYMISC	    	0x1000
#define PCIE_PHYMISC_FORCE_RCV_DET	BIT(2)
#define PCIE_PHYMISC_NFTS_MASK		0xFFUL
#define PCIE_PHYMISC_NFTS_SHIFT		16

#define REG_PCIE_PHYMISC2		0x1004
#define PCIE_PHYMISC2_L0S_TH_MASK	0x3UL
#define PCIE_PHYMISC2_L0S_TH_SHIFT	18
#define L2CB1_PCIE_PHYMISC2_L0S_TH	3
#define PCIE_PHYMISC2_CDR_BW_MASK	0x3UL
#define PCIE_PHYMISC2_CDR_BW_SHIFT	16
#define L2CB1_PCIE_PHYMISC2_CDR_BW	3

#define REG_TWSI_DEBUG			0x1108
#define TWSI_DEBUG_DEV_EXIST		BIT(29)

#define REG_DMA_DBG			0x1114
#define DMA_DBG_VENDOR_MSG		BIT(0)

#define REG_EEPROM_CTRL			0x12C0
#define EEPROM_CTRL_DATA_HI_MASK	0xFFFF
#define EEPROM_CTRL_DATA_HI_SHIFT	0
#define EEPROM_CTRL_ADDR_MASK		0x3FF
#define EEPROM_CTRL_ADDR_SHIFT		16
#define EEPROM_CTRL_ACK			0x40000000
#define EEPROM_CTRL_RW			0x80000000

#define REG_EEPROM_DATA_LO		0x12C4

#define REG_OTP_CTRL			0x12F0
#define OTP_CTRL_CLK_EN			BIT(1)

#define REG_PM_CTRL			0x12F8
#define PM_CTRL_HOTRST			BIT(31)
#define PM_CTRL_MAC_ASPM_CHK		BIT(30)	/* L0s/L1 dis by MAC based on
						 * thrghput(setting in 15A0) */
#define PM_CTRL_SA_DLY_EN		BIT(29)
#define PM_CTRL_L0S_BUFSRX_EN		BIT(28)
#define PM_CTRL_LCKDET_TIMER_MASK	0xFUL
#define PM_CTRL_LCKDET_TIMER_SHIFT	24
#define PM_CTRL_LCKDET_TIMER_DEF	0xC
#define PM_CTRL_PM_REQ_TIMER_MASK	0xFUL
#define PM_CTRL_PM_REQ_TIMER_SHIFT	20	/* pm_request_l1 time > @
						 * ->L0s not L1 */
#define PM_CTRL_PM_REQ_TO_DEF		0xF
#define PMCTRL_TXL1_AFTER_L0S		BIT(19)	/* l1dv2.0+ */
#define L1D_PMCTRL_L1_ENTRY_TM_MASK	7UL	/* l1dv2.0+, 3bits */
#define L1D_PMCTRL_L1_ENTRY_TM_SHIFT	16
#define L1D_PMCTRL_L1_ENTRY_TM_DIS	0
#define L1D_PMCTRL_L1_ENTRY_TM_2US	1
#define L1D_PMCTRL_L1_ENTRY_TM_4US	2
#define L1D_PMCTRL_L1_ENTRY_TM_8US	3
#define L1D_PMCTRL_L1_ENTRY_TM_16US	4
#define L1D_PMCTRL_L1_ENTRY_TM_24US	5
#define L1D_PMCTRL_L1_ENTRY_TM_32US	6
#define L1D_PMCTRL_L1_ENTRY_TM_63US	7
#define PM_CTRL_L1_ENTRY_TIMER_MASK	0xFUL  /* l1C 4bits */
#define PM_CTRL_L1_ENTRY_TIMER_SHIFT	16
#define L2CB1_PM_CTRL_L1_ENTRY_TM	7
#define L1C_PM_CTRL_L1_ENTRY_TM		0xF
#define PM_CTRL_RCVR_WT_TIMER		BIT(15)	/* 1:1us, 0:2ms */
#define PM_CTRL_CLK_PWM_VER1_1		BIT(14)	/* 0:1.0a,1:1.1 */
#define PM_CTRL_CLK_SWH_L1		BIT(13)	/* en pcie clk sw in L1 */
#define PM_CTRL_ASPM_L0S_EN		BIT(12)
#define PM_CTRL_RXL1_AFTER_L0S		BIT(11)	/* l1dv2.0+ */
#define L1D_PMCTRL_L0S_TIMER_MASK	7UL	/* l1d2.0+, 3bits*/
#define L1D_PMCTRL_L0S_TIMER_SHIFT	8
#define PM_CTRL_L0S_ENTRY_TIMER_MASK	0xFUL	/* l1c, 4bits */
#define PM_CTRL_L0S_ENTRY_TIMER_SHIFT	8
#define PM_CTRL_SERDES_BUFS_RX_L1_EN	BIT(7)
#define PM_CTRL_SERDES_PD_EX_L1		BIT(6)	/* power down serdes rx */
#define PM_CTRL_SERDES_PLL_L1_EN	BIT(5)
#define PM_CTRL_SERDES_L1_EN		BIT(4)
#define PM_CTRL_ASPM_L1_EN		BIT(3)
#define PM_CTRL_CLK_REQ_EN		BIT(2)
#define PM_CTRL_RBER_EN			BIT(1)
#define PM_CTRL_SPRSDWER_EN		BIT(0)

#define REG_LTSSM_ID_CTRL		0x12FC
#define LTSSM_ID_EN_WRO			0x1000


/* Selene Master Control Register */
#define REG_MASTER_CTRL			0x1400
#define MASTER_CTRL_OTP_SEL		BIT(31)
#define MASTER_DEV_NUM_MASK		0x7FUL
#define MASTER_DEV_NUM_SHIFT		24
#define MASTER_REV_NUM_MASK		0xFFUL
#define MASTER_REV_NUM_SHIFT		16
#define MASTER_CTRL_INT_RDCLR		BIT(14)
#define MASTER_CTRL_CLK_SEL_DIS		BIT(12)	/* 1:alwys sel pclk from
						 * serdes, not sw to 25M */
#define MASTER_CTRL_RX_ITIMER_EN	BIT(11)	/* IRQ MODURATION FOR RX */
#define MASTER_CTRL_TX_ITIMER_EN	BIT(10)	/* MODURATION FOR TX/RX */
#define MASTER_CTRL_MANU_INT		BIT(9)	/* SOFT MANUAL INT */
#define MASTER_CTRL_MANUTIMER_EN	BIT(8)
#define MASTER_CTRL_SA_TIMER_EN		BIT(7)	/* SYS ALIVE TIMER EN */
#define MASTER_CTRL_OOB_DIS		BIT(6)	/* OUT OF BOX DIS */
#define MASTER_CTRL_WAKEN_25M		BIT(5)	/* WAKE WO. PCIE CLK */
#define MASTER_CTRL_BERT_START		BIT(4)
#define MASTER_PCIE_TSTMOD_MASK		3UL
#define MASTER_PCIE_TSTMOD_SHIFT	2
#define MASTER_PCIE_RST			BIT(1)
#define MASTER_CTRL_SOFT_RST		BIT(0)	/* RST MAC & DMA */
#define DMA_MAC_RST_TO			50

/* Timer Initial Value Register */
#define REG_MANUAL_TIMER_INIT       	0x1404

/* IRQ ModeratorTimer Initial Value Register */
#define REG_IRQ_MODRT_TIMER_INIT     	0x1408
#define IRQ_MODRT_TIMER_MASK		0xffff
#define IRQ_MODRT_TX_TIMER_SHIFT    	0
#define IRQ_MODRT_RX_TIMER_SHIFT	16

#define REG_GPHY_CTRL               	0x140C
#define GPHY_CTRL_ADDR_MASK		0x1FUL
#define GPHY_CTRL_ADDR_SHIFT		19
#define GPHY_CTRL_BP_VLTGSW		BIT(18)
#define GPHY_CTRL_100AB_EN		BIT(17)
#define GPHY_CTRL_10AB_EN		BIT(16)
#define GPHY_CTRL_PHY_PLL_BYPASS	BIT(15)
#define GPHY_CTRL_PWDOWN_HW		BIT(14)	/* affect MAC&PHY, to low pw */
#define GPHY_CTRL_PHY_PLL_ON		BIT(13)	/* 1:pll always on, 0:can sw */
#define GPHY_CTRL_SEL_ANA_RST		BIT(12)
#define GPHY_CTRL_HIB_PULSE		BIT(11)
#define GPHY_CTRL_HIB_EN		BIT(10)
#define GPHY_CTRL_GIGA_DIS		BIT(9)
#define GPHY_CTRL_PHY_IDDQ_DIS		BIT(8)	/* pw on RST */
#define GPHY_CTRL_PHY_IDDQ		BIT(7)	/* bit8 affect bit7 while rb */
#define GPHY_CTRL_LPW_EXIT		BIT(6)
#define GPHY_CTRL_GATE_25M_EN		BIT(5)
#define GPHY_CTRL_REV_ANEG		BIT(4)
#define GPHY_CTRL_ANEG_NOW		BIT(3)
#define GPHY_CTRL_LED_MODE		BIT(2)
#define GPHY_CTRL_RTL_MODE		BIT(1)
#define GPHY_CTRL_EXT_RESET		BIT(0)	/* 1:out of DSP RST status */
#define GPHY_CTRL_EXT_RST_TO		80	/* 800us atmost */
#define GPHY_CTRL_CLS			(\
	GPHY_CTRL_LED_MODE		|\
	GPHY_CTRL_100AB_EN		|\
	GPHY_CTRL_PHY_PLL_ON)

/* Block IDLE Status Register */
#define REG_IDLE_STATUS			0x1410
#define IDLE_STATUS_SFORCE_MASK		0xFUL
#define IDLE_STATUS_SFORCE_SHIFT	14
#define IDLE_STATUS_CALIB_DONE		BIT(13)
#define IDLE_STATUS_CALIB_RES_MASK	0x1FUL
#define IDLE_STATUS_CALIB_RES_SHIFT	8
#define IDLE_STATUS_CALIBERR_MASK	0xFUL
#define IDLE_STATUS_CALIBERR_SHIFT	4
#define IDLE_STATUS_TXQ_BUSY		BIT(3)
#define IDLE_STATUS_RXQ_BUSY		BIT(2)
#define IDLE_STATUS_TXMAC_BUSY		BIT(1)
#define IDLE_STATUS_RXMAC_BUSY		BIT(0)
#define IDLE_STATUS_MASK		(\
	IDLE_STATUS_TXQ_BUSY		|\
	IDLE_STATUS_RXQ_BUSY		|\
	IDLE_STATUS_TXMAC_BUSY		|\
	IDLE_STATUS_RXMAC_BUSY)

/* MDIO Control Register */
#define REG_MDIO_CTRL           	0x1414
#define MDIO_CTRL_MODE_EXT		BIT(30)
#define MDIO_CTRL_POST_READ		BIT(29)
#define MDIO_CTRL_AP_EN			BIT(28)
#define MDIO_CTRL_BUSY			BIT(27)
#define MDIO_CTRL_CLK_SEL_MASK		0x7UL
#define MDIO_CTRL_CLK_SEL_SHIFT		24
#define MDIO_CTRL_CLK_25_4		0	/* 25MHz divide 4 */
#define MDIO_CTRL_CLK_25_6		2
#define MDIO_CTRL_CLK_25_8		3
#define MDIO_CTRL_CLK_25_10		4
#define MDIO_CTRL_CLK_25_32		5
#define MDIO_CTRL_CLK_25_64		6
#define MDIO_CTRL_CLK_25_128		7
#define MDIO_CTRL_START			BIT(23)
#define MDIO_CTRL_SPRES_PRMBL		BIT(22)
#define MDIO_CTRL_OP_READ		BIT(21)	/* 1:read, 0:write */
#define MDIO_CTRL_REG_MASK		0x1FUL
#define MDIO_CTRL_REG_SHIFT		16
#define MDIO_CTRL_DATA_MASK		0xFFFFUL
#define MDIO_CTRL_DATA_SHIFT		0
#define MDIO_MAX_AC_TO			120	/* 1.2ms timeout for slow clk */

/* for extension reg access */
#define REG_MDIO_EXTN			0x1448
#define MDIO_EXTN_PORTAD_MASK		0x1FUL
#define MDIO_EXTN_PORTAD_SHIFT		21
#define MDIO_EXTN_DEVAD_MASK		0x1FUL
#define MDIO_EXTN_DEVAD_SHIFT		16
#define MDIO_EXTN_REG_MASK		0xFFFFUL
#define MDIO_EXTN_REG_SHIFT		0

/* BIST Control and Status Register0 (for the Packet Memory) */
#define REG_BIST0_CTRL              	0x141c
#define BIST0_NOW                   	0x1
#define BIST0_SRAM_FAIL             	0x2 /* 1: The SRAM failure is
					     * un-repairable  because
					     * it has address decoder
					     * failure or more than 1 cell
					     * stuck-to-x failure */
#define BIST0_FUSE_FLAG             	0x4

/* BIST Control and Status Register1(for the retry buffer of PCI Express) */
#define REG_BIST1_CTRL			0x1420
#define BIST1_NOW                   	0x1
#define BIST1_SRAM_FAIL             	0x2
#define BIST1_FUSE_FLAG             	0x4

/* SerDes Lock Detect Control and Status Register */
#define REG_SERDES			0x1424
#define SERDES_PHY_CLK_SLOWDOWN		BIT(18)
#define SERDES_MAC_CLK_SLOWDOWN		BIT(17)
#define SERDES_SELFB_PLL_MASK		0x3UL
#define SERDES_SELFB_PLL_SHIFT		14
#define SERDES_PHYCLK_SEL_GTX		BIT(13)	/* 1:gtx_clk, 0:25M */
#define SERDES_PCIECLK_SEL_SRDS		BIT(12)	/* 1:serdes,0:25M */
#define SERDES_BUFS_RX_EN		BIT(11)
#define SERDES_PD_RX			BIT(10)
#define SERDES_PLL_EN			BIT(9)
#define SERDES_EN			BIT(8)
#define SERDES_SELFB_PLL_SEL_CSR	BIT(6)	/* 0:state-machine,1:csr */
#define SERDES_SELFB_PLL_CSR_MASK	0x3UL
#define SERDES_SELFB_PLL_CSR_SHIFT	4
#define SERDES_SELFB_PLL_CSR_4		3	/* 4-12% OV-CLK */
#define SERDES_SELFB_PLL_CSR_0		2	/* 0-4% OV-CLK */
#define SERDES_SELFB_PLL_CSR_12		1	/* 12-18% OV-CLK */
#define SERDES_SELFB_PLL_CSR_18		0	/* 18-25% OV-CLK */
#define SERDES_VCO_SLOW			BIT(3)
#define SERDES_VCO_FAST			BIT(2)
#define SERDES_LOCK_DETECT_EN		BIT(1)
#define SERDES_LOCK_DETECT		BIT(0)

#define REG_LPI_DECISN_TIMER            0x143C
#define L2CB_LPI_DESISN_TIMER		0x7D00

#define REG_LPI_CTRL                    0x1440
#define LPI_CTRL_CHK_DA			BIT(31)
#define LPI_CTRL_ENH_TO_MASK		0x1FFFUL
#define LPI_CTRL_ENH_TO_SHIFT		12
#define LPI_CTRL_ENH_TH_MASK		0x1FUL
#define LPI_CTRL_ENH_TH_SHIFT		6
#define LPI_CTRL_ENH_EN			BIT(5)
#define LPI_CTRL_CHK_RX			BIT(4)
#define LPI_CTRL_CHK_STATE		BIT(3)
#define LPI_CTRL_GMII			BIT(2)
#define LPI_CTRL_TO_PHY			BIT(1)
#define LPI_CTRL_EN			BIT(0)

#define REG_LPI_WAIT			0x1444
#define LPI_WAIT_TIMER_MASK		0xFFFFUL
#define LPI_WAIT_TIMER_SHIFT		0

/* MAC Control Register  */
#define REG_MAC_CTRL         		0x1480
#define MAC_CTRL_SPEED_MODE_SW		BIT(30) /* 0:phy,1:sw */
#define MAC_CTRL_HASH_ALG_CRC32		BIT(29) /* 1:legacy,0:lw_5b */
#define MAC_CTRL_SINGLE_PAUSE_EN	BIT(28)
#define MAC_CTRL_DBG			BIT(27)
#define MAC_CTRL_BC_EN			BIT(26)
#define MAC_CTRL_MC_ALL_EN		BIT(25)
#define MAC_CTRL_RX_CHKSUM_EN		BIT(24)
#define MAC_CTRL_TX_HUGE		BIT(23)
#define MAC_CTRL_DBG_TX_BKPRESURE	BIT(22)
#define MAC_CTRL_SPEED_MASK		3UL
#define MAC_CTRL_SPEED_SHIFT		20
#define MAC_CTRL_SPEED_10_100		1
#define MAC_CTRL_SPEED_1000		2
#define MAC_CTRL_TX_SIMURST		BIT(19)
#define MAC_CTRL_SCNT			BIT(17)
#define MAC_CTRL_TX_PAUSE		BIT(16)
#define MAC_CTRL_PROMIS_EN		BIT(15)
#define MAC_CTRL_RMV_VLAN		BIT(14)
#define MAC_CTRL_PRMLEN_MASK		0xFUL
#define MAC_CTRL_PRMLEN_SHIFT		10
#define MAC_CTRL_HUGE_EN		BIT(9)
#define MAC_CTRL_LENCHK			BIT(8)
#define MAC_CTRL_PAD			BIT(7)
#define MAC_CTRL_ADD_CRC		BIT(6)
#define MAC_CTRL_DUPLX			BIT(5)
#define MAC_CTRL_LOOPBACK		BIT(4)
#define MAC_CTRL_RX_FLOW		BIT(3)
#define MAC_CTRL_TX_FLOW		BIT(2)
#define MAC_CTRL_RX_EN			BIT(1)
#define MAC_CTRL_TX_EN			BIT(0)

/* MAC IPG/IFG Control Register  */
#define REG_MAC_IPG_IFG             	0x1484
#define MAC_IPG_IFG_IPGT_SHIFT      	0 	/* Desired back to back
						 * inter-packet gap. The
						 * default is 96-bit time */
#define MAC_IPG_IFG_IPGT_MASK       	0x7f
#define MAC_IPG_IFG_MIFG_SHIFT      	8       /* Minimum number of IFG to
						 * enforce in between RX frames */
#define MAC_IPG_IFG_MIFG_MASK       	0xff  	/* Frame gap below such IFP is dropped */
#define MAC_IPG_IFG_IPGR1_SHIFT     	16   	/* 64bit Carrier-Sense window */
#define MAC_IPG_IFG_IPGR1_MASK      	0x7f
#define MAC_IPG_IFG_IPGR2_SHIFT     	24    	/* 96-bit IPG window */
#define MAC_IPG_IFG_IPGR2_MASK      	0x7f

/* MAC STATION ADDRESS  */
#define REG_MAC_STA_ADDR		0x1488

/* Hash table for multicast address */
#define REG_RX_HASH_TABLE		0x1490

/* MAC Half-Duplex Control Register */
#define REG_MAC_HALF_DUPLX_CTRL     	0x1498
#define MAC_HALF_DUPLX_CTRL_LCOL_SHIFT  0      /* Collision Window */
#define MAC_HALF_DUPLX_CTRL_LCOL_MASK   0x3ff
#define MAC_HALF_DUPLX_CTRL_RETRY_SHIFT 12
#define MAC_HALF_DUPLX_CTRL_RETRY_MASK  0xf
#define MAC_HALF_DUPLX_CTRL_EXC_DEF_EN  0x10000
#define MAC_HALF_DUPLX_CTRL_NO_BACK_C   0x20000
#define MAC_HALF_DUPLX_CTRL_NO_BACK_P   0x40000 /* No back-off on backpressure,
						 * immediately start the
						 * transmission after back pressure */
#define MAC_HALF_DUPLX_CTRL_ABEBE        0x80000 /* 1: Alternative Binary Exponential Back-off Enabled */
#define MAC_HALF_DUPLX_CTRL_ABEBT_SHIFT  20      /* Maximum binary exponential number */
#define MAC_HALF_DUPLX_CTRL_ABEBT_MASK   0xf
#define MAC_HALF_DUPLX_CTRL_JAMIPG_SHIFT 24      /* IPG to start JAM for collision based flow control in half-duplex */
#define MAC_HALF_DUPLX_CTRL_JAMIPG_MASK  0xf     /* mode. In unit of 8-bit time */

/* Maximum Frame Length Control Register   */
#define REG_MTU                     	0x149c

/* Wake-On-Lan control register */
#define REG_WOL_CTRL                	0x14a0
#define WOL_PT7_MATCH			BIT(31)
#define WOL_PT6_MATCH			BIT(30)
#define WOL_PT5_MATCH			BIT(29)
#define WOL_PT4_MATCH			BIT(28)
#define WOL_PT3_MATCH			BIT(27)
#define WOL_PT2_MATCH			BIT(26)
#define WOL_PT1_MATCH			BIT(25)
#define WOL_PT0_MATCH			BIT(24)
#define WOL_PT7_EN			BIT(23)
#define WOL_PT6_EN			BIT(22)
#define WOL_PT5_EN			BIT(21)
#define WOL_PT4_EN			BIT(20)
#define WOL_PT3_EN			BIT(19)
#define WOL_PT2_EN			BIT(18)
#define WOL_PT1_EN			BIT(17)
#define WOL_PT0_EN			BIT(16)
#define WOL_LNKCHG_ST			BIT(10)
#define WOL_MAGIC_ST			BIT(9)
#define WOL_PATTERN_ST			BIT(8)
#define WOL_OOB_EN			BIT(6)
#define WOL_LINK_CHG_PME_EN		BIT(5)
#define WOL_LINK_CHG_EN			BIT(4)
#define WOL_MAGIC_PME_EN		BIT(3)
#define WOL_MAGIC_EN			BIT(2)
#define WOL_PATTERN_PME_EN		BIT(1)
#define WOL_PATTERN_EN			BIT(0)

/* WOL Length ( 2 DWORD ) */
#define REG_WOL_PTLEN1			0x14A4
#define WOL_PTLEN1_3_MASK		0xFFUL
#define WOL_PTLEN1_3_SHIFT		24
#define WOL_PTLEN1_2_MASK		0xFFUL
#define WOL_PTLEN1_2_SHIFT		16
#define WOL_PTLEN1_1_MASK		0xFFUL
#define WOL_PTLEN1_1_SHIFT		8
#define WOL_PTLEN1_0_MASK		0xFFUL
#define WOL_PTLEN1_0_SHIFT		0

#define REG_WOL_PTLEN2			0x14A8
#define WOL_PTLEN2_7_MASK		0xFFUL
#define WOL_PTLEN2_7_SHIFT		24
#define WOL_PTLEN2_6_MASK		0xFFUL
#define WOL_PTLEN2_6_SHIFT		16
#define WOL_PTLEN2_5_MASK		0xFFUL
#define WOL_PTLEN2_5_SHIFT		8
#define WOL_PTLEN2_4_MASK		0xFFUL
#define WOL_PTLEN2_4_SHIFT		0

/* Internal SRAM Partition Register */
#define RFDX_HEAD_ADDR_MASK		0x03FF
#define RFDX_HARD_ADDR_SHIFT		0
#define RFDX_TAIL_ADDR_MASK		0x03FF
#define RFDX_TAIL_ADDR_SHIFT            16

#define REG_SRAM_RFD0_INFO		0x1500
#define REG_SRAM_RFD1_INFO		0x1504
#define REG_SRAM_RFD2_INFO		0x1508
#define	REG_SRAM_RFD3_INFO		0x150C

#define REG_RFD_NIC_LEN			0x1510 /* In 8-bytes */
#define RFD_NIC_LEN_MASK		0x03FF

#define REG_SRAM_TRD_ADDR           	0x1518
#define TPD_HEAD_ADDR_MASK		0x03FF
#define TPD_HEAD_ADDR_SHIFT		0
#define TPD_TAIL_ADDR_MASK		0x03FF
#define TPD_TAIL_ADDR_SHIFT		16

#define REG_SRAM_TRD_LEN            	0x151C /* In 8-bytes */
#define TPD_NIC_LEN_MASK		0x03FF

#define REG_SRAM_RXF_ADDR          	0x1520
#define REG_SRAM_RXF_LEN            	0x1524
#define REG_SRAM_TXF_ADDR           	0x1528
#define REG_SRAM_TXF_LEN            	0x152C
#define REG_SRAM_TCPH_ADDR          	0x1530
#define REG_SRAM_PKTH_ADDR          	0x1532

/*
 * Load Ptr Register
 * Software sets this bit after the initialization of the head and tail */
#define REG_LOAD_PTR                	0x1534

/*
 * addresses of all descriptors, as well as the following descriptor
 * control register, which triggers each function block to load the head
 * pointer to prepare for the operation. This bit is then self-cleared
 * after one cycle.
 */
#define REG_RX_BASE_ADDR_HI		0x1540
#define REG_TX_BASE_ADDR_HI		0x1544
#define REG_RFD0_HEAD_ADDR_LO		0x1550
#define REG_RFD_RING_SIZE		0x1560
#define RFD_RING_SIZE_MASK		0x0FFF
#define REG_RX_BUF_SIZE			0x1564
#define RX_BUF_SIZE_MASK		0xFFFF
#define REG_RRD0_HEAD_ADDR_LO		0x1568
#define REG_RRD_RING_SIZE		0x1578
#define RRD_RING_SIZE_MASK		0x0FFF
#define REG_TPD_PRI1_ADDR_LO		0x157C
#define REG_TPD_PRI0_ADDR_LO		0x1580
#define REG_TPD_RING_SIZE		0x1584
#define TPD_RING_SIZE_MASK		0xFFFF

/* TXQ Control Register */
#define REG_TXQ_CTRL			0x1590
#define TXQ_TXF_BURST_NUM_MASK          0xFFFFUL
#define TXQ_TXF_BURST_NUM_SHIFT		16
#define L1C_TXQ_TXF_BURST_PREF          0x200
#define L2CB_TXQ_TXF_BURST_PREF         0x40
#define TXQ_CTRL_PEDING_CLR             BIT(8)
#define TXQ_CTRL_LS_8023_EN             BIT(7)
#define TXQ_CTRL_ENH_MODE               BIT(6)
#define TXQ_CTRL_EN                     BIT(5)
#define TXQ_CTRL_IP_OPTION_EN           BIT(4)
#define TXQ_NUM_TPD_BURST_MASK          0xFUL
#define TXQ_NUM_TPD_BURST_SHIFT         0
#define TXQ_NUM_TPD_BURST_DEF           5
#define TXQ_CFGV			(\
	FIELDX(TXQ_NUM_TPD_BURST, TXQ_NUM_TPD_BURST_DEF) |\
	TXQ_CTRL_ENH_MODE |\
	TXQ_CTRL_LS_8023_EN |\
	TXQ_CTRL_IP_OPTION_EN)
#define L1C_TXQ_CFGV			(\
	TXQ_CFGV |\
	FIELDX(TXQ_TXF_BURST_NUM, L1C_TXQ_TXF_BURST_PREF))
#define L2CB_TXQ_CFGV			(\
	TXQ_CFGV |\
	FIELDX(TXQ_TXF_BURST_NUM, L2CB_TXQ_TXF_BURST_PREF))


/* Jumbo packet Threshold for task offload */
#define REG_TX_TSO_OFFLOAD_THRESH	0x1594 /* In 8-bytes */
#define TX_TSO_OFFLOAD_THRESH_MASK	0x07FF
#define MAX_TSO_FRAME_SIZE		(7*1024)

#define	REG_TXF_WATER_MARK		0x1598 /* In 8-bytes */
#define TXF_WATER_MARK_MASK		0x0FFF
#define TXF_LOW_WATER_MARK_SHIFT	0
#define TXF_HIGH_WATER_MARK_SHIFT 	16
#define TXQ_CTRL_BURST_MODE_EN		0x80000000

#define REG_THRUPUT_MON_CTRL		0x159C
#define THRUPUT_MON_RATE_MASK		0x3
#define THRUPUT_MON_RATE_SHIFT		0
#define THRUPUT_MON_EN			0x80

/* RXQ Control Register */
#define REG_RXQ_CTRL                	0x15A0
#define ASPM_THRUPUT_LIMIT_MASK		0x3
#define ASPM_THRUPUT_LIMIT_SHIFT	0
#define ASPM_THRUPUT_LIMIT_NO		0x00
#define ASPM_THRUPUT_LIMIT_1M		0x01
#define ASPM_THRUPUT_LIMIT_10M		0x02
#define ASPM_THRUPUT_LIMIT_100M		0x03
#define IPV6_CHKSUM_CTRL_EN		BIT(7)
#define RXQ_RFD_BURST_NUM_MASK		0x003F
#define RXQ_RFD_BURST_NUM_SHIFT		20
#define RXQ_NUM_RFD_PREF_DEF		8
#define RSS_MODE_MASK			3UL
#define RSS_MODE_SHIFT			26
#define RSS_MODE_DIS			0
#define RSS_MODE_SQSI			1
#define RSS_MODE_MQSI			2
#define RSS_MODE_MQMI			3
#define RSS_NIP_QUEUE_SEL		BIT(28) /* 0:q0, 1:table */
#define RRS_HASH_CTRL_EN		BIT(29)
#define RX_CUT_THRU_EN			BIT(30)
#define RXQ_CTRL_EN			BIT(31)

#define REG_RFD_FREE_THRESH		0x15A4
#define RFD_FREE_THRESH_MASK		0x003F
#define RFD_FREE_HI_THRESH_SHIFT	0
#define RFD_FREE_LO_THRESH_SHIFT	6

/* RXF flow control register */
#define REG_RXQ_RXF_PAUSE_THRESH    	0x15A8
#define RXQ_RXF_PAUSE_TH_HI_SHIFT       0
#define RXQ_RXF_PAUSE_TH_HI_MASK        0x0FFF
#define RXQ_RXF_PAUSE_TH_LO_SHIFT       16
#define RXQ_RXF_PAUSE_TH_LO_MASK        0x0FFF

#define REG_RXD_DMA_CTRL		0x15AC
#define RXD_DMA_THRESH_MASK		0x0FFF	/* In 8-bytes */
#define RXD_DMA_THRESH_SHIFT		0
#define RXD_DMA_DOWN_TIMER_MASK		0xFFFF
#define RXD_DMA_DOWN_TIMER_SHIFT	16

/* DMA Engine Control Register */
#define REG_DMA_CTRL			0x15C0
#define DMA_CTRL_SMB_NOW                BIT(31)
#define DMA_CTRL_WPEND_CLR              BIT(30)
#define DMA_CTRL_RPEND_CLR              BIT(29)
#define DMA_CTRL_WDLY_CNT_MASK          0xFUL
#define DMA_CTRL_WDLY_CNT_SHIFT         16
#define DMA_CTRL_WDLY_CNT_DEF           4
#define DMA_CTRL_RDLY_CNT_MASK          0x1FUL
#define DMA_CTRL_RDLY_CNT_SHIFT         11
#define DMA_CTRL_RDLY_CNT_DEF           15
#define DMA_CTRL_RREQ_PRI_DATA          BIT(10)      /* 0:tpd, 1:data */
#define DMA_CTRL_WREQ_BLEN_MASK         7UL
#define DMA_CTRL_WREQ_BLEN_SHIFT        7
#define DMA_CTRL_RREQ_BLEN_MASK         7UL
#define DMA_CTRL_RREQ_BLEN_SHIFT        4
#define L1C_CTRL_DMA_RCB_LEN128         BIT(3)   /* 0:64bytes,1:128bytes */
#define DMA_CTRL_RORDER_MODE_MASK       7UL
#define DMA_CTRL_RORDER_MODE_SHIFT      0
#define DMA_CTRL_RORDER_MODE_OUT        4
#define DMA_CTRL_RORDER_MODE_ENHANCE    2
#define DMA_CTRL_RORDER_MODE_IN         1

/* INT-triggle/SMB Control Register */
#define REG_SMB_STAT_TIMER		0x15C4	/* 2us resolution */
#define SMB_STAT_TIMER_MASK		0xFFFFFF
#define REG_TINT_TPD_THRESH             0x15C8 /* tpd th to trig intrrupt */

/* Mail box */
#define MB_RFDX_PROD_IDX_MASK		0xFFFF
#define REG_MB_RFD0_PROD_IDX		0x15E0

#define REG_TPD_PRI1_PIDX               0x15F0	/* 16bit,hi-tpd producer idx */
#define REG_TPD_PRI0_PIDX		0x15F2	/* 16bit,lo-tpd producer idx */
#define REG_TPD_PRI1_CIDX		0x15F4	/* 16bit,hi-tpd consumer idx */
#define REG_TPD_PRI0_CIDX		0x15F6	/* 16bit,lo-tpd consumer idx */

#define REG_MB_RFD01_CONS_IDX		0x15F8
#define MB_RFD0_CONS_IDX_MASK		0x0000FFFF
#define MB_RFD1_CONS_IDX_MASK		0xFFFF0000

/* Interrupt Status Register */
#define REG_ISR    			0x1600
#define ISR_SMB				0x00000001
#define ISR_TIMER			0x00000002
/*
 * Software manual interrupt, for debug. Set when SW_MAN_INT_EN is set
 * in Table 51 Selene Master Control Register (Offset 0x1400).
 */
#define ISR_MANUAL         		0x00000004
#define ISR_HW_RXF_OV          		0x00000008 /* RXF overflow interrupt */
#define ISR_RFD0_UR			0x00000010 /* RFD0 under run */
#define ISR_RFD1_UR			0x00000020
#define ISR_RFD2_UR			0x00000040
#define ISR_RFD3_UR			0x00000080
#define ISR_TXF_UR			0x00000100
#define ISR_DMAR_TO_RST			0x00000200
#define ISR_DMAW_TO_RST			0x00000400
#define ISR_TX_CREDIT			0x00000800
#define ISR_GPHY			0x00001000
/* GPHY low power state interrupt */
#define ISR_GPHY_LPW           		0x00002000
#define ISR_TXQ_TO_RST			0x00004000
#define ISR_TX_PKT			0x00008000
#define ISR_RX_PKT_0			0x00010000
#define ISR_RX_PKT_1			0x00020000
#define ISR_RX_PKT_2			0x00040000
#define ISR_RX_PKT_3			0x00080000
#define ISR_MAC_RX			0x00100000
#define ISR_MAC_TX			0x00200000
#define ISR_UR_DETECTED			0x00400000
#define ISR_FERR_DETECTED		0x00800000
#define ISR_NFERR_DETECTED		0x01000000
#define ISR_CERR_DETECTED		0x02000000
#define ISR_PHY_LINKDOWN		0x04000000
#define ISR_DIS_INT			0x80000000

/* Interrupt Mask Register */
#define REG_IMR				0x1604

#define IMR_NORMAL_MASK		(\
		ISR_MANUAL	|\
		ISR_HW_RXF_OV	|\
		ISR_RFD0_UR	|\
		ISR_TXF_UR	|\
		ISR_DMAR_TO_RST	|\
		ISR_TXQ_TO_RST  |\
		ISR_DMAW_TO_RST	|\
		ISR_GPHY	|\
		ISR_TX_PKT	|\
		ISR_RX_PKT_0	|\
		ISR_GPHY_LPW    |\
		ISR_PHY_LINKDOWN)

#define ISR_RX_PKT 	(\
	ISR_RX_PKT_0    |\
	ISR_RX_PKT_1    |\
	ISR_RX_PKT_2    |\
	ISR_RX_PKT_3)

#define ISR_OVER	(\
	ISR_RFD0_UR 	|\
	ISR_RFD1_UR	|\
	ISR_RFD2_UR	|\
	ISR_RFD3_UR	|\
	ISR_HW_RXF_OV	|\
	ISR_TXF_UR)

#define ISR_ERROR	(\
	ISR_DMAR_TO_RST	|\
	ISR_TXQ_TO_RST  |\
	ISR_DMAW_TO_RST	|\
	ISR_PHY_LINKDOWN)

#define REG_INT_RETRIG_TIMER		0x1608
#define INT_RETRIG_TIMER_MASK		0xFFFF

#define REG_MAC_RX_STATUS_BIN 		0x1700
#define REG_MAC_RX_STATUS_END 		0x175c
#define REG_MAC_TX_STATUS_BIN 		0x1760
#define REG_MAC_TX_STATUS_END 		0x17c0

#define REG_CLK_GATING_CTRL		0x1814
#define CLK_GATING_DMAW_EN		0x0001
#define CLK_GATING_DMAR_EN		0x0002
#define CLK_GATING_TXQ_EN		0x0004
#define CLK_GATING_RXQ_EN		0x0008
#define CLK_GATING_TXMAC_EN		0x0010
#define CLK_GATING_RXMAC_EN		0x0020

#define CLK_GATING_EN_ALL	(CLK_GATING_DMAW_EN |\
				 CLK_GATING_DMAR_EN |\
				 CLK_GATING_TXQ_EN  |\
				 CLK_GATING_RXQ_EN  |\
				 CLK_GATING_TXMAC_EN|\
				 CLK_GATING_RXMAC_EN)

/* DEBUG ADDR */
#define REG_DEBUG_DATA0 		0x1900
#define REG_DEBUG_DATA1 		0x1904

#define L1D_MPW_PHYID1			0xD01C  /* V7 */
#define L1D_MPW_PHYID2			0xD01D  /* V1-V6 */
#define L1D_MPW_PHYID3			0xD01E  /* V8 */


/* Autoneg Advertisement Register */
#define ADVERTISE_DEFAULT_CAP \
	(ADVERTISE_ALL | ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM)

/* 1000BASE-T Control Register */
#define GIGA_CR_1000T_REPEATER_DTE	0x0400  /* 1=Repeater/switch device port 0=DTE device */

#define GIGA_CR_1000T_MS_VALUE		0x0800  /* 1=Configure PHY as Master 0=Configure PHY as Slave */
#define GIGA_CR_1000T_MS_ENABLE		0x1000  /* 1=Master/Slave manual config value 0=Automatic Master/Slave config */
#define GIGA_CR_1000T_TEST_MODE_NORMAL	0x0000  /* Normal Operation */
#define GIGA_CR_1000T_TEST_MODE_1	0x2000  /* Transmit Waveform test */
#define GIGA_CR_1000T_TEST_MODE_2	0x4000  /* Master Transmit Jitter test */
#define GIGA_CR_1000T_TEST_MODE_3	0x6000  /* Slave Transmit Jitter test */
#define GIGA_CR_1000T_TEST_MODE_4	0x8000	/* Transmitter Distortion test */
#define GIGA_CR_1000T_SPEED_MASK	0x0300
#define GIGA_CR_1000T_DEFAULT_CAP	0x0300

/* PHY Specific Status Register */
#define MII_GIGA_PSSR			0x11
#define GIGA_PSSR_SPD_DPLX_RESOLVED	0x0800  /* 1=Speed & Duplex resolved */
#define GIGA_PSSR_DPLX			0x2000  /* 1=Duplex 0=Half Duplex */
#define GIGA_PSSR_SPEED			0xC000  /* Speed, bits 14:15 */
#define GIGA_PSSR_10MBS			0x0000  /* 00=10Mbs */
#define GIGA_PSSR_100MBS		0x4000  /* 01=100Mbs */
#define GIGA_PSSR_1000MBS		0x8000  /* 10=1000Mbs */

/* PHY Interrupt Enable Register */
#define MII_IER				0x12
#define IER_LINK_UP			0x0400
#define IER_LINK_DOWN			0x0800

/* PHY Interrupt Status Register */
#define MII_ISR				0x13
#define ISR_LINK_UP			0x0400
#define ISR_LINK_DOWN			0x0800

/* Cable-Detect-Test Control Register */
#define MII_CDTC			0x16
#define CDTC_EN_OFF			0   /* sc */
#define CDTC_EN_BITS			1
#define CDTC_PAIR_OFF			8
#define CDTC_PAIR_BIT			2

/* Cable-Detect-Test Status Register */
#define MII_CDTS			0x1C
#define CDTS_STATUS_OFF			8
#define CDTS_STATUS_BITS		2
#define CDTS_STATUS_NORMAL		0
#define CDTS_STATUS_SHORT		1
#define CDTS_STATUS_OPEN		2
#define CDTS_STATUS_INVALID		3

#define MII_DBG_ADDR			0x1D
#define MII_DBG_DATA			0x1E

/***************************** debug port *************************************/

#define MIIDBG_ANACTRL                  0x00
#define ANACTRL_CLK125M_DELAY_EN        0x8000
#define ANACTRL_VCO_FAST                0x4000
#define ANACTRL_VCO_SLOW                0x2000
#define ANACTRL_AFE_MODE_EN             0x1000
#define ANACTRL_LCKDET_PHY              0x800
#define ANACTRL_LCKDET_EN               0x400
#define ANACTRL_OEN_125M                0x200
#define ANACTRL_HBIAS_EN                0x100
#define ANACTRL_HB_EN                   0x80
#define ANACTRL_SEL_HSP                 0x40
#define ANACTRL_CLASSA_EN               0x20
#define ANACTRL_MANUSWON_SWR_MASK       3U
#define ANACTRL_MANUSWON_SWR_SHIFT      2
#define ANACTRL_MANUSWON_SWR_2V         0
#define ANACTRL_MANUSWON_SWR_1P9V       1
#define ANACTRL_MANUSWON_SWR_1P8V       2
#define ANACTRL_MANUSWON_SWR_1P7V       3
#define ANACTRL_MANUSWON_BW3_4M         0x2
#define ANACTRL_RESTART_CAL             0x1
#define ANACTRL_DEF                     0x02EF

#define MIIDBG_SYSMODCTRL               0x04
#define SYSMODCTRL_IECHOADJ_PFMH_PHY    0x8000
#define SYSMODCTRL_IECHOADJ_BIASGEN     0x4000
#define SYSMODCTRL_IECHOADJ_PFML_PHY    0x2000
#define SYSMODCTRL_IECHOADJ_PS_MASK     3U
#define SYSMODCTRL_IECHOADJ_PS_SHIFT    10
#define SYSMODCTRL_IECHOADJ_PS_40       3
#define SYSMODCTRL_IECHOADJ_PS_20       2
#define SYSMODCTRL_IECHOADJ_PS_0        1
#define SYSMODCTRL_IECHOADJ_10BT_100MV  0x40 /* 1:100mv, 0:200mv */
#define SYSMODCTRL_IECHOADJ_HLFAP_MASK  3U
#define SYSMODCTRL_IECHOADJ_HLFAP_SHIFT 4
#define SYSMODCTRL_IECHOADJ_VDFULBW     0x8
#define SYSMODCTRL_IECHOADJ_VDBIASHLF   0x4
#define SYSMODCTRL_IECHOADJ_VDAMPHLF    0x2
#define SYSMODCTRL_IECHOADJ_VDLANSW     0x1
#define SYSMODCTRL_IECHOADJ_DEF         0x88BB /* ???? */

/* for l1d & l2cb */
#define SYSMODCTRL_IECHOADJ_CUR_ADD     0x8000
#define SYSMODCTRL_IECHOADJ_CUR_MASK    7U
#define SYSMODCTRL_IECHOADJ_CUR_SHIFT   12
#define SYSMODCTRL_IECHOADJ_VOL_MASK    0xFU
#define SYSMODCTRL_IECHOADJ_VOL_SHIFT   8
#define SYSMODCTRL_IECHOADJ_VOL_17ALL   3
#define SYSMODCTRL_IECHOADJ_VOL_100M15  1
#define SYSMODCTRL_IECHOADJ_VOL_10M17   0
#define SYSMODCTRL_IECHOADJ_BIAS1_MASK  0xFU
#define SYSMODCTRL_IECHOADJ_BIAS1_SHIFT 4
#define SYSMODCTRL_IECHOADJ_BIAS2_MASK  0xFU
#define SYSMODCTRL_IECHOADJ_BIAS2_SHIFT 0
#define L1D_SYSMODCTRL_IECHOADJ_DEF     0x4FBB

#define MIIDBG_SRDSYSMOD                0x05
#define SRDSYSMOD_LCKDET_EN             0x2000
#define SRDSYSMOD_PLL_EN                0x800
#define SRDSYSMOD_SEL_HSP               0x400
#define SRDSYSMOD_HLFTXDR               0x200
#define SRDSYSMOD_TXCLK_DELAY_EN        0x100
#define SRDSYSMOD_TXELECIDLE            0x80
#define SRDSYSMOD_DEEMP_EN              0x40
#define SRDSYSMOD_MS_PAD                0x4
#define SRDSYSMOD_CDR_ADC_VLTG          0x2
#define SRDSYSMOD_CDR_DAC_1MA           0x1
#define SRDSYSMOD_DEF                   0x2C46

#define MIIDBG_CFGLPSPD                 0x0A
#define CFGLPSPD_RSTCNT_MASK            3U
#define CFGLPSPD_RSTCNT_SHIFT           14
#define CFGLPSPD_RSTCNT_CLK125SW        0x2000

#define MIIDBG_HIBNEG                   0x0B
#define HIBNEG_PSHIB_EN                 0x8000
#define HIBNEG_WAKE_BOTH                0x4000
#define HIBNEG_ONOFF_ANACHG_SUDEN       0x2000
#define HIBNEG_HIB_PULSE                0x1000
#define HIBNEG_GATE_25M_EN              0x800
#define HIBNEG_RST_80U                  0x400
#define HIBNEG_RST_TIMER_MASK           3U
#define HIBNEG_RST_TIMER_SHIFT          8
#define HIBNEG_GTX_CLK_DELAY_MASK       3U
#define HIBNEG_GTX_CLK_DELAY_SHIFT      5
#define HIBNEG_BYPSS_BRKTIMER           0x10
#define HIBNEG_DEF                      0xBC40

#define MIIDBG_TST10BTCFG               0x12
#define TST10BTCFG_INTV_TIMER_MASK      3U
#define TST10BTCFG_INTV_TIMER_SHIFT     14
#define TST10BTCFG_TRIGER_TIMER_MASK    3U
#define TST10BTCFG_TRIGER_TIMER_SHIFT   12
#define TST10BTCFG_DIV_MAN_MLT3_EN      0x800
#define TST10BTCFG_OFF_DAC_IDLE         0x400
#define TST10BTCFG_LPBK_DEEP            0x4 /* 1:deep,0:shallow */
#define TST10BTCFG_DEF                  0x4C04

#define MIIDBG_AZ_ANADECT		0x15
#define AZ_ANADECT_10BTRX_TH		0x8000
#define AZ_ANADECT_BOTH_01CHNL		0x4000
#define AZ_ANADECT_INTV_MASK		0x3FU
#define AZ_ANADECT_INTV_SHIFT		8
#define AZ_ANADECT_THRESH_MASK		0xFU
#define AZ_ANADECT_THRESH_SHIFT		4
#define AZ_ANADECT_CHNL_MASK		0xFU
#define AZ_ANADECT_CHNL_SHIFT		0
#define AZ_ANADECT_DEF			0x3220
#define AZ_ANADECT_LONG                 0xb210

#define MIIDBG_MSE16DB			0x18	/* l1d */
#define L1D_MSE16DB_UP			0x05EA
#define L1D_MSE16DB_DOWN		0x02EA

#define MIIDBG_LEGCYPS                  0x29
#define LEGCYPS_EN                      0x8000
#define LEGCYPS_DAC_AMP1000_MASK        7U
#define LEGCYPS_DAC_AMP1000_SHIFT       12
#define LEGCYPS_DAC_AMP100_MASK         7U
#define LEGCYPS_DAC_AMP100_SHIFT        9
#define LEGCYPS_DAC_AMP10_MASK          7U
#define LEGCYPS_DAC_AMP10_SHIFT         6
#define LEGCYPS_UNPLUG_TIMER_MASK       7U
#define LEGCYPS_UNPLUG_TIMER_SHIFT      3
#define LEGCYPS_UNPLUG_DECT_EN          0x4
#define LEGCYPS_ECNC_PS_EN              0x1
#define L1D_LEGCYPS_DEF                 0x129D
#define L1C_LEGCYPS_DEF                 0x36DD

#define MIIDBG_TST100BTCFG              0x36
#define TST100BTCFG_NORMAL_BW_EN        0x8000
#define TST100BTCFG_BADLNK_BYPASS       0x4000
#define TST100BTCFG_SHORTCABL_TH_MASK   0x3FU
#define TST100BTCFG_SHORTCABL_TH_SHIFT  8
#define TST100BTCFG_LITCH_EN            0x80
#define TST100BTCFG_VLT_SW              0x40
#define TST100BTCFG_LONGCABL_TH_MASK    0x3FU
#define TST100BTCFG_LONGCABL_TH_SHIFT   0
#define TST100BTCFG_DEF                 0xE12C

#define MIIDBG_VOLT_CTRL                0x3B	/* only for l2cb 1 & 2 */
#define VOLT_CTRL_CABLE1TH_MASK         0x1FFU
#define VOLT_CTRL_CABLE1TH_SHIFT        7
#define VOLT_CTRL_AMPCTRL_MASK          3U
#define VOLT_CTRL_AMPCTRL_SHIFT         5
#define VOLT_CTRL_SW_BYPASS             0x10
#define VOLT_CTRL_SWLOWEST              0x8
#define VOLT_CTRL_DACAMP10_MASK         7U
#define VOLT_CTRL_DACAMP10_SHIFT        0

#define MIIDBG_CABLE1TH_DET             0x3E
#define CABLE1TH_DET_EN                 0x8000


/******* dev 3 *********/
#define MIIEXT_PCS                      3

#define MIIEXT_CLDCTRL3                 0x8003
#define CLDCTRL3_BP_CABLE1TH_DET_GT     0x8000
#define CLDCTRL3_AZ_DISAMP              0x1000
#define L2CB_CLDCTRL3                   0x4D19
#define L1D_CLDCTRL3                    0xDD19

#define MIIEXT_CLDCTRL6			0x8006
#define CLDCTRL6_CAB_LEN_MASK		0x1FFU
#define CLDCTRL6_CAB_LEN_SHIFT          0
#define CLDCTRL6_CAB_LEN_SHORT          0x50

/********* dev 7 **********/
#define MIIEXT_ANEG                     7

#define MIIEXT_LOCAL_EEEADV             0x3C
#define LOCAL_EEEADV_1000BT             0x4
#define LOCAL_EEEADV_100BT              0x2

#define MIIEXT_REMOTE_EEEADV            0x3D
#define REMOTE_EEEADV_1000BT            0x4
#define REMOTE_EEEADV_100BT             0x2

#define MIIEXT_EEE_ANEG                 0x8000
#define EEE_ANEG_1000M                  0x4
#define EEE_ANEG_100M                   0x2

#endif /*_ATL1C_HW_H_*/
