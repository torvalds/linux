#include <linux/config.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/mca.h>
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/pm.h>
#include <linux/pci.h>
#include <linux/apm_bios.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/highmem.h>
#include <linux/time.h>

#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/mmx.h>
#include <asm/desc.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/nmi.h>
#include <asm/ist.h>
#include <asm/kdebug.h>

extern void dump_thread(struct pt_regs *, struct user *);
extern spinlock_t rtc_lock;

/* This is definitely a GPL-only symbol */
EXPORT_SYMBOL_GPL(cpu_gdt_table);

#if defined(CONFIG_APM_MODULE)
extern void machine_real_restart(unsigned char *, int);
EXPORT_SYMBOL(machine_real_restart);
extern void default_idle(void);
EXPORT_SYMBOL(default_idle);
#endif

#ifdef CONFIG_SMP
extern void FASTCALL( __write_lock_failed(rwlock_t *rw));
extern void FASTCALL( __read_lock_failed(rwlock_t *rw));
#endif

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_HD) || defined(CONFIG_BLK_DEV_IDE_MODULE) || defined(CONFIG_BLK_DEV_HD_MODULE)
extern struct drive_info_struct drive_info;
EXPORT_SYMBOL(drive_info);
#endif

extern unsigned long cpu_khz;
extern unsigned long get_cmos_time(void);

/* platform dependent support */
EXPORT_SYMBOL(boot_cpu_data);
#ifdef CONFIG_DISCONTIGMEM
EXPORT_SYMBOL(node_data);
EXPORT_SYMBOL(physnode_map);
#endif
#ifdef CONFIG_X86_NUMAQ
EXPORT_SYMBOL(xquad_portio);
#endif
EXPORT_SYMBOL(dump_thread);
EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL_GPL(kernel_fpu_begin);
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(ioremap_nocache);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(pm_idle);
EXPORT_SYMBOL(pm_power_off);
EXPORT_SYMBOL(get_cmos_time);
EXPORT_SYMBOL(cpu_khz);
EXPORT_SYMBOL(apm_info);

EXPORT_SYMBOL(__down_failed);
EXPORT_SYMBOL(__down_failed_interruptible);
EXPORT_SYMBOL(__down_failed_trylock);
EXPORT_SYMBOL(__up_wakeup);
/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial_copy_generic);
/* Delay loops */
EXPORT_SYMBOL(__ndelay);
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__delay);
EXPORT_SYMBOL(__const_udelay);

EXPORT_SYMBOL(__get_user_1);
EXPORT_SYMBOL(__get_user_2);
EXPORT_SYMBOL(__get_user_4);

EXPORT_SYMBOL(__put_user_1);
EXPORT_SYMBOL(__put_user_2);
EXPORT_SYMBOL(__put_user_4);
EXPORT_SYMBOL(__put_user_8);

EXPORT_SYMBOL(strpbrk);
EXPORT_SYMBOL(strstr);

EXPORT_SYMBOL(strncpy_from_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(clear_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(__copy_from_user_ll);
EXPORT_SYMBOL(__copy_to_user_ll);
EXPORT_SYMBOL(strnlen_user);

EXPORT_SYMBOL(dma_alloc_coherent);
EXPORT_SYMBOL(dma_free_coherent);

#ifdef CONFIG_PCI
EXPORT_SYMBOL(pci_mem_start);
#endif

#ifdef CONFIG_PCI_BIOS
EXPORT_SYMBOL(pcibios_set_irq_routing);
EXPORT_SYMBOL(pcibios_get_irq_routing_table);
#endif

#ifdef CONFIG_X86_USE_3DNOW
EXPORT_SYMBOL(_mmx_memcpy);
EXPORT_SYMBOL(mmx_clear_page);
EXPORT_SYMBOL(mmx_copy_page);
#endif

#ifdef CONFIG_X86_HT
EXPORT_SYMBOL(smp_num_siblings);
EXPORT_SYMBOL(cpu_sibling_map);
#endif

#ifdef CONFIG_SMP
EXPORT_SYMBOL(cpu_data);
EXPORT_SYMBOL(cpu_online_map);
EXPORT_SYMBOL(cpu_callout_map);
EXPORT_SYMBOL(__write_lock_failed);
EXPORT_SYMBOL(__read_lock_failed);

/* Global SMP stuff */
EXPORT_SYMBOL(smp_call_function);

/* TLB flushing */
EXPORT_SYMBOL(flush_tlb_page);
#endif

#ifdef CONFIG_X86_IO_APIC
EXPORT_SYMBOL(IO_APIC_get_PCI_irq_vector);
#endif

#ifdef CONFIG_MCA
EXPORT_SYMBOL(machine_id);
#endif

#ifdef CONFIG_VT
EXPORT_SYMBOL(screen_info);
#endif

EXPORT_SYMBOL(get_wchan);

EXPORT_SYMBOL(rtc_lock);

EXPORT_SYMBOL_GPL(set_nmi_callback);
EXPORT_SYMBOL_GPL(unset_nmi_callback);

EXPORT_SYMBOL(register_die_notifier);
#ifdef CONFIG_HAVE_DEC_LOCK
EXPORT_SYMBOL(_atomic_dec_and_lock);
#endif

EXPORT_SYMBOL(__PAGE_KERNEL);

#ifdef CONFIG_HIGHMEM
EXPORT_SYMBOL(kmap);
EXPORT_SYMBOL(kunmap);
EXPORT_SYMBOL(kmap_atomic);
EXPORT_SYMBOL(kunmap_atomic);
EXPORT_SYMBOL(kmap_atomic_to_page);
#endif

#if defined(CONFIG_X86_SPEEDSTEP_SMI) || defined(CONFIG_X86_SPEEDSTEP_SMI_MODULE)
EXPORT_SYMBOL(ist_info);
#endif

EXPORT_SYMBOL(csum_partial);
