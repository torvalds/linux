/* SPDX-License-Identifier: GPL-2.0 */

	.section ".rodata.kernel_info", "a"

	.global kernel_info

kernel_info:
	/* Header, Linux top (structure). */
	.ascii	"LToP"
	/* Size. */
	.long	kernel_info_var_len_data - kernel_info
	/* Size total. */
	.long	kernel_info_end - kernel_info

kernel_info_var_len_data:
	/* Empty for time being... */
kernel_info_end:
