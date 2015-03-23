/*
 * wpa_supplicant/hostapd / Debug prints
 * Copyright (c) 2002-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef WPA_DEBUG_H
#define WPA_DEBUG_H

#include "wpabuf.h"

/* Debugging function - conditional printf and hex dump. Driver wrappers can
 * use these for debugging purposes. */

enum {
	MSG_EXCESSIVE, MSG_MSGDUMP, MSG_DEBUG, MSG_INFO, MSG_WARNING, MSG_ERROR
};

#ifdef CONFIG_ANDROID_LOG

#define wpa_debug_print_timestamp() do {} while (0)
#define wpa_hexdump(...)            do {} while (0)
#define wpa_hexdump_key(...)        do {} while (0)
#define wpa_hexdump_buf(l,t,b)      do {} while (0)
#define wpa_hexdump_buf_key(l,t,b)  do {} while (0)
#define wpa_hexdump_ascii(...)      do {} while (0)
#define wpa_hexdump_ascii_key(...)  do {} while (0)
#define wpa_debug_open_file(...)    do {} while (0)
#define wpa_debug_close_file()      do {} while (0)
#define wpa_dbg(...)                do {} while (0)

static inline int wpa_debug_reopen_file(void)
{
	return 0;
}


void android_printf(int level, char *format, ...)
PRINTF_FORMAT(2, 3);

#define wpa_printf android_printf

#else /* CONFIG_ANDROID_LOG */

#ifdef CONFIG_NO_STDOUT_DEBUG

#define wpa_debug_print_timestamp() do { } while (0)
#define wpa_printf(args...) do { } while (0)
#define wpa_hexdump(l,t,b,le) do { } while (0)
#define wpa_hexdump_buf(l,t,b) do { } while (0)
#define wpa_hexdump_key(l,t,b,le) do { } while (0)
#define wpa_hexdump_buf_key(l,t,b) do { } while (0)
#define wpa_hexdump_ascii(l,t,b,le) do { } while (0)
#define wpa_hexdump_ascii_key(l,t,b,le) do { } while (0)
#define wpa_debug_open_file(p) do { } while (0)
#define wpa_debug_close_file() do { } while (0)
#define wpa_dbg(args...) do { } while (0)

static inline int wpa_debug_reopen_file(void)
{
	return 0;
}

#else /* CONFIG_NO_STDOUT_DEBUG */

int wpa_debug_open_file(const char *path);
int wpa_debug_reopen_file(void);
void wpa_debug_close_file(void);

/**
 * wpa_debug_printf_timestamp - Print timestamp for debug output
 *
 * This function prints a timestamp in seconds_from_1970.microsoconds
 * format if debug output has been configured to include timestamps in debug
 * messages.
 */
void wpa_debug_print_timestamp(void);

/**
 * wpa_printf - conditional printf
 * @level: priority level (MSG_*) of the message
 * @fmt: printf format string, followed by optional arguments
 *
 * This function is used to print conditional debugging and error messages. The
 * output may be directed to stdout, stderr, and/or syslog based on
 * configuration.
 *
 * Note: New line '\n' is added to the end of the text when printing to stdout.
 */
void wpa_printf(int level, const char *fmt, ...)
PRINTF_FORMAT(2, 3);

/**
 * wpa_hexdump - conditional hex dump
 * @level: priority level (MSG_*) of the message
 * @title: title of for the message
 * @buf: data buffer to be dumped
 * @len: length of the buf
 *
 * This function is used to print conditional debugging and error messages. The
 * output may be directed to stdout, stderr, and/or syslog based on
 * configuration. The contents of buf is printed out has hex dump.
 */
void wpa_hexdump(int level, const char *title, const u8 *buf, size_t len);

static inline void wpa_hexdump_buf(int level, const char *title,
				   const struct wpabuf *buf)
{
	wpa_hexdump(level, title, buf ? wpabuf_head(buf) : NULL,
		    buf ? wpabuf_len(buf) : 0);
}

/**
 * wpa_hexdump_key - conditional hex dump, hide keys
 * @level: priority level (MSG_*) of the message
 * @title: title of for the message
 * @buf: data buffer to be dumped
 * @len: length of the buf
 *
 * This function is used to print conditional debugging and error messages. The
 * output may be directed to stdout, stderr, and/or syslog based on
 * configuration. The contents of buf is printed out has hex dump. This works
 * like wpa_hexdump(), but by default, does not include secret keys (passwords,
 * etc.) in debug output.
 */
void wpa_hexdump_key(int level, const char *title, const u8 *buf, size_t len);

static inline void wpa_hexdump_buf_key(int level, const char *title,
				       const struct wpabuf *buf)
{
	wpa_hexdump_key(level, title, buf ? wpabuf_head(buf) : 0,
			buf ? wpabuf_len(buf) : 0);
}

/**
 * wpa_hexdump_ascii - conditional hex dump
 * @level: priority level (MSG_*) of the message
 * @title: title of for the message
 * @buf: data buffer to be dumped
 * @len: length of the buf
 *
 * This function is used to print conditional debugging and error messages. The
 * output may be directed to stdout, stderr, and/or syslog based on
 * configuration. The contents of buf is printed out has hex dump with both
 * the hex numbers and ASCII characters (for printable range) are shown. 16
 * bytes per line will be shown.
 */
void wpa_hexdump_ascii(int level, const char *title, const u8 *buf,
		       size_t len);

/**
 * wpa_hexdump_ascii_key - conditional hex dump, hide keys
 * @level: priority level (MSG_*) of the message
 * @title: title of for the message
 * @buf: data buffer to be dumped
 * @len: length of the buf
 *
 * This function is used to print conditional debugging and error messages. The
 * output may be directed to stdout, stderr, and/or syslog based on
 * configuration. The contents of buf is printed out has hex dump with both
 * the hex numbers and ASCII characters (for printable range) are shown. 16
 * bytes per line will be shown. This works like wpa_hexdump_ascii(), but by
 * default, does not include secret keys (passwords, etc.) in debug output.
 */
void wpa_hexdump_ascii_key(int level, const char *title, const u8 *buf,
			   size_t len);

/*
 * wpa_dbg() behaves like wpa_msg(), but it can be removed from build to reduce
 * binary size. As such, it should be used with debugging messages that are not
 * needed in the control interface while wpa_msg() has to be used for anything
 * that needs to shown to control interface monitors.
 */
#define wpa_dbg(args...) wpa_msg(args)

#endif /* CONFIG_NO_STDOUT_DEBUG */

#endif /* CONFIG_ANDROID_LOG */


#ifdef CONFIG_NO_WPA_MSG
#define wpa_msg(args...) do { } while (0)
#define wpa_msg_ctrl(args...) do { } while (0)
#define wpa_msg_register_cb(f) do { } while (0)
#define wpa_msg_register_ifname_cb(f) do { } while (0)
#else /* CONFIG_NO_WPA_MSG */
/**
 * wpa_msg - Conditional printf for default target and ctrl_iface monitors
 * @ctx: Pointer to context data; this is the ctx variable registered
 *	with struct wpa_driver_ops::init()
 * @level: priority level (MSG_*) of the message
 * @fmt: printf format string, followed by optional arguments
 *
 * This function is used to print conditional debugging and error messages. The
 * output may be directed to stdout, stderr, and/or syslog based on
 * configuration. This function is like wpa_printf(), but it also sends the
 * same message to all attached ctrl_iface monitors.
 *
 * Note: New line '\n' is added to the end of the text when printing to stdout.
 */
void wpa_msg(void *ctx, int level, const char *fmt, ...) PRINTF_FORMAT(3, 4);

/**
 * wpa_msg_ctrl - Conditional printf for ctrl_iface monitors
 * @ctx: Pointer to context data; this is the ctx variable registered
 *	with struct wpa_driver_ops::init()
 * @level: priority level (MSG_*) of the message
 * @fmt: printf format string, followed by optional arguments
 *
 * This function is used to print conditional debugging and error messages.
 * This function is like wpa_msg(), but it sends the output only to the
 * attached ctrl_iface monitors. In other words, it can be used for frequent
 * events that do not need to be sent to syslog.
 */
void wpa_msg_ctrl(void *ctx, int level, const char *fmt, ...)
PRINTF_FORMAT(3, 4);

typedef void (*wpa_msg_cb_func)(void *ctx, int level, const char *txt,
				size_t len);

/**
 * wpa_msg_register_cb - Register callback function for wpa_msg() messages
 * @func: Callback function (%NULL to unregister)
 */
void wpa_msg_register_cb(wpa_msg_cb_func func);

typedef const char * (*wpa_msg_get_ifname_func)(void *ctx);
void wpa_msg_register_ifname_cb(wpa_msg_get_ifname_func func);

#endif /* CONFIG_NO_WPA_MSG */

#ifdef CONFIG_NO_HOSTAPD_LOGGER
#define hostapd_logger(args...) do { } while (0)
#define hostapd_logger_register_cb(f) do { } while (0)
#else /* CONFIG_NO_HOSTAPD_LOGGER */
void hostapd_logger(void *ctx, const u8 *addr, unsigned int module, int level,
		    const char *fmt, ...) PRINTF_FORMAT(5, 6);

typedef void (*hostapd_logger_cb_func)(void *ctx, const u8 *addr,
				       unsigned int module, int level,
				       const char *txt, size_t len);

/**
 * hostapd_logger_register_cb - Register callback function for hostapd_logger()
 * @func: Callback function (%NULL to unregister)
 */
void hostapd_logger_register_cb(hostapd_logger_cb_func func);
#endif /* CONFIG_NO_HOSTAPD_LOGGER */

#define HOSTAPD_MODULE_IEEE80211	0x00000001
#define HOSTAPD_MODULE_IEEE8021X	0x00000002
#define HOSTAPD_MODULE_RADIUS		0x00000004
#define HOSTAPD_MODULE_WPA		0x00000008
#define HOSTAPD_MODULE_DRIVER		0x00000010
#define HOSTAPD_MODULE_IAPP		0x00000020
#define HOSTAPD_MODULE_MLME		0x00000040

enum hostapd_logger_level {
	HOSTAPD_LEVEL_DEBUG_VERBOSE = 0,
	HOSTAPD_LEVEL_DEBUG = 1,
	HOSTAPD_LEVEL_INFO = 2,
	HOSTAPD_LEVEL_NOTICE = 3,
	HOSTAPD_LEVEL_WARNING = 4
};


#ifdef CONFIG_DEBUG_SYSLOG

void wpa_debug_open_syslog(void);
void wpa_debug_close_syslog(void);

#else /* CONFIG_DEBUG_SYSLOG */

static inline void wpa_debug_open_syslog(void)
{
}

static inline void wpa_debug_close_syslog(void)
{
}

#endif /* CONFIG_DEBUG_SYSLOG */


#ifdef EAPOL_TEST
#define WPA_ASSERT(a)						       \
	do {							       \
		if (!(a)) {					       \
			printf("WPA_ASSERT FAILED '" #a "' "	       \
			       "%s %s:%d\n",			       \
			       __FUNCTION__, __FILE__, __LINE__);      \
			exit(1);				       \
		}						       \
	} while (0)
#else
#define WPA_ASSERT(a) do { } while (0)
#endif

#endif /* WPA_DEBUG_H */
