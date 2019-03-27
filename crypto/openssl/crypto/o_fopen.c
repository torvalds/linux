/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

# if defined(__linux) || defined(__sun) || defined(__hpux)
/*
 * Following definition aliases fopen to fopen64 on above mentioned
 * platforms. This makes it possible to open and sequentially access files
 * larger than 2GB from 32-bit application. It does not allow to traverse
 * them beyond 2GB with fseek/ftell, but on the other hand *no* 32-bit
 * platform permits that, not with fseek/ftell. Not to mention that breaking
 * 2GB limit for seeking would require surgery to *our* API. But sequential
 * access suffices for practical cases when you can run into large files,
 * such as fingerprinting, so we can let API alone. For reference, the list
 * of 32-bit platforms which allow for sequential access of large files
 * without extra "magic" comprise *BSD, Darwin, IRIX...
 */
#  ifndef _FILE_OFFSET_BITS
#   define _FILE_OFFSET_BITS 64
#  endif
# endif

#include "e_os.h"
#include "internal/cryptlib.h"

#if !defined(OPENSSL_NO_STDIO)

# include <stdio.h>
# ifdef __DJGPP__
#  include <unistd.h>
# endif

FILE *openssl_fopen(const char *filename, const char *mode)
{
    FILE *file = NULL;
# if defined(_WIN32) && defined(CP_UTF8)
    int sz, len_0 = (int)strlen(filename) + 1;
    DWORD flags;

    /*
     * Basically there are three cases to cover: a) filename is
     * pure ASCII string; b) actual UTF-8 encoded string and
     * c) locale-ized string, i.e. one containing 8-bit
     * characters that are meaningful in current system locale.
     * If filename is pure ASCII or real UTF-8 encoded string,
     * MultiByteToWideChar succeeds and _wfopen works. If
     * filename is locale-ized string, chances are that
     * MultiByteToWideChar fails reporting
     * ERROR_NO_UNICODE_TRANSLATION, in which case we fall
     * back to fopen...
     */
    if ((sz = MultiByteToWideChar(CP_UTF8, (flags = MB_ERR_INVALID_CHARS),
                                  filename, len_0, NULL, 0)) > 0 ||
        (GetLastError() == ERROR_INVALID_FLAGS &&
         (sz = MultiByteToWideChar(CP_UTF8, (flags = 0),
                                   filename, len_0, NULL, 0)) > 0)
        ) {
        WCHAR wmode[8];
        WCHAR *wfilename = _alloca(sz * sizeof(WCHAR));

        if (MultiByteToWideChar(CP_UTF8, flags,
                                filename, len_0, wfilename, sz) &&
            MultiByteToWideChar(CP_UTF8, 0, mode, strlen(mode) + 1,
                                wmode, OSSL_NELEM(wmode)) &&
            (file = _wfopen(wfilename, wmode)) == NULL &&
            (errno == ENOENT || errno == EBADF)
            ) {
            /*
             * UTF-8 decode succeeded, but no file, filename
             * could still have been locale-ized...
             */
            file = fopen(filename, mode);
        }
    } else if (GetLastError() == ERROR_NO_UNICODE_TRANSLATION) {
        file = fopen(filename, mode);
    }
# elif defined(__DJGPP__)
    {
        char *newname = NULL;

        if (pathconf(filename, _PC_NAME_MAX) <= 12) {  /* 8.3 file system? */
            char *iterator;
            char lastchar;

            if ((newname = OPENSSL_malloc(strlen(filename) + 1)) == NULL) {
                CRYPTOerr(CRYPTO_F_OPENSSL_FOPEN, ERR_R_MALLOC_FAILURE);
                return NULL;
            }

            for (iterator = newname, lastchar = '\0';
                *filename; filename++, iterator++) {
                if (lastchar == '/' && filename[0] == '.'
                    && filename[1] != '.' && filename[1] != '/') {
                    /* Leading dots are not permitted in plain DOS. */
                    *iterator = '_';
                } else {
                    *iterator = *filename;
                }
                lastchar = *filename;
            }
            *iterator = '\0';
            filename = newname;
        }
        file = fopen(filename, mode);

        OPENSSL_free(newname);
    }
# else
    file = fopen(filename, mode);
# endif
    return file;
}

#else

void *openssl_fopen(const char *filename, const char *mode)
{
    return NULL;
}

#endif
