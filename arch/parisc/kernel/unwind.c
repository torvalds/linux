// SPDX-License-Identifier: GPL-2.0
/*
 * Kernel unwinding support
 *
 * (c) 2002-2004 Randolph Chung <tausq@debian.org>
 *
 * Derived partially from the IA64 implementation. The PA-RISC
 * Runtime Architecture Document is also a useful reference to
 * understand what is happening here
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/kallsyms.h>
#include <linux/sort.h>

#include <linux/uaccess.h>
#include <asm/assembly.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>

#include <asm/unwind.h>

/* #define DEBUG 1 */
#ifdef DEBUG
#define dbg(x...) pr_debug(x)
#else
#define dbg(x...)
#endif

#define KERNEL_START (KERNEL_BINARY_TEXT_START)

extern struct unwind_table_entry __start___unwind[];
extern struct unwind_table_entry __stop___unwind[];

static DEFINE_SPINLOCK(unwind_lock);
/*
 * the kernel unwind block is not dynamically allocated so that
 * we can call unwind_init as early in the bootup process as 
 * possible (before the slab allocator is initialized)
 */
static struct unwind_table kernel_unwind_table __read_mostly;
static LIST_HEAD(unwind_tables);

static inline const struct unwind_table_entry *
find_unwind_entry_in_table(const struct unwind_table *table, unsigned long addr)
{
	const struct unwind_table_entry *e = NULL;
	unsigned long lo, hi, mid;

	lo = 0; 
	hi = table->length - 1; 
	
	while (lo <= hi) {
		mid = (hi - lo) / 2 + lo;
		e = &table->table[mid];
		if (addr < e->region_start)
			hi = mid - 1;
		else if (addr > e->region_end)
			lo = mid + 1;
		else
			return e;
	}

	return NULL;
}

static const struct unwind_table_entry *
find_unwind_entry(unsigned long addr)
{
	struct unwind_table *table;
	const struct unwind_table_entry *e = NULL;

	if (addr >= kernel_unwind_table.start && 
	    addr <= kernel_unwind_table.end)
		e = find_unwind_entry_in_table(&kernel_unwind_table, addr);
	else {
		unsigned long flags;

		spin_lock_irqsave(&unwind_lock, flags);
		list_for_each_entry(table, &unwind_tables, list) {
			if (addr >= table->start && 
			    addr <= table->end)
				e = find_unwind_entry_in_table(table, addr);
			if (e) {
				/* Move-to-front to exploit common traces */
				list_move(&table->list, &unwind_tables);
				break;
			}
		}
		spin_unlock_irqrestore(&unwind_lock, flags);
	}

	return e;
}

static void
unwind_table_init(struct unwind_table *table, const char *name,
		  unsigned long base_addr, unsigned long gp,
		  void *table_start, void *table_end)
{
	struct unwind_table_entry *start = table_start;
	struct unwind_table_entry *end = 
		(struct unwind_table_entry *)table_end - 1;

	table->name = name;
	table->base_addr = base_addr;
	table->gp = gp;
	table->start = base_addr + start->region_start;
	table->end = base_addr + end->region_end;
	table->table = (struct unwind_table_entry *)table_start;
	table->length = end - start + 1;
	INIT_LIST_HEAD(&table->list);

	for (; start <= end; start++) {
		if (start < end && 
		    start->region_end > (start+1)->region_start) {
			printk("WARNING: Out of order unwind entry! %p and %p\n", start, start+1);
		}

		start->region_start += base_addr;
		start->region_end += base_addr;
	}
}

static int cmp_unwind_table_entry(const void *a, const void *b)
{
	return ((const struct unwind_table_entry *)a)->region_start
	     - ((const struct unwind_table_entry *)b)->region_start;
}

static void
unwind_table_sort(struct unwind_table_entry *start,
		  struct unwind_table_entry *finish)
{
	sort(start, finish - start, sizeof(struct unwind_table_entry),
	     cmp_unwind_table_entry, NULL);
}

struct unwind_table *
unwind_table_add(const char *name, unsigned long base_addr, 
		 unsigned long gp,
                 void *start, void *end)
{
	struct unwind_table *table;
	unsigned long flags;
	struct unwind_table_entry *s = (struct unwind_table_entry *)start;
	struct unwind_table_entry *e = (struct unwind_table_entry *)end;

	unwind_table_sort(s, e);

	table = kmalloc(sizeof(struct unwind_table), GFP_USER);
	if (table == NULL)
		return NULL;
	unwind_table_init(table, name, base_addr, gp, start, end);
	spin_lock_irqsave(&unwind_lock, flags);
	list_add_tail(&table->list, &unwind_tables);
	spin_unlock_irqrestore(&unwind_lock, flags);

	return table;
}

void unwind_table_remove(struct unwind_table *table)
{
	unsigned long flags;

	spin_lock_irqsave(&unwind_lock, flags);
	list_del(&table->list);
	spin_unlock_irqrestore(&unwind_lock, flags);

	kfree(table);
}

/* Called from setup_arch to import the kernel unwind info */
int __init unwind_init(void)
{
	long start, stop;
	register unsigned long gp __asm__ ("r27");

	start = (long)&__start___unwind[0];
	stop = (long)&__stop___unwind[0];

	dbg("unwind_init: start = 0x%lx, end = 0x%lx, entries = %lu\n",
	    start, stop,
	    (stop - start) / sizeof(struct unwind_table_entry));

	unwind_table_init(&kernel_unwind_table, "kernel", KERNEL_START,
			  gp, 
			  &__start___unwind[0], &__stop___unwind[0]);
#if 0
	{
		int i;
		for (i = 0; i < 10; i++)
		{
			printk("region 0x%x-0x%x\n", 
				__start___unwind[i].region_start, 
				__start___unwind[i].region_end);
		}
	}
#endif
	return 0;
}

#ifdef CONFIG_64BIT
#define get_func_addr(fptr) fptr[2]
#else
#define get_func_addr(fptr) fptr[0]
#endif

static int unwind_special(struct unwind_frame_info *info, unsigned long pc, int frame_size)
{
	extern void handle_interruption(int, struct pt_regs *);
	static unsigned long *hi = (unsigned long *)&handle_interruption;

	if (pc == get_func_addr(hi)) {
		struct pt_regs *regs = (struct pt_regs *)(info->sp - frame_size - PT_SZ_ALGN);
		dbg("Unwinding through handle_interruption()\n");
		info->prev_sp = regs->gr[30];
		info->prev_ip = regs->iaoq[0];

		return 1;
	}

	return 0;
}

static void unwind_frame_regs(struct unwind_frame_info *info)
{
	const struct unwind_table_entry *e;
	unsigned long npc;
	unsigned int insn;
	long frame_size = 0;
	int looking_for_rp, rpoffset = 0;

	e = find_unwind_entry(info->ip);
	if (e == NULL) {
		unsigned long sp;

		dbg("Cannot find unwind entry for 0x%lx; forced unwinding\n", info->ip);

#ifdef CONFIG_KALLSYMS
		/* Handle some frequent special cases.... */
		{
			char symname[KSYM_NAME_LEN];
			char *modname;

			kallsyms_lookup(info->ip, NULL, NULL, &modname,
				symname);

			dbg("info->ip = 0x%lx, name = %s\n", info->ip, symname);

			if (strcmp(symname, "_switch_to_ret") == 0) {
				info->prev_sp = info->sp - CALLEE_SAVE_FRAME_SIZE;
				info->prev_ip = *(unsigned long *)(info->prev_sp - RP_OFFSET);
				dbg("_switch_to_ret @ %lx - setting "
				    "prev_sp=%lx prev_ip=%lx\n", 
				    info->ip, info->prev_sp, 
				    info->prev_ip);
				return;
			} else if (strcmp(symname, "ret_from_kernel_thread") == 0 ||
				   strcmp(symname, "syscall_exit") == 0) {
				info->prev_ip = info->prev_sp = 0;
				return;
			}
		}
#endif

		/* Since we are doing the unwinding blind, we don't know if
		   we are adjusting the stack correctly or extracting the rp
		   correctly. The rp is checked to see if it belongs to the
		   kernel text section, if not we assume we don't have a 
		   correct stack frame and we continue to unwind the stack.
		   This is not quite correct, and will fail for loadable
		   modules. */
		sp = info->sp & ~63;
		do {
			unsigned long tmp;

			info->prev_sp = sp - 64;
			info->prev_ip = 0;

			/* The stack is at the end inside the thread_union
			 * struct. If we reach data, we have reached the
			 * beginning of the stack and should stop unwinding. */
			if (info->prev_sp >= (unsigned long) task_thread_info(info->t) &&
			    info->prev_sp < ((unsigned long) task_thread_info(info->t)
						+ THREAD_SZ_ALGN)) {
				info->prev_sp = 0;
				break;
			}

			if (get_user(tmp, (unsigned long *)(info->prev_sp - RP_OFFSET))) 
				break;
			info->prev_ip = tmp;
			sp = info->prev_sp;
		} while (!kernel_text_address(info->prev_ip));

		info->rp = 0;

		dbg("analyzing func @ %lx with no unwind info, setting "
		    "prev_sp=%lx prev_ip=%lx\n", info->ip, 
		    info->prev_sp, info->prev_ip);
	} else {
		dbg("e->start = 0x%x, e->end = 0x%x, Save_SP = %d, "
		    "Save_RP = %d, Millicode = %d size = %u\n", 
		    e->region_start, e->region_end, e->Save_SP, e->Save_RP, 
		    e->Millicode, e->Total_frame_size);

		looking_for_rp = e->Save_RP;

		for (npc = e->region_start; 
		     (frame_size < (e->Total_frame_size << 3) || 
		      looking_for_rp) && 
		     npc < info->ip; 
		     npc += 4) {

			insn = *(unsigned int *)npc;

			if ((insn & 0xffffc001) == 0x37de0000 ||
			    (insn & 0xffe00001) == 0x6fc00000) {
				/* ldo X(sp), sp, or stwm X,D(sp) */
				frame_size += (insn & 0x3fff) >> 1;
				dbg("analyzing func @ %lx, insn=%08x @ "
				    "%lx, frame_size = %ld\n", info->ip,
				    insn, npc, frame_size);
			} else if ((insn & 0xffe00009) == 0x73c00008) {
				/* std,ma X,D(sp) */
				frame_size += ((insn >> 4) & 0x3ff) << 3;
				dbg("analyzing func @ %lx, insn=%08x @ "
				    "%lx, frame_size = %ld\n", info->ip,
				    insn, npc, frame_size);
			} else if (insn == 0x6bc23fd9) { 
				/* stw rp,-20(sp) */
				rpoffset = 20;
				looking_for_rp = 0;
				dbg("analyzing func @ %lx, insn=stw rp,"
				    "-20(sp) @ %lx\n", info->ip, npc);
			} else if (insn == 0x0fc212c1) {
				/* std rp,-16(sr0,sp) */
				rpoffset = 16;
				looking_for_rp = 0;
				dbg("analyzing func @ %lx, insn=std rp,"
				    "-16(sp) @ %lx\n", info->ip, npc);
			}
		}

		if (frame_size > e->Total_frame_size << 3)
			frame_size = e->Total_frame_size << 3;

		if (!unwind_special(info, e->region_start, frame_size)) {
			info->prev_sp = info->sp - frame_size;
			if (e->Millicode)
				info->rp = info->r31;
			else if (rpoffset)
				info->rp = *(unsigned long *)(info->prev_sp - rpoffset);
			info->prev_ip = info->rp;
			info->rp = 0;
		}

		dbg("analyzing func @ %lx, setting prev_sp=%lx "
		    "prev_ip=%lx npc=%lx\n", info->ip, info->prev_sp, 
		    info->prev_ip, npc);
	}
}

void unwind_frame_init(struct unwind_frame_info *info, struct task_struct *t, 
		       struct pt_regs *regs)
{
	memset(info, 0, sizeof(struct unwind_frame_info));
	info->t = t;
	info->sp = regs->gr[30];
	info->ip = regs->iaoq[0];
	info->rp = regs->gr[2];
	info->r31 = regs->gr[31];

	dbg("(%d) Start unwind from sp=%08lx ip=%08lx\n", 
	    t ? (int)t->pid : -1, info->sp, info->ip);
}

void unwind_frame_init_from_blocked_task(struct unwind_frame_info *info, struct task_struct *t)
{
	struct pt_regs *r = &t->thread.regs;
	struct pt_regs *r2;

	r2 = kmalloc(sizeof(struct pt_regs), GFP_ATOMIC);
	if (!r2)
		return;
	*r2 = *r;
	r2->gr[30] = r->ksp;
	r2->iaoq[0] = r->kpc;
	unwind_frame_init(info, t, r2);
	kfree(r2);
}

void unwind_frame_init_running(struct unwind_frame_info *info, struct pt_regs *regs)
{
	unwind_frame_init(info, current, regs);
}

int unwind_once(struct unwind_frame_info *next_frame)
{
	unwind_frame_regs(next_frame);

	if (next_frame->prev_sp == 0 ||
	    next_frame->prev_ip == 0)
		return -1;

	next_frame->sp = next_frame->prev_sp;
	next_frame->ip = next_frame->prev_ip;
	next_frame->prev_sp = 0;
	next_frame->prev_ip = 0;

	dbg("(%d) Continue unwind to sp=%08lx ip=%08lx\n", 
	    next_frame->t ? (int)next_frame->t->pid : -1, 
	    next_frame->sp, next_frame->ip);

	return 0;
}

int unwind_to_user(struct unwind_frame_info *info)
{
	int ret;
	
	do {
		ret = unwind_once(info);
	} while (!ret && !(info->ip & 3));

	return ret;
}

unsigned long return_address(unsigned int level)
{
	struct unwind_frame_info info;
	struct pt_regs r;
	unsigned long sp;

	/* initialize unwind info */
	asm volatile ("copy %%r30, %0" : "=r"(sp));
	memset(&r, 0, sizeof(struct pt_regs));
	r.iaoq[0] = (unsigned long) current_text_addr();
	r.gr[2] = (unsigned long) __builtin_return_address(0);
	r.gr[30] = sp;
	unwind_frame_init(&info, current, &r);

	/* unwind stack */
	++level;
	do {
		if (unwind_once(&info) < 0 || info.ip == 0)
			return 0;
		if (!kernel_text_address(info.ip))
			return 0;
	} while (info.ip && level--);

	return info.ip;
}
