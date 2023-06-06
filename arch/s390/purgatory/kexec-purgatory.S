/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>

	.section .rodata, "a"

	.balign	8
SYM_DATA_START(kexec_purgatory)
	.incbin	"arch/s390/purgatory/purgatory.ro"
SYM_DATA_END_LABEL(kexec_purgatory, SYM_L_LOCAL, kexec_purgatory_end)

	.balign	8
SYM_DATA(kexec_purgatory_size, .quad kexec_purgatory_end-kexec_purgatory)
