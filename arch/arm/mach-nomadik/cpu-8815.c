/*
 * Copyright STMicroelectronics, 2007.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/dma-mapping.h>
#include <linux/irqchip.h>
#include <linux/platform_data/clk-nomadik.h>
#include <linux/platform_data/pinctrl-nomadik.h>
#include <linux/pinctrl/machine.h>
#include <linux/clocksource.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/mtd/fsmc.h>
#include <linux/gpio.h>
#include <linux/amba/mmci.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/mach-types.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>

/*
 * These are the only hard-coded address offsets we still have to use.
 */
#define NOMADIK_FSMC_BASE	0x10100000	/* FSMC registers */
#define NOMADIK_SDRAMC_BASE	0x10110000	/* SDRAM Controller */
#define NOMADIK_CLCDC_BASE	0x10120000	/* CLCD Controller */
#define NOMADIK_MDIF_BASE	0x10120000	/* MDIF */
#define NOMADIK_DMA0_BASE	0x10130000	/* DMA0 Controller */
#define NOMADIK_IC_BASE		0x10140000	/* Vectored Irq Controller */
#define NOMADIK_DMA1_BASE	0x10150000	/* DMA1 Controller */
#define NOMADIK_USB_BASE	0x10170000	/* USB-OTG conf reg base */
#define NOMADIK_CRYP_BASE	0x10180000	/* Crypto processor */
#define NOMADIK_SHA1_BASE	0x10190000	/* SHA-1 Processor */
#define NOMADIK_XTI_BASE	0x101A0000	/* XTI */
#define NOMADIK_RNG_BASE	0x101B0000	/* Random number generator */
#define NOMADIK_SRC_BASE	0x101E0000	/* SRC base */
#define NOMADIK_WDOG_BASE	0x101E1000	/* Watchdog */
#define NOMADIK_MTU0_BASE	0x101E2000	/* Multiple Timer 0 */
#define NOMADIK_MTU1_BASE	0x101E3000	/* Multiple Timer 1 */
#define NOMADIK_GPIO0_BASE	0x101E4000	/* GPIO0 */
#define NOMADIK_GPIO1_BASE	0x101E5000	/* GPIO1 */
#define NOMADIK_GPIO2_BASE	0x101E6000	/* GPIO2 */
#define NOMADIK_GPIO3_BASE	0x101E7000	/* GPIO3 */
#define NOMADIK_RTC_BASE	0x101E8000	/* Real Time Clock base */
#define NOMADIK_PMU_BASE	0x101E9000	/* Power Management Unit */
#define NOMADIK_OWM_BASE	0x101EA000	/* One wire master */
#define NOMADIK_SCR_BASE	0x101EF000	/* Secure Control registers */
#define NOMADIK_MSP2_BASE	0x101F0000	/* MSP 2 interface */
#define NOMADIK_MSP1_BASE	0x101F1000	/* MSP 1 interface */
#define NOMADIK_UART2_BASE	0x101F2000	/* UART 2 interface */
#define NOMADIK_SSIRx_BASE	0x101F3000	/* SSI 8-ch rx interface */
#define NOMADIK_SSITx_BASE	0x101F4000	/* SSI 8-ch tx interface */
#define NOMADIK_MSHC_BASE	0x101F5000	/* Memory Stick(Pro) Host */
#define NOMADIK_SDI_BASE	0x101F6000	/* SD-card/MM-Card */
#define NOMADIK_I2C1_BASE	0x101F7000	/* I2C1 interface */
#define NOMADIK_I2C0_BASE	0x101F8000	/* I2C0 interface */
#define NOMADIK_MSP0_BASE	0x101F9000	/* MSP 0 interface */
#define NOMADIK_FIRDA_BASE	0x101FA000	/* FIrDA interface */
#define NOMADIK_UART1_BASE	0x101FB000	/* UART 1 interface */
#define NOMADIK_SSP_BASE	0x101FC000	/* SSP interface */
#define NOMADIK_UART0_BASE	0x101FD000	/* UART 0 interface */
#define NOMADIK_SGA_BASE	0x101FE000	/* SGA interface */
#define NOMADIK_L2CC_BASE	0x10210000	/* L2 Cache controller */
#define NOMADIK_UART1_VBASE	0xF01FB000

static unsigned long out_low[] = { PIN_OUTPUT_LOW };
static unsigned long out_high[] = { PIN_OUTPUT_HIGH };
static unsigned long in_nopull[] = { PIN_INPUT_NOPULL };
static unsigned long in_pullup[] = { PIN_INPUT_PULLUP };

static struct pinctrl_map __initdata nhk8815_pinmap[] = {
	PIN_MAP_MUX_GROUP_DEFAULT("uart0", "pinctrl-stn8815", "u0_a_1", "u0"),
	PIN_MAP_MUX_GROUP_DEFAULT("uart1", "pinctrl-stn8815", "u1_a_1", "u1"),
	/* Hog in MMC/SD card mux */
	PIN_MAP_MUX_GROUP_HOG_DEFAULT("pinctrl-stn8815", "mmcsd_a_1", "mmcsd"),
	/* MCCLK */
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO8_B10", out_low),
	/* MCCMD */
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO9_A10", in_pullup),
	/* MCCMDDIR */
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO10_C11", out_high),
	/* MCDAT3-0 */
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO11_B11", in_pullup),
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO12_A11", in_pullup),
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO13_C12", in_pullup),
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO14_B12", in_pullup),
	/* MCDAT0DIR */
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO15_A12", out_high),
	/* MCDAT31DIR */
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO16_C13", out_high),
	/* MCMSFBCLK */
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO24_C15", in_pullup),
	/* CD input GPIO */
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO111_H21", in_nopull),
	/* CD bias drive */
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO112_J21", out_low),
	/* I2C0 */
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO62_D3", in_pullup),
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO63_D2", in_pullup),
	/* I2C1 */
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO53_L4", in_pullup),
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO54_L3", in_pullup),
	/* I2C2 */
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO73_C21", in_pullup),
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-stn8815", "GPIO74_C20", in_pullup),
};

/* This is needed for LL-debug/earlyprintk/debug-macro.S */
static struct map_desc cpu8815_io_desc[] __initdata = {
	{
		.virtual =	NOMADIK_UART1_VBASE,
		.pfn =		__phys_to_pfn(NOMADIK_UART1_BASE),
		.length =	SZ_4K,
		.type =		MT_DEVICE,
	},
};

static void __init cpu8815_map_io(void)
{
	iotable_init(cpu8815_io_desc, ARRAY_SIZE(cpu8815_io_desc));
}

static void cpu8815_restart(char mode, const char *cmd)
{
	void __iomem *srcbase = ioremap(NOMADIK_SRC_BASE, SZ_4K);

	/* FIXME: use egpio when implemented */

	/* Write anything to Reset status register */
	writel(1, srcbase + 0x18);
}

/* Initial value for SRC control register: all timers use MXTAL/8 source */
#define SRC_CR_INIT_MASK	0x00007fff
#define SRC_CR_INIT_VAL		0x2aaa8000

static void __init cpu8815_timer_init_of(void)
{
	struct device_node *mtu;
	void __iomem *base;
	int irq;
	u32 src_cr;

	/* We need this to be up now */
	nomadik_clk_init();

	mtu = of_find_node_by_path("/mtu@101e2000");
	if (!mtu)
		return;
	base = of_iomap(mtu, 0);
	if (WARN_ON(!base))
		return;
	irq = irq_of_parse_and_map(mtu, 0);

	pr_info("Remapped MTU @ %p, irq: %d\n", base, irq);

	/* Configure timer sources in "system reset controller" ctrl reg */
	src_cr = readl(base);
	src_cr &= SRC_CR_INIT_MASK;
	src_cr |= SRC_CR_INIT_VAL;
	writel(src_cr, base);

	clocksource_of_init();
}

static struct fsmc_nand_timings cpu8815_nand_timings = {
	.thiz	= 0,
	.thold	= 0x10,
	.twait	= 0x0A,
	.tset	= 0,
};

static struct fsmc_nand_platform_data cpu8815_nand_data = {
	.nand_timings = &cpu8815_nand_timings,
};

/*
 * The SMSC911x IRQ is connected to a GPIO pin, but the driver expects
 * to simply request an IRQ passed as a resource. So the GPIO pin needs
 * to be requested by this hog and set as input.
 */
static int __init cpu8815_eth_init(void)
{
	struct device_node *eth;
	int gpio, irq, err;

	eth = of_find_node_by_path("/usb-s8815/ethernet-gpio");
	if (!eth) {
		pr_info("could not find any ethernet GPIO\n");
		return 0;
	}
	gpio = of_get_gpio(eth, 0);
	err = gpio_request(gpio, "eth_irq");
	if (err) {
		pr_info("failed to request ethernet GPIO\n");
		return -ENODEV;
	}
	err = gpio_direction_input(gpio);
	if (err) {
		pr_info("failed to set ethernet GPIO as input\n");
		return -ENODEV;
	}
	irq = gpio_to_irq(gpio);
	pr_info("enabled USB-S8815 ethernet GPIO %d, IRQ %d\n", gpio, irq);
	return 0;
}
device_initcall(cpu8815_eth_init);

/*
 * TODO:
 * cannot be set from device tree, convert to a proper DT
 * binding.
 */
static struct mmci_platform_data mmcsd_plat_data = {
	.ocr_mask = MMC_VDD_29_30,
};

/*
 * This GPIO pin turns on a line that is used to detect card insertion
 * on this board.
 */
static int __init cpu8815_mmcsd_init(void)
{
	struct device_node *cdbias;
	int gpio, err;

	cdbias = of_find_node_by_path("/usb-s8815/mmcsd-gpio");
	if (!cdbias) {
		pr_info("could not find MMC/SD card detect bias node\n");
		return 0;
	}
	gpio = of_get_gpio(cdbias, 0);
	if (gpio < 0) {
		pr_info("could not obtain MMC/SD card detect bias GPIO\n");
		return 0;
	}
	err = gpio_request(gpio, "card detect bias");
	if (err) {
		pr_info("failed to request card detect bias GPIO %d\n", gpio);
		return -ENODEV;
	}
	err = gpio_direction_output(gpio, 0);
	if (err){
		pr_info("failed to set GPIO %d as output, low\n", gpio);
		return err;
	}
	pr_info("enabled USB-S8815 CD bias GPIO %d, low\n", gpio);
	return 0;
}
device_initcall(cpu8815_mmcsd_init);


/* These are mostly to get the right device names for the clock lookups */
static struct of_dev_auxdata cpu8815_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("stericsson,nmk-pinctrl-stn8815", 0,
		"pinctrl-stn8815", NULL),
	OF_DEV_AUXDATA("stericsson,fsmc-nand", NOMADIK_FSMC_BASE,
		NULL, &cpu8815_nand_data),
	OF_DEV_AUXDATA("arm,primecell", NOMADIK_SDI_BASE,
		NULL, &mmcsd_plat_data),
	{ /* sentinel */ },
};

static void __init cpu8815_init_of(void)
{
#ifdef CONFIG_CACHE_L2X0
	/* At full speed latency must be >=2, so 0x249 in low bits */
	l2x0_of_init(0x00730249, 0xfe000fff);
#endif
	pinctrl_register_mappings(nhk8815_pinmap, ARRAY_SIZE(nhk8815_pinmap));
	of_platform_populate(NULL, of_default_bus_match_table,
			cpu8815_auxdata_lookup, NULL);
}

static const char * cpu8815_board_compat[] = {
	"calaosystems,usb-s8815",
	NULL,
};

DT_MACHINE_START(NOMADIK_DT, "Nomadik STn8815")
	.map_io		= cpu8815_map_io,
	.init_irq	= irqchip_init,
	.init_time	= cpu8815_timer_init_of,
	.init_machine	= cpu8815_init_of,
	.restart	= cpu8815_restart,
	.dt_compat      = cpu8815_board_compat,
MACHINE_END
