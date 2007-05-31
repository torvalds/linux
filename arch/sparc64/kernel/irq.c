/* $Id: irq.c,v 1.114 2002/01/11 08:45:38 davem Exp $
 * irq.c: UltraSparc IRQ handling/init/registry.
 *
 * Copyright (C) 1997  David S. Miller  (davem@caip.rutgers.edu)
 * Copyright (C) 1998  Eddie C. Dost    (ecd@skynet.be)
 * Copyright (C) 1998  Jakub Jelinek    (jj@ultra.linux.cz)
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/bootmem.h>
#include <linux/irq.h>
#include <linux/msi.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/sbus.h>
#include <asm/iommu.h>
#include <asm/upa.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/timer.h>
#include <asm/smp.h>
#include <asm/starfire.h>
#include <asm/uaccess.h>
#include <asm/cache.h>
#include <asm/cpudata.h>
#include <asm/auxio.h>
#include <asm/head.h>

/* UPA nodes send interrupt packet to UltraSparc with first data reg
 * value low 5 (7 on Starfire) bits holding the IRQ identifier being
 * delivered.  We must translate this into a non-vector IRQ so we can
 * set the softint on this cpu.
 *
 * To make processing these packets efficient and race free we use
 * an array of irq buckets below.  The interrupt vector handler in
 * entry.S feeds incoming packets into per-cpu pil-indexed lists.
 * The IVEC handler does not need to act atomically, the PIL dispatch
 * code uses CAS to get an atomic snapshot of the list and clear it
 * at the same time.
 *
 * If you make changes to ino_bucket, please update hand coded assembler
 * of the vectored interrupt trap handler(s) in entry.S and sun4v_ivec.S
 */
struct ino_bucket {
	/* Next handler in per-CPU IRQ worklist.  We know that
	 * bucket pointers have the high 32-bits clear, so to
	 * save space we only store the bits we need.
	 */
/*0x00*/unsigned int irq_chain;

	/* Virtual interrupt number assigned to this INO.  */
/*0x04*/unsigned int virt_irq;
};

#define NUM_IVECS	(IMAP_INR + 1)
struct ino_bucket ivector_table[NUM_IVECS] __attribute__ ((aligned (SMP_CACHE_BYTES)));

#define __irq_ino(irq) \
        (((struct ino_bucket *)(unsigned long)(irq)) - &ivector_table[0])
#define __bucket(irq) ((struct ino_bucket *)(unsigned long)(irq))
#define __irq(bucket) ((unsigned int)(unsigned long)(bucket))

/* This has to be in the main kernel image, it cannot be
 * turned into per-cpu data.  The reason is that the main
 * kernel image is locked into the TLB and this structure
 * is accessed from the vectored interrupt trap handler.  If
 * access to this structure takes a TLB miss it could cause
 * the 5-level sparc v9 trap stack to overflow.
 */
#define irq_work(__cpu)	&(trap_block[(__cpu)].irq_worklist)

static unsigned int virt_to_real_irq_table[NR_IRQS];

static unsigned char virt_irq_alloc(unsigned int real_irq)
{
	unsigned char ent;

	BUILD_BUG_ON(NR_IRQS >= 256);

	for (ent = 1; ent < NR_IRQS; ent++) {
		if (!virt_to_real_irq_table[ent])
			break;
	}
	if (ent >= NR_IRQS) {
		printk(KERN_ERR "IRQ: Out of virtual IRQs.\n");
		return 0;
	}

	virt_to_real_irq_table[ent] = real_irq;

	return ent;
}

#ifdef CONFIG_PCI_MSI
static void virt_irq_free(unsigned int virt_irq)
{
	unsigned int real_irq;

	if (virt_irq >= NR_IRQS)
		return;

	real_irq = virt_to_real_irq_table[virt_irq];
	virt_to_real_irq_table[virt_irq] = 0;

	__bucket(real_irq)->virt_irq = 0;
}
#endif

static unsigned int virt_to_real_irq(unsigned char virt_irq)
{
	return virt_to_real_irq_table[virt_irq];
}

/*
 * /proc/interrupts printing:
 */

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction * action;
	unsigned long flags;

	if (i == 0) {
		seq_printf(p, "           ");
		for_each_online_cpu(j)
			seq_printf(p, "CPU%d       ",j);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action)
			goto skip;
		seq_printf(p, "%3d: ",i);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
#endif
		seq_printf(p, " %9s", irq_desc[i].chip->typename);
		seq_printf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);

		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	}
	return 0;
}

static unsigned int sun4u_compute_tid(unsigned long imap, unsigned long cpuid)
{
	unsigned int tid;

	if (this_is_starfire) {
		tid = starfire_translate(imap, cpuid);
		tid <<= IMAP_TID_SHIFT;
		tid &= IMAP_TID_UPA;
	} else {
		if (tlb_type == cheetah || tlb_type == cheetah_plus) {
			unsigned long ver;

			__asm__ ("rdpr %%ver, %0" : "=r" (ver));
			if ((ver >> 32UL) == __JALAPENO_ID ||
			    (ver >> 32UL) == __SERRANO_ID) {
				tid = cpuid << IMAP_TID_SHIFT;
				tid &= IMAP_TID_JBUS;
			} else {
				unsigned int a = cpuid & 0x1f;
				unsigned int n = (cpuid >> 5) & 0x1f;

				tid = ((a << IMAP_AID_SHIFT) |
				       (n << IMAP_NID_SHIFT));
				tid &= (IMAP_AID_SAFARI |
					IMAP_NID_SAFARI);;
			}
		} else {
			tid = cpuid << IMAP_TID_SHIFT;
			tid &= IMAP_TID_UPA;
		}
	}

	return tid;
}

struct irq_handler_data {
	unsigned long	iclr;
	unsigned long	imap;

	void		(*pre_handler)(unsigned int, void *, void *);
	void		*pre_handler_arg1;
	void		*pre_handler_arg2;
};

static inline struct ino_bucket *virt_irq_to_bucket(unsigned int virt_irq)
{
	unsigned int real_irq = virt_to_real_irq(virt_irq);
	struct ino_bucket *bucket = NULL;

	if (likely(real_irq))
		bucket = __bucket(real_irq);

	return bucket;
}

#ifdef CONFIG_SMP
static int irq_choose_cpu(unsigned int virt_irq)
{
	cpumask_t mask = irq_desc[virt_irq].affinity;
	int cpuid;

	if (cpus_equal(mask, CPU_MASK_ALL)) {
		static int irq_rover;
		static DEFINE_SPINLOCK(irq_rover_lock);
		unsigned long flags;

		/* Round-robin distribution... */
	do_round_robin:
		spin_lock_irqsave(&irq_rover_lock, flags);

		while (!cpu_online(irq_rover)) {
			if (++irq_rover >= NR_CPUS)
				irq_rover = 0;
		}
		cpuid = irq_rover;
		do {
			if (++irq_rover >= NR_CPUS)
				irq_rover = 0;
		} while (!cpu_online(irq_rover));

		spin_unlock_irqrestore(&irq_rover_lock, flags);
	} else {
		cpumask_t tmp;

		cpus_and(tmp, cpu_online_map, mask);

		if (cpus_empty(tmp))
			goto do_round_robin;

		cpuid = first_cpu(tmp);
	}

	return cpuid;
}
#else
static int irq_choose_cpu(unsigned int virt_irq)
{
	return real_hard_smp_processor_id();
}
#endif

static void sun4u_irq_enable(unsigned int virt_irq)
{
	struct irq_handler_data *data = get_irq_chip_data(virt_irq);

	if (likely(data)) {
		unsigned long cpuid, imap, val;
		unsigned int tid;

		cpuid = irq_choose_cpu(virt_irq);
		imap = data->imap;

		tid = sun4u_compute_tid(imap, cpuid);

		val = upa_readq(imap);
		val &= ~(IMAP_TID_UPA | IMAP_TID_JBUS |
			 IMAP_AID_SAFARI | IMAP_NID_SAFARI);
		val |= tid | IMAP_VALID;
		upa_writeq(val, imap);
	}
}

static void sun4u_irq_disable(unsigned int virt_irq)
{
	struct irq_handler_data *data = get_irq_chip_data(virt_irq);

	if (likely(data)) {
		unsigned long imap = data->imap;
		u32 tmp = upa_readq(imap);

		tmp &= ~IMAP_VALID;
		upa_writeq(tmp, imap);
	}
}

static void sun4u_irq_end(unsigned int virt_irq)
{
	struct irq_handler_data *data = get_irq_chip_data(virt_irq);

	if (likely(data))
		upa_writeq(ICLR_IDLE, data->iclr);
}

static void sun4v_irq_enable(unsigned int virt_irq)
{
	struct ino_bucket *bucket = virt_irq_to_bucket(virt_irq);
	unsigned int ino = bucket - &ivector_table[0];

	if (likely(bucket)) {
		unsigned long cpuid;
		int err;

		cpuid = irq_choose_cpu(virt_irq);

		err = sun4v_intr_settarget(ino, cpuid);
		if (err != HV_EOK)
			printk("sun4v_intr_settarget(%x,%lu): err(%d)\n",
			       ino, cpuid, err);
		err = sun4v_intr_setenabled(ino, HV_INTR_ENABLED);
		if (err != HV_EOK)
			printk("sun4v_intr_setenabled(%x): err(%d)\n",
			       ino, err);
	}
}

static void sun4v_irq_disable(unsigned int virt_irq)
{
	struct ino_bucket *bucket = virt_irq_to_bucket(virt_irq);
	unsigned int ino = bucket - &ivector_table[0];

	if (likely(bucket)) {
		int err;

		err = sun4v_intr_setenabled(ino, HV_INTR_DISABLED);
		if (err != HV_EOK)
			printk("sun4v_intr_setenabled(%x): "
			       "err(%d)\n", ino, err);
	}
}

#ifdef CONFIG_PCI_MSI
static void sun4v_msi_enable(unsigned int virt_irq)
{
	sun4v_irq_enable(virt_irq);
	unmask_msi_irq(virt_irq);
}

static void sun4v_msi_disable(unsigned int virt_irq)
{
	mask_msi_irq(virt_irq);
	sun4v_irq_disable(virt_irq);
}
#endif

static void sun4v_irq_end(unsigned int virt_irq)
{
	struct ino_bucket *bucket = virt_irq_to_bucket(virt_irq);
	unsigned int ino = bucket - &ivector_table[0];

	if (likely(bucket)) {
		int err;

		err = sun4v_intr_setstate(ino, HV_INTR_STATE_IDLE);
		if (err != HV_EOK)
			printk("sun4v_intr_setstate(%x): "
			       "err(%d)\n", ino, err);
	}
}

static void run_pre_handler(unsigned int virt_irq)
{
	struct ino_bucket *bucket = virt_irq_to_bucket(virt_irq);
	struct irq_handler_data *data = get_irq_chip_data(virt_irq);

	if (likely(data->pre_handler)) {
		data->pre_handler(__irq_ino(__irq(bucket)),
				  data->pre_handler_arg1,
				  data->pre_handler_arg2);
	}
}

static struct irq_chip sun4u_irq = {
	.typename	= "sun4u",
	.enable		= sun4u_irq_enable,
	.disable	= sun4u_irq_disable,
	.end		= sun4u_irq_end,
};

static struct irq_chip sun4u_irq_ack = {
	.typename	= "sun4u+ack",
	.enable		= sun4u_irq_enable,
	.disable	= sun4u_irq_disable,
	.ack		= run_pre_handler,
	.end		= sun4u_irq_end,
};

static struct irq_chip sun4v_irq = {
	.typename	= "sun4v",
	.enable		= sun4v_irq_enable,
	.disable	= sun4v_irq_disable,
	.end		= sun4v_irq_end,
};

static struct irq_chip sun4v_irq_ack = {
	.typename	= "sun4v+ack",
	.enable		= sun4v_irq_enable,
	.disable	= sun4v_irq_disable,
	.ack		= run_pre_handler,
	.end		= sun4v_irq_end,
};

#ifdef CONFIG_PCI_MSI
static struct irq_chip sun4v_msi = {
	.typename	= "sun4v+msi",
	.mask		= mask_msi_irq,
	.unmask		= unmask_msi_irq,
	.enable		= sun4v_msi_enable,
	.disable	= sun4v_msi_disable,
	.ack		= run_pre_handler,
	.end		= sun4v_irq_end,
};
#endif

void irq_install_pre_handler(int virt_irq,
			     void (*func)(unsigned int, void *, void *),
			     void *arg1, void *arg2)
{
	struct irq_handler_data *data = get_irq_chip_data(virt_irq);
	struct irq_chip *chip;

	data->pre_handler = func;
	data->pre_handler_arg1 = arg1;
	data->pre_handler_arg2 = arg2;

	chip = get_irq_chip(virt_irq);
	if (chip == &sun4u_irq_ack ||
	    chip == &sun4v_irq_ack
#ifdef CONFIG_PCI_MSI
	    || chip == &sun4v_msi
#endif
	    )
		return;

	chip = (chip == &sun4u_irq ?
		&sun4u_irq_ack : &sun4v_irq_ack);
	set_irq_chip(virt_irq, chip);
}

unsigned int build_irq(int inofixup, unsigned long iclr, unsigned long imap)
{
	struct ino_bucket *bucket;
	struct irq_handler_data *data;
	int ino;

	BUG_ON(tlb_type == hypervisor);

	ino = (upa_readq(imap) & (IMAP_IGN | IMAP_INO)) + inofixup;
	bucket = &ivector_table[ino];
	if (!bucket->virt_irq) {
		bucket->virt_irq = virt_irq_alloc(__irq(bucket));
		set_irq_chip(bucket->virt_irq, &sun4u_irq);
	}

	data = get_irq_chip_data(bucket->virt_irq);
	if (unlikely(data))
		goto out;

	data = kzalloc(sizeof(struct irq_handler_data), GFP_ATOMIC);
	if (unlikely(!data)) {
		prom_printf("IRQ: kzalloc(irq_handler_data) failed.\n");
		prom_halt();
	}
	set_irq_chip_data(bucket->virt_irq, data);

	data->imap  = imap;
	data->iclr  = iclr;

out:
	return bucket->virt_irq;
}

unsigned int sun4v_build_irq(u32 devhandle, unsigned int devino)
{
	struct ino_bucket *bucket;
	struct irq_handler_data *data;
	unsigned long sysino;

	BUG_ON(tlb_type != hypervisor);

	sysino = sun4v_devino_to_sysino(devhandle, devino);
	bucket = &ivector_table[sysino];
	if (!bucket->virt_irq) {
		bucket->virt_irq = virt_irq_alloc(__irq(bucket));
		set_irq_chip(bucket->virt_irq, &sun4v_irq);
	}

	data = get_irq_chip_data(bucket->virt_irq);
	if (unlikely(data))
		goto out;

	data = kzalloc(sizeof(struct irq_handler_data), GFP_ATOMIC);
	if (unlikely(!data)) {
		prom_printf("IRQ: kzalloc(irq_handler_data) failed.\n");
		prom_halt();
	}
	set_irq_chip_data(bucket->virt_irq, data);

	/* Catch accidental accesses to these things.  IMAP/ICLR handling
	 * is done by hypervisor calls on sun4v platforms, not by direct
	 * register accesses.
	 */
	data->imap = ~0UL;
	data->iclr = ~0UL;

out:
	return bucket->virt_irq;
}

#ifdef CONFIG_PCI_MSI
unsigned int sun4v_build_msi(u32 devhandle, unsigned int *virt_irq_p,
			     unsigned int msi_start, unsigned int msi_end)
{
	struct ino_bucket *bucket;
	struct irq_handler_data *data;
	unsigned long sysino;
	unsigned int devino;

	BUG_ON(tlb_type != hypervisor);

	/* Find a free devino in the given range.  */
	for (devino = msi_start; devino < msi_end; devino++) {
		sysino = sun4v_devino_to_sysino(devhandle, devino);
		bucket = &ivector_table[sysino];
		if (!bucket->virt_irq)
			break;
	}
	if (devino >= msi_end)
		return 0;

	sysino = sun4v_devino_to_sysino(devhandle, devino);
	bucket = &ivector_table[sysino];
	bucket->virt_irq = virt_irq_alloc(__irq(bucket));
	*virt_irq_p = bucket->virt_irq;
	set_irq_chip(bucket->virt_irq, &sun4v_msi);

	data = get_irq_chip_data(bucket->virt_irq);
	if (unlikely(data))
		return devino;

	data = kzalloc(sizeof(struct irq_handler_data), GFP_ATOMIC);
	if (unlikely(!data)) {
		prom_printf("IRQ: kzalloc(irq_handler_data) failed.\n");
		prom_halt();
	}
	set_irq_chip_data(bucket->virt_irq, data);

	data->imap = ~0UL;
	data->iclr = ~0UL;

	return devino;
}

void sun4v_destroy_msi(unsigned int virt_irq)
{
	virt_irq_free(virt_irq);
}
#endif

void ack_bad_irq(unsigned int virt_irq)
{
	struct ino_bucket *bucket = virt_irq_to_bucket(virt_irq);
	unsigned int ino = 0xdeadbeef;

	if (bucket)
		ino = bucket - &ivector_table[0];

	printk(KERN_CRIT "Unexpected IRQ from ino[%x] virt_irq[%u]\n",
	       ino, virt_irq);
}

void handler_irq(int irq, struct pt_regs *regs)
{
	struct ino_bucket *bucket;
	struct pt_regs *old_regs;

	clear_softint(1 << irq);

	old_regs = set_irq_regs(regs);
	irq_enter();

	/* Sliiiick... */
	bucket = __bucket(xchg32(irq_work(smp_processor_id()), 0));
	while (bucket) {
		struct ino_bucket *next = __bucket(bucket->irq_chain);

		bucket->irq_chain = 0;
		__do_IRQ(bucket->virt_irq);

		bucket = next;
	}

	irq_exit();
	set_irq_regs(old_regs);
}

struct sun5_timer {
	u64	count0;
	u64	limit0;
	u64	count1;
	u64	limit1;
};

static struct sun5_timer *prom_timers;
static u64 prom_limit0, prom_limit1;

static void map_prom_timers(void)
{
	struct device_node *dp;
	const unsigned int *addr;

	/* PROM timer node hangs out in the top level of device siblings... */
	dp = of_find_node_by_path("/");
	dp = dp->child;
	while (dp) {
		if (!strcmp(dp->name, "counter-timer"))
			break;
		dp = dp->sibling;
	}

	/* Assume if node is not present, PROM uses different tick mechanism
	 * which we should not care about.
	 */
	if (!dp) {
		prom_timers = (struct sun5_timer *) 0;
		return;
	}

	/* If PROM is really using this, it must be mapped by him. */
	addr = of_get_property(dp, "address", NULL);
	if (!addr) {
		prom_printf("PROM does not have timer mapped, trying to continue.\n");
		prom_timers = (struct sun5_timer *) 0;
		return;
	}
	prom_timers = (struct sun5_timer *) ((unsigned long)addr[0]);
}

static void kill_prom_timer(void)
{
	if (!prom_timers)
		return;

	/* Save them away for later. */
	prom_limit0 = prom_timers->limit0;
	prom_limit1 = prom_timers->limit1;

	/* Just as in sun4c/sun4m PROM uses timer which ticks at IRQ 14.
	 * We turn both off here just to be paranoid.
	 */
	prom_timers->limit0 = 0;
	prom_timers->limit1 = 0;

	/* Wheee, eat the interrupt packet too... */
	__asm__ __volatile__(
"	mov	0x40, %%g2\n"
"	ldxa	[%%g0] %0, %%g1\n"
"	ldxa	[%%g2] %1, %%g1\n"
"	stxa	%%g0, [%%g0] %0\n"
"	membar	#Sync\n"
	: /* no outputs */
	: "i" (ASI_INTR_RECEIVE), "i" (ASI_INTR_R)
	: "g1", "g2");
}

void init_irqwork_curcpu(void)
{
	int cpu = hard_smp_processor_id();

	trap_block[cpu].irq_worklist = 0;
}

/* Please be very careful with register_one_mondo() and
 * sun4v_register_mondo_queues().
 *
 * On SMP this gets invoked from the CPU trampoline before
 * the cpu has fully taken over the trap table from OBP,
 * and it's kernel stack + %g6 thread register state is
 * not fully cooked yet.
 *
 * Therefore you cannot make any OBP calls, not even prom_printf,
 * from these two routines.
 */
static void __cpuinit register_one_mondo(unsigned long paddr, unsigned long type, unsigned long qmask)
{
	unsigned long num_entries = (qmask + 1) / 64;
	unsigned long status;

	status = sun4v_cpu_qconf(type, paddr, num_entries);
	if (status != HV_EOK) {
		prom_printf("SUN4V: sun4v_cpu_qconf(%lu:%lx:%lu) failed, "
			    "err %lu\n", type, paddr, num_entries, status);
		prom_halt();
	}
}

static void __cpuinit sun4v_register_mondo_queues(int this_cpu)
{
	struct trap_per_cpu *tb = &trap_block[this_cpu];

	register_one_mondo(tb->cpu_mondo_pa, HV_CPU_QUEUE_CPU_MONDO,
			   tb->cpu_mondo_qmask);
	register_one_mondo(tb->dev_mondo_pa, HV_CPU_QUEUE_DEVICE_MONDO,
			   tb->dev_mondo_qmask);
	register_one_mondo(tb->resum_mondo_pa, HV_CPU_QUEUE_RES_ERROR,
			   tb->resum_qmask);
	register_one_mondo(tb->nonresum_mondo_pa, HV_CPU_QUEUE_NONRES_ERROR,
			   tb->nonresum_qmask);
}

static void __cpuinit alloc_one_mondo(unsigned long *pa_ptr, unsigned long qmask, int use_bootmem)
{
	unsigned long size = PAGE_ALIGN(qmask + 1);
	unsigned long order = get_order(size);
	void *p = NULL;

	if (use_bootmem) {
		p = __alloc_bootmem_low(size, size, 0);
	} else {
		struct page *page = alloc_pages(GFP_ATOMIC | __GFP_ZERO, order);
		if (page)
			p = page_address(page);
	}

	if (!p) {
		prom_printf("SUN4V: Error, cannot allocate mondo queue.\n");
		prom_halt();
	}

	*pa_ptr = __pa(p);
}

static void __cpuinit alloc_one_kbuf(unsigned long *pa_ptr, unsigned long qmask, int use_bootmem)
{
	unsigned long size = PAGE_ALIGN(qmask + 1);
	unsigned long order = get_order(size);
	void *p = NULL;

	if (use_bootmem) {
		p = __alloc_bootmem_low(size, size, 0);
	} else {
		struct page *page = alloc_pages(GFP_ATOMIC | __GFP_ZERO, order);
		if (page)
			p = page_address(page);
	}

	if (!p) {
		prom_printf("SUN4V: Error, cannot allocate kbuf page.\n");
		prom_halt();
	}

	*pa_ptr = __pa(p);
}

static void __cpuinit init_cpu_send_mondo_info(struct trap_per_cpu *tb, int use_bootmem)
{
#ifdef CONFIG_SMP
	void *page;

	BUILD_BUG_ON((NR_CPUS * sizeof(u16)) > (PAGE_SIZE - 64));

	if (use_bootmem)
		page = alloc_bootmem_low_pages(PAGE_SIZE);
	else
		page = (void *) get_zeroed_page(GFP_ATOMIC);

	if (!page) {
		prom_printf("SUN4V: Error, cannot allocate cpu mondo page.\n");
		prom_halt();
	}

	tb->cpu_mondo_block_pa = __pa(page);
	tb->cpu_list_pa = __pa(page + 64);
#endif
}

/* Allocate and register the mondo and error queues for this cpu.  */
void __cpuinit sun4v_init_mondo_queues(int use_bootmem, int cpu, int alloc, int load)
{
	struct trap_per_cpu *tb = &trap_block[cpu];

	if (alloc) {
		alloc_one_mondo(&tb->cpu_mondo_pa, tb->cpu_mondo_qmask, use_bootmem);
		alloc_one_mondo(&tb->dev_mondo_pa, tb->dev_mondo_qmask, use_bootmem);
		alloc_one_mondo(&tb->resum_mondo_pa, tb->resum_qmask, use_bootmem);
		alloc_one_kbuf(&tb->resum_kernel_buf_pa, tb->resum_qmask, use_bootmem);
		alloc_one_mondo(&tb->nonresum_mondo_pa, tb->nonresum_qmask, use_bootmem);
		alloc_one_kbuf(&tb->nonresum_kernel_buf_pa, tb->nonresum_qmask, use_bootmem);

		init_cpu_send_mondo_info(tb, use_bootmem);
	}

	if (load) {
		if (cpu != hard_smp_processor_id()) {
			prom_printf("SUN4V: init mondo on cpu %d not %d\n",
				    cpu, hard_smp_processor_id());
			prom_halt();
		}
		sun4v_register_mondo_queues(cpu);
	}
}

static struct irqaction timer_irq_action = {
	.name = "timer",
};

/* Only invoked on boot processor. */
void __init init_IRQ(void)
{
	map_prom_timers();
	kill_prom_timer();
	memset(&ivector_table[0], 0, sizeof(ivector_table));

	if (tlb_type == hypervisor)
		sun4v_init_mondo_queues(1, hard_smp_processor_id(), 1, 1);

	/* We need to clear any IRQ's pending in the soft interrupt
	 * registers, a spurious one could be left around from the
	 * PROM timer which we just disabled.
	 */
	clear_softint(get_softint());

	/* Now that ivector table is initialized, it is safe
	 * to receive IRQ vector traps.  We will normally take
	 * one or two right now, in case some device PROM used
	 * to boot us wants to speak to us.  We just ignore them.
	 */
	__asm__ __volatile__("rdpr	%%pstate, %%g1\n\t"
			     "or	%%g1, %0, %%g1\n\t"
			     "wrpr	%%g1, 0x0, %%pstate"
			     : /* No outputs */
			     : "i" (PSTATE_IE)
			     : "g1");

	irq_desc[0].action = &timer_irq_action;
}
