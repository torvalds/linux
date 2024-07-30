/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _TXGBE_TYPE_H_
#define _TXGBE_TYPE_H_

#include <linux/property.h>
#include <linux/irq.h>

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

/********************************* Flow Director *****************************/
#define TXGBE_RDB_FDIR_DROP_QUEUE               127
#define TXGBE_RDB_FDIR_CTL                      0x19500
#define TXGBE_RDB_FDIR_CTL_INIT_DONE            BIT(3)
#define TXGBE_RDB_FDIR_CTL_PERFECT_MATCH        BIT(4)
#define TXGBE_RDB_FDIR_CTL_DROP_Q(v)            FIELD_PREP(GENMASK(14, 8), v)
#define TXGBE_RDB_FDIR_CTL_HASH_BITS(v)         FIELD_PREP(GENMASK(23, 20), v)
#define TXGBE_RDB_FDIR_CTL_MAX_LENGTH(v)        FIELD_PREP(GENMASK(27, 24), v)
#define TXGBE_RDB_FDIR_CTL_FULL_THRESH(v)       FIELD_PREP(GENMASK(31, 28), v)
#define TXGBE_RDB_FDIR_IP6(_i)                  (0x1950C + ((_i) * 4)) /* 0-2 */
#define TXGBE_RDB_FDIR_SA                       0x19518
#define TXGBE_RDB_FDIR_DA                       0x1951C
#define TXGBE_RDB_FDIR_PORT                     0x19520
#define TXGBE_RDB_FDIR_PORT_DESTINATION_SHIFT   16
#define TXGBE_RDB_FDIR_FLEX                     0x19524
#define TXGBE_RDB_FDIR_FLEX_FLEX_SHIFT          16
#define TXGBE_RDB_FDIR_HASH                     0x19528
#define TXGBE_RDB_FDIR_HASH_SIG_SW_INDEX(v)     FIELD_PREP(GENMASK(31, 16), v)
#define TXGBE_RDB_FDIR_HASH_BUCKET_VALID        BIT(15)
#define TXGBE_RDB_FDIR_CMD                      0x1952C
#define TXGBE_RDB_FDIR_CMD_CMD_MASK             GENMASK(1, 0)
#define TXGBE_RDB_FDIR_CMD_CMD(v)               FIELD_PREP(GENMASK(1, 0), v)
#define TXGBE_RDB_FDIR_CMD_CMD_ADD_FLOW         TXGBE_RDB_FDIR_CMD_CMD(1)
#define TXGBE_RDB_FDIR_CMD_CMD_REMOVE_FLOW      TXGBE_RDB_FDIR_CMD_CMD(2)
#define TXGBE_RDB_FDIR_CMD_CMD_QUERY_REM_FILT   TXGBE_RDB_FDIR_CMD_CMD(3)
#define TXGBE_RDB_FDIR_CMD_FILTER_VALID         BIT(2)
#define TXGBE_RDB_FDIR_CMD_FILTER_UPDATE        BIT(3)
#define TXGBE_RDB_FDIR_CMD_FLOW_TYPE(v)         FIELD_PREP(GENMASK(6, 5), v)
#define TXGBE_RDB_FDIR_CMD_DROP                 BIT(9)
#define TXGBE_RDB_FDIR_CMD_LAST                 BIT(11)
#define TXGBE_RDB_FDIR_CMD_QUEUE_EN             BIT(15)
#define TXGBE_RDB_FDIR_CMD_RX_QUEUE(v)          FIELD_PREP(GENMASK(22, 16), v)
#define TXGBE_RDB_FDIR_CMD_VT_POOL(v)           FIELD_PREP(GENMASK(29, 24), v)
#define TXGBE_RDB_FDIR_DA4_MSK                  0x1953C
#define TXGBE_RDB_FDIR_SA4_MSK                  0x19540
#define TXGBE_RDB_FDIR_TCP_MSK                  0x19544
#define TXGBE_RDB_FDIR_UDP_MSK                  0x19548
#define TXGBE_RDB_FDIR_SCTP_MSK                 0x19560
#define TXGBE_RDB_FDIR_HKEY                     0x19568
#define TXGBE_RDB_FDIR_SKEY                     0x1956C
#define TXGBE_RDB_FDIR_OTHER_MSK                0x19570
#define TXGBE_RDB_FDIR_OTHER_MSK_POOL           BIT(2)
#define TXGBE_RDB_FDIR_OTHER_MSK_L4P            BIT(3)
#define TXGBE_RDB_FDIR_FLEX_CFG(_i)             (0x19580 + ((_i) * 4))
#define TXGBE_RDB_FDIR_FLEX_CFG_FIELD0          GENMASK(7, 0)
#define TXGBE_RDB_FDIR_FLEX_CFG_BASE_MAC        FIELD_PREP(GENMASK(1, 0), 0)
#define TXGBE_RDB_FDIR_FLEX_CFG_MSK             BIT(2)
#define TXGBE_RDB_FDIR_FLEX_CFG_OFST(v)         FIELD_PREP(GENMASK(7, 3), v)

/* Checksum and EEPROM pointers */
#define TXGBE_EEPROM_LAST_WORD                  0x800
#define TXGBE_EEPROM_CHECKSUM                   0x2F
#define TXGBE_EEPROM_SUM                        0xBABA
#define TXGBE_EEPROM_VERSION_L                  0x1D
#define TXGBE_EEPROM_VERSION_H                  0x1E
#define TXGBE_ISCSI_BOOT_CONFIG                 0x07

#define TXGBE_MAX_MSIX_VECTORS          64
#define TXGBE_MAX_FDIR_INDICES          63
#define TXGBE_MAX_RSS_INDICES           63

#define TXGBE_MAX_RX_QUEUES   (TXGBE_MAX_FDIR_INDICES + 1)
#define TXGBE_MAX_TX_QUEUES   (TXGBE_MAX_FDIR_INDICES + 1)

#define TXGBE_SP_MAX_TX_QUEUES  128
#define TXGBE_SP_MAX_RX_QUEUES  128
#define TXGBE_SP_RAR_ENTRIES    128
#define TXGBE_SP_MC_TBL_SIZE    128
#define TXGBE_SP_VFT_TBL_SIZE   128
#define TXGBE_SP_RX_PB_SIZE     512
#define TXGBE_SP_TDB_PB_SZ      (160 * 1024) /* 160KB Packet Buffer */

#define TXGBE_DEFAULT_ATR_SAMPLE_RATE           20

/* Software ATR hash keys */
#define TXGBE_ATR_BUCKET_HASH_KEY               0x3DAD14E2
#define TXGBE_ATR_SIGNATURE_HASH_KEY            0x174D3614

/* Software ATR input stream values and masks */
#define TXGBE_ATR_HASH_MASK                     0x7fff
#define TXGBE_ATR_L4TYPE_MASK                   0x3
#define TXGBE_ATR_L4TYPE_UDP                    0x1
#define TXGBE_ATR_L4TYPE_TCP                    0x2
#define TXGBE_ATR_L4TYPE_SCTP                   0x3
#define TXGBE_ATR_L4TYPE_IPV6_MASK              0x4
#define TXGBE_ATR_L4TYPE_TUNNEL_MASK            0x10

enum txgbe_atr_flow_type {
	TXGBE_ATR_FLOW_TYPE_IPV4                = 0x0,
	TXGBE_ATR_FLOW_TYPE_UDPV4               = 0x1,
	TXGBE_ATR_FLOW_TYPE_TCPV4               = 0x2,
	TXGBE_ATR_FLOW_TYPE_SCTPV4              = 0x3,
	TXGBE_ATR_FLOW_TYPE_IPV6                = 0x4,
	TXGBE_ATR_FLOW_TYPE_UDPV6               = 0x5,
	TXGBE_ATR_FLOW_TYPE_TCPV6               = 0x6,
	TXGBE_ATR_FLOW_TYPE_SCTPV6              = 0x7,
	TXGBE_ATR_FLOW_TYPE_TUNNELED_IPV4       = 0x10,
	TXGBE_ATR_FLOW_TYPE_TUNNELED_UDPV4      = 0x11,
	TXGBE_ATR_FLOW_TYPE_TUNNELED_TCPV4      = 0x12,
	TXGBE_ATR_FLOW_TYPE_TUNNELED_SCTPV4     = 0x13,
	TXGBE_ATR_FLOW_TYPE_TUNNELED_IPV6       = 0x14,
	TXGBE_ATR_FLOW_TYPE_TUNNELED_UDPV6      = 0x15,
	TXGBE_ATR_FLOW_TYPE_TUNNELED_TCPV6      = 0x16,
	TXGBE_ATR_FLOW_TYPE_TUNNELED_SCTPV6     = 0x17,
};

/* Flow Director ATR input struct. */
union txgbe_atr_input {
	/* Byte layout in order, all values with MSB first:
	 *
	 * vm_pool    - 1 byte
	 * flow_type  - 1 byte
	 * vlan_id    - 2 bytes
	 * dst_ip     - 16 bytes
	 * src_ip     - 16 bytes
	 * src_port   - 2 bytes
	 * dst_port   - 2 bytes
	 * flex_bytes - 2 bytes
	 * bkt_hash   - 2 bytes
	 */
	struct {
		u8 vm_pool;
		u8 flow_type;
		__be16 vlan_id;
		__be32 dst_ip[4];
		__be32 src_ip[4];
		__be16 src_port;
		__be16 dst_port;
		__be16 flex_bytes;
		__be16 bkt_hash;
	} formatted;
	__be32 dword_stream[11];
};

/* Flow Director compressed ATR hash input struct */
union txgbe_atr_hash_dword {
	struct {
		u8 vm_pool;
		u8 flow_type;
		__be16 vlan_id;
	} formatted;
	__be32 ip;
	struct {
		__be16 src;
		__be16 dst;
	} port;
	__be16 flex_bytes;
	__be32 dword;
};

enum txgbe_fdir_pballoc_type {
	TXGBE_FDIR_PBALLOC_NONE = 0,
	TXGBE_FDIR_PBALLOC_64K  = 1,
	TXGBE_FDIR_PBALLOC_128K = 2,
	TXGBE_FDIR_PBALLOC_256K = 3,
};

struct txgbe_fdir_filter {
	struct hlist_node fdir_node;
	union txgbe_atr_input filter;
	u16 sw_idx;
	u16 action;
};

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

#define TXGBE_INTR_MISC       BIT(0)
#define TXGBE_INTR_QALL(A)    GENMASK((A)->num_q_vectors, 1)

#define TXGBE_MAX_EITR        GENMASK(11, 3)

extern char txgbe_driver_name[];

void txgbe_down(struct wx *wx);
void txgbe_up(struct wx *wx);
int txgbe_setup_tc(struct net_device *dev, u8 tc);
void txgbe_do_reset(struct net_device *netdev);

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

enum txgbe_misc_irqs {
	TXGBE_IRQ_GPIO = 0,
	TXGBE_IRQ_LINK,
	TXGBE_IRQ_MAX
};

struct txgbe_irq {
	struct irq_chip chip;
	struct irq_domain *domain;
	int nirqs;
	int irq;
};

struct txgbe {
	struct wx *wx;
	struct txgbe_nodes nodes;
	struct txgbe_irq misc;
	struct dw_xpcs *xpcs;
	struct platform_device *sfp_dev;
	struct platform_device *i2c_dev;
	struct clk_lookup *clock;
	struct clk *clk;
	struct gpio_chip *gpio;
	unsigned int gpio_irq;
	unsigned int link_irq;

	/* flow director */
	struct hlist_head fdir_filter_list;
	union txgbe_atr_input fdir_mask;
	int fdir_filter_count;
	spinlock_t fdir_perfect_lock; /* spinlock for FDIR */
};

#endif /* _TXGBE_TYPE_H_ */
