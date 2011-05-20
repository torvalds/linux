/*
 * arch/arm/mach-spear3xx/spear3xx.c
 *
 * SPEAr3XX machines common source file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/types.h>
#include <linux/amba/pl061.h>
#include <linux/ptrace.h>
#include <linux/io.h>
#include <asm/hardware/vic.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <mach/generic.h>
#include <mach/hardware.h>

/* Add spear3xx machines common devices here */
/* gpio device registration */
static struct pl061_platform_data gpio_plat_data = {
	.gpio_base	= 0,
	.irq_base	= SPEAR3XX_GPIO_INT_BASE,
};

struct amba_device spear3xx_gpio_device = {
	.dev = {
		.init_name = "gpio",
		.platform_data = &gpio_plat_data,
	},
	.res = {
		.start = SPEAR3XX_ICM3_GPIO_BASE,
		.end = SPEAR3XX_ICM3_GPIO_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	.irq = {SPEAR3XX_IRQ_BASIC_GPIO, NO_IRQ},
};

/* uart device registration */
struct amba_device spear3xx_uart_device = {
	.dev = {
		.init_name = "uart",
	},
	.res = {
		.start = SPEAR3XX_ICM1_UART_BASE,
		.end = SPEAR3XX_ICM1_UART_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	.irq = {SPEAR3XX_IRQ_UART, NO_IRQ},
};

/* Do spear3xx familiy common initialization part here */
void __init spear3xx_init(void)
{
	/* nothing to do for now */
}

/* This will initialize vic */
void __init spear3xx_init_irq(void)
{
	vic_init((void __iomem *)VA_SPEAR3XX_ML1_VIC_BASE, 0, ~0, 0);
}

/* Following will create static virtual/physical mappings */
struct map_desc spear3xx_io_desc[] __initdata = {
	{
		.virtual	= VA_SPEAR3XX_ICM1_UART_BASE,
		.pfn		= __phys_to_pfn(SPEAR3XX_ICM1_UART_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= VA_SPEAR3XX_ML1_VIC_BASE,
		.pfn		= __phys_to_pfn(SPEAR3XX_ML1_VIC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= VA_SPEAR3XX_ICM3_SYS_CTRL_BASE,
		.pfn		= __phys_to_pfn(SPEAR3XX_ICM3_SYS_CTRL_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= VA_SPEAR3XX_ICM3_MISC_REG_BASE,
		.pfn		= __phys_to_pfn(SPEAR3XX_ICM3_MISC_REG_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	},
};

/* This will create static memory mapping for selected devices */
void __init spear3xx_map_io(void)
{
	iotable_init(spear3xx_io_desc, ARRAY_SIZE(spear3xx_io_desc));

	/* This will initialize clock framework */
	spear3xx_clk_init();
}

/* pad multiplexing support */
/* devices */
static struct pmx_dev_mode pmx_firda_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_FIRDA_MASK,
	},
};

struct pmx_dev spear3xx_pmx_firda = {
	.name = "firda",
	.modes = pmx_firda_modes,
	.mode_count = ARRAY_SIZE(pmx_firda_modes),
	.enb_on_reset = 0,
};

static struct pmx_dev_mode pmx_i2c_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_I2C_MASK,
	},
};

struct pmx_dev spear3xx_pmx_i2c = {
	.name = "i2c",
	.modes = pmx_i2c_modes,
	.mode_count = ARRAY_SIZE(pmx_i2c_modes),
	.enb_on_reset = 0,
};

static struct pmx_dev_mode pmx_ssp_cs_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_SSP_CS_MASK,
	},
};

struct pmx_dev spear3xx_pmx_ssp_cs = {
	.name = "ssp_chip_selects",
	.modes = pmx_ssp_cs_modes,
	.mode_count = ARRAY_SIZE(pmx_ssp_cs_modes),
	.enb_on_reset = 0,
};

static struct pmx_dev_mode pmx_ssp_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_SSP_MASK,
	},
};

struct pmx_dev spear3xx_pmx_ssp = {
	.name = "ssp",
	.modes = pmx_ssp_modes,
	.mode_count = ARRAY_SIZE(pmx_ssp_modes),
	.enb_on_reset = 0,
};

static struct pmx_dev_mode pmx_mii_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev spear3xx_pmx_mii = {
	.name = "mii",
	.modes = pmx_mii_modes,
	.mode_count = ARRAY_SIZE(pmx_mii_modes),
	.enb_on_reset = 0,
};

static struct pmx_dev_mode pmx_gpio_pin0_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_GPIO_PIN0_MASK,
	},
};

struct pmx_dev spear3xx_pmx_gpio_pin0 = {
	.name = "gpio_pin0",
	.modes = pmx_gpio_pin0_modes,
	.mode_count = ARRAY_SIZE(pmx_gpio_pin0_modes),
	.enb_on_reset = 0,
};

static struct pmx_dev_mode pmx_gpio_pin1_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_GPIO_PIN1_MASK,
	},
};

struct pmx_dev spear3xx_pmx_gpio_pin1 = {
	.name = "gpio_pin1",
	.modes = pmx_gpio_pin1_modes,
	.mode_count = ARRAY_SIZE(pmx_gpio_pin1_modes),
	.enb_on_reset = 0,
};

static struct pmx_dev_mode pmx_gpio_pin2_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_GPIO_PIN2_MASK,
	},
};

struct pmx_dev spear3xx_pmx_gpio_pin2 = {
	.name = "gpio_pin2",
	.modes = pmx_gpio_pin2_modes,
	.mode_count = ARRAY_SIZE(pmx_gpio_pin2_modes),
	.enb_on_reset = 0,
};

static struct pmx_dev_mode pmx_gpio_pin3_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_GPIO_PIN3_MASK,
	},
};

struct pmx_dev spear3xx_pmx_gpio_pin3 = {
	.name = "gpio_pin3",
	.modes = pmx_gpio_pin3_modes,
	.mode_count = ARRAY_SIZE(pmx_gpio_pin3_modes),
	.enb_on_reset = 0,
};

static struct pmx_dev_mode pmx_gpio_pin4_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_GPIO_PIN4_MASK,
	},
};

struct pmx_dev spear3xx_pmx_gpio_pin4 = {
	.name = "gpio_pin4",
	.modes = pmx_gpio_pin4_modes,
	.mode_count = ARRAY_SIZE(pmx_gpio_pin4_modes),
	.enb_on_reset = 0,
};

static struct pmx_dev_mode pmx_gpio_pin5_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_GPIO_PIN5_MASK,
	},
};

struct pmx_dev spear3xx_pmx_gpio_pin5 = {
	.name = "gpio_pin5",
	.modes = pmx_gpio_pin5_modes,
	.mode_count = ARRAY_SIZE(pmx_gpio_pin5_modes),
	.enb_on_reset = 0,
};

static struct pmx_dev_mode pmx_uart0_modem_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_UART0_MODEM_MASK,
	},
};

struct pmx_dev spear3xx_pmx_uart0_modem = {
	.name = "uart0_modem",
	.modes = pmx_uart0_modem_modes,
	.mode_count = ARRAY_SIZE(pmx_uart0_modem_modes),
	.enb_on_reset = 0,
};

static struct pmx_dev_mode pmx_uart0_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_UART0_MASK,
	},
};

struct pmx_dev spear3xx_pmx_uart0 = {
	.name = "uart0",
	.modes = pmx_uart0_modes,
	.mode_count = ARRAY_SIZE(pmx_uart0_modes),
	.enb_on_reset = 0,
};

static struct pmx_dev_mode pmx_timer_3_4_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_TIMER_3_4_MASK,
	},
};

struct pmx_dev spear3xx_pmx_timer_3_4 = {
	.name = "timer_3_4",
	.modes = pmx_timer_3_4_modes,
	.mode_count = ARRAY_SIZE(pmx_timer_3_4_modes),
	.enb_on_reset = 0,
};

static struct pmx_dev_mode pmx_timer_1_2_modes[] = {
	{
		.ids = 0xffffffff,
		.mask = PMX_TIMER_1_2_MASK,
	},
};

struct pmx_dev spear3xx_pmx_timer_1_2 = {
	.name = "timer_1_2",
	.modes = pmx_timer_1_2_modes,
	.mode_count = ARRAY_SIZE(pmx_timer_1_2_modes),
	.enb_on_reset = 0,
};

#if defined(CONFIG_MACH_SPEAR310) || defined(CONFIG_MACH_SPEAR320)
/* plgpios devices */
static struct pmx_dev_mode pmx_plgpio_0_1_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_FIRDA_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_0_1 = {
	.name = "plgpio 0 and 1",
	.modes = pmx_plgpio_0_1_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_0_1_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_plgpio_2_3_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_UART0_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_2_3 = {
	.name = "plgpio 2 and 3",
	.modes = pmx_plgpio_2_3_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_2_3_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_plgpio_4_5_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_I2C_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_4_5 = {
	.name = "plgpio 4 and 5",
	.modes = pmx_plgpio_4_5_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_4_5_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_plgpio_6_9_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_SSP_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_6_9 = {
	.name = "plgpio 6 to 9",
	.modes = pmx_plgpio_6_9_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_6_9_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_plgpio_10_27_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_MII_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_10_27 = {
	.name = "plgpio 10 to 27",
	.modes = pmx_plgpio_10_27_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_10_27_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_plgpio_28_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_GPIO_PIN0_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_28 = {
	.name = "plgpio 28",
	.modes = pmx_plgpio_28_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_28_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_plgpio_29_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_GPIO_PIN1_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_29 = {
	.name = "plgpio 29",
	.modes = pmx_plgpio_29_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_29_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_plgpio_30_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_GPIO_PIN2_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_30 = {
	.name = "plgpio 30",
	.modes = pmx_plgpio_30_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_30_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_plgpio_31_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_GPIO_PIN3_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_31 = {
	.name = "plgpio 31",
	.modes = pmx_plgpio_31_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_31_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_plgpio_32_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_GPIO_PIN4_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_32 = {
	.name = "plgpio 32",
	.modes = pmx_plgpio_32_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_32_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_plgpio_33_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_GPIO_PIN5_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_33 = {
	.name = "plgpio 33",
	.modes = pmx_plgpio_33_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_33_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_plgpio_34_36_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_SSP_CS_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_34_36 = {
	.name = "plgpio 34 to 36",
	.modes = pmx_plgpio_34_36_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_34_36_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_plgpio_37_42_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_UART0_MODEM_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_37_42 = {
	.name = "plgpio 37 to 42",
	.modes = pmx_plgpio_37_42_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_37_42_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_plgpio_43_44_47_48_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_TIMER_1_2_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_43_44_47_48 = {
	.name = "plgpio 43, 44, 47 and 48",
	.modes = pmx_plgpio_43_44_47_48_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_43_44_47_48_modes),
	.enb_on_reset = 1,
};

static struct pmx_dev_mode pmx_plgpio_45_46_49_50_modes[] = {
	{
		.ids = 0x00,
		.mask = PMX_TIMER_3_4_MASK,
	},
};

struct pmx_dev spear3xx_pmx_plgpio_45_46_49_50 = {
	.name = "plgpio 45, 46, 49 and 50",
	.modes = pmx_plgpio_45_46_49_50_modes,
	.mode_count = ARRAY_SIZE(pmx_plgpio_45_46_49_50_modes),
	.enb_on_reset = 1,
};
#endif /* CONFIG_MACH_SPEAR310 || CONFIG_MACH_SPEAR320 */

static void __init spear3xx_timer_init(void)
{
	char pclk_name[] = "pll3_48m_clk";
	struct clk *gpt_clk, *pclk;

	/* get the system timer clock */
	gpt_clk = clk_get_sys("gpt0", NULL);
	if (IS_ERR(gpt_clk)) {
		pr_err("%s:couldn't get clk for gpt\n", __func__);
		BUG();
	}

	/* get the suitable parent clock for timer*/
	pclk = clk_get(NULL, pclk_name);
	if (IS_ERR(pclk)) {
		pr_err("%s:couldn't get %s as parent for gpt\n",
				__func__, pclk_name);
		BUG();
	}

	clk_set_parent(gpt_clk, pclk);
	clk_put(gpt_clk);
	clk_put(pclk);

	spear_setup_timer();
}

struct sys_timer spear3xx_timer = {
	.init = spear3xx_timer_init,
};
