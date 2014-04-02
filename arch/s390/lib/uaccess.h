/*
 *    Copyright IBM Corp. 2007
 *
 */

#ifndef __ARCH_S390_LIB_UACCESS_H
#define __ARCH_S390_LIB_UACCESS_H

unsigned long copy_from_user_pt(void *to, const void __user *from, unsigned long n);
unsigned long copy_to_user_pt(void __user *to, const void *from, unsigned long n);
unsigned long copy_in_user_pt(void __user *to, const void __user *from, unsigned long n);
unsigned long clear_user_pt(void __user *to, unsigned long n);
unsigned long strnlen_user_pt(const char __user *src, unsigned long count);
long strncpy_from_user_pt(char *dst, const char __user *src, long count);

#endif /* __ARCH_S390_LIB_UACCESS_H */
