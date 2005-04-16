/*
 *  linux/arch/m68k/mm/kmap.c
 *
 *  Copyright (C) 1997 Roman Hodek
 *
 *  10/01/99 cleaned up the code and changing to the same interface
 *	     used by other architectures		/Roman Zippel
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/io.h>
#include <asm/system.h>

#undef DEBUG

#define PTRTREESIZE	(256*1024)

/*
 * For 040/060 we can use the virtual memory area like other architectures,
 * but for 020/030 we want to use early termination page descriptor and we
 * can't mix this with normal page descriptors, so we have to copy that code
 * (mm/vmalloc.c) and return appriorate aligned addresses.
 */

#ifdef CPU_M68040_OR_M68060_ONLY

#define IO_SIZE		PAGE_SIZE

static inline struct vm_struct *get_io_area(unsigned long size)
{
	return get_vm_area(size, VM_IOREMAP);
}


static inline void free_io_area(void *addr)
{
	vfree((void *)(PAGE_MASK & (unsigned long)addr));
}

#else

#define IO_SIZE		(256*1024)

static struct vm_struct *iolist;

static struct vm_struct *get_io_area(unsigned long size)
{
	unsigned long addr;
	struct vm_struct **p, *tmp, *area;

	area = (struct vm_struct *)kmalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		return NULL;
	addr = KMAP_START;
	for (p = &iolist; (tmp = *p) ; p = &tmp->next) {
		if (size + addr < (unsigned long)tmp->addr)
			break;
		if (addr > KMAP_END-size)
			return NULL;
		addr = tmp->size + (unsigned long)tmp->addr;
	}
	area->addr = (void *)addr;
	area->size = size + IO_SIZE;
	area->next = *p;
	*p = area;
	return area;
}

static inline void free_io_area(void *addr)
{
	struct vm_struct **p, *tmp;

	if (!addr)
		return;
	addr = (void *)((unsigned long)addr & -IO_SIZE);
	for (p = &iolist ; (tmp = *p) ; p = &tmp->next) {
		if (tmp->addr == addr) {
			*p = tmp->next;
			__iounmap(tmp->addr, tmp->size);
			kfree(tmp);
			return;
		}
	}
}

#endif

/*
 * Map some physical address range into the kernel address space. The
 * code is copied and adapted from map_chunk().
 */
/* Rewritten by Andreas Schwab to remove all races. */

void *__ioremap(unsigned long physaddr, unsigned long size, int cacheflag)
{
	struct vm_struct *area;
	unsigned long virtaddr, retaddr;
	long offset;
	pgd_t *pgd_dir;
	pmd_t *pmd_dir;
	pte_t *pte_dir;

	/*
	 * Don't allow mappings that wrap..
	 */
	if (!size || size > physaddr + size)
		return NULL;

#ifdef CONFIG_AMIGA
	if (MACH_IS_AMIGA) {
		if ((physaddr >= 0x40000000) && (physaddr + size < 0x60000000)
		    && (cacheflag == IOMAP_NOCACHE_SER))
			return (void *)physaddr;
	}
#endif

#ifdef DEBUG
	printk("ioremap: 0x%lx,0x%lx(%d) - ", physaddr, size, cacheflag);
#endif
	/*
	 * Mappings have to be aligned
	 */
	offset = physaddr & (IO_SIZE - 1);
	physaddr &= -IO_SIZE;
	size = (size + offset + IO_SIZE - 1) & -IO_SIZE;

	/*
	 * Ok, go for it..
	 */
	area = get_io_area(size);
	if (!area)
		return NULL;

	virtaddr = (unsigned long)area->addr;
	retaddr = virtaddr + offset;
#ifdef DEBUG
	printk("0x%lx,0x%lx,0x%lx", physaddr, virtaddr, retaddr);
#endif

	/*
	 * add cache and table flags to physical address
	 */
	if (CPU_IS_040_OR_060) {
		physaddr |= (_PAGE_PRESENT | _PAGE_GLOBAL040 |
			     _PAGE_ACCESSED | _PAGE_DIRTY);
		switch (cacheflag) {
		case IOMAP_FULL_CACHING:
			physaddr |= _PAGE_CACHE040;
			break;
		case IOMAP_NOCACHE_SER:
		default:
			physaddr |= _PAGE_NOCACHE_S;
			break;
		case IOMAP_NOCACHE_NONSER:
			physaddr |= _PAGE_NOCACHE;
			break;
		case IOMAP_WRITETHROUGH:
			physaddr |= _PAGE_CACHE040W;
			break;
		}
	} else {
		physaddr |= (_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_DIRTY);
		switch (cacheflag) {
		case IOMAP_NOCACHE_SER:
		case IOMAP_NOCACHE_NONSER:
		default:
			physaddr |= _PAGE_NOCACHE030;
			break;
		case IOMAP_FULL_CACHING:
		case IOMAP_WRITETHROUGH:
			break;
		}
	}

	while ((long)size > 0) {
#ifdef DEBUG
		if (!(virtaddr & (PTRTREESIZE-1)))
			printk ("\npa=%#lx va=%#lx ", physaddr, virtaddr);
#endif
		pgd_dir = pgd_offset_k(virtaddr);
		pmd_dir = pmd_alloc(&init_mm, pgd_dir, virtaddr);
		if (!pmd_dir) {
			printk("ioremap: no mem for pmd_dir\n");
			return NULL;
		}

		if (CPU_IS_020_OR_030) {
			pmd_dir->pmd[(virtaddr/PTRTREESIZE) & 15] = physaddr;
			physaddr += PTRTREESIZE;
			virtaddr += PTRTREESIZE;
			size -= PTRTREESIZE;
		} else {
			pte_dir = pte_alloc_kernel(&init_mm, pmd_dir, virtaddr);
			if (!pte_dir) {
				printk("ioremap: no mem for pte_dir\n");
				return NULL;
			}

			pte_val(*pte_dir) = physaddr;
			virtaddr += PAGE_SIZE;
			physaddr += PAGE_SIZE;
			size -= PAGE_SIZE;
		}
	}
#ifdef DEBUG
	printk("\n");
#endif
	flush_tlb_all();

	return (void *)retaddr;
}

/*
 * Unmap a ioremap()ed region again
 */
void iounmap(void *addr)
{
#ifdef CONFIG_AMIGA
	if ((!MACH_IS_AMIGA) ||
	    (((unsigned long)addr < 0x40000000) ||
	     ((unsigned long)addr > 0x60000000)))
			free_io_area(addr);
#else
	free_io_area(addr);
#endif
}

/*
 * __iounmap unmaps nearly everything, so be careful
 * it doesn't free currently pointer/page tables anymore but it
 * wans't used anyway and might be added later.
 */
void __iounmap(void *addr, unsigned long size)
{
	unsigned long virtaddr = (unsigned long)addr;
	pgd_t *pgd_dir;
	pmd_t *pmd_dir;
	pte_t *pte_dir;

	while ((long)size > 0) {
		pgd_dir = pgd_offset_k(virtaddr);
		if (pgd_bad(*pgd_dir)) {
			printk("iounmap: bad pgd(%08lx)\n", pgd_val(*pgd_dir));
			pgd_clear(pgd_dir);
			return;
		}
		pmd_dir = pmd_offset(pgd_dir, virtaddr);

		if (CPU_IS_020_OR_030) {
			int pmd_off = (virtaddr/PTRTREESIZE) & 15;

			if ((pmd_dir->pmd[pmd_off] & _DESCTYPE_MASK) == _PAGE_PRESENT) {
				pmd_dir->pmd[pmd_off] = 0;
				virtaddr += PTRTREESIZE;
				size -= PTRTREESIZE;
				continue;
			}
		}

		if (pmd_bad(*pmd_dir)) {
			printk("iounmap: bad pmd (%08lx)\n", pmd_val(*pmd_dir));
			pmd_clear(pmd_dir);
			return;
		}
		pte_dir = pte_offset_kernel(pmd_dir, virtaddr);

		pte_val(*pte_dir) = 0;
		virtaddr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	flush_tlb_all();
}

/*
 * Set new cache mode for some kernel address space.
 * The caller must push data for that range itself, if such data may already
 * be in the cache.
 */
void kernel_set_cachemode(void *addr, unsigned long size, int cmode)
{
	unsigned long virtaddr = (unsigned long)addr;
	pgd_t *pgd_dir;
	pmd_t *pmd_dir;
	pte_t *pte_dir;

	if (CPU_IS_040_OR_060) {
		switch (cmode) {
		case IOMAP_FULL_CACHING:
			cmode = _PAGE_CACHE040;
			break;
		case IOMAP_NOCACHE_SER:
		default:
			cmode = _PAGE_NOCACHE_S;
			break;
		case IOMAP_NOCACHE_NONSER:
			cmode = _PAGE_NOCACHE;
			break;
		case IOMAP_WRITETHROUGH:
			cmode = _PAGE_CACHE040W;
			break;
		}
	} else {
		switch (cmode) {
		case IOMAP_NOCACHE_SER:
		case IOMAP_NOCACHE_NONSER:
		default:
			cmode = _PAGE_NOCACHE030;
			break;
		case IOMAP_FULL_CACHING:
		case IOMAP_WRITETHROUGH:
			cmode = 0;
		}
	}

	while ((long)size > 0) {
		pgd_dir = pgd_offset_k(virtaddr);
		if (pgd_bad(*pgd_dir)) {
			printk("iocachemode: bad pgd(%08lx)\n", pgd_val(*pgd_dir));
			pgd_clear(pgd_dir);
			return;
		}
		pmd_dir = pmd_offset(pgd_dir, virtaddr);

		if (CPU_IS_020_OR_030) {
			int pmd_off = (virtaddr/PTRTREESIZE) & 15;

			if ((pmd_dir->pmd[pmd_off] & _DESCTYPE_MASK) == _PAGE_PRESENT) {
				pmd_dir->pmd[pmd_off] = (pmd_dir->pmd[pmd_off] &
							 _CACHEMASK040) | cmode;
				virtaddr += PTRTREESIZE;
				size -= PTRTREESIZE;
				continue;
			}
		}

		if (pmd_bad(*pmd_dir)) {
			printk("iocachemode: bad pmd (%08lx)\n", pmd_val(*pmd_dir));
			pmd_clear(pmd_dir);
			return;
		}
		pte_dir = pte_offset_kernel(pmd_dir, virtaddr);

		pte_val(*pte_dir) = (pte_val(*pte_dir) & _CACHEMASK040) | cmode;
		virtaddr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	flush_tlb_all();
}
