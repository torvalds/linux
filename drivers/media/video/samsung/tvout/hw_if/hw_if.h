/* linux/drivers/media/video/samsung/tvout/hw_if/hw_if.h
 *
 * Copyright (c) 2010 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Header file for interface of Samsung TVOUT-related hardware
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _SAMSUNG_TVOUT_HW_IF_H_
#define _SAMSUNG_TVOUT_HW_IF_H_ __FILE__

/*****************************************************************************
 * This file includes declarations for external functions of
 * Samsung TVOUT-related hardware. So only external functions
 * to be used by higher layer must exist in this file.
 *
 * Higher layer must use only the declarations included in this file.
 ****************************************************************************/

#include <linux/irqreturn.h>
#include <linux/stddef.h>

#include "../s5p_tvout_common_lib.h"

/*****************************************************************************
 * Common
 ****************************************************************************/
enum s5p_tvout_endian {
	TVOUT_LITTLE_ENDIAN = 0,
	TVOUT_BIG_ENDIAN = 1
};



/*****************************************************************************
 * for MIXER
 ****************************************************************************/
enum s5p_mixer_layer {
	MIXER_VIDEO_LAYER = 2,
	MIXER_GPR0_LAYER = 0,
	MIXER_GPR1_LAYER = 1
};

enum s5p_mixer_bg_color_num {
	MIXER_BG_COLOR_0 = 0,
	MIXER_BG_COLOR_1 = 1,
	MIXER_BG_COLOR_2 = 2
};

enum s5p_mixer_color_fmt {
	MIXER_RGB565  = 4,
	MIXER_RGB1555 = 5,
	MIXER_RGB4444 = 6,
	MIXER_RGB8888 = 7
};

enum s5p_mixer_rgb {
	MIXER_RGB601_0_255 = 0,
	MIXER_RGB601_16_235,
	MIXER_RGB709_0_255,
	MIXER_RGB709_16_235
};

enum s5p_mixer_out_type {
	MIXER_YUV444,
	MIXER_RGB888
};

extern int s5p_mixer_set_show(enum s5p_mixer_layer layer, bool show);
extern int s5p_mixer_set_priority(enum s5p_mixer_layer layer, u32 priority);
extern void s5p_mixer_set_pre_mul_mode(enum s5p_mixer_layer layer, bool enable);
extern int s5p_mixer_set_pixel_blend(enum s5p_mixer_layer layer, bool enable);
extern int s5p_mixer_set_layer_blend(enum s5p_mixer_layer layer, bool enable);
extern int s5p_mixer_set_alpha(enum s5p_mixer_layer layer, u32 alpha);
extern int s5p_mixer_set_grp_base_address(enum s5p_mixer_layer layer,
		u32 baseaddr);
extern int s5p_mixer_set_grp_layer_dst_pos(enum s5p_mixer_layer layer,
		u32 dst_offs_x, u32 dst_offs_y);
extern int s5p_mixer_set_grp_layer_src_pos(enum s5p_mixer_layer layer, u32 span,
		u32 width, u32 height, u32 src_offs_x, u32 src_offs_y);
extern void s5p_mixer_set_bg_color(enum s5p_mixer_bg_color_num colornum,
		u32 color_y, u32 color_cb, u32 color_cr);
extern void s5p_mixer_set_video_limiter(u32 upper_y, u32 lower_y,
		u32 upper_c, u32 lower_c, bool enable);
extern void s5p_mixer_init_status_reg(enum s5p_mixer_burst_mode burst,
		enum s5p_tvout_endian endian);
extern int s5p_mixer_init_display_mode(enum s5p_tvout_disp_mode mode,
		enum s5p_tvout_o_mode output_mode, enum s5p_mixer_rgb
		rgb_type);
extern void s5p_mixer_scaling(enum s5p_mixer_layer layer,
		struct s5ptvfb_user_scaling scaling);
extern void s5p_mixer_set_color_format(enum s5p_mixer_layer layer,
		enum s5p_mixer_color_fmt format);
extern void s5p_mixer_set_chroma_key(enum s5p_mixer_layer layer, bool enabled,
		u32 key);
extern void s5p_mixer_init_bg_dither_enable(bool cr_dither_enable,
		bool cdither_enable, bool y_dither_enable);
extern void s5p_mixer_init_csc_coef_default(enum s5p_mixer_rgb csc_type);
extern void s5p_mixer_start(void);
extern void s5p_mixer_stop(void);
extern void s5p_mixer_set_underflow_int_enable(enum s5p_mixer_layer layer,
		bool en);
extern void s5p_mixer_set_vsync_interrupt(bool);
extern void s5p_mixer_clear_pend_all(void);
extern irqreturn_t s5p_mixer_irq(int irq, void *dev_id);
extern void s5p_mixer_init(void __iomem *addr);


/*****************************************************************************
 * for HDMI
 ****************************************************************************/
#define hdmi_mask_8(x)		((x) & 0xFF)
#define hdmi_mask_16(x)		(((x) >> 8) & 0xFF)
#define hdmi_mask_24(x)		(((x) >> 16) & 0xFF)
#define hdmi_mask_32(x)		(((x) >> 24) & 0xFF)

#define hdmi_write_16(x, y)				\
	do {						\
		writeb(hdmi_mask_8(x), y);		\
		writeb(hdmi_mask_16(x), y + 4);		\
	} while (0);

#define hdmi_write_24(x, y)				\
	do {						\
		writeb(hdmi_mask_8(x), y);		\
		writeb(hdmi_mask_16(x), y + 4);		\
		writeb(hdmi_mask_24(x), y + 8);		\
	} while (0);

#define hdmi_write_32(x, y)				\
	do {						\
		writeb(hdmi_mask_8(x), y);		\
		writeb(hdmi_mask_16(x), y + 4);		\
		writeb(hdmi_mask_24(x), y + 8);		\
		writeb(hdmi_mask_32(x), y + 12);	\
	} while (0);

#define hdmi_write_l(buff, base, start, count)		\
	do {						\
		u8 *ptr = buff;				\
		int i = 0;				\
		int a = start;				\
		do {					\
			writeb(ptr[i], base + a);	\
			a += 4;				\
			i++;				\
		} while (i <= (count - 1));		\
	} while (0);

#define hdmi_read_l(buff, base, start, count)		\
	do {						\
		u8 *ptr = buff;				\
		int i = 0;				\
		int a = start;				\
		do {					\
			ptr[i] = readb(base + a);	\
			a += 4;				\
			i++;				\
		} while (i <= (count - 1));		\
	} while (0);

#define hdmi_bit_set(en, reg, val)			\
	do {						\
		if (en)					\
			reg |= val;			\
		else					\
			reg &= ~val;			\
	} while (0);

enum s5p_hdmi_transmit {
	HDMI_DO_NOT_TANS,
	HDMI_TRANS_ONCE,
	HDMI_TRANS_EVERY_SYNC,
};

enum s5p_tvout_audio_codec_type {
	PCM = 1,
	AC3,
	MP3,
	WMA
};

enum s5p_hdmi_infoframe_type {
	HDMI_VSI_INFO = 0x81,
	HDMI_AVI_INFO,
	HDMI_SPD_INFO,
	HDMI_AUI_INFO,
	HDMI_MPG_INFO,
};

enum s5p_hdmi_color_depth {
	HDMI_CD_48,
	HDMI_CD_36,
	HDMI_CD_30,
	HDMI_CD_24
};

enum s5p_hdmi_q_range {
	HDMI_Q_DEFAULT = 0,
	HDMI_Q_LIMITED_RANGE,
	HDMI_Q_FULL_RANGE,
	HDMI_Q_RESERVED,
};

enum s5p_hdmi_avi_yq {
	HDMI_AVI_YQ_LIMITED_RANGE = 0,
	HDMI_AVI_YQ_FULL_RANGE,
};

enum s5p_hdmi_interrrupt {
	HDMI_IRQ_PIN_POLAR_CTL	= 7,
	HDMI_IRQ_GLOBAL		= 6,
	HDMI_IRQ_I2S		= 5,
	HDMI_IRQ_CEC		= 4,
	HDMI_IRQ_HPD_PLUG	= 3,
	HDMI_IRQ_HPD_UNPLUG	= 2,
	HDMI_IRQ_SPDIF		= 1,
	HDMI_IRQ_HDCP		= 0
};

enum phy_freq {
	ePHY_FREQ_25_200,
	ePHY_FREQ_25_175,
	ePHY_FREQ_27,
	ePHY_FREQ_27_027,
	ePHY_FREQ_54,
	ePHY_FREQ_54_054,
	ePHY_FREQ_74_250,
	ePHY_FREQ_74_176,
	ePHY_FREQ_148_500,
	ePHY_FREQ_148_352,
	ePHY_FREQ_108_108,
	ePHY_FREQ_72,
	ePHY_FREQ_25,
	ePHY_FREQ_65,
	ePHY_FREQ_108,
	ePHY_FREQ_162
};

struct s5p_hdmi_infoframe {
	enum s5p_hdmi_infoframe_type	type;
	u8				version;
	u8				length;
};

struct s5p_hdmi_o_trans {
	enum s5p_hdmi_transmit	avi;
	enum s5p_hdmi_transmit	mpg;
	enum s5p_hdmi_transmit	spd;
	enum s5p_hdmi_transmit	gcp;
	enum s5p_hdmi_transmit	gmp;
	enum s5p_hdmi_transmit	isrc;
	enum s5p_hdmi_transmit	acp;
	enum s5p_hdmi_transmit	aui;
	enum s5p_hdmi_transmit	acr;
};

struct s5p_hdmi_o_reg {
	u8			pxl_fmt;
	u8			preemble;
	u8			mode;
	u8			pxl_limit;
	u8			dvi;
};

struct s5p_hdmi_v_frame {
#ifdef	CONFIG_HDMI_14A_3D
	u32			vH_Line;
	u32			vV_Line;
	u32			vH_SYNC_START;
	u32			vH_SYNC_END;
	u32			vV1_Blank;
	u32			vV2_Blank;
	u16			vHBlank;
	u32			VBLANK_F0;
	u32			VBLANK_F1;
	u32			vVSYNC_LINE_BEF_1;
	u32			vVSYNC_LINE_BEF_2;
	u32			vVSYNC_LINE_AFT_1;
	u32			vVSYNC_LINE_AFT_2;
	u32			vVSYNC_LINE_AFT_PXL_1;
	u32			vVSYNC_LINE_AFT_PXL_2;
	u32			vVACT_SPACE_1;
	u32			vVACT_SPACE_2;
	u8			Hsync_polarity;
	u8			Vsync_polarity;
	u8			interlaced;
	u8			vAVI_VIC;
	u8			vAVI_VIC_16_9;
	u8			repetition;
#else
	u8			vic;
	u8			vic_16_9;
	u8			repetition;
	u8			polarity;
	u8			i_p;

	u16			h_active;
	u16			v_active;

	u16			h_total;
	u16			h_blank;

	u16			v_total;
	u16			v_blank;
#endif
	enum phy_freq		pixel_clock;
};

enum s5p_hdmi_audio_type {
	HDMI_GENERIC_AUDIO,
	HDMI_60958_AUDIO,
	HDMI_DVD_AUDIO,
	HDMI_SUPER_AUDIO,
};

struct s5p_hdmi_audio {
	enum s5p_hdmi_audio_type	type;
	u32				freq;
	u32				bit;
	u32				channel;

	u8				on;
};

struct s5p_hdmi_tg_sync {
	u16			begin;
	u16			end;
};

struct s5p_hdmi_v_format {
	struct s5p_hdmi_v_frame	frame;

#ifdef	CONFIG_HDMI_14A_3D
	u16	tg_H_FSZ;
	u16	tg_HACT_ST;
	u16	tg_HACT_SZ;
	u16	tg_V_FSZ;
	u16	tg_VSYNC;
	u16	tg_VSYNC2;
	u16	tg_VACT_ST;
	u16	tg_VACT_SZ;
	u16	tg_FIELD_CHG;
	u16	tg_VACT_ST2;
	u16	tg_VACT_ST3;
	u16	tg_VACT_ST4;
	u16	tg_VSYNC_TOP_HDMI;
	u16	tg_VSYNC_BOT_HDMI;
	u16	tg_FIELD_TOP_HDMI;
	u16	tg_FIELD_BOT_HDMI;
#else
	struct s5p_hdmi_tg_sync	h_sync;
	struct s5p_hdmi_tg_sync	v_sync_top;
	struct s5p_hdmi_tg_sync	v_sync_bottom;
	struct s5p_hdmi_tg_sync	v_sync_h_pos;

	struct s5p_hdmi_tg_sync	v_blank_f;
#endif
	u8			mhl_hsync;
	u8			mhl_vsync;
};

#ifdef CONFIG_HDMI_TX_STRENGTH
#define HDMI_PHY_I2C_REG10	0x10
#define HDMI_PHY_I2C_REG0F	0x0F
#define HDMI_PHY_I2C_REG04	0x04
#define HDMI_PHY_I2C_REG13	0x13
#define HDMI_PHY_I2C_REG17	0x17

#define TX_EMP_LVL	0x10
#define TX_AMP_LVL	0x08
#define TX_LVL_CH0	0x04
#define TX_LVL_CH1	0x02
#define TX_LVL_CH2	0x01

#define TX_EMP_LVL_VAL	0
#define TX_AMP_LVL_VAL	1
#define TX_LVL_CH0_VAL	2
#define TX_LVL_CH1_VAL	3
#define TX_LVL_CH2_VAL	4

extern int s5p_hdmi_phy_set_tx_strength(u8 ch, u8 *value);
#endif
extern int s5p_hdmi_phy_power(bool on);
extern s32 s5p_hdmi_phy_config(
		enum phy_freq freq, enum s5p_hdmi_color_depth cd);

extern void s5p_hdmi_set_gcp(enum s5p_hdmi_color_depth depth, u8 *gcp);
extern void s5p_hdmi_reg_acr(u8 *acr);
extern void s5p_hdmi_reg_asp(u8 *asp, struct s5p_hdmi_audio *audio);
extern void s5p_hdmi_reg_gcp(u8 i_p, u8 *gcp);
extern void s5p_hdmi_reg_acp(u8 *header, u8 *acp);
extern void s5p_hdmi_reg_isrc(u8 *isrc1, u8 *isrc2);
extern void s5p_hdmi_reg_gmp(u8 *gmp);
#ifdef CONFIG_HDMI_14A_3D
extern void s5p_hdmi_reg_infoframe(
	struct s5p_hdmi_infoframe *info, u8 *data, u8 type_3D);
extern void s5p_hdmi_reg_tg(struct s5p_hdmi_v_format *v);
#else
extern void s5p_hdmi_reg_infoframe(struct s5p_hdmi_infoframe *info, u8 *data);
extern void s5p_hdmi_reg_tg(struct s5p_hdmi_v_frame *frame);
#endif
extern void s5p_hdmi_reg_v_timing(struct s5p_hdmi_v_format *v);
#ifdef CONFIG_HDMI_14A_3D
extern void s5p_hdmi_reg_bluescreen_clr(u16 b, u16 g, u16 r);
#else
extern void s5p_hdmi_reg_bluescreen_clr(u8 cb_b, u8 y_g, u8 cr_r);
#endif
extern void s5p_hdmi_reg_bluescreen(bool en);
extern void s5p_hdmi_reg_clr_range(u8 y_min, u8 y_max, u8 c_min, u8 c_max);
extern void s5p_hdmi_reg_tg_cmd(bool time, bool bt656, bool tg);
extern void s5p_hdmi_reg_enable(bool en);
extern u8 s5p_hdmi_reg_intc_status(void);
extern u8 s5p_hdmi_reg_intc_get_enabled(void);
extern void s5p_hdmi_reg_intc_clear_pending(enum s5p_hdmi_interrrupt intr);
extern void s5p_hdmi_reg_sw_hpd_enable(bool enable);
extern void s5p_hdmi_reg_set_hpd_onoff(bool on_off);
extern u8 s5p_hdmi_reg_get_hpd_status(void);
extern void s5p_hdmi_reg_hpd_gen(void);
extern int s5p_hdmi_reg_intc_set_isr(irqreturn_t (*isr)(int, void *), u8 num);
extern void s5p_hdmi_reg_intc_enable(enum s5p_hdmi_interrrupt intr, u8 en);
#ifdef CONFIG_HDMI_EARJACK_MUTE
extern bool hdmi_audio_ext;
#endif
extern void s5p_hdmi_reg_audio_enable(u8 en);
extern int s5p_hdmi_audio_init(
		enum s5p_tvout_audio_codec_type audio_codec,
		u32 sample_rate, u32 bits, u32 frame_size_code,
		struct s5p_hdmi_audio *audio);
extern irqreturn_t s5p_hdmi_irq(int irq, void *dev_id);
extern void s5p_hdmi_init(void __iomem *hdmi_addr);
extern void s5p_hdmi_phy_init(void __iomem *hdmi_phy_addr);
extern void s5p_hdmi_reg_output(struct s5p_hdmi_o_reg *reg);
extern void s5p_hdmi_reg_packet_trans(struct s5p_hdmi_o_trans *trans);
extern void s5p_hdmi_reg_mute(bool en);





/*****************************************************************************
 * for SDO
 ****************************************************************************/
#ifdef CONFIG_ANALOG_TVENC

enum s5p_sdo_level {
	SDO_LEVEL_0IRE,
	SDO_LEVEL_75IRE
};

enum s5p_sdo_vsync_ratio {
	SDO_VTOS_RATIO_10_4,
	SDO_VTOS_RATIO_7_3
};

enum s5p_sdo_order {
	SDO_O_ORDER_COMPONENT_RGB_PRYPB,
	SDO_O_ORDER_COMPONENT_RBG_PRPBY,
	SDO_O_ORDER_COMPONENT_BGR_PBYPR,
	SDO_O_ORDER_COMPONENT_BRG_PBPRY,
	SDO_O_ORDER_COMPONENT_GRB_YPRPB,
	SDO_O_ORDER_COMPONENT_GBR_YPBPR,
	SDO_O_ORDER_COMPOSITE_CVBS_Y_C,
	SDO_O_ORDER_COMPOSITE_CVBS_C_Y,
	SDO_O_ORDER_COMPOSITE_Y_C_CVBS,
	SDO_O_ORDER_COMPOSITE_Y_CVBS_C,
	SDO_O_ORDER_COMPOSITE_C_CVBS_Y,
	SDO_O_ORDER_COMPOSITE_C_Y_CVBS
};

enum s5p_sdo_sync_sig_pin {
	SDO_SYNC_SIG_NO,
	SDO_SYNC_SIG_YG,
	SDO_SYNC_SIG_ALL
};

enum s5p_sdo_closed_caption_type {
	SDO_NO_INS,
	SDO_INS_1,
	SDO_INS_2,
	SDO_INS_OTHERS
};

enum s5p_sdo_525_copy_permit {
	SDO_525_COPY_PERMIT,
	SDO_525_ONECOPY_PERMIT,
	SDO_525_NOCOPY_PERMIT
};

enum s5p_sdo_525_mv_psp {
	SDO_525_MV_PSP_OFF,
	SDO_525_MV_PSP_ON_2LINE_BURST,
	SDO_525_MV_PSP_ON_BURST_OFF,
	SDO_525_MV_PSP_ON_4LINE_BURST,
};

enum s5p_sdo_525_copy_info {
	SDO_525_COPY_INFO,
	SDO_525_DEFAULT,
};

enum s5p_sdo_525_aspect_ratio {
	SDO_525_4_3_NORMAL,
	SDO_525_16_9_ANAMORPIC,
	SDO_525_4_3_LETTERBOX
};

enum s5p_sdo_625_subtitles {
	SDO_625_NO_OPEN_SUBTITLES,
	SDO_625_INACT_OPEN_SUBTITLES,
	SDO_625_OUTACT_OPEN_SUBTITLES
};

enum s5p_sdo_625_camera_film {
	SDO_625_CAMERA,
	SDO_625_FILM
};

enum s5p_sdo_625_color_encoding {
	SDO_625_NORMAL_PAL,
	SDO_625_MOTION_ADAPTIVE_COLORPLUS
};

enum s5p_sdo_625_aspect_ratio {
	SDO_625_4_3_FULL_576,
	SDO_625_14_9_LETTERBOX_CENTER_504,
	SDO_625_14_9_LETTERBOX_TOP_504,
	SDO_625_16_9_LETTERBOX_CENTER_430,
	SDO_625_16_9_LETTERBOX_TOP_430,
	SDO_625_16_9_LETTERBOX_CENTER,
	SDO_625_14_9_FULL_CENTER_576,
	SDO_625_16_9_ANAMORPIC_576
};

struct s5p_sdo_cvbs_compensation {
	bool cvbs_color_compen;
	u32 y_lower_mid;
	u32 y_bottom;
	u32 y_top;
	u32 y_upper_mid;
	u32 radius;
};

struct s5p_sdo_bright_hue_saturation {
	bool bright_hue_sat_adj;
	u32 gain_brightness;
	u32 offset_brightness;
	u32 gain0_cb_hue_sat;
	u32 gain1_cb_hue_sat;
	u32 gain0_cr_hue_sat;
	u32 gain1_cr_hue_sat;
	u32 offset_cb_hue_sat;
	u32 offset_cr_hue_sat;
};

struct s5p_sdo_525_data {
	bool				analog_on;
	enum s5p_sdo_525_copy_permit	copy_permit;
	enum s5p_sdo_525_mv_psp		mv_psp;
	enum s5p_sdo_525_copy_info	copy_info;
	enum s5p_sdo_525_aspect_ratio	display_ratio;
};

struct s5p_sdo_625_data {
	bool				surround_sound;
	bool				copyright;
	bool				copy_protection;
	bool				text_subtitles;
	enum s5p_sdo_625_subtitles	open_subtitles;
	enum s5p_sdo_625_camera_film	camera_film;
	enum s5p_sdo_625_color_encoding	color_encoding;
	bool				helper_signal;
	enum s5p_sdo_625_aspect_ratio	display_ratio;
};

extern int s5p_sdo_set_video_scale_cfg(
		enum s5p_sdo_level composite_level,
		enum s5p_sdo_vsync_ratio composite_ratio);
extern int s5p_sdo_set_vbi(
		bool wss_cvbs, enum s5p_sdo_closed_caption_type caption_cvbs);
extern void s5p_sdo_set_offset_gain(u32 offset, u32 gain);
extern void s5p_sdo_set_delay(
		u32 delay_y, u32 offset_video_start, u32 offset_video_end);
extern void s5p_sdo_set_schlock(bool color_sucarrier_pha_adj);
extern void s5p_sdo_set_brightness_hue_saturation(
		struct s5p_sdo_bright_hue_saturation bri_hue_sat);
extern void s5p_sdo_set_cvbs_color_compensation(
		struct s5p_sdo_cvbs_compensation cvbs_comp);
extern void s5p_sdo_set_component_porch(
		u32 back_525, u32 front_525, u32 back_625, u32 front_625);
extern void s5p_sdo_set_ch_xtalk_cancel_coef(u32 coeff2, u32 coeff1);
extern void s5p_sdo_set_closed_caption(u32 display_cc, u32 non_display_cc);

extern int s5p_sdo_set_wss525_data(struct s5p_sdo_525_data wss525);
extern int s5p_sdo_set_wss625_data(struct s5p_sdo_625_data wss625);
extern int s5p_sdo_set_cgmsa525_data(struct s5p_sdo_525_data cgmsa525);
extern int s5p_sdo_set_cgmsa625_data(struct s5p_sdo_625_data cgmsa625);
extern int s5p_sdo_set_display_mode(
		enum s5p_tvout_disp_mode disp_mode, enum s5p_sdo_order order);
extern void s5p_sdo_clock_on(bool on);
extern void s5p_sdo_dac_on(bool on);
extern void s5p_sdo_sw_reset(bool active);
extern void s5p_sdo_set_interrupt_enable(bool vsync_intc_en);
extern void s5p_sdo_clear_interrupt_pending(void);
extern void s5p_sdo_init(void __iomem *addr);
#endif

/*****************************************************************************
 * for VP
 ****************************************************************************/
enum s5p_vp_field {
	VP_TOP_FIELD,
	VP_BOTTOM_FIELD
};

enum s5p_vp_line_eq {
	VP_LINE_EQ_0,
	VP_LINE_EQ_1,
	VP_LINE_EQ_2,
	VP_LINE_EQ_3,
	VP_LINE_EQ_4,
	VP_LINE_EQ_5,
	VP_LINE_EQ_6,
	VP_LINE_EQ_7,
	VP_LINE_EQ_DEFAULT
};

enum s5p_vp_mem_type {
	VP_YUV420_NV12,
	VP_YUV420_NV21
};

enum s5p_vp_mem_mode {
	VP_LINEAR_MODE,
	VP_2D_TILE_MODE
};

enum s5p_vp_chroma_expansion {
	VP_C_TOP,
	VP_C_TOP_BOTTOM
};

enum s5p_vp_pxl_rate {
	VP_PXL_PER_RATE_1_1,
	VP_PXL_PER_RATE_1_2,
	VP_PXL_PER_RATE_1_3,
	VP_PXL_PER_RATE_1_4
};

enum s5p_vp_sharpness_control {
	VP_SHARPNESS_NO,
	VP_SHARPNESS_MIN,
	VP_SHARPNESS_MOD,
	VP_SHARPNESS_MAX
};

enum s5p_vp_csc_type {
	VP_CSC_SD_HD,
	VP_CSC_HD_SD
};

enum s5p_vp_csc_coeff {
	VP_CSC_Y2Y_COEF,
	VP_CSC_CB2Y_COEF,
	VP_CSC_CR2Y_COEF,
	VP_CSC_Y2CB_COEF,
	VP_CSC_CB2CB_COEF,
	VP_CSC_CR2CB_COEF,
	VP_CSC_Y2CR_COEF,
	VP_CSC_CB2CR_COEF,
	VP_CSC_CR2CR_COEF
};


extern void s5p_vp_set_poly_filter_coef_default(
		u32 src_width, u32 src_height,
		u32 dst_width, u32 dst_height, bool ipc_2d);
extern void s5p_vp_set_field_id(enum s5p_vp_field mode);
extern int s5p_vp_set_top_field_address(u32 top_y_addr, u32 top_c_addr);
extern int s5p_vp_set_bottom_field_address(
		u32 bottom_y_addr, u32 bottom_c_addr);
extern int s5p_vp_set_img_size(u32 img_width, u32 img_height);
extern void s5p_vp_set_src_position(
		u32 src_off_x, u32 src_x_fract_step, u32 src_off_y);
extern void s5p_vp_set_dest_position(u32 dst_off_x, u32 dst_off_y);
extern void s5p_vp_set_src_dest_size(
		u32 src_width, u32 src_height,
		u32 dst_width, u32 dst_height, bool ipc_2d);
extern void s5p_vp_set_op_mode(
		bool line_skip,
		enum s5p_vp_mem_type mem_type,
		enum s5p_vp_mem_mode mem_mode,
		enum s5p_vp_chroma_expansion chroma_exp,
		bool auto_toggling);
extern void s5p_vp_set_pixel_rate_control(enum s5p_vp_pxl_rate rate);
extern void s5p_vp_set_endian(enum s5p_tvout_endian endian);
extern void s5p_vp_set_bypass_post_process(bool bypass);
extern void s5p_vp_set_saturation(u32 sat);
extern void s5p_vp_set_sharpness(
		u32 th_h_noise,	enum s5p_vp_sharpness_control sharpness);
extern void s5p_vp_set_brightness_contrast(u16 b, u8 c);
extern void s5p_vp_set_brightness_offset(u32 offset);
extern int s5p_vp_set_brightness_contrast_control(
		enum s5p_vp_line_eq eq_num, u32 intc, u32 slope);
extern void s5p_vp_set_csc_control(bool sub_y_offset_en, bool csc_en);
extern int s5p_vp_set_csc_coef(enum s5p_vp_csc_coeff csc_coeff, u32 coeff);
extern int s5p_vp_set_csc_coef_default(enum s5p_vp_csc_type csc_type);
extern int s5p_vp_update(void);
extern int s5p_vp_get_update_status(void);
extern void s5p_vp_sw_reset(void);
extern int s5p_vp_start(void);
extern int s5p_vp_stop(void);
extern void s5p_vp_init(void __iomem *addr);

/*****************************************************************************
 * for CEC
 ****************************************************************************/
enum cec_state {
	STATE_RX,
	STATE_TX,
	STATE_DONE,
	STATE_ERROR
};

struct cec_rx_struct {
	spinlock_t lock;
	wait_queue_head_t waitq;
	atomic_t state;
	u8 *buffer;
	unsigned int size;
};

struct cec_tx_struct {
	wait_queue_head_t waitq;
	atomic_t state;
};

extern struct cec_rx_struct cec_rx_struct;
extern struct cec_tx_struct cec_tx_struct;

void s5p_cec_set_divider(void);
void s5p_cec_enable_rx(void);
void s5p_cec_mask_rx_interrupts(void);
void s5p_cec_unmask_rx_interrupts(void);
void s5p_cec_mask_tx_interrupts(void);
void s5p_cec_unmask_tx_interrupts(void);
void s5p_cec_reset(void);
void s5p_cec_tx_reset(void);
void s5p_cec_rx_reset(void);
void s5p_cec_threshold(void);
void s5p_cec_set_tx_state(enum cec_state state);
void s5p_cec_set_rx_state(enum cec_state state);
void s5p_cec_copy_packet(char *data, size_t count);
void s5p_cec_set_addr(u32 addr);
u32 s5p_cec_get_status(void);
void s5p_clr_pending_tx(void);
void s5p_clr_pending_rx(void);
void s5p_cec_get_rx_buf(u32 size, u8 *buffer);
int __init s5p_cec_mem_probe(struct platform_device *pdev);



/*****************************************************************************
 * for HDCP
 ****************************************************************************/
extern int s5p_hdcp_encrypt_stop(bool on);
extern int __init s5p_hdcp_init(void);
extern int s5p_hdcp_start(void);
extern int s5p_hdcp_stop(void);
extern void s5p_hdcp_flush_work(void);

/****************************************
 * Definitions for sdo ctrl class
 ***************************************/
#if defined(CONFIG_BUSFREQ_OPP) || defined(CONFIG_BUSFREQ_LOCK_WRAPPER)
#define BUSFREQ_400MHZ	400200
#define BUSFREQ_133MHZ	133133
#endif

#ifdef CONFIG_ANALOG_TVENC

enum {
	SDO_PCLK = 0,
	SDO_MUX,
	SDO_NO_OF_CLK
};

struct s5p_sdo_vscale_cfg {
	enum s5p_sdo_level		composite_level;
	enum s5p_sdo_vsync_ratio	composite_ratio;
};

struct s5p_sdo_vbi {
	bool wss_cvbs;
	enum s5p_sdo_closed_caption_type caption_cvbs;
};

struct s5p_sdo_offset_gain {
	u32 offset;
	u32 gain;
};

struct s5p_sdo_delay {
	u32 delay_y;
	u32 offset_video_start;
	u32 offset_video_end;
};

struct s5p_sdo_component_porch {
	u32 back_525;
	u32 front_525;
	u32 back_625;
	u32 front_625;
};

struct s5p_sdo_ch_xtalk_cancellat_coeff {
	u32 coeff1;
	u32 coeff2;
};

struct s5p_sdo_closed_caption {
	u32 display_cc;
	u32 nondisplay_cc;
};

#endif



/****************************************
 * Definitions for hdmi ctrl class
 ***************************************/

#define AVI_SAME_WITH_PICTURE_AR	(0x1<<3)

enum {
	HDMI_PCLK = 0,
	HDMI_MUX,
	HDMI_NO_OF_CLK
};

enum {
	HDMI = 0,
	HDMI_PHY,
	HDMI_NO_OF_MEM_RES
};

enum s5p_hdmi_pic_aspect {
	HDMI_PIC_RATIO_4_3	= 1,
	HDMI_PIC_RATIO_16_9	= 2
};

enum s5p_hdmi_colorimetry {
	HDMI_CLRIMETRY_NO	= 0x00,
	HDMI_CLRIMETRY_601	= 0x40,
	HDMI_CLRIMETRY_709	= 0x80,
	HDMI_CLRIMETRY_X_VAL	= 0xc0,
};

enum s5p_hdmi_v_mode {
	v640x480p_60Hz,
	v720x480p_60Hz,
	v1280x720p_60Hz,
	v1920x1080i_60Hz,
	v720x480i_60Hz,
	v720x240p_60Hz,
	v2880x480i_60Hz,
	v2880x240p_60Hz,
	v1440x480p_60Hz,
	v1920x1080p_60Hz,
	v720x576p_50Hz,
	v1280x720p_50Hz,
	v1920x1080i_50Hz,
	v720x576i_50Hz,
	v720x288p_50Hz,
	v2880x576i_50Hz,
	v2880x288p_50Hz,
	v1440x576p_50Hz,
	v1920x1080p_50Hz,
	v1920x1080p_24Hz,
	v1920x1080p_25Hz,
	v1920x1080p_30Hz,
	v2880x480p_60Hz,
	v2880x576p_50Hz,
	v1920x1080i_50Hz_1250,
	v1920x1080i_100Hz,
	v1280x720p_100Hz,
	v720x576p_100Hz,
	v720x576i_100Hz,
	v1920x1080i_120Hz,
	v1280x720p_120Hz,
	v720x480p_120Hz,
	v720x480i_120Hz,
	v720x576p_200Hz,
	v720x576i_200Hz,
	v720x480p_240Hz,
	v720x480i_240Hz,
	v720x480p_59Hz,
	v1280x720p_59Hz,
	v1920x1080i_59Hz,
	v1920x1080p_59Hz,
#ifdef CONFIG_HDMI_14A_3D
	v1280x720p_60Hz_SBS_HALF,
	v1280x720p_59Hz_SBS_HALF,
	v1280x720p_50Hz_TB,
	v1920x1080p_24Hz_TB,
	v1920x1080p_23Hz_TB,
#endif
};

#ifdef CONFIG_HDMI_14A_3D
struct s5p_hdmi_bluescreen {
	bool	enable;
	u16	b;
	u16	g;
	u16	r;
};
#else
struct s5p_hdmi_bluescreen {
	bool	enable;
	u8	cb_b;
	u8	y_g;
	u8	cr_r;
};
#endif

struct s5p_hdmi_packet {
	u8				acr[7];
	u8				asp[7];
	u8				gcp[7];
	u8				acp[28];
	u8				isrc1[16];
	u8				isrc2[16];
	u8				obas[7];
	u8				dst[28];
	u8				gmp[28];

	u8				spd_vendor[8];
	u8				spd_product[16];

	u8				vsi[27];
	u8				avi[27];
	u8				spd[27];
	u8				aui[27];
	u8				mpg[27];

	struct s5p_hdmi_infoframe	vsi_info;
	struct s5p_hdmi_infoframe	avi_info;
	struct s5p_hdmi_infoframe	spd_info;
	struct s5p_hdmi_infoframe	aui_info;
	struct s5p_hdmi_infoframe	mpg_info;

	u8				h_asp[3];
	u8				h_acp[3];
	u8				h_isrc[3];
};

struct s5p_hdmi_color_range {
	u8	y_min;
	u8	y_max;
	u8	c_min;
	u8	c_max;
};

struct s5p_hdmi_tg {
	bool correction_en;
	bool bt656_en;
};

struct s5p_hdmi_video {
	struct s5p_hdmi_color_range	color_r;
	enum s5p_hdmi_pic_aspect	aspect;
	enum s5p_hdmi_colorimetry	colorimetry;
	enum s5p_hdmi_color_depth	depth;
	enum s5p_hdmi_q_range		q_range;
};

struct s5p_hdmi_o_params {
	struct s5p_hdmi_o_trans	trans;
	struct s5p_hdmi_o_reg	reg;
};

struct s5p_hdmi_ctrl_private_data {
	u8				vendor[8];
	u8				product[16];

	enum s5p_tvout_o_mode		out;
	enum s5p_hdmi_v_mode		mode;

	struct s5p_hdmi_bluescreen	blue_screen;
	struct s5p_hdmi_packet		packet;
	struct s5p_hdmi_tg		tg;
	struct s5p_hdmi_audio		audio;
	struct s5p_hdmi_video		video;

	bool				hpd_status;
	bool				hdcp_en;

	bool				av_mute;

	bool				running;
	char				*pow_name;
	struct s5p_tvout_clk_info		clk[HDMI_NO_OF_CLK];
	struct reg_mem_info		reg_mem[HDMI_NO_OF_MEM_RES];
	struct irq_info			irq;
};

/****************************************
 * Definitions for tvif ctrl class
 ***************************************/
struct s5p_tvif_ctrl_private_data {
	enum s5p_tvout_disp_mode	curr_std;
	enum s5p_tvout_o_mode		curr_if;

	bool				running;

#if defined(CONFIG_BUSFREQ_OPP) || defined(CONFIG_BUSFREQ_LOCK_WRAPPER)
	struct device *bus_dev; /* for BusFreq with Opp */
#endif
	struct device *dev; /* hpd device pointer */
#ifdef CONFIG_HDMI_TX_STRENGTH
	u8 tx_ch;
	u8 *tx_val;
#endif
};

#endif /* _SAMSUNG_TVOUT_HW_IF_H_ */
