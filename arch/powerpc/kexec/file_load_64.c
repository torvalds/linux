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
#include <linux/of_device.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/setup.h>
#include <asm/drmem.h>
#include <asm/kexec_ranges.h>
#include <asm/crashdump-ppc64.h>

struct umem_info {
	u64 *buf;		/* data buffer for usable-memory property */
	u32 size;		/* size allocated for the data buffer */
	u32 max_entries;	/* maximum no. of entries */
	u32 idx;		/* index of current entry */

	/* usable memory ranges to look up */
	unsigned int nr_ranges;
	const struct crash_mem_range *ranges;
};

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
 * get_usable_memory_ranges - Get usable memory ranges. This list includes
 *                            regions like crashkernel, opal/rtas & tce-table,
 *                            that kdump kernel could use.
 * @mem_ranges:               Range list to add the memory ranges to.
 *
 * Returns 0 on success, negative errno on error.
 */
static int get_usable_memory_ranges(struct crash_mem **mem_ranges)
{
	int ret;

	/*
	 * Early boot failure observed on guests when low memory (first memory
	 * block?) is not added to usable memory. So, add [0, crashk_res.end]
	 * instead of [crashk_res.start, crashk_res.end] to workaround it.
	 * Also, crashed kernel's memory must be added to reserve map to
	 * avoid kdump kernel from using it.
	 */
	ret = add_mem_range(mem_ranges, 0, crashk_res.end + 1);
	if (ret)
		goto out;

	ret = add_rtas_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_opal_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_tce_mem_ranges(mem_ranges);
out:
	if (ret)
		pr_err("Failed to setup usable memory ranges\n");
	return ret;
}

/**
 * get_crash_memory_ranges - Get crash memory ranges. This list includes
 *                           first/crashing kernel's memory regions that
 *                           would be exported via an elfcore.
 * @mem_ranges:              Range list to add the memory ranges to.
 *
 * Returns 0 on success, negative errno on error.
 */
static int get_crash_memory_ranges(struct crash_mem **mem_ranges)
{
	phys_addr_t base, end;
	struct crash_mem *tmem;
	u64 i;
	int ret;

	for_each_mem_range(i, &base, &end) {
		u64 size = end - base;

		/* Skip backup memory region, which needs a separate entry */
		if (base == BACKUP_SRC_START) {
			if (size > BACKUP_SRC_SIZE) {
				base = BACKUP_SRC_END + 1;
				size -= BACKUP_SRC_SIZE;
			} else
				continue;
		}

		ret = add_mem_range(mem_ranges, base, size);
		if (ret)
			goto out;

		/* Try merging adjacent ranges before reallocation attempt */
		if ((*mem_ranges)->nr_ranges == (*mem_ranges)->max_nr_ranges)
			sort_memory_ranges(*mem_ranges, true);
	}

	/* Reallocate memory ranges if there is no space to split ranges */
	tmem = *mem_ranges;
	if (tmem && (tmem->nr_ranges == tmem->max_nr_ranges)) {
		tmem = realloc_mem_ranges(mem_ranges);
		if (!tmem)
			goto out;
	}

	/* Exclude crashkernel region */
	ret = crash_exclude_mem_range(tmem, crashk_res.start, crashk_res.end);
	if (ret)
		goto out;

	/*
	 * FIXME: For now, stay in parity with kexec-tools but if RTAS/OPAL
	 *        regions are exported to save their context at the time of
	 *        crash, they should actually be backed up just like the
	 *        first 64K bytes of memory.
	 */
	ret = add_rtas_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_opal_mem_range(mem_ranges);
	if (ret)
		goto out;

	/* create a separate program header for the backup region */
	ret = add_mem_range(mem_ranges, BACKUP_SRC_START, BACKUP_SRC_SIZE);
	if (ret)
		goto out;

	sort_memory_ranges(*mem_ranges, false);
out:
	if (ret)
		pr_err("Failed to setup crash memory ranges\n");
	return ret;
}

/**
 * get_reserved_memory_ranges - Get reserve memory ranges. This list includes
 *                              memory regions that should be added to the
 *                              memory reserve map to ensure the region is
 *                              protected from any mischief.
 * @mem_ranges:                 Range list to add the memory ranges to.
 *
 * Returns 0 on success, negative errno on error.
 */
static int get_reserved_memory_ranges(struct crash_mem **mem_ranges)
{
	int ret;

	ret = add_rtas_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_tce_mem_ranges(mem_ranges);
	if (ret)
		goto out;

	ret = add_reserved_mem_ranges(mem_ranges);
out:
	if (ret)
		pr_err("Failed to setup reserved memory ranges\n");
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

	for_each_mem_range_rev(i, &start, &end) {
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

	for_each_mem_range(i, &start, &end) {
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
 * check_realloc_usable_mem - Reallocate buffer if it can't accommodate entries
 * @um_info:                  Usable memory buffer and ranges info.
 * @cnt:                      No. of entries to accommodate.
 *
 * Frees up the old buffer if memory reallocation fails.
 *
 * Returns buffer on success, NULL on error.
 */
static u64 *check_realloc_usable_mem(struct umem_info *um_info, int cnt)
{
	u32 new_size;
	u64 *tbuf;

	if ((um_info->idx + cnt) <= um_info->max_entries)
		return um_info->buf;

	new_size = um_info->size + MEM_RANGE_CHUNK_SZ;
	tbuf = krealloc(um_info->buf, new_size, GFP_KERNEL);
	if (tbuf) {
		um_info->buf = tbuf;
		um_info->size = new_size;
		um_info->max_entries = (um_info->size / sizeof(u64));
	}

	return tbuf;
}

/**
 * add_usable_mem - Add the usable memory ranges within the given memory range
 *                  to the buffer
 * @um_info:        Usable memory buffer and ranges info.
 * @base:           Base address of memory range to look for.
 * @end:            End address of memory range to look for.
 *
 * Returns 0 on success, negative errno on error.
 */
static int add_usable_mem(struct umem_info *um_info, u64 base, u64 end)
{
	u64 loc_base, loc_end;
	bool add;
	int i;

	for (i = 0; i < um_info->nr_ranges; i++) {
		add = false;
		loc_base = um_info->ranges[i].start;
		loc_end = um_info->ranges[i].end;
		if (loc_base >= base && loc_end <= end)
			add = true;
		else if (base < loc_end && end > loc_base) {
			if (loc_base < base)
				loc_base = base;
			if (loc_end > end)
				loc_end = end;
			add = true;
		}

		if (add) {
			if (!check_realloc_usable_mem(um_info, 2))
				return -ENOMEM;

			um_info->buf[um_info->idx++] = cpu_to_be64(loc_base);
			um_info->buf[um_info->idx++] =
					cpu_to_be64(loc_end - loc_base + 1);
		}
	}

	return 0;
}

/**
 * kdump_setup_usable_lmb - This is a callback function that gets called by
 *                          walk_drmem_lmbs for every LMB to set its
 *                          usable memory ranges.
 * @lmb:                    LMB info.
 * @usm:                    linux,drconf-usable-memory property value.
 * @data:                   Pointer to usable memory buffer and ranges info.
 *
 * Returns 0 on success, negative errno on error.
 */
static int kdump_setup_usable_lmb(struct drmem_lmb *lmb, const __be32 **usm,
				  void *data)
{
	struct umem_info *um_info;
	int tmp_idx, ret;
	u64 base, end;

	/*
	 * kdump load isn't supported on kernels already booted with
	 * linux,drconf-usable-memory property.
	 */
	if (*usm) {
		pr_err("linux,drconf-usable-memory property already exists!");
		return -EINVAL;
	}

	um_info = data;
	tmp_idx = um_info->idx;
	if (!check_realloc_usable_mem(um_info, 1))
		return -ENOMEM;

	um_info->idx++;
	base = lmb->base_addr;
	end = base + drmem_lmb_size() - 1;
	ret = add_usable_mem(um_info, base, end);
	if (!ret) {
		/*
		 * Update the no. of ranges added. Two entries (base & size)
		 * for every range added.
		 */
		um_info->buf[tmp_idx] =
				cpu_to_be64((um_info->idx - tmp_idx - 1) / 2);
	}

	return ret;
}

#define NODE_PATH_LEN		256
/**
 * add_usable_mem_property - Add usable memory property for the given
 *                           memory node.
 * @fdt:                     Flattened device tree for the kdump kernel.
 * @dn:                      Memory node.
 * @um_info:                 Usable memory buffer and ranges info.
 *
 * Returns 0 on success, negative errno on error.
 */
static int add_usable_mem_property(void *fdt, struct device_node *dn,
				   struct umem_info *um_info)
{
	int n_mem_addr_cells, n_mem_size_cells, node;
	char path[NODE_PATH_LEN];
	int i, len, ranges, ret;
	const __be32 *prop;
	u64 base, end;

	of_node_get(dn);

	if (snprintf(path, NODE_PATH_LEN, "%pOF", dn) > (NODE_PATH_LEN - 1)) {
		pr_err("Buffer (%d) too small for memory node: %pOF\n",
		       NODE_PATH_LEN, dn);
		return -EOVERFLOW;
	}
	pr_debug("Memory node path: %s\n", path);

	/* Now that we know the path, find its offset in kdump kernel's fdt */
	node = fdt_path_offset(fdt, path);
	if (node < 0) {
		pr_err("Malformed device tree: error reading %s\n", path);
		ret = -EINVAL;
		goto out;
	}

	/* Get the address & size cells */
	n_mem_addr_cells = of_n_addr_cells(dn);
	n_mem_size_cells = of_n_size_cells(dn);
	pr_debug("address cells: %d, size cells: %d\n", n_mem_addr_cells,
		 n_mem_size_cells);

	um_info->idx  = 0;
	if (!check_realloc_usable_mem(um_info, 2)) {
		ret = -ENOMEM;
		goto out;
	}

	prop = of_get_property(dn, "reg", &len);
	if (!prop || len <= 0) {
		ret = 0;
		goto out;
	}

	/*
	 * "reg" property represents sequence of (addr,size) tuples
	 * each representing a memory range.
	 */
	ranges = (len >> 2) / (n_mem_addr_cells + n_mem_size_cells);

	for (i = 0; i < ranges; i++) {
		base = of_read_number(prop, n_mem_addr_cells);
		prop += n_mem_addr_cells;
		end = base + of_read_number(prop, n_mem_size_cells) - 1;
		prop += n_mem_size_cells;

		ret = add_usable_mem(um_info, base, end);
		if (ret)
			goto out;
	}

	/*
	 * No kdump kernel usable memory found in this memory node.
	 * Write (0,0) tuple in linux,usable-memory property for
	 * this region to be ignored.
	 */
	if (um_info->idx == 0) {
		um_info->buf[0] = 0;
		um_info->buf[1] = 0;
		um_info->idx = 2;
	}

	ret = fdt_setprop(fdt, node, "linux,usable-memory", um_info->buf,
			  (um_info->idx * sizeof(u64)));

out:
	of_node_put(dn);
	return ret;
}


/**
 * update_usable_mem_fdt - Updates kdump kernel's fdt with linux,usable-memory
 *                         and linux,drconf-usable-memory DT properties as
 *                         appropriate to restrict its memory usage.
 * @fdt:                   Flattened device tree for the kdump kernel.
 * @usable_mem:            Usable memory ranges for kdump kernel.
 *
 * Returns 0 on success, negative errno on error.
 */
static int update_usable_mem_fdt(void *fdt, struct crash_mem *usable_mem)
{
	struct umem_info um_info;
	struct device_node *dn;
	int node, ret = 0;

	if (!usable_mem) {
		pr_err("Usable memory ranges for kdump kernel not found\n");
		return -ENOENT;
	}

	node = fdt_path_offset(fdt, "/ibm,dynamic-reconfiguration-memory");
	if (node == -FDT_ERR_NOTFOUND)
		pr_debug("No dynamic reconfiguration memory found\n");
	else if (node < 0) {
		pr_err("Malformed device tree: error reading /ibm,dynamic-reconfiguration-memory.\n");
		return -EINVAL;
	}

	um_info.buf  = NULL;
	um_info.size = 0;
	um_info.max_entries = 0;
	um_info.idx  = 0;
	/* Memory ranges to look up */
	um_info.ranges = &(usable_mem->ranges[0]);
	um_info.nr_ranges = usable_mem->nr_ranges;

	dn = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
	if (dn) {
		ret = walk_drmem_lmbs(dn, &um_info, kdump_setup_usable_lmb);
		of_node_put(dn);

		if (ret) {
			pr_err("Could not setup linux,drconf-usable-memory property for kdump\n");
			goto out;
		}

		ret = fdt_setprop(fdt, node, "linux,drconf-usable-memory",
				  um_info.buf, (um_info.idx * sizeof(u64)));
		if (ret) {
			pr_err("Failed to update fdt with linux,drconf-usable-memory property");
			goto out;
		}
	}

	/*
	 * Walk through each memory node and set linux,usable-memory property
	 * for the corresponding node in kdump kernel's fdt.
	 */
	for_each_node_by_type(dn, "memory") {
		ret = add_usable_mem_property(fdt, dn, &um_info);
		if (ret) {
			pr_err("Failed to set linux,usable-memory property for %s node",
			       dn->full_name);
			goto out;
		}
	}

out:
	kfree(um_info.buf);
	return ret;
}

/**
 * load_backup_segment - Locate a memory hole to place the backup region.
 * @image:               Kexec image.
 * @kbuf:                Buffer contents and memory parameters.
 *
 * Returns 0 on success, negative errno on error.
 */
static int load_backup_segment(struct kimage *image, struct kexec_buf *kbuf)
{
	void *buf;
	int ret;

	/*
	 * Setup a source buffer for backup segment.
	 *
	 * A source buffer has no meaning for backup region as data will
	 * be copied from backup source, after crash, in the purgatory.
	 * But as load segment code doesn't recognize such segments,
	 * setup a dummy source buffer to keep it happy for now.
	 */
	buf = vzalloc(BACKUP_SRC_SIZE);
	if (!buf)
		return -ENOMEM;

	kbuf->buffer = buf;
	kbuf->mem = KEXEC_BUF_MEM_UNKNOWN;
	kbuf->bufsz = kbuf->memsz = BACKUP_SRC_SIZE;
	kbuf->top_down = false;

	ret = kexec_add_buffer(kbuf);
	if (ret) {
		vfree(buf);
		return ret;
	}

	image->arch.backup_buf = buf;
	image->arch.backup_start = kbuf->mem;
	return 0;
}

/**
 * update_backup_region_phdr - Update backup region's offset for the core to
 *                             export the region appropriately.
 * @image:                     Kexec image.
 * @ehdr:                      ELF core header.
 *
 * Assumes an exclusive program header is setup for the backup region
 * in the ELF headers
 *
 * Returns nothing.
 */
static void update_backup_region_phdr(struct kimage *image, Elf64_Ehdr *ehdr)
{
	Elf64_Phdr *phdr;
	unsigned int i;

	phdr = (Elf64_Phdr *)(ehdr + 1);
	for (i = 0; i < ehdr->e_phnum; i++) {
		if (phdr->p_paddr == BACKUP_SRC_START) {
			phdr->p_offset = image->arch.backup_start;
			pr_debug("Backup region offset updated to 0x%lx\n",
				 image->arch.backup_start);
			return;
		}
	}
}

/**
 * load_elfcorehdr_segment - Setup crash memory ranges and initialize elfcorehdr
 *                           segment needed to load kdump kernel.
 * @image:                   Kexec image.
 * @kbuf:                    Buffer contents and memory parameters.
 *
 * Returns 0 on success, negative errno on error.
 */
static int load_elfcorehdr_segment(struct kimage *image, struct kexec_buf *kbuf)
{
	struct crash_mem *cmem = NULL;
	unsigned long headers_sz;
	void *headers = NULL;
	int ret;

	ret = get_crash_memory_ranges(&cmem);
	if (ret)
		goto out;

	/* Setup elfcorehdr segment */
	ret = crash_prepare_elf64_headers(cmem, false, &headers, &headers_sz);
	if (ret) {
		pr_err("Failed to prepare elf headers for the core\n");
		goto out;
	}

	/* Fix the offset for backup region in the ELF header */
	update_backup_region_phdr(image, headers);

	kbuf->buffer = headers;
	kbuf->mem = KEXEC_BUF_MEM_UNKNOWN;
	kbuf->bufsz = kbuf->memsz = headers_sz;
	kbuf->top_down = false;

	ret = kexec_add_buffer(kbuf);
	if (ret) {
		vfree(headers);
		goto out;
	}

	image->arch.elfcorehdr_addr = kbuf->mem;
	image->arch.elf_headers_sz = headers_sz;
	image->arch.elf_headers = headers;
out:
	kfree(cmem);
	return ret;
}

/**
 * load_crashdump_segments_ppc64 - Initialize the additional segements needed
 *                                 to load kdump kernel.
 * @image:                         Kexec image.
 * @kbuf:                          Buffer contents and memory parameters.
 *
 * Returns 0 on success, negative errno on error.
 */
int load_crashdump_segments_ppc64(struct kimage *image,
				  struct kexec_buf *kbuf)
{
	int ret;

	/* Load backup segment - first 64K bytes of the crashing kernel */
	ret = load_backup_segment(image, kbuf);
	if (ret) {
		pr_err("Failed to load backup segment\n");
		return ret;
	}
	pr_debug("Loaded the backup region at 0x%lx\n", kbuf->mem);

	/* Load elfcorehdr segment - to export crashing kernel's vmcore */
	ret = load_elfcorehdr_segment(image, kbuf);
	if (ret) {
		pr_err("Failed to load elfcorehdr segment\n");
		return ret;
	}
	pr_debug("Loaded elf core header at 0x%lx, bufsz=0x%lx memsz=0x%lx\n",
		 image->arch.elfcorehdr_addr, kbuf->bufsz, kbuf->memsz);

	return 0;
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
	struct device_node *dn = NULL;
	int ret;

	ret = setup_purgatory(image, slave_code, fdt, kernel_load_addr,
			      fdt_load_addr);
	if (ret)
		goto out;

	if (image->type == KEXEC_TYPE_CRASH) {
		u32 my_run_at_load = 1;

		/*
		 * Tell relocatable kernel to run at load address
		 * via the word meant for that at 0x5c.
		 */
		ret = kexec_purgatory_get_set_symbol(image, "run_at_load",
						     &my_run_at_load,
						     sizeof(my_run_at_load),
						     false);
		if (ret)
			goto out;
	}

	/* Tell purgatory where to look for backup region */
	ret = kexec_purgatory_get_set_symbol(image, "backup_start",
					     &image->arch.backup_start,
					     sizeof(image->arch.backup_start),
					     false);
	if (ret)
		goto out;

	/* Setup OPAL base & entry values */
	dn = of_find_node_by_path("/ibm,opal");
	if (dn) {
		u64 val;

		of_property_read_u64(dn, "opal-base-address", &val);
		ret = kexec_purgatory_get_set_symbol(image, "opal_base", &val,
						     sizeof(val), false);
		if (ret)
			goto out;

		of_property_read_u64(dn, "opal-entry-address", &val);
		ret = kexec_purgatory_get_set_symbol(image, "opal_entry", &val,
						     sizeof(val), false);
	}
out:
	if (ret)
		pr_err("Failed to setup purgatory symbols");
	of_node_put(dn);
	return ret;
}

/**
 * kexec_fdt_totalsize_ppc64 - Return the estimated size needed to setup FDT
 *                             for kexec/kdump kernel.
 * @image:                     kexec image being loaded.
 *
 * Returns the estimated size needed for kexec/kdump kernel FDT.
 */
unsigned int kexec_fdt_totalsize_ppc64(struct kimage *image)
{
	unsigned int fdt_size;
	u64 usm_entries;

	/*
	 * The below estimate more than accounts for a typical kexec case where
	 * the additional space is to accommodate things like kexec cmdline,
	 * chosen node with properties for initrd start & end addresses and
	 * a property to indicate kexec boot..
	 */
	fdt_size = fdt_totalsize(initial_boot_params) + (2 * COMMAND_LINE_SIZE);
	if (image->type != KEXEC_TYPE_CRASH)
		return fdt_size;

	/*
	 * For kdump kernel, also account for linux,usable-memory and
	 * linux,drconf-usable-memory properties. Get an approximate on the
	 * number of usable memory entries and use for FDT size estimation.
	 */
	usm_entries = ((memblock_end_of_DRAM() / drmem_lmb_size()) +
		       (2 * (resource_size(&crashk_res) / drmem_lmb_size())));
	fdt_size += (unsigned int)(usm_entries * sizeof(u64));

	return fdt_size;
}

/**
 * add_node_props - Reads node properties from device node structure and add
 *                  them to fdt.
 * @fdt:            Flattened device tree of the kernel
 * @node_offset:    offset of the node to add a property at
 * @dn:             device node pointer
 *
 * Returns 0 on success, negative errno on error.
 */
static int add_node_props(void *fdt, int node_offset, const struct device_node *dn)
{
	int ret = 0;
	struct property *pp;

	if (!dn)
		return -EINVAL;

	for_each_property_of_node(dn, pp) {
		ret = fdt_setprop(fdt, node_offset, pp->name, pp->value, pp->length);
		if (ret < 0) {
			pr_err("Unable to add %s property: %s\n", pp->name, fdt_strerror(ret));
			return ret;
		}
	}
	return ret;
}

/**
 * update_cpus_node - Update cpus node of flattened device tree using of_root
 *                    device node.
 * @fdt:              Flattened device tree of the kernel.
 *
 * Returns 0 on success, negative errno on error.
 */
static int update_cpus_node(void *fdt)
{
	struct device_node *cpus_node, *dn;
	int cpus_offset, cpus_subnode_offset, ret = 0;

	cpus_offset = fdt_path_offset(fdt, "/cpus");
	if (cpus_offset < 0 && cpus_offset != -FDT_ERR_NOTFOUND) {
		pr_err("Malformed device tree: error reading /cpus node: %s\n",
		       fdt_strerror(cpus_offset));
		return cpus_offset;
	}

	if (cpus_offset > 0) {
		ret = fdt_del_node(fdt, cpus_offset);
		if (ret < 0) {
			pr_err("Error deleting /cpus node: %s\n", fdt_strerror(ret));
			return -EINVAL;
		}
	}

	/* Add cpus node to fdt */
	cpus_offset = fdt_add_subnode(fdt, fdt_path_offset(fdt, "/"), "cpus");
	if (cpus_offset < 0) {
		pr_err("Error creating /cpus node: %s\n", fdt_strerror(cpus_offset));
		return -EINVAL;
	}

	/* Add cpus node properties */
	cpus_node = of_find_node_by_path("/cpus");
	ret = add_node_props(fdt, cpus_offset, cpus_node);
	of_node_put(cpus_node);
	if (ret < 0)
		return ret;

	/* Loop through all subnodes of cpus and add them to fdt */
	for_each_node_by_type(dn, "cpu") {
		cpus_subnode_offset = fdt_add_subnode(fdt, cpus_offset, dn->full_name);
		if (cpus_subnode_offset < 0) {
			pr_err("Unable to add %s subnode: %s\n", dn->full_name,
			       fdt_strerror(cpus_subnode_offset));
			ret = cpus_subnode_offset;
			goto out;
		}

		ret = add_node_props(fdt, cpus_subnode_offset, dn);
		if (ret < 0)
			goto out;
	}
out:
	of_node_put(dn);
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
	struct crash_mem *umem = NULL, *rmem = NULL;
	int i, nr_ranges, ret;

	ret = setup_new_fdt(image, fdt, initrd_load_addr, initrd_len, cmdline);
	if (ret)
		goto out;

	/*
	 * Restrict memory usage for kdump kernel by setting up
	 * usable memory ranges and memory reserve map.
	 */
	if (image->type == KEXEC_TYPE_CRASH) {
		ret = get_usable_memory_ranges(&umem);
		if (ret)
			goto out;

		ret = update_usable_mem_fdt(fdt, umem);
		if (ret) {
			pr_err("Error setting up usable-memory property for kdump kernel\n");
			goto out;
		}

		/*
		 * Ensure we don't touch crashed kernel's memory except the
		 * first 64K of RAM, which will be backed up.
		 */
		ret = fdt_add_mem_rsv(fdt, BACKUP_SRC_END + 1,
				      crashk_res.start - BACKUP_SRC_SIZE);
		if (ret) {
			pr_err("Error reserving crash memory: %s\n",
			       fdt_strerror(ret));
			goto out;
		}

		/* Ensure backup region is not used by kdump/capture kernel */
		ret = fdt_add_mem_rsv(fdt, image->arch.backup_start,
				      BACKUP_SRC_SIZE);
		if (ret) {
			pr_err("Error reserving memory for backup: %s\n",
			       fdt_strerror(ret));
			goto out;
		}
	}

	/* Update cpus nodes information to account hotplug CPUs. */
	ret =  update_cpus_node(fdt);
	if (ret < 0)
		goto out;

	/* Update memory reserve map */
	ret = get_reserved_memory_ranges(&rmem);
	if (ret)
		goto out;

	nr_ranges = rmem ? rmem->nr_ranges : 0;
	for (i = 0; i < nr_ranges; i++) {
		u64 base, size;

		base = rmem->ranges[i].start;
		size = rmem->ranges[i].end - base + 1;
		ret = fdt_add_mem_rsv(fdt, base, size);
		if (ret) {
			pr_err("Error updating memory reserve map: %s\n",
			       fdt_strerror(ret));
			goto out;
		}
	}

out:
	kfree(rmem);
	kfree(umem);
	return ret;
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

	/* Look up the exclude ranges list while locating the memory hole */
	emem = &(kbuf->image->arch.exclude_ranges);
	if (!(*emem) || ((*emem)->nr_ranges == 0)) {
		pr_warn("No exclude range list. Using the default locate mem hole method\n");
		return kexec_locate_mem_hole(kbuf);
	}

	buf_min = kbuf->buf_min;
	buf_max = kbuf->buf_max;
	/* Segments for kdump kernel should be within crashkernel region */
	if (kbuf->image->type == KEXEC_TYPE_CRASH) {
		buf_min = (buf_min < crashk_res.start ?
			   crashk_res.start : buf_min);
		buf_max = (buf_max > crashk_res.end ?
			   crashk_res.end : buf_max);
	}

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
	int ret;

	/* Get exclude memory ranges needed for setting up kexec segments */
	ret = get_exclude_memory_ranges(&(image->arch.exclude_ranges));
	if (ret) {
		pr_err("Failed to setup exclude memory ranges for buffer lookup\n");
		return ret;
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

	vfree(image->arch.backup_buf);
	image->arch.backup_buf = NULL;

	vfree(image->arch.elf_headers);
	image->arch.elf_headers = NULL;
	image->arch.elf_headers_sz = 0;

	return kexec_image_post_load_cleanup_default(image);
}
