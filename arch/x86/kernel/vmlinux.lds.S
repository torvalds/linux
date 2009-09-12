/*
 * ld script for the x86 kernel
 *
 * Historic 32-bit version written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 * Modernisation, unification and other changes and fixes:
 *   Copyright (C) 2007-2009  Sam Ravnborg <sam@ravnborg.org>
 *
 *
 * Don't define absolute symbols until and unless you know that symbol
 * value is should remain constant even if kernel image is relocated
 * at run time. Absolute symbols are not relocated. If symbol value should
 * change if kernel is relocated, make the symbol section relative and
 * put it inside the section definition.
 */

#ifdef CONFIG_X86_32
#define LOAD_OFFSET __PAGE_OFFSET
#else
#define LOAD_OFFSET __START_KERNEL_map
#endif

#include <asm-generic/vmlinux.lds.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/page_types.h>
#include <asm/cache.h>
#include <asm/boot.h>

#undef i386     /* in case the preprocessor is a 32bit one */

OUTPUT_FORMAT(CONFIG_OUTPUT_FORMAT, CONFIG_OUTPUT_FORMAT, CONFIG_OUTPUT_FORMAT)

#ifdef CONFIG_X86_32
OUTPUT_ARCH(i386)
ENTRY(phys_startup_32)
jiffies = jiffies_64;
#else
OUTPUT_ARCH(i386:x86-64)
ENTRY(phys_startup_64)
jiffies_64 = jiffies;
#endif

PHDRS {
	text PT_LOAD FLAGS(5);          /* R_E */
	data PT_LOAD FLAGS(7);          /* RWE */
#ifdef CONFIG_X86_64
	user PT_LOAD FLAGS(7);          /* RWE */
	data.init PT_LOAD FLAGS(7);     /* RWE */
#ifdef CONFIG_SMP
	percpu PT_LOAD FLAGS(7);        /* RWE */
#endif
	data.init2 PT_LOAD FLAGS(7);    /* RWE */
#endif
	note PT_NOTE FLAGS(0);          /* ___ */
}

SECTIONS
{
#ifdef CONFIG_X86_32
        . = LOAD_OFFSET + LOAD_PHYSICAL_ADDR;
        phys_startup_32 = startup_32 - LOAD_OFFSET;
#else
        . = __START_KERNEL;
        phys_startup_64 = startup_64 - LOAD_OFFSET;
#endif

	/* Text and read-only data */

	/* bootstrapping code */
	.text.head : AT(ADDR(.text.head) - LOAD_OFFSET) {
		_text = .;
		*(.text.head)
	} :text = 0x9090

	/* The rest of the text */
	.text :  AT(ADDR(.text) - LOAD_OFFSET) {
#ifdef CONFIG_X86_32
		/* not really needed, already page aligned */
		. = ALIGN(PAGE_SIZE);
		*(.text.page_aligned)
#endif
		. = ALIGN(8);
		_stext = .;
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		*(.fixup)
		*(.gnu.warning)
		/* End of text section */
		_etext = .;
	} :text = 0x9090

	NOTES :text :note

	/* Exception table */
	. = ALIGN(16);
	__ex_table : AT(ADDR(__ex_table) - LOAD_OFFSET) {
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;
	} :text = 0x9090

	RODATA

	/* Data */
	. = ALIGN(PAGE_SIZE);
	.data : AT(ADDR(.data) - LOAD_OFFSET) {
		/* Start of data section */
		_sdata = .;
		DATA_DATA
		CONSTRUCTORS
	} :data

#ifdef CONFIG_X86_32
	/* 32 bit has nosave before _edata */
	. = ALIGN(PAGE_SIZE);
	.data_nosave : AT(ADDR(.data_nosave) - LOAD_OFFSET) {
		__nosave_begin = .;
		*(.data.nosave)
		. = ALIGN(PAGE_SIZE);
		__nosave_end = .;
	}
#endif

	. = ALIGN(PAGE_SIZE);
	.data.page_aligned : AT(ADDR(.data.page_aligned) - LOAD_OFFSET) {
		*(.data.page_aligned)
		*(.data.idt)
	}

#ifdef CONFIG_X86_32
	. = ALIGN(32);
#else
	. = ALIGN(PAGE_SIZE);
	. = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
#endif
	.data.cacheline_aligned :
		AT(ADDR(.data.cacheline_aligned) - LOAD_OFFSET) {
		*(.data.cacheline_aligned)
	}

	/* rarely changed data like cpu maps */
#ifdef CONFIG_X86_32
	. = ALIGN(32);
#else
	. = ALIGN(CONFIG_X86_INTERNODE_CACHE_BYTES);
#endif
	.data.read_mostly : AT(ADDR(.data.read_mostly) - LOAD_OFFSET) {
		*(.data.read_mostly)

		/* End of data section */
		_edata = .;
	}

#ifdef CONFIG_X86_64

#define VSYSCALL_ADDR (-10*1024*1024)
#define VSYSCALL_PHYS_ADDR ((LOADADDR(.data.read_mostly) + \
                            SIZEOF(.data.read_mostly) + 4095) & ~(4095))
#define VSYSCALL_VIRT_ADDR ((ADDR(.data.read_mostly) + \
                            SIZEOF(.data.read_mostly) + 4095) & ~(4095))

#define VLOAD_OFFSET (VSYSCALL_ADDR - VSYSCALL_PHYS_ADDR)
#define VLOAD(x) (ADDR(x) - VLOAD_OFFSET)

#define VVIRT_OFFSET (VSYSCALL_ADDR - VSYSCALL_VIRT_ADDR)
#define VVIRT(x) (ADDR(x) - VVIRT_OFFSET)

	. = VSYSCALL_ADDR;
	.vsyscall_0 : AT(VSYSCALL_PHYS_ADDR) {
		*(.vsyscall_0)
	} :user

	__vsyscall_0 = VSYSCALL_VIRT_ADDR;

	. = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
	.vsyscall_fn : AT(VLOAD(.vsyscall_fn)) {
		*(.vsyscall_fn)
	}

	. = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
	.vsyscall_gtod_data : AT(VLOAD(.vsyscall_gtod_data)) {
		*(.vsyscall_gtod_data)
	}

	vsyscall_gtod_data = VVIRT(.vsyscall_gtod_data);
	.vsyscall_clock : AT(VLOAD(.vsyscall_clock)) {
		*(.vsyscall_clock)
	}
	vsyscall_clock = VVIRT(.vsyscall_clock);


	.vsyscall_1 ADDR(.vsyscall_0) + 1024: AT(VLOAD(.vsyscall_1)) {
		*(.vsyscall_1)
	}
	.vsyscall_2 ADDR(.vsyscall_0) + 2048: AT(VLOAD(.vsyscall_2)) {
		*(.vsyscall_2)
	}

	.vgetcpu_mode : AT(VLOAD(.vgetcpu_mode)) {
		*(.vgetcpu_mode)
	}
	vgetcpu_mode = VVIRT(.vgetcpu_mode);

	. = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
	.jiffies : AT(VLOAD(.jiffies)) {
		*(.jiffies)
	}
	jiffies = VVIRT(.jiffies);

	.vsyscall_3 ADDR(.vsyscall_0) + 3072: AT(VLOAD(.vsyscall_3)) {
		*(.vsyscall_3)
	}

	. = VSYSCALL_VIRT_ADDR + PAGE_SIZE;

#undef VSYSCALL_ADDR
#undef VSYSCALL_PHYS_ADDR
#undef VSYSCALL_VIRT_ADDR
#undef VLOAD_OFFSET
#undef VLOAD
#undef VVIRT_OFFSET
#undef VVIRT

#endif /* CONFIG_X86_64 */

	/* init_task */
	. = ALIGN(THREAD_SIZE);
	.data.init_task : AT(ADDR(.data.init_task) - LOAD_OFFSET) {
		*(.data.init_task)
	}
#ifdef CONFIG_X86_64
	 :data.init
#endif

	/*
	 * smp_locks might be freed after init
	 * start/end must be page aligned
	 */
	. = ALIGN(PAGE_SIZE);
	.smp_locks : AT(ADDR(.smp_locks) - LOAD_OFFSET) {
		__smp_locks = .;
		*(.smp_locks)
		__smp_locks_end = .;
		. = ALIGN(PAGE_SIZE);
	}

	/* Init code and data - will be freed after init */
	. = ALIGN(PAGE_SIZE);
	.init.text : AT(ADDR(.init.text) - LOAD_OFFSET) {
		__init_begin = .; /* paired with __init_end */
		_sinittext = .;
		INIT_TEXT
		_einittext = .;
	}

	.init.data : AT(ADDR(.init.data) - LOAD_OFFSET) {
		INIT_DATA
	}

	. = ALIGN(16);
	.init.setup : AT(ADDR(.init.setup) - LOAD_OFFSET) {
		__setup_start = .;
		*(.init.setup)
		__setup_end = .;
	}
	.initcall.init : AT(ADDR(.initcall.init) - LOAD_OFFSET) {
		__initcall_start = .;
		INITCALLS
		__initcall_end = .;
	}

	.con_initcall.init : AT(ADDR(.con_initcall.init) - LOAD_OFFSET) {
		__con_initcall_start = .;
		*(.con_initcall.init)
		__con_initcall_end = .;
	}

	.x86_cpu_dev.init : AT(ADDR(.x86_cpu_dev.init) - LOAD_OFFSET) {
		__x86_cpu_dev_start = .;
		*(.x86_cpu_dev.init)
		__x86_cpu_dev_end = .;
	}

	SECURITY_INIT

	. = ALIGN(8);
	.parainstructions : AT(ADDR(.parainstructions) - LOAD_OFFSET) {
		__parainstructions = .;
		*(.parainstructions)
		__parainstructions_end = .;
	}

	. = ALIGN(8);
	.altinstructions : AT(ADDR(.altinstructions) - LOAD_OFFSET) {
		__alt_instructions = .;
		*(.altinstructions)
		__alt_instructions_end = .;
	}

	.altinstr_replacement : AT(ADDR(.altinstr_replacement) - LOAD_OFFSET) {
		*(.altinstr_replacement)
	}

	/*
	 * .exit.text is discard at runtime, not link time, to deal with
	 *  references from .altinstructions and .eh_frame
	 */
	.exit.text : AT(ADDR(.exit.text) - LOAD_OFFSET) {
		EXIT_TEXT
	}

	.exit.data : AT(ADDR(.exit.data) - LOAD_OFFSET) {
		EXIT_DATA
	}

#ifdef CONFIG_BLK_DEV_INITRD
	. = ALIGN(PAGE_SIZE);
	.init.ramfs : AT(ADDR(.init.ramfs) - LOAD_OFFSET) {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
	}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_SMP)
	/*
	 * percpu offsets are zero-based on SMP.  PERCPU_VADDR() changes the
	 * output PHDR, so the next output section - __data_nosave - should
	 * start another section data.init2.  Also, pda should be at the head of
	 * percpu area.  Preallocate it and define the percpu offset symbol
	 * so that it can be accessed as a percpu variable.
	 */
	. = ALIGN(PAGE_SIZE);
	PERCPU_VADDR(0, :percpu)
#else
	PERCPU(PAGE_SIZE)
#endif

	. = ALIGN(PAGE_SIZE);

	/* freed after init ends here */
	.init.end : AT(ADDR(.init.end) - LOAD_OFFSET) {
		__init_end = .;
	}

#ifdef CONFIG_X86_64
	.data_nosave : AT(ADDR(.data_nosave) - LOAD_OFFSET) {
		. = ALIGN(PAGE_SIZE);
		__nosave_begin = .;
		*(.data.nosave)
		. = ALIGN(PAGE_SIZE);
		__nosave_end = .;
	} :data.init2
	/* use another section data.init2, see PERCPU_VADDR() above */
#endif

	/* BSS */
	. = ALIGN(PAGE_SIZE);
	.bss : AT(ADDR(.bss) - LOAD_OFFSET) {
		__bss_start = .;
		*(.bss.page_aligned)
		*(.bss)
		. = ALIGN(4);
		__bss_stop = .;
	}

	. = ALIGN(PAGE_SIZE);
	.brk : AT(ADDR(.brk) - LOAD_OFFSET) {
		__brk_base = .;
		. += 64 * 1024;		/* 64k alignment slop space */
		*(.brk_reservation)	/* areas brk users have reserved */
		__brk_limit = .;
	}

	.end : AT(ADDR(.end) - LOAD_OFFSET) {
		_end = .;
	}

	/* Sections to be discarded */
	/DISCARD/ : {
		*(.exitcall.exit)
		*(.eh_frame)
		*(.discard)
	}

        STABS_DEBUG
        DWARF_DEBUG
}


#ifdef CONFIG_X86_32
ASSERT((_end - LOAD_OFFSET <= KERNEL_IMAGE_SIZE),
        "kernel image bigger than KERNEL_IMAGE_SIZE")
#else
/*
 * Per-cpu symbols which need to be offset from __per_cpu_load
 * for the boot processor.
 */
#define INIT_PER_CPU(x) init_per_cpu__##x = per_cpu__##x + __per_cpu_load
INIT_PER_CPU(gdt_page);
INIT_PER_CPU(irq_stack_union);

/*
 * Build-time check on the image size:
 */
ASSERT((_end - _text <= KERNEL_IMAGE_SIZE),
	"kernel image bigger than KERNEL_IMAGE_SIZE")

#ifdef CONFIG_SMP
ASSERT((per_cpu__irq_stack_union == 0),
        "irq_stack_union is not at start of per-cpu area");
#endif

#endif /* CONFIG_X86_32 */

#ifdef CONFIG_KEXEC
#include <asm/kexec.h>

ASSERT(kexec_control_code_size <= KEXEC_CONTROL_CODE_MAX_SIZE,
       "kexec control code size is too big")
#endif

