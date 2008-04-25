/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2008 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _E1000_HW_H_
#define _E1000_HW_H_

#include <linux/types.h>

struct e1000_hw;
struct e1000_adapter;

#include "defines.h"

#define er32(reg)	__er32(hw, E1000_##reg)
#define ew32(reg,val)	__ew32(hw, E1000_##reg, (val))
#define e1e_flush()	er32(STATUS)

#define E1000_WRITE_REG_ARRAY(a, reg, offset, value) \
	(writel((value), ((a)->hw_addr + reg + ((offset) << 2))))

#define E1000_READ_REG_ARRAY(a, reg, offset) \
	(readl((a)->hw_addr + reg + ((offset) << 2)))

enum e1e_registers {
	E1000_CTRL     = 0x00000, /* Device Control - RW */
	E1000_STATUS   = 0x00008, /* Device Status - RO */
	E1000_EECD     = 0x00010, /* EEPROM/Flash Control - RW */
	E1000_EERD     = 0x00014, /* EEPROM Read - RW */
	E1000_CTRL_EXT = 0x00018, /* Extended Device Control - RW */
	E1000_FLA      = 0x0001C, /* Flash Access - RW */
	E1000_MDIC     = 0x00020, /* MDI Control - RW */
	E1000_SCTL     = 0x00024, /* SerDes Control - RW */
	E1000_FCAL     = 0x00028, /* Flow Control Address Low - RW */
	E1000_FCAH     = 0x0002C, /* Flow Control Address High -RW */
	E1000_FEXTNVM  = 0x00028, /* Future Extended NVM - RW */
	E1000_FCT      = 0x00030, /* Flow Control Type - RW */
	E1000_VET      = 0x00038, /* VLAN Ether Type - RW */
	E1000_ICR      = 0x000C0, /* Interrupt Cause Read - R/clr */
	E1000_ITR      = 0x000C4, /* Interrupt Throttling Rate - RW */
	E1000_ICS      = 0x000C8, /* Interrupt Cause Set - WO */
	E1000_IMS      = 0x000D0, /* Interrupt Mask Set - RW */
	E1000_IMC      = 0x000D8, /* Interrupt Mask Clear - WO */
	E1000_IAM      = 0x000E0, /* Interrupt Acknowledge Auto Mask */
	E1000_RCTL     = 0x00100, /* Rx Control - RW */
	E1000_FCTTV    = 0x00170, /* Flow Control Transmit Timer Value - RW */
	E1000_TXCW     = 0x00178, /* Tx Configuration Word - RW */
	E1000_RXCW     = 0x00180, /* Rx Configuration Word - RO */
	E1000_TCTL     = 0x00400, /* Tx Control - RW */
	E1000_TCTL_EXT = 0x00404, /* Extended Tx Control - RW */
	E1000_TIPG     = 0x00410, /* Tx Inter-packet gap -RW */
	E1000_AIT      = 0x00458, /* Adaptive Interframe Spacing Throttle -RW */
	E1000_LEDCTL   = 0x00E00, /* LED Control - RW */
	E1000_EXTCNF_CTRL  = 0x00F00, /* Extended Configuration Control */
	E1000_EXTCNF_SIZE  = 0x00F08, /* Extended Configuration Size */
	E1000_PHY_CTRL     = 0x00F10, /* PHY Control Register in CSR */
	E1000_PBA      = 0x01000, /* Packet Buffer Allocation - RW */
	E1000_PBS      = 0x01008, /* Packet Buffer Size */
	E1000_EEMNGCTL = 0x01010, /* MNG EEprom Control */
	E1000_EEWR     = 0x0102C, /* EEPROM Write Register - RW */
	E1000_FLOP     = 0x0103C, /* FLASH Opcode Register */
	E1000_ERT      = 0x02008, /* Early Rx Threshold - RW */
	E1000_FCRTL    = 0x02160, /* Flow Control Receive Threshold Low - RW */
	E1000_FCRTH    = 0x02168, /* Flow Control Receive Threshold High - RW */
	E1000_PSRCTL   = 0x02170, /* Packet Split Receive Control - RW */
	E1000_RDBAL    = 0x02800, /* Rx Descriptor Base Address Low - RW */
	E1000_RDBAH    = 0x02804, /* Rx Descriptor Base Address High - RW */
	E1000_RDLEN    = 0x02808, /* Rx Descriptor Length - RW */
	E1000_RDH      = 0x02810, /* Rx Descriptor Head - RW */
	E1000_RDT      = 0x02818, /* Rx Descriptor Tail - RW */
	E1000_RDTR     = 0x02820, /* Rx Delay Timer - RW */
	E1000_RXDCTL_BASE = 0x02828, /* Rx Descriptor Control - RW */
#define E1000_RXDCTL(_n)   (E1000_RXDCTL_BASE + (_n << 8))
	E1000_RADV     = 0x0282C, /* RX Interrupt Absolute Delay Timer - RW */

/* Convenience macros
 *
 * Note: "_n" is the queue number of the register to be written to.
 *
 * Example usage:
 * E1000_RDBAL_REG(current_rx_queue)
 *
 */
#define E1000_RDBAL_REG(_n)   (E1000_RDBAL + (_n << 8))
	E1000_KABGTXD  = 0x03004, /* AFE Band Gap Transmit Ref Data */
	E1000_TDBAL    = 0x03800, /* Tx Descriptor Base Address Low - RW */
	E1000_TDBAH    = 0x03804, /* Tx Descriptor Base Address High - RW */
	E1000_TDLEN    = 0x03808, /* Tx Descriptor Length - RW */
	E1000_TDH      = 0x03810, /* Tx Descriptor Head - RW */
	E1000_TDT      = 0x03818, /* Tx Descriptor Tail - RW */
	E1000_TIDV     = 0x03820, /* Tx Interrupt Delay Value - RW */
	E1000_TXDCTL_BASE = 0x03828, /* Tx Descriptor Control - RW */
#define E1000_TXDCTL(_n)   (E1000_TXDCTL_BASE + (_n << 8))
	E1000_TADV     = 0x0382C, /* Tx Interrupt Absolute Delay Val - RW */
	E1000_TARC_BASE = 0x03840, /* Tx Arbitration Count (0) */
#define E1000_TARC(_n)   (E1000_TARC_BASE + (_n << 8))
	E1000_CRCERRS  = 0x04000, /* CRC Error Count - R/clr */
	E1000_ALGNERRC = 0x04004, /* Alignment Error Count - R/clr */
	E1000_SYMERRS  = 0x04008, /* Symbol Error Count - R/clr */
	E1000_RXERRC   = 0x0400C, /* Receive Error Count - R/clr */
	E1000_MPC      = 0x04010, /* Missed Packet Count - R/clr */
	E1000_SCC      = 0x04014, /* Single Collision Count - R/clr */
	E1000_ECOL     = 0x04018, /* Excessive Collision Count - R/clr */
	E1000_MCC      = 0x0401C, /* Multiple Collision Count - R/clr */
	E1000_LATECOL  = 0x04020, /* Late Collision Count - R/clr */
	E1000_COLC     = 0x04028, /* Collision Count - R/clr */
	E1000_DC       = 0x04030, /* Defer Count - R/clr */
	E1000_TNCRS    = 0x04034, /* Tx-No CRS - R/clr */
	E1000_SEC      = 0x04038, /* Sequence Error Count - R/clr */
	E1000_CEXTERR  = 0x0403C, /* Carrier Extension Error Count - R/clr */
	E1000_RLEC     = 0x04040, /* Receive Length Error Count - R/clr */
	E1000_XONRXC   = 0x04048, /* XON Rx Count - R/clr */
	E1000_XONTXC   = 0x0404C, /* XON Tx Count - R/clr */
	E1000_XOFFRXC  = 0x04050, /* XOFF Rx Count - R/clr */
	E1000_XOFFTXC  = 0x04054, /* XOFF Tx Count - R/clr */
	E1000_FCRUC    = 0x04058, /* Flow Control Rx Unsupported Count- R/clr */
	E1000_PRC64    = 0x0405C, /* Packets Rx (64 bytes) - R/clr */
	E1000_PRC127   = 0x04060, /* Packets Rx (65-127 bytes) - R/clr */
	E1000_PRC255   = 0x04064, /* Packets Rx (128-255 bytes) - R/clr */
	E1000_PRC511   = 0x04068, /* Packets Rx (255-511 bytes) - R/clr */
	E1000_PRC1023  = 0x0406C, /* Packets Rx (512-1023 bytes) - R/clr */
	E1000_PRC1522  = 0x04070, /* Packets Rx (1024-1522 bytes) - R/clr */
	E1000_GPRC     = 0x04074, /* Good Packets Rx Count - R/clr */
	E1000_BPRC     = 0x04078, /* Broadcast Packets Rx Count - R/clr */
	E1000_MPRC     = 0x0407C, /* Multicast Packets Rx Count - R/clr */
	E1000_GPTC     = 0x04080, /* Good Packets Tx Count - R/clr */
	E1000_GORCL    = 0x04088, /* Good Octets Rx Count Low - R/clr */
	E1000_GORCH    = 0x0408C, /* Good Octets Rx Count High - R/clr */
	E1000_GOTCL    = 0x04090, /* Good Octets Tx Count Low - R/clr */
	E1000_GOTCH    = 0x04094, /* Good Octets Tx Count High - R/clr */
	E1000_RNBC     = 0x040A0, /* Rx No Buffers Count - R/clr */
	E1000_RUC      = 0x040A4, /* Rx Undersize Count - R/clr */
	E1000_RFC      = 0x040A8, /* Rx Fragment Count - R/clr */
	E1000_ROC      = 0x040AC, /* Rx Oversize Count - R/clr */
	E1000_RJC      = 0x040B0, /* Rx Jabber Count - R/clr */
	E1000_MGTPRC   = 0x040B4, /* Management Packets Rx Count - R/clr */
	E1000_MGTPDC   = 0x040B8, /* Management Packets Dropped Count - R/clr */
	E1000_MGTPTC   = 0x040BC, /* Management Packets Tx Count - R/clr */
	E1000_TORL     = 0x040C0, /* Total Octets Rx Low - R/clr */
	E1000_TORH     = 0x040C4, /* Total Octets Rx High - R/clr */
	E1000_TOTL     = 0x040C8, /* Total Octets Tx Low - R/clr */
	E1000_TOTH     = 0x040CC, /* Total Octets Tx High - R/clr */
	E1000_TPR      = 0x040D0, /* Total Packets Rx - R/clr */
	E1000_TPT      = 0x040D4, /* Total Packets Tx - R/clr */
	E1000_PTC64    = 0x040D8, /* Packets Tx (64 bytes) - R/clr */
	E1000_PTC127   = 0x040DC, /* Packets Tx (65-127 bytes) - R/clr */
	E1000_PTC255   = 0x040E0, /* Packets Tx (128-255 bytes) - R/clr */
	E1000_PTC511   = 0x040E4, /* Packets Tx (256-511 bytes) - R/clr */
	E1000_PTC1023  = 0x040E8, /* Packets Tx (512-1023 bytes) - R/clr */
	E1000_PTC1522  = 0x040EC, /* Packets Tx (1024-1522 Bytes) - R/clr */
	E1000_MPTC     = 0x040F0, /* Multicast Packets Tx Count - R/clr */
	E1000_BPTC     = 0x040F4, /* Broadcast Packets Tx Count - R/clr */
	E1000_TSCTC    = 0x040F8, /* TCP Segmentation Context Tx - R/clr */
	E1000_TSCTFC   = 0x040FC, /* TCP Segmentation Context Tx Fail - R/clr */
	E1000_IAC      = 0x04100, /* Interrupt Assertion Count */
	E1000_ICRXPTC  = 0x04104, /* Irq Cause Rx Packet Timer Expire Count */
	E1000_ICRXATC  = 0x04108, /* Irq Cause Rx Abs Timer Expire Count */
	E1000_ICTXPTC  = 0x0410C, /* Irq Cause Tx Packet Timer Expire Count */
	E1000_ICTXATC  = 0x04110, /* Irq Cause Tx Abs Timer Expire Count */
	E1000_ICTXQEC  = 0x04118, /* Irq Cause Tx Queue Empty Count */
	E1000_ICTXQMTC = 0x0411C, /* Irq Cause Tx Queue MinThreshold Count */
	E1000_ICRXDMTC = 0x04120, /* Irq Cause Rx Desc MinThreshold Count */
	E1000_ICRXOC   = 0x04124, /* Irq Cause Receiver Overrun Count */
	E1000_RXCSUM   = 0x05000, /* Rx Checksum Control - RW */
	E1000_RFCTL    = 0x05008, /* Receive Filter Control */
	E1000_MTA      = 0x05200, /* Multicast Table Array - RW Array */
	E1000_RA       = 0x05400, /* Receive Address - RW Array */
	E1000_VFTA     = 0x05600, /* VLAN Filter Table Array - RW Array */
	E1000_WUC      = 0x05800, /* Wakeup Control - RW */
	E1000_WUFC     = 0x05808, /* Wakeup Filter Control - RW */
	E1000_WUS      = 0x05810, /* Wakeup Status - RO */
	E1000_MANC     = 0x05820, /* Management Control - RW */
	E1000_FFLT     = 0x05F00, /* Flexible Filter Length Table - RW Array */
	E1000_HOST_IF  = 0x08800, /* Host Interface */

	E1000_KMRNCTRLSTA = 0x00034, /* MAC-PHY interface - RW */
	E1000_MANC2H    = 0x05860, /* Management Control To Host - RW */
	E1000_SW_FW_SYNC = 0x05B5C, /* Software-Firmware Synchronization - RW */
	E1000_GCR	= 0x05B00, /* PCI-Ex Control */
	E1000_FACTPS    = 0x05B30, /* Function Active and Power State to MNG */
	E1000_SWSM      = 0x05B50, /* SW Semaphore */
	E1000_FWSM      = 0x05B54, /* FW Semaphore */
	E1000_HICR      = 0x08F00, /* Host Interface Control */
};

/* RSS registers */

/* IGP01E1000 Specific Registers */
#define IGP01E1000_PHY_PORT_CONFIG	0x10 /* Port Config */
#define IGP01E1000_PHY_PORT_STATUS	0x11 /* Status */
#define IGP01E1000_PHY_PORT_CTRL	0x12 /* Control */
#define IGP01E1000_PHY_LINK_HEALTH	0x13 /* PHY Link Health */
#define IGP02E1000_PHY_POWER_MGMT	0x19 /* Power Management */
#define IGP01E1000_PHY_PAGE_SELECT	0x1F /* Page Select */

#define IGP01E1000_PHY_PCS_INIT_REG	0x00B4
#define IGP01E1000_PHY_POLARITY_MASK	0x0078

#define IGP01E1000_PSCR_AUTO_MDIX	0x1000
#define IGP01E1000_PSCR_FORCE_MDI_MDIX	0x2000 /* 0=MDI, 1=MDIX */

#define IGP01E1000_PSCFR_SMART_SPEED	0x0080

#define IGP02E1000_PM_SPD		0x0001 /* Smart Power Down */
#define IGP02E1000_PM_D0_LPLU		0x0002 /* For D0a states */
#define IGP02E1000_PM_D3_LPLU		0x0004 /* For all other states */

#define IGP01E1000_PLHR_SS_DOWNGRADE	0x8000

#define IGP01E1000_PSSR_POLARITY_REVERSED	0x0002
#define IGP01E1000_PSSR_MDIX			0x0008
#define IGP01E1000_PSSR_SPEED_MASK		0xC000
#define IGP01E1000_PSSR_SPEED_1000MBPS		0xC000

#define IGP02E1000_PHY_CHANNEL_NUM		4
#define IGP02E1000_PHY_AGC_A			0x11B1
#define IGP02E1000_PHY_AGC_B			0x12B1
#define IGP02E1000_PHY_AGC_C			0x14B1
#define IGP02E1000_PHY_AGC_D			0x18B1

#define IGP02E1000_AGC_LENGTH_SHIFT	9 /* Course - 15:13, Fine - 12:9 */
#define IGP02E1000_AGC_LENGTH_MASK	0x7F
#define IGP02E1000_AGC_RANGE		15

/* manage.c */
#define E1000_VFTA_ENTRY_SHIFT		5
#define E1000_VFTA_ENTRY_MASK		0x7F
#define E1000_VFTA_ENTRY_BIT_SHIFT_MASK	0x1F

#define E1000_HICR_EN			0x01  /* Enable bit - RO */
/* Driver sets this bit when done to put command in RAM */
#define E1000_HICR_C			0x02
#define E1000_HICR_FW_RESET_ENABLE	0x40
#define E1000_HICR_FW_RESET		0x80

#define E1000_FWSM_MODE_MASK		0xE
#define E1000_FWSM_MODE_SHIFT		1

#define E1000_MNG_IAMT_MODE		0x3
#define E1000_MNG_DHCP_COOKIE_LENGTH	0x10
#define E1000_MNG_DHCP_COOKIE_OFFSET	0x6F0
#define E1000_MNG_DHCP_COMMAND_TIMEOUT	10
#define E1000_MNG_DHCP_TX_PAYLOAD_CMD	64
#define E1000_MNG_DHCP_COOKIE_STATUS_PARSING	0x1
#define E1000_MNG_DHCP_COOKIE_STATUS_VLAN	0x2

/* nvm.c */
#define E1000_STM_OPCODE  0xDB00

#define E1000_KMRNCTRLSTA_OFFSET	0x001F0000
#define E1000_KMRNCTRLSTA_OFFSET_SHIFT	16
#define E1000_KMRNCTRLSTA_REN		0x00200000
#define E1000_KMRNCTRLSTA_DIAG_OFFSET	0x3    /* Kumeran Diagnostic */
#define E1000_KMRNCTRLSTA_DIAG_NELPBK	0x1000 /* Nearend Loopback mode */

#define IFE_PHY_EXTENDED_STATUS_CONTROL	0x10
#define IFE_PHY_SPECIAL_CONTROL		0x11 /* 100BaseTx PHY Special Control */
#define IFE_PHY_SPECIAL_CONTROL_LED	0x1B /* PHY Special and LED Control */
#define IFE_PHY_MDIX_CONTROL		0x1C /* MDI/MDI-X Control */

/* IFE PHY Extended Status Control */
#define IFE_PESC_POLARITY_REVERSED	0x0100

/* IFE PHY Special Control */
#define IFE_PSC_AUTO_POLARITY_DISABLE		0x0010
#define IFE_PSC_FORCE_POLARITY			0x0020

/* IFE PHY Special Control and LED Control */
#define IFE_PSCL_PROBE_MODE		0x0020
#define IFE_PSCL_PROBE_LEDS_OFF		0x0006 /* Force LEDs 0 and 2 off */
#define IFE_PSCL_PROBE_LEDS_ON		0x0007 /* Force LEDs 0 and 2 on */

/* IFE PHY MDIX Control */
#define IFE_PMC_MDIX_STATUS	0x0020 /* 1=MDI-X, 0=MDI */
#define IFE_PMC_FORCE_MDIX	0x0040 /* 1=force MDI-X, 0=force MDI */
#define IFE_PMC_AUTO_MDIX	0x0080 /* 1=enable auto MDI/MDI-X, 0=disable */

#define E1000_CABLE_LENGTH_UNDEFINED	0xFF

#define E1000_DEV_ID_82571EB_COPPER		0x105E
#define E1000_DEV_ID_82571EB_FIBER		0x105F
#define E1000_DEV_ID_82571EB_SERDES		0x1060
#define E1000_DEV_ID_82571EB_QUAD_COPPER	0x10A4
#define E1000_DEV_ID_82571PT_QUAD_COPPER	0x10D5
#define E1000_DEV_ID_82571EB_QUAD_FIBER		0x10A5
#define E1000_DEV_ID_82571EB_QUAD_COPPER_LP	0x10BC
#define E1000_DEV_ID_82571EB_SERDES_DUAL	0x10D9
#define E1000_DEV_ID_82571EB_SERDES_QUAD	0x10DA
#define E1000_DEV_ID_82572EI_COPPER		0x107D
#define E1000_DEV_ID_82572EI_FIBER		0x107E
#define E1000_DEV_ID_82572EI_SERDES		0x107F
#define E1000_DEV_ID_82572EI			0x10B9
#define E1000_DEV_ID_82573E			0x108B
#define E1000_DEV_ID_82573E_IAMT		0x108C
#define E1000_DEV_ID_82573L			0x109A

#define E1000_DEV_ID_80003ES2LAN_COPPER_DPT	0x1096
#define E1000_DEV_ID_80003ES2LAN_SERDES_DPT	0x1098
#define E1000_DEV_ID_80003ES2LAN_COPPER_SPT	0x10BA
#define E1000_DEV_ID_80003ES2LAN_SERDES_SPT	0x10BB

#define E1000_DEV_ID_ICH8_IGP_M_AMT		0x1049
#define E1000_DEV_ID_ICH8_IGP_AMT		0x104A
#define E1000_DEV_ID_ICH8_IGP_C			0x104B
#define E1000_DEV_ID_ICH8_IFE			0x104C
#define E1000_DEV_ID_ICH8_IFE_GT		0x10C4
#define E1000_DEV_ID_ICH8_IFE_G			0x10C5
#define E1000_DEV_ID_ICH8_IGP_M			0x104D
#define E1000_DEV_ID_ICH9_IGP_AMT		0x10BD
#define E1000_DEV_ID_ICH9_IGP_C			0x294C
#define E1000_DEV_ID_ICH9_IFE			0x10C0
#define E1000_DEV_ID_ICH9_IFE_GT		0x10C3
#define E1000_DEV_ID_ICH9_IFE_G			0x10C2

#define E1000_FUNC_1 1

enum e1000_mac_type {
	e1000_82571,
	e1000_82572,
	e1000_82573,
	e1000_80003es2lan,
	e1000_ich8lan,
	e1000_ich9lan,
};

enum e1000_media_type {
	e1000_media_type_unknown = 0,
	e1000_media_type_copper = 1,
	e1000_media_type_fiber = 2,
	e1000_media_type_internal_serdes = 3,
	e1000_num_media_types
};

enum e1000_nvm_type {
	e1000_nvm_unknown = 0,
	e1000_nvm_none,
	e1000_nvm_eeprom_spi,
	e1000_nvm_flash_hw,
	e1000_nvm_flash_sw
};

enum e1000_nvm_override {
	e1000_nvm_override_none = 0,
	e1000_nvm_override_spi_small,
	e1000_nvm_override_spi_large
};

enum e1000_phy_type {
	e1000_phy_unknown = 0,
	e1000_phy_none,
	e1000_phy_m88,
	e1000_phy_igp,
	e1000_phy_igp_2,
	e1000_phy_gg82563,
	e1000_phy_igp_3,
	e1000_phy_ife,
};

enum e1000_bus_width {
	e1000_bus_width_unknown = 0,
	e1000_bus_width_pcie_x1,
	e1000_bus_width_pcie_x2,
	e1000_bus_width_pcie_x4 = 4,
	e1000_bus_width_32,
	e1000_bus_width_64,
	e1000_bus_width_reserved
};

enum e1000_1000t_rx_status {
	e1000_1000t_rx_status_not_ok = 0,
	e1000_1000t_rx_status_ok,
	e1000_1000t_rx_status_undefined = 0xFF
};

enum e1000_rev_polarity{
	e1000_rev_polarity_normal = 0,
	e1000_rev_polarity_reversed,
	e1000_rev_polarity_undefined = 0xFF
};

enum e1000_fc_type {
	e1000_fc_none = 0,
	e1000_fc_rx_pause,
	e1000_fc_tx_pause,
	e1000_fc_full,
	e1000_fc_default = 0xFF
};

enum e1000_ms_type {
	e1000_ms_hw_default = 0,
	e1000_ms_force_master,
	e1000_ms_force_slave,
	e1000_ms_auto
};

enum e1000_smart_speed {
	e1000_smart_speed_default = 0,
	e1000_smart_speed_on,
	e1000_smart_speed_off
};

/* Receive Descriptor */
struct e1000_rx_desc {
	__le64 buffer_addr; /* Address of the descriptor's data buffer */
	__le16 length;      /* Length of data DMAed into data buffer */
	__le16 csum;	/* Packet checksum */
	u8  status;      /* Descriptor status */
	u8  errors;      /* Descriptor Errors */
	__le16 special;
};

/* Receive Descriptor - Extended */
union e1000_rx_desc_extended {
	struct {
		__le64 buffer_addr;
		__le64 reserved;
	} read;
	struct {
		struct {
			__le32 mrq;	      /* Multiple Rx Queues */
			union {
				__le32 rss;	    /* RSS Hash */
				struct {
					__le16 ip_id;  /* IP id */
					__le16 csum;   /* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			__le32 status_error;     /* ext status/error */
			__le16 length;
			__le16 vlan;	     /* VLAN tag */
		} upper;
	} wb;  /* writeback */
};

#define MAX_PS_BUFFERS 4
/* Receive Descriptor - Packet Split */
union e1000_rx_desc_packet_split {
	struct {
		/* one buffer for protocol header(s), three data buffers */
		__le64 buffer_addr[MAX_PS_BUFFERS];
	} read;
	struct {
		struct {
			__le32 mrq;	      /* Multiple Rx Queues */
			union {
				__le32 rss;	      /* RSS Hash */
				struct {
					__le16 ip_id;    /* IP id */
					__le16 csum;     /* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			__le32 status_error;     /* ext status/error */
			__le16 length0;	  /* length of buffer 0 */
			__le16 vlan;	     /* VLAN tag */
		} middle;
		struct {
			__le16 header_status;
			__le16 length[3];	/* length of buffers 1-3 */
		} upper;
		__le64 reserved;
	} wb; /* writeback */
};

/* Transmit Descriptor */
struct e1000_tx_desc {
	__le64 buffer_addr;      /* Address of the descriptor's data buffer */
	union {
		__le32 data;
		struct {
			__le16 length;    /* Data buffer length */
			u8 cso;	/* Checksum offset */
			u8 cmd;	/* Descriptor control */
		} flags;
	} lower;
	union {
		__le32 data;
		struct {
			u8 status;     /* Descriptor status */
			u8 css;	/* Checksum start */
			__le16 special;
		} fields;
	} upper;
};

/* Offload Context Descriptor */
struct e1000_context_desc {
	union {
		__le32 ip_config;
		struct {
			u8 ipcss;      /* IP checksum start */
			u8 ipcso;      /* IP checksum offset */
			__le16 ipcse;     /* IP checksum end */
		} ip_fields;
	} lower_setup;
	union {
		__le32 tcp_config;
		struct {
			u8 tucss;      /* TCP checksum start */
			u8 tucso;      /* TCP checksum offset */
			__le16 tucse;     /* TCP checksum end */
		} tcp_fields;
	} upper_setup;
	__le32 cmd_and_length;
	union {
		__le32 data;
		struct {
			u8 status;     /* Descriptor status */
			u8 hdr_len;    /* Header length */
			__le16 mss;       /* Maximum segment size */
		} fields;
	} tcp_seg_setup;
};

/* Offload data descriptor */
struct e1000_data_desc {
	__le64 buffer_addr;   /* Address of the descriptor's buffer address */
	union {
		__le32 data;
		struct {
			__le16 length;    /* Data buffer length */
			u8 typ_len_ext;
			u8 cmd;
		} flags;
	} lower;
	union {
		__le32 data;
		struct {
			u8 status;     /* Descriptor status */
			u8 popts;      /* Packet Options */
			__le16 special;   /* */
		} fields;
	} upper;
};

/* Statistics counters collected by the MAC */
struct e1000_hw_stats {
	u64 crcerrs;
	u64 algnerrc;
	u64 symerrs;
	u64 rxerrc;
	u64 mpc;
	u64 scc;
	u64 ecol;
	u64 mcc;
	u64 latecol;
	u64 colc;
	u64 dc;
	u64 tncrs;
	u64 sec;
	u64 cexterr;
	u64 rlec;
	u64 xonrxc;
	u64 xontxc;
	u64 xoffrxc;
	u64 xofftxc;
	u64 fcruc;
	u64 prc64;
	u64 prc127;
	u64 prc255;
	u64 prc511;
	u64 prc1023;
	u64 prc1522;
	u64 gprc;
	u64 bprc;
	u64 mprc;
	u64 gptc;
	u64 gorc;
	u64 gotc;
	u64 rnbc;
	u64 ruc;
	u64 rfc;
	u64 roc;
	u64 rjc;
	u64 mgprc;
	u64 mgpdc;
	u64 mgptc;
	u64 tor;
	u64 tot;
	u64 tpr;
	u64 tpt;
	u64 ptc64;
	u64 ptc127;
	u64 ptc255;
	u64 ptc511;
	u64 ptc1023;
	u64 ptc1522;
	u64 mptc;
	u64 bptc;
	u64 tsctc;
	u64 tsctfc;
	u64 iac;
	u64 icrxptc;
	u64 icrxatc;
	u64 ictxptc;
	u64 ictxatc;
	u64 ictxqec;
	u64 ictxqmtc;
	u64 icrxdmtc;
	u64 icrxoc;
};

struct e1000_phy_stats {
	u32 idle_errors;
	u32 receive_errors;
};

struct e1000_host_mng_dhcp_cookie {
	u32 signature;
	u8  status;
	u8  reserved0;
	u16 vlan_id;
	u32 reserved1;
	u16 reserved2;
	u8  reserved3;
	u8  checksum;
};

/* Host Interface "Rev 1" */
struct e1000_host_command_header {
	u8 command_id;
	u8 command_length;
	u8 command_options;
	u8 checksum;
};

#define E1000_HI_MAX_DATA_LENGTH     252
struct e1000_host_command_info {
	struct e1000_host_command_header command_header;
	u8 command_data[E1000_HI_MAX_DATA_LENGTH];
};

/* Host Interface "Rev 2" */
struct e1000_host_mng_command_header {
	u8  command_id;
	u8  checksum;
	u16 reserved1;
	u16 reserved2;
	u16 command_length;
};

#define E1000_HI_MAX_MNG_DATA_LENGTH 0x6F8
struct e1000_host_mng_command_info {
	struct e1000_host_mng_command_header command_header;
	u8 command_data[E1000_HI_MAX_MNG_DATA_LENGTH];
};

/* Function pointers and static data for the MAC. */
struct e1000_mac_operations {
	u32			mng_mode_enab;

	s32  (*check_for_link)(struct e1000_hw *);
	s32  (*cleanup_led)(struct e1000_hw *);
	void (*clear_hw_cntrs)(struct e1000_hw *);
	s32  (*get_bus_info)(struct e1000_hw *);
	s32  (*get_link_up_info)(struct e1000_hw *, u16 *, u16 *);
	s32  (*led_on)(struct e1000_hw *);
	s32  (*led_off)(struct e1000_hw *);
	void (*update_mc_addr_list)(struct e1000_hw *, u8 *, u32, u32, u32);
	s32  (*reset_hw)(struct e1000_hw *);
	s32  (*init_hw)(struct e1000_hw *);
	s32  (*setup_link)(struct e1000_hw *);
	s32  (*setup_physical_interface)(struct e1000_hw *);
};

/* Function pointers for the PHY. */
struct e1000_phy_operations {
	s32  (*acquire_phy)(struct e1000_hw *);
	s32  (*check_reset_block)(struct e1000_hw *);
	s32  (*commit_phy)(struct e1000_hw *);
	s32  (*force_speed_duplex)(struct e1000_hw *);
	s32  (*get_cfg_done)(struct e1000_hw *hw);
	s32  (*get_cable_length)(struct e1000_hw *);
	s32  (*get_phy_info)(struct e1000_hw *);
	s32  (*read_phy_reg)(struct e1000_hw *, u32, u16 *);
	void (*release_phy)(struct e1000_hw *);
	s32  (*reset_phy)(struct e1000_hw *);
	s32  (*set_d0_lplu_state)(struct e1000_hw *, bool);
	s32  (*set_d3_lplu_state)(struct e1000_hw *, bool);
	s32  (*write_phy_reg)(struct e1000_hw *, u32, u16);
};

/* Function pointers for the NVM. */
struct e1000_nvm_operations {
	s32  (*acquire_nvm)(struct e1000_hw *);
	s32  (*read_nvm)(struct e1000_hw *, u16, u16, u16 *);
	void (*release_nvm)(struct e1000_hw *);
	s32  (*update_nvm)(struct e1000_hw *);
	s32  (*valid_led_default)(struct e1000_hw *, u16 *);
	s32  (*validate_nvm)(struct e1000_hw *);
	s32  (*write_nvm)(struct e1000_hw *, u16, u16, u16 *);
};

struct e1000_mac_info {
	struct e1000_mac_operations ops;

	u8 addr[6];
	u8 perm_addr[6];

	enum e1000_mac_type type;

	u32 collision_delta;
	u32 ledctl_default;
	u32 ledctl_mode1;
	u32 ledctl_mode2;
	u32 mc_filter_type;
	u32 tx_packet_delta;
	u32 txcw;

	u16 current_ifs_val;
	u16 ifs_max_val;
	u16 ifs_min_val;
	u16 ifs_ratio;
	u16 ifs_step_size;
	u16 mta_reg_count;
	u16 rar_entry_count;

	u8  forced_speed_duplex;

	bool arc_subsystem_valid;
	bool autoneg;
	bool autoneg_failed;
	bool get_link_status;
	bool in_ifs_mode;
	bool serdes_has_link;
	bool tx_pkt_filtering;
};

struct e1000_phy_info {
	struct e1000_phy_operations ops;

	enum e1000_phy_type type;

	enum e1000_1000t_rx_status local_rx;
	enum e1000_1000t_rx_status remote_rx;
	enum e1000_ms_type ms_type;
	enum e1000_ms_type original_ms_type;
	enum e1000_rev_polarity cable_polarity;
	enum e1000_smart_speed smart_speed;

	u32 addr;
	u32 id;
	u32 reset_delay_us; /* in usec */
	u32 revision;

	enum e1000_media_type media_type;

	u16 autoneg_advertised;
	u16 autoneg_mask;
	u16 cable_length;
	u16 max_cable_length;
	u16 min_cable_length;

	u8 mdix;

	bool disable_polarity_correction;
	bool is_mdix;
	bool polarity_correction;
	bool speed_downgraded;
	bool autoneg_wait_to_complete;
};

struct e1000_nvm_info {
	struct e1000_nvm_operations ops;

	enum e1000_nvm_type type;
	enum e1000_nvm_override override;

	u32 flash_bank_size;
	u32 flash_base_addr;

	u16 word_size;
	u16 delay_usec;
	u16 address_bits;
	u16 opcode_bits;
	u16 page_size;
};

struct e1000_bus_info {
	enum e1000_bus_width width;

	u16 func;
};

struct e1000_fc_info {
	u32 high_water;          /* Flow control high-water mark */
	u32 low_water;           /* Flow control low-water mark */
	u16 pause_time;          /* Flow control pause timer */
	bool send_xon;           /* Flow control send XON */
	bool strict_ieee;        /* Strict IEEE mode */
	enum e1000_fc_type type; /* Type of flow control */
	enum e1000_fc_type original_type;
};

struct e1000_dev_spec_82571 {
	bool laa_is_present;
	bool alt_mac_addr_is_present;
};

struct e1000_shadow_ram {
	u16  value;
	bool modified;
};

#define E1000_ICH8_SHADOW_RAM_WORDS		2048

struct e1000_dev_spec_ich8lan {
	bool kmrn_lock_loss_workaround_enabled;
	struct e1000_shadow_ram shadow_ram[E1000_ICH8_SHADOW_RAM_WORDS];
};

struct e1000_hw {
	struct e1000_adapter *adapter;

	u8 __iomem *hw_addr;
	u8 __iomem *flash_address;

	struct e1000_mac_info  mac;
	struct e1000_fc_info   fc;
	struct e1000_phy_info  phy;
	struct e1000_nvm_info  nvm;
	struct e1000_bus_info  bus;
	struct e1000_host_mng_dhcp_cookie mng_cookie;

	union {
		struct e1000_dev_spec_82571	e82571;
		struct e1000_dev_spec_ich8lan	ich8lan;
	} dev_spec;
};

#ifdef DEBUG
#define hw_dbg(hw, format, arg...) \
	printk(KERN_DEBUG "%s: " format, e1000e_get_hw_dev_name(hw), ##arg)
#else
static inline int __attribute__ ((format (printf, 2, 3)))
hw_dbg(struct e1000_hw *hw, const char *format, ...)
{
	return 0;
}
#endif

#endif
