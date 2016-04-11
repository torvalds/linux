#include <linux/export.h>
#include <linux/smp.h>

#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/hw_irq.h>
#include <asm/time.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/dcr.h>

EXPORT_SYMBOL(ISA_DMA_THRESHOLD);
EXPORT_SYMBOL(DMA_MODE_READ);
EXPORT_SYMBOL(DMA_MODE_WRITE);

#if defined(CONFIG_PCI)
EXPORT_SYMBOL(isa_io_base);
EXPORT_SYMBOL(isa_mem_base);
EXPORT_SYMBOL(pci_dram_offset);
#endif

#ifdef CONFIG_SMP
EXPORT_SYMBOL(smp_hw_index);
#endif

long long __ashrdi3(long long, int);
long long __ashldi3(long long, int);
long long __lshrdi3(long long, int);
int __ucmpdi2(unsigned long long, unsigned long long);
int __cmpdi2(long long, long long);
EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(__ucmpdi2);
EXPORT_SYMBOL(__cmpdi2);

EXPORT_SYMBOL(timer_interrupt);
EXPORT_SYMBOL(tb_ticks_per_jiffy);

EXPORT_SYMBOL(switch_mmu_context);

#ifdef CONFIG_PPC_STD_MMU_32
extern long mol_trampoline;
EXPORT_SYMBOL(mol_trampoline); /* For MOL */
EXPORT_SYMBOL(flush_hash_pages); /* For MOL */
#ifdef CONFIG_SMP
extern int mmu_hash_lock;
EXPORT_SYMBOL(mmu_hash_lock); /* For MOL */
#endif /* CONFIG_SMP */
extern long *intercept_table;
EXPORT_SYMBOL(intercept_table);
#endif /* CONFIG_PPC_STD_MMU_32 */

#ifdef CONFIG_PPC_DCR_NATIVE
EXPORT_SYMBOL(__mtdcr);
EXPORT_SYMBOL(__mfdcr);
#endif

EXPORT_SYMBOL(flush_instruction_cache);
