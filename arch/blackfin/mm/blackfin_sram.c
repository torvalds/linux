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

static spinlock_t l1sram_lock, l1_data_sram_lock, l1_inst_sram_lock;
static spinlock_t l2_sram_lock;

/* the data structure for L1 scratchpad and DATA SRAM */
struct sram_piece {
	void *paddr;
	int size;
	pid_t pid;
	struct sram_piece *next;
};

static struct sram_piece free_l1_ssram_head, used_l1_ssram_head;

#if L1_DATA_A_LENGTH != 0
static struct sram_piece free_l1_data_A_sram_head, used_l1_data_A_sram_head;
#endif

#if L1_DATA_B_LENGTH != 0
static struct sram_piece free_l1_data_B_sram_head, used_l1_data_B_sram_head;
#endif

#if L1_CODE_LENGTH != 0
static struct sram_piece free_l1_inst_sram_head, used_l1_inst_sram_head;
#endif

#if L2_LENGTH != 0
static struct sram_piece free_l2_sram_head, used_l2_sram_head;
#endif

static struct kmem_cache *sram_piece_cache;

/* L1 Scratchpad SRAM initialization function */
static void __init l1sram_init(void)
{
	free_l1_ssram_head.next =
		kmem_cache_alloc(sram_piece_cache, GFP_KERNEL);
	if (!free_l1_ssram_head.next) {
		printk(KERN_INFO"Fail to initialize Scratchpad data SRAM.\n");
		return;
	}

	free_l1_ssram_head.next->paddr = (void *)L1_SCRATCH_START;
	free_l1_ssram_head.next->size = L1_SCRATCH_LENGTH;
	free_l1_ssram_head.next->pid = 0;
	free_l1_ssram_head.next->next = NULL;

	used_l1_ssram_head.next = NULL;

	/* mutex initialize */
	spin_lock_init(&l1sram_lock);

	printk(KERN_INFO "Blackfin Scratchpad data SRAM: %d KB\n",
	       L1_SCRATCH_LENGTH >> 10);
}

static void __init l1_data_sram_init(void)
{
#if L1_DATA_A_LENGTH != 0
	free_l1_data_A_sram_head.next =
		kmem_cache_alloc(sram_piece_cache, GFP_KERNEL);
	if (!free_l1_data_A_sram_head.next) {
		printk(KERN_INFO"Fail to initialize L1 Data A SRAM.\n");
		return;
	}

	free_l1_data_A_sram_head.next->paddr =
		(void *)L1_DATA_A_START + (_ebss_l1 - _sdata_l1);
	free_l1_data_A_sram_head.next->size =
		L1_DATA_A_LENGTH - (_ebss_l1 - _sdata_l1);
	free_l1_data_A_sram_head.next->pid = 0;
	free_l1_data_A_sram_head.next->next = NULL;

	used_l1_data_A_sram_head.next = NULL;

	printk(KERN_INFO "Blackfin L1 Data A SRAM: %d KB (%d KB free)\n",
		L1_DATA_A_LENGTH >> 10,
		free_l1_data_A_sram_head.next->size >> 10);
#endif
#if L1_DATA_B_LENGTH != 0
	free_l1_data_B_sram_head.next =
		kmem_cache_alloc(sram_piece_cache, GFP_KERNEL);
	if (!free_l1_data_B_sram_head.next) {
		printk(KERN_INFO"Fail to initialize L1 Data B SRAM.\n");
		return;
	}

	free_l1_data_B_sram_head.next->paddr =
		(void *)L1_DATA_B_START + (_ebss_b_l1 - _sdata_b_l1);
	free_l1_data_B_sram_head.next->size =
		L1_DATA_B_LENGTH - (_ebss_b_l1 - _sdata_b_l1);
	free_l1_data_B_sram_head.next->pid = 0;
	free_l1_data_B_sram_head.next->next = NULL;

	used_l1_data_B_sram_head.next = NULL;

	printk(KERN_INFO "Blackfin L1 Data B SRAM: %d KB (%d KB free)\n",
		L1_DATA_B_LENGTH >> 10,
		free_l1_data_B_sram_head.next->size >> 10);
#endif

	/* mutex initialize */
	spin_lock_init(&l1_data_sram_lock);
}

static void __init l1_inst_sram_init(void)
{
#if L1_CODE_LENGTH != 0
	free_l1_inst_sram_head.next =
		kmem_cache_alloc(sram_piece_cache, GFP_KERNEL);
	if (!free_l1_inst_sram_head.next) {
		printk(KERN_INFO"Fail to initialize L1 Instruction SRAM.\n");
		return;
	}

	free_l1_inst_sram_head.next->paddr =
		(void *)L1_CODE_START + (_etext_l1 - _stext_l1);
	free_l1_inst_sram_head.next->size =
		L1_CODE_LENGTH - (_etext_l1 - _stext_l1);
	free_l1_inst_sram_head.next->pid = 0;
	free_l1_inst_sram_head.next->next = NULL;

	used_l1_inst_sram_head.next = NULL;

	printk(KERN_INFO "Blackfin L1 Instruction SRAM: %d KB (%d KB free)\n",
		L1_CODE_LENGTH >> 10,
		free_l1_inst_sram_head.next->size >> 10);
#endif

	/* mutex initialize */
	spin_lock_init(&l1_inst_sram_lock);
}

static void __init l2_sram_init(void)
{
#if L2_LENGTH != 0
	free_l2_sram_head.next =
		kmem_cache_alloc(sram_piece_cache, GFP_KERNEL);
	if (!free_l2_sram_head.next) {
		printk(KERN_INFO"Fail to initialize L2 SRAM.\n");
		return;
	}

	free_l2_sram_head.next->paddr = (void *)L2_START +
		(_etext_l2 - _stext_l2) + (_edata_l2 - _sdata_l2);
	free_l2_sram_head.next->size = L2_LENGTH -
		(_etext_l2 - _stext_l2) + (_edata_l2 - _sdata_l2);
	free_l2_sram_head.next->pid = 0;
	free_l2_sram_head.next->next = NULL;

	used_l2_sram_head.next = NULL;

	printk(KERN_INFO "Blackfin L2 SRAM: %d KB (%d KB free)\n",
		L2_LENGTH >> 10,
		free_l2_sram_head.next->size >> 10);
#endif

	/* mutex initialize */
	spin_lock_init(&l2_sram_lock);
}
void __init bfin_sram_init(void)
{
	sram_piece_cache = kmem_cache_create("sram_piece_cache",
				sizeof(struct sram_piece),
				0, SLAB_PANIC, NULL);

	l1sram_init();
	l1_data_sram_init();
	l1_inst_sram_init();
	l2_sram_init();
}

/* SRAM allocate function */
static void *_sram_alloc(size_t size, struct sram_piece *pfree_head,
		struct sram_piece *pused_head)
{
	struct sram_piece *pslot, *plast, *pavail;

	if (size <= 0 || !pfree_head || !pused_head)
		return NULL;

	/* Align the size */
	size = (size + 3) & ~3;

	pslot = pfree_head->next;
	plast = pfree_head;

	/* search an available piece slot */
	while (pslot != NULL && size > pslot->size) {
		plast = pslot;
		pslot = pslot->next;
	}

	if (!pslot)
		return NULL;

	if (pslot->size == size) {
		plast->next = pslot->next;
		pavail = pslot;
	} else {
		pavail = kmem_cache_alloc(sram_piece_cache, GFP_KERNEL);

		if (!pavail)
			return NULL;

		pavail->paddr = pslot->paddr;
		pavail->size = size;
		pslot->paddr += size;
		pslot->size -= size;
	}

	pavail->pid = current->pid;

	pslot = pused_head->next;
	plast = pused_head;

	/* insert new piece into used piece list !!! */
	while (pslot != NULL && pavail->paddr < pslot->paddr) {
		plast = pslot;
		pslot = pslot->next;
	}

	pavail->next = pslot;
	plast->next = pavail;

	return pavail->paddr;
}

/* Allocate the largest available block.  */
static void *_sram_alloc_max(struct sram_piece *pfree_head,
				struct sram_piece *pused_head,
				unsigned long *psize)
{
	struct sram_piece *pslot, *pmax;

	if (!pfree_head || !pused_head)
		return NULL;

	pmax = pslot = pfree_head->next;

	/* search an available piece slot */
	while (pslot != NULL) {
		if (pslot->size > pmax->size)
			pmax = pslot;
		pslot = pslot->next;
	}

	if (!pmax)
		return NULL;

	*psize = pmax->size;

	return _sram_alloc(*psize, pfree_head, pused_head);
}

/* SRAM free function */
static int _sram_free(const void *addr,
			struct sram_piece *pfree_head,
			struct sram_piece *pused_head)
{
	struct sram_piece *pslot, *plast, *pavail;

	if (!pfree_head || !pused_head)
		return -1;

	/* search the relevant memory slot */
	pslot = pused_head->next;
	plast = pused_head;

	/* search an available piece slot */
	while (pslot != NULL && pslot->paddr != addr) {
		plast = pslot;
		pslot = pslot->next;
	}

	if (!pslot)
		return -1;

	plast->next = pslot->next;
	pavail = pslot;
	pavail->pid = 0;

	/* insert free pieces back to the free list */
	pslot = pfree_head->next;
	plast = pfree_head;

	while (pslot != NULL && addr > pslot->paddr) {
		plast = pslot;
		pslot = pslot->next;
	}

	if (plast != pfree_head && plast->paddr + plast->size == pavail->paddr) {
		plast->size += pavail->size;
		kmem_cache_free(sram_piece_cache, pavail);
	} else {
		pavail->next = plast->next;
		plast->next = pavail;
		plast = pavail;
	}

	if (pslot && plast->paddr + plast->size == pslot->paddr) {
		plast->size += pslot->size;
		plast->next = pslot->next;
		kmem_cache_free(sram_piece_cache, pslot);
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
#if L2_LENGTH != 0
	else if (addr >= (void *)L2_START
		 && addr < (void *)(L2_START + L2_LENGTH))
		return l2_sram_free(addr);
#endif
	else
		return -1;
}
EXPORT_SYMBOL(sram_free);

void *l1_data_A_sram_alloc(size_t size)
{
	unsigned long flags;
	void *addr = NULL;

	/* add mutex operation */
	spin_lock_irqsave(&l1_data_sram_lock, flags);

#if L1_DATA_A_LENGTH != 0
	addr = _sram_alloc(size, &free_l1_data_A_sram_head,
			&used_l1_data_A_sram_head);
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
	unsigned long flags;
	int ret;

	/* add mutex operation */
	spin_lock_irqsave(&l1_data_sram_lock, flags);

#if L1_DATA_A_LENGTH != 0
	ret = _sram_free(addr, &free_l1_data_A_sram_head,
			&used_l1_data_A_sram_head);
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
	unsigned long flags;
	void *addr;

	/* add mutex operation */
	spin_lock_irqsave(&l1_data_sram_lock, flags);

	addr = _sram_alloc(size, &free_l1_data_B_sram_head,
			&used_l1_data_B_sram_head);

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
	unsigned long flags;
	int ret;

	/* add mutex operation */
	spin_lock_irqsave(&l1_data_sram_lock, flags);

	ret = _sram_free(addr, &free_l1_data_B_sram_head,
			&used_l1_data_B_sram_head);

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
	unsigned long flags;
	void *addr;

	/* add mutex operation */
	spin_lock_irqsave(&l1_inst_sram_lock, flags);

	addr = _sram_alloc(size, &free_l1_inst_sram_head,
			&used_l1_inst_sram_head);

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
	unsigned long flags;
	int ret;

	/* add mutex operation */
	spin_lock_irqsave(&l1_inst_sram_lock, flags);

	ret = _sram_free(addr, &free_l1_inst_sram_head,
			&used_l1_inst_sram_head);

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
	unsigned long flags;
	void *addr;

	/* add mutex operation */
	spin_lock_irqsave(&l1sram_lock, flags);

	addr = _sram_alloc(size, &free_l1_ssram_head,
			&used_l1_ssram_head);

	/* add mutex operation */
	spin_unlock_irqrestore(&l1sram_lock, flags);

	return addr;
}

/* L1 Scratchpad memory allocate function */
void *l1sram_alloc_max(size_t *psize)
{
	unsigned long flags;
	void *addr;

	/* add mutex operation */
	spin_lock_irqsave(&l1sram_lock, flags);

	addr = _sram_alloc_max(&free_l1_ssram_head,
			&used_l1_ssram_head, psize);

	/* add mutex operation */
	spin_unlock_irqrestore(&l1sram_lock, flags);

	return addr;
}

/* L1 Scratchpad memory free function */
int l1sram_free(const void *addr)
{
	unsigned long flags;
	int ret;

	/* add mutex operation */
	spin_lock_irqsave(&l1sram_lock, flags);

	ret = _sram_free(addr, &free_l1_ssram_head,
			&used_l1_ssram_head);

	/* add mutex operation */
	spin_unlock_irqrestore(&l1sram_lock, flags);

	return ret;
}

void *l2_sram_alloc(size_t size)
{
#if L2_LENGTH != 0
	unsigned long flags;
	void *addr;

	/* add mutex operation */
	spin_lock_irqsave(&l2_sram_lock, flags);

	addr = _sram_alloc(size, &free_l2_sram_head,
			&used_l2_sram_head);

	/* add mutex operation */
	spin_unlock_irqrestore(&l2_sram_lock, flags);

	pr_debug("Allocated address in l2_sram_alloc is 0x%lx+0x%lx\n",
		 (long unsigned int)addr, size);

	return addr;
#else
	return NULL;
#endif
}
EXPORT_SYMBOL(l2_sram_alloc);

void *l2_sram_zalloc(size_t size)
{
	void *addr = l2_sram_alloc(size);

	if (addr)
		memset(addr, 0x00, size);

	return addr;
}
EXPORT_SYMBOL(l2_sram_zalloc);

int l2_sram_free(const void *addr)
{
#if L2_LENGTH != 0
	unsigned long flags;
	int ret;

	/* add mutex operation */
	spin_lock_irqsave(&l2_sram_lock, flags);

	ret = _sram_free(addr, &free_l2_sram_head,
			&used_l2_sram_head);

	/* add mutex operation */
	spin_unlock_irqrestore(&l2_sram_lock, flags);

	return ret;
#else
	return -1;
#endif
}
EXPORT_SYMBOL(l2_sram_free);

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

	if (addr == NULL && (flags & L2_SRAM))
		addr = l2_sram_alloc(size);

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
/* Need to keep line of output the same.  Currently, that is 44 bytes
 * (including newline).
 */
static int _sram_proc_read(char *buf, int *len, int count, const char *desc,
		struct sram_piece *pfree_head,
		struct sram_piece *pused_head)
{
	struct sram_piece *pslot;

	if (!pfree_head || !pused_head)
		return -1;

	*len += sprintf(&buf[*len], "--- SRAM %-14s Size   PID State     \n", desc);

	/* search the relevant memory slot */
	pslot = pused_head->next;

	while (pslot != NULL) {
		*len += sprintf(&buf[*len], "%p-%p %10i %5i %-10s\n",
			pslot->paddr, pslot->paddr + pslot->size,
			pslot->size, pslot->pid, "ALLOCATED");

		pslot = pslot->next;
	}

	pslot = pfree_head->next;

	while (pslot != NULL) {
		*len += sprintf(&buf[*len], "%p-%p %10i %5i %-10s\n",
			pslot->paddr, pslot->paddr + pslot->size,
			pslot->size, pslot->pid, "FREE");

		pslot = pslot->next;
	}

	return 0;
}
static int sram_proc_read(char *buf, char **start, off_t offset, int count,
		int *eof, void *data)
{
	int len = 0;

	if (_sram_proc_read(buf, &len, count, "Scratchpad",
			&free_l1_ssram_head, &used_l1_ssram_head))
		goto not_done;
#if L1_DATA_A_LENGTH != 0
	if (_sram_proc_read(buf, &len, count, "L1 Data A",
			&free_l1_data_A_sram_head,
			&used_l1_data_A_sram_head))
		goto not_done;
#endif
#if L1_DATA_B_LENGTH != 0
	if (_sram_proc_read(buf, &len, count, "L1 Data B",
			&free_l1_data_B_sram_head,
			&used_l1_data_B_sram_head))
		goto not_done;
#endif
#if L1_CODE_LENGTH != 0
	if (_sram_proc_read(buf, &len, count, "L1 Instruction",
			&free_l1_inst_sram_head, &used_l1_inst_sram_head))
		goto not_done;
#endif
#if L2_LENGTH != 0
	if (_sram_proc_read(buf, &len, count, "L2",
			&free_l2_sram_head, &used_l2_sram_head))
		goto not_done;
#endif

	*eof = 1;
 not_done:
	return len;
}

static int __init sram_proc_init(void)
{
	struct proc_dir_entry *ptr;
	ptr = create_proc_entry("sram", S_IFREG | S_IRUGO, NULL);
	if (!ptr) {
		printk(KERN_WARNING "unable to create /proc/sram\n");
		return -1;
	}
	ptr->owner = THIS_MODULE;
	ptr->read_proc = sram_proc_read;
	return 0;
}
late_initcall(sram_proc_init);
#endif
