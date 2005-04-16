/* $Id: conv.h,v 1.4 1998/08/15 20:42:51 davem Exp $
 * conv.h: Utility macros for Solaris emulation
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
 
/* #define DEBUG_SOLARIS */
#define DEBUG_SOLARIS_KMALLOC

#ifndef __ASSEMBLY__

#include <asm/unistd.h>

/* Use this to get at 32-bit user passed pointers. */
#define A(__x)				\
({	unsigned long __ret;		\
	__asm__ ("srl	%0, 0, %0"	\
		 : "=r" (__ret)		\
		 : "0" (__x));		\
	(void __user *)__ret;		\
})

extern unsigned sys_call_table[];
extern unsigned sys_call_table32[];
extern unsigned sunos_sys_table[];

#define SYS(name) ((long)sys_call_table[__NR_##name])
#define SUNOS(x) ((long)sunos_sys_table[x])

#ifdef DEBUG_SOLARIS
#define SOLD(s) printk("%s,%d,%s(): %s\n",__FILE__,__LINE__,__FUNCTION__,(s))
#define SOLDD(s) printk("solaris: "); printk s
#else
#define SOLD(s)
#define SOLDD(s)
#endif

#endif /* __ASSEMBLY__ */
