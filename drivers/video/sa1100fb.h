/*
 * linux/drivers/video/sa1100fb.h
 *    -- StrongARM 1100 LCD Controller Frame Buffer Device
 *
 *  Copyright (C) 1999 Eric A. Thomas
 *   Based on acornfb.c Copyright (C) Russell King.
 *  
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * These are the bitfields for each
 * display depth that we support.
 */
struct sa1100fb_rgb {
	struct fb_bitfield	red;
	struct fb_bitfield	green;
	struct fb_bitfield	blue;
	struct fb_bitfield	transp;
};

/*
 * This structure describes the machine which we are running on.
 */
struct sa1100fb_mach_info {
	u_long		pixclock;

	u_short		xres;
	u_short		yres;

	u_char		bpp;
	u_char		hsync_len;
	u_char		left_margin;
	u_char		right_margin;

	u_char		vsync_len;
	u_char		upper_margin;
	u_char		lower_margin;
	u_char		sync;

	u_int		cmap_greyscale:1,
			cmap_inverse:1,
			cmap_static:1,
			unused:29;

	u_int		lccr0;
	u_int		lccr3;
};

/* Shadows for LCD controller registers */
struct sa1100fb_lcd_reg {
	unsigned long lccr0;
	unsigned long lccr1;
	unsigned long lccr2;
	unsigned long lccr3;
};

#define RGB_8	(0)
#define RGB_16	(1)
#define NR_RGB	2

struct sa1100fb_info {
	struct fb_info		fb;
	struct device		*dev;
	struct sa1100fb_rgb	*rgb[NR_RGB];

	u_int			max_bpp;
	u_int			max_xres;
	u_int			max_yres;

	/*
	 * These are the addresses we mapped
	 * the framebuffer memory region to.
	 */
	dma_addr_t		map_dma;
	u_char *		map_cpu;
	u_int			map_size;

	u_char *		screen_cpu;
	dma_addr_t		screen_dma;
	u16 *			palette_cpu;
	dma_addr_t		palette_dma;
	u_int			palette_size;

	dma_addr_t		dbar1;
	dma_addr_t		dbar2;

	u_int			lccr0;
	u_int			lccr3;
	u_int			cmap_inverse:1,
				cmap_static:1,
				unused:30;

	u_int			reg_lccr0;
	u_int			reg_lccr1;
	u_int			reg_lccr2;
	u_int			reg_lccr3;

	volatile u_char		state;
	volatile u_char		task_state;
	struct semaphore	ctrlr_sem;
	wait_queue_head_t	ctrlr_wait;
	struct work_struct	task;

#ifdef CONFIG_CPU_FREQ
	struct notifier_block	freq_transition;
	struct notifier_block	freq_policy;
#endif
};

#define __type_entry(ptr,type,member) ((type *)((char *)(ptr)-offsetof(type,member)))

#define TO_INF(ptr,member)	__type_entry(ptr,struct sa1100fb_info,member)

#define SA1100_PALETTE_MODE_VAL(bpp)    (((bpp) & 0x018) << 9)

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

#define SA1100_NAME	"SA1100"

/*
 *  Debug macros 
 */
#if DEBUG
#  define DPRINTK(fmt, args...)	printk("%s: " fmt, __FUNCTION__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif

/*
 * Minimum X and Y resolutions
 */
#define MIN_XRES	64
#define MIN_YRES	64

