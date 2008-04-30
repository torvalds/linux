#include <linux/module.h>
#include <linux/linkage.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/in6.h>
#include <linux/interrupt.h>

#include <asm/pgalloc.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/checksum.h>
#include <asm/current.h>


extern void *trap_table;
EXPORT_SYMBOL (trap_table);

/* platform dependent support */
EXPORT_SYMBOL (kernel_thread);
EXPORT_SYMBOL (__bug);

/* Networking helper routines. */
EXPORT_SYMBOL (csum_partial_copy_nocheck);
EXPORT_SYMBOL (csum_partial_copy_from_user);
EXPORT_SYMBOL (ip_compute_csum);
EXPORT_SYMBOL (ip_fast_csum);

/* string / mem functions */
EXPORT_SYMBOL (memset);
EXPORT_SYMBOL (memcpy);
EXPORT_SYMBOL (memmove);

/*
 * libgcc functions - functions that are used internally by the
 * compiler...  (prototypes are not correct though, but that
 * doesn't really matter since they're not versioned).
 */
extern void __ashldi3 (void);
extern void __ashrdi3 (void);
extern void __lshrdi3 (void);
extern void __muldi3 (void);
extern void __negdi2 (void);

EXPORT_SYMBOL (__ashldi3);
EXPORT_SYMBOL (__ashrdi3);
EXPORT_SYMBOL (__lshrdi3);
EXPORT_SYMBOL (__muldi3);
EXPORT_SYMBOL (__negdi2);
