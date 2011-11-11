/*
 * bonito board support
 *
 * Copyright (C) 2011 Renesas Solutions Corp.
 * Copyright (C) 2011 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
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
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/cache-l2x0.h>
#include <mach/r8a7740.h>

/*
 * CS	Address		device			note
 *----------------------------------------------------------------
 * 0	0x0000_0000	NOR Flash (64MB)	SW12 : bit3 = OFF
 * 2	0x0800_0000	ExtNOR (64MB)		SW12 : bit3 = OFF
 * 4			-
 * 5A			-
 * 5B	0x1600_0000	SRAM (8MB)
 * 6	0x1800_0000	FPGA (64K)
 *	0x1801_0000	Ether (4KB)
 *	0x1801_1000	USB (4KB)
 */

/*
 * SW12
 *
 *	bit1			bit2			bit3
 *----------------------------------------------------------------------------
 * ON	NOR WriteProtect	NAND WriteProtect	CS0 ExtNOR / CS2 NOR
 * OFF	NOR Not WriteProtect	NAND Not WriteProtect	CS0 NOR    / CS2 ExtNOR
 */

/*
 * SCIFA5 (CN42)
 *
 * S38.3 = ON
 * S39.6 = ON
 * S43.1 = ON
 */

/*
 * FPGA
 */
#define BUSSWMR1	0x0070
#define BUSSWMR2	0x0072
#define BUSSWMR3	0x0074
#define BUSSWMR4	0x0076

#define A1MDSR		0x10E0
#define BVERR		0x1100
static u16 bonito_fpga_read(u32 offset)
{
	return __raw_readw(0xf0003000 + offset);
}

static void bonito_fpga_write(u32 offset, u16 val)
{
	__raw_writew(val, 0xf0003000 + offset);
}

/*
 * core board devices
 */
static struct platform_device *bonito_core_devices[] __initdata = {
};

/*
 * base board devices
 */
static struct platform_device *bonito_base_devices[] __initdata = {
};

/*
 * map I/O
 */
static struct map_desc bonito_io_desc[] __initdata = {
	 /*
	  * for CPGA/INTC/PFC
	  * 0xe6000000-0xefffffff -> 0xe6000000-0xefffffff
	  */
	{
		.virtual	= 0xe6000000,
		.pfn		= __phys_to_pfn(0xe6000000),
		.length		= 160 << 20,
		.type		= MT_DEVICE_NONSHARED
	},
#ifdef CONFIG_CACHE_L2X0
	/*
	 * for l2x0_init()
	 * 0xf0100000-0xf0101000 -> 0xf0002000-0xf0003000
	 */
	{
		.virtual	= 0xf0002000,
		.pfn		= __phys_to_pfn(0xf0100000),
		.length		= PAGE_SIZE,
		.type		= MT_DEVICE_NONSHARED
	},
#endif
	/*
	 * for FPGA (0x1800000-0x19ffffff)
	 * 0x18000000-0x18002000 -> 0xf0003000-0xf0005000
	 */
	{
		.virtual	= 0xf0003000,
		.pfn		= __phys_to_pfn(0x18000000),
		.length		= PAGE_SIZE * 2,
		.type		= MT_DEVICE_NONSHARED
	}
};

static void __init bonito_map_io(void)
{
	iotable_init(bonito_io_desc, ARRAY_SIZE(bonito_io_desc));

	/* setup early devices and console here as well */
	r8a7740_add_early_devices();
	shmobile_setup_console();
}

/*
 * board init
 */
#define BIT_ON(sw, bit)		(sw & (1 << bit))
#define BIT_OFF(sw, bit)	(!(sw & (1 << bit)))

static void __init bonito_init(void)
{
	u16 val;

	r8a7740_pinmux_init();

	/*
	 * core board settings
	 */

#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, Shared attribute override enable, 32K*8way */
	l2x0_init(__io(0xf0002000), 0x40440000, 0x82000fff);
#endif

	r8a7740_add_standard_devices();

	platform_add_devices(bonito_core_devices,
			     ARRAY_SIZE(bonito_core_devices));

	/*
	 * base board settings
	 */
	gpio_request(GPIO_PORT176, NULL);
	gpio_direction_input(GPIO_PORT176);
	if (!gpio_get_value(GPIO_PORT176)) {
		u16 bsw2;
		u16 bsw3;
		u16 bsw4;

		/*
		 * FPGA
		 */
		gpio_request(GPIO_FN_CS5B,		NULL);
		gpio_request(GPIO_FN_CS6A,		NULL);
		gpio_request(GPIO_FN_CS5A_PORT105,	NULL);
		gpio_request(GPIO_FN_IRQ10,		NULL);

		val = bonito_fpga_read(BVERR);
		pr_info("bonito version: cpu %02x, base %02x\n",
			((val >> 8) & 0xFF),
			((val >> 0) & 0xFF));

		bsw2 = bonito_fpga_read(BUSSWMR2);
		bsw3 = bonito_fpga_read(BUSSWMR3);
		bsw4 = bonito_fpga_read(BUSSWMR4);

		/*
		 * SCIFA5 (CN42)
		 */
		if (BIT_OFF(bsw2, 1) &&	/* S38.3 = ON */
		    BIT_OFF(bsw3, 9) &&	/* S39.6 = ON */
		    BIT_OFF(bsw4, 4)) {	/* S43.1 = ON */
			gpio_request(GPIO_FN_SCIFA5_TXD_PORT91,	NULL);
			gpio_request(GPIO_FN_SCIFA5_RXD_PORT92,	NULL);
		}

		platform_add_devices(bonito_base_devices,
				     ARRAY_SIZE(bonito_base_devices));
	}
}

static void __init bonito_timer_init(void)
{
	u16 val;
	u8 md_ck = 0;

	/* read MD_CK value */
	val = bonito_fpga_read(A1MDSR);
	if (val & (1 << 10))
		md_ck |= MD_CK2;
	if (val & (1 << 9))
		md_ck |= MD_CK1;
	if (val & (1 << 8))
		md_ck |= MD_CK0;

	r8a7740_clock_init(md_ck);
	shmobile_timer.init();
}

struct sys_timer bonito_timer = {
	.init	= bonito_timer_init,
};

MACHINE_START(BONITO, "bonito")
	.map_io		= bonito_map_io,
	.init_irq	= r8a7740_init_irq,
	.handle_irq	= shmobile_handle_irq_intc,
	.init_machine	= bonito_init,
	.timer		= &bonito_timer,
MACHINE_END
