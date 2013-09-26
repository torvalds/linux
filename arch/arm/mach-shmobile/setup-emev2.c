/*
 * Emma Mobile EV2 processor support
 *
 * Copyright (C) 2012  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/platform_data/gpio-em.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/irqchip/arm-gic.h>
#include <mach/common.h>
#include <mach/emev2.h>
#include <mach/irqs.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

static struct map_desc emev2_io_desc[] __initdata = {
#ifdef CONFIG_SMP
	/* 2M mapping for SCU + L2 controller */
	{
		.virtual	= 0xf0000000,
		.pfn		= __phys_to_pfn(0x1e000000),
		.length		= SZ_2M,
		.type		= MT_DEVICE
	},
#endif
};

void __init emev2_map_io(void)
{
	iotable_init(emev2_io_desc, ARRAY_SIZE(emev2_io_desc));
}

/* UART */
static struct resource uart0_resources[] = {
	DEFINE_RES_MEM(0xe1020000, 0x38),
	DEFINE_RES_IRQ(40),
};

static struct resource uart1_resources[] = {
	DEFINE_RES_MEM(0xe1030000, 0x38),
	DEFINE_RES_IRQ(41),
};

static struct resource uart2_resources[] = {
	DEFINE_RES_MEM(0xe1040000, 0x38),
	DEFINE_RES_IRQ(42),
};

static struct resource uart3_resources[] = {
	DEFINE_RES_MEM(0xe1050000, 0x38),
	DEFINE_RES_IRQ(43),
};

#define emev2_register_uart(idx)					\
	platform_device_register_simple("serial8250-em", idx,		\
					uart##idx##_resources,		\
					ARRAY_SIZE(uart##idx##_resources))

/* STI */
static struct resource sti_resources[] = {
	DEFINE_RES_MEM(0xe0180000, 0x54),
	DEFINE_RES_IRQ(157),
};

#define emev2_register_sti()					\
	platform_device_register_simple("em_sti", 0,		\
					sti_resources,		\
					ARRAY_SIZE(sti_resources))

/* GIO */
static struct gpio_em_config gio0_config = {
	.gpio_base = 0,
	.irq_base = EMEV2_GPIO_IRQ(0),
	.number_of_pins = 32,
};

static struct resource gio0_resources[] = {
	DEFINE_RES_MEM(0xe0050000, 0x2c),
	DEFINE_RES_MEM(0xe0050040, 0x20),
	DEFINE_RES_IRQ(99),
	DEFINE_RES_IRQ(100),
};

static struct gpio_em_config gio1_config = {
	.gpio_base = 32,
	.irq_base = EMEV2_GPIO_IRQ(32),
	.number_of_pins = 32,
};

static struct resource gio1_resources[] = {
	DEFINE_RES_MEM(0xe0050080, 0x2c),
	DEFINE_RES_MEM(0xe00500c0, 0x20),
	DEFINE_RES_IRQ(101),
	DEFINE_RES_IRQ(102),
};

static struct gpio_em_config gio2_config = {
	.gpio_base = 64,
	.irq_base = EMEV2_GPIO_IRQ(64),
	.number_of_pins = 32,
};

static struct resource gio2_resources[] = {
	DEFINE_RES_MEM(0xe0050100, 0x2c),
	DEFINE_RES_MEM(0xe0050140, 0x20),
	DEFINE_RES_IRQ(103),
	DEFINE_RES_IRQ(104),
};

static struct gpio_em_config gio3_config = {
	.gpio_base = 96,
	.irq_base = EMEV2_GPIO_IRQ(96),
	.number_of_pins = 32,
};

static struct resource gio3_resources[] = {
	DEFINE_RES_MEM(0xe0050180, 0x2c),
	DEFINE_RES_MEM(0xe00501c0, 0x20),
	DEFINE_RES_IRQ(105),
	DEFINE_RES_IRQ(106),
};

static struct gpio_em_config gio4_config = {
	.gpio_base = 128,
	.irq_base = EMEV2_GPIO_IRQ(128),
	.number_of_pins = 31,
};

static struct resource gio4_resources[] = {
	DEFINE_RES_MEM(0xe0050200, 0x2c),
	DEFINE_RES_MEM(0xe0050240, 0x20),
	DEFINE_RES_IRQ(107),
	DEFINE_RES_IRQ(108),
};

#define emev2_register_gio(idx)						\
	platform_device_register_resndata(&platform_bus, "em_gio",	\
					  idx, gio##idx##_resources,	\
					  ARRAY_SIZE(gio##idx##_resources), \
					  &gio##idx##_config,		\
					  sizeof(struct gpio_em_config))

static struct resource pmu_resources[] = {
	DEFINE_RES_IRQ(152),
	DEFINE_RES_IRQ(153),
};

#define emev2_register_pmu()					\
	platform_device_register_simple("arm-pmu", -1,		\
					pmu_resources,		\
					ARRAY_SIZE(pmu_resources))

void __init emev2_add_standard_devices(void)
{
	if (!IS_ENABLED(CONFIG_COMMON_CLK))
		emev2_clock_init();

	emev2_register_uart(0);
	emev2_register_uart(1);
	emev2_register_uart(2);
	emev2_register_uart(3);
	emev2_register_sti();
	emev2_register_gio(0);
	emev2_register_gio(1);
	emev2_register_gio(2);
	emev2_register_gio(3);
	emev2_register_gio(4);
	emev2_register_pmu();
}

void __init emev2_init_delay(void)
{
	shmobile_setup_delay(533, 1, 3); /* Cortex-A9 @ 533MHz */
}

#ifdef CONFIG_USE_OF

static const char *emev2_boards_compat_dt[] __initdata = {
	"renesas,emev2",
	NULL,
};

DT_MACHINE_START(EMEV2_DT, "Generic Emma Mobile EV2 (Flattened Device Tree)")
	.smp		= smp_ops(emev2_smp_ops),
	.map_io		= emev2_map_io,
	.init_early	= emev2_init_delay,
	.dt_compat	= emev2_boards_compat_dt,
MACHINE_END

#endif /* CONFIG_USE_OF */
