/*
 *    Copyright IBM Corp. 2007
 *
 */

#ifndef __ARCH_S390_LIB_UACCESS_H
#define __ARCH_S390_LIB_UACCESS_H

extern int futex_atomic_op_pt(int, u32 __user *, int, int *);
extern int futex_atomic_cmpxchg_pt(u32 *, u32 __user *, u32, u32);

#endif /* __ARCH_S390_LIB_UACCESS_H */
