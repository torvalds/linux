.. SPDX-License-Identifier: GPL-2.0-or-later

==========================
SHA-3 Algorithm Collection
==========================

.. contents::

Overview
========

The SHA-3 family of algorithms, as specified in NIST FIPS-202 [1]_, contains six
algorithms based on the Keccak sponge function.  The differences between them
are: the "rate" (how much of the state buffer gets updated with new data between
invocations of the Keccak function and analogous to the "block size"), what
domain separation suffix gets appended to the input data, and how much output
data is extracted at the end.  The Keccak sponge function is designed such that
arbitrary amounts of output can be obtained for certain algorithms.

Four digest algorithms are provided:

 - SHA3-224
 - SHA3-256
 - SHA3-384
 - SHA3-512

Additionally, two Extendable-Output Functions (XOFs) are provided:

 - SHAKE128
 - SHAKE256

The SHA-3 library API supports all six of these algorithms.  The four digest
algorithms are also supported by the crypto_shash and crypto_ahash APIs.

This document describes the SHA-3 library API.


Digests
=======

The following functions compute SHA-3 digests::

	void sha3_224(const u8 *in, size_t in_len, u8 out[SHA3_224_DIGEST_SIZE]);
	void sha3_256(const u8 *in, size_t in_len, u8 out[SHA3_256_DIGEST_SIZE]);
	void sha3_384(const u8 *in, size_t in_len, u8 out[SHA3_384_DIGEST_SIZE]);
	void sha3_512(const u8 *in, size_t in_len, u8 out[SHA3_512_DIGEST_SIZE]);

For users that need to pass in data incrementally, an incremental API is also
provided.  The incremental API uses the following struct::

	struct sha3_ctx { ... };

Initialization is done with one of::

	void sha3_224_init(struct sha3_ctx *ctx);
	void sha3_256_init(struct sha3_ctx *ctx);
	void sha3_384_init(struct sha3_ctx *ctx);
	void sha3_512_init(struct sha3_ctx *ctx);

Input data is then added with any number of calls to::

	void sha3_update(struct sha3_ctx *ctx, const u8 *in, size_t in_len);

Finally, the digest is generated using::

	void sha3_final(struct sha3_ctx *ctx, u8 *out);

which also zeroizes the context.  The length of the digest is determined by the
initialization function that was called.


Extendable-Output Functions
===========================

The following functions compute the SHA-3 extendable-output functions (XOFs)::

	void shake128(const u8 *in, size_t in_len, u8 *out, size_t out_len);
	void shake256(const u8 *in, size_t in_len, u8 *out, size_t out_len);

For users that need to provide the input data incrementally and/or receive the
output data incrementally, an incremental API is also provided.  The incremental
API uses the following struct::

	struct shake_ctx { ... };

Initialization is done with one of::

	void shake128_init(struct shake_ctx *ctx);
	void shake256_init(struct shake_ctx *ctx);

Input data is then added with any number of calls to::

	void shake_update(struct shake_ctx *ctx, const u8 *in, size_t in_len);

Finally, the output data is extracted with any number of calls to::

	void shake_squeeze(struct shake_ctx *ctx, u8 *out, size_t out_len);

and telling it how much data should be extracted.  Note that performing multiple
squeezes, with the output laid consecutively in a buffer, gets exactly the same
output as doing a single squeeze for the combined amount over the same buffer.

More input data cannot be added after squeezing has started.

Once all the desired output has been extracted, zeroize the context::

	void shake_zeroize_ctx(struct shake_ctx *ctx);


Testing
=======

To test the SHA-3 code, use sha3_kunit (CONFIG_CRYPTO_LIB_SHA3_KUNIT_TEST).

Since the SHA-3 algorithms are FIPS-approved, when the kernel is booted in FIPS
mode the SHA-3 library also performs a simple self-test.  This is purely to meet
a FIPS requirement.  Normal testing done by kernel developers and integrators
should use the much more comprehensive KUnit test suite instead.


References
==========

.. [1] https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.202.pdf


API Function Reference
======================

.. kernel-doc:: include/crypto/sha3.h
