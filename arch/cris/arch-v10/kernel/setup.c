// SPDX-License-Identifier: GPL-2.0
/*
 *
 *  linux/arch/cris/arch-v10/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (c) 2001-2002  Axis Communications AB
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/param.h>
#include <arch/system.h>

#ifdef CONFIG_PROC_FS
#define HAS_FPU		0x0001
#define HAS_MMU		0x0002
#define HAS_ETHERNET100	0x0004
#define HAS_TOKENRING	0x0008
#define HAS_SCSI	0x0010
#define HAS_ATA		0x0020
#define HAS_USB		0x0040
#define HAS_IRQ_BUG	0x0080
#define HAS_MMU_BUG	0x0100

static struct cpu_info {
	char *model;
	unsigned short cache;
	unsigned short flags;
} cpu_info[] = {
	/* The first four models will never ever run this code and are
	   only here for display.  */
	{ "ETRAX 1",         0, 0 },
	{ "ETRAX 2",         0, 0 },
	{ "ETRAX 3",         0, HAS_TOKENRING },
	{ "ETRAX 4",         0, HAS_TOKENRING | HAS_SCSI },
	{ "Unknown",         0, 0 },
	{ "Unknown",         0, 0 },
	{ "Unknown",         0, 0 },
	{ "Simulator",       8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA },
	{ "ETRAX 100",       8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA | HAS_IRQ_BUG },
	{ "ETRAX 100",       8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA },
	{ "ETRAX 100LX",     8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA | HAS_USB | HAS_MMU | HAS_MMU_BUG },
	{ "ETRAX 100LX v2",  8, HAS_ETHERNET100 | HAS_SCSI | HAS_ATA | HAS_USB | HAS_MMU  },
	{ "Unknown",         0, 0 }  /* This entry MUST be the last */
};

int show_cpuinfo(struct seq_file *m, void *v)
{
	unsigned long revision;
	struct cpu_info *info;

	/* read the version register in the CPU and print some stuff */

	revision = rdvr();

	if (revision >= ARRAY_SIZE(cpu_info))
		info = &cpu_info[ARRAY_SIZE(cpu_info) - 1];
	else
		info = &cpu_info[revision];

	seq_printf(m,
		   "processor\t: 0\n"
		   "cpu\t\t: CRIS\n"
		   "cpu revision\t: %lu\n"
		   "cpu model\t: %s\n"
		   "cache size\t: %d kB\n"
		   "fpu\t\t: %s\n"
		   "mmu\t\t: %s\n"
		   "mmu DMA bug\t: %s\n"
		   "ethernet\t: %s Mbps\n"
		   "token ring\t: %s\n"
		   "scsi\t\t: %s\n"
		   "ata\t\t: %s\n"
		   "usb\t\t: %s\n"
		   "bogomips\t: %lu.%02lu\n",

		   revision,
		   info->model,
		   info->cache,
		   info->flags & HAS_FPU ? "yes" : "no",
		   info->flags & HAS_MMU ? "yes" : "no",
		   info->flags & HAS_MMU_BUG ? "yes" : "no",
		   info->flags & HAS_ETHERNET100 ? "10/100" : "10",
		   info->flags & HAS_TOKENRING ? "4/16 Mbps" : "no",
		   info->flags & HAS_SCSI ? "yes" : "no",
		   info->flags & HAS_ATA ? "yes" : "no",
		   info->flags & HAS_USB ? "yes" : "no",
		   (loops_per_jiffy * HZ + 500) / 500000,
		   ((loops_per_jiffy * HZ + 500) / 5000) % 100);

	return 0;
}

#endif /* CONFIG_PROC_FS */

void
show_etrax_copyright(void)
{
	printk(KERN_INFO
               "Linux/CRIS port on ETRAX 100LX (c) 2001 Axis Communications AB\n");
}
