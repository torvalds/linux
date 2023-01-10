// SPDX-License-Identifier: GPL-2.0
/* Microchip KSZ PTP Implementation
 *
 * Copyright (C) 2020 ARRI Lighting
 * Copyright (C) 2022 Microchip Technology Inc.
 */

#include <linux/dsa/ksz_common.h>
#include <linux/kernel.h>
#include <linux/ptp_classify.h>
#include <linux/ptp_clock_kernel.h>

#include "ksz_common.h"
#include "ksz_ptp.h"
#include "ksz_ptp_reg.h"

#define ptp_caps_to_data(d) container_of((d), struct ksz_ptp_data, caps)
#define ptp_data_to_ksz_dev(d) container_of((d), struct ksz_device, ptp_data)

/* Sub-nanoseconds-adj,max * sub-nanoseconds / 40ns * 1ns
 * = (2^30-1) * (2 ^ 32) / 40 ns * 1 ns = 6249999
 */
#define KSZ_MAX_DRIFT_CORR 6249999

#define KSZ_PTP_INC_NS 40ULL  /* HW clock is incremented every 40 ns (by 40) */
#define KSZ_PTP_SUBNS_BITS 32

static int ksz_ptp_enable_mode(struct ksz_device *dev)
{
	struct ksz_tagger_data *tagger_data = ksz_tagger_data(dev->ds);
	struct ksz_port *prt;
	struct dsa_port *dp;
	bool tag_en = false;

	dsa_switch_for_each_user_port(dp, dev->ds) {
		prt = &dev->ports[dp->index];
		if (prt->hwts_tx_en || prt->hwts_rx_en) {
			tag_en = true;
			break;
		}
	}

	tagger_data->hwtstamp_set_state(dev->ds, tag_en);

	return ksz_rmw16(dev, REG_PTP_MSG_CONF1, PTP_ENABLE,
			 tag_en ? PTP_ENABLE : 0);
}

/* The function is return back the capability of timestamping feature when
 * requested through ethtool -T <interface> utility
 */
int ksz_get_ts_info(struct dsa_switch *ds, int port, struct ethtool_ts_info *ts)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_ptp_data *ptp_data;

	ptp_data = &dev->ptp_data;

	if (!ptp_data->clock)
		return -ENODEV;

	ts->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
			      SOF_TIMESTAMPING_RX_HARDWARE |
			      SOF_TIMESTAMPING_RAW_HARDWARE;

	ts->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ONESTEP_P2P);

	ts->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			 BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
			 BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			 BIT(HWTSTAMP_FILTER_PTP_V2_EVENT);

	ts->phc_index = ptp_clock_index(ptp_data->clock);

	return 0;
}

int ksz_hwtstamp_get(struct dsa_switch *ds, int port, struct ifreq *ifr)
{
	struct ksz_device *dev = ds->priv;
	struct hwtstamp_config *config;
	struct ksz_port *prt;

	prt = &dev->ports[port];
	config = &prt->tstamp_config;

	return copy_to_user(ifr->ifr_data, config, sizeof(*config)) ?
		-EFAULT : 0;
}

static int ksz_set_hwtstamp_config(struct ksz_device *dev,
				   struct ksz_port *prt,
				   struct hwtstamp_config *config)
{
	if (config->flags)
		return -EINVAL;

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		prt->hwts_tx_en = false;
		break;
	case HWTSTAMP_TX_ONESTEP_P2P:
		prt->hwts_tx_en = true;
		break;
	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		prt->hwts_rx_en = false;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
		prt->hwts_rx_en = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		prt->hwts_rx_en = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		prt->hwts_rx_en = true;
		break;
	default:
		config->rx_filter = HWTSTAMP_FILTER_NONE;
		return -ERANGE;
	}

	return ksz_ptp_enable_mode(dev);
}

int ksz_hwtstamp_set(struct dsa_switch *ds, int port, struct ifreq *ifr)
{
	struct ksz_device *dev = ds->priv;
	struct hwtstamp_config config;
	struct ksz_port *prt;
	int ret;

	prt = &dev->ports[port];

	ret = copy_from_user(&config, ifr->ifr_data, sizeof(config));
	if (ret)
		return ret;

	ret = ksz_set_hwtstamp_config(dev, prt, &config);
	if (ret)
		return ret;

	memcpy(&prt->tstamp_config, &config, sizeof(config));

	return copy_to_user(ifr->ifr_data, &config, sizeof(config));
}

static int _ksz_ptp_gettime(struct ksz_device *dev, struct timespec64 *ts)
{
	u32 nanoseconds;
	u32 seconds;
	u8 phase;
	int ret;

	/* Copy current PTP clock into shadow registers and read */
	ret = ksz_rmw16(dev, REG_PTP_CLK_CTRL, PTP_READ_TIME, PTP_READ_TIME);
	if (ret)
		return ret;

	ret = ksz_read8(dev, REG_PTP_RTC_SUB_NANOSEC__2, &phase);
	if (ret)
		return ret;

	ret = ksz_read32(dev, REG_PTP_RTC_NANOSEC, &nanoseconds);
	if (ret)
		return ret;

	ret = ksz_read32(dev, REG_PTP_RTC_SEC, &seconds);
	if (ret)
		return ret;

	ts->tv_sec = seconds;
	ts->tv_nsec = nanoseconds + phase * 8;

	return 0;
}

static int ksz_ptp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct ksz_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct ksz_device *dev = ptp_data_to_ksz_dev(ptp_data);
	int ret;

	mutex_lock(&ptp_data->lock);
	ret = _ksz_ptp_gettime(dev, ts);
	mutex_unlock(&ptp_data->lock);

	return ret;
}

static int ksz_ptp_settime(struct ptp_clock_info *ptp,
			   const struct timespec64 *ts)
{
	struct ksz_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct ksz_device *dev = ptp_data_to_ksz_dev(ptp_data);
	int ret;

	mutex_lock(&ptp_data->lock);

	/* Write to shadow registers and Load PTP clock */
	ret = ksz_write16(dev, REG_PTP_RTC_SUB_NANOSEC__2, PTP_RTC_0NS);
	if (ret)
		goto unlock;

	ret = ksz_write32(dev, REG_PTP_RTC_NANOSEC, ts->tv_nsec);
	if (ret)
		goto unlock;

	ret = ksz_write32(dev, REG_PTP_RTC_SEC, ts->tv_sec);
	if (ret)
		goto unlock;

	ret = ksz_rmw16(dev, REG_PTP_CLK_CTRL, PTP_LOAD_TIME, PTP_LOAD_TIME);

unlock:
	mutex_unlock(&ptp_data->lock);

	return ret;
}

static int ksz_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct ksz_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct ksz_device *dev = ptp_data_to_ksz_dev(ptp_data);
	u64 base, adj;
	bool negative;
	u32 data32;
	int ret;

	mutex_lock(&ptp_data->lock);

	if (scaled_ppm) {
		base = KSZ_PTP_INC_NS << KSZ_PTP_SUBNS_BITS;
		negative = diff_by_scaled_ppm(base, scaled_ppm, &adj);

		data32 = (u32)adj;
		data32 &= PTP_SUBNANOSEC_M;
		if (!negative)
			data32 |= PTP_RATE_DIR;

		ret = ksz_write32(dev, REG_PTP_SUBNANOSEC_RATE, data32);
		if (ret)
			goto unlock;

		ret = ksz_rmw16(dev, REG_PTP_CLK_CTRL, PTP_CLK_ADJ_ENABLE,
				PTP_CLK_ADJ_ENABLE);
		if (ret)
			goto unlock;
	} else {
		ret = ksz_rmw16(dev, REG_PTP_CLK_CTRL, PTP_CLK_ADJ_ENABLE, 0);
		if (ret)
			goto unlock;
	}

unlock:
	mutex_unlock(&ptp_data->lock);
	return ret;
}

static int ksz_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct ksz_ptp_data *ptp_data = ptp_caps_to_data(ptp);
	struct ksz_device *dev = ptp_data_to_ksz_dev(ptp_data);
	s32 sec, nsec;
	u16 data16;
	int ret;

	mutex_lock(&ptp_data->lock);

	/* do not use ns_to_timespec64(),
	 * both sec and nsec are subtracted by hw
	 */
	sec = div_s64_rem(delta, NSEC_PER_SEC, &nsec);

	ret = ksz_write32(dev, REG_PTP_RTC_NANOSEC, abs(nsec));
	if (ret)
		goto unlock;

	ret = ksz_write32(dev, REG_PTP_RTC_SEC, abs(sec));
	if (ret)
		goto unlock;

	ret = ksz_read16(dev, REG_PTP_CLK_CTRL, &data16);
	if (ret)
		goto unlock;

	data16 |= PTP_STEP_ADJ;

	/* PTP_STEP_DIR -- 0: subtract, 1: add */
	if (delta < 0)
		data16 &= ~PTP_STEP_DIR;
	else
		data16 |= PTP_STEP_DIR;

	ret = ksz_write16(dev, REG_PTP_CLK_CTRL, data16);

unlock:
	mutex_unlock(&ptp_data->lock);
	return ret;
}

static int ksz_ptp_start_clock(struct ksz_device *dev)
{
	return ksz_rmw16(dev, REG_PTP_CLK_CTRL, PTP_CLK_ENABLE, PTP_CLK_ENABLE);
}

int ksz_ptp_clock_register(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_ptp_data *ptp_data;
	int ret;

	ptp_data = &dev->ptp_data;
	mutex_init(&ptp_data->lock);

	ptp_data->caps.owner		= THIS_MODULE;
	snprintf(ptp_data->caps.name, 16, "Microchip Clock");
	ptp_data->caps.max_adj		= KSZ_MAX_DRIFT_CORR;
	ptp_data->caps.gettime64	= ksz_ptp_gettime;
	ptp_data->caps.settime64	= ksz_ptp_settime;
	ptp_data->caps.adjfine		= ksz_ptp_adjfine;
	ptp_data->caps.adjtime		= ksz_ptp_adjtime;

	ret = ksz_ptp_start_clock(dev);
	if (ret)
		return ret;

	/* Currently only P2P mode is supported. When 802_1AS bit is set, it
	 * forwards all PTP packets to host port and none to other ports.
	 */
	ret = ksz_rmw16(dev, REG_PTP_MSG_CONF1, PTP_TC_P2P | PTP_802_1AS,
			PTP_TC_P2P | PTP_802_1AS);
	if (ret)
		return ret;

	ptp_data->clock = ptp_clock_register(&ptp_data->caps, dev->dev);
	if (IS_ERR_OR_NULL(ptp_data->clock))
		return PTR_ERR(ptp_data->clock);

	return 0;
}

void ksz_ptp_clock_unregister(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_ptp_data *ptp_data;

	ptp_data = &dev->ptp_data;

	if (ptp_data->clock)
		ptp_clock_unregister(ptp_data->clock);
}

MODULE_AUTHOR("Christian Eggers <ceggers@arri.de>");
MODULE_AUTHOR("Arun Ramadoss <arun.ramadoss@microchip.com>");
MODULE_DESCRIPTION("PTP support for KSZ switch");
MODULE_LICENSE("GPL");
