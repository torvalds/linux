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
 *		Giga phys: ksz9021, ksz9031, ksz9131
 *		100/10 Phys : ksz8001, ksz8721, ksz8737, ksz8041
 *			   ksz8021, ksz8031, ksz8051,
 *			   ksz8081, ksz8091,
 *			   ksz8061,
 *		Switch : ksz8873, ksz886x
 *			 ksz9477
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
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/ptp_clock.h>
#include <linux/ptp_classify.h>
#include <linux/net_tstamp.h>
#include <linux/gpio/consumer.h>

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

/* Represents 1ppm adjustment in 2^32 format with
 * each nsec contains 4 clock cycles.
 * The value is calculated as following: (1/1000000)/((2^-32)/4)
 */
#define LAN8814_1PPM_FORMAT			17179

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

#define PTP_CLOCK_SET_SEC_MID			0x0206
#define PTP_CLOCK_SET_SEC_LO			0x0207
#define PTP_CLOCK_SET_NS_HI			0x0208
#define PTP_CLOCK_SET_NS_LO			0x0209

#define PTP_CLOCK_READ_SEC_MID			0x022A
#define PTP_CLOCK_READ_SEC_LO			0x022B
#define PTP_CLOCK_READ_NS_HI			0x022C
#define PTP_CLOCK_READ_NS_LO			0x022D

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

/*Register 2.10. 15:14 PME Output Select*/
#define MII_KSZPHY_OMSO_PME_N2                  BIT(10)
/*Register 2.10. BITS 6, 1 and 0 to detect the type of WOL */
#define MII_KSZPHY_WOL_MAGIC_PKT                BIT(6)
#define MII_KSZPHY_WOL_LINK_DOWN                BIT(1)
#define MII_KSZPHY_WOL_LINK_UP                  BIT(0)
/* Register 2.10.15:14 PME Output Select */
#define MII_KSZPHY_WOL_CTRL_PME_N2              BIT(15)
#define MII_KSZPHY_WOL_CTRL_INT_N               BIT(14)

/* MMD Address 2h, Register 2h Operation Mode Strap Override*/
#define MII_KSZPHY_OMSO_REG                     0x2
/* MMD Address 2h, Register 10h Wake-On-LAN Control   */
#define MII_KSZPHY_WOL_CTRL_REG                 0x10

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
	bool has_broadcast_disable;
	bool has_nand_tree_disable;
	bool has_rmii_ref_clk_sel;
};

/* Shared structure between the PHYs of the same package. */
struct lan8814_shared_priv {
	struct phy_device *phydev;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_info;

	/* Reference counter to how many ports in the package are enabling the
	 * timestamping
	 */
	u8 ref;

	/* Lock for ptp_clock and ref */
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
};

struct kszphy_priv {
	struct kszphy_ptp_priv ptp_priv;
	const struct kszphy_type *type;
	int led_mode;
	u16 vct_ctrl1000;
	bool rmii_ref_clk_sel;
	bool rmii_ref_clk_sel_val;
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

		temp = KSZPHY_INTCS_ALL;
		err = phy_write(phydev, MII_KSZPHY_INTCS, temp);
	} else {
		temp = 0;
		err = phy_write(phydev, MII_KSZPHY_INTCS, temp);
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

	if ((phydev->phy_id & MICREL_PHY_ID_MASK) != PHY_ID_KSZ8051)
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

static int ksz9031_ack_interrupt(struct phy_device *phydev)
{
	/* bit[7..0] int status, which is a read and clear register. */
	int rc;
	u32 reg_value;

	rc = phy_read(phydev, MII_KSZPHY_INTCS);

	reg_value = phy_read_mmd(phydev, 0x2, MII_KSZPHY_OMSO_REG);
	if (reg_value & MII_KSZPHY_OMSO_PME_N2) {
		/* PME output is cleared by disabling the PME trigger src */
		reg_value = phy_read_mmd(phydev, 0x2, MII_KSZPHY_WOL_CTRL_REG);
		reg_value &= ~MII_KSZPHY_WOL_MAGIC_PKT;
		reg_value &= ~MII_KSZPHY_WOL_LINK_UP;
		reg_value &= ~MII_KSZPHY_WOL_LINK_DOWN;
		phy_write_mmd(phydev, 0x2, MII_KSZPHY_WOL_CTRL_REG, reg_value);
		reg_value = phy_read_mmd(phydev, 0x2, MII_KSZPHY_WOL_CTRL_REG);
		reg_value |= MII_KSZPHY_WOL_MAGIC_PKT;
		reg_value |= MII_KSZPHY_WOL_LINK_UP;
		reg_value |= MII_KSZPHY_WOL_LINK_DOWN;
		phy_write_mmd(phydev, 0x2, MII_KSZPHY_WOL_CTRL_REG, reg_value);
	}

	return (rc < 0) ? rc : 0;
}

static int ksz9031_config_intr(struct phy_device *phydev)
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
		err = ksz9031_ack_interrupt(phydev);
		if (err)
			return err;

		temp = KSZPHY_INTCS_ALL;
		err = phy_write(phydev, MII_KSZPHY_INTCS, temp);
	} else {
		temp = 0;
		err = phy_write(phydev, MII_KSZPHY_INTCS, temp);
		if (err)
			return err;

		err = ksz9031_ack_interrupt(phydev);
	}

	return err;
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
#define KSZ9131RN_DLL_CTRL_BYPASS	BIT_MASK(12)
#define KSZ9131RN_DLL_ENABLE_DELAY	0
#define KSZ9131RN_DLL_DISABLE_DELAY	BIT(12)

static int ksz9131_config_rgmii_delay(struct phy_device *phydev)
{
	u16 rxcdll_val, txcdll_val;
	int ret;

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		rxcdll_val = KSZ9131RN_DLL_DISABLE_DELAY;
		txcdll_val = KSZ9131RN_DLL_DISABLE_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		rxcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		txcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		rxcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		txcdll_val = KSZ9131RN_DLL_DISABLE_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		rxcdll_val = KSZ9131RN_DLL_DISABLE_DELAY;
		txcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		break;
	default:
		return 0;
	}

	ret = phy_modify_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
			     KSZ9131RN_RXC_DLL_CTRL, KSZ9131RN_DLL_CTRL_BYPASS,
			     rxcdll_val);
	if (ret < 0)
		return ret;

	return phy_modify_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
			      KSZ9131RN_TXC_DLL_CTRL, KSZ9131RN_DLL_CTRL_BYPASS,
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
		if (phydev->drv->config_intr &&
		    (phydev->irq == PHY_MAC_INTERRUPT || phy_interrupt_is_valid(phydev)))
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
	if ((phydev->phy_id & MICREL_PHY_ID_MASK) == PHY_ID_KSZ9131)
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

static int kszphy_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(kszphy_hw_stats);
}

static void kszphy_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kszphy_hw_stats); i++) {
		strscpy(data + i * ETH_GSTRING_LEN,
			kszphy_hw_stats[i].string, ETH_GSTRING_LEN);
	}
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

static int kszphy_suspend(struct phy_device *phydev)
{
	/* Disable PHY Interrupts */
	if (phy_interrupt_is_valid(phydev)) {
		phydev->interrupts = PHY_INTERRUPT_DISABLED;
		if (phydev->drv->config_intr)
			phydev->drv->config_intr(phydev);
	}

	return genphy_suspend(phydev);
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

	genphy_resume(phydev);

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

	clk = devm_clk_get(&phydev->mdio.dev, "rmii-ref");
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
	}

	if (ksz8041_fiber_mode(phydev))
		phydev->port = PORT_FIBRE;

	/* Support legacy board-file configuration */
	if (phydev->dev_flags & MICREL_PHY_50MHZ_CLK) {
		priv->rmii_ref_clk_sel = true;
		priv->rmii_ref_clk_sel_val = true;
	}

	return 0;
}

static void ksz9031_set_wol_settings(struct phy_device *phydev)
{
	u32 reg_value;

	/* Enable both PHY and PME_N2 interrupts */
	reg_value = phy_read_mmd(phydev, 0x2, MII_KSZPHY_WOL_CTRL_REG);
	reg_value |= MII_KSZPHY_WOL_CTRL_PME_N2;
	reg_value &= ~MII_KSZPHY_WOL_CTRL_INT_N;
	reg_value |= MII_KSZPHY_WOL_MAGIC_PKT;
	reg_value |= MII_KSZPHY_WOL_LINK_UP;
	reg_value |= MII_KSZPHY_WOL_LINK_DOWN;
	phy_write_mmd(phydev, 0x2, MII_KSZPHY_WOL_CTRL_REG, reg_value);
}

static int ksz9031_set_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	struct net_device *ndev = phydev->attached_dev;
	const u8 *mac;
	int ret = 0;
	u32 reg_value;

	if (!ndev)
		return -ENODEV;

	if (wol->wolopts & WAKE_MAGIC) {
		mac = (const u8 *)ndev->dev_addr;

		if (!is_valid_ether_addr(mac))
			return -EINVAL;

		phy_write_mmd(phydev, 0x2, 0x11, mac[5] | (mac[4] << 8));
		phy_write_mmd(phydev, 0x2, 0x12, mac[3] | (mac[2] << 8));
		phy_write_mmd(phydev, 0x2, 0x13, mac[1] | (mac[0] << 8));

		/* Enable WOL interrupt for magic pkt, link up and down */
		ksz9031_set_wol_settings(phydev);

		/* Enable PME_N2 output */
		reg_value = phy_read_mmd(phydev, 0x2, MII_KSZPHY_OMSO_REG);
		reg_value |= MII_KSZPHY_OMSO_PME_N2;
		phy_write_mmd(phydev, 0x2, MII_KSZPHY_OMSO_REG, reg_value);
	}

	return ret;
}

static void ksz9031_get_wol(struct phy_device *phydev,
			    struct ethtool_wolinfo *wol)
{
	u32 reg_value;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	reg_value = phy_read_mmd(phydev, 0x2, MII_KSZPHY_OMSO_REG);
	if (reg_value & MII_KSZPHY_OMSO_PME_N2)
		wol->wolopts |= WAKE_MAGIC;
}

static int ksz9031_suspend(struct phy_device *phydev)
{
	int value;
	int wol_enabled;
	u32 reg_value;

	reg_value = phy_read_mmd(phydev, 0x2, MII_KSZPHY_OMSO_REG);
	wol_enabled = reg_value & MII_KSZPHY_OMSO_PME_N2;

	value = phy_read(phydev, MII_BMCR);
	if (wol_enabled)
		value |= BMCR_ISOLATE;
	else
		value |= BMCR_PDOWN;

	phy_write(phydev, MII_BMCR, value);
	return 0;
}

static int ksz9031_resume(struct phy_device *phydev)
{
	int value;

	value = phy_read(phydev, MII_BMCR);
	value &= ~(BMCR_PDOWN | BMCR_ISOLATE);
	phy_write(phydev, MII_BMCR, value);
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

	if ((phydev->phy_id & MICREL_PHY_ID_MASK) == PHY_ID_LAN8814)
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
	int pair, ret;

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

static int lan8814_ts_info(struct mii_timestamper *mii_ts, struct ethtool_ts_info *info)
{
	struct kszphy_ptp_priv *ptp_priv = container_of(mii_ts, struct kszphy_ptp_priv, mii_ts);
	struct phy_device *phydev = ptp_priv->phydev;
	struct lan8814_shared_priv *shared = phydev->shared->priv;

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

static int lan8814_hwtstamp(struct mii_timestamper *mii_ts, struct ifreq *ifr)
{
	struct kszphy_ptp_priv *ptp_priv =
			  container_of(mii_ts, struct kszphy_ptp_priv, mii_ts);
	struct phy_device *phydev = ptp_priv->phydev;
	struct lan8814_shared_priv *shared = phydev->shared->priv;
	struct lan8814_ptp_rx_ts *rx_ts, *tmp;
	struct hwtstamp_config config;
	int txcfg = 0, rxcfg = 0;
	int pkt_ts_enable;
	int tx_mod;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	ptp_priv->hwts_tx_type = config.tx_type;
	ptp_priv->rx_filter = config.rx_filter;

	switch (config.rx_filter) {
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

	if (config.rx_filter != HWTSTAMP_FILTER_NONE)
		lan8814_config_ts_intr(ptp_priv->phydev, true);
	else
		lan8814_config_ts_intr(ptp_priv->phydev, false);

	mutex_lock(&shared->shared_lock);
	if (config.rx_filter != HWTSTAMP_FILTER_NONE)
		shared->ref++;
	else
		shared->ref--;

	if (shared->ref)
		lanphy_write_page_reg(ptp_priv->phydev, 4, PTP_CMD_CTL,
				      PTP_CMD_CTL_PTP_ENABLE_);
	else
		lanphy_write_page_reg(ptp_priv->phydev, 4, PTP_CMD_CTL,
				      PTP_CMD_CTL_PTP_DISABLE_);
	mutex_unlock(&shared->shared_lock);

	/* In case of multiple starts and stops, these needs to be cleared */
	list_for_each_entry_safe(rx_ts, tmp, &ptp_priv->rx_ts_list, list) {
		list_del(&rx_ts->list);
		kfree(rx_ts);
	}
	skb_queue_purge(&ptp_priv->rx_queue);
	skb_queue_purge(&ptp_priv->tx_queue);

	lan8814_flush_fifo(ptp_priv->phydev, false);
	lan8814_flush_fifo(ptp_priv->phydev, true);

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ? -EFAULT : 0;
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

static bool lan8814_match_rx_ts(struct kszphy_ptp_priv *ptp_priv,
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
	if (!lan8814_match_rx_ts(ptp_priv, skb))
		skb_queue_tail(&ptp_priv->rx_queue, skb);

	return true;
}

static void lan8814_ptp_clock_set(struct phy_device *phydev,
				  u32 seconds, u32 nano_seconds)
{
	u32 sec_low, sec_high, nsec_low, nsec_high;

	sec_low = seconds & 0xffff;
	sec_high = (seconds >> 16) & 0xffff;
	nsec_low = nano_seconds & 0xffff;
	nsec_high = (nano_seconds >> 16) & 0x3fff;

	lanphy_write_page_reg(phydev, 4, PTP_CLOCK_SET_SEC_LO, sec_low);
	lanphy_write_page_reg(phydev, 4, PTP_CLOCK_SET_SEC_MID, sec_high);
	lanphy_write_page_reg(phydev, 4, PTP_CLOCK_SET_NS_LO, nsec_low);
	lanphy_write_page_reg(phydev, 4, PTP_CLOCK_SET_NS_HI, nsec_high);

	lanphy_write_page_reg(phydev, 4, PTP_CMD_CTL, PTP_CMD_CTL_PTP_CLOCK_LOAD_);
}

static void lan8814_ptp_clock_get(struct phy_device *phydev,
				  u32 *seconds, u32 *nano_seconds)
{
	lanphy_write_page_reg(phydev, 4, PTP_CMD_CTL, PTP_CMD_CTL_PTP_CLOCK_READ_);

	*seconds = lanphy_read_page_reg(phydev, 4, PTP_CLOCK_READ_SEC_MID);
	*seconds = (*seconds << 16) |
		   lanphy_read_page_reg(phydev, 4, PTP_CLOCK_READ_SEC_LO);

	*nano_seconds = lanphy_read_page_reg(phydev, 4, PTP_CLOCK_READ_NS_HI);
	*nano_seconds = ((*nano_seconds & 0x3fff) << 16) |
			lanphy_read_page_reg(phydev, 4, PTP_CLOCK_READ_NS_LO);
}

static int lan8814_ptpci_gettime64(struct ptp_clock_info *ptpci,
				   struct timespec64 *ts)
{
	struct lan8814_shared_priv *shared = container_of(ptpci, struct lan8814_shared_priv,
							  ptp_clock_info);
	struct phy_device *phydev = shared->phydev;
	u32 nano_seconds;
	u32 seconds;

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

static void lan8814_ptp_clock_step(struct phy_device *phydev,
				   s64 time_step_ns)
{
	u32 nano_seconds_step;
	u64 abs_time_step_ns;
	u32 unsigned_seconds;
	u32 nano_seconds;
	u32 remainder;
	s32 seconds;

	if (time_step_ns >  15000000000LL) {
		/* convert to clock set */
		lan8814_ptp_clock_get(phydev, &unsigned_seconds, &nano_seconds);
		unsigned_seconds += div_u64_rem(time_step_ns, 1000000000LL,
						&remainder);
		nano_seconds += remainder;
		if (nano_seconds >= 1000000000) {
			unsigned_seconds++;
			nano_seconds -= 1000000000;
		}
		lan8814_ptp_clock_set(phydev, unsigned_seconds, nano_seconds);
		return;
	} else if (time_step_ns < -15000000000LL) {
		/* convert to clock set */
		time_step_ns = -time_step_ns;

		lan8814_ptp_clock_get(phydev, &unsigned_seconds, &nano_seconds);
		unsigned_seconds -= div_u64_rem(time_step_ns, 1000000000LL,
						&remainder);
		nano_seconds_step = remainder;
		if (nano_seconds < nano_seconds_step) {
			unsigned_seconds--;
			nano_seconds += 1000000000;
		}
		nano_seconds -= nano_seconds_step;
		lan8814_ptp_clock_set(phydev, unsigned_seconds,
				      nano_seconds);
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

static void lan8814_dequeue_tx_skb(struct kszphy_ptp_priv *ptp_priv)
{
	struct phy_device *phydev = ptp_priv->phydev;
	struct skb_shared_hwtstamps shhwtstamps;
	struct sk_buff *skb, *skb_tmp;
	unsigned long flags;
	u32 seconds, nsec;
	bool ret = false;
	u16 skb_sig;
	u16 seq_id;

	lan8814_ptp_tx_ts_get(phydev, &seconds, &nsec, &seq_id);

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

static void lan8814_get_rx_ts(struct kszphy_ptp_priv *ptp_priv)
{
	struct phy_device *phydev = ptp_priv->phydev;
	struct lan8814_ptp_rx_ts *rx_ts;
	unsigned long flags;
	u32 reg;

	do {
		rx_ts = kzalloc(sizeof(*rx_ts), GFP_KERNEL);
		if (!rx_ts)
			return;

		lan8814_ptp_rx_ts_get(phydev, &rx_ts->seconds, &rx_ts->nsec,
				      &rx_ts->seq_id);

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

		/* If other timestamps are available in the FIFO,
		 * process them.
		 */
		reg = lanphy_read_page_reg(phydev, 5, PTP_CAP_INFO);
	} while (PTP_CAP_INFO_RX_TS_CNT_GET_(reg) > 0);
}

static void lan8814_handle_ptp_interrupt(struct phy_device *phydev)
{
	struct kszphy_priv *priv = phydev->priv;
	struct kszphy_ptp_priv *ptp_priv = &priv->ptp_priv;
	u16 status;

	status = lanphy_read_page_reg(phydev, 5, PTP_TSU_INT_STS);
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
	int irq_status, tsu_irq_status;
	int ret = IRQ_NONE;

	irq_status = phy_read(phydev, LAN8814_INTS);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (irq_status & LAN8814_INT_LINK) {
		phy_trigger_machine(phydev);
		ret = IRQ_HANDLED;
	}

	while (1) {
		tsu_irq_status = lanphy_read_page_reg(phydev, 4,
						      LAN8814_INTR_STS_REG);

		if (tsu_irq_status > 0 &&
		    (tsu_irq_status & (LAN8814_INTR_STS_REG_1588_TSU0_ |
				       LAN8814_INTR_STS_REG_1588_TSU1_ |
				       LAN8814_INTR_STS_REG_1588_TSU2_ |
				       LAN8814_INTR_STS_REG_1588_TSU3_))) {
			lan8814_handle_ptp_interrupt(phydev);
			ret = IRQ_HANDLED;
		} else {
			break;
		}
	}

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
}

static int lan8814_ptp_probe_once(struct phy_device *phydev)
{
	struct lan8814_shared_priv *shared = phydev->shared->priv;

	if (!IS_ENABLED(CONFIG_PTP_1588_CLOCK) ||
	    !IS_ENABLED(CONFIG_NETWORK_PHY_TIMESTAMPING))
		return 0;

	/* Initialise shared lock for clock*/
	mutex_init(&shared->shared_lock);

	shared->ptp_clock_info.owner = THIS_MODULE;
	snprintf(shared->ptp_clock_info.name, 30, "%s", phydev->drv->name);
	shared->ptp_clock_info.max_adj = 31249999;
	shared->ptp_clock_info.n_alarm = 0;
	shared->ptp_clock_info.n_ext_ts = 0;
	shared->ptp_clock_info.n_pins = 0;
	shared->ptp_clock_info.pps = 0;
	shared->ptp_clock_info.pin_config = NULL;
	shared->ptp_clock_info.adjfine = lan8814_ptpci_adjfine;
	shared->ptp_clock_info.adjtime = lan8814_ptpci_adjtime;
	shared->ptp_clock_info.gettime64 = lan8814_ptpci_gettime64;
	shared->ptp_clock_info.settime64 = lan8814_ptpci_settime64;
	shared->ptp_clock_info.getcrosststamp = NULL;

	shared->ptp_clock = ptp_clock_register(&shared->ptp_clock_info,
					       &phydev->mdio.dev);
	if (IS_ERR_OR_NULL(shared->ptp_clock)) {
		phydev_err(phydev, "ptp_clock_register failed %lu\n",
			   PTR_ERR(shared->ptp_clock));
		return -EINVAL;
	}

	phydev_dbg(phydev, "successfully registered ptp clock\n");

	shared->phydev = phydev;

	/* The EP.4 is shared between all the PHYs in the package and also it
	 * can be accessed by any of the PHYs
	 */
	lanphy_write_page_reg(phydev, 4, LTC_HARD_RESET, LTC_HARD_RESET_);
	lanphy_write_page_reg(phydev, 4, PTP_OPERATING_MODE,
			      PTP_OPERATING_MODE_STANDALONE_);

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

	return 0;
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
	/* No suspend/resume callbacks because of errata DS80000700A,
	 * receiver error following software power down.
	 */
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
	.suspend	= kszphy_suspend,
	.resume		= kszphy_resume,
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
	.config_intr	= ksz9031_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.set_wol	= ksz9031_set_wol,
	.get_wol	 = ksz9031_get_wol,
	.suspend	= ksz9031_suspend,
	.resume		= ksz9031_resume,
	.cable_test_start	= ksz9x31_cable_test_start,
	.cable_test_get_status	= ksz9x31_cable_test_get_status,
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
	.suspend	= genphy_suspend,
	.resume		= kszphy_resume,
	.config_intr	= lan8804_config_intr,
	.handle_interrupt = lan8804_handle_interrupt,
}, {
	.phy_id		= PHY_ID_KSZ9131,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Microchip KSZ9131 Gigabit PHY",
	/* PHY_GBIT_FEATURES */
	.flags		= PHY_POLL_CABLE_TEST,
	.driver_data	= &ksz9021_type,
	.probe		= kszphy_probe,
	.soft_reset	= genphy_soft_reset,
	.config_init	= ksz9131_config_init,
	.config_intr	= kszphy_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= kszphy_suspend,
	.resume		= kszphy_resume,
	.cable_test_start	= ksz9x31_cable_test_start,
	.cable_test_get_status	= ksz9x31_cable_test_get_status,
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
	.config_init	= kszphy_config_init,
	.config_intr	= kszphy_config_intr,
	.handle_interrupt = kszphy_handle_interrupt,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
} };

module_phy_driver(ksphy_driver);

MODULE_DESCRIPTION("Micrel PHY driver");
MODULE_AUTHOR("David J. Choi");
MODULE_LICENSE("GPL");

static struct mdio_device_id __maybe_unused micrel_tbl[] = {
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
	{ }
};

MODULE_DEVICE_TABLE(mdio, micrel_tbl);
