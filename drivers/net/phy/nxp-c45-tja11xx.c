// SPDX-License-Identifier: GPL-2.0
/* NXP C45 PHY driver
 * Copyright (C) 2021 NXP
 * Author: Radu Pirea <radu-nicolae.pirea@oss.nxp.com>
 */

#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/processor.h>
#include <linux/property.h>
#include <linux/ptp_classify.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/net_tstamp.h>

#define PHY_ID_TJA_1103			0x001BB010

#define PMAPMD_B100T1_PMAPMD_CTL	0x0834
#define B100T1_PMAPMD_CONFIG_EN		BIT(15)
#define B100T1_PMAPMD_MASTER		BIT(14)
#define MASTER_MODE			(B100T1_PMAPMD_CONFIG_EN | \
					 B100T1_PMAPMD_MASTER)
#define SLAVE_MODE			(B100T1_PMAPMD_CONFIG_EN)

#define VEND1_DEVICE_CONTROL		0x0040
#define DEVICE_CONTROL_RESET		BIT(15)
#define DEVICE_CONTROL_CONFIG_GLOBAL_EN	BIT(14)
#define DEVICE_CONTROL_CONFIG_ALL_EN	BIT(13)

#define VEND1_PHY_IRQ_ACK		0x80A0
#define VEND1_PHY_IRQ_EN		0x80A1
#define VEND1_PHY_IRQ_STATUS		0x80A2
#define PHY_IRQ_LINK_EVENT		BIT(1)

#define VEND1_PHY_CONTROL		0x8100
#define PHY_CONFIG_EN			BIT(14)
#define PHY_START_OP			BIT(0)

#define VEND1_PHY_CONFIG		0x8108
#define PHY_CONFIG_AUTO			BIT(0)

#define VEND1_SIGNAL_QUALITY		0x8320
#define SQI_VALID			BIT(14)
#define SQI_MASK			GENMASK(2, 0)
#define MAX_SQI				SQI_MASK

#define VEND1_CABLE_TEST		0x8330
#define CABLE_TEST_ENABLE		BIT(15)
#define CABLE_TEST_START		BIT(14)
#define CABLE_TEST_VALID		BIT(13)
#define CABLE_TEST_OK			0x00
#define CABLE_TEST_SHORTED		0x01
#define CABLE_TEST_OPEN			0x02
#define CABLE_TEST_UNKNOWN		0x07

#define VEND1_PORT_CONTROL		0x8040
#define PORT_CONTROL_EN			BIT(14)

#define VEND1_PORT_ABILITIES		0x8046
#define PTP_ABILITY			BIT(3)

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

#define VEND1_SYMBOL_ERROR_COUNTER	0x8350
#define VEND1_LINK_DROP_COUNTER		0x8352
#define VEND1_LINK_LOSSES_AND_FAILURES	0x8353
#define VEND1_R_GOOD_FRAME_CNT		0xA950
#define VEND1_R_BAD_FRAME_CNT		0xA952
#define VEND1_R_RXER_FRAME_CNT		0xA954
#define VEND1_RX_PREAMBLE_COUNT		0xAFCE
#define VEND1_TX_PREAMBLE_COUNT		0xAFCF
#define VEND1_RX_IPG_LENGTH		0xAFD0
#define VEND1_TX_IPG_LENGTH		0xAFD1
#define COUNTER_EN			BIT(15)

#define VEND1_PTP_CONFIG		0x1102
#define EXT_TRG_EDGE			BIT(1)
#define PPS_OUT_POL			BIT(2)
#define PPS_OUT_EN			BIT(3)

#define VEND1_LTC_LOAD_CTRL		0x1105
#define READ_LTC			BIT(2)
#define LOAD_LTC			BIT(0)

#define VEND1_LTC_WR_NSEC_0		0x1106
#define VEND1_LTC_WR_NSEC_1		0x1107
#define VEND1_LTC_WR_SEC_0		0x1108
#define VEND1_LTC_WR_SEC_1		0x1109

#define VEND1_LTC_RD_NSEC_0		0x110A
#define VEND1_LTC_RD_NSEC_1		0x110B
#define VEND1_LTC_RD_SEC_0		0x110C
#define VEND1_LTC_RD_SEC_1		0x110D

#define VEND1_RATE_ADJ_SUBNS_0		0x110F
#define VEND1_RATE_ADJ_SUBNS_1		0x1110
#define CLK_RATE_ADJ_LD			BIT(15)
#define CLK_RATE_ADJ_DIR		BIT(14)

#define VEND1_HW_LTC_LOCK_CTRL		0x1115
#define HW_LTC_LOCK_EN			BIT(0)

#define VEND1_PTP_IRQ_EN		0x1131
#define VEND1_PTP_IRQ_STATUS		0x1132
#define PTP_IRQ_EGR_TS			BIT(0)

#define VEND1_RX_TS_INSRT_CTRL		0x114D
#define RX_TS_INSRT_MODE2		0x02

#define VEND1_EGR_RING_DATA_0		0x114E
#define VEND1_EGR_RING_DATA_1_SEQ_ID	0x114F
#define VEND1_EGR_RING_DATA_2_NSEC_15_0	0x1150
#define VEND1_EGR_RING_DATA_3		0x1151
#define VEND1_EGR_RING_CTRL		0x1154

#define VEND1_EXT_TRG_TS_DATA_0		0x1121
#define VEND1_EXT_TRG_TS_DATA_1		0x1122
#define VEND1_EXT_TRG_TS_DATA_2		0x1123
#define VEND1_EXT_TRG_TS_DATA_3		0x1124
#define VEND1_EXT_TRG_TS_DATA_4		0x1125
#define VEND1_EXT_TRG_TS_CTRL		0x1126

#define RING_DATA_0_DOMAIN_NUMBER	GENMASK(7, 0)
#define RING_DATA_0_MSG_TYPE		GENMASK(11, 8)
#define RING_DATA_0_SEC_4_2		GENMASK(14, 2)
#define RING_DATA_0_TS_VALID		BIT(15)

#define RING_DATA_3_NSEC_29_16		GENMASK(13, 0)
#define RING_DATA_3_SEC_1_0		GENMASK(15, 14)
#define RING_DATA_5_SEC_16_5		GENMASK(15, 4)
#define RING_DONE			BIT(0)

#define TS_SEC_MASK			GENMASK(1, 0)

#define VEND1_PORT_FUNC_ENABLES		0x8048
#define PTP_ENABLE			BIT(3)

#define VEND1_PORT_PTP_CONTROL		0x9000
#define PORT_PTP_CONTROL_BYPASS		BIT(11)

#define VEND1_PTP_CLK_PERIOD		0x1104
#define PTP_CLK_PERIOD_100BT1		15ULL

#define VEND1_EVENT_MSG_FILT		0x1148
#define EVENT_MSG_FILT_ALL		0x0F
#define EVENT_MSG_FILT_NONE		0x00

#define VEND1_TX_PIPE_DLY_NS		0x1149
#define VEND1_TX_PIPEDLY_SUBNS		0x114A
#define VEND1_RX_PIPE_DLY_NS		0x114B
#define VEND1_RX_PIPEDLY_SUBNS		0x114C

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

#define PPM_TO_SUBNS_INC(ppb)	div_u64(GENMASK(31, 0) * (ppb) * \
					PTP_CLK_PERIOD_100BT1, NSEC_PER_SEC)

#define NXP_C45_SKB_CB(skb)	((struct nxp_c45_skb_cb *)(skb)->cb)

struct nxp_c45_skb_cb {
	struct ptp_header *header;
	unsigned int type;
};

struct nxp_c45_hwts {
	u32	nsec;
	u32	sec;
	u8	domain_number;
	u16	sequence_id;
	u8	msg_type;
};

struct nxp_c45_phy {
	struct phy_device *phydev;
	struct mii_timestamper mii_ts;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info caps;
	struct sk_buff_head tx_queue;
	struct sk_buff_head rx_queue;
	/* used to access the PTP registers atomic */
	struct mutex ptp_lock;
	int hwts_tx;
	int hwts_rx;
	u32 tx_delay;
	u32 rx_delay;
	struct timespec64 extts_ts;
	int extts_index;
	bool extts;
};

struct nxp_c45_phy_stats {
	const char	*name;
	u8		mmd;
	u16		reg;
	u8		off;
	u16		mask;
};

static bool nxp_c45_poll_txts(struct phy_device *phydev)
{
	return phydev->irq <= 0;
}

static int _nxp_c45_ptp_gettimex64(struct ptp_clock_info *ptp,
				   struct timespec64 *ts,
				   struct ptp_system_timestamp *sts)
{
	struct nxp_c45_phy *priv = container_of(ptp, struct nxp_c45_phy, caps);

	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1, VEND1_LTC_LOAD_CTRL,
		      READ_LTC);
	ts->tv_nsec = phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				   VEND1_LTC_RD_NSEC_0);
	ts->tv_nsec |= phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				    VEND1_LTC_RD_NSEC_1) << 16;
	ts->tv_sec = phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				  VEND1_LTC_RD_SEC_0);
	ts->tv_sec |= phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				   VEND1_LTC_RD_SEC_1) << 16;

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

	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1, VEND1_LTC_WR_NSEC_0,
		      ts->tv_nsec);
	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1, VEND1_LTC_WR_NSEC_1,
		      ts->tv_nsec >> 16);
	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1, VEND1_LTC_WR_SEC_0,
		      ts->tv_sec);
	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1, VEND1_LTC_WR_SEC_1,
		      ts->tv_sec >> 16);
	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1, VEND1_LTC_LOAD_CTRL,
		      LOAD_LTC);

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
	s32 ppb = scaled_ppm_to_ppb(scaled_ppm);
	u64 subns_inc_val;
	bool inc;

	mutex_lock(&priv->ptp_lock);
	inc = ppb >= 0;
	ppb = abs(ppb);

	subns_inc_val = PPM_TO_SUBNS_INC(ppb);

	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1, VEND1_RATE_ADJ_SUBNS_0,
		      subns_inc_val);
	subns_inc_val >>= 16;
	subns_inc_val |= CLK_RATE_ADJ_LD;
	if (inc)
		subns_inc_val |= CLK_RATE_ADJ_DIR;

	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1, VEND1_RATE_ADJ_SUBNS_1,
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

static void nxp_c45_get_extts(struct nxp_c45_phy *priv,
			      struct timespec64 *extts)
{
	extts->tv_nsec = phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				      VEND1_EXT_TRG_TS_DATA_0);
	extts->tv_nsec |= phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				       VEND1_EXT_TRG_TS_DATA_1) << 16;
	extts->tv_sec = phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				     VEND1_EXT_TRG_TS_DATA_2);
	extts->tv_sec |= phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				      VEND1_EXT_TRG_TS_DATA_3) << 16;
	phy_write_mmd(priv->phydev, MDIO_MMD_VEND1, VEND1_EXT_TRG_TS_CTRL,
		      RING_DONE);
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

	hwts->domain_number = reg;
	hwts->msg_type = (reg & RING_DATA_0_MSG_TYPE) >> 8;
	hwts->sec = (reg & RING_DATA_0_SEC_4_2) >> 10;
	hwts->sequence_id = phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
					 VEND1_EGR_RING_DATA_1_SEQ_ID);
	hwts->nsec = phy_read_mmd(priv->phydev, MDIO_MMD_VEND1,
				  VEND1_EGR_RING_DATA_2_NSEC_15_0);
	reg = phy_read_mmd(priv->phydev, MDIO_MMD_VEND1, VEND1_EGR_RING_DATA_3);
	hwts->nsec |= (reg & RING_DATA_3_NSEC_29_16) << 16;
	hwts->sec |= (reg & RING_DATA_3_SEC_1_0) >> 14;

nxp_c45_get_hwtxts_out:
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
	bool poll_txts = nxp_c45_poll_txts(priv->phydev);
	struct skb_shared_hwtstamps *shhwtstamps_rx;
	struct ptp_clock_event event;
	struct nxp_c45_hwts hwts;
	bool reschedule = false;
	struct timespec64 ts;
	struct sk_buff *skb;
	bool txts_valid;
	u32 ts_raw;

	while (!skb_queue_empty_lockless(&priv->tx_queue) && poll_txts) {
		txts_valid = nxp_c45_get_hwtxts(priv, &hwts);
		if (unlikely(!txts_valid)) {
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
		nxp_c45_get_extts(priv, &ts);
		if (timespec64_compare(&ts, &priv->extts_ts) != 0) {
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
	struct phy_device *phydev = priv->phydev;
	int pin;

	if (perout->flags & ~PTP_PEROUT_PHASE)
		return -EOPNOTSUPP;

	pin = ptp_find_pin(priv->ptp_clock, PTP_PF_PEROUT, perout->index);
	if (pin < 0)
		return pin;

	if (!on) {
		phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_CONFIG,
				   PPS_OUT_EN);
		phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_CONFIG,
				   PPS_OUT_POL);

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
			phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
					   VEND1_PTP_CONFIG, PPS_OUT_POL);
		else
			phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
					 VEND1_PTP_CONFIG, PPS_OUT_POL);
	}

	nxp_c45_gpio_config(priv, pin, GPIO_PPS_OUT_CFG);

	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_CONFIG, PPS_OUT_EN);

	return 0;
}

static int nxp_c45_extts_enable(struct nxp_c45_phy *priv,
				struct ptp_extts_request *extts, int on)
{
	int pin;

	if (extts->flags & ~(PTP_ENABLE_FEATURE |
			      PTP_RISING_EDGE |
			      PTP_FALLING_EDGE |
			      PTP_STRICT_FLAGS))
		return -EOPNOTSUPP;

	/* Sampling on both edges is not supported */
	if ((extts->flags & PTP_RISING_EDGE) &&
	    (extts->flags & PTP_FALLING_EDGE))
		return -EOPNOTSUPP;

	pin = ptp_find_pin(priv->ptp_clock, PTP_PF_EXTTS, extts->index);
	if (pin < 0)
		return pin;

	if (!on) {
		nxp_c45_gpio_config(priv, pin, GPIO_DISABLE);
		priv->extts = false;

		return 0;
	}

	if (extts->flags & PTP_RISING_EDGE)
		phy_clear_bits_mmd(priv->phydev, MDIO_MMD_VEND1,
				   VEND1_PTP_CONFIG, EXT_TRG_EDGE);

	if (extts->flags & PTP_FALLING_EDGE)
		phy_set_bits_mmd(priv->phydev, MDIO_MMD_VEND1,
				 VEND1_PTP_CONFIG, EXT_TRG_EDGE);

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
			    struct ifreq *ifreq)
{
	struct nxp_c45_phy *priv = container_of(mii_ts, struct nxp_c45_phy,
						mii_ts);
	struct phy_device *phydev = priv->phydev;
	struct hwtstamp_config cfg;

	if (copy_from_user(&cfg, ifreq->ifr_data, sizeof(cfg)))
		return -EFAULT;

	if (cfg.tx_type < 0 || cfg.tx_type > HWTSTAMP_TX_ON)
		return -ERANGE;

	priv->hwts_tx = cfg.tx_type;

	switch (cfg.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		priv->hwts_rx = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		priv->hwts_rx = 1;
		cfg.rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		break;
	default:
		return -ERANGE;
	}

	if (priv->hwts_rx || priv->hwts_tx) {
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_EVENT_MSG_FILT,
			      EVENT_MSG_FILT_ALL);
		phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
				   VEND1_PORT_PTP_CONTROL,
				   PORT_PTP_CONTROL_BYPASS);
	} else {
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_EVENT_MSG_FILT,
			      EVENT_MSG_FILT_NONE);
		phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PORT_PTP_CONTROL,
				 PORT_PTP_CONTROL_BYPASS);
	}

	if (nxp_c45_poll_txts(priv->phydev))
		goto nxp_c45_no_ptp_irq;

	if (priv->hwts_tx)
		phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
				 VEND1_PTP_IRQ_EN, PTP_IRQ_EGR_TS);
	else
		phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
				   VEND1_PTP_IRQ_EN, PTP_IRQ_EGR_TS);

nxp_c45_no_ptp_irq:
	return copy_to_user(ifreq->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

static int nxp_c45_ts_info(struct mii_timestamper *mii_ts,
			   struct ethtool_ts_info *ts_info)
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

static const struct nxp_c45_phy_stats nxp_c45_hw_stats[] = {
	{ "phy_symbol_error_cnt", MDIO_MMD_VEND1,
		VEND1_SYMBOL_ERROR_COUNTER, 0, GENMASK(15, 0) },
	{ "phy_link_status_drop_cnt", MDIO_MMD_VEND1,
		VEND1_LINK_DROP_COUNTER, 8, GENMASK(13, 8) },
	{ "phy_link_availability_drop_cnt", MDIO_MMD_VEND1,
		VEND1_LINK_DROP_COUNTER, 0, GENMASK(5, 0) },
	{ "phy_link_loss_cnt", MDIO_MMD_VEND1,
		VEND1_LINK_LOSSES_AND_FAILURES, 10, GENMASK(15, 10) },
	{ "phy_link_failure_cnt", MDIO_MMD_VEND1,
		VEND1_LINK_LOSSES_AND_FAILURES, 0, GENMASK(9, 0) },
	{ "r_good_frame_cnt", MDIO_MMD_VEND1,
		VEND1_R_GOOD_FRAME_CNT, 0, GENMASK(15, 0) },
	{ "r_bad_frame_cnt", MDIO_MMD_VEND1,
		VEND1_R_BAD_FRAME_CNT, 0, GENMASK(15, 0) },
	{ "r_rxer_frame_cnt", MDIO_MMD_VEND1,
		VEND1_R_RXER_FRAME_CNT, 0, GENMASK(15, 0) },
	{ "rx_preamble_count", MDIO_MMD_VEND1,
		VEND1_RX_PREAMBLE_COUNT, 0, GENMASK(5, 0) },
	{ "tx_preamble_count", MDIO_MMD_VEND1,
		VEND1_TX_PREAMBLE_COUNT, 0, GENMASK(5, 0) },
	{ "rx_ipg_length", MDIO_MMD_VEND1,
		VEND1_RX_IPG_LENGTH, 0, GENMASK(8, 0) },
	{ "tx_ipg_length", MDIO_MMD_VEND1,
		VEND1_TX_IPG_LENGTH, 0, GENMASK(8, 0) },
};

static int nxp_c45_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(nxp_c45_hw_stats);
}

static void nxp_c45_get_strings(struct phy_device *phydev, u8 *data)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(nxp_c45_hw_stats); i++) {
		strncpy(data + i * ETH_GSTRING_LEN,
			nxp_c45_hw_stats[i].name, ETH_GSTRING_LEN);
	}
}

static void nxp_c45_get_stats(struct phy_device *phydev,
			      struct ethtool_stats *stats, u64 *data)
{
	size_t i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(nxp_c45_hw_stats); i++) {
		ret = phy_read_mmd(phydev, nxp_c45_hw_stats[i].mmd,
				   nxp_c45_hw_stats[i].reg);
		if (ret < 0) {
			data[i] = U64_MAX;
		} else {
			data[i] = ret & nxp_c45_hw_stats[i].mask;
			data[i] >>= nxp_c45_hw_stats[i].off;
		}
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
	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		return phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
					VEND1_PHY_IRQ_EN, PHY_IRQ_LINK_EVENT);
	else
		return phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
					  VEND1_PHY_IRQ_EN, PHY_IRQ_LINK_EVENT);
}

static irqreturn_t nxp_c45_handle_interrupt(struct phy_device *phydev)
{
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

	/* There is no need for ACK.
	 * The irq signal will be asserted until the EGR TS FIFO will be
	 * emptied.
	 */
	irq = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_IRQ_STATUS);
	if (irq & PTP_IRQ_EGR_TS) {
		while (nxp_c45_get_hwtxts(priv, &hwts))
			nxp_c45_process_txts(priv, &hwts);

		ret = IRQ_HANDLED;
	}

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
	return phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_CABLE_TEST,
			     CABLE_TEST_ENABLE | CABLE_TEST_START);
}

static int nxp_c45_cable_test_get_status(struct phy_device *phydev,
					 bool *finished)
{
	int ret;
	u8 cable_test_result;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CABLE_TEST);
	if (!(ret & CABLE_TEST_VALID)) {
		*finished = false;
		return 0;
	}

	*finished = true;
	cable_test_result = ret & GENMASK(2, 0);

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

	phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_CABLE_TEST,
			   CABLE_TEST_ENABLE);

	return nxp_c45_start_op(phydev);
}

static int nxp_c45_setup_master_slave(struct phy_device *phydev)
{
	switch (phydev->master_slave_set) {
	case MASTER_SLAVE_CFG_MASTER_FORCE:
	case MASTER_SLAVE_CFG_MASTER_PREFERRED:
		phy_write_mmd(phydev, MDIO_MMD_PMAPMD, PMAPMD_B100T1_PMAPMD_CTL,
			      MASTER_MODE);
		break;
	case MASTER_SLAVE_CFG_SLAVE_PREFERRED:
	case MASTER_SLAVE_CFG_SLAVE_FORCE:
		phy_write_mmd(phydev, MDIO_MMD_PMAPMD, PMAPMD_B100T1_PMAPMD_CTL,
			      SLAVE_MODE);
		break;
	case MASTER_SLAVE_CFG_UNKNOWN:
	case MASTER_SLAVE_CFG_UNSUPPORTED:
		return 0;
	default:
		phydev_warn(phydev, "Unsupported Master/Slave mode\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int nxp_c45_read_master_slave(struct phy_device *phydev)
{
	int reg;

	phydev->master_slave_get = MASTER_SLAVE_CFG_UNKNOWN;
	phydev->master_slave_state = MASTER_SLAVE_STATE_UNKNOWN;

	reg = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, PMAPMD_B100T1_PMAPMD_CTL);
	if (reg < 0)
		return reg;

	if (reg & B100T1_PMAPMD_MASTER) {
		phydev->master_slave_get = MASTER_SLAVE_CFG_MASTER_FORCE;
		phydev->master_slave_state = MASTER_SLAVE_STATE_MASTER;
	} else {
		phydev->master_slave_get = MASTER_SLAVE_CFG_SLAVE_FORCE;
		phydev->master_slave_state = MASTER_SLAVE_STATE_SLAVE;
	}

	return 0;
}

static int nxp_c45_config_aneg(struct phy_device *phydev)
{
	return nxp_c45_setup_master_slave(phydev);
}

static int nxp_c45_read_status(struct phy_device *phydev)
{
	int ret;

	ret = genphy_c45_read_status(phydev);
	if (ret)
		return ret;

	ret = nxp_c45_read_master_slave(phydev);
	if (ret)
		return ret;

	return 0;
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
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_MII_BASIC_CONFIG,
			      MII_BASIC_CONFIG_RMII);
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

	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_LINK_DROP_COUNTER,
			 COUNTER_EN);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_RX_PREAMBLE_COUNT,
			 COUNTER_EN);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_TX_PREAMBLE_COUNT,
			 COUNTER_EN);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_RX_IPG_LENGTH,
			 COUNTER_EN);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_TX_IPG_LENGTH,
			 COUNTER_EN);

	ret = nxp_c45_set_phy_mode(phydev);
	if (ret)
		return ret;

	phydev->autoneg = AUTONEG_DISABLE;

	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_CLK_PERIOD,
		      PTP_CLK_PERIOD_100BT1);
	phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_HW_LTC_LOCK_CTRL,
			   HW_LTC_LOCK_EN);
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_RX_TS_INSRT_CTRL,
		      RX_TS_INSRT_MODE2);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PORT_FUNC_ENABLES,
			 PTP_ENABLE);

	return nxp_c45_start_op(phydev);
}

static int nxp_c45_probe(struct phy_device *phydev)
{
	struct nxp_c45_phy *priv;
	int ptp_ability;
	int ret = 0;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	skb_queue_head_init(&priv->tx_queue);
	skb_queue_head_init(&priv->rx_queue);

	priv->phydev = phydev;

	phydev->priv = priv;

	mutex_init(&priv->ptp_lock);

	ptp_ability = phy_read_mmd(phydev, MDIO_MMD_VEND1,
				   VEND1_PORT_ABILITIES);
	ptp_ability = !!(ptp_ability & PTP_ABILITY);
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
	} else {
		phydev_dbg(phydev, "PTP support not enabled even if the phy supports it");
	}

no_ptp_support:

	return ret;
}

static struct phy_driver nxp_c45_driver[] = {
	{
		PHY_ID_MATCH_MODEL(PHY_ID_TJA_1103),
		.name			= "NXP C45 TJA1103",
		.features		= PHY_BASIC_T1_FEATURES,
		.probe			= nxp_c45_probe,
		.soft_reset		= nxp_c45_soft_reset,
		.config_aneg		= nxp_c45_config_aneg,
		.config_init		= nxp_c45_config_init,
		.config_intr		= nxp_c45_config_intr,
		.handle_interrupt	= nxp_c45_handle_interrupt,
		.read_status		= nxp_c45_read_status,
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
	},
};

module_phy_driver(nxp_c45_driver);

static struct mdio_device_id __maybe_unused nxp_c45_tbl[] = {
	{ PHY_ID_MATCH_MODEL(PHY_ID_TJA_1103) },
	{ /*sentinel*/ },
};

MODULE_DEVICE_TABLE(mdio, nxp_c45_tbl);

MODULE_AUTHOR("Radu Pirea <radu-nicolae.pirea@oss.nxp.com>");
MODULE_DESCRIPTION("NXP C45 PHY driver");
MODULE_LICENSE("GPL v2");
