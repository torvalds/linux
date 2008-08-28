#ifndef _BLACKFIN_BUG_H
#define _BLACKFIN_BUG_H

#ifdef CONFIG_BUG
#define HAVE_ARCH_BUG

#define BUG() do { \
	dump_bfin_trace_buffer(); \
	printk(KERN_EMERG "BUG: failure at %s:%d/%s()!\n", __FILE__, __LINE__, __func__); \
	panic("BUG!"); \
} while (0)

#endif

#include <asm-generic/bug.h>

#endif
