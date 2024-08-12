/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MIPS_SETUP_H
#define _MIPS_SETUP_H

#include <linux/init.h>
#include <linux/types.h>
#include <uapi/asm/setup.h>

extern void prom_putchar(char);
extern void setup_early_printk(void);

#ifdef CONFIG_EARLY_PRINTK_8250
extern void setup_8250_early_printk_port(unsigned long base,
	unsigned int reg_shift, unsigned int timeout);
#else
static inline void setup_8250_early_printk_port(unsigned long base,
	unsigned int reg_shift, unsigned int timeout) {}
#endif

void set_handler(unsigned long offset, const void *addr, unsigned long len);
extern void set_uncached_handler(unsigned long offset, void *addr, unsigned long len);

typedef void (*vi_handler_t)(void);
extern void *set_vi_handler(int n, vi_handler_t addr);

extern void *set_except_vector(int n, void *addr);
extern unsigned long ebase;
extern unsigned int hwrena;
extern void per_cpu_trap_init(bool);
extern void cpu_cache_init(void);
extern void tlb_init(void);

#ifdef CONFIG_RELOCATABLE
extern void * __init relocate_kernel(void);
extern int plat_post_relocation(long);
#endif

#endif /* __SETUP_H */
