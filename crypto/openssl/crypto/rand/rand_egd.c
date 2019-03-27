/*
 * Copyright 2000-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslconf.h>
#ifdef OPENSSL_NO_EGD
NON_EMPTY_TRANSLATION_UNIT
#else

# include <openssl/crypto.h>
# include <openssl/e_os2.h>
# include <openssl/rand.h>

/*
 * Query an EGD
 */

# if defined(OPENSSL_SYS_WIN32) || defined(OPENSSL_SYS_VMS) || defined(OPENSSL_SYS_MSDOS) || defined(OPENSSL_SYS_VXWORKS) || defined(OPENSSL_SYS_VOS) || defined(OPENSSL_SYS_UEFI)
int RAND_query_egd_bytes(const char *path, unsigned char *buf, int bytes)
{
    return -1;
}

int RAND_egd(const char *path)
{
    return -1;
}

int RAND_egd_bytes(const char *path, int bytes)
{
    return -1;
}

# else

#  include OPENSSL_UNISTD
#  include <stddef.h>
#  include <sys/types.h>
#  include <sys/socket.h>
#  ifndef NO_SYS_UN_H
#   ifdef OPENSSL_SYS_VXWORKS
#    include <streams/un.h>
#   else
#    include <sys/un.h>
#   endif
#  else
struct sockaddr_un {
    short sun_family;           /* AF_UNIX */
    char sun_path[108];         /* path name (gag) */
};
#  endif                         /* NO_SYS_UN_H */
#  include <string.h>
#  include <errno.h>

int RAND_query_egd_bytes(const char *path, unsigned char *buf, int bytes)
{
    FILE *fp = NULL;
    struct sockaddr_un addr;
    int mybuffer, ret = -1, i, numbytes, fd;
    unsigned char tempbuf[255];

    if (bytes > (int)sizeof(tempbuf))
        return -1;

    /* Make socket. */
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path))
        return -1;
    strcpy(addr.sun_path, path);
    i = offsetof(struct sockaddr_un, sun_path) + strlen(path);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1 || (fp = fdopen(fd, "r+")) == NULL)
        return -1;
    setbuf(fp, NULL);

    /* Try to connect */
    for ( ; ; ) {
        if (connect(fd, (struct sockaddr *)&addr, i) == 0)
            break;
#  ifdef EISCONN
        if (errno == EISCONN)
            break;
#  endif
        switch (errno) {
#  ifdef EINTR
        case EINTR:
#  endif
#  ifdef EAGAIN
        case EAGAIN:
#  endif
#  ifdef EINPROGRESS
        case EINPROGRESS:
#  endif
#  ifdef EALREADY
        case EALREADY:
#  endif
            /* No error, try again */
            break;
        default:
            ret = -1;
            goto err;
        }
    }

    /* Make request, see how many bytes we can get back. */
    tempbuf[0] = 1;
    tempbuf[1] = bytes;
    if (fwrite(tempbuf, sizeof(char), 2, fp) != 2 || fflush(fp) == EOF)
        goto err;
    if (fread(tempbuf, sizeof(char), 1, fp) != 1 || tempbuf[0] == 0)
        goto err;
    numbytes = tempbuf[0];

    /* Which buffer are we using? */
    mybuffer = buf == NULL;
    if (mybuffer)
        buf = tempbuf;

    /* Read bytes. */
    i = fread(buf, sizeof(char), numbytes, fp);
    if (i < numbytes)
        goto err;
    ret = numbytes;
    if (mybuffer)
        RAND_add(tempbuf, i, i);

 err:
    if (fp != NULL)
        fclose(fp);
    return ret;
}

int RAND_egd_bytes(const char *path, int bytes)
{
    int num;

    num = RAND_query_egd_bytes(path, NULL, bytes);
    if (num < 0)
        return -1;
    if (RAND_status() != 1)
        return -1;
    return num;
}

int RAND_egd(const char *path)
{
    return RAND_egd_bytes(path, 255);
}

# endif

#endif
