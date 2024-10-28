// SPDX-License-Identifier: GPL-2.0-only
/*
 * Core MDSS framebuffer driver.
 *
 * Copyright (C) 2007 Google Incorporated
 * Copyright (c) 2008-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/videodev2.h>
#include <linux/console.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/msm_mdp.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/proc_fs.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>
#include "mdss_fb.h"
#include "mdss_mdp_splash_logo.h"
#define CREATE_TRACE_POINTS
#include "mdss_debug.h"
#include "mdss_smmu.h"
#include "mdss_mdp.h"
#include "mdss_sync.h"

MODULE_IMPORT_NS(DMA_BUF);

#ifdef CONFIG_FB_MSM_TRIPLE_BUFFER
#define MDSS_FB_NUM 3
#else
#define MDSS_FB_NUM 2
#endif

#ifndef EXPORT_COMPAT
#define EXPORT_COMPAT(x)
#endif

#define MAX_FBI_LIST 32

#ifndef TARGET_HW_MDSS_MDP3
#define BLANK_FLAG_LP	FB_BLANK_NORMAL
#define BLANK_FLAG_ULP	FB_BLANK_VSYNC_SUSPEND
#else
#define BLANK_FLAG_LP	FB_BLANK_VSYNC_SUSPEND
#define BLANK_FLAG_ULP	FB_BLANK_NORMAL
#endif

/*
 * Time period for fps calulation in micro seconds.
 * Default value is set to 1 sec.
 */
#define MDP_TIME_PERIOD_CALC_FPS_US	1000000

struct iosys_map map;

struct file *fb_file;		/* current file node */
static struct fb_info *fbi_list[MAX_FBI_LIST];
static int fbi_list_index;

static u32 mdss_fb_pseudo_palette[16] = {
	0x00000000, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff
};

static struct msm_mdp_interface *mdp_instance;

static int mdss_fb_register(struct msm_fb_data_type *mfd);
static int mdss_fb_open(struct fb_info *info, int user);
static int mdss_fb_release(struct fb_info *info, int user);
static int mdss_fb_release_all(struct fb_info *info, bool release_all);
static int mdss_fb_pan_display(struct fb_var_screeninfo *var,
			       struct fb_info *info);
static int mdss_fb_check_var(struct fb_var_screeninfo *var,
			     struct fb_info *info);
static int mdss_fb_set_par(struct fb_info *info);
static int mdss_fb_blank_sub(int blank_mode, struct fb_info *info,
			     int op_enable);
static int mdss_fb_suspend_sub(struct msm_fb_data_type *mfd);
static int mdss_fb_ioctl(struct fb_info *info, unsigned int cmd,
			 unsigned long arg);
static int mdss_fb_fbmem_ion_mmap(struct fb_info *info,
		struct vm_area_struct *vma);
static int mdss_fb_alloc_fb_ion_memory(struct msm_fb_data_type *mfd,
		size_t size);
static void mdss_fb_release_fences(struct msm_fb_data_type *mfd);
#ifndef CONFIG_FB_MSM_MDP_NONE
static int __mdss_fb_sync_buf_done_callback(struct notifier_block *p,
		unsigned long val, void *data);
#endif

static int __mdss_fb_display_thread(void *data);
static int mdss_fb_pan_idle(struct msm_fb_data_type *mfd);
static int mdss_fb_send_panel_event(struct msm_fb_data_type *mfd,
					int event, void *arg);
static void mdss_fb_set_mdp_sync_pt_threshold(struct msm_fb_data_type *mfd,
		int type);

static inline void __user *to_user_ptr(uint64_t address)
{
	return (void __user *)(uintptr_t)address;
}

static inline uint64_t __user to_user_u64(void *ptr)
{
	return (uint64_t)((uintptr_t)ptr);
}

void mdss_fb_no_update_notify_timer_cb(struct timer_list *list_data)
{
	struct msm_fb_data_type *mfd = from_timer(mfd, list_data, no_update.timer);

	if (!mfd) {
		pr_err("%s mfd NULL\n", __func__);
		return;
	}
	mfd->no_update.value = NOTIFY_TYPE_NO_UPDATE;
	complete(&mfd->no_update.comp);
}

void mdss_fb_bl_update_notify(struct msm_fb_data_type *mfd,
		uint32_t notification_type)
{
	struct mdss_overlay_private *mdp5_data = NULL;

	if (!mfd) {
		pr_err("%s mfd NULL\n", __func__);
		return;
	}
	mutex_lock(&mfd->update.lock);
	if (mfd->update.is_suspend) {
		mutex_unlock(&mfd->update.lock);
		return;
	}
	if (mfd->update.ref_count > 0) {
		mutex_unlock(&mfd->update.lock);
		mfd->update.value = notification_type;
		complete(&mfd->update.comp);
		mutex_lock(&mfd->update.lock);
	}
	mutex_unlock(&mfd->update.lock);

	mutex_lock(&mfd->no_update.lock);
	if (mfd->no_update.ref_count > 0) {
		mutex_unlock(&mfd->no_update.lock);
		mfd->no_update.value = notification_type;
		complete(&mfd->no_update.comp);
		mutex_lock(&mfd->no_update.lock);
	}
	mutex_unlock(&mfd->no_update.lock);
	mdp5_data = mfd_to_mdp5_data(mfd);
	if (mdp5_data) {
		if (notification_type == NOTIFY_TYPE_BL_AD_ATTEN_UPDATE) {
			mdp5_data->ad_bl_events++;
			sysfs_notify_dirent(mdp5_data->ad_bl_event_sd);
		} else if (notification_type == NOTIFY_TYPE_BL_UPDATE) {
			mdp5_data->bl_events++;
			sysfs_notify_dirent(mdp5_data->bl_event_sd);
		}
	}
}

static int mdss_fb_notify_update(struct msm_fb_data_type *mfd,
							unsigned long *argp)
{
	int ret;
	unsigned int notify = 0x0, to_user = 0x0;

	ret = copy_from_user(&notify, argp, sizeof(unsigned int));
	if (ret) {
		pr_err("%s:ioctl failed\n", __func__);
		return ret;
	}

	if (notify > NOTIFY_UPDATE_POWER_OFF)
		return -EINVAL;

	if (notify == NOTIFY_UPDATE_INIT) {
		mutex_lock(&mfd->update.lock);
		mfd->update.init_done = true;
		mutex_unlock(&mfd->update.lock);
		ret = 1;
	} else if (notify == NOTIFY_UPDATE_DEINIT) {
		mutex_lock(&mfd->update.lock);
		mfd->update.init_done = false;
		mutex_unlock(&mfd->update.lock);
		complete(&mfd->update.comp);
		complete(&mfd->no_update.comp);
		ret = 1;
	} else if (mfd->update.is_suspend) {
		to_user = NOTIFY_TYPE_SUSPEND;
		mfd->update.is_suspend = 0;
		ret = 1;
	} else if (notify == NOTIFY_UPDATE_START) {
		mutex_lock(&mfd->update.lock);
		if (mfd->update.init_done)
			reinit_completion(&mfd->update.comp);
		else {
			mutex_unlock(&mfd->update.lock);
			pr_err("notify update start called without init\n");
			return -EINVAL;
		}
		mfd->update.ref_count++;
		mutex_unlock(&mfd->update.lock);
		ret = wait_for_completion_interruptible_timeout(
						&mfd->update.comp, 4 * HZ);
		mutex_lock(&mfd->update.lock);
		mfd->update.ref_count--;
		mutex_unlock(&mfd->update.lock);
		to_user = (unsigned int)mfd->update.value;
		if (mfd->update.type == NOTIFY_TYPE_SUSPEND) {
			to_user = (unsigned int)mfd->update.type;
			ret = 1;
		}
	} else if (notify == NOTIFY_UPDATE_STOP) {
		mutex_lock(&mfd->update.lock);
		if (mfd->update.init_done) {
			mutex_unlock(&mfd->update.lock);
			mutex_lock(&mfd->no_update.lock);
			reinit_completion(&mfd->no_update.comp);
		} else {
			mutex_unlock(&mfd->update.lock);
			pr_err("notify update stop called without init\n");
			return -EINVAL;
		}
		mfd->no_update.ref_count++;
		mutex_unlock(&mfd->no_update.lock);
		ret = wait_for_completion_interruptible_timeout(
						&mfd->no_update.comp, 4 * HZ);
		mutex_lock(&mfd->no_update.lock);
		mfd->no_update.ref_count--;
		mutex_unlock(&mfd->no_update.lock);
		to_user = (unsigned int)mfd->no_update.value;
	} else {
		if (mdss_fb_is_power_on(mfd)) {
			reinit_completion(&mfd->power_off_comp);
			ret = wait_for_completion_interruptible_timeout(
						&mfd->power_off_comp, 1 * HZ);
		}
	}

	if (ret == 0)
		ret = -ETIMEDOUT;
	else if (ret > 0)
		ret = copy_to_user(argp, &to_user, sizeof(unsigned int));
	return ret;
}

static int lcd_backlight_registered;

static void mdss_fb_set_bl_brightness(struct led_classdev *led_cdev,
				      enum led_brightness value)
{
	struct msm_fb_data_type *mfd = dev_get_drvdata(led_cdev->dev->parent);
	u64 bl_lvl;

	if (mfd->boot_notification_led) {
		led_trigger_event(mfd->boot_notification_led, 0);
		mfd->boot_notification_led = NULL;
	}

	if (value > mfd->panel_info->brightness_max)
		value = mfd->panel_info->brightness_max;

	/* This maps android backlight level 0 to 255 into
	 * driver backlight level 0 to bl_max with rounding
	 */
	MDSS_BRIGHT_TO_BL(bl_lvl, value, mfd->panel_info->bl_max,
				mfd->panel_info->brightness_max);

	if (!bl_lvl && value)
		bl_lvl = 1;

	if (!IS_CALIB_MODE_BL(mfd) && (!mfd->ext_bl_ctrl || !value ||
							!mfd->bl_level)) {
		mutex_lock(&mfd->bl_lock);
		mdss_fb_set_backlight(mfd, bl_lvl);
		mutex_unlock(&mfd->bl_lock);
	}
	mfd->bl_level_usr = bl_lvl;
}

static enum led_brightness mdss_fb_get_bl_brightness(
	struct led_classdev *led_cdev)
{
	struct msm_fb_data_type *mfd = dev_get_drvdata(led_cdev->dev->parent);
	u64 value;

	MDSS_BL_TO_BRIGHT(value, mfd->bl_level_usr, mfd->panel_info->bl_max,
			  mfd->panel_info->brightness_max);

	return value;
}

static struct led_classdev backlight_led = {
	.name           = "lcd-backlight",
	.brightness     = MDSS_MAX_BL_BRIGHTNESS / 2,
	.brightness_set = mdss_fb_set_bl_brightness,
	.brightness_get = mdss_fb_get_bl_brightness,
	.max_brightness = MDSS_MAX_BL_BRIGHTNESS,
};

static ssize_t msm_fb_type_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;

	switch (mfd->panel.type) {
	case NO_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "no panel\n");
		break;
	case HDMI_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "hdmi panel\n");
		break;
	case LVDS_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "lvds panel\n");
		break;
	case DTV_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "dtv panel\n");
		break;
	case MIPI_VIDEO_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "mipi dsi video panel\n");
		break;
	case MIPI_CMD_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "mipi dsi cmd panel\n");
		break;
	case WRITEBACK_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "writeback panel\n");
		break;
	case EDP_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "edp panel\n");
		break;
	case SPI_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "spi panel\n");
		break;
	case RGB_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "rgb panel\n");
		break;
	case DP_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "dp panel\n");
		break;
	default:
		ret = snprintf(buf, PAGE_SIZE, "unknown panel\n");
		break;
	}

	return ret;
}

static int mdss_fb_get_panel_xres(struct mdss_panel_info *pinfo)
{
	struct mdss_panel_data *pdata;
	int xres;

	pdata = container_of(pinfo, struct mdss_panel_data, panel_info);

	xres = pinfo->xres;
	if (pdata->next && pdata->next->active)
		xres += mdss_fb_get_panel_xres(&pdata->next->panel_info);
	if (pinfo->split_link_enabled)
		xres = xres * pinfo->mipi.num_of_sublinks;
	return xres;
}

static inline int mdss_fb_validate_split(int left, int right,
			struct msm_fb_data_type *mfd)
{
	int rc = -EINVAL;
	u32 panel_xres = mdss_fb_get_panel_xres(mfd->panel_info);

	pr_debug("%pS: split_mode = %d left=%d right=%d panel_xres=%d\n",
		__builtin_return_address(0), mfd->split_mode,
		left, right, panel_xres);

	/* more validate condition could be added if needed */
	if (left && right) {
		if (panel_xres == left + right) {
			mfd->split_fb_left = left;
			mfd->split_fb_right = right;
			rc = 0;
		}
	} else {
		if (mfd->split_mode == MDP_DUAL_LM_DUAL_DISPLAY) {
			mfd->split_fb_left = mfd->panel_info->xres;
			mfd->split_fb_right = panel_xres - mfd->split_fb_left;
			rc = 0;
		} else {
			mfd->split_fb_left = mfd->split_fb_right = 0;
		}
	}

	return rc;
}

static ssize_t msm_fb_split_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int data[2] = {0};
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;

	if (sscanf(buf, "%d %d", &data[0], &data[1]) != 2)
		pr_debug("Not able to read split values\n");
	else if (!mdss_fb_validate_split(data[0], data[1], mfd))
		pr_debug("split left=%d right=%d\n", data[0], data[1]);

	return len;
}

static ssize_t msm_fb_split_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;

	ret = snprintf(buf, PAGE_SIZE, "%d %d\n",
		       mfd->split_fb_left, mfd->split_fb_right);
	return ret;
}

static void mdss_fb_get_split(struct msm_fb_data_type *mfd)
{
	if ((mfd->split_mode == MDP_SPLIT_MODE_NONE) &&
	    (mfd->split_fb_left && mfd->split_fb_right))
		mfd->split_mode = MDP_DUAL_LM_SINGLE_DISPLAY;

	pr_debug("split fb%d left=%d right=%d mode=%d\n", mfd->index,
		mfd->split_fb_left, mfd->split_fb_right, mfd->split_mode);
}

static ssize_t msm_fb_src_split_info_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = fbi->par;

	if (is_split_lm(mfd) && (fbi->var.yres > fbi->var.xres)) {
		pr_debug("always split mode enabled\n");
		ret = scnprintf(buf, PAGE_SIZE,
			"src_split_always\n");
	}

	return ret;
}

static ssize_t msm_fb_thermal_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = fbi->par;
	int ret;

	ret = scnprintf(buf, PAGE_SIZE, "thermal_level=%d\n",
						mfd->thermal_level);

	return ret;
}

static ssize_t msm_fb_thermal_level_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = fbi->par;
	int rc = 0;
	int thermal_level = 0;

	rc = kstrtoint(buf, 10, &thermal_level);
	if (rc) {
		pr_err("kstrtoint failed. rc=%d\n", rc);
		return rc;
	}

	pr_debug("Thermal level set to %d\n", thermal_level);
	mfd->thermal_level = thermal_level;
	sysfs_notify(&mfd->fbi->dev->kobj, NULL, "msm_fb_thermal_level");

	return count;
}

static ssize_t show_blank_event_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	int ret;

	pr_debug("fb%d panel_power_state = %d\n", mfd->index,
		mfd->panel_power_state);
	ret = scnprintf(buf, PAGE_SIZE, "panel_power_on = %d\n",
						mfd->panel_power_state);

	return ret;
}

static void __mdss_fb_idle_notify_work(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct msm_fb_data_type *mfd = container_of(dw, struct msm_fb_data_type,
		idle_notify_work);

	/* Notify idle-ness here */
	pr_debug("Idle timeout %dms expired!\n", mfd->idle_time);
	if (mfd->idle_time)
		sysfs_notify(&mfd->fbi->dev->kobj, NULL, "idle_notify");
	mfd->idle_state = MDSS_FB_IDLE;
}


static ssize_t measured_fps_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = fbi->par;
	u64 fps_int, fps_float;

	if (mfd->panel_power_state != MDSS_PANEL_POWER_ON)
		mfd->fps_info.measured_fps = 0;
	fps_int = (u64) mfd->fps_info.measured_fps;
	fps_float = do_div(fps_int, 10);
	return scnprintf(buf, PAGE_SIZE, "%llu.%llu\n", fps_int, fps_float);

}

static ssize_t idle_time_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = fbi->par;
	int ret;

	ret = scnprintf(buf, PAGE_SIZE, "%d", mfd->idle_time);

	return ret;
}

static ssize_t idle_time_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = fbi->par;
	int rc = 0;
	int idle_time = 0;

	rc = kstrtoint(buf, 10, &idle_time);
	if (rc) {
		pr_err("kstrtoint failed. rc=%d\n", rc);
		return rc;
	}

	pr_debug("Idle time = %d\n", idle_time);
	mfd->idle_time = idle_time;

	return count;
}

static ssize_t idle_notify_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = fbi->par;
	int ret;

	ret = scnprintf(buf, PAGE_SIZE, "%s",
		work_busy(&mfd->idle_notify_work.work) ? "no" : "yes");

	return ret;
}

static ssize_t msm_fb_panel_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = fbi->par;
	struct mdss_panel_info *pinfo = mfd->panel_info;
	int ret;
	bool dfps_porch_mode = false;

	if (pinfo->dfps_update == DFPS_IMMEDIATE_PORCH_UPDATE_MODE_HFP ||
		pinfo->dfps_update == DFPS_IMMEDIATE_PORCH_UPDATE_MODE_VFP)
		dfps_porch_mode = true;

	ret = scnprintf(buf, PAGE_SIZE,
			"pu_en=%d\nxstart=%d\nwalign=%d\nystart=%d\nhalign=%d\n"
			"min_w=%d\nmin_h=%d\nroi_merge=%d\ndyn_fps_en=%d\n"
			"min_fps=%d\nmax_fps=%d\npanel_name=%s\n"
			"primary_panel=%d\nis_pluggable=%d\ndisplay_id=%s\n"
			"is_cec_supported=%d\nis_pingpong_split=%d\n"
			"dfps_porch_mode=%d\npu_roi_cnt=%d\ndual_dsi=%d\n"
			"is_hdr_enabled=%d\npeak_brightness=%d\n"
			"blackness_level=%d\naverage_brightness=%d\n"
			"white_chromaticity_x=%d\nwhite_chromaticity_y=%d\n"
			"red_chromaticity_x=%d\nred_chromaticity_y=%d\n"
			"green_chromaticity_x=%d\ngreen_chromaticity_y=%d\n"
			"blue_chromaticity_x=%d\nblue_chromaticity_y=%d\n"
			"panel_orientation=%d\ndyn_bitclk_en=%d\n",
			pinfo->partial_update_enabled,
			pinfo->roi_alignment.xstart_pix_align,
			pinfo->roi_alignment.width_pix_align,
			pinfo->roi_alignment.ystart_pix_align,
			pinfo->roi_alignment.height_pix_align,
			pinfo->roi_alignment.min_width,
			pinfo->roi_alignment.min_height,
			pinfo->partial_update_roi_merge,
			pinfo->dynamic_fps, pinfo->min_fps, pinfo->max_fps,
			pinfo->panel_name, pinfo->is_prim_panel,
			pinfo->is_pluggable, pinfo->display_id,
			pinfo->is_cec_supported, is_pingpong_split(mfd),
			dfps_porch_mode, pinfo->partial_update_enabled,
			is_panel_split(mfd), pinfo->hdr_properties.hdr_enabled,
			pinfo->hdr_properties.peak_brightness,
			pinfo->hdr_properties.blackness_level,
			pinfo->hdr_properties.avg_brightness,
			pinfo->hdr_properties.display_primaries[0],
			pinfo->hdr_properties.display_primaries[1],
			pinfo->hdr_properties.display_primaries[2],
			pinfo->hdr_properties.display_primaries[3],
			pinfo->hdr_properties.display_primaries[4],
			pinfo->hdr_properties.display_primaries[5],
			pinfo->hdr_properties.display_primaries[6],
			pinfo->hdr_properties.display_primaries[7],
			pinfo->panel_orientation, pinfo->dynamic_bitclk);

	return ret;
}

static ssize_t msm_fb_panel_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = fbi->par;
	int ret;
	int panel_status;

	if (mdss_panel_is_power_off(mfd->panel_power_state)) {
		ret = scnprintf(buf, PAGE_SIZE, "panel_status=%s\n", "suspend");
	} else {
		panel_status = mdss_fb_send_panel_event(mfd,
				MDSS_EVENT_DSI_PANEL_STATUS, mfd);
		ret = scnprintf(buf, PAGE_SIZE, "panel_status=%s\n",
			panel_status > 0 ? "alive" : "dead");
	}

	return ret;
}

static ssize_t msm_fb_panel_status_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	struct mdss_panel_data *pdata;

	pdata = dev_get_platdata(&mfd->pdev->dev);
	if (!pdata) {
		pr_err("no panel connected!\n");
		return len;
	}

	if (kstrtouint(buf, 0, &pdata->panel_info.panel_force_dead))
		pr_err("kstrtouint buf error!\n");

	return len;
}

/*
 * mdss_fb_blanking_mode_switch() - Function triggers dynamic mode switch
 * @mfd:	Framebuffer data structure for display
 * @mode:	Enabled/Disable LowPowerMode
 *		1: Enter into LowPowerMode
 *		0: Exit from LowPowerMode
 *
 * This Function dynamically switches to and from video mode. This
 * switch involves the panel turning off backlight during trantision.
 */
static int mdss_fb_blanking_mode_switch(struct msm_fb_data_type *mfd, int mode)
{
	int ret = 0;
	u32 bl_lvl = 0;
	struct mdss_panel_info *pinfo = NULL;
	struct mdss_panel_data *pdata;

	if (!mfd || !mfd->panel_info)
		return -EINVAL;

	pinfo = mfd->panel_info;

	if (!pinfo->mipi.dms_mode) {
		pr_warn("Panel does not support dynamic switch!\n");
		return 0;
	}

	if (mode == pinfo->mipi.mode) {
		pr_debug("Already in requested mode!\n");
		return 0;
	}
	pr_debug("Enter mode: %d\n", mode);

	pdata = dev_get_platdata(&mfd->pdev->dev);

	pdata->panel_info.dynamic_switch_pending = true;
	ret = mdss_fb_pan_idle(mfd);
	if (ret) {
		pr_err("mdss_fb_pan_idle for fb%d failed. ret=%d\n",
			mfd->index, ret);
		pdata->panel_info.dynamic_switch_pending = false;
		return ret;
	}

	mutex_lock(&mfd->bl_lock);
	bl_lvl = mfd->bl_level;
	mdss_fb_set_backlight(mfd, 0);
	mutex_unlock(&mfd->bl_lock);

	lock_fb_info(mfd->fbi);
	ret = mdss_fb_blank_sub(FB_BLANK_POWERDOWN, mfd->fbi,
						mfd->op_enable);
	if (ret) {
		pr_err("can't turn off display!\n");
		unlock_fb_info(mfd->fbi);
		return ret;
	}

	mfd->op_enable = false;

	ret = mfd->mdp.configure_panel(mfd, mode, 1);
	mdss_fb_set_mdp_sync_pt_threshold(mfd, mfd->panel.type);

	mfd->op_enable = true;

	ret = mdss_fb_blank_sub(FB_BLANK_UNBLANK, mfd->fbi,
					mfd->op_enable);
	if (ret) {
		pr_err("can't turn on display!\n");
		unlock_fb_info(mfd->fbi);
		return ret;
	}
	unlock_fb_info(mfd->fbi);

	mutex_lock(&mfd->bl_lock);
	mfd->allow_bl_update = true;
	mdss_fb_set_backlight(mfd, bl_lvl);
	mutex_unlock(&mfd->bl_lock);

	pdata->panel_info.dynamic_switch_pending = false;
	pdata->panel_info.is_lpm_mode = mode ? 1 : 0;

	if (ret) {
		pr_err("can't turn on display!\n");
		return ret;
	}

	pr_debug("Exit mode: %d\n", mode);

	return 0;
}

static ssize_t msm_fb_dfps_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	struct mdss_panel_data *pdata;
	struct mdss_panel_info *pinfo;
	u32 dfps_mode;

	pdata = dev_get_platdata(&mfd->pdev->dev);
	if (!pdata) {
		pr_err("no panel connected!\n");
		return len;
	}
	pinfo = &pdata->panel_info;

	if (kstrtouint(buf, 0, &dfps_mode)) {
		pr_err("kstrtouint buf error!\n");
		return len;
	}

	if (dfps_mode >= DFPS_MODE_MAX) {
		pinfo->dynamic_fps = false;
		return len;
	}

	if (mfd->idle_time != 0) {
		pr_err("ERROR: Idle time is not disabled.\n");
		return len;
	}

	if (pinfo->current_fps != pinfo->default_fps) {
		pr_err("ERROR: panel not configured to default fps\n");
		return len;
	}

	pinfo->dynamic_fps = true;
	pinfo->dfps_update = dfps_mode;

	if (pdata->next)
		pdata->next->panel_info.dfps_update = dfps_mode;

	return len;
}

static ssize_t msm_fb_dfps_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	struct mdss_panel_data *pdata;
	struct mdss_panel_info *pinfo;
	int ret;

	pdata = dev_get_platdata(&mfd->pdev->dev);
	if (!pdata) {
		pr_err("no panel connected!\n");
		return -EINVAL;
	}
	pinfo = &pdata->panel_info;

	ret = scnprintf(buf, PAGE_SIZE, "dfps enabled=%d mode=%d\n",
		pinfo->dynamic_fps, pinfo->dfps_update);

	return ret;
}

static ssize_t msm_fb_persist_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	struct mdss_panel_info *pinfo = NULL;
	struct mdss_panel_data *pdata;
	int ret = 0;
	u32 persist_mode;

	if (!mfd || !mfd->panel_info) {
		pr_err("%s: Panel info is NULL!\n", __func__);
		return len;
	}

	pinfo = mfd->panel_info;

	if (kstrtouint(buf, 0, &persist_mode)) {
		pr_err("kstrtouint buf error!\n");
		return len;
	}

	mutex_lock(&mfd->mdss_sysfs_lock);
	if (mdss_panel_is_power_off(mfd->panel_power_state)) {
		pinfo->persist_mode = persist_mode;
		goto end;
	}

	mutex_lock(&mfd->bl_lock);

	pdata = dev_get_platdata(&mfd->pdev->dev);
	if ((pdata) && (pdata->apply_display_setting))
		ret = pdata->apply_display_setting(pdata, persist_mode);

	mutex_unlock(&mfd->bl_lock);

	if (!ret) {
		pr_debug("%s: Persist mode %d\n", __func__, persist_mode);
		pinfo->persist_mode = persist_mode;
	}

end:
	mutex_unlock(&mfd->mdss_sysfs_lock);

	return len;
}

static ssize_t msm_fb_persist_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;
	struct mdss_panel_data *pdata;
	struct mdss_panel_info *pinfo;
	int ret;

	pdata = dev_get_platdata(&mfd->pdev->dev);
	if (!pdata) {
		pr_err("no panel connected!\n");
		return -EINVAL;
	}
	pinfo = &pdata->panel_info;

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", pinfo->persist_mode);

	return ret;
}

static ssize_t idle_power_collapse_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "idle power collapsed\n");
}

static DEVICE_ATTR_RO(msm_fb_type);
static DEVICE_ATTR_RW(msm_fb_split);
static DEVICE_ATTR_RO(show_blank_event);
static DEVICE_ATTR_RW(idle_time);
static DEVICE_ATTR_RO(idle_notify);
static DEVICE_ATTR_RO(msm_fb_panel_info);
static DEVICE_ATTR_RO(msm_fb_src_split_info);
static DEVICE_ATTR_RW(msm_fb_thermal_level);
static DEVICE_ATTR_RW(msm_fb_panel_status);
static DEVICE_ATTR_RW(msm_fb_dfps_mode);
static DEVICE_ATTR_RO(measured_fps);
static DEVICE_ATTR_RW(msm_fb_persist_mode);
static DEVICE_ATTR_RO(idle_power_collapse);

static struct attribute *mdss_fb_attrs[] = {
	&dev_attr_msm_fb_type.attr,
	&dev_attr_msm_fb_split.attr,
	&dev_attr_show_blank_event.attr,
	&dev_attr_idle_time.attr,
	&dev_attr_idle_notify.attr,
	&dev_attr_msm_fb_panel_info.attr,
	&dev_attr_msm_fb_src_split_info.attr,
	&dev_attr_msm_fb_thermal_level.attr,
	&dev_attr_msm_fb_panel_status.attr,
	&dev_attr_msm_fb_dfps_mode.attr,
	&dev_attr_measured_fps.attr,
	&dev_attr_msm_fb_persist_mode.attr,
	&dev_attr_idle_power_collapse.attr,
	NULL,
};

static struct attribute_group mdss_fb_attr_group = {
	.attrs = mdss_fb_attrs,
};

static int mdss_fb_create_sysfs(struct msm_fb_data_type *mfd)
{
	int rc;

	rc = sysfs_create_group(&mfd->fbi->dev->kobj, &mdss_fb_attr_group);
	if (rc)
		pr_err("sysfs group creation failed, rc=%d\n", rc);
	return rc;
}

static void mdss_fb_remove_sysfs(struct msm_fb_data_type *mfd)
{
	sysfs_remove_group(&mfd->fbi->dev->kobj, &mdss_fb_attr_group);
}

static void mdss_fb_shutdown(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd = platform_get_drvdata(pdev);

	mfd->shutdown_pending = true;

	/* wake up threads waiting on idle or kickoff queues */
	wake_up_all(&mfd->idle_wait_q);
	wake_up_all(&mfd->kickoff_wait_q);

	lock_fb_info(mfd->fbi);
	mdss_fb_release_all(mfd->fbi, true);
	sysfs_notify(&mfd->fbi->dev->kobj, NULL, "show_blank_event");
	unlock_fb_info(mfd->fbi);
}

static void mdss_fb_input_event_handler(struct input_handle *handle,
				    unsigned int type,
				    unsigned int code,
				    int value)
{
	struct msm_fb_data_type *mfd = handle->handler->private;
	int rc;

	if ((type != EV_ABS) || !mdss_fb_is_power_on(mfd))
		return;

	if (mfd->mdp.input_event_handler) {
		rc = mfd->mdp.input_event_handler(mfd);
		if (rc)
			pr_err("mdp input event handler failed\n");
	}
}

static int mdss_fb_input_connect(struct input_handler *handler,
			     struct input_dev *dev,
			     const struct input_device_id *id)
{
	int rc;
	struct input_handle *handle;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = handler->name;

	rc = input_register_handle(handle);
	if (rc) {
		pr_err("failed to register input handle, rc = %d\n", rc);
		goto error;
	}

	rc = input_open_device(handle);
	if (rc) {
		pr_err("failed to open input device, rc = %d\n", rc);
		goto error_unregister;
	}

	return 0;

error_unregister:
	input_unregister_handle(handle);
error:
	kfree(handle);
	return rc;
}

static void mdss_fb_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

/*
 * Structure for specifying event parameters on which to receive callbacks.
 * This structure will trigger a callback in case of a touch event (specified by
 * EV_ABS) where there is a change in X and Y coordinates,
 */
static const struct input_device_id mdss_fb_input_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
				BIT_MASK(ABS_MT_POSITION_X) |
				BIT_MASK(ABS_MT_POSITION_Y) },
	},
	{ },
};

static int mdss_fb_register_input_handler(struct msm_fb_data_type *mfd)
{
	int rc;
	struct input_handler *handler;

	if (mfd->input_handler)
		return -EINVAL;

	handler = kzalloc(sizeof(*handler), GFP_KERNEL);
	if (!handler)
		return -ENOMEM;

	handler->event = mdss_fb_input_event_handler;
	handler->connect = mdss_fb_input_connect;
	handler->disconnect = mdss_fb_input_disconnect,
	handler->name = "mdss_fb",
	handler->id_table = mdss_fb_input_ids;
	handler->private = mfd;

	rc = input_register_handler(handler);
	if (rc) {
		pr_err("Unable to register the input handler\n");
		kfree(handler);
	} else {
		mfd->input_handler = handler;
	}

	return rc;
}

static void mdss_fb_unregister_input_handler(struct msm_fb_data_type *mfd)
{
	if (!mfd->input_handler)
		return;

	input_unregister_handler(mfd->input_handler);
	kfree(mfd->input_handler);
}

static void mdss_fb_videomode_from_panel_timing(struct fb_videomode *videomode,
		struct mdss_panel_timing *pt)
{
	videomode->name = pt->name;
	videomode->xres = pt->xres;
	videomode->yres = pt->yres;
	videomode->left_margin = pt->h_back_porch;
	videomode->right_margin = pt->h_front_porch;
	videomode->hsync_len = pt->h_pulse_width;
	videomode->upper_margin = pt->v_back_porch;
	videomode->lower_margin = pt->v_front_porch;
	videomode->vsync_len = pt->v_pulse_width;
	videomode->refresh = pt->frame_rate;
	videomode->flag = 0;
	videomode->vmode = 0;
	videomode->sync = 0;

	if (videomode->refresh) {
		unsigned long clk_rate, h_total, v_total;

		h_total = videomode->xres + videomode->left_margin
			+ videomode->right_margin + videomode->hsync_len;
		v_total = videomode->yres + videomode->lower_margin
			+ videomode->upper_margin + videomode->vsync_len;
		clk_rate = h_total * v_total * videomode->refresh;
		videomode->pixclock =
			KHZ2PICOS(clk_rate / 1000);
	} else {
		videomode->pixclock =
			KHZ2PICOS((unsigned long)pt->clk_rate / 1000);
	}
}

static void mdss_fb_set_split_mode(struct msm_fb_data_type *mfd,
		struct mdss_panel_data *pdata)
{
	if (pdata->panel_info.is_split_display) {
		struct mdss_panel_data *pnext = pdata->next;

		mfd->split_fb_left = pdata->panel_info.lm_widths[0];
		if (pnext)
			mfd->split_fb_right = pnext->panel_info.lm_widths[0];

		if (pdata->panel_info.use_pingpong_split)
			mfd->split_mode = MDP_PINGPONG_SPLIT;
		else
			mfd->split_mode = MDP_DUAL_LM_DUAL_DISPLAY;
	} else if ((pdata->panel_info.lm_widths[0] != 0)
			&& (pdata->panel_info.lm_widths[1] != 0)) {
		mfd->split_fb_left = pdata->panel_info.lm_widths[0];
		mfd->split_fb_right = pdata->panel_info.lm_widths[1];
		mfd->split_mode = MDP_DUAL_LM_SINGLE_DISPLAY;
	} else {
		mfd->split_mode = MDP_SPLIT_MODE_NONE;
	}
}

static int mdss_fb_init_panel_modes(struct msm_fb_data_type *mfd,
		struct mdss_panel_data *pdata)
{
	struct fb_info *fbi = mfd->fbi;
	struct fb_videomode *modedb;
	struct mdss_panel_timing *pt;
	struct list_head *pos;
	int num_timings = 0;
	int i = 0;

	/* check if multiple modes are supported */
	if (!pdata->timings_list.prev || !pdata->timings_list.next)
		INIT_LIST_HEAD(&pdata->timings_list);

	if (!fbi || !pdata->current_timing || list_empty(&pdata->timings_list))
		return 0;

	list_for_each(pos, &pdata->timings_list)
		num_timings++;

	modedb = devm_kzalloc(fbi->dev, num_timings * sizeof(*modedb),
			GFP_KERNEL);
	if (!modedb)
		return -ENOMEM;

	list_for_each_entry(pt, &pdata->timings_list, list) {
		struct mdss_panel_timing *spt = NULL;

		mdss_fb_videomode_from_panel_timing(modedb + i, pt);
		if (pdata->next) {
			spt = mdss_panel_get_timing_by_name(pdata->next,
					modedb[i].name);
			/* for split config, recalculate xres and pxl clock */
			if (!IS_ERR_OR_NULL(spt)) {
				unsigned long pclk, h_total, v_total;

				modedb[i].xres += spt->xres;
				h_total = modedb[i].xres +
					modedb[i].left_margin +
					modedb[i].right_margin +
					modedb[i].hsync_len;
				v_total = modedb[i].yres +
					modedb[i].lower_margin +
					modedb[i].upper_margin +
					modedb[i].vsync_len;
				pclk = h_total * v_total * modedb[i].refresh;
				modedb[i].pixclock = KHZ2PICOS(pclk / 1000);
			} else {
				pr_debug("no matching split config for %s\n",
						modedb[i].name);
			}

			/*
			 * if no panel timing found for current, need to
			 * disable it otherwise mark it as active
			 */
			if (pt == pdata->current_timing)
				pdata->next->active = !IS_ERR_OR_NULL(spt);
		}

		if (pt == pdata->current_timing) {
			pr_debug("found current mode: %s\n", pt->name);
			fbi->mode = modedb + i;
		}
		i++;
	}

	fbi->monspecs.modedb = modedb;
	fbi->monspecs.modedb_len = num_timings;

	/* destroy and recreate modelist */
	fb_destroy_modelist(&fbi->modelist);

	if (fbi->mode)
		fb_videomode_to_var(&fbi->var, fbi->mode);
	fb_videomode_to_modelist(modedb, num_timings, &fbi->modelist);

	return 0;
}

static int mdss_fb_probe(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd = NULL;
	struct mdss_panel_data *pdata;
	struct fb_info *fbi;
#ifndef CONFIG_FB_MSM_MDP_NONE
	struct mdss_overlay_private *mdp5_data = NULL;
#endif
	int rc;

	if (fbi_list_index >= MAX_FBI_LIST)
		return -ENOMEM;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata)
		return -EPROBE_DEFER;

	if (!mdp_instance) {
		pr_err("mdss mdp resource not initialized yet\n");
		return -ENODEV;
	}

	/*
	 * alloc framebuffer info + par data
	 */
	fbi = framebuffer_alloc(sizeof(struct msm_fb_data_type), NULL);
	if (fbi == NULL) {
		pr_err("can't allocate framebuffer info data!\n");
		return -ENOMEM;
	}

	mfd = (struct msm_fb_data_type *)fbi->par;
	mfd->key = MFD_KEY;
	mfd->fbi = fbi;
	mfd->panel_info = &pdata->panel_info;
	mfd->panel.type = pdata->panel_info.type;
	mfd->panel.id = mfd->index;
	mfd->fb_page = MDSS_FB_NUM;
	mfd->index = fbi_list_index;
	mfd->mdp_fb_page_protection = MDP_FB_PAGE_PROTECTION_WRITECOMBINE;

	mfd->ext_ad_ctrl = -1;
	if (mfd->panel_info && mfd->panel_info->brightness_max > 0)
		MDSS_BRIGHT_TO_BL(mfd->bl_level, backlight_led.brightness,
		mfd->panel_info->bl_max, mfd->panel_info->brightness_max);
	else
		mfd->bl_level = 0;

	mfd->bl_scale = 1024;
	mfd->ad_bl_level = 0;
	mfd->fb_imgType = MDP_RGBA_8888;
	mfd->calib_mode_bl = 0;
	mfd->unset_bl_level = U32_MAX;
	mfd->bl_extn_level = U64_MAX;
	mfd->bl_level_usr = backlight_led.brightness;

	mfd->pdev = pdev;

	if (mfd->panel.type == SPI_PANEL || mfd->panel.type == EBI2_PANEL)
		mfd->fb_imgType = MDP_RGB_565;

	mfd->split_fb_left = mfd->split_fb_right = 0;

	mdss_fb_set_split_mode(mfd, pdata);
	pr_info("fb%d: split_mode:%d left:%d right:%d\n", mfd->index,
		mfd->split_mode, mfd->split_fb_left, mfd->split_fb_right);

	mfd->mdp = *mdp_instance;

	rc = of_property_read_bool(pdev->dev.of_node,
		"qcom,boot-indication-enabled");

	if (rc) {
		led_trigger_register_simple("boot-indication",
			&(mfd->boot_notification_led));
	}

	INIT_LIST_HEAD(&mfd->file_list);

	mutex_init(&mfd->bl_lock);
	mutex_init(&mfd->mdss_sysfs_lock);
	mutex_init(&mfd->switch_lock);

	fbi_list[fbi_list_index++] = fbi;

	platform_set_drvdata(pdev, mfd);

	rc = mdss_fb_register(mfd);
	if (rc)
		return rc;

	mdss_fb_create_sysfs(mfd);
	mdss_fb_send_panel_event(mfd, MDSS_EVENT_FB_REGISTERED, fbi);

	if (mfd->mdp.init_fnc) {
		rc = mfd->mdp.init_fnc(mfd);
		if (rc) {
			pr_err("init_fnc failed\n");
			return rc;
		}
	}
	mdss_fb_init_fps_info(mfd);

	rc = pm_runtime_set_active(mfd->fbi->dev);
	if (rc < 0)
		pr_err("pm_runtime: fail to set active.\n");
	pm_runtime_enable(mfd->fbi->dev);

	/* android supports only one lcd-backlight/lcd for now */
	if (!lcd_backlight_registered) {
		backlight_led.brightness = mfd->panel_info->brightness_max;
		backlight_led.max_brightness = mfd->panel_info->brightness_max;
		if (led_classdev_register(&pdev->dev, &backlight_led))
			pr_err("led_classdev_register failed\n");
		else
			lcd_backlight_registered = 1;
	}

	mdss_fb_init_panel_modes(mfd, pdata);

#ifndef CONFIG_FB_MSM_MDP_NONE
	mfd->mdp_sync_pt_data.fence_name = "mdp-fence";
	if (mfd->mdp_sync_pt_data.timeline == NULL) {
		char timeline_name[32];

		snprintf(timeline_name, sizeof(timeline_name),
			"mdss_fb_%d", mfd->index);
		 mfd->mdp_sync_pt_data.timeline =
				mdss_create_timeline(timeline_name);
		if (mfd->mdp_sync_pt_data.timeline == NULL) {
			pr_err("cannot create release fence time line\n");
			return -ENOMEM;
		}
		mfd->mdp_sync_pt_data.notifier.notifier_call =
			__mdss_fb_sync_buf_done_callback;

		/* Initialize CWB notifier callback */
		mdp5_data = mfd_to_mdp5_data(mfd);
		if (test_bit(MDSS_CAPS_CWB_SUPPORTED,
					mdp5_data->mdata->mdss_caps_map))
			mdp5_data->cwb.cwb_sync_pt_data.notifier.notifier_call =
				__mdss_fb_sync_buf_done_callback;
	}

	mdss_fb_set_mdp_sync_pt_threshold(mfd, mfd->panel.type);

	if (mfd->mdp.splash_init_fnc)
		mfd->mdp.splash_init_fnc(mfd);

#endif
	/*
	 * Register with input driver for a callback for command mode panels.
	 * When there is an input event, mdp clocks will be turned on to reduce
	 * latency when a frame update happens.
	 * For video mode panels, idle timeout will be delayed so that userspace
	 * does not get an idle event while new frames are expected. In case of
	 * an idle event, user space tries to fall back to GPU composition which
	 * can lead to increased load when there are new frames.
	 */
	if ((mfd->panel_info->type == MIPI_CMD_PANEL) ||
	    (mfd->panel_info->type == MIPI_VIDEO_PANEL))
		if (mdss_fb_register_input_handler(mfd))
			pr_err("failed to register input handler\n");

	INIT_DELAYED_WORK(&mfd->idle_notify_work, __mdss_fb_idle_notify_work);

	return rc;
}

static void mdss_fb_set_mdp_sync_pt_threshold(struct msm_fb_data_type *mfd,
		int type)
{
	if (!mfd)
		return;

	switch (type) {
	case WRITEBACK_PANEL:
		mfd->mdp_sync_pt_data.threshold = 1;
		mfd->mdp_sync_pt_data.retire_threshold = 0;
		break;
	case MIPI_CMD_PANEL:
		mfd->mdp_sync_pt_data.threshold = 1;
		mfd->mdp_sync_pt_data.retire_threshold = 1;
		break;
	default:
		mfd->mdp_sync_pt_data.threshold = 2;
		mfd->mdp_sync_pt_data.retire_threshold = 0;
		break;
	}
}

static int mdss_fb_remove(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;

	mfd = (struct msm_fb_data_type *)platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;

	mdss_fb_remove_sysfs(mfd);

	pm_runtime_disable(mfd->fbi->dev);

	if (mfd->key != MFD_KEY)
		return -EINVAL;

	mdss_fb_unregister_input_handler(mfd);
	mdss_panel_debugfs_cleanup(mfd->panel_info);

	if (mdss_fb_suspend_sub(mfd))
		pr_err("msm_fb_remove: can't stop the device %d\n",
			    mfd->index);

	/* remove /dev/fb* */
	unregister_framebuffer(mfd->fbi);

	if (lcd_backlight_registered) {
		lcd_backlight_registered = 0;
		led_classdev_unregister(&backlight_led);
	}

	return 0;
}

static int mdss_fb_send_panel_event(struct msm_fb_data_type *mfd,
					int event, void *arg)
{
	int ret = 0;
	struct mdss_panel_data *pdata;

	pdata = dev_get_platdata(&mfd->pdev->dev);
	if (!pdata) {
		pr_err("no panel connected\n");
		return -ENODEV;
	}

	pr_debug("sending event=%d for fb%d\n", event, mfd->index);

	do {
		if (pdata->event_handler)
			ret = pdata->event_handler(pdata, event, arg);

		pdata = pdata->next;
	} while (!ret && pdata);

	return ret;
}

static int mdss_fb_suspend_sub(struct msm_fb_data_type *mfd)
{
	int ret = 0;

	if ((!mfd) || (mfd->key != MFD_KEY))
		return 0;

	pr_debug("mdss_fb suspend index=%d\n", mfd->index);

	ret = mdss_fb_pan_idle(mfd);
	if (ret) {
		pr_warn("mdss_fb_pan_idle for fb%d failed. ret=%d\n",
			mfd->index, ret);
		goto exit;
	}

	ret = mdss_fb_send_panel_event(mfd, MDSS_EVENT_SUSPEND, NULL);
	if (ret) {
		pr_warn("unable to suspend fb%d (%d)\n", mfd->index, ret);
		goto exit;
	}

	mfd->suspend.op_enable = mfd->op_enable;
	mfd->suspend.panel_power_state = mfd->panel_power_state;

	if (mfd->op_enable) {
		/*
		 * Ideally, display should have either been blanked by now, or
		 * should have transitioned to a low power state. If not, then
		 * as a fall back option, enter ulp state to leave the display
		 * on, but turn off all interface clocks.
		 */
		if (mdss_fb_is_power_on(mfd)) {
			ret = mdss_fb_blank_sub(BLANK_FLAG_ULP, mfd->fbi,
					mfd->suspend.op_enable);
			if (ret) {
				pr_err("can't turn off display!\n");
				goto exit;
			}
		}
		mfd->op_enable = false;
		fb_set_suspend(mfd->fbi, FBINFO_STATE_SUSPENDED);
	}
exit:
	return ret;
}

static int mdss_fb_resume_sub(struct msm_fb_data_type *mfd)
{
	int ret = 0;

	if ((!mfd) || (mfd->key != MFD_KEY))
		return 0;

	reinit_completion(&mfd->power_set_comp);
	mfd->is_power_setting = true;
	pr_debug("mdss_fb resume index=%d\n", mfd->index);

	ret = mdss_fb_pan_idle(mfd);
	if (ret) {
		pr_warn("mdss_fb_pan_idle for fb%d failed. ret=%d\n",
			mfd->index, ret);
		return ret;
	}

	ret = mdss_fb_send_panel_event(mfd, MDSS_EVENT_RESUME, NULL);
	if (ret) {
		pr_warn("unable to resume fb%d (%d)\n", mfd->index, ret);
		return ret;
	}

	/* resume state var recover */
	mfd->op_enable = mfd->suspend.op_enable;

	/*
	 * If the fb was explicitly blanked or transitioned to ulp during
	 * suspend, then undo it during resume with the appropriate unblank
	 * flag. If fb was in ulp state when entering suspend, then nothing
	 * needs to be done.
	 */
	if (mdss_panel_is_power_on(mfd->suspend.panel_power_state) &&
		!mdss_panel_is_power_on_ulp(mfd->suspend.panel_power_state)) {
		int unblank_flag = mdss_panel_is_power_on_interactive(
			mfd->suspend.panel_power_state) ? FB_BLANK_UNBLANK :
			BLANK_FLAG_LP;

		ret = mdss_fb_blank_sub(unblank_flag, mfd->fbi, mfd->op_enable);
		if (ret)
			pr_warn("can't turn on display!\n");
		else
			fb_set_suspend(mfd->fbi, FBINFO_STATE_RUNNING);
	}
	mfd->is_power_setting = false;
	complete_all(&mfd->power_set_comp);

	return ret;
}

#if defined(CONFIG_PM) && !defined(CONFIG_PM_SLEEP)
static int mdss_fb_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct msm_fb_data_type *mfd = platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;

	dev_dbg(&pdev->dev, "display suspend\n");

	return mdss_fb_suspend_sub(mfd);
}

static int mdss_fb_resume(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd = platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;

	dev_dbg(&pdev->dev, "display resume\n");

	return mdss_fb_resume_sub(mfd);
}
#else
#define mdss_fb_suspend NULL
#define mdss_fb_resume NULL
#endif

#ifdef CONFIG_PM_SLEEP
static int mdss_fb_pm_suspend(struct device *dev)
{
	struct msm_fb_data_type *mfd = dev_get_drvdata(dev);
	int rc = 0;

	if (!mfd)
		return -ENODEV;

	dev_dbg(dev, "display pm suspend\n");

	rc = mdss_fb_suspend_sub(mfd);

	/*
	 * Call MDSS footswitch control to ensure GDSC is
	 * off after pm suspend call. There are cases when
	 * mdss runtime call doesn't trigger even when clock
	 * ref count is zero after fb pm suspend.
	 */
	if (!rc) {
		if (mfd->mdp.footswitch_ctrl)
			mfd->mdp.footswitch_ctrl(false);
	} else {
		pr_err("fb pm suspend failed, rc: %d\n", rc);
	}

	return rc;

}

static int mdss_fb_pm_resume(struct device *dev)
{
	struct msm_fb_data_type *mfd = dev_get_drvdata(dev);

	if (!mfd)
		return -ENODEV;

	dev_dbg(dev, "display pm resume\n");

	/*
	 * It is possible that the runtime status of the fb device may
	 * have been active when the system was suspended. Reset the runtime
	 * status to suspended state after a complete system resume.
	 */
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_enable(dev);

	if (mfd->mdp.footswitch_ctrl)
		mfd->mdp.footswitch_ctrl(true);

	return mdss_fb_resume_sub(mfd);
}
#endif

static const struct dev_pm_ops mdss_fb_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mdss_fb_pm_suspend, mdss_fb_pm_resume)
};

static const struct of_device_id mdss_fb_dt_match[] = {
	{ .compatible = "qcom,mdss-fb",},
	{}
};
EXPORT_COMPAT("qcom,mdss-fb");

static struct platform_driver mdss_fb_driver = {
	.probe = mdss_fb_probe,
	.remove = mdss_fb_remove,
	.suspend = mdss_fb_suspend,
	.resume = mdss_fb_resume,
	.shutdown = mdss_fb_shutdown,
	.driver = {
		.name = "mdss_fb",
		.of_match_table = mdss_fb_dt_match,
		.pm = &mdss_fb_pm_ops,
	},
};

static void mdss_fb_scale_bl(struct msm_fb_data_type *mfd, u32 *bl_lvl)
{
	u32 temp = *bl_lvl;

	pr_debug("input = %d, scale = %d\n", temp, mfd->bl_scale);
	if (temp > mfd->panel_info->bl_max) {
		pr_warn("%s: invalid bl level\n",
				__func__);
		temp = mfd->panel_info->bl_max;
	}
	if (mfd->bl_scale > 1024) {
		pr_warn("%s: invalid bl scale\n",
				__func__);
		mfd->bl_scale = 1024;
	}
	/*
	 * bl_scale is the numerator of
	 * scaling fraction (x/1024)
	 */
	temp = (temp * mfd->bl_scale) / 1024;

	pr_debug("output = %d\n", temp);

	(*bl_lvl) = temp;
}

/* must call this function from within mfd->bl_lock */
void mdss_fb_set_backlight(struct msm_fb_data_type *mfd, u32 bkl_lvl)
{
	struct mdss_panel_data *pdata;
	u32 temp = bkl_lvl;
	bool ad_bl_notify_needed = false;
	bool bl_notify_needed = false;
	bool twm_en = false;

	if ((((mdss_fb_is_power_off(mfd) && mfd->dcm_state != DCM_ENTER)
		|| !mfd->allow_bl_update) && !IS_CALIB_MODE_BL(mfd)) ||
		mfd->panel_info->cont_splash_enabled) {
		mfd->unset_bl_level = bkl_lvl;
		return;
	} else if (mdss_fb_is_power_on(mfd) && mfd->panel_info->panel_dead) {
		mfd->unset_bl_level = mfd->bl_level;
	} else {
		mfd->unset_bl_level = U32_MAX;
	}

	pdata = dev_get_platdata(&mfd->pdev->dev);

	if ((pdata) && (pdata->set_backlight)) {
		if (mfd->mdp.ad_calc_bl)
			(*mfd->mdp.ad_calc_bl)(mfd, temp, &temp,
							&ad_bl_notify_needed);
		if (!IS_CALIB_MODE_BL(mfd))
			mdss_fb_scale_bl(mfd, &temp);
		/*
		 * Even though backlight has been scaled, want to show that
		 * backlight has been set to bkl_lvl to those that read from
		 * sysfs node. Thus, need to set bl_level even if it appears
		 * the backlight has already been set to the level it is at,
		 * as well as setting bl_level to bkl_lvl even though the
		 * backlight has been set to the scaled value.
		 */
		if (mfd->bl_level_scaled == temp) {
			mfd->bl_level = bkl_lvl;
		} else {
			if (mfd->bl_level != bkl_lvl)
				bl_notify_needed = true;
			pr_debug("backlight sent to panel :%d\n", temp);

			if (mfd->mdp.is_twm_en)
				twm_en = mfd->mdp.is_twm_en();

			if (twm_en) {
				pr_info("TWM Enabled skip backlight update\n");
			} else {
				pdata->set_backlight(pdata, temp);
				mfd->bl_level = bkl_lvl;
				mfd->bl_level_scaled = temp;
			}
		}
		if (ad_bl_notify_needed)
			mdss_fb_bl_update_notify(mfd,
				NOTIFY_TYPE_BL_AD_ATTEN_UPDATE);
		if (bl_notify_needed)
			mdss_fb_bl_update_notify(mfd,
				NOTIFY_TYPE_BL_UPDATE);
	}
}

void mdss_fb_update_backlight(struct msm_fb_data_type *mfd)
{
	struct mdss_panel_data *pdata;
	u32 temp;
	bool bl_notify = false;

	if (mfd->unset_bl_level == U32_MAX)
		return;
	mutex_lock(&mfd->bl_lock);
	if (!mfd->allow_bl_update) {
		pdata = dev_get_platdata(&mfd->pdev->dev);
		if ((pdata) && (pdata->set_backlight)) {
			mfd->bl_level = mfd->unset_bl_level;
			temp = mfd->bl_level;
			if (mfd->mdp.ad_calc_bl)
				(*mfd->mdp.ad_calc_bl)(mfd, temp, &temp,
								&bl_notify);
			if (bl_notify)
				mdss_fb_bl_update_notify(mfd,
					NOTIFY_TYPE_BL_AD_ATTEN_UPDATE);
			mdss_fb_bl_update_notify(mfd, NOTIFY_TYPE_BL_UPDATE);
			pdata->set_backlight(pdata, temp);
			mfd->bl_level_scaled = mfd->unset_bl_level;
			mfd->allow_bl_update = true;
		}
	}
	mutex_unlock(&mfd->bl_lock);
}

static int mdss_fb_start_disp_thread(struct msm_fb_data_type *mfd)
{
	int ret = 0;

	pr_debug("%pS: start display thread fb%d\n",
		__builtin_return_address(0), mfd->index);

	/* this is needed for new split request from debugfs */
	mdss_fb_get_split(mfd);

	atomic_set(&mfd->commits_pending, 0);
	mfd->disp_thread = kthread_run(__mdss_fb_display_thread,
				mfd, "mdss_fb%d", mfd->index);

	if (IS_ERR(mfd->disp_thread)) {
		pr_err("ERROR: unable to start display thread %d\n",
				mfd->index);
		ret = PTR_ERR(mfd->disp_thread);
		mfd->disp_thread = NULL;
	}

	return ret;
}

static void mdss_fb_stop_disp_thread(struct msm_fb_data_type *mfd)
{
	pr_debug("%pS: stop display thread fb%d\n",
		__builtin_return_address(0), mfd->index);

	kthread_stop(mfd->disp_thread);
	mfd->disp_thread = NULL;
}

static void mdss_panel_validate_debugfs_info(struct msm_fb_data_type *mfd)
{
	struct mdss_panel_info *panel_info = mfd->panel_info;
	struct fb_info *fbi = mfd->fbi;
	struct fb_var_screeninfo *var = &fbi->var;
	struct mdss_panel_data *pdata = container_of(panel_info,
				struct mdss_panel_data, panel_info);

	if (panel_info->debugfs_info->override_flag) {
		if (mfd->mdp.off_fnc) {
			mfd->panel_reconfig = true;
			mfd->mdp.off_fnc(mfd);
			mfd->panel_reconfig = false;
		}

		pr_debug("Overriding panel_info with debugfs_info\n");
		panel_info->debugfs_info->override_flag = 0;
		mdss_panel_debugfsinfo_to_panelinfo(panel_info);
		if (is_panel_split(mfd) && pdata->next)
			mdss_fb_validate_split(pdata->panel_info.xres,
					pdata->next->panel_info.xres, mfd);
		mdss_panelinfo_to_fb_var(panel_info, var);
		if (mdss_fb_send_panel_event(mfd, MDSS_EVENT_CHECK_PARAMS,
							panel_info))
			pr_err("Failed to send panel event CHECK_PARAMS\n");
	}
}

static int mdss_fb_blank_blank(struct msm_fb_data_type *mfd,
	int req_power_state)
{
	int ret = 0;
	int cur_power_state, current_bl;

	if (!mfd)
		return -EINVAL;

	if (!mdss_fb_is_power_on(mfd) || !mfd->mdp.off_fnc)
		return 0;

	cur_power_state = mfd->panel_power_state;

	pr_debug("Transitioning from %d --> %d\n", cur_power_state,
		req_power_state);

	if (cur_power_state == req_power_state) {
		pr_debug("No change in power state\n");
		return 0;
	}

	mutex_lock(&mfd->update.lock);
	mfd->update.type = NOTIFY_TYPE_SUSPEND;
	mfd->update.is_suspend = 1;
	mutex_unlock(&mfd->update.lock);
	complete(&mfd->update.comp);
	del_timer(&mfd->no_update.timer);
	mfd->no_update.value = NOTIFY_TYPE_SUSPEND;
	complete(&mfd->no_update.comp);

	mfd->op_enable = false;
	if (mdss_panel_is_power_off(req_power_state)) {
		/* Stop Display thread */
		if (mfd->disp_thread)
			mdss_fb_stop_disp_thread(mfd);
		mutex_lock(&mfd->bl_lock);
		current_bl = mfd->bl_level;
		mfd->allow_bl_update = true;
		mdss_fb_set_backlight(mfd, 0);
		mfd->allow_bl_update = false;
		mfd->unset_bl_level = current_bl;
		mutex_unlock(&mfd->bl_lock);
	}
	mfd->panel_power_state = req_power_state;

	ret = mfd->mdp.off_fnc(mfd);
	if (ret)
		mfd->panel_power_state = cur_power_state;
	else if (mdss_panel_is_power_off(req_power_state))
		mdss_fb_release_fences(mfd);
	mfd->op_enable = true;
	complete(&mfd->power_off_comp);

	return ret;
}

static int mdss_fb_blank_unblank(struct msm_fb_data_type *mfd)
{
	int ret = 0;
	int cur_power_state;

	if (!mfd)
		return -EINVAL;

	if (mfd->panel_info->debugfs_info)
		mdss_panel_validate_debugfs_info(mfd);

	/* Start Display thread */
	if (mfd->disp_thread == NULL) {
		ret = mdss_fb_start_disp_thread(mfd);
		if (IS_ERR_VALUE((unsigned long) ret))
			return ret;
	}

	cur_power_state = mfd->panel_power_state;
	pr_debug("Transitioning from %d --> %d\n", cur_power_state,
		MDSS_PANEL_POWER_ON);

	if (mdss_panel_is_power_on_interactive(cur_power_state)) {
		pr_debug("No change in power state\n");
		return 0;
	}

	if (mfd->mdp.on_fnc) {
		struct mdss_panel_info *panel_info = mfd->panel_info;
		struct fb_var_screeninfo *var = &mfd->fbi->var;

		ret = mfd->mdp.on_fnc(mfd);
		if (ret) {
			mdss_fb_stop_disp_thread(mfd);
			goto error;
		}

		mfd->panel_power_state = MDSS_PANEL_POWER_ON;
		mfd->panel_info->panel_dead = false;
		mutex_lock(&mfd->update.lock);
		mfd->update.type = NOTIFY_TYPE_UPDATE;
		mfd->update.is_suspend = 0;
		mutex_unlock(&mfd->update.lock);

		/*
		 * Panel info can change depending in the information
		 * programmed in the controller.
		 * Update this info in the upstream structs.
		 */
		mdss_panelinfo_to_fb_var(panel_info, var);

		/* Start the work thread to signal idle time */
		if (mfd->idle_time)
			schedule_delayed_work(&mfd->idle_notify_work,
				msecs_to_jiffies(mfd->idle_time));
	}

	/* Reset the backlight only if the panel was off */
	if (mdss_panel_is_power_off(cur_power_state)) {
		mutex_lock(&mfd->bl_lock);
		if (!mfd->allow_bl_update) {
			mfd->allow_bl_update = true;
			/*
			 * If in AD calibration mode then frameworks would not
			 * be allowed to update backlight hence post unblank
			 * the backlight would remain 0 (0 is set in blank).
			 * Hence resetting back to calibration mode value
			 */
			if (IS_CALIB_MODE_BL(mfd))
				mdss_fb_set_backlight(mfd, mfd->calib_mode_bl);
			else if ((!mfd->panel_info->mipi.post_init_delay) &&
				(mfd->unset_bl_level != U32_MAX))
				mdss_fb_set_backlight(mfd, mfd->unset_bl_level);

			/*
			 * it blocks the backlight update between unblank and
			 * first kickoff to avoid backlight turn on before black
			 * frame is transferred to panel through unblank call.
			 */
			mfd->allow_bl_update = false;
		}
		mutex_unlock(&mfd->bl_lock);
	}

error:
	return ret;
}

static int mdss_fb_blank_sub(int blank_mode, struct fb_info *info,
			     int op_enable)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	int ret = 0;
	int cur_power_state, req_power_state = MDSS_PANEL_POWER_OFF;
	char trace_buffer[32];

	if (!mfd || !op_enable)
		return -EPERM;

	if (mfd->dcm_state == DCM_ENTER)
		return -EPERM;

	pr_debug("%pS mode:%d\n", __builtin_return_address(0),
		blank_mode);

	snprintf(trace_buffer, sizeof(trace_buffer), "fb%d blank %d",
		mfd->index, blank_mode);
	ATRACE_BEGIN(trace_buffer);

	cur_power_state = mfd->panel_power_state;

	/*
	 * Low power (lp) and ultra low power (ulp) modes are currently only
	 * supported for command mode panels. For all other panel, treat lp
	 * mode as full unblank and ulp mode as full blank.
	 */
	if (mfd->panel_info->type != MIPI_CMD_PANEL) {
		if (blank_mode == BLANK_FLAG_LP) {
			pr_debug("lp mode only valid for cmd mode panels\n");
			if (mdss_fb_is_power_on_interactive(mfd))
				return 0;
			blank_mode = FB_BLANK_UNBLANK;
		} else if (blank_mode == BLANK_FLAG_ULP) {
			pr_debug("ulp mode valid for cmd mode panels\n");
			if (mdss_fb_is_power_off(mfd))
				return 0;
			blank_mode = FB_BLANK_POWERDOWN;
		}
	}

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		pr_debug("unblank called. cur pwr state=%d\n", cur_power_state);
		ret = mdss_fb_blank_unblank(mfd);
		break;
	case BLANK_FLAG_ULP:
		req_power_state = MDSS_PANEL_POWER_LP2;
		pr_debug("ultra low power mode requested\n");
		if (mdss_fb_is_power_off(mfd)) {
			pr_debug("Unsupp transition: off --> ulp\n");
			return 0;
		}

		ret = mdss_fb_blank_blank(mfd, req_power_state);
		break;
	case BLANK_FLAG_LP:
		req_power_state = MDSS_PANEL_POWER_LP1;
		pr_debug(" power mode requested\n");

		/*
		 * If low power mode is requested when panel is already off,
		 * then first unblank the panel before entering low power mode
		 */
		if (mdss_fb_is_power_off(mfd) && mfd->mdp.on_fnc) {
			pr_debug("off --> lp. switch to on first\n");
			ret = mdss_fb_blank_unblank(mfd);
			if (ret)
				break;
		}

		ret = mdss_fb_blank_blank(mfd, req_power_state);
		break;
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
	default:
		req_power_state = MDSS_PANEL_POWER_OFF;
		pr_debug("blank powerdown called\n");
		ret = mdss_fb_blank_blank(mfd, req_power_state);
		break;
	}

	/* Notify listeners */
	sysfs_notify(&mfd->fbi->dev->kobj, NULL, "show_blank_event");

	ATRACE_END(trace_buffer);

	return ret;
}

static int mdss_fb_blank(int blank_mode, struct fb_info *info)
{
	int ret;
	struct mdss_panel_data *pdata;
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;

	ret = mdss_fb_pan_idle(mfd);
	if (ret) {
		pr_warn("mdss_fb_pan_idle for fb%d failed. ret=%d\n",
			mfd->index, ret);
		return ret;
	}

	mutex_lock(&mfd->mdss_sysfs_lock);

	if (mfd->op_enable == 0) {
		if (blank_mode == FB_BLANK_UNBLANK)
			mfd->suspend.panel_power_state = MDSS_PANEL_POWER_ON;
		else if (blank_mode == BLANK_FLAG_ULP)
			mfd->suspend.panel_power_state = MDSS_PANEL_POWER_LP2;
		else if (blank_mode == BLANK_FLAG_LP)
			mfd->suspend.panel_power_state = MDSS_PANEL_POWER_LP1;
		else
			mfd->suspend.panel_power_state = MDSS_PANEL_POWER_OFF;

		ret = 0;
		goto end;
	}
	pr_debug("mode: %d\n", blank_mode);

	pdata = dev_get_platdata(&mfd->pdev->dev);

	if (pdata->panel_info.is_lpm_mode &&
			blank_mode == FB_BLANK_UNBLANK) {
		pr_debug("panel is in lpm mode\n");
		mfd->mdp.configure_panel(mfd, 0, 1);
		mdss_fb_set_mdp_sync_pt_threshold(mfd, mfd->panel.type);
		pdata->panel_info.is_lpm_mode = false;
	}

	if (pdata->panel_disable_mode && mfd->mdp.enable_panel_disable_mode)
		mfd->mdp.enable_panel_disable_mode(mfd, false);

	ret = mdss_fb_blank_sub(blank_mode, info, mfd->op_enable);
	MDSS_XLOG(blank_mode);

end:
	mutex_unlock(&mfd->mdss_sysfs_lock);

	return ret;
}

void mdss_fb_free_fb_ion_memory(struct msm_fb_data_type *mfd)
{
	if (!mfd) {
		pr_err("no mfd\n");
		return;
	}

	if (!mfd->fbi->screen_base)
		return;

	if (!mfd->fbmem_buf) {
		pr_err("invalid input parameters for fb%d\n", mfd->index);
		return;
	}

	mfd->fbi->screen_base = NULL;
	mfd->fbi->fix.smem_start = 0;

	dma_buf_vunmap(mfd->fbmem_buf, &map);

	dma_buf_end_cpu_access(mfd->fbmem_buf, DMA_BIDIRECTIONAL);

	if ((mfd->mdp.fb_mem_get_iommu_domain ||
		(mfd->panel.type == SPI_PANEL)) &&
		!(!mfd->fb_attachment || !mfd->fb_attachment->dmabuf ||
		!mfd->fb_attachment->dmabuf->ops)) {
		dma_buf_unmap_attachment(mfd->fb_attachment, mfd->fb_table,
				DMA_BIDIRECTIONAL);
		dma_buf_detach(mfd->fbmem_buf, mfd->fb_attachment);
		dma_buf_put(mfd->fbmem_buf);
		mfd->fbmem_buf = NULL;
	}

	if (mfd->fbmem_buf) {
		dma_buf_put(mfd->fbmem_buf);
		mfd->fbmem_buf = NULL;
	}
}

int mdss_fb_alloc_fb_ion_memory(struct msm_fb_data_type *mfd, size_t fb_size)
{
	int rc = 0;
	void *vaddr = NULL;
	int domain;
	struct dma_heap *heap = NULL;

	if (!mfd) {
		pr_err("Invalid input param - no mfd\n");
		return -EINVAL;
	}

	pr_debug("size for mmap = %zu\n", fb_size);
	pr_debug("%s: Starting DMAHEAP allocation\n", __func__);

	heap = dma_heap_find("qcom,system-uncached");
	if (!heap) {
		pr_err("%s: Unable to find the system-uncached heap\n", __func__);
		return -EINVAL;
	}

	mfd->fbmem_buf = dma_heap_buffer_alloc(heap, fb_size, 0, 0);
	if (IS_ERR_OR_NULL(mfd->fbmem_buf)) {
		if (IS_ERR(mfd->fbmem_buf))
			pr_err("%s: dmaheap_alloc failure err ptr=%ld, sz: 0x%zx\n",
				__func__, PTR_ERR(mfd->fbmem_buf), fb_size);
		return -ENOMEM;
	}

	if (mfd->mdp.fb_mem_get_iommu_domain) {
		domain = mfd->mdp.fb_mem_get_iommu_domain();

		mfd->fb_attachment = mdss_smmu_dma_buf_attach(mfd->fbmem_buf,
				&mfd->pdev->dev, domain);
		if (IS_ERR(mfd->fb_attachment)) {
			rc = PTR_ERR(mfd->fb_attachment);
			goto err_put;
		}

		mfd->fb_table = dma_buf_map_attachment(mfd->fb_attachment,
				DMA_BIDIRECTIONAL);
		if (IS_ERR(mfd->fb_table)) {
			rc = PTR_ERR(mfd->fb_table);
			goto err_detach;
		}
	} else if (mfd->panel.type == SPI_PANEL) {
		mfd->fb_attachment = dma_buf_attach(mfd->fbmem_buf,
				&mfd->pdev->dev);
		if (IS_ERR(mfd->fb_attachment)) {
			rc = PTR_ERR(mfd->fb_attachment);
			goto err_put;
		}

		mfd->fb_table = dma_buf_map_attachment(mfd->fb_attachment,
			DMA_BIDIRECTIONAL);
		if (IS_ERR(mfd->fb_table)) {
			rc = PTR_ERR(mfd->fb_table);
			goto err_detach;
		}
	} else {
		pr_err("No IOMMU Domain\n");
		rc = -EINVAL;
		goto fb_mmap_failed;
	}

	dma_buf_begin_cpu_access(mfd->fbmem_buf, DMA_BIDIRECTIONAL);
	rc  = dma_buf_vmap(mfd->fbmem_buf, &map);
	if (!rc) {
		pr_err("ION memory mapping failed - %ld\n", PTR_ERR(map.vaddr));
		rc = PTR_ERR(map.vaddr);
		goto err_unmap;
	}
	iosys_map_set_vaddr(&map, vaddr);

	pr_debug("alloc 0x%zxB vaddr = %pK for fb%d\n", fb_size,
			vaddr, mfd->index);

	mfd->fbi->screen_base = (char *) vaddr;
	mfd->fbi->fix.smem_len = fb_size;

	return rc;

err_unmap:
	dma_buf_unmap_attachment(mfd->fb_attachment, mfd->fb_table,
					DMA_BIDIRECTIONAL);
err_detach:
	dma_buf_detach(mfd->fbmem_buf, mfd->fb_attachment);
err_put:
	dma_buf_put(mfd->fbmem_buf);
fb_mmap_failed:
	mfd->fb_attachment = NULL;
	mfd->fb_table = NULL;
	mfd->fbmem_buf = NULL;
	return rc;
}

/**
 * mdss_fb_fbmem_ion_mmap() -  Custom fb  mmap() function for MSM driver.
 *
 * @info -  Framebuffer info.
 * @vma  -  VM area which is part of the process virtual memory.
 *
 * This framebuffer mmap function differs from standard mmap() function by
 * allowing for customized page-protection and dynamically allocate framebuffer
 * memory from system heap and map to iommu virtual address.
 *
 * Return: virtual address is returned through vma
 */
static int mdss_fb_fbmem_ion_mmap(struct fb_info *info,
		struct vm_area_struct *vma)
{
	int rc = 0;
	size_t req_size, fb_size;
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	struct sg_table *table;
	unsigned long addr = vma->vm_start;
	unsigned long offset = vma->vm_pgoff * PAGE_SIZE;
	struct scatterlist *sg;
	unsigned int i;
	struct page *page;

	if (!mfd || !mfd->pdev || !mfd->pdev->dev.of_node) {
		pr_err("Invalid device node\n");
		return -ENODEV;
	}

	req_size = vma->vm_end - vma->vm_start;
	fb_size = mfd->fbi->fix.smem_len;
	if (req_size > fb_size) {
		pr_warn("requested map is greater than framebuffer\n");
		return -EOVERFLOW;
	}

	mdss_fb_blank_blank(mfd, MDSS_PANEL_POWER_OFF);
	mdss_fb_blank_unblank(mfd);

	if (!mfd->fbi->screen_base) {
		rc = mdss_fb_alloc_fb_ion_memory(mfd, fb_size);
		if (rc < 0) {
			pr_err("fb mmap failed!!!!\n");
			return rc;
		}
	}

	table = mfd->fb_table;
	if (IS_ERR(table)) {
		pr_err("Unable to get sg_table from ion:%ld\n", PTR_ERR(table));
		mfd->fbi->screen_base = NULL;
		return PTR_ERR(table);
	} else if (!table) {
		pr_err("sg_list is NULL\n");
		mfd->fbi->screen_base = NULL;
		return -EINVAL;
	}

	page = sg_page(table->sgl);
	if (page) {
		for_each_sg(table->sgl, sg, table->nents, i) {
			unsigned long remainder = vma->vm_end - addr;
			unsigned long len = sg->length;

			page = sg_page(sg);

			if (offset >= sg->length) {
				offset -= sg->length;
				continue;
			} else if (offset) {
				page += offset / PAGE_SIZE;
				len = sg->length - offset;
				offset = 0;
			}
			len = min(len, remainder);

			if (mfd->mdp_fb_page_protection ==
					MDP_FB_PAGE_PROTECTION_WRITECOMBINE)
				vma->vm_page_prot =
					pgprot_writecombine(vma->vm_page_prot);

			pr_debug("vma=%pK, addr=%x len=%ld\n",
					vma, (unsigned int)addr, len);
			pr_debug("vm_start=%x vm_end=%x vm_page_prot=%ld\n",
					(unsigned int)vma->vm_start,
					(unsigned int)vma->vm_end,
					(unsigned long)pgprot_val
							(vma->vm_page_prot));

			io_remap_pfn_range(vma, addr, page_to_pfn(page), len,
					vma->vm_page_prot);
			addr += len;
			if (addr >= vma->vm_end)
				break;
		}
	} else {
		pr_err("PAGE is null\n");
		mdss_fb_free_fb_ion_memory(mfd);
		return -ENOMEM;
	}

	return rc;
}

/*
 * mdss_fb_physical_mmap() - Custom fb mmap() function for MSM driver.
 *
 * @info -  Framebuffer info.
 * @vma  -  VM area which is part of the process virtual memory.
 *
 * This framebuffer mmap function differs from standard mmap() function as
 * map to framebuffer memory from the CMA memory which is allocated during
 * bootup.
 *
 * Return: virtual address is returned through vma
 */
static int mdss_fb_physical_mmap(struct fb_info *info,
		struct vm_area_struct *vma)
{
	/* Get frame buffer memory range. */
	unsigned long start = info->fix.smem_start;
	u32 len = PAGE_ALIGN((start & ~PAGE_MASK) + info->fix.smem_len);
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;

	if (!start) {
		pr_warn("No framebuffer memory is allocated\n");
		return -ENOMEM;
	}

	/* Set VM flags. */
	start &= PAGE_MASK;
	if ((vma->vm_end <= vma->vm_start) ||
			(off >= len) ||
			((vma->vm_end - vma->vm_start) > (len - off)))
		return -EINVAL;
	off += start;
	if (off < start)
		return -EINVAL;
	vma->vm_pgoff = off >> PAGE_SHIFT;
	/* This is an IO map - tell maydump to skip this VMA */
	vm_flags_set(vma, VM_IO);
	//vma->vm_flags |= VM_IO;

	if (mfd->mdp_fb_page_protection == MDP_FB_PAGE_PROTECTION_WRITECOMBINE)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	/* Remap the frame buffer I/O range */
	if (io_remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static int mdss_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	int rc = -EINVAL;

	if (mfd->fb_mmap_type == MDP_FB_MMAP_ION_ALLOC) {
		rc = mdss_fb_fbmem_ion_mmap(info, vma);
	} else if (mfd->fb_mmap_type == MDP_FB_MMAP_PHYSICAL_ALLOC) {
		rc = mdss_fb_physical_mmap(info, vma);
	} else {
		if (!info->fix.smem_start) {
			rc = mdss_fb_fbmem_ion_mmap(info, vma);
			mfd->fb_mmap_type = MDP_FB_MMAP_ION_ALLOC;
		} else {
			rc = mdss_fb_physical_mmap(info, vma);
			mfd->fb_mmap_type = MDP_FB_MMAP_PHYSICAL_ALLOC;
		}
	}
	if (rc < 0)
		pr_err("fb mmap failed with rc = %d\n", rc);

	return rc;
}

static const struct fb_ops mdss_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = mdss_fb_open,
	.fb_release = mdss_fb_release,
	.fb_check_var = mdss_fb_check_var,	/* vinfo check */
	.fb_set_par = mdss_fb_set_par,	/* set the video mode */
	.fb_blank = mdss_fb_blank,	/* blank display */
	.fb_pan_display = mdss_fb_pan_display,	/* pan display */
	.fb_ioctl = mdss_fb_ioctl,	/* perform fb specific ioctl */
	.fb_mmap = mdss_fb_mmap,
};

static int mdss_fb_alloc_fbmem_iommu(struct msm_fb_data_type *mfd, int dom)
{
	void *virt = NULL;
	unsigned long long phys = 0;
	size_t size = 0;
	struct platform_device *pdev = mfd->pdev;
	int rc = 0;
	struct device_node *fbmem_pnode = NULL;
	const u32 *addr;
	u64 len;


	if (!pdev || !pdev->dev.of_node) {
		pr_err("Invalid device node\n");
		return -ENODEV;
	}

	fbmem_pnode = of_parse_phandle(pdev->dev.of_node,
		"linux,contiguous-region", 0);
	if (!fbmem_pnode) {
		pr_debug("fbmem is not reserved for %s\n", pdev->name);
		mfd->fbi->screen_base = NULL;
		mfd->fbi->fix.smem_start = 0;
		return 0;
	}

	addr = of_get_address(fbmem_pnode, 0, &len, NULL);
	if (!addr) {
		pr_err("fbmem size is not specified\n");
		of_node_put(fbmem_pnode);
		return -EINVAL;
	}
	size = (size_t)len;
	of_node_put(fbmem_pnode);

	pr_debug("%s frame buffer reserve_size=0x%zx\n", __func__, size);

	if (size < PAGE_ALIGN(mfd->fbi->fix.line_length *
			      mfd->fbi->var.yres_virtual))
		pr_warn("reserve size is smaller than framebuffer size\n");

	rc = mdss_smmu_dma_alloc_coherent(&pdev->dev, size, (dma_addr_t *)&phys, &mfd->iova,
			&virt, GFP_KERNEL, dom);
	if (rc) {
		pr_err("unable to alloc fbmem size=%zx\n", size);
		return -ENOMEM;
	}

	if (MDSS_LPAE_CHECK(phys)) {
		pr_warn("fb mem phys %pa > 4GB is not supported.\n", &phys);
		mdss_smmu_dma_free_coherent(&pdev->dev, size, &virt,
				phys, mfd->iova, dom);
		return -ERANGE;
	}

	pr_debug("alloc 0x%zxB @ (%pa phys) (0x%pK virt) (%pa iova) for fb%d\n",
		 size, &phys, virt, &mfd->iova, mfd->index);

	mfd->fbi->screen_base = virt;
	mfd->fbi->fix.smem_start = phys;
	mfd->fbi->fix.smem_len = size;

	return 0;
}

static int mdss_fb_alloc_fbmem(struct msm_fb_data_type *mfd)
{

	if (mfd->mdp.fb_mem_alloc_fnc) {
		return mfd->mdp.fb_mem_alloc_fnc(mfd);
	} else if (mfd->mdp.fb_mem_get_iommu_domain) {
		int dom = mfd->mdp.fb_mem_get_iommu_domain();

		if (dom >= 0)
			return mdss_fb_alloc_fbmem_iommu(mfd, dom);
		else
			return -ENOMEM;
	} else {
		pr_err("no fb memory allocator function defined\n");
		return -ENOMEM;
	}
}

static int mdss_fb_register(struct msm_fb_data_type *mfd)
{
	int ret = -ENODEV;
	int bpp;
	char panel_name[20];
	struct mdss_panel_info *panel_info = mfd->panel_info;
	struct fb_info *fbi = mfd->fbi;
	struct fb_fix_screeninfo *fix;
	struct fb_var_screeninfo *var;
	int *id;

	/*
	 * fb info initialization
	 */
	fix = &fbi->fix;
	var = &fbi->var;

	fix->type_aux = 0;	/* if type == FB_TYPE_INTERLEAVED_PLANES */
	fix->visual = FB_VISUAL_TRUECOLOR;	/* True Color */
	fix->ywrapstep = 0;	/* No support */
	fix->mmio_start = 0;	/* No MMIO Address */
	fix->mmio_len = 0;	/* No MMIO Address */
	fix->accel = FB_ACCEL_NONE;/* FB_ACCEL_MSM needes to be added in fb.h */

	var->xoffset = 0,	/* Offset from virtual to visible */
	var->yoffset = 0,	/* resolution */
	var->grayscale = 0,	/* No graylevels */
	var->nonstd = 0,	/* standard pxl format */
	var->activate = FB_ACTIVATE_VBL,	/* activate it at vsync */
	var->height = -1,	/* height of picture in mm */
	var->width = -1,	/* width of picture in mm */
	var->accel_flags = 0,	/* acceleration flags */
	var->sync = 0,	/* see FB_SYNC_* */
	var->rotate = 0,	/* angle we rotate counter clockwise */
	mfd->op_enable = false;

	switch (mfd->fb_imgType) {
	case MDP_RGB_565:
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->xpanstep = 1;
		fix->ypanstep = 1;
		var->vmode = FB_VMODE_NONINTERLACED;
		var->blue.offset = 0;
		var->green.offset = 5;
		var->red.offset = 11;
		var->blue.length = 5;
		var->green.length = 6;
		var->red.length = 5;
		var->blue.msb_right = 0;
		var->green.msb_right = 0;
		var->red.msb_right = 0;
		var->transp.offset = 0;
		var->transp.length = 0;
		bpp = 2;
		break;

	case MDP_RGB_888:
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->xpanstep = 1;
		fix->ypanstep = 1;
		var->vmode = FB_VMODE_NONINTERLACED;
		var->blue.offset = 0;
		var->green.offset = 8;
		var->red.offset = 16;
		var->blue.length = 8;
		var->green.length = 8;
		var->red.length = 8;
		var->blue.msb_right = 0;
		var->green.msb_right = 0;
		var->red.msb_right = 0;
		var->transp.offset = 0;
		var->transp.length = 0;
		bpp = 3;
		break;

	case MDP_ARGB_8888:
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->xpanstep = 1;
		fix->ypanstep = 1;
		var->vmode = FB_VMODE_NONINTERLACED;
		var->blue.offset = 24;
		var->green.offset = 16;
		var->red.offset = 8;
		var->blue.length = 8;
		var->green.length = 8;
		var->red.length = 8;
		var->blue.msb_right = 0;
		var->green.msb_right = 0;
		var->red.msb_right = 0;
		var->transp.offset = 0;
		var->transp.length = 8;
		bpp = 4;
		break;

	case MDP_RGBA_8888:
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->xpanstep = 1;
		fix->ypanstep = 1;
		var->vmode = FB_VMODE_NONINTERLACED;
		var->blue.offset = 16;
		var->green.offset = 8;
		var->red.offset = 0;
		var->blue.length = 8;
		var->green.length = 8;
		var->red.length = 8;
		var->blue.msb_right = 0;
		var->green.msb_right = 0;
		var->red.msb_right = 0;
		var->transp.offset = 24;
		var->transp.length = 8;
		bpp = 4;
		break;

	case MDP_YCRYCB_H2V1:
		fix->type = FB_TYPE_INTERLEAVED_PLANES;
		fix->xpanstep = 2;
		fix->ypanstep = 1;
		var->vmode = FB_VMODE_NONINTERLACED;

		/* how about R/G/B offset? */
		var->blue.offset = 0;
		var->green.offset = 5;
		var->red.offset = 11;
		var->blue.length = 5;
		var->green.length = 6;
		var->red.length = 5;
		var->blue.msb_right = 0;
		var->green.msb_right = 0;
		var->red.msb_right = 0;
		var->transp.offset = 0;
		var->transp.length = 0;
		bpp = 2;
		break;

	default:
		pr_err("msm_fb_init: fb %d unknown image type!\n",
			    mfd->index);
		return ret;
	}

	mdss_panelinfo_to_fb_var(panel_info, var);

	fix->type = panel_info->is_3d_panel;
	if (mfd->mdp.fb_stride)
		fix->line_length = mfd->mdp.fb_stride(mfd->index, var->xres,
							bpp);
	else
		fix->line_length = var->xres * bpp;

	var->xres_virtual = var->xres;
	var->yres_virtual = panel_info->yres * mfd->fb_page;
	var->bits_per_pixel = bpp * 8;	/* FrameBuffer color depth */

	/*
	 * Populate smem length here for uspace to get the
	 * Framebuffer size when FBIO_FSCREENINFO ioctl is called.
	 */
	fix->smem_len = PAGE_ALIGN(fix->line_length * var->yres) * mfd->fb_page;

	/* id field for fb app  */
	id = (int *)&mfd->panel;

	snprintf(fix->id, sizeof(fix->id), "mdssfb_%x", (u32) *id);

	fbi->fbops = &mdss_fb_ops;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->pseudo_palette = mdss_fb_pseudo_palette;

	mfd->ref_cnt = 0;
	mfd->panel_power_state = MDSS_PANEL_POWER_OFF;
	mfd->dcm_state = DCM_UNINIT;

	if (mdss_fb_alloc_fbmem(mfd))
		pr_warn("unable to allocate fb memory in fb register\n");

	mfd->op_enable = true;

	mutex_init(&mfd->update.lock);
	mutex_init(&mfd->no_update.lock);
	mutex_init(&mfd->mdp_sync_pt_data.sync_mutex);
	atomic_set(&mfd->mdp_sync_pt_data.commit_cnt, 0);
	atomic_set(&mfd->commits_pending, 0);
	atomic_set(&mfd->ioctl_ref_cnt, 0);
	atomic_set(&mfd->kickoff_pending, 0);

	// init_timer(&mfd->no_update.timer);
	// mfd->no_update.timer.function = mdss_fb_no_update_notify_timer_cb;
	// mfd->no_update.timer.data = (unsigned long)mfd;
	timer_setup(&mfd->no_update.timer, mdss_fb_no_update_notify_timer_cb, 0);
	mfd->update.ref_count = 0;
	mfd->no_update.ref_count = 0;
	mfd->update.init_done = false;
	init_completion(&mfd->update.comp);
	init_completion(&mfd->no_update.comp);
	init_completion(&mfd->power_off_comp);
	init_completion(&mfd->power_set_comp);
	init_waitqueue_head(&mfd->commit_wait_q);
	init_waitqueue_head(&mfd->idle_wait_q);
	init_waitqueue_head(&mfd->ioctl_q);
	init_waitqueue_head(&mfd->kickoff_wait_q);

	ret = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (ret)
		pr_err("fb_alloc_cmap() failed!\n");

	if (register_framebuffer(fbi) < 0) {
		fb_dealloc_cmap(&fbi->cmap);

		mfd->op_enable = false;
		return -EPERM;
	}

	snprintf(panel_name, ARRAY_SIZE(panel_name), "mdss_panel_fb%d",
		mfd->index);
	mdss_panel_debugfs_init(panel_info, panel_name);
	pr_info("FrameBuffer[%d] %dx%d registered successfully!\n", mfd->index,
					fbi->var.xres, fbi->var.yres);

	return 0;
}

static int mdss_fb_open(struct fb_info *info, int user)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	struct mdss_fb_file_info *file_info = NULL;
	int result;
	struct task_struct *task = current->group_leader;

	if (mfd->shutdown_pending) {
		pr_err_once("Shutdown pending. Aborting operation. Request from pid:%d name=%s\n",
			current->tgid, task->comm);
		sysfs_notify(&mfd->fbi->dev->kobj, NULL, "show_blank_event");
		return -ESHUTDOWN;
	}

	file_info = kmalloc(sizeof(*file_info), GFP_KERNEL);
	if (!file_info)
		return -ENOMEM;

	file_info->file = fb_file;
	list_add(&file_info->list, &mfd->file_list);

	result = pm_runtime_get_sync(info->dev);

	if (result < 0) {
		pr_err("pm_runtime: fail to wake up\n");
		goto pm_error;
	}

	if (!mfd->ref_cnt) {
		result = mdss_fb_blank_sub(FB_BLANK_UNBLANK, info,
					   mfd->op_enable);
		if (result) {
			pr_err("can't turn on fb%d! rc=%d\n", mfd->index,
				result);
			goto blank_error;
		}
	}

	mfd->ref_cnt++;
	pr_debug("mfd refcount:%d file:%pK\n", mfd->ref_cnt, fb_file);

	return 0;

blank_error:
	pm_runtime_put(info->dev);
pm_error:
	list_del(&file_info->list);
	kfree(file_info);
	return result;
}

static int mdss_fb_release_all(struct fb_info *info, bool release_all)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	struct mdss_fb_file_info *file_info = NULL, *temp_file_info = NULL;
	struct file *file = fb_file;
	int ret = 0;
	bool node_found = false;
	struct task_struct *task = current->group_leader;

	if (!mfd->ref_cnt) {
		pr_info("try to close unopened fb %d! from pid:%d name:%s\n",
			mfd->index, current->tgid, task->comm);
		return -EINVAL;
	}

	if (!wait_event_timeout(mfd->ioctl_q,
		!atomic_read(&mfd->ioctl_ref_cnt) || !release_all,
		msecs_to_jiffies(1000)))
		pr_warn("fb%d ioctl could not finish. waited 1 sec.\n",
			mfd->index);

	/* wait only for the last release */
	if (release_all || (mfd->ref_cnt == 1)) {
		ret = mdss_fb_pan_idle(mfd);
		if (ret && (ret != -ESHUTDOWN))
			pr_warn("mdss_fb_pan_idle for fb%d failed. ret=%d ignoring.\n",
				mfd->index, ret);
	}

	pr_debug("release_all = %s\n", release_all ? "true" : "false");

	list_for_each_entry_safe(file_info, temp_file_info, &mfd->file_list,
		list) {
		if (!release_all && file_info->file != file)
			continue;

		pr_debug("found file node mfd->ref=%d\n", mfd->ref_cnt);
		list_del(&file_info->list);
		kfree(file_info);

		mfd->ref_cnt--;
		pm_runtime_put(info->dev);

		node_found = true;

		if (!release_all)
			break;
	}

	if (!node_found || (release_all && mfd->ref_cnt))
		pr_warn("file node not found or wrong ref cnt: release all:%d refcnt:%d\n",
			release_all, mfd->ref_cnt);

	pr_debug("current process=%s pid=%d mfd->ref=%d file:%pK\n",
		task->comm, current->tgid, mfd->ref_cnt, fb_file);

	if (!mfd->ref_cnt || release_all) {
		/* resources (if any) will be released during blank */
		if (mfd->mdp.release_fnc)
			mfd->mdp.release_fnc(mfd, NULL);

		if (mfd->mdp.pp_release_fnc) {
			ret = (*mfd->mdp.pp_release_fnc)(mfd);
			if (ret)
				pr_err("PP release failed ret %d\n", ret);
		}

		/* reset backlight before blank to prevent backlight from
		 * enabling ahead of unblank. for some special cases like
		 * adb shell stop/start.
		 */
		mutex_lock(&mfd->bl_lock);
		mdss_fb_set_backlight(mfd, 0);
		mutex_unlock(&mfd->bl_lock);

		ret = mdss_fb_blank_sub(FB_BLANK_POWERDOWN, info,
			mfd->op_enable);
		if (ret) {
			pr_err("can't turn off fb%d! rc=%d current process=%s pid=%d\n",
			      mfd->index, ret, task->comm, current->tgid);
			return ret;
		}
		if (mfd->fbmem_buf)
			mdss_fb_free_fb_ion_memory(mfd);

		atomic_set(&mfd->ioctl_ref_cnt, 0);
	} else {
		if (mfd->mdp.release_fnc)
			ret = mfd->mdp.release_fnc(mfd, file);

		/* display commit is needed to release resources */
		if (ret)
			mdss_fb_pan_display(&mfd->fbi->var, mfd->fbi);
	}

	return ret;
}

static int mdss_fb_release(struct fb_info *info, int user)
{
	return mdss_fb_release_all(info, false);
}

static void mdss_fb_power_setting_idle(struct msm_fb_data_type *mfd)
{
	int ret;

	if (mfd->is_power_setting) {
		ret = wait_for_completion_timeout(
				&mfd->power_set_comp,
			msecs_to_jiffies(WAIT_DISP_OP_TIMEOUT));
		if (ret < 0)
			ret = -ERESTARTSYS;
		else if (!ret)
			pr_err("%s wait for power_set_comp timeout %d %d\n",
				__func__, ret, mfd->is_power_setting);
		if (ret <= 0) {
			mfd->is_power_setting = false;
			complete_all(&mfd->power_set_comp);
		}
	}
}

static void __mdss_fb_copy_fence(struct msm_sync_pt_data *sync_pt_data,
	struct mdss_fence **fences, u32 *fence_cnt)
{
	pr_debug("%s: wait for fences\n", sync_pt_data->fence_name);

	mutex_lock(&sync_pt_data->sync_mutex);
	/*
	 * Assuming that acq_fen_cnt is handled properly in bufsync ioctl
	 * to check for sync_pt_data->acq_fen_cnt <= MDP_MAX_FENCE_FD
	 */
	*fence_cnt = sync_pt_data->acq_fen_cnt;
	sync_pt_data->acq_fen_cnt = 0;
	if (*fence_cnt)
		memcpy(fences, sync_pt_data->acq_fen,
				*fence_cnt * sizeof(struct mdss_fence *));
	mutex_unlock(&sync_pt_data->sync_mutex);
}

static int __mdss_fb_wait_for_fence_sub(struct msm_sync_pt_data *sync_pt_data,
	struct mdss_fence **fences, int fence_cnt)
{
	int i, ret = 0;
	unsigned long max_wait = msecs_to_jiffies(WAIT_MAX_FENCE_TIMEOUT);
	unsigned long timeout = jiffies + max_wait;
	long wait_ms, wait_jf;

	/* buf sync */
	for (i = 0; i < fence_cnt && !ret; i++) {
		wait_jf = timeout - jiffies;
		wait_ms = jiffies_to_msecs(wait_jf);

		/*
		 * In this loop, if one of the previous fence took long
		 * time, give a chance for the next fence to check if
		 * fence is already signalled. If not signalled it breaks
		 * in the final wait timeout.
		 */
		if (wait_jf < 0)
			wait_ms = WAIT_MIN_FENCE_TIMEOUT;
		else
			wait_ms = min_t(long, WAIT_FENCE_FIRST_TIMEOUT,
					wait_ms);

		ret = mdss_wait_sync_fence(fences[i], wait_ms);

		if (ret == -ETIME) {
			wait_jf = timeout - jiffies;
			wait_ms = jiffies_to_msecs(wait_jf);
			if (wait_jf < 0)
				break;

			wait_ms = min_t(long, WAIT_FENCE_FINAL_TIMEOUT,
					wait_ms);

			pr_warn("%s: timed out! Waiting %ld.%ld more seconds\n",
				mdss_get_sync_fence_name(fences[i]),
				(wait_ms/MSEC_PER_SEC), (wait_ms%MSEC_PER_SEC));
			MDSS_XLOG(sync_pt_data->timeline_value);
			MDSS_XLOG_TOUT_HANDLER("mdp");
			ret = mdss_wait_sync_fence(fences[i], wait_ms);

			if (ret == -ETIME)
				break;
		}
		mdss_put_sync_fence(fences[i]);
	}

	if (ret < 0) {
		pr_err("%s: sync_fence_wait failed! ret = %x\n",
				sync_pt_data->fence_name, ret);
		for (; i < fence_cnt; i++)
			mdss_put_sync_fence(fences[i]);
	}
	return ret;
}

int mdss_fb_wait_for_fence(struct msm_sync_pt_data *sync_pt_data)
{
	struct mdss_fence *fences[MDP_MAX_FENCE_FD];
	int fence_cnt = 0;

	__mdss_fb_copy_fence(sync_pt_data, fences, &fence_cnt);

	if (fence_cnt)
		__mdss_fb_wait_for_fence_sub(sync_pt_data,
			fences, fence_cnt);

	return fence_cnt;
}

/**
 * mdss_fb_signal_timeline() - signal a single release fence
 * @sync_pt_data:	Sync point data structure for the timeline which
 *			should be signaled.
 *
 * This is called after a frame has been pushed to display. This signals the
 * timeline to release the fences associated with this frame.
 */
void mdss_fb_signal_timeline(struct msm_sync_pt_data *sync_pt_data)
{
	struct msm_fb_data_type *mfd;

	mfd = container_of(sync_pt_data, typeof(*mfd), mdp_sync_pt_data);
	mutex_lock(&sync_pt_data->sync_mutex);
	if (atomic_read(&sync_pt_data->commit_cnt) &&
			sync_pt_data->timeline) {
		mdss_inc_timeline(sync_pt_data->timeline, 1);

		/*
		 * For Command mode panels, the retire timeline is incremented
		 * whenever we receive a readptr_done. For all other panels,
		 * the retire fence should be signaled along with the release
		 * fence once the frame is done.
		 */
		if (mfd->panel.type != MIPI_CMD_PANEL)
			mdss_inc_timeline(sync_pt_data->timeline_retire, 1);
		MDSS_XLOG(sync_pt_data->timeline_value);
		sync_pt_data->timeline_value++;

		pr_debug("%s: buffer signaled! timeline val=%d commit_cnt=%d\n",
			sync_pt_data->fence_name, sync_pt_data->timeline_value,
			atomic_read(&sync_pt_data->commit_cnt));
	} else {
		pr_debug("%s timeline signaled without commits val=%d\n",
			sync_pt_data->fence_name, sync_pt_data->timeline_value);
	}
	mutex_unlock(&sync_pt_data->sync_mutex);
}

/**
 * mdss_fb_release_fences() - signal all pending release fences
 * @mfd:	Framebuffer data structure for display
 *
 * Release all currently pending release fences, including those that are in
 * the process to be committed.
 *
 * Note: this should only be called during close or suspend sequence.
 */
static void mdss_fb_release_fences(struct msm_fb_data_type *mfd)
{
	struct msm_sync_pt_data *sync_pt_data = &mfd->mdp_sync_pt_data;
	int val;

	mutex_lock(&sync_pt_data->sync_mutex);
	if (sync_pt_data->timeline) {
		val = sync_pt_data->threshold +
			atomic_read(&sync_pt_data->commit_cnt);
		mdss_resync_timeline(sync_pt_data->timeline);
		if (mfd->panel.type != MIPI_CMD_PANEL)
			mdss_resync_timeline(sync_pt_data->timeline_retire);
		sync_pt_data->timeline_value = val;

	}
	mutex_unlock(&sync_pt_data->sync_mutex);
}

static void mdss_fb_release_kickoff(struct msm_fb_data_type *mfd)
{
	if (mfd->wait_for_kickoff) {
		atomic_set(&mfd->kickoff_pending, 0);
		wake_up_all(&mfd->kickoff_wait_q);
	}
}

#ifndef CONFIG_FB_MSM_MDP_NONE
/**
 * __mdss_fb_sync_buf_done_callback() - process async display events
 * @p:		Notifier block registered for async events.
 * @event:	Event enum to identify the event.
 * @data:	Optional argument provided with the event.
 *
 * See enum mdp_notify_event for events handled.
 */
static int __mdss_fb_sync_buf_done_callback(struct notifier_block *p,
		unsigned long event, void *data)
{
	struct msm_sync_pt_data *sync_pt_data;
	struct msm_fb_data_type *mfd;
	int fence_cnt;
	int ret = NOTIFY_OK;

	sync_pt_data = container_of(p, struct msm_sync_pt_data, notifier);
	mfd = container_of(sync_pt_data, struct msm_fb_data_type,
		mdp_sync_pt_data);

	switch (event) {
	case MDP_NOTIFY_FRAME_BEGIN:
		if (mfd->idle_time && !mod_delayed_work(system_wq,
					&mfd->idle_notify_work,
					msecs_to_jiffies(WAIT_DISP_OP_TIMEOUT)))
			pr_debug("fb%d: start idle delayed work\n",
					mfd->index);

		mfd->idle_state = MDSS_FB_NOT_IDLE;
		break;
	case MDP_NOTIFY_FRAME_READY:
		if (sync_pt_data->async_wait_fences &&
			sync_pt_data->temp_fen_cnt) {
			fence_cnt = sync_pt_data->temp_fen_cnt;
			sync_pt_data->temp_fen_cnt = 0;
			ret = __mdss_fb_wait_for_fence_sub(sync_pt_data,
				sync_pt_data->temp_fen, fence_cnt);
		}
		if (mfd->idle_time && !mod_delayed_work(system_wq,
					&mfd->idle_notify_work,
					msecs_to_jiffies(mfd->idle_time)))
			pr_debug("fb%d: restarted idle work\n",
					mfd->index);
		if (ret == -ETIME)
			ret = NOTIFY_BAD;
		mfd->idle_state = MDSS_FB_IDLE_TIMER_RUNNING;
		break;
	case MDP_NOTIFY_FRAME_FLUSHED:
		pr_debug("%s: frame flushed\n", sync_pt_data->fence_name);
		sync_pt_data->flushed = true;
		break;
	case MDP_NOTIFY_FRAME_TIMEOUT:
		pr_err("%s: frame timeout\n", sync_pt_data->fence_name);
		mdss_fb_signal_timeline(sync_pt_data);
		break;
	case MDP_NOTIFY_FRAME_DONE:
		pr_debug("%s: frame done\n", sync_pt_data->fence_name);
		mdss_fb_signal_timeline(sync_pt_data);
		mdss_fb_calc_fps(mfd);
		break;
	case MDP_NOTIFY_FRAME_CFG_DONE:
		if (sync_pt_data->async_wait_fences)
			__mdss_fb_copy_fence(sync_pt_data,
					sync_pt_data->temp_fen,
					&sync_pt_data->temp_fen_cnt);
		break;
	case MDP_NOTIFY_FRAME_CTX_DONE:
		mdss_fb_release_kickoff(mfd);
		break;
	}

	return ret;
}
#endif

/**
 * mdss_fb_pan_idle() - wait for panel programming to be idle
 * @mfd:	Framebuffer data structure for display
 *
 * Wait for any pending programming to be done if in the process of programming
 * hardware configuration. After this function returns it is safe to perform
 * software updates for next frame.
 */
static int mdss_fb_pan_idle(struct msm_fb_data_type *mfd)
{
	int ret = 0;

	ret = wait_event_timeout(mfd->idle_wait_q,
			(!atomic_read(&mfd->commits_pending) ||
			 mfd->shutdown_pending),
			msecs_to_jiffies(WAIT_DISP_OP_TIMEOUT));
	if (!ret) {
		pr_err("%pS: wait for idle timeout commits=%d\n",
				__builtin_return_address(0),
				atomic_read(&mfd->commits_pending));
		MDSS_XLOG_TOUT_HANDLER("mdp", "vbif", "vbif_nrt",
			"dbg_bus", "vbif_dbg_bus");
		ret = -ETIMEDOUT;
	} else if (mfd->shutdown_pending) {
		pr_debug("Shutdown signalled\n");
		ret = -ESHUTDOWN;
	} else {
		ret = 0;
	}

	return ret;
}

static int mdss_fb_wait_for_kickoff(struct msm_fb_data_type *mfd)
{
	int ret = 0;

	if (!mfd->wait_for_kickoff)
		return mdss_fb_pan_idle(mfd);

	ret = wait_event_timeout(mfd->kickoff_wait_q,
			(!atomic_read(&mfd->kickoff_pending) ||
			 mfd->shutdown_pending),
			msecs_to_jiffies(WAIT_DISP_OP_TIMEOUT));
	if (!ret) {
		pr_err("%pS: wait for kickoff timeout koff=%d commits=%d\n",
				__builtin_return_address(0),
				atomic_read(&mfd->kickoff_pending),
				atomic_read(&mfd->commits_pending));
		MDSS_XLOG_TOUT_HANDLER("mdp", "vbif", "vbif_nrt",
			"dbg_bus", "vbif_dbg_bus");
		ret = -ETIMEDOUT;
	} else if (mfd->shutdown_pending) {
		pr_debug("Shutdown signalled\n");
		ret = -ESHUTDOWN;
	} else {
		ret = 0;
	}

	return ret;
}

static int mdss_fb_pan_display_ex(struct fb_info *info,
		struct mdp_display_commit *disp_commit)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	struct fb_var_screeninfo *var = &disp_commit->var;
	u32 wait_for_finish = disp_commit->wait_for_finish;
	int ret = 0;

	if (!mfd || (!mfd->op_enable))
		return -EPERM;

	if ((mdss_fb_is_power_off(mfd)) &&
		!((mfd->dcm_state == DCM_ENTER) &&
		(mfd->panel.type == MIPI_CMD_PANEL)))
		return -EPERM;

	if (var->xoffset > (info->var.xres_virtual - info->var.xres))
		return -EINVAL;

	if (var->yoffset > (info->var.yres_virtual - info->var.yres))
		return -EINVAL;

	ret = mdss_fb_pan_idle(mfd);
	if (ret) {
		pr_err("wait_for_kick failed. rc=%d\n", ret);
		return ret;
	}

	if (mfd->mdp.pre_commit_fnc) {
		ret = mfd->mdp.pre_commit_fnc(mfd);
		if (ret) {
			pr_err("fb%d: pre commit failed %d\n",
					mfd->index, ret);
			return ret;
		}
	}

	mutex_lock(&mfd->mdp_sync_pt_data.sync_mutex);
	if (info->fix.xpanstep)
		info->var.xoffset =
		(var->xoffset / info->fix.xpanstep) * info->fix.xpanstep;

	if (info->fix.ypanstep)
		info->var.yoffset =
		(var->yoffset / info->fix.ypanstep) * info->fix.ypanstep;

	mfd->msm_fb_backup.info = *info;
	mfd->msm_fb_backup.disp_commit = *disp_commit;

	atomic_inc(&mfd->commits_pending);
	atomic_inc(&mfd->kickoff_pending);
	wake_up_all(&mfd->commit_wait_q);
	mutex_unlock(&mfd->mdp_sync_pt_data.sync_mutex);
	if (wait_for_finish) {
		ret = mdss_fb_pan_idle(mfd);
		if (ret)
			pr_err("mdss_fb_pan_idle failed. rc=%d\n", ret);
	}
	return ret;
}

u32 mdss_fb_get_mode_switch(struct msm_fb_data_type *mfd)
{
	/* If there is no attached mfd then there is no pending mode switch */
	if (!mfd)
		return 0;

	if (mfd->pending_switch)
		return mfd->switch_new_mode;

	return 0;
}

/*
 * __ioctl_transition_dyn_mode_state() - State machine for mode switch
 * @mfd:	Framebuffer data structure for display
 * @cmd:	ioctl that was called
 * @validate:	used with atomic commit when doing validate layers
 *
 * This function assists with dynamic mode switch of DSI panel. States
 * are used to make sure that panel mode switch occurs on next
 * prepare/sync/commit (for legacy) and validate/pre_commit (for
 * atomic commit) pairing. This state machine insure that calculation
 * and return values (such as buffer release fences) are based on the
 * panel mode being switching into.
 */
static int __ioctl_transition_dyn_mode_state(struct msm_fb_data_type *mfd,
		unsigned int cmd, bool validate, bool null_commit)
{
	if (mfd->switch_state == MDSS_MDP_NO_UPDATE_REQUESTED)
		return 0;

	mutex_lock(&mfd->switch_lock);
	switch (cmd) {
	case MSMFB_ATOMIC_COMMIT:
		if ((mfd->switch_state == MDSS_MDP_WAIT_FOR_VALIDATE)
				&& validate) {
			if (mfd->switch_new_mode != SWITCH_RESOLUTION)
				mfd->pending_switch = true;
			mfd->switch_state = MDSS_MDP_WAIT_FOR_COMMIT;
		} else if (mfd->switch_state == MDSS_MDP_WAIT_FOR_COMMIT) {
			if (mfd->switch_new_mode != SWITCH_RESOLUTION)
				mdss_fb_set_mdp_sync_pt_threshold(mfd,
					mfd->switch_new_mode);
			mfd->switch_state = MDSS_MDP_WAIT_FOR_KICKOFF;
		} else if ((mfd->switch_state == MDSS_MDP_WAIT_FOR_VALIDATE)
				&& null_commit) {
			mfd->switch_state = MDSS_MDP_WAIT_FOR_KICKOFF;
		}
		break;
	}
	mutex_unlock(&mfd->switch_lock);
	return 0;
}

static inline bool mdss_fb_is_wb_config_same(struct msm_fb_data_type *mfd,
		struct mdp_output_layer *output_layer)
{
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct msm_mdp_interface *mdp5_interface = &mfd->mdp;

	if (!mdp5_data->wfd
		|| (mdp5_interface->is_config_same
		&& !mdp5_interface->is_config_same(mfd, output_layer)))
		return false;
	return true;
}

/* update pinfo and var for WB on config change */
static void mdss_fb_update_resolution(struct msm_fb_data_type *mfd,
		u32 xres, u32 yres, u32 format)
{
	struct mdss_panel_info *pinfo = mfd->panel_info;
	struct fb_var_screeninfo *var = &mfd->fbi->var;
	struct fb_fix_screeninfo *fix = &mfd->fbi->fix;
	struct mdss_mdp_format_params *fmt = NULL;

	pinfo->xres = xres;
	pinfo->yres = yres;
	mfd->fb_imgType = format;
	if (mfd->mdp.get_format_params) {
		fmt = mfd->mdp.get_format_params(format);
		if (fmt) {
			pinfo->bpp = fmt->bpp;
			var->bits_per_pixel = fmt->bpp * 8;
		}
		if (mfd->mdp.fb_stride)
			fix->line_length = mfd->mdp.fb_stride(mfd->index,
						var->xres,
						var->bits_per_pixel / 8);
		else
			fix->line_length = var->xres * var->bits_per_pixel / 8;

	}
	var->xres_virtual = var->xres;
	var->yres_virtual = pinfo->yres * mfd->fb_page;
	mdss_panelinfo_to_fb_var(pinfo, var);
}

int mdss_fb_atomic_commit(struct fb_info *info,
	struct mdp_layer_commit  *commit)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	struct mdp_layer_commit_v1 *commit_v1;
	struct mdp_output_layer *output_layer;
	struct mdss_panel_info *pinfo;
	bool wait_for_finish, wb_change = false;
	int ret = -EPERM;
	u32 old_xres, old_yres, old_format;

	if (!mfd || (!mfd->op_enable)) {
		pr_err("mfd is NULL or operation not permitted\n");
		return -EPERM;
	}

	if ((mdss_fb_is_power_off(mfd)) &&
		!((mfd->dcm_state == DCM_ENTER) &&
		(mfd->panel.type == MIPI_CMD_PANEL))) {
		pr_err("commit is not supported when interface is in off state\n");
		goto end;
	}
	pinfo = mfd->panel_info;

	/* only supports version 1.0 */
	if (commit->version != MDP_COMMIT_VERSION_1_0) {
		pr_err("commit version is not supported\n");
		goto end;
	}

	if (!mfd->mdp.pre_commit || !mfd->mdp.atomic_validate) {
		pr_err("commit callback is not registered\n");
		goto end;
	}

	commit_v1 = &commit->commit_v1;
	if (commit_v1->flags & MDP_VALIDATE_LAYER) {
		ret = mdss_fb_wait_for_kickoff(mfd);
		if (ret) {
			pr_err("wait for kickoff failed\n");
		} else {
			__ioctl_transition_dyn_mode_state(mfd,
				MSMFB_ATOMIC_COMMIT, true, false);
			if (mfd->panel.type == WRITEBACK_PANEL) {
				output_layer = commit_v1->output_layer;
				if (!output_layer) {
					pr_err("Output layer is null\n");
					goto end;
				}
				wb_change = !mdss_fb_is_wb_config_same(mfd,
						commit_v1->output_layer);
				if (wb_change) {
					old_xres = pinfo->xres;
					old_yres = pinfo->yres;
					old_format = mfd->fb_imgType;
					mdss_fb_update_resolution(mfd,
						output_layer->buffer.width,
						output_layer->buffer.height,
						output_layer->buffer.format);
				}
			}
			ret = mfd->mdp.atomic_validate(mfd, commit_v1);
			if (!ret)
				mfd->atomic_commit_pending = true;
		}
		goto end;
	} else {
		ret = mdss_fb_pan_idle(mfd);
		if (ret) {
			pr_err("pan display idle call failed\n");
			goto end;
		}
		__ioctl_transition_dyn_mode_state(mfd,
			MSMFB_ATOMIC_COMMIT, false,
			(commit_v1->input_layer_cnt ? 0 : 1));

		ret = mfd->mdp.pre_commit(mfd, commit_v1);
		if (ret) {
			pr_err("atomic pre commit failed\n");
			goto end;
		}
	}

	wait_for_finish = commit_v1->flags & MDP_COMMIT_WAIT_FOR_FINISH;
	mfd->msm_fb_backup.atomic_commit = true;
	mfd->msm_fb_backup.disp_commit.l_roi =  commit_v1->left_roi;
	mfd->msm_fb_backup.disp_commit.r_roi =  commit_v1->right_roi;
	mfd->msm_fb_backup.disp_commit.flags =  commit_v1->flags;
	if (commit_v1->flags & MDP_COMMIT_UPDATE_BRIGHTNESS) {
		MDSS_BRIGHT_TO_BL(mfd->bl_extn_level, commit_v1->bl_level,
			mfd->panel_info->bl_max,
			mfd->panel_info->brightness_max);
		if (!mfd->bl_extn_level && commit_v1->bl_level)
			mfd->bl_extn_level = 1;
	} else
		mfd->bl_extn_level = U64_MAX;

	mutex_lock(&mfd->mdp_sync_pt_data.sync_mutex);
	atomic_inc(&mfd->mdp_sync_pt_data.commit_cnt);
	atomic_inc(&mfd->commits_pending);
	atomic_inc(&mfd->kickoff_pending);
	wake_up_all(&mfd->commit_wait_q);
	mutex_unlock(&mfd->mdp_sync_pt_data.sync_mutex);

	if (wait_for_finish)
		ret = mdss_fb_pan_idle(mfd);

end:
	if (ret && (mfd->panel.type == WRITEBACK_PANEL) && wb_change)
		mdss_fb_update_resolution(mfd, old_xres, old_yres, old_format);
	return ret;
}

static int mdss_fb_pan_display(struct fb_var_screeninfo *var,
		struct fb_info *info)
{
	struct mdp_display_commit disp_commit;
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;

	/*
	 * during mode switch through mode sysfs node, it will trigger a
	 * pan_display after switch. This assumes that fb has been adjusted,
	 * however when using overlays we may not have the right size at this
	 * point, so it needs to go through PREPARE first. Abort pan_display
	 * operations until that happens
	 */
	if (mfd->switch_state != MDSS_MDP_NO_UPDATE_REQUESTED) {
		pr_debug("fb%d: pan_display skipped during switch\n",
				mfd->index);
		return 0;
	}

	memset(&disp_commit, 0, sizeof(disp_commit));
	disp_commit.wait_for_finish = true;
	memcpy(&disp_commit.var, var, sizeof(struct fb_var_screeninfo));
	return mdss_fb_pan_display_ex(info, &disp_commit);
}

static int mdss_fb_pan_display_sub(struct fb_var_screeninfo *var,
			       struct fb_info *info)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;

	if (!mfd || !var) {
		pr_err("Invalid parameters mfd:%pK var:%pK\n", mfd, var);
		return -EINVAL;
	}

	if (!mfd->op_enable)
		return -EPERM;

	if ((mdss_fb_is_power_off(mfd)) &&
		!((mfd->dcm_state == DCM_ENTER) &&
		(mfd->panel.type == MIPI_CMD_PANEL)))
		return -EPERM;

	if (var->xoffset > (info->var.xres_virtual - info->var.xres))
		return -EINVAL;

	if (var->yoffset > (info->var.yres_virtual - info->var.yres))
		return -EINVAL;

	if (info->fix.xpanstep)
		info->var.xoffset =
		(var->xoffset / info->fix.xpanstep) * info->fix.xpanstep;

	if (info->fix.ypanstep)
		info->var.yoffset =
		(var->yoffset / info->fix.ypanstep) * info->fix.ypanstep;

	if (mfd->mdp.dma_fnc)
		mfd->mdp.dma_fnc(mfd);
	else
		pr_warn("dma function not set for panel type=%d\n",
				mfd->panel.type);

	return 0;
}

static int mdss_grayscale_to_mdp_format(u32 grayscale)
{
	switch (grayscale) {
	case V4L2_PIX_FMT_RGB24:
		return MDP_RGB_888;
	case V4L2_PIX_FMT_NV12:
		return MDP_Y_CBCR_H2V2;
	default:
		return -EINVAL;
	}
}

static void mdss_fb_var_to_panelinfo(struct fb_var_screeninfo *var,
	struct mdss_panel_info *pinfo)
{
	int format = -EINVAL;

	pinfo->xres = var->xres;
	pinfo->yres = var->yres;
	pinfo->lcdc.v_front_porch = var->lower_margin;
	pinfo->lcdc.v_back_porch = var->upper_margin;
	pinfo->lcdc.v_pulse_width = var->vsync_len;
	pinfo->lcdc.h_front_porch = var->right_margin;
	pinfo->lcdc.h_back_porch = var->left_margin;
	pinfo->lcdc.h_pulse_width = var->hsync_len;

	if (var->grayscale > 1) {
		format = mdss_grayscale_to_mdp_format(var->grayscale);
		if (!IS_ERR_VALUE((unsigned long) format))
			pinfo->out_format = format;
		else
			pr_warn("Failed to map grayscale value (%d) to an MDP format\n",
					var->grayscale);
	}

	/*
	 * if greater than 1M, then rate would fall below 1mhz which is not
	 * even supported. In this case it means clock rate is actually
	 * passed directly in hz.
	 */
	if (var->pixclock > SZ_1M)
		pinfo->clk_rate = var->pixclock;
	else
		pinfo->clk_rate = PICOS2KHZ(var->pixclock) * 1000;

	/*
	 * if it is a DBA panel i.e. HDMI TV connected through
	 * DSI interface, then store the pxl clock value in
	 * DSI specific variable.
	 */
	if (pinfo->is_dba_panel)
		pinfo->mipi.dsi_pclk_rate = pinfo->clk_rate;

	if (var->sync & FB_SYNC_HOR_HIGH_ACT)
		pinfo->lcdc.h_polarity = 0;
	else
		pinfo->lcdc.h_polarity = 1;

	if (var->sync & FB_SYNC_VERT_HIGH_ACT)
		pinfo->lcdc.v_polarity = 0;
	else
		pinfo->lcdc.v_polarity = 1;
}

void mdss_panelinfo_to_fb_var(struct mdss_panel_info *pinfo,
						struct fb_var_screeninfo *var)
{
	u32 frame_rate;

	var->xres = mdss_fb_get_panel_xres(pinfo);
	var->yres = pinfo->yres;
	var->lower_margin = pinfo->lcdc.v_front_porch -
		pinfo->prg_fet;
	var->upper_margin = pinfo->lcdc.v_back_porch +
		pinfo->prg_fet;
	var->vsync_len = pinfo->lcdc.v_pulse_width;
	var->right_margin = pinfo->lcdc.h_front_porch;
	var->left_margin = pinfo->lcdc.h_back_porch;
	var->hsync_len = pinfo->lcdc.h_pulse_width;

	frame_rate = mdss_panel_get_framerate(pinfo);
	if (frame_rate) {
		unsigned long clk_rate, h_total, v_total;

		h_total = var->xres + var->left_margin
			+ var->right_margin + var->hsync_len;
		v_total = var->yres + var->lower_margin
			+ var->upper_margin + var->vsync_len;
		clk_rate = h_total * v_total * frame_rate;
		var->pixclock = KHZ2PICOS(clk_rate / 1000);
	} else if (pinfo->clk_rate) {
		var->pixclock = KHZ2PICOS(
				(unsigned long) pinfo->clk_rate / 1000);
	}

	if (pinfo->physical_width)
		var->width = pinfo->physical_width;
	if (pinfo->physical_height)
		var->height = pinfo->physical_height;

	pr_debug("ScreenInfo: res=%dx%d [%d, %d] [%d, %d]\n",
		var->xres, var->yres, var->left_margin,
		var->right_margin, var->upper_margin,
		var->lower_margin);
}

/**
 * __mdss_fb_perform_commit() - process a frame to display
 * @mfd:	Framebuffer data structure for display
 *
 * Processes all layers and buffers programmed and ensures all pending release
 * fences are signaled once the buffer is transferred to display.
 */
static int __mdss_fb_perform_commit(struct msm_fb_data_type *mfd)
{
	struct msm_sync_pt_data *sync_pt_data = &mfd->mdp_sync_pt_data;
	struct msm_fb_backup_type *fb_backup = &mfd->msm_fb_backup;
	int ret = -EOPNOTSUPP;
	u32 new_dsi_mode, dynamic_dsi_switch = 0;

	if (!sync_pt_data->async_wait_fences)
		mdss_fb_wait_for_fence(sync_pt_data);
	sync_pt_data->flushed = false;

	mutex_lock(&mfd->switch_lock);
	if (mfd->switch_state == MDSS_MDP_WAIT_FOR_KICKOFF) {
		dynamic_dsi_switch = 1;
		new_dsi_mode = mfd->switch_new_mode;
	} else if (mfd->switch_state != MDSS_MDP_NO_UPDATE_REQUESTED) {
		pr_err("invalid commit on fb%d with state = %d\n",
			mfd->index, mfd->switch_state);
		mutex_unlock(&mfd->switch_lock);
		goto skip_commit;
	}
	mutex_unlock(&mfd->switch_lock);
	if (dynamic_dsi_switch) {
		MDSS_XLOG(mfd->index, mfd->split_mode, new_dsi_mode,
			XLOG_FUNC_ENTRY);
		pr_debug("Triggering dyn mode switch to %d\n", new_dsi_mode);
		ret = mfd->mdp.mode_switch(mfd, new_dsi_mode);
		if (ret)
			pr_err("DSI mode switch has failed\n");
		else
			mfd->pending_switch = false;
	}
	if (fb_backup->disp_commit.flags & MDP_DISPLAY_COMMIT_OVERLAY) {
		if (mfd->mdp.kickoff_fnc)
			ret = mfd->mdp.kickoff_fnc(mfd,
					&fb_backup->disp_commit);
		else
			pr_warn("no kickoff function setup for fb%d\n",
					mfd->index);
	} else if (fb_backup->atomic_commit) {
		if (mfd->mdp.kickoff_fnc)
			ret = mfd->mdp.kickoff_fnc(mfd,
					&fb_backup->disp_commit);
		else
			pr_warn("no kickoff function setup for fb%d\n",
				mfd->index);
		fb_backup->atomic_commit = false;
	} else {
		ret = mdss_fb_pan_display_sub(&fb_backup->disp_commit.var,
				&fb_backup->info);
		if (ret)
			pr_err("pan display failed %x on fb%d\n", ret,
					mfd->index);
	}

skip_commit:
	if (!ret)
		mdss_fb_update_backlight(mfd);

	if (IS_ERR_VALUE((unsigned long) ret) || !sync_pt_data->flushed) {
		mdss_fb_release_kickoff(mfd);
		mdss_fb_signal_timeline(sync_pt_data);

		if ((mfd->panel.type == MIPI_CMD_PANEL) &&
			(mfd->mdp.signal_retire_fence))
			mfd->mdp.signal_retire_fence(mfd, 1);
	}
	if (dynamic_dsi_switch) {
		MDSS_XLOG(mfd->index, mfd->split_mode, new_dsi_mode,
			XLOG_FUNC_EXIT);
		mfd->mdp.mode_switch_post(mfd, new_dsi_mode);
		mutex_lock(&mfd->switch_lock);
		mfd->switch_state = MDSS_MDP_NO_UPDATE_REQUESTED;
		mutex_unlock(&mfd->switch_lock);
		if (new_dsi_mode != SWITCH_RESOLUTION)
			mfd->panel.type = new_dsi_mode;
		pr_debug("Dynamic mode switch completed\n");
	}

	return ret;
}

static int __mdss_fb_display_thread(void *data)
{
	struct msm_fb_data_type *mfd = data;
	int ret;
	struct sched_param param;

	/*
	 * this priority was found during empiric testing to have appropriate
	 * realtime scheduling to process display updates and interact with
	 * other real time and normal priority tasks
	 */
	param.sched_priority = 16;
	ret = sched_setscheduler(current, SCHED_FIFO, &param);
	if (ret)
		pr_warn("set priority failed for fb%d display thread\n",
				mfd->index);

	while (1) {
		wait_event(mfd->commit_wait_q,
				(atomic_read(&mfd->commits_pending) ||
				 kthread_should_stop()));

		if (kthread_should_stop())
			break;

		MDSS_XLOG(mfd->index, XLOG_FUNC_ENTRY);
		ret = __mdss_fb_perform_commit(mfd);
		MDSS_XLOG(mfd->index, XLOG_FUNC_EXIT);

		atomic_dec(&mfd->commits_pending);
		wake_up_all(&mfd->idle_wait_q);
	}

	mdss_fb_release_kickoff(mfd);
	atomic_set(&mfd->commits_pending, 0);
	wake_up_all(&mfd->idle_wait_q);

	return ret;
}

static int mdss_fb_check_var(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;

	if (var->rotate != FB_ROTATE_UR && var->rotate != FB_ROTATE_UD)
		return -EINVAL;

	switch (var->bits_per_pixel) {
	case 16:
		if ((var->green.offset != 5) ||
		    !((var->blue.offset == 11)
		      || (var->blue.offset == 0)) ||
		    !((var->red.offset == 11)
		      || (var->red.offset == 0)) ||
		    (var->blue.length != 5) ||
		    (var->green.length != 6) ||
		    (var->red.length != 5) ||
		    (var->blue.msb_right != 0) ||
		    (var->green.msb_right != 0) ||
		    (var->red.msb_right != 0) ||
		    (var->transp.offset != 0) ||
		    (var->transp.length != 0))
			return -EINVAL;
		break;

	case 24:
		if ((var->blue.offset != 0) ||
		    (var->green.offset != 8) ||
		    (var->red.offset != 16) ||
		    (var->blue.length != 8) ||
		    (var->green.length != 8) ||
		    (var->red.length != 8) ||
		    (var->blue.msb_right != 0) ||
		    (var->green.msb_right != 0) ||
		    (var->red.msb_right != 0) ||
		    !(((var->transp.offset == 0) &&
		       (var->transp.length == 0)) ||
		      ((var->transp.offset == 24) &&
		       (var->transp.length == 8))))
			return -EINVAL;
		break;

	case 32:
		/*
		 * Check user specified color format BGRA/ARGB/RGBA
		 * and verify the position of the RGB components
		 */

		if (!((var->transp.offset == 24) &&
			(var->blue.offset == 0) &&
			(var->green.offset == 8) &&
			(var->red.offset == 16)) &&
		    !((var->transp.offset == 0) &&
			(var->blue.offset == 24) &&
			(var->green.offset == 16) &&
			(var->red.offset == 8)) &&
		    !((var->transp.offset == 24) &&
			(var->blue.offset == 16) &&
			(var->green.offset == 8) &&
			(var->red.offset == 0)))
			return -EINVAL;

		/* Check the common values for both RGBA and ARGB */

		if ((var->blue.length != 8) ||
		    (var->green.length != 8) ||
		    (var->red.length != 8) ||
		    (var->transp.length != 8) ||
		    (var->blue.msb_right != 0) ||
		    (var->green.msb_right != 0) ||
		    (var->red.msb_right != 0))
			return -EINVAL;

		break;

	default:
		return -EINVAL;
	}

	if ((var->xres_virtual <= 0) || (var->yres_virtual <= 0))
		return -EINVAL;

	if ((var->xres == 0) || (var->yres == 0))
		return -EINVAL;

	if (var->xoffset > (var->xres_virtual - var->xres))
		return -EINVAL;

	if (var->yoffset > (var->yres_virtual - var->yres))
		return -EINVAL;

	if (info->mode) {
		const struct fb_videomode *mode;

		mode = fb_match_mode(var, &info->modelist);
		if (mode == NULL)
			return -EINVAL;
	} else if (mfd->panel_info && !(var->activate & FB_ACTIVATE_TEST)) {
		struct mdss_panel_info *panel_info;
		int rc;

		panel_info = kmemdup(mfd->panel_info, sizeof(struct mdss_panel_info),
				GFP_KERNEL);
		if (!panel_info)
			return -ENOMEM;

		mdss_fb_var_to_panelinfo(var, panel_info);
		rc = mdss_fb_send_panel_event(mfd, MDSS_EVENT_CHECK_PARAMS,
			panel_info);
		if (IS_ERR_VALUE((unsigned long) rc)) {
			kfree(panel_info);
			return rc;
		}
		mfd->panel_reconfig = rc;
		kfree(panel_info);
	}

	return 0;
}

static int mdss_fb_videomode_switch(struct msm_fb_data_type *mfd,
		const struct fb_videomode *mode)
{
	int ret = 0;
	struct mdss_panel_data *pdata, *tmp;
	struct mdss_panel_timing *timing;

	pdata = dev_get_platdata(&mfd->pdev->dev);
	if (!pdata) {
		pr_err("no panel connected\n");
		return -ENODEV;
	}

	/* make sure that we are idle while switching */
	mdss_fb_wait_for_kickoff(mfd);

	pr_debug("fb%d: changing display mode to %s\n", mfd->index, mode->name);
	MDSS_XLOG(mfd->index, mode->name,
			mdss_fb_get_panel_xres(mfd->panel_info),
			mfd->panel_info->yres, mfd->split_mode,
			XLOG_FUNC_ENTRY);
	tmp = pdata;
	do {
		if (!tmp->event_handler) {
			pr_warn("no event handler for panel\n");
			continue;
		}
		timing = mdss_panel_get_timing_by_name(tmp, mode->name);
		ret = tmp->event_handler(tmp,
				MDSS_EVENT_PANEL_TIMING_SWITCH, timing);

		tmp->active = timing != NULL;
		tmp = tmp->next;
	} while (tmp && !ret);

	if (!ret)
		mdss_fb_set_split_mode(mfd, pdata);

	if (!ret && mfd->mdp.configure_panel) {
		int dest_ctrl = 1;

		/* todo: currently assumes no changes in video/cmd mode */
		if (!mdss_fb_is_power_off(mfd)) {
			mutex_lock(&mfd->switch_lock);
			mfd->switch_state = MDSS_MDP_WAIT_FOR_VALIDATE;
			mfd->switch_new_mode = SWITCH_RESOLUTION;
			mutex_unlock(&mfd->switch_lock);
			dest_ctrl = 0;
		}
		ret = mfd->mdp.configure_panel(mfd,
				pdata->panel_info.mipi.mode, dest_ctrl);
	}

	MDSS_XLOG(mfd->index, mode->name,
			mdss_fb_get_panel_xres(mfd->panel_info),
			mfd->panel_info->yres, mfd->split_mode,
			XLOG_FUNC_EXIT);
	pr_debug("fb%d: %s mode change complete\n", mfd->index, mode->name);

	return ret;
}

static int mdss_fb_set_par(struct fb_info *info)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	struct fb_var_screeninfo *var = &info->var;
	int old_imgType, old_format;
	int ret = 0;

	ret = mdss_fb_pan_idle(mfd);
	if (ret) {
		pr_err("mdss_fb_pan_idle failed. rc=%d\n", ret);
		return ret;
	}

	old_imgType = mfd->fb_imgType;
	switch (var->bits_per_pixel) {
	case 16:
		if (var->red.offset == 0)
			mfd->fb_imgType = MDP_BGR_565;
		else
			mfd->fb_imgType	= MDP_RGB_565;
		break;

	case 24:
		if ((var->transp.offset == 0) && (var->transp.length == 0))
			mfd->fb_imgType = MDP_RGB_888;
		else if ((var->transp.offset == 24) &&
			 (var->transp.length == 8)) {
			mfd->fb_imgType = MDP_ARGB_8888;
			info->var.bits_per_pixel = 32;
		}
		break;

	case 32:
		if ((var->red.offset == 0) &&
		    (var->green.offset == 8) &&
		    (var->blue.offset == 16) &&
		    (var->transp.offset == 24))
			mfd->fb_imgType = MDP_RGBA_8888;
		else if ((var->red.offset == 16) &&
		    (var->green.offset == 8) &&
		    (var->blue.offset == 0) &&
		    (var->transp.offset == 24))
			mfd->fb_imgType = MDP_BGRA_8888;
		else if ((var->red.offset == 8) &&
		    (var->green.offset == 16) &&
		    (var->blue.offset == 24) &&
		    (var->transp.offset == 0))
			mfd->fb_imgType = MDP_ARGB_8888;
		else
			mfd->fb_imgType = MDP_RGBA_8888;
		break;

	default:
		return -EINVAL;
	}

	if (info->mode) {
		const struct fb_videomode *mode;

		mode = fb_match_mode(var, &info->modelist);
		if (!mode)
			return -EINVAL;

		pr_debug("found mode: %s\n", mode->name);

		if (fb_mode_is_equal(mode, info->mode)) {
			pr_debug("mode is equal to current mode\n");
			return 0;
		}

		ret = mdss_fb_videomode_switch(mfd, mode);
		if (ret)
			return ret;
	}

	if (mfd->mdp.fb_stride)
		mfd->fbi->fix.line_length = mfd->mdp.fb_stride(mfd->index,
						var->xres,
						var->bits_per_pixel / 8);
	else
		mfd->fbi->fix.line_length = var->xres * var->bits_per_pixel / 8;

	/* if memory is not allocated yet, change memory size for fb */
	if (!info->fix.smem_start)
		mfd->fbi->fix.smem_len = PAGE_ALIGN(mfd->fbi->fix.line_length *
				mfd->fbi->var.yres) * mfd->fb_page;

	old_format = mdss_grayscale_to_mdp_format(var->grayscale);
	if (!IS_ERR_VALUE((unsigned long) old_format)) {
		if (old_format != mfd->panel_info->out_format)
			mfd->panel_reconfig = true;
	}

	if (mfd->panel_reconfig || (mfd->fb_imgType != old_imgType)) {
		mdss_fb_blank_sub(FB_BLANK_POWERDOWN, info, mfd->op_enable);
		mdss_fb_var_to_panelinfo(var, mfd->panel_info);
		mdss_fb_blank_sub(FB_BLANK_UNBLANK, info, mfd->op_enable);
		mfd->panel_reconfig = false;
	}

	return ret;
}

int mdss_fb_dcm(struct msm_fb_data_type *mfd, int req_state)
{
	int ret = 0;

	if (req_state == mfd->dcm_state) {
		pr_warn("Already in correct DCM/DTM state\n");
		return ret;
	}

	switch (req_state) {
	case DCM_UNBLANK:
		if (mfd->dcm_state == DCM_UNINIT &&
			mdss_fb_is_power_off(mfd) && mfd->mdp.on_fnc) {
			if (mfd->disp_thread == NULL) {
				ret = mdss_fb_start_disp_thread(mfd);
				if (ret < 0)
					return ret;
			}
			ret = mfd->mdp.on_fnc(mfd);
			if (ret == 0) {
				mfd->panel_power_state = MDSS_PANEL_POWER_ON;
				mfd->dcm_state = DCM_UNBLANK;
			}
		}
		break;
	case DCM_ENTER:
		if (mfd->dcm_state == DCM_UNBLANK) {
			/*
			 * Keep unblank path available for only
			 * DCM operation
			 */
			mfd->panel_power_state = MDSS_PANEL_POWER_OFF;
			mfd->dcm_state = DCM_ENTER;
		}
		break;
	case DCM_EXIT:
		if (mfd->dcm_state == DCM_ENTER) {
			/* Release the unblank path for exit */
			mfd->panel_power_state = MDSS_PANEL_POWER_ON;
			mfd->dcm_state = DCM_EXIT;
		}
		break;
	case DCM_BLANK:
		if ((mfd->dcm_state == DCM_EXIT ||
			mfd->dcm_state == DCM_UNBLANK) &&
			mdss_fb_is_power_on(mfd) && mfd->mdp.off_fnc) {
			mfd->panel_power_state = MDSS_PANEL_POWER_OFF;
			ret = mfd->mdp.off_fnc(mfd);
			if (ret == 0)
				mfd->dcm_state = DCM_UNINIT;
			else
				pr_err("DCM_BLANK failed\n");

			if (mfd->disp_thread)
				mdss_fb_stop_disp_thread(mfd);
		}
		break;
	case DTM_ENTER:
		if (mfd->dcm_state == DCM_UNINIT)
			mfd->dcm_state = DTM_ENTER;
		break;
	case DTM_EXIT:
		if (mfd->dcm_state == DTM_ENTER)
			mfd->dcm_state = DCM_UNINIT;
		break;
	}

	return ret;
}

static int mdss_fb_cursor(struct fb_info *info, void __user *p)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	struct fb_cursor cursor;
	int ret;

	if (!mfd->mdp.cursor_update)
		return -ENODEV;

	ret = copy_from_user(&cursor, p, sizeof(cursor));
	if (ret)
		return ret;

	return mfd->mdp.cursor_update(mfd, &cursor);
}

int mdss_fb_async_position_update(struct fb_info *info,
		struct mdp_position_update *update_pos)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;

	if (!update_pos->input_layer_cnt) {
		pr_err("no input layers for position update\n");
		return -EINVAL;
	}
	return mfd->mdp.async_position_update(mfd, update_pos);
}

static int mdss_fb_async_position_update_ioctl(struct fb_info *info,
		unsigned long *argp)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	struct mdp_position_update update_pos;
	int ret, rc;
	u32 buffer_size, layer_cnt;
	struct mdp_async_layer *layer_list = NULL;
	struct mdp_async_layer __user *input_layer_list;

	if (!mfd->mdp.async_position_update)
		return -ENODEV;

	ret = copy_from_user(&update_pos, argp, sizeof(update_pos));
	if (ret) {
		pr_err("copy from user failed\n");
		return ret;
	}
	input_layer_list = update_pos.input_layers;

	layer_cnt = update_pos.input_layer_cnt;
	if ((!layer_cnt) || (layer_cnt > MAX_LAYER_COUNT)) {
		pr_err("invalid async layers :%d to update\n", layer_cnt);
		return -EINVAL;
	}

	buffer_size = sizeof(struct mdp_async_layer) * layer_cnt;
	layer_list = kmalloc(buffer_size, GFP_KERNEL);
	if (!layer_list) {
		pr_err("unable to allocate memory for layers\n");
		return -ENOMEM;
	}

	ret = copy_from_user(layer_list, input_layer_list, buffer_size);
	if (ret) {
		pr_err("layer list copy from user failed\n");
		goto end;
	}
	update_pos.input_layers = layer_list;

	ret = mdss_fb_async_position_update(info, &update_pos);
	if (ret)
		pr_err("async position update failed ret:%d\n", ret);

	rc = copy_to_user(input_layer_list, layer_list, buffer_size);
	if (rc)
		pr_err("layer error code copy to user failed\n");

	update_pos.input_layers = input_layer_list;
	rc = copy_to_user(argp, &update_pos,
			sizeof(struct mdp_position_update));
	if (rc)
		pr_err("copy to user for layers failed\n");

end:
	kfree(layer_list);
	return ret;
}

static int mdss_fb_set_lut(struct fb_info *info, void __user *p)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	struct fb_cmap cmap;
	int ret;

	if (!mfd->mdp.lut_update)
		return -ENODEV;

	ret = copy_from_user(&cmap, p, sizeof(cmap));
	if (ret)
		return ret;

	mfd->mdp.lut_update(mfd, &cmap);
	return 0;
}

/**
 * mdss_fb_sync_get_fence() - get fence from timeline
 * @timeline:	Timeline to create the fence on
 * @fence_name:	Name of the fence that will be created for debugging
 * @val:	Timeline value at which the fence will be signaled
 *
 * Function returns a fence on the timeline given with the name provided.
 * The fence created will be signaled when the timeline is advanced.
 */
struct mdss_fence *mdss_fb_sync_get_fence(struct mdss_timeline *timeline,
		const char *fence_name, int val)
{
	struct mdss_fence *fence;


	fence = mdss_get_sync_fence(timeline, fence_name, NULL, val);
	pr_debug("%s: buf sync fence timeline=%d\n",
		 mdss_get_sync_fence_name(fence), val);
	if (fence == NULL) {
		pr_err("%s: cannot create fence\n", fence_name);
		return NULL;
	}

	return fence;
}

static int mdss_fb_handle_buf_sync_ioctl(struct msm_sync_pt_data *sync_pt_data,
				 struct mdp_buf_sync *buf_sync)
{
	int i, ret = 0;
	int acq_fen_fd[MDP_MAX_FENCE_FD];
	struct mdss_fence *fence, *rel_fence, *retire_fence;
	int rel_fen_fd;
	int retire_fen_fd;
	int val;

	if ((buf_sync->acq_fen_fd_cnt > MDP_MAX_FENCE_FD) ||
				(sync_pt_data->timeline == NULL))
		return -EINVAL;

	if (buf_sync->acq_fen_fd_cnt)
		ret = copy_from_user(acq_fen_fd, buf_sync->acq_fen_fd,
				buf_sync->acq_fen_fd_cnt * sizeof(int));
	if (ret) {
		pr_err("%s: copy_from_user failed\n", sync_pt_data->fence_name);
		return ret;
	}

	i = mdss_fb_wait_for_fence(sync_pt_data);
	if (i > 0)
		pr_warn("%s: waited on %d active fences\n",
				sync_pt_data->fence_name, i);

	mutex_lock(&sync_pt_data->sync_mutex);
	for (i = 0; i < buf_sync->acq_fen_fd_cnt; i++) {
		fence = mdss_get_fd_sync_fence(acq_fen_fd[i]);
		if (fence == NULL) {
			pr_err("%s: null fence! i=%d fd=%d\n",
					sync_pt_data->fence_name, i,
					acq_fen_fd[i]);
			ret = -EINVAL;
			break;
		}
		sync_pt_data->acq_fen[i] = fence;
	}
	sync_pt_data->acq_fen_cnt = i;
	if (ret)
		goto buf_sync_err_1;

	val = sync_pt_data->threshold +
			atomic_read(&sync_pt_data->commit_cnt);

	MDSS_XLOG(sync_pt_data->timeline_value, val,
		atomic_read(&sync_pt_data->commit_cnt));
	pr_debug("%s: fence CTL%d Commit_cnt%d\n", sync_pt_data->fence_name,
		sync_pt_data->timeline_value,
		atomic_read(&sync_pt_data->commit_cnt));
	/* Set release fence */
	rel_fence = mdss_fb_sync_get_fence(sync_pt_data->timeline,
			sync_pt_data->fence_name, val);
	if (IS_ERR_OR_NULL(rel_fence)) {
		pr_err("%s: unable to retrieve release fence\n",
				sync_pt_data->fence_name);
		ret = rel_fence ? PTR_ERR(rel_fence) : -ENOMEM;
		goto buf_sync_err_1;
	}

	/* create fd */
	rel_fen_fd = mdss_get_sync_fence_fd(rel_fence);
	if (rel_fen_fd < 0) {
		pr_err("%s: get_unused_fd_flags failed error:0x%x\n",
				sync_pt_data->fence_name, rel_fen_fd);
		ret = rel_fen_fd;
		goto buf_sync_err_2;
	}

	ret = copy_to_user(buf_sync->rel_fen_fd, &rel_fen_fd, sizeof(int));
	if (ret) {
		pr_err("%s: copy_to_user failed\n", sync_pt_data->fence_name);
		goto buf_sync_err_3;
	}

	if (!(buf_sync->flags & MDP_BUF_SYNC_FLAG_RETIRE_FENCE))
		goto skip_retire_fence;

	if (sync_pt_data->get_retire_fence)
		retire_fence = sync_pt_data->get_retire_fence(sync_pt_data);
	else
		retire_fence = NULL;

	if (IS_ERR_OR_NULL(retire_fence)) {
		val += sync_pt_data->retire_threshold;
		retire_fence = mdss_fb_sync_get_fence(
			sync_pt_data->timeline, "mdp-retire", val);
	}

	if (IS_ERR_OR_NULL(retire_fence)) {
		pr_err("%s: unable to retrieve retire fence\n",
				sync_pt_data->fence_name);
		ret = retire_fence ? PTR_ERR(rel_fence) : -ENOMEM;
		goto buf_sync_err_3;
	}
	retire_fen_fd = mdss_get_sync_fence_fd(retire_fence);

	if (retire_fen_fd < 0) {
		pr_err("%s: get_unused_fd_flags failed for retire fence error:0x%x\n",
				sync_pt_data->fence_name, retire_fen_fd);
		ret = retire_fen_fd;
		mdss_put_sync_fence(retire_fence);
		goto buf_sync_err_3;
	}

	ret = copy_to_user(buf_sync->retire_fen_fd, &retire_fen_fd,
			sizeof(int));
	if (ret) {
		pr_err("%s: copy_to_user failed for retire fence\n",
				sync_pt_data->fence_name);
		put_unused_fd(retire_fen_fd);
		mdss_put_sync_fence(retire_fence);
		goto buf_sync_err_3;
	}

skip_retire_fence:
	mdss_get_sync_fence_fd(rel_fence);
	mutex_unlock(&sync_pt_data->sync_mutex);

	if (buf_sync->flags & MDP_BUF_SYNC_FLAG_WAIT)
		mdss_fb_wait_for_fence(sync_pt_data);

	return ret;
buf_sync_err_3:
	put_unused_fd(rel_fen_fd);
buf_sync_err_2:
	mdss_put_sync_fence(rel_fence);
buf_sync_err_1:
	for (i = 0; i < sync_pt_data->acq_fen_cnt; i++)
		mdss_put_sync_fence(sync_pt_data->acq_fen[i]);
	sync_pt_data->acq_fen_cnt = 0;
	mutex_unlock(&sync_pt_data->sync_mutex);
	return ret;
}
static int mdss_fb_display_commit(struct fb_info *info,
						unsigned long *argp)
{
	int ret;
	struct mdp_display_commit disp_commit;

	ret = copy_from_user(&disp_commit, argp,
			sizeof(disp_commit));
	if (ret) {
		pr_err("%s:copy_from_user failed\n", __func__);
		return ret;
	}
	ret = mdss_fb_pan_display_ex(info, &disp_commit);
	return ret;
}

/**
 * __mdss_fb_copy_pixel_ext() - copy pxl extension payload
 * @src: pxl extn structure
 * @dest: Qseed3/pxl extn common payload
 *
 * Function copies the pxl extension parameters into the scale data structure,
 * this is required to allow using the scale_v2 data structure for both
 * QSEED2 and QSEED3
 */
static void __mdss_fb_copy_pixel_ext(struct mdp_scale_data *src,
					struct mdp_scale_data_v2 *dest)
{
	if (!src || !dest)
		return;
	dest->enable = true;
	memcpy(dest->init_phase_x, src->init_phase_x,
		sizeof(src->init_phase_x));
	memcpy(dest->phase_step_x, src->phase_step_x,
		sizeof(src->init_phase_x));
	memcpy(dest->init_phase_y, src->init_phase_y,
		sizeof(src->init_phase_x));
	memcpy(dest->phase_step_y, src->phase_step_y,
		sizeof(src->init_phase_x));

	memcpy(dest->num_ext_pxls_left, src->num_ext_pxls_left,
		sizeof(src->num_ext_pxls_left));
	memcpy(dest->num_ext_pxls_right, src->num_ext_pxls_right,
		sizeof(src->num_ext_pxls_right));
	memcpy(dest->num_ext_pxls_top, src->num_ext_pxls_top,
		sizeof(src->num_ext_pxls_top));
	memcpy(dest->num_ext_pxls_btm, src->num_ext_pxls_btm,
		sizeof(src->num_ext_pxls_btm));

	memcpy(dest->left_ftch, src->left_ftch, sizeof(src->left_ftch));
	memcpy(dest->left_rpt, src->left_rpt, sizeof(src->left_rpt));
	memcpy(dest->right_ftch, src->right_ftch, sizeof(src->right_ftch));
	memcpy(dest->right_rpt, src->right_rpt, sizeof(src->right_rpt));


	memcpy(dest->top_rpt, src->top_rpt, sizeof(src->top_rpt));
	memcpy(dest->btm_rpt, src->btm_rpt, sizeof(src->btm_rpt));
	memcpy(dest->top_ftch, src->top_ftch, sizeof(src->top_ftch));
	memcpy(dest->btm_ftch, src->btm_ftch, sizeof(src->btm_ftch));

	memcpy(dest->roi_w, src->roi_w, sizeof(src->roi_w));
}

static int __mdss_fb_scaler_handler(struct mdp_input_layer *layer)
{
	int ret = 0;
	struct mdp_scale_data *pixel_ext = NULL;
	struct mdp_scale_data_v2 *scale = NULL;

	if ((layer->flags & MDP_LAYER_ENABLE_PIXEL_EXT) &&
			(layer->flags & MDP_LAYER_ENABLE_QSEED3_SCALE)) {
		pr_err("Invalid flag configuration for scaler, %x\n",
				layer->flags);
		ret = -EINVAL;
		goto err;
	}

	if (layer->flags & MDP_LAYER_ENABLE_PIXEL_EXT) {
		scale = kzalloc(sizeof(struct mdp_scale_data_v2),
				GFP_KERNEL);
		pixel_ext = kzalloc(sizeof(struct mdp_scale_data),
				GFP_KERNEL);
		if (!scale || !pixel_ext) {
			mdss_mdp_free_layer_pp_info(layer);
			ret = -ENOMEM;
			goto err;
		}
		ret = copy_from_user(pixel_ext, layer->scale,
				sizeof(struct mdp_scale_data));
		if (ret) {
			mdss_mdp_free_layer_pp_info(layer);
			ret = -EFAULT;
			goto err;
		}
		__mdss_fb_copy_pixel_ext(pixel_ext, scale);
		layer->scale = scale;
	} else if (layer->flags & MDP_LAYER_ENABLE_QSEED3_SCALE) {
		scale = kzalloc(sizeof(struct mdp_scale_data_v2),
				GFP_KERNEL);
		if (!scale) {
			mdss_mdp_free_layer_pp_info(layer);
			ret =  -ENOMEM;
			goto err;
		}

		ret = copy_from_user(scale, layer->scale,
				sizeof(struct mdp_scale_data_v2));
		if (ret) {
			mdss_mdp_free_layer_pp_info(layer);
			ret = -EFAULT;
			goto err;
		}
		layer->scale = scale;
	} else {
		layer->scale = NULL;
	}
	kfree(pixel_ext);
	return ret;
err:
	kfree(pixel_ext);
	kfree(scale);
	layer->scale = NULL;
	return ret;
}

static int __mdss_fb_copy_destscaler_data(struct fb_info *info,
		struct mdp_layer_commit *commit)
{
	int    i = 0;
	int    ret = 0;
	u32    data_size;
	struct mdp_destination_scaler_data __user *ds_data_user;
	struct mdp_destination_scaler_data *ds_data = NULL;
	void __user *scale_data_user;
	struct mdp_scale_data_v2 *scale_data = NULL;
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	struct mdss_data_type *mdata;

	if (!mfd || !mfd->mdp.private1) {
		pr_err("mfd is NULL or operation not permitted\n");
		ret = -EINVAL;
		goto err;
	}

	mdata = mfd_to_mdata(mfd);
	if (!mdata) {
		pr_err("mdata is NULL or not initialized\n");
		ret = -EINVAL;
		goto err;
	}

	if (commit->commit_v1.dest_scaler_cnt >
			mdata->scaler_off->ndest_scalers) {
		pr_err("Commit destination scaler cnt larger than HW setting, commit cnt=%d\n",
				commit->commit_v1.dest_scaler_cnt);
		ret = -EINVAL;
		goto err;
	}

	ds_data_user = (struct mdp_destination_scaler_data *)
		commit->commit_v1.dest_scaler;
	data_size = commit->commit_v1.dest_scaler_cnt *
		sizeof(struct mdp_destination_scaler_data);
	ds_data = kzalloc(data_size, GFP_KERNEL);
	if (!ds_data) {
		ret = -ENOMEM;
		goto err;
	}

	ret = copy_from_user(ds_data, ds_data_user, data_size);
	if (ret) {
		pr_err("dest scaler data copy from user failed\n");
		goto err;
	}

	commit->commit_v1.dest_scaler = ds_data;

	for (i = 0; i < commit->commit_v1.dest_scaler_cnt; i++) {
		scale_data = NULL;

		if (ds_data[i].scale) {
			scale_data_user = to_user_ptr(ds_data[i].scale);
			data_size = sizeof(struct mdp_scale_data_v2);

			scale_data = kzalloc(data_size, GFP_KERNEL);
			if (!scale_data) {
				ds_data[i].scale = 0;
				ret = -ENOMEM;
				goto err;
			}

			ds_data[i].scale = to_user_u64(scale_data);
		}

		if (scale_data && (ds_data[i].flags &
					(MDP_DESTSCALER_SCALE_UPDATE |
					MDP_DESTSCALER_ENHANCER_UPDATE))) {
			ret = copy_from_user(scale_data, scale_data_user,
					data_size);
			if (ret) {
				pr_err("scale data copy from user failed\n");
				kfree(scale_data);
				goto err;
			}
		}
	}

	return ret;

err:
	if (ds_data) {
		for (i--; i >= 0; i--) {
			scale_data = to_user_ptr(ds_data[i].scale);
			kfree(scale_data);
		}
		kfree(ds_data);
	}

	return ret;
}

static int mdss_fb_atomic_commit_ioctl(struct fb_info *info,
	unsigned long *argp)
{
	int ret, i = 0, j = 0, rc;
	struct mdp_layer_commit  commit;
	u32 buffer_size, layer_count;
	struct mdp_input_layer *layer, *layer_list = NULL;
	struct mdp_input_layer __user *input_layer_list;
	struct mdp_output_layer *output_layer = NULL;
	struct mdp_output_layer __user *output_layer_user;
	struct mdp_destination_scaler_data *ds_data = NULL;
	struct mdp_destination_scaler_data __user *ds_data_user;
	struct msm_fb_data_type *mfd;
	struct mdss_overlay_private *mdp5_data = NULL;
	struct mdss_data_type *mdata;

	ret = copy_from_user(&commit, argp, sizeof(struct mdp_layer_commit));
	if (ret) {
		pr_err("%s:copy_from_user failed\n", __func__);
		return ret;
	}

	mfd = (struct msm_fb_data_type *)info->par;
	if (!mfd)
		return -EINVAL;

	mdp5_data = mfd_to_mdp5_data(mfd);

	if (mfd->panel_info->panel_dead) {
		pr_debug("early commit return\n");
		MDSS_XLOG(mfd->panel_info->panel_dead);
		/*
		 * In case of an ESD attack, since we early return from the
		 * commits, we need to signal the outstanding fences.
		 */
		mdss_fb_release_fences(mfd);
		if ((mfd->panel.type == MIPI_CMD_PANEL) &&
			mfd->mdp.signal_retire_fence && mdp5_data)
			mfd->mdp.signal_retire_fence(mfd,
						mdp5_data->retire_cnt);
		return 0;
	}

	output_layer_user = commit.commit_v1.output_layer;
	if (output_layer_user) {
		buffer_size = sizeof(struct mdp_output_layer);
		output_layer = kzalloc(buffer_size, GFP_KERNEL);
		if (!output_layer) {
			pr_err("unable to allocate memory for output layer\n");
			return -ENOMEM;
		}

		ret = copy_from_user(output_layer,
			output_layer_user, buffer_size);
		if (ret) {
			pr_err("layer list copy from user failed\n");
			goto err;
		}
		commit.commit_v1.output_layer = output_layer;
	}

	layer_count = commit.commit_v1.input_layer_cnt;
	input_layer_list = commit.commit_v1.input_layers;

	if (layer_count > MAX_LAYER_COUNT) {
		pr_err("invalid layer count :%d\n", layer_count);
		ret = -EINVAL;
		goto err;
	} else if (layer_count) {
		buffer_size = sizeof(struct mdp_input_layer) * layer_count;
		layer_list = kzalloc(buffer_size, GFP_KERNEL);
		if (!layer_list) {
			pr_err("unable to allocate memory for layers\n");
			ret = -ENOMEM;
			goto err;
		}

		ret = copy_from_user(layer_list, input_layer_list, buffer_size);
		if (ret) {
			pr_err("layer list copy from user failed\n");
			goto err;
		}

		commit.commit_v1.input_layers = layer_list;

		for (i = 0; i < layer_count; i++) {
			layer = &layer_list[i];

			if (!(layer->flags & MDP_LAYER_PP)) {
				layer->pp_info = NULL;
			} else {
				ret = mdss_mdp_copy_layer_pp_info(layer);
				if (ret) {
					pr_err("failure to copy pp_info data for layer %d, ret = %d\n",
						i, ret);
					goto err;
				}
			}

			if ((layer->flags & MDP_LAYER_ENABLE_PIXEL_EXT) ||
				(layer->flags &
				 MDP_LAYER_ENABLE_QSEED3_SCALE)) {
				ret = __mdss_fb_scaler_handler(layer);
				if (ret) {
					pr_err("failure to copy scale params for layer %d, ret = %d\n",
						i, ret);
					goto err;
				}
			} else {
				layer->scale = NULL;
			}
		}
	}

	ds_data_user = commit.commit_v1.dest_scaler;
	if ((ds_data_user) &&
		(commit.commit_v1.dest_scaler_cnt)) {
		mdata = mfd_to_mdata(mfd);
		if (!mdata || !mdata->scaler_off ||
				 !mdata->scaler_off->has_dest_scaler) {
			pr_err("dest scaler not supported\n");
			ret = -EPERM;
			goto err;
		}
		ret = __mdss_fb_copy_destscaler_data(info, &commit);
		if (ret) {
			pr_err("copy dest scaler failed\n");
			goto err;
		}
		ds_data = commit.commit_v1.dest_scaler;
	}

	ATRACE_BEGIN("ATOMIC_COMMIT");
	ret = mdss_fb_atomic_commit(info, &commit);
	if (ret)
		pr_err("atomic commit failed ret:%d\n", ret);
	ATRACE_END("ATOMIC_COMMIT");

	if (layer_count) {
		for (j = 0; j < layer_count; j++) {
			rc = copy_to_user(&input_layer_list[j].error_code,
					&layer_list[j].error_code, sizeof(int));
			if (rc)
				pr_err("layer error code copy to user failed\n");
		}

		commit.commit_v1.input_layers = input_layer_list;
		commit.commit_v1.output_layer = output_layer_user;
		commit.commit_v1.dest_scaler  = ds_data_user;
		rc = copy_to_user(argp, &commit,
			sizeof(struct mdp_layer_commit));
		if (rc) {
			pr_err("copy to user for release & retire fence failed\n");
			goto err;
		}
	}

	if (output_layer_user) {
		rc = copy_to_user(&output_layer_user->buffer.fence,
			&output_layer->buffer.fence,
			sizeof(int));

		if (rc)
			pr_err("copy to user for output fence failed\n");
	}

err:
	for (i--; i >= 0; i--) {
		kfree(layer_list[i].scale);
		layer_list[i].scale = NULL;
		mdss_mdp_free_layer_pp_info(&layer_list[i]);
	}
	kfree(layer_list);
	kfree(output_layer);
	if (ds_data) {
		for (i = 0; i < commit.commit_v1.dest_scaler_cnt; i++)
			kfree(to_user_ptr(ds_data[i].scale));
		kfree(ds_data);
	}

	return ret;
}

int mdss_fb_switch_check(struct msm_fb_data_type *mfd, u32 mode)
{
	struct mdss_panel_info *pinfo = NULL;
	int panel_type;

	if (!mfd || !mfd->panel_info)
		return -EINVAL;

	pinfo = mfd->panel_info;

	if ((!mfd->op_enable) || (mdss_fb_is_power_off(mfd)))
		return -EPERM;

	if (pinfo->mipi.dms_mode != DYNAMIC_MODE_SWITCH_IMMEDIATE) {
		pr_warn("Panel does not support immediate dynamic switch!\n");
		return -EPERM;
	}

	if (mfd->dcm_state != DCM_UNINIT) {
		pr_warn("Switch not supported during DCM!\n");
		return -EPERM;
	}

	mutex_lock(&mfd->switch_lock);
	if (mode == pinfo->type) {
		pr_debug("Already in requested mode!\n");
		mutex_unlock(&mfd->switch_lock);
		return -EPERM;
	}
	mutex_unlock(&mfd->switch_lock);

	panel_type = mfd->panel.type;
	if (panel_type != MIPI_VIDEO_PANEL && panel_type != MIPI_CMD_PANEL) {
		pr_debug("Panel not in mipi video or cmd mode, cannot change\n");
		return -EPERM;
	}

	return 0;
}

static int mdss_fb_immediate_mode_switch(struct msm_fb_data_type *mfd, u32 mode)
{
	int ret;
	u32 tranlated_mode;

	if (mode)
		tranlated_mode = MIPI_CMD_PANEL;
	else
		tranlated_mode = MIPI_VIDEO_PANEL;

	pr_debug("%s: Request to switch to %d\n", __func__, tranlated_mode);

	ret = mdss_fb_switch_check(mfd, tranlated_mode);
	if (ret)
		return ret;

	mutex_lock(&mfd->switch_lock);
	if (mfd->switch_state != MDSS_MDP_NO_UPDATE_REQUESTED) {
		pr_err("%s: Mode switch already in progress\n", __func__);
		ret = -EAGAIN;
		goto exit;
	}
	mfd->switch_state = MDSS_MDP_WAIT_FOR_VALIDATE;
	mfd->switch_new_mode = tranlated_mode;

exit:
	mutex_unlock(&mfd->switch_lock);
	return ret;
}

/*
 * mdss_fb_mode_switch() - Function to change DSI mode
 * @mfd:	Framebuffer data structure for display
 * @mode:	Enabled/Disable LowPowerMode
 *		1: Switch to Command Mode
 *		0: Switch to video Mode
 *
 * This function is used to change from DSI mode based on the
 * argument @mode on the next frame to be displayed.
 */
static int mdss_fb_mode_switch(struct msm_fb_data_type *mfd, u32 mode)
{
	struct mdss_panel_info *pinfo = NULL;
	int ret = 0;

	if (!mfd || !mfd->panel_info)
		return -EINVAL;

	/* make sure that we are idle while switching */
	mdss_fb_wait_for_kickoff(mfd);

	pinfo = mfd->panel_info;
	if (pinfo->mipi.dms_mode == DYNAMIC_MODE_SWITCH_SUSPEND_RESUME) {
		ret = mdss_fb_blanking_mode_switch(mfd, mode);
	} else if (pinfo->mipi.dms_mode == DYNAMIC_MODE_SWITCH_IMMEDIATE) {
		ret = mdss_fb_immediate_mode_switch(mfd, mode);
	} else {
		pr_warn("Panel does not support dynamic mode switch!\n");
		ret = -EPERM;
	}

	return ret;
}

static int __ioctl_wait_idle(struct msm_fb_data_type *mfd, u32 cmd)
{
	int ret = 0;

	if (mfd->wait_for_kickoff &&
		((cmd == MSMFB_OVERLAY_PREPARE) ||
		(cmd == MSMFB_BUFFER_SYNC) ||
		(cmd == MSMFB_OVERLAY_PLAY) ||
		(cmd == MSMFB_CURSOR) ||
		(cmd == MSMFB_METADATA_GET) ||
		(cmd == MSMFB_METADATA_SET) ||
		(cmd == MSMFB_OVERLAY_GET) ||
		(cmd == MSMFB_OVERLAY_UNSET) ||
		(cmd == MSMFB_OVERLAY_SET))) {
		ret = mdss_fb_wait_for_kickoff(mfd);
	}

	if (ret && (ret != -ESHUTDOWN))
		pr_err("wait_idle failed. cmd=0x%x rc=%d\n", cmd, ret);

	return ret;
}

static bool check_not_supported_ioctl(u32 cmd)
{
	return((cmd == MSMFB_OVERLAY_SET) || (cmd == MSMFB_OVERLAY_UNSET) ||
		(cmd == MSMFB_OVERLAY_GET) || (cmd == MSMFB_OVERLAY_PREPARE) ||
		(cmd == MSMFB_DISPLAY_COMMIT) || (cmd == MSMFB_OVERLAY_PLAY) ||
		(cmd == MSMFB_BUFFER_SYNC) || (cmd == MSMFB_OVERLAY_QUEUE) ||
		(cmd == MSMFB_NOTIFY_UPDATE));
}

/*
 * mdss_fb_do_ioctl() - MDSS Framebuffer ioctl function
 * @info:	pointer to framebuffer info
 * @cmd:	ioctl command
 * @arg:	argument to ioctl
 *
 * This function provides an architecture agnostic implementation
 * of the mdss framebuffer ioctl. This function can be called
 * by compat ioctl or regular ioctl to handle the supported commands.
 */
int mdss_fb_do_ioctl(struct fb_info *info, unsigned int cmd,
			 unsigned long arg)
{
	struct msm_fb_data_type *mfd;
	void __user *argp = (void __user *)arg;
	int ret = -EOPNOTSUPP;
	struct mdp_buf_sync buf_sync;
	unsigned int dsi_mode = 0;
	struct mdss_panel_data *pdata = NULL;

	if (!info || !info->par)
		return -EINVAL;

	mfd = (struct msm_fb_data_type *)info->par;
	if (!mfd)
		return -EINVAL;

	if (mfd->shutdown_pending)
		return -ESHUTDOWN;

	pdata = dev_get_platdata(&mfd->pdev->dev);
	if (!pdata || pdata->panel_info.dynamic_switch_pending)
		return -EPERM;

	if (check_not_supported_ioctl(cmd)) {
		pr_err("Unsupported ioctl\n");
		return -EINVAL;
	}

	atomic_inc(&mfd->ioctl_ref_cnt);

	mdss_fb_power_setting_idle(mfd);

	ret = __ioctl_wait_idle(mfd, cmd);
	if (ret)
		goto exit;

	switch (cmd) {
	case MSMFB_CURSOR:
		ret = mdss_fb_cursor(info, argp);
		break;

	case MSMFB_SET_LUT:
		ret = mdss_fb_set_lut(info, argp);
		break;

	case MSMFB_BUFFER_SYNC:
		ret = copy_from_user(&buf_sync, argp, sizeof(buf_sync));
		if (ret)
			goto exit;

		if ((!mfd->op_enable) || (mdss_fb_is_power_off(mfd))) {
			ret = -EPERM;
			goto exit;
		}

		ret = mdss_fb_handle_buf_sync_ioctl(&mfd->mdp_sync_pt_data,
				&buf_sync);
		if (!ret)
			ret = copy_to_user(argp, &buf_sync, sizeof(buf_sync));
		break;

	case MSMFB_NOTIFY_UPDATE:
		ret = mdss_fb_notify_update(mfd, argp);
		break;

	case MSMFB_DISPLAY_COMMIT:
		ret = mdss_fb_display_commit(info, argp);
		break;

	case MSMFB_LPM_ENABLE:
		ret = copy_from_user(&dsi_mode, argp, sizeof(dsi_mode));
		if (ret) {
			pr_err("%s: MSMFB_LPM_ENABLE ioctl failed\n", __func__);
			goto exit;
		}

		ret = mdss_fb_mode_switch(mfd, dsi_mode);
		break;
	case MSMFB_ATOMIC_COMMIT:
		ret = mdss_fb_atomic_commit_ioctl(info, argp);
		break;

	case MSMFB_ASYNC_POSITION_UPDATE:
		ret = mdss_fb_async_position_update_ioctl(info, argp);
		break;

	default:
		if (mfd->mdp.ioctl_handler)
			ret = mfd->mdp.ioctl_handler(mfd, cmd, argp);
		break;
	}

	if (ret == -EOPNOTSUPP)
		pr_err("unsupported ioctl (%x)\n", cmd);

exit:
	if (!atomic_dec_return(&mfd->ioctl_ref_cnt))
		wake_up_all(&mfd->ioctl_q);

	return ret;
}

static int mdss_fb_ioctl(struct fb_info *info, unsigned int cmd,
			 unsigned long arg)
{
	if (!info || !info->par)
		return -EINVAL;

	return mdss_fb_do_ioctl(info, cmd, arg);
}

static int mdss_fb_register_extra_panel(struct platform_device *pdev,
	struct mdss_panel_data *pdata)
{
	struct mdss_panel_data *fb_pdata;

	fb_pdata = dev_get_platdata(&pdev->dev);
	if (!fb_pdata) {
		pr_err("framebuffer device %s contains invalid panel data\n",
				dev_name(&pdev->dev));
		return -EINVAL;
	}

	if (fb_pdata->next) {
		pr_err("split panel already setup for framebuffer device %s\n",
				dev_name(&pdev->dev));
		return -EEXIST;
	}

	fb_pdata->next = pdata;

	return 0;
}

int mdss_register_panel(struct platform_device *pdev,
	struct mdss_panel_data *pdata)
{
	struct platform_device *fb_pdev, *mdss_pdev;
	struct device_node *node = NULL;
	int rc = 0;
	bool master_panel = true;

	if (!pdev || !pdev->dev.of_node) {
		pr_err("Invalid device node\n");
		return -ENODEV;
	}

	if (!mdp_instance) {
		pr_err("mdss mdp resource not initialized yet\n");
		return -EPROBE_DEFER;
	}

	if (pdata->get_fb_node)
		node = pdata->get_fb_node(pdev);

	if (!node) {
		node = of_parse_phandle(pdev->dev.of_node,
			"qcom,mdss-fb-map", 0);
		if (!node) {
			pr_err("Unable to find fb node for device: %s\n",
					pdev->name);
			return -ENODEV;
		}
	}
	mdss_pdev = of_find_device_by_node(node->parent);
	if (!mdss_pdev) {
		pr_err("Unable to find mdss for node: %s\n", node->full_name);
		rc = -ENODEV;
		goto mdss_notfound;
	}

	pdata->active = true;
	fb_pdev = of_find_device_by_node(node);
	if (fb_pdev) {
		rc = mdss_fb_register_extra_panel(fb_pdev, pdata);
		if (rc == 0)
			master_panel = false;
	} else {
		pr_info("adding framebuffer device %s\n", dev_name(&pdev->dev));
		fb_pdev = of_platform_device_create(node, NULL,
				&mdss_pdev->dev);
		if (fb_pdev)
			fb_pdev->dev.platform_data = pdata;
	}

	if (master_panel && mdp_instance->panel_register_done)
		mdp_instance->panel_register_done(pdata);

mdss_notfound:
	of_node_put(node);
	return rc;
}
EXPORT_SYMBOL_GPL(mdss_register_panel);

int mdss_fb_register_mdp_instance(struct msm_mdp_interface *mdp)
{
	if (mdp_instance) {
		pr_err("multiple MDP instance registration\n");
		return -EINVAL;
	}

	mdp_instance = mdp;
	return 0;
}
EXPORT_SYMBOL_GPL(mdss_fb_register_mdp_instance);

int mdss_fb_get_phys_info(dma_addr_t *start, unsigned long *len, int fb_num)
{
	struct fb_info *info;
	struct msm_fb_data_type *mfd;

	if (fb_num >= MAX_FBI_LIST)
		return -EINVAL;

	info = fbi_list[fb_num];
	if (!info)
		return -ENOENT;

	mfd = (struct msm_fb_data_type *)info->par;
	if (!mfd)
		return -ENODEV;

	if (mfd->iova)
		*start = mfd->iova;
	else
		*start = info->fix.smem_start;
	*len = info->fix.smem_len;

	return 0;
}
EXPORT_SYMBOL_GPL(mdss_fb_get_phys_info);

int __init mdss_fb_init(void)
{
	int rc = -ENODEV;

	if (fb_get_options("msmfb", NULL))
		return rc;

	if (platform_driver_register(&mdss_fb_driver))
		return rc;

	return 0;
}

module_init(mdss_fb_init);

int mdss_fb_suspres_panel(struct device *dev, void *data)
{
	struct msm_fb_data_type *mfd;
	int rc = 0;
	u32 event;

	if (!data) {
		pr_err("Device state not defined\n");
		return -EINVAL;
	}
	mfd = dev_get_drvdata(dev);
	if (!mfd)
		return 0;

	event = *((bool *) data) ? MDSS_EVENT_RESUME : MDSS_EVENT_SUSPEND;

	/* Do not send runtime suspend/resume for HDMI primary */
	if (!mdss_fb_is_hdmi_primary(mfd)) {
		rc = mdss_fb_send_panel_event(mfd, event, NULL);
		if (rc)
			pr_warn("unable to %s fb%d (%d)\n",
				event == MDSS_EVENT_RESUME ?
				"resume" : "suspend",
				mfd->index, rc);
	}
	return rc;
}

/*
 * mdss_fb_report_panel_dead() - Sends the PANEL_ALIVE=0 status to HAL layer.
 * @mfd   : frame buffer structure associated with fb device.
 *
 * This function is called if the panel fails to respond as expected to
 * the register read/BTA or if the TE signal is not coming as expected
 * from the panel. The function sends the PANEL_ALIVE=0 status to HAL
 * layer.
 */
void mdss_fb_report_panel_dead(struct msm_fb_data_type *mfd)
{
	char *envp[2] = {"PANEL_ALIVE=0", NULL};
	struct mdss_panel_data *pdata =
		dev_get_platdata(&mfd->pdev->dev);
	if (!pdata) {
		pr_err("Panel data not available\n");
		return;
	}

	pdata->panel_info.panel_dead = true;
	kobject_uevent_env(&mfd->fbi->dev->kobj,
		KOBJ_CHANGE, envp);
	pr_err("Panel has gone bad, sending uevent - %s\n", envp[0]);
}


/*
 * mdss_fb_calc_fps() - Calculates fps value.
 * @mfd   : frame buffer structure associated with fb device.
 *
 * This function is called at frame done. It counts the number
 * of frames done for every 1 sec. Stores the value in measured_fps.
 * measured_fps value is 10 times the calculated fps value.
 * For example, measured_fps= 594 for calculated fps of 59.4
 */
void mdss_fb_calc_fps(struct msm_fb_data_type *mfd)
{
	ktime_t current_time_us;
	u64 fps, diff_us;

	current_time_us = ktime_get();
	diff_us = (u64)ktime_us_delta(current_time_us,
			mfd->fps_info.last_sampled_time_us);
	mfd->fps_info.frame_count++;

	if (diff_us >= MDP_TIME_PERIOD_CALC_FPS_US) {
		fps = ((u64)mfd->fps_info.frame_count) * 10000000;
		do_div(fps, diff_us);
		mfd->fps_info.measured_fps = (unsigned int)fps;
		pr_debug(" MDP_FPS for fb%d is %d.%d\n",
			mfd->index, (unsigned int)fps/10, (unsigned int)fps%10);
		mfd->fps_info.last_sampled_time_us = current_time_us;
		mfd->fps_info.frame_count = 0;
	}
}

void mdss_fb_idle_pc(struct msm_fb_data_type *mfd)
{
	struct mdss_overlay_private *mdp5_data;

	if (!mfd || mdss_fb_is_power_off(mfd))
		return;

	mdp5_data = mfd_to_mdp5_data(mfd);

	if ((mfd->panel_info->type == MIPI_CMD_PANEL) && mdp5_data) {
		pr_debug("Notify fb%d idle power collapsed\n", mfd->index);
		sysfs_notify(&mfd->fbi->dev->kobj, NULL, "idle_power_collapse");
	}
}
