/*
 * linux/arch/arm/mach-omap1/board-osk.c
 *
 * Board specific init for OMAP5912 OSK
 *
 * Written by Dirk Behme <dirk.behme@de.bosch.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/smc91x.h>
#include <linux/omapfb.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/i2c/tps65010.h>
#include <linux/platform_data/gpio-omap.h>
#include <linux/platform_data/omap1_bl.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/flash.h>
#include <plat/mux.h>
#include <plat/tc.h>

#include <mach/hardware.h>
#include <mach/usb.h>

#include "common.h"

/* At OMAP5912 OSK the Ethernet is directly connected to CS1 */
#define OMAP_OSK_ETHR_START		0x04800300

/* TPS65010 has four GPIOs.  nPG and LED2 can be treated like GPIOs with
 * alternate pin configurations for hardware-controlled blinking.
 */
#define OSK_TPS_GPIO_BASE		(OMAP_MAX_GPIO_LINES + 16 /* MPUIO */)
#	define OSK_TPS_GPIO_USB_PWR_EN	(OSK_TPS_GPIO_BASE + 0)
#	define OSK_TPS_GPIO_LED_D3	(OSK_TPS_GPIO_BASE + 1)
#	define OSK_TPS_GPIO_LAN_RESET	(OSK_TPS_GPIO_BASE + 2)
#	define OSK_TPS_GPIO_DSP_PWR_EN	(OSK_TPS_GPIO_BASE + 3)
#	define OSK_TPS_GPIO_LED_D9	(OSK_TPS_GPIO_BASE + 4)
#	define OSK_TPS_GPIO_LED_D2	(OSK_TPS_GPIO_BASE + 5)

static struct mtd_partition osk_partitions[] = {
	/* bootloader (U-Boot, etc) in first sector */
	{
	      .name		= "bootloader",
	      .offset		= 0,
	      .size		= SZ_128K,
	      .mask_flags	= MTD_WRITEABLE, /* force read-only */
	},
	/* bootloader params in the next sector */
	{
	      .name		= "params",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= SZ_128K,
	      .mask_flags	= 0,
	}, {
	      .name		= "kernel",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= SZ_2M,
	      .mask_flags	= 0
	}, {
	      .name		= "filesystem",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= MTDPART_SIZ_FULL,
	      .mask_flags	= 0
	}
};

static struct physmap_flash_data osk_flash_data = {
	.width		= 2,
	.set_vpp	= omap1_set_vpp,
	.parts		= osk_partitions,
	.nr_parts	= ARRAY_SIZE(osk_partitions),
};

static struct resource osk_flash_resource = {
	/* this is on CS3, wherever it's mapped */
	.flags		= IORESOURCE_MEM,
};

static struct platform_device osk5912_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &osk_flash_data,
	},
	.num_resources	= 1,
	.resource	= &osk_flash_resource,
};

static struct smc91x_platdata osk5912_smc91x_info = {
	.flags	= SMC91X_USE_16BIT | SMC91X_NOWAIT,
	.leda	= RPC_LED_100_10,
	.ledb	= RPC_LED_TX_RX,
};

static struct resource osk5912_smc91x_resources[] = {
	[0] = {
		.start	= OMAP_OSK_ETHR_START,		/* Physical */
		.end	= OMAP_OSK_ETHR_START + 0xf,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

static struct platform_device osk5912_smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.dev	= {
		.platform_data	= &osk5912_smc91x_info,
	},
	.num_resources	= ARRAY_SIZE(osk5912_smc91x_resources),
	.resource	= osk5912_smc91x_resources,
};

static struct resource osk5912_cf_resources[] = {
	[0] = {
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device osk5912_cf_device = {
	.name		= "omap_cf",
	.id		= -1,
	.dev = {
		.platform_data	= (void *) 2 /* CS2 */,
	},
	.num_resources	= ARRAY_SIZE(osk5912_cf_resources),
	.resource	= osk5912_cf_resources,
};

static struct platform_device *osk5912_devices[] __initdata = {
	&osk5912_flash_device,
	&osk5912_smc91x_device,
	&osk5912_cf_device,
};

static struct gpio_led tps_leds[] = {
	/* NOTE:  D9 and D2 have hardware blink support.
	 * Also, D9 requires non-battery power.
	 */
	{ .gpio = OSK_TPS_GPIO_LED_D9, .name = "d9",
			.default_trigger = "ide-disk", },
	{ .gpio = OSK_TPS_GPIO_LED_D2, .name = "d2", },
	{ .gpio = OSK_TPS_GPIO_LED_D3, .name = "d3", .active_low = 1,
			.default_trigger = "heartbeat", },
};

static struct gpio_led_platform_data tps_leds_data = {
	.num_leds	= 3,
	.leds		= tps_leds,
};

static struct platform_device osk5912_tps_leds = {
	.name			= "leds-gpio",
	.id			= 0,
	.dev.platform_data	= &tps_leds_data,
};

static int osk_tps_setup(struct i2c_client *client, void *context)
{
	/* Set GPIO 1 HIGH to disable VBUS power supply;
	 * OHCI driver powers it up/down as needed.
	 */
	gpio_request(OSK_TPS_GPIO_USB_PWR_EN, "n_vbus_en");
	gpio_direction_output(OSK_TPS_GPIO_USB_PWR_EN, 1);

	/* Set GPIO 2 high so LED D3 is off by default */
	tps65010_set_gpio_out_value(GPIO2, HIGH);

	/* Set GPIO 3 low to take ethernet out of reset */
	gpio_request(OSK_TPS_GPIO_LAN_RESET, "smc_reset");
	gpio_direction_output(OSK_TPS_GPIO_LAN_RESET, 0);

	/* GPIO4 is VDD_DSP */
	gpio_request(OSK_TPS_GPIO_DSP_PWR_EN, "dsp_power");
	gpio_direction_output(OSK_TPS_GPIO_DSP_PWR_EN, 1);
	/* REVISIT if DSP support isn't configured, power it off ... */

	/* Let LED1 (D9) blink; leds-gpio may override it */
	tps65010_set_led(LED1, BLINK);

	/* Set LED2 off by default */
	tps65010_set_led(LED2, OFF);

	/* Enable LOW_PWR handshake */
	tps65010_set_low_pwr(ON);

	/* Switch VLDO2 to 3.0V for AIC23 */
	tps65010_config_vregs1(TPS_LDO2_ENABLE | TPS_VLDO2_3_0V
			| TPS_LDO1_ENABLE);

	/* register these three LEDs */
	osk5912_tps_leds.dev.parent = &client->dev;
	platform_device_register(&osk5912_tps_leds);

	return 0;
}

static struct tps65010_board tps_board = {
	.base		= OSK_TPS_GPIO_BASE,
	.outmask	= 0x0f,
	.setup		= osk_tps_setup,
};

static struct i2c_board_info __initdata osk_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("tps65010", 0x48),
		.platform_data	= &tps_board,

	},
	{
		I2C_BOARD_INFO("tlv320aic23", 0x1B),
	},
	/* TODO when driver support is ready:
	 *  - optionally on Mistral, ov9640 camera sensor at 0x30
	 */
};

static void __init osk_init_smc91x(void)
{
	u32 l;

	if ((gpio_request(0, "smc_irq")) < 0) {
		printk("Error requesting gpio 0 for smc91x irq\n");
		return;
	}

	/* Check EMIFS wait states to fix errors with SMC_GET_PKT_HDR */
	l = omap_readl(EMIFS_CCS(1));
	l |= 0x3;
	omap_writel(l, EMIFS_CCS(1));
}

static void __init osk_init_cf(void)
{
	omap_cfg_reg(M7_1610_GPIO62);
	if ((gpio_request(62, "cf_irq")) < 0) {
		printk("Error requesting gpio 62 for CF irq\n");
		return;
	}
	/* the CF I/O IRQ is really active-low */
	irq_set_irq_type(gpio_to_irq(62), IRQ_TYPE_EDGE_FALLING);
}

static struct omap_usb_config osk_usb_config __initdata = {
	/* has usb host connector (A) ... for development it can also
	 * be used, with a NONSTANDARD gender-bending cable/dongle, as
	 * a peripheral.
	 */
#ifdef	CONFIG_USB_GADGET_OMAP
	.register_dev	= 1,
	.hmc_mode	= 0,
#else
	.register_host	= 1,
	.hmc_mode	= 16,
	.rwc		= 1,
#endif
	.pins[0]	= 2,
};

#ifdef	CONFIG_OMAP_OSK_MISTRAL
static struct omap_lcd_config osk_lcd_config __initdata = {
	.ctrl_name	= "internal",
};
#endif

#ifdef	CONFIG_OMAP_OSK_MISTRAL

#include <linux/input.h>
#include <linux/i2c/at24.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>

#include <plat/keypad.h>

static struct at24_platform_data at24c04 = {
	.byte_len	= SZ_4K / 8,
	.page_size	= 16,
};

static struct i2c_board_info __initdata mistral_i2c_board_info[] = {
	{
		/* NOTE:  powered from LCD supply */
		I2C_BOARD_INFO("24c04", 0x50),
		.platform_data	= &at24c04,
	},
	/* TODO when driver support is ready:
	 *  - optionally ov9640 camera sensor at 0x30
	 */
};

static const unsigned int osk_keymap[] = {
	/* KEY(col, row, code) */
	KEY(0, 0, KEY_F1),		/* SW4 */
	KEY(3, 0, KEY_UP),		/* (sw2/up) */
	KEY(1, 1, KEY_LEFTCTRL),	/* SW5 */
	KEY(2, 1, KEY_LEFT),		/* (sw2/left) */
	KEY(0, 2, KEY_SPACE),		/* SW3 */
	KEY(1, 2, KEY_ESC),		/* SW6 */
	KEY(2, 2, KEY_DOWN),		/* (sw2/down) */
	KEY(2, 3, KEY_ENTER),		/* (sw2/select) */
	KEY(3, 3, KEY_RIGHT),		/* (sw2/right) */
};

static const struct matrix_keymap_data osk_keymap_data = {
	.keymap		= osk_keymap,
	.keymap_size	= ARRAY_SIZE(osk_keymap),
};

static struct omap_kp_platform_data osk_kp_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap_data	= &osk_keymap_data,
	.delay		= 9,
};

static struct resource osk5912_kp_resources[] = {
	[0] = {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device osk5912_kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &osk_kp_data,
	},
	.num_resources	= ARRAY_SIZE(osk5912_kp_resources),
	.resource	= osk5912_kp_resources,
};

static struct omap_backlight_config mistral_bl_data = {
	.default_intensity	= 0xa0,
};

static struct platform_device mistral_bl_device = {
	.name		= "omap-bl",
	.id		= -1,
	.dev		= {
		.platform_data = &mistral_bl_data,
	},
};

static struct platform_device osk5912_lcd_device = {
	.name		= "lcd_osk",
	.id		= -1,
};

static struct platform_device *mistral_devices[] __initdata = {
	&osk5912_kp_device,
	&mistral_bl_device,
	&osk5912_lcd_device,
};

static int mistral_get_pendown_state(void)
{
	return !gpio_get_value(4);
}

static const struct ads7846_platform_data mistral_ts_info = {
	.model			= 7846,
	.vref_delay_usecs	= 100,	/* internal, no capacitor */
	.x_plate_ohms		= 419,
	.y_plate_ohms		= 486,
	.get_pendown_state	= mistral_get_pendown_state,
};

static struct spi_board_info __initdata mistral_boardinfo[] = { {
	/* MicroWire (bus 2) CS0 has an ads7846e */
	.modalias		= "ads7846",
	.platform_data		= &mistral_ts_info,
	.max_speed_hz		= 120000 /* max sample rate at 3V */
					* 26 /* command + data + overhead */,
	.bus_num		= 2,
	.chip_select		= 0,
} };

#ifdef	CONFIG_PM
static irqreturn_t
osk_mistral_wake_interrupt(int irq, void *ignored)
{
	return IRQ_HANDLED;
}
#endif

static void __init osk_mistral_init(void)
{
	/* NOTE:  we could actually tell if there's a Mistral board
	 * attached, e.g. by trying to read something from the ads7846.
	 * But this arch_init() code is too early for that, since we
	 * can't talk to the ads or even the i2c eeprom.
	 */

	/* parallel camera interface */
	omap_cfg_reg(J15_1610_CAM_LCLK);
	omap_cfg_reg(J18_1610_CAM_D7);
	omap_cfg_reg(J19_1610_CAM_D6);
	omap_cfg_reg(J14_1610_CAM_D5);
	omap_cfg_reg(K18_1610_CAM_D4);
	omap_cfg_reg(K19_1610_CAM_D3);
	omap_cfg_reg(K15_1610_CAM_D2);
	omap_cfg_reg(K14_1610_CAM_D1);
	omap_cfg_reg(L19_1610_CAM_D0);
	omap_cfg_reg(L18_1610_CAM_VS);
	omap_cfg_reg(L15_1610_CAM_HS);
	omap_cfg_reg(M19_1610_CAM_RSTZ);
	omap_cfg_reg(Y15_1610_CAM_OUTCLK);

	/* serial camera interface */
	omap_cfg_reg(H19_1610_CAM_EXCLK);
	omap_cfg_reg(W13_1610_CCP_CLKM);
	omap_cfg_reg(Y12_1610_CCP_CLKP);
	/* CCP_DATAM CONFLICTS WITH UART1.TX (and serial console) */
	/* omap_cfg_reg(Y14_1610_CCP_DATAM); */
	omap_cfg_reg(W14_1610_CCP_DATAP);

	/* CAM_PWDN */
	if (gpio_request(11, "cam_pwdn") == 0) {
		omap_cfg_reg(N20_1610_GPIO11);
		gpio_direction_output(11, 0);
	} else
		pr_debug("OSK+Mistral: CAM_PWDN is awol\n");


	/* omap_cfg_reg(P19_1610_GPIO6); */	/* BUSY */
	gpio_request(6, "ts_busy");
	gpio_direction_input(6);

	omap_cfg_reg(P20_1610_GPIO4);	/* PENIRQ */
	gpio_request(4, "ts_int");
	gpio_direction_input(4);
	irq_set_irq_type(gpio_to_irq(4), IRQ_TYPE_EDGE_FALLING);

	mistral_boardinfo[0].irq = gpio_to_irq(4);
	spi_register_board_info(mistral_boardinfo,
			ARRAY_SIZE(mistral_boardinfo));

	/* the sideways button (SW1) is for use as a "wakeup" button
	 *
	 * NOTE:  The Mistral board has the wakeup button (SW1) wired
	 * to the LCD 3.3V rail, which is powered down during suspend.
	 * To allow this button to wake up the omap, work around this
	 * HW bug by rewiring SW1 to use the main 3.3V rail.
	 */
	omap_cfg_reg(N15_1610_MPUIO2);
	if (gpio_request(OMAP_MPUIO(2), "wakeup") == 0) {
		int ret = 0;
		int irq = gpio_to_irq(OMAP_MPUIO(2));

		gpio_direction_input(OMAP_MPUIO(2));
		irq_set_irq_type(irq, IRQ_TYPE_EDGE_RISING);
#ifdef	CONFIG_PM
		/* share the IRQ in case someone wants to use the
		 * button for more than wakeup from system sleep.
		 */
		ret = request_irq(irq,
				&osk_mistral_wake_interrupt,
				IRQF_SHARED, "mistral_wakeup",
				&osk_mistral_wake_interrupt);
		if (ret != 0) {
			gpio_free(OMAP_MPUIO(2));
			printk(KERN_ERR "OSK+Mistral: no wakeup irq, %d?\n",
				ret);
		} else
			enable_irq_wake(irq);
#endif
	} else
		printk(KERN_ERR "OSK+Mistral: wakeup button is awol\n");

	/* LCD:  backlight, and power; power controls other devices on the
	 * board, like the touchscreen, EEPROM, and wakeup (!) switch.
	 */
	omap_cfg_reg(PWL);
	if (gpio_request(2, "lcd_pwr") == 0)
		gpio_direction_output(2, 1);

	i2c_register_board_info(1, mistral_i2c_board_info,
			ARRAY_SIZE(mistral_i2c_board_info));

	platform_add_devices(mistral_devices, ARRAY_SIZE(mistral_devices));
}
#else
static void __init osk_mistral_init(void) { }
#endif

#define EMIFS_CS3_VAL	(0x88013141)

static void __init osk_init(void)
{
	u32 l;

	osk_init_smc91x();
	osk_init_cf();

	/* Workaround for wrong CS3 (NOR flash) timing
	 * There are some U-Boot versions out there which configure
	 * wrong CS3 memory timings. This mainly leads to CRC
	 * or similar errors if you use NOR flash (e.g. with JFFS2)
	 */
	l = omap_readl(EMIFS_CCS(3));
	if (l != EMIFS_CS3_VAL)
		omap_writel(EMIFS_CS3_VAL, EMIFS_CCS(3));

	osk_flash_resource.end = osk_flash_resource.start = omap_cs3_phys();
	osk_flash_resource.end += SZ_32M - 1;
	osk5912_smc91x_resources[1].start = gpio_to_irq(0);
	osk5912_smc91x_resources[1].end = gpio_to_irq(0);
	osk5912_cf_resources[0].start = gpio_to_irq(62);
	osk5912_cf_resources[0].end = gpio_to_irq(62);
	platform_add_devices(osk5912_devices, ARRAY_SIZE(osk5912_devices));

	l = omap_readl(USB_TRANSCEIVER_CTRL);
	l |= (3 << 1);
	omap_writel(l, USB_TRANSCEIVER_CTRL);

	omap1_usb_init(&osk_usb_config);

	/* irq for tps65010 chip */
	/* bootloader effectively does:  omap_cfg_reg(U19_1610_MPUIO1); */
	if (gpio_request(OMAP_MPUIO(1), "tps65010") == 0)
		gpio_direction_input(OMAP_MPUIO(1));

	omap_serial_init();
	osk_i2c_board_info[0].irq = gpio_to_irq(OMAP_MPUIO(1));
	omap_register_i2c_bus(1, 400, osk_i2c_board_info,
			      ARRAY_SIZE(osk_i2c_board_info));
	osk_mistral_init();

#ifdef	CONFIG_OMAP_OSK_MISTRAL
	omapfb_set_lcd_config(&osk_lcd_config);
#endif

}

MACHINE_START(OMAP_OSK, "TI-OSK")
	/* Maintainer: Dirk Behme <dirk.behme@de.bosch.com> */
	.atag_offset	= 0x100,
	.map_io		= omap16xx_map_io,
	.init_early	= omap1_init_early,
	.reserve	= omap_reserve,
	.init_irq	= omap1_init_irq,
	.init_machine	= osk_init,
	.init_late	= omap1_init_late,
	.timer		= &omap1_timer,
	.restart	= omap1_restart,
MACHINE_END
