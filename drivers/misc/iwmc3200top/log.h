/*
 * iwmc3200top - Intel Wireless MultiCom 3200 Top Driver
 * drivers/misc/iwmc3200top/log.h
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * Author Name: Maxim Grabarnik <maxim.grabarnink@intel.com>
 *  -
 *
 */

#ifndef __LOG_H__
#define __LOG_H__


/* log severity:
 * The log levels here match FW log levels
 * so values need to stay as is */
#define LOG_SEV_CRITICAL		0
#define LOG_SEV_ERROR			1
#define LOG_SEV_WARNING			2
#define LOG_SEV_INFO			3
#define LOG_SEV_INFOEX			4

#define LOG_SEV_FILTER_ALL		\
	(BIT(LOG_SEV_CRITICAL) |	\
	 BIT(LOG_SEV_ERROR)    |	\
	 BIT(LOG_SEV_WARNING)  | 	\
	 BIT(LOG_SEV_INFO)     |	\
	 BIT(LOG_SEV_INFOEX))

/* log source */
#define LOG_SRC_INIT			0
#define LOG_SRC_DEBUGFS			1
#define LOG_SRC_FW_DOWNLOAD		2
#define LOG_SRC_FW_MSG			3
#define LOG_SRC_TST			4
#define LOG_SRC_IRQ			5

#define	LOG_SRC_MAX			6
#define	LOG_SRC_ALL			0xFF

/**
 * Default intitialization runtime log level
 */
#ifndef LOG_SEV_FILTER_RUNTIME
#define LOG_SEV_FILTER_RUNTIME			\
	(BIT(LOG_SEV_CRITICAL)	|		\
	 BIT(LOG_SEV_ERROR)	|		\
	 BIT(LOG_SEV_WARNING))
#endif

#ifndef FW_LOG_SEV_FILTER_RUNTIME
#define FW_LOG_SEV_FILTER_RUNTIME	LOG_SEV_FILTER_ALL
#endif

#ifdef CONFIG_IWMC3200TOP_DEBUG
/**
 * Log macros
 */

#define priv2dev(priv) (&(priv->func)->dev)

#define LOG_CRITICAL(priv, src, fmt, args...)				\
do {									\
	if (iwmct_logdefs[LOG_SRC_ ## src] & BIT(LOG_SEV_CRITICAL))	\
		dev_crit(priv2dev(priv), "%s %d: " fmt,			\
			__func__, __LINE__, ##args);			\
} while (0)

#define LOG_ERROR(priv, src, fmt, args...)				\
do {									\
	if (iwmct_logdefs[LOG_SRC_ ## src] & BIT(LOG_SEV_ERROR))	\
		dev_err(priv2dev(priv), "%s %d: " fmt,			\
			__func__, __LINE__, ##args);			\
} while (0)

#define LOG_WARNING(priv, src, fmt, args...)				\
do {									\
	if (iwmct_logdefs[LOG_SRC_ ## src] & BIT(LOG_SEV_WARNING))	\
		dev_warn(priv2dev(priv), "%s %d: " fmt,			\
			 __func__, __LINE__, ##args);			\
} while (0)

#define LOG_INFO(priv, src, fmt, args...)				\
do {									\
	if (iwmct_logdefs[LOG_SRC_ ## src] & BIT(LOG_SEV_INFO))		\
		dev_info(priv2dev(priv), "%s %d: " fmt,			\
			 __func__, __LINE__, ##args);			\
} while (0)

#define LOG_INFOEX(priv, src, fmt, args...)				\
do {									\
	if (iwmct_logdefs[LOG_SRC_ ## src] & BIT(LOG_SEV_INFOEX))	\
		dev_dbg(priv2dev(priv), "%s %d: " fmt,			\
			 __func__, __LINE__, ##args);			\
} while (0)

#define LOG_HEXDUMP(src, ptr, len)					\
do {									\
	if (iwmct_logdefs[LOG_SRC_ ## src] & BIT(LOG_SEV_INFOEX))	\
		print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_NONE,	\
				16, 1, ptr, len, false);		\
} while (0)

void iwmct_log_top_message(struct iwmct_priv *priv, u8 *buf, int len);

extern u8 iwmct_logdefs[];

int iwmct_log_set_filter(u8 src, u8 logmask);
int iwmct_log_set_fw_filter(u8 src, u8 logmask);

ssize_t show_iwmct_log_level(struct device *d,
			struct device_attribute *attr, char *buf);
ssize_t store_iwmct_log_level(struct device *d,
			struct device_attribute *attr,
			const char *buf, size_t count);
ssize_t show_iwmct_log_level_fw(struct device *d,
			struct device_attribute *attr, char *buf);
ssize_t store_iwmct_log_level_fw(struct device *d,
			struct device_attribute *attr,
			const char *buf, size_t count);

#else

#define LOG_CRITICAL(priv, src, fmt, args...)
#define LOG_ERROR(priv, src, fmt, args...)
#define LOG_WARNING(priv, src, fmt, args...)
#define LOG_INFO(priv, src, fmt, args...)
#define LOG_INFOEX(priv, src, fmt, args...)
#define LOG_HEXDUMP(src, ptr, len)

static inline void iwmct_log_top_message(struct iwmct_priv *priv,
					 u8 *buf, int len) {}
static inline int iwmct_log_set_filter(u8 src, u8 logmask) { return 0; }
static inline int iwmct_log_set_fw_filter(u8 src, u8 logmask) { return 0; }

#endif /* CONFIG_IWMC3200TOP_DEBUG */

int log_get_filter_str(char *buf, int size);
int log_get_fw_filter_str(char *buf, int size);

#endif /* __LOG_H__ */
