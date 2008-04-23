/*
 * File:         arch/blackfin/mm/blackfin_sram.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:  SRAM driver for Blackfin ADSP-BF5xx
 *
 * Modified:
 *               Copyright 2004-2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/rtc.h>
#include <asm/blackfin.h>
#include "blackfin_sram.h"

spinlock_t l1sram_lock, l1_data_sram_lock, l1_inst_sram_lock;

#if CONFIG_L1_MAX_PIECE < 16
#undef CONFIG_L1_MAX_PIECE
#define CONFIG_L1_MAX_PIECE        16
#endif

#if CONFIG_L1_MAX_PIECE > 1024
#undef CONFIG_L1_MAX_PIECE
#define CONFIG_L1_MAX_PIECE        1024
#endif

#define SRAM_SLT_NULL      0
#define SRAM_SLT_FREE      1
#define SRAM_SLT_ALLOCATED 2

/* the data structure for L1 scratchpad and DATA SRAM */
struct l1_sram_piece {
	void *paddr;
	int size;
	int flag;
	pid_t pid;
};

static struct l1_sram_piece l1_ssram[CONFIG_L1_MAX_PIECE];

#if L1_DATA_A_LENGTH != 0
static struct l1_sram_piece l1_data_A_sram[CONFIG_L1_MAX_PIECE];
#endif

#if L1_DATA_B_LENGTH != 0
static struct l1_sram_piece l1_data_B_sram[CONFIG_L1_MAX_PIECE];
#endif

#if L1_CODE_LENGTH != 0
static struct l1_sram_piece l1_inst_sram[CONFIG_L1_MAX_PIECE];
#endif

/* L1 Scratchpad SRAM initialization function */
void __init l1sram_init(void)
{
	printk(KERN_INFO "Blackfin Scratchpad data SRAM: %d KB\n",
	       L1_SCRATCH_LENGTH >> 10);

	memset(&l1_ssram, 0x00, sizeof(l1_ssram));
	l1_ssram[0].paddr = (void *)L1_SCRATCH_START;
	l1_ssram[0].size = L1_SCRATCH_LENGTH;
	l1_ssram[0].flag = SRAM_SLT_FREE;

	/* mutex initialize */
	spin_lock_init(&l1sram_lock);
}

void __init l1_data_sram_init(void)
{
#if L1_DATA_A_LENGTH != 0
	memset(&l1_data_A_sram, 0x00, sizeof(l1_data_A_sram));
	l1_data_A_sram[0].paddr = (void *)L1_DATA_A_START +
					(_ebss_l1 - _sdata_l1);
	l1_data_A_sram[0].size = L1_DATA_A_LENGTH - (_ebss_l1 - _sdata_l1);
	l1_data_A_sram[0].flag = SRAM_SLT_FREE;

	printk(KERN_INFO "Blackfin Data A SRAM: %d KB (%d KB free)\n",
	       L1_DATA_A_LENGTH >> 10, l1_data_A_sram[0].size >> 10);
#endif
#if L1_DATA_B_LENGTH != 0
	memset(&l1_data_B_sram, 0x00, sizeof(l1_data_B_sram));
	l1_data_B_sram[0].paddr = (void *)L1_DATA_B_START +
				(_ebss_b_l1 - _sdata_b_l1);
	l1_data_B_sram[0].size = L1_DATA_B_LENGTH - (_ebss_b_l1 - _sdata_b_l1);
	l1_data_B_sram[0].flag = SRAM_SLT_FREE;

	printk(KERN_INFO "Blackfin Data B SRAM: %d KB (%d KB free)\n",
	       L1_DATA_B_LENGTH >> 10, l1_data_B_sram[0].size >> 10);
#endif

	/* mutex initialize */
	spin_lock_init(&l1_data_sram_lock);
}

void __init l1_inst_sram_init(void)
{
#if L1_CODE_LENGTH != 0
	memset(&l1_inst_sram, 0x00, sizeof(l1_inst_sram));
	l1_inst_sram[0].paddr = (void *)L1_CODE_START + (_etext_l1 - _stext_l1);
	l1_inst_sram[0].size = L1_CODE_LENGTH - (_etext_l1 - _stext_l1);
	l1_inst_sram[0].flag = SRAM_SLT_FREE;

	printk(KERN_INFO "Blackfin Instruction SRAM: %d KB (%d KB free)\n",
	       L1_CODE_LENGTH >> 10, l1_inst_sram[0].size >> 10);
#endif

	/* mutex initialize */
	spin_lock_init(&l1_inst_sram_lock);
}

/* L1 memory allocate function */
static void *_l1_sram_alloc(size_t size, struct l1_sram_piece *pfree, int count)
{
	int i, index = 0;
	void *addr = NULL;

	if (size <= 0)
		return NULL;

	/* Align the size */
	size = (size + 3) & ~3;

	/* not use the good method to match the best slot !!! */
	/* search an available memory slot */
	for (i = 0; i < count; i++) {
		if ((pfree[i].flag == SRAM_SLT_FREE)
		    && (pfree[i].size >= size)) {
			addr = pfree[i].paddr;
			pfree[i].flag = SRAM_SLT_ALLOCATED;
			pfree[i].pid = current->pid;
			index = i;
			break;
		}
	}
	if (i >= count)
		return NULL;

	/* updated the NULL memory slot !!! */
	if (pfree[i].size > size) {
		for (i = 0; i < count; i++) {
			if (pfree[i].flag == SRAM_SLT_NULL) {
				pfree[i].pid = 0;
				pfree[i].flag = SRAM_SLT_FREE;
				pfree[i].paddr = addr + size;
				pfree[i].size = pfree[index].size - size;
				pfree[index].size = size;
				break;
			}
		}
	}

	return addr;
}

/* Allocate the largest available block.  */
static void *_l1_sram_alloc_max(struct l1_sram_piece *pfree, int count,
				unsigned long *psize)
{
	unsigned long best = 0;
	int i, index = -1;
	void *addr = NULL;

	/* search an available memory slot */
	for (i = 0; i < count; i++) {
		if (pfree[i].flag == SRAM_SLT_FREE && pfree[i].size > best) {
			addr = pfree[i].paddr;
			index = i;
			best = pfree[i].size;
		}
	}
	if (index < 0)
		return NULL;
	*psize = best;

	pfree[index].pid = current->pid;
	pfree[index].flag = SRAM_SLT_ALLOCATED;
	return addr;
}

/* L1 memory free function */
static int _l1_sram_free(const void *addr,
			struct l1_sram_piece *pfree,
			int count)
{
	int i, index = 0;

	/* search the relevant memory slot */
	for (i = 0; i < count; i++) {
		if (pfree[i].paddr == addr) {
			if (pfree[i].flag != SRAM_SLT_ALLOCATED) {
				/* error log */
				return -1;
			}
			index = i;
			break;
		}
	}
	if (i >= count)
		return -1;

	pfree[index].pid = 0;
	pfree[index].flag = SRAM_SLT_FREE;

	/* link the next address slot */
	for (i = 0; i < count; i++) {
		if (((pfree[index].paddr + pfree[index].size) == pfree[i].paddr)
		    && (pfree[i].flag == SRAM_SLT_FREE)) {
			pfree[i].pid = 0;
			pfree[i].flag = SRAM_SLT_NULL;
			pfree[index].size += pfree[i].size;
			pfree[index].flag = SRAM_SLT_FREE;
			break;
		}
	}

	/* link the last address slot */
	for (i = 0; i < count; i++) {
		if (((pfree[i].paddr + pfree[i].size) == pfree[index].paddr) &&
		    (pfree[i].flag == SRAM_SLT_FREE)) {
			pfree[index].flag = SRAM_SLT_NULL;
			pfree[i].size += pfree[index].size;
			break;
		}
	}

	return 0;
}

int sram_free(const void *addr)
{
	if (0) {}
#if L1_CODE_LENGTH != 0
	else if (addr >= (void *)L1_CODE_START
		 && addr < (void *)(L1_CODE_START + L1_CODE_LENGTH))
		return l1_inst_sram_free(addr);
#endif
#if L1_DATA_A_LENGTH != 0
	else if (addr >= (void *)L1_DATA_A_START
		 && addr < (void *)(L1_DATA_A_START + L1_DATA_A_LENGTH))
		return l1_data_A_sram_free(addr);
#endif
#if L1_DATA_B_LENGTH != 0
	else if (addr >= (void *)L1_DATA_B_START
		 && addr < (void *)(L1_DATA_B_START + L1_DATA_B_LENGTH))
		return l1_data_B_sram_free(addr);
#endif
	else
		return -1;
}
EXPORT_SYMBOL(sram_free);

void *l1_data_A_sram_alloc(size_t size)
{
	unsigned flags;
	void *addr = NULL;

	/* add mutex operation */
	spin_lock_irqsave(&l1_data_sram_lock, flags);

#if L1_DATA_A_LENGTH != 0
	addr = _l1_sram_alloc(size, l1_data_A_sram, ARRAY_SIZE(l1_data_A_sram));
#endif

	/* add mutex operation */
	spin_unlock_irqrestore(&l1_data_sram_lock, flags);

	pr_debug("Allocated address in l1_data_A_sram_alloc is 0x%lx+0x%lx\n",
		 (long unsigned int)addr, size);

	return addr;
}
EXPORT_SYMBOL(l1_data_A_sram_alloc);

int l1_data_A_sram_free(const void *addr)
{
	unsigned flags;
	int ret;

	/* add mutex operation */
	spin_lock_irqsave(&l1_data_sram_lock, flags);

#if L1_DATA_A_LENGTH != 0
	ret = _l1_sram_free(addr,
			   l1_data_A_sram, ARRAY_SIZE(l1_data_A_sram));
#else
	ret = -1;
#endif

	/* add mutex operation */
	spin_unlock_irqrestore(&l1_data_sram_lock, flags);

	return ret;
}
EXPORT_SYMBOL(l1_data_A_sram_free);

void *l1_data_B_sram_alloc(size_t size)
{
#if L1_DATA_B_LENGTH != 0
	unsigned flags;
	void *addr;

	/* add mutex operation */
	spin_lock_irqsave(&l1_data_sram_lock, flags);

	addr = _l1_sram_alloc(size, l1_data_B_sram, ARRAY_SIZE(l1_data_B_sram));

	/* add mutex operation */
	spin_unlock_irqrestore(&l1_data_sram_lock, flags);

	pr_debug("Allocated address in l1_data_B_sram_alloc is 0x%lx+0x%lx\n",
		 (long unsigned int)addr, size);

	return addr;
#else
	return NULL;
#endif
}
EXPORT_SYMBOL(l1_data_B_sram_alloc);

int l1_data_B_sram_free(const void *addr)
{
#if L1_DATA_B_LENGTH != 0
	unsigned flags;
	int ret;

	/* add mutex operation */
	spin_lock_irqsave(&l1_data_sram_lock, flags);

	ret = _l1_sram_free(addr, l1_data_B_sram, ARRAY_SIZE(l1_data_B_sram));

	/* add mutex operation */
	spin_unlock_irqrestore(&l1_data_sram_lock, flags);

	return ret;
#else
	return -1;
#endif
}
EXPORT_SYMBOL(l1_data_B_sram_free);

void *l1_data_sram_alloc(size_t size)
{
	void *addr = l1_data_A_sram_alloc(size);

	if (!addr)
		addr = l1_data_B_sram_alloc(size);

	return addr;
}
EXPORT_SYMBOL(l1_data_sram_alloc);

void *l1_data_sram_zalloc(size_t size)
{
	void *addr = l1_data_sram_alloc(size);

	if (addr)
		memset(addr, 0x00, size);

	return addr;
}
EXPORT_SYMBOL(l1_data_sram_zalloc);

int l1_data_sram_free(const void *addr)
{
	int ret;
	ret = l1_data_A_sram_free(addr);
	if (ret == -1)
		ret = l1_data_B_sram_free(addr);
	return ret;
}
EXPORT_SYMBOL(l1_data_sram_free);

void *l1_inst_sram_alloc(size_t size)
{
#if L1_CODE_LENGTH != 0
	unsigned flags;
	void *addr;

	/* add mutex operation */
	spin_lock_irqsave(&l1_inst_sram_lock, flags);

	addr = _l1_sram_alloc(size, l1_inst_sram, ARRAY_SIZE(l1_inst_sram));

	/* add mutex operation */
	spin_unlock_irqrestore(&l1_inst_sram_lock, flags);

	pr_debug("Allocated address in l1_inst_sram_alloc is 0x%lx+0x%lx\n",
		 (long unsigned int)addr, size);

	return addr;
#else
	return NULL;
#endif
}
EXPORT_SYMBOL(l1_inst_sram_alloc);

int l1_inst_sram_free(const void *addr)
{
#if L1_CODE_LENGTH != 0
	unsigned flags;
	int ret;

	/* add mutex operation */
	spin_lock_irqsave(&l1_inst_sram_lock, flags);

	ret = _l1_sram_free(addr, l1_inst_sram, ARRAY_SIZE(l1_inst_sram));

	/* add mutex operation */
	spin_unlock_irqrestore(&l1_inst_sram_lock, flags);

	return ret;
#else
	return -1;
#endif
}
EXPORT_SYMBOL(l1_inst_sram_free);

/* L1 Scratchpad memory allocate function */
void *l1sram_alloc(size_t size)
{
	unsigned flags;
	void *addr;

	/* add mutex operation */
	spin_lock_irqsave(&l1sram_lock, flags);

	addr = _l1_sram_alloc(size, l1_ssram, ARRAY_SIZE(l1_ssram));

	/* add mutex operation */
	spin_unlock_irqrestore(&l1sram_lock, flags);

	return addr;
}

/* L1 Scratchpad memory allocate function */
void *l1sram_alloc_max(size_t *psize)
{
	unsigned flags;
	void *addr;

	/* add mutex operation */
	spin_lock_irqsave(&l1sram_lock, flags);

	addr = _l1_sram_alloc_max(l1_ssram, ARRAY_SIZE(l1_ssram), psize);

	/* add mutex operation */
	spin_unlock_irqrestore(&l1sram_lock, flags);

	return addr;
}

/* L1 Scratchpad memory free function */
int l1sram_free(const void *addr)
{
	unsigned flags;
	int ret;

	/* add mutex operation */
	spin_lock_irqsave(&l1sram_lock, flags);

	ret = _l1_sram_free(addr, l1_ssram, ARRAY_SIZE(l1_ssram));

	/* add mutex operation */
	spin_unlock_irqrestore(&l1sram_lock, flags);

	return ret;
}

int sram_free_with_lsl(const void *addr)
{
	struct sram_list_struct *lsl, **tmp;
	struct mm_struct *mm = current->mm;

	for (tmp = &mm->context.sram_list; *tmp; tmp = &(*tmp)->next)
		if ((*tmp)->addr == addr)
			goto found;
	return -1;
found:
	lsl = *tmp;
	sram_free(addr);
	*tmp = lsl->next;
	kfree(lsl);

	return 0;
}
EXPORT_SYMBOL(sram_free_with_lsl);

void *sram_alloc_with_lsl(size_t size, unsigned long flags)
{
	void *addr = NULL;
	struct sram_list_struct *lsl = NULL;
	struct mm_struct *mm = current->mm;

	lsl = kzalloc(sizeof(struct sram_list_struct), GFP_KERNEL);
	if (!lsl)
		return NULL;

	if (flags & L1_INST_SRAM)
		addr = l1_inst_sram_alloc(size);

	if (addr == NULL && (flags & L1_DATA_A_SRAM))
		addr = l1_data_A_sram_alloc(size);

	if (addr == NULL && (flags & L1_DATA_B_SRAM))
		addr = l1_data_B_sram_alloc(size);

	if (addr == NULL) {
		kfree(lsl);
		return NULL;
	}
	lsl->addr = addr;
	lsl->length = size;
	lsl->next = mm->context.sram_list;
	mm->context.sram_list = lsl;
	return addr;
}
EXPORT_SYMBOL(sram_alloc_with_lsl);

#ifdef CONFIG_PROC_FS
/* Once we get a real allocator, we'll throw all of this away.
 * Until then, we need some sort of visibility into the L1 alloc.
 */
static void _l1sram_proc_read(char *buf, int *len, const char *desc,
		struct l1_sram_piece *pfree, const int array_size)
{
	int i;

	*len += sprintf(&buf[*len], "--- L1 %-14s Size  PID State\n", desc);
	for (i = 0; i < array_size; ++i) {
		const char *alloc_type;
		switch (pfree[i].flag) {
		case SRAM_SLT_NULL:      alloc_type = "NULL"; break;
		case SRAM_SLT_FREE:      alloc_type = "FREE"; break;
		case SRAM_SLT_ALLOCATED: alloc_type = "ALLOCATED"; break;
		default:                 alloc_type = "????"; break;
		}
		*len += sprintf(&buf[*len], "%p-%p %8i %4i %s\n",
			pfree[i].paddr, pfree[i].paddr + pfree[i].size,
			pfree[i].size, pfree[i].pid, alloc_type);
	}
}
static int l1sram_proc_read(char *buf, char **start, off_t offset, int count,
		int *eof, void *data)
{
	int len = 0;

	_l1sram_proc_read(buf, &len, "Scratchpad",
			l1_ssram, ARRAY_SIZE(l1_ssram));
#if L1_DATA_A_LENGTH != 0
	_l1sram_proc_read(buf, &len, "Data A",
			l1_data_A_sram, ARRAY_SIZE(l1_data_A_sram));
#endif
#if L1_DATA_B_LENGTH != 0
	_l1sram_proc_read(buf, &len, "Data B",
			l1_data_B_sram, ARRAY_SIZE(l1_data_B_sram));
#endif
#if L1_CODE_LENGTH != 0
	_l1sram_proc_read(buf, &len, "Instruction",
			l1_inst_sram, ARRAY_SIZE(l1_inst_sram));
#endif

	return len;
}

static int __init l1sram_proc_init(void)
{
	struct proc_dir_entry *ptr;
	ptr = create_proc_entry("sram", S_IFREG | S_IRUGO, NULL);
	if (!ptr) {
		printk(KERN_WARNING "unable to create /proc/sram\n");
		return -1;
	}
	ptr->owner = THIS_MODULE;
	ptr->read_proc = l1sram_proc_read;
	return 0;
}
late_initcall(l1sram_proc_init);
#endif
