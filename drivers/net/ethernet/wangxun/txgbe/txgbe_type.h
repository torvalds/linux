/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _TXGBE_TYPE_H_
#define _TXGBE_TYPE_H_

/* Device IDs */
#define TXGBE_DEV_ID_SP1000                     0x1001
#define TXGBE_DEV_ID_WX1820                     0x2001

/* Subsystem IDs */
/* SFP */
#define TXGBE_ID_SP1000_SFP                     0x0000
#define TXGBE_ID_WX1820_SFP                     0x2000
#define TXGBE_ID_SFP                            0x00

/* copper */
#define TXGBE_ID_SP1000_XAUI                    0x1010
#define TXGBE_ID_WX1820_XAUI                    0x2010
#define TXGBE_ID_XAUI                           0x10
#define TXGBE_ID_SP1000_SGMII                   0x1020
#define TXGBE_ID_WX1820_SGMII                   0x2020
#define TXGBE_ID_SGMII                          0x20
/* backplane */
#define TXGBE_ID_SP1000_KR_KX_KX4               0x1030
#define TXGBE_ID_WX1820_KR_KX_KX4               0x2030
#define TXGBE_ID_KR_KX_KX4                      0x30
/* MAC Interface */
#define TXGBE_ID_SP1000_MAC_XAUI                0x1040
#define TXGBE_ID_WX1820_MAC_XAUI                0x2040
#define TXGBE_ID_MAC_XAUI                       0x40
#define TXGBE_ID_SP1000_MAC_SGMII               0x1060
#define TXGBE_ID_WX1820_MAC_SGMII               0x2060
#define TXGBE_ID_MAC_SGMII                      0x60

/* Combined interface*/
#define TXGBE_ID_SFI_XAUI			0x50

/* Revision ID */
#define TXGBE_SP_MPW  1

/**************** SP Registers ****************************/
/* chip control Registers */
#define TXGBE_MIS_PRB_CTL                       0x10010
#define TXGBE_MIS_PRB_CTL_LAN_UP(_i)            BIT(1 - (_i))
/* FMGR Registers */
#define TXGBE_SPI_ILDR_STATUS                   0x10120
#define TXGBE_SPI_ILDR_STATUS_PERST             BIT(0) /* PCIE_PERST is done */
#define TXGBE_SPI_ILDR_STATUS_PWRRST            BIT(1) /* Power on reset is done */
#define TXGBE_SPI_ILDR_STATUS_LAN_SW_RST(_i)    BIT((_i) + 9) /* lan soft reset done */

/* Sensors for PVT(Process Voltage Temperature) */
#define TXGBE_TS_CTL                            0x10300
#define TXGBE_TS_CTL_EVAL_MD                    BIT(31)

/* Part Number String Length */
#define TXGBE_PBANUM_LENGTH                     32

/* Checksum and EEPROM pointers */
#define TXGBE_EEPROM_LAST_WORD                  0x800
#define TXGBE_EEPROM_CHECKSUM                   0x2F
#define TXGBE_EEPROM_SUM                        0xBABA
#define TXGBE_EEPROM_VERSION_L                  0x1D
#define TXGBE_EEPROM_VERSION_H                  0x1E
#define TXGBE_ISCSI_BOOT_CONFIG                 0x07
#define TXGBE_PBANUM0_PTR                       0x05
#define TXGBE_PBANUM1_PTR                       0x06
#define TXGBE_PBANUM_PTR_GUARD                  0xFAFA

struct txgbe_hw {
	struct wx_hw wxhw;
};

#endif /* _TXGBE_TYPE_H_ */
