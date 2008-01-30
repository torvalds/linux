/* Exports for assembly files.
   All C exports should go in the respective C files. */

#include <linux/module.h>
#include <linux/smp.h>

#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/desc.h>

EXPORT_SYMBOL(kernel_thread);

EXPORT_SYMBOL(__down_failed);
EXPORT_SYMBOL(__down_failed_interruptible);
EXPORT_SYMBOL(__down_failed_trylock);
EXPORT_SYMBOL(__up_wakeup);

EXPORT_SYMBOL(__get_user_1);
EXPORT_SYMBOL(__get_user_2);
EXPORT_SYMBOL(__get_user_4);
EXPORT_SYMBOL(__get_user_8);
EXPORT_SYMBOL(__put_user_1);
EXPORT_SYMBOL(__put_user_2);
EXPORT_SYMBOL(__put_user_4);
EXPORT_SYMBOL(__put_user_8);

EXPORT_SYMBOL(copy_user_generic);
EXPORT_SYMBOL(__copy_user_nocache);
EXPORT_SYMBOL(copy_from_user);
EXPORT_SYMBOL(copy_to_user);
EXPORT_SYMBOL(__copy_from_user_inatomic);

EXPORT_SYMBOL(copy_page);
EXPORT_SYMBOL(clear_page);

/* Export string functions. We normally rely on gcc builtin for most of these,
   but gcc sometimes decides not to inline them. */    
#undef memcpy
#undef memset
#undef memmove

extern void * memset(void *,int,__kernel_size_t);
extern void * memcpy(void *,const void *,__kernel_size_t);
extern void * __memcpy(void *,const void *,__kernel_size_t);

EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(__memcpy);

EXPORT_SYMBOL(empty_zero_page);
EXPORT_SYMBOL(init_level4_pgt);
EXPORT_SYMBOL(load_gs_index);

EXPORT_SYMBOL(_proxy_pda);

#ifdef CONFIG_PARAVIRT
/* Virtualized guests may want to use it */
EXPORT_SYMBOL_GPL(cpu_gdt_descr);
#endif
