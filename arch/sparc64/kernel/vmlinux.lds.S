/* ld script to make UltraLinux kernel */

#include <asm/page.h>
#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT("elf64-sparc", "elf64-sparc", "elf64-sparc")
OUTPUT_ARCH(sparc:v9a)
ENTRY(_start)

jiffies = jiffies_64;
SECTIONS
{
	swapper_low_pmd_dir = 0x0000000000402000;
	. = 0x4000;
	.text 0x0000000000404000 : {
		_text = .;
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		*(.gnu.warning)
	} = 0
	_etext = .;
	PROVIDE (etext = .);

	RO_DATA(PAGE_SIZE)
	.data : {
		DATA_DATA
		CONSTRUCTORS
	}
	.data1 : {
		*(.data1)
	}
	. = ALIGN(64);
	.data.cacheline_aligned : {
		*(.data.cacheline_aligned)
	}
	. = ALIGN(64);
	.data.read_mostly : {
		*(.data.read_mostly)
	}
	_edata = .;
	PROVIDE (edata = .);
	.fixup : {
		*(.fixup)
	}
	. = ALIGN(16);
	__ex_table : {
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;
	}
	NOTES

	. = ALIGN(PAGE_SIZE);
	.init.text : {
		__init_begin = .;
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

	. = ALIGN(4);
	.tsb_ldquad_phys_patch : {
		__tsb_ldquad_phys_patch = .;
		*(.tsb_ldquad_phys_patch)
		__tsb_ldquad_phys_patch_end = .;
	}

	.tsb_phys_patch : {
		__tsb_phys_patch = .;
		*(.tsb_phys_patch)
		__tsb_phys_patch_end = .;
	}

	.cpuid_patch : {
		__cpuid_patch = .;
		*(.cpuid_patch)
		__cpuid_patch_end = .;
	}

	.sun4v_1insn_patch : {
		__sun4v_1insn_patch = .;
		*(.sun4v_1insn_patch)
		__sun4v_1insn_patch_end = .;
	}
	.sun4v_2insn_patch : {
		__sun4v_2insn_patch = .;
		*(.sun4v_2insn_patch)
		__sun4v_2insn_patch_end = .;
	}

#ifdef CONFIG_BLK_DEV_INITRD
	. = ALIGN(PAGE_SIZE);
	.init.ramfs : {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
	}
#endif

	PERCPU(PAGE_SIZE)

	. = ALIGN(PAGE_SIZE);
	__init_end = .;
	__bss_start = .;
	.sbss : {
		*(.sbss)
		*(.scommon)
	}
	.bss : {
		*(.dynbss)
		*(.bss)
		*(COMMON)
	}
	_end = . ;
	PROVIDE (end = .);

	/DISCARD/ : {
		EXIT_TEXT
		EXIT_DATA
		*(.exitcall.exit)
	}

	STABS_DEBUG
	DWARF_DEBUG
}
