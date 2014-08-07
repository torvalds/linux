
#ifndef THREEFISHAPI_H
#define THREEFISHAPI_H

/**
 * @file threefish_api.h
 * @brief A Threefish cipher API and its functions.
 * @{
 *
 * This API and the functions that implement this API simplify the usage
 * of the Threefish cipher. The design and the way to use the functions
 * follow the openSSL design but at the same time take care of some Threefish
 * specific behaviour and possibilities.
 *
 * These are the low level functions that deal with Threefish blocks only.
 * Implementations for cipher modes such as ECB, CFB, or CBC may use these
 * functions.
 *
@code
	// Threefish cipher context data
	struct threefish_key key_ctx;

	// Initialize the context
	threefish_set_key(&key_ctx, THREEFISH_512, key, tweak);

	// Encrypt
	threefish_encrypt_block_bytes(&key_ctx, input, cipher);
@endcode
 */

#include <linux/types.h>
#include "skein.h"

#define KEY_SCHEDULE_CONST 0x1BD11BDAA9FC1A22L

/**
 * Which Threefish size to use
 */
enum threefish_size {
	THREEFISH_256 = 256,     /*!< Skein with 256 bit state */
	THREEFISH_512 = 512,     /*!< Skein with 512 bit state */
	THREEFISH_1024 = 1024    /*!< Skein with 1024 bit state */
};

/**
 * Context for Threefish key and tweak words.
 *
 * This structure was setup with some know-how of the internal
 * Skein structures, in particular ordering of header and size dependent
 * variables. If Skein implementation changes this, the adapt these
 * structures as well.
 */
struct threefish_key {
	u64 state_size;
	u64 key[SKEIN_MAX_STATE_WORDS+1];   /* max number of key words*/
	u64 tweak[3];
};

/**
 * Set Threefish key and tweak data.
 *
 * This function sets the key and tweak data for the Threefish cipher of
 * the given size. The key data must have the same length (number of bits)
 * as the state size
 *
 * @param key_ctx
 *     Pointer to a Threefish key structure.
 * @param size
 *     Which Skein size to use.
 * @param key_data
 *     Pointer to the key words (word has 64 bits).
 * @param tweak
 *     Pointer to the two tweak words (word has 64 bits).
 */
void threefish_set_key(struct threefish_key *key_ctx,
		       enum threefish_size state_size,
		       u64 *key_data, u64 *tweak);

/**
 * Encrypt Threefish block (bytes).
 *
 * The buffer must have at least the same length (number of bits) as the
 * state size for this key. The function uses the first @c state_size bits
 * of the input buffer, encrypts them and stores the result in the output
 * buffer.
 *
 * @param key_ctx
 *     Pointer to a Threefish key structure.
 * @param in
 *     Poionter to plaintext data buffer.
 * @param out
 *     Pointer to cipher buffer.
 */
void threefish_encrypt_block_bytes(struct threefish_key *key_ctx, u8 *in,
				   u8 *out);

/**
 * Encrypt Threefish block (words).
 *
 * The buffer must have at least the same length (number of bits) as the
 * state size for this key. The function uses the first @c state_size bits
 * of the input buffer, encrypts them and stores the result in the output
 * buffer.
 *
 * The wordsize ist set to 64 bits.
 *
 * @param key_ctx
 *     Pointer to a Threefish key structure.
 * @param in
 *     Poionter to plaintext data buffer.
 * @param out
 *     Pointer to cipher buffer.
 */
void threefish_encrypt_block_words(struct threefish_key *key_ctx, u64 *in,
				   u64 *out);

/**
 * Decrypt Threefish block (bytes).
 *
 * The buffer must have at least the same length (number of bits) as the
 * state size for this key. The function uses the first @c state_size bits
 * of the input buffer, decrypts them and stores the result in the output
 * buffer
 *
 * @param key_ctx
 *     Pointer to a Threefish key structure.
 * @param in
 *     Poionter to cipher data buffer.
 * @param out
 *     Pointer to plaintext buffer.
 */
void threefish_decrypt_block_bytes(struct threefish_key *key_ctx, u8 *in,
				   u8 *out);

/**
 * Decrypt Threefish block (words).
 *
 * The buffer must have at least the same length (number of bits) as the
 * state size for this key. The function uses the first @c state_size bits
 * of the input buffer, encrypts them and stores the result in the output
 * buffer.
 *
 * The wordsize ist set to 64 bits.
 *
 * @param key_ctx
 *     Pointer to a Threefish key structure.
 * @param in
 *     Poionter to cipher data buffer.
 * @param out
 *     Pointer to plaintext buffer.
 */
void threefish_decrypt_block_words(struct threefish_key *key_ctx, u64 *in,
				   u64 *out);

void threefish_encrypt_256(struct threefish_key *key_ctx, u64 *input,
			   u64 *output);
void threefish_encrypt_512(struct threefish_key *key_ctx, u64 *input,
			   u64 *output);
void threefish_encrypt_1024(struct threefish_key *key_ctx, u64 *input,
			    u64 *output);
void threefish_decrypt_256(struct threefish_key *key_ctx, u64 *input,
			   u64 *output);
void threefish_decrypt_512(struct threefish_key *key_ctx, u64 *input,
			   u64 *output);
void threefish_decrypt_1024(struct threefish_key *key_ctx, u64 *input,
			    u64 *output);
/**
 * @}
 */
#endif
