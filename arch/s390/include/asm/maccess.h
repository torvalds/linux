/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_S390_MACCESS_H
#define __ASM_S390_MACCESS_H

#include <linux/types.h>

struct iov_iter;

extern unsigned long __memcpy_real_area;
void memcpy_real_init(void);
size_t memcpy_real_iter(struct iov_iter *iter, unsigned long src, size_t count);
int memcpy_real(void *dest, unsigned long src, size_t count);
#ifdef CONFIG_CRASH_DUMP
int copy_oldmem_kernel(void *dst, unsigned long src, size_t count);
#endif

#endif /* __ASM_S390_MACCESS_H */
