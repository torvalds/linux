/*
 * Low-level user helpers placed in the vectors page for AArch32.
 * Based on the kuser helpers in arch/arm/kernel/entry-armv.S.
 *
 * Copyright (C) 2005-2011 Nicolas Pitre <nico@fluxnic.net>
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * AArch32 user helpers.
 *
 * Each segment is 32-byte aligned and will be moved to the top of the high
 * vector page.  New segments (if ever needed) must be added in front of
 * existing ones.  This mechanism should be used only for things that are
 * really small and justified, and not be abused freely.
 *
 * See Documentation/arm/kernel_user_helpers.txt for formal definitions.
 */

#include <asm/unistd32.h>

	.align	5
	.globl	__kuser_helper_start
__kuser_helper_start:

__kuser_cmpxchg64:			// 0xffff0f60
	.inst	0xe92d00f0		//	push		{r4, r5, r6, r7}
	.inst	0xe1c040d0		//	ldrd		r4, r5, [r0]
	.inst	0xe1c160d0		//	ldrd		r6, r7, [r1]
	.inst	0xe1b20e9f		// 1:	ldaexd		r0, r1, [r2]
	.inst	0xe0303004		//	eors		r3, r0, r4
	.inst	0x00313005		//	eoreqs		r3, r1, r5
	.inst	0x01a23e96		//	stlexdeq	r3, r6, [r2]
	.inst	0x03330001		//	teqeq		r3, #1
	.inst	0x0afffff9		//	beq		1b
	.inst	0xe2730000		//	rsbs		r0, r3, #0
	.inst	0xe8bd00f0		//	pop		{r4, r5, r6, r7}
	.inst	0xe12fff1e		//	bx		lr

	.align	5
__kuser_memory_barrier:			// 0xffff0fa0
	.inst	0xf57ff05b		//	dmb		ish
	.inst	0xe12fff1e		//	bx		lr

	.align	5
__kuser_cmpxchg:			// 0xffff0fc0
	.inst	0xe1923e9f		// 1:	ldaex		r3, [r2]
	.inst	0xe0533000		//	subs		r3, r3, r0
	.inst	0x01823e91		//	stlexeq		r3, r1, [r2]
	.inst	0x03330001		//	teqeq		r3, #1
	.inst	0x0afffffa		//	beq		1b
	.inst	0xe2730000		//	rsbs		r0, r3, #0
	.inst	0xe12fff1e		//	bx		lr

	.align	5
__kuser_get_tls:			// 0xffff0fe0
	.inst	0xee1d0f70		//	mrc		p15, 0, r0, c13, c0, 3
	.inst	0xe12fff1e		//	bx		lr
	.rep	5
	.word	0
	.endr

__kuser_helper_version:			// 0xffff0ffc
	.word	((__kuser_helper_end - __kuser_helper_start) >> 5)
	.globl	__kuser_helper_end
__kuser_helper_end:

/*
 * AArch32 sigreturn code
 *
 * For ARM syscalls, the syscall number has to be loaded into r7.
 * We do not support an OABI userspace.
 *
 * For Thumb syscalls, we also pass the syscall number via r7. We therefore
 * need two 16-bit instructions.
 */
	.globl __aarch32_sigret_code_start
__aarch32_sigret_code_start:

	/*
	 * ARM Code
	 */
	.byte	__NR_compat_sigreturn, 0x70, 0xa0, 0xe3	// mov	r7, #__NR_compat_sigreturn
	.byte	__NR_compat_sigreturn, 0x00, 0x00, 0xef	// svc	#__NR_compat_sigreturn

	/*
	 * Thumb code
	 */
	.byte	__NR_compat_sigreturn, 0x27			// svc	#__NR_compat_sigreturn
	.byte	__NR_compat_sigreturn, 0xdf			// mov	r7, #__NR_compat_sigreturn

	/*
	 * ARM code
	 */
	.byte	__NR_compat_rt_sigreturn, 0x70, 0xa0, 0xe3	// mov	r7, #__NR_compat_rt_sigreturn
	.byte	__NR_compat_rt_sigreturn, 0x00, 0x00, 0xef	// svc	#__NR_compat_rt_sigreturn

	/*
	 * Thumb code
	 */
	.byte	__NR_compat_rt_sigreturn, 0x27			// svc	#__NR_compat_rt_sigreturn
	.byte	__NR_compat_rt_sigreturn, 0xdf			// mov	r7, #__NR_compat_rt_sigreturn

        .globl __aarch32_sigret_code_end
__aarch32_sigret_code_end:
