/* smp.c: Sparc64 SMP support.
 *
 * Copyright (C) 1997, 2007, 2008 David S. Miller (davem@davemloft.net)
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/sched/mm.h>
#include <linux/sched/hotplug.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/cache.h>
#include <linux/jiffies.h>
#include <linux/profile.h>
#include <linux/bootmem.h>
#include <linux/vmalloc.h>
#include <linux/ftrace.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/kgdb.h>

#include <asm/head.h>
#include <asm/ptrace.h>
#include <linux/atomic.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/cpudata.h>
#include <asm/hvtramp.h>
#include <asm/io.h>
#include <asm/timer.h>
#include <asm/setup.h>

#include <asm/irq.h>
#include <asm/irq_regs.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <linux/uaccess.h>
#include <asm/starfire.h>
#include <asm/tlb.h>
#include <asm/sections.h>
#include <asm/prom.h>
#include <asm/mdesc.h>
#include <asm/ldc.h>
#include <asm/hypervisor.h>
#include <asm/pcr.h>

#include "cpumap.h"
#include "kernel.h"

DEFINE_PER_CPU(cpumask_t, cpu_sibling_map) = CPU_MASK_NONE;
cpumask_t cpu_core_map[NR_CPUS] __read_mostly =
	{ [0 ... NR_CPUS-1] = CPU_MASK_NONE };

cpumask_t cpu_core_sib_map[NR_CPUS] __read_mostly = {
	[0 ... NR_CPUS-1] = CPU_MASK_NONE };

cpumask_t cpu_core_sib_cache_map[NR_CPUS] __read_mostly = {
	[0 ... NR_CPUS - 1] = CPU_MASK_NONE };

EXPORT_PER_CPU_SYMBOL(cpu_sibling_map);
EXPORT_SYMBOL(cpu_core_map);
EXPORT_SYMBOL(cpu_core_sib_map);
EXPORT_SYMBOL(cpu_core_sib_cache_map);

static cpumask_t smp_commenced_mask;

void smp_info(struct seq_file *m)
{
	int i;
	
	seq_printf(m, "State:\n");
	for_each_online_cpu(i)
		seq_printf(m, "CPU%d:\t\tonline\n", i);
}

void smp_bogo(struct seq_file *m)
{
	int i;
	
	for_each_online_cpu(i)
		seq_printf(m,
			   "Cpu%dClkTck\t: %016lx\n",
			   i, cpu_data(i).clock_tick);
}

extern void setup_sparc64_timer(void);

static volatile unsigned long callin_flag = 0;

void smp_callin(void)
{
	int cpuid = hard_smp_processor_id();

	__local_per_cpu_offset = __per_cpu_offset(cpuid);

	if (tlb_type == hypervisor)
		sun4v_ktsb_register();

	__flush_tlb_all();

	setup_sparc64_timer();

	if (cheetah_pcache_forced_on)
		cheetah_enable_pcache();

	callin_flag = 1;
	__asm__ __volatile__("membar #Sync\n\t"
			     "flush  %%g6" : : : "memory");

	/* Clear this or we will die instantly when we
	 * schedule back to this idler...
	 */
	current_thread_info()->new_child = 0;

	/* Attach to the address space of init_task. */
	mmgrab(&init_mm);
	current->active_mm = &init_mm;

	/* inform the notifiers about the new cpu */
	notify_cpu_starting(cpuid);

	while (!cpumask_test_cpu(cpuid, &smp_commenced_mask))
		rmb();

	set_cpu_online(cpuid, true);

	/* idle thread is expected to have preempt disabled */
	preempt_disable();

	local_irq_enable();

	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

void cpu_panic(void)
{
	printk("CPU[%d]: Returns from cpu_idle!\n", smp_processor_id());
	panic("SMP bolixed\n");
}

/* This tick register synchronization scheme is taken entirely from
 * the ia64 port, see arch/ia64/kernel/smpboot.c for details and credit.
 *
 * The only change I've made is to rework it so that the master
 * initiates the synchonization instead of the slave. -DaveM
 */

#define MASTER	0
#define SLAVE	(SMP_CACHE_BYTES/sizeof(unsigned long))

#define NUM_ROUNDS	64	/* magic value */
#define NUM_ITERS	5	/* likewise */

static DEFINE_RAW_SPINLOCK(itc_sync_lock);
static unsigned long go[SLAVE + 1];

#define DEBUG_TICK_SYNC	0

static inline long get_delta (long *rt, long *master)
{
	unsigned long best_t0 = 0, best_t1 = ~0UL, best_tm = 0;
	unsigned long tcenter, t0, t1, tm;
	unsigned long i;

	for (i = 0; i < NUM_ITERS; i++) {
		t0 = tick_ops->get_tick();
		go[MASTER] = 1;
		membar_safe("#StoreLoad");
		while (!(tm = go[SLAVE]))
			rmb();
		go[SLAVE] = 0;
		wmb();
		t1 = tick_ops->get_tick();

		if (t1 - t0 < best_t1 - best_t0)
			best_t0 = t0, best_t1 = t1, best_tm = tm;
	}

	*rt = best_t1 - best_t0;
	*master = best_tm - best_t0;

	/* average best_t0 and best_t1 without overflow: */
	tcenter = (best_t0/2 + best_t1/2);
	if (best_t0 % 2 + best_t1 % 2 == 2)
		tcenter++;
	return tcenter - best_tm;
}

void smp_synchronize_tick_client(void)
{
	long i, delta, adj, adjust_latency = 0, done = 0;
	unsigned long flags, rt, master_time_stamp;
#if DEBUG_TICK_SYNC
	struct {
		long rt;	/* roundtrip time */
		long master;	/* master's timestamp */
		long diff;	/* difference between midpoint and master's timestamp */
		long lat;	/* estimate of itc adjustment latency */
	} t[NUM_ROUNDS];
#endif

	go[MASTER] = 1;

	while (go[MASTER])
		rmb();

	local_irq_save(flags);
	{
		for (i = 0; i < NUM_ROUNDS; i++) {
			delta = get_delta(&rt, &master_time_stamp);
			if (delta == 0)
				done = 1;	/* let's lock on to this... */

			if (!done) {
				if (i > 0) {
					adjust_latency += -delta;
					adj = -delta + adjust_latency/4;
				} else
					adj = -delta;

				tick_ops->add_tick(adj);
			}
#if DEBUG_TICK_SYNC
			t[i].rt = rt;
			t[i].master = master_time_stamp;
			t[i].diff = delta;
			t[i].lat = adjust_latency/4;
#endif
		}
	}
	local_irq_restore(flags);

#if DEBUG_TICK_SYNC
	for (i = 0; i < NUM_ROUNDS; i++)
		printk("rt=%5ld master=%5ld diff=%5ld adjlat=%5ld\n",
		       t[i].rt, t[i].master, t[i].diff, t[i].lat);
#endif

	printk(KERN_INFO "CPU %d: synchronized TICK with master CPU "
	       "(last diff %ld cycles, maxerr %lu cycles)\n",
	       smp_processor_id(), delta, rt);
}

static void smp_start_sync_tick_client(int cpu);

static void smp_synchronize_one_tick(int cpu)
{
	unsigned long flags, i;

	go[MASTER] = 0;

	smp_start_sync_tick_client(cpu);

	/* wait for client to be ready */
	while (!go[MASTER])
		rmb();

	/* now let the client proceed into his loop */
	go[MASTER] = 0;
	membar_safe("#StoreLoad");

	raw_spin_lock_irqsave(&itc_sync_lock, flags);
	{
		for (i = 0; i < NUM_ROUNDS*NUM_ITERS; i++) {
			while (!go[MASTER])
				rmb();
			go[MASTER] = 0;
			wmb();
			go[SLAVE] = tick_ops->get_tick();
			membar_safe("#StoreLoad");
		}
	}
	raw_spin_unlock_irqrestore(&itc_sync_lock, flags);
}

#if defined(CONFIG_SUN_LDOMS) && defined(CONFIG_HOTPLUG_CPU)
static void ldom_startcpu_cpuid(unsigned int cpu, unsigned long thread_reg,
				void **descrp)
{
	extern unsigned long sparc64_ttable_tl0;
	extern unsigned long kern_locked_tte_data;
	struct hvtramp_descr *hdesc;
	unsigned long trampoline_ra;
	struct trap_per_cpu *tb;
	u64 tte_vaddr, tte_data;
	unsigned long hv_err;
	int i;

	hdesc = kzalloc(sizeof(*hdesc) +
			(sizeof(struct hvtramp_mapping) *
			 num_kernel_image_mappings - 1),
			GFP_KERNEL);
	if (!hdesc) {
		printk(KERN_ERR "ldom_startcpu_cpuid: Cannot allocate "
		       "hvtramp_descr.\n");
		return;
	}
	*descrp = hdesc;

	hdesc->cpu = cpu;
	hdesc->num_mappings = num_kernel_image_mappings;

	tb = &trap_block[cpu];

	hdesc->fault_info_va = (unsigned long) &tb->fault_info;
	hdesc->fault_info_pa = kimage_addr_to_ra(&tb->fault_info);

	hdesc->thread_reg = thread_reg;

	tte_vaddr = (unsigned long) KERNBASE;
	tte_data = kern_locked_tte_data;

	for (i = 0; i < hdesc->num_mappings; i++) {
		hdesc->maps[i].vaddr = tte_vaddr;
		hdesc->maps[i].tte   = tte_data;
		tte_vaddr += 0x400000;
		tte_data  += 0x400000;
	}

	trampoline_ra = kimage_addr_to_ra(hv_cpu_startup);

	hv_err = sun4v_cpu_start(cpu, trampoline_ra,
				 kimage_addr_to_ra(&sparc64_ttable_tl0),
				 __pa(hdesc));
	if (hv_err)
		printk(KERN_ERR "ldom_startcpu_cpuid: sun4v_cpu_start() "
		       "gives error %lu\n", hv_err);
}
#endif

extern unsigned long sparc64_cpu_startup;

/* The OBP cpu startup callback truncates the 3rd arg cookie to
 * 32-bits (I think) so to be safe we have it read the pointer
 * contained here so we work on >4GB machines. -DaveM
 */
static struct thread_info *cpu_new_thread = NULL;

static int smp_boot_one_cpu(unsigned int cpu, struct task_struct *idle)
{
	unsigned long entry =
		(unsigned long)(&sparc64_cpu_startup);
	unsigned long cookie =
		(unsigned long)(&cpu_new_thread);
	void *descr = NULL;
	int timeout, ret;

	callin_flag = 0;
	cpu_new_thread = task_thread_info(idle);

	if (tlb_type == hypervisor) {
#if defined(CONFIG_SUN_LDOMS) && defined(CONFIG_HOTPLUG_CPU)
		if (ldom_domaining_enabled)
			ldom_startcpu_cpuid(cpu,
					    (unsigned long) cpu_new_thread,
					    &descr);
		else
#endif
			prom_startcpu_cpuid(cpu, entry, cookie);
	} else {
		struct device_node *dp = of_find_node_by_cpuid(cpu);

		prom_startcpu(dp->phandle, entry, cookie);
	}

	for (timeout = 0; timeout < 50000; timeout++) {
		if (callin_flag)
			break;
		udelay(100);
	}

	if (callin_flag) {
		ret = 0;
	} else {
		printk("Processor %d is stuck.\n", cpu);
		ret = -ENODEV;
	}
	cpu_new_thread = NULL;

	kfree(descr);

	return ret;
}

static void spitfire_xcall_helper(u64 data0, u64 data1, u64 data2, u64 pstate, unsigned long cpu)
{
	u64 result, target;
	int stuck, tmp;

	if (this_is_starfire) {
		/* map to real upaid */
		cpu = (((cpu & 0x3c) << 1) |
			((cpu & 0x40) >> 4) |
			(cpu & 0x3));
	}

	target = (cpu << 14) | 0x70;
again:
	/* Ok, this is the real Spitfire Errata #54.
	 * One must read back from a UDB internal register
	 * after writes to the UDB interrupt dispatch, but
	 * before the membar Sync for that write.
	 * So we use the high UDB control register (ASI 0x7f,
	 * ADDR 0x20) for the dummy read. -DaveM
	 */
	tmp = 0x40;
	__asm__ __volatile__(
	"wrpr	%1, %2, %%pstate\n\t"
	"stxa	%4, [%0] %3\n\t"
	"stxa	%5, [%0+%8] %3\n\t"
	"add	%0, %8, %0\n\t"
	"stxa	%6, [%0+%8] %3\n\t"
	"membar	#Sync\n\t"
	"stxa	%%g0, [%7] %3\n\t"
	"membar	#Sync\n\t"
	"mov	0x20, %%g1\n\t"
	"ldxa	[%%g1] 0x7f, %%g0\n\t"
	"membar	#Sync"
	: "=r" (tmp)
	: "r" (pstate), "i" (PSTATE_IE), "i" (ASI_INTR_W),
	  "r" (data0), "r" (data1), "r" (data2), "r" (target),
	  "r" (0x10), "0" (tmp)
        : "g1");

	/* NOTE: PSTATE_IE is still clear. */
	stuck = 100000;
	do {
		__asm__ __volatile__("ldxa [%%g0] %1, %0"
			: "=r" (result)
			: "i" (ASI_INTR_DISPATCH_STAT));
		if (result == 0) {
			__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
					     : : "r" (pstate));
			return;
		}
		stuck -= 1;
		if (stuck == 0)
			break;
	} while (result & 0x1);
	__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
			     : : "r" (pstate));
	if (stuck == 0) {
		printk("CPU[%d]: mondo stuckage result[%016llx]\n",
		       smp_processor_id(), result);
	} else {
		udelay(2);
		goto again;
	}
}

static void spitfire_xcall_deliver(struct trap_per_cpu *tb, int cnt)
{
	u64 *mondo, data0, data1, data2;
	u16 *cpu_list;
	u64 pstate;
	int i;

	__asm__ __volatile__("rdpr %%pstate, %0" : "=r" (pstate));
	cpu_list = __va(tb->cpu_list_pa);
	mondo = __va(tb->cpu_mondo_block_pa);
	data0 = mondo[0];
	data1 = mondo[1];
	data2 = mondo[2];
	for (i = 0; i < cnt; i++)
		spitfire_xcall_helper(data0, data1, data2, pstate, cpu_list[i]);
}

/* Cheetah now allows to send the whole 64-bytes of data in the interrupt
 * packet, but we have no use for that.  However we do take advantage of
 * the new pipelining feature (ie. dispatch to multiple cpus simultaneously).
 */
static void cheetah_xcall_deliver(struct trap_per_cpu *tb, int cnt)
{
	int nack_busy_id, is_jbus, need_more;
	u64 *mondo, pstate, ver, busy_mask;
	u16 *cpu_list;

	cpu_list = __va(tb->cpu_list_pa);
	mondo = __va(tb->cpu_mondo_block_pa);

	/* Unfortunately, someone at Sun had the brilliant idea to make the
	 * busy/nack fields hard-coded by ITID number for this Ultra-III
	 * derivative processor.
	 */
	__asm__ ("rdpr %%ver, %0" : "=r" (ver));
	is_jbus = ((ver >> 32) == __JALAPENO_ID ||
		   (ver >> 32) == __SERRANO_ID);

	__asm__ __volatile__("rdpr %%pstate, %0" : "=r" (pstate));

retry:
	need_more = 0;
	__asm__ __volatile__("wrpr %0, %1, %%pstate\n\t"
			     : : "r" (pstate), "i" (PSTATE_IE));

	/* Setup the dispatch data registers. */
	__asm__ __volatile__("stxa	%0, [%3] %6\n\t"
			     "stxa	%1, [%4] %6\n\t"
			     "stxa	%2, [%5] %6\n\t"
			     "membar	#Sync\n\t"
			     : /* no outputs */
			     : "r" (mondo[0]), "r" (mondo[1]), "r" (mondo[2]),
			       "r" (0x40), "r" (0x50), "r" (0x60),
			       "i" (ASI_INTR_W));

	nack_busy_id = 0;
	busy_mask = 0;
	{
		int i;

		for (i = 0; i < cnt; i++) {
			u64 target, nr;

			nr = cpu_list[i];
			if (nr == 0xffff)
				continue;

			target = (nr << 14) | 0x70;
			if (is_jbus) {
				busy_mask |= (0x1UL << (nr * 2));
			} else {
				target |= (nack_busy_id << 24);
				busy_mask |= (0x1UL <<
					      (nack_busy_id * 2));
			}
			__asm__ __volatile__(
				"stxa	%%g0, [%0] %1\n\t"
				"membar	#Sync\n\t"
				: /* no outputs */
				: "r" (target), "i" (ASI_INTR_W));
			nack_busy_id++;
			if (nack_busy_id == 32) {
				need_more = 1;
				break;
			}
		}
	}

	/* Now, poll for completion. */
	{
		u64 dispatch_stat, nack_mask;
		long stuck;

		stuck = 100000 * nack_busy_id;
		nack_mask = busy_mask << 1;
		do {
			__asm__ __volatile__("ldxa	[%%g0] %1, %0"
					     : "=r" (dispatch_stat)
					     : "i" (ASI_INTR_DISPATCH_STAT));
			if (!(dispatch_stat & (busy_mask | nack_mask))) {
				__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
						     : : "r" (pstate));
				if (unlikely(need_more)) {
					int i, this_cnt = 0;
					for (i = 0; i < cnt; i++) {
						if (cpu_list[i] == 0xffff)
							continue;
						cpu_list[i] = 0xffff;
						this_cnt++;
						if (this_cnt == 32)
							break;
					}
					goto retry;
				}
				return;
			}
			if (!--stuck)
				break;
		} while (dispatch_stat & busy_mask);

		__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
				     : : "r" (pstate));

		if (dispatch_stat & busy_mask) {
			/* Busy bits will not clear, continue instead
			 * of freezing up on this cpu.
			 */
			printk("CPU[%d]: mondo stuckage result[%016llx]\n",
			       smp_processor_id(), dispatch_stat);
		} else {
			int i, this_busy_nack = 0;

			/* Delay some random time with interrupts enabled
			 * to prevent deadlock.
			 */
			udelay(2 * nack_busy_id);

			/* Clear out the mask bits for cpus which did not
			 * NACK us.
			 */
			for (i = 0; i < cnt; i++) {
				u64 check_mask, nr;

				nr = cpu_list[i];
				if (nr == 0xffff)
					continue;

				if (is_jbus)
					check_mask = (0x2UL << (2*nr));
				else
					check_mask = (0x2UL <<
						      this_busy_nack);
				if ((dispatch_stat & check_mask) == 0)
					cpu_list[i] = 0xffff;
				this_busy_nack += 2;
				if (this_busy_nack == 64)
					break;
			}

			goto retry;
		}
	}
}

/* Multi-cpu list version.  */
static void hypervisor_xcall_deliver(struct trap_per_cpu *tb, int cnt)
{
	int retries, this_cpu, prev_sent, i, saw_cpu_error;
	unsigned long status;
	u16 *cpu_list;

	this_cpu = smp_processor_id();

	cpu_list = __va(tb->cpu_list_pa);

	saw_cpu_error = 0;
	retries = 0;
	prev_sent = 0;
	do {
		int forward_progress, n_sent;

		status = sun4v_cpu_mondo_send(cnt,
					      tb->cpu_list_pa,
					      tb->cpu_mondo_block_pa);

		/* HV_EOK means all cpus received the xcall, we're done.  */
		if (likely(status == HV_EOK))
			break;

		/* First, see if we made any forward progress.
		 *
		 * The hypervisor indicates successful sends by setting
		 * cpu list entries to the value 0xffff.
		 */
		n_sent = 0;
		for (i = 0; i < cnt; i++) {
			if (likely(cpu_list[i] == 0xffff))
				n_sent++;
		}

		forward_progress = 0;
		if (n_sent > prev_sent)
			forward_progress = 1;

		prev_sent = n_sent;

		/* If we get a HV_ECPUERROR, then one or more of the cpus
		 * in the list are in error state.  Use the cpu_state()
		 * hypervisor call to find out which cpus are in error state.
		 */
		if (unlikely(status == HV_ECPUERROR)) {
			for (i = 0; i < cnt; i++) {
				long err;
				u16 cpu;

				cpu = cpu_list[i];
				if (cpu == 0xffff)
					continue;

				err = sun4v_cpu_state(cpu);
				if (err == HV_CPU_STATE_ERROR) {
					saw_cpu_error = (cpu + 1);
					cpu_list[i] = 0xffff;
				}
			}
		} else if (unlikely(status != HV_EWOULDBLOCK))
			goto fatal_mondo_error;

		/* Don't bother rewriting the CPU list, just leave the
		 * 0xffff and non-0xffff entries in there and the
		 * hypervisor will do the right thing.
		 *
		 * Only advance timeout state if we didn't make any
		 * forward progress.
		 */
		if (unlikely(!forward_progress)) {
			if (unlikely(++retries > 10000))
				goto fatal_mondo_timeout;

			/* Delay a little bit to let other cpus catch up
			 * on their cpu mondo queue work.
			 */
			udelay(2 * cnt);
		}
	} while (1);

	if (unlikely(saw_cpu_error))
		goto fatal_mondo_cpu_error;

	return;

fatal_mondo_cpu_error:
	printk(KERN_CRIT "CPU[%d]: SUN4V mondo cpu error, some target cpus "
	       "(including %d) were in error state\n",
	       this_cpu, saw_cpu_error - 1);
	return;

fatal_mondo_timeout:
	printk(KERN_CRIT "CPU[%d]: SUN4V mondo timeout, no forward "
	       " progress after %d retries.\n",
	       this_cpu, retries);
	goto dump_cpu_list_and_out;

fatal_mondo_error:
	printk(KERN_CRIT "CPU[%d]: Unexpected SUN4V mondo error %lu\n",
	       this_cpu, status);
	printk(KERN_CRIT "CPU[%d]: Args were cnt(%d) cpulist_pa(%lx) "
	       "mondo_block_pa(%lx)\n",
	       this_cpu, cnt, tb->cpu_list_pa, tb->cpu_mondo_block_pa);

dump_cpu_list_and_out:
	printk(KERN_CRIT "CPU[%d]: CPU list [ ", this_cpu);
	for (i = 0; i < cnt; i++)
		printk("%u ", cpu_list[i]);
	printk("]\n");
}

static void (*xcall_deliver_impl)(struct trap_per_cpu *, int);

static void xcall_deliver(u64 data0, u64 data1, u64 data2, const cpumask_t *mask)
{
	struct trap_per_cpu *tb;
	int this_cpu, i, cnt;
	unsigned long flags;
	u16 *cpu_list;
	u64 *mondo;

	/* We have to do this whole thing with interrupts fully disabled.
	 * Otherwise if we send an xcall from interrupt context it will
	 * corrupt both our mondo block and cpu list state.
	 *
	 * One consequence of this is that we cannot use timeout mechanisms
	 * that depend upon interrupts being delivered locally.  So, for
	 * example, we cannot sample jiffies and expect it to advance.
	 *
	 * Fortunately, udelay() uses %stick/%tick so we can use that.
	 */
	local_irq_save(flags);

	this_cpu = smp_processor_id();
	tb = &trap_block[this_cpu];

	mondo = __va(tb->cpu_mondo_block_pa);
	mondo[0] = data0;
	mondo[1] = data1;
	mondo[2] = data2;
	wmb();

	cpu_list = __va(tb->cpu_list_pa);

	/* Setup the initial cpu list.  */
	cnt = 0;
	for_each_cpu(i, mask) {
		if (i == this_cpu || !cpu_online(i))
			continue;
		cpu_list[cnt++] = i;
	}

	if (cnt)
		xcall_deliver_impl(tb, cnt);

	local_irq_restore(flags);
}

/* Send cross call to all processors mentioned in MASK_P
 * except self.  Really, there are only two cases currently,
 * "cpu_online_mask" and "mm_cpumask(mm)".
 */
static void smp_cross_call_masked(unsigned long *func, u32 ctx, u64 data1, u64 data2, const cpumask_t *mask)
{
	u64 data0 = (((u64)ctx)<<32 | (((u64)func) & 0xffffffff));

	xcall_deliver(data0, data1, data2, mask);
}

/* Send cross call to all processors except self. */
static void smp_cross_call(unsigned long *func, u32 ctx, u64 data1, u64 data2)
{
	smp_cross_call_masked(func, ctx, data1, data2, cpu_online_mask);
}

extern unsigned long xcall_sync_tick;

static void smp_start_sync_tick_client(int cpu)
{
	xcall_deliver((u64) &xcall_sync_tick, 0, 0,
		      cpumask_of(cpu));
}

extern unsigned long xcall_call_function;

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	xcall_deliver((u64) &xcall_call_function, 0, 0, mask);
}

extern unsigned long xcall_call_function_single;

void arch_send_call_function_single_ipi(int cpu)
{
	xcall_deliver((u64) &xcall_call_function_single, 0, 0,
		      cpumask_of(cpu));
}

void __irq_entry smp_call_function_client(int irq, struct pt_regs *regs)
{
	clear_softint(1 << irq);
	irq_enter();
	generic_smp_call_function_interrupt();
	irq_exit();
}

void __irq_entry smp_call_function_single_client(int irq, struct pt_regs *regs)
{
	clear_softint(1 << irq);
	irq_enter();
	generic_smp_call_function_single_interrupt();
	irq_exit();
}

static void tsb_sync(void *info)
{
	struct trap_per_cpu *tp = &trap_block[raw_smp_processor_id()];
	struct mm_struct *mm = info;

	/* It is not valid to test "current->active_mm == mm" here.
	 *
	 * The value of "current" is not changed atomically with
	 * switch_mm().  But that's OK, we just need to check the
	 * current cpu's trap block PGD physical address.
	 */
	if (tp->pgd_paddr == __pa(mm->pgd))
		tsb_context_switch(mm);
}

void smp_tsb_sync(struct mm_struct *mm)
{
	smp_call_function_many(mm_cpumask(mm), tsb_sync, mm, 1);
}

extern unsigned long xcall_flush_tlb_mm;
extern unsigned long xcall_flush_tlb_page;
extern unsigned long xcall_flush_tlb_kernel_range;
extern unsigned long xcall_fetch_glob_regs;
extern unsigned long xcall_fetch_glob_pmu;
extern unsigned long xcall_fetch_glob_pmu_n4;
extern unsigned long xcall_receive_signal;
extern unsigned long xcall_new_mmu_context_version;
#ifdef CONFIG_KGDB
extern unsigned long xcall_kgdb_capture;
#endif

#ifdef DCACHE_ALIASING_POSSIBLE
extern unsigned long xcall_flush_dcache_page_cheetah;
#endif
extern unsigned long xcall_flush_dcache_page_spitfire;

static inline void __local_flush_dcache_page(struct page *page)
{
#ifdef DCACHE_ALIASING_POSSIBLE
	__flush_dcache_page(page_address(page),
			    ((tlb_type == spitfire) &&
			     page_mapping(page) != NULL));
#else
	if (page_mapping(page) != NULL &&
	    tlb_type == spitfire)
		__flush_icache_page(__pa(page_address(page)));
#endif
}

void smp_flush_dcache_page_impl(struct page *page, int cpu)
{
	int this_cpu;

	if (tlb_type == hypervisor)
		return;

#ifdef CONFIG_DEBUG_DCFLUSH
	atomic_inc(&dcpage_flushes);
#endif

	this_cpu = get_cpu();

	if (cpu == this_cpu) {
		__local_flush_dcache_page(page);
	} else if (cpu_online(cpu)) {
		void *pg_addr = page_address(page);
		u64 data0 = 0;

		if (tlb_type == spitfire) {
			data0 = ((u64)&xcall_flush_dcache_page_spitfire);
			if (page_mapping(page) != NULL)
				data0 |= ((u64)1 << 32);
		} else if (tlb_type == cheetah || tlb_type == cheetah_plus) {
#ifdef DCACHE_ALIASING_POSSIBLE
			data0 =	((u64)&xcall_flush_dcache_page_cheetah);
#endif
		}
		if (data0) {
			xcall_deliver(data0, __pa(pg_addr),
				      (u64) pg_addr, cpumask_of(cpu));
#ifdef CONFIG_DEBUG_DCFLUSH
			atomic_inc(&dcpage_flushes_xcall);
#endif
		}
	}

	put_cpu();
}

void flush_dcache_page_all(struct mm_struct *mm, struct page *page)
{
	void *pg_addr;
	u64 data0;

	if (tlb_type == hypervisor)
		return;

	preempt_disable();

#ifdef CONFIG_DEBUG_DCFLUSH
	atomic_inc(&dcpage_flushes);
#endif
	data0 = 0;
	pg_addr = page_address(page);
	if (tlb_type == spitfire) {
		data0 = ((u64)&xcall_flush_dcache_page_spitfire);
		if (page_mapping(page) != NULL)
			data0 |= ((u64)1 << 32);
	} else if (tlb_type == cheetah || tlb_type == cheetah_plus) {
#ifdef DCACHE_ALIASING_POSSIBLE
		data0 = ((u64)&xcall_flush_dcache_page_cheetah);
#endif
	}
	if (data0) {
		xcall_deliver(data0, __pa(pg_addr),
			      (u64) pg_addr, cpu_online_mask);
#ifdef CONFIG_DEBUG_DCFLUSH
		atomic_inc(&dcpage_flushes_xcall);
#endif
	}
	__local_flush_dcache_page(page);

	preempt_enable();
}

#ifdef CONFIG_KGDB
void kgdb_roundup_cpus(unsigned long flags)
{
	smp_cross_call(&xcall_kgdb_capture, 0, 0, 0);
}
#endif

void smp_fetch_global_regs(void)
{
	smp_cross_call(&xcall_fetch_glob_regs, 0, 0, 0);
}

void smp_fetch_global_pmu(void)
{
	if (tlb_type == hypervisor &&
	    sun4v_chip_type >= SUN4V_CHIP_NIAGARA4)
		smp_cross_call(&xcall_fetch_glob_pmu_n4, 0, 0, 0);
	else
		smp_cross_call(&xcall_fetch_glob_pmu, 0, 0, 0);
}

/* We know that the window frames of the user have been flushed
 * to the stack before we get here because all callers of us
 * are flush_tlb_*() routines, and these run after flush_cache_*()
 * which performs the flushw.
 *
 * The SMP TLB coherency scheme we use works as follows:
 *
 * 1) mm->cpu_vm_mask is a bit mask of which cpus an address
 *    space has (potentially) executed on, this is the heuristic
 *    we use to avoid doing cross calls.
 *
 *    Also, for flushing from kswapd and also for clones, we
 *    use cpu_vm_mask as the list of cpus to make run the TLB.
 *
 * 2) TLB context numbers are shared globally across all processors
 *    in the system, this allows us to play several games to avoid
 *    cross calls.
 *
 *    One invariant is that when a cpu switches to a process, and
 *    that processes tsk->active_mm->cpu_vm_mask does not have the
 *    current cpu's bit set, that tlb context is flushed locally.
 *
 *    If the address space is non-shared (ie. mm->count == 1) we avoid
 *    cross calls when we want to flush the currently running process's
 *    tlb state.  This is done by clearing all cpu bits except the current
 *    processor's in current->mm->cpu_vm_mask and performing the
 *    flush locally only.  This will force any subsequent cpus which run
 *    this task to flush the context from the local tlb if the process
 *    migrates to another cpu (again).
 *
 * 3) For shared address spaces (threads) and swapping we bite the
 *    bullet for most cases and perform the cross call (but only to
 *    the cpus listed in cpu_vm_mask).
 *
 *    The performance gain from "optimizing" away the cross call for threads is
 *    questionable (in theory the big win for threads is the massive sharing of
 *    address space state across processors).
 */

/* This currently is only used by the hugetlb arch pre-fault
 * hook on UltraSPARC-III+ and later when changing the pagesize
 * bits of the context register for an address space.
 */
void smp_flush_tlb_mm(struct mm_struct *mm)
{
	u32 ctx = CTX_HWBITS(mm->context);
	int cpu = get_cpu();

	if (atomic_read(&mm->mm_users) == 1) {
		cpumask_copy(mm_cpumask(mm), cpumask_of(cpu));
		goto local_flush_and_out;
	}

	smp_cross_call_masked(&xcall_flush_tlb_mm,
			      ctx, 0, 0,
			      mm_cpumask(mm));

local_flush_and_out:
	__flush_tlb_mm(ctx, SECONDARY_CONTEXT);

	put_cpu();
}

struct tlb_pending_info {
	unsigned long ctx;
	unsigned long nr;
	unsigned long *vaddrs;
};

static void tlb_pending_func(void *info)
{
	struct tlb_pending_info *t = info;

	__flush_tlb_pending(t->ctx, t->nr, t->vaddrs);
}

void smp_flush_tlb_pending(struct mm_struct *mm, unsigned long nr, unsigned long *vaddrs)
{
	u32 ctx = CTX_HWBITS(mm->context);
	struct tlb_pending_info info;
	int cpu = get_cpu();

	info.ctx = ctx;
	info.nr = nr;
	info.vaddrs = vaddrs;

	if (mm == current->mm && atomic_read(&mm->mm_users) == 1)
		cpumask_copy(mm_cpumask(mm), cpumask_of(cpu));
	else
		smp_call_function_many(mm_cpumask(mm), tlb_pending_func,
				       &info, 1);

	__flush_tlb_pending(ctx, nr, vaddrs);

	put_cpu();
}

void smp_flush_tlb_page(struct mm_struct *mm, unsigned long vaddr)
{
	unsigned long context = CTX_HWBITS(mm->context);
	int cpu = get_cpu();

	if (mm == current->mm && atomic_read(&mm->mm_users) == 1)
		cpumask_copy(mm_cpumask(mm), cpumask_of(cpu));
	else
		smp_cross_call_masked(&xcall_flush_tlb_page,
				      context, vaddr, 0,
				      mm_cpumask(mm));
	__flush_tlb_page(context, vaddr);

	put_cpu();
}

void smp_flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	start &= PAGE_MASK;
	end    = PAGE_ALIGN(end);
	if (start != end) {
		smp_cross_call(&xcall_flush_tlb_kernel_range,
			       0, start, end);

		__flush_tlb_kernel_range(start, end);
	}
}

/* CPU capture. */
/* #define CAPTURE_DEBUG */
extern unsigned long xcall_capture;

static atomic_t smp_capture_depth = ATOMIC_INIT(0);
static atomic_t smp_capture_registry = ATOMIC_INIT(0);
static unsigned long penguins_are_doing_time;

void smp_capture(void)
{
	int result = atomic_add_return(1, &smp_capture_depth);

	if (result == 1) {
		int ncpus = num_online_cpus();

#ifdef CAPTURE_DEBUG
		printk("CPU[%d]: Sending penguins to jail...",
		       smp_processor_id());
#endif
		penguins_are_doing_time = 1;
		atomic_inc(&smp_capture_registry);
		smp_cross_call(&xcall_capture, 0, 0, 0);
		while (atomic_read(&smp_capture_registry) != ncpus)
			rmb();
#ifdef CAPTURE_DEBUG
		printk("done\n");
#endif
	}
}

void smp_release(void)
{
	if (atomic_dec_and_test(&smp_capture_depth)) {
#ifdef CAPTURE_DEBUG
		printk("CPU[%d]: Giving pardon to "
		       "imprisoned penguins\n",
		       smp_processor_id());
#endif
		penguins_are_doing_time = 0;
		membar_safe("#StoreLoad");
		atomic_dec(&smp_capture_registry);
	}
}

/* Imprisoned penguins run with %pil == PIL_NORMAL_MAX, but PSTATE_IE
 * set, so they can service tlb flush xcalls...
 */
extern void prom_world(int);

void __irq_entry smp_penguin_jailcell(int irq, struct pt_regs *regs)
{
	clear_softint(1 << irq);

	preempt_disable();

	__asm__ __volatile__("flushw");
	prom_world(1);
	atomic_inc(&smp_capture_registry);
	membar_safe("#StoreLoad");
	while (penguins_are_doing_time)
		rmb();
	atomic_dec(&smp_capture_registry);
	prom_world(0);

	preempt_enable();
}

/* /proc/profile writes can call this, don't __init it please. */
int setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
}

void smp_prepare_boot_cpu(void)
{
}

void __init smp_setup_processor_id(void)
{
	if (tlb_type == spitfire)
		xcall_deliver_impl = spitfire_xcall_deliver;
	else if (tlb_type == cheetah || tlb_type == cheetah_plus)
		xcall_deliver_impl = cheetah_xcall_deliver;
	else
		xcall_deliver_impl = hypervisor_xcall_deliver;
}

void __init smp_fill_in_cpu_possible_map(void)
{
	int possible_cpus = num_possible_cpus();
	int i;

	if (possible_cpus > nr_cpu_ids)
		possible_cpus = nr_cpu_ids;

	for (i = 0; i < possible_cpus; i++)
		set_cpu_possible(i, true);
	for (; i < NR_CPUS; i++)
		set_cpu_possible(i, false);
}

void smp_fill_in_sib_core_maps(void)
{
	unsigned int i;

	for_each_present_cpu(i) {
		unsigned int j;

		cpumask_clear(&cpu_core_map[i]);
		if (cpu_data(i).core_id == 0) {
			cpumask_set_cpu(i, &cpu_core_map[i]);
			continue;
		}

		for_each_present_cpu(j) {
			if (cpu_data(i).core_id ==
			    cpu_data(j).core_id)
				cpumask_set_cpu(j, &cpu_core_map[i]);
		}
	}

	for_each_present_cpu(i)  {
		unsigned int j;

		for_each_present_cpu(j)  {
			if (cpu_data(i).max_cache_id ==
			    cpu_data(j).max_cache_id)
				cpumask_set_cpu(j, &cpu_core_sib_cache_map[i]);

			if (cpu_data(i).sock_id == cpu_data(j).sock_id)
				cpumask_set_cpu(j, &cpu_core_sib_map[i]);
		}
	}

	for_each_present_cpu(i) {
		unsigned int j;

		cpumask_clear(&per_cpu(cpu_sibling_map, i));
		if (cpu_data(i).proc_id == -1) {
			cpumask_set_cpu(i, &per_cpu(cpu_sibling_map, i));
			continue;
		}

		for_each_present_cpu(j) {
			if (cpu_data(i).proc_id ==
			    cpu_data(j).proc_id)
				cpumask_set_cpu(j, &per_cpu(cpu_sibling_map, i));
		}
	}
}

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	int ret = smp_boot_one_cpu(cpu, tidle);

	if (!ret) {
		cpumask_set_cpu(cpu, &smp_commenced_mask);
		while (!cpu_online(cpu))
			mb();
		if (!cpu_online(cpu)) {
			ret = -ENODEV;
		} else {
			/* On SUN4V, writes to %tick and %stick are
			 * not allowed.
			 */
			if (tlb_type != hypervisor)
				smp_synchronize_one_tick(cpu);
		}
	}
	return ret;
}

#ifdef CONFIG_HOTPLUG_CPU
void cpu_play_dead(void)
{
	int cpu = smp_processor_id();
	unsigned long pstate;

	idle_task_exit();

	if (tlb_type == hypervisor) {
		struct trap_per_cpu *tb = &trap_block[cpu];

		sun4v_cpu_qconf(HV_CPU_QUEUE_CPU_MONDO,
				tb->cpu_mondo_pa, 0);
		sun4v_cpu_qconf(HV_CPU_QUEUE_DEVICE_MONDO,
				tb->dev_mondo_pa, 0);
		sun4v_cpu_qconf(HV_CPU_QUEUE_RES_ERROR,
				tb->resum_mondo_pa, 0);
		sun4v_cpu_qconf(HV_CPU_QUEUE_NONRES_ERROR,
				tb->nonresum_mondo_pa, 0);
	}

	cpumask_clear_cpu(cpu, &smp_commenced_mask);
	membar_safe("#Sync");

	local_irq_disable();

	__asm__ __volatile__(
		"rdpr	%%pstate, %0\n\t"
		"wrpr	%0, %1, %%pstate"
		: "=r" (pstate)
		: "i" (PSTATE_IE));

	while (1)
		barrier();
}

int __cpu_disable(void)
{
	int cpu = smp_processor_id();
	cpuinfo_sparc *c;
	int i;

	for_each_cpu(i, &cpu_core_map[cpu])
		cpumask_clear_cpu(cpu, &cpu_core_map[i]);
	cpumask_clear(&cpu_core_map[cpu]);

	for_each_cpu(i, &per_cpu(cpu_sibling_map, cpu))
		cpumask_clear_cpu(cpu, &per_cpu(cpu_sibling_map, i));
	cpumask_clear(&per_cpu(cpu_sibling_map, cpu));

	c = &cpu_data(cpu);

	c->core_id = 0;
	c->proc_id = -1;

	smp_wmb();

	/* Make sure no interrupts point to this cpu.  */
	fixup_irqs();

	local_irq_enable();
	mdelay(1);
	local_irq_disable();

	set_cpu_online(cpu, false);

	cpu_map_rebuild();

	return 0;
}

void __cpu_die(unsigned int cpu)
{
	int i;

	for (i = 0; i < 100; i++) {
		smp_rmb();
		if (!cpumask_test_cpu(cpu, &smp_commenced_mask))
			break;
		msleep(100);
	}
	if (cpumask_test_cpu(cpu, &smp_commenced_mask)) {
		printk(KERN_ERR "CPU %u didn't die...\n", cpu);
	} else {
#if defined(CONFIG_SUN_LDOMS)
		unsigned long hv_err;
		int limit = 100;

		do {
			hv_err = sun4v_cpu_stop(cpu);
			if (hv_err == HV_EOK) {
				set_cpu_present(cpu, false);
				break;
			}
		} while (--limit > 0);
		if (limit <= 0) {
			printk(KERN_ERR "sun4v_cpu_stop() fails err=%lu\n",
			       hv_err);
		}
#endif
	}
}
#endif

void __init smp_cpus_done(unsigned int max_cpus)
{
}

void smp_send_reschedule(int cpu)
{
	if (cpu == smp_processor_id()) {
		WARN_ON_ONCE(preemptible());
		set_softint(1 << PIL_SMP_RECEIVE_SIGNAL);
	} else {
		xcall_deliver((u64) &xcall_receive_signal,
			      0, 0, cpumask_of(cpu));
	}
}

void __irq_entry smp_receive_signal_client(int irq, struct pt_regs *regs)
{
	clear_softint(1 << irq);
	scheduler_ipi();
}

static void stop_this_cpu(void *dummy)
{
	set_cpu_online(smp_processor_id(), false);
	prom_stopself();
}

void smp_send_stop(void)
{
	int cpu;

	if (tlb_type == hypervisor) {
		int this_cpu = smp_processor_id();
#ifdef CONFIG_SERIAL_SUNHV
		sunhv_migrate_hvcons_irq(this_cpu);
#endif
		for_each_online_cpu(cpu) {
			if (cpu == this_cpu)
				continue;

			set_cpu_online(cpu, false);
#ifdef CONFIG_SUN_LDOMS
			if (ldom_domaining_enabled) {
				unsigned long hv_err;
				hv_err = sun4v_cpu_stop(cpu);
				if (hv_err)
					printk(KERN_ERR "sun4v_cpu_stop() "
					       "failed err=%lu\n", hv_err);
			} else
#endif
				prom_stopcpu_cpuid(cpu);
		}
	} else
		smp_call_function(stop_this_cpu, NULL, 0);
}

/**
 * pcpu_alloc_bootmem - NUMA friendly alloc_bootmem wrapper for percpu
 * @cpu: cpu to allocate for
 * @size: size allocation in bytes
 * @align: alignment
 *
 * Allocate @size bytes aligned at @align for cpu @cpu.  This wrapper
 * does the right thing for NUMA regardless of the current
 * configuration.
 *
 * RETURNS:
 * Pointer to the allocated area on success, NULL on failure.
 */
static void * __init pcpu_alloc_bootmem(unsigned int cpu, size_t size,
					size_t align)
{
	const unsigned long goal = __pa(MAX_DMA_ADDRESS);
#ifdef CONFIG_NEED_MULTIPLE_NODES
	int node = cpu_to_node(cpu);
	void *ptr;

	if (!node_online(node) || !NODE_DATA(node)) {
		ptr = __alloc_bootmem(size, align, goal);
		pr_info("cpu %d has no node %d or node-local memory\n",
			cpu, node);
		pr_debug("per cpu data for cpu%d %lu bytes at %016lx\n",
			 cpu, size, __pa(ptr));
	} else {
		ptr = __alloc_bootmem_node(NODE_DATA(node),
					   size, align, goal);
		pr_debug("per cpu data for cpu%d %lu bytes on node%d at "
			 "%016lx\n", cpu, size, node, __pa(ptr));
	}
	return ptr;
#else
	return __alloc_bootmem(size, align, goal);
#endif
}

static void __init pcpu_free_bootmem(void *ptr, size_t size)
{
	free_bootmem(__pa(ptr), size);
}

static int __init pcpu_cpu_distance(unsigned int from, unsigned int to)
{
	if (cpu_to_node(from) == cpu_to_node(to))
		return LOCAL_DISTANCE;
	else
		return REMOTE_DISTANCE;
}

static void __init pcpu_populate_pte(unsigned long addr)
{
	pgd_t *pgd = pgd_offset_k(addr);
	pud_t *pud;
	pmd_t *pmd;

	if (pgd_none(*pgd)) {
		pud_t *new;

		new = __alloc_bootmem(PAGE_SIZE, PAGE_SIZE, PAGE_SIZE);
		pgd_populate(&init_mm, pgd, new);
	}

	pud = pud_offset(pgd, addr);
	if (pud_none(*pud)) {
		pmd_t *new;

		new = __alloc_bootmem(PAGE_SIZE, PAGE_SIZE, PAGE_SIZE);
		pud_populate(&init_mm, pud, new);
	}

	pmd = pmd_offset(pud, addr);
	if (!pmd_present(*pmd)) {
		pte_t *new;

		new = __alloc_bootmem(PAGE_SIZE, PAGE_SIZE, PAGE_SIZE);
		pmd_populate_kernel(&init_mm, pmd, new);
	}
}

void __init setup_per_cpu_areas(void)
{
	unsigned long delta;
	unsigned int cpu;
	int rc = -EINVAL;

	if (pcpu_chosen_fc != PCPU_FC_PAGE) {
		rc = pcpu_embed_first_chunk(PERCPU_MODULE_RESERVE,
					    PERCPU_DYNAMIC_RESERVE, 4 << 20,
					    pcpu_cpu_distance,
					    pcpu_alloc_bootmem,
					    pcpu_free_bootmem);
		if (rc)
			pr_warning("PERCPU: %s allocator failed (%d), "
				   "falling back to page size\n",
				   pcpu_fc_names[pcpu_chosen_fc], rc);
	}
	if (rc < 0)
		rc = pcpu_page_first_chunk(PERCPU_MODULE_RESERVE,
					   pcpu_alloc_bootmem,
					   pcpu_free_bootmem,
					   pcpu_populate_pte);
	if (rc < 0)
		panic("cannot initialize percpu area (err=%d)", rc);

	delta = (unsigned long)pcpu_base_addr - (unsigned long)__per_cpu_start;
	for_each_possible_cpu(cpu)
		__per_cpu_offset(cpu) = delta + pcpu_unit_offsets[cpu];

	/* Setup %g5 for the boot cpu.  */
	__local_per_cpu_offset = __per_cpu_offset(smp_processor_id());

	of_fill_in_cpu_data();
	if (tlb_type == hypervisor)
		mdesc_fill_in_cpu_data(cpu_all_mask);
}
