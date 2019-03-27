/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef BR_BEARSSL_X509_H__
#define BR_BEARSSL_X509_H__

#include <stddef.h>
#include <stdint.h>

#include "bearssl_ec.h"
#include "bearssl_hash.h"
#include "bearssl_rsa.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file bearssl_x509.h
 *
 * # X.509 Certificate Chain Processing
 *
 * An X.509 processing engine receives an X.509 chain, chunk by chunk,
 * as received from a SSL/TLS client or server (the client receives the
 * server's certificate chain, and the server receives the client's
 * certificate chain if it requested a client certificate). The chain
 * is thus injected in the engine in SSL order (end-entity first).
 *
 * The engine's job is to return the public key to use for SSL/TLS.
 * How exactly that key is obtained and verified is entirely up to the
 * engine.
 *
 * **The "known key" engine** returns a public key which is already known
 * from out-of-band information (e.g. the client _remembers_ the key from
 * a previous connection, as in the usual SSH model). This is the simplest
 * engine since it simply ignores the chain, thereby avoiding the need
 * for any decoding logic.
 *
 * **The "minimal" engine** implements minimal X.509 decoding and chain
 * validation:
 *
 *   - The provided chain should validate "as is". There is no attempt
 *     at reordering, skipping or downloading extra certificates.
 *
 *   - X.509 v1, v2 and v3 certificates are supported.
 *
 *   - Trust anchors are a DN and a public key. Each anchor is either a
 *     "CA" anchor, or a non-CA.
 *
 *   - If the end-entity certificate matches a non-CA anchor (subject DN
 *     is equal to the non-CA name, and public key is also identical to
 *     the anchor key), then this is a _direct trust_ case and the
 *     remaining certificates are ignored.
 *
 *   - Unless direct trust is applied, the chain must be verifiable up to
 *     a certificate whose issuer DN matches the DN from a "CA" trust anchor,
 *     and whose signature is verifiable against that anchor's public key.
 *     Subsequent certificates in the chain are ignored.
 *
 *   - The engine verifies subject/issuer DN matching, and enforces
 *     processing of Basic Constraints and Key Usage extensions. The
 *     Authority Key Identifier, Subject Key Identifier, Issuer Alt Name,
 *     Subject Directory Attribute, CRL Distribution Points, Freshest CRL,
 *     Authority Info Access and Subject Info Access extensions are
 *     ignored. The Subject Alt Name is decoded for the end-entity
 *     certificate under some conditions (see below). Other extensions
 *     are ignored if non-critical, or imply chain rejection if critical.
 *
 *   - The Subject Alt Name extension is parsed for names of type `dNSName`
 *     when decoding the end-entity certificate, and only if there is a
 *     server name to match. If there is no SAN extension, then the
 *     Common Name from the subjectDN is used. That name matching is
 *     case-insensitive and honours a single starting wildcard (i.e. if
 *     the name in the certificate starts with "`*.`" then this matches
 *     any word as first element). Note: this name matching is performed
 *     also in the "direct trust" model.
 *
 *   - DN matching is byte-to-byte equality (a future version might
 *     include some limited processing for case-insensitive matching and
 *     whitespace normalisation).
 *
 *   - Successful validation produces a public key type but also a set
 *     of allowed usages (`BR_KEYTYPE_KEYX` and/or `BR_KEYTYPE_SIGN`).
 *     The caller is responsible for checking that the key type and
 *     usages are compatible with the expected values (e.g. with the
 *     selected cipher suite, when the client validates the server's
 *     certificate).
 *
 * **Important caveats:**
 *
 *   - The "minimal" engine does not check revocation status. The relevant
 *     extensions are ignored, and CRL or OCSP responses are not gathered
 *     or checked.
 *
 *   - The "minimal" engine does not currently support Name Constraints
 *     (some basic functionality to handle sub-domains may be added in a
 *     later version).
 *
 *   - The decoder is not "validating" in the sense that it won't reject
 *     some certificates with invalid field values when these fields are
 *     not actually processed.
 */

/*
 * X.509 error codes are in the 32..63 range.
 */

/** \brief X.509 status: validation was successful; this is not actually
    an error. */
#define BR_ERR_X509_OK                    32

/** \brief X.509 status: invalid value in an ASN.1 structure. */
#define BR_ERR_X509_INVALID_VALUE         33

/** \brief X.509 status: truncated certificate. */
#define BR_ERR_X509_TRUNCATED             34

/** \brief X.509 status: empty certificate chain (no certificate at all). */
#define BR_ERR_X509_EMPTY_CHAIN           35

/** \brief X.509 status: decoding error: inner element extends beyond
    outer element size. */
#define BR_ERR_X509_INNER_TRUNC           36

/** \brief X.509 status: decoding error: unsupported tag class (application
    or private). */
#define BR_ERR_X509_BAD_TAG_CLASS         37

/** \brief X.509 status: decoding error: unsupported tag value. */
#define BR_ERR_X509_BAD_TAG_VALUE         38

/** \brief X.509 status: decoding error: indefinite length. */
#define BR_ERR_X509_INDEFINITE_LENGTH     39

/** \brief X.509 status: decoding error: extraneous element. */
#define BR_ERR_X509_EXTRA_ELEMENT         40

/** \brief X.509 status: decoding error: unexpected element. */
#define BR_ERR_X509_UNEXPECTED            41

/** \brief X.509 status: decoding error: expected constructed element, but
    is primitive. */
#define BR_ERR_X509_NOT_CONSTRUCTED       42

/** \brief X.509 status: decoding error: expected primitive element, but
    is constructed. */
#define BR_ERR_X509_NOT_PRIMITIVE         43

/** \brief X.509 status: decoding error: BIT STRING length is not multiple
    of 8. */
#define BR_ERR_X509_PARTIAL_BYTE          44

/** \brief X.509 status: decoding error: BOOLEAN value has invalid length. */
#define BR_ERR_X509_BAD_BOOLEAN           45

/** \brief X.509 status: decoding error: value is off-limits. */
#define BR_ERR_X509_OVERFLOW              46

/** \brief X.509 status: invalid distinguished name. */
#define BR_ERR_X509_BAD_DN                47

/** \brief X.509 status: invalid date/time representation. */
#define BR_ERR_X509_BAD_TIME              48

/** \brief X.509 status: certificate contains unsupported features that
    cannot be ignored. */
#define BR_ERR_X509_UNSUPPORTED           49

/** \brief X.509 status: key or signature size exceeds internal limits. */
#define BR_ERR_X509_LIMIT_EXCEEDED        50

/** \brief X.509 status: key type does not match that which was expected. */
#define BR_ERR_X509_WRONG_KEY_TYPE        51

/** \brief X.509 status: signature is invalid. */
#define BR_ERR_X509_BAD_SIGNATURE         52

/** \brief X.509 status: validation time is unknown. */
#define BR_ERR_X509_TIME_UNKNOWN          53

/** \brief X.509 status: certificate is expired or not yet valid. */
#define BR_ERR_X509_EXPIRED               54

/** \brief X.509 status: issuer/subject DN mismatch in the chain. */
#define BR_ERR_X509_DN_MISMATCH           55

/** \brief X.509 status: expected server name was not found in the chain. */
#define BR_ERR_X509_BAD_SERVER_NAME       56

/** \brief X.509 status: unknown critical extension in certificate. */
#define BR_ERR_X509_CRITICAL_EXTENSION    57

/** \brief X.509 status: not a CA, or path length constraint violation */
#define BR_ERR_X509_NOT_CA                58

/** \brief X.509 status: Key Usage extension prohibits intended usage. */
#define BR_ERR_X509_FORBIDDEN_KEY_USAGE   59

/** \brief X.509 status: public key found in certificate is too small. */
#define BR_ERR_X509_WEAK_PUBLIC_KEY       60

/** \brief X.509 status: chain could not be linked to a trust anchor. */
#define BR_ERR_X509_NOT_TRUSTED           62

/**
 * \brief Aggregate structure for public keys.
 */
typedef struct {
	/** \brief Key type: `BR_KEYTYPE_RSA` or `BR_KEYTYPE_EC` */
	unsigned char key_type;
	/** \brief Actual public key. */
	union {
		/** \brief RSA public key. */
		br_rsa_public_key rsa;
		/** \brief EC public key. */
		br_ec_public_key ec;
	} key;
} br_x509_pkey;

/**
 * \brief Distinguished Name (X.500) structure.
 *
 * The DN is DER-encoded.
 */
typedef struct {
	/** \brief Encoded DN data. */
	unsigned char *data;
	/** \brief Encoded DN length (in bytes). */
	size_t len;
} br_x500_name;

/**
 * \brief Trust anchor structure.
 */
typedef struct {
	/** \brief Encoded DN (X.500 name). */
	br_x500_name dn;
	/** \brief Anchor flags (e.g. `BR_X509_TA_CA`). */
	unsigned flags;
	/** \brief Anchor public key. */
	br_x509_pkey pkey;
} br_x509_trust_anchor;

/**
 * \brief Trust anchor flag: CA.
 *
 * A "CA" anchor is deemed fit to verify signatures on certificates.
 * A "non-CA" anchor is accepted only for direct trust (server's
 * certificate name and key match the anchor).
 */
#define BR_X509_TA_CA        0x0001

/*
 * Key type: combination of a basic key type (low 4 bits) and some
 * optional flags.
 *
 * For a public key, the basic key type only is set.
 *
 * For an expected key type, the flags indicate the intended purpose(s)
 * for the key; the basic key type may be set to 0 to indicate that any
 * key type compatible with the indicated purpose is acceptable.
 */
/** \brief Key type: algorithm is RSA. */
#define BR_KEYTYPE_RSA    1
/** \brief Key type: algorithm is EC. */
#define BR_KEYTYPE_EC     2

/**
 * \brief Key type: usage is "key exchange".
 *
 * This value is combined (with bitwise OR) with the algorithm
 * (`BR_KEYTYPE_RSA` or `BR_KEYTYPE_EC`) when informing the X.509
 * validation engine that it should find a public key of that type,
 * fit for key exchanges (e.g. `TLS_RSA_*` and `TLS_ECDH_*` cipher
 * suites).
 */
#define BR_KEYTYPE_KEYX   0x10

/**
 * \brief Key type: usage is "signature".
 *
 * This value is combined (with bitwise OR) with the algorithm
 * (`BR_KEYTYPE_RSA` or `BR_KEYTYPE_EC`) when informing the X.509
 * validation engine that it should find a public key of that type,
 * fit for signatures (e.g. `TLS_ECDHE_*` cipher suites).
 */
#define BR_KEYTYPE_SIGN   0x20

/*
 * start_chain   Called when a new chain is started. If 'server_name'
 *               is not NULL and non-empty, then it is a name that
 *               should be looked for in the EE certificate (in the
 *               SAN extension as dNSName, or in the subjectDN's CN
 *               if there is no SAN extension).
 *               The caller ensures that the provided 'server_name'
 *               pointer remains valid throughout validation.
 *
 * start_cert    Begins a new certificate in the chain. The provided
 *               length is in bytes; this is the total certificate length.
 *
 * append        Get some additional bytes for the current certificate.
 *
 * end_cert      Ends the current certificate.
 *
 * end_chain     Called at the end of the chain. Returned value is
 *               0 on success, or a non-zero error code.
 *
 * get_pkey      Returns the EE certificate public key.
 *
 * For a complete chain, start_chain() and end_chain() are always
 * called. For each certificate, start_cert(), some append() calls, then
 * end_cert() are called, in that order. There may be no append() call
 * at all if the certificate is empty (which is not valid but may happen
 * if the peer sends exactly that).
 *
 * get_pkey() shall return a pointer to a structure that is valid as
 * long as a new chain is not started. This may be a sub-structure
 * within the context for the engine. This function MAY return a valid
 * pointer to a public key even in some cases of validation failure,
 * depending on the validation engine.
 */

/**
 * \brief Class type for an X.509 engine.
 *
 * A certificate chain validation uses a caller-allocated context, which
 * contains the running state for that validation. Methods are called
 * in due order:
 *
 *   - `start_chain()` is called at the start of the validation.
 *   - Certificates are processed one by one, in SSL order (end-entity
 *     comes first). For each certificate, the following methods are
 *     called:
 *
 *       - `start_cert()` at the beginning of the certificate.
 *       - `append()` is called zero, one or more times, to provide
 *         the certificate (possibly in chunks).
 *       - `end_cert()` at the end of the certificate.
 *
 *   - `end_chain()` is called when the last certificate in the chain
 *     was processed.
 *   - `get_pkey()` is called after chain processing, if the chain
 *     validation was successful.
 *
 * A context structure may be reused; the `start_chain()` method shall
 * ensure (re)initialisation.
 */
typedef struct br_x509_class_ br_x509_class;
struct br_x509_class_ {
	/**
	 * \brief X.509 context size, in bytes.
	 */
	size_t context_size;

	/**
	 * \brief Start a new chain.
	 *
	 * This method shall set the vtable (first field) of the context
	 * structure.
	 *
	 * The `server_name`, if not `NULL`, will be considered as a
	 * fully qualified domain name, to be matched against the `dNSName`
	 * elements of the end-entity certificate's SAN extension (if there
	 * is no SAN, then the Common Name from the subjectDN will be used).
	 * If `server_name` is `NULL` then no such matching is performed.
	 *
	 * \param ctx           validation context.
	 * \param server_name   server name to match (or `NULL`).
	 */
	void (*start_chain)(const br_x509_class **ctx,
		const char *server_name);

	/**
	 * \brief Start a new certificate.
	 *
	 * \param ctx      validation context.
	 * \param length   new certificate length (in bytes).
	 */
	void (*start_cert)(const br_x509_class **ctx, uint32_t length);

	/**
	 * \brief Receive some bytes for the current certificate.
	 *
	 * This function may be called several times in succession for
	 * a given certificate. The caller guarantees that for each
	 * call, `len` is not zero, and the sum of all chunk lengths
	 * for a certificate matches the total certificate length which
	 * was provided in the previous `start_cert()` call.
	 *
	 * If the new certificate is empty (no byte at all) then this
	 * function won't be called at all.
	 *
	 * \param ctx   validation context.
	 * \param buf   certificate data chunk.
	 * \param len   certificate data chunk length (in bytes).
	 */
	void (*append)(const br_x509_class **ctx,
		const unsigned char *buf, size_t len);

	/**
	 * \brief Finish the current certificate.
	 *
	 * This function is called when the end of the current certificate
	 * is reached.
	 *
	 * \param ctx   validation context.
	 */
	void (*end_cert)(const br_x509_class **ctx);

	/**
	 * \brief Finish the chain.
	 *
	 * This function is called at the end of the chain. It shall
	 * return either 0 if the validation was successful, or a
	 * non-zero error code. The `BR_ERR_X509_*` constants are
	 * error codes, though other values may be possible.
	 *
	 * \param ctx   validation context.
	 * \return  0 on success, or a non-zero error code.
	 */
	unsigned (*end_chain)(const br_x509_class **ctx);

	/**
	 * \brief Get the resulting end-entity public key.
	 *
	 * The decoded public key is returned. The returned pointer
	 * may be valid only as long as the context structure is
	 * unmodified, i.e. it may cease to be valid if the context
	 * is released or reused.
	 *
	 * This function _may_ return `NULL` if the validation failed.
	 * However, returning a public key does not mean that the
	 * validation was wholly successful; some engines may return
	 * a decoded public key even if the chain did not end on a
	 * trusted anchor.
	 *
	 * If validation succeeded and `usage` is not `NULL`, then
	 * `*usage` is filled with a combination of `BR_KEYTYPE_SIGN`
	 * and/or `BR_KEYTYPE_KEYX` that specifies the validated key
	 * usage types. It is the caller's responsibility to check
	 * that value against the intended use of the public key.
	 *
	 * \param ctx   validation context.
	 * \return  the end-entity public key, or `NULL`.
	 */
	const br_x509_pkey *(*get_pkey)(
		const br_x509_class *const *ctx, unsigned *usages);
};

/**
 * \brief The "known key" X.509 engine structure.
 *
 * The structure contents are opaque (they shall not be accessed directly),
 * except for the first field (the vtable).
 *
 * The "known key" engine returns an externally configured public key,
 * and totally ignores the certificate contents.
 */
typedef struct {
	/** \brief Reference to the context vtable. */
	const br_x509_class *vtable;
#ifndef BR_DOXYGEN_IGNORE
	br_x509_pkey pkey;
	unsigned usages;
#endif
} br_x509_knownkey_context;

/**
 * \brief Class instance for the "known key" X.509 engine.
 */
extern const br_x509_class br_x509_knownkey_vtable;

/**
 * \brief Initialize a "known key" X.509 engine with a known RSA public key.
 *
 * The `usages` parameter indicates the allowed key usages for that key
 * (`BR_KEYTYPE_KEYX` and/or `BR_KEYTYPE_SIGN`).
 *
 * The provided pointers are linked in, not copied, so they must remain
 * valid while the public key may be in usage.
 *
 * \param ctx      context to initialise.
 * \param pk       known public key.
 * \param usages   allowed key usages.
 */
void br_x509_knownkey_init_rsa(br_x509_knownkey_context *ctx,
	const br_rsa_public_key *pk, unsigned usages);

/**
 * \brief Initialize a "known key" X.509 engine with a known EC public key.
 *
 * The `usages` parameter indicates the allowed key usages for that key
 * (`BR_KEYTYPE_KEYX` and/or `BR_KEYTYPE_SIGN`).
 *
 * The provided pointers are linked in, not copied, so they must remain
 * valid while the public key may be in usage.
 *
 * \param ctx      context to initialise.
 * \param pk       known public key.
 * \param usages   allowed key usages.
 */
void br_x509_knownkey_init_ec(br_x509_knownkey_context *ctx,
	const br_ec_public_key *pk, unsigned usages);

#ifndef BR_DOXYGEN_IGNORE
/*
 * The minimal X.509 engine has some state buffers which must be large
 * enough to simultaneously accommodate:
 * -- the public key extracted from the current certificate;
 * -- the signature on the current certificate or on the previous
 *    certificate;
 * -- the public key extracted from the EE certificate.
 *
 * We store public key elements in their raw unsigned big-endian
 * encoding. We want to support up to RSA-4096 with a short (up to 64
 * bits) public exponent, thus a buffer for a public key must have
 * length at least 520 bytes. Similarly, a RSA-4096 signature has length
 * 512 bytes.
 *
 * Though RSA public exponents can formally be as large as the modulus
 * (mathematically, even larger exponents would work, but PKCS#1 forbids
 * them), exponents that do not fit on 32 bits are extremely rare,
 * notably because some widespread implementations (e.g. Microsoft's
 * CryptoAPI) don't support them. Moreover, large public exponent do not
 * seem to imply any tangible security benefit, and they increase the
 * cost of public key operations. The X.509 "minimal" engine will tolerate
 * public exponents of arbitrary size as long as the modulus and the
 * exponent can fit together in the dedicated buffer.
 *
 * EC public keys are shorter than RSA public keys; even with curve
 * NIST P-521 (the largest curve we care to support), a public key is
 * encoded over 133 bytes only.
 */
#define BR_X509_BUFSIZE_KEY   520
#define BR_X509_BUFSIZE_SIG   512
#endif

/**
 * \brief Type for receiving a name element.
 *
 * An array of such structures can be provided to the X.509 decoding
 * engines. If the specified elements are found in the certificate
 * subject DN or the SAN extension, then the name contents are copied
 * as zero-terminated strings into the buffer.
 *
 * The decoder converts TeletexString and BMPString to UTF8String, and
 * ensures that the resulting string is zero-terminated. If the string
 * does not fit in the provided buffer, then the copy is aborted and an
 * error is reported.
 */
typedef struct {
	/**
	 * \brief Element OID.
	 *
	 * For X.500 name elements (to be extracted from the subject DN),
	 * this is the encoded OID for the requested name element; the
	 * first byte shall contain the length of the DER-encoded OID
	 * value, followed by the OID value (for instance, OID 2.5.4.3,
	 * for id-at-commonName, will be `03 55 04 03`). This is
	 * equivalent to full DER encoding with the length but without
	 * the tag.
	 *
	 * For SAN name elements, the first byte (`oid[0]`) has value 0,
	 * followed by another byte that matches the expected GeneralName
	 * tag. Allowed second byte values are then:
	 *
	 *   - 1: `rfc822Name`
	 *
	 *   - 2: `dNSName`
	 *
	 *   - 6: `uniformResourceIdentifier`
	 *
	 *   - 0: `otherName`
	 *
	 * If first and second byte are 0, then this is a SAN element of
	 * type `otherName`; the `oid[]` array should then contain, right
	 * after the two bytes of value 0, an encoded OID (with the same
	 * conventions as for X.500 name elements). If a match is found
	 * for that OID, then the corresponding name element will be
	 * extracted, as long as it is a supported string type.
	 */
	const unsigned char *oid;

	/**
	 * \brief Destination buffer.
	 */
	char *buf;

	/**
	 * \brief Length (in bytes) of the destination buffer.
	 *
	 * The buffer MUST NOT be smaller than 1 byte.
	 */
	size_t len;

	/**
	 * \brief Decoding status.
	 *
	 * Status is 0 if the name element was not found, 1 if it was
	 * found and decoded, or -1 on error. Error conditions include
	 * an unrecognised encoding, an invalid encoding, or a string
	 * too large for the destination buffer.
	 */
	int status;

} br_name_element;

/**
 * \brief The "minimal" X.509 engine structure.
 *
 * The structure contents are opaque (they shall not be accessed directly),
 * except for the first field (the vtable).
 *
 * The "minimal" engine performs a rudimentary but serviceable X.509 path
 * validation.
 */
typedef struct {
	const br_x509_class *vtable;

#ifndef BR_DOXYGEN_IGNORE
	/* Structure for returning the EE public key. */
	br_x509_pkey pkey;

	/* CPU for the T0 virtual machine. */
	struct {
		uint32_t *dp;
		uint32_t *rp;
		const unsigned char *ip;
	} cpu;
	uint32_t dp_stack[32];
	uint32_t rp_stack[32];
	int err;

	/* Server name to match with the SAN / CN of the EE certificate. */
	const char *server_name;

	/* Validated key usages. */
	unsigned char key_usages;

	/* Explicitly set date and time. */
	uint32_t days, seconds;

	/* Current certificate length (in bytes). Set to 0 when the
	   certificate has been fully processed. */
	uint32_t cert_length;

	/* Number of certificates processed so far in the current chain.
	   It is incremented at the end of the processing of a certificate,
	   so it is 0 for the EE. */
	uint32_t num_certs;

	/* Certificate data chunk. */
	const unsigned char *hbuf;
	size_t hlen;

	/* The pad serves as destination for various operations. */
	unsigned char pad[256];

	/* Buffer for EE public key data. */
	unsigned char ee_pkey_data[BR_X509_BUFSIZE_KEY];

	/* Buffer for currently decoded public key. */
	unsigned char pkey_data[BR_X509_BUFSIZE_KEY];

	/* Signature type: signer key type, offset to the hash
	   function OID (in the T0 data block) and hash function
	   output length (TBS hash length). */
	unsigned char cert_signer_key_type;
	uint16_t cert_sig_hash_oid;
	unsigned char cert_sig_hash_len;

	/* Current/last certificate signature. */
	unsigned char cert_sig[BR_X509_BUFSIZE_SIG];
	uint16_t cert_sig_len;

	/* Minimum RSA key length (difference in bytes from 128). */
	int16_t min_rsa_size;

	/* Configured trust anchors. */
	const br_x509_trust_anchor *trust_anchors;
	size_t trust_anchors_num;

	/*
	 * Multi-hasher for the TBS.
	 */
	unsigned char do_mhash;
	br_multihash_context mhash;
	unsigned char tbs_hash[64];

	/*
	 * Simple hasher for the subject/issuer DN.
	 */
	unsigned char do_dn_hash;
	const br_hash_class *dn_hash_impl;
	br_hash_compat_context dn_hash;
	unsigned char current_dn_hash[64];
	unsigned char next_dn_hash[64];
	unsigned char saved_dn_hash[64];

	/*
	 * Name elements to gather.
	 */
	br_name_element *name_elts;
	size_t num_name_elts;

	/*
	 * Public key cryptography implementations (signature verification).
	 */
	br_rsa_pkcs1_vrfy irsa;
	br_ecdsa_vrfy iecdsa;
	const br_ec_impl *iec;
#endif

} br_x509_minimal_context;

/**
 * \brief Class instance for the "minimal" X.509 engine.
 */
extern const br_x509_class br_x509_minimal_vtable;

/**
 * \brief Initialise a "minimal" X.509 engine.
 *
 * The `dn_hash_impl` parameter shall be a hash function internally used
 * to match X.500 names (subject/issuer DN, and anchor names). Any standard
 * hash function may be used, but a collision-resistant hash function is
 * advised.
 *
 * After initialization, some implementations for signature verification
 * (hash functions and signature algorithms) MUST be added.
 *
 * \param ctx                 context to initialise.
 * \param dn_hash_impl        hash function for DN comparisons.
 * \param trust_anchors       trust anchors.
 * \param trust_anchors_num   number of trust anchors.
 */
void br_x509_minimal_init(br_x509_minimal_context *ctx,
	const br_hash_class *dn_hash_impl,
	const br_x509_trust_anchor *trust_anchors, size_t trust_anchors_num);

/**
 * \brief Set a supported hash function in an X.509 "minimal" engine.
 *
 * Hash functions are used with signature verification algorithms.
 * Once initialised (with `br_x509_minimal_init()`), the context must
 * be configured with the hash functions it shall support for that
 * purpose. The hash function identifier MUST be one of the standard
 * hash function identifiers (1 to 6, for MD5, SHA-1, SHA-224, SHA-256,
 * SHA-384 and SHA-512).
 *
 * If `impl` is `NULL`, this _removes_ support for the designated
 * hash function.
 *
 * \param ctx    validation context.
 * \param id     hash function identifier (from 1 to 6).
 * \param impl   hash function implementation (or `NULL`).
 */
static inline void
br_x509_minimal_set_hash(br_x509_minimal_context *ctx,
	int id, const br_hash_class *impl)
{
	br_multihash_setimpl(&ctx->mhash, id, impl);
}

/**
 * \brief Set a RSA signature verification implementation in the X.509
 * "minimal" engine.
 *
 * Once initialised (with `br_x509_minimal_init()`), the context must
 * be configured with the signature verification implementations that
 * it is supposed to support. If `irsa` is `0`, then the RSA support
 * is disabled.
 *
 * \param ctx    validation context.
 * \param irsa   RSA signature verification implementation (or `0`).
 */
static inline void
br_x509_minimal_set_rsa(br_x509_minimal_context *ctx,
	br_rsa_pkcs1_vrfy irsa)
{
	ctx->irsa = irsa;
}

/**
 * \brief Set a ECDSA signature verification implementation in the X.509
 * "minimal" engine.
 *
 * Once initialised (with `br_x509_minimal_init()`), the context must
 * be configured with the signature verification implementations that
 * it is supposed to support.
 *
 * If `iecdsa` is `0`, then this call disables ECDSA support; in that
 * case, `iec` may be `NULL`. Otherwise, `iecdsa` MUST point to a function
 * that verifies ECDSA signatures with format "asn1", and it will use
 * `iec` as underlying elliptic curve support.
 *
 * \param ctx      validation context.
 * \param iec      elliptic curve implementation (or `NULL`).
 * \param iecdsa   ECDSA implementation (or `0`).
 */
static inline void
br_x509_minimal_set_ecdsa(br_x509_minimal_context *ctx,
	const br_ec_impl *iec, br_ecdsa_vrfy iecdsa)
{
	ctx->iecdsa = iecdsa;
	ctx->iec = iec;
}

/**
 * \brief Initialise a "minimal" X.509 engine with default algorithms.
 *
 * This function performs the same job as `br_x509_minimal_init()`, but
 * also sets implementations for RSA, ECDSA, and the standard hash
 * functions.
 *
 * \param ctx                 context to initialise.
 * \param trust_anchors       trust anchors.
 * \param trust_anchors_num   number of trust anchors.
 */
void br_x509_minimal_init_full(br_x509_minimal_context *ctx,
	const br_x509_trust_anchor *trust_anchors, size_t trust_anchors_num);

/**
 * \brief Set the validation time for the X.509 "minimal" engine.
 *
 * The validation time is set as two 32-bit integers, for days and
 * seconds since a fixed epoch:
 *
 *   - Days are counted in a proleptic Gregorian calendar since
 *     January 1st, 0 AD. Year "0 AD" is the one that preceded "1 AD";
 *     it is also traditionally known as "1 BC".
 *
 *   - Seconds are counted since midnight, from 0 to 86400 (a count of
 *     86400 is possible only if a leap second happened).
 *
 * The validation date and time is understood in the UTC time zone.
 *
 * If the validation date and time are not explicitly set, but BearSSL
 * was compiled with support for the system clock on the underlying
 * platform, then the current time will automatically be used. Otherwise,
 * not setting the validation date and time implies a validation
 * failure (except in case of direct trust of the EE key).
 *
 * \param ctx       validation context.
 * \param days      days since January 1st, 0 AD (Gregorian calendar).
 * \param seconds   seconds since midnight (0 to 86400).
 */
static inline void
br_x509_minimal_set_time(br_x509_minimal_context *ctx,
	uint32_t days, uint32_t seconds)
{
	ctx->days = days;
	ctx->seconds = seconds;
}

/**
 * \brief Set the minimal acceptable length for RSA keys (X.509 "minimal"
 * engine).
 *
 * The RSA key length is expressed in bytes. The default minimum key
 * length is 128 bytes, corresponding to 1017 bits. RSA keys shorter
 * than the configured length will be rejected, implying validation
 * failure. This setting applies to keys extracted from certificates
 * (both end-entity, and intermediate CA) but not to "CA" trust anchors.
 *
 * \param ctx           validation context.
 * \param byte_length   minimum RSA key length, **in bytes** (not bits).
 */
static inline void
br_x509_minimal_set_minrsa(br_x509_minimal_context *ctx, int byte_length)
{
	ctx->min_rsa_size = (int16_t)(byte_length - 128);
}

/**
 * \brief Set the name elements to gather.
 *
 * The provided array is linked in the context. The elements are
 * gathered from the EE certificate. If the same element type is
 * requested several times, then the relevant structures will be filled
 * in the order the matching values are encountered in the certificate.
 *
 * \param ctx        validation context.
 * \param elts       array of name element structures to fill.
 * \param num_elts   number of name element structures to fill.
 */
static inline void
br_x509_minimal_set_name_elements(br_x509_minimal_context *ctx,
	br_name_element *elts, size_t num_elts)
{
	ctx->name_elts = elts;
	ctx->num_name_elts = num_elts;
}

/**
 * \brief X.509 decoder context.
 *
 * This structure is _not_ for X.509 validation, but for extracting
 * names and public keys from encoded certificates. Intended usage is
 * to use (self-signed) certificates as trust anchors.
 *
 * Contents are opaque and shall not be accessed directly.
 */
typedef struct {

#ifndef BR_DOXYGEN_IGNORE
	/* Structure for returning the public key. */
	br_x509_pkey pkey;

	/* CPU for the T0 virtual machine. */
	struct {
		uint32_t *dp;
		uint32_t *rp;
		const unsigned char *ip;
	} cpu;
	uint32_t dp_stack[32];
	uint32_t rp_stack[32];
	int err;

	/* The pad serves as destination for various operations. */
	unsigned char pad[256];

	/* Flag set when decoding succeeds. */
	unsigned char decoded;

	/* Validity dates. */
	uint32_t notbefore_days, notbefore_seconds;
	uint32_t notafter_days, notafter_seconds;

	/* The "CA" flag. This is set to true if the certificate contains
	   a Basic Constraints extension that asserts CA status. */
	unsigned char isCA;

	/* DN processing: the subject DN is extracted and pushed to the
	   provided callback. */
	unsigned char copy_dn;
	void *append_dn_ctx;
	void (*append_dn)(void *ctx, const void *buf, size_t len);

	/* Certificate data chunk. */
	const unsigned char *hbuf;
	size_t hlen;

	/* Buffer for decoded public key. */
	unsigned char pkey_data[BR_X509_BUFSIZE_KEY];

	/* Type of key and hash function used in the certificate signature. */
	unsigned char signer_key_type;
	unsigned char signer_hash_id;
#endif

} br_x509_decoder_context;

/**
 * \brief Initialise an X.509 decoder context for processing a new
 * certificate.
 *
 * The `append_dn()` callback (with opaque context `append_dn_ctx`)
 * will be invoked to receive, chunk by chunk, the certificate's
 * subject DN. If `append_dn` is `0` then the subject DN will be
 * ignored.
 *
 * \param ctx             X.509 decoder context to initialise.
 * \param append_dn       DN receiver callback (or `0`).
 * \param append_dn_ctx   context for the DN receiver callback.
 */
void br_x509_decoder_init(br_x509_decoder_context *ctx,
	void (*append_dn)(void *ctx, const void *buf, size_t len),
	void *append_dn_ctx);

/**
 * \brief Push some certificate bytes into a decoder context.
 *
 * If `len` is non-zero, then that many bytes are pushed, from address
 * `data`, into the provided decoder context.
 *
 * \param ctx    X.509 decoder context.
 * \param data   certificate data chunk.
 * \param len    certificate data chunk length (in bytes).
 */
void br_x509_decoder_push(br_x509_decoder_context *ctx,
	const void *data, size_t len);

/**
 * \brief Obtain the decoded public key.
 *
 * Returned value is a pointer to a structure internal to the decoder
 * context; releasing or reusing the decoder context invalidates that
 * structure.
 *
 * If decoding was not finished, or failed, then `NULL` is returned.
 *
 * \param ctx   X.509 decoder context.
 * \return  the public key, or `NULL` on unfinished/error.
 */
static inline br_x509_pkey *
br_x509_decoder_get_pkey(br_x509_decoder_context *ctx)
{
	if (ctx->decoded && ctx->err == 0) {
		return &ctx->pkey;
	} else {
		return NULL;
	}
}

/**
 * \brief Get decoder error status.
 *
 * If no error was reported yet but the certificate decoding is not
 * finished, then the error code is `BR_ERR_X509_TRUNCATED`. If decoding
 * was successful, then 0 is returned.
 *
 * \param ctx   X.509 decoder context.
 * \return  0 on successful decoding, or a non-zero error code.
 */
static inline int
br_x509_decoder_last_error(br_x509_decoder_context *ctx)
{
	if (ctx->err != 0) {
		return ctx->err;
	}
	if (!ctx->decoded) {
		return BR_ERR_X509_TRUNCATED;
	}
	return 0;
}

/**
 * \brief Get the "isCA" flag from an X.509 decoder context.
 *
 * This flag is set if the decoded certificate claims to be a CA through
 * a Basic Constraints extension. This flag should not be read before
 * decoding completed successfully.
 *
 * \param ctx   X.509 decoder context.
 * \return  the "isCA" flag.
 */
static inline int
br_x509_decoder_isCA(br_x509_decoder_context *ctx)
{
	return ctx->isCA;
}

/**
 * \brief Get the issuing CA key type (type of algorithm used to sign the
 * decoded certificate).
 *
 * This is `BR_KEYTYPE_RSA` or `BR_KEYTYPE_EC`. The value 0 is returned
 * if the signature type was not recognised.
 *
 * \param ctx   X.509 decoder context.
 * \return  the issuing CA key type.
 */
static inline int
br_x509_decoder_get_signer_key_type(br_x509_decoder_context *ctx)
{
	return ctx->signer_key_type;
}

/**
 * \brief Get the identifier for the hash function used to sign the decoded
 * certificate.
 *
 * This is 0 if the hash function was not recognised.
 *
 * \param ctx   X.509 decoder context.
 * \return  the signature hash function identifier.
 */
static inline int
br_x509_decoder_get_signer_hash_id(br_x509_decoder_context *ctx)
{
	return ctx->signer_hash_id;
}

/**
 * \brief Type for an X.509 certificate (DER-encoded).
 */
typedef struct {
	/** \brief The DER-encoded certificate data. */
	unsigned char *data;
	/** \brief The DER-encoded certificate length (in bytes). */
	size_t data_len;
} br_x509_certificate;

/**
 * \brief Private key decoder context.
 *
 * The private key decoder recognises RSA and EC private keys, either in
 * their raw, DER-encoded format, or wrapped in an unencrypted PKCS#8
 * archive (again DER-encoded).
 *
 * Structure contents are opaque and shall not be accessed directly.
 */
typedef struct {
#ifndef BR_DOXYGEN_IGNORE
	/* Structure for returning the private key. */
	union {
		br_rsa_private_key rsa;
		br_ec_private_key ec;
	} key;

	/* CPU for the T0 virtual machine. */
	struct {
		uint32_t *dp;
		uint32_t *rp;
		const unsigned char *ip;
	} cpu;
	uint32_t dp_stack[32];
	uint32_t rp_stack[32];
	int err;

	/* Private key data chunk. */
	const unsigned char *hbuf;
	size_t hlen;

	/* The pad serves as destination for various operations. */
	unsigned char pad[256];

	/* Decoded key type; 0 until decoding is complete. */
	unsigned char key_type;

	/* Buffer for the private key elements. It shall be large enough
	   to accommodate all elements for a RSA-4096 private key (roughly
	   five 2048-bit integers, possibly a bit more). */
	unsigned char key_data[3 * BR_X509_BUFSIZE_SIG];
#endif
} br_skey_decoder_context;

/**
 * \brief Initialise a private key decoder context.
 *
 * \param ctx   key decoder context to initialise.
 */
void br_skey_decoder_init(br_skey_decoder_context *ctx);

/**
 * \brief Push some data bytes into a private key decoder context.
 *
 * If `len` is non-zero, then that many data bytes, starting at address
 * `data`, are pushed into the decoder.
 *
 * \param ctx    key decoder context.
 * \param data   private key data chunk.
 * \param len    private key data chunk length (in bytes).
 */
void br_skey_decoder_push(br_skey_decoder_context *ctx,
	const void *data, size_t len);

/**
 * \brief Get the decoding status for a private key.
 *
 * Decoding status is 0 on success, or a non-zero error code. If the
 * decoding is unfinished when this function is called, then the
 * status code `BR_ERR_X509_TRUNCATED` is returned.
 *
 * \param ctx   key decoder context.
 * \return  0 on successful decoding, or a non-zero error code.
 */
static inline int
br_skey_decoder_last_error(const br_skey_decoder_context *ctx)
{
	if (ctx->err != 0) {
		return ctx->err;
	}
	if (ctx->key_type == 0) {
		return BR_ERR_X509_TRUNCATED;
	}
	return 0;
}

/**
 * \brief Get the decoded private key type.
 *
 * Private key type is `BR_KEYTYPE_RSA` or `BR_KEYTYPE_EC`. If decoding is
 * not finished or failed, then 0 is returned.
 *
 * \param ctx   key decoder context.
 * \return  decoded private key type, or 0.
 */
static inline int
br_skey_decoder_key_type(const br_skey_decoder_context *ctx)
{
	if (ctx->err == 0) {
		return ctx->key_type;
	} else {
		return 0;
	}
}

/**
 * \brief Get the decoded RSA private key.
 *
 * This function returns `NULL` if the decoding failed, or is not
 * finished, or the key is not RSA. The returned pointer references
 * structures within the context that can become invalid if the context
 * is reused or released.
 *
 * \param ctx   key decoder context.
 * \return  decoded RSA private key, or `NULL`.
 */
static inline const br_rsa_private_key *
br_skey_decoder_get_rsa(const br_skey_decoder_context *ctx)
{
	if (ctx->err == 0 && ctx->key_type == BR_KEYTYPE_RSA) {
		return &ctx->key.rsa;
	} else {
		return NULL;
	}
}

/**
 * \brief Get the decoded EC private key.
 *
 * This function returns `NULL` if the decoding failed, or is not
 * finished, or the key is not EC. The returned pointer references
 * structures within the context that can become invalid if the context
 * is reused or released.
 *
 * \param ctx   key decoder context.
 * \return  decoded EC private key, or `NULL`.
 */
static inline const br_ec_private_key *
br_skey_decoder_get_ec(const br_skey_decoder_context *ctx)
{
	if (ctx->err == 0 && ctx->key_type == BR_KEYTYPE_EC) {
		return &ctx->key.ec;
	} else {
		return NULL;
	}
}

/**
 * \brief Encode an RSA private key (raw DER format).
 *
 * This function encodes the provided key into the "raw" format specified
 * in PKCS#1 (RFC 8017, Appendix C, type `RSAPrivateKey`), with DER
 * encoding rules.
 *
 * The key elements are:
 *
 *  - `sk`: the private key (`p`, `q`, `dp`, `dq` and `iq`)
 *
 *  - `pk`: the public key (`n` and `e`)
 *
 *  - `d` (size: `dlen` bytes): the private exponent
 *
 * The public key elements, and the private exponent `d`, can be
 * recomputed from the private key (see `br_rsa_compute_modulus()`,
 * `br_rsa_compute_pubexp()` and `br_rsa_compute_privexp()`).
 *
 * If `dest` is not `NULL`, then the encoded key is written at that
 * address, and the encoded length (in bytes) is returned. If `dest` is
 * `NULL`, then nothing is written, but the encoded length is still
 * computed and returned.
 *
 * \param dest   the destination buffer (or `NULL`).
 * \param sk     the RSA private key.
 * \param pk     the RSA public key.
 * \param d      the RSA private exponent.
 * \param dlen   the RSA private exponent length (in bytes).
 * \return  the encoded key length (in bytes).
 */
size_t br_encode_rsa_raw_der(void *dest, const br_rsa_private_key *sk,
	const br_rsa_public_key *pk, const void *d, size_t dlen);

/**
 * \brief Encode an RSA private key (PKCS#8 DER format).
 *
 * This function encodes the provided key into the PKCS#8 format
 * (RFC 5958, type `OneAsymmetricKey`). It wraps around the "raw DER"
 * format for the RSA key, as implemented by `br_encode_rsa_raw_der()`.
 *
 * The key elements are:
 *
 *  - `sk`: the private key (`p`, `q`, `dp`, `dq` and `iq`)
 *
 *  - `pk`: the public key (`n` and `e`)
 *
 *  - `d` (size: `dlen` bytes): the private exponent
 *
 * The public key elements, and the private exponent `d`, can be
 * recomputed from the private key (see `br_rsa_compute_modulus()`,
 * `br_rsa_compute_pubexp()` and `br_rsa_compute_privexp()`).
 *
 * If `dest` is not `NULL`, then the encoded key is written at that
 * address, and the encoded length (in bytes) is returned. If `dest` is
 * `NULL`, then nothing is written, but the encoded length is still
 * computed and returned.
 *
 * \param dest   the destination buffer (or `NULL`).
 * \param sk     the RSA private key.
 * \param pk     the RSA public key.
 * \param d      the RSA private exponent.
 * \param dlen   the RSA private exponent length (in bytes).
 * \return  the encoded key length (in bytes).
 */
size_t br_encode_rsa_pkcs8_der(void *dest, const br_rsa_private_key *sk,
	const br_rsa_public_key *pk, const void *d, size_t dlen);

/**
 * \brief Encode an EC private key (raw DER format).
 *
 * This function encodes the provided key into the "raw" format specified
 * in RFC 5915 (type `ECPrivateKey`), with DER encoding rules.
 *
 * The private key is provided in `sk`, the public key being `pk`. If
 * `pk` is `NULL`, then the encoded key will not include the public key
 * in its `publicKey` field (which is nominally optional).
 *
 * If `dest` is not `NULL`, then the encoded key is written at that
 * address, and the encoded length (in bytes) is returned. If `dest` is
 * `NULL`, then nothing is written, but the encoded length is still
 * computed and returned.
 *
 * If the key cannot be encoded (e.g. because there is no known OBJECT
 * IDENTIFIER for the used curve), then 0 is returned.
 *
 * \param dest   the destination buffer (or `NULL`).
 * \param sk     the EC private key.
 * \param pk     the EC public key (or `NULL`).
 * \return  the encoded key length (in bytes), or 0.
 */
size_t br_encode_ec_raw_der(void *dest,
	const br_ec_private_key *sk, const br_ec_public_key *pk);

/**
 * \brief Encode an EC private key (PKCS#8 DER format).
 *
 * This function encodes the provided key into the PKCS#8 format
 * (RFC 5958, type `OneAsymmetricKey`). The curve is identified
 * by an OID provided as parameters to the `privateKeyAlgorithm`
 * field. The private key value (contents of the `privateKey` field)
 * contains the DER encoding of the `ECPrivateKey` type defined in
 * RFC 5915, without the `parameters` field (since they would be
 * redundant with the information in `privateKeyAlgorithm`).
 *
 * The private key is provided in `sk`, the public key being `pk`. If
 * `pk` is not `NULL`, then the encoded public key is included in the
 * `publicKey` field of the private key value (but not in the `publicKey`
 * field of the PKCS#8 `OneAsymmetricKey` wrapper).
 *
 * If `dest` is not `NULL`, then the encoded key is written at that
 * address, and the encoded length (in bytes) is returned. If `dest` is
 * `NULL`, then nothing is written, but the encoded length is still
 * computed and returned.
 *
 * If the key cannot be encoded (e.g. because there is no known OBJECT
 * IDENTIFIER for the used curve), then 0 is returned.
 *
 * \param dest   the destination buffer (or `NULL`).
 * \param sk     the EC private key.
 * \param pk     the EC public key (or `NULL`).
 * \return  the encoded key length (in bytes), or 0.
 */
size_t br_encode_ec_pkcs8_der(void *dest,
	const br_ec_private_key *sk, const br_ec_public_key *pk);

/**
 * \brief PEM banner for RSA private key (raw).
 */
#define BR_ENCODE_PEM_RSA_RAW      "RSA PRIVATE KEY"

/**
 * \brief PEM banner for EC private key (raw).
 */
#define BR_ENCODE_PEM_EC_RAW       "EC PRIVATE KEY"

/**
 * \brief PEM banner for an RSA or EC private key in PKCS#8 format.
 */
#define BR_ENCODE_PEM_PKCS8        "PRIVATE KEY"

#ifdef __cplusplus
}
#endif

#endif
