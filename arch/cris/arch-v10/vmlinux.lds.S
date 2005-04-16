/* ld script to make the Linux/CRIS kernel
 * Authors: Bjorn Wesen (bjornw@axis.com)
 *
 * It is VERY DANGEROUS to fiddle around with the symbols in this
 * script. It is for example quite vital that all generated sections
 * that are used are actually named here, otherwise the linker will
 * put them at the end, where the init stuff is which is FREED after
 * the kernel has booted. 
 */	

#include <linux/config.h>
#include <asm-generic/vmlinux.lds.h>
		
jiffies = jiffies_64;
SECTIONS
{
	. = DRAM_VIRTUAL_BASE;
	dram_start = .;
	ibr_start = .;
	. = . + 0x4000; /* see head.S and pages reserved at the start */

	_text = .;                    /* Text and read-only data */
	text_start = .;              /* lots of aliases */
	_stext = .;
	__stext = .;
	.text : {
		*(.text)
		SCHED_TEXT
		LOCK_TEXT
		*(.fixup)
		*(.text.__*)
	}

	_etext = . ;                  /* End of text section */ 
	__etext = .;

	. = ALIGN(4);                /* Exception table */
  	__start___ex_table = .;
  	__ex_table : { *(__ex_table) }
  	__stop___ex_table = .;

	RODATA

	. = ALIGN (4);
	___data_start = . ;
	__Sdata = . ;
	.data : {                     /* Data */
		*(.data)
	}
	__edata = . ;                 /* End of data section */
	_edata = . ;

	. = ALIGN(8192);              /* init_task and stack, must be aligned */
  	.data.init_task : { *(.data.init_task) }

  	. = ALIGN(8192);              /* Init code and data */
  	__init_begin = .;
	.init.text : { 
		   _sinittext = .;
		   *(.init.text)
		   _einittext = .;
	}
  	.init.data : { *(.init.data) }
  	. = ALIGN(16);
  	__setup_start = .;
  	.init.setup : { *(.init.setup) }
  	__setup_end = .;
  	.initcall.init : {
		__initcall_start = .;
		*(.initcall1.init);
		*(.initcall2.init);
		*(.initcall3.init);
		*(.initcall4.init);
		*(.initcall5.init);
		*(.initcall6.init);
		*(.initcall7.init);
		__initcall_end = .;	
	}

	.con_initcall.init : {
		__con_initcall_start = .;
		*(.con_initcall.init)
		__con_initcall_end = .;
	}	
	SECURITY_INIT
		
	.init.ramfs : {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
		/* We fill to the next page, so we can discard all init
		   pages without needing to consider what payload might be
		   appended to the kernel image.  */
		FILL (0); 
		. = ALIGN (8192);
	}
	
	__vmlinux_end = .;            /* last address of the physical file */
  	__init_end = .;

	__data_end = . ;              /* Move to _edata ? */
	__bss_start = .;              /* BSS */
	.bss : {
		*(COMMON)
		*(.bss)
	}

	. =  ALIGN (0x20);
	_end = .;
	__end = .;

	/* Sections to be discarded */
  	/DISCARD/ : {
        	*(.text.exit)
        	*(.data.exit)
		*(.exitcall.exit)
        }

	dram_end = dram_start + CONFIG_ETRAX_DRAM_SIZE*1024*1024;
}
