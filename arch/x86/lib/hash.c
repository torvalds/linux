/*
 * Some portions derived from code covered by the following notice:
 *
 * Copyright (c) 2010-2013 Intel Corporation. All rights reserved.
 * All rights reserved.
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
 *   * Neither the name of Intel Corporation nor the names of its
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

#include <linux/hash.h>
#include <linux/init.h>

#include <asm/processor.h>
#include <asm/cpufeature.h>
#include <asm/hash.h>

static inline u32 crc32_u32(u32 crc, u32 val)
{
#ifdef CONFIG_AS_CRC32
	asm ("crc32l %1,%0\n" : "+r" (crc) : "rm" (val));
#else
	asm (".byte 0xf2, 0x0f, 0x38, 0xf1, 0xc1" : "+a" (crc) : "c" (val));
#endif
	return crc;
}

static u32 intel_crc4_2_hash(const void *data, u32 len, u32 seed)
{
	const u32 *p32 = (const u32 *) data;
	u32 i, tmp = 0;

	for (i = 0; i < len / 4; i++)
		seed = crc32_u32(seed, *p32++);

	switch (len & 3) {
	case 3:
		tmp |= *((const u8 *) p32 + 2) << 16;
		/* fallthrough */
	case 2:
		tmp |= *((const u8 *) p32 + 1) << 8;
		/* fallthrough */
	case 1:
		tmp |= *((const u8 *) p32);
		seed = crc32_u32(seed, tmp);
		break;
	}

	return seed;
}

static u32 intel_crc4_2_hash2(const u32 *data, u32 len, u32 seed)
{
	const u32 *p32 = (const u32 *) data;
	u32 i;

	for (i = 0; i < len; i++)
		seed = crc32_u32(seed, *p32++);

	return seed;
}

void __init setup_arch_fast_hash(struct fast_hash_ops *ops)
{
	if (cpu_has_xmm4_2) {
		ops->hash  = intel_crc4_2_hash;
		ops->hash2 = intel_crc4_2_hash2;
	}
}
