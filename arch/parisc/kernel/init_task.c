/* 
 *    Static declaration of "init" task data structure.
 *
 *    Copyright (C) 2000 Paul Bame <bame at parisc-linux.org>
 *    Copyright (C) 2000-2001 John Marvin <jsm at parisc-linux.org>
 *    Copyright (C) 2001 Helge Deller <deller @ parisc-linux.org>
 *    Copyright (C) 2002 Matthew Wilcox <willy with parisc-linux.org>
 *
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

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/init_task.h>
#include <linux/mqueue.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>

static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
struct mm_struct init_mm = INIT_MM(init_mm);

EXPORT_SYMBOL(init_mm);

/*
 * Initial task structure.
 *
 * We need to make sure that this is 16384-byte aligned due to the
 * way process stacks are handled. This is done by having a special
 * "init_task" linker map entry..
 */
unsigned char interrupt_stack[ISTACK_SIZE] __attribute__ ((section("init_istack"), aligned(4096)));
union thread_union init_thread_union
	__attribute__((aligned(128))) __attribute__((__section__(".data.init_task"))) =
		{ INIT_THREAD_INFO(init_task) };

#if PT_NLEVELS == 3
/* NOTE: This layout exactly conforms to the hybrid L2/L3 page table layout
 * with the first pmd adjacent to the pgd and below it. gcc doesn't actually
 * guarantee that global objects will be laid out in memory in the same order 
 * as the order of declaration, so put these in different sections and use
 * the linker script to order them. */
pmd_t pmd0[PTRS_PER_PMD] __attribute__ ((__section__ (".data.vm0.pmd"), aligned(PAGE_SIZE)));
#endif

pgd_t swapper_pg_dir[PTRS_PER_PGD] __attribute__ ((__section__ (".data.vm0.pgd"), aligned(PAGE_SIZE)));
pte_t pg0[PT_INITIAL * PTRS_PER_PTE] __attribute__ ((__section__ (".data.vm0.pte"), aligned(PAGE_SIZE)));

/*
 * Initial task structure.
 *
 * All other task structs will be allocated on slabs in fork.c
 */
EXPORT_SYMBOL(init_task);

__asm__(".data");
struct task_struct init_task = INIT_TASK(init_task);
