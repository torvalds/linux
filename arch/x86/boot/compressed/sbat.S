/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Embed SBAT data in the kernel.
 */
	.pushsection ".sbat", "a", @progbits
	.incbin CONFIG_EFI_SBAT_FILE
	.popsection
