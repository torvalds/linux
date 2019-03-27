/*
 * Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_AFALG_H
# define HEADER_AFALG_H

# if defined(__GNUC__) && __GNUC__ >= 4 && \
     (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L)
#  pragma GCC diagnostic ignored "-Wvariadic-macros"
# endif

# ifdef ALG_DEBUG
#  define ALG_DGB(x, ...) fprintf(stderr, "ALG_DBG: " x, __VA_ARGS__)
#  define ALG_INFO(x, ...) fprintf(stderr, "ALG_INFO: " x, __VA_ARGS__)
#  define ALG_WARN(x, ...) fprintf(stderr, "ALG_WARN: " x, __VA_ARGS__)
# else
#  define ALG_DGB(x, ...)
#  define ALG_INFO(x, ...)
#  define ALG_WARN(x, ...)
# endif

# define ALG_ERR(x, ...) fprintf(stderr, "ALG_ERR: " x, __VA_ARGS__)
# define ALG_PERR(x, ...) \
                do { \
                    fprintf(stderr, "ALG_PERR: " x, __VA_ARGS__); \
                    perror(NULL); \
                } while(0)
# define ALG_PWARN(x, ...) \
                do { \
                    fprintf(stderr, "ALG_PERR: " x, __VA_ARGS__); \
                    perror(NULL); \
                } while(0)

# ifndef AES_BLOCK_SIZE
#  define AES_BLOCK_SIZE   16
# endif
# define AES_KEY_SIZE_128 16
# define AES_KEY_SIZE_192 24
# define AES_KEY_SIZE_256 32
# define AES_IV_LEN       16

# define MAX_INFLIGHTS 1

typedef enum {
    MODE_UNINIT = 0,
    MODE_SYNC,
    MODE_ASYNC
} op_mode;

enum {
    AES_CBC_128 = 0,
    AES_CBC_192,
    AES_CBC_256
};

struct cbc_cipher_handles {
    int key_size;
    EVP_CIPHER *_hidden;
};

typedef struct cbc_cipher_handles cbc_handles;

struct afalg_aio_st {
    int efd;
    op_mode mode;
    aio_context_t aio_ctx;
    struct io_event events[MAX_INFLIGHTS];
    struct iocb cbt[MAX_INFLIGHTS];
};
typedef struct afalg_aio_st afalg_aio;

/*
 * MAGIC Number to identify correct initialisation
 * of afalg_ctx.
 */
# define MAGIC_INIT_NUM 0x1890671

struct afalg_ctx_st {
    int init_done;
    int sfd;
    int bfd;
# ifdef ALG_ZERO_COPY
    int zc_pipe[2];
# endif
    afalg_aio aio;
};

typedef struct afalg_ctx_st afalg_ctx;
#endif
