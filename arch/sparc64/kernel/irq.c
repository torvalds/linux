/* $Id: irq.c,v 1.114 2002/01/11 08:45:38 davem Exp $
 * irq.c: UltraSparc IRQ handling/init/registry.
 *
 * Copyright (C) 1997  David S. Miller  (davem@caip.rutgers.edu)
 * Copyright (C) 1998  Eddie C. Dost    (ecd@skynet.be)
 * Copyright (C) 1998  Jakub Jelinek    (jj@ultra.linux.cz)
 */

#include <linux/config.h>
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
#include <asm/timer.h>
#include <asm/smp.h>
#include <asm/starfire.h>
#include <asm/uaccess.h>
#include <asm/cache.h>
#include <asm/cpudata.h>
#include <asm/auxio.h>
#include <asm/head.h>

#ifdef CONFIG_SMP
static void distribute_irqs(void);
#endif

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
 */

struct ino_bucket ivector_table[NUM_IVECS] __attribute__ ((aligned (SMP_CACHE_BYTES)));

/* This has to be in the main kernel image, it cannot be
 * turned into per-cpu data.  The reason is that the main
 * kernel image is locked into the TLB and this structure
 * is accessed from the vectored interrupt trap handler.  If
 * access to this structure takes a TLB miss it could cause
 * the 5-level sparc v9 trap stack to overflow.
 */
struct irq_work_struct {
	unsigned int	irq_worklists[16];
};
struct irq_work_struct __irq_work[NR_CPUS];
#define irq_work(__cpu, __pil)	&(__irq_work[(__cpu)].irq_worklists[(__pil)])

static struct irqaction *irq_action[NR_IRQS+1];

/* This only synchronizes entities which modify IRQ handler
 * state and some selected user-level spots that want to
 * read things in the table.  IRQ handler processing orders
 * its' accesses such that no locking is needed.
 */
static DEFINE_SPINLOCK(irq_action_lock);

static void register_irq_proc (unsigned int irq);

/*
 * Upper 2b of irqaction->flags holds the ino.
 * irqaction->mask holds the smp affinity information.
 */
#define put_ino_in_irqaction(action, irq) \
	action->flags &= 0xffffffffffffUL; \
	if (__bucket(irq) == &pil0_dummy_bucket) \
		action->flags |= 0xdeadUL << 48;  \
	else \
		action->flags |= __irq_ino(irq) << 48;
#define get_ino_in_irqaction(action)	(action->flags >> 48)

#define put_smpaff_in_irqaction(action, smpaff)	(action)->mask = (smpaff)
#define get_smpaff_in_irqaction(action) 	((action)->mask)

int show_interrupts(struct seq_file *p, void *v)
{
	unsigned long flags;
	int i = *(loff_t *) v;
	struct irqaction *action;
#ifdef CONFIG_SMP
	int j;
#endif

	spin_lock_irqsave(&irq_action_lock, flags);
	if (i <= NR_IRQS) {
		if (!(action = *(i + irq_action)))
			goto out_unlock;
		seq_printf(p, "%3d: ", i);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for_each_online_cpu(j) {
			seq_printf(p, "%10u ",
				   kstat_cpu(j).irqs[i]);
		}
#endif
		seq_printf(p, " %s:%lx", action->name,
			   get_ino_in_irqaction(action));
		for (action = action->next; action; action = action->next) {
			seq_printf(p, ", %s:%lx", action->name,
				   get_ino_in_irqaction(action));
		}
		seq_putc(p, '\n');
	}
out_unlock:
	spin_unlock_irqrestore(&irq_action_lock, flags);

	return 0;
}

extern unsigned long real_hard_smp_processor_id(void);

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

/* Now these are always passed a true fully specified sun4u INO. */
void enable_irq(unsigned int irq)
{
	struct ino_bucket *bucket = __bucket(irq);
	unsigned long imap, cpuid;

	imap = bucket->imap;
	if (imap == 0UL)
		return;

	preempt_disable();

	/* This gets the physical processor ID, even on uniprocessor,
	 * so we can always program the interrupt target correctly.
	 */
	cpuid = real_hard_smp_processor_id();

	if (tlb_type == hypervisor) {
		unsigned int ino = __irq_ino(irq);
		int err;

		err = sun4v_intr_settarget(ino, cpuid);
		if (err != HV_EOK)
			printk("sun4v_intr_settarget(%x,%lu): err(%d)\n",
			       ino, cpuid, err);
		err = sun4v_intr_setenabled(ino, HV_INTR_ENABLED);
		if (err != HV_EOK)
			printk("sun4v_intr_setenabled(%x): err(%d)\n",
			       ino, err);
	} else {
		unsigned int tid = sun4u_compute_tid(imap, cpuid);

		/* NOTE NOTE NOTE, IGN and INO are read-only, IGN is a product
		 * of this SYSIO's preconfigured IGN in the SYSIO Control
		 * Register, the hardware just mirrors that value here.
		 * However for Graphics and UPA Slave devices the full
		 * IMAP_INR field can be set by the programmer here.
		 *
		 * Things like FFB can now be handled via the new IRQ
		 * mechanism.
		 */
		upa_writel(tid | IMAP_VALID, imap);
	}

	preempt_enable();
}

/* This now gets passed true ino's as well. */
void disable_irq(unsigned int irq)
{
	struct ino_bucket *bucket = __bucket(irq);
	unsigned long imap;

	imap = bucket->imap;
	if (imap != 0UL) {
		if (tlb_type == hypervisor) {
			unsigned int ino = __irq_ino(irq);
			int err;

			err = sun4v_intr_setenabled(ino, HV_INTR_DISABLED);
			if (err != HV_EOK)
				printk("sun4v_intr_setenabled(%x): "
				       "err(%d)\n", ino, err);
		} else {
			u32 tmp;

			/* NOTE: We do not want to futz with the IRQ clear registers
			 *       and move the state to IDLE, the SCSI code does call
			 *       disable_irq() to assure atomicity in the queue cmd
			 *       SCSI adapter driver code.  Thus we'd lose interrupts.
			 */
			tmp = upa_readl(imap);
			tmp &= ~IMAP_VALID;
			upa_writel(tmp, imap);
		}
	}
}

/* The timer is the one "weird" interrupt which is generated by
 * the CPU %tick register and not by some normal vectored interrupt
 * source.  To handle this special case, we use this dummy INO bucket.
 */
static struct irq_desc pil0_dummy_desc;
static struct ino_bucket pil0_dummy_bucket = {
	.irq_info	=	&pil0_dummy_desc,
};

static void build_irq_error(const char *msg, unsigned int ino, int pil, int inofixup,
			    unsigned long iclr, unsigned long imap,
			    struct ino_bucket *bucket)
{
	prom_printf("IRQ: INO %04x (%d:%016lx:%016lx) --> "
		    "(%d:%d:%016lx:%016lx), halting...\n",
		    ino, bucket->pil, bucket->iclr, bucket->imap,
		    pil, inofixup, iclr, imap);
	prom_halt();
}

unsigned int build_irq(int pil, int inofixup, unsigned long iclr, unsigned long imap)
{
	struct ino_bucket *bucket;
	int ino;

	if (pil == 0) {
		if (iclr != 0UL || imap != 0UL) {
			prom_printf("Invalid dummy bucket for PIL0 (%lx:%lx)\n",
				    iclr, imap);
			prom_halt();
		}
		return __irq(&pil0_dummy_bucket);
	}

	BUG_ON(tlb_type == hypervisor);

	/* RULE: Both must be specified in all other cases. */
	if (iclr == 0UL || imap == 0UL) {
		prom_printf("Invalid build_irq %d %d %016lx %016lx\n",
			    pil, inofixup, iclr, imap);
		prom_halt();
	}
	
	ino = (upa_readl(imap) & (IMAP_IGN | IMAP_INO)) + inofixup;
	if (ino > NUM_IVECS) {
		prom_printf("Invalid INO %04x (%d:%d:%016lx:%016lx)\n",
			    ino, pil, inofixup, iclr, imap);
		prom_halt();
	}

	bucket = &ivector_table[ino];
	if (bucket->flags & IBF_ACTIVE)
		build_irq_error("IRQ: Trying to build active INO bucket.\n",
				ino, pil, inofixup, iclr, imap, bucket);

	if (bucket->irq_info) {
		if (bucket->imap != imap || bucket->iclr != iclr)
			build_irq_error("IRQ: Trying to reinit INO bucket.\n",
					ino, pil, inofixup, iclr, imap, bucket);

		goto out;
	}

	bucket->irq_info = kzalloc(sizeof(struct irq_desc), GFP_ATOMIC);
	if (!bucket->irq_info) {
		prom_printf("IRQ: Error, kmalloc(irq_desc) failed.\n");
		prom_halt();
	}

	/* Ok, looks good, set it up.  Don't touch the irq_chain or
	 * the pending flag.
	 */
	bucket->imap  = imap;
	bucket->iclr  = iclr;
	bucket->pil   = pil;
	bucket->flags = 0;

out:
	return __irq(bucket);
}

unsigned int sun4v_build_irq(u32 devhandle, unsigned int devino, int pil, unsigned char flags)
{
	struct ino_bucket *bucket;
	unsigned long sysino;

	sysino = sun4v_devino_to_sysino(devhandle, devino);

	bucket = &ivector_table[sysino];

	/* Catch accidental accesses to these things.  IMAP/ICLR handling
	 * is done by hypervisor calls on sun4v platforms, not by direct
	 * register accesses.
	 *
	 * But we need to make them look unique for the disable_irq() logic
	 * in free_irq().
	 */
	bucket->imap = ~0UL - sysino;
	bucket->iclr = ~0UL - sysino;

	bucket->pil = pil;
	bucket->flags = flags;

	bucket->irq_info = kzalloc(sizeof(struct irq_desc), GFP_ATOMIC);
	if (!bucket->irq_info) {
		prom_printf("IRQ: Error, kmalloc(irq_desc) failed.\n");
		prom_halt();
	}

	return __irq(bucket);
}

static void atomic_bucket_insert(struct ino_bucket *bucket)
{
	unsigned long pstate;
	unsigned int *ent;

	__asm__ __volatile__("rdpr %%pstate, %0" : "=r" (pstate));
	__asm__ __volatile__("wrpr %0, %1, %%pstate"
			     : : "r" (pstate), "i" (PSTATE_IE));
	ent = irq_work(smp_processor_id(), bucket->pil);
	bucket->irq_chain = *ent;
	*ent = __irq(bucket);
	__asm__ __volatile__("wrpr %0, 0x0, %%pstate" : : "r" (pstate));
}

static int check_irq_sharing(int pil, unsigned long irqflags)
{
	struct irqaction *action, *tmp;

	action = *(irq_action + pil);
	if (action) {
		if ((action->flags & SA_SHIRQ) && (irqflags & SA_SHIRQ)) {
			for (tmp = action; tmp->next; tmp = tmp->next)
				;
		} else {
			return -EBUSY;
		}
	}
	return 0;
}

static void append_irq_action(int pil, struct irqaction *action)
{
	struct irqaction **pp = irq_action + pil;

	while (*pp)
		pp = &((*pp)->next);
	*pp = action;
}

static struct irqaction *get_action_slot(struct ino_bucket *bucket)
{
	struct irq_desc *desc = bucket->irq_info;
	int max_irq, i;

	max_irq = 1;
	if (bucket->flags & IBF_PCI)
		max_irq = MAX_IRQ_DESC_ACTION;
	for (i = 0; i < max_irq; i++) {
		struct irqaction *p = &desc->action[i];
		u32 mask = (1 << i);

		if (desc->action_active_mask & mask)
			continue;

		desc->action_active_mask |= mask;
		return p;
	}
	return NULL;
}

int request_irq(unsigned int irq, irqreturn_t (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char *name, void *dev_id)
{
	struct irqaction *action;
	struct ino_bucket *bucket = __bucket(irq);
	unsigned long flags;
	int pending = 0;

	if (unlikely(!handler))
		return -EINVAL;

	if (unlikely(!bucket->irq_info))
		return -ENODEV;

	if ((bucket != &pil0_dummy_bucket) && (irqflags & SA_SAMPLE_RANDOM)) {
		/*
	 	 * This function might sleep, we want to call it first,
	 	 * outside of the atomic block. In SA_STATIC_ALLOC case,
		 * random driver's kmalloc will fail, but it is safe.
		 * If already initialized, random driver will not reinit.
	 	 * Yes, this might clear the entropy pool if the wrong
	 	 * driver is attempted to be loaded, without actually
	 	 * installing a new handler, but is this really a problem,
	 	 * only the sysadmin is able to do this.
	 	 */
		rand_initialize_irq(irq);
	}

	spin_lock_irqsave(&irq_action_lock, flags);

	if (check_irq_sharing(bucket->pil, irqflags)) {
		spin_unlock_irqrestore(&irq_action_lock, flags);
		return -EBUSY;
	}

	action = get_action_slot(bucket);
	if (!action) { 
		spin_unlock_irqrestore(&irq_action_lock, flags);
		return -ENOMEM;
	}

	bucket->flags |= IBF_ACTIVE;
	pending = 0;
	if (bucket != &pil0_dummy_bucket) {
		pending = bucket->pending;
		if (pending)
			bucket->pending = 0;
	}

	action->handler = handler;
	action->flags = irqflags;
	action->name = name;
	action->next = NULL;
	action->dev_id = dev_id;
	put_ino_in_irqaction(action, irq);
	put_smpaff_in_irqaction(action, CPU_MASK_NONE);

	append_irq_action(bucket->pil, action);

	enable_irq(irq);

	/* We ate the IVEC already, this makes sure it does not get lost. */
	if (pending) {
		atomic_bucket_insert(bucket);
		set_softint(1 << bucket->pil);
	}

	spin_unlock_irqrestore(&irq_action_lock, flags);

	if (bucket != &pil0_dummy_bucket)
		register_irq_proc(__irq_ino(irq));

#ifdef CONFIG_SMP
	distribute_irqs();
#endif
	return 0;
}

EXPORT_SYMBOL(request_irq);

static struct irqaction *unlink_irq_action(unsigned int irq, void *dev_id)
{
	struct ino_bucket *bucket = __bucket(irq);
	struct irqaction *action, **pp;

	pp = irq_action + bucket->pil;
	action = *pp;
	if (unlikely(!action))
		return NULL;

	if (unlikely(!action->handler)) {
		printk("Freeing free IRQ %d\n", bucket->pil);
		return NULL;
	}

	while (action && action->dev_id != dev_id) {
		pp = &action->next;
		action = *pp;
	}

	if (likely(action))
		*pp = action->next;

	return action;
}

void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction *action;
	struct ino_bucket *bucket;
	unsigned long flags;

	spin_lock_irqsave(&irq_action_lock, flags);

	action = unlink_irq_action(irq, dev_id);

	spin_unlock_irqrestore(&irq_action_lock, flags);

	if (unlikely(!action))
		return;

	synchronize_irq(irq);

	spin_lock_irqsave(&irq_action_lock, flags);

	bucket = __bucket(irq);
	if (bucket != &pil0_dummy_bucket) {
		struct irq_desc *desc = bucket->irq_info;
		int ent, i;

		for (i = 0; i < MAX_IRQ_DESC_ACTION; i++) {
			struct irqaction *p = &desc->action[i];

			if (p == action) {
				desc->action_active_mask &= ~(1 << i);
				break;
			}
		}

		if (!desc->action_active_mask) {
			unsigned long imap = bucket->imap;

			/* This unique interrupt source is now inactive. */
			bucket->flags &= ~IBF_ACTIVE;

			/* See if any other buckets share this bucket's IMAP
			 * and are still active.
			 */
			for (ent = 0; ent < NUM_IVECS; ent++) {
				struct ino_bucket *bp = &ivector_table[ent];
				if (bp != bucket	&&
				    bp->imap == imap	&&
				    (bp->flags & IBF_ACTIVE) != 0)
					break;
			}

			/* Only disable when no other sub-irq levels of
			 * the same IMAP are active.
			 */
			if (ent == NUM_IVECS)
				disable_irq(irq);
		}
	}

	spin_unlock_irqrestore(&irq_action_lock, flags);
}

EXPORT_SYMBOL(free_irq);

#ifdef CONFIG_SMP
void synchronize_irq(unsigned int irq)
{
	struct ino_bucket *bucket = __bucket(irq);

#if 0
	/* The following is how I wish I could implement this.
	 * Unfortunately the ICLR registers are read-only, you can
	 * only write ICLR_foo values to them.  To get the current
	 * IRQ status you would need to get at the IRQ diag registers
	 * in the PCI/SBUS controller and the layout of those vary
	 * from one controller to the next, sigh... -DaveM
	 */
	unsigned long iclr = bucket->iclr;

	while (1) {
		u32 tmp = upa_readl(iclr);
		
		if (tmp == ICLR_TRANSMIT ||
		    tmp == ICLR_PENDING) {
			cpu_relax();
			continue;
		}
		break;
	}
#else
	/* So we have to do this with a INPROGRESS bit just like x86.  */
	while (bucket->flags & IBF_INPROGRESS)
		cpu_relax();
#endif
}
#endif /* CONFIG_SMP */

static void process_bucket(int irq, struct ino_bucket *bp, struct pt_regs *regs)
{
	struct irq_desc *desc = bp->irq_info;
	unsigned char flags = bp->flags;
	u32 action_mask, i;
	int random;

	bp->flags |= IBF_INPROGRESS;

	if (unlikely(!(flags & IBF_ACTIVE))) {
		bp->pending = 1;
		goto out;
	}

	if (desc->pre_handler)
		desc->pre_handler(bp,
				  desc->pre_handler_arg1,
				  desc->pre_handler_arg2);

	action_mask = desc->action_active_mask;
	random = 0;
	for (i = 0; i < MAX_IRQ_DESC_ACTION; i++) {
		struct irqaction *p = &desc->action[i];
		u32 mask = (1 << i);

		if (!(action_mask & mask))
			continue;

		action_mask &= ~mask;

		if (p->handler(__irq(bp), p->dev_id, regs) == IRQ_HANDLED)
			random |= p->flags;

		if (!action_mask)
			break;
	}
	if (bp->pil != 0) {
		if (tlb_type == hypervisor) {
			unsigned int ino = __irq_ino(bp);
			int err;

			err = sun4v_intr_setstate(ino, HV_INTR_STATE_IDLE);
			if (err != HV_EOK)
				printk("sun4v_intr_setstate(%x): "
				       "err(%d)\n", ino, err);
		} else {
			upa_writel(ICLR_IDLE, bp->iclr);
		}

		/* Test and add entropy */
		if (random & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
	}
out:
	bp->flags &= ~IBF_INPROGRESS;
}

void handler_irq(int irq, struct pt_regs *regs)
{
	struct ino_bucket *bp;
	int cpu = smp_processor_id();

#ifndef CONFIG_SMP
	/*
	 * Check for TICK_INT on level 14 softint.
	 */
	{
		unsigned long clr_mask = 1 << irq;
		unsigned long tick_mask = tick_ops->softint_mask;

		if ((irq == 14) && (get_softint() & tick_mask)) {
			irq = 0;
			clr_mask = tick_mask;
		}
		clear_softint(clr_mask);
	}
#else
	clear_softint(1 << irq);
#endif

	irq_enter();
	kstat_this_cpu.irqs[irq]++;

	/* Sliiiick... */
#ifndef CONFIG_SMP
	bp = ((irq != 0) ?
	      __bucket(xchg32(irq_work(cpu, irq), 0)) :
	      &pil0_dummy_bucket);
#else
	bp = __bucket(xchg32(irq_work(cpu, irq), 0));
#endif
	while (bp) {
		struct ino_bucket *nbp = __bucket(bp->irq_chain);

		bp->irq_chain = 0;
		process_bucket(irq, bp, regs);
		bp = nbp;
	}
	irq_exit();
}

#ifdef CONFIG_BLK_DEV_FD
extern irqreturn_t floppy_interrupt(int, void *, struct pt_regs *);

/* XXX No easy way to include asm/floppy.h XXX */
extern unsigned char *pdma_vaddr;
extern unsigned long pdma_size;
extern volatile int doing_pdma;
extern unsigned long fdc_status;

irqreturn_t sparc_floppy_irq(int irq, void *dev_cookie, struct pt_regs *regs)
{
	if (likely(doing_pdma)) {
		void __iomem *stat = (void __iomem *) fdc_status;
		unsigned char *vaddr = pdma_vaddr;
		unsigned long size = pdma_size;
		u8 val;

		while (size) {
			val = readb(stat);
			if (unlikely(!(val & 0x80))) {
				pdma_vaddr = vaddr;
				pdma_size = size;
				return IRQ_HANDLED;
			}
			if (unlikely(!(val & 0x20))) {
				pdma_vaddr = vaddr;
				pdma_size = size;
				doing_pdma = 0;
				goto main_interrupt;
			}
			if (val & 0x40) {
				/* read */
				*vaddr++ = readb(stat + 1);
			} else {
				unsigned char data = *vaddr++;

				/* write */
				writeb(data, stat + 1);
			}
			size--;
		}

		pdma_vaddr = vaddr;
		pdma_size = size;

		/* Send Terminal Count pulse to floppy controller. */
		val = readb(auxio_register);
		val |= AUXIO_AUX1_FTCNT;
		writeb(val, auxio_register);
		val &= ~AUXIO_AUX1_FTCNT;
		writeb(val, auxio_register);

		doing_pdma = 0;
	}

main_interrupt:
	return floppy_interrupt(irq, dev_cookie, regs);
}
EXPORT_SYMBOL(sparc_floppy_irq);
#endif

/* We really don't need these at all on the Sparc.  We only have
 * stubs here because they are exported to modules.
 */
unsigned long probe_irq_on(void)
{
	return 0;
}

EXPORT_SYMBOL(probe_irq_on);

int probe_irq_off(unsigned long mask)
{
	return 0;
}

EXPORT_SYMBOL(probe_irq_off);

#ifdef CONFIG_SMP
static int retarget_one_irq(struct irqaction *p, int goal_cpu)
{
	struct ino_bucket *bucket = get_ino_in_irqaction(p) + ivector_table;

	while (!cpu_online(goal_cpu)) {
		if (++goal_cpu >= NR_CPUS)
			goal_cpu = 0;
	}

	if (tlb_type == hypervisor) {
		unsigned int ino = __irq_ino(bucket);

		sun4v_intr_settarget(ino, goal_cpu);
		sun4v_intr_setenabled(ino, HV_INTR_ENABLED);
	} else {
		unsigned long imap = bucket->imap;
		unsigned int tid = sun4u_compute_tid(imap, goal_cpu);

		upa_writel(tid | IMAP_VALID, imap);
	}

	do {
		if (++goal_cpu >= NR_CPUS)
			goal_cpu = 0;
	} while (!cpu_online(goal_cpu));

	return goal_cpu;
}

/* Called from request_irq. */
static void distribute_irqs(void)
{
	unsigned long flags;
	int cpu, level;

	spin_lock_irqsave(&irq_action_lock, flags);
	cpu = 0;

	/*
	 * Skip the timer at [0], and very rare error/power intrs at [15].
	 * Also level [12], it causes problems on Ex000 systems.
	 */
	for (level = 1; level < NR_IRQS; level++) {
		struct irqaction *p = irq_action[level];

		if (level == 12)
			continue;

		while(p) {
			cpu = retarget_one_irq(p, cpu);
			p = p->next;
		}
	}
	spin_unlock_irqrestore(&irq_action_lock, flags);
}
#endif

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
	unsigned int addr[3];
	int tnode, err;

	/* PROM timer node hangs out in the top level of device siblings... */
	tnode = prom_finddevice("/counter-timer");

	/* Assume if node is not present, PROM uses different tick mechanism
	 * which we should not care about.
	 */
	if (tnode == 0 || tnode == -1) {
		prom_timers = (struct sun5_timer *) 0;
		return;
	}

	/* If PROM is really using this, it must be mapped by him. */
	err = prom_getproperty(tnode, "address", (char *)addr, sizeof(addr));
	if (err == -1) {
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

	memset(__irq_work + cpu, 0, sizeof(struct irq_work_struct));
}

static void __cpuinit register_one_mondo(unsigned long paddr, unsigned long type)
{
	unsigned long num_entries = 128;
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

	register_one_mondo(tb->cpu_mondo_pa, HV_CPU_QUEUE_CPU_MONDO);
	register_one_mondo(tb->dev_mondo_pa, HV_CPU_QUEUE_DEVICE_MONDO);
	register_one_mondo(tb->resum_mondo_pa, HV_CPU_QUEUE_RES_ERROR);
	register_one_mondo(tb->nonresum_mondo_pa, HV_CPU_QUEUE_NONRES_ERROR);
}

static void __cpuinit alloc_one_mondo(unsigned long *pa_ptr, int use_bootmem)
{
	void *page;

	if (use_bootmem)
		page = alloc_bootmem_low_pages(PAGE_SIZE);
	else
		page = (void *) get_zeroed_page(GFP_ATOMIC);

	if (!page) {
		prom_printf("SUN4V: Error, cannot allocate mondo queue.\n");
		prom_halt();
	}

	*pa_ptr = __pa(page);
}

static void __cpuinit alloc_one_kbuf(unsigned long *pa_ptr, int use_bootmem)
{
	void *page;

	if (use_bootmem)
		page = alloc_bootmem_low_pages(PAGE_SIZE);
	else
		page = (void *) get_zeroed_page(GFP_ATOMIC);

	if (!page) {
		prom_printf("SUN4V: Error, cannot allocate kbuf page.\n");
		prom_halt();
	}

	*pa_ptr = __pa(page);
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
		alloc_one_mondo(&tb->cpu_mondo_pa, use_bootmem);
		alloc_one_mondo(&tb->dev_mondo_pa, use_bootmem);
		alloc_one_mondo(&tb->resum_mondo_pa, use_bootmem);
		alloc_one_kbuf(&tb->resum_kernel_buf_pa, use_bootmem);
		alloc_one_mondo(&tb->nonresum_mondo_pa, use_bootmem);
		alloc_one_kbuf(&tb->nonresum_kernel_buf_pa, use_bootmem);

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
}

static struct proc_dir_entry * root_irq_dir;
static struct proc_dir_entry * irq_dir [NUM_IVECS];

#ifdef CONFIG_SMP

static int irq_affinity_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	struct ino_bucket *bp = ivector_table + (long)data;
	struct irq_desc *desc = bp->irq_info;
	struct irqaction *ap = desc->action;
	cpumask_t mask;
	int len;

	mask = get_smpaff_in_irqaction(ap);
	if (cpus_empty(mask))
		mask = cpu_online_map;

	len = cpumask_scnprintf(page, count, mask);
	if (count - len < 2)
		return -EINVAL;
	len += sprintf(page + len, "\n");
	return len;
}

static inline void set_intr_affinity(int irq, cpumask_t hw_aff)
{
	struct ino_bucket *bp = ivector_table + irq;
	struct irq_desc *desc = bp->irq_info;
	struct irqaction *ap = desc->action;

	/* Users specify affinity in terms of hw cpu ids.
	 * As soon as we do this, handler_irq() might see and take action.
	 */
	put_smpaff_in_irqaction(ap, hw_aff);

	/* Migration is simply done by the next cpu to service this
	 * interrupt.
	 */
}

static int irq_affinity_write_proc (struct file *file, const char __user *buffer,
					unsigned long count, void *data)
{
	int irq = (long) data, full_count = count, err;
	cpumask_t new_value;

	err = cpumask_parse(buffer, count, new_value);

	/*
	 * Do not allow disabling IRQs completely - it's a too easy
	 * way to make the system unusable accidentally :-) At least
	 * one online CPU still has to be targeted.
	 */
	cpus_and(new_value, new_value, cpu_online_map);
	if (cpus_empty(new_value))
		return -EINVAL;

	set_intr_affinity(irq, new_value);

	return full_count;
}

#endif

#define MAX_NAMELEN 10

static void register_irq_proc (unsigned int irq)
{
	char name [MAX_NAMELEN];

	if (!root_irq_dir || irq_dir[irq])
		return;

	memset(name, 0, MAX_NAMELEN);
	sprintf(name, "%x", irq);

	/* create /proc/irq/1234 */
	irq_dir[irq] = proc_mkdir(name, root_irq_dir);

#ifdef CONFIG_SMP
	/* XXX SMP affinity not supported on starfire yet. */
	if (this_is_starfire == 0) {
		struct proc_dir_entry *entry;

		/* create /proc/irq/1234/smp_affinity */
		entry = create_proc_entry("smp_affinity", 0600, irq_dir[irq]);

		if (entry) {
			entry->nlink = 1;
			entry->data = (void *)(long)irq;
			entry->read_proc = irq_affinity_read_proc;
			entry->write_proc = irq_affinity_write_proc;
		}
	}
#endif
}

void init_irq_proc (void)
{
	/* create /proc/irq */
	root_irq_dir = proc_mkdir("irq", NULL);
}

