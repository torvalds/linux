/* linux/drivers/video/s3c-fb.c
 *
 * Copyright 2008 Openmoko Inc.
 * Copyright 2008-2010 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * Samsung SoC Framebuffer driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/kthread.h>

#if defined(CONFIG_FB_EXYNOS_FIMD_MC) || defined(CONFIG_FB_EXYNOS_FIMD_MC_WB)
#include <media/v4l2-subdev.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/exynos_mc.h>
#include <plat/map-base.h>
#endif

#include <mach/exynos5_bus.h>
#include <mach/map.h>
#include <mach/bts.h>
#include <plat/regs-fb-v4.h>
#include <plat/fb.h>
#include <plat/cpu.h>

#ifdef CONFIG_ION_EXYNOS
#include <linux/dma-buf.h>
#include <linux/exynos_ion.h>
#include <linux/ion.h>
#include <linux/highmem.h>
#include <linux/memblock.h>
#include <linux/sw_sync.h>
#include <plat/devs.h>
#include <plat/iovmm.h>
#include <plat/sysmmu.h>
#include <mach/sysmmu.h>
#endif

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
#include <linux/pm_qos.h>
static struct pm_qos_request exynos5_mif_qos;
static struct pm_qos_request exynos5_int_qos;
#endif


/* This driver will export a number of framebuffer interfaces depending
 * on the configuration passed in via the platform data. Each fb instance
 * maps to a hardware window. Currently there is no support for runtime
 * setting of the alpha-blending functions that each window has, so only
 * window 0 is actually useful.
 *
 * Window 0 is treated specially, it is used for the basis of the LCD
 * output timings and as the control for the output power-down state.
*/

/* note, the previous use of <mach/regs-fb.h> to get platform specific data
 * has been replaced by using the platform device name to pick the correct
 * configuration data for the system.
*/

#ifdef CONFIG_FB_S3C_DEBUG_REGWRITE
#undef writel
#define writel(v, r) do { \
	printk(KERN_DEBUG "%s: %08x => %p\n", __func__, (unsigned int)v, r); \
	__raw_writel(v, r); \
} while (0)
#endif /* FB_S3C_DEBUG_REGWRITE */

#define VSYNC_TIMEOUT_MSEC 50

#define MAX_BW_PER_WINDOW	(2560 * 1600 * 4 * 60)
#define FHD_MAX_BW_PER_WINDOW	(1080 * 1920 * 4 * 60)

struct s3c_fb;

#ifdef CONFIG_ION_EXYNOS
extern struct ion_device *ion_exynos;
#endif

#ifdef CONFIG_FB_EXYNOS_FIMD_MC
#define SYSREG_MIXER0_VALID	(1 << 7)
#define SYSREG_MIXER1_VALID	(1 << 4)
#define FIMD_PAD_SINK_FROM_GSCALER_SRC		0
#define FIMD_PADS_NUM				1

/* SYSREG for local path between Gscaler and Mixer */
#define SYSREG_DISP1BLK_CFG	(S3C_VA_SYS + 0x0214)
#endif

#ifdef CONFIG_FB_EXYNOS_FIMD_MC_WB
#define SYSREG_DISP1WB_DEST(_x)			((_x) << 10)
#define SYSREG_DISP1WB_DEST_MASK		(0x3 << 10)
#define FIMD_WB_PAD_SRC_TO_GSCALER_SINK		0
#define FIMD_WB_PADS_NUM			1

/* SYSREG for local path between Gscaler and Mixer */
#define SYSREG_GSCLBLK_CFG	(S3C_VA_SYS + 0x0224)
#endif

#define VALID_BPP(x) (1 << ((x) - 1))

#define OSD_BASE(win, variant) ((variant).osd + ((win) * (variant).osd_stride))
#define VIDOSD_A(win, variant) (OSD_BASE(win, variant) + 0x00)
#define VIDOSD_B(win, variant) (OSD_BASE(win, variant) + 0x04)
#define VIDOSD_C(win, variant) (OSD_BASE(win, variant) + 0x08)
#define VIDOSD_D(win, variant) (OSD_BASE(win, variant) + 0x0C)

enum {
	POWER_ON,
	POWER_DOWN,
};

/**
 * struct s3c_fb_variant - fb variant information
 * @is_2443: Set if S3C2443/S3C2416 style hardware.
 * @nr_windows: The number of windows.
 * @vidtcon: The base for the VIDTCONx registers
 * @wincon: The base for the WINxCON registers.
 * @winmap: The base for the WINxMAP registers.
 * @keycon: The abse for the WxKEYCON registers.
 * @buf_start: Offset of buffer start registers.
 * @buf_size: Offset of buffer size registers.
 * @buf_end: Offset of buffer end registers.
 * @osd: The base for the OSD registers.
 * @palette: Address of palette memory, or 0 if none.
 * @has_prtcon: Set if has PRTCON register.
 * @has_shadowcon: Set if has SHADOWCON register.
 * @has_blendcon: Set if has BLENDCON register.
 * @has_alphacon: Set if has VIDWALPHA register.
 * @has_clksel: Set if VIDCON0 register has CLKSEL bit.
 * @has_fixvclk: Set if VIDCON1 register has FIXVCLK bits.
 */
struct s3c_fb_variant {
	unsigned int	is_2443:1;
	unsigned short	nr_windows;
	unsigned int	vidtcon;
	unsigned short	wincon;
	unsigned short	winmap;
	unsigned short	keycon;
	unsigned short	buf_start;
	unsigned short	buf_end;
	unsigned short	buf_size;
	unsigned short	osd;
	unsigned short	osd_stride;
	unsigned short	palette[S3C_FB_MAX_WIN];

	unsigned int	has_prtcon:1;
	unsigned int	has_shadowcon:1;
	unsigned int	has_blendcon:1;
	unsigned int	has_alphacon:1;
	unsigned int	has_clksel:1;
	unsigned int	has_fixvclk:1;
};

/**
 * struct s3c_fb_win_variant
 * @has_osd_c: Set if has OSD C register.
 * @has_osd_d: Set if has OSD D register.
 * @has_osd_alpha: Set if can change alpha transparency for a window.
 * @palette_sz: Size of palette in entries.
 * @palette_16bpp: Set if palette is 16bits wide.
 * @osd_size_off: If != 0, supports setting up OSD for a window; the appropriate
 *                register is located at the given offset from OSD_BASE.
 * @valid_bpp: 1 bit per BPP setting to show valid bits-per-pixel.
 *
 * valid_bpp bit x is set if (x+1)BPP is supported.
 */
struct s3c_fb_win_variant {
	unsigned int	has_osd_c:1;
	unsigned int	has_osd_d:1;
	unsigned int	has_osd_alpha:1;
	unsigned int	palette_16bpp:1;
	unsigned short	osd_size_off;
	unsigned short	palette_sz;
	u32		valid_bpp;
};

/**
 * struct s3c_fb_driverdata - per-device type driver data for init time.
 * @variant: The variant information for this driver.
 * @win: The window information for each window.
 */
struct s3c_fb_driverdata {
	struct s3c_fb_variant	variant;
	struct s3c_fb_win_variant *win[S3C_FB_MAX_WIN];
};

/**
 * struct s3c_fb_palette - palette information
 * @r: Red bitfield.
 * @g: Green bitfield.
 * @b: Blue bitfield.
 * @a: Alpha bitfield.
 */
struct s3c_fb_palette {
	struct fb_bitfield	r;
	struct fb_bitfield	g;
	struct fb_bitfield	b;
	struct fb_bitfield	a;
};

#ifdef CONFIG_ION_EXYNOS
struct s3c_dma_buf_data {
	struct ion_handle *ion_handle;
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attachment;
	struct sg_table *sg_table;
	dma_addr_t dma_addr;
	struct sync_fence *fence;
};

struct s3c_reg_data {
	struct list_head	list;
	u32			shadowcon;
	u32			wincon[S3C_FB_MAX_WIN];
	u32			win_rgborder[S3C_FB_MAX_WIN];
	u32			winmap[S3C_FB_MAX_WIN];
	u32			vidosd_a[S3C_FB_MAX_WIN];
	u32			vidosd_b[S3C_FB_MAX_WIN];
	u32			vidosd_c[S3C_FB_MAX_WIN];
	u32			vidosd_d[S3C_FB_MAX_WIN];
	u32			vidw_alpha0[S3C_FB_MAX_WIN];
	u32			vidw_alpha1[S3C_FB_MAX_WIN];
	u32			blendeq[S3C_FB_MAX_WIN - 1];
	u32			vidw_buf_start[S3C_FB_MAX_WIN];
	u32			vidw_buf_end[S3C_FB_MAX_WIN];
	u32			vidw_buf_size[S3C_FB_MAX_WIN];
	struct s3c_dma_buf_data	dma_buf_data[S3C_FB_MAX_WIN];
	unsigned int		bandwidth;
};
#endif

/**
 * struct s3c_fb_win - per window private data for each framebuffer.
 * @windata: The platform data supplied for the window configuration.
 * @parent: The hardware that this window is part of.
 * @fbinfo: Pointer pack to the framebuffer info for this window.
 * @varint: The variant information for this window.
 * @palette_buffer: Buffer/cache to hold palette entries.
 * @pseudo_palette: For use in TRUECOLOUR modes for entries 0..15/
 * @index: The window number of this window.
 * @palette: The bitfields for changing r/g/b into a hardware palette entry.
 */
struct s3c_fb_win {
	struct s3c_fb_pd_win	*windata;
	struct s3c_fb		*parent;
	struct fb_info		*fbinfo;
	struct s3c_fb_palette	 palette;
	struct s3c_fb_win_variant variant;

	u32			*palette_buffer;
	u32			 pseudo_palette[16];
	unsigned int		 index;
#ifdef CONFIG_ION_EXYNOS
	struct s3c_dma_buf_data	dma_buf_data;
	struct fb_var_screeninfo prev_var;
	struct fb_fix_screeninfo prev_fix;
#endif

	int			fps;

#ifdef CONFIG_FB_EXYNOS_FIMD_MC
	int use;		/* use of widnow subdev in fimd */
	struct media_pad pads[FIMD_PADS_NUM];	/* window's pad : 1 sink */
	struct v4l2_subdev sd;		/* Take a window as a v4l2_subdevice */
#endif
	int local;		/* use of local path gscaler to window in fimd */
};

/**
 * struct s3c_fb_vsync - vsync information
 * @wait:		a queue for processes waiting for vsync
 * @timestamp:		the time of the last vsync interrupt
 * @active:		whether userspace is requesting vsync notifications
 * @irq_refcount:	reference count for the underlying irq
 * @irq_lock:		mutex protecting the irq refcount and register
 * @thread:		notification-generating thread
 */
struct s3c_fb_vsync {
	wait_queue_head_t	wait;
	ktime_t			timestamp;
	bool			active;
	int			irq_refcount;
	struct mutex		irq_lock;
	struct task_struct	*thread;
};

#ifdef CONFIG_DEBUG_FS
#define S3C_FB_DEBUG_FIFO_TIMESTAMPS 32
#define S3C_FB_DEBUG_REGS_SIZE 0x0280

struct s3c_fb_debug {
	ktime_t		fifo_timestamps[S3C_FB_DEBUG_FIFO_TIMESTAMPS];
	unsigned int	num_timestamps;
	unsigned int	first_timestamp;
	u8		regs_at_underflow[S3C_FB_DEBUG_REGS_SIZE];
};
#endif

/**
 * struct s3c_fb - overall hardware state of the hardware
 * @slock: The spinlock protection for this data sturcture.
 * @dev: The device that we bound to, for printing, etc.
 * @bus_clk: The clk (hclk) feeding our interface and possibly pixclk.
 * @lcd_clk: The clk (sclk) feeding pixclk.
 * @regs: The mapped hardware registers.
 * @variant: Variant information for this hardware.
 * @enabled: A bitmask of enabled hardware windows.
 * @output_on: Flag if the physical output is enabled.
 * @pdata: The platform configuration data passed with the device.
 * @windows: The hardware windows that have been claimed.
 * @irq_no: IRQ line number
 * @vsync_info: VSYNC-related information (count, queues...)
 */
struct s3c_fb {
	spinlock_t		slock;
	struct device		*dev;
	struct clk		*bus_clk;
	struct clk		*lcd_clk;
	struct clk		*axi_disp1;
	void __iomem		*regs;
	struct s3c_fb_variant	 variant;

	bool			output_on;
	struct mutex		output_lock;

	struct s3c_fb_platdata	*pdata;
	struct s3c_fb_win	*windows[S3C_FB_MAX_WIN];

	int			 irq_no;
	struct s3c_fb_vsync	 vsync_info;
	bool			power_state;

#ifdef CONFIG_ION_EXYNOS
	struct ion_client	*fb_ion_client;

	struct list_head	update_regs_list;
	struct mutex		update_regs_list_lock;
	struct kthread_worker	update_regs_worker;
	struct task_struct	*update_regs_thread;
	struct kthread_work	update_regs_work;

	struct sw_sync_timeline *timeline;
	int			timeline_max;
#endif

#ifdef CONFIG_FB_EXYNOS_FIMD_MC
	struct exynos_md *md;
#endif
#ifdef CONFIG_FB_EXYNOS_FIMD_MC_WB
	struct exynos_md *md_wb;
	int use_wb;	/* use of fimd subdev for writeback */
	int local_wb;	/* use of writeback path to gscaler in fimd */
	struct media_pad pads_wb;	/* FIMD1's pad */
	struct v4l2_subdev sd_wb;	/* Take a FIMD1 as a v4l2_subdevice */
#endif

#ifdef CONFIG_DEBUG_FS
	struct dentry		*debug_dentry;
	struct s3c_fb_debug	debug_data;
#endif
	struct exynos5_bus_mif_handle *fb_mif_handle;
	struct exynos5_bus_int_handle *fb_int_handle;

};

#if defined(CONFIG_MACH_ODROIDXU)

static  unsigned char   VoutBootArgs[5];
static  bool            DP_Mode = false;

static int __init vout_mode(char *line)
{
    sprintf(VoutBootArgs, "%s", line);
    return  0;
}
__setup("vout=", vout_mode);

#endif

static void s3c_fb_dump_registers(struct s3c_fb *sfb)
{
#ifdef CONFIG_FB_EXYNOS_FIMD_V8
	pr_err("dumping registers\n");
	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS, 32, 4, sfb->regs,
			0x0280, false);
	pr_err("...\n");
	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS, 32, 4,
			sfb->regs + SHD_VIDW_BUF_START(0), 0x74, false);
	pr_err("...\n");
	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS, 32, 4,
			sfb->regs + 0x20000, 0x20, false);
#endif
}

static bool s3c_fb_validate_x_alignment(struct s3c_fb *sfb, int x, u32 w,
		u32 bits_per_pixel)
{
	uint8_t pixel_alignment = 32 / bits_per_pixel;

	if (x % pixel_alignment) {
		dev_err(sfb->dev, "left X coordinate not properly aligned to %u-pixel boundary (bpp = %u, x = %u)\n",
				pixel_alignment, bits_per_pixel, x);
		return 0;
	}
	if ((x + w) % pixel_alignment) {
		dev_err(sfb->dev, "right X coordinate not properly aligned to %u-pixel boundary (bpp = %u, x = %u, w = %u)\n",
				pixel_alignment, bits_per_pixel, x, w);
		return 0;
	}

	return 1;
}

/**
 * s3c_fb_validate_win_bpp - validate the bits-per-pixel for this mode.
 * @win: The device window.
 * @bpp: The bit depth.
 */
static bool s3c_fb_validate_win_bpp(struct s3c_fb_win *win, unsigned int bpp)
{
	return win->variant.valid_bpp & VALID_BPP(bpp);
}

/**
 * s3c_fb_check_var() - framebuffer layer request to verify a given mode.
 * @var: The screen information to verify.
 * @info: The framebuffer device.
 *
 * Framebuffer layer call to verify the given information and allow us to
 * update various information depending on the hardware capabilities.
 */
static int s3c_fb_check_var(struct fb_var_screeninfo *var,
			    struct fb_info *info)
{
	struct s3c_fb_win *win = info->par;
	struct s3c_fb *sfb = win->parent;
	int x, y;
	unsigned long long hz;

	dev_dbg(sfb->dev, "checking parameters\n");

	var->xres_virtual = max(var->xres_virtual, var->xres);
	var->yres_virtual = max(var->yres_virtual, var->yres);

	if (!s3c_fb_validate_win_bpp(win, var->bits_per_pixel)) {
		dev_dbg(sfb->dev, "win %d: unsupported bpp %d\n",
			win->index, var->bits_per_pixel);
		return -EINVAL;
	}

	if (!s3c_fb_validate_x_alignment(sfb, 0, var->xres,
			var->bits_per_pixel))
		return -EINVAL;

	/* always ensure these are zero, for drop through cases below */
	var->transp.offset = 0;
	var->transp.length = 0;

	switch (var->bits_per_pixel) {
	case 1:
	case 2:
	case 4:
	case 8:
		if (sfb->variant.palette[win->index] != 0) {
			/* non palletised, A:1,R:2,G:3,B:2 mode */
			var->red.offset		= 4;
			var->green.offset	= 2;
			var->blue.offset	= 0;
			var->red.length		= 5;
			var->green.length	= 3;
			var->blue.length	= 2;
			var->transp.offset	= 7;
			var->transp.length	= 1;
		} else {
			var->red.offset	= 0;
			var->red.length	= var->bits_per_pixel;
			var->green	= var->red;
			var->blue	= var->red;
		}
		break;

	case 19:
		/* 666 with one bit alpha/transparency */
		var->transp.offset	= 18;
		var->transp.length	= 1;
	case 18:
		var->bits_per_pixel	= 32;

		/* 666 format */
		var->red.offset		= 12;
		var->green.offset	= 6;
		var->blue.offset	= 0;
		var->red.length		= 6;
		var->green.length	= 6;
		var->blue.length	= 6;
		break;

	case 16:
		/* 16 bpp, 565 format */
		var->red.offset		= 11;
		var->green.offset	= 5;
		var->blue.offset	= 0;
		var->red.length		= 5;
		var->green.length	= 6;
		var->blue.length	= 5;
		break;

	case 32:
	case 28:
	case 25:
		var->transp.length	= var->bits_per_pixel - 24;
		var->transp.offset	= 24;
		/* drop through */
	case 24:
		/* our 24bpp is unpacked, so 32bpp */
		var->bits_per_pixel	= 32;
		var->red.offset		= 16;
		var->red.length		= 8;
		var->green.offset	= 8;
		var->green.length	= 8;
		var->blue.offset	= 0;
		var->blue.length	= 8;
		break;

	default:
		dev_err(sfb->dev, "invalid bpp\n");
	}

	x = var->xres + var->left_margin + var->right_margin + var->hsync_len;
	y = var->yres + var->upper_margin + var->lower_margin + var->vsync_len;

	hz = 1000000000000ULL;		/* 1e12 picoseconds per second */

	hz += (x * y) / 2;
	do_div(hz, x * y);		/* divide by x * y with rounding */

	hz += var->pixclock / 2;
	do_div(hz, var->pixclock);	/* divide by pixclock with rounding */

	win->fps = hz;

	dev_dbg(sfb->dev, "%s: verified parameters\n", __func__);
	return 0;
}

/**
 * s3c_fb_calc_pixclk() - calculate the divider to create the pixel clock.
 * @sfb: The hardware state.
 * @pixclock: The pixel clock wanted, in picoseconds.
 *
 * Given the specified pixel clock, work out the necessary divider to get
 * close to the output frequency.
 */
static int s3c_fb_calc_pixclk(struct s3c_fb *sfb, unsigned int pixclk)
{
	unsigned long clk;
	unsigned long long tmp;
	unsigned int result;

	if (sfb->variant.has_clksel)
		clk = clk_get_rate(sfb->bus_clk);
	else
		clk = clk_get_rate(sfb->lcd_clk);

	tmp = (unsigned long long)clk;
	tmp *= pixclk;

	do_div(tmp, 1000000000UL);
	result = (unsigned int)tmp / 1000;

	return result;
}

/**
 * vidosd_set_size() - set OSD size for a window
 *
 * @win: the window to set OSD size for
 * @size: OSD size register value
 */
static void vidosd_set_size(struct s3c_fb_win *win, u32 size)
{
	struct s3c_fb *sfb = win->parent;

	/* OSD can be set up if osd_size_off != 0 for this window */
	if (win->variant.osd_size_off)
		writel(size, sfb->regs + OSD_BASE(win->index, sfb->variant)
				+ win->variant.osd_size_off);
}

/**
 * shadow_protect_win() - disable updating values from shadow registers at vsync
 *
 * @win: window to protect registers for
 * @protect: 1 to protect (disable updates)
 */
static void shadow_protect_win(struct s3c_fb_win *win, bool protect)
{
	struct s3c_fb *sfb = win->parent;
	u32 reg;

	if (protect) {
		if (sfb->variant.has_prtcon) {
			writel(PRTCON_PROTECT, sfb->regs + PRTCON);
		} else if (sfb->variant.has_shadowcon) {
			reg = readl(sfb->regs + SHADOWCON);
			writel(reg | SHADOWCON_WINx_PROTECT(win->index),
				sfb->regs + SHADOWCON);
		}
	} else {
		if (sfb->variant.has_prtcon) {
			writel(0, sfb->regs + PRTCON);
		} else if (sfb->variant.has_shadowcon) {
			reg = readl(sfb->regs + SHADOWCON);
			writel(reg & ~SHADOWCON_WINx_PROTECT(win->index),
				sfb->regs + SHADOWCON);
		}
	}
}

static inline u32 fb_visual(u32 bits_per_pixel, unsigned short palette_sz)
{
	switch (bits_per_pixel) {
	case 32:
	case 24:
	case 16:
	case 12:
		return FB_VISUAL_TRUECOLOR;
	case 8:
		if (palette_sz >= 256)
			return FB_VISUAL_PSEUDOCOLOR;
		else
			return FB_VISUAL_TRUECOLOR;
	case 1:
		return FB_VISUAL_MONO01;
	default:
		return FB_VISUAL_PSEUDOCOLOR;
	}
}

static inline u32 fb_linelength(u32 xres_virtual, u32 bits_per_pixel)
{
	return (xres_virtual * bits_per_pixel) / 8;
}

static inline u16 fb_panstep(u32 res, u32 res_virtual)
{
	return res_virtual > res ? 1 : 0;
}

static inline u32 vidw_buf_size(u32 xres, u32 line_length, u32 bits_per_pixel)
{
	u32 pagewidth = (xres * bits_per_pixel) >> 3;
	return VIDW_BUF_SIZE_OFFSET(line_length - pagewidth) |
	       VIDW_BUF_SIZE_PAGEWIDTH(pagewidth) |
	       VIDW_BUF_SIZE_OFFSET_E(line_length - pagewidth) |
	       VIDW_BUF_SIZE_PAGEWIDTH_E(pagewidth);
}

static inline u32 vidosd_a(int x, int y)
{
	return VIDOSDxA_TOPLEFT_X(x) |
			VIDOSDxA_TOPLEFT_Y(y) |
			VIDOSDxA_TOPLEFT_X_E(x) |
			VIDOSDxA_TOPLEFT_Y_E(y);
}

static inline u32 vidosd_b(int x, int y, u32 xres, u32 yres)
{
	return VIDOSDxB_BOTRIGHT_X(x + xres - 1) |
		VIDOSDxB_BOTRIGHT_Y(y + yres - 1) |
		VIDOSDxB_BOTRIGHT_X_E(x + xres - 1) |
		VIDOSDxB_BOTRIGHT_Y_E(y + yres - 1);
}

static inline u32 vidosd_c(u8 r0, u8 g0, u8 b0, u8 r1, u8 g1, u8 b1)
{
	return VIDOSDxC_ALPHA0_R_H(r0) |
		VIDOSDxC_ALPHA0_G_H(g0) |
		VIDOSDxC_ALPHA0_B_H(b0) |
		VIDOSDxC_ALPHA1_R_H(r1) |
		VIDOSDxC_ALPHA1_G_H(g1) |
		VIDOSDxC_ALPHA1_B_H(b1);
}

static inline u32 vidw_alpha(bool has_osd_alpha, u8 r, u8 g, u8 b)
{
	if (has_osd_alpha)
		return VIDWxALPHAx_R_L(r) |
			VIDWxALPHAx_G_L(g) |
			VIDWxALPHAx_B_L(b);
	else
		return VIDWxALPHAx_R(r) |
			VIDWxALPHAx_G(g) |
			VIDWxALPHAx_B(b);
}

static inline u32 wincon(u32 bits_per_pixel, u32 transp_length, u32 red_length)
{
	u32 data = 0;

	switch (bits_per_pixel) {
	case 1:
		data |= WINCON0_BPPMODE_1BPP;
		data |= WINCONx_BITSWP;
		data |= WINCONx_BURSTLEN_4WORD;
		break;
	case 2:
		data |= WINCON0_BPPMODE_2BPP;
		data |= WINCONx_BITSWP;
		data |= WINCONx_BURSTLEN_8WORD;
		break;
	case 4:
		data |= WINCON0_BPPMODE_4BPP;
		data |= WINCONx_BITSWP;
		data |= WINCONx_BURSTLEN_8WORD;
		break;
	case 8:
		if (transp_length != 0)
			data |= WINCON1_BPPMODE_8BPP_1232;
		else
			data |= WINCON0_BPPMODE_8BPP_PALETTE;
		data |= WINCONx_BURSTLEN_8WORD;
		data |= WINCONx_BYTSWP;
		break;
	case 16:
		if (transp_length == 1)
			data |= WINCON1_BPPMODE_16BPP_A1555
				| WINCON1_BLD_PIX;
		else if (transp_length == 4)
			data |= WINCON1_BPPMODE_16BPP_A4444
				| WINCON1_BLD_PIX;
		else
			data |= WINCON0_BPPMODE_16BPP_565;
		data |= WINCONx_HAWSWP;
		data |= WINCONx_BURSTLEN_16WORD;
		break;
	case 24:
	case 32:
		if (red_length == 6) {
			if (transp_length != 0)
				data |= WINCON1_BPPMODE_19BPP_A1666;
			else
				data |= WINCON1_BPPMODE_18BPP_666;
		} else if (transp_length == 1)
			data |= WINCON1_BPPMODE_25BPP_A1888
				| WINCON1_BLD_PIX;
		else if ((transp_length == 4) ||
			(transp_length == 8))
			data |= WINCON1_BPPMODE_28BPP_A4888
				| WINCON1_BLD_PIX;
		else
			data |= WINCON0_BPPMODE_24BPP_888;

		data |= WINCONx_WSWP;
		data |= WINCONx_BURSTLEN_16WORD;
		break;
	}

	if (transp_length != 1)
		data |= WINCON1_ALPHA_SEL;

	return data;
}

static inline u32 blendeq(enum s3c_fb_blending blending, u8 transp_length)
{
	u8 a, b;

	if (transp_length == 1 && blending == S3C_FB_BLENDING_PREMULT)
		blending = S3C_FB_BLENDING_COVERAGE;

	switch (blending) {
	case S3C_FB_BLENDING_NONE:
		a = BLENDEQ_COEF_ONE;
		b = BLENDEQ_COEF_ZERO;
		break;

	case S3C_FB_BLENDING_PREMULT:
		a = BLENDEQ_COEF_ONE;
		b = BLENDEQ_COEF_ONE_MINUS_ALPHA_A;
		break;

	case S3C_FB_BLENDING_COVERAGE:
		a = BLENDEQ_COEF_ALPHA_A;
		b = BLENDEQ_COEF_ONE_MINUS_ALPHA_A;
		break;

	default:
		return 0;
	}

	return BLENDEQ_A_FUNC(a) |
			BLENDEQ_B_FUNC(b) |
			BLENDEQ_P_FUNC(BLENDEQ_COEF_ZERO) |
			BLENDEQ_Q_FUNC(BLENDEQ_COEF_ZERO);
}

static void s3c_fb_configure_lcd(struct s3c_fb *sfb,
		struct fb_videomode *win_mode)
{
	int clkdiv = s3c_fb_calc_pixclk(sfb, win_mode->pixclock);
	u32 data;

	data = readl(sfb->regs + VIDCON2);
	data &= ~(VIDCON2_RGB_ORDER_E_MASK | VIDCON2_RGB_ORDER_O_MASK);
	data |= VIDCON2_RGB_ORDER_E_BGR | VIDCON2_RGB_ORDER_O_BGR;
	writel(data, sfb->regs + VIDCON2);

	/* Set alpha value width */
	if (sfb->variant.has_blendcon) {
		data = readl(sfb->regs + BLENDCON);
		data &= ~BLENDCON_NEW_MASK;
		data |= BLENDCON_NEW_8BIT_ALPHA_VALUE;
		writel(data, sfb->regs + BLENDCON);
	}

	data = VIDTCON0_VBPD(win_mode->upper_margin - 1)
			| VIDTCON0_VFPD(win_mode->lower_margin - 1)
			| VIDTCON0_VSPW(win_mode->vsync_len - 1);
	writel(data, sfb->regs + sfb->variant.vidtcon);
	data = VIDTCON1_HBPD(win_mode->left_margin - 1)
			| VIDTCON1_HFPD(win_mode->right_margin - 1)
			| VIDTCON1_HSPW(win_mode->hsync_len - 1);
	/* VIDTCON1 */
	writel(data, sfb->regs + sfb->variant.vidtcon + 4);
	data = VIDTCON2_LINEVAL(win_mode->yres - 1)
			| VIDTCON2_HOZVAL(win_mode->xres - 1)
			| VIDTCON2_LINEVAL_E(win_mode->yres - 1)
			| VIDTCON2_HOZVAL_E(win_mode->xres - 1);
	/* VIDTCON2 */
	writel(data, sfb->regs + sfb->variant.vidtcon + 8);

	data = sfb->pdata->vidcon0;
	data &= ~(VIDCON0_CLKVAL_F_MASK | VIDCON0_CLKDIR);
	if (clkdiv > 1)
		data |= VIDCON0_CLKVAL_F(clkdiv-1) | VIDCON0_CLKDIR;
	else
		data &= ~VIDCON0_CLKDIR;

	/* write the timing data to the panel */
	if (sfb->variant.is_2443)
		data |= (1 << 5);

	data |= VIDCON0_ENVID | VIDCON0_ENVID_F;
	writel(data, sfb->regs + VIDCON0);
}

static unsigned int s3c_fb_calc_bandwidth(u32 w, u32 h, u32 bits_per_pixel, int fps)
{
	unsigned int bw = w * h;

	bw *= DIV_ROUND_UP(bits_per_pixel, 8);
	bw *= fps;

	return bw;
}

/**
 * s3c_fb_set_par() - framebuffer request to set new framebuffer state.
 * @info: The framebuffer to change.
 *
 * Framebuffer layer request to set a new mode for the specified framebuffer
 */
static int s3c_fb_set_par(struct fb_info *info)
{
	struct fb_var_screeninfo *var = &info->var;
	struct s3c_fb_win *win = info->par;
	struct s3c_fb *sfb = win->parent;
	void __iomem *regs = sfb->regs;
	void __iomem *buf = regs;
	int win_no = win->index;
	u32 data;
	int old_wincon;

	dev_dbg(sfb->dev, "setting framebuffer parameters\n");

	pm_runtime_get_sync(sfb->dev);

	if (sfb->power_state == POWER_DOWN)
		return 0;

	shadow_protect_win(win, 1);

	info->fix.visual = fb_visual(var->bits_per_pixel,
			win->variant.palette_sz);

	info->fix.line_length = fb_linelength(var->xres_virtual,
			var->bits_per_pixel);

	info->fix.xpanstep = fb_panstep(var->xres, var->xres_virtual);
	info->fix.ypanstep = fb_panstep(var->yres, var->yres_virtual);

	/* disable the window whilst we update it */
	old_wincon = readl(regs + WINCON(win_no));
	writel(0, regs + WINCON(win_no));

	/* write the buffer address */

	/* start and end registers stride is 8 */
	buf = regs + win_no * 8;

	writel(info->fix.smem_start, buf + sfb->variant.buf_start);

	data = info->fix.smem_start + info->fix.line_length * var->yres;
	writel(data, buf + sfb->variant.buf_end);

	data = vidw_buf_size(var->xres, info->fix.line_length,
			var->bits_per_pixel);
	writel(data, regs + sfb->variant.buf_size + (win_no * 4));

	/* write 'OSD' registers to control position of framebuffer */

	data = vidosd_a(0, 0);
	writel(data, regs + VIDOSD_A(win_no, sfb->variant));

	data = vidosd_b(0, 0, var->xres, var->yres);
	writel(data, regs + VIDOSD_B(win_no, sfb->variant));

	data = var->xres * var->yres;

	if (win->variant.has_osd_alpha) {
		data = vidosd_c(0, 0, 0, 0xff, 0xff, 0xff);
		writel(data, regs + VIDOSD_C(win_no, sfb->variant));
	}
	data = vidw_alpha(win->variant.has_osd_alpha, 0, 0, 0);
	writel(data, regs + VIDW_ALPHA0(win_no));
	data = vidw_alpha(win->variant.has_osd_alpha, 0xff, 0xff, 0xff);
	writel(data, regs + VIDW_ALPHA1(win_no));

	/* preserve whether window was enabled */
	data = old_wincon & WINCONx_ENWIN;

	/* note, since we have to round up the bits-per-pixel, we end up
	 * relying on the bitfield information for r/g/b/a to work out
	 * exactly which mode of operation is intended. */

	data |= wincon(var->bits_per_pixel, var->transp.length,
			var->red.length);

	/* Enable the colour keying for the window below this one */
	if (win_no > 0) {
		u32 keycon0_data = 0, keycon1_data = 0;
		void __iomem *keycon = regs + sfb->variant.keycon;

		keycon0_data = ~(WxKEYCON0_KEYBL_EN |
				WxKEYCON0_KEYEN_F |
				WxKEYCON0_DIRCON) | WxKEYCON0_COMPKEY(0);

		keycon1_data = WxKEYCON1_COLVAL(0xffffff);

		keycon += (win_no - 1) * 8;

		writel(keycon0_data, keycon + WKEYCON0);
		writel(keycon1_data, keycon + WKEYCON1);
	}

	writel(data, regs + sfb->variant.wincon + (win_no * 4));
	writel(0x0, regs + sfb->variant.winmap + (win_no * 4));

	shadow_protect_win(win, 0);

	pm_runtime_put_sync(sfb->dev);

	return 0;
}

/**
 * s3c_fb_update_palette() - set or schedule a palette update.
 * @sfb: The hardware information.
 * @win: The window being updated.
 * @reg: The palette index being changed.
 * @value: The computed palette value.
 *
 * Change the value of a palette register, either by directly writing to
 * the palette (this requires the palette RAM to be disconnected from the
 * hardware whilst this is in progress) or schedule the update for later.
 *
 * At the moment, since we have no VSYNC interrupt support, we simply set
 * the palette entry directly.
 */
static void s3c_fb_update_palette(struct s3c_fb *sfb,
				  struct s3c_fb_win *win,
				  unsigned int reg,
				  u32 value)
{
	void __iomem *palreg;
	u32 palcon;

	palreg = sfb->regs + sfb->variant.palette[win->index];

	dev_dbg(sfb->dev, "%s: win %d, reg %d (%p): %08x\n",
		__func__, win->index, reg, palreg, value);

	win->palette_buffer[reg] = value;

	palcon = readl(sfb->regs + WPALCON);
	writel(palcon | WPALCON_PAL_UPDATE, sfb->regs + WPALCON);

	if (win->variant.palette_16bpp)
		writew(value, palreg + (reg * 2));
	else
		writel(value, palreg + (reg * 4));

	writel(palcon, sfb->regs + WPALCON);
}

static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

/**
 * s3c_fb_setcolreg() - framebuffer layer request to change palette.
 * @regno: The palette index to change.
 * @red: The red field for the palette data.
 * @green: The green field for the palette data.
 * @blue: The blue field for the palette data.
 * @trans: The transparency (alpha) field for the palette data.
 * @info: The framebuffer being changed.
 */
static int s3c_fb_setcolreg(unsigned regno,
			    unsigned red, unsigned green, unsigned blue,
			    unsigned transp, struct fb_info *info)
{
	struct s3c_fb_win *win = info->par;
	struct s3c_fb *sfb = win->parent;
	unsigned int val;

	dev_dbg(sfb->dev, "%s: win %d: %d => rgb=%d/%d/%d\n",
		__func__, win->index, regno, red, green, blue);

	pm_runtime_get_sync(sfb->dev);

	if (sfb->power_state == POWER_DOWN)
		return 0;

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/* true-colour, use pseudo-palette */

		if (regno < 16) {
			u32 *pal = info->pseudo_palette;

			val  = chan_to_field(red,   &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue,  &info->var.blue);

			pal[regno] = val;
		}
		break;

	case FB_VISUAL_PSEUDOCOLOR:
		if (regno < win->variant.palette_sz) {
			val  = chan_to_field(red, &win->palette.r);
			val |= chan_to_field(green, &win->palette.g);
			val |= chan_to_field(blue, &win->palette.b);

			s3c_fb_update_palette(sfb, win, regno, val);
		}

		break;

	default:
		pm_runtime_put_sync(sfb->dev);
		return 1;	/* unknown type */
	}

	pm_runtime_put_sync(sfb->dev);
	return 0;
}

static void s3c_fb_activate_window(struct s3c_fb *sfb, unsigned int index)
{
	u32 wincon = readl(sfb->regs + WINCON(index));
	wincon |= WINCONx_ENWIN;
	writel(wincon, sfb->regs + WINCON(index));
}

static void s3c_fb_activate_window_dma(struct s3c_fb *sfb, unsigned int index)
{
	u32 shadowcon = readl(sfb->regs + SHADOWCON);
	shadowcon |= SHADOWCON_CHx_ENABLE(index);
	writel(shadowcon, sfb->regs + SHADOWCON);

	writel(0, sfb->regs + WINxMAP(index));
}

static int s3c_fb_enable(struct s3c_fb *sfb);
static int s3c_fb_disable(struct s3c_fb *sfb);

/**
 * s3c_fb_blank() - blank or unblank the given window
 * @blank_mode: The blank state from FB_BLANK_*
 * @info: The framebuffer to blank.
 *
 * Framebuffer layer request to change the power state.
 */
static int s3c_fb_blank(int blank_mode, struct fb_info *info)
{
	struct s3c_fb_win *win = info->par;
	struct s3c_fb *sfb = win->parent;
	int ret = 0;

	dev_dbg(sfb->dev, "blank mode %d\n", blank_mode);

	pm_runtime_get_sync(sfb->dev);

	if (sfb->power_state == POWER_DOWN)
		return 0;

	switch (blank_mode) {
	case FB_BLANK_POWERDOWN:
	case FB_BLANK_NORMAL:
		ret = s3c_fb_disable(sfb);
#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
		pm_qos_update_request(&exynos5_mif_qos, 0);
#endif
		break;

	case FB_BLANK_UNBLANK:
#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
		pm_qos_update_request(&exynos5_mif_qos, 200000);
#endif
		ret = s3c_fb_enable(sfb);
		break;

	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	default:
		ret = -EINVAL;
	}

	pm_runtime_put_sync(sfb->dev);

	return ret;
}

/**
 * s3c_fb_pan_display() - Pan the display.
 *
 * Note that the offsets can be written to the device at any time, as their
 * values are latched at each vsync automatically. This also means that only
 * the last call to this function will have any effect on next vsync, but
 * there is no need to sleep waiting for it to prevent tearing.
 *
 * @var: The screen information to verify.
 * @info: The framebuffer device.
 */
static int s3c_fb_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct s3c_fb_win *win	= info->par;
	struct s3c_fb *sfb	= win->parent;
	void __iomem *buf	= sfb->regs + win->index * 8;
	unsigned int start_boff, end_boff;

	pm_runtime_get_sync(sfb->dev);

	/* Offset in bytes to the start of the displayed area */
	start_boff = var->yoffset * info->fix.line_length;
	/* X offset depends on the current bpp */
	if (info->var.bits_per_pixel >= 8) {
		start_boff += var->xoffset * (info->var.bits_per_pixel >> 3);
	} else {
		switch (info->var.bits_per_pixel) {
		case 4:
			start_boff += var->xoffset >> 1;
			break;
		case 2:
			start_boff += var->xoffset >> 2;
			break;
		case 1:
			start_boff += var->xoffset >> 3;
			break;
		default:
			dev_err(sfb->dev, "invalid bpp\n");
			pm_runtime_put_sync(sfb->dev);
			return -EINVAL;
		}
	}
	/* Offset in bytes to the end of the displayed area */
	end_boff = start_boff + info->var.yres * info->fix.line_length;

	/* Temporarily turn off per-vsync update from shadow registers until
	 * both start and end addresses are updated to prevent corruption */
	shadow_protect_win(win, 1);

	s3c_fb_activate_window_dma(sfb, win->index);
	s3c_fb_activate_window(sfb, win->index);

	writel(info->fix.smem_start + start_boff, buf + sfb->variant.buf_start);
	writel(info->fix.smem_start + end_boff, buf + sfb->variant.buf_end);

	shadow_protect_win(win, 0);

	pm_runtime_put_sync(sfb->dev);
	return 0;
}

/**
 * s3c_fb_enable_irq() - enable framebuffer interrupts
 * @sfb: main hardware state
 */
static void s3c_fb_enable_irq(struct s3c_fb *sfb)
{
	void __iomem *regs = sfb->regs;
	u32 irq_ctrl_reg;

	pm_runtime_get_sync(sfb->dev);
	irq_ctrl_reg = readl(regs + VIDINTCON0);

	irq_ctrl_reg |= VIDINTCON0_INT_ENABLE;
	irq_ctrl_reg |= VIDINTCON0_INT_FRAME;
#ifdef CONFIG_DEBUG_FS
	irq_ctrl_reg &= ~VIDINTCON0_FIFOLEVEL_MASK;
	irq_ctrl_reg |= VIDINTCON0_FIFOLEVEL_EMPTY;
	irq_ctrl_reg |= VIDINTCON0_INT_FIFO;
	irq_ctrl_reg |= VIDINTCON0_FIFIOSEL_WINDOW0;
	irq_ctrl_reg |= VIDINTCON0_FIFIOSEL_WINDOW1;
	irq_ctrl_reg |= VIDINTCON0_FIFIOSEL_WINDOW2;
	irq_ctrl_reg |= VIDINTCON0_FIFIOSEL_WINDOW3;
	irq_ctrl_reg |= VIDINTCON0_FIFIOSEL_WINDOW4;
#endif

	irq_ctrl_reg &= ~VIDINTCON0_FRAMESEL0_MASK;
	irq_ctrl_reg |= VIDINTCON0_FRAMESEL0_VSYNC;
	irq_ctrl_reg &= ~VIDINTCON0_FRAMESEL1_MASK;
	irq_ctrl_reg |= VIDINTCON0_FRAMESEL1_NONE;

	writel(irq_ctrl_reg, regs + VIDINTCON0);
	pm_runtime_put_sync(sfb->dev);
}

/**
 * s3c_fb_disable_irq() - disable framebuffer interrupts
 * @sfb: main hardware state
 */
static void s3c_fb_disable_irq(struct s3c_fb *sfb)
{
	void __iomem *regs = sfb->regs;
	u32 irq_ctrl_reg;

	pm_runtime_get_sync(sfb->dev);
	irq_ctrl_reg = readl(regs + VIDINTCON0);

#ifdef CONFIG_DEBUG_FS
	irq_ctrl_reg |= VIDINTCON0_INT_FIFO;
#endif
	irq_ctrl_reg &= ~VIDINTCON0_INT_FRAME;
	irq_ctrl_reg &= ~VIDINTCON0_INT_ENABLE;

	writel(irq_ctrl_reg, regs + VIDINTCON0);
	pm_runtime_put_sync(sfb->dev);
}

static void s3c_fb_activate_vsync(struct s3c_fb *sfb)
{
	int prev_refcount;

	mutex_lock(&sfb->vsync_info.irq_lock);

	prev_refcount = sfb->vsync_info.irq_refcount++;
	if (!prev_refcount)
		s3c_fb_enable_irq(sfb);

	mutex_unlock(&sfb->vsync_info.irq_lock);
}

static void s3c_fb_deactivate_vsync(struct s3c_fb *sfb)
{
	int new_refcount;

	mutex_lock(&sfb->vsync_info.irq_lock);

	new_refcount = --sfb->vsync_info.irq_refcount;
	WARN_ON(new_refcount < 0);
	if (!new_refcount)
		s3c_fb_disable_irq(sfb);

	mutex_unlock(&sfb->vsync_info.irq_lock);
}

/**
 * sfb_fb_log_fifo_underflow_locked - log a FIFO underflow event.  Caller must
 * hold the driver's spin lock while calling.
 *
 * @sfb: main hardware state
 * @timestamp: timestamp of the FIFO underflow event
 */
void s3c_fb_log_fifo_underflow_locked(struct s3c_fb *sfb, ktime_t timestamp)
{
#ifdef CONFIG_DEBUG_FS
	unsigned int idx = sfb->debug_data.num_timestamps %
			S3C_FB_DEBUG_FIFO_TIMESTAMPS;
	sfb->debug_data.fifo_timestamps[idx] = timestamp;
	sfb->debug_data.num_timestamps++;
	if (sfb->debug_data.num_timestamps > S3C_FB_DEBUG_FIFO_TIMESTAMPS) {
		sfb->debug_data.first_timestamp++;
		sfb->debug_data.first_timestamp %= S3C_FB_DEBUG_FIFO_TIMESTAMPS;
	}

	memcpy(sfb->debug_data.regs_at_underflow, sfb->regs,
			sizeof(sfb->debug_data.regs_at_underflow));
#endif
}

static irqreturn_t s3c_fb_irq(int irq, void *dev_id)
{
	struct s3c_fb *sfb = dev_id;
	void __iomem  *regs = sfb->regs;
	u32 irq_sts_reg;
	ktime_t timestamp = ktime_get();

	spin_lock(&sfb->slock);

	irq_sts_reg = readl(regs + VIDINTCON1);

	if (irq_sts_reg & VIDINTCON1_INT_FRAME) {

		/* VSYNC interrupt, accept it */
		writel(VIDINTCON1_INT_FRAME, regs + VIDINTCON1);

		sfb->vsync_info.timestamp = timestamp;
		wake_up_interruptible_all(&sfb->vsync_info.wait);
	}
	if (irq_sts_reg & VIDINTCON1_INT_FIFO) {
		writel(VIDINTCON1_INT_FIFO, regs + VIDINTCON1);
		s3c_fb_log_fifo_underflow_locked(sfb, timestamp);
	}

	spin_unlock(&sfb->slock);
	return IRQ_HANDLED;
}

/**
 * s3c_fb_wait_for_vsync() - sleep until next VSYNC interrupt or timeout
 * @sfb: main hardware state
 * @timeout: timeout in msecs, or 0 to wait indefinitely.
 */
static int s3c_fb_wait_for_vsync(struct s3c_fb *sfb, u32 timeout)
{
	ktime_t timestamp;
	int ret;

	pm_runtime_get_sync(sfb->dev);

	timestamp = sfb->vsync_info.timestamp;
	s3c_fb_activate_vsync(sfb);
	if (timeout) {
		ret = wait_event_interruptible_timeout(sfb->vsync_info.wait,
				!ktime_equal(timestamp,
						sfb->vsync_info.timestamp),
				msecs_to_jiffies(timeout));
	} else {
		ret = wait_event_interruptible(sfb->vsync_info.wait,
				!ktime_equal(timestamp,
						sfb->vsync_info.timestamp));
	}
	s3c_fb_deactivate_vsync(sfb);

	pm_runtime_put_sync(sfb->dev);

	if (timeout && ret == 0)
		return -ETIMEDOUT;

	return 0;
}

int s3c_fb_set_window_position(struct fb_info *info,
				struct s3c_fb_user_window user_window)
{
	struct s3c_fb_win *win = info->par;
	struct s3c_fb *sfb = win->parent;
	struct fb_var_screeninfo *var = &info->var;
	int win_no = win->index;
	void __iomem *regs = sfb->regs;
	u32 data;

	pm_runtime_get_sync(sfb->dev);
	shadow_protect_win(win, 1);

	if (!s3c_fb_validate_x_alignment(sfb, user_window.x, var->xres,
			var->bits_per_pixel))
		return -EINVAL;

	/* write 'OSD' registers to control position of framebuffer */
	data = vidosd_a(user_window.x, user_window.y);
	writel(data, regs + VIDOSD_A(win_no, sfb->variant));

	data = vidosd_b(user_window.x, user_window.y, var->xres, var->yres);
	writel(data, regs + VIDOSD_B(win_no, sfb->variant));

	shadow_protect_win(win, 0);
	pm_runtime_put_sync(sfb->dev);
	return 0;
}

int s3c_fb_set_plane_alpha_blending(struct fb_info *info,
				struct s3c_fb_user_plane_alpha user_alpha)
{
	struct s3c_fb_win *win = info->par;
	struct s3c_fb *sfb = win->parent;
	int win_no = win->index;
	void __iomem *regs = sfb->regs;
	u32 data;

	u32 alpha_high = 0;
	u32 alpha_low = 0;

	alpha_high = ((((user_alpha.red & 0xf0) >> 4) << 8) |
			(((user_alpha.green & 0xf0) >> 4) << 4) |
			(((user_alpha.blue & 0xf0) >> 4) << 0));

	alpha_low = ((((user_alpha.red & 0xf)) << 16) |
			(((user_alpha.green & 0xf)) << 8) |
			(((user_alpha.blue & 0xf)) << 0));

	pm_runtime_get_sync(sfb->dev);
	shadow_protect_win(win, 1);

	data = readl(regs + sfb->variant.wincon + (win_no * 4));
	data &= ~(WINCON1_BLD_PIX | WINCON1_ALPHA_SEL);
	data |= WINCON1_BLD_PLANE;

	if (user_alpha.channel == 0)
		alpha_high = alpha_high << 12;
	else {
		data |= WINCON1_ALPHA_SEL;
		alpha_high = alpha_high << 0;
	}

	writel(data, regs + sfb->variant.wincon + (win_no * 4));
	writel(alpha_high, regs + VIDOSD_C(win_no, sfb->variant));

	if (sfb->variant.has_alphacon) {
		if (user_alpha.channel == 0)
			writel(alpha_low, regs + VIDW_ALPHA0(win_no));
		else
			writel(alpha_low, regs + VIDW_ALPHA1(win_no));
	}

	shadow_protect_win(win, 0);
	pm_runtime_put_sync(sfb->dev);

	return 0;
}

int s3c_fb_set_chroma_key(struct fb_info *info,
			struct s3c_fb_user_chroma user_chroma)
{
	struct s3c_fb_win *win = info->par;
	struct s3c_fb *sfb = win->parent;
	int win_no = win->index;
	void __iomem *regs = sfb->regs;
	void __iomem *keycon = regs + sfb->variant.keycon;

	u32 data = 0;

	u32 chroma_value;

	chroma_value = (((user_chroma.red & 0xff) << 16) |
			((user_chroma.green & 0xff) << 8) |
			((user_chroma.blue & 0xff) << 0));

	pm_runtime_get_sync(sfb->dev);
	shadow_protect_win(win, 1);

	if (user_chroma.enabled)
		data |= WxKEYCON0_KEYEN_F;

	keycon += (win_no-1) * 8;
	writel(data, keycon + WKEYCON0);

	data = (chroma_value & 0xffffff);
	writel(data, keycon + WKEYCON1);

	shadow_protect_win(win, 0);
	pm_runtime_put_sync(sfb->dev);

	return 0;
}

int s3c_fb_set_vsync_int(struct fb_info *info,
		bool active)
{
	struct s3c_fb_win *win = info->par;
	struct s3c_fb *sfb = win->parent;
	bool prev_active = sfb->vsync_info.active;

	sfb->vsync_info.active = active;
	smp_wmb();

	if (active && !prev_active)
		s3c_fb_activate_vsync(sfb);
	else if (!active && prev_active)
		s3c_fb_deactivate_vsync(sfb);

	return 0;
}

#ifdef CONFIG_ION_EXYNOS
static unsigned int s3c_fb_map_ion_handle(struct s3c_fb *sfb,
		struct s3c_dma_buf_data *dma, struct ion_handle *ion_handle,
		int fd)
{
	dma->fence = NULL;

	dma->dma_buf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dma->dma_buf)) {
		dev_err(sfb->dev, "dma_buf_get() failed: %ld\n",
				PTR_ERR(dma->dma_buf));
		goto err_share_dma_buf;
	}

	dma->attachment = dma_buf_attach(dma->dma_buf, sfb->dev);
	if (IS_ERR_OR_NULL(dma->attachment)) {
		dev_err(sfb->dev, "dma_buf_attach() failed: %ld\n",
				PTR_ERR(dma->attachment));
		goto err_buf_map_attach;
	}

#if !defined(CONFIG_FB_EXYNOS_FIMD_SYSMMU_DISABLE)
	dma->sg_table = dma_buf_map_attachment(dma->attachment,
			DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(dma->sg_table)) {
		dev_err(sfb->dev, "dma_buf_map_attachment() failed: %ld\n",
				PTR_ERR(dma->sg_table));
		goto err_buf_map_attachment;
	}

#ifdef CONFIG_ARCH_EXYNOS4
	dma->dma_addr = iovmm_map(&s5p_device_fimd0.dev, dma->sg_table->sgl, 0,
			dma->dma_buf->size);
#else
	dma->dma_addr = iovmm_map(&s5p_device_fimd1.dev, dma->sg_table->sgl, 0,
			dma->dma_buf->size);
#endif
	if (!dma->dma_addr || IS_ERR_VALUE(dma->dma_addr)) {
		dev_err(sfb->dev, "iovmm_map() failed: %d\n", dma->dma_addr);
		goto err_iovmm_map;
	}
#else
	if (ion_phys(sfb->fb_ion_client, ion_handle, &dma->dma_addr, &dma->dma_buf->size)) {
		dev_err(sfb->dev, "%s: Failed to get phys. addr of buf\n", __func__);
		goto err_buf_map_attachment;
	}
#endif
	dma->ion_handle = ion_handle;
	return dma->dma_buf->size;

err_iovmm_map:
	dma_buf_unmap_attachment(dma->attachment, dma->sg_table,
			DMA_BIDIRECTIONAL);
err_buf_map_attachment:
	dma_buf_detach(dma->dma_buf, dma->attachment);
err_buf_map_attach:
	dma_buf_put(dma->dma_buf);
err_share_dma_buf:
	return 0;
}

static void s3c_fb_free_dma_buf(struct s3c_fb *sfb,
		struct s3c_dma_buf_data *dma)
{
	if (!dma->dma_addr)
		return;

	if (dma->fence)
		sync_fence_put(dma->fence);

#if !defined(CONFIG_FB_EXYNOS_FIMD_SYSMMU_DISABLE)
	iovmm_unmap(sfb->dev, dma->dma_addr);

	dma_buf_unmap_attachment(dma->attachment, dma->sg_table,
			DMA_BIDIRECTIONAL);
#endif
	dma_buf_detach(dma->dma_buf, dma->attachment);
	dma_buf_put(dma->dma_buf);
	ion_free(sfb->fb_ion_client, dma->ion_handle);
	memset(dma, 0, sizeof(struct s3c_dma_buf_data));
}

static u32 s3c_fb_red_length(int format)
{
	switch (format) {
	case S3C_FB_PIXEL_FORMAT_RGBA_8888:
	case S3C_FB_PIXEL_FORMAT_RGBX_8888:
	case S3C_FB_PIXEL_FORMAT_BGRA_8888:
	case S3C_FB_PIXEL_FORMAT_BGRX_8888:
		return 8;

	case S3C_FB_PIXEL_FORMAT_RGBA_5551:
		return 5;

	case S3C_FB_PIXEL_FORMAT_RGB_565:
		return 5;

	default:
		pr_warn("s3c-fb: unrecognized pixel format %u\n", format);
		return 0;
	}
}

static u32 s3c_fb_red_offset(int format)
{
	switch (format) {
	case S3C_FB_PIXEL_FORMAT_RGBA_8888:
	case S3C_FB_PIXEL_FORMAT_RGBX_8888:
	case S3C_FB_PIXEL_FORMAT_RGBA_5551:
		return 0;

	case S3C_FB_PIXEL_FORMAT_RGB_565:
		return 11;

	case S3C_FB_PIXEL_FORMAT_BGRA_8888:
	case S3C_FB_PIXEL_FORMAT_BGRX_8888:
		return 16;

	default:
		pr_warn("s3c-fb: unrecognized pixel format %u\n", format);
		return 0;
	}
}

static u32 s3c_fb_green_length(int format)
{
	switch (format) {
	case S3C_FB_PIXEL_FORMAT_RGBA_8888:
	case S3C_FB_PIXEL_FORMAT_RGBX_8888:
	case S3C_FB_PIXEL_FORMAT_BGRA_8888:
	case S3C_FB_PIXEL_FORMAT_BGRX_8888:
		return 8;

	case S3C_FB_PIXEL_FORMAT_RGBA_5551:
		return 5;

	case S3C_FB_PIXEL_FORMAT_RGB_565:
		return 6;

	default:
		pr_warn("s3c-fb: unrecognized pixel format %u\n", format);
		return 0;
	}
}

static u32 s3c_fb_green_offset(int format)
{
	switch (format) {
	case S3C_FB_PIXEL_FORMAT_RGBA_8888:
	case S3C_FB_PIXEL_FORMAT_RGBX_8888:
	case S3C_FB_PIXEL_FORMAT_BGRA_8888:
	case S3C_FB_PIXEL_FORMAT_BGRX_8888:
		return 8;

	case S3C_FB_PIXEL_FORMAT_RGBA_5551:
	case S3C_FB_PIXEL_FORMAT_RGB_565:
		return 5;
	default:
		pr_warn("s3c-fb: unrecognized pixel format %u\n", format);
		return 0;
	}
}

static u32 s3c_fb_blue_length(int format)
{
	return s3c_fb_red_length(format);
}

static u32 s3c_fb_blue_offset(int format)
{
	switch (format) {
	case S3C_FB_PIXEL_FORMAT_RGBA_8888:
	case S3C_FB_PIXEL_FORMAT_RGBX_8888:
		return 16;

	case S3C_FB_PIXEL_FORMAT_RGBA_5551:
		return 10;

	case S3C_FB_PIXEL_FORMAT_RGB_565:
	case S3C_FB_PIXEL_FORMAT_BGRA_8888:
	case S3C_FB_PIXEL_FORMAT_BGRX_8888:
		return 0;

	default:
		pr_warn("s3c-fb: unrecognized pixel format %u\n", format);
		return 0;
	}
}

static u32 s3c_fb_transp_length(int format)
{
	switch (format) {
	case S3C_FB_PIXEL_FORMAT_RGBA_8888:
	case S3C_FB_PIXEL_FORMAT_BGRA_8888:
		return 8;

	case S3C_FB_PIXEL_FORMAT_RGBA_5551:
		return 1;

	case S3C_FB_PIXEL_FORMAT_RGBX_8888:
	case S3C_FB_PIXEL_FORMAT_RGB_565:
	case S3C_FB_PIXEL_FORMAT_BGRX_8888:
		return 0;

	default:
		pr_warn("s3c-fb: unrecognized pixel format %u\n", format);
		return 0;
	}
}

static u32 s3c_fb_transp_offset(int format)
{
	switch (format) {
	case S3C_FB_PIXEL_FORMAT_RGBA_8888:
	case S3C_FB_PIXEL_FORMAT_BGRA_8888:
		return 24;

	case S3C_FB_PIXEL_FORMAT_RGBA_5551:
		return 15;

	case S3C_FB_PIXEL_FORMAT_RGBX_8888:
		return s3c_fb_blue_offset(format);

	case S3C_FB_PIXEL_FORMAT_BGRX_8888:
		return s3c_fb_red_offset(format);

	case S3C_FB_PIXEL_FORMAT_RGB_565:
		return 0;

	default:
		pr_warn("s3c-fb: unrecognized pixel format %u\n", format);
		return 0;
	}
}

static u32 s3c_fb_padding(int format)
{
	switch (format) {
	case S3C_FB_PIXEL_FORMAT_RGBX_8888:
	case S3C_FB_PIXEL_FORMAT_BGRX_8888:
		return 8;

	case S3C_FB_PIXEL_FORMAT_RGBA_8888:
	case S3C_FB_PIXEL_FORMAT_RGBA_5551:
	case S3C_FB_PIXEL_FORMAT_RGB_565:
	case S3C_FB_PIXEL_FORMAT_BGRA_8888:
		return 0;

	default:
		pr_warn("s3c-fb: unrecognized pixel format %u\n", format);
		return 0;
	}

}

static u32 s3c_fb_rgborder(int format)
{
	switch (format) {
	case S3C_FB_PIXEL_FORMAT_RGBX_8888:
	case S3C_FB_PIXEL_FORMAT_RGBA_8888:
	case S3C_FB_PIXEL_FORMAT_RGBA_5551:
		return WIN_RGB_ORDER_RGB;

	case S3C_FB_PIXEL_FORMAT_RGB_565:
	case S3C_FB_PIXEL_FORMAT_BGRA_8888:
	case S3C_FB_PIXEL_FORMAT_BGRX_8888:
		return WIN_RGB_ORDER_BGR;

	default:
		pr_warn("s3c-fb: unrecognized pixel format %u\n", format);
		return 0;
	}

}
static int s3c_fb_set_win_buffer(struct s3c_fb *sfb, struct s3c_fb_win *win,
		struct s3c_fb_win_config *win_config, struct s3c_reg_data *regs)
{
	struct ion_handle *handle;
	struct fb_var_screeninfo prev_var = win->fbinfo->var;
	struct s3c_dma_buf_data dma_buf_data;
	unsigned short win_no = win->index;
	int ret;
	size_t buf_size, window_size;
	u8 alpha0, alpha1;

	if (win_config->format >= S3C_FB_PIXEL_FORMAT_MAX) {
		dev_err(sfb->dev, "unknown pixel format %u\n",
				win_config->format);
		return -EINVAL;
	}

	if (win_config->blending >= S3C_FB_BLENDING_MAX) {
		dev_err(sfb->dev, "unknown blending %u\n",
				win_config->blending);
		return -EINVAL;
	}

	if (win_no == 0 && win_config->blending != S3C_FB_BLENDING_NONE) {
		dev_err(sfb->dev, "blending not allowed on window 0\n");
		return -EINVAL;
	}

	if (win_config->w == 0 || win_config->h == 0) {
		dev_err(sfb->dev, "window is size 0 (w = %u, h = %u)\n",
				win_config->w, win_config->h);
		return -EINVAL;
	}

	win->fbinfo->var.red.length = s3c_fb_red_length(win_config->format);
	win->fbinfo->var.red.offset = s3c_fb_red_offset(win_config->format);
	win->fbinfo->var.green.length = s3c_fb_green_length(win_config->format);
	win->fbinfo->var.green.offset = s3c_fb_green_offset(win_config->format);
	win->fbinfo->var.blue.length = s3c_fb_blue_length(win_config->format);
	win->fbinfo->var.blue.offset = s3c_fb_blue_offset(win_config->format);
	win->fbinfo->var.transp.length =
			s3c_fb_transp_length(win_config->format);
	win->fbinfo->var.transp.offset =
			s3c_fb_transp_offset(win_config->format);
	win->fbinfo->var.bits_per_pixel = win->fbinfo->var.red.length +
			win->fbinfo->var.green.length +
			win->fbinfo->var.blue.length +
			win->fbinfo->var.transp.length +
			s3c_fb_padding(win_config->format);


	regs->win_rgborder[win_no] = s3c_fb_rgborder(win_config->format);

	if (win_config->w * win->fbinfo->var.bits_per_pixel / 8 < 128) {
		dev_err(sfb->dev, "window must be at least 128 bytes wide (width = %u, bpp = %u)\n",
				win_config->w,
				win->fbinfo->var.bits_per_pixel);
		ret = -EINVAL;
		goto err_invalid;
	}

	if (win_config->stride <
			win_config->w * win->fbinfo->var.bits_per_pixel / 8) {
		dev_err(sfb->dev, "stride shorter than buffer width (stride = %u, width = %u, bpp = %u)\n",
				win_config->stride, win_config->w,
				win->fbinfo->var.bits_per_pixel);
		ret = -EINVAL;
		goto err_invalid;
	}

	if (!s3c_fb_validate_x_alignment(sfb, win_config->x, win_config->w,
			win->fbinfo->var.bits_per_pixel)) {
		ret = -EINVAL;
		goto err_invalid;
	}

	handle = ion_import_dma_buf(sfb->fb_ion_client, win_config->fd);
	if (IS_ERR(handle)) {
		dev_err(sfb->dev, "failed to import fd\n");
		ret = PTR_ERR(handle);
		goto err_invalid;
	}

	buf_size = s3c_fb_map_ion_handle(sfb, &dma_buf_data, handle,
			win_config->fd);
	if (!buf_size) {
		ret = -ENOMEM;
		ion_free(sfb->fb_ion_client, handle);
		goto err_invalid;
	}

	if (win_config->fence_fd >= 0) {
		dma_buf_data.fence = sync_fence_fdget(win_config->fence_fd);
		if (!dma_buf_data.fence) {
			dev_err(sfb->dev, "failed to import fence fd\n");
			ret = -EINVAL;
			goto err_offset;
		}
	}

	window_size = win_config->stride * (win_config->h);
	if (win_config->offset + window_size > buf_size) {
		dev_err(sfb->dev, "window goes past end of buffer (width = %u, height = %u, stride = %u, offset = %u, buf_size = %u)\n",
				win_config->w, win_config->h,
				win_config->stride, win_config->offset,
				buf_size);
		ret = -EINVAL;
		goto err_offset;
	}

	win->fbinfo->fix.smem_start = dma_buf_data.dma_addr
			+ win_config->offset;
	win->fbinfo->fix.smem_len = window_size;
	win->fbinfo->var.xres = win_config->w;
	win->fbinfo->var.xres_virtual = win_config->stride * 8 /
			win->fbinfo->var.bits_per_pixel;
	win->fbinfo->var.yres = win->fbinfo->var.yres_virtual = win_config->h;
	win->fbinfo->var.xoffset = win_config->offset % win_config->stride;
	win->fbinfo->var.yoffset = win_config->offset / win_config->stride;

	win->fbinfo->fix.visual = fb_visual(win->fbinfo->var.bits_per_pixel,
			win->variant.palette_sz);
	win->fbinfo->fix.line_length = win_config->stride;
	win->fbinfo->fix.xpanstep = fb_panstep(win_config->w,
			win->fbinfo->var.xres_virtual);
	win->fbinfo->fix.ypanstep = fb_panstep(win_config->h, win_config->h);

	regs->dma_buf_data[win_no] = dma_buf_data;
	regs->vidw_buf_start[win_no] = win->fbinfo->fix.smem_start;
	regs->vidw_buf_end[win_no] = regs->vidw_buf_start[win_no] +
			window_size;
	regs->vidw_buf_size[win_no] = vidw_buf_size(win_config->w,
			win->fbinfo->fix.line_length,
			win->fbinfo->var.bits_per_pixel);

	regs->vidosd_a[win_no] = vidosd_a(win_config->x, win_config->y);
	regs->vidosd_b[win_no] = vidosd_b(win_config->x, win_config->y,
			win_config->w, win_config->h);

	if (win->fbinfo->var.transp.length == 1 &&
			win_config->blending == S3C_FB_BLENDING_NONE) {
		alpha0 = 0xff;
		alpha1 = 0xff;
	} else {
		alpha0 = 0;
		alpha1 = 0xff;
	}

	if (win->variant.has_osd_alpha)
		regs->vidosd_c[win_no] = vidosd_c(alpha0, alpha0, alpha0,
				alpha1, alpha1, alpha1);
	regs->vidw_alpha0[win_no] = vidw_alpha(win->variant.has_osd_alpha,
			alpha0, alpha0, alpha0);
	regs->vidw_alpha1[win_no] = vidw_alpha(win->variant.has_osd_alpha,
			alpha1, alpha1, alpha1);

	if (win->variant.osd_size_off) {
		u32 size = win_config->w * win_config->h;
		if (win->variant.has_osd_alpha)
			regs->vidosd_d[win_no] = size;
		else
			regs->vidosd_c[win_no] = size;
	}

	regs->shadowcon |= SHADOWCON_CHx_ENABLE(win_no);

	regs->wincon[win_no] = wincon(win->fbinfo->var.bits_per_pixel,
			win->fbinfo->var.transp.length,
			win->fbinfo->var.red.length);
	if (win_no)
		regs->blendeq[win_no - 1] = blendeq(win_config->blending,
				win->fbinfo->var.transp.length);

	return 0;

err_offset:
	s3c_fb_free_dma_buf(sfb, &dma_buf_data);
err_invalid:
	win->fbinfo->var = prev_var;
	return ret;
}

static int s3c_fb_set_win_config(struct s3c_fb *sfb,
		struct s3c_fb_win_config_data *win_data)
{
	struct s3c_fb_win_config *win_config = win_data->config;
	int ret = 0;
	unsigned short i;
	struct s3c_reg_data *regs;
	struct sync_fence *fence;
	struct sync_pt *pt;
	int fd;
	unsigned int bw = 0;

	fd = get_unused_fd();

	if (fd < 0)
		return fd;

	mutex_lock(&sfb->output_lock);

	if (!sfb->output_on) {
		sfb->timeline_max++;
		pt = sw_sync_pt_create(sfb->timeline, sfb->timeline_max);
		fence = sync_fence_create("display", pt);
		sync_fence_install(fence, fd);
		win_data->fence = fd;

		sw_sync_timeline_inc(sfb->timeline, 1);
		goto err;
	}

	regs = kzalloc(sizeof(struct s3c_reg_data), GFP_KERNEL);
	if (!regs) {
		dev_err(sfb->dev, "could not allocate s3c_reg_data\n");
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0; i < sfb->variant.nr_windows; i++) {
		if (!sfb->windows[i]->local) {
			sfb->windows[i]->prev_fix =
				sfb->windows[i]->fbinfo->fix;
			sfb->windows[i]->prev_var =
				sfb->windows[i]->fbinfo->var;
		}
	}

	for (i = 0; i < sfb->variant.nr_windows && !ret; i++) {
		struct s3c_fb_win_config *config = &win_config[i];
		struct s3c_fb_win *win = sfb->windows[i];

		bool enabled = 0;
		u32 color_map = WINxMAP_MAP | WINxMAP_MAP_COLOUR(0);

		if (!sfb->windows[i]->local) {
			switch (config->state) {
			case S3C_FB_WIN_STATE_DISABLED:
				break;
			case S3C_FB_WIN_STATE_COLOR:
				enabled = 1;
				color_map |= WINxMAP_MAP_COLOUR(config->color);
				regs->vidosd_a[i] = vidosd_a(config->x, config->y);
				regs->vidosd_b[i] = vidosd_b(config->x, config->y,
						config->w, config->h);
				break;
			case S3C_FB_WIN_STATE_BUFFER:
				ret = s3c_fb_set_win_buffer(sfb, win, config, regs);
				if (!ret) {
					enabled = 1;
					color_map = 0;
				}
				break;
			default:
				dev_warn(sfb->dev, "unrecognized window state %u",
						config->state);
				ret = -EINVAL;
				break;
			}
		}
		if (enabled)
			regs->wincon[i] |= WINCONx_ENWIN;
		else
			regs->wincon[i] &= ~WINCONx_ENWIN;
		regs->winmap[i] = color_map;

		if (enabled && config->state == S3C_FB_WIN_STATE_BUFFER) {
			bw += s3c_fb_calc_bandwidth(config->w, config->h,
					win->fbinfo->var.bits_per_pixel,
					win->fps);
		}
	}

	regs->bandwidth = bw;

	dev_dbg(sfb->dev, "Total BW = %d Mbits, Max BW per window = %d Mbits\n",
			bw / (1024 * 1024), MAX_BW_PER_WINDOW / (1024 * 1024));

	if (ret) {
		for (i = 0; i < sfb->variant.nr_windows; i++) {
			if (!sfb->windows[i]->local) {
				sfb->windows[i]->fbinfo->fix =
					sfb->windows[i]->prev_fix;
				sfb->windows[i]->fbinfo->var =
					sfb->windows[i]->prev_var;
				s3c_fb_free_dma_buf(sfb,
					&regs->dma_buf_data[i]);
			}
		}
		put_unused_fd(fd);
		kfree(regs);
	} else {
		mutex_lock(&sfb->update_regs_list_lock);
		sfb->timeline_max++;
		pt = sw_sync_pt_create(sfb->timeline, sfb->timeline_max);
		fence = sync_fence_create("display", pt);
		sync_fence_install(fence, fd);
		win_data->fence = fd;

		list_add_tail(&regs->list, &sfb->update_regs_list);
		mutex_unlock(&sfb->update_regs_list_lock);
		queue_kthread_work(&sfb->update_regs_worker,
				&sfb->update_regs_work);
	}

err:
	mutex_unlock(&sfb->output_lock);
	return ret;
}

static void __s3c_fb_update_regs(struct s3c_fb *sfb, struct s3c_reg_data *regs)
{
	unsigned short i;
	unsigned int data;
	for (i = 0; i < sfb->variant.nr_windows; i++) {
		if (!sfb->windows[i]->local)
			shadow_protect_win(sfb->windows[i], 1);
	}
	for (i = 0; i < sfb->variant.nr_windows; i++) {
		if (!sfb->windows[i]->local) {
			writel(regs->wincon[i], sfb->regs + WINCON(i));
			writel(regs->win_rgborder[i],
				sfb->regs + WIN_RGB_ORDER(i));
			writel(regs->winmap[i], sfb->regs + WINxMAP(i));
			writel(regs->vidosd_a[i],
				sfb->regs + VIDOSD_A(i, sfb->variant));
			writel(regs->vidosd_b[i],
				sfb->regs + VIDOSD_B(i, sfb->variant));
			if (sfb->windows[i]->variant.has_osd_c)
				writel(regs->vidosd_c[i],
				sfb->regs + VIDOSD_C(i, sfb->variant));
			if (sfb->windows[i]->variant.has_osd_d)
				writel(regs->vidosd_d[i],
				sfb->regs + VIDOSD_D(i, sfb->variant));
			writel(regs->vidw_alpha0[i],
				sfb->regs + VIDW_ALPHA0(i));
			writel(regs->vidw_alpha1[i],
				sfb->regs + VIDW_ALPHA1(i));
			writel(regs->vidw_buf_start[i],
				sfb->regs + VIDW_BUF_START(i));
			writel(regs->vidw_buf_end[i],
				sfb->regs + VIDW_BUF_END(i));
			writel(regs->vidw_buf_size[i],
				sfb->regs + VIDW_BUF_SIZE(i));
			if (i)
				writel(regs->blendeq[i - 1],
				sfb->regs + BLENDEQ(i));
			sfb->windows[i]->dma_buf_data =
				regs->dma_buf_data[i];
		}
	}

	if (sfb->variant.has_shadowcon) {
		data = readl(sfb->regs + SHADOWCON);
		if ((data & 0x21) == 0x21)
			data = (0x21 | regs->shadowcon);
		else
			data = regs->shadowcon | 0x1;

		writel(data, sfb->regs + SHADOWCON);
	}

	for (i = 0; i < sfb->variant.nr_windows; i++) {
		if (!sfb->windows[i]->local)
			shadow_protect_win(sfb->windows[i], 0);
	}
}

static void s3c_fd_fence_wait(struct s3c_fb *sfb, struct sync_fence *fence)
{
	int err = sync_fence_wait(fence, 1000);
	if (err >= 0)
		return;

	if (err == -ETIME)
		err = sync_fence_wait(fence, 10 * MSEC_PER_SEC);

	if (err < 0)
		dev_warn(sfb->dev, "error waiting on fence: %d\n", err);
}

static void s3c_fb_update_regs(struct s3c_fb *sfb, struct s3c_reg_data *regs)
{
	struct s3c_dma_buf_data old_dma_bufs[S3C_FB_MAX_WIN];
	bool wait_for_vsync;
	int count = 100;
	int i;

	pm_runtime_get_sync(sfb->dev);

	for (i = 0; i < sfb->variant.nr_windows; i++) {
		u32 new_start = regs->vidw_buf_start[i];
		u32 shadow_start = readl(sfb->regs +
				SHD_VIDW_BUF_START(i));
#if !defined(CONFIG_FB_EXYNOS_FIMD_SYSMMU_DISABLE)
		if (new_start && (new_start == shadow_start)) {
			pr_err("%s: 0x%08x is already unmapped \
					address\n", __func__, new_start);
			pm_runtime_put_sync(sfb->dev);
			sw_sync_timeline_inc(sfb->timeline, 1);
			return;
		}
#endif
		if (!sfb->windows[i]->local) {
			old_dma_bufs[i] = sfb->windows[i]->dma_buf_data;
			if (regs->dma_buf_data[i].fence)
				s3c_fd_fence_wait(sfb,
					regs->dma_buf_data[i].fence);
		}
	}

#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	if (regs->bandwidth > FHD_MAX_BW_PER_WINDOW) {
		pm_qos_update_request(&exynos5_mif_qos, 400000);
		pm_qos_update_request(&exynos5_int_qos, 100000);
		bts_set_bw(regs->bandwidth);
	}
#endif

	do {
		__s3c_fb_update_regs(sfb, regs);
		s3c_fb_wait_for_vsync(sfb, VSYNC_TIMEOUT_MSEC);
		wait_for_vsync = false;

		for (i = 0; i < sfb->variant.nr_windows; i++) {
			if (!sfb->windows[i]->local) {
				u32 new_start = regs->vidw_buf_start[i];
				u32 shadow_start = readl(sfb->regs +
						SHD_VIDW_BUF_START(i));
				if (unlikely(new_start != shadow_start)) {
					wait_for_vsync = true;
					break;
				}
			}
		}
	} while (wait_for_vsync && count--);
	if (wait_for_vsync) {
		pr_err("%s: failed to update registers\n", __func__);
		for (i = 0; i < sfb->variant.nr_windows; i++)
			if (!sfb->windows[i]->local)
				pr_err("window %d new value %08x register value %08x\n",
				i, regs->vidw_buf_start[i],
				readl(sfb->regs + SHD_VIDW_BUF_START(i)));
	}

	pm_runtime_put_sync(sfb->dev);
	sw_sync_timeline_inc(sfb->timeline, 1);

	for (i = 0; i < sfb->variant.nr_windows; i++)
		if (!sfb->windows[i]->local)
			s3c_fb_free_dma_buf(sfb, &old_dma_bufs[i]);

#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	if (regs->bandwidth <= FHD_MAX_BW_PER_WINDOW) {
		bts_set_bw(regs->bandwidth);
		pm_qos_update_request(&exynos5_mif_qos, 200000);
		pm_qos_update_request(&exynos5_int_qos, 0);
	}
#endif
}

static void s3c_fb_update_regs_handler(struct kthread_work *work)
{
	struct s3c_fb *sfb =
			container_of(work, struct s3c_fb, update_regs_work);
	struct s3c_reg_data *data, *next;
	struct list_head saved_list;

	mutex_lock(&sfb->update_regs_list_lock);
	saved_list = sfb->update_regs_list;
	list_replace_init(&sfb->update_regs_list, &saved_list);
	mutex_unlock(&sfb->update_regs_list_lock);

	list_for_each_entry_safe(data, next, &saved_list, list) {
		s3c_fb_update_regs(sfb, data);
		list_del(&data->list);
		kfree(data);
	}
}

static int s3c_fb_get_user_ion_handle(struct s3c_fb *sfb,
				struct s3c_fb_win *win,
				struct s3c_fb_user_ion_client *user_ion_client)
{
	/* Create fd for ion_buffer */
	user_ion_client->fd = ion_share_dma_buf(sfb->fb_ion_client,
					win->dma_buf_data.ion_handle);
	if (user_ion_client->fd < 0) {
		pr_err("ion_share_fd failed\n");
		return user_ion_client->fd;
	}
	return 0;
}
#endif

static int s3c_fb_ioctl(struct fb_info *info, unsigned int cmd,
			unsigned long arg)
{
	struct s3c_fb_win *win = info->par;
	struct s3c_fb *sfb = win->parent;
	int ret;
	u32 crtc;

	struct fb_var_screeninfo *var = &info->var;
	int offset;

	union {
		struct s3c_fb_user_window user_window;
		struct s3c_fb_user_plane_alpha user_alpha;
		struct s3c_fb_user_chroma user_chroma;
		struct s3c_fb_user_ion_client user_ion_client;
		struct s3c_fb_win_config_data win_data;
		u32 vsync;
	} p;

	if ((sfb->power_state == POWER_DOWN) &&
		(cmd != S3CFB_WIN_CONFIG))
		return 0;

	switch (cmd) {
	case FBIO_WAITFORVSYNC:
		if (get_user(crtc, (u32 __user *)arg)) {
			ret = -EFAULT;
			break;
		}

		if (crtc == 0)
			ret = s3c_fb_wait_for_vsync(sfb, VSYNC_TIMEOUT_MSEC);
		else
			ret = -ENODEV;

		break;

	case S3CFB_WIN_POSITION:
		if (copy_from_user(&p.user_window,
				(struct s3c_fb_user_window __user *)arg,
				sizeof(p.user_window))) {
			ret = -EFAULT;
			break;
		}

		if (p.user_window.x < 0)
			p.user_window.x = 0;
		if (p.user_window.y < 0)
			p.user_window.y = 0;

		ret = s3c_fb_set_window_position(info, p.user_window);
		break;

	case S3CFB_WIN_SET_PLANE_ALPHA:
		if (copy_from_user(&p.user_alpha,
				(struct s3c_fb_user_plane_alpha __user *)arg,
				sizeof(p.user_alpha))) {
			ret = -EFAULT;
			break;
		}

		ret = s3c_fb_set_plane_alpha_blending(info, p.user_alpha);
		break;

	case S3CFB_WIN_SET_CHROMA:
		if (copy_from_user(&p.user_chroma,
				   (struct s3c_fb_user_chroma __user *)arg,
				   sizeof(p.user_chroma))) {
			ret = -EFAULT;
			break;
		}

		ret = s3c_fb_set_chroma_key(info, p.user_chroma);
		break;

	case S3CFB_SET_VSYNC_INT:
		if (get_user(p.vsync, (int __user *)arg)) {
			ret = -EFAULT;
			break;
		}

		ret = s3c_fb_set_vsync_int(info, p.vsync);
		break;

#ifdef CONFIG_ION_EXYNOS
	case S3CFB_WIN_CONFIG:
		if (copy_from_user(&p.win_data,
				   (struct s3c_fb_win_config_data __user *)arg,
				   sizeof(p.win_data))) {
			ret = -EFAULT;
			break;
		}

		ret = s3c_fb_set_win_config(sfb, &p.win_data);
		if (ret)
			break;

		if (copy_to_user((struct s3c_fb_win_config_data __user *)arg,
				 &p.win_data,
				 sizeof(p.win_data))) {
			ret = -EFAULT;
			break;
		}

		break;

	case S3CFB_GET_ION_USER_HANDLE:
		if (copy_from_user(&p.user_ion_client,
				(struct s3c_fb_user_ion_client __user *)arg,
				sizeof(p.user_ion_client))) {
			ret = -EFAULT;
			break;
		}

		if (s3c_fb_get_user_ion_handle(sfb, win, &p.user_ion_client)) {
			ret = -EFAULT;
			break;
		}

		offset = var->xres_virtual * var->yoffset + var->xoffset;
		offset *= var->bits_per_pixel / 8;
		p.user_ion_client.offset = offset;

		dev_dbg(sfb->dev, "Buffer offset: 0x%x\n",
			p.user_ion_client.offset);

		if (copy_to_user((struct s3c_fb_user_ion_client __user *)arg,
				&p.user_ion_client,
				sizeof(p.user_ion_client))) {
			ret = -EFAULT;
			break;
		}
		ret = 0;
		break;
#endif

	default:
		ret = -ENOTTY;
	}

	return ret;
}

static int s3c_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct s3c_fb_win *win = info->par;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	return dma_buf_mmap(win->dma_buf_data.dma_buf, vma, 0);
}

static struct fb_ops s3c_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= s3c_fb_check_var,
	.fb_set_par	= s3c_fb_set_par,
	.fb_blank	= s3c_fb_blank,
	.fb_setcolreg	= s3c_fb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_pan_display	= s3c_fb_pan_display,
	.fb_ioctl	= s3c_fb_ioctl,
	.fb_mmap	= s3c_fb_mmap,
};

/**
 * s3c_fb_missing_pixclock() - calculates pixel clock
 * @mode: The video mode to change.
 *
 * Calculate the pixel clock when none has been given through platform data.
 */
static void __devinit s3c_fb_missing_pixclock(struct fb_videomode *mode)
{
	u64 pixclk = 1000000000000ULL;
	u32 div;

	div  = mode->left_margin + mode->hsync_len + mode->right_margin +
	       mode->xres;
	div *= mode->upper_margin + mode->vsync_len + mode->lower_margin +
	       mode->yres;
#if defined(CONFIG_LCD_MIPI_S6E8AB0) /* this define will be delete after mipi lcd supports 60Hz */
	div *= mode->refresh ? : 40;
#else
	div *= mode->refresh ? : 60;
#endif
	do_div(pixclk, div);

	mode->pixclock = pixclk;
}

/**
 * s3c_fb_alloc_memory() - allocate display memory for framebuffer window
 * @sfb: The base resources for the hardware.
 * @win: The window to initialise memory for.
 *
 * Allocate memory for the given framebuffer.
 */
static int __devinit s3c_fb_alloc_memory(struct s3c_fb *sfb,
					 struct s3c_fb_win *win)
{
	struct s3c_fb_pd_win *windata = win->windata;
	unsigned int real_size, virt_size, size;
	struct fb_info *fbi = win->fbinfo;
	struct ion_handle *handle;
	dma_addr_t map_dma;
	int fd;
	unsigned int ret;
	struct file *file;

	dev_dbg(sfb->dev, "allocating memory for display\n");

	real_size = windata->win_mode.xres * windata->win_mode.yres;
	virt_size = windata->virtual_x * windata->virtual_y;

	dev_dbg(sfb->dev, "real_size=%u (%u.%u), virt_size=%u (%u.%u)\n",
		real_size, windata->win_mode.xres, windata->win_mode.yres,
		virt_size, windata->virtual_x, windata->virtual_y);

	size = (real_size > virt_size) ? real_size : virt_size;
	size *= (windata->max_bpp > 16) ? 32 : windata->max_bpp;
	size /= 8;

	fbi->fix.smem_len = size;
	size = PAGE_ALIGN(size);

	dev_dbg(sfb->dev, "want %u bytes for window[%d]\n", size, win->index);

#if defined(CONFIG_ION_EXYNOS)
#if defined(CONFIG_FB_EXYNOS_FIMD_SYSMMU_DISABLE)
	map_dma = NULL;
#else
	handle = ion_alloc(sfb->fb_ion_client, (size_t)size, 0,
					ION_HEAP_EXYNOS_MASK, 0);
	if (IS_ERR(handle)) {
		dev_err(sfb->dev, "failed to ion_alloc\n");
		return -ENOMEM;
	}

	fd = ion_share_dma_buf(sfb->fb_ion_client, handle);
	if (fd < 0) {
		dev_err(sfb->dev, "ion_share_dma_buf() failed\n");
		goto err_share_dma_buf;
	}

	ret = s3c_fb_map_ion_handle(sfb, &win->dma_buf_data, handle, fd);
	if (!ret)
		goto err_map;
	map_dma = win->dma_buf_data.dma_addr;
#endif
#else
	fbi->screen_base = dma_alloc_writecombine(sfb->dev, size,
						  &map_dma, GFP_KERNEL);
	if (!fbi->screen_base)
		return -ENOMEM;

	dev_dbg(sfb->dev, "mapped %x to %p\n",
		(unsigned int)map_dma, fbi->screen_base);

	memset(fbi->screen_base, 0x0, size);
#endif
	fbi->fix.smem_start = map_dma;

	return 0;

#ifdef CONFIG_ION_EXYNOS
err_map:
	file = fget(fd);
	if (file) {
		fput(file);
		fput(file);
	}
err_share_dma_buf:
	ion_free(sfb->fb_ion_client, handle);
	return -ENOMEM;
#endif
}

/**
 * s3c_fb_free_memory() - free the display memory for the given window
 * @sfb: The base resources for the hardware.
 * @win: The window to free the display memory for.
 *
 * Free the display memory allocated by s3c_fb_alloc_memory().
 */
static void s3c_fb_free_memory(struct s3c_fb *sfb, struct s3c_fb_win *win)
{
#if defined(CONFIG_ION_EXYNOS)
	s3c_fb_free_dma_buf(sfb, &win->dma_buf_data);
#else
	struct fb_info *fbi = win->fbinfo;

	if (fbi->screen_base)
		dma_free_writecombine(sfb->dev, PAGE_ALIGN(fbi->fix.smem_len),
			      fbi->screen_base, fbi->fix.smem_start);
#endif

}

/**
 * s3c_fb_release_win() - release resources for a framebuffer window.
 * @win: The window to cleanup the resources for.
 *
 * Release the resources that where claimed for the hardware window,
 * such as the framebuffer instance and any memory claimed for it.
 */
static void s3c_fb_release_win(struct s3c_fb *sfb, struct s3c_fb_win *win)
{
	u32 data;

	if (win->fbinfo) {
		if (sfb->variant.has_shadowcon) {
			data = readl(sfb->regs + SHADOWCON);
			data &= ~SHADOWCON_CHx_ENABLE(win->index);
			data &= ~SHADOWCON_CHx_LOCAL_ENABLE(win->index);
			writel(data, sfb->regs + SHADOWCON);
		}
		if (win->fbinfo->cmap.len)
			fb_dealloc_cmap(&win->fbinfo->cmap);
		s3c_fb_free_memory(sfb, win);
		framebuffer_release(win->fbinfo);
	}
}

/**
 * s3c_fb_probe_win() - register an hardware window
 * @sfb: The base resources for the hardware
 * @variant: The variant information for this window.
 * @res: Pointer to where to place the resultant window.
 *
 * Allocate and do the basic initialisation for one of the hardware's graphics
 * windows.
 */
static int __devinit s3c_fb_probe_win(struct s3c_fb *sfb, unsigned int win_no,
				      struct s3c_fb_win_variant *variant,
				      struct s3c_fb_win **res)
{
	struct fb_var_screeninfo *var;
	struct fb_videomode *initmode;
	struct s3c_fb_pd_win *windata;
	struct s3c_fb_win *win;
	struct fb_info *fbinfo;
	int palette_size;
	int ret;

	dev_dbg(sfb->dev, "probing window %d, variant %p\n", win_no, variant);

	init_waitqueue_head(&sfb->vsync_info.wait);

	palette_size = variant->palette_sz * 4;

	fbinfo = framebuffer_alloc(sizeof(struct s3c_fb_win) +
				   palette_size * sizeof(u32), sfb->dev);
	if (!fbinfo) {
		dev_err(sfb->dev, "failed to allocate framebuffer\n");
		return -ENOENT;
	}

	windata = sfb->pdata->win[win_no];
	initmode = &windata->win_mode;

	WARN_ON(windata->max_bpp == 0);
	WARN_ON(windata->win_mode.xres == 0);
	WARN_ON(windata->win_mode.yres == 0);

	win = fbinfo->par;
	*res = win;
	var = &fbinfo->var;
	win->variant = *variant;
	win->fbinfo = fbinfo;
	win->parent = sfb;
	win->windata = windata;
	win->index = win_no;
	win->palette_buffer = (u32 *)(win + 1);
	memset(&win->dma_buf_data, 0, sizeof(win->dma_buf_data));

	ret = s3c_fb_alloc_memory(sfb, win);
	if (ret) {
		dev_err(sfb->dev, "failed to allocate display memory\n");
		return ret;
	}

	/* setup the r/b/g positions for the window's palette */
	if (win->variant.palette_16bpp) {
		/* Set RGB 5:6:5 as default */
		win->palette.r.offset = 11;
		win->palette.r.length = 5;
		win->palette.g.offset = 5;
		win->palette.g.length = 6;
		win->palette.b.offset = 0;
		win->palette.b.length = 5;

	} else {
		/* Set 8bpp or 8bpp and 1bit alpha */
		win->palette.r.offset = 16;
		win->palette.r.length = 8;
		win->palette.g.offset = 8;
		win->palette.g.length = 8;
		win->palette.b.offset = 0;
		win->palette.b.length = 8;
	}

	/* setup the initial video mode from the window */
	fb_videomode_to_var(&fbinfo->var, initmode);

	fbinfo->fix.type	= FB_TYPE_PACKED_PIXELS;
	fbinfo->fix.accel	= FB_ACCEL_NONE;
	fbinfo->var.activate	= FB_ACTIVATE_NOW;
	fbinfo->var.vmode	= FB_VMODE_NONINTERLACED;
	fbinfo->var.bits_per_pixel = windata->default_bpp;
	fbinfo->var.width	= windata->width;
	fbinfo->var.height	= windata->height;
	fbinfo->fbops		= &s3c_fb_ops;
	fbinfo->flags		= FBINFO_FLAG_DEFAULT;
	fbinfo->pseudo_palette  = &win->pseudo_palette;

	/* prepare to actually start the framebuffer */

	ret = s3c_fb_check_var(&fbinfo->var, fbinfo);
	if (ret < 0) {
		dev_err(sfb->dev, "check_var failed on initial video params\n");
		return ret;
	}

	/* create initial colour map */

	ret = fb_alloc_cmap(&fbinfo->cmap, win->variant.palette_sz, 1);
	if (ret == 0)
		fb_set_cmap(&fbinfo->cmap, fbinfo);
	else
		dev_err(sfb->dev, "failed to allocate fb cmap\n");

	return 0;
}

/**
 * s3c_fb_clear_win() - clear hardware window registers.
 * @sfb: The base resources for the hardware.
 * @win: The window to process.
 *
 * Reset the specific window registers to a known state.
 */
static void s3c_fb_clear_win(struct s3c_fb *sfb, int win)
{
	void __iomem *regs = sfb->regs;
	u32 reg;

	writel(0, regs + sfb->variant.wincon + (win * 4));
	writel(0, regs + VIDOSD_A(win, sfb->variant));
	writel(0, regs + VIDOSD_B(win, sfb->variant));
	writel(0, regs + VIDOSD_C(win, sfb->variant));
	if (sfb->variant.has_shadowcon) {
		reg = readl(sfb->regs + SHADOWCON);
		reg &= ~SHADOWCON_CHx_ENABLE(win);
		reg &= ~SHADOWCON_WINx_PROTECT(win);
		writel(reg, sfb->regs + SHADOWCON);
	}
	reg = readl(sfb->regs + WINCON(win));
	reg &= ~WINCONx_ENWIN;
	writel(reg, sfb->regs + WINCON(win));
}

/* --------------------For Local path from Gscaler ------------------------*/
#ifdef CONFIG_FB_EXYNOS_FIMD_MC
static inline struct s3c_fb_win *v4l2_subdev_to_s3c_fb_win(struct v4l2_subdev *sd)
{
	/* member instance, name of the parent structure, name of memeber in the parent structure */
	return container_of(sd, struct s3c_fb_win, sd);
}

static int s3c_fb_sd_pad_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_format *format)
{
	u32 data;
	struct s3c_fb_win *win = v4l2_subdev_to_s3c_fb_win(sd);
	struct s3c_fb *sfb = win->parent;

	/* (width, height) : (xres, yres) */
	win->fbinfo->var.xres = format->format.width;
	win->fbinfo->var.yres = format->format.height;
	data = win->fbinfo->var.xres * win->fbinfo->var.yres;

	vidosd_set_size(win, data);

	if (data > (1280*720)) {
		data = readl(sfb->regs + WINCON(win->index));
		data |= WINCONx_CSC_CON_EQ709;
		writel(data, sfb->regs + WINCON(win->index));
		dev_dbg(sfb->dev, "Over HD size : (width, height) : (%d, %d)\n",
				win->fbinfo->var.xres, win->fbinfo->var.yres);
	}

	dev_dbg(sfb->dev, "Set sd pad format (width, height) : (%d, %d)\n",
			win->fbinfo->var.xres, win->fbinfo->var.yres);
	return 0;
}

static int s3c_fb_sd_pad_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_format *format)
{
	struct s3c_fb_win *win = v4l2_subdev_to_s3c_fb_win(sd);
	struct s3c_fb *sfb = win->parent;

	/* (width, height) : (xres, yres) */
	format->format.width = win->fbinfo->var.xres;
	format->format.height = win->fbinfo->var.yres;

	/* FIMD only accept the YUV data via local bus from GSCALER */
	format->format.code = V4L2_MBUS_FMT_YUV8_1X24;

	dev_dbg(sfb->dev, "Get sd pad format (width, height) : (%d, %d)\n",
			format->format.width, format->format.height);
	return 0;
}

static int s3c_fb_sd_set_pad_crop(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_crop *crop)
{
	int ret;
	struct s3c_fb_win *win = v4l2_subdev_to_s3c_fb_win(sd);
	struct s3c_fb *sfb = win->parent;
	struct s3c_fb_user_window user_window;

	/* (left, top) : (xoffset, yoffset) */
	user_window.x = crop->rect.left;
	user_window.y = crop->rect.top;

	ret = s3c_fb_set_window_position(win->fbinfo, user_window);

	if (ret)
		return ret;

	dev_dbg(sfb->dev, "Set sd pad crop (x, y) : (%d, %d)\n",
			crop->rect.left, crop->rect.top);
	return 0;
}

static int s3c_fb_sd_pad_get_crop(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_crop *crop)
{
	struct s3c_fb_win *win = v4l2_subdev_to_s3c_fb_win(sd);
	struct s3c_fb *sfb = win->parent;

	/* (width, height) : (xres, yres) */
	crop->rect.width = win->fbinfo->var.xres;
	crop->rect.height = win->fbinfo->var.yres;
	dev_dbg(sfb->dev, "Get sd pad crop (width, height) : (%d, %d)\n",
			crop->rect.width, crop->rect.height);

	/* (left, top) : (xoffset, yoffset) */
	crop->rect.left = win->fbinfo->var.xoffset;
	crop->rect.top = win->fbinfo->var.yoffset;
	dev_dbg(sfb->dev, "Get sd pad crop (left, top) : (%d, %d)\n",
			crop->rect.left, crop->rect.top);

	return 0;
}

static int s3c_fb_sd_s_stream(struct v4l2_subdev *sd, int enable)
{
	u32 data = 0;
	struct s3c_fb_win *win = v4l2_subdev_to_s3c_fb_win(sd);
	struct s3c_fb *sfb = win->parent;
	int ret;
	if (enable) {
		shadow_protect_win(win, 1);
		data = readl(sfb->regs + SHADOWCON);
		data |= (SHADOWCON_CHx_ENABLE(win->index) |
				SHADOWCON_CHx_LOCAL_ENABLE(win->index));
		writel(data, sfb->regs + SHADOWCON);
		data = readl(sfb->regs + WINCON(win->index));
		data |= WINCONx_INRGB_YCBCR;
		data |= (1 << 0);
		writel(data, sfb->regs + WINCON(win->index));
		shadow_protect_win(win, 0);
	} else {
		shadow_protect_win(win, 1);
		data = readl(sfb->regs + SHADOWCON);
		data &=  ~(SHADOWCON_CHx_ENABLE(win->index) |
				SHADOWCON_CHx_LOCAL_ENABLE(win->index));
		writel(data, sfb->regs + SHADOWCON);
		data = readl(sfb->regs + WINCON(win->index));
		data &= ~(1 << 0);
		writel(data, sfb->regs + WINCON(win->index));
		shadow_protect_win(win, 0);

		ret = s3c_fb_wait_for_vsync(sfb, VSYNC_TIMEOUT_MSEC);
		if (ret) {
			dev_err(sfb->dev, "wait timeout for vsync: %s\n", __func__);
			return ret;
		}
	}

	dev_dbg(sfb->dev, "Get the window via local path started/stopped : %d\n",
			enable);
	return 0;
}

static const struct v4l2_subdev_pad_ops s3c_fb_sd_pad_ops = {
	.set_fmt = s3c_fb_sd_pad_set_fmt,
	.get_fmt = s3c_fb_sd_pad_get_fmt,
	.get_crop = s3c_fb_sd_pad_get_crop,
	.set_crop = s3c_fb_sd_set_pad_crop,
};

static const struct v4l2_subdev_video_ops s3c_fb_sd_video_ops = {
	.s_stream = s3c_fb_sd_s_stream,
};

static const struct v4l2_subdev_ops s3c_fb_sd_ops = {
	.pad = &s3c_fb_sd_pad_ops,
	.video = &s3c_fb_sd_video_ops,
};

static void s3c_fb_mc_local_path_setup(struct s3c_fb_win *win)
{
	u32 data = 0;
	struct s3c_fb *sfb = win->parent;

	if (win->local) {
		/* Enable  the channel 1 to a local path for the window1
		   in fimd1 */

		/* MIXER0_VALID[7] & MIXER1_VALID[4] : should be 0
		   (FIMD1 Data Valid) */
		data = __raw_readl(SYSREG_DISP1BLK_CFG);
		data &= ~(SYSREG_MIXER0_VALID | SYSREG_MIXER1_VALID);
		writel(data, SYSREG_DISP1BLK_CFG);
	}

	dev_dbg(sfb->dev, "Local path set up in Winow[%d] : %d\n", win->index,
		win->local);
}

static int s3c_fb_me_link_setup(struct media_entity *entity,
			      const struct media_pad *local,
			      const struct media_pad *remote, u32 flags)
{
	int i;
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct s3c_fb_win *win = v4l2_subdev_to_s3c_fb_win(sd);
	struct s3c_fb *sfb = win->parent;

	if (flags & MEDIA_LNK_FL_ENABLED) {
		win->use = 1;
		if (local->index == FIMD_PAD_SINK_FROM_GSCALER_SRC)
			win->local = 1;
	} else {
		if (local->index == FIMD_PAD_SINK_FROM_GSCALER_SRC)
			win->local = 0;
		win->use = 0;

		for (i = 0; i < entity->num_links; ++i)
			if (entity->links[i].flags & MEDIA_LNK_FL_ENABLED)
				win->use = 1;
	}

	s3c_fb_mc_local_path_setup(win);

	dev_dbg(sfb->dev, "MC link set up between Window[%d] and Gscaler: \
			flag - %d\n", win->index, flags);
	return 0;
}

/* media entity operations */
static const struct media_entity_operations s3c_fb_me_ops = {
	.link_setup = s3c_fb_me_link_setup,
};

/*---- In probing function (local path) ------*/
static int s3c_fb_register_mc_entity(struct s3c_fb_win *win, struct exynos_md *md)
{
	int ret;
	struct s3c_fb *sfb = win->parent;
	struct v4l2_subdev *sd = &win->sd;
	struct media_pad *pads = win->pads;
	struct media_entity *me = &sd->entity;

	/* Init a window of fimd as a sub-device */
	v4l2_subdev_init(sd, &s3c_fb_sd_ops);
	sd->owner = THIS_MODULE;
	sprintf(sd->name, "s3c-fb-window%d", win->index);

	/* fimd sub-devices can be opened in user space */
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* FIMD takes a role of SINK between FIMD and Gscaler */
	pads[FIMD_PAD_SINK_FROM_GSCALER_SRC].flags = MEDIA_PAD_FL_SINK;
	me->ops = &s3c_fb_me_ops;

	/* Init a sub-device as an entity */
	ret = media_entity_init(me, FIMD_PADS_NUM, pads, 0);
	if (ret) {
		dev_err(sfb->dev, "failed to initialize media entity in FIMD\n");
		return ret;
	}

	ret = v4l2_device_register_subdev(&md->v4l2_dev, sd);
	if (ret) {
		dev_err(sfb->dev, "failed to register FIMD Window subdev\n");
		return ret;
	}

	dev_dbg(sfb->dev, "FIMD Winow[%d] MC entity init & subdev registered: %s\n",
			win->index, sd->name);

	return 0;
}

static int s3c_fb_register_mc_components(struct s3c_fb_win *win)
{
	int ret;
	struct exynos_md *md;
	struct s3c_fb *sfb = win->parent;

	/* Local paths have been set up only between Gscaler0~2 and Window0~2 */
	if (win->index >= 3)
		return -ENODEV;

	if (sfb->md == NULL) {
		md = (struct exynos_md *)module_name_to_driver_data(MDEV_MODULE_NAME);

		if (!md) {
			dev_err(sfb->dev, "failed to get output media device\n");
			return -ENODEV;
		}
		sfb->md = md;
	}

	ret = s3c_fb_register_mc_entity(win, sfb->md);
	if (ret)
		return ret;

	return 0;
}

static int s3c_fb_register_mc_subdev_nodes(struct s3c_fb *sfb)
{
	int ret;

	/* This function is for exposing sub-devices nodes to user space
	 * in case of marking with V4L2_SUBDEV_FL_HAS_DEVNODE flag.
	 *
	 * And it depends on probe sequence
	 * because v4l2_dev ptr is shared all of output devices below
	 *
	 * probe sequence of output devices
	 * output media device -> gscaler -> window in fimd
	 */
	ret = v4l2_device_register_subdev_nodes(&sfb->md->v4l2_dev);
	if (ret) {
		dev_err(sfb->dev, "failed to make nodes for subdev\n");
		return ret;
	}

	dev_dbg(sfb->dev, "Register V4L2 subdev nodes for FIMD\n");

	return 0;
}

static int s3c_fb_create_mc_links(struct s3c_fb_win *win)
{
	int ret;
	int flags;
	char err[80];
	struct exynos_md *md;
	struct s3c_fb *sfb = win->parent;

	if (win->use)
		flags = MEDIA_LNK_FL_ENABLED;
	else
		flags = 0;

	/* link creation between pads: Gscaler[1] -> Window[0] */
	md = (struct exynos_md *)module_name_to_driver_data(MDEV_MODULE_NAME);

	/* Gscaler 0 --> Winwow 0, Gscaler 1 --> Winow 1,
	   Gscaler 2 --> Window 2, Gscaler 3 --> Window 3 */
	if (md->gsc_sd[win->index] != NULL) {
		ret = media_entity_create_link(&md->gsc_sd[win->index]->entity,
			GSC_OUT_PAD_SOURCE,
			&win->sd.entity,
			FIMD_PAD_SINK_FROM_GSCALER_SRC, 0);
		if (ret) {
			sprintf(err, "%s --> %s",
				md->gsc_sd[win->index]->entity.name,
				win->sd.entity.name);
				goto mc_link_create_fail;
		}
	}

	dev_dbg(sfb->dev, "A link between Gscaler and window[%d] is created \
		successfully\n", win->index);

	return 0;

mc_link_create_fail:
	dev_err(sfb->dev, "failed to create a link between Gscaler and \
		window[%d]: %s\n", win->index, err);
	return ret;
}

static void s3c_fb_unregister_mc_entities(struct s3c_fb_win *win)
{
	v4l2_device_unregister_subdev(&win->sd);
}
#endif

/* --------------------For Writeback to Scaler ------------------------*/
#ifdef CONFIG_FB_EXYNOS_FIMD_MC_WB
static inline struct s3c_fb *v4l2_subdev_to_s3c_fb(struct v4l2_subdev *sd)
{
	/* member instance, name of the parent structure, name of memeber
	   in the parent structure */
	return container_of(sd, struct s3c_fb, sd_wb);
}
static int s3c_fb_sd_wb_s_stream(struct v4l2_subdev *sd_wb, int enable)
{
	struct s3c_fb *sfb = v4l2_subdev_to_s3c_fb(sd_wb);
	u32 ret;
	u32 vidcon0 = readl(sfb->regs + VIDCON0);
	u32 vidcon2 = readl(sfb->regs + VIDCON2);

	vidcon0 &= ~VIDCON0_VIDOUT_MASK;
	vidcon2 &= ~(VIDCON2_WB_MASK | VIDCON2_TVFORMATSEL_HW_SW_MASK | \
					VIDCON2_TVFORMATSEL_MASK);

	if (enable) {
		vidcon0 |= VIDCON0_VIDOUT_WB;
		vidcon2 |= (VIDCON2_WB_ENABLE | VIDCON2_TVFORMATSEL_SW | \
					VIDCON2_TVFORMATSEL_YUV444);
	} else {
		vidcon0 |= VIDCON0_VIDOUT_RGB;
		vidcon2 |= VIDCON2_WB_DISABLE;
	}

	ret = s3c_fb_wait_for_vsync(sfb, VSYNC_TIMEOUT_MSEC);
	if (ret) {
		dev_err(sfb->dev, "wait timeout(writeback) : %s\n", __func__);
		return ret;
	}

	writel(vidcon0, sfb->regs + VIDCON0);
	writel(vidcon2, sfb->regs + VIDCON2);

	dev_dbg(sfb->dev, "Get the writeback started/stopped : %d\n", enable);
	return 0;
}

static int s3c_fb_sd_wb_pad_get_fmt(struct v4l2_subdev *sd_wb, struct v4l2_subdev_fh *fh,
		       struct v4l2_subdev_format *format)
{
	struct s3c_fb *sfb = v4l2_subdev_to_s3c_fb(sd_wb);
	int default_win = sfb->pdata->default_win;

	/* (width, height) : (xres, yres) */
	format->format.width = sfb->windows[default_win]->fbinfo->var.xres;
	format->format.height = sfb->windows[default_win]->fbinfo->var.yres;

	/* FIMD writes the video data back to GSCALER */
	format->format.code = V4L2_MBUS_FMT_XRGB8888_4X8_LE;

	dev_dbg(sfb->dev, "Get sd wb pad format (width, height) : (%d, %d)\n",
			format->format.width, format->format.height);
	return 0;
}

static const struct v4l2_subdev_pad_ops s3c_fb_sd_wb_pad_ops = {
	.get_fmt = s3c_fb_sd_wb_pad_get_fmt,
};

static const struct v4l2_subdev_video_ops s3c_fb_sd_wb_video_ops = {
	.s_stream = s3c_fb_sd_wb_s_stream,
};

static const struct v4l2_subdev_ops s3c_fb_sd_wb_ops = {
	.video = &s3c_fb_sd_wb_video_ops,
	.pad = &s3c_fb_sd_wb_pad_ops,
};

static int s3c_fb_me_wb_link_setup(struct media_entity *entity,
			      const struct media_pad *local,
			      const struct media_pad *remote, u32 flags)
{
	int i;
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct s3c_fb *sfb = v4l2_subdev_to_s3c_fb(sd);

	if (flags & MEDIA_LNK_FL_ENABLED) {
		sfb->use_wb = 1;
		if (local->index == FIMD_WB_PAD_SRC_TO_GSCALER_SINK)
			sfb->local_wb = 1;
	} else {
		if (local->index == FIMD_WB_PAD_SRC_TO_GSCALER_SINK)
			sfb->local_wb = 0;
		sfb->use_wb = 0;

		for (i = 0; i < entity->num_links; ++i)
			if (entity->links[i].flags & MEDIA_LNK_FL_ENABLED)
				sfb->use_wb = 1;
	}

	dev_dbg(sfb->dev, "MC WB link set up between FIMD and Gscaler: \
			flag - %d\n", flags);
	return 0;
}

/* media entity operations */
static const struct media_entity_operations s3c_fb_me_wb_ops = {
	.link_setup = s3c_fb_me_wb_link_setup,
};

/* --- In probing function (writeback) ---*/
static int s3c_fb_register_mc_wb_entity(struct s3c_fb *sfb, struct exynos_md *md_wb)
{
	int ret;

	struct v4l2_subdev *sd_wb = &sfb->sd_wb;
	struct media_pad *pads_wb = &sfb->pads_wb;
	struct media_entity *me_wb = &sd_wb->entity;

	/* Init a window of fimd as a sub-device */
	v4l2_subdev_init(sd_wb, &s3c_fb_sd_wb_ops);
	sd_wb->owner = THIS_MODULE;
#ifdef CONFIG_ARCH_EXYNOS4
		snprintf(sd_wb->name, sizeof(sd_wb->name), "s5p-fimd0");
#else
		snprintf(sd_wb->name, sizeof(sd_wb->name), "s5p-fimd1");
#endif

	/* fimd sub-devices can be opened in user space */
	sd_wb->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* FIMD takes a role of sources between FIMD and Gscaler */
	pads_wb->flags = MEDIA_PAD_FL_SOURCE;
	me_wb->ops = &s3c_fb_me_wb_ops;

	/* Init a sub-device as an entity */
	ret = media_entity_init(me_wb, FIMD_WB_PADS_NUM, pads_wb, 0);
	if (ret) {
		dev_err(sfb->dev, "failed to initialize media entity in FIMD WB\n");
		return ret;
	}

	ret = v4l2_device_register_subdev(&md_wb->v4l2_dev, sd_wb);
	if (ret) {
		dev_err(sfb->dev, "failed to register FIMD WB subdev\n");
		return ret;
	}

	dev_dbg(sfb->dev, "FIMD1 WB MC entity init & subdev registered: %s\n", sd_wb->name);

	return 0;
}

static int fimd_get_media_info(struct device *dev, void *p)
{
	struct exynos_md **mdev = p;
	struct platform_device *pdev = to_platform_device(dev);

	mdev[pdev->id] = dev_get_drvdata(dev);

	if (!mdev[pdev->id])
		return -ENODEV;

	return 0;
}

static int s3c_fb_register_mc_wb_components(struct s3c_fb *sfb)
{
	int ret;
	struct exynos_md *mdev[2] = {NULL, NULL};
	struct device_driver *driver;

	driver = driver_find(MDEV_MODULE_NAME, &platform_bus_type);
	if (!driver)
		dev_err(sfb->dev, "MC driver not found in s3c_fb\n");

	ret = driver_for_each_device(driver, NULL, &mdev[0],
		fimd_get_media_info);

	sfb->md_wb = mdev[MDEV_CAPTURE];

	/* Local paths have been set up only between FIMD1 and Gscaler0~3 */
	ret = s3c_fb_register_mc_wb_entity(sfb, sfb->md_wb);
	if (ret)
		return ret;

	return 0;
}

static int s3c_fb_create_mc_wb_links(struct s3c_fb *sfb)
{
	int ret, i;
	int flags;
	char err[80];

	if (sfb->use_wb)
		flags = MEDIA_LNK_FL_ENABLED;
	else
		flags = 0;

	/* FIMD1 --> Gscaler 0, Gscaler 1, Gscaler 2, or Gscaler 3 */
	for (i = 0; i < MAX_GSC_SUBDEV; ++i) {
		if (sfb->md_wb->gsc_cap_sd[i] != NULL) {
			ret = media_entity_create_link(&sfb->sd_wb.entity, /* source */
				FIMD_WB_PAD_SRC_TO_GSCALER_SINK,
				&sfb->md_wb->gsc_cap_sd[i]->entity, /* sink */
				GSC_CAP_PAD_SINK, 0);
			if (ret) {
				sprintf(err, "%s --> %s",
					sfb->md_wb->gsc_cap_sd[i]->entity.name,
					sfb->sd_wb.entity.name);
					goto mc_wb_link_create_fail;
			}
		}

		dev_dbg(sfb->dev, "A link between FIMD1 and Gscaler[%d] is created \
			successfully\n", i);
	}

	return 0;

mc_wb_link_create_fail:
	dev_err(sfb->dev, "failed to create a link  FIMD1 and Gscaler[%d] \
		%s\n", i, err);
	return ret;
}

static int s3c_fb_register_mc_subdev_wb_nodes(struct s3c_fb *sfb)
{
	int ret;

	/* This function is for exposing sub-devices nodes to user space
	 * in case of marking with V4L2_SUBDEV_FL_HAS_DEVNODE flag.
	 *
	 * And it depends on probe sequence
	 * because v4l2_dev ptr is shared all of output devices below
	 *
	 * probe sequence of output devices
	 * output media device -> gscaler -> window in fimd
	 */
	ret = v4l2_device_register_subdev_nodes(&sfb->md_wb->v4l2_dev);
	if (ret) {
		dev_err(sfb->dev, "failed to make nodes for subdev\n");
		return ret;
	}

	dev_dbg(sfb->dev, "Register V4L2 subdev nodes for FIMD\n");

	return 0;
}

static void s3c_fb_unregister_mc_wb_entities(struct s3c_fb *sfb)
{
	v4l2_device_unregister_subdev(&sfb->sd_wb);
}
#endif

static int s3c_fb_wait_for_vsync_thread(void *data)
{
	struct s3c_fb *sfb = data;

	while (!kthread_should_stop()) {
		ktime_t timestamp = sfb->vsync_info.timestamp;
		int ret = wait_event_interruptible(sfb->vsync_info.wait,
			!ktime_equal(timestamp, sfb->vsync_info.timestamp) &&
			sfb->vsync_info.active);

		if (!ret) {
			exynos5_ppmu_trace();
			sysfs_notify(&sfb->dev->kobj, NULL, "vsync");
		}
	}

	return 0;
}

/*------------------------------------------------------------------ */

static ssize_t s3c_fb_vsync_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct s3c_fb *sfb = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%llu\n",
			ktime_to_ns(sfb->vsync_info.timestamp));
}

static DEVICE_ATTR(vsync, S_IRUGO, s3c_fb_vsync_show, NULL);

#ifdef CONFIG_ION_EXYNOS
int s3c_fb_sysmmu_fault_handler(struct device *dev, const char *mmuname,
		enum exynos_sysmmu_inttype itype, unsigned long pgtable_base,
		unsigned long fault_addr)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s3c_fb *sfb = platform_get_drvdata(pdev);

	pr_err("FIMD1 PAGE FAULT occurred at 0x%lx (Page table base: 0x%lx)\n",
			fault_addr, pgtable_base);

	s3c_fb_dump_registers(sfb);

	pr_err("Generating Kernel OOPS... because it is unrecoverable.\n");

	BUG();

	return 0;
}

static int __devinit s3c_fb_copy_bootloader_fb(struct platform_device *pdev,
		struct dma_buf *dest_buf)
{
	struct resource *res;
	int ret = 0;
	size_t i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res || !res->start || !resource_size(res)) {
		dev_warn(&pdev->dev, "failed to find bootloader framebuffer\n");
		return -ENOENT;
	}

	ret = dma_buf_begin_cpu_access(dest_buf, 0, resource_size(res),
			DMA_TO_DEVICE);
	if (ret < 0) {
		dev_warn(&pdev->dev, "dma_buf_begin_cpu_access() failed on bootloader framebuffer: %u\n",
				ret);
		goto err;
	}
	for (i = 0; i < resource_size(res); i += PAGE_SIZE) {
		void *page = phys_to_page(res->start + i);
		void *from_virt = kmap(page);
		void *to_virt = dma_buf_kmap(dest_buf, i / PAGE_SIZE);
		memcpy(to_virt, from_virt, PAGE_SIZE);
		kunmap(page);
		dma_buf_kunmap(dest_buf, i / PAGE_SIZE, to_virt);
	}

	dma_buf_end_cpu_access(dest_buf, 0, resource_size(res), DMA_TO_DEVICE);

err:
	if (memblock_free(res->start, resource_size(res)))
		dev_warn(&pdev->dev, "failed to free bootloader framebuffer memblock\n");

	return ret;
}

static int __devinit s3c_fb_clear_fb(struct s3c_fb *sfb,
		struct dma_buf *dest_buf, size_t size)
{
	size_t i;

	int ret = dma_buf_begin_cpu_access(dest_buf, 0, dest_buf->size,
			DMA_TO_DEVICE);
	if (ret < 0) {
		dev_warn(sfb->dev, "dma_buf_begin_cpu_access() failed while clearing framebuffer: %u\n",
				ret);
		return ret;
	}

	for (i = 0; i < dest_buf->size / PAGE_SIZE; i++) {
		void *to_virt = dma_buf_kmap(dest_buf, i);
		memset(to_virt, 0, PAGE_SIZE);
		dma_buf_kunmap(dest_buf, i, to_virt);
	}

	dma_buf_end_cpu_access(dest_buf, 0, size, DMA_TO_DEVICE);
	return 0;
}
#endif

#ifdef CONFIG_DEBUG_FS

static int s3c_fb_debugfs_show(struct seq_file *f, void *offset)
{
	struct s3c_fb *sfb = f->private;
	struct s3c_fb_debug *debug_data = kzalloc(sizeof(struct s3c_fb_debug),
			GFP_KERNEL);

	if (!debug_data) {
		seq_printf(f, "kmalloc() failed; can't generate file\n");
		return -ENOMEM;
	}

	spin_lock(&sfb->slock);
	memcpy(debug_data, &sfb->debug_data, sizeof(sfb->debug_data));
	spin_unlock(&sfb->slock);

	seq_printf(f, "%u FIFO underflows\n", debug_data->num_timestamps);
	if (debug_data->num_timestamps) {
		unsigned int i;
		unsigned int temp = S3C_FB_DEBUG_FIFO_TIMESTAMPS;
		unsigned int n = min(debug_data->num_timestamps, temp);

		seq_printf(f, "Last %u FIFO underflow timestamps:\n", n);
		for (i = 0; i < n; i++) {
			unsigned int idx = (debug_data->first_timestamp + i) %
					S3C_FB_DEBUG_FIFO_TIMESTAMPS;
			ktime_t timestamp = debug_data->fifo_timestamps[idx];
			seq_printf(f, "\t%lld\n", ktime_to_ns(timestamp));
		}

		seq_printf(f, "Registers at time of last underflow:\n");
		for (i = 0; i < S3C_FB_DEBUG_REGS_SIZE; i += 32) {
			unsigned char buf[128];
			hex_dump_to_buffer(debug_data->regs_at_underflow + i,
					32, 32, 4, buf,
					sizeof(buf), false);
			seq_printf(f, "%.8x: %s\n", i, buf);
		}
	}

	kfree(debug_data);
	return 0;
}

static int s3c_fb_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, s3c_fb_debugfs_show, inode->i_private);
}

static const struct file_operations s3c_fb_debugfs_fops = {
	.owner		= THIS_MODULE,
	.open		= s3c_fb_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int s3c_fb_debugfs_init(struct s3c_fb *sfb)
{
	sfb->debug_dentry = debugfs_create_file("s3c-fb", S_IRUGO, NULL, sfb,
			&s3c_fb_debugfs_fops);
	if (IS_ERR_OR_NULL(sfb->debug_dentry)) {
		sfb->debug_dentry = NULL;
		dev_err(sfb->dev, "debugfs_create_file() failed\n");
		return -EIO;
	}

	return 0;
}

static void s3c_fb_debugfs_cleanup(struct s3c_fb *sfb)
{
	if (sfb->debug_dentry)
		debugfs_remove(sfb->debug_dentry);
}

#else

static int s3c_fb_debugfs_init(struct s3c_fb *sfb) { return 0; }
static void s3c_fb_debugfs_cleanup(struct s3c_fb *sfb) { }

#endif
static int s3c_fb_inquire_version(struct s3c_fb *sfb)
{
	struct s3c_fb_platdata *pd = sfb->pdata;

	return pd->ip_version == EXYNOS5_813 ? 0 : 1;
}

static int __devinit s3c_fb_probe(struct platform_device *pdev)
{
	const struct platform_device_id *platid;
	struct s3c_fb_driverdata *fbdrv;
	struct device *dev = &pdev->dev;
	struct s3c_fb_platdata *pd;
	struct s3c_fb *sfb;
	struct resource *res;
	struct fb_info *fbinfo;
	int win;
	int default_win;
	int i;
	int ret = 0;
	u32 reg;

#if defined(CONFIG_MACH_ODROIDXU)
    if(!strncmp(VoutBootArgs, "dp", sizeof("dp")))  DP_Mode = true;
#endif        
	
	platid = platform_get_device_id(pdev);
	fbdrv = (struct s3c_fb_driverdata *)platid->driver_data;

	if (fbdrv->variant.nr_windows > S3C_FB_MAX_WIN) {
		dev_err(dev, "too many windows, cannot attach\n");
		return -EINVAL;
	}

	pd = pdev->dev.platform_data;
	if (!pd) {
		dev_err(dev, "no platform data specified\n");
		return -EINVAL;
	}

	sfb = devm_kzalloc(dev, sizeof(struct s3c_fb), GFP_KERNEL);
	if (!sfb) {
		dev_err(dev, "no memory for framebuffers\n");
		return -ENOMEM;
	}

	dev_dbg(dev, "allocate new framebuffer %p\n", sfb);

	sfb->dev = dev;
	sfb->pdata = pd;
	sfb->variant = fbdrv->variant;

	spin_lock_init(&sfb->slock);
	mutex_init(&sfb->output_lock);

	ret = s3c_fb_debugfs_init(sfb);
	if (ret)
		dev_warn(dev, "failed to initialize debugfs entry\n");

#ifdef CONFIG_ION_EXYNOS
	INIT_LIST_HEAD(&sfb->update_regs_list);
	mutex_init(&sfb->update_regs_list_lock);
	init_kthread_worker(&sfb->update_regs_worker);

	sfb->update_regs_thread = kthread_run(kthread_worker_fn,
			&sfb->update_regs_worker, "s3c-fb");
	if (IS_ERR(sfb->update_regs_thread)) {
		int err = PTR_ERR(sfb->update_regs_thread);
		sfb->update_regs_thread = NULL;

		dev_err(dev, "failed to run update_regs thread\n");
		return err;
	}
	init_kthread_work(&sfb->update_regs_work, s3c_fb_update_regs_handler);
	sfb->timeline = sw_sync_timeline_create("s3c-fb");
	sfb->timeline_max = 1;
	/* XXX need to cleanup on errors */
#endif
	pm_runtime_enable(sfb->dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to find registers\n");
		ret = -ENOENT;
		goto err_lcd_clk;
	}

	sfb->regs = devm_request_and_ioremap(dev, res);
	if (!sfb->regs) {
		dev_err(dev, "failed to map registers\n");
		ret = -ENXIO;
		goto err_lcd_clk;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(dev, "failed to acquire irq resource\n");
		ret = -ENOENT;
		goto err_lcd_clk;
	}
	sfb->irq_no = res->start;
	ret = devm_request_irq(dev, sfb->irq_no, s3c_fb_irq,
			  0, "s3c_fb", sfb);
	if (ret) {
		dev_err(dev, "irq request failed\n");
		goto err_lcd_clk;
	}

	sfb->bus_clk = clk_get(dev, "lcd");
	if (IS_ERR(sfb->bus_clk)) {
		dev_err(dev, "failed to get bus clock\n");
		ret = PTR_ERR(sfb->bus_clk);
		goto err_sfb;
	}
	clk_enable(sfb->bus_clk);

	if (!s3c_fb_inquire_version(sfb)) {
		sfb->axi_disp1 = clk_get(dev, "axi_disp1");
		if (IS_ERR(sfb->axi_disp1)) {
			dev_err(dev, "failed to get axi bus clock\n");
			ret = PTR_ERR(sfb->axi_disp1);
			goto err_bus_clk;
		}
		clk_enable(sfb->axi_disp1);
	}

	if (!sfb->variant.has_clksel) {
		sfb->lcd_clk = clk_get(dev, "sclk_fimd");
		if (IS_ERR(sfb->lcd_clk)) {
			dev_err(dev, "failed to get lcd clock\n");
			ret = PTR_ERR(sfb->lcd_clk);
			goto err_axi_clk;
		}
		clk_enable(sfb->lcd_clk);
	}

	dev_dbg(dev, "got resources (regs %p), probing windows\n", sfb->regs);

	platform_set_drvdata(pdev, sfb);
	pm_runtime_get_sync(sfb->dev);

	/* setup gpio and output polarity controls */

	pd->setup_gpio();

	sfb->power_state = POWER_ON;

	writel(pd->vidcon1, sfb->regs + VIDCON1);

	/* set video clock running at under-run */
	if (sfb->variant.has_fixvclk) {
		reg = readl(sfb->regs + VIDCON1);
		reg &= ~VIDCON1_VCLK_MASK;
		reg |= VIDCON1_VCLK_HOLD;
		writel(reg, sfb->regs + VIDCON1);
	}

	/* zero all windows before we do anything */
	for (win = 0; win < fbdrv->variant.nr_windows; win++)
		if (win != pd->default_win)
			s3c_fb_clear_win(sfb, win);

	/* initialise colour key controls */
	for (win = 0; win < (fbdrv->variant.nr_windows - 1); win++) {
		void __iomem *regs = sfb->regs + sfb->variant.keycon;

		regs += (win * 8);
		writel(0xffffff, regs + WKEYCON0);
		writel(0xffffff, regs + WKEYCON1);
	}
#ifdef CONFIG_ION_EXYNOS
	sfb->fb_ion_client = ion_client_create(ion_exynos,
#if !defined(CONFIG_FB_EXYNOS_FIMD_SYSMMU_DISABLE)
			ION_HEAP_EXYNOS_MASK,
#else
			ION_HEAP_EXYNOS_CONTIG_MASK,
#endif
			"fimd");
	if (IS_ERR(sfb->fb_ion_client)) {
		dev_err(sfb->dev, "failed to ion_client_create\n");
		goto err_pm_runtime;
	}

	/* setup vmm */
#ifdef CONFIG_ARCH_EXYNOS4
	exynos_sysmmu_set_fault_handler(&s5p_device_fimd0.dev,
			s3c_fb_sysmmu_fault_handler);
#else
	exynos_sysmmu_set_fault_handler(&s5p_device_fimd1.dev,
			s3c_fb_sysmmu_fault_handler);
#endif
#endif

	default_win = sfb->pdata->default_win;
	for (win = 0; win < fbdrv->variant.nr_windows; win++) {
		if (!pd->win[win])
			continue;

		if (!pd->win[win]->win_mode.pixclock)
			s3c_fb_missing_pixclock(&pd->win[win]->win_mode);
	}

	/* use platform specified window as the basis for the lcd timings */
	s3c_fb_configure_lcd(sfb, &pd->win[default_win]->win_mode);
#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	pm_qos_add_request(&exynos5_mif_qos, PM_QOS_BUS_THROUGHPUT, 200000);
	pm_qos_add_request(&exynos5_int_qos, PM_QOS_DEVICE_THROUGHPUT, 0);
#endif
	/* we have the register setup, start allocating framebuffers */
	for (i = 0; i < fbdrv->variant.nr_windows; i++) {
		win = i;
		if (i == 0)
			win = default_win;
		if (i == default_win)
			win = 0;

		ret = s3c_fb_probe_win(sfb, win, fbdrv->win[win],
				       &sfb->windows[win]);
		if (ret < 0) {
			dev_err(dev, "failed to create window %d\n", win);
			for (; win >= 0; win--)
				s3c_fb_release_win(sfb, sfb->windows[win]);
			goto err_pm_runtime;
		}

#ifdef CONFIG_FB_EXYNOS_FIMD_MC
		/* local path is only supported for windows 0-2 */
		if (win > 2)
			continue;

		/* register a window subdev as entity */
		ret = s3c_fb_register_mc_components(sfb->windows[win]);
		if (ret) {
			dev_err(sfb->dev, "failed to register s3c_fb mc entities\n");
			goto err_mc_entity_create_fail;
		}

		/* create links connected between gscaler and fimd */
		ret = s3c_fb_create_mc_links(sfb->windows[win]);
		if (ret) {
			dev_err(sfb->dev, "failed to create s3c_fb mc links\n");
			goto err_mc_link_create_fail;
		}
#endif
	}

#ifdef CONFIG_FB_EXYNOS_FIMD_MC
	ret = s3c_fb_register_mc_subdev_nodes(sfb);
	if (ret) {
			dev_err(sfb->dev, "failed to register s3c_fb mc subdev node\n");
			goto err_mc_link_create_fail;
	}
#endif

#ifdef CONFIG_FB_EXYNOS_FIMD_MC_WB
	/* register a window subdev as entity */
	ret = s3c_fb_register_mc_wb_components(sfb);
	if (ret) {
		dev_err(sfb->dev, "failed to register s3c_fb mc entities\n");
		goto err_mc_wb_entity_create_fail;
	}

	/* create links connected between gscaler and fimd */
	ret = s3c_fb_create_mc_wb_links(sfb);
	if (ret) {
		dev_err(sfb->dev, "failed to create s3c_fb mc links\n");
		goto err_mc_wb_link_create_fail;
	}

	ret = s3c_fb_register_mc_subdev_wb_nodes(sfb);
	if (ret) {
			dev_err(sfb->dev, "failed to register s3c_fb mc subdev node\n");
			goto err_mc_wb_link_create_fail;
	}
#endif

#if defined(CONFIG_MACH_ODROIDXU)
    if(DP_Mode)     writel(DPCLKCON_ENABLE, sfb->regs + DPCLKCON);
#else
    #if defined(CONFIG_S5P_DP)
        writel(DPCLKCON_ENABLE, sfb->regs + DPCLKCON);
    #endif        
#endif
	platform_set_drvdata(pdev, sfb);

	mutex_init(&sfb->vsync_info.irq_lock);

	ret = device_create_file(sfb->dev, &dev_attr_vsync);
	if (ret) {
		dev_err(sfb->dev, "failed to create vsync file\n");
		goto err_create_file;
	}

	sfb->vsync_info.thread = kthread_run(s3c_fb_wait_for_vsync_thread,
			sfb, "s3c-fb-vsync");
	if (sfb->vsync_info.thread == ERR_PTR(-ENOMEM)) {
		dev_err(sfb->dev, "failed to run vsync thread\n");
		sfb->vsync_info.thread = NULL;
	}

#ifdef CONFIG_ION_EXYNOS
#if !defined(CONFIG_FB_EXYNOS_FIMD_SYSMMU_DISABLE)
	ret = s3c_fb_copy_bootloader_fb(pdev,
			sfb->windows[default_win]->dma_buf_data.dma_buf);
	if (ret < 0) {
		struct s3c_fb_win *win = sfb->windows[default_win];
		dev_warn(sfb->dev, "couldn't copy bootloader framebuffer into default window; clearing instead\n");
		s3c_fb_clear_fb(sfb, win->dma_buf_data.dma_buf,
				PAGE_ALIGN(win->fbinfo->fix.smem_len));
	}
#endif
#endif

	s3c_fb_set_par(sfb->windows[default_win]->fbinfo);

#ifdef CONFIG_ION_EXYNOS
	s3c_fb_wait_for_vsync(sfb, 0);
#ifdef CONFIG_ARCH_EXYNOS4
	ret = iovmm_activate(&s5p_device_fimd0.dev);
#else
#if !defined(CONFIG_FB_EXYNOS_FIMD_SYSMMU_DISABLE)
	ret = iovmm_activate(&s5p_device_fimd1.dev);
#else
	iovmm_deactivate(&s5p_device_fimd1.dev);
#endif
#endif
	if (ret < 0) {
		dev_err(sfb->dev, "failed to activate vmm\n");
		goto err_iovmm;
	}
#endif

	s3c_fb_activate_window_dma(sfb, default_win);
	s3c_fb_activate_window(sfb, default_win);
	sfb->output_on = true;

	dev_dbg(sfb->dev, "about to register framebuffer\n");

	/* run the check_var and set_par on our configuration. */

	fbinfo = sfb->windows[default_win]->fbinfo;
	ret = register_framebuffer(fbinfo);
	if (ret < 0) {
		dev_err(sfb->dev, "failed to register framebuffer\n");
		goto err_fb;
	}

	dev_info(sfb->dev, "window %d: fb %s\n", default_win, fbinfo->fix.id);

	bts_initialize("pd-disp1", true);

	return 0;

err_fb:
#ifdef CONFIG_ARCH_EXYNOS4
	iovmm_deactivate(&s5p_device_fimd0.dev);
#else
#if !defined(CONFIG_FB_EXYNOS_FIMD_SYSMMU_DISABLE)
	iovmm_deactivate(&s5p_device_fimd1.dev);
#endif
#endif

err_iovmm:
	device_remove_file(sfb->dev, &dev_attr_vsync);

err_create_file:
#ifdef CONFIG_FB_EXYNOS_FIMD_MC_WB
err_mc_wb_entity_create_fail:
err_mc_wb_link_create_fail:
	s3c_fb_unregister_mc_wb_entities(sfb);
#endif

#ifdef CONFIG_FB_EXYNOS_FIMD_MC
err_mc_entity_create_fail:
err_mc_link_create_fail:
	s3c_fb_unregister_mc_entities(sfb->windows[win]);
#endif

err_pm_runtime:
	pm_runtime_put_sync(sfb->dev);

err_lcd_clk:
	pm_runtime_disable(sfb->dev);

	if (!sfb->variant.has_clksel) {
		clk_disable(sfb->lcd_clk);
		clk_put(sfb->lcd_clk);
	}

err_axi_clk:
	if (!s3c_fb_inquire_version(sfb)) {
		clk_disable(sfb->axi_disp1);
		clk_put(sfb->axi_disp1);
	}
err_bus_clk:
	clk_disable(sfb->bus_clk);
	clk_put(sfb->bus_clk);

err_sfb:
#ifdef CONFIG_ION_EXYNOS
	kthread_stop(sfb->update_regs_thread);
#endif
	return ret;
}

/**
 * s3c_fb_remove() - Cleanup on module finalisation
 * @pdev: The platform device we are bound to.
 *
 * Shutdown and then release all the resources that the driver allocated
 * on initialisation.
 */
static int __devexit s3c_fb_remove(struct platform_device *pdev)
{
	struct s3c_fb *sfb = platform_get_drvdata(pdev);
	int win;

	pm_runtime_get_sync(sfb->dev);

	unregister_framebuffer(sfb->windows[sfb->pdata->default_win]->fbinfo);

	if (sfb->update_regs_thread)
		kthread_stop(sfb->update_regs_thread);

	for (win = 0; win < S3C_FB_MAX_WIN; win++)
		if (sfb->windows[win])
			s3c_fb_release_win(sfb, sfb->windows[win]);

	if (sfb->vsync_info.thread)
		kthread_stop(sfb->vsync_info.thread);

	device_remove_file(sfb->dev, &dev_attr_vsync);

	if (!sfb->variant.has_clksel) {
		clk_disable(sfb->lcd_clk);
		clk_put(sfb->lcd_clk);
	}

	if (!s3c_fb_inquire_version(sfb))
		clk_disable(sfb->axi_disp1);

	clk_disable(sfb->bus_clk);

	if (!s3c_fb_inquire_version(sfb))
		clk_put(sfb->axi_disp1);

	clk_put(sfb->bus_clk);

	s3c_fb_debugfs_cleanup(sfb);

	pm_runtime_put_sync(sfb->dev);
	pm_runtime_disable(sfb->dev);

	return 0;
}

static int s3c_fb_disable(struct s3c_fb *sfb)
{
	u32 vidcon0;
	int ret = 0;

	mutex_lock(&sfb->output_lock);

	if (!sfb->output_on) {
		ret = -EBUSY;
		goto err;
	}

	if (sfb->pdata->backlight_off)
		sfb->pdata->backlight_off();

	if (sfb->pdata->lcd_off)
		sfb->pdata->lcd_off();

	flush_kthread_worker(&sfb->update_regs_worker);

	vidcon0 = readl(sfb->regs + VIDCON0);

	/* see the note in the framebuffer datasheet about
	 * why you cannot take both of these bits down at the
	 * same time. */

	if (vidcon0 & VIDCON0_ENVID) {
		vidcon0 |= VIDCON0_ENVID;
		vidcon0 &= ~VIDCON0_ENVID_F;
		writel(vidcon0, sfb->regs + VIDCON0);
	} else
		dev_warn(sfb->dev, "ENVID not set while disabling fb");

	sfb->power_state = POWER_DOWN;

	if (!sfb->variant.has_clksel)
		clk_disable(sfb->lcd_clk);

	if (!s3c_fb_inquire_version(sfb))
		clk_disable(sfb->axi_disp1);

	clk_disable(sfb->bus_clk);
#ifdef CONFIG_ION_EXYNOS
#if !defined(CONFIG_FB_EXYNOS_FIMD_SYSMMU_DISABLE)
#ifdef CONFIG_ARCH_EXYNOS4
	iovmm_deactivate(&s5p_device_fimd0.dev);
#else
	iovmm_deactivate(&s5p_device_fimd1.dev);
#endif
#endif
#endif

	pm_runtime_put_sync(sfb->dev);
	sfb->output_on = false;

err:
	mutex_unlock(&sfb->output_lock);
	return ret;
}

/**
 * s3c_fb_enable() - Enable the main LCD output
 * @sfb: The main framebuffer state.
 */
static int s3c_fb_enable(struct s3c_fb *sfb)
{
	struct s3c_fb_platdata *pd = sfb->pdata;
	int win_no;
	int default_win;
	int ret = 0;
	u32 reg;

	mutex_lock(&sfb->output_lock);
	if (sfb->output_on) {
		ret = -EBUSY;
		goto err;
	}

	pm_runtime_get_sync(sfb->dev);
	clk_enable(sfb->bus_clk);

	if (!s3c_fb_inquire_version(sfb))
		clk_enable(sfb->axi_disp1);

	if (!sfb->variant.has_clksel)
		clk_enable(sfb->lcd_clk);

	/* setup gpio and output polarity controls */
	pd->setup_gpio();

	sfb->power_state = POWER_ON;

	writel(pd->vidcon1, sfb->regs + VIDCON1);
	writel(REG_CLKGATE_MODE_NON_CLOCK_GATE,
			sfb->regs + REG_CLKGATE_MODE);

	/* set video clock running at under-run */
	if (sfb->variant.has_fixvclk) {
		reg = readl(sfb->regs + VIDCON1);
		reg &= ~VIDCON1_VCLK_MASK;
		reg |= VIDCON1_VCLK_HOLD;
		writel(reg, sfb->regs + VIDCON1);
	}

	/* zero all windows before we do anything */
	for (win_no = 0; win_no < sfb->variant.nr_windows; win_no++)
		s3c_fb_clear_win(sfb, win_no);

	/* use platform specified window as the basis for the lcd timings */
	default_win = sfb->pdata->default_win;
	s3c_fb_configure_lcd(sfb, &pd->win[default_win]->win_mode);

	mutex_lock(&sfb->vsync_info.irq_lock);
	if (sfb->vsync_info.irq_refcount)
		s3c_fb_enable_irq(sfb);
	mutex_unlock(&sfb->vsync_info.irq_lock);

#ifdef CONFIG_ION_EXYNOS
#if !defined(CONFIG_FB_EXYNOS_FIMD_SYSMMU_DISABLE)
#ifdef CONFIG_ARCH_EXYNOS4
	ret = iovmm_activate(&s5p_device_fimd0.dev);
#else
	ret = iovmm_activate(&s5p_device_fimd1.dev);
#endif
	if (ret < 0) {
		dev_err(sfb->dev, "failed to reactivate vmm\n");
		goto err;
	}
#endif
#endif

#if defined(CONFIG_MACH_ODROIDXU)    
    if(DP_Mode) writel(DPCLKCON_ENABLE, sfb->regs + DPCLKCON);
#else
    #if defined(CONFIG_S5P_DP)
	    writel(DPCLKCON_ENABLE, sfb->regs + DPCLKCON);
	#endif
#endif	

	reg = readl(sfb->regs + VIDCON0);
	reg |= VIDCON0_ENVID | VIDCON0_ENVID_F;
	writel(reg, sfb->regs + VIDCON0);

	sfb->output_on = true;

err:
	mutex_unlock(&sfb->output_lock);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int s3c_fb_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s3c_fb *sfb = platform_get_drvdata(pdev);
	int ret = 0;
	u32 vidcon0;

	mutex_lock(&sfb->output_lock);
	sfb->power_state = POWER_DOWN;
	if (sfb->output_on) {
		if (sfb->pdata->backlight_off)
			sfb->pdata->backlight_off();

		if (sfb->pdata->lcd_off)
			sfb->pdata->lcd_off();

		flush_kthread_worker(&sfb->update_regs_worker);

		vidcon0 = readl(sfb->regs + VIDCON0);

		/* see the note in the framebuffer datasheet about
		 * why you cannot take both of these bits down at the
		 * same time.
		*/

		if (vidcon0 & VIDCON0_ENVID) {
			vidcon0 |= VIDCON0_ENVID;
			vidcon0 &= ~VIDCON0_ENVID_F;
			writel(vidcon0, sfb->regs + VIDCON0);
		} else
			dev_warn(sfb->dev, "ENVID not set while disabling fb");

		if (!sfb->variant.has_clksel)
			clk_disable(sfb->lcd_clk);

		if (!s3c_fb_inquire_version(sfb))
			clk_disable(sfb->axi_disp1);

		clk_disable(sfb->bus_clk);
#ifdef CONFIG_ION_EXYNOS
#ifdef CONFIG_ARCH_EXYNOS4
		iovmm_deactivate(&s5p_device_fimd0.dev);
#else
#if !defined(CONFIG_FB_EXYNOS_FIMD_SYSMMU_DISABLE)
		iovmm_deactivate(&s5p_device_fimd1.dev);
#endif
#endif
#endif
		pm_runtime_put_sync(sfb->dev);
		sfb->output_on = false;
	}
	mutex_unlock(&sfb->output_lock);

	return ret;
}

static int s3c_fb_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s3c_fb *sfb = platform_get_drvdata(pdev);
	struct s3c_fb_platdata *pd = sfb->pdata;

	int win_no;
	int default_win;
	int ret = 0;
	u32 reg;

	mutex_lock(&sfb->output_lock);
	if (sfb->output_on) {
		ret = -EBUSY;
		goto err;
	}
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_get_sync(sfb->dev);
#elif !defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	if (!sfb->fb_mif_handle) {
		sfb->fb_mif_handle = exynos5_bus_mif_min(300000);
		if (!sfb->fb_mif_handle)
			dev_err(sfb->dev, "failed to request min_freq for mif\n");
	}

	if (!sfb->fb_int_handle) {
		sfb->fb_int_handle = exynos5_bus_int_min(160000);
		if (!sfb->fb_int_handle)
			dev_err(sfb->dev, "failed to request min_freq for int\n");
	}
#endif
	clk_enable(sfb->bus_clk);

	if (!s3c_fb_inquire_version(sfb))
		clk_enable(sfb->axi_disp1);

	if (!sfb->variant.has_clksel)
		clk_enable(sfb->lcd_clk);

	/* setup gpio and output polarity controls */
	pd->setup_gpio();

	sfb->power_state = POWER_ON;

	writel(pd->vidcon1, sfb->regs + VIDCON1);
	writel(REG_CLKGATE_MODE_NON_CLOCK_GATE,
			sfb->regs + REG_CLKGATE_MODE);

	/* set video clock running at under-run */
	if (sfb->variant.has_fixvclk) {
		reg = readl(sfb->regs + VIDCON1);
		reg &= ~VIDCON1_VCLK_MASK;
		reg |= VIDCON1_VCLK_HOLD;
		writel(reg, sfb->regs + VIDCON1);
	}

	/* zero all windows before we do anything */
	for (win_no = 0; win_no < sfb->variant.nr_windows; win_no++)
		s3c_fb_clear_win(sfb, win_no);

	/* use platform specified window as the basis for the lcd timings */
	default_win = sfb->pdata->default_win;
	s3c_fb_configure_lcd(sfb, &pd->win[default_win]->win_mode);

	mutex_lock(&sfb->vsync_info.irq_lock);
	if (sfb->vsync_info.irq_refcount)
		s3c_fb_enable_irq(sfb);
	mutex_unlock(&sfb->vsync_info.irq_lock);

#ifdef CONFIG_ION_EXYNOS
#ifdef CONFIG_ARCH_EXYNOS4
	ret = iovmm_activate(&s5p_device_fimd0.dev);
#else
#if !defined(CONFIG_FB_EXYNOS_FIMD_SYSMMU_DISABLE)
	ret = iovmm_activate(&s5p_device_fimd1.dev);
#endif
#endif
	if (ret < 0) {
		dev_err(sfb->dev, "failed to reactivate vmm\n");
		goto err;
	}
#endif

#if defined(CONFIG_MACH_ODROIDXU)
    if(DP_Mode) writel(DPCLKCON_ENABLE, sfb->regs + DPCLKCON);
#else        
    #if defined(CONFIG_S5P_DP)
    	writel(DPCLKCON_ENABLE, sfb->regs + DPCLKCON);
    #endif
#endif    

	reg = readl(sfb->regs + VIDCON0);
	reg |= VIDCON0_ENVID | VIDCON0_ENVID_F;
	writel(reg, sfb->regs + VIDCON0);

	sfb->output_on = true;

err:
	mutex_unlock(&sfb->output_lock);
	return ret;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int s3c_fb_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s3c_fb *sfb = platform_get_drvdata(pdev);

	sfb->power_state = POWER_DOWN;

	if (!sfb->variant.has_clksel)
		clk_disable(sfb->lcd_clk);

	if (!s3c_fb_inquire_version(sfb))
		clk_disable(sfb->axi_disp1);

	clk_disable(sfb->bus_clk);

	return 0;
}

static int s3c_fb_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s3c_fb *sfb = platform_get_drvdata(pdev);
	struct s3c_fb_platdata *pd = sfb->pdata;

#if !defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	if (!sfb->fb_mif_handle) {
		sfb->fb_mif_handle = exynos5_bus_mif_min(300000);
		if (!sfb->fb_mif_handle)
			dev_err(sfb->dev, "failed to request min_freq for mif\n");
	}

	if (!sfb->fb_int_handle) {
		sfb->fb_int_handle = exynos5_bus_int_min(160000);
		if (!sfb->fb_int_handle)
			dev_err(sfb->dev, "failed to request min_freq for int\n");
	}
#endif
	clk_enable(sfb->bus_clk);

	if (!s3c_fb_inquire_version(sfb))
		clk_enable(sfb->axi_disp1);

	if (!sfb->variant.has_clksel)
		clk_enable(sfb->lcd_clk);

	/* setup gpio and output polarity controls */
	pd->setup_gpio();

	sfb->power_state = POWER_ON;

	writel(pd->vidcon1, sfb->regs + VIDCON1);

	return 0;
}
#endif

#define VALID_BPP124 (VALID_BPP(1) | VALID_BPP(2) | VALID_BPP(4))
#define VALID_BPP1248 (VALID_BPP124 | VALID_BPP(8))

static struct s3c_fb_win_variant s3c_fb_data_64xx_wins[] = {
	[0] = {
		.has_osd_c	= 1,
		.osd_size_off	= 0x8,
		.palette_sz	= 256,
		.valid_bpp	= (VALID_BPP1248 | VALID_BPP(16) |
				   VALID_BPP(18) | VALID_BPP(24)),
	},
	[1] = {
		.has_osd_c	= 1,
		.has_osd_d	= 1,
		.osd_size_off	= 0xc,
		.has_osd_alpha	= 1,
		.palette_sz	= 256,
		.valid_bpp	= (VALID_BPP1248 | VALID_BPP(16) |
				   VALID_BPP(18) | VALID_BPP(19) |
				   VALID_BPP(24) | VALID_BPP(25) |
				   VALID_BPP(28)),
	},
	[2] = {
		.has_osd_c	= 1,
		.has_osd_d	= 1,
		.osd_size_off	= 0xc,
		.has_osd_alpha	= 1,
		.palette_sz	= 16,
		.palette_16bpp	= 1,
		.valid_bpp	= (VALID_BPP1248 | VALID_BPP(16) |
				   VALID_BPP(18) | VALID_BPP(19) |
				   VALID_BPP(24) | VALID_BPP(25) |
				   VALID_BPP(28)),
	},
	[3] = {
		.has_osd_c	= 1,
		.has_osd_alpha	= 1,
		.palette_sz	= 16,
		.palette_16bpp	= 1,
		.valid_bpp	= (VALID_BPP124  | VALID_BPP(16) |
				   VALID_BPP(18) | VALID_BPP(19) |
				   VALID_BPP(24) | VALID_BPP(25) |
				   VALID_BPP(28)),
	},
	[4] = {
		.has_osd_c	= 1,
		.has_osd_alpha	= 1,
		.palette_sz	= 4,
		.palette_16bpp	= 1,
		.valid_bpp	= (VALID_BPP(1) | VALID_BPP(2) |
				   VALID_BPP(16) | VALID_BPP(18) |
				   VALID_BPP(19) | VALID_BPP(24) |
				   VALID_BPP(25) | VALID_BPP(28)),
	},
};

static struct s3c_fb_win_variant s3c_fb_data_s5p_wins[] = {
	[0] = {
		.has_osd_c	= 1,
		.osd_size_off	= 0x8,
		.palette_sz	= 256,
		.valid_bpp	= (VALID_BPP1248 | VALID_BPP(13) |
				   VALID_BPP(15) | VALID_BPP(16) |
				   VALID_BPP(18) | VALID_BPP(19) |
				   VALID_BPP(24) | VALID_BPP(25) |
				   VALID_BPP(32)),
	},
	[1] = {
		.has_osd_c	= 1,
		.has_osd_d	= 1,
		.osd_size_off	= 0xc,
		.has_osd_alpha	= 1,
		.palette_sz	= 256,
		.valid_bpp	= (VALID_BPP1248 | VALID_BPP(13) |
				   VALID_BPP(15) | VALID_BPP(16) |
				   VALID_BPP(18) | VALID_BPP(19) |
				   VALID_BPP(24) | VALID_BPP(25) |
				   VALID_BPP(32)),
	},
	[2] = {
		.has_osd_c	= 1,
		.has_osd_d	= 1,
		.osd_size_off	= 0xc,
		.has_osd_alpha	= 1,
		.palette_sz	= 256,
		.valid_bpp	= (VALID_BPP1248 | VALID_BPP(13) |
				   VALID_BPP(15) | VALID_BPP(16) |
				   VALID_BPP(18) | VALID_BPP(19) |
				   VALID_BPP(24) | VALID_BPP(25) |
				   VALID_BPP(32)),
	},
	[3] = {
		.has_osd_c	= 1,
		.has_osd_alpha	= 1,
		.palette_sz	= 256,
		.valid_bpp	= (VALID_BPP1248 | VALID_BPP(13) |
				   VALID_BPP(15) | VALID_BPP(16) |
				   VALID_BPP(18) | VALID_BPP(19) |
				   VALID_BPP(24) | VALID_BPP(25) |
				   VALID_BPP(32)),
	},
	[4] = {
		.has_osd_c	= 1,
		.has_osd_alpha	= 1,
		.palette_sz	= 256,
		.valid_bpp	= (VALID_BPP1248 | VALID_BPP(13) |
				   VALID_BPP(15) | VALID_BPP(16) |
				   VALID_BPP(18) | VALID_BPP(19) |
				   VALID_BPP(24) | VALID_BPP(25) |
				   VALID_BPP(32)),
	},
};

static struct s3c_fb_driverdata s3c_fb_data_64xx = {
	.variant = {
		.nr_windows	= 5,
		.vidtcon	= VIDTCON0,
		.wincon		= WINCON(0),
		.winmap		= WINxMAP(0),
		.keycon		= WKEYCON,
		.osd		= VIDOSD_BASE,
		.osd_stride	= 16,
		.buf_start	= VIDW_BUF_START(0),
		.buf_size	= VIDW_BUF_SIZE(0),
		.buf_end	= VIDW_BUF_END(0),

		.palette = {
			[0] = 0x400,
			[1] = 0x800,
			[2] = 0x300,
			[3] = 0x320,
			[4] = 0x340,
		},

		.has_prtcon	= 1,
		.has_clksel	= 1,
	},
	.win[0]	= &s3c_fb_data_64xx_wins[0],
	.win[1]	= &s3c_fb_data_64xx_wins[1],
	.win[2]	= &s3c_fb_data_64xx_wins[2],
	.win[3]	= &s3c_fb_data_64xx_wins[3],
	.win[4]	= &s3c_fb_data_64xx_wins[4],
};

static struct s3c_fb_driverdata s3c_fb_data_s5pc100 = {
	.variant = {
		.nr_windows	= 5,
		.vidtcon	= VIDTCON0,
		.wincon		= WINCON(0),
		.winmap		= WINxMAP(0),
		.keycon		= WKEYCON,
		.osd		= VIDOSD_BASE,
		.osd_stride	= 16,
		.buf_start	= VIDW_BUF_START(0),
		.buf_size	= VIDW_BUF_SIZE(0),
		.buf_end	= VIDW_BUF_END(0),

		.palette = {
			[0] = 0x2400,
			[1] = 0x2800,
			[2] = 0x2c00,
			[3] = 0x3000,
			[4] = 0x3400,
		},

		.has_prtcon	= 1,
		.has_blendcon	= 1,
		.has_alphacon	= 1,
		.has_clksel	= 1,
	},
	.win[0]	= &s3c_fb_data_s5p_wins[0],
	.win[1]	= &s3c_fb_data_s5p_wins[1],
	.win[2]	= &s3c_fb_data_s5p_wins[2],
	.win[3]	= &s3c_fb_data_s5p_wins[3],
	.win[4]	= &s3c_fb_data_s5p_wins[4],
};

static struct s3c_fb_driverdata s3c_fb_data_s5pv210 = {
	.variant = {
		.nr_windows	= 5,
		.vidtcon	= VIDTCON0,
		.wincon		= WINCON(0),
		.winmap		= WINxMAP(0),
		.keycon		= WKEYCON,
		.osd		= VIDOSD_BASE,
		.osd_stride	= 16,
		.buf_start	= VIDW_BUF_START(0),
		.buf_size	= VIDW_BUF_SIZE(0),
		.buf_end	= VIDW_BUF_END(0),

		.palette = {
			[0] = 0x2400,
			[1] = 0x2800,
			[2] = 0x2c00,
			[3] = 0x3000,
			[4] = 0x3400,
		},

		.has_shadowcon	= 1,
		.has_blendcon	= 1,
		.has_alphacon	= 1,
		.has_clksel	= 1,
		.has_fixvclk	= 1,
	},
	.win[0]	= &s3c_fb_data_s5p_wins[0],
	.win[1]	= &s3c_fb_data_s5p_wins[1],
	.win[2]	= &s3c_fb_data_s5p_wins[2],
	.win[3]	= &s3c_fb_data_s5p_wins[3],
	.win[4]	= &s3c_fb_data_s5p_wins[4],
};

static struct s3c_fb_driverdata s3c_fb_data_exynos4 = {
	.variant = {
		.nr_windows	= 5,
		.vidtcon	= VIDTCON0,
		.wincon		= WINCON(0),
		.winmap		= WINxMAP(0),
		.keycon		= WKEYCON,
		.osd		= VIDOSD_BASE,
		.osd_stride	= 16,
		.buf_start	= VIDW_BUF_START(0),
		.buf_size	= VIDW_BUF_SIZE(0),
		.buf_end	= VIDW_BUF_END(0),

		.palette = {
			[0] = 0x2400,
			[1] = 0x2800,
			[2] = 0x2c00,
			[3] = 0x3000,
			[4] = 0x3400,
		},

		.has_shadowcon	= 1,
		.has_blendcon	= 1,
		.has_alphacon	= 1,
		.has_fixvclk	= 1,
	},
	.win[0]	= &s3c_fb_data_s5p_wins[0],
	.win[1]	= &s3c_fb_data_s5p_wins[1],
	.win[2]	= &s3c_fb_data_s5p_wins[2],
	.win[3]	= &s3c_fb_data_s5p_wins[3],
	.win[4]	= &s3c_fb_data_s5p_wins[4],
};

static struct s3c_fb_driverdata s3c_fb_data_exynos5 = {
	.variant = {
#if !defined(CONFIG_FB_EXYNOS_FIMD_SYSMMU_DISABLE)
		.nr_windows	= 5,
#else
		.nr_windows	= 2,
#endif
		.vidtcon	= VIDTCON0,
		.wincon		= WINCON(0),
		.winmap		= WINxMAP(0),
		.keycon		= WKEYCON,
		.osd		= VIDOSD_BASE,
		.osd_stride	= 16,
		.buf_start	= VIDW_BUF_START(0),
		.buf_size	= VIDW_BUF_SIZE(0),
		.buf_end	= VIDW_BUF_END(0),

		.palette = {
			[0] = 0x2400,
			[1] = 0x2800,
			[2] = 0x2c00,
			[3] = 0x3000,
			[4] = 0x3400,
		},
		.has_shadowcon	= 1,
		.has_blendcon	= 1,
		.has_alphacon	= 1,
		.has_fixvclk	= 1,
	},
	.win[0]	= &s3c_fb_data_s5p_wins[0],
	.win[1]	= &s3c_fb_data_s5p_wins[1],
	.win[2]	= &s3c_fb_data_s5p_wins[2],
	.win[3]	= &s3c_fb_data_s5p_wins[3],
	.win[4]	= &s3c_fb_data_s5p_wins[4],
};

/* S3C2443/S3C2416 style hardware */
static struct s3c_fb_driverdata s3c_fb_data_s3c2443 = {
	.variant = {
		.nr_windows	= 2,
		.is_2443	= 1,

		.vidtcon	= 0x08,
		.wincon		= 0x14,
		.winmap		= 0xd0,
		.keycon		= 0xb0,
		.osd		= 0x28,
		.osd_stride	= 12,
		.buf_start	= 0x64,
		.buf_size	= 0x94,
		.buf_end	= 0x7c,

		.palette = {
			[0] = 0x400,
			[1] = 0x800,
		},
		.has_clksel	= 1,
	},
	.win[0] = &(struct s3c_fb_win_variant) {
		.palette_sz	= 256,
		.valid_bpp	= VALID_BPP1248 | VALID_BPP(16) | VALID_BPP(24),
	},
	.win[1] = &(struct s3c_fb_win_variant) {
		.has_osd_c	= 1,
		.has_osd_alpha	= 1,
		.palette_sz	= 256,
		.valid_bpp	= (VALID_BPP1248 | VALID_BPP(16) |
				   VALID_BPP(18) | VALID_BPP(19) |
				   VALID_BPP(24) | VALID_BPP(25) |
				   VALID_BPP(28)),
	},
};

static struct s3c_fb_driverdata s3c_fb_data_s5p64x0 = {
	.variant = {
		.nr_windows	= 3,
		.vidtcon	= VIDTCON0,
		.wincon		= WINCON(0),
		.winmap		= WINxMAP(0),
		.keycon		= WKEYCON,
		.osd		= VIDOSD_BASE,
		.osd_stride	= 16,
		.buf_start	= VIDW_BUF_START(0),
		.buf_size	= VIDW_BUF_SIZE(0),
		.buf_end	= VIDW_BUF_END(0),

		.palette = {
			[0] = 0x2400,
			[1] = 0x2800,
			[2] = 0x2c00,
		},

		.has_blendcon	= 1,
		.has_fixvclk	= 1,
	},
	.win[0] = &s3c_fb_data_s5p_wins[0],
	.win[1] = &s3c_fb_data_s5p_wins[1],
	.win[2] = &s3c_fb_data_s5p_wins[2],
};

static struct platform_device_id s3c_fb_driver_ids[] = {
	{
		.name		= "s3c-fb",
		.driver_data	= (unsigned long)&s3c_fb_data_64xx,
	}, {
		.name		= "s5pc100-fb",
		.driver_data	= (unsigned long)&s3c_fb_data_s5pc100,
	}, {
		.name		= "s5pv210-fb",
		.driver_data	= (unsigned long)&s3c_fb_data_s5pv210,
	}, {
		.name		= "exynos4-fb",
		.driver_data	= (unsigned long)&s3c_fb_data_exynos4,
	}, {
		.name		= "exynos5-fb",
		.driver_data	= (unsigned long)&s3c_fb_data_exynos5,
	}, {
		.name		= "s3c2443-fb",
		.driver_data	= (unsigned long)&s3c_fb_data_s3c2443,
	}, {
		.name		= "s5p64x0-fb",
		.driver_data	= (unsigned long)&s3c_fb_data_s5p64x0,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, s3c_fb_driver_ids);

static const struct dev_pm_ops s3cfb_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(s3c_fb_suspend, s3c_fb_resume)
	SET_RUNTIME_PM_OPS(s3c_fb_runtime_suspend, s3c_fb_runtime_resume,
			   NULL)
};

static struct platform_driver s3c_fb_driver = {
	.probe		= s3c_fb_probe,
	.remove		= __devexit_p(s3c_fb_remove),
	.id_table	= s3c_fb_driver_ids,
	.driver		= {
		.name	= "s3c-fb",
		.owner	= THIS_MODULE,
		.pm	= &s3cfb_pm_ops,
	},
};

static int __init s3c_fb_init(void)
{
	return platform_driver_register(&s3c_fb_driver);
}

static void __exit s3c_fb_cleanup(void)
{
	platform_driver_unregister(&s3c_fb_driver);
}

#if defined(CONFIG_FB_EXYNOS_FIMD_MC) || defined(CONFIG_FB_EXYNOS_FIMD_MC_WB)
late_initcall(s3c_fb_init);
#else
module_init(s3c_fb_init);
#endif
module_exit(s3c_fb_cleanup);

MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("Samsung S3C SoC Framebuffer driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:s3c-fb");
