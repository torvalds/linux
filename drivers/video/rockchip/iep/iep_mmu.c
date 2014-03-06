/* 
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 */

#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <asm/cacheflush.h>
#include "iep_mmu.h"

#define RK_MMU_DTE_SHIFT    22
#define RK_MMU_DTE_MASK     (~0xFFFL)
#define RK_MMU_PTE_SHIFT    12
#define RK_MMU_PTE_MASK     (~0xFFFL)
#define RK_MMU_DTE_ENTRY_CNT    ((1) << (32-RK_MMU_DTE_SHIFT))
#define RK_MMU_PTE_ENTRY_CNT    ((1) << (RK_MMU_DTE_SHIFT - RK_MMU_PTE_SHIFT))

#define RK_MMU_PAGE_PRESENT                 (1<<0)
#define RK_MMU_READ_PERMISSION              (1<<1)
#define RK_MMU_WRITE_PERMISSION             (1<<2)
#define RK_MMU_OVERRIDE_CACHE_ATTRIBUTES    (0<<3)
#define RK_MMU_WRITE_CACHE_ABLE             (0<<4)
#define RK_MMU_WRITE_ALLOCABLE              (0<<5)
#define RK_MMU_WRITE_BUFFERABLE             (0<<6)
#define RK_MMU_READ_CACHE_ABLE              (0<<7)
#define RK_MMU_READ_ALLOCABLE               (0<<8)

#define RK_MMU_PTE_CTRL     (RK_MMU_PAGE_PRESENT | RK_MMU_READ_PERMISSION | \
                            RK_MMU_WRITE_PERMISSION | RK_MMU_OVERRIDE_CACHE_ATTRIBUTES | \
                            RK_MMU_WRITE_CACHE_ABLE | RK_MMU_WRITE_ALLOCABLE | \
                            RK_MMU_WRITE_BUFFERABLE | RK_MMU_READ_CACHE_ABLE | \
                            RK_MMU_READ_ALLOCABLE)

#define RK_MMU_DTE_CTRL     RK_MMU_PAGE_PRESENT

//#define RK_MMU_DEBUG

static int map_user_space_addr(struct task_struct *tsk,
                               uint32_t *pte_table,
                               uint32_t page_index,
                               uint32_t page_count)
{
    int result;
    int i;
    int status = 0;
    uint32_t phy_addr = 0;
    struct page **pages;

    pages = kzalloc((page_count + 1) * sizeof(struct page*), GFP_KERNEL);

    down_read(&tsk->mm->mmap_sem);
    result = get_user_pages(tsk, 
                            tsk->mm, 
                            page_index << PAGE_SHIFT, 
                            page_count,
                            1, 0, pages, NULL
                            );
    up_read(&tsk->mm->mmap_sem);

    if (result <= 0 || result < page_count) {
        struct vm_area_struct *vma;

        for(i=0; i<page_count; i++) {
            vma = find_vma(tsk->mm, (page_index + i) << PAGE_SHIFT);

            if (vma) {
                pte_t *pte;
                spinlock_t *ptl;
                unsigned long pfn;
                pgd_t *pgd;
                pud_t *pud;
                pmd_t *pmd;

                pgd = pgd_offset(tsk->mm, (page_index + i) << PAGE_SHIFT);

                if (pgd_none(*pgd) || pgd_bad(*pgd)) {
                    IEP_ERR("iep, invalid pgd\n");
                    status = -EIO;
                    break;
                }

                pud = pud_offset(pgd, (page_index + i) << PAGE_SHIFT);
                if (pud_none(*pud) || pud_bad(*pud)) {
                    IEP_ERR("iep, invalid pud\n");
                    status = -EIO;
                    break;
                }

                pmd = pmd_offset(pud, (page_index + i) << PAGE_SHIFT);
                if (pmd_none(*pmd) || pmd_bad(*pmd)) {
                    status = -EIO;
                    continue;
                }

                pte = pte_offset_map_lock(tsk->mm, pmd, (page_index + i) << PAGE_SHIFT, &ptl);
                if (pte_none(*pte)) {
                    pte_unmap_unlock(pte, ptl);
                    status = -EIO;
                    continue;
                }

                pfn = pte_pfn(*pte);
                phy_addr = ((pfn << PAGE_SHIFT) | (((unsigned long)((page_index + i) << PAGE_SHIFT)) & ~PAGE_MASK));
                pte_unmap_unlock(pte, ptl);

                pte_table[i] = (phy_addr & RK_MMU_PTE_MASK) | RK_MMU_PTE_CTRL;
            } else {
                status = -EIO;
                break;
            }
        }

    } else {
        /* fill the page table. */
        for(i=0; i<page_count; i++) {
            /* get the physical address from page struct. */
            pte_table[i] = (page_to_phys(pages[i]) & RK_MMU_PTE_MASK) | RK_MMU_PTE_CTRL;
        }
    }

    kfree(pages);

    return status;
}

int rk_mmu_generate_pte_from_va(iep_session *session, uint32_t va) 
{
    int i;
    int dte_index = va >> RK_MMU_DTE_SHIFT;
    struct rk_mmu_pte *pte_node = NULL, *n;


    if (session->dte_table[dte_index] != 0) {
        list_for_each_entry_safe(pte_node, n, &session->pte_list, session_link) {
            if (pte_node->index == dte_index) {
                // a incomplete pte.
#ifdef RK_MMU_DEBUG
                uint32_t phy_addr;
                uint32_t dte_addr;
                uint32_t *pte_table;
                uint32_t pte_addr;

                int pte_index = (va >> PAGE_SHIFT) & 0x3FF;
                int page_index = va & 0xFFF;

                dte_addr = session->dte_table[dte_index];
                IEP_DBG("dte_addr = %08x\n", dte_addr);

                pte_table = phys_to_virt(dte_addr & RK_MMU_DTE_MASK);

                pte_addr = pte_table[pte_index];

                IEP_DBG("pte_addr = %08x\n", pte_addr);

                phy_addr = (pte_addr & RK_MMU_PTE_MASK) | page_index;

                IEP_DBG("phy %08x\n", phy_addr);
#endif
                IEP_DBG("Incomplete pte\n");
                break;
            }
        }
    }

    if (pte_node == NULL || pte_node->index != dte_index) {
        // pte node is absence
        pte_node = (struct rk_mmu_pte*)kzalloc(sizeof(struct rk_mmu_pte), GFP_KERNEL);
        pte_node->pte = (uint32_t*)kzalloc(sizeof(uint32_t) * RK_MMU_PTE_ENTRY_CNT, GFP_KERNEL);
        pte_node->index = dte_index;
    }
    
    IEP_DBG("va = %08x\n", va);

    if (va < PAGE_OFFSET) {
        map_user_space_addr(session->tsk, pte_node->pte, (va >> PAGE_SHIFT) & (~0x3FFL), RK_MMU_PTE_ENTRY_CNT);
    } else {
        for (i=0; i<RK_MMU_PTE_ENTRY_CNT; i++) {
            pte_node->pte[i] = (uint32_t)(virt_to_phys((uint32_t*)((va + i) << PAGE_SHIFT)) & RK_MMU_PTE_MASK) | RK_MMU_PTE_CTRL;
        }
    }

    IEP_DBG("pa = %08x\n", (uint32_t)((pte_node->pte[(va>>PAGE_SHIFT) & 0x3FFL] & RK_MMU_PTE_MASK) | (va & 0xFFFL)));

    INIT_LIST_HEAD(&pte_node->session_link);
    list_add_tail(&pte_node->session_link, &session->pte_list);

    dmac_flush_range(&pte_node->pte[0], &pte_node->pte[RK_MMU_PTE_ENTRY_CNT-1]);
    outer_flush_range(virt_to_phys(&pte_node->pte[0]),virt_to_phys(&pte_node->pte[RK_MMU_PTE_ENTRY_CNT-1]));

    session->dte_table[pte_node->index] = (uint32_t)(virt_to_phys((uint32_t*)pte_node->pte) & RK_MMU_DTE_MASK) | RK_MMU_DTE_CTRL;

    dmac_flush_range(&session->dte_table[pte_node->index], &session->dte_table[pte_node->index+1]);
    outer_flush_range(virt_to_phys(&session->dte_table[pte_node->index]),virt_to_phys(&session->dte_table[pte_node->index+1]));

    return 0;
}

void rk_mmu_reclaim_pte_list(iep_session *session)
{
    struct rk_mmu_pte *pte_node, *n;

    list_for_each_entry_safe(pte_node, n, &session->pte_list, session_link) {
        list_del_init(&pte_node->session_link);
        kfree(pte_node->pte);
        kfree(pte_node);
    }
}

/// don't call this function in interupt service.
int rk_mmu_init_dte_table(iep_session *session)
{
    session->tsk = current;
    session->dte_table = (uint32_t*)kzalloc(sizeof(uint32_t) * RK_MMU_DTE_ENTRY_CNT, GFP_KERNEL);

    return 0;
}

void rk_mmu_deinit_dte_table(iep_session *session)
{
    rk_mmu_reclaim_pte_list(session);
    kfree(session->dte_table);
}

