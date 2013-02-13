#include <linux/export.h>
#include <linux/linkage.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/user.h>
#include <linux/interrupt.h>
#include <linux/hardirq.h>

#include <asm/setup.h>
#include <asm/checksum.h>
#include <asm/uaccess.h>
#include <asm/traps.h>
#include <asm/ftrace.h>
#include <asm/tbx.h>

/* uaccess symbols */
EXPORT_SYMBOL(__copy_user_zeroing);
EXPORT_SYMBOL(__copy_user);
EXPORT_SYMBOL(__get_user_asm_b);
EXPORT_SYMBOL(__get_user_asm_w);
EXPORT_SYMBOL(__get_user_asm_d);
EXPORT_SYMBOL(__put_user_asm_b);
EXPORT_SYMBOL(__put_user_asm_w);
EXPORT_SYMBOL(__put_user_asm_d);
EXPORT_SYMBOL(__put_user_asm_l);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(strnlen_user);
EXPORT_SYMBOL(__do_clear_user);

EXPORT_SYMBOL(pTBI_get);
EXPORT_SYMBOL(meta_memoffset);
EXPORT_SYMBOL(kick_register_func);
EXPORT_SYMBOL(kick_unregister_func);

EXPORT_SYMBOL(clear_page);
EXPORT_SYMBOL(copy_page);
EXPORT_SYMBOL(empty_zero_page);

EXPORT_SYMBOL(pfn_base);
#ifdef CONFIG_FLATMEM
/* needed for the pfn_valid macro */
EXPORT_SYMBOL(max_pfn);
EXPORT_SYMBOL(min_low_pfn);
#endif

/* TBI symbols */
EXPORT_SYMBOL(__TBI);
EXPORT_SYMBOL(__TBIFindSeg);
EXPORT_SYMBOL(__TBIPoll);
EXPORT_SYMBOL(__TBITimeStamp);

#define DECLARE_EXPORT(name) extern void name(void); EXPORT_SYMBOL(name)

/* libgcc functions */
DECLARE_EXPORT(__ashldi3);
DECLARE_EXPORT(__ashrdi3);
DECLARE_EXPORT(__lshrdi3);
DECLARE_EXPORT(__udivsi3);
DECLARE_EXPORT(__divsi3);
DECLARE_EXPORT(__umodsi3);
DECLARE_EXPORT(__modsi3);
DECLARE_EXPORT(__muldi3);
DECLARE_EXPORT(__cmpdi2);
DECLARE_EXPORT(__ucmpdi2);

/* Maths functions */
EXPORT_SYMBOL(div_u64);
EXPORT_SYMBOL(div_s64);

/* String functions */
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memmove);

#ifdef CONFIG_FUNCTION_TRACER
EXPORT_SYMBOL(mcount_wrapper);
#endif
