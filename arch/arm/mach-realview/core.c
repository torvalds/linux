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
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>
#include <linux/io.h>
#include <linux/smsc911x.h>
#include <linux/ata_platform.h>
#include <linux/amba/mmci.h>
#include <linux/gfp.h>
#include <linux/clkdev.h>

#include <asm/system.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/leds.h>
#include <asm/mach-types.h>
#include <asm/hardware/arm_timer.h>
#include <asm/hardware/icst.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

#include <asm/hardware/gic.h>

#include <mach/platform.h>
#include <mach/irqs.h>
#include <asm/hardware/timer-sp.h>

#include <plat/clcd.h>
#include <plat/sched_clock.h>

#include "core.h"

#ifdef CONFIG_ZONE_DMA
/*
 * Adjust the zones if there are restrictions for DMA access.
 */
void __init realview_adjust_zones(unsigned long *size, unsigned long *hole)
{
	unsigned long dma_size = SZ_256M >> PAGE_SHIFT;

	if (!machine_is_realview_pbx() || size[0] <= dma_size)
		return;

	size[ZONE_NORMAL] = size[0] - dma_size;
	size[ZONE_DMA] = dma_size;
	hole[ZONE_NORMAL] = hole[0];
	hole[ZONE_DMA] = 0;
}
#endif


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

struct platform_device realview_flash_device = {
	.name			= "armflash",
	.id			= 0,
	.dev			= {
		.platform_data	= &realview_flash_data,
	},
};

int realview_flash_register(struct resource *res, u32 num)
{
	realview_flash_device.resource = res;
	realview_flash_device.num_resources = num;
	return platform_device_register(&realview_flash_device);
}

static struct smsc911x_platform_config smsc911x_config = {
	.flags		= SMSC911X_USE_32BIT,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_HIGH,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct platform_device realview_eth_device = {
	.name		= "smsc911x",
	.id		= 0,
	.num_resources	= 2,
};

int realview_eth_register(const char *name, struct resource *res)
{
	if (name)
		realview_eth_device.name = name;
	realview_eth_device.resource = res;
	if (strcmp(realview_eth_device.name, "smsc911x") == 0)
		realview_eth_device.dev.platform_data = &smsc911x_config;

	return platform_device_register(&realview_eth_device);
}

struct platform_device realview_usb_device = {
	.name			= "isp1760",
	.num_resources		= 2,
};

int realview_usb_register(struct resource *res)
{
	realview_usb_device.resource = res;
	return platform_device_register(&realview_usb_device);
}

static struct pata_platform_info pata_platform_data = {
	.ioport_shift		= 1,
};

static struct resource pata_resources[] = {
	[0] = {
		.start		= REALVIEW_CF_BASE,
		.end		= REALVIEW_CF_BASE + 0xff,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= REALVIEW_CF_BASE + 0x100,
		.end		= REALVIEW_CF_BASE + SZ_4K - 1,
		.flags		= IORESOURCE_MEM,
	},
};

struct platform_device realview_cf_device = {
	.name			= "pata_platform",
	.id			= -1,
	.num_resources		= ARRAY_SIZE(pata_resources),
	.resource		= pata_resources,
	.dev			= {
		.platform_data	= &pata_platform_data,
	},
};

static struct resource realview_i2c_resource = {
	.start		= REALVIEW_I2C_BASE,
	.end		= REALVIEW_I2C_BASE + SZ_4K - 1,
	.flags		= IORESOURCE_MEM,
};

struct platform_device realview_i2c_device = {
	.name		= "versatile-i2c",
	.id		= 0,
	.num_resources	= 1,
	.resource	= &realview_i2c_resource,
};

static struct i2c_board_info realview_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("ds1338", 0xd0 >> 1),
	},
};

static int __init realview_i2c_init(void)
{
	return i2c_register_board_info(0, realview_i2c_board_info,
				       ARRAY_SIZE(realview_i2c_board_info));
}
arch_initcall(realview_i2c_init);

#define REALVIEW_SYSMCI	(__io_address(REALVIEW_SYS_BASE) + REALVIEW_SYS_MCI_OFFSET)

/*
 * This is only used if GPIOLIB support is disabled
 */
static unsigned int realview_mmc_status(struct device *dev)
{
	struct amba_device *adev = container_of(dev, struct amba_device, dev);
	u32 mask;

	if (machine_is_realview_pb1176()) {
		static bool inserted = false;

		/*
		 * The PB1176 does not have the status register,
		 * assume it is inserted at startup, then invert
		 * for each call so card insertion/removal will
		 * be detected anyway. This will not be called if
		 * GPIO on PL061 is active, which is the proper
		 * way to do this on the PB1176.
		 */
		inserted = !inserted;
		return inserted ? 0 : 1;
	}

	if (adev->res.start == REALVIEW_MMCI0_BASE)
		mask = 1;
	else
		mask = 2;

	return readl(REALVIEW_SYSMCI) & mask;
}

struct mmci_platform_data realview_mmc0_plat_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= realview_mmc_status,
	.gpio_wp	= 17,
	.gpio_cd	= 16,
	.cd_invert	= true,
};

struct mmci_platform_data realview_mmc1_plat_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= realview_mmc_status,
	.gpio_wp	= 19,
	.gpio_cd	= 18,
	.cd_invert	= true,
};

/*
 * Clock handling
 */
static const struct icst_params realview_oscvco_params = {
	.ref		= 24000000,
	.vco_max	= ICST307_VCO_MAX,
	.vco_min	= ICST307_VCO_MIN,
	.vd_min		= 4 + 8,
	.vd_max		= 511 + 8,
	.rd_min		= 1 + 2,
	.rd_max		= 127 + 2,
	.s2div		= icst307_s2div,
	.idx2s		= icst307_idx2s,
};

static void realview_oscvco_set(struct clk *clk, struct icst_vco vco)
{
	void __iomem *sys_lock = __io_address(REALVIEW_SYS_BASE) + REALVIEW_SYS_LOCK_OFFSET;
	u32 val;

	val = readl(clk->vcoreg) & ~0x7ffff;
	val |= vco.v | (vco.r << 9) | (vco.s << 16);

	writel(0xa05f, sys_lock);
	writel(val, clk->vcoreg);
	writel(0, sys_lock);
}

static const struct clk_ops oscvco_clk_ops = {
	.round	= icst_clk_round,
	.set	= icst_clk_set,
	.setvco	= realview_oscvco_set,
};

static struct clk oscvco_clk = {
	.ops	= &oscvco_clk_ops,
	.params	= &realview_oscvco_params,
};

/*
 * These are fixed clocks.
 */
static struct clk ref24_clk = {
	.rate	= 24000000,
};

static struct clk dummy_apb_pclk;

static struct clk_lookup lookups[] = {
	{	/* Bus clock */
		.con_id		= "apb_pclk",
		.clk		= &dummy_apb_pclk,
	}, {	/* UART0 */
		.dev_id		= "dev:uart0",
		.clk		= &ref24_clk,
	}, {	/* UART1 */
		.dev_id		= "dev:uart1",
		.clk		= &ref24_clk,
	}, {	/* UART2 */
		.dev_id		= "dev:uart2",
		.clk		= &ref24_clk,
	}, {	/* UART3 */
		.dev_id		= "fpga:uart3",
		.clk		= &ref24_clk,
	}, {	/* UART3 is on the dev chip in PB1176 */
		.dev_id		= "dev:uart3",
		.clk		= &ref24_clk,
	}, {	/* UART4 only exists in PB1176 */
		.dev_id		= "fpga:uart4",
		.clk		= &ref24_clk,
	}, {	/* KMI0 */
		.dev_id		= "fpga:kmi0",
		.clk		= &ref24_clk,
	}, {	/* KMI1 */
		.dev_id		= "fpga:kmi1",
		.clk		= &ref24_clk,
	}, {	/* MMC0 */
		.dev_id		= "fpga:mmc0",
		.clk		= &ref24_clk,
	}, {	/* CLCD is in the PB1176 and EB DevChip */
		.dev_id		= "dev:clcd",
		.clk		= &oscvco_clk,
	}, {	/* PB:CLCD */
		.dev_id		= "issp:clcd",
		.clk		= &oscvco_clk,
	}, {	/* SSP */
		.dev_id		= "dev:ssp0",
		.clk		= &ref24_clk,
	}
};

void __init realview_init_early(void)
{
	void __iomem *sys = __io_address(REALVIEW_SYS_BASE);

	if (machine_is_realview_pb1176())
		oscvco_clk.vcoreg = sys + REALVIEW_SYS_OSC0_OFFSET;
	else
		oscvco_clk.vcoreg = sys + REALVIEW_SYS_OSC4_OFFSET;

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	versatile_sched_clock_init(sys + REALVIEW_SYS_24MHz_OFFSET, 24000000);
}

/*
 * CLCD support.
 */
#define SYS_CLCD_NLCDIOON	(1 << 2)
#define SYS_CLCD_VDDPOSSWITCH	(1 << 3)
#define SYS_CLCD_PWR3V5SWITCH	(1 << 4)
#define SYS_CLCD_ID_MASK	(0x1f << 8)
#define SYS_CLCD_ID_SANYO_3_8	(0x00 << 8)
#define SYS_CLCD_ID_UNKNOWN_8_4	(0x01 << 8)
#define SYS_CLCD_ID_EPSON_2_2	(0x02 << 8)
#define SYS_CLCD_ID_SANYO_2_5	(0x07 << 8)
#define SYS_CLCD_ID_VGA		(0x1f << 8)

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

	/*
	 * Enable the PSUs
	 */
	val = readl(sys_clcd);
	val |= SYS_CLCD_NLCDIOON | SYS_CLCD_PWR3V5SWITCH;
	writel(val, sys_clcd);
}

/*
 * Detect which LCD panel is connected, and return the appropriate
 * clcd_panel structure.  Note: we do not have any information on
 * the required timings for the 8.4in panel, so we presently assume
 * VGA timings.
 */
static int realview_clcd_setup(struct clcd_fb *fb)
{
	void __iomem *sys_clcd = __io_address(REALVIEW_SYS_BASE) + REALVIEW_SYS_CLCD_OFFSET;
	const char *panel_name, *vga_panel_name;
	unsigned long framesize;
	u32 val;

	if (machine_is_realview_eb()) {
		/* VGA, 16bpp */
		framesize = 640 * 480 * 2;
		vga_panel_name = "VGA";
	} else {
		/* XVGA, 16bpp */
		framesize = 1024 * 768 * 2;
		vga_panel_name = "XVGA";
	}

	val = readl(sys_clcd) & SYS_CLCD_ID_MASK;
	if (val == SYS_CLCD_ID_SANYO_3_8)
		panel_name = "Sanyo TM38QV67A02A";
	else if (val == SYS_CLCD_ID_SANYO_2_5)
		panel_name = "Sanyo QVGA Portrait";
	else if (val == SYS_CLCD_ID_EPSON_2_2)
		panel_name = "Epson L2F50113T00";
	else if (val == SYS_CLCD_ID_VGA)
		panel_name = vga_panel_name;
	else {
		pr_err("CLCD: unknown LCD panel ID 0x%08x, using VGA\n", val);
		panel_name = vga_panel_name;
	}

	fb->panel = versatile_clcd_get_panel(panel_name);
	if (!fb->panel)
		return -EINVAL;

	return versatile_clcd_setup_dma(fb, framesize);
}

struct clcd_board clcd_plat_data = {
	.name		= "RealView",
	.caps		= CLCD_CAP_ALL,
	.check		= clcdfb_check,
	.decode		= clcdfb_decode,
	.disable	= realview_clcd_disable,
	.enable		= realview_clcd_enable,
	.setup		= realview_clcd_setup,
	.mmap		= versatile_clcd_mmap_dma,
	.remove		= versatile_clcd_remove_dma,
};

#ifdef CONFIG_LEDS
#define VA_LEDS_BASE (__io_address(REALVIEW_SYS_BASE) + REALVIEW_SYS_LED_OFFSET)

void realview_leds_event(led_event_t ledevt)
{
	unsigned long flags;
	u32 val;
	u32 led = 1 << smp_processor_id();

	local_irq_save(flags);
	val = readl(VA_LEDS_BASE);

	switch (ledevt) {
	case led_idle_start:
		val = val & ~led;
		break;

	case led_idle_end:
		val = val | led;
		break;

	case led_timer:
		val = val ^ REALVIEW_SYS_LED7;
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
void __iomem *timer0_va_base;
void __iomem *timer1_va_base;
void __iomem *timer2_va_base;
void __iomem *timer3_va_base;

/*
 * Set up the clock source and clock events devices
 */
void __init realview_timer_init(unsigned int timer_irq)
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
	writel(0, timer0_va_base + TIMER_CTRL);
	writel(0, timer1_va_base + TIMER_CTRL);
	writel(0, timer2_va_base + TIMER_CTRL);
	writel(0, timer3_va_base + TIMER_CTRL);

	sp804_clocksource_init(timer3_va_base);
	sp804_clockevents_init(timer0_va_base, timer_irq);
}

/*
 * Setup the memory banks.
 */
void realview_fixup(struct machine_desc *mdesc, struct tag *tags, char **from,
		    struct meminfo *meminfo)
{
	/*
	 * Most RealView platforms have 512MB contiguous RAM at 0x70000000.
	 * Half of this is mirrored at 0.
	 */
#ifdef CONFIG_REALVIEW_HIGH_PHYS_OFFSET
	meminfo->bank[0].start = 0x70000000;
	meminfo->bank[0].size = SZ_512M;
	meminfo->nr_banks = 1;
#else
	meminfo->bank[0].start = 0;
	meminfo->bank[0].size = SZ_256M;
	meminfo->nr_banks = 1;
#endif
}
