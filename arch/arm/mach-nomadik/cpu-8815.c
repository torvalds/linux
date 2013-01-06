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
#include <linux/platform_data/clocksource-nomadik-mtu.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/mtd/fsmc.h>
#include <linux/gpio.h>
#include <linux/amba/mmci.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/mach-types.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>

#include "cpu-8815.h"

/* The 8815 has 4 GPIO blocks, let's register them immediately */
static resource_size_t __initdata cpu8815_gpio_base[] = {
	NOMADIK_GPIO0_BASE,
	NOMADIK_GPIO1_BASE,
	NOMADIK_GPIO2_BASE,
	NOMADIK_GPIO3_BASE,
};

static struct platform_device *
cpu8815_add_gpio(int id, resource_size_t addr, int irq,
		 struct nmk_gpio_platform_data *pdata)
{
	struct resource resources[] = {
		{
			.start	= addr,
			.end	= addr + 127,
			.flags	= IORESOURCE_MEM,
		},
		{
			.start	= irq,
			.end	= irq,
			.flags	= IORESOURCE_IRQ,
		}
	};

	return platform_device_register_resndata(NULL, "gpio", id,
				resources, ARRAY_SIZE(resources),
				pdata, sizeof(*pdata));
}

void cpu8815_add_gpios(resource_size_t *base, int num, int irq,
		       struct nmk_gpio_platform_data *pdata)
{
	int first = 0;
	int i;

	for (i = 0; i < num; i++, first += 32, irq++) {
		pdata->first_gpio = first;
		pdata->first_irq = NOMADIK_GPIO_TO_IRQ(first);
		pdata->num_gpio = 32;

		cpu8815_add_gpio(i, base[i], irq, pdata);
	}
}

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

static inline void
cpu8815_add_pinctrl(struct device *parent, const char *name)
{
	struct platform_device_info pdevinfo = {
		.parent = parent,
		.name = name,
		.id = -1,
	};

	pinctrl_register_mappings(nhk8815_pinmap, ARRAY_SIZE(nhk8815_pinmap));
	platform_device_register_full(&pdevinfo);
}

static int __init cpu8815_init(void)
{
	struct nmk_gpio_platform_data pdata = {
		/* No custom data yet */
	};

	/* For e.g. device tree boots */
	if (!machine_is_nomadik())
		return 0;

	cpu8815_add_gpios(cpu8815_gpio_base, ARRAY_SIZE(cpu8815_gpio_base),
			  IRQ_GPIO0, &pdata);
	cpu8815_add_pinctrl(NULL, "pinctrl-stn8815");
	amba_apb_device_add(NULL, "rng", NOMADIK_RNG_BASE, SZ_4K, 0, 0, NULL, 0);
	amba_apb_device_add(NULL, "rtc-pl031", NOMADIK_RTC_BASE, SZ_4K, IRQ_RTC_RTT, 0, NULL, 0);
	return 0;
}
arch_initcall(cpu8815_init);

/* All SoC devices live in the same area (see hardware.h) */
static struct map_desc nomadik_io_desc[] __initdata = {
	{
		.virtual =	NOMADIK_IO_VIRTUAL,
		.pfn =		__phys_to_pfn(NOMADIK_IO_PHYSICAL),
		.length =	NOMADIK_IO_SIZE,
		.type = 	MT_DEVICE,
	}
	/* static ram and secured ram may be added later */
};

void __init cpu8815_map_io(void)
{
	iotable_init(nomadik_io_desc, ARRAY_SIZE(nomadik_io_desc));
}

void __init cpu8815_init_irq(void)
{
	/* This modified VIC cell has two register blocks, at 0 and 0x20 */
	vic_init(io_p2v(NOMADIK_IC_BASE + 0x00), IRQ_VIC_START +  0, ~0, 0);
	vic_init(io_p2v(NOMADIK_IC_BASE + 0x20), IRQ_VIC_START + 32, ~0, 0);

	/*
	 * Init clocks here so that they are available for system timer
	 * initialization.
	 */
	nomadik_clk_init();
}

/*
 * This function is called from the board init ("init_machine").
 */
 void __init cpu8815_platform_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	/* At full speed latency must be >=2, so 0x249 in low bits */
	l2x0_init(io_p2v(NOMADIK_L2CC_BASE), 0x00730249, 0xfe000fff);
#endif
	 return;
}

void cpu8815_restart(char mode, const char *cmd)
{
	void __iomem *src_rstsr = io_p2v(NOMADIK_SRC_BASE + 0x18);

	/* FIXME: use egpio when implemented */

	/* Write anything to Reset status register */
	writel(1, src_rstsr);
}

#ifdef CONFIG_OF

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

	mtu = of_find_node_by_path("/mtu0");
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

	nmdk_timer_init(base, irq);
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
	OF_DEV_AUXDATA("st,nomadik-gpio", NOMADIK_GPIO0_BASE,
		"gpio.0", NULL),
	OF_DEV_AUXDATA("st,nomadik-gpio", NOMADIK_GPIO1_BASE,
		"gpio.1", NULL),
	OF_DEV_AUXDATA("st,nomadik-gpio", NOMADIK_GPIO2_BASE,
		"gpio.2", NULL),
	OF_DEV_AUXDATA("st,nomadik-gpio", NOMADIK_GPIO3_BASE,
		"gpio.3", NULL),
	OF_DEV_AUXDATA("stericsson,nmk-pinctrl-stn8815", 0,
		"pinctrl-stn8815", NULL),
	OF_DEV_AUXDATA("arm,primecell", NOMADIK_UART0_BASE,
		"uart0", NULL),
	OF_DEV_AUXDATA("arm,primecell", NOMADIK_UART1_BASE,
		"uart1", NULL),
	OF_DEV_AUXDATA("arm,primecell", NOMADIK_RNG_BASE,
		"rng", NULL),
	OF_DEV_AUXDATA("arm,primecell", NOMADIK_RTC_BASE,
		"rtc-pl031", NULL),
	OF_DEV_AUXDATA("stericsson,fsmc-nand", NOMADIK_FSMC_BASE,
		"fsmc-nand", &cpu8815_nand_data),
	OF_DEV_AUXDATA("arm,primecell", NOMADIK_SDI_BASE,
		"mmci", &mmcsd_plat_data),
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

#endif
