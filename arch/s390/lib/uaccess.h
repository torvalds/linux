/*
 *    Copyright IBM Corp. 2007
 *
 */

#ifndef __ARCH_S390_LIB_UACCESS_H
#define __ARCH_S390_LIB_UACCESS_H

size_t copy_from_user_pt(void *to, const void __user *from, size_t n);
size_t copy_to_user_pt(void __user *to, const void *from, size_t n);
size_t copy_in_user_pt(void __user *to, const void __user *from, size_t n);
size_t clear_user_pt(void __user *to, size_t n);
size_t strnlen_user_pt(const char __user *src, size_t count);
size_t strncpy_from_user_pt(char *dst, const char __user *src, size_t count);

#endif /* __ARCH_S390_LIB_UACCESS_H */
