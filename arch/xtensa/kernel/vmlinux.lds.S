/*
 * arch/xtensa/kernel/vmlinux.lds.S
 *
 * Xtensa linker script
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 * Marc Gauthier <marc@tensilica.com, marc@alumni.uwaterloo.ca>
 * Joe Taylor <joe@tensilica.com, joetylr@yahoo.com>
 */

#include <asm-generic/vmlinux.lds.h>

#include <asm/variant/core.h>
OUTPUT_ARCH(xtensa)
ENTRY(_start)

#ifdef __XTENSA_EB__
jiffies = jiffies_64 + 4;
#else
jiffies = jiffies_64;
#endif

#define KERNELOFFSET 0xd0001000

/* Note: In the following macros, it would be nice to specify only the
   vector name and section kind and construct "sym" and "section" using
   CPP concatenation, but that does not work reliably.  Concatenating a
   string with "." produces an invalid token.  CPP will not print a
   warning because it thinks this is an assembly file, but it leaves
   them as multiple tokens and there may or may not be whitespace
   between them.  */

/* Macro for a relocation entry */

#define RELOCATE_ENTRY(sym, section)		\
	LONG(sym ## _start);			\
	LONG(sym ## _end);			\
	LONG(LOADADDR(section))

/* Macro to define a section for a vector.
 *
 * Use of the MIN function catches the types of errors illustrated in
 * the following example:
 *
 * Assume the section .DoubleExceptionVector.literal is completely
 * full.  Then a programmer adds code to .DoubleExceptionVector.text
 * that produces another literal.  The final literal position will
 * overlay onto the first word of the adjacent code section
 * .DoubleExceptionVector.text.  (In practice, the literals will
 * overwrite the code, and the first few instructions will be
 * garbage.)
 */

#define SECTION_VECTOR(sym, section, addr, max_prevsec_size, prevsec)       \
  section addr : AT((MIN(LOADADDR(prevsec) + max_prevsec_size,		    \
		         LOADADDR(prevsec) + SIZEOF(prevsec)) + 3) & ~ 3)   \
  {									    \
    . = ALIGN(4);							    \
    sym ## _start = ABSOLUTE(.);		 			    \
    *(section)								    \
    sym ## _end = ABSOLUTE(.);						    \
  }

/*
 *  Mapping of input sections to output sections when linking.
 */

SECTIONS
{
  . = KERNELOFFSET;
  /* .text section */

  _text = .;
  _stext = .;
  _ftext = .;

  .text :
  {
    /* The .head.text section must be the first section! */
    *(.head.text)
    *(.literal .text)
    VMLINUX_SYMBOL(__sched_text_start) = .;
    *(.sched.literal .sched.text)
    VMLINUX_SYMBOL(__sched_text_end) = .;
    VMLINUX_SYMBOL(__lock_text_start) = .;
    *(.spinlock.literal .spinlock.text)
    VMLINUX_SYMBOL(__lock_text_end) = .;

  }
  _etext = .;
  PROVIDE (etext = .);

  . = ALIGN(16);

  RODATA

  /*  Relocation table */

  .fixup   : { *(.fixup) }

  . = ALIGN(16);

  __ex_table : {
    __start___ex_table = .;
    *(__ex_table)
    __stop___ex_table = .;
  }

  /* Data section */

  . = ALIGN(XCHAL_ICACHE_LINESIZE);
  _fdata = .;
  .data :
  {
    DATA_DATA
    CONSTRUCTORS
    . = ALIGN(XCHAL_ICACHE_LINESIZE);
    *(.data.cacheline_aligned)
  }

  _edata = .;

  /* The initial task */
  . = ALIGN(8192);
  .data.init_task : { *(.data.init_task) }

  /* Initialization code and data: */

  . = ALIGN(1 << 12);
  __init_begin = .;
  .init.text : {
  	_sinittext = .;
	*(.init.literal) *(.cpuinit.literal) 
	*(.devinit.literal) *(.meminit.literal)
	INIT_TEXT
	_einittext = .;
  }

  .init.data :
  {
    INIT_DATA
    . = ALIGN(0x4);
    __tagtable_begin = .;
    *(.taglist)
    __tagtable_end = .;

    . = ALIGN(16);
    __boot_reloc_table_start = ABSOLUTE(.);

    RELOCATE_ENTRY(_WindowVectors_text,
		   .WindowVectors.text);
    RELOCATE_ENTRY(_KernelExceptionVector_text,
		   .KernelExceptionVector.text);
    RELOCATE_ENTRY(_UserExceptionVector_text,
		   .UserExceptionVector.text);
    RELOCATE_ENTRY(_DoubleExceptionVector_literal,
		   .DoubleExceptionVector.literal);
    RELOCATE_ENTRY(_DoubleExceptionVector_text,
		   .DoubleExceptionVector.text);
    RELOCATE_ENTRY(_DebugInterruptVector_text,
		   .DebugInterruptVector.text);
  
    __boot_reloc_table_end = ABSOLUTE(.) ;
  }

  . = ALIGN(XCHAL_ICACHE_LINESIZE);

  __setup_start = .;
  .init.setup : { *(.init.setup) }
  __setup_end = .;

  __initcall_start = .;
  .initcall.init : {
	INITCALLS
  }
  __initcall_end = .;

  __con_initcall_start = .;
  .con_initcall.init : { *(.con_initcall.init) }
  __con_initcall_end = .;

  SECURITY_INIT


#ifdef CONFIG_BLK_DEV_INITRD
  . = ALIGN(4096);
  __initramfs_start =.;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;
#endif

  PERCPU(4096)


  /* We need this dummy segment here */

  . = ALIGN(4);
  .dummy : { LONG(0) }

  /* The vectors are relocated to the real position at startup time */

  SECTION_VECTOR (_WindowVectors_text,
		  .WindowVectors.text,
		  XCHAL_WINDOW_VECTORS_VADDR, 4,
		  .dummy)
  SECTION_VECTOR (_DebugInterruptVector_literal,
		  .DebugInterruptVector.literal,
		  XCHAL_DEBUG_VECTOR_VADDR - 4,
		  SIZEOF(.WindowVectors.text),
		  .WindowVectors.text)
  SECTION_VECTOR (_DebugInterruptVector_text,
		  .DebugInterruptVector.text,
		  XCHAL_DEBUG_VECTOR_VADDR,
		  4,
		  .DebugInterruptVector.literal)
  SECTION_VECTOR (_KernelExceptionVector_literal,
		  .KernelExceptionVector.literal,
		  XCHAL_KERNEL_VECTOR_VADDR - 4,
		  SIZEOF(.DebugInterruptVector.text),
		  .DebugInterruptVector.text)
  SECTION_VECTOR (_KernelExceptionVector_text,
		  .KernelExceptionVector.text,
		  XCHAL_KERNEL_VECTOR_VADDR,
		  4,
		  .KernelExceptionVector.literal)
  SECTION_VECTOR (_UserExceptionVector_literal,
		  .UserExceptionVector.literal,
		  XCHAL_USER_VECTOR_VADDR - 4,
		  SIZEOF(.KernelExceptionVector.text),
		  .KernelExceptionVector.text)
  SECTION_VECTOR (_UserExceptionVector_text,
		  .UserExceptionVector.text,
		  XCHAL_USER_VECTOR_VADDR,
		  4,
		  .UserExceptionVector.literal)
  SECTION_VECTOR (_DoubleExceptionVector_literal,
		  .DoubleExceptionVector.literal,
		  XCHAL_DOUBLEEXC_VECTOR_VADDR - 16,
		  SIZEOF(.UserExceptionVector.text),
		  .UserExceptionVector.text)
  SECTION_VECTOR (_DoubleExceptionVector_text,
		  .DoubleExceptionVector.text,
		  XCHAL_DOUBLEEXC_VECTOR_VADDR,
		  32,
		  .DoubleExceptionVector.literal)

  . = (LOADADDR( .DoubleExceptionVector.text ) + SIZEOF( .DoubleExceptionVector.text ) + 3) & ~ 3;
  . = ALIGN(1 << 12);

  __init_end = .;

  . = ALIGN(8192);

  /* BSS section */
  _bss_start = .;
  .bss : { *(.bss.page_aligned) *(.bss) }
  _bss_end = .;

  _end = .;

  /* only used by the boot loader  */

  . = ALIGN(0x10);
  .bootstrap : { *(.bootstrap.literal .bootstrap.text .bootstrap.data) }

  . = ALIGN(0x1000);
  __initrd_start = .;
  .initrd : { *(.initrd) }
  __initrd_end = .;

  .ResetVector.text XCHAL_RESET_VECTOR_VADDR :
  {
    *(.ResetVector.text)
  }

  /* Sections to be discarded */
  /DISCARD/ :
  {
	*(.exit.literal)
	EXIT_TEXT
	EXIT_DATA
        *(.exitcall.exit)
  }

  .xt.lit : { *(.xt.lit) }
  .xt.prop : { *(.xt.prop) }

  .debug  0 :  { *(.debug) }
  .line  0 :  { *(.line) }
  .debug_srcinfo  0 :  { *(.debug_srcinfo) }
  .debug_sfnames  0 :  { *(.debug_sfnames) }
  .debug_aranges  0 :  { *(.debug_aranges) }
  .debug_pubnames  0 :  { *(.debug_pubnames) }
  .debug_info  0 :  { *(.debug_info) }
  .debug_abbrev  0 :  { *(.debug_abbrev) }
  .debug_line  0 :  { *(.debug_line) }
  .debug_frame  0 :  { *(.debug_frame) }
  .debug_str  0 :  { *(.debug_str) }
  .debug_loc  0 :  { *(.debug_loc) }
  .debug_macinfo  0 :  { *(.debug_macinfo) }
  .debug_weaknames  0 :  { *(.debug_weaknames) }
  .debug_funcnames  0 :  { *(.debug_funcnames) }
  .debug_typenames  0 :  { *(.debug_typenames) }
  .debug_varnames  0 :  { *(.debug_varnames) }

  .xt.insn 0 :
  {
    *(.xt.insn)
    *(.gnu.linkonce.x*)
  }

  .xt.lit 0 :
  {
    *(.xt.lit)
    *(.gnu.linkonce.p*)
  }
}
