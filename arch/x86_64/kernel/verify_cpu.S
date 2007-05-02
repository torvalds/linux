/*
 *
 *	verify_cpu.S - Code for cpu long mode and SSE verification. This
 *	code has been borrowed from boot/setup.S and was introduced by
 * 	Andi Kleen.
 *
 *	Copyright (c) 2007  Andi Kleen (ak@suse.de)
 *	Copyright (c) 2007  Eric Biederman (ebiederm@xmission.com)
 *	Copyright (c) 2007  Vivek Goyal (vgoyal@in.ibm.com)
 *
 * 	This source code is licensed under the GNU General Public License,
 * 	Version 2.  See the file COPYING for more details.
 *
 *	This is a common code for verification whether CPU supports
 * 	long mode and SSE or not. It is not called directly instead this
 *	file is included at various places and compiled in that context.
 * 	Following are the current usage.
 *
 * 	This file is included by both 16bit and 32bit code.
 *
 *	arch/x86_64/boot/setup.S : Boot cpu verification (16bit)
 *	arch/x86_64/boot/compressed/head.S: Boot cpu verification (32bit)
 *	arch/x86_64/kernel/trampoline.S: secondary processor verfication (16bit)
 *	arch/x86_64/kernel/acpi/wakeup.S:Verfication at resume (16bit)
 *
 *	verify_cpu, returns the status of cpu check in register %eax.
 *		0: Success    1: Failure
 *
 * 	The caller needs to check for the error code and take the action
 * 	appropriately. Either display a message or halt.
 */

#include <asm/cpufeature.h>

verify_cpu:
	pushfl				# Save caller passed flags
	pushl	$0			# Kill any dangerous flags
	popfl

	/* minimum CPUID flags for x86-64 as defined by AMD */
#define M(x) (1<<(x))
#define M2(a,b) M(a)|M(b)
#define M4(a,b,c,d) M(a)|M(b)|M(c)|M(d)

#define SSE_MASK \
	(M2(X86_FEATURE_XMM,X86_FEATURE_XMM2))
#define REQUIRED_MASK1 \
	(M4(X86_FEATURE_FPU,X86_FEATURE_PSE,X86_FEATURE_TSC,X86_FEATURE_MSR)|\
	 M4(X86_FEATURE_PAE,X86_FEATURE_CX8,X86_FEATURE_PGE,X86_FEATURE_CMOV)|\
	 M(X86_FEATURE_FXSR))
#define REQUIRED_MASK2 \
	(M(X86_FEATURE_LM - 32))

	pushfl				# standard way to check for cpuid
	popl	%eax
	movl	%eax,%ebx
	xorl	$0x200000,%eax
	pushl	%eax
	popfl
	pushfl
	popl	%eax
	cmpl	%eax,%ebx
	jz	verify_cpu_no_longmode	# cpu has no cpuid

	movl	$0x0,%eax		# See if cpuid 1 is implemented
	cpuid
	cmpl	$0x1,%eax
	jb	verify_cpu_no_longmode	# no cpuid 1

	xor	%di,%di
	cmpl	$0x68747541,%ebx	# AuthenticAMD
	jnz	verify_cpu_noamd
	cmpl	$0x69746e65,%edx
	jnz	verify_cpu_noamd
	cmpl	$0x444d4163,%ecx
	jnz	verify_cpu_noamd
	mov	$1,%di			# cpu is from AMD

verify_cpu_noamd:
	movl    $0x1,%eax		# Does the cpu have what it takes
	cpuid
	andl	$REQUIRED_MASK1,%edx
	xorl	$REQUIRED_MASK1,%edx
	jnz	verify_cpu_no_longmode

	movl    $0x80000000,%eax	# See if extended cpuid is implemented
	cpuid
	cmpl    $0x80000001,%eax
	jb      verify_cpu_no_longmode	# no extended cpuid

	movl    $0x80000001,%eax	# Does the cpu have what it takes
	cpuid
	andl    $REQUIRED_MASK2,%edx
	xorl    $REQUIRED_MASK2,%edx
	jnz     verify_cpu_no_longmode

verify_cpu_sse_test:
	movl	$1,%eax
	cpuid
	andl	$SSE_MASK,%edx
	cmpl	$SSE_MASK,%edx
	je	verify_cpu_sse_ok
	test	%di,%di
	jz	verify_cpu_no_longmode	# only try to force SSE on AMD
	movl	$0xc0010015,%ecx	# HWCR
	rdmsr
	btr	$15,%eax		# enable SSE
	wrmsr
	xor	%di,%di			# don't loop
	jmp	verify_cpu_sse_test	# try again

verify_cpu_no_longmode:
	popfl				# Restore caller passed flags
	movl $1,%eax
	ret
verify_cpu_sse_ok:
	popfl				# Restore caller passed flags
	xorl %eax, %eax
	ret
