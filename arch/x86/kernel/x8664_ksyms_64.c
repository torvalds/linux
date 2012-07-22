/* Exports for assembly files.
   All C exports should go in the respective C files. */

#include <linux/module.h>
#include <linux/smp.h>

#include <net/checksum.h>

#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/desc.h>
#include <asm/ftrace.h>

#ifdef CONFIG_FUNCTION_TRACER
/* mcount is defined in assembly */
EXPORT_SYMBOL(mcount);
#endif

EXPORT_SYMBOL(__get_user_1);
EXPORT_SYMBOL(__get_user_2);
EXPORT_SYMBOL(__get_user_4);
EXPORT_SYMBOL(__get_user_8);
EXPORT_SYMBOL(__put_user_1);
EXPORT_SYMBOL(__put_user_2);
EXPORT_SYMBOL(__put_user_4);
EXPORT_SYMBOL(__put_user_8);

EXPORT_SYMBOL(copy_user_generic_string);
EXPORT_SYMBOL(copy_user_generic_unrolled);
EXPORT_SYMBOL(copy_user_enhanced_fast_string);
EXPORT_SYMBOL(__copy_user_nocache);
EXPORT_SYMBOL(_copy_from_user);
EXPORT_SYMBOL(_copy_to_user);

EXPORT_SYMBOL(copy_page);
EXPORT_SYMBOL(clear_page);

EXPORT_SYMBOL(csum_partial);

/*
 * Export string functions. We normally rely on gcc builtin for most of these,
 * but gcc sometimes decides not to inline them.
 */
#undef memcpy
#undef memset
#undef memmove

extern void *memset(void *, int, __kernel_size_t);
extern void *memcpy(void *, const void *, __kernel_size_t);
extern void *__memcpy(void *, const void *, __kernel_size_t);

EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(__memcpy);
EXPORT_SYMBOL(memmove);

EXPORT_SYMBOL(empty_zero_page);
#ifndef CONFIG_PARAVIRT
EXPORT_SYMBOL(native_load_gs_index);
#endif
