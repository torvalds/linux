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

#include "includes.h"

#include "common.h"

#ifdef CONFIG_DEBUG_SYSLOG
#include <syslog.h>

static int wpa_debug_syslog = 0;
#endif /* CONFIG_DEBUG_SYSLOG */


int wpa_debug_level = MSG_INFO;
int wpa_debug_show_keys = 0;
int wpa_debug_timestamp = 0;


#ifdef CONFIG_ANDROID_LOG

#include <android/log.h>

void android_printf(int level, char *format, ...)
{
	if (level >= wpa_debug_level) {
		va_list ap;
		if (level == MSG_ERROR)
			level = ANDROID_LOG_ERROR;
		else if (level == MSG_WARNING)
			level = ANDROID_LOG_WARN;
		else if (level == MSG_INFO)
			level = ANDROID_LOG_INFO;
		else
			level = ANDROID_LOG_DEBUG;
		va_start(ap, format);
		__android_log_vprint(level, "wpa_supplicant", format, ap);
		va_end(ap);
	}
}

#else /* CONFIG_ANDROID_LOG */

#ifndef CONFIG_NO_STDOUT_DEBUG

#ifdef CONFIG_DEBUG_FILE
static FILE *out_file = NULL;
#endif /* CONFIG_DEBUG_FILE */


void wpa_debug_print_timestamp(void)
{
	struct os_time tv;

	if (!wpa_debug_timestamp)
		return;

	os_get_time(&tv);
#ifdef CONFIG_DEBUG_FILE
	if (out_file) {
		fprintf(out_file, "%ld.%06u: ", (long) tv.sec,
			(unsigned int) tv.usec);
	} else
#endif /* CONFIG_DEBUG_FILE */
	printf("%ld.%06u: ", (long) tv.sec, (unsigned int) tv.usec);
}


#ifdef CONFIG_DEBUG_SYSLOG
#ifndef LOG_HOSTAPD
#define LOG_HOSTAPD LOG_DAEMON
#endif /* LOG_HOSTAPD */

void wpa_debug_open_syslog(void)
{
	openlog("wpa_supplicant", LOG_PID | LOG_NDELAY, LOG_HOSTAPD);
	wpa_debug_syslog++;
}


void wpa_debug_close_syslog(void)
{
	if (wpa_debug_syslog)
		closelog();
}


static int syslog_priority(int level)
{
	switch (level) {
	case MSG_MSGDUMP:
	case MSG_DEBUG:
		return LOG_DEBUG;
	case MSG_INFO:
		return LOG_NOTICE;
	case MSG_WARNING:
		return LOG_WARNING;
	case MSG_ERROR:
		return LOG_ERR;
	}
	return LOG_INFO;
}
#endif /* CONFIG_DEBUG_SYSLOG */


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
{
	va_list ap;

	va_start(ap, fmt);
	if (level >= wpa_debug_level) {
#ifdef CONFIG_DEBUG_SYSLOG
		if (wpa_debug_syslog) {
			vsyslog(syslog_priority(level), fmt, ap);
		} else {
#endif /* CONFIG_DEBUG_SYSLOG */
		wpa_debug_print_timestamp();
#ifdef CONFIG_DEBUG_FILE
		if (out_file) {
			vfprintf(out_file, fmt, ap);
			fprintf(out_file, "\n");
		} else {
#endif /* CONFIG_DEBUG_FILE */
		vprintf(fmt, ap);
		printf("\n");
#ifdef CONFIG_DEBUG_FILE
		}
#endif /* CONFIG_DEBUG_FILE */
#ifdef CONFIG_DEBUG_SYSLOG
		}
#endif /* CONFIG_DEBUG_SYSLOG */
	}
	va_end(ap);
}


static void _wpa_hexdump(int level, const char *title, const u8 *buf,
			 size_t len, int show)
{
	size_t i;
	if (level < wpa_debug_level)
		return;
	wpa_debug_print_timestamp();
#ifdef CONFIG_DEBUG_FILE
	if (out_file) {
		fprintf(out_file, "%s - hexdump(len=%lu):",
			title, (unsigned long) len);
		if (buf == NULL) {
			fprintf(out_file, " [NULL]");
		} else if (show) {
			for (i = 0; i < len; i++)
				fprintf(out_file, " %02x", buf[i]);
		} else {
			fprintf(out_file, " [REMOVED]");
		}
		fprintf(out_file, "\n");
	} else {
#endif /* CONFIG_DEBUG_FILE */
	printf("%s - hexdump(len=%lu):", title, (unsigned long) len);
	if (buf == NULL) {
		printf(" [NULL]");
	} else if (show) {
		for (i = 0; i < len; i++)
			printf(" %02x", buf[i]);
	} else {
		printf(" [REMOVED]");
	}
	printf("\n");
#ifdef CONFIG_DEBUG_FILE
	}
#endif /* CONFIG_DEBUG_FILE */
}

void wpa_hexdump(int level, const char *title, const u8 *buf, size_t len)
{
	_wpa_hexdump(level, title, buf, len, 1);
}


void wpa_hexdump_key(int level, const char *title, const u8 *buf, size_t len)
{
	_wpa_hexdump(level, title, buf, len, wpa_debug_show_keys);
}


static void _wpa_hexdump_ascii(int level, const char *title, const u8 *buf,
			       size_t len, int show)
{
	size_t i, llen;
	const u8 *pos = buf;
	const size_t line_len = 16;

	if (level < wpa_debug_level)
		return;
	wpa_debug_print_timestamp();
#ifdef CONFIG_DEBUG_FILE
	if (out_file) {
		if (!show) {
			fprintf(out_file,
				"%s - hexdump_ascii(len=%lu): [REMOVED]\n",
				title, (unsigned long) len);
			return;
		}
		if (buf == NULL) {
			fprintf(out_file,
				"%s - hexdump_ascii(len=%lu): [NULL]\n",
				title, (unsigned long) len);
			return;
		}
		fprintf(out_file, "%s - hexdump_ascii(len=%lu):\n",
			title, (unsigned long) len);
		while (len) {
			llen = len > line_len ? line_len : len;
			fprintf(out_file, "    ");
			for (i = 0; i < llen; i++)
				fprintf(out_file, " %02x", pos[i]);
			for (i = llen; i < line_len; i++)
				fprintf(out_file, "   ");
			fprintf(out_file, "   ");
			for (i = 0; i < llen; i++) {
				if (isprint(pos[i]))
					fprintf(out_file, "%c", pos[i]);
				else
					fprintf(out_file, "_");
			}
			for (i = llen; i < line_len; i++)
				fprintf(out_file, " ");
			fprintf(out_file, "\n");
			pos += llen;
			len -= llen;
		}
	} else {
#endif /* CONFIG_DEBUG_FILE */
	if (!show) {
		printf("%s - hexdump_ascii(len=%lu): [REMOVED]\n",
		       title, (unsigned long) len);
		return;
	}
	if (buf == NULL) {
		printf("%s - hexdump_ascii(len=%lu): [NULL]\n",
		       title, (unsigned long) len);
		return;
	}
	printf("%s - hexdump_ascii(len=%lu):\n", title, (unsigned long) len);
	while (len) {
		llen = len > line_len ? line_len : len;
		printf("    ");
		for (i = 0; i < llen; i++)
			printf(" %02x", pos[i]);
		for (i = llen; i < line_len; i++)
			printf("   ");
		printf("   ");
		for (i = 0; i < llen; i++) {
			if (isprint(pos[i]))
				printf("%c", pos[i]);
			else
				printf("_");
		}
		for (i = llen; i < line_len; i++)
			printf(" ");
		printf("\n");
		pos += llen;
		len -= llen;
	}
#ifdef CONFIG_DEBUG_FILE
	}
#endif /* CONFIG_DEBUG_FILE */
}


void wpa_hexdump_ascii(int level, const char *title, const u8 *buf, size_t len)
{
	_wpa_hexdump_ascii(level, title, buf, len, 1);
}


void wpa_hexdump_ascii_key(int level, const char *title, const u8 *buf,
			   size_t len)
{
	_wpa_hexdump_ascii(level, title, buf, len, wpa_debug_show_keys);
}


#ifdef CONFIG_DEBUG_FILE
static char *last_path = NULL;
#endif /* CONFIG_DEBUG_FILE */

int wpa_debug_reopen_file(void)
{
#ifdef CONFIG_DEBUG_FILE
	int rv;
	if (last_path) {
		char *tmp = os_strdup(last_path);
		wpa_debug_close_file();
		rv = wpa_debug_open_file(tmp);
		os_free(tmp);
	} else {
		wpa_printf(MSG_ERROR, "Last-path was not set, cannot "
			   "re-open log file.");
		rv = -1;
	}
	return rv;
#else /* CONFIG_DEBUG_FILE */
	return 0;
#endif /* CONFIG_DEBUG_FILE */
}


int wpa_debug_open_file(const char *path)
{
#ifdef CONFIG_DEBUG_FILE
	if (!path)
		return 0;

	if (last_path == NULL || os_strcmp(last_path, path) != 0) {
		/* Save our path to enable re-open */
		os_free(last_path);
		last_path = os_strdup(path);
	}

	out_file = fopen(path, "a");
	if (out_file == NULL) {
		wpa_printf(MSG_ERROR, "wpa_debug_open_file: Failed to open "
			   "output file, using standard output");
		return -1;
	}
#ifndef _WIN32
	setvbuf(out_file, NULL, _IOLBF, 0);
#endif /* _WIN32 */
#endif /* CONFIG_DEBUG_FILE */
	return 0;
}


void wpa_debug_close_file(void)
{
#ifdef CONFIG_DEBUG_FILE
	if (!out_file)
		return;
	fclose(out_file);
	out_file = NULL;
	os_free(last_path);
	last_path = NULL;
#endif /* CONFIG_DEBUG_FILE */
}

#endif /* CONFIG_NO_STDOUT_DEBUG */

#endif /* CONFIG_ANDROID_LOG */

#ifndef CONFIG_NO_WPA_MSG
static wpa_msg_cb_func wpa_msg_cb = NULL;

void wpa_msg_register_cb(wpa_msg_cb_func func)
{
	wpa_msg_cb = func;
}


static wpa_msg_get_ifname_func wpa_msg_ifname_cb = NULL;

void wpa_msg_register_ifname_cb(wpa_msg_get_ifname_func func)
{
	wpa_msg_ifname_cb = func;
}


void wpa_msg(void *ctx, int level, const char *fmt, ...)
{
	va_list ap;
	char *buf;
	const int buflen = 2048;
	int len;
	char prefix[130];

	buf = os_malloc(buflen);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR, "wpa_msg: Failed to allocate message "
			   "buffer");
		return;
	}
	va_start(ap, fmt);
	prefix[0] = '\0';
	if (wpa_msg_ifname_cb) {
		const char *ifname = wpa_msg_ifname_cb(ctx);
		if (ifname) {
			int res = os_snprintf(prefix, sizeof(prefix), "%s: ",
					      ifname);
			if (res < 0 || res >= (int) sizeof(prefix))
				prefix[0] = '\0';
		}
	}
	len = vsnprintf(buf, buflen, fmt, ap);
	va_end(ap);
	wpa_printf(level, "%s%s", prefix, buf);
	if (wpa_msg_cb)
		wpa_msg_cb(ctx, level, buf, len);
	os_free(buf);
}


void wpa_msg_ctrl(void *ctx, int level, const char *fmt, ...)
{
	va_list ap;
	char *buf;
	const int buflen = 2048;
	int len;

	if (!wpa_msg_cb)
		return;

	buf = os_malloc(buflen);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR, "wpa_msg_ctrl: Failed to allocate "
			   "message buffer");
		return;
	}
	va_start(ap, fmt);
	len = vsnprintf(buf, buflen, fmt, ap);
	va_end(ap);
	wpa_msg_cb(ctx, level, buf, len);
	os_free(buf);
}
#endif /* CONFIG_NO_WPA_MSG */


#ifndef CONFIG_NO_HOSTAPD_LOGGER
static hostapd_logger_cb_func hostapd_logger_cb = NULL;

void hostapd_logger_register_cb(hostapd_logger_cb_func func)
{
	hostapd_logger_cb = func;
}


void hostapd_logger(void *ctx, const u8 *addr, unsigned int module, int level,
		    const char *fmt, ...)
{
	va_list ap;
	char *buf;
	const int buflen = 2048;
	int len;

	buf = os_malloc(buflen);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR, "hostapd_logger: Failed to allocate "
			   "message buffer");
		return;
	}
	va_start(ap, fmt);
	len = vsnprintf(buf, buflen, fmt, ap);
	va_end(ap);
	if (hostapd_logger_cb)
		hostapd_logger_cb(ctx, addr, module, level, buf, len);
	else if (addr)
		wpa_printf(MSG_DEBUG, "hostapd_logger: STA " MACSTR " - %s",
			   MAC2STR(addr), buf);
	else
		wpa_printf(MSG_DEBUG, "hostapd_logger: %s", buf);
	os_free(buf);
}
#endif /* CONFIG_NO_HOSTAPD_LOGGER */
