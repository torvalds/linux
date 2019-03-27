/*
 * Copyright 2015-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_CHACHA_H
#define HEADER_CHACHA_H

#include <stddef.h>

/*
 * ChaCha20_ctr32 encrypts |len| bytes from |inp| with the given key and
 * nonce and writes the result to |out|, which may be equal to |inp|.
 * The |key| is not 32 bytes of verbatim key material though, but the
 * said material collected into 8 32-bit elements array in host byte
 * order. Same approach applies to nonce: the |counter| argument is
 * pointer to concatenated nonce and counter values collected into 4
 * 32-bit elements. This, passing crypto material collected into 32-bit
 * elements as opposite to passing verbatim byte vectors, is chosen for
 * efficiency in multi-call scenarios.
 */
void ChaCha20_ctr32(unsigned char *out, const unsigned char *inp,
                    size_t len, const unsigned int key[8],
                    const unsigned int counter[4]);
/*
 * You can notice that there is no key setup procedure. Because it's
 * as trivial as collecting bytes into 32-bit elements, it's reckoned
 * that below macro is sufficient.
 */
#define CHACHA_U8TOU32(p)  ( \
                ((unsigned int)(p)[0])     | ((unsigned int)(p)[1]<<8) | \
                ((unsigned int)(p)[2]<<16) | ((unsigned int)(p)[3]<<24)  )

#define CHACHA_KEY_SIZE		32
#define CHACHA_CTR_SIZE		16
#define CHACHA_BLK_SIZE		64

#endif
