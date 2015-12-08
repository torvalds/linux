/*
 *  linux/arch/arm/mach-versatile/core.c
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
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>
#include <linux/platform_data/video-clcd-versatile.h>
#include <linux/amba/mmci.h>
#include <linux/io.h>
#include <linux/mtd/physmap.h>
#include <linux/reboot.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/hardware.h>
#include <mach/platform.h>

#include "core.h"

static struct map_desc versatile_io_desc[] __initdata __maybe_unused = {
	{
		.virtual	=  IO_ADDRESS(VERSATILE_SYS_BASE),
		.pfn		= __phys_to_pfn(VERSATILE_SYS_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	=  IO_ADDRESS(VERSATILE_SCTL_BASE),
		.pfn		= __phys_to_pfn(VERSATILE_SCTL_BASE),
		.length		= SZ_4K * 9,
		.type		= MT_DEVICE
	},
 	{
		.virtual	=  IO_ADDRESS(VERSATILE_IB2_BASE),
		.pfn		= __phys_to_pfn(VERSATILE_IB2_BASE),
		.length		= SZ_64M,
		.type		= MT_DEVICE
	},
};

void __init versatile_map_io(void)
{
	debug_ll_io_init();
	iotable_init(versatile_io_desc, ARRAY_SIZE(versatile_io_desc));
}


#define VERSATILE_FLASHCTRL    (__io_address(VERSATILE_SYS_BASE) + VERSATILE_SYS_FLASH_OFFSET)

static void versatile_flash_set_vpp(struct platform_device *pdev, int on)
{
	u32 val;

	val = __raw_readl(VERSATILE_FLASHCTRL);
	if (on)
		val |= VERSATILE_FLASHPROG_FLVPPEN;
	else
		val &= ~VERSATILE_FLASHPROG_FLVPPEN;
	__raw_writel(val, VERSATILE_FLASHCTRL);
}

static struct physmap_flash_data versatile_flash_data = {
	.width			= 4,
	.set_vpp		= versatile_flash_set_vpp,
};

static struct resource versatile_flash_resource = {
	.start			= VERSATILE_FLASH_BASE,
	.end			= VERSATILE_FLASH_BASE + VERSATILE_FLASH_SIZE - 1,
	.flags			= IORESOURCE_MEM,
};

struct platform_device versatile_flash_device = {
	.name			= "physmap-flash",
	.id			= 0,
	.dev			= {
		.platform_data	= &versatile_flash_data,
	},
	.num_resources		= 1,
	.resource		= &versatile_flash_resource,
};

#define VERSATILE_SYSMCI	(__io_address(VERSATILE_SYS_BASE) + VERSATILE_SYS_MCI_OFFSET)

unsigned int mmc_status(struct device *dev)
{
	struct amba_device *adev = container_of(dev, struct amba_device, dev);
	u32 mask;

	if (adev->res.start == VERSATILE_MMCI0_BASE)
		mask = 1;
	else
		mask = 2;

	return readl(VERSATILE_SYSMCI) & mask;
}

static struct mmci_platform_data mmc0_plat_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= mmc_status,
	.gpio_wp	= -1,
	.gpio_cd	= -1,
};

static struct mmci_platform_data mmc1_plat_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= mmc_status,
	.gpio_wp	= -1,
	.gpio_cd	= -1,
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

static bool is_sanyo_2_5_lcd;

/*
 * Disable all display connectors on the interface module.
 */
static void versatile_clcd_disable(struct clcd_fb *fb)
{
	void __iomem *sys_clcd = __io_address(VERSATILE_SYS_BASE) + VERSATILE_SYS_CLCD_OFFSET;
	u32 val;

	val = readl(sys_clcd);
	val &= ~SYS_CLCD_NLCDIOON | SYS_CLCD_PWR3V5SWITCH;
	writel(val, sys_clcd);

	/*
	 * If the LCD is Sanyo 2x5 in on the IB2 board, turn the back-light off
	 */
	if (of_machine_is_compatible("arm,versatile-ab") && is_sanyo_2_5_lcd) {
		void __iomem *versatile_ib2_ctrl = __io_address(VERSATILE_IB2_CTRL);
		unsigned long ctrl;

		ctrl = readl(versatile_ib2_ctrl);
		ctrl &= ~0x01;
		writel(ctrl, versatile_ib2_ctrl);
	}
}

/*
 * Enable the relevant connector on the interface module.
 */
static void versatile_clcd_enable(struct clcd_fb *fb)
{
	struct fb_var_screeninfo *var = &fb->fb.var;
	void __iomem *sys_clcd = __io_address(VERSATILE_SYS_BASE) + VERSATILE_SYS_CLCD_OFFSET;
	u32 val;

	val = readl(sys_clcd);
	val &= ~SYS_CLCD_MODE_MASK;

	switch (var->green.length) {
	case 5:
		val |= SYS_CLCD_MODE_5551;
		break;
	case 6:
		if (var->red.offset == 0)
			val |= SYS_CLCD_MODE_565_RLSB;
		else
			val |= SYS_CLCD_MODE_565_BLSB;
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

	/*
	 * If the LCD is Sanyo 2x5 in on the IB2 board, turn the back-light on
	 */
	if (of_machine_is_compatible("arm,versatile-ab") && is_sanyo_2_5_lcd) {
		void __iomem *versatile_ib2_ctrl = __io_address(VERSATILE_IB2_CTRL);
		unsigned long ctrl;

		ctrl = readl(versatile_ib2_ctrl);
		ctrl |= 0x01;
		writel(ctrl, versatile_ib2_ctrl);
	}
}

/*
 * Detect which LCD panel is connected, and return the appropriate
 * clcd_panel structure.  Note: we do not have any information on
 * the required timings for the 8.4in panel, so we presently assume
 * VGA timings.
 */
static int versatile_clcd_setup(struct clcd_fb *fb)
{
	void __iomem *sys_clcd = __io_address(VERSATILE_SYS_BASE) + VERSATILE_SYS_CLCD_OFFSET;
	const char *panel_name;
	u32 val;

	is_sanyo_2_5_lcd = false;

	val = readl(sys_clcd) & SYS_CLCD_ID_MASK;
	if (val == SYS_CLCD_ID_SANYO_3_8)
		panel_name = "Sanyo TM38QV67A02A";
	else if (val == SYS_CLCD_ID_SANYO_2_5) {
		panel_name = "Sanyo QVGA Portrait";
		is_sanyo_2_5_lcd = true;
	} else if (val == SYS_CLCD_ID_EPSON_2_2)
		panel_name = "Epson L2F50113T00";
	else if (val == SYS_CLCD_ID_VGA)
		panel_name = "VGA";
	else {
		printk(KERN_ERR "CLCD: unknown LCD panel ID 0x%08x, using VGA\n",
			val);
		panel_name = "VGA";
	}

	fb->panel = versatile_clcd_get_panel(panel_name);
	if (!fb->panel)
		return -EINVAL;

	return versatile_clcd_setup_dma(fb, SZ_1M);
}

static void versatile_clcd_decode(struct clcd_fb *fb, struct clcd_regs *regs)
{
	clcdfb_decode(fb, regs);

	/* Always clear BGR for RGB565: we do the routing externally */
	if (fb->fb.var.green.length == 6)
		regs->cntl &= ~CNTL_BGR;
}

static struct clcd_board clcd_plat_data = {
	.name		= "Versatile",
	.caps		= CLCD_CAP_5551 | CLCD_CAP_565 | CLCD_CAP_888,
	.check		= clcdfb_check,
	.decode		= versatile_clcd_decode,
	.disable	= versatile_clcd_disable,
	.enable		= versatile_clcd_enable,
	.setup		= versatile_clcd_setup,
	.mmap		= versatile_clcd_mmap_dma,
	.remove		= versatile_clcd_remove_dma,
};

/*
 * Lookup table for attaching a specific name and platform_data pointer to
 * devices as they get created by of_platform_populate().  Ideally this table
 * would not exist, but the current clock implementation depends on some devices
 * having a specific name.
 */
struct of_dev_auxdata versatile_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_MMCI0_BASE, "fpga:05", &mmc0_plat_data),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_MMCI1_BASE, "fpga:0b", &mmc1_plat_data),

	OF_DEV_AUXDATA("arm,primecell", VERSATILE_CLCD_BASE, "dev:20", &clcd_plat_data),

	{}
};

void versatile_restart(enum reboot_mode mode, const char *cmd)
{
	void __iomem *sys = __io_address(VERSATILE_SYS_BASE);
	u32 val;

	val = __raw_readl(sys + VERSATILE_SYS_RESETCTL_OFFSET);
	val |= 0x105;

	__raw_writel(0xa05f, sys + VERSATILE_SYS_LOCK_OFFSET);
	__raw_writel(val, sys + VERSATILE_SYS_RESETCTL_OFFSET);
	__raw_writel(0, sys + VERSATILE_SYS_LOCK_OFFSET);
}

/* Early initializations */
void __init versatile_init_early(void)
{
	u32 val;

	/*
	 * set clock frequency:
	 *	VERSATILE_REFCLK is 32KHz
	 *	VERSATILE_TIMCLK is 1MHz
	 */
	val = readl(__io_address(VERSATILE_SCTL_BASE));
	writel((VERSATILE_TIMCLK << VERSATILE_TIMER1_EnSel) |
	       (VERSATILE_TIMCLK << VERSATILE_TIMER2_EnSel) |
	       (VERSATILE_TIMCLK << VERSATILE_TIMER3_EnSel) |
	       (VERSATILE_TIMCLK << VERSATILE_TIMER4_EnSel) | val,
	       __io_address(VERSATILE_SCTL_BASE));
}
