/*
 * wpa_supplicant/hostapd / Debug prints
 * Copyright (c) 2002-2013, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"

#ifdef CONFIG_DEBUG_SYSLOG
#include <syslog.h>

int wpa_debug_syslog = 0;
#endif /* CONFIG_DEBUG_SYSLOG */

#ifdef CONFIG_DEBUG_LINUX_TRACING
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

static FILE *wpa_debug_tracing_file = NULL;

#define WPAS_TRACE_PFX "wpas <%d>: "
#endif /* CONFIG_DEBUG_LINUX_TRACING */


int wpa_debug_level = MSG_INFO;
int wpa_debug_show_keys = 0;
int wpa_debug_timestamp = 0;


#ifdef CONFIG_ANDROID_LOG

#include <android/log.h>

#ifndef ANDROID_LOG_NAME
#define ANDROID_LOG_NAME	"wpa_supplicant"
#endif /* ANDROID_LOG_NAME */

static int wpa_to_android_level(int level)
{
	if (level == MSG_ERROR)
		return ANDROID_LOG_ERROR;
	if (level == MSG_WARNING)
		return ANDROID_LOG_WARN;
	if (level == MSG_INFO)
		return ANDROID_LOG_INFO;
	return ANDROID_LOG_DEBUG;
}

#endif /* CONFIG_ANDROID_LOG */

#ifndef CONFIG_NO_STDOUT_DEBUG

#ifdef CONFIG_DEBUG_FILE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static FILE *out_file = NULL;
#endif /* CONFIG_DEBUG_FILE */


void wpa_debug_print_timestamp(void)
{
#ifndef CONFIG_ANDROID_LOG
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
#endif /* CONFIG_ANDROID_LOG */
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


#ifdef CONFIG_DEBUG_LINUX_TRACING

int wpa_debug_open_linux_tracing(void)
{
	int mounts, trace_fd;
	char buf[4096] = {};
	ssize_t buflen;
	char *line, *tmp1, *path = NULL;

	mounts = open("/proc/mounts", O_RDONLY);
	if (mounts < 0) {
		printf("no /proc/mounts\n");
		return -1;
	}

	buflen = read(mounts, buf, sizeof(buf) - 1);
	close(mounts);
	if (buflen < 0) {
		printf("failed to read /proc/mounts\n");
		return -1;
	}

	line = strtok_r(buf, "\n", &tmp1);
	while (line) {
		char *tmp2, *tmp_path, *fstype;
		/* "<dev> <mountpoint> <fs type> ..." */
		strtok_r(line, " ", &tmp2);
		tmp_path = strtok_r(NULL, " ", &tmp2);
		fstype = strtok_r(NULL, " ", &tmp2);
		if (fstype && strcmp(fstype, "debugfs") == 0) {
			path = tmp_path;
			break;
		}

		line = strtok_r(NULL, "\n", &tmp1);
	}

	if (path == NULL) {
		printf("debugfs mountpoint not found\n");
		return -1;
	}

	snprintf(buf, sizeof(buf) - 1, "%s/tracing/trace_marker", path);

	trace_fd = open(buf, O_WRONLY);
	if (trace_fd < 0) {
		printf("failed to open trace_marker file\n");
		return -1;
	}
	wpa_debug_tracing_file = fdopen(trace_fd, "w");
	if (wpa_debug_tracing_file == NULL) {
		close(trace_fd);
		printf("failed to fdopen()\n");
		return -1;
	}

	return 0;
}


void wpa_debug_close_linux_tracing(void)
{
	if (wpa_debug_tracing_file == NULL)
		return;
	fclose(wpa_debug_tracing_file);
	wpa_debug_tracing_file = NULL;
}

#endif /* CONFIG_DEBUG_LINUX_TRACING */


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
#ifdef CONFIG_ANDROID_LOG
		__android_log_vprint(wpa_to_android_level(level),
				     ANDROID_LOG_NAME, fmt, ap);
#else /* CONFIG_ANDROID_LOG */
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
#endif /* CONFIG_ANDROID_LOG */
	}
	va_end(ap);

#ifdef CONFIG_DEBUG_LINUX_TRACING
	if (wpa_debug_tracing_file != NULL) {
		va_start(ap, fmt);
		fprintf(wpa_debug_tracing_file, WPAS_TRACE_PFX, level);
		vfprintf(wpa_debug_tracing_file, fmt, ap);
		fprintf(wpa_debug_tracing_file, "\n");
		fflush(wpa_debug_tracing_file);
		va_end(ap);
	}
#endif /* CONFIG_DEBUG_LINUX_TRACING */
}


static void _wpa_hexdump(int level, const char *title, const u8 *buf,
			 size_t len, int show)
{
	size_t i;

#ifdef CONFIG_DEBUG_LINUX_TRACING
	if (wpa_debug_tracing_file != NULL) {
		fprintf(wpa_debug_tracing_file,
			WPAS_TRACE_PFX "%s - hexdump(len=%lu):",
			level, title, (unsigned long) len);
		if (buf == NULL) {
			fprintf(wpa_debug_tracing_file, " [NULL]\n");
		} else if (!show) {
			fprintf(wpa_debug_tracing_file, " [REMOVED]\n");
		} else {
			for (i = 0; i < len; i++)
				fprintf(wpa_debug_tracing_file,
					" %02x", buf[i]);
		}
		fflush(wpa_debug_tracing_file);
	}
#endif /* CONFIG_DEBUG_LINUX_TRACING */

	if (level < wpa_debug_level)
		return;
#ifdef CONFIG_ANDROID_LOG
	{
		const char *display;
		char *strbuf = NULL;
		size_t slen = len;
		if (buf == NULL) {
			display = " [NULL]";
		} else if (len == 0) {
			display = "";
		} else if (show && len) {
			/* Limit debug message length for Android log */
			if (slen > 32)
				slen = 32;
			strbuf = os_malloc(1 + 3 * slen);
			if (strbuf == NULL) {
				wpa_printf(MSG_ERROR, "wpa_hexdump: Failed to "
					   "allocate message buffer");
				return;
			}

			for (i = 0; i < slen; i++)
				os_snprintf(&strbuf[i * 3], 4, " %02x",
					    buf[i]);

			display = strbuf;
		} else {
			display = " [REMOVED]";
		}

		__android_log_print(wpa_to_android_level(level),
				    ANDROID_LOG_NAME,
				    "%s - hexdump(len=%lu):%s%s",
				    title, (long unsigned int) len, display,
				    len > slen ? " ..." : "");
		bin_clear_free(strbuf, 1 + 3 * slen);
		return;
	}
#else /* CONFIG_ANDROID_LOG */
#ifdef CONFIG_DEBUG_SYSLOG
	if (wpa_debug_syslog) {
		const char *display;
		char *strbuf = NULL;

		if (buf == NULL) {
			display = " [NULL]";
		} else if (len == 0) {
			display = "";
		} else if (show && len) {
			strbuf = os_malloc(1 + 3 * len);
			if (strbuf == NULL) {
				wpa_printf(MSG_ERROR, "wpa_hexdump: Failed to "
					   "allocate message buffer");
				return;
			}

			for (i = 0; i < len; i++)
				os_snprintf(&strbuf[i * 3], 4, " %02x",
					    buf[i]);

			display = strbuf;
		} else {
			display = " [REMOVED]";
		}

		syslog(syslog_priority(level), "%s - hexdump(len=%lu):%s",
		       title, (unsigned long) len, display);
		bin_clear_free(strbuf, 1 + 3 * len);
		return;
	}
#endif /* CONFIG_DEBUG_SYSLOG */
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
#endif /* CONFIG_ANDROID_LOG */
}

void wpa_hexdump(int level, const char *title, const void *buf, size_t len)
{
	_wpa_hexdump(level, title, buf, len, 1);
}


void wpa_hexdump_key(int level, const char *title, const void *buf, size_t len)
{
	_wpa_hexdump(level, title, buf, len, wpa_debug_show_keys);
}


static void _wpa_hexdump_ascii(int level, const char *title, const void *buf,
			       size_t len, int show)
{
	size_t i, llen;
	const u8 *pos = buf;
	const size_t line_len = 16;

#ifdef CONFIG_DEBUG_LINUX_TRACING
	if (wpa_debug_tracing_file != NULL) {
		fprintf(wpa_debug_tracing_file,
			WPAS_TRACE_PFX "%s - hexdump_ascii(len=%lu):",
			level, title, (unsigned long) len);
		if (buf == NULL) {
			fprintf(wpa_debug_tracing_file, " [NULL]\n");
		} else if (!show) {
			fprintf(wpa_debug_tracing_file, " [REMOVED]\n");
		} else {
			/* can do ascii processing in userspace */
			for (i = 0; i < len; i++)
				fprintf(wpa_debug_tracing_file,
					" %02x", pos[i]);
		}
		fflush(wpa_debug_tracing_file);
	}
#endif /* CONFIG_DEBUG_LINUX_TRACING */

	if (level < wpa_debug_level)
		return;
#ifdef CONFIG_ANDROID_LOG
	_wpa_hexdump(level, title, buf, len, show);
#else /* CONFIG_ANDROID_LOG */
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
#endif /* CONFIG_ANDROID_LOG */
}


void wpa_hexdump_ascii(int level, const char *title, const void *buf,
		       size_t len)
{
	_wpa_hexdump_ascii(level, title, buf, len, 1);
}


void wpa_hexdump_ascii_key(int level, const char *title, const void *buf,
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
	char *tmp;

	if (!last_path)
		return 0; /* logfile not used */

	tmp = os_strdup(last_path);
	if (!tmp)
		return -1;

	wpa_debug_close_file();
	rv = wpa_debug_open_file(tmp);
	os_free(tmp);
	return rv;
#else /* CONFIG_DEBUG_FILE */
	return 0;
#endif /* CONFIG_DEBUG_FILE */
}


int wpa_debug_open_file(const char *path)
{
#ifdef CONFIG_DEBUG_FILE
	int out_fd;

	if (!path)
		return 0;

	if (last_path == NULL || os_strcmp(last_path, path) != 0) {
		/* Save our path to enable re-open */
		os_free(last_path);
		last_path = os_strdup(path);
	}

	out_fd = open(path, O_CREAT | O_APPEND | O_WRONLY,
		      S_IRUSR | S_IWUSR | S_IRGRP);
	if (out_fd < 0) {
		wpa_printf(MSG_ERROR,
			   "%s: Failed to open output file descriptor, using standard output",
			   __func__);
		return -1;
	}

#ifdef __linux__
	if (fcntl(out_fd, F_SETFD, FD_CLOEXEC) < 0) {
		wpa_printf(MSG_DEBUG,
			   "%s: Failed to set FD_CLOEXEC - continue without: %s",
			   __func__, strerror(errno));
	}
#endif /* __linux__ */

	out_file = fdopen(out_fd, "a");
	if (out_file == NULL) {
		wpa_printf(MSG_ERROR, "wpa_debug_open_file: Failed to open "
			   "output file, using standard output");
		close(out_fd);
		return -1;
	}
#ifndef _WIN32
	setvbuf(out_file, NULL, _IOLBF, 0);
#endif /* _WIN32 */
#else /* CONFIG_DEBUG_FILE */
	(void)path;
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


void wpa_debug_setup_stdout(void)
{
#ifndef _WIN32
	setvbuf(stdout, NULL, _IOLBF, 0);
#endif /* _WIN32 */
}

#endif /* CONFIG_NO_STDOUT_DEBUG */


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
	int buflen;
	int len;
	char prefix[130];

	va_start(ap, fmt);
	buflen = vsnprintf(NULL, 0, fmt, ap) + 1;
	va_end(ap);

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
			if (os_snprintf_error(sizeof(prefix), res))
				prefix[0] = '\0';
		}
	}
	len = vsnprintf(buf, buflen, fmt, ap);
	va_end(ap);
	wpa_printf(level, "%s%s", prefix, buf);
	if (wpa_msg_cb)
		wpa_msg_cb(ctx, level, WPA_MSG_PER_INTERFACE, buf, len);
	bin_clear_free(buf, buflen);
}


void wpa_msg_ctrl(void *ctx, int level, const char *fmt, ...)
{
	va_list ap;
	char *buf;
	int buflen;
	int len;

	if (!wpa_msg_cb)
		return;

	va_start(ap, fmt);
	buflen = vsnprintf(NULL, 0, fmt, ap) + 1;
	va_end(ap);

	buf = os_malloc(buflen);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR, "wpa_msg_ctrl: Failed to allocate "
			   "message buffer");
		return;
	}
	va_start(ap, fmt);
	len = vsnprintf(buf, buflen, fmt, ap);
	va_end(ap);
	wpa_msg_cb(ctx, level, WPA_MSG_PER_INTERFACE, buf, len);
	bin_clear_free(buf, buflen);
}


void wpa_msg_global(void *ctx, int level, const char *fmt, ...)
{
	va_list ap;
	char *buf;
	int buflen;
	int len;

	va_start(ap, fmt);
	buflen = vsnprintf(NULL, 0, fmt, ap) + 1;
	va_end(ap);

	buf = os_malloc(buflen);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR, "wpa_msg_global: Failed to allocate "
			   "message buffer");
		return;
	}
	va_start(ap, fmt);
	len = vsnprintf(buf, buflen, fmt, ap);
	va_end(ap);
	wpa_printf(level, "%s", buf);
	if (wpa_msg_cb)
		wpa_msg_cb(ctx, level, WPA_MSG_GLOBAL, buf, len);
	bin_clear_free(buf, buflen);
}


void wpa_msg_global_ctrl(void *ctx, int level, const char *fmt, ...)
{
	va_list ap;
	char *buf;
	int buflen;
	int len;

	if (!wpa_msg_cb)
		return;

	va_start(ap, fmt);
	buflen = vsnprintf(NULL, 0, fmt, ap) + 1;
	va_end(ap);

	buf = os_malloc(buflen);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR,
			   "wpa_msg_global_ctrl: Failed to allocate message buffer");
		return;
	}
	va_start(ap, fmt);
	len = vsnprintf(buf, buflen, fmt, ap);
	va_end(ap);
	wpa_msg_cb(ctx, level, WPA_MSG_GLOBAL, buf, len);
	bin_clear_free(buf, buflen);
}


void wpa_msg_no_global(void *ctx, int level, const char *fmt, ...)
{
	va_list ap;
	char *buf;
	int buflen;
	int len;

	va_start(ap, fmt);
	buflen = vsnprintf(NULL, 0, fmt, ap) + 1;
	va_end(ap);

	buf = os_malloc(buflen);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR, "wpa_msg_no_global: Failed to allocate "
			   "message buffer");
		return;
	}
	va_start(ap, fmt);
	len = vsnprintf(buf, buflen, fmt, ap);
	va_end(ap);
	wpa_printf(level, "%s", buf);
	if (wpa_msg_cb)
		wpa_msg_cb(ctx, level, WPA_MSG_NO_GLOBAL, buf, len);
	bin_clear_free(buf, buflen);
}


void wpa_msg_global_only(void *ctx, int level, const char *fmt, ...)
{
	va_list ap;
	char *buf;
	int buflen;
	int len;

	va_start(ap, fmt);
	buflen = vsnprintf(NULL, 0, fmt, ap) + 1;
	va_end(ap);

	buf = os_malloc(buflen);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR, "%s: Failed to allocate message buffer",
			   __func__);
		return;
	}
	va_start(ap, fmt);
	len = vsnprintf(buf, buflen, fmt, ap);
	va_end(ap);
	wpa_printf(level, "%s", buf);
	if (wpa_msg_cb)
		wpa_msg_cb(ctx, level, WPA_MSG_ONLY_GLOBAL, buf, len);
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
	int buflen;
	int len;

	va_start(ap, fmt);
	buflen = vsnprintf(NULL, 0, fmt, ap) + 1;
	va_end(ap);

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
	bin_clear_free(buf, buflen);
}
#endif /* CONFIG_NO_HOSTAPD_LOGGER */


const char * debug_level_str(int level)
{
	switch (level) {
	case MSG_EXCESSIVE:
		return "EXCESSIVE";
	case MSG_MSGDUMP:
		return "MSGDUMP";
	case MSG_DEBUG:
		return "DEBUG";
	case MSG_INFO:
		return "INFO";
	case MSG_WARNING:
		return "WARNING";
	case MSG_ERROR:
		return "ERROR";
	default:
		return "?";
	}
}


int str_to_debug_level(const char *s)
{
	if (os_strcasecmp(s, "EXCESSIVE") == 0)
		return MSG_EXCESSIVE;
	if (os_strcasecmp(s, "MSGDUMP") == 0)
		return MSG_MSGDUMP;
	if (os_strcasecmp(s, "DEBUG") == 0)
		return MSG_DEBUG;
	if (os_strcasecmp(s, "INFO") == 0)
		return MSG_INFO;
	if (os_strcasecmp(s, "WARNING") == 0)
		return MSG_WARNING;
	if (os_strcasecmp(s, "ERROR") == 0)
		return MSG_ERROR;
	return -1;
}
