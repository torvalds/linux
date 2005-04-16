/*
 * Copyright (C) 1995-2001 Russell King
 *               2001-2002 Keith Owens
 *               2003      Ian Molton
 *     
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>

/*
 * Make sure that the compiler and target are compatible.
 */
#if defined(__APCS_32__) && defined(CONFIG_CPU_26)
#error Sorry, your compiler targets APCS-32 but this kernel requires APCS-26
#endif
#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 95)
#error Sorry, your compiler is known to miscompile kernels.  Only use gcc 2.95.3 and later.
#endif
#if __GNUC__ == 2 && __GNUC_MINOR__ == 95
/* shame we can't detect the .1 or .2 releases */
#warning GCC 2.95.2 and earlier miscompiles kernels.
#endif

/* Use marker if you need to separate the values later */

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

int main(void)
{
  DEFINE(TSK_ACTIVE_MM,		offsetof(struct task_struct, active_mm));
  BLANK();
  DEFINE(VMA_VM_MM,		offsetof(struct vm_area_struct, vm_mm));
  DEFINE(VMA_VM_FLAGS,		offsetof(struct vm_area_struct, vm_flags));
  BLANK();
  DEFINE(VM_EXEC,	       	VM_EXEC);
  BLANK();
  BLANK();
  DEFINE(PAGE_PRESENT,		_PAGE_PRESENT);
  DEFINE(PAGE_READONLY,		_PAGE_READONLY);
  DEFINE(PAGE_NOT_USER,		_PAGE_NOT_USER);
  DEFINE(PAGE_OLD,		_PAGE_OLD);
  DEFINE(PAGE_CLEAN,		_PAGE_CLEAN);
  BLANK();
  DEFINE(PAGE_SZ,	       	PAGE_SIZE);
  BLANK();
  DEFINE(SYS_ERROR0,		0x9f0000);
  return 0; 
}
