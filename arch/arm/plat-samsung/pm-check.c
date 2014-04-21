/* linux/arch/arm/plat-s3c/pm-check.c
 *  originally in linux/arch/arm/plat-s3c24xx/pm.c
 *
 * Copyright (c) 2004-2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C Power Mangament - suspend/resume memory corruptiuon check.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/suspend.h>
#include <linux/init.h>
#include <linux/crc32.h>
#include <linux/ioport.h>
#include <linux/slab.h>

#include <plat/pm-common.h>

#if CONFIG_SAMSUNG_PM_CHECK_CHUNKSIZE < 1
#error CONFIG_SAMSUNG_PM_CHECK_CHUNKSIZE must be a positive non-zero value
#endif

/* suspend checking code...
 *
 * this next area does a set of crc checks over all the installed
 * memory, so the system can verify if the resume was ok.
 *
 * CONFIG_SAMSUNG_PM_CHECK_CHUNKSIZE defines the block-size for the CRC,
 * increasing it will mean that the area corrupted will be less easy to spot,
 * and reducing the size will cause the CRC save area to grow
*/

#define CHECK_CHUNKSIZE (CONFIG_SAMSUNG_PM_CHECK_CHUNKSIZE * 1024)

static u32 crc_size;	/* size needed for the crc block */
static u32 *crcs;	/* allocated over suspend/resume */

typedef u32 *(run_fn_t)(struct resource *ptr, u32 *arg);

/* s3c_pm_run_res
 *
 * go through the given resource list, and look for system ram
*/

static void s3c_pm_run_res(struct resource *ptr, run_fn_t fn, u32 *arg)
{
	while (ptr != NULL) {
		if (ptr->child != NULL)
			s3c_pm_run_res(ptr->child, fn, arg);

		if ((ptr->flags & IORESOURCE_MEM) &&
		    strcmp(ptr->name, "System RAM") == 0) {
			S3C_PMDBG("Found system RAM at %08lx..%08lx\n",
				  (unsigned long)ptr->start,
				  (unsigned long)ptr->end);
			arg = (fn)(ptr, arg);
		}

		ptr = ptr->sibling;
	}
}

static void s3c_pm_run_sysram(run_fn_t fn, u32 *arg)
{
	s3c_pm_run_res(&iomem_resource, fn, arg);
}

static u32 *s3c_pm_countram(struct resource *res, u32 *val)
{
	u32 size = (u32)resource_size(res);

	size += CHECK_CHUNKSIZE-1;
	size /= CHECK_CHUNKSIZE;

	S3C_PMDBG("Area %08lx..%08lx, %d blocks\n",
		  (unsigned long)res->start, (unsigned long)res->end, size);

	*val += size * sizeof(u32);
	return val;
}

/* s3c_pm_prepare_check
 *
 * prepare the necessary information for creating the CRCs. This
 * must be done before the final save, as it will require memory
 * allocating, and thus touching bits of the kernel we do not
 * know about.
*/

void s3c_pm_check_prepare(void)
{
	crc_size = 0;

	s3c_pm_run_sysram(s3c_pm_countram, &crc_size);

	S3C_PMDBG("s3c_pm_prepare_check: %u checks needed\n", crc_size);

	crcs = kmalloc(crc_size+4, GFP_KERNEL);
	if (crcs == NULL)
		printk(KERN_ERR "Cannot allocated CRC save area\n");
}

static u32 *s3c_pm_makecheck(struct resource *res, u32 *val)
{
	unsigned long addr, left;

	for (addr = res->start; addr < res->end;
	     addr += CHECK_CHUNKSIZE) {
		left = res->end - addr;

		if (left > CHECK_CHUNKSIZE)
			left = CHECK_CHUNKSIZE;

		*val = crc32_le(~0, phys_to_virt(addr), left);
		val++;
	}

	return val;
}

/* s3c_pm_check_store
 *
 * compute the CRC values for the memory blocks before the final
 * sleep.
*/

void s3c_pm_check_store(void)
{
	if (crcs != NULL)
		s3c_pm_run_sysram(s3c_pm_makecheck, crcs);
}

/* in_region
 *
 * return TRUE if the area defined by ptr..ptr+size contains the
 * what..what+whatsz
*/

static inline int in_region(void *ptr, int size, void *what, size_t whatsz)
{
	if ((what+whatsz) < ptr)
		return 0;

	if (what > (ptr+size))
		return 0;

	return 1;
}

/**
 * s3c_pm_runcheck() - helper to check a resource on restore.
 * @res: The resource to check
 * @vak: Pointer to list of CRC32 values to check.
 *
 * Called from the s3c_pm_check_restore() via s3c_pm_run_sysram(), this
 * function runs the given memory resource checking it against the stored
 * CRC to ensure that memory is restored. The function tries to skip as
 * many of the areas used during the suspend process.
 */
static u32 *s3c_pm_runcheck(struct resource *res, u32 *val)
{
	unsigned long addr;
	unsigned long left;
	void *stkpage;
	void *ptr;
	u32 calc;

	stkpage = (void *)((u32)&calc & ~PAGE_MASK);

	for (addr = res->start; addr < res->end;
	     addr += CHECK_CHUNKSIZE) {
		left = res->end - addr;

		if (left > CHECK_CHUNKSIZE)
			left = CHECK_CHUNKSIZE;

		ptr = phys_to_virt(addr);

		if (in_region(ptr, left, stkpage, 4096)) {
			S3C_PMDBG("skipping %08lx, has stack in\n", addr);
			goto skip_check;
		}

		if (in_region(ptr, left, crcs, crc_size)) {
			S3C_PMDBG("skipping %08lx, has crc block in\n", addr);
			goto skip_check;
		}

		/* calculate and check the checksum */

		calc = crc32_le(~0, ptr, left);
		if (calc != *val) {
			printk(KERN_ERR "Restore CRC error at "
			       "%08lx (%08x vs %08x)\n", addr, calc, *val);

			S3C_PMDBG("Restore CRC error at %08lx (%08x vs %08x)\n",
			    addr, calc, *val);
		}

	skip_check:
		val++;
	}

	return val;
}

/**
 * s3c_pm_check_restore() - memory check called on resume
 *
 * check the CRCs after the restore event and free the memory used
 * to hold them
*/
void s3c_pm_check_restore(void)
{
	if (crcs != NULL)
		s3c_pm_run_sysram(s3c_pm_runcheck, crcs);
}

/**
 * s3c_pm_check_cleanup() - free memory resources
 *
 * Free the resources that where allocated by the suspend
 * memory check code. We do this separately from the
 * s3c_pm_check_restore() function as we cannot call any
 * functions that might sleep during that resume.
 */
void s3c_pm_check_cleanup(void)
{
	kfree(crcs);
	crcs = NULL;
}

