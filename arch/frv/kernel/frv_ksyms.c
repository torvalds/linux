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
#include <asm/pgalloc.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/semaphore.h>
#include <asm/checksum.h>
#include <asm/hardirq.h>
#include <asm/cacheflush.h>

extern long __memcpy_user(void *dst, const void *src, size_t count);
extern long __memset_user(void *dst, const void *src, size_t count);

/* platform dependent support */

EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(iounmap);

EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(strncpy);

EXPORT_SYMBOL(ip_fast_csum);

#if 0
EXPORT_SYMBOL(local_irq_count);
EXPORT_SYMBOL(local_bh_count);
#endif
EXPORT_SYMBOL(kernel_thread);

EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);
EXPORT_SYMBOL(__res_bus_clock_speed_HZ);
EXPORT_SYMBOL(__page_offset);
EXPORT_SYMBOL(__memcpy_user);
EXPORT_SYMBOL(__memset_user);
EXPORT_SYMBOL(frv_dcache_writeback);
EXPORT_SYMBOL(frv_cache_invalidate);
EXPORT_SYMBOL(frv_icache_invalidate);
EXPORT_SYMBOL(frv_cache_wback_inv);

#ifndef CONFIG_MMU
EXPORT_SYMBOL(memory_start);
EXPORT_SYMBOL(memory_end);
#endif

EXPORT_SYMBOL(__debug_bug_trap);

/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial_copy);

/* The following are special because they're not called
   explicitly (the C compiler generates them).  Fortunately,
   their interface isn't gonna change any time soon now, so
   it's OK to leave it out of version control.  */
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memscan);
EXPORT_SYMBOL(memmove);

EXPORT_SYMBOL(__outsl_ns);
EXPORT_SYMBOL(__insl_ns);

#ifdef CONFIG_FRV_OUTOFLINE_ATOMIC_OPS
EXPORT_SYMBOL(atomic_test_and_ANDNOT_mask);
EXPORT_SYMBOL(atomic_test_and_OR_mask);
EXPORT_SYMBOL(atomic_test_and_XOR_mask);
EXPORT_SYMBOL(atomic_add_return);
EXPORT_SYMBOL(atomic_sub_return);
EXPORT_SYMBOL(__xchg_32);
EXPORT_SYMBOL(__cmpxchg_32);
#endif

EXPORT_SYMBOL(__debug_bug_printk);
EXPORT_SYMBOL(__delay_loops_MHz);

/*
 * libgcc functions - functions that are used internally by the
 * compiler...  (prototypes are not correct though, but that
 * doesn't really matter since they're not versioned).
 */
extern void __gcc_bcmp(void);
extern void __ashldi3(void);
extern void __ashrdi3(void);
extern void __cmpdi2(void);
extern void __divdi3(void);
extern void __lshrdi3(void);
extern void __moddi3(void);
extern void __muldi3(void);
extern void __mulll(void);
extern void __umulll(void);
extern void __negdi2(void);
extern void __ucmpdi2(void);
extern void __udivdi3(void);
extern void __udivmoddi4(void);
extern void __umoddi3(void);

        /* gcc lib functions */
//EXPORT_SYMBOL(__gcc_bcmp);
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__ashrdi3);
//EXPORT_SYMBOL(__cmpdi2);
//EXPORT_SYMBOL(__divdi3);
EXPORT_SYMBOL(__lshrdi3);
//EXPORT_SYMBOL(__moddi3);
EXPORT_SYMBOL(__muldi3);
EXPORT_SYMBOL(__mulll);
EXPORT_SYMBOL(__umulll);
EXPORT_SYMBOL(__negdi2);
EXPORT_SYMBOL(__ucmpdi2);
//EXPORT_SYMBOL(__udivdi3);
//EXPORT_SYMBOL(__udivmoddi4);
//EXPORT_SYMBOL(__umoddi3);
