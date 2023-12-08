// SPDX-License-Identifier: GPL-2.0
/*
 * OS info memory interface
 *
 * Copyright IBM Corp. 2012
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#define KMSG_COMPONENT "os_info"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/crash_dump.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm/checksum.h>
#include <asm/abs_lowcore.h>
#include <asm/os_info.h>
#include <asm/maccess.h>
#include <asm/asm-offsets.h>

/*
 * OS info structure has to be page aligned
 */
static struct os_info os_info __page_aligned_data;

/*
 * Compute checksum over OS info structure
 */
u32 os_info_csum(struct os_info *os_info)
{
	int size = sizeof(*os_info) - offsetof(struct os_info, version_major);
	return (__force u32)csum_partial(&os_info->version_major, size, 0);
}

/*
 * Add crashkernel info to OS info and update checksum
 */
void os_info_crashkernel_add(unsigned long base, unsigned long size)
{
	os_info.crashkernel_addr = (u64)(unsigned long)base;
	os_info.crashkernel_size = (u64)(unsigned long)size;
	os_info.csum = os_info_csum(&os_info);
}

/*
 * Add OS info entry and update checksum
 */
void os_info_entry_add(int nr, void *ptr, u64 size)
{
	os_info.entry[nr].addr = __pa(ptr);
	os_info.entry[nr].size = size;
	os_info.entry[nr].csum = (__force u32)csum_partial(ptr, size, 0);
	os_info.csum = os_info_csum(&os_info);
}

/*
 * Initialize OS info structure and set lowcore pointer
 */
void __init os_info_init(void)
{
	struct lowcore *abs_lc;
	unsigned long flags;

	os_info.version_major = OS_INFO_VERSION_MAJOR;
	os_info.version_minor = OS_INFO_VERSION_MINOR;
	os_info.magic = OS_INFO_MAGIC;
	os_info.csum = os_info_csum(&os_info);
	abs_lc = get_abs_lowcore(&flags);
	abs_lc->os_info = __pa(&os_info);
	put_abs_lowcore(abs_lc, flags);
}

#ifdef CONFIG_CRASH_DUMP

static struct os_info *os_info_old;

/*
 * Allocate and copy OS info entry from oldmem
 */
static void os_info_old_alloc(int nr, int align)
{
	unsigned long addr, size = 0;
	char *buf, *buf_align, *msg;
	u32 csum;

	addr = os_info_old->entry[nr].addr;
	if (!addr) {
		msg = "not available";
		goto fail;
	}
	size = os_info_old->entry[nr].size;
	buf = kmalloc(size + align - 1, GFP_KERNEL);
	if (!buf) {
		msg = "alloc failed";
		goto fail;
	}
	buf_align = PTR_ALIGN(buf, align);
	if (copy_oldmem_kernel(buf_align, addr, size)) {
		msg = "copy failed";
		goto fail_free;
	}
	csum = (__force u32)csum_partial(buf_align, size, 0);
	if (csum != os_info_old->entry[nr].csum) {
		msg = "checksum failed";
		goto fail_free;
	}
	os_info_old->entry[nr].addr = (u64)(unsigned long)buf_align;
	msg = "copied";
	goto out;
fail_free:
	kfree(buf);
fail:
	os_info_old->entry[nr].addr = 0;
out:
	pr_info("entry %i: %s (addr=0x%lx size=%lu)\n",
		nr, msg, addr, size);
}

/*
 * Initialize os info and os info entries from oldmem
 */
static void os_info_old_init(void)
{
	static int os_info_init;
	unsigned long addr;

	if (os_info_init)
		return;
	if (!oldmem_data.start)
		goto fail;
	if (copy_oldmem_kernel(&addr, __LC_OS_INFO, sizeof(addr)))
		goto fail;
	if (addr == 0 || addr % PAGE_SIZE)
		goto fail;
	os_info_old = kzalloc(sizeof(*os_info_old), GFP_KERNEL);
	if (!os_info_old)
		goto fail;
	if (copy_oldmem_kernel(os_info_old, addr, sizeof(*os_info_old)))
		goto fail_free;
	if (os_info_old->magic != OS_INFO_MAGIC)
		goto fail_free;
	if (os_info_old->csum != os_info_csum(os_info_old))
		goto fail_free;
	if (os_info_old->version_major > OS_INFO_VERSION_MAJOR)
		goto fail_free;
	os_info_old_alloc(OS_INFO_VMCOREINFO, 1);
	os_info_old_alloc(OS_INFO_REIPL_BLOCK, 1);
	pr_info("crashkernel: addr=0x%lx size=%lu\n",
		(unsigned long) os_info_old->crashkernel_addr,
		(unsigned long) os_info_old->crashkernel_size);
	os_info_init = 1;
	return;
fail_free:
	kfree(os_info_old);
fail:
	os_info_init = 1;
	os_info_old = NULL;
}

/*
 * Return pointer to os infor entry and its size
 */
void *os_info_old_entry(int nr, unsigned long *size)
{
	os_info_old_init();

	if (!os_info_old)
		return NULL;
	if (!os_info_old->entry[nr].addr)
		return NULL;
	*size = (unsigned long) os_info_old->entry[nr].size;
	return (void *)(unsigned long)os_info_old->entry[nr].addr;
}
#endif
