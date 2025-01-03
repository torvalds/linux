// SPDX-License-Identifier: GPL-2.0
/* NXP C45 PHY driver
 * Copyright 2021-2023 NXP
 * Author: Radu Pirea <radu-nicolae.pirea@oss.nxp.com>
 */

#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/processor.h>
#include <linux/property.h>
#include <linux/ptp_classify.h>
#include <linux/net_tstamp.h>

#include "nxp-c45-tja11xx.h"

#define PHY_ID_TJA_1103			0x001BB010
#define PHY_ID_TJA_1120			0x001BB031

#define VEND1_DEVICE_CONTROL		0x0040
#define DEVICE_CONTROL_RESET		BIT(15)
#define DEVICE_CONTROL_CONFIG_GLOBAL_EN	BIT(14)
#define DEVICE_CONTROL_CONFIG_ALL_EN	BIT(13)

#define VEND1_DEVICE_CONFIG		0x0048

#define TJA1120_VEND1_EXT_TS_MODE	0x1012

#define TJA1120_GLOBAL_INFRA_IRQ_ACK	0x2C08
#define TJA1120_GLOBAL_INFRA_IRQ_EN	0x2C0A
#define TJA1120_GLOBAL_INFRA_IRQ_STATUS	0x2C0C
#define TJA1120_DEV_BOOT_DONE		BIT(1)

#define TJA1120_VEND1_PTP_TRIG_DATA_S	0x1070

#define TJA1120_EGRESS_TS_DATA_S	0x9060
#define TJA1120_EGRESS_TS_END		0x9067
#define TJA1120_TS_VALID		BIT(0)
#define TJA1120_MORE_TS			BIT(15)

#define VEND1_PHY_IRQ_ACK		0x80A0
#define VEND1_PHY_IRQ_EN		0x80A1
#define VEND1_PHY_IRQ_STATUS		0x80A2
#define PHY_IRQ_LINK_EVENT		BIT(1)

#define VEND1_ALWAYS_ACCESSIBLE		0x801F
#define FUSA_PASS			BIT(4)

#define VEND1_PHY_CONTROL		0x8100
#define PHY_CONFIG_EN			BIT(14)
#define PHY_START_OP			BIT(0)

#define VEND1_PHY_CONFIG		0x8108
#define PHY_CONFIG_AUTO			BIT(0)

#define TJA1120_EPHY_RESETS		0x810A
#define EPHY_PCS_RESET			BIT(3)

#define VEND1_SIGNAL_QUALITY		0x8320
#define SQI_VALID			BIT(14)
#define SQI_MASK			GENMASK(2, 0)
#define MAX_SQI				SQI_MASK

#define CABLE_TEST_ENABLE		BIT(15)
#define CABLE_TEST_START		BIT(14)
#define CABLE_TEST_OK			0x00
#define CABLE_TEST_SHORTED		0x01
#define CABLE_TEST_OPEN			0x02
#define CABLE_TEST_UNKNOWN		0x07

#define VEND1_PORT_CONTROL		0x8040
#define PORT_CONTROL_EN			BIT(14)

#define VEND1_PORT_ABILITIES		0x8046
#define MACSEC_ABILITY			BIT(5)
#define PTP_ABILITY			BIT(3)

#define VEND1_PORT_FUNC_IRQ_EN		0x807A
#define MACSEC_IRQS			BIT(5)
#define PTP_IRQS			BIT(3)

#define VEND1_PTP_IRQ_ACK		0x9008
#define EGR_TS_IRQ			BIT(1)

#define VEND1_PORT_INFRA_CONTROL	0xAC00
#define PORT_INFRA_CONTROL_EN		BIT(14)

#define VEND1_RXID			0xAFCC
#define VEND1_TXID			0xAFCD
#define ID_ENABLE			BIT(15)

#define VEND1_ABILITIES			0xAFC4
#define RGMII_ID_ABILITY		BIT(15)
#define RGMII_ABILITY			BIT(14)
#define RMII_ABILITY			BIT(10)
#define REVMII_ABILITY			BIT(9)
#define MII_ABILITY			BIT(8)
#define SGMII_ABILITY			BIT(0)

#define VEND1_MII_BASIC_CONFIG		0xAFC6
#define MII_BASIC_CONFIG_REV		BIT(4)
#define MII_BASIC_CONFIG_SGMII		0x9
#define MII_BASIC_CONFIG_RGMII		0x7
#define MII_BASIC_CONFIG_RMII		0x5
#define MII_BASIC_CONFIG_MII		0x4

#define VEND1_SYMBOL_ERROR_CNT_XTD	0x8351
#define EXTENDED_CNT_EN			BIT(15)
#define VEND1_MONITOR_STATUS		0xAC80
#define MONITOR_RESET			BIT(15)
#define VEND1_MONITOR_CONFIG		0xAC86
#define LOST_FRAMES_CNT_EN		BIT(9)
#define ALL_FRAMES_CNT_EN		BIT(8)

#define VEND1_SYMBOL_ERROR_COUNTER	0x8350
#define VEND1_LINK_DROP_COUNTER		0x8352
#define VEND1_LINK_LOSSES_AND_FAILURES	0x8353
#define VEND1_RX_PREAMBLE_COUNT		0xAFCE
#define VEND1_TX_PREAMBLE_COUNT		0xAFCF
#define VEND1_RX_IPG_LENGTH		0xAFD0
#define VEND1_TX_IPG_LENGTH		0xAFD1
#define COUNTER_EN			BIT(15)

#define VEND1_PTP_CONFIG		0x1102
#define EXT_TRG_EDGE			BIT(1)

#define TJA1120_SYNC_TRIG_FILTER	0x1010
#define PTP_TRIG_RISE_TS		BIT(3)
#define PTP_TRIG_FALLING_TS		BIT(2)

#define CLK_RATE_ADJ_LD			BIT(15)
#define CLK_RATE_ADJ_DIR		BIT(14)

#define VEND1_RX_TS_INSRT_CTRL		0x114D
#define TJA1103_RX_TS_INSRT_MODE2	0x02

#define TJA1120_RX_TS_INSRT_CTRL	0x9012
#define TJA1120_RX_TS_INSRT_EN		BIT(15)
#define TJA1120_TS_INSRT_MODE		BIT(4)

#define VEND1_EGR_RING_DATA_0		0x114E
#define VEND1_EGR_RING_CTRL		0x1154

#define RING_DATA_0_TS_VALID		BIT(15)

#define RING_DONE			BIT(0)

#define TS_SEC_MASK			GENMASK(1, 0)

#define PTP_ENABLE			BIT(3)
#define PHY_TEST_ENABLE			BIT(0)

#define VEND1_PORT_PTP_CONTROL		0x9000
#define PORT_PTP_CONTROL_BYPASS		BIT(11)

#define PTP_CLK_PERIOD_100BT1		15ULL
#define PTP_CLK_PERIOD_1000BT1		8ULL

#define EVENT_MSG_FILT_ALL		0x0F
#define EVENT_MSG_FILT_NONE		0x00

#define VEND1_GPIO_FUNC_CONFIG_BASE	0x2C40
#define GPIO_FUNC_EN			BIT(15)
#define GPIO_FUNC_PTP			BIT(6)
#define GPIO_SIGNAL_PTP_TRIGGER		0x01
#define GPIO_SIGNAL_PPS_OUT		0x12
#define GPIO_DISABLE			0
#define GPIO_PPS_OUT_CFG		(GPIO_FUNC_EN | GPIO_FUNC_PTP | \
	GPIO_SIGNAL_PPS_OUT)
#define GPIO_EXTTS_OUT_CFG		(GPIO_FUNC_EN | GPIO_FUNC_PTP | \
	GPIO_SIGNAL_PTP_TRIGGER)

#define RGMII_PERIOD_PS			8000U
#define PS_PER_DEGREE			div_u64(RGMII_PERIOD_PS, 360)
#define MIN_ID_PS			1644U
#define MAX_ID_PS			2260U
#define DEFAULT_ID_PS			2000U

#define PPM_TO_SUBNS_INC(ppb, ptp_clk_period) div_u64(GENMASK_ULL(31, 0) * \
	(ppb) * (ptp_clk_period), NSEC_PER_SEC)

#define NXP_C45_SKB_CB(skb)	((struct nxp_c45_skb_cb *)(skb)->cb)

#define TJA11XX_REVERSE_MODE		BIT(0)

struct nxp_c45_phy;

struct nxp_c45_skb_cb {
	struct ptp_header *header;
	unsigned int type;
};

#define NXP_C45_REG_FIELD(_reg, _devad, _offset, _size)	\
	((struct nxp_c45_reg_field) {			\
		.reg = _reg,				\
		.devad =  _devad,			\
		.offset = _offset,			\
		.size = _size,				\
	})

struct nxp_c45_reg_field {
	u16 reg;
	u8 devad;
	u8 offset;
	u8 size;
};

struct nxp_c45_hwts {
	u32	nsec;
	u32	sec;
	u8	domain_number;
	u16	sequence_id;
	u8	msg_type;
};

struct nxp_c45_regmap {
	/* PTP config regs. */
	u16 vend1_ptp_clk_period;
	u16 vend1_event_msg_filt;

	/* LTC bits and regs. */
	struct nxp_c45_reg_field ltc_read;
	struct nxp_c45_reg_field ltc_write;
	struct nxp_c45_reg_field ltc_lock_ctrl;
	u16 vend1_ltc_wr_nsec_0;
	u16 vend1_ltc_wr_nsec_1;
	u16 vend1_ltc_wr_sec_0;
	u16 vend1_ltc_wr_sec_1;
	u16 vend1_ltc_rd_nsec_0;
	u16 vend1_ltc_rd_nsec_1;
	u16 vend1_ltc_rd_sec_0;
	u16 vend1_ltc_rd_sec_1;
	u16 vend1_rate_adj_subns_0;
	u16 vend1_rate_adj_subns_1;

	/* External trigger reg fields. */
	struct nxp_c45_reg_field irq_egr_ts_en;
	struct nxp_c45_reg_field irq_egr_ts_status;
	struct nxp_c45_reg_field domain_number;
	struct nxp_c45_reg_field msg_type;
	struct nxp_c45_reg_field sequence_id;
	struct nxp_c45_reg_field sec_1_0;
	struct nxp_c45_reg_field sec_4_2;
	struct nxp_c45_reg_field nsec_15_0;
	struct nxp_c45_reg_field nsec_29_16;

	/* PPS and EXT Trigger bits and regs. */
	struct nxp_c45_reg_field pps_enable;
	struct nxp_c45_reg_field pps_polarity;
	u16 vend1_ext_trg_data_0;
	u16 vend1_ext_trg_data_1;
	u16 vend1_ext_trg_data_2;
	u16 vend1_ext_trg_data_3;
	u16 vend1_ext_trg_ctrl;

	/* Cable test reg fields. */
	u16 cable_test;
	struct nxp_c45_reg_field cable_test_valid;
	struct nxp_c45_reg_field cable_test_result;
};

struct nxp_c45_phy_stats {
	const char	*name;
	const struct nxp_c45_reg_field counter;
};

struct nxp_c45_phy_data {
	const struct nxp_c45_regmap *regmap;
	const struct nxp_c45_phy_stats *stats;
	int n_stats;
	u8 ptp_clk_period;
	bool ext_ts_both_edges;
	bool ack_ptp_irq;
	void (*counters_enable)(struct phy_device *phydev);
	bool (*get_egressts)(struct nxp_c45_phy *priv,
			     struct nxp_c45_hwts *hwts);
	bool (*get_extts)(struct nxp_c45_phy *priv, struct timespec64 *extts);
	void (*ptp_init)(struct phy_device *phydev);
	void (*ptp_enable)(struct phy_device *phydev, bool enable);
	void (*nmi_handler)(struct phy_device *phydev,
			    irqreturn_t *irq_status);
};

static const
struct nxp_c45_phy_data *nxp_c45_get_data(struct phy_device *phydev)
{
	return phydev->drv->driver_data;
}

static const
struct nxp_c45_regmap *nxp_c45_get_regmap(struct phy_device *phydev)
{
	const struct nxp_c45_phy_data *phy_data = nxp_c45_get_data(phydev);

	return phy_data->regmap;
}

static int nxp_c45_read_reg_field(struct phy_device *phydev,
				  const struct nxp_c45_reg_field *reg_field)
{
	u16 mask;
	int ret;

	if (reg_field->size == 0) {
		phydev_err(phydev, "Trying to read a reg field of size 0.\n");
		return -EINVAL;
	}

	ret = phy_read_mmd(phydev, reg_field->devad, reg_field->reg);
	if (ret < 0)
		return ret;

	mask = reg_field->size == 1 ? BIT(reg_field->offset) :
		GENMASK(reg_field->offset + reg_field->size - 1,
			reg_field->offset);
	ret &= mask;
	ret >>= reg_field->offset;

	return ret;
}

static int nxp_c45_write_reg_field(struct phy_device *phydev,
				   const struct nxp_c45_reg_field *reg_field,
				   u16 val)
{
	u16 mask;
	u16 set;

	if (reg_field->size == 0) {
		phydev_err(phydev, "Trying to write a reg field of size 0.\n");
		return -EINVAL;
	}

	mask = reg_field->size == 1 ? BIT(reg_field->offset) :
		GENMASK(reg_field->offset + reg_field->size - 1,
			reg_field->offset);
	set = val << reg_field->offset;

	return phy_modify_mmd_changed(phydev, reg_field->devad,
				      reg_field->reg, mask, set);
}

static int nxp_c45_set_reg_field(struct phy_device *phydev,
				 const struct nxp_c45_reg_field *reg_field)
{
	if (reg_field->size != 1) {
		phydev_err(phydev, "Trying to set a reg field of size different than 1.\n");
		return -EINVAL;
	}

	return nxp_c45_write_reg_field(phydev, reg_field, 1);
}

static int nxp_c45_clear_reg_field(struct phy_device *phydev,
				   const struct nxp_c45_reg_field *reg_field)
{
	if (reg_field->size != 1) {
		phydev_err(phydev, "Trying to set a reg field of size different than 1.\n");
		return -EINVAL;
	}

	return nxp_c45_write_reg_field(phydev, reg_field, 0);
}

static bool nxp_c45_poll_txts(struct phy_device *phydev)
{
	return phydev->irq <= 0;
}

static int _nxp_c45_ptp_gettimex64(struct ptp_clock_info *ptp,
				   struct timespec64 *ts,
				   struct ptp_system_timestamp *sts)
{
	struct nxp_c45_phy *priv = container_of(ptp, struct nxp_c45_phy, caps);
	const struct nxp_c45_regmap *regmap = nxp_c45_get_regmap(priv->phydev);

	nxp_c45_set_reg_field(priv->phydev, &regmap->ltc_read);
	ts->tv_nsec = phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				   regmap->vend1_ltc_rd_nsec_0);
	ts->tv_nsec |= phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				    regmap->vend1_ltc_rd_nsec_1) << 16;
	ts->tv_sec = phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				  regmap->vend1_ltc_rd_sec_0);
	ts->tv_sec |= phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				   regmap->vend1_ltc_rd_sec_1) << 16;

	return 0;
}

static int nxp_c45_ptp_gettimex64(struct ptp_clock_info *ptp,
				  struct timespec64 *ts,
				  struct ptp_system_timestamp *sts)
{
	struct nxp_c45_phy *priv = container_of(ptp, struct nxp_c45_phy, caps);

	mutex_lock(&priv->ptp_lock);
	_nxp_c45_ptp_gettimex64(ptp, ts, sts);
	mutex_unlock(&priv->ptp_lock);

	return 0;
}

static int _nxp_c45_ptp_settime64(struct ptp_clock_info *ptp,
				  const struct timespec64 *ts)
{
	struct nxp_c45_phy *priv = container_of(ptp, struct nxp_c45_phy, caps);
	const struct nxp_c45_regmap *regmap = nxp_c45_get_regmap(priv->phydev);

	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1, regmap->vend1_ltc_wr_nsec_0,
		      ts->tv_nsec);
	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1, regmap->vend1_ltc_wr_nsec_1,
		      ts->tv_nsec >> 16);
	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1, regmap->vend1_ltc_wr_sec_0,
		      ts->tv_sec);
	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1, regmap->vend1_ltc_wr_sec_1,
		      ts->tv_sec >> 16);
	nxp_c45_set_reg_field(priv->phydev, &regmap->ltc_write);

	return 0;
}

static int nxp_c45_ptp_settime64(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct nxp_c45_phy *priv = container_of(ptp, struct nxp_c45_phy, caps);

	mutex_lock(&priv->ptp_lock);
	_nxp_c45_ptp_settime64(ptp, ts);
	mutex_unlock(&priv->ptp_lock);

	return 0;
}

static int nxp_c45_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct nxp_c45_phy *priv = container_of(ptp, struct nxp_c45_phy, caps);
	const struct nxp_c45_phy_data *data = nxp_c45_get_data(priv->phydev);
	const struct nxp_c45_regmap *regmap = data->regmap;
	s32 ppb = scaled_ppm_to_ppb(scaled_ppm);
	u64 subns_inc_val;
	bool inc;

	mutex_lock(&priv->ptp_lock);
	inc = ppb >= 0;
	ppb = abs(ppb);

	subns_inc_val = PPM_TO_SUBNS_INC(ppb, data->ptp_clk_period);

	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1,
		      regmap->vend1_rate_adj_subns_0,
		      subns_inc_val);
	subns_inc_val >>= 16;
	subns_inc_val |= CLK_RATE_ADJ_LD;
	if (inc)
		subns_inc_val |= CLK_RATE_ADJ_DIR;

	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1,
		      regmap->vend1_rate_adj_subns_1,
		      subns_inc_val);
	mutex_unlock(&priv->ptp_lock);

	return 0;
}

static int nxp_c45_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct nxp_c45_phy *priv = container_of(ptp, struct nxp_c45_phy, caps);
	struct timespec64 now, then;

	mutex_lock(&priv->ptp_lock);
	then = ns_to_timespec64(delta);
	_nxp_c45_ptp_gettimex64(ptp, &now, NULL);
	now = timespec64_add(now, then);
	_nxp_c45_ptp_settime64(ptp, &now);
	mutex_unlock(&priv->ptp_lock);

	return 0;
}

static void nxp_c45_reconstruct_ts(struct timespec64 *ts,
				   struct nxp_c45_hwts *hwts)
{
	ts->tv_nsec = hwts->nsec;
	if ((ts->tv_sec & TS_SEC_MASK) < (hwts->sec & TS_SEC_MASK))
		ts->tv_sec -= TS_SEC_MASK + 1;
	ts->tv_sec &= ~TS_SEC_MASK;
	ts->tv_sec |= hwts->sec & TS_SEC_MASK;
}

static bool nxp_c45_match_ts(struct ptp_header *header,
			     struct nxp_c45_hwts *hwts,
			     unsigned int type)
{
	return ntohs(header->sequence_id) == hwts->sequence_id &&
	       ptp_get_msgtype(header, type) == hwts->msg_type &&
	       header->domain_number  == hwts->domain_number;
}

static bool nxp_c45_get_extts(struct nxp_c45_phy *priv,
			      struct timespec64 *extts)
{
	const struct nxp_c45_regmap *regmap = nxp_c45_get_regmap(priv->phydev);

	extts->tv_nsec = phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				      regmap->vend1_ext_trg_data_0);
	extts->tv_nsec |= phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				       regmap->vend1_ext_trg_data_1) << 16;
	extts->tv_sec = phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				     regmap->vend1_ext_trg_data_2);
	extts->tv_sec |= phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				      regmap->vend1_ext_trg_data_3) << 16;
	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1,
		      regmap->vend1_ext_trg_ctrl, RING_DONE);

	return true;
}

static bool tja1120_extts_is_valid(struct phy_device *phydev)
{
	bool valid;
	int reg;

	reg = phy_read_mmd(phydev, MDIO_MMD_VEND1,
			   TJA1120_VEND1_PTP_TRIG_DATA_S);
	valid = !!(reg & TJA1120_TS_VALID);

	return valid;
}

static bool tja1120_get_extts(struct nxp_c45_phy *priv,
			      struct timespec64 *extts)
{
	const struct nxp_c45_regmap *regmap = nxp_c45_get_regmap(priv->phydev);
	struct phy_device *phydev = priv->phydev;
	bool more_ts;
	bool valid;
	u16 reg;

	reg = phy_read_mmd(phydev, MDIO_MMD_VEND1,
			   regmap->vend1_ext_trg_ctrl);
	more_ts = !!(reg & TJA1120_MORE_TS);

	valid = tja1120_extts_is_valid(phydev);
	if (!valid) {
		if (!more_ts)
			goto tja1120_get_extts_out;

		/* Bug workaround for TJA1120 engineering samples: move the new
		 * timestamp from the FIFO to the buffer.
		 */
		phy_write_mmd(phydev, MDIO_MMD_VEND1,
			      regmap->vend1_ext_trg_ctrl, RING_DONE);
		valid = tja1120_extts_is_valid(phydev);
		if (!valid)
			goto tja1120_get_extts_out;
	}

	nxp_c45_get_extts(priv, extts);
tja1120_get_extts_out:
	return valid;
}

static void nxp_c45_read_egress_ts(struct nxp_c45_phy *priv,
				   struct nxp_c45_hwts *hwts)
{
	const struct nxp_c45_regmap *regmap = nxp_c45_get_regmap(priv->phydev);
	struct phy_device *phydev = priv->phydev;

	hwts->domain_number =
		nxp_c45_read_reg_field(phydev, &regmap->domain_number);
	hwts->msg_type =
		nxp_c45_read_reg_field(phydev, &regmap->msg_type);
	hwts->sequence_id =
		nxp_c45_read_reg_field(phydev, &regmap->sequence_id);
	hwts->nsec =
		nxp_c45_read_reg_field(phydev, &regmap->nsec_15_0);
	hwts->nsec |=
		nxp_c45_read_reg_field(phydev, &regmap->nsec_29_16) << 16;
	hwts->sec = nxp_c45_read_reg_field(phydev, &regmap->sec_1_0);
	hwts->sec |= nxp_c45_read_reg_field(phydev, &regmap->sec_4_2) << 2;
}

static bool nxp_c45_get_hwtxts(struct nxp_c45_phy *priv,
			       struct nxp_c45_hwts *hwts)
{
	bool valid;
	u16 reg;

	mutex_lock(&priv->ptp_lock);
	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1, VEND1_EGR_RING_CTRL,
		      RING_DONE);
	reg = phy_read_mmd(priv->phydev, MDIO_MMD_VEND1, VEND1_EGR_RING_DATA_0);
	valid = !!(reg & RING_DATA_0_TS_VALID);
	if (!valid)
		goto nxp_c45_get_hwtxts_out;

	nxp_c45_read_egress_ts(priv, hwts);
nxp_c45_get_hwtxts_out:
	mutex_unlock(&priv->ptp_lock);
	return valid;
}

static bool tja1120_egress_ts_is_valid(struct phy_device *phydev)
{
	bool valid;
	u16 reg;

	reg = phy_read_mmd(phydev, MDIO_MMD_VEND1, TJA1120_EGRESS_TS_DATA_S);
	valid = !!(reg & TJA1120_TS_VALID);

	return valid;
}

static bool tja1120_get_hwtxts(struct nxp_c45_phy *priv,
			       struct nxp_c45_hwts *hwts)
{
	struct phy_device *phydev = priv->phydev;
	bool more_ts;
	bool valid;
	u16 reg;

	mutex_lock(&priv->ptp_lock);
	reg = phy_read_mmd(phydev, MDIO_MMD_VEND1, TJA1120_EGRESS_TS_END);
	more_ts = !!(reg & TJA1120_MORE_TS);
	valid = tja1120_egress_ts_is_valid(phydev);
	if (!valid) {
		if (!more_ts)
			goto tja1120_get_hwtxts_out;

		/* Bug workaround for TJA1120 engineering samples: move the
		 * new timestamp from the FIFO to the buffer.
		 */
		phy_write_mmd(phydev, MDIO_MMD_VEND1,
			      TJA1120_EGRESS_TS_END, TJA1120_TS_VALID);
		valid = tja1120_egress_ts_is_valid(phydev);
		if (!valid)
			goto tja1120_get_hwtxts_out;
	}
	nxp_c45_read_egress_ts(priv, hwts);
	phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, TJA1120_EGRESS_TS_DATA_S,
			   TJA1120_TS_VALID);
tja1120_get_hwtxts_out:
	mutex_unlock(&priv->ptp_lock);
	return valid;
}

static void nxp_c45_process_txts(struct nxp_c45_phy *priv,
				 struct nxp_c45_hwts *txts)
{
	struct sk_buff *skb, *tmp, *skb_match = NULL;
	struct skb_shared_hwtstamps shhwtstamps;
	struct timespec64 ts;
	unsigned long flags;
	bool ts_match;
	s64 ts_ns;

	spin_lock_irqsave(&priv->tx_queue.lock, flags);
	skb_queue_walk_safe(&priv->tx_queue, skb, tmp) {
		ts_match = nxp_c45_match_ts(NXP_C45_SKB_CB(skb)->header, txts,
					    NXP_C45_SKB_CB(skb)->type);
		if (!ts_match)
			continue;
		skb_match = skb;
		__skb_unlink(skb, &priv->tx_queue);
		break;
	}
	spin_unlock_irqrestore(&priv->tx_queue.lock, flags);

	if (skb_match) {
		nxp_c45_ptp_gettimex64(&priv->caps, &ts, NULL);
		nxp_c45_reconstruct_ts(&ts, txts);
		memset(&shhwtstamps, 0, sizeof(shhwtstamps));
		ts_ns = timespec64_to_ns(&ts);
		shhwtstamps.hwtstamp = ns_to_ktime(ts_ns);
		skb_complete_tx_timestamp(skb_match, &shhwtstamps);
	} else {
		phydev_warn(priv->phydev,
			    "the tx timestamp doesn't match with any skb\n");
	}
}

static long nxp_c45_do_aux_work(struct ptp_clock_info *ptp)
{
	struct nxp_c45_phy *priv = container_of(ptp, struct nxp_c45_phy, caps);
	const struct nxp_c45_phy_data *data = nxp_c45_get_data(priv->phydev);
	bool poll_txts = nxp_c45_poll_txts(priv->phydev);
	struct skb_shared_hwtstamps *shhwtstamps_rx;
	struct ptp_clock_event event;
	struct nxp_c45_hwts hwts;
	bool reschedule = false;
	struct timespec64 ts;
	struct sk_buff *skb;
	bool ts_valid;
	u32 ts_raw;

	while (!skb_queue_empty_lockless(&priv->tx_queue) && poll_txts) {
		ts_valid = data->get_egressts(priv, &hwts);
		if (unlikely(!ts_valid)) {
			/* Still more skbs in the queue */
			reschedule = true;
			break;
		}

		nxp_c45_process_txts(priv, &hwts);
	}

	while ((skb = skb_dequeue(&priv->rx_queue)) != NULL) {
		nxp_c45_ptp_gettimex64(&priv->caps, &ts, NULL);
		ts_raw = __be32_to_cpu(NXP_C45_SKB_CB(skb)->header->reserved2);
		hwts.sec = ts_raw >> 30;
		hwts.nsec = ts_raw & GENMASK(29, 0);
		nxp_c45_reconstruct_ts(&ts, &hwts);
		shhwtstamps_rx = skb_hwtstamps(skb);
		shhwtstamps_rx->hwtstamp = ns_to_ktime(timespec64_to_ns(&ts));
		NXP_C45_SKB_CB(skb)->header->reserved2 = 0;
		netif_rx(skb);
	}

	if (priv->extts) {
		ts_valid = data->get_extts(priv, &ts);
		if (ts_valid && timespec64_compare(&ts, &priv->extts_ts) != 0) {
			priv->extts_ts = ts;
			event.index = priv->extts_index;
			event.type = PTP_CLOCK_EXTTS;
			event.timestamp = ns_to_ktime(timespec64_to_ns(&ts));
			ptp_clock_event(priv->ptp_clock, &event);
		}
		reschedule = true;
	}

	return reschedule ? 1 : -1;
}

static void nxp_c45_gpio_config(struct nxp_c45_phy *priv,
				int pin, u16 pin_cfg)
{
	struct phy_device *phydev = priv->phydev;

	phy_write_mmd(phydev, MDIO_MMD_VEND1,
		      VEND1_GPIO_FUNC_CONFIG_BASE + pin, pin_cfg);
}

static int nxp_c45_perout_enable(struct nxp_c45_phy *priv,
				 struct ptp_perout_request *perout, int on)
{
	const struct nxp_c45_regmap *regmap = nxp_c45_get_regmap(priv->phydev);
	struct phy_device *phydev = priv->phydev;
	int pin;

	if (perout->flags & ~PTP_PEROUT_PHASE)
		return -EOPNOTSUPP;

	pin = ptp_find_pin(priv->ptp_clock, PTP_PF_PEROUT, perout->index);
	if (pin < 0)
		return pin;

	if (!on) {
		nxp_c45_clear_reg_field(priv->phydev,
					&regmap->pps_enable);
		nxp_c45_clear_reg_field(priv->phydev,
					&regmap->pps_polarity);

		nxp_c45_gpio_config(priv, pin, GPIO_DISABLE);

		return 0;
	}

	/* The PPS signal is fixed to 1 second and is always generated when the
	 * seconds counter is incremented. The start time is not configurable.
	 * If the clock is adjusted, the PPS signal is automatically readjusted.
	 */
	if (perout->period.sec != 1 || perout->period.nsec != 0) {
		phydev_warn(phydev, "The period can be set only to 1 second.");
		return -EINVAL;
	}

	if (!(perout->flags & PTP_PEROUT_PHASE)) {
		if (perout->start.sec != 0 || perout->start.nsec != 0) {
			phydev_warn(phydev, "The start time is not configurable. Should be set to 0 seconds and 0 nanoseconds.");
			return -EINVAL;
		}
	} else {
		if (perout->phase.nsec != 0 &&
		    perout->phase.nsec != (NSEC_PER_SEC >> 1)) {
			phydev_warn(phydev, "The phase can be set only to 0 or 500000000 nanoseconds.");
			return -EINVAL;
		}

		if (perout->phase.nsec == 0)
			nxp_c45_clear_reg_field(priv->phydev,
						&regmap->pps_polarity);
		else
			nxp_c45_set_reg_field(priv->phydev,
					      &regmap->pps_polarity);
	}

	nxp_c45_gpio_config(priv, pin, GPIO_PPS_OUT_CFG);

	nxp_c45_set_reg_field(priv->phydev, &regmap->pps_enable);

	return 0;
}

static void nxp_c45_set_rising_or_falling(struct phy_device *phydev,
					  struct ptp_extts_request *extts)
{
	if (extts->flags & PTP_RISING_EDGE)
		phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
				   VEND1_PTP_CONFIG, EXT_TRG_EDGE);

	if (extts->flags & PTP_FALLING_EDGE)
		phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
				 VEND1_PTP_CONFIG, EXT_TRG_EDGE);
}

static void nxp_c45_set_rising_and_falling(struct phy_device *phydev,
					   struct ptp_extts_request *extts)
{
	/* PTP_EXTTS_REQUEST may have only the PTP_ENABLE_FEATURE flag set. In
	 * this case external ts will be enabled on rising edge.
	 */
	if (extts->flags & PTP_RISING_EDGE ||
	    extts->flags == PTP_ENABLE_FEATURE)
		phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
				 TJA1120_SYNC_TRIG_FILTER,
				 PTP_TRIG_RISE_TS);
	else
		phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
				   TJA1120_SYNC_TRIG_FILTER,
				   PTP_TRIG_RISE_TS);

	if (extts->flags & PTP_FALLING_EDGE)
		phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
				 TJA1120_SYNC_TRIG_FILTER,
				 PTP_TRIG_FALLING_TS);
	else
		phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
				   TJA1120_SYNC_TRIG_FILTER,
				   PTP_TRIG_FALLING_TS);
}

static int nxp_c45_extts_enable(struct nxp_c45_phy *priv,
				struct ptp_extts_request *extts, int on)
{
	const struct nxp_c45_phy_data *data = nxp_c45_get_data(priv->phydev);
	int pin;

	if (extts->flags & ~(PTP_ENABLE_FEATURE |
			      PTP_RISING_EDGE |
			      PTP_FALLING_EDGE |
			      PTP_STRICT_FLAGS))
		return -EOPNOTSUPP;

	/* Sampling on both edges is not supported */
	if ((extts->flags & PTP_RISING_EDGE) &&
	    (extts->flags & PTP_FALLING_EDGE) &&
	    !data->ext_ts_both_edges)
		return -EOPNOTSUPP;

	pin = ptp_find_pin(priv->ptp_clock, PTP_PF_EXTTS, extts->index);
	if (pin < 0)
		return pin;

	if (!on) {
		nxp_c45_gpio_config(priv, pin, GPIO_DISABLE);
		priv->extts = false;

		return 0;
	}

	if (data->ext_ts_both_edges)
		nxp_c45_set_rising_and_falling(priv->phydev, extts);
	else
		nxp_c45_set_rising_or_falling(priv->phydev, extts);

	nxp_c45_gpio_config(priv, pin, GPIO_EXTTS_OUT_CFG);
	priv->extts = true;
	priv->extts_index = extts->index;
	ptp_schedule_worker(priv->ptp_clock, 0);

	return 0;
}

static int nxp_c45_ptp_enable(struct ptp_clock_info *ptp,
			      struct ptp_clock_request *req, int on)
{
	struct nxp_c45_phy *priv = container_of(ptp, struct nxp_c45_phy, caps);

	switch (req->type) {
	case PTP_CLK_REQ_EXTTS:
		return nxp_c45_extts_enable(priv, &req->extts, on);
	case PTP_CLK_REQ_PEROUT:
		return nxp_c45_perout_enable(priv, &req->perout, on);
	default:
		return -EOPNOTSUPP;
	}
}

static struct ptp_pin_desc nxp_c45_ptp_pins[] = {
	{ "nxp_c45_gpio0", 0, PTP_PF_NONE},
	{ "nxp_c45_gpio1", 1, PTP_PF_NONE},
	{ "nxp_c45_gpio2", 2, PTP_PF_NONE},
	{ "nxp_c45_gpio3", 3, PTP_PF_NONE},
	{ "nxp_c45_gpio4", 4, PTP_PF_NONE},
	{ "nxp_c45_gpio5", 5, PTP_PF_NONE},
	{ "nxp_c45_gpio6", 6, PTP_PF_NONE},
	{ "nxp_c45_gpio7", 7, PTP_PF_NONE},
	{ "nxp_c45_gpio8", 8, PTP_PF_NONE},
	{ "nxp_c45_gpio9", 9, PTP_PF_NONE},
	{ "nxp_c45_gpio10", 10, PTP_PF_NONE},
	{ "nxp_c45_gpio11", 11, PTP_PF_NONE},
};

static int nxp_c45_ptp_verify_pin(struct ptp_clock_info *ptp, unsigned int pin,
				  enum ptp_pin_function func, unsigned int chan)
{
	if (pin >= ARRAY_SIZE(nxp_c45_ptp_pins))
		return -EINVAL;

	switch (func) {
	case PTP_PF_NONE:
	case PTP_PF_PEROUT:
	case PTP_PF_EXTTS:
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int nxp_c45_init_ptp_clock(struct nxp_c45_phy *priv)
{
	priv->caps = (struct ptp_clock_info) {
		.owner		= THIS_MODULE,
		.name		= "NXP C45 PHC",
		.max_adj	= 16666666,
		.adjfine	= nxp_c45_ptp_adjfine,
		.adjtime	= nxp_c45_ptp_adjtime,
		.gettimex64	= nxp_c45_ptp_gettimex64,
		.settime64	= nxp_c45_ptp_settime64,
		.enable		= nxp_c45_ptp_enable,
		.verify		= nxp_c45_ptp_verify_pin,
		.do_aux_work	= nxp_c45_do_aux_work,
		.pin_config	= nxp_c45_ptp_pins,
		.n_pins		= ARRAY_SIZE(nxp_c45_ptp_pins),
		.n_ext_ts	= 1,
		.n_per_out	= 1,
	};

	priv->ptp_clock = ptp_clock_register(&priv->caps,
					     &priv->phydev->mdio.dev);

	if (IS_ERR(priv->ptp_clock))
		return PTR_ERR(priv->ptp_clock);

	if (!priv->ptp_clock)
		return -ENOMEM;

	return 0;
}

static void nxp_c45_txtstamp(struct mii_timestamper *mii_ts,
			     struct sk_buff *skb, int type)
{
	struct nxp_c45_phy *priv = container_of(mii_ts, struct nxp_c45_phy,
						mii_ts);

	switch (priv->hwts_tx) {
	case HWTSTAMP_TX_ON:
		NXP_C45_SKB_CB(skb)->type = type;
		NXP_C45_SKB_CB(skb)->header = ptp_parse_header(skb, type);
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		skb_queue_tail(&priv->tx_queue, skb);
		if (nxp_c45_poll_txts(priv->phydev))
			ptp_schedule_worker(priv->ptp_clock, 0);
		break;
	case HWTSTAMP_TX_OFF:
	default:
		kfree_skb(skb);
		break;
	}
}

static bool nxp_c45_rxtstamp(struct mii_timestamper *mii_ts,
			     struct sk_buff *skb, int type)
{
	struct nxp_c45_phy *priv = container_of(mii_ts, struct nxp_c45_phy,
						mii_ts);
	struct ptp_header *header = ptp_parse_header(skb, type);

	if (!header)
		return false;

	if (!priv->hwts_rx)
		return false;

	NXP_C45_SKB_CB(skb)->header = header;
	skb_queue_tail(&priv->rx_queue, skb);
	ptp_schedule_worker(priv->ptp_clock, 0);

	return true;
}

static int nxp_c45_hwtstamp(struct mii_timestamper *mii_ts,
			    struct kernel_hwtstamp_config *cfg,
			    struct netlink_ext_ack *extack)
{
	struct nxp_c45_phy *priv = container_of(mii_ts, struct nxp_c45_phy,
						mii_ts);
	struct phy_device *phydev = priv->phydev;
	const struct nxp_c45_phy_data *data;

	if (cfg->tx_type < 0 || cfg->tx_type > HWTSTAMP_TX_ON)
		return -ERANGE;

	data = nxp_c45_get_data(phydev);
	priv->hwts_tx = cfg->tx_type;

	switch (cfg->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		priv->hwts_rx = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		priv->hwts_rx = 1;
		cfg->rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		break;
	default:
		return -ERANGE;
	}

	if (priv->hwts_rx || priv->hwts_tx) {
		phy_write_mmd(phydev, MDIO_MMD_VEND1,
			      data->regmap->vend1_event_msg_filt,
			      EVENT_MSG_FILT_ALL);
		data->ptp_enable(phydev, true);
	} else {
		phy_write_mmd(phydev, MDIO_MMD_VEND1,
			      data->regmap->vend1_event_msg_filt,
			      EVENT_MSG_FILT_NONE);
		data->ptp_enable(phydev, false);
	}

	if (nxp_c45_poll_txts(priv->phydev))
		goto nxp_c45_no_ptp_irq;

	if (priv->hwts_tx)
		nxp_c45_set_reg_field(phydev, &data->regmap->irq_egr_ts_en);
	else
		nxp_c45_clear_reg_field(phydev, &data->regmap->irq_egr_ts_en);

nxp_c45_no_ptp_irq:
	return 0;
}

static int nxp_c45_ts_info(struct mii_timestamper *mii_ts,
			   struct kernel_ethtool_ts_info *ts_info)
{
	struct nxp_c45_phy *priv = container_of(mii_ts, struct nxp_c45_phy,
						mii_ts);

	ts_info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
			SOF_TIMESTAMPING_RX_HARDWARE |
			SOF_TIMESTAMPING_RAW_HARDWARE;
	ts_info->phc_index = ptp_clock_index(priv->ptp_clock);
	ts_info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);
	ts_info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			(1 << HWTSTAMP_FILTER_PTP_V2_L2_SYNC) |
			(1 << HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ) |
			(1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT);

	return 0;
}

static const struct nxp_c45_phy_stats common_hw_stats[] = {
	{ "phy_link_status_drop_cnt",
		NXP_C45_REG_FIELD(0x8352, MDIO_MMD_VEND1, 8, 6), },
	{ "phy_link_availability_drop_cnt",
		NXP_C45_REG_FIELD(0x8352, MDIO_MMD_VEND1, 0, 6), },
	{ "phy_link_loss_cnt",
		NXP_C45_REG_FIELD(0x8353, MDIO_MMD_VEND1, 10, 6), },
	{ "phy_link_failure_cnt",
		NXP_C45_REG_FIELD(0x8353, MDIO_MMD_VEND1, 0, 10), },
	{ "phy_symbol_error_cnt",
		NXP_C45_REG_FIELD(0x8350, MDIO_MMD_VEND1, 0, 16) },
};

static const struct nxp_c45_phy_stats tja1103_hw_stats[] = {
	{ "rx_preamble_count",
		NXP_C45_REG_FIELD(0xAFCE, MDIO_MMD_VEND1, 0, 6), },
	{ "tx_preamble_count",
		NXP_C45_REG_FIELD(0xAFCF, MDIO_MMD_VEND1, 0, 6), },
	{ "rx_ipg_length",
		NXP_C45_REG_FIELD(0xAFD0, MDIO_MMD_VEND1, 0, 9), },
	{ "tx_ipg_length",
		NXP_C45_REG_FIELD(0xAFD1, MDIO_MMD_VEND1, 0, 9), },
};

static const struct nxp_c45_phy_stats tja1120_hw_stats[] = {
	{ "phy_symbol_error_cnt_ext",
		NXP_C45_REG_FIELD(0x8351, MDIO_MMD_VEND1, 0, 14) },
	{ "tx_frames_xtd",
		NXP_C45_REG_FIELD(0xACA1, MDIO_MMD_VEND1, 0, 8), },
	{ "tx_frames",
		NXP_C45_REG_FIELD(0xACA0, MDIO_MMD_VEND1, 0, 16), },
	{ "rx_frames_xtd",
		NXP_C45_REG_FIELD(0xACA3, MDIO_MMD_VEND1, 0, 8), },
	{ "rx_frames",
		NXP_C45_REG_FIELD(0xACA2, MDIO_MMD_VEND1, 0, 16), },
	{ "tx_lost_frames_xtd",
		NXP_C45_REG_FIELD(0xACA5, MDIO_MMD_VEND1, 0, 8), },
	{ "tx_lost_frames",
		NXP_C45_REG_FIELD(0xACA4, MDIO_MMD_VEND1, 0, 16), },
	{ "rx_lost_frames_xtd",
		NXP_C45_REG_FIELD(0xACA7, MDIO_MMD_VEND1, 0, 8), },
	{ "rx_lost_frames",
		NXP_C45_REG_FIELD(0xACA6, MDIO_MMD_VEND1, 0, 16), },
};

static int nxp_c45_get_sset_count(struct phy_device *phydev)
{
	const struct nxp_c45_phy_data *phy_data = nxp_c45_get_data(phydev);

	return ARRAY_SIZE(common_hw_stats) + (phy_data ? phy_data->n_stats : 0);
}

static void nxp_c45_get_strings(struct phy_device *phydev, u8 *data)
{
	const struct nxp_c45_phy_data *phy_data = nxp_c45_get_data(phydev);
	size_t count = nxp_c45_get_sset_count(phydev);
	size_t idx;
	size_t i;

	for (i = 0; i < count; i++) {
		if (i < ARRAY_SIZE(common_hw_stats)) {
			ethtool_puts(&data, common_hw_stats[i].name);
			continue;
		}
		idx = i - ARRAY_SIZE(common_hw_stats);
		ethtool_puts(&data, phy_data->stats[idx].name);
	}
}

static void nxp_c45_get_stats(struct phy_device *phydev,
			      struct ethtool_stats *stats, u64 *data)
{
	const struct nxp_c45_phy_data *phy_data = nxp_c45_get_data(phydev);
	size_t count = nxp_c45_get_sset_count(phydev);
	const struct nxp_c45_reg_field *reg_field;
	size_t idx;
	size_t i;
	int ret;

	for (i = 0; i < count; i++) {
		if (i < ARRAY_SIZE(common_hw_stats)) {
			reg_field = &common_hw_stats[i].counter;
		} else {
			idx = i - ARRAY_SIZE(common_hw_stats);
			reg_field = &phy_data->stats[idx].counter;
		}

		ret = nxp_c45_read_reg_field(phydev, reg_field);
		if (ret < 0)
			data[i] = U64_MAX;
		else
			data[i] = ret;
	}
}

static int nxp_c45_config_enable(struct phy_device *phydev)
{
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_DEVICE_CONTROL,
		      DEVICE_CONTROL_CONFIG_GLOBAL_EN |
		      DEVICE_CONTROL_CONFIG_ALL_EN);
	usleep_range(400, 450);

	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_PORT_CONTROL,
		      PORT_CONTROL_EN);
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_PHY_CONTROL,
		      PHY_CONFIG_EN);
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_PORT_INFRA_CONTROL,
		      PORT_INFRA_CONTROL_EN);

	return 0;
}

static int nxp_c45_start_op(struct phy_device *phydev)
{
	return phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PHY_CONTROL,
				PHY_START_OP);
}

static int nxp_c45_config_intr(struct phy_device *phydev)
{
	int ret;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
				       VEND1_PORT_FUNC_IRQ_EN, MACSEC_IRQS);
		if (ret)
			return ret;

		return phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
					VEND1_PHY_IRQ_EN, PHY_IRQ_LINK_EVENT);
	}

	ret = phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
				 VEND1_PORT_FUNC_IRQ_EN, MACSEC_IRQS);
	if (ret)
		return ret;

	return phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
				  VEND1_PHY_IRQ_EN, PHY_IRQ_LINK_EVENT);
}

static int tja1103_config_intr(struct phy_device *phydev)
{
	int ret;

	/* We can't disable the FUSA IRQ for TJA1103, but we can clean it up. */
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_ALWAYS_ACCESSIBLE,
			    FUSA_PASS);
	if (ret)
		return ret;

	return nxp_c45_config_intr(phydev);
}

static int tja1120_config_intr(struct phy_device *phydev)
{
	int ret;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
				       TJA1120_GLOBAL_INFRA_IRQ_EN,
				       TJA1120_DEV_BOOT_DONE);
	else
		ret = phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
					 TJA1120_GLOBAL_INFRA_IRQ_EN,
					 TJA1120_DEV_BOOT_DONE);
	if (ret)
		return ret;

	return nxp_c45_config_intr(phydev);
}

static irqreturn_t nxp_c45_handle_interrupt(struct phy_device *phydev)
{
	const struct nxp_c45_phy_data *data = nxp_c45_get_data(phydev);
	struct nxp_c45_phy *priv = phydev->priv;
	irqreturn_t ret = IRQ_NONE;
	struct nxp_c45_hwts hwts;
	int irq;

	irq = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_PHY_IRQ_STATUS);
	if (irq & PHY_IRQ_LINK_EVENT) {
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_PHY_IRQ_ACK,
			      PHY_IRQ_LINK_EVENT);
		phy_trigger_machine(phydev);
		ret = IRQ_HANDLED;
	}

	irq = nxp_c45_read_reg_field(phydev, &data->regmap->irq_egr_ts_status);
	if (irq) {
		/* If ack_ptp_irq is false, the IRQ bit is self-clear and will
		 * be cleared when the EGR TS FIFO is empty. Otherwise, the
		 * IRQ bit should be cleared before reading the timestamp,
		 */
		if (data->ack_ptp_irq)
			phy_write_mmd(phydev, MDIO_MMD_VEND1,
				      VEND1_PTP_IRQ_ACK, EGR_TS_IRQ);
		while (data->get_egressts(priv, &hwts))
			nxp_c45_process_txts(priv, &hwts);

		ret = IRQ_HANDLED;
	}

	data->nmi_handler(phydev, &ret);
	nxp_c45_handle_macsec_interrupt(phydev, &ret);

	return ret;
}

static int nxp_c45_soft_reset(struct phy_device *phydev)
{
	int ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_DEVICE_CONTROL,
			    DEVICE_CONTROL_RESET);
	if (ret)
		return ret;

	return phy_read_mmd_poll_timeout(phydev, MDIO_MMD_VEND1,
					 VEND1_DEVICE_CONTROL, ret,
					 !(ret & DEVICE_CONTROL_RESET), 20000,
					 240000, false);
}

static int nxp_c45_cable_test_start(struct phy_device *phydev)
{
	const struct nxp_c45_regmap *regmap = nxp_c45_get_regmap(phydev);

	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
			 VEND1_PORT_FUNC_ENABLES, PHY_TEST_ENABLE);
	return phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, regmap->cable_test,
				CABLE_TEST_ENABLE | CABLE_TEST_START);
}

static int nxp_c45_cable_test_get_status(struct phy_device *phydev,
					 bool *finished)
{
	const struct nxp_c45_regmap *regmap = nxp_c45_get_regmap(phydev);
	int ret;
	u8 cable_test_result;

	ret = nxp_c45_read_reg_field(phydev, &regmap->cable_test_valid);
	if (!ret) {
		*finished = false;
		return 0;
	}

	*finished = true;
	cable_test_result = nxp_c45_read_reg_field(phydev,
						   &regmap->cable_test_result);

	switch (cable_test_result) {
	case CABLE_TEST_OK:
		ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
					ETHTOOL_A_CABLE_RESULT_CODE_OK);
		break;
	case CABLE_TEST_SHORTED:
		ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
					ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT);
		break;
	case CABLE_TEST_OPEN:
		ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
					ETHTOOL_A_CABLE_RESULT_CODE_OPEN);
		break;
	default:
		ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
					ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC);
	}

	phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, regmap->cable_test,
			   CABLE_TEST_ENABLE);
	phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
			   VEND1_PORT_FUNC_ENABLES, PHY_TEST_ENABLE);

	return nxp_c45_start_op(phydev);
}

static int nxp_c45_get_sqi(struct phy_device *phydev)
{
	int reg;

	reg = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_SIGNAL_QUALITY);
	if (!(reg & SQI_VALID))
		return -EINVAL;

	reg &= SQI_MASK;

	return reg;
}

static void tja1120_link_change_notify(struct phy_device *phydev)
{
	/* Bug workaround for TJA1120 enegineering samples: fix egress
	 * timestamps lost after link recovery.
	 */
	if (phydev->state == PHY_NOLINK) {
		phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
				 TJA1120_EPHY_RESETS, EPHY_PCS_RESET);
		phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
				   TJA1120_EPHY_RESETS, EPHY_PCS_RESET);
	}
}

static int nxp_c45_get_sqi_max(struct phy_device *phydev)
{
	return MAX_SQI;
}

static int nxp_c45_check_delay(struct phy_device *phydev, u32 delay)
{
	if (delay < MIN_ID_PS) {
		phydev_err(phydev, "delay value smaller than %u\n", MIN_ID_PS);
		return -EINVAL;
	}

	if (delay > MAX_ID_PS) {
		phydev_err(phydev, "delay value higher than %u\n", MAX_ID_PS);
		return -EINVAL;
	}

	return 0;
}

static void nxp_c45_counters_enable(struct phy_device *phydev)
{
	const struct nxp_c45_phy_data *data = nxp_c45_get_data(phydev);

	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_LINK_DROP_COUNTER,
			 COUNTER_EN);

	data->counters_enable(phydev);
}

static void nxp_c45_ptp_init(struct phy_device *phydev)
{
	const struct nxp_c45_phy_data *data = nxp_c45_get_data(phydev);

	phy_write_mmd(phydev, MDIO_MMD_VEND1,
		      data->regmap->vend1_ptp_clk_period,
		      data->ptp_clk_period);
	nxp_c45_clear_reg_field(phydev, &data->regmap->ltc_lock_ctrl);

	data->ptp_init(phydev);
}

static u64 nxp_c45_get_phase_shift(u64 phase_offset_raw)
{
	/* The delay in degree phase is 73.8 + phase_offset_raw * 0.9.
	 * To avoid floating point operations we'll multiply by 10
	 * and get 1 decimal point precision.
	 */
	phase_offset_raw *= 10;
	phase_offset_raw -= 738;
	return div_u64(phase_offset_raw, 9);
}

static void nxp_c45_disable_delays(struct phy_device *phydev)
{
	phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_TXID, ID_ENABLE);
	phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_RXID, ID_ENABLE);
}

static void nxp_c45_set_delays(struct phy_device *phydev)
{
	struct nxp_c45_phy *priv = phydev->priv;
	u64 tx_delay = priv->tx_delay;
	u64 rx_delay = priv->rx_delay;
	u64 degree;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		degree = div_u64(tx_delay, PS_PER_DEGREE);
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_TXID,
			      ID_ENABLE | nxp_c45_get_phase_shift(degree));
	} else {
		phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_TXID,
				   ID_ENABLE);
	}

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) {
		degree = div_u64(rx_delay, PS_PER_DEGREE);
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_RXID,
			      ID_ENABLE | nxp_c45_get_phase_shift(degree));
	} else {
		phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_RXID,
				   ID_ENABLE);
	}
}

static int nxp_c45_get_delays(struct phy_device *phydev)
{
	struct nxp_c45_phy *priv = phydev->priv;
	int ret;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		ret = device_property_read_u32(&phydev->mdio.dev,
					       "tx-internal-delay-ps",
					       &priv->tx_delay);
		if (ret)
			priv->tx_delay = DEFAULT_ID_PS;

		ret = nxp_c45_check_delay(phydev, priv->tx_delay);
		if (ret) {
			phydev_err(phydev,
				   "tx-internal-delay-ps invalid value\n");
			return ret;
		}
	}

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) {
		ret = device_property_read_u32(&phydev->mdio.dev,
					       "rx-internal-delay-ps",
					       &priv->rx_delay);
		if (ret)
			priv->rx_delay = DEFAULT_ID_PS;

		ret = nxp_c45_check_delay(phydev, priv->rx_delay);
		if (ret) {
			phydev_err(phydev,
				   "rx-internal-delay-ps invalid value\n");
			return ret;
		}
	}

	return 0;
}

static int nxp_c45_set_phy_mode(struct phy_device *phydev)
{
	struct nxp_c45_phy *priv = phydev->priv;
	u16 basic_config;
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_ABILITIES);
	phydev_dbg(phydev, "Clause 45 managed PHY abilities 0x%x\n", ret);

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		if (!(ret & RGMII_ABILITY)) {
			phydev_err(phydev, "rgmii mode not supported\n");
			return -EINVAL;
		}
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_MII_BASIC_CONFIG,
			      MII_BASIC_CONFIG_RGMII);
		nxp_c45_disable_delays(phydev);
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		if (!(ret & RGMII_ID_ABILITY)) {
			phydev_err(phydev, "rgmii-id, rgmii-txid, rgmii-rxid modes are not supported\n");
			return -EINVAL;
		}
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_MII_BASIC_CONFIG,
			      MII_BASIC_CONFIG_RGMII);
		ret = nxp_c45_get_delays(phydev);
		if (ret)
			return ret;

		nxp_c45_set_delays(phydev);
		break;
	case PHY_INTERFACE_MODE_MII:
		if (!(ret & MII_ABILITY)) {
			phydev_err(phydev, "mii mode not supported\n");
			return -EINVAL;
		}
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_MII_BASIC_CONFIG,
			      MII_BASIC_CONFIG_MII);
		break;
	case PHY_INTERFACE_MODE_REVMII:
		if (!(ret & REVMII_ABILITY)) {
			phydev_err(phydev, "rev-mii mode not supported\n");
			return -EINVAL;
		}
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_MII_BASIC_CONFIG,
			      MII_BASIC_CONFIG_MII | MII_BASIC_CONFIG_REV);
		break;
	case PHY_INTERFACE_MODE_RMII:
		if (!(ret & RMII_ABILITY)) {
			phydev_err(phydev, "rmii mode not supported\n");
			return -EINVAL;
		}

		basic_config = MII_BASIC_CONFIG_RMII;

		/* This is not PHY_INTERFACE_MODE_REVRMII */
		if (priv->flags & TJA11XX_REVERSE_MODE)
			basic_config |= MII_BASIC_CONFIG_REV;

		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_MII_BASIC_CONFIG,
			      basic_config);
		break;
	case PHY_INTERFACE_MODE_SGMII:
		if (!(ret & SGMII_ABILITY)) {
			phydev_err(phydev, "sgmii mode not supported\n");
			return -EINVAL;
		}
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_MII_BASIC_CONFIG,
			      MII_BASIC_CONFIG_SGMII);
		break;
	case PHY_INTERFACE_MODE_INTERNAL:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int nxp_c45_config_init(struct phy_device *phydev)
{
	int ret;

	ret = nxp_c45_config_enable(phydev);
	if (ret) {
		phydev_err(phydev, "Failed to enable config\n");
		return ret;
	}

	/* Bug workaround for SJA1110 rev B: enable write access
	 * to MDIO_MMD_PMAPMD
	 */
	phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x01F8, 1);
	phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x01F9, 2);

	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PHY_CONFIG,
			 PHY_CONFIG_AUTO);

	ret = nxp_c45_set_phy_mode(phydev);
	if (ret)
		return ret;

	phydev->autoneg = AUTONEG_DISABLE;

	nxp_c45_counters_enable(phydev);
	nxp_c45_ptp_init(phydev);
	ret = nxp_c45_macsec_config_init(phydev);
	if (ret)
		return ret;

	return nxp_c45_start_op(phydev);
}

static int nxp_c45_get_features(struct phy_device *phydev)
{
	linkmode_set_bit(ETHTOOL_LINK_MODE_TP_BIT, phydev->supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_MII_BIT, phydev->supported);

	return genphy_c45_pma_read_abilities(phydev);
}

static int nxp_c45_parse_dt(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	struct nxp_c45_phy *priv = phydev->priv;

	if (!IS_ENABLED(CONFIG_OF_MDIO))
		return 0;

	if (of_property_read_bool(node, "nxp,rmii-refclk-out"))
		priv->flags |= TJA11XX_REVERSE_MODE;

	return 0;
}

static int nxp_c45_probe(struct phy_device *phydev)
{
	struct nxp_c45_phy *priv;
	bool macsec_ability;
	int phy_abilities;
	bool ptp_ability;
	int ret = 0;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	skb_queue_head_init(&priv->tx_queue);
	skb_queue_head_init(&priv->rx_queue);

	priv->phydev = phydev;

	phydev->priv = priv;

	nxp_c45_parse_dt(phydev);

	mutex_init(&priv->ptp_lock);

	phy_abilities = phy_read_mmd(phydev, MDIO_MMD_VEND1,
				     VEND1_PORT_ABILITIES);
	ptp_ability = !!(phy_abilities & PTP_ABILITY);
	if (!ptp_ability) {
		phydev_dbg(phydev, "the phy does not support PTP");
		goto no_ptp_support;
	}

	if (IS_ENABLED(CONFIG_PTP_1588_CLOCK) &&
	    IS_ENABLED(CONFIG_NETWORK_PHY_TIMESTAMPING)) {
		priv->mii_ts.rxtstamp = nxp_c45_rxtstamp;
		priv->mii_ts.txtstamp = nxp_c45_txtstamp;
		priv->mii_ts.hwtstamp = nxp_c45_hwtstamp;
		priv->mii_ts.ts_info = nxp_c45_ts_info;
		phydev->mii_ts = &priv->mii_ts;
		ret = nxp_c45_init_ptp_clock(priv);

		/* Timestamp selected by default to keep legacy API */
		phydev->default_timestamp = true;
	} else {
		phydev_dbg(phydev, "PTP support not enabled even if the phy supports it");
	}

no_ptp_support:
	macsec_ability = !!(phy_abilities & MACSEC_ABILITY);
	if (!macsec_ability) {
		phydev_info(phydev, "the phy does not support MACsec\n");
		goto no_macsec_support;
	}

	if (IS_ENABLED(CONFIG_MACSEC)) {
		ret = nxp_c45_macsec_probe(phydev);
		phydev_dbg(phydev, "MACsec support enabled.");
	} else {
		phydev_dbg(phydev, "MACsec support not enabled even if the phy supports it");
	}

no_macsec_support:

	return ret;
}

static void nxp_c45_remove(struct phy_device *phydev)
{
	struct nxp_c45_phy *priv = phydev->priv;

	if (priv->ptp_clock)
		ptp_clock_unregister(priv->ptp_clock);

	skb_queue_purge(&priv->tx_queue);
	skb_queue_purge(&priv->rx_queue);
	nxp_c45_macsec_remove(phydev);
}

static void tja1103_counters_enable(struct phy_device *phydev)
{
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_RX_PREAMBLE_COUNT,
			 COUNTER_EN);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_TX_PREAMBLE_COUNT,
			 COUNTER_EN);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_RX_IPG_LENGTH,
			 COUNTER_EN);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_TX_IPG_LENGTH,
			 COUNTER_EN);
}

static void tja1103_ptp_init(struct phy_device *phydev)
{
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_RX_TS_INSRT_CTRL,
		      TJA1103_RX_TS_INSRT_MODE2);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PORT_FUNC_ENABLES,
			 PTP_ENABLE);
}

static void tja1103_ptp_enable(struct phy_device *phydev, bool enable)
{
	if (enable)
		phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
				   VEND1_PORT_PTP_CONTROL,
				   PORT_PTP_CONTROL_BYPASS);
	else
		phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
				 VEND1_PORT_PTP_CONTROL,
				 PORT_PTP_CONTROL_BYPASS);
}

static void tja1103_nmi_handler(struct phy_device *phydev,
				irqreturn_t *irq_status)
{
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1,
			   VEND1_ALWAYS_ACCESSIBLE);
	if (ret & FUSA_PASS) {
		phy_write_mmd(phydev, MDIO_MMD_VEND1,
			      VEND1_ALWAYS_ACCESSIBLE,
			      FUSA_PASS);
		*irq_status = IRQ_HANDLED;
	}
}

static const struct nxp_c45_regmap tja1103_regmap = {
	.vend1_ptp_clk_period	= 0x1104,
	.vend1_event_msg_filt	= 0x1148,
	.pps_enable		=
		NXP_C45_REG_FIELD(0x1102, MDIO_MMD_VEND1, 3, 1),
	.pps_polarity		=
		NXP_C45_REG_FIELD(0x1102, MDIO_MMD_VEND1, 2, 1),
	.ltc_lock_ctrl		=
		NXP_C45_REG_FIELD(0x1115, MDIO_MMD_VEND1, 0, 1),
	.ltc_read		=
		NXP_C45_REG_FIELD(0x1105, MDIO_MMD_VEND1, 2, 1),
	.ltc_write		=
		NXP_C45_REG_FIELD(0x1105, MDIO_MMD_VEND1, 0, 1),
	.vend1_ltc_wr_nsec_0	= 0x1106,
	.vend1_ltc_wr_nsec_1	= 0x1107,
	.vend1_ltc_wr_sec_0	= 0x1108,
	.vend1_ltc_wr_sec_1	= 0x1109,
	.vend1_ltc_rd_nsec_0	= 0x110A,
	.vend1_ltc_rd_nsec_1	= 0x110B,
	.vend1_ltc_rd_sec_0	= 0x110C,
	.vend1_ltc_rd_sec_1	= 0x110D,
	.vend1_rate_adj_subns_0	= 0x110F,
	.vend1_rate_adj_subns_1	= 0x1110,
	.irq_egr_ts_en		=
		NXP_C45_REG_FIELD(0x1131, MDIO_MMD_VEND1, 0, 1),
	.irq_egr_ts_status	=
		NXP_C45_REG_FIELD(0x1132, MDIO_MMD_VEND1, 0, 1),
	.domain_number		=
		NXP_C45_REG_FIELD(0x114E, MDIO_MMD_VEND1, 0, 8),
	.msg_type		=
		NXP_C45_REG_FIELD(0x114E, MDIO_MMD_VEND1, 8, 4),
	.sequence_id		=
		NXP_C45_REG_FIELD(0x114F, MDIO_MMD_VEND1, 0, 16),
	.sec_1_0		=
		NXP_C45_REG_FIELD(0x1151, MDIO_MMD_VEND1, 14, 2),
	.sec_4_2		=
		NXP_C45_REG_FIELD(0x114E, MDIO_MMD_VEND1, 12, 3),
	.nsec_15_0		=
		NXP_C45_REG_FIELD(0x1150, MDIO_MMD_VEND1, 0, 16),
	.nsec_29_16		=
		NXP_C45_REG_FIELD(0x1151, MDIO_MMD_VEND1, 0, 14),
	.vend1_ext_trg_data_0	= 0x1121,
	.vend1_ext_trg_data_1	= 0x1122,
	.vend1_ext_trg_data_2	= 0x1123,
	.vend1_ext_trg_data_3	= 0x1124,
	.vend1_ext_trg_ctrl	= 0x1126,
	.cable_test		= 0x8330,
	.cable_test_valid	=
		NXP_C45_REG_FIELD(0x8330, MDIO_MMD_VEND1, 13, 1),
	.cable_test_result	=
		NXP_C45_REG_FIELD(0x8330, MDIO_MMD_VEND1, 0, 3),
};

static const struct nxp_c45_phy_data tja1103_phy_data = {
	.regmap = &tja1103_regmap,
	.stats = tja1103_hw_stats,
	.n_stats = ARRAY_SIZE(tja1103_hw_stats),
	.ptp_clk_period = PTP_CLK_PERIOD_100BT1,
	.ext_ts_both_edges = false,
	.ack_ptp_irq = false,
	.counters_enable = tja1103_counters_enable,
	.get_egressts = nxp_c45_get_hwtxts,
	.get_extts = nxp_c45_get_extts,
	.ptp_init = tja1103_ptp_init,
	.ptp_enable = tja1103_ptp_enable,
	.nmi_handler = tja1103_nmi_handler,
};

static void tja1120_counters_enable(struct phy_device *phydev)
{
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_SYMBOL_ERROR_CNT_XTD,
			 EXTENDED_CNT_EN);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_MONITOR_STATUS,
			 MONITOR_RESET);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_MONITOR_CONFIG,
			 ALL_FRAMES_CNT_EN | LOST_FRAMES_CNT_EN);
}

static void tja1120_ptp_init(struct phy_device *phydev)
{
	phy_write_mmd(phydev, MDIO_MMD_VEND1, TJA1120_RX_TS_INSRT_CTRL,
		      TJA1120_RX_TS_INSRT_EN | TJA1120_TS_INSRT_MODE);
	phy_write_mmd(phydev, MDIO_MMD_VEND1, TJA1120_VEND1_EXT_TS_MODE,
		      TJA1120_TS_INSRT_MODE);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_DEVICE_CONFIG,
			 PTP_ENABLE);
}

static void tja1120_ptp_enable(struct phy_device *phydev, bool enable)
{
	if (enable)
		phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
				 VEND1_PORT_FUNC_ENABLES,
				 PTP_ENABLE);
	else
		phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
				   VEND1_PORT_FUNC_ENABLES,
				   PTP_ENABLE);
}

static void tja1120_nmi_handler(struct phy_device *phydev,
				irqreturn_t *irq_status)
{
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1,
			   TJA1120_GLOBAL_INFRA_IRQ_STATUS);
	if (ret & TJA1120_DEV_BOOT_DONE) {
		phy_write_mmd(phydev, MDIO_MMD_VEND1,
			      TJA1120_GLOBAL_INFRA_IRQ_ACK,
			      TJA1120_DEV_BOOT_DONE);
		*irq_status = IRQ_HANDLED;
	}
}

static const struct nxp_c45_regmap tja1120_regmap = {
	.vend1_ptp_clk_period	= 0x1020,
	.vend1_event_msg_filt	= 0x9010,
	.pps_enable		=
		NXP_C45_REG_FIELD(0x1006, MDIO_MMD_VEND1, 4, 1),
	.pps_polarity		=
		NXP_C45_REG_FIELD(0x1006, MDIO_MMD_VEND1, 5, 1),
	.ltc_lock_ctrl		=
		NXP_C45_REG_FIELD(0x1006, MDIO_MMD_VEND1, 2, 1),
	.ltc_read		=
		NXP_C45_REG_FIELD(0x1000, MDIO_MMD_VEND1, 1, 1),
	.ltc_write		=
		NXP_C45_REG_FIELD(0x1000, MDIO_MMD_VEND1, 2, 1),
	.vend1_ltc_wr_nsec_0	= 0x1040,
	.vend1_ltc_wr_nsec_1	= 0x1041,
	.vend1_ltc_wr_sec_0	= 0x1042,
	.vend1_ltc_wr_sec_1	= 0x1043,
	.vend1_ltc_rd_nsec_0	= 0x1048,
	.vend1_ltc_rd_nsec_1	= 0x1049,
	.vend1_ltc_rd_sec_0	= 0x104A,
	.vend1_ltc_rd_sec_1	= 0x104B,
	.vend1_rate_adj_subns_0	= 0x1030,
	.vend1_rate_adj_subns_1	= 0x1031,
	.irq_egr_ts_en		=
		NXP_C45_REG_FIELD(0x900A, MDIO_MMD_VEND1, 1, 1),
	.irq_egr_ts_status	=
		NXP_C45_REG_FIELD(0x900C, MDIO_MMD_VEND1, 1, 1),
	.domain_number		=
		NXP_C45_REG_FIELD(0x9061, MDIO_MMD_VEND1, 8, 8),
	.msg_type		=
		NXP_C45_REG_FIELD(0x9061, MDIO_MMD_VEND1, 4, 4),
	.sequence_id		=
		NXP_C45_REG_FIELD(0x9062, MDIO_MMD_VEND1, 0, 16),
	.sec_1_0		=
		NXP_C45_REG_FIELD(0x9065, MDIO_MMD_VEND1, 0, 2),
	.sec_4_2		=
		NXP_C45_REG_FIELD(0x9065, MDIO_MMD_VEND1, 2, 3),
	.nsec_15_0		=
		NXP_C45_REG_FIELD(0x9063, MDIO_MMD_VEND1, 0, 16),
	.nsec_29_16		=
		NXP_C45_REG_FIELD(0x9064, MDIO_MMD_VEND1, 0, 14),
	.vend1_ext_trg_data_0	= 0x1071,
	.vend1_ext_trg_data_1	= 0x1072,
	.vend1_ext_trg_data_2	= 0x1073,
	.vend1_ext_trg_data_3	= 0x1074,
	.vend1_ext_trg_ctrl	= 0x1075,
	.cable_test		= 0x8360,
	.cable_test_valid	=
		NXP_C45_REG_FIELD(0x8361, MDIO_MMD_VEND1, 15, 1),
	.cable_test_result	=
		NXP_C45_REG_FIELD(0x8361, MDIO_MMD_VEND1, 0, 3),
};

static const struct nxp_c45_phy_data tja1120_phy_data = {
	.regmap = &tja1120_regmap,
	.stats = tja1120_hw_stats,
	.n_stats = ARRAY_SIZE(tja1120_hw_stats),
	.ptp_clk_period = PTP_CLK_PERIOD_1000BT1,
	.ext_ts_both_edges = true,
	.ack_ptp_irq = true,
	.counters_enable = tja1120_counters_enable,
	.get_egressts = tja1120_get_hwtxts,
	.get_extts = tja1120_get_extts,
	.ptp_init = tja1120_ptp_init,
	.ptp_enable = tja1120_ptp_enable,
	.nmi_handler = tja1120_nmi_handler,
};

static struct phy_driver nxp_c45_driver[] = {
	{
		PHY_ID_MATCH_MODEL(PHY_ID_TJA_1103),
		.name			= "NXP C45 TJA1103",
		.get_features		= nxp_c45_get_features,
		.driver_data		= &tja1103_phy_data,
		.probe			= nxp_c45_probe,
		.soft_reset		= nxp_c45_soft_reset,
		.config_aneg		= genphy_c45_config_aneg,
		.config_init		= nxp_c45_config_init,
		.config_intr		= tja1103_config_intr,
		.handle_interrupt	= nxp_c45_handle_interrupt,
		.read_status		= genphy_c45_read_status,
		.suspend		= genphy_c45_pma_suspend,
		.resume			= genphy_c45_pma_resume,
		.get_sset_count		= nxp_c45_get_sset_count,
		.get_strings		= nxp_c45_get_strings,
		.get_stats		= nxp_c45_get_stats,
		.cable_test_start	= nxp_c45_cable_test_start,
		.cable_test_get_status	= nxp_c45_cable_test_get_status,
		.set_loopback		= genphy_c45_loopback,
		.get_sqi		= nxp_c45_get_sqi,
		.get_sqi_max		= nxp_c45_get_sqi_max,
		.remove			= nxp_c45_remove,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_TJA_1120),
		.name			= "NXP C45 TJA1120",
		.get_features		= nxp_c45_get_features,
		.driver_data		= &tja1120_phy_data,
		.probe			= nxp_c45_probe,
		.soft_reset		= nxp_c45_soft_reset,
		.config_aneg		= genphy_c45_config_aneg,
		.config_init		= nxp_c45_config_init,
		.config_intr		= tja1120_config_intr,
		.handle_interrupt	= nxp_c45_handle_interrupt,
		.read_status		= genphy_c45_read_status,
		.link_change_notify	= tja1120_link_change_notify,
		.suspend		= genphy_c45_pma_suspend,
		.resume			= genphy_c45_pma_resume,
		.get_sset_count		= nxp_c45_get_sset_count,
		.get_strings		= nxp_c45_get_strings,
		.get_stats		= nxp_c45_get_stats,
		.cable_test_start	= nxp_c45_cable_test_start,
		.cable_test_get_status	= nxp_c45_cable_test_get_status,
		.set_loopback		= genphy_c45_loopback,
		.get_sqi		= nxp_c45_get_sqi,
		.get_sqi_max		= nxp_c45_get_sqi_max,
		.remove			= nxp_c45_remove,
	},
};

module_phy_driver(nxp_c45_driver);

static struct mdio_device_id __maybe_unused nxp_c45_tbl[] = {
	{ PHY_ID_MATCH_MODEL(PHY_ID_TJA_1103) },
	{ PHY_ID_MATCH_MODEL(PHY_ID_TJA_1120) },
	{ /*sentinel*/ },
};

MODULE_DEVICE_TABLE(mdio, nxp_c45_tbl);

MODULE_AUTHOR("Radu Pirea <radu-nicolae.pirea@oss.nxp.com>");
MODULE_DESCRIPTION("NXP C45 PHY driver");
MODULE_LICENSE("GPL v2");
