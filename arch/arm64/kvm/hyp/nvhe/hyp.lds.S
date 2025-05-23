/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Google LLC.
 * Written by David Brazdil <dbrazdil@google.com>
 *
 * Linker script used for partial linking of nVHE EL2 object files.
 */

#include <asm/hyp_image.h>
#include <asm-generic/vmlinux.lds.h>
#include <asm/cache.h>
#include <asm/memory.h>

SECTIONS {
	HYP_SECTION(.idmap.text)
	HYP_SECTION(.text)
	HYP_SECTION(.data..ro_after_init)
	HYP_SECTION(.rodata)

	/*
	 * .hyp..data..percpu needs to be page aligned to maintain the same
	 * alignment for when linking into vmlinux.
	 */
	. = ALIGN(PAGE_SIZE);
	BEGIN_HYP_SECTION(.data..percpu)
		PERCPU_INPUT(L1_CACHE_BYTES)
	END_HYP_SECTION

	HYP_SECTION(.bss)
	HYP_SECTION(.data)
}
