#include <linux/config.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/string.h>

#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/tlbflush.h>

/* platform dependent support */
EXPORT_SYMBOL(boot_cpu_data);
EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);
EXPORT_SYMBOL(disable_irq_nosync);
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(__down);
EXPORT_SYMBOL(__down_interruptible);
EXPORT_SYMBOL(__up);
EXPORT_SYMBOL(__down_trylock);

/* Networking helper routines. */
/* Delay loops */
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__delay);
EXPORT_SYMBOL(__const_udelay);

EXPORT_SYMBOL(strpbrk);
EXPORT_SYMBOL(strstr);

EXPORT_SYMBOL(strncpy_from_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(clear_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(__generic_copy_from_user);
EXPORT_SYMBOL(__generic_copy_to_user);
EXPORT_SYMBOL(strnlen_user);

#ifdef CONFIG_SMP
#ifdef CONFIG_CHIP_M32700_TS1
extern void *dcache_dummy;
EXPORT_SYMBOL(dcache_dummy);
#endif
EXPORT_SYMBOL(cpu_data);
EXPORT_SYMBOL(cpu_online_map);
EXPORT_SYMBOL(cpu_callout_map);

/* Global SMP stuff */
EXPORT_SYMBOL(synchronize_irq);
EXPORT_SYMBOL(smp_call_function);

/* TLB flushing */
EXPORT_SYMBOL(smp_flush_tlb_page);
#endif

/* compiler generated symbol */
extern void __ashldi3(void);
extern void __ashrdi3(void);
extern void __lshldi3(void);
extern void __lshrdi3(void);
extern void __muldi3(void);
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__lshldi3);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(__muldi3);

/* memory and string operations */
EXPORT_SYMBOL(memchr);
EXPORT_SYMBOL(memcpy);
/* EXPORT_SYMBOL(memcpy_fromio); // not implement yet */
/* EXPORT_SYMBOL(memcpy_toio); // not implement yet */
EXPORT_SYMBOL(memset);
/* EXPORT_SYMBOL(memset_io); // not implement yet */
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memscan);
EXPORT_SYMBOL(copy_page);
EXPORT_SYMBOL(clear_page);

EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strncat);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(strncpy);

EXPORT_SYMBOL(_inb);
EXPORT_SYMBOL(_inw);
EXPORT_SYMBOL(_inl);
EXPORT_SYMBOL(_outb);
EXPORT_SYMBOL(_outw);
EXPORT_SYMBOL(_outl);
EXPORT_SYMBOL(_inb_p);
EXPORT_SYMBOL(_inw_p);
EXPORT_SYMBOL(_inl_p);
EXPORT_SYMBOL(_outb_p);
EXPORT_SYMBOL(_outw_p);
EXPORT_SYMBOL(_outl_p);
EXPORT_SYMBOL(_insb);
EXPORT_SYMBOL(_insw);
EXPORT_SYMBOL(_insl);
EXPORT_SYMBOL(_outsb);
EXPORT_SYMBOL(_outsw);
EXPORT_SYMBOL(_outsl);
EXPORT_SYMBOL(_readb);
EXPORT_SYMBOL(_readw);
EXPORT_SYMBOL(_readl);
EXPORT_SYMBOL(_writeb);
EXPORT_SYMBOL(_writew);
EXPORT_SYMBOL(_writel);

