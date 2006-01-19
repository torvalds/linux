/* smp.c: Sparc64 SMP support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
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

#include <asm/head.h>
#include <asm/ptrace.h>
#include <asm/atomic.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/cpudata.h>

#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/uaccess.h>
#include <asm/timer.h>
#include <asm/starfire.h>
#include <asm/tlb.h>

extern void calibrate_delay(void);

/* Please don't make this stuff initdata!!!  --DaveM */
static unsigned char boot_cpu_id;

cpumask_t cpu_online_map __read_mostly = CPU_MASK_NONE;
cpumask_t phys_cpu_present_map __read_mostly = CPU_MASK_NONE;
static cpumask_t smp_commenced_mask;
static cpumask_t cpu_callout_map;

void smp_info(struct seq_file *m)
{
	int i;
	
	seq_printf(m, "State:\n");
	for (i = 0; i < NR_CPUS; i++) {
		if (cpu_online(i))
			seq_printf(m,
				   "CPU%d:\t\tonline\n", i);
	}
}

void smp_bogo(struct seq_file *m)
{
	int i;
	
	for (i = 0; i < NR_CPUS; i++)
		if (cpu_online(i))
			seq_printf(m,
				   "Cpu%dBogo\t: %lu.%02lu\n"
				   "Cpu%dClkTck\t: %016lx\n",
				   i, cpu_data(i).udelay_val / (500000/HZ),
				   (cpu_data(i).udelay_val / (5000/HZ)) % 100,
				   i, cpu_data(i).clock_tick);
}

void __init smp_store_cpu_info(int id)
{
	int cpu_node;

	/* multiplier and counter set by
	   smp_setup_percpu_timer()  */
	cpu_data(id).udelay_val			= loops_per_jiffy;

	cpu_find_by_mid(id, &cpu_node);
	cpu_data(id).clock_tick = prom_getintdefault(cpu_node,
						     "clock-frequency", 0);

	cpu_data(id).pgcache_size		= 0;
	cpu_data(id).pte_cache[0]		= NULL;
	cpu_data(id).pte_cache[1]		= NULL;
	cpu_data(id).pgd_cache			= NULL;
	cpu_data(id).idle_volume		= 1;

	cpu_data(id).dcache_size = prom_getintdefault(cpu_node, "dcache-size",
						      16 * 1024);
	cpu_data(id).dcache_line_size =
		prom_getintdefault(cpu_node, "dcache-line-size", 32);
	cpu_data(id).icache_size = prom_getintdefault(cpu_node, "icache-size",
						      16 * 1024);
	cpu_data(id).icache_line_size =
		prom_getintdefault(cpu_node, "icache-line-size", 32);
	cpu_data(id).ecache_size = prom_getintdefault(cpu_node, "ecache-size",
						      4 * 1024 * 1024);
	cpu_data(id).ecache_line_size =
		prom_getintdefault(cpu_node, "ecache-line-size", 64);
	printk("CPU[%d]: Caches "
	       "D[sz(%d):line_sz(%d)] "
	       "I[sz(%d):line_sz(%d)] "
	       "E[sz(%d):line_sz(%d)]\n",
	       id,
	       cpu_data(id).dcache_size, cpu_data(id).dcache_line_size,
	       cpu_data(id).icache_size, cpu_data(id).icache_line_size,
	       cpu_data(id).ecache_size, cpu_data(id).ecache_line_size);
}

static void smp_setup_percpu_timer(void);

static volatile unsigned long callin_flag = 0;

extern void inherit_locked_prom_mappings(int save_p);

static inline void cpu_setup_percpu_base(unsigned long cpu_id)
{
	__asm__ __volatile__("mov	%0, %%g5\n\t"
			     "stxa	%0, [%1] %2\n\t"
			     "membar	#Sync"
			     : /* no outputs */
			     : "r" (__per_cpu_offset(cpu_id)),
			       "r" (TSB_REG), "i" (ASI_IMMU));
}

void __init smp_callin(void)
{
	int cpuid = hard_smp_processor_id();

	inherit_locked_prom_mappings(0);

	__flush_tlb_all();

	cpu_setup_percpu_base(cpuid);

	smp_setup_percpu_timer();

	if (cheetah_pcache_forced_on)
		cheetah_enable_pcache();

	local_irq_enable();

	calibrate_delay();
	smp_store_cpu_info(cpuid);
	callin_flag = 1;
	__asm__ __volatile__("membar #Sync\n\t"
			     "flush  %%g6" : : : "memory");

	/* Clear this or we will die instantly when we
	 * schedule back to this idler...
	 */
	current_thread_info()->new_child = 0;

	/* Attach to the address space of init_task. */
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;

	while (!cpu_isset(cpuid, smp_commenced_mask))
		rmb();

	cpu_set(cpuid, cpu_online_map);

	/* idle thread is expected to have preempt disabled */
	preempt_disable();
}

void cpu_panic(void)
{
	printk("CPU[%d]: Returns from cpu_idle!\n", smp_processor_id());
	panic("SMP bolixed\n");
}

static unsigned long current_tick_offset __read_mostly;

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

static DEFINE_SPINLOCK(itc_sync_lock);
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
		membar_storeload();
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
	unsigned long flags, rt, master_time_stamp, bound;
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
			if (delta == 0) {
				done = 1;	/* let's lock on to this... */
				bound = rt;
			}

			if (!done) {
				if (i > 0) {
					adjust_latency += -delta;
					adj = -delta + adjust_latency/4;
				} else
					adj = -delta;

				tick_ops->add_tick(adj, current_tick_offset);
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

	printk(KERN_INFO "CPU %d: synchronized TICK with master CPU (last diff %ld cycles,"
	       "maxerr %lu cycles)\n", smp_processor_id(), delta, rt);
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
	membar_storeload();

	spin_lock_irqsave(&itc_sync_lock, flags);
	{
		for (i = 0; i < NUM_ROUNDS*NUM_ITERS; i++) {
			while (!go[MASTER])
				rmb();
			go[MASTER] = 0;
			wmb();
			go[SLAVE] = tick_ops->get_tick();
			membar_storeload();
		}
	}
	spin_unlock_irqrestore(&itc_sync_lock, flags);
}

extern unsigned long sparc64_cpu_startup;

/* The OBP cpu startup callback truncates the 3rd arg cookie to
 * 32-bits (I think) so to be safe we have it read the pointer
 * contained here so we work on >4GB machines. -DaveM
 */
static struct thread_info *cpu_new_thread = NULL;

static int __devinit smp_boot_one_cpu(unsigned int cpu)
{
	unsigned long entry =
		(unsigned long)(&sparc64_cpu_startup);
	unsigned long cookie =
		(unsigned long)(&cpu_new_thread);
	struct task_struct *p;
	int timeout, ret, cpu_node;

	p = fork_idle(cpu);
	callin_flag = 0;
	cpu_new_thread = task_thread_info(p);
	cpu_set(cpu, cpu_callout_map);

	cpu_find_by_mid(cpu, &cpu_node);
	prom_startcpu(cpu_node, entry, cookie);

	for (timeout = 0; timeout < 5000000; timeout++) {
		if (callin_flag)
			break;
		udelay(100);
	}
	if (callin_flag) {
		ret = 0;
	} else {
		printk("Processor %d is stuck.\n", cpu);
		cpu_clear(cpu, cpu_callout_map);
		ret = -ENODEV;
	}
	cpu_new_thread = NULL;

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
		printk("CPU[%d]: mondo stuckage result[%016lx]\n",
		       smp_processor_id(), result);
	} else {
		udelay(2);
		goto again;
	}
}

static __inline__ void spitfire_xcall_deliver(u64 data0, u64 data1, u64 data2, cpumask_t mask)
{
	u64 pstate;
	int i;

	__asm__ __volatile__("rdpr %%pstate, %0" : "=r" (pstate));
	for_each_cpu_mask(i, mask)
		spitfire_xcall_helper(data0, data1, data2, pstate, i);
}

/* Cheetah now allows to send the whole 64-bytes of data in the interrupt
 * packet, but we have no use for that.  However we do take advantage of
 * the new pipelining feature (ie. dispatch to multiple cpus simultaneously).
 */
static void cheetah_xcall_deliver(u64 data0, u64 data1, u64 data2, cpumask_t mask)
{
	u64 pstate, ver;
	int nack_busy_id, is_jalapeno;

	if (cpus_empty(mask))
		return;

	/* Unfortunately, someone at Sun had the brilliant idea to make the
	 * busy/nack fields hard-coded by ITID number for this Ultra-III
	 * derivative processor.
	 */
	__asm__ ("rdpr %%ver, %0" : "=r" (ver));
	is_jalapeno = ((ver >> 32) == 0x003e0016);

	__asm__ __volatile__("rdpr %%pstate, %0" : "=r" (pstate));

retry:
	__asm__ __volatile__("wrpr %0, %1, %%pstate\n\t"
			     : : "r" (pstate), "i" (PSTATE_IE));

	/* Setup the dispatch data registers. */
	__asm__ __volatile__("stxa	%0, [%3] %6\n\t"
			     "stxa	%1, [%4] %6\n\t"
			     "stxa	%2, [%5] %6\n\t"
			     "membar	#Sync\n\t"
			     : /* no outputs */
			     : "r" (data0), "r" (data1), "r" (data2),
			       "r" (0x40), "r" (0x50), "r" (0x60),
			       "i" (ASI_INTR_W));

	nack_busy_id = 0;
	{
		int i;

		for_each_cpu_mask(i, mask) {
			u64 target = (i << 14) | 0x70;

			if (!is_jalapeno)
				target |= (nack_busy_id << 24);
			__asm__ __volatile__(
				"stxa	%%g0, [%0] %1\n\t"
				"membar	#Sync\n\t"
				: /* no outputs */
				: "r" (target), "i" (ASI_INTR_W));
			nack_busy_id++;
		}
	}

	/* Now, poll for completion. */
	{
		u64 dispatch_stat;
		long stuck;

		stuck = 100000 * nack_busy_id;
		do {
			__asm__ __volatile__("ldxa	[%%g0] %1, %0"
					     : "=r" (dispatch_stat)
					     : "i" (ASI_INTR_DISPATCH_STAT));
			if (dispatch_stat == 0UL) {
				__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
						     : : "r" (pstate));
				return;
			}
			if (!--stuck)
				break;
		} while (dispatch_stat & 0x5555555555555555UL);

		__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
				     : : "r" (pstate));

		if ((dispatch_stat & ~(0x5555555555555555UL)) == 0) {
			/* Busy bits will not clear, continue instead
			 * of freezing up on this cpu.
			 */
			printk("CPU[%d]: mondo stuckage result[%016lx]\n",
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
			for_each_cpu_mask(i, mask) {
				u64 check_mask;

				if (is_jalapeno)
					check_mask = (0x2UL << (2*i));
				else
					check_mask = (0x2UL <<
						      this_busy_nack);
				if ((dispatch_stat & check_mask) == 0)
					cpu_clear(i, mask);
				this_busy_nack += 2;
			}

			goto retry;
		}
	}
}

/* Send cross call to all processors mentioned in MASK
 * except self.
 */
static void smp_cross_call_masked(unsigned long *func, u32 ctx, u64 data1, u64 data2, cpumask_t mask)
{
	u64 data0 = (((u64)ctx)<<32 | (((u64)func) & 0xffffffff));
	int this_cpu = get_cpu();

	cpus_and(mask, mask, cpu_online_map);
	cpu_clear(this_cpu, mask);

	if (tlb_type == spitfire)
		spitfire_xcall_deliver(data0, data1, data2, mask);
	else
		cheetah_xcall_deliver(data0, data1, data2, mask);
	/* NOTE: Caller runs local copy on master. */

	put_cpu();
}

extern unsigned long xcall_sync_tick;

static void smp_start_sync_tick_client(int cpu)
{
	cpumask_t mask = cpumask_of_cpu(cpu);

	smp_cross_call_masked(&xcall_sync_tick,
			      0, 0, 0, mask);
}

/* Send cross call to all processors except self. */
#define smp_cross_call(func, ctx, data1, data2) \
	smp_cross_call_masked(func, ctx, data1, data2, cpu_online_map)

struct call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t finished;
	int wait;
};

static DEFINE_SPINLOCK(call_lock);
static struct call_data_struct *call_data;

extern unsigned long xcall_call_function;

/*
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.
 */
int smp_call_function(void (*func)(void *info), void *info,
		      int nonatomic, int wait)
{
	struct call_data_struct data;
	int cpus = num_online_cpus() - 1;
	long timeout;

	if (!cpus)
		return 0;

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	data.func = func;
	data.info = info;
	atomic_set(&data.finished, 0);
	data.wait = wait;

	spin_lock(&call_lock);

	call_data = &data;

	smp_cross_call(&xcall_call_function, 0, 0, 0);

	/* 
	 * Wait for other cpus to complete function or at
	 * least snap the call data.
	 */
	timeout = 1000000;
	while (atomic_read(&data.finished) != cpus) {
		if (--timeout <= 0)
			goto out_timeout;
		barrier();
		udelay(1);
	}

	spin_unlock(&call_lock);

	return 0;

out_timeout:
	spin_unlock(&call_lock);
	printk("XCALL: Remote cpus not responding, ncpus=%ld finished=%ld\n",
	       (long) num_online_cpus() - 1L,
	       (long) atomic_read(&data.finished));
	return 0;
}

void smp_call_function_client(int irq, struct pt_regs *regs)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;

	clear_softint(1 << irq);
	if (call_data->wait) {
		/* let initiator proceed only after completion */
		func(info);
		atomic_inc(&call_data->finished);
	} else {
		/* let initiator proceed after getting data */
		atomic_inc(&call_data->finished);
		func(info);
	}
}

extern unsigned long xcall_flush_tlb_mm;
extern unsigned long xcall_flush_tlb_pending;
extern unsigned long xcall_flush_tlb_kernel_range;
extern unsigned long xcall_flush_tlb_all_spitfire;
extern unsigned long xcall_flush_tlb_all_cheetah;
extern unsigned long xcall_report_regs;
extern unsigned long xcall_receive_signal;

#ifdef DCACHE_ALIASING_POSSIBLE
extern unsigned long xcall_flush_dcache_page_cheetah;
#endif
extern unsigned long xcall_flush_dcache_page_spitfire;

#ifdef CONFIG_DEBUG_DCFLUSH
extern atomic_t dcpage_flushes;
extern atomic_t dcpage_flushes_xcall;
#endif

static __inline__ void __local_flush_dcache_page(struct page *page)
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
	cpumask_t mask = cpumask_of_cpu(cpu);
	int this_cpu = get_cpu();

#ifdef CONFIG_DEBUG_DCFLUSH
	atomic_inc(&dcpage_flushes);
#endif
	if (cpu == this_cpu) {
		__local_flush_dcache_page(page);
	} else if (cpu_online(cpu)) {
		void *pg_addr = page_address(page);
		u64 data0;

		if (tlb_type == spitfire) {
			data0 =
				((u64)&xcall_flush_dcache_page_spitfire);
			if (page_mapping(page) != NULL)
				data0 |= ((u64)1 << 32);
			spitfire_xcall_deliver(data0,
					       __pa(pg_addr),
					       (u64) pg_addr,
					       mask);
		} else {
#ifdef DCACHE_ALIASING_POSSIBLE
			data0 =
				((u64)&xcall_flush_dcache_page_cheetah);
			cheetah_xcall_deliver(data0,
					      __pa(pg_addr),
					      0, mask);
#endif
		}
#ifdef CONFIG_DEBUG_DCFLUSH
		atomic_inc(&dcpage_flushes_xcall);
#endif
	}

	put_cpu();
}

void flush_dcache_page_all(struct mm_struct *mm, struct page *page)
{
	void *pg_addr = page_address(page);
	cpumask_t mask = cpu_online_map;
	u64 data0;
	int this_cpu = get_cpu();

	cpu_clear(this_cpu, mask);

#ifdef CONFIG_DEBUG_DCFLUSH
	atomic_inc(&dcpage_flushes);
#endif
	if (cpus_empty(mask))
		goto flush_self;
	if (tlb_type == spitfire) {
		data0 = ((u64)&xcall_flush_dcache_page_spitfire);
		if (page_mapping(page) != NULL)
			data0 |= ((u64)1 << 32);
		spitfire_xcall_deliver(data0,
				       __pa(pg_addr),
				       (u64) pg_addr,
				       mask);
	} else {
#ifdef DCACHE_ALIASING_POSSIBLE
		data0 = ((u64)&xcall_flush_dcache_page_cheetah);
		cheetah_xcall_deliver(data0,
				      __pa(pg_addr),
				      0, mask);
#endif
	}
#ifdef CONFIG_DEBUG_DCFLUSH
	atomic_inc(&dcpage_flushes_xcall);
#endif
 flush_self:
	__local_flush_dcache_page(page);

	put_cpu();
}

void smp_receive_signal(int cpu)
{
	cpumask_t mask = cpumask_of_cpu(cpu);

	if (cpu_online(cpu)) {
		u64 data0 = (((u64)&xcall_receive_signal) & 0xffffffff);

		if (tlb_type == spitfire)
			spitfire_xcall_deliver(data0, 0, 0, mask);
		else
			cheetah_xcall_deliver(data0, 0, 0, mask);
	}
}

void smp_receive_signal_client(int irq, struct pt_regs *regs)
{
	/* Just return, rtrap takes care of the rest. */
	clear_softint(1 << irq);
}

void smp_report_regs(void)
{
	smp_cross_call(&xcall_report_regs, 0, 0, 0);
}

void smp_flush_tlb_all(void)
{
	if (tlb_type == spitfire)
		smp_cross_call(&xcall_flush_tlb_all_spitfire, 0, 0, 0);
	else
		smp_cross_call(&xcall_flush_tlb_all_cheetah, 0, 0, 0);
	__flush_tlb_all();
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
 *    processor's in current->active_mm->cpu_vm_mask and performing the
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
		mm->cpu_vm_mask = cpumask_of_cpu(cpu);
		goto local_flush_and_out;
	}

	smp_cross_call_masked(&xcall_flush_tlb_mm,
			      ctx, 0, 0,
			      mm->cpu_vm_mask);

local_flush_and_out:
	__flush_tlb_mm(ctx, SECONDARY_CONTEXT);

	put_cpu();
}

void smp_flush_tlb_pending(struct mm_struct *mm, unsigned long nr, unsigned long *vaddrs)
{
	u32 ctx = CTX_HWBITS(mm->context);
	int cpu = get_cpu();

	if (mm == current->active_mm && atomic_read(&mm->mm_users) == 1)
		mm->cpu_vm_mask = cpumask_of_cpu(cpu);
	else
		smp_cross_call_masked(&xcall_flush_tlb_pending,
				      ctx, nr, (unsigned long) vaddrs,
				      mm->cpu_vm_mask);

	__flush_tlb_pending(ctx, nr, vaddrs);

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
	int result = atomic_add_ret(1, &smp_capture_depth);

	if (result == 1) {
		int ncpus = num_online_cpus();

#ifdef CAPTURE_DEBUG
		printk("CPU[%d]: Sending penguins to jail...",
		       smp_processor_id());
#endif
		penguins_are_doing_time = 1;
		membar_storestore_loadstore();
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
		membar_storeload_storestore();
		atomic_dec(&smp_capture_registry);
	}
}

/* Imprisoned penguins run with %pil == 15, but PSTATE_IE set, so they
 * can service tlb flush xcalls...
 */
extern void prom_world(int);
extern void save_alternate_globals(unsigned long *);
extern void restore_alternate_globals(unsigned long *);
void smp_penguin_jailcell(int irq, struct pt_regs *regs)
{
	unsigned long global_save[24];

	clear_softint(1 << irq);

	preempt_disable();

	__asm__ __volatile__("flushw");
	save_alternate_globals(global_save);
	prom_world(1);
	atomic_inc(&smp_capture_registry);
	membar_storeload_storestore();
	while (penguins_are_doing_time)
		rmb();
	restore_alternate_globals(global_save);
	atomic_dec(&smp_capture_registry);
	prom_world(0);

	preempt_enable();
}

#define prof_multiplier(__cpu)		cpu_data(__cpu).multiplier
#define prof_counter(__cpu)		cpu_data(__cpu).counter

void smp_percpu_timer_interrupt(struct pt_regs *regs)
{
	unsigned long compare, tick, pstate;
	int cpu = smp_processor_id();
	int user = user_mode(regs);

	/*
	 * Check for level 14 softint.
	 */
	{
		unsigned long tick_mask = tick_ops->softint_mask;

		if (!(get_softint() & tick_mask)) {
			extern void handler_irq(int, struct pt_regs *);

			handler_irq(14, regs);
			return;
		}
		clear_softint(tick_mask);
	}

	do {
		profile_tick(CPU_PROFILING, regs);
		if (!--prof_counter(cpu)) {
			irq_enter();

			if (cpu == boot_cpu_id) {
				kstat_this_cpu.irqs[0]++;
				timer_tick_interrupt(regs);
			}

			update_process_times(user);

			irq_exit();

			prof_counter(cpu) = prof_multiplier(cpu);
		}

		/* Guarantee that the following sequences execute
		 * uninterrupted.
		 */
		__asm__ __volatile__("rdpr	%%pstate, %0\n\t"
				     "wrpr	%0, %1, %%pstate"
				     : "=r" (pstate)
				     : "i" (PSTATE_IE));

		compare = tick_ops->add_compare(current_tick_offset);
		tick = tick_ops->get_tick();

		/* Restore PSTATE_IE. */
		__asm__ __volatile__("wrpr	%0, 0x0, %%pstate"
				     : /* no outputs */
				     : "r" (pstate));
	} while (time_after_eq(tick, compare));
}

static void __init smp_setup_percpu_timer(void)
{
	int cpu = smp_processor_id();
	unsigned long pstate;

	prof_counter(cpu) = prof_multiplier(cpu) = 1;

	/* Guarantee that the following sequences execute
	 * uninterrupted.
	 */
	__asm__ __volatile__("rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %1, %%pstate"
			     : "=r" (pstate)
			     : "i" (PSTATE_IE));

	tick_ops->init_tick(current_tick_offset);

	/* Restore PSTATE_IE. */
	__asm__ __volatile__("wrpr	%0, 0x0, %%pstate"
			     : /* no outputs */
			     : "r" (pstate));
}

void __init smp_tick_init(void)
{
	boot_cpu_id = hard_smp_processor_id();
	current_tick_offset = timer_tick_offset;

	cpu_set(boot_cpu_id, cpu_online_map);
	prof_counter(boot_cpu_id) = prof_multiplier(boot_cpu_id) = 1;
}

/* /proc/profile writes can call this, don't __init it please. */
static DEFINE_SPINLOCK(prof_setup_lock);

int setup_profiling_timer(unsigned int multiplier)
{
	unsigned long flags;
	int i;

	if ((!multiplier) || (timer_tick_offset / multiplier) < 1000)
		return -EINVAL;

	spin_lock_irqsave(&prof_setup_lock, flags);
	for (i = 0; i < NR_CPUS; i++)
		prof_multiplier(i) = multiplier;
	current_tick_offset = (timer_tick_offset / multiplier);
	spin_unlock_irqrestore(&prof_setup_lock, flags);

	return 0;
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	int instance, mid;

	instance = 0;
	while (!cpu_find_by_instance(instance, NULL, &mid)) {
		if (mid < max_cpus)
			cpu_set(mid, phys_cpu_present_map);
		instance++;
	}

	if (num_possible_cpus() > max_cpus) {
		instance = 0;
		while (!cpu_find_by_instance(instance, NULL, &mid)) {
			if (mid != boot_cpu_id) {
				cpu_clear(mid, phys_cpu_present_map);
				if (num_possible_cpus() <= max_cpus)
					break;
			}
			instance++;
		}
	}

	smp_store_cpu_info(boot_cpu_id);
}

void __devinit smp_prepare_boot_cpu(void)
{
	if (hard_smp_processor_id() >= NR_CPUS) {
		prom_printf("Serious problem, boot cpu id >= NR_CPUS\n");
		prom_halt();
	}

	current_thread_info()->cpu = hard_smp_processor_id();

	cpu_set(smp_processor_id(), cpu_online_map);
	cpu_set(smp_processor_id(), phys_cpu_present_map);
}

int __devinit __cpu_up(unsigned int cpu)
{
	int ret = smp_boot_one_cpu(cpu);

	if (!ret) {
		cpu_set(cpu, smp_commenced_mask);
		while (!cpu_isset(cpu, cpu_online_map))
			mb();
		if (!cpu_isset(cpu, cpu_online_map)) {
			ret = -ENODEV;
		} else {
			smp_synchronize_one_tick(cpu);
		}
	}
	return ret;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
	unsigned long bogosum = 0;
	int i;

	for (i = 0; i < NR_CPUS; i++) {
		if (cpu_online(i))
			bogosum += cpu_data(i).udelay_val;
	}
	printk("Total of %ld processors activated "
	       "(%lu.%02lu BogoMIPS).\n",
	       (long) num_online_cpus(),
	       bogosum/(500000/HZ),
	       (bogosum/(5000/HZ))%100);
}

void smp_send_reschedule(int cpu)
{
	smp_receive_signal(cpu);
}

/* This is a nop because we capture all other cpus
 * anyways when making the PROM active.
 */
void smp_send_stop(void)
{
}

unsigned long __per_cpu_base __read_mostly;
unsigned long __per_cpu_shift __read_mostly;

EXPORT_SYMBOL(__per_cpu_base);
EXPORT_SYMBOL(__per_cpu_shift);

void __init setup_per_cpu_areas(void)
{
	unsigned long goal, size, i;
	char *ptr;
	/* Created by linker magic */
	extern char __per_cpu_start[], __per_cpu_end[];

	/* Copy section for each CPU (we discard the original) */
	goal = ALIGN(__per_cpu_end - __per_cpu_start, PAGE_SIZE);

#ifdef CONFIG_MODULES
	if (goal < PERCPU_ENOUGH_ROOM)
		goal = PERCPU_ENOUGH_ROOM;
#endif
	__per_cpu_shift = 0;
	for (size = 1UL; size < goal; size <<= 1UL)
		__per_cpu_shift++;

	/* Make sure the resulting __per_cpu_base value
	 * will fit in the 43-bit sign extended IMMU
	 * TSB register.
	 */
	ptr = __alloc_bootmem(size * NR_CPUS, PAGE_SIZE,
			      (unsigned long) __per_cpu_start);

	__per_cpu_base = ptr - __per_cpu_start;

	if ((__per_cpu_shift < PAGE_SHIFT) ||
	    (__per_cpu_base & ~PAGE_MASK) ||
	    (__per_cpu_base != (((long) __per_cpu_base << 20) >> 20))) {
		prom_printf("PER_CPU: Invalid layout, "
			    "ptr[%p] shift[%lx] base[%lx]\n",
			    ptr, __per_cpu_shift, __per_cpu_base);
		prom_halt();
	}

	for (i = 0; i < NR_CPUS; i++, ptr += size)
		memcpy(ptr, __per_cpu_start, __per_cpu_end - __per_cpu_start);

	/* Finally, load in the boot cpu's base value.
	 * We abuse the IMMU TSB register for trap handler
	 * entry and exit loading of %g5.  That is why it
	 * has to be page aligned.
	 */
	cpu_setup_percpu_base(hard_smp_processor_id());
}
