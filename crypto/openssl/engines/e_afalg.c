/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* Required for vmsplice */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <openssl/engine.h>
#include <openssl/async.h>
#include <openssl/err.h>
#include "internal/nelem.h"

#include <sys/socket.h>
#include <linux/version.h>
#define K_MAJ   4
#define K_MIN1  1
#define K_MIN2  0
#if LINUX_VERSION_CODE < KERNEL_VERSION(K_MAJ, K_MIN1, K_MIN2) || \
    !defined(AF_ALG)
# ifndef PEDANTIC
#  warning "AFALG ENGINE requires Kernel Headers >= 4.1.0"
#  warning "Skipping Compilation of AFALG engine"
# endif
void engine_load_afalg_int(void);
void engine_load_afalg_int(void)
{
}
#else

# include <linux/if_alg.h>
# include <fcntl.h>
# include <sys/utsname.h>

# include <linux/aio_abi.h>
# include <sys/syscall.h>
# include <errno.h>

# include "e_afalg.h"
# include "e_afalg_err.c"

# ifndef SOL_ALG
#  define SOL_ALG 279
# endif

# ifdef ALG_ZERO_COPY
#  ifndef SPLICE_F_GIFT
#   define SPLICE_F_GIFT    (0x08)
#  endif
# endif

# define ALG_AES_IV_LEN 16
# define ALG_IV_LEN(len) (sizeof(struct af_alg_iv) + (len))
# define ALG_OP_TYPE     unsigned int
# define ALG_OP_LEN      (sizeof(ALG_OP_TYPE))

#define ALG_MAX_SALG_NAME       64
#define ALG_MAX_SALG_TYPE       14

# ifdef OPENSSL_NO_DYNAMIC_ENGINE
void engine_load_afalg_int(void);
# endif

/* Local Linkage Functions */
static int afalg_init_aio(afalg_aio *aio);
static int afalg_fin_cipher_aio(afalg_aio *ptr, int sfd,
                                unsigned char *buf, size_t len);
static int afalg_create_sk(afalg_ctx *actx, const char *ciphertype,
                                const char *ciphername);
static int afalg_destroy(ENGINE *e);
static int afalg_init(ENGINE *e);
static int afalg_finish(ENGINE *e);
static const EVP_CIPHER *afalg_aes_cbc(int nid);
static cbc_handles *get_cipher_handle(int nid);
static int afalg_ciphers(ENGINE *e, const EVP_CIPHER **cipher,
                         const int **nids, int nid);
static int afalg_cipher_init(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                             const unsigned char *iv, int enc);
static int afalg_do_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                           const unsigned char *in, size_t inl);
static int afalg_cipher_cleanup(EVP_CIPHER_CTX *ctx);
static int afalg_chk_platform(void);

/* Engine Id and Name */
static const char *engine_afalg_id = "afalg";
static const char *engine_afalg_name = "AFALG engine support";

static int afalg_cipher_nids[] = {
    NID_aes_128_cbc,
    NID_aes_192_cbc,
    NID_aes_256_cbc,
};

static cbc_handles cbc_handle[] = {{AES_KEY_SIZE_128, NULL},
                                    {AES_KEY_SIZE_192, NULL},
                                    {AES_KEY_SIZE_256, NULL}};

static ossl_inline int io_setup(unsigned n, aio_context_t *ctx)
{
    return syscall(__NR_io_setup, n, ctx);
}

static ossl_inline int eventfd(int n)
{
    return syscall(__NR_eventfd2, n, 0);
}

static ossl_inline int io_destroy(aio_context_t ctx)
{
    return syscall(__NR_io_destroy, ctx);
}

static ossl_inline int io_read(aio_context_t ctx, long n, struct iocb **iocb)
{
    return syscall(__NR_io_submit, ctx, n, iocb);
}

static ossl_inline int io_getevents(aio_context_t ctx, long min, long max,
                               struct io_event *events,
                               struct timespec *timeout)
{
    return syscall(__NR_io_getevents, ctx, min, max, events, timeout);
}

static void afalg_waitfd_cleanup(ASYNC_WAIT_CTX *ctx, const void *key,
                                 OSSL_ASYNC_FD waitfd, void *custom)
{
    close(waitfd);
}

static int afalg_setup_async_event_notification(afalg_aio *aio)
{
    ASYNC_JOB *job;
    ASYNC_WAIT_CTX *waitctx;
    void *custom = NULL;
    int ret;

    if ((job = ASYNC_get_current_job()) != NULL) {
        /* Async mode */
        waitctx = ASYNC_get_wait_ctx(job);
        if (waitctx == NULL) {
            ALG_WARN("%s(%d): ASYNC_get_wait_ctx error", __FILE__, __LINE__);
            return 0;
        }
        /* Get waitfd from ASYNC_WAIT_CTX if it is already set */
        ret = ASYNC_WAIT_CTX_get_fd(waitctx, engine_afalg_id,
                                    &aio->efd, &custom);
        if (ret == 0) {
            /*
             * waitfd is not set in ASYNC_WAIT_CTX, create a new one
             * and set it. efd will be signaled when AIO operation completes
             */
            aio->efd = eventfd(0);
            if (aio->efd == -1) {
                ALG_PERR("%s(%d): Failed to get eventfd : ", __FILE__,
                         __LINE__);
                AFALGerr(AFALG_F_AFALG_SETUP_ASYNC_EVENT_NOTIFICATION,
                         AFALG_R_EVENTFD_FAILED);
                return 0;
            }
            ret = ASYNC_WAIT_CTX_set_wait_fd(waitctx, engine_afalg_id,
                                             aio->efd, custom,
                                             afalg_waitfd_cleanup);
            if (ret == 0) {
                ALG_WARN("%s(%d): Failed to set wait fd", __FILE__, __LINE__);
                close(aio->efd);
                return 0;
            }
            /* make fd non-blocking in async mode */
            if (fcntl(aio->efd, F_SETFL, O_NONBLOCK) != 0) {
                ALG_WARN("%s(%d): Failed to set event fd as NONBLOCKING",
                         __FILE__, __LINE__);
            }
        }
        aio->mode = MODE_ASYNC;
    } else {
        /* Sync mode */
        aio->efd = eventfd(0);
        if (aio->efd == -1) {
            ALG_PERR("%s(%d): Failed to get eventfd : ", __FILE__, __LINE__);
            AFALGerr(AFALG_F_AFALG_SETUP_ASYNC_EVENT_NOTIFICATION,
                     AFALG_R_EVENTFD_FAILED);
            return 0;
        }
        aio->mode = MODE_SYNC;
    }
    return 1;
}

static int afalg_init_aio(afalg_aio *aio)
{
    int r = -1;

    /* Initialise for AIO */
    aio->aio_ctx = 0;
    r = io_setup(MAX_INFLIGHTS, &aio->aio_ctx);
    if (r < 0) {
        ALG_PERR("%s(%d): io_setup error : ", __FILE__, __LINE__);
        AFALGerr(AFALG_F_AFALG_INIT_AIO, AFALG_R_IO_SETUP_FAILED);
        return 0;
    }

    memset(aio->cbt, 0, sizeof(aio->cbt));
    aio->efd = -1;
    aio->mode = MODE_UNINIT;

    return 1;
}

static int afalg_fin_cipher_aio(afalg_aio *aio, int sfd, unsigned char *buf,
                                size_t len)
{
    int r;
    int retry = 0;
    unsigned int done = 0;
    struct iocb *cb;
    struct timespec timeout;
    struct io_event events[MAX_INFLIGHTS];
    u_int64_t eval = 0;

    timeout.tv_sec = 0;
    timeout.tv_nsec = 0;

    /* if efd has not been initialised yet do it here */
    if (aio->mode == MODE_UNINIT) {
        r = afalg_setup_async_event_notification(aio);
        if (r == 0)
            return 0;
    }

    cb = &(aio->cbt[0 % MAX_INFLIGHTS]);
    memset(cb, '\0', sizeof(*cb));
    cb->aio_fildes = sfd;
    cb->aio_lio_opcode = IOCB_CMD_PREAD;
    /*
     * The pointer has to be converted to unsigned value first to avoid
     * sign extension on cast to 64 bit value in 32-bit builds
     */
    cb->aio_buf = (size_t)buf;
    cb->aio_offset = 0;
    cb->aio_data = 0;
    cb->aio_nbytes = len;
    cb->aio_flags = IOCB_FLAG_RESFD;
    cb->aio_resfd = aio->efd;

    /*
     * Perform AIO read on AFALG socket, this in turn performs an async
     * crypto operation in kernel space
     */
    r = io_read(aio->aio_ctx, 1, &cb);
    if (r < 0) {
        ALG_PWARN("%s(%d): io_read failed : ", __FILE__, __LINE__);
        return 0;
    }

    do {
        /* While AIO read is being performed pause job */
        ASYNC_pause_job();

        /* Check for completion of AIO read */
        r = read(aio->efd, &eval, sizeof(eval));
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            ALG_PERR("%s(%d): read failed for event fd : ", __FILE__, __LINE__);
            return 0;
        } else if (r == 0 || eval <= 0) {
            ALG_WARN("%s(%d): eventfd read %d bytes, eval = %lu\n", __FILE__,
                     __LINE__, r, eval);
        }
        if (eval > 0) {

            /* Get results of AIO read */
            r = io_getevents(aio->aio_ctx, 1, MAX_INFLIGHTS,
                             events, &timeout);
            if (r > 0) {
                /*
                 * events.res indicates the actual status of the operation.
                 * Handle the error condition first.
                 */
                if (events[0].res < 0) {
                    /*
                     * Underlying operation cannot be completed at the time
                     * of previous submission. Resubmit for the operation.
                     */
                    if (events[0].res == -EBUSY && retry++ < 3) {
                        r = io_read(aio->aio_ctx, 1, &cb);
                        if (r < 0) {
                            ALG_PERR("%s(%d): retry %d for io_read failed : ",
                                     __FILE__, __LINE__, retry);
                            return 0;
                        }
                        continue;
                    } else {
                        /*
                         * Retries exceed for -EBUSY or unrecoverable error
                         * condition for this instance of operation.
                         */
                        ALG_WARN
                            ("%s(%d): Crypto Operation failed with code %lld\n",
                             __FILE__, __LINE__, events[0].res);
                        return 0;
                    }
                }
                /* Operation successful. */
                done = 1;
            } else if (r < 0) {
                ALG_PERR("%s(%d): io_getevents failed : ", __FILE__, __LINE__);
                return 0;
            } else {
                ALG_WARN("%s(%d): io_geteventd read 0 bytes\n", __FILE__,
                         __LINE__);
            }
        }
    } while (!done);

    return 1;
}

static ossl_inline void afalg_set_op_sk(struct cmsghdr *cmsg,
                                   const ALG_OP_TYPE op)
{
    cmsg->cmsg_level = SOL_ALG;
    cmsg->cmsg_type = ALG_SET_OP;
    cmsg->cmsg_len = CMSG_LEN(ALG_OP_LEN);
    memcpy(CMSG_DATA(cmsg), &op, ALG_OP_LEN);
}

static void afalg_set_iv_sk(struct cmsghdr *cmsg, const unsigned char *iv,
                            const unsigned int len)
{
    struct af_alg_iv *aiv;

    cmsg->cmsg_level = SOL_ALG;
    cmsg->cmsg_type = ALG_SET_IV;
    cmsg->cmsg_len = CMSG_LEN(ALG_IV_LEN(len));
    aiv = (struct af_alg_iv *)CMSG_DATA(cmsg);
    aiv->ivlen = len;
    memcpy(aiv->iv, iv, len);
}

static ossl_inline int afalg_set_key(afalg_ctx *actx, const unsigned char *key,
                                const int klen)
{
    int ret;
    ret = setsockopt(actx->bfd, SOL_ALG, ALG_SET_KEY, key, klen);
    if (ret < 0) {
        ALG_PERR("%s(%d): Failed to set socket option : ", __FILE__, __LINE__);
        AFALGerr(AFALG_F_AFALG_SET_KEY, AFALG_R_SOCKET_SET_KEY_FAILED);
        return 0;
    }
    return 1;
}

static int afalg_create_sk(afalg_ctx *actx, const char *ciphertype,
                                const char *ciphername)
{
    struct sockaddr_alg sa;
    int r = -1;

    actx->bfd = actx->sfd = -1;

    memset(&sa, 0, sizeof(sa));
    sa.salg_family = AF_ALG;
    strncpy((char *) sa.salg_type, ciphertype, ALG_MAX_SALG_TYPE);
    sa.salg_type[ALG_MAX_SALG_TYPE-1] = '\0';
    strncpy((char *) sa.salg_name, ciphername, ALG_MAX_SALG_NAME);
    sa.salg_name[ALG_MAX_SALG_NAME-1] = '\0';

    actx->bfd = socket(AF_ALG, SOCK_SEQPACKET, 0);
    if (actx->bfd == -1) {
        ALG_PERR("%s(%d): Failed to open socket : ", __FILE__, __LINE__);
        AFALGerr(AFALG_F_AFALG_CREATE_SK, AFALG_R_SOCKET_CREATE_FAILED);
        goto err;
    }

    r = bind(actx->bfd, (struct sockaddr *)&sa, sizeof(sa));
    if (r < 0) {
        ALG_PERR("%s(%d): Failed to bind socket : ", __FILE__, __LINE__);
        AFALGerr(AFALG_F_AFALG_CREATE_SK, AFALG_R_SOCKET_BIND_FAILED);
        goto err;
    }

    actx->sfd = accept(actx->bfd, NULL, 0);
    if (actx->sfd < 0) {
        ALG_PERR("%s(%d): Socket Accept Failed : ", __FILE__, __LINE__);
        AFALGerr(AFALG_F_AFALG_CREATE_SK, AFALG_R_SOCKET_ACCEPT_FAILED);
        goto err;
    }

    return 1;

 err:
    if (actx->bfd >= 0)
        close(actx->bfd);
    if (actx->sfd >= 0)
        close(actx->sfd);
    actx->bfd = actx->sfd = -1;
    return 0;
}

static int afalg_start_cipher_sk(afalg_ctx *actx, const unsigned char *in,
                                 size_t inl, const unsigned char *iv,
                                 unsigned int enc)
{
    struct msghdr msg = { 0 };
    struct cmsghdr *cmsg;
    struct iovec iov;
    ssize_t sbytes;
# ifdef ALG_ZERO_COPY
    int ret;
# endif
    char cbuf[CMSG_SPACE(ALG_IV_LEN(ALG_AES_IV_LEN)) + CMSG_SPACE(ALG_OP_LEN)];

    memset(cbuf, 0, sizeof(cbuf));
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof(cbuf);

    /*
     * cipher direction (i.e. encrypt or decrypt) and iv are sent to the
     * kernel as part of sendmsg()'s ancillary data
     */
    cmsg = CMSG_FIRSTHDR(&msg);
    afalg_set_op_sk(cmsg, enc);
    cmsg = CMSG_NXTHDR(&msg, cmsg);
    afalg_set_iv_sk(cmsg, iv, ALG_AES_IV_LEN);

    /* iov that describes input data */
    iov.iov_base = (unsigned char *)in;
    iov.iov_len = inl;

    msg.msg_flags = MSG_MORE;

# ifdef ALG_ZERO_COPY
    /*
     * ZERO_COPY mode
     * Works best when buffer is 4k aligned
     * OPENS: out of place processing (i.e. out != in)
     */

    /* Input data is not sent as part of call to sendmsg() */
    msg.msg_iovlen = 0;
    msg.msg_iov = NULL;

    /* Sendmsg() sends iv and cipher direction to the kernel */
    sbytes = sendmsg(actx->sfd, &msg, 0);
    if (sbytes < 0) {
        ALG_PERR("%s(%d): sendmsg failed for zero copy cipher operation : ",
                 __FILE__, __LINE__);
        return 0;
    }

    /*
     * vmsplice and splice are used to pin the user space input buffer for
     * kernel space processing avoiding copys from user to kernel space
     */
    ret = vmsplice(actx->zc_pipe[1], &iov, 1, SPLICE_F_GIFT);
    if (ret < 0) {
        ALG_PERR("%s(%d): vmsplice failed : ", __FILE__, __LINE__);
        return 0;
    }

    ret = splice(actx->zc_pipe[0], NULL, actx->sfd, NULL, inl, 0);
    if (ret < 0) {
        ALG_PERR("%s(%d): splice failed : ", __FILE__, __LINE__);
        return 0;
    }
# else
    msg.msg_iovlen = 1;
    msg.msg_iov = &iov;

    /* Sendmsg() sends iv, cipher direction and input data to the kernel */
    sbytes = sendmsg(actx->sfd, &msg, 0);
    if (sbytes < 0) {
        ALG_PERR("%s(%d): sendmsg failed for cipher operation : ", __FILE__,
                 __LINE__);
        return 0;
    }

    if (sbytes != (ssize_t) inl) {
        ALG_WARN("Cipher operation send bytes %zd != inlen %zd\n", sbytes,
                inl);
        return 0;
    }
# endif

    return 1;
}

static int afalg_cipher_init(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                             const unsigned char *iv, int enc)
{
    int ciphertype;
    int ret;
    afalg_ctx *actx;
    char ciphername[ALG_MAX_SALG_NAME];

    if (ctx == NULL || key == NULL) {
        ALG_WARN("%s(%d): Null Parameter\n", __FILE__, __LINE__);
        return 0;
    }

    if (EVP_CIPHER_CTX_cipher(ctx) == NULL) {
        ALG_WARN("%s(%d): Cipher object NULL\n", __FILE__, __LINE__);
        return 0;
    }

    actx = EVP_CIPHER_CTX_get_cipher_data(ctx);
    if (actx == NULL) {
        ALG_WARN("%s(%d): Cipher data NULL\n", __FILE__, __LINE__);
        return 0;
    }

    ciphertype = EVP_CIPHER_CTX_nid(ctx);
    switch (ciphertype) {
    case NID_aes_128_cbc:
    case NID_aes_192_cbc:
    case NID_aes_256_cbc:
        strncpy(ciphername, "cbc(aes)", ALG_MAX_SALG_NAME);
        break;
    default:
        ALG_WARN("%s(%d): Unsupported Cipher type %d\n", __FILE__, __LINE__,
                 ciphertype);
        return 0;
    }
    ciphername[ALG_MAX_SALG_NAME-1]='\0';

    if (ALG_AES_IV_LEN != EVP_CIPHER_CTX_iv_length(ctx)) {
        ALG_WARN("%s(%d): Unsupported IV length :%d\n", __FILE__, __LINE__,
                 EVP_CIPHER_CTX_iv_length(ctx));
        return 0;
    }

    /* Setup AFALG socket for crypto processing */
    ret = afalg_create_sk(actx, "skcipher", ciphername);
    if (ret < 1)
        return 0;


    ret = afalg_set_key(actx, key, EVP_CIPHER_CTX_key_length(ctx));
    if (ret < 1)
        goto err;

    /* Setup AIO ctx to allow async AFALG crypto processing */
    if (afalg_init_aio(&actx->aio) == 0)
        goto err;

# ifdef ALG_ZERO_COPY
    pipe(actx->zc_pipe);
# endif

    actx->init_done = MAGIC_INIT_NUM;

    return 1;

err:
    close(actx->sfd);
    close(actx->bfd);
    return 0;
}

static int afalg_do_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                           const unsigned char *in, size_t inl)
{
    afalg_ctx *actx;
    int ret;
    char nxtiv[ALG_AES_IV_LEN] = { 0 };

    if (ctx == NULL || out == NULL || in == NULL) {
        ALG_WARN("NULL parameter passed to function %s(%d)\n", __FILE__,
                 __LINE__);
        return 0;
    }

    actx = (afalg_ctx *) EVP_CIPHER_CTX_get_cipher_data(ctx);
    if (actx == NULL || actx->init_done != MAGIC_INIT_NUM) {
        ALG_WARN("%s afalg ctx passed\n",
                 ctx == NULL ? "NULL" : "Uninitialised");
        return 0;
    }

    /*
     * set iv now for decrypt operation as the input buffer can be
     * overwritten for inplace operation where in = out.
     */
    if (EVP_CIPHER_CTX_encrypting(ctx) == 0) {
        memcpy(nxtiv, in + (inl - ALG_AES_IV_LEN), ALG_AES_IV_LEN);
    }

    /* Send input data to kernel space */
    ret = afalg_start_cipher_sk(actx, (unsigned char *)in, inl,
                                EVP_CIPHER_CTX_iv(ctx),
                                EVP_CIPHER_CTX_encrypting(ctx));
    if (ret < 1) {
        return 0;
    }

    /* Perform async crypto operation in kernel space */
    ret = afalg_fin_cipher_aio(&actx->aio, actx->sfd, out, inl);
    if (ret < 1)
        return 0;

    if (EVP_CIPHER_CTX_encrypting(ctx)) {
        memcpy(EVP_CIPHER_CTX_iv_noconst(ctx), out + (inl - ALG_AES_IV_LEN),
               ALG_AES_IV_LEN);
    } else {
        memcpy(EVP_CIPHER_CTX_iv_noconst(ctx), nxtiv, ALG_AES_IV_LEN);
    }

    return 1;
}

static int afalg_cipher_cleanup(EVP_CIPHER_CTX *ctx)
{
    afalg_ctx *actx;

    if (ctx == NULL) {
        ALG_WARN("NULL parameter passed to function %s(%d)\n", __FILE__,
                 __LINE__);
        return 0;
    }

    actx = (afalg_ctx *) EVP_CIPHER_CTX_get_cipher_data(ctx);
    if (actx == NULL || actx->init_done != MAGIC_INIT_NUM) {
        ALG_WARN("%s afalg ctx passed\n",
                 ctx == NULL ? "NULL" : "Uninitialised");
        return 0;
    }

    close(actx->sfd);
    close(actx->bfd);
# ifdef ALG_ZERO_COPY
    close(actx->zc_pipe[0]);
    close(actx->zc_pipe[1]);
# endif
    /* close efd in sync mode, async mode is closed in afalg_waitfd_cleanup() */
    if (actx->aio.mode == MODE_SYNC)
        close(actx->aio.efd);
    io_destroy(actx->aio.aio_ctx);

    return 1;
}

static cbc_handles *get_cipher_handle(int nid)
{
    switch (nid) {
    case NID_aes_128_cbc:
        return &cbc_handle[AES_CBC_128];
    case NID_aes_192_cbc:
        return &cbc_handle[AES_CBC_192];
    case NID_aes_256_cbc:
        return &cbc_handle[AES_CBC_256];
    default:
        return NULL;
    }
}

static const EVP_CIPHER *afalg_aes_cbc(int nid)
{
    cbc_handles *cipher_handle = get_cipher_handle(nid);
    if (cipher_handle->_hidden == NULL
        && ((cipher_handle->_hidden =
         EVP_CIPHER_meth_new(nid,
                             AES_BLOCK_SIZE,
                             cipher_handle->key_size)) == NULL
        || !EVP_CIPHER_meth_set_iv_length(cipher_handle->_hidden,
                                          AES_IV_LEN)
        || !EVP_CIPHER_meth_set_flags(cipher_handle->_hidden,
                                      EVP_CIPH_CBC_MODE |
                                      EVP_CIPH_FLAG_DEFAULT_ASN1)
        || !EVP_CIPHER_meth_set_init(cipher_handle->_hidden,
                                     afalg_cipher_init)
        || !EVP_CIPHER_meth_set_do_cipher(cipher_handle->_hidden,
                                          afalg_do_cipher)
        || !EVP_CIPHER_meth_set_cleanup(cipher_handle->_hidden,
                                        afalg_cipher_cleanup)
        || !EVP_CIPHER_meth_set_impl_ctx_size(cipher_handle->_hidden,
                                              sizeof(afalg_ctx)))) {
        EVP_CIPHER_meth_free(cipher_handle->_hidden);
        cipher_handle->_hidden= NULL;
    }
    return cipher_handle->_hidden;
}

static int afalg_ciphers(ENGINE *e, const EVP_CIPHER **cipher,
                         const int **nids, int nid)
{
    int r = 1;

    if (cipher == NULL) {
        *nids = afalg_cipher_nids;
        return (sizeof(afalg_cipher_nids) / sizeof(afalg_cipher_nids[0]));
    }

    switch (nid) {
    case NID_aes_128_cbc:
    case NID_aes_192_cbc:
    case NID_aes_256_cbc:
        *cipher = afalg_aes_cbc(nid);
        break;
    default:
        *cipher = NULL;
        r = 0;
    }
    return r;
}

static int bind_afalg(ENGINE *e)
{
    /* Ensure the afalg error handling is set up */
    unsigned short i;
    ERR_load_AFALG_strings();

    if (!ENGINE_set_id(e, engine_afalg_id)
        || !ENGINE_set_name(e, engine_afalg_name)
        || !ENGINE_set_destroy_function(e, afalg_destroy)
        || !ENGINE_set_init_function(e, afalg_init)
        || !ENGINE_set_finish_function(e, afalg_finish)) {
        AFALGerr(AFALG_F_BIND_AFALG, AFALG_R_INIT_FAILED);
        return 0;
    }

    /*
     * Create _hidden_aes_xxx_cbc by calling afalg_aes_xxx_cbc
     * now, as bind_aflag can only be called by one thread at a
     * time.
     */
    for(i = 0; i < OSSL_NELEM(afalg_cipher_nids); i++) {
        if (afalg_aes_cbc(afalg_cipher_nids[i]) == NULL) {
            AFALGerr(AFALG_F_BIND_AFALG, AFALG_R_INIT_FAILED);
            return 0;
        }
    }

    if (!ENGINE_set_ciphers(e, afalg_ciphers)) {
        AFALGerr(AFALG_F_BIND_AFALG, AFALG_R_INIT_FAILED);
        return 0;
    }

    return 1;
}

# ifndef OPENSSL_NO_DYNAMIC_ENGINE
static int bind_helper(ENGINE *e, const char *id)
{
    if (id && (strcmp(id, engine_afalg_id) != 0))
        return 0;

    if (!afalg_chk_platform())
        return 0;

    if (!bind_afalg(e))
        return 0;
    return 1;
}

IMPLEMENT_DYNAMIC_CHECK_FN()
    IMPLEMENT_DYNAMIC_BIND_FN(bind_helper)
# endif

static int afalg_chk_platform(void)
{
    int ret;
    int i;
    int kver[3] = { -1, -1, -1 };
    int sock;
    char *str;
    struct utsname ut;

    ret = uname(&ut);
    if (ret != 0) {
        AFALGerr(AFALG_F_AFALG_CHK_PLATFORM,
                 AFALG_R_FAILED_TO_GET_PLATFORM_INFO);
        return 0;
    }

    str = strtok(ut.release, ".");
    for (i = 0; i < 3 && str != NULL; i++) {
        kver[i] = atoi(str);
        str = strtok(NULL, ".");
    }

    if (KERNEL_VERSION(kver[0], kver[1], kver[2])
        < KERNEL_VERSION(K_MAJ, K_MIN1, K_MIN2)) {
        ALG_ERR("ASYNC AFALG not supported this kernel(%d.%d.%d)\n",
                 kver[0], kver[1], kver[2]);
        ALG_ERR("ASYNC AFALG requires kernel version %d.%d.%d or later\n",
                 K_MAJ, K_MIN1, K_MIN2);
        AFALGerr(AFALG_F_AFALG_CHK_PLATFORM,
                 AFALG_R_KERNEL_DOES_NOT_SUPPORT_ASYNC_AFALG);
        return 0;
    }

    /* Test if we can actually create an AF_ALG socket */
    sock = socket(AF_ALG, SOCK_SEQPACKET, 0);
    if (sock == -1) {
        AFALGerr(AFALG_F_AFALG_CHK_PLATFORM, AFALG_R_SOCKET_CREATE_FAILED);
        return 0;
    }
    close(sock);

    return 1;
}

# ifdef OPENSSL_NO_DYNAMIC_ENGINE
static ENGINE *engine_afalg(void)
{
    ENGINE *ret = ENGINE_new();
    if (ret == NULL)
        return NULL;
    if (!bind_afalg(ret)) {
        ENGINE_free(ret);
        return NULL;
    }
    return ret;
}

void engine_load_afalg_int(void)
{
    ENGINE *toadd;

    if (!afalg_chk_platform())
        return;

    toadd = engine_afalg();
    if (toadd == NULL)
        return;
    ENGINE_add(toadd);
    ENGINE_free(toadd);
    ERR_clear_error();
}
# endif

static int afalg_init(ENGINE *e)
{
    return 1;
}

static int afalg_finish(ENGINE *e)
{
    return 1;
}

static int free_cbc(void)
{
    short unsigned int i;
    for(i = 0; i < OSSL_NELEM(afalg_cipher_nids); i++) {
        EVP_CIPHER_meth_free(cbc_handle[i]._hidden);
        cbc_handle[i]._hidden = NULL;
    }
    return 1;
}

static int afalg_destroy(ENGINE *e)
{
    ERR_unload_AFALG_strings();
    free_cbc();
    return 1;
}

#endif                          /* KERNEL VERSION */
