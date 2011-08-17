/* ld script to make the Linux/CRIS kernel
 * Authors: Bjorn Wesen (bjornw@axis.com)
 *
 * It is VERY DANGEROUS to fiddle around with the symbols in this
 * script. It is for example quite vital that all generated sections
 * that are used are actually named here, otherwise the linker will
 * put them at the end, where the init stuff is which is FREED after
 * the kernel has booted.
 */

#include <asm-generic/vmlinux.lds.h>
#include <asm/page.h>

#ifdef CONFIG_ETRAX_VMEM_SIZE
#define __CONFIG_ETRAX_VMEM_SIZE CONFIG_ETRAX_VMEM_SIZE
#else
#define __CONFIG_ETRAX_VMEM_SIZE 0
#endif


jiffies = jiffies_64;
SECTIONS
{
	. = DRAM_VIRTUAL_BASE;
	dram_start = .;
#ifdef CONFIG_ETRAX_ARCH_V10
	ibr_start = .;
#else
	ebp_start = .;
	/* The boot section is only necessary until the VCS top */
	/* level testbench includes both flash and DRAM. */
	.boot : { *(.boot) }
#endif

	/* see head.S and pages reserved at the start */
	. = DRAM_VIRTUAL_BASE + 0x4000;

	_text = .;			/* Text and read-only data. */
	text_start = .;			/* Lots of aliases. */
	_stext = .;
	__stext = .;
	.text : {
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		*(.fixup)
		*(.text.__*)
	}

	_etext = . ;			/* End of text section. */
	__etext = .;

	EXCEPTION_TABLE(4)

	RODATA

	. = ALIGN (4);
	___data_start = . ;
	__Sdata = . ;
	.data : {			/* Data */
		CACHELINE_ALIGNED_DATA(32)
		READ_MOSTLY_DATA(32)
		DATA_DATA
	}
	__edata = . ;			/* End of data section. */
	_edata = . ;

	INIT_TASK_DATA_SECTION(PAGE_SIZE)

	. = ALIGN(PAGE_SIZE);		/* Init code and data. */
	__init_begin = .;
	INIT_TEXT_SECTION(PAGE_SIZE)
	.init.data : { INIT_DATA }
	.init.setup : { INIT_SETUP(16) }
	.initcall.init : {
		INIT_CALLS
	}

	.con_initcall.init : {
		CON_INITCALL
	}
	SECURITY_INIT

	/* .exit.text is discarded at runtime, not link time,
	 * to deal with references from __bug_table
	 */
	.exit.text : {
		EXIT_TEXT
	}
	.exit.data : {
		EXIT_DATA
	}

#ifdef CONFIG_ETRAX_ARCH_V10
#ifdef CONFIG_BLK_DEV_INITRD
	.init.ramfs : {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
	}
#endif
#endif
	__vmlinux_end = .;		/* Last address of the physical file. */
#ifdef CONFIG_ETRAX_ARCH_V32
	PERCPU_SECTION(32)

	.init.ramfs : {
		INIT_RAM_FS
	}
#endif

	/*
	 * We fill to the next page, so we can discard all init
	 * pages without needing to consider what payload might be
	 * appended to the kernel image.
	 */
	. = ALIGN(PAGE_SIZE);

	__init_end = .;

	__data_end = . ;		/* Move to _edata ? */
	BSS_SECTION(1, 1, 1)

	. =  ALIGN (0x20);
	_end = .;
	__end = .;

	dram_end = dram_start + (CONFIG_ETRAX_DRAM_SIZE - __CONFIG_ETRAX_VMEM_SIZE)*1024*1024;

	DISCARDS
}
