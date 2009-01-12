/* ld script for sparc32/sparc64 kernel */

#include <asm-generic/vmlinux.lds.h>

#include <asm/page.h>
#include <asm/thread_info.h>

#ifdef CONFIG_SPARC32
#define INITIAL_ADDRESS  0x10000 + SIZEOF_HEADERS
#define TEXTSTART	0xf0004000

#define SMP_CACHE_BYTES_SHIFT 5

#else
#define SMP_CACHE_BYTES_SHIFT 6
#define INITIAL_ADDRESS 0x4000
#define TEXTSTART      0x0000000000404000

#endif

#define SMP_CACHE_BYTES (1 << SMP_CACHE_BYTES_SHIFT)

#ifdef CONFIG_SPARC32
OUTPUT_FORMAT("elf32-sparc", "elf32-sparc", "elf32-sparc")
OUTPUT_ARCH(sparc)
ENTRY(_start)
jiffies = jiffies_64 + 4;
#else
/* sparc64 */
OUTPUT_FORMAT("elf64-sparc", "elf64-sparc", "elf64-sparc")
OUTPUT_ARCH(sparc:v9a)
ENTRY(_start)
jiffies = jiffies_64;
#endif

SECTIONS
{
	/* swapper_low_pmd_dir is sparc64 only */
	swapper_low_pmd_dir = 0x0000000000402000;
	. = INITIAL_ADDRESS;
	.text TEXTSTART :
	{
		_text = .;
		*(.text.head)
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		*(.gnu.warning)
	} = 0
	_etext = .;

	RO_DATA(PAGE_SIZE)
	.data :	{
		DATA_DATA
		CONSTRUCTORS
	}
	.data1 : {
		*(.data1)
	}
	. = ALIGN(SMP_CACHE_BYTES);
	.data.cacheline_aligned : {
		*(.data.cacheline_aligned)
	}
	. = ALIGN(SMP_CACHE_BYTES);
	.data.read_mostly : {
		*(.data.read_mostly)
	}
	/* End of data section */
	_edata = .;

	/* init_task */
	. = ALIGN(THREAD_SIZE);
	.data.init_task : {
		*(.data.init_task)
	}
	.fixup : {
		__start___fixup = .;
		*(.fixup)
		__stop___fixup = .;
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
	__init_text_end = .;
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

	/DISCARD/ : {
		EXIT_TEXT
		EXIT_DATA
		*(.exitcall.exit)
	}

	STABS_DEBUG
	DWARF_DEBUG
}
