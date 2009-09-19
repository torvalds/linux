/*
 * arch/score/kernel/vmlinux.lds.S
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <asm-generic/vmlinux.lds.h>

OUTPUT_ARCH(score)
ENTRY(_stext)

jiffies = jiffies_64;

SECTIONS
{
	. = CONFIG_MEMORY_START + 0x2000;
	/* read-only */
	.text : {
		_text = .;	/* Text and read-only data */
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		*(.text.*)
		*(.fixup)
		. = ALIGN (4) ;
		_etext = .;	/* End of text section */
	}

	. = ALIGN(16);
	RODATA

	/* Exception table */
	. = ALIGN(16);
	__ex_table : {
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;
	}

	/* writeable */
	.data ALIGN (4096): {
		*(.data.init_task)

		DATA_DATA
		CONSTRUCTORS
	}

	/* We want the small data sections together, so single-instruction offsets
	   can access them all, and initialized data all before uninitialized, so
	   we can shorten the on-disk segment size.  */
	. = ALIGN(8);
	.sdata : {
		*(.sdata)
	}

	. = ALIGN(32);
	.data.cacheline_aligned : {
		*(.data.cacheline_aligned)
	}
	_edata =  .;			/* End of data section */

	/* will be freed after init */
	. = ALIGN(4096);		/* Init code and data */
	__init_begin = .;

	. = ALIGN(4096);
	.init.text : {
		_sinittext = .;
		INIT_TEXT
		_einittext = .;
	}
	.init.data : {
		INIT_DATA
	}
	. = ALIGN(16);
	.init.setup : {
		__setup_start = .;
		*(.init.setup)
		__setup_end = .;
	}

	.initcall.init : {
		__initcall_start = .;
		INITCALLS
		__initcall_end = .;
	}

	.con_initcall.init : {
		__con_initcall_start = .;
		*(.con_initcall.init)
		__con_initcall_end = .;
	}
	SECURITY_INIT

	/* .exit.text is discarded at runtime, not link time, to deal with
	 * references from .rodata
	 */
	.exit.text : {
		EXIT_TEXT
	}
	.exit.data : {
		EXIT_DATA
	}
#if defined(CONFIG_BLK_DEV_INITRD)
	.init.ramfs ALIGN(4096): {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
		. = ALIGN(4);
		LONG(0);
	}
#endif
	. = ALIGN(4096);
	__init_end = .;
	/* freed after init ends here */

	__bss_start = .;	/* BSS */
	.sbss  : {
		*(.sbss)
		*(.scommon)
	}
	.bss : {
		*(.bss)
		*(COMMON)
	}
	__bss_stop = .;
	_end = .;
}
