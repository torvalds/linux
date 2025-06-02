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
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/setup.h>
#include <asm/drmem.h>
#include <asm/firmware.h>
#include <asm/kexec_ranges.h>
#include <asm/crashdump-ppc64.h>
#include <asm/mmzone.h>
#include <asm/iommu.h>
#include <asm/prom.h>
#include <asm/plpks.h>
#include <asm/cputhreads.h>

struct umem_info {
	__be64 *buf;		/* data buffer for usable-memory property */
	u32 size;		/* size allocated for the data buffer */
	u32 max_entries;	/* maximum no. of entries */
	u32 idx;		/* index of current entry */

	/* usable memory ranges to look up */
	unsigned int nr_ranges;
	const struct range *ranges;
};

const struct kexec_file_ops * const kexec_file_loaders[] = {
	&kexec_elf64_ops,
	NULL
};

int arch_check_excluded_range(struct kimage *image, unsigned long start,
			      unsigned long end)
{
	struct crash_mem *emem;
	int i;

	emem = image->arch.exclude_ranges;
	for (i = 0; i < emem->nr_ranges; i++)
		if (start < emem->ranges[i].end && end > emem->ranges[i].start)
			return 1;

	return 0;
}

#ifdef CONFIG_CRASH_DUMP
/**
 * check_realloc_usable_mem - Reallocate buffer if it can't accommodate entries
 * @um_info:                  Usable memory buffer and ranges info.
 * @cnt:                      No. of entries to accommodate.
 *
 * Frees up the old buffer if memory reallocation fails.
 *
 * Returns buffer on success, NULL on error.
 */
static __be64 *check_realloc_usable_mem(struct umem_info *um_info, int cnt)
{
	u32 new_size;
	__be64 *tbuf;

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
	int node;
	char path[NODE_PATH_LEN];
	int i, ret;
	u64 base, size;

	of_node_get(dn);

	if (snprintf(path, NODE_PATH_LEN, "%pOF", dn) > (NODE_PATH_LEN - 1)) {
		pr_err("Buffer (%d) too small for memory node: %pOF\n",
		       NODE_PATH_LEN, dn);
		return -EOVERFLOW;
	}
	kexec_dprintk("Memory node path: %s\n", path);

	/* Now that we know the path, find its offset in kdump kernel's fdt */
	node = fdt_path_offset(fdt, path);
	if (node < 0) {
		pr_err("Malformed device tree: error reading %s\n", path);
		ret = -EINVAL;
		goto out;
	}

	um_info->idx  = 0;
	if (!check_realloc_usable_mem(um_info, 2)) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * "reg" property represents sequence of (addr,size) tuples
	 * each representing a memory range.
	 */
	for (i = 0; ; i++) {
		ret = of_property_read_reg(dn, i, &base, &size);
		if (ret)
			break;

		ret = add_usable_mem(um_info, base, base + size - 1);
		if (ret)
			goto out;
	}

	// No reg or empty reg? Skip this node.
	if (i == 0)
		goto out;

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
		kexec_dprintk("No dynamic reconfiguration memory found\n");
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
			pr_err("Failed to update fdt with linux,drconf-usable-memory property: %s",
			       fdt_strerror(ret));
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
			of_node_put(dn);
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
			kexec_dprintk("Backup region offset updated to 0x%lx\n",
				      image->arch.backup_start);
			return;
		}
	}
}

static unsigned int kdump_extra_elfcorehdr_size(struct crash_mem *cmem)
{
#if defined(CONFIG_CRASH_HOTPLUG) && defined(CONFIG_MEMORY_HOTPLUG)
	unsigned int extra_sz = 0;

	if (CONFIG_CRASH_MAX_MEMORY_RANGES > (unsigned int)PN_XNUM)
		pr_warn("Number of Phdrs %u exceeds max\n", CONFIG_CRASH_MAX_MEMORY_RANGES);
	else if (cmem->nr_ranges >= CONFIG_CRASH_MAX_MEMORY_RANGES)
		pr_warn("Configured crash mem ranges may not be enough\n");
	else
		extra_sz = (CONFIG_CRASH_MAX_MEMORY_RANGES - cmem->nr_ranges) * sizeof(Elf64_Phdr);

	return extra_sz;
#endif
	return 0;
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
	kbuf->bufsz = headers_sz;
	kbuf->memsz = headers_sz + kdump_extra_elfcorehdr_size(cmem);
	kbuf->top_down = false;

	ret = kexec_add_buffer(kbuf);
	if (ret) {
		vfree(headers);
		goto out;
	}

	image->elf_load_addr = kbuf->mem;
	image->elf_headers_sz = headers_sz;
	image->elf_headers = headers;
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
	kexec_dprintk("Loaded the backup region at 0x%lx\n", kbuf->mem);

	/* Load elfcorehdr segment - to export crashing kernel's vmcore */
	ret = load_elfcorehdr_segment(image, kbuf);
	if (ret) {
		pr_err("Failed to load elfcorehdr segment\n");
		return ret;
	}
	kexec_dprintk("Loaded elf core header at 0x%lx, bufsz=0x%lx memsz=0x%lx\n",
		      image->elf_load_addr, kbuf->bufsz, kbuf->memsz);

	return 0;
}
#endif

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

		ret = of_property_read_u64(dn, "opal-base-address", &val);
		if (ret)
			goto out;

		ret = kexec_purgatory_get_set_symbol(image, "opal_base", &val,
						     sizeof(val), false);
		if (ret)
			goto out;

		ret = of_property_read_u64(dn, "opal-entry-address", &val);
		if (ret)
			goto out;
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
 * cpu_node_size - Compute the size of a CPU node in the FDT.
 *                 This should be done only once and the value is stored in
 *                 a static variable.
 * Returns the max size of a CPU node in the FDT.
 */
static unsigned int cpu_node_size(void)
{
	static unsigned int size;
	struct device_node *dn;
	struct property *pp;

	/*
	 * Don't compute it twice, we are assuming that the per CPU node size
	 * doesn't change during the system's life.
	 */
	if (size)
		return size;

	dn = of_find_node_by_type(NULL, "cpu");
	if (WARN_ON_ONCE(!dn)) {
		// Unlikely to happen
		return 0;
	}

	/*
	 * We compute the sub node size for a CPU node, assuming it
	 * will be the same for all.
	 */
	size += strlen(dn->name) + 5;
	for_each_property_of_node(dn, pp) {
		size += strlen(pp->name);
		size += pp->length;
	}

	of_node_put(dn);
	return size;
}

static unsigned int kdump_extra_fdt_size_ppc64(struct kimage *image, unsigned int cpu_nodes)
{
	unsigned int extra_size = 0;
	u64 usm_entries;
#ifdef CONFIG_CRASH_HOTPLUG
	unsigned int possible_cpu_nodes;
#endif

	if (!IS_ENABLED(CONFIG_CRASH_DUMP) || image->type != KEXEC_TYPE_CRASH)
		return 0;

	/*
	 * For kdump kernel, account for linux,usable-memory and
	 * linux,drconf-usable-memory properties. Get an approximate on the
	 * number of usable memory entries and use for FDT size estimation.
	 */
	if (drmem_lmb_size()) {
		usm_entries = ((memory_hotplug_max() / drmem_lmb_size()) +
			       (2 * (resource_size(&crashk_res) / drmem_lmb_size())));
		extra_size += (unsigned int)(usm_entries * sizeof(u64));
	}

#ifdef CONFIG_CRASH_HOTPLUG
	/*
	 * Make sure enough space is reserved to accommodate possible CPU nodes
	 * in the crash FDT. This allows packing possible CPU nodes which are
	 * not yet present in the system without regenerating the entire FDT.
	 */
	if (image->type == KEXEC_TYPE_CRASH) {
		possible_cpu_nodes = num_possible_cpus() / threads_per_core;
		if (possible_cpu_nodes > cpu_nodes)
			extra_size += (possible_cpu_nodes - cpu_nodes) * cpu_node_size();
	}
#endif

	return extra_size;
}

/**
 * kexec_extra_fdt_size_ppc64 - Return the estimated additional size needed to
 *                              setup FDT for kexec/kdump kernel.
 * @image:                      kexec image being loaded.
 *
 * Returns the estimated extra size needed for kexec/kdump kernel FDT.
 */
unsigned int kexec_extra_fdt_size_ppc64(struct kimage *image, struct crash_mem *rmem)
{
	struct device_node *dn;
	unsigned int cpu_nodes = 0, extra_size = 0;

	// Budget some space for the password blob. There's already extra space
	// for the key name
	if (plpks_is_available())
		extra_size += (unsigned int)plpks_get_passwordlen();

	/* Get the number of CPU nodes in the current device tree */
	for_each_node_by_type(dn, "cpu") {
		cpu_nodes++;
	}

	/* Consider extra space for CPU nodes added since the boot time */
	if (cpu_nodes > boot_cpu_node_count)
		extra_size += (cpu_nodes - boot_cpu_node_count) * cpu_node_size();

	/* Consider extra space for reserved memory ranges if any */
	if (rmem->nr_ranges > 0)
		extra_size += sizeof(struct fdt_reserve_entry) * rmem->nr_ranges;

	return extra_size + kdump_extra_fdt_size_ppc64(image, cpu_nodes);
}

static int copy_property(void *fdt, int node_offset, const struct device_node *dn,
			 const char *propname)
{
	const void *prop, *fdtprop;
	int len = 0, fdtlen = 0;

	prop = of_get_property(dn, propname, &len);
	fdtprop = fdt_getprop(fdt, node_offset, propname, &fdtlen);

	if (fdtprop && !prop)
		return fdt_delprop(fdt, node_offset, propname);
	else if (prop)
		return fdt_setprop(fdt, node_offset, propname, prop, len);
	else
		return -FDT_ERR_NOTFOUND;
}

static int update_pci_dma_nodes(void *fdt, const char *dmapropname)
{
	struct device_node *dn;
	int pci_offset, root_offset, ret = 0;

	if (!firmware_has_feature(FW_FEATURE_LPAR))
		return 0;

	root_offset = fdt_path_offset(fdt, "/");
	for_each_node_with_property(dn, dmapropname) {
		pci_offset = fdt_subnode_offset(fdt, root_offset, of_node_full_name(dn));
		if (pci_offset < 0)
			continue;

		ret = copy_property(fdt, pci_offset, dn, "ibm,dma-window");
		if (ret < 0) {
			of_node_put(dn);
			break;
		}
		ret = copy_property(fdt, pci_offset, dn, dmapropname);
		if (ret < 0) {
			of_node_put(dn);
			break;
		}
	}

	return ret;
}

/**
 * setup_new_fdt_ppc64 - Update the flattend device-tree of the kernel
 *                       being loaded.
 * @image:               kexec image being loaded.
 * @fdt:                 Flattened device tree for the next kernel.
 * @rmem:                Reserved memory ranges.
 *
 * Returns 0 on success, negative errno on error.
 */
int setup_new_fdt_ppc64(const struct kimage *image, void *fdt, struct crash_mem *rmem)
{
	struct crash_mem *umem = NULL;
	int i, nr_ranges, ret;

#ifdef CONFIG_CRASH_DUMP
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
#endif

	/* Update cpus nodes information to account hotplug CPUs. */
	ret =  update_cpus_node(fdt);
	if (ret < 0)
		goto out;

	ret = update_pci_dma_nodes(fdt, DIRECT64_PROPNAME);
	if (ret < 0)
		goto out;

	ret = update_pci_dma_nodes(fdt, DMA64_PROPNAME);
	if (ret < 0)
		goto out;

	/* Update memory reserve map */
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

	// If we have PLPKS active, we need to provide the password to the new kernel
	if (plpks_is_available())
		ret = plpks_populate_fdt(fdt);

out:
	kfree(umem);
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

	vfree(image->elf_headers);
	image->elf_headers = NULL;
	image->elf_headers_sz = 0;

	kvfree(image->arch.fdt);
	image->arch.fdt = NULL;

	return kexec_image_post_load_cleanup_default(image);
}
