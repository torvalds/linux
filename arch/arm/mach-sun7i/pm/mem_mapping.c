/*
 * Hibernation support specific for ARM
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Copyright (C) 2006 Rafael J. Wysocki <rjw <at> sisk.pl>
 *
 * Contact: Hiroshi DOYU <Hiroshi.DOYU <at> nokia.com>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <linux/power/aw_pm.h>
#include <linux/module.h>
#include <asm/io.h>
#include <asm/mach/map.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>
#include "pm_i.h"

/*
 * "Linux" PTE definitions.
 *
 * We keep two sets of PTEs - the hardware and the linux version.
 * This allows greater flexibility in the way we map the Linux bits
 * onto the hardware tables, and allows us to have YOUNG and DIRTY
 * bits.
 *
 * The PTE table pointer refers to the hardware entries; the "Linux"
 * entries are stored 1024 bytes below.
 */

#define L_PTE_WRITE		(1 << 7)
#define L_PTE_EXEC		(1 << 9)
#define PAGE_TBL_ADDR 		(0xc0004000)

struct saved_mmu_level_one {
	u32 vaddr;
	u32 entry_val;
};

/* Used in mem_context_asm.S */
#define SYS_CONTEXT_SIZE (2)
#define SVC_CONTEXT_SIZE (2)	//do not need backup r14? reason?
#define FIQ_CONTEXT_SIZE (7)
#define ABT_CONTEXT_SIZE (2 )
#define IRQ_CONTEXT_SIZE (2)
#define UND_CONTEXT_SIZE (2)
#define MON_CONTEXT_SIZE (2)

#define EMPTY_CONTEXT_SIZE (11 * sizeof(u32))


unsigned long saved_context_r13_sys[SYS_CONTEXT_SIZE];
unsigned long saved_cpsr_svc;
unsigned long saved_context_r12_svc[SVC_CONTEXT_SIZE];
unsigned long saved_spsr_svc;   
unsigned long saved_context_r13_fiq[FIQ_CONTEXT_SIZE];
unsigned long saved_spsr_fiq;
unsigned long saved_context_r13_abt[ABT_CONTEXT_SIZE];
unsigned long saved_spsr_abt;
unsigned long saved_context_r13_irq[IRQ_CONTEXT_SIZE];
unsigned long saved_spsr_irq;
unsigned long saved_context_r13_und[UND_CONTEXT_SIZE];
unsigned long saved_spsr_und;
unsigned long saved_context_r13_mon[MON_CONTEXT_SIZE];
unsigned long saved_spsr_mon;
unsigned long saved_empty_context_svc[EMPTY_CONTEXT_SIZE];

static struct saved_mmu_level_one backup_tbl[1];

/* References to section boundaries */
extern const void __nosave_begin, __nosave_end;

struct mem_type {
	unsigned int prot_pte;
	unsigned int prot_l1;
	unsigned int prot_sect;
	unsigned int domain;
};


#define PROT_PTE_DEVICE		L_PTE_PRESENT|L_PTE_YOUNG|L_PTE_DIRTY|L_PTE_WRITE
#define PROT_SECT_DEVICE	PMD_TYPE_SECT|PMD_SECT_AP_WRITE



/*
 * Create the page directory entries for 0x0000,0000 <-> 0x0000,0000
 */
void create_mapping(void)
{
	//busy_waiting();
	//__cpuc_flush_kern_all();
	//__cpuc_flush_user_all();
	*((volatile __u32 *)(PAGE_TBL_ADDR)) = 0xc4a;
	//clean cache
	__cpuc_flush_dcache_area((void *)(PAGE_TBL_ADDR), (long unsigned int)(PAGE_TBL_ADDR + 64*(sizeof(u32))));
#if 0
	__cpuc_coherent_user_range((long unsigned int)(PAGE_TBL_ADDR), (long unsigned int)(PAGE_TBL_ADDR + 64*(sizeof(u32))));
	__cpuc_coherent_kern_range((long unsigned int)(PAGE_TBL_ADDR), (long unsigned int)(PAGE_TBL_ADDR + 64*(sizeof(u32))));
	//v7_dma_clean_range((long unsigned int)(PAGE_TBL_ADDR), (long unsigned int)(PAGE_TBL_ADDR + (sizeof(u32))));
	dmac_flush_range((long unsigned int)(PAGE_TBL_ADDR), (long unsigned int)(PAGE_TBL_ADDR + 64*(sizeof(u32))));
	//actually, not need, just for test.
	//flush_tlb_all();
#endif
	return;
}

/**save the va: 0x0000,0000 mapping. 
*@vaddr: the va of mmu mapping to save;
*/
void save_mapping(unsigned long vaddr)
{
	unsigned long addr;

	//busy_waiting();
	addr = vaddr & PAGE_MASK;

	//__cpuc_flush_kern_all();
	backup_tbl[0].vaddr = addr;
	backup_tbl[0].entry_val = *((volatile __u32 *)(PAGE_TBL_ADDR));
	//flush_tlb_all();

	return;
}

/**restore the va: 0x0000,0000 mapping. 
*@vaddr: the va of mmu mapping to restore.
*
*/
void restore_mapping(unsigned long vaddr)
{
	unsigned long addr;
	
	addr = vaddr & PAGE_MASK;
	
	if(addr != backup_tbl[0].vaddr){
		while(1);
		return;
	}
	
	//__cpuc_flush_kern_all();
	*((volatile __u32 *)(PAGE_TBL_ADDR)) = backup_tbl[0].entry_val;
	//clean cache
	__cpuc_coherent_user_range((long unsigned int)(PAGE_TBL_ADDR), (long unsigned int)(PAGE_TBL_ADDR + (sizeof(u32)) - 1));
	flush_tlb_all();

	return;
}

void init_pgd(unsigned int *pgd)
{
    int i = 0;
    volatile __u32 *tmp;
    memcpy(pgd, (void*)0xc0004000, 0x4000);
	*((volatile __u32 *)(pgd)) = 0xc4a;     //0x0000,0000 <-> 0x0000,0000
    tmp = pgd; 
	for (i = 0; i < 1024; i++)        //vaddr:0x0000,0000 <-> paddr:0x0000,0000
	{
        *((volatile __u32 *)(tmp++)) = (0xc4a|(i<<20));
	}
    tmp = pgd + 1 * 1024; 
	for (i = 0; i < 1024; i++)        //vaddr:0x4000,0000 <-> paddr:0x4000,0000
	{
        *((volatile __u32 *)(tmp++)) = (0xc4a|(0x40000000)|(i<<20));
	}
	__cpuc_coherent_kern_range((long unsigned int)(pgd), (long unsigned int)(pgd + 0x4000 - 1));
}

