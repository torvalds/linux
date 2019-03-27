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

/* see bearssl_ssl.h */
void
br_sslio_init(br_sslio_context *ctx,
	br_ssl_engine_context *engine,
	int (*low_read)(void *read_context,
		unsigned char *data, size_t len),
	void *read_context,
	int (*low_write)(void *write_context,
		const unsigned char *data, size_t len),
	void *write_context)
{
	ctx->engine = engine;
	ctx->low_read = low_read;
	ctx->read_context = read_context;
	ctx->low_write = low_write;
	ctx->write_context = write_context;
}

/*
 * Run the engine, until the specified target state is achieved, or
 * an error occurs. The target state is SENDAPP, RECVAPP, or the
 * combination of both (the combination matches either). When a match is
 * achieved, this function returns 0. On error, it returns -1.
 */
static int
run_until(br_sslio_context *ctx, unsigned target)
{
	for (;;) {
		unsigned state;

		state = br_ssl_engine_current_state(ctx->engine);
		if (state & BR_SSL_CLOSED) {
			return -1;
		}

		/*
		 * If there is some record data to send, do it. This takes
		 * precedence over everything else.
		 */
		if (state & BR_SSL_SENDREC) {
			unsigned char *buf;
			size_t len;
			int wlen;

			buf = br_ssl_engine_sendrec_buf(ctx->engine, &len);
			wlen = ctx->low_write(ctx->write_context, buf, len);
			if (wlen < 0) {
				/*
				 * If we received a close_notify and we
				 * still send something, then we have our
				 * own response close_notify to send, and
				 * the peer is allowed by RFC 5246 not to
				 * wait for it.
				 */
				if (!ctx->engine->shutdown_recv) {
					br_ssl_engine_fail(
						ctx->engine, BR_ERR_IO);
				}
				return -1;
			}
			if (wlen > 0) {
				br_ssl_engine_sendrec_ack(ctx->engine, wlen);
			}
			continue;
		}

		/*
		 * If we reached our target, then we are finished.
		 */
		if (state & target) {
			return 0;
		}

		/*
		 * If some application data must be read, and we did not
		 * exit, then this means that we are trying to write data,
		 * and that's not possible until the application data is
		 * read. This may happen if using a shared in/out buffer,
		 * and the underlying protocol is not strictly half-duplex.
		 * This is unrecoverable here, so we report an error.
		 */
		if (state & BR_SSL_RECVAPP) {
			return -1;
		}

		/*
		 * If we reached that point, then either we are trying
		 * to read data and there is some, or the engine is stuck
		 * until a new record is obtained.
		 */
		if (state & BR_SSL_RECVREC) {
			unsigned char *buf;
			size_t len;
			int rlen;

			buf = br_ssl_engine_recvrec_buf(ctx->engine, &len);
			rlen = ctx->low_read(ctx->read_context, buf, len);
			if (rlen < 0) {
				br_ssl_engine_fail(ctx->engine, BR_ERR_IO);
				return -1;
			}
			if (rlen > 0) {
				br_ssl_engine_recvrec_ack(ctx->engine, rlen);
			}
			continue;
		}

		/*
		 * We can reach that point if the target RECVAPP, and
		 * the state contains SENDAPP only. This may happen with
		 * a shared in/out buffer. In that case, we must flush
		 * the buffered data to "make room" for a new incoming
		 * record.
		 */
		br_ssl_engine_flush(ctx->engine, 0);
	}
}

/* see bearssl_ssl.h */
int
br_sslio_read(br_sslio_context *ctx, void *dst, size_t len)
{
	unsigned char *buf;
	size_t alen;

	if (len == 0) {
		return 0;
	}
	if (run_until(ctx, BR_SSL_RECVAPP) < 0) {
		return -1;
	}
	buf = br_ssl_engine_recvapp_buf(ctx->engine, &alen);
	if (alen > len) {
		alen = len;
	}
	memcpy(dst, buf, alen);
	br_ssl_engine_recvapp_ack(ctx->engine, alen);
	return (int)alen;
}

/* see bearssl_ssl.h */
int
br_sslio_read_all(br_sslio_context *ctx, void *dst, size_t len)
{
	unsigned char *buf;

	buf = dst;
	while (len > 0) {
		int rlen;

		rlen = br_sslio_read(ctx, buf, len);
		if (rlen < 0) {
			return -1;
		}
		buf += rlen;
		len -= (size_t)rlen;
	}
	return 0;
}

/* see bearssl_ssl.h */
int
br_sslio_write(br_sslio_context *ctx, const void *src, size_t len)
{
	unsigned char *buf;
	size_t alen;

	if (len == 0) {
		return 0;
	}
	if (run_until(ctx, BR_SSL_SENDAPP) < 0) {
		return -1;
	}
	buf = br_ssl_engine_sendapp_buf(ctx->engine, &alen);
	if (alen > len) {
		alen = len;
	}
	memcpy(buf, src, alen);
	br_ssl_engine_sendapp_ack(ctx->engine, alen);
	return (int)alen;
}

/* see bearssl_ssl.h */
int
br_sslio_write_all(br_sslio_context *ctx, const void *src, size_t len)
{
	const unsigned char *buf;

	buf = src;
	while (len > 0) {
		int wlen;

		wlen = br_sslio_write(ctx, buf, len);
		if (wlen < 0) {
			return -1;
		}
		buf += wlen;
		len -= (size_t)wlen;
	}
	return 0;
}

/* see bearssl_ssl.h */
int
br_sslio_flush(br_sslio_context *ctx)
{
	/*
	 * We trigger a flush. We know the data is gone when there is
	 * no longer any record data to send, and we can either read
	 * or write application data. The call to run_until() does the
	 * job because it ensures that any assembled record data is
	 * first sent down the wire before considering anything else.
	 */
	br_ssl_engine_flush(ctx->engine, 0);
	return run_until(ctx, BR_SSL_SENDAPP | BR_SSL_RECVAPP);
}

/* see bearssl_ssl.h */
int
br_sslio_close(br_sslio_context *ctx)
{
	br_ssl_engine_close(ctx->engine);
	while (br_ssl_engine_current_state(ctx->engine) != BR_SSL_CLOSED) {
		/*
		 * Discard any incoming application data.
		 */
		size_t len;

		run_until(ctx, BR_SSL_RECVAPP);
		if (br_ssl_engine_recvapp_buf(ctx->engine, &len) != NULL) {
			br_ssl_engine_recvapp_ack(ctx->engine, len);
		}
	}
	return br_ssl_engine_last_error(ctx->engine) == BR_ERR_OK;
}
