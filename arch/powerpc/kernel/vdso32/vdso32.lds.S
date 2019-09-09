/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This is the infamous ld script for the 32 bits vdso
 * library
 */
#include <asm/vdso.h>

#ifdef __LITTLE_ENDIAN__
OUTPUT_FORMAT("elf32-powerpcle", "elf32-powerpcle", "elf32-powerpcle")
#else
OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc", "elf32-powerpc")
#endif
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)

SECTIONS
{
	. = VDSO32_LBASE + SIZEOF_HEADERS;

	.hash          	: { *(.hash) }			:text
	.gnu.hash      	: { *(.gnu.hash) }
	.dynsym        	: { *(.dynsym) }
	.dynstr        	: { *(.dynstr) }
	.gnu.version   	: { *(.gnu.version) }
	.gnu.version_d 	: { *(.gnu.version_d) }
	.gnu.version_r 	: { *(.gnu.version_r) }

	.note		: { *(.note.*) }		:text	:note

	. = ALIGN(16);
	.text		: {
		*(.text .stub .text.* .gnu.linkonce.t.* __ftr_alt_*)
	}		      	      			:text
	PROVIDE(__etext = .);
	PROVIDE(_etext = .);
	PROVIDE(etext = .);

	. = ALIGN(8);
	__ftr_fixup	: { *(__ftr_fixup) }

	. = ALIGN(8);
	__mmu_ftr_fixup	: { *(__mmu_ftr_fixup) }

	. = ALIGN(8);
	__lwsync_fixup	: { *(__lwsync_fixup) }

#ifdef CONFIG_PPC64
	. = ALIGN(8);
	__fw_ftr_fixup	: { *(__fw_ftr_fixup) }
#endif

	/*
	 * Other stuff is appended to the text segment:
	 */
	.rodata		: { *(.rodata .rodata.* .gnu.linkonce.r.*) }
	.rodata1	: { *(.rodata1) }

	.eh_frame_hdr	: { *(.eh_frame_hdr) }		:text	:eh_frame_hdr
	.eh_frame	: { KEEP (*(.eh_frame)) }	:text
	.gcc_except_table : { *(.gcc_except_table) }
	.fixup		: { *(.fixup) }

	.dynamic	: { *(.dynamic) }		:text	:dynamic
	.got		: { *(.got) }			:text
	.plt		: { *(.plt) }

	_end = .;
	__end = .;
	PROVIDE(end = .);

	/*
	 * Stabs debugging sections are here too.
	 */
	.stab 0 : { *(.stab) }
	.stabstr 0 : { *(.stabstr) }
	.stab.excl 0 : { *(.stab.excl) }
	.stab.exclstr 0 : { *(.stab.exclstr) }
	.stab.index 0 : { *(.stab.index) }
	.stab.indexstr 0 : { *(.stab.indexstr) }
	.comment       0 : { *(.comment) }

	/*
	 * DWARF debug sections.
	 * Symbols in the DWARF debugging sections are relative to the beginning
	 * of the section so we begin them at 0.
	 */
	/* DWARF 1 */
	.debug          0 : { *(.debug) }
	.line           0 : { *(.line) }
	/* GNU DWARF 1 extensions */
	.debug_srcinfo  0 : { *(.debug_srcinfo) }
	.debug_sfnames  0 : { *(.debug_sfnames) }
	/* DWARF 1.1 and DWARF 2 */
	.debug_aranges  0 : { *(.debug_aranges) }
	.debug_pubnames 0 : { *(.debug_pubnames) }
	/* DWARF 2 */
	.debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
	.debug_abbrev   0 : { *(.debug_abbrev) }
	.debug_line     0 : { *(.debug_line) }
	.debug_frame    0 : { *(.debug_frame) }
	.debug_str      0 : { *(.debug_str) }
	.debug_loc      0 : { *(.debug_loc) }
	.debug_macinfo  0 : { *(.debug_macinfo) }
	/* SGI/MIPS DWARF 2 extensions */
	.debug_weaknames 0 : { *(.debug_weaknames) }
	.debug_funcnames 0 : { *(.debug_funcnames) }
	.debug_typenames 0 : { *(.debug_typenames) }
	.debug_varnames  0 : { *(.debug_varnames) }

	/DISCARD/	: {
		*(.note.GNU-stack)
		*(.data .data.* .gnu.linkonce.d.* .sdata*)
		*(.bss .sbss .dynbss .dynsbss)
	}
}

/*
 * Very old versions of ld do not recognize this name token; use the constant.
 */
#define PT_GNU_EH_FRAME	0x6474e550

/*
 * We must supply the ELF program headers explicitly to get just one
 * PT_LOAD segment, and set the flags explicitly to make segments read-only.
 */
PHDRS
{
	text		PT_LOAD FILEHDR PHDRS FLAGS(5);	/* PF_R|PF_X */
	dynamic		PT_DYNAMIC FLAGS(4);		/* PF_R */
	note		PT_NOTE FLAGS(4);		/* PF_R */
	eh_frame_hdr	PT_GNU_EH_FRAME;
}

/*
 * This controls what symbols we export from the DSO.
 */
VERSION
{
	VDSO_VERSION_STRING {
	global:
		/*
		 * Has to be there for the kernel to find
		 */
		__kernel_datapage_offset;

		__kernel_get_syscall_map;
		__kernel_gettimeofday;
		__kernel_clock_gettime;
		__kernel_clock_getres;
		__kernel_get_tbfreq;
		__kernel_sync_dicache;
		__kernel_sync_dicache_p5;
		__kernel_sigtramp32;
		__kernel_sigtramp_rt32;
#ifdef CONFIG_PPC64
		__kernel_getcpu;
#endif
		__kernel_time;

	local: *;
	};
}
