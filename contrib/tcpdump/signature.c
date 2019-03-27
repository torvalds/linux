/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code
 * distributions retain the above copyright notice and this paragraph
 * in its entirety, and (2) distributions including binary code include
 * the above copyright notice and this paragraph in its entirety in
 * the documentation or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
 * LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * Functions for signature and digest verification.
 *
 * Original code by Hannes Gredler (hannes@gredler.at)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <string.h>
#include <stdlib.h>

#include "netdissect.h"
#include "signature.h"

#ifdef HAVE_LIBCRYPTO
#include <openssl/md5.h>
#endif

const struct tok signature_check_values[] = {
    { SIGNATURE_VALID, "valid"},
    { SIGNATURE_INVALID, "invalid"},
    { CANT_ALLOCATE_COPY, "can't allocate memory"},
    { CANT_CHECK_SIGNATURE, "unchecked"},
    { 0, NULL }
};


#ifdef HAVE_LIBCRYPTO
/*
 * Compute a HMAC MD5 sum.
 * Taken from rfc2104, Appendix.
 */
USES_APPLE_DEPRECATED_API
static void
signature_compute_hmac_md5(const uint8_t *text, int text_len, unsigned char *key,
                           unsigned int key_len, uint8_t *digest)
{
    MD5_CTX context;
    unsigned char k_ipad[65];    /* inner padding - key XORd with ipad */
    unsigned char k_opad[65];    /* outer padding - key XORd with opad */
    unsigned char tk[16];
    int i;

    /* if key is longer than 64 bytes reset it to key=MD5(key) */
    if (key_len > 64) {

        MD5_CTX tctx;

        MD5_Init(&tctx);
        MD5_Update(&tctx, key, key_len);
        MD5_Final(tk, &tctx);

        key = tk;
        key_len = 16;
    }

    /*
     * the HMAC_MD5 transform looks like:
     *
     * MD5(K XOR opad, MD5(K XOR ipad, text))
     *
     * where K is an n byte key
     * ipad is the byte 0x36 repeated 64 times
     * opad is the byte 0x5c repeated 64 times
     * and text is the data being protected
     */

    /* start out by storing key in pads */
    memset(k_ipad, 0, sizeof k_ipad);
    memset(k_opad, 0, sizeof k_opad);
    memcpy(k_ipad, key, key_len);
    memcpy(k_opad, key, key_len);

    /* XOR key with ipad and opad values */
    for (i=0; i<64; i++) {
        k_ipad[i] ^= 0x36;
        k_opad[i] ^= 0x5c;
    }

    /*
     * perform inner MD5
     */
    MD5_Init(&context);                   /* init context for 1st pass */
    MD5_Update(&context, k_ipad, 64);     /* start with inner pad */
    MD5_Update(&context, text, text_len); /* then text of datagram */
    MD5_Final(digest, &context);          /* finish up 1st pass */

    /*
     * perform outer MD5
     */
    MD5_Init(&context);                   /* init context for 2nd pass */
    MD5_Update(&context, k_opad, 64);     /* start with outer pad */
    MD5_Update(&context, digest, 16);     /* then results of 1st hash */
    MD5_Final(digest, &context);          /* finish up 2nd pass */
}
USES_APPLE_RST

/*
 * Verify a cryptographic signature of the packet.
 * Currently only MD5 is supported.
 */
int
signature_verify(netdissect_options *ndo, const u_char *pptr, u_int plen,
                 const u_char *sig_ptr, void (*clear_rtn)(void *),
                 const void *clear_arg)
{
    uint8_t *packet_copy, *sig_copy;
    uint8_t sig[16];
    unsigned int i;

    if (!ndo->ndo_sigsecret) {
        return (CANT_CHECK_SIGNATURE);
    }

    /*
     * Do we have all the packet data to be checked?
     */
    if (!ND_TTEST2(pptr, plen)) {
        /* No. */
        return (CANT_CHECK_SIGNATURE);
    }

    /*
     * Do we have the entire signature to check?
     */
    if (!ND_TTEST2(sig_ptr, sizeof(sig))) {
        /* No. */
        return (CANT_CHECK_SIGNATURE);
    }
    if (sig_ptr + sizeof(sig) > pptr + plen) {
        /* No. */
        return (CANT_CHECK_SIGNATURE);
    }

    /*
     * Make a copy of the packet, so we don't overwrite the original.
     */
    packet_copy = malloc(plen);
    if (packet_copy == NULL) {
        return (CANT_ALLOCATE_COPY);
    }

    memcpy(packet_copy, pptr, plen);

    /*
     * Clear the signature in the copy.
     */
    sig_copy = packet_copy + (sig_ptr - pptr);
    memset(sig_copy, 0, sizeof(sig));

    /*
     * Clear anything else that needs to be cleared in the copy.
     * Our caller is assumed to have vetted the clear_arg pointer.
     */
    (*clear_rtn)((void *)(packet_copy + ((const uint8_t *)clear_arg - pptr)));

    /*
     * Compute the signature.
     */
    signature_compute_hmac_md5(packet_copy, plen,
                               (unsigned char *)ndo->ndo_sigsecret,
                               strlen(ndo->ndo_sigsecret), sig);

    /*
     * Free the copy.
     */
    free(packet_copy);

    /*
     * Does the computed signature match the signature in the packet?
     */
    if (memcmp(sig_ptr, sig, sizeof(sig)) == 0) {
        /* Yes. */
        return (SIGNATURE_VALID);
    } else {
        /* No - print the computed signature. */
        for (i = 0; i < sizeof(sig); ++i) {
            ND_PRINT((ndo, "%02x", sig[i]));
        }

        return (SIGNATURE_INVALID);
    }
}
#else
int
signature_verify(netdissect_options *ndo _U_, const u_char *pptr _U_,
                 u_int plen _U_, const u_char *sig_ptr _U_,
                 void (*clear_rtn)(void *) _U_, const void *clear_arg _U_)
{
    return (CANT_CHECK_SIGNATURE);
}
#endif

/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 4
 * End:
 */
