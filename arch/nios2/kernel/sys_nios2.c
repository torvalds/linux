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

	if (len == 0)
		return 0;

	/* We only support op 0 now, return error if op is non-zero.*/
	if (op)
		return -EINVAL;

	/* Check for overflow */
	if (addr + len < addr)
		return -EFAULT;

	/*
	 * Verify that the specified address region actually belongs
	 * to this process.
	 */
	vma = find_vma(current->mm, addr);
	if (vma == NULL || addr < vma->vm_start || addr + len > vma->vm_end)
		return -EFAULT;

	flush_cache_range(vma, addr, addr + len);

	return 0;
}

asmlinkage int sys_getpagesize(void)
{
	return PAGE_SIZE;
}
