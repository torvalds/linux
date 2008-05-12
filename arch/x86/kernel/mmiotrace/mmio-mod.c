/*
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2005
 *               Jeff Muizelaar, 2006, 2007
 *               Pekka Paalanen, 2008 <pq@iki.fi>
 *
 * Derived from the read-mod example from relay-examples by Tom Zanussi.
 */
#include <linux/module.h>
#include <linux/relay.h>
#include <linux/debugfs.h>
#include <linux/proc_fs.h>
#include <asm/io.h>
#include <linux/version.h>
#include <linux/kallsyms.h>
#include <asm/pgtable.h>
#include <linux/mmiotrace.h>
#include <asm/e820.h> /* for ISA_START_ADDRESS */

#include "kmmio.h"
#include "pf_in.h"

/* This app's relay channel files will appear in /debug/mmio-trace */
#define APP_DIR		"mmio-trace"
/* the marker injection file in /proc */
#define MARKER_FILE     "mmio-marker"

#define MODULE_NAME     "mmiotrace"

struct trap_reason {
	unsigned long addr;
	unsigned long ip;
	enum reason_type type;
	int active_traces;
};

static struct trap_reason pf_reason[NR_CPUS];
static struct mm_io_header_rw cpu_trace[NR_CPUS];

static struct file_operations mmio_fops = {
	.owner = THIS_MODULE,
};

static const size_t subbuf_size = 256*1024;
static struct rchan *chan;
static struct dentry *dir;
static int suspended;      /* XXX should this be per cpu? */
static struct proc_dir_entry *proc_marker_file;

/* module parameters */
static unsigned int      n_subbufs = 32*4;
static unsigned long filter_offset;
static int                 nommiotrace;
static int               ISA_trace;
static int                trace_pc;

module_param(n_subbufs, uint, 0);
module_param(filter_offset, ulong, 0);
module_param(nommiotrace, bool, 0);
module_param(ISA_trace, bool, 0);
module_param(trace_pc, bool, 0);

MODULE_PARM_DESC(n_subbufs, "Number of 256kB buffers, default 128.");
MODULE_PARM_DESC(filter_offset, "Start address of traced mappings.");
MODULE_PARM_DESC(nommiotrace, "Disable actual MMIO tracing.");
MODULE_PARM_DESC(ISA_trace, "Do not exclude the low ISA range.");
MODULE_PARM_DESC(trace_pc, "Record address of faulting instructions.");

static void record_timestamp(struct mm_io_header *header)
{
	struct timespec now;

	getnstimeofday(&now);
	header->sec = now.tv_sec;
	header->nsec = now.tv_nsec;
}

/*
 * Write callback for the /proc entry:
 * Read a marker and write it to the mmio trace log
 */
static int write_marker(struct file *file, const char __user *buffer,
					unsigned long count, void *data)
{
	char *event = NULL;
	struct mm_io_header *headp;
	int len = (count > 65535) ? 65535 : count;

	event = kzalloc(sizeof(*headp) + len, GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	headp = (struct mm_io_header *)event;
	headp->type = MMIO_MAGIC | (MMIO_MARKER << MMIO_OPCODE_SHIFT);
	headp->data_len = len;
	record_timestamp(headp);

	if (copy_from_user(event + sizeof(*headp), buffer, len)) {
		kfree(event);
		return -EFAULT;
	}

	relay_write(chan, event, sizeof(*headp) + len);
	kfree(event);
	return len;
}

static void print_pte(unsigned long address)
{
	pgd_t *pgd = pgd_offset_k(address);
	pud_t *pud = pud_offset(pgd, address);
	pmd_t *pmd = pmd_offset(pud, address);
	if (pmd_large(*pmd)) {
		printk(KERN_EMERG MODULE_NAME ": 4MB pages are not "
						"currently supported: %lx\n",
						address);
		BUG();
	}
	printk(KERN_DEBUG MODULE_NAME ": pte for 0x%lx: 0x%lx 0x%lx\n",
		address,
		pte_val(*pte_offset_kernel(pmd, address)),
		pte_val(*pte_offset_kernel(pmd, address)) & _PAGE_PRESENT);
}

/*
 * For some reason the pre/post pairs have been called in an
 * unmatched order. Report and die.
 */
static void die_kmmio_nesting_error(struct pt_regs *regs, unsigned long addr)
{
	const unsigned long cpu = smp_processor_id();
	printk(KERN_EMERG MODULE_NAME ": unexpected fault for address: %lx, "
					"last fault for address: %lx\n",
					addr, pf_reason[cpu].addr);
	print_pte(addr);
#ifdef __i386__
	print_symbol(KERN_EMERG "faulting EIP is at %s\n", regs->ip);
	print_symbol(KERN_EMERG "last faulting EIP was at %s\n",
							pf_reason[cpu].ip);
	printk(KERN_EMERG
			"eax: %08lx   ebx: %08lx   ecx: %08lx   edx: %08lx\n",
			regs->ax, regs->bx, regs->cx, regs->dx);
	printk(KERN_EMERG
			"esi: %08lx   edi: %08lx   ebp: %08lx   esp: %08lx\n",
			regs->si, regs->di, regs->bp, regs->sp);
#else
	print_symbol(KERN_EMERG "faulting RIP is at %s\n", regs->ip);
	print_symbol(KERN_EMERG "last faulting RIP was at %s\n",
							pf_reason[cpu].ip);
	printk(KERN_EMERG "rax: %016lx   rcx: %016lx   rdx: %016lx\n",
					regs->ax, regs->cx, regs->dx);
	printk(KERN_EMERG "rsi: %016lx   rdi: %016lx   "
				"rbp: %016lx   rsp: %016lx\n",
				regs->si, regs->di, regs->bp, regs->sp);
#endif
	BUG();
}

static void pre(struct kmmio_probe *p, struct pt_regs *regs,
						unsigned long addr)
{
	const unsigned long cpu = smp_processor_id();
	const unsigned long instptr = instruction_pointer(regs);
	const enum reason_type type = get_ins_type(instptr);

	/* it doesn't make sense to have more than one active trace per cpu */
	if (pf_reason[cpu].active_traces)
		die_kmmio_nesting_error(regs, addr);
	else
		pf_reason[cpu].active_traces++;

	pf_reason[cpu].type = type;
	pf_reason[cpu].addr = addr;
	pf_reason[cpu].ip = instptr;

	cpu_trace[cpu].header.type = MMIO_MAGIC;
	cpu_trace[cpu].header.pid = 0;
	cpu_trace[cpu].header.data_len = sizeof(struct mm_io_rw);
	cpu_trace[cpu].rw.address = addr;

	/*
	 * Only record the program counter when requested.
	 * It may taint clean-room reverse engineering.
	 */
	if (trace_pc)
		cpu_trace[cpu].rw.pc = instptr;
	else
		cpu_trace[cpu].rw.pc = 0;

	record_timestamp(&cpu_trace[cpu].header);

	switch (type) {
	case REG_READ:
		cpu_trace[cpu].header.type |=
			(MMIO_READ << MMIO_OPCODE_SHIFT) |
			(get_ins_mem_width(instptr) << MMIO_WIDTH_SHIFT);
		break;
	case REG_WRITE:
		cpu_trace[cpu].header.type |=
			(MMIO_WRITE << MMIO_OPCODE_SHIFT) |
			(get_ins_mem_width(instptr) << MMIO_WIDTH_SHIFT);
		cpu_trace[cpu].rw.value = get_ins_reg_val(instptr, regs);
		break;
	case IMM_WRITE:
		cpu_trace[cpu].header.type |=
			(MMIO_WRITE << MMIO_OPCODE_SHIFT) |
			(get_ins_mem_width(instptr) << MMIO_WIDTH_SHIFT);
		cpu_trace[cpu].rw.value = get_ins_imm_val(instptr);
		break;
	default:
		{
			unsigned char *ip = (unsigned char *)instptr;
			cpu_trace[cpu].header.type |=
					(MMIO_UNKNOWN_OP << MMIO_OPCODE_SHIFT);
			cpu_trace[cpu].rw.value = (*ip) << 16 |
							*(ip + 1) << 8 |
							*(ip + 2);
		}
	}
}

static void post(struct kmmio_probe *p, unsigned long condition,
							struct pt_regs *regs)
{
	const unsigned long cpu = smp_processor_id();

	/* this should always return the active_trace count to 0 */
	pf_reason[cpu].active_traces--;
	if (pf_reason[cpu].active_traces) {
		printk(KERN_EMERG MODULE_NAME ": unexpected post handler");
		BUG();
	}

	switch (pf_reason[cpu].type) {
	case REG_READ:
		cpu_trace[cpu].rw.value = get_ins_reg_val(pf_reason[cpu].ip,
									regs);
		break;
	default:
		break;
	}
	relay_write(chan, &cpu_trace[cpu], sizeof(struct mm_io_header_rw));
}

/*
 * subbuf_start() relay callback.
 *
 * Defined so that we know when events are dropped due to the buffer-full
 * condition.
 */
static int subbuf_start_handler(struct rchan_buf *buf, void *subbuf,
					void *prev_subbuf, size_t prev_padding)
{
	if (relay_buf_full(buf)) {
		if (!suspended) {
			suspended = 1;
			printk(KERN_ERR MODULE_NAME
						": cpu %d buffer full!!!\n",
						smp_processor_id());
		}
		return 0;
	} else if (suspended) {
		suspended = 0;
		printk(KERN_ERR MODULE_NAME
					": cpu %d buffer no longer full.\n",
					smp_processor_id());
	}

	return 1;
}

/* file_create() callback.  Creates relay file in debugfs. */
static struct dentry *create_buf_file_handler(const char *filename,
						struct dentry *parent,
						int mode,
						struct rchan_buf *buf,
						int *is_global)
{
	struct dentry *buf_file;

	mmio_fops.read = relay_file_operations.read;
	mmio_fops.open = relay_file_operations.open;
	mmio_fops.poll = relay_file_operations.poll;
	mmio_fops.mmap = relay_file_operations.mmap;
	mmio_fops.release = relay_file_operations.release;
	mmio_fops.splice_read = relay_file_operations.splice_read;

	buf_file = debugfs_create_file(filename, mode, parent, buf,
								&mmio_fops);

	return buf_file;
}

/* file_remove() default callback.  Removes relay file in debugfs. */
static int remove_buf_file_handler(struct dentry *dentry)
{
	debugfs_remove(dentry);
	return 0;
}

static struct rchan_callbacks relay_callbacks = {
	.subbuf_start = subbuf_start_handler,
	.create_buf_file = create_buf_file_handler,
	.remove_buf_file = remove_buf_file_handler,
};

/*
 * create_channel - creates channel /debug/APP_DIR/cpuXXX
 * Returns channel on success, NULL otherwise
 */
static struct rchan *create_channel(unsigned size, unsigned n)
{
	return relay_open("cpu", dir, size, n, &relay_callbacks, NULL);
}

/* destroy_channel - destroys channel /debug/APP_DIR/cpuXXX */
static void destroy_channel(void)
{
	if (chan) {
		relay_close(chan);
		chan = NULL;
	}
}

struct remap_trace {
	struct list_head list;
	struct kmmio_probe probe;
};
static LIST_HEAD(trace_list);
static DEFINE_SPINLOCK(trace_list_lock);

static void do_ioremap_trace_core(unsigned long offset, unsigned long size,
							void __iomem *addr)
{
	struct remap_trace *trace = kmalloc(sizeof(*trace), GFP_KERNEL);
	struct mm_io_header_map event = {
		.header = {
			.type = MMIO_MAGIC |
					(MMIO_PROBE << MMIO_OPCODE_SHIFT),
			.sec = 0,
			.nsec = 0,
			.pid = 0,
			.data_len = sizeof(struct mm_io_map)
		},
		.map = {
			.phys = offset,
			.addr = (unsigned long)addr,
			.len  = size,
			.pc   = 0
		}
	};
	record_timestamp(&event.header);

	*trace = (struct remap_trace) {
		.probe = {
			.addr = (unsigned long)addr,
			.len = size,
			.pre_handler = pre,
			.post_handler = post,
		}
	};

	relay_write(chan, &event, sizeof(event));
	spin_lock(&trace_list_lock);
	list_add_tail(&trace->list, &trace_list);
	spin_unlock(&trace_list_lock);
	if (!nommiotrace)
		register_kmmio_probe(&trace->probe);
}

static void ioremap_trace_core(unsigned long offset, unsigned long size,
							void __iomem *addr)
{
	if ((filter_offset) && (offset != filter_offset))
		return;

	/* Don't trace the low PCI/ISA area, it's always mapped.. */
	if (!ISA_trace && (offset < ISA_END_ADDRESS) &&
					(offset + size > ISA_START_ADDRESS)) {
		printk(KERN_NOTICE MODULE_NAME ": Ignoring map of low "
						"PCI/ISA area (0x%lx-0x%lx)\n",
						offset, offset + size);
		return;
	}
	do_ioremap_trace_core(offset, size, addr);
}

void __iomem *ioremap_cache_trace(unsigned long offset, unsigned long size)
{
	void __iomem *p = ioremap_cache(offset, size);
	printk(KERN_DEBUG MODULE_NAME ": ioremap_cache(0x%lx, 0x%lx) = %p\n",
							offset, size, p);
	ioremap_trace_core(offset, size, p);
	return p;
}
EXPORT_SYMBOL(ioremap_cache_trace);

void __iomem *ioremap_nocache_trace(unsigned long offset, unsigned long size)
{
	void __iomem *p = ioremap_nocache(offset, size);
	printk(KERN_DEBUG MODULE_NAME ": ioremap_nocache(0x%lx, 0x%lx) = %p\n",
							offset, size, p);
	ioremap_trace_core(offset, size, p);
	return p;
}
EXPORT_SYMBOL(ioremap_nocache_trace);

void iounmap_trace(volatile void __iomem *addr)
{
	struct mm_io_header_map event = {
		.header = {
			.type = MMIO_MAGIC |
				(MMIO_UNPROBE << MMIO_OPCODE_SHIFT),
			.sec = 0,
			.nsec = 0,
			.pid = 0,
			.data_len = sizeof(struct mm_io_map)
		},
		.map = {
			.phys = 0,
			.addr = (unsigned long)addr,
			.len  = 0,
			.pc   = 0
		}
	};
	struct remap_trace *trace;
	struct remap_trace *tmp;
	printk(KERN_DEBUG MODULE_NAME ": Unmapping %p.\n", addr);
	record_timestamp(&event.header);

	spin_lock(&trace_list_lock);
	list_for_each_entry_safe(trace, tmp, &trace_list, list) {
		if ((unsigned long)addr == trace->probe.addr) {
			if (!nommiotrace)
				unregister_kmmio_probe(&trace->probe);
			list_del(&trace->list);
			kfree(trace);
			break;
		}
	}
	spin_unlock(&trace_list_lock);
	relay_write(chan, &event, sizeof(event));
	iounmap(addr);
}
EXPORT_SYMBOL(iounmap_trace);

static void clear_trace_list(void)
{
	struct remap_trace *trace;
	struct remap_trace *tmp;

	spin_lock(&trace_list_lock);
	list_for_each_entry_safe(trace, tmp, &trace_list, list) {
		printk(KERN_WARNING MODULE_NAME ": purging non-iounmapped "
					"trace @0x%08lx, size 0x%lx.\n",
					trace->probe.addr, trace->probe.len);
		if (!nommiotrace)
			unregister_kmmio_probe(&trace->probe);
		list_del(&trace->list);
		kfree(trace);
		break;
	}
	spin_unlock(&trace_list_lock);
}

static int __init init(void)
{
	if (n_subbufs < 2)
		return -EINVAL;

	dir = debugfs_create_dir(APP_DIR, NULL);
	if (!dir) {
		printk(KERN_ERR MODULE_NAME
				": Couldn't create relay app directory.\n");
		return -ENOMEM;
	}

	chan = create_channel(subbuf_size, n_subbufs);
	if (!chan) {
		debugfs_remove(dir);
		printk(KERN_ERR MODULE_NAME
				": relay app channel creation failed\n");
		return -ENOMEM;
	}

	init_kmmio();

	proc_marker_file = create_proc_entry(MARKER_FILE, 0, NULL);
	if (proc_marker_file)
		proc_marker_file->write_proc = write_marker;

	printk(KERN_DEBUG MODULE_NAME ": loaded.\n");
	if (nommiotrace)
		printk(KERN_DEBUG MODULE_NAME ": MMIO tracing disabled.\n");
	if (ISA_trace)
		printk(KERN_WARNING MODULE_NAME
				": Warning! low ISA range will be traced.\n");
	return 0;
}

static void __exit cleanup(void)
{
	printk(KERN_DEBUG MODULE_NAME ": unload...\n");
	clear_trace_list();
	cleanup_kmmio();
	remove_proc_entry(MARKER_FILE, NULL);
	destroy_channel();
	if (dir)
		debugfs_remove(dir);
}

module_init(init);
module_exit(cleanup);
MODULE_LICENSE("GPL");
