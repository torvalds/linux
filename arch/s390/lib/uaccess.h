/*
 *  arch/s390/uaccess.h
 *
 *    Copyright IBM Corp. 2007
 *
 */

#ifndef __ARCH_S390_LIB_UACCESS_H
#define __ARCH_S390_LIB_UACCESS_H

extern size_t copy_from_user_std(size_t, const void __user *, void *);
extern size_t copy_to_user_std(size_t, void __user *, const void *);
extern size_t strnlen_user_std(size_t, const char __user *);
extern size_t strncpy_from_user_std(size_t, const char __user *, char *);
extern int futex_atomic_cmpxchg_std(int __user *, int, int);
extern int futex_atomic_op_std(int, int __user *, int, int *);

extern size_t copy_from_user_pt(size_t, const void __user *, void *);
extern size_t copy_to_user_pt(size_t, void __user *, const void *);
extern int futex_atomic_op_pt(int, int __user *, int, int *);
extern int futex_atomic_cmpxchg_pt(int __user *, int, int);

#endif /* __ARCH_S390_LIB_UACCESS_H */
