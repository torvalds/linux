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
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_data/clk-u300.h>
#include <linux/irqchip.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/clocksource.h>
#include <linux/clk.h>

#include <asm/mach/map.h>
#include <asm/mach/arch.h>

/*
 * These are the large blocks of memory allocated for I/O.
 * the defines are used for setting up the I/O memory mapping.
 */

/* NAND Flash CS0 */
#define U300_NAND_CS0_PHYS_BASE		0x80000000
/* NFIF */
#define U300_NAND_IF_PHYS_BASE		0x9f800000
/* ALE, CLE offset for FSMC NAND */
#define PLAT_NAND_CLE			(1 << 16)
#define PLAT_NAND_ALE			(1 << 17)
/* AHB Peripherals */
#define U300_AHB_PER_PHYS_BASE		0xa0000000
#define U300_AHB_PER_VIRT_BASE		0xff010000
/* FAST Peripherals */
#define U300_FAST_PER_PHYS_BASE		0xc0000000
#define U300_FAST_PER_VIRT_BASE		0xff020000
/* SLOW Peripherals */
#define U300_SLOW_PER_PHYS_BASE		0xc0010000
#define U300_SLOW_PER_VIRT_BASE		0xff000000
/* Boot ROM */
#define U300_BOOTROM_PHYS_BASE		0xffff0000
#define U300_BOOTROM_VIRT_BASE		0xffff0000
/* SEMI config base */
#define U300_SEMI_CONFIG_BASE		0x2FFE0000

/*
 * AHB peripherals
 */

/* AHB Peripherals Bridge Controller */
#define U300_AHB_BRIDGE_BASE		(U300_AHB_PER_PHYS_BASE+0x0000)
/* Vectored Interrupt Controller 0, servicing 32 interrupts */
#define U300_INTCON0_BASE		(U300_AHB_PER_PHYS_BASE+0x1000)
#define U300_INTCON0_VBASE		IOMEM(U300_AHB_PER_VIRT_BASE+0x1000)
/* Vectored Interrupt Controller 1, servicing 32 interrupts */
#define U300_INTCON1_BASE		(U300_AHB_PER_PHYS_BASE+0x2000)
#define U300_INTCON1_VBASE		IOMEM(U300_AHB_PER_VIRT_BASE+0x2000)
/* Memory Stick Pro (MSPRO) controller */
#define U300_MSPRO_BASE			(U300_AHB_PER_PHYS_BASE+0x3000)
/* EMIF Configuration Area */
#define U300_EMIF_CFG_BASE		(U300_AHB_PER_PHYS_BASE+0x4000)

/*
 * FAST peripherals
 */

/* FAST bridge control */
#define U300_FAST_BRIDGE_BASE		(U300_FAST_PER_PHYS_BASE+0x0000)
/* MMC/SD controller */
#define U300_MMCSD_BASE			(U300_FAST_PER_PHYS_BASE+0x1000)
/* PCM I2S0 controller */
#define U300_PCM_I2S0_BASE		(U300_FAST_PER_PHYS_BASE+0x2000)
/* PCM I2S1 controller */
#define U300_PCM_I2S1_BASE		(U300_FAST_PER_PHYS_BASE+0x3000)
/* I2C0 controller */
#define U300_I2C0_BASE			(U300_FAST_PER_PHYS_BASE+0x4000)
/* I2C1 controller */
#define U300_I2C1_BASE			(U300_FAST_PER_PHYS_BASE+0x5000)
/* SPI controller */
#define U300_SPI_BASE			(U300_FAST_PER_PHYS_BASE+0x6000)
/* Fast UART1 on U335 only */
#define U300_UART1_BASE			(U300_FAST_PER_PHYS_BASE+0x7000)

/*
 * SLOW peripherals
 */

/* SLOW bridge control */
#define U300_SLOW_BRIDGE_BASE		(U300_SLOW_PER_PHYS_BASE)
/* SYSCON */
#define U300_SYSCON_BASE		(U300_SLOW_PER_PHYS_BASE+0x1000)
#define U300_SYSCON_VBASE		IOMEM(U300_SLOW_PER_VIRT_BASE+0x1000)
/* Watchdog */
#define U300_WDOG_BASE			(U300_SLOW_PER_PHYS_BASE+0x2000)
/* UART0 */
#define U300_UART0_BASE			(U300_SLOW_PER_PHYS_BASE+0x3000)
/* APP side special timer */
#define U300_TIMER_APP_BASE		(U300_SLOW_PER_PHYS_BASE+0x4000)
#define U300_TIMER_APP_VBASE		IOMEM(U300_SLOW_PER_VIRT_BASE+0x4000)
/* Keypad */
#define U300_KEYPAD_BASE		(U300_SLOW_PER_PHYS_BASE+0x5000)
/* GPIO */
#define U300_GPIO_BASE			(U300_SLOW_PER_PHYS_BASE+0x6000)
/* RTC */
#define U300_RTC_BASE			(U300_SLOW_PER_PHYS_BASE+0x7000)
/* Bus tracer */
#define U300_BUSTR_BASE			(U300_SLOW_PER_PHYS_BASE+0x8000)
/* Event handler (hardware queue) */
#define U300_EVHIST_BASE		(U300_SLOW_PER_PHYS_BASE+0x9000)
/* Genric Timer */
#define U300_TIMER_BASE			(U300_SLOW_PER_PHYS_BASE+0xa000)
/* PPM */
#define U300_PPM_BASE			(U300_SLOW_PER_PHYS_BASE+0xb000)

/*
 * REST peripherals
 */

/* ISP (image signal processor) */
#define U300_ISP_BASE			(0xA0008000)
/* DMA Controller base */
#define U300_DMAC_BASE			(0xC0020000)
/* MSL Base */
#define U300_MSL_BASE			(0xc0022000)
/* APEX Base */
#define U300_APEX_BASE			(0xc0030000)
/* Video Encoder Base */
#define U300_VIDEOENC_BASE		(0xc0080000)
/* XGAM Base */
#define U300_XGAM_BASE			(0xd0000000)

/*
 * SYSCON addresses applicable to the core machine.
 */

/* Chip ID register 16bit (R/-) */
#define U300_SYSCON_CIDR					(0x400)
/* SMCR */
#define U300_SYSCON_SMCR					(0x4d0)
#define U300_SYSCON_SMCR_FIELD_MASK				(0x000e)
#define U300_SYSCON_SMCR_SEMI_SREFACK_IND			(0x0008)
#define U300_SYSCON_SMCR_SEMI_SREFREQ_ENABLE			(0x0004)
#define U300_SYSCON_SMCR_SEMI_EXT_BOOT_MODE_ENABLE		(0x0002)
/* CPU_SW_DBGEN Software Debug Enable 16bit (R/W) */
#define U300_SYSCON_CSDR					(0x4f0)
#define U300_SYSCON_CSDR_SW_DEBUG_ENABLE			(0x0001)
/* PRINT_CONTROL Print Control 16bit (R/-) */
#define U300_SYSCON_PCR						(0x4f8)
#define U300_SYSCON_PCR_SERV_IND				(0x0001)
/* BOOT_CONTROL 16bit (R/-) */
#define U300_SYSCON_BCR						(0x4fc)
#define U300_SYSCON_BCR_ACC_CPU_SUBSYS_VINITHI_IND		(0x0400)
#define U300_SYSCON_BCR_APP_CPU_SUBSYS_VINITHI_IND		(0x0200)
#define U300_SYSCON_BCR_EXTRA_BOOT_OPTION_MASK			(0x01FC)
#define U300_SYSCON_BCR_APP_BOOT_SERV_MASK			(0x0003)

static void __iomem *syscon_base;

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
	val = readw(syscon_base + U300_SYSCON_CIDR);
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

/* These are mostly to get the right device names for the clock lookups */
static struct of_dev_auxdata u300_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("stericsson,pinctrl-u300", U300_SYSCON_BASE,
		"pinctrl-u300", NULL),
	OF_DEV_AUXDATA("stericsson,gpio-coh901", U300_GPIO_BASE,
		"u300-gpio", NULL),
	OF_DEV_AUXDATA("stericsson,coh901327", U300_WDOG_BASE,
		"coh901327_wdog", NULL),
	OF_DEV_AUXDATA("stericsson,coh901331", U300_RTC_BASE,
		"rtc-coh901331", NULL),
	OF_DEV_AUXDATA("stericsson,coh901318", U300_DMAC_BASE,
		"coh901318", NULL),
	OF_DEV_AUXDATA("stericsson,fsmc-nand", U300_NAND_IF_PHYS_BASE,
		"fsmc-nand", NULL),
	OF_DEV_AUXDATA("arm,primecell", U300_UART0_BASE,
		"uart0", NULL),
	OF_DEV_AUXDATA("arm,primecell", U300_UART1_BASE,
		"uart1", NULL),
	OF_DEV_AUXDATA("arm,primecell", U300_SPI_BASE,
		"pl022", NULL),
	OF_DEV_AUXDATA("st,ddci2c", U300_I2C0_BASE,
		"stu300.0", NULL),
	OF_DEV_AUXDATA("st,ddci2c", U300_I2C1_BASE,
		"stu300.1", NULL),
	OF_DEV_AUXDATA("arm,primecell", U300_MMCSD_BASE,
		"mmci", NULL),
	{ /* sentinel */ },
};

static void __init u300_init_irq_dt(void)
{
	struct device_node *syscon;
	struct clk *clk;

	syscon = of_find_node_by_path("/syscon@c0011000");
	if (!syscon) {
		pr_crit("could not find syscon node\n");
		return;
	}
	syscon_base = of_iomap(syscon, 0);
	if (!syscon_base) {
		pr_crit("could not remap syscon\n");
		return;
	}
	/* initialize clocking early, we want to clock the INTCON */
	u300_clk_init(syscon_base);

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

	/* Initialize pinmuxing */
	pinctrl_register_mappings(u300_pinmux_map,
				  ARRAY_SIZE(u300_pinmux_map));

	of_platform_populate(NULL, of_default_bus_match_table,
			u300_auxdata_lookup, NULL);

	/* Enable SEMI self refresh */
	val = readw(syscon_base + U300_SYSCON_SMCR) |
		U300_SYSCON_SMCR_SEMI_SREFREQ_ENABLE;
	writew(val, syscon_base + U300_SYSCON_SMCR);
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
