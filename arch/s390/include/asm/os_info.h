/* SPDX-License-Identifier: GPL-2.0 */
/*
 * OS info memory interface
 *
 * Copyright IBM Corp. 2012
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */
#ifndef _ASM_S390_OS_INFO_H
#define _ASM_S390_OS_INFO_H

#include <linux/uio.h>

#define OS_INFO_VERSION_MAJOR	1
#define OS_INFO_VERSION_MINOR	1
#define OS_INFO_MAGIC		0x4f53494e464f535aULL /* OSINFOSZ */

#define OS_INFO_VMCOREINFO	0
#define OS_INFO_REIPL_BLOCK	1
#define OS_INFO_FLAGS_ENTRY	2
#define OS_INFO_RESERVED	3
#define OS_INFO_IDENTITY_BASE	4
#define OS_INFO_KASLR_OFFSET	5
#define OS_INFO_KASLR_OFF_PHYS	6
#define OS_INFO_VMEMMAP		7
#define OS_INFO_AMODE31_START	8
#define OS_INFO_AMODE31_END	9
#define OS_INFO_IMAGE_START	10
#define OS_INFO_IMAGE_END	11
#define OS_INFO_IMAGE_PHYS	12
#define OS_INFO_MAX		13

#define OS_INFO_FLAG_REIPL_CLEAR	(1UL << 0)

struct os_info_entry {
	union {
		u64	addr;
		u64	val;
	};
	u64	size;
	u32	csum;
} __packed;

struct os_info {
	u64	magic;
	u32	csum;
	u16	version_major;
	u16	version_minor;
	u64	crashkernel_addr;
	u64	crashkernel_size;
	struct os_info_entry entry[OS_INFO_MAX];
	u8	reserved[3804];
} __packed;

void os_info_init(void);
void os_info_entry_add_data(int nr, void *ptr, u64 len);
void os_info_entry_add_val(int nr, u64 val);
void os_info_crashkernel_add(unsigned long base, unsigned long size);
u32 os_info_csum(struct os_info *os_info);

#ifdef CONFIG_CRASH_DUMP
void *os_info_old_entry(int nr, unsigned long *size);
static inline unsigned long os_info_old_value(int nr)
{
	unsigned long size;

	return (unsigned long)os_info_old_entry(nr, &size);
}
#else
static inline void *os_info_old_entry(int nr, unsigned long *size)
{
	return NULL;
}
#endif

#endif /* _ASM_S390_OS_INFO_H */
