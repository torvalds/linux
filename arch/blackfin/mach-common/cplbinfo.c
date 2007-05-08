/*
 * File:         arch/blackfin/mach-common/cplbinfo.c
 * Based on:
 * Author:       Sonic Zhang <sonic.zhang@analog.com>
 *
 * Created:      Jan. 2005
 * Description:  Display CPLB status
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

#include <asm/current.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#include <asm/cplb.h>
#include <asm/blackfin.h>

#define CPLB_I 1
#define CPLB_D 2

#define SYNC_SYS    SSYNC()
#define SYNC_CORE   CSYNC()

#define CPLB_BIT_PAGESIZE 0x30000

static int page_size_table[4] = {
	0x00000400,		/* 1K */
	0x00001000,		/* 4K */
	0x00100000,		/* 1M */
	0x00400000		/* 4M */
};

static char page_size_string_table[][4] = { "1K", "4K", "1M", "4M" };

static int cplb_find_entry(unsigned long *cplb_addr,
			   unsigned long *cplb_data, unsigned long addr,
			   unsigned long data)
{
	int ii;

	for (ii = 0; ii < 16; ii++)
		if (addr >= cplb_addr[ii] && addr < cplb_addr[ii] +
		    page_size_table[(cplb_data[ii] & CPLB_BIT_PAGESIZE) >> 16]
			&& (cplb_data[ii] == data))
			return ii;

	return -1;
}

static char *cplb_print_entry(char *buf, int type)
{
	unsigned long *p_addr = dpdt_table;
	unsigned long *p_data = dpdt_table + 1;
	unsigned long *p_icount = dpdt_swapcount_table;
	unsigned long *p_ocount = dpdt_swapcount_table + 1;
	unsigned long *cplb_addr = (unsigned long *)DCPLB_ADDR0;
	unsigned long *cplb_data = (unsigned long *)DCPLB_DATA0;
	int entry = 0, used_cplb = 0;

	if (type == CPLB_I) {
		buf += sprintf(buf, "Instrction CPLB entry:\n");
		p_addr = ipdt_table;
		p_data = ipdt_table + 1;
		p_icount = ipdt_swapcount_table;
		p_ocount = ipdt_swapcount_table + 1;
		cplb_addr = (unsigned long *)ICPLB_ADDR0;
		cplb_data = (unsigned long *)ICPLB_DATA0;
	} else
		buf += sprintf(buf, "Data CPLB entry:\n");

	buf += sprintf(buf, "Address\t\tData\tSize\tValid\tLocked\tSwapin\
\tiCount\toCount\n");

	while (*p_addr != 0xffffffff) {
		entry = cplb_find_entry(cplb_addr, cplb_data, *p_addr, *p_data);
		if (entry >= 0)
			used_cplb |= 1 << entry;

		buf +=
		    sprintf(buf,
			    "0x%08lx\t0x%05lx\t%s\t%c\t%c\t%2d\t%ld\t%ld\n",
			    *p_addr, *p_data,
			    page_size_string_table[(*p_data & 0x30000) >> 16],
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

		for (entry = 0; entry < 16; entry++)
			if (0 == ((1 << entry) & used_cplb)) {
				int flags = cplb_data[entry];
				buf +=
				    sprintf(buf,
					    "%2d: 0x%08lx\t0x%05x\t%s\t%c\t%c\n",
					    entry, cplb_addr[entry], flags,
					    page_size_string_table[(flags &
								    0x30000) >>
								   16],
					    (flags & CPLB_VALID) ? 'Y' : 'N',
					    (flags & CPLB_LOCK) ? 'Y' : 'N');
			}
	}

	buf += sprintf(buf, "\n");

	return buf;
}

static int cplbinfo_proc_output(char *buf)
{
	char *p;

	p = buf;

	p += sprintf(p,
		     "------------------ CPLB Information ------------------\n\n");

	if (bfin_read_IMEM_CONTROL() & ENICPLB)
		p = cplb_print_entry(p, CPLB_I);
	else
		p += sprintf(p, "Instruction CPLB is disabled.\n\n");

	if (bfin_read_DMEM_CONTROL() & ENDCPLB)
		p = cplb_print_entry(p, CPLB_D);
	else
		p += sprintf(p, "Data CPLB is disabled.\n");

	return p - buf;
}

static int cplbinfo_read_proc(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	int len;

	len = cplbinfo_proc_output(page);
	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

static int cplbinfo_write_proc(struct file *file, const char __user *buffer,
			       unsigned long count, void *data)
{
	printk(KERN_INFO "Reset the CPLB swap in/out counts.\n");
	memset(ipdt_swapcount_table, 0, MAX_SWITCH_I_CPLBS * sizeof(unsigned long));
	memset(dpdt_swapcount_table, 0, MAX_SWITCH_D_CPLBS * sizeof(unsigned long));

	return count;
}

static int __init cplbinfo_init(void)
{
	struct proc_dir_entry *entry;

	if ((entry = create_proc_entry("cplbinfo", 0, NULL)) == NULL) {
		return -ENOMEM;
	}

	entry->read_proc = cplbinfo_read_proc;
	entry->write_proc = cplbinfo_write_proc;
	entry->data = NULL;

	return 0;
}

static void __exit cplbinfo_exit(void)
{
	remove_proc_entry("cplbinfo", NULL);
}

module_init(cplbinfo_init);
module_exit(cplbinfo_exit);
