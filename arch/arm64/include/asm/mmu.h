/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_MMU_H
#define __ASM_MMU_H

typedef struct {
	unsigned int id;
	raw_spinlock_t id_lock;
	void *vdso;
} mm_context_t;

#define INIT_MM_CONTEXT(name) \
	.context.id_lock = __RAW_SPIN_LOCK_UNLOCKED(name.context.id_lock),

#define ASID(mm)	((mm)->context.id & 0xffff)

extern void paging_init(void);
extern void __iomem *early_io_map(phys_addr_t phys, unsigned long virt);
extern void init_mem_pgprot(void);
extern void create_pgd_mapping(struct mm_struct *mm, phys_addr_t phys,
			       unsigned long virt, phys_addr_t size,
			       pgprot_t prot);
extern void *fixmap_remap_fdt(phys_addr_t dt_phys);

#endif
