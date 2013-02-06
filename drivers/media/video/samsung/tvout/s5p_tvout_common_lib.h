/* linux/drivers/media/video/samsung/tvout/s5p_tvout_common_lib.h
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Header file of common library for SAMSUNG TVOUT driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _S5P_TVOUT_COMMON_LIB_H_
#define _S5P_TVOUT_COMMON_LIB_H_

#include <linux/stddef.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/interrupt.h>

/*****************************************************************************
 * This file includes declarations for TVOUT driver's common library.
 * All files in TVOUT driver can access function or definition in this file.
 ****************************************************************************/

#define DRV_NAME	"TVOUT"

#define tvout_err(fmt, ...)					\
		printk(KERN_ERR "[%s] %s(): " fmt,		\
			DRV_NAME, __func__, ##__VA_ARGS__)

#define CONFIG_TVOUT_DEBUG

#ifndef tvout_dbg
#ifdef CONFIG_TVOUT_DEBUG
#define tvout_dbg(fmt, ...)					\
do {								\
	if (unlikely(tvout_dbg_flag & (1 << DBG_FLAG_TVOUT))) {	\
		printk(KERN_INFO "[%s] %s(): " fmt,		\
			DRV_NAME, __func__, ##__VA_ARGS__);	\
	}							\
} while (0)
#else
#define tvout_dbg(fmt, ...)
#endif
#endif

/*
#if defined(CONFIG_MACH_T0) || defined(CONFIG_MACH_M3)
#define	__CONFIG_HDMI_SUPPORT_FULL_RANGE__
#endif
*/

#define S5PTV_FB_CNT	2
#define S5PTV_VP_BUFF_CNT	4
#define S5PTV_VP_BUFF_SIZE	(4*1024*1024)

#define to_tvout_plat(d) (to_platform_device(d)->dev.platform_data)

#define HDMI_START_NUM 0x1000

enum s5p_tvout_disp_mode {
	TVOUT_NTSC_M = 0,
	TVOUT_PAL_BDGHI,
	TVOUT_PAL_M,
	TVOUT_PAL_N,
	TVOUT_PAL_NC,
	TVOUT_PAL_60,
	TVOUT_NTSC_443,

	TVOUT_480P_60_16_9 = HDMI_START_NUM,
	TVOUT_480P_60_4_3,
	TVOUT_480P_59,

	TVOUT_576P_50_16_9,
	TVOUT_576P_50_4_3,

	TVOUT_720P_60,
	TVOUT_720P_50,
	TVOUT_720P_59,

	TVOUT_1080P_60,
	TVOUT_1080P_50,
	TVOUT_1080P_59,
	TVOUT_1080P_30,

	TVOUT_1080I_60,
	TVOUT_1080I_50,
	TVOUT_1080I_59,
#ifdef CONFIG_HDMI_14A_3D
	TVOUT_720P_60_SBS_HALF,
	TVOUT_720P_59_SBS_HALF,
	TVOUT_720P_50_TB,
	TVOUT_1080P_24_TB,
	TVOUT_1080P_23_TB,
#endif
	TVOUT_INIT_DISP_VALUE
};

#ifdef CONFIG_HDMI_14A_3D
enum s5p_tvout_3d_type {
	HDMI_3D_FP_FORMAT,
	HDMI_3D_SSH_FORMAT,
	HDMI_3D_TB_FORMAT,
	HDMI_2D_FORMAT,
};
#endif

enum s5p_tvout_o_mode {
	TVOUT_COMPOSITE,
	TVOUT_HDMI,
	TVOUT_HDMI_RGB,
	TVOUT_DVI,
	TVOUT_INIT_O_VALUE
};

enum s5p_mixer_burst_mode {
	MIXER_BURST_8 = 0,
	MIXER_BURST_16 = 1
};

enum s5ptv_audio_channel {
	TVOUT_AUDIO_2CH = 0,
	TVOUT_AUDIO_5_1CH = 1,
	TVOUT_AUDIO_2CH_VAL = 2,
	TVOUT_AUDIO_5_1CH_VAL = 6,
};

enum s5ptvfb_data_path_t {
	DATA_PATH_FIFO = 0,
	DATA_PATH_DMA = 1,
};

enum s5ptvfb_alpha_t {
	LAYER_BLENDING,
	PIXEL_BLENDING,
	NONE_BLENDING,
};

enum s5ptvfb_ver_scaling_t {
	VERTICAL_X1,
	VERTICAL_X2,
};

enum s5ptvfb_hor_scaling_t {
	HORIZONTAL_X1,
	HORIZONTAL_X2,
};

struct s5ptvfb_alpha {
	enum s5ptvfb_alpha_t	mode;
	int			channel;
	unsigned int		value;
};

struct s5ptvfb_chroma {
	int		enabled;
	unsigned int	key;
};

struct s5ptvfb_user_window {
	int x;
	int y;
};

struct s5ptvfb_user_plane_alpha {
	int		channel;
	unsigned char	alpha;
};

struct s5ptvfb_user_chroma {
	int		enabled;
	unsigned char	red;
	unsigned char	green;
	unsigned char	blue;
};

struct s5ptvfb_user_scaling {
	enum s5ptvfb_ver_scaling_t ver;
	enum s5ptvfb_hor_scaling_t hor;
};

struct s5p_tvout_status {
	struct clk *i2c_phy_clk;
	struct clk *sclk_hdmiphy;
	struct clk *sclk_pixel;
	struct clk *sclk_dac;
	struct clk *sclk_hdmi;
	spinlock_t tvout_lock;
};

struct s5p_tvout_vp_buff {
	unsigned int        phy_base;
	unsigned int        vir_base;
	unsigned int        size;
};

struct s5p_tvout_vp_bufferinfo {
	struct s5p_tvout_vp_buff    vp_buffs[S5PTV_VP_BUFF_CNT];
	unsigned int                copy_buff_idxs[S5PTV_VP_BUFF_CNT - 1];
	unsigned int                curr_copy_idx;
	unsigned int                vp_access_buff_idx;
	unsigned int                size;
};

struct s5ptv_vp_buf_info {
	unsigned int                buff_cnt;
	struct s5p_tvout_vp_buff    *buffs;
};

struct reg_mem_info {
	char		*name;
	struct resource *res;
	void __iomem	*base;
};

struct irq_info {
	char		*name;
	irq_handler_t	handler;
	int		no;
};

struct s5p_tvout_clk_info {
	char		*name;
	struct clk	*ptr;
};

#ifdef CONFIG_TVOUT_DEBUG
enum tvout_dbg_flag_bit_num {
	DBG_FLAG_HDCP = 0,
	DBG_FLAG_TVOUT,
	DBG_FLAG_HPD,
	DBG_FLAG_HDMI
};

extern int tvout_dbg_flag;
#endif

extern struct s5p_tvout_status s5ptv_status;

extern int s5p_tvout_vcm_create_unified(void);

extern int s5p_tvout_vcm_init(void);

extern void s5p_tvout_vcm_activate(void);

extern void s5p_tvout_vcm_deactivate(void);

extern int s5p_tvout_map_resource_mem(
		struct platform_device *pdev, char *name,
		void __iomem **base, struct resource **res);
extern void s5p_tvout_unmap_resource_mem(
		void __iomem *base, struct resource *res);

extern void s5p_tvout_pm_runtime_enable(struct device *dev);
extern void s5p_tvout_pm_runtime_disable(struct device *dev);
extern void s5p_tvout_pm_runtime_get(void);
extern void s5p_tvout_pm_runtime_put(void);

extern void s5p_hdmi_ctrl_clock(bool on);
extern bool on_stop_process;
extern bool on_start_process;
extern struct s5p_tvout_vp_bufferinfo s5ptv_vp_buff;
#ifdef CONFIG_HAS_EARLYSUSPEND
extern unsigned int suspend_status;
extern int s5p_hpd_get_status(void);
extern void s5p_tvout_mutex_lock(void);
extern void s5p_tvout_mutex_unlock(void);
#endif
#ifdef CONFIG_PM
extern void s5p_hdmi_ctrl_phy_power_resume(void);
#endif

#if defined(CONFIG_SAMSUNG_WORKAROUND_HPD_GLANCE) &&\
	!defined(CONFIG_SAMSUNG_MHL_9290)
extern void call_sched_mhl_hpd_handler(void);
extern int (*hpd_intr_state)(void);
#endif
#endif /* _S5P_TVOUT_COMMON_LIB_H_ */
