/*
 * sh7760fb.h -- platform data for SH7760/SH7763 LCDC framebuffer driver.
 *
 * (c) 2006-2008 MSC Vertriebsges.m.b.H.,
 * 			Manuel Lauss <mano@roarinelk.homelinux.net>
 * (c) 2008 Nobuhiro Iwamatsu <iwamatsu.nobuhiro@renesas.com>
 */

#ifndef _ASM_SH_SH7760FB_H
#define _ASM_SH_SH7760FB_H

/*
 * some bits of the colormap registers should be written as zero.
 * create a mask for that.
 */
#define SH7760FB_PALETTE_MASK 0x00f8fcf8

/* The LCDC dma engine always sets bits 27-26 to 1: this is Area3 */
#define SH7760FB_DMA_MASK 0x0C000000

/* palette */
#define LDPR(x) (((x) << 2))

/* framebuffer registers and bits */
#define LDICKR 0x400
#define LDMTR 0x402
/* see sh7760fb.h for LDMTR bits */
#define LDDFR 0x404
#define LDDFR_PABD (1 << 8)
#define LDDFR_COLOR_MASK 0x7F
#define LDSMR 0x406
#define LDSMR_ROT (1 << 13)
#define LDSARU 0x408
#define LDSARL 0x40c
#define LDLAOR 0x410
#define LDPALCR 0x412
#define LDPALCR_PALS (1 << 4)
#define LDPALCR_PALEN (1 << 0)
#define LDHCNR 0x414
#define LDHSYNR 0x416
#define LDVDLNR 0x418
#define LDVTLNR 0x41a
#define LDVSYNR 0x41c
#define LDACLNR 0x41e
#define LDINTR 0x420
#define LDPMMR 0x424
#define LDPSPR 0x426
#define LDCNTR 0x428
#define LDCNTR_DON (1 << 0)
#define LDCNTR_DON2 (1 << 4)

#ifdef CONFIG_CPU_SUBTYPE_SH7763
# define LDLIRNR       0x440
/* LDINTR bit */
# define LDINTR_MINTEN (1 << 15)
# define LDINTR_FINTEN (1 << 14)
# define LDINTR_VSINTEN (1 << 13)
# define LDINTR_VEINTEN (1 << 12)
# define LDINTR_MINTS (1 << 11)
# define LDINTR_FINTS (1 << 10)
# define LDINTR_VSINTS (1 << 9)
# define LDINTR_VEINTS (1 << 8)
# define VINT_START (LDINTR_VSINTEN)
# define VINT_CHECK (LDINTR_VSINTS)
#else
/* LDINTR bit */
# define LDINTR_VINTSEL (1 << 12)
# define LDINTR_VINTE (1 << 8)
# define LDINTR_VINTS (1 << 0)
# define VINT_START (LDINTR_VINTSEL)
# define VINT_CHECK (LDINTR_VINTS)
#endif

/* HSYNC polarity inversion */
#define LDMTR_FLMPOL (1 << 15)

/* VSYNC polarity inversion */
#define LDMTR_CL1POL (1 << 14)

/* DISPLAY-ENABLE polarity inversion */
#define LDMTR_DISPEN_LOWACT (1 << 13)

/* DISPLAY DATA BUS polarity inversion */
#define LDMTR_DPOL_LOWACT (1 << 12)

/* AC modulation signal enable */
#define LDMTR_MCNT (1 << 10)

/* Disable output of HSYNC during VSYNC period */
#define LDMTR_CL1CNT (1 << 9)

/* Disable output of VSYNC during VSYNC period */
#define LDMTR_CL2CNT (1 << 8)

/* Display types supported by the LCDC */
#define LDMTR_STN_MONO_4       0x00
#define LDMTR_STN_MONO_8       0x01
#define LDMTR_STN_COLOR_4      0x08
#define LDMTR_STN_COLOR_8      0x09
#define LDMTR_STN_COLOR_12     0x0A
#define LDMTR_STN_COLOR_16     0x0B
#define LDMTR_DSTN_MONO_8      0x11
#define LDMTR_DSTN_MONO_16     0x13
#define LDMTR_DSTN_COLOR_8     0x19
#define LDMTR_DSTN_COLOR_12    0x1A
#define LDMTR_DSTN_COLOR_16    0x1B
#define LDMTR_TFT_COLOR_16     0x2B

/* framebuffer color layout */
#define LDDFR_1BPP_MONO 0x00
#define LDDFR_2BPP_MONO 0x01
#define LDDFR_4BPP_MONO 0x02
#define LDDFR_6BPP_MONO 0x04
#define LDDFR_4BPP 0x0A
#define LDDFR_8BPP 0x0C
#define LDDFR_16BPP_RGB555 0x1D
#define LDDFR_16BPP_RGB565 0x2D

/* LCDC Pixclock sources */
#define LCDC_CLKSRC_BUSCLOCK 0
#define LCDC_CLKSRC_PERIPHERAL 1
#define LCDC_CLKSRC_EXTERNAL 2

#define LDICKR_CLKSRC(x) \
       (((x) & 3) << 12)

/* LCDC pixclock input divider. Set to 1 at a minimum! */
#define LDICKR_CLKDIV(x) \
       ((x) & 0x1f)

struct sh7760fb_platdata {

	/* Set this member to a valid fb_videmode for the display you
	 * wish to use.  The following members must be initialized:
	 * xres, yres, hsync_len, vsync_len, sync,
	 * {left,right,upper,lower}_margin.
	 * The driver uses the above members to calculate register values
	 * and memory requirements. Other members are ignored but may
	 * be used by other framebuffer layer components.
	 */
	struct fb_videomode *def_mode;

	/* LDMTR includes display type and signal polarity.  The
	 * HSYNC/VSYNC polarities are derived from the fb_var_screeninfo
	 * data above; however the polarities of the following signals
	 * must be encoded in the ldmtr member:
	 * Display Enable signal (default high-active)  DISPEN_LOWACT
	 * Display Data signals (default high-active)   DPOL_LOWACT
	 * AC Modulation signal (default off)           MCNT
	 * Hsync-During-Vsync suppression (default off) CL1CNT
	 * Vsync-during-vsync suppression (default off) CL2CNT
	 * NOTE: also set a display type!
	 * (one of LDMTR_{STN,DSTN,TFT}_{MONO,COLOR}_{4,8,12,16})
	 */
	u16 ldmtr;

	/* LDDFR controls framebuffer image format (depth, organization)
	 * Use ONE of the LDDFR_?BPP_* macros!
	 */
	u16 lddfr;

	/* LDPMMR and LDPSPR control the timing of the power signals
	 * for the display. Please read the SH7760 Hardware Manual,
	 * Chapters 30.3.17, 30.3.18 and 30.4.6!
	 */
	u16 ldpmmr;
	u16 ldpspr;

	/* LDACLNR contains the line numbers after which the AC modulation
	 * signal is to toggle. Set to ZERO for TFTs or displays which
	 * do not need it. (Chapter 30.3.15 in SH7760 Hardware Manual).
	 */
	u16 ldaclnr;

	/* LDICKR contains information on pixelclock source and config.
	 * Please use the LDICKR_CLKSRC() and LDICKR_CLKDIV() macros.
	 * minimal value for CLKDIV() must be 1!.
	 */
	u16 ldickr;

	/* set this member to 1 if you wish to use the LCDC's hardware
	 * rotation function.  This is limited to displays <= 320x200
	 * pixels resolution!
	 */
	int rotate;		/* set to 1 to rotate 90 CCW */

	/* set this to 1 to suppress vsync irq use. */
	int novsync;

	/* blanking hook for platform. Set this if your platform can do
	 * more than the LCDC in terms of blanking (e.g. disable clock
	 * generator / backlight power supply / etc.
	 */
	void (*blank) (int);
};

#endif /* _ASM_SH_SH7760FB_H */
