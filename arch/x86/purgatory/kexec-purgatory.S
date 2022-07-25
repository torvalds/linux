/* SPDX-License-Identifier: GPL-2.0 */

	.section .rodata, "a"

	.align	8
kexec_purgatory:
	.globl	kexec_purgatory
	.incbin	"arch/x86/purgatory/purgatory.ro"
.Lkexec_purgatory_end:

	.align	8
kexec_purgatory_size:
	.globl	kexec_purgatory_size
	.quad	.Lkexec_purgatory_end - kexec_purgatory
