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
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/timekeeping.h>
#include <linux/string.h>

#include "ptp_private.h"
#include "ptp_clockmatrix.h"

MODULE_DESCRIPTION("Driver for IDT ClockMatrix(TM) family");
MODULE_AUTHOR("Richard Cochran <richardcochran@gmail.com>");
MODULE_AUTHOR("IDT support-1588 <IDT-support-1588@lm.renesas.com>");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");

/*
 * The name of the firmware file to be loaded
 * over-rides any automatic selection
 */
static char *firmware;
module_param(firmware, charp, 0);

#define SETTIME_CORRECTION (0)

static long set_write_phase_ready(struct ptp_clock_info *ptp)
{
	struct idtcm_channel *channel =
		container_of(ptp, struct idtcm_channel, caps);

	channel->write_phase_ready = 1;

	return 0;
}

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

static int idtcm_strverscmp(const char *ver1, const char *ver2)
{
	u8 num1;
	u8 num2;
	int result = 0;

	/* loop through each level of the version string */
	while (result == 0) {
		/* extract leading version numbers */
		if (kstrtou8(ver1, 10, &num1) < 0)
			return -1;

		if (kstrtou8(ver2, 10, &num2) < 0)
			return -1;

		/* if numbers differ, then set the result */
		if (num1 < num2)
			result = -1;
		else if (num1 > num2)
			result = 1;
		else {
			/* if numbers are the same, go to next level */
			ver1 = strchr(ver1, '.');
			ver2 = strchr(ver2, '.');
			if (!ver1 && !ver2)
				break;
			else if (!ver1)
				result = -1;
			else if (!ver2)
				result = 1;
			else {
				ver1++;
				ver2++;
			}
		}
	}
	return result;
}

static int idtcm_xfer_read(struct idtcm *idtcm,
			   u8 regaddr,
			   u8 *buf,
			   u16 count)
{
	struct i2c_client *client = idtcm->client;
	struct i2c_msg msg[2];
	int cnt;
	char *fmt = "i2c_transfer failed at %d in %s, at addr: %04X!\n";

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &regaddr;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = count;
	msg[1].buf = buf;

	cnt = i2c_transfer(client->adapter, msg, 2);

	if (cnt < 0) {
		dev_err(&client->dev,
			fmt,
			__LINE__,
			__func__,
			regaddr);
		return cnt;
	} else if (cnt != 2) {
		dev_err(&client->dev,
			"i2c_transfer sent only %d of %d messages\n", cnt, 2);
		return -EIO;
	}

	return 0;
}

static int idtcm_xfer_write(struct idtcm *idtcm,
			    u8 regaddr,
			    u8 *buf,
			    u16 count)
{
	struct i2c_client *client = idtcm->client;
	/* we add 1 byte for device register */
	u8 msg[IDTCM_MAX_WRITE_COUNT + 1];
	int cnt;
	char *fmt = "i2c_master_send failed at %d in %s, at addr: %04X!\n";

	if (count > IDTCM_MAX_WRITE_COUNT)
		return -EINVAL;

	msg[0] = regaddr;
	memcpy(&msg[1], buf, count);

	cnt = i2c_master_send(client, msg, count + 1);

	if (cnt < 0) {
		dev_err(&client->dev,
			fmt,
			__LINE__,
			__func__,
			regaddr);
		return cnt;
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

	err = idtcm_xfer_write(idtcm, PAGE_ADDR, buf, sizeof(buf));

	if (err) {
		idtcm->page_offset = 0xff;
		dev_err(&idtcm->client->dev, "failed to set page offset\n");
	} else {
		idtcm->page_offset = val;
	}

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
		return err;

	if (write)
		return idtcm_xfer_write(idtcm, lo, buf, count);

	return idtcm_xfer_read(idtcm, lo, buf, count);
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
	u8 timeout = 10;
	u8 trigger;
	int err;

	err = idtcm_read(idtcm, channel->tod_read_primary,
			 TOD_READ_PRIMARY_CMD, &trigger, sizeof(trigger));
	if (err)
		return err;

	trigger &= ~(TOD_READ_TRIGGER_MASK << TOD_READ_TRIGGER_SHIFT);
	trigger |= (1 << TOD_READ_TRIGGER_SHIFT);
	trigger &= ~TOD_READ_TRIGGER_MODE; /* single shot */

	err = idtcm_write(idtcm, channel->tod_read_primary,
			  TOD_READ_PRIMARY_CMD, &trigger, sizeof(trigger));
	if (err)
		return err;

	/* wait trigger to be 0 */
	while (trigger & TOD_READ_TRIGGER_MASK) {

		if (idtcm->calculate_overhead_flag)
			idtcm->start_time = ktime_get_raw();

		err = idtcm_read(idtcm, channel->tod_read_primary,
				 TOD_READ_PRIMARY_CMD, &trigger,
				 sizeof(trigger));

		if (err)
			return err;

		if (--timeout == 0)
			return -EIO;
	}

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
	u8 temp;

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

	/* PLL5 can have OUT8 as second additional output. */
	if ((pll == 5) && (qn_plus_1 != 0)) {
		err = idtcm_read(idtcm, 0, HW_Q8_CTRL_SPARE,
				 &temp, sizeof(temp));
		if (err)
			return err;

		temp &= ~(Q9_TO_Q8_SYNC_TRIG);

		err = idtcm_write(idtcm, 0, HW_Q8_CTRL_SPARE,
				  &temp, sizeof(temp));
		if (err)
			return err;

		temp |= Q9_TO_Q8_SYNC_TRIG;

		err = idtcm_write(idtcm, 0, HW_Q8_CTRL_SPARE,
				  &temp, sizeof(temp));
		if (err)
			return err;
	}

	/* PLL6 can have OUT11 as second additional output. */
	if ((pll == 6) && (qn_plus_1 != 0)) {
		err = idtcm_read(idtcm, 0, HW_Q11_CTRL_SPARE,
				 &temp, sizeof(temp));
		if (err)
			return err;

		temp &= ~(Q10_TO_Q11_SYNC_TRIG);

		err = idtcm_write(idtcm, 0, HW_Q11_CTRL_SPARE,
				  &temp, sizeof(temp));
		if (err)
			return err;

		temp |= Q10_TO_Q11_SYNC_TRIG;

		err = idtcm_write(idtcm, 0, HW_Q11_CTRL_SPARE,
				  &temp, sizeof(temp));
		if (err)
			return err;
	}

	/* Place master sync out of reset */
	val &= ~(SYNCTRL1_MASTER_SYNC_RST);
	err = idtcm_write(idtcm, 0, sync_ctrl1, &val, sizeof(val));

	return err;
}

static int sync_source_dpll_tod_pps(u16 tod_addr, u8 *sync_src)
{
	int err = 0;

	switch (tod_addr) {
	case TOD_0:
		*sync_src = SYNC_SOURCE_DPLL0_TOD_PPS;
		break;
	case TOD_1:
		*sync_src = SYNC_SOURCE_DPLL1_TOD_PPS;
		break;
	case TOD_2:
		*sync_src = SYNC_SOURCE_DPLL2_TOD_PPS;
		break;
	case TOD_3:
		*sync_src = SYNC_SOURCE_DPLL3_TOD_PPS;
		break;
	default:
		err = -EINVAL;
	}

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
	u8 out8_mux = 0;
	u8 out11_mux = 0;
	u8 temp;

	u16 output_mask = channel->output_mask;

	err = sync_source_dpll_tod_pps(channel->tod_n, &sync_src);
	if (err)
		return err;

	err = idtcm_read(idtcm, 0, HW_Q8_CTRL_SPARE,
			 &temp, sizeof(temp));
	if (err)
		return err;

	if ((temp & Q9_TO_Q8_FANOUT_AND_CLOCK_SYNC_ENABLE_MASK) ==
	    Q9_TO_Q8_FANOUT_AND_CLOCK_SYNC_ENABLE_MASK)
		out8_mux = 1;

	err = idtcm_read(idtcm, 0, HW_Q11_CTRL_SPARE,
			 &temp, sizeof(temp));
	if (err)
		return err;

	if ((temp & Q10_TO_Q11_FANOUT_AND_CLOCK_SYNC_ENABLE_MASK) ==
	    Q10_TO_Q11_FANOUT_AND_CLOCK_SYNC_ENABLE_MASK)
		out11_mux = 1;

	for (pll = 0; pll < 8; pll++) {
		qn = 0;
		qn_plus_1 = 0;

		if (pll < 4) {
			/* First 4 pll has 2 outputs */
			qn = output_mask & 0x1;
			output_mask = output_mask >> 1;
			qn_plus_1 = output_mask & 0x1;
			output_mask = output_mask >> 1;
		} else if (pll == 4) {
			if (out8_mux == 0) {
				qn = output_mask & 0x1;
				output_mask = output_mask >> 1;
			}
		} else if (pll == 5) {
			if (out8_mux) {
				qn_plus_1 = output_mask & 0x1;
				output_mask = output_mask >> 1;
			}
			qn = output_mask & 0x1;
			output_mask = output_mask >> 1;
		} else if (pll == 6) {
			qn = output_mask & 0x1;
			output_mask = output_mask >> 1;
			if (out11_mux) {
				qn_plus_1 = output_mask & 0x1;
				output_mask = output_mask >> 1;
			}
		} else if (pll == 7) {
			if (out11_mux == 0) {
				qn = output_mask & 0x1;
				output_mask = output_mask >> 1;
			}
		}

		if ((qn != 0) || (qn_plus_1 != 0))
			err = _sync_pll_output(idtcm, pll, sync_src, qn,
					       qn_plus_1);

		if (err)
			return err;
	}

	return err;
}

static int _idtcm_set_dpll_hw_tod(struct idtcm_channel *channel,
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
			/* Assumption: I2C @ 400KHz */
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

static int _idtcm_set_dpll_scsr_tod(struct idtcm_channel *channel,
				    struct timespec64 const *ts,
				    enum scsr_tod_write_trig_sel wr_trig,
				    enum scsr_tod_write_type_sel wr_type)
{
	struct idtcm *idtcm = channel->idtcm;
	unsigned char buf[TOD_BYTE_COUNT], cmd;
	struct timespec64 local_ts = *ts;
	int err, count = 0;

	timespec64_add_ns(&local_ts, SETTIME_CORRECTION);

	err = timespec_to_char_array(&local_ts, buf, sizeof(buf));

	if (err)
		return err;

	err = idtcm_write(idtcm, channel->tod_write, TOD_WRITE,
			  buf, sizeof(buf));
	if (err)
		return err;

	/* Trigger the write operation. */
	err = idtcm_read(idtcm, channel->tod_write, TOD_WRITE_CMD,
			 &cmd, sizeof(cmd));
	if (err)
		return err;

	cmd &= ~(TOD_WRITE_SELECTION_MASK << TOD_WRITE_SELECTION_SHIFT);
	cmd &= ~(TOD_WRITE_TYPE_MASK << TOD_WRITE_TYPE_SHIFT);
	cmd |= (wr_trig << TOD_WRITE_SELECTION_SHIFT);
	cmd |= (wr_type << TOD_WRITE_TYPE_SHIFT);

	err = idtcm_write(idtcm, channel->tod_write, TOD_WRITE_CMD,
			   &cmd, sizeof(cmd));
	if (err)
		return err;

	/* Wait for the operation to complete. */
	while (1) {
		/* pps trigger takes up to 1 sec to complete */
		if (wr_trig == SCSR_TOD_WR_TRIG_SEL_TODPPS)
			msleep(50);

		err = idtcm_read(idtcm, channel->tod_write, TOD_WRITE_CMD,
				 &cmd, sizeof(cmd));
		if (err)
			return err;

		if (cmd == 0)
			break;

		if (++count > 20) {
			dev_err(&idtcm->client->dev,
				"Timed out waiting for the write counter\n");
			return -EIO;
		}
	}

	return 0;
}

static int _idtcm_settime(struct idtcm_channel *channel,
			  struct timespec64 const *ts,
			  enum hw_tod_write_trig_sel wr_trig)
{
	struct idtcm *idtcm = channel->idtcm;
	int err;
	int i;
	u8 trig_sel;

	err = _idtcm_set_dpll_hw_tod(channel, ts, wr_trig);

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

	if (err) {
		dev_err(&idtcm->client->dev,
			"Failed at line %d in func %s!\n",
			__LINE__,
			__func__);
		return err;
	}

	return idtcm_sync_pps_output(channel);
}

static int _idtcm_settime_v487(struct idtcm_channel *channel,
			       struct timespec64 const *ts,
			       enum scsr_tod_write_type_sel wr_type)
{
	return _idtcm_set_dpll_scsr_tod(channel, ts,
					SCSR_TOD_WR_TRIG_SEL_IMMEDIATE,
					wr_type);
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

static int set_tod_write_overhead(struct idtcm_channel *channel)
{
	struct idtcm *idtcm = channel->idtcm;
	s64 current_ns = 0;
	s64 lowest_ns = 0;
	int err;
	u8 i;

	ktime_t start;
	ktime_t stop;

	char buf[TOD_BYTE_COUNT] = {0};

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

		current_ns = ktime_to_ns(stop - start);

		if (i == 0) {
			lowest_ns = current_ns;
		} else {
			if (current_ns < lowest_ns)
				lowest_ns = current_ns;
		}
	}

	idtcm->tod_write_overhead_ns = lowest_ns;

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

		err = set_tod_write_overhead(channel);

		if (err)
			return err;

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
	return idtcm_read(idtcm, HW_REVISION, REV_ID, hw_rev_id, sizeof(u8));
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

static int idtcm_read_otp_scsr_config_select(struct idtcm *idtcm,
					     u8 *config_select)
{
	return idtcm_read(idtcm, GENERAL_STATUS, OTP_SCSR_CONFIG_SELECT,
			  config_select, sizeof(u8));
}

static int set_pll_output_mask(struct idtcm *idtcm, u16 addr, u8 val)
{
	int err = 0;

	switch (addr) {
	case TOD0_OUT_ALIGN_MASK_ADDR:
		SET_U16_LSB(idtcm->channel[0].output_mask, val);
		break;
	case TOD0_OUT_ALIGN_MASK_ADDR + 1:
		SET_U16_MSB(idtcm->channel[0].output_mask, val);
		break;
	case TOD1_OUT_ALIGN_MASK_ADDR:
		SET_U16_LSB(idtcm->channel[1].output_mask, val);
		break;
	case TOD1_OUT_ALIGN_MASK_ADDR + 1:
		SET_U16_MSB(idtcm->channel[1].output_mask, val);
		break;
	case TOD2_OUT_ALIGN_MASK_ADDR:
		SET_U16_LSB(idtcm->channel[2].output_mask, val);
		break;
	case TOD2_OUT_ALIGN_MASK_ADDR + 1:
		SET_U16_MSB(idtcm->channel[2].output_mask, val);
		break;
	case TOD3_OUT_ALIGN_MASK_ADDR:
		SET_U16_LSB(idtcm->channel[3].output_mask, val);
		break;
	case TOD3_OUT_ALIGN_MASK_ADDR + 1:
		SET_U16_MSB(idtcm->channel[3].output_mask, val);
		break;
	default:
		err = -EFAULT; /* Bad address */;
		break;
	}

	return err;
}

static int set_tod_ptp_pll(struct idtcm *idtcm, u8 index, u8 pll)
{
	if (index >= MAX_TOD) {
		dev_err(&idtcm->client->dev, "ToD%d not supported\n", index);
		return -EINVAL;
	}

	if (pll >= MAX_PLL) {
		dev_err(&idtcm->client->dev, "Pll%d not supported\n", pll);
		return -EINVAL;
	}

	idtcm->channel[index].pll = pll;

	return 0;
}

static int check_and_set_masks(struct idtcm *idtcm,
			       u16 regaddr,
			       u8 val)
{
	int err = 0;

	switch (regaddr) {
	case TOD_MASK_ADDR:
		if ((val & 0xf0) || !(val & 0x0f)) {
			dev_err(&idtcm->client->dev,
				"Invalid TOD mask 0x%hhx\n", val);
			err = -EINVAL;
		} else {
			idtcm->tod_mask = val;
		}
		break;
	case TOD0_PTP_PLL_ADDR:
		err = set_tod_ptp_pll(idtcm, 0, val);
		break;
	case TOD1_PTP_PLL_ADDR:
		err = set_tod_ptp_pll(idtcm, 1, val);
		break;
	case TOD2_PTP_PLL_ADDR:
		err = set_tod_ptp_pll(idtcm, 2, val);
		break;
	case TOD3_PTP_PLL_ADDR:
		err = set_tod_ptp_pll(idtcm, 3, val);
		break;
	default:
		err = set_pll_output_mask(idtcm, regaddr, val);
		break;
	}

	return err;
}

static void display_pll_and_masks(struct idtcm *idtcm)
{
	u8 i;
	u8 mask;

	dev_dbg(&idtcm->client->dev, "tod_mask = 0x%02x\n", idtcm->tod_mask);

	for (i = 0; i < MAX_TOD; i++) {
		mask = 1 << i;

		if (mask & idtcm->tod_mask)
			dev_dbg(&idtcm->client->dev,
				"TOD%d pll = %d    output_mask = 0x%04x\n",
				i, idtcm->channel[i].pll,
				idtcm->channel[i].output_mask);
	}
}

static int idtcm_load_firmware(struct idtcm *idtcm,
			       struct device *dev)
{
	char fname[128] = FW_FILENAME;
	const struct firmware *fw;
	struct idtcm_fwrc *rec;
	u32 regaddr;
	int err;
	s32 len;
	u8 val;
	u8 loaddr;

	if (firmware) /* module parameter */
		snprintf(fname, sizeof(fname), "%s", firmware);

	dev_dbg(&idtcm->client->dev, "requesting firmware '%s'\n", fname);

	err = request_firmware(&fw, fname, dev);

	if (err) {
		dev_err(&idtcm->client->dev,
			"Failed at line %d in func %s!\n",
			__LINE__,
			__func__);
		return err;
	}

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

		if (err != -EINVAL) {
			err = 0;

			/* Top (status registers) and bottom are read-only */
			if ((regaddr < GPIO_USER_CONTROL)
			    || (regaddr >= SCRATCH))
				continue;

			/* Page size 128, last 4 bytes of page skipped */
			if (((loaddr > 0x7b) && (loaddr <= 0x7f))
			     || loaddr > 0xfb)
				continue;

			err = idtcm_write(idtcm, regaddr, 0, &val, sizeof(val));
		}

		if (err)
			goto out;
	}

	display_pll_and_masks(idtcm);

out:
	release_firmware(fw);
	return err;
}

static int idtcm_output_enable(struct idtcm_channel *channel,
			       bool enable, unsigned int outn)
{
	struct idtcm *idtcm = channel->idtcm;
	int err;
	u8 val;

	err = idtcm_read(idtcm, OUTPUT_MODULE_FROM_INDEX(outn),
			 OUT_CTRL_1, &val, sizeof(val));

	if (err)
		return err;

	if (enable)
		val |= SQUELCH_DISABLE;
	else
		val &= ~SQUELCH_DISABLE;

	return idtcm_write(idtcm, OUTPUT_MODULE_FROM_INDEX(outn),
			   OUT_CTRL_1, &val, sizeof(val));
}

static int idtcm_output_mask_enable(struct idtcm_channel *channel,
				    bool enable)
{
	u16 mask;
	int err;
	u8 outn;

	mask = channel->output_mask;
	outn = 0;

	while (mask) {

		if (mask & 0x1) {

			err = idtcm_output_enable(channel, enable, outn);

			if (err)
				return err;
		}

		mask >>= 0x1;
		outn++;
	}

	return 0;
}

static int idtcm_perout_enable(struct idtcm_channel *channel,
			       bool enable,
			       struct ptp_perout_request *perout)
{
	unsigned int flags = perout->flags;

	if (flags == PEROUT_ENABLE_OUTPUT_MASK)
		return idtcm_output_mask_enable(channel, enable);

	/* Enable/disable individual output instead */
	return idtcm_output_enable(channel, enable, perout->index);
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

/**
 * @brief Maximum absolute value for write phase offset in picoseconds
 *
 * Destination signed register is 32-bit register in resolution of 50ps
 *
 * 0x7fffffff * 50 =  2147483647 * 50 = 107374182350
 */
static int _idtcm_adjphase(struct idtcm_channel *channel, s32 delta_ns)
{
	struct idtcm *idtcm = channel->idtcm;

	int err;
	u8 i;
	u8 buf[4] = {0};
	s32 phase_50ps;
	s64 offset_ps;

	if (channel->pll_mode != PLL_MODE_WRITE_PHASE) {

		err = idtcm_set_pll_mode(channel, PLL_MODE_WRITE_PHASE);

		if (err)
			return err;

		channel->write_phase_ready = 0;

		ptp_schedule_worker(channel->ptp_clock,
				    msecs_to_jiffies(WR_PHASE_SETUP_MS));
	}

	if (!channel->write_phase_ready)
		delta_ns = 0;

	offset_ps = (s64)delta_ns * 1000;

	/*
	 * Check for 32-bit signed max * 50:
	 *
	 * 0x7fffffff * 50 =  2147483647 * 50 = 107374182350
	 */
	if (offset_ps > MAX_ABS_WRITE_PHASE_PICOSECONDS)
		offset_ps = MAX_ABS_WRITE_PHASE_PICOSECONDS;
	else if (offset_ps < -MAX_ABS_WRITE_PHASE_PICOSECONDS)
		offset_ps = -MAX_ABS_WRITE_PHASE_PICOSECONDS;

	phase_50ps = DIV_ROUND_CLOSEST(div64_s64(offset_ps, 50), 1);

	for (i = 0; i < 4; i++) {
		buf[i] = phase_50ps & 0xff;
		phase_50ps >>= 8;
	}

	err = idtcm_write(idtcm, channel->dpll_phase, DPLL_WR_PHASE,
			  buf, sizeof(buf));

	return err;
}

static int _idtcm_adjfine(struct idtcm_channel *channel, long scaled_ppm)
{
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
	if (scaled_ppm < 0) {
		neg_adj = 1;
		scaled_ppm = -scaled_ppm;
	}

	/* 2 ^ -53 = 1.1102230246251565404236316680908e-16 */
	fcw = scaled_ppm * 244140625ULL;

	fcw = div_u64(fcw, 1776);

	if (neg_adj)
		fcw = -fcw;

	for (i = 0; i < 6; i++) {
		buf[i] = fcw & 0xff;
		fcw >>= 8;
	}

	err = idtcm_write(idtcm, channel->dpll_freq, DPLL_WR_FREQ,
			  buf, sizeof(buf));

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

	if (err)
		dev_err(&idtcm->client->dev,
			"Failed at line %d in func %s!\n",
			__LINE__,
			__func__);

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

	if (err)
		dev_err(&idtcm->client->dev,
			"Failed at line %d in func %s!\n",
			__LINE__,
			__func__);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_settime_v487(struct ptp_clock_info *ptp,
			 const struct timespec64 *ts)
{
	struct idtcm_channel *channel =
		container_of(ptp, struct idtcm_channel, caps);
	struct idtcm *idtcm = channel->idtcm;
	int err;

	mutex_lock(&idtcm->reg_lock);

	err = _idtcm_settime_v487(channel, ts, SCSR_TOD_WR_TYPE_SEL_ABSOLUTE);

	if (err)
		dev_err(&idtcm->client->dev,
			"Failed at line %d in func %s!\n",
			__LINE__,
			__func__);

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

	if (err)
		dev_err(&idtcm->client->dev,
			"Failed at line %d in func %s!\n",
			__LINE__,
			__func__);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_adjtime_v487(struct ptp_clock_info *ptp, s64 delta)
{
	struct idtcm_channel *channel =
		container_of(ptp, struct idtcm_channel, caps);
	struct idtcm *idtcm = channel->idtcm;
	struct timespec64 ts;
	enum scsr_tod_write_type_sel type;
	int err;

	if (abs(delta) < PHASE_PULL_IN_THRESHOLD_NS_V487) {
		err = idtcm_do_phase_pull_in(channel, delta, 0);
		if (err)
			dev_err(&idtcm->client->dev,
				"Failed at line %d in func %s!\n",
				__LINE__,
				__func__);
		return err;
	}

	if (delta >= 0) {
		ts = ns_to_timespec64(delta);
		type = SCSR_TOD_WR_TYPE_SEL_DELTA_PLUS;
	} else {
		ts = ns_to_timespec64(-delta);
		type = SCSR_TOD_WR_TYPE_SEL_DELTA_MINUS;
	}

	mutex_lock(&idtcm->reg_lock);

	err = _idtcm_settime_v487(channel, &ts, type);

	if (err)
		dev_err(&idtcm->client->dev,
			"Failed at line %d in func %s!\n",
			__LINE__,
			__func__);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_adjphase(struct ptp_clock_info *ptp, s32 delta)
{
	struct idtcm_channel *channel =
		container_of(ptp, struct idtcm_channel, caps);

	struct idtcm *idtcm = channel->idtcm;

	int err;

	mutex_lock(&idtcm->reg_lock);

	err = _idtcm_adjphase(channel, delta);

	if (err)
		dev_err(&idtcm->client->dev,
			"Failed at line %d in func %s!\n",
			__LINE__,
			__func__);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_adjfine(struct ptp_clock_info *ptp,  long scaled_ppm)
{
	struct idtcm_channel *channel =
		container_of(ptp, struct idtcm_channel, caps);

	struct idtcm *idtcm = channel->idtcm;

	int err;

	mutex_lock(&idtcm->reg_lock);

	err = _idtcm_adjfine(channel, scaled_ppm);

	if (err)
		dev_err(&idtcm->client->dev,
			"Failed at line %d in func %s!\n",
			__LINE__,
			__func__);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_enable(struct ptp_clock_info *ptp,
			struct ptp_clock_request *rq, int on)
{
	int err;

	struct idtcm_channel *channel =
		container_of(ptp, struct idtcm_channel, caps);

	switch (rq->type) {
	case PTP_CLK_REQ_PEROUT:
		if (!on) {
			err = idtcm_perout_enable(channel, false, &rq->perout);
			if (err)
				dev_err(&channel->idtcm->client->dev,
					"Failed at line %d in func %s!\n",
					__LINE__,
					__func__);
			return err;
		}

		/* Only accept a 1-PPS aligned to the second. */
		if (rq->perout.start.nsec || rq->perout.period.sec != 1 ||
		    rq->perout.period.nsec)
			return -ERANGE;

		err = idtcm_perout_enable(channel, true, &rq->perout);
		if (err)
			dev_err(&channel->idtcm->client->dev,
				"Failed at line %d in func %s!\n",
				__LINE__,
				__func__);
		return err;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int _enable_pll_tod_sync(struct idtcm *idtcm,
				u8 pll,
				u8 sync_src,
				u8 qn,
				u8 qn_plus_1)
{
	int err;
	u8 val;
	u16 dpll;
	u16 out0 = 0, out1 = 0;

	if ((qn == 0) && (qn_plus_1 == 0))
		return 0;

	switch (pll) {
	case 0:
		dpll = DPLL_0;
		if (qn)
			out0 = OUTPUT_0;
		if (qn_plus_1)
			out1 = OUTPUT_1;
		break;
	case 1:
		dpll = DPLL_1;
		if (qn)
			out0 = OUTPUT_2;
		if (qn_plus_1)
			out1 = OUTPUT_3;
		break;
	case 2:
		dpll = DPLL_2;
		if (qn)
			out0 = OUTPUT_4;
		if (qn_plus_1)
			out1 = OUTPUT_5;
		break;
	case 3:
		dpll = DPLL_3;
		if (qn)
			out0 = OUTPUT_6;
		if (qn_plus_1)
			out1 = OUTPUT_7;
		break;
	case 4:
		dpll = DPLL_4;
		if (qn)
			out0 = OUTPUT_8;
		break;
	case 5:
		dpll = DPLL_5;
		if (qn)
			out0 = OUTPUT_9;
		if (qn_plus_1)
			out1 = OUTPUT_8;
		break;
	case 6:
		dpll = DPLL_6;
		if (qn)
			out0 = OUTPUT_10;
		if (qn_plus_1)
			out1 = OUTPUT_11;
		break;
	case 7:
		dpll = DPLL_7;
		if (qn)
			out0 = OUTPUT_11;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Enable OUTPUT OUT_SYNC.
	 */
	if (out0) {
		err = idtcm_read(idtcm, out0, OUT_CTRL_1, &val, sizeof(val));

		if (err)
			return err;

		val &= ~OUT_SYNC_DISABLE;

		err = idtcm_write(idtcm, out0, OUT_CTRL_1, &val, sizeof(val));

		if (err)
			return err;
	}

	if (out1) {
		err = idtcm_read(idtcm, out1, OUT_CTRL_1, &val, sizeof(val));

		if (err)
			return err;

		val &= ~OUT_SYNC_DISABLE;

		err = idtcm_write(idtcm, out1, OUT_CTRL_1, &val, sizeof(val));

		if (err)
			return err;
	}

	/* enable dpll sync tod pps, must be set before dpll_mode */
	err = idtcm_read(idtcm, dpll, DPLL_TOD_SYNC_CFG, &val, sizeof(val));
	if (err)
		return err;

	val &= ~(TOD_SYNC_SOURCE_MASK << TOD_SYNC_SOURCE_SHIFT);
	val |= (sync_src << TOD_SYNC_SOURCE_SHIFT);
	val |= TOD_SYNC_EN;

	return idtcm_write(idtcm, dpll, DPLL_TOD_SYNC_CFG, &val, sizeof(val));
}

static int idtcm_enable_tod_sync(struct idtcm_channel *channel)
{
	struct idtcm *idtcm = channel->idtcm;

	u8 pll;
	u8 sync_src;
	u8 qn;
	u8 qn_plus_1;
	u8 cfg;
	int err = 0;
	u16 output_mask = channel->output_mask;
	u8 out8_mux = 0;
	u8 out11_mux = 0;
	u8 temp;

	/*
	 * set tod_out_sync_enable to 0.
	 */
	err = idtcm_read(idtcm, channel->tod_n, TOD_CFG, &cfg, sizeof(cfg));
	if (err)
		return err;

	cfg &= ~TOD_OUT_SYNC_ENABLE;

	err = idtcm_write(idtcm, channel->tod_n, TOD_CFG, &cfg, sizeof(cfg));
	if (err)
		return err;

	switch (channel->tod_n) {
	case TOD_0:
		sync_src = 0;
		break;
	case TOD_1:
		sync_src = 1;
		break;
	case TOD_2:
		sync_src = 2;
		break;
	case TOD_3:
		sync_src = 3;
		break;
	default:
		return -EINVAL;
	}

	err = idtcm_read(idtcm, 0, HW_Q8_CTRL_SPARE,
			 &temp, sizeof(temp));
	if (err)
		return err;

	if ((temp & Q9_TO_Q8_FANOUT_AND_CLOCK_SYNC_ENABLE_MASK) ==
	    Q9_TO_Q8_FANOUT_AND_CLOCK_SYNC_ENABLE_MASK)
		out8_mux = 1;

	err = idtcm_read(idtcm, 0, HW_Q11_CTRL_SPARE,
			 &temp, sizeof(temp));
	if (err)
		return err;

	if ((temp & Q10_TO_Q11_FANOUT_AND_CLOCK_SYNC_ENABLE_MASK) ==
	    Q10_TO_Q11_FANOUT_AND_CLOCK_SYNC_ENABLE_MASK)
		out11_mux = 1;

	for (pll = 0; pll < 8; pll++) {
		qn = 0;
		qn_plus_1 = 0;

		if (pll < 4) {
			/* First 4 pll has 2 outputs */
			qn = output_mask & 0x1;
			output_mask = output_mask >> 1;
			qn_plus_1 = output_mask & 0x1;
			output_mask = output_mask >> 1;
		} else if (pll == 4) {
			if (out8_mux == 0) {
				qn = output_mask & 0x1;
				output_mask = output_mask >> 1;
			}
		} else if (pll == 5) {
			if (out8_mux) {
				qn_plus_1 = output_mask & 0x1;
				output_mask = output_mask >> 1;
			}
			qn = output_mask & 0x1;
			output_mask = output_mask >> 1;
		} else if (pll == 6) {
			qn = output_mask & 0x1;
			output_mask = output_mask >> 1;
			if (out11_mux) {
				qn_plus_1 = output_mask & 0x1;
				output_mask = output_mask >> 1;
			}
		} else if (pll == 7) {
			if (out11_mux == 0) {
				qn = output_mask & 0x1;
				output_mask = output_mask >> 1;
			}
		}

		if ((qn != 0) || (qn_plus_1 != 0))
			err = _enable_pll_tod_sync(idtcm, pll, sync_src, qn,
					       qn_plus_1);

		if (err)
			return err;
	}

	return err;
}

static int idtcm_enable_tod(struct idtcm_channel *channel)
{
	struct idtcm *idtcm = channel->idtcm;
	struct timespec64 ts = {0, 0};
	u8 cfg;
	int err;

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
	u16 product_id;
	u8 hw_rev_id;
	u8 config_select;
	char *fmt = "%d.%d.%d, Id: 0x%04x  HW Rev: %d  OTP Config Select: %d\n";

	idtcm_read_major_release(idtcm, &major);
	idtcm_read_minor_release(idtcm, &minor);
	idtcm_read_hotfix_release(idtcm, &hotfix);

	idtcm_read_product_id(idtcm, &product_id);
	idtcm_read_hw_rev_id(idtcm, &hw_rev_id);

	idtcm_read_otp_scsr_config_select(idtcm, &config_select);

	snprintf(idtcm->version, sizeof(idtcm->version), "%u.%u.%u",
		 major, minor, hotfix);

	dev_info(&idtcm->client->dev, fmt, major, minor, hotfix,
		 product_id, hw_rev_id, config_select);
}

static const struct ptp_clock_info idtcm_caps_v487 = {
	.owner		= THIS_MODULE,
	.max_adj	= 244000,
	.n_per_out	= 12,
	.adjphase	= &idtcm_adjphase,
	.adjfine	= &idtcm_adjfine,
	.adjtime	= &idtcm_adjtime_v487,
	.gettime64	= &idtcm_gettime,
	.settime64	= &idtcm_settime_v487,
	.enable		= &idtcm_enable,
	.do_aux_work	= &set_write_phase_ready,
};

static const struct ptp_clock_info idtcm_caps = {
	.owner		= THIS_MODULE,
	.max_adj	= 244000,
	.n_per_out	= 12,
	.adjphase	= &idtcm_adjphase,
	.adjfine	= &idtcm_adjfine,
	.adjtime	= &idtcm_adjtime,
	.gettime64	= &idtcm_gettime,
	.settime64	= &idtcm_settime,
	.enable		= &idtcm_enable,
	.do_aux_work	= &set_write_phase_ready,
};

static int configure_channel_pll(struct idtcm_channel *channel)
{
	int err = 0;

	switch (channel->pll) {
	case 0:
		channel->dpll_freq = DPLL_FREQ_0;
		channel->dpll_n = DPLL_0;
		channel->hw_dpll_n = HW_DPLL_0;
		channel->dpll_phase = DPLL_PHASE_0;
		channel->dpll_ctrl_n = DPLL_CTRL_0;
		channel->dpll_phase_pull_in = DPLL_PHASE_PULL_IN_0;
		break;
	case 1:
		channel->dpll_freq = DPLL_FREQ_1;
		channel->dpll_n = DPLL_1;
		channel->hw_dpll_n = HW_DPLL_1;
		channel->dpll_phase = DPLL_PHASE_1;
		channel->dpll_ctrl_n = DPLL_CTRL_1;
		channel->dpll_phase_pull_in = DPLL_PHASE_PULL_IN_1;
		break;
	case 2:
		channel->dpll_freq = DPLL_FREQ_2;
		channel->dpll_n = DPLL_2;
		channel->hw_dpll_n = HW_DPLL_2;
		channel->dpll_phase = DPLL_PHASE_2;
		channel->dpll_ctrl_n = DPLL_CTRL_2;
		channel->dpll_phase_pull_in = DPLL_PHASE_PULL_IN_2;
		break;
	case 3:
		channel->dpll_freq = DPLL_FREQ_3;
		channel->dpll_n = DPLL_3;
		channel->hw_dpll_n = HW_DPLL_3;
		channel->dpll_phase = DPLL_PHASE_3;
		channel->dpll_ctrl_n = DPLL_CTRL_3;
		channel->dpll_phase_pull_in = DPLL_PHASE_PULL_IN_3;
		break;
	case 4:
		channel->dpll_freq = DPLL_FREQ_4;
		channel->dpll_n = DPLL_4;
		channel->hw_dpll_n = HW_DPLL_4;
		channel->dpll_phase = DPLL_PHASE_4;
		channel->dpll_ctrl_n = DPLL_CTRL_4;
		channel->dpll_phase_pull_in = DPLL_PHASE_PULL_IN_4;
		break;
	case 5:
		channel->dpll_freq = DPLL_FREQ_5;
		channel->dpll_n = DPLL_5;
		channel->hw_dpll_n = HW_DPLL_5;
		channel->dpll_phase = DPLL_PHASE_5;
		channel->dpll_ctrl_n = DPLL_CTRL_5;
		channel->dpll_phase_pull_in = DPLL_PHASE_PULL_IN_5;
		break;
	case 6:
		channel->dpll_freq = DPLL_FREQ_6;
		channel->dpll_n = DPLL_6;
		channel->hw_dpll_n = HW_DPLL_6;
		channel->dpll_phase = DPLL_PHASE_6;
		channel->dpll_ctrl_n = DPLL_CTRL_6;
		channel->dpll_phase_pull_in = DPLL_PHASE_PULL_IN_6;
		break;
	case 7:
		channel->dpll_freq = DPLL_FREQ_7;
		channel->dpll_n = DPLL_7;
		channel->hw_dpll_n = HW_DPLL_7;
		channel->dpll_phase = DPLL_PHASE_7;
		channel->dpll_ctrl_n = DPLL_CTRL_7;
		channel->dpll_phase_pull_in = DPLL_PHASE_PULL_IN_7;
		break;
	default:
		err = -EINVAL;
	}

	return err;
}

static int idtcm_enable_channel(struct idtcm *idtcm, u32 index)
{
	struct idtcm_channel *channel;
	int err;

	if (!(index < MAX_TOD))
		return -EINVAL;

	channel = &idtcm->channel[index];

	/* Set pll addresses */
	err = configure_channel_pll(channel);
	if (err)
		return err;

	/* Set tod addresses */
	switch (index) {
	case 0:
		channel->tod_read_primary = TOD_READ_PRIMARY_0;
		channel->tod_write = TOD_WRITE_0;
		channel->tod_n = TOD_0;
		break;
	case 1:
		channel->tod_read_primary = TOD_READ_PRIMARY_1;
		channel->tod_write = TOD_WRITE_1;
		channel->tod_n = TOD_1;
		break;
	case 2:
		channel->tod_read_primary = TOD_READ_PRIMARY_2;
		channel->tod_write = TOD_WRITE_2;
		channel->tod_n = TOD_2;
		break;
	case 3:
		channel->tod_read_primary = TOD_READ_PRIMARY_3;
		channel->tod_write = TOD_WRITE_3;
		channel->tod_n = TOD_3;
		break;
	default:
		return -EINVAL;
	}

	channel->idtcm = idtcm;

	if (idtcm_strverscmp(idtcm->version, "4.8.7") >= 0)
		channel->caps = idtcm_caps_v487;
	else
		channel->caps = idtcm_caps;

	snprintf(channel->caps.name, sizeof(channel->caps.name),
		 "IDT CM TOD%u", index);

	if (idtcm_strverscmp(idtcm->version, "4.8.7") >= 0) {
		err = idtcm_enable_tod_sync(channel);
		if (err) {
			dev_err(&idtcm->client->dev,
				"Failed at line %d in func %s!\n",
				__LINE__,
				__func__);
			return err;
		}
	}

	err = idtcm_set_pll_mode(channel, PLL_MODE_WRITE_FREQUENCY);
	if (err) {
		dev_err(&idtcm->client->dev,
			"Failed at line %d in func %s!\n",
			__LINE__,
			__func__);
		return err;
	}

	err = idtcm_enable_tod(channel);
	if (err) {
		dev_err(&idtcm->client->dev,
			"Failed at line %d in func %s!\n",
			__LINE__,
			__func__);
		return err;
	}

	channel->ptp_clock = ptp_clock_register(&channel->caps, NULL);

	if (IS_ERR(channel->ptp_clock)) {
		err = PTR_ERR(channel->ptp_clock);
		channel->ptp_clock = NULL;
		return err;
	}

	if (!channel->ptp_clock)
		return -ENOTSUPP;

	channel->write_phase_ready = 0;

	dev_info(&idtcm->client->dev, "PLL%d registered as ptp%d\n",
		 index, channel->ptp_clock->index);

	return 0;
}

static void ptp_clock_unregister_all(struct idtcm *idtcm)
{
	u8 i;
	struct idtcm_channel *channel;

	for (i = 0; i < MAX_TOD; i++) {

		channel = &idtcm->channel[i];

		if (channel->ptp_clock)
			ptp_clock_unregister(channel->ptp_clock);
	}
}

static void set_default_masks(struct idtcm *idtcm)
{
	idtcm->tod_mask = DEFAULT_TOD_MASK;

	idtcm->channel[0].pll = DEFAULT_TOD0_PTP_PLL;
	idtcm->channel[1].pll = DEFAULT_TOD1_PTP_PLL;
	idtcm->channel[2].pll = DEFAULT_TOD2_PTP_PLL;
	idtcm->channel[3].pll = DEFAULT_TOD3_PTP_PLL;

	idtcm->channel[0].output_mask = DEFAULT_OUTPUT_MASK_PLL0;
	idtcm->channel[1].output_mask = DEFAULT_OUTPUT_MASK_PLL1;
	idtcm->channel[2].output_mask = DEFAULT_OUTPUT_MASK_PLL2;
	idtcm->channel[3].output_mask = DEFAULT_OUTPUT_MASK_PLL3;
}

static int idtcm_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct idtcm *idtcm;
	int err;
	u8 i;
	char *fmt = "Failed at %d in line %s with channel output %d!\n";

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

	err = idtcm_load_firmware(idtcm, &client->dev);

	if (err)
		dev_warn(&idtcm->client->dev,
			 "loading firmware failed with %d\n", err);

	if (idtcm->tod_mask) {
		for (i = 0; i < MAX_TOD; i++) {
			if (idtcm->tod_mask & (1 << i)) {
				err = idtcm_enable_channel(idtcm, i);
				if (err) {
					dev_err(&idtcm->client->dev,
						fmt,
						__LINE__,
						__func__,
						i);
					break;
				}
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
