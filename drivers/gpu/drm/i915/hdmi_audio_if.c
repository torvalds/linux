/*
 * Copyright (c) 2010, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Authors:
 *	jim liu <jim.liu@intel.com>
 *	Uma Shankar <uma.shankar@intel.com>
 */

#include <drm/drmP.h>
#include "hdmi_audio_if.h"
#include "i915_drv.h"
#include "i915_reg.h"

#define CONFIG_SUPPORT_HDMI_AUDIO
#ifdef CONFIG_SUPPORT_HDMI_AUDIO

int i915_hdmi_state;

/*
 * Audio register range 0x65000 to 0x65FFF
 */

#define IS_HDMI_AUDIO_I915(reg) ((reg >= 0x65000) && (reg < 0x65FFF))

/* Added for HDMI Audio */
#define HAD_MAX_ELD_BYTES 84
uint8_t hdmi_eld[HAD_MAX_ELD_BYTES];

static struct hdmi_audio_priv *hdmi_priv;

void i915_hdmi_audio_init(struct hdmi_audio_priv *p_hdmi_priv)
{
	hdmi_priv = p_hdmi_priv;
}

/* Added for HDMI Audio */
void hdmi_get_eld(uint8_t *eld)
{
	struct drm_device *dev = hdmi_priv->dev;
	struct drm_i915_private *dev_priv =
		(struct drm_i915_private *) dev->dev_private;
	memcpy(hdmi_eld, eld, HAD_MAX_ELD_BYTES);
	mid_hdmi_audio_signal_event(dev, HAD_EVENT_HOT_PLUG);
}

static inline int android_hdmi_get_eld(struct drm_device *dev, void *eld)
{
	memcpy(eld, hdmi_eld, HAD_MAX_ELD_BYTES);
	return 0;
}

struct hdmi_audio_priv *get_hdmi_priv()
{
	return hdmi_priv;
}

/*
 * return whether HDMI audio device is busy.
 */
bool mid_hdmi_audio_is_busy(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv =
		(struct drm_i915_private *) dev->dev_private;
	int hdmi_audio_busy = 0;
	hdmi_audio_event_t hdmi_audio_event;

	if (i915_hdmi_state == connector_status_disconnected) {
		/* HDMI is not connected, assuming audio device is idle. */
		return false;
	}

	if (dev_priv->had_interface) {
		hdmi_audio_event.type = HAD_EVENT_QUERY_IS_AUDIO_BUSY;
		hdmi_audio_busy = dev_priv->had_interface->query(
				dev_priv->had_pvt_data,
				hdmi_audio_event);
		return hdmi_audio_busy != 0;
	}
	return false;
}

/*
 * return whether HDMI audio device is suspended.
 */
bool mid_hdmi_audio_suspend(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv =
		(struct drm_i915_private *) dev->dev_private;
	hdmi_audio_event_t hdmi_audio_event;
	int ret = 0;

	if (i915_hdmi_state == connector_status_disconnected) {
		/* HDMI is not connected, assuming audio device
		 * is suspended already.
		 */
		return true;
	}
	DRM_DEBUG_DRIVER("%s: i915_hdmi_state %d", __func__,
			i915_hdmi_state);

	if (dev_priv->had_interface) {
		hdmi_audio_event.type = 0;
		ret = dev_priv->had_interface->suspend(dev_priv->had_pvt_data,
				hdmi_audio_event);
		return (ret == 0) ? true : false;
	}
	return true;
}

void mid_hdmi_audio_resume(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv =
		(struct drm_i915_private *) dev->dev_private;

	if (i915_hdmi_state == connector_status_disconnected) {
		/* HDMI is not connected, there is no need
		 * to resume audio device.
		 */
		return;
	}
	DRM_DEBUG_DRIVER("%s: i915_hdmi_state %d", __func__,
				i915_hdmi_state);

	if (dev_priv->had_interface)
		dev_priv->had_interface->resume(dev_priv->had_pvt_data);
}

void mid_hdmi_audio_signal_event(struct drm_device *dev,
		enum had_event_type event)
{
	struct drm_i915_private *dev_priv =
		(struct drm_i915_private *) dev->dev_private;

	if (dev_priv->had_event_callbacks)
		(*dev_priv->had_event_callbacks)(event,
			dev_priv->had_pvt_data);
}

/**
 * hdmi_audio_write:
 * used to write into display controller HDMI audio registers.
 *
 */
static int hdmi_audio_write(uint32_t reg, uint32_t val)
{
	struct drm_device *dev = hdmi_priv->dev;
	struct drm_i915_private *dev_priv =
		(struct drm_i915_private *) dev->dev_private;
	int ret = 0;

	if (hdmi_priv->monitor_type == MONITOR_TYPE_DVI)
		return 0;

	if (IS_HDMI_AUDIO_I915(reg))
		I915_WRITE(_MMIO(VLV_DISPLAY_BASE + reg), val);
	else
		ret = -EINVAL;

	return ret;
}

/**
 * hdmi_audio_read:
 * used to get the register value read from
 * display controller HDMI audio registers.
 */
static int hdmi_audio_read(uint32_t reg, uint32_t *val)
{
	struct drm_device *dev = hdmi_priv->dev;
	struct drm_i915_private *dev_priv =
		(struct drm_i915_private *) dev->dev_private;
	int ret = 0;

	if (hdmi_priv->monitor_type == MONITOR_TYPE_DVI)
		return 0;

	if (IS_HDMI_AUDIO_I915(reg))
		*val = I915_READ(_MMIO(VLV_DISPLAY_BASE + reg));
	else
		ret = -EINVAL;

	return ret;
}

/**
 * hdmi_audio_rmw:
 * used to update the masked bits in display controller HDMI audio registers .
 *
 */
static int hdmi_audio_rmw(uint32_t reg, uint32_t val, uint32_t mask)
{
	struct drm_device *dev = hdmi_priv->dev;
	struct drm_i915_private *dev_priv =
		(struct drm_i915_private *) dev->dev_private;
	int ret = 0;
	uint32_t val_tmp = 0;

	if (IS_HDMI_AUDIO_I915(reg)) {
		val_tmp = (val & mask) |
			(I915_READ(_MMIO(VLV_DISPLAY_BASE + reg)) & ~mask);
		I915_WRITE(_MMIO(VLV_DISPLAY_BASE + reg), val_tmp);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

/**
 * hdmi_audio_get_caps:
 * used to return the HDMI audio capabilities.
 * e.g. resolution, frame rate.
 */
static int hdmi_audio_get_caps(enum had_caps_list get_element,
			void *capabilities)
{
	struct drm_device *dev = hdmi_priv->dev;
	struct drm_i915_private *dev_priv =
		(struct drm_i915_private *) dev->dev_private;
	int ret = 0;

	DRM_DEBUG_DRIVER("\n");

	switch (get_element) {
	case HAD_GET_ELD:
		ret = android_hdmi_get_eld(dev, capabilities);
		break;
	case HAD_GET_SAMPLING_FREQ:
		/* ToDo: Verify if sampling freq logic is correct */
		memcpy(capabilities, &(dev_priv->tmds_clock_speed),
			sizeof(uint32_t));
		break;
	default:
		break;
	}

	return ret;
}

/**
 * hdmi_audio_get_register_base
 * used to get the current hdmi base address
 */
int hdmi_audio_get_register_base(uint32_t *reg_base)
{
	*reg_base = hdmi_priv->hdmi_lpe_audio_reg;
	return 0;
}

/**
 * hdmi_audio_set_caps:
 * used to set the HDMI audio capabilities.
 * e.g. Audio INT.
 */
static int hdmi_audio_set_caps(enum had_caps_list set_element,
			void *capabilties)
{
	struct drm_device *dev = hdmi_priv->dev;
	struct drm_i915_private *dev_priv =
		(struct drm_i915_private *) dev->dev_private;
	int ret = 0;
	u32 hdmi_reg;
	u32 int_masks = 0;

	DRM_DEBUG_DRIVER("\n");

	switch (set_element) {
	case HAD_SET_ENABLE_AUDIO:
		hdmi_reg = I915_READ(_MMIO(hdmi_priv->hdmi_reg));
		if (hdmi_reg & PORT_ENABLE)
			hdmi_reg |= SDVO_AUDIO_ENABLE;

		I915_WRITE(_MMIO(hdmi_priv->hdmi_reg), hdmi_reg);
		I915_READ(_MMIO(hdmi_priv->hdmi_reg));
		break;
	case HAD_SET_DISABLE_AUDIO:
		hdmi_reg = I915_READ(_MMIO(hdmi_priv->hdmi_reg)) &
			~SDVO_AUDIO_ENABLE;
		I915_WRITE(_MMIO(hdmi_priv->hdmi_reg), hdmi_reg);
		I915_READ(_MMIO(hdmi_priv->hdmi_reg));
		break;

	case HAD_SET_ENABLE_AUDIO_INT:
		if (*((u32 *)capabilties) & HDMI_AUDIO_UNDERRUN)
			int_masks |= I915_HDMI_AUDIO_UNDERRUN_ENABLE;
		dev_priv->hdmi_audio_interrupt_mask |= int_masks;
		i915_enable_hdmi_audio_int(dev);
		break;
	case HAD_SET_DISABLE_AUDIO_INT:
		if (*((u32 *)capabilties) & HDMI_AUDIO_UNDERRUN)
			int_masks |= I915_HDMI_AUDIO_UNDERRUN_ENABLE;
		dev_priv->hdmi_audio_interrupt_mask &= ~int_masks;

		i915_disable_hdmi_audio_int(dev);
		break;
	default:
		break;
	}

	return ret;
}

static struct  hdmi_audio_registers_ops hdmi_audio_reg_ops = {
	.hdmi_audio_get_register_base = hdmi_audio_get_register_base,
	.hdmi_audio_read_register = hdmi_audio_read,
	.hdmi_audio_write_register = hdmi_audio_write,
	.hdmi_audio_read_modify = hdmi_audio_rmw,
};

static struct hdmi_audio_query_set_ops hdmi_audio_get_set_ops = {
	.hdmi_audio_get_caps = hdmi_audio_get_caps,
	.hdmi_audio_set_caps = hdmi_audio_set_caps,
};

int mid_hdmi_audio_setup(
		had_event_call_back audio_callbacks,
		struct hdmi_audio_registers_ops *reg_ops,
		struct hdmi_audio_query_set_ops *query_ops)
{
	struct drm_device *dev = hdmi_priv->dev;
	struct drm_i915_private *dev_priv =
		(struct drm_i915_private *) dev->dev_private;
	int ret = 0;

	DRM_DEBUG_DRIVER("%s: called\n", __func__);

	reg_ops->hdmi_audio_get_register_base =
		(hdmi_audio_reg_ops.hdmi_audio_get_register_base);
	reg_ops->hdmi_audio_read_register =
		(hdmi_audio_reg_ops.hdmi_audio_read_register);
	reg_ops->hdmi_audio_write_register =
		(hdmi_audio_reg_ops.hdmi_audio_write_register);
	reg_ops->hdmi_audio_read_modify =
		(hdmi_audio_reg_ops.hdmi_audio_read_modify);
	query_ops->hdmi_audio_get_caps =
		hdmi_audio_get_set_ops.hdmi_audio_get_caps;
	query_ops->hdmi_audio_set_caps =
		hdmi_audio_get_set_ops.hdmi_audio_set_caps;

	dev_priv->had_event_callbacks = audio_callbacks;

	return ret;
}
EXPORT_SYMBOL(mid_hdmi_audio_setup);

int mid_hdmi_audio_register(struct snd_intel_had_interface *driver,
				void *had_data)
{
	struct drm_device *dev;
	struct drm_i915_private *dev_priv;

	DRM_DEBUG_DRIVER("%s: called\n", __func__);
	if (!hdmi_priv)
		return -ENODEV;

	dev = hdmi_priv->dev;
	dev_priv = (struct drm_i915_private *) dev->dev_private;
	dev_priv->had_pvt_data = had_data;
	dev_priv->had_interface = driver;

	if (hdmi_priv->monitor_type == MONITOR_TYPE_DVI)
		return 0;

	/* The Audio driver is loading now and we need to notify
	 * it if there is an HDMI device attached
	 */
	DRM_INFO("%s: Scheduling HDMI audio work queue\n", __func__);
	schedule_work(&dev_priv->hdmi_audio_wq);

	return 0;
}
EXPORT_SYMBOL(mid_hdmi_audio_register);
#else
bool hdmi_audio_is_busy(struct drm_device *dev)
{
	/* always in idle state */
	return false;
}

bool hdmi_audio_suspend(struct drm_device *dev)
{
	/* always in suspend state */
	return true;
}

void hdmi_audio_resume(struct drm_device *dev)
{
}

void hdmi_audio_signal_event(struct drm_device *dev, enum had_event_type event)
{
}

void i915_hdmi_audio_init(struct hdmi_audio_priv *hdmi_priv)
{
	DRM_INFO("%s: HDMI is not supported.\n", __func__);
}

int mid_hdmi_audio_setup(
		had_event_call_back audio_callbacks,
		struct hdmi_audio_registers_ops *reg_ops,
		struct hdmi_audio_query_set_ops *query_ops)
{
	DRM_ERROR("%s: HDMI is not supported.\n", __func__);
	return -ENODEV;
}
EXPORT_SYMBOL(mid_hdmi_audio_setup);

int mid_hdmi_audio_register(struct snd_intel_had_interface *driver,
			void *had_data)
{
	DRM_ERROR("%s: HDMI is not supported.\n", __func__);
	return -ENODEV;
}
EXPORT_SYMBOL(mid_hdmi_audio_register);
#endif
