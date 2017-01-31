#include <linux/module.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/tty.h>

#include <asm/processor.h>
#include <linux/uaccess.h>
#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/fasttimer.h>

extern void __Udiv(void);
extern void __Umod(void);
extern void __Div(void);
extern void __Mod(void);
extern void __ashldi3(void);
extern void __ashrdi3(void);
extern void __lshrdi3(void);
extern void __negdi2(void);
extern void iounmap(volatile void * __iomem);

/* Platform dependent support */
EXPORT_SYMBOL(loops_per_usec);

/* Math functions */
EXPORT_SYMBOL(__Udiv);
EXPORT_SYMBOL(__Umod);
EXPORT_SYMBOL(__Div);
EXPORT_SYMBOL(__Mod);
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(__negdi2);

/* Memory functions */
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(iounmap);

#undef memcpy
#undef memset
extern void * memset(void *, int, __kernel_size_t);
extern void * memcpy(void *, const void *, __kernel_size_t);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
#ifdef CONFIG_ETRAX_ARCH_V32
#undef strcmp
EXPORT_SYMBOL(strcmp);
#endif

#ifdef CONFIG_ETRAX_FAST_TIMER
/* Fast timer functions */
EXPORT_SYMBOL(fast_timer_list);
EXPORT_SYMBOL(start_one_shot_timer);
EXPORT_SYMBOL(del_fast_timer);
EXPORT_SYMBOL(schedule_usleep);
#endif
EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(csum_partial_copy_from_user);
EXPORT_SYMBOL(csum_partial_copy_nocheck);
