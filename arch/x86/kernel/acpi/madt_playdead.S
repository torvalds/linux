/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/nospec-branch.h>
#include <asm/page_types.h>
#include <asm/processor-flags.h>

	.text
	.align PAGE_SIZE

/*
 * asm_acpi_mp_play_dead() - Hand over control of the CPU to the BIOS
 *
 * rdi: Address of the ACPI MADT MPWK ResetVector
 * rsi: PGD of the identity mapping
 */
SYM_FUNC_START(asm_acpi_mp_play_dead)
	ANNOTATE_NOENDBR
	/* Turn off global entries. Following CR3 write will flush them. */
	movq	%cr4, %rdx
	andq	$~(X86_CR4_PGE), %rdx
	movq	%rdx, %cr4

	/* Switch to identity mapping */
	movq	%rsi, %cr3

	/* Jump to reset vector */
	ANNOTATE_RETPOLINE_SAFE
	jmp	*%rdi
SYM_FUNC_END(asm_acpi_mp_play_dead)
