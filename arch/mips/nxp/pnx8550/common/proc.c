/*
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/random.h>

#include <asm/io.h>
#include <int.h>
#include <uart.h>


static int pnx8550_timers_read(char* page, char** start, off_t offset, int count, int* eof, void* data)
{
        int len = 0;
	int configPR = read_c0_config7();

        if (offset==0) {
		len += sprintf(&page[len], "Timer:       count,  compare, tc, status\n");
                len += sprintf(&page[len], "    1: %11i, %8i,  %1i, %s\n",
			       read_c0_count(), read_c0_compare(),
			      (configPR>>6)&0x1, ((configPR>>3)&0x1)? "off":"on");
                len += sprintf(&page[len], "    2: %11i, %8i,  %1i, %s\n",
			       read_c0_count2(), read_c0_compare2(),
			      (configPR>>7)&0x1, ((configPR>>4)&0x1)? "off":"on");
                len += sprintf(&page[len], "    3: %11i, %8i,  %1i, %s\n",
			       read_c0_count3(), read_c0_compare3(),
			      (configPR>>8)&0x1, ((configPR>>5)&0x1)? "off":"on");
        }

        return len;
}

static int pnx8550_registers_read(char* page, char** start, off_t offset, int count, int* eof, void* data)
{
        int len = 0;

        if (offset==0) {
                len += sprintf(&page[len], "config1:   %#10.8x\n", read_c0_config1());
                len += sprintf(&page[len], "config2:   %#10.8x\n", read_c0_config2());
                len += sprintf(&page[len], "config3:   %#10.8x\n", read_c0_config3());
                len += sprintf(&page[len], "configPR:  %#10.8x\n", read_c0_config7());
                len += sprintf(&page[len], "status:    %#10.8x\n", read_c0_status());
                len += sprintf(&page[len], "cause:     %#10.8x\n", read_c0_cause());
                len += sprintf(&page[len], "count:     %#10.8x\n", read_c0_count());
                len += sprintf(&page[len], "count_2:   %#10.8x\n", read_c0_count2());
                len += sprintf(&page[len], "count_3:   %#10.8x\n", read_c0_count3());
                len += sprintf(&page[len], "compare:   %#10.8x\n", read_c0_compare());
                len += sprintf(&page[len], "compare_2: %#10.8x\n", read_c0_compare2());
                len += sprintf(&page[len], "compare_3: %#10.8x\n", read_c0_compare3());
        }

        return len;
}

static struct proc_dir_entry* pnx8550_dir;
static struct proc_dir_entry* pnx8550_timers;
static struct proc_dir_entry* pnx8550_registers;

static int pnx8550_proc_init( void )
{

	// Create /proc/pnx8550
        pnx8550_dir = proc_mkdir("pnx8550", NULL);
        if (!pnx8550_dir) {
                printk(KERN_ERR "Can't create pnx8550 proc dir\n");
                return -1;
        }

	// Create /proc/pnx8550/timers
        pnx8550_timers = create_proc_read_entry(
		"timers",
		0,
		pnx8550_dir,
		pnx8550_timers_read,
		NULL);

        if (!pnx8550_timers)
                printk(KERN_ERR "Can't create pnx8550 timers proc file\n");

	// Create /proc/pnx8550/registers
        pnx8550_registers = create_proc_read_entry(
		"registers",
		0,
		pnx8550_dir,
		pnx8550_registers_read,
		NULL);

        if (!pnx8550_registers)
                printk(KERN_ERR "Can't create pnx8550 registers proc file\n");

	return 0;
}

__initcall(pnx8550_proc_init);
