#ifndef UNBOUND_DNSCRYPT_CERT_H
#define UNBOUND_DNSCRYPT_CERT_H

/**
 * \file
 * certificate type for dnscrypt for use in other header files
 */

#include <sodium.h>
#define CERT_MAGIC_CERT "DNSC"
#define CERT_MAJOR_VERSION 1
#define CERT_MINOR_VERSION 0
#define CERT_OLD_MAGIC_HEADER "7PYqwfzt"

#define CERT_FILE_EXPIRE_DAYS 365

struct SignedCert {
    uint8_t magic_cert[4];
    uint8_t version_major[2];
    uint8_t version_minor[2];

    // Signed Content
    uint8_t signed_content[64];
    uint8_t server_publickey[crypto_box_PUBLICKEYBYTES];
    uint8_t magic_query[8];
    uint8_t serial[4];
    uint8_t ts_begin[4];
    uint8_t ts_end[4];
};


#endif
