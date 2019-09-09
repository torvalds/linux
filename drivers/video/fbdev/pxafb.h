#ifndef __PXAFB_H__
#define __PXAFB_H__

/*
 * linux/drivers/video/pxafb.h
 *    -- Intel PXA250/210 LCD Controller Frame Buffer Device
 *
 *  Copyright (C) 1999 Eric A. Thomas.
 *  Copyright (C) 2004 Jean-Frederic Clere.
 *  Copyright (C) 2004 Ian Campbell.
 *  Copyright (C) 2004 Jeff Lackey.
 *   Based on sa1100fb.c Copyright (C) 1999 Eric A. Thomas
 *  which in turn is
 *   Based on acornfb.c Copyright (C) Russell King.
 *
 *  2001-08-03: Cliff Brake <cbrake@acclent.com>
 *	 - ported SA1100 code to PXA
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/* PXA LCD DMA descriptor */
struct pxafb_dma_descriptor {
	unsigned int fdadr;
	unsigned int fsadr;
	unsigned int fidr;
	unsigned int ldcmd;
};

enum {
	PAL_NONE	= -1,
	PAL_BASE	= 0,
	PAL_OV1		= 1,
	PAL_OV2		= 2,
	PAL_MAX,
};

enum {
	DMA_BASE	= 0,
	DMA_UPPER	= 0,
	DMA_LOWER	= 1,
	DMA_OV1		= 1,
	DMA_OV2_Y	= 2,
	DMA_OV2_Cb	= 3,
	DMA_OV2_Cr	= 4,
	DMA_CURSOR	= 5,
	DMA_CMD		= 6,
	DMA_MAX,
};

/* maximum palette size - 256 entries, each 4 bytes long */
#define PALETTE_SIZE	(256 * 4)
#define CMD_BUFF_SIZE	(1024 * 50)

/* NOTE: the palette and frame dma descriptors are doubled to allow
 * the 2nd set for branch settings (FBRx)
 */
struct pxafb_dma_buff {
	unsigned char palette[PAL_MAX * PALETTE_SIZE];
	uint16_t cmd_buff[CMD_BUFF_SIZE];
	struct pxafb_dma_descriptor pal_desc[PAL_MAX * 2];
	struct pxafb_dma_descriptor dma_desc[DMA_MAX * 2];
};

enum {
	OVERLAY1,
	OVERLAY2,
};

enum {
	OVERLAY_FORMAT_RGB = 0,
	OVERLAY_FORMAT_YUV444_PACKED,
	OVERLAY_FORMAT_YUV444_PLANAR,
	OVERLAY_FORMAT_YUV422_PLANAR,
	OVERLAY_FORMAT_YUV420_PLANAR,
};

#define NONSTD_TO_XPOS(x)	(((x) >> 0)  & 0x3ff)
#define NONSTD_TO_YPOS(x)	(((x) >> 10) & 0x3ff)
#define NONSTD_TO_PFOR(x)	(((x) >> 20) & 0x7)

struct pxafb_layer;

struct pxafb_layer_ops {
	void (*enable)(struct pxafb_layer *);
	void (*disable)(struct pxafb_layer *);
	void (*setup)(struct pxafb_layer *);
};

struct pxafb_layer {
	struct fb_info		fb;
	int			id;
	int			registered;
	uint32_t		usage;
	uint32_t		control[2];

	struct pxafb_layer_ops	*ops;

	void __iomem		*video_mem;
	unsigned long		video_mem_phys;
	size_t			video_mem_size;
	struct completion	branch_done;

	struct pxafb_info	*fbi;
};

struct pxafb_info {
	struct fb_info		fb;
	struct device		*dev;
	struct clk		*clk;

	void __iomem		*mmio_base;

	struct pxafb_dma_buff	*dma_buff;
	size_t			dma_buff_size;
	dma_addr_t		dma_buff_phys;
	dma_addr_t		fdadr[DMA_MAX * 2];

	void __iomem		*video_mem;	/* virtual address of frame buffer */
	unsigned long		video_mem_phys;	/* physical address of frame buffer */
	size_t			video_mem_size;	/* size of the frame buffer */
	u16 *			palette_cpu;	/* virtual address of palette memory */
	u_int			palette_size;

	u_int			lccr0;
	u_int			lccr3;
	u_int			lccr4;
	u_int			cmap_inverse:1,
				cmap_static:1,
				unused:30;

	u_int			reg_lccr0;
	u_int			reg_lccr1;
	u_int			reg_lccr2;
	u_int			reg_lccr3;
	u_int			reg_lccr4;
	u_int			reg_cmdcr;

	unsigned long	hsync_time;

	volatile u_char		state;
	volatile u_char		task_state;
	struct mutex		ctrlr_lock;
	wait_queue_head_t	ctrlr_wait;
	struct work_struct	task;

	struct completion	disable_done;

#ifdef CONFIG_FB_PXA_SMARTPANEL
	uint16_t		*smart_cmds;
	size_t			n_smart_cmds;
	struct completion	command_done;
	struct completion	refresh_done;
	struct task_struct	*smart_thread;
#endif

#ifdef CONFIG_FB_PXA_OVERLAY
	struct pxafb_layer	overlay[2];
#endif

#ifdef CONFIG_CPU_FREQ
	struct notifier_block	freq_transition;
	struct notifier_block	freq_policy;
#endif

	struct regulator *lcd_supply;
	bool lcd_supply_enabled;

	void (*lcd_power)(int, struct fb_var_screeninfo *);
	void (*backlight_power)(int);

	struct pxafb_mach_info	*inf;
};

#define TO_INF(ptr,member) container_of(ptr,struct pxafb_info,member)

/*
 * These are the actions for set_ctrlr_state
 */
#define C_DISABLE		(0)
#define C_ENABLE		(1)
#define C_DISABLE_CLKCHANGE	(2)
#define C_ENABLE_CLKCHANGE	(3)
#define C_REENABLE		(4)
#define C_DISABLE_PM		(5)
#define C_ENABLE_PM		(6)
#define C_STARTUP		(7)

#define PXA_NAME	"PXA"

/*
 * Minimum X and Y resolutions
 */
#define MIN_XRES	64
#define MIN_YRES	64

/* maximum X and Y resolutions - note these are limits from the register
 * bits length instead of the real ones
 */
#define MAX_XRES	1024
#define MAX_YRES	1024

#endif /* __PXAFB_H__ */
