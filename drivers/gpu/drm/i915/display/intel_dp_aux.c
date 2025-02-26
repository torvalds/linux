// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020-2021 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_aux.h"
#include "intel_dp_aux_regs.h"
#include "intel_pps.h"
#include "intel_quirks.h"
#include "intel_tc.h"
#include "intel_uncore_trace.h"

#define AUX_CH_NAME_BUFSIZE	6

static const char *aux_ch_name(struct intel_display *display,
			       char *buf, int size, enum aux_ch aux_ch)
{
	if (DISPLAY_VER(display) >= 13 && aux_ch >= AUX_CH_D_XELPD)
		snprintf(buf, size, "%c", 'A' + aux_ch - AUX_CH_D_XELPD + AUX_CH_D);
	else if (DISPLAY_VER(display) >= 12 && aux_ch >= AUX_CH_USBC1)
		snprintf(buf, size, "USBC%c", '1' + aux_ch - AUX_CH_USBC1);
	else
		snprintf(buf, size, "%c", 'A' + aux_ch);

	return buf;
}

u32 intel_dp_aux_pack(const u8 *src, int src_bytes)
{
	int i;
	u32 v = 0;

	if (src_bytes > 4)
		src_bytes = 4;
	for (i = 0; i < src_bytes; i++)
		v |= ((u32)src[i]) << ((3 - i) * 8);
	return v;
}

static void intel_dp_aux_unpack(u32 src, u8 *dst, int dst_bytes)
{
	int i;

	if (dst_bytes > 4)
		dst_bytes = 4;
	for (i = 0; i < dst_bytes; i++)
		dst[i] = src >> ((3 - i) * 8);
}

static u32
intel_dp_aux_wait_done(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	i915_reg_t ch_ctl = intel_dp->aux_ch_ctl_reg(intel_dp);
	const unsigned int timeout_ms = 10;
	u32 status;
	int ret;

	ret = intel_de_wait_custom(display, ch_ctl, DP_AUX_CH_CTL_SEND_BUSY,
				   0,
				   2, timeout_ms, &status);

	if (ret == -ETIMEDOUT)
		drm_err(display->drm,
			"%s: did not complete or timeout within %ums (status 0x%08x)\n",
			intel_dp->aux.name, timeout_ms, status);

	return status;
}

static u32 g4x_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	struct intel_display *display = to_intel_display(intel_dp);

	if (index)
		return 0;

	/*
	 * The clock divider is based off the hrawclk, and would like to run at
	 * 2MHz.  So, take the hrawclk value and divide by 2000 and use that
	 */
	return DIV_ROUND_CLOSEST(DISPLAY_RUNTIME_INFO(display)->rawclk_freq, 2000);
}

static u32 ilk_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	u32 freq;

	if (index)
		return 0;

	/*
	 * The clock divider is based off the cdclk or PCH rawclk, and would
	 * like to run at 2MHz.  So, take the cdclk or PCH rawclk value and
	 * divide by 2000 and use that
	 */
	if (dig_port->aux_ch == AUX_CH_A)
		freq = display->cdclk.hw.cdclk;
	else
		freq = DISPLAY_RUNTIME_INFO(display)->rawclk_freq;
	return DIV_ROUND_CLOSEST(freq, 2000);
}

static u32 hsw_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_i915_private *i915 = to_i915(display->drm);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);

	if (dig_port->aux_ch != AUX_CH_A && HAS_PCH_LPT_H(i915)) {
		/* Workaround for non-ULT HSW */
		switch (index) {
		case 0: return 63;
		case 1: return 72;
		default: return 0;
		}
	}

	return ilk_get_aux_clock_divider(intel_dp, index);
}

static u32 skl_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	/*
	 * SKL doesn't need us to program the AUX clock divider (Hardware will
	 * derive the clock from CDCLK automatically). We still implement the
	 * get_aux_clock_divider vfunc to plug-in into the existing code.
	 */
	return index ? 0 : 1;
}

static int intel_dp_aux_sync_len(void)
{
	int precharge = 16; /* 10-16 */
	int preamble = 16;

	return precharge + preamble;
}

int intel_dp_aux_fw_sync_len(struct intel_dp *intel_dp)
{
	int precharge = 10; /* 10-16 */
	int preamble = 8;

	/*
	 * We faced some glitches on Dell Precision 5490 MTL laptop with panel:
	 * "Manufacturer: AUO, Model: 63898" when using HW default 18. Using 20
	 * is fixing these problems with the panel. It is still within range
	 * mentioned in eDP specification. Increasing Fast Wake sync length is
	 * causing problems with other panels: increase length as a quirk for
	 * this specific laptop.
	 */
	if (intel_has_dpcd_quirk(intel_dp, QUIRK_FW_SYNC_LEN))
		precharge += 2;

	return precharge + preamble;
}

static int g4x_dp_aux_precharge_len(void)
{
	int precharge_min = 10;
	int preamble = 16;

	/* HW wants the length of the extra precharge in 2us units */
	return (intel_dp_aux_sync_len() -
		precharge_min - preamble) / 2;
}

static u32 g4x_get_aux_send_ctl(struct intel_dp *intel_dp,
				int send_bytes,
				u32 aux_clock_divider)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	u32 timeout;

	/* Max timeout value on G4x-BDW: 1.6ms */
	if (IS_BROADWELL(i915))
		timeout = DP_AUX_CH_CTL_TIME_OUT_600us;
	else
		timeout = DP_AUX_CH_CTL_TIME_OUT_400us;

	return DP_AUX_CH_CTL_SEND_BUSY |
		DP_AUX_CH_CTL_DONE |
		DP_AUX_CH_CTL_INTERRUPT |
		DP_AUX_CH_CTL_TIME_OUT_ERROR |
		timeout |
		DP_AUX_CH_CTL_RECEIVE_ERROR |
		DP_AUX_CH_CTL_MESSAGE_SIZE(send_bytes) |
		DP_AUX_CH_CTL_PRECHARGE_2US(g4x_dp_aux_precharge_len()) |
		DP_AUX_CH_CTL_BIT_CLOCK_2X(aux_clock_divider);
}

static u32 skl_get_aux_send_ctl(struct intel_dp *intel_dp,
				int send_bytes,
				u32 unused)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	u32 ret;

	/*
	 * Max timeout values:
	 * SKL-GLK: 1.6ms
	 * ICL+: 4ms
	 */
	ret = DP_AUX_CH_CTL_SEND_BUSY |
		DP_AUX_CH_CTL_DONE |
		DP_AUX_CH_CTL_INTERRUPT |
		DP_AUX_CH_CTL_TIME_OUT_ERROR |
		DP_AUX_CH_CTL_TIME_OUT_MAX |
		DP_AUX_CH_CTL_RECEIVE_ERROR |
		DP_AUX_CH_CTL_MESSAGE_SIZE(send_bytes) |
		DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL(intel_dp_aux_fw_sync_len(intel_dp)) |
		DP_AUX_CH_CTL_SYNC_PULSE_SKL(intel_dp_aux_sync_len());

	if (intel_tc_port_in_tbt_alt_mode(dig_port))
		ret |= DP_AUX_CH_CTL_TBT_IO;

	/*
	 * Power request bit is already set during aux power well enable.
	 * Preserve the bit across aux transactions.
	 */
	if (DISPLAY_VER(display) >= 14)
		ret |= XELPDP_DP_AUX_CH_CTL_POWER_REQUEST;

	return ret;
}

static int
intel_dp_aux_xfer(struct intel_dp *intel_dp,
		  const u8 *send, int send_bytes,
		  u8 *recv, int recv_size,
		  u32 aux_send_ctl_flags)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &dig_port->base;
	i915_reg_t ch_ctl, ch_data[5];
	u32 aux_clock_divider;
	enum intel_display_power_domain aux_domain;
	intel_wakeref_t aux_wakeref;
	intel_wakeref_t pps_wakeref;
	int i, ret, recv_bytes;
	int try, clock = 0;
	u32 status;
	bool vdd;

	ch_ctl = intel_dp->aux_ch_ctl_reg(intel_dp);
	for (i = 0; i < ARRAY_SIZE(ch_data); i++)
		ch_data[i] = intel_dp->aux_ch_data_reg(intel_dp, i);

	intel_digital_port_lock(encoder);
	/*
	 * Abort transfers on a disconnected port as required by
	 * DP 1.4a link CTS 4.2.1.5, also avoiding the long AUX
	 * timeouts that would otherwise happen.
	 */
	if (!intel_dp_is_edp(intel_dp) &&
	    !intel_digital_port_connected_locked(&dig_port->base)) {
		ret = -ENXIO;
		goto out_unlock;
	}

	aux_domain = intel_aux_power_domain(dig_port);

	aux_wakeref = intel_display_power_get(display, aux_domain);
	pps_wakeref = intel_pps_lock(intel_dp);

	/*
	 * We will be called with VDD already enabled for dpcd/edid/oui reads.
	 * In such cases we want to leave VDD enabled and it's up to upper layers
	 * to turn it off. But for eg. i2c-dev access we need to turn it on/off
	 * ourselves.
	 */
	vdd = intel_pps_vdd_on_unlocked(intel_dp);

	/*
	 * dp aux is extremely sensitive to irq latency, hence request the
	 * lowest possible wakeup latency and so prevent the cpu from going into
	 * deep sleep states.
	 */
	cpu_latency_qos_update_request(&intel_dp->pm_qos, 0);

	intel_pps_check_power_unlocked(intel_dp);

	/*
	 * FIXME PSR should be disabled here to prevent
	 * it using the same AUX CH simultaneously
	 */

	/* Try to wait for any previous AUX channel activity */
	for (try = 0; try < 3; try++) {
		status = intel_de_read_notrace(display, ch_ctl);
		if ((status & DP_AUX_CH_CTL_SEND_BUSY) == 0)
			break;
		msleep(1);
	}
	/* just trace the final value */
	trace_i915_reg_rw(false, ch_ctl, status, sizeof(status), true);

	if (try == 3) {
		const u32 status = intel_de_read(display, ch_ctl);

		if (status != intel_dp->aux_busy_last_status) {
			drm_WARN(display->drm, 1,
				 "%s: not started (status 0x%08x)\n",
				 intel_dp->aux.name, status);
			intel_dp->aux_busy_last_status = status;
		}

		ret = -EBUSY;
		goto out;
	}

	/* Only 5 data registers! */
	if (drm_WARN_ON(display->drm, send_bytes > 20 || recv_size > 20)) {
		ret = -E2BIG;
		goto out;
	}

	while ((aux_clock_divider = intel_dp->get_aux_clock_divider(intel_dp, clock++))) {
		u32 send_ctl = intel_dp->get_aux_send_ctl(intel_dp,
							  send_bytes,
							  aux_clock_divider);

		send_ctl |= aux_send_ctl_flags;

		/* Must try at least 3 times according to DP spec */
		for (try = 0; try < 5; try++) {
			/* Load the send data into the aux channel data registers */
			for (i = 0; i < send_bytes; i += 4)
				intel_de_write(display, ch_data[i >> 2],
					       intel_dp_aux_pack(send + i,
								 send_bytes - i));

			/* Send the command and wait for it to complete */
			intel_de_write(display, ch_ctl, send_ctl);

			status = intel_dp_aux_wait_done(intel_dp);

			/* Clear done status and any errors */
			intel_de_write(display, ch_ctl,
				       status | DP_AUX_CH_CTL_DONE |
				       DP_AUX_CH_CTL_TIME_OUT_ERROR |
				       DP_AUX_CH_CTL_RECEIVE_ERROR);

			/*
			 * DP CTS 1.2 Core Rev 1.1, 4.2.1.1 & 4.2.1.2
			 *   400us delay required for errors and timeouts
			 *   Timeout errors from the HW already meet this
			 *   requirement so skip to next iteration
			 */
			if (status & DP_AUX_CH_CTL_TIME_OUT_ERROR)
				continue;

			if (status & DP_AUX_CH_CTL_RECEIVE_ERROR) {
				usleep_range(400, 500);
				continue;
			}
			if (status & DP_AUX_CH_CTL_DONE)
				goto done;
		}
	}

	if ((status & DP_AUX_CH_CTL_DONE) == 0) {
		drm_err(display->drm, "%s: not done (status 0x%08x)\n",
			intel_dp->aux.name, status);
		ret = -EBUSY;
		goto out;
	}

done:
	/*
	 * Check for timeout or receive error. Timeouts occur when the sink is
	 * not connected.
	 */
	if (status & DP_AUX_CH_CTL_RECEIVE_ERROR) {
		drm_err(display->drm, "%s: receive error (status 0x%08x)\n",
			intel_dp->aux.name, status);
		ret = -EIO;
		goto out;
	}

	/*
	 * Timeouts occur when the device isn't connected, so they're "normal"
	 * -- don't fill the kernel log with these
	 */
	if (status & DP_AUX_CH_CTL_TIME_OUT_ERROR) {
		drm_dbg_kms(display->drm, "%s: timeout (status 0x%08x)\n",
			    intel_dp->aux.name, status);
		ret = -ETIMEDOUT;
		goto out;
	}

	/* Unload any bytes sent back from the other side */
	recv_bytes = REG_FIELD_GET(DP_AUX_CH_CTL_MESSAGE_SIZE_MASK, status);

	/*
	 * By BSpec: "Message sizes of 0 or >20 are not allowed."
	 * We have no idea of what happened so we return -EBUSY so
	 * drm layer takes care for the necessary retries.
	 */
	if (recv_bytes == 0 || recv_bytes > 20) {
		drm_dbg_kms(display->drm,
			    "%s: Forbidden recv_bytes = %d on aux transaction\n",
			    intel_dp->aux.name, recv_bytes);
		ret = -EBUSY;
		goto out;
	}

	if (recv_bytes > recv_size)
		recv_bytes = recv_size;

	for (i = 0; i < recv_bytes; i += 4)
		intel_dp_aux_unpack(intel_de_read(display, ch_data[i >> 2]),
				    recv + i, recv_bytes - i);

	ret = recv_bytes;
out:
	cpu_latency_qos_update_request(&intel_dp->pm_qos, PM_QOS_DEFAULT_VALUE);

	if (vdd)
		intel_pps_vdd_off_unlocked(intel_dp, false);

	intel_pps_unlock(intel_dp, pps_wakeref);
	intel_display_power_put_async(display, aux_domain, aux_wakeref);
out_unlock:
	intel_digital_port_unlock(encoder);

	return ret;
}

#define BARE_ADDRESS_SIZE	3
#define HEADER_SIZE		(BARE_ADDRESS_SIZE + 1)

static void
intel_dp_aux_header(u8 txbuf[HEADER_SIZE],
		    const struct drm_dp_aux_msg *msg)
{
	txbuf[0] = (msg->request << 4) | ((msg->address >> 16) & 0xf);
	txbuf[1] = (msg->address >> 8) & 0xff;
	txbuf[2] = msg->address & 0xff;
	txbuf[3] = msg->size - 1;
}

static u32 intel_dp_aux_xfer_flags(const struct drm_dp_aux_msg *msg)
{
	/*
	 * If we're trying to send the HDCP Aksv, we need to set a the Aksv
	 * select bit to inform the hardware to send the Aksv after our header
	 * since we can't access that data from software.
	 */
	if ((msg->request & ~DP_AUX_I2C_MOT) == DP_AUX_NATIVE_WRITE &&
	    msg->address == DP_AUX_HDCP_AKSV)
		return DP_AUX_CH_CTL_AUX_AKSV_SELECT;

	return 0;
}

static ssize_t
intel_dp_aux_transfer(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	struct intel_dp *intel_dp = container_of(aux, struct intel_dp, aux);
	struct intel_display *display = to_intel_display(intel_dp);
	u8 txbuf[20], rxbuf[20];
	size_t txsize, rxsize;
	u32 flags = intel_dp_aux_xfer_flags(msg);
	int ret;

	intel_dp_aux_header(txbuf, msg);

	switch (msg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
	case DP_AUX_I2C_WRITE_STATUS_UPDATE:
		txsize = msg->size ? HEADER_SIZE + msg->size : BARE_ADDRESS_SIZE;
		rxsize = 2; /* 0 or 1 data bytes */

		if (drm_WARN_ON(display->drm, txsize > 20))
			return -E2BIG;

		drm_WARN_ON(display->drm, !msg->buffer != !msg->size);

		if (msg->buffer)
			memcpy(txbuf + HEADER_SIZE, msg->buffer, msg->size);

		ret = intel_dp_aux_xfer(intel_dp, txbuf, txsize,
					rxbuf, rxsize, flags);
		if (ret > 0) {
			msg->reply = rxbuf[0] >> 4;

			if (ret > 1) {
				/* Number of bytes written in a short write. */
				ret = clamp_t(int, rxbuf[1], 0, msg->size);
			} else {
				/* Return payload size. */
				ret = msg->size;
			}
		}
		break;

	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ:
		txsize = msg->size ? HEADER_SIZE : BARE_ADDRESS_SIZE;
		rxsize = msg->size + 1;

		if (drm_WARN_ON(display->drm, rxsize > 20))
			return -E2BIG;

		ret = intel_dp_aux_xfer(intel_dp, txbuf, txsize,
					rxbuf, rxsize, flags);
		if (ret > 0) {
			msg->reply = rxbuf[0] >> 4;
			/*
			 * Assume happy day, and copy the data. The caller is
			 * expected to check msg->reply before touching it.
			 *
			 * Return payload size.
			 */
			ret--;
			memcpy(msg->buffer, rxbuf + 1, ret);
		}
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static i915_reg_t vlv_aux_ctl_reg(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_D:
		return VLV_DP_AUX_CH_CTL(aux_ch);
	default:
		MISSING_CASE(aux_ch);
		return VLV_DP_AUX_CH_CTL(AUX_CH_B);
	}
}

static i915_reg_t vlv_aux_data_reg(struct intel_dp *intel_dp, int index)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_D:
		return VLV_DP_AUX_CH_DATA(aux_ch, index);
	default:
		MISSING_CASE(aux_ch);
		return VLV_DP_AUX_CH_DATA(AUX_CH_B, index);
	}
}

static i915_reg_t g4x_aux_ctl_reg(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_D:
		return DP_AUX_CH_CTL(aux_ch);
	default:
		MISSING_CASE(aux_ch);
		return DP_AUX_CH_CTL(AUX_CH_B);
	}
}

static i915_reg_t g4x_aux_data_reg(struct intel_dp *intel_dp, int index)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_D:
		return DP_AUX_CH_DATA(aux_ch, index);
	default:
		MISSING_CASE(aux_ch);
		return DP_AUX_CH_DATA(AUX_CH_B, index);
	}
}

static i915_reg_t ilk_aux_ctl_reg(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_A:
		return DP_AUX_CH_CTL(aux_ch);
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_D:
		return PCH_DP_AUX_CH_CTL(aux_ch);
	default:
		MISSING_CASE(aux_ch);
		return DP_AUX_CH_CTL(AUX_CH_A);
	}
}

static i915_reg_t ilk_aux_data_reg(struct intel_dp *intel_dp, int index)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_A:
		return DP_AUX_CH_DATA(aux_ch, index);
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_D:
		return PCH_DP_AUX_CH_DATA(aux_ch, index);
	default:
		MISSING_CASE(aux_ch);
		return DP_AUX_CH_DATA(AUX_CH_A, index);
	}
}

static i915_reg_t skl_aux_ctl_reg(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_A:
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_D:
	case AUX_CH_E:
	case AUX_CH_F:
		return DP_AUX_CH_CTL(aux_ch);
	default:
		MISSING_CASE(aux_ch);
		return DP_AUX_CH_CTL(AUX_CH_A);
	}
}

static i915_reg_t skl_aux_data_reg(struct intel_dp *intel_dp, int index)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_A:
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_D:
	case AUX_CH_E:
	case AUX_CH_F:
		return DP_AUX_CH_DATA(aux_ch, index);
	default:
		MISSING_CASE(aux_ch);
		return DP_AUX_CH_DATA(AUX_CH_A, index);
	}
}

static i915_reg_t tgl_aux_ctl_reg(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_A:
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_USBC1:
	case AUX_CH_USBC2:
	case AUX_CH_USBC3:
	case AUX_CH_USBC4:
	case AUX_CH_USBC5:  /* aka AUX_CH_D_XELPD */
	case AUX_CH_USBC6:  /* aka AUX_CH_E_XELPD */
		return DP_AUX_CH_CTL(aux_ch);
	default:
		MISSING_CASE(aux_ch);
		return DP_AUX_CH_CTL(AUX_CH_A);
	}
}

static i915_reg_t tgl_aux_data_reg(struct intel_dp *intel_dp, int index)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_A:
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_USBC1:
	case AUX_CH_USBC2:
	case AUX_CH_USBC3:
	case AUX_CH_USBC4:
	case AUX_CH_USBC5:  /* aka AUX_CH_D_XELPD */
	case AUX_CH_USBC6:  /* aka AUX_CH_E_XELPD */
		return DP_AUX_CH_DATA(aux_ch, index);
	default:
		MISSING_CASE(aux_ch);
		return DP_AUX_CH_DATA(AUX_CH_A, index);
	}
}

static i915_reg_t xelpdp_aux_ctl_reg(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_A:
	case AUX_CH_B:
	case AUX_CH_USBC1:
	case AUX_CH_USBC2:
	case AUX_CH_USBC3:
	case AUX_CH_USBC4:
		return XELPDP_DP_AUX_CH_CTL(display, aux_ch);
	default:
		MISSING_CASE(aux_ch);
		return XELPDP_DP_AUX_CH_CTL(display, AUX_CH_A);
	}
}

static i915_reg_t xelpdp_aux_data_reg(struct intel_dp *intel_dp, int index)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_A:
	case AUX_CH_B:
	case AUX_CH_USBC1:
	case AUX_CH_USBC2:
	case AUX_CH_USBC3:
	case AUX_CH_USBC4:
		return XELPDP_DP_AUX_CH_DATA(display, aux_ch, index);
	default:
		MISSING_CASE(aux_ch);
		return XELPDP_DP_AUX_CH_DATA(display, AUX_CH_A, index);
	}
}

void intel_dp_aux_fini(struct intel_dp *intel_dp)
{
	if (cpu_latency_qos_request_active(&intel_dp->pm_qos))
		cpu_latency_qos_remove_request(&intel_dp->pm_qos);

	kfree(intel_dp->aux.name);
}

void intel_dp_aux_init(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_i915_private *i915 = to_i915(display->drm);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &dig_port->base;
	enum aux_ch aux_ch = dig_port->aux_ch;
	char buf[AUX_CH_NAME_BUFSIZE];

	if (DISPLAY_VER(display) >= 14) {
		intel_dp->aux_ch_ctl_reg = xelpdp_aux_ctl_reg;
		intel_dp->aux_ch_data_reg = xelpdp_aux_data_reg;
	} else if (DISPLAY_VER(display) >= 12) {
		intel_dp->aux_ch_ctl_reg = tgl_aux_ctl_reg;
		intel_dp->aux_ch_data_reg = tgl_aux_data_reg;
	} else if (DISPLAY_VER(display) >= 9) {
		intel_dp->aux_ch_ctl_reg = skl_aux_ctl_reg;
		intel_dp->aux_ch_data_reg = skl_aux_data_reg;
	} else if (HAS_PCH_SPLIT(i915)) {
		intel_dp->aux_ch_ctl_reg = ilk_aux_ctl_reg;
		intel_dp->aux_ch_data_reg = ilk_aux_data_reg;
	} else if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915)) {
		intel_dp->aux_ch_ctl_reg = vlv_aux_ctl_reg;
		intel_dp->aux_ch_data_reg = vlv_aux_data_reg;
	} else {
		intel_dp->aux_ch_ctl_reg = g4x_aux_ctl_reg;
		intel_dp->aux_ch_data_reg = g4x_aux_data_reg;
	}

	if (DISPLAY_VER(display) >= 9)
		intel_dp->get_aux_clock_divider = skl_get_aux_clock_divider;
	else if (IS_BROADWELL(i915) || IS_HASWELL(i915))
		intel_dp->get_aux_clock_divider = hsw_get_aux_clock_divider;
	else if (HAS_PCH_SPLIT(i915))
		intel_dp->get_aux_clock_divider = ilk_get_aux_clock_divider;
	else
		intel_dp->get_aux_clock_divider = g4x_get_aux_clock_divider;

	if (DISPLAY_VER(display) >= 9)
		intel_dp->get_aux_send_ctl = skl_get_aux_send_ctl;
	else
		intel_dp->get_aux_send_ctl = g4x_get_aux_send_ctl;

	intel_dp->aux.drm_dev = display->drm;
	drm_dp_aux_init(&intel_dp->aux);

	/* Failure to allocate our preferred name is not critical */
	intel_dp->aux.name = kasprintf(GFP_KERNEL, "AUX %s/%s",
				       aux_ch_name(display, buf, sizeof(buf), aux_ch),
				       encoder->base.name);

	intel_dp->aux.transfer = intel_dp_aux_transfer;
	cpu_latency_qos_add_request(&intel_dp->pm_qos, PM_QOS_DEFAULT_VALUE);
}

static enum aux_ch default_aux_ch(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);

	/* SKL has DDI E but no AUX E */
	if (DISPLAY_VER(display) == 9 && encoder->port == PORT_E)
		return AUX_CH_A;

	return (enum aux_ch)encoder->port;
}

static struct intel_encoder *
get_encoder_by_aux_ch(struct intel_encoder *encoder,
		      enum aux_ch aux_ch)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_encoder *other;

	for_each_intel_encoder(display->drm, other) {
		if (other == encoder)
			continue;

		if (!intel_encoder_is_dig_port(other))
			continue;

		if (enc_to_dig_port(other)->aux_ch == aux_ch)
			return other;
	}

	return NULL;
}

enum aux_ch intel_dp_aux_ch(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_encoder *other;
	const char *source;
	enum aux_ch aux_ch;
	char buf[AUX_CH_NAME_BUFSIZE];

	aux_ch = intel_bios_dp_aux_ch(encoder->devdata);
	source = "VBT";

	if (aux_ch == AUX_CH_NONE) {
		aux_ch = default_aux_ch(encoder);
		source = "platform default";
	}

	if (aux_ch == AUX_CH_NONE)
		return AUX_CH_NONE;

	/* FIXME validate aux_ch against platform caps */

	other = get_encoder_by_aux_ch(encoder, aux_ch);
	if (other) {
		drm_dbg_kms(display->drm,
			    "[ENCODER:%d:%s] AUX CH %s already claimed by [ENCODER:%d:%s]\n",
			    encoder->base.base.id, encoder->base.name,
			    aux_ch_name(display, buf, sizeof(buf), aux_ch),
			    other->base.base.id, other->base.name);
		return AUX_CH_NONE;
	}

	drm_dbg_kms(display->drm,
		    "[ENCODER:%d:%s] Using AUX CH %s (%s)\n",
		    encoder->base.base.id, encoder->base.name,
		    aux_ch_name(display, buf, sizeof(buf), aux_ch), source);

	return aux_ch;
}

void intel_dp_aux_irq_handler(struct intel_display *display)
{
	wake_up_all(&display->gmbus.wait_queue);
}
