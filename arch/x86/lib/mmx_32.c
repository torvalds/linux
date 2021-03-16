// SPDX-License-Identifier: GPL-2.0
/*
 *	MMX 3DNow! library helper functions
 *
 *	To do:
 *	We can use MMX just for prefetch in IRQ's. This may be a win.
 *		(reported so on K6-III)
 *	We should use a better code neutral filler for the short jump
 *		leal ebx. [ebx] is apparently best for K6-2, but Cyrix ??
 *	We also want to clobber the filler register so we don't get any
 *		register forwarding stalls on the filler.
 *
 *	Add *user handling. Checksums are not a win with MMX on any CPU
 *	tested so far for any MMX solution figured.
 *
 *	22/09/2000 - Arjan van de Ven
 *		Improved for non-egineering-sample Athlons
 *
 */
#include <linux/hardirq.h>
#include <linux/string.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/types.h>

#include <asm/fpu/api.h>
#include <asm/asm.h>

/*
 * Use KFPU_387.  MMX instructions are not affected by MXCSR,
 * but both AMD and Intel documentation states that even integer MMX
 * operations will result in #MF if an exception is pending in FCW.
 *
 * EMMS is not needed afterwards because, after calling kernel_fpu_end(),
 * any subsequent user of the 387 stack will reinitialize it using
 * KFPU_387.
 */

void *_mmx_memcpy(void *to, const void *from, size_t len)
{
	void *p;
	int i;

	if (unlikely(in_interrupt()))
		return __memcpy(to, from, len);

	p = to;
	i = len >> 6; /* len/64 */

	kernel_fpu_begin_mask(KFPU_387);

	__asm__ __volatile__ (
		"1: prefetch (%0)\n"		/* This set is 28 bytes */
		"   prefetch 64(%0)\n"
		"   prefetch 128(%0)\n"
		"   prefetch 192(%0)\n"
		"   prefetch 256(%0)\n"
		"2:  \n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x1AEB, 1b\n"	/* jmp on 26 bytes */
		"   jmp 2b\n"
		".previous\n"
			_ASM_EXTABLE(1b, 3b)
			: : "r" (from));

	for ( ; i > 5; i--) {
		__asm__ __volatile__ (
		"1:  prefetch 320(%0)\n"
		"2:  movq (%0), %%mm0\n"
		"  movq 8(%0), %%mm1\n"
		"  movq 16(%0), %%mm2\n"
		"  movq 24(%0), %%mm3\n"
		"  movq %%mm0, (%1)\n"
		"  movq %%mm1, 8(%1)\n"
		"  movq %%mm2, 16(%1)\n"
		"  movq %%mm3, 24(%1)\n"
		"  movq 32(%0), %%mm0\n"
		"  movq 40(%0), %%mm1\n"
		"  movq 48(%0), %%mm2\n"
		"  movq 56(%0), %%mm3\n"
		"  movq %%mm0, 32(%1)\n"
		"  movq %%mm1, 40(%1)\n"
		"  movq %%mm2, 48(%1)\n"
		"  movq %%mm3, 56(%1)\n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x05EB, 1b\n"	/* jmp on 5 bytes */
		"   jmp 2b\n"
		".previous\n"
			_ASM_EXTABLE(1b, 3b)
			: : "r" (from), "r" (to) : "memory");

		from += 64;
		to += 64;
	}

	for ( ; i > 0; i--) {
		__asm__ __volatile__ (
		"  movq (%0), %%mm0\n"
		"  movq 8(%0), %%mm1\n"
		"  movq 16(%0), %%mm2\n"
		"  movq 24(%0), %%mm3\n"
		"  movq %%mm0, (%1)\n"
		"  movq %%mm1, 8(%1)\n"
		"  movq %%mm2, 16(%1)\n"
		"  movq %%mm3, 24(%1)\n"
		"  movq 32(%0), %%mm0\n"
		"  movq 40(%0), %%mm1\n"
		"  movq 48(%0), %%mm2\n"
		"  movq 56(%0), %%mm3\n"
		"  movq %%mm0, 32(%1)\n"
		"  movq %%mm1, 40(%1)\n"
		"  movq %%mm2, 48(%1)\n"
		"  movq %%mm3, 56(%1)\n"
			: : "r" (from), "r" (to) : "memory");

		from += 64;
		to += 64;
	}
	/*
	 * Now do the tail of the block:
	 */
	__memcpy(to, from, len & 63);
	kernel_fpu_end();

	return p;
}
EXPORT_SYMBOL(_mmx_memcpy);

#ifdef CONFIG_MK7

/*
 *	The K7 has streaming cache bypass load/store. The Cyrix III, K6 and
 *	other MMX using processors do not.
 */

static void fast_clear_page(void *page)
{
	int i;

	kernel_fpu_begin_mask(KFPU_387);

	__asm__ __volatile__ (
		"  pxor %%mm0, %%mm0\n" : :
	);

	for (i = 0; i < 4096/64; i++) {
		__asm__ __volatile__ (
		"  movntq %%mm0, (%0)\n"
		"  movntq %%mm0, 8(%0)\n"
		"  movntq %%mm0, 16(%0)\n"
		"  movntq %%mm0, 24(%0)\n"
		"  movntq %%mm0, 32(%0)\n"
		"  movntq %%mm0, 40(%0)\n"
		"  movntq %%mm0, 48(%0)\n"
		"  movntq %%mm0, 56(%0)\n"
		: : "r" (page) : "memory");
		page += 64;
	}

	/*
	 * Since movntq is weakly-ordered, a "sfence" is needed to become
	 * ordered again:
	 */
	__asm__ __volatile__("sfence\n"::);

	kernel_fpu_end();
}

static void fast_copy_page(void *to, void *from)
{
	int i;

	kernel_fpu_begin_mask(KFPU_387);

	/*
	 * maybe the prefetch stuff can go before the expensive fnsave...
	 * but that is for later. -AV
	 */
	__asm__ __volatile__(
		"1: prefetch (%0)\n"
		"   prefetch 64(%0)\n"
		"   prefetch 128(%0)\n"
		"   prefetch 192(%0)\n"
		"   prefetch 256(%0)\n"
		"2:  \n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x1AEB, 1b\n"	/* jmp on 26 bytes */
		"   jmp 2b\n"
		".previous\n"
			_ASM_EXTABLE(1b, 3b) : : "r" (from));

	for (i = 0; i < (4096-320)/64; i++) {
		__asm__ __volatile__ (
		"1: prefetch 320(%0)\n"
		"2: movq (%0), %%mm0\n"
		"   movntq %%mm0, (%1)\n"
		"   movq 8(%0), %%mm1\n"
		"   movntq %%mm1, 8(%1)\n"
		"   movq 16(%0), %%mm2\n"
		"   movntq %%mm2, 16(%1)\n"
		"   movq 24(%0), %%mm3\n"
		"   movntq %%mm3, 24(%1)\n"
		"   movq 32(%0), %%mm4\n"
		"   movntq %%mm4, 32(%1)\n"
		"   movq 40(%0), %%mm5\n"
		"   movntq %%mm5, 40(%1)\n"
		"   movq 48(%0), %%mm6\n"
		"   movntq %%mm6, 48(%1)\n"
		"   movq 56(%0), %%mm7\n"
		"   movntq %%mm7, 56(%1)\n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x05EB, 1b\n"	/* jmp on 5 bytes */
		"   jmp 2b\n"
		".previous\n"
		_ASM_EXTABLE(1b, 3b) : : "r" (from), "r" (to) : "memory");

		from += 64;
		to += 64;
	}

	for (i = (4096-320)/64; i < 4096/64; i++) {
		__asm__ __volatile__ (
		"2: movq (%0), %%mm0\n"
		"   movntq %%mm0, (%1)\n"
		"   movq 8(%0), %%mm1\n"
		"   movntq %%mm1, 8(%1)\n"
		"   movq 16(%0), %%mm2\n"
		"   movntq %%mm2, 16(%1)\n"
		"   movq 24(%0), %%mm3\n"
		"   movntq %%mm3, 24(%1)\n"
		"   movq 32(%0), %%mm4\n"
		"   movntq %%mm4, 32(%1)\n"
		"   movq 40(%0), %%mm5\n"
		"   movntq %%mm5, 40(%1)\n"
		"   movq 48(%0), %%mm6\n"
		"   movntq %%mm6, 48(%1)\n"
		"   movq 56(%0), %%mm7\n"
		"   movntq %%mm7, 56(%1)\n"
			: : "r" (from), "r" (to) : "memory");
		from += 64;
		to += 64;
	}
	/*
	 * Since movntq is weakly-ordered, a "sfence" is needed to become
	 * ordered again:
	 */
	__asm__ __volatile__("sfence \n"::);
	kernel_fpu_end();
}

#else /* CONFIG_MK7 */

/*
 *	Generic MMX implementation without K7 specific streaming
 */
static void fast_clear_page(void *page)
{
	int i;

	kernel_fpu_begin_mask(KFPU_387);

	__asm__ __volatile__ (
		"  pxor %%mm0, %%mm0\n" : :
	);

	for (i = 0; i < 4096/128; i++) {
		__asm__ __volatile__ (
		"  movq %%mm0, (%0)\n"
		"  movq %%mm0, 8(%0)\n"
		"  movq %%mm0, 16(%0)\n"
		"  movq %%mm0, 24(%0)\n"
		"  movq %%mm0, 32(%0)\n"
		"  movq %%mm0, 40(%0)\n"
		"  movq %%mm0, 48(%0)\n"
		"  movq %%mm0, 56(%0)\n"
		"  movq %%mm0, 64(%0)\n"
		"  movq %%mm0, 72(%0)\n"
		"  movq %%mm0, 80(%0)\n"
		"  movq %%mm0, 88(%0)\n"
		"  movq %%mm0, 96(%0)\n"
		"  movq %%mm0, 104(%0)\n"
		"  movq %%mm0, 112(%0)\n"
		"  movq %%mm0, 120(%0)\n"
			: : "r" (page) : "memory");
		page += 128;
	}

	kernel_fpu_end();
}

static void fast_copy_page(void *to, void *from)
{
	int i;

	kernel_fpu_begin_mask(KFPU_387);

	__asm__ __volatile__ (
		"1: prefetch (%0)\n"
		"   prefetch 64(%0)\n"
		"   prefetch 128(%0)\n"
		"   prefetch 192(%0)\n"
		"   prefetch 256(%0)\n"
		"2:  \n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x1AEB, 1b\n"	/* jmp on 26 bytes */
		"   jmp 2b\n"
		".previous\n"
			_ASM_EXTABLE(1b, 3b) : : "r" (from));

	for (i = 0; i < 4096/64; i++) {
		__asm__ __volatile__ (
		"1: prefetch 320(%0)\n"
		"2: movq (%0), %%mm0\n"
		"   movq 8(%0), %%mm1\n"
		"   movq 16(%0), %%mm2\n"
		"   movq 24(%0), %%mm3\n"
		"   movq %%mm0, (%1)\n"
		"   movq %%mm1, 8(%1)\n"
		"   movq %%mm2, 16(%1)\n"
		"   movq %%mm3, 24(%1)\n"
		"   movq 32(%0), %%mm0\n"
		"   movq 40(%0), %%mm1\n"
		"   movq 48(%0), %%mm2\n"
		"   movq 56(%0), %%mm3\n"
		"   movq %%mm0, 32(%1)\n"
		"   movq %%mm1, 40(%1)\n"
		"   movq %%mm2, 48(%1)\n"
		"   movq %%mm3, 56(%1)\n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x05EB, 1b\n"	/* jmp on 5 bytes */
		"   jmp 2b\n"
		".previous\n"
			_ASM_EXTABLE(1b, 3b)
			: : "r" (from), "r" (to) : "memory");

		from += 64;
		to += 64;
	}
	kernel_fpu_end();
}

#endif /* !CONFIG_MK7 */

/*
 * Favour MMX for page clear and copy:
 */
static void slow_zero_page(void *page)
{
	int d0, d1;

	__asm__ __volatile__(
		"cld\n\t"
		"rep ; stosl"

			: "=&c" (d0), "=&D" (d1)
			:"a" (0), "1" (page), "0" (1024)
			:"memory");
}

void mmx_clear_page(void *page)
{
	if (unlikely(in_interrupt()))
		slow_zero_page(page);
	else
		fast_clear_page(page);
}
EXPORT_SYMBOL(mmx_clear_page);

static void slow_copy_page(void *to, void *from)
{
	int d0, d1, d2;

	__asm__ __volatile__(
		"cld\n\t"
		"rep ; movsl"
		: "=&c" (d0), "=&D" (d1), "=&S" (d2)
		: "0" (1024), "1" ((long) to), "2" ((long) from)
		: "memory");
}

void mmx_copy_page(void *to, void *from)
{
	if (unlikely(in_interrupt()))
		slow_copy_page(to, from);
	else
		fast_copy_page(to, from);
}
EXPORT_SYMBOL(mmx_copy_page);
