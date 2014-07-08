/*
 * ld script to make ARM PIEs
 * taken from the ARM vmlinux.lds.S version by Russ Dill <russ.dill@ti.com.
 */

#include <asm-generic/pie.lds.h>

OUTPUT_ARCH(arm)

SECTIONS
{
	. = 0x0;

	PIE_COMMON_START
	.got.plt : {
		*(.got)
		*(.got.plt)
	}
	.text : {
		PIE_TEXT_TEXT
	}
	PIE_COMMON_END

	__pie_rel_dyn_start : {
		VMLINUX_SYMBOL(__pie_rel_dyn_start) = .;
	}
	.rel.dyn : {
		KEEP(*(.rel.pie.text))
	}
	__pie_rel_dyn_end : {
		VMLINUX_SYMBOL(__pie_rel_dyn_end) = .;
	}

	PIE_OVERLAY_START
	OVERLAY : NOCROSSREFS {
		PIE_OVERLAY_SECTION(overlay)
		PIE_OVERLAY_SECTION(rk3288)
		PIE_OVERLAY_SECTION(rk3188)
		PIE_OVERLAY_SECTION(rk3036)
	}
	PIE_OVERLAY_END

	PIE_DISCARDS
}
