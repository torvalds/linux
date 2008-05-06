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
#include <linux/pm.h>
#include <linux/bitops.h>

#include <asm/page.h>
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
#include <asm/system.h>
#include <asm/pci-bridge.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/hw_irq.h>
#include <asm/nvram.h>
#include <asm/mmu_context.h>
#include <asm/backlight.h>
#include <asm/time.h>
#include <asm/cputable.h>
#include <asm/btext.h>
#include <asm/xmon.h>
#include <asm/signal.h>
#include <asm/dcr.h>

#ifdef  CONFIG_8xx
#include <asm/cpm1.h>
#endif

extern void transfer_to_handler(void);
extern void do_IRQ(struct pt_regs *regs);
extern void machine_check_exception(struct pt_regs *regs);
extern void alignment_exception(struct pt_regs *regs);
extern void program_check_exception(struct pt_regs *regs);
extern void single_step_exception(struct pt_regs *regs);
extern int sys_sigreturn(struct pt_regs *regs);

long long __ashrdi3(long long, int);
long long __ashldi3(long long, int);
long long __lshrdi3(long long, int);

EXPORT_SYMBOL(clear_pages);
EXPORT_SYMBOL(clear_user_page);
EXPORT_SYMBOL(transfer_to_handler);
EXPORT_SYMBOL(do_IRQ);
EXPORT_SYMBOL(machine_check_exception);
EXPORT_SYMBOL(alignment_exception);
EXPORT_SYMBOL(program_check_exception);
EXPORT_SYMBOL(single_step_exception);
EXPORT_SYMBOL(sys_sigreturn);
EXPORT_SYMBOL(ppc_n_lost_interrupts);

EXPORT_SYMBOL(ISA_DMA_THRESHOLD);
EXPORT_SYMBOL(DMA_MODE_READ);
EXPORT_SYMBOL(DMA_MODE_WRITE);

#if !defined(__INLINE_BITOPS)
EXPORT_SYMBOL(set_bit);
EXPORT_SYMBOL(clear_bit);
EXPORT_SYMBOL(change_bit);
EXPORT_SYMBOL(test_and_set_bit);
EXPORT_SYMBOL(test_and_clear_bit);
EXPORT_SYMBOL(test_and_change_bit);
#endif /* __INLINE_BITOPS */

EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strncmp);

EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(csum_partial_copy_generic);
EXPORT_SYMBOL(ip_fast_csum);
EXPORT_SYMBOL(csum_tcpudp_magic);

EXPORT_SYMBOL(__copy_tofrom_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(__strnlen_user);

/*
EXPORT_SYMBOL(inb);
EXPORT_SYMBOL(inw);
EXPORT_SYMBOL(inl);
EXPORT_SYMBOL(outb);
EXPORT_SYMBOL(outw);
EXPORT_SYMBOL(outl);
EXPORT_SYMBOL(outsl);*/

EXPORT_SYMBOL(_insb);
EXPORT_SYMBOL(_outsb);
EXPORT_SYMBOL(_insw_ns);
EXPORT_SYMBOL(_outsw_ns);
EXPORT_SYMBOL(_insl_ns);
EXPORT_SYMBOL(_outsl_ns);
EXPORT_SYMBOL(iopa);
EXPORT_SYMBOL(ioremap);
#ifdef CONFIG_44x
EXPORT_SYMBOL(ioremap64);
#endif
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(ioremap_bot);	/* aka VMALLOC_END */

#ifdef CONFIG_PCI
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
extern void flush_dcache_all(void);
EXPORT_SYMBOL(flush_dcache_all);
#endif

EXPORT_SYMBOL(start_thread);
EXPORT_SYMBOL(kernel_thread);

EXPORT_SYMBOL(flush_instruction_cache);
EXPORT_SYMBOL(giveup_fpu);
EXPORT_SYMBOL(__flush_icache_range);
EXPORT_SYMBOL(flush_dcache_range);
EXPORT_SYMBOL(flush_icache_user_range);
EXPORT_SYMBOL(flush_dcache_page);
EXPORT_SYMBOL(flush_tlb_kernel_range);
EXPORT_SYMBOL(flush_tlb_page);
EXPORT_SYMBOL(_tlbie);
#ifdef CONFIG_ALTIVEC
#ifndef CONFIG_SMP
EXPORT_SYMBOL(last_task_used_altivec);
#endif
EXPORT_SYMBOL(giveup_altivec);
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_SMP
EXPORT_SYMBOL(smp_call_function);
EXPORT_SYMBOL(smp_hw_index);
#endif

EXPORT_SYMBOL(ppc_md);

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
#if defined(CONFIG_BOOTX_TEXT)
EXPORT_SYMBOL(btext_update_display);
#endif
EXPORT_SYMBOL(to_tm);

EXPORT_SYMBOL(pm_power_off);

EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(cacheable_memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memchr);

#if defined(CONFIG_FB_VGA16_MODULE)
EXPORT_SYMBOL(screen_info);
#endif

EXPORT_SYMBOL(__delay);
EXPORT_SYMBOL(timer_interrupt);
EXPORT_SYMBOL(irq_desc);
EXPORT_SYMBOL(tb_ticks_per_jiffy);
EXPORT_SYMBOL(console_drivers);
#ifdef CONFIG_XMON
EXPORT_SYMBOL(xmon);
EXPORT_SYMBOL(xmon_printf);
#endif

#if defined(CONFIG_KGDB) || defined(CONFIG_XMON)
extern void (*debugger)(struct pt_regs *regs);
extern int (*debugger_bpt)(struct pt_regs *regs);
extern int (*debugger_sstep)(struct pt_regs *regs);
extern int (*debugger_iabr_match)(struct pt_regs *regs);
extern int (*debugger_dabr_match)(struct pt_regs *regs);
extern void (*debugger_fault_handler)(struct pt_regs *regs);

EXPORT_SYMBOL(debugger);
EXPORT_SYMBOL(debugger_bpt);
EXPORT_SYMBOL(debugger_sstep);
EXPORT_SYMBOL(debugger_iabr_match);
EXPORT_SYMBOL(debugger_dabr_match);
EXPORT_SYMBOL(debugger_fault_handler);
#endif

#ifdef  CONFIG_8xx
EXPORT_SYMBOL(cpm_install_handler);
EXPORT_SYMBOL(cpm_free_handler);
#endif /* CONFIG_8xx */
#if defined(CONFIG_8xx) || defined(CONFIG_40x)
EXPORT_SYMBOL(__res);
#endif

EXPORT_SYMBOL(next_mmu_context);
EXPORT_SYMBOL(set_context);
EXPORT_SYMBOL(disarm_decr);
#ifdef CONFIG_PPC_STD_MMU
extern long mol_trampoline;
EXPORT_SYMBOL(mol_trampoline); /* For MOL */
EXPORT_SYMBOL(flush_hash_pages); /* For MOL */
#ifdef CONFIG_SMP
extern int mmu_hash_lock;
EXPORT_SYMBOL(mmu_hash_lock); /* For MOL */
#endif /* CONFIG_SMP */
extern long *intercept_table;
EXPORT_SYMBOL(intercept_table);
#endif /* CONFIG_PPC_STD_MMU */
#ifdef CONFIG_PPC_DCR_NATIVE
EXPORT_SYMBOL(__mtdcr);
EXPORT_SYMBOL(__mfdcr);
#endif
