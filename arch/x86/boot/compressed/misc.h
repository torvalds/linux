/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_COMPRESSED_MISC_H
#define BOOT_COMPRESSED_MISC_H

/*
 * Special hack: we have to be careful, because no indirections are allowed here,
 * and paravirt_ops is a kind of one. As it will only run in baremetal anyway,
 * we just keep it from happening. (This list needs to be extended when new
 * paravirt and debugging variants are added.)
 */
#undef CONFIG_PARAVIRT
#undef CONFIG_PARAVIRT_XXL
#undef CONFIG_PARAVIRT_SPINLOCKS
#undef CONFIG_KASAN
#undef CONFIG_KASAN_GENERIC

/* cpu_feature_enabled() cannot be used this early */
#define USE_EARLY_PGTABLE_L5

#include <linux/linkage.h>
#include <linux/screen_info.h>
#include <linux/elf.h>
#include <linux/io.h>
#include <asm/page.h>
#include <asm/boot.h>
#include <asm/bootparam.h>
#include <asm/desc_defs.h>

#define BOOT_CTYPE_H
#include <linux/acpi.h>

#define BOOT_BOOT_H
#include "../ctype.h"

#ifdef CONFIG_X86_64
#define memptr long
#else
#define memptr unsigned
#endif

/* boot/compressed/vmlinux start and end markers */
extern char _head[], _end[];

/* misc.c */
extern memptr free_mem_ptr;
extern memptr free_mem_end_ptr;
extern struct boot_params *boot_params;
void __putstr(const char *s);
void __puthex(unsigned long value);
#define error_putstr(__x)  __putstr(__x)
#define error_puthex(__x)  __puthex(__x)

#ifdef CONFIG_X86_VERBOSE_BOOTUP

#define debug_putstr(__x)  __putstr(__x)
#define debug_puthex(__x)  __puthex(__x)
#define debug_putaddr(__x) { \
		debug_putstr(#__x ": 0x"); \
		debug_puthex((unsigned long)(__x)); \
		debug_putstr("\n"); \
	}

#else

static inline void debug_putstr(const char *s)
{ }
static inline void debug_puthex(unsigned long value)
{ }
#define debug_putaddr(x) /* */

#endif

/* cmdline.c */
int cmdline_find_option(const char *option, char *buffer, int bufsize);
int cmdline_find_option_bool(const char *option);

struct mem_vector {
	u64 start;
	u64 size;
};

#if CONFIG_RANDOMIZE_BASE
/* kaslr.c */
void choose_random_location(unsigned long input,
			    unsigned long input_size,
			    unsigned long *output,
			    unsigned long output_size,
			    unsigned long *virt_addr);
#else
static inline void choose_random_location(unsigned long input,
					  unsigned long input_size,
					  unsigned long *output,
					  unsigned long output_size,
					  unsigned long *virt_addr)
{
}
#endif

/* cpuflags.c */
bool has_cpuflag(int flag);

#ifdef CONFIG_X86_64
extern int set_page_decrypted(unsigned long address);
extern int set_page_encrypted(unsigned long address);
extern int set_page_non_present(unsigned long address);
extern unsigned char _pgtable[];
#endif

#ifdef CONFIG_EARLY_PRINTK
/* early_serial_console.c */
extern int early_serial_base;
void console_init(void);
#else
static const int early_serial_base;
static inline void console_init(void)
{ }
#endif

void set_sev_encryption_mask(void);

#ifdef CONFIG_AMD_MEM_ENCRYPT
void sev_es_shutdown_ghcb(void);
extern bool sev_es_check_ghcb_fault(unsigned long address);
#else
static inline void sev_es_shutdown_ghcb(void) { }
static inline bool sev_es_check_ghcb_fault(unsigned long address)
{
	return false;
}
#endif

/* acpi.c */
#ifdef CONFIG_ACPI
acpi_physical_address get_rsdp_addr(void);
#else
static inline acpi_physical_address get_rsdp_addr(void) { return 0; }
#endif

#if defined(CONFIG_RANDOMIZE_BASE) && defined(CONFIG_MEMORY_HOTREMOVE) && defined(CONFIG_ACPI)
extern struct mem_vector immovable_mem[MAX_NUMNODES*2];
int count_immovable_mem_regions(void);
#else
static inline int count_immovable_mem_regions(void) { return 0; }
#endif

/* ident_map_64.c */
#ifdef CONFIG_X86_5LEVEL
extern unsigned int __pgtable_l5_enabled, pgdir_shift, ptrs_per_p4d;
#endif

/* Used by PAGE_KERN* macros: */
extern pteval_t __default_kernel_pte_mask;

/* idt_64.c */
extern gate_desc boot_idt[BOOT_IDT_ENTRIES];
extern struct desc_ptr boot_idt_desc;

/* IDT Entry Points */
void boot_page_fault(void);
void boot_stage1_vc(void);
void boot_stage2_vc(void);

unsigned long sev_verify_cbit(unsigned long cr3);

#endif /* BOOT_COMPRESSED_MISC_H */
