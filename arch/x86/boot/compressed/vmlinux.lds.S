/* SPDX-License-Identifier: GPL-2.0 */
#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT(CONFIG_OUTPUT_FORMAT)

#undef i386

#include <asm/cache.h>
#include <asm/page_types.h>

#ifdef CONFIG_X86_64
OUTPUT_ARCH(i386:x86-64)
ENTRY(startup_64)
#else
OUTPUT_ARCH(i386)
ENTRY(startup_32)
#endif

SECTIONS
{
	/* Be careful parts of head_64.S assume startup_32 is at
	 * address 0.
	 */
	. = 0;
	.head.text : {
		_head = . ;
		HEAD_TEXT
		_ehead = . ;
	}
	.rodata..compressed : {
		*(.rodata..compressed)
	}
	.text :	{
		_text = .; 	/* Text */
		*(.text)
		*(.text.*)
		_etext = . ;
	}
	.rodata : {
		_rodata = . ;
		*(.rodata)	 /* read-only data */
		*(.rodata.*)
		_erodata = . ;
	}
	.rel.dyn : {
		*(.rel.*)
	}
	.rela.dyn : {
		*(.rela.*)
	}
	.got : {
		*(.got)
	}
	.got.plt : {
		*(.got.plt)
	}

	.data :	{
		_data = . ;
		*(.data)
		*(.data.*)
		*(.bss.efistub)
		_edata = . ;
	}
	. = ALIGN(L1_CACHE_BYTES);
	.bss : {
		_bss = . ;
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(8);	/* For convenience during zeroing */
		_ebss = .;
	}
#ifdef CONFIG_X86_64
       . = ALIGN(PAGE_SIZE);
       .pgtable : {
		_pgtable = . ;
		*(.pgtable)
		_epgtable = . ;
	}
#endif
	. = ALIGN(PAGE_SIZE);	/* keep ZO size page aligned */
	_end = .;

	DISCARDS
}

ASSERT(SIZEOF(.got) == 0, "Unexpected GOT entries detected!")
#ifdef CONFIG_X86_64
ASSERT(SIZEOF(.got.plt) == 0 || SIZEOF(.got.plt) == 0x18, "Unexpected GOT/PLT entries detected!")
#else
ASSERT(SIZEOF(.got.plt) == 0 || SIZEOF(.got.plt) == 0xc, "Unexpected GOT/PLT entries detected!")
#endif

ASSERT(SIZEOF(.rel.dyn) == 0 && SIZEOF(.rela.dyn) == 0, "Unexpected run-time relocations detected!")
