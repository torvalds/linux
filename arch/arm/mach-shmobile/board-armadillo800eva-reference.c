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
#include <linux/gpio.h>
#include <linux/io.h>
#include <mach/common.h>
#include <mach/r8a7740.h>
#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>

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
 *      0    | SDHI1         | COM8 disable, COM14 enable
 *      1    | SDHI1         | COM8 enable,  COM14 disable
 * -12345678-+---------------+----------------------------
 *       0   | USB0          | COM20 enable,  COM24 disable
 *       1   | USB0          | COM20 disable, COM24 enable
 * -12345678-+---------------+----------------------------
 *        00 | JTAG          | SH-X2
 *        10 | JTAG          | ARM
 *        01 | JTAG          | -
 *        11 | JTAG          | Boundary Scan
 *-----------+---------------+----------------------------
 */

/*
 * FSI-WM8978
 *
 * this command is required when playback.
 *
 * # amixer set "Headphone" 50
 *
 * this command is required when capture.
 *
 * # amixer set "Input PGA" 15
 * # amixer set "Left Input Mixer MicP" on
 * # amixer set "Left Input Mixer MicN" on
 * # amixer set "Right Input Mixer MicN" on
 * # amixer set "Right Input Mixer MicP" on
 */

/*
 * USB function
 *
 * When you use USB Function,
 * set SW1.6 ON, and connect cable to CN24.
 *
 * USBF needs workaround on R8A7740 chip.
 * These are a little bit complex.
 * see
 *	usbhsf_power_ctrl()
 */

static void __init eva_clock_init(void)
{
	struct clk *system	= clk_get(NULL, "system_clk");
	struct clk *xtal1	= clk_get(NULL, "extal1");
	struct clk *usb24s	= clk_get(NULL, "usb24s");
	struct clk *fsibck	= clk_get(NULL, "fsibck");

	if (IS_ERR(system)	||
	    IS_ERR(xtal1)	||
	    IS_ERR(usb24s)	||
	    IS_ERR(fsibck)) {
		pr_err("armadillo800eva board clock init failed\n");
		goto clock_error;
	}

	/* armadillo 800 eva extal1 is 24MHz */
	clk_set_rate(xtal1, 24000000);

	/* usb24s use extal1 (= system) clock (= 24MHz) */
	clk_set_parent(usb24s, system);

	/* FSIBCK is 12.288MHz, and it is parent of FSI-B */
	clk_set_rate(fsibck, 12288000);

clock_error:
	if (!IS_ERR(system))
		clk_put(system);
	if (!IS_ERR(xtal1))
		clk_put(xtal1);
	if (!IS_ERR(usb24s))
		clk_put(usb24s);
	if (!IS_ERR(fsibck))
		clk_put(fsibck);
}

/*
 * board init
 */
static void __init eva_init(void)
{
	r8a7740_clock_init(MD_CK0 | MD_CK2);
	eva_clock_init();

	r8a7740_meram_workaround();

#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, Shared attribute override enable, 32K*8way */
	l2x0_init(IOMEM(0xf0002000), 0x40440000, 0x82000fff);
#endif

	r8a7740_add_standard_devices_dt();

	r8a7740_pm_init();
}

#define RESCNT2 IOMEM(0xe6188020)
static void eva_restart(enum reboot_mode mode, const char *cmd)
{
	/* Do soft power on reset */
	writel(1 << 31, RESCNT2);
}

static const char *eva_boards_compat_dt[] __initdata = {
	"renesas,armadillo800eva-reference",
	NULL,
};

DT_MACHINE_START(ARMADILLO800EVA_DT, "armadillo800eva-reference")
	.map_io		= r8a7740_map_io,
	.init_early	= r8a7740_init_delay,
	.init_irq	= r8a7740_init_irq_of,
	.init_machine	= eva_init,
	.init_time	= shmobile_timer_init,
	.init_late	= shmobile_init_late,
	.dt_compat	= eva_boards_compat_dt,
	.restart	= eva_restart,
MACHINE_END
