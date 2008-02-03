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
#include <linux/uaccess.h>

#include <asm/current.h>
#include <asm/system.h>
#include <asm/cplb.h>
#include <asm/cplbinit.h>
#include <asm/blackfin.h>

#define CPLB_I 1
#define CPLB_D 2

#define SYNC_SYS    SSYNC()
#define SYNC_CORE   CSYNC()

#define CPLB_BIT_PAGESIZE 0x30000

static char page_size_string_table[][4] = { "1K", "4K", "1M", "4M" };

static char *cplb_print_entry(char *buf, struct cplb_entry *tbl, int switched)
{
	int i;
	buf += sprintf(buf, "Index\tAddress\t\tData\tSize\tU/RD\tU/WR\tS/WR\tSwitch\n");
	for (i = 0; i < MAX_CPLBS; i++) {
		unsigned long data = tbl[i].data;
		unsigned long addr = tbl[i].addr;
		if (!(data & CPLB_VALID))
			continue;

		buf +=
		    sprintf(buf,
			    "%d\t0x%08lx\t%06lx\t%s\t%c\t%c\t%c\t%c\n",
			    i, addr, data,
			    page_size_string_table[(data & 0x30000) >> 16],
			    (data & CPLB_USER_RD) ? 'Y' : 'N',
			    (data & CPLB_USER_WR) ? 'Y' : 'N',
			    (data & CPLB_SUPV_WR) ? 'Y' : 'N',
			    i < switched ? 'N' : 'Y');
	}
	buf += sprintf(buf, "\n");

	return buf;
}

int cplbinfo_proc_output(char *buf)
{
	char *p;

	p = buf;

	p += sprintf(p, "------------------ CPLB Information ------------------\n\n");

	if (bfin_read_IMEM_CONTROL() & ENICPLB) {
		p += sprintf(p, "Instruction CPLB entry:\n");
		p = cplb_print_entry(p, icplb_tbl, first_switched_icplb);
	} else
		p += sprintf(p, "Instruction CPLB is disabled.\n\n");

	if (1 || bfin_read_DMEM_CONTROL() & ENDCPLB) {
		p += sprintf(p, "Data CPLB entry:\n");
		p = cplb_print_entry(p, dcplb_tbl, first_switched_dcplb);
	} else
		p += sprintf(p, "Data CPLB is disabled.\n");

	p += sprintf(p, "ICPLB miss: %d\nICPLB supervisor miss: %d\n",
		     nr_icplb_miss, nr_icplb_supv_miss);
	p += sprintf(p, "DCPLB miss: %d\nDCPLB protection fault:%d\n",
		     nr_dcplb_miss, nr_dcplb_prot);
	p += sprintf(p, "CPLB flushes: %d\n",
		     nr_cplb_flush);

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

static int __init cplbinfo_init(void)
{
	struct proc_dir_entry *entry;

	entry = create_proc_entry("cplbinfo", 0, NULL);
	if (!entry)
		return -ENOMEM;

	entry->read_proc = cplbinfo_read_proc;
	entry->data = NULL;

	return 0;
}

static void __exit cplbinfo_exit(void)
{
	remove_proc_entry("cplbinfo", NULL);
}

module_init(cplbinfo_init);
module_exit(cplbinfo_exit);
