/*
 * Copyright (C) 2013 Altera Corporation
 * Copyright (C) 2011-2012 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/export.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/syscalls.h>

#include <asm/cacheflush.h>
#include <asm/traps.h>

/* sys_cacheflush -- flush the processor cache. */
asmlinkage int sys_cacheflush(unsigned long addr, unsigned long len,
				unsigned int op)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;

	if (len == 0)
		return 0;

	/* We only support op 0 now, return error if op is non-zero.*/
	if (op)
		return -EINVAL;

	/* Check for overflow */
	if (addr + len < addr)
		return -EFAULT;

	if (mmap_read_lock_killable(mm))
		return -EINTR;

	/*
	 * Verify that the specified address region actually belongs
	 * to this process.
	 */
	vma = find_vma(mm, addr);
	if (vma == NULL || addr < vma->vm_start || addr + len > vma->vm_end) {
		mmap_read_unlock(mm);
		return -EFAULT;
	}

	flush_cache_range(vma, addr, addr + len);

	mmap_read_unlock(mm);
	return 0;
}

asmlinkage int sys_getpagesize(void)
{
	return PAGE_SIZE;
}
