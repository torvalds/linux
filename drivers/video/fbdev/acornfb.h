/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/drivers/video/acornfb.h
 *
 *  Copyright (C) 1998,1999 Russell King
 *
 *  Frame buffer code for Acorn platforms
 */
#if defined(HAS_VIDC20)
#include <asm/hardware/iomd.h>
#define VIDC_PALETTE_SIZE	256
#define VIDC_NAME		"VIDC20"
#endif

#define EXTEND8(x) ((x)|(x)<<8)
#define EXTEND4(x) ((x)|(x)<<4|(x)<<8|(x)<<12)

struct vidc20_palette {
	u_int red:8;
	u_int green:8;
	u_int blue:8;
	u_int ext:4;
	u_int unused:4;
};

struct vidc_palette {
	u_int red:4;
	u_int green:4;
	u_int blue:4;
	u_int trans:1;
	u_int sbz1:13;
	u_int reg:4;
	u_int sbz2:2;
};

union palette {
	struct vidc20_palette	vidc20;
	struct vidc_palette	vidc;
	u_int	p;
};

struct acornfb_par {
	struct device	*dev;
	unsigned long	screen_end;
	unsigned int	dram_size;
	unsigned int	vram_half_sam;
	unsigned int	palette_size;
	  signed int	montype;
	unsigned int	using_vram	: 1;
	unsigned int	dpms		: 1;

	union palette palette[VIDC_PALETTE_SIZE];

	u32		pseudo_palette[16];
};

struct vidc_timing {
	u_int	h_cycle;
	u_int	h_sync_width;
	u_int	h_border_start;
	u_int	h_display_start;
	u_int	h_display_end;
	u_int	h_border_end;
	u_int	h_interlace;

	u_int	v_cycle;
	u_int	v_sync_width;
	u_int	v_border_start;
	u_int	v_display_start;
	u_int	v_display_end;
	u_int	v_border_end;

	u_int	control;

	/* VIDC20 only */
	u_int	pll_ctl;
};

struct modey_params {
	u_int	y_res;
	u_int	u_margin;
	u_int	b_margin;
	u_int	vsync_len;
	u_int	vf;
};

struct modex_params {
	u_int	x_res;
	u_int	l_margin;
	u_int	r_margin;
	u_int	hsync_len;
	u_int	clock;
	u_int	hf;
	const struct modey_params *modey;
};

#ifdef HAS_VIDC20
/*
 * VIDC20 registers
 */
#define VIDC20_CTRL		0xe0000000
#define VIDC20_CTRL_PIX_VCLK	(0 << 0)
#define VIDC20_CTRL_PIX_HCLK	(1 << 0)
#define VIDC20_CTRL_PIX_RCLK	(2 << 0)
#define VIDC20_CTRL_PIX_CK	(0 << 2)
#define VIDC20_CTRL_PIX_CK2	(1 << 2)
#define VIDC20_CTRL_PIX_CK3	(2 << 2)
#define VIDC20_CTRL_PIX_CK4	(3 << 2)
#define VIDC20_CTRL_PIX_CK5	(4 << 2)
#define VIDC20_CTRL_PIX_CK6	(5 << 2)
#define VIDC20_CTRL_PIX_CK7	(6 << 2)
#define VIDC20_CTRL_PIX_CK8	(7 << 2)
#define VIDC20_CTRL_1BPP	(0 << 5)
#define VIDC20_CTRL_2BPP	(1 << 5)
#define VIDC20_CTRL_4BPP	(2 << 5)
#define VIDC20_CTRL_8BPP	(3 << 5)
#define VIDC20_CTRL_16BPP	(4 << 5)
#define VIDC20_CTRL_32BPP	(6 << 5)
#define VIDC20_CTRL_FIFO_NS	(0 << 8)
#define VIDC20_CTRL_FIFO_4	(1 << 8)
#define VIDC20_CTRL_FIFO_8	(2 << 8)
#define VIDC20_CTRL_FIFO_12	(3 << 8)
#define VIDC20_CTRL_FIFO_16	(4 << 8)
#define VIDC20_CTRL_FIFO_20	(5 << 8)
#define VIDC20_CTRL_FIFO_24	(6 << 8)
#define VIDC20_CTRL_FIFO_28	(7 << 8)
#define VIDC20_CTRL_INT		(1 << 12)
#define VIDC20_CTRL_DUP		(1 << 13)
#define VIDC20_CTRL_PDOWN	(1 << 14)

#define VIDC20_ECTL		0xc0000000
#define VIDC20_ECTL_REG(x)	((x) & 0xf3)
#define VIDC20_ECTL_ECK		(1 << 2)
#define VIDC20_ECTL_REDPED	(1 << 8)
#define VIDC20_ECTL_GREENPED	(1 << 9)
#define VIDC20_ECTL_BLUEPED	(1 << 10)
#define VIDC20_ECTL_DAC		(1 << 12)
#define VIDC20_ECTL_LCDGS	(1 << 13)
#define VIDC20_ECTL_HRM		(1 << 14)

#define VIDC20_ECTL_HS_MASK	(3 << 16)
#define VIDC20_ECTL_HS_HSYNC	(0 << 16)
#define VIDC20_ECTL_HS_NHSYNC	(1 << 16)
#define VIDC20_ECTL_HS_CSYNC	(2 << 16)
#define VIDC20_ECTL_HS_NCSYNC	(3 << 16)

#define VIDC20_ECTL_VS_MASK	(3 << 18)
#define VIDC20_ECTL_VS_VSYNC	(0 << 18)
#define VIDC20_ECTL_VS_NVSYNC	(1 << 18)
#define VIDC20_ECTL_VS_CSYNC	(2 << 18)
#define VIDC20_ECTL_VS_NCSYNC	(3 << 18)

#define VIDC20_DCTL		0xf0000000
/* 0-9 = number of words in scanline */
#define VIDC20_DCTL_SNA		(1 << 12)
#define VIDC20_DCTL_HDIS	(1 << 13)
#define VIDC20_DCTL_BUS_NS	(0 << 16)
#define VIDC20_DCTL_BUS_D31_0	(1 << 16)
#define VIDC20_DCTL_BUS_D63_32	(2 << 16)
#define VIDC20_DCTL_BUS_D63_0	(3 << 16)
#define VIDC20_DCTL_VRAM_DIS	(0 << 18)
#define VIDC20_DCTL_VRAM_PXCLK	(1 << 18)
#define VIDC20_DCTL_VRAM_PXCLK2	(2 << 18)
#define VIDC20_DCTL_VRAM_PXCLK4	(3 << 18)

#endif
