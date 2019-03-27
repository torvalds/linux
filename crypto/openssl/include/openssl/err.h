/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_ERR_H
# define HEADER_ERR_H

# include <openssl/e_os2.h>

# ifndef OPENSSL_NO_STDIO
#  include <stdio.h>
#  include <stdlib.h>
# endif

# include <openssl/ossl_typ.h>
# include <openssl/bio.h>
# include <openssl/lhash.h>

#ifdef  __cplusplus
extern "C" {
#endif

# ifndef OPENSSL_NO_ERR
#  define ERR_PUT_error(a,b,c,d,e)        ERR_put_error(a,b,c,d,e)
# else
#  define ERR_PUT_error(a,b,c,d,e)        ERR_put_error(a,b,c,NULL,0)
# endif

# include <errno.h>

# define ERR_TXT_MALLOCED        0x01
# define ERR_TXT_STRING          0x02

# define ERR_FLAG_MARK           0x01

# define ERR_NUM_ERRORS  16
typedef struct err_state_st {
    int err_flags[ERR_NUM_ERRORS];
    unsigned long err_buffer[ERR_NUM_ERRORS];
    char *err_data[ERR_NUM_ERRORS];
    int err_data_flags[ERR_NUM_ERRORS];
    const char *err_file[ERR_NUM_ERRORS];
    int err_line[ERR_NUM_ERRORS];
    int top, bottom;
} ERR_STATE;

/* library */
# define ERR_LIB_NONE            1
# define ERR_LIB_SYS             2
# define ERR_LIB_BN              3
# define ERR_LIB_RSA             4
# define ERR_LIB_DH              5
# define ERR_LIB_EVP             6
# define ERR_LIB_BUF             7
# define ERR_LIB_OBJ             8
# define ERR_LIB_PEM             9
# define ERR_LIB_DSA             10
# define ERR_LIB_X509            11
/* #define ERR_LIB_METH         12 */
# define ERR_LIB_ASN1            13
# define ERR_LIB_CONF            14
# define ERR_LIB_CRYPTO          15
# define ERR_LIB_EC              16
# define ERR_LIB_SSL             20
/* #define ERR_LIB_SSL23        21 */
/* #define ERR_LIB_SSL2         22 */
/* #define ERR_LIB_SSL3         23 */
/* #define ERR_LIB_RSAREF       30 */
/* #define ERR_LIB_PROXY        31 */
# define ERR_LIB_BIO             32
# define ERR_LIB_PKCS7           33
# define ERR_LIB_X509V3          34
# define ERR_LIB_PKCS12          35
# define ERR_LIB_RAND            36
# define ERR_LIB_DSO             37
# define ERR_LIB_ENGINE          38
# define ERR_LIB_OCSP            39
# define ERR_LIB_UI              40
# define ERR_LIB_COMP            41
# define ERR_LIB_ECDSA           42
# define ERR_LIB_ECDH            43
# define ERR_LIB_OSSL_STORE      44
# define ERR_LIB_FIPS            45
# define ERR_LIB_CMS             46
# define ERR_LIB_TS              47
# define ERR_LIB_HMAC            48
/* # define ERR_LIB_JPAKE       49 */
# define ERR_LIB_CT              50
# define ERR_LIB_ASYNC           51
# define ERR_LIB_KDF             52
# define ERR_LIB_SM2             53

# define ERR_LIB_USER            128

# define SYSerr(f,r)  ERR_PUT_error(ERR_LIB_SYS,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define BNerr(f,r)   ERR_PUT_error(ERR_LIB_BN,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define RSAerr(f,r)  ERR_PUT_error(ERR_LIB_RSA,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define DHerr(f,r)   ERR_PUT_error(ERR_LIB_DH,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define EVPerr(f,r)  ERR_PUT_error(ERR_LIB_EVP,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define BUFerr(f,r)  ERR_PUT_error(ERR_LIB_BUF,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define OBJerr(f,r)  ERR_PUT_error(ERR_LIB_OBJ,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define PEMerr(f,r)  ERR_PUT_error(ERR_LIB_PEM,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define DSAerr(f,r)  ERR_PUT_error(ERR_LIB_DSA,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define X509err(f,r) ERR_PUT_error(ERR_LIB_X509,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define ASN1err(f,r) ERR_PUT_error(ERR_LIB_ASN1,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define CONFerr(f,r) ERR_PUT_error(ERR_LIB_CONF,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define CRYPTOerr(f,r) ERR_PUT_error(ERR_LIB_CRYPTO,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define ECerr(f,r)   ERR_PUT_error(ERR_LIB_EC,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define SSLerr(f,r)  ERR_PUT_error(ERR_LIB_SSL,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define BIOerr(f,r)  ERR_PUT_error(ERR_LIB_BIO,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define PKCS7err(f,r) ERR_PUT_error(ERR_LIB_PKCS7,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define X509V3err(f,r) ERR_PUT_error(ERR_LIB_X509V3,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define PKCS12err(f,r) ERR_PUT_error(ERR_LIB_PKCS12,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define RANDerr(f,r) ERR_PUT_error(ERR_LIB_RAND,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define DSOerr(f,r) ERR_PUT_error(ERR_LIB_DSO,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define ENGINEerr(f,r) ERR_PUT_error(ERR_LIB_ENGINE,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define OCSPerr(f,r) ERR_PUT_error(ERR_LIB_OCSP,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define UIerr(f,r) ERR_PUT_error(ERR_LIB_UI,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define COMPerr(f,r) ERR_PUT_error(ERR_LIB_COMP,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define ECDSAerr(f,r)  ERR_PUT_error(ERR_LIB_ECDSA,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define ECDHerr(f,r)  ERR_PUT_error(ERR_LIB_ECDH,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define OSSL_STOREerr(f,r) ERR_PUT_error(ERR_LIB_OSSL_STORE,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define FIPSerr(f,r) ERR_PUT_error(ERR_LIB_FIPS,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define CMSerr(f,r) ERR_PUT_error(ERR_LIB_CMS,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define TSerr(f,r) ERR_PUT_error(ERR_LIB_TS,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define HMACerr(f,r) ERR_PUT_error(ERR_LIB_HMAC,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define CTerr(f,r) ERR_PUT_error(ERR_LIB_CT,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define ASYNCerr(f,r) ERR_PUT_error(ERR_LIB_ASYNC,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define KDFerr(f,r) ERR_PUT_error(ERR_LIB_KDF,(f),(r),OPENSSL_FILE,OPENSSL_LINE)
# define SM2err(f,r) ERR_PUT_error(ERR_LIB_SM2,(f),(r),OPENSSL_FILE,OPENSSL_LINE)

# define ERR_PACK(l,f,r) ( \
        (((unsigned int)(l) & 0x0FF) << 24L) | \
        (((unsigned int)(f) & 0xFFF) << 12L) | \
        (((unsigned int)(r) & 0xFFF)       ) )
# define ERR_GET_LIB(l)          (int)(((l) >> 24L) & 0x0FFL)
# define ERR_GET_FUNC(l)         (int)(((l) >> 12L) & 0xFFFL)
# define ERR_GET_REASON(l)       (int)( (l)         & 0xFFFL)
# define ERR_FATAL_ERROR(l)      (int)( (l)         & ERR_R_FATAL)

/* OS functions */
# define SYS_F_FOPEN             1
# define SYS_F_CONNECT           2
# define SYS_F_GETSERVBYNAME     3
# define SYS_F_SOCKET            4
# define SYS_F_IOCTLSOCKET       5
# define SYS_F_BIND              6
# define SYS_F_LISTEN            7
# define SYS_F_ACCEPT            8
# define SYS_F_WSASTARTUP        9/* Winsock stuff */
# define SYS_F_OPENDIR           10
# define SYS_F_FREAD             11
# define SYS_F_GETADDRINFO       12
# define SYS_F_GETNAMEINFO       13
# define SYS_F_SETSOCKOPT        14
# define SYS_F_GETSOCKOPT        15
# define SYS_F_GETSOCKNAME       16
# define SYS_F_GETHOSTBYNAME     17
# define SYS_F_FFLUSH            18
# define SYS_F_OPEN              19
# define SYS_F_CLOSE             20
# define SYS_F_IOCTL             21
# define SYS_F_STAT              22
# define SYS_F_FCNTL             23
# define SYS_F_FSTAT             24

/* reasons */
# define ERR_R_SYS_LIB   ERR_LIB_SYS/* 2 */
# define ERR_R_BN_LIB    ERR_LIB_BN/* 3 */
# define ERR_R_RSA_LIB   ERR_LIB_RSA/* 4 */
# define ERR_R_DH_LIB    ERR_LIB_DH/* 5 */
# define ERR_R_EVP_LIB   ERR_LIB_EVP/* 6 */
# define ERR_R_BUF_LIB   ERR_LIB_BUF/* 7 */
# define ERR_R_OBJ_LIB   ERR_LIB_OBJ/* 8 */
# define ERR_R_PEM_LIB   ERR_LIB_PEM/* 9 */
# define ERR_R_DSA_LIB   ERR_LIB_DSA/* 10 */
# define ERR_R_X509_LIB  ERR_LIB_X509/* 11 */
# define ERR_R_ASN1_LIB  ERR_LIB_ASN1/* 13 */
# define ERR_R_EC_LIB    ERR_LIB_EC/* 16 */
# define ERR_R_BIO_LIB   ERR_LIB_BIO/* 32 */
# define ERR_R_PKCS7_LIB ERR_LIB_PKCS7/* 33 */
# define ERR_R_X509V3_LIB ERR_LIB_X509V3/* 34 */
# define ERR_R_ENGINE_LIB ERR_LIB_ENGINE/* 38 */
# define ERR_R_UI_LIB    ERR_LIB_UI/* 40 */
# define ERR_R_ECDSA_LIB ERR_LIB_ECDSA/* 42 */
# define ERR_R_OSSL_STORE_LIB ERR_LIB_OSSL_STORE/* 44 */

# define ERR_R_NESTED_ASN1_ERROR                 58
# define ERR_R_MISSING_ASN1_EOS                  63

/* fatal error */
# define ERR_R_FATAL                             64
# define ERR_R_MALLOC_FAILURE                    (1|ERR_R_FATAL)
# define ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED       (2|ERR_R_FATAL)
# define ERR_R_PASSED_NULL_PARAMETER             (3|ERR_R_FATAL)
# define ERR_R_INTERNAL_ERROR                    (4|ERR_R_FATAL)
# define ERR_R_DISABLED                          (5|ERR_R_FATAL)
# define ERR_R_INIT_FAIL                         (6|ERR_R_FATAL)
# define ERR_R_PASSED_INVALID_ARGUMENT           (7)
# define ERR_R_OPERATION_FAIL                    (8|ERR_R_FATAL)

/*
 * 99 is the maximum possible ERR_R_... code, higher values are reserved for
 * the individual libraries
 */

typedef struct ERR_string_data_st {
    unsigned long error;
    const char *string;
} ERR_STRING_DATA;

DEFINE_LHASH_OF(ERR_STRING_DATA);

void ERR_put_error(int lib, int func, int reason, const char *file, int line);
void ERR_set_error_data(char *data, int flags);

unsigned long ERR_get_error(void);
unsigned long ERR_get_error_line(const char **file, int *line);
unsigned long ERR_get_error_line_data(const char **file, int *line,
                                      const char **data, int *flags);
unsigned long ERR_peek_error(void);
unsigned long ERR_peek_error_line(const char **file, int *line);
unsigned long ERR_peek_error_line_data(const char **file, int *line,
                                       const char **data, int *flags);
unsigned long ERR_peek_last_error(void);
unsigned long ERR_peek_last_error_line(const char **file, int *line);
unsigned long ERR_peek_last_error_line_data(const char **file, int *line,
                                            const char **data, int *flags);
void ERR_clear_error(void);
char *ERR_error_string(unsigned long e, char *buf);
void ERR_error_string_n(unsigned long e, char *buf, size_t len);
const char *ERR_lib_error_string(unsigned long e);
const char *ERR_func_error_string(unsigned long e);
const char *ERR_reason_error_string(unsigned long e);
void ERR_print_errors_cb(int (*cb) (const char *str, size_t len, void *u),
                         void *u);
# ifndef OPENSSL_NO_STDIO
void ERR_print_errors_fp(FILE *fp);
# endif
void ERR_print_errors(BIO *bp);
void ERR_add_error_data(int num, ...);
void ERR_add_error_vdata(int num, va_list args);
int ERR_load_strings(int lib, ERR_STRING_DATA *str);
int ERR_load_strings_const(const ERR_STRING_DATA *str);
int ERR_unload_strings(int lib, ERR_STRING_DATA *str);
int ERR_load_ERR_strings(void);

#if OPENSSL_API_COMPAT < 0x10100000L
# define ERR_load_crypto_strings() \
    OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL)
# define ERR_free_strings() while(0) continue
#endif

DEPRECATEDIN_1_1_0(void ERR_remove_thread_state(void *))
DEPRECATEDIN_1_0_0(void ERR_remove_state(unsigned long pid))
ERR_STATE *ERR_get_state(void);

int ERR_get_next_error_library(void);

int ERR_set_mark(void);
int ERR_pop_to_mark(void);
int ERR_clear_last_mark(void);

#ifdef  __cplusplus
}
#endif

#endif
