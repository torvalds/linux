/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <errno.h>

#include "bio_lcl.h"

#if defined(OPENSSL_NO_POSIX_IO)
/*
 * Dummy placeholder for BIO_s_fd...
 */
BIO *BIO_new_fd(int fd, int close_flag)
{
    return NULL;
}

int BIO_fd_non_fatal_error(int err)
{
    return 0;
}

int BIO_fd_should_retry(int i)
{
    return 0;
}

const BIO_METHOD *BIO_s_fd(void)
{
    return NULL;
}
#else
/*
 * As for unconditional usage of "UPLINK" interface in this module.
 * Trouble is that unlike Unix file descriptors [which are indexes
 * in kernel-side per-process table], corresponding descriptors on
 * platforms which require "UPLINK" interface seem to be indexes
 * in a user-land, non-global table. Well, in fact they are indexes
 * in stdio _iob[], and recall that _iob[] was the very reason why
 * "UPLINK" interface was introduced in first place. But one way on
 * another. Neither libcrypto or libssl use this BIO meaning that
 * file descriptors can only be provided by application. Therefore
 * "UPLINK" calls are due...
 */
static int fd_write(BIO *h, const char *buf, int num);
static int fd_read(BIO *h, char *buf, int size);
static int fd_puts(BIO *h, const char *str);
static int fd_gets(BIO *h, char *buf, int size);
static long fd_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int fd_new(BIO *h);
static int fd_free(BIO *data);
int BIO_fd_should_retry(int s);

static const BIO_METHOD methods_fdp = {
    BIO_TYPE_FD,
    "file descriptor",
    /* TODO: Convert to new style write function */
    bwrite_conv,
    fd_write,
    /* TODO: Convert to new style read function */
    bread_conv,
    fd_read,
    fd_puts,
    fd_gets,
    fd_ctrl,
    fd_new,
    fd_free,
    NULL,                       /* fd_callback_ctrl */
};

const BIO_METHOD *BIO_s_fd(void)
{
    return &methods_fdp;
}

BIO *BIO_new_fd(int fd, int close_flag)
{
    BIO *ret;
    ret = BIO_new(BIO_s_fd());
    if (ret == NULL)
        return NULL;
    BIO_set_fd(ret, fd, close_flag);
    return ret;
}

static int fd_new(BIO *bi)
{
    bi->init = 0;
    bi->num = -1;
    bi->ptr = NULL;
    bi->flags = BIO_FLAGS_UPLINK; /* essentially redundant */
    return 1;
}

static int fd_free(BIO *a)
{
    if (a == NULL)
        return 0;
    if (a->shutdown) {
        if (a->init) {
            UP_close(a->num);
        }
        a->init = 0;
        a->flags = BIO_FLAGS_UPLINK;
    }
    return 1;
}

static int fd_read(BIO *b, char *out, int outl)
{
    int ret = 0;

    if (out != NULL) {
        clear_sys_error();
        ret = UP_read(b->num, out, outl);
        BIO_clear_retry_flags(b);
        if (ret <= 0) {
            if (BIO_fd_should_retry(ret))
                BIO_set_retry_read(b);
        }
    }
    return ret;
}

static int fd_write(BIO *b, const char *in, int inl)
{
    int ret;
    clear_sys_error();
    ret = UP_write(b->num, in, inl);
    BIO_clear_retry_flags(b);
    if (ret <= 0) {
        if (BIO_fd_should_retry(ret))
            BIO_set_retry_write(b);
    }
    return ret;
}

static long fd_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    long ret = 1;
    int *ip;

    switch (cmd) {
    case BIO_CTRL_RESET:
        num = 0;
        /* fall thru */
    case BIO_C_FILE_SEEK:
        ret = (long)UP_lseek(b->num, num, 0);
        break;
    case BIO_C_FILE_TELL:
    case BIO_CTRL_INFO:
        ret = (long)UP_lseek(b->num, 0, 1);
        break;
    case BIO_C_SET_FD:
        fd_free(b);
        b->num = *((int *)ptr);
        b->shutdown = (int)num;
        b->init = 1;
        break;
    case BIO_C_GET_FD:
        if (b->init) {
            ip = (int *)ptr;
            if (ip != NULL)
                *ip = b->num;
            ret = b->num;
        } else
            ret = -1;
        break;
    case BIO_CTRL_GET_CLOSE:
        ret = b->shutdown;
        break;
    case BIO_CTRL_SET_CLOSE:
        b->shutdown = (int)num;
        break;
    case BIO_CTRL_PENDING:
    case BIO_CTRL_WPENDING:
        ret = 0;
        break;
    case BIO_CTRL_DUP:
    case BIO_CTRL_FLUSH:
        ret = 1;
        break;
    default:
        ret = 0;
        break;
    }
    return ret;
}

static int fd_puts(BIO *bp, const char *str)
{
    int n, ret;

    n = strlen(str);
    ret = fd_write(bp, str, n);
    return ret;
}

static int fd_gets(BIO *bp, char *buf, int size)
{
    int ret = 0;
    char *ptr = buf;
    char *end = buf + size - 1;

    while (ptr < end && fd_read(bp, ptr, 1) > 0) {
        if (*ptr++ == '\n')
           break;
    }

    ptr[0] = '\0';

    if (buf[0] != '\0')
        ret = strlen(buf);
    return ret;
}

int BIO_fd_should_retry(int i)
{
    int err;

    if ((i == 0) || (i == -1)) {
        err = get_last_sys_error();

        return BIO_fd_non_fatal_error(err);
    }
    return 0;
}

int BIO_fd_non_fatal_error(int err)
{
    switch (err) {

# ifdef EWOULDBLOCK
#  ifdef WSAEWOULDBLOCK
#   if WSAEWOULDBLOCK != EWOULDBLOCK
    case EWOULDBLOCK:
#   endif
#  else
    case EWOULDBLOCK:
#  endif
# endif

# if defined(ENOTCONN)
    case ENOTCONN:
# endif

# ifdef EINTR
    case EINTR:
# endif

# ifdef EAGAIN
#  if EWOULDBLOCK != EAGAIN
    case EAGAIN:
#  endif
# endif

# ifdef EPROTO
    case EPROTO:
# endif

# ifdef EINPROGRESS
    case EINPROGRESS:
# endif

# ifdef EALREADY
    case EALREADY:
# endif
        return 1;
    default:
        break;
    }
    return 0;
}
#endif
