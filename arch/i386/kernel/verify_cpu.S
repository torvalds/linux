/* Check if CPU has some minimum CPUID bits
   This runs in 16bit mode so that the caller can still use the BIOS
   to output errors on the screen */
#include <asm/cpufeature.h>

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
#if REQUIRED_MASK1 != 0
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
	jz	bad			# REQUIRED_MASK1 != 0 requires CPUID

	movl	$0x0,%eax		# See if cpuid 1 is implemented
	cpuid
	cmpl	$0x1,%eax
	jb	bad			# no cpuid 1

	movl    $0x1,%eax		# Does the cpu have what it takes
	cpuid

#if CONFIG_X86_MINIMUM_CPU_MODEL > 4
#error	add proper model checking here
#endif

	andl	$REQUIRED_MASK1,%edx
	xorl	$REQUIRED_MASK1,%edx
	jnz	bad
#endif /* REQUIRED_MASK1 */

	popfl
	xor	%eax,%eax
	ret

bad:
	popfl
	movl	$1,%eax
	ret
