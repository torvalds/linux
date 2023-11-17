// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
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
#include <soc/rockchip/rk_minidump.h>
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
#include <linux/android_debug_symbols.h>
#include <linux/elf.h>
#include <linux/seq_buf.h>
#include <linux/elfcore.h>
#include "minidump_private.h"

#ifdef CONFIG_ROCKCHIP_MINIDUMP_PANIC_DUMP
#include <linux/bits.h>
#include <linux/sched/prio.h>

#include "../../../kernel/sched/sched.h"

#include <linux/kdebug.h>
#include <linux/thread_info.h>
#include <asm/ptrace.h>
#include <linux/uaccess.h>
#include <linux/percpu.h>

#include <linux/module.h>
#include <linux/cma.h>
#include <linux/dma-map-ops.h>
#include <asm-generic/irq_regs.h>
#ifdef CONFIG_ROCKCHIP_MINIDUMP_PANIC_CPU_CONTEXT
#include <trace/hooks/debug.h>
#endif
#include "minidump_memory.h"
#endif	/* CONFIG_ROCKCHIP_MINIDUMP_PANIC_DUMP */

#ifdef CONFIG_ROCKCHIP_DYN_MINIDUMP_STACK

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
#endif	/* CONFIG_ROCKCHIP_DYN_MINIDUMP_STACK */

static bool is_vmap_stack __read_mostly;

#ifdef CONFIG_ROCKCHIP_MINIDUMP_FTRACE
#include <trace/hooks/ftrace_dump.h>
#include <linux/ring_buffer.h>

#define MD_FTRACE_BUF_SIZE	SZ_2M

static char *md_ftrace_buf_addr;
static size_t md_ftrace_buf_current;
static bool minidump_ftrace_in_oops;
static bool minidump_ftrace_dump = true;
#endif

#ifdef CONFIG_ROCKCHIP_MINIDUMP_PANIC_DUMP
/* Rnqueue information */
#define MD_RUNQUEUE_PAGES	8

static bool md_in_oops_handler;
static struct seq_buf *md_runq_seq_buf;
static int md_align_offset;

/* CPU context information */
#ifdef CONFIG_ROCKCHIP_MINIDUMP_PANIC_CPU_CONTEXT
#define MD_CPU_CNTXT_PAGES	32

static int die_cpu = -1;
static struct seq_buf *md_cntxt_seq_buf;
#endif

/* Meminfo */
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

/* Modules information */
#ifdef CONFIG_MODULES
#define MD_MODULE_PAGES	  8
static struct seq_buf *md_mod_info_seq_buf;
static DEFINE_SPINLOCK(md_modules_lock);
#endif	/* CONFIG_MODULES */
#endif

static struct md_region note_md_entry;
static DEFINE_PER_CPU_SHARED_ALIGNED(struct elf_prstatus *, cpu_epr);
static struct elf_prstatus *epr_hang_task[8];

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

	entry = rk_minidump_add_region(ksp_entry);
	if (entry < 0)
		pr_err("Failed to add stack of entry %s in Minidump\n",
				ksp_entry->name);
	return entry;
}

#ifdef CONFIG_ANDROID_DEBUG_SYMBOLS
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

	_sdata = android_debug_symbol(ADS_SDATA);
	__bss_stop = android_debug_symbol(ADS_BSS_END);
	base = android_debug_symbol(ADS_PER_CPU_START);
	static_size = (size_t)(android_debug_symbol(ADS_PER_CPU_END) - base);

	strscpy(ksec_entry.name, data_name, sizeof(ksec_entry.name));
	ksec_entry.virt_addr = (u64)_sdata;
	ksec_entry.phys_addr = virt_to_phys(_sdata);
	ksec_entry.size = roundup((__bss_stop - _sdata), 4);
	if (rk_minidump_add_region(&ksec_entry) < 0)
		pr_err("Failed to add data section in Minidump\n");

	start_ro = android_debug_symbol(ADS_START_RO_AFTER_INIT);
	end_ro = android_debug_symbol(ADS_END_RO_AFTER_INIT);
	strscpy(ksec_entry.name, rodata_name, sizeof(ksec_entry.name));
	ksec_entry.virt_addr = (uintptr_t)start_ro;
	ksec_entry.phys_addr = virt_to_phys(start_ro);
	ksec_entry.size = roundup((end_ro - start_ro), 4);
	if (rk_minidump_add_region(&ksec_entry) < 0)
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
		if (rk_minidump_add_region(&ksec_entry) < 0)
			pr_err("Failed to add percpu sections in Minidump\n");
	}
}
#endif

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

	if (IS_ENABLED(CONFIG_ROCKCHIP_DYN_MINIDUMP_STACK))
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
	if (rk_minidump_add_region(&ktsk_entry) < 0)
		pr_err("Failed to add current task %d in Minidump\n", cpu);
}

#ifdef CONFIG_ROCKCHIP_DYN_MINIDUMP_STACK
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
	if (rk_minidump_update_region(mdno, ksp_entry) < 0) {
		pr_err_ratelimited(
			"Failed to update stack entry %s in minidump\n",
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

static void update_md_cpu_stack(u32 cpu, u64 sp)
{
	struct md_stack_cpu_data *md_stack_cpu_d = &per_cpu(md_stack_data, cpu);

	if (!md_current_stack_init)
		return;

	update_md_stack(md_stack_cpu_d->stack_mdr,
			md_stack_cpu_d->stack_mdidx, sp);
}

static void md_current_stack_notifer(void *ignore, bool preempt,
		struct task_struct *prev, struct task_struct *next)
{
	u32 cpu = task_cpu(next);
	u64 sp = (u64)next->stack;

	update_md_cpu_stack(cpu, sp);
}

static void md_current_stack_ipi_handler(void *data)
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
	update_md_cpu_stack(cpu, sp);
}

static void update_md_current_task(struct md_region *mdr, int mdno)
{
	mdr->virt_addr = (u64)current;
	mdr->phys_addr = virt_to_phys((uintptr_t *)current);
	if (rk_minidump_update_region(mdno, mdr) < 0)
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
	char name_str[MD_MAX_NAME_LENGTH];

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
	char name_str[MD_MAX_NAME_LENGTH];
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
	*mdno = rk_minidump_add_region(mdr);
	if (*mdno < 0)
		pr_err("Failed to add current task %s in Minidump\n",
		       mdr->name);
}

static void register_suspend_current_task(void)
{
	char name_str[MD_MAX_NAME_LENGTH];

	scnprintf(name_str, sizeof(name_str), "KSUSPTASK");
	register_current_task(&md_suspend_context.task_mdr,
			&md_suspend_context.task_mdno, name_str);
}

#if !defined(MODULE) && defined(CONFIG_ARM64)
static void register_irq_stacks(void)
{
	struct md_region md_entry;
	int cpu, ret;
	struct page *sp_page;

	for_each_possible_cpu(cpu) {
		scnprintf(md_entry.name, sizeof(md_entry.name), "KIRQSTACK%d", cpu);
		md_entry.virt_addr = (u64)per_cpu(irq_stack_ptr, cpu);

		if (is_vmap_stack) {
			sp_page = vmalloc_to_page((const void *) md_entry.virt_addr);
			md_entry.phys_addr = page_to_phys(sp_page);
		} else {
			md_entry.phys_addr = virt_to_phys((const volatile void *)md_entry.virt_addr);
		}

		md_entry.size = IRQ_STACK_SIZE;
		ret = rk_minidump_add_region(&md_entry);
		if (ret < 0)
			pr_err("Failed to add %s entry in Minidump\n", md_entry.name);
	}
}
#else
static inline void register_irq_stacks(void)
{
}
#endif

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
#endif	/* CONFIG_ROCKCHIP_DYN_MINIDUMP_STACK */

static Elf_Word *append_elf_note(Elf_Word *buf, char *name, unsigned int type,
			  size_t data_len)
{
	struct elf_note *note = (struct elf_note *)buf;

	note->n_namesz = strlen(name) + 1;
	note->n_descsz = data_len;
	note->n_type   = type;
	buf += DIV_ROUND_UP(sizeof(*note), sizeof(Elf_Word));
	memcpy(buf, name, note->n_namesz);
	buf += DIV_ROUND_UP(note->n_namesz, sizeof(Elf_Word));
	return buf;
}

static void register_note_section(void)
{
	int ret = 0, i = 0, j = 0;
	size_t data_len;
	Elf_Word *buf;
	void *buffer_start;
	struct elf_prstatus *epr;
	struct user_pt_regs *regs;
	struct md_region *mdr = &note_md_entry;

	buffer_start = kzalloc(PAGE_SIZE * 2, GFP_KERNEL);
	if (!buffer_start)
		return;

	memcpy(mdr->name, "note", 5);
	mdr->virt_addr = (uintptr_t)buffer_start;
	mdr->phys_addr = virt_to_phys(buffer_start);

	buf = (Elf_Word *)mdr->virt_addr;
	data_len = sizeof(struct elf_prstatus);

	for_each_possible_cpu(i) {
		buf = append_elf_note(buf, "CORE", NT_PRSTATUS, data_len);
		epr = (struct elf_prstatus *)buf;
		epr->pr_pid = i;
		per_cpu(cpu_epr, i) = epr;
		regs = (struct user_pt_regs *)&epr->pr_reg;
		regs->pc = (u64)register_note_section; /* just for fun */

		buf += DIV_ROUND_UP(data_len, sizeof(Elf_Word));
	}

	j = i;
	for (; i < 16; i++) {
		buf = append_elf_note(buf, "TASK", NT_PRSTATUS, data_len);
		epr = (struct elf_prstatus *)buf;
		epr->pr_pid = i;
		epr_hang_task[i - j] = epr;
		regs = (struct user_pt_regs *)&epr->pr_reg;
		regs->pc = (u64)register_note_section; /* just for fun */
		buf += DIV_ROUND_UP(data_len, sizeof(Elf_Word));
	}

	mdr->size = (u64)buf - mdr->virt_addr;
	rk_md_flush_dcache_area((void *)mdr->virt_addr, mdr->size);
	ret = rk_minidump_add_region(mdr);
	if (ret < 0)
		pr_err("Failed to add %s entry in Minidump\n", mdr->name);
}

static int md_register_minidump_entry(char *name, u64 virt_addr,
				      u64 phys_addr, u64 size)
{
	struct md_region md_entry;
	int ret;

	strscpy(md_entry.name, name, sizeof(md_entry.name));
	md_entry.virt_addr = virt_addr;
	md_entry.phys_addr = phys_addr;
	md_entry.size = size;
	ret = rk_minidump_add_region(&md_entry);
	if (ret < 0)
		pr_err("Failed to add %s entry in Minidump\n", name);
	return ret;
}

static struct page *md_vmalloc_to_page(const void *vmalloc_addr)
{
	unsigned long addr = (unsigned long) vmalloc_addr;
	struct page *page = NULL;
	pgd_t *pgd = pgd_offset_k(addr);
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;

	if (pgd_none(*pgd))
		return NULL;
	p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d))
		return NULL;
	pud = pud_offset(p4d, addr);

	if (pud_none(*pud) || pud_bad(*pud))
		return NULL;
	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		return NULL;

	ptep = pte_offset_map(pmd, addr);
	pte = *ptep;
	if (pte_present(pte))
		page = pte_page(pte);
	pte_unmap(ptep);
	return page;
}

static bool md_is_kernel_address(u64 addr)
{
	u32 data;
	u64 phys_addr = 0;
	struct page *page;

	if (!is_ttbr1_addr(addr))
		return false;

	if (addr >= (u64)_text && addr < (u64)_end)
		return false;

	if (__is_lm_address(addr)) {
		phys_addr = virt_to_phys((void *)addr);
	} else if (is_vmalloc_or_module_addr((const void *)addr)) {
		page = md_vmalloc_to_page((const void *) addr);
		if (page)
			phys_addr = page_to_phys(page);
		else
			return false;
	} else {
		return false;
	}

	if (!md_is_ddr_address(phys_addr))
		return false;

	if (aarch64_insn_read((void *)addr, &data))
		return false;
	else
		return true;
}

static int md_save_page(u64 addr, bool flush)
{
	u64 phys_addr, virt_addr;
	struct page *page;
	char buf[32];
	int ret;

	if (md_is_kernel_address(addr)) {
		if (!md_is_in_the_region(addr)) {
			virt_addr = addr & PAGE_MASK;
			sprintf(buf, "%x", (u32)(virt_addr >> 12));

			if (__is_lm_address(virt_addr)) {
				phys_addr = virt_to_phys((void *)virt_addr);
			} else if (is_vmalloc_or_module_addr((const void *)virt_addr)) {
				page = md_vmalloc_to_page((const void *) virt_addr);
				phys_addr = page_to_phys(page);
			} else {
				return -1;
			}

			ret = md_register_minidump_entry(buf, (uintptr_t)virt_addr,
							 phys_addr, PAGE_SIZE);
			if (ret > 0 && flush)
				rk_md_flush_dcache_area((void *)virt_addr, PAGE_SIZE);
		} else {
			if (flush)
				rk_md_flush_dcache_area((void *)(addr & PAGE_MASK), PAGE_SIZE);
		}
		return 0;
	}
	return -1;
}

static void md_save_pages(u64 addr, bool flush)
{
	u64 *p, *end;

	if (!md_save_page(addr, flush)) {
		addr &= ~0x7;
		p = (u64 *)addr;
		end = (u64 *)((addr & ~(PAGE_SIZE - 1)) + PAGE_SIZE);
		while (p < end) {
			if (!md_is_kernel_address((u64)p))
				break;
			md_save_page(*p++, flush);
		}
	}
}

void rk_minidump_update_cpu_regs(struct pt_regs *regs)
{
	int cpu = raw_smp_processor_id();
	struct user_pt_regs *old_regs;
	int i = 0;

	struct elf_prstatus *epr = per_cpu(cpu_epr, cpu);

	if (!epr)
		return;

	if (system_state == SYSTEM_RESTART)
		return;

	old_regs = (struct user_pt_regs *)&epr->pr_reg;
	/* if epr has been saved, don't save it again in panic notifier*/
	if (old_regs->sp != 0)
		return;

	memcpy((void *)&epr->pr_reg, (void *)regs, sizeof(elf_gregset_t));
	rk_md_flush_dcache_area((void *)&epr->pr_reg, sizeof(elf_gregset_t));
	rk_md_flush_dcache_area((void *)(regs->sp & ~(PAGE_SIZE - 1)), PAGE_SIZE);

	/* dump sp */
	md_save_pages(regs->sp, true);

	/*dump x0-x28, x29 is lr, x30 is fp*/
	for (i = 0; i < 29; i++)
		md_save_pages(regs->regs[i], true);
}
EXPORT_SYMBOL(rk_minidump_update_cpu_regs);

#ifdef CONFIG_ROCKCHIP_MINIDUMP_FTRACE
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
	if (rk_minidump_add_region(&md_entry) < 0)
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

	/* Complete registration before adding entries */
	smp_mb();
	WRITE_ONCE(md_ftrace_buf_addr, buffer_start);
}
#endif

#ifdef CONFIG_ROCKCHIP_MINIDUMP_PANIC_DUMP
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
			       "[status: curr] pid: %d comm: %s preempt: %#llx\n",
			       task_pid_nr(task), task->comm,
			       (u64)task->thread_info.preempt_count);
		return;
	}

	seq_buf_printf(md_runq_seq_buf,
		       "[status: %s] pid: %d tsk: %#lx comm: %s stack: %#lx",
		       status, task_pid_nr(task),
		       (unsigned long)task,
		       task->comm,
		       (unsigned long)task->stack);
	seq_buf_printf(md_runq_seq_buf,
		       " prio: %d aff: %*pb",
		       task->prio, cpumask_pr_args(&task->cpus_mask));
#ifdef CONFIG_SCHED_WALT
	seq_buf_printf(md_runq_seq_buf, " enq: %lu wake: %lu sleep: %lu",
		       task->wts.last_enqueued_ts, task->wts.last_wake_ts,
		       task->wts.last_sleep_ts);
#endif
	seq_buf_printf(md_runq_seq_buf,
		       " vrun: %lu arr: %lu sum_ex: %lu\n",
		       (unsigned long)se->vruntime,
		       (unsigned long)se->exec_start,
		       (unsigned long)se->sum_exec_runtime);
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

static void md_dump_runqueues(void)
{
	int cpu;
	struct rq *rq;
	struct rt_rq  *rt;
	struct cfs_rq *cfs;

	if (!md_runq_seq_buf)
		return;

	for_each_possible_cpu(cpu) {
		rq = cpu_rq(cpu);
		rt = &rq->rt;
		cfs = &rq->cfs;
		seq_buf_printf(md_runq_seq_buf,
			       "CPU%d %d process is running\n",
			       cpu, rq->nr_running);
		md_dump_task_info(cpu_curr(cpu), "curr", NULL);
		seq_buf_printf(md_runq_seq_buf,
			       "CFS %d process is pending\n",
			       cfs->nr_running);
		md_dump_cfs_rq(cfs, cpu_curr(cpu));
		seq_buf_printf(md_runq_seq_buf,
			       "RT %d process is pending\n",
			       rt->rt_nr_running);
		md_dump_rt_rq(rt, cpu_curr(cpu));
		seq_buf_printf(md_runq_seq_buf, "\n");
	}

	rk_md_flush_dcache_area((void *)md_runq_seq_buf->buffer, md_runq_seq_buf->len);
}

#ifdef CONFIG_ROCKCHIP_MINIDUMP_PANIC_CPU_CONTEXT
/*
 * dump a block of kernel memory from around the given address.
 * Bulk of the code is lifted from arch/arm64/kernel/process.c.
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
	mm_segment_t fs;
	unsigned int i;
	int nbytes = 128;

	if (user_mode(regs) ||  !regs->pc)
		return;

	rk_minidump_update_cpu_regs(regs);
	fs = get_fs();
	set_fs(KERNEL_DS);
	md_dump_data(regs->pc - nbytes, nbytes * 2, "PC");
	md_dump_data(regs->regs[30] - nbytes, nbytes * 2, "LR");
	md_dump_data(regs->sp - nbytes, nbytes * 2, "SP");
	for (i = 0; i < 30; i++) {
		char name[4];

		snprintf(name, sizeof(name), "X%u", i);
		md_dump_data(regs->regs[i] - nbytes, nbytes * 2, name);
	}
	set_fs(fs);
	rk_md_flush_dcache_area((void *)md_cntxt_seq_buf->buffer, md_cntxt_seq_buf->len);
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
	if (in_interrupt())
		md_reg_context_data(get_irq_regs());
	else
		md_reg_context_data(&regs);
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
	.priority = INT_MAX - 2, /* < rk watchdog die notifier */
};
#endif

static int rk_minidump_collect_hang_task(void)
{
	struct task_struct *g, *p;
	struct elf_prstatus *epr;
	struct user_pt_regs *regs;
	int idx = 0, i = 0;

	for_each_process_thread(g, p) {
		touch_nmi_watchdog();
		touch_all_softlockup_watchdogs();
		if (p->state == TASK_UNINTERRUPTIBLE && p->state != TASK_IDLE) {
			epr = epr_hang_task[idx++];
			regs = (struct user_pt_regs *)&epr->pr_reg;
			regs->regs[19] = (unsigned long)(p->thread.cpu_context.x19);
			regs->regs[20] = (unsigned long)(p->thread.cpu_context.x20);
			regs->regs[21] = (unsigned long)(p->thread.cpu_context.x21);
			regs->regs[22] = (unsigned long)(p->thread.cpu_context.x22);
			regs->regs[23] = (unsigned long)(p->thread.cpu_context.x23);
			regs->regs[24] = (unsigned long)(p->thread.cpu_context.x24);
			regs->regs[25] = (unsigned long)(p->thread.cpu_context.x25);
			regs->regs[26] = (unsigned long)(p->thread.cpu_context.x26);
			regs->regs[27] = (unsigned long)(p->thread.cpu_context.x27);
			regs->regs[28] = (unsigned long)(p->thread.cpu_context.x28);
			regs->regs[29] = (unsigned long)(p->thread.cpu_context.fp);
			regs->sp = (unsigned long)(p->thread.cpu_context.sp);
			regs->pc = (unsigned long)p->thread.cpu_context.pc;
			md_save_pages(regs->sp, true);
			for (i = 19; i < 29; i++)
				md_save_pages(regs->regs[i], true);
			rk_md_flush_dcache_area((void *)epr, sizeof(struct elf_prstatus));
		}
		if (idx >= 8)
			return 0;
	}
	return 0;
}

static int md_panic_handler(struct notifier_block *this,
			    unsigned long event, void *ptr)
{
	if (md_in_oops_handler)
		return NOTIFY_DONE;
	md_in_oops_handler = true;
#ifdef CONFIG_ROCKCHIP_MINIDUMP_PANIC_CPU_CONTEXT
	if (!md_cntxt_seq_buf)
		goto dump_rq;
	if (raw_smp_processor_id() != die_cpu)
		md_dump_panic_regs();
dump_rq:
#endif
	md_dump_runqueues();
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

	rk_minidump_collect_hang_task();

	rk_minidump_flush_elfheader();
	md_in_oops_handler = false;
	return NOTIFY_DONE;
}

static struct notifier_block md_panic_blk = {
	.notifier_call = md_panic_handler,
	.priority = INT_MAX - 2,
};

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
	struct dentry *minidump_dir = NULL;

	md_register_panic_entries(MD_RUNQUEUE_PAGES, "KRUNQUEUE",
				  &md_runq_seq_buf);
#ifdef CONFIG_ROCKCHIP_MINIDUMP_PANIC_CPU_CONTEXT
	md_register_panic_entries(MD_CPU_CNTXT_PAGES, "KCNTXT",
				  &md_cntxt_seq_buf);
#endif
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
	md_register_memory_dump(md_dma_buf_info_size, "DMABUF_INFO");
	md_debugfs_dmabufinfo(minidump_dir);
	md_register_memory_dump(md_dma_buf_procs_size, "DMABUF_PROCS");
	md_debugfs_dmabufprocs(minidump_dir);
}

static int print_module(const char *name, void *mod_addr, void *data)
{
	if (!md_mod_info_seq_buf) {
		pr_err("md_mod_info_seq_buf is NULL\n");
		return -EINVAL;
	}

	seq_buf_printf(md_mod_info_seq_buf, "name: %s, base: %#lx\n", name, (uintptr_t)mod_addr);
	return 0;
}

static int md_module_notify(struct notifier_block *self,
			    unsigned long val, void *data)
{
	struct module *mod = data;

	spin_lock(&md_modules_lock);
	switch (mod->state) {
	case MODULE_STATE_LIVE:
		print_module(mod->name, mod->core_layout.base, data);
		break;
	case MODULE_STATE_GOING:
		print_module(mod->name, mod->core_layout.base, data);
		break;
	default:
		break;
	}
	spin_unlock(&md_modules_lock);
	return 0;
}

static struct notifier_block md_module_nb = {
	.notifier_call = md_module_notify,
};

static void md_register_module_data(void)
{
	int ret;

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

	android_debug_for_each_module(print_module, NULL);
}
#endif /* CONFIG_ROCKCHIP_MINIDUMP_PANIC_DUMP */

#ifdef CONFIG_HARDLOCKUP_DETECTOR
int rk_minidump_hardlock_notify(struct notifier_block *nb, unsigned long event,
				void *p)
{
	struct elf_prstatus *epr;
	struct user_pt_regs *regs;
	unsigned long hardlock_cpu = event;
#ifdef CONFIG_ROCKCHIP_DYN_MINIDUMP_STACK
	int i = 0;
	struct md_stack_cpu_data *md_stack_cpu_d;
	struct md_region *mdr;
#endif

	if (hardlock_cpu >= num_possible_cpus())
		return NOTIFY_DONE;

#ifdef CONFIG_ROCKCHIP_DYN_MINIDUMP_STACK
	md_stack_cpu_d = &per_cpu(md_stack_data, hardlock_cpu);
	for (i = 0; i < STACK_NUM_PAGES; i++) {
		mdr = &md_stack_cpu_d->stack_mdr[i];
		if (md_is_kernel_address(mdr->virt_addr))
			rk_md_flush_dcache_area((void *)mdr->virt_addr, mdr->size);
	}
#endif
	epr = per_cpu(cpu_epr, hardlock_cpu);
	if (!epr)
		return NOTIFY_DONE;
	regs = (struct user_pt_regs *)&epr->pr_reg;
	regs->pc = (u64)p;
#ifdef CONFIG_ROCKCHIP_DYN_MINIDUMP_STACK
	regs->sp = mdr->virt_addr + mdr->size;
#endif
	rk_md_flush_dcache_area((void *)epr, sizeof(struct elf_prstatus));
	return NOTIFY_OK;
}
#endif

int rk_minidump_log_init(void)
{
	is_vmap_stack = IS_ENABLED(CONFIG_VMAP_STACK);

	register_note_section();
#ifdef CONFIG_ANDROID_DEBUG_SYMBOLS
	register_kernel_sections();
#endif

#ifdef CONFIG_ROCKCHIP_DYN_MINIDUMP_STACK
	register_current_stack();
	register_suspend_context();
	register_irq_stacks();
#endif

#ifdef CONFIG_ROCKCHIP_MINIDUMP_FTRACE
	md_register_trace_buf();
#endif

#ifdef CONFIG_ROCKCHIP_MINIDUMP_PANIC_DUMP
	md_register_module_data();
	md_register_panic_data();
	atomic_notifier_chain_register(&panic_notifier_list, &md_panic_blk);
#ifdef CONFIG_ROCKCHIP_MINIDUMP_PANIC_CPU_CONTEXT
	register_die_notifier(&md_die_context_nb);
#endif
#endif
	return 0;
}
