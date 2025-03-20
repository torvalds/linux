// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/net/phy/micrel.c
 *
 * Driver for Micrel PHYs
 *
 * Author: David J. Choi
 *
 * Copyright (c) 2010-2013 Micrel, Inc.
 * Copyright (c) 2014 Johan Hovold <johan@kernel.org>
 *
 * Support : Micrel Phys:
 *		Giga phys: ksz9021, ksz9031, ksz9131, lan8841, lan8814
 *		100/10 Phys : ksz8001, ksz8721, ksz8737, ksz8041
 *			   ksz8021, ksz8031, ksz8051,
 *			   ksz8081, ksz8091,
 *			   ksz8061,
 *		Switch : ksz8873, ksz886x
 *			 ksz9477, lan8804
 */

#include <linux/bitfield.h>
#include <linux/ethtool_netlink.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/micrel_phy.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/ptp_clock.h>
#include <linux/ptp_classify.h>
#include <linux/net_tstamp.h>
#include <linux/gpio/consumer.h>

#include "phylib.h"

/* Operation Mode Strap Override */
#define MII_KSZPHY_OMSO				0x16
#define KSZPHY_OMSO_FACTORY_TEST		BIT(15)
#define KSZPHY_OMSO_B_CAST_OFF			BIT(9)
#define KSZPHY_OMSO_NAND_TREE_ON		BIT(5)
#define KSZPHY_OMSO_RMII_OVERRIDE		BIT(1)
#define KSZPHY_OMSO_MII_OVERRIDE		BIT(0)

/* general Interrupt control/status reg in vendor specific block. */
#define MII_KSZPHY_INTCS			0x1B
#define KSZPHY_INTCS_JABBER			BIT(15)
#define KSZPHY_INTCS_RECEIVE_ERR		BIT(14)
#define KSZPHY_INTCS_PAGE_RECEIVE		BIT(13)
#define KSZPHY_INTCS_PARELLEL			BIT(12)
#define KSZPHY_INTCS_LINK_PARTNER_ACK		BIT(11)
#define KSZPHY_INTCS_LINK_DOWN			BIT(10)
#define KSZPHY_INTCS_REMOTE_FAULT		BIT(9)
#define KSZPHY_INTCS_LINK_UP			BIT(8)
#define KSZPHY_INTCS_ALL			(KSZPHY_INTCS_LINK_UP |\
						KSZPHY_INTCS_LINK_DOWN)
#define KSZPHY_INTCS_LINK_DOWN_STATUS		BIT(2)
#define KSZPHY_INTCS_LINK_UP_STATUS		BIT(0)
#define KSZPHY_INTCS_STATUS			(KSZPHY_INTCS_LINK_DOWN_STATUS |\
						 KSZPHY_INTCS_LINK_UP_STATUS)

/* LinkMD Control/Status */
#define KSZ8081_LMD				0x1d
#define KSZ8081_LMD_ENABLE_TEST			BIT(15)
#define KSZ8081_LMD_STAT_NORMAL			0
#define KSZ8081_LMD_STAT_OPEN			1
#define KSZ8081_LMD_STAT_SHORT			2
#define KSZ8081_LMD_STAT_FAIL			3
#define KSZ8081_LMD_STAT_MASK			GENMASK(14, 13)
/* Short cable (<10 meter) has been detected by LinkMD */
#define KSZ8081_LMD_SHORT_INDICATOR		BIT(12)
#define KSZ8081_LMD_DELTA_TIME_MASK		GENMASK(8, 0)

#define KSZ9x31_LMD				0x12
#define KSZ9x31_LMD_VCT_EN			BIT(15)
#define KSZ9x31_LMD_VCT_DIS_TX			BIT(14)
#define KSZ9x31_LMD_VCT_PAIR(n)			(((n) & 0x3) << 12)
#define KSZ9x31_LMD_VCT_SEL_RESULT		0
#define KSZ9x31_LMD_VCT_SEL_THRES_HI		BIT(10)
#define KSZ9x31_LMD_VCT_SEL_THRES_LO		BIT(11)
#define KSZ9x31_LMD_VCT_SEL_MASK		GENMASK(11, 10)
#define KSZ9x31_LMD_VCT_ST_NORMAL		0
#define KSZ9x31_LMD_VCT_ST_OPEN			1
#define KSZ9x31_LMD_VCT_ST_SHORT		2
#define KSZ9x31_LMD_VCT_ST_FAIL			3
#define KSZ9x31_LMD_VCT_ST_MASK			GENMASK(9, 8)
#define KSZ9x31_LMD_VCT_DATA_REFLECTED_INVALID	BIT(7)
#define KSZ9x31_LMD_VCT_DATA_SIG_WAIT_TOO_LONG	BIT(6)
#define KSZ9x31_LMD_VCT_DATA_MASK100		BIT(5)
#define KSZ9x31_LMD_VCT_DATA_NLP_FLP		BIT(4)
#define KSZ9x31_LMD_VCT_DATA_LO_PULSE_MASK	GENMASK(3, 2)
#define KSZ9x31_LMD_VCT_DATA_HI_PULSE_MASK	GENMASK(1, 0)
#define KSZ9x31_LMD_VCT_DATA_MASK		GENMASK(7, 0)

#define KSZPHY_WIRE_PAIR_MASK			0x3

#define LAN8814_CABLE_DIAG			0x12
#define LAN8814_CABLE_DIAG_STAT_MASK		GENMASK(9, 8)
#define LAN8814_CABLE_DIAG_VCT_DATA_MASK	GENMASK(7, 0)
#define LAN8814_PAIR_BIT_SHIFT			12

#define LAN8814_WIRE_PAIR_MASK			0xF

/* Lan8814 general Interrupt control/status reg in GPHY specific block. */
#define LAN8814_INTC				0x18
#define LAN8814_INTS				0x1B

#define LAN8814_INT_LINK_DOWN			BIT(2)
#define LAN8814_INT_LINK_UP			BIT(0)
#define LAN8814_INT_LINK			(LAN8814_INT_LINK_UP |\
						 LAN8814_INT_LINK_DOWN)

#define LAN8814_INTR_CTRL_REG			0x34
#define LAN8814_INTR_CTRL_REG_POLARITY		BIT(1)
#define LAN8814_INTR_CTRL_REG_INTR_ENABLE	BIT(0)

#define LAN8814_EEE_STATE			0x38
#define LAN8814_EEE_STATE_MASK2P5P		BIT(10)

#define LAN8814_PD_CONTROLS			0x9d
#define LAN8814_PD_CONTROLS_PD_MEAS_TIME_MASK	GENMASK(3, 0)
#define LAN8814_PD_CONTROLS_PD_MEAS_TIME_VAL	0xb

/* Represents 1ppm adjustment in 2^32 format with
 * each nsec contains 4 clock cycles.
 * The value is calculated as following: (1/1000000)/((2^-32)/4)
 */
#define LAN8814_1PPM_FORMAT			17179

/* Represents 1ppm adjustment in 2^32 format with
 * each nsec contains 8 clock cycles.
 * The value is calculated as following: (1/1000000)/((2^-32)/8)
 */
#define LAN8841_1PPM_FORMAT			34360

#define PTP_RX_VERSION				0x0248
#define PTP_TX_VERSION				0x0288
#define PTP_MAX_VERSION(x)			(((x) & GENMASK(7, 0)) << 8)
#define PTP_MIN_VERSION(x)			((x) & GENMASK(7, 0))

#define PTP_RX_MOD				0x024F
#define PTP_RX_MOD_BAD_UDPV4_CHKSUM_FORCE_FCS_DIS_ BIT(3)
#define PTP_RX_TIMESTAMP_EN			0x024D
#define PTP_TX_TIMESTAMP_EN			0x028D

#define PTP_TIMESTAMP_EN_SYNC_			BIT(0)
#define PTP_TIMESTAMP_EN_DREQ_			BIT(1)
#define PTP_TIMESTAMP_EN_PDREQ_			BIT(2)
#define PTP_TIMESTAMP_EN_PDRES_			BIT(3)

#define PTP_TX_PARSE_L2_ADDR_EN			0x0284
#define PTP_RX_PARSE_L2_ADDR_EN			0x0244

#define PTP_TX_PARSE_IP_ADDR_EN			0x0285
#define PTP_RX_PARSE_IP_ADDR_EN			0x0245
#define LTC_HARD_RESET				0x023F
#define LTC_HARD_RESET_				BIT(0)

#define TSU_HARD_RESET				0x02C1
#define TSU_HARD_RESET_				BIT(0)

#define PTP_CMD_CTL				0x0200
#define PTP_CMD_CTL_PTP_DISABLE_		BIT(0)
#define PTP_CMD_CTL_PTP_ENABLE_			BIT(1)
#define PTP_CMD_CTL_PTP_CLOCK_READ_		BIT(3)
#define PTP_CMD_CTL_PTP_CLOCK_LOAD_		BIT(4)
#define PTP_CMD_CTL_PTP_LTC_STEP_SEC_		BIT(5)
#define PTP_CMD_CTL_PTP_LTC_STEP_NSEC_		BIT(6)

#define PTP_COMMON_INT_ENA			0x0204
#define PTP_COMMON_INT_ENA_GPIO_CAP_EN		BIT(2)

#define PTP_CLOCK_SET_SEC_HI			0x0205
#define PTP_CLOCK_SET_SEC_MID			0x0206
#define PTP_CLOCK_SET_SEC_LO			0x0207
#define PTP_CLOCK_SET_NS_HI			0x0208
#define PTP_CLOCK_SET_NS_LO			0x0209

#define PTP_CLOCK_READ_SEC_HI			0x0229
#define PTP_CLOCK_READ_SEC_MID			0x022A
#define PTP_CLOCK_READ_SEC_LO			0x022B
#define PTP_CLOCK_READ_NS_HI			0x022C
#define PTP_CLOCK_READ_NS_LO			0x022D

#define PTP_GPIO_SEL				0x0230
#define PTP_GPIO_SEL_GPIO_SEL(pin)		((pin) << 8)
#define PTP_GPIO_CAP_MAP_LO			0x0232

#define PTP_GPIO_CAP_EN				0x0233
#define PTP_GPIO_CAP_EN_GPIO_RE_CAPTURE_ENABLE(gpio)	BIT(gpio)
#define PTP_GPIO_CAP_EN_GPIO_FE_CAPTURE_ENABLE(gpio)	(BIT(gpio) << 8)

#define PTP_GPIO_RE_LTC_SEC_HI_CAP		0x0235
#define PTP_GPIO_RE_LTC_SEC_LO_CAP		0x0236
#define PTP_GPIO_RE_LTC_NS_HI_CAP		0x0237
#define PTP_GPIO_RE_LTC_NS_LO_CAP		0x0238
#define PTP_GPIO_FE_LTC_SEC_HI_CAP		0x0239
#define PTP_GPIO_FE_LTC_SEC_LO_CAP		0x023A
#define PTP_GPIO_FE_LTC_NS_HI_CAP		0x023B
#define PTP_GPIO_FE_LTC_NS_LO_CAP		0x023C

#define PTP_GPIO_CAP_STS			0x023D
#define PTP_GPIO_CAP_STS_PTP_GPIO_RE_STS(gpio)	BIT(gpio)
#define PTP_GPIO_CAP_STS_PTP_GPIO_FE_STS(gpio)	(BIT(gpio) << 8)

#define PTP_OPERATING_MODE			0x0241
#define PTP_OPERATING_MODE_STANDALONE_		BIT(0)

#define PTP_TX_MOD				0x028F
#define PTP_TX_MOD_TX_PTP_SYNC_TS_INSERT_	BIT(12)
#define PTP_TX_MOD_BAD_UDPV4_CHKSUM_FORCE_FCS_DIS_ BIT(3)

#define PTP_RX_PARSE_CONFIG			0x0242
#define PTP_RX_PARSE_CONFIG_LAYER2_EN_		BIT(0)
#define PTP_RX_PARSE_CONFIG_IPV4_EN_		BIT(1)
#define PTP_RX_PARSE_CONFIG_IPV6_EN_		BIT(2)

#define PTP_TX_PARSE_CONFIG			0x0282
#define PTP_TX_PARSE_CONFIG_LAYER2_EN_		BIT(0)
#define PTP_TX_PARSE_CONFIG_IPV4_EN_		BIT(1)
#define PTP_TX_PARSE_CONFIG_IPV6_EN_		BIT(2)

#define PTP_CLOCK_RATE_ADJ_HI			0x020C
#define PTP_CLOCK_RATE_ADJ_LO			0x020D
#define PTP_CLOCK_RATE_ADJ_DIR_			BIT(15)

#define PTP_LTC_STEP_ADJ_HI			0x0212
#define PTP_LTC_STEP_ADJ_LO			0x0213
#define PTP_LTC_STEP_ADJ_DIR_			BIT(15)

#define LAN8814_INTR_STS_REG			0x0033
#define LAN8814_INTR_STS_REG_1588_TSU0_		BIT(0)
#define LAN8814_INTR_STS_REG_1588_TSU1_		BIT(1)
#define LAN8814_INTR_STS_REG_1588_TSU2_		BIT(2)
#define LAN8814_INTR_STS_REG_1588_TSU3_		BIT(3)

#define PTP_CAP_INFO				0x022A
#define PTP_CAP_INFO_TX_TS_CNT_GET_(reg_val)	(((reg_val) & 0x0f00) >> 8)
#define PTP_CAP_INFO_RX_TS_CNT_GET_(reg_val)	((reg_val) & 0x000f)

#define PTP_TX_EGRESS_SEC_HI			0x0296
#define PTP_TX_EGRESS_SEC_LO			0x0297
#define PTP_TX_EGRESS_NS_HI			0x0294
#define PTP_TX_EGRESS_NS_LO			0x0295
#define PTP_TX_MSG_HEADER2			0x0299

#define PTP_RX_INGRESS_SEC_HI			0x0256
#define PTP_RX_INGRESS_SEC_LO			0x0257
#define PTP_RX_INGRESS_NS_HI			0x0254
#define PTP_RX_INGRESS_NS_LO			0x0255
#define PTP_RX_MSG_HEADER2			0x0259

#define PTP_TSU_INT_EN				0x0200
#define PTP_TSU_INT_EN_PTP_TX_TS_OVRFL_EN_	BIT(3)
#define PTP_TSU_INT_EN_PTP_TX_TS_EN_		BIT(2)
#define PTP_TSU_INT_EN_PTP_RX_TS_OVRFL_EN_	BIT(1)
#define PTP_TSU_INT_EN_PTP_RX_TS_EN_		BIT(0)

#define PTP_TSU_INT_STS				0x0201
#define PTP_TSU_INT_STS_PTP_TX_TS_OVRFL_INT_	BIT(3)
#define PTP_TSU_INT_STS_PTP_TX_TS_EN_		BIT(2)
#define PTP_TSU_INT_STS_PTP_RX_TS_OVRFL_INT_	BIT(1)
#define PTP_TSU_INT_STS_PTP_RX_TS_EN_		BIT(0)

#define LAN8814_LED_CTRL_1			0x0
#define LAN8814_LED_CTRL_1_KSZ9031_LED_MODE_	BIT(6)

/* PHY Control 1 */
#define MII_KSZPHY_CTRL_1			0x1e
#define KSZ8081_CTRL1_MDIX_STAT			BIT(4)

/* PHY Control 2 / PHY Control (if no PHY Control 1) */
#define MII_KSZPHY_CTRL_2			0x1f
#define MII_KSZPHY_CTRL				MII_KSZPHY_CTRL_2
/* bitmap of PHY register to set interrupt mode */
#define KSZ8081_CTRL2_HP_MDIX			BIT(15)
#define KSZ8081_CTRL2_MDI_MDI_X_SELECT		BIT(14)
#define KSZ8081_CTRL2_DISABLE_AUTO_MDIX		BIT(13)
#define KSZ8081_CTRL2_FORCE_LINK		BIT(11)
#define KSZ8081_CTRL2_POWER_SAVING		BIT(10)
#define KSZPHY_CTRL_INT_ACTIVE_HIGH		BIT(9)
#define KSZPHY_RMII_REF_CLK_SEL			BIT(7)

/* Write/read to/from extended registers */
#define MII_KSZPHY_EXTREG			0x0b
#define KSZPHY_EXTREG_WRITE			0x8000

#define MII_KSZPHY_EXTREG_WRITE			0x0c
#define MII_KSZPHY_EXTREG_READ			0x0d

/* Extended registers */
#define MII_KSZPHY_CLK_CONTROL_PAD_SKEW		0x104
#define MII_KSZPHY_RX_DATA_PAD_SKEW		0x105
#define MII_KSZPHY_TX_DATA_PAD_SKEW		0x106

#define PS_TO_REG				200
#define FIFO_SIZE				8

#define LAN8814_PTP_GPIO_NUM			24
#define LAN8814_PTP_PEROUT_NUM			2
#define LAN8814_PTP_EXTTS_NUM			3

#define LAN8814_BUFFER_TIME			2

#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_200MS	13
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_100MS	12
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_50MS	11
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_10MS	10
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_5MS	9
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_1MS	8
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_500US	7
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_100US	6
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_50US	5
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_10US	4
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_5US	3
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_1US	2
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_500NS	1
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_100NS	0

#define LAN8814_GPIO_EN1			0x20
#define LAN8814_GPIO_EN2			0x21
#define LAN8814_GPIO_DIR1			0x22
#define LAN8814_GPIO_DIR2			0x23
#define LAN8814_GPIO_BUF1			0x24
#define LAN8814_GPIO_BUF2			0x25

#define LAN8814_GPIO_EN_ADDR(pin) \
	((pin) > 15 ? LAN8814_GPIO_EN1 : LAN8814_GPIO_EN2)
#define LAN8814_GPIO_EN_BIT(pin)		BIT(pin)
#define LAN8814_GPIO_DIR_ADDR(pin) \
	((pin) > 15 ? LAN8814_GPIO_DIR1 : LAN8814_GPIO_DIR2)
#define LAN8814_GPIO_DIR_BIT(pin)		BIT(pin)
#define LAN8814_GPIO_BUF_ADDR(pin) \
	((pin) > 15 ? LAN8814_GPIO_BUF1 : LAN8814_GPIO_BUF2)
#define LAN8814_GPIO_BUF_BIT(pin)		BIT(pin)

#define LAN8814_EVENT_A				0
#define LAN8814_EVENT_B				1

#define LAN8814_PTP_GENERAL_CONFIG		0x0201
#define LAN8814_PTP_GENERAL_CONFIG_LTC_EVENT_MASK(event) \
	((event) ? GENMASK(11, 8) : GENMASK(7, 4))
#define LAN8814_PTP_GENERAL_CONFIG_LTC_EVENT_SET(event, value) \
	(((value) & GENMASK(3, 0)) << (4 + ((event) << 2)))
#define LAN8814_PTP_GENERAL_CONFIG_RELOAD_ADD_X(event) \
	((event) ? BIT(2) : BIT(0))
#define LAN8814_PTP_GENERAL_CONFIG_POLARITY_X(event) \
	((event) ? BIT(3) : BIT(1))

#define LAN8814_PTP_CLOCK_TARGET_SEC_HI(event)	((event) ? 0x21F : 0x215)
#define LAN8814_PTP_CLOCK_TARGET_SEC_LO(event)	((event) ? 0x220 : 0x216)
#define LAN8814_PTP_CLOCK_TARGET_NS_HI(event)	((event) ? 0x221 : 0x217)
#define LAN8814_PTP_CLOCK_TARGET_NS_LO(event)	((event) ? 0x222 : 0x218)

#define LAN8814_PTP_CLOCK_TARGET_RELOAD_SEC_HI(event)	((event) ? 0x223 : 0x219)
#define LAN8814_PTP_CLOCK_TARGET_RELOAD_SEC_LO(event)	((event) ? 0x224 : 0x21A)
#define LAN8814_PTP_CLOCK_TARGET_RELOAD_NS_HI(event)	((event) ? 0x225 : 0x21B)
#define LAN8814_PTP_CLOCK_TARGET_RELOAD_NS_LO(event)	((event) ? 0x226 : 0x21C)

/* Delay used to get the second part from the LTC */
#define LAN8841_GET_SEC_LTC_DELAY		(500 * NSEC_PER_MSEC)

struct kszphy_hw_stat {
	const char *string;
	u8 reg;
	u8 bits;
};

static struct kszphy_hw_stat kszphy_hw_stats[] = {
	{ "phy_receive_errors", 21, 16},
	{ "phy_idle_errors", 10, 8 },
};

struct kszphy_type {
	u32 led_mode_reg;
	u16 interrupt_level_mask;
	u16 cable_diag_reg;
	unsigned long pair_mask;
	u16 disable_dll_tx_bit;
	u16 disable_dll_rx_bit;
	u16 disable_dll_mask;
	bool has_broadcast_disable;
	bool has_nand_tree_disable;
	bool has_rmii_ref_clk_sel;
};

/* Shared structure between the PHYs of the same package. */
struct lan8814_shared_priv {
	struct phy_device *phydev;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_info;
	struct ptp_pin_desc *pin_config;

	/* Lock for ptp_clock */
	struct mutex shared_lock;
};

struct lan8814_ptp_rx_ts {
	struct list_head list;
	u32 seconds;
	u32 nsec;
	u16 seq_id;
};

struct kszphy_ptp_priv {
	struct mii_timestamper mii_ts;
	struct phy_device *phydev;

	struct sk_buff_head tx_queue;
	struct sk_buff_head rx_queue;

	struct list_head rx_ts_list;
	/* Lock for Rx ts fifo */
	spinlock_t rx_ts_lock;

	int hwts_tx_type;
	enum hwtstamp_rx_filters rx_filter;
	int layer;
	int version;

	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_info;
	/* Lock for ptp_clock */
	struct mutex ptp_lock;
	struct ptp_pin_desc *pin_config;

	s64 seconds;
	/* Lock for accessing seconds */
	spinlock_t seconds_lock;
};

struct kszphy_priv {
	struct kszphy_ptp_priv ptp_priv;
	const struct kszphy_type *type;
	struct clk *clk;
	int led_mode;
	u16 vct_ctrl1000;
	bool rmii_ref_clk_sel;
	bool rmii_ref_clk_sel_val;
	bool clk_enable;
	u64 stats[ARRAY_SIZE(kszphy_hw_stats)];
};

static const struct kszphy_type lan8814_type = {
	.led_mode_reg		= ~LAN8814_LED_CTRL_1,
	.cable_diag_reg		= LAN8814_CABLE_DIAG,
	.pair_mask		= LAN8814_WIRE_PAIR_MASK,
};

static const struct kszphy_type ksz886x_type = {
	.cable_diag_reg		= KSZ8081_LMD,
	.pair_mask		= KSZPHY_WIRE_PAIR_MASK,
};

static const struct kszphy_type ksz8021_type = {
	.led_mode_reg		= MII_KSZPHY_CTRL_2,
	.has_broadcast_disable	= true,
	.has_nand_tree_disable	= true,
	.has_rmii_ref_clk_sel	= true,
};

static const struct kszphy_type ksz8041_type = {
	.led_mode_reg		= MII_KSZPHY_CTRL_1,
};

static const struct kszphy_type ksz8051_type = {
	.led_mode_reg		= MII_KSZPHY_CTRL_2,
	.has_nand_tree_disable	= true,
};

static const struct kszphy_type ksz8081_type = {
	.led_mode_reg		= MII_KSZPHY_CTRL_2,
	.has_broadcast_disable	= true,
	.has_nand_tree_disable	= true,
	.has_rmii_ref_clk_sel	= true,
};

static const struct kszphy_type ks8737_type = {
	.interrupt_level_mask	= BIT(14),
};

static const struct kszphy_type ksz9021_type = {
	.interrupt_level_mask	= BIT(14),
};

static const struct kszphy_type ksz9131_type = {
	.interrupt_level_mask	= BIT(14),
	.disable_dll_tx_bit	= BIT(12),
	.disable_dll_rx_bit	= BIT(12),
	.disable_dll_mask	= BIT_MASK(12),
};

static const struct kszphy_type lan8841_type = {
	.disable_dll_tx_bit	= BIT(14),
	.disable_dll_rx_bit	= BIT(14),
	.disable_dll_mask	= BIT_MASK(14),
	.cable_diag_reg		= LAN8814_CABLE_DIAG,
	.pair_mask		= LAN8814_WIRE_PAIR_MASK,
};

static int kszphy_extended_write(struct phy_device *phydev,
				u32 regnum, u16 val)
{
	phy_write(phydev, MII_KSZPHY_EXTREG, KSZPHY_EXTREG_WRITE | regnum);
	return phy_write(phydev, MII_KSZPHY_EXTREG_WRITE, val);
}

static int kszphy_extended_read(struct phy_device *phydev,
				u32 regnum)
{
	phy_write(phydev, MII_KSZPHY_EXTREG, regnum);
	return phy_read(phydev, MII_KSZPHY_EXTREG_READ);
}

static int kszphy_ack_interrupt(struct phy_device *phydev)
{
	/* bit[7..0] int status, which is a read and clear register. */
	int rc;

	rc = phy_read(phydev, MII_KSZPHY_INTCS);

	return (rc < 0) ? rc : 0;
}

static int kszphy_config_intr(struct phy_device *phydev)
{
	const struct kszphy_type *type = phydev->drv->driver_data;
	int temp, err;
	u16 mask;

	if (type && type->interrupt_level_mask)
		mask = type->interrupt_level_mask;
	else
		mask = KSZPHY_CTRL_INT_ACTIVE_HIGH;

	/* set the interrupt pin active low */
	temp = phy_read(phydev, MII_KSZPHY_CTRL);
	if (temp < 0)
		return temp;
	temp &= ~mask;
	phy_write(phydev, MII_KSZPHY_CTRL, temp);

	/* enable / disable interrupts */
	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = kszphy_ack_interrupt(phydev);
		if (err)
			return err;

		err = phy_write(phydev, MII_KSZPHY_INTCS, KSZPHY_INTCS_ALL);
	} else {
		err = phy_write(phydev, MII_KSZPHY_INTCS, 0);
		if (err)
			return err;

		err = kszphy_ack_interrupt(phydev);
	}

	return err;
}

static irqreturn_t kszphy_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read(phydev, MII_KSZPHY_INTCS);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(irq_status & KSZPHY_INTCS_STATUS))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int kszphy_rmii_clk_sel(struct phy_device *phydev, bool val)
{
	int ctrl;

	ctrl = phy_read(phydev, MII_KSZPHY_CTRL);
	if (ctrl < 0)
		return ctrl;

	if (val)
		ctrl |= KSZPHY_RMII_REF_CLK_SEL;
	else
		ctrl &= ~KSZPHY_RMII_REF_CLK_SEL;

	return phy_write(phydev, MII_KSZPHY_CTRL, ctrl);
}

static int kszphy_setup_led(struct phy_device *phydev, u32 reg, int val)
{
	int rc, temp, shift;

	switch (reg) {
	case MII_KSZPHY_CTRL_1:
		shift = 14;
		break;
	case MII_KSZPHY_CTRL_2:
		shift = 4;
		break;
	default:
		return -EINVAL;
	}

	temp = phy_read(phydev, reg);
	if (temp < 0) {
		rc = temp;
		goto out;
	}

	temp &= ~(3 << shift);
	temp |= val << shift;
	rc = phy_write(phydev, reg, temp);
out:
	if (rc < 0)
		phydev_err(phydev, "failed to set led mode\n");

	return rc;
}

/* Disable PHY address 0 as the broadcast address, so that it can be used as a
 * unique (non-broadcast) address on a shared bus.
 */
static int kszphy_broadcast_disable(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, MII_KSZPHY_OMSO);
	if (ret < 0)
		goto out;

	ret = phy_write(phydev, MII_KSZPHY_OMSO, ret | KSZPHY_OMSO_B_CAST_OFF);
out:
	if (ret)
		phydev_err(phydev, "failed to disable broadcast address\n");

	return ret;
}

static int kszphy_nand_tree_disable(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, MII_KSZPHY_OMSO);
	if (ret < 0)
		goto out;

	if (!(ret & KSZPHY_OMSO_NAND_TREE_ON))
		return 0;

	ret = phy_write(phydev, MII_KSZPHY_OMSO,
			ret & ~KSZPHY_OMSO_NAND_TREE_ON);
out:
	if (ret)
		phydev_err(phydev, "failed to disable NAND tree mode\n");

	return ret;
}

/* Some config bits need to be set again on resume, handle them here. */
static int kszphy_config_reset(struct phy_device *phydev)
{
	struct kszphy_priv *priv = phydev->priv;
	int ret;

	if (priv->rmii_ref_clk_sel) {
		ret = kszphy_rmii_clk_sel(phydev, priv->rmii_ref_clk_sel_val);
		if (ret) {
			phydev_err(phydev,
				   "failed to set rmii reference clock\n");
			return ret;
		}
	}

	if (priv->type && priv->led_mode >= 0)
		kszphy_setup_led(phydev, priv->type->led_mode_reg, priv->led_mode);

	return 0;
}

static int kszphy_config_init(struct phy_device *phydev)
{
	struct kszphy_priv *priv = phydev->priv;
	const struct kszphy_type *type;

	if (!priv)
		return 0;

	type = priv->type;

	if (type && type->has_broadcast_disable)
		kszphy_broadcast_disable(phydev);

	if (type && type->has_nand_tree_disable)
		kszphy_nand_tree_disable(phydev);

	return kszphy_config_reset(phydev);
}

static int ksz8041_fiber_mode(struct phy_device *phydev)
{
	struct device_node *of_node = phydev->mdio.dev.of_node;

	return of_property_read_bool(of_node, "micrel,fiber-mode");
}

static int ksz8041_config_init(struct phy_device *phydev)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	/* Limit supported and advertised modes in fiber mode */
	if (ksz8041_fiber_mode(phydev)) {
		phydev->dev_flags |= MICREL_PHY_FXEN;
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT, mask);
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT, mask);

		linkmode_and(phydev->supported, phydev->supported, mask);
		linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
				 phydev->supported);
		linkmode_and(phydev->advertising, phydev->advertising, mask);
		linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
				 phydev->advertising);
		phydev->autoneg = AUTONEG_DISABLE;
	}

	return kszphy_config_init(phydev);
}

static int ksz8041_config_aneg(struct phy_device *phydev)
{
	/* Skip auto-negotiation in fiber mode */
	if (phydev->dev_flags & MICREL_PHY_FXEN) {
		phydev->speed = SPEED_100;
		return 0;
	}

	return genphy_config_aneg(phydev);
}

static int ksz8051_ksz8795_match_phy_device(struct phy_device *phydev,
					    const bool ksz_8051)
{
	int ret;

	if (!phy_id_compare(phydev->phy_id, PHY_ID_KSZ8051, MICREL_PHY_ID_MASK))
		return 0;

	ret = phy_read(phydev, MII_BMSR);
	if (ret < 0)
		return ret;

	/* KSZ8051 PHY and KSZ8794/KSZ8795/KSZ8765 switch share the same
	 * exact PHY ID. However, they can be told apart by the extended
	 * capability registers presence. The KSZ8051 PHY has them while
	 * the switch does not.
	 */
	ret &= BMSR_ERCAP;
	if (ksz_8051)
		return ret;
	else
		return !ret;
}

static int ksz8051_match_phy_device(struct phy_device *phydev)
{
	return ksz8051_ksz8795_match_phy_device(phydev, true);
}

static int ksz8081_config_init(struct phy_device *phydev)
{
	/* KSZPHY_OMSO_FACTORY_TEST is set at de-assertion of the reset line
	 * based on the RXER (KSZ8081RNA/RND) or TXC (KSZ8081MNX/RNB) pin. If a
	 * pull-down is missing, the factory test mode should be cleared by
	 * manually writing a 0.
	 */
	phy_clear_bits(phydev, MII_KSZPHY_OMSO, KSZPHY_OMSO_FACTORY_TEST);

	return kszphy_config_init(phydev);
}

static int ksz8081_config_mdix(struct phy_device *phydev, u8 ctrl)
{
	u16 val;

	switch (ctrl) {
	case ETH_TP_MDI:
		val = KSZ8081_CTRL2_DISABLE_AUTO_MDIX;
		break;
	case ETH_TP_MDI_X:
		val = KSZ8081_CTRL2_DISABLE_AUTO_MDIX |
			KSZ8081_CTRL2_MDI_MDI_X_SELECT;
		break;
	case ETH_TP_MDI_AUTO:
		val = 0;
		break;
	default:
		return 0;
	}

	return phy_modify(phydev, MII_KSZPHY_CTRL_2,
			  KSZ8081_CTRL2_HP_MDIX |
			  KSZ8081_CTRL2_MDI_MDI_X_SELECT |
			  KSZ8081_CTRL2_DISABLE_AUTO_MDIX,
			  KSZ8081_CTRL2_HP_MDIX | val);
}

static int ksz8081_config_aneg(struct phy_device *phydev)
{
	int ret;

	ret = genphy_config_aneg(phydev);
	if (ret)
		return ret;

	/* The MDI-X configuration is automatically changed by the PHY after
	 * switching from autoneg off to on. So, take MDI-X configuration under
	 * own control and set it after autoneg configuration was done.
	 */
	return ksz8081_config_mdix(phydev, phydev->mdix_ctrl);
}

static int ksz8081_mdix_update(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, MII_KSZPHY_CTRL_2);
	if (ret < 0)
		return ret;

	if (ret & KSZ8081_CTRL2_DISABLE_AUTO_MDIX) {
		if (ret & KSZ8081_CTRL2_MDI_MDI_X_SELECT)
			phydev->mdix_ctrl = ETH_TP_MDI_X;
		else
			phydev->mdix_ctrl = ETH_TP_MDI;
	} else {
		phydev->mdix_ctrl = ETH_TP_MDI_AUTO;
	}

	ret = phy_read(phydev, MII_KSZPHY_CTRL_1);
	if (ret < 0)
		return ret;

	if (ret & KSZ8081_CTRL1_MDIX_STAT)
		phydev->mdix = ETH_TP_MDI;
	else
		phydev->mdix = ETH_TP_MDI_X;

	return 0;
}

static int ksz8081_read_status(struct phy_device *phydev)
{
	int ret;

	ret = ksz8081_mdix_update(phydev);
	if (ret < 0)
		return ret;

	return genphy_read_status(phydev);
}

static int ksz8061_config_init(struct phy_device *phydev)
{
	int ret;

	/* Chip can be powered down by the bootstrap code. */
	ret = phy_read(phydev, MII_BMCR);
	if (ret < 0)
		return ret;
	if (ret & BMCR_PDOWN) {
		ret = phy_write(phydev, MII_BMCR, ret & ~BMCR_PDOWN);
		if (ret < 0)
			return ret;
		usleep_range(1000, 2000);
	}

	ret = phy_write_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_DEVID1, 0xB61A);
	if (ret)
		return ret;

	return kszphy_config_init(phydev);
}

static int ksz8795_match_phy_device(struct phy_device *phydev)
{
	return ksz8051_ksz8795_match_phy_device(phydev, false);
}

static int ksz9021_load_values_from_of(struct phy_device *phydev,
				       const struct device_node *of_node,
				       u16 reg,
				       const char *field1, const char *field2,
				       const char *field3, const char *field4)
{
	int val1 = -1;
	int val2 = -2;
	int val3 = -3;
	int val4 = -4;
	int newval;
	int matches = 0;

	if (!of_property_read_u32(of_node, field1, &val1))
		matches++;

	if (!of_property_read_u32(of_node, field2, &val2))
		matches++;

	if (!of_property_read_u32(of_node, field3, &val3))
		matches++;

	if (!of_property_read_u32(of_node, field4, &val4))
		matches++;

	if (!matches)
		return 0;

	if (matches < 4)
		newval = kszphy_extended_read(phydev, reg);
	else
		newval = 0;

	if (val1 != -1)
		newval = ((newval & 0xfff0) | ((val1 / PS_TO_REG) & 0xf) << 0);

	if (val2 != -2)
		newval = ((newval & 0xff0f) | ((val2 / PS_TO_REG) & 0xf) << 4);

	if (val3 != -3)
		newval = ((newval & 0xf0ff) | ((val3 / PS_TO_REG) & 0xf) << 8);

	if (val4 != -4)
		newval = ((newval & 0x0fff) | ((val4 / PS_TO_REG) & 0xf) << 12);

	return kszphy_extended_write(phydev, reg, newval);
}

static int ksz9021_config_init(struct phy_device *phydev)
{
	const struct device_node *of_node;
	const struct device *dev_walker;

	/* The Micrel driver has a deprecated option to place phy OF
	 * properties in the MAC node. Walk up the tree of devices to
	 * find a device with an OF node.
	 */
	dev_walker = &phydev->mdio.dev;
	do {
		of_node = dev_walker->of_node;
		dev_walker = dev_walker->parent;

	} while (!of_node && dev_walker);

	if (of_node) {
		ksz9021_load_values_from_of(phydev, of_node,
				    MII_KSZPHY_CLK_CONTROL_PAD_SKEW,
				    "txen-skew-ps", "txc-skew-ps",
				    "rxdv-skew-ps", "rxc-skew-ps");
		ksz9021_load_values_from_of(phydev, of_node,
				    MII_KSZPHY_RX_DATA_PAD_SKEW,
				    "rxd0-skew-ps", "rxd1-skew-ps",
				    "rxd2-skew-ps", "rxd3-skew-ps");
		ksz9021_load_values_from_of(phydev, of_node,
				    MII_KSZPHY_TX_DATA_PAD_SKEW,
				    "txd0-skew-ps", "txd1-skew-ps",
				    "txd2-skew-ps", "txd3-skew-ps");
	}
	return 0;
}

#define KSZ9031_PS_TO_REG		60

/* Extended registers */
/* MMD Address 0x0 */
#define MII_KSZ9031RN_FLP_BURST_TX_LO	3
#define MII_KSZ9031RN_FLP_BURST_TX_HI	4

/* MMD Address 0x2 */
#define MII_KSZ9031RN_CONTROL_PAD_SKEW	4
#define MII_KSZ9031RN_RX_CTL_M		GENMASK(7, 4)
#define MII_KSZ9031RN_TX_CTL_M		GENMASK(3, 0)

#define MII_KSZ9031RN_RX_DATA_PAD_SKEW	5
#define MII_KSZ9031RN_RXD3		GENMASK(15, 12)
#define MII_KSZ9031RN_RXD2		GENMASK(11, 8)
#define MII_KSZ9031RN_RXD1		GENMASK(7, 4)
#define MII_KSZ9031RN_RXD0		GENMASK(3, 0)

#define MII_KSZ9031RN_TX_DATA_PAD_SKEW	6
#define MII_KSZ9031RN_TXD3		GENMASK(15, 12)
#define MII_KSZ9031RN_TXD2		GENMASK(11, 8)
#define MII_KSZ9031RN_TXD1		GENMASK(7, 4)
#define MII_KSZ9031RN_TXD0		GENMASK(3, 0)

#define MII_KSZ9031RN_CLK_PAD_SKEW	8
#define MII_KSZ9031RN_GTX_CLK		GENMASK(9, 5)
#define MII_KSZ9031RN_RX_CLK		GENMASK(4, 0)

/* KSZ9031 has internal RGMII_IDRX = 1.2ns and RGMII_IDTX = 0ns. To
 * provide different RGMII options we need to configure delay offset
 * for each pad relative to build in delay.
 */
/* keep rx as "No delay adjustment" and set rx_clk to +0.60ns to get delays of
 * 1.80ns
 */
#define RX_ID				0x7
#define RX_CLK_ID			0x19

/* set rx to +0.30ns and rx_clk to -0.90ns to compensate the
 * internal 1.2ns delay.
 */
#define RX_ND				0xc
#define RX_CLK_ND			0x0

/* set tx to -0.42ns and tx_clk to +0.96ns to get 1.38ns delay */
#define TX_ID				0x0
#define TX_CLK_ID			0x1f

/* set tx and tx_clk to "No delay adjustment" to keep 0ns
 * dealy
 */
#define TX_ND				0x7
#define TX_CLK_ND			0xf

/* MMD Address 0x1C */
#define MII_KSZ9031RN_EDPD		0x23
#define MII_KSZ9031RN_EDPD_ENABLE	BIT(0)

static int ksz9031_set_loopback(struct phy_device *phydev, bool enable,
				int speed)
{
	u16 ctl = BMCR_LOOPBACK;
	int val;

	if (!enable)
		return genphy_loopback(phydev, enable, 0);

	if (speed == SPEED_10 || speed == SPEED_100 || speed == SPEED_1000)
		phydev->speed = speed;
	else if (speed)
		return -EINVAL;
	phydev->duplex = DUPLEX_FULL;

	ctl |= mii_bmcr_encode_fixed(phydev->speed, phydev->duplex);

	phy_write(phydev, MII_BMCR, ctl);

	return phy_read_poll_timeout(phydev, MII_BMSR, val, val & BMSR_LSTATUS,
				     5000, 500000, true);
}

static int ksz9031_of_load_skew_values(struct phy_device *phydev,
				       const struct device_node *of_node,
				       u16 reg, size_t field_sz,
				       const char *field[], u8 numfields,
				       bool *update)
{
	int val[4] = {-1, -2, -3, -4};
	int matches = 0;
	u16 mask;
	u16 maxval;
	u16 newval;
	int i;

	for (i = 0; i < numfields; i++)
		if (!of_property_read_u32(of_node, field[i], val + i))
			matches++;

	if (!matches)
		return 0;

	*update |= true;

	if (matches < numfields)
		newval = phy_read_mmd(phydev, 2, reg);
	else
		newval = 0;

	maxval = (field_sz == 4) ? 0xf : 0x1f;
	for (i = 0; i < numfields; i++)
		if (val[i] != -(i + 1)) {
			mask = 0xffff;
			mask ^= maxval << (field_sz * i);
			newval = (newval & mask) |
				(((val[i] / KSZ9031_PS_TO_REG) & maxval)
					<< (field_sz * i));
		}

	return phy_write_mmd(phydev, 2, reg, newval);
}

/* Center KSZ9031RNX FLP timing at 16ms. */
static int ksz9031_center_flp_timing(struct phy_device *phydev)
{
	int result;

	result = phy_write_mmd(phydev, 0, MII_KSZ9031RN_FLP_BURST_TX_HI,
			       0x0006);
	if (result)
		return result;

	result = phy_write_mmd(phydev, 0, MII_KSZ9031RN_FLP_BURST_TX_LO,
			       0x1A80);
	if (result)
		return result;

	return genphy_restart_aneg(phydev);
}

/* Enable energy-detect power-down mode */
static int ksz9031_enable_edpd(struct phy_device *phydev)
{
	int reg;

	reg = phy_read_mmd(phydev, 0x1C, MII_KSZ9031RN_EDPD);
	if (reg < 0)
		return reg;
	return phy_write_mmd(phydev, 0x1C, MII_KSZ9031RN_EDPD,
			     reg | MII_KSZ9031RN_EDPD_ENABLE);
}

static int ksz9031_config_rgmii_delay(struct phy_device *phydev)
{
	u16 rx, tx, rx_clk, tx_clk;
	int ret;

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		tx = TX_ND;
		tx_clk = TX_CLK_ND;
		rx = RX_ND;
		rx_clk = RX_CLK_ND;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		tx = TX_ID;
		tx_clk = TX_CLK_ID;
		rx = RX_ID;
		rx_clk = RX_CLK_ID;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		tx = TX_ND;
		tx_clk = TX_CLK_ND;
		rx = RX_ID;
		rx_clk = RX_CLK_ID;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		tx = TX_ID;
		tx_clk = TX_CLK_ID;
		rx = RX_ND;
		rx_clk = RX_CLK_ND;
		break;
	default:
		return 0;
	}

	ret = phy_write_mmd(phydev, 2, MII_KSZ9031RN_CONTROL_PAD_SKEW,
			    FIELD_PREP(MII_KSZ9031RN_RX_CTL_M, rx) |
			    FIELD_PREP(MII_KSZ9031RN_TX_CTL_M, tx));
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, 2, MII_KSZ9031RN_RX_DATA_PAD_SKEW,
			    FIELD_PREP(MII_KSZ9031RN_RXD3, rx) |
			    FIELD_PREP(MII_KSZ9031RN_RXD2, rx) |
			    FIELD_PREP(MII_KSZ9031RN_RXD1, rx) |
			    FIELD_PREP(MII_KSZ9031RN_RXD0, rx));
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, 2, MII_KSZ9031RN_TX_DATA_PAD_SKEW,
			    FIELD_PREP(MII_KSZ9031RN_TXD3, tx) |
			    FIELD_PREP(MII_KSZ9031RN_TXD2, tx) |
			    FIELD_PREP(MII_KSZ9031RN_TXD1, tx) |
			    FIELD_PREP(MII_KSZ9031RN_TXD0, tx));
	if (ret < 0)
		return ret;

	return phy_write_mmd(phydev, 2, MII_KSZ9031RN_CLK_PAD_SKEW,
			     FIELD_PREP(MII_KSZ9031RN_GTX_CLK, tx_clk) |
			     FIELD_PREP(MII_KSZ9031RN_RX_CLK, rx_clk));
}

static int ksz9031_config_init(struct phy_device *phydev)
{
	const struct device_node *of_node;
	static const char *clk_skews[2] = {"rxc-skew-ps", "txc-skew-ps"};
	static const char *rx_data_skews[4] = {
		"rxd0-skew-ps", "rxd1-skew-ps",
		"rxd2-skew-ps", "rxd3-skew-ps"
	};
	static const char *tx_data_skews[4] = {
		"txd0-skew-ps", "txd1-skew-ps",
		"txd2-skew-ps", "txd3-skew-ps"
	};
	static const char *control_skews[2] = {"txen-skew-ps", "rxdv-skew-ps"};
	const struct device *dev_walker;
	int result;

	result = ksz9031_enable_edpd(phydev);
	if (result < 0)
		return result;

	/* The Micrel driver has a deprecated option to place phy OF
	 * properties in the MAC node. Walk up the tree of devices to
	 * find a device with an OF node.
	 */
	dev_walker = &phydev->mdio.dev;
	do {
		of_node = dev_walker->of_node;
		dev_walker = dev_walker->parent;
	} while (!of_node && dev_walker);

	if (of_node) {
		bool update = false;

		if (phy_interface_is_rgmii(phydev)) {
			result = ksz9031_config_rgmii_delay(phydev);
			if (result < 0)
				return result;
		}

		ksz9031_of_load_skew_values(phydev, of_node,
				MII_KSZ9031RN_CLK_PAD_SKEW, 5,
				clk_skews, 2, &update);

		ksz9031_of_load_skew_values(phydev, of_node,
				MII_KSZ9031RN_CONTROL_PAD_SKEW, 4,
				control_skews, 2, &update);

		ksz9031_of_load_skew_values(phydev, of_node,
				MII_KSZ9031RN_RX_DATA_PAD_SKEW, 4,
				rx_data_skews, 4, &update);

		ksz9031_of_load_skew_values(phydev, of_node,
				MII_KSZ9031RN_TX_DATA_PAD_SKEW, 4,
				tx_data_skews, 4, &update);

		if (update && !phy_interface_is_rgmii(phydev))
			phydev_warn(phydev,
				    "*-skew-ps values should be used only with RGMII PHY modes\n");

		/* Silicon Errata Sheet (DS80000691D or DS80000692D):
		 * When the device links in the 1000BASE-T slave mode only,
		 * the optional 125MHz reference output clock (CLK125_NDO)
		 * has wide duty cycle variation.
		 *
		 * The optional CLK125_NDO clock does not meet the RGMII
		 * 45/55 percent (min/max) duty cycle requirement and therefore
		 * cannot be used directly by the MAC side for clocking
		 * applications that have setup/hold time requirements on
		 * rising and falling clock edges.
		 *
		 * Workaround:
		 * Force the phy to be the master to receive a stable clock
		 * which meets the duty cycle requirement.
		 */
		if (of_property_read_bool(of_node, "micrel,force-master")) {
			result = phy_read(phydev, MII_CTRL1000);
			if (result < 0)
				goto err_force_master;

			/* enable master mode, config & prefer master */
			result |= CTL1000_ENABLE_MASTER | CTL1000_AS_MASTER;
			result = phy_write(phydev, MII_CTRL1000, result);
			if (result < 0)
				goto err_force_master;
		}
	}

	return ksz9031_center_flp_timing(phydev);

err_force_master:
	phydev_err(phydev, "failed to force the phy to master mode\n");
	return result;
}

#define KSZ9131_SKEW_5BIT_MAX	2400
#define KSZ9131_SKEW_4BIT_MAX	800
#define KSZ9131_OFFSET		700
#define KSZ9131_STEP		100

static int ksz9131_of_load_skew_values(struct phy_device *phydev,
				       struct device_node *of_node,
				       u16 reg, size_t field_sz,
				       char *field[], u8 numfields)
{
	int val[4] = {-(1 + KSZ9131_OFFSET), -(2 + KSZ9131_OFFSET),
		      -(3 + KSZ9131_OFFSET), -(4 + KSZ9131_OFFSET)};
	int skewval, skewmax = 0;
	int matches = 0;
	u16 maxval;
	u16 newval;
	u16 mask;
	int i;

	/* psec properties in dts should mean x pico seconds */
	if (field_sz == 5)
		skewmax = KSZ9131_SKEW_5BIT_MAX;
	else
		skewmax = KSZ9131_SKEW_4BIT_MAX;

	for (i = 0; i < numfields; i++)
		if (!of_property_read_s32(of_node, field[i], &skewval)) {
			if (skewval < -KSZ9131_OFFSET)
				skewval = -KSZ9131_OFFSET;
			else if (skewval > skewmax)
				skewval = skewmax;

			val[i] = skewval + KSZ9131_OFFSET;
			matches++;
		}

	if (!matches)
		return 0;

	if (matches < numfields)
		newval = phy_read_mmd(phydev, 2, reg);
	else
		newval = 0;

	maxval = (field_sz == 4) ? 0xf : 0x1f;
	for (i = 0; i < numfields; i++)
		if (val[i] != -(i + 1 + KSZ9131_OFFSET)) {
			mask = 0xffff;
			mask ^= maxval << (field_sz * i);
			newval = (newval & mask) |
				(((val[i] / KSZ9131_STEP) & maxval)
					<< (field_sz * i));
		}

	return phy_write_mmd(phydev, 2, reg, newval);
}

#define KSZ9131RN_MMD_COMMON_CTRL_REG	2
#define KSZ9131RN_RXC_DLL_CTRL		76
#define KSZ9131RN_TXC_DLL_CTRL		77
#define KSZ9131RN_DLL_ENABLE_DELAY	0

static int ksz9131_config_rgmii_delay(struct phy_device *phydev)
{
	const struct kszphy_type *type = phydev->drv->driver_data;
	u16 rxcdll_val, txcdll_val;
	int ret;

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		rxcdll_val = type->disable_dll_rx_bit;
		txcdll_val = type->disable_dll_tx_bit;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		rxcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		txcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		rxcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		txcdll_val = type->disable_dll_tx_bit;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		rxcdll_val = type->disable_dll_rx_bit;
		txcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		break;
	default:
		return 0;
	}

	ret = phy_modify_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
			     KSZ9131RN_RXC_DLL_CTRL, type->disable_dll_mask,
			     rxcdll_val);
	if (ret < 0)
		return ret;

	return phy_modify_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
			      KSZ9131RN_TXC_DLL_CTRL, type->disable_dll_mask,
			      txcdll_val);
}

/* Silicon Errata DS80000693B
 *
 * When LEDs are configured in Individual Mode, LED1 is ON in a no-link
 * condition. Workaround is to set register 0x1e, bit 9, this way LED1 behaves
 * according to the datasheet (off if there is no link).
 */
static int ksz9131_led_errata(struct phy_device *phydev)
{
	int reg;

	reg = phy_read_mmd(phydev, 2, 0);
	if (reg < 0)
		return reg;

	if (!(reg & BIT(4)))
		return 0;

	return phy_set_bits(phydev, 0x1e, BIT(9));
}

static int ksz9131_config_init(struct phy_device *phydev)
{
	struct device_node *of_node;
	char *clk_skews[2] = {"rxc-skew-psec", "txc-skew-psec"};
	char *rx_data_skews[4] = {
		"rxd0-skew-psec", "rxd1-skew-psec",
		"rxd2-skew-psec", "rxd3-skew-psec"
	};
	char *tx_data_skews[4] = {
		"txd0-skew-psec", "txd1-skew-psec",
		"txd2-skew-psec", "txd3-skew-psec"
	};
	char *control_skews[2] = {"txen-skew-psec", "rxdv-skew-psec"};
	const struct device *dev_walker;
	int ret;

	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;

	dev_walker = &phydev->mdio.dev;
	do {
		of_node = dev_walker->of_node;
		dev_walker = dev_walker->parent;
	} while (!of_node && dev_walker);

	if (!of_node)
		return 0;

	if (phy_interface_is_rgmii(phydev)) {
		ret = ksz9131_config_rgmii_delay(phydev);
		if (ret < 0)
			return ret;
	}

	ret = ksz9131_of_load_skew_values(phydev, of_node,
					  MII_KSZ9031RN_CLK_PAD_SKEW, 5,
					  clk_skews, 2);
	if (ret < 0)
		return ret;

	ret = ksz9131_of_load_skew_values(phydev, of_node,
					  MII_KSZ9031RN_CONTROL_PAD_SKEW, 4,
					  control_skews, 2);
	if (ret < 0)
		return ret;

	ret = ksz9131_of_load_skew_values(phydev, of_node,
					  MII_KSZ9031RN_RX_DATA_PAD_SKEW, 4,
					  rx_data_skews, 4);
	if (ret < 0)
		return ret;

	ret = ksz9131_of_load_skew_values(phydev, of_node,
					  MII_KSZ9031RN_TX_DATA_PAD_SKEW, 4,
					  tx_data_skews, 4);
	if (ret < 0)
		return ret;

	ret = ksz9131_led_errata(phydev);
	if (ret < 0)
		return ret;

	return 0;
}

#define MII_KSZ9131_AUTO_MDIX		0x1C
#define MII_KSZ9131_AUTO_MDI_SET	BIT(7)
#define MII_KSZ9131_AUTO_MDIX_SWAP_OFF	BIT(6)
#define MII_KSZ9131_DIG_AXAN_STS	0x14
#define MII_KSZ9131_DIG_AXAN_STS_LINK_DET	BIT(14)
#define MII_KSZ9131_DIG_AXAN_STS_A_SELECT	BIT(12)

static int ksz9131_mdix_update(struct phy_device *phydev)
{
	int ret;

	if (phydev->mdix_ctrl != ETH_TP_MDI_AUTO) {
		phydev->mdix = phydev->mdix_ctrl;
	} else {
		ret = phy_read(phydev, MII_KSZ9131_DIG_AXAN_STS);
		if (ret < 0)
			return ret;

		if (ret & MII_KSZ9131_DIG_AXAN_STS_LINK_DET) {
			if (ret & MII_KSZ9131_DIG_AXAN_STS_A_SELECT)
				phydev->mdix = ETH_TP_MDI;
			else
				phydev->mdix = ETH_TP_MDI_X;
		} else {
			phydev->mdix = ETH_TP_MDI_INVALID;
		}
	}

	return 0;
}

static int ksz9131_config_mdix(struct phy_device *phydev, u8 ctrl)
{
	u16 val;

	switch (ctrl) {
	case ETH_TP_MDI:
		val = MII_KSZ9131_AUTO_MDIX_SWAP_OFF |
		      MII_KSZ9131_AUTO_MDI_SET;
		break;
	case ETH_TP_MDI_X:
		val = MII_KSZ9131_AUTO_MDIX_SWAP_OFF;
		break;
	case ETH_TP_MDI_AUTO:
		val = 0;
		break;
	default:
		return 0;
	}

	return phy_modify(phydev, MII_KSZ9131_AUTO_MDIX,
			  MII_KSZ9131_AUTO_MDIX_SWAP_OFF |
			  MII_KSZ9131_AUTO_MDI_SET, val);
}

static int ksz9131_read_status(struct phy_device *phydev)
{
	int ret;

	ret = ksz9131_mdix_update(phydev);
	if (ret < 0)
		return ret;

	return genphy_read_status(phydev);
}

static int ksz9131_config_aneg(struct phy_device *phydev)
{
	int ret;

	ret = ksz9131_config_mdix(phydev, phydev->mdix_ctrl);
	if (ret)
		return ret;

	return genphy_config_aneg(phydev);
}

static int ksz9477_get_features(struct phy_device *phydev)
{
	int ret;

	ret = genphy_read_abilities(phydev);
	if (ret)
		return ret;

	/* The "EEE control and capability 1" (Register 3.20) seems to be
	 * influenced by the "EEE advertisement 1" (Register 7.60). Changes
	 * on the 7.60 will affect 3.20. So, we need to construct our own list
	 * of caps.
	 * KSZ8563R should have 100BaseTX/Full only.
	 */
	linkmode_and(phydev->supported_eee, phydev->supported,
		     PHY_EEE_CAP1_FEATURES);

	return 0;
}

#define KSZ8873MLL_GLOBAL_CONTROL_4	0x06
#define KSZ8873MLL_GLOBAL_CONTROL_4_DUPLEX	BIT(6)
#define KSZ8873MLL_GLOBAL_CONTROL_4_SPEED	BIT(4)
static int ksz8873mll_read_status(struct phy_device *phydev)
{
	int regval;

	/* dummy read */
	regval = phy_read(phydev, KSZ8873MLL_GLOBAL_CONTROL_4);

	regval = phy_read(phydev, KSZ8873MLL_GLOBAL_CONTROL_4);

	if (regval & KSZ8873MLL_GLOBAL_CONTROL_4_DUPLEX)
		phydev->duplex = DUPLEX_HALF;
	else
		phydev->duplex = DUPLEX_FULL;

	if (regval & KSZ8873MLL_GLOBAL_CONTROL_4_SPEED)
		phydev->speed = SPEED_10;
	else
		phydev->speed = SPEED_100;

	phydev->link = 1;
	phydev->pause = phydev->asym_pause = 0;

	return 0;
}

static int ksz9031_get_features(struct phy_device *phydev)
{
	int ret;

	ret = genphy_read_abilities(phydev);
	if (ret < 0)
		return ret;

	/* Silicon Errata Sheet (DS80000691D or DS80000692D):
	 * Whenever the device's Asymmetric Pause capability is set to 1,
	 * link-up may fail after a link-up to link-down transition.
	 *
	 * The Errata Sheet is for ksz9031, but ksz9021 has the same issue
	 *
	 * Workaround:
	 * Do not enable the Asymmetric Pause capability bit.
	 */
	linkmode_clear_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, phydev->supported);

	/* We force setting the Pause capability as the core will force the
	 * Asymmetric Pause capability to 1 otherwise.
	 */
	linkmode_set_bit(ETHTOOL_LINK_MODE_Pause_BIT, phydev->supported);

	return 0;
}

static int ksz9031_read_status(struct phy_device *phydev)
{
	int err;
	int regval;

	err = genphy_read_status(phydev);
	if (err)
		return err;

	/* Make sure the PHY is not broken. Read idle error count,
	 * and reset the PHY if it is maxed out.
	 */
	regval = phy_read(phydev, MII_STAT1000);
	if ((regval & 0xFF) == 0xFF) {
		phy_init_hw(phydev);
		phydev->link = 0;
		if (phydev->drv->config_intr && phy_interrupt_is_valid(phydev))
			phydev->drv->config_intr(phydev);
		return genphy_config_aneg(phydev);
	}

	return 0;
}

static int ksz9x31_cable_test_start(struct phy_device *phydev)
{
	struct kszphy_priv *priv = phydev->priv;
	int ret;

	/* KSZ9131RNX, DS00002841B-page 38, 4.14 LinkMD (R) Cable Diagnostic
	 * Prior to running the cable diagnostics, Auto-negotiation should
	 * be disabled, full duplex set and the link speed set to 1000Mbps
	 * via the Basic Control Register.
	 */
	ret = phy_modify(phydev, MII_BMCR,
			 BMCR_SPEED1000 | BMCR_FULLDPLX |
			 BMCR_ANENABLE | BMCR_SPEED100,
			 BMCR_SPEED1000 | BMCR_FULLDPLX);
	if (ret)
		return ret;

	/* KSZ9131RNX, DS00002841B-page 38, 4.14 LinkMD (R) Cable Diagnostic
	 * The Master-Slave configuration should be set to Slave by writing
	 * a value of 0x1000 to the Auto-Negotiation Master Slave Control
	 * Register.
	 */
	ret = phy_read(phydev, MII_CTRL1000);
	if (ret < 0)
		return ret;

	/* Cache these bits, they need to be restored once LinkMD finishes. */
	priv->vct_ctrl1000 = ret & (CTL1000_ENABLE_MASTER | CTL1000_AS_MASTER);
	ret &= ~(CTL1000_ENABLE_MASTER | CTL1000_AS_MASTER);
	ret |= CTL1000_ENABLE_MASTER;

	return phy_write(phydev, MII_CTRL1000, ret);
}

static int ksz9x31_cable_test_result_trans(u16 status)
{
	switch (FIELD_GET(KSZ9x31_LMD_VCT_ST_MASK, status)) {
	case KSZ9x31_LMD_VCT_ST_NORMAL:
		return ETHTOOL_A_CABLE_RESULT_CODE_OK;
	case KSZ9x31_LMD_VCT_ST_OPEN:
		return ETHTOOL_A_CABLE_RESULT_CODE_OPEN;
	case KSZ9x31_LMD_VCT_ST_SHORT:
		return ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT;
	case KSZ9x31_LMD_VCT_ST_FAIL:
		fallthrough;
	default:
		return ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	}
}

static bool ksz9x31_cable_test_failed(u16 status)
{
	int stat = FIELD_GET(KSZ9x31_LMD_VCT_ST_MASK, status);

	return stat == KSZ9x31_LMD_VCT_ST_FAIL;
}

static bool ksz9x31_cable_test_fault_length_valid(u16 status)
{
	switch (FIELD_GET(KSZ9x31_LMD_VCT_ST_MASK, status)) {
	case KSZ9x31_LMD_VCT_ST_OPEN:
		fallthrough;
	case KSZ9x31_LMD_VCT_ST_SHORT:
		return true;
	}
	return false;
}

static int ksz9x31_cable_test_fault_length(struct phy_device *phydev, u16 stat)
{
	int dt = FIELD_GET(KSZ9x31_LMD_VCT_DATA_MASK, stat);

	/* KSZ9131RNX, DS00002841B-page 38, 4.14 LinkMD (R) Cable Diagnostic
	 *
	 * distance to fault = (VCT_DATA - 22) * 4 / cable propagation velocity
	 */
	if (phydev_id_compare(phydev, PHY_ID_KSZ9131))
		dt = clamp(dt - 22, 0, 255);

	return (dt * 400) / 10;
}

static int ksz9x31_cable_test_wait_for_completion(struct phy_device *phydev)
{
	int val, ret;

	ret = phy_read_poll_timeout(phydev, KSZ9x31_LMD, val,
				    !(val & KSZ9x31_LMD_VCT_EN),
				    30000, 100000, true);

	return ret < 0 ? ret : 0;
}

static int ksz9x31_cable_test_get_pair(int pair)
{
	static const int ethtool_pair[] = {
		ETHTOOL_A_CABLE_PAIR_A,
		ETHTOOL_A_CABLE_PAIR_B,
		ETHTOOL_A_CABLE_PAIR_C,
		ETHTOOL_A_CABLE_PAIR_D,
	};

	return ethtool_pair[pair];
}

static int ksz9x31_cable_test_one_pair(struct phy_device *phydev, int pair)
{
	int ret, val;

	/* KSZ9131RNX, DS00002841B-page 38, 4.14 LinkMD (R) Cable Diagnostic
	 * To test each individual cable pair, set the cable pair in the Cable
	 * Diagnostics Test Pair (VCT_PAIR[1:0]) field of the LinkMD Cable
	 * Diagnostic Register, along with setting the Cable Diagnostics Test
	 * Enable (VCT_EN) bit. The Cable Diagnostics Test Enable (VCT_EN) bit
	 * will self clear when the test is concluded.
	 */
	ret = phy_write(phydev, KSZ9x31_LMD,
			KSZ9x31_LMD_VCT_EN | KSZ9x31_LMD_VCT_PAIR(pair));
	if (ret)
		return ret;

	ret = ksz9x31_cable_test_wait_for_completion(phydev);
	if (ret)
		return ret;

	val = phy_read(phydev, KSZ9x31_LMD);
	if (val < 0)
		return val;

	if (ksz9x31_cable_test_failed(val))
		return -EAGAIN;

	ret = ethnl_cable_test_result(phydev,
				      ksz9x31_cable_test_get_pair(pair),
				      ksz9x31_cable_test_result_trans(val));
	if (ret)
		return ret;

	if (!ksz9x31_cable_test_fault_length_valid(val))
		return 0;

	return ethnl_cable_test_fault_length(phydev,
					     ksz9x31_cable_test_get_pair(pair),
					     ksz9x31_cable_test_fault_length(phydev, val));
}

static int ksz9x31_cable_test_get_status(struct phy_device *phydev,
					 bool *finished)
{
	struct kszphy_priv *priv = phydev->priv;
	unsigned long pair_mask = 0xf;
	int retries = 20;
	int pair, ret, rv;

	*finished = false;

	/* Try harder if link partner is active */
	while (pair_mask && retries--) {
		for_each_set_bit(pair, &pair_mask, 4) {
			ret = ksz9x31_cable_test_one_pair(phydev, pair);
			if (ret == -EAGAIN)
				continue;
			if (ret < 0)
				return ret;
			clear_bit(pair, &pair_mask);
		}
		/* If link partner is in autonegotiation mode it will send 2ms
		 * of FLPs with at least 6ms of silence.
		 * Add 2ms sleep to have better chances to hit this silence.
		 */
		if (pair_mask)
			usleep_range(2000, 3000);
	}

	/* Report remaining unfinished pair result as unknown. */
	for_each_set_bit(pair, &pair_mask, 4) {
		ret = ethnl_cable_test_result(phydev,
					      ksz9x31_cable_test_get_pair(pair),
					      ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC);
	}

	*finished = true;

	/* Restore cached bits from before LinkMD got started. */
	rv = phy_modify(phydev, MII_CTRL1000,
			CTL1000_ENABLE_MASTER | CTL1000_AS_MASTER,
			priv->vct_ctrl1000);
	if (rv)
		return rv;

	return ret;
}

static int ksz8873mll_config_aneg(struct phy_device *phydev)
{
	return 0;
}

static int ksz886x_config_mdix(struct phy_device *phydev, u8 ctrl)
{
	u16 val;

	switch (ctrl) {
	case ETH_TP_MDI:
		val = KSZ886X_BMCR_DISABLE_AUTO_MDIX;
		break;
	case ETH_TP_MDI_X:
		/* Note: The naming of the bit KSZ886X_BMCR_FORCE_MDI is bit
		 * counter intuitive, the "-X" in "1 = Force MDI" in the data
		 * sheet seems to be missing:
		 * 1 = Force MDI (sic!) (transmit on RX+/RX- pins)
		 * 0 = Normal operation (transmit on TX+/TX- pins)
		 */
		val = KSZ886X_BMCR_DISABLE_AUTO_MDIX | KSZ886X_BMCR_FORCE_MDI;
		break;
	case ETH_TP_MDI_AUTO:
		val = 0;
		break;
	default:
		return 0;
	}

	return phy_modify(phydev, MII_BMCR,
			  KSZ886X_BMCR_HP_MDIX | KSZ886X_BMCR_FORCE_MDI |
			  KSZ886X_BMCR_DISABLE_AUTO_MDIX,
			  KSZ886X_BMCR_HP_MDIX | val);
}

static int ksz886x_config_aneg(struct phy_device *phydev)
{
	int ret;

	ret = genphy_config_aneg(phydev);
	if (ret)
		return ret;

	if (phydev->autoneg != AUTONEG_ENABLE) {
		/* When autonegotation is disabled, we need to manually force
		 * the link state. If we don't do this, the PHY will keep
		 * sending Fast Link Pulses (FLPs) which are part of the
		 * autonegotiation process. This is not desired when
		 * autonegotiation is off.
		 */
		ret = phy_set_bits(phydev, MII_KSZPHY_CTRL,
				   KSZ886X_CTRL_FORCE_LINK);
		if (ret)
			return ret;
	} else {
		/* If we had previously forced the link state, we need to
		 * clear KSZ886X_CTRL_FORCE_LINK bit now. Otherwise, the PHY
		 * will not perform autonegotiation.
		 */
		ret = phy_clear_bits(phydev, MII_KSZPHY_CTRL,
				     KSZ886X_CTRL_FORCE_LINK);
		if (ret)
			return ret;
	}

	/* The MDI-X configuration is automatically changed by the PHY after
	 * switching from autoneg off to on. So, take MDI-X configuration under
	 * own control and set it after autoneg configuration was done.
	 */
	return ksz886x_config_mdix(phydev, phydev->mdix_ctrl);
}

static int ksz886x_mdix_update(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, MII_BMCR);
	if (ret < 0)
		return ret;

	if (ret & KSZ886X_BMCR_DISABLE_AUTO_MDIX) {
		if (ret & KSZ886X_BMCR_FORCE_MDI)
			phydev->mdix_ctrl = ETH_TP_MDI_X;
		else
			phydev->mdix_ctrl = ETH_TP_MDI;
	} else {
		phydev->mdix_ctrl = ETH_TP_MDI_AUTO;
	}

	ret = phy_read(phydev, MII_KSZPHY_CTRL);
	if (ret < 0)
		return ret;

	/* Same reverse logic as KSZ886X_BMCR_FORCE_MDI */
	if (ret & KSZ886X_CTRL_MDIX_STAT)
		phydev->mdix = ETH_TP_MDI_X;
	else
		phydev->mdix = ETH_TP_MDI;

	return 0;
}

static int ksz886x_read_status(struct phy_device *phydev)
{
	int ret;

	ret = ksz886x_mdix_update(phydev);
	if (ret < 0)
		return ret;

	return genphy_read_status(phydev);
}

struct ksz9477_errata_write {
	u8 dev_addr;
	u8 reg_addr;
	u16 val;
};

static const struct ksz9477_errata_write ksz9477_errata_writes[] = {
	 /* Register settings are needed to improve PHY receive performance */
	{0x01, 0x6f, 0xdd0b},
	{0x01, 0x8f, 0x6032},
	{0x01, 0x9d, 0x248c},
	{0x01, 0x75, 0x0060},
	{0x01, 0xd3, 0x7777},
	{0x1c, 0x06, 0x3008},
	{0x1c, 0x08, 0x2000},

	/* Transmit waveform amplitude can be improved (1000BASE-T, 100BASE-TX, 10BASE-Te) */
	{0x1c, 0x04, 0x00d0},

	/* Register settings are required to meet data sheet supply current specifications */
	{0x1c, 0x13, 0x6eff},
	{0x1c, 0x14, 0xe6ff},
	{0x1c, 0x15, 0x6eff},
	{0x1c, 0x16, 0xe6ff},
	{0x1c, 0x17, 0x00ff},
	{0x1c, 0x18, 0x43ff},
	{0x1c, 0x19, 0xc3ff},
	{0x1c, 0x1a, 0x6fff},
	{0x1c, 0x1b, 0x07ff},
	{0x1c, 0x1c, 0x0fff},
	{0x1c, 0x1d, 0xe7ff},
	{0x1c, 0x1e, 0xefff},
	{0x1c, 0x20, 0xeeee},
};

static int ksz9477_phy_errata(struct phy_device *phydev)
{
	int err;
	int i;

	/* Apply PHY settings to address errata listed in
	 * KSZ9477, KSZ9897, KSZ9896, KSZ9567, KSZ8565
	 * Silicon Errata and Data Sheet Clarification documents.
	 *
	 * Document notes: Before configuring the PHY MMD registers, it is
	 * necessary to set the PHY to 100 Mbps speed with auto-negotiation
	 * disabled by writing to register 0xN100-0xN101. After writing the
	 * MMD registers, and after all errata workarounds that involve PHY
	 * register settings, write register 0xN100-0xN101 again to enable
	 * and restart auto-negotiation.
	 */
	err = phy_write(phydev, MII_BMCR, BMCR_SPEED100 | BMCR_FULLDPLX);
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(ksz9477_errata_writes); ++i) {
		const struct ksz9477_errata_write *errata = &ksz9477_errata_writes[i];

		err = phy_write_mmd(phydev, errata->dev_addr, errata->reg_addr, errata->val);
		if (err)
			return err;
	}

	err = genphy_restart_aneg(phydev);
	if (err)
		return err;

	return err;
}

static int ksz9477_config_init(struct phy_device *phydev)
{
	int err;

	/* Only KSZ9897 family of switches needs this fix. */
	if ((phydev->phy_id & 0xf) == 1) {
		err = ksz9477_phy_errata(phydev);
		if (err)
			return err;
	}

	/* According to KSZ9477 Errata DS80000754C (Module 4) all EEE modes
	 * in this switch shall be regarded as broken.
	 */
	if (phydev->dev_flags & MICREL_NO_EEE)
		phy_disable_eee(phydev);

	return kszphy_config_init(phydev);
}

static int kszphy_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(kszphy_hw_stats);
}

static void kszphy_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kszphy_hw_stats); i++)
		ethtool_puts(&data, kszphy_hw_stats[i].string);
}

static u64 kszphy_get_stat(struct phy_device *phydev, int i)
{
	struct kszphy_hw_stat stat = kszphy_hw_stats[i];
	struct kszphy_priv *priv = phydev->priv;
	int val;
	u64 ret;

	val = phy_read(phydev, stat.reg);
	if (val < 0) {
		ret = U64_MAX;
	} else {
		val = val & ((1 << stat.bits) - 1);
		priv->stats[i] += val;
		ret = priv->stats[i];
	}

	return ret;
}

static void kszphy_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kszphy_hw_stats); i++)
		data[i] = kszphy_get_stat(phydev, i);
}

static void kszphy_enable_clk(struct phy_device *phydev)
{
	struct kszphy_priv *priv = phydev->priv;

	if (!priv->clk_enable && priv->clk) {
		clk_prepare_enable(priv->clk);
		priv->clk_enable = true;
	}
}

static void kszphy_disable_clk(struct phy_device *phydev)
{
	struct kszphy_priv *priv = phydev->priv;

	if (priv->clk_enable && priv->clk) {
		clk_disable_unprepare(priv->clk);
		priv->clk_enable = false;
	}
}

static int kszphy_generic_resume(struct phy_device *phydev)
{
	kszphy_enable_clk(phydev);

	return genphy_resume(phydev);
}

static int kszphy_generic_suspend(struct phy_device *phydev)
{
	int ret;

	ret = genphy_suspend(phydev);
	if (ret)
		return ret;

	kszphy_disable_clk(phydev);

	return 0;
}

static int kszphy_suspend(struct phy_device *phydev)
{
	/* Disable PHY Interrupts */
	if (phy_interrupt_is_valid(phydev)) {
		phydev->interrupts = PHY_INTERRUPT_DISABLED;
		if (phydev->drv->config_intr)
			phydev->drv->config_intr(phydev);
	}

	return kszphy_generic_suspend(phydev);
}

static void kszphy_parse_led_mode(struct phy_device *phydev)
{
	const struct kszphy_type *type = phydev->drv->driver_data;
	const struct device_node *np = phydev->mdio.dev.of_node;
	struct kszphy_priv *priv = phydev->priv;
	int ret;

	if (type && type->led_mode_reg) {
		ret = of_property_read_u32(np, "micrel,led-mode",
					   &priv->led_mode);

		if (ret)
			priv->led_mode = -1;

		if (priv->led_mode > 3) {
			phydev_err(phydev, "invalid led mode: 0x%02x\n",
				   priv->led_mode);
			priv->led_mode = -1;
		}
	} else {
		priv->led_mode = -1;
	}
}

static int kszphy_resume(struct phy_device *phydev)
{
	int ret;

	ret = kszphy_generic_resume(phydev);
	if (ret)
		return ret;

	/* After switching from power-down to normal mode, an internal global
	 * reset is automatically generated. Wait a minimum of 1 ms before
	 * read/write access to the PHY registers.
	 */
	usleep_range(1000, 2000);

	ret = kszphy_config_reset(phydev);
	if (ret)
		return ret;

	/* Enable PHY Interrupts */
	if (phy_interrupt_is_valid(phydev)) {
		phydev->interrupts = PHY_INTERRUPT_ENABLED;
		if (phydev->drv->config_intr)
			phydev->drv->config_intr(phydev);
	}

	return 0;
}

/* Because of errata DS80000700A, receiver error following software
 * power down. Suspend and resume callbacks only disable and enable
 * external rmii reference clock.
 */
static int ksz8041_resume(struct phy_device *phydev)
{
	kszphy_enable_clk(phydev);

	return 0;
}

static int ksz8041_suspend(struct phy_device *phydev)
{
	kszphy_disable_clk(phydev);

	return 0;
}

static int ksz9477_resume(struct phy_device *phydev)
{
	int ret;

	/* No need to initialize registers if not powered down. */
	ret = phy_read(phydev, MII_BMCR);
	if (ret < 0)
		return ret;
	if (!(ret & BMCR_PDOWN))
		return 0;

	genphy_resume(phydev);

	/* After switching from power-down to normal mode, an internal global
	 * reset is automatically generated. Wait a minimum of 1 ms before
	 * read/write access to the PHY registers.
	 */
	usleep_range(1000, 2000);

	/* Only KSZ9897 family of switches needs this fix. */
	if ((phydev->phy_id & 0xf) == 1) {
		ret = ksz9477_phy_errata(phydev);
		if (ret)
			return ret;
	}

	/* Enable PHY Interrupts */
	if (phy_interrupt_is_valid(phydev)) {
		phydev->interrupts = PHY_INTERRUPT_ENABLED;
		if (phydev->drv->config_intr)
			phydev->drv->config_intr(phydev);
	}

	return 0;
}

static int ksz8061_resume(struct phy_device *phydev)
{
	int ret;

	/* This function can be called twice when the Ethernet device is on. */
	ret = phy_read(phydev, MII_BMCR);
	if (ret < 0)
		return ret;
	if (!(ret & BMCR_PDOWN))
		return 0;

	ret = kszphy_generic_resume(phydev);
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	/* Re-program the value after chip is reset. */
	ret = phy_write_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_DEVID1, 0xB61A);
	if (ret)
		return ret;

	/* Enable PHY Interrupts */
	if (phy_interrupt_is_valid(phydev)) {
		phydev->interrupts = PHY_INTERRUPT_ENABLED;
		if (phydev->drv->config_intr)
			phydev->drv->config_intr(phydev);
	}

	return 0;
}

static int ksz8061_suspend(struct phy_device *phydev)
{
	return kszphy_suspend(phydev);
}

static int kszphy_probe(struct phy_device *phydev)
{
	const struct kszphy_type *type = phydev->drv->driver_data;
	const struct device_node *np = phydev->mdio.dev.of_node;
	struct kszphy_priv *priv;
	struct clk *clk;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	priv->type = type;

	kszphy_parse_led_mode(phydev);

	clk = devm_clk_get_optional_enabled(&phydev->mdio.dev, "rmii-ref");
	/* NOTE: clk may be NULL if building without CONFIG_HAVE_CLK */
	if (!IS_ERR_OR_NULL(clk)) {
		unsigned long rate = clk_get_rate(clk);
		bool rmii_ref_clk_sel_25_mhz;

		if (type)
			priv->rmii_ref_clk_sel = type->has_rmii_ref_clk_sel;
		rmii_ref_clk_sel_25_mhz = of_property_read_bool(np,
				"micrel,rmii-reference-clock-select-25-mhz");

		if (rate > 24500000 && rate < 25500000) {
			priv->rmii_ref_clk_sel_val = rmii_ref_clk_sel_25_mhz;
		} else if (rate > 49500000 && rate < 50500000) {
			priv->rmii_ref_clk_sel_val = !rmii_ref_clk_sel_25_mhz;
		} else {
			phydev_err(phydev, "Clock rate out of range: %ld\n",
				   rate);
			return -EINVAL;
		}
	} else if (!clk) {
		/* unnamed clock from the generic ethernet-phy binding */
		clk = devm_clk_get_optional_enabled(&phydev->mdio.dev, NULL);
	}

	if (IS_ERR(clk))
		return PTR_ERR(clk);

	clk_disable_unprepare(clk);
	priv->clk = clk;

	if (ksz8041_fiber_mode(phydev))
		phydev->port = PORT_FIBRE;

	/* Support legacy board-file configuration */
	if (phydev->dev_flags & MICREL_PHY_50MHZ_CLK) {
		priv->rmii_ref_clk_sel = true;
		priv->rmii_ref_clk_sel_val = true;
	}

	return 0;
}

static int lan8814_cable_test_start(struct phy_device *phydev)
{
	/* If autoneg is enabled, we won't be able to test cross pair
	 * short. In this case, the PHY will "detect" a link and
	 * confuse the internal state machine - disable auto neg here.
	 * Set the speed to 1000mbit and full duplex.
	 */
	return phy_modify(phydev, MII_BMCR, BMCR_ANENABLE | BMCR_SPEED100,
			  BMCR_SPEED1000 | BMCR_FULLDPLX);
}

static int ksz886x_cable_test_start(struct phy_device *phydev)
{
	if (phydev->dev_flags & MICREL_KSZ8_P1_ERRATA)
		return -EOPNOTSUPP;

	/* If autoneg is enabled, we won't be able to test cross pair
	 * short. In this case, the PHY will "detect" a link and
	 * confuse the internal state machine - disable auto neg here.
	 * If autoneg is disabled, we should set the speed to 10mbit.
	 */
	return phy_clear_bits(phydev, MII_BMCR, BMCR_ANENABLE | BMCR_SPEED100);
}

static __always_inline int ksz886x_cable_test_result_trans(u16 status, u16 mask)
{
	switch (FIELD_GET(mask, status)) {
	case KSZ8081_LMD_STAT_NORMAL:
		return ETHTOOL_A_CABLE_RESULT_CODE_OK;
	case KSZ8081_LMD_STAT_SHORT:
		return ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT;
	case KSZ8081_LMD_STAT_OPEN:
		return ETHTOOL_A_CABLE_RESULT_CODE_OPEN;
	case KSZ8081_LMD_STAT_FAIL:
		fallthrough;
	default:
		return ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	}
}

static __always_inline bool ksz886x_cable_test_failed(u16 status, u16 mask)
{
	return FIELD_GET(mask, status) ==
		KSZ8081_LMD_STAT_FAIL;
}

static __always_inline bool ksz886x_cable_test_fault_length_valid(u16 status, u16 mask)
{
	switch (FIELD_GET(mask, status)) {
	case KSZ8081_LMD_STAT_OPEN:
		fallthrough;
	case KSZ8081_LMD_STAT_SHORT:
		return true;
	}
	return false;
}

static __always_inline int ksz886x_cable_test_fault_length(struct phy_device *phydev,
							   u16 status, u16 data_mask)
{
	int dt;

	/* According to the data sheet the distance to the fault is
	 * DELTA_TIME * 0.4 meters for ksz phys.
	 * (DELTA_TIME - 22) * 0.8 for lan8814 phy.
	 */
	dt = FIELD_GET(data_mask, status);

	if (phydev_id_compare(phydev, PHY_ID_LAN8814))
		return ((dt - 22) * 800) / 10;
	else
		return (dt * 400) / 10;
}

static int ksz886x_cable_test_wait_for_completion(struct phy_device *phydev)
{
	const struct kszphy_type *type = phydev->drv->driver_data;
	int val, ret;

	ret = phy_read_poll_timeout(phydev, type->cable_diag_reg, val,
				    !(val & KSZ8081_LMD_ENABLE_TEST),
				    30000, 100000, true);

	return ret < 0 ? ret : 0;
}

static int lan8814_cable_test_one_pair(struct phy_device *phydev, int pair)
{
	static const int ethtool_pair[] = { ETHTOOL_A_CABLE_PAIR_A,
					    ETHTOOL_A_CABLE_PAIR_B,
					    ETHTOOL_A_CABLE_PAIR_C,
					    ETHTOOL_A_CABLE_PAIR_D,
					  };
	u32 fault_length;
	int ret;
	int val;

	val = KSZ8081_LMD_ENABLE_TEST;
	val = val | (pair << LAN8814_PAIR_BIT_SHIFT);

	ret = phy_write(phydev, LAN8814_CABLE_DIAG, val);
	if (ret < 0)
		return ret;

	ret = ksz886x_cable_test_wait_for_completion(phydev);
	if (ret)
		return ret;

	val = phy_read(phydev, LAN8814_CABLE_DIAG);
	if (val < 0)
		return val;

	if (ksz886x_cable_test_failed(val, LAN8814_CABLE_DIAG_STAT_MASK))
		return -EAGAIN;

	ret = ethnl_cable_test_result(phydev, ethtool_pair[pair],
				      ksz886x_cable_test_result_trans(val,
								      LAN8814_CABLE_DIAG_STAT_MASK
								      ));
	if (ret)
		return ret;

	if (!ksz886x_cable_test_fault_length_valid(val, LAN8814_CABLE_DIAG_STAT_MASK))
		return 0;

	fault_length = ksz886x_cable_test_fault_length(phydev, val,
						       LAN8814_CABLE_DIAG_VCT_DATA_MASK);

	return ethnl_cable_test_fault_length(phydev, ethtool_pair[pair], fault_length);
}

static int ksz886x_cable_test_one_pair(struct phy_device *phydev, int pair)
{
	static const int ethtool_pair[] = {
		ETHTOOL_A_CABLE_PAIR_A,
		ETHTOOL_A_CABLE_PAIR_B,
	};
	int ret, val, mdix;
	u32 fault_length;

	/* There is no way to choice the pair, like we do one ksz9031.
	 * We can workaround this limitation by using the MDI-X functionality.
	 */
	if (pair == 0)
		mdix = ETH_TP_MDI;
	else
		mdix = ETH_TP_MDI_X;

	switch (phydev->phy_id & MICREL_PHY_ID_MASK) {
	case PHY_ID_KSZ8081:
		ret = ksz8081_config_mdix(phydev, mdix);
		break;
	case PHY_ID_KSZ886X:
		ret = ksz886x_config_mdix(phydev, mdix);
		break;
	default:
		ret = -ENODEV;
	}

	if (ret)
		return ret;

	/* Now we are ready to fire. This command will send a 100ns pulse
	 * to the pair.
	 */
	ret = phy_write(phydev, KSZ8081_LMD, KSZ8081_LMD_ENABLE_TEST);
	if (ret)
		return ret;

	ret = ksz886x_cable_test_wait_for_completion(phydev);
	if (ret)
		return ret;

	val = phy_read(phydev, KSZ8081_LMD);
	if (val < 0)
		return val;

	if (ksz886x_cable_test_failed(val, KSZ8081_LMD_STAT_MASK))
		return -EAGAIN;

	ret = ethnl_cable_test_result(phydev, ethtool_pair[pair],
				      ksz886x_cable_test_result_trans(val, KSZ8081_LMD_STAT_MASK));
	if (ret)
		return ret;

	if (!ksz886x_cable_test_fault_length_valid(val, KSZ8081_LMD_STAT_MASK))
		return 0;

	fault_length = ksz886x_cable_test_fault_length(phydev, val, KSZ8081_LMD_DELTA_TIME_MASK);

	return ethnl_cable_test_fault_length(phydev, ethtool_pair[pair], fault_length);
}

static int ksz886x_cable_test_get_status(struct phy_device *phydev,
					 bool *finished)
{
	const struct kszphy_type *type = phydev->drv->driver_data;
	unsigned long pair_mask = type->pair_mask;
	int retries = 20;
	int ret = 0;
	int pair;

	*finished = false;

	/* Try harder if link partner is active */
	while (pair_mask && retries--) {
		for_each_set_bit(pair, &pair_mask, 4) {
			if (type->cable_diag_reg == LAN8814_CABLE_DIAG)
				ret = lan8814_cable_test_one_pair(phydev, pair);
			else
				ret = ksz886x_cable_test_one_pair(phydev, pair);
			if (ret == -EAGAIN)
				continue;
			if (ret < 0)
				return ret;
			clear_bit(pair, &pair_mask);
		}
		/* If link partner is in autonegotiation mode it will send 2ms
		 * of FLPs with at least 6ms of silence.
		 * Add 2ms sleep to have better chances to hit this silence.
		 */
		if (pair_mask)
			msleep(2);
	}

	*finished = true;

	return ret;
}

#define LAN_EXT_PAGE_ACCESS_CONTROL			0x16
#define LAN_EXT_PAGE_ACCESS_ADDRESS_DATA		0x17
#define LAN_EXT_PAGE_ACCESS_CTRL_EP_FUNC		0x4000

#define LAN8814_QSGMII_SOFT_RESET			0x43
#define LAN8814_QSGMII_SOFT_RESET_BIT			BIT(0)
#define LAN8814_QSGMII_PCS1G_ANEG_CONFIG		0x13
#define LAN8814_QSGMII_PCS1G_ANEG_CONFIG_ANEG_ENA	BIT(3)
#define LAN8814_ALIGN_SWAP				0x4a
#define LAN8814_ALIGN_TX_A_B_SWAP			0x1
#define LAN8814_ALIGN_TX_A_B_SWAP_MASK			GENMASK(2, 0)

#define LAN8804_ALIGN_SWAP				0x4a
#define LAN8804_ALIGN_TX_A_B_SWAP			0x1
#define LAN8804_ALIGN_TX_A_B_SWAP_MASK			GENMASK(2, 0)
#define LAN8814_CLOCK_MANAGEMENT			0xd
#define LAN8814_LINK_QUALITY				0x8e

static int lanphy_read_page_reg(struct phy_device *phydev, int page, u32 addr)
{
	int data;

	phy_lock_mdio_bus(phydev);
	__phy_write(phydev, LAN_EXT_PAGE_ACCESS_CONTROL, page);
	__phy_write(phydev, LAN_EXT_PAGE_ACCESS_ADDRESS_DATA, addr);
	__phy_write(phydev, LAN_EXT_PAGE_ACCESS_CONTROL,
		    (page | LAN_EXT_PAGE_ACCESS_CTRL_EP_FUNC));
	data = __phy_read(phydev, LAN_EXT_PAGE_ACCESS_ADDRESS_DATA);
	phy_unlock_mdio_bus(phydev);

	return data;
}

static int lanphy_write_page_reg(struct phy_device *phydev, int page, u16 addr,
				 u16 val)
{
	phy_lock_mdio_bus(phydev);
	__phy_write(phydev, LAN_EXT_PAGE_ACCESS_CONTROL, page);
	__phy_write(phydev, LAN_EXT_PAGE_ACCESS_ADDRESS_DATA, addr);
	__phy_write(phydev, LAN_EXT_PAGE_ACCESS_CONTROL,
		    page | LAN_EXT_PAGE_ACCESS_CTRL_EP_FUNC);

	val = __phy_write(phydev, LAN_EXT_PAGE_ACCESS_ADDRESS_DATA, val);
	if (val != 0)
		phydev_err(phydev, "Error: phy_write has returned error %d\n",
			   val);
	phy_unlock_mdio_bus(phydev);
	return val;
}

static int lan8814_config_ts_intr(struct phy_device *phydev, bool enable)
{
	u16 val = 0;

	if (enable)
		val = PTP_TSU_INT_EN_PTP_TX_TS_EN_ |
		      PTP_TSU_INT_EN_PTP_TX_TS_OVRFL_EN_ |
		      PTP_TSU_INT_EN_PTP_RX_TS_EN_ |
		      PTP_TSU_INT_EN_PTP_RX_TS_OVRFL_EN_;

	return lanphy_write_page_reg(phydev, 5, PTP_TSU_INT_EN, val);
}

static void lan8814_ptp_rx_ts_get(struct phy_device *phydev,
				  u32 *seconds, u32 *nano_seconds, u16 *seq_id)
{
	*seconds = lanphy_read_page_reg(phydev, 5, PTP_RX_INGRESS_SEC_HI);
	*seconds = (*seconds << 16) |
		   lanphy_read_page_reg(phydev, 5, PTP_RX_INGRESS_SEC_LO);

	*nano_seconds = lanphy_read_page_reg(phydev, 5, PTP_RX_INGRESS_NS_HI);
	*nano_seconds = ((*nano_seconds & 0x3fff) << 16) |
			lanphy_read_page_reg(phydev, 5, PTP_RX_INGRESS_NS_LO);

	*seq_id = lanphy_read_page_reg(phydev, 5, PTP_RX_MSG_HEADER2);
}

static void lan8814_ptp_tx_ts_get(struct phy_device *phydev,
				  u32 *seconds, u32 *nano_seconds, u16 *seq_id)
{
	*seconds = lanphy_read_page_reg(phydev, 5, PTP_TX_EGRESS_SEC_HI);
	*seconds = *seconds << 16 |
		   lanphy_read_page_reg(phydev, 5, PTP_TX_EGRESS_SEC_LO);

	*nano_seconds = lanphy_read_page_reg(phydev, 5, PTP_TX_EGRESS_NS_HI);
	*nano_seconds = ((*nano_seconds & 0x3fff) << 16) |
			lanphy_read_page_reg(phydev, 5, PTP_TX_EGRESS_NS_LO);

	*seq_id = lanphy_read_page_reg(phydev, 5, PTP_TX_MSG_HEADER2);
}

static int lan8814_ts_info(struct mii_timestamper *mii_ts, struct kernel_ethtool_ts_info *info)
{
	struct kszphy_ptp_priv *ptp_priv = container_of(mii_ts, struct kszphy_ptp_priv, mii_ts);
	struct lan8814_shared_priv *shared = phy_package_get_priv(ptp_priv->phydev);

	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	info->phc_index = ptp_clock_index(shared->ptp_clock);

	info->tx_types =
		(1 << HWTSTAMP_TX_OFF) |
		(1 << HWTSTAMP_TX_ON) |
		(1 << HWTSTAMP_TX_ONESTEP_SYNC);

	info->rx_filters =
		(1 << HWTSTAMP_FILTER_NONE) |
		(1 << HWTSTAMP_FILTER_PTP_V1_L4_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_EVENT);

	return 0;
}

static void lan8814_flush_fifo(struct phy_device *phydev, bool egress)
{
	int i;

	for (i = 0; i < FIFO_SIZE; ++i)
		lanphy_read_page_reg(phydev, 5,
				     egress ? PTP_TX_MSG_HEADER2 : PTP_RX_MSG_HEADER2);

	/* Read to clear overflow status bit */
	lanphy_read_page_reg(phydev, 5, PTP_TSU_INT_STS);
}

static int lan8814_hwtstamp(struct mii_timestamper *mii_ts,
			    struct kernel_hwtstamp_config *config,
			    struct netlink_ext_ack *extack)
{
	struct kszphy_ptp_priv *ptp_priv =
			  container_of(mii_ts, struct kszphy_ptp_priv, mii_ts);
	struct lan8814_ptp_rx_ts *rx_ts, *tmp;
	int txcfg = 0, rxcfg = 0;
	int pkt_ts_enable;
	int tx_mod;

	ptp_priv->hwts_tx_type = config->tx_type;
	ptp_priv->rx_filter = config->rx_filter;

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		ptp_priv->layer = 0;
		ptp_priv->version = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		ptp_priv->layer = PTP_CLASS_L4;
		ptp_priv->version = PTP_CLASS_V2;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		ptp_priv->layer = PTP_CLASS_L2;
		ptp_priv->version = PTP_CLASS_V2;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		ptp_priv->layer = PTP_CLASS_L4 | PTP_CLASS_L2;
		ptp_priv->version = PTP_CLASS_V2;
		break;
	default:
		return -ERANGE;
	}

	if (ptp_priv->layer & PTP_CLASS_L2) {
		rxcfg = PTP_RX_PARSE_CONFIG_LAYER2_EN_;
		txcfg = PTP_TX_PARSE_CONFIG_LAYER2_EN_;
	} else if (ptp_priv->layer & PTP_CLASS_L4) {
		rxcfg |= PTP_RX_PARSE_CONFIG_IPV4_EN_ | PTP_RX_PARSE_CONFIG_IPV6_EN_;
		txcfg |= PTP_TX_PARSE_CONFIG_IPV4_EN_ | PTP_TX_PARSE_CONFIG_IPV6_EN_;
	}
	lanphy_write_page_reg(ptp_priv->phydev, 5, PTP_RX_PARSE_CONFIG, rxcfg);
	lanphy_write_page_reg(ptp_priv->phydev, 5, PTP_TX_PARSE_CONFIG, txcfg);

	pkt_ts_enable = PTP_TIMESTAMP_EN_SYNC_ | PTP_TIMESTAMP_EN_DREQ_ |
			PTP_TIMESTAMP_EN_PDREQ_ | PTP_TIMESTAMP_EN_PDRES_;
	lanphy_write_page_reg(ptp_priv->phydev, 5, PTP_RX_TIMESTAMP_EN, pkt_ts_enable);
	lanphy_write_page_reg(ptp_priv->phydev, 5, PTP_TX_TIMESTAMP_EN, pkt_ts_enable);

	tx_mod = lanphy_read_page_reg(ptp_priv->phydev, 5, PTP_TX_MOD);
	if (ptp_priv->hwts_tx_type == HWTSTAMP_TX_ONESTEP_SYNC) {
		lanphy_write_page_reg(ptp_priv->phydev, 5, PTP_TX_MOD,
				      tx_mod | PTP_TX_MOD_TX_PTP_SYNC_TS_INSERT_);
	} else if (ptp_priv->hwts_tx_type == HWTSTAMP_TX_ON) {
		lanphy_write_page_reg(ptp_priv->phydev, 5, PTP_TX_MOD,
				      tx_mod & ~PTP_TX_MOD_TX_PTP_SYNC_TS_INSERT_);
	}

	if (config->rx_filter != HWTSTAMP_FILTER_NONE)
		lan8814_config_ts_intr(ptp_priv->phydev, true);
	else
		lan8814_config_ts_intr(ptp_priv->phydev, false);

	/* In case of multiple starts and stops, these needs to be cleared */
	list_for_each_entry_safe(rx_ts, tmp, &ptp_priv->rx_ts_list, list) {
		list_del(&rx_ts->list);
		kfree(rx_ts);
	}
	skb_queue_purge(&ptp_priv->rx_queue);
	skb_queue_purge(&ptp_priv->tx_queue);

	lan8814_flush_fifo(ptp_priv->phydev, false);
	lan8814_flush_fifo(ptp_priv->phydev, true);

	return 0;
}

static void lan8814_txtstamp(struct mii_timestamper *mii_ts,
			     struct sk_buff *skb, int type)
{
	struct kszphy_ptp_priv *ptp_priv = container_of(mii_ts, struct kszphy_ptp_priv, mii_ts);

	switch (ptp_priv->hwts_tx_type) {
	case HWTSTAMP_TX_ONESTEP_SYNC:
		if (ptp_msg_is_sync(skb, type)) {
			kfree_skb(skb);
			return;
		}
		fallthrough;
	case HWTSTAMP_TX_ON:
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		skb_queue_tail(&ptp_priv->tx_queue, skb);
		break;
	case HWTSTAMP_TX_OFF:
	default:
		kfree_skb(skb);
		break;
	}
}

static bool lan8814_get_sig_rx(struct sk_buff *skb, u16 *sig)
{
	struct ptp_header *ptp_header;
	u32 type;

	skb_push(skb, ETH_HLEN);
	type = ptp_classify_raw(skb);
	ptp_header = ptp_parse_header(skb, type);
	skb_pull_inline(skb, ETH_HLEN);

	if (!ptp_header)
		return false;

	*sig = (__force u16)(ntohs(ptp_header->sequence_id));
	return true;
}

static bool lan8814_match_rx_skb(struct kszphy_ptp_priv *ptp_priv,
				 struct sk_buff *skb)
{
	struct skb_shared_hwtstamps *shhwtstamps;
	struct lan8814_ptp_rx_ts *rx_ts, *tmp;
	unsigned long flags;
	bool ret = false;
	u16 skb_sig;

	if (!lan8814_get_sig_rx(skb, &skb_sig))
		return ret;

	/* Iterate over all RX timestamps and match it with the received skbs */
	spin_lock_irqsave(&ptp_priv->rx_ts_lock, flags);
	list_for_each_entry_safe(rx_ts, tmp, &ptp_priv->rx_ts_list, list) {
		/* Check if we found the signature we were looking for. */
		if (memcmp(&skb_sig, &rx_ts->seq_id, sizeof(rx_ts->seq_id)))
			continue;

		shhwtstamps = skb_hwtstamps(skb);
		memset(shhwtstamps, 0, sizeof(*shhwtstamps));
		shhwtstamps->hwtstamp = ktime_set(rx_ts->seconds,
						  rx_ts->nsec);
		list_del(&rx_ts->list);
		kfree(rx_ts);

		ret = true;
		break;
	}
	spin_unlock_irqrestore(&ptp_priv->rx_ts_lock, flags);

	if (ret)
		netif_rx(skb);
	return ret;
}

static bool lan8814_rxtstamp(struct mii_timestamper *mii_ts, struct sk_buff *skb, int type)
{
	struct kszphy_ptp_priv *ptp_priv =
			container_of(mii_ts, struct kszphy_ptp_priv, mii_ts);

	if (ptp_priv->rx_filter == HWTSTAMP_FILTER_NONE ||
	    type == PTP_CLASS_NONE)
		return false;

	if ((type & ptp_priv->version) == 0 || (type & ptp_priv->layer) == 0)
		return false;

	/* If we failed to match then add it to the queue for when the timestamp
	 * will come
	 */
	if (!lan8814_match_rx_skb(ptp_priv, skb))
		skb_queue_tail(&ptp_priv->rx_queue, skb);

	return true;
}

static void lan8814_ptp_clock_set(struct phy_device *phydev,
				  time64_t sec, u32 nsec)
{
	lanphy_write_page_reg(phydev, 4, PTP_CLOCK_SET_SEC_LO, lower_16_bits(sec));
	lanphy_write_page_reg(phydev, 4, PTP_CLOCK_SET_SEC_MID, upper_16_bits(sec));
	lanphy_write_page_reg(phydev, 4, PTP_CLOCK_SET_SEC_HI, upper_32_bits(sec));
	lanphy_write_page_reg(phydev, 4, PTP_CLOCK_SET_NS_LO, lower_16_bits(nsec));
	lanphy_write_page_reg(phydev, 4, PTP_CLOCK_SET_NS_HI, upper_16_bits(nsec));

	lanphy_write_page_reg(phydev, 4, PTP_CMD_CTL, PTP_CMD_CTL_PTP_CLOCK_LOAD_);
}

static void lan8814_ptp_clock_get(struct phy_device *phydev,
				  time64_t *sec, u32 *nsec)
{
	lanphy_write_page_reg(phydev, 4, PTP_CMD_CTL, PTP_CMD_CTL_PTP_CLOCK_READ_);

	*sec = lanphy_read_page_reg(phydev, 4, PTP_CLOCK_READ_SEC_HI);
	*sec <<= 16;
	*sec |= lanphy_read_page_reg(phydev, 4, PTP_CLOCK_READ_SEC_MID);
	*sec <<= 16;
	*sec |= lanphy_read_page_reg(phydev, 4, PTP_CLOCK_READ_SEC_LO);

	*nsec = lanphy_read_page_reg(phydev, 4, PTP_CLOCK_READ_NS_HI);
	*nsec <<= 16;
	*nsec |= lanphy_read_page_reg(phydev, 4, PTP_CLOCK_READ_NS_LO);
}

static int lan8814_ptpci_gettime64(struct ptp_clock_info *ptpci,
				   struct timespec64 *ts)
{
	struct lan8814_shared_priv *shared = container_of(ptpci, struct lan8814_shared_priv,
							  ptp_clock_info);
	struct phy_device *phydev = shared->phydev;
	u32 nano_seconds;
	time64_t seconds;

	mutex_lock(&shared->shared_lock);
	lan8814_ptp_clock_get(phydev, &seconds, &nano_seconds);
	mutex_unlock(&shared->shared_lock);
	ts->tv_sec = seconds;
	ts->tv_nsec = nano_seconds;

	return 0;
}

static int lan8814_ptpci_settime64(struct ptp_clock_info *ptpci,
				   const struct timespec64 *ts)
{
	struct lan8814_shared_priv *shared = container_of(ptpci, struct lan8814_shared_priv,
							  ptp_clock_info);
	struct phy_device *phydev = shared->phydev;

	mutex_lock(&shared->shared_lock);
	lan8814_ptp_clock_set(phydev, ts->tv_sec, ts->tv_nsec);
	mutex_unlock(&shared->shared_lock);

	return 0;
}

static void lan8814_ptp_set_target(struct phy_device *phydev, int event,
				   s64 start_sec, u32 start_nsec)
{
	/* Set the start time */
	lanphy_write_page_reg(phydev, 4, LAN8814_PTP_CLOCK_TARGET_SEC_LO(event),
			      lower_16_bits(start_sec));
	lanphy_write_page_reg(phydev, 4, LAN8814_PTP_CLOCK_TARGET_SEC_HI(event),
			      upper_16_bits(start_sec));

	lanphy_write_page_reg(phydev, 4, LAN8814_PTP_CLOCK_TARGET_NS_LO(event),
			      lower_16_bits(start_nsec));
	lanphy_write_page_reg(phydev, 4, LAN8814_PTP_CLOCK_TARGET_NS_HI(event),
			      upper_16_bits(start_nsec) & 0x3fff);
}

static void lan8814_ptp_update_target(struct phy_device *phydev, time64_t sec)
{
	lan8814_ptp_set_target(phydev, LAN8814_EVENT_A,
			       sec + LAN8814_BUFFER_TIME, 0);
	lan8814_ptp_set_target(phydev, LAN8814_EVENT_B,
			       sec + LAN8814_BUFFER_TIME, 0);
}

static void lan8814_ptp_clock_step(struct phy_device *phydev,
				   s64 time_step_ns)
{
	u32 nano_seconds_step;
	u64 abs_time_step_ns;
	time64_t set_seconds;
	u32 nano_seconds;
	u32 remainder;
	s32 seconds;

	if (time_step_ns >  15000000000LL) {
		/* convert to clock set */
		lan8814_ptp_clock_get(phydev, &set_seconds, &nano_seconds);
		set_seconds += div_u64_rem(time_step_ns, 1000000000LL,
					   &remainder);
		nano_seconds += remainder;
		if (nano_seconds >= 1000000000) {
			set_seconds++;
			nano_seconds -= 1000000000;
		}
		lan8814_ptp_clock_set(phydev, set_seconds, nano_seconds);
		lan8814_ptp_update_target(phydev, set_seconds);
		return;
	} else if (time_step_ns < -15000000000LL) {
		/* convert to clock set */
		time_step_ns = -time_step_ns;

		lan8814_ptp_clock_get(phydev, &set_seconds, &nano_seconds);
		set_seconds -= div_u64_rem(time_step_ns, 1000000000LL,
					   &remainder);
		nano_seconds_step = remainder;
		if (nano_seconds < nano_seconds_step) {
			set_seconds--;
			nano_seconds += 1000000000;
		}
		nano_seconds -= nano_seconds_step;
		lan8814_ptp_clock_set(phydev, set_seconds, nano_seconds);
		lan8814_ptp_update_target(phydev, set_seconds);
		return;
	}

	/* do clock step */
	if (time_step_ns >= 0) {
		abs_time_step_ns = (u64)time_step_ns;
		seconds = (s32)div_u64_rem(abs_time_step_ns, 1000000000,
					   &remainder);
		nano_seconds = remainder;
	} else {
		abs_time_step_ns = (u64)(-time_step_ns);
		seconds = -((s32)div_u64_rem(abs_time_step_ns, 1000000000,
			    &remainder));
		nano_seconds = remainder;
		if (nano_seconds > 0) {
			/* subtracting nano seconds is not allowed
			 * convert to subtracting from seconds,
			 * and adding to nanoseconds
			 */
			seconds--;
			nano_seconds = (1000000000 - nano_seconds);
		}
	}

	if (nano_seconds > 0) {
		/* add 8 ns to cover the likely normal increment */
		nano_seconds += 8;
	}

	if (nano_seconds >= 1000000000) {
		/* carry into seconds */
		seconds++;
		nano_seconds -= 1000000000;
	}

	while (seconds) {
		u32 nsec;

		if (seconds > 0) {
			u32 adjustment_value = (u32)seconds;
			u16 adjustment_value_lo, adjustment_value_hi;

			if (adjustment_value > 0xF)
				adjustment_value = 0xF;

			adjustment_value_lo = adjustment_value & 0xffff;
			adjustment_value_hi = (adjustment_value >> 16) & 0x3fff;

			lanphy_write_page_reg(phydev, 4, PTP_LTC_STEP_ADJ_LO,
					      adjustment_value_lo);
			lanphy_write_page_reg(phydev, 4, PTP_LTC_STEP_ADJ_HI,
					      PTP_LTC_STEP_ADJ_DIR_ |
					      adjustment_value_hi);
			seconds -= ((s32)adjustment_value);

			lan8814_ptp_clock_get(phydev, &set_seconds, &nsec);
			set_seconds -= adjustment_value;
			lan8814_ptp_update_target(phydev, set_seconds);
		} else {
			u32 adjustment_value = (u32)(-seconds);
			u16 adjustment_value_lo, adjustment_value_hi;

			if (adjustment_value > 0xF)
				adjustment_value = 0xF;

			adjustment_value_lo = adjustment_value & 0xffff;
			adjustment_value_hi = (adjustment_value >> 16) & 0x3fff;

			lanphy_write_page_reg(phydev, 4, PTP_LTC_STEP_ADJ_LO,
					      adjustment_value_lo);
			lanphy_write_page_reg(phydev, 4, PTP_LTC_STEP_ADJ_HI,
					      adjustment_value_hi);
			seconds += ((s32)adjustment_value);

			lan8814_ptp_clock_get(phydev, &set_seconds, &nsec);
			set_seconds += adjustment_value;
			lan8814_ptp_update_target(phydev, set_seconds);
		}
		lanphy_write_page_reg(phydev, 4, PTP_CMD_CTL,
				      PTP_CMD_CTL_PTP_LTC_STEP_SEC_);
	}
	if (nano_seconds) {
		u16 nano_seconds_lo;
		u16 nano_seconds_hi;

		nano_seconds_lo = nano_seconds & 0xffff;
		nano_seconds_hi = (nano_seconds >> 16) & 0x3fff;

		lanphy_write_page_reg(phydev, 4, PTP_LTC_STEP_ADJ_LO,
				      nano_seconds_lo);
		lanphy_write_page_reg(phydev, 4, PTP_LTC_STEP_ADJ_HI,
				      PTP_LTC_STEP_ADJ_DIR_ |
				      nano_seconds_hi);
		lanphy_write_page_reg(phydev, 4, PTP_CMD_CTL,
				      PTP_CMD_CTL_PTP_LTC_STEP_NSEC_);
	}
}

static int lan8814_ptpci_adjtime(struct ptp_clock_info *ptpci, s64 delta)
{
	struct lan8814_shared_priv *shared = container_of(ptpci, struct lan8814_shared_priv,
							  ptp_clock_info);
	struct phy_device *phydev = shared->phydev;

	mutex_lock(&shared->shared_lock);
	lan8814_ptp_clock_step(phydev, delta);
	mutex_unlock(&shared->shared_lock);

	return 0;
}

static int lan8814_ptpci_adjfine(struct ptp_clock_info *ptpci, long scaled_ppm)
{
	struct lan8814_shared_priv *shared = container_of(ptpci, struct lan8814_shared_priv,
							  ptp_clock_info);
	struct phy_device *phydev = shared->phydev;
	u16 kszphy_rate_adj_lo, kszphy_rate_adj_hi;
	bool positive = true;
	u32 kszphy_rate_adj;

	if (scaled_ppm < 0) {
		scaled_ppm = -scaled_ppm;
		positive = false;
	}

	kszphy_rate_adj = LAN8814_1PPM_FORMAT * (scaled_ppm >> 16);
	kszphy_rate_adj += (LAN8814_1PPM_FORMAT * (0xffff & scaled_ppm)) >> 16;

	kszphy_rate_adj_lo = kszphy_rate_adj & 0xffff;
	kszphy_rate_adj_hi = (kszphy_rate_adj >> 16) & 0x3fff;

	if (positive)
		kszphy_rate_adj_hi |= PTP_CLOCK_RATE_ADJ_DIR_;

	mutex_lock(&shared->shared_lock);
	lanphy_write_page_reg(phydev, 4, PTP_CLOCK_RATE_ADJ_HI, kszphy_rate_adj_hi);
	lanphy_write_page_reg(phydev, 4, PTP_CLOCK_RATE_ADJ_LO, kszphy_rate_adj_lo);
	mutex_unlock(&shared->shared_lock);

	return 0;
}

static void lan8814_ptp_set_reload(struct phy_device *phydev, int event,
				   s64 period_sec, u32 period_nsec)
{
	lanphy_write_page_reg(phydev, 4,
			      LAN8814_PTP_CLOCK_TARGET_RELOAD_SEC_LO(event),
			      lower_16_bits(period_sec));
	lanphy_write_page_reg(phydev, 4,
			      LAN8814_PTP_CLOCK_TARGET_RELOAD_SEC_HI(event),
			      upper_16_bits(period_sec));

	lanphy_write_page_reg(phydev, 4,
			      LAN8814_PTP_CLOCK_TARGET_RELOAD_NS_LO(event),
			      lower_16_bits(period_nsec));
	lanphy_write_page_reg(phydev, 4,
			      LAN8814_PTP_CLOCK_TARGET_RELOAD_NS_HI(event),
			      upper_16_bits(period_nsec) & 0x3fff);
}

static void lan8814_ptp_enable_event(struct phy_device *phydev, int event,
				     int pulse_width)
{
	u16 val;

	val = lanphy_read_page_reg(phydev, 4, LAN8814_PTP_GENERAL_CONFIG);
	/* Set the pulse width of the event */
	val &= ~(LAN8814_PTP_GENERAL_CONFIG_LTC_EVENT_MASK(event));
	/* Make sure that the target clock will be incremented each time when
	 * local time reaches or pass it
	 */
	val |= LAN8814_PTP_GENERAL_CONFIG_LTC_EVENT_SET(event, pulse_width);
	val &= ~(LAN8814_PTP_GENERAL_CONFIG_RELOAD_ADD_X(event));
	/* Set the polarity high */
	val |= LAN8814_PTP_GENERAL_CONFIG_POLARITY_X(event);
	lanphy_write_page_reg(phydev, 4, LAN8814_PTP_GENERAL_CONFIG, val);
}

static void lan8814_ptp_disable_event(struct phy_device *phydev, int event)
{
	u16 val;

	/* Set target to too far in the future, effectively disabling it */
	lan8814_ptp_set_target(phydev, event, 0xFFFFFFFF, 0);

	/* And then reload once it recheas the target */
	val = lanphy_read_page_reg(phydev, 4, LAN8814_PTP_GENERAL_CONFIG);
	val |= LAN8814_PTP_GENERAL_CONFIG_RELOAD_ADD_X(event);
	lanphy_write_page_reg(phydev, 4, LAN8814_PTP_GENERAL_CONFIG, val);
}

static void lan8814_ptp_perout_off(struct phy_device *phydev, int pin)
{
	u16 val;

	/* Disable gpio alternate function,
	 * 1: select as gpio,
	 * 0: select alt func
	 */
	val = lanphy_read_page_reg(phydev, 4, LAN8814_GPIO_EN_ADDR(pin));
	val |= LAN8814_GPIO_EN_BIT(pin);
	lanphy_write_page_reg(phydev, 4, LAN8814_GPIO_EN_ADDR(pin), val);

	val = lanphy_read_page_reg(phydev, 4, LAN8814_GPIO_DIR_ADDR(pin));
	val &= ~LAN8814_GPIO_DIR_BIT(pin);
	lanphy_write_page_reg(phydev, 4, LAN8814_GPIO_DIR_ADDR(pin), val);

	val = lanphy_read_page_reg(phydev, 4, LAN8814_GPIO_BUF_ADDR(pin));
	val &= ~LAN8814_GPIO_BUF_BIT(pin);
	lanphy_write_page_reg(phydev, 4, LAN8814_GPIO_BUF_ADDR(pin), val);
}

static void lan8814_ptp_perout_on(struct phy_device *phydev, int pin)
{
	int val;

	/* Set as gpio output */
	val = lanphy_read_page_reg(phydev, 4, LAN8814_GPIO_DIR_ADDR(pin));
	val |= LAN8814_GPIO_DIR_BIT(pin);
	lanphy_write_page_reg(phydev, 4, LAN8814_GPIO_DIR_ADDR(pin), val);

	/* Enable gpio 0:for alternate function, 1:gpio */
	val = lanphy_read_page_reg(phydev, 4, LAN8814_GPIO_EN_ADDR(pin));
	val &= ~LAN8814_GPIO_EN_BIT(pin);
	lanphy_write_page_reg(phydev, 4, LAN8814_GPIO_EN_ADDR(pin), val);

	/* Set buffer type to push pull */
	val = lanphy_read_page_reg(phydev, 4, LAN8814_GPIO_BUF_ADDR(pin));
	val |= LAN8814_GPIO_BUF_BIT(pin);
	lanphy_write_page_reg(phydev, 4, LAN8814_GPIO_BUF_ADDR(pin), val);
}

static int lan8814_ptp_perout(struct ptp_clock_info *ptpci,
			      struct ptp_clock_request *rq, int on)
{
	struct lan8814_shared_priv *shared = container_of(ptpci, struct lan8814_shared_priv,
							  ptp_clock_info);
	struct phy_device *phydev = shared->phydev;
	struct timespec64 ts_on, ts_period;
	s64 on_nsec, period_nsec;
	int pulse_width;
	int pin, event;

	/* Reject requests with unsupported flags */
	if (rq->perout.flags & ~PTP_PEROUT_DUTY_CYCLE)
		return -EOPNOTSUPP;

	mutex_lock(&shared->shared_lock);
	event = rq->perout.index;
	pin = ptp_find_pin(shared->ptp_clock, PTP_PF_PEROUT, event);
	if (pin < 0 || pin >= LAN8814_PTP_PEROUT_NUM) {
		mutex_unlock(&shared->shared_lock);
		return -EBUSY;
	}

	if (!on) {
		lan8814_ptp_perout_off(phydev, pin);
		lan8814_ptp_disable_event(phydev, event);
		mutex_unlock(&shared->shared_lock);
		return 0;
	}

	ts_on.tv_sec = rq->perout.on.sec;
	ts_on.tv_nsec = rq->perout.on.nsec;
	on_nsec = timespec64_to_ns(&ts_on);

	ts_period.tv_sec = rq->perout.period.sec;
	ts_period.tv_nsec = rq->perout.period.nsec;
	period_nsec = timespec64_to_ns(&ts_period);

	if (period_nsec < 200) {
		pr_warn_ratelimited("%s: perout period too small, minimum is 200 nsec\n",
				    phydev_name(phydev));
		mutex_unlock(&shared->shared_lock);
		return -EOPNOTSUPP;
	}

	if (on_nsec >= period_nsec) {
		pr_warn_ratelimited("%s: pulse width must be smaller than period\n",
				    phydev_name(phydev));
		mutex_unlock(&shared->shared_lock);
		return -EINVAL;
	}

	switch (on_nsec) {
	case 200000000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_200MS;
		break;
	case 100000000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_100MS;
		break;
	case 50000000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_50MS;
		break;
	case 10000000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_10MS;
		break;
	case 5000000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_5MS;
		break;
	case 1000000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_1MS;
		break;
	case 500000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_500US;
		break;
	case 100000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_100US;
		break;
	case 50000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_50US;
		break;
	case 10000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_10US;
		break;
	case 5000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_5US;
		break;
	case 1000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_1US;
		break;
	case 500:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_500NS;
		break;
	case 100:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_100NS;
		break;
	default:
		pr_warn_ratelimited("%s: Use default duty cycle of 100ns\n",
				    phydev_name(phydev));
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_100NS;
		break;
	}

	/* Configure to pulse every period */
	lan8814_ptp_enable_event(phydev, event, pulse_width);
	lan8814_ptp_set_target(phydev, event, rq->perout.start.sec,
			       rq->perout.start.nsec);
	lan8814_ptp_set_reload(phydev, event, rq->perout.period.sec,
			       rq->perout.period.nsec);
	lan8814_ptp_perout_on(phydev, pin);
	mutex_unlock(&shared->shared_lock);

	return 0;
}

static void lan8814_ptp_extts_on(struct phy_device *phydev, int pin, u32 flags)
{
	u16 tmp;

	/* Set as gpio input */
	tmp = lanphy_read_page_reg(phydev, 4, LAN8814_GPIO_DIR_ADDR(pin));
	tmp &= ~LAN8814_GPIO_DIR_BIT(pin);
	lanphy_write_page_reg(phydev, 4, LAN8814_GPIO_DIR_ADDR(pin), tmp);

	/* Map the pin to ltc pin 0 of the capture map registers */
	tmp = lanphy_read_page_reg(phydev, 4, PTP_GPIO_CAP_MAP_LO);
	tmp |= pin;
	lanphy_write_page_reg(phydev, 4, PTP_GPIO_CAP_MAP_LO, tmp);

	/* Enable capture on the edges of the ltc pin */
	tmp = lanphy_read_page_reg(phydev, 4, PTP_GPIO_CAP_EN);
	if (flags & PTP_RISING_EDGE)
		tmp |= PTP_GPIO_CAP_EN_GPIO_RE_CAPTURE_ENABLE(0);
	if (flags & PTP_FALLING_EDGE)
		tmp |= PTP_GPIO_CAP_EN_GPIO_FE_CAPTURE_ENABLE(0);
	lanphy_write_page_reg(phydev, 4, PTP_GPIO_CAP_EN, tmp);

	/* Enable interrupt top interrupt */
	tmp = lanphy_read_page_reg(phydev, 4, PTP_COMMON_INT_ENA);
	tmp |= PTP_COMMON_INT_ENA_GPIO_CAP_EN;
	lanphy_write_page_reg(phydev, 4, PTP_COMMON_INT_ENA, tmp);
}

static void lan8814_ptp_extts_off(struct phy_device *phydev, int pin)
{
	u16 tmp;

	/* Set as gpio out */
	tmp = lanphy_read_page_reg(phydev, 4, LAN8814_GPIO_DIR_ADDR(pin));
	tmp |= LAN8814_GPIO_DIR_BIT(pin);
	lanphy_write_page_reg(phydev, 4, LAN8814_GPIO_DIR_ADDR(pin), tmp);

	/* Enable alternate, 0:for alternate function, 1:gpio */
	tmp = lanphy_read_page_reg(phydev, 4, LAN8814_GPIO_EN_ADDR(pin));
	tmp &= ~LAN8814_GPIO_EN_BIT(pin);
	lanphy_write_page_reg(phydev, 4, LAN8814_GPIO_EN_ADDR(pin), tmp);

	/* Clear the mapping of pin to registers 0 of the capture registers */
	tmp = lanphy_read_page_reg(phydev, 4, PTP_GPIO_CAP_MAP_LO);
	tmp &= ~GENMASK(3, 0);
	lanphy_write_page_reg(phydev, 4, PTP_GPIO_CAP_MAP_LO, tmp);

	/* Disable capture on both of the edges */
	tmp = lanphy_read_page_reg(phydev, 4, PTP_GPIO_CAP_EN);
	tmp &= ~PTP_GPIO_CAP_EN_GPIO_RE_CAPTURE_ENABLE(pin);
	tmp &= ~PTP_GPIO_CAP_EN_GPIO_FE_CAPTURE_ENABLE(pin);
	lanphy_write_page_reg(phydev, 4, PTP_GPIO_CAP_EN, tmp);

	/* Disable interrupt top interrupt */
	tmp = lanphy_read_page_reg(phydev, 4, PTP_COMMON_INT_ENA);
	tmp &= ~PTP_COMMON_INT_ENA_GPIO_CAP_EN;
	lanphy_write_page_reg(phydev, 4, PTP_COMMON_INT_ENA, tmp);
}

static int lan8814_ptp_extts(struct ptp_clock_info *ptpci,
			     struct ptp_clock_request *rq, int on)
{
	struct lan8814_shared_priv *shared = container_of(ptpci, struct lan8814_shared_priv,
							  ptp_clock_info);
	struct phy_device *phydev = shared->phydev;
	int pin;

	if (rq->extts.flags & ~(PTP_ENABLE_FEATURE |
				PTP_EXTTS_EDGES |
				PTP_STRICT_FLAGS))
		return -EOPNOTSUPP;

	pin = ptp_find_pin(shared->ptp_clock, PTP_PF_EXTTS,
			   rq->extts.index);
	if (pin == -1 || pin != LAN8814_PTP_EXTTS_NUM)
		return -EINVAL;

	mutex_lock(&shared->shared_lock);
	if (on)
		lan8814_ptp_extts_on(phydev, pin, rq->extts.flags);
	else
		lan8814_ptp_extts_off(phydev, pin);

	mutex_unlock(&shared->shared_lock);

	return 0;
}

static int lan8814_ptpci_enable(struct ptp_clock_info *ptpci,
				struct ptp_clock_request *rq, int on)
{
	switch (rq->type) {
	case PTP_CLK_REQ_PEROUT:
		return lan8814_ptp_perout(ptpci, rq, on);
	case PTP_CLK_REQ_EXTTS:
		return lan8814_ptp_extts(ptpci, rq, on);
	default:
		return -EINVAL;
	}
}

static int lan8814_ptpci_verify(struct ptp_clock_info *ptp, unsigned int pin,
				enum ptp_pin_function func, unsigned int chan)
{
	switch (func) {
	case PTP_PF_NONE:
	case PTP_PF_PEROUT:
		/* Only pins 0 and 1 can generate perout signals. And for pin 0
		 * there is only chan 0 (event A) and for pin 1 there is only
		 * chan 1 (event B)
		 */
		if (pin >= LAN8814_PTP_PEROUT_NUM || pin != chan)
			return -1;
		break;
	case PTP_PF_EXTTS:
		if (pin != LAN8814_PTP_EXTTS_NUM)
			return -1;
		break;
	default:
		return -1;
	}

	return 0;
}

static bool lan8814_get_sig_tx(struct sk_buff *skb, u16 *sig)
{
	struct ptp_header *ptp_header;
	u32 type;

	type = ptp_classify_raw(skb);
	ptp_header = ptp_parse_header(skb, type);

	if (!ptp_header)
		return false;

	*sig = (__force u16)(ntohs(ptp_header->sequence_id));
	return true;
}

static void lan8814_match_tx_skb(struct kszphy_ptp_priv *ptp_priv,
				 u32 seconds, u32 nsec, u16 seq_id)
{
	struct skb_shared_hwtstamps shhwtstamps;
	struct sk_buff *skb, *skb_tmp;
	unsigned long flags;
	bool ret = false;
	u16 skb_sig;

	spin_lock_irqsave(&ptp_priv->tx_queue.lock, flags);
	skb_queue_walk_safe(&ptp_priv->tx_queue, skb, skb_tmp) {
		if (!lan8814_get_sig_tx(skb, &skb_sig))
			continue;

		if (memcmp(&skb_sig, &seq_id, sizeof(seq_id)))
			continue;

		__skb_unlink(skb, &ptp_priv->tx_queue);
		ret = true;
		break;
	}
	spin_unlock_irqrestore(&ptp_priv->tx_queue.lock, flags);

	if (ret) {
		memset(&shhwtstamps, 0, sizeof(shhwtstamps));
		shhwtstamps.hwtstamp = ktime_set(seconds, nsec);
		skb_complete_tx_timestamp(skb, &shhwtstamps);
	}
}

static void lan8814_dequeue_tx_skb(struct kszphy_ptp_priv *ptp_priv)
{
	struct phy_device *phydev = ptp_priv->phydev;
	u32 seconds, nsec;
	u16 seq_id;

	lan8814_ptp_tx_ts_get(phydev, &seconds, &nsec, &seq_id);
	lan8814_match_tx_skb(ptp_priv, seconds, nsec, seq_id);
}

static void lan8814_get_tx_ts(struct kszphy_ptp_priv *ptp_priv)
{
	struct phy_device *phydev = ptp_priv->phydev;
	u32 reg;

	do {
		lan8814_dequeue_tx_skb(ptp_priv);

		/* If other timestamps are available in the FIFO,
		 * process them.
		 */
		reg = lanphy_read_page_reg(phydev, 5, PTP_CAP_INFO);
	} while (PTP_CAP_INFO_TX_TS_CNT_GET_(reg) > 0);
}

static bool lan8814_match_skb(struct kszphy_ptp_priv *ptp_priv,
			      struct lan8814_ptp_rx_ts *rx_ts)
{
	struct skb_shared_hwtstamps *shhwtstamps;
	struct sk_buff *skb, *skb_tmp;
	unsigned long flags;
	bool ret = false;
	u16 skb_sig;

	spin_lock_irqsave(&ptp_priv->rx_queue.lock, flags);
	skb_queue_walk_safe(&ptp_priv->rx_queue, skb, skb_tmp) {
		if (!lan8814_get_sig_rx(skb, &skb_sig))
			continue;

		if (memcmp(&skb_sig, &rx_ts->seq_id, sizeof(rx_ts->seq_id)))
			continue;

		__skb_unlink(skb, &ptp_priv->rx_queue);

		ret = true;
		break;
	}
	spin_unlock_irqrestore(&ptp_priv->rx_queue.lock, flags);

	if (ret) {
		shhwtstamps = skb_hwtstamps(skb);
		memset(shhwtstamps, 0, sizeof(*shhwtstamps));
		shhwtstamps->hwtstamp = ktime_set(rx_ts->seconds, rx_ts->nsec);
		netif_rx(skb);
	}

	return ret;
}

static void lan8814_match_rx_ts(struct kszphy_ptp_priv *ptp_priv,
				struct lan8814_ptp_rx_ts *rx_ts)
{
	unsigned long flags;

	/* If we failed to match the skb add it to the queue for when
	 * the frame will come
	 */
	if (!lan8814_match_skb(ptp_priv, rx_ts)) {
		spin_lock_irqsave(&ptp_priv->rx_ts_lock, flags);
		list_add(&rx_ts->list, &ptp_priv->rx_ts_list);
		spin_unlock_irqrestore(&ptp_priv->rx_ts_lock, flags);
	} else {
		kfree(rx_ts);
	}
}

static void lan8814_get_rx_ts(struct kszphy_ptp_priv *ptp_priv)
{
	struct phy_device *phydev = ptp_priv->phydev;
	struct lan8814_ptp_rx_ts *rx_ts;
	u32 reg;

	do {
		rx_ts = kzalloc(sizeof(*rx_ts), GFP_KERNEL);
		if (!rx_ts)
			return;

		lan8814_ptp_rx_ts_get(phydev, &rx_ts->seconds, &rx_ts->nsec,
				      &rx_ts->seq_id);
		lan8814_match_rx_ts(ptp_priv, rx_ts);

		/* If other timestamps are available in the FIFO,
		 * process them.
		 */
		reg = lanphy_read_page_reg(phydev, 5, PTP_CAP_INFO);
	} while (PTP_CAP_INFO_RX_TS_CNT_GET_(reg) > 0);
}

static void lan8814_handle_ptp_interrupt(struct phy_device *phydev, u16 status)
{
	struct kszphy_priv *priv = phydev->priv;
	struct kszphy_ptp_priv *ptp_priv = &priv->ptp_priv;

	if (status & PTP_TSU_INT_STS_PTP_TX_TS_EN_)
		lan8814_get_tx_ts(ptp_priv);

	if (status & PTP_TSU_INT_STS_PTP_RX_TS_EN_)
		lan8814_get_rx_ts(ptp_priv);

	if (status & PTP_TSU_INT_STS_PTP_TX_TS_OVRFL_INT_) {
		lan8814_flush_fifo(phydev, true);
		skb_queue_purge(&ptp_priv->tx_queue);
	}

	if (status & PTP_TSU_INT_STS_PTP_RX_TS_OVRFL_INT_) {
		lan8814_flush_fifo(phydev, false);
		skb_queue_purge(&ptp_priv->rx_queue);
	}
}

static int lan8814_gpio_process_cap(struct lan8814_shared_priv *shared)
{
	struct phy_device *phydev = shared->phydev;
	struct ptp_clock_event ptp_event = {0};
	unsigned long nsec;
	s64 sec;
	u16 tmp;

	/* This is 0 because whatever was the input pin it was mapped it to
	 * ltc gpio pin 0
	 */
	tmp = lanphy_read_page_reg(phydev, 4, PTP_GPIO_SEL);
	tmp |= PTP_GPIO_SEL_GPIO_SEL(0);
	lanphy_write_page_reg(phydev, 4, PTP_GPIO_SEL, tmp);

	tmp = lanphy_read_page_reg(phydev, 4, PTP_GPIO_CAP_STS);
	if (!(tmp & PTP_GPIO_CAP_STS_PTP_GPIO_RE_STS(0)) &&
	    !(tmp & PTP_GPIO_CAP_STS_PTP_GPIO_FE_STS(0)))
		return -1;

	if (tmp & BIT(0)) {
		sec = lanphy_read_page_reg(phydev, 4, PTP_GPIO_RE_LTC_SEC_HI_CAP);
		sec <<= 16;
		sec |= lanphy_read_page_reg(phydev, 4, PTP_GPIO_RE_LTC_SEC_LO_CAP);

		nsec = lanphy_read_page_reg(phydev, 4, PTP_GPIO_RE_LTC_NS_HI_CAP) & 0x3fff;
		nsec <<= 16;
		nsec |= lanphy_read_page_reg(phydev, 4, PTP_GPIO_RE_LTC_NS_LO_CAP);
	} else {
		sec = lanphy_read_page_reg(phydev, 4, PTP_GPIO_FE_LTC_SEC_HI_CAP);
		sec <<= 16;
		sec |= lanphy_read_page_reg(phydev, 4, PTP_GPIO_FE_LTC_SEC_LO_CAP);

		nsec = lanphy_read_page_reg(phydev, 4, PTP_GPIO_FE_LTC_NS_HI_CAP) & 0x3fff;
		nsec <<= 16;
		nsec |= lanphy_read_page_reg(phydev, 4, PTP_GPIO_RE_LTC_NS_LO_CAP);
	}

	ptp_event.index = 0;
	ptp_event.timestamp = ktime_set(sec, nsec);
	ptp_event.type = PTP_CLOCK_EXTTS;
	ptp_clock_event(shared->ptp_clock, &ptp_event);

	return 0;
}

static int lan8814_handle_gpio_interrupt(struct phy_device *phydev, u16 status)
{
	struct lan8814_shared_priv *shared = phy_package_get_priv(phydev);
	int ret;

	mutex_lock(&shared->shared_lock);
	ret = lan8814_gpio_process_cap(shared);
	mutex_unlock(&shared->shared_lock);

	return ret;
}

static int lan8804_config_init(struct phy_device *phydev)
{
	int val;

	/* MDI-X setting for swap A,B transmit */
	val = lanphy_read_page_reg(phydev, 2, LAN8804_ALIGN_SWAP);
	val &= ~LAN8804_ALIGN_TX_A_B_SWAP_MASK;
	val |= LAN8804_ALIGN_TX_A_B_SWAP;
	lanphy_write_page_reg(phydev, 2, LAN8804_ALIGN_SWAP, val);

	/* Make sure that the PHY will not stop generating the clock when the
	 * link partner goes down
	 */
	lanphy_write_page_reg(phydev, 31, LAN8814_CLOCK_MANAGEMENT, 0x27e);
	lanphy_read_page_reg(phydev, 1, LAN8814_LINK_QUALITY);

	return 0;
}

static irqreturn_t lan8804_handle_interrupt(struct phy_device *phydev)
{
	int status;

	status = phy_read(phydev, LAN8814_INTS);
	if (status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (status > 0)
		phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

#define LAN8804_OUTPUT_CONTROL			25
#define LAN8804_OUTPUT_CONTROL_INTR_BUFFER	BIT(14)
#define LAN8804_CONTROL				31
#define LAN8804_CONTROL_INTR_POLARITY		BIT(14)

static int lan8804_config_intr(struct phy_device *phydev)
{
	int err;

	/* This is an internal PHY of lan966x and is not possible to change the
	 * polarity on the GIC found in lan966x, therefore change the polarity
	 * of the interrupt in the PHY from being active low instead of active
	 * high.
	 */
	phy_write(phydev, LAN8804_CONTROL, LAN8804_CONTROL_INTR_POLARITY);

	/* By default interrupt buffer is open-drain in which case the interrupt
	 * can be active only low. Therefore change the interrupt buffer to be
	 * push-pull to be able to change interrupt polarity
	 */
	phy_write(phydev, LAN8804_OUTPUT_CONTROL,
		  LAN8804_OUTPUT_CONTROL_INTR_BUFFER);

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = phy_read(phydev, LAN8814_INTS);
		if (err < 0)
			return err;

		err = phy_write(phydev, LAN8814_INTC, LAN8814_INT_LINK);
		if (err)
			return err;
	} else {
		err = phy_write(phydev, LAN8814_INTC, 0);
		if (err)
			return err;

		err = phy_read(phydev, LAN8814_INTS);
		if (err < 0)
			return err;
	}

	return 0;
}

static irqreturn_t lan8814_handle_interrupt(struct phy_device *phydev)
{
	int ret = IRQ_NONE;
	int irq_status;

	irq_status = phy_read(phydev, LAN8814_INTS);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (irq_status & LAN8814_INT_LINK) {
		phy_trigger_machine(phydev);
		ret = IRQ_HANDLED;
	}

	while (true) {
		irq_status = lanphy_read_page_reg(phydev, 5, PTP_TSU_INT_STS);
		if (!irq_status)
			break;

		lan8814_handle_ptp_interrupt(phydev, irq_status);
		ret = IRQ_HANDLED;
	}

	if (!lan8814_handle_gpio_interrupt(phydev, irq_status))
		ret = IRQ_HANDLED;

	return ret;
}

static int lan8814_ack_interrupt(struct phy_device *phydev)
{
	/* bit[12..0] int status, which is a read and clear register. */
	int rc;

	rc = phy_read(phydev, LAN8814_INTS);

	return (rc < 0) ? rc : 0;
}

static int lan8814_config_intr(struct phy_device *phydev)
{
	int err;

	lanphy_write_page_reg(phydev, 4, LAN8814_INTR_CTRL_REG,
			      LAN8814_INTR_CTRL_REG_POLARITY |
			      LAN8814_INTR_CTRL_REG_INTR_ENABLE);

	/* enable / disable interrupts */
	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = lan8814_ack_interrupt(phydev);
		if (err)
			return err;

		err = phy_write(phydev, LAN8814_INTC, LAN8814_INT_LINK);
	} else {
		err = phy_write(phydev, LAN8814_INTC, 0);
		if (err)
			return err;

		err = lan8814_ack_interrupt(phydev);
	}

	return err;
}

static void lan8814_ptp_init(struct phy_device *phydev)
{
	struct kszphy_priv *priv = phydev->priv;
	struct kszphy_ptp_priv *ptp_priv = &priv->ptp_priv;
	u32 temp;

	if (!IS_ENABLED(CONFIG_PTP_1588_CLOCK) ||
	    !IS_ENABLED(CONFIG_NETWORK_PHY_TIMESTAMPING))
		return;

	lanphy_write_page_reg(phydev, 5, TSU_HARD_RESET, TSU_HARD_RESET_);

	temp = lanphy_read_page_reg(phydev, 5, PTP_TX_MOD);
	temp |= PTP_TX_MOD_BAD_UDPV4_CHKSUM_FORCE_FCS_DIS_;
	lanphy_write_page_reg(phydev, 5, PTP_TX_MOD, temp);

	temp = lanphy_read_page_reg(phydev, 5, PTP_RX_MOD);
	temp |= PTP_RX_MOD_BAD_UDPV4_CHKSUM_FORCE_FCS_DIS_;
	lanphy_write_page_reg(phydev, 5, PTP_RX_MOD, temp);

	lanphy_write_page_reg(phydev, 5, PTP_RX_PARSE_CONFIG, 0);
	lanphy_write_page_reg(phydev, 5, PTP_TX_PARSE_CONFIG, 0);

	/* Removing default registers configs related to L2 and IP */
	lanphy_write_page_reg(phydev, 5, PTP_TX_PARSE_L2_ADDR_EN, 0);
	lanphy_write_page_reg(phydev, 5, PTP_RX_PARSE_L2_ADDR_EN, 0);
	lanphy_write_page_reg(phydev, 5, PTP_TX_PARSE_IP_ADDR_EN, 0);
	lanphy_write_page_reg(phydev, 5, PTP_RX_PARSE_IP_ADDR_EN, 0);

	/* Disable checking for minorVersionPTP field */
	lanphy_write_page_reg(phydev, 5, PTP_RX_VERSION,
			      PTP_MAX_VERSION(0xff) | PTP_MIN_VERSION(0x0));
	lanphy_write_page_reg(phydev, 5, PTP_TX_VERSION,
			      PTP_MAX_VERSION(0xff) | PTP_MIN_VERSION(0x0));

	skb_queue_head_init(&ptp_priv->tx_queue);
	skb_queue_head_init(&ptp_priv->rx_queue);
	INIT_LIST_HEAD(&ptp_priv->rx_ts_list);
	spin_lock_init(&ptp_priv->rx_ts_lock);

	ptp_priv->phydev = phydev;

	ptp_priv->mii_ts.rxtstamp = lan8814_rxtstamp;
	ptp_priv->mii_ts.txtstamp = lan8814_txtstamp;
	ptp_priv->mii_ts.hwtstamp = lan8814_hwtstamp;
	ptp_priv->mii_ts.ts_info  = lan8814_ts_info;

	phydev->mii_ts = &ptp_priv->mii_ts;

	/* Timestamp selected by default to keep legacy API */
	phydev->default_timestamp = true;
}

static int lan8814_ptp_probe_once(struct phy_device *phydev)
{
	struct lan8814_shared_priv *shared = phy_package_get_priv(phydev);

	/* Initialise shared lock for clock*/
	mutex_init(&shared->shared_lock);

	shared->pin_config = devm_kmalloc_array(&phydev->mdio.dev,
						LAN8814_PTP_GPIO_NUM,
						sizeof(*shared->pin_config),
						GFP_KERNEL);
	if (!shared->pin_config)
		return -ENOMEM;

	for (int i = 0; i < LAN8814_PTP_GPIO_NUM; i++) {
		struct ptp_pin_desc *ptp_pin = &shared->pin_config[i];

		memset(ptp_pin, 0, sizeof(*ptp_pin));
		snprintf(ptp_pin->name,
			 sizeof(ptp_pin->name), "lan8814_ptp_pin_%02d", i);
		ptp_pin->index = i;
		ptp_pin->func =  PTP_PF_NONE;
	}

	shared->ptp_clock_info.owner = THIS_MODULE;
	snprintf(shared->ptp_clock_info.name, 30, "%s", phydev->drv->name);
	shared->ptp_clock_info.max_adj = 31249999;
	shared->ptp_clock_info.n_alarm = 0;
	shared->ptp_clock_info.n_ext_ts = LAN8814_PTP_EXTTS_NUM;
	shared->ptp_clock_info.n_pins = LAN8814_PTP_GPIO_NUM;
	shared->ptp_clock_info.pps = 0;
	shared->ptp_clock_info.pin_config = shared->pin_config;
	shared->ptp_clock_info.n_per_out = LAN8814_PTP_PEROUT_NUM;
	shared->ptp_clock_info.adjfine = lan8814_ptpci_adjfine;
	shared->ptp_clock_info.adjtime = lan8814_ptpci_adjtime;
	shared->ptp_clock_info.gettime64 = lan8814_ptpci_gettime64;
	shared->ptp_clock_info.settime64 = lan8814_ptpci_settime64;
	shared->ptp_clock_info.getcrosststamp = NULL;
	shared->ptp_clock_info.enable = lan8814_ptpci_enable;
	shared->ptp_clock_info.verify = lan8814_ptpci_verify;

	shared->ptp_clock = ptp_clock_register(&shared->ptp_clock_info,
					       &phydev->mdio.dev);
	if (IS_ERR(shared->ptp_clock)) {
		phydev_err(phydev, "ptp_clock_register failed %lu\n",
			   PTR_ERR(shared->ptp_clock));
		return -EINVAL;
	}

	/* Check if PHC support is missing at the configuration level */
	if (!shared->ptp_clock)
		return 0;

	phydev_dbg(phydev, "successfully registered ptp clock\n");

	shared->phydev = phydev;

	/* The EP.4 is shared between all the PHYs in the package and also it
	 * can be accessed by any of the PHYs
	 */
	lanphy_write_page_reg(phydev, 4, LTC_HARD_RESET, LTC_HARD_RESET_);
	lanphy_write_page_reg(phydev, 4, PTP_OPERATING_MODE,
			      PTP_OPERATING_MODE_STANDALONE_);

	/* Enable ptp to run LTC clock for ptp and gpio 1PPS operation */
	lanphy_write_page_reg(phydev, 4, PTP_CMD_CTL, PTP_CMD_CTL_PTP_ENABLE_);

	return 0;
}

static void lan8814_setup_led(struct phy_device *phydev, int val)
{
	int temp;

	temp = lanphy_read_page_reg(phydev, 5, LAN8814_LED_CTRL_1);

	if (val)
		temp |= LAN8814_LED_CTRL_1_KSZ9031_LED_MODE_;
	else
		temp &= ~LAN8814_LED_CTRL_1_KSZ9031_LED_MODE_;

	lanphy_write_page_reg(phydev, 5, LAN8814_LED_CTRL_1, temp);
}

static int lan8814_config_init(struct phy_device *phydev)
{
	struct kszphy_priv *lan8814 = phydev->priv;
	int val;

	/* Reset the PHY */
	val = lanphy_read_page_reg(phydev, 4, LAN8814_QSGMII_SOFT_RESET);
	val |= LAN8814_QSGMII_SOFT_RESET_BIT;
	lanphy_write_page_reg(phydev, 4, LAN8814_QSGMII_SOFT_RESET, val);

	/* Disable ANEG with QSGMII PCS Host side */
	val = lanphy_read_page_reg(phydev, 5, LAN8814_QSGMII_PCS1G_ANEG_CONFIG);
	val &= ~LAN8814_QSGMII_PCS1G_ANEG_CONFIG_ANEG_ENA;
	lanphy_write_page_reg(phydev, 5, LAN8814_QSGMII_PCS1G_ANEG_CONFIG, val);

	/* MDI-X setting for swap A,B transmit */
	val = lanphy_read_page_reg(phydev, 2, LAN8814_ALIGN_SWAP);
	val &= ~LAN8814_ALIGN_TX_A_B_SWAP_MASK;
	val |= LAN8814_ALIGN_TX_A_B_SWAP;
	lanphy_write_page_reg(phydev, 2, LAN8814_ALIGN_SWAP, val);

	if (lan8814->led_mode >= 0)
		lan8814_setup_led(phydev, lan8814->led_mode);

	return 0;
}

/* It is expected that there will not be any 'lan8814_take_coma_mode'
 * function called in suspend. Because the GPIO line can be shared, so if one of
 * the phys goes back in coma mode, then all the other PHYs will go, which is
 * wrong.
 */
static int lan8814_release_coma_mode(struct phy_device *phydev)
{
	struct gpio_desc *gpiod;

	gpiod = devm_gpiod_get_optional(&phydev->mdio.dev, "coma-mode",
					GPIOD_OUT_HIGH_OPEN_DRAIN |
					GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(gpiod))
		return PTR_ERR(gpiod);

	gpiod_set_consumer_name(gpiod, "LAN8814 coma mode");
	gpiod_set_value_cansleep(gpiod, 0);

	return 0;
}

static void lan8814_clear_2psp_bit(struct phy_device *phydev)
{
	u16 val;

	/* It was noticed that when traffic is passing through the PHY and the
	 * cable is removed then the LED was still one even though there is no
	 * link
	 */
	val = lanphy_read_page_reg(phydev, 2, LAN8814_EEE_STATE);
	val &= ~LAN8814_EEE_STATE_MASK2P5P;
	lanphy_write_page_reg(phydev, 2, LAN8814_EEE_STATE, val);
}

static void lan8814_update_meas_time(struct phy_device *phydev)
{
	u16 val;

	/* By setting the measure time to a value of 0xb this will allow cables
	 * longer than 100m to be used. This configuration can be used
	 * regardless of the mode of operation of the PHY
	 */
	val = lanphy_read_page_reg(phydev, 1, LAN8814_PD_CONTROLS);
	val &= ~LAN8814_PD_CONTROLS_PD_MEAS_TIME_MASK;
	val |= LAN8814_PD_CONTROLS_PD_MEAS_TIME_VAL;
	lanphy_write_page_reg(phydev, 1, LAN8814_PD_CONTROLS, val);
}

static int lan8814_probe(struct phy_device *phydev)
{
	const struct kszphy_type *type = phydev->drv->driver_data;
	struct kszphy_priv *priv;
	u16 addr;
	int err;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	priv->type = type;

	kszphy_parse_led_mode(phydev);

	/* Strap-in value for PHY address, below register read gives starting
	 * phy address value
	 */
	addr = lanphy_read_page_reg(phydev, 4, 0) & 0x1F;
	devm_phy_package_join(&phydev->mdio.dev, phydev,
			      addr, sizeof(struct lan8814_shared_priv));

	if (phy_package_init_once(phydev)) {
		err = lan8814_release_coma_mode(phydev);
		if (err)
			return err;

		err = lan8814_ptp_probe_once(phydev);
		if (err)
			return err;
	}

	lan8814_ptp_init(phydev);

	/* Errata workarounds */
	lan8814_clear_2psp_bit(phydev);
	lan8814_update_meas_time(phydev);

	return 0;
}

#define LAN8841_MMD_TIMER_REG			0
#define LAN8841_MMD0_REGISTER_17		17
#define LAN8841_MMD0_REGISTER_17_DROP_OPT(x)	((x) & 0x3)
#define LAN8841_MMD0_REGISTER_17_XMIT_TOG_TX_DIS	BIT(3)
#define LAN8841_OPERATION_MODE_STRAP_OVERRIDE_LOW_REG	2
#define LAN8841_OPERATION_MODE_STRAP_OVERRIDE_LOW_REG_MAGJACK	BIT(14)
#define LAN8841_MMD_ANALOG_REG			28
#define LAN8841_ANALOG_CONTROL_1		1
#define LAN8841_ANALOG_CONTROL_1_PLL_TRIM(x)	(((x) & 0x3) << 5)
#define LAN8841_ANALOG_CONTROL_10		13
#define LAN8841_ANALOG_CONTROL_10_PLL_DIV(x)	((x) & 0x3)
#define LAN8841_ANALOG_CONTROL_11		14
#define LAN8841_ANALOG_CONTROL_11_LDO_REF(x)	(((x) & 0x7) << 12)
#define LAN8841_TX_LOW_I_CH_C_D_POWER_MANAGMENT	69
#define LAN8841_TX_LOW_I_CH_C_D_POWER_MANAGMENT_VAL 0xbffc
#define LAN8841_BTRX_POWER_DOWN			70
#define LAN8841_BTRX_POWER_DOWN_QBIAS_CH_A	BIT(0)
#define LAN8841_BTRX_POWER_DOWN_BTRX_CH_A	BIT(1)
#define LAN8841_BTRX_POWER_DOWN_QBIAS_CH_B	BIT(2)
#define LAN8841_BTRX_POWER_DOWN_BTRX_CH_B	BIT(3)
#define LAN8841_BTRX_POWER_DOWN_BTRX_CH_C	BIT(5)
#define LAN8841_BTRX_POWER_DOWN_BTRX_CH_D	BIT(7)
#define LAN8841_ADC_CHANNEL_MASK		198
#define LAN8841_PTP_RX_PARSE_L2_ADDR_EN		370
#define LAN8841_PTP_RX_PARSE_IP_ADDR_EN		371
#define LAN8841_PTP_RX_VERSION			374
#define LAN8841_PTP_TX_PARSE_L2_ADDR_EN		434
#define LAN8841_PTP_TX_PARSE_IP_ADDR_EN		435
#define LAN8841_PTP_TX_VERSION			438
#define LAN8841_PTP_CMD_CTL			256
#define LAN8841_PTP_CMD_CTL_PTP_ENABLE		BIT(2)
#define LAN8841_PTP_CMD_CTL_PTP_DISABLE		BIT(1)
#define LAN8841_PTP_CMD_CTL_PTP_RESET		BIT(0)
#define LAN8841_PTP_RX_PARSE_CONFIG		368
#define LAN8841_PTP_TX_PARSE_CONFIG		432
#define LAN8841_PTP_RX_MODE			381
#define LAN8841_PTP_INSERT_TS_EN		BIT(0)
#define LAN8841_PTP_INSERT_TS_32BIT		BIT(1)

static int lan8841_config_init(struct phy_device *phydev)
{
	int ret;

	ret = ksz9131_config_init(phydev);
	if (ret)
		return ret;

	/* Initialize the HW by resetting everything */
	phy_modify_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
		       LAN8841_PTP_CMD_CTL,
		       LAN8841_PTP_CMD_CTL_PTP_RESET,
		       LAN8841_PTP_CMD_CTL_PTP_RESET);

	phy_modify_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
		       LAN8841_PTP_CMD_CTL,
		       LAN8841_PTP_CMD_CTL_PTP_ENABLE,
		       LAN8841_PTP_CMD_CTL_PTP_ENABLE);

	/* Don't process any frames */
	phy_write_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
		      LAN8841_PTP_RX_PARSE_CONFIG, 0);
	phy_write_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
		      LAN8841_PTP_TX_PARSE_CONFIG, 0);
	phy_write_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
		      LAN8841_PTP_TX_PARSE_L2_ADDR_EN, 0);
	phy_write_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
		      LAN8841_PTP_RX_PARSE_L2_ADDR_EN, 0);
	phy_write_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
		      LAN8841_PTP_TX_PARSE_IP_ADDR_EN, 0);
	phy_write_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
		      LAN8841_PTP_RX_PARSE_IP_ADDR_EN, 0);

	/* Disable checking for minorVersionPTP field */
	phy_write_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
		      LAN8841_PTP_RX_VERSION, 0xff00);
	phy_write_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
		      LAN8841_PTP_TX_VERSION, 0xff00);

	/* 100BT Clause 40 improvenent errata */
	phy_write_mmd(phydev, LAN8841_MMD_ANALOG_REG,
		      LAN8841_ANALOG_CONTROL_1,
		      LAN8841_ANALOG_CONTROL_1_PLL_TRIM(0x2));
	phy_write_mmd(phydev, LAN8841_MMD_ANALOG_REG,
		      LAN8841_ANALOG_CONTROL_10,
		      LAN8841_ANALOG_CONTROL_10_PLL_DIV(0x1));

	/* 10M/100M Ethernet Signal Tuning Errata for Shorted-Center Tap
	 * Magnetics
	 */
	ret = phy_read_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
			   LAN8841_OPERATION_MODE_STRAP_OVERRIDE_LOW_REG);
	if (ret & LAN8841_OPERATION_MODE_STRAP_OVERRIDE_LOW_REG_MAGJACK) {
		phy_write_mmd(phydev, LAN8841_MMD_ANALOG_REG,
			      LAN8841_TX_LOW_I_CH_C_D_POWER_MANAGMENT,
			      LAN8841_TX_LOW_I_CH_C_D_POWER_MANAGMENT_VAL);
		phy_write_mmd(phydev, LAN8841_MMD_ANALOG_REG,
			      LAN8841_BTRX_POWER_DOWN,
			      LAN8841_BTRX_POWER_DOWN_QBIAS_CH_A |
			      LAN8841_BTRX_POWER_DOWN_BTRX_CH_A |
			      LAN8841_BTRX_POWER_DOWN_QBIAS_CH_B |
			      LAN8841_BTRX_POWER_DOWN_BTRX_CH_B |
			      LAN8841_BTRX_POWER_DOWN_BTRX_CH_C |
			      LAN8841_BTRX_POWER_DOWN_BTRX_CH_D);
	}

	/* LDO Adjustment errata */
	phy_write_mmd(phydev, LAN8841_MMD_ANALOG_REG,
		      LAN8841_ANALOG_CONTROL_11,
		      LAN8841_ANALOG_CONTROL_11_LDO_REF(1));

	/* 100BT RGMII latency tuning errata */
	phy_write_mmd(phydev, MDIO_MMD_PMAPMD,
		      LAN8841_ADC_CHANNEL_MASK, 0x0);
	phy_write_mmd(phydev, LAN8841_MMD_TIMER_REG,
		      LAN8841_MMD0_REGISTER_17,
		      LAN8841_MMD0_REGISTER_17_DROP_OPT(2) |
		      LAN8841_MMD0_REGISTER_17_XMIT_TOG_TX_DIS);

	return 0;
}

#define LAN8841_OUTPUT_CTRL			25
#define LAN8841_OUTPUT_CTRL_INT_BUFFER		BIT(14)
#define LAN8841_INT_PTP				BIT(9)

static int lan8841_config_intr(struct phy_device *phydev)
{
	int err;

	phy_modify(phydev, LAN8841_OUTPUT_CTRL,
		   LAN8841_OUTPUT_CTRL_INT_BUFFER, 0);

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = phy_read(phydev, LAN8814_INTS);
		if (err < 0)
			return err;

		/* Enable / disable interrupts. It is OK to enable PTP interrupt
		 * even if it PTP is not enabled. Because the underneath blocks
		 * will not enable the PTP so we will never get the PTP
		 * interrupt.
		 */
		err = phy_write(phydev, LAN8814_INTC,
				LAN8814_INT_LINK | LAN8841_INT_PTP);
	} else {
		err = phy_write(phydev, LAN8814_INTC, 0);
		if (err)
			return err;

		err = phy_read(phydev, LAN8814_INTS);
		if (err < 0)
			return err;

		/* Getting a positive value doesn't mean that is an error, it
		 * just indicates what was the status. Therefore make sure to
		 * clear the value and say that there is no error.
		 */
		err = 0;
	}

	return err;
}

#define LAN8841_PTP_TX_EGRESS_SEC_LO			453
#define LAN8841_PTP_TX_EGRESS_SEC_HI			452
#define LAN8841_PTP_TX_EGRESS_NS_LO			451
#define LAN8841_PTP_TX_EGRESS_NS_HI			450
#define LAN8841_PTP_TX_EGRESS_NSEC_HI_VALID		BIT(15)
#define LAN8841_PTP_TX_MSG_HEADER2			455

static bool lan8841_ptp_get_tx_ts(struct kszphy_ptp_priv *ptp_priv,
				  u32 *sec, u32 *nsec, u16 *seq)
{
	struct phy_device *phydev = ptp_priv->phydev;

	*nsec = phy_read_mmd(phydev, 2, LAN8841_PTP_TX_EGRESS_NS_HI);
	if (!(*nsec & LAN8841_PTP_TX_EGRESS_NSEC_HI_VALID))
		return false;

	*nsec = ((*nsec & 0x3fff) << 16);
	*nsec = *nsec | phy_read_mmd(phydev, 2, LAN8841_PTP_TX_EGRESS_NS_LO);

	*sec = phy_read_mmd(phydev, 2, LAN8841_PTP_TX_EGRESS_SEC_HI);
	*sec = *sec << 16;
	*sec = *sec | phy_read_mmd(phydev, 2, LAN8841_PTP_TX_EGRESS_SEC_LO);

	*seq = phy_read_mmd(phydev, 2, LAN8841_PTP_TX_MSG_HEADER2);

	return true;
}

static void lan8841_ptp_process_tx_ts(struct kszphy_ptp_priv *ptp_priv)
{
	u32 sec, nsec;
	u16 seq;

	while (lan8841_ptp_get_tx_ts(ptp_priv, &sec, &nsec, &seq))
		lan8814_match_tx_skb(ptp_priv, sec, nsec, seq);
}

#define LAN8841_PTP_INT_STS			259
#define LAN8841_PTP_INT_STS_PTP_TX_TS_OVRFL_INT	BIT(13)
#define LAN8841_PTP_INT_STS_PTP_TX_TS_INT	BIT(12)
#define LAN8841_PTP_INT_STS_PTP_GPIO_CAP_INT	BIT(2)

static void lan8841_ptp_flush_fifo(struct kszphy_ptp_priv *ptp_priv)
{
	struct phy_device *phydev = ptp_priv->phydev;
	int i;

	for (i = 0; i < FIFO_SIZE; ++i)
		phy_read_mmd(phydev, 2, LAN8841_PTP_TX_MSG_HEADER2);

	phy_read_mmd(phydev, 2, LAN8841_PTP_INT_STS);
}

#define LAN8841_PTP_GPIO_CAP_STS			506
#define LAN8841_PTP_GPIO_SEL				327
#define LAN8841_PTP_GPIO_SEL_GPIO_SEL(gpio)		((gpio) << 8)
#define LAN8841_PTP_GPIO_RE_LTC_SEC_HI_CAP		498
#define LAN8841_PTP_GPIO_RE_LTC_SEC_LO_CAP		499
#define LAN8841_PTP_GPIO_RE_LTC_NS_HI_CAP		500
#define LAN8841_PTP_GPIO_RE_LTC_NS_LO_CAP		501
#define LAN8841_PTP_GPIO_FE_LTC_SEC_HI_CAP		502
#define LAN8841_PTP_GPIO_FE_LTC_SEC_LO_CAP		503
#define LAN8841_PTP_GPIO_FE_LTC_NS_HI_CAP		504
#define LAN8841_PTP_GPIO_FE_LTC_NS_LO_CAP		505

static void lan8841_gpio_process_cap(struct kszphy_ptp_priv *ptp_priv)
{
	struct phy_device *phydev = ptp_priv->phydev;
	struct ptp_clock_event ptp_event = {0};
	int pin, ret, tmp;
	s32 sec, nsec;

	pin = ptp_find_pin_unlocked(ptp_priv->ptp_clock, PTP_PF_EXTTS, 0);
	if (pin == -1)
		return;

	tmp = phy_read_mmd(phydev, 2, LAN8841_PTP_GPIO_CAP_STS);
	if (tmp < 0)
		return;

	ret = phy_write_mmd(phydev, 2, LAN8841_PTP_GPIO_SEL,
			    LAN8841_PTP_GPIO_SEL_GPIO_SEL(pin));
	if (ret)
		return;

	mutex_lock(&ptp_priv->ptp_lock);
	if (tmp & BIT(pin)) {
		sec = phy_read_mmd(phydev, 2, LAN8841_PTP_GPIO_RE_LTC_SEC_HI_CAP);
		sec <<= 16;
		sec |= phy_read_mmd(phydev, 2, LAN8841_PTP_GPIO_RE_LTC_SEC_LO_CAP);

		nsec = phy_read_mmd(phydev, 2, LAN8841_PTP_GPIO_RE_LTC_NS_HI_CAP) & 0x3fff;
		nsec <<= 16;
		nsec |= phy_read_mmd(phydev, 2, LAN8841_PTP_GPIO_RE_LTC_NS_LO_CAP);
	} else {
		sec = phy_read_mmd(phydev, 2, LAN8841_PTP_GPIO_FE_LTC_SEC_HI_CAP);
		sec <<= 16;
		sec |= phy_read_mmd(phydev, 2, LAN8841_PTP_GPIO_FE_LTC_SEC_LO_CAP);

		nsec = phy_read_mmd(phydev, 2, LAN8841_PTP_GPIO_FE_LTC_NS_HI_CAP) & 0x3fff;
		nsec <<= 16;
		nsec |= phy_read_mmd(phydev, 2, LAN8841_PTP_GPIO_FE_LTC_NS_LO_CAP);
	}
	mutex_unlock(&ptp_priv->ptp_lock);
	ret = phy_write_mmd(phydev, 2, LAN8841_PTP_GPIO_SEL, 0);
	if (ret)
		return;

	ptp_event.index = 0;
	ptp_event.timestamp = ktime_set(sec, nsec);
	ptp_event.type = PTP_CLOCK_EXTTS;
	ptp_clock_event(ptp_priv->ptp_clock, &ptp_event);
}

static void lan8841_handle_ptp_interrupt(struct phy_device *phydev)
{
	struct kszphy_priv *priv = phydev->priv;
	struct kszphy_ptp_priv *ptp_priv = &priv->ptp_priv;
	u16 status;

	do {
		status = phy_read_mmd(phydev, 2, LAN8841_PTP_INT_STS);

		if (status & LAN8841_PTP_INT_STS_PTP_TX_TS_INT)
			lan8841_ptp_process_tx_ts(ptp_priv);

		if (status & LAN8841_PTP_INT_STS_PTP_GPIO_CAP_INT)
			lan8841_gpio_process_cap(ptp_priv);

		if (status & LAN8841_PTP_INT_STS_PTP_TX_TS_OVRFL_INT) {
			lan8841_ptp_flush_fifo(ptp_priv);
			skb_queue_purge(&ptp_priv->tx_queue);
		}

	} while (status & (LAN8841_PTP_INT_STS_PTP_TX_TS_INT |
			   LAN8841_PTP_INT_STS_PTP_GPIO_CAP_INT |
			   LAN8841_PTP_INT_STS_PTP_TX_TS_OVRFL_INT));
}

#define LAN8841_INTS_PTP		BIT(9)

static irqreturn_t lan8841_handle_interrupt(struct phy_device *phydev)
{
	irqreturn_t ret = IRQ_NONE;
	int irq_status;

	irq_status = phy_read(phydev, LAN8814_INTS);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (irq_status & LAN8814_INT_LINK) {
		phy_trigger_machine(phydev);
		ret = IRQ_HANDLED;
	}

	if (irq_status & LAN8841_INTS_PTP) {
		lan8841_handle_ptp_interrupt(phydev);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static int lan8841_ts_info(struct mii_timestamper *mii_ts,
			   struct kernel_ethtool_ts_info *info)
{
	struct kszphy_ptp_priv *ptp_priv;

	ptp_priv = container_of(mii_ts, struct kszphy_ptp_priv, mii_ts);

	info->phc_index = ptp_priv->ptp_clock ?
				ptp_clock_index(ptp_priv->ptp_clock) : -1;
	if (info->phc_index == -1)
		return 0;

	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	info->tx_types = (1 << HWTSTAMP_TX_OFF) |
			 (1 << HWTSTAMP_TX_ON) |
			 (1 << HWTSTAMP_TX_ONESTEP_SYNC);

	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			   (1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
			   (1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			   (1 << HWTSTAMP_FILTER_PTP_V2_EVENT);

	return 0;
}

#define LAN8841_PTP_INT_EN			260
#define LAN8841_PTP_INT_EN_PTP_TX_TS_OVRFL_EN	BIT(13)
#define LAN8841_PTP_INT_EN_PTP_TX_TS_EN		BIT(12)

static void lan8841_ptp_enable_processing(struct kszphy_ptp_priv *ptp_priv,
					  bool enable)
{
	struct phy_device *phydev = ptp_priv->phydev;

	if (enable) {
		/* Enable interrupts on the TX side */
		phy_modify_mmd(phydev, 2, LAN8841_PTP_INT_EN,
			       LAN8841_PTP_INT_EN_PTP_TX_TS_OVRFL_EN |
			       LAN8841_PTP_INT_EN_PTP_TX_TS_EN,
			       LAN8841_PTP_INT_EN_PTP_TX_TS_OVRFL_EN |
			       LAN8841_PTP_INT_EN_PTP_TX_TS_EN);

		/* Enable the modification of the frame on RX side,
		 * this will add the ns and 2 bits of sec in the reserved field
		 * of the PTP header
		 */
		phy_modify_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
			       LAN8841_PTP_RX_MODE,
			       LAN8841_PTP_INSERT_TS_EN |
			       LAN8841_PTP_INSERT_TS_32BIT,
			       LAN8841_PTP_INSERT_TS_EN |
			       LAN8841_PTP_INSERT_TS_32BIT);

		ptp_schedule_worker(ptp_priv->ptp_clock, 0);
	} else {
		/* Disable interrupts on the TX side */
		phy_modify_mmd(phydev, 2, LAN8841_PTP_INT_EN,
			       LAN8841_PTP_INT_EN_PTP_TX_TS_OVRFL_EN |
			       LAN8841_PTP_INT_EN_PTP_TX_TS_EN, 0);

		/* Disable modification of the RX frames */
		phy_modify_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
			       LAN8841_PTP_RX_MODE,
			       LAN8841_PTP_INSERT_TS_EN |
			       LAN8841_PTP_INSERT_TS_32BIT, 0);

		ptp_cancel_worker_sync(ptp_priv->ptp_clock);
	}
}

#define LAN8841_PTP_RX_TIMESTAMP_EN		379
#define LAN8841_PTP_TX_TIMESTAMP_EN		443
#define LAN8841_PTP_TX_MOD			445

static int lan8841_hwtstamp(struct mii_timestamper *mii_ts,
			    struct kernel_hwtstamp_config *config,
			    struct netlink_ext_ack *extack)
{
	struct kszphy_ptp_priv *ptp_priv = container_of(mii_ts, struct kszphy_ptp_priv, mii_ts);
	struct phy_device *phydev = ptp_priv->phydev;
	int txcfg = 0, rxcfg = 0;
	int pkt_ts_enable;

	ptp_priv->hwts_tx_type = config->tx_type;
	ptp_priv->rx_filter = config->rx_filter;

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		ptp_priv->layer = 0;
		ptp_priv->version = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		ptp_priv->layer = PTP_CLASS_L4;
		ptp_priv->version = PTP_CLASS_V2;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		ptp_priv->layer = PTP_CLASS_L2;
		ptp_priv->version = PTP_CLASS_V2;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		ptp_priv->layer = PTP_CLASS_L4 | PTP_CLASS_L2;
		ptp_priv->version = PTP_CLASS_V2;
		break;
	default:
		return -ERANGE;
	}

	/* Setup parsing of the frames and enable the timestamping for ptp
	 * frames
	 */
	if (ptp_priv->layer & PTP_CLASS_L2) {
		rxcfg |= PTP_RX_PARSE_CONFIG_LAYER2_EN_;
		txcfg |= PTP_TX_PARSE_CONFIG_LAYER2_EN_;
	} else if (ptp_priv->layer & PTP_CLASS_L4) {
		rxcfg |= PTP_RX_PARSE_CONFIG_IPV4_EN_ | PTP_RX_PARSE_CONFIG_IPV6_EN_;
		txcfg |= PTP_TX_PARSE_CONFIG_IPV4_EN_ | PTP_TX_PARSE_CONFIG_IPV6_EN_;
	}

	phy_write_mmd(phydev, 2, LAN8841_PTP_RX_PARSE_CONFIG, rxcfg);
	phy_write_mmd(phydev, 2, LAN8841_PTP_TX_PARSE_CONFIG, txcfg);

	pkt_ts_enable = PTP_TIMESTAMP_EN_SYNC_ | PTP_TIMESTAMP_EN_DREQ_ |
			PTP_TIMESTAMP_EN_PDREQ_ | PTP_TIMESTAMP_EN_PDRES_;
	phy_write_mmd(phydev, 2, LAN8841_PTP_RX_TIMESTAMP_EN, pkt_ts_enable);
	phy_write_mmd(phydev, 2, LAN8841_PTP_TX_TIMESTAMP_EN, pkt_ts_enable);

	/* Enable / disable of the TX timestamp in the SYNC frames */
	phy_modify_mmd(phydev, 2, LAN8841_PTP_TX_MOD,
		       PTP_TX_MOD_TX_PTP_SYNC_TS_INSERT_,
		       ptp_priv->hwts_tx_type == HWTSTAMP_TX_ONESTEP_SYNC ?
				PTP_TX_MOD_TX_PTP_SYNC_TS_INSERT_ : 0);

	/* Now enable/disable the timestamping */
	lan8841_ptp_enable_processing(ptp_priv,
				      config->rx_filter != HWTSTAMP_FILTER_NONE);

	skb_queue_purge(&ptp_priv->tx_queue);

	lan8841_ptp_flush_fifo(ptp_priv);

	return 0;
}

static bool lan8841_rxtstamp(struct mii_timestamper *mii_ts,
			     struct sk_buff *skb, int type)
{
	struct kszphy_ptp_priv *ptp_priv =
			container_of(mii_ts, struct kszphy_ptp_priv, mii_ts);
	struct ptp_header *header = ptp_parse_header(skb, type);
	struct skb_shared_hwtstamps *shhwtstamps;
	struct timespec64 ts;
	unsigned long flags;
	u32 ts_header;

	if (!header)
		return false;

	if (ptp_priv->rx_filter == HWTSTAMP_FILTER_NONE ||
	    type == PTP_CLASS_NONE)
		return false;

	if ((type & ptp_priv->version) == 0 || (type & ptp_priv->layer) == 0)
		return false;

	spin_lock_irqsave(&ptp_priv->seconds_lock, flags);
	ts.tv_sec = ptp_priv->seconds;
	spin_unlock_irqrestore(&ptp_priv->seconds_lock, flags);
	ts_header = __be32_to_cpu(header->reserved2);

	shhwtstamps = skb_hwtstamps(skb);
	memset(shhwtstamps, 0, sizeof(*shhwtstamps));

	/* Check for any wrap arounds for the second part */
	if ((ts.tv_sec & GENMASK(1, 0)) == 0 && (ts_header >> 30) == 3)
		ts.tv_sec -= GENMASK(1, 0) + 1;
	else if ((ts.tv_sec & GENMASK(1, 0)) == 3 && (ts_header >> 30) == 0)
		ts.tv_sec += 1;

	shhwtstamps->hwtstamp =
		ktime_set((ts.tv_sec & ~(GENMASK(1, 0))) | ts_header >> 30,
			  ts_header & GENMASK(29, 0));
	header->reserved2 = 0;

	netif_rx(skb);

	return true;
}

#define LAN8841_EVENT_A		0
#define LAN8841_EVENT_B		1
#define LAN8841_PTP_LTC_TARGET_SEC_HI(event)	((event) == LAN8841_EVENT_A ? 278 : 288)
#define LAN8841_PTP_LTC_TARGET_SEC_LO(event)	((event) == LAN8841_EVENT_A ? 279 : 289)
#define LAN8841_PTP_LTC_TARGET_NS_HI(event)	((event) == LAN8841_EVENT_A ? 280 : 290)
#define LAN8841_PTP_LTC_TARGET_NS_LO(event)	((event) == LAN8841_EVENT_A ? 281 : 291)

static int lan8841_ptp_set_target(struct kszphy_ptp_priv *ptp_priv, u8 event,
				  s64 sec, u32 nsec)
{
	struct phy_device *phydev = ptp_priv->phydev;
	int ret;

	ret = phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_TARGET_SEC_HI(event),
			    upper_16_bits(sec));
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_TARGET_SEC_LO(event),
			    lower_16_bits(sec));
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_TARGET_NS_HI(event) & 0x3fff,
			    upper_16_bits(nsec));
	if (ret)
		return ret;

	return phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_TARGET_NS_LO(event),
			    lower_16_bits(nsec));
}

#define LAN8841_BUFFER_TIME	2

static int lan8841_ptp_update_target(struct kszphy_ptp_priv *ptp_priv,
				     const struct timespec64 *ts)
{
	return lan8841_ptp_set_target(ptp_priv, LAN8841_EVENT_A,
				      ts->tv_sec + LAN8841_BUFFER_TIME, 0);
}

#define LAN8841_PTP_LTC_TARGET_RELOAD_SEC_HI(event)	((event) == LAN8841_EVENT_A ? 282 : 292)
#define LAN8841_PTP_LTC_TARGET_RELOAD_SEC_LO(event)	((event) == LAN8841_EVENT_A ? 283 : 293)
#define LAN8841_PTP_LTC_TARGET_RELOAD_NS_HI(event)	((event) == LAN8841_EVENT_A ? 284 : 294)
#define LAN8841_PTP_LTC_TARGET_RELOAD_NS_LO(event)	((event) == LAN8841_EVENT_A ? 285 : 295)

static int lan8841_ptp_set_reload(struct kszphy_ptp_priv *ptp_priv, u8 event,
				  s64 sec, u32 nsec)
{
	struct phy_device *phydev = ptp_priv->phydev;
	int ret;

	ret = phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_TARGET_RELOAD_SEC_HI(event),
			    upper_16_bits(sec));
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_TARGET_RELOAD_SEC_LO(event),
			    lower_16_bits(sec));
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_TARGET_RELOAD_NS_HI(event) & 0x3fff,
			    upper_16_bits(nsec));
	if (ret)
		return ret;

	return phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_TARGET_RELOAD_NS_LO(event),
			     lower_16_bits(nsec));
}

#define LAN8841_PTP_LTC_SET_SEC_HI	262
#define LAN8841_PTP_LTC_SET_SEC_MID	263
#define LAN8841_PTP_LTC_SET_SEC_LO	264
#define LAN8841_PTP_LTC_SET_NS_HI	265
#define LAN8841_PTP_LTC_SET_NS_LO	266
#define LAN8841_PTP_CMD_CTL_PTP_LTC_LOAD	BIT(4)

static int lan8841_ptp_settime64(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct kszphy_ptp_priv *ptp_priv = container_of(ptp, struct kszphy_ptp_priv,
							ptp_clock_info);
	struct phy_device *phydev = ptp_priv->phydev;
	unsigned long flags;
	int ret;

	/* Set the value to be stored */
	mutex_lock(&ptp_priv->ptp_lock);
	phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_SET_SEC_LO, lower_16_bits(ts->tv_sec));
	phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_SET_SEC_MID, upper_16_bits(ts->tv_sec));
	phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_SET_SEC_HI, upper_32_bits(ts->tv_sec) & 0xffff);
	phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_SET_NS_LO, lower_16_bits(ts->tv_nsec));
	phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_SET_NS_HI, upper_16_bits(ts->tv_nsec) & 0x3fff);

	/* Set the command to load the LTC */
	phy_write_mmd(phydev, 2, LAN8841_PTP_CMD_CTL,
		      LAN8841_PTP_CMD_CTL_PTP_LTC_LOAD);
	ret = lan8841_ptp_update_target(ptp_priv, ts);
	mutex_unlock(&ptp_priv->ptp_lock);

	spin_lock_irqsave(&ptp_priv->seconds_lock, flags);
	ptp_priv->seconds = ts->tv_sec;
	spin_unlock_irqrestore(&ptp_priv->seconds_lock, flags);

	return ret;
}

#define LAN8841_PTP_LTC_RD_SEC_HI	358
#define LAN8841_PTP_LTC_RD_SEC_MID	359
#define LAN8841_PTP_LTC_RD_SEC_LO	360
#define LAN8841_PTP_LTC_RD_NS_HI	361
#define LAN8841_PTP_LTC_RD_NS_LO	362
#define LAN8841_PTP_CMD_CTL_PTP_LTC_READ	BIT(3)

static int lan8841_ptp_gettime64(struct ptp_clock_info *ptp,
				 struct timespec64 *ts)
{
	struct kszphy_ptp_priv *ptp_priv = container_of(ptp, struct kszphy_ptp_priv,
							ptp_clock_info);
	struct phy_device *phydev = ptp_priv->phydev;
	time64_t s;
	s64 ns;

	mutex_lock(&ptp_priv->ptp_lock);
	/* Issue the command to read the LTC */
	phy_write_mmd(phydev, 2, LAN8841_PTP_CMD_CTL,
		      LAN8841_PTP_CMD_CTL_PTP_LTC_READ);

	/* Read the LTC */
	s = phy_read_mmd(phydev, 2, LAN8841_PTP_LTC_RD_SEC_HI);
	s <<= 16;
	s |= phy_read_mmd(phydev, 2, LAN8841_PTP_LTC_RD_SEC_MID);
	s <<= 16;
	s |= phy_read_mmd(phydev, 2, LAN8841_PTP_LTC_RD_SEC_LO);

	ns = phy_read_mmd(phydev, 2, LAN8841_PTP_LTC_RD_NS_HI) & 0x3fff;
	ns <<= 16;
	ns |= phy_read_mmd(phydev, 2, LAN8841_PTP_LTC_RD_NS_LO);
	mutex_unlock(&ptp_priv->ptp_lock);

	set_normalized_timespec64(ts, s, ns);
	return 0;
}

static void lan8841_ptp_getseconds(struct ptp_clock_info *ptp,
				   struct timespec64 *ts)
{
	struct kszphy_ptp_priv *ptp_priv = container_of(ptp, struct kszphy_ptp_priv,
							ptp_clock_info);
	struct phy_device *phydev = ptp_priv->phydev;
	time64_t s;

	mutex_lock(&ptp_priv->ptp_lock);
	/* Issue the command to read the LTC */
	phy_write_mmd(phydev, 2, LAN8841_PTP_CMD_CTL,
		      LAN8841_PTP_CMD_CTL_PTP_LTC_READ);

	/* Read the LTC */
	s = phy_read_mmd(phydev, 2, LAN8841_PTP_LTC_RD_SEC_HI);
	s <<= 16;
	s |= phy_read_mmd(phydev, 2, LAN8841_PTP_LTC_RD_SEC_MID);
	s <<= 16;
	s |= phy_read_mmd(phydev, 2, LAN8841_PTP_LTC_RD_SEC_LO);
	mutex_unlock(&ptp_priv->ptp_lock);

	set_normalized_timespec64(ts, s, 0);
}

#define LAN8841_PTP_LTC_STEP_ADJ_LO			276
#define LAN8841_PTP_LTC_STEP_ADJ_HI			275
#define LAN8841_PTP_LTC_STEP_ADJ_DIR			BIT(15)
#define LAN8841_PTP_CMD_CTL_PTP_LTC_STEP_SECONDS	BIT(5)
#define LAN8841_PTP_CMD_CTL_PTP_LTC_STEP_NANOSECONDS	BIT(6)

static int lan8841_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct kszphy_ptp_priv *ptp_priv = container_of(ptp, struct kszphy_ptp_priv,
							ptp_clock_info);
	struct phy_device *phydev = ptp_priv->phydev;
	struct timespec64 ts;
	bool add = true;
	u32 nsec;
	s32 sec;
	int ret;

	/* The HW allows up to 15 sec to adjust the time, but here we limit to
	 * 10 sec the adjustment. The reason is, in case the adjustment is 14
	 * sec and 999999999 nsec, then we add 8ns to compansate the actual
	 * increment so the value can be bigger than 15 sec. Therefore limit the
	 * possible adjustments so we will not have these corner cases
	 */
	if (delta > 10000000000LL || delta < -10000000000LL) {
		/* The timeadjustment is too big, so fall back using set time */
		u64 now;

		ptp->gettime64(ptp, &ts);

		now = ktime_to_ns(timespec64_to_ktime(ts));
		ts = ns_to_timespec64(now + delta);

		ptp->settime64(ptp, &ts);
		return 0;
	}

	sec = div_u64_rem(delta < 0 ? -delta : delta, NSEC_PER_SEC, &nsec);
	if (delta < 0 && nsec != 0) {
		/* It is not allowed to adjust low the nsec part, therefore
		 * subtract more from second part and add to nanosecond such
		 * that would roll over, so the second part will increase
		 */
		sec--;
		nsec = NSEC_PER_SEC - nsec;
	}

	/* Calculate the adjustments and the direction */
	if (delta < 0)
		add = false;

	if (nsec > 0)
		/* add 8 ns to cover the likely normal increment */
		nsec += 8;

	if (nsec >= NSEC_PER_SEC) {
		/* carry into seconds */
		sec++;
		nsec -= NSEC_PER_SEC;
	}

	mutex_lock(&ptp_priv->ptp_lock);
	if (sec) {
		phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_STEP_ADJ_LO, sec);
		phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_STEP_ADJ_HI,
			      add ? LAN8841_PTP_LTC_STEP_ADJ_DIR : 0);
		phy_write_mmd(phydev, 2, LAN8841_PTP_CMD_CTL,
			      LAN8841_PTP_CMD_CTL_PTP_LTC_STEP_SECONDS);
	}

	if (nsec) {
		phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_STEP_ADJ_LO,
			      nsec & 0xffff);
		phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_STEP_ADJ_HI,
			      (nsec >> 16) & 0x3fff);
		phy_write_mmd(phydev, 2, LAN8841_PTP_CMD_CTL,
			      LAN8841_PTP_CMD_CTL_PTP_LTC_STEP_NANOSECONDS);
	}
	mutex_unlock(&ptp_priv->ptp_lock);

	/* Update the target clock */
	ptp->gettime64(ptp, &ts);
	mutex_lock(&ptp_priv->ptp_lock);
	ret = lan8841_ptp_update_target(ptp_priv, &ts);
	mutex_unlock(&ptp_priv->ptp_lock);

	return ret;
}

#define LAN8841_PTP_LTC_RATE_ADJ_HI		269
#define LAN8841_PTP_LTC_RATE_ADJ_HI_DIR		BIT(15)
#define LAN8841_PTP_LTC_RATE_ADJ_LO		270

static int lan8841_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct kszphy_ptp_priv *ptp_priv = container_of(ptp, struct kszphy_ptp_priv,
							ptp_clock_info);
	struct phy_device *phydev = ptp_priv->phydev;
	bool faster = true;
	u32 rate;

	if (!scaled_ppm)
		return 0;

	if (scaled_ppm < 0) {
		scaled_ppm = -scaled_ppm;
		faster = false;
	}

	rate = LAN8841_1PPM_FORMAT * (upper_16_bits(scaled_ppm));
	rate += (LAN8841_1PPM_FORMAT * (lower_16_bits(scaled_ppm))) >> 16;

	mutex_lock(&ptp_priv->ptp_lock);
	phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_RATE_ADJ_HI,
		      faster ? LAN8841_PTP_LTC_RATE_ADJ_HI_DIR | (upper_16_bits(rate) & 0x3fff)
			     : upper_16_bits(rate) & 0x3fff);
	phy_write_mmd(phydev, 2, LAN8841_PTP_LTC_RATE_ADJ_LO, lower_16_bits(rate));
	mutex_unlock(&ptp_priv->ptp_lock);

	return 0;
}

static int lan8841_ptp_verify(struct ptp_clock_info *ptp, unsigned int pin,
			      enum ptp_pin_function func, unsigned int chan)
{
	switch (func) {
	case PTP_PF_NONE:
	case PTP_PF_PEROUT:
	case PTP_PF_EXTTS:
		break;
	default:
		return -1;
	}

	return 0;
}

#define LAN8841_PTP_GPIO_NUM	10
#define LAN8841_GPIO_EN		128
#define LAN8841_GPIO_DIR	129
#define LAN8841_GPIO_BUF	130

static int lan8841_ptp_perout_off(struct kszphy_ptp_priv *ptp_priv, int pin)
{
	struct phy_device *phydev = ptp_priv->phydev;
	int ret;

	ret = phy_clear_bits_mmd(phydev, 2, LAN8841_GPIO_EN, BIT(pin));
	if (ret)
		return ret;

	ret = phy_clear_bits_mmd(phydev, 2, LAN8841_GPIO_DIR, BIT(pin));
	if (ret)
		return ret;

	return phy_clear_bits_mmd(phydev, 2, LAN8841_GPIO_BUF, BIT(pin));
}

static int lan8841_ptp_perout_on(struct kszphy_ptp_priv *ptp_priv, int pin)
{
	struct phy_device *phydev = ptp_priv->phydev;
	int ret;

	ret = phy_set_bits_mmd(phydev, 2, LAN8841_GPIO_EN, BIT(pin));
	if (ret)
		return ret;

	ret = phy_set_bits_mmd(phydev, 2, LAN8841_GPIO_DIR, BIT(pin));
	if (ret)
		return ret;

	return phy_set_bits_mmd(phydev, 2, LAN8841_GPIO_BUF, BIT(pin));
}

#define LAN8841_GPIO_DATA_SEL1				131
#define LAN8841_GPIO_DATA_SEL2				132
#define LAN8841_GPIO_DATA_SEL_GPIO_DATA_SEL_EVENT_MASK	GENMASK(2, 0)
#define LAN8841_GPIO_DATA_SEL_GPIO_DATA_SEL_EVENT_A	1
#define LAN8841_GPIO_DATA_SEL_GPIO_DATA_SEL_EVENT_B	2
#define LAN8841_PTP_GENERAL_CONFIG			257
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_POL_A	BIT(1)
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_POL_B	BIT(3)
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_A_MASK	GENMASK(7, 4)
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_B_MASK	GENMASK(11, 8)
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_A		4
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_B		7

static int lan8841_ptp_remove_event(struct kszphy_ptp_priv *ptp_priv, int pin,
				    u8 event)
{
	struct phy_device *phydev = ptp_priv->phydev;
	u16 tmp;
	int ret;

	/* Now remove pin from the event. GPIO_DATA_SEL1 contains the GPIO
	 * pins 0-4 while GPIO_DATA_SEL2 contains GPIO pins 5-9, therefore
	 * depending on the pin, it requires to read a different register
	 */
	if (pin < 5) {
		tmp = LAN8841_GPIO_DATA_SEL_GPIO_DATA_SEL_EVENT_MASK << (3 * pin);
		ret = phy_clear_bits_mmd(phydev, 2, LAN8841_GPIO_DATA_SEL1, tmp);
	} else {
		tmp = LAN8841_GPIO_DATA_SEL_GPIO_DATA_SEL_EVENT_MASK << (3 * (pin - 5));
		ret = phy_clear_bits_mmd(phydev, 2, LAN8841_GPIO_DATA_SEL2, tmp);
	}
	if (ret)
		return ret;

	/* Disable the event */
	if (event == LAN8841_EVENT_A)
		tmp = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_POL_A |
		      LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_A_MASK;
	else
		tmp = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_POL_B |
		      LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_B_MASK;
	return phy_clear_bits_mmd(phydev, 2, LAN8841_GPIO_EN, tmp);
}

static int lan8841_ptp_enable_event(struct kszphy_ptp_priv *ptp_priv, int pin,
				    u8 event, int pulse_width)
{
	struct phy_device *phydev = ptp_priv->phydev;
	u16 tmp;
	int ret;

	/* Enable the event */
	if (event == LAN8841_EVENT_A)
		ret = phy_modify_mmd(phydev, 2, LAN8841_PTP_GENERAL_CONFIG,
				     LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_POL_A |
				     LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_A_MASK,
				     LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_POL_A |
				     pulse_width << LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_A);
	else
		ret = phy_modify_mmd(phydev, 2, LAN8841_PTP_GENERAL_CONFIG,
				     LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_POL_B |
				     LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_B_MASK,
				     LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_POL_B |
				     pulse_width << LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_B);
	if (ret)
		return ret;

	/* Now connect the pin to the event. GPIO_DATA_SEL1 contains the GPIO
	 * pins 0-4 while GPIO_DATA_SEL2 contains GPIO pins 5-9, therefore
	 * depending on the pin, it requires to read a different register
	 */
	if (event == LAN8841_EVENT_A)
		tmp = LAN8841_GPIO_DATA_SEL_GPIO_DATA_SEL_EVENT_A;
	else
		tmp = LAN8841_GPIO_DATA_SEL_GPIO_DATA_SEL_EVENT_B;

	if (pin < 5)
		ret = phy_set_bits_mmd(phydev, 2, LAN8841_GPIO_DATA_SEL1,
				       tmp << (3 * pin));
	else
		ret = phy_set_bits_mmd(phydev, 2, LAN8841_GPIO_DATA_SEL2,
				       tmp << (3 * (pin - 5)));

	return ret;
}

#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_200MS	13
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_100MS	12
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_50MS	11
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_10MS	10
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_5MS	9
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_1MS	8
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_500US	7
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_100US	6
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_50US	5
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_10US	4
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_5US	3
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_1US	2
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_500NS	1
#define LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_100NS	0

static int lan8841_ptp_perout(struct ptp_clock_info *ptp,
			      struct ptp_clock_request *rq, int on)
{
	struct kszphy_ptp_priv *ptp_priv = container_of(ptp, struct kszphy_ptp_priv,
							ptp_clock_info);
	struct phy_device *phydev = ptp_priv->phydev;
	struct timespec64 ts_on, ts_period;
	s64 on_nsec, period_nsec;
	int pulse_width;
	int pin;
	int ret;

	if (rq->perout.flags & ~PTP_PEROUT_DUTY_CYCLE)
		return -EOPNOTSUPP;

	pin = ptp_find_pin(ptp_priv->ptp_clock, PTP_PF_PEROUT, rq->perout.index);
	if (pin == -1 || pin >= LAN8841_PTP_GPIO_NUM)
		return -EINVAL;

	if (!on) {
		ret = lan8841_ptp_perout_off(ptp_priv, pin);
		if (ret)
			return ret;

		return lan8841_ptp_remove_event(ptp_priv, LAN8841_EVENT_A, pin);
	}

	ts_on.tv_sec = rq->perout.on.sec;
	ts_on.tv_nsec = rq->perout.on.nsec;
	on_nsec = timespec64_to_ns(&ts_on);

	ts_period.tv_sec = rq->perout.period.sec;
	ts_period.tv_nsec = rq->perout.period.nsec;
	period_nsec = timespec64_to_ns(&ts_period);

	if (period_nsec < 200) {
		pr_warn_ratelimited("%s: perout period too small, minimum is 200 nsec\n",
				    phydev_name(phydev));
		return -EOPNOTSUPP;
	}

	if (on_nsec >= period_nsec) {
		pr_warn_ratelimited("%s: pulse width must be smaller than period\n",
				    phydev_name(phydev));
		return -EINVAL;
	}

	switch (on_nsec) {
	case 200000000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_200MS;
		break;
	case 100000000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_100MS;
		break;
	case 50000000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_50MS;
		break;
	case 10000000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_10MS;
		break;
	case 5000000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_5MS;
		break;
	case 1000000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_1MS;
		break;
	case 500000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_500US;
		break;
	case 100000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_100US;
		break;
	case 50000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_50US;
		break;
	case 10000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_10US;
		break;
	case 5000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_5US;
		break;
	case 1000:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_1US;
		break;
	case 500:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_500NS;
		break;
	case 100:
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_100NS;
		break;
	default:
		pr_warn_ratelimited("%s: Use default duty cycle of 100ns\n",
				    phydev_name(phydev));
		pulse_width = LAN8841_PTP_GENERAL_CONFIG_LTC_EVENT_100NS;
		break;
	}

	mutex_lock(&ptp_priv->ptp_lock);
	ret = lan8841_ptp_set_target(ptp_priv, LAN8841_EVENT_A, rq->perout.start.sec,
				     rq->perout.start.nsec);
	mutex_unlock(&ptp_priv->ptp_lock);
	if (ret)
		return ret;

	ret = lan8841_ptp_set_reload(ptp_priv, LAN8841_EVENT_A, rq->perout.period.sec,
				     rq->perout.period.nsec);
	if (ret)
		return ret;

	ret = lan8841_ptp_enable_event(ptp_priv, pin, LAN8841_EVENT_A,
				       pulse_width);
	if (ret)
		return ret;

	ret = lan8841_ptp_perout_on(ptp_priv, pin);
	if (ret)
		lan8841_ptp_remove_event(ptp_priv, pin, LAN8841_EVENT_A);

	return ret;
}

#define LAN8841_PTP_GPIO_CAP_EN			496
#define LAN8841_PTP_GPIO_CAP_EN_GPIO_RE_CAPTURE_ENABLE(gpio)	(BIT(gpio))
#define LAN8841_PTP_GPIO_CAP_EN_GPIO_FE_CAPTURE_ENABLE(gpio)	(BIT(gpio) << 8)
#define LAN8841_PTP_INT_EN_PTP_GPIO_CAP_EN	BIT(2)

static int lan8841_ptp_extts_on(struct kszphy_ptp_priv *ptp_priv, int pin,
				u32 flags)
{
	struct phy_device *phydev = ptp_priv->phydev;
	u16 tmp = 0;
	int ret;

	/* Set GPIO to be intput */
	ret = phy_set_bits_mmd(phydev, 2, LAN8841_GPIO_EN, BIT(pin));
	if (ret)
		return ret;

	ret = phy_clear_bits_mmd(phydev, 2, LAN8841_GPIO_BUF, BIT(pin));
	if (ret)
		return ret;

	/* Enable capture on the edges of the pin */
	if (flags & PTP_RISING_EDGE)
		tmp |= LAN8841_PTP_GPIO_CAP_EN_GPIO_RE_CAPTURE_ENABLE(pin);
	if (flags & PTP_FALLING_EDGE)
		tmp |= LAN8841_PTP_GPIO_CAP_EN_GPIO_FE_CAPTURE_ENABLE(pin);
	ret = phy_write_mmd(phydev, 2, LAN8841_PTP_GPIO_CAP_EN, tmp);
	if (ret)
		return ret;

	/* Enable interrupt */
	return phy_modify_mmd(phydev, 2, LAN8841_PTP_INT_EN,
			      LAN8841_PTP_INT_EN_PTP_GPIO_CAP_EN,
			      LAN8841_PTP_INT_EN_PTP_GPIO_CAP_EN);
}

static int lan8841_ptp_extts_off(struct kszphy_ptp_priv *ptp_priv, int pin)
{
	struct phy_device *phydev = ptp_priv->phydev;
	int ret;

	/* Set GPIO to be output */
	ret = phy_clear_bits_mmd(phydev, 2, LAN8841_GPIO_EN, BIT(pin));
	if (ret)
		return ret;

	ret = phy_clear_bits_mmd(phydev, 2, LAN8841_GPIO_BUF, BIT(pin));
	if (ret)
		return ret;

	/* Disable capture on both of the edges */
	ret = phy_modify_mmd(phydev, 2, LAN8841_PTP_GPIO_CAP_EN,
			     LAN8841_PTP_GPIO_CAP_EN_GPIO_RE_CAPTURE_ENABLE(pin) |
			     LAN8841_PTP_GPIO_CAP_EN_GPIO_FE_CAPTURE_ENABLE(pin),
			     0);
	if (ret)
		return ret;

	/* Disable interrupt */
	return phy_modify_mmd(phydev, 2, LAN8841_PTP_INT_EN,
			      LAN8841_PTP_INT_EN_PTP_GPIO_CAP_EN,
			      0);
}

static int lan8841_ptp_extts(struct ptp_clock_info *ptp,
			     struct ptp_clock_request *rq, int on)
{
	struct kszphy_ptp_priv *ptp_priv = container_of(ptp, struct kszphy_ptp_priv,
							ptp_clock_info);
	int pin;
	int ret;

	/* Reject requests with unsupported flags */
	if (rq->extts.flags & ~(PTP_ENABLE_FEATURE |
				PTP_EXTTS_EDGES |
				PTP_STRICT_FLAGS))
		return -EOPNOTSUPP;

	pin = ptp_find_pin(ptp_priv->ptp_clock, PTP_PF_EXTTS, rq->extts.index);
	if (pin == -1 || pin >= LAN8841_PTP_GPIO_NUM)
		return -EINVAL;

	mutex_lock(&ptp_priv->ptp_lock);
	if (on)
		ret = lan8841_ptp_extts_on(ptp_priv, pin, rq->extts.flags);
	else
		ret = lan8841_ptp_extts_off(ptp_priv, pin);
	mutex_unlock(&ptp_priv->ptp_lock);

	return ret;
}

static int lan8841_ptp_enable(struct ptp_clock_info *ptp,
			      struct ptp_clock_request *rq, int on)
{
	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
		return lan8841_ptp_extts(ptp, rq, on);
	case PTP_CLK_REQ_PEROUT:
		return lan8841_ptp_perout(ptp, rq, on);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static long lan8841_ptp_do_aux_work(struct ptp_clock_info *ptp)
{
	struct kszphy_ptp_priv *ptp_priv = container_of(ptp, struct kszphy_ptp_priv,
							ptp_clock_info);
	struct timespec64 ts;
	unsigned long flags;

	lan8841_ptp_getseconds(&ptp_priv->ptp_clock_info, &ts);

	spin_lock_irqsave(&ptp_priv->seconds_lock, flags);
	ptp_priv->seconds = ts.tv_sec;
	spin_unlock_irqrestore(&ptp_priv->seconds_lock, flags);

	return nsecs_to_jiffies(LAN8841_GET_SEC_LTC_DELAY);
}

static struct ptp_clock_info lan8841_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "lan8841 ptp",
	.max_adj	= 31249999,
	.gettime64	= lan8841_ptp_gettime64,
	.settime64	= lan8841_ptp_settime64,
	.adjtime	= lan8841_ptp_adjtime,
	.adjfine	= lan8841_ptp_adjfine,
	.verify         = lan8841_ptp_verify,
	.enable         = lan8841_ptp_enable,
	.do_aux_work	= lan8841_ptp_do_aux_work,
	.n_per_out      = LAN8841_PTP_GPIO_NUM,
	.n_ext_ts       = LAN8841_PTP_GPIO_NUM,
	.n_pins         = LAN8841_PTP_GPIO_NUM,
};

#define LAN8841_OPERATION_MODE_STRAP_LOW_REGISTER 3
#define LAN8841_OPERATION_MODE_STRAP_LOW_REGISTER_STRAP_RGMII_EN BIT(0)

static int lan8841_probe(struct phy_device *phydev)
{
	struct kszphy_ptp_priv *ptp_priv;
	struct kszphy_priv *priv;
	int err;

	err = kszphy_probe(phydev);
	if (err)
		return err;

	if (phy_read_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
			 LAN8841_OPERATION_MODE_STRAP_LOW_REGISTER) &
	    LAN8841_OPERATION_MODE_STRAP_LOW_REGISTER_STRAP_RGMII_EN)
		phydev->interface = PHY_INTERFACE_MODE_RGMII_RXID;

	/* Register the clock */
	if (!IS_ENABLED(CONFIG_NETWORK_PHY_TIMESTAMPING))
		return 0;

	priv = phydev->priv;
	ptp_priv = &priv->ptp_priv;

	ptp_priv->pin_config = devm_kcalloc(&phydev->mdio.dev,
					    LAN8841_PTP_GPIO_NUM,
					    sizeof(*ptp_priv->pin_config),
					    GFP_KERNEL);
	if (!ptp_priv->pin_config)
		return -ENOMEM;

	for (int i = 0; i < LAN8841_PTP_GPIO_NUM; ++i) {
		struct ptp_pin_desc *p = &ptp_priv->pin_config[i];

		snprintf(p->name, sizeof(p->name), "pin%d", i);
		p->index = i;
		p->func = PTP_PF_NONE;
	}

	ptp_priv->ptp_clock_info = lan8841_ptp_clock_info;
	ptp_priv->ptp_clock_info.pin_config = ptp_priv->pin_config;
	ptp_priv->ptp_clock = ptp_clock_register(&ptp_priv->ptp_clock_info,
						 &phydev->mdio.dev);
	if (IS_ERR(ptp_priv->ptp_clock)) {
		phydev_err(phydev, "ptp_clock_register failed: %lu\n",
			   PTR_ERR(ptp_priv->ptp_clock));
		return -EINVAL;
	}

	if (!ptp_priv->ptp_clock)
		return 0;

	/* Initialize the SW */
	skb_queue_head_init(&ptp_priv->tx_queue);
	ptp_priv->phydev = phydev;
	mutex_init(&ptp_priv->ptp_lock);
	spin_lock_init(&ptp_priv->seconds_lock);

	ptp_priv->mii_ts.rxtstamp = lan8841_rxtstamp;
	ptp_priv->mii_ts.txtstamp = lan8814_txtstamp;
	ptp_priv->mii_ts.hwtstamp = lan8841_hwtstamp;
	ptp_priv->mii_ts.ts_info = lan8841_ts_info;

	phydev->mii_ts = &ptp_priv->mii_ts;

	/* Timestamp selected by default to keep legacy API */
	phydev->default_timestamp = true;

	return 0;
}

static int lan8804_resume(struct phy_device *phydev)
{
	return kszphy_resume(phydev);
}

static int lan8804_suspend(struct phy_device *phydev)
{
	return kszphy_generic_suspend(phydev);
}

static int lan8841_resume(struct phy_device *phydev)
{
	return kszphy_generic_resume(phydev);
}

static int lan8841_suspend(struct phy_device *phydev)
{
	struct kszphy_priv *priv = phydev->priv;
	struct kszphy_ptp_priv *ptp_priv = &priv->ptp_priv;

	if (ptp_priv->ptp_clock)
		ptp_cancel_worker_sync(ptp_priv->ptp_clock);

	return kszphy_generic_suspend(phydev);
}

static struct phy_driver ksphy_driver[] = {
{
	.phy_id		= PHY_ID_KS8737,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Micrel KS8737",
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ks8737_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.config_intr	= kszphy_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.suspend	= kszphy_suspend,
	.resume		= kszphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ8021,
	.phy_id_mask	= 0x00ffffff,
	.name		= "Micrel KSZ8021 or KSZ8031",
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ksz8021_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.config_intr	= kszphy_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= kszphy_suspend,
	.resume		= kszphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ8031,
	.phy_id_mask	= 0x00ffffff,
	.name		= "Micrel KSZ8031",
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ksz8021_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.config_intr	= kszphy_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= kszphy_suspend,
	.resume		= kszphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ8041,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Micrel KSZ8041",
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ksz8041_type,
	.probe		= kszphy_probe,
	.config_init	= ksz8041_config_init,
	.config_aneg	= ksz8041_config_aneg,
	.config_intr	= kszphy_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= ksz8041_suspend,
	.resume		= ksz8041_resume,
}, {
	.phy_id		= PHY_ID_KSZ8041RNLI,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Micrel KSZ8041RNLI",
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ksz8041_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.config_intr	= kszphy_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= kszphy_suspend,
	.resume		= kszphy_resume,
}, {
	.name		= "Micrel KSZ8051",
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ksz8051_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.config_intr	= kszphy_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.match_phy_device = ksz8051_match_phy_device,
	.suspend	= kszphy_suspend,
	.resume		= kszphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ8001,
	.name		= "Micrel KSZ8001 or KS8721",
	.phy_id_mask	= 0x00fffffc,
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ksz8041_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.config_intr	= kszphy_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= kszphy_suspend,
	.resume		= kszphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ8081,
	.name		= "Micrel KSZ8081 or KSZ8091",
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.flags		= PHY_POLL_CABLE_TEST,
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ksz8081_type,
	.probe		= kszphy_probe,
	.config_init	= ksz8081_config_init,
	.soft_reset	= genphy_soft_reset,
	.config_aneg	= ksz8081_config_aneg,
	.read_status	= ksz8081_read_status,
	.config_intr	= kszphy_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= kszphy_suspend,
	.resume		= kszphy_resume,
	.cable_test_start	= ksz886x_cable_test_start,
	.cable_test_get_status	= ksz886x_cable_test_get_status,
}, {
	.phy_id		= PHY_ID_KSZ8061,
	.name		= "Micrel KSZ8061",
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	/* PHY_BASIC_FEATURES */
	.probe		= kszphy_probe,
	.config_init	= ksz8061_config_init,
	.soft_reset	= genphy_soft_reset,
	.config_intr	= kszphy_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.suspend	= ksz8061_suspend,
	.resume		= ksz8061_resume,
}, {
	.phy_id		= PHY_ID_KSZ9021,
	.phy_id_mask	= 0x000ffffe,
	.name		= "Micrel KSZ9021 Gigabit PHY",
	/* PHY_GBIT_FEATURES */
	.driver_data	= &ksz9021_type,
	.probe		= kszphy_probe,
	.get_features	= ksz9031_get_features,
	.config_init	= ksz9021_config_init,
	.config_intr	= kszphy_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= kszphy_suspend,
	.resume		= kszphy_resume,
	.read_mmd	= genphy_read_mmd_unsupported,
	.write_mmd	= genphy_write_mmd_unsupported,
}, {
	.phy_id		= PHY_ID_KSZ9031,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Micrel KSZ9031 Gigabit PHY",
	.flags		= PHY_POLL_CABLE_TEST,
	.driver_data	= &ksz9021_type,
	.probe		= kszphy_probe,
	.get_features	= ksz9031_get_features,
	.config_init	= ksz9031_config_init,
	.soft_reset	= genphy_soft_reset,
	.read_status	= ksz9031_read_status,
	.config_intr	= kszphy_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= kszphy_suspend,
	.resume		= kszphy_resume,
	.cable_test_start	= ksz9x31_cable_test_start,
	.cable_test_get_status	= ksz9x31_cable_test_get_status,
	.set_loopback	= ksz9031_set_loopback,
}, {
	.phy_id		= PHY_ID_LAN8814,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Microchip INDY Gigabit Quad PHY",
	.flags          = PHY_POLL_CABLE_TEST,
	.config_init	= lan8814_config_init,
	.driver_data	= &lan8814_type,
	.probe		= lan8814_probe,
	.soft_reset	= genphy_soft_reset,
	.read_status	= ksz9031_read_status,
	.get_sset_count	= kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= genphy_suspend,
	.resume		= kszphy_resume,
	.config_intr	= lan8814_config_intr,
	.handle_interrupt = lan8814_handle_interrupt,
	.cable_test_start	= lan8814_cable_test_start,
	.cable_test_get_status	= ksz886x_cable_test_get_status,
}, {
	.phy_id		= PHY_ID_LAN8804,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Microchip LAN966X Gigabit PHY",
	.config_init	= lan8804_config_init,
	.driver_data	= &ksz9021_type,
	.probe		= kszphy_probe,
	.soft_reset	= genphy_soft_reset,
	.read_status	= ksz9031_read_status,
	.get_sset_count	= kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= lan8804_suspend,
	.resume		= lan8804_resume,
	.config_intr	= lan8804_config_intr,
	.handle_interrupt = lan8804_handle_interrupt,
}, {
	.phy_id		= PHY_ID_LAN8841,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Microchip LAN8841 Gigabit PHY",
	.flags		= PHY_POLL_CABLE_TEST,
	.driver_data	= &lan8841_type,
	.config_init	= lan8841_config_init,
	.probe		= lan8841_probe,
	.soft_reset	= genphy_soft_reset,
	.config_intr	= lan8841_config_intr,
	.handle_interrupt = lan8841_handle_interrupt,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= lan8841_suspend,
	.resume		= lan8841_resume,
	.cable_test_start	= lan8814_cable_test_start,
	.cable_test_get_status	= ksz886x_cable_test_get_status,
}, {
	.phy_id		= PHY_ID_KSZ9131,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Microchip KSZ9131 Gigabit PHY",
	/* PHY_GBIT_FEATURES */
	.flags		= PHY_POLL_CABLE_TEST,
	.driver_data	= &ksz9131_type,
	.probe		= kszphy_probe,
	.soft_reset	= genphy_soft_reset,
	.config_init	= ksz9131_config_init,
	.config_intr	= kszphy_config_intr,
	.config_aneg	= ksz9131_config_aneg,
	.read_status	= ksz9131_read_status,
	.handle_interrupt = kszphy_handle_interrupt,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= kszphy_suspend,
	.resume		= kszphy_resume,
	.cable_test_start	= ksz9x31_cable_test_start,
	.cable_test_get_status	= ksz9x31_cable_test_get_status,
	.get_features	= ksz9477_get_features,
}, {
	.phy_id		= PHY_ID_KSZ8873MLL,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Micrel KSZ8873MLL Switch",
	/* PHY_BASIC_FEATURES */
	.config_init	= kszphy_config_init,
	.config_aneg	= ksz8873mll_config_aneg,
	.read_status	= ksz8873mll_read_status,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ886X,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Micrel KSZ8851 Ethernet MAC or KSZ886X Switch",
	.driver_data	= &ksz886x_type,
	/* PHY_BASIC_FEATURES */
	.flags		= PHY_POLL_CABLE_TEST,
	.config_init	= kszphy_config_init,
	.config_aneg	= ksz886x_config_aneg,
	.read_status	= ksz886x_read_status,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.cable_test_start	= ksz886x_cable_test_start,
	.cable_test_get_status	= ksz886x_cable_test_get_status,
}, {
	.name		= "Micrel KSZ87XX Switch",
	/* PHY_BASIC_FEATURES */
	.config_init	= kszphy_config_init,
	.match_phy_device = ksz8795_match_phy_device,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ9477,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Microchip KSZ9477",
	/* PHY_GBIT_FEATURES */
	.config_init	= ksz9477_config_init,
	.config_intr	= kszphy_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.suspend	= genphy_suspend,
	.resume		= ksz9477_resume,
	.get_features	= ksz9477_get_features,
} };

module_phy_driver(ksphy_driver);

MODULE_DESCRIPTION("Micrel PHY driver");
MODULE_AUTHOR("David J. Choi");
MODULE_LICENSE("GPL");

static const struct mdio_device_id __maybe_unused micrel_tbl[] = {
	{ PHY_ID_KSZ9021, 0x000ffffe },
	{ PHY_ID_KSZ9031, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ9131, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ8001, 0x00fffffc },
	{ PHY_ID_KS8737, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ8021, 0x00ffffff },
	{ PHY_ID_KSZ8031, 0x00ffffff },
	{ PHY_ID_KSZ8041, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ8051, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ8061, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ8081, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ8873MLL, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ886X, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ9477, MICREL_PHY_ID_MASK },
	{ PHY_ID_LAN8814, MICREL_PHY_ID_MASK },
	{ PHY_ID_LAN8804, MICREL_PHY_ID_MASK },
	{ PHY_ID_LAN8841, MICREL_PHY_ID_MASK },
	{ }
};

MODULE_DEVICE_TABLE(mdio, micrel_tbl);
