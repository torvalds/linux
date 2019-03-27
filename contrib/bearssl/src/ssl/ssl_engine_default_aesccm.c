/*
 * Copyright (c) 2018 Thomas Pornin <pornin@bolet.org>
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
br_ssl_engine_set_default_aes_ccm(br_ssl_engine_context *cc)
{
#if BR_AES_X86NI || BR_POWER8
	const br_block_ctrcbc_class *ictrcbc;
#endif

	br_ssl_engine_set_ccm(cc,
		&br_sslrec_in_ccm_vtable,
		&br_sslrec_out_ccm_vtable);
#if BR_AES_X86NI
	ictrcbc = br_aes_x86ni_ctrcbc_get_vtable();
	if (ictrcbc != NULL) {
		br_ssl_engine_set_aes_ctrcbc(cc, ictrcbc);
	} else {
#if BR_64
		br_ssl_engine_set_aes_ctrcbc(cc, &br_aes_ct64_ctrcbc_vtable);
#else
		br_ssl_engine_set_aes_ctrcbc(cc, &br_aes_ct_ctrcbc_vtable);
#endif
	}
#elif BR_POWER8
	ictrcbc = br_aes_pwr8_ctrcbc_get_vtable();
	if (ictrcbc != NULL) {
		br_ssl_engine_set_aes_ctrcbc(cc, ictrcbc);
	} else {
#if BR_64
		br_ssl_engine_set_aes_ctrcbc(cc, &br_aes_ct64_ctrcbc_vtable);
#else
		br_ssl_engine_set_aes_ctrcbc(cc, &br_aes_ct_ctrcbc_vtable);
#endif
	}
#else
#if BR_64
	br_ssl_engine_set_aes_ctrcbc(cc, &br_aes_ct64_ctrcbc_vtable);
#else
	br_ssl_engine_set_aes_ctrcbc(cc, &br_aes_ct_ctrcbc_vtable);
#endif
#endif
}
