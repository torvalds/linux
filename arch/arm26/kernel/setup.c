/*
 *  linux/arch/arm26/kernel/setup.c
 *
 *  Copyright (C) 1995-2001 Russell King
 *  Copyright (C) 2003 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/blkdev.h>
#include <linux/console.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <linux/root_dev.h>

#include <asm/elf.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/procinfo.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/tlbflush.h>

#include <asm/irqchip.h>

#ifndef MEM_SIZE
#define MEM_SIZE	(16*1024*1024)
#endif

#ifdef CONFIG_PREEMPT
DEFINE_SPINLOCK(kernel_flag);
#endif

#if defined(CONFIG_FPE_NWFPE)
char fpe_type[8];

static int __init fpe_setup(char *line)
{
	memcpy(fpe_type, line, 8);
	return 1;
}

__setup("fpe=", fpe_setup);
#endif

extern void paging_init(struct meminfo *);
extern void convert_to_tag_list(struct tag *tags);
extern void squash_mem_tags(struct tag *tag);
extern void bootmem_init(struct meminfo *);
extern int root_mountflags;
extern int _stext, _text, _etext, _edata, _end;
#ifdef CONFIG_XIP_KERNEL
extern int _endtext, _sdata;
#endif


unsigned int processor_id;
unsigned int __machine_arch_type;
unsigned int system_rev;
unsigned int system_serial_low;
unsigned int system_serial_high;
unsigned int elf_hwcap;
unsigned int memc_ctrl_reg;
unsigned int number_mfm_drives;

struct processor processor;

char elf_platform[ELF_PLATFORM_SIZE];

unsigned long phys_initrd_start __initdata = 0;
unsigned long phys_initrd_size __initdata = 0;
static struct meminfo meminfo __initdata = { 0, };
static struct proc_info_item proc_info;
static const char *machine_name;
static char command_line[COMMAND_LINE_SIZE];

static char default_command_line[COMMAND_LINE_SIZE] __initdata = CONFIG_CMDLINE;

/*
 * Standard memory resources
 */
static struct resource mem_res[] = {
	{ "Video RAM",   0,     0,     IORESOURCE_MEM			},
	{ "Kernel code", 0,     0,     IORESOURCE_MEM			},
	{ "Kernel data", 0,     0,     IORESOURCE_MEM			}
};

#define video_ram   mem_res[0]
#define kernel_code mem_res[1]
#define kernel_data mem_res[2]

static struct resource io_res[] = {
	{ "reserved",    0x3bc, 0x3be, IORESOURCE_IO | IORESOURCE_BUSY },
	{ "reserved",    0x378, 0x37f, IORESOURCE_IO | IORESOURCE_BUSY },
	{ "reserved",    0x278, 0x27f, IORESOURCE_IO | IORESOURCE_BUSY }
};

#define lp0 io_res[0]
#define lp1 io_res[1]
#define lp2 io_res[2]

#define dump_cpu_info() do { } while (0)

static void __init setup_processor(void)
{
	extern struct proc_info_list __proc_info_begin, __proc_info_end;
	struct proc_info_list *list;

	/*
	 * locate processor in the list of supported processor
	 * types.  The linker builds this table for us from the
	 * entries in arch/arm26/mm/proc-*.S
	 */
	for (list = &__proc_info_begin; list < &__proc_info_end ; list++)
		if ((processor_id & list->cpu_mask) == list->cpu_val)
			break;

	/*
	 * If processor type is unrecognised, then we
	 * can do nothing...
	 */
	if (list >= &__proc_info_end) {
		printk("CPU configuration botched (ID %08x), unable "
		       "to continue.\n", processor_id);
		while (1);
	}

	proc_info = *list->info;
	processor = *list->proc;


	printk("CPU: %s %s revision %d\n",
	       proc_info.manufacturer, proc_info.cpu_name,
	       (int)processor_id & 15);

	dump_cpu_info();

	sprintf(system_utsname.machine, "%s", list->arch_name);
	sprintf(elf_platform, "%s", list->elf_name);
	elf_hwcap = list->elf_hwcap;

	cpu_proc_init();
}

/*
 * Initial parsing of the command line.  We need to pick out the
 * memory size.  We look for mem=size@start, where start and size
 * are "size[KkMm]"
 */
static void __init
parse_cmdline(struct meminfo *mi, char **cmdline_p, char *from)
{
	char c = ' ', *to = command_line;
	int usermem = 0, len = 0;

	for (;;) {
		if (c == ' ' && !memcmp(from, "mem=", 4)) {
			unsigned long size, start;

			if (to != command_line)
				to -= 1;

			/*
			 * If the user specifies memory size, we
			 * blow away any automatically generated
			 * size.
			 */
			if (usermem == 0) {
				usermem = 1;
				mi->nr_banks = 0;
			}

			start = PHYS_OFFSET;
			size  = memparse(from + 4, &from);
			if (*from == '@')
				start = memparse(from + 1, &from);

			mi->bank[mi->nr_banks].start = start;
			mi->bank[mi->nr_banks].size  = size;
			mi->bank[mi->nr_banks].node  = PHYS_TO_NID(start);
			mi->nr_banks += 1;
		}
		c = *from++;
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*to++ = c;
	}
	*to = '\0';
	*cmdline_p = command_line;
}

static void __init
setup_ramdisk(int doload, int prompt, int image_start, unsigned int rd_sz)
{
#ifdef CONFIG_BLK_DEV_RAM
	extern int rd_size, rd_image_start, rd_prompt, rd_doload;

	rd_image_start = image_start;
	rd_prompt = prompt;
	rd_doload = doload;

	if (rd_sz)
		rd_size = rd_sz;
#endif
}

static void __init
request_standard_resources(struct meminfo *mi)
{
	struct resource *res;
	int i;

	kernel_code.start  = init_mm.start_code;
	kernel_code.end    = init_mm.end_code - 1;
#ifdef CONFIG_XIP_KERNEL
	kernel_data.start  = init_mm.start_data;
#else
	kernel_data.start  = init_mm.end_code;
#endif
	kernel_data.end    = init_mm.brk - 1;

	for (i = 0; i < mi->nr_banks; i++) {
		unsigned long virt_start, virt_end;

		if (mi->bank[i].size == 0)
			continue;

		virt_start = mi->bank[i].start;
		virt_end   = virt_start + mi->bank[i].size - 1;

		res = alloc_bootmem_low(sizeof(*res));
		res->name  = "System RAM";
		res->start = virt_start;
		res->end   = virt_end;
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;

		request_resource(&iomem_resource, res);

		if (kernel_code.start >= res->start &&
		    kernel_code.end <= res->end)
			request_resource(res, &kernel_code);
		if (kernel_data.start >= res->start &&
		    kernel_data.end <= res->end)
			request_resource(res, &kernel_data);
	}

/*	FIXME - needed? if (mdesc->video_start) {
		video_ram.start = mdesc->video_start;
		video_ram.end   = mdesc->video_end;
		request_resource(&iomem_resource, &video_ram);
	}*/

	/*
	 * Some machines don't have the possibility of ever
	 * possessing lp1 or lp2
	 */
	if (0)  /* FIXME - need to do this for A5k at least */
		request_resource(&ioport_resource, &lp0);
}

/*
 *  Tag parsing.
 *
 * This is the new way of passing data to the kernel at boot time.  Rather
 * than passing a fixed inflexible structure to the kernel, we pass a list
 * of variable-sized tags to the kernel.  The first tag must be a ATAG_CORE
 * tag for the list to be recognised (to distinguish the tagged list from
 * a param_struct).  The list is terminated with a zero-length tag (this tag
 * is not parsed in any way).
 */
static int __init parse_tag_core(const struct tag *tag)
{
	if (tag->hdr.size > 2) {
		if ((tag->u.core.flags & 1) == 0)
			root_mountflags &= ~MS_RDONLY;
		ROOT_DEV = old_decode_dev(tag->u.core.rootdev);
	}
	return 0;
}

__tagtable(ATAG_CORE, parse_tag_core);

static int __init parse_tag_mem32(const struct tag *tag)
{
	if (meminfo.nr_banks >= NR_BANKS) {
		printk(KERN_WARNING
		       "Ignoring memory bank 0x%08x size %dKB\n",
			tag->u.mem.start, tag->u.mem.size / 1024);
		return -EINVAL;
	}
	meminfo.bank[meminfo.nr_banks].start = tag->u.mem.start;
	meminfo.bank[meminfo.nr_banks].size  = tag->u.mem.size;
	meminfo.bank[meminfo.nr_banks].node  = PHYS_TO_NID(tag->u.mem.start);
	meminfo.nr_banks += 1;

	return 0;
}

__tagtable(ATAG_MEM, parse_tag_mem32);

#if defined(CONFIG_DUMMY_CONSOLE)
struct screen_info screen_info = {
 .orig_video_lines	= 30,
 .orig_video_cols	= 80,
 .orig_video_mode	= 0,
 .orig_video_ega_bx	= 0,
 .orig_video_isVGA	= 1,
 .orig_video_points	= 8
};

static int __init parse_tag_videotext(const struct tag *tag)
{
	screen_info.orig_x            = tag->u.videotext.x;
	screen_info.orig_y            = tag->u.videotext.y;
	screen_info.orig_video_page   = tag->u.videotext.video_page;
	screen_info.orig_video_mode   = tag->u.videotext.video_mode;
	screen_info.orig_video_cols   = tag->u.videotext.video_cols;
	screen_info.orig_video_ega_bx = tag->u.videotext.video_ega_bx;
	screen_info.orig_video_lines  = tag->u.videotext.video_lines;
	screen_info.orig_video_isVGA  = tag->u.videotext.video_isvga;
	screen_info.orig_video_points = tag->u.videotext.video_points;
	return 0;
}

__tagtable(ATAG_VIDEOTEXT, parse_tag_videotext);
#endif

static int __init parse_tag_acorn(const struct tag *tag)
{
        memc_ctrl_reg = tag->u.acorn.memc_control_reg;
        number_mfm_drives = tag->u.acorn.adfsdrives;
        return 0;
}

__tagtable(ATAG_ACORN, parse_tag_acorn);

static int __init parse_tag_ramdisk(const struct tag *tag)
{
	setup_ramdisk((tag->u.ramdisk.flags & 1) == 0,
		      (tag->u.ramdisk.flags & 2) == 0,
		      tag->u.ramdisk.start, tag->u.ramdisk.size);
	return 0;
}

__tagtable(ATAG_RAMDISK, parse_tag_ramdisk);

static int __init parse_tag_initrd(const struct tag *tag)
{
	printk(KERN_WARNING "ATAG_INITRD is deprecated; please update your bootloader. \n");
        phys_initrd_start = (unsigned long)tag->u.initrd.start;
        phys_initrd_size = (unsigned long)tag->u.initrd.size;
	return 0;
}

__tagtable(ATAG_INITRD, parse_tag_initrd);

static int __init parse_tag_initrd2(const struct tag *tag)
{
	printk(KERN_WARNING "ATAG_INITRD is deprecated; please update your bootloader. \n");
	phys_initrd_start = (unsigned long)tag->u.initrd.start;
	phys_initrd_size = (unsigned long)tag->u.initrd.size;
	return 0;
}

__tagtable(ATAG_INITRD2, parse_tag_initrd2);

static int __init parse_tag_serialnr(const struct tag *tag)
{
	system_serial_low = tag->u.serialnr.low;
	system_serial_high = tag->u.serialnr.high;
	return 0;
}

__tagtable(ATAG_SERIAL, parse_tag_serialnr);

static int __init parse_tag_revision(const struct tag *tag)
{
	system_rev = tag->u.revision.rev;
	return 0;
}

__tagtable(ATAG_REVISION, parse_tag_revision);

static int __init parse_tag_cmdline(const struct tag *tag)
{
	strncpy(default_command_line, tag->u.cmdline.cmdline, COMMAND_LINE_SIZE);
	default_command_line[COMMAND_LINE_SIZE - 1] = '\0';
	return 0;
}

__tagtable(ATAG_CMDLINE, parse_tag_cmdline);

/*
 * Scan the tag table for this tag, and call its parse function.
 * The tag table is built by the linker from all the __tagtable
 * declarations.
 */
static int __init parse_tag(const struct tag *tag)
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
 * Parse all tags in the list, checking both the global and architecture
 * specific tag tables.
 */
static void __init parse_tags(const struct tag *t)
{
	for (; t->hdr.size; t = tag_next(t))
		if (!parse_tag(t))
			printk(KERN_WARNING
				"Ignoring unrecognised tag 0x%08x\n",
				t->hdr.tag);
}

/*
 * This holds our defaults.
 */
static struct init_tags {
	struct tag_header hdr1;
	struct tag_core   core;
	struct tag_header hdr2;
	struct tag_mem32  mem;
	struct tag_header hdr3;
} init_tags __initdata = {
	{ tag_size(tag_core), ATAG_CORE },
	{ 1, PAGE_SIZE, 0xff },
	{ tag_size(tag_mem32), ATAG_MEM },
	{ MEM_SIZE, PHYS_OFFSET },
	{ 0, ATAG_NONE }
};

void __init setup_arch(char **cmdline_p)
{
	struct tag *tags = (struct tag *)&init_tags;
	char *from = default_command_line;

	setup_processor();
	if(machine_arch_type == MACH_TYPE_A5K)
		machine_name = "A5000";
	else if(machine_arch_type == MACH_TYPE_ARCHIMEDES)
		machine_name = "Archimedes";
	else
		machine_name = "UNKNOWN";

	//FIXME - the tag struct is always copied here but this is a block
	// of RAM that is accidentally reserved along with video RAM. perhaps
	// it would be a good idea to explicitly reserve this?

	tags = (struct tag *)0x0207c000;

	/*
	 * If we have the old style parameters, convert them to
	 * a tag list.
	 */
	if (tags->hdr.tag != ATAG_CORE)
		convert_to_tag_list(tags);
	if (tags->hdr.tag != ATAG_CORE)
		tags = (struct tag *)&init_tags;
	if (tags->hdr.tag == ATAG_CORE) {
		if (meminfo.nr_banks != 0)
			squash_mem_tags(tags);
		parse_tags(tags);
	}

	init_mm.start_code = (unsigned long) &_text;
#ifndef CONFIG_XIP_KERNEL
	init_mm.end_code   = (unsigned long) &_etext;
#else
	init_mm.end_code   = (unsigned long) &_endtext;
	init_mm.start_data   = (unsigned long) &_sdata;
#endif
	init_mm.end_data   = (unsigned long) &_edata;
	init_mm.brk	   = (unsigned long) &_end;

	memcpy(saved_command_line, from, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';
	parse_cmdline(&meminfo, cmdline_p, from);
	bootmem_init(&meminfo);
	paging_init(&meminfo);
	request_standard_resources(&meminfo);

#ifdef CONFIG_VT
#if defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
}

static const char *hwcap_str[] = {
	"swp",
	"half",
	"thumb",
	"26bit",
	"fastmult",
	"fpa",
	"vfp",
	"edsp",
	NULL
};

static int c_show(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "Processor\t: %s %s rev %d (%s)\n",
		   proc_info.manufacturer, proc_info.cpu_name,
		   (int)processor_id & 15, elf_platform);

	seq_printf(m, "BogoMIPS\t: %lu.%02lu\n",
		   loops_per_jiffy / (500000/HZ),
		   (loops_per_jiffy / (5000/HZ)) % 100);

	/* dump out the processor features */
	seq_puts(m, "Features\t: ");

	for (i = 0; hwcap_str[i]; i++)
		if (elf_hwcap & (1 << i))
			seq_printf(m, "%s ", hwcap_str[i]);

	seq_puts(m, "\n");

	seq_printf(m, "CPU part\t\t: %07x\n", processor_id >> 4);
	seq_printf(m, "CPU revision\t: %d\n\n", processor_id & 15);
	seq_printf(m, "Hardware\t: %s\n", machine_name);
	seq_printf(m, "Revision\t: %04x\n", system_rev);
	seq_printf(m, "Serial\t\t: %08x%08x\n",
		   system_serial_high, system_serial_low);

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void c_stop(struct seq_file *m, void *v)
{
}

struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_show
};
