// SPDX-License-Identifier: GPL-2.0
/*
 * This Synopsys software and associated documentation (hereinafter the
 * "Software") is an unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you. The
 * Software IS NOT an item of Licensed Software or a Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Products
 * with Synopsys or any supplement thereto. Synopsys is a registered trademark
 * of Synopsys, Inc. Other names included in the SOFTWARE may be the
 * trademarks of their respective owners.
 *
 * The contents of this file are dual-licensed; you may select either version
 * 2 of the GNU General Public License ("GPL") or the BSD-3-Clause license
 * ("BSD-3-Clause"). The GPL is included in the COPYING file accompanying the
 * SOFTWARE. The BSD License is copied below.
 *
 * BSD-3-Clause License:
 * Copyright (c) 2011-2017 Synopsys, Inc. and/or its affiliates.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer, without
 *    modification.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. The names of the above-listed copyright holders may not be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "elppdu.h"

void pdu_to_dev32(void *addr_, u32 *src, unsigned long nword)
{
	unsigned char *addr = addr_;

	while (nword--) {
		pdu_io_write32(addr, *src++);
		addr += 4;
	}
}
EXPORT_SYMBOL(pdu_to_dev32);

void pdu_from_dev32(u32 *dst, void *addr_, unsigned long nword)
{
	unsigned char *addr = addr_;

	while (nword--) {
		*dst++ = pdu_io_read32(addr);
		addr += 4;
	}
}
EXPORT_SYMBOL(pdu_from_dev32);

void pdu_to_dev32_big(void *addr_, const unsigned char *src,
		      unsigned long nword)
{
	unsigned char *addr = addr_;
	unsigned long v;

	while (nword--) {
		v = 0;
		v = (v << 8) | ((unsigned long)*src++);
		v = (v << 8) | ((unsigned long)*src++);
		v = (v << 8) | ((unsigned long)*src++);
		v = (v << 8) | ((unsigned long)*src++);
		pdu_io_write32(addr, v);
		addr += 4;
	}
}
EXPORT_SYMBOL(pdu_to_dev32_big);

void pdu_from_dev32_big(unsigned char *dst, void *addr_, unsigned long nword)
{
	unsigned char *addr = addr_;
	unsigned long v;

	while (nword--) {
		v = pdu_io_read32(addr);
		addr += 4;
		*dst++ = (v >> 24) & 0xFF;
		v <<= 8;
		*dst++ = (v >> 24) & 0xFF;
		v <<= 8;
		*dst++ = (v >> 24) & 0xFF;
		v <<= 8;
		*dst++ = (v >> 24) & 0xFF;
		v <<= 8;
	}
}
EXPORT_SYMBOL(pdu_from_dev32_big);

void pdu_to_dev32_little(void *addr_, const unsigned char *src,
			 unsigned long nword)
{
	unsigned char *addr = addr_;
	unsigned long v;

	while (nword--) {
		v = 0;
		v = (v >> 8) | ((unsigned long)*src++ << 24UL);
		v = (v >> 8) | ((unsigned long)*src++ << 24UL);
		v = (v >> 8) | ((unsigned long)*src++ << 24UL);
		v = (v >> 8) | ((unsigned long)*src++ << 24UL);
		pdu_io_write32(addr, v);
		addr += 4;
	}
}
EXPORT_SYMBOL(pdu_to_dev32_little);

void pdu_from_dev32_little(unsigned char *dst, void *addr_, unsigned long nword)
{
	unsigned char *addr = addr_;
	unsigned long v;

	while (nword--) {
		v = pdu_io_read32(addr);
		addr += 4;
		*dst++ = v & 0xFF;
		v >>= 8;
		*dst++ = v & 0xFF;
		v >>= 8;
		*dst++ = v & 0xFF;
		v >>= 8;
		*dst++ = v & 0xFF;
		v >>= 8;
	}
}
EXPORT_SYMBOL(pdu_from_dev32_little);

void pdu_to_dev32_s(void *addr, const unsigned char *src, unsigned long nword,
		    int endian)
{
	if (endian)
		pdu_to_dev32_big(addr, src, nword);
	else
		pdu_to_dev32_little(addr, src, nword);
}
EXPORT_SYMBOL(pdu_to_dev32_s);

void pdu_from_dev32_s(unsigned char *dst, void *addr, unsigned long nword,
		      int endian)
{
	if (endian)
		pdu_from_dev32_big(dst, addr, nword);
	else
		pdu_from_dev32_little(dst, addr, nword);
}
EXPORT_SYMBOL(pdu_from_dev32_s);
