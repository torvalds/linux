// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2023 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 *
 * This module handles all the sysfs info/configuration that is related to the
 * v4l2 input devices.
 */

#include <linux/device.h>
#include "mgb4_core.h"
#include "mgb4_i2c.h"
#include "mgb4_vin.h"
#include "mgb4_cmt.h"
#include "mgb4_sysfs.h"

/* Common for both FPDL3 and GMSL */

static ssize_t input_id_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);

	return sprintf(buf, "%d\n", vindev->config->id);
}

static ssize_t oldi_lane_width_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	struct mgb4_dev *mgbdev = vindev->mgbdev;
	u16 i2c_reg;
	u8 i2c_mask, i2c_single_val, i2c_dual_val;
	u32 config;
	int ret;

	i2c_reg = MGB4_IS_GMSL(mgbdev) ? 0x1CE : 0x49;
	i2c_mask = MGB4_IS_GMSL(mgbdev) ? 0x0E : 0x03;
	i2c_single_val = MGB4_IS_GMSL(mgbdev) ? 0x00 : 0x02;
	i2c_dual_val = MGB4_IS_GMSL(mgbdev) ? 0x0E : 0x00;

	mutex_lock(&mgbdev->i2c_lock);
	ret = mgb4_i2c_read_byte(&vindev->deser, i2c_reg);
	mutex_unlock(&mgbdev->i2c_lock);
	if (ret < 0)
		return -EIO;

	config = mgb4_read_reg(&mgbdev->video, vindev->config->regs.config);

	if (((config & (1U << 9)) && ((ret & i2c_mask) != i2c_dual_val)) ||
	    (!(config & (1U << 9)) && ((ret & i2c_mask) != i2c_single_val))) {
		dev_err(dev, "I2C/FPGA register value mismatch\n");
		return -EINVAL;
	}

	return sprintf(buf, "%s\n", config & (1U << 9) ? "1" : "0");
}

/*
 * OLDI lane width change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t oldi_lane_width_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	struct mgb4_dev *mgbdev = vindev->mgbdev;
	u32 fpga_data;
	u16 i2c_reg;
	u8 i2c_mask, i2c_data;
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	switch (val) {
	case 0: /* single */
		fpga_data = 0;
		i2c_data = MGB4_IS_GMSL(mgbdev) ? 0x00 : 0x02;
		break;
	case 1: /* dual */
		fpga_data = 1U << 9;
		i2c_data = MGB4_IS_GMSL(mgbdev) ? 0x0E : 0x00;
		break;
	default:
		return -EINVAL;
	}

	i2c_reg = MGB4_IS_GMSL(mgbdev) ? 0x1CE : 0x49;
	i2c_mask = MGB4_IS_GMSL(mgbdev) ? 0x0E : 0x03;

	mutex_lock(&mgbdev->i2c_lock);
	ret = mgb4_i2c_mask_byte(&vindev->deser, i2c_reg, i2c_mask, i2c_data);
	mutex_unlock(&mgbdev->i2c_lock);
	if (ret < 0)
		return -EIO;
	mgb4_mask_reg(&mgbdev->video, vindev->config->regs.config, 1U << 9,
		      fpga_data);
	if (MGB4_IS_GMSL(mgbdev)) {
		/* reset input link */
		mutex_lock(&mgbdev->i2c_lock);
		ret = mgb4_i2c_mask_byte(&vindev->deser, 0x10, 1U << 5, 1U << 5);
		mutex_unlock(&mgbdev->i2c_lock);
		if (ret < 0)
			return -EIO;
	}

	return count;
}

static ssize_t color_mapping_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 config = mgb4_read_reg(&vindev->mgbdev->video,
	  vindev->config->regs.config);

	return sprintf(buf, "%s\n", config & (1U << 8) ? "0" : "1");
}

/*
 * Color mapping change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t color_mapping_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 fpga_data;
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	switch (val) {
	case 0: /* OLDI/JEIDA */
		fpga_data = (1U << 8);
		break;
	case 1: /* SPWG/VESA */
		fpga_data = 0;
		break;
	default:
		return -EINVAL;
	}

	mgb4_mask_reg(&vindev->mgbdev->video, vindev->config->regs.config,
		      1U << 8, fpga_data);

	return count;
}

static ssize_t link_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 status = mgb4_read_reg(&vindev->mgbdev->video,
				   vindev->config->regs.status);

	return sprintf(buf, "%s\n", status & (1U << 2) ? "1" : "0");
}

static ssize_t stream_status_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 status = mgb4_read_reg(&vindev->mgbdev->video,
				   vindev->config->regs.status);

	return sprintf(buf, "%s\n", ((status & (1 << 14)) &&
		       (status & (1 << 2)) && (status & (3 << 9))) ? "1" : "0");
}

static ssize_t video_width_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 config = mgb4_read_reg(&vindev->mgbdev->video,
	  vindev->config->regs.resolution);

	return sprintf(buf, "%u\n", config >> 16);
}

static ssize_t video_height_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 config = mgb4_read_reg(&vindev->mgbdev->video,
	  vindev->config->regs.resolution);

	return sprintf(buf, "%u\n", config & 0xFFFF);
}

static ssize_t hsync_status_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 status = mgb4_read_reg(&vindev->mgbdev->video,
				   vindev->config->regs.status);
	u32 res;

	if (!(status & (1U << 11)))
		res = 0x02; // not available
	else if (status & (1U << 12))
		res = 0x01; // active high
	else
		res = 0x00; // active low

	return sprintf(buf, "%u\n", res);
}

static ssize_t vsync_status_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 status = mgb4_read_reg(&vindev->mgbdev->video,
				   vindev->config->regs.status);
	u32 res;

	if (!(status & (1U << 11)))
		res = 0x02; // not available
	else if (status & (1U << 13))
		res = 0x01; // active high
	else
		res = 0x00; // active low

	return sprintf(buf, "%u\n", res);
}

static ssize_t hsync_gap_length_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 sync = mgb4_read_reg(&vindev->mgbdev->video,
				 vindev->config->regs.sync);

	return sprintf(buf, "%u\n", sync >> 16);
}

/*
 * HSYNC gap length change is expected to be called on live streams. Video
 * device locking/queue check is not needed.
 */
static ssize_t hsync_gap_length_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 0xFFFF)
		return -EINVAL;

	mgb4_mask_reg(&vindev->mgbdev->video, vindev->config->regs.sync,
		      0xFFFF0000, val << 16);

	return count;
}

static ssize_t vsync_gap_length_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 sync = mgb4_read_reg(&vindev->mgbdev->video,
				 vindev->config->regs.sync);

	return sprintf(buf, "%u\n", sync & 0xFFFF);
}

/*
 * VSYNC gap length change is expected to be called on live streams. Video
 * device locking/queue check is not needed.
 */
static ssize_t vsync_gap_length_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 0xFFFF)
		return -EINVAL;

	mgb4_mask_reg(&vindev->mgbdev->video, vindev->config->regs.sync, 0xFFFF,
		      val);

	return count;
}

static ssize_t pclk_frequency_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 freq = mgb4_read_reg(&vindev->mgbdev->video,
				 vindev->config->regs.pclk);

	return sprintf(buf, "%u\n", freq);
}

static ssize_t hsync_width_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 sig = mgb4_read_reg(&vindev->mgbdev->video,
				vindev->config->regs.hsync);

	return sprintf(buf, "%u\n", (sig & 0x00FF0000) >> 16);
}

static ssize_t vsync_width_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 sig = mgb4_read_reg(&vindev->mgbdev->video,
				vindev->config->regs.vsync);

	return sprintf(buf, "%u\n", (sig & 0x00FF0000) >> 16);
}

static ssize_t hback_porch_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 sig = mgb4_read_reg(&vindev->mgbdev->video,
				vindev->config->regs.hsync);

	return sprintf(buf, "%u\n", (sig & 0x0000FF00) >> 8);
}

static ssize_t hfront_porch_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 sig = mgb4_read_reg(&vindev->mgbdev->video,
				vindev->config->regs.hsync);

	return sprintf(buf, "%u\n", (sig & 0x000000FF));
}

static ssize_t vback_porch_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 sig = mgb4_read_reg(&vindev->mgbdev->video,
				vindev->config->regs.vsync);

	return sprintf(buf, "%u\n", (sig & 0x0000FF00) >> 8);
}

static ssize_t vfront_porch_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u32 sig = mgb4_read_reg(&vindev->mgbdev->video,
				vindev->config->regs.vsync);

	return sprintf(buf, "%u\n", (sig & 0x000000FF));
}

static ssize_t frequency_range_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);

	return sprintf(buf, "%d\n", vindev->freq_range);
}

static ssize_t frequency_range_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 1)
		return -EINVAL;

	mutex_lock(vindev->vdev.lock);
	if (vb2_is_busy(vindev->vdev.queue)) {
		mutex_unlock(vindev->vdev.lock);
		return -EBUSY;
	}

	mgb4_cmt_set_vin_freq_range(vindev, val);
	vindev->freq_range = val;

	mutex_unlock(vindev->vdev.lock);

	return count;
}

/* FPDL3 only */

static ssize_t fpdl3_input_width_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	s32 ret;

	mutex_lock(&vindev->mgbdev->i2c_lock);
	ret = mgb4_i2c_read_byte(&vindev->deser, 0x34);
	mutex_unlock(&vindev->mgbdev->i2c_lock);
	if (ret < 0)
		return -EIO;

	switch ((u8)ret & 0x18) {
	case 0:
		return sprintf(buf, "0\n");
	case 0x10:
		return sprintf(buf, "1\n");
	case 0x08:
		return sprintf(buf, "2\n");
	default:
		return -EINVAL;
	}
}

/*
 * FPD-Link width change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t fpdl3_input_width_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	u8 i2c_data;
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	switch (val) {
	case 0: /* auto */
		i2c_data = 0x00;
		break;
	case 1: /* single */
		i2c_data = 0x10;
		break;
	case 2: /* dual */
		i2c_data = 0x08;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&vindev->mgbdev->i2c_lock);
	ret = mgb4_i2c_mask_byte(&vindev->deser, 0x34, 0x18, i2c_data);
	mutex_unlock(&vindev->mgbdev->i2c_lock);
	if (ret < 0)
		return -EIO;

	return count;
}

/* GMSL only */

static ssize_t gmsl_mode_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	s32 r1, r300, r3;

	mutex_lock(&vindev->mgbdev->i2c_lock);
	r1 = mgb4_i2c_read_byte(&vindev->deser, 0x01);
	r300 = mgb4_i2c_read_byte(&vindev->deser, 0x300);
	r3 = mgb4_i2c_read_byte(&vindev->deser, 0x03);
	mutex_unlock(&vindev->mgbdev->i2c_lock);
	if (r1 < 0 || r300 < 0 || r3 < 0)
		return -EIO;

	if ((r1 & 0x03) == 0x03 && (r300 & 0x0C) == 0x0C && (r3 & 0xC0) == 0xC0)
		return sprintf(buf, "0\n");
	else if ((r1 & 0x03) == 0x02 && (r300 & 0x0C) == 0x08 && (r3 & 0xC0) == 0x00)
		return sprintf(buf, "1\n");
	else if ((r1 & 0x03) == 0x01 && (r300 & 0x0C) == 0x04 && (r3 & 0xC0) == 0x00)
		return sprintf(buf, "2\n");
	else if ((r1 & 0x03) == 0x00 && (r300 & 0x0C) == 0x00 && (r3 & 0xC0) == 0x00)
		return sprintf(buf, "3\n");
	else
		return -EINVAL;
}

/*
 * GMSL mode change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t gmsl_mode_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	static const struct mgb4_i2c_kv G12[] = {
		{0x01, 0x03, 0x03}, {0x300, 0x0C, 0x0C}, {0x03, 0xC0, 0xC0}};
	static const struct mgb4_i2c_kv G6[] = {
		{0x01, 0x03, 0x02}, {0x300, 0x0C, 0x08}, {0x03, 0xC0, 0x00}};
	static const struct mgb4_i2c_kv G3[] = {
		{0x01, 0x03, 0x01}, {0x300, 0x0C, 0x04}, {0x03, 0xC0, 0x00}};
	static const struct mgb4_i2c_kv G1[] = {
		{0x01, 0x03, 0x00}, {0x300, 0x0C, 0x00}, {0x03, 0xC0, 0x00}};
	static const struct mgb4_i2c_kv reset[] = {
		{0x10, 1U << 5, 1U << 5}, {0x300, 1U << 6, 1U << 6}};
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	const struct mgb4_i2c_kv *values;
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	switch (val) {
	case 0: /* 12Gb/s */
		values = G12;
		break;
	case 1: /* 6Gb/s */
		values = G6;
		break;
	case 2: /* 3Gb/s */
		values = G3;
		break;
	case 3: /* 1.5Gb/s */
		values = G1;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&vindev->mgbdev->i2c_lock);
	ret = mgb4_i2c_configure(&vindev->deser, values, 3);
	ret |= mgb4_i2c_configure(&vindev->deser, reset, 2);
	mutex_unlock(&vindev->mgbdev->i2c_lock);
	if (ret < 0)
		return -EIO;

	return count;
}

static ssize_t gmsl_stream_id_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	s32 ret;

	mutex_lock(&vindev->mgbdev->i2c_lock);
	ret = mgb4_i2c_read_byte(&vindev->deser, 0xA0);
	mutex_unlock(&vindev->mgbdev->i2c_lock);
	if (ret < 0)
		return -EIO;

	return sprintf(buf, "%d\n", ret & 0x03);
}

static ssize_t gmsl_stream_id_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 3)
		return -EINVAL;

	mutex_lock(vindev->vdev.lock);
	if (vb2_is_busy(vindev->vdev.queue)) {
		mutex_unlock(vindev->vdev.lock);
		return -EBUSY;
	}

	mutex_lock(&vindev->mgbdev->i2c_lock);
	ret = mgb4_i2c_mask_byte(&vindev->deser, 0xA0, 0x03, (u8)val);
	mutex_unlock(&vindev->mgbdev->i2c_lock);

	mutex_unlock(vindev->vdev.lock);

	return (ret < 0) ? -EIO : count;
}

static ssize_t gmsl_fec_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	s32 r3e0, r308;

	mutex_lock(&vindev->mgbdev->i2c_lock);
	r3e0 = mgb4_i2c_read_byte(&vindev->deser, 0x3E0);
	r308 = mgb4_i2c_read_byte(&vindev->deser, 0x308);
	mutex_unlock(&vindev->mgbdev->i2c_lock);
	if (r3e0 < 0 || r308 < 0)
		return -EIO;

	if ((r3e0 & 0x07) == 0x00 && (r308 & 0x01) == 0x00)
		return sprintf(buf, "0\n");
	else if ((r3e0 & 0x07) == 0x07 && (r308 & 0x01) == 0x01)
		return sprintf(buf, "1\n");
	else
		return -EINVAL;
}

/*
 * GMSL FEC change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t gmsl_fec_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vin_dev *vindev = video_get_drvdata(vdev);
	static const struct mgb4_i2c_kv enable[] = {
		{0x3E0, 0x07, 0x07}, {0x308, 0x01, 0x01}};
	static const struct mgb4_i2c_kv disable[] = {
		{0x3E0, 0x07, 0x00}, {0x308, 0x01, 0x00}};
	static const struct mgb4_i2c_kv reset[] = {
		{0x10, 1U << 5, 1U << 5}, {0x300, 1U << 6, 1U << 6}};
	const struct mgb4_i2c_kv *values;
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	switch (val) {
	case 0: /* disabled */
		values = disable;
		break;
	case 1: /* enabled */
		values = enable;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&vindev->mgbdev->i2c_lock);
	ret = mgb4_i2c_configure(&vindev->deser, values, 2);
	ret |= mgb4_i2c_configure(&vindev->deser, reset, 2);
	mutex_unlock(&vindev->mgbdev->i2c_lock);
	if (ret < 0)
		return -EIO;

	return count;
}

static DEVICE_ATTR_RO(input_id);
static DEVICE_ATTR_RW(oldi_lane_width);
static DEVICE_ATTR_RW(color_mapping);
static DEVICE_ATTR_RO(link_status);
static DEVICE_ATTR_RO(stream_status);
static DEVICE_ATTR_RO(video_width);
static DEVICE_ATTR_RO(video_height);
static DEVICE_ATTR_RO(hsync_status);
static DEVICE_ATTR_RO(vsync_status);
static DEVICE_ATTR_RW(hsync_gap_length);
static DEVICE_ATTR_RW(vsync_gap_length);
static DEVICE_ATTR_RO(pclk_frequency);
static DEVICE_ATTR_RO(hsync_width);
static DEVICE_ATTR_RO(vsync_width);
static DEVICE_ATTR_RO(hback_porch);
static DEVICE_ATTR_RO(hfront_porch);
static DEVICE_ATTR_RO(vback_porch);
static DEVICE_ATTR_RO(vfront_porch);
static DEVICE_ATTR_RW(frequency_range);

static DEVICE_ATTR_RW(fpdl3_input_width);

static DEVICE_ATTR_RW(gmsl_mode);
static DEVICE_ATTR_RW(gmsl_stream_id);
static DEVICE_ATTR_RW(gmsl_fec);

struct attribute *mgb4_fpdl3_in_attrs[] = {
	&dev_attr_input_id.attr,
	&dev_attr_link_status.attr,
	&dev_attr_stream_status.attr,
	&dev_attr_video_width.attr,
	&dev_attr_video_height.attr,
	&dev_attr_hsync_status.attr,
	&dev_attr_vsync_status.attr,
	&dev_attr_oldi_lane_width.attr,
	&dev_attr_color_mapping.attr,
	&dev_attr_hsync_gap_length.attr,
	&dev_attr_vsync_gap_length.attr,
	&dev_attr_pclk_frequency.attr,
	&dev_attr_hsync_width.attr,
	&dev_attr_vsync_width.attr,
	&dev_attr_hback_porch.attr,
	&dev_attr_hfront_porch.attr,
	&dev_attr_vback_porch.attr,
	&dev_attr_vfront_porch.attr,
	&dev_attr_frequency_range.attr,
	&dev_attr_fpdl3_input_width.attr,
	NULL
};

struct attribute *mgb4_gmsl_in_attrs[] = {
	&dev_attr_input_id.attr,
	&dev_attr_link_status.attr,
	&dev_attr_stream_status.attr,
	&dev_attr_video_width.attr,
	&dev_attr_video_height.attr,
	&dev_attr_hsync_status.attr,
	&dev_attr_vsync_status.attr,
	&dev_attr_oldi_lane_width.attr,
	&dev_attr_color_mapping.attr,
	&dev_attr_hsync_gap_length.attr,
	&dev_attr_vsync_gap_length.attr,
	&dev_attr_pclk_frequency.attr,
	&dev_attr_hsync_width.attr,
	&dev_attr_vsync_width.attr,
	&dev_attr_hback_porch.attr,
	&dev_attr_hfront_porch.attr,
	&dev_attr_vback_porch.attr,
	&dev_attr_vfront_porch.attr,
	&dev_attr_frequency_range.attr,
	&dev_attr_gmsl_mode.attr,
	&dev_attr_gmsl_stream_id.attr,
	&dev_attr_gmsl_fec.attr,
	NULL
};
