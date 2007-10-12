/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/sched.h>
#include <linux/console.h>
#include <linux/ioport.h>
#include <linux/bootmem.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/pfn.h>
#include <linux/root_dev.h>
#include <linux/cpu.h>
#include <linux/kernel.h>

#include <asm/sections.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/setup.h>
#include <asm/sysreg.h>

#include <asm/arch/board.h>
#include <asm/arch/init.h>

extern int root_mountflags;

/*
 * Initialize loops_per_jiffy as 5000000 (500MIPS).
 * Better make it too large than too small...
 */
struct avr32_cpuinfo boot_cpu_data = {
	.loops_per_jiffy = 5000000
};
EXPORT_SYMBOL(boot_cpu_data);

static char __initdata command_line[COMMAND_LINE_SIZE];

/*
 * Standard memory resources
 */
static struct resource __initdata kernel_data = {
	.name	= "Kernel data",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_MEM,
};
static struct resource __initdata kernel_code = {
	.name	= "Kernel code",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_MEM,
	.sibling = &kernel_data,
};

/*
 * Available system RAM and reserved regions as singly linked
 * lists. These lists are traversed using the sibling pointer in
 * struct resource and are kept sorted at all times.
 */
static struct resource *__initdata system_ram;
static struct resource *__initdata reserved = &kernel_code;

/*
 * We need to allocate these before the bootmem allocator is up and
 * running, so we need this "cache". 32 entries are probably enough
 * for all but the most insanely complex systems.
 */
static struct resource __initdata res_cache[32];
static unsigned int __initdata res_cache_next_free;

static void __init resource_init(void)
{
	struct resource *mem, *res;
	struct resource *new;

	kernel_code.start = __pa(init_mm.start_code);

	for (mem = system_ram; mem; mem = mem->sibling) {
		new = alloc_bootmem_low(sizeof(struct resource));
		memcpy(new, mem, sizeof(struct resource));

		new->sibling = NULL;
		if (request_resource(&iomem_resource, new))
			printk(KERN_WARNING "Bad RAM resource %08x-%08x\n",
			       mem->start, mem->end);
	}

	for (res = reserved; res; res = res->sibling) {
		new = alloc_bootmem_low(sizeof(struct resource));
		memcpy(new, res, sizeof(struct resource));

		new->sibling = NULL;
		if (insert_resource(&iomem_resource, new))
			printk(KERN_WARNING
			       "Bad reserved resource %s (%08x-%08x)\n",
			       res->name, res->start, res->end);
	}
}

static void __init
add_physical_memory(resource_size_t start, resource_size_t end)
{
	struct resource *new, *next, **pprev;

	for (pprev = &system_ram, next = system_ram; next;
	     pprev = &next->sibling, next = next->sibling) {
		if (end < next->start)
			break;
		if (start <= next->end) {
			printk(KERN_WARNING
			       "Warning: Physical memory map is broken\n");
			printk(KERN_WARNING
			       "Warning: %08x-%08x overlaps %08x-%08x\n",
			       start, end, next->start, next->end);
			return;
		}
	}

	if (res_cache_next_free >= ARRAY_SIZE(res_cache)) {
		printk(KERN_WARNING
		       "Warning: Failed to add physical memory %08x-%08x\n",
		       start, end);
		return;
	}

	new = &res_cache[res_cache_next_free++];
	new->start = start;
	new->end = end;
	new->name = "System RAM";
	new->flags = IORESOURCE_MEM;

	*pprev = new;
}

static int __init
add_reserved_region(resource_size_t start, resource_size_t end,
		    const char *name)
{
	struct resource *new, *next, **pprev;

	if (end < start)
		return -EINVAL;

	if (res_cache_next_free >= ARRAY_SIZE(res_cache))
		return -ENOMEM;

	for (pprev = &reserved, next = reserved; next;
	     pprev = &next->sibling, next = next->sibling) {
		if (end < next->start)
			break;
		if (start <= next->end)
			return -EBUSY;
	}

	new = &res_cache[res_cache_next_free++];
	new->start = start;
	new->end = end;
	new->name = name;
	new->flags = IORESOURCE_MEM;

	*pprev = new;

	return 0;
}

static unsigned long __init
find_free_region(const struct resource *mem, resource_size_t size,
		 resource_size_t align)
{
	struct resource *res;
	unsigned long target;

	target = ALIGN(mem->start, align);
	for (res = reserved; res; res = res->sibling) {
		if ((target + size) <= res->start)
			break;
		if (target <= res->end)
			target = ALIGN(res->end + 1, align);
	}

	if ((target + size) > (mem->end + 1))
		return mem->end + 1;

	return target;
}

static int __init
alloc_reserved_region(resource_size_t *start, resource_size_t size,
		      resource_size_t align, const char *name)
{
	struct resource *mem;
	resource_size_t target;
	int ret;

	for (mem = system_ram; mem; mem = mem->sibling) {
		target = find_free_region(mem, size, align);
		if (target <= mem->end) {
			ret = add_reserved_region(target, target + size - 1,
						  name);
			if (!ret)
				*start = target;
			return ret;
		}
	}

	return -ENOMEM;
}

/*
 * Early framebuffer allocation. Works as follows:
 *   - If fbmem_size is zero, nothing will be allocated or reserved.
 *   - If fbmem_start is zero when setup_bootmem() is called,
 *     a block of fbmem_size bytes will be reserved before bootmem
 *     initialization. It will be aligned to the largest page size
 *     that fbmem_size is a multiple of.
 *   - If fbmem_start is nonzero, an area of size fbmem_size will be
 *     reserved at the physical address fbmem_start if possible. If
 *     it collides with other reserved memory, a different block of
 *     same size will be allocated, just as if fbmem_start was zero.
 *
 * Board-specific code may use these variables to set up platform data
 * for the framebuffer driver if fbmem_size is nonzero.
 */
resource_size_t __initdata fbmem_start;
resource_size_t __initdata fbmem_size;

/*
 * "fbmem=xxx[kKmM]" allocates the specified amount of boot memory for
 * use as framebuffer.
 *
 * "fbmem=xxx[kKmM]@yyy[kKmM]" defines a memory region of size xxx and
 * starting at yyy to be reserved for use as framebuffer.
 *
 * The kernel won't verify that the memory region starting at yyy
 * actually contains usable RAM.
 */
static int __init early_parse_fbmem(char *p)
{
	int ret;
	unsigned long align;

	fbmem_size = memparse(p, &p);
	if (*p == '@') {
		fbmem_start = memparse(p + 1, &p);
		ret = add_reserved_region(fbmem_start,
					  fbmem_start + fbmem_size - 1,
					  "Framebuffer");
		if (ret) {
			printk(KERN_WARNING
			       "Failed to reserve framebuffer memory\n");
			fbmem_start = 0;
		}
	}

	if (!fbmem_start) {
		if ((fbmem_size & 0x000fffffUL) == 0)
			align = 0x100000;	/* 1 MiB */
		else if ((fbmem_size & 0x0000ffffUL) == 0)
			align = 0x10000;	/* 64 KiB */
		else
			align = 0x1000;		/* 4 KiB */

		ret = alloc_reserved_region(&fbmem_start, fbmem_size,
					    align, "Framebuffer");
		if (ret) {
			printk(KERN_WARNING
			       "Failed to allocate framebuffer memory\n");
			fbmem_size = 0;
		}
	}

	return 0;
}
early_param("fbmem", early_parse_fbmem);

static int __init parse_tag_core(struct tag *tag)
{
	if (tag->hdr.size > 2) {
		if ((tag->u.core.flags & 1) == 0)
			root_mountflags &= ~MS_RDONLY;
		ROOT_DEV = new_decode_dev(tag->u.core.rootdev);
	}
	return 0;
}
__tagtable(ATAG_CORE, parse_tag_core);

static int __init parse_tag_mem(struct tag *tag)
{
	unsigned long start, end;

	/*
	 * Ignore zero-sized entries. If we're running standalone, the
	 * SDRAM code may emit such entries if something goes
	 * wrong...
	 */
	if (tag->u.mem_range.size == 0)
		return 0;

	start = tag->u.mem_range.addr;
	end = tag->u.mem_range.addr + tag->u.mem_range.size - 1;

	add_physical_memory(start, end);
	return 0;
}
__tagtable(ATAG_MEM, parse_tag_mem);

static int __init parse_tag_rdimg(struct tag *tag)
{
#ifdef CONFIG_BLK_DEV_INITRD
	struct tag_mem_range *mem = &tag->u.mem_range;
	int ret;

	if (initrd_start) {
		printk(KERN_WARNING
		       "Warning: Only the first initrd image will be used\n");
		return 0;
	}

	ret = add_reserved_region(mem->addr, mem->addr + mem->size - 1,
				  "initrd");
	if (ret) {
		printk(KERN_WARNING
		       "Warning: Failed to reserve initrd memory\n");
		return ret;
	}

	initrd_start = (unsigned long)__va(mem->addr);
	initrd_end = initrd_start + mem->size;
#else
	printk(KERN_WARNING "RAM disk image present, but "
	       "no initrd support in kernel, ignoring\n");
#endif

	return 0;
}
__tagtable(ATAG_RDIMG, parse_tag_rdimg);

static int __init parse_tag_rsvd_mem(struct tag *tag)
{
	struct tag_mem_range *mem = &tag->u.mem_range;

	return add_reserved_region(mem->addr, mem->addr + mem->size - 1,
				   "Reserved");
}
__tagtable(ATAG_RSVD_MEM, parse_tag_rsvd_mem);

static int __init parse_tag_cmdline(struct tag *tag)
{
	strlcpy(boot_command_line, tag->u.cmdline.cmdline, COMMAND_LINE_SIZE);
	return 0;
}
__tagtable(ATAG_CMDLINE, parse_tag_cmdline);

static int __init parse_tag_clock(struct tag *tag)
{
	/*
	 * We'll figure out the clocks by peeking at the system
	 * manager regs directly.
	 */
	return 0;
}
__tagtable(ATAG_CLOCK, parse_tag_clock);

/*
 * Scan the tag table for this tag, and call its parse function. The
 * tag table is built by the linker from all the __tagtable
 * declarations.
 */
static int __init parse_tag(struct tag *tag)
{
	extern struct tagtable __tagtable_begin, __tagtable_end;
	struct tagtable *t;

	for (t = &__tagtable_begin; t < &__tagtable_end; t++)
		if (tag->hdr.tag == t->tag) {
			t->parse(tag);
			break;
		}

	return t < &__tagtable_end;
}

/*
 * Parse all tags in the list we got from the boot loader
 */
static void __init parse_tags(struct tag *t)
{
	for (; t->hdr.tag != ATAG_NONE; t = tag_next(t))
		if (!parse_tag(t))
			printk(KERN_WARNING
			       "Ignoring unrecognised tag 0x%08x\n",
			       t->hdr.tag);
}

/*
 * Find a free memory region large enough for storing the
 * bootmem bitmap.
 */
static unsigned long __init
find_bootmap_pfn(const struct resource *mem)
{
	unsigned long bootmap_pages, bootmap_len;
	unsigned long node_pages = PFN_UP(mem->end - mem->start + 1);
	unsigned long bootmap_start;

	bootmap_pages = bootmem_bootmap_pages(node_pages);
	bootmap_len = bootmap_pages << PAGE_SHIFT;

	/*
	 * Find a large enough region without reserved pages for
	 * storing the bootmem bitmap. We can take advantage of the
	 * fact that all lists have been sorted.
	 *
	 * We have to check that we don't collide with any reserved
	 * regions, which includes the kernel image and any RAMDISK
	 * images.
	 */
	bootmap_start = find_free_region(mem, bootmap_len, PAGE_SIZE);

	return bootmap_start >> PAGE_SHIFT;
}

#define MAX_LOWMEM	HIGHMEM_START
#define MAX_LOWMEM_PFN	PFN_DOWN(MAX_LOWMEM)

static void __init setup_bootmem(void)
{
	unsigned bootmap_size;
	unsigned long first_pfn, bootmap_pfn, pages;
	unsigned long max_pfn, max_low_pfn;
	unsigned node = 0;
	struct resource *res;

	printk(KERN_INFO "Physical memory:\n");
	for (res = system_ram; res; res = res->sibling)
		printk("  %08x-%08x\n", res->start, res->end);
	printk(KERN_INFO "Reserved memory:\n");
	for (res = reserved; res; res = res->sibling)
		printk("  %08x-%08x: %s\n",
		       res->start, res->end, res->name);

	nodes_clear(node_online_map);

	if (system_ram->sibling)
		printk(KERN_WARNING "Only using first memory bank\n");

	for (res = system_ram; res; res = NULL) {
		first_pfn = PFN_UP(res->start);
		max_low_pfn = max_pfn = PFN_DOWN(res->end + 1);
		bootmap_pfn = find_bootmap_pfn(res);
		if (bootmap_pfn > max_pfn)
			panic("No space for bootmem bitmap!\n");

		if (max_low_pfn > MAX_LOWMEM_PFN) {
			max_low_pfn = MAX_LOWMEM_PFN;
#ifndef CONFIG_HIGHMEM
			/*
			 * Lowmem is memory that can be addressed
			 * directly through P1/P2
			 */
			printk(KERN_WARNING
			       "Node %u: Only %ld MiB of memory will be used.\n",
			       node, MAX_LOWMEM >> 20);
			printk(KERN_WARNING "Use a HIGHMEM enabled kernel.\n");
#else
#error HIGHMEM is not supported by AVR32 yet
#endif
		}

		/* Initialize the boot-time allocator with low memory only. */
		bootmap_size = init_bootmem_node(NODE_DATA(node), bootmap_pfn,
						 first_pfn, max_low_pfn);

		/*
		 * Register fully available RAM pages with the bootmem
		 * allocator.
		 */
		pages = max_low_pfn - first_pfn;
		free_bootmem_node (NODE_DATA(node), PFN_PHYS(first_pfn),
				   PFN_PHYS(pages));

		/* Reserve space for the bootmem bitmap... */
		reserve_bootmem_node(NODE_DATA(node),
				     PFN_PHYS(bootmap_pfn),
				     bootmap_size);

		/* ...and any other reserved regions. */
		for (res = reserved; res; res = res->sibling) {
			if (res->start > PFN_PHYS(max_pfn))
				break;

			/*
			 * resource_init will complain about partial
			 * overlaps, so we'll just ignore such
			 * resources for now.
			 */
			if (res->start >= PFN_PHYS(first_pfn)
			    && res->end < PFN_PHYS(max_pfn))
				reserve_bootmem_node(
					NODE_DATA(node), res->start,
					res->end - res->start + 1);
		}

		node_set_online(node);
	}
}

void __init setup_arch (char **cmdline_p)
{
	struct clk *cpu_clk;

	init_mm.start_code = (unsigned long)_text;
	init_mm.end_code = (unsigned long)_etext;
	init_mm.end_data = (unsigned long)_edata;
	init_mm.brk = (unsigned long)_end;

	/*
	 * Include .init section to make allocations easier. It will
	 * be removed before the resource is actually requested.
	 */
	kernel_code.start = __pa(__init_begin);
	kernel_code.end = __pa(init_mm.end_code - 1);
	kernel_data.start = __pa(init_mm.end_code);
	kernel_data.end = __pa(init_mm.brk - 1);

	parse_tags(bootloader_tags);

	setup_processor();
	setup_platform();
	setup_board();

	cpu_clk = clk_get(NULL, "cpu");
	if (IS_ERR(cpu_clk)) {
		printk(KERN_WARNING "Warning: Unable to get CPU clock\n");
	} else {
		unsigned long cpu_hz = clk_get_rate(cpu_clk);

		/*
		 * Well, duh, but it's probably a good idea to
		 * increment the use count.
		 */
		clk_enable(cpu_clk);

		boot_cpu_data.clk = cpu_clk;
		boot_cpu_data.loops_per_jiffy = cpu_hz * 4;
		printk("CPU: Running at %lu.%03lu MHz\n",
		       ((cpu_hz + 500) / 1000) / 1000,
		       ((cpu_hz + 500) / 1000) % 1000);
	}

	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;
	parse_early_param();

	setup_bootmem();

#ifdef CONFIG_VT
	conswitchp = &dummy_con;
#endif

	paging_init();
	resource_init();
}
