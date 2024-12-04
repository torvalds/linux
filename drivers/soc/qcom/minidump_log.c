// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cache.h>
#include <linux/freezer.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/thread_info.h>
#include <soc/qcom/minidump.h>
#include <asm/page.h>
#include <asm/memory.h>
#include <asm/sections.h>
#include <asm/stacktrace.h>
#include <linux/mm.h>
#include <linux/ratelimit.h>
#include <linux/notifier.h>
#include <linux/sizes.h>
#include <linux/sched/task.h>
#include <linux/suspend.h>
#include <linux/vmalloc.h>
#include <linux/panic_notifier.h>
#include "debug_symbol.h"
#ifdef CONFIG_QCOM_MINIDUMP_PSTORE
#include <linux/math64.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#endif

#ifdef CONFIG_QCOM_MINIDUMP_PANIC_DUMP
#include <linux/bits.h>
#include <linux/sched/prio.h>
#include <linux/seq_buf.h>
#include <linux/debugfs.h>

#include <asm/memory.h>

#include <linux/sched/cputime.h>
#include "../../../kernel/sched/sched.h"
#include <linux/sched/walt.h>

#include <linux/kdebug.h>
#include <linux/thread_info.h>
#include <asm/ptrace.h>
#include <linux/uaccess.h>
#include <linux/percpu.h>

#include <linux/module.h>
#include <linux/cma.h>
#include <linux/dma-map-ops.h>
#include <linux/sched/clock.h>
#include <trace/hooks/cpufreq.h>
#ifdef CONFIG_QCOM_MINIDUMP_PANIC_CPU_CONTEXT
#include <trace/hooks/debug.h>
#endif
#include "minidump_memory.h"
#endif
#include "../../../kernel/time/tick-internal.h"

#ifdef CONFIG_QCOM_DYN_MINIDUMP_STACK

#include <trace/events/sched.h>

#ifdef CONFIG_VMAP_STACK
#define STACK_NUM_PAGES (THREAD_SIZE / PAGE_SIZE)
#else
#define STACK_NUM_PAGES 1
#endif	/* !CONFIG_VMAP_STACK */

struct md_stack_cpu_data {
	int stack_mdidx[STACK_NUM_PAGES];
	struct md_region stack_mdr[STACK_NUM_PAGES];
} ____cacheline_aligned_in_smp;

static int md_current_stack_init __read_mostly;

static DEFINE_PER_CPU_SHARED_ALIGNED(struct md_stack_cpu_data, md_stack_data);

struct md_suspend_context_data {
	int task_mdno;
	int stack_mdidx[STACK_NUM_PAGES];
	struct md_region stack_mdr[STACK_NUM_PAGES];
	struct md_region task_mdr;
	bool init;
};

static struct md_suspend_context_data md_suspend_context;
#endif

static bool is_vmap_stack __read_mostly;

#ifdef CONFIG_QCOM_MINIDUMP_FTRACE
#include <trace/hooks/ftrace_dump.h>
#include <linux/ring_buffer.h>
#include <linux/trace_seq.h>

#define MD_FTRACE_BUF_SIZE	SZ_2M

static char *md_ftrace_buf_addr;
static size_t md_ftrace_buf_current;
static bool minidump_ftrace_in_oops;
static bool minidump_ftrace_dump = true;
#endif

#ifdef CONFIG_QCOM_MINIDUMP_PANIC_DUMP
/* Rnqueue information */
#ifndef CONFIG_MINIDUMP_ALL_TASK_INFO
#define MD_RUNQUEUE_PAGES	8
#else
#define MD_RUNQUEUE_PAGES	150
#endif

static bool md_in_oops_handler;
static atomic_t md_handle_done;
static struct seq_buf *md_runq_seq_buf;
static int md_align_offset;

/* CPU context information */
#ifdef CONFIG_QCOM_MINIDUMP_PANIC_CPU_CONTEXT
#define MD_CPU_CNTXT_PAGES	32

static int die_cpu = -1;
static struct seq_buf *md_cntxt_seq_buf;
static DEFINE_PER_CPU(struct pt_regs, regs_before_stop);
#endif

/* Meminfo */
#ifdef CONFIG_QCOM_MINIDUMP_PANIC_MEMORY_INFO
static struct seq_buf *md_meminfo_seq_buf;

/* Slabinfo */
#ifdef CONFIG_SLUB_DEBUG
static struct seq_buf *md_slabinfo_seq_buf;
#endif

#ifdef CONFIG_PAGE_OWNER
size_t md_pageowner_dump_size = SZ_2M;
char *md_pageowner_dump_addr;
#endif

#ifdef CONFIG_SLUB_DEBUG
size_t md_slabowner_dump_size = SZ_2M;
char *md_slabowner_dump_addr;
#endif

size_t md_dma_buf_info_size = SZ_256K;
char *md_dma_buf_info_addr;

size_t md_dma_buf_procs_size = SZ_256K;
char *md_dma_buf_procs_addr;
#endif

/* Modules information */
#ifdef CONFIG_MODULES
#define MD_MODULE_PAGES	  8
static struct seq_buf *md_mod_info_seq_buf;
static DEFINE_SPINLOCK(md_modules_lock);

static int n_modump;
static char *key_modules[10];
module_param_array(key_modules, charp, &n_modump, 0644);
#endif	/* CONFIG_MODULES */
#endif

#define FREQ_LOG_MAX	10

static int register_stack_entry(struct md_region *ksp_entry, u64 sp, u64 size)
{
	struct page *sp_page;
	int entry;

	ksp_entry->virt_addr = sp;
	ksp_entry->size = size;
	if (is_vmap_stack) {
		sp_page = vmalloc_to_page((const void *) sp);
		ksp_entry->phys_addr = page_to_phys(sp_page);
	} else {
		ksp_entry->phys_addr = virt_to_phys((uintptr_t *)sp);
	}

	entry = msm_minidump_add_region(ksp_entry);
	if (entry < 0)
		printk_deferred("Failed to add stack of entry %s in Minidump\n",
				ksp_entry->name);
	return entry;
}

static void register_kernel_sections(void)
{
	struct md_region ksec_entry;
	char *data_name = "KDATABSS";
	char *rodata_name = "KROAIDATA";
	size_t static_size;
	void __percpu *base;
	unsigned int cpu;
	void *_sdata, *__bss_stop;
	void *start_ro, *end_ro;

	_sdata = DEBUG_SYMBOL_LOOKUP(_sdata);
	__bss_stop = DEBUG_SYMBOL_LOOKUP(__bss_stop);
	base = DEBUG_SYMBOL_LOOKUP(__per_cpu_start);
	static_size = (size_t)(DEBUG_SYMBOL_LOOKUP(__per_cpu_end) - base);

	strscpy(ksec_entry.name, data_name, sizeof(ksec_entry.name));
	ksec_entry.virt_addr = (u64)_sdata;
	ksec_entry.phys_addr = virt_to_phys(_sdata);
	ksec_entry.size = roundup((__bss_stop - _sdata), 4);
	if (msm_minidump_add_region(&ksec_entry) < 0)
		pr_err("Failed to add data section in Minidump\n");

	start_ro = DEBUG_SYMBOL_LOOKUP(__start_ro_after_init);
	end_ro = DEBUG_SYMBOL_LOOKUP(__end_ro_after_init);
	strscpy(ksec_entry.name, rodata_name, sizeof(ksec_entry.name));
	ksec_entry.virt_addr = (uintptr_t)start_ro;
	ksec_entry.phys_addr = virt_to_phys(start_ro);
	ksec_entry.size = roundup((end_ro - start_ro), 4);
	if (msm_minidump_add_region(&ksec_entry) < 0)
		pr_err("Failed to add rodata section in Minidump\n");

	/* Add percpu static sections */
	for_each_possible_cpu(cpu) {
		void *start = per_cpu_ptr(base, cpu);

		memset(&ksec_entry, 0, sizeof(ksec_entry));
		scnprintf(ksec_entry.name, sizeof(ksec_entry.name),
			"KSPERCPU%d", cpu);
		ksec_entry.virt_addr = (uintptr_t)start;
		ksec_entry.phys_addr = per_cpu_ptr_to_phys(start);
		ksec_entry.size = static_size;
		if (msm_minidump_add_region(&ksec_entry) < 0)
			pr_err("Failed to add percpu sections in Minidump\n");
	}
}

static inline bool in_stack_range(
		u64 sp, u64 base_addr, unsigned int stack_size)
{
	u64 min_addr = base_addr;
	u64 max_addr = base_addr + stack_size;

	return (min_addr <= sp && sp < max_addr);
}

static unsigned int calculate_copy_pages(u64 sp, struct vm_struct *stack_area)
{
	u64 tsk_stack_base = (u64) stack_area->addr;
	u64 offset;
	unsigned int stack_pages, copy_pages;

	if (in_stack_range(sp, tsk_stack_base, get_vm_area_size(stack_area))) {
		offset = sp - tsk_stack_base;
		stack_pages = get_vm_area_size(stack_area) / PAGE_SIZE;
		copy_pages = stack_pages - (offset / PAGE_SIZE);
	} else {
		copy_pages = 0;
	}
	return copy_pages;
}

void dump_stack_minidump(u64 sp)
{
	struct md_region ksp_entry, ktsk_entry;
	u32 cpu = smp_processor_id();
	struct vm_struct *stack_vm_area;
	unsigned int i, copy_pages;

	if (IS_ENABLED(CONFIG_QCOM_DYN_MINIDUMP_STACK))
		return;

	if (is_idle_task(current))
		return;

	is_vmap_stack = IS_ENABLED(CONFIG_VMAP_STACK);

	if (sp < KIMAGE_VADDR || sp > -256UL)
		sp = current_stack_pointer;

	/*
	 * Since stacks are now allocated with vmalloc, the translation to
	 * physical address is not a simple linear transformation like it is
	 * for kernel logical addresses, since vmalloc creates a virtual
	 * mapping. Thus, virt_to_phys() should not be used in this context;
	 * instead the page table must be walked to acquire the physical
	 * address of one page of the stack.
	 */
	stack_vm_area = task_stack_vm_area(current);
	if (is_vmap_stack) {
		sp &= ~(PAGE_SIZE - 1);
		copy_pages = calculate_copy_pages(sp, stack_vm_area);
		for (i = 0; i < copy_pages; i++) {
			scnprintf(ksp_entry.name, sizeof(ksp_entry.name),
				  "KSTACK%d_%d", cpu, i);
			(void)register_stack_entry(&ksp_entry, sp, PAGE_SIZE);
			sp += PAGE_SIZE;
		}
	} else {
		sp &= ~(THREAD_SIZE - 1);
		scnprintf(ksp_entry.name, sizeof(ksp_entry.name), "KSTACK%d",
			  cpu);
		(void)register_stack_entry(&ksp_entry, sp, THREAD_SIZE);
	}

	scnprintf(ktsk_entry.name, sizeof(ktsk_entry.name), "KTASK%d", cpu);
	ktsk_entry.virt_addr = (u64)current;
	ktsk_entry.phys_addr = virt_to_phys((uintptr_t *)current);
	ktsk_entry.size = sizeof(struct task_struct);
	if (msm_minidump_add_region(&ktsk_entry) < 0)
		pr_err("Failed to add current task %d in Minidump\n", cpu);
}

#ifdef CONFIG_QCOM_DYN_MINIDUMP_STACK
static void update_stack_entry(struct md_region *ksp_entry, u64 sp,
			       int mdno)
{
	struct page *sp_page;

	ksp_entry->virt_addr = sp;
	if (likely(is_vmap_stack)) {
		sp_page = vmalloc_to_page((const void *) sp);
		ksp_entry->phys_addr = page_to_phys(sp_page);
	} else {
		ksp_entry->phys_addr = virt_to_phys((uintptr_t *)sp);
	}
	if (msm_minidump_update_region(mdno, ksp_entry) < 0) {
		printk_deferred("Failed to update stack entry %s in minidump\n",
			ksp_entry->name);
	}
}

static void register_vmapped_stack(struct md_region *mdr, int *mdno,
				   u64 sp, char *name_str, bool update)
{
	int i;

	sp &= ~(PAGE_SIZE - 1);
	for (i = 0; i < STACK_NUM_PAGES; i++) {
		if (unlikely(!update)) {
			scnprintf(mdr->name, sizeof(mdr->name), "%s_%d",
					  name_str, i);
			*mdno = register_stack_entry(mdr, sp, PAGE_SIZE);
		} else {
			update_stack_entry(mdr, sp, *mdno);
		}
		sp += PAGE_SIZE;
		mdr++;
		mdno++;
	}
}

static void register_normal_stack(struct md_region *mdr, int *mdno,
				  u64 sp, char *name_str, bool update)
{
	sp &= ~(THREAD_SIZE - 1);
	if (unlikely(!update)) {
		scnprintf(mdr->name, sizeof(mdr->name), name_str);
		*mdno = register_stack_entry(mdr, sp, THREAD_SIZE);
	} else {
		update_stack_entry(mdr, sp, *mdno);
	}
}

static void update_md_stack(struct md_region *stack_mdr,
			    int *stack_mdno, u64 sp)
{
	unsigned int i;
	int *mdno;

	if (likely(is_vmap_stack)) {
		for (i = 0; i < STACK_NUM_PAGES; i++) {
			mdno = stack_mdno + i;
			if (unlikely(*mdno < 0))
				return;
		}
		register_vmapped_stack(stack_mdr, stack_mdno, sp, NULL, true);
	} else {
		if (unlikely(*stack_mdno < 0))
			return;
		register_normal_stack(stack_mdr, stack_mdno, sp, NULL, true);
	}
}

static void update_md_cpu_stack(struct task_struct *tsk, u32 cpu, u64 sp)
{
	struct md_stack_cpu_data *md_stack_cpu_d = &per_cpu(md_stack_data, cpu);

	if (is_idle_task(tsk) || !md_current_stack_init)
		return;

	update_md_stack(md_stack_cpu_d->stack_mdr,
			md_stack_cpu_d->stack_mdidx, sp);
}

void md_current_stack_notifer(void *ignore, bool preempt,
		struct task_struct *prev, struct task_struct *next,
		unsigned int prev_state)
{
	u32 cpu = task_cpu(next);
	u64 sp = (u64)next->stack;

	update_md_cpu_stack(next, cpu, sp);
}

void md_current_stack_ipi_handler(void *data)
{
	u32 cpu = smp_processor_id();
	struct vm_struct *stack_vm_area;
	u64 sp = current_stack_pointer;

	if (is_idle_task(current))
		return;
	if (likely(is_vmap_stack)) {
		stack_vm_area = task_stack_vm_area(current);
		sp = (u64)stack_vm_area->addr;
	}
	update_md_cpu_stack(current, cpu, sp);
}

static void update_md_current_task(struct md_region *mdr, int mdno)
{
	mdr->virt_addr = (u64)current;
	mdr->phys_addr = virt_to_phys((uintptr_t *)current);
	if (msm_minidump_update_region(mdno, mdr) < 0)
		pr_err("Failed to update %s current task in minidump\n",
			   mdr->name);
}

static void update_md_suspend_current_stack(void)
{
	u64 sp = current_stack_pointer;
	struct vm_struct *stack_vm_area;

	if (likely(is_vmap_stack)) {
		stack_vm_area = task_stack_vm_area(current);
		sp = (u64)stack_vm_area->addr;
	}
	update_md_stack(md_suspend_context.stack_mdr,
			md_suspend_context.stack_mdidx, sp);
}

static void update_md_suspend_current_task(void)
{
	if (unlikely(md_suspend_context.task_mdno < 0))
		return;
	update_md_current_task(&md_suspend_context.task_mdr,
			md_suspend_context.task_mdno);
}

static void update_md_suspend_currents(void)
{
	if (!md_suspend_context.init)
		return;
	update_md_suspend_current_stack();
	update_md_suspend_current_task();
}

static void register_current_stack(void)
{
	int cpu;
	u64 sp = current_stack_pointer;
	struct md_stack_cpu_data *md_stack_cpu_d;
	struct vm_struct *stack_vm_area;
	char name_str[MAX_NAME_LENGTH];

	/*
	 * Since stacks are now allocated with vmalloc, the translation to
	 * physical address is not a simple linear transformation like it is
	 * for kernel logical addresses, since vmalloc creates a virtual
	 * mapping. Thus, virt_to_phys() should not be used in this context;
	 * instead the page table must be walked to acquire the physical
	 * address of all pages of the stack.
	 */
	if (likely(is_vmap_stack)) {
		stack_vm_area = task_stack_vm_area(current);
		sp = (u64)stack_vm_area->addr;
	}
	for_each_possible_cpu(cpu) {
		/*
		 * Let's register dummies for now,
		 * once system up and running, let the cpu update its currents.
		 */
		md_stack_cpu_d = &per_cpu(md_stack_data, cpu);
		scnprintf(name_str, sizeof(name_str), "KSTACK%d", cpu);
		if (is_vmap_stack)
			register_vmapped_stack(md_stack_cpu_d->stack_mdr,
				md_stack_cpu_d->stack_mdidx, sp,
				name_str, false);
		else
			register_normal_stack(md_stack_cpu_d->stack_mdr,
				md_stack_cpu_d->stack_mdidx, sp,
				name_str, false);
	}

	register_trace_sched_switch(md_current_stack_notifer, NULL);
	md_current_stack_init = 1;
	smp_call_function(md_current_stack_ipi_handler, NULL, 1);
}

static void register_suspend_stack(void)
{
	char name_str[MAX_NAME_LENGTH];
	u64 sp = current_stack_pointer;
	struct vm_struct *stack_vm_area = task_stack_vm_area(current);

	scnprintf(name_str, sizeof(name_str), "KSUSPSTK");
	if (is_vmap_stack) {
		sp = (u64)stack_vm_area->addr;
		register_vmapped_stack(md_suspend_context.stack_mdr,
				md_suspend_context.stack_mdidx,
				sp, name_str, false);
	} else {
		register_normal_stack(md_suspend_context.stack_mdr,
			md_suspend_context.stack_mdidx,
			sp, name_str, false);
	}
}

static void register_current_task(struct md_region *mdr, int *mdno,
				  char *name_str)
{
	scnprintf(mdr->name, sizeof(mdr->name), name_str);
	mdr->virt_addr = (u64)current;
	mdr->phys_addr = virt_to_phys((uintptr_t *)current);
	mdr->size = sizeof(struct task_struct);
	*mdno = msm_minidump_add_region(mdr);
	if (*mdno < 0)
		pr_err("Failed to add current task %s in Minidump\n",
		       mdr->name);
}

static void register_suspend_current_task(void)
{
	char name_str[MAX_NAME_LENGTH];

	scnprintf(name_str, sizeof(name_str), "KSUSPTASK");
	register_current_task(&md_suspend_context.task_mdr,
			&md_suspend_context.task_mdno, name_str);
}

static int minidump_pm_notifier(struct notifier_block *nb,
				unsigned long event, void *unused)
{
	switch (event) {
	case PM_SUSPEND_PREPARE:
		update_md_suspend_currents();
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block minidump_pm_nb = {
	.notifier_call = minidump_pm_notifier,
};

static void register_suspend_context(void)
{
	register_suspend_stack();
	register_suspend_current_task();
	register_pm_notifier(&minidump_pm_nb);
	md_suspend_context.init = true;
}
#endif

#ifdef CONFIG_ARM64
static void register_irq_stack(void)
{
	int cpu;
	unsigned int i;
	int irq_stack_pages_count;
	u64 irq_stack_base;
	struct md_region irq_sp_entry;
	u64 sp;
	u64 *irq_stack_ptr = DEBUG_SYMBOL_LOOKUP(irq_stack_ptr);

	for_each_possible_cpu(cpu) {
		irq_stack_base = *(u64 *)(per_cpu_ptr((void *)irq_stack_ptr, cpu));
		if (is_vmap_stack) {
			irq_stack_pages_count = IRQ_STACK_SIZE / PAGE_SIZE;
			sp = irq_stack_base & ~(PAGE_SIZE - 1);
			for (i = 0; i < irq_stack_pages_count; i++) {
				scnprintf(irq_sp_entry.name,
				sizeof(irq_sp_entry.name),
					"KISTK%d_%d", cpu, i);
				register_stack_entry(&irq_sp_entry, sp,
					PAGE_SIZE);
				sp += PAGE_SIZE;
			}
		} else {
			sp = irq_stack_base;
			scnprintf(irq_sp_entry.name, sizeof(irq_sp_entry.name),
				"KISTK%d", cpu);
			register_stack_entry(&irq_sp_entry, sp, IRQ_STACK_SIZE);
			}
	}
}
#else
static inline void register_irq_stack(void) {}
#endif

#ifdef CONFIG_QCOM_MINIDUMP_FTRACE
static void minidump_add_trace_event(char *buf, size_t size)
{
	char *addr;

	if (!READ_ONCE(md_ftrace_buf_addr) ||
	    (size > (size_t)MD_FTRACE_BUF_SIZE))
		return;

	if ((md_ftrace_buf_current + size) > (size_t)MD_FTRACE_BUF_SIZE)
		md_ftrace_buf_current = 0;
	addr = md_ftrace_buf_addr + md_ftrace_buf_current;
	memcpy(addr, buf, size);
	md_ftrace_buf_current += size;
}

static void md_trace_oops_enter(void *unused, bool *enter_check)
{
	if (!minidump_ftrace_in_oops) {
		minidump_ftrace_in_oops = true;
		*enter_check = false;
	} else {
		*enter_check = true;
	}
}

static void md_trace_oops_exit(void *unused, bool *exit_check)
{
	minidump_ftrace_in_oops = false;
}

static void md_update_trace_fmt(void *unused, bool *format_check)
{
	*format_check = false;
}

static void md_buf_size_check(void *unused, unsigned long buffer_size,
			      bool *size_check)
{
	if (!minidump_ftrace_dump) {
		*size_check = true;
		return;
	}

	if (buffer_size > (SZ_256K + PAGE_SIZE)) {
		pr_err("Skip md ftrace buffer dump for: %#lx\n", buffer_size);
		minidump_ftrace_dump = false;
		*size_check = true;
	}
}

static void md_dump_trace_buf(void *unused, struct trace_seq *trace_buf,
			      bool *printk_check)
{
	if (minidump_ftrace_in_oops && minidump_ftrace_dump) {
		minidump_add_trace_event(trace_buf->buffer,
					 trace_buf->seq.len);
		*printk_check = false;
	}
}

static void md_register_trace_buf(void)
{
	struct md_region md_entry;
	void *buffer_start;

	buffer_start = kzalloc(MD_FTRACE_BUF_SIZE, GFP_KERNEL);

	if (!buffer_start)
		return;

	strscpy(md_entry.name, "KFTRACE", sizeof(md_entry.name));
	md_entry.virt_addr = (uintptr_t)buffer_start;
	md_entry.phys_addr = virt_to_phys(buffer_start);
	md_entry.size = MD_FTRACE_BUF_SIZE;
	if (msm_minidump_add_region(&md_entry) < 0)
		pr_err("Failed to add ftrace buffer entry in Minidump\n");

	register_trace_android_vh_ftrace_oops_enter(md_trace_oops_enter,
							 NULL);
	register_trace_android_vh_ftrace_oops_exit(md_trace_oops_exit,
							 NULL);
	register_trace_android_vh_ftrace_size_check(md_buf_size_check,
						    NULL);
	register_trace_android_vh_ftrace_format_check(md_update_trace_fmt,
						      NULL);
	register_trace_android_vh_ftrace_dump_buffer(md_dump_trace_buf,
						     NULL);

	/* Complete registration before adding enteries */
	smp_mb();
	WRITE_ONCE(md_ftrace_buf_addr, buffer_start);
}
#endif

#ifdef CONFIG_QCOM_MINIDUMP_PANIC_DUMP

static void md_dump_align(void)
{
	int tab_offset = md_align_offset;

	while (tab_offset--)
		seq_buf_printf(md_runq_seq_buf, " | ");
	seq_buf_printf(md_runq_seq_buf, " |--");
}

static void md_dump_task_info(struct task_struct *task, char *status,
			      struct task_struct *curr)
{
	struct sched_entity *se;

	md_dump_align();
	if (!task) {
		seq_buf_printf(md_runq_seq_buf, "%s : None(0)\n", status);
		return;
	}

	se = &task->se;
	if (task == curr) {
		seq_buf_printf(md_runq_seq_buf,
			       "[status: curr] pid: %d preempt: %#x\n",
			       task_pid_nr(task),
			       task->thread_info.preempt_count);
		return;
	}

	seq_buf_printf(md_runq_seq_buf,
		       "[status: %s] pid: %d\n",
		       status, task_pid_nr(task));
}

static void md_dump_cfs_rq(struct cfs_rq *cfs, struct task_struct *curr);

static void md_dump_cgroup_state(char *status, struct sched_entity *se_p,
				 struct task_struct *curr)
{
	struct task_struct *task;
	struct cfs_rq *my_q = NULL;
	unsigned int nr_running;

	if (!se_p) {
		md_dump_task_info(NULL, status, NULL);
		return;
	}
#ifdef CONFIG_FAIR_GROUP_SCHED
	my_q = se_p->my_q;
#endif
	if (!my_q) {
		task = container_of(se_p, struct task_struct, se);
		md_dump_task_info(task, status, curr);
		return;
	}
	nr_running = my_q->nr_running;
	md_dump_align();
	seq_buf_printf(md_runq_seq_buf, "%s: %d process is grouping\n",
				   status, nr_running);
	md_align_offset++;
	md_dump_cfs_rq(my_q, curr);
	md_align_offset--;
}

static void md_dump_cfs_node_func(struct rb_node *node,
				  struct task_struct *curr)
{
	struct sched_entity *se_p = container_of(node, struct sched_entity,
						 run_node);

	md_dump_cgroup_state("pend", se_p, curr);
}

static void md_rb_walk_cfs(struct rb_root_cached *rb_root_cached_p,
			   struct task_struct *curr)
{
	int max_walk = 200;	/* Bail out, in case of loop */
	struct rb_node *leftmost = rb_root_cached_p->rb_leftmost;
	struct rb_root *root = &rb_root_cached_p->rb_root;
	struct rb_node *rb_node = rb_first(root);

	if (!leftmost)
		return;
	while (rb_node && max_walk--) {
		md_dump_cfs_node_func(rb_node, curr);
		rb_node = rb_next(rb_node);
	}
}

static void md_dump_cfs_rq(struct cfs_rq *cfs, struct task_struct *curr)
{
	struct rb_root_cached *rb_root_cached_p = &cfs->tasks_timeline;

	md_dump_cgroup_state("curr", cfs->curr, curr);
	md_dump_cgroup_state("next", cfs->next, curr);
	md_dump_cgroup_state("last", cfs->last, curr);
	md_dump_cgroup_state("skip", cfs->skip, curr);
	md_rb_walk_cfs(rb_root_cached_p, curr);
}

static void md_dump_rt_rq(struct rt_rq  *rt_rq, struct task_struct *curr)
{
	struct rt_prio_array *array = &rt_rq->active;
	struct sched_rt_entity *rt_se;
	int idx;

	/* Lifted most of the below code from dump_throttled_rt_tasks() */
	if (bitmap_empty(array->bitmap, MAX_RT_PRIO))
		return;

	idx = sched_find_first_bit(array->bitmap);
	while (idx < MAX_RT_PRIO) {
		list_for_each_entry(rt_se, array->queue + idx, run_list) {
			struct task_struct *p;

#ifdef CONFIG_RT_GROUP_SCHED
			if (rt_se->my_q)
				continue;
#endif

			p = container_of(rt_se, struct task_struct, rt);
			md_dump_task_info(p, "pend", curr);
		}
		idx = find_next_bit(array->bitmap, MAX_RT_PRIO, idx + 1);
	}
}

static const char * const task_state_array[] = {
	"R", /* 0x00 */
	"S", /* 0x01 */
	"D", /* 0x02 */
	"T", /* 0x04 */
	"t", /* 0x08 */
	"X", /* 0x10 */
	"Z", /* 0x20 */
	"P", /* 0x40 */
	"I", /* 0x80 */
};

/* In line with task_state_index from fs/proc/array.c */
static inline unsigned int md_task_state_index(struct task_struct *tsk)
{
	unsigned int tsk_state = READ_ONCE(tsk->__state);
	unsigned int state = (tsk_state | tsk->exit_state) & TASK_REPORT;

	if (tsk_state == TASK_IDLE)
		state = TASK_REPORT_IDLE;

	return fls(state);
}

/* In line with get_task_state from fs/proc/array.c */
static inline const char *md_get_task_state(struct task_struct *tsk)
{
	return task_state_array[md_task_state_index(tsk)];
}

static void md_dump_next_event(void)
{
	int cpu;
	struct tick_device *device_dump;
	struct clock_event_device *event_dev;

	device_dump = DEBUG_SYMBOL_LOOKUP(tick_cpu_device);
	if (!device_dump)
		return;

	for_each_possible_cpu(cpu) {
		event_dev = per_cpu(device_dump->evtdev, cpu);
		if (event_dev)
			pr_emerg("CPU%d next event is %ld\n", cpu,
				event_dev->next_event);
		else
			pr_emerg("CPU%d next event is not available\n", cpu);
	}
}

static void md_dump_runqueues(void)
{
	int cpu;
	struct rq *rq;
	struct rt_rq  *rt;
	struct cfs_rq *cfs;
	struct task_struct *p, *t;
#if IS_ENABLED(CONFIG_SCHED_WALT)
	struct walt_task_struct *wts;
#endif

	if (!md_runq_seq_buf)
		return;

	for_each_possible_cpu(cpu) {
		rq = cpu_rq(cpu);
		rt = &rq->rt;
		cfs = &rq->cfs;
		seq_buf_printf(md_runq_seq_buf,
			       "CPU%d has %d process, current is pid %d\n",
			       cpu, rq->nr_running, cpu_curr(cpu)->pid);
		seq_buf_printf(md_runq_seq_buf,
			       "CFS has %d process\n",
			       cfs->nr_running);
		md_dump_cfs_rq(cfs, cpu_curr(cpu));
		seq_buf_printf(md_runq_seq_buf,
			       "RT has %d process\n",
			       rt->rt_nr_running);
		md_dump_rt_rq(rt, cpu_curr(cpu));
		seq_buf_printf(md_runq_seq_buf, "\n");
	}

	seq_buf_printf(md_runq_seq_buf, "%-15s", "Task name");
	seq_buf_printf(md_runq_seq_buf, "%*s", 6, "PID");
	seq_buf_printf(md_runq_seq_buf, "%*s", 16, "Exec_started_at");
	seq_buf_printf(md_runq_seq_buf, "%*s", 16, "Last_queued_at");
	seq_buf_printf(md_runq_seq_buf, "%*s", 16, "Total_wait_time");
	seq_buf_printf(md_runq_seq_buf, "%*s", 12, "Exec_times");
	seq_buf_printf(md_runq_seq_buf, "%*s", 4, "CPU");
	seq_buf_printf(md_runq_seq_buf, "%*s", 5, "Prio");
	seq_buf_printf(md_runq_seq_buf, "%*s", 6, "State");
#if IS_ENABLED(CONFIG_SCHED_WALT)
	seq_buf_printf(md_runq_seq_buf, "%*s", 17, "Last_enqueued_ts");
	seq_buf_printf(md_runq_seq_buf, "%*s", 16, "Last_sleep_ts");
#endif
	seq_buf_printf(md_runq_seq_buf, "\n");

	for_each_process_thread(p, t) {
#ifndef CONFIG_MINIDUMP_ALL_TASK_INFO
		if (READ_ONCE(t->__state))
			continue;
#endif
		seq_buf_printf(md_runq_seq_buf, "%-15s", t->comm);
		seq_buf_printf(md_runq_seq_buf, "%6d", t->pid);
		seq_buf_printf(md_runq_seq_buf, "%16lld", t->sched_info.last_arrival);
		seq_buf_printf(md_runq_seq_buf, "%16lld", t->sched_info.last_queued);
		seq_buf_printf(md_runq_seq_buf, "%16lld", t->sched_info.run_delay);
		seq_buf_printf(md_runq_seq_buf, "%12ld", t->sched_info.pcount);
		seq_buf_printf(md_runq_seq_buf, "%4d", t->on_cpu);
		seq_buf_printf(md_runq_seq_buf, "%5d", t->prio);
		seq_buf_printf(md_runq_seq_buf, "%*s", 6, md_get_task_state(t));
#if IS_ENABLED(CONFIG_SCHED_WALT)
		wts = (struct walt_task_struct *) t->android_vendor_data1;
		seq_buf_printf(md_runq_seq_buf, "%17ld", wts->last_enqueued_ts);
		seq_buf_printf(md_runq_seq_buf, "%16ld", wts->last_sleep_ts);
#endif
		seq_buf_printf(md_runq_seq_buf, "\n");
	}
}

#ifdef CONFIG_QCOM_MINIDUMP_PANIC_CPU_CONTEXT
/*
 * dump a block of kernel memory from around the given address.
 * Bulk of the code is lifted from arch/arm64/kernel/proccess.c.
 */
static void md_dump_data(unsigned long addr, int nbytes, const char *name)
{
	int	i, j;
	int	nlines;
	u32	*p;

	/*
	 * don't attempt to dump non-kernel addresses or
	 * values that are probably just small negative numbers
	 */
	if (addr < PAGE_OFFSET || addr > -256UL)
		return;

	seq_buf_printf(md_cntxt_seq_buf, "\n%s: %#lx:\n", name, addr);

	/*
	 * round address down to a 32 bit boundary
	 * and always dump a multiple of 32 bytes
	 */
	p = (u32 *)(addr & ~(sizeof(u32) - 1));
	nbytes += (addr & (sizeof(u32) - 1));
	nlines = (nbytes + 31) / 32;

	for (i = 0; i < nlines; i++) {
		/*
		 * just display low 16 bits of address to keep
		 * each line of the dump < 80 characters
		 */
		seq_buf_printf(md_cntxt_seq_buf, "%04lx ",
			       (unsigned long)p & 0xffff);
		for (j = 0; j < 8; j++) {
			u32	data = 0;

			if (get_kernel_nofault(data, p))
				seq_buf_printf(md_cntxt_seq_buf, " ********");
			else
				seq_buf_printf(md_cntxt_seq_buf, " %08x", data);
			++p;
		}
		seq_buf_printf(md_cntxt_seq_buf, "\n");
	}
}

static void md_reg_context_data(struct pt_regs *regs)
{
	int nbytes = 128;

	if (user_mode(regs) ||  !regs->pc)
		return;

	md_dump_data(regs->pc - nbytes, nbytes * 2, "PC");
	md_dump_data(regs->regs[30] - nbytes, nbytes * 2, "LR");
	md_dump_data(regs->sp - nbytes, nbytes * 2, "SP");
}

static inline void md_dump_panic_regs(void)
{
	struct pt_regs regs;
	u64 tmp1, tmp2;

	/* Lifted from crash_setup_regs() */
	__asm__ __volatile__ (
		"stp	 x0,   x1, [%2, #16 *  0]\n"
		"stp	 x2,   x3, [%2, #16 *  1]\n"
		"stp	 x4,   x5, [%2, #16 *  2]\n"
		"stp	 x6,   x7, [%2, #16 *  3]\n"
		"stp	 x8,   x9, [%2, #16 *  4]\n"
		"stp	x10,  x11, [%2, #16 *  5]\n"
		"stp	x12,  x13, [%2, #16 *  6]\n"
		"stp	x14,  x15, [%2, #16 *  7]\n"
		"stp	x16,  x17, [%2, #16 *  8]\n"
		"stp	x18,  x19, [%2, #16 *  9]\n"
		"stp	x20,  x21, [%2, #16 * 10]\n"
		"stp	x22,  x23, [%2, #16 * 11]\n"
		"stp	x24,  x25, [%2, #16 * 12]\n"
		"stp	x26,  x27, [%2, #16 * 13]\n"
		"stp	x28,  x29, [%2, #16 * 14]\n"
		"mov	 %0,  sp\n"
		"stp	x30,  %0,  [%2, #16 * 15]\n"

		"/* faked current PSTATE */\n"
		"mrs	 %0, CurrentEL\n"
		"mrs	 %1, SPSEL\n"
		"orr	 %0, %0, %1\n"
		"mrs	 %1, DAIF\n"
		"orr	 %0, %0, %1\n"
		"mrs	 %1, NZCV\n"
		"orr	 %0, %0, %1\n"
		/* pc */
		"adr	 %1, 1f\n"
		"1:\n"
		"stp	 %1, %0,   [%2, #16 * 16]\n"
		: "=&r" (tmp1), "=&r" (tmp2)
		: "r" (&regs)
		: "memory"
		);

	seq_buf_printf(md_cntxt_seq_buf, "PANIC CPU : %d\n",
				   raw_smp_processor_id());
	md_reg_context_data(&regs);
}

static void md_dump_other_cpus_context(void)
{
	int cpu;
	struct pt_regs regs;

	for_each_possible_cpu(cpu) {
		regs = per_cpu(regs_before_stop, cpu);
		seq_buf_printf(md_cntxt_seq_buf, "\nSTOPPED CPU : %d\n", cpu);
		md_reg_context_data(&regs);
	}
}

static int md_die_context_notify(struct notifier_block *self,
				 unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;

	if (md_in_oops_handler)
		return NOTIFY_DONE;
	md_in_oops_handler = true;
	if (!md_cntxt_seq_buf) {
		md_in_oops_handler = false;
		return NOTIFY_DONE;
	}
	die_cpu = raw_smp_processor_id();
	seq_buf_printf(md_cntxt_seq_buf, "\nDIE CPU : %d\n", die_cpu);
	md_reg_context_data(args->regs);
	md_in_oops_handler = false;
	return NOTIFY_DONE;
}

static struct notifier_block md_die_context_nb = {
	.notifier_call = md_die_context_notify,
	.priority = INT_MAX - 2, /* < msm watchdog die notifier */
};

static void md_ipi_stop(void *unused, struct pt_regs *regs)
{
	unsigned int cpu = smp_processor_id();

	per_cpu(regs_before_stop, cpu) = *regs;
}
#endif

void md_dump_process(void)
{
	if (md_in_oops_handler)
		return;
	if (!atomic_add_unless(&md_handle_done, 1, 1))
		return;
	md_in_oops_handler = true;
#ifdef CONFIG_QCOM_MINIDUMP_PANIC_CPU_CONTEXT
	if (!md_cntxt_seq_buf)
		goto dump_rq;
	if (raw_smp_processor_id() != die_cpu)
		md_dump_panic_regs();
	md_dump_other_cpus_context();
dump_rq:
#endif
	md_dump_next_event();
	md_dump_runqueues();
#ifdef CONFIG_QCOM_MINIDUMP_PANIC_MEMORY_INFO
	if (md_meminfo_seq_buf)
		md_dump_meminfo(md_meminfo_seq_buf);

#ifdef CONFIG_SLUB_DEBUG
	if (md_slabinfo_seq_buf)
		md_dump_slabinfo(md_slabinfo_seq_buf);
#endif

#ifdef CONFIG_PAGE_OWNER
	if (md_pageowner_dump_addr)
		md_dump_pageowner(md_pageowner_dump_addr, md_pageowner_dump_size);
#endif

#ifdef CONFIG_SLUB_DEBUG
	if (md_slabowner_dump_addr)
		md_dump_slabowner(md_slabowner_dump_addr, md_slabowner_dump_size);
#endif
	if (md_dma_buf_info_addr)
		md_dma_buf_info(md_dma_buf_info_addr, md_dma_buf_info_size);
	if (md_dma_buf_procs_addr)
		md_dma_buf_procs(md_dma_buf_procs_addr, md_dma_buf_procs_size);
#endif
	md_in_oops_handler = false;
}
EXPORT_SYMBOL(md_dump_process);

static int md_panic_handler(struct notifier_block *this,
			    unsigned long event, void *ptr)
{
	md_dump_process();
	return NOTIFY_DONE;
}

static struct notifier_block md_panic_blk = {
	.notifier_call = md_panic_handler,
	.priority = INT_MAX - 3, /* < msm watchdog panic notifier */
};

static int md_register_minidump_entry(char *name, u64 virt_addr,
				      u64 phys_addr, u64 size)
{
	struct md_region md_entry;
	int ret;

	strscpy(md_entry.name, name, sizeof(md_entry.name));
	md_entry.virt_addr = virt_addr;
	md_entry.phys_addr = phys_addr;
	md_entry.size = size;
	ret = msm_minidump_add_region(&md_entry);
	if (ret < 0)
		pr_err("Failed to add %s entry in Minidump\n", name);
	return ret;
}

static int md_register_panic_entries(int num_pages, char *name,
				      struct seq_buf **global_buf)
{
	char *buf;
	struct seq_buf *seq_buf_p;
	int ret;

	buf = kzalloc(num_pages * PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -EINVAL;

	seq_buf_p = kzalloc(sizeof(*seq_buf_p), GFP_KERNEL);
	if (!seq_buf_p) {
		ret = -EINVAL;
		goto err_seq_buf;
	}

	ret = md_register_minidump_entry(name, (uintptr_t)buf,
					 virt_to_phys(buf),
					 num_pages * PAGE_SIZE);
	if (ret < 0)
		goto err_entry_reg;

	seq_buf_init(seq_buf_p, buf, num_pages * PAGE_SIZE);

	/* Complete registration before populating data */
	smp_mb();
	WRITE_ONCE(*global_buf, seq_buf_p);
	return 0;

err_entry_reg:
	kfree(seq_buf_p);
err_seq_buf:
	kfree(buf);
	return ret;
}

static void md_register_panic_data(void)
{
#ifdef CONFIG_QCOM_MINIDUMP_PANIC_MEMORY_INFO
	struct dentry *minidump_dir = NULL;
	int ret;

	ret = md_minidump_memory_init();
	if (ret) {
		pr_err("Failed to look up all minidump memory symbols, rc: %d\n", ret);
		return;
	}

	md_register_panic_entries(MD_MEMINFO_PAGES, "MEMINFO",
				  &md_meminfo_seq_buf);
#ifdef CONFIG_SLUB_DEBUG
	md_register_panic_entries(MD_SLABINFO_PAGES, "SLABINFO",
				  &md_slabinfo_seq_buf);
#endif
	if (!minidump_dir)
		minidump_dir = debugfs_create_dir("minidump", NULL);
#ifdef CONFIG_PAGE_OWNER
	if (is_page_owner_enabled()) {
		md_register_memory_dump(md_pageowner_dump_size, "PAGEOWNER");
		md_debugfs_pageowner(minidump_dir);
	}
#endif
#ifdef CONFIG_SLUB_DEBUG
	if (is_slub_debug_enabled()) {
		md_register_memory_dump(md_slabowner_dump_size, "SLABOWNER");
		md_debugfs_slabowner(minidump_dir);
	}
#endif
	md_register_memory_dump(md_dma_buf_info_size, "DMA_INFO");
	md_debugfs_dmabufinfo(minidump_dir);
	md_register_memory_dump(md_dma_buf_procs_size, "DMA_PROC");
	md_debugfs_dmabufprocs(minidump_dir);
#endif
#ifdef CONFIG_QCOM_MINIDUMP_PANIC_CPU_CONTEXT
	md_register_panic_entries(MD_CPU_CNTXT_PAGES, "KCNTXT",
				  &md_cntxt_seq_buf);
	register_trace_android_vh_ipi_stop(md_ipi_stop, NULL);
#endif
	md_register_panic_entries(MD_RUNQUEUE_PAGES, "KRUNQUEUE",
				  &md_runq_seq_buf);
}

static int register_vmap_mem(const char *name, void *virual_addr, size_t dump_len)
{
	int to_dump;
	u64 phys_addr;
	char entry_name[12];
	void *dump_addr = virual_addr;
	int i = 0;

	while (dump_len) {
		to_dump = min(dump_len, PAGE_SIZE - offset_in_page(dump_addr));
		phys_addr = page_to_phys(vmalloc_to_page((const void *)dump_addr));
		snprintf(entry_name, sizeof(entry_name), "%d_%s", i, name);
		md_register_minidump_entry(entry_name, (u64)dump_addr, phys_addr, to_dump);
		dump_addr += to_dump;
		dump_len -= to_dump;
		i++;
	}

	return 0;
}

struct module_sect_attr {
	struct bin_attribute battr;
	unsigned long address;
};

struct module_sect_attrs {
	struct attribute_group grp;
	unsigned int nsections;
	struct module_sect_attr attrs[];
};

static int md_module_process(struct module *mod)
{
	int i;
	bool is_key_module = false;
	unsigned long sec_addr, base_addr;
	unsigned long dump_start, dump_end;

	for (i = 0; i < n_modump; i++) {
		if (strcmp(key_modules[i], mod->name) == 0)
			is_key_module = true;
	}

	if (md_mod_info_seq_buf) {
		base_addr = (unsigned long)mod->core_layout.base;
		seq_buf_printf(md_mod_info_seq_buf, "name: %s, base: %lx, nplt: %d",
				mod->name, base_addr, mod->arch.core.plt_max_entries +
				1 + NR_FTRACE_PLTS);
		if (is_key_module) {
			dump_start = base_addr +
					mod->core_layout.ro_after_init_size;
			dump_end = base_addr + mod->core_layout.size;
			if (((dump_end - dump_start) / PAGE_SIZE) <
				msm_minidump_get_available_region()) {
				for (i = 0; i < mod->sect_attrs->nsections ; i++) {
					sec_addr = mod->sect_attrs->attrs[i].address;
					if (sec_addr >= dump_start && sec_addr < dump_end) {
						seq_buf_printf(md_mod_info_seq_buf, ", %s: %lx",
							mod->sect_attrs->attrs[i].battr.attr.name,
									sec_addr);
					}
				}
				register_vmap_mem(mod->name, (void *)dump_start,
						(dump_end - dump_start));
			} else
				pr_err("Failed to dump module %s\n", mod->name);
		}
		seq_buf_printf(md_mod_info_seq_buf, "\n");
	}

	return 0;
}

static int md_module_notify(struct notifier_block *self,
			    unsigned long val, void *data)
{
	struct module *mod = data;

	spin_lock(&md_modules_lock);
	if (mod->state == MODULE_STATE_LIVE)
		md_module_process(mod);
	spin_unlock(&md_modules_lock);
	return 0;
}

static struct notifier_block md_module_nb = {
	.notifier_call = md_module_notify,
};

static void md_register_module_data(void)
{
	int ret;
	struct module *module;
	struct list_head *module_list;

	ret = md_register_panic_entries(MD_MODULE_PAGES, "KMODULES",
					&md_mod_info_seq_buf);
	if (ret) {
		pr_err("Failed to register minidump module buffer\n");
		return;
	}

	seq_buf_printf(md_mod_info_seq_buf, "=== MODULE INFO ===\n");
	ret = register_module_notifier(&md_module_nb);
	if (ret) {
		pr_err("Failed to register minidump module notifier\n");
		return;
	}

	module_list = DEBUG_SYMBOL_LOOKUP(modules);
	if (IS_ERR_OR_NULL(module_list))
		return;
	preempt_disable();
	list_for_each_entry_rcu(module, module_list, list) {
		if (module != THIS_MODULE)
			md_module_process(module);
	}
	preempt_enable();
}

struct freq_log {
	uint64_t ktime;
	uint64_t freq;
};

struct freq_hist {
	uint32_t idx;
	struct freq_log log[FREQ_LOG_MAX];
};

static int max_cluster;
static struct freq_hist *cpuclk_log;

static void log_cpu_freq(void *unused,
			struct cpufreq_policy *policy,
			unsigned int *target_freq,
			unsigned int old_target_freq)
{
	uint32_t index;
	int cluster = topology_cluster_id(policy->cpu);

	if (cluster > max_cluster)
		return;
	index = cpuclk_log[cluster].idx;
	cpuclk_log[cluster].log[index].ktime = sched_clock();
	cpuclk_log[cluster].log[index].freq = *target_freq;
	cpuclk_log[cluster].idx = (index + 1) % FREQ_LOG_MAX;
}

static void register_cpufreq_log(void)
{
	int cpu;
	struct md_region md_entry;
	size_t freq_hist_sz;

	for_each_possible_cpu(cpu) {
		if (topology_cluster_id(cpu) > max_cluster)
			max_cluster = topology_cluster_id(cpu);
	}
	freq_hist_sz = sizeof(struct freq_hist) * (max_cluster + 1);

	cpuclk_log = kzalloc(freq_hist_sz, GFP_KERNEL);

	strscpy(md_entry.name, "FREQ_LOG", sizeof(md_entry.name));
	md_entry.virt_addr = (uintptr_t)cpuclk_log;
	md_entry.phys_addr = virt_to_phys(cpuclk_log);
	md_entry.size = freq_hist_sz;

	if (msm_minidump_add_region(&md_entry) < 0)
		pr_err("Failed to add pmsg in Minidump\n");

	register_trace_android_vh_cpufreq_resolve_freq(log_cpu_freq, NULL);
	register_trace_android_vh_cpufreq_fast_switch(log_cpu_freq, NULL);
	register_trace_android_vh_cpufreq_target(log_cpu_freq, NULL);
}
#endif

#ifdef CONFIG_QCOM_MINIDUMP_PSTORE
static void register_pstore_info(void)
{
	int ret;
	struct device_node *node;
	struct resource resource;
	struct reserved_mem *rmem = NULL;
	unsigned int size;
	phys_addr_t paddr;
	unsigned long total_size;
	struct md_region md_entry;

	node = of_find_compatible_node(NULL, NULL, "ramoops");
	if (IS_ERR_OR_NULL(node)) {
		pr_err("Failed to get pstore node\n");
		return;
	}

	ret = of_address_to_resource(node, 0, &resource);
	if (ret) {
		rmem = of_reserved_mem_lookup(node);
		if (rmem) {
			paddr = rmem->base;
			total_size = rmem->size;
		} else {
			pr_err("Failed to get pstore mem\n");
			return;
		}
	} else {
		paddr = resource.start;
		total_size = resource_size(&resource);
	}

	ret = of_property_read_u32(node, "record-size", &size);
	if (!ret && size > 0) {
		strscpy(md_entry.name, "KDMESG", sizeof(md_entry.name));
		md_entry.virt_addr = (uintptr_t)phys_to_virt(paddr);
		md_entry.phys_addr = paddr;
		md_entry.size = size;

		if (msm_minidump_add_region(&md_entry) < 0)
			pr_err("Failed to add dmesg in Minidump\n");

		paddr += size;
	}

	ret = of_property_read_u32(node, "console-size", &size);
	if (!ret && size > 0) {
		strscpy(md_entry.name, "KCONSOLE", sizeof(md_entry.name));
		md_entry.virt_addr = (uintptr_t)phys_to_virt(paddr);
		md_entry.phys_addr = paddr;
		md_entry.size = size;

		if (msm_minidump_add_region(&md_entry) < 0)
			pr_err("Failed to add console in Minidump\n");

		paddr += size;
	}

	ret = of_property_read_u32(node, "ftrace-size", &size);
	if (!ret && size > 0) {
		strscpy(md_entry.name, "KFTRACE", sizeof(md_entry.name));
		md_entry.virt_addr = (uintptr_t)phys_to_virt(paddr);
		md_entry.phys_addr = paddr;
		md_entry.size = size;

		if (msm_minidump_add_region(&md_entry) < 0)
			pr_err("Failed to add ftrace in Minidump\n");

		paddr += size;
	}

	ret = of_property_read_u32(node, "pmsg-size", &size);
	if (!ret && size > 0) {
		strscpy(md_entry.name, "KPMSG", sizeof(md_entry.name));
		md_entry.virt_addr = (uintptr_t)phys_to_virt(paddr);
		md_entry.phys_addr = paddr;
		md_entry.size = size;

		if (msm_minidump_add_region(&md_entry) < 0)
			pr_err("Failed to add pmsg in Minidump\n");

		paddr += size;
	}
}
#endif

int msm_minidump_log_init(void)
{
	register_kernel_sections();
	is_vmap_stack = IS_ENABLED(CONFIG_VMAP_STACK);
	register_irq_stack();
#ifdef CONFIG_QCOM_DYN_MINIDUMP_STACK
	register_current_stack();
	register_suspend_context();
#endif
#ifdef CONFIG_QCOM_MINIDUMP_PSTORE
	register_pstore_info();
#endif
#ifdef CONFIG_QCOM_MINIDUMP_FTRACE
	md_register_trace_buf();
#endif
#ifdef CONFIG_QCOM_MINIDUMP_PANIC_DUMP
	register_cpufreq_log();
	md_register_module_data();
	md_register_panic_data();
	atomic_notifier_chain_register(&panic_notifier_list, &md_panic_blk);
#ifdef CONFIG_QCOM_MINIDUMP_PANIC_CPU_CONTEXT
	register_die_notifier(&md_die_context_nb);
#endif
#endif
	return 0;
}
