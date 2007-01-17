/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/console.h>
#include <linux/ioport.h>
#include <linux/bootmem.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/root_dev.h>
#include <linux/cpu.h>

#include <asm/sections.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/setup.h>
#include <asm/sysreg.h>

#include <asm/arch/board.h>
#include <asm/arch/init.h>

extern int root_mountflags;

/*
 * Bootloader-provided information about physical memory
 */
struct tag_mem_range *mem_phys;
struct tag_mem_range *mem_reserved;
struct tag_mem_range *mem_ramdisk;

/*
 * Initialize loops_per_jiffy as 5000000 (500MIPS).
 * Better make it too large than too small...
 */
struct avr32_cpuinfo boot_cpu_data = {
	.loops_per_jiffy = 5000000
};
EXPORT_SYMBOL(boot_cpu_data);

static char command_line[COMMAND_LINE_SIZE];

/*
 * Should be more than enough, but if you have a _really_ complex
 * setup, you might need to increase the size of this...
 */
static struct tag_mem_range __initdata mem_range_cache[32];
static unsigned mem_range_next_free;

/*
 * Standard memory resources
 */
static struct resource mem_res[] = {
	{
		.name	= "Kernel code",
		.start	= 0,
		.end	= 0,
		.flags	= IORESOURCE_MEM
	},
	{
		.name	= "Kernel data",
		.start	= 0,
		.end	= 0,
		.flags	= IORESOURCE_MEM,
	},
};

#define kernel_code	mem_res[0]
#define kernel_data	mem_res[1]

/*
 * Early framebuffer allocation. Works as follows:
 *   - If fbmem_size is zero, nothing will be allocated or reserved.
 *   - If fbmem_start is zero when setup_bootmem() is called,
 *     fbmem_size bytes will be allocated from the bootmem allocator.
 *   - If fbmem_start is nonzero, an area of size fbmem_size will be
 *     reserved at the physical address fbmem_start if necessary. If
 *     the area isn't in a memory region known to the kernel, it will
 *     be left alone.
 *
 * Board-specific code may use these variables to set up platform data
 * for the framebuffer driver if fbmem_size is nonzero.
 */
static unsigned long __initdata fbmem_start;
static unsigned long __initdata fbmem_size;

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
	fbmem_size = memparse(p, &p);
	if (*p == '@')
		fbmem_start = memparse(p, &p);
	return 0;
}
early_param("fbmem", early_parse_fbmem);

static inline void __init resource_init(void)
{
	struct tag_mem_range *region;

	kernel_code.start = __pa(init_mm.start_code);
	kernel_code.end = __pa(init_mm.end_code - 1);
	kernel_data.start = __pa(init_mm.end_code);
	kernel_data.end = __pa(init_mm.brk - 1);

	for (region = mem_phys; region; region = region->next) {
		struct resource *res;
		unsigned long phys_start, phys_end;

		if (region->size == 0)
			continue;

		phys_start = region->addr;
		phys_end = phys_start + region->size - 1;

		res = alloc_bootmem_low(sizeof(*res));
		res->name = "System RAM";
		res->start = phys_start;
		res->end = phys_end;
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;

		request_resource (&iomem_resource, res);

		if (kernel_code.start >= res->start &&
		    kernel_code.end <= res->end)
			request_resource (res, &kernel_code);
		if (kernel_data.start >= res->start &&
		    kernel_data.end <= res->end)
			request_resource (res, &kernel_data);
	}
}

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

static int __init parse_tag_mem_range(struct tag *tag,
				      struct tag_mem_range **root)
{
	struct tag_mem_range *cur, **pprev;
	struct tag_mem_range *new;

	/*
	 * Ignore zero-sized entries. If we're running standalone, the
	 * SDRAM code may emit such entries if something goes
	 * wrong...
	 */
	if (tag->u.mem_range.size == 0)
		return 0;

	/*
	 * Copy the data so the bootmem init code doesn't need to care
	 * about it.
	 */
	if (mem_range_next_free >=
	    (sizeof(mem_range_cache) / sizeof(mem_range_cache[0])))
		panic("Physical memory map too complex!\n");

	new = &mem_range_cache[mem_range_next_free++];
	*new = tag->u.mem_range;

	pprev = root;
	cur = *root;
	while (cur) {
		pprev = &cur->next;
		cur = cur->next;
	}

	*pprev = new;
	new->next = NULL;

	return 0;
}

static int __init parse_tag_mem(struct tag *tag)
{
	return parse_tag_mem_range(tag, &mem_phys);
}
__tagtable(ATAG_MEM, parse_tag_mem);

static int __init parse_tag_cmdline(struct tag *tag)
{
	strlcpy(saved_command_line, tag->u.cmdline.cmdline, COMMAND_LINE_SIZE);
	return 0;
}
__tagtable(ATAG_CMDLINE, parse_tag_cmdline);

static int __init parse_tag_rdimg(struct tag *tag)
{
	return parse_tag_mem_range(tag, &mem_ramdisk);
}
__tagtable(ATAG_RDIMG, parse_tag_rdimg);

static int __init parse_tag_clock(struct tag *tag)
{
	/*
	 * We'll figure out the clocks by peeking at the system
	 * manager regs directly.
	 */
	return 0;
}
__tagtable(ATAG_CLOCK, parse_tag_clock);

static int __init parse_tag_rsvd_mem(struct tag *tag)
{
	return parse_tag_mem_range(tag, &mem_reserved);
}
__tagtable(ATAG_RSVD_MEM, parse_tag_rsvd_mem);

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

void __init setup_arch (char **cmdline_p)
{
	struct clk *cpu_clk;

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

	init_mm.start_code = (unsigned long) &_text;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) &_end;

	strlcpy(command_line, saved_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;
	parse_early_param();

	setup_bootmem();

	board_setup_fbmem(fbmem_start, fbmem_size);

#ifdef CONFIG_VT
	conswitchp = &dummy_con;
#endif

	paging_init();

	resource_init();
}
