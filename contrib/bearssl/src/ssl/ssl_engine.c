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

#include "inner.h"

#if 0
/* obsolete */

/*
 * If BR_USE_URANDOM is not defined, then try to autodetect its presence
 * through compiler macros.
 */
#ifndef BR_USE_URANDOM

/*
 * Macro values documented on:
 *    https://sourceforge.net/p/predef/wiki/OperatingSystems/
 *
 * Only the most common systems have been included here for now. This
 * should be enriched later on.
 */
#if defined _AIX \
	|| defined __ANDROID__ \
	|| defined __FreeBSD__ \
	|| defined __NetBSD__ \
	|| defined __OpenBSD__ \
	|| defined __DragonFly__ \
	|| defined __linux__ \
	|| (defined __sun && (defined __SVR4 || defined __svr4__)) \
	|| (defined __APPLE__ && defined __MACH__)
#define BR_USE_URANDOM   1
#endif

#endif

/*
 * If BR_USE_WIN32_RAND is not defined, perform autodetection here.
 */
#ifndef BR_USE_WIN32_RAND

#if defined _WIN32 || defined _WIN64
#define BR_USE_WIN32_RAND   1
#endif

#endif

#if BR_USE_URANDOM
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

#if BR_USE_WIN32_RAND
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "advapi32")
#endif

#endif

/* ==================================================================== */
/*
 * This part of the file does the low-level record management.
 */

/*
 * IMPLEMENTATION NOTES
 * ====================
 *
 * In this file, we designate by "input" (and the "i" letter) the "recv"
 * operations: incoming records from the peer, from which payload data
 * is obtained, and must be extracted by the application (or the SSL
 * handshake engine). Similarly, "output" (and the "o" letter) is for
 * "send": payload data injected by the application (and SSL handshake
 * engine), to be wrapped into records, that are then conveyed to the
 * peer over the transport medium.
 *
 * The input and output buffers may be distinct or shared. When
 * shared, input and output cannot occur concurrently; the caller
 * must make sure that it never needs to output data while input
 * data has been received. In practice, a shared buffer prevents
 * pipelining of HTTP requests, or similar protocols; however, a
 * shared buffer saves RAM.
 *
 * The input buffer is pointed to by 'ibuf' and has size 'ibuf_len';
 * the output buffer is pointed to by 'obuf' and has size 'obuf_len'.
 * From the size of these buffers is derived the maximum fragment
 * length, which will be honoured upon sending records; regardless of
 * that length, incoming records will be processed as long as they
 * fit in the input buffer, and their length still complies with the
 * protocol specification (maximum plaintext payload length is 16384
 * bytes).
 *
 * Three registers are used to manage buffering in ibuf, called ixa,
 * ixb and ixc. Similarly, three registers are used to manage buffering
 * in obuf, called oxa, oxb and oxc.
 *
 *
 * At any time, the engine is in one of the following modes:
 * -- Failed mode: an error occurs, no I/O can happen.
 * -- Input mode: the engine can either receive record bytes from the
 * transport layer, or it has some buffered payload bytes to yield.
 * -- Output mode: the engine can either receive payload bytes, or it
 * has some record bytes to send to the transport layer.
 * -- Input/Output mode: both input and output modes are active. When
 * the buffer is shared, this can happen only when the buffer is empty
 * (no buffered payload bytes or record bytes in either direction).
 *
 *
 * Failed mode:
 * ------------
 *
 * I/O failed for some reason (invalid received data, not enough room
 * for the next record...). No I/O may ever occur again for this context,
 * until an explicit reset is performed. This mode, and the error code,
 * are also used for protocol errors, especially handshake errors.
 *
 *
 * Input mode:
 * -----------
 *
 *  ixa   index within ibuf[] for the currently read data
 *  ixb   maximum index within ibuf[] for the currently read data
 *  ixc   number of bytes not yet received for the current record
 * 
 * -- When ixa == ixb, there is no available data for readers. When
 * ixa != ixb, there is available data and it starts at offset ixa.
 *
 * -- When waiting for the next record header, ixa and ixb are equal
 * and contain a value ranging from 0 to 4; ixc is equal to 5-ixa.
 *
 * -- When the header has been received, record data is obtained. The
 * ixc field records how many bytes are still needed to reach the
 * end of the current record.
 *
 *    ** If encryption is active, then ixa and ixb are kept equal, and
 *    point to the end of the currently received record bytes. When
 *    ixc reaches 0, decryption/MAC is applied, and ixa and ixb are
 *    adjusted.
 *
 *    ** If encryption is not active, then ixa and ixb are distinct
 *    and data can be read right away. Additional record data is
 *    obtained only when ixa == ixb.
 *
 * Note: in input mode and no encryption, records larger than the buffer
 * size are allowed. When encryption is active, the complete record must
 * fit within the buffer, since it cannot be decrypted/MACed until it
 * has been completely received.
 *
 * -- When receiving the next record header, 'version_in' contains the
 * expected input version (0 if not expecting a specific version); on
 * mismatch, the mode switches to 'failed'.
 *
 * -- When the header has been received, 'version_in' contains the received
 * version. It is up to the caller to check and adjust the 'version_in' field
 * to implement the required semantics.
 *
 * -- The 'record_type_in' field is updated with the incoming record type
 * when the next record header has been received.
 *
 *
 * Output mode:
 * ------------
 *
 *  oxa   index within obuf[] for the currently accumulated data
 *  oxb   maximum index within obuf[] for record data
 *  oxc   pointer for start of record data, and for record sending
 *
 * -- When oxa != oxb, more data can be accumulated into the current
 * record; when oxa == oxb, a closed record is being sent.
 *
 * -- When accumulating data, oxc points to the start of the data.
 *
 * -- During record sending, oxa (and oxb) point to the next record byte
 * to send, and oxc indicates the end of the current record.
 *
 * Note: sent records must fit within the buffer, since the header is
 * adjusted only when the complete record has been assembled.
 *
 * -- The 'version_out' and 'record_type_out' fields are used to build the
 * record header when the mode is switched to 'sending'.
 *
 *
 * Modes:
 * ------
 *
 * The state register iomode contains one of the following values:
 *
 *  BR_IO_FAILED   I/O failed
 *  BR_IO_IN       input mode
 *  BR_IO_OUT      output mode
 *  BR_IO_INOUT    input/output mode
 *
 * Whether encryption is active on incoming records is indicated by the
 * incrypt flag. For outgoing records, there is no such flag; "encryption"
 * is always considered active, but initially uses functions that do not
 * encrypt anything. The 'incrypt' flag is needed because when there is
 * no active encryption, records larger than the I/O buffer are accepted.
 *
 * Note: we do not support no-encryption modes (MAC only).
 *
 * TODO: implement GCM support
 *
 *
 * Misc:
 * -----
 *
 * 'max_frag_len' is the maximum plaintext size for an outgoing record.
 * By default, it is set to the maximum value that fits in the provided
 * buffers, in the following list: 512, 1024, 2048, 4096, 16384. The
 * caller may change it if needed, but the new value MUST still fit in
 * the buffers, and it MUST be one of the list above for compatibility
 * with the Maximum Fragment Length extension.
 *
 * For incoming records, only the total buffer length and current
 * encryption mode impact the maximum length for incoming records. The
 * 'max_frag_len' value is still adjusted so that records up to that
 * length can be both received and sent.
 *
 *
 * Offsets and lengths:
 * --------------------
 *
 * When sending fragments with TLS-1.1+, the maximum overhead is:
 *   5 bytes for the record header
 *   16 bytes for the explicit IV
 *   48 bytes for the MAC (HMAC/SHA-384)
 *   16 bytes for the padding (AES)
 * so a total of 85 extra bytes. Note that we support block cipher sizes
 * up to 16 bytes (AES) and HMAC output sizes up to 48 bytes (SHA-384).
 *
 * With TLS-1.0 and CBC mode, we apply a 1/n-1 split, for a maximum
 * overhead of:
 *   5 bytes for the first record header
 *   32 bytes for the first record payload (AES-CBC + HMAC/SHA-1)
 *   5 bytes for the second record header
 *   20 bytes for the MAC (HMAC/SHA-1)
 *   16 bytes for the padding (AES)
 *   -1 byte to account for the payload byte in the first record
 * so a total of 77 extra bytes at most, less than the 85 bytes above.
 * Note that with TLS-1.0, the MAC is HMAC with either MD5 or SHA-1, but
 * no other hash function.
 *
 * The implementation does not try to send larger records when the current
 * encryption mode has less overhead.
 *
 * Maximum input record overhead is:
 *   5 bytes for the record header
 *   16 bytes for the explicit IV (TLS-1.1+)
 *   48 bytes for the MAC (HMAC/SHA-384)
 *   256 bytes for the padding
 * so a total of 325 extra bytes.
 *
 * When receiving the next record header, it is written into the buffer
 * bytes 0 to 4 (inclusive). Record data is always written into buf[]
 * starting at offset 5. When encryption is active, the plaintext data
 * may start at a larger offset (e.g. because of an explicit IV).
 */

#define MAX_OUT_OVERHEAD    85
#define MAX_IN_OVERHEAD    325

/* see inner.h */
void
br_ssl_engine_fail(br_ssl_engine_context *rc, int err)
{
	if (rc->iomode != BR_IO_FAILED) {
		rc->iomode = BR_IO_FAILED;
		rc->err = err;
	}
}

/*
 * Adjust registers for a new incoming record.
 */
static void
make_ready_in(br_ssl_engine_context *rc)
{
	rc->ixa = rc->ixb = 0;
	rc->ixc = 5;
	if (rc->iomode == BR_IO_IN) {
		rc->iomode = BR_IO_INOUT;
	}
}

/*
 * Adjust registers for a new outgoing record.
 */
static void
make_ready_out(br_ssl_engine_context *rc)
{
	size_t a, b;

	a = 5;
	b = rc->obuf_len - a;
	rc->out.vtable->max_plaintext(&rc->out.vtable, &a, &b);
	if ((b - a) > rc->max_frag_len) {
		b = a + rc->max_frag_len;
	}
	rc->oxa = a;
	rc->oxb = b;
	rc->oxc = a;
	if (rc->iomode == BR_IO_OUT) {
		rc->iomode = BR_IO_INOUT;
	}
}

/* see inner.h */
void
br_ssl_engine_new_max_frag_len(br_ssl_engine_context *rc, unsigned max_frag_len)
{
	size_t nxb;

	rc->max_frag_len = max_frag_len;
	nxb = rc->oxc + max_frag_len;
	if (rc->oxa < rc->oxb && rc->oxb > nxb && rc->oxa < nxb) {
		rc->oxb = nxb;
	}
}

/* see bearssl_ssl.h */
void
br_ssl_engine_set_buffer(br_ssl_engine_context *rc,
	void *buf, size_t buf_len, int bidi)
{
	if (buf == NULL) {
		br_ssl_engine_set_buffers_bidi(rc, NULL, 0, NULL, 0);
	} else {
		/*
		 * In bidirectional mode, we want to maximise input
		 * buffer size, since we support arbitrary fragmentation
		 * when sending, but the peer will not necessarily
		 * comply to any low fragment length (in particular if
		 * we are the server, because the maximum fragment
		 * length extension is under client control).
		 *
		 * We keep a minimum size of 512 bytes for the plaintext
		 * of our outgoing records.
		 *
		 * br_ssl_engine_set_buffers_bidi() will compute the maximum
		 * fragment length for outgoing records by using the minimum
		 * of allocated spaces for both input and output records,
		 * rounded down to a standard length.
		 */
		if (bidi) {
			size_t w;

			if (buf_len < (512 + MAX_IN_OVERHEAD
				+ 512 + MAX_OUT_OVERHEAD))
			{
				rc->iomode = BR_IO_FAILED;
				rc->err = BR_ERR_BAD_PARAM;
				return;
			} else if (buf_len < (16384 + MAX_IN_OVERHEAD
				+ 512 + MAX_OUT_OVERHEAD))
			{
				w = 512 + MAX_OUT_OVERHEAD;
			} else {
				w = buf_len - (16384 + MAX_IN_OVERHEAD);
			}
			br_ssl_engine_set_buffers_bidi(rc,
				buf, buf_len - w,
				(unsigned char *)buf + w, w);
		} else {
			br_ssl_engine_set_buffers_bidi(rc,
				buf, buf_len, NULL, 0);
		}
	}
}

/* see bearssl_ssl.h */
void
br_ssl_engine_set_buffers_bidi(br_ssl_engine_context *rc,
	void *ibuf, size_t ibuf_len, void *obuf, size_t obuf_len)
{
	rc->iomode = BR_IO_INOUT;
	rc->incrypt = 0;
	rc->err = BR_ERR_OK;
	rc->version_in = 0;
	rc->record_type_in = 0;
	rc->version_out = 0;
	rc->record_type_out = 0;
	if (ibuf == NULL) {
		if (rc->ibuf == NULL) {
			br_ssl_engine_fail(rc, BR_ERR_BAD_PARAM);
		}
	} else {
		unsigned u;

		rc->ibuf = ibuf;
		rc->ibuf_len = ibuf_len;
		if (obuf == NULL) {
			obuf = ibuf;
			obuf_len = ibuf_len;
		}
		rc->obuf = obuf;
		rc->obuf_len = obuf_len;

		/*
		 * Compute the maximum fragment length, that fits for
		 * both incoming and outgoing records. This length will
		 * be used in fragment length negotiation, so we must
		 * honour it both ways. Regardless, larger incoming
		 * records will be accepted, as long as they fit in the
		 * actual buffer size.
		 */
		for (u = 14; u >= 9; u --) {
			size_t flen;

			flen = (size_t)1 << u;
			if (obuf_len >= flen + MAX_OUT_OVERHEAD
				&& ibuf_len >= flen + MAX_IN_OVERHEAD)
			{
				break;
			}
		}
		if (u == 8) {
			br_ssl_engine_fail(rc, BR_ERR_BAD_PARAM);
			return;
		} else if (u == 13) {
			u = 12;
		}
		rc->max_frag_len = (size_t)1 << u;
		rc->log_max_frag_len = u;
		rc->peer_log_max_frag_len = 0;
	}
	rc->out.vtable = &br_sslrec_out_clear_vtable;
	make_ready_in(rc);
	make_ready_out(rc);
}

/*
 * Clear buffers in both directions.
 */
static void
engine_clearbuf(br_ssl_engine_context *rc)
{
	make_ready_in(rc);
	make_ready_out(rc);
}

/*
 * Make sure the internal PRNG is initialised (but not necessarily
 * seeded properly yet).
 */
static int
rng_init(br_ssl_engine_context *cc)
{
	const br_hash_class *h;

	if (cc->rng_init_done != 0) {
		return 1;
	}

	/*
	 * If using TLS-1.2, then SHA-256 or SHA-384 must be present (or
	 * both); we prefer SHA-256 which is faster for 32-bit systems.
	 *
	 * If using TLS-1.0 or 1.1 then SHA-1 must be present.
	 *
	 * Though HMAC_DRBG/SHA-1 is, as far as we know, as safe as
	 * these things can be, we still prefer the SHA-2 functions over
	 * SHA-1, if only for public relations (known theoretical
	 * weaknesses of SHA-1 with regards to collisions are mostly
	 * irrelevant here, but they still make people nervous).
	 */
	h = br_multihash_getimpl(&cc->mhash, br_sha256_ID);
	if (!h) {
		h = br_multihash_getimpl(&cc->mhash, br_sha384_ID);
		if (!h) {
			h = br_multihash_getimpl(&cc->mhash,
				br_sha1_ID);
			if (!h) {
				br_ssl_engine_fail(cc, BR_ERR_BAD_STATE);
				return 0;
			}
		}
	}
	br_hmac_drbg_init(&cc->rng, h, NULL, 0);
	cc->rng_init_done = 1;
	return 1;
}

/* see inner.h */
int
br_ssl_engine_init_rand(br_ssl_engine_context *cc)
{
	if (!rng_init(cc)) {
		return 0;
	}

	/*
	 * We always try OS/hardware seeding once. If it works, then
	 * we assume proper seeding. If not, then external entropy must
	 * have been injected; otherwise, we report an error.
	 */
	if (!cc->rng_os_rand_done) {
		br_prng_seeder sd;

		sd = br_prng_seeder_system(NULL);
		if (sd != 0 && sd(&cc->rng.vtable)) {
			cc->rng_init_done = 2;
		}
		cc->rng_os_rand_done = 1;
	}
	if (cc->rng_init_done < 2) {
		br_ssl_engine_fail(cc, BR_ERR_NO_RANDOM);
		return 0;
	}
	return 1;
}

/* see bearssl_ssl.h */
void
br_ssl_engine_inject_entropy(br_ssl_engine_context *cc,
	const void *data, size_t len)
{
	/*
	 * Externally provided entropy is assumed to be "good enough"
	 * (we cannot really test its quality) so if the RNG structure
	 * could be initialised at all, then we marked the RNG as
	 * "properly seeded".
	 */
	if (!rng_init(cc)) {
		return;
	}
	br_hmac_drbg_update(&cc->rng, data, len);
	cc->rng_init_done = 2;
}

/*
 * We define a few internal functions that implement the low-level engine
 * API for I/O; the external API (br_ssl_engine_sendapp_buf() and similar
 * functions) is built upon these function, with special processing for
 * records which are not of type "application data".
 *
 *   recvrec_buf, recvrec_ack     receives bytes from transport medium
 *   sendrec_buf, sendrec_ack     send bytes to transport medium
 *   recvpld_buf, recvpld_ack     receives payload data from engine
 *   sendpld_buf, sendpld_ack     send payload data to engine
 */

static unsigned char *
recvrec_buf(const br_ssl_engine_context *rc, size_t *len)
{
	if (rc->shutdown_recv) {
		*len = 0;
		return NULL;
	}

	/*
	 * Bytes from the transport can be injected only if the mode is
	 * compatible (in or in/out), and ixa == ixb; ixc then contains
	 * the number of bytes that are still expected (but it may
	 * exceed our buffer size).
	 *
	 * We cannot get "stuck" here (buffer is full, but still more
	 * data is expected) because oversized records are detected when
	 * their header is processed.
	 */
	switch (rc->iomode) {
	case BR_IO_IN:
	case BR_IO_INOUT:
		if (rc->ixa == rc->ixb) {
			size_t z;

			z = rc->ixc;
			if (z > rc->ibuf_len - rc->ixa) {
				z = rc->ibuf_len - rc->ixa;
			}
			*len = z;
			return rc->ibuf + rc->ixa;
		}
		break;
	}
	*len = 0;
	return NULL;
}

static void
recvrec_ack(br_ssl_engine_context *rc, size_t len)
{
	unsigned char *pbuf;
	size_t pbuf_len;

	/*
	 * Adjust state if necessary (for a shared input/output buffer):
	 * we got some incoming bytes, so we cannot (temporarily) handle
	 * outgoing data.
	 */
	if (rc->iomode == BR_IO_INOUT && rc->ibuf == rc->obuf) {
		rc->iomode = BR_IO_IN;
	}

	/*
	 * Adjust data pointers.
	 */
	rc->ixb = (rc->ixa += len);
	rc->ixc -= len;

	/*
	 * If we are receiving a header and did not fully obtained it
	 * yet, then just wait for the next bytes.
	 */
	if (rc->ixa < 5) {
		return;
	}

	/*
	 * If we just obtained a full header, process it.
	 */
	if (rc->ixa == 5) {
		unsigned version;
		unsigned rlen;

		/*
		 * Get record type and version. We support only versions
		 * 3.x (if the version major number does not match, then
		 * we suppose that the record format is too alien for us
		 * to process it).
		 *
		 * Note: right now, we reject clients that try to send
		 * a ClientHello in a format compatible with SSL-2.0. It
		 * is unclear whether this will ever be supported; and
		 * if we want to support it, then this might be done in
		 * in the server-specific code, not here.
		 */
		rc->record_type_in = rc->ibuf[0];
		version = br_dec16be(rc->ibuf + 1);
		if ((version >> 8) != 3) {
			br_ssl_engine_fail(rc, BR_ERR_UNSUPPORTED_VERSION);
			return;
		}

		/*
		 * We ensure that successive records have the same
		 * version. The handshake code must check and adjust the
		 * variables when necessary to accommodate the protocol
		 * negotiation details.
		 */
		if (rc->version_in != 0 && rc->version_in != version) {
			br_ssl_engine_fail(rc, BR_ERR_BAD_VERSION);
			return;
		}
		rc->version_in = version;

		/*
		 * Decode record length. We must check that the length
		 * is valid (relatively to the current encryption mode)
		 * and also (if encryption is active) that the record
		 * will fit in our buffer.
		 *
		 * When no encryption is active, we can process records
		 * by chunks, and thus accept any record up to the
		 * maximum allowed plaintext length (16384 bytes).
		 */
		rlen = br_dec16be(rc->ibuf + 3);
		if (rc->incrypt) {
			if (!rc->in.vtable->check_length(
				&rc->in.vtable, rlen))
			{
				br_ssl_engine_fail(rc, BR_ERR_BAD_LENGTH);
				return;
			}
			if (rlen > (rc->ibuf_len - 5)) {
				br_ssl_engine_fail(rc, BR_ERR_TOO_LARGE);
				return;
			}
		} else {
			if (rlen > 16384) {
				br_ssl_engine_fail(rc, BR_ERR_BAD_LENGTH);
				return;
			}
		}

		/*
		 * If the record is completely empty then we must switch
		 * to a new record. Note that, in that case, we
		 * completely ignore the record type, which is fitting
		 * since we received no actual data of that type.
		 *
		 * A completely empty record is technically allowed as
		 * long as encryption/MAC is not active, i.e. before
		 * completion of the first handshake. It it still weird;
		 * it might conceptually be useful as a heartbeat or
		 * keep-alive mechanism while some lengthy operation is
		 * going on, e.g. interaction with a human user.
		 */
		if (rlen == 0) {
			make_ready_in(rc);
		} else {
			rc->ixa = rc->ixb = 5;
			rc->ixc = rlen;
		}
		return;
	}

	/*
	 * If there is no active encryption, then the data can be read
	 * right away. Note that we do not receive bytes from the
	 * transport medium when we still have payload bytes to be
	 * acknowledged.
	 */
	if (!rc->incrypt) {
		rc->ixa = 5;
		return;
	}

	/*
	 * Since encryption is active, we must wait for a full record
	 * before processing it.
	 */
	if (rc->ixc != 0) {
		return;
	}

	/*
	 * We got the full record. Decrypt it.
	 */
	pbuf_len = rc->ixa - 5;
	pbuf = rc->in.vtable->decrypt(&rc->in.vtable,
		rc->record_type_in, rc->version_in, rc->ibuf + 5, &pbuf_len);
	if (pbuf == 0) {
		br_ssl_engine_fail(rc, BR_ERR_BAD_MAC);
		return;
	}
	rc->ixa = (size_t)(pbuf - rc->ibuf);
	rc->ixb = rc->ixa + pbuf_len;

	/*
	 * Decryption may have yielded an empty record, in which case
	 * we get back to "ready" state immediately.
	 */
	if (rc->ixa == rc->ixb) {
		make_ready_in(rc);
	}
}

/* see inner.h */
int
br_ssl_engine_recvrec_finished(const br_ssl_engine_context *rc)
{
	switch (rc->iomode) {
	case BR_IO_IN:
	case BR_IO_INOUT:
		return rc->ixc == 0 || rc->ixa < 5;
	default:
		return 1;
	}
}

static unsigned char *
recvpld_buf(const br_ssl_engine_context *rc, size_t *len)
{
	/*
	 * There is payload data to be read only if the mode is
	 * compatible, and ixa != ixb.
	 */
	switch (rc->iomode) {
	case BR_IO_IN:
	case BR_IO_INOUT:
		*len = rc->ixb - rc->ixa;
		return (*len == 0) ? NULL : (rc->ibuf + rc->ixa);
	default:
		*len = 0;
		return NULL;
	}
}

static void
recvpld_ack(br_ssl_engine_context *rc, size_t len)
{
	rc->ixa += len;

	/*
	 * If we read all the available data, then we either expect
	 * the remainder of the current record (if the current record
	 * was not finished; this may happen when encryption is not
	 * active), or go to "ready" state.
	 */
	if (rc->ixa == rc->ixb) {
		if (rc->ixc == 0) {
			make_ready_in(rc);
		} else {
			rc->ixa = rc->ixb = 5;
		}
	}
}

static unsigned char *
sendpld_buf(const br_ssl_engine_context *rc, size_t *len)
{
	/*
	 * Payload data can be injected only if the current mode is
	 * compatible, and oxa != oxb.
	 */
	switch (rc->iomode) {
	case BR_IO_OUT:
	case BR_IO_INOUT:
		*len = rc->oxb - rc->oxa;
		return (*len == 0) ? NULL : (rc->obuf + rc->oxa);
	default:
		*len = 0;
		return NULL;
	}
}

/*
 * If some payload bytes have been accumulated, then wrap them into
 * an outgoing record. Otherwise, this function does nothing, unless
 * 'force' is non-zero, in which case an empty record is assembled.
 *
 * The caller must take care not to invoke this function if the engine
 * is not currently ready to receive payload bytes to send.
 */
static void
sendpld_flush(br_ssl_engine_context *rc, int force)
{
	size_t xlen;
	unsigned char *buf;

	if (rc->oxa == rc->oxb) {
		return;
	}
	xlen = rc->oxa - rc->oxc;
	if (xlen == 0 && !force) {
		return;
	}
	buf = rc->out.vtable->encrypt(&rc->out.vtable,
		rc->record_type_out, rc->version_out,
		rc->obuf + rc->oxc, &xlen);
	rc->oxb = rc->oxa = (size_t)(buf - rc->obuf);
	rc->oxc = rc->oxa + xlen;
}

static void
sendpld_ack(br_ssl_engine_context *rc, size_t len)
{
	/*
	 * If using a shared buffer, then we may have to modify the
	 * current mode.
	 */
	if (rc->iomode == BR_IO_INOUT && rc->ibuf == rc->obuf) {
		rc->iomode = BR_IO_OUT;
	}
	rc->oxa += len;
	if (rc->oxa >= rc->oxb) {
		/*
		 * Set oxb to one more than oxa so that sendpld_flush()
		 * does not mistakingly believe that a record is
		 * already prepared and being sent.
		 */
		rc->oxb = rc->oxa + 1;
		sendpld_flush(rc, 0);
	}
}

static unsigned char *
sendrec_buf(const br_ssl_engine_context *rc, size_t *len)
{
	/*
	 * When still gathering payload bytes, oxc points to the start
	 * of the record data, so oxc <= oxa. However, when a full
	 * record has been completed, oxc points to the end of the record,
	 * so oxc > oxa.
	 */
	switch (rc->iomode) {
	case BR_IO_OUT:
	case BR_IO_INOUT:
		if (rc->oxc > rc->oxa) {
			*len = rc->oxc - rc->oxa;
			return rc->obuf + rc->oxa;
		}
		break;
	}
	*len = 0;
	return NULL;
}

static void
sendrec_ack(br_ssl_engine_context *rc, size_t len)
{
	rc->oxb = (rc->oxa += len);
	if (rc->oxa == rc->oxc) {
		make_ready_out(rc);
	}
}

/*
 * Test whether there is some buffered outgoing record that still must
 * sent.
 */
static inline int
has_rec_tosend(const br_ssl_engine_context *rc)
{
	return rc->oxa == rc->oxb && rc->oxa != rc->oxc;
}

/*
 * The "no encryption" mode has no overhead. It limits the payload size
 * to the maximum size allowed by the standard (16384 bytes); the caller
 * is responsible for possibly enforcing a smaller fragment length.
 */
static void
clear_max_plaintext(const br_sslrec_out_clear_context *cc,
	size_t *start, size_t *end)
{
	size_t len;

	(void)cc;
	len = *end - *start;
	if (len > 16384) {
		*end = *start + 16384;
	}
}

/*
 * In "no encryption" mode, encryption is trivial (a no-operation) so
 * we just have to encode the header.
 */
static unsigned char *
clear_encrypt(br_sslrec_out_clear_context *cc,
	int record_type, unsigned version, void *data, size_t *data_len)
{
	unsigned char *buf;

	(void)cc;
	buf = (unsigned char *)data - 5;
	buf[0] = record_type;
	br_enc16be(buf + 1, version);
	br_enc16be(buf + 3, *data_len);
	*data_len += 5;
	return buf;
}

/* see bearssl_ssl.h */
const br_sslrec_out_class br_sslrec_out_clear_vtable = {
	sizeof(br_sslrec_out_clear_context),
	(void (*)(const br_sslrec_out_class *const *, size_t *, size_t *))
		&clear_max_plaintext,
	(unsigned char *(*)(const br_sslrec_out_class **,
		int, unsigned, void *, size_t *))
		&clear_encrypt
};

/* ==================================================================== */
/*
 * In this part of the file, we handle the various record types, and
 * communications with the handshake processor.
 */

/*
 * IMPLEMENTATION NOTES
 * ====================
 *
 * The handshake processor is written in T0 and runs as a coroutine.
 * It receives the contents of all records except application data, and
 * is responsible for producing the contents of all records except
 * application data.
 *
 * A state flag is maintained, which specifies whether application data
 * is acceptable or not. When it is set:
 *
 * -- Application data can be injected as payload data (provided that
 *    the output buffer is ready for that).
 *
 * -- Incoming application data records are accepted, and yield data
 *    that the caller may retrieve.
 *
 * When the flag is cleared, application data is not accepted from the
 * application, and incoming application data records trigger an error.
 *
 *
 * Records of type handshake, alert or change-cipher-spec are handled
 * by the handshake processor. The handshake processor is written in T0
 * and runs as a coroutine; it gets invoked whenever one of the following
 * situations is reached:
 *
 * -- An incoming record has type handshake, alert or change-cipher-spec,
 *    and yields data that can be read (zero-length records are thus
 *    ignored).
 *
 * -- An outgoing record has just finished being sent, and the "application
 *    data" flag is cleared.
 *
 * -- The caller wishes to perform a close (call to br_ssl_engine_close()).
 *
 * -- The caller wishes to perform a renegotiation (call to
 *    br_ssl_engine_renegotiate()).
 *
 * Whenever the handshake processor is entered, access to the payload
 * buffers is provided, along with some information about explicit
 * closures or renegotiations.
 */

/* see bearssl_ssl.h */
void
br_ssl_engine_set_suites(br_ssl_engine_context *cc,
	const uint16_t *suites, size_t suites_num)
{
	if ((suites_num * sizeof *suites) > sizeof cc->suites_buf) {
		br_ssl_engine_fail(cc, BR_ERR_BAD_PARAM);
		return;
	}
	memcpy(cc->suites_buf, suites, suites_num * sizeof *suites);
	cc->suites_num = suites_num;
}

/*
 * Give control to handshake processor. 'action' is 1 for a close,
 * 2 for a renegotiation, or 0 for a jump due to I/O completion.
 */
static void
jump_handshake(br_ssl_engine_context *cc, int action)
{
	/*
	 * We use a loop because the handshake processor actions may
	 * allow for more actions; namely, if the processor reads all
	 * input data, then it may allow for output data to be produced,
	 * in case of a shared in/out buffer.
	 */
	for (;;) {
		size_t hlen_in, hlen_out;

		/*
		 * Get input buffer. We do not want to provide
		 * application data to the handshake processor (we could
		 * get called with an explicit close or renegotiation
		 * while there is application data ready to be read).
		 */
		cc->hbuf_in = recvpld_buf(cc, &hlen_in);
		if (cc->hbuf_in != NULL
			&& cc->record_type_in == BR_SSL_APPLICATION_DATA)
		{
			hlen_in = 0;
		}

		/*
		 * Get output buffer. The handshake processor never
		 * leaves an unfinished outgoing record, so if there is
		 * buffered output, then it MUST be some application
		 * data, so the processor cannot write to it.
		 */
		cc->saved_hbuf_out = cc->hbuf_out = sendpld_buf(cc, &hlen_out);
		if (cc->hbuf_out != NULL && br_ssl_engine_has_pld_to_send(cc)) {
			hlen_out = 0;
		}

		/*
		 * Note: hlen_in and hlen_out can be both non-zero only if
		 * the input and output buffers are disjoint. Thus, we can
		 * offer both buffers to the handshake code.
		 */

		cc->hlen_in = hlen_in;
		cc->hlen_out = hlen_out;
		cc->action = action;
		cc->hsrun(&cc->cpu);
		if (br_ssl_engine_closed(cc)) {
			return;
		}
		if (cc->hbuf_out != cc->saved_hbuf_out) {
			sendpld_ack(cc, cc->hbuf_out - cc->saved_hbuf_out);
		}
		if (hlen_in != cc->hlen_in) {
			recvpld_ack(cc, hlen_in - cc->hlen_in);
			if (cc->hlen_in == 0) {
				/*
				 * We read all data bytes, which may have
				 * released the output buffer in case it
				 * is shared with the input buffer, and
				 * the handshake code might be waiting for
				 * that.
				 */
				action = 0;
				continue;
			}
		}
		break;
	}
}

/* see inner.h */
void
br_ssl_engine_flush_record(br_ssl_engine_context *cc)
{
	if (cc->hbuf_out != cc->saved_hbuf_out) {
		sendpld_ack(cc, cc->hbuf_out - cc->saved_hbuf_out);
	}
	if (br_ssl_engine_has_pld_to_send(cc)) {
		sendpld_flush(cc, 0);
	}
	cc->saved_hbuf_out = cc->hbuf_out = sendpld_buf(cc, &cc->hlen_out);
}

/* see bearssl_ssl.h */
unsigned char *
br_ssl_engine_sendapp_buf(const br_ssl_engine_context *cc, size_t *len)
{
	if (!(cc->application_data & 1)) {
		*len = 0;
		return NULL;
	}
	return sendpld_buf(cc, len);
}

/* see bearssl_ssl.h */
void
br_ssl_engine_sendapp_ack(br_ssl_engine_context *cc, size_t len)
{
	sendpld_ack(cc, len);
}

/* see bearssl_ssl.h */
unsigned char *
br_ssl_engine_recvapp_buf(const br_ssl_engine_context *cc, size_t *len)
{
	if (!(cc->application_data & 1)
		|| cc->record_type_in != BR_SSL_APPLICATION_DATA)
	{
		*len = 0;
		return NULL;
	}
	return recvpld_buf(cc, len);
}

/* see bearssl_ssl.h */
void
br_ssl_engine_recvapp_ack(br_ssl_engine_context *cc, size_t len)
{
	recvpld_ack(cc, len);
}

/* see bearssl_ssl.h */
unsigned char *
br_ssl_engine_sendrec_buf(const br_ssl_engine_context *cc, size_t *len)
{
	return sendrec_buf(cc, len);
}

/* see bearssl_ssl.h */
void
br_ssl_engine_sendrec_ack(br_ssl_engine_context *cc, size_t len)
{
	sendrec_ack(cc, len);
	if (len != 0 && !has_rec_tosend(cc)
		&& (cc->record_type_out != BR_SSL_APPLICATION_DATA
		|| (cc->application_data & 1) == 0))
	{
		jump_handshake(cc, 0);
	}
}

/* see bearssl_ssl.h */
unsigned char *
br_ssl_engine_recvrec_buf(const br_ssl_engine_context *cc, size_t *len)
{
	return recvrec_buf(cc, len);
}

/* see bearssl_ssl.h */
void
br_ssl_engine_recvrec_ack(br_ssl_engine_context *cc, size_t len)
{
	unsigned char *buf;

	recvrec_ack(cc, len);
	if (br_ssl_engine_closed(cc)) {
		return;
	}

	/*
	 * We just received some bytes from the peer. This may have
	 * yielded some payload bytes, in which case we must process
	 * them according to the record type.
	 */
	buf = recvpld_buf(cc, &len);
	if (buf != NULL) {
		switch (cc->record_type_in) {
		case BR_SSL_CHANGE_CIPHER_SPEC:
		case BR_SSL_ALERT:
		case BR_SSL_HANDSHAKE:
			jump_handshake(cc, 0);
			break;
		case BR_SSL_APPLICATION_DATA:
			if (cc->application_data == 1) {
				break;
			}

			/*
			 * If we are currently closing, and waiting for
			 * a close_notify from the peer, then incoming
			 * application data should be discarded.
			 */
			if (cc->application_data == 2) {
				recvpld_ack(cc, len);
				break;
			}

			/* Fall through */
		default:
			br_ssl_engine_fail(cc, BR_ERR_UNEXPECTED);
			break;
		}
	}
}

/* see bearssl_ssl.h */
void
br_ssl_engine_close(br_ssl_engine_context *cc)
{
	if (!br_ssl_engine_closed(cc)) {
		jump_handshake(cc, 1);
	}
}

/* see bearssl_ssl.h */
int
br_ssl_engine_renegotiate(br_ssl_engine_context *cc)
{
	size_t len;

	if (br_ssl_engine_closed(cc) || cc->reneg == 1
		|| (cc->flags & BR_OPT_NO_RENEGOTIATION) != 0
		|| br_ssl_engine_recvapp_buf(cc, &len) != NULL)
	{
		return 0;
	}
	jump_handshake(cc, 2);
	return 1;
}

/* see bearssl.h */
unsigned
br_ssl_engine_current_state(const br_ssl_engine_context *cc)
{
	unsigned s;
	size_t len;

	if (br_ssl_engine_closed(cc)) {
		return BR_SSL_CLOSED;
	}

	s = 0;
	if (br_ssl_engine_sendrec_buf(cc, &len) != NULL) {
		s |= BR_SSL_SENDREC;
	}
	if (br_ssl_engine_recvrec_buf(cc, &len) != NULL) {
		s |= BR_SSL_RECVREC;
	}
	if (br_ssl_engine_sendapp_buf(cc, &len) != NULL) {
		s |= BR_SSL_SENDAPP;
	}
	if (br_ssl_engine_recvapp_buf(cc, &len) != NULL) {
		s |= BR_SSL_RECVAPP;
	}
	return s;
}

/* see bearssl_ssl.h */
void
br_ssl_engine_flush(br_ssl_engine_context *cc, int force)
{
	if (!br_ssl_engine_closed(cc) && (cc->application_data & 1) != 0) {
		sendpld_flush(cc, force);
	}
}

/* see inner.h */
void
br_ssl_engine_hs_reset(br_ssl_engine_context *cc,
	void (*hsinit)(void *), void (*hsrun)(void *))
{
	engine_clearbuf(cc);
	cc->cpu.dp = cc->dp_stack;
	cc->cpu.rp = cc->rp_stack;
	hsinit(&cc->cpu);
	cc->hsrun = hsrun;
	cc->shutdown_recv = 0;
	cc->application_data = 0;
	cc->alert = 0;
	jump_handshake(cc, 0);
}

/* see inner.h */
br_tls_prf_impl
br_ssl_engine_get_PRF(br_ssl_engine_context *cc, int prf_id)
{
	if (cc->session.version >= BR_TLS12) {
		if (prf_id == br_sha384_ID) {
			return cc->prf_sha384;
		} else {
			return cc->prf_sha256;
		}
	} else {
		return cc->prf10;
	}
}

/* see inner.h */
void
br_ssl_engine_compute_master(br_ssl_engine_context *cc,
	int prf_id, const void *pms, size_t pms_len)
{
	br_tls_prf_impl iprf;
	br_tls_prf_seed_chunk seed[2] = {
		{ cc->client_random, sizeof cc->client_random },
		{ cc->server_random, sizeof cc->server_random }
	};

	iprf = br_ssl_engine_get_PRF(cc, prf_id);
	iprf(cc->session.master_secret, sizeof cc->session.master_secret,
		pms, pms_len, "master secret", 2, seed);
}

/*
 * Compute key block.
 */
static void
compute_key_block(br_ssl_engine_context *cc, int prf_id,
	size_t half_len, unsigned char *kb)
{
	br_tls_prf_impl iprf;
	br_tls_prf_seed_chunk seed[2] = {
		{ cc->server_random, sizeof cc->server_random },
		{ cc->client_random, sizeof cc->client_random }
	};

	iprf = br_ssl_engine_get_PRF(cc, prf_id);
	iprf(kb, half_len << 1,
		cc->session.master_secret, sizeof cc->session.master_secret,
		"key expansion", 2, seed);
}

/* see inner.h */
void
br_ssl_engine_switch_cbc_in(br_ssl_engine_context *cc,
	int is_client, int prf_id, int mac_id,
	const br_block_cbcdec_class *bc_impl, size_t cipher_key_len)
{
	unsigned char kb[192];
	unsigned char *cipher_key, *mac_key, *iv;
	const br_hash_class *imh;
	size_t mac_key_len, mac_out_len, iv_len;

	imh = br_ssl_engine_get_hash(cc, mac_id);
	mac_out_len = (imh->desc >> BR_HASHDESC_OUT_OFF) & BR_HASHDESC_OUT_MASK;
	mac_key_len = mac_out_len;

	/*
	 * TLS 1.1+ uses per-record explicit IV, so no IV to generate here.
	 */
	if (cc->session.version >= BR_TLS11) {
		iv_len = 0;
	} else {
		iv_len = bc_impl->block_size;
	}
	compute_key_block(cc, prf_id,
		mac_key_len + cipher_key_len + iv_len, kb);
	if (is_client) {
		mac_key = &kb[mac_key_len];
		cipher_key = &kb[(mac_key_len << 1) + cipher_key_len];
		iv = &kb[((mac_key_len + cipher_key_len) << 1) + iv_len];
	} else {
		mac_key = &kb[0];
		cipher_key = &kb[mac_key_len << 1];
		iv = &kb[(mac_key_len + cipher_key_len) << 1];
	}
	if (iv_len == 0) {
		iv = NULL;
	}
	cc->icbc_in->init(&cc->in.cbc.vtable,
		bc_impl, cipher_key, cipher_key_len,
		imh, mac_key, mac_key_len, mac_out_len, iv);
	cc->incrypt = 1;
}

/* see inner.h */
void
br_ssl_engine_switch_cbc_out(br_ssl_engine_context *cc,
	int is_client, int prf_id, int mac_id,
	const br_block_cbcenc_class *bc_impl, size_t cipher_key_len)
{
	unsigned char kb[192];
	unsigned char *cipher_key, *mac_key, *iv;
	const br_hash_class *imh;
	size_t mac_key_len, mac_out_len, iv_len;

	imh = br_ssl_engine_get_hash(cc, mac_id);
	mac_out_len = (imh->desc >> BR_HASHDESC_OUT_OFF) & BR_HASHDESC_OUT_MASK;
	mac_key_len = mac_out_len;

	/*
	 * TLS 1.1+ uses per-record explicit IV, so no IV to generate here.
	 */
	if (cc->session.version >= BR_TLS11) {
		iv_len = 0;
	} else {
		iv_len = bc_impl->block_size;
	}
	compute_key_block(cc, prf_id,
		mac_key_len + cipher_key_len + iv_len, kb);
	if (is_client) {
		mac_key = &kb[0];
		cipher_key = &kb[mac_key_len << 1];
		iv = &kb[(mac_key_len + cipher_key_len) << 1];
	} else {
		mac_key = &kb[mac_key_len];
		cipher_key = &kb[(mac_key_len << 1) + cipher_key_len];
		iv = &kb[((mac_key_len + cipher_key_len) << 1) + iv_len];
	}
	if (iv_len == 0) {
		iv = NULL;
	}
	cc->icbc_out->init(&cc->out.cbc.vtable,
		bc_impl, cipher_key, cipher_key_len,
		imh, mac_key, mac_key_len, mac_out_len, iv);
}

/* see inner.h */
void
br_ssl_engine_switch_gcm_in(br_ssl_engine_context *cc,
	int is_client, int prf_id,
	const br_block_ctr_class *bc_impl, size_t cipher_key_len)
{
	unsigned char kb[72];
	unsigned char *cipher_key, *iv;

	compute_key_block(cc, prf_id, cipher_key_len + 4, kb);
	if (is_client) {
		cipher_key = &kb[cipher_key_len];
		iv = &kb[(cipher_key_len << 1) + 4];
	} else {
		cipher_key = &kb[0];
		iv = &kb[cipher_key_len << 1];
	}
	cc->igcm_in->init(&cc->in.gcm.vtable.in,
		bc_impl, cipher_key, cipher_key_len, cc->ighash, iv);
	cc->incrypt = 1;
}

/* see inner.h */
void
br_ssl_engine_switch_gcm_out(br_ssl_engine_context *cc,
	int is_client, int prf_id,
	const br_block_ctr_class *bc_impl, size_t cipher_key_len)
{
	unsigned char kb[72];
	unsigned char *cipher_key, *iv;

	compute_key_block(cc, prf_id, cipher_key_len + 4, kb);
	if (is_client) {
		cipher_key = &kb[0];
		iv = &kb[cipher_key_len << 1];
	} else {
		cipher_key = &kb[cipher_key_len];
		iv = &kb[(cipher_key_len << 1) + 4];
	}
	cc->igcm_out->init(&cc->out.gcm.vtable.out,
		bc_impl, cipher_key, cipher_key_len, cc->ighash, iv);
}

/* see inner.h */
void
br_ssl_engine_switch_chapol_in(br_ssl_engine_context *cc,
	int is_client, int prf_id)
{
	unsigned char kb[88];
	unsigned char *cipher_key, *iv;

	compute_key_block(cc, prf_id, 44, kb);
	if (is_client) {
		cipher_key = &kb[32];
		iv = &kb[76];
	} else {
		cipher_key = &kb[0];
		iv = &kb[64];
	}
	cc->ichapol_in->init(&cc->in.chapol.vtable.in,
		cc->ichacha, cc->ipoly, cipher_key, iv);
	cc->incrypt = 1;
}

/* see inner.h */
void
br_ssl_engine_switch_chapol_out(br_ssl_engine_context *cc,
	int is_client, int prf_id)
{
	unsigned char kb[88];
	unsigned char *cipher_key, *iv;

	compute_key_block(cc, prf_id, 44, kb);
	if (is_client) {
		cipher_key = &kb[0];
		iv = &kb[64];
	} else {
		cipher_key = &kb[32];
		iv = &kb[76];
	}
	cc->ichapol_out->init(&cc->out.chapol.vtable.out,
		cc->ichacha, cc->ipoly, cipher_key, iv);
}

/* see inner.h */
void
br_ssl_engine_switch_ccm_in(br_ssl_engine_context *cc,
	int is_client, int prf_id,
	const br_block_ctrcbc_class *bc_impl,
	size_t cipher_key_len, size_t tag_len)
{
	unsigned char kb[72];
	unsigned char *cipher_key, *iv;

	compute_key_block(cc, prf_id, cipher_key_len + 4, kb);
	if (is_client) {
		cipher_key = &kb[cipher_key_len];
		iv = &kb[(cipher_key_len << 1) + 4];
	} else {
		cipher_key = &kb[0];
		iv = &kb[cipher_key_len << 1];
	}
	cc->iccm_in->init(&cc->in.ccm.vtable.in,
		bc_impl, cipher_key, cipher_key_len, iv, tag_len);
	cc->incrypt = 1;
}

/* see inner.h */
void
br_ssl_engine_switch_ccm_out(br_ssl_engine_context *cc,
	int is_client, int prf_id,
	const br_block_ctrcbc_class *bc_impl,
	size_t cipher_key_len, size_t tag_len)
{
	unsigned char kb[72];
	unsigned char *cipher_key, *iv;

	compute_key_block(cc, prf_id, cipher_key_len + 4, kb);
	if (is_client) {
		cipher_key = &kb[0];
		iv = &kb[cipher_key_len << 1];
	} else {
		cipher_key = &kb[cipher_key_len];
		iv = &kb[(cipher_key_len << 1) + 4];
	}
	cc->iccm_out->init(&cc->out.ccm.vtable.out,
		bc_impl, cipher_key, cipher_key_len, iv, tag_len);
}
