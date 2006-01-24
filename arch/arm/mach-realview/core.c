/*
 *  linux/arch/arm/mach-realview/core.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>

#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/leds.h>
#include <asm/hardware/arm_timer.h>
#include <asm/hardware/icst307.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <asm/mach/mmc.h>

#include <asm/hardware/gic.h>

#include "core.h"
#include "clock.h"

#define REALVIEW_REFCOUNTER	(__io_address(REALVIEW_SYS_BASE) + REALVIEW_SYS_24MHz_OFFSET)

/*
 * This is the RealView sched_clock implementation.  This has
 * a resolution of 41.7ns, and a maximum value of about 179s.
 */
unsigned long long sched_clock(void)
{
	unsigned long long v;

	v = (unsigned long long)readl(REALVIEW_REFCOUNTER) * 125;
	do_div(v, 3);

	return v;
}


#define REALVIEW_FLASHCTRL    (__io_address(REALVIEW_SYS_BASE) + REALVIEW_SYS_FLASH_OFFSET)

static int realview_flash_init(void)
{
	u32 val;

	val = __raw_readl(REALVIEW_FLASHCTRL);
	val &= ~REALVIEW_FLASHPROG_FLVPPEN;
	__raw_writel(val, REALVIEW_FLASHCTRL);

	return 0;
}

static void realview_flash_exit(void)
{
	u32 val;

	val = __raw_readl(REALVIEW_FLASHCTRL);
	val &= ~REALVIEW_FLASHPROG_FLVPPEN;
	__raw_writel(val, REALVIEW_FLASHCTRL);
}

static void realview_flash_set_vpp(int on)
{
	u32 val;

	val = __raw_readl(REALVIEW_FLASHCTRL);
	if (on)
		val |= REALVIEW_FLASHPROG_FLVPPEN;
	else
		val &= ~REALVIEW_FLASHPROG_FLVPPEN;
	__raw_writel(val, REALVIEW_FLASHCTRL);
}

static struct flash_platform_data realview_flash_data = {
	.map_name		= "cfi_probe",
	.width			= 4,
	.init			= realview_flash_init,
	.exit			= realview_flash_exit,
	.set_vpp		= realview_flash_set_vpp,
};

static struct resource realview_flash_resource = {
	.start			= REALVIEW_FLASH_BASE,
	.end			= REALVIEW_FLASH_BASE + REALVIEW_FLASH_SIZE,
	.flags			= IORESOURCE_MEM,
};

struct platform_device realview_flash_device = {
	.name			= "armflash",
	.id			= 0,
	.dev			= {
		.platform_data	= &realview_flash_data,
	},
	.num_resources		= 1,
	.resource		= &realview_flash_resource,
};

static struct resource realview_smc91x_resources[] = {
	[0] = {
		.start		= REALVIEW_ETH_BASE,
		.end		= REALVIEW_ETH_BASE + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_ETH,
		.end		= IRQ_ETH,
		.flags		= IORESOURCE_IRQ,
	},
};

struct platform_device realview_smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(realview_smc91x_resources),
	.resource	= realview_smc91x_resources,
};

#define REALVIEW_SYSMCI	(__io_address(REALVIEW_SYS_BASE) + REALVIEW_SYS_MCI_OFFSET)

static unsigned int realview_mmc_status(struct device *dev)
{
	struct amba_device *adev = container_of(dev, struct amba_device, dev);
	u32 mask;

	if (adev->res.start == REALVIEW_MMCI0_BASE)
		mask = 1;
	else
		mask = 2;

	return readl(REALVIEW_SYSMCI) & mask;
}

struct mmc_platform_data realview_mmc0_plat_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= realview_mmc_status,
};

struct mmc_platform_data realview_mmc1_plat_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= realview_mmc_status,
};

/*
 * Clock handling
 */
static const struct icst307_params realview_oscvco_params = {
	.ref		= 24000,
	.vco_max	= 200000,
	.vd_min		= 4 + 8,
	.vd_max		= 511 + 8,
	.rd_min		= 1 + 2,
	.rd_max		= 127 + 2,
};

static void realview_oscvco_set(struct clk *clk, struct icst307_vco vco)
{
	void __iomem *sys_lock = __io_address(REALVIEW_SYS_BASE) + REALVIEW_SYS_LOCK_OFFSET;
	void __iomem *sys_osc = __io_address(REALVIEW_SYS_BASE) + REALVIEW_SYS_OSC1_OFFSET;
	u32 val;

	val = readl(sys_osc) & ~0x7ffff;
	val |= vco.v | (vco.r << 9) | (vco.s << 16);

	writel(0xa05f, sys_lock);
	writel(val, sys_osc);
	writel(0, sys_lock);
}

struct clk realview_clcd_clk = {
	.name	= "CLCDCLK",
	.params	= &realview_oscvco_params,
	.setvco = realview_oscvco_set,
};

/*
 * CLCD support.
 */
#define SYS_CLCD_MODE_MASK	(3 << 0)
#define SYS_CLCD_MODE_888	(0 << 0)
#define SYS_CLCD_MODE_5551	(1 << 0)
#define SYS_CLCD_MODE_565_RLSB	(2 << 0)
#define SYS_CLCD_MODE_565_BLSB	(3 << 0)
#define SYS_CLCD_NLCDIOON	(1 << 2)
#define SYS_CLCD_VDDPOSSWITCH	(1 << 3)
#define SYS_CLCD_PWR3V5SWITCH	(1 << 4)
#define SYS_CLCD_ID_MASK	(0x1f << 8)
#define SYS_CLCD_ID_SANYO_3_8	(0x00 << 8)
#define SYS_CLCD_ID_UNKNOWN_8_4	(0x01 << 8)
#define SYS_CLCD_ID_EPSON_2_2	(0x02 << 8)
#define SYS_CLCD_ID_SANYO_2_5	(0x07 << 8)
#define SYS_CLCD_ID_VGA		(0x1f << 8)

static struct clcd_panel vga = {
	.mode		= {
		.name		= "VGA",
		.refresh	= 60,
		.xres		= 640,
		.yres		= 480,
		.pixclock	= 39721,
		.left_margin	= 40,
		.right_margin	= 24,
		.upper_margin	= 32,
		.lower_margin	= 11,
		.hsync_len	= 96,
		.vsync_len	= 2,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_BCD | TIM2_IPC,
	.cntl		= CNTL_LCDTFT | CNTL_LCDVCOMP(1),
	.bpp		= 16,
};

static struct clcd_panel sanyo_3_8_in = {
	.mode		= {
		.name		= "Sanyo QVGA",
		.refresh	= 116,
		.xres		= 320,
		.yres		= 240,
		.pixclock	= 100000,
		.left_margin	= 6,
		.right_margin	= 6,
		.upper_margin	= 5,
		.lower_margin	= 5,
		.hsync_len	= 6,
		.vsync_len	= 6,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_BCD,
	.cntl		= CNTL_LCDTFT | CNTL_LCDVCOMP(1),
	.bpp		= 16,
};

static struct clcd_panel sanyo_2_5_in = {
	.mode		= {
		.name		= "Sanyo QVGA Portrait",
		.refresh	= 116,
		.xres		= 240,
		.yres		= 320,
		.pixclock	= 100000,
		.left_margin	= 20,
		.right_margin	= 10,
		.upper_margin	= 2,
		.lower_margin	= 2,
		.hsync_len	= 10,
		.vsync_len	= 2,
		.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_IVS | TIM2_IHS | TIM2_IPC,
	.cntl		= CNTL_LCDTFT | CNTL_LCDVCOMP(1),
	.bpp		= 16,
};

static struct clcd_panel epson_2_2_in = {
	.mode		= {
		.name		= "Epson QCIF",
		.refresh	= 390,
		.xres		= 176,
		.yres		= 220,
		.pixclock	= 62500,
		.left_margin	= 3,
		.right_margin	= 2,
		.upper_margin	= 1,
		.lower_margin	= 0,
		.hsync_len	= 3,
		.vsync_len	= 2,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_BCD | TIM2_IPC,
	.cntl		= CNTL_LCDTFT | CNTL_LCDVCOMP(1),
	.bpp		= 16,
};

/*
 * Detect which LCD panel is connected, and return the appropriate
 * clcd_panel structure.  Note: we do not have any information on
 * the required timings for the 8.4in panel, so we presently assume
 * VGA timings.
 */
static struct clcd_panel *realview_clcd_panel(void)
{
	void __iomem *sys_clcd = __io_address(REALVIEW_SYS_BASE) + REALVIEW_SYS_CLCD_OFFSET;
	struct clcd_panel *panel = &vga;
	u32 val;

	val = readl(sys_clcd) & SYS_CLCD_ID_MASK;
	if (val == SYS_CLCD_ID_SANYO_3_8)
		panel = &sanyo_3_8_in;
	else if (val == SYS_CLCD_ID_SANYO_2_5)
		panel = &sanyo_2_5_in;
	else if (val == SYS_CLCD_ID_EPSON_2_2)
		panel = &epson_2_2_in;
	else if (val == SYS_CLCD_ID_VGA)
		panel = &vga;
	else {
		printk(KERN_ERR "CLCD: unknown LCD panel ID 0x%08x, using VGA\n",
			val);
		panel = &vga;
	}

	return panel;
}

/*
 * Disable all display connectors on the interface module.
 */
static void realview_clcd_disable(struct clcd_fb *fb)
{
	void __iomem *sys_clcd = __io_address(REALVIEW_SYS_BASE) + REALVIEW_SYS_CLCD_OFFSET;
	u32 val;

	val = readl(sys_clcd);
	val &= ~SYS_CLCD_NLCDIOON | SYS_CLCD_PWR3V5SWITCH;
	writel(val, sys_clcd);
}

/*
 * Enable the relevant connector on the interface module.
 */
static void realview_clcd_enable(struct clcd_fb *fb)
{
	void __iomem *sys_clcd = __io_address(REALVIEW_SYS_BASE) + REALVIEW_SYS_CLCD_OFFSET;
	u32 val;

	val = readl(sys_clcd);
	val &= ~SYS_CLCD_MODE_MASK;

	switch (fb->fb.var.green.length) {
	case 5:
		val |= SYS_CLCD_MODE_5551;
		break;
	case 6:
		val |= SYS_CLCD_MODE_565_RLSB;
		break;
	case 8:
		val |= SYS_CLCD_MODE_888;
		break;
	}

	/*
	 * Set the MUX
	 */
	writel(val, sys_clcd);

	/*
	 * And now enable the PSUs
	 */
	val |= SYS_CLCD_NLCDIOON | SYS_CLCD_PWR3V5SWITCH;
	writel(val, sys_clcd);
}

static unsigned long framesize = SZ_1M;

static int realview_clcd_setup(struct clcd_fb *fb)
{
	dma_addr_t dma;

	fb->panel		= realview_clcd_panel();

	fb->fb.screen_base = dma_alloc_writecombine(&fb->dev->dev, framesize,
						    &dma, GFP_KERNEL);
	if (!fb->fb.screen_base) {
		printk(KERN_ERR "CLCD: unable to map framebuffer\n");
		return -ENOMEM;
	}

	fb->fb.fix.smem_start	= dma;
	fb->fb.fix.smem_len	= framesize;

	return 0;
}

static int realview_clcd_mmap(struct clcd_fb *fb, struct vm_area_struct *vma)
{
	return dma_mmap_writecombine(&fb->dev->dev, vma,
				     fb->fb.screen_base,
				     fb->fb.fix.smem_start,
				     fb->fb.fix.smem_len);
}

static void realview_clcd_remove(struct clcd_fb *fb)
{
	dma_free_writecombine(&fb->dev->dev, fb->fb.fix.smem_len,
			      fb->fb.screen_base, fb->fb.fix.smem_start);
}

struct clcd_board clcd_plat_data = {
	.name		= "RealView",
	.check		= clcdfb_check,
	.decode		= clcdfb_decode,
	.disable	= realview_clcd_disable,
	.enable		= realview_clcd_enable,
	.setup		= realview_clcd_setup,
	.mmap		= realview_clcd_mmap,
	.remove		= realview_clcd_remove,
};

#ifdef CONFIG_LEDS
#define VA_LEDS_BASE (__io_address(REALVIEW_SYS_BASE) + REALVIEW_SYS_LED_OFFSET)

void realview_leds_event(led_event_t ledevt)
{
	unsigned long flags;
	u32 val;

	local_irq_save(flags);
	val = readl(VA_LEDS_BASE);

	switch (ledevt) {
	case led_idle_start:
		val = val & ~REALVIEW_SYS_LED0;
		break;

	case led_idle_end:
		val = val | REALVIEW_SYS_LED0;
		break;

	case led_timer:
		val = val ^ REALVIEW_SYS_LED1;
		break;

	case led_halted:
		val = 0;
		break;

	default:
		break;
	}

	writel(val, VA_LEDS_BASE);
	local_irq_restore(flags);
}
#endif	/* CONFIG_LEDS */

/*
 * Where is the timer (VA)?
 */
#define TIMER0_VA_BASE		 __io_address(REALVIEW_TIMER0_1_BASE)
#define TIMER1_VA_BASE		(__io_address(REALVIEW_TIMER0_1_BASE) + 0x20)
#define TIMER2_VA_BASE		 __io_address(REALVIEW_TIMER2_3_BASE)
#define TIMER3_VA_BASE		(__io_address(REALVIEW_TIMER2_3_BASE) + 0x20)

/*
 * How long is the timer interval?
 */
#define TIMER_INTERVAL	(TICKS_PER_uSEC * mSEC_10)
#if TIMER_INTERVAL >= 0x100000
#define TIMER_RELOAD	(TIMER_INTERVAL >> 8)
#define TIMER_DIVISOR	(TIMER_CTRL_DIV256)
#define TICKS2USECS(x)	(256 * (x) / TICKS_PER_uSEC)
#elif TIMER_INTERVAL >= 0x10000
#define TIMER_RELOAD	(TIMER_INTERVAL >> 4)		/* Divide by 16 */
#define TIMER_DIVISOR	(TIMER_CTRL_DIV16)
#define TICKS2USECS(x)	(16 * (x) / TICKS_PER_uSEC)
#else
#define TIMER_RELOAD	(TIMER_INTERVAL)
#define TIMER_DIVISOR	(TIMER_CTRL_DIV1)
#define TICKS2USECS(x)	((x) / TICKS_PER_uSEC)
#endif

/*
 * Returns number of ms since last clock interrupt.  Note that interrupts
 * will have been disabled by do_gettimeoffset()
 */
static unsigned long realview_gettimeoffset(void)
{
	unsigned long ticks1, ticks2, status;

	/*
	 * Get the current number of ticks.  Note that there is a race
	 * condition between us reading the timer and checking for
	 * an interrupt.  We get around this by ensuring that the
	 * counter has not reloaded between our two reads.
	 */
	ticks2 = readl(TIMER0_VA_BASE + TIMER_VALUE) & 0xffff;
	do {
		ticks1 = ticks2;
		status = __raw_readl(__io_address(REALVIEW_GIC_DIST_BASE + GIC_DIST_PENDING_SET)
				     + ((IRQ_TIMERINT0_1 >> 5) << 2));
		ticks2 = readl(TIMER0_VA_BASE + TIMER_VALUE) & 0xffff;
	} while (ticks2 > ticks1);

	/*
	 * Number of ticks since last interrupt.
	 */
	ticks1 = TIMER_RELOAD - ticks2;

	/*
	 * Interrupt pending?  If so, we've reloaded once already.
	 *
	 * FIXME: Need to check this is effectively timer 0 that expires
	 */
	if (status & IRQMASK_TIMERINT0_1)
		ticks1 += TIMER_RELOAD;

	/*
	 * Convert the ticks to usecs
	 */
	return TICKS2USECS(ticks1);
}

/*
 * IRQ handler for the timer
 */
static irqreturn_t realview_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	write_seqlock(&xtime_lock);

	// ...clear the interrupt
	writel(1, TIMER0_VA_BASE + TIMER_INTCLR);

	timer_tick(regs);

#if defined(CONFIG_SMP) && !defined(CONFIG_LOCAL_TIMERS)
	smp_send_timer();
	update_process_times(user_mode(regs));
#endif

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction realview_timer_irq = {
	.name		= "RealView Timer Tick",
	.flags		= SA_INTERRUPT | SA_TIMER,
	.handler	= realview_timer_interrupt,
};

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
static void __init realview_timer_init(void)
{
	u32 val;

	/* 
	 * set clock frequency: 
	 *	REALVIEW_REFCLK is 32KHz
	 *	REALVIEW_TIMCLK is 1MHz
	 */
	val = readl(__io_address(REALVIEW_SCTL_BASE));
	writel((REALVIEW_TIMCLK << REALVIEW_TIMER1_EnSel) |
	       (REALVIEW_TIMCLK << REALVIEW_TIMER2_EnSel) | 
	       (REALVIEW_TIMCLK << REALVIEW_TIMER3_EnSel) |
	       (REALVIEW_TIMCLK << REALVIEW_TIMER4_EnSel) | val,
	       __io_address(REALVIEW_SCTL_BASE));

	/*
	 * Initialise to a known state (all timers off)
	 */
	writel(0, TIMER0_VA_BASE + TIMER_CTRL);
	writel(0, TIMER1_VA_BASE + TIMER_CTRL);
	writel(0, TIMER2_VA_BASE + TIMER_CTRL);
	writel(0, TIMER3_VA_BASE + TIMER_CTRL);

	writel(TIMER_RELOAD, TIMER0_VA_BASE + TIMER_LOAD);
	writel(TIMER_RELOAD, TIMER0_VA_BASE + TIMER_VALUE);
	writel(TIMER_DIVISOR | TIMER_CTRL_ENABLE | TIMER_CTRL_PERIODIC |
	       TIMER_CTRL_IE, TIMER0_VA_BASE + TIMER_CTRL);

	/* 
	 * Make irqs happen for the system timer
	 */
	setup_irq(IRQ_TIMERINT0_1, &realview_timer_irq);
}

struct sys_timer realview_timer = {
	.init		= realview_timer_init,
	.offset		= realview_gettimeoffset,
};
