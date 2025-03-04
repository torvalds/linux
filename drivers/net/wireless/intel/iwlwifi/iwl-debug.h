/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
 *
 * Copyright(c) 2003 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2018 - 2021, 2024-2025 Intel Corporation
 *
 * Portions of this file are derived from the ipw3945 project.
 *****************************************************************************/

#ifndef __iwl_debug_h__
#define __iwl_debug_h__

#include "iwl-modparams.h"


static inline bool iwl_have_debug_level(u32 level)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	return iwlwifi_mod_params.debug_level & level;
#else
	return false;
#endif
}

enum iwl_err_mode {
	IWL_ERR_MODE_REGULAR,
	IWL_ERR_MODE_RFKILL,
	IWL_ERR_MODE_TRACE_ONLY,
	IWL_ERR_MODE_RATELIMIT,
};

struct device;
void __iwl_err(struct device *dev, enum iwl_err_mode mode, const char *fmt, ...)
	__printf(3, 4);
void __iwl_warn(struct device *dev, const char *fmt, ...) __printf(2, 3);
void __iwl_info(struct device *dev, const char *fmt, ...) __printf(2, 3);
void __iwl_crit(struct device *dev, const char *fmt, ...) __printf(2, 3);

/* not all compilers can evaluate strlen() at compile time, so use sizeof() */
#define CHECK_FOR_NEWLINE(f) BUILD_BUG_ON(f[sizeof(f) - 2] != '\n')

/* No matter what is m (priv, bus, trans), this will work */
#define __IWL_ERR_DEV(d, mode, f, a...)					\
	do {								\
		CHECK_FOR_NEWLINE(f);					\
		__iwl_err((d), mode, f, ## a);				\
	} while (0)
#define IWL_ERR_DEV(d, f, a...)						\
	__IWL_ERR_DEV(d, IWL_ERR_MODE_REGULAR, f, ## a)
#define IWL_ERR(m, f, a...)						\
	IWL_ERR_DEV((m)->dev, f, ## a)
#define IWL_ERR_LIMIT(m, f, a...)					\
	__IWL_ERR_DEV((m)->dev, IWL_ERR_MODE_RATELIMIT, f, ## a)
#define IWL_WARN(m, f, a...)						\
	do {								\
		CHECK_FOR_NEWLINE(f);					\
		__iwl_warn((m)->dev, f, ## a);				\
	} while (0)
#define IWL_INFO(m, f, a...)						\
	do {								\
		CHECK_FOR_NEWLINE(f);					\
		__iwl_info((m)->dev, f, ## a);				\
	} while (0)
#define IWL_CRIT(m, f, a...)						\
	do {								\
		CHECK_FOR_NEWLINE(f);					\
		__iwl_crit((m)->dev, f, ## a);				\
	} while (0)

#if defined(CONFIG_IWLWIFI_DEBUG) || defined(CONFIG_IWLWIFI_DEVICE_TRACING)
void __iwl_dbg(struct device *dev,
	       u32 level, bool limit, const char *function,
	       const char *fmt, ...) __printf(5, 6);
#else
__printf(5, 6) static inline void
__iwl_dbg(struct device *dev,
	  u32 level, bool limit, const char *function,
	  const char *fmt, ...)
{}
#endif

#define iwl_print_hex_error(m, p, len)					\
do {									\
	print_hex_dump(KERN_ERR, "iwl data: ",				\
		       DUMP_PREFIX_OFFSET, 16, 1, p, len, 1);		\
} while (0)

#define __IWL_DEBUG_DEV(dev, level, limit, fmt, args...)		\
	do {								\
		CHECK_FOR_NEWLINE(fmt);					\
		__iwl_dbg(dev, level, limit, __func__, fmt, ##args);	\
	} while (0)
#define IWL_DEBUG(m, level, fmt, args...)				\
	__IWL_DEBUG_DEV((m)->dev, level, false, fmt, ##args)
#define IWL_DEBUG_DEV(dev, level, fmt, args...)				\
	__IWL_DEBUG_DEV(dev, level, false, fmt, ##args)
#define IWL_DEBUG_LIMIT(m, level, fmt, args...)				\
	__IWL_DEBUG_DEV((m)->dev, level, true, fmt, ##args)

#ifdef CONFIG_IWLWIFI_DEBUG
#define iwl_print_hex_dump(m, level, p, len)				\
do {                                            			\
	if (iwl_have_debug_level(level))				\
		print_hex_dump(KERN_DEBUG, "iwl data: ",		\
			       DUMP_PREFIX_OFFSET, 16, 1, p, len, 1);	\
} while (0)
#else
#define iwl_print_hex_dump(m, level, p, len)
#endif				/* CONFIG_IWLWIFI_DEBUG */

/*
 * To use the debug system:
 *
 * If you are defining a new debug classification, simply add it to the #define
 * list here in the form of
 *
 * #define IWL_DL_xxxx VALUE
 *
 * where xxxx should be the name of the classification (for example, WEP).
 *
 * You then need to either add a IWL_xxxx_DEBUG() macro definition for your
 * classification, or use IWL_DEBUG(IWL_DL_xxxx, ...) whenever you want
 * to send output to that classification.
 *
 * The active debug levels can be accessed via files
 *
 *	/sys/module/iwlwifi/parameters/debug
 * when CONFIG_IWLWIFI_DEBUG=y.
 *
 *	/sys/kernel/debug/phy0/iwlwifi/debug/debug_level
 * when CONFIG_IWLWIFI_DEBUGFS=y.
 *
 */

/* 0x0000000F - 0x00000001 */
#define IWL_DL_INFO		0x00000001
#define IWL_DL_MAC80211		0x00000002
#define IWL_DL_HCMD		0x00000004
#define IWL_DL_TDLS		0x00000008
/* 0x000000F0 - 0x00000010 */
#define IWL_DL_QUOTA		0x00000010
#define IWL_DL_TE		0x00000020
#define IWL_DL_EEPROM		0x00000040
#define IWL_DL_RADIO		0x00000080
/* 0x00000F00 - 0x00000100 */
#define IWL_DL_POWER		0x00000100
#define IWL_DL_TEMP		0x00000200
#define IWL_DL_WOWLAN		0x00000400
#define IWL_DL_SCAN		0x00000800
/* 0x0000F000 - 0x00001000 */
#define IWL_DL_ASSOC		0x00001000
#define IWL_DL_DROP		0x00002000
#define IWL_DL_LAR		0x00004000
#define IWL_DL_COEX		0x00008000
/* 0x000F0000 - 0x00010000 */
#define IWL_DL_FW		0x00010000
#define IWL_DL_RF_KILL		0x00020000
#define IWL_DL_TPT		0x00040000
#define IWL_DL_PTP		0x00080000
/* 0x00F00000 - 0x00100000 */
#define IWL_DL_RATE		0x00100000
#define IWL_DL_CALIB		0x00200000
#define IWL_DL_WEP		0x00400000
#define IWL_DL_TX		0x00800000
/* 0x0F000000 - 0x01000000 */
#define IWL_DL_RX		0x01000000
#define IWL_DL_ISR		0x02000000
#define IWL_DL_HT		0x04000000
#define IWL_DL_EHT		0x08000000
/* 0xF0000000 - 0x10000000 */
#define IWL_DL_11H		0x10000000
#define IWL_DL_STATS		0x20000000
#define IWL_DL_TX_REPLY		0x40000000
#define IWL_DL_TX_QUEUES	0x80000000

#define IWL_DEBUG_INFO(p, f, a...)	IWL_DEBUG(p, IWL_DL_INFO, f, ## a)
#define IWL_DEBUG_TDLS(p, f, a...)	IWL_DEBUG(p, IWL_DL_TDLS, f, ## a)
#define IWL_DEBUG_MAC80211(p, f, a...)	IWL_DEBUG(p, IWL_DL_MAC80211, f, ## a)
#define IWL_DEBUG_TEMP(p, f, a...)	IWL_DEBUG(p, IWL_DL_TEMP, f, ## a)
#define IWL_DEBUG_SCAN(p, f, a...)	IWL_DEBUG(p, IWL_DL_SCAN, f, ## a)
#define IWL_DEBUG_RX(p, f, a...)	IWL_DEBUG(p, IWL_DL_RX, f, ## a)
#define IWL_DEBUG_TX(p, f, a...)	IWL_DEBUG(p, IWL_DL_TX, f, ## a)
#define IWL_DEBUG_ISR(p, f, a...)	IWL_DEBUG(p, IWL_DL_ISR, f, ## a)
#define IWL_DEBUG_WEP(p, f, a...)	IWL_DEBUG(p, IWL_DL_WEP, f, ## a)
#define IWL_DEBUG_HC(p, f, a...)	IWL_DEBUG(p, IWL_DL_HCMD, f, ## a)
#define IWL_DEBUG_QUOTA(p, f, a...)	IWL_DEBUG(p, IWL_DL_QUOTA, f, ## a)
#define IWL_DEBUG_TE(p, f, a...)	IWL_DEBUG(p, IWL_DL_TE, f, ## a)
#define IWL_DEBUG_EEPROM(d, f, a...)	IWL_DEBUG_DEV(d, IWL_DL_EEPROM, f, ## a)
#define IWL_DEBUG_CALIB(p, f, a...)	IWL_DEBUG(p, IWL_DL_CALIB, f, ## a)
#define IWL_DEBUG_FW(p, f, a...)	IWL_DEBUG(p, IWL_DL_FW, f, ## a)
#define IWL_DEBUG_RF_KILL(p, f, a...)	IWL_DEBUG(p, IWL_DL_RF_KILL, f, ## a)
#define IWL_DEBUG_DROP(p, f, a...)	IWL_DEBUG(p, IWL_DL_DROP, f, ## a)
#define IWL_DEBUG_DROP_LIMIT(p, f, a...)	\
		IWL_DEBUG_LIMIT(p, IWL_DL_DROP, f, ## a)
#define IWL_DEBUG_COEX(p, f, a...)	IWL_DEBUG(p, IWL_DL_COEX, f, ## a)
#define IWL_DEBUG_RATE(p, f, a...)	IWL_DEBUG(p, IWL_DL_RATE, f, ## a)
#define IWL_DEBUG_RATE_LIMIT(p, f, a...)	\
		IWL_DEBUG_LIMIT(p, IWL_DL_RATE, f, ## a)
#define IWL_DEBUG_ASSOC(p, f, a...)	\
		IWL_DEBUG(p, IWL_DL_ASSOC | IWL_DL_INFO, f, ## a)
#define IWL_DEBUG_ASSOC_LIMIT(p, f, a...)	\
		IWL_DEBUG_LIMIT(p, IWL_DL_ASSOC | IWL_DL_INFO, f, ## a)
#define IWL_DEBUG_HT(p, f, a...)	IWL_DEBUG(p, IWL_DL_HT, f, ## a)
#define IWL_DEBUG_STATS(p, f, a...)	IWL_DEBUG(p, IWL_DL_STATS, f, ## a)
#define IWL_DEBUG_STATS_LIMIT(p, f, a...)	\
		IWL_DEBUG_LIMIT(p, IWL_DL_STATS, f, ## a)
#define IWL_DEBUG_TX_REPLY(p, f, a...)	IWL_DEBUG(p, IWL_DL_TX_REPLY, f, ## a)
#define IWL_DEBUG_TX_QUEUES(p, f, a...)	IWL_DEBUG(p, IWL_DL_TX_QUEUES, f, ## a)
#define IWL_DEBUG_RADIO(p, f, a...)	IWL_DEBUG(p, IWL_DL_RADIO, f, ## a)
#define IWL_DEBUG_DEV_RADIO(p, f, a...)	IWL_DEBUG_DEV(p, IWL_DL_RADIO, f, ## a)
#define IWL_DEBUG_POWER(p, f, a...)	IWL_DEBUG(p, IWL_DL_POWER, f, ## a)
#define IWL_DEBUG_DEV_POWER(p, f, a...)	IWL_DEBUG_DEV(p, IWL_DL_POWER, f, ## a)
#define IWL_DEBUG_11H(p, f, a...)	IWL_DEBUG(p, IWL_DL_11H, f, ## a)
#define IWL_DEBUG_TPT(p, f, a...)	IWL_DEBUG(p, IWL_DL_TPT, f, ## a)
#define IWL_DEBUG_WOWLAN(p, f, a...)	IWL_DEBUG(p, IWL_DL_WOWLAN, f, ## a)
#define IWL_DEBUG_LAR(p, f, a...)	IWL_DEBUG(p, IWL_DL_LAR, f, ## a)
#define IWL_DEBUG_FW_INFO(p, f, a...)		\
		IWL_DEBUG(p, IWL_DL_INFO | IWL_DL_FW, f, ## a)
#define IWL_DEBUG_PTP(p, f, a...)	IWL_DEBUG(p, IWL_DL_PTP, f, ## a)
#define IWL_DEBUG_EHT(p, f, a...)	IWL_DEBUG(p, IWL_DL_EHT, f, ## a)
#endif
