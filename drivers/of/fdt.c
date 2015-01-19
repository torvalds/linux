/*
 * Functions for working with the Flattened Device Tree data format
 *
 * Copyright 2009 Benjamin Herrenschmidt, IBM Corp
 * benh@kernel.crashing.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/initrd.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <asm/setup.h>  /* for COMMAND_LINE_SIZE */
#ifdef CONFIG_PPC
#include <asm/machdep.h>
#endif /* CONFIG_PPC */

#include <asm/page.h>
#if defined(CONFIG_PLAT_MESON)
#include <mach/cpu.h>
#endif
#ifdef CONFIG_MESON_TRUSTZONE
#include <mach/meson-secure.h>
#endif

char *of_fdt_get_string(struct boot_param_header *blob, u32 offset)
{
	return ((char *)blob) +
		be32_to_cpu(blob->off_dt_strings) + offset;
}

/**
 * of_fdt_get_property - Given a node in the given flat blob, return
 * the property ptr
 */
void *of_fdt_get_property(struct boot_param_header *blob,
		       unsigned long node, const char *name,
		       unsigned long *size)
{
	unsigned long p = node;

	do {
		u32 tag = be32_to_cpup((__be32 *)p);
		u32 sz, noff;
		const char *nstr;

		p += 4;
		if (tag == OF_DT_NOP)
			continue;
		if (tag != OF_DT_PROP)
			return NULL;

		sz = be32_to_cpup((__be32 *)p);
		noff = be32_to_cpup((__be32 *)(p + 4));
		p += 8;
		if (be32_to_cpu(blob->version) < 0x10)
			p = ALIGN(p, sz >= 8 ? 8 : 4);

		nstr = of_fdt_get_string(blob, noff);
		if (nstr == NULL) {
			pr_warning("Can't find property index name !\n");
			return NULL;
		}
		if (strcmp(name, nstr) == 0) {
			if (size)
				*size = sz;
			return (void *)p;
		}
		p += sz;
		p = ALIGN(p, 4);
	} while (1);
}

/**
 * of_fdt_is_compatible - Return true if given node from the given blob has
 * compat in its compatible list
 * @blob: A device tree blob
 * @node: node to test
 * @compat: compatible string to compare with compatible list.
 *
 * On match, returns a non-zero value with smaller values returned for more
 * specific compatible values.
 */
int of_fdt_is_compatible(struct boot_param_header *blob,
		      unsigned long node, const char *compat)
{
	const char *cp;
	unsigned long cplen, l, score = 0;

	cp = of_fdt_get_property(blob, node, "compatible", &cplen);
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
 * of_fdt_match - Return true if node matches a list of compatible values
 */
int of_fdt_match(struct boot_param_header *blob, unsigned long node,
                 const char *const *compat)
{
	unsigned int tmp, score = 0;

	if (!compat)
		return 0;

	while (*compat) {
		tmp = of_fdt_is_compatible(blob, node, *compat);
		if (tmp && (score == 0 || (tmp < score)))
			score = tmp;
		compat++;
	}

	return score;
}

static void *unflatten_dt_alloc(unsigned long *mem, unsigned long size,
				       unsigned long align)
{
	void *res;

	*mem = ALIGN(*mem, align);
	res = (void *)*mem;
	*mem += size;

	return res;
}

/**
 * unflatten_dt_node - Alloc and populate a device_node from the flat tree
 * @blob: The parent device tree blob
 * @mem: Memory chunk to use for allocating device nodes and properties
 * @p: pointer to node in flat tree
 * @dad: Parent struct device_node
 * @allnextpp: pointer to ->allnext from last allocated device_node
 * @fpsize: Size of the node path up at the current depth.
 */
static unsigned long unflatten_dt_node(struct boot_param_header *blob,
				unsigned long mem,
				unsigned long *p,
				struct device_node *dad,
				struct device_node ***allnextpp,
				unsigned long fpsize)
{
	struct device_node *np;
	struct property *pp, **prev_pp = NULL;
	char *pathp;
	u32 tag;
	unsigned int l, allocl;
	int has_name = 0;
	int new_format = 0;

	tag = be32_to_cpup((__be32 *)(*p));
	if (tag != OF_DT_BEGIN_NODE) {
		pr_err("Weird tag at start of node: %x\n", tag);
		return mem;
	}
	*p += 4;
	pathp = (char *)*p;
	l = allocl = strlen(pathp) + 1;
	*p = ALIGN(*p + l, 4);

	/* version 0x10 has a more compact unit name here instead of the full
	 * path. we accumulate the full path size using "fpsize", we'll rebuild
	 * it later. We detect this because the first character of the name is
	 * not '/'.
	 */
	if ((*pathp) != '/') {
		new_format = 1;
		if (fpsize == 0) {
			/* root node: special case. fpsize accounts for path
			 * plus terminating zero. root node only has '/', so
			 * fpsize should be 2, but we want to avoid the first
			 * level nodes to have two '/' so we use fpsize 1 here
			 */
			fpsize = 1;
			allocl = 2;
			l = 1;
			*pathp = '\0';
		} else {
			/* account for '/' and path size minus terminal 0
			 * already in 'l'
			 */
			fpsize += l;
			allocl = fpsize;
		}
	}

	np = unflatten_dt_alloc(&mem, sizeof(struct device_node) + allocl,
				__alignof__(struct device_node));
	if (allnextpp) {
		char *fn;
		memset(np, 0, sizeof(*np));
		np->full_name = fn = ((char *)np) + sizeof(*np);
		if (new_format) {
			/* rebuild full path for new format */
			if (dad && dad->parent) {
				strcpy(fn, dad->full_name);
#ifdef DEBUG
				if ((strlen(fn) + l + 1) != allocl) {
					pr_debug("%s: p: %d, l: %d, a: %d\n",
						pathp, (int)strlen(fn),
						l, allocl);
				}
#endif
				fn += strlen(fn);
			}
			*(fn++) = '/';
		}
		memcpy(fn, pathp, l);

		prev_pp = &np->properties;
		**allnextpp = np;
		*allnextpp = &np->allnext;
		if (dad != NULL) {
			np->parent = dad;
			/* we temporarily use the next field as `last_child'*/
			if (dad->next == NULL)
				dad->child = np;
			else
				dad->next->sibling = np;
			dad->next = np;
		}
		kref_init(&np->kref);
	}
	/* process properties */
	while (1) {
		u32 sz, noff;
		char *pname;

		tag = be32_to_cpup((__be32 *)(*p));
		if (tag == OF_DT_NOP) {
			*p += 4;
			continue;
		}
		if (tag != OF_DT_PROP)
			break;
		*p += 4;
		sz = be32_to_cpup((__be32 *)(*p));
		noff = be32_to_cpup((__be32 *)((*p) + 4));
		*p += 8;
		if (be32_to_cpu(blob->version) < 0x10)
			*p = ALIGN(*p, sz >= 8 ? 8 : 4);

		pname = of_fdt_get_string(blob, noff);
		if (pname == NULL) {
			pr_info("Can't find property name in list !\n");
			break;
		}
		if (strcmp(pname, "name") == 0)
			has_name = 1;
		l = strlen(pname) + 1;
		pp = unflatten_dt_alloc(&mem, sizeof(struct property),
					__alignof__(struct property));
		if (allnextpp) {
			/* We accept flattened tree phandles either in
			 * ePAPR-style "phandle" properties, or the
			 * legacy "linux,phandle" properties.  If both
			 * appear and have different values, things
			 * will get weird.  Don't do that. */
			if ((strcmp(pname, "phandle") == 0) ||
			    (strcmp(pname, "linux,phandle") == 0)) {
				if (np->phandle == 0)
					np->phandle = be32_to_cpup((__be32*)*p);
			}
			/* And we process the "ibm,phandle" property
			 * used in pSeries dynamic device tree
			 * stuff */
			if (strcmp(pname, "ibm,phandle") == 0)
				np->phandle = be32_to_cpup((__be32 *)*p);
			pp->name = pname;
			pp->length = sz;
			pp->value = (void *)*p;
			*prev_pp = pp;
			prev_pp = &pp->next;
		}
		*p = ALIGN((*p) + sz, 4);
	}
	/* with version 0x10 we may not have the name property, recreate
	 * it here from the unit name if absent
	 */
	if (!has_name) {
		char *p1 = pathp, *ps = pathp, *pa = NULL;
		int sz;

		while (*p1) {
			if ((*p1) == '@')
				pa = p1;
			if ((*p1) == '/')
				ps = p1 + 1;
			p1++;
		}
		if (pa < ps)
			pa = p1;
		sz = (pa - ps) + 1;
		pp = unflatten_dt_alloc(&mem, sizeof(struct property) + sz,
					__alignof__(struct property));
		if (allnextpp) {
			pp->name = "name";
			pp->length = sz;
			pp->value = pp + 1;
			*prev_pp = pp;
			prev_pp = &pp->next;
			memcpy(pp->value, ps, sz - 1);
			((char *)pp->value)[sz - 1] = 0;
			pr_debug("fixed up name for %s -> %s\n", pathp,
				(char *)pp->value);
		}
	}
	if (allnextpp) {
		*prev_pp = NULL;
		np->name = of_get_property(np, "name", NULL);
		np->type = of_get_property(np, "device_type", NULL);

		if (!np->name)
			np->name = "<NULL>";
		if (!np->type)
			np->type = "<NULL>";
	}
	while (tag == OF_DT_BEGIN_NODE || tag == OF_DT_NOP) {
		if (tag == OF_DT_NOP)
			*p += 4;
		else
			mem = unflatten_dt_node(blob, mem, p, np, allnextpp,
						fpsize);
		tag = be32_to_cpup((__be32 *)(*p));
	}
	if (tag != OF_DT_END_NODE) {
		pr_err("Weird tag at end of node: %x\n", tag);
		return mem;
	}
	*p += 4;
	return mem;
}

/**
 * __unflatten_device_tree - create tree of device_nodes from flat blob
 *
 * unflattens a device-tree, creating the
 * tree of struct device_node. It also fills the "name" and "type"
 * pointers of the nodes so the normal device-tree walking functions
 * can be used.
 * @blob: The blob to expand
 * @mynodes: The device_node tree created by the call
 * @dt_alloc: An allocator that provides a virtual address to memory
 * for the resulting tree
 */
static void __unflatten_device_tree(struct boot_param_header *blob,
			     struct device_node **mynodes,
			     void * (*dt_alloc)(u64 size, u64 align))
{
	unsigned long start, mem, size;
	struct device_node **allnextp = mynodes;

	pr_debug(" -> unflatten_device_tree()\n");

	if (!blob) {
		pr_debug("No device tree pointer\n");
		return;
	}

	pr_debug("Unflattening device tree:%x\n",(unsigned int)blob);
	pr_debug("magic: %08x\n", be32_to_cpu(blob->magic));
	pr_debug("size: %08x\n", be32_to_cpu(blob->totalsize));
	pr_debug("version: %08x\n", be32_to_cpu(blob->version));

	if (be32_to_cpu(blob->magic) != OF_DT_HEADER) {
		pr_err("Invalid device tree blob header\n");
		return;
	}

	/* First pass, scan for size */
	start = ((unsigned long)blob) +
		be32_to_cpu(blob->off_dt_struct);
	size = unflatten_dt_node(blob, 0, &start, NULL, NULL, 0);
	size = (size | 3) + 1;

	pr_debug("  size is %lx, allocating...\n", size);

	/* Allocate memory for the expanded device tree */
	mem = (unsigned long)
		dt_alloc(size + 4, __alignof__(struct device_node));

	memset((void *)mem, 0, size);

	((__be32 *)mem)[size / 4] = cpu_to_be32(0xdeadbeef);

	pr_debug("  unflattening %lx...\n", mem);

	/* Second pass, do actual unflattening */
	start = ((unsigned long)blob) +
		be32_to_cpu(blob->off_dt_struct);
	unflatten_dt_node(blob, mem, &start, NULL, &allnextp, 0);
	if (be32_to_cpup((__be32 *)start) != OF_DT_END)
		pr_warning("Weird tag at end of tree: %08x\n", *((u32 *)start));
	if (be32_to_cpu(((__be32 *)mem)[size / 4]) != 0xdeadbeef)
		pr_warning("End of tree marker overwritten: %08x\n",
			   be32_to_cpu(((__be32 *)mem)[size / 4]));
	*allnextp = NULL;

	pr_debug(" <- unflatten_device_tree()\n");
}

static void *kernel_tree_alloc(u64 size, u64 align)
{
	return kzalloc(size, GFP_KERNEL);
}

/**
 * of_fdt_unflatten_tree - create tree of device_nodes from flat blob
 *
 * unflattens the device-tree passed by the firmware, creating the
 * tree of struct device_node. It also fills the "name" and "type"
 * pointers of the nodes so the normal device-tree walking functions
 * can be used.
 */
void of_fdt_unflatten_tree(unsigned long *blob,
			struct device_node **mynodes)
{
	struct boot_param_header *device_tree =
		(struct boot_param_header *)blob;
	__unflatten_device_tree(device_tree, mynodes, &kernel_tree_alloc);
}
EXPORT_SYMBOL_GPL(of_fdt_unflatten_tree);

/* Everything below here references initial_boot_params directly. */
int __initdata dt_root_addr_cells;
int __initdata dt_root_size_cells;

struct boot_param_header *initial_boot_params;

#ifdef CONFIG_OF_EARLY_FLATTREE

/**
 * of_scan_flat_dt - scan flattened tree blob and call callback on each.
 * @it: callback function
 * @data: context data pointer
 *
 * This function is used to scan the flattened device-tree, it is
 * used to extract the memory information at boot before we can
 * unflatten the tree
 */
int __init of_scan_flat_dt(int (*it)(unsigned long node,
				     const char *uname, int depth,
				     void *data),
			   void *data)
{
	unsigned long p = ((unsigned long)initial_boot_params) +
		be32_to_cpu(initial_boot_params->off_dt_struct);
	int rc = 0;
	int depth = -1;

	do {
		u32 tag = be32_to_cpup((__be32 *)p);
		const char *pathp;

		p += 4;
		if (tag == OF_DT_END_NODE) {
			depth--;
			continue;
		}
		if (tag == OF_DT_NOP)
			continue;
		if (tag == OF_DT_END)
			break;
		if (tag == OF_DT_PROP) {
			u32 sz = be32_to_cpup((__be32 *)p);
			p += 8;
			if (be32_to_cpu(initial_boot_params->version) < 0x10)
				p = ALIGN(p, sz >= 8 ? 8 : 4);
			p += sz;
			p = ALIGN(p, 4);
			continue;
		}
		if (tag != OF_DT_BEGIN_NODE) {
			pr_err("Invalid tag %x in flat device tree!\n", tag);
			return -EINVAL;
		}
		depth++;
		pathp = (char *)p;
		p = ALIGN(p + strlen(pathp) + 1, 4);
		if (*pathp == '/')
			pathp = kbasename(pathp);
		rc = it(p, pathp, depth, data);
		if (rc != 0)
			break;
	} while (1);

	return rc;
}

/**
 * of_get_flat_dt_root - find the root node in the flat blob
 */
unsigned long __init of_get_flat_dt_root(void)
{
	unsigned long p = ((unsigned long)initial_boot_params) +
		be32_to_cpu(initial_boot_params->off_dt_struct);

	while (be32_to_cpup((__be32 *)p) == OF_DT_NOP)
		p += 4;
	BUG_ON(be32_to_cpup((__be32 *)p) != OF_DT_BEGIN_NODE);
	p += 4;
	return ALIGN(p + strlen((char *)p) + 1, 4);
}

/**
 * of_get_flat_dt_prop - Given a node in the flat blob, return the property ptr
 *
 * This function can be used within scan_flattened_dt callback to get
 * access to properties
 */
void *__init of_get_flat_dt_prop(unsigned long node, const char *name,
				 unsigned long *size)
{
	return of_fdt_get_property(initial_boot_params, node, name, size);
}

/**
 * of_flat_dt_is_compatible - Return true if given node has compat in compatible list
 * @node: node to test
 * @compat: compatible string to compare with compatible list.
 */
int __init of_flat_dt_is_compatible(unsigned long node, const char *compat)
{
	return of_fdt_is_compatible(initial_boot_params, node, compat);
}

/**
 * of_flat_dt_match - Return true if node matches a list of compatible values
 */
int __init of_flat_dt_match(unsigned long node, const char *const *compat)
{
	return of_fdt_match(initial_boot_params, node, compat);
}

#ifdef CONFIG_BLK_DEV_INITRD
/**
 * early_init_dt_check_for_initrd - Decode initrd location from flat tree
 * @node: reference to node containing initrd location ('chosen')
 */
void __init early_init_dt_check_for_initrd(unsigned long node)
{
	unsigned long start, end, len;
	__be32 *prop;

	pr_debug("Looking for initrd properties... ");

	prop = of_get_flat_dt_prop(node, "linux,initrd-start", &len);
	if (!prop)
		return;
	start = of_read_ulong(prop, len/4);

	prop = of_get_flat_dt_prop(node, "linux,initrd-end", &len);
	if (!prop)
		return;
	end = of_read_ulong(prop, len/4);

	early_init_dt_setup_initrd_arch(start, end);
	pr_debug("initrd_start=0x%lx  initrd_end=0x%lx\n", start, end);
}
#else
inline void early_init_dt_check_for_initrd(unsigned long node)
{
}
#endif /* CONFIG_BLK_DEV_INITRD */

/**
 * early_init_dt_scan_root - fetch the top level address and size cells
 */
int __init early_init_dt_scan_root(unsigned long node, const char *uname,
				   int depth, void *data)
{
	__be32 *prop;

	if (depth != 0)
		return 0;

	dt_root_size_cells = OF_ROOT_NODE_SIZE_CELLS_DEFAULT;
	dt_root_addr_cells = OF_ROOT_NODE_ADDR_CELLS_DEFAULT;

	prop = of_get_flat_dt_prop(node, "#size-cells", NULL);
	if (prop)
		dt_root_size_cells = be32_to_cpup(prop);
	pr_debug("dt_root_size_cells = %x\n", dt_root_size_cells);

	prop = of_get_flat_dt_prop(node, "#address-cells", NULL);
	if (prop)
		dt_root_addr_cells = be32_to_cpup(prop);
	pr_debug("dt_root_addr_cells = %x\n", dt_root_addr_cells);

	/* break now */
	return 1;
}

u64 __init dt_mem_next_cell(int s, __be32 **cellp)
{
	__be32 *p = *cellp;

	*cellp = p + s;
	return of_read_number(p, s);
}

#if defined(CONFIG_PLAT_MESON)
extern unsigned long long aml_reserved_start;
extern unsigned long long aml_reserved_end;
unsigned long long phys_offset=0;

#define MAX_RESERVE_BLOCK  32
//limit: reserve block < 32
#define DSP_MEM_SIZE	0x100000

#define FIRMWARE_ADDR 0x9ff00000

#ifdef CONFIG_MESON_TRUSTZONE
#define EARLY_RESERVED_MEM_SIZE	(DSP_MEM_SIZE+meson_secure_mem_total_size())
#else
#define EARLY_RESERVED_MEM_SIZE	(DSP_MEM_SIZE)
#endif
#define MEM_BLOCK1_SIZE	0x4000000

struct reserve_mem{
	unsigned long long startaddr;
	unsigned long long size;
	unsigned int flag;					//0: high memory  1:low memory
	char name[16];					//limit: device name must < 14;
};

struct reserve_mgr{
	int count;
	unsigned long long start_memory_addr;
	unsigned long long total_memory;
	unsigned long long current_addr_from_low;
	unsigned long long current_addr_from_high;
	struct reserve_mem reserve[MAX_RESERVE_BLOCK];
};

struct reserve_mgr Reserve_Manager;
struct reserve_mgr * pReserve_Manager;


int init_reserve_mgr(void)
{
	pReserve_Manager = &Reserve_Manager;
	pReserve_Manager->count = 0;
	pReserve_Manager->current_addr_from_low=0;
	pReserve_Manager->current_addr_from_high=0;
	return 0;
}

unsigned long long get_reserve_end(void)
{
	return pReserve_Manager->current_addr_from_low+aml_reserved_start+EARLY_RESERVED_MEM_SIZE-1;
}

unsigned long long get_high_reserve_size(void)
{
	return pReserve_Manager->current_addr_from_high;
}

void set_memory_start_addr(unsigned long long addr)
{
	pReserve_Manager->start_memory_addr = addr;
}

void set_memory_total_size(unsigned long long size)
{
	pReserve_Manager->total_memory = size;
}

int find_reserve_block(const char * name,int idx)
{
	int i;

	for(i=0;i<pReserve_Manager->count;i++)
	{
		if((strncmp(pReserve_Manager->reserve[i].name,name,strlen(name))==0)&&(pReserve_Manager->reserve[i].name[strlen(name)]=='0'+idx))
			return i;
	}

	return -1;
}

int find_reserve_block_by_name(const char * name)
{
	int i;

	for(i=0;i<pReserve_Manager->count;i++)
	{
		if(strcmp(pReserve_Manager->reserve[i].name,name)==0)
			return i;
	}

	return -1;
}

unsigned long long get_reserve_block_addr(int blockid)
{
	unsigned long long addr;
	struct reserve_mem * prm = &pReserve_Manager->reserve[blockid];
	if(blockid >= MAX_RESERVE_BLOCK)
		printk("error: reserve block count is larger than MAX_RESERVE_BLOCK,please reset the code\n");

	if(prm->flag)
	{
		addr = prm->startaddr+aml_reserved_start+EARLY_RESERVED_MEM_SIZE;
	}
	else
	{
		addr = pReserve_Manager->start_memory_addr+pReserve_Manager->total_memory-prm->startaddr-prm->size;
	}

	return addr;
}

unsigned long long get_reserve_block_size(int blockid)
{
	if(blockid >= MAX_RESERVE_BLOCK)
		printk("error: reserve block count is larger than MAX_RESERVE_BLOCK,please reset the code\n");
	return pReserve_Manager->reserve[blockid].size;
}


/**
 * early_init_dt_scan_memory - Look for an parse memory nodes
 */
int __init early_init_dt_scan_reserve_memory(unsigned long node, const char *uname,
				     int depth, void *data)
{
	__be32 *mem, *endp;
	char * need_iomap=NULL;
	unsigned int iomap_flag = 0;
	unsigned long l;
	int idx=0;
	struct reserve_mem * prm;

	mem = of_get_flat_dt_prop(node, "reserve-memory", &l);
	if (mem == NULL)
		return 0;

	endp = mem + (l / sizeof(__be32));
	while ((endp - mem) >= dt_root_size_cells) {
		u64 size;

		size = dt_mem_next_cell(dt_root_size_cells, &mem);

		if (size == 0)
			continue;

		need_iomap = of_get_flat_dt_prop(node,"reserve-iomap",&l);
		if(need_iomap&&(strcmp(need_iomap,"true")==0))
		{
			iomap_flag = 1;
		}

		prm = &pReserve_Manager->reserve[pReserve_Manager->count];

		if(iomap_flag)
		{
			prm->startaddr = pReserve_Manager->current_addr_from_low;
			prm->size = size;
			strcpy(prm->name,uname);
			prm->name[strlen(uname)] = '0'+idx;
			prm->name[strlen(uname)+1] = 0;
			pReserve_Manager->current_addr_from_low +=size;
		}
		else
		{
			prm->startaddr = pReserve_Manager->current_addr_from_high;
			prm->size = size;
			strcpy(prm->name,uname);
			prm->name[strlen(uname)] = '0'+idx;
			prm->name[strlen(uname)+1] = 0;
			pReserve_Manager->current_addr_from_high +=size;
		}
		prm->flag = iomap_flag;
		pReserve_Manager->count +=1;
		idx++;

		if(pReserve_Manager->count >= MAX_RESERVE_BLOCK){
			printk("error: reserve block count is larger than MAX_RESERVE_BLOCK,please reset the code\n");
			break;
		}
	}

	return 0;
}
#endif
/**
 * early_init_dt_scan_memory - Look for an parse memory nodes
 */
int __init early_init_dt_scan_memory(unsigned long node, const char *uname,
				     int depth, void *data)
{
	char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	__be32 *reg;
	unsigned long l;
#if defined(CONFIG_PLAT_MESON)
	unsigned long long high_reserve_size;
	struct reserve_mem * prm;
	int i;
	u64 total;
	unsigned long long phys_offset=0;
#else
	__be32 * endp;
#endif
	/* We are scanning "memory" nodes only */
	if (type == NULL) {
		/*
		 * The longtrail doesn't have a device_type on the
		 * /memory node, so look for the node called /memory@0.
		 */
		if (depth != 1 || strcmp(uname, "memory@0") != 0)
			return 0;
	} else if (strcmp(type, "memory") != 0)
		return 0;

#if defined(CONFIG_PLAT_MESON)
	reg = of_get_flat_dt_prop(node, "aml_reserved_start", &l);
	if (reg == NULL)
		printk("error: can not get reserved mem start for AML\n");
	else
		aml_reserved_start = of_read_number(reg,1);

	reg = of_get_flat_dt_prop(node, "aml_reserved_end", &l);
	if (reg == NULL)
		printk("error: can not get reserved mem end for AML\n");
	else
		aml_reserved_end = of_read_number(reg,1);
	reg = of_get_flat_dt_prop(node, "phys_offset", &l);
	if (reg == NULL)
		printk("error: can not get phys_offset for AML\n");
	else
	{
		phys_offset = of_read_number(reg,1);
		printk("physical memory start address is 0x%llx\n",phys_offset);
	}

	early_init_dt_add_memory_arch(phys_offset,MEM_BLOCK1_SIZE);
	early_init_dt_add_memory_arch(aml_reserved_end,aml_reserved_start-aml_reserved_end);

	aml_reserved_end = get_reserve_end();
	pr_info("reserved_end is %llx \n ",aml_reserved_end);
	high_reserve_size = get_high_reserve_size();

	reg = of_get_flat_dt_prop(node, "linux,total-memory", &l);
	if (reg == NULL){
		printk("error: can not get total-memory for AML\n");
		return -1;
	}
	else
		total =  of_read_number(reg,1);

	set_memory_start_addr(phys_offset);
	set_memory_total_size(total);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TV
	early_init_dt_add_memory_arch(aml_reserved_end+1,phys_offset+total-aml_reserved_end-1-high_reserve_size);
#else
	if(aml_reserved_end+1 > FIRMWARE_ADDR)
	{
		printk("error: firmware memory has been used\n");
		return -1;
	}
	else{
		early_init_dt_add_memory_arch(aml_reserved_end+1,FIRMWARE_ADDR-aml_reserved_end-1);  //511M-512M reserved for firmware
		early_init_dt_add_memory_arch(FIRMWARE_ADDR+0x100000, phys_offset+total-(FIRMWARE_ADDR+0x100000)-high_reserve_size);
		printk("reserved 511M-512M 1M memory for firmware\n");
	}
#endif

	pr_info("Total memory is %4d MiB\n",((unsigned int)total >> 20));
	pr_info("Reserved low memory from 0x%08llx to 0x%08llx, size: %3ld MiB \n",
		aml_reserved_start,aml_reserved_end,
		((unsigned long)(aml_reserved_end - aml_reserved_start + 1)) >> 20);

	for(i = 0; i < MAX_RESERVE_BLOCK; i++){
		prm = &pReserve_Manager->reserve[i];
		if(!prm->size)
			break;
		if(prm->flag)
		{
			pr_info("\t%s(low)   \t: 0x%08llx - 0x%08llx (%3ld MiB)\n",
				prm->name,
				prm->startaddr + aml_reserved_start+EARLY_RESERVED_MEM_SIZE ,
				prm->startaddr + prm->size + aml_reserved_start+EARLY_RESERVED_MEM_SIZE,
				(unsigned long)(prm->size >> 20));
		}
		else
		{
			pr_info("\t%s(high)   \t: 0x%08llx - 0x%08llx (%3ld MiB)\n",
				prm->name,
				pReserve_Manager->start_memory_addr+pReserve_Manager->total_memory-prm->startaddr-prm->size,
				pReserve_Manager->start_memory_addr+pReserve_Manager->total_memory-prm->startaddr,
				(unsigned long)(prm->size >> 20));
		}
	}
#else
	reg = of_get_flat_dt_prop(node, "linux,usable-memory", &l);
	if (reg == NULL)
		reg = of_get_flat_dt_prop(node, "reg", &l);
	if (reg == NULL)
		return 0;

	endp = reg + (l / sizeof(__be32));

	pr_debug("memory scan node %s, reg size %ld, data: %x %x %x %x,\n",
	    uname, l, reg[0], reg[1], reg[2], reg[3]);

	while ((endp - reg) >= (dt_root_addr_cells + dt_root_size_cells)) {
		u64 base, size;

		base = dt_mem_next_cell(dt_root_addr_cells, &reg);
		size = dt_mem_next_cell(dt_root_size_cells, &reg);

		if (size == 0)
			continue;
		pr_debug(" - %llx ,  %llx\n", (unsigned long long)base,
		    (unsigned long long)size);

		early_init_dt_add_memory_arch(base, size);
	}

#endif
	return 0;
}

int __init early_init_dt_scan_chosen(unsigned long node, const char *uname,
				     int depth, void *data)
{
	unsigned long l;
	char *p;

	pr_debug("search \"chosen\", depth: %d, uname: %s\n", depth, uname);

	if (depth != 1 || !data ||
	    (strcmp(uname, "chosen") != 0 && strcmp(uname, "chosen@0") != 0))
		return 0;

	early_init_dt_check_for_initrd(node);

	/* Retrieve command line */
	p = of_get_flat_dt_prop(node, "bootargs", &l);
	if (p != NULL && l > 0)
		strlcpy(data, p, min((int)l, COMMAND_LINE_SIZE));

	/*
	 * CONFIG_CMDLINE is meant to be a default in case nothing else
	 * managed to set the command line, unless CONFIG_CMDLINE_FORCE
	 * is set in which case we override whatever was found earlier.
	 */
#ifdef CONFIG_CMDLINE
#ifndef CONFIG_CMDLINE_FORCE
	if (!((char *)data)[0])
#endif
		strlcpy(data, CONFIG_CMDLINE, COMMAND_LINE_SIZE);
#endif /* CONFIG_CMDLINE */

	pr_debug("Command line is: %s\n", (char*)data);

	/* break now */
	return 1;
}

/**
 * unflatten_device_tree - create tree of device_nodes from flat blob
 *
 * unflattens the device-tree passed by the firmware, creating the
 * tree of struct device_node. It also fills the "name" and "type"
 * pointers of the nodes so the normal device-tree walking functions
 * can be used.
 */
void __init unflatten_device_tree(void)
{
	__unflatten_device_tree(initial_boot_params, &of_allnodes,
				early_init_dt_alloc_memory_arch);

	/* Get pointer to "/chosen" and "/aliasas" nodes for use everywhere */
	of_alias_scan(early_init_dt_alloc_memory_arch);
}

#endif /* CONFIG_OF_EARLY_FLATTREE */
