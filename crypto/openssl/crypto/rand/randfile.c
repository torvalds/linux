/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/cryptlib.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/rand_drbg.h>
#include <openssl/buffer.h>

#ifdef OPENSSL_SYS_VMS
# include <unixio.h>
#endif
#include <sys/types.h>
#ifndef OPENSSL_NO_POSIX_IO
# include <sys/stat.h>
# include <fcntl.h>
# ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  define stat    _stat
#  define chmod   _chmod
#  define open    _open
#  define fdopen  _fdopen
#  define fstat   _fstat
#  define fileno  _fileno
# endif
#endif

/*
 * Following should not be needed, and we could have been stricter
 * and demand S_IS*. But some systems just don't comply... Formally
 * below macros are "anatomically incorrect", because normally they
 * would look like ((m) & MASK == TYPE), but since MASK availability
 * is as questionable, we settle for this poor-man fallback...
 */
# if !defined(S_ISREG)
#   define S_ISREG(m) ((m) & S_IFREG)
# endif

#define RAND_BUF_SIZE 1024
#define RFILE ".rnd"

#ifdef OPENSSL_SYS_VMS
/*
 * __FILE_ptr32 is a type provided by DEC C headers (types.h specifically)
 * to make sure the FILE* is a 32-bit pointer no matter what.  We know that
 * stdio functions return this type (a study of stdio.h proves it).
 *
 * This declaration is a nasty hack to get around vms' extension to fopen for
 * passing in sharing options being disabled by /STANDARD=ANSI89
 */
static __FILE_ptr32 (*const vms_fopen)(const char *, const char *, ...) =
        (__FILE_ptr32 (*)(const char *, const char *, ...))fopen;
# define VMS_OPEN_ATTRS \
        "shr=get,put,upd,del","ctx=bin,stm","rfm=stm","rat=none","mrs=0"
# define openssl_fopen(fname, mode) vms_fopen((fname), (mode), VMS_OPEN_ATTRS)
#endif

/*
 * Note that these functions are intended for seed files only. Entropy
 * devices and EGD sockets are handled in rand_unix.c  If |bytes| is
 * -1 read the complete file; otherwise read the specified amount.
 */
int RAND_load_file(const char *file, long bytes)
{
    /*
     * The load buffer size exceeds the chunk size by the comfortable amount
     * of 'RAND_DRBG_STRENGTH' bytes (not bits!). This is done on purpose
     * to avoid calling RAND_add() with a small final chunk. Instead, such
     * a small final chunk will be added together with the previous chunk
     * (unless it's the only one).
     */
#define RAND_LOAD_BUF_SIZE (RAND_BUF_SIZE + RAND_DRBG_STRENGTH)
    unsigned char buf[RAND_LOAD_BUF_SIZE];

#ifndef OPENSSL_NO_POSIX_IO
    struct stat sb;
#endif
    int i, n, ret = 0;
    FILE *in;

    if (bytes == 0)
        return 0;

    if ((in = openssl_fopen(file, "rb")) == NULL) {
        RANDerr(RAND_F_RAND_LOAD_FILE, RAND_R_CANNOT_OPEN_FILE);
        ERR_add_error_data(2, "Filename=", file);
        return -1;
    }

#ifndef OPENSSL_NO_POSIX_IO
    if (fstat(fileno(in), &sb) < 0) {
        RANDerr(RAND_F_RAND_LOAD_FILE, RAND_R_INTERNAL_ERROR);
        ERR_add_error_data(2, "Filename=", file);
        fclose(in);
        return -1;
    }

    if (bytes < 0) {
        if (S_ISREG(sb.st_mode))
            bytes = sb.st_size;
        else
            bytes = RAND_DRBG_STRENGTH;
    }
#endif
    /*
     * On VMS, setbuf() will only take 32-bit pointers, and a compilation
     * with /POINTER_SIZE=64 will give off a MAYLOSEDATA2 warning here.
     * However, we trust that the C RTL will never give us a FILE pointer
     * above the first 4 GB of memory, so we simply turn off the warning
     * temporarily.
     */
#if defined(OPENSSL_SYS_VMS) && defined(__DECC)
# pragma environment save
# pragma message disable maylosedata2
#endif
    /*
     * Don't buffer, because even if |file| is regular file, we have
     * no control over the buffer, so why would we want a copy of its
     * contents lying around?
     */
    setbuf(in, NULL);
#if defined(OPENSSL_SYS_VMS) && defined(__DECC)
# pragma environment restore
#endif

    for ( ; ; ) {
        if (bytes > 0)
            n = (bytes <= RAND_LOAD_BUF_SIZE) ? (int)bytes : RAND_BUF_SIZE;
        else
            n = RAND_LOAD_BUF_SIZE;
        i = fread(buf, 1, n, in);
#ifdef EINTR
        if (ferror(in) && errno == EINTR){
            clearerr(in);
            if (i == 0)
                continue;
        }
#endif
        if (i == 0)
            break;

        RAND_add(buf, i, (double)i);
        ret += i;

        /* If given a bytecount, and we did it, break. */
        if (bytes > 0 && (bytes -= i) <= 0)
            break;
    }

    OPENSSL_cleanse(buf, sizeof(buf));
    fclose(in);
    if (!RAND_status()) {
        RANDerr(RAND_F_RAND_LOAD_FILE, RAND_R_RESEED_ERROR);
        ERR_add_error_data(2, "Filename=", file);
        return -1;
    }

    return ret;
}

int RAND_write_file(const char *file)
{
    unsigned char buf[RAND_BUF_SIZE];
    int ret = -1;
    FILE *out = NULL;
#ifndef OPENSSL_NO_POSIX_IO
    struct stat sb;

    if (stat(file, &sb) >= 0 && !S_ISREG(sb.st_mode)) {
        RANDerr(RAND_F_RAND_WRITE_FILE, RAND_R_NOT_A_REGULAR_FILE);
        ERR_add_error_data(2, "Filename=", file);
        return -1;
    }
#endif

    /* Collect enough random data. */
    if (RAND_priv_bytes(buf, (int)sizeof(buf)) != 1)
        return  -1;

#if defined(O_CREAT) && !defined(OPENSSL_NO_POSIX_IO) && \
    !defined(OPENSSL_SYS_VMS) && !defined(OPENSSL_SYS_WINDOWS)
    {
# ifndef O_BINARY
#  define O_BINARY 0
# endif
        /*
         * chmod(..., 0600) is too late to protect the file, permissions
         * should be restrictive from the start
         */
        int fd = open(file, O_WRONLY | O_CREAT | O_BINARY, 0600);
        if (fd != -1)
            out = fdopen(fd, "wb");
    }
#endif

#ifdef OPENSSL_SYS_VMS
    /*
     * VMS NOTE: Prior versions of this routine created a _new_ version of
     * the rand file for each call into this routine, then deleted all
     * existing versions named ;-1, and finally renamed the current version
     * as ';1'. Under concurrent usage, this resulted in an RMS race
     * condition in rename() which could orphan files (see vms message help
     * for RMS$_REENT). With the fopen() calls below, openssl/VMS now shares
     * the top-level version of the rand file. Note that there may still be
     * conditions where the top-level rand file is locked. If so, this code
     * will then create a new version of the rand file. Without the delete
     * and rename code, this can result in ascending file versions that stop
     * at version 32767, and this routine will then return an error. The
     * remedy for this is to recode the calling application to avoid
     * concurrent use of the rand file, or synchronize usage at the
     * application level. Also consider whether or not you NEED a persistent
     * rand file in a concurrent use situation.
     */
    out = openssl_fopen(file, "rb+");
#endif

    if (out == NULL)
        out = openssl_fopen(file, "wb");
    if (out == NULL) {
        RANDerr(RAND_F_RAND_WRITE_FILE, RAND_R_CANNOT_OPEN_FILE);
        ERR_add_error_data(2, "Filename=", file);
        return -1;
    }

#if !defined(NO_CHMOD) && !defined(OPENSSL_NO_POSIX_IO)
    /*
     * Yes it's late to do this (see above comment), but better than nothing.
     */
    chmod(file, 0600);
#endif

    ret = fwrite(buf, 1, RAND_BUF_SIZE, out);
    fclose(out);
    OPENSSL_cleanse(buf, RAND_BUF_SIZE);
    return ret;
}

const char *RAND_file_name(char *buf, size_t size)
{
    char *s = NULL;
    size_t len;
    int use_randfile = 1;

#if defined(_WIN32) && defined(CP_UTF8)
    DWORD envlen;
    WCHAR *var;

    /* Look up various environment variables. */
    if ((envlen = GetEnvironmentVariableW(var = L"RANDFILE", NULL, 0)) == 0) {
        use_randfile = 0;
        if ((envlen = GetEnvironmentVariableW(var = L"HOME", NULL, 0)) == 0
                && (envlen = GetEnvironmentVariableW(var = L"USERPROFILE",
                                                  NULL, 0)) == 0)
            envlen = GetEnvironmentVariableW(var = L"SYSTEMROOT", NULL, 0);
    }

    /* If we got a value, allocate space to hold it and then get it. */
    if (envlen != 0) {
        int sz;
        WCHAR *val = _alloca(envlen * sizeof(WCHAR));

        if (GetEnvironmentVariableW(var, val, envlen) < envlen
                && (sz = WideCharToMultiByte(CP_UTF8, 0, val, -1, NULL, 0,
                                             NULL, NULL)) != 0) {
            s = _alloca(sz);
            if (WideCharToMultiByte(CP_UTF8, 0, val, -1, s, sz,
                                    NULL, NULL) == 0)
                s = NULL;
        }
    }
#else
    if ((s = ossl_safe_getenv("RANDFILE")) == NULL || *s == '\0') {
        use_randfile = 0;
        s = ossl_safe_getenv("HOME");
    }
#endif

#ifdef DEFAULT_HOME
    if (!use_randfile && s == NULL)
        s = DEFAULT_HOME;
#endif
    if (s == NULL || *s == '\0')
        return NULL;

    len = strlen(s);
    if (use_randfile) {
        if (len + 1 >= size)
            return NULL;
        strcpy(buf, s);
    } else {
        if (len + 1 + strlen(RFILE) + 1 >= size)
            return NULL;
        strcpy(buf, s);
#ifndef OPENSSL_SYS_VMS
        strcat(buf, "/");
#endif
        strcat(buf, RFILE);
    }

    return buf;
}
