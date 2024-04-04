/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2008-2018, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef MDSS_FB_H
#define MDSS_FB_H

#include <linux/ion.h>
#include <linux/list.h>
#include <linux/msm_mdp_ext.h>
#include <linux/types.h>
#include <linux/notifier.h>
#include <linux/leds.h>

#include "mdss_panel.h"
#include "mdss_mdp_splash_logo.h"

#define MDSS_LPAE_CHECK(phys)	\
	((sizeof(phys) > sizeof(unsigned long)) ? ((phys >> 32) & 0xFF) : (0))

#define MSM_FB_DEFAULT_PAGE_SIZE 2
#define MFD_KEY  0x11161126
#define MSM_FB_MAX_DEV_LIST 32

#define MSM_FB_ENABLE_DBGFS
#define WAIT_FENCE_FIRST_TIMEOUT (3 * MSEC_PER_SEC)
#define WAIT_FENCE_FINAL_TIMEOUT (7 * MSEC_PER_SEC)
#define WAIT_MAX_FENCE_TIMEOUT (WAIT_FENCE_FIRST_TIMEOUT + \
					WAIT_FENCE_FINAL_TIMEOUT)
#define WAIT_MIN_FENCE_TIMEOUT  (1)
/*
 * Display op timeout should be greater than total time it can take for
 * a display thread to commit one frame. One of the largest time consuming
 * activity performed by display thread is waiting for fences. So keeping
 * that as a reference and add additional 20s to sustain system holdups.
 */
#define WAIT_DISP_OP_TIMEOUT (WAIT_FENCE_FIRST_TIMEOUT + \
		WAIT_FENCE_FINAL_TIMEOUT + (20 * MSEC_PER_SEC))

#ifndef MAX
#define  MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif

#ifndef MIN
#define  MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

#define MDP_PP_AD_BL_LINEAR	0x0
#define MDP_PP_AD_BL_LINEAR_INV	0x1

/**
 * enum mdp_notify_event - Different frame events to indicate frame update state
 *
 * @MDP_NOTIFY_FRAME_BEGIN:	Frame update has started, the frame is about to
 *				be programmed into hardware.
 * @MDP_NOTIFY_FRAME_CFG_DONE:	Frame configuration is done.
 * @MDP_NOTIFY_FRAME_CTX_DONE:	Frame has finished accessing sw context.
 *				Next frame can start preparing.
 * @MDP_NOTIFY_FRAME_READY:	Frame ready to be kicked off, this can be used
 *				as the last point in time to synchronize with
 *				source buffers before kickoff.
 * @MDP_NOTIFY_FRAME_FLUSHED:	Configuration of frame has been flushed and
 *				DMA transfer has started.
 * @MDP_NOTIFY_FRAME_DONE:	Frame DMA transfer has completed.
 *				- For video mode panels this will indicate that
 *				  previous frame has been replaced by new one.
 *				- For command mode/writeback frame done happens
 *				  as soon as the DMA of the frame is done.
 * @MDP_NOTIFY_FRAME_TIMEOUT:	Frame DMA transfer has failed to complete within
 *				a fair amount of time.
 */
enum mdp_notify_event {
	MDP_NOTIFY_FRAME_BEGIN = 1,
	MDP_NOTIFY_FRAME_CFG_DONE,
	MDP_NOTIFY_FRAME_CTX_DONE,
	MDP_NOTIFY_FRAME_READY,
	MDP_NOTIFY_FRAME_FLUSHED,
	MDP_NOTIFY_FRAME_DONE,
	MDP_NOTIFY_FRAME_TIMEOUT,
};

/**
 * enum mdp_split_mode - Lists the possible split modes in the device
 *
 * @MDP_SPLIT_MODE_NONE: Single physical display with single ctl path
 *                       and single layer mixer.
 *                       i.e. 1080p single DSI with single LM.
 * #MDP_DUAL_LM_SINGLE_DISPLAY: Single physical display with signle ctl
 *                              path but two layer mixers.
 *                              i.e. WQXGA eDP or 4K HDMI primary or 1080p
 *                                   single DSI with split LM to reduce power.
 * @MDP_DUAL_LM_DUAL_DISPLAY: Two physically separate displays with two
 *                            separate but synchronized ctl paths. Each ctl
 *                            path with its own layer mixer.
 *                            i.e. 1440x2560 with two DSI interfaces.
 * @MDP_PINGPONG_SPLIT: Two physically separate display but single ctl path with
 *                      single layer mixer. Data is split at pingpong module.
 *                      i.e. 1440x2560 on chipsets with single DSI interface.
 */
enum mdp_split_mode {
	MDP_SPLIT_MODE_NONE,
	MDP_DUAL_LM_SINGLE_DISPLAY,
	MDP_DUAL_LM_DUAL_DISPLAY,
	MDP_PINGPONG_SPLIT,
};

/* enum mdp_mmap_type - Lists the possible mmap type in the device
 *
 * @MDP_FB_MMAP_NONE: Unknown type.
 * @MDP_FB_MMAP_ION_ALLOC:   Use ION allocate a buffer for mmap
 * @MDP_FB_MMAP_PHYSICAL_ALLOC:  Use physical buffer for mmap
 */
enum mdp_mmap_type {
	MDP_FB_MMAP_NONE,
	MDP_FB_MMAP_ION_ALLOC,
	MDP_FB_MMAP_PHYSICAL_ALLOC,
};

/**
 * enum dyn_mode_switch_state - Lists next stage for dynamic mode switch work
 *
 * @MDSS_MDP_NO_UPDATE_REQUESTED: incoming frame is processed normally
 * @MDSS_MDP_WAIT_FOR_VALIDATE: Waiting for ATOMIC_COMMIT-validate to be called
 * @MDSS_MDP_WAIT_FOR_COMMIT: Waiting for ATOMIC_COMMIT-commit to be called
 * @MDSS_MDP_WAIT_FOR_KICKOFF: Waiting for KICKOFF to be called
 */
enum dyn_mode_switch_state {
	MDSS_MDP_NO_UPDATE_REQUESTED,
	MDSS_MDP_WAIT_FOR_VALIDATE,
	MDSS_MDP_WAIT_FOR_COMMIT,
	MDSS_MDP_WAIT_FOR_KICKOFF,
};

/**
 * enum mdss_fb_idle_state - idle states based on frame updates
 * @MDSS_FB_NOT_IDLE: Frame updates have started
 * @MDSS_FB_IDLE_TIMER_RUNNING: Idle timer has been kicked
 * @MDSS_FB_IDLE: Currently idle
 */
enum mdss_fb_idle_state {
	MDSS_FB_NOT_IDLE,
	MDSS_FB_IDLE_TIMER_RUNNING,
	MDSS_FB_IDLE
};

struct disp_info_type_suspend {
	int op_enable;
	int panel_power_state;
};

struct disp_info_notify {
	int type;
	struct timer_list timer;
	struct completion comp;
	struct mutex lock;
	int value;
	int is_suspend;
	int ref_count;
	bool init_done;
};

struct msm_sync_pt_data {
	char *fence_name;
	u32 acq_fen_cnt;
	struct mdss_fence *acq_fen[MDP_MAX_FENCE_FD];
	u32 temp_fen_cnt;
	struct mdss_fence *temp_fen[MDP_MAX_FENCE_FD];

	struct mdss_timeline *timeline;
	struct mdss_timeline *timeline_retire;
	int timeline_value;
	u32 threshold;
	u32 retire_threshold;
	atomic_t commit_cnt;
	bool flushed;
	bool async_wait_fences;

	struct mutex sync_mutex;
	struct notifier_block notifier;

	struct mdss_fence *(*get_retire_fence)
		(struct msm_sync_pt_data *sync_pt_data);
};

struct msm_fb_data_type;

struct msm_mdp_interface {
	int (*fb_mem_alloc_fnc)(struct msm_fb_data_type *mfd);
	int (*fb_mem_get_iommu_domain)(void);
	int (*init_fnc)(struct msm_fb_data_type *mfd);
	int (*on_fnc)(struct msm_fb_data_type *mfd);
	int (*off_fnc)(struct msm_fb_data_type *mfd);
	/* called to release resources associated to the process */
	int (*release_fnc)(struct msm_fb_data_type *mfd, struct file *file);
	int (*mode_switch)(struct msm_fb_data_type *mfd,
					u32 mode);
	int (*mode_switch_post)(struct msm_fb_data_type *mfd,
					u32 mode);
	int (*kickoff_fnc)(struct msm_fb_data_type *mfd,
					struct mdp_display_commit *data);
	int (*atomic_validate)(struct msm_fb_data_type *mfd,
				struct mdp_layer_commit_v1 *commit);
	bool (*is_config_same)(struct msm_fb_data_type *mfd,
				struct mdp_output_layer *layer);
	int (*pre_commit)(struct msm_fb_data_type *mfd,
				struct mdp_layer_commit_v1 *commit);
	int (*pre_commit_fnc)(struct msm_fb_data_type *mfd);
	int (*ioctl_handler)(struct msm_fb_data_type *mfd, u32 cmd, void *arg);
	void (*dma_fnc)(struct msm_fb_data_type *mfd);
	int (*cursor_update)(struct msm_fb_data_type *mfd,
				struct fb_cursor *cursor);
	int (*async_position_update)(struct msm_fb_data_type *mfd,
				struct mdp_position_update *update_pos);
	int (*lut_update)(struct msm_fb_data_type *mfd, struct fb_cmap *cmap);
	int (*do_histogram)(struct msm_fb_data_type *mfd,
				struct mdp_histogram *hist);
	int (*ad_calc_bl)(struct msm_fb_data_type *mfd, int bl_in,
		int *bl_out, bool *bl_out_notify);
	int (*panel_register_done)(struct mdss_panel_data *pdata);
	u32 (*fb_stride)(u32 fb_index, u32 xres, int bpp);
	struct mdss_mdp_format_params *(*get_format_params)(u32 format);
	int (*splash_init_fnc)(struct msm_fb_data_type *mfd);
	void (*check_dsi_status)(struct work_struct *work, uint32_t interval);
	int (*configure_panel)(struct msm_fb_data_type *mfd, int mode,
				int dest_ctrl);
	int (*input_event_handler)(struct msm_fb_data_type *mfd);
	void (*footswitch_ctrl)(bool on);
	int (*pp_release_fnc)(struct msm_fb_data_type *mfd);
	void (*signal_retire_fence)(struct msm_fb_data_type *mfd,
					int retire_cnt);
	int (*enable_panel_disable_mode)(struct msm_fb_data_type *mfd,
		bool disable_panel);
	bool (*is_twm_en)(void);
	void *private1;
};

#define IS_CALIB_MODE_BL(mfd) (((mfd)->calib_mode) & MDSS_CALIB_MODE_BL)
#define MDSS_BRIGHT_TO_BL(out, v, bl_max, max_bright) do {\
				out = (2 * (v) * (bl_max) + max_bright);\
				do_div(out, 2 * max_bright);\
				} while (0)
#define MDSS_BL_TO_BRIGHT(out, v, bl_max, max_bright) do {\
				out = (2 * ((v) * (max_bright)) + (bl_max));\
				do_div(out, 2 * bl_max);\
				} while (0)

struct mdss_fb_file_info {
	struct file *file;
	struct list_head list;
};

struct msm_fb_backup_type {
	struct fb_info info;
	struct mdp_display_commit disp_commit;
	bool   atomic_commit;
};

struct msm_fb_fps_info {
	u32 frame_count;
	ktime_t last_sampled_time_us;
	u32 measured_fps;
};

struct msm_fb_data_type {
	u32 key;
	u32 index;
	u32 ref_cnt;
	u32 fb_page;

	struct panel_id panel;
	struct mdss_panel_info *panel_info;
	struct mdss_panel_info reconfig_panel_info;
	int split_mode;
	int split_fb_left;
	int split_fb_right;

	u32 dest;
	struct fb_info *fbi;

	int idle_time;
	u32 idle_state;
	struct msm_fb_fps_info fps_info;
	struct delayed_work idle_notify_work;

	bool atomic_commit_pending;

	int op_enable;
	u32 fb_imgType;
	int panel_reconfig;
	u32 panel_orientation;

	u32 dst_format;
	int panel_power_state;
	struct disp_info_type_suspend suspend;

	struct dma_buf *dbuf;
	struct dma_buf_attachment *attachment;
	struct sg_table *table;
	dma_addr_t iova;
	void *cursor_buf;
	phys_addr_t cursor_buf_phys;
	dma_addr_t cursor_buf_iova;

	int ext_ad_ctrl;
	u32 ext_bl_ctrl;
	u32 calib_mode;
	u32 calib_mode_bl;
	u32 ad_bl_level;
	u64 bl_level;
	u64 bl_extn_level;
	u32 bl_scale;
	u32 unset_bl_level;
	bool allow_bl_update;
	u32 bl_level_scaled;
	u32 bl_level_usr;
	struct mutex bl_lock;
	struct mutex mdss_sysfs_lock;
	bool ipc_resume;

	struct platform_device *pdev;

	u32 mdp_fb_page_protection;

	struct disp_info_notify update;
	struct disp_info_notify no_update;
	struct completion power_off_comp;

	struct msm_mdp_interface mdp;

	struct msm_sync_pt_data mdp_sync_pt_data;

	/* for non-blocking */
	struct task_struct *disp_thread;
	atomic_t commits_pending;
	atomic_t kickoff_pending;
	wait_queue_head_t commit_wait_q;
	wait_queue_head_t idle_wait_q;
	wait_queue_head_t kickoff_wait_q;
	bool shutdown_pending;

	struct msm_fb_splash_info splash_info;

	wait_queue_head_t ioctl_q;
	atomic_t ioctl_ref_cnt;

	struct msm_fb_backup_type msm_fb_backup;
	struct completion power_set_comp;
	u32 is_power_setting;

	u32 dcm_state;
	struct list_head file_list;
	struct dma_buf *fbmem_buf;
	struct dma_buf_attachment *fb_attachment;
	struct sg_table *fb_table;

	bool mdss_fb_split_stored;

	u32 wait_for_kickoff;
	u32 thermal_level;

	int fb_mmap_type;
	struct led_trigger *boot_notification_led;

	/* Following is used for dynamic mode switch */
	enum dyn_mode_switch_state switch_state;
	u32 switch_new_mode;
	bool pending_switch;
	struct mutex switch_lock;
	struct input_handler *input_handler;
};

static inline void mdss_fb_update_notify_update(struct msm_fb_data_type *mfd)
{
	int needs_complete = 0;

	mutex_lock(&mfd->update.lock);
	mfd->update.value = mfd->update.type;
	needs_complete = mfd->update.value == NOTIFY_TYPE_UPDATE;

	mutex_unlock(&mfd->update.lock);
	if (needs_complete) {
		complete(&mfd->update.comp);
		mutex_lock(&mfd->no_update.lock);
		if (mfd->no_update.timer.function)
			del_timer(&(mfd->no_update.timer));

		mfd->no_update.timer.expires = jiffies + (2 * HZ);
		add_timer(&mfd->no_update.timer);
		mutex_unlock(&mfd->no_update.lock);
	}
}

/* Function returns true for split link */
static inline bool is_panel_split_link(struct msm_fb_data_type *mfd)
{
	return mfd && mfd->panel_info && mfd->panel_info->split_link_enabled;
}

/* Function returns true for either any kind of dual display */
static inline bool is_panel_split(struct msm_fb_data_type *mfd)
{
	return mfd && mfd->panel_info && mfd->panel_info->is_split_display;
}
/* Function returns true, if Layer Mixer split is Set */
static inline bool is_split_lm(struct msm_fb_data_type *mfd)
{
	return mfd &&
	       (mfd->split_mode == MDP_DUAL_LM_DUAL_DISPLAY ||
		mfd->split_mode == MDP_DUAL_LM_SINGLE_DISPLAY);
}
/* Function returns true, if Ping pong split is Set*/
static inline bool is_pingpong_split(struct msm_fb_data_type *mfd)
{
	return mfd && (mfd->split_mode == MDP_PINGPONG_SPLIT);
}
static inline bool is_dual_lm_single_display(struct msm_fb_data_type *mfd)
{
	return mfd && (mfd->split_mode == MDP_DUAL_LM_SINGLE_DISPLAY);
}
static inline bool mdss_fb_is_power_off(struct msm_fb_data_type *mfd)
{
	return mdss_panel_is_power_off(mfd->panel_power_state);
}

static inline bool mdss_fb_is_power_on_interactive(
	struct msm_fb_data_type *mfd)
{
	return mdss_panel_is_power_on_interactive(mfd->panel_power_state);
}

static inline bool mdss_fb_is_power_on(struct msm_fb_data_type *mfd)
{
	return mdss_panel_is_power_on(mfd->panel_power_state);
}

static inline bool mdss_fb_is_power_on_lp(struct msm_fb_data_type *mfd)
{
	return mdss_panel_is_power_on_lp(mfd->panel_power_state);
}

static inline bool mdss_fb_is_power_on_ulp(struct msm_fb_data_type *mfd)
{
	return mdss_panel_is_power_on_ulp(mfd->panel_power_state);
}


static inline bool mdss_fb_is_hdmi_primary(struct msm_fb_data_type *mfd)
{
	return (mfd && (mfd->index == 0) &&
		(mfd->panel_info->type == DTV_PANEL));
}

static inline void mdss_fb_init_fps_info(struct msm_fb_data_type *mfd)
{
	memset(&mfd->fps_info, 0, sizeof(mfd->fps_info));
}
int mdss_fb_get_phys_info(dma_addr_t *start, unsigned long *len, int fb_num);
void mdss_fb_set_backlight(struct msm_fb_data_type *mfd, u32 bkl_lvl);
void mdss_fb_update_backlight(struct msm_fb_data_type *mfd);
int mdss_fb_wait_for_fence(struct msm_sync_pt_data *sync_pt_data);
void mdss_fb_signal_timeline(struct msm_sync_pt_data *sync_pt_data);
struct mdss_fence *mdss_fb_sync_get_fence(struct mdss_timeline *timeline,
				const char *fence_name, int val);
int mdss_fb_register_mdp_instance(struct msm_mdp_interface *mdp);
int mdss_fb_dcm(struct msm_fb_data_type *mfd, int req_state);
int mdss_fb_suspres_panel(struct device *dev, void *data);
int mdss_fb_do_ioctl(struct fb_info *info, unsigned int cmd,
		     unsigned long arg);
int mdss_fb_compat_ioctl(struct fb_info *info, unsigned int cmd,
			 unsigned long arg);
int mdss_fb_atomic_commit(struct fb_info *info,
	struct mdp_layer_commit  *commit);
int mdss_fb_async_position_update(struct fb_info *info,
		struct mdp_position_update *update_pos);

u32 mdss_fb_get_mode_switch(struct msm_fb_data_type *mfd);
void mdss_fb_report_panel_dead(struct msm_fb_data_type *mfd);
void mdss_panelinfo_to_fb_var(struct mdss_panel_info *pinfo,
						struct fb_var_screeninfo *var);
void mdss_fb_calc_fps(struct msm_fb_data_type *mfd);
void mdss_fb_idle_pc(struct msm_fb_data_type *mfd);
extern struct dma_buf *ion_alloc(size_t len, unsigned int heap_id_mask,
							unsigned int flags);

#endif /* MDSS_FB_H */
