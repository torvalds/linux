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

struct gpio_desc;

#define LCCR0           0x0000          /* LCD Control Reg. 0 */
#define LCSR            0x0004          /* LCD Status Reg. */
#define DBAR1           0x0010          /* LCD DMA Base Address Reg. channel 1 */
#define DCAR1           0x0014          /* LCD DMA Current Address Reg. channel 1 */
#define DBAR2           0x0018          /* LCD DMA Base Address Reg.  channel 2 */
#define DCAR2           0x001C          /* LCD DMA Current Address Reg. channel 2 */
#define LCCR1           0x0020          /* LCD Control Reg. 1 */
#define LCCR2           0x0024          /* LCD Control Reg. 2 */
#define LCCR3           0x0028          /* LCD Control Reg. 3 */

/* Shadows for LCD controller registers */
struct sa1100fb_lcd_reg {
	unsigned long lccr0;
	unsigned long lccr1;
	unsigned long lccr2;
	unsigned long lccr3;
};

struct sa1100fb_info {
	struct fb_info		fb;
	struct device		*dev;
	const struct sa1100fb_rgb *rgb[NR_RGB];
	void __iomem		*base;
	struct gpio_desc	*shannon_lcden;

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

	u_int			reg_lccr0;
	u_int			reg_lccr1;
	u_int			reg_lccr2;
	u_int			reg_lccr3;

	volatile u_char		state;
	volatile u_char		task_state;
	struct mutex		ctrlr_lock;
	wait_queue_head_t	ctrlr_wait;
	struct work_struct	task;

#ifdef CONFIG_CPU_FREQ
	struct notifier_block	freq_transition;
#endif

	const struct sa1100fb_mach_info *inf;
	struct clk *clk;

	u32 pseudo_palette[16];
};

#define TO_INF(ptr,member)	container_of(ptr,struct sa1100fb_info,member)

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
 * Minimum X and Y resolutions
 */
#define MIN_XRES	64
#define MIN_YRES	64

