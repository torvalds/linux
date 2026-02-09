/* SPDX-License-Identifier: GPL-2.0 */
#define COMPILE_OFFSETS
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/elf.h>
#include <linux/crypto.h>
#include <linux/kbuild.h>
#include <linux/audit.h>
#include <linux/fs.h>
#include <asm/mman.h>
#include <asm/seccomp.h>
#include <asm/extable.h>

/* workaround for a warning with -Wmissing-prototypes */
void foo(void);

void foo(void)
{
	DEFINE(KERNEL_MADV_REMOVE, MADV_REMOVE);

	DEFINE(UM_KERN_PAGE_SIZE, PAGE_SIZE);
	DEFINE(UM_KERN_PAGE_MASK, PAGE_MASK);
	DEFINE(UM_KERN_PAGE_SHIFT, PAGE_SHIFT);

	DEFINE(UM_GFP_KERNEL, GFP_KERNEL);
	DEFINE(UM_GFP_ATOMIC, GFP_ATOMIC);

	DEFINE(UM_THREAD_SIZE, THREAD_SIZE);

	DEFINE(UM_NSEC_PER_SEC, NSEC_PER_SEC);
	DEFINE(UM_NSEC_PER_USEC, NSEC_PER_USEC);

	DEFINE(UM_KERN_GDT_ENTRY_TLS_ENTRIES, GDT_ENTRY_TLS_ENTRIES);

	DEFINE(UM_SECCOMP_ARCH_NATIVE, SECCOMP_ARCH_NATIVE);

	DEFINE(HOSTFS_ATTR_MODE, ATTR_MODE);
	DEFINE(HOSTFS_ATTR_UID, ATTR_UID);
	DEFINE(HOSTFS_ATTR_GID, ATTR_GID);
	DEFINE(HOSTFS_ATTR_SIZE, ATTR_SIZE);
	DEFINE(HOSTFS_ATTR_ATIME, ATTR_ATIME);
	DEFINE(HOSTFS_ATTR_MTIME, ATTR_MTIME);
	DEFINE(HOSTFS_ATTR_CTIME, ATTR_CTIME);
	DEFINE(HOSTFS_ATTR_ATIME_SET, ATTR_ATIME_SET);
	DEFINE(HOSTFS_ATTR_MTIME_SET, ATTR_MTIME_SET);

	DEFINE(ALT_INSTR_SIZE, sizeof(struct alt_instr));
	DEFINE(EXTABLE_SIZE,   sizeof(struct exception_table_entry));
}
