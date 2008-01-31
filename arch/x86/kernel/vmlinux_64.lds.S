/* ld script to make x86-64 Linux kernel
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>;
 */

#define LOAD_OFFSET __START_KERNEL_map

#include <asm-generic/vmlinux.lds.h>
#include <asm/page.h>

#undef i386	/* in case the preprocessor is a 32bit one */

OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(phys_startup_64)
jiffies_64 = jiffies;
_proxy_pda = 1;
PHDRS {
	text PT_LOAD FLAGS(5);	/* R_E */
	data PT_LOAD FLAGS(7);	/* RWE */
	user PT_LOAD FLAGS(7);	/* RWE */
	data.init PT_LOAD FLAGS(7);	/* RWE */
	note PT_NOTE FLAGS(4);	/* R__ */
}
SECTIONS
{
  . = __START_KERNEL;
  phys_startup_64 = startup_64 - LOAD_OFFSET;
  _text = .;			/* Text and read-only data */
  .text :  AT(ADDR(.text) - LOAD_OFFSET) {
	/* First the code that has to be first for bootstrapping */
	*(.text.head)
	_stext = .;
	/* Then the rest */
	TEXT_TEXT
	SCHED_TEXT
	LOCK_TEXT
	KPROBES_TEXT
	*(.fixup)
	*(.gnu.warning)
	_etext = .;			/* End of text section */
  } :text = 0x9090

  . = ALIGN(16);		/* Exception table */
  __ex_table : AT(ADDR(__ex_table) - LOAD_OFFSET) {
  	__start___ex_table = .;
	 *(__ex_table)
  	__stop___ex_table = .;
  }

  NOTES :text :note

  BUG_TABLE :text

  RODATA

  . = ALIGN(4);
  .tracedata : AT(ADDR(.tracedata) - LOAD_OFFSET) {
  	__tracedata_start = .;
	*(.tracedata)
  	__tracedata_end = .;
  }

  . = ALIGN(PAGE_SIZE);        /* Align data segment to page size boundary */
				/* Data */
  .data : AT(ADDR(.data) - LOAD_OFFSET) {
	DATA_DATA
	CONSTRUCTORS
	} :data

  _edata = .;			/* End of data section */

  . = ALIGN(PAGE_SIZE);
  . = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
  .data.cacheline_aligned : AT(ADDR(.data.cacheline_aligned) - LOAD_OFFSET) {
	*(.data.cacheline_aligned)
  }
  . = ALIGN(CONFIG_X86_INTERNODE_CACHE_BYTES);
  .data.read_mostly : AT(ADDR(.data.read_mostly) - LOAD_OFFSET) {
  	*(.data.read_mostly)
  }

#define VSYSCALL_ADDR (-10*1024*1024)
#define VSYSCALL_PHYS_ADDR ((LOADADDR(.data.read_mostly) + SIZEOF(.data.read_mostly) + 4095) & ~(4095))
#define VSYSCALL_VIRT_ADDR ((ADDR(.data.read_mostly) + SIZEOF(.data.read_mostly) + 4095) & ~(4095))

#define VLOAD_OFFSET (VSYSCALL_ADDR - VSYSCALL_PHYS_ADDR)
#define VLOAD(x) (ADDR(x) - VLOAD_OFFSET)

#define VVIRT_OFFSET (VSYSCALL_ADDR - VSYSCALL_VIRT_ADDR)
#define VVIRT(x) (ADDR(x) - VVIRT_OFFSET)

  . = VSYSCALL_ADDR;
  .vsyscall_0 :	 AT(VSYSCALL_PHYS_ADDR) { *(.vsyscall_0) } :user
  __vsyscall_0 = VSYSCALL_VIRT_ADDR;

  . = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
  .vsyscall_fn : AT(VLOAD(.vsyscall_fn)) { *(.vsyscall_fn) }
  . = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
  .vsyscall_gtod_data : AT(VLOAD(.vsyscall_gtod_data))
		{ *(.vsyscall_gtod_data) }
  vsyscall_gtod_data = VVIRT(.vsyscall_gtod_data);
  .vsyscall_clock : AT(VLOAD(.vsyscall_clock))
		{ *(.vsyscall_clock) }
  vsyscall_clock = VVIRT(.vsyscall_clock);


  .vsyscall_1 ADDR(.vsyscall_0) + 1024: AT(VLOAD(.vsyscall_1))
		{ *(.vsyscall_1) }
  .vsyscall_2 ADDR(.vsyscall_0) + 2048: AT(VLOAD(.vsyscall_2))
		{ *(.vsyscall_2) }

  .vgetcpu_mode : AT(VLOAD(.vgetcpu_mode)) { *(.vgetcpu_mode) }
  vgetcpu_mode = VVIRT(.vgetcpu_mode);

  . = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
  .jiffies : AT(VLOAD(.jiffies)) { *(.jiffies) }
  jiffies = VVIRT(.jiffies);

  .vsyscall_3 ADDR(.vsyscall_0) + 3072: AT(VLOAD(.vsyscall_3))
		{ *(.vsyscall_3) }

  . = VSYSCALL_VIRT_ADDR + 4096;

#undef VSYSCALL_ADDR
#undef VSYSCALL_PHYS_ADDR
#undef VSYSCALL_VIRT_ADDR
#undef VLOAD_OFFSET
#undef VLOAD
#undef VVIRT_OFFSET
#undef VVIRT

  . = ALIGN(8192);		/* init_task */
  .data.init_task : AT(ADDR(.data.init_task) - LOAD_OFFSET) {
	*(.data.init_task)
  }:data.init

  . = ALIGN(4096);
  .data.page_aligned : AT(ADDR(.data.page_aligned) - LOAD_OFFSET) {
	*(.data.page_aligned)
  }

  /* might get freed after init */
  . = ALIGN(4096);
  __smp_alt_begin = .;
  __smp_locks = .;
  .smp_locks : AT(ADDR(.smp_locks) - LOAD_OFFSET) {
	*(.smp_locks)
  }
  __smp_locks_end = .;
  . = ALIGN(4096);
  __smp_alt_end = .;

  . = ALIGN(4096);		/* Init code and data */
  __init_begin = .;
  .init.text : AT(ADDR(.init.text) - LOAD_OFFSET) {
	_sinittext = .;
	INIT_TEXT
	_einittext = .;
  }
  .init.data : AT(ADDR(.init.data) - LOAD_OFFSET) {
	__initdata_begin = .;
	INIT_DATA
	__initdata_end = .;
   }

  . = ALIGN(16);
  __setup_start = .;
  .init.setup : AT(ADDR(.init.setup) - LOAD_OFFSET) { *(.init.setup) }
  __setup_end = .;
  __initcall_start = .;
  .initcall.init : AT(ADDR(.initcall.init) - LOAD_OFFSET) {
	INITCALLS
  }
  __initcall_end = .;
  __con_initcall_start = .;
  .con_initcall.init : AT(ADDR(.con_initcall.init) - LOAD_OFFSET) {
	*(.con_initcall.init)
  }
  __con_initcall_end = .;
  SECURITY_INIT

  . = ALIGN(8);
  .parainstructions : AT(ADDR(.parainstructions) - LOAD_OFFSET) {
  __parainstructions = .;
       *(.parainstructions)
  __parainstructions_end = .;
  }

  . = ALIGN(8);
  __alt_instructions = .;
  .altinstructions : AT(ADDR(.altinstructions) - LOAD_OFFSET) {
	*(.altinstructions)
  }
  __alt_instructions_end = .; 
  .altinstr_replacement : AT(ADDR(.altinstr_replacement) - LOAD_OFFSET) {
	*(.altinstr_replacement)
  }
  /* .exit.text is discard at runtime, not link time, to deal with references
     from .altinstructions and .eh_frame */
  .exit.text : AT(ADDR(.exit.text) - LOAD_OFFSET) {
	EXIT_TEXT
  }
  .exit.data : AT(ADDR(.exit.data) - LOAD_OFFSET) {
	EXIT_DATA
  }

/* vdso blob that is mapped into user space */
  vdso_start = . ;
  .vdso  : AT(ADDR(.vdso) - LOAD_OFFSET) { *(.vdso) }
  . = ALIGN(4096);
  vdso_end = .;

#ifdef CONFIG_BLK_DEV_INITRD
  . = ALIGN(4096);
  __initramfs_start = .;
  .init.ramfs : AT(ADDR(.init.ramfs) - LOAD_OFFSET) { *(.init.ramfs) }
  __initramfs_end = .;
#endif

  PERCPU(4096)

  . = ALIGN(4096);
  __init_end = .;

  . = ALIGN(4096);
  __nosave_begin = .;
  .data_nosave : AT(ADDR(.data_nosave) - LOAD_OFFSET) { *(.data.nosave) }
  . = ALIGN(4096);
  __nosave_end = .;

  __bss_start = .;		/* BSS */
  .bss : AT(ADDR(.bss) - LOAD_OFFSET) {
	*(.bss.page_aligned)
	*(.bss)
	}
  __bss_stop = .;

  _end = . ;

  /* Sections to be discarded */
  /DISCARD/ : {
	*(.exitcall.exit)
	*(.eh_frame)
	}

  STABS_DEBUG

  DWARF_DEBUG
}
