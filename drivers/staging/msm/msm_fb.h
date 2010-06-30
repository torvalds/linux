/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Code Aurora nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef MSM_FB_H
#define MSM_FB_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include "linux/proc_fs.h"

#include <mach/hardware.h>
#include <linux/io.h>
#include <mach/board.h>

#include <asm/system.h>
#include <asm/mach-types.h>
#include <mach/memory.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>

#include <linux/fb.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include "msm_fb_panel.h"
#include "mdp.h"

#define MSM_FB_DEFAULT_PAGE_SIZE 2
#define MFD_KEY  0x11161126
#define MSM_FB_MAX_DEV_LIST 32

struct disp_info_type_suspend {
	boolean op_enable;
	boolean sw_refreshing_enable;
	boolean panel_power_on;
};

struct msm_fb_data_type {
	__u32 key;
	__u32 index;
	__u32 ref_cnt;
	__u32 fb_page;

	panel_id_type panel;
	struct msm_panel_info panel_info;

	DISP_TARGET dest;
	struct fb_info *fbi;

	boolean op_enable;
	uint32 fb_imgType;
	boolean sw_currently_refreshing;
	boolean sw_refreshing_enable;
	boolean hw_refresh;

	MDPIBUF ibuf;
	boolean ibuf_flushed;
	struct timer_list refresh_timer;
	struct completion refresher_comp;

	boolean pan_waiting;
	struct completion pan_comp;

	/* vsync */
	boolean use_mdp_vsync;
	__u32 vsync_gpio;
	__u32 total_lcd_lines;
	__u32 total_porch_lines;
	__u32 lcd_ref_usec_time;
	__u32 refresh_timer_duration;

	struct hrtimer dma_hrtimer;

	boolean panel_power_on;
	struct work_struct dma_update_worker;
	struct semaphore sem;

	struct timer_list vsync_resync_timer;
	boolean vsync_handler_pending;
	struct work_struct vsync_resync_worker;

	ktime_t last_vsync_timetick;

	__u32 *vsync_width_boundary;

	unsigned int pmem_id;
	struct disp_info_type_suspend suspend;

	__u32 channel_irq;

	struct mdp_dma_data *dma;
	void (*dma_fnc) (struct msm_fb_data_type *mfd);
	int (*cursor_update) (struct fb_info *info,
			      struct fb_cursor *cursor);
	int (*lut_update) (struct fb_info *info,
			      struct fb_cmap *cmap);
	int (*do_histogram) (struct fb_info *info,
			      struct mdp_histogram *hist);
	void *cursor_buf;
	void *cursor_buf_phys;

	void *cmd_port;
	void *data_port;
	void *data_port_phys;

	__u32 bl_level;

	struct platform_device *pdev;

	__u32 var_xres;
	__u32 var_yres;
	__u32 var_pixclock;

#ifdef MSM_FB_ENABLE_DBGFS
	struct dentry *sub_dir;
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
	struct early_suspend mddi_early_suspend;
	struct early_suspend mddi_ext_early_suspend;
#endif
	u32 mdp_fb_page_protection;
	int allow_set_offset;
};

struct dentry *msm_fb_get_debugfs_root(void);
void msm_fb_debugfs_file_create(struct dentry *root, const char *name,
				u32 *var);
void msm_fb_set_backlight(struct msm_fb_data_type *mfd, __u32 bkl_lvl,
				u32 save);

void msm_fb_add_device(struct platform_device *pdev);

int msm_fb_detect_client(const char *name);

#ifdef CONFIG_FB_BACKLIGHT
void msm_fb_config_backlight(struct msm_fb_data_type *mfd);
#endif

#endif /* MSM_FB_H */
