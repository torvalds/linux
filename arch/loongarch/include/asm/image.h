/* SPDX-License-Identifier: GPL-2.0 */
/*
 * LoongArch binary image header for EFI(PE/COFF) format.
 *
 * Author: Youling Tang <tangyouling@kylinos.cn>
 * Copyright (C) 2025 KylinSoft Corporation.
 */

#ifndef __ASM_IMAGE_H
#define __ASM_IMAGE_H

#ifndef __ASSEMBLER__

/**
 * struct loongarch_image_header
 *
 * @dos_sig: Optional PE format 'MZ' signature.
 * @padding_1: Reserved.
 * @kernel_entry: Kernel image entry pointer.
 * @kernel_asize: An estimated size of the memory image size in LSB byte order.
 * @text_offset: The image load offset in LSB byte order.
 * @padding_2: Reserved.
 * @pe_header: Optional offset to a PE format header.
 **/

struct loongarch_image_header {
	uint8_t dos_sig[2];
	uint16_t padding_1[3];
	uint64_t kernel_entry;
	uint64_t kernel_asize;
	uint64_t text_offset;
	uint32_t padding_2[7];
	uint32_t pe_header;
};

/*
 * loongarch_header_check_dos_sig - Helper to check the header
 *
 * Returns true (non-zero) if 'MZ' signature is found.
 */

static inline int loongarch_header_check_dos_sig(const struct loongarch_image_header *h)
{
	if (!h)
		return 0;

	return (h->dos_sig[0] == 'M' && h->dos_sig[1] == 'Z');
}

#endif /* __ASSEMBLER__ */

#endif /* __ASM_IMAGE_H */
