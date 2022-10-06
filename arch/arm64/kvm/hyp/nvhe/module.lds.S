/* SPDX-License-Identifier: GPL-2.0 */

#include <asm/hyp_image.h>
#include <asm/page-def.h>

SECTIONS {
	.hyp.text : {
		HYP_SECTION_SYMBOL_NAME(.text) = .;
		__hypmod_text_start = .;
		*(.text .text.*)
		__hypmod_text_end = .;
	}

	.hyp.bss : {
		HYP_SECTION_SYMBOL_NAME(.bss) = .;
		__hypmod_bss_start = .;
		*(.bss .bss.*)
		FILL(0)
		__hypmod_bss_end = .;
	}

	.hyp.rodata : {
		HYP_SECTION_SYMBOL_NAME(.rodata) = .;
		__hypmod_rodata_start = .;
		*(.rodata .rodata.*)
		BYTE(0)
		__hypmod_rodata_end = .;
	}

	.hyp.data : {
		HYP_SECTION_SYMBOL_NAME(.data) = .;
		__hypmod_data_start = .;
		*(.data .data.*)
		BYTE(0)
		__hypmod_data_end = .;
	}
}
