// SPDX-License-Identifier: GPL-2.0+
/*
 * PTP hardware clock driver for the IDT ClockMatrix(TM) family of timing and
 * synchronization devices.
 *
 * Copyright (C) 2019 Integrated Device Technology, Inc., a Renesas Company.
 */
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/timekeeping.h>

#include "ptp_private.h"
#include "ptp_clockmatrix.h"

MODULE_DESCRIPTION("Driver for IDT ClockMatrix(TM) family");
MODULE_AUTHOR("Richard Cochran <richardcochran@gmail.com>");
MODULE_AUTHOR("IDT support-1588 <IDT-support-1588@lm.renesas.com>");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");

#define SETTIME_CORRECTION (0)

static int char_array_to_timespec(u8 *buf,
				  u8 count,
				  struct timespec64 *ts)
{
	u8 i;
	u64 nsec;
	time64_t sec;

	if (count < TOD_BYTE_COUNT)
		return 1;

	/* Sub-nanoseconds are in buf[0]. */
	nsec = buf[4];
	for (i = 0; i < 3; i++) {
		nsec <<= 8;
		nsec |= buf[3 - i];
	}

	sec = buf[10];
	for (i = 0; i < 5; i++) {
		sec <<= 8;
		sec |= buf[9 - i];
	}

	ts->tv_sec = sec;
	ts->tv_nsec = nsec;

	return 0;
}

static int timespec_to_char_array(struct timespec64 const *ts,
				  u8 *buf,
				  u8 count)
{
	u8 i;
	s32 nsec;
	time64_t sec;

	if (count < TOD_BYTE_COUNT)
		return 1;

	nsec = ts->tv_nsec;
	sec = ts->tv_sec;

	/* Sub-nanoseconds are in buf[0]. */
	buf[0] = 0;
	for (i = 1; i < 5; i++) {
		buf[i] = nsec & 0xff;
		nsec >>= 8;
	}

	for (i = 5; i < TOD_BYTE_COUNT; i++) {

		buf[i] = sec & 0xff;
		sec >>= 8;
	}

	return 0;
}

static int idtcm_xfer(struct idtcm *idtcm,
		      u8 regaddr,
		      u8 *buf,
		      u16 count,
		      bool write)
{
	struct i2c_client *client = idtcm->client;
	struct i2c_msg msg[2];
	int cnt;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &regaddr;

	msg[1].addr = client->addr;
	msg[1].flags = write ? 0 : I2C_M_RD;
	msg[1].len = count;
	msg[1].buf = buf;

	cnt = i2c_transfer(client->adapter, msg, 2);

	if (cnt < 0) {
		dev_err(&client->dev, "i2c_transfer returned %d\n", cnt);
		return cnt;
	} else if (cnt != 2) {
		dev_err(&client->dev,
			"i2c_transfer sent only %d of %d messages\n", cnt, 2);
		return -EIO;
	}

	return 0;
}

static int idtcm_page_offset(struct idtcm *idtcm, u8 val)
{
	u8 buf[4];
	int err;

	if (idtcm->page_offset == val)
		return 0;

	buf[0] = 0x0;
	buf[1] = val;
	buf[2] = 0x10;
	buf[3] = 0x20;

	err = idtcm_xfer(idtcm, PAGE_ADDR, buf, sizeof(buf), 1);

	if (err)
		dev_err(&idtcm->client->dev, "failed to set page offset\n");
	else
		idtcm->page_offset = val;

	return err;
}

static int _idtcm_rdwr(struct idtcm *idtcm,
		       u16 regaddr,
		       u8 *buf,
		       u16 count,
		       bool write)
{
	u8 hi;
	u8 lo;
	int err;

	hi = (regaddr >> 8) & 0xff;
	lo = regaddr & 0xff;

	err = idtcm_page_offset(idtcm, hi);

	if (err)
		goto out;

	err = idtcm_xfer(idtcm, lo, buf, count, write);
out:
	return err;
}

static int idtcm_read(struct idtcm *idtcm,
		      u16 module,
		      u16 regaddr,
		      u8 *buf,
		      u16 count)
{
	return _idtcm_rdwr(idtcm, module + regaddr, buf, count, false);
}

static int idtcm_write(struct idtcm *idtcm,
		       u16 module,
		       u16 regaddr,
		       u8 *buf,
		       u16 count)
{
	return _idtcm_rdwr(idtcm, module + regaddr, buf, count, true);
}

static int _idtcm_gettime(struct idtcm_channel *channel,
			  struct timespec64 *ts)
{
	struct idtcm *idtcm = channel->idtcm;
	u8 buf[TOD_BYTE_COUNT];
	u8 trigger;
	int err;

	err = idtcm_read(idtcm, channel->tod_read_primary,
			 TOD_READ_PRIMARY_CMD, &trigger, sizeof(trigger));
	if (err)
		return err;

	trigger &= ~(TOD_READ_TRIGGER_MASK << TOD_READ_TRIGGER_SHIFT);
	trigger |= (1 << TOD_READ_TRIGGER_SHIFT);
	trigger |= TOD_READ_TRIGGER_MODE;

	err = idtcm_write(idtcm, channel->tod_read_primary,
			  TOD_READ_PRIMARY_CMD, &trigger, sizeof(trigger));

	if (err)
		return err;

	if (idtcm->calculate_overhead_flag)
		idtcm->start_time = ktime_get_raw();

	err = idtcm_read(idtcm, channel->tod_read_primary,
			 TOD_READ_PRIMARY, buf, sizeof(buf));

	if (err)
		return err;

	err = char_array_to_timespec(buf, sizeof(buf), ts);

	return err;
}

static int _sync_pll_output(struct idtcm *idtcm,
			    u8 pll,
			    u8 sync_src,
			    u8 qn,
			    u8 qn_plus_1)
{
	int err;
	u8 val;
	u16 sync_ctrl0;
	u16 sync_ctrl1;

	if ((qn == 0) && (qn_plus_1 == 0))
		return 0;

	switch (pll) {
	case 0:
		sync_ctrl0 = HW_Q0_Q1_CH_SYNC_CTRL_0;
		sync_ctrl1 = HW_Q0_Q1_CH_SYNC_CTRL_1;
		break;
	case 1:
		sync_ctrl0 = HW_Q2_Q3_CH_SYNC_CTRL_0;
		sync_ctrl1 = HW_Q2_Q3_CH_SYNC_CTRL_1;
		break;
	case 2:
		sync_ctrl0 = HW_Q4_Q5_CH_SYNC_CTRL_0;
		sync_ctrl1 = HW_Q4_Q5_CH_SYNC_CTRL_1;
		break;
	case 3:
		sync_ctrl0 = HW_Q6_Q7_CH_SYNC_CTRL_0;
		sync_ctrl1 = HW_Q6_Q7_CH_SYNC_CTRL_1;
		break;
	case 4:
		sync_ctrl0 = HW_Q8_CH_SYNC_CTRL_0;
		sync_ctrl1 = HW_Q8_CH_SYNC_CTRL_1;
		break;
	case 5:
		sync_ctrl0 = HW_Q9_CH_SYNC_CTRL_0;
		sync_ctrl1 = HW_Q9_CH_SYNC_CTRL_1;
		break;
	case 6:
		sync_ctrl0 = HW_Q10_CH_SYNC_CTRL_0;
		sync_ctrl1 = HW_Q10_CH_SYNC_CTRL_1;
		break;
	case 7:
		sync_ctrl0 = HW_Q11_CH_SYNC_CTRL_0;
		sync_ctrl1 = HW_Q11_CH_SYNC_CTRL_1;
		break;
	default:
		return -EINVAL;
	}

	val = SYNCTRL1_MASTER_SYNC_RST;

	/* Place master sync in reset */
	err = idtcm_write(idtcm, 0, sync_ctrl1, &val, sizeof(val));
	if (err)
		return err;

	err = idtcm_write(idtcm, 0, sync_ctrl0, &sync_src, sizeof(sync_src));
	if (err)
		return err;

	/* Set sync trigger mask */
	val |= SYNCTRL1_FBDIV_FRAME_SYNC_TRIG | SYNCTRL1_FBDIV_SYNC_TRIG;

	if (qn)
		val |= SYNCTRL1_Q0_DIV_SYNC_TRIG;

	if (qn_plus_1)
		val |= SYNCTRL1_Q1_DIV_SYNC_TRIG;

	err = idtcm_write(idtcm, 0, sync_ctrl1, &val, sizeof(val));
	if (err)
		return err;

	/* Place master sync out of reset */
	val &= ~(SYNCTRL1_MASTER_SYNC_RST);
	err = idtcm_write(idtcm, 0, sync_ctrl1, &val, sizeof(val));

	return err;
}

static int idtcm_sync_pps_output(struct idtcm_channel *channel)
{
	struct idtcm *idtcm = channel->idtcm;

	u8 pll;
	u8 sync_src;
	u8 qn;
	u8 qn_plus_1;
	int err = 0;

	u16 output_mask = channel->output_mask;

	switch (channel->dpll_n) {
	case DPLL_0:
		sync_src = SYNC_SOURCE_DPLL0_TOD_PPS;
		break;
	case DPLL_1:
		sync_src = SYNC_SOURCE_DPLL1_TOD_PPS;
		break;
	case DPLL_2:
		sync_src = SYNC_SOURCE_DPLL2_TOD_PPS;
		break;
	case DPLL_3:
		sync_src = SYNC_SOURCE_DPLL3_TOD_PPS;
		break;
	default:
		return -EINVAL;
	}

	for (pll = 0; pll < 8; pll++) {

		qn = output_mask & 0x1;
		output_mask = output_mask >> 1;

		if (pll < 4) {
			/* First 4 pll has 2 outputs */
			qn_plus_1 = output_mask & 0x1;
			output_mask = output_mask >> 1;
		} else {
			qn_plus_1 = 0;
		}

		if ((qn != 0) || (qn_plus_1 != 0))
			err = _sync_pll_output(idtcm, pll, sync_src, qn,
					       qn_plus_1);

		if (err)
			return err;
	}

	return err;
}

static int _idtcm_set_dpll_tod(struct idtcm_channel *channel,
			       struct timespec64 const *ts,
			       enum hw_tod_write_trig_sel wr_trig)
{
	struct idtcm *idtcm = channel->idtcm;

	u8 buf[TOD_BYTE_COUNT];
	u8 cmd;
	int err;
	struct timespec64 local_ts = *ts;
	s64 total_overhead_ns;

	/* Configure HW TOD write trigger. */
	err = idtcm_read(idtcm, channel->hw_dpll_n, HW_DPLL_TOD_CTRL_1,
			 &cmd, sizeof(cmd));

	if (err)
		return err;

	cmd &= ~(0x0f);
	cmd |= wr_trig | 0x08;

	err = idtcm_write(idtcm, channel->hw_dpll_n, HW_DPLL_TOD_CTRL_1,
			  &cmd, sizeof(cmd));

	if (err)
		return err;

	if (wr_trig  != HW_TOD_WR_TRIG_SEL_MSB) {

		err = timespec_to_char_array(&local_ts, buf, sizeof(buf));

		if (err)
			return err;

		err = idtcm_write(idtcm, channel->hw_dpll_n,
				  HW_DPLL_TOD_OVR__0, buf, sizeof(buf));

		if (err)
			return err;
	}

	/* ARM HW TOD write trigger. */
	cmd &= ~(0x08);

	err = idtcm_write(idtcm, channel->hw_dpll_n, HW_DPLL_TOD_CTRL_1,
			  &cmd, sizeof(cmd));

	if (wr_trig == HW_TOD_WR_TRIG_SEL_MSB) {

		if (idtcm->calculate_overhead_flag) {
			total_overhead_ns =  ktime_to_ns(ktime_get_raw()
							 - idtcm->start_time)
					     + idtcm->tod_write_overhead_ns
					     + SETTIME_CORRECTION;

			timespec64_add_ns(&local_ts, total_overhead_ns);

			idtcm->calculate_overhead_flag = 0;
		}

		err = timespec_to_char_array(&local_ts, buf, sizeof(buf));

		if (err)
			return err;

		err = idtcm_write(idtcm, channel->hw_dpll_n,
				  HW_DPLL_TOD_OVR__0, buf, sizeof(buf));
	}

	return err;
}

static int _idtcm_settime(struct idtcm_channel *channel,
			  struct timespec64 const *ts,
			  enum hw_tod_write_trig_sel wr_trig)
{
	struct idtcm *idtcm = channel->idtcm;
	s32 retval;
	int err;
	int i;
	u8 trig_sel;

	err = _idtcm_set_dpll_tod(channel, ts, wr_trig);

	if (err)
		return err;

	/* Wait for the operation to complete. */
	for (i = 0; i < 10000; i++) {
		err = idtcm_read(idtcm, channel->hw_dpll_n,
				 HW_DPLL_TOD_CTRL_1, &trig_sel,
				 sizeof(trig_sel));

		if (err)
			return err;

		if (trig_sel == 0x4a)
			break;

		err = 1;
	}

	if (err)
		return err;

	retval = idtcm_sync_pps_output(channel);

	return retval;
}

static int idtcm_set_phase_pull_in_offset(struct idtcm_channel *channel,
					  s32 offset_ns)
{
	int err;
	int i;
	struct idtcm *idtcm = channel->idtcm;

	u8 buf[4];

	for (i = 0; i < 4; i++) {
		buf[i] = 0xff & (offset_ns);
		offset_ns >>= 8;
	}

	err = idtcm_write(idtcm, channel->dpll_phase_pull_in, PULL_IN_OFFSET,
			  buf, sizeof(buf));

	return err;
}

static int idtcm_set_phase_pull_in_slope_limit(struct idtcm_channel *channel,
					       u32 max_ffo_ppb)
{
	int err;
	u8 i;
	struct idtcm *idtcm = channel->idtcm;

	u8 buf[3];

	if (max_ffo_ppb & 0xff000000)
		max_ffo_ppb = 0;

	for (i = 0; i < 3; i++) {
		buf[i] = 0xff & (max_ffo_ppb);
		max_ffo_ppb >>= 8;
	}

	err = idtcm_write(idtcm, channel->dpll_phase_pull_in,
			  PULL_IN_SLOPE_LIMIT, buf, sizeof(buf));

	return err;
}

static int idtcm_start_phase_pull_in(struct idtcm_channel *channel)
{
	int err;
	struct idtcm *idtcm = channel->idtcm;

	u8 buf;

	err = idtcm_read(idtcm, channel->dpll_phase_pull_in, PULL_IN_CTRL,
			 &buf, sizeof(buf));

	if (err)
		return err;

	if (buf == 0) {
		buf = 0x01;
		err = idtcm_write(idtcm, channel->dpll_phase_pull_in,
				  PULL_IN_CTRL, &buf, sizeof(buf));
	} else {
		err = -EBUSY;
	}

	return err;
}

static int idtcm_do_phase_pull_in(struct idtcm_channel *channel,
				  s32 offset_ns,
				  u32 max_ffo_ppb)
{
	int err;

	err = idtcm_set_phase_pull_in_offset(channel, -offset_ns);

	if (err)
		return err;

	err = idtcm_set_phase_pull_in_slope_limit(channel, max_ffo_ppb);

	if (err)
		return err;

	err = idtcm_start_phase_pull_in(channel);

	return err;
}

static int _idtcm_adjtime(struct idtcm_channel *channel, s64 delta)
{
	int err;
	struct idtcm *idtcm = channel->idtcm;
	struct timespec64 ts;
	s64 now;

	if (abs(delta) < PHASE_PULL_IN_THRESHOLD_NS) {
		err = idtcm_do_phase_pull_in(channel, delta, 0);
	} else {
		idtcm->calculate_overhead_flag = 1;

		err = _idtcm_gettime(channel, &ts);

		if (err)
			return err;

		now = timespec64_to_ns(&ts);
		now += delta;

		ts = ns_to_timespec64(now);

		err = _idtcm_settime(channel, &ts, HW_TOD_WR_TRIG_SEL_MSB);
	}

	return err;
}

static int idtcm_state_machine_reset(struct idtcm *idtcm)
{
	int err;
	u8 byte = SM_RESET_CMD;

	err = idtcm_write(idtcm, RESET_CTRL, SM_RESET, &byte, sizeof(byte));

	if (!err)
		msleep_interruptible(POST_SM_RESET_DELAY_MS);

	return err;
}

static int idtcm_read_hw_rev_id(struct idtcm *idtcm, u8 *hw_rev_id)
{
	return idtcm_read(idtcm,
			  GENERAL_STATUS,
			  HW_REV_ID,
			  hw_rev_id,
			  sizeof(u8));
}

static int idtcm_read_bond_id(struct idtcm *idtcm, u8 *bond_id)
{
	return idtcm_read(idtcm,
			  GENERAL_STATUS,
			  BOND_ID,
			  bond_id,
			  sizeof(u8));
}

static int idtcm_read_hw_csr_id(struct idtcm *idtcm, u16 *hw_csr_id)
{
	int err;
	u8 buf[2] = {0};

	err = idtcm_read(idtcm, GENERAL_STATUS, HW_CSR_ID, buf, sizeof(buf));

	*hw_csr_id = (buf[1] << 8) | buf[0];

	return err;
}

static int idtcm_read_hw_irq_id(struct idtcm *idtcm, u16 *hw_irq_id)
{
	int err;
	u8 buf[2] = {0};

	err = idtcm_read(idtcm, GENERAL_STATUS, HW_IRQ_ID, buf, sizeof(buf));

	*hw_irq_id = (buf[1] << 8) | buf[0];

	return err;
}

static int idtcm_read_product_id(struct idtcm *idtcm, u16 *product_id)
{
	int err;
	u8 buf[2] = {0};

	err = idtcm_read(idtcm, GENERAL_STATUS, PRODUCT_ID, buf, sizeof(buf));

	*product_id = (buf[1] << 8) | buf[0];

	return err;
}

static int idtcm_read_major_release(struct idtcm *idtcm, u8 *major)
{
	int err;
	u8 buf = 0;

	err = idtcm_read(idtcm, GENERAL_STATUS, MAJ_REL, &buf, sizeof(buf));

	*major = buf >> 1;

	return err;
}

static int idtcm_read_minor_release(struct idtcm *idtcm, u8 *minor)
{
	return idtcm_read(idtcm, GENERAL_STATUS, MIN_REL, minor, sizeof(u8));
}

static int idtcm_read_hotfix_release(struct idtcm *idtcm, u8 *hotfix)
{
	return idtcm_read(idtcm,
			  GENERAL_STATUS,
			  HOTFIX_REL,
			  hotfix,
			  sizeof(u8));
}

static int idtcm_read_pipeline(struct idtcm *idtcm, u32 *pipeline)
{
	int err;
	u8 buf[4] = {0};

	err = idtcm_read(idtcm,
			 GENERAL_STATUS,
			 PIPELINE_ID,
			 &buf[0],
			 sizeof(buf));

	*pipeline = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];

	return err;
}

static int process_pll_mask(struct idtcm *idtcm, u32 addr, u8 val, u8 *mask)
{
	int err = 0;

	if (addr == PLL_MASK_ADDR) {
		if ((val & 0xf0) || !(val & 0xf)) {
			dev_err(&idtcm->client->dev,
				"Invalid PLL mask 0x%hhx\n", val);
			err = -EINVAL;
		}
		*mask = val;
	}

	return err;
}

static int set_pll_output_mask(struct idtcm *idtcm, u16 addr, u8 val)
{
	int err = 0;

	switch (addr) {
	case OUTPUT_MASK_PLL0_ADDR:
		SET_U16_LSB(idtcm->channel[0].output_mask, val);
		break;
	case OUTPUT_MASK_PLL0_ADDR + 1:
		SET_U16_MSB(idtcm->channel[0].output_mask, val);
		break;
	case OUTPUT_MASK_PLL1_ADDR:
		SET_U16_LSB(idtcm->channel[1].output_mask, val);
		break;
	case OUTPUT_MASK_PLL1_ADDR + 1:
		SET_U16_MSB(idtcm->channel[1].output_mask, val);
		break;
	case OUTPUT_MASK_PLL2_ADDR:
		SET_U16_LSB(idtcm->channel[2].output_mask, val);
		break;
	case OUTPUT_MASK_PLL2_ADDR + 1:
		SET_U16_MSB(idtcm->channel[2].output_mask, val);
		break;
	case OUTPUT_MASK_PLL3_ADDR:
		SET_U16_LSB(idtcm->channel[3].output_mask, val);
		break;
	case OUTPUT_MASK_PLL3_ADDR + 1:
		SET_U16_MSB(idtcm->channel[3].output_mask, val);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int check_and_set_masks(struct idtcm *idtcm,
			       u16 regaddr,
			       u8 val)
{
	int err = 0;

	if (set_pll_output_mask(idtcm, regaddr, val)) {
		/* Not an output mask, check for pll mask */
		err = process_pll_mask(idtcm, regaddr, val, &idtcm->pll_mask);
	}

	return err;
}

static void display_pll_and_output_masks(struct idtcm *idtcm)
{
	u8 i;
	u8 mask;

	dev_dbg(&idtcm->client->dev, "pllmask = 0x%02x\n", idtcm->pll_mask);

	for (i = 0; i < MAX_PHC_PLL; i++) {
		mask = 1 << i;

		if (mask & idtcm->pll_mask)
			dev_dbg(&idtcm->client->dev,
				"PLL%d output_mask = 0x%04x\n",
				i, idtcm->channel[i].output_mask);
	}
}

static int idtcm_load_firmware(struct idtcm *idtcm,
			       struct device *dev)
{
	const struct firmware *fw;
	struct idtcm_fwrc *rec;
	u32 regaddr;
	int err;
	s32 len;
	u8 val;
	u8 loaddr;

	dev_dbg(&idtcm->client->dev, "requesting firmware '%s'\n", FW_FILENAME);

	err = request_firmware(&fw, FW_FILENAME, dev);

	if (err)
		return err;

	dev_dbg(&idtcm->client->dev, "firmware size %zu bytes\n", fw->size);

	rec = (struct idtcm_fwrc *) fw->data;

	if (fw->size > 0)
		idtcm_state_machine_reset(idtcm);

	for (len = fw->size; len > 0; len -= sizeof(*rec)) {

		if (rec->reserved) {
			dev_err(&idtcm->client->dev,
				"bad firmware, reserved field non-zero\n");
			err = -EINVAL;
		} else {
			regaddr = rec->hiaddr << 8;
			regaddr |= rec->loaddr;

			val = rec->value;
			loaddr = rec->loaddr;

			rec++;

			err = check_and_set_masks(idtcm, regaddr, val);
		}

		if (err == 0) {
			/* Top (status registers) and bottom are read-only */
			if ((regaddr < GPIO_USER_CONTROL)
			    || (regaddr >= SCRATCH))
				continue;

			/* Page size 128, last 4 bytes of page skipped */
			if (((loaddr > 0x7b) && (loaddr <= 0x7f))
			     || ((loaddr > 0xfb) && (loaddr <= 0xff)))
				continue;

			err = idtcm_write(idtcm, regaddr, 0, &val, sizeof(val));
		}

		if (err)
			goto out;
	}

	display_pll_and_output_masks(idtcm);

out:
	release_firmware(fw);
	return err;
}

static int idtcm_pps_enable(struct idtcm_channel *channel, bool enable)
{
	struct idtcm *idtcm = channel->idtcm;
	u32 module;
	u8 val;
	int err;

	/*
	 * This assumes that the 1-PPS is on the second of the two
	 * output.  But is this always true?
	 */
	switch (channel->dpll_n) {
	case DPLL_0:
		module = OUTPUT_1;
		break;
	case DPLL_1:
		module = OUTPUT_3;
		break;
	case DPLL_2:
		module = OUTPUT_5;
		break;
	case DPLL_3:
		module = OUTPUT_7;
		break;
	default:
		return -EINVAL;
	}

	err = idtcm_read(idtcm, module, OUT_CTRL_1, &val, sizeof(val));

	if (err)
		return err;

	if (enable)
		val |= SQUELCH_DISABLE;
	else
		val &= ~SQUELCH_DISABLE;

	err = idtcm_write(idtcm, module, OUT_CTRL_1, &val, sizeof(val));

	if (err)
		return err;

	return 0;
}

static int idtcm_set_pll_mode(struct idtcm_channel *channel,
			      enum pll_mode pll_mode)
{
	struct idtcm *idtcm = channel->idtcm;
	int err;
	u8 dpll_mode;

	err = idtcm_read(idtcm, channel->dpll_n, DPLL_MODE,
			 &dpll_mode, sizeof(dpll_mode));
	if (err)
		return err;

	dpll_mode &= ~(PLL_MODE_MASK << PLL_MODE_SHIFT);

	dpll_mode |= (pll_mode << PLL_MODE_SHIFT);

	channel->pll_mode = pll_mode;

	err = idtcm_write(idtcm, channel->dpll_n, DPLL_MODE,
			  &dpll_mode, sizeof(dpll_mode));
	if (err)
		return err;

	return 0;
}

/* PTP Hardware Clock interface */

static int idtcm_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct idtcm_channel *channel =
		container_of(ptp, struct idtcm_channel, caps);
	struct idtcm *idtcm = channel->idtcm;
	u8 i;
	bool neg_adj = 0;
	int err;
	u8 buf[6] = {0};
	s64 fcw;

	if (channel->pll_mode  != PLL_MODE_WRITE_FREQUENCY) {
		err = idtcm_set_pll_mode(channel, PLL_MODE_WRITE_FREQUENCY);
		if (err)
			return err;
	}

	/*
	 * Frequency Control Word unit is: 1.11 * 10^-10 ppm
	 *
	 * adjfreq:
	 *       ppb * 10^9
	 * FCW = ----------
	 *          111
	 *
	 * adjfine:
	 *       ppm_16 * 5^12
	 * FCW = -------------
	 *         111 * 2^4
	 */
	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}

	/* 2 ^ -53 = 1.1102230246251565404236316680908e-16 */
	fcw = ppb * 1000000000000ULL;

	fcw = div_u64(fcw, 111022);

	if (neg_adj)
		fcw = -fcw;

	for (i = 0; i < 6; i++) {
		buf[i] = fcw & 0xff;
		fcw >>= 8;
	}

	mutex_lock(&idtcm->reg_lock);

	err = idtcm_write(idtcm, channel->dpll_freq, DPLL_WR_FREQ,
			  buf, sizeof(buf));

	mutex_unlock(&idtcm->reg_lock);
	return err;
}

static int idtcm_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct idtcm_channel *channel =
		container_of(ptp, struct idtcm_channel, caps);
	struct idtcm *idtcm = channel->idtcm;
	int err;

	mutex_lock(&idtcm->reg_lock);

	err = _idtcm_gettime(channel, ts);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_settime(struct ptp_clock_info *ptp,
			 const struct timespec64 *ts)
{
	struct idtcm_channel *channel =
		container_of(ptp, struct idtcm_channel, caps);
	struct idtcm *idtcm = channel->idtcm;
	int err;

	mutex_lock(&idtcm->reg_lock);

	err = _idtcm_settime(channel, ts, HW_TOD_WR_TRIG_SEL_MSB);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct idtcm_channel *channel =
		container_of(ptp, struct idtcm_channel, caps);
	struct idtcm *idtcm = channel->idtcm;
	int err;

	mutex_lock(&idtcm->reg_lock);

	err = _idtcm_adjtime(channel, delta);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_enable(struct ptp_clock_info *ptp,
			struct ptp_clock_request *rq, int on)
{
	struct idtcm_channel *channel =
		container_of(ptp, struct idtcm_channel, caps);

	switch (rq->type) {
	case PTP_CLK_REQ_PEROUT:
		if (!on)
			return idtcm_pps_enable(channel, false);

		/* Only accept a 1-PPS aligned to the second. */
		if (rq->perout.start.nsec || rq->perout.period.sec != 1 ||
		    rq->perout.period.nsec)
			return -ERANGE;

		return idtcm_pps_enable(channel, true);
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int idtcm_enable_tod(struct idtcm_channel *channel)
{
	struct idtcm *idtcm = channel->idtcm;
	struct timespec64 ts = {0, 0};
	u8 cfg;
	int err;

	err = idtcm_pps_enable(channel, false);
	if (err)
		return err;

	/*
	 * Start the TOD clock ticking.
	 */
	err = idtcm_read(idtcm, channel->tod_n, TOD_CFG, &cfg, sizeof(cfg));
	if (err)
		return err;

	cfg |= TOD_ENABLE;

	err = idtcm_write(idtcm, channel->tod_n, TOD_CFG, &cfg, sizeof(cfg));
	if (err)
		return err;

	return _idtcm_settime(channel, &ts, HW_TOD_WR_TRIG_SEL_MSB);
}

static void idtcm_display_version_info(struct idtcm *idtcm)
{
	u8 major;
	u8 minor;
	u8 hotfix;
	u32 pipeline;
	u16 product_id;
	u16 csr_id;
	u16 irq_id;
	u8 hw_rev_id;
	u8 bond_id;

	idtcm_read_major_release(idtcm, &major);
	idtcm_read_minor_release(idtcm, &minor);
	idtcm_read_hotfix_release(idtcm, &hotfix);
	idtcm_read_pipeline(idtcm, &pipeline);

	idtcm_read_product_id(idtcm, &product_id);
	idtcm_read_hw_rev_id(idtcm, &hw_rev_id);
	idtcm_read_bond_id(idtcm, &bond_id);
	idtcm_read_hw_csr_id(idtcm, &csr_id);
	idtcm_read_hw_irq_id(idtcm, &irq_id);

	dev_info(&idtcm->client->dev, "Version:  %d.%d.%d, Pipeline %u\t"
		 "0x%04x, Rev %d, Bond %d, CSR %d, IRQ %d\n",
		 major, minor, hotfix, pipeline,
		 product_id, hw_rev_id, bond_id, csr_id, irq_id);
}

static struct ptp_clock_info idtcm_caps = {
	.owner		= THIS_MODULE,
	.max_adj	= 244000,
	.n_per_out	= 1,
	.adjfreq	= &idtcm_adjfreq,
	.adjtime	= &idtcm_adjtime,
	.gettime64	= &idtcm_gettime,
	.settime64	= &idtcm_settime,
	.enable		= &idtcm_enable,
};

static int idtcm_enable_channel(struct idtcm *idtcm, u32 index)
{
	struct idtcm_channel *channel;
	int err;

	if (!(index < MAX_PHC_PLL))
		return -EINVAL;

	channel = &idtcm->channel[index];

	switch (index) {
	case 0:
		channel->dpll_freq = DPLL_FREQ_0;
		channel->dpll_n = DPLL_0;
		channel->tod_read_primary = TOD_READ_PRIMARY_0;
		channel->tod_write = TOD_WRITE_0;
		channel->tod_n = TOD_0;
		channel->hw_dpll_n = HW_DPLL_0;
		channel->dpll_phase = DPLL_PHASE_0;
		channel->dpll_ctrl_n = DPLL_CTRL_0;
		channel->dpll_phase_pull_in = DPLL_PHASE_PULL_IN_0;
		break;
	case 1:
		channel->dpll_freq = DPLL_FREQ_1;
		channel->dpll_n = DPLL_1;
		channel->tod_read_primary = TOD_READ_PRIMARY_1;
		channel->tod_write = TOD_WRITE_1;
		channel->tod_n = TOD_1;
		channel->hw_dpll_n = HW_DPLL_1;
		channel->dpll_phase = DPLL_PHASE_1;
		channel->dpll_ctrl_n = DPLL_CTRL_1;
		channel->dpll_phase_pull_in = DPLL_PHASE_PULL_IN_1;
		break;
	case 2:
		channel->dpll_freq = DPLL_FREQ_2;
		channel->dpll_n = DPLL_2;
		channel->tod_read_primary = TOD_READ_PRIMARY_2;
		channel->tod_write = TOD_WRITE_2;
		channel->tod_n = TOD_2;
		channel->hw_dpll_n = HW_DPLL_2;
		channel->dpll_phase = DPLL_PHASE_2;
		channel->dpll_ctrl_n = DPLL_CTRL_2;
		channel->dpll_phase_pull_in = DPLL_PHASE_PULL_IN_2;
		break;
	case 3:
		channel->dpll_freq = DPLL_FREQ_3;
		channel->dpll_n = DPLL_3;
		channel->tod_read_primary = TOD_READ_PRIMARY_3;
		channel->tod_write = TOD_WRITE_3;
		channel->tod_n = TOD_3;
		channel->hw_dpll_n = HW_DPLL_3;
		channel->dpll_phase = DPLL_PHASE_3;
		channel->dpll_ctrl_n = DPLL_CTRL_3;
		channel->dpll_phase_pull_in = DPLL_PHASE_PULL_IN_3;
		break;
	default:
		return -EINVAL;
	}

	channel->idtcm = idtcm;

	channel->caps = idtcm_caps;
	snprintf(channel->caps.name, sizeof(channel->caps.name),
		 "IDT CM PLL%u", index);

	err = idtcm_set_pll_mode(channel, PLL_MODE_WRITE_FREQUENCY);
	if (err)
		return err;

	err = idtcm_enable_tod(channel);
	if (err)
		return err;

	channel->ptp_clock = ptp_clock_register(&channel->caps, NULL);

	if (IS_ERR(channel->ptp_clock)) {
		err = PTR_ERR(channel->ptp_clock);
		channel->ptp_clock = NULL;
		return err;
	}

	if (!channel->ptp_clock)
		return -ENOTSUPP;

	dev_info(&idtcm->client->dev, "PLL%d registered as ptp%d\n",
		 index, channel->ptp_clock->index);

	return 0;
}

static void ptp_clock_unregister_all(struct idtcm *idtcm)
{
	u8 i;
	struct idtcm_channel *channel;

	for (i = 0; i < MAX_PHC_PLL; i++) {

		channel = &idtcm->channel[i];

		if (channel->ptp_clock)
			ptp_clock_unregister(channel->ptp_clock);
	}
}

static void set_default_masks(struct idtcm *idtcm)
{
	idtcm->pll_mask = DEFAULT_PLL_MASK;

	idtcm->channel[0].output_mask = DEFAULT_OUTPUT_MASK_PLL0;
	idtcm->channel[1].output_mask = DEFAULT_OUTPUT_MASK_PLL1;
	idtcm->channel[2].output_mask = DEFAULT_OUTPUT_MASK_PLL2;
	idtcm->channel[3].output_mask = DEFAULT_OUTPUT_MASK_PLL3;
}

static int set_tod_write_overhead(struct idtcm *idtcm)
{
	int err;
	u8 i;

	s64 total_ns = 0;

	ktime_t start;
	ktime_t stop;

	char buf[TOD_BYTE_COUNT];

	struct idtcm_channel *channel = &idtcm->channel[2];

	/* Set page offset */
	idtcm_write(idtcm, channel->hw_dpll_n, HW_DPLL_TOD_OVR__0,
		    buf, sizeof(buf));

	for (i = 0; i < TOD_WRITE_OVERHEAD_COUNT_MAX; i++) {

		start = ktime_get_raw();

		err = idtcm_write(idtcm, channel->hw_dpll_n,
				  HW_DPLL_TOD_OVR__0, buf, sizeof(buf));

		if (err)
			return err;

		stop = ktime_get_raw();

		total_ns += ktime_to_ns(stop - start);
	}

	idtcm->tod_write_overhead_ns = div_s64(total_ns,
					       TOD_WRITE_OVERHEAD_COUNT_MAX);

	return err;
}

static int idtcm_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct idtcm *idtcm;
	int err;
	u8 i;

	/* Unused for now */
	(void)id;

	idtcm = devm_kzalloc(&client->dev, sizeof(struct idtcm), GFP_KERNEL);

	if (!idtcm)
		return -ENOMEM;

	idtcm->client = client;
	idtcm->page_offset = 0xff;
	idtcm->calculate_overhead_flag = 0;

	set_default_masks(idtcm);

	mutex_init(&idtcm->reg_lock);
	mutex_lock(&idtcm->reg_lock);

	idtcm_display_version_info(idtcm);

	err = set_tod_write_overhead(idtcm);

	if (err) {
		mutex_unlock(&idtcm->reg_lock);
		return err;
	}

	err = idtcm_load_firmware(idtcm, &client->dev);

	if (err)
		dev_warn(&idtcm->client->dev,
			 "loading firmware failed with %d\n", err);

	if (idtcm->pll_mask) {
		for (i = 0; i < MAX_PHC_PLL; i++) {
			if (idtcm->pll_mask & (1 << i)) {
				err = idtcm_enable_channel(idtcm, i);
				if (err)
					break;
			}
		}
	} else {
		dev_err(&idtcm->client->dev,
			"no PLLs flagged as PHCs, nothing to do\n");
		err = -ENODEV;
	}

	mutex_unlock(&idtcm->reg_lock);

	if (err) {
		ptp_clock_unregister_all(idtcm);
		return err;
	}

	i2c_set_clientdata(client, idtcm);

	return 0;
}

static int idtcm_remove(struct i2c_client *client)
{
	struct idtcm *idtcm = i2c_get_clientdata(client);

	ptp_clock_unregister_all(idtcm);

	mutex_destroy(&idtcm->reg_lock);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id idtcm_dt_id[] = {
	{ .compatible = "idt,8a34000" },
	{ .compatible = "idt,8a34001" },
	{ .compatible = "idt,8a34002" },
	{ .compatible = "idt,8a34003" },
	{ .compatible = "idt,8a34004" },
	{ .compatible = "idt,8a34005" },
	{ .compatible = "idt,8a34006" },
	{ .compatible = "idt,8a34007" },
	{ .compatible = "idt,8a34008" },
	{ .compatible = "idt,8a34009" },
	{ .compatible = "idt,8a34010" },
	{ .compatible = "idt,8a34011" },
	{ .compatible = "idt,8a34012" },
	{ .compatible = "idt,8a34013" },
	{ .compatible = "idt,8a34014" },
	{ .compatible = "idt,8a34015" },
	{ .compatible = "idt,8a34016" },
	{ .compatible = "idt,8a34017" },
	{ .compatible = "idt,8a34018" },
	{ .compatible = "idt,8a34019" },
	{ .compatible = "idt,8a34040" },
	{ .compatible = "idt,8a34041" },
	{ .compatible = "idt,8a34042" },
	{ .compatible = "idt,8a34043" },
	{ .compatible = "idt,8a34044" },
	{ .compatible = "idt,8a34045" },
	{ .compatible = "idt,8a34046" },
	{ .compatible = "idt,8a34047" },
	{ .compatible = "idt,8a34048" },
	{ .compatible = "idt,8a34049" },
	{},
};
MODULE_DEVICE_TABLE(of, idtcm_dt_id);
#endif

static const struct i2c_device_id idtcm_i2c_id[] = {
	{ "8a34000" },
	{ "8a34001" },
	{ "8a34002" },
	{ "8a34003" },
	{ "8a34004" },
	{ "8a34005" },
	{ "8a34006" },
	{ "8a34007" },
	{ "8a34008" },
	{ "8a34009" },
	{ "8a34010" },
	{ "8a34011" },
	{ "8a34012" },
	{ "8a34013" },
	{ "8a34014" },
	{ "8a34015" },
	{ "8a34016" },
	{ "8a34017" },
	{ "8a34018" },
	{ "8a34019" },
	{ "8a34040" },
	{ "8a34041" },
	{ "8a34042" },
	{ "8a34043" },
	{ "8a34044" },
	{ "8a34045" },
	{ "8a34046" },
	{ "8a34047" },
	{ "8a34048" },
	{ "8a34049" },
	{},
};
MODULE_DEVICE_TABLE(i2c, idtcm_i2c_id);

static struct i2c_driver idtcm_driver = {
	.driver = {
		.of_match_table	= of_match_ptr(idtcm_dt_id),
		.name		= "idtcm",
	},
	.probe		= idtcm_probe,
	.remove		= idtcm_remove,
	.id_table	= idtcm_i2c_id,
};

module_i2c_driver(idtcm_driver);
