/*
 * arch/arm/plat-meson/include/plat/hdmi_config.h
 *
 * Copyright (C) 2010-2014 Amlogic, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __PLAT_MESON_HDMI_CONFIG_H
#define __PLAT_MESON_HDMI_CONFIG_H

struct hdmi_phy_set_data {
	unsigned long freq;
	unsigned long addr;
	unsigned long data;
};

struct vendor_info_data {
	unsigned char *vendor_name;	    // Max Chars: 8
	unsigned int  vendor_id;	    // 3 Bytes, Refer to http://standards.ieee.org/develop/regauth/oui/oui.txt
	unsigned char *product_desc;	// Max Chars: 16
	unsigned char *cec_osd_string;	// Max Chars: 14
	unsigned int  cec_config;       // 4 bytes: use to control cec switch on/off
	unsigned int  ao_cec;           //switch between ao-cec and ee-cec:: 1: ao-cec; 0: ee-cec
};

enum pwr_type {
	NONE = 0,
	CPU_GPO = 1,
	AXP202,
};

struct pwr_cpu_gpo {
	unsigned int pin;
	unsigned int val;
};

struct pwr_axp202 {
	unsigned int pin;
	unsigned int val;
};

struct pwr_ctl_var {
	enum pwr_type type;
	union {
		struct pwr_cpu_gpo gpo;
		struct pwr_axp202 axp202;
	} var;
};

struct hdmi_pwr_ctl {
	struct pwr_ctl_var pwr_5v_on;
	struct pwr_ctl_var pwr_5v_off;
	struct pwr_ctl_var pwr_3v3_on;
	struct pwr_ctl_var pwr_3v3_off;
	struct pwr_ctl_var pwr_hpll_vdd_on;
	struct pwr_ctl_var pwr_hpll_vdd_off;
	int pwr_level;
};

struct hdmi_config_platform_data {
	void (*hdmi_sspll_ctrl)(unsigned int level);	// SSPLL control level
	struct hdmi_phy_set_data *phy_data;		// For some boards, HDMI PHY setting may diff from ref board.
	struct vendor_info_data *vend_data;
	struct hdmi_pwr_ctl *pwr_ctl;
};

#endif //__PLAT_MESON_HDMI_CONFIG_H
