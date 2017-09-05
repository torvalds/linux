#ifndef BOOT_COMPRESSED_MISC_H
#define BOOT_COMPRESSED_MISC_H

/*
 * Special hack: we have to be careful, because no indirections are allowed here,
 * and paravirt_ops is a kind of one. As it will only run in baremetal anyway,
 * we just keep it from happening. (This list needs to be extended when new
 * paravirt and debugging variants are added.)
 */
#undef CONFIG_PARAVIRT
#undef CONFIG_PARAVIRT_SPINLOCKS
#undef CONFIG_KAISER
#undef CONFIG_KASAN

#include <linux/linkage.h>
#include <linux/screen_info.h>
#include <linux/elf.h>
#include <linux/io.h>
#include <asm/page.h>
#include <asm/boot.h>
#include <asm/bootparam.h>
#include <asm/bootparam_utils.h>

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
extern struct boot_params *real_mode;		/* Pointer to real-mode data */
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

#if CONFIG_EARLY_PRINTK || CONFIG_RANDOMIZE_BASE
/* cmdline.c */
int cmdline_find_option(const char *option, char *buffer, int bufsize);
int cmdline_find_option_bool(const char *option);
#endif


#if CONFIG_RANDOMIZE_BASE
/* aslr.c */
unsigned char *choose_kernel_location(struct boot_params *boot_params,
				      unsigned char *input,
				      unsigned long input_size,
				      unsigned char *output,
				      unsigned long output_size);
/* cpuflags.c */
bool has_cpuflag(int flag);
#else
static inline
unsigned char *choose_kernel_location(struct boot_params *boot_params,
				      unsigned char *input,
				      unsigned long input_size,
				      unsigned char *output,
				      unsigned long output_size)
{
	return output;
}
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

#endif
