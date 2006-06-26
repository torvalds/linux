#include <linux/module.h>
#include <linux/linkage.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/in6.h>
#include <linux/interrupt.h>
#include <linux/config.h>

#include <asm/setup.h>
#include <asm/machdep.h>
#include <asm/pgalloc.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/semaphore.h>
#include <asm/checksum.h>

asmlinkage long long __ashldi3 (long long, int);
asmlinkage long long __ashrdi3 (long long, int);
asmlinkage long long __lshrdi3 (long long, int);
asmlinkage long long __muldi3 (long long, long long);
extern char m68k_debug_device[];

/* platform dependent support */

EXPORT_SYMBOL(m68k_machtype);
EXPORT_SYMBOL(m68k_cputype);
EXPORT_SYMBOL(m68k_is040or060);
EXPORT_SYMBOL(m68k_realnum_memory);
EXPORT_SYMBOL(m68k_memory);
#ifndef CONFIG_SUN3
EXPORT_SYMBOL(cache_push);
EXPORT_SYMBOL(cache_clear);
#ifndef CONFIG_SINGLE_MEMORY_CHUNK
EXPORT_SYMBOL(mm_vtop);
EXPORT_SYMBOL(mm_ptov);
EXPORT_SYMBOL(mm_end_of_chunk);
#else
EXPORT_SYMBOL(m68k_memoffset);
#endif /* !CONFIG_SINGLE_MEMORY_CHUNK */
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(kernel_set_cachemode);
#endif /* !CONFIG_SUN3 */
EXPORT_SYMBOL(m68k_debug_device);
EXPORT_SYMBOL(mach_hwclk);
EXPORT_SYMBOL(mach_get_ss);
EXPORT_SYMBOL(mach_get_rtc_pll);
EXPORT_SYMBOL(mach_set_rtc_pll);
#ifdef CONFIG_INPUT_M68K_BEEP_MODULE
EXPORT_SYMBOL(mach_beep);
#endif
EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(dump_thread);
EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(kernel_thread);
#ifdef CONFIG_VME
EXPORT_SYMBOL(vme_brdtype);
#endif

/* The following are special because they're not called
   explicitly (the C compiler generates them).  Fortunately,
   their interface isn't gonna change any time soon now, so
   it's OK to leave it out of version control.  */
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(__muldi3);

EXPORT_SYMBOL(__down_failed);
EXPORT_SYMBOL(__down_failed_interruptible);
EXPORT_SYMBOL(__down_failed_trylock);
EXPORT_SYMBOL(__up_wakeup);

