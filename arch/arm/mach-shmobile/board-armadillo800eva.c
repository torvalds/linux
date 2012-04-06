/*
 * armadillo 800 eva board support
 *
 * Copyright (C) 2012 Renesas Solutions Corp.
 * Copyright (C) 2012 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
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

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <asm/page.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/cache-l2x0.h>
#include <mach/r8a7740.h>

/*
 * CON1		Camera Module
 * CON2		Extension Bus
 * CON3		HDMI Output
 * CON4		Composite Video Output
 * CON5		H-UDI JTAG
 * CON6		ARM JTAG
 * CON7		SD1
 * CON8		SD2
 * CON9		RTC BackUp
 * CON10	Monaural Mic Input
 * CON11	Stereo Headphone Output
 * CON12	Audio Line Output(L)
 * CON13	Audio Line Output(R)
 * CON14	AWL13 Module
 * CON15	Extension
 * CON16	LCD1
 * CON17	LCD2
 * CON19	Power Input
 * CON20	USB1
 * CON21	USB2
 * CON22	Serial
 * CON23	LAN
 * CON24	USB3
 * LED1		Camera LED(Yellow)
 * LED2		Power LED (Green)
 * ED3-LED6	User LED(Yellow)
 * LED7		LAN link LED(Green)
 * LED8		LAN activity LED(Yellow)
 */

/*
 * DipSwitch
 *
 *                    SW1
 *
 * -12345678-+---------------+----------------------------
 *  1        | boot          | hermit
 *  0        | boot          | OS auto boot
 * -12345678-+---------------+----------------------------
 *   00      | boot device   | eMMC
 *   10      | boot device   | SDHI0 (CON7)
 *   01      | boot device   | -
 *   11      | boot device   | Extension Buss (CS0)
 * -12345678-+---------------+----------------------------
 *     0     | Extension Bus | D8-D15 disable, eMMC enable
 *     1     | Extension Bus | D8-D15 enable,  eMMC disable
 * -12345678-+---------------+----------------------------
 *      0    | SDHI1         | COM8 enable,  COM14 disable
 *      1    | SDHI1         | COM8 enable,  COM14 disable
 * -12345678-+---------------+----------------------------
 *        00 | JTAG          | SH-X2
 *        10 | JTAG          | ARM
 *        01 | JTAG          | -
 *        11 | JTAG          | Boundary Scan
 *-----------+---------------+----------------------------
 */

/*
 * board devices
 */
static struct platform_device *eva_devices[] __initdata = {
};

/*
 * board init
 */
static void __init eva_init(void)
{
	r8a7740_pinmux_init();

	/* SCIFA1 */
	gpio_request(GPIO_FN_SCIFA1_RXD, NULL);
	gpio_request(GPIO_FN_SCIFA1_TXD, NULL);

#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, Shared attribute override enable, 32K*8way */
	l2x0_init(__io(0xf0002000), 0x40440000, 0x82000fff);
#endif

	r8a7740_add_standard_devices();

	platform_add_devices(eva_devices,
			     ARRAY_SIZE(eva_devices));
}

static void __init eva_earlytimer_init(void)
{
	struct clk *xtal1;

	r8a7740_clock_init(MD_CK0 | MD_CK2);

	xtal1 = clk_get(NULL, "extal1");
	if (!IS_ERR(xtal1)) {
		/* armadillo 800 eva extal1 is 24MHz */
		clk_set_rate(xtal1, 24000000);
		clk_put(xtal1);
	}

	shmobile_earlytimer_init();
}

static void __init eva_add_early_devices(void)
{
	r8a7740_add_early_devices();

	/* override timer setup with board-specific code */
	shmobile_timer.init = eva_earlytimer_init;
}

MACHINE_START(ARMADILLO800EVA, "armadillo800eva")
	.map_io		= r8a7740_map_io,
	.init_early	= eva_add_early_devices,
	.init_irq	= r8a7740_init_irq,
	.handle_irq	= shmobile_handle_irq_intc,
	.init_machine	= eva_init,
	.timer		= &shmobile_timer,
MACHINE_END
