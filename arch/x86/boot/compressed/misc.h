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

/* cpu_feature_enabled() cannot be used this early */
#define USE_EARLY_PGTABLE_L5

#include <linux/linkage.h>
#include <linux/screen_info.h>
#include <linux/elf.h>
#include <linux/io.h>
#include <asm/page.h>
#include <asm/boot.h>
#include <asm/bootparam.h>
#include <asm/bootparam_utils.h>

#define BOOT_CTYPE_H
#include <linux/acpi.h>

#define BOOT_BOOT_H
#include "../ctype.h"

#ifdef CONFIG_X86_64
#define memptr long
#else
#define memptr unsigned
#endif

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
static inline void debug_puthex(const char *s)
{ }
#define debug_putaddr(x) /* */

#endif

/* cmdline.c */
int cmdline_find_option(const char *option, char *buffer, int bufsize);
int cmdline_find_option_bool(const char *option);

struct mem_vector {
	unsigned long long start;
	unsigned long long size;
};

#if CONFIG_RANDOMIZE_BASE
/* kaslr.c */
void choose_random_location(unsigned long input,
			    unsigned long input_size,
			    unsigned long *output,
			    unsigned long output_size,
			    unsigned long *virt_addr);
/* cpuflags.c */
bool has_cpuflag(int flag);
#else
static inline void choose_random_location(unsigned long input,
					  unsigned long input_size,
					  unsigned long *output,
					  unsigned long output_size,
					  unsigned long *virt_addr)
{
}
#endif

#ifdef CONFIG_X86_64
void initialize_identity_maps(void);
void add_identity_map(unsigned long start, unsigned long size);
void finalize_identity_maps(void);
extern unsigned char _pgtable[];
#else
static inline void initialize_identity_maps(void)
{ }
static inline void add_identity_map(unsigned long start, unsigned long size)
{ }
static inline void finalize_identity_maps(void)
{ }
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
