#define VMLINUX_SYMBOL(_sym_) _##_sym_
#include <asm-generic/vmlinux.lds.h>
#include <linux/config.h>

/* target memory map */
#ifdef CONFIG_H8300H_GENERIC
#define ROMTOP  0x000000
#define ROMSIZE 0x400000
#define RAMTOP  0x400000
#define RAMSIZE 0x400000
#endif

#ifdef CONFIG_H8300H_AKI3068NET
#define ROMTOP  0x000000
#define ROMSIZE 0x080000
#define RAMTOP  0x400000
#define RAMSIZE 0x200000
#endif

#ifdef CONFIG_H8300H_H8MAX
#define ROMTOP  0x000000
#define ROMSIZE 0x080000
#define RAMTOP  0x400000
#define RAMSIZE 0x200000
#endif

#ifdef CONFIG_H8300H_SIM
#define ROMTOP  0x000000
#define ROMSIZE 0x400000
#define RAMTOP  0x400000
#define RAMSIZE 0x400000
#endif

#ifdef CONFIG_H8S_SIM
#define ROMTOP  0x000000
#define ROMSIZE 0x400000
#define RAMTOP  0x400000
#define RAMSIZE 0x800000
#endif

#ifdef CONFIG_H8S_EDOSK2674
#define ROMTOP  0x000000
#define ROMSIZE 0x400000
#define RAMTOP  0x400000
#define RAMSIZE 0x800000
#endif

#if defined(CONFIG_H8300H_SIM) || defined(CONFIG_H8S_SIM)
INPUT(romfs.o)
#endif

_jiffies = _jiffies_64 + 4;

ENTRY(__start)

SECTIONS
{
#if defined(CONFIG_ROMKERNEL)
	. = ROMTOP; 
	.vectors :
	{
	__vector = . ;
		*(.vectors*)
	}
#else
	. = RAMTOP; 
	.bootvec :	
	{
		*(.bootvec)
	}
#endif
        .text :
	{
#if defined(CONFIG_ROMKERNEL)
	*(.int_redirect)
#endif
	__stext = . ;
        	*(.text)
	SCHED_TEXT
	LOCK_TEXT
	__etext = . ;
	. = ALIGN(16);          /* Exception table              */
	___start___ex_table = .;
		*(__ex_table)
	___stop___ex_table = .;
	}

	RODATA
#if defined(CONFIG_ROMKERNEL)
	SECURITY_INIT
#endif
	ROEND = .; 
#if defined(CONFIG_ROMKERNEL)
	. = RAMTOP;
	.data :	AT(ROEND)
#else
	.data :	
#endif
	{
	__sdata = . ;
	___data_start = . ;

	. = ALIGN(0x2000) ;
		*(.data.init_task)
	. = ALIGN(0x4) ;
		*(.data)
	. = ALIGN(0x4) ;
		*(.data.*)	

	. = ALIGN(0x4) ;
	___init_begin = .;
	__sinittext = .; 
		*(.init.text)
	__einittext = .; 
		*(.init.data)
	. = ALIGN(0x4) ;
	___setup_start = .;
		*(.init.setup)
	. = ALIGN(0x4) ;
	___setup_end = .;
	___initcall_start = .;
		*(.initcall1.init)
		*(.initcall2.init)
		*(.initcall3.init)
		*(.initcall4.init)
		*(.initcall5.init)
		*(.initcall6.init)
		*(.initcall7.init)
	___initcall_end = .;
	___con_initcall_start = .;
		*(.con_initcall.init)
	___con_initcall_end = .;
		*(.exit.text)
		*(.exit.data)
		. = ALIGN(4);
	___initramfs_start = .;
  		*(.init.ramfs)
  	___initramfs_end = .;
	. = ALIGN(0x4) ;
	___init_end = .;
	__edata = . ;
	}
#if defined(CONFIG_RAMKERNEL)
	SECURITY_INIT
#endif
	__begin_data = LOADADDR(.data);
        .bss : 
        {
	. = ALIGN(0x4) ;
	__sbss = . ;
		*(.bss*)
	. = ALIGN(0x4) ;
		*(COMMON)
	. = ALIGN(0x4) ;
	__ebss = . ;
	__end = . ;
	__ramstart = .;
	}
	/DISCARD/ : {
		*(.exitcall.exit)
	}
        .romfs :	
	{
		*(.romfs*)
	}
	. = RAMTOP+RAMSIZE;
        .dummy :
        {
	COMMAND_START = . - 0x200 ;
	__ramend = . ;
	}
}
