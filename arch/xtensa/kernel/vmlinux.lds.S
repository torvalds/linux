/*
 * arch/xtensa/kernel/vmlinux.lds.S
 *
 * Xtensa linker script
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2008 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 * Marc Gauthier <marc@tensilica.com, marc@alumni.uwaterloo.ca>
 * Joe Taylor <joe@tensilica.com, joetylr@yahoo.com>
 */

#define RO_EXCEPTION_TABLE_ALIGN	16

#include <asm-generic/vmlinux.lds.h>
#include <asm/page.h>
#include <asm/thread_info.h>

#include <asm/core.h>
#include <asm/vectors.h>

OUTPUT_ARCH(xtensa)
ENTRY(_start)

#ifdef __XTENSA_EB__
jiffies = jiffies_64 + 4;
#else
jiffies = jiffies_64;
#endif

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

/*
 * Macro to define a section for a vector. When CONFIG_VECTORS_OFFSET is
 * defined code for every vector is located with other init data. At startup
 * time head.S copies code for every vector to its final position according
 * to description recorded in the corresponding RELOCATE_ENTRY.
 */

#ifdef CONFIG_VECTORS_OFFSET
#define SECTION_VECTOR(sym, section, addr, prevsec)                         \
  section addr : AT(((LOADADDR(prevsec) + SIZEOF(prevsec)) + 3) & ~ 3)      \
  {									    \
    . = ALIGN(4);							    \
    sym ## _start = ABSOLUTE(.);		 			    \
    *(section)								    \
    sym ## _end = ABSOLUTE(.);						    \
  }
#else
#define SECTION_VECTOR(section, addr)					    \
  . = addr;								    \
  *(section)
#endif

/*
 *  Mapping of input sections to output sections when linking.
 */

SECTIONS
{
  . = KERNELOFFSET;
  /* .text section */

  _text = .;
  _stext = .;

  .text :
  {
    /* The HEAD_TEXT section must be the first section! */
    HEAD_TEXT

#ifndef CONFIG_VECTORS_OFFSET
  . = ALIGN(PAGE_SIZE);
  _vecbase = .;

  SECTION_VECTOR (.WindowVectors.text, WINDOW_VECTORS_VADDR)
#if XCHAL_EXCM_LEVEL >= 2
  SECTION_VECTOR (.Level2InterruptVector.text, INTLEVEL2_VECTOR_VADDR)
#endif
#if XCHAL_EXCM_LEVEL >= 3
  SECTION_VECTOR (.Level3InterruptVector.text, INTLEVEL3_VECTOR_VADDR)
#endif
#if XCHAL_EXCM_LEVEL >= 4
  SECTION_VECTOR (.Level4InterruptVector.text, INTLEVEL4_VECTOR_VADDR)
#endif
#if XCHAL_EXCM_LEVEL >= 5
  SECTION_VECTOR (.Level5InterruptVector.text, INTLEVEL5_VECTOR_VADDR)
#endif
#if XCHAL_EXCM_LEVEL >= 6
  SECTION_VECTOR (.Level6InterruptVector.text, INTLEVEL6_VECTOR_VADDR)
#endif
  SECTION_VECTOR (.DebugInterruptVector.text, DEBUG_VECTOR_VADDR)
  SECTION_VECTOR (.KernelExceptionVector.text, KERNEL_VECTOR_VADDR)
  SECTION_VECTOR (.UserExceptionVector.text, USER_VECTOR_VADDR)
  SECTION_VECTOR (.DoubleExceptionVector.text, DOUBLEEXC_VECTOR_VADDR)
#endif

    IRQENTRY_TEXT
    SOFTIRQENTRY_TEXT
    ENTRY_TEXT
    TEXT_TEXT
    SCHED_TEXT
    CPUIDLE_TEXT
    LOCK_TEXT
    *(.fixup)
  }
  _etext = .;
  PROVIDE (etext = .);

  . = ALIGN(16);

  RO_DATA(4096)

  /* Data section */

#ifdef CONFIG_XIP_KERNEL
  INIT_TEXT_SECTION(PAGE_SIZE)
#else
  _sdata = .;
  RW_DATA(XCHAL_ICACHE_LINESIZE, PAGE_SIZE, THREAD_SIZE)
  _edata = .;

  /* Initialization code and data: */

  . = ALIGN(PAGE_SIZE);
  __init_begin = .;
  INIT_TEXT_SECTION(PAGE_SIZE)

  .init.data :
  {
    INIT_DATA
  }
#endif

  .init.rodata :
  {
    . = ALIGN(0x4);
    __tagtable_begin = .;
    *(.taglist)
    __tagtable_end = .;

    . = ALIGN(16);
    __boot_reloc_table_start = ABSOLUTE(.);

#ifdef CONFIG_VECTORS_OFFSET
    RELOCATE_ENTRY(_WindowVectors_text,
		   .WindowVectors.text);
#if XCHAL_EXCM_LEVEL >= 2
    RELOCATE_ENTRY(_Level2InterruptVector_text,
		   .Level2InterruptVector.text);
#endif
#if XCHAL_EXCM_LEVEL >= 3
    RELOCATE_ENTRY(_Level3InterruptVector_text,
		   .Level3InterruptVector.text);
#endif
#if XCHAL_EXCM_LEVEL >= 4
    RELOCATE_ENTRY(_Level4InterruptVector_text,
		   .Level4InterruptVector.text);
#endif
#if XCHAL_EXCM_LEVEL >= 5
    RELOCATE_ENTRY(_Level5InterruptVector_text,
		   .Level5InterruptVector.text);
#endif
#if XCHAL_EXCM_LEVEL >= 6
    RELOCATE_ENTRY(_Level6InterruptVector_text,
		   .Level6InterruptVector.text);
#endif
    RELOCATE_ENTRY(_KernelExceptionVector_text,
		   .KernelExceptionVector.text);
    RELOCATE_ENTRY(_UserExceptionVector_text,
		   .UserExceptionVector.text);
    RELOCATE_ENTRY(_DoubleExceptionVector_text,
		   .DoubleExceptionVector.text);
    RELOCATE_ENTRY(_DebugInterruptVector_text,
		   .DebugInterruptVector.text);
#endif
#ifdef CONFIG_XIP_KERNEL
    RELOCATE_ENTRY(_xip_data, .data);
    RELOCATE_ENTRY(_xip_init_data, .init.data);
#else
#if defined(CONFIG_SMP)
    RELOCATE_ENTRY(_SecondaryResetVector_text,
		   .SecondaryResetVector.text);
#endif
#endif

    __boot_reloc_table_end = ABSOLUTE(.) ;

    INIT_SETUP(XCHAL_ICACHE_LINESIZE)
    INIT_CALLS
    CON_INITCALL
    INIT_RAM_FS
  }

  PERCPU_SECTION(XCHAL_ICACHE_LINESIZE)

  /* We need this dummy segment here */

  . = ALIGN(4);
  .dummy : { LONG(0) }

#ifdef CONFIG_VECTORS_OFFSET
  /* The vectors are relocated to the real position at startup time */

  SECTION_VECTOR (_WindowVectors_text,
		  .WindowVectors.text,
		  WINDOW_VECTORS_VADDR,
		  .dummy)
  SECTION_VECTOR (_DebugInterruptVector_text,
		  .DebugInterruptVector.text,
		  DEBUG_VECTOR_VADDR,
		  .WindowVectors.text)
#undef LAST
#define LAST	.DebugInterruptVector.text
#if XCHAL_EXCM_LEVEL >= 2
  SECTION_VECTOR (_Level2InterruptVector_text,
		  .Level2InterruptVector.text,
		  INTLEVEL2_VECTOR_VADDR,
		  LAST)
# undef LAST
# define LAST	.Level2InterruptVector.text
#endif
#if XCHAL_EXCM_LEVEL >= 3
  SECTION_VECTOR (_Level3InterruptVector_text,
		  .Level3InterruptVector.text,
		  INTLEVEL3_VECTOR_VADDR,
		  LAST)
# undef LAST
# define LAST	.Level3InterruptVector.text
#endif
#if XCHAL_EXCM_LEVEL >= 4
  SECTION_VECTOR (_Level4InterruptVector_text,
		  .Level4InterruptVector.text,
		  INTLEVEL4_VECTOR_VADDR,
		  LAST)
# undef LAST
# define LAST	.Level4InterruptVector.text
#endif
#if XCHAL_EXCM_LEVEL >= 5
  SECTION_VECTOR (_Level5InterruptVector_text,
		  .Level5InterruptVector.text,
		  INTLEVEL5_VECTOR_VADDR,
		  LAST)
# undef LAST
# define LAST	.Level5InterruptVector.text
#endif
#if XCHAL_EXCM_LEVEL >= 6
  SECTION_VECTOR (_Level6InterruptVector_text,
		  .Level6InterruptVector.text,
		  INTLEVEL6_VECTOR_VADDR,
		  LAST)
# undef LAST
# define LAST	.Level6InterruptVector.text
#endif
  SECTION_VECTOR (_KernelExceptionVector_text,
		  .KernelExceptionVector.text,
		  KERNEL_VECTOR_VADDR,
		  LAST)
#undef LAST
  SECTION_VECTOR (_UserExceptionVector_text,
		  .UserExceptionVector.text,
		  USER_VECTOR_VADDR,
		  .KernelExceptionVector.text)
  SECTION_VECTOR (_DoubleExceptionVector_text,
		  .DoubleExceptionVector.text,
		  DOUBLEEXC_VECTOR_VADDR,
		  .UserExceptionVector.text)

  . = (LOADADDR( .DoubleExceptionVector.text ) + SIZEOF( .DoubleExceptionVector.text ) + 3) & ~ 3;

#endif
#if !defined(CONFIG_XIP_KERNEL) && defined(CONFIG_SMP)

  SECTION_VECTOR (_SecondaryResetVector_text,
		  .SecondaryResetVector.text,
		  RESET_VECTOR1_VADDR,
		  .DoubleExceptionVector.text)

  . = LOADADDR(.SecondaryResetVector.text)+SIZEOF(.SecondaryResetVector.text);

#endif

  . = ALIGN(PAGE_SIZE);

#ifndef CONFIG_XIP_KERNEL
  __init_end = .;

  BSS_SECTION(0, 8192, 0)
#endif

  _end = .;

#ifdef CONFIG_XIP_KERNEL
  . = CONFIG_XIP_DATA_ADDR;

  _xip_start = .;

#undef LOAD_OFFSET
#define LOAD_OFFSET \
  (CONFIG_XIP_DATA_ADDR - (LOADADDR(.dummy) + SIZEOF(.dummy) + 3) & ~ 3)

  _xip_data_start = .;
  _sdata = .;
  RW_DATA(XCHAL_ICACHE_LINESIZE, PAGE_SIZE, THREAD_SIZE)
  _edata = .;
  _xip_data_end = .;

  /* Initialization data: */

  STRUCT_ALIGN();

  _xip_init_data_start = .;
  __init_begin = .;
  .init.data :
  {
    INIT_DATA
  }
  _xip_init_data_end = .;
  __init_end = .;
  BSS_SECTION(0, 8192, 0)

  _xip_end = .;

#undef LOAD_OFFSET
#endif

  DWARF_DEBUG

  .xt.prop 0 : { KEEP(*(.xt.prop .xt.prop.* .gnu.linkonce.prop.*)) }
  .xt.insn 0 : { KEEP(*(.xt.insn .xt.insn.* .gnu.linkonce.x*)) }
  .xt.lit  0 : { KEEP(*(.xt.lit  .xt.lit.*  .gnu.linkonce.p*)) }

  /* Sections to be discarded */
  DISCARDS
}
