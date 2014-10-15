/*
 * hdmirx_drv.h for HDMI device driver, and declare IO function,
 * structure, enum, used in TVIN AFE sub-module processing
 *
 * Copyright (C) 2012 AMLOGIC, INC. All Rights Reserved.
 * Author: Rain Zhang <rain.zhang@amlogic.com>
 * Author: Xiaofei Zhu <xiaofei.zhu@amlogic.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#ifndef _TVHDMI_H
#define _TVHDMI_H


#include <linux/amlogic/tvin/tvin.h>
#include "../tvin_global.h"
#include "../tvin_format_table.h"

#define HDMIRX_VER "Ref.2014/05/08"
#define HDMI_STATE_CHECK_FREQ     (20*5)
#define ABS(x) ((x)<0 ? -(x) : (x))
#define CEC_FUNC_ENABLE		0

/*
 * enum definitions
 */
typedef enum hdmirx_src_type_e {
	TVHDMI_SRC_TYPE_NULL = 0,
} hdmirx_src_type_t;

/* add new value at the end,
 * do not insert new value in the middle
 * to avoid wrong VIC value !!!
 */
typedef enum HDMI_Video_Type_ {

	HDMI_Unkown = 0 ,
	HDMI_640x480p60 = 1 ,
	HDMI_480p60,
	HDMI_480p60_16x9,
	HDMI_720p60,
	HDMI_1080i60,               /* 5 */

	HDMI_480i60,
	HDMI_480i60_16x9,
	HDMI_1440x240p60,
	HDMI_1440x240p60_16x9,
	HDMI_2880x480i60,           /* 10 */

	HDMI_2880x480i60_16x9,
	HDMI_2880x240p60,
	HDMI_2880x240p60_16x9,
	HDMI_1440x480p60,
	HDMI_1440x480p60_16x9,      /* 15 */

	HDMI_1080p60,
	HDMI_576p50,
	HDMI_576p50_16x9,
	HDMI_720p50,
	HDMI_1080i50,               /* 20 */

	HDMI_576i50,
	HDMI_576i50_16x9,
	HDMI_1440x288p50,
	HDMI_1440x288p50_16x9,
	HDMI_2880x576i50,           /* 25 */

	HDMI_2880x576i50_16x9,
	HDMI_2880x288p50,
	HDMI_2880x288p50_16x9,
	HDMI_1440x576p50,
	HDMI_1440x576p50_16x9,      /* 30 */

	HDMI_1080p50,
	HDMI_1080p24,
	HDMI_1080p25,
	HDMI_1080p30,
	HDMI_2880x480p60,           /* 35 */

	HDMI_2880x480p60_16x9,
	HDMI_2880x576p50,
	HDMI_2880x576p50_16x9,
	HDMI_1080i50_1250,          /* 39 */



	HDMI_1080I120 = 46,
	HDMI_720p120  = 47,

	HDMI_720p24   = 60,
	HDMI_720p30   = 62,
	HDMI_1080p120 = 63,
	HDMI_800_600  = 65,

	HDMI_1024_768,              /* 66 */
	HDMI_720_400,
	HDMI_1280_768,
	HDMI_1280_800,
	HDMI_1280_960,              /* 70 */

	HDMI_1280_1024,
	HDMI_1360_768,
	HDMI_1366_768,
	HDMI_1600_900,
	HDMI_1600_1200,             /* 75 */

	HDMI_1920_1200,
	HDMI_1440_900,
	HDMI_1400_1050,
	HDMI_1680_1050,             /* 79 */

    /* 4k2k mode */
    HDMI_3840_2160p,
    HDMI_4096_2160p,            /* 81 */
    HDMI_MAX_IS_UNSUPPORT,
} HDMI_Video_Codes_t ;

/*
*/
#define USE_GPIO_FOR_HPD

/** TMDS clock minimum [kHz] */
#define TMDS_CLK_MIN			(24000UL)//(25000UL)
/** TMDS clock maximum [kHz] */
#define TMDS_CLK_MAX			(340000UL)

struct hdmi_rx_phy
{
	/** (@b user) Context status: closed (0), opened (<0) and configured (>0) */
	int status;
	/** (@b user) Configuration clock frequency [kHz], valid range 10MHz to 160MHz */
	unsigned long cfg_clk;
	/** Peaking configuration */
	uint16_t peaking;
	/** PLL configuration */
	uint32_t pll_cfg;
  /**/
  int lock_thres;
  int fast_switching;
  int fsm_enhancement;
  int port_select_ovr_en;
  int phy_cmu_config_force_val;
  int phy_system_config_force_val;
};


/**
 * @short HDMI RX controller video parameters
 *
 * For Auxiliary Video InfoFrame (AVI) details see HDMI 1.4a section 8.2.2
 */
struct hdmi_rx_ctrl_video
{
	/** DVI detection status: DVI (true) or HDMI (false) */
	bool dvi;
	/** Deep color mode: 24, 30, 36 or 48 [bits per pixel] */
	unsigned deep_color_mode;

	/** Pixel clock frequency [kHz] */
	unsigned long pixel_clk;
	/** Refresh rate [0.01Hz] */
	unsigned long refresh_rate;
	/** Interlaced */
	bool interlaced;
	/** Vertical offset */
	unsigned voffset;
	/** Vertical active */
	unsigned vactive;
	/** Vertical total */
	unsigned vtotal;
	/** Horizontal offset */
	unsigned hoffset;
	/** Horizontal active */
	unsigned hactive;
	/** Horizontal total */
	unsigned htotal;

	/** AVI Y1-0, video format */
	unsigned video_format;
	/** AVI A0, active format information present */
	unsigned active_valid;
	/** AVI B1-0, bar valid information */
	unsigned bar_valid;
	/** AVI S1-0, scan information */
	unsigned scan_info;
	/** AVI C1-0, colorimetry information */
	unsigned colorimetry;
	/** AVI M1-0, picture aspect ratio */
	unsigned picture_ratio;
	/** AVI R3-0, active format aspect ratio */
	unsigned active_ratio;
	/** AVI ITC, IT content */
	unsigned it_content;
	/** AVI EC2-0, extended colorimetry */
	unsigned ext_colorimetry;
	/** AVI Q1-0, RGB quantization range */
	unsigned rgb_quant_range;
	/** AVI SC1-0, non-uniform scaling information */
	unsigned n_uniform_scale;
	/** AVI VIC6-0, video mode identification code */
	unsigned video_mode;
	/** AVI PR3-0, pixel repetition factor */
	unsigned pixel_repetition;
	/** AVI, line number of end of top bar */
	unsigned bar_end_top;
	/** AVI, line number of start of bottom bar */
	unsigned bar_start_bottom;
	/** AVI, pixel number of end of left bar */
	unsigned bar_end_left;
	/** AVI, pixel number of start of right bar */
	unsigned bar_start_right;

	/* for sw info */
	unsigned int sw_vic;
	unsigned int sw_dvi;
	bool           sw_fp;
	bool           sw_alternative;
};

/**
 * @short HDMI RX controller context information
 *
 * Initialize @b user fields (set status to zero).
 * After opening this data is for internal use only.
 */
struct hdmi_rx_ctrl
{
	/** (@b user) Context status: closed (0), opened (<0) and configured (>0) */
	int status;
	/** (@b user) Configuration clock frequency [kHz], valid range 10MHz to 160MHz */
	unsigned long cfg_clk;
	/** (@b user) Mode detection clock frequency [kHz], valid range 10MHz to 50MHz */
	unsigned long md_clk;
	/** TDMS clock frequency [kHz] */
	unsigned long tmds_clk;
	unsigned long tmds_clk2; //[MHz], measured with clk_util_clk_msr2
	/** Debug status, audio FIFO reset count */
	int acr_mode;
	/**/
	unsigned debug_audio_fifo_rst;
	/** Debug status, packet FIFO reset count */
	unsigned debug_packet_fifo_rst;
	/** Debug status, IRQ handling count */
	unsigned debug_irq_handling;
	/** Debug status, IRQ packet decoder count */
	unsigned debug_irq_packet_decoder;
	/** Debug status, IRQ audio clock count */
	unsigned debug_irq_audio_clock;
	/** Debug status, IRQ audio FIFO count */
	unsigned debug_irq_audio_fifo;
	/** Debug status, IRQ video mode count */
	unsigned debug_irq_video_mode;
	/** Debug status, IRQ HDMI count */
	unsigned debug_irq_hdmi;
};

/** Receiver key selection size - 40 bits */
#define HDCP_BKSV_SIZE	(2 *  1)
/** Encrypted keys size - 40 bits x 40 keys */
#define HDCP_KEYS_SIZE	(2 * 40)

/**
 * @short HDMI RX controller HDCP configuration
 */
struct hdmi_rx_ctrl_hdcp {
	/** Repeater mode else receiver only */
	bool repeat;
	/** Key description seed */
	uint32_t seed;
	/**
	 * Receiver key selection
	 * @note 0: high order, 1: low order
	 */
	uint32_t bksv[HDCP_BKSV_SIZE];
	/**
	 * Encrypted keys
	 * @note 0: high order, 1: low order
	 */
	uint32_t keys[HDCP_KEYS_SIZE];
};

#define CHANNEL_STATUS_SIZE   24

struct aud_info_s{
    /* info frame*/
    /*
    unsigned char cc;
    unsigned char ct;
    unsigned char ss;
    unsigned char sf;
    */
    int coding_type;
    int channel_count;
    int sample_frequency;
    int sample_size;
    int coding_extension;
    int channel_allocation;
    int down_mix_inhibit;
    int level_shift_value;

    /* channel status */
    unsigned char channel_status[CHANNEL_STATUS_SIZE];
    unsigned char channel_status_bak[CHANNEL_STATUS_SIZE];
    /**/
    unsigned int cts;
    unsigned int n;
    unsigned int audio_recovery_clock;
    /**/
    int real_channel_num;
    int real_sample_size;
    int real_sample_rate;
};

struct vendor_specific_info_s{
    unsigned identifier;
    unsigned char hdmi_video_format;
    unsigned char _3d_structure;
    unsigned char _3d_ext_data;
};

struct rx {
	/** HDMI RX received signal changed */
	uint change;
	/** HDMI RX input port 0 (A) or 1 (B) (or 2(C) or 3 (D)) */
	unsigned port;
	/** HDMI RX PHY context */
	struct hdmi_rx_phy phy;
	/** HDMI RX controller context */
	struct hdmi_rx_ctrl ctrl;
	/** HDMI RX controller HDCP configuration */
	struct hdmi_rx_ctrl_hdcp hdcp;

	/* wrapper */
	unsigned int state;
	unsigned char pow5v_state[10];
	bool tx_5v_status;
	bool tx_5v_status_pre;
	bool no_signal;
	int hpd_wait_time;
	int audio_wait_time;
	int audio_sample_rate_stable_count;
	unsigned int audio_reset_release_flag;
	int video_wait_time;
	/* info */
	struct aud_info_s aud_info;
	struct hdmi_rx_ctrl_video video_params;
	struct hdmi_rx_ctrl_video pre_video_params;
	struct hdmi_rx_ctrl_video cur_video_params;
	struct hdmi_rx_ctrl_video reltime_video_params;
	struct vendor_specific_info_s vendor_specific_info;

};


extern struct rx rx;
extern int hdmirx_log_flag;
extern int hdcp_enable;
extern int hdmirx_print_flag;
extern bool irq_ctrl_reg_en; /* enable/disable reg rd/wr in irq  */

void hdmirx_wr_top (unsigned long addr, unsigned long data);
unsigned long hdmirx_rd_top (unsigned long addr);
void hdmirx_wr_dwc(uint16_t addr, uint32_t data);
uint32_t hdmirx_rd_dwc(uint16_t addr);
int hdmirx_wr_phy(uint8_t reg_address, uint16_t data);
uint16_t hdmirx_rd_phy(uint8_t reg_address);
uint32_t hdmirx_rd_bits_dwc( uint16_t addr, uint32_t mask);
void hdmirx_wr_bits_dwc( uint16_t addr, uint32_t mask, uint32_t value);

uint32_t get(uint32_t data, uint32_t mask);
uint32_t set(uint32_t data, uint32_t mask, uint32_t value);

int hdmirx_control_clk_range(unsigned long min, unsigned long max);
int hdmirx_packet_fifo_rst(void);
void hdmirx_audio_enable(bool en);
int hdmirx_audio_fifo_rst(void);
void hdmirx_phy_hw_reset(void);
void phy_init(int rx_port_sel, int dcm);
void hdmirx_hw_config(void);
void hdmirx_hw_reset(void);
void hdmirx_set_hpd(int port, unsigned char val);
int hdmirx_interrupts_cfg( bool enable);
int hdmirx_interrupts_hpd( bool enable);
void hdmirx_phy_reset(bool enable);
void hdmirx_phy_pddq(int enable);

int hdmirx_get_video_info(struct hdmi_rx_ctrl *ctx, struct hdmi_rx_ctrl_video *params);
int hdmirx_packet_get_avi(struct hdmi_rx_ctrl_video *params);
int hdmirx_config_audio(void);
void hdmirx_config_video(struct hdmi_rx_ctrl_video *video_params);
int hdmirx_get_tmds_clock(void);
int hdmirx_get_pixel_clock(void);
void hdmirx_read_audio_info(struct aud_info_s* audio_info);
int hdmirx_get_pdec_aud_sts(void);
void hdmirx_read_vendor_specific_info_frame(struct vendor_specific_info_s* vs);
void hdmirx_set_pinmux(void);
int hdmirx_get_clock(int index);

/**
 * all functions declare
 */
extern enum tvin_sig_fmt_e hdmirx_hw_get_fmt(void);
extern void hdmirx_hw_monitor(void);
extern bool hdmirx_hw_is_nosig(void);
extern bool video_format_change(void);
extern bool hdmirx_hw_pll_lock(void);
extern void hdmirx_reset(void);
extern void hdmirx_hw_init(tvin_port_t port);
extern void to_init_state(void);
extern void hdmirx_hw_uninit(void);
extern unsigned int hdmirx_get_cur_vic(void);
extern void hdmirx_hw_enable(void);
extern void hdmirx_default_hpd(bool high);
extern void hdmirx_hw_disable(unsigned char flag);
extern void hdmirx_fill_edid_buf(const char* buf, int size);
extern int hdmirx_read_edid_buf(char* buf, int max_size);
extern void hdmirx_fill_key_buf(const char* buf, int size);
extern int hdmirx_read_key_buf(char* buf, int max_size);
extern int hdmirx_debug(const char* buf, int size);
extern int hdmirx_hw_get_color_fmt(void);
extern int hdmirx_hw_get_3d_structure(unsigned char*, unsigned char*);
extern int hdmirx_hw_get_dvi_info(void);
extern int hdmirx_hw_get_pixel_repeat(void);
extern bool hdmirx_hw_check_frame_skip(void);
extern int hdmirx_print(const char *fmt, ...);
extern int hdmirx_log_flag;
extern int hdmirx_de_repeat_enable;
extern int hdmirx_hw_dump_reg(unsigned char* buf, int size);

#endif  // _TVHDMI_H
