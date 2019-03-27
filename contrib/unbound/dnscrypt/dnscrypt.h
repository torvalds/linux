#ifndef UNBOUND_DNSCRYPT_H
#define UNBOUND_DNSCRYPT_H

/**
 * \file
 * dnscrypt functions for encrypting DNS packets.
 */

#include "dnscrypt/dnscrypt_config.h"
#ifdef USE_DNSCRYPT

#define DNSCRYPT_MAGIC_HEADER_LEN 8U
#define DNSCRYPT_MAGIC_RESPONSE  "r6fnvWj8"

#ifndef DNSCRYPT_MAX_PADDING
# define DNSCRYPT_MAX_PADDING 256U
#endif
#ifndef DNSCRYPT_BLOCK_SIZE
# define DNSCRYPT_BLOCK_SIZE 64U
#endif
#ifndef DNSCRYPT_MIN_PAD_LEN
# define DNSCRYPT_MIN_PAD_LEN 8U
#endif

#define crypto_box_HALF_NONCEBYTES (crypto_box_NONCEBYTES / 2U)

#include "config.h"
#include "dnscrypt/cert.h"
#include "util/locks.h"

#define DNSCRYPT_QUERY_HEADER_SIZE \
    (DNSCRYPT_MAGIC_HEADER_LEN + crypto_box_PUBLICKEYBYTES + crypto_box_HALF_NONCEBYTES + crypto_box_MACBYTES)
#define DNSCRYPT_RESPONSE_HEADER_SIZE \
    (DNSCRYPT_MAGIC_HEADER_LEN + crypto_box_NONCEBYTES + crypto_box_MACBYTES)

#define DNSCRYPT_REPLY_HEADER_SIZE \
    (DNSCRYPT_MAGIC_HEADER_LEN + crypto_box_HALF_NONCEBYTES * 2 + crypto_box_MACBYTES)

struct sldns_buffer;
struct config_file;
struct comm_reply;
struct slabhash;

typedef struct KeyPair_ {
    uint8_t crypt_publickey[crypto_box_PUBLICKEYBYTES];
    uint8_t crypt_secretkey[crypto_box_SECRETKEYBYTES];
} KeyPair;

typedef struct cert_ {
    uint8_t magic_query[DNSCRYPT_MAGIC_HEADER_LEN];
    uint8_t es_version[2];
    KeyPair *keypair;
} dnsccert;

struct dnsc_env {
	struct SignedCert *signed_certs;
	struct SignedCert **rotated_certs;
	dnsccert *certs;
	size_t signed_certs_count;
	size_t rotated_certs_count;
	uint8_t provider_publickey[crypto_sign_ed25519_PUBLICKEYBYTES];
	uint8_t provider_secretkey[crypto_sign_ed25519_SECRETKEYBYTES];
	KeyPair *keypairs;
	size_t keypairs_count;
	uint64_t nonce_ts_last;
	unsigned char hash_key[crypto_shorthash_KEYBYTES];
	char * provider_name;

    /** Caches */
	struct slabhash *shared_secrets_cache;
	/** lock on shared secret cache counters */
	lock_basic_type shared_secrets_cache_lock;
	/** number of misses from shared_secrets_cache */
	size_t num_query_dnscrypt_secret_missed_cache;

	/** slabhash keeping track of nonce/cient pk/server sk pairs. */
	struct slabhash *nonces_cache;
	/** lock on nonces_cache, used to avoid race condition in updating the hash */
	lock_basic_type nonces_cache_lock;
	/** number of replayed queries */
	size_t num_query_dnscrypt_replay;
};

struct dnscrypt_query_header {
    uint8_t magic_query[DNSCRYPT_MAGIC_HEADER_LEN];
    uint8_t publickey[crypto_box_PUBLICKEYBYTES];
    uint8_t nonce[crypto_box_HALF_NONCEBYTES];
    uint8_t mac[crypto_box_MACBYTES];
};

/**
 * Initialize DNSCrypt environment.
 * Initialize sodium library and allocate the dnsc_env structure.
 * \return an uninitialized struct dnsc_env.
 */
struct dnsc_env * dnsc_create(void);

/**
 * Apply configuration.
 * Read certificates and secret keys from configuration. Initialize hashkey and
 * provider name as well as loading cert TXT records.
 * In case of issue applying configuration, this function fatals.
 * \param[in] env the struct dnsc_env to populate.
 * \param[in] cfg the config_file struct with dnscrypt options.
 * \return 0 on success.
 */
int dnsc_apply_cfg(struct dnsc_env *env, struct config_file *cfg);

/**
 * Delete DNSCrypt environment
 *
 */
void dnsc_delete(struct dnsc_env *env);

/**
 * handle a crypted dnscrypt request.
 * Determine wether or not a query is coming over the dnscrypt listener and
 * attempt to uncurve it or detect if it is a certificate query.
 * return 0 in case of failure.
 */
int dnsc_handle_curved_request(struct dnsc_env* dnscenv,
                               struct comm_reply* repinfo);
/**
 * handle an unencrypted dnscrypt request.
 * Determine wether or not a query is going over the dnscrypt channel and
 * attempt to curve it unless it was not crypted like when  it is a
 * certificate query.
 * \return 0 in case of failure.
 */

int dnsc_handle_uncurved_request(struct comm_reply *repinfo);

/**
 * Computes the size of the shared secret cache entry.
 */
size_t dnsc_shared_secrets_sizefunc(void *k, void *d);

/**
 * Compares two shared secret cache keys.
 */
int dnsc_shared_secrets_compfunc(void *m1, void *m2);

/**
 * Function to delete a shared secret cache key.
 */
void dnsc_shared_secrets_delkeyfunc(void *k, void* arg);

/**
 * Function to delete a share secret cache value.
 */
void dnsc_shared_secrets_deldatafunc(void* d, void* arg);

/**
 * Computes the size of the nonce cache entry.
 */
size_t dnsc_nonces_sizefunc(void *k, void *d);

/**
 * Compares two nonce cache keys.
 */
int dnsc_nonces_compfunc(void *m1, void *m2);

/**
 * Function to delete a nonce cache key.
 */
void dnsc_nonces_delkeyfunc(void *k, void* arg);

/**
 * Function to delete a nonce cache value.
 */
void dnsc_nonces_deldatafunc(void* d, void* arg);


#endif /* USE_DNSCRYPT */
#endif
