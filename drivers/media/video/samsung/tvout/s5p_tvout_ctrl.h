/* linux/drivers/media/video/samsung/tvout/s5p_tvout_ctrl.h
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Header file for tvout control class of Samsung TVOUT driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#ifndef _S5P_TVOUT_CTRL_H_
#define _S5P_TVOUT_CTRL_H_ __FILE__

/*****************************************************************************
 * This file includes declarations for external functions of
 * TVOUT driver's control class. So only external functions
 * to be used by higher layer must exist in this file.
 *
 * Higher layer must use only the declarations included in this file.
 ****************************************************************************/

#include "hw_if/hw_if.h"
#include "s5p_tvout_common_lib.h"

/****************************************
 * for Mixer control class
 ***************************************/
extern void s5p_mixer_ctrl_init_fb_addr_phy(enum s5p_mixer_layer layer,
		dma_addr_t fb_addr);
extern void s5p_mixer_ctrl_init_grp_layer(enum s5p_mixer_layer layer);
extern int s5p_mixer_ctrl_set_pixel_format(
	enum s5p_mixer_layer layer, u32 bpp, u32 trans_len);
extern int s5p_mixer_ctrl_enable_layer(enum s5p_mixer_layer layer);
extern int s5p_mixer_ctrl_disable_layer(enum s5p_mixer_layer layer);
extern int s5p_mixer_ctrl_set_priority(enum s5p_mixer_layer layer, u32 prio);
extern int s5p_mixer_ctrl_set_dst_win_pos(enum s5p_mixer_layer layer,
		int dst_x, int dst_y, u32 w, u32 h);
extern int s5p_mixer_ctrl_set_src_win_pos(enum s5p_mixer_layer layer,
		u32 src_x, u32 src_y, u32 w, u32 h);
extern int s5p_mixer_ctrl_set_buffer_address(enum s5p_mixer_layer layer,
		dma_addr_t start_addr);
extern int s5p_mixer_ctrl_set_chroma_key(enum s5p_mixer_layer layer,
		struct s5ptvfb_chroma chroma);
extern int s5p_mixer_ctrl_set_alpha(enum s5p_mixer_layer layer, u32 alpha);
extern int s5p_mixer_ctrl_set_blend_mode(enum s5p_mixer_layer layer,
		enum s5ptvfb_alpha_t mode);
extern int s5p_mixer_ctrl_set_alpha_blending(enum s5p_mixer_layer layer,
		enum s5ptvfb_alpha_t blend_mode, unsigned int alpha);
extern int s5p_mixer_ctrl_scaling(enum s5p_mixer_layer,
		struct s5ptvfb_user_scaling scaling);
extern int s5p_mixer_ctrl_mux_clk(struct clk *ptr);
extern void s5p_mixer_ctrl_set_int_enable(bool en);
extern void s5p_mixer_ctrl_set_vsync_interrupt(bool en);
extern bool s5p_mixer_ctrl_get_vsync_interrupt(void);
extern void s5p_mixer_ctrl_disable_vsync_interrupt(void);
extern void s5p_mixer_ctrl_clear_pend_all(void);
extern void s5p_mixer_ctrl_stop(void);
extern void s5p_mixer_ctrl_internal_start(void);
extern int s5p_mixer_ctrl_start(enum s5p_tvout_disp_mode disp,
		enum s5p_tvout_o_mode out);
extern int s5p_mixer_ctrl_constructor(struct platform_device *pdev);
extern void s5p_mixer_ctrl_destructor(void);
extern void s5p_mixer_ctrl_suspend(void);
extern void s5p_mixer_ctrl_resume(void);

/* Interrupt for Vsync */
typedef struct {
	wait_queue_head_t	wq;
	unsigned int		wq_count;
} s5p_tv_irq;

extern wait_queue_head_t s5ptv_wq;

/****************************************
 * for TV interface control class
 ***************************************/
extern int s5p_tvif_ctrl_set_audio(bool en);
extern void s5p_tvif_audio_channel(int channel);
extern void s5p_tvif_q_color_range(int range);
extern int s5p_tvif_get_q_range(void);
extern int s5p_tvif_ctrl_set_av_mute(bool en);
extern int s5p_tvif_ctrl_get_std_if(
		enum s5p_tvout_disp_mode *std, enum s5p_tvout_o_mode *inf);
extern bool s5p_tvif_ctrl_get_run_state(void);
extern int s5p_tvif_ctrl_start(
		enum s5p_tvout_disp_mode std, enum s5p_tvout_o_mode inf);
extern void s5p_tvif_ctrl_stop(void);

extern int s5p_tvif_ctrl_constructor(struct platform_device *pdev);
extern void s5p_tvif_ctrl_destructor(void);
extern void s5p_tvif_ctrl_suspend(void);
extern void s5p_tvif_ctrl_resume(void);

extern u8 s5p_hdmi_ctrl_get_mute(void);
extern void s5p_hdmi_ctrl_set_hdcp(bool en);

extern int s5p_hpd_set_hdmiint(void);
extern int s5p_hpd_set_eint(void);

#ifdef CONFIG_HDMI_EARJACK_MUTE
extern void s5p_hdmi_ctrl_set_audio(bool en);
#endif

/****************************************
 * for VP control class
 ***************************************/
enum s5p_vp_src_color {
	VP_SRC_COLOR_NV12,
	VP_SRC_COLOR_NV12IW,
	VP_SRC_COLOR_TILE_NV12,
	VP_SRC_COLOR_TILE_NV12IW,
	VP_SRC_COLOR_NV21,
	VP_SRC_COLOR_NV21IW,
	VP_SRC_COLOR_TILE_NV21,
	VP_SRC_COLOR_TILE_NV21IW
};

extern void s5p_vp_ctrl_set_src_plane(
		u32 base_y, u32 base_c, u32 width, u32 height,
		enum s5p_vp_src_color color, enum s5p_vp_field field);
extern void s5p_vp_ctrl_set_src_win(u32 left, u32 top, u32 width, u32 height);
extern void s5p_vp_ctrl_set_dest_win(u32 left, u32 top, u32 width, u32 height);
extern void s5p_vp_ctrl_set_dest_win_alpha_val(u32 alpha);
extern void s5p_vp_ctrl_set_dest_win_blend(bool enable);
extern void s5p_vp_ctrl_set_dest_win_priority(u32 prio);
extern int s5p_vp_ctrl_start(void);
extern void s5p_vp_ctrl_stop(void);
extern int s5p_vp_ctrl_constructor(struct platform_device *pdev);
extern void s5p_vp_ctrl_destructor(void);
extern void s5p_vp_ctrl_suspend(void);
void s5p_vp_ctrl_resume(void);

#endif /* _S5P_TVOUT_CTRL_H_ */
