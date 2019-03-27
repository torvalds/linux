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

#ifndef BR_BEARSSL_PEM_H__
#define BR_BEARSSL_PEM_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \file bearssl_pem.h
 *
 * # PEM Support
 *
 * PEM is a traditional encoding layer use to store binary objects (in
 * particular X.509 certificates, and private keys) in text files. While
 * the acronym comes from an old, defunct standard ("Privacy Enhanced
 * Mail"), the format has been reused, with some variations, by many
 * systems, and is a _de facto_ standard, even though it is not, actually,
 * specified in all clarity anywhere.
 *
 * ## Format Details
 *
 * BearSSL contains a generic, streamed PEM decoder, which handles the
 * following format:
 *
 *   - The input source (a sequence of bytes) is assumed to be the
 *     encoding of a text file in an ASCII-compatible charset. This
 *     includes ISO-8859-1, Windows-1252, and UTF-8 encodings. Each
 *     line ends on a newline character (U+000A LINE FEED). The
 *     U+000D CARRIAGE RETURN characters are ignored, so the code
 *     accepts both Windows-style and Unix-style line endings.
 *
 *   - Each object begins with a banner that occurs at the start of
 *     a line; the first banner characters are "`-----BEGIN `" (five
 *     dashes, the word "BEGIN", and a space). The banner matching is
 *     not case-sensitive.
 *
 *   - The _object name_ consists in the characters that follow the
 *     banner start sequence, up to the end of the line, but without
 *     trailing dashes (in "normal" PEM, there are five trailing
 *     dashes, but this implementation is not picky about these dashes).
 *     The BearSSL decoder normalises the name characters to uppercase
 *     (for ASCII letters only) and accepts names up to 127 characters.
 *
 *   - The object ends with a banner that again occurs at the start of
 *     a line, and starts with "`-----END `" (again case-insensitive).
 *
 *   - Between that start and end banner, only Base64 data shall occur.
 *     Base64 converts each sequence of three bytes into four
 *     characters; the four characters are ASCII letters, digits, "`+`"
 *     or "`-`" signs, and one or two "`=`" signs may occur in the last
 *     quartet. Whitespace is ignored (whitespace is any ASCII character
 *     of code 32 or less, so control characters are whitespace) and
 *     lines may have arbitrary length; the only restriction is that the
 *     four characters of a quartet must appear on the same line (no
 *     line break inside a quartet).
 *
 *   - A single file may contain more than one PEM object. Bytes that
 *     occur between objects are ignored.
 *
 *
 * ## PEM Decoder API
 *
 * The PEM decoder offers a state-machine API. The caller allocates a
 * decoder context, then injects source bytes. Source bytes are pushed
 * with `br_pem_decoder_push()`. The decoder stops accepting bytes when
 * it reaches an "event", which is either the start of an object, the
 * end of an object, or a decoding error within an object.
 *
 * The `br_pem_decoder_event()` function is used to obtain the current
 * event; it also clears it, thus allowing the decoder to accept more
 * bytes. When a object start event is raised, the decoder context
 * offers the found object name (normalised to ASCII uppercase).
 *
 * When an object is reached, the caller must set an appropriate callback
 * function, which will receive (by chunks) the decoded object data.
 *
 * Since the decoder context makes no dynamic allocation, it requires
 * no explicit deallocation.
 */

/**
 * \brief PEM decoder context.
 *
 * Contents are opaque (they should not be accessed directly).
 */
typedef struct {
#ifndef BR_DOXYGEN_IGNORE
	/* CPU for the T0 virtual machine. */
	struct {
		uint32_t *dp;
		uint32_t *rp;
		const unsigned char *ip;
	} cpu;
	uint32_t dp_stack[32];
	uint32_t rp_stack[32];
	int err;

	const unsigned char *hbuf;
	size_t hlen;

	void (*dest)(void *dest_ctx, const void *src, size_t len);
	void *dest_ctx;

	unsigned char event;
	char name[128];
	unsigned char buf[255];
	size_t ptr;
#endif
} br_pem_decoder_context;

/**
 * \brief Initialise a PEM decoder structure.
 *
 * \param ctx   decoder context to initialise.
 */
void br_pem_decoder_init(br_pem_decoder_context *ctx);

/**
 * \brief Push some bytes into the decoder.
 *
 * Returned value is the number of bytes actually consumed; this may be
 * less than the number of provided bytes if an event is raised. When an
 * event is raised, it must be read (with `br_pem_decoder_event()`);
 * until the event is read, this function will return 0.
 *
 * \param ctx    decoder context.
 * \param data   new data bytes.
 * \param len    number of new data bytes.
 * \return  the number of bytes actually received (may be less than `len`).
 */
size_t br_pem_decoder_push(br_pem_decoder_context *ctx,
	const void *data, size_t len);

/**
 * \brief Set the receiver for decoded data.
 *
 * When an object is entered, the provided function (with opaque context
 * pointer) will be called repeatedly with successive chunks of decoded
 * data for that object. If `dest` is set to 0, then decoded data is
 * simply ignored. The receiver can be set at any time, but, in practice,
 * it should be called immediately after receiving a "start of object"
 * event.
 *
 * \param ctx        decoder context.
 * \param dest       callback for receiving decoded data.
 * \param dest_ctx   opaque context pointer for the `dest` callback.
 */
static inline void
br_pem_decoder_setdest(br_pem_decoder_context *ctx,
	void (*dest)(void *dest_ctx, const void *src, size_t len),
	void *dest_ctx)
{
	ctx->dest = dest;
	ctx->dest_ctx = dest_ctx;
}

/**
 * \brief Get the last event.
 *
 * If an event was raised, then this function returns the event value, and
 * also clears it, thereby allowing the decoder to proceed. If no event
 * was raised since the last call to `br_pem_decoder_event()`, then this
 * function returns 0.
 *
 * \param ctx   decoder context.
 * \return  the raised event, or 0.
 */
int br_pem_decoder_event(br_pem_decoder_context *ctx);

/**
 * \brief Event: start of object.
 *
 * This event is raised when the start of a new object has been detected.
 * The object name (normalised to uppercase) can be accessed with
 * `br_pem_decoder_name()`.
 */
#define BR_PEM_BEGIN_OBJ   1

/**
 * \brief Event: end of object.
 *
 * This event is raised when the end of the current object is reached
 * (normally, i.e. with no decoding error).
 */
#define BR_PEM_END_OBJ     2

/**
 * \brief Event: decoding error.
 *
 * This event is raised when decoding fails within an object.
 * This formally closes the current object and brings the decoder back
 * to the "out of any object" state. The offending line in the source
 * is consumed.
 */
#define BR_PEM_ERROR       3

/**
 * \brief Get the name of the encountered object.
 *
 * The encountered object name is defined only when the "start of object"
 * event is raised. That name is normalised to uppercase (for ASCII letters
 * only) and does not include trailing dashes.
 *
 * \param ctx   decoder context.
 * \return  the current object name.
 */
static inline const char *
br_pem_decoder_name(br_pem_decoder_context *ctx)
{
	return ctx->name;
}

/**
 * \brief Encode an object in PEM.
 *
 * This function encodes the provided binary object (`data`, of length `len`
 * bytes) into PEM. The `banner` text will be included in the header and
 * footer (e.g. use `"CERTIFICATE"` to get a `"BEGIN CERTIFICATE"` header).
 *
 * The length (in characters) of the PEM output is returned; that length
 * does NOT include the terminating zero, that this function nevertheless
 * adds. If using the returned value for allocation purposes, the allocated
 * buffer size MUST be at least one byte larger than the returned size.
 *
 * If `dest` is `NULL`, then the encoding does not happen; however, the
 * length of the encoded object is still computed and returned.
 *
 * The `data` pointer may be `NULL` only if `len` is zero (when encoding
 * an object of length zero, which is not very useful), or when `dest`
 * is `NULL` (in that case, source data bytes are ignored).
 *
 * Some `flags` can be specified to alter the encoding behaviour:
 *
 *   - If `BR_PEM_LINE64` is set, then line-breaking will occur after
 *     every 64 characters of output, instead of the default of 76.
 *
 *   - If `BR_PEM_CRLF` is set, then end-of-line sequence will use
 *     CR+LF instead of a single LF.
 *
 * The `data` and `dest` buffers may overlap, in which case the source
 * binary data is destroyed in the process. Note that the PEM-encoded output
 * is always larger than the source binary.
 *
 * \param dest     the destination buffer (or `NULL`).
 * \param data     the source buffer (can be `NULL` in some cases).
 * \param len      the source length (in bytes).
 * \param banner   the PEM banner expression.
 * \param flags    the behavioural flags.
 * \return  the PEM object length (in characters), EXCLUDING the final zero.
 */
size_t br_pem_encode(void *dest, const void *data, size_t len,
	const char *banner, unsigned flags);

/**
 * \brief PEM encoding flag: split lines at 64 characters.
 */
#define BR_PEM_LINE64   0x0001

/**
 * \brief PEM encoding flag: use CR+LF line endings.
 */
#define BR_PEM_CRLF     0x0002

#ifdef __cplusplus
}
#endif

#endif
