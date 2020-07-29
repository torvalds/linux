// SPDX-License-Identifier: GPL-2.0-only
/*
 * ppc64 code to implement the kexec_file_load syscall
 *
 * Copyright (C) 2004  Adam Litke (agl@us.ibm.com)
 * Copyright (C) 2004  IBM Corp.
 * Copyright (C) 2004,2005  Milton D Miller II, IBM Corporation
 * Copyright (C) 2005  R Sharada (sharada@in.ibm.com)
 * Copyright (C) 2006  Mohan Kumar M (mohan@in.ibm.com)
 * Copyright (C) 2020  IBM Corporation
 *
 * Based on kexec-tools' kexec-ppc64.c, kexec-elf-rel-ppc64.c, fs2dt.c.
 * Heavily modified for the kernel by
 * Hari Bathini, IBM Corporation.
 */

#include <linux/kexec.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <linux/memblock.h>
#include <asm/kexec_ranges.h>

const struct kexec_file_ops * const kexec_file_loaders[] = {
	&kexec_elf64_ops,
	NULL
};

/**
 * get_exclude_memory_ranges - Get exclude memory ranges. This list includes
 *                             regions like opal/rtas, tce-table, initrd,
 *                             kernel, htab which should be avoided while
 *                             setting up kexec load segments.
 * @mem_ranges:                Range list to add the memory ranges to.
 *
 * Returns 0 on success, negative errno on error.
 */
static int get_exclude_memory_ranges(struct crash_mem **mem_ranges)
{
	int ret;

	ret = add_tce_mem_ranges(mem_ranges);
	if (ret)
		goto out;

	ret = add_initrd_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_htab_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_kernel_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_rtas_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_opal_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_reserved_mem_ranges(mem_ranges);
	if (ret)
		goto out;

	/* exclude memory ranges should be sorted for easy lookup */
	sort_memory_ranges(*mem_ranges, true);
out:
	if (ret)
		pr_err("Failed to setup exclude memory ranges\n");
	return ret;
}

/**
 * __locate_mem_hole_top_down - Looks top down for a large enough memory hole
 *                              in the memory regions between buf_min & buf_max
 *                              for the buffer. If found, sets kbuf->mem.
 * @kbuf:                       Buffer contents and memory parameters.
 * @buf_min:                    Minimum address for the buffer.
 * @buf_max:                    Maximum address for the buffer.
 *
 * Returns 0 on success, negative errno on error.
 */
static int __locate_mem_hole_top_down(struct kexec_buf *kbuf,
				      u64 buf_min, u64 buf_max)
{
	int ret = -EADDRNOTAVAIL;
	phys_addr_t start, end;
	u64 i;

	for_each_mem_range_rev(i, &memblock.memory, NULL, NUMA_NO_NODE,
			       MEMBLOCK_NONE, &start, &end, NULL) {
		/*
		 * memblock uses [start, end) convention while it is
		 * [start, end] here. Fix the off-by-one to have the
		 * same convention.
		 */
		end -= 1;

		if (start > buf_max)
			continue;

		/* Memory hole not found */
		if (end < buf_min)
			break;

		/* Adjust memory region based on the given range */
		if (start < buf_min)
			start = buf_min;
		if (end > buf_max)
			end = buf_max;

		start = ALIGN(start, kbuf->buf_align);
		if (start < end && (end - start + 1) >= kbuf->memsz) {
			/* Suitable memory range found. Set kbuf->mem */
			kbuf->mem = ALIGN_DOWN(end - kbuf->memsz + 1,
					       kbuf->buf_align);
			ret = 0;
			break;
		}
	}

	return ret;
}

/**
 * locate_mem_hole_top_down_ppc64 - Skip special memory regions to find a
 *                                  suitable buffer with top down approach.
 * @kbuf:                           Buffer contents and memory parameters.
 * @buf_min:                        Minimum address for the buffer.
 * @buf_max:                        Maximum address for the buffer.
 * @emem:                           Exclude memory ranges.
 *
 * Returns 0 on success, negative errno on error.
 */
static int locate_mem_hole_top_down_ppc64(struct kexec_buf *kbuf,
					  u64 buf_min, u64 buf_max,
					  const struct crash_mem *emem)
{
	int i, ret = 0, err = -EADDRNOTAVAIL;
	u64 start, end, tmin, tmax;

	tmax = buf_max;
	for (i = (emem->nr_ranges - 1); i >= 0; i--) {
		start = emem->ranges[i].start;
		end = emem->ranges[i].end;

		if (start > tmax)
			continue;

		if (end < tmax) {
			tmin = (end < buf_min ? buf_min : end + 1);
			ret = __locate_mem_hole_top_down(kbuf, tmin, tmax);
			if (!ret)
				return 0;
		}

		tmax = start - 1;

		if (tmax < buf_min) {
			ret = err;
			break;
		}
		ret = 0;
	}

	if (!ret) {
		tmin = buf_min;
		ret = __locate_mem_hole_top_down(kbuf, tmin, tmax);
	}
	return ret;
}

/**
 * __locate_mem_hole_bottom_up - Looks bottom up for a large enough memory hole
 *                               in the memory regions between buf_min & buf_max
 *                               for the buffer. If found, sets kbuf->mem.
 * @kbuf:                        Buffer contents and memory parameters.
 * @buf_min:                     Minimum address for the buffer.
 * @buf_max:                     Maximum address for the buffer.
 *
 * Returns 0 on success, negative errno on error.
 */
static int __locate_mem_hole_bottom_up(struct kexec_buf *kbuf,
				       u64 buf_min, u64 buf_max)
{
	int ret = -EADDRNOTAVAIL;
	phys_addr_t start, end;
	u64 i;

	for_each_mem_range(i, &memblock.memory, NULL, NUMA_NO_NODE,
			   MEMBLOCK_NONE, &start, &end, NULL) {
		/*
		 * memblock uses [start, end) convention while it is
		 * [start, end] here. Fix the off-by-one to have the
		 * same convention.
		 */
		end -= 1;

		if (end < buf_min)
			continue;

		/* Memory hole not found */
		if (start > buf_max)
			break;

		/* Adjust memory region based on the given range */
		if (start < buf_min)
			start = buf_min;
		if (end > buf_max)
			end = buf_max;

		start = ALIGN(start, kbuf->buf_align);
		if (start < end && (end - start + 1) >= kbuf->memsz) {
			/* Suitable memory range found. Set kbuf->mem */
			kbuf->mem = start;
			ret = 0;
			break;
		}
	}

	return ret;
}

/**
 * locate_mem_hole_bottom_up_ppc64 - Skip special memory regions to find a
 *                                   suitable buffer with bottom up approach.
 * @kbuf:                            Buffer contents and memory parameters.
 * @buf_min:                         Minimum address for the buffer.
 * @buf_max:                         Maximum address for the buffer.
 * @emem:                            Exclude memory ranges.
 *
 * Returns 0 on success, negative errno on error.
 */
static int locate_mem_hole_bottom_up_ppc64(struct kexec_buf *kbuf,
					   u64 buf_min, u64 buf_max,
					   const struct crash_mem *emem)
{
	int i, ret = 0, err = -EADDRNOTAVAIL;
	u64 start, end, tmin, tmax;

	tmin = buf_min;
	for (i = 0; i < emem->nr_ranges; i++) {
		start = emem->ranges[i].start;
		end = emem->ranges[i].end;

		if (end < tmin)
			continue;

		if (start > tmin) {
			tmax = (start > buf_max ? buf_max : start - 1);
			ret = __locate_mem_hole_bottom_up(kbuf, tmin, tmax);
			if (!ret)
				return 0;
		}

		tmin = end + 1;

		if (tmin > buf_max) {
			ret = err;
			break;
		}
		ret = 0;
	}

	if (!ret) {
		tmax = buf_max;
		ret = __locate_mem_hole_bottom_up(kbuf, tmin, tmax);
	}
	return ret;
}

/**
 * setup_purgatory_ppc64 - initialize PPC64 specific purgatory's global
 *                         variables and call setup_purgatory() to initialize
 *                         common global variable.
 * @image:                 kexec image.
 * @slave_code:            Slave code for the purgatory.
 * @fdt:                   Flattened device tree for the next kernel.
 * @kernel_load_addr:      Address where the kernel is loaded.
 * @fdt_load_addr:         Address where the flattened device tree is loaded.
 *
 * Returns 0 on success, negative errno on error.
 */
int setup_purgatory_ppc64(struct kimage *image, const void *slave_code,
			  const void *fdt, unsigned long kernel_load_addr,
			  unsigned long fdt_load_addr)
{
	int ret;

	ret = setup_purgatory(image, slave_code, fdt, kernel_load_addr,
			      fdt_load_addr);
	if (ret)
		pr_err("Failed to setup purgatory symbols");
	return ret;
}

/**
 * setup_new_fdt_ppc64 - Update the flattend device-tree of the kernel
 *                       being loaded.
 * @image:               kexec image being loaded.
 * @fdt:                 Flattened device tree for the next kernel.
 * @initrd_load_addr:    Address where the next initrd will be loaded.
 * @initrd_len:          Size of the next initrd, or 0 if there will be none.
 * @cmdline:             Command line for the next kernel, or NULL if there will
 *                       be none.
 *
 * Returns 0 on success, negative errno on error.
 */
int setup_new_fdt_ppc64(const struct kimage *image, void *fdt,
			unsigned long initrd_load_addr,
			unsigned long initrd_len, const char *cmdline)
{
	return setup_new_fdt(image, fdt, initrd_load_addr, initrd_len, cmdline);
}

/**
 * arch_kexec_locate_mem_hole - Skip special memory regions like rtas, opal,
 *                              tce-table, reserved-ranges & such (exclude
 *                              memory ranges) as they can't be used for kexec
 *                              segment buffer. Sets kbuf->mem when a suitable
 *                              memory hole is found.
 * @kbuf:                       Buffer contents and memory parameters.
 *
 * Assumes minimum of PAGE_SIZE alignment for kbuf->memsz & kbuf->buf_align.
 *
 * Returns 0 on success, negative errno on error.
 */
int arch_kexec_locate_mem_hole(struct kexec_buf *kbuf)
{
	struct crash_mem **emem;
	u64 buf_min, buf_max;
	int ret;

	/*
	 * Use the generic kexec_locate_mem_hole for regular
	 * kexec_file_load syscall
	 */
	if (kbuf->image->type != KEXEC_TYPE_CRASH)
		return kexec_locate_mem_hole(kbuf);

	/* Look up the exclude ranges list while locating the memory hole */
	emem = &(kbuf->image->arch.exclude_ranges);
	if (!(*emem) || ((*emem)->nr_ranges == 0)) {
		pr_warn("No exclude range list. Using the default locate mem hole method\n");
		return kexec_locate_mem_hole(kbuf);
	}

	/* Segments for kdump kernel should be within crashkernel region */
	buf_min = (kbuf->buf_min < crashk_res.start ?
		   crashk_res.start : kbuf->buf_min);
	buf_max = (kbuf->buf_max > crashk_res.end ?
		   crashk_res.end : kbuf->buf_max);

	if (buf_min > buf_max) {
		pr_err("Invalid buffer min and/or max values\n");
		return -EINVAL;
	}

	if (kbuf->top_down)
		ret = locate_mem_hole_top_down_ppc64(kbuf, buf_min, buf_max,
						     *emem);
	else
		ret = locate_mem_hole_bottom_up_ppc64(kbuf, buf_min, buf_max,
						      *emem);

	/* Add the buffer allocated to the exclude list for the next lookup */
	if (!ret) {
		add_mem_range(emem, kbuf->mem, kbuf->memsz);
		sort_memory_ranges(*emem, true);
	} else {
		pr_err("Failed to locate memory buffer of size %lu\n",
		       kbuf->memsz);
	}
	return ret;
}

/**
 * arch_kexec_kernel_image_probe - Does additional handling needed to setup
 *                                 kexec segments.
 * @image:                         kexec image being loaded.
 * @buf:                           Buffer pointing to elf data.
 * @buf_len:                       Length of the buffer.
 *
 * Returns 0 on success, negative errno on error.
 */
int arch_kexec_kernel_image_probe(struct kimage *image, void *buf,
				  unsigned long buf_len)
{
	if (image->type == KEXEC_TYPE_CRASH) {
		int ret;

		/* Get exclude memory ranges needed for setting up kdump segments */
		ret = get_exclude_memory_ranges(&(image->arch.exclude_ranges));
		if (ret)
			pr_err("Failed to setup exclude memory ranges for buffer lookup\n");
		/* Return this until all changes for panic kernel are in */
		return -EOPNOTSUPP;
	}

	return kexec_image_probe_default(image, buf, buf_len);
}

/**
 * arch_kimage_file_post_load_cleanup - Frees up all the allocations done
 *                                      while loading the image.
 * @image:                              kexec image being loaded.
 *
 * Returns 0 on success, negative errno on error.
 */
int arch_kimage_file_post_load_cleanup(struct kimage *image)
{
	kfree(image->arch.exclude_ranges);
	image->arch.exclude_ranges = NULL;

	return kexec_image_post_load_cleanup_default(image);
}
