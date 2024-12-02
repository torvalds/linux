/* SPDX-License-Identifier: GPL-2.0 */

	.section .rodata, "a"

	.align	8
kexec_purgatory:
	.globl	kexec_purgatory
	.incbin	"arch/s390/purgatory/purgatory.ro"
.Lkexec_purgatroy_end:

	.align	8
kexec_purgatory_size:
	.globl	kexec_purgatory_size
	.quad	.Lkexec_purgatroy_end - kexec_purgatory
