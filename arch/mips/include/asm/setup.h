#ifndef _MIPS_SETUP_H
#define _MIPS_SETUP_H

#define COMMAND_LINE_SIZE	4096

#ifdef  __KERNEL__
extern void setup_early_printk(void);

extern void set_handler(unsigned long offset, void *addr, unsigned long len);
extern void set_uncached_handler(unsigned long offset, void *addr, unsigned long len);

typedef void (*vi_handler_t)(void);
extern void *set_vi_handler(int n, vi_handler_t addr);

extern void *set_except_vector(int n, void *addr);
extern unsigned long ebase;
extern void per_cpu_trap_init(void);

#endif /* __KERNEL__ */

#endif /* __SETUP_H */
