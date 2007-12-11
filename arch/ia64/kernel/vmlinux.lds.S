
#include <asm/cache.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/pgtable.h>

#define LOAD_OFFSET	(KERNEL_START - KERNEL_TR_PAGE_SIZE)
#include <asm-generic/vmlinux.lds.h>

#define IVT_TEXT							\
		VMLINUX_SYMBOL(__start_ivt_text) = .;			\
		*(.text.ivt)						\
		VMLINUX_SYMBOL(__end_ivt_text) = .;

OUTPUT_FORMAT("elf64-ia64-little")
OUTPUT_ARCH(ia64)
ENTRY(phys_start)
jiffies = jiffies_64;
PHDRS {
  code   PT_LOAD;
  percpu PT_LOAD;
  data   PT_LOAD;
  note   PT_NOTE;
  unwind 0x70000001; /* PT_IA_64_UNWIND, but ld doesn't match the name */
}
SECTIONS
{
  /* Sections to be discarded */
  /DISCARD/ : {
	*(.exit.text)
	*(.exit.data)
	*(.exitcall.exit)
	*(.IA_64.unwind.exit.text)
	*(.IA_64.unwind_info.exit.text)
	}

  v = PAGE_OFFSET;	/* this symbol is here to make debugging easier... */
  phys_start = _start - LOAD_OFFSET;

  code : { } :code
  . = KERNEL_START;

  _text = .;
  _stext = .;

  .text : AT(ADDR(.text) - LOAD_OFFSET)
    {
	IVT_TEXT
	TEXT_TEXT
	SCHED_TEXT
	LOCK_TEXT
	KPROBES_TEXT
	*(.gnu.linkonce.t*)
    }
  .text.head : AT(ADDR(.text.head) - LOAD_OFFSET)
	{ *(.text.head) }
  .text2 : AT(ADDR(.text2) - LOAD_OFFSET)
	{ *(.text2) }
#ifdef CONFIG_SMP
  .text.lock : AT(ADDR(.text.lock) - LOAD_OFFSET)
	{ *(.text.lock) }
#endif
  _etext = .;

  /* Read-only data */

  NOTES :code :note		/* put .notes in text and mark in PT_NOTE  */
  code_continues : {} :code	/* switch back to regular program...  */

  /* Exception table */
  . = ALIGN(16);
  __ex_table : AT(ADDR(__ex_table) - LOAD_OFFSET)
	{
	  __start___ex_table = .;
	  *(__ex_table)
	  __stop___ex_table = .;
	}

  /* MCA table */
  . = ALIGN(16);
  __mca_table : AT(ADDR(__mca_table) - LOAD_OFFSET)
	{
	  __start___mca_table = .;
	  *(__mca_table)
	  __stop___mca_table = .;
	}

  .data.patch.phys_stack_reg : AT(ADDR(.data.patch.phys_stack_reg) - LOAD_OFFSET)
	{
	  __start___phys_stack_reg_patchlist = .;
	  *(.data.patch.phys_stack_reg)
	  __end___phys_stack_reg_patchlist = .;
	}

  /* Global data */
  _data = .;

  /* Unwind info & table: */
  . = ALIGN(8);
  .IA_64.unwind_info : AT(ADDR(.IA_64.unwind_info) - LOAD_OFFSET)
	{ *(.IA_64.unwind_info*) }
  .IA_64.unwind : AT(ADDR(.IA_64.unwind) - LOAD_OFFSET)
	{
	  __start_unwind = .;
	  *(.IA_64.unwind*)
	  __end_unwind = .;
	} :code :unwind
  code_continues2 : {} : code

  RODATA

  .opd : AT(ADDR(.opd) - LOAD_OFFSET)
	{ *(.opd) }

  /* Initialization code and data: */

  . = ALIGN(PAGE_SIZE);
  __init_begin = .;
  .init.text : AT(ADDR(.init.text) - LOAD_OFFSET)
	{
	  _sinittext = .;
	  *(.init.text)
	  _einittext = .;
	}

  .init.data : AT(ADDR(.init.data) - LOAD_OFFSET)
	{ *(.init.data) }

#ifdef CONFIG_BLK_DEV_INITRD
  .init.ramfs : AT(ADDR(.init.ramfs) - LOAD_OFFSET)
	{
	  __initramfs_start = .;
	  *(.init.ramfs)
	  __initramfs_end = .;
	}
#endif

   . = ALIGN(16);
  .init.setup : AT(ADDR(.init.setup) - LOAD_OFFSET)
        {
	  __setup_start = .;
	  *(.init.setup)
	  __setup_end = .;
	}
  .initcall.init : AT(ADDR(.initcall.init) - LOAD_OFFSET)
	{
	  __initcall_start = .;
	INITCALLS
	  __initcall_end = .;
	}

  .data.patch.vtop : AT(ADDR(.data.patch.vtop) - LOAD_OFFSET)
	{
	  __start___vtop_patchlist = .;
	  *(.data.patch.vtop)
	  __end___vtop_patchlist = .;
	}

  .data.patch.mckinley_e9 : AT(ADDR(.data.patch.mckinley_e9) - LOAD_OFFSET)
	{
	  __start___mckinley_e9_bundles = .;
	  *(.data.patch.mckinley_e9)
	  __end___mckinley_e9_bundles = .;
	}

#if defined(CONFIG_IA64_GENERIC)
  /* Machine Vector */
  . = ALIGN(16);
  .machvec : AT(ADDR(.machvec) - LOAD_OFFSET)
	{
	  machvec_start = .;
	  *(.machvec)
	  machvec_end = .;
	}
#endif

  . = ALIGN(8);
   __con_initcall_start = .;
  .con_initcall.init : AT(ADDR(.con_initcall.init) - LOAD_OFFSET)
	{ *(.con_initcall.init) }
  __con_initcall_end = .;
  __security_initcall_start = .;
  .security_initcall.init : AT(ADDR(.security_initcall.init) - LOAD_OFFSET)
	{ *(.security_initcall.init) }
  __security_initcall_end = .;
  . = ALIGN(PAGE_SIZE);
  __init_end = .;

  /* The initial task and kernel stack */
  .data.init_task : AT(ADDR(.data.init_task) - LOAD_OFFSET)
	{ *(.data.init_task) }

  .data.page_aligned : AT(ADDR(.data.page_aligned) - LOAD_OFFSET)
        { *(__special_page_section)
	  __start_gate_section = .;
	  *(.data.gate)
	  __stop_gate_section = .;
	}
  . = ALIGN(PAGE_SIZE);		/* make sure the gate page doesn't expose
  				 * kernel data
				 */

  .data.read_mostly : AT(ADDR(.data.read_mostly) - LOAD_OFFSET)
        { *(.data.read_mostly) }

  .data.cacheline_aligned : AT(ADDR(.data.cacheline_aligned) - LOAD_OFFSET)
        { *(.data.cacheline_aligned) }

  /* Per-cpu data: */
  percpu : { } :percpu
  . = ALIGN(PERCPU_PAGE_SIZE);
  __phys_per_cpu_start = .;
  .data.percpu PERCPU_ADDR : AT(__phys_per_cpu_start - LOAD_OFFSET)
	{
		__per_cpu_start = .;
		*(.data.percpu)
		*(.data.percpu.shared_aligned)
		__per_cpu_end = .;
	}
  . = __phys_per_cpu_start + PERCPU_PAGE_SIZE;	/* ensure percpu data fits
  						 * into percpu page size
						 */

  data : { } :data
  .data : AT(ADDR(.data) - LOAD_OFFSET)
	{
		DATA_DATA
		*(.data1)
		*(.gnu.linkonce.d*)
		CONSTRUCTORS
	}

  . = ALIGN(16);	/* gp must be 16-byte aligned for exc. table */
  .got : AT(ADDR(.got) - LOAD_OFFSET)
	{ *(.got.plt) *(.got) }
  __gp = ADDR(.got) + 0x200000;
  /* We want the small data sections together, so single-instruction offsets
     can access them all, and initialized data all before uninitialized, so
     we can shorten the on-disk segment size.  */
  .sdata : AT(ADDR(.sdata) - LOAD_OFFSET)
	{ *(.sdata) *(.sdata1) *(.srdata) }
  _edata  =  .;
  __bss_start = .;
  .sbss : AT(ADDR(.sbss) - LOAD_OFFSET)
	{ *(.sbss) *(.scommon) }
  .bss : AT(ADDR(.bss) - LOAD_OFFSET)
	{ *(.bss) *(COMMON) }
  __bss_stop = .;

  _end = .;

  code : { } :code
  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  /* DWARF debug sections.
     Symbols in the DWARF debugging sections are relative to the beginning
     of the section so we begin them at 0.  */
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
  .debug_info     0 : { *(.debug_info) }
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
  /* These must appear regardless of  .  */
  /DISCARD/ : { *(.comment) }
  /DISCARD/ : { *(.note) }
}
