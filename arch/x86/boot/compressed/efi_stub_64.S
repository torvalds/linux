#include <asm/segment.h>
#include <asm/msr.h>
#include <asm/processor-flags.h>

#include "../../platform/efi/efi_stub_64.S"

#ifdef CONFIG_EFI_MIXED
	.code64
	.text
ENTRY(efi64_thunk)
	push	%rbp
	push	%rbx

	subq	$16, %rsp
	leaq	efi_exit32(%rip), %rax
	movl	%eax, 8(%rsp)
	leaq	efi_gdt64(%rip), %rax
	movl	%eax, 4(%rsp)
	movl	%eax, 2(%rax)		/* Fixup the gdt base address */
	leaq	efi32_boot_gdt(%rip), %rax
	movl	%eax, (%rsp)

	call	__efi64_thunk

	addq	$16, %rsp
	pop	%rbx
	pop	%rbp
	ret
ENDPROC(efi64_thunk)
#endif /* CONFIG_EFI_MIXED */
