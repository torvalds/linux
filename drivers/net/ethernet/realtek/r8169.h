/* SPDX-License-Identifier: GPL-2.0-only */
/* r8169.h: RealTek 8169/8168/8101 ethernet driver.
 *
 * Copyright (c) 2002 ShuChen <shuchen@realtek.com.tw>
 * Copyright (c) 2003 - 2007 Francois Romieu <romieu@fr.zoreil.com>
 * Copyright (c) a lot of people too. Please respect their work.
 *
 * See MAINTAINERS file for support contact information.
 */

#include <linux/types.h>
#include <linux/phy.h>

enum mac_version {
	/* support for ancient RTL_GIGA_MAC_VER_01 has been removed */
	RTL_GIGA_MAC_VER_02,
	RTL_GIGA_MAC_VER_03,
	RTL_GIGA_MAC_VER_04,
	RTL_GIGA_MAC_VER_05,
	RTL_GIGA_MAC_VER_06,
	RTL_GIGA_MAC_VER_07,
	RTL_GIGA_MAC_VER_08,
	RTL_GIGA_MAC_VER_09,
	RTL_GIGA_MAC_VER_10,
	RTL_GIGA_MAC_VER_11,
	RTL_GIGA_MAC_VER_12,
	RTL_GIGA_MAC_VER_13,
	RTL_GIGA_MAC_VER_14,
	RTL_GIGA_MAC_VER_16,
	RTL_GIGA_MAC_VER_17,
	RTL_GIGA_MAC_VER_18,
	RTL_GIGA_MAC_VER_19,
	RTL_GIGA_MAC_VER_20,
	RTL_GIGA_MAC_VER_21,
	RTL_GIGA_MAC_VER_22,
	RTL_GIGA_MAC_VER_23,
	RTL_GIGA_MAC_VER_24,
	RTL_GIGA_MAC_VER_25,
	RTL_GIGA_MAC_VER_26,
	RTL_GIGA_MAC_VER_27,
	RTL_GIGA_MAC_VER_28,
	RTL_GIGA_MAC_VER_29,
	RTL_GIGA_MAC_VER_30,
	RTL_GIGA_MAC_VER_31,
	RTL_GIGA_MAC_VER_32,
	RTL_GIGA_MAC_VER_33,
	RTL_GIGA_MAC_VER_34,
	RTL_GIGA_MAC_VER_35,
	RTL_GIGA_MAC_VER_36,
	RTL_GIGA_MAC_VER_37,
	RTL_GIGA_MAC_VER_38,
	RTL_GIGA_MAC_VER_39,
	RTL_GIGA_MAC_VER_40,
	RTL_GIGA_MAC_VER_41,
	RTL_GIGA_MAC_VER_42,
	RTL_GIGA_MAC_VER_43,
	RTL_GIGA_MAC_VER_44,
	RTL_GIGA_MAC_VER_45,
	RTL_GIGA_MAC_VER_46,
	RTL_GIGA_MAC_VER_47,
	RTL_GIGA_MAC_VER_48,
	RTL_GIGA_MAC_VER_49,
	RTL_GIGA_MAC_VER_50,
	RTL_GIGA_MAC_VER_51,
	RTL_GIGA_MAC_VER_52,
	RTL_GIGA_MAC_VER_60,
	RTL_GIGA_MAC_VER_61,
	RTL_GIGA_MAC_VER_63,
	RTL_GIGA_MAC_NONE
};

struct rtl8169_private;

void r8169_apply_firmware(struct rtl8169_private *tp);
u16 rtl8168h_2_get_adc_bias_ioffset(struct rtl8169_private *tp);
u8 rtl8168d_efuse_read(struct rtl8169_private *tp, int reg_addr);
void r8169_hw_phy_config(struct rtl8169_private *tp, struct phy_device *phydev,
			 enum mac_version ver);
