// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* Qualcomm Technologies, Inc. EMAC Ethernet Controller driver.
 */

#include <linux/phy.h>
#include <linux/net_tstamp.h>
#include <linux/of.h>
#include "emac_main.h"
#include "emac_hw.h"
#include "emac_ptp.h"

#define RTC_INC_FRAC_NS_BMSK	0x03ffffff
#define RTC_INC_FRAC_NS_SHFT	0
#define RTC_INC_NS_BMSK		0xfc000000
#define RTC_INC_NS_SHFT		26
#define RTC_NUM_FRAC_NS_PER_NS BIT(RTC_INC_NS_SHFT)

#define TS_TX_FIFO_SYNC_RST (TX_INDX_FIFO_SYNC_RST | TX_TS_FIFO_SYNC_RST)
#define TS_RX_FIFO_SYNC_RST (RX_TS_FIFO1_SYNC_RST  | RX_TS_FIFO2_SYNC_RST)
#define TS_FIFO_SYNC_RST    (TS_TX_FIFO_SYNC_RST | TS_RX_FIFO_SYNC_RST)

struct emac_tstamp_hw_delay {
	int phy_mode;
	u32 speed;
	u32 tx;
	u32 rx;
};

struct emac_ptp_frac_ns_adj {
	u32 ref_clk_rate;
	s32 adj_val;
};

static const struct emac_tstamp_hw_delay emac_ptp_hw_delay[] = {
	{ PHY_INTERFACE_MODE_SGMII, SPEED_1000, 16, 60 },
	{ PHY_INTERFACE_MODE_SGMII, SPEED_100, 280, 100 },
	{ PHY_INTERFACE_MODE_SGMII, SPEED_10, 2400, 400 },
	{ 0 }
};

static inline u32 get_rtc_ref_clkrate(struct emac_hw *hw)
{
	struct emac_adapter *adpt = emac_hw_get_adap(hw);

	return clk_get_rate(adpt->clk[EMAC_CLK_HIGH_SPEED].clk);
}

static inline bool is_valid_frac_ns_adj(s32 val)
{
	if (val >= RTC_NUM_FRAC_NS_PER_NS || (val <= -RTC_NUM_FRAC_NS_PER_NS))
		return false;

	return true;
}

static s32 get_frac_ns_adj_from_tbl(struct emac_hw *hw)
{
	const struct emac_ptp_frac_ns_adj *tbl = hw->frac_ns_adj_tbl;
	u32 clk = get_rtc_ref_clkrate(hw);
	s32 val = 0;
	int i;

	for (i = 0; tbl && i < hw->frac_ns_adj_tbl_sz; i++) {
		if (tbl[i].ref_clk_rate == clk) {
			if (is_valid_frac_ns_adj(tbl[i].adj_val))
				val = tbl[i].adj_val;
			break;
		}
	}

	return val;
}

static int emac_hw_set_rtc_inc_value(struct emac_hw *hw, s32 adj)
{
	u32 clk = get_rtc_ref_clkrate(hw);
	u32 ns, frac, rem, inc;
	u64 v;

	ns = div_u64_rem(1000000000LL, clk, &rem);
	v = (u64)rem << RTC_INC_NS_SHFT;
	frac = div_u64(v, clk);

	if (adj) {
		s32 res;

		res = (s32)frac + adj;
		if (res < 0) {
			ns--;
			res += RTC_NUM_FRAC_NS_PER_NS;
		} else if (res >= RTC_NUM_FRAC_NS_PER_NS) {
			ns++;
			res -= RTC_NUM_FRAC_NS_PER_NS;
		}
		frac = (u32)res;
	}

	inc = (ns << RTC_INC_NS_SHFT) | frac;
	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_INC_VALUE_2,
		     (inc >> 16) & INC_VALUE_2_BMSK);
	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_INC_VALUE_1,
		     inc & INC_VALUE_1_BMSK);
	wmb(); /* ensure P1588_INC_VALUE is set before we proceed */

	return 0;
}

static const struct emac_tstamp_hw_delay *emac_get_ptp_hw_delay(u32 link_speed,
								int phy_mode)
{
	const struct emac_tstamp_hw_delay *info = emac_ptp_hw_delay;

	for (info = emac_ptp_hw_delay; info->phy_mode; info++) {
		if (info->phy_mode == phy_mode && info->speed == link_speed)
			return info;
	}

	return NULL;
}

static int emac_hw_adjust_tstamp_offset(struct emac_hw *hw,
					enum emac_ptp_clk_mode clk_mode,
					u32 link_speed)
{
	const struct emac_tstamp_hw_delay *delay_info;
	struct emac_phy *phy = &emac_hw_get_adap(hw)->phy;

	delay_info = emac_get_ptp_hw_delay(link_speed, phy->phy_interface);

	if (clk_mode == emac_ptp_clk_mode_oc_one_step) {
		u32 latency = (delay_info) ? delay_info->tx : 0;

		emac_reg_update32(hw, EMAC_1588, EMAC_P1588_TX_LATENCY,
				  TX_LATENCY_BMSK, latency << TX_LATENCY_SHFT);
		wmb(); /* ensure that the latency time is flushed to HW */
	}

	if (delay_info) {
		hw->tstamp_rx_offset = delay_info->rx;
		hw->tstamp_tx_offset = delay_info->tx;
	} else {
		hw->tstamp_rx_offset = 0;
		hw->tstamp_tx_offset = 0;
	}

	return 0;
}

static int emac_hw_config_tx_tstamp(struct emac_hw *hw, bool enable)
{
	if (enable) {
		/* Reset the TX timestamp FIFO */
		emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR1,
				  TS_TX_FIFO_SYNC_RST, TS_TX_FIFO_SYNC_RST);
		wmb(); /* ensure that the Tx timestamp reset is flushed to HW */
		emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR1,
				  TS_TX_FIFO_SYNC_RST, 0);
		wmb(); /* ensure that the Tx timestamp is out of reset */

		emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR1,
				  TX_TS_ENABLE, TX_TS_ENABLE);
		wmb(); /* ensure enabling the Tx timestamp is flushed to HW */
		SET_FLAG(hw, HW_TS_TX_EN);
	} else {
		emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR1,
				  TX_TS_ENABLE, 0);
		wmb(); /* ensure disabling the Tx timestamp is flushed to HW */
		CLR_FLAG(hw, HW_TS_TX_EN);
	}

	return 0;
}

static int emac_hw_config_rx_tstamp(struct emac_hw *hw, bool enable)
{
	if (enable) {
		/* Reset the RX timestamp FIFO */
		emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR1,
				  TS_RX_FIFO_SYNC_RST, TS_RX_FIFO_SYNC_RST);
		wmb(); /* ensure that the Rx timestamp reset is flushed to HW */
		emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR1,
				  TS_RX_FIFO_SYNC_RST, 0);
		wmb(); /* ensure that the Rx timestamp is out of reset */

		SET_FLAG(hw, HW_TS_RX_EN);
	} else {
		CLR_FLAG(hw, HW_TS_RX_EN);
	}

	return 0;
}

static int emac_hw_1588_core_disable(struct emac_hw *hw)
{
	if (TEST_FLAG(hw, HW_TS_RX_EN))
		emac_hw_config_rx_tstamp(hw, false);
	if (TEST_FLAG(hw, HW_TS_TX_EN))
		emac_hw_config_tx_tstamp(hw, false);

	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR1,
			  DIS_1588_CLKS, DIS_1588_CLKS);
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR10,
			  DIS_1588, DIS_1588);
	emac_reg_update32(hw, EMAC_1588, EMAC_P1588_CTRL_REG,
			  BYPASS_O, BYPASS_O);
	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_PTP_EXPANDED_INT_MASK, 0);
	wmb(); /* ensure that disabling PTP is flushed to HW */

	CLR_FLAG(hw, HW_PTP_EN);
	return 0;
}

static int emac_hw_1588_core_enable(struct emac_hw *hw,
				    enum emac_ptp_mode mode,
				    enum emac_ptp_clk_mode clk_mode,
				    u32 link_speed,
				    s32 frac_ns_adj)
{
	if (clk_mode != emac_ptp_clk_mode_oc_one_step &&
	    clk_mode != emac_ptp_clk_mode_oc_two_step) {
		struct emac_adapter *adpt = emac_hw_get_adap(hw);

		emac_dbg(emac_hw_get_adap(hw), hw, adpt->netdev, "invalid ptp clk mode %d\n",
			 clk_mode);
		return -EINVAL;
	}

	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR1,
			  DIS_1588_CLKS, 0);
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR10, DIS_1588, 0);
	emac_reg_update32(hw, EMAC_1588, EMAC_P1588_CTRL_REG, BYPASS_O, 0);
	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_PTP_EXPANDED_INT_MASK, 0);
	emac_reg_update32(hw, EMAC_1588, EMAC_P1588_RTC_EXPANDED_CONFIG,
			  RTC_READ_MODE, RTC_READ_MODE);
	emac_reg_update32(hw, EMAC_1588, EMAC_P1588_CTRL_REG, ATTACH_EN, 0);
	wmb(); /* ensure P1588_CTRL_REG is set before we proceed */

	emac_hw_adjust_tstamp_offset(hw, clk_mode, link_speed);

	emac_reg_update32(hw, EMAC_1588, EMAC_P1588_CTRL_REG, CLOCK_MODE_BMSK,
			  (clk_mode << CLOCK_MODE_SHFT));
	emac_reg_update32(hw, EMAC_1588, EMAC_P1588_CTRL_REG, ETH_MODE_SW,
			  (link_speed == EMAC_LINK_SPEED_1GB_FULL) ?
			  0 : ETH_MODE_SW);

	/* set RTC increment every 8ns to fit 125MHZ clock */
	emac_hw_set_rtc_inc_value(hw, frac_ns_adj);

	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR10,
			  RD_CLR_1588, RD_CLR_1588);
	wmb(); /* ensure clear-on-read is enabled on PTP config registers */

	emac_reg_r32(hw, EMAC_1588, EMAC_P1588_PTP_EXPANDED_INT_STATUS);

	/* Reset the timestamp FIFO */
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR1,
			  TS_FIFO_SYNC_RST, TS_FIFO_SYNC_RST);
	wmb(); /* ensure timestamp reset is complete */
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR1,
			  TS_FIFO_SYNC_RST, 0);
	wmb(); /* ensure timestamp is out of reset */

	if (mode == emac_ptp_mode_master)
		emac_reg_update32(hw, EMAC_1588,
				  EMAC_P1588_GRAND_MASTER_CONFIG_0,
				  GRANDMASTER_MODE | GM_PPS_SYNC,
				  GRANDMASTER_MODE);
	else
		emac_reg_update32(hw, EMAC_1588,
				  EMAC_P1588_GRAND_MASTER_CONFIG_0,
				  GRANDMASTER_MODE | GM_PPS_SYNC, 0);
	wmb(); /* ensure gradmaster mode setting is flushed to HW */

	SET_FLAG(hw, HW_PTP_EN);
	return 0;
}

static void rtc_settime(struct emac_hw *hw, const struct timespec64 *ts)
{
	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_RTC_PRELOADED_5, 0);

	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_RTC_PRELOADED_4,
		     (ts->tv_sec >> 16) & RTC_PRELOADED_4_BMSK);
	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_RTC_PRELOADED_3,
		     ts->tv_sec & RTC_PRELOADED_3_BMSK);
	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_RTC_PRELOADED_2,
		     (ts->tv_nsec >> 16) & RTC_PRELOADED_2_BMSK);
	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_RTC_PRELOADED_1,
		     ts->tv_nsec & RTC_PRELOADED_1_BMSK);

	emac_reg_update32(hw, EMAC_1588, EMAC_P1588_RTC_EXPANDED_CONFIG,
			  LOAD_RTC, LOAD_RTC);
	wmb(); /* ensure RTC setting is flushed to HW */
}

static void rtc_gettime(struct emac_hw *hw, struct timespec64 *ts)
{
	emac_reg_update32(hw, EMAC_1588, EMAC_P1588_RTC_EXPANDED_CONFIG,
			  RTC_SNAPSHOT, RTC_SNAPSHOT);
	wmb(); /* ensure snapshot is saved before reading it back */

	ts->tv_sec = emac_reg_field_r32(hw, EMAC_1588, EMAC_P1588_REAL_TIME_5,
					REAL_TIME_5_BMSK, REAL_TIME_5_SHFT);
	ts->tv_sec = (u64)ts->tv_sec << 32;
	ts->tv_sec |= emac_reg_field_r32(hw, EMAC_1588, EMAC_P1588_REAL_TIME_4,
					 REAL_TIME_4_BMSK, REAL_TIME_4_SHFT);
	ts->tv_sec <<= 16;
	ts->tv_sec |= emac_reg_field_r32(hw, EMAC_1588, EMAC_P1588_REAL_TIME_3,
					 REAL_TIME_3_BMSK, REAL_TIME_3_SHFT);

	ts->tv_nsec = emac_reg_field_r32(hw, EMAC_1588, EMAC_P1588_REAL_TIME_2,
					 REAL_TIME_2_BMSK, REAL_TIME_2_SHFT);
	ts->tv_nsec <<= 16;
	ts->tv_nsec |= emac_reg_field_r32(hw, EMAC_1588, EMAC_P1588_REAL_TIME_1,
					  REAL_TIME_1_BMSK, REAL_TIME_1_SHFT);
}

static void rtc_adjtime(struct emac_hw *hw, s64 delta)
{
	s32 delta_ns;
	s32 delta_sec;

	delta_sec = div_s64_rem(delta, 1000000000LL, &delta_ns);

	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_SEC_OFFSET_3, 0);
	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_SEC_OFFSET_2,
		     (delta_sec >> 16) & SEC_OFFSET_2_BMSK);
	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_SEC_OFFSET_1,
		     delta_sec & SEC_OFFSET_1_BMSK);
	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_NANO_OFFSET_2,
		     (delta_ns >> 16) & NANO_OFFSET_2_BMSK);
	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_NANO_OFFSET_1,
		     (delta_ns & NANO_OFFSET_1_BMSK));
	emac_reg_w32(hw, EMAC_1588, EMAC_P1588_ADJUST_RTC, 1);
	wmb(); /* ensure that RTC adjustment is flushed to HW */
}

static void rtc_ns_sync_pps_in(struct emac_hw *hw)
{
	u32 ts;
	s64 delta = 0;

	ts = emac_reg_r32(hw, EMAC_1588, EMAC_P1588_GM_PPS_TIMESTAMP_2);
	ts <<= 16;

	ts |= emac_reg_r32(hw, EMAC_1588, EMAC_P1588_GM_PPS_TIMESTAMP_1);

	if (ts < 500000000)
		delta = 0LL - (s64)ts;
	else
		delta = 1000000000LL - (s64)ts;

	if (delta) {
		struct emac_adapter *adpt = emac_hw_get_adap(hw);

		rtc_adjtime(hw, delta);
		emac_dbg(emac_hw_get_adap(hw), intr, adpt->netdev,
			 "RTC_SYNC: gm_pps_tstamp_ns 0x%08x, adjust %lldns\n",
			 ts, delta);
	}
}

static void emac_ptp_rtc_ns_sync(struct emac_hw *hw)
{
	unsigned long flag = 0;

	spin_lock_irqsave(&hw->ptp_lock, flag);
	rtc_ns_sync_pps_in(hw);
	spin_unlock_irqrestore(&hw->ptp_lock, flag);
}

int emac_ptp_config(struct emac_hw *hw)
{
	struct timespec64 ts;
	int ret = 0;
	unsigned long flag = 0;

	spin_lock_irqsave(&hw->ptp_lock, flag);

	if (TEST_FLAG(hw, HW_PTP_EN))
		goto unlock_out;

	hw->frac_ns_adj = get_frac_ns_adj_from_tbl(hw);
	ret = emac_hw_1588_core_enable(hw,
				       hw->ptp_mode,
				       hw->ptp_clk_mode,
				       SPEED_1000,
				       hw->frac_ns_adj);
	if (ret)
		goto unlock_out;

	ktime_get_real_ts64(&ts);
	rtc_settime(hw, &ts);

	emac_hw_get_adap(hw)->irq[0].mask |= PTP_INT;
	hw->ptp_intr_mask = PPS_IN;

unlock_out:
	spin_unlock_irqrestore(&hw->ptp_lock, flag);

	return ret;
}

int emac_ptp_stop(struct emac_hw *hw)
{
	int ret = 0;
	unsigned long flag = 0;

	spin_lock_irqsave(&hw->ptp_lock, flag);

	if (TEST_FLAG(hw, HW_PTP_EN))
		ret = emac_hw_1588_core_disable(hw);

	hw->ptp_intr_mask = 0;
	emac_hw_get_adap(hw)->irq[0].mask &= ~PTP_INT;

	spin_unlock_irqrestore(&hw->ptp_lock, flag);

	return ret;
}

int emac_ptp_set_linkspeed(struct emac_hw *hw, u32 link_speed)
{
	unsigned long flag = 0;

	spin_lock_irqsave(&hw->ptp_lock, flag);
	emac_reg_update32(hw, EMAC_1588, EMAC_P1588_CTRL_REG, ETH_MODE_SW,
			  (link_speed == SPEED_1000) ? 0 :
			  ETH_MODE_SW);
	wmb(); /* ensure ETH_MODE_SW is set before we proceed */
	emac_hw_adjust_tstamp_offset(hw, hw->ptp_clk_mode, link_speed);
	spin_unlock_irqrestore(&hw->ptp_lock, flag);

	return 0;
}

void emac_ptp_intr(struct emac_hw *hw)
{
	u32 isr, status;
	struct emac_adapter *adpt = emac_hw_get_adap(hw);

	isr = emac_reg_r32(hw, EMAC_1588, EMAC_P1588_PTP_EXPANDED_INT_STATUS);
	status = isr & hw->ptp_intr_mask;

	emac_dbg(emac_hw_get_adap(hw), intr, adpt->netdev,
		 "receive ptp interrupt: isr 0x%x\n", isr);

	if (status & PPS_IN)
		emac_ptp_rtc_ns_sync(hw);
}

static int emac_ptp_settime(struct emac_hw *hw, const struct timespec64 *ts)
{
	int ret = 0;
	unsigned long flag = 0;

	spin_lock_irqsave(&hw->ptp_lock, flag);
	if (!TEST_FLAG(hw, HW_PTP_EN))
		ret = -EPERM;
	else
		rtc_settime(hw, ts);
	spin_unlock_irqrestore(&hw->ptp_lock, flag);

	return ret;
}

static int emac_ptp_gettime(struct emac_hw *hw, struct timespec64 *ts)
{
	int ret = 0;
	unsigned long flag = 0;

	spin_lock_irqsave(&hw->ptp_lock, flag);
	if (!TEST_FLAG(hw, HW_PTP_EN))
		ret = -EPERM;
	else
		rtc_gettime(hw, ts);
	spin_unlock_irqrestore(&hw->ptp_lock, flag);

	return ret;
}

int emac_ptp_adjtime(struct emac_hw *hw, s64 delta)
{
	int ret = 0;
	unsigned long flag = 0;

	spin_lock_irqsave(&hw->ptp_lock, flag);
	if (!TEST_FLAG(hw, HW_PTP_EN))
		ret = -EPERM;
	else
		rtc_adjtime(hw, delta);
	spin_unlock_irqrestore(&hw->ptp_lock, flag);

	return ret;
}

int emac_tstamp_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct emac_adapter *adpt = netdev_priv(netdev);
	struct emac_hw *hw = &adpt->hw;
	struct hwtstamp_config cfg;

	if (!TEST_FLAG(hw, HW_PTP_EN))
		return -EPERM;

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

	switch (cfg.tx_type) {
	case HWTSTAMP_TX_OFF:
		emac_hw_config_tx_tstamp(hw, false);
		break;
	case HWTSTAMP_TX_ON:
		if (TEST_FLAG(hw, HW_TS_TX_EN))
			break;

		emac_hw_config_tx_tstamp(hw, true);
		break;
	default:
		return -ERANGE;
	}

	switch (cfg.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		emac_hw_config_rx_tstamp(hw, false);
		break;
	default:
		cfg.rx_filter = HWTSTAMP_FILTER_ALL;
		if (TEST_FLAG(hw, HW_TS_RX_EN))
			break;

		emac_hw_config_rx_tstamp(hw, true);
		break;
	}

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ?
		-EFAULT : 0;
}

static ssize_t emac_ptp_sysfs_tstamp_set(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct emac_adapter *adpt = netdev_priv(to_net_dev(dev));
	struct timespec64 ts;
	int ret;

	ktime_get_real_ts64(&ts);
	ret = emac_ptp_settime(&adpt->hw, &ts);
	if (!ret)
		ret = count;

	return ret;
}

static ssize_t emac_ptp_sysfs_tstamp_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct emac_adapter *adpt = netdev_priv(to_net_dev(dev));
	struct timespec64 ts = { 0 };
	struct timespec64 ts_now = { 0 };
	int count = PAGE_SIZE;
	ssize_t retval;

	retval = emac_ptp_gettime(&adpt->hw, &ts);
	if (retval)
		return retval;

	ktime_get_real_ts64(&ts_now);
	retval = scnprintf(buf, count,
			   "%12u.%09u tstamp  %12u.%08u time-of-day\n",
			   (int)ts.tv_sec, (int)ts.tv_nsec,
			   (int)ts_now.tv_sec, (int)ts_now.tv_nsec);

	return retval;
}

/* display ethernet mac time as well as the time of the next mac pps pulse */
static ssize_t emac_ptp_sysfs_mtnp_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct emac_adapter *adpt = netdev_priv(to_net_dev(dev));
	int                  count = PAGE_SIZE;
	struct timespec64      ts;
	ssize_t              ret;

	ret = emac_ptp_gettime(&adpt->hw, &ts);
	if (ret)
		return ret;

	return scnprintf(buf, count, "%ld %ld %d %ld\n",
			 ts.tv_sec,
			 ts.tv_nsec,
			 (ts.tv_nsec == 0) ? 1 : 0,
			 (ts.tv_nsec == 0) ? 0 : (NSEC_PER_SEC - ts.tv_nsec));
}

/* Do a "slam" of a very particular time into the time registers... */
static ssize_t emac_ptp_sysfs_slam(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct emac_adapter *adpt = netdev_priv(to_net_dev(dev));
	u32                  sec = 0;
	u32                  nsec = 0;
	ssize_t              ret = -EINVAL;

	if (sscanf(buf, "%u %u", &sec, &nsec) == 2) {
		struct timespec64 ts = {sec, nsec};

		ret = emac_ptp_settime(&adpt->hw, &ts);
		if (ret) {
			pr_err("%s: emac_ptp_settime failed.\n", __func__);
			return ret;
		}
		ret = count;
	} else {
		pr_err("%s: sscanf failed.\n", __func__);
	}

	return ret;
}

/* Do a coarse time ajustment (ie. coarsely adjust (+/-) the time
 * registers by the passed offset)
 */
static ssize_t emac_ptp_sysfs_cadj(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct emac_adapter *adpt = netdev_priv(to_net_dev(dev));
	s64                  offset = 0;
	ssize_t              ret = -EINVAL;

	if (!kstrtos64(buf, 10, &offset)) {
		struct timespec64 ts;
		u64        new_offset;
		u32        sec;
		u32        nsec;

		ret = emac_ptp_gettime(&adpt->hw, &ts);
		if (ret) {
			pr_err("%s: emac_ptp_gettime failed.\n", __func__);
			return ret;
		}

		sec  = ts.tv_sec;
		nsec = ts.tv_nsec;

		new_offset = (((uint64_t)sec * NSEC_PER_SEC) +
			      (uint64_t)nsec) + offset;

		nsec = do_div(new_offset, NSEC_PER_SEC);
		sec  = new_offset;

		ts.tv_sec  = sec;
		ts.tv_nsec = nsec;

		ret = emac_ptp_settime(&adpt->hw, &ts);
		if (ret) {
			pr_err("%s: emac_ptp_settime failed.\n", __func__);
			return ret;
		}
		ret = count;
	} else {
		pr_err("%s: sscanf failed.\n", __func__);
	}

	return ret;
}

/* Do a fine time ajustment (ie. have the timestamp registers adjust
 * themselves by the passed amount).
 */
static ssize_t emac_ptp_sysfs_fadj(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct emac_adapter *adpt = netdev_priv(to_net_dev(dev));
	s64                  offset = 0;
	ssize_t              ret = -EINVAL;

	if (!kstrtos64(buf, 10, &offset)) {
		ret = emac_ptp_adjtime(&adpt->hw, offset);
		if (ret) {
			pr_err("%s: emac_ptp_adjtime failed.\n", __func__);
			return ret;
		}
		ret = count;
	} else {
		pr_err("%s: sscanf failed.\n", __func__);
	}

	return ret;
}

static ssize_t emac_ptp_sysfs_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct emac_adapter *adpt = netdev_priv(to_net_dev(dev));
	int count = PAGE_SIZE;
	ssize_t ret;

	ret = scnprintf(buf, count, "%s\n",
			(adpt->hw.ptp_mode == emac_ptp_mode_master) ?
			"master" : "slave");

	return ret;
}

static ssize_t emac_ptp_sysfs_mode_set(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct emac_adapter *adpt = netdev_priv(to_net_dev(dev));
	struct emac_hw *hw = &adpt->hw;
	struct phy_device *phydev = adpt->phydev;
	enum emac_ptp_mode mode;

	if (!strcmp(buf, "master"))
		mode = emac_ptp_mode_master;
	else if (!strcmp(buf, "slave"))
		mode = emac_ptp_mode_slave;
	else
		return -EINVAL;

	if (mode == hw->ptp_mode)
		goto out;

	if (TEST_FLAG(hw, HW_PTP_EN)) {
		bool rx_tstamp_enable = TEST_FLAG(hw, HW_TS_RX_EN);
		bool tx_tstamp_enable = TEST_FLAG(hw, HW_TS_TX_EN);

		emac_hw_1588_core_disable(hw);
		emac_hw_1588_core_enable(hw, mode, hw->ptp_clk_mode,
					 phydev->speed, hw->frac_ns_adj);
		if (rx_tstamp_enable)
			emac_hw_config_rx_tstamp(hw, true);
		if (tx_tstamp_enable)
			emac_hw_config_tx_tstamp(hw, true);

		emac_reg_w32(hw, EMAC_1588, EMAC_P1588_PTP_EXPANDED_INT_MASK,
			     hw->ptp_intr_mask);
		wmb(); /* ensure PTP_EXPANDED_INT_MASK is set */
	}

	hw->ptp_mode = mode;

out:
	return count;
}

static ssize_t emac_ptp_sysfs_frac_ns_adj_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct emac_adapter *adpt = netdev_priv(to_net_dev(dev));
	struct emac_hw *hw = &adpt->hw;
	int count = PAGE_SIZE;
	ssize_t ret;

	if (!TEST_FLAG(hw, HW_PTP_EN))
		return -EPERM;

	ret = scnprintf(buf, count, "%d\n", adpt->hw.frac_ns_adj);

	return ret;
}

static ssize_t emac_ptp_sysfs_frac_ns_adj_set(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	struct emac_adapter *adpt = netdev_priv(to_net_dev(dev));
	struct emac_hw *hw = &adpt->hw;
	s32 adj;

	if (!TEST_FLAG(hw, HW_PTP_EN))
		return -EPERM;

	if (kstrtos32(buf, 0, &adj))
		return -EINVAL;

	if (!is_valid_frac_ns_adj(adj))
		return -EINVAL;

	emac_hw_set_rtc_inc_value(hw, adj);
	hw->frac_ns_adj = adj;

	return count;
}

static struct device_attribute ptp_sysfs_devattr[] = {
	__ATTR(tstamp, 0660,
	       emac_ptp_sysfs_tstamp_show, emac_ptp_sysfs_tstamp_set),
	__ATTR(mtnp, 0440, emac_ptp_sysfs_mtnp_show, NULL),
	__ATTR(slam, 0220, NULL, emac_ptp_sysfs_slam),
	__ATTR(cadj, 0220, NULL, emac_ptp_sysfs_cadj),
	__ATTR(fadj, 0220, NULL, emac_ptp_sysfs_fadj),
	__ATTR(frac_ns_adj, 0660,
	       emac_ptp_sysfs_frac_ns_adj_show, emac_ptp_sysfs_frac_ns_adj_set),
	__ATTR(ptp_mode, 0660,
	       emac_ptp_sysfs_mode_show, emac_ptp_sysfs_mode_set),
	__ATTR_NULL
};

static void emac_ptp_sysfs_create(struct net_device *netdev)
{
	struct emac_adapter *adpt = netdev_priv(netdev);
	struct device_attribute *devattr;

	for (devattr = ptp_sysfs_devattr; devattr->attr.name; devattr++) {
		if (device_create_file(&netdev->dev, devattr)) {
			emac_err(adpt,
				 "emac_ptp: failed to create sysfs files\n");
			break;
		}
	}
}

static void emac_ptp_of_get_property(struct emac_adapter *adpt)
{
	struct emac_hw *hw = &adpt->hw;
	struct device *parent = adpt->netdev->dev.parent;
	struct device_node *node = parent->of_node;
	const int *tbl;
	struct emac_ptp_frac_ns_adj *adj_tbl = NULL;
	int size, tbl_size;

	if (of_property_read_bool(node, "qcom,emac-ptp-grandmaster"))
		hw->ptp_mode = emac_ptp_mode_master;
	else
		hw->ptp_mode = emac_ptp_mode_slave;

	hw->frac_ns_adj_tbl = NULL;
	hw->frac_ns_adj_tbl_sz = 0;

	tbl = of_get_property(node, "qcom,emac-ptp-frac-ns-adj", &size);
	if (!tbl)
		return;

	if ((size % sizeof(struct emac_ptp_frac_ns_adj))) {
		emac_err(adpt, "emac_ptp: invalid frac-ns-adj tbl size(%d)\n",
			 size);
		return;
	}
	tbl_size = size / sizeof(struct emac_ptp_frac_ns_adj);

	adj_tbl = kzalloc(size, GFP_KERNEL);
	if (!adj_tbl)
		return;

	if (of_property_read_u32_array(node, "qcom,emac-ptp-frac-ns-adj",
				       (u32 *)adj_tbl, size / sizeof(u32))) {
		emac_err(adpt, "emac_ptp: failed to read frac-ns-adj tbl\n");
		kfree(adj_tbl);
		return;
	}

	hw->frac_ns_adj_tbl = adj_tbl;
	hw->frac_ns_adj_tbl_sz = tbl_size;
}

int emac_ptp_init(struct net_device *netdev)
{
	struct emac_adapter *adpt = netdev_priv(netdev);
	struct emac_hw *hw = &adpt->hw;
	int ret = 0;

	emac_ptp_of_get_property(adpt);
	spin_lock_init(&hw->ptp_lock);
	emac_ptp_sysfs_create(netdev);
	ret = emac_hw_1588_core_disable(hw);

	return ret;
}

void emac_ptp_remove(struct net_device *netdev)
{
	struct emac_adapter *adpt = netdev_priv(netdev);
	struct emac_hw *hw = &adpt->hw;

	kfree(hw->frac_ns_adj_tbl);
}
