#include <linux/config.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/user.h>
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/pm.h>
#include <linux/pci.h>
#include <linux/apm_bios.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/tty.h>

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
#include <asm/pgalloc.h>
#include <asm/nmi.h>
#include <asm/kdebug.h>
#include <asm/unistd.h>
#include <asm/tlbflush.h>
#include <asm/kdebug.h>

extern spinlock_t rtc_lock;

#ifdef CONFIG_SMP
extern void __write_lock_failed(rwlock_t *rw);
extern void __read_lock_failed(rwlock_t *rw);
#endif

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_HD) || defined(CONFIG_BLK_DEV_IDE_MODULE) || defined(CONFIG_BLK_DEV_HD_MODULE)
extern struct drive_info_struct drive_info;
EXPORT_SYMBOL(drive_info);
#endif

/* platform dependent support */
EXPORT_SYMBOL(boot_cpu_data);
//EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(ioremap_nocache);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(pm_idle);
EXPORT_SYMBOL(pm_power_off);

EXPORT_SYMBOL(__down_failed);
EXPORT_SYMBOL(__down_failed_interruptible);
EXPORT_SYMBOL(__down_failed_trylock);
EXPORT_SYMBOL(__up_wakeup);
/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial_copy_nocheck);
EXPORT_SYMBOL(ip_compute_csum);
/* Delay loops */
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__ndelay);
EXPORT_SYMBOL(__delay);
EXPORT_SYMBOL(__const_udelay);

EXPORT_SYMBOL(__get_user_1);
EXPORT_SYMBOL(__get_user_2);
EXPORT_SYMBOL(__get_user_4);
EXPORT_SYMBOL(__get_user_8);
EXPORT_SYMBOL(__put_user_1);
EXPORT_SYMBOL(__put_user_2);
EXPORT_SYMBOL(__put_user_4);
EXPORT_SYMBOL(__put_user_8);

EXPORT_SYMBOL(strncpy_from_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(clear_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(copy_user_generic);
EXPORT_SYMBOL(copy_from_user);
EXPORT_SYMBOL(copy_to_user);
EXPORT_SYMBOL(copy_in_user);
EXPORT_SYMBOL(strnlen_user);

#ifdef CONFIG_PCI
EXPORT_SYMBOL(pci_mem_start);
#endif

EXPORT_SYMBOL(copy_page);
EXPORT_SYMBOL(clear_page);

EXPORT_SYMBOL(_cpu_pda);
#ifdef CONFIG_SMP
EXPORT_SYMBOL(cpu_data);
EXPORT_SYMBOL(__write_lock_failed);
EXPORT_SYMBOL(__read_lock_failed);

EXPORT_SYMBOL(smp_call_function);
EXPORT_SYMBOL(cpu_callout_map);
#endif

#ifdef CONFIG_VT
EXPORT_SYMBOL(screen_info);
#endif

EXPORT_SYMBOL(get_wchan);

EXPORT_SYMBOL(rtc_lock);

EXPORT_SYMBOL_GPL(set_nmi_callback);
EXPORT_SYMBOL_GPL(unset_nmi_callback);

/* Export string functions. We normally rely on gcc builtin for most of these,
   but gcc sometimes decides not to inline them. */    
#undef memcpy
#undef memset
#undef memmove
#undef strlen

extern void * memset(void *,int,__kernel_size_t);
extern size_t strlen(const char *);
extern void * memmove(void * dest,const void *src,size_t count);
extern void * memcpy(void *,const void *,__kernel_size_t);
extern void * __memcpy(void *,const void *,__kernel_size_t);

EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(__memcpy);

#ifdef CONFIG_RWSEM_XCHGADD_ALGORITHM
/* prototypes are wrong, these are assembly with custom calling functions */
extern void rwsem_down_read_failed_thunk(void);
extern void rwsem_wake_thunk(void);
extern void rwsem_downgrade_thunk(void);
extern void rwsem_down_write_failed_thunk(void);
EXPORT_SYMBOL(rwsem_down_read_failed_thunk);
EXPORT_SYMBOL(rwsem_wake_thunk);
EXPORT_SYMBOL(rwsem_downgrade_thunk);
EXPORT_SYMBOL(rwsem_down_write_failed_thunk);
#endif

EXPORT_SYMBOL(empty_zero_page);

EXPORT_SYMBOL(die_chain);
EXPORT_SYMBOL(register_die_notifier);

#ifdef CONFIG_SMP
EXPORT_SYMBOL(cpu_sibling_map);
EXPORT_SYMBOL(smp_num_siblings);
#endif

extern void do_softirq_thunk(void);
EXPORT_SYMBOL(do_softirq_thunk);

#ifdef CONFIG_BUG
EXPORT_SYMBOL(out_of_line_bug);
#endif

EXPORT_SYMBOL(init_level4_pgt);

extern unsigned long __supported_pte_mask;
EXPORT_SYMBOL(__supported_pte_mask);

#ifdef CONFIG_SMP
EXPORT_SYMBOL(flush_tlb_page);
#endif

EXPORT_SYMBOL(cpu_khz);

EXPORT_SYMBOL(load_gs_index);

