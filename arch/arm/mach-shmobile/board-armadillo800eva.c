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
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/sh_eth.h>
#include <linux/videodev2.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <asm/page.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/cache-l2x0.h>
#include <mach/r8a7740.h>
#include <video/sh_mobile_lcdc.h>

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

/* Ether */
static struct sh_eth_plat_data sh_eth_platdata = {
	.phy			= 0x00, /* LAN8710A */
	.edmac_endian		= EDMAC_LITTLE_ENDIAN,
	.register_type		= SH_ETH_REG_GIGABIT,
	.phy_interface		= PHY_INTERFACE_MODE_MII,
};

static struct resource sh_eth_resources[] = {
	{
		.start	= 0xe9a00000,
		.end	= 0xe9a00800 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= 0xe9a01800,
		.end	= 0xe9a02000 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= evt2irq(0x0500),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sh_eth_device = {
	.name = "sh-eth",
	.dev = {
		.platform_data = &sh_eth_platdata,
	},
	.resource = sh_eth_resources,
	.num_resources = ARRAY_SIZE(sh_eth_resources),
};

/* LCDC */
static struct fb_videomode lcdc0_mode = {
	.name		= "AMPIER/AM-800480",
	.xres		= 800,
	.yres		= 480,
	.left_margin	= 88,
	.right_margin	= 40,
	.hsync_len	= 128,
	.upper_margin	= 20,
	.lower_margin	= 5,
	.vsync_len	= 5,
	.sync		= 0,
};

static struct sh_mobile_lcdc_info lcdc0_info = {
	.clock_source	= LCDC_CLK_BUS,
	.ch[0] = {
		.chan		= LCDC_CHAN_MAINLCD,
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.interface_type	= RGB24,
		.clock_divider	= 5,
		.flags		= 0,
		.lcd_modes	= &lcdc0_mode,
		.num_modes	= 1,
		.panel_cfg = {
			.width	= 111,
			.height = 68,
		},
	},
};

static struct resource lcdc0_resources[] = {
	[0] = {
		.name	= "LCD0",
		.start	= 0xfe940000,
		.end	= 0xfe943fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x580),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device lcdc0_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(lcdc0_resources),
	.resource	= lcdc0_resources,
	.id		= 0,
	.dev	= {
		.platform_data	= &lcdc0_info,
		.coherent_dma_mask = ~0,
	},
};

/* GPIO KEY */
#define GPIO_KEY(c, g, d) { .code = c, .gpio = g, .desc = d, .active_low = 1 }

static struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_POWER,	GPIO_PORT99,	"SW1"),
	GPIO_KEY(KEY_BACK,	GPIO_PORT100,	"SW2"),
	GPIO_KEY(KEY_MENU,	GPIO_PORT97,	"SW3"),
	GPIO_KEY(KEY_HOME,	GPIO_PORT98,	"SW4"),
};

static struct gpio_keys_platform_data gpio_key_info = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

static struct platform_device gpio_keys_device = {
	.name   = "gpio-keys",
	.id     = -1,
	.dev    = {
		.platform_data  = &gpio_key_info,
	},
};

/*
 * board devices
 */
static struct platform_device *eva_devices[] __initdata = {
	&lcdc0_device,
	&gpio_keys_device,
	&sh_eth_device,
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

	/* LCDC0 */
	gpio_request(GPIO_FN_LCDC0_SELECT,	NULL);
	gpio_request(GPIO_FN_LCD0_D0,		NULL);
	gpio_request(GPIO_FN_LCD0_D1,		NULL);
	gpio_request(GPIO_FN_LCD0_D2,		NULL);
	gpio_request(GPIO_FN_LCD0_D3,		NULL);
	gpio_request(GPIO_FN_LCD0_D4,		NULL);
	gpio_request(GPIO_FN_LCD0_D5,		NULL);
	gpio_request(GPIO_FN_LCD0_D6,		NULL);
	gpio_request(GPIO_FN_LCD0_D7,		NULL);
	gpio_request(GPIO_FN_LCD0_D8,		NULL);
	gpio_request(GPIO_FN_LCD0_D9,		NULL);
	gpio_request(GPIO_FN_LCD0_D10,		NULL);
	gpio_request(GPIO_FN_LCD0_D11,		NULL);
	gpio_request(GPIO_FN_LCD0_D12,		NULL);
	gpio_request(GPIO_FN_LCD0_D13,		NULL);
	gpio_request(GPIO_FN_LCD0_D14,		NULL);
	gpio_request(GPIO_FN_LCD0_D15,		NULL);
	gpio_request(GPIO_FN_LCD0_D16,		NULL);
	gpio_request(GPIO_FN_LCD0_D17,		NULL);
	gpio_request(GPIO_FN_LCD0_D18_PORT40,	NULL);
	gpio_request(GPIO_FN_LCD0_D19_PORT4,	NULL);
	gpio_request(GPIO_FN_LCD0_D20_PORT3,	NULL);
	gpio_request(GPIO_FN_LCD0_D21_PORT2,	NULL);
	gpio_request(GPIO_FN_LCD0_D22_PORT0,	NULL);
	gpio_request(GPIO_FN_LCD0_D23_PORT1,	NULL);
	gpio_request(GPIO_FN_LCD0_DCK,		NULL);
	gpio_request(GPIO_FN_LCD0_VSYN,		NULL);
	gpio_request(GPIO_FN_LCD0_HSYN,		NULL);
	gpio_request(GPIO_FN_LCD0_DISP,		NULL);
	gpio_request(GPIO_FN_LCD0_LCLK_PORT165,	NULL);

	gpio_request(GPIO_PORT61, NULL); /* LCDDON */
	gpio_direction_output(GPIO_PORT61, 1);

	gpio_request(GPIO_PORT202, NULL); /* LCD0_LED_CONT */
	gpio_direction_output(GPIO_PORT202, 0);

	/* GETHER */
	gpio_request(GPIO_FN_ET_CRS,		NULL);
	gpio_request(GPIO_FN_ET_MDC,		NULL);
	gpio_request(GPIO_FN_ET_MDIO,		NULL);
	gpio_request(GPIO_FN_ET_TX_ER,		NULL);
	gpio_request(GPIO_FN_ET_RX_ER,		NULL);
	gpio_request(GPIO_FN_ET_ERXD0,		NULL);
	gpio_request(GPIO_FN_ET_ERXD1,		NULL);
	gpio_request(GPIO_FN_ET_ERXD2,		NULL);
	gpio_request(GPIO_FN_ET_ERXD3,		NULL);
	gpio_request(GPIO_FN_ET_TX_CLK,		NULL);
	gpio_request(GPIO_FN_ET_TX_EN,		NULL);
	gpio_request(GPIO_FN_ET_ETXD0,		NULL);
	gpio_request(GPIO_FN_ET_ETXD1,		NULL);
	gpio_request(GPIO_FN_ET_ETXD2,		NULL);
	gpio_request(GPIO_FN_ET_ETXD3,		NULL);
	gpio_request(GPIO_FN_ET_PHY_INT,	NULL);
	gpio_request(GPIO_FN_ET_COL,		NULL);
	gpio_request(GPIO_FN_ET_RX_DV,		NULL);
	gpio_request(GPIO_FN_ET_RX_CLK,		NULL);

	gpio_request(GPIO_PORT18, NULL); /* PHY_RST */
	gpio_direction_output(GPIO_PORT18, 1);

	/*
	 * CAUTION
	 *
	 * DBGMD/LCDC0/FSIA MUX
	 * DBGMD_SELECT_B should be set after setting PFC Function.
	 */
	gpio_request(GPIO_PORT176, NULL);
	gpio_direction_output(GPIO_PORT176, 1);

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
