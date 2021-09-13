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

static int contains_full_configuration(struct idtcm *idtcm,
				       const struct firmware *fw)
{
	struct idtcm_fwrc *rec = (struct idtcm_fwrc *)fw->data;
	u16 scratch = IDTCM_FW_REG(idtcm->fw_ver, V520, SCRATCH);
	s32 full_count;
	s32 count = 0;
	u16 regaddr;
	u8 loaddr;
	s32 len;

	/* 4 bytes skipped every 0x80 */
	full_count = (scratch - GPIO_USER_CONTROL) -
		     ((scratch >> 7) - (GPIO_USER_CONTROL >> 7)) * 4;

	/* If the firmware contains 'full configuration' SM_RESET can be used
	 * to ensure proper configuration.
	 *
	 * Full configuration is defined as the number of programmable
	 * bytes within the configuration range minus page offset addr range.
	 */
	for (len = fw->size; len > 0; len -= sizeof(*rec)) {
		regaddr = rec->hiaddr << 8;
		regaddr |= rec->loaddr;

		loaddr = rec->loaddr;

		rec++;

		/* Top (status registers) and bottom are read-only */
		if (regaddr < GPIO_USER_CONTROL || regaddr >= scratch)
			continue;

		/* Page size 128, last 4 bytes of page skipped */
		if ((loaddr > 0x7b && loaddr <= 0x7f) || loaddr > 0xfb)
			continue;

		count++;
	}

	return (count >= full_count);
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

static int idtcm_strverscmp(const char *version1, const char *version2)
{
	u8 ver1[3], ver2[3];
	int i;

	if (sscanf(version1, "%hhu.%hhu.%hhu",
		   &ver1[0], &ver1[1], &ver1[2]) != 3)
		return -1;
	if (sscanf(version2, "%hhu.%hhu.%hhu",
		   &ver2[0], &ver2[1], &ver2[2]) != 3)
		return -1;

	for (i = 0; i < 3; i++) {
		if (ver1[i] > ver2[i])
			return 1;
		if (ver1[i] < ver2[i])
			return -1;
	}

	return 0;
}

static enum fw_version idtcm_fw_version(const char *version)
{
	enum fw_version ver = V_DEFAULT;

	if (idtcm_strverscmp(version, "4.8.7") >= 0)
		ver = V487;

	if (idtcm_strverscmp(version, "5.2.0") >= 0)
		ver = V520;

	return ver;
}

static int idtcm_xfer_read(struct idtcm *idtcm,
			   u8 regaddr,
			   u8 *buf,
			   u16 count)
{
	struct i2c_client *client = idtcm->client;
	struct i2c_msg msg[2];
	int cnt;

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
			"i2c_transfer failed at %d in %s, at addr: %04x!",
			__LINE__, __func__, regaddr);
		return cnt;
	} else if (cnt != 2) {
		dev_err(&client->dev,
			"i2c_transfer sent only %d of %d messages", cnt, 2);
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

	if (count > IDTCM_MAX_WRITE_COUNT)
		return -EINVAL;

	msg[0] = regaddr;
	memcpy(&msg[1], buf, count);

	cnt = i2c_master_send(client, msg, count + 1);

	if (cnt < 0) {
		dev_err(&client->dev,
			"i2c_master_send failed at %d in %s, at addr: %04x!",
			__LINE__, __func__, regaddr);
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
		dev_err(&idtcm->client->dev, "failed to set page offset");
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

static int clear_boot_status(struct idtcm *idtcm)
{
	u8 buf[4] = {0};

	return idtcm_write(idtcm, GENERAL_STATUS, BOOT_STATUS, buf, sizeof(buf));
}

static int read_boot_status(struct idtcm *idtcm, u32 *status)
{
	int err;
	u8 buf[4] = {0};

	err = idtcm_read(idtcm, GENERAL_STATUS, BOOT_STATUS, buf, sizeof(buf));

	*status = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];

	return err;
}

static int wait_for_boot_status_ready(struct idtcm *idtcm)
{
	u32 status = 0;
	u8 i = 30;	/* 30 * 100ms = 3s */
	int err;

	do {
		err = read_boot_status(idtcm, &status);
		if (err)
			return err;

		if (status == 0xA0)
			return 0;

		msleep(100);
		i--;

	} while (i);

	dev_warn(&idtcm->client->dev, "%s timed out", __func__);

	return -EBUSY;
}

static int read_sys_apll_status(struct idtcm *idtcm, u8 *status)
{
	return idtcm_read(idtcm, STATUS, DPLL_SYS_APLL_STATUS, status,
			  sizeof(u8));
}

static int read_sys_dpll_status(struct idtcm *idtcm, u8 *status)
{
	return idtcm_read(idtcm, STATUS, DPLL_SYS_STATUS, status, sizeof(u8));
}

static int wait_for_sys_apll_dpll_lock(struct idtcm *idtcm)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(LOCK_TIMEOUT_MS);
	u8 apll = 0;
	u8 dpll = 0;
	int err;

	do {
		err = read_sys_apll_status(idtcm, &apll);
		if (err)
			return err;

		err = read_sys_dpll_status(idtcm, &dpll);
		if (err)
			return err;

		apll &= SYS_APLL_LOSS_LOCK_LIVE_MASK;
		dpll &= DPLL_SYS_STATE_MASK;

		if (apll == SYS_APLL_LOSS_LOCK_LIVE_LOCKED &&
		    dpll == DPLL_STATE_LOCKED) {
			return 0;
		} else if (dpll == DPLL_STATE_FREERUN ||
			   dpll == DPLL_STATE_HOLDOVER ||
			   dpll == DPLL_STATE_OPEN_LOOP) {
			dev_warn(&idtcm->client->dev,
				"No wait state: DPLL_SYS_STATE %d", dpll);
			return -EPERM;
		}

		msleep(LOCK_POLL_INTERVAL_MS);
	} while (time_is_after_jiffies(timeout));

	dev_warn(&idtcm->client->dev,
		 "%d ms lock timeout: SYS APLL Loss Lock %d  SYS DPLL state %d",
		 LOCK_TIMEOUT_MS, apll, dpll);

	return -ETIME;
}

static void wait_for_chip_ready(struct idtcm *idtcm)
{
	if (wait_for_boot_status_ready(idtcm))
		dev_warn(&idtcm->client->dev, "BOOT_STATUS != 0xA0");

	if (wait_for_sys_apll_dpll_lock(idtcm))
		dev_warn(&idtcm->client->dev,
			 "Continuing while SYS APLL/DPLL is not locked");
}

static int _idtcm_gettime(struct idtcm_channel *channel,
			  struct timespec64 *ts)
{
	struct idtcm *idtcm = channel->idtcm;
	u16 tod_read_cmd = IDTCM_FW_REG(idtcm->fw_ver, V520, TOD_READ_PRIMARY_CMD);
	u8 buf[TOD_BYTE_COUNT];
	u8 timeout = 10;
	u8 trigger;
	int err;

	err = idtcm_read(idtcm, channel->tod_read_primary,
			 tod_read_cmd, &trigger, sizeof(trigger));
	if (err)
		return err;

	trigger &= ~(TOD_READ_TRIGGER_MASK << TOD_READ_TRIGGER_SHIFT);
	trigger |= (1 << TOD_READ_TRIGGER_SHIFT);
	trigger &= ~TOD_READ_TRIGGER_MODE; /* single shot */

	err = idtcm_write(idtcm, channel->tod_read_primary,
			  tod_read_cmd, &trigger, sizeof(trigger));
	if (err)
		return err;

	/* wait trigger to be 0 */
	while (trigger & TOD_READ_TRIGGER_MASK) {
		if (idtcm->calculate_overhead_flag)
			idtcm->start_time = ktime_get_raw();

		err = idtcm_read(idtcm, channel->tod_read_primary,
				 tod_read_cmd, &trigger,
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

	if (qn == 0 && qn_plus_1 == 0)
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
	if (pll == 5 && qn_plus_1 != 0) {
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
	if (pll == 6 && qn_plus_1 != 0) {
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

static int idtcm_sync_pps_output(struct idtcm_channel *channel)
{
	struct idtcm *idtcm = channel->idtcm;
	u8 pll;
	u8 qn;
	u8 qn_plus_1;
	int err = 0;
	u8 out8_mux = 0;
	u8 out11_mux = 0;
	u8 temp;
	u16 output_mask = channel->output_mask;

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

		if (qn != 0 || qn_plus_1 != 0)
			err = _sync_pll_output(idtcm, pll, channel->sync_src,
					       qn, qn_plus_1);

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
			ktime_t diff = ktime_sub(ktime_get_raw(),
						 idtcm->start_time);
			total_overhead_ns =  ktime_to_ns(diff)
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

		if ((cmd & TOD_WRITE_SELECTION_MASK) == 0)
			break;

		if (++count > 20) {
			dev_err(&idtcm->client->dev,
				"Timed out waiting for the write counter");
			return -EIO;
		}
	}

	return 0;
}

static int get_output_base_addr(enum fw_version ver, u8 outn)
{
	int base;

	switch (outn) {
	case 0:
		base = IDTCM_FW_REG(ver, V520, OUTPUT_0);
		break;
	case 1:
		base = IDTCM_FW_REG(ver, V520, OUTPUT_1);
		break;
	case 2:
		base = IDTCM_FW_REG(ver, V520, OUTPUT_2);
		break;
	case 3:
		base = IDTCM_FW_REG(ver, V520, OUTPUT_3);
		break;
	case 4:
		base = IDTCM_FW_REG(ver, V520, OUTPUT_4);
		break;
	case 5:
		base = IDTCM_FW_REG(ver, V520, OUTPUT_5);
		break;
	case 6:
		base = IDTCM_FW_REG(ver, V520, OUTPUT_6);
		break;
	case 7:
		base = IDTCM_FW_REG(ver, V520, OUTPUT_7);
		break;
	case 8:
		base = IDTCM_FW_REG(ver, V520, OUTPUT_8);
		break;
	case 9:
		base = IDTCM_FW_REG(ver, V520, OUTPUT_9);
		break;
	case 10:
		base = IDTCM_FW_REG(ver, V520, OUTPUT_10);
		break;
	case 11:
		base = IDTCM_FW_REG(ver, V520, OUTPUT_11);
		break;
	default:
		base = -EINVAL;
	}

	return base;
}

static int _idtcm_settime_deprecated(struct idtcm_channel *channel,
				     struct timespec64 const *ts)
{
	struct idtcm *idtcm = channel->idtcm;
	int err;

	err = _idtcm_set_dpll_hw_tod(channel, ts, HW_TOD_WR_TRIG_SEL_MSB);
	if (err) {
		dev_err(&idtcm->client->dev,
			"%s: Set HW ToD failed", __func__);
		return err;
	}

	return idtcm_sync_pps_output(channel);
}

static int _idtcm_settime(struct idtcm_channel *channel,
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
	ktime_t diff;

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

		diff = ktime_sub(stop, start);

		current_ns = ktime_to_ns(diff);

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

static int _idtcm_adjtime_deprecated(struct idtcm_channel *channel, s64 delta)
{
	int err;
	struct idtcm *idtcm = channel->idtcm;
	struct timespec64 ts;
	s64 now;

	if (abs(delta) < PHASE_PULL_IN_THRESHOLD_NS_DEPRECATED) {
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

		err = _idtcm_settime_deprecated(channel, &ts);
	}

	return err;
}

static int idtcm_state_machine_reset(struct idtcm *idtcm)
{
	u8 byte = SM_RESET_CMD;
	u32 status = 0;
	int err;
	u8 i;

	clear_boot_status(idtcm);

	err = idtcm_write(idtcm, RESET_CTRL,
			  IDTCM_FW_REG(idtcm->fw_ver, V520, SM_RESET),
			  &byte, sizeof(byte));

	if (!err) {
		for (i = 0; i < 30; i++) {
			msleep_interruptible(100);
			read_boot_status(idtcm, &status);

			if (status == 0xA0) {
				dev_dbg(&idtcm->client->dev,
					"SM_RESET completed in %d ms", i * 100);
				break;
			}
		}

		if (!status)
			dev_err(&idtcm->client->dev,
				"Timed out waiting for CM_RESET to complete");
	}

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
		dev_err(&idtcm->client->dev, "ToD%d not supported", index);
		return -EINVAL;
	}

	if (pll >= MAX_PLL) {
		dev_err(&idtcm->client->dev, "Pll%d not supported", pll);
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
			dev_err(&idtcm->client->dev, "Invalid TOD mask 0x%02x", val);
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

	dev_dbg(&idtcm->client->dev, "tod_mask = 0x%02x", idtcm->tod_mask);

	for (i = 0; i < MAX_TOD; i++) {
		mask = 1 << i;

		if (mask & idtcm->tod_mask)
			dev_dbg(&idtcm->client->dev,
				"TOD%d pll = %d    output_mask = 0x%04x",
				i, idtcm->channel[i].pll,
				idtcm->channel[i].output_mask);
	}
}

static int idtcm_load_firmware(struct idtcm *idtcm,
			       struct device *dev)
{
	u16 scratch = IDTCM_FW_REG(idtcm->fw_ver, V520, SCRATCH);
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

	dev_dbg(&idtcm->client->dev, "requesting firmware '%s'", fname);

	err = request_firmware(&fw, fname, dev);
	if (err) {
		dev_err(&idtcm->client->dev,
			"Failed at line %d in %s!", __LINE__, __func__);
		return err;
	}

	dev_dbg(&idtcm->client->dev, "firmware size %zu bytes", fw->size);

	rec = (struct idtcm_fwrc *) fw->data;

	if (contains_full_configuration(idtcm, fw))
		idtcm_state_machine_reset(idtcm);

	for (len = fw->size; len > 0; len -= sizeof(*rec)) {
		if (rec->reserved) {
			dev_err(&idtcm->client->dev,
				"bad firmware, reserved field non-zero");
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
			if (regaddr < GPIO_USER_CONTROL || regaddr >= scratch)
				continue;

			/* Page size 128, last 4 bytes of page skipped */
			if ((loaddr > 0x7b && loaddr <= 0x7f) || loaddr > 0xfb)
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
	int base;
	int err;
	u8 val;

	base = get_output_base_addr(idtcm->fw_ver, outn);

	if (!(base > 0)) {
		dev_err(&idtcm->client->dev,
			"%s - Unsupported out%d", __func__, outn);
		return base;
	}

	err = idtcm_read(idtcm, (u16)base, OUT_CTRL_1, &val, sizeof(val));
	if (err)
		return err;

	if (enable)
		val |= SQUELCH_DISABLE;
	else
		val &= ~SQUELCH_DISABLE;

	return idtcm_write(idtcm, (u16)base, OUT_CTRL_1, &val, sizeof(val));
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
	struct idtcm *idtcm = channel->idtcm;
	unsigned int flags = perout->flags;
	struct timespec64 ts = {0, 0};
	int err;

	if (flags == PEROUT_ENABLE_OUTPUT_MASK)
		err = idtcm_output_mask_enable(channel, enable);
	else
		err = idtcm_output_enable(channel, enable, perout->index);

	if (err) {
		dev_err(&idtcm->client->dev, "Unable to set output enable");
		return err;
	}

	/* Align output to internal 1 PPS */
	return _idtcm_settime(channel, &ts, SCSR_TOD_WR_TYPE_SEL_DELTA_PLUS);
}

static int idtcm_get_pll_mode(struct idtcm_channel *channel,
			      enum pll_mode *pll_mode)
{
	struct idtcm *idtcm = channel->idtcm;
	int err;
	u8 dpll_mode;

	err = idtcm_read(idtcm, channel->dpll_n,
			 IDTCM_FW_REG(idtcm->fw_ver, V520, DPLL_MODE),
			 &dpll_mode, sizeof(dpll_mode));
	if (err)
		return err;

	*pll_mode = (dpll_mode >> PLL_MODE_SHIFT) & PLL_MODE_MASK;

	return 0;
}

static int idtcm_set_pll_mode(struct idtcm_channel *channel,
			      enum pll_mode pll_mode)
{
	struct idtcm *idtcm = channel->idtcm;
	int err;
	u8 dpll_mode;

	err = idtcm_read(idtcm, channel->dpll_n,
			 IDTCM_FW_REG(idtcm->fw_ver, V520, DPLL_MODE),
			 &dpll_mode, sizeof(dpll_mode));
	if (err)
		return err;

	dpll_mode &= ~(PLL_MODE_MASK << PLL_MODE_SHIFT);

	dpll_mode |= (pll_mode << PLL_MODE_SHIFT);

	channel->pll_mode = pll_mode;

	err = idtcm_write(idtcm, channel->dpll_n,
			  IDTCM_FW_REG(idtcm->fw_ver, V520, DPLL_MODE),
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
	}

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

	phase_50ps = div_s64(offset_ps, 50);

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

	/* 2 ^ -53 = 1.1102230246251565404236316680908e-16 */
	fcw = scaled_ppm * 244140625ULL;

	fcw = div_s64(fcw, 1776);

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
	struct idtcm_channel *channel = container_of(ptp, struct idtcm_channel, caps);
	struct idtcm *idtcm = channel->idtcm;
	int err;

	mutex_lock(&idtcm->reg_lock);

	err = _idtcm_gettime(channel, ts);
	if (err)
		dev_err(&idtcm->client->dev, "Failed at line %d in %s!",
			__LINE__, __func__);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_settime_deprecated(struct ptp_clock_info *ptp,
				    const struct timespec64 *ts)
{
	struct idtcm_channel *channel = container_of(ptp, struct idtcm_channel, caps);
	struct idtcm *idtcm = channel->idtcm;
	int err;

	mutex_lock(&idtcm->reg_lock);

	err = _idtcm_settime_deprecated(channel, ts);
	if (err)
		dev_err(&idtcm->client->dev,
			"Failed at line %d in %s!", __LINE__, __func__);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_settime(struct ptp_clock_info *ptp,
			 const struct timespec64 *ts)
{
	struct idtcm_channel *channel = container_of(ptp, struct idtcm_channel, caps);
	struct idtcm *idtcm = channel->idtcm;
	int err;

	mutex_lock(&idtcm->reg_lock);

	err = _idtcm_settime(channel, ts, SCSR_TOD_WR_TYPE_SEL_ABSOLUTE);
	if (err)
		dev_err(&idtcm->client->dev,
			"Failed at line %d in %s!", __LINE__, __func__);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_adjtime_deprecated(struct ptp_clock_info *ptp, s64 delta)
{
	struct idtcm_channel *channel = container_of(ptp, struct idtcm_channel, caps);
	struct idtcm *idtcm = channel->idtcm;
	int err;

	mutex_lock(&idtcm->reg_lock);

	err = _idtcm_adjtime_deprecated(channel, delta);
	if (err)
		dev_err(&idtcm->client->dev,
			"Failed at line %d in %s!", __LINE__, __func__);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct idtcm_channel *channel = container_of(ptp, struct idtcm_channel, caps);
	struct idtcm *idtcm = channel->idtcm;
	struct timespec64 ts;
	enum scsr_tod_write_type_sel type;
	int err;

	if (abs(delta) < PHASE_PULL_IN_THRESHOLD_NS) {
		err = idtcm_do_phase_pull_in(channel, delta, 0);
		if (err)
			dev_err(&idtcm->client->dev,
				"Failed at line %d in %s!", __LINE__, __func__);
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

	err = _idtcm_settime(channel, &ts, type);
	if (err)
		dev_err(&idtcm->client->dev,
			"Failed at line %d in %s!", __LINE__, __func__);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_adjphase(struct ptp_clock_info *ptp, s32 delta)
{
	struct idtcm_channel *channel = container_of(ptp, struct idtcm_channel, caps);
	struct idtcm *idtcm = channel->idtcm;
	int err;

	mutex_lock(&idtcm->reg_lock);

	err = _idtcm_adjphase(channel, delta);
	if (err)
		dev_err(&idtcm->client->dev,
			"Failed at line %d in %s!", __LINE__, __func__);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_adjfine(struct ptp_clock_info *ptp,  long scaled_ppm)
{
	struct idtcm_channel *channel = container_of(ptp, struct idtcm_channel, caps);
	struct idtcm *idtcm = channel->idtcm;
	int err;

	mutex_lock(&idtcm->reg_lock);

	err = _idtcm_adjfine(channel, scaled_ppm);
	if (err)
		dev_err(&idtcm->client->dev,
			"Failed at line %d in %s!", __LINE__, __func__);

	mutex_unlock(&idtcm->reg_lock);

	return err;
}

static int idtcm_enable(struct ptp_clock_info *ptp,
			struct ptp_clock_request *rq, int on)
{
	int err;
	struct idtcm_channel *channel = container_of(ptp, struct idtcm_channel, caps);

	switch (rq->type) {
	case PTP_CLK_REQ_PEROUT:
		if (!on) {
			err = idtcm_perout_enable(channel, false, &rq->perout);
			if (err)
				dev_err(&channel->idtcm->client->dev,
					"Failed at line %d in %s!",
					__LINE__, __func__);
			return err;
		}

		/* Only accept a 1-PPS aligned to the second. */
		if (rq->perout.start.nsec || rq->perout.period.sec != 1 ||
		    rq->perout.period.nsec)
			return -ERANGE;

		err = idtcm_perout_enable(channel, true, &rq->perout);
		if (err)
			dev_err(&channel->idtcm->client->dev,
				"Failed at line %d in %s!", __LINE__, __func__);
		return err;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int idtcm_enable_tod(struct idtcm_channel *channel)
{
	struct idtcm *idtcm = channel->idtcm;
	struct timespec64 ts = {0, 0};
	u16 tod_cfg = IDTCM_FW_REG(idtcm->fw_ver, V520, TOD_CFG);
	u8 cfg;
	int err;

	/* STEELAI-366 - Temporary workaround for ts2phc compatibility */
	if (0) {
		err = idtcm_output_mask_enable(channel, false);
		if (err)
			return err;
	}

	/*
	 * Start the TOD clock ticking.
	 */
	err = idtcm_read(idtcm, channel->tod_n, tod_cfg, &cfg, sizeof(cfg));
	if (err)
		return err;

	cfg |= TOD_ENABLE;

	err = idtcm_write(idtcm, channel->tod_n, tod_cfg, &cfg, sizeof(cfg));
	if (err)
		return err;

	if (idtcm->fw_ver < V487)
		return _idtcm_settime_deprecated(channel, &ts);
	else
		return _idtcm_settime(channel, &ts,
				      SCSR_TOD_WR_TYPE_SEL_ABSOLUTE);
}

static void idtcm_set_version_info(struct idtcm *idtcm)
{
	u8 major;
	u8 minor;
	u8 hotfix;
	u16 product_id;
	u8 hw_rev_id;
	u8 config_select;

	idtcm_read_major_release(idtcm, &major);
	idtcm_read_minor_release(idtcm, &minor);
	idtcm_read_hotfix_release(idtcm, &hotfix);

	idtcm_read_product_id(idtcm, &product_id);
	idtcm_read_hw_rev_id(idtcm, &hw_rev_id);

	idtcm_read_otp_scsr_config_select(idtcm, &config_select);

	snprintf(idtcm->version, sizeof(idtcm->version), "%u.%u.%u",
		 major, minor, hotfix);

	idtcm->fw_ver = idtcm_fw_version(idtcm->version);

	dev_info(&idtcm->client->dev,
		 "%d.%d.%d, Id: 0x%04x  HW Rev: %d  OTP Config Select: %d",
		 major, minor, hotfix,
		 product_id, hw_rev_id, config_select);
}

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
};

static const struct ptp_clock_info idtcm_caps_deprecated = {
	.owner		= THIS_MODULE,
	.max_adj	= 244000,
	.n_per_out	= 12,
	.adjphase	= &idtcm_adjphase,
	.adjfine	= &idtcm_adjfine,
	.adjtime	= &idtcm_adjtime_deprecated,
	.gettime64	= &idtcm_gettime,
	.settime64	= &idtcm_settime_deprecated,
	.enable		= &idtcm_enable,
};

static int configure_channel_pll(struct idtcm_channel *channel)
{
	struct idtcm *idtcm = channel->idtcm;
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
		channel->dpll_n = IDTCM_FW_REG(idtcm->fw_ver, V520, DPLL_2);
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
		channel->dpll_n = IDTCM_FW_REG(idtcm->fw_ver, V520, DPLL_4);
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
		channel->dpll_n = IDTCM_FW_REG(idtcm->fw_ver, V520, DPLL_6);
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
	enum fw_version fw_ver = idtcm->fw_ver;
	struct idtcm_channel *channel;
	int err;

	if (!(index < MAX_TOD))
		return -EINVAL;

	channel = &idtcm->channel[index];
	channel->idtcm = idtcm;

	/* Set pll addresses */
	err = configure_channel_pll(channel);
	if (err)
		return err;

	/* Set tod addresses */
	switch (index) {
	case 0:
		channel->tod_read_primary = IDTCM_FW_REG(fw_ver, V520, TOD_READ_PRIMARY_0);
		channel->tod_write = IDTCM_FW_REG(fw_ver, V520, TOD_WRITE_0);
		channel->tod_n = IDTCM_FW_REG(fw_ver, V520, TOD_0);
		channel->sync_src = SYNC_SOURCE_DPLL0_TOD_PPS;
		break;
	case 1:
		channel->tod_read_primary = IDTCM_FW_REG(fw_ver, V520, TOD_READ_PRIMARY_1);
		channel->tod_write = IDTCM_FW_REG(fw_ver, V520, TOD_WRITE_1);
		channel->tod_n = IDTCM_FW_REG(fw_ver, V520, TOD_1);
		channel->sync_src = SYNC_SOURCE_DPLL1_TOD_PPS;
		break;
	case 2:
		channel->tod_read_primary = IDTCM_FW_REG(fw_ver, V520, TOD_READ_PRIMARY_2);
		channel->tod_write = IDTCM_FW_REG(fw_ver, V520, TOD_WRITE_2);
		channel->tod_n = IDTCM_FW_REG(fw_ver, V520, TOD_2);
		channel->sync_src = SYNC_SOURCE_DPLL2_TOD_PPS;
		break;
	case 3:
		channel->tod_read_primary = IDTCM_FW_REG(fw_ver, V520, TOD_READ_PRIMARY_3);
		channel->tod_write = IDTCM_FW_REG(fw_ver, V520, TOD_WRITE_3);
		channel->tod_n = IDTCM_FW_REG(fw_ver, V520, TOD_3);
		channel->sync_src = SYNC_SOURCE_DPLL3_TOD_PPS;
		break;
	default:
		return -EINVAL;
	}

	if (idtcm->fw_ver < V487)
		channel->caps = idtcm_caps_deprecated;
	else
		channel->caps = idtcm_caps;

	snprintf(channel->caps.name, sizeof(channel->caps.name),
		 "IDT CM TOD%u", index);

	/* Sync pll mode with hardware */
	err = idtcm_get_pll_mode(channel, &channel->pll_mode);
	if (err) {
		dev_err(&idtcm->client->dev,
			"Error: %s - Unable to read pll mode", __func__);
		return err;
	}

	err = idtcm_enable_tod(channel);
	if (err) {
		dev_err(&idtcm->client->dev,
			"Failed at line %d in %s!", __LINE__, __func__);
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

	dev_info(&idtcm->client->dev, "PLL%d registered as ptp%d",
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

	idtcm_set_version_info(idtcm);

	err = idtcm_load_firmware(idtcm, &client->dev);
	if (err)
		dev_warn(&idtcm->client->dev, "loading firmware failed with %d", err);

	wait_for_chip_ready(idtcm);

	if (idtcm->tod_mask) {
		for (i = 0; i < MAX_TOD; i++) {
			if (idtcm->tod_mask & (1 << i)) {
				err = idtcm_enable_channel(idtcm, i);
				if (err) {
					dev_err(&idtcm->client->dev,
						"idtcm_enable_channel %d failed!", i);
					break;
				}
			}
		}
	} else {
		dev_err(&idtcm->client->dev,
			"no PLLs flagged as PHCs, nothing to do");
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
