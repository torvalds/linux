/*
 * Copyright (c) 2015, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, June 2015
 */

/*
 * Portions of this file are:
 *   Copyright (c) 1983, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#include "xo_config.h"
#include "xo.h"
#include "xo_encoder.h"		/* For xo_realloc */
#include "xo_buf.h"

/*
 * SYSLOG (RFC 5424) requires an enterprise identifier.  This turns
 * out to be a fickle little issue.  For a single-vendor box, the
 * system should have a single EID that all software can use.  When
 * VendorX turns FreeBSD into a product, all software (kernel and
 * utilities) should report VendorX's EID.  But when software is
 * installed on top of an external operating system, the application
 * should report it's own EID, distinct from the base OS.
 *
 * To make this happen, the kernel should support a sysctl to assign a
 * custom enterprise-id ("kern.syslog.enterprise_id").  libxo then
 * allows an application to set a custom EID to override that system
 * wide value, if needed.
 *
 * We try to set the stock IANA assigned Enterprise ID value for the
 * vendors we know about (FreeBSD, macosx), but fallback to the
 * "example" EID defined by IANA.  See:
 * https://www.iana.org/assignments/enterprise-numbers/enterprise-numbers
 */

#define XO_SYSLOG_ENTERPRISE_ID	"kern.syslog.enterprise_id"

#if defined(__FreeBSD__)
#define XO_DEFAULT_EID	2238
#elif defined(__macosx__)
#define XO_DEFAULT_EID	63
#else
#define XO_DEFAULT_EID	32473	/* Fallback to the "example" number */
#endif

#ifndef HOST_NAME_MAX
#ifdef _SC_HOST_NAME_MAX
#define HOST_NAME_MAX _SC_HOST_NAME_MAX
#else
#define HOST_NAME_MAX 255
#endif /* _SC_HOST_NAME_MAX */
#endif /* HOST_NAME_MAX */

#ifndef UNUSED
#define UNUSED __attribute__ ((__unused__))
#endif /* UNUSED */

static int xo_logfile = -1;		/* fd for log */
static int xo_status;			/* connection xo_status */
static int xo_opened;			/* have done openlog() */
static int xo_logstat = 0;		/* xo_status bits, set by openlog() */
static const char *xo_logtag = NULL;	/* string to tag the entry with */
static int xo_logfacility = LOG_USER;	/* default facility code */
static int xo_logmask = 0xff;		/* mask of priorities to be logged */
static pthread_mutex_t xo_syslog_mutex UNUSED = PTHREAD_MUTEX_INITIALIZER;
static int xo_unit_test;		/* Fake data for unit test */

#define REAL_VOID(_x) \
    do { int really_ignored = _x; if (really_ignored) { }} while (0)

#if !defined(HAVE_DECL___ISTHREADED) || !HAVE_DECL___ISTHREADED
#define __isthreaded 1
#endif

#define    THREAD_LOCK()						\
    do {								\
        if (__isthreaded) pthread_mutex_lock(&xo_syslog_mutex);		\
    } while(0)
#define    THREAD_UNLOCK()						\
    do {								\
        if (__isthreaded) pthread_mutex_unlock(&xo_syslog_mutex);       \
    } while(0)

static void xo_disconnect_log(void); /* disconnect from syslogd */
static void xo_connect_log(void);    /* (re)connect to syslogd */
static void xo_open_log_unlocked(const char *, int, int);

enum {
    NOCONN = 0,
    CONNDEF,
    CONNPRIV,
};

static xo_syslog_open_t xo_syslog_open;
static xo_syslog_send_t xo_syslog_send;
static xo_syslog_close_t xo_syslog_close;

static char xo_syslog_enterprise_id[12];

/*
 * Record an enterprise ID, which functions as a namespace for syslog
 * messages.  The value is pre-formatted into a string.  This allows
 * applications to customize their syslog message set, when needed. 
 */
void
xo_set_syslog_enterprise_id (unsigned short eid)
{
    snprintf(xo_syslog_enterprise_id, sizeof(xo_syslog_enterprise_id),
	     "%u", eid);
}

/*
 * Handle the work of transmitting the syslog message
 */
static void
xo_send_syslog (char *full_msg, char *v0_hdr,
		char *text_only)
{
    if (xo_syslog_send) {
	xo_syslog_send(full_msg, v0_hdr, text_only);
	return;
    }

    int fd;
    int full_len = strlen(full_msg);

    /* Output to stderr if requested, then we've been passed a v0 header */
    if (v0_hdr) {
        struct iovec iov[3];
        struct iovec *v = iov;
        char newline[] = "\n";

        v->iov_base = v0_hdr;
        v->iov_len = strlen(v0_hdr);
        v += 1;
        v->iov_base = text_only;
        v->iov_len = strlen(text_only);
        v += 1;
        v->iov_base = newline;
        v->iov_len = 1;
        v += 1;
        REAL_VOID(writev(STDERR_FILENO, iov, 3));
    }

    /* Get connected, output the message to the local logger. */
    if (!xo_opened)
        xo_open_log_unlocked(xo_logtag, xo_logstat | LOG_NDELAY, 0);
    xo_connect_log();

    /*
     * If the send() fails, there are two likely scenarios: 
     *  1) syslogd was restarted
     *  2) /var/run/log is out of socket buffer space, which
     *     in most cases means local DoS.
     * If the error does not indicate a full buffer, we address
     * case #1 by attempting to reconnect to /var/run/log[priv]
     * and resending the message once.
     *
     * If we are working with a privileged socket, the retry
     * attempts end there, because we don't want to freeze a
     * critical application like su(1) or sshd(8).
     *
     * Otherwise, we address case #2 by repeatedly retrying the
     * send() to give syslogd a chance to empty its socket buffer.
     */

    if (send(xo_logfile, full_msg, full_len, 0) < 0) {
        if (errno != ENOBUFS) {
            /*
             * Scenario 1: syslogd was restarted
             * reconnect and resend once
             */
            xo_disconnect_log();
            xo_connect_log();
            if (send(xo_logfile, full_msg, full_len, 0) >= 0) {
                return;
            }
            /*
             * if the resend failed, fall through to
             * possible scenario 2
             */
        }
        while (errno == ENOBUFS) {
            /*
             * Scenario 2: out of socket buffer space
             * possible DoS, fail fast on a privileged
             * socket
             */
            if (xo_status == CONNPRIV)
                break;
            usleep(1);
            if (send(xo_logfile, full_msg, full_len, 0) >= 0) {
                return;
            }
        }
    } else {
        return;
    }

    /*
     * Output the message to the console; try not to block
     * as a blocking console should not stop other processes.
     * Make sure the error reported is the one from the syslogd failure.
     */
    int flags = O_WRONLY | O_NONBLOCK;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif /* O_CLOEXEC */

    if (xo_logstat & LOG_CONS
        && (fd = open(_PATH_CONSOLE, flags, 0)) >= 0) {
        struct iovec iov[2];
        struct iovec *v = iov;
        char crnl[] = "\r\n";
	char *p;

        p = strchr(full_msg, '>') + 1;
        v->iov_base = p;
        v->iov_len = full_len - (p - full_msg);
        ++v;
        v->iov_base = crnl;
        v->iov_len = 2;
        REAL_VOID(writev(fd, iov, 2));
        (void) close(fd);
    }
}

/* Should be called with mutex acquired */
static void
xo_disconnect_log (void)
{
    if (xo_syslog_close) {
	xo_syslog_close();
	return;
    }

    /*
     * If the user closed the FD and opened another in the same slot,
     * that's their problem.  They should close it before calling on
     * system services.
     */
    if (xo_logfile != -1) {
        close(xo_logfile);
        xo_logfile = -1;
    }
    xo_status = NOCONN;            /* retry connect */
}

/* Should be called with mutex acquired */
static void
xo_connect_log (void)
{
    if (xo_syslog_open) {
	xo_syslog_open();
	return;
    }

    struct sockaddr_un saddr;    /* AF_UNIX address of local logger */

    if (xo_logfile == -1) {
        int flags = SOCK_DGRAM;
#ifdef SOCK_CLOEXEC
        flags |= SOCK_CLOEXEC;
#endif /* SOCK_CLOEXEC */
        if ((xo_logfile = socket(AF_UNIX, flags, 0)) == -1)
            return;
    }
    if (xo_logfile != -1 && xo_status == NOCONN) {
#ifdef HAVE_SUN_LEN
        saddr.sun_len = sizeof(saddr);
#endif /* HAVE_SUN_LEN */
        saddr.sun_family = AF_UNIX;

        /*
         * First try privileged socket. If no success,
         * then try default socket.
         */

#ifdef _PATH_LOG_PRIV
        (void) strncpy(saddr.sun_path, _PATH_LOG_PRIV,
            sizeof saddr.sun_path);
        if (connect(xo_logfile, (struct sockaddr *) &saddr,
            sizeof(saddr)) != -1)
            xo_status = CONNPRIV;
#endif /* _PATH_LOG_PRIV */

#ifdef _PATH_LOG
        if (xo_status == NOCONN) {
            (void) strncpy(saddr.sun_path, _PATH_LOG,
                sizeof saddr.sun_path);
            if (connect(xo_logfile, (struct sockaddr *)&saddr,
                sizeof(saddr)) != -1)
                xo_status = CONNDEF;
        }
#endif /* _PATH_LOG */

#ifdef _PATH_OLDLOG
        if (xo_status == NOCONN) {
            /*
             * Try the old "/dev/log" path, for backward
             * compatibility.
             */
            (void) strncpy(saddr.sun_path, _PATH_OLDLOG,
                sizeof saddr.sun_path);
            if (connect(xo_logfile, (struct sockaddr *)&saddr,
                sizeof(saddr)) != -1)
                xo_status = CONNDEF;
        }
#endif /* _PATH_OLDLOG */

        if (xo_status == NOCONN) {
            (void) close(xo_logfile);
            xo_logfile = -1;
        }
    }
}

static void
xo_open_log_unlocked (const char *ident, int logstat, int logfac)
{
    if (ident != NULL)
        xo_logtag = ident;
    xo_logstat = logstat;
    if (logfac != 0 && (logfac &~ LOG_FACMASK) == 0)
        xo_logfacility = logfac;

    if (xo_logstat & LOG_NDELAY)    /* open immediately */
        xo_connect_log();

    xo_opened = 1;    /* ident and facility has been set */
}

void
xo_open_log (const char *ident, int logstat, int logfac)
{
    THREAD_LOCK();
    xo_open_log_unlocked(ident, logstat, logfac);
    THREAD_UNLOCK();
}


void
xo_close_log (void) 
{
    THREAD_LOCK();
    if (xo_logfile != -1) {
        (void) close(xo_logfile);
        xo_logfile = -1;
    }
    xo_logtag = NULL;
    xo_status = NOCONN;
    THREAD_UNLOCK();
}

/* xo_set_logmask -- set the log mask level */
int
xo_set_logmask (int pmask)
{
    int omask;

    THREAD_LOCK();
    omask = xo_logmask;
    if (pmask != 0)
        xo_logmask = pmask;
    THREAD_UNLOCK();
    return (omask);
}

void
xo_set_syslog_handler (xo_syslog_open_t open_func,
		       xo_syslog_send_t send_func,
		       xo_syslog_close_t close_func)
{
    xo_syslog_open = open_func;
    xo_syslog_send = send_func;
    xo_syslog_close = close_func;
}

static ssize_t
xo_snprintf (char *out, ssize_t outsize, const char *fmt, ...)
{
    ssize_t status;
    ssize_t retval = 0;
    va_list ap;

    if (out && outsize) {
        va_start(ap, fmt);
        status = vsnprintf(out, outsize, fmt, ap);
        if (status < 0) { /* this should never happen, */
            *out = 0;     /* handle it in the safest way possible if it does */
            retval = 0;
        } else {
            retval = status;
            retval = retval > outsize ? outsize : retval;
        }
        va_end(ap);
    }

    return retval;
}

static xo_ssize_t
xo_syslog_handle_write (void *opaque, const char *data)
{
    xo_buffer_t *xbp = opaque;
    int len = strlen(data);
    int left = xo_buf_left(xbp);

    if (len > left - 1)
	len = left - 1;

    memcpy(xbp->xb_curp, data, len);
    xbp->xb_curp += len;
    *xbp->xb_curp = '\0';

    return len;
}

static void
xo_syslog_handle_close (void *opaque UNUSED)
{
}

static int
xo_syslog_handle_flush (void *opaque UNUSED)
{
    return 0;
}

void
xo_set_unit_test_mode (int value)
{
    xo_unit_test = value;
}

void
xo_vsyslog (int pri, const char *name, const char *fmt, va_list vap)
{
    int saved_errno = errno;
    char tbuf[2048];
    char *tp = NULL, *ep = NULL;
    unsigned start_of_msg = 0;
    char *v0_hdr = NULL;
    xo_buffer_t xb;
    static pid_t my_pid;
    unsigned log_offset;

    if (my_pid == 0)
	my_pid = xo_unit_test ? 222 : getpid();

    /* Check for invalid bits */
    if (pri & ~(LOG_PRIMASK|LOG_FACMASK)) {
        xo_syslog(LOG_ERR | LOG_CONS | LOG_PERROR | LOG_PID,
		  "syslog-unknown-priority",
		  "syslog: unknown facility/priority: %#x", pri);
        pri &= LOG_PRIMASK|LOG_FACMASK;
    }

    THREAD_LOCK();

    /* Check priority against setlogmask values. */
    if (!(LOG_MASK(LOG_PRI(pri)) & xo_logmask)) {
        THREAD_UNLOCK();
        return;
    }

    /* Set default facility if none specified. */
    if ((pri & LOG_FACMASK) == 0)
        pri |= xo_logfacility;

    /* Create the primary stdio hook */
    xb.xb_bufp = tbuf;
    xb.xb_curp = tbuf;
    xb.xb_size = sizeof(tbuf);

    xo_handle_t *xop = xo_create(XO_STYLE_SDPARAMS, 0);
    if (xop == NULL) {
        THREAD_UNLOCK();
	return;
    }

#ifdef HAVE_GETPROGNAME
    if (xo_logtag == NULL)
        xo_logtag = getprogname();
#endif /* HAVE_GETPROGNAME */

    xo_set_writer(xop, &xb, xo_syslog_handle_write, xo_syslog_handle_close,
		  xo_syslog_handle_flush);

    /* Build the message; start by getting the time */
    struct tm tm;
    struct timeval tv;

    /* Unit test hack: fake a fixed time */
    if (xo_unit_test) {
	tv.tv_sec = 1435085229;
	tv.tv_usec = 123456;
    } else
	gettimeofday(&tv, NULL);

    (void) localtime_r(&tv.tv_sec, &tm);

    if (xo_logstat & LOG_PERROR) {
	/*
	 * For backwards compatibility, we need to make the old-style
	 * message.  This message can be emitted to the console/tty.
	 */
	v0_hdr = alloca(2048);
	tp = v0_hdr;
	ep = v0_hdr + 2048;

	if (xo_logtag != NULL)
	    tp += xo_snprintf(tp, ep - tp, "%s", xo_logtag);
	if (xo_logstat & LOG_PID)
	    tp += xo_snprintf(tp, ep - tp, "[%d]", my_pid);
	if (xo_logtag)
	    tp += xo_snprintf(tp, ep - tp, ": ");
    }

    log_offset = xb.xb_curp - xb.xb_bufp;

    /* Add PRI, PRIVAL, and VERSION */
    xb.xb_curp += xo_snprintf(xb.xb_curp, xo_buf_left(&xb), "<%d>1 ", pri);

    /* Add TIMESTAMP with milliseconds and TZOFFSET */
    xb.xb_curp += strftime(xb.xb_curp, xo_buf_left(&xb), "%FT%T", &tm);
    xb.xb_curp += xo_snprintf(xb.xb_curp, xo_buf_left(&xb),
			      ".%03.3u", tv.tv_usec / 1000);
    xb.xb_curp += strftime(xb.xb_curp, xo_buf_left(&xb), "%z ", &tm);

    /*
     * Add HOSTNAME; we rely on gethostname and don't fluff with
     * ip addresses.  Might need to revisit.....
     */
    char hostname[HOST_NAME_MAX];
    hostname[0] = '\0';
    if (xo_unit_test)
	strcpy(hostname, "worker-host");
    else
	(void) gethostname(hostname, sizeof(hostname));

    xb.xb_curp += xo_snprintf(xb.xb_curp, xo_buf_left(&xb), "%s ",
			      hostname[0] ? hostname : "-");

    /* Add APP-NAME */
    xb.xb_curp += xo_snprintf(xb.xb_curp, xo_buf_left(&xb), "%s ",
			      xo_logtag ?: "-");

    /* Add PROCID */
    xb.xb_curp += xo_snprintf(xb.xb_curp, xo_buf_left(&xb), "%d ", my_pid);

    /*
     * Add MSGID.  The user should provide us with a name, which we
     * prefix with the current enterprise ID, as learned from the kernel.
     * If the kernel won't tell us, we use the stock/builtin number.
     */
    char *buf UNUSED = NULL;
    const char *eid = xo_syslog_enterprise_id;
    const char *at_sign = "@";

    if (name == NULL) {
	name = "-";
	eid = at_sign = "";

    } else if (*name == '@') {
	/* Our convention is to prefix IANA-defined names with an "@" */
	name += 1;
	eid = at_sign = "";

    } else if (eid[0] == '\0') {
#ifdef HAVE_SYSCTLBYNAME
	/*
	 * See if the kernel knows the sysctl for the enterprise ID
	 */
	size_t size = 0;
	if (sysctlbyname(XO_SYSLOG_ENTERPRISE_ID, NULL, &size, NULL, 0) == 0
	    	&& size > 0) {
	    buf = alloca(size);
	    if (sysctlbyname(XO_SYSLOG_ENTERPRISE_ID, buf, &size, NULL, 0) == 0
			&& size > 0)
		eid = buf;
	}
#endif /* HAVE_SYSCTLBYNAME */

	if (eid[0] == '\0') {
	    /* Fallback to our base default */
	    xo_set_syslog_enterprise_id(XO_DEFAULT_EID);
	    eid = xo_syslog_enterprise_id;
	}
    }

    xb.xb_curp += xo_snprintf(xb.xb_curp, xo_buf_left(&xb), "%s [%s%s%s ",
			      name, name, at_sign, eid);

    /*
     * Now for the real content.  We make two distinct passes thru the
     * xo_emit engine, first for the SD-PARAMS and then for the text
     * message.
     */
    va_list ap;
    va_copy(ap, vap);

    errno = saved_errno;	/* Restore saved error value */
    xo_emit_hv(xop, fmt, ap);
    xo_flush_h(xop);

    va_end(ap);

    /* Trim trailing space */
    if (xb.xb_curp[-1] == ' ')
	xb.xb_curp -= 1;

    /* Close the structured data (SD-ELEMENT) */
    xb.xb_curp += xo_snprintf(xb.xb_curp, xo_buf_left(&xb), "] ");

    /*
     * Since our MSG is known to be UTF-8, we MUST prefix it with
     * that most-annoying-of-all-UTF-8 features, the BOM (0xEF.BB.BF).
     */
    xb.xb_curp += xo_snprintf(xb.xb_curp, xo_buf_left(&xb),
			      "%c%c%c", 0xEF, 0xBB, 0xBF);

    /* Save the start of the message */
    if (xo_logstat & LOG_PERROR)
	start_of_msg = xb.xb_curp - xb.xb_bufp;

    xo_set_style(xop, XO_STYLE_TEXT);
    xo_set_flags(xop, XOF_UTF8);

    errno = saved_errno;	/* Restore saved error value */
    xo_emit_hv(xop, fmt, ap);
    xo_flush_h(xop);

    /* Remove a trailing newline */
    if (xb.xb_curp[-1] == '\n')
        *--xb.xb_curp = '\0';

    if (xo_get_flags(xop) & XOF_LOG_SYSLOG)
	fprintf(stderr, "xo: syslog: %s\n", xb.xb_bufp + log_offset);

    xo_send_syslog(xb.xb_bufp, v0_hdr, xb.xb_bufp + start_of_msg);

    xo_destroy(xop);

    THREAD_UNLOCK();
}

/*
 * syslog - print message on log file; output is intended for syslogd(8).
 */
void
xo_syslog (int pri, const char *name, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    xo_vsyslog(pri, name, fmt, ap);
    va_end(ap);
}
