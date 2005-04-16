/* ld script to make x86-64 Linux kernel
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>;
 */

#include <asm-generic/vmlinux.lds.h>
#include <linux/config.h>

OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(phys_startup_64)
jiffies_64 = jiffies;
SECTIONS
{
  . = 0xffffffff80100000;
  phys_startup_64 = startup_64 - LOAD_OFFSET;
  _text = .;			/* Text and read-only data */
  .text : {
	*(.text)
	SCHED_TEXT
	LOCK_TEXT
	*(.fixup)
	*(.gnu.warning)
	} = 0x9090
  .text.lock : { *(.text.lock) }	/* out-of-line lock text */

  _etext = .;			/* End of text section */

  . = ALIGN(16);		/* Exception table */
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  RODATA

  .data : {			/* Data */
	*(.data)
	CONSTRUCTORS
	}

  _edata = .;			/* End of data section */

  __bss_start = .;		/* BSS */
  .bss : {
	*(.bss.page_aligned)	
	*(.bss)
	}
  __bss_end = .;

  . = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

#define AFTER(x)      BINALIGN(LOADADDR(x) + SIZEOF(x), 16)
#define BINALIGN(x,y) (((x) + (y) - 1)  & ~((y) - 1))
#define CACHE_ALIGN(x) BINALIGN(x, CONFIG_X86_L1_CACHE_BYTES)

  .vsyscall_0 -10*1024*1024: AT ((LOADADDR(.data.cacheline_aligned) + SIZEOF(.data.cacheline_aligned) + 4095) & ~(4095)) { *(.vsyscall_0) }
  __vsyscall_0 = LOADADDR(.vsyscall_0);
  . = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
  .xtime_lock : AT CACHE_ALIGN(AFTER(.vsyscall_0)) { *(.xtime_lock) }
  xtime_lock = LOADADDR(.xtime_lock);
  .vxtime : AT AFTER(.xtime_lock) { *(.vxtime) }
  vxtime = LOADADDR(.vxtime);
  .wall_jiffies : AT AFTER(.vxtime) { *(.wall_jiffies) }
  wall_jiffies = LOADADDR(.wall_jiffies);
  .sys_tz : AT AFTER(.wall_jiffies) { *(.sys_tz) }
  sys_tz = LOADADDR(.sys_tz);
  .sysctl_vsyscall : AT AFTER(.sys_tz) { *(.sysctl_vsyscall) }
  sysctl_vsyscall = LOADADDR(.sysctl_vsyscall); 
  .xtime : AT AFTER(.sysctl_vsyscall) { *(.xtime) }
  xtime = LOADADDR(.xtime);
  . = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
  .jiffies : AT CACHE_ALIGN(AFTER(.xtime)) { *(.jiffies) }
  jiffies = LOADADDR(.jiffies);
  .vsyscall_1 ADDR(.vsyscall_0) + 1024: AT (LOADADDR(.vsyscall_0) + 1024) { *(.vsyscall_1) }
  . = LOADADDR(.vsyscall_0) + 4096;

  . = ALIGN(8192);		/* init_task */
  .data.init_task : { *(.data.init_task) }

  . = ALIGN(4096);
  .data.page_aligned : { *(.data.page_aligned) }

  . = ALIGN(4096);		/* Init code and data */
  __init_begin = .;
  .init.text : { 
	_sinittext = .;
	*(.init.text)
	_einittext = .;
  }
  __initdata_begin = .;
  .init.data : { *(.init.data) }
  __initdata_end = .;
  . = ALIGN(16);
  __setup_start = .;
  .init.setup : { *(.init.setup) }
  __setup_end = .;
  __initcall_start = .;
  .initcall.init : {
	*(.initcall1.init) 
	*(.initcall2.init) 
	*(.initcall3.init) 
	*(.initcall4.init) 
	*(.initcall5.init) 
	*(.initcall6.init) 
	*(.initcall7.init)
  }
  __initcall_end = .;
  __con_initcall_start = .;
  .con_initcall.init : { *(.con_initcall.init) }
  __con_initcall_end = .;
  SECURITY_INIT
  . = ALIGN(8);
  __alt_instructions = .;
  .altinstructions : { *(.altinstructions) } 
  __alt_instructions_end = .; 
 .altinstr_replacement : { *(.altinstr_replacement) }
  /* .exit.text is discard at runtime, not link time, to deal with references
     from .altinstructions and .eh_frame */
  .exit.text : { *(.exit.text) }
  .exit.data : { *(.exit.data) }	
  . = ALIGN(4096);
  __initramfs_start = .;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;	
  . = ALIGN(32);
  __per_cpu_start = .;
  .data.percpu  : { *(.data.percpu) }
  __per_cpu_end = .;
  . = ALIGN(4096);
  __init_end = .;

  . = ALIGN(4096);
  __nosave_begin = .;
  .data_nosave : { *(.data.nosave) }
  . = ALIGN(4096);
  __nosave_end = .;

  _end = . ;

  /* Sections to be discarded */
  /DISCARD/ : {
	*(.exitcall.exit)
#ifndef CONFIG_DEBUG_INFO
	*(.eh_frame)
#endif
	}

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


  .comment 0 : { *(.comment) }
}
