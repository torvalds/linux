/* $OpenBSD: log.h,v 1.23 2018/07/27 12:03:17 markus Exp $ */

/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 */

#ifndef SSH_LOG_H
#define SSH_LOG_H

/* Supported syslog facilities and levels. */
typedef enum {
	SYSLOG_FACILITY_DAEMON,
	SYSLOG_FACILITY_USER,
	SYSLOG_FACILITY_AUTH,
#ifdef LOG_AUTHPRIV
	SYSLOG_FACILITY_AUTHPRIV,
#endif
	SYSLOG_FACILITY_LOCAL0,
	SYSLOG_FACILITY_LOCAL1,
	SYSLOG_FACILITY_LOCAL2,
	SYSLOG_FACILITY_LOCAL3,
	SYSLOG_FACILITY_LOCAL4,
	SYSLOG_FACILITY_LOCAL5,
	SYSLOG_FACILITY_LOCAL6,
	SYSLOG_FACILITY_LOCAL7,
	SYSLOG_FACILITY_NOT_SET = -1
}       SyslogFacility;

typedef enum {
	SYSLOG_LEVEL_QUIET,
	SYSLOG_LEVEL_FATAL,
	SYSLOG_LEVEL_ERROR,
	SYSLOG_LEVEL_INFO,
	SYSLOG_LEVEL_VERBOSE,
	SYSLOG_LEVEL_DEBUG1,
	SYSLOG_LEVEL_DEBUG2,
	SYSLOG_LEVEL_DEBUG3,
	SYSLOG_LEVEL_NOT_SET = -1
}       LogLevel;

typedef void (log_handler_fn)(LogLevel, const char *, void *);

void     log_init(char *, LogLevel, SyslogFacility, int);
LogLevel log_level_get(void);
int      log_change_level(LogLevel);
int      log_is_on_stderr(void);
void     log_redirect_stderr_to(const char *);

SyslogFacility	log_facility_number(char *);
const char * 	log_facility_name(SyslogFacility);
LogLevel	log_level_number(char *);
const char *	log_level_name(LogLevel);

void     fatal(const char *, ...) __attribute__((noreturn))
    __attribute__((format(printf, 1, 2)));
void     error(const char *, ...) __attribute__((format(printf, 1, 2)));
void     sigdie(const char *, ...)  __attribute__((noreturn))
    __attribute__((format(printf, 1, 2)));
void     logdie(const char *, ...) __attribute__((noreturn))
    __attribute__((format(printf, 1, 2)));
void     logit(const char *, ...) __attribute__((format(printf, 1, 2)));
void     verbose(const char *, ...) __attribute__((format(printf, 1, 2)));
void     debug(const char *, ...) __attribute__((format(printf, 1, 2)));
void     debug2(const char *, ...) __attribute__((format(printf, 1, 2)));
void     debug3(const char *, ...) __attribute__((format(printf, 1, 2)));


void	 set_log_handler(log_handler_fn *, void *);
void	 do_log2(LogLevel, const char *, ...)
    __attribute__((format(printf, 2, 3)));
void	 do_log(LogLevel, const char *, va_list);
void	 cleanup_exit(int) __attribute__((noreturn));
#endif
