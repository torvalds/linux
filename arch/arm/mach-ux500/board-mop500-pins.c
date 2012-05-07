/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <linux/string.h>
#include <linux/pinctrl/machine.h>

#include <asm/mach-types.h>
#include <plat/pincfg.h>
#include <plat/gpio-nomadik.h>

#include <mach/hardware.h>

#include "pins-db8500.h"
#include "board-mop500.h"

enum custom_pin_cfg_t {
	PINS_FOR_DEFAULT,
	PINS_FOR_U9500,
};

static enum custom_pin_cfg_t pinsfor;

/* These simply sets bias for pins */
#define BIAS(a,b) static unsigned long a[] = { b }

BIAS(pd, PIN_PULL_DOWN);
BIAS(slpm_gpio_nopull, PIN_SLPM_GPIO|PIN_SLPM_INPUT_NOPULL);
BIAS(in_nopull, PIN_INPUT_NOPULL);
BIAS(in_pu, PIN_INPUT_PULLUP);
BIAS(in_pd, PIN_INPUT_PULLDOWN);
BIAS(in_pd_slpm_in_pu, PIN_INPUT_PULLDOWN|PIN_SLPM_INPUT_PULLUP);
BIAS(in_pu_slpm_out_lo, PIN_INPUT_PULLUP|PIN_SLPM_OUTPUT_LOW);
BIAS(out_hi, PIN_OUTPUT_HIGH);
BIAS(out_lo, PIN_OUTPUT_LOW);
/* These also force them into GPIO mode */
BIAS(gpio_in_pu, PIN_INPUT_PULLUP|PIN_GPIOMODE_ENABLED);
BIAS(gpio_in_pd, PIN_INPUT_PULLDOWN|PIN_GPIOMODE_ENABLED);
BIAS(gpio_in_pu_slpm_gpio_nopull, PIN_INPUT_PULLUP|PIN_GPIOMODE_ENABLED|PIN_SLPM_GPIO|PIN_SLPM_INPUT_NOPULL);
BIAS(gpio_in_pd_slpm_gpio_nopull, PIN_INPUT_PULLDOWN|PIN_GPIOMODE_ENABLED|PIN_SLPM_GPIO|PIN_SLPM_INPUT_NOPULL);
BIAS(gpio_out_hi, PIN_OUTPUT_HIGH|PIN_GPIOMODE_ENABLED);
BIAS(gpio_out_lo, PIN_OUTPUT_LOW|PIN_GPIOMODE_ENABLED);

/* We use these to define hog settings that are always done on boot */
#define DB8500_MUX_HOG(group,func) \
	PIN_MAP_MUX_GROUP_HOG_DEFAULT("pinctrl-db8500", group, func)
#define DB8500_PIN_HOG(pin,conf) \
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT("pinctrl-db8500", pin, conf)

/* These are default states associated with device and changed runtime */
#define DB8500_MUX(group,func,dev) \
	PIN_MAP_MUX_GROUP_DEFAULT(dev, "pinctrl-db8500", group, func)
#define DB8500_PIN(pin,conf,dev) \
	PIN_MAP_CONFIGS_PIN_DEFAULT(dev, "pinctrl-db8500", pin, conf)

/* Pin control settings */
static struct pinctrl_map __initdata mop500_family_pinmap[] = {
	/*
	 * uMSP0, mux in 4 pins, regular placement of RX/TX
	 * explicitly set the pins to no pull
	 */
	DB8500_MUX_HOG("msp0txrx_a_1", "msp0"),
	DB8500_MUX_HOG("msp0tfstck_a_1", "msp0"),
	DB8500_PIN_HOG("GPIO12_AC4", in_nopull), /* TXD */
	DB8500_PIN_HOG("GPIO15_AC3", in_nopull), /* RXD */
	DB8500_PIN_HOG("GPIO13_AF3", in_nopull), /* TFS */
	DB8500_PIN_HOG("GPIO14_AE3", in_nopull), /* TCK */
	/* MSP2 for HDMI, pull down TXD, TCK, TFS  */
	DB8500_MUX_HOG("msp2_a_1", "msp2"),
	DB8500_PIN_HOG("GPIO193_AH27", in_pd), /* TXD */
	DB8500_PIN_HOG("GPIO194_AF27", in_pd), /* TCK */
	DB8500_PIN_HOG("GPIO195_AG28", in_pd), /* TFS */
	DB8500_PIN_HOG("GPIO196_AG26", out_lo), /* RXD */
	/*
	 * LCD, set TE0 (using LCD VSI0) and D14 (touch screen interrupt) to
	 * pull-up
	 * TODO: is this really correct? Snowball doesn't have a LCD.
	 */
	DB8500_MUX_HOG("lcdvsi0_a_1", "lcd"),
	DB8500_PIN_HOG("GPIO68_E1", in_pu),
	DB8500_PIN_HOG("GPIO84_C2", gpio_in_pu),
	/*
	 * STMPE1601/tc35893 keypad IRQ GPIO 218
	 * TODO: set for snowball and HREF really??
	 */
	DB8500_PIN_HOG("GPIO218_AH11", gpio_in_pu),
	/*
	 * UART0, we do not mux in u0 here.
	 * uart-0 pins gpio configuration should be kept intact to prevent
	 * a glitch in tx line when the tty dev is opened. Later these pins
	 * are configured to uart mop500_pins_uart0
	 */
	DB8500_PIN_HOG("GPIO0_AJ5", in_pu), /* CTS */
	DB8500_PIN_HOG("GPIO1_AJ3", out_hi), /* RTS */
	DB8500_PIN_HOG("GPIO2_AH4", in_pu), /* RXD */
	DB8500_PIN_HOG("GPIO3_AH3", out_hi), /* TXD */
	/*
	 * Mux in UART2 on altfunction C and set pull-ups.
	 * TODO: is this used on U8500 variants and Snowball really?
	 * The setting on GPIO31 conflicts with magnetometer use on hrefv60
	 */
	DB8500_MUX_HOG("u2rxtx_c_1", "u2"),
	DB8500_MUX_HOG("u2ctsrts_c_1", "u2"),
	DB8500_PIN_HOG("GPIO29_W2", in_pu), /* RXD */
	DB8500_PIN_HOG("GPIO30_W3", out_hi), /* TXD */
	DB8500_PIN_HOG("GPIO31_V3", in_pu), /* CTS */
	DB8500_PIN_HOG("GPIO32_V2", out_hi), /* RTS */
	/*
	 * The following pin sets were known as "runtime pins" before being
	 * converted to the pinctrl model. Here we model them as "default"
	 * states.
	 */
	/* Mux in LCD data lines 8 thru 11 and LCDA CLK for MCDE TVOUT */
	DB8500_MUX("lcd_d8_d11_a_1", "lcd", "mcde-tvout"),
	DB8500_MUX("lcdaclk_b_1", "lcda", "mcde-tvout"),
	/* Mux in LCD VSI1 and pull it up for MCDE HDMI output */
	DB8500_MUX("lcdvsi1_a_1", "lcd", "av8100-hdmi"),
	/* Mux in I2C blocks, put pins into GPIO in sleepmode no pull-up */
	DB8500_MUX("i2c0_a_1", "i2c0", "nmk-i2c.0"),
	DB8500_PIN("GPIO147_C15", slpm_gpio_nopull, "nmk-i2c.0"),
	DB8500_PIN("GPIO148_B16", slpm_gpio_nopull, "nmk-i2c.0"),
	DB8500_MUX("i2c1_b_2", "i2c1", "nmk-i2c.1"),
	DB8500_PIN("GPIO16_AD3", slpm_gpio_nopull, "nmk-i2c.1"),
	DB8500_PIN("GPIO17_AD4", slpm_gpio_nopull, "nmk-i2c.1"),
	DB8500_MUX("i2c2_b_2", "i2c2", "nmk-i2c.2"),
	DB8500_PIN("GPIO10_AF5", slpm_gpio_nopull, "nmk-i2c.2"),
	DB8500_PIN("GPIO11_AG4", slpm_gpio_nopull, "nmk-i2c.2"),
	DB8500_MUX("i2c3_c_2", "i2c3", "nmk-i2c.3"),
	DB8500_PIN("GPIO229_AG7", slpm_gpio_nopull, "nmk-i2c.3"),
	DB8500_PIN("GPIO230_AF7", slpm_gpio_nopull, "nmk-i2c.3"),
	/* Mux in SDI0 (here called MC0) used for removable MMC/SD/SDIO cards */
	DB8500_MUX("mc0_a_1", "mc0", "sdi0"),
	DB8500_PIN("GPIO18_AC2", out_hi, "sdi0"), /* CMDDIR */
	DB8500_PIN("GPIO19_AC1", out_hi, "sdi0"), /* DAT0DIR */
	DB8500_PIN("GPIO20_AB4", out_hi, "sdi0"), /* DAT2DIR */
	DB8500_PIN("GPIO22_AA3", in_nopull, "sdi0"), /* FBCLK */
	DB8500_PIN("GPIO23_AA4", out_lo, "sdi0"), /* CLK */
	DB8500_PIN("GPIO24_AB2", in_pu, "sdi0"), /* CMD */
	DB8500_PIN("GPIO25_Y4", in_pu, "sdi0"), /* DAT0 */
	DB8500_PIN("GPIO26_Y2", in_pu, "sdi0"), /* DAT1 */
	DB8500_PIN("GPIO27_AA2", in_pu, "sdi0"), /* DAT2 */
	DB8500_PIN("GPIO28_AA1", in_pu, "sdi0"), /* DAT3 */
	/* Mux in SDI1 (here called MC1) used for SDIO for CW1200 WLAN */
	DB8500_MUX("mc1_a_1", "mc1", "sdi1"),
	DB8500_PIN("GPIO208_AH16", out_lo, "sdi1"), /* CLK */
	DB8500_PIN("GPIO209_AG15", in_nopull, "sdi1"), /* FBCLK */
	DB8500_PIN("GPIO210_AJ15", in_pu, "sdi1"), /* CMD */
	DB8500_PIN("GPIO211_AG14", in_pu, "sdi1"), /* DAT0 */
	DB8500_PIN("GPIO212_AF13", in_pu, "sdi1"), /* DAT1 */
	DB8500_PIN("GPIO213_AG13", in_pu, "sdi1"), /* DAT2 */
	DB8500_PIN("GPIO214_AH15", in_pu, "sdi1"), /* DAT3 */
	/* Mux in SDI2 (here called MC2) used for for PoP eMMC */
	DB8500_MUX("mc2_a_1", "mc2", "sdi2"),
	DB8500_PIN("GPIO128_A5", out_lo, "sdi2"), /* CLK */
	DB8500_PIN("GPIO129_B4", in_pu, "sdi2"), /* CMD */
	DB8500_PIN("GPIO130_C8", in_nopull, "sdi2"), /* FBCLK */
	DB8500_PIN("GPIO131_A12", in_pu, "sdi2"), /* DAT0 */
	DB8500_PIN("GPIO132_C10", in_pu, "sdi2"), /* DAT1 */
	DB8500_PIN("GPIO133_B10", in_pu, "sdi2"), /* DAT2 */
	DB8500_PIN("GPIO134_B9", in_pu, "sdi2"), /* DAT3 */
	DB8500_PIN("GPIO135_A9", in_pu, "sdi2"), /* DAT4 */
	DB8500_PIN("GPIO136_C7", in_pu, "sdi2"), /* DAT5 */
	DB8500_PIN("GPIO137_A7", in_pu, "sdi2"), /* DAT6 */
	DB8500_PIN("GPIO138_C5", in_pu, "sdi2"), /* DAT7 */
	/* Mux in SDI4 (here called MC4) used for for PCB-mounted eMMC */
	DB8500_MUX("mc4_a_1", "mc4", "sdi4"),
	DB8500_PIN("GPIO197_AH24", in_pu, "sdi4"), /* DAT3 */
	DB8500_PIN("GPIO198_AG25", in_pu, "sdi4"), /* DAT2 */
	DB8500_PIN("GPIO199_AH23", in_pu, "sdi4"), /* DAT1 */
	DB8500_PIN("GPIO200_AH26", in_pu, "sdi4"), /* DAT0 */
	DB8500_PIN("GPIO201_AF24", in_pu, "sdi4"), /* CMD */
	DB8500_PIN("GPIO202_AF25", in_nopull, "sdi4"), /* FBCLK */
	DB8500_PIN("GPIO203_AE23", out_lo, "sdi4"), /* CLK */
	DB8500_PIN("GPIO204_AF23", in_pu, "sdi4"), /* DAT7 */
	DB8500_PIN("GPIO205_AG23", in_pu, "sdi4"), /* DAT6 */
	DB8500_PIN("GPIO206_AG24", in_pu, "sdi4"), /* DAT5 */
	DB8500_PIN("GPIO207_AJ23", in_pu, "sdi4"), /* DAT4 */
	/* Mux in USB pins, drive STP high */
	DB8500_MUX("usb_a_1", "usb", "musb-ux500.0"),
	DB8500_PIN("GPIO257_AE29", out_hi, "musb-ux500.0"), /* STP */
	/* Mux in SPI2 pins on the "other C1" altfunction */
	DB8500_MUX("spi2_oc1_1", "spi2", "spi2"),
	DB8500_PIN("GPIO216_AG12", gpio_out_hi, "spi2"), /* FRM */
	DB8500_PIN("GPIO218_AH11", in_pd, "spi2"), /* RXD */
	DB8500_PIN("GPIO215_AH13", out_lo, "spi2"), /* TXD */
	DB8500_PIN("GPIO217_AH12", out_lo, "spi2"), /* CLK */
};

/*
 * These are specifically for the MOP500 and HREFP (pre-v60) version of the
 * board, which utilized a TC35892 GPIO expander instead of using a lot of
 * on-chip pins as the HREFv60 and later does.
 */
static struct pinctrl_map __initdata mop500_pinmap[] = {
	/* Mux in SSP0, pull down RXD pin */
	DB8500_MUX_HOG("ssp0_a_1", "ssp0"),
	DB8500_PIN_HOG("GPIO145_C13", pd),
	/*
	 * XENON Flashgun on image processor GPIO (controlled from image
	 * processor firmware), mux in these image processor GPIO lines 0
	 * (XENON_FLASH_ID) and 1 (XENON_READY) on altfunction C and pull up
	 * the pins.
	 */
	DB8500_MUX_HOG("ipgpio0_c_1", "ipgpio"),
	DB8500_MUX_HOG("ipgpio1_c_1", "ipgpio"),
	DB8500_PIN_HOG("GPIO6_AF6", in_pu),
	DB8500_PIN_HOG("GPIO7_AG5", in_pu),
	/* TC35892 IRQ, pull up the line, let the driver mux in the pin */
	DB8500_PIN_HOG("GPIO217_AH12", gpio_in_pu),
	/* Mux in UART1 and set the pull-ups */
	DB8500_MUX_HOG("u1rxtx_a_1", "u1"),
	DB8500_MUX_HOG("u1ctsrts_a_1", "u1"),
	DB8500_PIN_HOG("GPIO4_AH6", in_pu), /* RXD */
	DB8500_PIN_HOG("GPIO5_AG6", out_hi), /* TXD */
	DB8500_PIN_HOG("GPIO6_AF6", in_pu), /* CTS */
	DB8500_PIN_HOG("GPIO7_AG5", out_hi), /* RTS */
	/*
	 * Runtime stuff: make it possible to mux in the SKE keypad
	 * and bias the pins
	 */
	DB8500_MUX("kp_a_2", "kp", "ske"),
	DB8500_PIN("GPIO153_B17", in_pd_slpm_in_pu, "ske"), /* I7 */
	DB8500_PIN("GPIO154_C16", in_pd_slpm_in_pu, "ske"), /* I6 */
	DB8500_PIN("GPIO155_C19", in_pd_slpm_in_pu, "ske"), /* I5 */
	DB8500_PIN("GPIO156_C17", in_pd_slpm_in_pu, "ske"), /* I4 */
	DB8500_PIN("GPIO161_D21", in_pd_slpm_in_pu, "ske"), /* I3 */
	DB8500_PIN("GPIO162_D20", in_pd_slpm_in_pu, "ske"), /* I2 */
	DB8500_PIN("GPIO163_C20", in_pd_slpm_in_pu, "ske"), /* I1 */
	DB8500_PIN("GPIO164_B21", in_pd_slpm_in_pu, "ske"), /* I0 */
	DB8500_PIN("GPIO157_A18", in_pu_slpm_out_lo, "ske"), /* O7 */
	DB8500_PIN("GPIO158_C18", in_pu_slpm_out_lo, "ske"), /* O6 */
	DB8500_PIN("GPIO159_B19", in_pu_slpm_out_lo, "ske"), /* O5 */
	DB8500_PIN("GPIO160_B20", in_pu_slpm_out_lo, "ske"), /* O4 */
	DB8500_PIN("GPIO165_C21", in_pu_slpm_out_lo, "ske"), /* O3 */
	DB8500_PIN("GPIO166_A22", in_pu_slpm_out_lo, "ske"), /* O2 */
	DB8500_PIN("GPIO167_B24", in_pu_slpm_out_lo, "ske"), /* O1 */
	DB8500_PIN("GPIO168_C22", in_pu_slpm_out_lo, "ske"), /* O0 */
	/* Mux in and drive the SDI0 DAT31DIR line high at runtime */
	DB8500_MUX("mc0dat31dir_a_1", "mc0", "sdi0"),
	DB8500_PIN("GPIO21_AB3", out_hi, "sdi0"),
};

/*
 * The HREFv60 series of platforms is using available pins on the DB8500
 * insteaf of the Toshiba I2C GPIO expander, reusing some pins like the SSP0
 * and SSP1 ports (previously connected to the AB8500) as generic GPIO lines.
 */
static struct pinctrl_map __initdata hrefv60_pinmap[] = {
	/* Drive WLAN_ENA low */
	DB8500_PIN_HOG("GPIO85_D5", gpio_out_lo), /* WLAN_ENA */
	/*
	 * XENON Flashgun on image processor GPIO (controlled from image
	 * processor firmware), mux in these image processor GPIO lines 0
	 * (XENON_FLASH_ID), 1 (XENON_READY) and there is an assistant
	 * LED on IP GPIO 4 (XENON_EN2) on altfunction C, that need bias
	 * from GPIO21 so pull up 0, 1 and drive 4 and GPIO21 low as output.
	 */
	DB8500_MUX_HOG("ipgpio0_c_1", "ipgpio"),
	DB8500_MUX_HOG("ipgpio1_c_1", "ipgpio"),
	DB8500_MUX_HOG("ipgpio4_c_1", "ipgpio"),
	DB8500_PIN_HOG("GPIO6_AF6", in_pu), /* XENON_FLASH_ID */
	DB8500_PIN_HOG("GPIO7_AG5", in_pu), /* XENON_READY */
	DB8500_PIN_HOG("GPIO21_AB3", gpio_out_lo), /* XENON_EN1 */
	DB8500_PIN_HOG("GPIO64_F3", out_lo), /* XENON_EN2 */
	/* Magnetometer uses GPIO 31 and 32, pull these up/down respectively */
	DB8500_PIN_HOG("GPIO31_V3", gpio_in_pu), /* EN1 */
	DB8500_PIN_HOG("GPIO32_V2", gpio_in_pd), /* DRDY */
	/*
	 * Display Interface 1 uses GPIO 65 for RST (reset).
	 * Display Interface 2 uses GPIO 66 for RST (reset).
	 * Drive DISP1 reset high (not reset), driver DISP2 reset low (reset)
	 */
	DB8500_PIN_HOG("GPIO65_F1", gpio_out_hi), /* DISP1 NO RST */
	DB8500_PIN_HOG("GPIO66_G3", gpio_out_lo), /* DISP2 RST */
	/*
	 * Touch screen uses GPIO 143 for RST1, GPIO 146 for RST2 and
	 * GPIO 67 for interrupts. Pull-up the IRQ line and drive both
	 * reset signals low.
	 */
	DB8500_PIN_HOG("GPIO143_D12", gpio_out_lo), /* TOUCH_RST1 */
	DB8500_PIN_HOG("GPIO67_G2", gpio_in_pu), /* TOUCH_INT2 */
	DB8500_PIN_HOG("GPIO146_D13", gpio_out_lo), /* TOUCH_RST2 */
	/*
	 * Drive D19-D23 for the ETM PTM trace interface low,
	 * (presumably pins are unconnected therefore grounded here,
	 * the "other alt C1" setting enables these pins)
	 */
	DB8500_PIN_HOG("GPIO70_G5", gpio_out_lo),
	DB8500_PIN_HOG("GPIO71_G4", gpio_out_lo),
	DB8500_PIN_HOG("GPIO72_H4", gpio_out_lo),
	DB8500_PIN_HOG("GPIO73_H3", gpio_out_lo),
	DB8500_PIN_HOG("GPIO74_J3", gpio_out_lo),
	/* NAHJ CTRL on GPIO 76 to low, CTRL_INV on GPIO216 to high */
	DB8500_PIN_HOG("GPIO76_J2", gpio_out_lo), /* CTRL */
	DB8500_PIN_HOG("GPIO216_AG12", gpio_out_hi), /* CTRL_INV */
	/* NFC ENA and RESET to low, pulldown IRQ line */
	DB8500_PIN_HOG("GPIO77_H1", gpio_out_lo), /* NFC_ENA */
	DB8500_PIN_HOG("GPIO144_B13", gpio_in_pd), /* NFC_IRQ */
	DB8500_PIN_HOG("GPIO142_C11", gpio_out_lo), /* NFC_RESET */
	/*
	 * SKE keyboard partly on alt A and partly on "Other alt C1"
	 * Driver KP_O1,2,3,6,7 low and pull up KP_I 0,2,3 for three
	 * rows of 6 keys, then pull up force sensing interrup and
	 * drive reset and force sensing WU low.
	 */
	DB8500_MUX_HOG("kp_a_1", "kp"),
	DB8500_MUX_HOG("kp_oc1_1", "kp"),
	DB8500_PIN_HOG("GPIO90_A3", out_lo), /* KP_O1 */
	DB8500_PIN_HOG("GPIO87_B3", out_lo), /* KP_O2 */
	DB8500_PIN_HOG("GPIO86_C6", out_lo), /* KP_O3 */
	DB8500_PIN_HOG("GPIO96_D8", out_lo), /* KP_O6 */
	DB8500_PIN_HOG("GPIO94_D7", out_lo), /* KP_O7 */
	DB8500_PIN_HOG("GPIO93_B7", in_pu), /* KP_I0 */
	DB8500_PIN_HOG("GPIO89_E6", in_pu), /* KP_I2 */
	DB8500_PIN_HOG("GPIO88_C4", in_pu), /* KP_I3 */
	DB8500_PIN_HOG("GPIO91_B6", gpio_in_pu), /* FORCE_SENSING_INT */
	DB8500_PIN_HOG("GPIO92_D6", gpio_out_lo), /* FORCE_SENSING_RST */
	DB8500_PIN_HOG("GPIO97_D9", gpio_out_lo), /* FORCE_SENSING_WU */
	/* DiPro Sensor interrupt */
	DB8500_PIN_HOG("GPIO139_C9", gpio_in_pu), /* DIPRO_INT */
	/* Audio Amplifier HF enable */
	DB8500_PIN_HOG("GPIO149_B14", gpio_out_hi), /* VAUDIO_HF_EN, enable MAX8968 */
	/* GBF interface, pull low to reset state */
	DB8500_PIN_HOG("GPIO171_D23", gpio_out_lo), /* GBF_ENA_RESET */
	/* MSP : HDTV INTERFACE GPIO line */
	DB8500_PIN_HOG("GPIO192_AJ27", gpio_in_pd),
	/* Accelerometer interrupt lines */
	DB8500_PIN_HOG("GPIO82_C1", gpio_in_pu), /* ACC_INT1 */
	DB8500_PIN_HOG("GPIO83_D3", gpio_in_pu), /* ACC_INT2 */
	/* SD card detect GPIO pin */
	DB8500_PIN_HOG("GPIO95_E8", gpio_in_pu),
	/*
	 * Runtime stuff
	 * Pull up/down of some sensor GPIO pins, for proximity, HAL sensor
	 * etc.
	 */
	DB8500_PIN("GPIO217_AH12", gpio_in_pu_slpm_gpio_nopull, "gpio-keys.0"),
	DB8500_PIN("GPIO145_C13", gpio_in_pd_slpm_gpio_nopull, "gpio-keys.0"),
	DB8500_PIN("GPIO139_C9", gpio_in_pu_slpm_gpio_nopull, "gpio-keys.0"),
	/*
	 * Make it possible to mux in the SKE keypad and bias the pins
	 * FIXME: what's the point with this on HREFv60? KP/SKE is already
	 * muxed in at another place! Enabling this will bork.
	 */
	DB8500_MUX("kp_a_2", "kp", "ske"),
	DB8500_PIN("GPIO153_B17", in_pd_slpm_in_pu, "ske"), /* I7 */
	DB8500_PIN("GPIO154_C16", in_pd_slpm_in_pu, "ske"), /* I6 */
	DB8500_PIN("GPIO155_C19", in_pd_slpm_in_pu, "ske"), /* I5 */
	DB8500_PIN("GPIO156_C17", in_pd_slpm_in_pu, "ske"), /* I4 */
	DB8500_PIN("GPIO161_D21", in_pd_slpm_in_pu, "ske"), /* I3 */
	DB8500_PIN("GPIO162_D20", in_pd_slpm_in_pu, "ske"), /* I2 */
	DB8500_PIN("GPIO163_C20", in_pd_slpm_in_pu, "ske"), /* I1 */
	DB8500_PIN("GPIO164_B21", in_pd_slpm_in_pu, "ske"), /* I0 */
	DB8500_PIN("GPIO157_A18", in_pu_slpm_out_lo, "ske"), /* O7 */
	DB8500_PIN("GPIO158_C18", in_pu_slpm_out_lo, "ske"), /* O6 */
	DB8500_PIN("GPIO159_B19", in_pu_slpm_out_lo, "ske"), /* O5 */
	DB8500_PIN("GPIO160_B20", in_pu_slpm_out_lo, "ske"), /* O4 */
	DB8500_PIN("GPIO165_C21", in_pu_slpm_out_lo, "ske"), /* O3 */
	DB8500_PIN("GPIO166_A22", in_pu_slpm_out_lo, "ske"), /* O2 */
	DB8500_PIN("GPIO167_B24", in_pu_slpm_out_lo, "ske"), /* O1 */
	DB8500_PIN("GPIO168_C22", in_pu_slpm_out_lo, "ske"), /* O0 */
};

static struct pinctrl_map __initdata u9500_pinmap[] = {
	/* Mux in UART1 (just RX/TX) and set the pull-ups */
	DB8500_MUX_HOG("u1rxtx_a_1", "u1"),
	DB8500_PIN_HOG("GPIO4_AH6", in_pu),
	DB8500_PIN_HOG("GPIO5_AG6", out_hi),
	/* WLAN_IRQ line */
	DB8500_PIN_HOG("GPIO144_B13", gpio_in_pu),
	/* HSI */
	DB8500_MUX_HOG("hsir_a_1", "hsi"),
	DB8500_MUX_HOG("hsit_a_1", "hsi"),
	DB8500_PIN_HOG("GPIO219_AG10", in_pd), /* RX FLA0 */
	DB8500_PIN_HOG("GPIO220_AH10", in_pd), /* RX DAT0 */
	DB8500_PIN_HOG("GPIO221_AJ11", out_lo), /* RX RDY0 */
	DB8500_PIN_HOG("GPIO222_AJ9", out_lo), /* TX FLA0 */
	DB8500_PIN_HOG("GPIO223_AH9", out_lo), /* TX DAT0 */
	DB8500_PIN_HOG("GPIO224_AG9", in_pd), /* TX RDY0 */
	DB8500_PIN_HOG("GPIO225_AG8", in_pd), /* CAWAKE0 */
	DB8500_PIN_HOG("GPIO226_AF8", out_hi), /* ACWAKE0 */
};

static struct pinctrl_map __initdata u8500_pinmap[] = {
	DB8500_PIN_HOG("GPIO226_AF8", gpio_out_lo), /* WLAN_PMU_EN */
	DB8500_PIN_HOG("GPIO4_AH6", gpio_in_pu), /* WLAN_IRQ */
};

static struct pinctrl_map __initdata snowball_pinmap[] = {
	/* Mux in SSP0 connected to AB8500, pull down RXD pin */
	DB8500_MUX_HOG("ssp0_a_1", "ssp0"),
	DB8500_PIN_HOG("GPIO145_C13", pd),
	/* Always drive the MC0 DAT31DIR line high on these boards */
	DB8500_PIN_HOG("GPIO21_AB3", out_hi),
	/* Mux in "SM" which is used for the SMSC911x Ethernet adapter */
	DB8500_MUX_HOG("sm_b_1", "sm"),
	/* Drive RSTn_LAN high */
	DB8500_PIN_HOG("GPIO141_C12", gpio_out_hi),
	/*  Accelerometer/Magnetometer */
	DB8500_PIN_HOG("GPIO163_C20", gpio_in_pu), /* ACCEL_IRQ1 */
	DB8500_PIN_HOG("GPIO164_B21", gpio_in_pu), /* ACCEL_IRQ2 */
	DB8500_PIN_HOG("GPIO165_C21", gpio_in_pu), /* MAG_DRDY */
	/* WLAN/GBF */
	DB8500_PIN_HOG("GPIO161_D21", gpio_out_lo), /* WLAN_PMU_EN */
	DB8500_PIN_HOG("GPIO171_D23", gpio_out_hi), /* GBF_ENA */
	DB8500_PIN_HOG("GPIO215_AH13", gpio_out_lo), /* WLAN_ENA */
	DB8500_PIN_HOG("GPIO216_AG12", gpio_in_pu), /* WLAN_IRQ */
};

/*
 * passing "pinsfor=" in kernel cmdline allows for custom
 * configuration of GPIOs on u8500 derived boards.
 */
static int __init early_pinsfor(char *p)
{
	pinsfor = PINS_FOR_DEFAULT;

	if (strcmp(p, "u9500-21") == 0)
		pinsfor = PINS_FOR_U9500;

	return 0;
}
early_param("pinsfor", early_pinsfor);

int pins_for_u9500(void)
{
	if (pinsfor == PINS_FOR_U9500)
		return 1;

	return 0;
}

static void __init mop500_href_family_pinmaps_init(void)
{
	switch (pinsfor) {
	case PINS_FOR_U9500:
		pinctrl_register_mappings(u9500_pinmap,
					  ARRAY_SIZE(u9500_pinmap));
		break;
	case PINS_FOR_DEFAULT:
		pinctrl_register_mappings(u8500_pinmap,
					  ARRAY_SIZE(u8500_pinmap));
	default:
		break;
	}
}

void __init mop500_pinmaps_init(void)
{
	pinctrl_register_mappings(mop500_family_pinmap,
				  ARRAY_SIZE(mop500_family_pinmap));
	pinctrl_register_mappings(mop500_pinmap,
				  ARRAY_SIZE(mop500_pinmap));
	mop500_href_family_pinmaps_init();
}

void __init snowball_pinmaps_init(void)
{
	pinctrl_register_mappings(mop500_family_pinmap,
				  ARRAY_SIZE(mop500_family_pinmap));
	pinctrl_register_mappings(snowball_pinmap,
				  ARRAY_SIZE(snowball_pinmap));
	pinctrl_register_mappings(u8500_pinmap,
				  ARRAY_SIZE(u8500_pinmap));
}

void __init hrefv60_pinmaps_init(void)
{
	pinctrl_register_mappings(mop500_family_pinmap,
				  ARRAY_SIZE(mop500_family_pinmap));
	pinctrl_register_mappings(hrefv60_pinmap,
				  ARRAY_SIZE(hrefv60_pinmap));
	mop500_href_family_pinmaps_init();
}
