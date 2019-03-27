
#include "config.h"
#include <stdlib.h>
#include <fcntl.h>
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include <inttypes.h>
#include <sys/time.h>
#include <sys/types.h>
#include "sldns/sbuffer.h"
#include "util/config_file.h"
#include "util/net_help.h"
#include "util/netevent.h"
#include "util/log.h"
#include "util/storage/slabhash.h"
#include "util/storage/lookup3.h"

#include "dnscrypt/cert.h"
#include "dnscrypt/dnscrypt.h"
#include "dnscrypt/dnscrypt_config.h"

#include <ctype.h>


/**
 * \file
 * dnscrypt functions for encrypting DNS packets.
 */

#define DNSCRYPT_QUERY_BOX_OFFSET \
    (DNSCRYPT_MAGIC_HEADER_LEN + crypto_box_PUBLICKEYBYTES + \
    crypto_box_HALF_NONCEBYTES)

//  8 bytes: magic header (CERT_MAGIC_HEADER)
// 12 bytes: the client's nonce
// 12 bytes: server nonce extension
// 16 bytes: Poly1305 MAC (crypto_box_ZEROBYTES - crypto_box_BOXZEROBYTES)

#define DNSCRYPT_REPLY_BOX_OFFSET \
    (DNSCRYPT_MAGIC_HEADER_LEN + crypto_box_HALF_NONCEBYTES + \
    crypto_box_HALF_NONCEBYTES)


/**
 * Shared secret cache key length.
 * secret key.
 * 1 byte: ES_VERSION[1]
 * 32 bytes: client crypto_box_PUBLICKEYBYTES
 * 32 bytes: server crypto_box_SECRETKEYBYTES
 */
#define DNSCRYPT_SHARED_SECRET_KEY_LENGTH \
    (1 + crypto_box_PUBLICKEYBYTES + crypto_box_SECRETKEYBYTES)


struct shared_secret_cache_key {
    /** the hash table key */
    uint8_t key[DNSCRYPT_SHARED_SECRET_KEY_LENGTH];
    /** the hash table entry, data is uint8_t pointer of size crypto_box_BEFORENMBYTES which contains the shared secret. */
    struct lruhash_entry entry;
};


struct nonce_cache_key {
    /** the nonce used by the client */
    uint8_t nonce[crypto_box_HALF_NONCEBYTES];
    /** the client_magic used by the client, this is associated to 1 cert only */
    uint8_t magic_query[DNSCRYPT_MAGIC_HEADER_LEN];
    /** the client public key */
    uint8_t client_publickey[crypto_box_PUBLICKEYBYTES];
    /** the hash table entry, data is uint8_t */
    struct lruhash_entry entry;
};

/**
 * Generate a key suitable to find shared secret in slabhash.
 * \param[in] key: a uint8_t pointer of size DNSCRYPT_SHARED_SECRET_KEY_LENGTH
 * \param[in] esversion: The es version least significant byte.
 * \param[in] pk: The public key of the client. uint8_t pointer of size
 * crypto_box_PUBLICKEYBYTES.
 * \param[in] sk: The secret key of the server matching the magic query number.
 * uint8_t pointer of size crypto_box_SECRETKEYBYTES.
 * \return the hash of the key.
 */
static uint32_t
dnsc_shared_secrets_cache_key(uint8_t* key,
                              uint8_t esversion,
                              uint8_t* pk,
                              uint8_t* sk)
{
    key[0] = esversion;
    memcpy(key + 1, pk, crypto_box_PUBLICKEYBYTES);
    memcpy(key + 1 + crypto_box_PUBLICKEYBYTES, sk, crypto_box_SECRETKEYBYTES);
    return hashlittle(key, DNSCRYPT_SHARED_SECRET_KEY_LENGTH, 0);
}

/**
 * Inserts a shared secret into the shared_secrets_cache slabhash.
 * The shared secret is copied so the caller can use it freely without caring
 * about the cache entry being evicted or not.
 * \param[in] cache: the slabhash in which to look for the key.
 * \param[in] key: a uint8_t pointer of size DNSCRYPT_SHARED_SECRET_KEY_LENGTH
 * which contains the key of the shared secret.
 * \param[in] hash: the hash of the key.
 * \param[in] nmkey: a uint8_t pointer of size crypto_box_BEFORENMBYTES which
 * contains the shared secret.
 */
static void
dnsc_shared_secret_cache_insert(struct slabhash *cache,
                                uint8_t key[DNSCRYPT_SHARED_SECRET_KEY_LENGTH],
                                uint32_t hash,
                                uint8_t nmkey[crypto_box_BEFORENMBYTES])
{
    struct shared_secret_cache_key* k =
        (struct shared_secret_cache_key*)calloc(1, sizeof(*k));
    uint8_t* d = malloc(crypto_box_BEFORENMBYTES);
    if(!k || !d) {
        free(k);
        free(d);
        return;
    }
    memcpy(d, nmkey, crypto_box_BEFORENMBYTES);
    lock_rw_init(&k->entry.lock);
    memcpy(k->key, key, DNSCRYPT_SHARED_SECRET_KEY_LENGTH);
    k->entry.hash = hash;
    k->entry.key = k;
    k->entry.data = d;
    slabhash_insert(cache,
                    hash, &k->entry,
                    d,
                    NULL);
}

/**
 * Lookup a record in shared_secrets_cache.
 * \param[in] cache: a pointer to shared_secrets_cache slabhash.
 * \param[in] key: a uint8_t pointer of size DNSCRYPT_SHARED_SECRET_KEY_LENGTH
 * containing the key to look for.
 * \param[in] hash: a hash of the key.
 * \return a pointer to the locked cache entry or NULL on failure.
 */
static struct lruhash_entry*
dnsc_shared_secrets_lookup(struct slabhash* cache,
                           uint8_t key[DNSCRYPT_SHARED_SECRET_KEY_LENGTH],
                           uint32_t hash)
{
    return slabhash_lookup(cache, hash, key, 0);
}

/**
 * Generate a key hash suitable to find a nonce in slabhash.
 * \param[in] nonce: a uint8_t pointer of size crypto_box_HALF_NONCEBYTES
 * \param[in] magic_query: a uint8_t pointer of size DNSCRYPT_MAGIC_HEADER_LEN
 * \param[in] pk: The public key of the client. uint8_t pointer of size
 * crypto_box_PUBLICKEYBYTES.
 * \return the hash of the key.
 */
static uint32_t
dnsc_nonce_cache_key_hash(const uint8_t nonce[crypto_box_HALF_NONCEBYTES],
                          const uint8_t magic_query[DNSCRYPT_MAGIC_HEADER_LEN],
                          const uint8_t pk[crypto_box_PUBLICKEYBYTES])
{
    uint32_t h = 0;
    h = hashlittle(nonce, crypto_box_HALF_NONCEBYTES, h);
    h = hashlittle(magic_query, DNSCRYPT_MAGIC_HEADER_LEN, h);
    return hashlittle(pk, crypto_box_PUBLICKEYBYTES, h);
}

/**
 * Inserts a nonce, magic_query, pk tuple into the nonces_cache slabhash.
 * \param[in] cache: the slabhash in which to look for the key.
 * \param[in] nonce: a uint8_t pointer of size crypto_box_HALF_NONCEBYTES
 * \param[in] magic_query: a uint8_t pointer of size DNSCRYPT_MAGIC_HEADER_LEN
 * \param[in] pk: The public key of the client. uint8_t pointer of size
 * crypto_box_PUBLICKEYBYTES.
 * \param[in] hash: the hash of the key.
 */
static void
dnsc_nonce_cache_insert(struct slabhash *cache,
                        const uint8_t nonce[crypto_box_HALF_NONCEBYTES],
                        const uint8_t magic_query[DNSCRYPT_MAGIC_HEADER_LEN],
                        const uint8_t pk[crypto_box_PUBLICKEYBYTES],
                        uint32_t hash)
{
    struct nonce_cache_key* k =
        (struct nonce_cache_key*)calloc(1, sizeof(*k));
    if(!k) {
        free(k);
        return;
    }
    lock_rw_init(&k->entry.lock);
    memcpy(k->nonce, nonce, crypto_box_HALF_NONCEBYTES);
    memcpy(k->magic_query, magic_query, DNSCRYPT_MAGIC_HEADER_LEN);
    memcpy(k->client_publickey, pk, crypto_box_PUBLICKEYBYTES);
    k->entry.hash = hash;
    k->entry.key = k;
    k->entry.data = NULL;
    slabhash_insert(cache,
                    hash, &k->entry,
                    NULL,
                    NULL);
}

/**
 * Lookup a record in nonces_cache.
 * \param[in] cache: the slabhash in which to look for the key.
 * \param[in] nonce: a uint8_t pointer of size crypto_box_HALF_NONCEBYTES
 * \param[in] magic_query: a uint8_t pointer of size DNSCRYPT_MAGIC_HEADER_LEN
 * \param[in] pk: The public key of the client. uint8_t pointer of size
 * crypto_box_PUBLICKEYBYTES.
 * \param[in] hash: the hash of the key.
 * \return a pointer to the locked cache entry or NULL on failure.
 */
static struct lruhash_entry*
dnsc_nonces_lookup(struct slabhash* cache,
                   const uint8_t nonce[crypto_box_HALF_NONCEBYTES],
                   const uint8_t magic_query[DNSCRYPT_MAGIC_HEADER_LEN],
                   const uint8_t pk[crypto_box_PUBLICKEYBYTES],
                   uint32_t hash)
{
    struct nonce_cache_key k;
    memset(&k, 0, sizeof(k));
    k.entry.hash = hash;
    memcpy(k.nonce, nonce, crypto_box_HALF_NONCEBYTES);
    memcpy(k.magic_query, magic_query, DNSCRYPT_MAGIC_HEADER_LEN);
    memcpy(k.client_publickey, pk, crypto_box_PUBLICKEYBYTES);

    return slabhash_lookup(cache, hash, &k, 0);
}

/**
 * Decrypt a query using the dnsccert that was found using dnsc_find_cert.
 * The client nonce will be extracted from the encrypted query and stored in
 * client_nonce, a shared secret will be computed and stored in nmkey and the
 * buffer will be decrypted inplace.
 * \param[in] env the dnscrypt environment.
 * \param[in] cert the cert that matches this encrypted query.
 * \param[in] client_nonce where the client nonce will be stored.
 * \param[in] nmkey where the shared secret key will be written.
 * \param[in] buffer the encrypted buffer.
 * \return 0 on success.
 */
static int
dnscrypt_server_uncurve(struct dnsc_env* env,
                        const dnsccert *cert,
                        uint8_t client_nonce[crypto_box_HALF_NONCEBYTES],
                        uint8_t nmkey[crypto_box_BEFORENMBYTES],
                        struct sldns_buffer* buffer)
{
    size_t len = sldns_buffer_limit(buffer);
    uint8_t *const buf = sldns_buffer_begin(buffer);
    uint8_t nonce[crypto_box_NONCEBYTES];
    struct dnscrypt_query_header *query_header;
    // shared secret cache
    uint8_t key[DNSCRYPT_SHARED_SECRET_KEY_LENGTH];
    struct lruhash_entry* entry;
    uint32_t hash;

    uint32_t nonce_hash;

    if (len <= DNSCRYPT_QUERY_HEADER_SIZE) {
        return -1;
    }

    query_header = (struct dnscrypt_query_header *)buf;

    /* Detect replay attacks */
    nonce_hash = dnsc_nonce_cache_key_hash(
        query_header->nonce,
        cert->magic_query,
        query_header->publickey);

    lock_basic_lock(&env->nonces_cache_lock);
    entry = dnsc_nonces_lookup(
        env->nonces_cache,
        query_header->nonce,
        cert->magic_query,
        query_header->publickey,
        nonce_hash);

    if(entry) {
        lock_rw_unlock(&entry->lock);
        env->num_query_dnscrypt_replay++;
        lock_basic_unlock(&env->nonces_cache_lock);
        return -1;
    }

    dnsc_nonce_cache_insert(
        env->nonces_cache,
        query_header->nonce,
        cert->magic_query,
        query_header->publickey,
        nonce_hash);
    lock_basic_unlock(&env->nonces_cache_lock);

    /* Find existing shared secret */
    hash = dnsc_shared_secrets_cache_key(key,
                                         cert->es_version[1],
                                         query_header->publickey,
                                         cert->keypair->crypt_secretkey);
    entry = dnsc_shared_secrets_lookup(env->shared_secrets_cache,
                                       key,
                                       hash);

    if(!entry) {
        lock_basic_lock(&env->shared_secrets_cache_lock);
        env->num_query_dnscrypt_secret_missed_cache++;
        lock_basic_unlock(&env->shared_secrets_cache_lock);
        if(cert->es_version[1] == 2) {
#ifdef USE_DNSCRYPT_XCHACHA20
            if (crypto_box_curve25519xchacha20poly1305_beforenm(
                        nmkey, query_header->publickey,
                        cert->keypair->crypt_secretkey) != 0) {
                return -1;
            }
#else
            return -1;
#endif
    } else {
        if (crypto_box_beforenm(nmkey,
                                query_header->publickey,
                                cert->keypair->crypt_secretkey) != 0) {
            return -1;
        }
    }
    // Cache the shared secret we just computed.
    dnsc_shared_secret_cache_insert(env->shared_secrets_cache,
                                    key,
                                    hash,
                                    nmkey);
    } else {
        /* copy shared secret and unlock entry */
        memcpy(nmkey, entry->data, crypto_box_BEFORENMBYTES);
        lock_rw_unlock(&entry->lock);
    }

    memcpy(nonce, query_header->nonce, crypto_box_HALF_NONCEBYTES);
    memset(nonce + crypto_box_HALF_NONCEBYTES, 0, crypto_box_HALF_NONCEBYTES);

    if(cert->es_version[1] == 2) {
#ifdef USE_DNSCRYPT_XCHACHA20
        if (crypto_box_curve25519xchacha20poly1305_open_easy_afternm
                (buf,
                buf + DNSCRYPT_QUERY_BOX_OFFSET,
                len - DNSCRYPT_QUERY_BOX_OFFSET, nonce,
                nmkey) != 0) {
            return -1;
        }
#else
        return -1;
#endif
    } else {
        if (crypto_box_open_easy_afternm
            (buf,
             buf + DNSCRYPT_QUERY_BOX_OFFSET,
             len - DNSCRYPT_QUERY_BOX_OFFSET, nonce,
             nmkey) != 0) {
            return -1;
        }
    }

    len -= DNSCRYPT_QUERY_HEADER_SIZE;

    while (*sldns_buffer_at(buffer, --len) == 0)
        ;

    if (*sldns_buffer_at(buffer, len) != 0x80) {
        return -1;
    }

    memcpy(client_nonce, nonce, crypto_box_HALF_NONCEBYTES);

    sldns_buffer_set_position(buffer, 0);
    sldns_buffer_set_limit(buffer, len);

    return 0;
}


/**
 * Add random padding to a buffer, according to a client nonce.
 * The length has to depend on the query in order to avoid reply attacks.
 *
 * @param buf a buffer
 * @param len the initial size of the buffer
 * @param max_len the maximum size
 * @param nonce a nonce, made of the client nonce repeated twice
 * @param secretkey
 * @return the new size, after padding
 */
size_t
dnscrypt_pad(uint8_t *buf, const size_t len, const size_t max_len,
             const uint8_t *nonce, const uint8_t *secretkey)
{
    uint8_t *buf_padding_area = buf + len;
    size_t padded_len;
    uint32_t rnd;

    // no padding
    if (max_len < len + DNSCRYPT_MIN_PAD_LEN)
        return len;

    assert(nonce[crypto_box_HALF_NONCEBYTES] == nonce[0]);

    crypto_stream((unsigned char *)&rnd, (unsigned long long)sizeof(rnd), nonce,
                  secretkey);
    padded_len =
        len + DNSCRYPT_MIN_PAD_LEN + rnd % (max_len - len -
                                            DNSCRYPT_MIN_PAD_LEN + 1);
    padded_len += DNSCRYPT_BLOCK_SIZE - padded_len % DNSCRYPT_BLOCK_SIZE;
    if (padded_len > max_len)
        padded_len = max_len;

    memset(buf_padding_area, 0, padded_len - len);
    *buf_padding_area = 0x80;

    return padded_len;
}

uint64_t
dnscrypt_hrtime(void)
{
    struct timeval tv;
    uint64_t ts = (uint64_t)0U;
    int ret;

    ret = gettimeofday(&tv, NULL);
    if (ret == 0) {
        ts = (uint64_t)tv.tv_sec * 1000000U + (uint64_t)tv.tv_usec;
    } else {
        log_err("gettimeofday: %s", strerror(errno));
    }
    return ts;
}

/**
 * Add the server nonce part to once.
 * The nonce is made half of client nonce and the seconf half of the server
 * nonce, both of them of size crypto_box_HALF_NONCEBYTES.
 * \param[in] nonce: a uint8_t* of size crypto_box_NONCEBYTES
 */
static void
add_server_nonce(uint8_t *nonce)
{
    uint64_t ts;
    uint64_t tsn;
    uint32_t suffix;
    ts = dnscrypt_hrtime();
    // TODO? dnscrypt-wrapper does some logic with context->nonce_ts_last
    // unclear if we really need it, so skipping it for now.
    tsn = (ts << 10) | (randombytes_random() & 0x3ff);
#if (BYTE_ORDER == LITTLE_ENDIAN)
    tsn =
        (((uint64_t)htonl((uint32_t)tsn)) << 32) | htonl((uint32_t)(tsn >> 32));
#endif
    memcpy(nonce + crypto_box_HALF_NONCEBYTES, &tsn, 8);
    suffix = randombytes_random();
    memcpy(nonce + crypto_box_HALF_NONCEBYTES + 8, &suffix, 4);
}

/**
 * Encrypt a reply using the dnsccert that was used with the query.
 * The client nonce will be extracted from the encrypted query and stored in
 * The buffer will be encrypted inplace.
 * \param[in] cert the dnsccert that matches this encrypted query.
 * \param[in] client_nonce client nonce used during the query
 * \param[in] nmkey shared secret key used during the query.
 * \param[in] buffer the buffer where to encrypt the reply.
 * \param[in] udp if whether or not it is a UDP query.
 * \param[in] max_udp_size configured max udp size.
 * \return 0 on success.
 */
static int
dnscrypt_server_curve(const dnsccert *cert,
                      uint8_t client_nonce[crypto_box_HALF_NONCEBYTES],
                      uint8_t nmkey[crypto_box_BEFORENMBYTES],
                      struct sldns_buffer* buffer,
                      uint8_t udp,
                      size_t max_udp_size)
{
    size_t dns_reply_len = sldns_buffer_limit(buffer);
    size_t max_len = dns_reply_len + DNSCRYPT_MAX_PADDING \
        + DNSCRYPT_REPLY_HEADER_SIZE;
    size_t max_reply_size = max_udp_size - 20U - 8U;
    uint8_t nonce[crypto_box_NONCEBYTES];
    uint8_t *boxed;
    uint8_t *const buf = sldns_buffer_begin(buffer);
    size_t len = sldns_buffer_limit(buffer);

    if(udp){
        if (max_len > max_reply_size)
            max_len = max_reply_size;
    }


    memcpy(nonce, client_nonce, crypto_box_HALF_NONCEBYTES);
    memcpy(nonce + crypto_box_HALF_NONCEBYTES, client_nonce,
           crypto_box_HALF_NONCEBYTES);

    boxed = buf + DNSCRYPT_REPLY_BOX_OFFSET;
    memmove(boxed + crypto_box_MACBYTES, buf, len);
    len = dnscrypt_pad(boxed + crypto_box_MACBYTES, len,
                       max_len - DNSCRYPT_REPLY_HEADER_SIZE, nonce,
                       cert->keypair->crypt_secretkey);
    sldns_buffer_set_at(buffer,
                        DNSCRYPT_REPLY_BOX_OFFSET - crypto_box_BOXZEROBYTES,
                        0, crypto_box_ZEROBYTES);

    // add server nonce extension
    add_server_nonce(nonce);

    if(cert->es_version[1] == 2) {
#ifdef USE_DNSCRYPT_XCHACHA20
        if (crypto_box_curve25519xchacha20poly1305_easy_afternm
            (boxed, boxed + crypto_box_MACBYTES, len, nonce, nmkey) != 0) {
            return -1;
        }
#else
        return -1;
#endif
    } else {
        if (crypto_box_easy_afternm
            (boxed, boxed + crypto_box_MACBYTES, len, nonce, nmkey) != 0) {
            return -1;
        }
    }

    sldns_buffer_write_at(buffer,
                          0,
                          DNSCRYPT_MAGIC_RESPONSE,
                          DNSCRYPT_MAGIC_HEADER_LEN);
    sldns_buffer_write_at(buffer,
                          DNSCRYPT_MAGIC_HEADER_LEN,
                          nonce,
                          crypto_box_NONCEBYTES);
    sldns_buffer_set_limit(buffer, len + DNSCRYPT_REPLY_HEADER_SIZE);
    return 0;
}

/**
 * Read the content of fname into buf.
 * \param[in] fname name of the file to read.
 * \param[in] buf the buffer in which to read the content of the file.
 * \param[in] count number of bytes to read.
 * \return 0 on success.
 */
static int
dnsc_read_from_file(char *fname, char *buf, size_t count)
{
    int fd;
    fd = open(fname, O_RDONLY);
    if (fd == -1) {
        return -1;
    }
    if (read(fd, buf, count) != (ssize_t)count) {
        close(fd);
        return -2;
    }
    close(fd);
    return 0;
}

/**
 * Given an absolute path on the original root, returns the absolute path
 * within the chroot. If chroot is disabled, the path is not modified.
 * No char * is malloced so there is no need to free this.
 * \param[in] cfg the configuration.
 * \param[in] path the path from the original root.
 * \return the path from inside the chroot.
 */
static char *
dnsc_chroot_path(struct config_file *cfg, char *path)
{
    char *nm;
    nm = path;
    if(cfg->chrootdir && cfg->chrootdir[0] && strncmp(nm,
        cfg->chrootdir, strlen(cfg->chrootdir)) == 0)
        nm += strlen(cfg->chrootdir);
    return nm;
}

/**
 * Parse certificates files provided by the configuration and load them into
 * dnsc_env.
 * \param[in] env the dnsc_env structure to load the certs into.
 * \param[in] cfg the configuration.
 * \return the number of certificates loaded.
 */
static int
dnsc_parse_certs(struct dnsc_env *env, struct config_file *cfg)
{
	struct config_strlist *head, *head2;
	size_t signed_cert_id;
	size_t rotated_cert_id;
	char *nm;

	env->signed_certs_count = 0U;
	env->rotated_certs_count = 0U;
	for (head = cfg->dnscrypt_provider_cert; head; head = head->next) {
		env->signed_certs_count++;
	}
	for (head = cfg->dnscrypt_provider_cert_rotated; head; head = head->next) {
		env->rotated_certs_count++;
	}
	env->signed_certs = sodium_allocarray(env->signed_certs_count,
										  sizeof *env->signed_certs);

	env->rotated_certs = sodium_allocarray(env->rotated_certs_count,
										  sizeof env->signed_certs);
	signed_cert_id = 0U;
	rotated_cert_id = 0U;
	for(head = cfg->dnscrypt_provider_cert; head; head = head->next, signed_cert_id++) {
		nm = dnsc_chroot_path(cfg, head->str);
		if(dnsc_read_from_file(
				nm,
				(char *)(env->signed_certs + signed_cert_id),
				sizeof(struct SignedCert)) != 0) {
			fatal_exit("dnsc_parse_certs: failed to load %s: %s", head->str, strerror(errno));
		}
		for(head2 = cfg->dnscrypt_provider_cert_rotated; head2; head2 = head2->next) {
			if(strcmp(head->str, head2->str) == 0) {
				*(env->rotated_certs + rotated_cert_id) = env->signed_certs + signed_cert_id;
				rotated_cert_id++;
				verbose(VERB_OPS, "Cert %s is rotated and will not be distributed via DNS", head->str);
				break;
			}
		}
		verbose(VERB_OPS, "Loaded cert %s", head->str);
	}
	return signed_cert_id;
}

/**
 * Helper function to convert a binary key into a printable fingerprint.
 * \param[in] fingerprint the buffer in which to write the printable key.
 * \param[in] key the key to convert.
 */
void
dnsc_key_to_fingerprint(char fingerprint[80U], const uint8_t * const key)
{
    const size_t fingerprint_size = 80U;
    size_t       fingerprint_pos = (size_t) 0U;
    size_t       key_pos = (size_t) 0U;

    for (;;) {
        assert(fingerprint_size > fingerprint_pos);
        snprintf(&fingerprint[fingerprint_pos],
                        fingerprint_size - fingerprint_pos, "%02X%02X",
                        key[key_pos], key[key_pos + 1U]);
        key_pos += 2U;
        if (key_pos >= crypto_box_PUBLICKEYBYTES) {
            break;
        }
        fingerprint[fingerprint_pos + 4U] = ':';
        fingerprint_pos += 5U;
    }
}

/**
 * Find the cert matching a DNSCrypt query.
 * \param[in] dnscenv The DNSCrypt environment, which contains the list of certs
 * supported by the server.
 * \param[in] buffer The encrypted DNS query.
 * \return a dnsccert * if we found a cert matching the magic_number of the
 * query, NULL otherwise.
 */
static const dnsccert *
dnsc_find_cert(struct dnsc_env* dnscenv, struct sldns_buffer* buffer)
{
	const dnsccert *certs = dnscenv->certs;
	struct dnscrypt_query_header *dnscrypt_header;
	size_t i;

	if (sldns_buffer_limit(buffer) < DNSCRYPT_QUERY_HEADER_SIZE) {
		return NULL;
	}
	dnscrypt_header = (struct dnscrypt_query_header *)sldns_buffer_begin(buffer);
	for (i = 0U; i < dnscenv->signed_certs_count; i++) {
		if (memcmp(certs[i].magic_query, dnscrypt_header->magic_query,
                   DNSCRYPT_MAGIC_HEADER_LEN) == 0) {
			return &certs[i];
		}
	}
	return NULL;
}

/**
 * Insert local-zone and local-data into configuration.
 * In order to be able to serve certs over TXT, we can reuse the local-zone and
 * local-data config option. The zone and qname are infered from the
 * provider_name and the content of the TXT record from the certificate content.
 * returns the number of certificate TXT record that were loaded.
 * < 0 in case of error.
 */
static int
dnsc_load_local_data(struct dnsc_env* dnscenv, struct config_file *cfg)
{
    size_t i, j;
	// Insert 'local-zone: "2.dnscrypt-cert.example.com" deny'
    if(!cfg_str2list_insert(&cfg->local_zones,
                            strdup(dnscenv->provider_name),
                            strdup("deny"))) {
        log_err("Could not load dnscrypt local-zone: %s deny",
                dnscenv->provider_name);
        return -1;
    }

    // Add local data entry of type:
    // 2.dnscrypt-cert.example.com 86400 IN TXT "DNSC......"
    for(i=0; i<dnscenv->signed_certs_count; i++) {
        const char *ttl_class_type = " 86400 IN TXT \"";
        int rotated_cert = 0;
	uint32_t serial;
	uint16_t rrlen;
	char* rr;
        struct SignedCert *cert = dnscenv->signed_certs + i;
		// Check if the certificate is being rotated and should not be published
        for(j=0; j<dnscenv->rotated_certs_count; j++){
            if(cert == dnscenv->rotated_certs[j]) {
                rotated_cert = 1;
                break;
            }
        }
		memcpy(&serial, cert->serial, sizeof serial);
		serial = htonl(serial);
        if(rotated_cert) {
            verbose(VERB_OPS,
                "DNSCrypt: not adding cert with serial #%"
                PRIu32
                " to local-data as it is rotated",
                serial
            );
            continue;
        }
        rrlen = strlen(dnscenv->provider_name) +
                         strlen(ttl_class_type) +
                         4 * sizeof(struct SignedCert) + // worst case scenario
                         1 + // trailing double quote
                         1;
        rr = malloc(rrlen);
        if(!rr) {
            log_err("Could not allocate memory");
            return -2;
        }
        snprintf(rr, rrlen - 1, "%s 86400 IN TXT \"", dnscenv->provider_name);
        for(j=0; j<sizeof(struct SignedCert); j++) {
			int c = (int)*((const uint8_t *) cert + j);
            if (isprint(c) && c != '"' && c != '\\') {
                snprintf(rr + strlen(rr), rrlen - 1 - strlen(rr), "%c", c);
            } else {
                snprintf(rr + strlen(rr), rrlen - 1 - strlen(rr), "\\%03d", c);
            }
        }
        verbose(VERB_OPS,
			"DNSCrypt: adding cert with serial #%"
			PRIu32
			" to local-data to config: %s",
			serial, rr
		);
        snprintf(rr + strlen(rr), rrlen - 1 - strlen(rr), "\"");
        cfg_strlist_insert(&cfg->local_data, strdup(rr));
        free(rr);
    }
    return dnscenv->signed_certs_count;
}

static const char *
key_get_es_version(uint8_t version[2])
{
    struct es_version {
        uint8_t es_version[2];
        const char *name;
    };

    struct es_version es_versions[] = {
        {{0x00, 0x01}, "X25519-XSalsa20Poly1305"},
        {{0x00, 0x02}, "X25519-XChacha20Poly1305"},
    };
    int i;
    for(i=0; i < (int)sizeof(es_versions); i++){
        if(es_versions[i].es_version[0] == version[0] &&
           es_versions[i].es_version[1] == version[1]){
            return es_versions[i].name;
        }
    }
    return NULL;
}


/**
 * Parse the secret key files from `dnscrypt-secret-key` config and populates
 * a list of dnsccert with es_version, magic number and secret/public keys
 * supported by dnscrypt listener.
 * \param[in] env The dnsc_env structure which will hold the keypairs.
 * \param[in] cfg The config with the secret key file paths.
 */
static int
dnsc_parse_keys(struct dnsc_env *env, struct config_file *cfg)
{
	struct config_strlist *head;
	size_t cert_id, keypair_id;
	size_t c;
	char *nm;

	env->keypairs_count = 0U;
	for (head = cfg->dnscrypt_secret_key; head; head = head->next) {
		env->keypairs_count++;
	}

	env->keypairs = sodium_allocarray(env->keypairs_count,
		sizeof *env->keypairs);
	env->certs = sodium_allocarray(env->signed_certs_count,
		sizeof *env->certs);

	cert_id = 0U;
	keypair_id = 0U;
	for(head = cfg->dnscrypt_secret_key; head; head = head->next, keypair_id++) {
		char fingerprint[80];
		int found_cert = 0;
		KeyPair *current_keypair = &env->keypairs[keypair_id];
		nm = dnsc_chroot_path(cfg, head->str);
		if(dnsc_read_from_file(
				nm,
				(char *)(current_keypair->crypt_secretkey),
				crypto_box_SECRETKEYBYTES) != 0) {
			fatal_exit("dnsc_parse_keys: failed to load %s: %s", head->str, strerror(errno));
		}
		verbose(VERB_OPS, "Loaded key %s", head->str);
		if (crypto_scalarmult_base(current_keypair->crypt_publickey,
			current_keypair->crypt_secretkey) != 0) {
			fatal_exit("dnsc_parse_keys: could not generate public key from %s", head->str);
		}
		dnsc_key_to_fingerprint(fingerprint, current_keypair->crypt_publickey);
		verbose(VERB_OPS, "Crypt public key fingerprint for %s: %s", head->str, fingerprint);
		// find the cert matching this key
		for(c = 0; c < env->signed_certs_count; c++) {
			if(memcmp(current_keypair->crypt_publickey,
				env->signed_certs[c].server_publickey,
				crypto_box_PUBLICKEYBYTES) == 0) {
				dnsccert *current_cert = &env->certs[cert_id++];
				found_cert = 1;
				current_cert->keypair = current_keypair;
				memcpy(current_cert->magic_query,
				       env->signed_certs[c].magic_query,
					sizeof env->signed_certs[c].magic_query);
				memcpy(current_cert->es_version,
				       env->signed_certs[c].version_major,
				       sizeof env->signed_certs[c].version_major
				);
				dnsc_key_to_fingerprint(fingerprint,
							current_cert->keypair->crypt_publickey);
				verbose(VERB_OPS, "Crypt public key fingerprint for %s: %s",
					head->str, fingerprint);
				verbose(VERB_OPS, "Using %s",
					key_get_es_version(current_cert->es_version));
#ifndef USE_DNSCRYPT_XCHACHA20
				if (current_cert->es_version[1] == 0x02) {
				    fatal_exit("Certificate for XChacha20 but libsodium does not support it.");
				}
#endif

            		}
        	}
		if (!found_cert) {
		    fatal_exit("dnsc_parse_keys: could not match certificate for key "
			       "%s. Unable to determine ES version.",
			       head->str);
		}
	}
	return cert_id;
}

static void
sodium_misuse_handler(void)
{
	fatal_exit(
		"dnscrypt: libsodium could not be initialized, this typically"
		" happens when no good source of entropy is found. If you run"
		" unbound in a chroot, make sure /dev/random is available. See"
		" https://www.unbound.net/documentation/unbound.conf.html");
}


/**
 * #########################################################
 * ############# Publicly accessible functions #############
 * #########################################################
 */

int
dnsc_handle_curved_request(struct dnsc_env* dnscenv,
                           struct comm_reply* repinfo)
{
    struct comm_point* c = repinfo->c;

    repinfo->is_dnscrypted = 0;
    if( !c->dnscrypt ) {
        return 1;
    }
    // Attempt to decrypt the query. If it is not crypted, we may still need
    // to serve the certificate.
    verbose(VERB_ALGO, "handle request called on DNSCrypt socket");
    if ((repinfo->dnsc_cert = dnsc_find_cert(dnscenv, c->buffer)) != NULL) {
        if(dnscrypt_server_uncurve(dnscenv,
                                   repinfo->dnsc_cert,
                                   repinfo->client_nonce,
                                   repinfo->nmkey,
                                   c->buffer) != 0){
            verbose(VERB_ALGO, "dnscrypt: Failed to uncurve");
            comm_point_drop_reply(repinfo);
            return 0;
        }
        repinfo->is_dnscrypted = 1;
        sldns_buffer_rewind(c->buffer);
    }
    return 1;
}

int
dnsc_handle_uncurved_request(struct comm_reply *repinfo)
{
    if(!repinfo->c->dnscrypt) {
        return 1;
    }
    sldns_buffer_copy(repinfo->c->dnscrypt_buffer, repinfo->c->buffer);
    if(!repinfo->is_dnscrypted) {
        return 1;
    }
	if(dnscrypt_server_curve(repinfo->dnsc_cert,
                             repinfo->client_nonce,
                             repinfo->nmkey,
                             repinfo->c->dnscrypt_buffer,
                             repinfo->c->type == comm_udp,
                             repinfo->max_udp_size) != 0){
		verbose(VERB_ALGO, "dnscrypt: Failed to curve cached missed answer");
		comm_point_drop_reply(repinfo);
		return 0;
	}
    return 1;
}

struct dnsc_env *
dnsc_create(void)
{
	struct dnsc_env *env;
#ifdef SODIUM_MISUSE_HANDLER
	sodium_set_misuse_handler(sodium_misuse_handler);
#endif
	if (sodium_init() == -1) {
		fatal_exit("dnsc_create: could not initialize libsodium.");
	}
	env = (struct dnsc_env *) calloc(1, sizeof(struct dnsc_env));
	lock_basic_init(&env->shared_secrets_cache_lock);
	lock_protect(&env->shared_secrets_cache_lock,
                 &env->num_query_dnscrypt_secret_missed_cache,
                 sizeof(env->num_query_dnscrypt_secret_missed_cache));
	lock_basic_init(&env->nonces_cache_lock);
	lock_protect(&env->nonces_cache_lock,
                 &env->nonces_cache,
                 sizeof(env->nonces_cache));
	lock_protect(&env->nonces_cache_lock,
                 &env->num_query_dnscrypt_replay,
                 sizeof(env->num_query_dnscrypt_replay));

	return env;
}

int
dnsc_apply_cfg(struct dnsc_env *env, struct config_file *cfg)
{
    if(dnsc_parse_certs(env, cfg) <= 0) {
        fatal_exit("dnsc_apply_cfg: no cert file loaded");
    }
    if(dnsc_parse_keys(env, cfg) <= 0) {
        fatal_exit("dnsc_apply_cfg: no key file loaded");
    }
    randombytes_buf(env->hash_key, sizeof env->hash_key);
    env->provider_name = cfg->dnscrypt_provider;

    if(dnsc_load_local_data(env, cfg) <= 0) {
        fatal_exit("dnsc_apply_cfg: could not load local data");
    }
    lock_basic_lock(&env->shared_secrets_cache_lock);
    env->shared_secrets_cache = slabhash_create(
        cfg->dnscrypt_shared_secret_cache_slabs,
        HASH_DEFAULT_STARTARRAY,
        cfg->dnscrypt_shared_secret_cache_size,
        dnsc_shared_secrets_sizefunc,
        dnsc_shared_secrets_compfunc,
        dnsc_shared_secrets_delkeyfunc,
        dnsc_shared_secrets_deldatafunc,
        NULL
    );
    lock_basic_unlock(&env->shared_secrets_cache_lock);
    if(!env->shared_secrets_cache){
        fatal_exit("dnsc_apply_cfg: could not create shared secrets cache.");
    }
    lock_basic_lock(&env->nonces_cache_lock);
    env->nonces_cache = slabhash_create(
        cfg->dnscrypt_nonce_cache_slabs,
        HASH_DEFAULT_STARTARRAY,
        cfg->dnscrypt_nonce_cache_size,
        dnsc_nonces_sizefunc,
        dnsc_nonces_compfunc,
        dnsc_nonces_delkeyfunc,
        dnsc_nonces_deldatafunc,
        NULL
    );
    lock_basic_unlock(&env->nonces_cache_lock);
    return 0;
}

void
dnsc_delete(struct dnsc_env *env)
{
	if(!env) {
		return;
	}
	verbose(VERB_OPS, "DNSCrypt: Freeing environment.");
	sodium_free(env->signed_certs);
	sodium_free(env->rotated_certs);
	sodium_free(env->certs);
	sodium_free(env->keypairs);
	lock_basic_destroy(&env->shared_secrets_cache_lock);
	lock_basic_destroy(&env->nonces_cache_lock);
	slabhash_delete(env->shared_secrets_cache);
	slabhash_delete(env->nonces_cache);
	free(env);
}

/**
 * #########################################################
 * ############# Shared secrets cache functions ############
 * #########################################################
 */

size_t
dnsc_shared_secrets_sizefunc(void *k, void* ATTR_UNUSED(d))
{
    struct shared_secret_cache_key* ssk = (struct shared_secret_cache_key*)k;
    size_t key_size = sizeof(struct shared_secret_cache_key)
        + lock_get_mem(&ssk->entry.lock);
    size_t data_size = crypto_box_BEFORENMBYTES;
    (void)ssk; /* otherwise ssk is unused if no threading, or fixed locksize */
    return key_size + data_size;
}

int
dnsc_shared_secrets_compfunc(void *m1, void *m2)
{
    return sodium_memcmp(m1, m2, DNSCRYPT_SHARED_SECRET_KEY_LENGTH);
}

void
dnsc_shared_secrets_delkeyfunc(void *k, void* ATTR_UNUSED(arg))
{
    struct shared_secret_cache_key* ssk = (struct shared_secret_cache_key*)k;
    lock_rw_destroy(&ssk->entry.lock);
    free(ssk);
}

void
dnsc_shared_secrets_deldatafunc(void* d, void* ATTR_UNUSED(arg))
{
    uint8_t* data = (uint8_t*)d;
    free(data);
}

/**
 * #########################################################
 * ############### Nonces cache functions ##################
 * #########################################################
 */

size_t
dnsc_nonces_sizefunc(void *k, void* ATTR_UNUSED(d))
{
    struct nonce_cache_key* nk = (struct nonce_cache_key*)k;
    size_t key_size = sizeof(struct nonce_cache_key)
        + lock_get_mem(&nk->entry.lock);
    (void)nk; /* otherwise ssk is unused if no threading, or fixed locksize */
    return key_size;
}

int
dnsc_nonces_compfunc(void *m1, void *m2)
{
    struct nonce_cache_key *k1 = m1, *k2 = m2;
    return
        sodium_memcmp(
            k1->nonce,
            k2->nonce,
            crypto_box_HALF_NONCEBYTES) != 0 ||
        sodium_memcmp(
            k1->magic_query,
            k2->magic_query,
            DNSCRYPT_MAGIC_HEADER_LEN) != 0 ||
        sodium_memcmp(
            k1->client_publickey, k2->client_publickey,
            crypto_box_PUBLICKEYBYTES) != 0;
}

void
dnsc_nonces_delkeyfunc(void *k, void* ATTR_UNUSED(arg))
{
    struct nonce_cache_key* nk = (struct nonce_cache_key*)k;
    lock_rw_destroy(&nk->entry.lock);
    free(nk);
}

void
dnsc_nonces_deldatafunc(void* ATTR_UNUSED(d), void* ATTR_UNUSED(arg))
{
    return;
}
