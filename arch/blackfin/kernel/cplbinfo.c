/*
 * arch/blackfin/kernel/cplbinfo.c - display CPLB status
 *
 * Copyright 2004-2008 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include <asm/cplbinit.h>
#include <asm/blackfin.h>

typedef enum { ICPLB, DCPLB } cplb_type;

static char page_strtbl[][3] = { "1K", "4K", "1M", "4M" };
#define page(flags)    (((flags) & 0x30000) >> 16)
#define strpage(flags) page_strtbl[page(flags)]

#ifdef CONFIG_MPU

static char *cplb_print_entry(char *buf, cplb_type type, unsigned int cpu)
{
	struct cplb_entry *tbl;
	int switched;
	int i;

	if (type == ICPLB) {
		tbl = icplb_tbl[cpu];
		switched = first_switched_icplb;
	} else {
		tbl = dcplb_tbl[cpu];
		switched = first_switched_dcplb;
	}

	buf += sprintf(buf, "Index\tAddress\t\tData\tSize\tU/RD\tU/WR\tS/WR\tSwitch\n");
	for (i = 0; i < MAX_CPLBS; ++i) {
		unsigned long data = tbl[i].data;
		unsigned long addr = tbl[i].addr;

		if (!(data & CPLB_VALID))
			continue;

		buf += sprintf(buf,
			"%d\t0x%08lx\t%05lx\t%s\t%c\t%c\t%c\t%c\n",
			i, addr, data, strpage(data),
			(data & CPLB_USER_RD) ? 'Y' : 'N',
			(data & CPLB_USER_WR) ? 'Y' : 'N',
			(data & CPLB_SUPV_WR) ? 'Y' : 'N',
			i < switched ? 'N' : 'Y');
	}
	buf += sprintf(buf, "\n");

	return buf;
}

#else

extern int page_size_table[];

static int cplb_find_entry(unsigned long *cplb_addr,
			   unsigned long *cplb_data, unsigned long addr,
			   unsigned long data)
{
	int i;

	for (i = 0; i < 16; ++i)
		if (addr >= cplb_addr[i] &&
		    addr < cplb_addr[i] + page_size_table[page(cplb_data[i])] &&
		    cplb_data[i] == data)
			return i;

	return -1;
}

static char *cplb_print_entry(char *buf, cplb_type type, unsigned int cpu)
{
	unsigned long *p_addr, *p_data, *p_icount, *p_ocount;
	unsigned long *cplb_addr, *cplb_data;
	int entry = 0, used_cplb = 0;

	if (type == ICPLB) {
		p_addr = ipdt_tables[cpu];
		p_data = ipdt_tables[cpu] + 1;
		p_icount = ipdt_swapcount_tables[cpu];
		p_ocount = ipdt_swapcount_tables[cpu] + 1;
		cplb_addr = (unsigned long *)ICPLB_ADDR0;
		cplb_data = (unsigned long *)ICPLB_DATA0;
	} else {
		p_addr = dpdt_tables[cpu];
		p_data = dpdt_tables[cpu] + 1;
		p_icount = dpdt_swapcount_tables[cpu];
		p_ocount = dpdt_swapcount_tables[cpu] + 1;
		cplb_addr = (unsigned long *)DCPLB_ADDR0;
		cplb_data = (unsigned long *)DCPLB_DATA0;
	}

	buf += sprintf(buf, "Address\t\tData\tSize\tValid\tLocked\tSwapin\tiCount\toCount\n");

	while (*p_addr != 0xffffffff) {
		entry = cplb_find_entry(cplb_addr, cplb_data, *p_addr, *p_data);
		if (entry >= 0)
			used_cplb |= 1 << entry;

		buf += sprintf(buf,
			"0x%08lx\t0x%05lx\t%s\t%c\t%c\t%2d\t%ld\t%ld\n",
			*p_addr, *p_data, strpage(*p_data),
			(*p_data & CPLB_VALID) ? 'Y' : 'N',
			(*p_data & CPLB_LOCK) ? 'Y' : 'N', entry, *p_icount,
			*p_ocount);

		p_addr += 2;
		p_data += 2;
		p_icount += 2;
		p_ocount += 2;
	}

	if (used_cplb != 0xffff) {
		buf += sprintf(buf, "Unused/mismatched CPLBs:\n");

		for (entry = 0; entry < 16; ++entry)
			if (0 == ((1 << entry) & used_cplb)) {
				int flags = cplb_data[entry];
				buf += sprintf(buf,
					"%2d: 0x%08lx\t0x%05x\t%s\t%c\t%c\n",
					entry, cplb_addr[entry], flags, strpage(flags),
					(flags & CPLB_VALID) ? 'Y' : 'N',
					(flags & CPLB_LOCK) ? 'Y' : 'N');
			}
	}

	buf += sprintf(buf, "\n");

	return buf;
}

#endif

static int cplbinfo_proc_output(char *buf, void *data)
{
	unsigned int cpu = (unsigned int)data;
	char *p = buf;

	if (bfin_read_IMEM_CONTROL() & ENICPLB) {
		p += sprintf(p, "Instruction CPLB entry:\n");
		p = cplb_print_entry(p, ICPLB, cpu);
	} else
		p += sprintf(p, "Instruction CPLB is disabled.\n\n");

	if (bfin_read_DMEM_CONTROL() & ENDCPLB) {
		p += sprintf(p, "Data CPLB entry:\n");
		p = cplb_print_entry(p, DCPLB, cpu);
	} else
		p += sprintf(p, "Data CPLB is disabled.\n\n");

	return p - buf;
}

static int cplbinfo_read_proc(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	int len = cplbinfo_proc_output(page, data);
	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	len -= off;
	return max(min(len, count), 0);
}

static int __init cplbinfo_init(void)
{
	struct proc_dir_entry *parent, *entry;
	unsigned int cpu;
	unsigned char str[10];

	parent = proc_mkdir("cplbinfo", NULL);
	if (!parent)
		return -ENOMEM;

	for_each_online_cpu(cpu) {
		sprintf(str, "cpu%u", cpu);
		entry = create_proc_entry(str, 0, parent);
		if (!entry)
			return -ENOMEM;

		entry->read_proc = cplbinfo_read_proc;
		entry->data = (void *)cpu;
	}

	return 0;
}
late_initcall(cplbinfo_init);
