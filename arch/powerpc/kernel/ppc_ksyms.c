#include <linux/config.h>
#include <linux/module.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/elfcore.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>
#include <linux/nvram.h>
#include <linux/console.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/bitops.h>

#include <asm/page.h>
#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/ide.h>
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

#ifdef  CONFIG_8xx
#include <asm/commproc.h>
#endif

#ifdef CONFIG_PPC32
extern void transfer_to_handler(void);
extern void do_IRQ(struct pt_regs *regs);
extern void machine_check_exception(struct pt_regs *regs);
extern void alignment_exception(struct pt_regs *regs);
extern void program_check_exception(struct pt_regs *regs);
extern void single_step_exception(struct pt_regs *regs);
extern int pmac_newworld;
extern int sys_sigreturn(struct pt_regs *regs);

EXPORT_SYMBOL(clear_pages);
EXPORT_SYMBOL(ISA_DMA_THRESHOLD);
EXPORT_SYMBOL(DMA_MODE_READ);
EXPORT_SYMBOL(DMA_MODE_WRITE);
EXPORT_SYMBOL(__div64_32);

EXPORT_SYMBOL(do_signal);
EXPORT_SYMBOL(transfer_to_handler);
EXPORT_SYMBOL(do_IRQ);
EXPORT_SYMBOL(machine_check_exception);
EXPORT_SYMBOL(alignment_exception);
EXPORT_SYMBOL(program_check_exception);
EXPORT_SYMBOL(single_step_exception);
EXPORT_SYMBOL(sys_sigreturn);
#endif

#if defined(CONFIG_PPC_PREP)
EXPORT_SYMBOL(_prep_type);
EXPORT_SYMBOL(ucSystemType);
#endif

EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strncat);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(strpbrk);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(strcasecmp);

EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(csum_partial_copy_generic);
EXPORT_SYMBOL(ip_fast_csum);
EXPORT_SYMBOL(csum_tcpudp_magic);

EXPORT_SYMBOL(__copy_tofrom_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(__strnlen_user);

EXPORT_SYMBOL(_insb);
EXPORT_SYMBOL(_outsb);
EXPORT_SYMBOL(_insw);
EXPORT_SYMBOL(_outsw);
EXPORT_SYMBOL(_insl);
EXPORT_SYMBOL(_outsl);
EXPORT_SYMBOL(_insw_ns);
EXPORT_SYMBOL(_outsw_ns);
EXPORT_SYMBOL(_insl_ns);
EXPORT_SYMBOL(_outsl_ns);
EXPORT_SYMBOL(ioremap);
#ifdef CONFIG_44x
EXPORT_SYMBOL(ioremap64);
#endif
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(iounmap);
#ifdef CONFIG_PPC32
EXPORT_SYMBOL(ioremap_bot);	/* aka VMALLOC_END */
#endif

#if defined(CONFIG_PPC32) && (defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE))
EXPORT_SYMBOL(ppc_ide_md);
#endif

#if defined(CONFIG_PCI) && defined(CONFIG_PPC32)
EXPORT_SYMBOL(isa_io_base);
EXPORT_SYMBOL(isa_mem_base);
EXPORT_SYMBOL(pci_dram_offset);
EXPORT_SYMBOL(pci_alloc_consistent);
EXPORT_SYMBOL(pci_free_consistent);
EXPORT_SYMBOL(pci_bus_io_base);
EXPORT_SYMBOL(pci_bus_io_base_phys);
EXPORT_SYMBOL(pci_bus_mem_base_phys);
EXPORT_SYMBOL(pci_bus_to_hose);
EXPORT_SYMBOL(pci_resource_to_bus);
EXPORT_SYMBOL(pci_phys_to_bus);
EXPORT_SYMBOL(pci_bus_to_phys);
#endif /* CONFIG_PCI */

#ifdef CONFIG_NOT_COHERENT_CACHE
EXPORT_SYMBOL(flush_dcache_all);
#endif

EXPORT_SYMBOL(start_thread);
EXPORT_SYMBOL(kernel_thread);

EXPORT_SYMBOL(giveup_fpu);
#ifdef CONFIG_ALTIVEC
EXPORT_SYMBOL(giveup_altivec);
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_SPE
EXPORT_SYMBOL(giveup_spe);
#endif /* CONFIG_SPE */

#ifdef CONFIG_PPC64
EXPORT_SYMBOL(__flush_icache_range);
#else
EXPORT_SYMBOL(flush_instruction_cache);
EXPORT_SYMBOL(flush_icache_range);
EXPORT_SYMBOL(flush_tlb_kernel_range);
EXPORT_SYMBOL(flush_tlb_page);
EXPORT_SYMBOL(_tlbie);
#endif
EXPORT_SYMBOL(flush_dcache_range);

#ifdef CONFIG_SMP
EXPORT_SYMBOL(smp_call_function);
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
#ifdef CONFIG_PPC_PMAC
EXPORT_SYMBOL(sys_ctrler);
#endif
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
#endif

EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memscan);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memchr);

#if defined(CONFIG_FB_VGA16_MODULE)
EXPORT_SYMBOL(screen_info);
#endif

#ifdef CONFIG_PPC32
EXPORT_SYMBOL(__delay);
EXPORT_SYMBOL(timer_interrupt);
EXPORT_SYMBOL(irq_desc);
EXPORT_SYMBOL(tb_ticks_per_jiffy);
EXPORT_SYMBOL(console_drivers);
EXPORT_SYMBOL(cacheable_memcpy);
#endif

EXPORT_SYMBOL(__up);
EXPORT_SYMBOL(__down);
EXPORT_SYMBOL(__down_interruptible);

#ifdef  CONFIG_8xx
EXPORT_SYMBOL(cpm_install_handler);
EXPORT_SYMBOL(cpm_free_handler);
#endif /* CONFIG_8xx */
#if defined(CONFIG_8xx) || defined(CONFIG_40x) || defined(CONFIG_85xx) ||\
	defined(CONFIG_83xx)
EXPORT_SYMBOL(__res);
#endif

#ifdef CONFIG_PPC32
EXPORT_SYMBOL(next_mmu_context);
EXPORT_SYMBOL(set_context);
#endif

#ifdef CONFIG_PPC_STD_MMU_32
extern long mol_trampoline;
EXPORT_SYMBOL(mol_trampoline); /* For MOL */
EXPORT_SYMBOL(flush_hash_pages); /* For MOL */
EXPORT_SYMBOL_GPL(__handle_mm_fault); /* For MOL */
#ifdef CONFIG_SMP
extern int mmu_hash_lock;
EXPORT_SYMBOL(mmu_hash_lock); /* For MOL */
#endif /* CONFIG_SMP */
extern long *intercept_table;
EXPORT_SYMBOL(intercept_table);
#endif /* CONFIG_PPC_STD_MMU_32 */
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
EXPORT_SYMBOL(__mtdcr);
EXPORT_SYMBOL(__mfdcr);
#endif
