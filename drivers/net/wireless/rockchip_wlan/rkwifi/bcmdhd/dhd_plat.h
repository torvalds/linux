/*
 * DHD Linux platform header file
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#ifndef __DHD_PLAT_H__
#define __DHD_PLAT_H__

#include <linuxver.h>

#if !defined(CONFIG_WIFI_CONTROL_FUNC)
#define WLAN_PLAT_NODFS_FLAG	0x01
#define WLAN_PLAT_AP_FLAG	0x02
struct wifi_platform_data {
#ifdef BUS_POWER_RESTORE
	int (*set_power)(int val, wifi_adapter_info_t *adapter);
#else
	int (*set_power)(int val);
#endif
	int (*set_reset)(int val);
	int (*set_carddetect)(int val);
#ifdef DHD_COREDUMP
	int (*set_coredump)(const char *buf, int buf_len, const char *info);
#endif /* DHD_COREDUMP */
	void *(*mem_prealloc)(int section, unsigned long size);
#ifdef CUSTOM_MULTI_MAC
	int (*get_mac_addr)(unsigned char *buf, char *name);
#else
	int (*get_mac_addr)(unsigned char *buf);
#endif
#ifdef BCMSDIO
	int (*get_wake_irq)(void);
#endif
#ifdef CUSTOM_FORCE_NODFS_FLAG
	void *(*get_country_code)(char *ccode, u32 flags);
#else /* defined (CUSTOM_FORCE_NODFS_FLAG) */
	void *(*get_country_code)(char *ccode);
#endif
};
#endif /* CONFIG_WIFI_CONTROL_FUNC */

#endif /* __DHD_PLAT_H__ */
