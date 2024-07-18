/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_S390_ARCH_HWEIGHT_H
#define _ASM_S390_ARCH_HWEIGHT_H

#include <linux/types.h>

static __always_inline unsigned long popcnt_z196(unsigned long w)
{
	unsigned long cnt;

	asm volatile(".insn	rrf,0xb9e10000,%[cnt],%[w],0,0"
		     : [cnt] "=d" (cnt)
		     : [w] "d" (w)
		     : "cc");
	return cnt;
}

static __always_inline unsigned long popcnt_z15(unsigned long w)
{
	unsigned long cnt;

	asm volatile(".insn	rrf,0xb9e10000,%[cnt],%[w],8,0"
		     : [cnt] "=d" (cnt)
		     : [w] "d" (w)
		     : "cc");
	return cnt;
}

static __always_inline unsigned long __arch_hweight64(__u64 w)
{
	if (IS_ENABLED(CONFIG_HAVE_MARCH_Z15_FEATURES))
		return popcnt_z15(w);
	if (IS_ENABLED(CONFIG_HAVE_MARCH_Z196_FEATURES)) {
		w = popcnt_z196(w);
		w += w >> 32;
		w += w >> 16;
		w += w >> 8;
		return w & 0xff;
	}
	return __sw_hweight64(w);
}

static __always_inline unsigned int __arch_hweight32(unsigned int w)
{
	if (IS_ENABLED(CONFIG_HAVE_MARCH_Z15_FEATURES))
		return popcnt_z15(w);
	if (IS_ENABLED(CONFIG_HAVE_MARCH_Z196_FEATURES)) {
		w = popcnt_z196(w);
		w += w >> 16;
		w += w >> 8;
		return w & 0xff;
	}
	return __sw_hweight32(w);
}

static __always_inline unsigned int __arch_hweight16(unsigned int w)
{
	if (IS_ENABLED(CONFIG_HAVE_MARCH_Z15_FEATURES))
		return popcnt_z15((unsigned short)w);
	if (IS_ENABLED(CONFIG_HAVE_MARCH_Z196_FEATURES)) {
		w = popcnt_z196(w);
		w += w >> 8;
		return w & 0xff;
	}
	return __sw_hweight16(w);
}

static __always_inline unsigned int __arch_hweight8(unsigned int w)
{
	if (IS_ENABLED(CONFIG_HAVE_MARCH_Z196_FEATURES))
		return popcnt_z196((unsigned char)w);
	return __sw_hweight8(w);
}

#endif /* _ASM_S390_ARCH_HWEIGHT_H */
