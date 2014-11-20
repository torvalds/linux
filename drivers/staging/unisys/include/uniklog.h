/* uniklog.h
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/* This module contains macros to aid developers in logging messages.
 *
 * This module is affected by the DEBUG compiletime option.
 *
 */
#ifndef __UNIKLOG_H__
#define __UNIKLOG_H__

#include <linux/printk.h>

/*
 * # DBGINF
 *
 * \brief Log debug informational message - log a LOG_INFO message only
 *        if DEBUG compiletime option enabled
 *
 * \param devname the device name of the device reporting this message, or
 *                NULL if this message is NOT device-related.
 * \param fmt printf()-style format string containing the message to log.
 * \param args Optional arguments to be formatted and inserted into the
 *             format string.
 * \return nothing
 *
 * Log a message at the LOG_INFO level, but only if DEBUG is enabled.  If
 * DEBUG is disabled, this expands to a no-op.
 */

/*
 * # DBGVER
 *
 * \brief Log debug verbose message - log a LOG_DEBUG message only if
 *        DEBUG compiletime option enabled
 *
 * \param devname the device name of the device reporting this message, or
 *                NULL if this message is NOT device-related.
 * \param fmt printf()-style format string containing the message to log.
 * \param args Optional arguments to be formatted and inserted into the
 *             format string.
 * \return nothing
 *
 * Log a message at the LOG_DEBUG level, but only if DEBUG is enabled.  If
 * DEBUG is disabled, this expands to a no-op.  Note also that LOG_DEBUG
 * messages can be enabled/disabled at runtime as well.
 */
#define DBGINFDEV(devname, fmt, args...)        do { } while (0)
#define DBGVERDEV(devname, fmt, args...)        do { } while (0)
#define DBGINF(fmt, args...)                    do { } while (0)
#define DBGVER(fmt, args...)                    do { } while (0)

/*
 * # LOGINF
 *
 * \brief Log informational message - logs a message at the LOG_INFO level
 *
 * \param devname the device name of the device reporting this message, or
 *                NULL if this message is NOT device-related.
 * \param fmt printf()-style format string containing the message to log.
 * \param args Optional arguments to be formatted and inserted into the
 *             format string.
 * \return nothing
 *
 * Logs the specified message at the LOG_INFO level.
 */

#define LOGINF(fmt, args...) pr_info(fmt, ## args)
#define LOGINFDEV(devname, fmt, args...) \
	pr_info("%s " fmt, devname, ## args)
#define LOGINFDEVX(devno, fmt, args...) \
	pr_info("dev%d " fmt, devno, ## args)
#define LOGINFNAME(vnic, fmt, args...)				\
	do {								\
		if (vnic != NULL) {					\
			pr_info("%s " fmt, vnic->name, ## args);	\
		} else {						\
			pr_info(fmt, ## args);				\
		}							\
	} while (0)

/*
 * # LOGVER
 *
 * \brief Log verbose message - logs a message at the LOG_DEBUG level,
 *        which can be disabled at runtime
 *
 * \param devname the device name of the device reporting this message, or
 *                NULL if this message is NOT device-related.
 * \param fmt printf()-style format string containing the message to log.
 * \param args Optional arguments to be formatted and inserted into the format
 * \param string.
 * \return nothing
 *
 * Logs the specified message at the LOG_DEBUG level.  Note also that
 * LOG_DEBUG messages can be enabled/disabled at runtime as well.
 */
#define LOGVER(fmt, args...) pr_debug(fmt, ## args)
#define LOGVERDEV(devname, fmt, args...) \
	pr_debug("%s " fmt, devname, ## args)
#define LOGVERNAME(vnic, fmt, args...)					\
	do {								\
		if (vnic != NULL) {					\
			pr_debug("%s " fmt, vnic->name, ## args);	\
		} else {						\
			pr_debug(fmt, ## args);				\
		}							\
	} while (0)

/*
 * # LOGERR
 *
 * \brief Log error message - logs a message at the LOG_ERR level,
 *        including source line number information
 *
 * \param devname the device name of the device reporting this message, or
 *                NULL if this message is NOT device-related.
 * \param fmt printf()-style format string containing the message to log.
 * \param args Optional arguments to be formatted and inserted into the format
 * \param string.
 * \return nothing
 *
 * Logs the specified error message at the LOG_ERR level.  It will also
 * include the file, line number, and function name of where the error
 * originated in the log message.
 */
#define LOGERR(fmt, args...) pr_err(fmt, ## args)
#define LOGERRDEV(devname, fmt, args...) \
	pr_err("%s " fmt, devname, ## args)
#define LOGERRDEVX(devno, fmt, args...) \
	pr_err("dev%d " fmt, devno, ## args)
#define LOGERRNAME(vnic, fmt, args...)				\
	do {								\
		if (vnic != NULL) {					\
			pr_err("%s " fmt, vnic->name, ## args);	\
		} else {						\
			pr_err(fmt, ## args);				\
		}							\
	} while (0)
#define LOGORDUMPERR(seqfile, fmt, args...) do {		\
		if (seqfile) {					\
			seq_printf(seqfile, fmt, ## args);	\
		} else {					\
			LOGERR(fmt, ## args);			\
		}						\
	} while (0)

/*
 * # LOGWRN
 *
 * \brief Log warning message - Logs a message at the LOG_WARNING level,
 *        including source line number information
 *
 * \param devname the device name of the device reporting this message, or
 *                NULL if this message is NOT device-related.
 * \param fmt printf()-style format string containing the message to log.
 * \param args Optional arguments to be formatted and inserted into the format
 * \param string.
 * \return nothing
 *
 * Logs the specified error message at the LOG_WARNING level.  It will also
 * include the file, line number, and function name of where the error
 * originated in the log message.
 */
#define LOGWRN(fmt, args...) pr_warn(fmt, ## args)
#define LOGWRNDEV(devname, fmt, args...) \
	pr_warn("%s " fmt, devname, ## args)
#define LOGWRNNAME(vnic, fmt, args...) \
	do {								\
		if (vnic != NULL) {					\
			pr_warn("%s " fmt, vnic->name, ## args);	\
		} else {						\
			pr_warn(fmt, ## args);				\
		}							\
	} while (0)

#endif /* __UNIKLOG_H__ */
