///////////////////////////////////////////////////////////////////////////////
//
/// \file       check.h
/// \brief      Internal API to different integrity check functions
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_CHECK_H
#define LZMA_CHECK_H

#include "common.h"

// If the function for external SHA-256 is missing, use the internal SHA-256
// code. Due to how configure works, these defines can only get defined when
// both a usable header and a type have already been found.
#if !(defined(HAVE_CC_SHA256_INIT) \
		|| defined(HAVE_SHA256_INIT) \
		|| defined(HAVE_SHA256INIT))
#	define HAVE_INTERNAL_SHA256 1
#endif

#if defined(HAVE_INTERNAL_SHA256)
// Nothing
#elif defined(HAVE_COMMONCRYPTO_COMMONDIGEST_H)
#	include <CommonCrypto/CommonDigest.h>
#elif defined(HAVE_SHA256_H)
#	include <sys/types.h>
#	include <sha256.h>
#elif defined(HAVE_SHA2_H)
#	include <sys/types.h>
#	include <sha2.h>
#endif

#if defined(HAVE_INTERNAL_SHA256)
/// State for the internal SHA-256 implementation
typedef struct {
	/// Internal state
	uint32_t state[8];

	/// Size of the message excluding padding
	uint64_t size;
} lzma_sha256_state;
#elif defined(HAVE_CC_SHA256_CTX)
typedef CC_SHA256_CTX lzma_sha256_state;
#elif defined(HAVE_SHA256_CTX)
typedef SHA256_CTX lzma_sha256_state;
#elif defined(HAVE_SHA2_CTX)
typedef SHA2_CTX lzma_sha256_state;
#endif

#if defined(HAVE_INTERNAL_SHA256)
// Nothing
#elif defined(HAVE_CC_SHA256_INIT)
#	define LZMA_SHA256FUNC(x) CC_SHA256_ ## x
#elif defined(HAVE_SHA256_INIT)
#	define LZMA_SHA256FUNC(x) SHA256_ ## x
#elif defined(HAVE_SHA256INIT)
#	define LZMA_SHA256FUNC(x) SHA256 ## x
#endif

// Index hashing needs the best possible hash function (preferably
// a cryptographic hash) for maximum reliability.
#if defined(HAVE_CHECK_SHA256)
#	define LZMA_CHECK_BEST LZMA_CHECK_SHA256
#elif defined(HAVE_CHECK_CRC64)
#	define LZMA_CHECK_BEST LZMA_CHECK_CRC64
#else
#	define LZMA_CHECK_BEST LZMA_CHECK_CRC32
#endif


/// \brief      Structure to hold internal state of the check being calculated
///
/// \note       This is not in the public API because this structure may
///             change in future if new integrity check algorithms are added.
typedef struct {
	/// Buffer to hold the final result and a temporary buffer for SHA256.
	union {
		uint8_t u8[64];
		uint32_t u32[16];
		uint64_t u64[8];
	} buffer;

	/// Check-specific data
	union {
		uint32_t crc32;
		uint64_t crc64;
		lzma_sha256_state sha256;
	} state;

} lzma_check_state;


/// lzma_crc32_table[0] is needed by LZ encoder so we need to keep
/// the array two-dimensional.
#ifdef HAVE_SMALL
extern uint32_t lzma_crc32_table[1][256];
extern void lzma_crc32_init(void);
#else
extern const uint32_t lzma_crc32_table[8][256];
extern const uint64_t lzma_crc64_table[4][256];
#endif


/// \brief      Initialize *check depending on type
///
/// \return     LZMA_OK on success. LZMA_UNSUPPORTED_CHECK if the type is not
///             supported by the current version or build of liblzma.
///             LZMA_PROG_ERROR if type > LZMA_CHECK_ID_MAX.
extern void lzma_check_init(lzma_check_state *check, lzma_check type);

/// Update the check state
extern void lzma_check_update(lzma_check_state *check, lzma_check type,
		const uint8_t *buf, size_t size);

/// Finish the check calculation and store the result to check->buffer.u8.
extern void lzma_check_finish(lzma_check_state *check, lzma_check type);


#ifndef LZMA_SHA256FUNC

/// Prepare SHA-256 state for new input.
extern void lzma_sha256_init(lzma_check_state *check);

/// Update the SHA-256 hash state
extern void lzma_sha256_update(
		const uint8_t *buf, size_t size, lzma_check_state *check);

/// Finish the SHA-256 calculation and store the result to check->buffer.u8.
extern void lzma_sha256_finish(lzma_check_state *check);


#else

static inline void
lzma_sha256_init(lzma_check_state *check)
{
	LZMA_SHA256FUNC(Init)(&check->state.sha256);
}


static inline void
lzma_sha256_update(const uint8_t *buf, size_t size, lzma_check_state *check)
{
#if defined(HAVE_CC_SHA256_INIT) && SIZE_MAX > UINT32_MAX
	// Darwin's CC_SHA256_Update takes uint32_t as the buffer size,
	// so use a loop to support size_t.
	while (size > UINT32_MAX) {
		LZMA_SHA256FUNC(Update)(&check->state.sha256, buf, UINT32_MAX);
		buf += UINT32_MAX;
		size -= UINT32_MAX;
	}
#endif

	LZMA_SHA256FUNC(Update)(&check->state.sha256, buf, size);
}


static inline void
lzma_sha256_finish(lzma_check_state *check)
{
	LZMA_SHA256FUNC(Final)(check->buffer.u8, &check->state.sha256);
}

#endif

#endif
