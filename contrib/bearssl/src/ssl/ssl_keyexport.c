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

#include "inner.h"

/*
 * Supported cipher suites that use SHA-384 for the PRF when selected
 * for TLS 1.2. All other cipher suites are deemed to use SHA-256.
 */
static const uint16_t suites_sha384[] = {
	BR_TLS_RSA_WITH_AES_256_GCM_SHA384,
	BR_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384,
	BR_TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA384,
	BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384,
	BR_TLS_ECDH_RSA_WITH_AES_256_CBC_SHA384,
	BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
	BR_TLS_ECDH_ECDSA_WITH_AES_256_GCM_SHA384,
	BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
	BR_TLS_ECDH_RSA_WITH_AES_256_GCM_SHA384
};

/* see bearssl_ssl.h */
int
br_ssl_key_export(br_ssl_engine_context *cc,
	void *dst, size_t len, const char *label,
	const void *context, size_t context_len)
{
	br_tls_prf_seed_chunk chunks[4];
	br_tls_prf_impl iprf;
	size_t num_chunks, u;
	unsigned char tmp[2];
	int prf_id;

	if (cc->application_data != 1) {
		return 0;
	}
	chunks[0].data = cc->client_random;
	chunks[0].len = sizeof cc->client_random;
	chunks[1].data = cc->server_random;
	chunks[1].len = sizeof cc->server_random;
	if (context != NULL) {
		br_enc16be(tmp, (unsigned)context_len);
		chunks[2].data = tmp;
		chunks[2].len = 2;
		chunks[3].data = context;
		chunks[3].len = context_len;
		num_chunks = 4;
	} else {
		num_chunks = 2;
	}
	prf_id = BR_SSLPRF_SHA256;
	for (u = 0; u < (sizeof suites_sha384) / sizeof(uint16_t); u ++) {
		if (suites_sha384[u] == cc->session.cipher_suite) {
			prf_id = BR_SSLPRF_SHA384;
		}
	}
	iprf = br_ssl_engine_get_PRF(cc, prf_id);
	iprf(dst, len,
		cc->session.master_secret, sizeof cc->session.master_secret,
		label, num_chunks, chunks);
	return 1;
}
