/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2017 Google, Inc.
 *
 * Authors:
 * Sean Paul <seanpaul@chromium.org>
 */

#include <linux/component.h>
#include <linux/i2c.h>
#include <linux/random.h>

#include <drm/drm_hdcp.h>
#include <drm/i915_component.h>

#include "i915_reg.h"
#include "intel_drv.h"
#include "intel_hdcp.h"
#include "intel_sideband.h"

#define KEY_LOAD_TRIES	5
#define ENCRYPT_STATUS_CHANGE_TIMEOUT_MS	50
#define HDCP2_LC_RETRY_CNT			3

static
bool intel_hdcp_is_ksv_valid(u8 *ksv)
{
	int i, ones = 0;
	/* KSV has 20 1's and 20 0's */
	for (i = 0; i < DRM_HDCP_KSV_LEN; i++)
		ones += hweight8(ksv[i]);
	if (ones != 20)
		return false;

	return true;
}

static
int intel_hdcp_read_valid_bksv(struct intel_digital_port *intel_dig_port,
			       const struct intel_hdcp_shim *shim, u8 *bksv)
{
	int ret, i, tries = 2;

	/* HDCP spec states that we must retry the bksv if it is invalid */
	for (i = 0; i < tries; i++) {
		ret = shim->read_bksv(intel_dig_port, bksv);
		if (ret)
			return ret;
		if (intel_hdcp_is_ksv_valid(bksv))
			break;
	}
	if (i == tries) {
		DRM_DEBUG_KMS("Bksv is invalid\n");
		return -ENODEV;
	}

	return 0;
}

/* Is HDCP1.4 capable on Platform and Sink */
bool intel_hdcp_capable(struct intel_connector *connector)
{
	struct intel_digital_port *intel_dig_port = conn_to_dig_port(connector);
	const struct intel_hdcp_shim *shim = connector->hdcp.shim;
	bool capable = false;
	u8 bksv[5];

	if (!shim)
		return capable;

	if (shim->hdcp_capable) {
		shim->hdcp_capable(intel_dig_port, &capable);
	} else {
		if (!intel_hdcp_read_valid_bksv(intel_dig_port, shim, bksv))
			capable = true;
	}

	return capable;
}

/* Is HDCP2.2 capable on Platform and Sink */
static bool intel_hdcp2_capable(struct intel_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_digital_port *intel_dig_port = conn_to_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	bool capable = false;

	/* I915 support for HDCP2.2 */
	if (!hdcp->hdcp2_supported)
		return false;

	/* MEI interface is solid */
	mutex_lock(&dev_priv->hdcp_comp_mutex);
	if (!dev_priv->hdcp_comp_added ||  !dev_priv->hdcp_master) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return false;
	}
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	/* Sink's capability for HDCP2.2 */
	hdcp->shim->hdcp_2_2_capable(intel_dig_port, &capable);

	return capable;
}

static inline bool intel_hdcp_in_use(struct intel_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	enum port port = connector->encoder->port;
	u32 reg;

	reg = I915_READ(PORT_HDCP_STATUS(port));
	return reg & HDCP_STATUS_ENC;
}

static inline bool intel_hdcp2_in_use(struct intel_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	enum port port = connector->encoder->port;
	u32 reg;

	reg = I915_READ(HDCP2_STATUS_DDI(port));
	return reg & LINK_ENCRYPTION_STATUS;
}

static int intel_hdcp_poll_ksv_fifo(struct intel_digital_port *intel_dig_port,
				    const struct intel_hdcp_shim *shim)
{
	int ret, read_ret;
	bool ksv_ready;

	/* Poll for ksv list ready (spec says max time allowed is 5s) */
	ret = __wait_for(read_ret = shim->read_ksv_ready(intel_dig_port,
							 &ksv_ready),
			 read_ret || ksv_ready, 5 * 1000 * 1000, 1000,
			 100 * 1000);
	if (ret)
		return ret;
	if (read_ret)
		return read_ret;
	if (!ksv_ready)
		return -ETIMEDOUT;

	return 0;
}

static bool hdcp_key_loadable(struct drm_i915_private *dev_priv)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *power_well;
	enum i915_power_well_id id;
	bool enabled = false;

	/*
	 * On HSW and BDW, Display HW loads the Key as soon as Display resumes.
	 * On all BXT+, SW can load the keys only when the PW#1 is turned on.
	 */
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		id = HSW_DISP_PW_GLOBAL;
	else
		id = SKL_DISP_PW_1;

	mutex_lock(&power_domains->lock);

	/* PG1 (power well #1) needs to be enabled */
	for_each_power_well(dev_priv, power_well) {
		if (power_well->desc->id == id) {
			enabled = power_well->desc->ops->is_enabled(dev_priv,
								    power_well);
			break;
		}
	}
	mutex_unlock(&power_domains->lock);

	/*
	 * Another req for hdcp key loadability is enabled state of pll for
	 * cdclk. Without active crtc we wont land here. So we are assuming that
	 * cdclk is already on.
	 */

	return enabled;
}

static void intel_hdcp_clear_keys(struct drm_i915_private *dev_priv)
{
	I915_WRITE(HDCP_KEY_CONF, HDCP_CLEAR_KEYS_TRIGGER);
	I915_WRITE(HDCP_KEY_STATUS, HDCP_KEY_LOAD_DONE | HDCP_KEY_LOAD_STATUS |
		   HDCP_FUSE_IN_PROGRESS | HDCP_FUSE_ERROR | HDCP_FUSE_DONE);
}

static int intel_hdcp_load_keys(struct drm_i915_private *dev_priv)
{
	int ret;
	u32 val;

	val = I915_READ(HDCP_KEY_STATUS);
	if ((val & HDCP_KEY_LOAD_DONE) && (val & HDCP_KEY_LOAD_STATUS))
		return 0;

	/*
	 * On HSW and BDW HW loads the HDCP1.4 Key when Display comes
	 * out of reset. So if Key is not already loaded, its an error state.
	 */
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		if (!(I915_READ(HDCP_KEY_STATUS) & HDCP_KEY_LOAD_DONE))
			return -ENXIO;

	/*
	 * Initiate loading the HDCP key from fuses.
	 *
	 * BXT+ platforms, HDCP key needs to be loaded by SW. Only Gen 9
	 * platforms except BXT and GLK, differ in the key load trigger process
	 * from other platforms. So GEN9_BC uses the GT Driver Mailbox i/f.
	 */
	if (IS_GEN9_BC(dev_priv)) {
		ret = sandybridge_pcode_write(dev_priv,
					      SKL_PCODE_LOAD_HDCP_KEYS, 1);
		if (ret) {
			DRM_ERROR("Failed to initiate HDCP key load (%d)\n",
			          ret);
			return ret;
		}
	} else {
		I915_WRITE(HDCP_KEY_CONF, HDCP_KEY_LOAD_TRIGGER);
	}

	/* Wait for the keys to load (500us) */
	ret = __intel_wait_for_register(&dev_priv->uncore, HDCP_KEY_STATUS,
					HDCP_KEY_LOAD_DONE, HDCP_KEY_LOAD_DONE,
					10, 1, &val);
	if (ret)
		return ret;
	else if (!(val & HDCP_KEY_LOAD_STATUS))
		return -ENXIO;

	/* Send Aksv over to PCH display for use in authentication */
	I915_WRITE(HDCP_KEY_CONF, HDCP_AKSV_SEND_TRIGGER);

	return 0;
}

/* Returns updated SHA-1 index */
static int intel_write_sha_text(struct drm_i915_private *dev_priv, u32 sha_text)
{
	I915_WRITE(HDCP_SHA_TEXT, sha_text);
	if (intel_wait_for_register(&dev_priv->uncore, HDCP_REP_CTL,
				    HDCP_SHA1_READY, HDCP_SHA1_READY, 1)) {
		DRM_ERROR("Timed out waiting for SHA1 ready\n");
		return -ETIMEDOUT;
	}
	return 0;
}

static
u32 intel_hdcp_get_repeater_ctl(struct intel_digital_port *intel_dig_port)
{
	enum port port = intel_dig_port->base.port;
	switch (port) {
	case PORT_A:
		return HDCP_DDIA_REP_PRESENT | HDCP_DDIA_SHA1_M0;
	case PORT_B:
		return HDCP_DDIB_REP_PRESENT | HDCP_DDIB_SHA1_M0;
	case PORT_C:
		return HDCP_DDIC_REP_PRESENT | HDCP_DDIC_SHA1_M0;
	case PORT_D:
		return HDCP_DDID_REP_PRESENT | HDCP_DDID_SHA1_M0;
	case PORT_E:
		return HDCP_DDIE_REP_PRESENT | HDCP_DDIE_SHA1_M0;
	default:
		break;
	}
	DRM_ERROR("Unknown port %d\n", port);
	return -EINVAL;
}

static
int intel_hdcp_validate_v_prime(struct intel_digital_port *intel_dig_port,
				const struct intel_hdcp_shim *shim,
				u8 *ksv_fifo, u8 num_downstream, u8 *bstatus)
{
	struct drm_i915_private *dev_priv;
	u32 vprime, sha_text, sha_leftovers, rep_ctl;
	int ret, i, j, sha_idx;

	dev_priv = intel_dig_port->base.base.dev->dev_private;

	/* Process V' values from the receiver */
	for (i = 0; i < DRM_HDCP_V_PRIME_NUM_PARTS; i++) {
		ret = shim->read_v_prime_part(intel_dig_port, i, &vprime);
		if (ret)
			return ret;
		I915_WRITE(HDCP_SHA_V_PRIME(i), vprime);
	}

	/*
	 * We need to write the concatenation of all device KSVs, BINFO (DP) ||
	 * BSTATUS (HDMI), and M0 (which is added via HDCP_REP_CTL). This byte
	 * stream is written via the HDCP_SHA_TEXT register in 32-bit
	 * increments. Every 64 bytes, we need to write HDCP_REP_CTL again. This
	 * index will keep track of our progress through the 64 bytes as well as
	 * helping us work the 40-bit KSVs through our 32-bit register.
	 *
	 * NOTE: data passed via HDCP_SHA_TEXT should be big-endian
	 */
	sha_idx = 0;
	sha_text = 0;
	sha_leftovers = 0;
	rep_ctl = intel_hdcp_get_repeater_ctl(intel_dig_port);
	I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_32);
	for (i = 0; i < num_downstream; i++) {
		unsigned int sha_empty;
		u8 *ksv = &ksv_fifo[i * DRM_HDCP_KSV_LEN];

		/* Fill up the empty slots in sha_text and write it out */
		sha_empty = sizeof(sha_text) - sha_leftovers;
		for (j = 0; j < sha_empty; j++)
			sha_text |= ksv[j] << ((sizeof(sha_text) - j - 1) * 8);

		ret = intel_write_sha_text(dev_priv, sha_text);
		if (ret < 0)
			return ret;

		/* Programming guide writes this every 64 bytes */
		sha_idx += sizeof(sha_text);
		if (!(sha_idx % 64))
			I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_32);

		/* Store the leftover bytes from the ksv in sha_text */
		sha_leftovers = DRM_HDCP_KSV_LEN - sha_empty;
		sha_text = 0;
		for (j = 0; j < sha_leftovers; j++)
			sha_text |= ksv[sha_empty + j] <<
					((sizeof(sha_text) - j - 1) * 8);

		/*
		 * If we still have room in sha_text for more data, continue.
		 * Otherwise, write it out immediately.
		 */
		if (sizeof(sha_text) > sha_leftovers)
			continue;

		ret = intel_write_sha_text(dev_priv, sha_text);
		if (ret < 0)
			return ret;
		sha_leftovers = 0;
		sha_text = 0;
		sha_idx += sizeof(sha_text);
	}

	/*
	 * We need to write BINFO/BSTATUS, and M0 now. Depending on how many
	 * bytes are leftover from the last ksv, we might be able to fit them
	 * all in sha_text (first 2 cases), or we might need to split them up
	 * into 2 writes (last 2 cases).
	 */
	if (sha_leftovers == 0) {
		/* Write 16 bits of text, 16 bits of M0 */
		I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_16);
		ret = intel_write_sha_text(dev_priv,
					   bstatus[0] << 8 | bstatus[1]);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 32 bits of M0 */
		I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_0);
		ret = intel_write_sha_text(dev_priv, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 16 bits of M0 */
		I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_16);
		ret = intel_write_sha_text(dev_priv, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

	} else if (sha_leftovers == 1) {
		/* Write 24 bits of text, 8 bits of M0 */
		I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_24);
		sha_text |= bstatus[0] << 16 | bstatus[1] << 8;
		/* Only 24-bits of data, must be in the LSB */
		sha_text = (sha_text & 0xffffff00) >> 8;
		ret = intel_write_sha_text(dev_priv, sha_text);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 32 bits of M0 */
		I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_0);
		ret = intel_write_sha_text(dev_priv, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 24 bits of M0 */
		I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_8);
		ret = intel_write_sha_text(dev_priv, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

	} else if (sha_leftovers == 2) {
		/* Write 32 bits of text */
		I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_32);
		sha_text |= bstatus[0] << 24 | bstatus[1] << 16;
		ret = intel_write_sha_text(dev_priv, sha_text);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 64 bits of M0 */
		I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_0);
		for (i = 0; i < 2; i++) {
			ret = intel_write_sha_text(dev_priv, 0);
			if (ret < 0)
				return ret;
			sha_idx += sizeof(sha_text);
		}
	} else if (sha_leftovers == 3) {
		/* Write 32 bits of text */
		I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_32);
		sha_text |= bstatus[0] << 24;
		ret = intel_write_sha_text(dev_priv, sha_text);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 8 bits of text, 24 bits of M0 */
		I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_8);
		ret = intel_write_sha_text(dev_priv, bstatus[1]);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 32 bits of M0 */
		I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_0);
		ret = intel_write_sha_text(dev_priv, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 8 bits of M0 */
		I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_24);
		ret = intel_write_sha_text(dev_priv, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);
	} else {
		DRM_DEBUG_KMS("Invalid number of leftovers %d\n",
			      sha_leftovers);
		return -EINVAL;
	}

	I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_32);
	/* Fill up to 64-4 bytes with zeros (leave the last write for length) */
	while ((sha_idx % 64) < (64 - sizeof(sha_text))) {
		ret = intel_write_sha_text(dev_priv, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);
	}

	/*
	 * Last write gets the length of the concatenation in bits. That is:
	 *  - 5 bytes per device
	 *  - 10 bytes for BINFO/BSTATUS(2), M0(8)
	 */
	sha_text = (num_downstream * 5 + 10) * 8;
	ret = intel_write_sha_text(dev_priv, sha_text);
	if (ret < 0)
		return ret;

	/* Tell the HW we're done with the hash and wait for it to ACK */
	I915_WRITE(HDCP_REP_CTL, rep_ctl | HDCP_SHA1_COMPLETE_HASH);
	if (intel_wait_for_register(&dev_priv->uncore, HDCP_REP_CTL,
				    HDCP_SHA1_COMPLETE,
				    HDCP_SHA1_COMPLETE, 1)) {
		DRM_ERROR("Timed out waiting for SHA1 complete\n");
		return -ETIMEDOUT;
	}
	if (!(I915_READ(HDCP_REP_CTL) & HDCP_SHA1_V_MATCH)) {
		DRM_DEBUG_KMS("SHA-1 mismatch, HDCP failed\n");
		return -ENXIO;
	}

	return 0;
}

/* Implements Part 2 of the HDCP authorization procedure */
static
int intel_hdcp_auth_downstream(struct intel_digital_port *intel_dig_port,
			       const struct intel_hdcp_shim *shim)
{
	u8 bstatus[2], num_downstream, *ksv_fifo;
	int ret, i, tries = 3;

	ret = intel_hdcp_poll_ksv_fifo(intel_dig_port, shim);
	if (ret) {
		DRM_DEBUG_KMS("KSV list failed to become ready (%d)\n", ret);
		return ret;
	}

	ret = shim->read_bstatus(intel_dig_port, bstatus);
	if (ret)
		return ret;

	if (DRM_HDCP_MAX_DEVICE_EXCEEDED(bstatus[0]) ||
	    DRM_HDCP_MAX_CASCADE_EXCEEDED(bstatus[1])) {
		DRM_DEBUG_KMS("Max Topology Limit Exceeded\n");
		return -EPERM;
	}

	/*
	 * When repeater reports 0 device count, HDCP1.4 spec allows disabling
	 * the HDCP encryption. That implies that repeater can't have its own
	 * display. As there is no consumption of encrypted content in the
	 * repeater with 0 downstream devices, we are failing the
	 * authentication.
	 */
	num_downstream = DRM_HDCP_NUM_DOWNSTREAM(bstatus[0]);
	if (num_downstream == 0)
		return -EINVAL;

	ksv_fifo = kcalloc(DRM_HDCP_KSV_LEN, num_downstream, GFP_KERNEL);
	if (!ksv_fifo)
		return -ENOMEM;

	ret = shim->read_ksv_fifo(intel_dig_port, num_downstream, ksv_fifo);
	if (ret)
		goto err;

	/*
	 * When V prime mismatches, DP Spec mandates re-read of
	 * V prime atleast twice.
	 */
	for (i = 0; i < tries; i++) {
		ret = intel_hdcp_validate_v_prime(intel_dig_port, shim,
						  ksv_fifo, num_downstream,
						  bstatus);
		if (!ret)
			break;
	}

	if (i == tries) {
		DRM_DEBUG_KMS("V Prime validation failed.(%d)\n", ret);
		goto err;
	}

	DRM_DEBUG_KMS("HDCP is enabled (%d downstream devices)\n",
		      num_downstream);
	ret = 0;
err:
	kfree(ksv_fifo);
	return ret;
}

/* Implements Part 1 of the HDCP authorization procedure */
static int intel_hdcp_auth(struct intel_digital_port *intel_dig_port,
			   const struct intel_hdcp_shim *shim)
{
	struct drm_i915_private *dev_priv;
	enum port port;
	unsigned long r0_prime_gen_start;
	int ret, i, tries = 2;
	union {
		u32 reg[2];
		u8 shim[DRM_HDCP_AN_LEN];
	} an;
	union {
		u32 reg[2];
		u8 shim[DRM_HDCP_KSV_LEN];
	} bksv;
	union {
		u32 reg;
		u8 shim[DRM_HDCP_RI_LEN];
	} ri;
	bool repeater_present, hdcp_capable;

	dev_priv = intel_dig_port->base.base.dev->dev_private;

	port = intel_dig_port->base.port;

	/*
	 * Detects whether the display is HDCP capable. Although we check for
	 * valid Bksv below, the HDCP over DP spec requires that we check
	 * whether the display supports HDCP before we write An. For HDMI
	 * displays, this is not necessary.
	 */
	if (shim->hdcp_capable) {
		ret = shim->hdcp_capable(intel_dig_port, &hdcp_capable);
		if (ret)
			return ret;
		if (!hdcp_capable) {
			DRM_DEBUG_KMS("Panel is not HDCP capable\n");
			return -EINVAL;
		}
	}

	/* Initialize An with 2 random values and acquire it */
	for (i = 0; i < 2; i++)
		I915_WRITE(PORT_HDCP_ANINIT(port), get_random_u32());
	I915_WRITE(PORT_HDCP_CONF(port), HDCP_CONF_CAPTURE_AN);

	/* Wait for An to be acquired */
	if (intel_wait_for_register(&dev_priv->uncore, PORT_HDCP_STATUS(port),
				    HDCP_STATUS_AN_READY,
				    HDCP_STATUS_AN_READY, 1)) {
		DRM_ERROR("Timed out waiting for An\n");
		return -ETIMEDOUT;
	}

	an.reg[0] = I915_READ(PORT_HDCP_ANLO(port));
	an.reg[1] = I915_READ(PORT_HDCP_ANHI(port));
	ret = shim->write_an_aksv(intel_dig_port, an.shim);
	if (ret)
		return ret;

	r0_prime_gen_start = jiffies;

	memset(&bksv, 0, sizeof(bksv));

	ret = intel_hdcp_read_valid_bksv(intel_dig_port, shim, bksv.shim);
	if (ret < 0)
		return ret;

	I915_WRITE(PORT_HDCP_BKSVLO(port), bksv.reg[0]);
	I915_WRITE(PORT_HDCP_BKSVHI(port), bksv.reg[1]);

	ret = shim->repeater_present(intel_dig_port, &repeater_present);
	if (ret)
		return ret;
	if (repeater_present)
		I915_WRITE(HDCP_REP_CTL,
			   intel_hdcp_get_repeater_ctl(intel_dig_port));

	ret = shim->toggle_signalling(intel_dig_port, true);
	if (ret)
		return ret;

	I915_WRITE(PORT_HDCP_CONF(port), HDCP_CONF_AUTH_AND_ENC);

	/* Wait for R0 ready */
	if (wait_for(I915_READ(PORT_HDCP_STATUS(port)) &
		     (HDCP_STATUS_R0_READY | HDCP_STATUS_ENC), 1)) {
		DRM_ERROR("Timed out waiting for R0 ready\n");
		return -ETIMEDOUT;
	}

	/*
	 * Wait for R0' to become available. The spec says 100ms from Aksv, but
	 * some monitors can take longer than this. We'll set the timeout at
	 * 300ms just to be sure.
	 *
	 * On DP, there's an R0_READY bit available but no such bit
	 * exists on HDMI. Since the upper-bound is the same, we'll just do
	 * the stupid thing instead of polling on one and not the other.
	 */
	wait_remaining_ms_from_jiffies(r0_prime_gen_start, 300);

	tries = 3;

	/*
	 * DP HDCP Spec mandates the two more reattempt to read R0, incase
	 * of R0 mismatch.
	 */
	for (i = 0; i < tries; i++) {
		ri.reg = 0;
		ret = shim->read_ri_prime(intel_dig_port, ri.shim);
		if (ret)
			return ret;
		I915_WRITE(PORT_HDCP_RPRIME(port), ri.reg);

		/* Wait for Ri prime match */
		if (!wait_for(I915_READ(PORT_HDCP_STATUS(port)) &
		    (HDCP_STATUS_RI_MATCH | HDCP_STATUS_ENC), 1))
			break;
	}

	if (i == tries) {
		DRM_DEBUG_KMS("Timed out waiting for Ri prime match (%x)\n",
			      I915_READ(PORT_HDCP_STATUS(port)));
		return -ETIMEDOUT;
	}

	/* Wait for encryption confirmation */
	if (intel_wait_for_register(&dev_priv->uncore, PORT_HDCP_STATUS(port),
				    HDCP_STATUS_ENC, HDCP_STATUS_ENC,
				    ENCRYPT_STATUS_CHANGE_TIMEOUT_MS)) {
		DRM_ERROR("Timed out waiting for encryption\n");
		return -ETIMEDOUT;
	}

	/*
	 * XXX: If we have MST-connected devices, we need to enable encryption
	 * on those as well.
	 */

	if (repeater_present)
		return intel_hdcp_auth_downstream(intel_dig_port, shim);

	DRM_DEBUG_KMS("HDCP is enabled (no repeater present)\n");
	return 0;
}

static int _intel_hdcp_disable(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;
	struct drm_i915_private *dev_priv = connector->base.dev->dev_private;
	struct intel_digital_port *intel_dig_port = conn_to_dig_port(connector);
	enum port port = intel_dig_port->base.port;
	int ret;

	DRM_DEBUG_KMS("[%s:%d] HDCP is being disabled...\n",
		      connector->base.name, connector->base.base.id);

	hdcp->hdcp_encrypted = false;
	I915_WRITE(PORT_HDCP_CONF(port), 0);
	if (intel_wait_for_register(&dev_priv->uncore,
				    PORT_HDCP_STATUS(port), ~0, 0,
				    ENCRYPT_STATUS_CHANGE_TIMEOUT_MS)) {
		DRM_ERROR("Failed to disable HDCP, timeout clearing status\n");
		return -ETIMEDOUT;
	}

	ret = hdcp->shim->toggle_signalling(intel_dig_port, false);
	if (ret) {
		DRM_ERROR("Failed to disable HDCP signalling\n");
		return ret;
	}

	DRM_DEBUG_KMS("HDCP is disabled\n");
	return 0;
}

static int _intel_hdcp_enable(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;
	struct drm_i915_private *dev_priv = connector->base.dev->dev_private;
	int i, ret, tries = 3;

	DRM_DEBUG_KMS("[%s:%d] HDCP is being enabled...\n",
		      connector->base.name, connector->base.base.id);

	if (!hdcp_key_loadable(dev_priv)) {
		DRM_ERROR("HDCP key Load is not possible\n");
		return -ENXIO;
	}

	for (i = 0; i < KEY_LOAD_TRIES; i++) {
		ret = intel_hdcp_load_keys(dev_priv);
		if (!ret)
			break;
		intel_hdcp_clear_keys(dev_priv);
	}
	if (ret) {
		DRM_ERROR("Could not load HDCP keys, (%d)\n", ret);
		return ret;
	}

	/* Incase of authentication failures, HDCP spec expects reauth. */
	for (i = 0; i < tries; i++) {
		ret = intel_hdcp_auth(conn_to_dig_port(connector), hdcp->shim);
		if (!ret) {
			hdcp->hdcp_encrypted = true;
			return 0;
		}

		DRM_DEBUG_KMS("HDCP Auth failure (%d)\n", ret);

		/* Ensuring HDCP encryption and signalling are stopped. */
		_intel_hdcp_disable(connector);
	}

	DRM_DEBUG_KMS("HDCP authentication failed (%d tries/%d)\n", tries, ret);
	return ret;
}

static inline
struct intel_connector *intel_hdcp_to_connector(struct intel_hdcp *hdcp)
{
	return container_of(hdcp, struct intel_connector, hdcp);
}

/* Implements Part 3 of the HDCP authorization procedure */
static int intel_hdcp_check_link(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;
	struct drm_i915_private *dev_priv = connector->base.dev->dev_private;
	struct intel_digital_port *intel_dig_port = conn_to_dig_port(connector);
	enum port port = intel_dig_port->base.port;
	int ret = 0;

	mutex_lock(&hdcp->mutex);

	/* Check_link valid only when HDCP1.4 is enabled */
	if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_ENABLED ||
	    !hdcp->hdcp_encrypted) {
		ret = -EINVAL;
		goto out;
	}

	if (WARN_ON(!intel_hdcp_in_use(connector))) {
		DRM_ERROR("%s:%d HDCP link stopped encryption,%x\n",
			  connector->base.name, connector->base.base.id,
			  I915_READ(PORT_HDCP_STATUS(port)));
		ret = -ENXIO;
		hdcp->value = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		schedule_work(&hdcp->prop_work);
		goto out;
	}

	if (hdcp->shim->check_link(intel_dig_port)) {
		if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
			hdcp->value = DRM_MODE_CONTENT_PROTECTION_ENABLED;
			schedule_work(&hdcp->prop_work);
		}
		goto out;
	}

	DRM_DEBUG_KMS("[%s:%d] HDCP link failed, retrying authentication\n",
		      connector->base.name, connector->base.base.id);

	ret = _intel_hdcp_disable(connector);
	if (ret) {
		DRM_ERROR("Failed to disable hdcp (%d)\n", ret);
		hdcp->value = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		schedule_work(&hdcp->prop_work);
		goto out;
	}

	ret = _intel_hdcp_enable(connector);
	if (ret) {
		DRM_ERROR("Failed to enable hdcp (%d)\n", ret);
		hdcp->value = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		schedule_work(&hdcp->prop_work);
		goto out;
	}

out:
	mutex_unlock(&hdcp->mutex);
	return ret;
}

static void intel_hdcp_prop_work(struct work_struct *work)
{
	struct intel_hdcp *hdcp = container_of(work, struct intel_hdcp,
					       prop_work);
	struct intel_connector *connector = intel_hdcp_to_connector(hdcp);
	struct drm_device *dev = connector->base.dev;
	struct drm_connector_state *state;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	mutex_lock(&hdcp->mutex);

	/*
	 * This worker is only used to flip between ENABLED/DESIRED. Either of
	 * those to UNDESIRED is handled by core. If value == UNDESIRED,
	 * we're running just after hdcp has been disabled, so just exit
	 */
	if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
		state = connector->base.state;
		state->content_protection = hdcp->value;
	}

	mutex_unlock(&hdcp->mutex);
	drm_modeset_unlock(&dev->mode_config.connection_mutex);
}

bool is_hdcp_supported(struct drm_i915_private *dev_priv, enum port port)
{
	/* PORT E doesn't have HDCP, and PORT F is disabled */
	return INTEL_GEN(dev_priv) >= 9 && port < PORT_E;
}

static int
hdcp2_prepare_ake_init(struct intel_connector *connector,
		       struct hdcp2_ake_init *ake_data)
{
	struct hdcp_port_data *data = &connector->hdcp.port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->initiate_hdcp2_session(comp->mei_dev, data, ake_data);
	if (ret)
		DRM_DEBUG_KMS("Prepare_ake_init failed. %d\n", ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int
hdcp2_verify_rx_cert_prepare_km(struct intel_connector *connector,
				struct hdcp2_ake_send_cert *rx_cert,
				bool *paired,
				struct hdcp2_ake_no_stored_km *ek_pub_km,
				size_t *msg_sz)
{
	struct hdcp_port_data *data = &connector->hdcp.port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->verify_receiver_cert_prepare_km(comp->mei_dev, data,
							 rx_cert, paired,
							 ek_pub_km, msg_sz);
	if (ret < 0)
		DRM_DEBUG_KMS("Verify rx_cert failed. %d\n", ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int hdcp2_verify_hprime(struct intel_connector *connector,
			       struct hdcp2_ake_send_hprime *rx_hprime)
{
	struct hdcp_port_data *data = &connector->hdcp.port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->verify_hprime(comp->mei_dev, data, rx_hprime);
	if (ret < 0)
		DRM_DEBUG_KMS("Verify hprime failed. %d\n", ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int
hdcp2_store_pairing_info(struct intel_connector *connector,
			 struct hdcp2_ake_send_pairing_info *pairing_info)
{
	struct hdcp_port_data *data = &connector->hdcp.port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->store_pairing_info(comp->mei_dev, data, pairing_info);
	if (ret < 0)
		DRM_DEBUG_KMS("Store pairing info failed. %d\n", ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int
hdcp2_prepare_lc_init(struct intel_connector *connector,
		      struct hdcp2_lc_init *lc_init)
{
	struct hdcp_port_data *data = &connector->hdcp.port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->initiate_locality_check(comp->mei_dev, data, lc_init);
	if (ret < 0)
		DRM_DEBUG_KMS("Prepare lc_init failed. %d\n", ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int
hdcp2_verify_lprime(struct intel_connector *connector,
		    struct hdcp2_lc_send_lprime *rx_lprime)
{
	struct hdcp_port_data *data = &connector->hdcp.port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->verify_lprime(comp->mei_dev, data, rx_lprime);
	if (ret < 0)
		DRM_DEBUG_KMS("Verify L_Prime failed. %d\n", ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int hdcp2_prepare_skey(struct intel_connector *connector,
			      struct hdcp2_ske_send_eks *ske_data)
{
	struct hdcp_port_data *data = &connector->hdcp.port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->get_session_key(comp->mei_dev, data, ske_data);
	if (ret < 0)
		DRM_DEBUG_KMS("Get session key failed. %d\n", ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int
hdcp2_verify_rep_topology_prepare_ack(struct intel_connector *connector,
				      struct hdcp2_rep_send_receiverid_list
								*rep_topology,
				      struct hdcp2_rep_send_ack *rep_send_ack)
{
	struct hdcp_port_data *data = &connector->hdcp.port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->repeater_check_flow_prepare_ack(comp->mei_dev, data,
							 rep_topology,
							 rep_send_ack);
	if (ret < 0)
		DRM_DEBUG_KMS("Verify rep topology failed. %d\n", ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int
hdcp2_verify_mprime(struct intel_connector *connector,
		    struct hdcp2_rep_stream_ready *stream_ready)
{
	struct hdcp_port_data *data = &connector->hdcp.port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->verify_mprime(comp->mei_dev, data, stream_ready);
	if (ret < 0)
		DRM_DEBUG_KMS("Verify mprime failed. %d\n", ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int hdcp2_authenticate_port(struct intel_connector *connector)
{
	struct hdcp_port_data *data = &connector->hdcp.port_data;
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->enable_hdcp_authentication(comp->mei_dev, data);
	if (ret < 0)
		DRM_DEBUG_KMS("Enable hdcp auth failed. %d\n", ret);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int hdcp2_close_mei_session(struct intel_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct i915_hdcp_comp_master *comp;
	int ret;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	comp = dev_priv->hdcp_master;

	if (!comp || !comp->ops) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return -EINVAL;
	}

	ret = comp->ops->close_hdcp_session(comp->mei_dev,
					     &connector->hdcp.port_data);
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return ret;
}

static int hdcp2_deauthenticate_port(struct intel_connector *connector)
{
	return hdcp2_close_mei_session(connector);
}

/* Authentication flow starts from here */
static int hdcp2_authentication_key_exchange(struct intel_connector *connector)
{
	struct intel_digital_port *intel_dig_port = conn_to_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	union {
		struct hdcp2_ake_init ake_init;
		struct hdcp2_ake_send_cert send_cert;
		struct hdcp2_ake_no_stored_km no_stored_km;
		struct hdcp2_ake_send_hprime send_hprime;
		struct hdcp2_ake_send_pairing_info pairing_info;
	} msgs;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	size_t size;
	int ret;

	/* Init for seq_num */
	hdcp->seq_num_v = 0;
	hdcp->seq_num_m = 0;

	ret = hdcp2_prepare_ake_init(connector, &msgs.ake_init);
	if (ret < 0)
		return ret;

	ret = shim->write_2_2_msg(intel_dig_port, &msgs.ake_init,
				  sizeof(msgs.ake_init));
	if (ret < 0)
		return ret;

	ret = shim->read_2_2_msg(intel_dig_port, HDCP_2_2_AKE_SEND_CERT,
				 &msgs.send_cert, sizeof(msgs.send_cert));
	if (ret < 0)
		return ret;

	if (msgs.send_cert.rx_caps[0] != HDCP_2_2_RX_CAPS_VERSION_VAL)
		return -EINVAL;

	hdcp->is_repeater = HDCP_2_2_RX_REPEATER(msgs.send_cert.rx_caps[2]);

	/*
	 * Here msgs.no_stored_km will hold msgs corresponding to the km
	 * stored also.
	 */
	ret = hdcp2_verify_rx_cert_prepare_km(connector, &msgs.send_cert,
					      &hdcp->is_paired,
					      &msgs.no_stored_km, &size);
	if (ret < 0)
		return ret;

	ret = shim->write_2_2_msg(intel_dig_port, &msgs.no_stored_km, size);
	if (ret < 0)
		return ret;

	ret = shim->read_2_2_msg(intel_dig_port, HDCP_2_2_AKE_SEND_HPRIME,
				 &msgs.send_hprime, sizeof(msgs.send_hprime));
	if (ret < 0)
		return ret;

	ret = hdcp2_verify_hprime(connector, &msgs.send_hprime);
	if (ret < 0)
		return ret;

	if (!hdcp->is_paired) {
		/* Pairing is required */
		ret = shim->read_2_2_msg(intel_dig_port,
					 HDCP_2_2_AKE_SEND_PAIRING_INFO,
					 &msgs.pairing_info,
					 sizeof(msgs.pairing_info));
		if (ret < 0)
			return ret;

		ret = hdcp2_store_pairing_info(connector, &msgs.pairing_info);
		if (ret < 0)
			return ret;
		hdcp->is_paired = true;
	}

	return 0;
}

static int hdcp2_locality_check(struct intel_connector *connector)
{
	struct intel_digital_port *intel_dig_port = conn_to_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	union {
		struct hdcp2_lc_init lc_init;
		struct hdcp2_lc_send_lprime send_lprime;
	} msgs;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	int tries = HDCP2_LC_RETRY_CNT, ret, i;

	for (i = 0; i < tries; i++) {
		ret = hdcp2_prepare_lc_init(connector, &msgs.lc_init);
		if (ret < 0)
			continue;

		ret = shim->write_2_2_msg(intel_dig_port, &msgs.lc_init,
				      sizeof(msgs.lc_init));
		if (ret < 0)
			continue;

		ret = shim->read_2_2_msg(intel_dig_port,
					 HDCP_2_2_LC_SEND_LPRIME,
					 &msgs.send_lprime,
					 sizeof(msgs.send_lprime));
		if (ret < 0)
			continue;

		ret = hdcp2_verify_lprime(connector, &msgs.send_lprime);
		if (!ret)
			break;
	}

	return ret;
}

static int hdcp2_session_key_exchange(struct intel_connector *connector)
{
	struct intel_digital_port *intel_dig_port = conn_to_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	struct hdcp2_ske_send_eks send_eks;
	int ret;

	ret = hdcp2_prepare_skey(connector, &send_eks);
	if (ret < 0)
		return ret;

	ret = hdcp->shim->write_2_2_msg(intel_dig_port, &send_eks,
					sizeof(send_eks));
	if (ret < 0)
		return ret;

	return 0;
}

static
int hdcp2_propagate_stream_management_info(struct intel_connector *connector)
{
	struct intel_digital_port *intel_dig_port = conn_to_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	union {
		struct hdcp2_rep_stream_manage stream_manage;
		struct hdcp2_rep_stream_ready stream_ready;
	} msgs;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	int ret;

	/* Prepare RepeaterAuth_Stream_Manage msg */
	msgs.stream_manage.msg_id = HDCP_2_2_REP_STREAM_MANAGE;
	drm_hdcp2_u32_to_seq_num(msgs.stream_manage.seq_num_m, hdcp->seq_num_m);

	/* K no of streams is fixed as 1. Stored as big-endian. */
	msgs.stream_manage.k = cpu_to_be16(1);

	/* For HDMI this is forced to be 0x0. For DP SST also this is 0x0. */
	msgs.stream_manage.streams[0].stream_id = 0;
	msgs.stream_manage.streams[0].stream_type = hdcp->content_type;

	/* Send it to Repeater */
	ret = shim->write_2_2_msg(intel_dig_port, &msgs.stream_manage,
				  sizeof(msgs.stream_manage));
	if (ret < 0)
		return ret;

	ret = shim->read_2_2_msg(intel_dig_port, HDCP_2_2_REP_STREAM_READY,
				 &msgs.stream_ready, sizeof(msgs.stream_ready));
	if (ret < 0)
		return ret;

	hdcp->port_data.seq_num_m = hdcp->seq_num_m;
	hdcp->port_data.streams[0].stream_type = hdcp->content_type;

	ret = hdcp2_verify_mprime(connector, &msgs.stream_ready);
	if (ret < 0)
		return ret;

	hdcp->seq_num_m++;

	if (hdcp->seq_num_m > HDCP_2_2_SEQ_NUM_MAX) {
		DRM_DEBUG_KMS("seq_num_m roll over.\n");
		return -1;
	}

	return 0;
}

static
int hdcp2_authenticate_repeater_topology(struct intel_connector *connector)
{
	struct intel_digital_port *intel_dig_port = conn_to_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	union {
		struct hdcp2_rep_send_receiverid_list recvid_list;
		struct hdcp2_rep_send_ack rep_ack;
	} msgs;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	u8 *rx_info;
	u32 seq_num_v;
	int ret;

	ret = shim->read_2_2_msg(intel_dig_port, HDCP_2_2_REP_SEND_RECVID_LIST,
				 &msgs.recvid_list, sizeof(msgs.recvid_list));
	if (ret < 0)
		return ret;

	rx_info = msgs.recvid_list.rx_info;

	if (HDCP_2_2_MAX_CASCADE_EXCEEDED(rx_info[1]) ||
	    HDCP_2_2_MAX_DEVS_EXCEEDED(rx_info[1])) {
		DRM_DEBUG_KMS("Topology Max Size Exceeded\n");
		return -EINVAL;
	}

	/* Converting and Storing the seq_num_v to local variable as DWORD */
	seq_num_v = drm_hdcp2_seq_num_to_u32(msgs.recvid_list.seq_num_v);

	if (seq_num_v < hdcp->seq_num_v) {
		/* Roll over of the seq_num_v from repeater. Reauthenticate. */
		DRM_DEBUG_KMS("Seq_num_v roll over.\n");
		return -EINVAL;
	}

	ret = hdcp2_verify_rep_topology_prepare_ack(connector,
						    &msgs.recvid_list,
						    &msgs.rep_ack);
	if (ret < 0)
		return ret;

	hdcp->seq_num_v = seq_num_v;
	ret = shim->write_2_2_msg(intel_dig_port, &msgs.rep_ack,
				  sizeof(msgs.rep_ack));
	if (ret < 0)
		return ret;

	return 0;
}

static int hdcp2_authenticate_repeater(struct intel_connector *connector)
{
	int ret;

	ret = hdcp2_authenticate_repeater_topology(connector);
	if (ret < 0)
		return ret;

	return hdcp2_propagate_stream_management_info(connector);
}

static int hdcp2_authenticate_sink(struct intel_connector *connector)
{
	struct intel_digital_port *intel_dig_port = conn_to_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	int ret;

	ret = hdcp2_authentication_key_exchange(connector);
	if (ret < 0) {
		DRM_DEBUG_KMS("AKE Failed. Err : %d\n", ret);
		return ret;
	}

	ret = hdcp2_locality_check(connector);
	if (ret < 0) {
		DRM_DEBUG_KMS("Locality Check failed. Err : %d\n", ret);
		return ret;
	}

	ret = hdcp2_session_key_exchange(connector);
	if (ret < 0) {
		DRM_DEBUG_KMS("SKE Failed. Err : %d\n", ret);
		return ret;
	}

	if (shim->config_stream_type) {
		ret = shim->config_stream_type(intel_dig_port,
					       hdcp->is_repeater,
					       hdcp->content_type);
		if (ret < 0)
			return ret;
	}

	if (hdcp->is_repeater) {
		ret = hdcp2_authenticate_repeater(connector);
		if (ret < 0) {
			DRM_DEBUG_KMS("Repeater Auth Failed. Err: %d\n", ret);
			return ret;
		}
	}

	hdcp->port_data.streams[0].stream_type = hdcp->content_type;
	ret = hdcp2_authenticate_port(connector);
	if (ret < 0)
		return ret;

	return ret;
}

static int hdcp2_enable_encryption(struct intel_connector *connector)
{
	struct intel_digital_port *intel_dig_port = conn_to_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = connector->encoder->port;
	int ret;

	WARN_ON(I915_READ(HDCP2_STATUS_DDI(port)) & LINK_ENCRYPTION_STATUS);

	if (hdcp->shim->toggle_signalling) {
		ret = hdcp->shim->toggle_signalling(intel_dig_port, true);
		if (ret) {
			DRM_ERROR("Failed to enable HDCP signalling. %d\n",
				  ret);
			return ret;
		}
	}

	if (I915_READ(HDCP2_STATUS_DDI(port)) & LINK_AUTH_STATUS) {
		/* Link is Authenticated. Now set for Encryption */
		I915_WRITE(HDCP2_CTL_DDI(port),
			   I915_READ(HDCP2_CTL_DDI(port)) |
			   CTL_LINK_ENCRYPTION_REQ);
	}

	ret = intel_wait_for_register(&dev_priv->uncore, HDCP2_STATUS_DDI(port),
				      LINK_ENCRYPTION_STATUS,
				      LINK_ENCRYPTION_STATUS,
				      ENCRYPT_STATUS_CHANGE_TIMEOUT_MS);

	return ret;
}

static int hdcp2_disable_encryption(struct intel_connector *connector)
{
	struct intel_digital_port *intel_dig_port = conn_to_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = connector->encoder->port;
	int ret;

	WARN_ON(!(I915_READ(HDCP2_STATUS_DDI(port)) & LINK_ENCRYPTION_STATUS));

	I915_WRITE(HDCP2_CTL_DDI(port),
		   I915_READ(HDCP2_CTL_DDI(port)) & ~CTL_LINK_ENCRYPTION_REQ);

	ret = intel_wait_for_register(&dev_priv->uncore, HDCP2_STATUS_DDI(port),
				      LINK_ENCRYPTION_STATUS, 0x0,
				      ENCRYPT_STATUS_CHANGE_TIMEOUT_MS);
	if (ret == -ETIMEDOUT)
		DRM_DEBUG_KMS("Disable Encryption Timedout");

	if (hdcp->shim->toggle_signalling) {
		ret = hdcp->shim->toggle_signalling(intel_dig_port, false);
		if (ret) {
			DRM_ERROR("Failed to disable HDCP signalling. %d\n",
				  ret);
			return ret;
		}
	}

	return ret;
}

static int hdcp2_authenticate_and_encrypt(struct intel_connector *connector)
{
	int ret, i, tries = 3;

	for (i = 0; i < tries; i++) {
		ret = hdcp2_authenticate_sink(connector);
		if (!ret)
			break;

		/* Clearing the mei hdcp session */
		DRM_DEBUG_KMS("HDCP2.2 Auth %d of %d Failed.(%d)\n",
			      i + 1, tries, ret);
		if (hdcp2_deauthenticate_port(connector) < 0)
			DRM_DEBUG_KMS("Port deauth failed.\n");
	}

	if (i != tries) {
		/*
		 * Ensuring the required 200mSec min time interval between
		 * Session Key Exchange and encryption.
		 */
		msleep(HDCP_2_2_DELAY_BEFORE_ENCRYPTION_EN);
		ret = hdcp2_enable_encryption(connector);
		if (ret < 0) {
			DRM_DEBUG_KMS("Encryption Enable Failed.(%d)\n", ret);
			if (hdcp2_deauthenticate_port(connector) < 0)
				DRM_DEBUG_KMS("Port deauth failed.\n");
		}
	}

	return ret;
}

static int _intel_hdcp2_enable(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	DRM_DEBUG_KMS("[%s:%d] HDCP2.2 is being enabled. Type: %d\n",
		      connector->base.name, connector->base.base.id,
		      hdcp->content_type);

	ret = hdcp2_authenticate_and_encrypt(connector);
	if (ret) {
		DRM_DEBUG_KMS("HDCP2 Type%d  Enabling Failed. (%d)\n",
			      hdcp->content_type, ret);
		return ret;
	}

	DRM_DEBUG_KMS("[%s:%d] HDCP2.2 is enabled. Type %d\n",
		      connector->base.name, connector->base.base.id,
		      hdcp->content_type);

	hdcp->hdcp2_encrypted = true;
	return 0;
}

static int _intel_hdcp2_disable(struct intel_connector *connector)
{
	int ret;

	DRM_DEBUG_KMS("[%s:%d] HDCP2.2 is being Disabled\n",
		      connector->base.name, connector->base.base.id);

	ret = hdcp2_disable_encryption(connector);

	if (hdcp2_deauthenticate_port(connector) < 0)
		DRM_DEBUG_KMS("Port deauth failed.\n");

	connector->hdcp.hdcp2_encrypted = false;

	return ret;
}

/* Implements the Link Integrity Check for HDCP2.2 */
static int intel_hdcp2_check_link(struct intel_connector *connector)
{
	struct intel_digital_port *intel_dig_port = conn_to_dig_port(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = connector->encoder->port;
	int ret = 0;

	mutex_lock(&hdcp->mutex);

	/* hdcp2_check_link is expected only when HDCP2.2 is Enabled */
	if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_ENABLED ||
	    !hdcp->hdcp2_encrypted) {
		ret = -EINVAL;
		goto out;
	}

	if (WARN_ON(!intel_hdcp2_in_use(connector))) {
		DRM_ERROR("HDCP2.2 link stopped the encryption, %x\n",
			  I915_READ(HDCP2_STATUS_DDI(port)));
		ret = -ENXIO;
		hdcp->value = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		schedule_work(&hdcp->prop_work);
		goto out;
	}

	ret = hdcp->shim->check_2_2_link(intel_dig_port);
	if (ret == HDCP_LINK_PROTECTED) {
		if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
			hdcp->value = DRM_MODE_CONTENT_PROTECTION_ENABLED;
			schedule_work(&hdcp->prop_work);
		}
		goto out;
	}

	if (ret == HDCP_TOPOLOGY_CHANGE) {
		if (hdcp->value == DRM_MODE_CONTENT_PROTECTION_UNDESIRED)
			goto out;

		DRM_DEBUG_KMS("HDCP2.2 Downstream topology change\n");
		ret = hdcp2_authenticate_repeater_topology(connector);
		if (!ret) {
			hdcp->value = DRM_MODE_CONTENT_PROTECTION_ENABLED;
			schedule_work(&hdcp->prop_work);
			goto out;
		}
		DRM_DEBUG_KMS("[%s:%d] Repeater topology auth failed.(%d)\n",
			      connector->base.name, connector->base.base.id,
			      ret);
	} else {
		DRM_DEBUG_KMS("[%s:%d] HDCP2.2 link failed, retrying auth\n",
			      connector->base.name, connector->base.base.id);
	}

	ret = _intel_hdcp2_disable(connector);
	if (ret) {
		DRM_ERROR("[%s:%d] Failed to disable hdcp2.2 (%d)\n",
			  connector->base.name, connector->base.base.id, ret);
		hdcp->value = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		schedule_work(&hdcp->prop_work);
		goto out;
	}

	ret = _intel_hdcp2_enable(connector);
	if (ret) {
		DRM_DEBUG_KMS("[%s:%d] Failed to enable hdcp2.2 (%d)\n",
			      connector->base.name, connector->base.base.id,
			      ret);
		hdcp->value = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		schedule_work(&hdcp->prop_work);
		goto out;
	}

out:
	mutex_unlock(&hdcp->mutex);
	return ret;
}

static void intel_hdcp_check_work(struct work_struct *work)
{
	struct intel_hdcp *hdcp = container_of(to_delayed_work(work),
					       struct intel_hdcp,
					       check_work);
	struct intel_connector *connector = intel_hdcp_to_connector(hdcp);

	if (!intel_hdcp2_check_link(connector))
		schedule_delayed_work(&hdcp->check_work,
				      DRM_HDCP2_CHECK_PERIOD_MS);
	else if (!intel_hdcp_check_link(connector))
		schedule_delayed_work(&hdcp->check_work,
				      DRM_HDCP_CHECK_PERIOD_MS);
}

static int i915_hdcp_component_bind(struct device *i915_kdev,
				    struct device *mei_kdev, void *data)
{
	struct drm_i915_private *dev_priv = kdev_to_i915(i915_kdev);

	DRM_DEBUG("I915 HDCP comp bind\n");
	mutex_lock(&dev_priv->hdcp_comp_mutex);
	dev_priv->hdcp_master = (struct i915_hdcp_comp_master *)data;
	dev_priv->hdcp_master->mei_dev = mei_kdev;
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	return 0;
}

static void i915_hdcp_component_unbind(struct device *i915_kdev,
				       struct device *mei_kdev, void *data)
{
	struct drm_i915_private *dev_priv = kdev_to_i915(i915_kdev);

	DRM_DEBUG("I915 HDCP comp unbind\n");
	mutex_lock(&dev_priv->hdcp_comp_mutex);
	dev_priv->hdcp_master = NULL;
	mutex_unlock(&dev_priv->hdcp_comp_mutex);
}

static const struct component_ops i915_hdcp_component_ops = {
	.bind   = i915_hdcp_component_bind,
	.unbind = i915_hdcp_component_unbind,
};

static inline int initialize_hdcp_port_data(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;
	struct hdcp_port_data *data = &hdcp->port_data;

	data->port = connector->encoder->port;
	data->port_type = (u8)HDCP_PORT_TYPE_INTEGRATED;
	data->protocol = (u8)hdcp->shim->protocol;

	data->k = 1;
	if (!data->streams)
		data->streams = kcalloc(data->k,
					sizeof(struct hdcp2_streamid_type),
					GFP_KERNEL);
	if (!data->streams) {
		DRM_ERROR("Out of Memory\n");
		return -ENOMEM;
	}

	data->streams[0].stream_id = 0;
	data->streams[0].stream_type = hdcp->content_type;

	return 0;
}

static bool is_hdcp2_supported(struct drm_i915_private *dev_priv)
{
	if (!IS_ENABLED(CONFIG_INTEL_MEI_HDCP))
		return false;

	return (INTEL_GEN(dev_priv) >= 10 || IS_GEMINILAKE(dev_priv) ||
		IS_KABYLAKE(dev_priv));
}

void intel_hdcp_component_init(struct drm_i915_private *dev_priv)
{
	int ret;

	if (!is_hdcp2_supported(dev_priv))
		return;

	mutex_lock(&dev_priv->hdcp_comp_mutex);
	WARN_ON(dev_priv->hdcp_comp_added);

	dev_priv->hdcp_comp_added = true;
	mutex_unlock(&dev_priv->hdcp_comp_mutex);
	ret = component_add_typed(dev_priv->drm.dev, &i915_hdcp_component_ops,
				  I915_COMPONENT_HDCP);
	if (ret < 0) {
		DRM_DEBUG_KMS("Failed at component add(%d)\n", ret);
		mutex_lock(&dev_priv->hdcp_comp_mutex);
		dev_priv->hdcp_comp_added = false;
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return;
	}
}

static void intel_hdcp2_init(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	ret = initialize_hdcp_port_data(connector);
	if (ret) {
		DRM_DEBUG_KMS("Mei hdcp data init failed\n");
		return;
	}

	hdcp->hdcp2_supported = true;
}

int intel_hdcp_init(struct intel_connector *connector,
		    const struct intel_hdcp_shim *shim)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	if (!shim)
		return -EINVAL;

	ret = drm_connector_attach_content_protection_property(&connector->base);
	if (ret)
		return ret;

	hdcp->shim = shim;
	mutex_init(&hdcp->mutex);
	INIT_DELAYED_WORK(&hdcp->check_work, intel_hdcp_check_work);
	INIT_WORK(&hdcp->prop_work, intel_hdcp_prop_work);

	if (is_hdcp2_supported(dev_priv))
		intel_hdcp2_init(connector);
	init_waitqueue_head(&hdcp->cp_irq_queue);

	return 0;
}

int intel_hdcp_enable(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;
	unsigned long check_link_interval = DRM_HDCP_CHECK_PERIOD_MS;
	int ret = -EINVAL;

	if (!hdcp->shim)
		return -ENOENT;

	mutex_lock(&hdcp->mutex);
	WARN_ON(hdcp->value == DRM_MODE_CONTENT_PROTECTION_ENABLED);

	/*
	 * Considering that HDCP2.2 is more secure than HDCP1.4, If the setup
	 * is capable of HDCP2.2, it is preferred to use HDCP2.2.
	 */
	if (intel_hdcp2_capable(connector)) {
		ret = _intel_hdcp2_enable(connector);
		if (!ret)
			check_link_interval = DRM_HDCP2_CHECK_PERIOD_MS;
	}

	/* When HDCP2.2 fails, HDCP1.4 will be attempted */
	if (ret && intel_hdcp_capable(connector)) {
		ret = _intel_hdcp_enable(connector);
	}

	if (!ret) {
		schedule_delayed_work(&hdcp->check_work, check_link_interval);
		hdcp->value = DRM_MODE_CONTENT_PROTECTION_ENABLED;
		schedule_work(&hdcp->prop_work);
	}

	mutex_unlock(&hdcp->mutex);
	return ret;
}

int intel_hdcp_disable(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret = 0;

	if (!hdcp->shim)
		return -ENOENT;

	mutex_lock(&hdcp->mutex);

	if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
		hdcp->value = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;
		if (hdcp->hdcp2_encrypted)
			ret = _intel_hdcp2_disable(connector);
		else if (hdcp->hdcp_encrypted)
			ret = _intel_hdcp_disable(connector);
	}

	mutex_unlock(&hdcp->mutex);
	cancel_delayed_work_sync(&hdcp->check_work);
	return ret;
}

void intel_hdcp_component_fini(struct drm_i915_private *dev_priv)
{
	mutex_lock(&dev_priv->hdcp_comp_mutex);
	if (!dev_priv->hdcp_comp_added) {
		mutex_unlock(&dev_priv->hdcp_comp_mutex);
		return;
	}

	dev_priv->hdcp_comp_added = false;
	mutex_unlock(&dev_priv->hdcp_comp_mutex);

	component_del(dev_priv->drm.dev, &i915_hdcp_component_ops);
}

void intel_hdcp_cleanup(struct intel_connector *connector)
{
	if (!connector->hdcp.shim)
		return;

	mutex_lock(&connector->hdcp.mutex);
	kfree(connector->hdcp.port_data.streams);
	mutex_unlock(&connector->hdcp.mutex);
}

void intel_hdcp_atomic_check(struct drm_connector *connector,
			     struct drm_connector_state *old_state,
			     struct drm_connector_state *new_state)
{
	u64 old_cp = old_state->content_protection;
	u64 new_cp = new_state->content_protection;
	struct drm_crtc_state *crtc_state;

	if (!new_state->crtc) {
		/*
		 * If the connector is being disabled with CP enabled, mark it
		 * desired so it's re-enabled when the connector is brought back
		 */
		if (old_cp == DRM_MODE_CONTENT_PROTECTION_ENABLED)
			new_state->content_protection =
				DRM_MODE_CONTENT_PROTECTION_DESIRED;
		return;
	}

	/*
	 * Nothing to do if the state didn't change, or HDCP was activated since
	 * the last commit
	 */
	if (old_cp == new_cp ||
	    (old_cp == DRM_MODE_CONTENT_PROTECTION_DESIRED &&
	     new_cp == DRM_MODE_CONTENT_PROTECTION_ENABLED))
		return;

	crtc_state = drm_atomic_get_new_crtc_state(new_state->state,
						   new_state->crtc);
	crtc_state->mode_changed = true;
}

/* Handles the CP_IRQ raised from the DP HDCP sink */
void intel_hdcp_handle_cp_irq(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;

	if (!hdcp->shim)
		return;

	atomic_inc(&connector->hdcp.cp_irq_count);
	wake_up_all(&connector->hdcp.cp_irq_queue);

	schedule_delayed_work(&hdcp->check_work, 0);
}
