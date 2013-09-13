/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999,2001-2004, 2006 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * Module to export the system's Firmware Interface Tables, including
 * PROM revision numbers and banners, in /proc
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/nodemask.h>
#include <asm/io.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/addrs.h>

MODULE_DESCRIPTION("PROM version reporting for /proc");
MODULE_AUTHOR("Chad Talbott");
MODULE_LICENSE("GPL");

/* Standard Intel FIT entry types */
#define FIT_ENTRY_FIT_HEADER	0x00	/* FIT header entry */
#define FIT_ENTRY_PAL_B		0x01	/* PAL_B entry */
/* Entries 0x02 through 0x0D reserved by Intel */
#define FIT_ENTRY_PAL_A_PROC	0x0E	/* Processor-specific PAL_A entry */
#define FIT_ENTRY_PAL_A		0x0F	/* PAL_A entry, same as... */
#define FIT_ENTRY_PAL_A_GEN	0x0F	/* ...Generic PAL_A entry */
#define FIT_ENTRY_UNUSED	0x7F	/* Unused (reserved by Intel?) */
/* OEM-defined entries range from 0x10 to 0x7E. */
#define FIT_ENTRY_SAL_A		0x10	/* SAL_A entry */
#define FIT_ENTRY_SAL_B		0x11	/* SAL_B entry */
#define FIT_ENTRY_SALRUNTIME	0x12	/* SAL runtime entry */
#define FIT_ENTRY_EFI		0x1F	/* EFI entry */
#define FIT_ENTRY_FPSWA		0x20	/* embedded fpswa entry */
#define FIT_ENTRY_VMLINUX	0x21	/* embedded vmlinux entry */

#define FIT_MAJOR_SHIFT	(32 + 8)
#define FIT_MAJOR_MASK	((1 << 8) - 1)
#define FIT_MINOR_SHIFT	32
#define FIT_MINOR_MASK	((1 << 8) - 1)

#define FIT_MAJOR(q)	\
	((unsigned) ((q) >> FIT_MAJOR_SHIFT) & FIT_MAJOR_MASK)
#define FIT_MINOR(q)	\
	((unsigned) ((q) >> FIT_MINOR_SHIFT) & FIT_MINOR_MASK)

#define FIT_TYPE_SHIFT	(32 + 16)
#define FIT_TYPE_MASK	((1 << 7) - 1)

#define FIT_TYPE(q)	\
	((unsigned) ((q) >> FIT_TYPE_SHIFT) & FIT_TYPE_MASK)

struct fit_type_map_t {
	unsigned char type;
	const char *name;
};

static const struct fit_type_map_t fit_entry_types[] = {
	{FIT_ENTRY_FIT_HEADER, "FIT Header"},
	{FIT_ENTRY_PAL_A_GEN, "Generic PAL_A"},
	{FIT_ENTRY_PAL_A_PROC, "Processor-specific PAL_A"},
	{FIT_ENTRY_PAL_A, "PAL_A"},
	{FIT_ENTRY_PAL_B, "PAL_B"},
	{FIT_ENTRY_SAL_A, "SAL_A"},
	{FIT_ENTRY_SAL_B, "SAL_B"},
	{FIT_ENTRY_SALRUNTIME, "SAL runtime"},
	{FIT_ENTRY_EFI, "EFI"},
	{FIT_ENTRY_VMLINUX, "Embedded Linux"},
	{FIT_ENTRY_FPSWA, "Embedded FPSWA"},
	{FIT_ENTRY_UNUSED, "Unused"},
	{0xff, "Error"},
};

static const char *fit_type_name(unsigned char type)
{
	struct fit_type_map_t const *mapp;

	for (mapp = fit_entry_types; mapp->type != 0xff; mapp++)
		if (type == mapp->type)
			return mapp->name;

	if ((type > FIT_ENTRY_PAL_A) && (type < FIT_ENTRY_UNUSED))
		return "OEM type";
	if ((type > FIT_ENTRY_PAL_B) && (type < FIT_ENTRY_PAL_A))
		return "Reserved";

	return "Unknown type";
}

static int
get_fit_entry(unsigned long nasid, int index, unsigned long *fentry,
	      char *banner, int banlen)
{
	return ia64_sn_get_fit_compt(nasid, index, fentry, banner, banlen);
}


/*
 * These two routines display the FIT table for each node.
 */
static void dump_fit_entry(struct seq_file *m, unsigned long *fentry)
{
	unsigned type;

	type = FIT_TYPE(fentry[1]);
	seq_printf(m, "%02x %-25s %x.%02x %016lx %u\n",
		   type,
		   fit_type_name(type),
		   FIT_MAJOR(fentry[1]), FIT_MINOR(fentry[1]),
		   fentry[0],
		   /* mult by sixteen to get size in bytes */
		   (unsigned)(fentry[1] & 0xffffff) * 16);
}


/*
 * We assume that the fit table will be small enough that we can print
 * the whole thing into one page.  (This is true for our default 16kB
 * pages -- each entry is about 60 chars wide when printed.)  I read
 * somewhere that the maximum size of the FIT is 128 entries, so we're
 * OK except for 4kB pages (and no one is going to do that on SN
 * anyway).
 */
static int proc_fit_show(struct seq_file *m, void *v)
{
	unsigned long nasid = (unsigned long)m->private;
	unsigned long fentry[2];
	int index;

	for (index=0;;index++) {
		BUG_ON(index * 60 > PAGE_SIZE);
		if (get_fit_entry(nasid, index, fentry, NULL, 0))
			break;
		dump_fit_entry(m, fentry);
	}
	return 0;
}

static int proc_fit_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_fit_show, PDE_DATA(inode));
}

static const struct file_operations proc_fit_fops = {
	.open		= proc_fit_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int proc_version_show(struct seq_file *m, void *v)
{
	unsigned long nasid = (unsigned long)m->private;
	unsigned long fentry[2];
	char banner[128];
	int index;

	for (index = 0; ; index++) {
		if (get_fit_entry(nasid, index, fentry, banner,
				  sizeof(banner)))
			return 0;
		if (FIT_TYPE(fentry[1]) == FIT_ENTRY_SAL_A)
			break;
	}

	seq_printf(m, "%x.%02x\n", FIT_MAJOR(fentry[1]), FIT_MINOR(fentry[1]));

	if (banner[0])
		seq_printf(m, "%s\n", banner);
	return 0;
}

static int proc_version_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_version_show, PDE_DATA(inode));
}

static const struct file_operations proc_version_fops = {
	.open		= proc_version_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* module entry points */
int __init prominfo_init(void);
void __exit prominfo_exit(void);

module_init(prominfo_init);
module_exit(prominfo_exit);

#define NODE_NAME_LEN 11

int __init prominfo_init(void)
{
	struct proc_dir_entry *sgi_prominfo_entry;
	cnodeid_t cnodeid;

	if (!ia64_platform_is("sn2"))
		return 0;

	sgi_prominfo_entry = proc_mkdir("sgi_prominfo", NULL);
	if (!sgi_prominfo_entry)
		return -ENOMEM;

	for_each_online_node(cnodeid) {
		struct proc_dir_entry *dir;
		unsigned long nasid;
		char name[NODE_NAME_LEN];

		sprintf(name, "node%d", cnodeid);
		dir = proc_mkdir(name, sgi_prominfo_entry);
		if (!dir)
			continue;
		nasid = cnodeid_to_nasid(cnodeid);
		proc_create_data("fit", 0, dir,
				 &proc_fit_fops, (void *)nasid);
		proc_create_data("version", 0, dir,
				 &proc_version_fops, (void *)nasid);
	}
	return 0;
}

void __exit prominfo_exit(void)
{
	remove_proc_subtree("sgi_prominfo", NULL);
}
