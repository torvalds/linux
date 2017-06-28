/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/types.h>
#include "qed.h"
#include "qed_dev_api.h"
#include "qed_hw.h"
#include "qed_l2.h"
#include "qed_ptp.h"
#include "qed_reg_addr.h"

/* 16 nano second time quantas to wait before making a Drift adjustment */
#define QED_DRIFT_CNTR_TIME_QUANTA_SHIFT	0
/* Nano seconds to add/subtract when making a Drift adjustment */
#define QED_DRIFT_CNTR_ADJUSTMENT_SHIFT		28
/* Add/subtract the Adjustment_Value when making a Drift adjustment */
#define QED_DRIFT_CNTR_DIRECTION_SHIFT		31
#define QED_TIMESTAMP_MASK			BIT(16)

/* Read Rx timestamp */
static int qed_ptp_hw_read_rx_ts(struct qed_dev *cdev, u64 *timestamp)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *p_ptt = p_hwfn->p_ptp_ptt;
	u32 val;

	*timestamp = 0;
	val = qed_rd(p_hwfn, p_ptt, NIG_REG_LLH_PTP_HOST_BUF_SEQID);
	if (!(val & QED_TIMESTAMP_MASK)) {
		DP_INFO(p_hwfn, "Invalid Rx timestamp, buf_seqid = %d\n", val);
		return -EINVAL;
	}

	val = qed_rd(p_hwfn, p_ptt, NIG_REG_LLH_PTP_HOST_BUF_TS_LSB);
	*timestamp = qed_rd(p_hwfn, p_ptt, NIG_REG_LLH_PTP_HOST_BUF_TS_MSB);
	*timestamp <<= 32;
	*timestamp |= val;

	/* Reset timestamp register to allow new timestamp */
	qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_PTP_HOST_BUF_SEQID,
	       QED_TIMESTAMP_MASK);

	return 0;
}

/* Read Tx timestamp */
static int qed_ptp_hw_read_tx_ts(struct qed_dev *cdev, u64 *timestamp)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *p_ptt = p_hwfn->p_ptp_ptt;
	u32 val;

	*timestamp = 0;
	val = qed_rd(p_hwfn, p_ptt, NIG_REG_TX_LLH_PTP_BUF_SEQID);
	if (!(val & QED_TIMESTAMP_MASK)) {
		DP_INFO(p_hwfn, "Invalid Tx timestamp, buf_seqid = %d\n", val);
		return -EINVAL;
	}

	val = qed_rd(p_hwfn, p_ptt, NIG_REG_TX_LLH_PTP_BUF_TS_LSB);
	*timestamp = qed_rd(p_hwfn, p_ptt, NIG_REG_TX_LLH_PTP_BUF_TS_MSB);
	*timestamp <<= 32;
	*timestamp |= val;

	/* Reset timestamp register to allow new timestamp */
	qed_wr(p_hwfn, p_ptt, NIG_REG_TX_LLH_PTP_BUF_SEQID, QED_TIMESTAMP_MASK);

	return 0;
}

/* Read Phy Hardware Clock */
static int qed_ptp_hw_read_cc(struct qed_dev *cdev, u64 *phc_cycles)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *p_ptt = p_hwfn->p_ptp_ptt;
	u32 temp = 0;

	temp = qed_rd(p_hwfn, p_ptt, NIG_REG_TSGEN_SYNC_TIME_LSB);
	*phc_cycles = qed_rd(p_hwfn, p_ptt, NIG_REG_TSGEN_SYNC_TIME_MSB);
	*phc_cycles <<= 32;
	*phc_cycles |= temp;

	return 0;
}

/* Filter PTP protocol packets that need to be timestamped */
static int qed_ptp_hw_cfg_rx_filters(struct qed_dev *cdev,
				     enum qed_ptp_filter_type type)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *p_ptt = p_hwfn->p_ptp_ptt;
	u32 rule_mask, parm_mask;

	switch (type) {
	case QED_PTP_FILTER_L2_IPV4_IPV6:
		parm_mask = 0x6AA;
		rule_mask = 0x3EEE;
		break;
	case QED_PTP_FILTER_L2:
		parm_mask = 0x6BF;
		rule_mask = 0x3EFF;
		break;
	case QED_PTP_FILTER_IPV4_IPV6:
		parm_mask = 0x7EA;
		rule_mask = 0x3FFE;
		break;
	case QED_PTP_FILTER_IPV4:
		parm_mask = 0x7EE;
		rule_mask = 0x3FFE;
		break;
	default:
		DP_INFO(p_hwfn, "Invalid PTP filter type %d\n", type);
		return -EINVAL;
	}

	qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_PTP_PARAM_MASK, parm_mask);
	qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_PTP_RULE_MASK, rule_mask);

	qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_PTP_TO_HOST, 0x1);

	/* Reset possibly old timestamps */
	qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_PTP_HOST_BUF_SEQID,
	       QED_TIMESTAMP_MASK);

	return 0;
}

/* Adjust the HW clock by a rate given in parts-per-billion (ppb) units.
 * FW/HW accepts the adjustment value in terms of 3 parameters:
 *   Drift period - adjustment happens once in certain number of nano seconds.
 *   Drift value - time is adjusted by a certain value, for example by 5 ns.
 *   Drift direction - add or subtract the adjustment value.
 * The routine translates ppb into the adjustment triplet in an optimal manner.
 */
static int qed_ptp_hw_adjfreq(struct qed_dev *cdev, s32 ppb)
{
	s64 best_val = 0, val, best_period = 0, period, approx_dev, dif, dif2;
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *p_ptt = p_hwfn->p_ptp_ptt;
	u32 drift_ctr_cfg = 0, drift_state;
	int drift_dir = 1;

	if (ppb < 0) {
		ppb = -ppb;
		drift_dir = 0;
	}

	if (ppb > 1) {
		s64 best_dif = ppb, best_approx_dev = 1;

		/* Adjustment value is up to +/-7ns, find an optimal value in
		 * this range.
		 */
		for (val = 7; val > 0; val--) {
			period = div_s64(val * 1000000000, ppb);
			period -= 8;
			period >>= 4;
			if (period < 1)
				period = 1;
			if (period > 0xFFFFFFE)
				period = 0xFFFFFFE;

			/* Check both rounding ends for approximate error */
			approx_dev = period * 16 + 8;
			dif = ppb * approx_dev - val * 1000000000;
			dif2 = dif + 16 * ppb;

			if (dif < 0)
				dif = -dif;
			if (dif2 < 0)
				dif2 = -dif2;

			/* Determine which end gives better approximation */
			if (dif * (approx_dev + 16) > dif2 * approx_dev) {
				period++;
				approx_dev += 16;
				dif = dif2;
			}

			/* Track best approximation found so far */
			if (best_dif * approx_dev > dif * best_approx_dev) {
				best_dif = dif;
				best_val = val;
				best_period = period;
				best_approx_dev = approx_dev;
			}
		}
	} else if (ppb == 1) {
		/* This is a special case as its the only value which wouldn't
		 * fit in a s64 variable. In order to prevent castings simple
		 * handle it seperately.
		 */
		best_val = 4;
		best_period = 0xee6b27f;
	} else {
		best_val = 0;
		best_period = 0xFFFFFFF;
	}

	drift_ctr_cfg = (best_period << QED_DRIFT_CNTR_TIME_QUANTA_SHIFT) |
			(((int)best_val) << QED_DRIFT_CNTR_ADJUSTMENT_SHIFT) |
			(((int)drift_dir) << QED_DRIFT_CNTR_DIRECTION_SHIFT);

	qed_wr(p_hwfn, p_ptt, NIG_REG_TSGEN_RST_DRIFT_CNTR, 0x1);

	drift_state = qed_rd(p_hwfn, p_ptt, NIG_REG_TSGEN_RST_DRIFT_CNTR);
	if (drift_state & 1) {
		qed_wr(p_hwfn, p_ptt, NIG_REG_TSGEN_DRIFT_CNTR_CONF,
		       drift_ctr_cfg);
	} else {
		DP_INFO(p_hwfn, "Drift counter is not reset\n");
		return -EINVAL;
	}

	qed_wr(p_hwfn, p_ptt, NIG_REG_TSGEN_RST_DRIFT_CNTR, 0x0);

	return 0;
}

static int qed_ptp_hw_enable(struct qed_dev *cdev)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *p_ptt = p_hwfn->p_ptp_ptt;

	/* Reset PTP event detection rules - will be configured in the IOCTL */
	qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_PTP_PARAM_MASK, 0x7FF);
	qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_PTP_RULE_MASK, 0x3FFF);
	qed_wr(p_hwfn, p_ptt, NIG_REG_TX_LLH_PTP_PARAM_MASK, 0x7FF);
	qed_wr(p_hwfn, p_ptt, NIG_REG_TX_LLH_PTP_RULE_MASK, 0x3FFF);

	qed_wr(p_hwfn, p_ptt, NIG_REG_TX_PTP_EN, 7);
	qed_wr(p_hwfn, p_ptt, NIG_REG_RX_PTP_EN, 7);

	qed_wr(p_hwfn, p_ptt, NIG_REG_TS_OUTPUT_ENABLE_PDA, 0x1);

	/* Pause free running counter */
	qed_wr(p_hwfn, p_ptt, NIG_REG_TIMESYNC_GEN_REG_BB, 2);

	qed_wr(p_hwfn, p_ptt, NIG_REG_TSGEN_FREE_CNT_VALUE_LSB, 0);
	qed_wr(p_hwfn, p_ptt, NIG_REG_TSGEN_FREE_CNT_VALUE_MSB, 0);
	/* Resume free running counter */
	qed_wr(p_hwfn, p_ptt, NIG_REG_TIMESYNC_GEN_REG_BB, 4);

	/* Disable drift register */
	qed_wr(p_hwfn, p_ptt, NIG_REG_TSGEN_DRIFT_CNTR_CONF, 0x0);
	qed_wr(p_hwfn, p_ptt, NIG_REG_TSGEN_RST_DRIFT_CNTR, 0x0);

	/* Reset possibly old timestamps */
	qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_PTP_HOST_BUF_SEQID,
	       QED_TIMESTAMP_MASK);
	qed_wr(p_hwfn, p_ptt, NIG_REG_TX_LLH_PTP_BUF_SEQID, QED_TIMESTAMP_MASK);

	return 0;
}

static int qed_ptp_hw_hwtstamp_tx_on(struct qed_dev *cdev)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *p_ptt = p_hwfn->p_ptp_ptt;

	qed_wr(p_hwfn, p_ptt, NIG_REG_TX_LLH_PTP_PARAM_MASK, 0x6AA);
	qed_wr(p_hwfn, p_ptt, NIG_REG_TX_LLH_PTP_RULE_MASK, 0x3EEE);

	return 0;
}

static int qed_ptp_hw_disable(struct qed_dev *cdev)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *p_ptt = p_hwfn->p_ptp_ptt;

	/* Reset PTP event detection rules */
	qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_PTP_PARAM_MASK, 0x7FF);
	qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_PTP_RULE_MASK, 0x3FFF);

	qed_wr(p_hwfn, p_ptt, NIG_REG_TX_LLH_PTP_PARAM_MASK, 0x7FF);
	qed_wr(p_hwfn, p_ptt, NIG_REG_TX_LLH_PTP_RULE_MASK, 0x3FFF);

	/* Disable the PTP feature */
	qed_wr(p_hwfn, p_ptt, NIG_REG_RX_PTP_EN, 0x0);
	qed_wr(p_hwfn, p_ptt, NIG_REG_TX_PTP_EN, 0x0);

	return 0;
}

const struct qed_eth_ptp_ops qed_ptp_ops_pass = {
	.hwtstamp_tx_on = qed_ptp_hw_hwtstamp_tx_on,
	.cfg_rx_filters = qed_ptp_hw_cfg_rx_filters,
	.read_rx_ts = qed_ptp_hw_read_rx_ts,
	.read_tx_ts = qed_ptp_hw_read_tx_ts,
	.read_cc = qed_ptp_hw_read_cc,
	.adjfreq = qed_ptp_hw_adjfreq,
	.disable = qed_ptp_hw_disable,
	.enable = qed_ptp_hw_enable,
};
