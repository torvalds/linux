/*
 * Copyright 2000-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_OCSP_H
# define HEADER_OCSP_H

#include <openssl/opensslconf.h>

/*
 * These definitions are outside the OPENSSL_NO_OCSP guard because although for
 * historical reasons they have OCSP_* names, they can actually be used
 * independently of OCSP. E.g. see RFC5280
 */
/*-
 *   CRLReason ::= ENUMERATED {
 *        unspecified             (0),
 *        keyCompromise           (1),
 *        cACompromise            (2),
 *        affiliationChanged      (3),
 *        superseded              (4),
 *        cessationOfOperation    (5),
 *        certificateHold         (6),
 *        removeFromCRL           (8) }
 */
#  define OCSP_REVOKED_STATUS_NOSTATUS               -1
#  define OCSP_REVOKED_STATUS_UNSPECIFIED             0
#  define OCSP_REVOKED_STATUS_KEYCOMPROMISE           1
#  define OCSP_REVOKED_STATUS_CACOMPROMISE            2
#  define OCSP_REVOKED_STATUS_AFFILIATIONCHANGED      3
#  define OCSP_REVOKED_STATUS_SUPERSEDED              4
#  define OCSP_REVOKED_STATUS_CESSATIONOFOPERATION    5
#  define OCSP_REVOKED_STATUS_CERTIFICATEHOLD         6
#  define OCSP_REVOKED_STATUS_REMOVEFROMCRL           8


# ifndef OPENSSL_NO_OCSP

#  include <openssl/ossl_typ.h>
#  include <openssl/x509.h>
#  include <openssl/x509v3.h>
#  include <openssl/safestack.h>
#  include <openssl/ocsperr.h>

#ifdef  __cplusplus
extern "C" {
#endif

/* Various flags and values */

#  define OCSP_DEFAULT_NONCE_LENGTH       16

#  define OCSP_NOCERTS                    0x1
#  define OCSP_NOINTERN                   0x2
#  define OCSP_NOSIGS                     0x4
#  define OCSP_NOCHAIN                    0x8
#  define OCSP_NOVERIFY                   0x10
#  define OCSP_NOEXPLICIT                 0x20
#  define OCSP_NOCASIGN                   0x40
#  define OCSP_NODELEGATED                0x80
#  define OCSP_NOCHECKS                   0x100
#  define OCSP_TRUSTOTHER                 0x200
#  define OCSP_RESPID_KEY                 0x400
#  define OCSP_NOTIME                     0x800

typedef struct ocsp_cert_id_st OCSP_CERTID;

DEFINE_STACK_OF(OCSP_CERTID)

typedef struct ocsp_one_request_st OCSP_ONEREQ;

DEFINE_STACK_OF(OCSP_ONEREQ)

typedef struct ocsp_req_info_st OCSP_REQINFO;
typedef struct ocsp_signature_st OCSP_SIGNATURE;
typedef struct ocsp_request_st OCSP_REQUEST;

#  define OCSP_RESPONSE_STATUS_SUCCESSFUL           0
#  define OCSP_RESPONSE_STATUS_MALFORMEDREQUEST     1
#  define OCSP_RESPONSE_STATUS_INTERNALERROR        2
#  define OCSP_RESPONSE_STATUS_TRYLATER             3
#  define OCSP_RESPONSE_STATUS_SIGREQUIRED          5
#  define OCSP_RESPONSE_STATUS_UNAUTHORIZED         6

typedef struct ocsp_resp_bytes_st OCSP_RESPBYTES;

#  define V_OCSP_RESPID_NAME 0
#  define V_OCSP_RESPID_KEY  1

DEFINE_STACK_OF(OCSP_RESPID)

typedef struct ocsp_revoked_info_st OCSP_REVOKEDINFO;

#  define V_OCSP_CERTSTATUS_GOOD    0
#  define V_OCSP_CERTSTATUS_REVOKED 1
#  define V_OCSP_CERTSTATUS_UNKNOWN 2

typedef struct ocsp_cert_status_st OCSP_CERTSTATUS;
typedef struct ocsp_single_response_st OCSP_SINGLERESP;

DEFINE_STACK_OF(OCSP_SINGLERESP)

typedef struct ocsp_response_data_st OCSP_RESPDATA;

typedef struct ocsp_basic_response_st OCSP_BASICRESP;

typedef struct ocsp_crl_id_st OCSP_CRLID;
typedef struct ocsp_service_locator_st OCSP_SERVICELOC;

#  define PEM_STRING_OCSP_REQUEST "OCSP REQUEST"
#  define PEM_STRING_OCSP_RESPONSE "OCSP RESPONSE"

#  define d2i_OCSP_REQUEST_bio(bp,p) ASN1_d2i_bio_of(OCSP_REQUEST,OCSP_REQUEST_new,d2i_OCSP_REQUEST,bp,p)

#  define d2i_OCSP_RESPONSE_bio(bp,p) ASN1_d2i_bio_of(OCSP_RESPONSE,OCSP_RESPONSE_new,d2i_OCSP_RESPONSE,bp,p)

#  define PEM_read_bio_OCSP_REQUEST(bp,x,cb) (OCSP_REQUEST *)PEM_ASN1_read_bio( \
     (char *(*)())d2i_OCSP_REQUEST,PEM_STRING_OCSP_REQUEST, \
     bp,(char **)(x),cb,NULL)

#  define PEM_read_bio_OCSP_RESPONSE(bp,x,cb)(OCSP_RESPONSE *)PEM_ASN1_read_bio(\
     (char *(*)())d2i_OCSP_RESPONSE,PEM_STRING_OCSP_RESPONSE, \
     bp,(char **)(x),cb,NULL)

#  define PEM_write_bio_OCSP_REQUEST(bp,o) \
    PEM_ASN1_write_bio((int (*)())i2d_OCSP_REQUEST,PEM_STRING_OCSP_REQUEST,\
                        bp,(char *)(o), NULL,NULL,0,NULL,NULL)

#  define PEM_write_bio_OCSP_RESPONSE(bp,o) \
    PEM_ASN1_write_bio((int (*)())i2d_OCSP_RESPONSE,PEM_STRING_OCSP_RESPONSE,\
                        bp,(char *)(o), NULL,NULL,0,NULL,NULL)

#  define i2d_OCSP_RESPONSE_bio(bp,o) ASN1_i2d_bio_of(OCSP_RESPONSE,i2d_OCSP_RESPONSE,bp,o)

#  define i2d_OCSP_REQUEST_bio(bp,o) ASN1_i2d_bio_of(OCSP_REQUEST,i2d_OCSP_REQUEST,bp,o)

#  define ASN1_BIT_STRING_digest(data,type,md,len) \
        ASN1_item_digest(ASN1_ITEM_rptr(ASN1_BIT_STRING),type,data,md,len)

#  define OCSP_CERTSTATUS_dup(cs)\
                (OCSP_CERTSTATUS*)ASN1_dup((int(*)())i2d_OCSP_CERTSTATUS,\
                (char *(*)())d2i_OCSP_CERTSTATUS,(char *)(cs))

OCSP_CERTID *OCSP_CERTID_dup(OCSP_CERTID *id);

OCSP_RESPONSE *OCSP_sendreq_bio(BIO *b, const char *path, OCSP_REQUEST *req);
OCSP_REQ_CTX *OCSP_sendreq_new(BIO *io, const char *path, OCSP_REQUEST *req,
                               int maxline);
int OCSP_REQ_CTX_nbio(OCSP_REQ_CTX *rctx);
int OCSP_sendreq_nbio(OCSP_RESPONSE **presp, OCSP_REQ_CTX *rctx);
OCSP_REQ_CTX *OCSP_REQ_CTX_new(BIO *io, int maxline);
void OCSP_REQ_CTX_free(OCSP_REQ_CTX *rctx);
void OCSP_set_max_response_length(OCSP_REQ_CTX *rctx, unsigned long len);
int OCSP_REQ_CTX_i2d(OCSP_REQ_CTX *rctx, const ASN1_ITEM *it,
                     ASN1_VALUE *val);
int OCSP_REQ_CTX_nbio_d2i(OCSP_REQ_CTX *rctx, ASN1_VALUE **pval,
                          const ASN1_ITEM *it);
BIO *OCSP_REQ_CTX_get0_mem_bio(OCSP_REQ_CTX *rctx);
int OCSP_REQ_CTX_http(OCSP_REQ_CTX *rctx, const char *op, const char *path);
int OCSP_REQ_CTX_set1_req(OCSP_REQ_CTX *rctx, OCSP_REQUEST *req);
int OCSP_REQ_CTX_add1_header(OCSP_REQ_CTX *rctx,
                             const char *name, const char *value);

OCSP_CERTID *OCSP_cert_to_id(const EVP_MD *dgst, const X509 *subject,
                             const X509 *issuer);

OCSP_CERTID *OCSP_cert_id_new(const EVP_MD *dgst,
                              const X509_NAME *issuerName,
                              const ASN1_BIT_STRING *issuerKey,
                              const ASN1_INTEGER *serialNumber);

OCSP_ONEREQ *OCSP_request_add0_id(OCSP_REQUEST *req, OCSP_CERTID *cid);

int OCSP_request_add1_nonce(OCSP_REQUEST *req, unsigned char *val, int len);
int OCSP_basic_add1_nonce(OCSP_BASICRESP *resp, unsigned char *val, int len);
int OCSP_check_nonce(OCSP_REQUEST *req, OCSP_BASICRESP *bs);
int OCSP_copy_nonce(OCSP_BASICRESP *resp, OCSP_REQUEST *req);

int OCSP_request_set1_name(OCSP_REQUEST *req, X509_NAME *nm);
int OCSP_request_add1_cert(OCSP_REQUEST *req, X509 *cert);

int OCSP_request_sign(OCSP_REQUEST *req,
                      X509 *signer,
                      EVP_PKEY *key,
                      const EVP_MD *dgst,
                      STACK_OF(X509) *certs, unsigned long flags);

int OCSP_response_status(OCSP_RESPONSE *resp);
OCSP_BASICRESP *OCSP_response_get1_basic(OCSP_RESPONSE *resp);

const ASN1_OCTET_STRING *OCSP_resp_get0_signature(const OCSP_BASICRESP *bs);
const X509_ALGOR *OCSP_resp_get0_tbs_sigalg(const OCSP_BASICRESP *bs);
const OCSP_RESPDATA *OCSP_resp_get0_respdata(const OCSP_BASICRESP *bs);
int OCSP_resp_get0_signer(OCSP_BASICRESP *bs, X509 **signer,
                          STACK_OF(X509) *extra_certs);

int OCSP_resp_count(OCSP_BASICRESP *bs);
OCSP_SINGLERESP *OCSP_resp_get0(OCSP_BASICRESP *bs, int idx);
const ASN1_GENERALIZEDTIME *OCSP_resp_get0_produced_at(const OCSP_BASICRESP* bs);
const STACK_OF(X509) *OCSP_resp_get0_certs(const OCSP_BASICRESP *bs);
int OCSP_resp_get0_id(const OCSP_BASICRESP *bs,
                      const ASN1_OCTET_STRING **pid,
                      const X509_NAME **pname);
int OCSP_resp_get1_id(const OCSP_BASICRESP *bs,
                      ASN1_OCTET_STRING **pid,
                      X509_NAME **pname);

int OCSP_resp_find(OCSP_BASICRESP *bs, OCSP_CERTID *id, int last);
int OCSP_single_get0_status(OCSP_SINGLERESP *single, int *reason,
                            ASN1_GENERALIZEDTIME **revtime,
                            ASN1_GENERALIZEDTIME **thisupd,
                            ASN1_GENERALIZEDTIME **nextupd);
int OCSP_resp_find_status(OCSP_BASICRESP *bs, OCSP_CERTID *id, int *status,
                          int *reason,
                          ASN1_GENERALIZEDTIME **revtime,
                          ASN1_GENERALIZEDTIME **thisupd,
                          ASN1_GENERALIZEDTIME **nextupd);
int OCSP_check_validity(ASN1_GENERALIZEDTIME *thisupd,
                        ASN1_GENERALIZEDTIME *nextupd, long sec, long maxsec);

int OCSP_request_verify(OCSP_REQUEST *req, STACK_OF(X509) *certs,
                        X509_STORE *store, unsigned long flags);

int OCSP_parse_url(const char *url, char **phost, char **pport, char **ppath,
                   int *pssl);

int OCSP_id_issuer_cmp(OCSP_CERTID *a, OCSP_CERTID *b);
int OCSP_id_cmp(OCSP_CERTID *a, OCSP_CERTID *b);

int OCSP_request_onereq_count(OCSP_REQUEST *req);
OCSP_ONEREQ *OCSP_request_onereq_get0(OCSP_REQUEST *req, int i);
OCSP_CERTID *OCSP_onereq_get0_id(OCSP_ONEREQ *one);
int OCSP_id_get0_info(ASN1_OCTET_STRING **piNameHash, ASN1_OBJECT **pmd,
                      ASN1_OCTET_STRING **pikeyHash,
                      ASN1_INTEGER **pserial, OCSP_CERTID *cid);
int OCSP_request_is_signed(OCSP_REQUEST *req);
OCSP_RESPONSE *OCSP_response_create(int status, OCSP_BASICRESP *bs);
OCSP_SINGLERESP *OCSP_basic_add1_status(OCSP_BASICRESP *rsp,
                                        OCSP_CERTID *cid,
                                        int status, int reason,
                                        ASN1_TIME *revtime,
                                        ASN1_TIME *thisupd,
                                        ASN1_TIME *nextupd);
int OCSP_basic_add1_cert(OCSP_BASICRESP *resp, X509 *cert);
int OCSP_basic_sign(OCSP_BASICRESP *brsp,
                    X509 *signer, EVP_PKEY *key, const EVP_MD *dgst,
                    STACK_OF(X509) *certs, unsigned long flags);
int OCSP_basic_sign_ctx(OCSP_BASICRESP *brsp,
                        X509 *signer, EVP_MD_CTX *ctx,
                        STACK_OF(X509) *certs, unsigned long flags);
int OCSP_RESPID_set_by_name(OCSP_RESPID *respid, X509 *cert);
int OCSP_RESPID_set_by_key(OCSP_RESPID *respid, X509 *cert);
int OCSP_RESPID_match(OCSP_RESPID *respid, X509 *cert);

X509_EXTENSION *OCSP_crlID_new(const char *url, long *n, char *tim);

X509_EXTENSION *OCSP_accept_responses_new(char **oids);

X509_EXTENSION *OCSP_archive_cutoff_new(char *tim);

X509_EXTENSION *OCSP_url_svcloc_new(X509_NAME *issuer, const char **urls);

int OCSP_REQUEST_get_ext_count(OCSP_REQUEST *x);
int OCSP_REQUEST_get_ext_by_NID(OCSP_REQUEST *x, int nid, int lastpos);
int OCSP_REQUEST_get_ext_by_OBJ(OCSP_REQUEST *x, const ASN1_OBJECT *obj,
                                int lastpos);
int OCSP_REQUEST_get_ext_by_critical(OCSP_REQUEST *x, int crit, int lastpos);
X509_EXTENSION *OCSP_REQUEST_get_ext(OCSP_REQUEST *x, int loc);
X509_EXTENSION *OCSP_REQUEST_delete_ext(OCSP_REQUEST *x, int loc);
void *OCSP_REQUEST_get1_ext_d2i(OCSP_REQUEST *x, int nid, int *crit,
                                int *idx);
int OCSP_REQUEST_add1_ext_i2d(OCSP_REQUEST *x, int nid, void *value, int crit,
                              unsigned long flags);
int OCSP_REQUEST_add_ext(OCSP_REQUEST *x, X509_EXTENSION *ex, int loc);

int OCSP_ONEREQ_get_ext_count(OCSP_ONEREQ *x);
int OCSP_ONEREQ_get_ext_by_NID(OCSP_ONEREQ *x, int nid, int lastpos);
int OCSP_ONEREQ_get_ext_by_OBJ(OCSP_ONEREQ *x, const ASN1_OBJECT *obj, int lastpos);
int OCSP_ONEREQ_get_ext_by_critical(OCSP_ONEREQ *x, int crit, int lastpos);
X509_EXTENSION *OCSP_ONEREQ_get_ext(OCSP_ONEREQ *x, int loc);
X509_EXTENSION *OCSP_ONEREQ_delete_ext(OCSP_ONEREQ *x, int loc);
void *OCSP_ONEREQ_get1_ext_d2i(OCSP_ONEREQ *x, int nid, int *crit, int *idx);
int OCSP_ONEREQ_add1_ext_i2d(OCSP_ONEREQ *x, int nid, void *value, int crit,
                             unsigned long flags);
int OCSP_ONEREQ_add_ext(OCSP_ONEREQ *x, X509_EXTENSION *ex, int loc);

int OCSP_BASICRESP_get_ext_count(OCSP_BASICRESP *x);
int OCSP_BASICRESP_get_ext_by_NID(OCSP_BASICRESP *x, int nid, int lastpos);
int OCSP_BASICRESP_get_ext_by_OBJ(OCSP_BASICRESP *x, const ASN1_OBJECT *obj,
                                  int lastpos);
int OCSP_BASICRESP_get_ext_by_critical(OCSP_BASICRESP *x, int crit,
                                       int lastpos);
X509_EXTENSION *OCSP_BASICRESP_get_ext(OCSP_BASICRESP *x, int loc);
X509_EXTENSION *OCSP_BASICRESP_delete_ext(OCSP_BASICRESP *x, int loc);
void *OCSP_BASICRESP_get1_ext_d2i(OCSP_BASICRESP *x, int nid, int *crit,
                                  int *idx);
int OCSP_BASICRESP_add1_ext_i2d(OCSP_BASICRESP *x, int nid, void *value,
                                int crit, unsigned long flags);
int OCSP_BASICRESP_add_ext(OCSP_BASICRESP *x, X509_EXTENSION *ex, int loc);

int OCSP_SINGLERESP_get_ext_count(OCSP_SINGLERESP *x);
int OCSP_SINGLERESP_get_ext_by_NID(OCSP_SINGLERESP *x, int nid, int lastpos);
int OCSP_SINGLERESP_get_ext_by_OBJ(OCSP_SINGLERESP *x, const ASN1_OBJECT *obj,
                                   int lastpos);
int OCSP_SINGLERESP_get_ext_by_critical(OCSP_SINGLERESP *x, int crit,
                                        int lastpos);
X509_EXTENSION *OCSP_SINGLERESP_get_ext(OCSP_SINGLERESP *x, int loc);
X509_EXTENSION *OCSP_SINGLERESP_delete_ext(OCSP_SINGLERESP *x, int loc);
void *OCSP_SINGLERESP_get1_ext_d2i(OCSP_SINGLERESP *x, int nid, int *crit,
                                   int *idx);
int OCSP_SINGLERESP_add1_ext_i2d(OCSP_SINGLERESP *x, int nid, void *value,
                                 int crit, unsigned long flags);
int OCSP_SINGLERESP_add_ext(OCSP_SINGLERESP *x, X509_EXTENSION *ex, int loc);
const OCSP_CERTID *OCSP_SINGLERESP_get0_id(const OCSP_SINGLERESP *x);

DECLARE_ASN1_FUNCTIONS(OCSP_SINGLERESP)
DECLARE_ASN1_FUNCTIONS(OCSP_CERTSTATUS)
DECLARE_ASN1_FUNCTIONS(OCSP_REVOKEDINFO)
DECLARE_ASN1_FUNCTIONS(OCSP_BASICRESP)
DECLARE_ASN1_FUNCTIONS(OCSP_RESPDATA)
DECLARE_ASN1_FUNCTIONS(OCSP_RESPID)
DECLARE_ASN1_FUNCTIONS(OCSP_RESPONSE)
DECLARE_ASN1_FUNCTIONS(OCSP_RESPBYTES)
DECLARE_ASN1_FUNCTIONS(OCSP_ONEREQ)
DECLARE_ASN1_FUNCTIONS(OCSP_CERTID)
DECLARE_ASN1_FUNCTIONS(OCSP_REQUEST)
DECLARE_ASN1_FUNCTIONS(OCSP_SIGNATURE)
DECLARE_ASN1_FUNCTIONS(OCSP_REQINFO)
DECLARE_ASN1_FUNCTIONS(OCSP_CRLID)
DECLARE_ASN1_FUNCTIONS(OCSP_SERVICELOC)

const char *OCSP_response_status_str(long s);
const char *OCSP_cert_status_str(long s);
const char *OCSP_crl_reason_str(long s);

int OCSP_REQUEST_print(BIO *bp, OCSP_REQUEST *a, unsigned long flags);
int OCSP_RESPONSE_print(BIO *bp, OCSP_RESPONSE *o, unsigned long flags);

int OCSP_basic_verify(OCSP_BASICRESP *bs, STACK_OF(X509) *certs,
                      X509_STORE *st, unsigned long flags);


#  ifdef  __cplusplus
}
#  endif
# endif
#endif
