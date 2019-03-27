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

#include "bearssl.h"

/*
 * A "profile" is an initialisation function for a SSL context, that
 * configures a list of cipher suites and algorithm implementations.
 * While BearSSL comes with a few predefined profiles, you might one
 * to define you own, using the example below as guidance.
 *
 * Each individual initialisation call sets a parameter or an algorithm
 * support. Setting a specific algorithm pulls in the implementation of
 * that algorithm in the compiled binary, as per static linking
 * behaviour. Removing some of this calls will then reduce total code
 * footprint, but also mechanically prevents some features to be
 * supported (protocol versions and cipher suites).
 *
 * The two below define profiles for the client and the server contexts,
 * respectively. Of course, in a typical size-constrained application,
 * you would use one or the other, not both, to avoid pulling in code
 * for both.
 */

void
example_client_profile(br_ssl_client_context *cc
	/* and possibly some other arguments */)
{
	/*
	 * A list of cipher suites, by preference (first is most
	 * preferred). The list below contains all cipher suites supported
	 * by BearSSL; trim it done to your needs.
	 */
	static const uint16_t suites[] = {
		BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
		BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
		BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
		BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
		BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
		BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256,
		BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
		BR_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384,
		BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384,
		BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,
		BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
		BR_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA,
		BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
		BR_TLS_ECDH_ECDSA_WITH_AES_128_GCM_SHA256,
		BR_TLS_ECDH_RSA_WITH_AES_128_GCM_SHA256,
		BR_TLS_ECDH_ECDSA_WITH_AES_256_GCM_SHA384,
		BR_TLS_ECDH_RSA_WITH_AES_256_GCM_SHA384,
		BR_TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA256,
		BR_TLS_ECDH_RSA_WITH_AES_128_CBC_SHA256,
		BR_TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA384,
		BR_TLS_ECDH_RSA_WITH_AES_256_CBC_SHA384,
		BR_TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA,
		BR_TLS_ECDH_RSA_WITH_AES_128_CBC_SHA,
		BR_TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA,
		BR_TLS_ECDH_RSA_WITH_AES_256_CBC_SHA,
		BR_TLS_RSA_WITH_AES_128_GCM_SHA256,
		BR_TLS_RSA_WITH_AES_256_GCM_SHA384,
		BR_TLS_RSA_WITH_AES_128_CBC_SHA256,
		BR_TLS_RSA_WITH_AES_256_CBC_SHA256,
		BR_TLS_RSA_WITH_AES_128_CBC_SHA,
		BR_TLS_RSA_WITH_AES_256_CBC_SHA,
		BR_TLS_ECDHE_ECDSA_WITH_3DES_EDE_CBC_SHA,
		BR_TLS_ECDHE_RSA_WITH_3DES_EDE_CBC_SHA,
		BR_TLS_ECDH_ECDSA_WITH_3DES_EDE_CBC_SHA,
		BR_TLS_ECDH_RSA_WITH_3DES_EDE_CBC_SHA,
		BR_TLS_RSA_WITH_3DES_EDE_CBC_SHA
	};

	/*
	 * Client context must be cleared at some point. This sets
	 * every value and pointer to 0 or NULL.
	 */
	br_ssl_client_zero(cc);

	/*
	 * Define minimum and maximum protocol versions. Supported
	 * versions are:
	 *    BR_TLS10    TLS 1.0
	 *    BR_TLS11    TLS 1.1
	 *    BR_TLS12    TLS 1.2
	 */
	br_ssl_engine_set_versions(&cc->eng, BR_TLS10, BR_TLS12);

	/*
	 * Set the PRF implementation(s).
	 * For TLS 1.0 and 1.1, the "prf10" is needed.
	 * For TLS 1.2, this depends on the cipher suite:
	 *  -- cipher suites with a name ending in "SHA384" need "prf_sha384";
	 *  -- all others need "prf_sha256".
	 *
	 * Note that a cipher suite like TLS_RSA_WITH_AES_128_CBC_SHA will
	 * use SHA-1 for the per-record MAC (that's what the final "SHA"
	 * means), but still SHA-256 for the PRF when selected along with
	 * the TLS-1.2 protocol version.
	 */
	br_ssl_engine_set_prf10(&cc->eng, &br_tls10_prf);
	br_ssl_engine_set_prf_sha256(&cc->eng, &br_tls12_sha256_prf);
	br_ssl_engine_set_prf_sha384(&cc->eng, &br_tls12_sha384_prf);

	/*
	 * Set hash functions for the engine. Required hash functions
	 * depend on the protocol and cipher suite:
	 *
	 * -- TLS 1.0 and 1.1 require both MD5 and SHA-1.
	 * -- With TLS 1.2, cipher suites with a name ending in "SHA384"
	 *    require SHA-384.
	 * -- With TLS 1.2, cipher suites with a name ending in "SHA256"
	 *    require SHA-256.
	 * -- With TLS 1.2, cipher suites with a name ending in "SHA"
	 *    require both SHA-256 and SHA-1.
	 *
	 * Moreover, these hash functions are also used to compute
	 * hashes supporting signatures on the server side (for ECDHE_*
	 * cipher suites), and on the client side (for client
	 * certificates, except in the case of full static ECDH). In TLS
	 * 1.0 and 1.1, SHA-1 (and also MD5) will be used, but with TLS
	 * 1.2 these hash functions are negotiated between client and
	 * server; SHA-256 and/or SHA-384 should be sufficient in
	 * practice.
	 *
	 * Note that with current implementations, SHA-224 and SHA-256
	 * share the same file, so if you use one, you may have the other
	 * one with no additional overhead. Similarly, SHA-384 and SHA-512
	 * share the same implementation code.
	 */
	br_ssl_engine_set_hash(&cc->eng, br_md5_ID, &br_md5_vtable);
	br_ssl_engine_set_hash(&cc->eng, br_sha1_ID, &br_sha1_vtable);
	br_ssl_engine_set_hash(&cc->eng, br_sha224_ID, &br_sha224_vtable);
	br_ssl_engine_set_hash(&cc->eng, br_sha256_ID, &br_sha256_vtable);
	br_ssl_engine_set_hash(&cc->eng, br_sha384_ID, &br_sha384_vtable);
	br_ssl_engine_set_hash(&cc->eng, br_sha512_ID, &br_sha512_vtable);

	/*
	 * Set the cipher suites. All specified cipher suite MUST be
	 * supported, and the relevant algorithms MUST have been
	 * configured (failure to provide needed implementations may
	 * trigger unwanted behaviours like segfaults or overflows).
	 */
	br_ssl_engine_set_suites(&cc->eng, suites,
		(sizeof suites) / (sizeof suites[0]));

	/*
	 * Public-key algorithm implementations.
	 *
	 * -- RSA public core ("rsapub") is needed for "RSA" key exchange
	 *    (cipher suites whose name starts with TLS_RSA).
	 *
	 * -- RSA signature verification ("rsavrfy") is needed for
	 *    "ECDHE_RSA" cipher suites (not ECDH_RSA).
	 *
	 * -- Elliptic curve implementation ("ec") is needed for cipher
	 *    suites that use elliptic curves (both "ECDH" and "ECDHE"
	 *    cipher suites).
	 *
	 * -- ECDSA signature verification is needed for "ECDHE_ECDSA"
	 *    cipher suites (but not for ECDHE_RSA, ECDH_ECDSA or ECDH_RSA).
	 *
	 * Normally, you use the "default" implementations, obtained
	 * through relevant function calls. These functions return
	 * implementations that are deemed "best" for the current
	 * platform, where "best" means "fastest within constant-time
	 * implementations". Selecting the default implementation is a
	 * mixture of compile-time and runtime checks.
	 *
	 * Nevertheless, specific implementations may be selected
	 * explicitly, e.g. to use code which is slower but with a
	 * smaller footprint.
	 *
	 * The RSA code comes in three variants, called "i15", "i31" and
	 * "i32". The "i31" code is somewhat faster than the "i32" code.
	 * Usually, "i31" is faster than "i15", except on some specific
	 * architectures (ARM Cortex M0, M0+, M1 and M3) where the "i15"
	 * should be preferred (the "i15" code is constant-time, while
	 * the "i31" is not, and the "i15" code is faster anyway).
	 *
	 * ECDSA code also comes in "i15" and "i31" variants. As in the
	 * case of RSA, the "i31" code is faster, except on the small
	 * ARM Cortex M, where the "i15" code is faster and safer.
	 *
	 * There are no less than 10 elliptic curve implementations:
	 *
	 *  - ec_c25519_i15, ec_c25519_i31, ec_c25519_m15 and ec_c25519_m31
	 *    implement Curve25519.
	 *
	 *  - ec_p256_m15 and ec_p256_m31 implement NIST curve P-256.
	 *
	 *  - ec_prime_i15 and ec_prime_i31 implement NIST curves P-256,
	 *    P-384 and P-521.
	 *
	 *  - ec_all_m15 is an aggregate implementation that uses
	 *    ec_c25519_m15, ec_p256_m15 and ec_prime_i15.
	 *
	 *  - ec_all_m31 is an aggregate implementation that uses
	 *    ec_c25519_m31, ec_p256_m31 and ec_prime_i31.
	 *
	 * For a given curve, "m15" is faster than "i15" (but possibly
	 * with a larger code footprint) and "m31" is faster than "i31"
	 * (there again with a larger code footprint). For best
	 * performance, use ec_all_m31, except on the small ARM Cortex M
	 * where ec_all_m15 should be used. Referencing the other
	 * implementations directly will result in smaller code, but
	 * support for fewer curves and possibly lower performance.
	 */
	br_ssl_client_set_default_rsapub(cc);
	br_ssl_engine_set_default_rsavrfy(&cc->eng);
	br_ssl_engine_set_default_ecdsa(&cc->eng);
	/* Alternate: set implementations explicitly.
	br_ssl_client_set_rsapub(cc, &br_rsa_i31_public);
	br_ssl_client_set_rsavrfy(cc, &br_rsa_i31_pkcs1_vrfy);
	br_ssl_engine_set_ec(&cc->eng, &br_ec_all_m31);
	br_ssl_engine_set_ecdsa(&cc->eng, &br_ecdsa_i31_vrfy_asn1);
	*/

	/*
	 * Record handler:
	 * -- Cipher suites in AES_128_CBC, AES_256_CBC and 3DES_EDE_CBC
	 *    need the CBC record handler ("set_cbc").
	 * -- Cipher suites in AES_128_GCM and AES_256_GCM need the GCM
	 *    record handler ("set_gcm").
	 * -- Cipher suites in CHACHA20_POLY1305 need the ChaCha20+Poly1305
	 *    record handler ("set_chapol").
	 */
	br_ssl_engine_set_cbc(&cc->eng,
		&br_sslrec_in_cbc_vtable,
		&br_sslrec_out_cbc_vtable);
	br_ssl_engine_set_gcm(&cc->eng,
		&br_sslrec_in_gcm_vtable,
		&br_sslrec_out_gcm_vtable);
	br_ssl_engine_set_chapol(&cc->eng,
		&br_sslrec_in_chapol_vtable,
		&br_sslrec_out_chapol_vtable);

	/*
	 * Symmetric encryption:
	 * -- AES_128_CBC and AES_256_CBC require an "aes_cbc" implementation
	 *    (actually two implementations, for encryption and decryption).
	 * -- 3DES_EDE_CBC requires a "des_cbc" implementation
	 *    (actually two implementations, for encryption and decryption).
	 * -- AES_128_GCM and AES_256_GCM require an "aes_ctr" imeplementation
	 *    and also a GHASH implementation.
	 *
	 * Two 3DES implementations are provided:
	 *
	 *    des_tab     Classical table-based implementation; it is
	 *                not constant-time.
	 *
	 *    dest_ct     Constant-time DES/3DES implementation. It is
	 *                slower than des_tab.
	 *
	 * Four AES implementations are provided:
	 *
	 *    aes_ct      Constant-time AES implementation, for 32-bit
	 *                systems.
	 *
	 *    aes_ct64    Constant-time AES implementation, for 64-bit
	 *                systems. It actually also runs on 32-bit systems,
	 *                but, on such systems, it yields larger code and
	 *                slightly worse performance. On 64-bit systems,
	 *                aes_ct64 is about twice faster than aes_ct for
	 *                CTR processing (GCM encryption and decryption),
	 *                and for CBC (decryption only).
	 *
	 *    aes_small   Smallest implementation provided, but also the
	 *                slowest, and it is not constant-time. Use it
	 *                only if desperate for code size.
	 *
	 *    aes_big     Classical table-based AES implementation. This
	 *                is decently fast and still resonably compact,
	 *                but it is not constant-time.
	 *
	 *    aes_x86ni   Very fast implementation that uses the AES-NI
	 *                opcodes on recent x86 CPU. But it may not be
	 *                compiled in the library if the compiler or
	 *                architecture is not supported; and the CPU
	 *                may also not support the opcodes. Selection
	 *                functions are provided to test for availability
	 *                of the code and the opcodes.
	 *
	 * Whether having constant-time implementations is absolutely
	 * required for security depends on the context (in particular
	 * whether the target architecture actually has cache memory),
	 * and while side-channel analysis for non-constant-time AES
	 * code has been demonstrated in lab conditions, it certainly
	 * does not apply to all actual usages, and it has never been
	 * spotted in the wild. It is still considered cautious to use
	 * constant-time code by default, and to consider the other
	 * implementations only if duly measured performance issues make
	 * it mandatory.
	 */
	br_ssl_engine_set_aes_cbc(&cc->eng,
		&br_aes_ct_cbcenc_vtable,
		&br_aes_ct_cbcdec_vtable);
	br_ssl_engine_set_aes_ctr(&cc->eng,
		&br_aes_ct_ctr_vtable);
	/* Alternate: aes_ct64
	br_ssl_engine_set_aes_cbc(&cc->eng,
		&br_aes_ct64_cbcenc_vtable,
		&br_aes_ct64_cbcdec_vtable);
	br_ssl_engine_set_aes_ctr(&cc->eng,
		&br_aes_ct64_ctr_vtable);
	*/
	/* Alternate: aes_small
	br_ssl_engine_set_aes_cbc(&cc->eng,
		&br_aes_small_cbcenc_vtable,
		&br_aes_small_cbcdec_vtable);
	br_ssl_engine_set_aes_ctr(&cc->eng,
		&br_aes_small_ctr_vtable);
	*/
	/* Alternate: aes_big
	br_ssl_engine_set_aes_cbc(&cc->eng,
		&br_aes_big_cbcenc_vtable,
		&br_aes_big_cbcdec_vtable);
	br_ssl_engine_set_aes_ctr(&cc->eng,
		&br_aes_big_ctr_vtable);
	*/
	br_ssl_engine_set_des_cbc(&cc->eng,
		&br_des_ct_cbcenc_vtable,
		&br_des_ct_cbcdec_vtable);
	/* Alternate: des_tab
	br_ssl_engine_set_des_cbc(&cc->eng,
		&br_des_tab_cbcenc_vtable,
		&br_des_tab_cbcdec_vtable);
	*/

	/*
	 * GHASH is needed for AES_128_GCM and AES_256_GCM. Three
	 * implementations are provided:
	 *
	 *    ctmul     Uses 32-bit multiplications with a 64-bit result.
	 *
	 *    ctmul32   Uses 32-bit multiplications with a 32-bit result.
	 *
	 *    ctmul64   Uses 64-bit multiplications with a 64-bit result.
	 *
	 * On 64-bit platforms, ctmul64 is the smallest and fastest of
	 * the three. On 32-bit systems, ctmul should be preferred. The
	 * ctmul32 implementation is meant to be used for the specific
	 * 32-bit systems that do not have a 32x32->64 multiplier (i.e.
	 * the ARM Cortex-M0 and Cortex-M0+).
	 *
	 * These implementations are all constant-time as long as the
	 * underlying multiplication opcode is constant-time (which is
	 * true for all modern systems, but not for older architectures
	 * such that ARM9 or 80486).
	 */
	br_ssl_engine_set_ghash(&cc->eng,
		&br_ghash_ctmul);
	/* Alternate: ghash_ctmul32
	br_ssl_engine_set_ghash(&cc->eng,
		&br_ghash_ctmul32);
	*/
	/* Alternate: ghash_ctmul64
	br_ssl_engine_set_ghash(&cc->eng,
		&br_ghash_ctmul64);
	*/

#if 0
	/*
	 * For a client, the normal case is to validate the server
	 * certificate with regards to a set of trust anchors. This
	 * entails using a br_x509_minimal_context structure, configured
	 * with the relevant algorithms, as shown below.
	 *
	 * Alternatively, the client could "know" the intended server
	 * public key through an out-of-band mechanism, in which case
	 * a br_x509_knownkey_context is appropriate, for a much reduced
	 * code footprint.
	 *
	 * We assume here that the following extra parameters have been
	 * provided:
	 *
	 *   xc                  engine context (br_x509_minimal_context *)
	 *   trust_anchors       trust anchors (br_x509_trust_anchor *)
	 *   trust_anchors_num   number of trust anchors (size_t)
	 */

	/*
	 * The X.509 engine needs a hash function for processing the
	 * subject and issuer DN of certificates and trust anchors. Any
	 * supported hash function is appropriate; here we use SHA-256.
	 * The trust an
	 */
	br_x509_minimal_init(xc, &br_sha256_vtable,
		trust_anchors, trust_anchors_num);

	/*
	 * Set suites and asymmetric crypto implementations. We use the
	 * "i31" code for RSA (it is somewhat faster than the "i32"
	 * implementation). These implementations are used for
	 * signature verification on certificates, but not for the
	 * SSL-specific usage of the server's public key. For instance,
	 * if the server has an EC public key but the rest of the chain
	 * (intermediate CA, root...) use RSA, then you would need only
	 * the RSA verification function below.
	 */
	br_x509_minimal_set_rsa(xc, &br_rsa_i31_pkcs1_vrfy);
	br_x509_minimal_set_ecdsa(xc,
		&br_ec_prime_i31, &br_ecdsa_i31_vrfy_asn1);

	/*
	 * Set supported hash functions. These are for signatures on
	 * certificates. There again, you only need the hash functions
	 * that are actually used in certificates, but if a given
	 * function was included for the SSL engine, you may as well
	 * add it here.
	 *
	 * Note: the engine explicitly rejects signatures that use MD5.
	 * Thus, there is no need for MD5 here.
	 */
	br_ssl_engine_set_hash(xc, br_sha1_ID, &br_sha1_vtable);
	br_ssl_engine_set_hash(xc, br_sha224_ID, &br_sha224_vtable);
	br_ssl_engine_set_hash(xc, br_sha256_ID, &br_sha256_vtable);
	br_ssl_engine_set_hash(xc, br_sha384_ID, &br_sha384_vtable);
	br_ssl_engine_set_hash(xc, br_sha512_ID, &br_sha512_vtable);

	/*
	 * Link the X.509 engine in the SSL engine.
	 */
	br_ssl_engine_set_x509(&cc->eng, &xc->vtable);
#endif
}

/*
 * Example server profile. Most of it is shared with the client
 * profile, so see the comments in the client function for details.
 *
 * This example function assumes a server with a (unique) RSA private
 * key, so the list of cipher suites is trimmed down for RSA.
 */
void
example_server_profile(br_ssl_server_context *cc,
	const br_x509_certificate *chain, size_t chain_len,
	const br_rsa_private_key *sk)
{
	static const uint16_t suites[] = {
		BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
		BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
		BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
		BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384,
		BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
		BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
		BR_TLS_RSA_WITH_AES_128_GCM_SHA256,
		BR_TLS_RSA_WITH_AES_256_GCM_SHA384,
		BR_TLS_RSA_WITH_AES_128_CBC_SHA256,
		BR_TLS_RSA_WITH_AES_256_CBC_SHA256,
		BR_TLS_RSA_WITH_AES_128_CBC_SHA,
		BR_TLS_RSA_WITH_AES_256_CBC_SHA,
		BR_TLS_ECDHE_RSA_WITH_3DES_EDE_CBC_SHA,
		BR_TLS_RSA_WITH_3DES_EDE_CBC_SHA
	};

	br_ssl_server_zero(cc);
	br_ssl_engine_set_versions(&cc->eng, BR_TLS10, BR_TLS12);

	br_ssl_engine_set_prf10(&cc->eng, &br_tls10_prf);
	br_ssl_engine_set_prf_sha256(&cc->eng, &br_tls12_sha256_prf);
	br_ssl_engine_set_prf_sha384(&cc->eng, &br_tls12_sha384_prf);

	/*
	 * Apart from the requirements listed in the client side, these
	 * hash functions are also used by the server to compute its
	 * signature on ECDHE parameters. Which functions are needed
	 * depends on what the client may support; furthermore, the
	 * client may fail to send the relevant extension, in which
	 * case the server will default to whatever it can (as per the
	 * standard, it should be SHA-1 in that case).
	 */
	br_ssl_engine_set_hash(&cc->eng, br_md5_ID, &br_md5_vtable);
	br_ssl_engine_set_hash(&cc->eng, br_sha1_ID, &br_sha1_vtable);
	br_ssl_engine_set_hash(&cc->eng, br_sha224_ID, &br_sha224_vtable);
	br_ssl_engine_set_hash(&cc->eng, br_sha256_ID, &br_sha256_vtable);
	br_ssl_engine_set_hash(&cc->eng, br_sha384_ID, &br_sha384_vtable);
	br_ssl_engine_set_hash(&cc->eng, br_sha512_ID, &br_sha512_vtable);

	br_ssl_engine_set_suites(&cc->eng, suites,
		(sizeof suites) / (sizeof suites[0]));

	/*
	 * Elliptic curve implementation is used for ECDHE suites (but
	 * not for ECDH).
	 */
	br_ssl_engine_set_ec(&cc->eng, &br_ec_prime_i31);

	/*
	 * Set the "server policy": handler for the certificate chain
	 * and private key operations. Here, we indicate that the RSA
	 * private key is fit for both signing and decrypting, and we
	 * provide the two relevant implementations.

	 * BR_KEYTYPE_KEYX allows TLS_RSA_*, BR_KEYTYPE_SIGN allows
	 * TLS_ECDHE_RSA_*.
	 */
	br_ssl_server_set_single_rsa(cc, chain, chain_len, sk,
		BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN,
		br_rsa_i31_private, br_rsa_i31_pkcs1_sign);
	/*
	 * If the server used an EC private key, this call would look
	 * like this:

	br_ssl_server_set_single_ec(cc, chain, chain_len, sk,
		BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN,
		cert_issuer_key_type,
		&br_ec_prime_i31, br_ecdsa_i31_sign_asn1);

	 * Note the tricky points:
	 *
	 * -- "ECDH" cipher suites use only the EC code (&br_ec_prime_i31);
	 *    the ECDHE_ECDSA cipher suites need both the EC code and
	 *    the ECDSA signature implementation.
	 *
	 * -- For "ECDH" (not "ECDHE") cipher suites, the engine must
	 *    know the key type (RSA or EC) for the intermediate CA that
	 *    issued the server's certificate; this is an artefact of
	 *    how the protocol is defined. BearSSL won't try to decode
	 *    the server's certificate to obtain that information (it
	 *    could do that, the code is there, but it would increase the
	 *    footprint). So this must be provided by the caller.
	 *
	 * -- BR_KEYTYPE_KEYX allows ECDH, BR_KEYTYPE_SIGN allows
	 *    ECDHE_ECDSA.
	 */

	br_ssl_engine_set_cbc(&cc->eng,
		&br_sslrec_in_cbc_vtable,
		&br_sslrec_out_cbc_vtable);
	br_ssl_engine_set_gcm(&cc->eng,
		&br_sslrec_in_gcm_vtable,
		&br_sslrec_out_gcm_vtable);

	br_ssl_engine_set_aes_cbc(&cc->eng,
		&br_aes_ct_cbcenc_vtable,
		&br_aes_ct_cbcdec_vtable);
	br_ssl_engine_set_aes_ctr(&cc->eng,
		&br_aes_ct_ctr_vtable);
	/* Alternate: aes_ct64
	br_ssl_engine_set_aes_cbc(&cc->eng,
		&br_aes_ct64_cbcenc_vtable,
		&br_aes_ct64_cbcdec_vtable);
	br_ssl_engine_set_aes_ctr(&cc->eng,
		&br_aes_ct64_ctr_vtable);
	*/
	/* Alternate: aes_small
	br_ssl_engine_set_aes_cbc(&cc->eng,
		&br_aes_small_cbcenc_vtable,
		&br_aes_small_cbcdec_vtable);
	br_ssl_engine_set_aes_ctr(&cc->eng,
		&br_aes_small_ctr_vtable);
	*/
	/* Alternate: aes_big
	br_ssl_engine_set_aes_cbc(&cc->eng,
		&br_aes_big_cbcenc_vtable,
		&br_aes_big_cbcdec_vtable);
	br_ssl_engine_set_aes_ctr(&cc->eng,
		&br_aes_big_ctr_vtable);
	*/
	br_ssl_engine_set_des_cbc(&cc->eng,
		&br_des_ct_cbcenc_vtable,
		&br_des_ct_cbcdec_vtable);
	/* Alternate: des_tab
	br_ssl_engine_set_des_cbc(&cc->eng,
		&br_des_tab_cbcenc_vtable,
		&br_des_tab_cbcdec_vtable);
	*/

	br_ssl_engine_set_ghash(&cc->eng,
		&br_ghash_ctmul);
	/* Alternate: ghash_ctmul32
	br_ssl_engine_set_ghash(&cc->eng,
		&br_ghash_ctmul32);
	*/
	/* Alternate: ghash_ctmul64
	br_ssl_engine_set_ghash(&cc->eng,
		&br_ghash_ctmul64);
	*/
}
