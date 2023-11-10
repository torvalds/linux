/*
 * Permedia2 framebuffer driver.
 *
 * 2.5/2.6 driver:
 * Copyright (c) 2003 Jim Hague (jim.hague@acm.org)
 *
 * based on 2.4 driver:
 * Copyright (c) 1998-2000 Ilario Nardinocchi (nardinoc@CS.UniBO.IT)
 * Copyright (c) 1999 Jakub Jelinek (jakub@redhat.com)
 *
 * and additional input from James Simmon's port of Hannu Mallat's tdfx
 * driver.
 *
 * I have a Creative Graphics Blaster Exxtreme card - pm2fb on x86. I
 * have no access to other pm2fb implementations. Sparc (and thus
 * hopefully other big-endian) devices now work, thanks to a lot of
 * testing work by Ron Murray. I have no access to CVision hardware,
 * and therefore for now I am omitting the CVision code.
 *
 * Multiple boards support has been on the TODO list for ages.
 * Don't expect this to change.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 *
 */

#include <linux/aperture.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <video/permedia2.h>
#include <video/cvisionppc.h>

#if !defined(__LITTLE_ENDIAN) && !defined(__BIG_ENDIAN)
#error	"The endianness of the target host has not been defined."
#endif

#if !defined(CONFIG_PCI)
#error "Only generic PCI cards supported."
#endif

#undef PM2FB_MASTER_DEBUG
#ifdef PM2FB_MASTER_DEBUG
#define DPRINTK(a, b...)	\
	printk(KERN_DEBUG "pm2fb: %s: " a, __func__ , ## b)
#else
#define DPRINTK(a, b...)	no_printk(a, ##b)
#endif

#define PM2_PIXMAP_SIZE	(1600 * 4)

/*
 * Driver data
 */
static int hwcursor = 1;
static char *mode_option;

/*
 * The XFree GLINT driver will (I think to implement hardware cursor
 * support on TVP4010 and similar where there is no RAMDAC - see
 * comment in set_video) always request +ve sync regardless of what
 * the mode requires. This screws me because I have a Sun
 * fixed-frequency monitor which absolutely has to have -ve sync. So
 * these flags allow the user to specify that requests for +ve sync
 * should be silently turned in -ve sync.
 */
static bool lowhsync;
static bool lowvsync;
static bool noaccel;
static bool nomtrr;

/*
 * The hardware state of the graphics card that isn't part of the
 * screeninfo.
 */
struct pm2fb_par
{
	pm2type_t	type;		/* Board type */
	unsigned char	__iomem *v_regs;/* virtual address of p_regs */
	u32		memclock;	/* memclock */
	u32		video;		/* video flags before blanking */
	u32		mem_config;	/* MemConfig reg at probe */
	u32		mem_control;	/* MemControl reg at probe */
	u32		boot_address;	/* BootAddress reg at probe */
	u32		palette[16];
	int		wc_cookie;
};

/*
 * Here we define the default structs fb_fix_screeninfo and fb_var_screeninfo
 * if we don't use modedb.
 */
static struct fb_fix_screeninfo pm2fb_fix = {
	.id =		"",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_PSEUDOCOLOR,
	.xpanstep =	1,
	.ypanstep =	1,
	.ywrapstep =	0,
	.accel =	FB_ACCEL_3DLABS_PERMEDIA2,
};

/*
 * Default video mode. In case the modedb doesn't work.
 */
static const struct fb_var_screeninfo pm2fb_var = {
	/* "640x480, 8 bpp @ 60 Hz */
	.xres =			640,
	.yres =			480,
	.xres_virtual =		640,
	.yres_virtual =		480,
	.bits_per_pixel =	8,
	.red =			{0, 8, 0},
	.blue =			{0, 8, 0},
	.green =		{0, 8, 0},
	.activate =		FB_ACTIVATE_NOW,
	.height =		-1,
	.width =		-1,
	.accel_flags =		0,
	.pixclock =		39721,
	.left_margin =		40,
	.right_margin =		24,
	.upper_margin =		32,
	.lower_margin =		11,
	.hsync_len =		96,
	.vsync_len =		2,
	.vmode =		FB_VMODE_NONINTERLACED
};

/*
 * Utility functions
 */

static inline u32 pm2_RD(struct pm2fb_par *p, s32 off)
{
	return fb_readl(p->v_regs + off);
}

static inline void pm2_WR(struct pm2fb_par *p, s32 off, u32 v)
{
	fb_writel(v, p->v_regs + off);
}

static inline u32 pm2_RDAC_RD(struct pm2fb_par *p, s32 idx)
{
	pm2_WR(p, PM2R_RD_PALETTE_WRITE_ADDRESS, idx);
	mb();
	return pm2_RD(p, PM2R_RD_INDEXED_DATA);
}

static inline u32 pm2v_RDAC_RD(struct pm2fb_par *p, s32 idx)
{
	pm2_WR(p, PM2VR_RD_INDEX_LOW, idx & 0xff);
	mb();
	return pm2_RD(p,  PM2VR_RD_INDEXED_DATA);
}

static inline void pm2_RDAC_WR(struct pm2fb_par *p, s32 idx, u32 v)
{
	pm2_WR(p, PM2R_RD_PALETTE_WRITE_ADDRESS, idx);
	wmb();
	pm2_WR(p, PM2R_RD_INDEXED_DATA, v);
	wmb();
}

static inline void pm2v_RDAC_WR(struct pm2fb_par *p, s32 idx, u32 v)
{
	pm2_WR(p, PM2VR_RD_INDEX_LOW, idx & 0xff);
	wmb();
	pm2_WR(p, PM2VR_RD_INDEXED_DATA, v);
	wmb();
}

#ifdef CONFIG_FB_PM2_FIFO_DISCONNECT
#define WAIT_FIFO(p, a)
#else
static inline void WAIT_FIFO(struct pm2fb_par *p, u32 a)
{
	while (pm2_RD(p, PM2R_IN_FIFO_SPACE) < a)
		cpu_relax();
}
#endif

/*
 * partial products for the supported horizontal resolutions.
 */
#define PACKPP(p0, p1, p2)	(((p2) << 6) | ((p1) << 3) | (p0))
static const struct {
	u16 width;
	u16 pp;
} pp_table[] = {
	{ 32,	PACKPP(1, 0, 0) }, { 64,	PACKPP(1, 1, 0) },
	{ 96,	PACKPP(1, 1, 1) }, { 128,	PACKPP(2, 1, 1) },
	{ 160,	PACKPP(2, 2, 1) }, { 192,	PACKPP(2, 2, 2) },
	{ 224,	PACKPP(3, 2, 1) }, { 256,	PACKPP(3, 2, 2) },
	{ 288,	PACKPP(3, 3, 1) }, { 320,	PACKPP(3, 3, 2) },
	{ 384,	PACKPP(3, 3, 3) }, { 416,	PACKPP(4, 3, 1) },
	{ 448,	PACKPP(4, 3, 2) }, { 512,	PACKPP(4, 3, 3) },
	{ 544,	PACKPP(4, 4, 1) }, { 576,	PACKPP(4, 4, 2) },
	{ 640,	PACKPP(4, 4, 3) }, { 768,	PACKPP(4, 4, 4) },
	{ 800,	PACKPP(5, 4, 1) }, { 832,	PACKPP(5, 4, 2) },
	{ 896,	PACKPP(5, 4, 3) }, { 1024,	PACKPP(5, 4, 4) },
	{ 1056,	PACKPP(5, 5, 1) }, { 1088,	PACKPP(5, 5, 2) },
	{ 1152,	PACKPP(5, 5, 3) }, { 1280,	PACKPP(5, 5, 4) },
	{ 1536,	PACKPP(5, 5, 5) }, { 1568,	PACKPP(6, 5, 1) },
	{ 1600,	PACKPP(6, 5, 2) }, { 1664,	PACKPP(6, 5, 3) },
	{ 1792,	PACKPP(6, 5, 4) }, { 2048,	PACKPP(6, 5, 5) },
	{ 0,	0 } };

static u32 partprod(u32 xres)
{
	int i;

	for (i = 0; pp_table[i].width && pp_table[i].width != xres; i++)
		;
	if (pp_table[i].width == 0)
		DPRINTK("invalid width %u\n", xres);
	return pp_table[i].pp;
}

static u32 to3264(u32 timing, int bpp, int is64)
{
	switch (bpp) {
	case 24:
		timing *= 3;
		fallthrough;
	case 8:
		timing >>= 1;
		fallthrough;
	case 16:
		timing >>= 1;
		fallthrough;
	case 32:
		break;
	}
	if (is64)
		timing >>= 1;
	return timing;
}

static void pm2_mnp(u32 clk, unsigned char *mm, unsigned char *nn,
		    unsigned char *pp)
{
	unsigned char m;
	unsigned char n;
	unsigned char p;
	u32 f;
	s32 curr;
	s32 delta = 100000;

	*mm = *nn = *pp = 0;
	for (n = 2; n < 15; n++) {
		for (m = 2; m; m++) {
			f = PM2_REFERENCE_CLOCK * m / n;
			if (f >= 150000 && f <= 300000) {
				for (p = 0; p < 5; p++, f >>= 1) {
					curr = (clk > f) ? clk - f : f - clk;
					if (curr < delta) {
						delta = curr;
						*mm = m;
						*nn = n;
						*pp = p;
					}
				}
			}
		}
	}
}

static void pm2v_mnp(u32 clk, unsigned char *mm, unsigned char *nn,
		     unsigned char *pp)
{
	unsigned char m;
	unsigned char n;
	unsigned char p;
	u32 f;
	s32 delta = 1000;

	*mm = *nn = *pp = 0;
	for (m = 1; m < 128; m++) {
		for (n = 2 * m + 1; n; n++) {
			for (p = 0; p < 2; p++) {
				f = (PM2_REFERENCE_CLOCK >> (p + 1)) * n / m;
				if (clk > f - delta && clk < f + delta) {
					delta = (clk > f) ? clk - f : f - clk;
					*mm = m;
					*nn = n;
					*pp = p;
				}
			}
		}
	}
}

static void clear_palette(struct pm2fb_par *p)
{
	int i = 256;

	WAIT_FIFO(p, 1);
	pm2_WR(p, PM2R_RD_PALETTE_WRITE_ADDRESS, 0);
	wmb();
	while (i--) {
		WAIT_FIFO(p, 3);
		pm2_WR(p, PM2R_RD_PALETTE_DATA, 0);
		pm2_WR(p, PM2R_RD_PALETTE_DATA, 0);
		pm2_WR(p, PM2R_RD_PALETTE_DATA, 0);
	}
}

static void reset_card(struct pm2fb_par *p)
{
	if (p->type == PM2_TYPE_PERMEDIA2V)
		pm2_WR(p, PM2VR_RD_INDEX_HIGH, 0);
	pm2_WR(p, PM2R_RESET_STATUS, 0);
	mb();
	while (pm2_RD(p, PM2R_RESET_STATUS) & PM2F_BEING_RESET)
		cpu_relax();
	mb();
#ifdef CONFIG_FB_PM2_FIFO_DISCONNECT
	DPRINTK("FIFO disconnect enabled\n");
	pm2_WR(p, PM2R_FIFO_DISCON, 1);
	mb();
#endif

	/* Restore stashed memory config information from probe */
	WAIT_FIFO(p, 3);
	pm2_WR(p, PM2R_MEM_CONTROL, p->mem_control);
	pm2_WR(p, PM2R_BOOT_ADDRESS, p->boot_address);
	wmb();
	pm2_WR(p, PM2R_MEM_CONFIG, p->mem_config);
}

static void reset_config(struct pm2fb_par *p)
{
	WAIT_FIFO(p, 53);
	pm2_WR(p, PM2R_CHIP_CONFIG, pm2_RD(p, PM2R_CHIP_CONFIG) &
			~(PM2F_VGA_ENABLE | PM2F_VGA_FIXED));
	pm2_WR(p, PM2R_BYPASS_WRITE_MASK, ~(0L));
	pm2_WR(p, PM2R_FRAMEBUFFER_WRITE_MASK, ~(0L));
	pm2_WR(p, PM2R_FIFO_CONTROL, 0);
	pm2_WR(p, PM2R_APERTURE_ONE, 0);
	pm2_WR(p, PM2R_APERTURE_TWO, 0);
	pm2_WR(p, PM2R_RASTERIZER_MODE, 0);
	pm2_WR(p, PM2R_DELTA_MODE, PM2F_DELTA_ORDER_RGB);
	pm2_WR(p, PM2R_LB_READ_FORMAT, 0);
	pm2_WR(p, PM2R_LB_WRITE_FORMAT, 0);
	pm2_WR(p, PM2R_LB_READ_MODE, 0);
	pm2_WR(p, PM2R_LB_SOURCE_OFFSET, 0);
	pm2_WR(p, PM2R_FB_SOURCE_OFFSET, 0);
	pm2_WR(p, PM2R_FB_PIXEL_OFFSET, 0);
	pm2_WR(p, PM2R_FB_WINDOW_BASE, 0);
	pm2_WR(p, PM2R_LB_WINDOW_BASE, 0);
	pm2_WR(p, PM2R_FB_SOFT_WRITE_MASK, ~(0L));
	pm2_WR(p, PM2R_FB_HARD_WRITE_MASK, ~(0L));
	pm2_WR(p, PM2R_FB_READ_PIXEL, 0);
	pm2_WR(p, PM2R_DITHER_MODE, 0);
	pm2_WR(p, PM2R_AREA_STIPPLE_MODE, 0);
	pm2_WR(p, PM2R_DEPTH_MODE, 0);
	pm2_WR(p, PM2R_STENCIL_MODE, 0);
	pm2_WR(p, PM2R_TEXTURE_ADDRESS_MODE, 0);
	pm2_WR(p, PM2R_TEXTURE_READ_MODE, 0);
	pm2_WR(p, PM2R_TEXEL_LUT_MODE, 0);
	pm2_WR(p, PM2R_YUV_MODE, 0);
	pm2_WR(p, PM2R_COLOR_DDA_MODE, 0);
	pm2_WR(p, PM2R_TEXTURE_COLOR_MODE, 0);
	pm2_WR(p, PM2R_FOG_MODE, 0);
	pm2_WR(p, PM2R_ALPHA_BLEND_MODE, 0);
	pm2_WR(p, PM2R_LOGICAL_OP_MODE, 0);
	pm2_WR(p, PM2R_STATISTICS_MODE, 0);
	pm2_WR(p, PM2R_SCISSOR_MODE, 0);
	pm2_WR(p, PM2R_FILTER_MODE, PM2F_SYNCHRONIZATION);
	pm2_WR(p, PM2R_RD_PIXEL_MASK, 0xff);
	switch (p->type) {
	case PM2_TYPE_PERMEDIA2:
		pm2_RDAC_WR(p, PM2I_RD_MODE_CONTROL, 0); /* no overlay */
		pm2_RDAC_WR(p, PM2I_RD_CURSOR_CONTROL, 0);
		pm2_RDAC_WR(p, PM2I_RD_MISC_CONTROL, PM2F_RD_PALETTE_WIDTH_8);
		pm2_RDAC_WR(p, PM2I_RD_COLOR_KEY_CONTROL, 0);
		pm2_RDAC_WR(p, PM2I_RD_OVERLAY_KEY, 0);
		pm2_RDAC_WR(p, PM2I_RD_RED_KEY, 0);
		pm2_RDAC_WR(p, PM2I_RD_GREEN_KEY, 0);
		pm2_RDAC_WR(p, PM2I_RD_BLUE_KEY, 0);
		break;
	case PM2_TYPE_PERMEDIA2V:
		pm2v_RDAC_WR(p, PM2VI_RD_MISC_CONTROL, 1); /* 8bit */
		break;
	}
}

static void set_aperture(struct pm2fb_par *p, u32 depth)
{
	/*
	 * The hardware is little-endian. When used in big-endian
	 * hosts, the on-chip aperture settings are used where
	 * possible to translate from host to card byte order.
	 */
	WAIT_FIFO(p, 2);
#ifdef __LITTLE_ENDIAN
	pm2_WR(p, PM2R_APERTURE_ONE, PM2F_APERTURE_STANDARD);
#else
	switch (depth) {
	case 24:	/* RGB->BGR */
		/*
		 * We can't use the aperture to translate host to
		 * card byte order here, so we switch to BGR mode
		 * in pm2fb_set_par().
		 */
	case 8:		/* B->B */
		pm2_WR(p, PM2R_APERTURE_ONE, PM2F_APERTURE_STANDARD);
		break;
	case 16:	/* HL->LH */
		pm2_WR(p, PM2R_APERTURE_ONE, PM2F_APERTURE_HALFWORDSWAP);
		break;
	case 32:	/* RGBA->ABGR */
		pm2_WR(p, PM2R_APERTURE_ONE, PM2F_APERTURE_BYTESWAP);
		break;
	}
#endif

	/* We don't use aperture two, so this may be superflous */
	pm2_WR(p, PM2R_APERTURE_TWO, PM2F_APERTURE_STANDARD);
}

static void set_color(struct pm2fb_par *p, unsigned char regno,
		      unsigned char r, unsigned char g, unsigned char b)
{
	WAIT_FIFO(p, 4);
	pm2_WR(p, PM2R_RD_PALETTE_WRITE_ADDRESS, regno);
	wmb();
	pm2_WR(p, PM2R_RD_PALETTE_DATA, r);
	wmb();
	pm2_WR(p, PM2R_RD_PALETTE_DATA, g);
	wmb();
	pm2_WR(p, PM2R_RD_PALETTE_DATA, b);
}

static void set_memclock(struct pm2fb_par *par, u32 clk)
{
	int i;
	unsigned char m, n, p;

	switch (par->type) {
	case PM2_TYPE_PERMEDIA2V:
		pm2v_mnp(clk/2, &m, &n, &p);
		WAIT_FIFO(par, 12);
		pm2_WR(par, PM2VR_RD_INDEX_HIGH, PM2VI_RD_MCLK_CONTROL >> 8);
		pm2v_RDAC_WR(par, PM2VI_RD_MCLK_CONTROL, 0);
		pm2v_RDAC_WR(par, PM2VI_RD_MCLK_PRESCALE, m);
		pm2v_RDAC_WR(par, PM2VI_RD_MCLK_FEEDBACK, n);
		pm2v_RDAC_WR(par, PM2VI_RD_MCLK_POSTSCALE, p);
		pm2v_RDAC_WR(par, PM2VI_RD_MCLK_CONTROL, 1);
		rmb();
		for (i = 256; i; i--)
			if (pm2v_RDAC_RD(par, PM2VI_RD_MCLK_CONTROL) & 2)
				break;
		pm2_WR(par, PM2VR_RD_INDEX_HIGH, 0);
		break;
	case PM2_TYPE_PERMEDIA2:
		pm2_mnp(clk, &m, &n, &p);
		WAIT_FIFO(par, 10);
		pm2_RDAC_WR(par, PM2I_RD_MEMORY_CLOCK_3, 6);
		pm2_RDAC_WR(par, PM2I_RD_MEMORY_CLOCK_1, m);
		pm2_RDAC_WR(par, PM2I_RD_MEMORY_CLOCK_2, n);
		pm2_RDAC_WR(par, PM2I_RD_MEMORY_CLOCK_3, 8|p);
		pm2_RDAC_RD(par, PM2I_RD_MEMORY_CLOCK_STATUS);
		rmb();
		for (i = 256; i; i--)
			if (pm2_RD(par, PM2R_RD_INDEXED_DATA) & PM2F_PLL_LOCKED)
				break;
		break;
	}
}

static void set_pixclock(struct pm2fb_par *par, u32 clk)
{
	int i;
	unsigned char m, n, p;

	switch (par->type) {
	case PM2_TYPE_PERMEDIA2:
		pm2_mnp(clk, &m, &n, &p);
		WAIT_FIFO(par, 10);
		pm2_RDAC_WR(par, PM2I_RD_PIXEL_CLOCK_A3, 0);
		pm2_RDAC_WR(par, PM2I_RD_PIXEL_CLOCK_A1, m);
		pm2_RDAC_WR(par, PM2I_RD_PIXEL_CLOCK_A2, n);
		pm2_RDAC_WR(par, PM2I_RD_PIXEL_CLOCK_A3, 8|p);
		pm2_RDAC_RD(par, PM2I_RD_PIXEL_CLOCK_STATUS);
		rmb();
		for (i = 256; i; i--)
			if (pm2_RD(par, PM2R_RD_INDEXED_DATA) & PM2F_PLL_LOCKED)
				break;
		break;
	case PM2_TYPE_PERMEDIA2V:
		pm2v_mnp(clk/2, &m, &n, &p);
		WAIT_FIFO(par, 8);
		pm2_WR(par, PM2VR_RD_INDEX_HIGH, PM2VI_RD_CLK0_PRESCALE >> 8);
		pm2v_RDAC_WR(par, PM2VI_RD_CLK0_PRESCALE, m);
		pm2v_RDAC_WR(par, PM2VI_RD_CLK0_FEEDBACK, n);
		pm2v_RDAC_WR(par, PM2VI_RD_CLK0_POSTSCALE, p);
		pm2_WR(par, PM2VR_RD_INDEX_HIGH, 0);
		break;
	}
}

static void set_video(struct pm2fb_par *p, u32 video)
{
	u32 tmp;
	u32 vsync = video;

	DPRINTK("video = 0x%x\n", video);

	/*
	 * The hardware cursor needs +vsync to recognise vert retrace.
	 * We may not be using the hardware cursor, but the X Glint
	 * driver may well. So always set +hsync/+vsync and then set
	 * the RAMDAC to invert the sync if necessary.
	 */
	vsync &= ~(PM2F_HSYNC_MASK | PM2F_VSYNC_MASK);
	vsync |= PM2F_HSYNC_ACT_HIGH | PM2F_VSYNC_ACT_HIGH;

	WAIT_FIFO(p, 3);
	pm2_WR(p, PM2R_VIDEO_CONTROL, vsync);

	switch (p->type) {
	case PM2_TYPE_PERMEDIA2:
		tmp = PM2F_RD_PALETTE_WIDTH_8;
		if ((video & PM2F_HSYNC_MASK) == PM2F_HSYNC_ACT_LOW)
			tmp |= 4; /* invert hsync */
		if ((video & PM2F_VSYNC_MASK) == PM2F_VSYNC_ACT_LOW)
			tmp |= 8; /* invert vsync */
		pm2_RDAC_WR(p, PM2I_RD_MISC_CONTROL, tmp);
		break;
	case PM2_TYPE_PERMEDIA2V:
		tmp = 0;
		if ((video & PM2F_HSYNC_MASK) == PM2F_HSYNC_ACT_LOW)
			tmp |= 1; /* invert hsync */
		if ((video & PM2F_VSYNC_MASK) == PM2F_VSYNC_ACT_LOW)
			tmp |= 4; /* invert vsync */
		pm2v_RDAC_WR(p, PM2VI_RD_SYNC_CONTROL, tmp);
		break;
	}
}

/*
 *	pm2fb_check_var - Optional function. Validates a var passed in.
 *	@var: frame buffer variable screen structure
 *	@info: frame buffer structure that represents a single frame buffer
 *
 *	Checks to see if the hardware supports the state requested by
 *	var passed in.
 *
 *	Returns negative errno on error, or zero on success.
 */
static int pm2fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	u32 lpitch;

	if (var->bits_per_pixel != 8  && var->bits_per_pixel != 16 &&
	    var->bits_per_pixel != 24 && var->bits_per_pixel != 32) {
		DPRINTK("depth not supported: %u\n", var->bits_per_pixel);
		return -EINVAL;
	}

	if (var->xres != var->xres_virtual) {
		DPRINTK("virtual x resolution != "
			"physical x resolution not supported\n");
		return -EINVAL;
	}

	if (var->yres > var->yres_virtual) {
		DPRINTK("virtual y resolution < "
			"physical y resolution not possible\n");
		return -EINVAL;
	}

	/* permedia cannot blit over 2048 */
	if (var->yres_virtual > 2047) {
		var->yres_virtual = 2047;
	}

	if (var->xoffset) {
		DPRINTK("xoffset not supported\n");
		return -EINVAL;
	}

	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		DPRINTK("interlace not supported\n");
		return -EINVAL;
	}

	var->xres = (var->xres + 15) & ~15; /* could sometimes be 8 */
	lpitch = var->xres * ((var->bits_per_pixel + 7) >> 3);

	if (var->xres < 320 || var->xres > 1600) {
		DPRINTK("width not supported: %u\n", var->xres);
		return -EINVAL;
	}

	if (var->yres < 200 || var->yres > 1200) {
		DPRINTK("height not supported: %u\n", var->yres);
		return -EINVAL;
	}

	if (lpitch * var->yres_virtual > info->fix.smem_len) {
		DPRINTK("no memory for screen (%ux%ux%u)\n",
			var->xres, var->yres_virtual, var->bits_per_pixel);
		return -EINVAL;
	}

	if (!var->pixclock) {
		DPRINTK("pixclock is zero\n");
		return -EINVAL;
	}

	if (PICOS2KHZ(var->pixclock) > PM2_MAX_PIXCLOCK) {
		DPRINTK("pixclock too high (%ldKHz)\n",
			PICOS2KHZ(var->pixclock));
		return -EINVAL;
	}

	var->transp.offset = 0;
	var->transp.length = 0;
	switch (var->bits_per_pixel) {
	case 8:
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		break;
	case 16:
		var->red.offset   = 11;
		var->red.length   = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset  = 0;
		var->blue.length  = 5;
		break;
	case 32:
		var->transp.offset = 24;
		var->transp.length = 8;
		var->red.offset	  = 16;
		var->green.offset = 8;
		var->blue.offset  = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		break;
	case 24:
#ifdef __BIG_ENDIAN
		var->red.offset   = 0;
		var->blue.offset  = 16;
#else
		var->red.offset   = 16;
		var->blue.offset  = 0;
#endif
		var->green.offset = 8;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		break;
	}
	var->height = -1;
	var->width = -1;

	var->accel_flags = 0;	/* Can't mmap if this is on */

	DPRINTK("Checking graphics mode at %dx%d depth %d\n",
		var->xres, var->yres, var->bits_per_pixel);
	return 0;
}

/**
 *	pm2fb_set_par - Alters the hardware state.
 *	@info: frame buffer structure that represents a single frame buffer
 *
 *	Using the fb_var_screeninfo in fb_info we set the resolution of the
 *	this particular framebuffer.
 */
static int pm2fb_set_par(struct fb_info *info)
{
	struct pm2fb_par *par = info->par;
	u32 pixclock;
	u32 width = (info->var.xres_virtual + 7) & ~7;
	u32 height = info->var.yres_virtual;
	u32 depth = (info->var.bits_per_pixel + 7) & ~7;
	u32 hsstart, hsend, hbend, htotal;
	u32 vsstart, vsend, vbend, vtotal;
	u32 stride;
	u32 base;
	u32 video = 0;
	u32 clrmode = PM2F_RD_COLOR_MODE_RGB | PM2F_RD_GUI_ACTIVE;
	u32 txtmap = 0;
	u32 pixsize = 0;
	u32 clrformat = 0;
	u32 misc = 1; /* 8-bit DAC */
	u32 xres = (info->var.xres + 31) & ~31;
	int data64;

	reset_card(par);
	reset_config(par);
	clear_palette(par);
	if (par->memclock)
		set_memclock(par, par->memclock);

	depth = (depth > 32) ? 32 : depth;
	data64 = depth > 8 || par->type == PM2_TYPE_PERMEDIA2V;

	pixclock = PICOS2KHZ(info->var.pixclock);
	if (pixclock > PM2_MAX_PIXCLOCK) {
		DPRINTK("pixclock too high (%uKHz)\n", pixclock);
		return -EINVAL;
	}

	hsstart = to3264(info->var.right_margin, depth, data64);
	hsend = hsstart + to3264(info->var.hsync_len, depth, data64);
	hbend = hsend + to3264(info->var.left_margin, depth, data64);
	htotal = to3264(xres, depth, data64) + hbend - 1;
	vsstart = (info->var.lower_margin)
		? info->var.lower_margin - 1
		: 0;	/* FIXME! */
	vsend = info->var.lower_margin + info->var.vsync_len - 1;
	vbend = info->var.lower_margin + info->var.vsync_len +
		info->var.upper_margin;
	vtotal = info->var.yres + vbend - 1;
	stride = to3264(width, depth, 1);
	base = to3264(info->var.yoffset * xres + info->var.xoffset, depth, 1);
	if (data64)
		video |= PM2F_DATA_64_ENABLE;

	if (info->var.sync & FB_SYNC_HOR_HIGH_ACT) {
		if (lowhsync) {
			DPRINTK("ignoring +hsync, using -hsync.\n");
			video |= PM2F_HSYNC_ACT_LOW;
		} else
			video |= PM2F_HSYNC_ACT_HIGH;
	} else
		video |= PM2F_HSYNC_ACT_LOW;

	if (info->var.sync & FB_SYNC_VERT_HIGH_ACT) {
		if (lowvsync) {
			DPRINTK("ignoring +vsync, using -vsync.\n");
			video |= PM2F_VSYNC_ACT_LOW;
		} else
			video |= PM2F_VSYNC_ACT_HIGH;
	} else
		video |= PM2F_VSYNC_ACT_LOW;

	if ((info->var.vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		DPRINTK("interlaced not supported\n");
		return -EINVAL;
	}
	if ((info->var.vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE)
		video |= PM2F_LINE_DOUBLE;
	if ((info->var.activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW)
		video |= PM2F_VIDEO_ENABLE;
	par->video = video;

	info->fix.visual =
		(depth == 8) ? FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;
	info->fix.line_length = info->var.xres * depth / 8;
	info->cmap.len = 256;

	/*
	 * Settings calculated. Now write them out.
	 */
	if (par->type == PM2_TYPE_PERMEDIA2V) {
		WAIT_FIFO(par, 1);
		pm2_WR(par, PM2VR_RD_INDEX_HIGH, 0);
	}

	set_aperture(par, depth);

	mb();
	WAIT_FIFO(par, 19);
	switch (depth) {
	case 8:
		pm2_WR(par, PM2R_FB_READ_PIXEL, 0);
		clrformat = 0x2e;
		break;
	case 16:
		pm2_WR(par, PM2R_FB_READ_PIXEL, 1);
		clrmode |= PM2F_RD_TRUECOLOR | PM2F_RD_PIXELFORMAT_RGB565;
		txtmap = PM2F_TEXTEL_SIZE_16;
		pixsize = 1;
		clrformat = 0x70;
		misc |= 8;
		break;
	case 32:
		pm2_WR(par, PM2R_FB_READ_PIXEL, 2);
		clrmode |= PM2F_RD_TRUECOLOR | PM2F_RD_PIXELFORMAT_RGBA8888;
		txtmap = PM2F_TEXTEL_SIZE_32;
		pixsize = 2;
		clrformat = 0x20;
		misc |= 8;
		break;
	case 24:
		pm2_WR(par, PM2R_FB_READ_PIXEL, 4);
		clrmode |= PM2F_RD_TRUECOLOR | PM2F_RD_PIXELFORMAT_RGB888;
		txtmap = PM2F_TEXTEL_SIZE_24;
		pixsize = 4;
		clrformat = 0x20;
		misc |= 8;
		break;
	}
	pm2_WR(par, PM2R_FB_WRITE_MODE, PM2F_FB_WRITE_ENABLE);
	pm2_WR(par, PM2R_FB_READ_MODE, partprod(xres));
	pm2_WR(par, PM2R_LB_READ_MODE, partprod(xres));
	pm2_WR(par, PM2R_TEXTURE_MAP_FORMAT, txtmap | partprod(xres));
	pm2_WR(par, PM2R_H_TOTAL, htotal);
	pm2_WR(par, PM2R_HS_START, hsstart);
	pm2_WR(par, PM2R_HS_END, hsend);
	pm2_WR(par, PM2R_HG_END, hbend);
	pm2_WR(par, PM2R_HB_END, hbend);
	pm2_WR(par, PM2R_V_TOTAL, vtotal);
	pm2_WR(par, PM2R_VS_START, vsstart);
	pm2_WR(par, PM2R_VS_END, vsend);
	pm2_WR(par, PM2R_VB_END, vbend);
	pm2_WR(par, PM2R_SCREEN_STRIDE, stride);
	wmb();
	pm2_WR(par, PM2R_WINDOW_ORIGIN, 0);
	pm2_WR(par, PM2R_SCREEN_SIZE, (height << 16) | width);
	pm2_WR(par, PM2R_SCISSOR_MODE, PM2F_SCREEN_SCISSOR_ENABLE);
	wmb();
	pm2_WR(par, PM2R_SCREEN_BASE, base);
	wmb();
	set_video(par, video);
	WAIT_FIFO(par, 10);
	switch (par->type) {
	case PM2_TYPE_PERMEDIA2:
		pm2_RDAC_WR(par, PM2I_RD_COLOR_MODE, clrmode);
		pm2_RDAC_WR(par, PM2I_RD_COLOR_KEY_CONTROL,
				(depth == 8) ? 0 : PM2F_COLOR_KEY_TEST_OFF);
		break;
	case PM2_TYPE_PERMEDIA2V:
		pm2v_RDAC_WR(par, PM2VI_RD_DAC_CONTROL, 0);
		pm2v_RDAC_WR(par, PM2VI_RD_PIXEL_SIZE, pixsize);
		pm2v_RDAC_WR(par, PM2VI_RD_COLOR_FORMAT, clrformat);
		pm2v_RDAC_WR(par, PM2VI_RD_MISC_CONTROL, misc);
		pm2v_RDAC_WR(par, PM2VI_RD_OVERLAY_KEY, 0);
		break;
	}
	set_pixclock(par, pixclock);
	DPRINTK("Setting graphics mode at %dx%d depth %d\n",
		info->var.xres, info->var.yres, info->var.bits_per_pixel);
	return 0;
}

/**
 *	pm2fb_setcolreg - Sets a color register.
 *	@regno: boolean, 0 copy local, 1 get_user() function
 *	@red: frame buffer colormap structure
 *	@green: The green value which can be up to 16 bits wide
 *	@blue:  The blue value which can be up to 16 bits wide.
 *	@transp: If supported the alpha value which can be up to 16 bits wide.
 *	@info: frame buffer info structure
 *
 *	Set a single color register. The values supplied have a 16 bit
 *	magnitude which needs to be scaled in this function for the hardware.
 *	Pretty much a direct lift from tdfxfb.c.
 *
 *	Returns negative errno on error, or zero on success.
 */
static int pm2fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
	struct pm2fb_par *par = info->par;

	if (regno >= info->cmap.len)  /* no. of hw registers */
		return -EINVAL;
	/*
	 * Program hardware... do anything you want with transp
	 */

	/* grayscale works only partially under directcolor */
	/* grayscale = 0.30*R + 0.59*G + 0.11*B */
	if (info->var.grayscale)
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;

	/* Directcolor:
	 *   var->{color}.offset contains start of bitfield
	 *   var->{color}.length contains length of bitfield
	 *   {hardwarespecific} contains width of DAC
	 *   cmap[X] is programmed to
	 *   (X << red.offset) | (X << green.offset) | (X << blue.offset)
	 *   RAMDAC[X] is programmed to (red, green, blue)
	 *
	 * Pseudocolor:
	 *    uses offset = 0 && length = DAC register width.
	 *    var->{color}.offset is 0
	 *    var->{color}.length contains width of DAC
	 *    cmap is not used
	 *    DAC[X] is programmed to (red, green, blue)
	 * Truecolor:
	 *    does not use RAMDAC (usually has 3 of them).
	 *    var->{color}.offset contains start of bitfield
	 *    var->{color}.length contains length of bitfield
	 *    cmap is programmed to
	 *    (red << red.offset) | (green << green.offset) |
	 *    (blue << blue.offset) | (transp << transp.offset)
	 *    RAMDAC does not exist
	 */
#define CNVT_TOHW(val, width) ((((val) << (width)) + 0x7FFF -(val)) >> 16)
	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
		red = CNVT_TOHW(red, info->var.red.length);
		green = CNVT_TOHW(green, info->var.green.length);
		blue = CNVT_TOHW(blue, info->var.blue.length);
		transp = CNVT_TOHW(transp, info->var.transp.length);
		break;
	case FB_VISUAL_DIRECTCOLOR:
		/* example here assumes 8 bit DAC. Might be different
		 * for your hardware */
		red = CNVT_TOHW(red, 8);
		green = CNVT_TOHW(green, 8);
		blue = CNVT_TOHW(blue, 8);
		/* hey, there is bug in transp handling... */
		transp = CNVT_TOHW(transp, 8);
		break;
	}
#undef CNVT_TOHW
	/* Truecolor has hardware independent palette */
	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
		u32 v;

		if (regno >= 16)
			return -EINVAL;

		v = (red << info->var.red.offset) |
			(green << info->var.green.offset) |
			(blue << info->var.blue.offset) |
			(transp << info->var.transp.offset);

		switch (info->var.bits_per_pixel) {
		case 8:
			break;
		case 16:
		case 24:
		case 32:
			par->palette[regno] = v;
			break;
		}
		return 0;
	} else if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR)
		set_color(par, regno, red, green, blue);

	return 0;
}

/**
 *	pm2fb_pan_display - Pans the display.
 *	@var: frame buffer variable screen structure
 *	@info: frame buffer structure that represents a single frame buffer
 *
 *	Pan (or wrap, depending on the `vmode' field) the display using the
 *	`xoffset' and `yoffset' fields of the `var' structure.
 *	If the values don't fit, return -EINVAL.
 *
 *	Returns negative errno on error, or zero on success.
 *
 */
static int pm2fb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	struct pm2fb_par *p = info->par;
	u32 base;
	u32 depth = (info->var.bits_per_pixel + 7) & ~7;
	u32 xres = (info->var.xres + 31) & ~31;

	depth = (depth > 32) ? 32 : depth;
	base = to3264(var->yoffset * xres + var->xoffset, depth, 1);
	WAIT_FIFO(p, 1);
	pm2_WR(p, PM2R_SCREEN_BASE, base);
	return 0;
}

/**
 *	pm2fb_blank - Blanks the display.
 *	@blank_mode: the blank mode we want.
 *	@info: frame buffer structure that represents a single frame buffer
 *
 *	Blank the screen if blank_mode != 0, else unblank. Return 0 if
 *	blanking succeeded, != 0 if un-/blanking failed due to e.g. a
 *	video mode which doesn't support it. Implements VESA suspend
 *	and powerdown modes on hardware that supports disabling hsync/vsync:
 *	blank_mode == 2: suspend vsync
 *	blank_mode == 3: suspend hsync
 *	blank_mode == 4: powerdown
 *
 *	Returns negative errno on error, or zero on success.
 *
 */
static int pm2fb_blank(int blank_mode, struct fb_info *info)
{
	struct pm2fb_par *par = info->par;
	u32 video = par->video;

	DPRINTK("blank_mode %d\n", blank_mode);

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		/* Screen: On */
		video |= PM2F_VIDEO_ENABLE;
		break;
	case FB_BLANK_NORMAL:
		/* Screen: Off */
		video &= ~PM2F_VIDEO_ENABLE;
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		/* VSync: Off */
		video &= ~(PM2F_VSYNC_MASK | PM2F_BLANK_LOW);
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		/* HSync: Off */
		video &= ~(PM2F_HSYNC_MASK | PM2F_BLANK_LOW);
		break;
	case FB_BLANK_POWERDOWN:
		/* HSync: Off, VSync: Off */
		video &= ~(PM2F_VSYNC_MASK | PM2F_HSYNC_MASK | PM2F_BLANK_LOW);
		break;
	}
	set_video(par, video);
	return 0;
}

static int pm2fb_sync(struct fb_info *info)
{
	struct pm2fb_par *par = info->par;

	WAIT_FIFO(par, 1);
	pm2_WR(par, PM2R_SYNC, 0);
	mb();
	do {
		while (pm2_RD(par, PM2R_OUT_FIFO_WORDS) == 0)
			cpu_relax();
	} while (pm2_RD(par, PM2R_OUT_FIFO) != PM2TAG(PM2R_SYNC));

	return 0;
}

static void pm2fb_fillrect(struct fb_info *info,
				const struct fb_fillrect *region)
{
	struct pm2fb_par *par = info->par;
	struct fb_fillrect modded;
	int vxres, vyres;
	u32 color = (info->fix.visual == FB_VISUAL_TRUECOLOR) ?
		((u32 *)info->pseudo_palette)[region->color] : region->color;

	if (info->state != FBINFO_STATE_RUNNING)
		return;
	if ((info->flags & FBINFO_HWACCEL_DISABLED) ||
		region->rop != ROP_COPY ) {
		cfb_fillrect(info, region);
		return;
	}

	vxres = info->var.xres_virtual;
	vyres = info->var.yres_virtual;

	memcpy(&modded, region, sizeof(struct fb_fillrect));

	if (!modded.width || !modded.height ||
	    modded.dx >= vxres || modded.dy >= vyres)
		return;

	if (modded.dx + modded.width  > vxres)
		modded.width  = vxres - modded.dx;
	if (modded.dy + modded.height > vyres)
		modded.height = vyres - modded.dy;

	if (info->var.bits_per_pixel == 8)
		color |= color << 8;
	if (info->var.bits_per_pixel <= 16)
		color |= color << 16;

	WAIT_FIFO(par, 3);
	pm2_WR(par, PM2R_CONFIG, PM2F_CONFIG_FB_WRITE_ENABLE);
	pm2_WR(par, PM2R_RECTANGLE_ORIGIN, (modded.dy << 16) | modded.dx);
	pm2_WR(par, PM2R_RECTANGLE_SIZE, (modded.height << 16) | modded.width);
	if (info->var.bits_per_pixel != 24) {
		WAIT_FIFO(par, 2);
		pm2_WR(par, PM2R_FB_BLOCK_COLOR, color);
		wmb();
		pm2_WR(par, PM2R_RENDER,
				PM2F_RENDER_RECTANGLE | PM2F_RENDER_FASTFILL);
	} else {
		WAIT_FIFO(par, 4);
		pm2_WR(par, PM2R_COLOR_DDA_MODE, 1);
		pm2_WR(par, PM2R_CONSTANT_COLOR, color);
		wmb();
		pm2_WR(par, PM2R_RENDER,
				PM2F_RENDER_RECTANGLE |
				PM2F_INCREASE_X | PM2F_INCREASE_Y );
		pm2_WR(par, PM2R_COLOR_DDA_MODE, 0);
	}
}

static void pm2fb_copyarea(struct fb_info *info,
				const struct fb_copyarea *area)
{
	struct pm2fb_par *par = info->par;
	struct fb_copyarea modded;
	u32 vxres, vyres;

	if (info->state != FBINFO_STATE_RUNNING)
		return;
	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_copyarea(info, area);
		return;
	}

	memcpy(&modded, area, sizeof(struct fb_copyarea));

	vxres = info->var.xres_virtual;
	vyres = info->var.yres_virtual;

	if (!modded.width || !modded.height ||
	    modded.sx >= vxres || modded.sy >= vyres ||
	    modded.dx >= vxres || modded.dy >= vyres)
		return;

	if (modded.sx + modded.width > vxres)
		modded.width = vxres - modded.sx;
	if (modded.dx + modded.width > vxres)
		modded.width = vxres - modded.dx;
	if (modded.sy + modded.height > vyres)
		modded.height = vyres - modded.sy;
	if (modded.dy + modded.height > vyres)
		modded.height = vyres - modded.dy;

	WAIT_FIFO(par, 5);
	pm2_WR(par, PM2R_CONFIG, PM2F_CONFIG_FB_WRITE_ENABLE |
		PM2F_CONFIG_FB_READ_SOURCE_ENABLE);
	pm2_WR(par, PM2R_FB_SOURCE_DELTA,
			((modded.sy - modded.dy) & 0xfff) << 16 |
			((modded.sx - modded.dx) & 0xfff));
	pm2_WR(par, PM2R_RECTANGLE_ORIGIN, (modded.dy << 16) | modded.dx);
	pm2_WR(par, PM2R_RECTANGLE_SIZE, (modded.height << 16) | modded.width);
	wmb();
	pm2_WR(par, PM2R_RENDER, PM2F_RENDER_RECTANGLE |
				(modded.dx < modded.sx ? PM2F_INCREASE_X : 0) |
				(modded.dy < modded.sy ? PM2F_INCREASE_Y : 0));
}

static void pm2fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct pm2fb_par *par = info->par;
	u32 height = image->height;
	u32 fgx, bgx;
	const u32 *src = (const u32 *)image->data;
	u32 xres = (info->var.xres + 31) & ~31;
	int raster_mode = 1; /* invert bits */

#ifdef __LITTLE_ENDIAN
	raster_mode |= 3 << 7; /* reverse byte order */
#endif

	if (info->state != FBINFO_STATE_RUNNING)
		return;
	if (info->flags & FBINFO_HWACCEL_DISABLED || image->depth != 1) {
		cfb_imageblit(info, image);
		return;
	}
	switch (info->fix.visual) {
	case FB_VISUAL_PSEUDOCOLOR:
		fgx = image->fg_color;
		bgx = image->bg_color;
		break;
	case FB_VISUAL_TRUECOLOR:
	default:
		fgx = par->palette[image->fg_color];
		bgx = par->palette[image->bg_color];
		break;
	}
	if (info->var.bits_per_pixel == 8) {
		fgx |= fgx << 8;
		bgx |= bgx << 8;
	}
	if (info->var.bits_per_pixel <= 16) {
		fgx |= fgx << 16;
		bgx |= bgx << 16;
	}

	WAIT_FIFO(par, 13);
	pm2_WR(par, PM2R_FB_READ_MODE, partprod(xres));
	pm2_WR(par, PM2R_SCISSOR_MIN_XY,
			((image->dy & 0xfff) << 16) | (image->dx & 0x0fff));
	pm2_WR(par, PM2R_SCISSOR_MAX_XY,
			(((image->dy + image->height) & 0x0fff) << 16) |
			((image->dx + image->width) & 0x0fff));
	pm2_WR(par, PM2R_SCISSOR_MODE, 1);
	/* GXcopy & UNIT_ENABLE */
	pm2_WR(par, PM2R_LOGICAL_OP_MODE, (0x3 << 1) | 1);
	pm2_WR(par, PM2R_RECTANGLE_ORIGIN,
			((image->dy & 0xfff) << 16) | (image->dx & 0x0fff));
	pm2_WR(par, PM2R_RECTANGLE_SIZE,
			((image->height & 0x0fff) << 16) |
			((image->width) & 0x0fff));
	if (info->var.bits_per_pixel == 24) {
		pm2_WR(par, PM2R_COLOR_DDA_MODE, 1);
		/* clear area */
		pm2_WR(par, PM2R_CONSTANT_COLOR, bgx);
		pm2_WR(par, PM2R_RENDER,
			PM2F_RENDER_RECTANGLE |
			PM2F_INCREASE_X | PM2F_INCREASE_Y);
		/* BitMapPackEachScanline */
		pm2_WR(par, PM2R_RASTERIZER_MODE, raster_mode | (1 << 9));
		pm2_WR(par, PM2R_CONSTANT_COLOR, fgx);
		pm2_WR(par, PM2R_RENDER,
			PM2F_RENDER_RECTANGLE |
			PM2F_INCREASE_X | PM2F_INCREASE_Y |
			PM2F_RENDER_SYNC_ON_BIT_MASK);
	} else {
		pm2_WR(par, PM2R_COLOR_DDA_MODE, 0);
		/* clear area */
		pm2_WR(par, PM2R_FB_BLOCK_COLOR, bgx);
		pm2_WR(par, PM2R_RENDER,
			PM2F_RENDER_RECTANGLE |
			PM2F_RENDER_FASTFILL |
			PM2F_INCREASE_X | PM2F_INCREASE_Y);
		pm2_WR(par, PM2R_RASTERIZER_MODE, raster_mode);
		pm2_WR(par, PM2R_FB_BLOCK_COLOR, fgx);
		pm2_WR(par, PM2R_RENDER,
			PM2F_RENDER_RECTANGLE |
			PM2F_INCREASE_X | PM2F_INCREASE_Y |
			PM2F_RENDER_FASTFILL |
			PM2F_RENDER_SYNC_ON_BIT_MASK);
	}

	while (height--) {
		int width = ((image->width + 7) >> 3)
				+ info->pixmap.scan_align - 1;
		width >>= 2;
		WAIT_FIFO(par, width);
		while (width--) {
			pm2_WR(par, PM2R_BIT_MASK_PATTERN, *src);
			src++;
		}
	}
	WAIT_FIFO(par, 3);
	pm2_WR(par, PM2R_RASTERIZER_MODE, 0);
	pm2_WR(par, PM2R_COLOR_DDA_MODE, 0);
	pm2_WR(par, PM2R_SCISSOR_MODE, 0);
}

/*
 *	Hardware cursor support.
 */
static const u8 cursor_bits_lookup[16] = {
	0x00, 0x40, 0x10, 0x50, 0x04, 0x44, 0x14, 0x54,
	0x01, 0x41, 0x11, 0x51, 0x05, 0x45, 0x15, 0x55
};

static int pm2vfb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct pm2fb_par *par = info->par;
	u8 mode = PM2F_CURSORMODE_TYPE_X;
	int x = cursor->image.dx - info->var.xoffset;
	int y = cursor->image.dy - info->var.yoffset;

	if (cursor->enable)
		mode |= PM2F_CURSORMODE_CURSOR_ENABLE;

	pm2v_RDAC_WR(par, PM2VI_RD_CURSOR_MODE, mode);

	if (!cursor->enable)
		x = 2047;	/* push it outside display */
	pm2v_RDAC_WR(par, PM2VI_RD_CURSOR_X_LOW, x & 0xff);
	pm2v_RDAC_WR(par, PM2VI_RD_CURSOR_X_HIGH, (x >> 8) & 0xf);
	pm2v_RDAC_WR(par, PM2VI_RD_CURSOR_Y_LOW, y & 0xff);
	pm2v_RDAC_WR(par, PM2VI_RD_CURSOR_Y_HIGH, (y >> 8) & 0xf);

	/*
	 * If the cursor is not be changed this means either we want the
	 * current cursor state (if enable is set) or we want to query what
	 * we can do with the cursor (if enable is not set)
	 */
	if (!cursor->set)
		return 0;

	if (cursor->set & FB_CUR_SETHOT) {
		pm2v_RDAC_WR(par, PM2VI_RD_CURSOR_X_HOT,
			     cursor->hot.x & 0x3f);
		pm2v_RDAC_WR(par, PM2VI_RD_CURSOR_Y_HOT,
			     cursor->hot.y & 0x3f);
	}

	if (cursor->set & FB_CUR_SETCMAP) {
		u32 fg_idx = cursor->image.fg_color;
		u32 bg_idx = cursor->image.bg_color;
		struct fb_cmap cmap = info->cmap;

		/* the X11 driver says one should use these color registers */
		pm2_WR(par, PM2VR_RD_INDEX_HIGH, PM2VI_RD_CURSOR_PALETTE >> 8);
		pm2v_RDAC_WR(par, PM2VI_RD_CURSOR_PALETTE + 0,
			     cmap.red[bg_idx] >> 8 );
		pm2v_RDAC_WR(par, PM2VI_RD_CURSOR_PALETTE + 1,
			     cmap.green[bg_idx] >> 8 );
		pm2v_RDAC_WR(par, PM2VI_RD_CURSOR_PALETTE + 2,
			     cmap.blue[bg_idx] >> 8 );

		pm2v_RDAC_WR(par, PM2VI_RD_CURSOR_PALETTE + 3,
			     cmap.red[fg_idx] >> 8 );
		pm2v_RDAC_WR(par, PM2VI_RD_CURSOR_PALETTE + 4,
			     cmap.green[fg_idx] >> 8 );
		pm2v_RDAC_WR(par, PM2VI_RD_CURSOR_PALETTE + 5,
			     cmap.blue[fg_idx] >> 8 );
		pm2_WR(par, PM2VR_RD_INDEX_HIGH, 0);
	}

	if (cursor->set & (FB_CUR_SETSHAPE | FB_CUR_SETIMAGE)) {
		u8 *bitmap = (u8 *)cursor->image.data;
		u8 *mask = (u8 *)cursor->mask;
		int i;
		int pos = PM2VI_RD_CURSOR_PATTERN;

		for (i = 0; i < cursor->image.height; i++) {
			int j = (cursor->image.width + 7) >> 3;
			int k = 8 - j;

			pm2_WR(par, PM2VR_RD_INDEX_HIGH, pos >> 8);

			for (; j > 0; j--) {
				u8 data = *bitmap ^ *mask;

				if (cursor->rop == ROP_COPY)
					data = *mask & *bitmap;
				/* Upper 4 bits of bitmap data */
				pm2v_RDAC_WR(par, pos++,
					cursor_bits_lookup[data >> 4] |
					(cursor_bits_lookup[*mask >> 4] << 1));
				/* Lower 4 bits of bitmap */
				pm2v_RDAC_WR(par, pos++,
					cursor_bits_lookup[data & 0xf] |
					(cursor_bits_lookup[*mask & 0xf] << 1));
				bitmap++;
				mask++;
			}
			for (; k > 0; k--) {
				pm2v_RDAC_WR(par, pos++, 0);
				pm2v_RDAC_WR(par, pos++, 0);
			}
		}

		while (pos < (1024 + PM2VI_RD_CURSOR_PATTERN)) {
			pm2_WR(par, PM2VR_RD_INDEX_HIGH, pos >> 8);
			pm2v_RDAC_WR(par, pos++, 0);
		}

		pm2_WR(par, PM2VR_RD_INDEX_HIGH, 0);
	}
	return 0;
}

static int pm2fb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct pm2fb_par *par = info->par;
	u8 mode;

	if (!hwcursor)
		return -EINVAL;	/* just to force soft_cursor() call */

	/* Too large of a cursor or wrong bpp :-( */
	if (cursor->image.width > 64 ||
	    cursor->image.height > 64 ||
	    cursor->image.depth > 1)
		return -EINVAL;

	if (par->type == PM2_TYPE_PERMEDIA2V)
		return pm2vfb_cursor(info, cursor);

	mode = 0x40;
	if (cursor->enable)
		 mode = 0x43;

	pm2_RDAC_WR(par, PM2I_RD_CURSOR_CONTROL, mode);

	/*
	 * If the cursor is not be changed this means either we want the
	 * current cursor state (if enable is set) or we want to query what
	 * we can do with the cursor (if enable is not set)
	 */
	if (!cursor->set)
		return 0;

	if (cursor->set & FB_CUR_SETPOS) {
		int x = cursor->image.dx - info->var.xoffset + 63;
		int y = cursor->image.dy - info->var.yoffset + 63;

		WAIT_FIFO(par, 4);
		pm2_WR(par, PM2R_RD_CURSOR_X_LSB, x & 0xff);
		pm2_WR(par, PM2R_RD_CURSOR_X_MSB, (x >> 8) & 0x7);
		pm2_WR(par, PM2R_RD_CURSOR_Y_LSB, y & 0xff);
		pm2_WR(par, PM2R_RD_CURSOR_Y_MSB, (y >> 8) & 0x7);
	}

	if (cursor->set & FB_CUR_SETCMAP) {
		u32 fg_idx = cursor->image.fg_color;
		u32 bg_idx = cursor->image.bg_color;

		WAIT_FIFO(par, 7);
		pm2_WR(par, PM2R_RD_CURSOR_COLOR_ADDRESS, 1);
		pm2_WR(par, PM2R_RD_CURSOR_COLOR_DATA,
			info->cmap.red[bg_idx] >> 8);
		pm2_WR(par, PM2R_RD_CURSOR_COLOR_DATA,
			info->cmap.green[bg_idx] >> 8);
		pm2_WR(par, PM2R_RD_CURSOR_COLOR_DATA,
			info->cmap.blue[bg_idx] >> 8);

		pm2_WR(par, PM2R_RD_CURSOR_COLOR_DATA,
			info->cmap.red[fg_idx] >> 8);
		pm2_WR(par, PM2R_RD_CURSOR_COLOR_DATA,
			info->cmap.green[fg_idx] >> 8);
		pm2_WR(par, PM2R_RD_CURSOR_COLOR_DATA,
			info->cmap.blue[fg_idx] >> 8);
	}

	if (cursor->set & (FB_CUR_SETSHAPE | FB_CUR_SETIMAGE)) {
		u8 *bitmap = (u8 *)cursor->image.data;
		u8 *mask = (u8 *)cursor->mask;
		int i;

		WAIT_FIFO(par, 1);
		pm2_WR(par, PM2R_RD_PALETTE_WRITE_ADDRESS, 0);

		for (i = 0; i < cursor->image.height; i++) {
			int j = (cursor->image.width + 7) >> 3;
			int k = 8 - j;

			WAIT_FIFO(par, 8);
			for (; j > 0; j--) {
				u8 data = *bitmap ^ *mask;

				if (cursor->rop == ROP_COPY)
					data = *mask & *bitmap;
				/* bitmap data */
				pm2_WR(par, PM2R_RD_CURSOR_DATA, data);
				bitmap++;
				mask++;
			}
			for (; k > 0; k--)
				pm2_WR(par, PM2R_RD_CURSOR_DATA, 0);
		}
		for (; i < 64; i++) {
			int j = 8;
			WAIT_FIFO(par, 8);
			while (j-- > 0)
				pm2_WR(par, PM2R_RD_CURSOR_DATA, 0);
		}

		mask = (u8 *)cursor->mask;
		for (i = 0; i < cursor->image.height; i++) {
			int j = (cursor->image.width + 7) >> 3;
			int k = 8 - j;

			WAIT_FIFO(par, 8);
			for (; j > 0; j--) {
				/* mask */
				pm2_WR(par, PM2R_RD_CURSOR_DATA, *mask);
				mask++;
			}
			for (; k > 0; k--)
				pm2_WR(par, PM2R_RD_CURSOR_DATA, 0);
		}
		for (; i < 64; i++) {
			int j = 8;
			WAIT_FIFO(par, 8);
			while (j-- > 0)
				pm2_WR(par, PM2R_RD_CURSOR_DATA, 0);
		}
	}
	return 0;
}

/* ------------ Hardware Independent Functions ------------ */

/*
 *  Frame buffer operations
 */

static const struct fb_ops pm2fb_ops = {
	.owner		= THIS_MODULE,
	__FB_DEFAULT_IOMEM_OPS_RDWR,
	.fb_check_var	= pm2fb_check_var,
	.fb_set_par	= pm2fb_set_par,
	.fb_setcolreg	= pm2fb_setcolreg,
	.fb_blank	= pm2fb_blank,
	.fb_pan_display	= pm2fb_pan_display,
	.fb_fillrect	= pm2fb_fillrect,
	.fb_copyarea	= pm2fb_copyarea,
	.fb_imageblit	= pm2fb_imageblit,
	.fb_sync	= pm2fb_sync,
	.fb_cursor	= pm2fb_cursor,
	__FB_DEFAULT_IOMEM_OPS_MMAP,
};

/*
 * PCI stuff
 */


/**
 * pm2fb_probe - Initialise and allocate resource for PCI device.
 *
 * @pdev:	PCI device.
 * @id:		PCI device ID.
 */
static int pm2fb_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct pm2fb_par *default_par;
	struct fb_info *info;
	int err;
	int retval = -ENXIO;

	err = aperture_remove_conflicting_pci_devices(pdev, "pm2fb");
	if (err)
		return err;

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_WARNING "pm2fb: Can't enable pdev: %d\n", err);
		return err;
	}

	info = framebuffer_alloc(sizeof(struct pm2fb_par), &pdev->dev);
	if (!info) {
		err = -ENOMEM;
		goto err_exit_disable;
	}
	default_par = info->par;

	switch (pdev->device) {
	case  PCI_DEVICE_ID_TI_TVP4020:
		strcpy(pm2fb_fix.id, "TVP4020");
		default_par->type = PM2_TYPE_PERMEDIA2;
		break;
	case  PCI_DEVICE_ID_3DLABS_PERMEDIA2:
		strcpy(pm2fb_fix.id, "Permedia2");
		default_par->type = PM2_TYPE_PERMEDIA2;
		break;
	case  PCI_DEVICE_ID_3DLABS_PERMEDIA2V:
		strcpy(pm2fb_fix.id, "Permedia2v");
		default_par->type = PM2_TYPE_PERMEDIA2V;
		break;
	}

	pm2fb_fix.mmio_start = pci_resource_start(pdev, 0);
	pm2fb_fix.mmio_len = PM2_REGS_SIZE;

#if defined(__BIG_ENDIAN)
	/*
	 * PM2 has a 64k register file, mapped twice in 128k. Lower
	 * map is little-endian, upper map is big-endian.
	 */
	pm2fb_fix.mmio_start += PM2_REGS_SIZE;
	DPRINTK("Adjusting register base for big-endian.\n");
#endif
	DPRINTK("Register base at 0x%lx\n", pm2fb_fix.mmio_start);

	/* Registers - request region and map it. */
	if (!request_mem_region(pm2fb_fix.mmio_start, pm2fb_fix.mmio_len,
				"pm2fb regbase")) {
		printk(KERN_WARNING "pm2fb: Can't reserve regbase.\n");
		goto err_exit_neither;
	}
	default_par->v_regs =
		ioremap(pm2fb_fix.mmio_start, pm2fb_fix.mmio_len);
	if (!default_par->v_regs) {
		printk(KERN_WARNING "pm2fb: Can't remap %s register area.\n",
		       pm2fb_fix.id);
		release_mem_region(pm2fb_fix.mmio_start, pm2fb_fix.mmio_len);
		goto err_exit_neither;
	}

	/* Stash away memory register info for use when we reset the board */
	default_par->mem_control = pm2_RD(default_par, PM2R_MEM_CONTROL);
	default_par->boot_address = pm2_RD(default_par, PM2R_BOOT_ADDRESS);
	default_par->mem_config = pm2_RD(default_par, PM2R_MEM_CONFIG);
	DPRINTK("MemControl 0x%x BootAddress 0x%x MemConfig 0x%x\n",
		default_par->mem_control, default_par->boot_address,
		default_par->mem_config);

	if (default_par->mem_control == 0 &&
		default_par->boot_address == 0x31 &&
		default_par->mem_config == 0x259fffff) {
		default_par->memclock = CVPPC_MEMCLOCK;
		default_par->mem_control = 0;
		default_par->boot_address = 0x20;
		default_par->mem_config = 0xe6002021;
		if (pdev->subsystem_vendor == 0x1048 &&
			pdev->subsystem_device == 0x0a31) {
			DPRINTK("subsystem_vendor: %04x, "
				"subsystem_device: %04x\n",
				pdev->subsystem_vendor, pdev->subsystem_device);
			DPRINTK("We have not been initialized by VGA BIOS and "
				"are running on an Elsa Winner 2000 Office\n");
			DPRINTK("Initializing card timings manually...\n");
			default_par->memclock = 100000;
		}
		if (pdev->subsystem_vendor == 0x3d3d &&
			pdev->subsystem_device == 0x0100) {
			DPRINTK("subsystem_vendor: %04x, "
				"subsystem_device: %04x\n",
				pdev->subsystem_vendor, pdev->subsystem_device);
			DPRINTK("We have not been initialized by VGA BIOS and "
				"are running on an 3dlabs reference board\n");
			DPRINTK("Initializing card timings manually...\n");
			default_par->memclock = 74894;
		}
	}

	/* Now work out how big lfb is going to be. */
	switch (default_par->mem_config & PM2F_MEM_CONFIG_RAM_MASK) {
	case PM2F_MEM_BANKS_1:
		pm2fb_fix.smem_len = 0x200000;
		break;
	case PM2F_MEM_BANKS_2:
		pm2fb_fix.smem_len = 0x400000;
		break;
	case PM2F_MEM_BANKS_3:
		pm2fb_fix.smem_len = 0x600000;
		break;
	case PM2F_MEM_BANKS_4:
		pm2fb_fix.smem_len = 0x800000;
		break;
	}
	pm2fb_fix.smem_start = pci_resource_start(pdev, 1);

	/* Linear frame buffer - request region and map it. */
	if (!request_mem_region(pm2fb_fix.smem_start, pm2fb_fix.smem_len,
				"pm2fb smem")) {
		printk(KERN_WARNING "pm2fb: Can't reserve smem.\n");
		goto err_exit_mmio;
	}
	info->screen_base =
		ioremap_wc(pm2fb_fix.smem_start, pm2fb_fix.smem_len);
	if (!info->screen_base) {
		printk(KERN_WARNING "pm2fb: Can't ioremap smem area.\n");
		release_mem_region(pm2fb_fix.smem_start, pm2fb_fix.smem_len);
		goto err_exit_mmio;
	}

	if (!nomtrr)
		default_par->wc_cookie = arch_phys_wc_add(pm2fb_fix.smem_start,
							  pm2fb_fix.smem_len);

	info->fbops		= &pm2fb_ops;
	info->fix		= pm2fb_fix;
	info->pseudo_palette	= default_par->palette;
	info->flags		= FBINFO_HWACCEL_YPAN |
				  FBINFO_HWACCEL_COPYAREA |
				  FBINFO_HWACCEL_IMAGEBLIT |
				  FBINFO_HWACCEL_FILLRECT;

	info->pixmap.addr = kmalloc(PM2_PIXMAP_SIZE, GFP_KERNEL);
	if (!info->pixmap.addr) {
		retval = -ENOMEM;
		goto err_exit_pixmap;
	}
	info->pixmap.size = PM2_PIXMAP_SIZE;
	info->pixmap.buf_align = 4;
	info->pixmap.scan_align = 4;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;

	if (noaccel) {
		printk(KERN_DEBUG "disabling acceleration\n");
		info->flags |= FBINFO_HWACCEL_DISABLED;
		info->pixmap.scan_align = 1;
	}

	if (!mode_option)
		mode_option = "640x480@60";

	err = fb_find_mode(&info->var, info, mode_option, NULL, 0, NULL, 8);
	if (!err || err == 4)
		info->var = pm2fb_var;

	retval = fb_alloc_cmap(&info->cmap, 256, 0);
	if (retval < 0)
		goto err_exit_both;

	retval = register_framebuffer(info);
	if (retval < 0)
		goto err_exit_all;

	fb_info(info, "%s frame buffer device, memory = %dK\n",
		info->fix.id, pm2fb_fix.smem_len / 1024);

	/*
	 * Our driver data
	 */
	pci_set_drvdata(pdev, info);

	return 0;

 err_exit_all:
	fb_dealloc_cmap(&info->cmap);
 err_exit_both:
	kfree(info->pixmap.addr);
 err_exit_pixmap:
	iounmap(info->screen_base);
	release_mem_region(pm2fb_fix.smem_start, pm2fb_fix.smem_len);
 err_exit_mmio:
	iounmap(default_par->v_regs);
	release_mem_region(pm2fb_fix.mmio_start, pm2fb_fix.mmio_len);
 err_exit_neither:
	framebuffer_release(info);
 err_exit_disable:
	pci_disable_device(pdev);
	return retval;
}

/**
 * pm2fb_remove - Release all device resources.
 *
 * @pdev:	PCI device to clean up.
 */
static void pm2fb_remove(struct pci_dev *pdev)
{
	struct fb_info *info = pci_get_drvdata(pdev);
	struct fb_fix_screeninfo *fix = &info->fix;
	struct pm2fb_par *par = info->par;

	unregister_framebuffer(info);
	arch_phys_wc_del(par->wc_cookie);
	iounmap(info->screen_base);
	release_mem_region(fix->smem_start, fix->smem_len);
	iounmap(par->v_regs);
	release_mem_region(fix->mmio_start, fix->mmio_len);

	fb_dealloc_cmap(&info->cmap);
	kfree(info->pixmap.addr);
	framebuffer_release(info);
	pci_disable_device(pdev);
}

static const struct pci_device_id pm2fb_id_table[] = {
	{ PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_TVP4020,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_3DLABS, PCI_DEVICE_ID_3DLABS_PERMEDIA2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_3DLABS, PCI_DEVICE_ID_3DLABS_PERMEDIA2V,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, }
};

static struct pci_driver pm2fb_driver = {
	.name		= "pm2fb",
	.id_table	= pm2fb_id_table,
	.probe		= pm2fb_probe,
	.remove		= pm2fb_remove,
};

MODULE_DEVICE_TABLE(pci, pm2fb_id_table);


#ifndef MODULE
/*
 * Parse user specified options.
 *
 * This is, comma-separated options following `video=pm2fb:'.
 */
static int __init pm2fb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		if (!strcmp(this_opt, "lowhsync"))
			lowhsync = 1;
		else if (!strcmp(this_opt, "lowvsync"))
			lowvsync = 1;
		else if (!strncmp(this_opt, "hwcursor=", 9))
			hwcursor = simple_strtoul(this_opt + 9, NULL, 0);
		else if (!strncmp(this_opt, "nomtrr", 6))
			nomtrr = 1;
		else if (!strncmp(this_opt, "noaccel", 7))
			noaccel = 1;
		else
			mode_option = this_opt;
	}
	return 0;
}
#endif


static int __init pm2fb_init(void)
{
#ifndef MODULE
	char *option = NULL;
#endif

	if (fb_modesetting_disabled("pm2fb"))
		return -ENODEV;

#ifndef MODULE
	if (fb_get_options("pm2fb", &option))
		return -ENODEV;
	pm2fb_setup(option);
#endif

	return pci_register_driver(&pm2fb_driver);
}

module_init(pm2fb_init);

#ifdef MODULE
/*
 *  Cleanup
 */

static void __exit pm2fb_exit(void)
{
	pci_unregister_driver(&pm2fb_driver);
}
#endif

#ifdef MODULE
module_exit(pm2fb_exit);

module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option, "Initial video mode e.g. '648x480-8@60'");
module_param_named(mode, mode_option, charp, 0);
MODULE_PARM_DESC(mode, "Initial video mode e.g. '648x480-8@60' (deprecated)");
module_param(lowhsync, bool, 0);
MODULE_PARM_DESC(lowhsync, "Force horizontal sync low regardless of mode");
module_param(lowvsync, bool, 0);
MODULE_PARM_DESC(lowvsync, "Force vertical sync low regardless of mode");
module_param(noaccel, bool, 0);
MODULE_PARM_DESC(noaccel, "Disable acceleration");
module_param(hwcursor, int, 0644);
MODULE_PARM_DESC(hwcursor, "Enable hardware cursor "
			"(1=enable, 0=disable, default=1)");
module_param(nomtrr, bool, 0);
MODULE_PARM_DESC(nomtrr, "Disable MTRR support (0 or 1=disabled) (default=0)");

MODULE_AUTHOR("Jim Hague <jim.hague@acm.org>");
MODULE_DESCRIPTION("Permedia2 framebuffer device driver");
MODULE_LICENSE("GPL");
#endif
