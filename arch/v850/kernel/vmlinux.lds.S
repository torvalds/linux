/*
 * arch/v850/vmlinux.lds.S -- kernel linker script for v850 platforms
 *
 *  Copyright (C) 2002,03,04,05  NEC Electronics Corporation
 *  Copyright (C) 2002,03,04,05  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */


#define VMLINUX_SYMBOL(_sym_) _##_sym_
#include <asm-generic/vmlinux.lds.h>

/* For most platforms, this will define useful things like RAM addr/size.  */
#include <asm/machdep.h>


/* The following macros contain the usual definitions for various data areas.
   The prefix `RAMK_' is used to indicate macros suitable for kernels loaded
   into RAM, and similarly `ROMK_' for ROM-resident kernels.  Note that all
   symbols are prefixed with an extra `_' for compatibility with the v850
   toolchain.  */

	
/* Interrupt vectors.  */
#define INTV_CONTENTS							      \
		. = ALIGN (0x10) ;					      \
		__intv_start = . ;					      \
			*(.intv.reset)	/* Reset vector */		      \
		. = __intv_start + 0x10 ;				      \
			*(.intv.common)	/* Vectors common to all v850e proc */\
		. = __intv_start + 0x80 ;				      \
			*(.intv.mach)	/* Machine-specific int. vectors.  */ \
		__intv_end = . ;

#define RODATA_CONTENTS							      \
		. = ALIGN (16) ;					      \
			*(.rodata) *(.rodata.*)				      \
			*(__vermagic)		/* Kernel version magic */    \
			*(.rodata1)					      \
		/* PCI quirks */					      \
		___start_pci_fixups_early = . ;				      \
			*(.pci_fixup_early)				      \
		___end_pci_fixups_early = . ;				      \
		___start_pci_fixups_header = . ;			      \
			*(.pci_fixup_header)				      \
		___end_pci_fixups_header = . ;				      \
		___start_pci_fixups_final = . ;				      \
			*(.pci_fixup_final)				      \
		___end_pci_fixups_final = . ;				      \
		___start_pci_fixups_enable = . ;			      \
			*(.pci_fixup_enable)				      \
		___end_pci_fixups_enable = . ;				      \
		/* Kernel symbol table: Normal symbols */		      \
		___start___ksymtab = .;					      \
			*(__ksymtab)					      \
		___stop___ksymtab = .;					      \
		/* Kernel symbol table: GPL-only symbols */		      \
		___start___ksymtab_gpl = .;				      \
			*(__ksymtab_gpl)				      \
		___stop___ksymtab_gpl = .;				      \
		/* Kernel symbol table: GPL-future symbols */		      \
		___start___ksymtab_gpl_future = .;			      \
			*(__ksymtab_gpl_future)				      \
		___stop___ksymtab_gpl_future = .;			      \
		/* Kernel symbol table: strings */			      \
			*(__ksymtab_strings)				      \
		/* Kernel symbol table: Normal symbols */		      \
		___start___kcrctab = .;					      \
			*(__kcrctab)					      \
		___stop___kcrctab = .;					      \
		/* Kernel symbol table: GPL-only symbols */		      \
		___start___kcrctab_gpl = .;				      \
			*(__kcrctab_gpl)				      \
		___stop___kcrctab_gpl = .;				      \
		/* Kernel symbol table: GPL-future symbols */		      \
		___start___kcrctab_gpl_future = .;			      \
			*(__kcrctab_gpl_future)				      \
		___stop___kcrctab_gpl_future = .;			      \
		/* Built-in module parameters */			      \
		. = ALIGN (4) ;						      \
		___start___param = .;					      \
		*(__param)						      \
		___stop___param = .;


/* Kernel text segment, and some constant data areas.  */
#define TEXT_CONTENTS							      \
		_text = .;						      \
		__stext = . ;						      \
		TEXT_TEXT						      \
		SCHED_TEXT						      \
			*(.exit.text)	/* 2.5 convention */		      \
			*(.text.exit)	/* 2.4 convention */		      \
			*(.text.lock)					      \
			*(.exitcall.exit)				      \
		__real_etext = . ;	/* There may be data after here.  */  \
		RODATA_CONTENTS						      \
		. = ALIGN (4) ;						      \
		    	*(.call_table_data)				      \
			*(.call_table_text)				      \
		. = ALIGN (16) ;	/* Exception table.  */		      \
		___start___ex_table = . ;				      \
			*(__ex_table)					      \
		___stop___ex_table = . ;				      \
		. = ALIGN (4) ;						      \
		__etext = . ;

/* Kernel data segment.  */
#define DATA_CONTENTS							      \
		__sdata = . ;						      \
		DATA_DATA						      \
			EXIT_DATA	/* 2.5 convention */		      \
			*(.data.exit)	/* 2.4 convention */		      \
		. = ALIGN (16) ;					      \
		*(.data.cacheline_aligned)				      \
		. = ALIGN (0x2000) ;					      \
        	*(.data.init_task)					      \
		. = ALIGN (0x2000) ;					      \
		__edata = . ;

/* Kernel BSS segment.  */
#define BSS_CONTENTS							      \
		__sbss = . ;						      \
			*(.bss)						      \
			*(COMMON)					      \
		. = ALIGN (4) ;						      \
		__init_stack_end = . ;					      \
		__ebss = . ;

/* `initcall' tables.  */
#define INITCALL_CONTENTS						      \
		. = ALIGN (16) ;					      \
		___setup_start = . ;					      \
			*(.init.setup)	/* 2.5 convention */		      \
			*(.setup.init)	/* 2.4 convention */		      \
		___setup_end = . ;					      \
		___initcall_start = . ;					      \
			*(.initcall.init)				      \
			INITCALLS					      \
		. = ALIGN (4) ;						      \
		___initcall_end = . ;					      \
		___con_initcall_start = .;				      \
			*(.con_initcall.init)				      \
		___con_initcall_end = .;

/* Contents of `init' section for a kernel that's loaded into RAM.  */
#define RAMK_INIT_CONTENTS						      \
		RAMK_INIT_CONTENTS_NO_END				      \
		__init_end = . ;
/* Same as RAMK_INIT_CONTENTS, but doesn't define the `__init_end' symbol.  */
#define RAMK_INIT_CONTENTS_NO_END					      \
		. = ALIGN (4096) ;					      \
		__init_start = . ;					      \
			__sinittext = .;				      \
			INIT_TEXT	/* 2.5 convention */		      \
			__einittext = .;				      \
			INIT_DATA					      \
			*(.text.init)	/* 2.4 convention */		      \
			*(.data.init)					      \
		INITCALL_CONTENTS					      \
		INITRAMFS_CONTENTS

/* The contents of `init' section for a ROM-resident kernel which
   should go into RAM.  */	
#define ROMK_INIT_RAM_CONTENTS						      \
		. = ALIGN (4096) ;					      \
		__init_start = . ;					      \
			INIT_DATA	/* 2.5 convention */		      \
			*(.data.init)	/* 2.4 convention */		      \
		__init_end = . ;					      \
		. = ALIGN (4096) ;

/* The contents of `init' section for a ROM-resident kernel which
   should go into ROM.  */	
#define ROMK_INIT_ROM_CONTENTS						      \
			_sinittext = .;					      \
			INIT_TEXT	/* 2.5 convention */		      \
			_einittext = .;					      \
			*(.text.init)	/* 2.4 convention */		      \
		INITCALL_CONTENTS					      \
		INITRAMFS_CONTENTS

/* A root filesystem image, for kernels with an embedded root filesystem.  */
#define ROOT_FS_CONTENTS						      \
		__root_fs_image_start = . ;				      \
		*(.root)						      \
		__root_fs_image_end = . ;

#ifdef CONFIG_BLK_DEV_INITRD
/* The initramfs archive.  */
#define INITRAMFS_CONTENTS						      \
		. = ALIGN (4) ;						      \
		___initramfs_start = . ;				      \
			*(.init.ramfs)					      \
		___initramfs_end = . ;
#endif

/* Where the initial bootmap (bitmap for the boot-time memory allocator) 
   should be place.  */
#define BOOTMAP_CONTENTS						      \
		. = ALIGN (4096) ;					      \
		__bootmap = . ;						      \
		. = . + 4096 ;		/* enough for 128MB.   */

/* The contents of a `typical' kram area for a kernel in RAM.  */
#define RAMK_KRAM_CONTENTS						      \
		__kram_start = . ;					      \
		TEXT_CONTENTS						      \
		DATA_CONTENTS						      \
		BSS_CONTENTS						      \
		RAMK_INIT_CONTENTS					      \
		__kram_end = . ;					      \
		BOOTMAP_CONTENTS


/* Define output sections normally used for a ROM-resident kernel.  
   ROM and RAM should be appropriate memory areas to use for kernel
   ROM and RAM data.  This assumes that ROM starts at 0 (and thus can
   hold the interrupt vectors).  */
#define ROMK_SECTIONS(ROM, RAM)						      \
	.rom : {							      \
		INTV_CONTENTS						      \
		TEXT_CONTENTS						      \
		ROMK_INIT_ROM_CONTENTS					      \
		ROOT_FS_CONTENTS					      \
	} > ROM								      \
									      \
	__rom_copy_src_start = . ;					      \
									      \
	.data : {							      \
		__kram_start = . ;					      \
		__rom_copy_dst_start = . ;				      \
		DATA_CONTENTS						      \
		ROMK_INIT_RAM_CONTENTS					      \
		__rom_copy_dst_end = . ;				      \
	} > RAM  AT> ROM						      \
									      \
	.bss ALIGN (4) : {						      \
		BSS_CONTENTS						      \
		__kram_end = . ;					      \
		BOOTMAP_CONTENTS					      \
	} > RAM


/* The 32-bit variable `jiffies' is just the lower 32-bits of `jiffies_64'.  */
_jiffies = _jiffies_64 ;


/* Include an appropriate platform-dependent linker-script (which
   usually should use the above macros to do most of the work).  */

#ifdef CONFIG_V850E_SIM
# include "sim.ld"
#endif

#ifdef CONFIG_V850E2_SIM85E2
# include "sim85e2.ld"
#endif

#ifdef CONFIG_V850E2_FPGA85E2C
# include "fpga85e2c.ld"
#endif

#ifdef CONFIG_V850E2_ANNA
# ifdef CONFIG_ROM_KERNEL
#  include "anna-rom.ld"
# else
#  include "anna.ld"
# endif
#endif

#ifdef CONFIG_V850E_AS85EP1
# ifdef CONFIG_ROM_KERNEL
#  include "as85ep1-rom.ld"
# else
#  include "as85ep1.ld"
# endif
#endif

#ifdef CONFIG_RTE_CB_MA1
# ifdef CONFIG_ROM_KERNEL
#  include "rte_ma1_cb-rom.ld"
# else
#  include "rte_ma1_cb.ld"
# endif
#endif

#ifdef CONFIG_RTE_CB_NB85E
# ifdef CONFIG_ROM_KERNEL
#  include "rte_nb85e_cb-rom.ld"
# elif defined(CONFIG_RTE_CB_MULTI)
#  include "rte_nb85e_cb-multi.ld"
# else
#  include "rte_nb85e_cb.ld"
# endif
#endif

#ifdef CONFIG_RTE_CB_ME2
#  include "rte_me2_cb.ld"
#endif

