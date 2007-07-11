/* Check if CPU has some minimum CPUID bits
   This runs in 16bit mode so that the caller can still use the BIOS
   to output errors on the screen */
#include <asm/cpufeature.h>
#include <asm/msr.h>

verify_cpu:
	pushfl				# Save caller passed flags
	pushl	$0			# Kill any dangerous flags
	popfl

#if CONFIG_X86_MINIMUM_CPU_MODEL >= 4
	pushfl
	pop	%eax
	orl	$(1<<18),%eax		# try setting AC
	push	%eax
	popfl
	pushfl
	popl    %eax
	testl	$(1<<18),%eax
	jz	bad
#endif
#if REQUIRED_MASK0 != 0
	pushfl				# standard way to check for cpuid
	popl	%eax
	movl	%eax,%ebx
	xorl	$0x200000,%eax
	pushl	%eax
	popfl
	pushfl
	popl	%eax
	cmpl	%eax,%ebx
	pushfl				# standard way to check for cpuid
	popl	%eax
	movl	%eax,%ebx
	xorl	$0x200000,%eax
	pushl	%eax
	popfl
	pushfl
	popl	%eax
	cmpl	%eax,%ebx
	jz	bad			# REQUIRED_MASK0 != 0 requires CPUID

	movl	$0x0,%eax		# See if cpuid 1 is implemented
	cpuid
	cmpl	$0x1,%eax
	jb	bad			# no cpuid 1

#if REQUIRED_MASK0 & NEED_CMPXCHG64
	/* Some VIA C3s need magic MSRs to enable CX64. Do this here */
	cmpl	$0x746e6543,%ebx	# Cent
	jne	1f
	cmpl 	$0x48727561,%edx	# aurH
	jne	1f
	cmpl	$0x736c7561,%ecx	# auls
	jne	1f
	movl	$1,%eax			# check model
	cpuid
	movl	%eax,%ebx
	shr	$8,%ebx
	andl	$0xf,%ebx
	cmp	$6,%ebx			# check family == 6
	jne	1f
	shr	$4,%eax
	andl	$0xf,%eax
	cmpl	$6,%eax			# check model >= 6
	jb	1f
	# assume models >= 6 all support this MSR
	movl	$MSR_VIA_FCR,%ecx
	rdmsr
	orl	$((1<<1)|(1<<7)),%eax	# enable CMPXCHG64 and PGE
	wrmsr
1:
#endif
	movl    $0x1,%eax		# Does the cpu have what it takes
	cpuid

#if CONFIG_X86_MINIMUM_CPU_MODEL > 4
#error	add proper model checking here
#endif

	andl	$REQUIRED_MASK0,%edx
	xorl	$REQUIRED_MASK0,%edx
	jnz	bad
#endif /* REQUIRED_MASK0 */

	popfl
	xor	%eax,%eax
	ret

bad:
	popfl
	movl	$1,%eax
	ret
