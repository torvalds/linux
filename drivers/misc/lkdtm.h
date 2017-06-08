#ifndef __LKDTM_H
#define __LKDTM_H

#define pr_fmt(fmt) "lkdtm: " fmt

#include <linux/kernel.h>

/* lkdtm_bugs.c */
void __init lkdtm_bugs_init(int *recur_param);
void lkdtm_PANIC(void);
void lkdtm_BUG(void);
void lkdtm_WARNING(void);
void lkdtm_EXCEPTION(void);
void lkdtm_LOOP(void);
void lkdtm_OVERFLOW(void);
void lkdtm_CORRUPT_STACK(void);
void lkdtm_UNALIGNED_LOAD_STORE_WRITE(void);
void lkdtm_SOFTLOCKUP(void);
void lkdtm_HARDLOCKUP(void);
void lkdtm_SPINLOCKUP(void);
void lkdtm_HUNG_TASK(void);
void lkdtm_REFCOUNT_SATURATE_INC(void);
void lkdtm_REFCOUNT_SATURATE_ADD(void);
void lkdtm_REFCOUNT_ZERO_DEC(void);
void lkdtm_REFCOUNT_ZERO_INC(void);
void lkdtm_REFCOUNT_ZERO_SUB(void);
void lkdtm_REFCOUNT_ZERO_ADD(void);
void lkdtm_CORRUPT_LIST_ADD(void);
void lkdtm_CORRUPT_LIST_DEL(void);
void lkdtm_CORRUPT_USER_DS(void);

/* lkdtm_heap.c */
void lkdtm_OVERWRITE_ALLOCATION(void);
void lkdtm_WRITE_AFTER_FREE(void);
void lkdtm_READ_AFTER_FREE(void);
void lkdtm_WRITE_BUDDY_AFTER_FREE(void);
void lkdtm_READ_BUDDY_AFTER_FREE(void);

/* lkdtm_perms.c */
void __init lkdtm_perms_init(void);
void lkdtm_WRITE_RO(void);
void lkdtm_WRITE_RO_AFTER_INIT(void);
void lkdtm_WRITE_KERN(void);
void lkdtm_EXEC_DATA(void);
void lkdtm_EXEC_STACK(void);
void lkdtm_EXEC_KMALLOC(void);
void lkdtm_EXEC_VMALLOC(void);
void lkdtm_EXEC_RODATA(void);
void lkdtm_EXEC_USERSPACE(void);
void lkdtm_ACCESS_USERSPACE(void);

/* lkdtm_rodata.c */
void lkdtm_rodata_do_nothing(void);

/* lkdtm_usercopy.c */
void __init lkdtm_usercopy_init(void);
void __exit lkdtm_usercopy_exit(void);
void lkdtm_USERCOPY_HEAP_SIZE_TO(void);
void lkdtm_USERCOPY_HEAP_SIZE_FROM(void);
void lkdtm_USERCOPY_HEAP_FLAG_TO(void);
void lkdtm_USERCOPY_HEAP_FLAG_FROM(void);
void lkdtm_USERCOPY_STACK_FRAME_TO(void);
void lkdtm_USERCOPY_STACK_FRAME_FROM(void);
void lkdtm_USERCOPY_STACK_BEYOND(void);
void lkdtm_USERCOPY_KERNEL(void);

#endif
