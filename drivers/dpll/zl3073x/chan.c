// SPDX-License-Identifier: GPL-2.0-only

#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include "chan.h"
#include "core.h"

/**
 * zl3073x_chan_state_update - update DPLL channel status from HW
 * @zldev: pointer to zl3073x_dev structure
 * @index: DPLL channel index
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_chan_state_update(struct zl3073x_dev *zldev, u8 index)
{
	struct zl3073x_chan *chan = &zldev->chan[index];
	u64 val;
	int rc;

	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_MON_STATUS(index),
			     &chan->mon_status);
	if (rc)
		return rc;

	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_REFSEL_STATUS(index),
			     &chan->refsel_status);
	if (rc)
		return rc;

	/* Read df_offset only when locked to a reference. In NCO mode
	 * df_offset was captured at entry by nco_mode_set() - preserve it.
	 */
	if (!zl3073x_chan_is_locked(chan)) {
		if (!zl3073x_chan_mode_is_nco(chan))
			chan->df_offset = ZL_DPLL_DF_OFFSET_UNKNOWN;
		return 0;
	}

	rc = zl3073x_poll_zero_u8(zldev, ZL_REG_DPLL_DF_READ(index),
				  ZL_DPLL_DF_READ_SEM,
				  ZL_POLL_DF_READ_TIMEOUT_US);
	if (rc)
		return rc;

	rc = zl3073x_write_u8(zldev, ZL_REG_DPLL_DF_READ(index),
			      ZL_DPLL_DF_READ_SEM | ZL_DPLL_DF_READ_REF_OFST);
	if (rc)
		return rc;

	rc = zl3073x_poll_zero_u8(zldev, ZL_REG_DPLL_DF_READ(index),
				  ZL_DPLL_DF_READ_SEM,
				  ZL_POLL_DF_READ_TIMEOUT_US);
	if (rc)
		return rc;

	rc = zl3073x_read_u48(zldev, ZL_REG_DPLL_DF_OFFSET(index), &val);
	if (rc)
		return rc;

	chan->df_offset = sign_extend64(val, 47);

	return 0;
}

/**
 * zl3073x_chan_nco_mode_set - switch DPLL channel to NCO mode
 * @zldev: pointer to zl3073x_dev structure
 * @index: DPLL channel index
 *
 * Switches the channel to NCO mode and reads the df_offset
 * auto-captured by nco_auto_read directly from the register.
 * No DF_READ handshake is needed as nco_auto_read populates
 * the register before the mode switch completes.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_chan_nco_mode_set(struct zl3073x_dev *zldev, u8 index)
{
	struct zl3073x_chan *chan = &zldev->chan[index];
	u8 prev_mode, df_read;
	u64 val;
	int rc;

	prev_mode = zl3073x_chan_mode_get(chan);

	/* nco_auto_read captures the tracking offset at NCO entry only
	 * from reflock, auto or holdover mode. From freerun the captured
	 * value is not meaningful.
	 */
	if (prev_mode == ZL_DPLL_MODE_REFSEL_MODE_FREERUN) {
		zl3073x_chan_mode_set(chan, ZL_DPLL_MODE_REFSEL_MODE_NCO);

		rc = zl3073x_write_u8(zldev, ZL_REG_DPLL_MODE_REFSEL(index),
				      chan->mode_refsel);
		if (rc) {
			zl3073x_chan_mode_set(chan, prev_mode);
			return rc;
		}

		chan->df_offset = ZL_DPLL_DF_OFFSET_UNKNOWN;
		return 0;
	}

	/* Configure df_read for nco_auto_read:
	 * ref_ofst=0 - reads offset relative to master clock (not input ref)
	 * cmd=CMD_ACC_I - accumulated I-part covering both locked and
	 *                 holdover entry.
	 *
	 * No semaphore is set - this only configures what the df_offset
	 * value represents after the mode switch; nco_auto_read performs
	 * the actual read automatically.
	 */
	df_read = FIELD_PREP(ZL_DPLL_DF_READ_REF_OFST, 0) |
		  FIELD_PREP(ZL_DPLL_DF_READ_CMD, ZL_DPLL_DF_READ_CMD_ACC_I);
	rc = zl3073x_write_u8(zldev, ZL_REG_DPLL_DF_READ(index), df_read);
	if (rc)
		return rc;

	/* Wait for df_read configuration to take effect before
	 * triggering nco_auto_read via mode switch. The worst-case
	 * internal register update time is 25 ms.
	 */
	fsleep(25000);

	zl3073x_chan_mode_set(chan, ZL_DPLL_MODE_REFSEL_MODE_NCO);
	rc = zl3073x_write_u8(zldev, ZL_REG_DPLL_MODE_REFSEL(index),
			      chan->mode_refsel);
	if (rc) {
		zl3073x_chan_mode_set(chan, prev_mode);
		return rc;
	}

	/* Wait for nco_auto_read to populate df_offset. The worst-case
	 * internal register update time is 25 ms.
	 */
	fsleep(25000);

	/* Read df_offset captured by nco_auto_read during mode switch.
	 * No DF_READ semaphore handshake needed. Mode switch already
	 * succeeded, so don't propagate a read failure back to userspace.
	 */
	rc = zl3073x_read_u48(zldev, ZL_REG_DPLL_DF_OFFSET(index), &val);
	if (rc) {
		dev_warn(zldev->dev,
			 "Failed to read DPLL%u df_offset: %pe\n",
			 index, ERR_PTR(rc));
		chan->df_offset = ZL_DPLL_DF_OFFSET_UNKNOWN;
	} else {
		chan->df_offset = sign_extend64(val, 47);
	}

	return 0;
}

/**
 * zl3073x_chan_state_fetch - fetch DPLL channel state from hardware
 * @zldev: pointer to zl3073x_dev structure
 * @index: DPLL channel index to fetch state for
 *
 * Reads the mode_refsel, status and reference priority registers for
 * the given DPLL channel and stores the values for later use.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_chan_state_fetch(struct zl3073x_dev *zldev, u8 index)
{
	struct zl3073x_chan *chan = &zldev->chan[index];
	int rc, i;

	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_CTRL(index), &chan->ctrl);
	if (rc)
		return rc;

	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_MODE_REFSEL(index),
			     &chan->mode_refsel);
	if (rc)
		return rc;

	dev_dbg(zldev->dev, "DPLL%u mode: %u, ref: %u\n", index,
		zl3073x_chan_mode_get(chan), zl3073x_chan_ref_get(chan));

	rc = zl3073x_chan_state_update(zldev, index);
	if (rc)
		return rc;

	/* If firmware left the channel in NCO mode, mark df_offset as
	 * unknown - we cannot know whether the preconditions for a valid
	 * nco_auto_read capture were met.
	 */
	if (zl3073x_chan_mode_is_nco(chan))
		chan->df_offset = ZL_DPLL_DF_OFFSET_UNKNOWN;

	dev_dbg(zldev->dev,
		"DPLL%u lock_state: %u, ho: %u, sel_state: %u, sel_ref: %u\n",
		index, zl3073x_chan_lock_state_get(chan),
		zl3073x_chan_is_ho_ready(chan) ? 1 : 0,
		zl3073x_chan_refsel_state_get(chan),
		zl3073x_chan_refsel_ref_get(chan));

	guard(mutex)(&zldev->multiop_lock);

	/* Read DPLL configuration from mailbox */
	rc = zl3073x_mb_op(zldev, ZL_REG_DPLL_MB_SEM, ZL_DPLL_MB_SEM_RD,
			   ZL_REG_DPLL_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* Read reference priority registers */
	for (i = 0; i < ARRAY_SIZE(chan->ref_prio); i++) {
		rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_REF_PRIO(i),
				     &chan->ref_prio[i]);
		if (rc)
			return rc;
	}

	return 0;
}

/**
 * zl3073x_chan_state_get - get current DPLL channel state
 * @zldev: pointer to zl3073x_dev structure
 * @index: DPLL channel index to get state for
 *
 * Return: pointer to given DPLL channel state
 */
const struct zl3073x_chan *zl3073x_chan_state_get(struct zl3073x_dev *zldev,
						  u8 index)
{
	return &zldev->chan[index];
}

/**
 * zl3073x_chan_tod_ready_wait - wait for ToD semaphore to clear
 * @zldev: pointer to zl3073x device
 * @ch: DPLL channel index
 *
 * Checks the ToD control register semaphore bit. If clear, returns
 * immediately. Otherwise polls until the bit is cleared by the device.
 *
 * Return:
 * * 0			- success
 * * %-EBUSY		- timeout
 * * %-EOPNOTSUPP	- unknown command detected
 * * negative		- other error
 */
int zl3073x_chan_tod_ready_wait(struct zl3073x_dev *zldev, u8 ch)
{
	unsigned int timeout;
	u8 tod_ctrl;
	int rc;

	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_TOD_CTRL(ch), &tod_ctrl);
	if (rc)
		return rc;

	if (!(tod_ctrl & ZL_DPLL_TOD_CTRL_SEM))
		return 0;

	switch (FIELD_GET(ZL_DPLL_TOD_CTRL_CMD, tod_ctrl)) {
	case ZL_DPLL_TOD_CTRL_CMD_WR_NEXT_1HZ:
		timeout = ZL_POLL_TOD_WR_TIMEOUT_US;
		break;
	case ZL_DPLL_TOD_CTRL_CMD_RD_CURRENT:
	case ZL_DPLL_TOD_CTRL_CMD_RD_NEXT_1HZ:
		timeout = ZL_POLL_TOD_RD_TIMEOUT_US;
		break;
	default:
		return -EOPNOTSUPP;
	}

	rc = zl3073x_poll_zero_u8(zldev, ZL_REG_DPLL_TOD_CTRL(ch),
				  ZL_DPLL_TOD_CTRL_SEM, timeout);

	return rc == -ETIMEDOUT ? -EBUSY : rc;
}

/**
 * zl3073x_chan_tod_ctrl - issue ToD command
 * @zldev: pointer to zl3073x device
 * @ch: DPLL channel index
 * @cmd: ToD command to execute
 *
 * Writes the semaphore and command to dpll_tod_ctrl. The caller must
 * ensure the device is ready (semaphore clear) before calling and
 * must wait for completion if needed.
 *
 * Return: 0 on success, <0 on error
 */
static int zl3073x_chan_tod_ctrl(struct zl3073x_dev *zldev, u8 ch, u8 cmd)
{
	return zl3073x_write_u8(zldev, ZL_REG_DPLL_TOD_CTRL(ch),
				ZL_DPLL_TOD_CTRL_SEM | cmd);
}

/**
 * zl3073x_chan_tod_read - read ToD registers after issuing a command
 * @zldev: pointer to zl3073x device
 * @ch: DPLL channel index
 * @next_hz: if true, read predicted ToD at next 1 Hz; otherwise read current
 * @ts: timespec to store the result
 * @sts: optional system timestamp pair for cross-timestamping
 *
 * Context: Caller must serialize all zl3073x_chan_tod_* calls externally.
 * Return: 0 on success, <0 on error
 */
int zl3073x_chan_tod_read(struct zl3073x_dev *zldev, u8 ch,
			  bool next_hz, struct timespec64 *ts,
			  struct ptp_system_timestamp *sts)
{
	u32 nsec;
	u64 sec;
	u8 cmd;
	int rc;

	if (next_hz)
		cmd = ZL_DPLL_TOD_CTRL_CMD_RD_NEXT_1HZ;
	else
		cmd = ZL_DPLL_TOD_CTRL_CMD_RD_CURRENT;

	/* Wait for any previous ToD operation to complete */
	rc = zl3073x_chan_tod_ready_wait(zldev, ch);
	if (rc)
		return rc;

	ptp_read_system_prets(sts);
	rc = zl3073x_chan_tod_ctrl(zldev, ch, cmd);
	if (rc)
		return rc;

	rc = zl3073x_chan_tod_ready_wait(zldev, ch);
	if (rc)
		return rc;
	ptp_read_system_postts(sts);

	rc = zl3073x_read_u48(zldev, ZL_REG_DPLL_TOD_SEC(ch), &sec);
	if (rc)
		return rc;

	/* HW nanoseconds are always in [0, NSEC_PER_SEC) range */
	rc = zl3073x_read_u32(zldev, ZL_REG_DPLL_TOD_NS(ch), &nsec);
	if (rc)
		return rc;

	ts->tv_sec = sec;
	ts->tv_nsec = nsec;

	return 0;
}

/**
 * zl3073x_chan_tod_write - write ToD registers and trigger 1 Hz update
 * @zldev: pointer to zl3073x device
 * @ch: DPLL channel index
 * @ts: time to set
 *
 * Context: Caller must serialize all zl3073x_chan_tod_* calls externally.
 * Return: 0 on success, <0 on error
 */
int zl3073x_chan_tod_write(struct zl3073x_dev *zldev, u8 ch,
			   struct timespec64 ts)
{
	int rc;

	/* Wait for any previous ToD operation to complete */
	rc = zl3073x_chan_tod_ready_wait(zldev, ch);
	if (rc)
		return rc;

	rc = zl3073x_write_u48(zldev, ZL_REG_DPLL_TOD_SEC(ch), ts.tv_sec);
	if (rc)
		return rc;

	rc = zl3073x_write_u32(zldev, ZL_REG_DPLL_TOD_NS(ch), ts.tv_nsec);
	if (rc)
		return rc;

	return zl3073x_chan_tod_ctrl(zldev, ch,
				     ZL_DPLL_TOD_CTRL_CMD_WR_NEXT_1HZ);
}

/**
 * zl3073x_chan_tod_adjust - atomic ToD read-modify-write with rollover guard
 * @zldev: pointer to zl3073x device
 * @ch: DPLL channel index
 * @delta: time adjustment to apply
 *
 * Reads the next-Hz ToD and current ToD, then checks whether enough time
 * remains before the next 1 Hz rollover to safely complete the write.
 * Re-reads if the 1 Hz tick crossed between the two reads or if less
 * than 20 ms remains before the next rollover. Applies @delta and writes
 * the result back.
 *
 * Context: Caller must serialize all zl3073x_chan_tod_* calls externally.
 * Return: 0 on success, <0 on error
 */
int zl3073x_chan_tod_adjust(struct zl3073x_dev *zldev, u8 ch,
			    struct timespec64 delta)
{
#define ZL_TOD_MAX_RETRIES	20
	static const long threshold_ns = 20 * NSEC_PER_MSEC;
	struct timespec64 ts_next, ts_cur, diff;
	int rc, i;

	for (i = 0; i < ZL_TOD_MAX_RETRIES; i++) {
		rc = zl3073x_chan_tod_read(zldev, ch, true, &ts_next, NULL);
		if (rc)
			return rc;

		rc = zl3073x_chan_tod_read(zldev, ch, false, &ts_cur, NULL);
		if (rc)
			return rc;

		/* Ensure the 1 Hz tick did not cross between the two reads
		 * and that enough margin remains to complete the write.
		 */
		diff = timespec64_sub(ts_next, ts_cur);
		if (diff.tv_sec > 0 ||
		    (!diff.tv_sec && diff.tv_nsec >= threshold_ns))
			break;
	}
	if (i == ZL_TOD_MAX_RETRIES) {
		dev_warn(zldev->dev,
			 "DPLL%u ToD adjust failed to get stable margin\n",
			 ch);
		return -EBUSY;
	}

	/* Apply delta to the next-Hz ToD */
	ts_next = timespec64_add(ts_next, delta);
	if (!timespec64_valid_settod(&ts_next))
		return -EINVAL;

	return zl3073x_chan_tod_write(zldev, ch, ts_next);
#undef ZL_TOD_MAX_RETRIES
}

/**
 * zl3073x_chan_df_offset_set - write delta frequency offset to hardware
 * @zldev: pointer to zl3073x device
 * @ch: DPLL channel index
 * @offset: frequency offset in 2^-48 steps
 *
 * Context: Caller must hold the per-DPLL lock.
 * Return: 0 on success, <0 on error
 */
int zl3073x_chan_df_offset_set(struct zl3073x_dev *zldev, u8 ch, s64 offset)
{
	int rc;

	rc = zl3073x_write_u48(zldev, ZL_REG_DPLL_DF_OFFSET(ch), offset);
	if (!rc)
		zldev->chan[ch].df_offset = offset;

	return rc;
}

/**
 * zl3073x_chan_tie_write - adjust DPLL phase using TIE write
 * @zldev: pointer to zl3073x device
 * @ch: DPLL channel index
 * @delta_ns: phase adjustment in nanoseconds (must be in (-1s, 1s))
 *
 * Converts nanoseconds to TIE units (0.01 ps) and writes TIE data
 * to the specified channel.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_chan_tie_write(struct zl3073x_dev *zldev, u8 ch, s64 delta_ns)
{
	s64 tie_data;
	int rc;

	guard(mutex)(&zldev->tie_lock);

	/* Wait for any previous TIE operation to complete */
	rc = zl3073x_poll_zero_u8(zldev, ZL_REG_DPLL_TIE_CTRL,
				  ZL_DPLL_TIE_CTRL_OP,
				  ZL_POLL_TIE_WR_TIMEOUT_US);
	if (rc)
		return rc;

	/* Convert ns to TIE units (0.01 ps = 10^-14 s) */
	tie_data = delta_ns * 100000LL;

	rc = zl3073x_write_u48(zldev, ZL_REG_DPLL_TIE_DATA(ch), tie_data);
	if (rc)
		return rc;

	rc = zl3073x_write_u8(zldev, ZL_REG_DPLL_TIE_CTRL_MASK, BIT(ch));
	if (rc)
		return rc;

	return zl3073x_write_u8(zldev, ZL_REG_DPLL_TIE_CTRL,
				ZL_DPLL_TIE_CTRL_OP_WR);
}

/**
 * zl3073x_chan_phase_step - execute one output phase step operation
 * @zldev: pointer to zl3073x device
 * @ch: DPLL channel index
 * @out_mask: bitmask of outputs to step
 * @step_cycles: phase step in synthesizer clock cycles
 * @tod_step: also step the ToD counter
 *
 * All masked outputs must use synthesizers of the same frequency since
 * the step value is in synthesizer clock cycles.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_chan_phase_step(struct zl3073x_dev *zldev, u8 ch,
			    u16 out_mask, s32 step_cycles,
			    bool tod_step)
{
	u8 ctrl;
	int rc;

	guard(mutex)(&zldev->phase_step_lock);

	/* Wait for any previous phase step operation to complete */
	rc = zl3073x_poll_zero_u8(zldev, ZL_REG_OUTPUT_PHASE_STEP_CTRL,
				  ZL_OUTPUT_PHASE_STEP_CTRL_OP,
				  ZL_POLL_PHASE_STEP_TIMEOUT_US);
	if (rc)
		return rc;

	rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_PHASE_STEP_DATA,
			       step_cycles);
	if (rc)
		return rc;

	rc = zl3073x_write_u16(zldev, ZL_REG_OUTPUT_PHASE_STEP_MASK, out_mask);
	if (rc)
		return rc;

	rc = zl3073x_write_u8(zldev, ZL_REG_OUTPUT_PHASE_STEP_NUMBER, 1);
	if (rc)
		return rc;

	ctrl = FIELD_PREP(ZL_OUTPUT_PHASE_STEP_CTRL_DPLL, ch) |
	       FIELD_PREP(ZL_OUTPUT_PHASE_STEP_CTRL_OP,
			  ZL_OUTPUT_PHASE_STEP_CTRL_OP_WRITE);
	if (tod_step)
		ctrl |= ZL_OUTPUT_PHASE_STEP_CTRL_TOD_STEP;

	return zl3073x_write_u8(zldev, ZL_REG_OUTPUT_PHASE_STEP_CTRL, ctrl);
}

/**
 * zl3073x_chan_state_set - commit DPLL channel state changes to hardware
 * @zldev: pointer to zl3073x_dev structure
 * @index: DPLL channel index to set state for
 * @chan: desired channel state
 *
 * Skips the HW write if the configuration is unchanged, and otherwise
 * writes only the changed registers to hardware. The mode_refsel register
 * is written directly, while the reference priority registers are written
 * via the DPLL mailbox interface.
 *
 * Return: 0 on success, <0 on HW error
 */
int zl3073x_chan_state_set(struct zl3073x_dev *zldev, u8 index,
			   const struct zl3073x_chan *chan)
{
	struct zl3073x_chan *dchan = &zldev->chan[index];
	int rc, i;

	/* Skip HW write if configuration hasn't changed */
	if (!memcmp(&dchan->cfg, &chan->cfg, sizeof(chan->cfg)))
		return 0;

	/* Direct register writes for ctrl and mode_refsel */
	if (dchan->ctrl != chan->ctrl) {
		rc = zl3073x_write_u8(zldev, ZL_REG_DPLL_CTRL(index),
				      chan->ctrl);
		if (rc)
			return rc;
		dchan->ctrl = chan->ctrl;
	}

	if (dchan->mode_refsel != chan->mode_refsel) {
		rc = zl3073x_write_u8(zldev, ZL_REG_DPLL_MODE_REFSEL(index),
				      chan->mode_refsel);
		if (rc)
			return rc;
		dchan->mode_refsel = chan->mode_refsel;
	}

	/* Mailbox write for ref_prio if changed */
	if (!memcmp(dchan->ref_prio, chan->ref_prio, sizeof(chan->ref_prio))) {
		dchan->cfg = chan->cfg;
		return 0;
	}

	guard(mutex)(&zldev->multiop_lock);

	/* Read DPLL configuration into mailbox */
	rc = zl3073x_mb_op(zldev, ZL_REG_DPLL_MB_SEM, ZL_DPLL_MB_SEM_RD,
			   ZL_REG_DPLL_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* Update changed ref_prio registers */
	for (i = 0; i < ARRAY_SIZE(chan->ref_prio); i++) {
		if (dchan->ref_prio[i] != chan->ref_prio[i]) {
			rc = zl3073x_write_u8(zldev,
					      ZL_REG_DPLL_REF_PRIO(i),
					      chan->ref_prio[i]);
			if (rc)
				return rc;
		}
	}

	/* Commit DPLL configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_DPLL_MB_SEM, ZL_DPLL_MB_SEM_WR,
			   ZL_REG_DPLL_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* After successful write store new state */
	dchan->cfg = chan->cfg;

	return 0;
}
