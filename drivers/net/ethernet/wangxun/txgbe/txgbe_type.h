/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _TXGBE_TYPE_H_
#define _TXGBE_TYPE_H_

#include <linux/property.h>

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

/* GPIO register bit */
#define TXGBE_GPIOBIT_0                         BIT(0) /* I:tx fault */
#define TXGBE_GPIOBIT_1                         BIT(1) /* O:tx disabled */
#define TXGBE_GPIOBIT_2                         BIT(2) /* I:sfp module absent */
#define TXGBE_GPIOBIT_3                         BIT(3) /* I:rx signal lost */
#define TXGBE_GPIOBIT_4                         BIT(4) /* O:rate select, 1G(0) 10G(1) */
#define TXGBE_GPIOBIT_5                         BIT(5) /* O:rate select, 1G(0) 10G(1) */

/* Extended Interrupt Enable Set */
#define TXGBE_PX_MISC_ETH_LKDN                  BIT(8)
#define TXGBE_PX_MISC_DEV_RST                   BIT(10)
#define TXGBE_PX_MISC_ETH_EVENT                 BIT(17)
#define TXGBE_PX_MISC_ETH_LK                    BIT(18)
#define TXGBE_PX_MISC_ETH_AN                    BIT(19)
#define TXGBE_PX_MISC_INT_ERR                   BIT(20)
#define TXGBE_PX_MISC_GPIO                      BIT(26)
#define TXGBE_PX_MISC_IEN_MASK                            \
	(TXGBE_PX_MISC_ETH_LKDN | TXGBE_PX_MISC_DEV_RST | \
	 TXGBE_PX_MISC_ETH_EVENT | TXGBE_PX_MISC_ETH_LK | \
	 TXGBE_PX_MISC_ETH_AN | TXGBE_PX_MISC_INT_ERR |   \
	 TXGBE_PX_MISC_GPIO)

/* Port cfg registers */
#define TXGBE_CFG_PORT_ST                       0x14404
#define TXGBE_CFG_PORT_ST_LINK_UP               BIT(0)

/* I2C registers */
#define TXGBE_I2C_BASE                          0x14900

/************************************** ETH PHY ******************************/
#define TXGBE_XPCS_IDA_ADDR                     0x13000
#define TXGBE_XPCS_IDA_DATA                     0x13004

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

#define TXGBE_MAX_MSIX_VECTORS          64
#define TXGBE_MAX_FDIR_INDICES          63

#define TXGBE_MAX_RX_QUEUES   (TXGBE_MAX_FDIR_INDICES + 1)
#define TXGBE_MAX_TX_QUEUES   (TXGBE_MAX_FDIR_INDICES + 1)

#define TXGBE_SP_MAX_TX_QUEUES  128
#define TXGBE_SP_MAX_RX_QUEUES  128
#define TXGBE_SP_RAR_ENTRIES    128
#define TXGBE_SP_MC_TBL_SIZE    128
#define TXGBE_SP_VFT_TBL_SIZE   128
#define TXGBE_SP_RX_PB_SIZE     512
#define TXGBE_SP_TDB_PB_SZ      (160 * 1024) /* 160KB Packet Buffer */

/* TX/RX descriptor defines */
#define TXGBE_DEFAULT_TXD               512
#define TXGBE_DEFAULT_TX_WORK           256

#if (PAGE_SIZE < 8192)
#define TXGBE_DEFAULT_RXD               512
#define TXGBE_DEFAULT_RX_WORK           256
#else
#define TXGBE_DEFAULT_RXD               256
#define TXGBE_DEFAULT_RX_WORK           128
#endif

#define TXGBE_INTR_MISC(A)    BIT((A)->num_q_vectors)
#define TXGBE_INTR_QALL(A)    (TXGBE_INTR_MISC(A) - 1)

#define TXGBE_MAX_EITR        GENMASK(11, 3)

extern char txgbe_driver_name[];

static inline struct txgbe *netdev_to_txgbe(struct net_device *netdev)
{
	struct wx *wx = netdev_priv(netdev);

	return wx->priv;
}

#define NODE_PROP(_NAME, _PROP)			\
	(const struct software_node) {		\
		.name = _NAME,			\
		.properties = _PROP,		\
	}

enum txgbe_swnodes {
	SWNODE_GPIO = 0,
	SWNODE_I2C,
	SWNODE_SFP,
	SWNODE_PHYLINK,
	SWNODE_MAX
};

struct txgbe_nodes {
	char gpio_name[32];
	char i2c_name[32];
	char sfp_name[32];
	char phylink_name[32];
	struct property_entry gpio_props[1];
	struct property_entry i2c_props[3];
	struct property_entry sfp_props[8];
	struct property_entry phylink_props[2];
	struct software_node_ref_args i2c_ref[1];
	struct software_node_ref_args gpio0_ref[1];
	struct software_node_ref_args gpio1_ref[1];
	struct software_node_ref_args gpio2_ref[1];
	struct software_node_ref_args gpio3_ref[1];
	struct software_node_ref_args gpio4_ref[1];
	struct software_node_ref_args gpio5_ref[1];
	struct software_node_ref_args sfp_ref[1];
	struct software_node swnodes[SWNODE_MAX];
	const struct software_node *group[SWNODE_MAX + 1];
};

struct txgbe {
	struct wx *wx;
	struct txgbe_nodes nodes;
	struct dw_xpcs *xpcs;
	struct phylink *phylink;
	struct platform_device *sfp_dev;
	struct platform_device *i2c_dev;
	struct clk_lookup *clock;
	struct clk *clk;
	struct gpio_chip *gpio;
};

#endif /* _TXGBE_TYPE_H_ */
