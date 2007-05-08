/*
 * Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __TT_UACCESS_H
#define __TT_UACCESS_H

#include "linux/string.h"
#include "linux/sched.h"
#include "asm/processor.h"
#include "asm/errno.h"
#include "asm/current.h"
#include "asm/a.out.h"
#include "uml_uaccess.h"

#define ABOVE_KMEM (16 * 1024 * 1024)

extern unsigned long end_vm;
extern unsigned long uml_physmem;

#define is_stack(addr, size) \
	(((unsigned long) (addr) < STACK_TOP) && \
	 ((unsigned long) (addr) >= STACK_TOP - ABOVE_KMEM) && \
	 (((unsigned long) (addr) + (size)) <= STACK_TOP))

#define access_ok_tt(type, addr, size) \
	(is_stack(addr, size))

extern int __do_copy_from_user(void *to, const void *from, int n,
			       void **fault_addr, void **fault_catcher);
extern int __do_strncpy_from_user(char *dst, const char *src, size_t n,
				  void **fault_addr, void **fault_catcher);
extern int __do_clear_user(void *mem, size_t len, void **fault_addr,
			   void **fault_catcher);
extern int __do_strnlen_user(const char *str, unsigned long n,
			     void **fault_addr, void **fault_catcher);

extern int copy_from_user_tt(void *to, const void __user *from, int n);
extern int copy_to_user_tt(void __user *to, const void *from, int n);
extern int strncpy_from_user_tt(char *dst, const char __user *src, int count);
extern int __clear_user_tt(void __user *mem, int len);
extern int clear_user_tt(void __user *mem, int len);
extern int strnlen_user_tt(const void __user *str, int len);

#endif
