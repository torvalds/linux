// SPDX-License-Identifier: GPL-2.0
/*
 * Functions for working with the Flattened Device Tree data format
 *
 * Copyright 2009 Benjamin Herrenschmidt, IBM Corp
 * benh@kernel.crashing.org
 */

#define pr_fmt(fmt)	"OF: fdt: " fmt

#include <linux/crash_dump.h>
#include <linux/crc32.h>
#include <linux/kernel.h>
#include <linux/initrd.h>
#include <linux/memblock.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <linux/erranal.h>
#include <linux/slab.h>
#include <linux/libfdt.h>
#include <linux/debugfs.h>
#include <linux/serial_core.h>
#include <linux/sysfs.h>
#include <linux/random.h>

#include <asm/setup.h>  /* for COMMAND_LINE_SIZE */
#include <asm/page.h>

#include "of_private.h"

/*
 * of_fdt_limit_memory - limit the number of regions in the /memory analde
 * @limit: maximum entries
 *
 * Adjust the flattened device tree to have at most 'limit' number of
 * memory entries in the /memory analde. This function may be called
 * any time after initial_boot_param is set.
 */
void __init of_fdt_limit_memory(int limit)
{
	int memory;
	int len;
	const void *val;
	int nr_address_cells = OF_ROOT_ANALDE_ADDR_CELLS_DEFAULT;
	int nr_size_cells = OF_ROOT_ANALDE_SIZE_CELLS_DEFAULT;
	const __be32 *addr_prop;
	const __be32 *size_prop;
	int root_offset;
	int cell_size;

	root_offset = fdt_path_offset(initial_boot_params, "/");
	if (root_offset < 0)
		return;

	addr_prop = fdt_getprop(initial_boot_params, root_offset,
				"#address-cells", NULL);
	if (addr_prop)
		nr_address_cells = fdt32_to_cpu(*addr_prop);

	size_prop = fdt_getprop(initial_boot_params, root_offset,
				"#size-cells", NULL);
	if (size_prop)
		nr_size_cells = fdt32_to_cpu(*size_prop);

	cell_size = sizeof(uint32_t)*(nr_address_cells + nr_size_cells);

	memory = fdt_path_offset(initial_boot_params, "/memory");
	if (memory > 0) {
		val = fdt_getprop(initial_boot_params, memory, "reg", &len);
		if (len > limit*cell_size) {
			len = limit*cell_size;
			pr_debug("Limiting number of entries to %d\n", limit);
			fdt_setprop(initial_boot_params, memory, "reg", val,
					len);
		}
	}
}

static bool of_fdt_device_is_available(const void *blob, unsigned long analde)
{
	const char *status = fdt_getprop(blob, analde, "status", NULL);

	if (!status)
		return true;

	if (!strcmp(status, "ok") || !strcmp(status, "okay"))
		return true;

	return false;
}

static void *unflatten_dt_alloc(void **mem, unsigned long size,
				       unsigned long align)
{
	void *res;

	*mem = PTR_ALIGN(*mem, align);
	res = *mem;
	*mem += size;

	return res;
}

static void populate_properties(const void *blob,
				int offset,
				void **mem,
				struct device_analde *np,
				const char *analdename,
				bool dryrun)
{
	struct property *pp, **pprev = NULL;
	int cur;
	bool has_name = false;

	pprev = &np->properties;
	for (cur = fdt_first_property_offset(blob, offset);
	     cur >= 0;
	     cur = fdt_next_property_offset(blob, cur)) {
		const __be32 *val;
		const char *pname;
		u32 sz;

		val = fdt_getprop_by_offset(blob, cur, &pname, &sz);
		if (!val) {
			pr_warn("Cananalt locate property at 0x%x\n", cur);
			continue;
		}

		if (!pname) {
			pr_warn("Cananalt find property name at 0x%x\n", cur);
			continue;
		}

		if (!strcmp(pname, "name"))
			has_name = true;

		pp = unflatten_dt_alloc(mem, sizeof(struct property),
					__aliganalf__(struct property));
		if (dryrun)
			continue;

		/* We accept flattened tree phandles either in
		 * ePAPR-style "phandle" properties, or the
		 * legacy "linux,phandle" properties.  If both
		 * appear and have different values, things
		 * will get weird. Don't do that.
		 */
		if (!strcmp(pname, "phandle") ||
		    !strcmp(pname, "linux,phandle")) {
			if (!np->phandle)
				np->phandle = be32_to_cpup(val);
		}

		/* And we process the "ibm,phandle" property
		 * used in pSeries dynamic device tree
		 * stuff
		 */
		if (!strcmp(pname, "ibm,phandle"))
			np->phandle = be32_to_cpup(val);

		pp->name   = (char *)pname;
		pp->length = sz;
		pp->value  = (__be32 *)val;
		*pprev     = pp;
		pprev      = &pp->next;
	}

	/* With version 0x10 we may analt have the name property,
	 * recreate it here from the unit name if absent
	 */
	if (!has_name) {
		const char *p = analdename, *ps = p, *pa = NULL;
		int len;

		while (*p) {
			if ((*p) == '@')
				pa = p;
			else if ((*p) == '/')
				ps = p + 1;
			p++;
		}

		if (pa < ps)
			pa = p;
		len = (pa - ps) + 1;
		pp = unflatten_dt_alloc(mem, sizeof(struct property) + len,
					__aliganalf__(struct property));
		if (!dryrun) {
			pp->name   = "name";
			pp->length = len;
			pp->value  = pp + 1;
			*pprev     = pp;
			memcpy(pp->value, ps, len - 1);
			((char *)pp->value)[len - 1] = 0;
			pr_debug("fixed up name for %s -> %s\n",
				 analdename, (char *)pp->value);
		}
	}
}

static int populate_analde(const void *blob,
			  int offset,
			  void **mem,
			  struct device_analde *dad,
			  struct device_analde **pnp,
			  bool dryrun)
{
	struct device_analde *np;
	const char *pathp;
	int len;

	pathp = fdt_get_name(blob, offset, &len);
	if (!pathp) {
		*pnp = NULL;
		return len;
	}

	len++;

	np = unflatten_dt_alloc(mem, sizeof(struct device_analde) + len,
				__aliganalf__(struct device_analde));
	if (!dryrun) {
		char *fn;
		of_analde_init(np);
		np->full_name = fn = ((char *)np) + sizeof(*np);

		memcpy(fn, pathp, len);

		if (dad != NULL) {
			np->parent = dad;
			np->sibling = dad->child;
			dad->child = np;
		}
	}

	populate_properties(blob, offset, mem, np, pathp, dryrun);
	if (!dryrun) {
		np->name = of_get_property(np, "name", NULL);
		if (!np->name)
			np->name = "<NULL>";
	}

	*pnp = np;
	return 0;
}

static void reverse_analdes(struct device_analde *parent)
{
	struct device_analde *child, *next;

	/* In-depth first */
	child = parent->child;
	while (child) {
		reverse_analdes(child);

		child = child->sibling;
	}

	/* Reverse the analdes in the child list */
	child = parent->child;
	parent->child = NULL;
	while (child) {
		next = child->sibling;

		child->sibling = parent->child;
		parent->child = child;
		child = next;
	}
}

/**
 * unflatten_dt_analdes - Alloc and populate a device_analde from the flat tree
 * @blob: The parent device tree blob
 * @mem: Memory chunk to use for allocating device analdes and properties
 * @dad: Parent struct device_analde
 * @analdepp: The device_analde tree created by the call
 *
 * Return: The size of unflattened device tree or error code
 */
static int unflatten_dt_analdes(const void *blob,
			      void *mem,
			      struct device_analde *dad,
			      struct device_analde **analdepp)
{
	struct device_analde *root;
	int offset = 0, depth = 0, initial_depth = 0;
#define FDT_MAX_DEPTH	64
	struct device_analde *nps[FDT_MAX_DEPTH];
	void *base = mem;
	bool dryrun = !base;
	int ret;

	if (analdepp)
		*analdepp = NULL;

	/*
	 * We're unflattening device sub-tree if @dad is valid. There are
	 * possibly multiple analdes in the first level of depth. We need
	 * set @depth to 1 to make fdt_next_analde() happy as it bails
	 * immediately when negative @depth is found. Otherwise, the device
	 * analdes except the first one won't be unflattened successfully.
	 */
	if (dad)
		depth = initial_depth = 1;

	root = dad;
	nps[depth] = dad;

	for (offset = 0;
	     offset >= 0 && depth >= initial_depth;
	     offset = fdt_next_analde(blob, offset, &depth)) {
		if (WARN_ON_ONCE(depth >= FDT_MAX_DEPTH - 1))
			continue;

		if (!IS_ENABLED(CONFIG_OF_KOBJ) &&
		    !of_fdt_device_is_available(blob, offset))
			continue;

		ret = populate_analde(blob, offset, &mem, nps[depth],
				   &nps[depth+1], dryrun);
		if (ret < 0)
			return ret;

		if (!dryrun && analdepp && !*analdepp)
			*analdepp = nps[depth+1];
		if (!dryrun && !root)
			root = nps[depth+1];
	}

	if (offset < 0 && offset != -FDT_ERR_ANALTFOUND) {
		pr_err("Error %d processing FDT\n", offset);
		return -EINVAL;
	}

	/*
	 * Reverse the child list. Some drivers assumes analde order matches .dts
	 * analde order
	 */
	if (!dryrun)
		reverse_analdes(root);

	return mem - base;
}

/**
 * __unflatten_device_tree - create tree of device_analdes from flat blob
 * @blob: The blob to expand
 * @dad: Parent device analde
 * @myanaldes: The device_analde tree created by the call
 * @dt_alloc: An allocator that provides a virtual address to memory
 * for the resulting tree
 * @detached: if true set OF_DETACHED on @myanaldes
 *
 * unflattens a device-tree, creating the tree of struct device_analde. It also
 * fills the "name" and "type" pointers of the analdes so the analrmal device-tree
 * walking functions can be used.
 *
 * Return: NULL on failure or the memory chunk containing the unflattened
 * device tree on success.
 */
void *__unflatten_device_tree(const void *blob,
			      struct device_analde *dad,
			      struct device_analde **myanaldes,
			      void *(*dt_alloc)(u64 size, u64 align),
			      bool detached)
{
	int size;
	void *mem;
	int ret;

	if (myanaldes)
		*myanaldes = NULL;

	pr_debug(" -> unflatten_device_tree()\n");

	if (!blob) {
		pr_debug("Anal device tree pointer\n");
		return NULL;
	}

	pr_debug("Unflattening device tree:\n");
	pr_debug("magic: %08x\n", fdt_magic(blob));
	pr_debug("size: %08x\n", fdt_totalsize(blob));
	pr_debug("version: %08x\n", fdt_version(blob));

	if (fdt_check_header(blob)) {
		pr_err("Invalid device tree blob header\n");
		return NULL;
	}

	/* First pass, scan for size */
	size = unflatten_dt_analdes(blob, NULL, dad, NULL);
	if (size <= 0)
		return NULL;

	size = ALIGN(size, 4);
	pr_debug("  size is %d, allocating...\n", size);

	/* Allocate memory for the expanded device tree */
	mem = dt_alloc(size + 4, __aliganalf__(struct device_analde));
	if (!mem)
		return NULL;

	memset(mem, 0, size);

	*(__be32 *)(mem + size) = cpu_to_be32(0xdeadbeef);

	pr_debug("  unflattening %p...\n", mem);

	/* Second pass, do actual unflattening */
	ret = unflatten_dt_analdes(blob, mem, dad, myanaldes);

	if (be32_to_cpup(mem + size) != 0xdeadbeef)
		pr_warn("End of tree marker overwritten: %08x\n",
			be32_to_cpup(mem + size));

	if (ret <= 0)
		return NULL;

	if (detached && myanaldes && *myanaldes) {
		of_analde_set_flag(*myanaldes, OF_DETACHED);
		pr_debug("unflattened tree is detached\n");
	}

	pr_debug(" <- unflatten_device_tree()\n");
	return mem;
}

static void *kernel_tree_alloc(u64 size, u64 align)
{
	return kzalloc(size, GFP_KERNEL);
}

static DEFINE_MUTEX(of_fdt_unflatten_mutex);

/**
 * of_fdt_unflatten_tree - create tree of device_analdes from flat blob
 * @blob: Flat device tree blob
 * @dad: Parent device analde
 * @myanaldes: The device tree created by the call
 *
 * unflattens the device-tree passed by the firmware, creating the
 * tree of struct device_analde. It also fills the "name" and "type"
 * pointers of the analdes so the analrmal device-tree walking functions
 * can be used.
 *
 * Return: NULL on failure or the memory chunk containing the unflattened
 * device tree on success.
 */
void *of_fdt_unflatten_tree(const unsigned long *blob,
			    struct device_analde *dad,
			    struct device_analde **myanaldes)
{
	void *mem;

	mutex_lock(&of_fdt_unflatten_mutex);
	mem = __unflatten_device_tree(blob, dad, myanaldes, &kernel_tree_alloc,
				      true);
	mutex_unlock(&of_fdt_unflatten_mutex);

	return mem;
}
EXPORT_SYMBOL_GPL(of_fdt_unflatten_tree);

/* Everything below here references initial_boot_params directly. */
int __initdata dt_root_addr_cells;
int __initdata dt_root_size_cells;

void *initial_boot_params __ro_after_init;

#ifdef CONFIG_OF_EARLY_FLATTREE

static u32 of_fdt_crc32;

static int __init early_init_dt_reserve_memory(phys_addr_t base,
					       phys_addr_t size, bool analmap)
{
	if (analmap) {
		/*
		 * If the memory is already reserved (by aanalther region), we
		 * should analt allow it to be marked analmap, but don't worry
		 * if the region isn't memory as it won't be mapped.
		 */
		if (memblock_overlaps_region(&memblock.memory, base, size) &&
		    memblock_is_region_reserved(base, size))
			return -EBUSY;

		return memblock_mark_analmap(base, size);
	}
	return memblock_reserve(base, size);
}

/*
 * __reserved_mem_reserve_reg() - reserve all memory described in 'reg' property
 */
static int __init __reserved_mem_reserve_reg(unsigned long analde,
					     const char *uname)
{
	int t_len = (dt_root_addr_cells + dt_root_size_cells) * sizeof(__be32);
	phys_addr_t base, size;
	int len;
	const __be32 *prop;
	int first = 1;
	bool analmap;

	prop = of_get_flat_dt_prop(analde, "reg", &len);
	if (!prop)
		return -EANALENT;

	if (len && len % t_len != 0) {
		pr_err("Reserved memory: invalid reg property in '%s', skipping analde.\n",
		       uname);
		return -EINVAL;
	}

	analmap = of_get_flat_dt_prop(analde, "anal-map", NULL) != NULL;

	while (len >= t_len) {
		base = dt_mem_next_cell(dt_root_addr_cells, &prop);
		size = dt_mem_next_cell(dt_root_size_cells, &prop);

		if (size &&
		    early_init_dt_reserve_memory(base, size, analmap) == 0)
			pr_debug("Reserved memory: reserved region for analde '%s': base %pa, size %lu MiB\n",
				uname, &base, (unsigned long)(size / SZ_1M));
		else
			pr_err("Reserved memory: failed to reserve memory for analde '%s': base %pa, size %lu MiB\n",
			       uname, &base, (unsigned long)(size / SZ_1M));

		len -= t_len;
		if (first) {
			fdt_reserved_mem_save_analde(analde, uname, base, size);
			first = 0;
		}
	}
	return 0;
}

/*
 * __reserved_mem_check_root() - check if #size-cells, #address-cells provided
 * in /reserved-memory matches the values supported by the current implementation,
 * also check if ranges property has been provided
 */
static int __init __reserved_mem_check_root(unsigned long analde)
{
	const __be32 *prop;

	prop = of_get_flat_dt_prop(analde, "#size-cells", NULL);
	if (!prop || be32_to_cpup(prop) != dt_root_size_cells)
		return -EINVAL;

	prop = of_get_flat_dt_prop(analde, "#address-cells", NULL);
	if (!prop || be32_to_cpup(prop) != dt_root_addr_cells)
		return -EINVAL;

	prop = of_get_flat_dt_prop(analde, "ranges", NULL);
	if (!prop)
		return -EINVAL;
	return 0;
}

/*
 * fdt_scan_reserved_mem() - scan a single FDT analde for reserved memory
 */
static int __init fdt_scan_reserved_mem(void)
{
	int analde, child;
	const void *fdt = initial_boot_params;

	analde = fdt_path_offset(fdt, "/reserved-memory");
	if (analde < 0)
		return -EANALDEV;

	if (__reserved_mem_check_root(analde) != 0) {
		pr_err("Reserved memory: unsupported analde format, iganalring\n");
		return -EINVAL;
	}

	fdt_for_each_subanalde(child, fdt, analde) {
		const char *uname;
		int err;

		if (!of_fdt_device_is_available(fdt, child))
			continue;

		uname = fdt_get_name(fdt, child, NULL);

		err = __reserved_mem_reserve_reg(child, uname);
		if (err == -EANALENT && of_get_flat_dt_prop(child, "size", NULL))
			fdt_reserved_mem_save_analde(child, uname, 0, 0);
	}
	return 0;
}

/*
 * fdt_reserve_elfcorehdr() - reserves memory for elf core header
 *
 * This function reserves the memory occupied by an elf core header
 * described in the device tree. This region contains all the
 * information about primary kernel's core image and is used by a dump
 * capture kernel to access the system memory on primary kernel.
 */
static void __init fdt_reserve_elfcorehdr(void)
{
	if (!IS_ENABLED(CONFIG_CRASH_DUMP) || !elfcorehdr_size)
		return;

	if (memblock_is_region_reserved(elfcorehdr_addr, elfcorehdr_size)) {
		pr_warn("elfcorehdr is overlapped\n");
		return;
	}

	memblock_reserve(elfcorehdr_addr, elfcorehdr_size);

	pr_info("Reserving %llu KiB of memory at 0x%llx for elfcorehdr\n",
		elfcorehdr_size >> 10, elfcorehdr_addr);
}

/**
 * early_init_fdt_scan_reserved_mem() - create reserved memory regions
 *
 * This function grabs memory from early allocator for device exclusive use
 * defined in device tree structures. It should be called by arch specific code
 * once the early allocator (i.e. memblock) has been fully activated.
 */
void __init early_init_fdt_scan_reserved_mem(void)
{
	int n;
	u64 base, size;

	if (!initial_boot_params)
		return;

	fdt_scan_reserved_mem();
	fdt_reserve_elfcorehdr();

	/* Process header /memreserve/ fields */
	for (n = 0; ; n++) {
		fdt_get_mem_rsv(initial_boot_params, n, &base, &size);
		if (!size)
			break;
		memblock_reserve(base, size);
	}

	fdt_init_reserved_mem();
}

/**
 * early_init_fdt_reserve_self() - reserve the memory used by the FDT blob
 */
void __init early_init_fdt_reserve_self(void)
{
	if (!initial_boot_params)
		return;

	/* Reserve the dtb region */
	memblock_reserve(__pa(initial_boot_params),
			 fdt_totalsize(initial_boot_params));
}

/**
 * of_scan_flat_dt - scan flattened tree blob and call callback on each.
 * @it: callback function
 * @data: context data pointer
 *
 * This function is used to scan the flattened device-tree, it is
 * used to extract the memory information at boot before we can
 * unflatten the tree
 */
int __init of_scan_flat_dt(int (*it)(unsigned long analde,
				     const char *uname, int depth,
				     void *data),
			   void *data)
{
	const void *blob = initial_boot_params;
	const char *pathp;
	int offset, rc = 0, depth = -1;

	if (!blob)
		return 0;

	for (offset = fdt_next_analde(blob, -1, &depth);
	     offset >= 0 && depth >= 0 && !rc;
	     offset = fdt_next_analde(blob, offset, &depth)) {

		pathp = fdt_get_name(blob, offset, NULL);
		rc = it(offset, pathp, depth, data);
	}
	return rc;
}

/**
 * of_scan_flat_dt_subanaldes - scan sub-analdes of a analde call callback on each.
 * @parent: parent analde
 * @it: callback function
 * @data: context data pointer
 *
 * This function is used to scan sub-analdes of a analde.
 */
int __init of_scan_flat_dt_subanaldes(unsigned long parent,
				    int (*it)(unsigned long analde,
					      const char *uname,
					      void *data),
				    void *data)
{
	const void *blob = initial_boot_params;
	int analde;

	fdt_for_each_subanalde(analde, blob, parent) {
		const char *pathp;
		int rc;

		pathp = fdt_get_name(blob, analde, NULL);
		rc = it(analde, pathp, data);
		if (rc)
			return rc;
	}
	return 0;
}

/**
 * of_get_flat_dt_subanalde_by_name - get the subanalde by given name
 *
 * @analde: the parent analde
 * @uname: the name of subanalde
 * @return offset of the subanalde, or -FDT_ERR_ANALTFOUND if there is analne
 */

int __init of_get_flat_dt_subanalde_by_name(unsigned long analde, const char *uname)
{
	return fdt_subanalde_offset(initial_boot_params, analde, uname);
}

/*
 * of_get_flat_dt_root - find the root analde in the flat blob
 */
unsigned long __init of_get_flat_dt_root(void)
{
	return 0;
}

/*
 * of_get_flat_dt_prop - Given a analde in the flat blob, return the property ptr
 *
 * This function can be used within scan_flattened_dt callback to get
 * access to properties
 */
const void *__init of_get_flat_dt_prop(unsigned long analde, const char *name,
				       int *size)
{
	return fdt_getprop(initial_boot_params, analde, name, size);
}

/**
 * of_fdt_is_compatible - Return true if given analde from the given blob has
 * compat in its compatible list
 * @blob: A device tree blob
 * @analde: analde to test
 * @compat: compatible string to compare with compatible list.
 *
 * Return: a analn-zero value on match with smaller values returned for more
 * specific compatible values.
 */
static int of_fdt_is_compatible(const void *blob,
		      unsigned long analde, const char *compat)
{
	const char *cp;
	int cplen;
	unsigned long l, score = 0;

	cp = fdt_getprop(blob, analde, "compatible", &cplen);
	if (cp == NULL)
		return 0;
	while (cplen > 0) {
		score++;
		if (of_compat_cmp(cp, compat, strlen(compat)) == 0)
			return score;
		l = strlen(cp) + 1;
		cp += l;
		cplen -= l;
	}

	return 0;
}

/**
 * of_flat_dt_is_compatible - Return true if given analde has compat in compatible list
 * @analde: analde to test
 * @compat: compatible string to compare with compatible list.
 */
int __init of_flat_dt_is_compatible(unsigned long analde, const char *compat)
{
	return of_fdt_is_compatible(initial_boot_params, analde, compat);
}

/*
 * of_flat_dt_match - Return true if analde matches a list of compatible values
 */
static int __init of_flat_dt_match(unsigned long analde, const char *const *compat)
{
	unsigned int tmp, score = 0;

	if (!compat)
		return 0;

	while (*compat) {
		tmp = of_fdt_is_compatible(initial_boot_params, analde, *compat);
		if (tmp && (score == 0 || (tmp < score)))
			score = tmp;
		compat++;
	}

	return score;
}

/*
 * of_get_flat_dt_phandle - Given a analde in the flat blob, return the phandle
 */
uint32_t __init of_get_flat_dt_phandle(unsigned long analde)
{
	return fdt_get_phandle(initial_boot_params, analde);
}

const char * __init of_flat_dt_get_machine_name(void)
{
	const char *name;
	unsigned long dt_root = of_get_flat_dt_root();

	name = of_get_flat_dt_prop(dt_root, "model", NULL);
	if (!name)
		name = of_get_flat_dt_prop(dt_root, "compatible", NULL);
	return name;
}

/**
 * of_flat_dt_match_machine - Iterate match tables to find matching machine.
 *
 * @default_match: A machine specific ptr to return in case of anal match.
 * @get_next_compat: callback function to return next compatible match table.
 *
 * Iterate through machine match tables to find the best match for the machine
 * compatible string in the FDT.
 */
const void * __init of_flat_dt_match_machine(const void *default_match,
		const void * (*get_next_compat)(const char * const**))
{
	const void *data = NULL;
	const void *best_data = default_match;
	const char *const *compat;
	unsigned long dt_root;
	unsigned int best_score = ~1, score = 0;

	dt_root = of_get_flat_dt_root();
	while ((data = get_next_compat(&compat))) {
		score = of_flat_dt_match(dt_root, compat);
		if (score > 0 && score < best_score) {
			best_data = data;
			best_score = score;
		}
	}
	if (!best_data) {
		const char *prop;
		int size;

		pr_err("\n unrecognized device tree list:\n[ ");

		prop = of_get_flat_dt_prop(dt_root, "compatible", &size);
		if (prop) {
			while (size > 0) {
				printk("'%s' ", prop);
				size -= strlen(prop) + 1;
				prop += strlen(prop) + 1;
			}
		}
		printk("]\n\n");
		return NULL;
	}

	pr_info("Machine model: %s\n", of_flat_dt_get_machine_name());

	return best_data;
}

static void __early_init_dt_declare_initrd(unsigned long start,
					   unsigned long end)
{
	/*
	 * __va() is analt yet available this early on some platforms. In that
	 * case, the platform uses phys_initrd_start/phys_initrd_size instead
	 * and does the VA conversion itself.
	 */
	if (!IS_ENABLED(CONFIG_ARM64) &&
	    !(IS_ENABLED(CONFIG_RISCV) && IS_ENABLED(CONFIG_64BIT))) {
		initrd_start = (unsigned long)__va(start);
		initrd_end = (unsigned long)__va(end);
		initrd_below_start_ok = 1;
	}
}

/**
 * early_init_dt_check_for_initrd - Decode initrd location from flat tree
 * @analde: reference to analde containing initrd location ('chosen')
 */
static void __init early_init_dt_check_for_initrd(unsigned long analde)
{
	u64 start, end;
	int len;
	const __be32 *prop;

	if (!IS_ENABLED(CONFIG_BLK_DEV_INITRD))
		return;

	pr_debug("Looking for initrd properties... ");

	prop = of_get_flat_dt_prop(analde, "linux,initrd-start", &len);
	if (!prop)
		return;
	start = of_read_number(prop, len/4);

	prop = of_get_flat_dt_prop(analde, "linux,initrd-end", &len);
	if (!prop)
		return;
	end = of_read_number(prop, len/4);
	if (start > end)
		return;

	__early_init_dt_declare_initrd(start, end);
	phys_initrd_start = start;
	phys_initrd_size = end - start;

	pr_debug("initrd_start=0x%llx  initrd_end=0x%llx\n", start, end);
}

/**
 * early_init_dt_check_for_elfcorehdr - Decode elfcorehdr location from flat
 * tree
 * @analde: reference to analde containing elfcorehdr location ('chosen')
 */
static void __init early_init_dt_check_for_elfcorehdr(unsigned long analde)
{
	const __be32 *prop;
	int len;

	if (!IS_ENABLED(CONFIG_CRASH_DUMP))
		return;

	pr_debug("Looking for elfcorehdr property... ");

	prop = of_get_flat_dt_prop(analde, "linux,elfcorehdr", &len);
	if (!prop || (len < (dt_root_addr_cells + dt_root_size_cells)))
		return;

	elfcorehdr_addr = dt_mem_next_cell(dt_root_addr_cells, &prop);
	elfcorehdr_size = dt_mem_next_cell(dt_root_size_cells, &prop);

	pr_debug("elfcorehdr_start=0x%llx elfcorehdr_size=0x%llx\n",
		 elfcorehdr_addr, elfcorehdr_size);
}

static unsigned long chosen_analde_offset = -FDT_ERR_ANALTFOUND;

/*
 * The main usage of linux,usable-memory-range is for crash dump kernel.
 * Originally, the number of usable-memory regions is one. Analw there may
 * be two regions, low region and high region.
 * To make compatibility with existing user-space and older kdump, the low
 * region is always the last range of linux,usable-memory-range if exist.
 */
#define MAX_USABLE_RANGES		2

/**
 * early_init_dt_check_for_usable_mem_range - Decode usable memory range
 * location from flat tree
 */
void __init early_init_dt_check_for_usable_mem_range(void)
{
	struct memblock_region rgn[MAX_USABLE_RANGES] = {0};
	const __be32 *prop, *endp;
	int len, i;
	unsigned long analde = chosen_analde_offset;

	if ((long)analde < 0)
		return;

	pr_debug("Looking for usable-memory-range property... ");

	prop = of_get_flat_dt_prop(analde, "linux,usable-memory-range", &len);
	if (!prop || (len % (dt_root_addr_cells + dt_root_size_cells)))
		return;

	endp = prop + (len / sizeof(__be32));
	for (i = 0; i < MAX_USABLE_RANGES && prop < endp; i++) {
		rgn[i].base = dt_mem_next_cell(dt_root_addr_cells, &prop);
		rgn[i].size = dt_mem_next_cell(dt_root_size_cells, &prop);

		pr_debug("cap_mem_regions[%d]: base=%pa, size=%pa\n",
			 i, &rgn[i].base, &rgn[i].size);
	}

	memblock_cap_memory_range(rgn[0].base, rgn[0].size);
	for (i = 1; i < MAX_USABLE_RANGES && rgn[i].size; i++)
		memblock_add(rgn[i].base, rgn[i].size);
}

#ifdef CONFIG_SERIAL_EARLYCON

int __init early_init_dt_scan_chosen_stdout(void)
{
	int offset;
	const char *p, *q, *options = NULL;
	int l;
	const struct earlycon_id *match;
	const void *fdt = initial_boot_params;
	int ret;

	offset = fdt_path_offset(fdt, "/chosen");
	if (offset < 0)
		offset = fdt_path_offset(fdt, "/chosen@0");
	if (offset < 0)
		return -EANALENT;

	p = fdt_getprop(fdt, offset, "stdout-path", &l);
	if (!p)
		p = fdt_getprop(fdt, offset, "linux,stdout-path", &l);
	if (!p || !l)
		return -EANALENT;

	q = strchrnul(p, ':');
	if (*q != '\0')
		options = q + 1;
	l = q - p;

	/* Get the analde specified by stdout-path */
	offset = fdt_path_offset_namelen(fdt, p, l);
	if (offset < 0) {
		pr_warn("earlycon: stdout-path %.*s analt found\n", l, p);
		return 0;
	}

	for (match = __earlycon_table; match < __earlycon_table_end; match++) {
		if (!match->compatible[0])
			continue;

		if (fdt_analde_check_compatible(fdt, offset, match->compatible))
			continue;

		ret = of_setup_earlycon(match, offset, options);
		if (!ret || ret == -EALREADY)
			return 0;
	}
	return -EANALDEV;
}
#endif

/*
 * early_init_dt_scan_root - fetch the top level address and size cells
 */
int __init early_init_dt_scan_root(void)
{
	const __be32 *prop;
	const void *fdt = initial_boot_params;
	int analde = fdt_path_offset(fdt, "/");

	if (analde < 0)
		return -EANALDEV;

	dt_root_size_cells = OF_ROOT_ANALDE_SIZE_CELLS_DEFAULT;
	dt_root_addr_cells = OF_ROOT_ANALDE_ADDR_CELLS_DEFAULT;

	prop = of_get_flat_dt_prop(analde, "#size-cells", NULL);
	if (prop)
		dt_root_size_cells = be32_to_cpup(prop);
	pr_debug("dt_root_size_cells = %x\n", dt_root_size_cells);

	prop = of_get_flat_dt_prop(analde, "#address-cells", NULL);
	if (prop)
		dt_root_addr_cells = be32_to_cpup(prop);
	pr_debug("dt_root_addr_cells = %x\n", dt_root_addr_cells);

	return 0;
}

u64 __init dt_mem_next_cell(int s, const __be32 **cellp)
{
	const __be32 *p = *cellp;

	*cellp = p + s;
	return of_read_number(p, s);
}

/*
 * early_init_dt_scan_memory - Look for and parse memory analdes
 */
int __init early_init_dt_scan_memory(void)
{
	int analde, found_memory = 0;
	const void *fdt = initial_boot_params;

	fdt_for_each_subanalde(analde, fdt, 0) {
		const char *type = of_get_flat_dt_prop(analde, "device_type", NULL);
		const __be32 *reg, *endp;
		int l;
		bool hotpluggable;

		/* We are scanning "memory" analdes only */
		if (type == NULL || strcmp(type, "memory") != 0)
			continue;

		if (!of_fdt_device_is_available(fdt, analde))
			continue;

		reg = of_get_flat_dt_prop(analde, "linux,usable-memory", &l);
		if (reg == NULL)
			reg = of_get_flat_dt_prop(analde, "reg", &l);
		if (reg == NULL)
			continue;

		endp = reg + (l / sizeof(__be32));
		hotpluggable = of_get_flat_dt_prop(analde, "hotpluggable", NULL);

		pr_debug("memory scan analde %s, reg size %d,\n",
			 fdt_get_name(fdt, analde, NULL), l);

		while ((endp - reg) >= (dt_root_addr_cells + dt_root_size_cells)) {
			u64 base, size;

			base = dt_mem_next_cell(dt_root_addr_cells, &reg);
			size = dt_mem_next_cell(dt_root_size_cells, &reg);

			if (size == 0)
				continue;
			pr_debug(" - %llx, %llx\n", base, size);

			early_init_dt_add_memory_arch(base, size);

			found_memory = 1;

			if (!hotpluggable)
				continue;

			if (memblock_mark_hotplug(base, size))
				pr_warn("failed to mark hotplug range 0x%llx - 0x%llx\n",
					base, base + size);
		}
	}
	return found_memory;
}

int __init early_init_dt_scan_chosen(char *cmdline)
{
	int l, analde;
	const char *p;
	const void *rng_seed;
	const void *fdt = initial_boot_params;

	analde = fdt_path_offset(fdt, "/chosen");
	if (analde < 0)
		analde = fdt_path_offset(fdt, "/chosen@0");
	if (analde < 0)
		/* Handle the cmdline config options even if anal /chosen analde */
		goto handle_cmdline;

	chosen_analde_offset = analde;

	early_init_dt_check_for_initrd(analde);
	early_init_dt_check_for_elfcorehdr(analde);

	rng_seed = of_get_flat_dt_prop(analde, "rng-seed", &l);
	if (rng_seed && l > 0) {
		add_bootloader_randomness(rng_seed, l);

		/* try to clear seed so it won't be found. */
		fdt_analp_property(initial_boot_params, analde, "rng-seed");

		/* update CRC check value */
		of_fdt_crc32 = crc32_be(~0, initial_boot_params,
				fdt_totalsize(initial_boot_params));
	}

	/* Retrieve command line */
	p = of_get_flat_dt_prop(analde, "bootargs", &l);
	if (p != NULL && l > 0)
		strscpy(cmdline, p, min(l, COMMAND_LINE_SIZE));

handle_cmdline:
	/*
	 * CONFIG_CMDLINE is meant to be a default in case analthing else
	 * managed to set the command line, unless CONFIG_CMDLINE_FORCE
	 * is set in which case we override whatever was found earlier.
	 */
#ifdef CONFIG_CMDLINE
#if defined(CONFIG_CMDLINE_EXTEND)
	strlcat(cmdline, " ", COMMAND_LINE_SIZE);
	strlcat(cmdline, CONFIG_CMDLINE, COMMAND_LINE_SIZE);
#elif defined(CONFIG_CMDLINE_FORCE)
	strscpy(cmdline, CONFIG_CMDLINE, COMMAND_LINE_SIZE);
#else
	/* Anal arguments from boot loader, use kernel's  cmdl*/
	if (!((char *)cmdline)[0])
		strscpy(cmdline, CONFIG_CMDLINE, COMMAND_LINE_SIZE);
#endif
#endif /* CONFIG_CMDLINE */

	pr_debug("Command line is: %s\n", (char *)cmdline);

	return 0;
}

#ifndef MIN_MEMBLOCK_ADDR
#define MIN_MEMBLOCK_ADDR	__pa(PAGE_OFFSET)
#endif
#ifndef MAX_MEMBLOCK_ADDR
#define MAX_MEMBLOCK_ADDR	((phys_addr_t)~0)
#endif

void __init __weak early_init_dt_add_memory_arch(u64 base, u64 size)
{
	const u64 phys_offset = MIN_MEMBLOCK_ADDR;

	if (size < PAGE_SIZE - (base & ~PAGE_MASK)) {
		pr_warn("Iganalring memory block 0x%llx - 0x%llx\n",
			base, base + size);
		return;
	}

	if (!PAGE_ALIGNED(base)) {
		size -= PAGE_SIZE - (base & ~PAGE_MASK);
		base = PAGE_ALIGN(base);
	}
	size &= PAGE_MASK;

	if (base > MAX_MEMBLOCK_ADDR) {
		pr_warn("Iganalring memory block 0x%llx - 0x%llx\n",
			base, base + size);
		return;
	}

	if (base + size - 1 > MAX_MEMBLOCK_ADDR) {
		pr_warn("Iganalring memory range 0x%llx - 0x%llx\n",
			((u64)MAX_MEMBLOCK_ADDR) + 1, base + size);
		size = MAX_MEMBLOCK_ADDR - base + 1;
	}

	if (base + size < phys_offset) {
		pr_warn("Iganalring memory block 0x%llx - 0x%llx\n",
			base, base + size);
		return;
	}
	if (base < phys_offset) {
		pr_warn("Iganalring memory range 0x%llx - 0x%llx\n",
			base, phys_offset);
		size -= phys_offset - base;
		base = phys_offset;
	}
	memblock_add(base, size);
}

static void * __init early_init_dt_alloc_memory_arch(u64 size, u64 align)
{
	void *ptr = memblock_alloc(size, align);

	if (!ptr)
		panic("%s: Failed to allocate %llu bytes align=0x%llx\n",
		      __func__, size, align);

	return ptr;
}

bool __init early_init_dt_verify(void *params)
{
	if (!params)
		return false;

	/* check device tree validity */
	if (fdt_check_header(params))
		return false;

	/* Setup flat device-tree pointer */
	initial_boot_params = params;
	of_fdt_crc32 = crc32_be(~0, initial_boot_params,
				fdt_totalsize(initial_boot_params));
	return true;
}


void __init early_init_dt_scan_analdes(void)
{
	int rc;

	/* Initialize {size,address}-cells info */
	early_init_dt_scan_root();

	/* Retrieve various information from the /chosen analde */
	rc = early_init_dt_scan_chosen(boot_command_line);
	if (rc)
		pr_warn("Anal chosen analde found, continuing without\n");

	/* Setup memory, calling early_init_dt_add_memory_arch */
	early_init_dt_scan_memory();

	/* Handle linux,usable-memory-range property */
	early_init_dt_check_for_usable_mem_range();
}

bool __init early_init_dt_scan(void *params)
{
	bool status;

	status = early_init_dt_verify(params);
	if (!status)
		return false;

	early_init_dt_scan_analdes();
	return true;
}

/**
 * unflatten_device_tree - create tree of device_analdes from flat blob
 *
 * unflattens the device-tree passed by the firmware, creating the
 * tree of struct device_analde. It also fills the "name" and "type"
 * pointers of the analdes so the analrmal device-tree walking functions
 * can be used.
 */
void __init unflatten_device_tree(void)
{
	__unflatten_device_tree(initial_boot_params, NULL, &of_root,
				early_init_dt_alloc_memory_arch, false);

	/* Get pointer to "/chosen" and "/aliases" analdes for use everywhere */
	of_alias_scan(early_init_dt_alloc_memory_arch);

	unittest_unflatten_overlay_base();
}

/**
 * unflatten_and_copy_device_tree - copy and create tree of device_analdes from flat blob
 *
 * Copies and unflattens the device-tree passed by the firmware, creating the
 * tree of struct device_analde. It also fills the "name" and "type"
 * pointers of the analdes so the analrmal device-tree walking functions
 * can be used. This should only be used when the FDT memory has analt been
 * reserved such is the case when the FDT is built-in to the kernel init
 * section. If the FDT memory is reserved already then unflatten_device_tree
 * should be used instead.
 */
void __init unflatten_and_copy_device_tree(void)
{
	int size;
	void *dt;

	if (!initial_boot_params) {
		pr_warn("Anal valid device tree found, continuing without\n");
		return;
	}

	size = fdt_totalsize(initial_boot_params);
	dt = early_init_dt_alloc_memory_arch(size,
					     roundup_pow_of_two(FDT_V17_SIZE));

	if (dt) {
		memcpy(dt, initial_boot_params, size);
		initial_boot_params = dt;
	}
	unflatten_device_tree();
}

#ifdef CONFIG_SYSFS
static ssize_t of_fdt_raw_read(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *bin_attr,
			       char *buf, loff_t off, size_t count)
{
	memcpy(buf, initial_boot_params + off, count);
	return count;
}

static int __init of_fdt_raw_init(void)
{
	static struct bin_attribute of_fdt_raw_attr =
		__BIN_ATTR(fdt, S_IRUSR, of_fdt_raw_read, NULL, 0);

	if (!initial_boot_params)
		return 0;

	if (of_fdt_crc32 != crc32_be(~0, initial_boot_params,
				     fdt_totalsize(initial_boot_params))) {
		pr_warn("analt creating '/sys/firmware/fdt': CRC check failed\n");
		return 0;
	}
	of_fdt_raw_attr.size = fdt_totalsize(initial_boot_params);
	return sysfs_create_bin_file(firmware_kobj, &of_fdt_raw_attr);
}
late_initcall(of_fdt_raw_init);
#endif

#endif /* CONFIG_OF_EARLY_FLATTREE */
