/*
 * Constant-time equality testing of memory regions.
 *
 * Authors:
 *
 *   James Yonan <james@openvpn.net>
 *   Daniel Borkmann <dborkman@redhat.com>
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2013 OpenVPN Technologies, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2013 OpenVPN Technologies, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of OpenVPN Technologies nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <crypto/algapi.h>

#ifndef __HAVE_ARCH_CRYPTO_MEMNEQ

/* Generic path for arbitrary size */
static inline unsigned long
__crypto_memneq_generic(const void *a, const void *b, size_t size)
{
	unsigned long neq = 0;

#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	while (size >= sizeof(unsigned long)) {
		neq |= *(unsigned long *)a ^ *(unsigned long *)b;
		OPTIMIZER_HIDE_VAR(neq);
		a += sizeof(unsigned long);
		b += sizeof(unsigned long);
		size -= sizeof(unsigned long);
	}
#endif /* CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS */
	while (size > 0) {
		neq |= *(unsigned char *)a ^ *(unsigned char *)b;
		OPTIMIZER_HIDE_VAR(neq);
		a += 1;
		b += 1;
		size -= 1;
	}
	return neq;
}

/* Loop-free fast-path for frequently used 16-byte size */
static inline unsigned long __crypto_memneq_16(const void *a, const void *b)
{
	unsigned long neq = 0;

#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (sizeof(unsigned long) == 8) {
		neq |= *(unsigned long *)(a)   ^ *(unsigned long *)(b);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned long *)(a+8) ^ *(unsigned long *)(b+8);
		OPTIMIZER_HIDE_VAR(neq);
	} else if (sizeof(unsigned int) == 4) {
		neq |= *(unsigned int *)(a)    ^ *(unsigned int *)(b);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned int *)(a+4)  ^ *(unsigned int *)(b+4);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned int *)(a+8)  ^ *(unsigned int *)(b+8);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned int *)(a+12) ^ *(unsigned int *)(b+12);
		OPTIMIZER_HIDE_VAR(neq);
	} else {
#endif /* CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS */
		neq |= *(unsigned char *)(a)    ^ *(unsigned char *)(b);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+1)  ^ *(unsigned char *)(b+1);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+2)  ^ *(unsigned char *)(b+2);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+3)  ^ *(unsigned char *)(b+3);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+4)  ^ *(unsigned char *)(b+4);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+5)  ^ *(unsigned char *)(b+5);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+6)  ^ *(unsigned char *)(b+6);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+7)  ^ *(unsigned char *)(b+7);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+8)  ^ *(unsigned char *)(b+8);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+9)  ^ *(unsigned char *)(b+9);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+10) ^ *(unsigned char *)(b+10);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+11) ^ *(unsigned char *)(b+11);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+12) ^ *(unsigned char *)(b+12);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+13) ^ *(unsigned char *)(b+13);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+14) ^ *(unsigned char *)(b+14);
		OPTIMIZER_HIDE_VAR(neq);
		neq |= *(unsigned char *)(a+15) ^ *(unsigned char *)(b+15);
		OPTIMIZER_HIDE_VAR(neq);
	}

	return neq;
}

/* Compare two areas of memory without leaking timing information,
 * and with special optimizations for common sizes.  Users should
 * not call this function directly, but should instead use
 * crypto_memneq defined in crypto/algapi.h.
 */
noinline unsigned long __crypto_memneq(const void *a, const void *b,
				       size_t size)
{
	switch (size) {
	case 16:
		return __crypto_memneq_16(a, b);
	default:
		return __crypto_memneq_generic(a, b, size);
	}
}
EXPORT_SYMBOL(__crypto_memneq);

#endif /* __HAVE_ARCH_CRYPTO_MEMNEQ */
