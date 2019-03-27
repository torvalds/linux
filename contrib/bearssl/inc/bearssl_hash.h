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

#ifndef BR_BEARSSL_HASH_H__
#define BR_BEARSSL_HASH_H__

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \file bearssl_hash.h
 *
 * # Hash Functions
 *
 * This file documents the API for hash functions.
 *
 *
 * ## Procedural API
 *
 * For each implemented hash function, of name "`xxx`", the following
 * elements are defined:
 *
 *   - `br_xxx_vtable`
 *
 *     An externally defined instance of `br_hash_class`.
 *
 *   - `br_xxx_SIZE`
 *
 *     A macro that evaluates to the output size (in bytes) of the
 *     hash function.
 *
 *   - `br_xxx_ID`
 *
 *     A macro that evaluates to a symbolic identifier for the hash
 *     function. Such identifiers are used with HMAC and signature
 *     algorithm implementations.
 *
 *     NOTE: for the "standard" hash functions defined in [the TLS
 *     standard](https://tools.ietf.org/html/rfc5246#section-7.4.1.4.1),
 *     the symbolic identifiers match the constants used in TLS, i.e.
 *     1 to 6 for MD5, SHA-1, SHA-224, SHA-256, SHA-384 and SHA-512,
 *     respectively.
 *
 *   - `br_xxx_context`
 *
 *     Context for an ongoing computation. It is allocated by the
 *     caller, and a pointer to it is passed to all functions. A
 *     context contains no interior pointer, so it can be moved around
 *     and cloned (with a simple `memcpy()` or equivalent) in order to
 *     capture the function state at some point. Computations that use
 *     distinct context structures are independent of each other. The
 *     first field of `br_xxx_context` is always a pointer to the
 *     `br_xxx_vtable` structure; `br_xxx_init()` sets that pointer.
 *
 *   - `br_xxx_init(br_xxx_context *ctx)`
 *
 *     Initialise the provided context. Previous contents of the structure
 *     are ignored. This calls resets the context to the start of a new
 *     hash computation; it also sets the first field of the context
 *     structure (called `vtable`) to a pointer to the statically
 *     allocated constant `br_xxx_vtable` structure.
 *
 *   - `br_xxx_update(br_xxx_context *ctx, const void *data, size_t len)`
 *
 *     Add some more bytes to the hash computation represented by the
 *     provided context.
 *
 *   - `br_xxx_out(const br_xxx_context *ctx, void *out)`
 *
 *     Complete the hash computation and write the result in the provided
 *     buffer. The output buffer MUST be large enough to accommodate the
 *     result. The context is NOT modified by this operation, so this
 *     function can be used to get a "partial hash" while still keeping
 *     the possibility of adding more bytes to the input.
 *
 *   - `br_xxx_state(const br_xxx_context *ctx, void *out)`
 *
 *     Get a copy of the "current state" for the computation so far. For
 *     MD functions (MD5, SHA-1, SHA-2 family), this is the running state
 *     resulting from the processing of the last complete input block.
 *     Returned value is the current input length (in bytes).
 *
 *   - `br_xxx_set_state(br_xxx_context *ctx, const void *stb, uint64_t count)`
 *
 *     Set the internal state to the provided values. The 'stb' and
 *     'count' values shall match that which was obtained from
 *     `br_xxx_state()`. This restores the hash state only if the state
 *     values were at an appropriate block boundary. This does NOT set
 *     the `vtable` pointer in the context.
 *
 * Context structures can be discarded without any explicit deallocation.
 * Hash function implementations are purely software and don't reserve
 * any resources outside of the context structure itself.
 *
 *
 * ## Object-Oriented API
 *
 * For each hash function that follows the procedural API described
 * above, an object-oriented API is also provided. In that API, function
 * pointers from the vtable (`br_xxx_vtable`) are used. The vtable
 * incarnates object-oriented programming. An introduction on the OOP
 * concept used here can be read on the BearSSL Web site:<br />
 * &nbsp;&nbsp;&nbsp;[https://www.bearssl.org/oop.html](https://www.bearssl.org/oop.html)
 *
 * The vtable offers functions called `init()`, `update()`, `out()`,
 * `set()` and `set_state()`, which are in fact the functions from
 * the procedural API. That vtable also contains two informative fields:
 *
 *   - `context_size`
 *
 *     The size of the context structure (`br_xxx_context`), in bytes.
 *     This can be used by generic implementations to perform dynamic
 *     context allocation.
 *
 *   - `desc`
 *
 *     A "descriptor" field that encodes some information on the hash
 *     function: symbolic identifier, output size, state size,
 *     internal block size, details on the padding.
 *
 * Users of this object-oriented API (in particular generic HMAC
 * implementations) may make the following assumptions:
 *
 *   - Hash output size is no more than 64 bytes.
 *   - Hash internal state size is no more than 64 bytes.
 *   - Internal block size is a power of two, no less than 16 and no more
 *     than 256.
 *
 *
 * ## Implemented Hash Functions
 *
 * Implemented hash functions are:
 *
 * | Function  | Name    | Output length | State length |
 * | :-------- | :------ | :-----------: | :----------: |
 * | MD5       | md5     |     16        |     16       |
 * | SHA-1     | sha1    |     20        |     20       |
 * | SHA-224   | sha224  |     28        |     32       |
 * | SHA-256   | sha256  |     32        |     32       |
 * | SHA-384   | sha384  |     48        |     64       |
 * | SHA-512   | sha512  |     64        |     64       |
 * | MD5+SHA-1 | md5sha1 |     36        |     36       |
 *
 * (MD5+SHA-1 is the concatenation of MD5 and SHA-1 computed over the
 * same input; in the implementation, the internal data buffer is
 * shared, thus making it more memory-efficient than separate MD5 and
 * SHA-1. It can be useful in implementing SSL 3.0, TLS 1.0 and TLS
 * 1.1.)
 *
 *
 * ## Multi-Hasher
 *
 * An aggregate hasher is provided, that can compute several standard
 * hash functions in parallel. It uses `br_multihash_context` and a
 * procedural API. It is configured with the implementations (the vtables)
 * that it should use; it will then compute all these hash functions in
 * parallel, on the same input. It is meant to be used in cases when the
 * hash of an object will be used, but the exact hash function is not
 * known yet (typically, streamed processing on X.509 certificates).
 *
 * Only the standard hash functions (MD5, SHA-1, SHA-224, SHA-256, SHA-384
 * and SHA-512) are supported by the multi-hasher.
 *
 *
 * ## GHASH
 *
 * GHASH is not a generic hash function; it is a _universal_ hash function,
 * which, as the name does not say, means that it CANNOT be used in most
 * places where a hash function is needed. GHASH is used within the GCM
 * encryption mode, to provide the checked integrity functionality.
 *
 * A GHASH implementation is basically a function that uses the type defined
 * in this file under the name `br_ghash`:
 *
 *     typedef void (*br_ghash)(void *y, const void *h, const void *data, size_t len);
 *
 * The `y` pointer refers to a 16-byte value which is used as input, and
 * receives the output of the GHASH invocation. `h` is a 16-byte secret
 * value (that serves as key). `data` and `len` define the input data.
 *
 * Three GHASH implementations are provided, all constant-time, based on
 * the use of integer multiplications with appropriate masking to cancel
 * carry propagation.
 */

/**
 * \brief Class type for hash function implementations.
 *
 * A `br_hash_class` instance references the methods implementing a hash
 * function. Constant instances of this structure are defined for each
 * implemented hash function. Such instances are also called "vtables".
 *
 * Vtables are used to support object-oriented programming, as
 * described on [the BearSSL Web site](https://www.bearssl.org/oop.html).
 */
typedef struct br_hash_class_ br_hash_class;
struct br_hash_class_ {
	/**
	 * \brief Size (in bytes) of the context structure appropriate for
	 * computing this hash function.
	 */
	size_t context_size;

	/**
	 * \brief Descriptor word that contains information about the hash
	 * function.
	 *
	 * For each word `xxx` described below, use `BR_HASHDESC_xxx_OFF`
	 * and `BR_HASHDESC_xxx_MASK` to access the specific value, as
	 * follows:
	 *
	 *     (hf->desc >> BR_HASHDESC_xxx_OFF) & BR_HASHDESC_xxx_MASK
	 *
	 * The defined elements are:
	 *
	 *  - `ID`: the symbolic identifier for the function, as defined
	 *    in [TLS](https://tools.ietf.org/html/rfc5246#section-7.4.1.4.1)
	 *    (MD5 = 1, SHA-1 = 2,...).
	 *
	 *  - `OUT`: hash output size, in bytes.
	 *
	 *  - `STATE`: internal running state size, in bytes.
	 *
	 *  - `LBLEN`: base-2 logarithm for the internal block size, as
	 *    defined for HMAC processing (this is 6 for MD5, SHA-1, SHA-224
	 *    and SHA-256, since these functions use 64-byte blocks; for
	 *    SHA-384 and SHA-512, this is 7, corresponding to their
	 *    128-byte blocks).
	 *
	 * The descriptor may contain a few other flags.
	 */
	uint32_t desc;

	/**
	 * \brief Initialisation method.
	 *
	 * This method takes as parameter a pointer to a context area,
	 * that it initialises. The first field of the context is set
	 * to this vtable; other elements are initialised for a new hash
	 * computation.
	 *
	 * \param ctx   pointer to (the first field of) the context.
	 */
	void (*init)(const br_hash_class **ctx);

	/**
	 * \brief Data injection method.
	 *
	 * The `len` bytes starting at address `data` are injected into
	 * the running hash computation incarnated by the specified
	 * context. The context is updated accordingly. It is allowed
	 * to have `len == 0`, in which case `data` is ignored (and could
	 * be `NULL`), and nothing happens.
	 * on the input data.
	 *
	 * \param ctx    pointer to (the first field of) the context.
	 * \param data   pointer to the first data byte to inject.
	 * \param len    number of bytes to inject.
	 */
	void (*update)(const br_hash_class **ctx, const void *data, size_t len);

	/**
	 * \brief Produce hash output.
	 *
	 * The hash output corresponding to all data bytes injected in the
	 * context since the last `init()` call is computed, and written
	 * in the buffer pointed to by `dst`. The hash output size depends
	 * on the implemented hash function (e.g. 16 bytes for MD5).
	 * The context is _not_ modified by this call, so further bytes
	 * may be afterwards injected to continue the current computation.
	 *
	 * \param ctx   pointer to (the first field of) the context.
	 * \param dst   destination buffer for the hash output.
	 */
	void (*out)(const br_hash_class *const *ctx, void *dst);

	/**
	 * \brief Get running state.
	 *
	 * This method saves the current running state into the `dst`
	 * buffer. What constitutes the "running state" depends on the
	 * hash function; for Merkle-Damgård hash functions (like
	 * MD5 or SHA-1), this is the output obtained after processing
	 * each block. The number of bytes injected so far is returned.
	 * The context is not modified by this call.
	 *
	 * \param ctx   pointer to (the first field of) the context.
	 * \param dst   destination buffer for the state.
	 * \return  the injected total byte length.
	 */
	uint64_t (*state)(const br_hash_class *const *ctx, void *dst);

	/**
	 * \brief Set running state.
	 *
	 * This methods replaces the running state for the function.
	 *
	 * \param ctx     pointer to (the first field of) the context.
	 * \param stb     source buffer for the state.
	 * \param count   injected total byte length.
	 */
	void (*set_state)(const br_hash_class **ctx,
		const void *stb, uint64_t count);
};

#ifndef BR_DOXYGEN_IGNORE
#define BR_HASHDESC_ID(id)           ((uint32_t)(id) << BR_HASHDESC_ID_OFF)
#define BR_HASHDESC_ID_OFF           0
#define BR_HASHDESC_ID_MASK          0xFF

#define BR_HASHDESC_OUT(size)        ((uint32_t)(size) << BR_HASHDESC_OUT_OFF)
#define BR_HASHDESC_OUT_OFF          8
#define BR_HASHDESC_OUT_MASK         0x7F

#define BR_HASHDESC_STATE(size)      ((uint32_t)(size) << BR_HASHDESC_STATE_OFF)
#define BR_HASHDESC_STATE_OFF        15
#define BR_HASHDESC_STATE_MASK       0xFF

#define BR_HASHDESC_LBLEN(ls)        ((uint32_t)(ls) << BR_HASHDESC_LBLEN_OFF)
#define BR_HASHDESC_LBLEN_OFF        23
#define BR_HASHDESC_LBLEN_MASK       0x0F

#define BR_HASHDESC_MD_PADDING       ((uint32_t)1 << 28)
#define BR_HASHDESC_MD_PADDING_128   ((uint32_t)1 << 29)
#define BR_HASHDESC_MD_PADDING_BE    ((uint32_t)1 << 30)
#endif

/*
 * Specific hash functions.
 *
 * Rules for contexts:
 * -- No interior pointer.
 * -- No pointer to external dynamically allocated resources.
 * -- First field is called 'vtable' and is a pointer to a
 *    const-qualified br_hash_class instance (pointer is set by init()).
 * -- SHA-224 and SHA-256 contexts are identical.
 * -- SHA-384 and SHA-512 contexts are identical.
 *
 * Thus, contexts can be moved and cloned to capture the hash function
 * current state; and there is no need for any explicit "release" function.
 */

/**
 * \brief Symbolic identifier for MD5.
 */
#define br_md5_ID     1

/**
 * \brief MD5 output size (in bytes).
 */
#define br_md5_SIZE   16

/**
 * \brief Constant vtable for MD5.
 */
extern const br_hash_class br_md5_vtable;

/**
 * \brief MD5 context.
 *
 * First field is a pointer to the vtable; it is set by the initialisation
 * function. Other fields are not supposed to be accessed by user code.
 */
typedef struct {
	/**
	 * \brief Pointer to vtable for this context.
	 */
	const br_hash_class *vtable;
#ifndef BR_DOXYGEN_IGNORE
	unsigned char buf[64];
	uint64_t count;
	uint32_t val[4];
#endif
} br_md5_context;

/**
 * \brief MD5 context initialisation.
 *
 * This function initialises or resets a context for a new MD5
 * computation. It also sets the vtable pointer.
 *
 * \param ctx   pointer to the context structure.
 */
void br_md5_init(br_md5_context *ctx);

/**
 * \brief Inject some data bytes in a running MD5 computation.
 *
 * The provided context is updated with some data bytes. If the number
 * of bytes (`len`) is zero, then the data pointer (`data`) is ignored
 * and may be `NULL`, and this function does nothing.
 *
 * \param ctx    pointer to the context structure.
 * \param data   pointer to the injected data.
 * \param len    injected data length (in bytes).
 */
void br_md5_update(br_md5_context *ctx, const void *data, size_t len);

/**
 * \brief Compute MD5 output.
 *
 * The MD5 output for the concatenation of all bytes injected in the
 * provided context since the last initialisation or reset call, is
 * computed and written in the buffer pointed to by `out`. The context
 * itself is not modified, so extra bytes may be injected afterwards
 * to continue that computation.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the hash output.
 */
void br_md5_out(const br_md5_context *ctx, void *out);

/**
 * \brief Save MD5 running state.
 *
 * The running state for MD5 (output of the last internal block
 * processing) is written in the buffer pointed to by `out`. The
 * number of bytes injected since the last initialisation or reset
 * call is returned. The context is not modified.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the running state.
 * \return  the injected total byte length.
 */
uint64_t br_md5_state(const br_md5_context *ctx, void *out);

/**
 * \brief Restore MD5 running state.
 *
 * The running state for MD5 is set to the provided values.
 *
 * \param ctx     pointer to the context structure.
 * \param stb     source buffer for the running state.
 * \param count   the injected total byte length.
 */
void br_md5_set_state(br_md5_context *ctx, const void *stb, uint64_t count);

/**
 * \brief Symbolic identifier for SHA-1.
 */
#define br_sha1_ID     2

/**
 * \brief SHA-1 output size (in bytes).
 */
#define br_sha1_SIZE   20

/**
 * \brief Constant vtable for SHA-1.
 */
extern const br_hash_class br_sha1_vtable;

/**
 * \brief SHA-1 context.
 *
 * First field is a pointer to the vtable; it is set by the initialisation
 * function. Other fields are not supposed to be accessed by user code.
 */
typedef struct {
	/**
	 * \brief Pointer to vtable for this context.
	 */
	const br_hash_class *vtable;
#ifndef BR_DOXYGEN_IGNORE
	unsigned char buf[64];
	uint64_t count;
	uint32_t val[5];
#endif
} br_sha1_context;

/**
 * \brief SHA-1 context initialisation.
 *
 * This function initialises or resets a context for a new SHA-1
 * computation. It also sets the vtable pointer.
 *
 * \param ctx   pointer to the context structure.
 */
void br_sha1_init(br_sha1_context *ctx);

/**
 * \brief Inject some data bytes in a running SHA-1 computation.
 *
 * The provided context is updated with some data bytes. If the number
 * of bytes (`len`) is zero, then the data pointer (`data`) is ignored
 * and may be `NULL`, and this function does nothing.
 *
 * \param ctx    pointer to the context structure.
 * \param data   pointer to the injected data.
 * \param len    injected data length (in bytes).
 */
void br_sha1_update(br_sha1_context *ctx, const void *data, size_t len);

/**
 * \brief Compute SHA-1 output.
 *
 * The SHA-1 output for the concatenation of all bytes injected in the
 * provided context since the last initialisation or reset call, is
 * computed and written in the buffer pointed to by `out`. The context
 * itself is not modified, so extra bytes may be injected afterwards
 * to continue that computation.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the hash output.
 */
void br_sha1_out(const br_sha1_context *ctx, void *out);

/**
 * \brief Save SHA-1 running state.
 *
 * The running state for SHA-1 (output of the last internal block
 * processing) is written in the buffer pointed to by `out`. The
 * number of bytes injected since the last initialisation or reset
 * call is returned. The context is not modified.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the running state.
 * \return  the injected total byte length.
 */
uint64_t br_sha1_state(const br_sha1_context *ctx, void *out);

/**
 * \brief Restore SHA-1 running state.
 *
 * The running state for SHA-1 is set to the provided values.
 *
 * \param ctx     pointer to the context structure.
 * \param stb     source buffer for the running state.
 * \param count   the injected total byte length.
 */
void br_sha1_set_state(br_sha1_context *ctx, const void *stb, uint64_t count);

/**
 * \brief Symbolic identifier for SHA-224.
 */
#define br_sha224_ID     3

/**
 * \brief SHA-224 output size (in bytes).
 */
#define br_sha224_SIZE   28

/**
 * \brief Constant vtable for SHA-224.
 */
extern const br_hash_class br_sha224_vtable;

/**
 * \brief SHA-224 context.
 *
 * First field is a pointer to the vtable; it is set by the initialisation
 * function. Other fields are not supposed to be accessed by user code.
 */
typedef struct {
	/**
	 * \brief Pointer to vtable for this context.
	 */
	const br_hash_class *vtable;
#ifndef BR_DOXYGEN_IGNORE
	unsigned char buf[64];
	uint64_t count;
	uint32_t val[8];
#endif
} br_sha224_context;

/**
 * \brief SHA-224 context initialisation.
 *
 * This function initialises or resets a context for a new SHA-224
 * computation. It also sets the vtable pointer.
 *
 * \param ctx   pointer to the context structure.
 */
void br_sha224_init(br_sha224_context *ctx);

/**
 * \brief Inject some data bytes in a running SHA-224 computation.
 *
 * The provided context is updated with some data bytes. If the number
 * of bytes (`len`) is zero, then the data pointer (`data`) is ignored
 * and may be `NULL`, and this function does nothing.
 *
 * \param ctx    pointer to the context structure.
 * \param data   pointer to the injected data.
 * \param len    injected data length (in bytes).
 */
void br_sha224_update(br_sha224_context *ctx, const void *data, size_t len);

/**
 * \brief Compute SHA-224 output.
 *
 * The SHA-224 output for the concatenation of all bytes injected in the
 * provided context since the last initialisation or reset call, is
 * computed and written in the buffer pointed to by `out`. The context
 * itself is not modified, so extra bytes may be injected afterwards
 * to continue that computation.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the hash output.
 */
void br_sha224_out(const br_sha224_context *ctx, void *out);

/**
 * \brief Save SHA-224 running state.
 *
 * The running state for SHA-224 (output of the last internal block
 * processing) is written in the buffer pointed to by `out`. The
 * number of bytes injected since the last initialisation or reset
 * call is returned. The context is not modified.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the running state.
 * \return  the injected total byte length.
 */
uint64_t br_sha224_state(const br_sha224_context *ctx, void *out);

/**
 * \brief Restore SHA-224 running state.
 *
 * The running state for SHA-224 is set to the provided values.
 *
 * \param ctx     pointer to the context structure.
 * \param stb     source buffer for the running state.
 * \param count   the injected total byte length.
 */
void br_sha224_set_state(br_sha224_context *ctx,
	const void *stb, uint64_t count);

/**
 * \brief Symbolic identifier for SHA-256.
 */
#define br_sha256_ID     4

/**
 * \brief SHA-256 output size (in bytes).
 */
#define br_sha256_SIZE   32

/**
 * \brief Constant vtable for SHA-256.
 */
extern const br_hash_class br_sha256_vtable;

#ifdef BR_DOXYGEN_IGNORE
/**
 * \brief SHA-256 context.
 *
 * First field is a pointer to the vtable; it is set by the initialisation
 * function. Other fields are not supposed to be accessed by user code.
 */
typedef struct {
	/**
	 * \brief Pointer to vtable for this context.
	 */
	const br_hash_class *vtable;
} br_sha256_context;
#else
typedef br_sha224_context br_sha256_context;
#endif

/**
 * \brief SHA-256 context initialisation.
 *
 * This function initialises or resets a context for a new SHA-256
 * computation. It also sets the vtable pointer.
 *
 * \param ctx   pointer to the context structure.
 */
void br_sha256_init(br_sha256_context *ctx);

#ifdef BR_DOXYGEN_IGNORE
/**
 * \brief Inject some data bytes in a running SHA-256 computation.
 *
 * The provided context is updated with some data bytes. If the number
 * of bytes (`len`) is zero, then the data pointer (`data`) is ignored
 * and may be `NULL`, and this function does nothing.
 *
 * \param ctx    pointer to the context structure.
 * \param data   pointer to the injected data.
 * \param len    injected data length (in bytes).
 */
void br_sha256_update(br_sha256_context *ctx, const void *data, size_t len);
#else
#define br_sha256_update      br_sha224_update
#endif

/**
 * \brief Compute SHA-256 output.
 *
 * The SHA-256 output for the concatenation of all bytes injected in the
 * provided context since the last initialisation or reset call, is
 * computed and written in the buffer pointed to by `out`. The context
 * itself is not modified, so extra bytes may be injected afterwards
 * to continue that computation.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the hash output.
 */
void br_sha256_out(const br_sha256_context *ctx, void *out);

#if BR_DOXYGEN_IGNORE
/**
 * \brief Save SHA-256 running state.
 *
 * The running state for SHA-256 (output of the last internal block
 * processing) is written in the buffer pointed to by `out`. The
 * number of bytes injected since the last initialisation or reset
 * call is returned. The context is not modified.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the running state.
 * \return  the injected total byte length.
 */
uint64_t br_sha256_state(const br_sha256_context *ctx, void *out);
#else
#define br_sha256_state       br_sha224_state
#endif

#if BR_DOXYGEN_IGNORE
/**
 * \brief Restore SHA-256 running state.
 *
 * The running state for SHA-256 is set to the provided values.
 *
 * \param ctx     pointer to the context structure.
 * \param stb     source buffer for the running state.
 * \param count   the injected total byte length.
 */
void br_sha256_set_state(br_sha256_context *ctx,
	const void *stb, uint64_t count);
#else
#define br_sha256_set_state   br_sha224_set_state
#endif

/**
 * \brief Symbolic identifier for SHA-384.
 */
#define br_sha384_ID     5

/**
 * \brief SHA-384 output size (in bytes).
 */
#define br_sha384_SIZE   48

/**
 * \brief Constant vtable for SHA-384.
 */
extern const br_hash_class br_sha384_vtable;

/**
 * \brief SHA-384 context.
 *
 * First field is a pointer to the vtable; it is set by the initialisation
 * function. Other fields are not supposed to be accessed by user code.
 */
typedef struct {
	/**
	 * \brief Pointer to vtable for this context.
	 */
	const br_hash_class *vtable;
#ifndef BR_DOXYGEN_IGNORE
	unsigned char buf[128];
	uint64_t count;
	uint64_t val[8];
#endif
} br_sha384_context;

/**
 * \brief SHA-384 context initialisation.
 *
 * This function initialises or resets a context for a new SHA-384
 * computation. It also sets the vtable pointer.
 *
 * \param ctx   pointer to the context structure.
 */
void br_sha384_init(br_sha384_context *ctx);

/**
 * \brief Inject some data bytes in a running SHA-384 computation.
 *
 * The provided context is updated with some data bytes. If the number
 * of bytes (`len`) is zero, then the data pointer (`data`) is ignored
 * and may be `NULL`, and this function does nothing.
 *
 * \param ctx    pointer to the context structure.
 * \param data   pointer to the injected data.
 * \param len    injected data length (in bytes).
 */
void br_sha384_update(br_sha384_context *ctx, const void *data, size_t len);

/**
 * \brief Compute SHA-384 output.
 *
 * The SHA-384 output for the concatenation of all bytes injected in the
 * provided context since the last initialisation or reset call, is
 * computed and written in the buffer pointed to by `out`. The context
 * itself is not modified, so extra bytes may be injected afterwards
 * to continue that computation.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the hash output.
 */
void br_sha384_out(const br_sha384_context *ctx, void *out);

/**
 * \brief Save SHA-384 running state.
 *
 * The running state for SHA-384 (output of the last internal block
 * processing) is written in the buffer pointed to by `out`. The
 * number of bytes injected since the last initialisation or reset
 * call is returned. The context is not modified.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the running state.
 * \return  the injected total byte length.
 */
uint64_t br_sha384_state(const br_sha384_context *ctx, void *out);

/**
 * \brief Restore SHA-384 running state.
 *
 * The running state for SHA-384 is set to the provided values.
 *
 * \param ctx     pointer to the context structure.
 * \param stb     source buffer for the running state.
 * \param count   the injected total byte length.
 */
void br_sha384_set_state(br_sha384_context *ctx,
	const void *stb, uint64_t count);

/**
 * \brief Symbolic identifier for SHA-512.
 */
#define br_sha512_ID     6

/**
 * \brief SHA-512 output size (in bytes).
 */
#define br_sha512_SIZE   64

/**
 * \brief Constant vtable for SHA-512.
 */
extern const br_hash_class br_sha512_vtable;

#ifdef BR_DOXYGEN_IGNORE
/**
 * \brief SHA-512 context.
 *
 * First field is a pointer to the vtable; it is set by the initialisation
 * function. Other fields are not supposed to be accessed by user code.
 */
typedef struct {
	/**
	 * \brief Pointer to vtable for this context.
	 */
	const br_hash_class *vtable;
} br_sha512_context;
#else
typedef br_sha384_context br_sha512_context;
#endif

/**
 * \brief SHA-512 context initialisation.
 *
 * This function initialises or resets a context for a new SHA-512
 * computation. It also sets the vtable pointer.
 *
 * \param ctx   pointer to the context structure.
 */
void br_sha512_init(br_sha512_context *ctx);

#ifdef BR_DOXYGEN_IGNORE
/**
 * \brief Inject some data bytes in a running SHA-512 computation.
 *
 * The provided context is updated with some data bytes. If the number
 * of bytes (`len`) is zero, then the data pointer (`data`) is ignored
 * and may be `NULL`, and this function does nothing.
 *
 * \param ctx    pointer to the context structure.
 * \param data   pointer to the injected data.
 * \param len    injected data length (in bytes).
 */
void br_sha512_update(br_sha512_context *ctx, const void *data, size_t len);
#else
#define br_sha512_update   br_sha384_update
#endif

/**
 * \brief Compute SHA-512 output.
 *
 * The SHA-512 output for the concatenation of all bytes injected in the
 * provided context since the last initialisation or reset call, is
 * computed and written in the buffer pointed to by `out`. The context
 * itself is not modified, so extra bytes may be injected afterwards
 * to continue that computation.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the hash output.
 */
void br_sha512_out(const br_sha512_context *ctx, void *out);

#ifdef BR_DOXYGEN_IGNORE
/**
 * \brief Save SHA-512 running state.
 *
 * The running state for SHA-512 (output of the last internal block
 * processing) is written in the buffer pointed to by `out`. The
 * number of bytes injected since the last initialisation or reset
 * call is returned. The context is not modified.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the running state.
 * \return  the injected total byte length.
 */
uint64_t br_sha512_state(const br_sha512_context *ctx, void *out);
#else
#define br_sha512_state   br_sha384_state
#endif

#ifdef BR_DOXYGEN_IGNORE
/**
 * \brief Restore SHA-512 running state.
 *
 * The running state for SHA-512 is set to the provided values.
 *
 * \param ctx     pointer to the context structure.
 * \param stb     source buffer for the running state.
 * \param count   the injected total byte length.
 */
void br_sha512_set_state(br_sha512_context *ctx,
	const void *stb, uint64_t count);
#else
#define br_sha512_set_state   br_sha384_set_state
#endif

/*
 * "md5sha1" is a special hash function that computes both MD5 and SHA-1
 * on the same input, and produces a 36-byte output (MD5 and SHA-1
 * concatenation, in that order). State size is also 36 bytes.
 */

/**
 * \brief Symbolic identifier for MD5+SHA-1.
 *
 * MD5+SHA-1 is the concatenation of MD5 and SHA-1, computed over the
 * same input. It is not one of the functions identified in TLS, so
 * we give it a symbolic identifier of value 0.
 */
#define br_md5sha1_ID     0

/**
 * \brief MD5+SHA-1 output size (in bytes).
 */
#define br_md5sha1_SIZE   36

/**
 * \brief Constant vtable for MD5+SHA-1.
 */
extern const br_hash_class br_md5sha1_vtable;

/**
 * \brief MD5+SHA-1 context.
 *
 * First field is a pointer to the vtable; it is set by the initialisation
 * function. Other fields are not supposed to be accessed by user code.
 */
typedef struct {
	/**
	 * \brief Pointer to vtable for this context.
	 */
	const br_hash_class *vtable;
#ifndef BR_DOXYGEN_IGNORE
	unsigned char buf[64];
	uint64_t count;
	uint32_t val_md5[4];
	uint32_t val_sha1[5];
#endif
} br_md5sha1_context;

/**
 * \brief MD5+SHA-1 context initialisation.
 *
 * This function initialises or resets a context for a new SHA-512
 * computation. It also sets the vtable pointer.
 *
 * \param ctx   pointer to the context structure.
 */
void br_md5sha1_init(br_md5sha1_context *ctx);

/**
 * \brief Inject some data bytes in a running MD5+SHA-1 computation.
 *
 * The provided context is updated with some data bytes. If the number
 * of bytes (`len`) is zero, then the data pointer (`data`) is ignored
 * and may be `NULL`, and this function does nothing.
 *
 * \param ctx    pointer to the context structure.
 * \param data   pointer to the injected data.
 * \param len    injected data length (in bytes).
 */
void br_md5sha1_update(br_md5sha1_context *ctx, const void *data, size_t len);

/**
 * \brief Compute MD5+SHA-1 output.
 *
 * The MD5+SHA-1 output for the concatenation of all bytes injected in the
 * provided context since the last initialisation or reset call, is
 * computed and written in the buffer pointed to by `out`. The context
 * itself is not modified, so extra bytes may be injected afterwards
 * to continue that computation.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the hash output.
 */
void br_md5sha1_out(const br_md5sha1_context *ctx, void *out);

/**
 * \brief Save MD5+SHA-1 running state.
 *
 * The running state for MD5+SHA-1 (output of the last internal block
 * processing) is written in the buffer pointed to by `out`. The
 * number of bytes injected since the last initialisation or reset
 * call is returned. The context is not modified.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the running state.
 * \return  the injected total byte length.
 */
uint64_t br_md5sha1_state(const br_md5sha1_context *ctx, void *out);

/**
 * \brief Restore MD5+SHA-1 running state.
 *
 * The running state for MD5+SHA-1 is set to the provided values.
 *
 * \param ctx     pointer to the context structure.
 * \param stb     source buffer for the running state.
 * \param count   the injected total byte length.
 */
void br_md5sha1_set_state(br_md5sha1_context *ctx,
	const void *stb, uint64_t count);

/**
 * \brief Aggregate context for configurable hash function support.
 *
 * The `br_hash_compat_context` type is a type which is large enough to
 * serve as context for all standard hash functions defined above.
 */
typedef union {
	const br_hash_class *vtable;
	br_md5_context md5;
	br_sha1_context sha1;
	br_sha224_context sha224;
	br_sha256_context sha256;
	br_sha384_context sha384;
	br_sha512_context sha512;
	br_md5sha1_context md5sha1;
} br_hash_compat_context;

/*
 * The multi-hasher is a construct that handles hashing of the same input
 * data with several hash functions, with a single shared input buffer.
 * It can handle MD5, SHA-1, SHA-224, SHA-256, SHA-384 and SHA-512
 * simultaneously, though which functions are activated depends on
 * the set implementation pointers.
 */

/**
 * \brief Multi-hasher context structure.
 *
 * The multi-hasher runs up to six hash functions in the standard TLS list
 * (MD5, SHA-1, SHA-224, SHA-256, SHA-384 and SHA-512) in parallel, over
 * the same input.
 *
 * The multi-hasher does _not_ follow the OOP structure with a vtable.
 * Instead, it is configured with the vtables of the hash functions it
 * should run. Structure fields are not supposed to be accessed directly.
 */
typedef struct {
#ifndef BR_DOXYGEN_IGNORE
	unsigned char buf[128];
	uint64_t count;
	uint32_t val_32[25];
	uint64_t val_64[16];
	const br_hash_class *impl[6];
#endif
} br_multihash_context;

/**
 * \brief Clear a multi-hasher context.
 *
 * This should always be called once on a given context, _before_ setting
 * the implementation pointers.
 *
 * \param ctx   the multi-hasher context.
 */
void br_multihash_zero(br_multihash_context *ctx);

/**
 * \brief Set a hash function implementation.
 *
 * Implementations shall be set _after_ clearing the context (with
 * `br_multihash_zero()`) but _before_ initialising the computation
 * (with `br_multihash_init()`). The hash function implementation
 * MUST be one of the standard hash functions (MD5, SHA-1, SHA-224,
 * SHA-256, SHA-384 or SHA-512); it may also be `NULL` to remove
 * an implementation from the multi-hasher.
 *
 * \param ctx    the multi-hasher context.
 * \param id     the hash function symbolic identifier.
 * \param impl   the hash function vtable, or `NULL`.
 */
static inline void
br_multihash_setimpl(br_multihash_context *ctx,
	int id, const br_hash_class *impl)
{
	/*
	 * This code relies on hash functions ID being values 1 to 6,
	 * in the MD5 to SHA-512 order.
	 */
	ctx->impl[id - 1] = impl;
}

/**
 * \brief Get a hash function implementation.
 *
 * This function returns the currently configured vtable for a given
 * hash function (by symbolic ID). If no such function was configured in
 * the provided multi-hasher context, then this function returns `NULL`.
 *
 * \param ctx    the multi-hasher context.
 * \param id     the hash function symbolic identifier.
 * \return  the hash function vtable, or `NULL`.
 */
static inline const br_hash_class *
br_multihash_getimpl(const br_multihash_context *ctx, int id)
{
	return ctx->impl[id - 1];
}

/**
 * \brief Reset a multi-hasher context.
 *
 * This function prepares the context for a new hashing computation,
 * for all implementations configured at that point.
 *
 * \param ctx    the multi-hasher context.
 */
void br_multihash_init(br_multihash_context *ctx);

/**
 * \brief Inject some data bytes in a running multi-hashing computation.
 *
 * The provided context is updated with some data bytes. If the number
 * of bytes (`len`) is zero, then the data pointer (`data`) is ignored
 * and may be `NULL`, and this function does nothing.
 *
 * \param ctx    pointer to the context structure.
 * \param data   pointer to the injected data.
 * \param len    injected data length (in bytes).
 */
void br_multihash_update(br_multihash_context *ctx,
	const void *data, size_t len);

/**
 * \brief Compute a hash output from a multi-hasher.
 *
 * The hash output for the concatenation of all bytes injected in the
 * provided context since the last initialisation or reset call, is
 * computed and written in the buffer pointed to by `dst`. The hash
 * function to use is identified by `id` and must be one of the standard
 * hash functions. If that hash function was indeed configured in the
 * multi-hasher context, the corresponding hash value is written in
 * `dst` and its length (in bytes) is returned. If the hash function
 * was _not_ configured, then nothing is written in `dst` and 0 is
 * returned.
 *
 * The context itself is not modified, so extra bytes may be injected
 * afterwards to continue the hash computations.
 *
 * \param ctx   pointer to the context structure.
 * \param id    the hash function symbolic identifier.
 * \param dst   destination buffer for the hash output.
 * \return  the hash output length (in bytes), or 0.
 */
size_t br_multihash_out(const br_multihash_context *ctx, int id, void *dst);

/**
 * \brief Type for a GHASH implementation.
 *
 * GHASH is a sort of keyed hash meant to be used to implement GCM in
 * combination with a block cipher (with 16-byte blocks).
 *
 * The `y` array has length 16 bytes and is used for input and output; in
 * a complete GHASH run, it starts with an all-zero value. `h` is a 16-byte
 * value that serves as key (it is derived from the encryption key in GCM,
 * using the block cipher). The data length (`len`) is expressed in bytes.
 * The `y` array is updated.
 *
 * If the data length is not a multiple of 16, then the data is implicitly
 * padded with zeros up to the next multiple of 16. Thus, when using GHASH
 * in GCM, this method may be called twice, for the associated data and
 * for the ciphertext, respectively; the zero-padding implements exactly
 * the GCM rules.
 *
 * \param y      the array to update.
 * \param h      the GHASH key.
 * \param data   the input data (may be `NULL` if `len` is zero).
 * \param len    the input data length (in bytes).
 */
typedef void (*br_ghash)(void *y, const void *h, const void *data, size_t len);

/**
 * \brief GHASH implementation using multiplications (mixed 32-bit).
 *
 * This implementation uses multiplications of 32-bit values, with a
 * 64-bit result. It is constant-time (if multiplications are
 * constant-time).
 *
 * \param y      the array to update.
 * \param h      the GHASH key.
 * \param data   the input data (may be `NULL` if `len` is zero).
 * \param len    the input data length (in bytes).
 */
void br_ghash_ctmul(void *y, const void *h, const void *data, size_t len);

/**
 * \brief GHASH implementation using multiplications (strict 32-bit).
 *
 * This implementation uses multiplications of 32-bit values, with a
 * 32-bit result. It is usually somewhat slower than `br_ghash_ctmul()`,
 * but it is expected to be faster on architectures for which the
 * 32-bit multiplication opcode does not yield the upper 32 bits of the
 * product. It is constant-time (if multiplications are constant-time).
 *
 * \param y      the array to update.
 * \param h      the GHASH key.
 * \param data   the input data (may be `NULL` if `len` is zero).
 * \param len    the input data length (in bytes).
 */
void br_ghash_ctmul32(void *y, const void *h, const void *data, size_t len);

/**
 * \brief GHASH implementation using multiplications (64-bit).
 *
 * This implementation uses multiplications of 64-bit values, with a
 * 64-bit result. It is constant-time (if multiplications are
 * constant-time). It is substantially faster than `br_ghash_ctmul()`
 * and `br_ghash_ctmul32()` on most 64-bit architectures.
 *
 * \param y      the array to update.
 * \param h      the GHASH key.
 * \param data   the input data (may be `NULL` if `len` is zero).
 * \param len    the input data length (in bytes).
 */
void br_ghash_ctmul64(void *y, const void *h, const void *data, size_t len);

/**
 * \brief GHASH implementation using the `pclmulqdq` opcode (part of the
 * AES-NI instructions).
 *
 * This implementation is available only on x86 platforms where the
 * compiler supports the relevant intrinsic functions. Even if the
 * compiler supports these functions, the local CPU might not support
 * the `pclmulqdq` opcode, meaning that a call will fail with an
 * illegal instruction exception. To safely obtain a pointer to this
 * function when supported (or 0 otherwise), use `br_ghash_pclmul_get()`.
 *
 * \param y      the array to update.
 * \param h      the GHASH key.
 * \param data   the input data (may be `NULL` if `len` is zero).
 * \param len    the input data length (in bytes).
 */
void br_ghash_pclmul(void *y, const void *h, const void *data, size_t len);

/**
 * \brief Obtain the `pclmul` GHASH implementation, if available.
 *
 * If the `pclmul` implementation was compiled in the library (depending
 * on the compiler abilities) _and_ the local CPU appears to support the
 * opcode, then this function will return a pointer to the
 * `br_ghash_pclmul()` function. Otherwise, it will return `0`.
 *
 * \return  the `pclmul` GHASH implementation, or `0`.
 */
br_ghash br_ghash_pclmul_get(void);

/**
 * \brief GHASH implementation using the POWER8 opcodes.
 *
 * This implementation is available only on POWER8 platforms (and later).
 * To safely obtain a pointer to this function when supported (or 0
 * otherwise), use `br_ghash_pwr8_get()`.
 *
 * \param y      the array to update.
 * \param h      the GHASH key.
 * \param data   the input data (may be `NULL` if `len` is zero).
 * \param len    the input data length (in bytes).
 */
void br_ghash_pwr8(void *y, const void *h, const void *data, size_t len);

/**
 * \brief Obtain the `pwr8` GHASH implementation, if available.
 *
 * If the `pwr8` implementation was compiled in the library (depending
 * on the compiler abilities) _and_ the local CPU appears to support the
 * opcode, then this function will return a pointer to the
 * `br_ghash_pwr8()` function. Otherwise, it will return `0`.
 *
 * \return  the `pwr8` GHASH implementation, or `0`.
 */
br_ghash br_ghash_pwr8_get(void);

#ifdef __cplusplus
}
#endif

#endif
