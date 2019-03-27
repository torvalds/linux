/*
 * edns-subnet/edns-subnet.c - Subnet option related constants 
 *
 * Copyright (c) 2013, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * \file
 * Subnet option related constants. 
 */

#include "config.h"

#ifdef CLIENT_SUBNET /* keeps splint happy */
#include "edns-subnet/edns-subnet.h"
#include <string.h>

int
copy_clear(uint8_t* dst, size_t dstlen, uint8_t* src, size_t srclen, size_t n)
{
	size_t intpart = n / 8;  /* bytes */
	size_t fracpart = n % 8; /* bits */
	size_t written = intpart;
	if (intpart > dstlen || intpart > srclen)
		return 1;
	if (fracpart && (intpart+1 > dstlen || intpart+1 > srclen))
		return 1;
	memcpy(dst, src, intpart);
	if (fracpart) {
		dst[intpart] = src[intpart] & ~(0xFF >> fracpart);
		written++;
	}
	memset(dst + written, 0, dstlen - written);
	return 0;
}

#endif /* CLIENT_SUBNET */
