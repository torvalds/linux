/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <drm/drm_dp_dual_mode_helper.h>
#include <drm/drmP.h>

/**
 * DOC: dp dual mode helpers
 *
 * Helper functions to deal with DP dual mode (aka. DP++) adaptors.
 *
 * Type 1:
 * Adaptor registers (if any) and the sink DDC bus may be accessed via I2C.
 *
 * Type 2:
 * Adaptor registers and sink DDC bus can be accessed either via I2C or
 * I2C-over-AUX. Source devices may choose to implement either of these
 * access methods.
 */

#define DP_DUAL_MODE_SLAVE_ADDRESS 0x40

/**
 * drm_dp_dual_mode_read - Read from the DP dual mode adaptor register(s)
 * @adapter: I2C adapter for the DDC bus
 * @offset: register offset
 * @buffer: buffer for return data
 * @size: sizo of the buffer
 *
 * Reads @size bytes from the DP dual mode adaptor registers
 * starting at @offset.
 *
 * Returns:
 * 0 on success, negative error code on failure
 */
ssize_t drm_dp_dual_mode_read(struct i2c_adapter *adapter,
			      u8 offset, void *buffer, size_t size)
{
	struct i2c_msg msgs[] = {
		{
			.addr = DP_DUAL_MODE_SLAVE_ADDRESS,
			.flags = 0,
			.len = 1,
			.buf = &offset,
		},
		{
			.addr = DP_DUAL_MODE_SLAVE_ADDRESS,
			.flags = I2C_M_RD,
			.len = size,
			.buf = buffer,
		},
	};
	int ret;

	ret = i2c_transfer(adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		return ret;
	if (ret != ARRAY_SIZE(msgs))
		return -EPROTO;

	return 0;
}
EXPORT_SYMBOL(drm_dp_dual_mode_read);

/**
 * drm_dp_dual_mode_write - Write to the DP dual mode adaptor register(s)
 * @adapter: I2C adapter for the DDC bus
 * @offset: register offset
 * @buffer: buffer for write data
 * @size: sizo of the buffer
 *
 * Writes @size bytes to the DP dual mode adaptor registers
 * starting at @offset.
 *
 * Returns:
 * 0 on success, negative error code on failure
 */
ssize_t drm_dp_dual_mode_write(struct i2c_adapter *adapter,
			       u8 offset, const void *buffer, size_t size)
{
	struct i2c_msg msg = {
		.addr = DP_DUAL_MODE_SLAVE_ADDRESS,
		.flags = 0,
		.len = 1 + size,
		.buf = NULL,
	};
	void *data;
	int ret;

	data = kmalloc(msg.len, GFP_TEMPORARY);
	if (!data)
		return -ENOMEM;

	msg.buf = data;

	memcpy(data, &offset, 1);
	memcpy(data + 1, buffer, size);

	ret = i2c_transfer(adapter, &msg, 1);

	kfree(data);

	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EPROTO;

	return 0;
}
EXPORT_SYMBOL(drm_dp_dual_mode_write);

static bool is_hdmi_adaptor(const char hdmi_id[DP_DUAL_MODE_HDMI_ID_LEN])
{
	static const char dp_dual_mode_hdmi_id[DP_DUAL_MODE_HDMI_ID_LEN] =
		"DP-HDMI ADAPTOR\x04";

	return memcmp(hdmi_id, dp_dual_mode_hdmi_id,
		      sizeof(dp_dual_mode_hdmi_id)) == 0;
}

static bool is_type2_adaptor(uint8_t adaptor_id)
{
	return adaptor_id == (DP_DUAL_MODE_TYPE_TYPE2 |
			      DP_DUAL_MODE_REV_TYPE2);
}

/**
 * drm_dp_dual_mode_detect - Identify the DP dual mode adaptor
 * @adapter: I2C adapter for the DDC bus
 *
 * Attempt to identify the type of the DP dual mode adaptor used.
 *
 * Note that when the answer is @DRM_DP_DUAL_MODE_UNKNOWN it's not
 * certain whether we're dealing with a native HDMI port or
 * a type 1 DVI dual mode adaptor. The driver will have to use
 * some other hardware/driver specific mechanism to make that
 * distinction.
 *
 * Returns:
 * The type of the DP dual mode adaptor used
 */
enum drm_dp_dual_mode_type drm_dp_dual_mode_detect(struct i2c_adapter *adapter)
{
	char hdmi_id[DP_DUAL_MODE_HDMI_ID_LEN] = {};
	uint8_t adaptor_id = 0x00;
	ssize_t ret;

	/*
	 * Let's see if the adaptor is there the by reading the
	 * HDMI ID registers.
	 *
	 * Note that type 1 DVI adaptors are not required to implemnt
	 * any registers, and that presents a problem for detection.
	 * If the i2c transfer is nacked, we may or may not be dealing
	 * with a type 1 DVI adaptor. Some other mechanism of detecting
	 * the presence of the adaptor is required. One way would be
	 * to check the state of the CONFIG1 pin, Another method would
	 * simply require the driver to know whether the port is a DP++
	 * port or a native HDMI port. Both of these methods are entirely
	 * hardware/driver specific so we can't deal with them here.
	 */
	ret = drm_dp_dual_mode_read(adapter, DP_DUAL_MODE_HDMI_ID,
				    hdmi_id, sizeof(hdmi_id));
	if (ret)
		return DRM_DP_DUAL_MODE_UNKNOWN;

	/*
	 * Sigh. Some (maybe all?) type 1 adaptors are broken and ack
	 * the offset but ignore it, and instead they just always return
	 * data from the start of the HDMI ID buffer. So for a broken
	 * type 1 HDMI adaptor a single byte read will always give us
	 * 0x44, and for a type 1 DVI adaptor it should give 0x00
	 * (assuming it implements any registers). Fortunately neither
	 * of those values will match the type 2 signature of the
	 * DP_DUAL_MODE_ADAPTOR_ID register so we can proceed with
	 * the type 2 adaptor detection safely even in the presence
	 * of broken type 1 adaptors.
	 */
	ret = drm_dp_dual_mode_read(adapter, DP_DUAL_MODE_ADAPTOR_ID,
				    &adaptor_id, sizeof(adaptor_id));
	if (ret == 0) {
		if (is_type2_adaptor(adaptor_id)) {
			if (is_hdmi_adaptor(hdmi_id))
				return DRM_DP_DUAL_MODE_TYPE2_HDMI;
			else
				return DRM_DP_DUAL_MODE_TYPE2_DVI;
		}
	}

	if (is_hdmi_adaptor(hdmi_id))
		return DRM_DP_DUAL_MODE_TYPE1_HDMI;
	else
		return DRM_DP_DUAL_MODE_TYPE1_DVI;
}
EXPORT_SYMBOL(drm_dp_dual_mode_detect);

/**
 * drm_dp_dual_mode_max_tmds_clock - Max TMDS clock for DP dual mode adaptor
 * @type: DP dual mode adaptor type
 * @adapter: I2C adapter for the DDC bus
 *
 * Determine the max TMDS clock the adaptor supports based on the
 * type of the dual mode adaptor and the DP_DUAL_MODE_MAX_TMDS_CLOCK
 * register (on type2 adaptors). As some type 1 adaptors have
 * problems with registers (see comments in drm_dp_dual_mode_detect())
 * we don't read the register on those, instead we simply assume
 * a 165 MHz limit based on the specification.
 *
 * Returns:
 * Maximum supported TMDS clock rate for the DP dual mode adaptor in kHz.
 */
int drm_dp_dual_mode_max_tmds_clock(enum drm_dp_dual_mode_type type,
				    struct i2c_adapter *adapter)
{
	uint8_t max_tmds_clock;
	ssize_t ret;

	/* native HDMI so no limit */
	if (type == DRM_DP_DUAL_MODE_NONE)
		return 0;

	/*
	 * Type 1 adaptors are limited to 165MHz
	 * Type 2 adaptors can tells us their limit
	 */
	if (type < DRM_DP_DUAL_MODE_TYPE2_DVI)
		return 165000;

	ret = drm_dp_dual_mode_read(adapter, DP_DUAL_MODE_MAX_TMDS_CLOCK,
				    &max_tmds_clock, sizeof(max_tmds_clock));
	if (ret || max_tmds_clock == 0x00 || max_tmds_clock == 0xff) {
		DRM_DEBUG_KMS("Failed to query max TMDS clock\n");
		return 165000;
	}

	return max_tmds_clock * 5000 / 2;
}
EXPORT_SYMBOL(drm_dp_dual_mode_max_tmds_clock);

/**
 * drm_dp_dual_mode_get_tmds_output - Get the state of the TMDS output buffers in the DP dual mode adaptor
 * @type: DP dual mode adaptor type
 * @adapter: I2C adapter for the DDC bus
 * @enabled: current state of the TMDS output buffers
 *
 * Get the state of the TMDS output buffers in the adaptor. For
 * type2 adaptors this is queried from the DP_DUAL_MODE_TMDS_OEN
 * register. As some type 1 adaptors have problems with registers
 * (see comments in drm_dp_dual_mode_detect()) we don't read the
 * register on those, instead we simply assume that the buffers
 * are always enabled.
 *
 * Returns:
 * 0 on success, negative error code on failure
 */
int drm_dp_dual_mode_get_tmds_output(enum drm_dp_dual_mode_type type,
				     struct i2c_adapter *adapter,
				     bool *enabled)
{
	uint8_t tmds_oen;
	ssize_t ret;

	if (type < DRM_DP_DUAL_MODE_TYPE2_DVI) {
		*enabled = true;
		return 0;
	}

	ret = drm_dp_dual_mode_read(adapter, DP_DUAL_MODE_TMDS_OEN,
				    &tmds_oen, sizeof(tmds_oen));
	if (ret) {
		DRM_DEBUG_KMS("Failed to query state of TMDS output buffers\n");
		return ret;
	}

	*enabled = !(tmds_oen & DP_DUAL_MODE_TMDS_DISABLE);

	return 0;
}
EXPORT_SYMBOL(drm_dp_dual_mode_get_tmds_output);

/**
 * drm_dp_dual_mode_set_tmds_output - Enable/disable TMDS output buffers in the DP dual mode adaptor
 * @type: DP dual mode adaptor type
 * @adapter: I2C adapter for the DDC bus
 * @enable: enable (as opposed to disable) the TMDS output buffers
 *
 * Set the state of the TMDS output buffers in the adaptor. For
 * type2 this is set via the DP_DUAL_MODE_TMDS_OEN register. As
 * some type 1 adaptors have problems with registers (see comments
 * in drm_dp_dual_mode_detect()) we avoid touching the register,
 * making this function a no-op on type 1 adaptors.
 *
 * Returns:
 * 0 on success, negative error code on failure
 */
int drm_dp_dual_mode_set_tmds_output(enum drm_dp_dual_mode_type type,
				     struct i2c_adapter *adapter, bool enable)
{
	uint8_t tmds_oen = enable ? 0 : DP_DUAL_MODE_TMDS_DISABLE;
	ssize_t ret;

	if (type < DRM_DP_DUAL_MODE_TYPE2_DVI)
		return 0;

	ret = drm_dp_dual_mode_write(adapter, DP_DUAL_MODE_TMDS_OEN,
				     &tmds_oen, sizeof(tmds_oen));
	if (ret) {
		DRM_DEBUG_KMS("Failed to %s TMDS output buffers\n",
			      enable ? "enable" : "disable");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_dual_mode_set_tmds_output);

/**
 * drm_dp_get_dual_mode_type_name - Get the name of the DP dual mode adaptor type as a string
 * @type: DP dual mode adaptor type
 *
 * Returns:
 * String representation of the DP dual mode adaptor type
 */
const char *drm_dp_get_dual_mode_type_name(enum drm_dp_dual_mode_type type)
{
	switch (type) {
	case DRM_DP_DUAL_MODE_NONE:
		return "none";
	case DRM_DP_DUAL_MODE_TYPE1_DVI:
		return "type 1 DVI";
	case DRM_DP_DUAL_MODE_TYPE1_HDMI:
		return "type 1 HDMI";
	case DRM_DP_DUAL_MODE_TYPE2_DVI:
		return "type 2 DVI";
	case DRM_DP_DUAL_MODE_TYPE2_HDMI:
		return "type 2 HDMI";
	default:
		WARN_ON(type != DRM_DP_DUAL_MODE_UNKNOWN);
		return "unknown";
	}
}
EXPORT_SYMBOL(drm_dp_get_dual_mode_type_name);
