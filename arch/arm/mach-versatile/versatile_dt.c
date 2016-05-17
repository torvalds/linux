/*
 * Versatile board support using the device tree
 *
 *  Copyright (C) 2010 Secret Lab Technologies Ltd.
 *  Copyright (C) 2009 Jeremy Kerr <jeremy.kerr@canonical.com>
 *  Copyright (C) 2004 ARM Limited
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
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>
#include <linux/platform_data/video-clcd-versatile.h>
#include <linux/amba/mmci.h>
#include <linux/mtd/physmap.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

/* macro to get at MMIO space when running virtually */
#define IO_ADDRESS(x)		(((x) & 0x0fffffff) + (((x) >> 4) & 0x0f000000) + 0xf0000000)
#define __io_address(n)		((void __iomem __force *)IO_ADDRESS(n))

/*
 * Memory definitions
 */
#define VERSATILE_FLASH_BASE           0x34000000
#define VERSATILE_FLASH_SIZE           SZ_64M

/*
 * ------------------------------------------------------------------------
 *  Versatile Registers
 * ------------------------------------------------------------------------
 */
#define VERSATILE_SYS_PCICTL_OFFSET           0x44
#define VERSATILE_SYS_MCI_OFFSET              0x48
#define VERSATILE_SYS_FLASH_OFFSET            0x4C
#define VERSATILE_SYS_CLCD_OFFSET             0x50

/*
 * VERSATILE_SYS_FLASH
 */
#define VERSATILE_FLASHPROG_FLVPPEN	(1 << 0)	/* Enable writing to flash */

/*
 * VERSATILE peripheral addresses
 */
#define VERSATILE_MMCI0_BASE           0x10005000	/* MMC interface */
#define VERSATILE_MMCI1_BASE           0x1000B000	/* MMC Interface */
#define VERSATILE_CLCD_BASE            0x10120000	/* CLCD */
#define VERSATILE_SCTL_BASE            0x101E0000	/* System controller */
#define VERSATILE_IB2_BASE             0x24000000	/* IB2 module */
#define VERSATILE_IB2_CTL_BASE		(VERSATILE_IB2_BASE + 0x03000000)

/*
 * System controller bit assignment
 */
#define VERSATILE_REFCLK	0
#define VERSATILE_TIMCLK	1

#define VERSATILE_TIMER1_EnSel	15
#define VERSATILE_TIMER2_EnSel	17
#define VERSATILE_TIMER3_EnSel	19
#define VERSATILE_TIMER4_EnSel	21

static void __iomem *versatile_sys_base;
static void __iomem *versatile_ib2_ctrl;

static void versatile_flash_set_vpp(struct platform_device *pdev, int on)
{
	u32 val;

	val = readl(versatile_sys_base + VERSATILE_SYS_FLASH_OFFSET);
	if (on)
		val |= VERSATILE_FLASHPROG_FLVPPEN;
	else
		val &= ~VERSATILE_FLASHPROG_FLVPPEN;
	writel(val, versatile_sys_base + VERSATILE_SYS_FLASH_OFFSET);
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

unsigned int mmc_status(struct device *dev)
{
	struct amba_device *adev = container_of(dev, struct amba_device, dev);
	u32 mask;

	if (adev->res.start == VERSATILE_MMCI0_BASE)
		mask = 1;
	else
		mask = 2;

	return readl(versatile_sys_base + VERSATILE_SYS_MCI_OFFSET) & mask;
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
	void __iomem *sys_clcd = versatile_sys_base + VERSATILE_SYS_CLCD_OFFSET;
	u32 val;

	val = readl(sys_clcd);
	val &= ~SYS_CLCD_NLCDIOON | SYS_CLCD_PWR3V5SWITCH;
	writel(val, sys_clcd);

	/*
	 * If the LCD is Sanyo 2x5 in on the IB2 board, turn the back-light off
	 */
	if (of_machine_is_compatible("arm,versatile-ab") && is_sanyo_2_5_lcd) {
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
	void __iomem *sys_clcd = versatile_sys_base + VERSATILE_SYS_CLCD_OFFSET;
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
	void __iomem *sys_clcd = versatile_sys_base + VERSATILE_SYS_CLCD_OFFSET;
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

static struct map_desc versatile_io_desc[] __initdata __maybe_unused = {
	{
		.virtual	=  IO_ADDRESS(VERSATILE_SCTL_BASE),
		.pfn		= __phys_to_pfn(VERSATILE_SCTL_BASE),
		.length		= SZ_4K * 9,
		.type		= MT_DEVICE
	}
};

static void __init versatile_map_io(void)
{
	debug_ll_io_init();
	iotable_init(versatile_io_desc, ARRAY_SIZE(versatile_io_desc));
}

static void __init versatile_init_early(void)
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

static void __init versatile_dt_pci_init(void)
{
	u32 val;
	struct device_node *np;
	struct property *newprop;

	np = of_find_compatible_node(NULL, NULL, "arm,versatile-pci");
	if (!np)
		return;

	/* Check if PCI backplane is detected */
	val = readl(versatile_sys_base + VERSATILE_SYS_PCICTL_OFFSET);
	if (val & 1) {
		/*
		 * Enable PCI accesses. Note that the documentaton is
		 * inconsistent whether or not this is needed, but the old
		 * driver had it so we will keep it.
		 */
		writel(1, versatile_sys_base + VERSATILE_SYS_PCICTL_OFFSET);
		return;
	}

	newprop = kzalloc(sizeof(*newprop), GFP_KERNEL);
	if (!newprop)
		return;

	newprop->name = kstrdup("status", GFP_KERNEL);
	newprop->value = kstrdup("disabled", GFP_KERNEL);
	newprop->length = sizeof("disabled");
	of_update_property(np, newprop);

	pr_info("Not plugged into PCI backplane!\n");
}

static void __init versatile_dt_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "arm,core-module-versatile");
	if (np)
		versatile_sys_base = of_iomap(np, 0);
	WARN_ON(!versatile_sys_base);

	versatile_ib2_ctrl = ioremap(VERSATILE_IB2_CTL_BASE, SZ_4K);

	versatile_dt_pci_init();

	platform_device_register(&versatile_flash_device);
	of_platform_populate(NULL, of_default_bus_match_table,
			     versatile_auxdata_lookup, NULL);
}

static const char *const versatile_dt_match[] __initconst = {
	"arm,versatile-ab",
	"arm,versatile-pb",
	NULL,
};

DT_MACHINE_START(VERSATILE_PB, "ARM-Versatile (Device Tree Support)")
	.map_io		= versatile_map_io,
	.init_early	= versatile_init_early,
	.init_machine	= versatile_dt_init,
	.dt_compat	= versatile_dt_match,
MACHINE_END
