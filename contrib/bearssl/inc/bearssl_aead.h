/*
 * Copyright (c) 2017 Thomas Pornin <pornin@bolet.org>
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

#ifndef BR_BEARSSL_AEAD_H__
#define BR_BEARSSL_AEAD_H__

#include <stddef.h>
#include <stdint.h>

#include "bearssl_block.h"
#include "bearssl_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file bearssl_aead.h
 *
 * # Authenticated Encryption with Additional Data
 *
 * This file documents the API for AEAD encryption.
 *
 *
 * ## Procedural API
 *
 * An AEAD algorithm processes messages and provides confidentiality
 * (encryption) and checked integrity (MAC). It uses the following
 * parameters:
 *
 *   - A symmetric key. Exact size depends on the AEAD algorithm.
 *
 *   - A nonce (IV). Size depends on the AEAD algorithm; for most
 *     algorithms, it is crucial for security that any given nonce
 *     value is never used twice for the same key and distinct
 *     messages.
 *
 *   - Data to encrypt and protect.
 *
 *   - Additional authenticated data, which is covered by the MAC but
 *     otherwise left untouched (i.e. not encrypted).
 *
 * The AEAD algorithm encrypts the data, and produces an authentication
 * tag. It is assumed that the encrypted data, the tag, the additional
 * authenticated data and the nonce are sent to the receiver; the
 * additional data and the nonce may be implicit (e.g. using elements of
 * the underlying transport protocol, such as record sequence numbers).
 * The receiver will recompute the tag value and compare it with the one
 * received; if they match, then the data is correct, and can be
 * decrypted and used; otherwise, at least one of the elements was
 * altered in transit, normally leading to wholesale rejection of the
 * complete message.
 *
 * For each AEAD algorithm, identified by a symbolic name (hereafter
 * denoted as "`xxx`"), the following functions are defined:
 *
 *   - `br_xxx_init()`
 *
 *     Initialise the AEAD algorithm, on a provided context structure.
 *     Exact parameters depend on the algorithm, and may include
 *     pointers to extra implementations and context structures. The
 *     secret key is provided at this point, either directly or
 *     indirectly.
 *
 *   - `br_xxx_reset()`
 *
 *     Start a new AEAD computation. The nonce value is provided as
 *     parameter to this function.
 *
 *   - `br_xxx_aad_inject()`
 *
 *     Inject some additional authenticated data. Additional data may
 *     be provided in several chunks of arbitrary length.
 *
 *   - `br_xxx_flip()`
 *
 *     This function MUST be called after injecting all additional
 *     authenticated data, and before beginning to encrypt the plaintext
 *     (or decrypt the ciphertext).
 *
 *   - `br_xxx_run()`
 *
 *     Process some plaintext (to encrypt) or ciphertext (to decrypt).
 *     Encryption/decryption is done in place. Data may be provided in
 *     several chunks of arbitrary length.
 *
 *   - `br_xxx_get_tag()`
 *
 *     Compute the authentication tag. All message data (encrypted or
 *     decrypted) must have been injected at that point. Also, this
 *     call may modify internal context elements, so it may be called
 *     only once for a given AEAD computation.
 *
 *   - `br_xxx_check_tag()`
 *
 *     An alternative to `br_xxx_get_tag()`, meant to be used by the
 *     receiver: the authentication tag is internally recomputed, and
 *     compared with the one provided as parameter.
 *
 * This API makes the following assumptions on the AEAD algorithm:
 *
 *   - Encryption does not expand the size of the ciphertext; there is
 *     no padding. This is true of most modern AEAD modes such as GCM.
 *
 *   - The additional authenticated data must be processed first,
 *     before the encrypted/decrypted data.
 *
 *   - Nonce, plaintext and additional authenticated data all consist
 *     in an integral number of bytes. There is no provision to use
 *     elements whose length in bits is not a multiple of 8.
 *
 * Each AEAD algorithm has its own requirements and limits on the sizes
 * of additional data and plaintext. This API does not provide any
 * way to report invalid usage; it is up to the caller to ensure that
 * the provided key, nonce, and data elements all fit the algorithm's
 * requirements.
 *
 *
 * ## Object-Oriented API
 *
 * Each context structure begins with a field (called `vtable`) that
 * points to an instance of a structure that references the relevant
 * functions through pointers. Each such structure contains the
 * following:
 *
 *   - `reset`
 *
 *     Pointer to the reset function, that allows starting a new
 *     computation.
 *
 *   - `aad_inject`
 *
 *     Pointer to the additional authenticated data injection function.
 *
 *   - `flip`
 *
 *     Pointer to the function that transitions from additional data
 *     to main message data processing.
 *
 *   - `get_tag`
 *
 *     Pointer to the function that computes and returns the tag.
 *
 *   - `check_tag`
 *
 *     Pointer to the function that computes and verifies the tag against
 *     a received value.
 *
 * Note that there is no OOP method for context initialisation: the
 * various AEAD algorithms have different requirements that would not
 * map well to a single initialisation API.
 *
 * The OOP API is not provided for CCM, due to its specific requirements
 * (length of plaintext must be known in advance).
 */

/**
 * \brief Class type of an AEAD algorithm.
 */
typedef struct br_aead_class_ br_aead_class;
struct br_aead_class_ {

	/**
	 * \brief Size (in bytes) of authentication tags created by
	 * this AEAD algorithm.
	 */
	size_t tag_size;

	/**
	 * \brief Reset an AEAD context.
	 *
	 * This function resets an already initialised AEAD context for
	 * a new computation run. Implementations and keys are
	 * conserved. This function can be called at any time; it
	 * cancels any ongoing AEAD computation that uses the provided
	 * context structure.

	 * The provided IV is a _nonce_. Each AEAD algorithm has its
	 * own requirements on IV size and contents; for most of them,
	 * it is crucial to security that each nonce value is used
	 * only once for a given secret key.
	 *
	 * \param cc    AEAD context structure.
	 * \param iv    AEAD nonce to use.
	 * \param len   AEAD nonce length (in bytes).
	 */
	void (*reset)(const br_aead_class **cc, const void *iv, size_t len);

	/**
	 * \brief Inject additional authenticated data.
	 *
	 * The provided data is injected into a running AEAD
	 * computation. Additional data must be injected _before_ the
	 * call to `flip()`. Additional data can be injected in several
	 * chunks of arbitrary length.
	 *
	 * \param cc     AEAD context structure.
	 * \param data   pointer to additional authenticated data.
	 * \param len    length of additional authenticated data (in bytes).
	 */
	void (*aad_inject)(const br_aead_class **cc,
		const void *data, size_t len);

	/**
	 * \brief Finish injection of additional authenticated data.
	 *
	 * This function MUST be called before beginning the actual
	 * encryption or decryption (with `run()`), even if no
	 * additional authenticated data was injected. No additional
	 * authenticated data may be injected after this function call.
	 *
	 * \param cc   AEAD context structure.
	 */
	void (*flip)(const br_aead_class **cc);

	/**
	 * \brief Encrypt or decrypt some data.
	 *
	 * Data encryption or decryption can be done after `flip()` has
	 * been called on the context. If `encrypt` is non-zero, then
	 * the provided data shall be plaintext, and it is encrypted in
	 * place. Otherwise, the data shall be ciphertext, and it is
	 * decrypted in place.
	 *
	 * Data may be provided in several chunks of arbitrary length.
	 *
	 * \param cc        AEAD context structure.
	 * \param encrypt   non-zero for encryption, zero for decryption.
	 * \param data      data to encrypt or decrypt.
	 * \param len       data length (in bytes).
	 */
	void (*run)(const br_aead_class **cc, int encrypt,
		void *data, size_t len);

	/**
	 * \brief Compute authentication tag.
	 *
	 * Compute the AEAD authentication tag. The tag length depends
	 * on the AEAD algorithm; it is written in the provided `tag`
	 * buffer. This call terminates the AEAD run: no data may be
	 * processed with that AEAD context afterwards, until `reset()`
	 * is called to initiate a new AEAD run.
	 *
	 * The tag value must normally be sent along with the encrypted
	 * data. When decrypting, the tag value must be recomputed and
	 * compared with the received tag: if the two tag values differ,
	 * then either the tag or the encrypted data was altered in
	 * transit. As an alternative to this function, the
	 * `check_tag()` function may be used to compute and check the
	 * tag value.
	 *
	 * Tag length depends on the AEAD algorithm.
	 *
	 * \param cc    AEAD context structure.
	 * \param tag   destination buffer for the tag.
	 */
	void (*get_tag)(const br_aead_class **cc, void *tag);

	/**
	 * \brief Compute and check authentication tag.
	 *
	 * This function is an alternative to `get_tag()`, and is
	 * normally used on the receiving end (i.e. when decrypting
	 * messages). The tag value is recomputed and compared with the
	 * provided tag value. If they match, 1 is returned; on
	 * mismatch, 0 is returned. A returned value of 0 means that the
	 * data or the tag was altered in transit, normally leading to
	 * wholesale rejection of the complete message.
	 *
	 * Tag length depends on the AEAD algorithm.
	 *
	 * \param cc    AEAD context structure.
	 * \param tag   tag value to compare with.
	 * \return  1 on success (exact match of tag value), 0 otherwise.
	 */
	uint32_t (*check_tag)(const br_aead_class **cc, const void *tag);

	/**
	 * \brief Compute authentication tag (with truncation).
	 *
	 * This function is similar to `get_tag()`, except that the tag
	 * length is provided. Some AEAD algorithms allow several tag
	 * lengths, usually by truncating the normal tag. Shorter tags
	 * mechanically increase success probability of forgeries.
	 * The range of allowed tag lengths depends on the algorithm.
	 *
	 * \param cc    AEAD context structure.
	 * \param tag   destination buffer for the tag.
	 * \param len   tag length (in bytes).
	 */
	void (*get_tag_trunc)(const br_aead_class **cc, void *tag, size_t len);

	/**
	 * \brief Compute and check authentication tag (with truncation).
	 *
	 * This function is similar to `check_tag()` except that it
	 * works over an explicit tag length. See `get_tag()` for a
	 * discussion of explicit tag lengths; the range of allowed tag
	 * lengths depends on the algorithm.
	 *
	 * \param cc    AEAD context structure.
	 * \param tag   tag value to compare with.
	 * \param len   tag length (in bytes).
	 * \return  1 on success (exact match of tag value), 0 otherwise.
	 */
	uint32_t (*check_tag_trunc)(const br_aead_class **cc,
		const void *tag, size_t len);
};

/**
 * \brief Context structure for GCM.
 *
 * GCM is an AEAD mode that combines a block cipher in CTR mode with a
 * MAC based on GHASH, to provide authenticated encryption:
 *
 *   - Any block cipher with 16-byte blocks can be used with GCM.
 *
 *   - The nonce can have any length, from 0 up to 2^64-1 bits; however,
 *     96-bit nonces (12 bytes) are recommended (nonces with a length
 *     distinct from 12 bytes are internally hashed, which risks reusing
 *     nonce value with a small but not always negligible probability).
 *
 *   - Additional authenticated data may have length up to 2^64-1 bits.
 *
 *   - Message length may range up to 2^39-256 bits at most.
 *
 *   - The authentication tag has length 16 bytes.
 *
 * The GCM initialisation function receives as parameter an
 * _initialised_ block cipher implementation context, with the secret
 * key already set. A pointer to that context will be kept within the
 * GCM context structure. It is up to the caller to allocate and
 * initialise that block cipher context.
 */
typedef struct {
	/** \brief Pointer to vtable for this context. */
	const br_aead_class *vtable;

#ifndef BR_DOXYGEN_IGNORE
	const br_block_ctr_class **bctx;
	br_ghash gh;
	unsigned char h[16];
	unsigned char j0_1[12];
	unsigned char buf[16];
	unsigned char y[16];
	uint32_t j0_2, jc;
	uint64_t count_aad, count_ctr;
#endif
} br_gcm_context;

/**
 * \brief Initialize a GCM context.
 *
 * A block cipher implementation, with its initialised context structure,
 * is provided. The block cipher MUST use 16-byte blocks in CTR mode,
 * and its secret key MUST have been already set in the provided context.
 * A GHASH implementation must also be provided. The parameters are linked
 * in the GCM context.
 *
 * After this function has been called, the `br_gcm_reset()` function must
 * be called, to provide the IV for GCM computation.
 *
 * \param ctx    GCM context structure.
 * \param bctx   block cipher context (already initialised with secret key).
 * \param gh     GHASH implementation.
 */
void br_gcm_init(br_gcm_context *ctx,
	const br_block_ctr_class **bctx, br_ghash gh);

/**
 * \brief Reset a GCM context.
 *
 * This function resets an already initialised GCM context for a new
 * computation run. Implementations and keys are conserved. This function
 * can be called at any time; it cancels any ongoing GCM computation that
 * uses the provided context structure.
 *
 * The provided IV is a _nonce_. It is critical to GCM security that IV
 * values are not repeated for the same encryption key. IV can have
 * arbitrary length (up to 2^64-1 bits), but the "normal" length is
 * 96 bits (12 bytes).
 *
 * \param ctx   GCM context structure.
 * \param iv    GCM nonce to use.
 * \param len   GCM nonce length (in bytes).
 */
void br_gcm_reset(br_gcm_context *ctx, const void *iv, size_t len);

/**
 * \brief Inject additional authenticated data into GCM.
 *
 * The provided data is injected into a running GCM computation. Additional
 * data must be injected _before_ the call to `br_gcm_flip()`.
 * Additional data can be injected in several chunks of arbitrary length;
 * the maximum total size of additional authenticated data is 2^64-1
 * bits.
 *
 * \param ctx    GCM context structure.
 * \param data   pointer to additional authenticated data.
 * \param len    length of additional authenticated data (in bytes).
 */
void br_gcm_aad_inject(br_gcm_context *ctx, const void *data, size_t len);

/**
 * \brief Finish injection of additional authenticated data into GCM.
 *
 * This function MUST be called before beginning the actual encryption
 * or decryption (with `br_gcm_run()`), even if no additional authenticated
 * data was injected. No additional authenticated data may be injected
 * after this function call.
 *
 * \param ctx   GCM context structure.
 */
void br_gcm_flip(br_gcm_context *ctx);

/**
 * \brief Encrypt or decrypt some data with GCM.
 *
 * Data encryption or decryption can be done after `br_gcm_flip()`
 * has been called on the context. If `encrypt` is non-zero, then the
 * provided data shall be plaintext, and it is encrypted in place.
 * Otherwise, the data shall be ciphertext, and it is decrypted in place.
 *
 * Data may be provided in several chunks of arbitrary length. The maximum
 * total length for data is 2^39-256 bits, i.e. about 65 gigabytes.
 *
 * \param ctx       GCM context structure.
 * \param encrypt   non-zero for encryption, zero for decryption.
 * \param data      data to encrypt or decrypt.
 * \param len       data length (in bytes).
 */
void br_gcm_run(br_gcm_context *ctx, int encrypt, void *data, size_t len);

/**
 * \brief Compute GCM authentication tag.
 *
 * Compute the GCM authentication tag. The tag is a 16-byte value which
 * is written in the provided `tag` buffer. This call terminates the
 * GCM run: no data may be processed with that GCM context afterwards,
 * until `br_gcm_reset()` is called to initiate a new GCM run.
 *
 * The tag value must normally be sent along with the encrypted data.
 * When decrypting, the tag value must be recomputed and compared with
 * the received tag: if the two tag values differ, then either the tag
 * or the encrypted data was altered in transit. As an alternative to
 * this function, the `br_gcm_check_tag()` function can be used to
 * compute and check the tag value.
 *
 * \param ctx   GCM context structure.
 * \param tag   destination buffer for the tag (16 bytes).
 */
void br_gcm_get_tag(br_gcm_context *ctx, void *tag);

/**
 * \brief Compute and check GCM authentication tag.
 *
 * This function is an alternative to `br_gcm_get_tag()`, normally used
 * on the receiving end (i.e. when decrypting value). The tag value is
 * recomputed and compared with the provided tag value. If they match, 1
 * is returned; on mismatch, 0 is returned. A returned value of 0 means
 * that the data or the tag was altered in transit, normally leading to
 * wholesale rejection of the complete message.
 *
 * \param ctx   GCM context structure.
 * \param tag   tag value to compare with (16 bytes).
 * \return  1 on success (exact match of tag value), 0 otherwise.
 */
uint32_t br_gcm_check_tag(br_gcm_context *ctx, const void *tag);

/**
 * \brief Compute GCM authentication tag (with truncation).
 *
 * This function is similar to `br_gcm_get_tag()`, except that it allows
 * the tag to be truncated to a smaller length. The intended tag length
 * is provided as `len` (in bytes); it MUST be no more than 16, but
 * it may be smaller. Note that decreasing tag length mechanically makes
 * forgeries easier; NIST SP 800-38D specifies that the tag length shall
 * lie between 12 and 16 bytes (inclusive), but may be truncated down to
 * 4 or 8 bytes, for specific applications that can tolerate it. It must
 * also be noted that successful forgeries leak information on the
 * authentication key, making subsequent forgeries easier. Therefore,
 * tag truncation, and in particular truncation to sizes lower than 12
 * bytes, shall be envisioned only with great care.
 *
 * The tag is written in the provided `tag` buffer. This call terminates
 * the GCM run: no data may be processed with that GCM context
 * afterwards, until `br_gcm_reset()` is called to initiate a new GCM
 * run.
 *
 * The tag value must normally be sent along with the encrypted data.
 * When decrypting, the tag value must be recomputed and compared with
 * the received tag: if the two tag values differ, then either the tag
 * or the encrypted data was altered in transit. As an alternative to
 * this function, the `br_gcm_check_tag_trunc()` function can be used to
 * compute and check the tag value.
 *
 * \param ctx   GCM context structure.
 * \param tag   destination buffer for the tag.
 * \param len   tag length (16 bytes or less).
 */
void br_gcm_get_tag_trunc(br_gcm_context *ctx, void *tag, size_t len);

/**
 * \brief Compute and check GCM authentication tag (with truncation).
 *
 * This function is an alternative to `br_gcm_get_tag_trunc()`, normally used
 * on the receiving end (i.e. when decrypting value). The tag value is
 * recomputed and compared with the provided tag value. If they match, 1
 * is returned; on mismatch, 0 is returned. A returned value of 0 means
 * that the data or the tag was altered in transit, normally leading to
 * wholesale rejection of the complete message.
 *
 * Tag length MUST be 16 bytes or less. The normal GCM tag length is 16
 * bytes. See `br_check_tag_trunc()` for some discussion on the potential
 * perils of truncating authentication tags.
 *
 * \param ctx   GCM context structure.
 * \param tag   tag value to compare with.
 * \param len   tag length (in bytes).
 * \return  1 on success (exact match of tag value), 0 otherwise.
 */
uint32_t br_gcm_check_tag_trunc(br_gcm_context *ctx,
	const void *tag, size_t len);

/**
 * \brief Class instance for GCM.
 */
extern const br_aead_class br_gcm_vtable;

/**
 * \brief Context structure for EAX.
 *
 * EAX is an AEAD mode that combines a block cipher in CTR mode with
 * CBC-MAC using the same block cipher and the same key, to provide
 * authenticated encryption:
 *
 *   - Any block cipher with 16-byte blocks can be used with EAX
 *     (technically, other block sizes are defined as well, but this
 *     is not implemented by these functions; shorter blocks also
 *     imply numerous security issues).
 *
 *   - The nonce can have any length, as long as nonce values are
 *     not reused (thus, if nonces are randomly selected, the nonce
 *     size should be such that reuse probability is negligible).
 *
 *   - Additional authenticated data length is unlimited.
 *
 *   - Message length is unlimited.
 *
 *   - The authentication tag has length 16 bytes.
 *
 * The EAX initialisation function receives as parameter an
 * _initialised_ block cipher implementation context, with the secret
 * key already set. A pointer to that context will be kept within the
 * EAX context structure. It is up to the caller to allocate and
 * initialise that block cipher context.
 */
typedef struct {
	/** \brief Pointer to vtable for this context. */
	const br_aead_class *vtable;

#ifndef BR_DOXYGEN_IGNORE
	const br_block_ctrcbc_class **bctx;
	unsigned char L2[16];
	unsigned char L4[16];
	unsigned char nonce[16];
	unsigned char head[16];
	unsigned char ctr[16];
	unsigned char cbcmac[16];
	unsigned char buf[16];
	size_t ptr;
#endif
} br_eax_context;

/**
 * \brief EAX captured state.
 *
 * Some internal values computed by EAX may be captured at various
 * points, and reused for another EAX run with the same secret key,
 * for lower per-message overhead. Captured values do not depend on
 * the nonce.
 */
typedef struct {
#ifndef BR_DOXYGEN_IGNORE
	unsigned char st[3][16];
#endif
} br_eax_state;

/**
 * \brief Initialize an EAX context.
 *
 * A block cipher implementation, with its initialised context
 * structure, is provided. The block cipher MUST use 16-byte blocks in
 * CTR + CBC-MAC mode, and its secret key MUST have been already set in
 * the provided context. The parameters are linked in the EAX context.
 *
 * After this function has been called, the `br_eax_reset()` function must
 * be called, to provide the nonce for EAX computation.
 *
 * \param ctx    EAX context structure.
 * \param bctx   block cipher context (already initialised with secret key).
 */
void br_eax_init(br_eax_context *ctx, const br_block_ctrcbc_class **bctx);

/**
 * \brief Capture pre-AAD state.
 *
 * This function precomputes key-dependent data, and stores it in the
 * provided `st` structure. This structure should then be used with
 * `br_eax_reset_pre_aad()`, or updated with `br_eax_get_aad_mac()`
 * and then used with `br_eax_reset_post_aad()`.
 *
 * The EAX context structure is unmodified by this call.
 *
 * \param ctx   EAX context structure.
 * \param st    recipient for captured state.
 */
void br_eax_capture(const br_eax_context *ctx, br_eax_state *st);

/**
 * \brief Reset an EAX context.
 *
 * This function resets an already initialised EAX context for a new
 * computation run. Implementations and keys are conserved. This function
 * can be called at any time; it cancels any ongoing EAX computation that
 * uses the provided context structure.
 *
 * It is critical to EAX security that nonce values are not repeated for
 * the same encryption key. Nonces can have arbitrary length. If nonces
 * are randomly generated, then a nonce length of at least 128 bits (16
 * bytes) is recommended, to make nonce reuse probability sufficiently
 * low.
 *
 * \param ctx     EAX context structure.
 * \param nonce   EAX nonce to use.
 * \param len     EAX nonce length (in bytes).
 */
void br_eax_reset(br_eax_context *ctx, const void *nonce, size_t len);

/**
 * \brief Reset an EAX context with a pre-AAD captured state.
 *
 * This function is an alternative to `br_eax_reset()`, that reuses a
 * previously captured state structure for lower per-message overhead.
 * The state should have been populated with `br_eax_capture_state()`
 * but not updated with `br_eax_get_aad_mac()`.
 *
 * After this function is called, additional authenticated data MUST
 * be injected. At least one byte of additional authenticated data
 * MUST be provided with `br_eax_aad_inject()`; computation result will
 * be incorrect if `br_eax_flip()` is called right away.
 *
 * After injection of the AAD and call to `br_eax_flip()`, at least
 * one message byte must be provided. Empty messages are not supported
 * with this reset mode.
 *
 * \param ctx     EAX context structure.
 * \param st      pre-AAD captured state.
 * \param nonce   EAX nonce to use.
 * \param len     EAX nonce length (in bytes).
 */
void br_eax_reset_pre_aad(br_eax_context *ctx, const br_eax_state *st,
	const void *nonce, size_t len);

/**
 * \brief Reset an EAX context with a post-AAD captured state.
 *
 * This function is an alternative to `br_eax_reset()`, that reuses a
 * previously captured state structure for lower per-message overhead.
 * The state should have been populated with `br_eax_capture_state()`
 * and then updated with `br_eax_get_aad_mac()`.
 *
 * After this function is called, message data MUST be injected. The
 * `br_eax_flip()` function MUST NOT be called. At least one byte of
 * message data MUST be provided with `br_eax_run()`; empty messages
 * are not supported with this reset mode.
 *
 * \param ctx     EAX context structure.
 * \param st      post-AAD captured state.
 * \param nonce   EAX nonce to use.
 * \param len     EAX nonce length (in bytes).
 */
void br_eax_reset_post_aad(br_eax_context *ctx, const br_eax_state *st,
	const void *nonce, size_t len);

/**
 * \brief Inject additional authenticated data into EAX.
 *
 * The provided data is injected into a running EAX computation. Additional
 * data must be injected _before_ the call to `br_eax_flip()`.
 * Additional data can be injected in several chunks of arbitrary length;
 * the total amount of additional authenticated data is unlimited.
 *
 * \param ctx    EAX context structure.
 * \param data   pointer to additional authenticated data.
 * \param len    length of additional authenticated data (in bytes).
 */
void br_eax_aad_inject(br_eax_context *ctx, const void *data, size_t len);

/**
 * \brief Finish injection of additional authenticated data into EAX.
 *
 * This function MUST be called before beginning the actual encryption
 * or decryption (with `br_eax_run()`), even if no additional authenticated
 * data was injected. No additional authenticated data may be injected
 * after this function call.
 *
 * \param ctx   EAX context structure.
 */
void br_eax_flip(br_eax_context *ctx);

/**
 * \brief Obtain a copy of the MAC on additional authenticated data.
 *
 * This function may be called only after `br_eax_flip()`; it copies the
 * AAD-specific MAC value into the provided state. The MAC value depends
 * on the secret key and the additional data itself, but not on the
 * nonce. The updated state `st` is meant to be used as parameter for a
 * further `br_eax_reset_post_aad()` call.
 *
 * \param ctx   EAX context structure.
 * \param st    captured state to update.
 */
static inline void
br_eax_get_aad_mac(const br_eax_context *ctx, br_eax_state *st)
{
	memcpy(st->st[1], ctx->head, sizeof ctx->head);
}

/**
 * \brief Encrypt or decrypt some data with EAX.
 *
 * Data encryption or decryption can be done after `br_eax_flip()`
 * has been called on the context. If `encrypt` is non-zero, then the
 * provided data shall be plaintext, and it is encrypted in place.
 * Otherwise, the data shall be ciphertext, and it is decrypted in place.
 *
 * Data may be provided in several chunks of arbitrary length.
 *
 * \param ctx       EAX context structure.
 * \param encrypt   non-zero for encryption, zero for decryption.
 * \param data      data to encrypt or decrypt.
 * \param len       data length (in bytes).
 */
void br_eax_run(br_eax_context *ctx, int encrypt, void *data, size_t len);

/**
 * \brief Compute EAX authentication tag.
 *
 * Compute the EAX authentication tag. The tag is a 16-byte value which
 * is written in the provided `tag` buffer. This call terminates the
 * EAX run: no data may be processed with that EAX context afterwards,
 * until `br_eax_reset()` is called to initiate a new EAX run.
 *
 * The tag value must normally be sent along with the encrypted data.
 * When decrypting, the tag value must be recomputed and compared with
 * the received tag: if the two tag values differ, then either the tag
 * or the encrypted data was altered in transit. As an alternative to
 * this function, the `br_eax_check_tag()` function can be used to
 * compute and check the tag value.
 *
 * \param ctx   EAX context structure.
 * \param tag   destination buffer for the tag (16 bytes).
 */
void br_eax_get_tag(br_eax_context *ctx, void *tag);

/**
 * \brief Compute and check EAX authentication tag.
 *
 * This function is an alternative to `br_eax_get_tag()`, normally used
 * on the receiving end (i.e. when decrypting value). The tag value is
 * recomputed and compared with the provided tag value. If they match, 1
 * is returned; on mismatch, 0 is returned. A returned value of 0 means
 * that the data or the tag was altered in transit, normally leading to
 * wholesale rejection of the complete message.
 *
 * \param ctx   EAX context structure.
 * \param tag   tag value to compare with (16 bytes).
 * \return  1 on success (exact match of tag value), 0 otherwise.
 */
uint32_t br_eax_check_tag(br_eax_context *ctx, const void *tag);

/**
 * \brief Compute EAX authentication tag (with truncation).
 *
 * This function is similar to `br_eax_get_tag()`, except that it allows
 * the tag to be truncated to a smaller length. The intended tag length
 * is provided as `len` (in bytes); it MUST be no more than 16, but
 * it may be smaller. Note that decreasing tag length mechanically makes
 * forgeries easier; NIST SP 800-38D specifies that the tag length shall
 * lie between 12 and 16 bytes (inclusive), but may be truncated down to
 * 4 or 8 bytes, for specific applications that can tolerate it. It must
 * also be noted that successful forgeries leak information on the
 * authentication key, making subsequent forgeries easier. Therefore,
 * tag truncation, and in particular truncation to sizes lower than 12
 * bytes, shall be envisioned only with great care.
 *
 * The tag is written in the provided `tag` buffer. This call terminates
 * the EAX run: no data may be processed with that EAX context
 * afterwards, until `br_eax_reset()` is called to initiate a new EAX
 * run.
 *
 * The tag value must normally be sent along with the encrypted data.
 * When decrypting, the tag value must be recomputed and compared with
 * the received tag: if the two tag values differ, then either the tag
 * or the encrypted data was altered in transit. As an alternative to
 * this function, the `br_eax_check_tag_trunc()` function can be used to
 * compute and check the tag value.
 *
 * \param ctx   EAX context structure.
 * \param tag   destination buffer for the tag.
 * \param len   tag length (16 bytes or less).
 */
void br_eax_get_tag_trunc(br_eax_context *ctx, void *tag, size_t len);

/**
 * \brief Compute and check EAX authentication tag (with truncation).
 *
 * This function is an alternative to `br_eax_get_tag_trunc()`, normally used
 * on the receiving end (i.e. when decrypting value). The tag value is
 * recomputed and compared with the provided tag value. If they match, 1
 * is returned; on mismatch, 0 is returned. A returned value of 0 means
 * that the data or the tag was altered in transit, normally leading to
 * wholesale rejection of the complete message.
 *
 * Tag length MUST be 16 bytes or less. The normal EAX tag length is 16
 * bytes. See `br_check_tag_trunc()` for some discussion on the potential
 * perils of truncating authentication tags.
 *
 * \param ctx   EAX context structure.
 * \param tag   tag value to compare with.
 * \param len   tag length (in bytes).
 * \return  1 on success (exact match of tag value), 0 otherwise.
 */
uint32_t br_eax_check_tag_trunc(br_eax_context *ctx,
	const void *tag, size_t len);

/**
 * \brief Class instance for EAX.
 */
extern const br_aead_class br_eax_vtable;

/**
 * \brief Context structure for CCM.
 *
 * CCM is an AEAD mode that combines a block cipher in CTR mode with
 * CBC-MAC using the same block cipher and the same key, to provide
 * authenticated encryption:
 *
 *   - Any block cipher with 16-byte blocks can be used with CCM
 *     (technically, other block sizes are defined as well, but this
 *     is not implemented by these functions; shorter blocks also
 *     imply numerous security issues).
 *
 *   - The authentication tag length, and plaintext length, MUST be
 *     known when starting processing data. Plaintext and ciphertext
 *     can still be provided by chunks, but the total size must match
 *     the value provided upon initialisation.
 *
 *   - The nonce length is constrained between 7 and 13 bytes (inclusive).
 *     Furthermore, the plaintext length, when encoded, must fit over
 *     15-nonceLen bytes; thus, if the nonce has length 13 bytes, then
 *     the plaintext length cannot exceed 65535 bytes.
 *
 *   - Additional authenticated data length is practically unlimited
 *     (formal limit is at 2^64 bytes).
 *
 *   - The authentication tag has length 4 to 16 bytes (even values only).
 *
 * The CCM initialisation function receives as parameter an
 * _initialised_ block cipher implementation context, with the secret
 * key already set. A pointer to that context will be kept within the
 * CCM context structure. It is up to the caller to allocate and
 * initialise that block cipher context.
 */
typedef struct {
#ifndef BR_DOXYGEN_IGNORE
	const br_block_ctrcbc_class **bctx;
	unsigned char ctr[16];
	unsigned char cbcmac[16];
	unsigned char tagmask[16];
	unsigned char buf[16];
	size_t ptr;
	size_t tag_len;
#endif
} br_ccm_context;

/**
 * \brief Initialize a CCM context.
 *
 * A block cipher implementation, with its initialised context
 * structure, is provided. The block cipher MUST use 16-byte blocks in
 * CTR + CBC-MAC mode, and its secret key MUST have been already set in
 * the provided context. The parameters are linked in the CCM context.
 *
 * After this function has been called, the `br_ccm_reset()` function must
 * be called, to provide the nonce for CCM computation.
 *
 * \param ctx    CCM context structure.
 * \param bctx   block cipher context (already initialised with secret key).
 */
void br_ccm_init(br_ccm_context *ctx, const br_block_ctrcbc_class **bctx);

/**
 * \brief Reset a CCM context.
 *
 * This function resets an already initialised CCM context for a new
 * computation run. Implementations and keys are conserved. This function
 * can be called at any time; it cancels any ongoing CCM computation that
 * uses the provided context structure.
 *
 * The `aad_len` parameter contains the total length, in bytes, of the
 * additional authenticated data. It may be zero. That length MUST be
 * exact.
 *
 * The `data_len` parameter contains the total length, in bytes, of the
 * data that will be injected (plaintext or ciphertext). That length MUST
 * be exact. Moreover, that length MUST be less than 2^(8*(15-nonce_len)).
 *
 * The nonce length (`nonce_len`), in bytes, must be in the 7..13 range
 * (inclusive).
 *
 * The tag length (`tag_len`), in bytes, must be in the 4..16 range, and
 * be an even integer. Short tags mechanically allow for higher forgery
 * probabilities; hence, tag sizes smaller than 12 bytes shall be used only
 * with care.
 *
 * It is critical to CCM security that nonce values are not repeated for
 * the same encryption key. Random generation of nonces is not generally
 * recommended, due to the relatively small maximum nonce value.
 *
 * Returned value is 1 on success, 0 on error. An error is reported if
 * the tag or nonce length is out of range, or if the
 * plaintext/ciphertext length cannot be encoded with the specified
 * nonce length.
 *
 * \param ctx         CCM context structure.
 * \param nonce       CCM nonce to use.
 * \param nonce_len   CCM nonce length (in bytes, 7 to 13).
 * \param aad_len     additional authenticated data length (in bytes).
 * \param data_len    plaintext/ciphertext length (in bytes).
 * \param tag_len     tag length (in bytes).
 * \return  1 on success, 0 on error.
 */
int br_ccm_reset(br_ccm_context *ctx, const void *nonce, size_t nonce_len,
	uint64_t aad_len, uint64_t data_len, size_t tag_len);

/**
 * \brief Inject additional authenticated data into CCM.
 *
 * The provided data is injected into a running CCM computation. Additional
 * data must be injected _before_ the call to `br_ccm_flip()`.
 * Additional data can be injected in several chunks of arbitrary length,
 * but the total amount MUST exactly match the value which was provided
 * to `br_ccm_reset()`.
 *
 * \param ctx    CCM context structure.
 * \param data   pointer to additional authenticated data.
 * \param len    length of additional authenticated data (in bytes).
 */
void br_ccm_aad_inject(br_ccm_context *ctx, const void *data, size_t len);

/**
 * \brief Finish injection of additional authenticated data into CCM.
 *
 * This function MUST be called before beginning the actual encryption
 * or decryption (with `br_ccm_run()`), even if no additional authenticated
 * data was injected. No additional authenticated data may be injected
 * after this function call.
 *
 * \param ctx   CCM context structure.
 */
void br_ccm_flip(br_ccm_context *ctx);

/**
 * \brief Encrypt or decrypt some data with CCM.
 *
 * Data encryption or decryption can be done after `br_ccm_flip()`
 * has been called on the context. If `encrypt` is non-zero, then the
 * provided data shall be plaintext, and it is encrypted in place.
 * Otherwise, the data shall be ciphertext, and it is decrypted in place.
 *
 * Data may be provided in several chunks of arbitrary length, provided
 * that the total length exactly matches the length provided to the
 * `br_ccm_reset()` call.
 *
 * \param ctx       CCM context structure.
 * \param encrypt   non-zero for encryption, zero for decryption.
 * \param data      data to encrypt or decrypt.
 * \param len       data length (in bytes).
 */
void br_ccm_run(br_ccm_context *ctx, int encrypt, void *data, size_t len);

/**
 * \brief Compute CCM authentication tag.
 *
 * Compute the CCM authentication tag. This call terminates the CCM
 * run: all data must have been injected with `br_ccm_run()` (in zero,
 * one or more successive calls). After this function has been called,
 * no more data can br processed; a `br_ccm_reset()` call is required
 * to start a new message.
 *
 * The tag length was provided upon context initialisation (last call
 * to `br_ccm_reset()`); it is returned by this function.
 *
 * The tag value must normally be sent along with the encrypted data.
 * When decrypting, the tag value must be recomputed and compared with
 * the received tag: if the two tag values differ, then either the tag
 * or the encrypted data was altered in transit. As an alternative to
 * this function, the `br_ccm_check_tag()` function can be used to
 * compute and check the tag value.
 *
 * \param ctx   CCM context structure.
 * \param tag   destination buffer for the tag (up to 16 bytes).
 * \return  the tag length (in bytes).
 */
size_t br_ccm_get_tag(br_ccm_context *ctx, void *tag);

/**
 * \brief Compute and check CCM authentication tag.
 *
 * This function is an alternative to `br_ccm_get_tag()`, normally used
 * on the receiving end (i.e. when decrypting value). The tag value is
 * recomputed and compared with the provided tag value. If they match, 1
 * is returned; on mismatch, 0 is returned. A returned value of 0 means
 * that the data or the tag was altered in transit, normally leading to
 * wholesale rejection of the complete message.
 *
 * \param ctx   CCM context structure.
 * \param tag   tag value to compare with (up to 16 bytes).
 * \return  1 on success (exact match of tag value), 0 otherwise.
 */
uint32_t br_ccm_check_tag(br_ccm_context *ctx, const void *tag);

#ifdef __cplusplus
}
#endif

#endif
