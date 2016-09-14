/*
 * Implementation of s390 diagnose codes
 *
 * Copyright IBM Corp. 2007
 * Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <asm/diag.h>
#include <asm/trace/diag.h>

struct diag_stat {
	unsigned int counter[NR_DIAG_STAT];
};

static DEFINE_PER_CPU(struct diag_stat, diag_stat);

struct diag_desc {
	int code;
	char *name;
};

static const struct diag_desc diag_map[NR_DIAG_STAT] = {
	[DIAG_STAT_X008] = { .code = 0x008, .name = "Console Function" },
	[DIAG_STAT_X00C] = { .code = 0x00c, .name = "Pseudo Timer" },
	[DIAG_STAT_X010] = { .code = 0x010, .name = "Release Pages" },
	[DIAG_STAT_X014] = { .code = 0x014, .name = "Spool File Services" },
	[DIAG_STAT_X044] = { .code = 0x044, .name = "Voluntary Timeslice End" },
	[DIAG_STAT_X064] = { .code = 0x064, .name = "NSS Manipulation" },
	[DIAG_STAT_X09C] = { .code = 0x09c, .name = "Relinquish Timeslice" },
	[DIAG_STAT_X0DC] = { .code = 0x0dc, .name = "Appldata Control" },
	[DIAG_STAT_X204] = { .code = 0x204, .name = "Logical-CPU Utilization" },
	[DIAG_STAT_X210] = { .code = 0x210, .name = "Device Information" },
	[DIAG_STAT_X224] = { .code = 0x224, .name = "EBCDIC-Name Table" },
	[DIAG_STAT_X250] = { .code = 0x250, .name = "Block I/O" },
	[DIAG_STAT_X258] = { .code = 0x258, .name = "Page-Reference Services" },
	[DIAG_STAT_X288] = { .code = 0x288, .name = "Time Bomb" },
	[DIAG_STAT_X2C4] = { .code = 0x2c4, .name = "FTP Services" },
	[DIAG_STAT_X2FC] = { .code = 0x2fc, .name = "Guest Performance Data" },
	[DIAG_STAT_X304] = { .code = 0x304, .name = "Partition-Resource Service" },
	[DIAG_STAT_X308] = { .code = 0x308, .name = "List-Directed IPL" },
	[DIAG_STAT_X500] = { .code = 0x500, .name = "Virtio Service" },
};

static int show_diag_stat(struct seq_file *m, void *v)
{
	struct diag_stat *stat;
	unsigned long n = (unsigned long) v - 1;
	int cpu, prec, tmp;

	get_online_cpus();
	if (n == 0) {
		seq_puts(m, "         ");

		for_each_online_cpu(cpu) {
			prec = 10;
			for (tmp = 10; cpu >= tmp; tmp *= 10)
				prec--;
			seq_printf(m, "%*s%d", prec, "CPU", cpu);
		}
		seq_putc(m, '\n');
	} else if (n <= NR_DIAG_STAT) {
		seq_printf(m, "diag %03x:", diag_map[n-1].code);
		for_each_online_cpu(cpu) {
			stat = &per_cpu(diag_stat, cpu);
			seq_printf(m, " %10u", stat->counter[n-1]);
		}
		seq_printf(m, "    %s\n", diag_map[n-1].name);
	}
	put_online_cpus();
	return 0;
}

static void *show_diag_stat_start(struct seq_file *m, loff_t *pos)
{
	return *pos <= nr_cpu_ids ? (void *)((unsigned long) *pos + 1) : NULL;
}

static void *show_diag_stat_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return show_diag_stat_start(m, pos);
}

static void show_diag_stat_stop(struct seq_file *m, void *v)
{
}

static const struct seq_operations show_diag_stat_sops = {
	.start	= show_diag_stat_start,
	.next	= show_diag_stat_next,
	.stop	= show_diag_stat_stop,
	.show	= show_diag_stat,
};

static int show_diag_stat_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &show_diag_stat_sops);
}

static const struct file_operations show_diag_stat_fops = {
	.open		= show_diag_stat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};


static int __init show_diag_stat_init(void)
{
	debugfs_create_file("diag_stat", 0400, NULL, NULL,
			    &show_diag_stat_fops);
	return 0;
}

device_initcall(show_diag_stat_init);

void diag_stat_inc(enum diag_stat_enum nr)
{
	this_cpu_inc(diag_stat.counter[nr]);
	trace_s390_diagnose(diag_map[nr].code);
}
EXPORT_SYMBOL(diag_stat_inc);

void diag_stat_inc_norecursion(enum diag_stat_enum nr)
{
	this_cpu_inc(diag_stat.counter[nr]);
	trace_s390_diagnose_norecursion(diag_map[nr].code);
}
EXPORT_SYMBOL(diag_stat_inc_norecursion);

/*
 * Diagnose 14: Input spool file manipulation
 */
static inline int __diag14(unsigned long rx, unsigned long ry1,
			   unsigned long subcode)
{
	register unsigned long _ry1 asm("2") = ry1;
	register unsigned long _ry2 asm("3") = subcode;
	int rc = 0;

	asm volatile(
		"   sam31\n"
		"   diag    %2,2,0x14\n"
		"   sam64\n"
		"   ipm     %0\n"
		"   srl     %0,28\n"
		: "=d" (rc), "+d" (_ry2)
		: "d" (rx), "d" (_ry1)
		: "cc");

	return rc;
}

int diag14(unsigned long rx, unsigned long ry1, unsigned long subcode)
{
	diag_stat_inc(DIAG_STAT_X014);
	return __diag14(rx, ry1, subcode);
}
EXPORT_SYMBOL(diag14);

static inline int __diag204(unsigned long *subcode, unsigned long size, void *addr)
{
	register unsigned long _subcode asm("0") = *subcode;
	register unsigned long _size asm("1") = size;

	asm volatile(
		"	diag	%2,%0,0x204\n"
		"0:	nopr	%%r7\n"
		EX_TABLE(0b,0b)
		: "+d" (_subcode), "+d" (_size) : "d" (addr) : "memory");
	*subcode = _subcode;
	return _size;
}

int diag204(unsigned long subcode, unsigned long size, void *addr)
{
	diag_stat_inc(DIAG_STAT_X204);
	size = __diag204(&subcode, size, addr);
	if (subcode)
		return -1;
	return size;
}
EXPORT_SYMBOL(diag204);

/*
 * Diagnose 210: Get information about a virtual device
 */
int diag210(struct diag210 *addr)
{
	/*
	 * diag 210 needs its data below the 2GB border, so we
	 * use a static data area to be sure
	 */
	static struct diag210 diag210_tmp;
	static DEFINE_SPINLOCK(diag210_lock);
	unsigned long flags;
	int ccode;

	spin_lock_irqsave(&diag210_lock, flags);
	diag210_tmp = *addr;

	diag_stat_inc(DIAG_STAT_X210);
	asm volatile(
		"	lhi	%0,-1\n"
		"	sam31\n"
		"	diag	%1,0,0x210\n"
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"1:	sam64\n"
		EX_TABLE(0b, 1b)
		: "=&d" (ccode) : "a" (&diag210_tmp) : "cc", "memory");

	*addr = diag210_tmp;
	spin_unlock_irqrestore(&diag210_lock, flags);

	return ccode;
}
EXPORT_SYMBOL(diag210);

int diag224(void *ptr)
{
	int rc = -EOPNOTSUPP;

	diag_stat_inc(DIAG_STAT_X224);
	asm volatile(
		"	diag	%1,%2,0x224\n"
		"0:	lhi	%0,0x0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+d" (rc) :"d" (0), "d" (ptr) : "memory");
	return rc;
}
EXPORT_SYMBOL(diag224);
