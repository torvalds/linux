#include <linux/module.h>

#include <asm/checksum.h>
#include <asm/pgtable.h>
#include <asm/desc.h>
#include <asm/ftrace.h>

#ifdef CONFIG_FUNCTION_TRACER
/* mcount is defined in assembly */
EXPORT_SYMBOL(mcount);
#endif

/*
 * Note, this is a prototype to get at the symbol for
 * the export, but dont use it from C code, it is used
 * by assembly code and is not using C calling convention!
 */
extern void cmpxchg8b_emu(void);
EXPORT_SYMBOL(cmpxchg8b_emu);

/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial_copy_generic);

EXPORT_SYMBOL(__get_user_1);
EXPORT_SYMBOL(__get_user_2);
EXPORT_SYMBOL(__get_user_4);

EXPORT_SYMBOL(__put_user_1);
EXPORT_SYMBOL(__put_user_2);
EXPORT_SYMBOL(__put_user_4);
EXPORT_SYMBOL(__put_user_8);

EXPORT_SYMBOL(strstr);

EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(empty_zero_page);
