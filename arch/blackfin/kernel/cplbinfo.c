/*
 * arch/blackfin/kernel/cplbinfo.c - display CPLB status
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#include <asm/cplbinit.h>
#include <asm/blackfin.h>

static char const page_strtbl[][4] = {
	"1K", "4K", "1M", "4M",
#ifdef CONFIG_BF60x
	"16K", "64K", "16M", "64M",
#endif
};
#define page(flags)    (((flags) & 0x70000) >> 16)
#define strpage(flags) page_strtbl[page(flags)]

struct cplbinfo_data {
	loff_t pos;
	char cplb_type;
	u32 mem_control;
	struct cplb_entry *tbl;
	int switched;
};

static void cplbinfo_print_header(struct seq_file *m)
{
	seq_printf(m, "Index\tAddress\t\tData\tSize\tU/RD\tU/WR\tS/WR\tSwitch\n");
}

static int cplbinfo_nomore(struct cplbinfo_data *cdata)
{
	return cdata->pos >= MAX_CPLBS;
}

static int cplbinfo_show(struct seq_file *m, void *p)
{
	struct cplbinfo_data *cdata;
	unsigned long data, addr;
	loff_t pos;

	cdata = p;
	pos = cdata->pos;
	addr = cdata->tbl[pos].addr;
	data = cdata->tbl[pos].data;

	seq_printf(m,
		"%d\t0x%08lx\t%05lx\t%s\t%c\t%c\t%c\t%c\n",
		(int)pos, addr, data, strpage(data),
		(data & CPLB_USER_RD) ? 'Y' : 'N',
		(data & CPLB_USER_WR) ? 'Y' : 'N',
		(data & CPLB_SUPV_WR) ? 'Y' : 'N',
		pos < cdata->switched ? 'N' : 'Y');

	return 0;
}

static void cplbinfo_seq_init(struct cplbinfo_data *cdata, unsigned int cpu)
{
	if (cdata->cplb_type == 'I') {
		cdata->mem_control = bfin_read_IMEM_CONTROL();
		cdata->tbl = icplb_tbl[cpu];
		cdata->switched = first_switched_icplb;
	} else {
		cdata->mem_control = bfin_read_DMEM_CONTROL();
		cdata->tbl = dcplb_tbl[cpu];
		cdata->switched = first_switched_dcplb;
	}
}

static void *cplbinfo_start(struct seq_file *m, loff_t *pos)
{
	struct cplbinfo_data *cdata = m->private;

	if (!*pos) {
		seq_printf(m, "%cCPLBs are %sabled: 0x%x\n", cdata->cplb_type,
			(cdata->mem_control & ENDCPLB ? "en" : "dis"),
			cdata->mem_control);
		cplbinfo_print_header(m);
	} else if (cplbinfo_nomore(cdata))
		return NULL;

	get_cpu();
	return cdata;
}

static void *cplbinfo_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct cplbinfo_data *cdata = p;
	cdata->pos = ++(*pos);
	if (cplbinfo_nomore(cdata))
		return NULL;
	else
		return cdata;
}

static void cplbinfo_stop(struct seq_file *m, void *p)
{
	put_cpu();
}

static const struct seq_operations cplbinfo_sops = {
	.start = cplbinfo_start,
	.next  = cplbinfo_next,
	.stop  = cplbinfo_stop,
	.show  = cplbinfo_show,
};

#define CPLBINFO_DCPLB_FLAG 0x80000000

static int cplbinfo_open(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *pde = PDE(file_inode(file));
	char cplb_type;
	unsigned int cpu;
	int ret;
	struct seq_file *m;
	struct cplbinfo_data *cdata;

	cpu = (unsigned int)pde->data;
	cplb_type = cpu & CPLBINFO_DCPLB_FLAG ? 'D' : 'I';
	cpu &= ~CPLBINFO_DCPLB_FLAG;

	if (!cpu_online(cpu))
		return -ENODEV;

	ret = seq_open_private(file, &cplbinfo_sops, sizeof(*cdata));
	if (ret)
		return ret;
	m = file->private_data;
	cdata = m->private;

	cdata->pos = 0;
	cdata->cplb_type = cplb_type;
	cplbinfo_seq_init(cdata, cpu);

	return 0;
}

static const struct file_operations cplbinfo_fops = {
	.open    = cplbinfo_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private,
};

static int __init cplbinfo_init(void)
{
	struct proc_dir_entry *cplb_dir, *cpu_dir;
	char buf[10];
	unsigned int cpu;

	cplb_dir = proc_mkdir("cplbinfo", NULL);
	if (!cplb_dir)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		sprintf(buf, "cpu%i", cpu);
		cpu_dir = proc_mkdir(buf, cplb_dir);
		if (!cpu_dir)
			return -ENOMEM;

		proc_create_data("icplb", S_IRUGO, cpu_dir, &cplbinfo_fops,
			(void *)cpu);
		proc_create_data("dcplb", S_IRUGO, cpu_dir, &cplbinfo_fops,
			(void *)(cpu | CPLBINFO_DCPLB_FLAG));
	}

	return 0;
}
late_initcall(cplbinfo_init);
