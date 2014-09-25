#include "bcm47xx_private.h"

#include <linux/leds.h>
#include <bcm47xx_board.h>

/**************************************************
 * Database
 **************************************************/

#define BCM47XX_GPIO_LED(_gpio, _color, _function, _active_low,		\
			 _default_state)				\
	{								\
		.name		= "bcm47xx:" _color ":" _function,	\
		.gpio		= _gpio,				\
		.active_low	= _active_low,				\
		.default_state	= _default_state,			\
	}

#define BCM47XX_GPIO_LED_TRIGGER(_gpio, _color, _function, _active_low,	\
				 _default_trigger)			\
	{								\
		.name		= "bcm47xx:" _color ":" _function,	\
		.gpio		= _gpio,				\
		.active_low	= _active_low,				\
		.default_state	= LEDS_GPIO_DEFSTATE_OFF,		\
		.default_trigger	= _default_trigger,		\
	}

/* Asus */

static const struct gpio_led
bcm47xx_leds_asus_rtn12[] __initconst = {
	BCM47XX_GPIO_LED(2, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(7, "unk", "wlan", 0, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_asus_rtn15u[] __initconst = {
	/* TODO: Add "wlan" LED */
	BCM47XX_GPIO_LED(3, "blue", "wan", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(4, "blue", "lan", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(6, "blue", "power", 1, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(9, "blue", "usb", 0, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_asus_rtn16[] __initconst = {
	BCM47XX_GPIO_LED(1, "blue", "power", 1, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(7, "blue", "wlan", 0, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_asus_rtn66u[] __initconst = {
	BCM47XX_GPIO_LED(12, "blue", "power", 1, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(15, "blue", "usb", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_asus_wl300g[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON),
};

static const struct gpio_led
bcm47xx_leds_asus_wl320ge[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "wlan", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(2, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(11, "unk", "link", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_asus_wl330ge[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON),
};

static const struct gpio_led
bcm47xx_leds_asus_wl500g[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON),
};

static const struct gpio_led
bcm47xx_leds_asus_wl500gd[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON),
};

static const struct gpio_led
bcm47xx_leds_asus_wl500gpv1[] __initconst = {
	BCM47XX_GPIO_LED(1, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON),
};

static const struct gpio_led
bcm47xx_leds_asus_wl500gpv2[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(1, "unk", "wlan", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_asus_wl500w[] __initconst = {
	BCM47XX_GPIO_LED(5, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON),
};

static const struct gpio_led
bcm47xx_leds_asus_wl520gc[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(1, "unk", "wlan", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_asus_wl520gu[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(1, "unk", "wlan", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_asus_wl700ge[] __initconst = {
	BCM47XX_GPIO_LED(1, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON), /* Labeled "READY" (there is no "power" LED). Originally ON, flashing on USB activity. */
};

static const struct gpio_led
bcm47xx_leds_asus_wlhdd[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(2, "unk", "usb", 1, LEDS_GPIO_DEFSTATE_OFF),
};

/* Belkin */

static const struct gpio_led
bcm47xx_leds_belkin_f7d4301[] __initconst = {
	BCM47XX_GPIO_LED(10, "green", "power", 1, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(11, "amber", "power", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(12, "unk", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(13, "unk", "wlan", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(14, "unk", "usb0", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(15, "unk", "usb1", 1, LEDS_GPIO_DEFSTATE_OFF),
};

/* Buffalo */

static const struct gpio_led
bcm47xx_leds_buffalo_whr2_a54g54[] __initconst = {
	BCM47XX_GPIO_LED(7, "unk", "diag", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_buffalo_whr_g125[] __initconst = {
	BCM47XX_GPIO_LED(1, "unk", "bridge", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(2, "unk", "wlan", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(3, "unk", "internal", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(6, "unk", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(7, "unk", "diag", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_buffalo_whr_g54s[] __initconst = {
	BCM47XX_GPIO_LED(1, "unk", "bridge", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(2, "unk", "wlan", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(3, "unk", "internal", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(6, "unk", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(7, "unk", "diag", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_buffalo_whr_hp_g54[] __initconst = {
	BCM47XX_GPIO_LED(1, "unk", "bridge", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(2, "unk", "wlan", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(3, "unk", "internal", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(6, "unk", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(7, "unk", "diag", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_buffalo_wzr_g300n[] __initconst = {
	BCM47XX_GPIO_LED(1, "unk", "bridge", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(6, "unk", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(7, "unk", "diag", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_buffalo_wzr_rs_g54[] __initconst = {
	BCM47XX_GPIO_LED(6, "unk", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(1, "unk", "vpn", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(7, "unk", "diag", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_buffalo_wzr_rs_g54hp[] __initconst = {
	BCM47XX_GPIO_LED(6, "unk", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(1, "unk", "vpn", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(7, "unk", "diag", 1, LEDS_GPIO_DEFSTATE_OFF),
};

/* Dell */

static const struct gpio_led
bcm47xx_leds_dell_tm2300[] __initconst = {
	BCM47XX_GPIO_LED(6, "unk", "wlan", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(7, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON),
};

/* D-Link */

static const struct gpio_led
bcm47xx_leds_dlink_dir130[] __initconst = {
	BCM47XX_GPIO_LED_TRIGGER(0, "green", "status", 1, "timer"), /* Originally blinking when device is ready, separated from "power" LED */
	BCM47XX_GPIO_LED(6, "blue", "unk", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_dlink_dir330[] __initconst = {
	BCM47XX_GPIO_LED_TRIGGER(0, "green", "status", 1, "timer"), /* Originally blinking when device is ready, separated from "power" LED */
	BCM47XX_GPIO_LED(4, "unk", "usb", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(6, "blue", "unk", 1, LEDS_GPIO_DEFSTATE_OFF),
};

/* Huawei */

static const struct gpio_led
bcm47xx_leds_huawei_e970[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "wlan", 0, LEDS_GPIO_DEFSTATE_OFF),
};

/* Linksys */

static const struct gpio_led
bcm47xx_leds_linksys_e1000v1[] __initconst = {
	BCM47XX_GPIO_LED(0, "blue", "wlan", 0, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(1, "blue", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(2, "amber", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(4, "blue", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_linksys_e1000v21[] __initconst = {
	BCM47XX_GPIO_LED(5, "blue", "wlan", 0, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(6, "blue", "power", 1, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(7, "amber", "wps", 0, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(8, "blue", "wps", 0, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_linksys_e2000v1[] __initconst = {
	BCM47XX_GPIO_LED(1, "blue", "wlan", 0, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(2, "blue", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(3, "blue", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(4, "amber", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_linksys_e3000v1[] __initconst = {
	BCM47XX_GPIO_LED(0, "amber", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(1, "unk", "wlan", 0, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(3, "blue", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(5, "unk", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(7, "unk", "usb", 0, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_linksys_e3200v1[] __initconst = {
	BCM47XX_GPIO_LED(3, "green", "power", 1, LEDS_GPIO_DEFSTATE_ON),
};

static const struct gpio_led
bcm47xx_leds_linksys_e4200v1[] __initconst = {
	BCM47XX_GPIO_LED(5, "white", "power", 1, LEDS_GPIO_DEFSTATE_ON),
};

static const struct gpio_led
bcm47xx_leds_linksys_wrt150nv1[] __initconst = {
	BCM47XX_GPIO_LED(1, "unk", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(3, "amber", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(5, "green", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_linksys_wrt150nv11[] __initconst = {
	BCM47XX_GPIO_LED(1, "unk", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(3, "amber", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(5, "green", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_linksys_wrt160nv1[] __initconst = {
	BCM47XX_GPIO_LED(1, "unk", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(3, "amber", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(5, "blue", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_linksys_wrt160nv3[] __initconst = {
	BCM47XX_GPIO_LED(1, "unk", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(2, "amber", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(4, "blue", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_linksys_wrt300nv11[] __initconst = {
	BCM47XX_GPIO_LED(1, "unk", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(3, "amber", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(5, "green", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_linksys_wrt310nv1[] __initconst = {
	BCM47XX_GPIO_LED(1, "blue", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(3, "amber", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(9, "blue", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_linksys_wrt54g_generic[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "dmz", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(1, "unk", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(5, "white", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(7, "orange", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_linksys_wrt54g3gv2[] __initconst = {
	BCM47XX_GPIO_LED(1, "unk", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(2, "green", "3g", 0, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(3, "blue", "3g", 0, LEDS_GPIO_DEFSTATE_OFF),
};

/* Verified on: WRT54GS V1.0 */
static const struct gpio_led
bcm47xx_leds_linksys_wrt54g_type_0101[] __initconst = {
	BCM47XX_GPIO_LED(0, "green", "wlan", 0, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(1, "green", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(7, "green", "dmz", 1, LEDS_GPIO_DEFSTATE_OFF),
};

/* Verified on: WRT54GL V1.1 */
static const struct gpio_led
bcm47xx_leds_linksys_wrt54g_type_0467[] __initconst = {
	BCM47XX_GPIO_LED(0, "green", "wlan", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(1, "green", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(2, "white", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(3, "orange", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(7, "green", "dmz", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_linksys_wrt610nv1[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "usb",  1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(1, "unk", "power",  0, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(3, "amber", "wps",  1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(9, "blue", "wps",  1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_linksys_wrt610nv2[] __initconst = {
	BCM47XX_GPIO_LED(0, "amber", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(1, "unk", "wlan", 0, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(3, "blue", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(5, "unk", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(7, "unk", "usb", 0, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_linksys_wrtsl54gs[] __initconst = {
	BCM47XX_GPIO_LED(0, "green", "dmz", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(1, "green", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(5, "white", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(7, "orange", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
};

/* Microsoft */

static const struct gpio_led
bcm47xx_leds_microsoft_nm700[] __initconst = {
	BCM47XX_GPIO_LED(6, "unk", "power", 0, LEDS_GPIO_DEFSTATE_ON),
};

/* Motorola */

static const struct gpio_led
bcm47xx_leds_motorola_we800g[] __initconst = {
	BCM47XX_GPIO_LED(1, "amber", "wlan", 0, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(2, "unk", "unk", 1, LEDS_GPIO_DEFSTATE_OFF), /* There are only 3 LEDs: Power, Wireless and Device (ethernet) */
	BCM47XX_GPIO_LED(4, "green", "power", 0, LEDS_GPIO_DEFSTATE_ON),
};

static const struct gpio_led
bcm47xx_leds_motorola_wr850gp[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "wlan", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(1, "unk", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(6, "unk", "dmz", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(7, "unk", "diag", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_motorola_wr850gv2v3[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "wlan", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(1, "unk", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(7, "unk", "diag", 1, LEDS_GPIO_DEFSTATE_OFF),
};

/* Netgear */

static const struct gpio_led
bcm47xx_leds_netgear_wndr3400v1[] __initconst = {
	BCM47XX_GPIO_LED(2, "green", "usb", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(3, "green", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(7, "amber", "power", 0, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_netgear_wndr4500v1[] __initconst = {
	BCM47XX_GPIO_LED(1, "green", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(2, "green", "power", 1, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(3, "amber", "power", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(8, "green", "usb1", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(9, "green", "2ghz", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(11, "blue", "5ghz", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(14, "green", "usb2", 1, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_netgear_wnr3500lv1[] __initconst = {
	BCM47XX_GPIO_LED(0, "blue", "wlan", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(1, "green", "wps", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(2, "green", "wan", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(3, "green", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(7, "amber", "power", 0, LEDS_GPIO_DEFSTATE_OFF),
};

static const struct gpio_led
bcm47xx_leds_netgear_wnr834bv2[] __initconst = {
	BCM47XX_GPIO_LED(2, "green", "power", 0, LEDS_GPIO_DEFSTATE_ON),
	BCM47XX_GPIO_LED(3, "amber", "power", 0, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(7, "unk", "connected", 0, LEDS_GPIO_DEFSTATE_OFF),
};

/* Siemens */
static const struct gpio_led
bcm47xx_leds_siemens_se505v2[] __initconst = {
	BCM47XX_GPIO_LED(0, "unk", "dmz", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(3, "unk", "wlan", 1, LEDS_GPIO_DEFSTATE_OFF),
	BCM47XX_GPIO_LED(5, "unk", "power", 1, LEDS_GPIO_DEFSTATE_ON),
};

/* SimpleTech */

static const struct gpio_led
bcm47xx_leds_simpletech_simpleshare[] __initconst = {
	BCM47XX_GPIO_LED(1, "unk", "status", 1, LEDS_GPIO_DEFSTATE_OFF), /* "Ready" LED */
};

/**************************************************
 * Init
 **************************************************/

static struct gpio_led_platform_data bcm47xx_leds_pdata;

#define bcm47xx_set_pdata(dev_leds) do {				\
	bcm47xx_leds_pdata.leds = dev_leds;				\
	bcm47xx_leds_pdata.num_leds = ARRAY_SIZE(dev_leds);		\
} while (0)

void __init bcm47xx_leds_register(void)
{
	enum bcm47xx_board board = bcm47xx_board_get();

	switch (board) {
	case BCM47XX_BOARD_ASUS_RTN12:
		bcm47xx_set_pdata(bcm47xx_leds_asus_rtn12);
		break;
	case BCM47XX_BOARD_ASUS_RTN15U:
		bcm47xx_set_pdata(bcm47xx_leds_asus_rtn15u);
		break;
	case BCM47XX_BOARD_ASUS_RTN16:
		bcm47xx_set_pdata(bcm47xx_leds_asus_rtn16);
		break;
	case BCM47XX_BOARD_ASUS_RTN66U:
		bcm47xx_set_pdata(bcm47xx_leds_asus_rtn66u);
		break;
	case BCM47XX_BOARD_ASUS_WL300G:
		bcm47xx_set_pdata(bcm47xx_leds_asus_wl300g);
		break;
	case BCM47XX_BOARD_ASUS_WL320GE:
		bcm47xx_set_pdata(bcm47xx_leds_asus_wl320ge);
		break;
	case BCM47XX_BOARD_ASUS_WL330GE:
		bcm47xx_set_pdata(bcm47xx_leds_asus_wl330ge);
		break;
	case BCM47XX_BOARD_ASUS_WL500G:
		bcm47xx_set_pdata(bcm47xx_leds_asus_wl500g);
		break;
	case BCM47XX_BOARD_ASUS_WL500GD:
		bcm47xx_set_pdata(bcm47xx_leds_asus_wl500gd);
		break;
	case BCM47XX_BOARD_ASUS_WL500GPV1:
		bcm47xx_set_pdata(bcm47xx_leds_asus_wl500gpv1);
		break;
	case BCM47XX_BOARD_ASUS_WL500GPV2:
		bcm47xx_set_pdata(bcm47xx_leds_asus_wl500gpv2);
		break;
	case BCM47XX_BOARD_ASUS_WL500W:
		bcm47xx_set_pdata(bcm47xx_leds_asus_wl500w);
		break;
	case BCM47XX_BOARD_ASUS_WL520GC:
		bcm47xx_set_pdata(bcm47xx_leds_asus_wl520gc);
		break;
	case BCM47XX_BOARD_ASUS_WL520GU:
		bcm47xx_set_pdata(bcm47xx_leds_asus_wl520gu);
		break;
	case BCM47XX_BOARD_ASUS_WL700GE:
		bcm47xx_set_pdata(bcm47xx_leds_asus_wl700ge);
		break;
	case BCM47XX_BOARD_ASUS_WLHDD:
		bcm47xx_set_pdata(bcm47xx_leds_asus_wlhdd);
		break;

	case BCM47XX_BOARD_BELKIN_F7D3301:
	case BCM47XX_BOARD_BELKIN_F7D3302:
	case BCM47XX_BOARD_BELKIN_F7D4301:
	case BCM47XX_BOARD_BELKIN_F7D4302:
	case BCM47XX_BOARD_BELKIN_F7D4401:
		bcm47xx_set_pdata(bcm47xx_leds_belkin_f7d4301);
		break;

	case BCM47XX_BOARD_BUFFALO_WHR2_A54G54:
		bcm47xx_set_pdata(bcm47xx_leds_buffalo_whr2_a54g54);
		break;
	case BCM47XX_BOARD_BUFFALO_WHR_G125:
		bcm47xx_set_pdata(bcm47xx_leds_buffalo_whr_g125);
		break;
	case BCM47XX_BOARD_BUFFALO_WHR_G54S:
		bcm47xx_set_pdata(bcm47xx_leds_buffalo_whr_g54s);
		break;
	case BCM47XX_BOARD_BUFFALO_WHR_HP_G54:
		bcm47xx_set_pdata(bcm47xx_leds_buffalo_whr_hp_g54);
		break;
	case BCM47XX_BOARD_BUFFALO_WZR_G300N:
		bcm47xx_set_pdata(bcm47xx_leds_buffalo_wzr_g300n);
		break;
	case BCM47XX_BOARD_BUFFALO_WZR_RS_G54:
		bcm47xx_set_pdata(bcm47xx_leds_buffalo_wzr_rs_g54);
		break;
	case BCM47XX_BOARD_BUFFALO_WZR_RS_G54HP:
		bcm47xx_set_pdata(bcm47xx_leds_buffalo_wzr_rs_g54hp);
		break;

	case BCM47XX_BOARD_DELL_TM2300:
		bcm47xx_set_pdata(bcm47xx_leds_dell_tm2300);
		break;

	case BCM47XX_BOARD_DLINK_DIR130:
		bcm47xx_set_pdata(bcm47xx_leds_dlink_dir130);
		break;
	case BCM47XX_BOARD_DLINK_DIR330:
		bcm47xx_set_pdata(bcm47xx_leds_dlink_dir330);
		break;

	case BCM47XX_BOARD_HUAWEI_E970:
		bcm47xx_set_pdata(bcm47xx_leds_huawei_e970);
		break;

	case BCM47XX_BOARD_LINKSYS_E1000V1:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_e1000v1);
		break;
	case BCM47XX_BOARD_LINKSYS_E1000V21:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_e1000v21);
		break;
	case BCM47XX_BOARD_LINKSYS_E2000V1:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_e2000v1);
		break;
	case BCM47XX_BOARD_LINKSYS_E3000V1:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_e3000v1);
		break;
	case BCM47XX_BOARD_LINKSYS_E3200V1:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_e3200v1);
		break;
	case BCM47XX_BOARD_LINKSYS_E4200V1:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_e4200v1);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT150NV1:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_wrt150nv1);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT150NV11:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_wrt150nv11);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT160NV1:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_wrt160nv1);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT160NV3:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_wrt160nv3);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT300NV11:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_wrt300nv11);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT310NV1:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_wrt310nv1);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT54G3GV2:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_wrt54g3gv2);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT54G_TYPE_0101:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_wrt54g_type_0101);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT54G_TYPE_0467:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_wrt54g_type_0467);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT54G_TYPE_0708:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_wrt54g_generic);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT610NV1:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_wrt610nv1);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT610NV2:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_wrt610nv2);
		break;
	case BCM47XX_BOARD_LINKSYS_WRTSL54GS:
		bcm47xx_set_pdata(bcm47xx_leds_linksys_wrtsl54gs);
		break;

	case BCM47XX_BOARD_MICROSOFT_MN700:
		bcm47xx_set_pdata(bcm47xx_leds_microsoft_nm700);
		break;

	case BCM47XX_BOARD_MOTOROLA_WE800G:
		bcm47xx_set_pdata(bcm47xx_leds_motorola_we800g);
		break;
	case BCM47XX_BOARD_MOTOROLA_WR850GP:
		bcm47xx_set_pdata(bcm47xx_leds_motorola_wr850gp);
		break;
	case BCM47XX_BOARD_MOTOROLA_WR850GV2V3:
		bcm47xx_set_pdata(bcm47xx_leds_motorola_wr850gv2v3);
		break;

	case BCM47XX_BOARD_NETGEAR_WNDR3400V1:
		bcm47xx_set_pdata(bcm47xx_leds_netgear_wndr3400v1);
		break;
	case BCM47XX_BOARD_NETGEAR_WNDR4500V1:
		bcm47xx_set_pdata(bcm47xx_leds_netgear_wndr4500v1);
		break;
	case BCM47XX_BOARD_NETGEAR_WNR3500L:
		bcm47xx_set_pdata(bcm47xx_leds_netgear_wnr3500lv1);
		break;
	case BCM47XX_BOARD_NETGEAR_WNR834BV2:
		bcm47xx_set_pdata(bcm47xx_leds_netgear_wnr834bv2);
		break;

	case BCM47XX_BOARD_SIEMENS_SE505V2:
		bcm47xx_set_pdata(bcm47xx_leds_siemens_se505v2);
		break;

	case BCM47XX_BOARD_SIMPLETECH_SIMPLESHARE:
		bcm47xx_set_pdata(bcm47xx_leds_simpletech_simpleshare);
		break;

	default:
		pr_debug("No LEDs configuration found for this device\n");
		return;
	}

	gpio_led_register_device(-1, &bcm47xx_leds_pdata);
}
