/*
 * kota2 board support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 * Copyright (C) 2010  Takashi Yoshii <yoshii.takashi.zj@renesas.com>
 * Copyright (C) 2009  Yoshihiro Shimoda <shimoda.yoshihiro@renesas.com>
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
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/smsc911x.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/input/sh_keysc.h>
#include <linux/gpio_keys.h>
#include <mach/hardware.h>
#include <mach/sh73a0.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/traps.h>

static struct resource smsc9220_resources[] = {
	[0] = {
		.start		= 0x14000000, /* CS5A */
		.end		= 0x140000ff, /* A1->A7 */
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= gic_spi(33), /* PINTA2 @ PORT144 */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc9220_platdata = {
	.flags		= SMSC911X_USE_32BIT, /* 32-bit SW on 16-bit HW bus */
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct platform_device eth_device = {
	.name		= "smsc911x",
	.id		= 0,
	.dev  = {
		.platform_data = &smsc9220_platdata,
	},
	.resource	= smsc9220_resources,
	.num_resources	= ARRAY_SIZE(smsc9220_resources),
};

static struct sh_keysc_info keysc_platdata = {
	.mode		= SH_KEYSC_MODE_6,
	.scan_timing	= 3,
	.delay		= 100,
	.keycodes	= {
		KEY_NUMERIC_STAR, KEY_NUMERIC_0, KEY_NUMERIC_POUND,
		0, 0, 0, 0, 0,
		KEY_NUMERIC_7, KEY_NUMERIC_8, KEY_NUMERIC_9,
		0, KEY_DOWN, 0, 0, 0,
		KEY_NUMERIC_4, KEY_NUMERIC_5, KEY_NUMERIC_6,
		KEY_LEFT, KEY_ENTER, KEY_RIGHT, 0, 0,
		KEY_NUMERIC_1, KEY_NUMERIC_2, KEY_NUMERIC_3,
		0, KEY_UP, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
	},
};

static struct resource keysc_resources[] = {
	[0] = {
		.name	= "KEYSC",
		.start	= 0xe61b0000,
		.end	= 0xe61b0098 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(71),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device keysc_device = {
	.name		= "sh_keysc",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(keysc_resources),
	.resource	= keysc_resources,
	.dev		= {
		.platform_data	= &keysc_platdata,
	},
};

#define GPIO_KEY(c, g, d) { .code = c, .gpio = g, .desc = d, .active_low = 1 }

static struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_VOLUMEUP, GPIO_PORT56, "+"), /* S2: VOL+ [IRQ9] */
	GPIO_KEY(KEY_VOLUMEDOWN, GPIO_PORT54, "-"), /* S3: VOL- [IRQ10] */
	GPIO_KEY(KEY_MENU, GPIO_PORT27, "Menu"), /* S4: MENU [IRQ30] */
	GPIO_KEY(KEY_HOMEPAGE, GPIO_PORT26, "Home"), /* S5: HOME [IRQ31] */
	GPIO_KEY(KEY_BACK, GPIO_PORT11, "Back"), /* S6: BACK [IRQ0] */
	GPIO_KEY(KEY_PHONE, GPIO_PORT238, "Tel"), /* S7: TEL [IRQ11] */
	GPIO_KEY(KEY_POWER, GPIO_PORT239, "C1"), /* S8: CAM [IRQ13] */
	GPIO_KEY(KEY_MAIL, GPIO_PORT224, "Mail"), /* S9: MAIL [IRQ3] */
	/* Omitted button "C3?": GPIO_PORT223 - S10: CUST [IRQ8] */
	GPIO_KEY(KEY_CAMERA, GPIO_PORT164, "C2"), /* S11: CAM_HALF [IRQ25] */
	/* Omitted button "?": GPIO_PORT152 - S12: CAM_FULL [No IRQ] */
};

static struct gpio_keys_platform_data gpio_key_info = {
	.buttons        = gpio_buttons,
	.nbuttons       = ARRAY_SIZE(gpio_buttons),
	.poll_interval  = 250, /* polled for now */
};

static struct platform_device gpio_keys_device = {
	.name   = "gpio-keys-polled", /* polled for now */
	.id     = -1,
	.dev    = {
		.platform_data  = &gpio_key_info,
	},
};

static struct platform_device *kota2_devices[] __initdata = {
	&eth_device,
	&keysc_device,
	&gpio_keys_device,
};

static struct map_desc kota2_io_desc[] __initdata = {
	/* create a 1:1 entity map for 0xe6xxxxxx
	 * used by CPGA, INTC and PFC.
	 */
	{
		.virtual	= 0xe6000000,
		.pfn		= __phys_to_pfn(0xe6000000),
		.length		= 256 << 20,
		.type		= MT_DEVICE_NONSHARED
	},
};

static void __init kota2_map_io(void)
{
	iotable_init(kota2_io_desc, ARRAY_SIZE(kota2_io_desc));

	/* setup early devices and console here as well */
	sh73a0_add_early_devices();
	shmobile_setup_console();
}

#define PINTER0A	0xe69000a0
#define PINTCR0A	0xe69000b0

void __init kota2_init_irq(void)
{
	sh73a0_init_irq();

	/* setup PINT: enable PINTA2 as active low */
	__raw_writel(1 << 29, PINTER0A);
	__raw_writew(2 << 10, PINTCR0A);
}

static void __init kota2_init(void)
{
	sh73a0_pinmux_init();

	/* SCIFA2 (UART2) */
	gpio_request(GPIO_FN_SCIFA2_TXD1, NULL);
	gpio_request(GPIO_FN_SCIFA2_RXD1, NULL);
	gpio_request(GPIO_FN_SCIFA2_RTS1_, NULL);
	gpio_request(GPIO_FN_SCIFA2_CTS1_, NULL);

	/* SMSC911X */
	gpio_request(GPIO_FN_D0_NAF0, NULL);
	gpio_request(GPIO_FN_D1_NAF1, NULL);
	gpio_request(GPIO_FN_D2_NAF2, NULL);
	gpio_request(GPIO_FN_D3_NAF3, NULL);
	gpio_request(GPIO_FN_D4_NAF4, NULL);
	gpio_request(GPIO_FN_D5_NAF5, NULL);
	gpio_request(GPIO_FN_D6_NAF6, NULL);
	gpio_request(GPIO_FN_D7_NAF7, NULL);
	gpio_request(GPIO_FN_D8_NAF8, NULL);
	gpio_request(GPIO_FN_D9_NAF9, NULL);
	gpio_request(GPIO_FN_D10_NAF10, NULL);
	gpio_request(GPIO_FN_D11_NAF11, NULL);
	gpio_request(GPIO_FN_D12_NAF12, NULL);
	gpio_request(GPIO_FN_D13_NAF13, NULL);
	gpio_request(GPIO_FN_D14_NAF14, NULL);
	gpio_request(GPIO_FN_D15_NAF15, NULL);
	gpio_request(GPIO_FN_CS5A_, NULL);
	gpio_request(GPIO_FN_WE0__FWE, NULL);
	gpio_request(GPIO_PORT144, NULL); /* PINTA2 */
	gpio_direction_input(GPIO_PORT144);
	gpio_request(GPIO_PORT145, NULL); /* RESET */
	gpio_direction_output(GPIO_PORT145, 1);

	/* KEYSC */
	gpio_request(GPIO_FN_KEYIN0_PU, NULL);
	gpio_request(GPIO_FN_KEYIN1_PU, NULL);
	gpio_request(GPIO_FN_KEYIN2_PU, NULL);
	gpio_request(GPIO_FN_KEYIN3_PU, NULL);
	gpio_request(GPIO_FN_KEYIN4_PU, NULL);
	gpio_request(GPIO_FN_KEYIN5_PU, NULL);
	gpio_request(GPIO_FN_KEYIN6_PU, NULL);
	gpio_request(GPIO_FN_KEYIN7_PU, NULL);
	gpio_request(GPIO_FN_KEYOUT0, NULL);
	gpio_request(GPIO_FN_KEYOUT1, NULL);
	gpio_request(GPIO_FN_KEYOUT2, NULL);
	gpio_request(GPIO_FN_KEYOUT3, NULL);
	gpio_request(GPIO_FN_KEYOUT4, NULL);
	gpio_request(GPIO_FN_KEYOUT5, NULL);
	gpio_request(GPIO_FN_PORT59_KEYOUT6, NULL);
	gpio_request(GPIO_FN_PORT58_KEYOUT7, NULL);
	gpio_request(GPIO_FN_KEYOUT8, NULL);

#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, Shared attribute override enable, 64K*8way */
	l2x0_init(__io(0xf0100000), 0x40460000, 0x82000fff);
#endif
	sh73a0_add_standard_devices();
	platform_add_devices(kota2_devices, ARRAY_SIZE(kota2_devices));
}

static void __init kota2_timer_init(void)
{
	sh73a0_clock_init();
	shmobile_timer.init();
	return;
}

struct sys_timer kota2_timer = {
	.init	= kota2_timer_init,
};

MACHINE_START(KOTA2, "kota2")
	.map_io		= kota2_map_io,
	.init_irq	= kota2_init_irq,
	.handle_irq	= shmobile_handle_irq_gic,
	.init_machine	= kota2_init,
	.timer		= &kota2_timer,
MACHINE_END
