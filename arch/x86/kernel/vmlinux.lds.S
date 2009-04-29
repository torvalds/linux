/*
 * ld script for the x86 kernel
 *
 * Historic 32-bit version written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 * Modernisation and unification done by Sam Ravnborg <sam@ravnborg.org>
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


#ifdef CONFIG_X86_32
# include "vmlinux_32.lds.S"
#else
# include "vmlinux_64.lds.S"
#endif

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

