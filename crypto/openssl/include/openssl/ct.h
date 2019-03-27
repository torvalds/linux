/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_CT_H
# define HEADER_CT_H

# include <openssl/opensslconf.h>

# ifndef OPENSSL_NO_CT
# include <openssl/ossl_typ.h>
# include <openssl/safestack.h>
# include <openssl/x509.h>
# include <openssl/cterr.h>
# ifdef  __cplusplus
extern "C" {
# endif


/* Minimum RSA key size, from RFC6962 */
# define SCT_MIN_RSA_BITS 2048

/* All hashes are SHA256 in v1 of Certificate Transparency */
# define CT_V1_HASHLEN SHA256_DIGEST_LENGTH

typedef enum {
    CT_LOG_ENTRY_TYPE_NOT_SET = -1,
    CT_LOG_ENTRY_TYPE_X509 = 0,
    CT_LOG_ENTRY_TYPE_PRECERT = 1
} ct_log_entry_type_t;

typedef enum {
    SCT_VERSION_NOT_SET = -1,
    SCT_VERSION_V1 = 0
} sct_version_t;

typedef enum {
    SCT_SOURCE_UNKNOWN,
    SCT_SOURCE_TLS_EXTENSION,
    SCT_SOURCE_X509V3_EXTENSION,
    SCT_SOURCE_OCSP_STAPLED_RESPONSE
} sct_source_t;

typedef enum {
    SCT_VALIDATION_STATUS_NOT_SET,
    SCT_VALIDATION_STATUS_UNKNOWN_LOG,
    SCT_VALIDATION_STATUS_VALID,
    SCT_VALIDATION_STATUS_INVALID,
    SCT_VALIDATION_STATUS_UNVERIFIED,
    SCT_VALIDATION_STATUS_UNKNOWN_VERSION
} sct_validation_status_t;

DEFINE_STACK_OF(SCT)
DEFINE_STACK_OF(CTLOG)

/******************************************
 * CT policy evaluation context functions *
 ******************************************/

/*
 * Creates a new, empty policy evaluation context.
 * The caller is responsible for calling CT_POLICY_EVAL_CTX_free when finished
 * with the CT_POLICY_EVAL_CTX.
 */
CT_POLICY_EVAL_CTX *CT_POLICY_EVAL_CTX_new(void);

/* Deletes a policy evaluation context and anything it owns. */
void CT_POLICY_EVAL_CTX_free(CT_POLICY_EVAL_CTX *ctx);

/* Gets the peer certificate that the SCTs are for */
X509* CT_POLICY_EVAL_CTX_get0_cert(const CT_POLICY_EVAL_CTX *ctx);

/*
 * Sets the certificate associated with the received SCTs.
 * Increments the reference count of cert.
 * Returns 1 on success, 0 otherwise.
 */
int CT_POLICY_EVAL_CTX_set1_cert(CT_POLICY_EVAL_CTX *ctx, X509 *cert);

/* Gets the issuer of the aforementioned certificate */
X509* CT_POLICY_EVAL_CTX_get0_issuer(const CT_POLICY_EVAL_CTX *ctx);

/*
 * Sets the issuer of the certificate associated with the received SCTs.
 * Increments the reference count of issuer.
 * Returns 1 on success, 0 otherwise.
 */
int CT_POLICY_EVAL_CTX_set1_issuer(CT_POLICY_EVAL_CTX *ctx, X509 *issuer);

/* Gets the CT logs that are trusted sources of SCTs */
const CTLOG_STORE *CT_POLICY_EVAL_CTX_get0_log_store(const CT_POLICY_EVAL_CTX *ctx);

/* Sets the log store that is in use. It must outlive the CT_POLICY_EVAL_CTX. */
void CT_POLICY_EVAL_CTX_set_shared_CTLOG_STORE(CT_POLICY_EVAL_CTX *ctx,
                                               CTLOG_STORE *log_store);

/*
 * Gets the time, in milliseconds since the Unix epoch, that will be used as the
 * current time when checking whether an SCT was issued in the future.
 * Such SCTs will fail validation, as required by RFC6962.
 */
uint64_t CT_POLICY_EVAL_CTX_get_time(const CT_POLICY_EVAL_CTX *ctx);

/*
 * Sets the time to evaluate SCTs against, in milliseconds since the Unix epoch.
 * If an SCT's timestamp is after this time, it will be interpreted as having
 * been issued in the future. RFC6962 states that "TLS clients MUST reject SCTs
 * whose timestamp is in the future", so an SCT will not validate in this case.
 */
void CT_POLICY_EVAL_CTX_set_time(CT_POLICY_EVAL_CTX *ctx, uint64_t time_in_ms);

/*****************
 * SCT functions *
 *****************/

/*
 * Creates a new, blank SCT.
 * The caller is responsible for calling SCT_free when finished with the SCT.
 */
SCT *SCT_new(void);

/*
 * Creates a new SCT from some base64-encoded strings.
 * The caller is responsible for calling SCT_free when finished with the SCT.
 */
SCT *SCT_new_from_base64(unsigned char version,
                         const char *logid_base64,
                         ct_log_entry_type_t entry_type,
                         uint64_t timestamp,
                         const char *extensions_base64,
                         const char *signature_base64);

/*
 * Frees the SCT and the underlying data structures.
 */
void SCT_free(SCT *sct);

/*
 * Free a stack of SCTs, and the underlying SCTs themselves.
 * Intended to be compatible with X509V3_EXT_FREE.
 */
void SCT_LIST_free(STACK_OF(SCT) *a);

/*
 * Returns the version of the SCT.
 */
sct_version_t SCT_get_version(const SCT *sct);

/*
 * Set the version of an SCT.
 * Returns 1 on success, 0 if the version is unrecognized.
 */
__owur int SCT_set_version(SCT *sct, sct_version_t version);

/*
 * Returns the log entry type of the SCT.
 */
ct_log_entry_type_t SCT_get_log_entry_type(const SCT *sct);

/*
 * Set the log entry type of an SCT.
 * Returns 1 on success, 0 otherwise.
 */
__owur int SCT_set_log_entry_type(SCT *sct, ct_log_entry_type_t entry_type);

/*
 * Gets the ID of the log that an SCT came from.
 * Ownership of the log ID remains with the SCT.
 * Returns the length of the log ID.
 */
size_t SCT_get0_log_id(const SCT *sct, unsigned char **log_id);

/*
 * Set the log ID of an SCT to point directly to the *log_id specified.
 * The SCT takes ownership of the specified pointer.
 * Returns 1 on success, 0 otherwise.
 */
__owur int SCT_set0_log_id(SCT *sct, unsigned char *log_id, size_t log_id_len);

/*
 * Set the log ID of an SCT.
 * This makes a copy of the log_id.
 * Returns 1 on success, 0 otherwise.
 */
__owur int SCT_set1_log_id(SCT *sct, const unsigned char *log_id,
                           size_t log_id_len);

/*
 * Returns the timestamp for the SCT (epoch time in milliseconds).
 */
uint64_t SCT_get_timestamp(const SCT *sct);

/*
 * Set the timestamp of an SCT (epoch time in milliseconds).
 */
void SCT_set_timestamp(SCT *sct, uint64_t timestamp);

/*
 * Return the NID for the signature used by the SCT.
 * For CT v1, this will be either NID_sha256WithRSAEncryption or
 * NID_ecdsa_with_SHA256 (or NID_undef if incorrect/unset).
 */
int SCT_get_signature_nid(const SCT *sct);

/*
 * Set the signature type of an SCT
 * For CT v1, this should be either NID_sha256WithRSAEncryption or
 * NID_ecdsa_with_SHA256.
 * Returns 1 on success, 0 otherwise.
 */
__owur int SCT_set_signature_nid(SCT *sct, int nid);

/*
 * Set *ext to point to the extension data for the SCT. ext must not be NULL.
 * The SCT retains ownership of this pointer.
 * Returns length of the data pointed to.
 */
size_t SCT_get0_extensions(const SCT *sct, unsigned char **ext);

/*
 * Set the extensions of an SCT to point directly to the *ext specified.
 * The SCT takes ownership of the specified pointer.
 */
void SCT_set0_extensions(SCT *sct, unsigned char *ext, size_t ext_len);

/*
 * Set the extensions of an SCT.
 * This takes a copy of the ext.
 * Returns 1 on success, 0 otherwise.
 */
__owur int SCT_set1_extensions(SCT *sct, const unsigned char *ext,
                               size_t ext_len);

/*
 * Set *sig to point to the signature for the SCT. sig must not be NULL.
 * The SCT retains ownership of this pointer.
 * Returns length of the data pointed to.
 */
size_t SCT_get0_signature(const SCT *sct, unsigned char **sig);

/*
 * Set the signature of an SCT to point directly to the *sig specified.
 * The SCT takes ownership of the specified pointer.
 */
void SCT_set0_signature(SCT *sct, unsigned char *sig, size_t sig_len);

/*
 * Set the signature of an SCT to be a copy of the *sig specified.
 * Returns 1 on success, 0 otherwise.
 */
__owur int SCT_set1_signature(SCT *sct, const unsigned char *sig,
                              size_t sig_len);

/*
 * The origin of this SCT, e.g. TLS extension, OCSP response, etc.
 */
sct_source_t SCT_get_source(const SCT *sct);

/*
 * Set the origin of this SCT, e.g. TLS extension, OCSP response, etc.
 * Returns 1 on success, 0 otherwise.
 */
__owur int SCT_set_source(SCT *sct, sct_source_t source);

/*
 * Returns a text string describing the validation status of |sct|.
 */
const char *SCT_validation_status_string(const SCT *sct);

/*
 * Pretty-prints an |sct| to |out|.
 * It will be indented by the number of spaces specified by |indent|.
 * If |logs| is not NULL, it will be used to lookup the CT log that the SCT came
 * from, so that the log name can be printed.
 */
void SCT_print(const SCT *sct, BIO *out, int indent, const CTLOG_STORE *logs);

/*
 * Pretty-prints an |sct_list| to |out|.
 * It will be indented by the number of spaces specified by |indent|.
 * SCTs will be delimited by |separator|.
 * If |logs| is not NULL, it will be used to lookup the CT log that each SCT
 * came from, so that the log names can be printed.
 */
void SCT_LIST_print(const STACK_OF(SCT) *sct_list, BIO *out, int indent,
                    const char *separator, const CTLOG_STORE *logs);

/*
 * Gets the last result of validating this SCT.
 * If it has not been validated yet, returns SCT_VALIDATION_STATUS_NOT_SET.
 */
sct_validation_status_t SCT_get_validation_status(const SCT *sct);

/*
 * Validates the given SCT with the provided context.
 * Sets the "validation_status" field of the SCT.
 * Returns 1 if the SCT is valid and the signature verifies.
 * Returns 0 if the SCT is invalid or could not be verified.
 * Returns -1 if an error occurs.
 */
__owur int SCT_validate(SCT *sct, const CT_POLICY_EVAL_CTX *ctx);

/*
 * Validates the given list of SCTs with the provided context.
 * Sets the "validation_status" field of each SCT.
 * Returns 1 if there are no invalid SCTs and all signatures verify.
 * Returns 0 if at least one SCT is invalid or could not be verified.
 * Returns a negative integer if an error occurs.
 */
__owur int SCT_LIST_validate(const STACK_OF(SCT) *scts,
                             CT_POLICY_EVAL_CTX *ctx);


/*********************************
 * SCT parsing and serialisation *
 *********************************/

/*
 * Serialize (to TLS format) a stack of SCTs and return the length.
 * "a" must not be NULL.
 * If "pp" is NULL, just return the length of what would have been serialized.
 * If "pp" is not NULL and "*pp" is null, function will allocate a new pointer
 * for data that caller is responsible for freeing (only if function returns
 * successfully).
 * If "pp" is NULL and "*pp" is not NULL, caller is responsible for ensuring
 * that "*pp" is large enough to accept all of the serialized data.
 * Returns < 0 on error, >= 0 indicating bytes written (or would have been)
 * on success.
 */
__owur int i2o_SCT_LIST(const STACK_OF(SCT) *a, unsigned char **pp);

/*
 * Convert TLS format SCT list to a stack of SCTs.
 * If "a" or "*a" is NULL, a new stack will be created that the caller is
 * responsible for freeing (by calling SCT_LIST_free).
 * "**pp" and "*pp" must not be NULL.
 * Upon success, "*pp" will point to after the last bytes read, and a stack
 * will be returned.
 * Upon failure, a NULL pointer will be returned, and the position of "*pp" is
 * not defined.
 */
STACK_OF(SCT) *o2i_SCT_LIST(STACK_OF(SCT) **a, const unsigned char **pp,
                            size_t len);

/*
 * Serialize (to DER format) a stack of SCTs and return the length.
 * "a" must not be NULL.
 * If "pp" is NULL, just returns the length of what would have been serialized.
 * If "pp" is not NULL and "*pp" is null, function will allocate a new pointer
 * for data that caller is responsible for freeing (only if function returns
 * successfully).
 * If "pp" is NULL and "*pp" is not NULL, caller is responsible for ensuring
 * that "*pp" is large enough to accept all of the serialized data.
 * Returns < 0 on error, >= 0 indicating bytes written (or would have been)
 * on success.
 */
__owur int i2d_SCT_LIST(const STACK_OF(SCT) *a, unsigned char **pp);

/*
 * Parses an SCT list in DER format and returns it.
 * If "a" or "*a" is NULL, a new stack will be created that the caller is
 * responsible for freeing (by calling SCT_LIST_free).
 * "**pp" and "*pp" must not be NULL.
 * Upon success, "*pp" will point to after the last bytes read, and a stack
 * will be returned.
 * Upon failure, a NULL pointer will be returned, and the position of "*pp" is
 * not defined.
 */
STACK_OF(SCT) *d2i_SCT_LIST(STACK_OF(SCT) **a, const unsigned char **pp,
                            long len);

/*
 * Serialize (to TLS format) an |sct| and write it to |out|.
 * If |out| is null, no SCT will be output but the length will still be returned.
 * If |out| points to a null pointer, a string will be allocated to hold the
 * TLS-format SCT. It is the responsibility of the caller to free it.
 * If |out| points to an allocated string, the TLS-format SCT will be written
 * to it.
 * The length of the SCT in TLS format will be returned.
 */
__owur int i2o_SCT(const SCT *sct, unsigned char **out);

/*
 * Parses an SCT in TLS format and returns it.
 * If |psct| is not null, it will end up pointing to the parsed SCT. If it
 * already points to a non-null pointer, the pointer will be free'd.
 * |in| should be a pointer to a string containing the TLS-format SCT.
 * |in| will be advanced to the end of the SCT if parsing succeeds.
 * |len| should be the length of the SCT in |in|.
 * Returns NULL if an error occurs.
 * If the SCT is an unsupported version, only the SCT's 'sct' and 'sct_len'
 * fields will be populated (with |in| and |len| respectively).
 */
SCT *o2i_SCT(SCT **psct, const unsigned char **in, size_t len);

/********************
 * CT log functions *
 ********************/

/*
 * Creates a new CT log instance with the given |public_key| and |name|.
 * Takes ownership of |public_key| but copies |name|.
 * Returns NULL if malloc fails or if |public_key| cannot be converted to DER.
 * Should be deleted by the caller using CTLOG_free when no longer needed.
 */
CTLOG *CTLOG_new(EVP_PKEY *public_key, const char *name);

/*
 * Creates a new CTLOG instance with the base64-encoded SubjectPublicKeyInfo DER
 * in |pkey_base64|. The |name| is a string to help users identify this log.
 * Returns 1 on success, 0 on failure.
 * Should be deleted by the caller using CTLOG_free when no longer needed.
 */
int CTLOG_new_from_base64(CTLOG ** ct_log,
                          const char *pkey_base64, const char *name);

/*
 * Deletes a CT log instance and its fields.
 */
void CTLOG_free(CTLOG *log);

/* Gets the name of the CT log */
const char *CTLOG_get0_name(const CTLOG *log);
/* Gets the ID of the CT log */
void CTLOG_get0_log_id(const CTLOG *log, const uint8_t **log_id,
                       size_t *log_id_len);
/* Gets the public key of the CT log */
EVP_PKEY *CTLOG_get0_public_key(const CTLOG *log);

/**************************
 * CT log store functions *
 **************************/

/*
 * Creates a new CT log store.
 * Should be deleted by the caller using CTLOG_STORE_free when no longer needed.
 */
CTLOG_STORE *CTLOG_STORE_new(void);

/*
 * Deletes a CT log store and all of the CT log instances held within.
 */
void CTLOG_STORE_free(CTLOG_STORE *store);

/*
 * Finds a CT log in the store based on its log ID.
 * Returns the CT log, or NULL if no match is found.
 */
const CTLOG *CTLOG_STORE_get0_log_by_id(const CTLOG_STORE *store,
                                        const uint8_t *log_id,
                                        size_t log_id_len);

/*
 * Loads a CT log list into a |store| from a |file|.
 * Returns 1 if loading is successful, or 0 otherwise.
 */
__owur int CTLOG_STORE_load_file(CTLOG_STORE *store, const char *file);

/*
 * Loads the default CT log list into a |store|.
 * See internal/cryptlib.h for the environment variable and file path that are
 * consulted to find the default file.
 * Returns 1 if loading is successful, or 0 otherwise.
 */
__owur int CTLOG_STORE_load_default_file(CTLOG_STORE *store);

#  ifdef  __cplusplus
}
#  endif
# endif
#endif
