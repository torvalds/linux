/*
 * Handles the Mitac Mio A701 Board
 *
 * Copyright (C) 2008 Robert Jarzmik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/syscore_ops.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/gpio_keys.h>
#include <linux/pwm_backlight.h>
#include <linux/rtc.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pda_power.h>
#include <linux/power_supply.h>
#include <linux/wm97xx.h>
#include <linux/mtd/physmap.h>
#include <linux/usb/gpio_vbus.h>
#include <linux/regulator/max1586.h>
#include <linux/slab.h>
#include <linux/i2c/pxa-i2c.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/pxa27x.h>
#include <mach/regs-rtc.h>
#include <plat/pxa27x_keypad.h>
#include <mach/pxafb.h>
#include <mach/mmc.h>
#include <mach/udc.h>
#include <mach/pxa27x-udc.h>
#include <mach/camera.h>
#include <mach/audio.h>
#include <media/soc_camera.h>

#include <mach/mioa701.h>

#include "generic.h"
#include "devices.h"

static unsigned long mioa701_pin_config[] = {
	/* Mio global */
	MIO_CFG_OUT(GPIO9_CHARGE_EN, AF0, DRIVE_LOW),
	MIO_CFG_OUT(GPIO18_POWEROFF, AF0, DRIVE_LOW),
	MFP_CFG_OUT(GPIO3, AF0, DRIVE_HIGH),
	MFP_CFG_OUT(GPIO4, AF0, DRIVE_HIGH),
	MIO_CFG_IN(GPIO80_MAYBE_CHARGE_VDROP, AF0),

	/* Backlight PWM 0 */
	GPIO16_PWM0_OUT,

	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
	MIO_CFG_IN(GPIO78_SDIO_RO, AF0),
	MIO_CFG_IN(GPIO15_SDIO_INSERT, AF0),
	MIO_CFG_OUT(GPIO91_SDIO_EN, AF0, DRIVE_LOW),

	/* USB */
	MIO_CFG_IN(GPIO13_nUSB_DETECT, AF0),
	MIO_CFG_OUT(GPIO22_USB_ENABLE, AF0, DRIVE_LOW),

	/* LCD */
	GPIOxx_LCD_TFT_16BPP,

	/* QCI */
	GPIO12_CIF_DD_7,
	GPIO17_CIF_DD_6,
	GPIO50_CIF_DD_3,
	GPIO51_CIF_DD_2,
	GPIO52_CIF_DD_4,
	GPIO53_CIF_MCLK,
	GPIO54_CIF_PCLK,
	GPIO55_CIF_DD_1,
	GPIO81_CIF_DD_0,
	GPIO82_CIF_DD_5,
	GPIO84_CIF_FV,
	GPIO85_CIF_LV,

	/* Bluetooth */
	MIO_CFG_IN(GPIO14_BT_nACTIVITY, AF0),
	GPIO44_BTUART_CTS,
	GPIO42_BTUART_RXD,
	GPIO45_BTUART_RTS,
	GPIO43_BTUART_TXD,
	MIO_CFG_OUT(GPIO83_BT_ON, AF0, DRIVE_LOW),
	MIO_CFG_OUT(GPIO77_BT_UNKNOWN1, AF0, DRIVE_HIGH),
	MIO_CFG_OUT(GPIO86_BT_MAYBE_nRESET, AF0, DRIVE_HIGH),

	/* GPS */
	MIO_CFG_OUT(GPIO23_GPS_UNKNOWN1, AF0, DRIVE_LOW),
	MIO_CFG_OUT(GPIO26_GPS_ON, AF0, DRIVE_LOW),
	MIO_CFG_OUT(GPIO27_GPS_RESET, AF0, DRIVE_LOW),
	MIO_CFG_OUT(GPIO106_GPS_UNKNOWN2, AF0, DRIVE_LOW),
	MIO_CFG_OUT(GPIO107_GPS_UNKNOWN3, AF0, DRIVE_LOW),
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,

	/* GSM */
	MIO_CFG_OUT(GPIO24_GSM_MOD_RESET_CMD, AF0, DRIVE_LOW),
	MIO_CFG_OUT(GPIO88_GSM_nMOD_ON_CMD, AF0, DRIVE_HIGH),
	MIO_CFG_OUT(GPIO90_GSM_nMOD_OFF_CMD, AF0, DRIVE_HIGH),
	MIO_CFG_OUT(GPIO114_GSM_nMOD_DTE_UART_STATE, AF0, DRIVE_HIGH),
	MIO_CFG_IN(GPIO25_GSM_MOD_ON_STATE, AF0),
	MIO_CFG_IN(GPIO113_GSM_EVENT, AF0) | WAKEUP_ON_EDGE_BOTH,
	GPIO34_FFUART_RXD,
	GPIO35_FFUART_CTS,
	GPIO36_FFUART_DCD,
	GPIO37_FFUART_DSR,
	GPIO39_FFUART_TXD,
	GPIO40_FFUART_DTR,
	GPIO41_FFUART_RTS,

	/* Sound */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO89_AC97_SYSCLK,
	MIO_CFG_IN(GPIO12_HPJACK_INSERT, AF0),

	/* Leds */
	MIO_CFG_OUT(GPIO10_LED_nCharging, AF0, DRIVE_HIGH),
	MIO_CFG_OUT(GPIO97_LED_nBlue, AF0, DRIVE_HIGH),
	MIO_CFG_OUT(GPIO98_LED_nOrange, AF0, DRIVE_HIGH),
	MIO_CFG_OUT(GPIO82_LED_nVibra, AF0, DRIVE_HIGH),
	MIO_CFG_OUT(GPIO115_LED_nKeyboard, AF0, DRIVE_HIGH),

	/* Keyboard */
	MIO_CFG_IN(GPIO0_KEY_POWER, AF0) | WAKEUP_ON_EDGE_BOTH,
	MIO_CFG_IN(GPIO93_KEY_VOLUME_UP, AF0),
	MIO_CFG_IN(GPIO94_KEY_VOLUME_DOWN, AF0),
	GPIO100_KP_MKIN_0,
	GPIO101_KP_MKIN_1,
	GPIO102_KP_MKIN_2,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* Unknown */
	MFP_CFG_IN(GPIO20, AF0),
	MFP_CFG_IN(GPIO21, AF0),
	MFP_CFG_IN(GPIO33, AF0),
	MFP_CFG_OUT(GPIO49, AF0, DRIVE_HIGH),
	MFP_CFG_OUT(GPIO57, AF0, DRIVE_HIGH),
	MFP_CFG_IN(GPIO96, AF0),
	MFP_CFG_OUT(GPIO116, AF0, DRIVE_HIGH),
};

/* LCD Screen and Backlight */
static struct platform_pwm_backlight_data mioa701_backlight_data = {
	.pwm_id		= 0,
	.max_brightness	= 100,
	.dft_brightness	= 50,
	.pwm_period_ns	= 4000 * 1024,	/* Fl = 250kHz */
};

/*
 * LTM0305A776C LCD panel timings
 *
 * see:
 *  - the LTM0305A776C datasheet,
 *  - and the PXA27x Programmers' manual
 */
static struct pxafb_mode_info mioa701_ltm0305a776c = {
	.pixclock		= 220000,	/* CLK=4.545 MHz */
	.xres			= 240,
	.yres			= 320,
	.bpp			= 16,
	.hsync_len		= 4,
	.vsync_len		= 2,
	.left_margin		= 6,
	.right_margin		= 4,
	.upper_margin		= 5,
	.lower_margin		= 3,
};

static void mioa701_lcd_power(int on, struct fb_var_screeninfo *si)
{
	gpio_set_value(GPIO87_LCD_POWER, on);
}

static struct pxafb_mach_info mioa701_pxafb_info = {
	.modes			= &mioa701_ltm0305a776c,
	.num_modes		= 1,
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL,
	.pxafb_lcd_power	= mioa701_lcd_power,
};

/*
 * Keyboard configuration
 */
static unsigned int mioa701_matrix_keys[] = {
	KEY(0, 0, KEY_UP),
	KEY(0, 1, KEY_RIGHT),
	KEY(0, 2, KEY_MEDIA),
	KEY(1, 0, KEY_DOWN),
	KEY(1, 1, KEY_ENTER),
	KEY(1, 2, KEY_CONNECT),	/* GPS key */
	KEY(2, 0, KEY_LEFT),
	KEY(2, 1, KEY_PHONE),	/* Phone Green key */
	KEY(2, 2, KEY_CAMERA)	/* Camera key */
};
static struct pxa27x_keypad_platform_data mioa701_keypad_info = {
	.matrix_key_rows = 3,
	.matrix_key_cols = 3,
	.matrix_key_map = mioa701_matrix_keys,
	.matrix_key_map_size = ARRAY_SIZE(mioa701_matrix_keys),
};

/*
 * GPIO Key Configuration
 */
#define MIO_KEY(key, _gpio, _desc, _wakeup) \
	{ .code = (key), .gpio = (_gpio), .active_low = 0, \
	.desc = (_desc), .type = EV_KEY, .wakeup = (_wakeup) }
static struct gpio_keys_button mioa701_button_table[] = {
	MIO_KEY(KEY_EXIT, GPIO0_KEY_POWER, "Power button", 1),
	MIO_KEY(KEY_VOLUMEUP, GPIO93_KEY_VOLUME_UP, "Volume up", 0),
	MIO_KEY(KEY_VOLUMEDOWN, GPIO94_KEY_VOLUME_DOWN, "Volume down", 0),
	MIO_KEY(KEY_HP, GPIO12_HPJACK_INSERT, "HP jack detect", 0)
};

static struct gpio_keys_platform_data mioa701_gpio_keys_data = {
	.buttons  = mioa701_button_table,
	.nbuttons = ARRAY_SIZE(mioa701_button_table),
};

/*
 * Leds and vibrator
 */
#define ONE_LED(_gpio, _name) \
{ .gpio = (_gpio), .name = (_name), .active_low = true }
static struct gpio_led gpio_leds[] = {
	ONE_LED(GPIO10_LED_nCharging, "mioa701:charging"),
	ONE_LED(GPIO97_LED_nBlue, "mioa701:blue"),
	ONE_LED(GPIO98_LED_nOrange, "mioa701:orange"),
	ONE_LED(GPIO82_LED_nVibra, "mioa701:vibra"),
	ONE_LED(GPIO115_LED_nKeyboard, "mioa701:keyboard")
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds = gpio_leds,
	.num_leds = ARRAY_SIZE(gpio_leds),
};

/*
 * GSM Sagem XS200 chip
 *
 * GSM handling was purged from kernel. For history, this is the way to go :
 *   - init : GPIO24_GSM_MOD_RESET_CMD = 0, GPIO114_GSM_nMOD_DTE_UART_STATE = 1
 *            GPIO88_GSM_nMOD_ON_CMD = 1, GPIO90_GSM_nMOD_OFF_CMD = 1
 *   - reset : GPIO24_GSM_MOD_RESET_CMD = 1, msleep(100),
 *             GPIO24_GSM_MOD_RESET_CMD = 0
 *   - turn on  : GPIO88_GSM_nMOD_ON_CMD = 0, msleep(1000),
 *                GPIO88_GSM_nMOD_ON_CMD = 1
 *   - turn off : GPIO90_GSM_nMOD_OFF_CMD = 0, msleep(1000),
 *                GPIO90_GSM_nMOD_OFF_CMD = 1
 */
static int is_gsm_on(void)
{
	int is_on;

	is_on = !!gpio_get_value(GPIO25_GSM_MOD_ON_STATE);
	return is_on;
}

irqreturn_t gsm_on_irq(int irq, void *p)
{
	printk(KERN_DEBUG "Mioa701: GSM status changed to %s\n",
	       is_gsm_on() ? "on" : "off");
	return IRQ_HANDLED;
}

static struct gpio gsm_gpios[] = {
	{ GPIO25_GSM_MOD_ON_STATE, GPIOF_IN, "GSM state" },
	{ GPIO113_GSM_EVENT, GPIOF_IN, "GSM event" },
};

static int __init gsm_init(void)
{
	int rc;

	rc = gpio_request_array(ARRAY_AND_SIZE(gsm_gpios));
	if (rc)
		goto err_gpio;
	rc = request_irq(gpio_to_irq(GPIO25_GSM_MOD_ON_STATE), gsm_on_irq,
			 IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			 "GSM XS200 Power Irq", NULL);
	if (rc)
		goto err_irq;

	gpio_set_wake(GPIO113_GSM_EVENT, 1);
	return 0;

err_irq:
	printk(KERN_ERR "Mioa701: Can't request GSM_ON irq\n");
	gpio_free_array(ARRAY_AND_SIZE(gsm_gpios));
err_gpio:
	printk(KERN_ERR "Mioa701: gsm not available\n");
	return rc;
}

static void gsm_exit(void)
{
	free_irq(gpio_to_irq(GPIO25_GSM_MOD_ON_STATE), NULL);
	gpio_free_array(ARRAY_AND_SIZE(gsm_gpios));
}

/*
 * Bluetooth BRF6150 chip
 *
 * BT handling was purged from kernel. For history, this is the way to go :
 * - turn on  : GPIO83_BT_ON = 1
 * - turn off : GPIO83_BT_ON = 0
 */

/*
 * GPS Sirf Star III chip
 *
 * GPS handling was purged from kernel. For history, this is the way to go :
 * - init : GPIO23_GPS_UNKNOWN1 = 1, GPIO26_GPS_ON = 0, GPIO27_GPS_RESET = 0
 *          GPIO106_GPS_UNKNOWN2 = 0, GPIO107_GPS_UNKNOWN3 = 0
 * - turn on  : GPIO27_GPS_RESET = 1, GPIO26_GPS_ON = 1
 * - turn off : GPIO26_GPS_ON = 0, GPIO27_GPS_RESET = 0
 */

/*
 * USB UDC
 */
static int is_usb_connected(void)
{
	return !gpio_get_value(GPIO13_nUSB_DETECT);
}

static struct pxa2xx_udc_mach_info mioa701_udc_info = {
	.udc_is_connected = is_usb_connected,
	.gpio_pullup	  = GPIO22_USB_ENABLE,
};

struct gpio_vbus_mach_info gpio_vbus_data = {
	.gpio_vbus = GPIO13_nUSB_DETECT,
	.gpio_vbus_inverted = 1,
	.gpio_pullup = -1,
};

/*
 * SDIO/MMC Card controller
 */
/**
 * The card detect interrupt isn't debounced so we delay it by 250ms
 * to give the card a chance to fully insert/eject.
 */
static struct pxamci_platform_data mioa701_mci_info = {
	.detect_delay_ms	= 250,
	.ocr_mask 		= MMC_VDD_32_33 | MMC_VDD_33_34,
	.gpio_card_detect	= GPIO15_SDIO_INSERT,
	.gpio_card_ro		= GPIO78_SDIO_RO,
	.gpio_power		= GPIO91_SDIO_EN,
};

/* FlashRAM */
static struct resource strataflash_resource = {
	.start = PXA_CS0_PHYS,
	.end   = PXA_CS0_PHYS + SZ_64M - 1,
	.flags = IORESOURCE_MEM,
};

static struct physmap_flash_data strataflash_data = {
	.width = 2,
	/* .set_vpp = mioa701_set_vpp, */
};

static struct platform_device strataflash = {
	.name	       = "physmap-flash",
	.id	       = -1,
	.resource      = &strataflash_resource,
	.num_resources = 1,
	.dev = {
		.platform_data = &strataflash_data,
	},
};

/*
 * Suspend/Resume bootstrap management
 *
 * MIO A701 reboot sequence is highly ROM dependent. From the one dissassembled,
 * this sequence is as follows :
 *   - disables interrupts
 *   - initialize SDRAM (self refresh RAM into active RAM)
 *   - initialize GPIOs (depends on value at 0xa020b020)
 *   - initialize coprossessors
 *   - if edge detect on PWR_SCL(GPIO3), then proceed to cold start
 *   - or if value at 0xa020b000 not equal to 0x0f0f0f0f, proceed to cold start
 *   - else do a resume, ie. jump to addr 0xa0100000
 */
#define RESUME_ENABLE_ADDR	0xa020b000
#define RESUME_ENABLE_VAL	0x0f0f0f0f
#define RESUME_BT_ADDR		0xa020b020
#define RESUME_UNKNOWN_ADDR	0xa020b024
#define RESUME_VECTOR_ADDR	0xa0100000
#define BOOTSTRAP_WORDS		mioa701_bootstrap_lg/4

static u32 *save_buffer;

static void install_bootstrap(void)
{
	int i;
	u32 *rom_bootstrap  = phys_to_virt(RESUME_VECTOR_ADDR);
	u32 *src = &mioa701_bootstrap;

	for (i = 0; i < BOOTSTRAP_WORDS; i++)
		rom_bootstrap[i] = src[i];
}


static int mioa701_sys_suspend(void)
{
	int i = 0, is_bt_on;
	u32 *mem_resume_vector	= phys_to_virt(RESUME_VECTOR_ADDR);
	u32 *mem_resume_enabler = phys_to_virt(RESUME_ENABLE_ADDR);
	u32 *mem_resume_bt	= phys_to_virt(RESUME_BT_ADDR);
	u32 *mem_resume_unknown	= phys_to_virt(RESUME_UNKNOWN_ADDR);

	/* Devices prepare suspend */
	is_bt_on = !!gpio_get_value(GPIO83_BT_ON);
	pxa2xx_mfp_set_lpm(GPIO83_BT_ON,
			   is_bt_on ? MFP_LPM_DRIVE_HIGH : MFP_LPM_DRIVE_LOW);

	for (i = 0; i < BOOTSTRAP_WORDS; i++)
		save_buffer[i] = mem_resume_vector[i];
	save_buffer[i++] = *mem_resume_enabler;
	save_buffer[i++] = *mem_resume_bt;
	save_buffer[i++] = *mem_resume_unknown;

	*mem_resume_enabler = RESUME_ENABLE_VAL;
	*mem_resume_bt	    = is_bt_on;

	install_bootstrap();
	return 0;
}

static void mioa701_sys_resume(void)
{
	int i = 0;
	u32 *mem_resume_vector	= phys_to_virt(RESUME_VECTOR_ADDR);
	u32 *mem_resume_enabler = phys_to_virt(RESUME_ENABLE_ADDR);
	u32 *mem_resume_bt	= phys_to_virt(RESUME_BT_ADDR);
	u32 *mem_resume_unknown	= phys_to_virt(RESUME_UNKNOWN_ADDR);

	for (i = 0; i < BOOTSTRAP_WORDS; i++)
		mem_resume_vector[i] = save_buffer[i];
	*mem_resume_enabler = save_buffer[i++];
	*mem_resume_bt	    = save_buffer[i++];
	*mem_resume_unknown = save_buffer[i++];
}

static struct syscore_ops mioa701_syscore_ops = {
	.suspend	= mioa701_sys_suspend,
	.resume		= mioa701_sys_resume,
};

static int __init bootstrap_init(void)
{
	int save_size = mioa701_bootstrap_lg + (sizeof(u32) * 3);

	register_syscore_ops(&mioa701_syscore_ops);

	save_buffer = kmalloc(save_size, GFP_KERNEL);
	if (!save_buffer)
		return -ENOMEM;
	printk(KERN_INFO "MioA701: allocated %d bytes for bootstrap\n",
	       save_size);
	return 0;
}

static void bootstrap_exit(void)
{
	kfree(save_buffer);
	unregister_syscore_ops(&mioa701_syscore_ops);

	printk(KERN_CRIT "Unregistering mioa701 suspend will hang next"
	       "resume !!!\n");
}

/*
 * Power Supply
 */
static char *supplicants[] = {
	"mioa701_battery"
};

static int is_ac_connected(void)
{
	return gpio_get_value(GPIO96_AC_DETECT);
}

static void mioa701_set_charge(int flags)
{
	gpio_set_value(GPIO9_CHARGE_EN, (flags == PDA_POWER_CHARGE_USB));
}

static struct pda_power_pdata power_pdata = {
	.is_ac_online	= is_ac_connected,
	.is_usb_online	= is_usb_connected,
	.set_charge = mioa701_set_charge,
	.supplied_to = supplicants,
	.num_supplicants = ARRAY_SIZE(supplicants),
};

static struct resource power_resources[] = {
	[0] = {
		.name	= "ac",
		.start	= gpio_to_irq(GPIO96_AC_DETECT),
		.end	= gpio_to_irq(GPIO96_AC_DETECT),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE |
		IORESOURCE_IRQ_LOWEDGE,
	},
	[1] = {
		.name	= "usb",
		.start	= gpio_to_irq(GPIO13_nUSB_DETECT),
		.end	= gpio_to_irq(GPIO13_nUSB_DETECT),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE |
		IORESOURCE_IRQ_LOWEDGE,
	},
};

static struct platform_device power_dev = {
	.name		= "pda-power",
	.id		= -1,
	.resource	= power_resources,
	.num_resources	= ARRAY_SIZE(power_resources),
	.dev = {
		.platform_data	= &power_pdata,
	},
};

static struct wm97xx_batt_pdata mioa701_battery_data = {
	.batt_aux	= WM97XX_AUX_ID1,
	.temp_aux	= -1,
	.charge_gpio	= -1,
	.min_voltage	= 0xc00,
	.max_voltage	= 0xfc0,
	.batt_tech	= POWER_SUPPLY_TECHNOLOGY_LION,
	.batt_div	= 1,
	.batt_mult	= 1,
	.batt_name	= "mioa701_battery",
};

static struct wm97xx_pdata mioa701_wm97xx_pdata = {
	.batt_pdata	= &mioa701_battery_data,
};

/*
 * Voltage regulation
 */
static struct regulator_consumer_supply max1586_consumers[] = {
	{
		.supply = "vcc_core",
	}
};

static struct regulator_init_data max1586_v3_info = {
	.constraints = {
		.name = "vcc_core range",
		.min_uV = 1000000,
		.max_uV = 1705000,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(max1586_consumers),
	.consumer_supplies = max1586_consumers,
};

static struct max1586_subdev_data max1586_subdevs[] = {
	{ .name = "vcc_core", .id = MAX1586_V3,
	  .platform_data = &max1586_v3_info },
};

static struct max1586_platform_data max1586_info = {
	.subdevs = max1586_subdevs,
	.num_subdevs = ARRAY_SIZE(max1586_subdevs),
	.v3_gain = MAX1586_GAIN_NO_R24, /* 700..1475 mV */
};

/*
 * Camera interface
 */
struct pxacamera_platform_data mioa701_pxacamera_platform_data = {
	.flags  = PXA_CAMERA_MASTER | PXA_CAMERA_DATAWIDTH_8 |
		PXA_CAMERA_PCLK_EN | PXA_CAMERA_MCLK_EN,
	.mclk_10khz = 5000,
};

static struct i2c_board_info __initdata mioa701_pi2c_devices[] = {
	{
		I2C_BOARD_INFO("max1586", 0x14),
		.platform_data = &max1586_info,
	},
};

/* Board I2C devices. */
static struct i2c_board_info mioa701_i2c_devices[] = {
	{
		I2C_BOARD_INFO("mt9m111", 0x5d),
	},
};

static struct soc_camera_link iclink = {
	.bus_id		= 0, /* Match id in pxa27x_device_camera in device.c */
	.board_info	= &mioa701_i2c_devices[0],
	.i2c_adapter_id	= 0,
};

struct i2c_pxa_platform_data i2c_pdata = {
	.fast_mode = 1,
};

static pxa2xx_audio_ops_t mioa701_ac97_info = {
	.reset_gpio = 95,
	.codec_pdata = { &mioa701_wm97xx_pdata, },
};

/*
 * Mio global
 */

/* Devices */
#define MIO_PARENT_DEV(var, strname, tparent, pdata)	\
static struct platform_device var = {			\
	.name		= strname,			\
	.id		= -1,				\
	.dev		= {				\
		.platform_data = pdata,			\
		.parent	= tparent,			\
	},						\
};
#define MIO_SIMPLE_DEV(var, strname, pdata)	\
	MIO_PARENT_DEV(var, strname, NULL, pdata)

MIO_SIMPLE_DEV(mioa701_gpio_keys, "gpio-keys",	    &mioa701_gpio_keys_data)
MIO_PARENT_DEV(mioa701_backlight, "pwm-backlight",  &pxa27x_device_pwm0.dev,
		&mioa701_backlight_data);
MIO_SIMPLE_DEV(mioa701_led,	  "leds-gpio",	    &gpio_led_info)
MIO_SIMPLE_DEV(pxa2xx_pcm,	  "pxa2xx-pcm",	    NULL)
MIO_SIMPLE_DEV(mioa701_sound,	  "mioa701-wm9713", NULL)
MIO_SIMPLE_DEV(mioa701_board,	  "mioa701-board",  NULL)
MIO_SIMPLE_DEV(gpio_vbus,	  "gpio-vbus",      &gpio_vbus_data);
MIO_SIMPLE_DEV(mioa701_camera,	  "soc-camera-pdrv",&iclink);

static struct platform_device *devices[] __initdata = {
	&mioa701_gpio_keys,
	&mioa701_backlight,
	&mioa701_led,
	&pxa2xx_pcm,
	&mioa701_sound,
	&power_dev,
	&strataflash,
	&gpio_vbus,
	&mioa701_camera,
	&mioa701_board,
};

static void mioa701_machine_exit(void);

static void mioa701_poweroff(void)
{
	mioa701_machine_exit();
	arm_machine_restart('s', NULL);
}

static void mioa701_restart(char c, const char *cmd)
{
	mioa701_machine_exit();
	arm_machine_restart('s', cmd);
}

static struct gpio global_gpios[] = {
	{ GPIO9_CHARGE_EN, GPIOF_OUT_INIT_HIGH, "Charger enable" },
	{ GPIO18_POWEROFF, GPIOF_OUT_INIT_LOW, "Power Off" },
	{ GPIO87_LCD_POWER, GPIOF_OUT_INIT_LOW, "LCD Power" },
};

static void __init mioa701_machine_init(void)
{
	int rc;

	PSLR  = 0xff100000; /* SYSDEL=125ms, PWRDEL=125ms, PSLR_SL_ROD=1 */
	PCFR = PCFR_DC_EN | PCFR_GPR_EN | PCFR_OPDE;
	RTTR = 32768 - 1; /* Reset crazy WinCE value */
	UP2OCR = UP2OCR_HXOE;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(mioa701_pin_config));
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	rc = gpio_request_array(ARRAY_AND_SIZE(global_gpios));
	if (rc)
		pr_err("MioA701: Failed to request GPIOs: %d", rc);
	bootstrap_init();
	pxa_set_fb_info(NULL, &mioa701_pxafb_info);
	pxa_set_mci_info(&mioa701_mci_info);
	pxa_set_keypad_info(&mioa701_keypad_info);
	pxa_set_udc_info(&mioa701_udc_info);
	pxa_set_ac97_info(&mioa701_ac97_info);
	pm_power_off = mioa701_poweroff;
	arm_pm_restart = mioa701_restart;
	platform_add_devices(devices, ARRAY_SIZE(devices));
	gsm_init();

	i2c_register_board_info(1, ARRAY_AND_SIZE(mioa701_pi2c_devices));
	pxa_set_i2c_info(&i2c_pdata);
	pxa27x_set_i2c_power_info(NULL);
	pxa_set_camera_info(&mioa701_pxacamera_platform_data);
}

static void mioa701_machine_exit(void)
{
	bootstrap_exit();
	gsm_exit();
}

MACHINE_START(MIOA701, "MIO A701")
	.boot_params	= 0xa0000100,
	.map_io		= &pxa27x_map_io,
	.init_irq	= &pxa27x_init_irq,
	.handle_irq	= &pxa27x_handle_irq,
	.init_machine	= mioa701_machine_init,
	.timer		= &pxa_timer,
MACHINE_END
