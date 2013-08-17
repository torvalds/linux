/* linux/arm/arch/mach-exynos/include/mach/dsim.h
 *
 * Platform data header for Samsung MIPI-DSIM.
 *
 * Copyright (c) 2009 Samsung Electronics
 * InKi Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _DSIM_H
#define _DSIM_H

/* h/w configuration */
#define MIPI_FIN		24000000
#define DSIM_HEADER_FIFO_SZ	16
#define DSIM_TIMEOUT_MS		5000
#define DSIM_NO_OF_INTERRUPT	26
#define DSIM_PM_STABLE_TIME	10

#define DSIM_TRUE		1
#define DSIM_FALSE		0

#define CMD_LENGTH		0xf

enum dsim_interface_type {
	DSIM_COMMAND 		= 0,
	DSIM_VIDEO 		= 1,
};

enum dsim_state {
	DSIM_STATE_RESET 	= 0,
	DSIM_STATE_INIT 	= 1,
	DSIM_STATE_STOP 	= 2,
	DSIM_STATE_HSCLKEN 	= 3,
	DSIM_STATE_ULPS 	= 4,
};

enum {
	DSIM_NONE_STATE 	= 0,
	DSIM_RESUME_COMPLETE 	= 1,
	DSIM_FRAME_DONE 	= 2,
};

enum dsim_virtual_ch_no {
	DSIM_VIRTUAL_CH_0 	= 0,
	DSIM_VIRTUAL_CH_1 	= 1,
	DSIM_VIRTUAL_CH_2 	= 2,
	DSIM_VIRTUAL_CH_3 	= 3,
};

enum dsim_video_mode_type {
	DSIM_NON_BURST_SYNC_EVENT 	= 0,
	DSIM_BURST_SYNC_EVENT 		= 1,
	DSIM_NON_BURST_SYNC_PULSE 	= 2,
	DSIM_BURST 			= 3,
	DSIM_NON_VIDEO_MODE 		= 4,
};

enum dsim_fifo_state {
	DSIM_RX_DATA_FULL 	= (1 << 25),
	DSIM_RX_DATA_EMPTY 	= (1 << 24),
	SFR_HEADER_FULL 	= (1 << 23),
	SFR_HEADER_EMPTY 	= (1 << 22),
	SFR_PAYLOAD_FULL 	= (1 << 21),
	SFR_PAYLOAD_EMPTY 	= (1 << 20),
	I80_HEADER_FULL 	= (1 << 19),
	I80_HEADER_EMPTY 	= (1 << 18),
	I80_PALOAD_FULL 	= (1 << 17),
	I80_PALOAD_EMPTY 	= (1 << 16),
	SUB_DISP_HEADER_FULL 	= (1 << 15),
	SUB_DISP_HEADER_EMPTY 	= (1 << 14),
	SUB_DISP_PAYLOAD_FULL 	= (1 << 13),
	SUB_DISP_PAYLOAD_EMPTY 	= (1 << 12),
	MAIN_DISP_HEADER_FULL 	= (1 << 11),
	MAIN_DISP_HEADER_EMPTY 	= (1 << 10),
	MAIN_DISP_PAYLOAD_FULL 	= (1 << 9),
	MAIN_DISP_PAYLOAD_EMPTY = (1 << 8),
};

enum dsim_no_of_data_lane {
	DSIM_DATA_LANE_1 	= 0,
	DSIM_DATA_LANE_2 	= 1,
	DSIM_DATA_LANE_3 	= 2,
	DSIM_DATA_LANE_4 	= 3,
};

enum dsim_byte_clk_src {
	DSIM_PLL_OUT_DIV8 	= 0,
	DSIM_EXT_CLK_DIV8 	= 1,
	DSIM_EXT_CLK_BYPASS 	= 2,
};

enum dsim_lane {
	DSIM_LANE_DATA0 	= (1 << 0),
	DSIM_LANE_DATA1 	= (1 << 1),
	DSIM_LANE_DATA2 	= (1 << 2),
	DSIM_LANE_DATA3 	= (1 << 3),
	DSIM_LANE_DATA_ALL 	= 0xf,
	DSIM_LANE_CLOCK 	= (1 << 4),
	DSIM_LANE_ALL 		= DSIM_LANE_CLOCK | DSIM_LANE_DATA_ALL,
};

enum dsim_pixel_format {
	DSIM_CMD_3BPP 			= 0,
	DSIM_CMD_8BPP 			= 1,
	DSIM_CMD_12BPP 			= 2,
	DSIM_CMD_16BPP 			= 3,
	DSIM_VID_16BPP_565 		= 4,
	DSIM_VID_18BPP_666PACKED 	= 5,
	DSIM_18BPP_666LOOSELYPACKED 	= 6,
	DSIM_24BPP_888 			= 7,
};

enum dsim_lane_state {
	DSIM_LANE_STATE_HS_READY,
	DSIM_LANE_STATE_ULPS,
	DSIM_LANE_STATE_STOP,
	DSIM_LANE_STATE_LPDT,
};

enum dsim_transfer {
	DSIM_TRANSFER_NEITHER	= 0,
	DSIM_TRANSFER_BYCPU	= (1 << 7),
	DSIM_TRANSFER_BYLCDC	= (1 << 6),
	DSIM_TRANSFER_BOTH	= (0x3 << 6)
};

enum dsim_lane_change {
	DSIM_NO_CHANGE 		= 0,
	DSIM_DATA_LANE_CHANGE 	= 1,
	DSIM_CLOCK_NALE_CHANGE 	= 2,
	DSIM_ALL_LANE_CHANGE 	= 3,
};

enum dsim_int_src {
	DSIM_ALL_OF_INTR 	= 0xffffffff,
	DSIM_PLL_STABLE 	= (1 << 31),
};

enum dsim_data_id {
	/* short packet types of packet types for command */
	GEN_SHORT_WR_NO_PARA	= 0x03,
	GEN_SHORT_WR_1_PARA	= 0x13,
	GEN_SHORT_WR_2_PARA	= 0x23,
	GEN_RD_NO_PARA		= 0x04,
	GEN_RD_1_PARA		= 0x14,
	GEN_RD_2_PARA		= 0x24,
	DCS_WR_NO_PARA		= 0x05,
	DCS_WR_1_PARA		= 0x15,
	DCS_RD_NO_PARA		= 0x06,
	SET_MAX_RTN_PKT_SIZE	= 0x37,

	/* long packet types of packet types for command */
	NULL_PKT		= 0x09,
	BLANKING_PKT		= 0x19,
	GEN_LONG_WR		= 0x29,
	DCS_LONG_WR		= 0x39,

	/* short packet types of generic command */
	CMD_OFF			= 0x02,
	CMD_ON			= 0x12,
	SHUT_DOWN		= 0x22,
	TURN_ON			= 0x32,

	/* short packet types for video data */
	VSYNC_START		= 0x01,
	VSYNC_END		= 0x11,
	HSYNC_START		= 0x21,
	HSYNC_END		= 0x31,
	EOT_PKT			= 0x08,

	/* long packet types for video data */
	RGB565_PACKED		= 0x0e,
	RGB666_PACKED		= 0x1e,
	RGB666_LOOSLY		= 0x2e,
	RGB888_PACKED		= 0x3e,
};

struct dsim_config {
	/* only DSIM_1_03 */
	unsigned char auto_flush;

	/* only DSIM_1.02 or DSIM_1_03 */
	unsigned char eot_disable;

	/* porch option */
	unsigned char auto_vertical_cnt;	/* auto vertical cnt mode */
	unsigned char hse;			/* horizontal sync event mode */
	unsigned char hfp;			/* discard horizontal front porch time */
	unsigned char hbp;			/* discard horizontal back porch time */
	unsigned char hsa;			/* discard horizontal sync area timing */

	/* data lane */
	enum dsim_no_of_data_lane e_no_data_lane;	/* number of data lane using DSI Master */

	/* byte clock and escape clock */
	enum dsim_byte_clk_src e_byte_clk;

	/* pll pms value */
	unsigned char p;
	unsigned short m;
	unsigned char s;

	/* pll stable time */
	unsigned int pll_stable_time;

	unsigned long esc_clk;

	/* BTA sequence */
	unsigned short stop_holding_cnt;
	unsigned char bta_timeout;
	unsigned short rx_timeout;
	enum dsim_video_mode_type e_lane_swap;
};

struct dsim_lcd_config {
	enum dsim_interface_type e_interface;
	unsigned int parameter[3];

	/* lcd panel info */
	void *lcd_panel_info;

	/* platform data for lcd panel based on MIPI-DSI. */
	void *mipi_ddi_pd;
};

struct s5p_platform_dsim {
	char	*clk_name;
	char	lcd_panel_name[64];
	unsigned int te_irq;

	struct dsim_config *dsim_info;
	struct dsim_lcd_config *dsim_lcd_info;

	void (*mipi_power) (int enable);
	void (*enable_clk) (void *d_clk, unsigned char enable);
	void (*part_reset) (void);
	void (*init_d_phy) (unsigned int dsim_base);
	void (*cfg_gpio) (void);
};

extern void s5p_dsim_enable_clk(void *d_clk, unsigned char enable);
extern void s5p_dsim_part_reset(void);
extern void s5p_dsim_init_d_phy(unsigned int dsim_base);
extern void exynos4_dsim_gpio_setup_24bpp(void);

#endif /* _DSIM_H */
