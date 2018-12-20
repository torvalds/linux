/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/page.h>

/*
 * .boot.data section is shared between the decompressor code and the
 * decompressed kernel. The decompressor will store values in it, and copy
 * over to the decompressed image before starting it.
 *
 * .boot.data variables are kept in separate .boot.data.<var name> sections,
 * which are sorted by alignment first, then by name before being merged
 * into single .boot.data section. This way big holes cased by page aligned
 * structs are avoided and linker produces consistent result.
 */
#define BOOT_DATA							\
	. = ALIGN(PAGE_SIZE);						\
	.boot.data : {							\
		__boot_data_start = .;					\
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.boot.data*)))		\
		__boot_data_end = .;					\
	}
