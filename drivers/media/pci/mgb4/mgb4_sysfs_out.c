// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2023 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 *
 * This module handles all the sysfs info/configuration that is related to the
 * v4l2 output devices.
 */

#include <linux/device.h>
#include <linux/nospec.h>
#include "mgb4_core.h"
#include "mgb4_i2c.h"
#include "mgb4_vout.h"
#include "mgb4_vin.h"
#include "mgb4_cmt.h"
#include "mgb4_sysfs.h"

static int loopin_cnt(struct mgb4_vin_dev *vindev)
{
	struct mgb4_vout_dev *voutdev;
	u32 config;
	int i, cnt = 0;

	for (i = 0; i < MGB4_VOUT_DEVICES; i++) {
		voutdev = vindev->mgbdev->vout[i];
		if (!voutdev)
			continue;

		config = mgb4_read_reg(&voutdev->mgbdev->video,
				       voutdev->config->regs.config);
		if ((config & 0xc) >> 2 == vindev->config->id)
			cnt++;
	}

	return cnt;
}

static bool is_busy(struct video_device *dev)
{
	bool ret;

	mutex_lock(dev->lock);
	ret = vb2_is_busy(dev->queue);
	mutex_unlock(dev->lock);

	return ret;
}

/* Common for both FPDL3 and GMSL */

static ssize_t output_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);

	return sprintf(buf, "%d\n", voutdev->config->id);
}

static ssize_t video_source_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	u32 config = mgb4_read_reg(&voutdev->mgbdev->video,
	  voutdev->config->regs.config);

	return sprintf(buf, "%u\n", (config & 0xc) >> 2);
}

/*
 * Video source change may affect the buffer queue of ANY video input/output on
 * the card thus if any of the inputs/outputs is in use, we do not allow
 * the change.
 *
 * As we do not want to lock all the video devices at the same time, a two-stage
 * locking strategy is used. In addition to the video device locking there is
 * a global (PCI device) variable "io_reconfig" atomically checked/set when
 * the reconfiguration is running. All the video devices check the variable in
 * their queue_setup() functions and do not allow to start the queue when
 * the reconfiguration has started.
 */
static ssize_t video_source_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	struct mgb4_dev *mgbdev = voutdev->mgbdev;
	struct mgb4_vin_dev *loopin_new = NULL, *loopin_old = NULL;
	unsigned long val;
	ssize_t ret;
	u32 config;
	int i;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 3)
		return -EINVAL;

	if (test_and_set_bit(0, &mgbdev->io_reconfig))
		return -EBUSY;

	ret = -EBUSY;
	for (i = 0; i < MGB4_VIN_DEVICES; i++)
		if (mgbdev->vin[i] && is_busy(&mgbdev->vin[i]->vdev))
			goto end;
	for (i = 0; i < MGB4_VOUT_DEVICES; i++)
		if (mgbdev->vout[i] && is_busy(&mgbdev->vout[i]->vdev))
			goto end;

	config = mgb4_read_reg(&mgbdev->video, voutdev->config->regs.config);

	if (((config & 0xc) >> 2) < MGB4_VIN_DEVICES)
		loopin_old = mgbdev->vin[(config & 0xc) >> 2];
	if (val < MGB4_VIN_DEVICES) {
		val = array_index_nospec(val, MGB4_VIN_DEVICES);
		loopin_new = mgbdev->vin[val];
	}
	if (loopin_old && loopin_cnt(loopin_old) == 1)
		mgb4_mask_reg(&mgbdev->video, loopin_old->config->regs.config,
			      0x2, 0x0);
	if (loopin_new)
		mgb4_mask_reg(&mgbdev->video, loopin_new->config->regs.config,
			      0x2, 0x2);

	if (val == voutdev->config->id + MGB4_VIN_DEVICES)
		mgb4_write_reg(&mgbdev->video, voutdev->config->regs.config,
			       config & ~(1 << 1));
	else
		mgb4_write_reg(&mgbdev->video, voutdev->config->regs.config,
			       config | (1U << 1));

	mgb4_mask_reg(&mgbdev->video, voutdev->config->regs.config, 0xc,
		      val << 2);

	ret = count;
end:
	clear_bit(0, &mgbdev->io_reconfig);

	return ret;
}

static ssize_t display_width_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	u32 config = mgb4_read_reg(&voutdev->mgbdev->video,
	  voutdev->config->regs.resolution);

	return sprintf(buf, "%u\n", config >> 16);
}

static ssize_t display_width_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 0xFFFF)
		return -EINVAL;

	mutex_lock(voutdev->vdev.lock);
	if (vb2_is_busy(voutdev->vdev.queue)) {
		mutex_unlock(voutdev->vdev.lock);
		return -EBUSY;
	}

	mgb4_mask_reg(&voutdev->mgbdev->video, voutdev->config->regs.resolution,
		      0xFFFF0000, val << 16);

	mutex_unlock(voutdev->vdev.lock);

	return count;
}

static ssize_t display_height_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	u32 config = mgb4_read_reg(&voutdev->mgbdev->video,
	  voutdev->config->regs.resolution);

	return sprintf(buf, "%u\n", config & 0xFFFF);
}

static ssize_t display_height_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 0xFFFF)
		return -EINVAL;

	mutex_lock(voutdev->vdev.lock);
	if (vb2_is_busy(voutdev->vdev.queue)) {
		mutex_unlock(voutdev->vdev.lock);
		return -EBUSY;
	}

	mgb4_mask_reg(&voutdev->mgbdev->video, voutdev->config->regs.resolution,
		      0xFFFF, val);

	mutex_unlock(voutdev->vdev.lock);

	return count;
}

static ssize_t frame_rate_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	u32 period = mgb4_read_reg(&voutdev->mgbdev->video,
				   voutdev->config->regs.frame_period);

	return sprintf(buf, "%u\n", 125000000 / period);
}

/*
 * Frame rate change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t frame_rate_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	mgb4_write_reg(&voutdev->mgbdev->video,
		       voutdev->config->regs.frame_period, 125000000 / val);

	return count;
}

static ssize_t hsync_width_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	u32 sig = mgb4_read_reg(&voutdev->mgbdev->video,
				voutdev->config->regs.hsync);

	return sprintf(buf, "%u\n", (sig & 0x00FF0000) >> 16);
}

/*
 * HSYNC width change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t hsync_width_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 0xFF)
		return -EINVAL;

	mgb4_mask_reg(&voutdev->mgbdev->video, voutdev->config->regs.hsync,
		      0x00FF0000, val << 16);

	return count;
}

static ssize_t vsync_width_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	u32 sig = mgb4_read_reg(&voutdev->mgbdev->video,
				voutdev->config->regs.vsync);

	return sprintf(buf, "%u\n", (sig & 0x00FF0000) >> 16);
}

/*
 * VSYNC vidth change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t vsync_width_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 0xFF)
		return -EINVAL;

	mgb4_mask_reg(&voutdev->mgbdev->video, voutdev->config->regs.vsync,
		      0x00FF0000, val << 16);

	return count;
}

static ssize_t hback_porch_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	u32 sig = mgb4_read_reg(&voutdev->mgbdev->video,
				voutdev->config->regs.hsync);

	return sprintf(buf, "%u\n", (sig & 0x0000FF00) >> 8);
}

/*
 * hback porch change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t hback_porch_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 0xFF)
		return -EINVAL;

	mgb4_mask_reg(&voutdev->mgbdev->video, voutdev->config->regs.hsync,
		      0x0000FF00, val << 8);

	return count;
}

static ssize_t vback_porch_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	u32 sig = mgb4_read_reg(&voutdev->mgbdev->video,
				voutdev->config->regs.vsync);

	return sprintf(buf, "%u\n", (sig & 0x0000FF00) >> 8);
}

/*
 * vback porch change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t vback_porch_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 0xFF)
		return -EINVAL;

	mgb4_mask_reg(&voutdev->mgbdev->video, voutdev->config->regs.vsync,
		      0x0000FF00, val << 8);

	return count;
}

static ssize_t hfront_porch_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	u32 sig = mgb4_read_reg(&voutdev->mgbdev->video,
				voutdev->config->regs.hsync);

	return sprintf(buf, "%u\n", (sig & 0x000000FF));
}

/*
 * hfront porch change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t hfront_porch_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 0xFF)
		return -EINVAL;

	mgb4_mask_reg(&voutdev->mgbdev->video, voutdev->config->regs.hsync,
		      0x000000FF, val);

	return count;
}

static ssize_t vfront_porch_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	u32 sig = mgb4_read_reg(&voutdev->mgbdev->video,
				voutdev->config->regs.vsync);

	return sprintf(buf, "%u\n", (sig & 0x000000FF));
}

/*
 * vfront porch change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t vfront_porch_store(struct device *dev,
				  struct device_attribute *attr, const char *buf,
				  size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 0xFF)
		return -EINVAL;

	mgb4_mask_reg(&voutdev->mgbdev->video, voutdev->config->regs.vsync,
		      0x000000FF, val);

	return count;
}

/* FPDL3 only */

static ssize_t hsync_polarity_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	u32 config = mgb4_read_reg(&voutdev->mgbdev->video,
	  voutdev->config->regs.hsync);

	return sprintf(buf, "%u\n", (config & (1U << 31)) >> 31);
}

/*
 * HSYNC polarity change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t hsync_polarity_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 1)
		return -EINVAL;

	mgb4_mask_reg(&voutdev->mgbdev->video, voutdev->config->regs.hsync,
		      (1U << 31), val << 31);

	return count;
}

static ssize_t vsync_polarity_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	u32 config = mgb4_read_reg(&voutdev->mgbdev->video,
	  voutdev->config->regs.vsync);

	return sprintf(buf, "%u\n", (config & (1U << 31)) >> 31);
}

/*
 * VSYNC polarity change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t vsync_polarity_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 1)
		return -EINVAL;

	mgb4_mask_reg(&voutdev->mgbdev->video, voutdev->config->regs.vsync,
		      (1U << 31), val << 31);

	return count;
}

static ssize_t de_polarity_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	u32 config = mgb4_read_reg(&voutdev->mgbdev->video,
	  voutdev->config->regs.vsync);

	return sprintf(buf, "%u\n", (config & (1U << 30)) >> 30);
}

/*
 * DE polarity change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t de_polarity_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val > 1)
		return -EINVAL;

	mgb4_mask_reg(&voutdev->mgbdev->video, voutdev->config->regs.vsync,
		      (1U << 30), val << 30);

	return count;
}

static ssize_t fpdl3_output_width_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	s32 ret;

	mutex_lock(&voutdev->mgbdev->i2c_lock);
	ret = mgb4_i2c_read_byte(&voutdev->ser, 0x5B);
	mutex_unlock(&voutdev->mgbdev->i2c_lock);
	if (ret < 0)
		return -EIO;

	switch ((u8)ret & 0x03) {
	case 0:
		return sprintf(buf, "0\n");
	case 1:
		return sprintf(buf, "1\n");
	case 3:
		return sprintf(buf, "2\n");
	default:
		return -EINVAL;
	}
}

/*
 * FPD-Link width change is expected to be called on live streams. Video device
 * locking/queue check is not needed.
 */
static ssize_t fpdl3_output_width_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
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
		i2c_data = 0x01;
		break;
	case 2: /* dual */
		i2c_data = 0x03;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&voutdev->mgbdev->i2c_lock);
	ret = mgb4_i2c_mask_byte(&voutdev->ser, 0x5B, 0x03, i2c_data);
	mutex_unlock(&voutdev->mgbdev->i2c_lock);
	if (ret < 0)
		return -EIO;

	return count;
}

static ssize_t pclk_frequency_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);

	return sprintf(buf, "%u\n", voutdev->freq);
}

static ssize_t pclk_frequency_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct video_device *vdev = to_video_device(dev);
	struct mgb4_vout_dev *voutdev = video_get_drvdata(vdev);
	unsigned long val;
	int ret;
	unsigned int dp;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(voutdev->vdev.lock);
	if (vb2_is_busy(voutdev->vdev.queue)) {
		mutex_unlock(voutdev->vdev.lock);
		return -EBUSY;
	}

	dp = (val > 50000) ? 1 : 0;
	voutdev->freq = mgb4_cmt_set_vout_freq(voutdev, val >> dp) << dp;

	mgb4_mask_reg(&voutdev->mgbdev->video, voutdev->config->regs.config,
		      0x10, dp << 4);
	mutex_lock(&voutdev->mgbdev->i2c_lock);
	ret = mgb4_i2c_mask_byte(&voutdev->ser, 0x4F, 1 << 6, ((~dp) & 1) << 6);
	mutex_unlock(&voutdev->mgbdev->i2c_lock);

	mutex_unlock(voutdev->vdev.lock);

	return (ret < 0) ? -EIO : count;
}

static DEVICE_ATTR_RO(output_id);
static DEVICE_ATTR_RW(video_source);
static DEVICE_ATTR_RW(display_width);
static DEVICE_ATTR_RW(display_height);
static DEVICE_ATTR_RW(frame_rate);
static DEVICE_ATTR_RW(hsync_polarity);
static DEVICE_ATTR_RW(vsync_polarity);
static DEVICE_ATTR_RW(de_polarity);
static DEVICE_ATTR_RW(pclk_frequency);
static DEVICE_ATTR_RW(hsync_width);
static DEVICE_ATTR_RW(vsync_width);
static DEVICE_ATTR_RW(hback_porch);
static DEVICE_ATTR_RW(hfront_porch);
static DEVICE_ATTR_RW(vback_porch);
static DEVICE_ATTR_RW(vfront_porch);

static DEVICE_ATTR_RW(fpdl3_output_width);

struct attribute *mgb4_fpdl3_out_attrs[] = {
	&dev_attr_output_id.attr,
	&dev_attr_video_source.attr,
	&dev_attr_display_width.attr,
	&dev_attr_display_height.attr,
	&dev_attr_frame_rate.attr,
	&dev_attr_hsync_polarity.attr,
	&dev_attr_vsync_polarity.attr,
	&dev_attr_de_polarity.attr,
	&dev_attr_pclk_frequency.attr,
	&dev_attr_hsync_width.attr,
	&dev_attr_vsync_width.attr,
	&dev_attr_hback_porch.attr,
	&dev_attr_hfront_porch.attr,
	&dev_attr_vback_porch.attr,
	&dev_attr_vfront_porch.attr,
	&dev_attr_fpdl3_output_width.attr,
	NULL
};

struct attribute *mgb4_gmsl_out_attrs[] = {
	&dev_attr_output_id.attr,
	&dev_attr_video_source.attr,
	&dev_attr_display_width.attr,
	&dev_attr_display_height.attr,
	&dev_attr_frame_rate.attr,
	NULL
};
