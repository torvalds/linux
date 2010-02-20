
#include <asm/cache.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/pgtable.h>

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
  /* unwind exit sections must be discarded before the rest of the
     sections get included. */
  /DISCARD/ : {
	*(.IA_64.unwind.exit.text)
	*(.IA_64.unwind_info.exit.text)
	*(.comment)
	*(.note)
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

  EXCEPTION_TABLE(16)

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

  INIT_TEXT_SECTION(PAGE_SIZE)
  INIT_DATA_SECTION(16)

  .data.patch.vtop : AT(ADDR(.data.patch.vtop) - LOAD_OFFSET)
	{
	  __start___vtop_patchlist = .;
	  *(.data.patch.vtop)
	  __end___vtop_patchlist = .;
	}

  .data.patch.rse : AT(ADDR(.data.patch.rse) - LOAD_OFFSET)
	{
	  __start___rse_patchlist = .;
	  *(.data.patch.rse)
	  __end___rse_patchlist = .;
	}

  .data.patch.mckinley_e9 : AT(ADDR(.data.patch.mckinley_e9) - LOAD_OFFSET)
	{
	  __start___mckinley_e9_bundles = .;
	  *(.data.patch.mckinley_e9)
	  __end___mckinley_e9_bundles = .;
	}

#if defined(CONFIG_PARAVIRT)
  . = ALIGN(16);
  .paravirt_bundles : AT(ADDR(.paravirt_bundles) - LOAD_OFFSET)
	{
	  __start_paravirt_bundles = .;
          *(.paravirt_bundles)
	  __stop_paravirt_bundles = .;
	}
  . = ALIGN(16);
  .paravirt_insts : AT(ADDR(.paravirt_insts) - LOAD_OFFSET)
	{
	  __start_paravirt_insts = .;
          *(.paravirt_insts)
	  __stop_paravirt_insts = .;
	}
  . = ALIGN(16);
  .paravirt_branches : AT(ADDR(.paravirt_branches) - LOAD_OFFSET)
	{
	  __start_paravirt_branches = .;
	  *(.paravirt_branches)
	  __stop_paravirt_branches = .;
	}
#endif

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

#ifdef	CONFIG_SMP
  . = ALIGN(PERCPU_PAGE_SIZE);
  __cpu0_per_cpu = .;
  . = . + PERCPU_PAGE_SIZE;	/* cpu0 per-cpu space */
#endif

  . = ALIGN(PAGE_SIZE);
  __init_end = .;

  .data..page_aligned : AT(ADDR(.data..page_aligned) - LOAD_OFFSET)
        {
	PAGE_ALIGNED_DATA(PAGE_SIZE)
	  . = ALIGN(PAGE_SIZE);
	  __start_gate_section = .;
	  *(.data..gate)
	  __stop_gate_section = .;
#ifdef CONFIG_XEN
	  . = ALIGN(PAGE_SIZE);
	  __xen_start_gate_section = .;
	  *(.data..gate.xen)
	  __xen_stop_gate_section = .;
#endif
	}
  . = ALIGN(PAGE_SIZE);		/* make sure the gate page doesn't expose
  				 * kernel data
				 */

  /* Per-cpu data: */
  . = ALIGN(PERCPU_PAGE_SIZE);
  PERCPU_VADDR(PERCPU_ADDR, :percpu)
  __phys_per_cpu_start = __per_cpu_load;
  . = __phys_per_cpu_start + PERCPU_PAGE_SIZE;	/* ensure percpu data fits
  						 * into percpu page size
						 */

  data : { } :data
  .data : AT(ADDR(.data) - LOAD_OFFSET)
	{
		INIT_TASK_DATA(PAGE_SIZE)
		CACHELINE_ALIGNED_DATA(SMP_CACHE_BYTES)
		READ_MOSTLY_DATA(SMP_CACHE_BYTES)
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

  BSS_SECTION(0, 0, 0)

  _end = .;

  code : { } :code

  STABS_DEBUG
  DWARF_DEBUG

  /* Default discards */
  DISCARDS
}
