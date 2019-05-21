// SPDX-License-Identifier: GPL-2.0
#include "bcm47xx_private.h"

#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/interrupt.h>
#include <bcm47xx_board.h>
#include <bcm47xx.h>

/**************************************************
 * Database
 **************************************************/

#define BCM47XX_GPIO_KEY(_gpio, _code)					\
	{								\
		.code		= _code,				\
		.gpio		= _gpio,				\
		.active_low	= 1,					\
	}

#define BCM47XX_GPIO_KEY_H(_gpio, _code)				\
	{								\
		.code		= _code,				\
		.gpio		= _gpio,				\
	}

/* Asus */

static const struct gpio_keys_button
bcm47xx_buttons_asus_rtn12[] __initconst = {
	BCM47XX_GPIO_KEY(0, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(1, KEY_RESTART),
	BCM47XX_GPIO_KEY(4, BTN_0), /* Router mode */
	BCM47XX_GPIO_KEY(5, BTN_1), /* Repeater mode */
	BCM47XX_GPIO_KEY(6, BTN_2), /* AP mode */
};

static const struct gpio_keys_button
bcm47xx_buttons_asus_rtn16[] __initconst = {
	BCM47XX_GPIO_KEY(6, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(8, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_asus_rtn66u[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(9, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_asus_wl300g[] __initconst = {
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_asus_wl320ge[] __initconst = {
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_asus_wl330ge[] __initconst = {
	BCM47XX_GPIO_KEY(2, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_asus_wl500g[] __initconst = {
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_asus_wl500gd[] __initconst = {
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_asus_wl500gpv1[] __initconst = {
	BCM47XX_GPIO_KEY(0, KEY_RESTART),
	BCM47XX_GPIO_KEY(4, KEY_WPS_BUTTON),
};

static const struct gpio_keys_button
bcm47xx_buttons_asus_wl500gpv2[] __initconst = {
	BCM47XX_GPIO_KEY(2, KEY_RESTART),
	BCM47XX_GPIO_KEY(3, KEY_WPS_BUTTON),
};

static const struct gpio_keys_button
bcm47xx_buttons_asus_wl500w[] __initconst = {
	BCM47XX_GPIO_KEY_H(6, KEY_RESTART),
	BCM47XX_GPIO_KEY_H(7, KEY_WPS_BUTTON),
};

static const struct gpio_keys_button
bcm47xx_buttons_asus_wl520gc[] __initconst = {
	BCM47XX_GPIO_KEY(2, KEY_RESTART),
	BCM47XX_GPIO_KEY(3, KEY_WPS_BUTTON),
};

static const struct gpio_keys_button
bcm47xx_buttons_asus_wl520gu[] __initconst = {
	BCM47XX_GPIO_KEY(2, KEY_RESTART),
	BCM47XX_GPIO_KEY(3, KEY_WPS_BUTTON),
};

static const struct gpio_keys_button
bcm47xx_buttons_asus_wl700ge[] __initconst = {
	BCM47XX_GPIO_KEY(0, KEY_POWER), /* Hard disk power switch */
	BCM47XX_GPIO_KEY(4, KEY_WPS_BUTTON), /* EZSetup */
	BCM47XX_GPIO_KEY(6, KEY_COPY), /* Copy data from USB to internal disk */
	BCM47XX_GPIO_KEY(7, KEY_RESTART), /* Hard reset */
};

static const struct gpio_keys_button
bcm47xx_buttons_asus_wlhdd[] __initconst = {
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

/* Huawei */

static const struct gpio_keys_button
bcm47xx_buttons_huawei_e970[] __initconst = {
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

/* Belkin */

static const struct gpio_keys_button
bcm47xx_buttons_belkin_f7d4301[] __initconst = {
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
	BCM47XX_GPIO_KEY(8, KEY_WPS_BUTTON),
};

/* Buffalo */

static const struct gpio_keys_button
bcm47xx_buttons_buffalo_whr2_a54g54[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_buffalo_whr_g125[] __initconst = {
	BCM47XX_GPIO_KEY(0, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(4, KEY_RESTART),
	BCM47XX_GPIO_KEY(5, BTN_0), /* Router / AP mode swtich */
};

static const struct gpio_keys_button
bcm47xx_buttons_buffalo_whr_g54s[] __initconst = {
	BCM47XX_GPIO_KEY(0, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY_H(4, KEY_RESTART),
	BCM47XX_GPIO_KEY(5, BTN_0), /* Router / AP mode swtich */
};

static const struct gpio_keys_button
bcm47xx_buttons_buffalo_whr_hp_g54[] __initconst = {
	BCM47XX_GPIO_KEY(0, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(4, KEY_RESTART),
	BCM47XX_GPIO_KEY(5, BTN_0), /* Router / AP mode swtich */
};

static const struct gpio_keys_button
bcm47xx_buttons_buffalo_wzr_g300n[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_buffalo_wzr_rs_g54[] __initconst = {
	BCM47XX_GPIO_KEY(0, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(4, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_buffalo_wzr_rs_g54hp[] __initconst = {
	BCM47XX_GPIO_KEY(0, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(4, KEY_RESTART),
};

/* Dell */

static const struct gpio_keys_button
bcm47xx_buttons_dell_tm2300[] __initconst = {
	BCM47XX_GPIO_KEY(0, KEY_RESTART),
};

/* D-Link */

static const struct gpio_keys_button
bcm47xx_buttons_dlink_dir130[] __initconst = {
	BCM47XX_GPIO_KEY(3, KEY_RESTART),
	BCM47XX_GPIO_KEY(7, KEY_UNKNOWN),
};

static const struct gpio_keys_button
bcm47xx_buttons_dlink_dir330[] __initconst = {
	BCM47XX_GPIO_KEY(3, KEY_RESTART),
	BCM47XX_GPIO_KEY(7, KEY_UNKNOWN),
};

/* Linksys */

static const struct gpio_keys_button
bcm47xx_buttons_linksys_e1000v1[] __initconst = {
	BCM47XX_GPIO_KEY(5, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_e1000v21[] __initconst = {
	BCM47XX_GPIO_KEY(9, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(10, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_e2000v1[] __initconst = {
	BCM47XX_GPIO_KEY(5, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(8, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_e3000v1[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_e3200v1[] __initconst = {
	BCM47XX_GPIO_KEY(5, KEY_RESTART),
	BCM47XX_GPIO_KEY(8, KEY_WPS_BUTTON),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_e4200v1[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_wrt150nv1[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_wrt150nv11[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_wrt160nv1[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_wrt160nv3[] __initconst = {
	BCM47XX_GPIO_KEY(5, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_wrt300n_v1[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_wrt300nv11[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_UNKNOWN),
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_wrt310nv1[] __initconst = {
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
	BCM47XX_GPIO_KEY(8, KEY_UNKNOWN),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_wrt54g3gv2[] __initconst = {
	BCM47XX_GPIO_KEY(5, KEY_WIMAX),
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_wrt54g_generic[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_wrt610nv1[] __initconst = {
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
	BCM47XX_GPIO_KEY(8, KEY_WPS_BUTTON),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_wrt610nv2[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_linksys_wrtsl54gs[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

/* Luxul */

static const struct gpio_keys_button
bcm47xx_buttons_luxul_abr_4400_v1[] = {
	BCM47XX_GPIO_KEY(14, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_luxul_xap_310_v1[] = {
	BCM47XX_GPIO_KEY(20, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_luxul_xap_1210_v1[] = {
	BCM47XX_GPIO_KEY(8, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_luxul_xap_1230_v1[] = {
	BCM47XX_GPIO_KEY(8, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_luxul_xap_1240_v1[] = {
	BCM47XX_GPIO_KEY(8, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_luxul_xap_1500_v1[] = {
	BCM47XX_GPIO_KEY(14, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_luxul_xbr_4400_v1[] = {
	BCM47XX_GPIO_KEY(14, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_luxul_xvw_p30_v1[] = {
	BCM47XX_GPIO_KEY(20, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_luxul_xwr_600_v1[] = {
	BCM47XX_GPIO_KEY(8, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_luxul_xwr_1750_v1[] = {
	BCM47XX_GPIO_KEY(14, KEY_RESTART),
};

/* Microsoft */

static const struct gpio_keys_button
bcm47xx_buttons_microsoft_nm700[] __initconst = {
	BCM47XX_GPIO_KEY(7, KEY_RESTART),
};

/* Motorola */

static const struct gpio_keys_button
bcm47xx_buttons_motorola_we800g[] __initconst = {
	BCM47XX_GPIO_KEY(0, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_motorola_wr850gp[] __initconst = {
	BCM47XX_GPIO_KEY(5, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_motorola_wr850gv2v3[] __initconst = {
	BCM47XX_GPIO_KEY(5, KEY_RESTART),
};

/* Netgear */

static const struct gpio_keys_button
bcm47xx_buttons_netgear_wndr3400v1[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_RESTART),
	BCM47XX_GPIO_KEY(6, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(8, KEY_RFKILL),
};

static const struct gpio_keys_button
bcm47xx_buttons_netgear_wndr3400_v3[] __initconst = {
	BCM47XX_GPIO_KEY(12, KEY_RESTART),
	BCM47XX_GPIO_KEY(23, KEY_WPS_BUTTON),
};

static const struct gpio_keys_button
bcm47xx_buttons_netgear_wndr3700v3[] __initconst = {
	BCM47XX_GPIO_KEY(2, KEY_RFKILL),
	BCM47XX_GPIO_KEY(3, KEY_RESTART),
	BCM47XX_GPIO_KEY(4, KEY_WPS_BUTTON),
};

static const struct gpio_keys_button
bcm47xx_buttons_netgear_wndr4500v1[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(5, KEY_RFKILL),
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_netgear_wnr1000_v3[] __initconst = {
	BCM47XX_GPIO_KEY(2, KEY_WPS_BUTTON),
	BCM47XX_GPIO_KEY(3, KEY_RESTART),
};

static const struct gpio_keys_button
bcm47xx_buttons_netgear_wnr3500lv1[] __initconst = {
	BCM47XX_GPIO_KEY(4, KEY_RESTART),
	BCM47XX_GPIO_KEY(6, KEY_WPS_BUTTON),
};

static const struct gpio_keys_button
bcm47xx_buttons_netgear_wnr834bv2[] __initconst = {
	BCM47XX_GPIO_KEY(6, KEY_RESTART),
};

/* SimpleTech */

static const struct gpio_keys_button
bcm47xx_buttons_simpletech_simpleshare[] __initconst = {
	BCM47XX_GPIO_KEY(0, KEY_RESTART),
};

/**************************************************
 * Init
 **************************************************/

static struct gpio_keys_platform_data bcm47xx_button_pdata;

static struct platform_device bcm47xx_buttons_gpio_keys = {
	.name = "gpio-keys",
	.dev = {
		.platform_data = &bcm47xx_button_pdata,
	}
};

/* Copy data from __initconst */
static int __init bcm47xx_buttons_copy(const struct gpio_keys_button *buttons,
				       size_t nbuttons)
{
	size_t size = nbuttons * sizeof(*buttons);

	bcm47xx_button_pdata.buttons = kmemdup(buttons, size, GFP_KERNEL);
	if (!bcm47xx_button_pdata.buttons)
		return -ENOMEM;
	bcm47xx_button_pdata.nbuttons = nbuttons;

	return 0;
}

#define bcm47xx_copy_bdata(dev_buttons)					\
	bcm47xx_buttons_copy(dev_buttons, ARRAY_SIZE(dev_buttons));

int __init bcm47xx_buttons_register(void)
{
	enum bcm47xx_board board = bcm47xx_board_get();
	int err;

	switch (board) {
	case BCM47XX_BOARD_ASUS_RTN12:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_rtn12);
		break;
	case BCM47XX_BOARD_ASUS_RTN16:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_rtn16);
		break;
	case BCM47XX_BOARD_ASUS_RTN66U:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_rtn66u);
		break;
	case BCM47XX_BOARD_ASUS_WL300G:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_wl300g);
		break;
	case BCM47XX_BOARD_ASUS_WL320GE:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_wl320ge);
		break;
	case BCM47XX_BOARD_ASUS_WL330GE:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_wl330ge);
		break;
	case BCM47XX_BOARD_ASUS_WL500G:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_wl500g);
		break;
	case BCM47XX_BOARD_ASUS_WL500GD:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_wl500gd);
		break;
	case BCM47XX_BOARD_ASUS_WL500GPV1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_wl500gpv1);
		break;
	case BCM47XX_BOARD_ASUS_WL500GPV2:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_wl500gpv2);
		break;
	case BCM47XX_BOARD_ASUS_WL500W:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_wl500w);
		break;
	case BCM47XX_BOARD_ASUS_WL520GC:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_wl520gc);
		break;
	case BCM47XX_BOARD_ASUS_WL520GU:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_wl520gu);
		break;
	case BCM47XX_BOARD_ASUS_WL700GE:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_wl700ge);
		break;
	case BCM47XX_BOARD_ASUS_WLHDD:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_asus_wlhdd);
		break;

	case BCM47XX_BOARD_BELKIN_F7D3301:
	case BCM47XX_BOARD_BELKIN_F7D3302:
	case BCM47XX_BOARD_BELKIN_F7D4301:
	case BCM47XX_BOARD_BELKIN_F7D4302:
	case BCM47XX_BOARD_BELKIN_F7D4401:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_belkin_f7d4301);
		break;

	case BCM47XX_BOARD_BUFFALO_WHR2_A54G54:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_buffalo_whr2_a54g54);
		break;
	case BCM47XX_BOARD_BUFFALO_WHR_G125:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_buffalo_whr_g125);
		break;
	case BCM47XX_BOARD_BUFFALO_WHR_G54S:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_buffalo_whr_g54s);
		break;
	case BCM47XX_BOARD_BUFFALO_WHR_HP_G54:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_buffalo_whr_hp_g54);
		break;
	case BCM47XX_BOARD_BUFFALO_WZR_G300N:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_buffalo_wzr_g300n);
		break;
	case BCM47XX_BOARD_BUFFALO_WZR_RS_G54:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_buffalo_wzr_rs_g54);
		break;
	case BCM47XX_BOARD_BUFFALO_WZR_RS_G54HP:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_buffalo_wzr_rs_g54hp);
		break;

	case BCM47XX_BOARD_DELL_TM2300:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_dell_tm2300);
		break;

	case BCM47XX_BOARD_DLINK_DIR130:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_dlink_dir130);
		break;
	case BCM47XX_BOARD_DLINK_DIR330:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_dlink_dir330);
		break;

	case BCM47XX_BOARD_HUAWEI_E970:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_huawei_e970);
		break;

	case BCM47XX_BOARD_LINKSYS_E1000V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_e1000v1);
		break;
	case BCM47XX_BOARD_LINKSYS_E1000V21:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_e1000v21);
		break;
	case BCM47XX_BOARD_LINKSYS_E2000V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_e2000v1);
		break;
	case BCM47XX_BOARD_LINKSYS_E3000V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_e3000v1);
		break;
	case BCM47XX_BOARD_LINKSYS_E3200V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_e3200v1);
		break;
	case BCM47XX_BOARD_LINKSYS_E4200V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_e4200v1);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT150NV1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_wrt150nv1);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT150NV11:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_wrt150nv11);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT160NV1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_wrt160nv1);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT160NV3:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_wrt160nv3);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT300N_V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_wrt300n_v1);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT300NV11:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_wrt300nv11);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT310NV1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_wrt310nv1);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT54G3GV2:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_wrt54g3gv2);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT54G_TYPE_0101:
	case BCM47XX_BOARD_LINKSYS_WRT54G_TYPE_0467:
	case BCM47XX_BOARD_LINKSYS_WRT54G_TYPE_0708:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_wrt54g_generic);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT610NV1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_wrt610nv1);
		break;
	case BCM47XX_BOARD_LINKSYS_WRT610NV2:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_wrt610nv2);
		break;
	case BCM47XX_BOARD_LINKSYS_WRTSL54GS:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_linksys_wrtsl54gs);
		break;

	case BCM47XX_BOARD_LUXUL_ABR_4400_V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_luxul_abr_4400_v1);
		break;
	case BCM47XX_BOARD_LUXUL_XAP_310_V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_luxul_xap_310_v1);
		break;
	case BCM47XX_BOARD_LUXUL_XAP_1210_V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_luxul_xap_1210_v1);
		break;
	case BCM47XX_BOARD_LUXUL_XAP_1230_V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_luxul_xap_1230_v1);
		break;
	case BCM47XX_BOARD_LUXUL_XAP_1240_V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_luxul_xap_1240_v1);
		break;
	case BCM47XX_BOARD_LUXUL_XAP_1500_V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_luxul_xap_1500_v1);
		break;
	case BCM47XX_BOARD_LUXUL_XBR_4400_V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_luxul_xbr_4400_v1);
		break;
	case BCM47XX_BOARD_LUXUL_XVW_P30_V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_luxul_xvw_p30_v1);
		break;
	case BCM47XX_BOARD_LUXUL_XWR_600_V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_luxul_xwr_600_v1);
		break;
	case BCM47XX_BOARD_LUXUL_XWR_1750_V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_luxul_xwr_1750_v1);
		break;

	case BCM47XX_BOARD_MICROSOFT_MN700:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_microsoft_nm700);
		break;

	case BCM47XX_BOARD_MOTOROLA_WE800G:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_motorola_we800g);
		break;
	case BCM47XX_BOARD_MOTOROLA_WR850GP:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_motorola_wr850gp);
		break;
	case BCM47XX_BOARD_MOTOROLA_WR850GV2V3:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_motorola_wr850gv2v3);
		break;

	case BCM47XX_BOARD_NETGEAR_WNDR3400V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_netgear_wndr3400v1);
		break;
	case BCM47XX_BOARD_NETGEAR_WNDR3400_V3:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_netgear_wndr3400_v3);
		break;
	case BCM47XX_BOARD_NETGEAR_WNDR3700V3:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_netgear_wndr3700v3);
		break;
	case BCM47XX_BOARD_NETGEAR_WNDR4500V1:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_netgear_wndr4500v1);
		break;
	case BCM47XX_BOARD_NETGEAR_WNR1000_V3:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_netgear_wnr1000_v3);
		break;
	case BCM47XX_BOARD_NETGEAR_WNR3500L:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_netgear_wnr3500lv1);
		break;
	case BCM47XX_BOARD_NETGEAR_WNR834BV2:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_netgear_wnr834bv2);
		break;

	case BCM47XX_BOARD_SIMPLETECH_SIMPLESHARE:
		err = bcm47xx_copy_bdata(bcm47xx_buttons_simpletech_simpleshare);
		break;

	default:
		pr_debug("No buttons configuration found for this device\n");
		return -ENOTSUPP;
	}

	if (err)
		return -ENOMEM;

	err = platform_device_register(&bcm47xx_buttons_gpio_keys);
	if (err) {
		pr_err("Failed to register platform device: %d\n", err);
		return err;
	}

	return 0;
}
