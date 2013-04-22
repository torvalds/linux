/*
 *
 * arch/arm/mach-u300/core.c
 *
 *
 * Copyright (C) 2007-2012 ST-Ericsson SA
 * License terms: GNU General Public License (GPL) version 2
 * Core platform support, IRQ handling and device definitions.
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/termios.h>
#include <linux/dmaengine.h>
#include <linux/amba/bus.h>
#include <linux/amba/mmci.h>
#include <linux/amba/pl022.h>
#include <linux/amba/serial.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/fsmc.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/dma-mapping.h>
#include <linux/platform_data/clk-u300.h>
#include <linux/platform_data/pinctrl-coh901.h>
#include <linux/platform_data/dma-coh901318.h>
#include <linux/irqchip/arm-vic.h>
#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <linux/clocksource.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/hardware.h>
#include <mach/syscon.h>
#include <mach/irqs.h>

#include "timer.h"
#include "spi.h"
#include "i2c.h"
#include "u300-gpio.h"

/*
 * Static I/O mappings that are needed for booting the U300 platforms. The
 * only things we need are the areas where we find the timer, syscon and
 * intcon, since the remaining device drivers will map their own memory
 * physical to virtual as the need arise.
 */
static struct map_desc u300_io_desc[] __initdata = {
	{
		.virtual	= U300_SLOW_PER_VIRT_BASE,
		.pfn		= __phys_to_pfn(U300_SLOW_PER_PHYS_BASE),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= U300_AHB_PER_VIRT_BASE,
		.pfn		= __phys_to_pfn(U300_AHB_PER_PHYS_BASE),
		.length		= SZ_32K,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= U300_FAST_PER_VIRT_BASE,
		.pfn		= __phys_to_pfn(U300_FAST_PER_PHYS_BASE),
		.length		= SZ_32K,
		.type		= MT_DEVICE,
	},
};

static void __init u300_map_io(void)
{
	iotable_init(u300_io_desc, ARRAY_SIZE(u300_io_desc));
}

/*
 * Declaration of devices found on the U300 board and
 * their respective memory locations.
 */

static struct amba_pl011_data uart0_plat_data = {
#ifdef CONFIG_COH901318
	.dma_filter = coh901318_filter_id,
	.dma_rx_param = (void *) U300_DMA_UART0_RX,
	.dma_tx_param = (void *) U300_DMA_UART0_TX,
#endif
};

/* Slow device at 0x3000 offset */
static AMBA_APB_DEVICE(uart0, "uart0", 0, U300_UART0_BASE,
	{ IRQ_U300_UART0 }, &uart0_plat_data);

/* The U335 have an additional UART1 on the APP CPU */
static struct amba_pl011_data uart1_plat_data = {
#ifdef CONFIG_COH901318
	.dma_filter = coh901318_filter_id,
	.dma_rx_param = (void *) U300_DMA_UART1_RX,
	.dma_tx_param = (void *) U300_DMA_UART1_TX,
#endif
};

/* Fast device at 0x7000 offset */
static AMBA_APB_DEVICE(uart1, "uart1", 0, U300_UART1_BASE,
	{ IRQ_U300_UART1 }, &uart1_plat_data);

/* AHB device at 0x4000 offset */
static AMBA_APB_DEVICE(pl172, "pl172", 0, U300_EMIF_CFG_BASE, { }, NULL);

/* Fast device at 0x6000 offset */
static AMBA_APB_DEVICE(pl022, "pl022", 0, U300_SPI_BASE,
	{ IRQ_U300_SPI }, NULL);

/* Fast device at 0x1000 offset */
#define U300_MMCSD_IRQS	{ IRQ_U300_MMCSD_MCIINTR0, IRQ_U300_MMCSD_MCIINTR1 }

static struct mmci_platform_data mmcsd_platform_data = {
	/*
	 * Do not set ocr_mask or voltage translation function,
	 * we have a regulator we can control instead.
	 */
	.f_max = 24000000,
	.gpio_wp = -1,
	.gpio_cd = U300_GPIO_PIN_MMC_CD,
	.cd_invert = true,
	.capabilities = MMC_CAP_MMC_HIGHSPEED |
	MMC_CAP_SD_HIGHSPEED | MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA,
#ifdef CONFIG_COH901318
	.dma_filter = coh901318_filter_id,
	.dma_rx_param = (void *) U300_DMA_MMCSD_RX_TX,
	/* Don't specify a TX channel, this RX channel is bidirectional */
#endif
};

static AMBA_APB_DEVICE(mmcsd, "mmci", 0, U300_MMCSD_BASE,
	U300_MMCSD_IRQS, &mmcsd_platform_data);

/*
 * The order of device declaration may be important, since some devices
 * have dependencies on other devices being initialized first.
 */
static struct amba_device *amba_devs[] __initdata = {
	&uart0_device,
	&uart1_device,
	&pl022_device,
	&pl172_device,
	&mmcsd_device,
};

/* Here follows a list of all hw resources that the platform devices
 * allocate. Note, clock dependencies are not included
 */

static struct resource gpio_resources[] = {
	{
		.start = U300_GPIO_BASE,
		.end   = (U300_GPIO_BASE + SZ_4K - 1),
		.flags = IORESOURCE_MEM,
	},
	{
		.name  = "gpio0",
		.start = IRQ_U300_GPIO_PORT0,
		.end   = IRQ_U300_GPIO_PORT0,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name  = "gpio1",
		.start = IRQ_U300_GPIO_PORT1,
		.end   = IRQ_U300_GPIO_PORT1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name  = "gpio2",
		.start = IRQ_U300_GPIO_PORT2,
		.end   = IRQ_U300_GPIO_PORT2,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name  = "gpio3",
		.start = IRQ_U300_GPIO_PORT3,
		.end   = IRQ_U300_GPIO_PORT3,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name  = "gpio4",
		.start = IRQ_U300_GPIO_PORT4,
		.end   = IRQ_U300_GPIO_PORT4,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name  = "gpio5",
		.start = IRQ_U300_GPIO_PORT5,
		.end   = IRQ_U300_GPIO_PORT5,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name  = "gpio6",
		.start = IRQ_U300_GPIO_PORT6,
		.end   = IRQ_U300_GPIO_PORT6,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource keypad_resources[] = {
	{
		.start = U300_KEYPAD_BASE,
		.end   = U300_KEYPAD_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.name  = "coh901461-press",
		.start = IRQ_U300_KEYPAD_KEYBF,
		.end   = IRQ_U300_KEYPAD_KEYBF,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name  = "coh901461-release",
		.start = IRQ_U300_KEYPAD_KEYBR,
		.end   = IRQ_U300_KEYPAD_KEYBR,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource rtc_resources[] = {
	{
		.start = U300_RTC_BASE,
		.end   = U300_RTC_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_U300_RTC,
		.end   = IRQ_U300_RTC,
		.flags = IORESOURCE_IRQ,
	},
};

/*
 * Fsmc does have IRQs: #43 and #44 (NFIF and NFIF2)
 * but these are not yet used by the driver.
 */
static struct resource fsmc_resources[] = {
	{
		.name  = "nand_addr",
		.start = U300_NAND_CS0_PHYS_BASE + PLAT_NAND_ALE,
		.end   = U300_NAND_CS0_PHYS_BASE + PLAT_NAND_ALE + SZ_16K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.name  = "nand_cmd",
		.start = U300_NAND_CS0_PHYS_BASE + PLAT_NAND_CLE,
		.end   = U300_NAND_CS0_PHYS_BASE + PLAT_NAND_CLE + SZ_16K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.name  = "nand_data",
		.start = U300_NAND_CS0_PHYS_BASE,
		.end   = U300_NAND_CS0_PHYS_BASE + SZ_16K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.name  = "fsmc_regs",
		.start = U300_NAND_IF_PHYS_BASE,
		.end   = U300_NAND_IF_PHYS_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource i2c0_resources[] = {
	{
		.start = U300_I2C0_BASE,
		.end   = U300_I2C0_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_U300_I2C0,
		.end   = IRQ_U300_I2C0,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource i2c1_resources[] = {
	{
		.start = U300_I2C1_BASE,
		.end   = U300_I2C1_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_U300_I2C1,
		.end   = IRQ_U300_I2C1,
		.flags = IORESOURCE_IRQ,
	},

};

static struct resource wdog_resources[] = {
	{
		.start = U300_WDOG_BASE,
		.end   = U300_WDOG_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_U300_WDOG,
		.end   = IRQ_U300_WDOG,
		.flags = IORESOURCE_IRQ,
	}
};

static struct resource dma_resource[] = {
	{
		.start = U300_DMAC_BASE,
		.end = U300_DMAC_BASE + PAGE_SIZE - 1,
		.flags =  IORESOURCE_MEM,
	},
	{
		.start = IRQ_U300_DMA,
		.end = IRQ_U300_DMA,
		.flags =  IORESOURCE_IRQ,
	}
};


static struct resource pinctrl_resources[] = {
	{
		.start = U300_SYSCON_BASE,
		.end   = U300_SYSCON_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device wdog_device = {
	.name = "coh901327_wdog",
	.id = -1,
	.num_resources = ARRAY_SIZE(wdog_resources),
	.resource = wdog_resources,
};

static struct platform_device i2c0_device = {
	.name = "stu300",
	.id = 0,
	.num_resources = ARRAY_SIZE(i2c0_resources),
	.resource = i2c0_resources,
};

static struct platform_device i2c1_device = {
	.name = "stu300",
	.id = 1,
	.num_resources = ARRAY_SIZE(i2c1_resources),
	.resource = i2c1_resources,
};

static struct platform_device pinctrl_device = {
	.name = "pinctrl-u300",
	.id = -1,
	.num_resources = ARRAY_SIZE(pinctrl_resources),
	.resource = pinctrl_resources,
};

/*
 * The different variants have a few different versions of the
 * GPIO block, with different number of ports.
 */
static struct u300_gpio_platform u300_gpio_plat = {
	.ports = 7,
	.gpio_base = 0,
};

static struct platform_device gpio_device = {
	.name = "u300-gpio",
	.id = -1,
	.num_resources = ARRAY_SIZE(gpio_resources),
	.resource = gpio_resources,
	.dev = {
		.platform_data = &u300_gpio_plat,
	},
};

static struct platform_device keypad_device = {
	.name = "keypad",
	.id = -1,
	.num_resources = ARRAY_SIZE(keypad_resources),
	.resource = keypad_resources,
};

static struct platform_device rtc_device = {
	.name = "rtc-coh901331",
	.id = -1,
	.num_resources = ARRAY_SIZE(rtc_resources),
	.resource = rtc_resources,
};

static struct mtd_partition u300_partitions[] = {
	{
		.name = "bootrecords",
		.offset = 0,
		.size = SZ_128K,
	},
	{
		.name = "free",
		.offset = SZ_128K,
		.size = 8064 * SZ_1K,
	},
	{
		.name = "platform",
		.offset = 8192 * SZ_1K,
		.size = 253952 * SZ_1K,
	},
};

static struct fsmc_nand_platform_data nand_platform_data = {
	.partitions = u300_partitions,
	.nr_partitions = ARRAY_SIZE(u300_partitions),
	.options = NAND_SKIP_BBTSCAN,
	.width = FSMC_NAND_BW8,
};

static struct platform_device nand_device = {
	.name = "fsmc-nand",
	.id = -1,
	.resource = fsmc_resources,
	.num_resources = ARRAY_SIZE(fsmc_resources),
	.dev = {
		.platform_data = &nand_platform_data,
	},
};

static struct platform_device dma_device = {
	.name		= "coh901318",
	.id		= -1,
	.resource	= dma_resource,
	.num_resources  = ARRAY_SIZE(dma_resource),
	.dev = {
		.coherent_dma_mask = ~0,
	},
};

static unsigned long pin_pullup_conf[] = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_UP, 1),
};

static unsigned long pin_highz_conf[] = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_HIGH_IMPEDANCE, 0),
};

/* Pin control settings */
static struct pinctrl_map __initdata u300_pinmux_map[] = {
	/* anonymous maps for chip power and EMIFs */
	PIN_MAP_MUX_GROUP_HOG_DEFAULT("pinctrl-u300", NULL, "power"),
	PIN_MAP_MUX_GROUP_HOG_DEFAULT("pinctrl-u300", NULL, "emif0"),
	PIN_MAP_MUX_GROUP_HOG_DEFAULT("pinctrl-u300", NULL, "emif1"),
	/* per-device maps for MMC/SD, SPI and UART */
	PIN_MAP_MUX_GROUP_DEFAULT("mmci",  "pinctrl-u300", NULL, "mmc0"),
	PIN_MAP_MUX_GROUP_DEFAULT("pl022", "pinctrl-u300", NULL, "spi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("uart0", "pinctrl-u300", NULL, "uart0"),
	/* This pin is used for clock return rather than GPIO */
	PIN_MAP_CONFIGS_PIN_DEFAULT("mmci", "pinctrl-u300", "PIO APP GPIO 11",
				    pin_pullup_conf),
	/* This pin is used for card detect */
	PIN_MAP_CONFIGS_PIN_DEFAULT("mmci", "pinctrl-u300", "PIO MS INS",
				    pin_highz_conf),
};

/*
 * Notice that AMBA devices are initialized before platform devices.
 *
 */
static struct platform_device *platform_devs[] __initdata = {
	&dma_device,
	&i2c0_device,
	&i2c1_device,
	&keypad_device,
	&rtc_device,
	&pinctrl_device,
	&gpio_device,
	&nand_device,
	&wdog_device,
};

/*
 * Interrupts: the U300 platforms have two pl190 ARM PrimeCells connected
 * together so some interrupts are connected to the first one and some
 * to the second one.
 */
static void __init u300_init_irq(void)
{
	u32 mask[2] = {0, 0};
	struct clk *clk;
	int i;

	/* initialize clocking early, we want to clock the INTCON */
	u300_clk_init(U300_SYSCON_VBASE);

	/* Bootstrap EMIF and SEMI clocks */
	clk = clk_get_sys("pl172", NULL);
	BUG_ON(IS_ERR(clk));
	clk_prepare_enable(clk);
	clk = clk_get_sys("semi", NULL);
	BUG_ON(IS_ERR(clk));
	clk_prepare_enable(clk);

	/* Clock the interrupt controller */
	clk = clk_get_sys("intcon", NULL);
	BUG_ON(IS_ERR(clk));
	clk_prepare_enable(clk);

	for (i = 0; i < U300_VIC_IRQS_END; i++)
		set_bit(i, (unsigned long *) &mask[0]);
	vic_init((void __iomem *) U300_INTCON0_VBASE, IRQ_U300_INTCON0_START,
		 mask[0], mask[0]);
	vic_init((void __iomem *) U300_INTCON1_VBASE, IRQ_U300_INTCON1_START,
		 mask[1], mask[1]);
}


/*
 * U300 platforms peripheral handling
 */
struct db_chip {
	u16 chipid;
	const char *name;
};

/*
 * This is a list of the Digital Baseband chips used in the U300 platform.
 */
static struct db_chip db_chips[] __initdata = {
	{
		.chipid = 0xb800,
		.name = "DB3000",
	},
	{
		.chipid = 0xc000,
		.name = "DB3100",
	},
	{
		.chipid = 0xc800,
		.name = "DB3150",
	},
	{
		.chipid = 0xd800,
		.name = "DB3200",
	},
	{
		.chipid = 0xe000,
		.name = "DB3250",
	},
	{
		.chipid = 0xe800,
		.name = "DB3210",
	},
	{
		.chipid = 0xf000,
		.name = "DB3350 P1x",
	},
	{
		.chipid = 0xf100,
		.name = "DB3350 P2x",
	},
	{
		.chipid = 0x0000, /* List terminator */
		.name = NULL,
	}
};

static void __init u300_init_check_chip(void)
{

	u16 val;
	struct db_chip *chip;
	const char *chipname;
	const char unknown[] = "UNKNOWN";

	/* Read out and print chip ID */
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_CIDR);
	/* This is in funky bigendian order... */
	val = (val & 0xFFU) << 8 | (val >> 8);
	chip = db_chips;
	chipname = unknown;

	for ( ; chip->chipid; chip++) {
		if (chip->chipid == (val & 0xFF00U)) {
			chipname = chip->name;
			break;
		}
	}
	printk(KERN_INFO "Initializing U300 system on %s baseband chip " \
	       "(chip ID 0x%04x)\n", chipname, val);

	if ((val & 0xFF00U) != 0xf000 && (val & 0xFF00U) != 0xf100) {
		printk(KERN_ERR "Platform configured for BS335 " \
		       " with DB3350 but %s detected, expect problems!",
		       chipname);
	}
}

/*
 * Some devices and their resources require reserved physical memory from
 * the end of the available RAM. This function traverses the list of devices
 * and assigns actual addresses to these.
 */
static void __init u300_assign_physmem(void)
{
	unsigned long curr_start = __pa(high_memory);
	int i, j;

	for (i = 0; i < ARRAY_SIZE(platform_devs); i++) {
		for (j = 0; j < platform_devs[i]->num_resources; j++) {
			struct resource *const res =
			  &platform_devs[i]->resource[j];

			if (IORESOURCE_MEM == res->flags &&
				     0 == res->start) {
				res->start  = curr_start;
				res->end   += curr_start;
				curr_start += resource_size(res);

				printk(KERN_INFO "core.c: Mapping RAM " \
				       "%#x-%#x to device %s:%s\n",
					res->start, res->end,
				       platform_devs[i]->name, res->name);
			}
		}
	}
}

static void __init u300_init_machine(void)
{
	int i;
	u16 val;

	/* Check what platform we run and print some status information */
	u300_init_check_chip();

	/* Initialize SPI device with some board specifics */
	u300_spi_init(&pl022_device);

	/* Register the AMBA devices in the AMBA bus abstraction layer */
	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}

	u300_assign_physmem();

	/* Initialize pinmuxing */
	pinctrl_register_mappings(u300_pinmux_map,
				  ARRAY_SIZE(u300_pinmux_map));

	/* Register subdevices on the I2C buses */
	u300_i2c_register_board_devices();

	/* Register the platform devices */
	platform_add_devices(platform_devs, ARRAY_SIZE(platform_devs));

	/* Register subdevices on the SPI bus */
	u300_spi_register_board_devices();

	/* Enable SEMI self refresh */
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_SMCR) |
		U300_SYSCON_SMCR_SEMI_SREFREQ_ENABLE;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_SMCR);
}

/* Forward declare this function from the watchdog */
void coh901327_watchdog_reset(void);

static void u300_restart(char mode, const char *cmd)
{
	switch (mode) {
	case 's':
	case 'h':
#ifdef CONFIG_COH901327_WATCHDOG
		coh901327_watchdog_reset();
#endif
		break;
	default:
		/* Do nothing */
		break;
	}
	/* Wait for system do die/reset. */
	while (1);
}

MACHINE_START(U300, "Ericsson AB U335 S335/B335 Prototype Board")
	/* Maintainer: Linus Walleij <linus.walleij@stericsson.com> */
	.atag_offset	= 0x100,
	.map_io		= u300_map_io,
	.nr_irqs	= 0,
	.init_irq	= u300_init_irq,
	.init_time	= u300_timer_init,
	.init_machine	= u300_init_machine,
	.restart	= u300_restart,
MACHINE_END

#ifdef CONFIG_OF

static struct pl022_ssp_controller spi_plat_data = {
	/* If you have several SPI buses this varies, we have only bus 0 */
	.bus_id = 0,
	/*
	 * On the APP CPU GPIO 4, 5 and 6 are connected as generic
	 * chip selects for SPI. (Same on U330, U335 and U365.)
	 * TODO: make sure the GPIO driver can select these properly
	 * and do padmuxing accordingly too.
	 */
	.num_chipselect = 3,
	.enable_dma = 1,
	.dma_filter = coh901318_filter_id,
	.dma_rx_param = (void *) U300_DMA_SPI_RX,
	.dma_tx_param = (void *) U300_DMA_SPI_TX,
};

/* These are mostly to get the right device names for the clock lookups */
static struct of_dev_auxdata u300_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("stericsson,pinctrl-u300", U300_SYSCON_BASE,
		"pinctrl-u300", NULL),
	OF_DEV_AUXDATA("stericsson,gpio-coh901", U300_GPIO_BASE,
		"u300-gpio", &u300_gpio_plat),
	OF_DEV_AUXDATA("stericsson,coh901327", U300_WDOG_BASE,
		"coh901327_wdog", NULL),
	OF_DEV_AUXDATA("stericsson,coh901331", U300_RTC_BASE,
		"rtc-coh901331", NULL),
	OF_DEV_AUXDATA("stericsson,coh901318", U300_DMAC_BASE,
		"coh901318", NULL),
	OF_DEV_AUXDATA("stericsson,fsmc-nand", U300_NAND_IF_PHYS_BASE,
		"fsmc-nand", NULL),
	OF_DEV_AUXDATA("arm,primecell", U300_UART0_BASE,
		"uart0", &uart0_plat_data),
	OF_DEV_AUXDATA("arm,primecell", U300_UART1_BASE,
		"uart1", &uart1_plat_data),
	OF_DEV_AUXDATA("arm,primecell", U300_SPI_BASE,
		"pl022", &spi_plat_data),
	OF_DEV_AUXDATA("st,ddci2c", U300_I2C0_BASE,
		"stu300.0", NULL),
	OF_DEV_AUXDATA("st,ddci2c", U300_I2C1_BASE,
		"stu300.1", NULL),
	OF_DEV_AUXDATA("arm,primecell", U300_MMCSD_BASE,
		"mmci", &mmcsd_platform_data),
	{ /* sentinel */ },
};

static void __init u300_init_irq_dt(void)
{
	struct clk *clk;

	/* initialize clocking early, we want to clock the INTCON */
	u300_clk_init(U300_SYSCON_VBASE);

	/* Bootstrap EMIF and SEMI clocks */
	clk = clk_get_sys("pl172", NULL);
	BUG_ON(IS_ERR(clk));
	clk_prepare_enable(clk);
	clk = clk_get_sys("semi", NULL);
	BUG_ON(IS_ERR(clk));
	clk_prepare_enable(clk);

	/* Clock the interrupt controller */
	clk = clk_get_sys("intcon", NULL);
	BUG_ON(IS_ERR(clk));
	clk_prepare_enable(clk);

	irqchip_init();
}

static void __init u300_init_machine_dt(void)
{
	u16 val;

	/* Check what platform we run and print some status information */
	u300_init_check_chip();

	u300_assign_physmem();

	/* Initialize pinmuxing */
	pinctrl_register_mappings(u300_pinmux_map,
				  ARRAY_SIZE(u300_pinmux_map));

	of_platform_populate(NULL, of_default_bus_match_table,
			u300_auxdata_lookup, NULL);

	/* Enable SEMI self refresh */
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_SMCR) |
		U300_SYSCON_SMCR_SEMI_SREFREQ_ENABLE;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_SMCR);
}

static const char * u300_board_compat[] = {
	"stericsson,u300",
	NULL,
};

DT_MACHINE_START(U300_DT, "U300 S335/B335 (Device Tree)")
	.map_io		= u300_map_io,
	.init_irq	= u300_init_irq_dt,
	.init_time	= clocksource_of_init,
	.init_machine	= u300_init_machine_dt,
	.restart	= u300_restart,
	.dt_compat      = u300_board_compat,
MACHINE_END

#endif /* CONFIG_OF */
