/*
 * Copyright 1999-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * Why BIO_s_log?
 *
 * BIO_s_log is useful for system daemons (or services under NT). It is
 * one-way BIO, it sends all stuff to syslogd (on system that commonly use
 * that), or event log (on NT), or OPCOM (on OpenVMS).
 *
 */

#include <stdio.h>
#include <errno.h>

#include "bio_lcl.h"
#include "internal/cryptlib.h"

#if defined(OPENSSL_SYS_WINCE)
#elif defined(OPENSSL_SYS_WIN32)
#elif defined(OPENSSL_SYS_VMS)
# include <opcdef.h>
# include <descrip.h>
# include <lib$routines.h>
# include <starlet.h>
/* Some compiler options may mask the declaration of "_malloc32". */
# if __INITIAL_POINTER_SIZE && defined _ANSI_C_SOURCE
#  if __INITIAL_POINTER_SIZE == 64
#   pragma pointer_size save
#   pragma pointer_size 32
void *_malloc32(__size_t);
#   pragma pointer_size restore
#  endif                        /* __INITIAL_POINTER_SIZE == 64 */
# endif                         /* __INITIAL_POINTER_SIZE && defined
                                 * _ANSI_C_SOURCE */
#elif defined(__DJGPP__) && defined(OPENSSL_NO_SOCK)
# define NO_SYSLOG
#elif (!defined(MSDOS) || defined(WATT32)) && !defined(OPENSSL_SYS_VXWORKS) && !defined(NO_SYSLOG)
# include <syslog.h>
#endif

#include <openssl/buffer.h>
#include <openssl/err.h>

#ifndef NO_SYSLOG

# if defined(OPENSSL_SYS_WIN32)
#  define LOG_EMERG       0
#  define LOG_ALERT       1
#  define LOG_CRIT        2
#  define LOG_ERR         3
#  define LOG_WARNING     4
#  define LOG_NOTICE      5
#  define LOG_INFO        6
#  define LOG_DEBUG       7

#  define LOG_DAEMON      (3<<3)
# elif defined(OPENSSL_SYS_VMS)
/* On VMS, we don't really care about these, but we need them to compile */
#  define LOG_EMERG       0
#  define LOG_ALERT       1
#  define LOG_CRIT        2
#  define LOG_ERR         3
#  define LOG_WARNING     4
#  define LOG_NOTICE      5
#  define LOG_INFO        6
#  define LOG_DEBUG       7

#  define LOG_DAEMON      OPC$M_NM_NTWORK
# endif

static int slg_write(BIO *h, const char *buf, int num);
static int slg_puts(BIO *h, const char *str);
static long slg_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int slg_new(BIO *h);
static int slg_free(BIO *data);
static void xopenlog(BIO *bp, char *name, int level);
static void xsyslog(BIO *bp, int priority, const char *string);
static void xcloselog(BIO *bp);

static const BIO_METHOD methods_slg = {
    BIO_TYPE_MEM,
    "syslog",
    /* TODO: Convert to new style write function */
    bwrite_conv,
    slg_write,
    NULL,                      /* slg_write_old,    */
    NULL,                      /* slg_read,         */
    slg_puts,
    NULL,
    slg_ctrl,
    slg_new,
    slg_free,
    NULL,                      /* slg_callback_ctrl */
};

const BIO_METHOD *BIO_s_log(void)
{
    return &methods_slg;
}

static int slg_new(BIO *bi)
{
    bi->init = 1;
    bi->num = 0;
    bi->ptr = NULL;
    xopenlog(bi, "application", LOG_DAEMON);
    return 1;
}

static int slg_free(BIO *a)
{
    if (a == NULL)
        return 0;
    xcloselog(a);
    return 1;
}

static int slg_write(BIO *b, const char *in, int inl)
{
    int ret = inl;
    char *buf;
    char *pp;
    int priority, i;
    static const struct {
        int strl;
        char str[10];
        int log_level;
    } mapping[] = {
        {
            6, "PANIC ", LOG_EMERG
        },
        {
            6, "EMERG ", LOG_EMERG
        },
        {
            4, "EMR ", LOG_EMERG
        },
        {
            6, "ALERT ", LOG_ALERT
        },
        {
            4, "ALR ", LOG_ALERT
        },
        {
            5, "CRIT ", LOG_CRIT
        },
        {
            4, "CRI ", LOG_CRIT
        },
        {
            6, "ERROR ", LOG_ERR
        },
        {
            4, "ERR ", LOG_ERR
        },
        {
            8, "WARNING ", LOG_WARNING
        },
        {
            5, "WARN ", LOG_WARNING
        },
        {
            4, "WAR ", LOG_WARNING
        },
        {
            7, "NOTICE ", LOG_NOTICE
        },
        {
            5, "NOTE ", LOG_NOTICE
        },
        {
            4, "NOT ", LOG_NOTICE
        },
        {
            5, "INFO ", LOG_INFO
        },
        {
            4, "INF ", LOG_INFO
        },
        {
            6, "DEBUG ", LOG_DEBUG
        },
        {
            4, "DBG ", LOG_DEBUG
        },
        {
            0, "", LOG_ERR
        }
        /* The default */
    };

    if ((buf = OPENSSL_malloc(inl + 1)) == NULL) {
        BIOerr(BIO_F_SLG_WRITE, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    memcpy(buf, in, inl);
    buf[inl] = '\0';

    i = 0;
    while (strncmp(buf, mapping[i].str, mapping[i].strl) != 0)
        i++;
    priority = mapping[i].log_level;
    pp = buf + mapping[i].strl;

    xsyslog(b, priority, pp);

    OPENSSL_free(buf);
    return ret;
}

static long slg_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    switch (cmd) {
    case BIO_CTRL_SET:
        xcloselog(b);
        xopenlog(b, ptr, num);
        break;
    default:
        break;
    }
    return 0;
}

static int slg_puts(BIO *bp, const char *str)
{
    int n, ret;

    n = strlen(str);
    ret = slg_write(bp, str, n);
    return ret;
}

# if defined(OPENSSL_SYS_WIN32)

static void xopenlog(BIO *bp, char *name, int level)
{
    if (check_winnt())
        bp->ptr = RegisterEventSourceA(NULL, name);
    else
        bp->ptr = NULL;
}

static void xsyslog(BIO *bp, int priority, const char *string)
{
    LPCSTR lpszStrings[2];
    WORD evtype = EVENTLOG_ERROR_TYPE;
    char pidbuf[DECIMAL_SIZE(DWORD) + 4];

    if (bp->ptr == NULL)
        return;

    switch (priority) {
    case LOG_EMERG:
    case LOG_ALERT:
    case LOG_CRIT:
    case LOG_ERR:
        evtype = EVENTLOG_ERROR_TYPE;
        break;
    case LOG_WARNING:
        evtype = EVENTLOG_WARNING_TYPE;
        break;
    case LOG_NOTICE:
    case LOG_INFO:
    case LOG_DEBUG:
        evtype = EVENTLOG_INFORMATION_TYPE;
        break;
    default:
        /*
         * Should never happen, but set it
         * as error anyway.
         */
        evtype = EVENTLOG_ERROR_TYPE;
        break;
    }

    sprintf(pidbuf, "[%lu] ", GetCurrentProcessId());
    lpszStrings[0] = pidbuf;
    lpszStrings[1] = string;

    ReportEventA(bp->ptr, evtype, 0, 1024, NULL, 2, 0, lpszStrings, NULL);
}

static void xcloselog(BIO *bp)
{
    if (bp->ptr)
        DeregisterEventSource((HANDLE) (bp->ptr));
    bp->ptr = NULL;
}

# elif defined(OPENSSL_SYS_VMS)

static int VMS_OPC_target = LOG_DAEMON;

static void xopenlog(BIO *bp, char *name, int level)
{
    VMS_OPC_target = level;
}

static void xsyslog(BIO *bp, int priority, const char *string)
{
    struct dsc$descriptor_s opc_dsc;

/* Arrange 32-bit pointer to opcdef buffer and malloc(), if needed. */
#  if __INITIAL_POINTER_SIZE == 64
#   pragma pointer_size save
#   pragma pointer_size 32
#   define OPCDEF_TYPE __char_ptr32
#   define OPCDEF_MALLOC _malloc32
#  else                         /* __INITIAL_POINTER_SIZE == 64 */
#   define OPCDEF_TYPE char *
#   define OPCDEF_MALLOC OPENSSL_malloc
#  endif                        /* __INITIAL_POINTER_SIZE == 64 [else] */

    struct opcdef *opcdef_p;

#  if __INITIAL_POINTER_SIZE == 64
#   pragma pointer_size restore
#  endif                        /* __INITIAL_POINTER_SIZE == 64 */

    char buf[10240];
    unsigned int len;
    struct dsc$descriptor_s buf_dsc;
    $DESCRIPTOR(fao_cmd, "!AZ: !AZ");
    char *priority_tag;

    switch (priority) {
    case LOG_EMERG:
        priority_tag = "Emergency";
        break;
    case LOG_ALERT:
        priority_tag = "Alert";
        break;
    case LOG_CRIT:
        priority_tag = "Critical";
        break;
    case LOG_ERR:
        priority_tag = "Error";
        break;
    case LOG_WARNING:
        priority_tag = "Warning";
        break;
    case LOG_NOTICE:
        priority_tag = "Notice";
        break;
    case LOG_INFO:
        priority_tag = "Info";
        break;
    case LOG_DEBUG:
        priority_tag = "DEBUG";
        break;
    }

    buf_dsc.dsc$b_dtype = DSC$K_DTYPE_T;
    buf_dsc.dsc$b_class = DSC$K_CLASS_S;
    buf_dsc.dsc$a_pointer = buf;
    buf_dsc.dsc$w_length = sizeof(buf) - 1;

    lib$sys_fao(&fao_cmd, &len, &buf_dsc, priority_tag, string);

    /* We know there's an 8-byte header.  That's documented. */
    opcdef_p = OPCDEF_MALLOC(8 + len);
    opcdef_p->opc$b_ms_type = OPC$_RQ_RQST;
    memcpy(opcdef_p->opc$z_ms_target_classes, &VMS_OPC_target, 3);
    opcdef_p->opc$l_ms_rqstid = 0;
    memcpy(&opcdef_p->opc$l_ms_text, buf, len);

    opc_dsc.dsc$b_dtype = DSC$K_DTYPE_T;
    opc_dsc.dsc$b_class = DSC$K_CLASS_S;
    opc_dsc.dsc$a_pointer = (OPCDEF_TYPE) opcdef_p;
    opc_dsc.dsc$w_length = len + 8;

    sys$sndopr(opc_dsc, 0);

    OPENSSL_free(opcdef_p);
}

static void xcloselog(BIO *bp)
{
}

# else                          /* Unix/Watt32 */

static void xopenlog(BIO *bp, char *name, int level)
{
#  ifdef WATT32                 /* djgpp/DOS */
    openlog(name, LOG_PID | LOG_CONS | LOG_NDELAY, level);
#  else
    openlog(name, LOG_PID | LOG_CONS, level);
#  endif
}

static void xsyslog(BIO *bp, int priority, const char *string)
{
    syslog(priority, "%s", string);
}

static void xcloselog(BIO *bp)
{
    closelog();
}

# endif                         /* Unix */

#else                           /* NO_SYSLOG */
const BIO_METHOD *BIO_s_log(void)
{
    return NULL;
}
#endif                          /* NO_SYSLOG */
