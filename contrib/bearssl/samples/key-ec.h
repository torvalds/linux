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

#include "bearssl.h"

/*
 * The private key for the server certificate (EC).
 */

static const unsigned char EC_X[] = {
	0x03, 0x91, 0x5B, 0x42, 0x06, 0x90, 0x73, 0x91, 0x1B, 0x48, 0xEF, 0x08,
	0xFB, 0xB5, 0xAD, 0x75, 0x65, 0xF9, 0xE6, 0xF7, 0x21, 0x47, 0x62, 0x48,
	0xFA, 0x3F, 0x97, 0x7B, 0x70, 0x9D, 0x86, 0xA5
};

static const br_ec_private_key EC = {
	23,
	(unsigned char *)EC_X, sizeof EC_X
};
