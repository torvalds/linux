/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/mman.h"
 */
#ifndef __S390_MMAN_H__
#define __S390_MMAN_H__

#include <uapi/asm/mman.h>

#if !defined(__ASSEMBLY__) && defined(CONFIG_64BIT)
int s390_mmap_check(unsigned long addr, unsigned long len);
#define arch_mmap_check(addr,len,flags)	s390_mmap_check(addr,len)
#endif
#endif /* __S390_MMAN_H__ */
