/*
 *    Architecture-specific kernel symbols
 *
 *    Copyright (C) 2000-2001 Richard Hirst <rhirst with parisc-linux.org>
 *    Copyright (C) 2001 Dave Kennedy
 *    Copyright (C) 2001 Paul Bame <bame at parisc-linux.org>
 *    Copyright (C) 2001-2003 Grant Grundler <grundler with parisc-linux.org>
 *    Copyright (C) 2002-2003 Matthew Wilcox <willy at parisc-linux.org>
 *    Copyright (C) 2002 Randolph Chung <tausq at parisc-linux.org>
 *    Copyright (C) 2002-2007 Helge Deller <deller with parisc-linux.org>
 * 
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>

#include <linux/string.h>
EXPORT_SYMBOL(memset);

#include <asm/atomic.h>
EXPORT_SYMBOL(__xchg8);
EXPORT_SYMBOL(__xchg32);
EXPORT_SYMBOL(__cmpxchg_u32);
#ifdef CONFIG_SMP
EXPORT_SYMBOL(__atomic_hash);
#endif
#ifdef CONFIG_64BIT
EXPORT_SYMBOL(__xchg64);
EXPORT_SYMBOL(__cmpxchg_u64);
#endif

#include <asm/uaccess.h>
EXPORT_SYMBOL(lstrncpy_from_user);
EXPORT_SYMBOL(lclear_user);
EXPORT_SYMBOL(lstrnlen_user);

/* Global fixups */
extern void fixup_get_user_skip_1(void);
extern void fixup_get_user_skip_2(void);
extern void fixup_put_user_skip_1(void);
extern void fixup_put_user_skip_2(void);
EXPORT_SYMBOL(fixup_get_user_skip_1);
EXPORT_SYMBOL(fixup_get_user_skip_2);
EXPORT_SYMBOL(fixup_put_user_skip_1);
EXPORT_SYMBOL(fixup_put_user_skip_2);

#ifndef CONFIG_64BIT
/* Needed so insmod can set dp value */
extern int $global$;
EXPORT_SYMBOL($global$);
#endif

#include <asm/io.h>
EXPORT_SYMBOL(memcpy_toio);
EXPORT_SYMBOL(memcpy_fromio);
EXPORT_SYMBOL(memset_io);

#include <asm/semaphore.h>
EXPORT_SYMBOL(__up);
EXPORT_SYMBOL(__down_interruptible);
EXPORT_SYMBOL(__down);

extern void $$divI(void);
extern void $$divU(void);
extern void $$remI(void);
extern void $$remU(void);
extern void $$mulI(void);
extern void $$divU_3(void);
extern void $$divU_5(void);
extern void $$divU_6(void);
extern void $$divU_9(void);
extern void $$divU_10(void);
extern void $$divU_12(void);
extern void $$divU_7(void);
extern void $$divU_14(void);
extern void $$divU_15(void);
extern void $$divI_3(void);
extern void $$divI_5(void);
extern void $$divI_6(void);
extern void $$divI_7(void);
extern void $$divI_9(void);
extern void $$divI_10(void);
extern void $$divI_12(void);
extern void $$divI_14(void);
extern void $$divI_15(void);

EXPORT_SYMBOL($$divI);
EXPORT_SYMBOL($$divU);
EXPORT_SYMBOL($$remI);
EXPORT_SYMBOL($$remU);
EXPORT_SYMBOL($$mulI);
EXPORT_SYMBOL($$divU_3);
EXPORT_SYMBOL($$divU_5);
EXPORT_SYMBOL($$divU_6);
EXPORT_SYMBOL($$divU_9);
EXPORT_SYMBOL($$divU_10);
EXPORT_SYMBOL($$divU_12);
EXPORT_SYMBOL($$divU_7);
EXPORT_SYMBOL($$divU_14);
EXPORT_SYMBOL($$divU_15);
EXPORT_SYMBOL($$divI_3);
EXPORT_SYMBOL($$divI_5);
EXPORT_SYMBOL($$divI_6);
EXPORT_SYMBOL($$divI_7);
EXPORT_SYMBOL($$divI_9);
EXPORT_SYMBOL($$divI_10);
EXPORT_SYMBOL($$divI_12);
EXPORT_SYMBOL($$divI_14);
EXPORT_SYMBOL($$divI_15);

asmlinkage void * __canonicalize_funcptr_for_compare(void *);
EXPORT_SYMBOL(__canonicalize_funcptr_for_compare);

#ifndef CONFIG_64BIT
extern void $$dyncall(void);
EXPORT_SYMBOL($$dyncall);
#endif

#ifdef CONFIG_DISCONTIGMEM
#include <asm/mmzone.h>
EXPORT_SYMBOL(node_data);
EXPORT_SYMBOL(pfnnid_map);
#endif
