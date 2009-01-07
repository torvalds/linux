/*
 * arch/blackfin/kernel/cplbinfo.c - display CPLB status
 *
 * Copyright 2004-2008 Analog Devices Inc.
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

static char const page_strtbl[][3] = { "1K", "4K", "1M", "4M" };
#define page(flags)    (((flags) & 0x30000) >> 16)
#define strpage(flags) page_strtbl[page(flags)]

#ifdef CONFIG_MPU

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

#else

struct cplbinfo_data {
	loff_t pos;
	char cplb_type;
	u32 mem_control;
	unsigned long *pdt_tables, *pdt_swapcount;
	unsigned long cplb_addr, cplb_data;
};

extern int page_size_table[];

static int cplb_find_entry(unsigned long addr_tbl, unsigned long data_tbl,
                           unsigned long addr_find, unsigned long data_find)
{
	int i;

	for (i = 0; i < 16; ++i) {
		unsigned long cplb_addr = bfin_read32(addr_tbl + i * 4);
		unsigned long cplb_data = bfin_read32(data_tbl + i * 4);
		if (addr_find >= cplb_addr &&
		    addr_find < cplb_addr + page_size_table[page(cplb_data)] &&
		    cplb_data == data_find)
			return i;
	}

	return -1;
}

static void cplbinfo_print_header(struct seq_file *m)
{
	seq_printf(m, "Address\t\tData\tSize\tValid\tLocked\tSwapin\tiCount\toCount\n");
}

static int cplbinfo_nomore(struct cplbinfo_data *cdata)
{
	return cdata->pdt_tables[cdata->pos * 2] == 0xffffffff;
}

static int cplbinfo_show(struct seq_file *m, void *p)
{
	struct cplbinfo_data *cdata;
	unsigned long data, addr;
	int entry;
	loff_t pos;

	cdata = p;
	pos = cdata->pos * 2;
	addr = cdata->pdt_tables[pos];
	data = cdata->pdt_tables[pos + 1];
	entry = cplb_find_entry(cdata->cplb_addr, cdata->cplb_data, addr, data);

	seq_printf(m,
		"0x%08lx\t0x%05lx\t%s\t%c\t%c\t%2d\t%ld\t%ld\n",
		addr, data, strpage(data),
		(data & CPLB_VALID) ? 'Y' : 'N',
		(data & CPLB_LOCK) ? 'Y' : 'N', entry,
		cdata->pdt_swapcount[pos],
		cdata->pdt_swapcount[pos + 1]);

	return 0;
}

static void cplbinfo_seq_init(struct cplbinfo_data *cdata, unsigned int cpu)
{
	if (cdata->cplb_type == 'I') {
		cdata->mem_control = bfin_read_IMEM_CONTROL();
		cdata->pdt_tables = ipdt_tables[cpu];
		cdata->pdt_swapcount = ipdt_swapcount_tables[cpu];
		cdata->cplb_addr = ICPLB_ADDR0;
		cdata->cplb_data = ICPLB_DATA0;
	} else {
		cdata->mem_control = bfin_read_DMEM_CONTROL();
		cdata->pdt_tables = dpdt_tables[cpu];
		cdata->pdt_swapcount = dpdt_swapcount_tables[cpu];
		cdata->cplb_addr = DCPLB_ADDR0;
		cdata->cplb_data = DCPLB_DATA0;
	}
}

#endif

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

static int cplbinfo_open(struct inode *inode, struct file *file)
{
	char buf[256], *path, *p;
	unsigned int cpu;
	char *s_cpu, *s_cplb;
	int ret;
	struct seq_file *m;
	struct cplbinfo_data *cdata;

	path = d_path(&file->f_path, buf, sizeof(buf));
	if (IS_ERR(path))
		return PTR_ERR(path);
	s_cpu = strstr(path, "/cpu");
	s_cplb = strrchr(path, '/');
	if (!s_cpu || !s_cplb)
		return -EINVAL;

	cpu = simple_strtoul(s_cpu + 4, &p, 10);
	if (!cpu_online(cpu))
		return -ENODEV;

	ret = seq_open_private(file, &cplbinfo_sops, sizeof(*cdata));
	if (ret)
		return ret;
	m = file->private_data;
	cdata = m->private;

	cdata->pos = 0;
	cdata->cplb_type = toupper(s_cplb[1]);
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

		proc_create("icplb", S_IRUGO, cpu_dir, &cplbinfo_fops);
		proc_create("dcplb", S_IRUGO, cpu_dir, &cplbinfo_fops);
	}

	return 0;
}
late_initcall(cplbinfo_init);
