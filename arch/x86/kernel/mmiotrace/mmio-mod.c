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
#define DEBUG 1

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <linux/version.h>
#include <linux/kallsyms.h>
#include <asm/pgtable.h>
#include <linux/mmiotrace.h>
#include <asm/e820.h> /* for ISA_START_ADDRESS */
#include <asm/atomic.h>
#include <linux/percpu.h>

#include "pf_in.h"

#define NAME "mmiotrace: "

/* This app's relay channel files will appear in /debug/mmio-trace */
static const char APP_DIR[] = "mmio-trace";
/* the marker injection file in /debug/APP_DIR */
static const char MARKER_FILE[] = "mmio-marker";

struct trap_reason {
	unsigned long addr;
	unsigned long ip;
	enum reason_type type;
	int active_traces;
};

struct remap_trace {
	struct list_head list;
	struct kmmio_probe probe;
	unsigned long phys;
	unsigned long id;
};

static const size_t subbuf_size = 256*1024;

/* Accessed per-cpu. */
static DEFINE_PER_CPU(struct trap_reason, pf_reason);
static DEFINE_PER_CPU(struct mm_io_header_rw, cpu_trace);

#if 0 /* XXX: no way gather this info anymore */
/* Access to this is not per-cpu. */
static DEFINE_PER_CPU(atomic_t, dropped);
#endif

static struct dentry *dir;
static struct dentry *marker_file;

static DEFINE_MUTEX(mmiotrace_mutex);
static DEFINE_SPINLOCK(trace_lock);
static atomic_t mmiotrace_enabled;
static LIST_HEAD(trace_list);		/* struct remap_trace */

/*
 * Locking in this file:
 * - mmiotrace_mutex enforces enable/disable_mmiotrace() critical sections.
 * - mmiotrace_enabled may be modified only when holding mmiotrace_mutex
 *   and trace_lock.
 * - Routines depending on is_enabled() must take trace_lock.
 * - trace_list users must hold trace_lock.
 * - is_enabled() guarantees that chan is valid.
 * - pre/post callbacks assume the effect of is_enabled() being true.
 */

/* module parameters */
static unsigned int	n_subbufs = 32*4;
static unsigned long	filter_offset;
static int		nommiotrace;
static int		ISA_trace;
static int		trace_pc;

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

static bool is_enabled(void)
{
	return atomic_read(&mmiotrace_enabled);
}

/*
 * Write callback for the debugfs entry:
 * Read a marker and write it to the mmio trace log
 */
static ssize_t write_marker(struct file *file, const char __user *buffer,
						size_t count, loff_t *ppos)
{
	char *event = NULL;
	struct mm_io_header *headp;
	ssize_t len = (count > 65535) ? 65535 : count;

	event = kzalloc(sizeof(*headp) + len, GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	headp = (struct mm_io_header *)event;
	headp->type = MMIO_MAGIC | (MMIO_MARKER << MMIO_OPCODE_SHIFT);
	headp->data_len = len;

	if (copy_from_user(event + sizeof(*headp), buffer, len)) {
		kfree(event);
		return -EFAULT;
	}

	spin_lock_irq(&trace_lock);
#if 0 /* XXX: convert this to use tracing */
	if (is_enabled())
		relay_write(chan, event, sizeof(*headp) + len);
	else
#endif
		len = -EINVAL;
	spin_unlock_irq(&trace_lock);
	kfree(event);
	return len;
}

static void print_pte(unsigned long address)
{
	int level;
	pte_t *pte = lookup_address(address, &level);

	if (!pte) {
		pr_err(NAME "Error in %s: no pte for page 0x%08lx\n",
							__func__, address);
		return;
	}

	if (level == PG_LEVEL_2M) {
		pr_emerg(NAME "4MB pages are not currently supported: "
							"0x%08lx\n", address);
		BUG();
	}
	pr_info(NAME "pte for 0x%lx: 0x%lx 0x%lx\n", address, pte_val(*pte),
						pte_val(*pte) & _PAGE_PRESENT);
}

/*
 * For some reason the pre/post pairs have been called in an
 * unmatched order. Report and die.
 */
static void die_kmmio_nesting_error(struct pt_regs *regs, unsigned long addr)
{
	const struct trap_reason *my_reason = &get_cpu_var(pf_reason);
	pr_emerg(NAME "unexpected fault for address: 0x%08lx, "
					"last fault for address: 0x%08lx\n",
					addr, my_reason->addr);
	print_pte(addr);
	print_symbol(KERN_EMERG "faulting IP is at %s\n", regs->ip);
	print_symbol(KERN_EMERG "last faulting IP was at %s\n", my_reason->ip);
#ifdef __i386__
	pr_emerg("eax: %08lx   ebx: %08lx   ecx: %08lx   edx: %08lx\n",
			regs->ax, regs->bx, regs->cx, regs->dx);
	pr_emerg("esi: %08lx   edi: %08lx   ebp: %08lx   esp: %08lx\n",
			regs->si, regs->di, regs->bp, regs->sp);
#else
	pr_emerg("rax: %016lx   rcx: %016lx   rdx: %016lx\n",
					regs->ax, regs->cx, regs->dx);
	pr_emerg("rsi: %016lx   rdi: %016lx   rbp: %016lx   rsp: %016lx\n",
				regs->si, regs->di, regs->bp, regs->sp);
#endif
	put_cpu_var(pf_reason);
	BUG();
}

static void pre(struct kmmio_probe *p, struct pt_regs *regs,
						unsigned long addr)
{
	struct trap_reason *my_reason = &get_cpu_var(pf_reason);
	struct mm_io_header_rw *my_trace = &get_cpu_var(cpu_trace);
	const unsigned long instptr = instruction_pointer(regs);
	const enum reason_type type = get_ins_type(instptr);

	/* it doesn't make sense to have more than one active trace per cpu */
	if (my_reason->active_traces)
		die_kmmio_nesting_error(regs, addr);
	else
		my_reason->active_traces++;

	my_reason->type = type;
	my_reason->addr = addr;
	my_reason->ip = instptr;

	my_trace->header.type = MMIO_MAGIC;
	my_trace->header.pid = 0;
	my_trace->header.data_len = sizeof(struct mm_io_rw);
	my_trace->rw.address = addr;
	/*
	 * struct remap_trace *trace = p->user_data;
	 * phys = addr - trace->probe.addr + trace->phys;
	 */

	/*
	 * Only record the program counter when requested.
	 * It may taint clean-room reverse engineering.
	 */
	if (trace_pc)
		my_trace->rw.pc = instptr;
	else
		my_trace->rw.pc = 0;

	/*
	 * XXX: the timestamp recorded will be *after* the tracing has been
	 * done, not at the time we hit the instruction. SMP implications
	 * on event ordering?
	 */

	switch (type) {
	case REG_READ:
		my_trace->header.type |=
			(MMIO_READ << MMIO_OPCODE_SHIFT) |
			(get_ins_mem_width(instptr) << MMIO_WIDTH_SHIFT);
		break;
	case REG_WRITE:
		my_trace->header.type |=
			(MMIO_WRITE << MMIO_OPCODE_SHIFT) |
			(get_ins_mem_width(instptr) << MMIO_WIDTH_SHIFT);
		my_trace->rw.value = get_ins_reg_val(instptr, regs);
		break;
	case IMM_WRITE:
		my_trace->header.type |=
			(MMIO_WRITE << MMIO_OPCODE_SHIFT) |
			(get_ins_mem_width(instptr) << MMIO_WIDTH_SHIFT);
		my_trace->rw.value = get_ins_imm_val(instptr);
		break;
	default:
		{
			unsigned char *ip = (unsigned char *)instptr;
			my_trace->header.type |=
					(MMIO_UNKNOWN_OP << MMIO_OPCODE_SHIFT);
			my_trace->rw.value = (*ip) << 16 | *(ip + 1) << 8 |
								*(ip + 2);
		}
	}
	put_cpu_var(cpu_trace);
	put_cpu_var(pf_reason);
}

static void post(struct kmmio_probe *p, unsigned long condition,
							struct pt_regs *regs)
{
	struct trap_reason *my_reason = &get_cpu_var(pf_reason);
	struct mm_io_header_rw *my_trace = &get_cpu_var(cpu_trace);

	/* this should always return the active_trace count to 0 */
	my_reason->active_traces--;
	if (my_reason->active_traces) {
		pr_emerg(NAME "unexpected post handler");
		BUG();
	}

	switch (my_reason->type) {
	case REG_READ:
		my_trace->rw.value = get_ins_reg_val(my_reason->ip, regs);
		break;
	default:
		break;
	}

	/*
	 * XXX: Several required values are ignored:
	 * - mapping id
	 * - program counter
	 * Also the address should be physical, not virtual.
	 */
	mmio_trace_record(my_trace->header.type, my_trace->rw.address,
							my_trace->rw.value);
	put_cpu_var(cpu_trace);
	put_cpu_var(pf_reason);
}

static void ioremap_trace_core(unsigned long offset, unsigned long size,
							void __iomem *addr)
{
	static atomic_t next_id;
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

	if (!trace) {
		pr_err(NAME "kmalloc failed in ioremap\n");
		return;
	}

	*trace = (struct remap_trace) {
		.probe = {
			.addr = (unsigned long)addr,
			.len = size,
			.pre_handler = pre,
			.post_handler = post,
			.user_data = trace
		},
		.phys = offset,
		.id = atomic_inc_return(&next_id)
	};

	spin_lock_irq(&trace_lock);
	if (!is_enabled())
		goto not_enabled;

	/*
	 * XXX: Insufficient data recorded!
	 */
	mmio_trace_record(event.header.type, event.map.addr, event.map.len);
	list_add_tail(&trace->list, &trace_list);
	if (!nommiotrace)
		register_kmmio_probe(&trace->probe);

not_enabled:
	spin_unlock_irq(&trace_lock);
}

void
mmiotrace_ioremap(unsigned long offset, unsigned long size, void __iomem *addr)
{
	if (!is_enabled()) /* recheck and proper locking in *_core() */
		return;

	pr_debug(NAME "ioremap_*(0x%lx, 0x%lx) = %p\n", offset, size, addr);
	if ((filter_offset) && (offset != filter_offset))
		return;
	ioremap_trace_core(offset, size, addr);
}

static void iounmap_trace_core(volatile void __iomem *addr)
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
	struct remap_trace *found_trace = NULL;

	pr_debug(NAME "Unmapping %p.\n", addr);

	spin_lock_irq(&trace_lock);
	if (!is_enabled())
		goto not_enabled;

	list_for_each_entry_safe(trace, tmp, &trace_list, list) {
		if ((unsigned long)addr == trace->probe.addr) {
			if (!nommiotrace)
				unregister_kmmio_probe(&trace->probe);
			list_del(&trace->list);
			found_trace = trace;
			break;
		}
	}
	mmio_trace_record(event.header.type, event.map.addr,
					found_trace ? found_trace->id : -1);

not_enabled:
	spin_unlock_irq(&trace_lock);
	if (found_trace) {
		synchronize_rcu(); /* unregister_kmmio_probe() requirement */
		kfree(found_trace);
	}
}

void mmiotrace_iounmap(volatile void __iomem *addr)
{
	might_sleep();
	if (is_enabled()) /* recheck and proper locking in *_core() */
		iounmap_trace_core(addr);
}

static void clear_trace_list(void)
{
	struct remap_trace *trace;
	struct remap_trace *tmp;

	/*
	 * No locking required, because the caller ensures we are in a
	 * critical section via mutex, and is_enabled() is false,
	 * i.e. nothing can traverse or modify this list.
	 * Caller also ensures is_enabled() cannot change.
	 */
	list_for_each_entry(trace, &trace_list, list) {
		pr_notice(NAME "purging non-iounmapped "
					"trace @0x%08lx, size 0x%lx.\n",
					trace->probe.addr, trace->probe.len);
		if (!nommiotrace)
			unregister_kmmio_probe(&trace->probe);
	}
	synchronize_rcu(); /* unregister_kmmio_probe() requirement */

	list_for_each_entry_safe(trace, tmp, &trace_list, list) {
		list_del(&trace->list);
		kfree(trace);
	}
}

static struct file_operations fops_marker = {
	.owner =	THIS_MODULE,
	.write =	write_marker
};

void enable_mmiotrace(void)
{
	mutex_lock(&mmiotrace_mutex);
	if (is_enabled())
		goto out;

	reference_kmmio();

#if 0 /* XXX: tracing does not support text entries */
	marker_file = debugfs_create_file("marker", 0660, dir, NULL,
								&fops_marker);
#endif
	if (!marker_file)
		pr_err(NAME "marker file creation failed.\n");

	if (nommiotrace)
		pr_info(NAME "MMIO tracing disabled.\n");
	if (ISA_trace)
		pr_warning(NAME "Warning! low ISA range will be traced.\n");
	spin_lock_irq(&trace_lock);
	atomic_inc(&mmiotrace_enabled);
	spin_unlock_irq(&trace_lock);
	pr_info(NAME "enabled.\n");
out:
	mutex_unlock(&mmiotrace_mutex);
}

void disable_mmiotrace(void)
{
	mutex_lock(&mmiotrace_mutex);
	if (!is_enabled())
		goto out;

	spin_lock_irq(&trace_lock);
	atomic_dec(&mmiotrace_enabled);
	BUG_ON(is_enabled());
	spin_unlock_irq(&trace_lock);

	clear_trace_list(); /* guarantees: no more kmmio callbacks */
	unreference_kmmio();
	if (marker_file) {
		debugfs_remove(marker_file);
		marker_file = NULL;
	}

	pr_info(NAME "disabled.\n");
out:
	mutex_unlock(&mmiotrace_mutex);
}

int __init init_mmiotrace(void)
{
	pr_debug(NAME "load...\n");
	if (n_subbufs < 2)
		return -EINVAL;

	dir = debugfs_create_dir(APP_DIR, NULL);
	if (!dir) {
		pr_err(NAME "Couldn't create relay app directory.\n");
		return -ENOMEM;
	}
	return 0;
}
