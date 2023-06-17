// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020-2021 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_display_types.h"
#include "intel_dp_aux.h"
#include "intel_pps.h"
#include "intel_tc.h"

static u32 intel_dp_aux_pack(const u8 *src, int src_bytes)
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
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	i915_reg_t ch_ctl = intel_dp->aux_ch_ctl_reg(intel_dp);
	const unsigned int timeout_ms = 10;
	u32 status;
	bool done;

#define C (((status = intel_uncore_read_notrace(&i915->uncore, ch_ctl)) & DP_AUX_CH_CTL_SEND_BUSY) == 0)
	done = wait_event_timeout(i915->display.gmbus.wait_queue, C,
				  msecs_to_jiffies_timeout(timeout_ms));

	/* just trace the final value */
	trace_i915_reg_rw(false, ch_ctl, status, sizeof(status), true);

	if (!done)
		drm_err(&i915->drm,
			"%s: did not complete or timeout within %ums (status 0x%08x)\n",
			intel_dp->aux.name, timeout_ms, status);
#undef C

	return status;
}

static u32 g4x_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	if (index)
		return 0;

	/*
	 * The clock divider is based off the hrawclk, and would like to run at
	 * 2MHz.  So, take the hrawclk value and divide by 2000 and use that
	 */
	return DIV_ROUND_CLOSEST(RUNTIME_INFO(dev_priv)->rawclk_freq, 2000);
}

static u32 ilk_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
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
		freq = dev_priv->display.cdclk.hw.cdclk;
	else
		freq = RUNTIME_INFO(dev_priv)->rawclk_freq;
	return DIV_ROUND_CLOSEST(freq, 2000);
}

static u32 hsw_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);

	if (dig_port->aux_ch != AUX_CH_A && HAS_PCH_LPT_H(dev_priv)) {
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

static u32 g4x_get_aux_send_ctl(struct intel_dp *intel_dp,
				int send_bytes,
				u32 aux_clock_divider)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv =
			to_i915(dig_port->base.base.dev);
	u32 timeout;

	/* Max timeout value on G4x-BDW: 1.6ms */
	if (IS_BROADWELL(dev_priv))
		timeout = DP_AUX_CH_CTL_TIME_OUT_600us;
	else
		timeout = DP_AUX_CH_CTL_TIME_OUT_400us;

	return DP_AUX_CH_CTL_SEND_BUSY |
	       DP_AUX_CH_CTL_DONE |
	       DP_AUX_CH_CTL_INTERRUPT |
	       DP_AUX_CH_CTL_TIME_OUT_ERROR |
	       timeout |
	       DP_AUX_CH_CTL_RECEIVE_ERROR |
	       (send_bytes << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
	       (3 << DP_AUX_CH_CTL_PRECHARGE_2US_SHIFT) |
	       (aux_clock_divider << DP_AUX_CH_CTL_BIT_CLOCK_2X_SHIFT);
}

static u32 skl_get_aux_send_ctl(struct intel_dp *intel_dp,
				int send_bytes,
				u32 unused)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *i915 =	to_i915(dig_port->base.base.dev);
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
	      (send_bytes << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
	      DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL(24) |
	      DP_AUX_CH_CTL_SYNC_PULSE_SKL(32);

	if (intel_tc_port_in_tbt_alt_mode(dig_port))
		ret |= DP_AUX_CH_CTL_TBT_IO;

	/*
	 * Power request bit is already set during aux power well enable.
	 * Preserve the bit across aux transactions.
	 */
	if (DISPLAY_VER(i915) >= 14)
		ret |= XELPDP_DP_AUX_CH_CTL_POWER_REQUEST;

	return ret;
}

static int
intel_dp_aux_xfer(struct intel_dp *intel_dp,
		  const u8 *send, int send_bytes,
		  u8 *recv, int recv_size,
		  u32 aux_send_ctl_flags)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *i915 =
			to_i915(dig_port->base.base.dev);
	struct intel_uncore *uncore = &i915->uncore;
	enum phy phy = intel_port_to_phy(i915, dig_port->base.port);
	bool is_tc_port = intel_phy_is_tc(i915, phy);
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

	if (is_tc_port)
		intel_tc_port_lock(dig_port);

	aux_domain = intel_aux_power_domain(dig_port);

	aux_wakeref = intel_display_power_get(i915, aux_domain);
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

	/* Try to wait for any previous AUX channel activity */
	for (try = 0; try < 3; try++) {
		status = intel_uncore_read_notrace(uncore, ch_ctl);
		if ((status & DP_AUX_CH_CTL_SEND_BUSY) == 0)
			break;
		msleep(1);
	}
	/* just trace the final value */
	trace_i915_reg_rw(false, ch_ctl, status, sizeof(status), true);

	if (try == 3) {
		const u32 status = intel_uncore_read(uncore, ch_ctl);

		if (status != intel_dp->aux_busy_last_status) {
			drm_WARN(&i915->drm, 1,
				 "%s: not started (status 0x%08x)\n",
				 intel_dp->aux.name, status);
			intel_dp->aux_busy_last_status = status;
		}

		ret = -EBUSY;
		goto out;
	}

	/* Only 5 data registers! */
	if (drm_WARN_ON(&i915->drm, send_bytes > 20 || recv_size > 20)) {
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
				intel_uncore_write(uncore,
						   ch_data[i >> 2],
						   intel_dp_aux_pack(send + i,
								     send_bytes - i));

			/* Send the command and wait for it to complete */
			intel_uncore_write(uncore, ch_ctl, send_ctl);

			status = intel_dp_aux_wait_done(intel_dp);

			/* Clear done status and any errors */
			intel_uncore_write(uncore,
					   ch_ctl,
					   status |
					   DP_AUX_CH_CTL_DONE |
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
		drm_err(&i915->drm, "%s: not done (status 0x%08x)\n",
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
		drm_err(&i915->drm, "%s: receive error (status 0x%08x)\n",
			intel_dp->aux.name, status);
		ret = -EIO;
		goto out;
	}

	/*
	 * Timeouts occur when the device isn't connected, so they're "normal"
	 * -- don't fill the kernel log with these
	 */
	if (status & DP_AUX_CH_CTL_TIME_OUT_ERROR) {
		drm_dbg_kms(&i915->drm, "%s: timeout (status 0x%08x)\n",
			    intel_dp->aux.name, status);
		ret = -ETIMEDOUT;
		goto out;
	}

	/* Unload any bytes sent back from the other side */
	recv_bytes = ((status & DP_AUX_CH_CTL_MESSAGE_SIZE_MASK) >>
		      DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT);

	/*
	 * By BSpec: "Message sizes of 0 or >20 are not allowed."
	 * We have no idea of what happened so we return -EBUSY so
	 * drm layer takes care for the necessary retries.
	 */
	if (recv_bytes == 0 || recv_bytes > 20) {
		drm_dbg_kms(&i915->drm,
			    "%s: Forbidden recv_bytes = %d on aux transaction\n",
			    intel_dp->aux.name, recv_bytes);
		ret = -EBUSY;
		goto out;
	}

	if (recv_bytes > recv_size)
		recv_bytes = recv_size;

	for (i = 0; i < recv_bytes; i += 4)
		intel_dp_aux_unpack(intel_uncore_read(uncore, ch_data[i >> 2]),
				    recv + i, recv_bytes - i);

	ret = recv_bytes;
out:
	cpu_latency_qos_update_request(&intel_dp->pm_qos, PM_QOS_DEFAULT_VALUE);

	if (vdd)
		intel_pps_vdd_off_unlocked(intel_dp, false);

	intel_pps_unlock(intel_dp, pps_wakeref);
	intel_display_power_put_async(i915, aux_domain, aux_wakeref);

	if (is_tc_port)
		intel_tc_port_unlock(dig_port);

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
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
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

		if (drm_WARN_ON(&i915->drm, txsize > 20))
			return -E2BIG;

		drm_WARN_ON(&i915->drm, !msg->buffer != !msg->size);

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

		if (drm_WARN_ON(&i915->drm, rxsize > 20))
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

static i915_reg_t g4x_aux_ctl_reg(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_A:
	case AUX_CH_B:
	case AUX_CH_USBC1:
	case AUX_CH_USBC2:
	case AUX_CH_USBC3:
	case AUX_CH_USBC4:
		return XELPDP_DP_AUX_CH_CTL(aux_ch);
	default:
		MISSING_CASE(aux_ch);
		return XELPDP_DP_AUX_CH_CTL(AUX_CH_A);
	}
}

static i915_reg_t xelpdp_aux_data_reg(struct intel_dp *intel_dp, int index)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_A:
	case AUX_CH_B:
	case AUX_CH_USBC1:
	case AUX_CH_USBC2:
	case AUX_CH_USBC3:
	case AUX_CH_USBC4:
		return XELPDP_DP_AUX_CH_DATA(aux_ch, index);
	default:
		MISSING_CASE(aux_ch);
		return XELPDP_DP_AUX_CH_DATA(AUX_CH_A, index);
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
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &dig_port->base;
	enum aux_ch aux_ch = dig_port->aux_ch;

	if (DISPLAY_VER(dev_priv) >= 14) {
		intel_dp->aux_ch_ctl_reg = xelpdp_aux_ctl_reg;
		intel_dp->aux_ch_data_reg = xelpdp_aux_data_reg;
	} else if (DISPLAY_VER(dev_priv) >= 12) {
		intel_dp->aux_ch_ctl_reg = tgl_aux_ctl_reg;
		intel_dp->aux_ch_data_reg = tgl_aux_data_reg;
	} else if (DISPLAY_VER(dev_priv) >= 9) {
		intel_dp->aux_ch_ctl_reg = skl_aux_ctl_reg;
		intel_dp->aux_ch_data_reg = skl_aux_data_reg;
	} else if (HAS_PCH_SPLIT(dev_priv)) {
		intel_dp->aux_ch_ctl_reg = ilk_aux_ctl_reg;
		intel_dp->aux_ch_data_reg = ilk_aux_data_reg;
	} else {
		intel_dp->aux_ch_ctl_reg = g4x_aux_ctl_reg;
		intel_dp->aux_ch_data_reg = g4x_aux_data_reg;
	}

	if (DISPLAY_VER(dev_priv) >= 9)
		intel_dp->get_aux_clock_divider = skl_get_aux_clock_divider;
	else if (IS_BROADWELL(dev_priv) || IS_HASWELL(dev_priv))
		intel_dp->get_aux_clock_divider = hsw_get_aux_clock_divider;
	else if (HAS_PCH_SPLIT(dev_priv))
		intel_dp->get_aux_clock_divider = ilk_get_aux_clock_divider;
	else
		intel_dp->get_aux_clock_divider = g4x_get_aux_clock_divider;

	if (DISPLAY_VER(dev_priv) >= 9)
		intel_dp->get_aux_send_ctl = skl_get_aux_send_ctl;
	else
		intel_dp->get_aux_send_ctl = g4x_get_aux_send_ctl;

	intel_dp->aux.drm_dev = &dev_priv->drm;
	drm_dp_aux_init(&intel_dp->aux);

	/* Failure to allocate our preferred name is not critical */
	if (DISPLAY_VER(dev_priv) >= 13 && aux_ch >= AUX_CH_D_XELPD)
		intel_dp->aux.name = kasprintf(GFP_KERNEL, "AUX %c/%s",
					       aux_ch_name(aux_ch - AUX_CH_D_XELPD + AUX_CH_D),
					       encoder->base.name);
	else if (DISPLAY_VER(dev_priv) >= 12 && aux_ch >= AUX_CH_USBC1)
		intel_dp->aux.name = kasprintf(GFP_KERNEL, "AUX USBC%c/%s",
					       aux_ch - AUX_CH_USBC1 + '1',
					       encoder->base.name);
	else
		intel_dp->aux.name = kasprintf(GFP_KERNEL, "AUX %c/%s",
					       aux_ch_name(aux_ch),
					       encoder->base.name);

	intel_dp->aux.transfer = intel_dp_aux_transfer;
	cpu_latency_qos_add_request(&intel_dp->pm_qos, PM_QOS_DEFAULT_VALUE);
}
