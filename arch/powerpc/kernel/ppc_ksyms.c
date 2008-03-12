#include <linux/module.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/elfcore.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/screen_info.h>
#include <linux/vt_kern.h>
#include <linux/nvram.h>
#include <linux/console.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/bitops.h>

#include <asm/page.h>
#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/checksum.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <linux/adb.h>
#include <linux/cuda.h>
#include <linux/pmu.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/pci-bridge.h>
#include <asm/irq.h>
#include <asm/pmac_feature.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/hw_irq.h>
#include <asm/nvram.h>
#include <asm/mmu_context.h>
#include <asm/backlight.h>
#include <asm/time.h>
#include <asm/cputable.h>
#include <asm/btext.h>
#include <asm/div64.h>
#include <asm/signal.h>
#include <asm/dcr.h>

#ifdef CONFIG_PPC64
EXPORT_SYMBOL(local_irq_restore);
#endif

#ifdef CONFIG_PPC32
extern void transfer_to_handler(void);
extern void do_IRQ(struct pt_regs *regs);
extern void machine_check_exception(struct pt_regs *regs);
extern void alignment_exception(struct pt_regs *regs);
extern void program_check_exception(struct pt_regs *regs);
extern void single_step_exception(struct pt_regs *regs);
extern int sys_sigreturn(struct pt_regs *regs);

EXPORT_SYMBOL(empty_zero_page);
EXPORT_SYMBOL(clear_pages);
EXPORT_SYMBOL(copy_page);
EXPORT_SYMBOL(ISA_DMA_THRESHOLD);
EXPORT_SYMBOL(DMA_MODE_READ);
EXPORT_SYMBOL(DMA_MODE_WRITE);

EXPORT_SYMBOL(transfer_to_handler);
EXPORT_SYMBOL(do_IRQ);
EXPORT_SYMBOL(machine_check_exception);
EXPORT_SYMBOL(alignment_exception);
EXPORT_SYMBOL(program_check_exception);
EXPORT_SYMBOL(single_step_exception);
EXPORT_SYMBOL(sys_sigreturn);
#endif

EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strcmp);

EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(csum_partial_copy_generic);
EXPORT_SYMBOL(ip_fast_csum);
EXPORT_SYMBOL(csum_tcpudp_magic);

EXPORT_SYMBOL(__copy_tofrom_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(__strnlen_user);
#ifdef CONFIG_PPC64
EXPORT_SYMBOL(copy_4K_page);
#endif

#if defined(CONFIG_PCI) && defined(CONFIG_PPC32)
EXPORT_SYMBOL(isa_io_base);
EXPORT_SYMBOL(isa_mem_base);
EXPORT_SYMBOL(pci_dram_offset);
EXPORT_SYMBOL(pci_alloc_consistent);
EXPORT_SYMBOL(pci_free_consistent);
#endif /* CONFIG_PCI */

EXPORT_SYMBOL(start_thread);
EXPORT_SYMBOL(kernel_thread);

EXPORT_SYMBOL(giveup_fpu);
#ifdef CONFIG_ALTIVEC
EXPORT_SYMBOL(giveup_altivec);
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_SPE
EXPORT_SYMBOL(giveup_spe);
#endif /* CONFIG_SPE */

#ifndef CONFIG_PPC64
EXPORT_SYMBOL(flush_instruction_cache);
EXPORT_SYMBOL(flush_tlb_kernel_range);
EXPORT_SYMBOL(flush_tlb_page);
EXPORT_SYMBOL(_tlbie);
#endif
EXPORT_SYMBOL(__flush_icache_range);
EXPORT_SYMBOL(flush_dcache_range);

#ifdef CONFIG_SMP
#ifdef CONFIG_PPC32
EXPORT_SYMBOL(smp_hw_index);
#endif
#endif

#ifdef CONFIG_ADB
EXPORT_SYMBOL(adb_request);
EXPORT_SYMBOL(adb_register);
EXPORT_SYMBOL(adb_unregister);
EXPORT_SYMBOL(adb_poll);
EXPORT_SYMBOL(adb_try_handler_change);
#endif /* CONFIG_ADB */
#ifdef CONFIG_ADB_CUDA
EXPORT_SYMBOL(cuda_request);
EXPORT_SYMBOL(cuda_poll);
#endif /* CONFIG_ADB_CUDA */
#ifdef CONFIG_VT
EXPORT_SYMBOL(kd_mksound);
#endif
EXPORT_SYMBOL(to_tm);

#ifdef CONFIG_PPC32
long long __ashrdi3(long long, int);
long long __ashldi3(long long, int);
long long __lshrdi3(long long, int);
EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__lshrdi3);
int __ucmpdi2(unsigned long long, unsigned long long);
EXPORT_SYMBOL(__ucmpdi2);
#endif

EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memchr);

#if defined(CONFIG_FB_VGA16_MODULE)
EXPORT_SYMBOL(screen_info);
#endif

#ifdef CONFIG_PPC32
EXPORT_SYMBOL(timer_interrupt);
EXPORT_SYMBOL(irq_desc);
EXPORT_SYMBOL(tb_ticks_per_jiffy);
EXPORT_SYMBOL(console_drivers);
EXPORT_SYMBOL(cacheable_memcpy);
#endif

#ifdef CONFIG_PPC32
EXPORT_SYMBOL(next_mmu_context);
EXPORT_SYMBOL(set_context);
#endif

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
