// SPDX-License-Identifier: GPL-2.0+
/*
 * PTP hardware clock driver for the FemtoClock3 family of timing and
 * synchronization devices.
 *
 * Copyright (C) 2023 Integrated Device Technology, Inc., a Renesas Company.
 */
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/timekeeping.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/bitfield.h>
#include <linux/mfd/rsmu.h>
#include <linux/mfd/idtRC38xxx_reg.h>
#include <asm/unaligned.h>

#include "ptp_private.h"
#include "ptp_fc3.h"

MODULE_DESCRIPTION("Driver for IDT FemtoClock3(TM) family");
MODULE_AUTHOR("IDT support-1588 <IDT-support-1588@lm.renesas.com>");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");

/*
 * The name of the firmware file to be loaded
 * over-rides any automatic selection
 */
static char *firmware;
module_param(firmware, charp, 0);

static s64 ns2counters(struct idtfc3 *idtfc3, s64 nsec, u32 *sub_ns)
{
	s64 sync;
	s32 rem;

	if (likely(nsec >= 0)) {
		sync = div_u64_rem(nsec, idtfc3->ns_per_sync, &rem);
		*sub_ns = rem;
	} else {
		sync = -div_u64_rem(-nsec - 1, idtfc3->ns_per_sync, &rem) - 1;
		*sub_ns = idtfc3->ns_per_sync - rem - 1;
	}

	return sync * idtfc3->ns_per_sync;
}

static s64 tdc_meas2offset(struct idtfc3 *idtfc3, u64 meas_read)
{
	s64 coarse, fine;

	fine = sign_extend64(FIELD_GET(FINE_MEAS_MASK, meas_read), 12);
	coarse = sign_extend64(FIELD_GET(COARSE_MEAS_MASK, meas_read), (39 - 13));

	fine = div64_s64(fine * NSEC_PER_SEC, idtfc3->tdc_apll_freq * 62LL);
	coarse = div64_s64(coarse * NSEC_PER_SEC, idtfc3->time_ref_freq);

	return coarse + fine;
}

static s64 tdc_offset2phase(struct idtfc3 *idtfc3, s64 offset_ns)
{
	if (offset_ns > idtfc3->ns_per_sync / 2)
		offset_ns -= idtfc3->ns_per_sync;

	return offset_ns * idtfc3->tdc_offset_sign;
}

static int idtfc3_set_lpf_mode(struct idtfc3 *idtfc3, u8 mode)
{
	int err;

	if (mode >= LPF_INVALID)
		return -EINVAL;

	if (idtfc3->lpf_mode == mode)
		return 0;

	err = regmap_bulk_write(idtfc3->regmap, LPF_MODE_CNFG, &mode, sizeof(mode));
	if (err)
		return err;

	idtfc3->lpf_mode = mode;

	return 0;
}

static int idtfc3_enable_lpf(struct idtfc3 *idtfc3, bool enable)
{
	u8 val;
	int err;

	err = regmap_bulk_read(idtfc3->regmap, LPF_CTRL, &val, sizeof(val));
	if (err)
		return err;

	if (enable == true)
		val |= LPF_EN;
	else
		val &= ~LPF_EN;

	return regmap_bulk_write(idtfc3->regmap, LPF_CTRL, &val, sizeof(val));
}

static int idtfc3_get_time_ref_freq(struct idtfc3 *idtfc3)
{
	int err;
	u8 buf[4];
	u8 time_ref_div;
	u8 time_clk_div;

	err = regmap_bulk_read(idtfc3->regmap, TIME_CLOCK_MEAS_DIV_CNFG, buf, sizeof(buf));
	if (err)
		return err;
	time_ref_div = FIELD_GET(TIME_REF_DIV_MASK, get_unaligned_le32(buf)) + 1;

	err = regmap_bulk_read(idtfc3->regmap, TIME_CLOCK_COUNT, buf, 1);
	if (err)
		return err;
	time_clk_div = (buf[0] & TIME_CLOCK_COUNT_MASK) + 1;
	idtfc3->time_ref_freq = idtfc3->hw_param.time_clk_freq *
				time_clk_div / time_ref_div;

	return 0;
}

static int idtfc3_get_tdc_offset_sign(struct idtfc3 *idtfc3)
{
	int err;
	u8 buf[4];
	u32 val;
	u8 sig1, sig2;

	err = regmap_bulk_read(idtfc3->regmap, TIME_CLOCK_TDC_FANOUT_CNFG, buf, sizeof(buf));
	if (err)
		return err;

	val = get_unaligned_le32(buf);
	if ((val & TIME_SYNC_TO_TDC_EN) != TIME_SYNC_TO_TDC_EN) {
		dev_err(idtfc3->dev, "TIME_SYNC_TO_TDC_EN is off !!!");
		return -EINVAL;
	}

	sig1 = FIELD_GET(SIG1_MUX_SEL_MASK, val);
	sig2 = FIELD_GET(SIG2_MUX_SEL_MASK, val);

	if ((sig1 == sig2) || ((sig1 != TIME_SYNC) && (sig2 != TIME_SYNC))) {
		dev_err(idtfc3->dev, "Invalid tdc_mux_sel sig1=%d sig2=%d", sig1, sig2);
		return -EINVAL;
	} else if (sig1 == TIME_SYNC) {
		idtfc3->tdc_offset_sign = 1;
	} else if (sig2 == TIME_SYNC) {
		idtfc3->tdc_offset_sign = -1;
	}

	return 0;
}

static int idtfc3_lpf_bw(struct idtfc3 *idtfc3, u8 shift, u8 mult)
{
	u8 val = FIELD_PREP(LPF_BW_SHIFT, shift) | FIELD_PREP(LPF_BW_MULT, mult);

	return regmap_bulk_write(idtfc3->regmap, LPF_BW_CNFG, &val, sizeof(val));
}

static int idtfc3_enable_tdc(struct idtfc3 *idtfc3, bool enable, u8 meas_mode)
{
	int err;
	u8 val = 0;

	/* Disable TDC first */
	err = regmap_bulk_write(idtfc3->regmap, TIME_CLOCK_MEAS_CTRL, &val, sizeof(val));
	if (err)
		return err;

	if (enable == false)
		return idtfc3_lpf_bw(idtfc3, LPF_BW_SHIFT_DEFAULT, LPF_BW_MULT_DEFAULT);

	if (meas_mode >= MEAS_MODE_INVALID)
		return -EINVAL;

	/* Change TDC meas mode */
	err = regmap_bulk_write(idtfc3->regmap, TIME_CLOCK_MEAS_CNFG,
				&meas_mode, sizeof(meas_mode));
	if (err)
		return err;

	/* Enable TDC */
	val = TDC_MEAS_EN;
	if (meas_mode == CONTINUOUS)
		val |= TDC_MEAS_START;
	err = regmap_bulk_write(idtfc3->regmap, TIME_CLOCK_MEAS_CTRL, &val, sizeof(val));
	if (err)
		return err;

	return idtfc3_lpf_bw(idtfc3, LPF_BW_SHIFT_1PPS, LPF_BW_MULT_DEFAULT);
}

static bool get_tdc_meas(struct idtfc3 *idtfc3, s64 *offset_ns)
{
	bool valid = false;
	u8 buf[9];
	u8 val;
	int err;

	while (true) {
		err = regmap_bulk_read(idtfc3->regmap, TDC_FIFO_STS,
				       &val, sizeof(val));
		if (err)
			return false;

		if (val & FIFO_EMPTY)
			break;

		err = regmap_bulk_read(idtfc3->regmap, TDC_FIFO_READ_REQ,
				       &buf, sizeof(buf));
		if (err)
			return false;

		valid = true;
	}

	if (valid)
		*offset_ns = tdc_meas2offset(idtfc3, get_unaligned_le64(&buf[1]));

	return valid;
}

static int check_tdc_fifo_overrun(struct idtfc3 *idtfc3)
{
	u8 val;
	int err;

	/* Check if FIFO is overrun */
	err = regmap_bulk_read(idtfc3->regmap, TDC_FIFO_STS, &val, sizeof(val));
	if (err)
		return err;

	if (!(val & FIFO_FULL))
		return 0;

	dev_warn(idtfc3->dev, "TDC FIFO overrun !!!");

	err = idtfc3_enable_tdc(idtfc3, true, CONTINUOUS);
	if (err)
		return err;

	return 0;
}

static int get_tdc_meas_continuous(struct idtfc3 *idtfc3)
{
	int err;
	s64 offset_ns;
	struct ptp_clock_event event;

	err = check_tdc_fifo_overrun(idtfc3);
	if (err)
		return err;

	if (get_tdc_meas(idtfc3, &offset_ns) && offset_ns >= 0) {
		event.index = 0;
		event.offset = tdc_offset2phase(idtfc3, offset_ns);
		event.type = PTP_CLOCK_EXTOFF;
		ptp_clock_event(idtfc3->ptp_clock, &event);
	}

	return 0;
}

static int idtfc3_read_subcounter(struct idtfc3 *idtfc3)
{
	u8 buf[5] = {0};
	int err;

	err = regmap_bulk_read(idtfc3->regmap, TOD_COUNTER_READ_REQ,
			       &buf, sizeof(buf));
	if (err)
		return err;

	/* sync_counter_value is [31:82] and sub_sync_counter_value is [0:30] */
	return get_unaligned_le32(&buf[1]) & SUB_SYNC_COUNTER_MASK;
}

static int idtfc3_tod_update_is_done(struct idtfc3 *idtfc3)
{
	int err;
	u8 req;

	err = read_poll_timeout_atomic(regmap_bulk_read, err, !req, USEC_PER_MSEC,
				       idtfc3->tc_write_timeout, true, idtfc3->regmap,
				       TOD_SYNC_LOAD_REQ_CTRL, &req, 1);
	if (err)
		dev_err(idtfc3->dev, "TOD counter write timeout !!!");

	return err;
}

static int idtfc3_write_subcounter(struct idtfc3 *idtfc3, u32 counter)
{
	u8 buf[18] = {0};
	int err;

	/* sync_counter_value is [31:82] and sub_sync_counter_value is [0:30] */
	put_unaligned_le32(counter & SUB_SYNC_COUNTER_MASK, &buf[0]);

	buf[16] = SUB_SYNC_LOAD_ENABLE | SYNC_LOAD_ENABLE;
	buf[17] = SYNC_LOAD_REQ;

	err = regmap_bulk_write(idtfc3->regmap, TOD_SYNC_LOAD_VAL_CTRL,
				&buf, sizeof(buf));
	if (err)
		return err;

	return idtfc3_tod_update_is_done(idtfc3);
}

static int idtfc3_timecounter_update(struct idtfc3 *idtfc3, u32 counter, s64 ns)
{
	int err;

	err = idtfc3_write_subcounter(idtfc3, counter);
	if (err)
		return err;

	/* Update time counter */
	idtfc3->ns = ns;
	idtfc3->last_counter = counter;

	return 0;
}

static int idtfc3_timecounter_read(struct idtfc3 *idtfc3)
{
	int now, delta;

	now = idtfc3_read_subcounter(idtfc3);
	if (now < 0)
		return now;

	/* calculate the delta since the last idtfc3_timecounter_read(): */
	if (now >= idtfc3->last_counter)
		delta = now - idtfc3->last_counter;
	else
		delta = idtfc3->sub_sync_count - idtfc3->last_counter + now;

	/* Update time counter */
	idtfc3->ns += delta * idtfc3->ns_per_counter;
	idtfc3->last_counter = now;

	return 0;
}

static int _idtfc3_gettime(struct idtfc3 *idtfc3, struct timespec64 *ts)
{
	int err;

	err = idtfc3_timecounter_read(idtfc3);
	if (err)
		return err;

	*ts = ns_to_timespec64(idtfc3->ns);

	return 0;
}

static int idtfc3_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct idtfc3 *idtfc3 = container_of(ptp, struct idtfc3, caps);
	int err;

	mutex_lock(idtfc3->lock);
	err = _idtfc3_gettime(idtfc3, ts);
	mutex_unlock(idtfc3->lock);

	return err;
}

static int _idtfc3_settime(struct idtfc3 *idtfc3, const struct timespec64 *ts)
{
	s64 offset_ns, now_ns;
	u32 counter, sub_ns;
	int now;

	if (timespec64_valid(ts) == false) {
		dev_err(idtfc3->dev, "%s: invalid timespec", __func__);
		return -EINVAL;
	}

	now = idtfc3_read_subcounter(idtfc3);
	if (now < 0)
		return now;

	offset_ns = (idtfc3->sub_sync_count - now) * idtfc3->ns_per_counter;
	now_ns = timespec64_to_ns(ts);
	(void)ns2counters(idtfc3, offset_ns + now_ns, &sub_ns);

	counter = sub_ns / idtfc3->ns_per_counter;
	return idtfc3_timecounter_update(idtfc3, counter, now_ns);
}

static int idtfc3_settime(struct ptp_clock_info *ptp, const struct timespec64 *ts)
{
	struct idtfc3 *idtfc3 = container_of(ptp, struct idtfc3, caps);
	int err;

	mutex_lock(idtfc3->lock);
	err = _idtfc3_settime(idtfc3, ts);
	mutex_unlock(idtfc3->lock);

	return err;
}

static int _idtfc3_adjtime(struct idtfc3 *idtfc3, s64 delta)
{
	/*
	 * The TOD counter can be synchronously loaded with any value,
	 * to be loaded on the next Time Sync pulse
	 */
	s64 sync_ns;
	u32 sub_ns;
	u32 counter;

	if (idtfc3->ns + delta < 0) {
		dev_err(idtfc3->dev, "%lld ns adj is too large", delta);
		return -EINVAL;
	}

	sync_ns = ns2counters(idtfc3, delta + idtfc3->ns_per_sync, &sub_ns);

	counter = sub_ns / idtfc3->ns_per_counter;
	return idtfc3_timecounter_update(idtfc3, counter, idtfc3->ns + sync_ns +
									counter * idtfc3->ns_per_counter);
}

static int idtfc3_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct idtfc3 *idtfc3 = container_of(ptp, struct idtfc3, caps);
	int err;

	mutex_lock(idtfc3->lock);
	err = _idtfc3_adjtime(idtfc3, delta);
	mutex_unlock(idtfc3->lock);

	return err;
}

static int _idtfc3_adjphase(struct idtfc3 *idtfc3, s32 delta)
{
	u8 buf[8] = {0};
	int err;
	s64 pcw;

	err = idtfc3_set_lpf_mode(idtfc3, LPF_WP);
	if (err)
		return err;

	/*
	 * Phase Control Word unit is: 10^9 / (TDC_APLL_FREQ * 124)
	 *
	 *       delta * TDC_APLL_FREQ * 124
	 * PCW = ---------------------------
	 *                  10^9
	 *
	 */
	pcw = div_s64((s64)delta * idtfc3->tdc_apll_freq * 124, NSEC_PER_SEC);

	put_unaligned_le64(pcw, buf);

	return regmap_bulk_write(idtfc3->regmap, LPF_WR_PHASE_CTRL, buf, sizeof(buf));
}

static int idtfc3_adjphase(struct ptp_clock_info *ptp, s32 delta)
{
	struct idtfc3 *idtfc3 = container_of(ptp, struct idtfc3, caps);
	int err;

	mutex_lock(idtfc3->lock);
	err = _idtfc3_adjphase(idtfc3, delta);
	mutex_unlock(idtfc3->lock);

	return err;
}

static int _idtfc3_adjfine(struct idtfc3 *idtfc3, long scaled_ppm)
{
	u8 buf[8] = {0};
	int err;
	s64 fcw;

	err = idtfc3_set_lpf_mode(idtfc3, LPF_WF);
	if (err)
		return err;

	/*
	 * Frequency Control Word unit is: 2^-44 * 10^6 ppm
	 *
	 * adjfreq:
	 *       ppb * 2^44
	 * FCW = ----------
	 *          10^9
	 *
	 * adjfine:
	 *       ppm_16 * 2^28
	 * FCW = -------------
	 *           10^6
	 */
	fcw = scaled_ppm * BIT(28);
	fcw = div_s64(fcw, 1000000);

	put_unaligned_le64(fcw, buf);

	return regmap_bulk_write(idtfc3->regmap, LPF_WR_FREQ_CTRL, buf, sizeof(buf));
}

static int idtfc3_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct idtfc3 *idtfc3 = container_of(ptp, struct idtfc3, caps);
	int err;

	mutex_lock(idtfc3->lock);
	err = _idtfc3_adjfine(idtfc3, scaled_ppm);
	mutex_unlock(idtfc3->lock);

	return err;
}

static int idtfc3_enable(struct ptp_clock_info *ptp,
			 struct ptp_clock_request *rq, int on)
{
	struct idtfc3 *idtfc3 = container_of(ptp, struct idtfc3, caps);
	int err = -EOPNOTSUPP;

	mutex_lock(idtfc3->lock);
	switch (rq->type) {
	case PTP_CLK_REQ_PEROUT:
		if (!on)
			err = 0;
		/* Only accept a 1-PPS aligned to the second. */
		else if (rq->perout.start.nsec || rq->perout.period.sec != 1 ||
			 rq->perout.period.nsec)
			err = -ERANGE;
		else
			err = 0;
		break;
	case PTP_CLK_REQ_EXTTS:
		if (on) {
			/* Only accept requests for external phase offset */
			if ((rq->extts.flags & PTP_EXT_OFFSET) != (PTP_EXT_OFFSET))
				err = -EOPNOTSUPP;
			else
				err = idtfc3_enable_tdc(idtfc3, true, CONTINUOUS);
		} else {
			err = idtfc3_enable_tdc(idtfc3, false, MEAS_MODE_INVALID);
		}
		break;
	default:
		break;
	}
	mutex_unlock(idtfc3->lock);

	if (err)
		dev_err(idtfc3->dev, "Failed in %s with err %d!", __func__, err);

	return err;
}

static long idtfc3_aux_work(struct ptp_clock_info *ptp)
{
	struct idtfc3 *idtfc3 = container_of(ptp, struct idtfc3, caps);
	static int tdc_get;

	mutex_lock(idtfc3->lock);
	tdc_get %= TDC_GET_PERIOD;
	if ((tdc_get == 0) || (tdc_get == TDC_GET_PERIOD / 2))
		idtfc3_timecounter_read(idtfc3);
	get_tdc_meas_continuous(idtfc3);
	tdc_get++;
	mutex_unlock(idtfc3->lock);

	return idtfc3->tc_update_period;
}

static const struct ptp_clock_info idtfc3_caps = {
	.owner		= THIS_MODULE,
	.max_adj	= MAX_FFO_PPB,
	.n_per_out	= 1,
	.n_ext_ts	= 1,
	.adjphase	= &idtfc3_adjphase,
	.adjfine	= &idtfc3_adjfine,
	.adjtime	= &idtfc3_adjtime,
	.gettime64	= &idtfc3_gettime,
	.settime64	= &idtfc3_settime,
	.enable		= &idtfc3_enable,
	.do_aux_work	= &idtfc3_aux_work,
};

static int idtfc3_hw_calibrate(struct idtfc3 *idtfc3)
{
	int err = 0;
	u8 val;

	mdelay(10);
	/*
	 * Toggle TDC_DAC_RECAL_REQ:
	 * (1) set tdc_en to 1
	 * (2) set tdc_dac_recal_req to 0
	 * (3) set tdc_dac_recal_req to 1
	 */
	val = TDC_EN;
	err = regmap_bulk_write(idtfc3->regmap, TDC_CTRL,
				&val, sizeof(val));
	if (err)
		return err;
	val = TDC_EN | TDC_DAC_RECAL_REQ;
	err = regmap_bulk_write(idtfc3->regmap, TDC_CTRL,
				&val, sizeof(val));
	if (err)
		return err;
	mdelay(10);

	/*
	 * Toggle APLL_REINIT:
	 * (1) set apll_reinit to 0
	 * (2) set apll_reinit to 1
	 */
	val = 0;
	err = regmap_bulk_write(idtfc3->regmap, SOFT_RESET_CTRL,
				&val, sizeof(val));
	if (err)
		return err;
	val = APLL_REINIT;
	err = regmap_bulk_write(idtfc3->regmap, SOFT_RESET_CTRL,
				&val, sizeof(val));
	if (err)
		return err;
	mdelay(10);

	return err;
}

static int idtfc3_init_timecounter(struct idtfc3 *idtfc3)
{
	int err;
	u32 period_ms;

	period_ms = idtfc3->sub_sync_count * MSEC_PER_SEC /
			idtfc3->hw_param.time_clk_freq;

	idtfc3->tc_update_period = msecs_to_jiffies(period_ms / TDC_GET_PERIOD);
	idtfc3->tc_write_timeout = period_ms * USEC_PER_MSEC;

	err = idtfc3_timecounter_update(idtfc3, 0, 0);
	if (err)
		return err;

	err = idtfc3_timecounter_read(idtfc3);
	if (err)
		return err;

	ptp_schedule_worker(idtfc3->ptp_clock, idtfc3->tc_update_period);

	return 0;
}

static int idtfc3_get_tdc_apll_freq(struct idtfc3 *idtfc3)
{
	int err;
	u8 tdc_fb_div_int;
	u8 tdc_ref_div;
	struct idtfc3_hw_param *param = &idtfc3->hw_param;

	err = regmap_bulk_read(idtfc3->regmap, TDC_REF_DIV_CNFG,
				&tdc_ref_div, sizeof(tdc_ref_div));
	if (err)
		return err;

	err = regmap_bulk_read(idtfc3->regmap, TDC_FB_DIV_INT_CNFG,
				&tdc_fb_div_int, sizeof(tdc_fb_div_int));
	if (err)
		return err;

	tdc_fb_div_int &= TDC_FB_DIV_INT_MASK;
	tdc_ref_div &= TDC_REF_DIV_CONFIG_MASK;

	idtfc3->tdc_apll_freq = div_u64(param->xtal_freq * (u64)tdc_fb_div_int,
					1 << tdc_ref_div);

	return 0;
}

static int idtfc3_get_fod(struct idtfc3 *idtfc3)
{
	int err;
	u8 fod;

	err = regmap_bulk_read(idtfc3->regmap, TIME_CLOCK_SRC, &fod, sizeof(fod));
	if (err)
		return err;

	switch (fod) {
	case 0:
		idtfc3->fod_n = FOD_0;
		break;
	case 1:
		idtfc3->fod_n = FOD_1;
		break;
	case 2:
		idtfc3->fod_n = FOD_2;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int idtfc3_get_sync_count(struct idtfc3 *idtfc3)
{
	int err;
	u8 buf[4];

	err = regmap_bulk_read(idtfc3->regmap, SUB_SYNC_GEN_CNFG, buf, sizeof(buf));
	if (err)
		return err;

	idtfc3->sub_sync_count = (get_unaligned_le32(buf) & SUB_SYNC_COUNTER_MASK) + 1;
	idtfc3->ns_per_counter = NSEC_PER_SEC / idtfc3->hw_param.time_clk_freq;
	idtfc3->ns_per_sync = idtfc3->sub_sync_count * idtfc3->ns_per_counter;

	return 0;
}

static int idtfc3_setup_hw_param(struct idtfc3 *idtfc3)
{
	int err;

	err = idtfc3_get_fod(idtfc3);
	if (err)
		return err;

	err = idtfc3_get_sync_count(idtfc3);
	if (err)
		return err;

	err = idtfc3_get_time_ref_freq(idtfc3);
	if (err)
		return err;

	return idtfc3_get_tdc_apll_freq(idtfc3);
}

static int idtfc3_configure_hw(struct idtfc3 *idtfc3)
{
	int err = 0;

	err = idtfc3_hw_calibrate(idtfc3);
	if (err)
		return err;

	err = idtfc3_enable_lpf(idtfc3, true);
	if (err)
		return err;

	err = idtfc3_enable_tdc(idtfc3, false, MEAS_MODE_INVALID);
	if (err)
		return err;

	err = idtfc3_get_tdc_offset_sign(idtfc3);
	if (err)
		return err;

	return idtfc3_setup_hw_param(idtfc3);
}

static int idtfc3_set_overhead(struct idtfc3 *idtfc3)
{
	s64 current_ns = 0;
	s64 lowest_ns = 0;
	int err;
	u8 i;
	ktime_t start;
	ktime_t stop;
	ktime_t diff;

	char buf[18] = {0};

	for (i = 0; i < 5; i++) {
		start = ktime_get_raw();

		err = regmap_bulk_write(idtfc3->regmap, TOD_SYNC_LOAD_VAL_CTRL,
					&buf, sizeof(buf));
		if (err)
			return err;

		stop = ktime_get_raw();

		diff = ktime_sub(stop, start);

		current_ns = ktime_to_ns(diff);

		if (i == 0) {
			lowest_ns = current_ns;
		} else {
			if (current_ns < lowest_ns)
				lowest_ns = current_ns;
		}
	}

	idtfc3->tod_write_overhead = lowest_ns;

	return err;
}

static int idtfc3_enable_ptp(struct idtfc3 *idtfc3)
{
	int err;

	idtfc3->caps = idtfc3_caps;
	snprintf(idtfc3->caps.name, sizeof(idtfc3->caps.name), "IDT FC3W");
	idtfc3->ptp_clock = ptp_clock_register(&idtfc3->caps, NULL);

	if (IS_ERR(idtfc3->ptp_clock)) {
		err = PTR_ERR(idtfc3->ptp_clock);
		idtfc3->ptp_clock = NULL;
		return err;
	}

	err = idtfc3_set_overhead(idtfc3);
	if (err)
		return err;

	err = idtfc3_init_timecounter(idtfc3);
	if (err)
		return err;

	dev_info(idtfc3->dev, "TIME_SYNC_CHANNEL registered as ptp%d",
		 idtfc3->ptp_clock->index);

	return 0;
}

static int idtfc3_load_firmware(struct idtfc3 *idtfc3)
{
	char fname[128] = FW_FILENAME;
	const struct firmware *fw;
	struct idtfc3_fwrc *rec;
	u16 addr;
	u8 val;
	int err;
	s32 len;

	idtfc3_default_hw_param(&idtfc3->hw_param);

	if (firmware) /* module parameter */
		snprintf(fname, sizeof(fname), "%s", firmware);

	dev_info(idtfc3->dev, "requesting firmware '%s'\n", fname);

	err = request_firmware(&fw, fname, idtfc3->dev);

	if (err) {
		dev_err(idtfc3->dev,
			"requesting firmware failed with err %d!\n", err);
		return err;
	}

	dev_dbg(idtfc3->dev, "firmware size %zu bytes\n", fw->size);

	rec = (struct idtfc3_fwrc *)fw->data;

	for (len = fw->size; len > 0; len -= sizeof(*rec)) {
		if (rec->reserved) {
			dev_err(idtfc3->dev,
				"bad firmware, reserved field non-zero\n");
			err = -EINVAL;
		} else {
			val = rec->value;
			addr = rec->hiaddr << 8 | rec->loaddr;

			rec++;

			err = idtfc3_set_hw_param(&idtfc3->hw_param, addr, val);
		}

		if (err != -EINVAL) {
			err = 0;

			/* Max register */
			if (addr >= 0xE88)
				continue;

			err = regmap_bulk_write(idtfc3->regmap, addr,
						&val, sizeof(val));
		}

		if (err)
			goto out;
	}

	err = idtfc3_configure_hw(idtfc3);
out:
	release_firmware(fw);
	return err;
}

static int idtfc3_read_device_id(struct idtfc3 *idtfc3, u16 *device_id)
{
	int err;
	u8 buf[2] = {0};

	err = regmap_bulk_read(idtfc3->regmap, DEVICE_ID,
			       &buf, sizeof(buf));
	if (err) {
		dev_err(idtfc3->dev, "%s failed with %d", __func__, err);
		return err;
	}

	*device_id = get_unaligned_le16(buf);

	return 0;
}

static int idtfc3_check_device_compatibility(struct idtfc3 *idtfc3)
{
	int err;
	u16 device_id;

	err = idtfc3_read_device_id(idtfc3, &device_id);
	if (err)
		return err;

	if ((device_id & DEVICE_ID_MASK) == 0) {
		dev_err(idtfc3->dev, "invalid device");
		return -EINVAL;
	}

	return 0;
}

static int idtfc3_probe(struct platform_device *pdev)
{
	struct rsmu_ddata *ddata = dev_get_drvdata(pdev->dev.parent);
	struct idtfc3 *idtfc3;
	int err;

	idtfc3 = devm_kzalloc(&pdev->dev, sizeof(struct idtfc3), GFP_KERNEL);

	if (!idtfc3)
		return -ENOMEM;

	idtfc3->dev = &pdev->dev;
	idtfc3->mfd = pdev->dev.parent;
	idtfc3->lock = &ddata->lock;
	idtfc3->regmap = ddata->regmap;

	mutex_lock(idtfc3->lock);

	err = idtfc3_check_device_compatibility(idtfc3);
	if (err) {
		mutex_unlock(idtfc3->lock);
		return err;
	}

	err = idtfc3_load_firmware(idtfc3);
	if (err) {
		if (err == -ENOENT) {
			mutex_unlock(idtfc3->lock);
			return -EPROBE_DEFER;
		}
		dev_warn(idtfc3->dev, "loading firmware failed with %d", err);
	}

	err = idtfc3_enable_ptp(idtfc3);
	if (err) {
		dev_err(idtfc3->dev, "idtfc3_enable_ptp failed with %d", err);
		mutex_unlock(idtfc3->lock);
		return err;
	}

	mutex_unlock(idtfc3->lock);

	if (err) {
		ptp_clock_unregister(idtfc3->ptp_clock);
		return err;
	}

	platform_set_drvdata(pdev, idtfc3);

	return 0;
}

static void idtfc3_remove(struct platform_device *pdev)
{
	struct idtfc3 *idtfc3 = platform_get_drvdata(pdev);

	ptp_clock_unregister(idtfc3->ptp_clock);
}

static struct platform_driver idtfc3_driver = {
	.driver = {
		.name = "rc38xxx-phc",
	},
	.probe = idtfc3_probe,
	.remove_new = idtfc3_remove,
};

module_platform_driver(idtfc3_driver);
