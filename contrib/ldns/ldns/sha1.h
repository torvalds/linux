#ifndef LDNS_SHA1_H
#define LDNS_SHA1_H

#ifdef __cplusplus
extern "C" {
#endif
 
#define LDNS_SHA1_BLOCK_LENGTH               64
#define LDNS_SHA1_DIGEST_LENGTH              20

typedef struct {
        uint32_t       state[5];
        uint64_t       count;
        unsigned char   buffer[LDNS_SHA1_BLOCK_LENGTH];
} ldns_sha1_ctx;
  
void ldns_sha1_init(ldns_sha1_ctx * context);
void ldns_sha1_transform(uint32_t state[5], const unsigned char buffer[LDNS_SHA1_BLOCK_LENGTH]);
void ldns_sha1_update(ldns_sha1_ctx *context, const unsigned char *data, unsigned int len);
void ldns_sha1_final(unsigned char digest[LDNS_SHA1_DIGEST_LENGTH], ldns_sha1_ctx *context);

/**
 * Convenience function to digest a fixed block of data at once.
 *
 * \param[in] data the data to digest
 * \param[in] data_len the length of data in bytes
 * \param[out] digest the length of data in bytes
 *             This pointer MUST have LDNS_SHA1_DIGEST_LENGTH bytes
 *             available
 * \return the SHA1 digest of the given data
 */
unsigned char *ldns_sha1(unsigned char *data, unsigned int data_len, unsigned char *digest);

#ifdef __cplusplus
}
#endif

#endif /* LDNS_SHA1_H */
