/*
 * Copyright (c) 2013, Kenneth MacKay
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *  * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _CRYPTO_ECC_H
#define _CRYPTO_ECC_H

#define ECC_MAX_DIGITS	4 /* 256 */

#define ECC_DIGITS_TO_BYTES_SHIFT 3

/**
 * ecc_is_key_valid() - Validate a given ECDH private key
 *
 * @curve_id:		id representing the curve to use
 * @ndigits:		curve number of digits
 * @private_key:	private key to be used for the given curve
 * @private_key_len:	private key len
 *
 * Returns 0 if the key is acceptable, a negative value otherwise
 */
int ecc_is_key_valid(unsigned int curve_id, unsigned int ndigits,
		     const u8 *private_key, unsigned int private_key_len);

/**
 * ecdh_make_pub_key() - Compute an ECC public key
 *
 * @curve_id:		id representing the curve to use
 * @private_key:	pregenerated private key for the given curve
 * @private_key_len:	length of private_key
 * @public_key:		buffer for storing the public key generated
 * @public_key_len:	length of the public_key buffer
 *
 * Returns 0 if the public key was generated successfully, a negative value
 * if an error occurred.
 */
int ecdh_make_pub_key(const unsigned int curve_id, unsigned int ndigits,
		      const u8 *private_key, unsigned int private_key_len,
		      u8 *public_key, unsigned int public_key_len);

/**
 * ecdh_shared_secret() - Compute a shared secret
 *
 * @curve_id:		id representing the curve to use
 * @private_key:	private key of part A
 * @private_key_len:	length of private_key
 * @public_key:		public key of counterpart B
 * @public_key_len:	length of public_key
 * @secret:		buffer for storing the calculated shared secret
 * @secret_len:		length of the secret buffer
 *
 * Note: It is recommended that you hash the result of ecdh_shared_secret
 * before using it for symmetric encryption or HMAC.
 *
 * Returns 0 if the shared secret was generated successfully, a negative value
 * if an error occurred.
 */
int ecdh_shared_secret(unsigned int curve_id, unsigned int ndigits,
		       const u8 *private_key, unsigned int private_key_len,
		       const u8 *public_key, unsigned int public_key_len,
		       u8 *secret, unsigned int secret_len);
#endif
