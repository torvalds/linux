// SPDX-License-Identifier: GPL-2.0
/* irq.c: UltraSparc IRQ handling/init/registry.
 *
 * Copyright (C) 1997, 2007, 2008 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1998  Eddie C. Dost    (ecd@skynet.be)
 * Copyright (C) 1998  Jakub Jelinek    (jj@ultra.linux.cz)
 */

#include <linux/sched.h>
#include <linux/linkage.h>
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
#include <linux/ftrace.h>
#include <linux/irq.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <linux/atomic.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/iommu.h>
#include <asm/upa.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/timer.h>
#include <asm/smp.h>
#include <asm/starfire.h>
#include <linux/uaccess.h>
#include <asm/cache.h>
#include <asm/cpudata.h>
#include <asm/auxio.h>
#include <asm/head.h>
#include <asm/hypervisor.h>
#include <asm/cacheflush.h>

#include "entry.h"
#include "cpumap.h"
#include "kstack.h"

struct ino_bucket *ivector_table;
unsigned long ivector_table_pa;

/* On several sun4u processors, it is illegal to mix bypass and
 * non-bypass accesses.  Therefore we access all INO buckets
 * using bypass accesses only.
 */
static unsigned long bucket_get_chain_pa(unsigned long bucket_pa)
{
	unsigned long ret;

	__asm__ __volatile__("ldxa	[%1] %2, %0"
			     : "=&r" (ret)
			     : "r" (bucket_pa +
				    offsetof(struct ino_bucket,
					     __irq_chain_pa)),
			       "i" (ASI_PHYS_USE_EC));

	return ret;
}

static void bucket_clear_chain_pa(unsigned long bucket_pa)
{
	__asm__ __volatile__("stxa	%%g0, [%0] %1"
			     : /* no outputs */
			     : "r" (bucket_pa +
				    offsetof(struct ino_bucket,
					     __irq_chain_pa)),
			       "i" (ASI_PHYS_USE_EC));
}

static unsigned int bucket_get_irq(unsigned long bucket_pa)
{
	unsigned int ret;

	__asm__ __volatile__("lduwa	[%1] %2, %0"
			     : "=&r" (ret)
			     : "r" (bucket_pa +
				    offsetof(struct ino_bucket,
					     __irq)),
			       "i" (ASI_PHYS_USE_EC));

	return ret;
}

static void bucket_set_irq(unsigned long bucket_pa, unsigned int irq)
{
	__asm__ __volatile__("stwa	%0, [%1] %2"
			     : /* no outputs */
			     : "r" (irq),
			       "r" (bucket_pa +
				    offsetof(struct ino_bucket,
					     __irq)),
			       "i" (ASI_PHYS_USE_EC));
}

#define irq_work_pa(__cpu)	&(trap_block[(__cpu)].irq_worklist_pa)

static unsigned long hvirq_major __initdata;
static int __init early_hvirq_major(char *p)
{
	int rc = kstrtoul(p, 10, &hvirq_major);

	return rc;
}
early_param("hvirq", early_hvirq_major);

static int hv_irq_version;

/* Major version 2.0 of HV_GRP_INTR added support for the VIRQ cookie
 * based interfaces, but:
 *
 * 1) Several OSs, Solaris and Linux included, use them even when only
 *    negotiating version 1.0 (or failing to negotiate at all).  So the
 *    hypervisor has a workaround that provides the VIRQ interfaces even
 *    when only verion 1.0 of the API is in use.
 *
 * 2) Second, and more importantly, with major version 2.0 these VIRQ
 *    interfaces only were actually hooked up for LDC interrupts, even
 *    though the Hypervisor specification clearly stated:
 *
 *	The new interrupt API functions will be available to a guest
 *	when it negotiates version 2.0 in the interrupt API group 0x2. When
 *	a guest negotiates version 2.0, all interrupt sources will only
 *	support using the cookie interface, and any attempt to use the
 *	version 1.0 interrupt APIs numbered 0xa0 to 0xa6 will result in the
 *	ENOTSUPPORTED error being returned.
 *
 *   with an emphasis on "all interrupt sources".
 *
 * To correct this, major version 3.0 was created which does actually
 * support VIRQs for all interrupt sources (not just LDC devices).  So
 * if we want to move completely over the cookie based VIRQs we must
 * negotiate major version 3.0 or later of HV_GRP_INTR.
 */
static bool sun4v_cookie_only_virqs(void)
{
	if (hv_irq_version >= 3)
		return true;
	return false;
}

static void __init irq_init_hv(void)
{
	unsigned long hv_error, major, minor = 0;

	if (tlb_type != hypervisor)
		return;

	if (hvirq_major)
		major = hvirq_major;
	else
		major = 3;

	hv_error = sun4v_hvapi_register(HV_GRP_INTR, major, &minor);
	if (!hv_error)
		hv_irq_version = major;
	else
		hv_irq_version = 1;

	pr_info("SUN4V: Using IRQ API major %d, cookie only virqs %s\n",
		hv_irq_version,
		sun4v_cookie_only_virqs() ? "enabled" : "disabled");
}

/* This function is for the timer interrupt.*/
int __init arch_probe_nr_irqs(void)
{
	return 1;
}

#define DEFAULT_NUM_IVECS	(0xfffU)
static unsigned int nr_ivec = DEFAULT_NUM_IVECS;
#define NUM_IVECS (nr_ivec)

static unsigned int __init size_nr_ivec(void)
{
	if (tlb_type == hypervisor) {
		switch (sun4v_chip_type) {
		/* Athena's devhandle|devino is large.*/
		case SUN4V_CHIP_SPARC64X:
			nr_ivec = 0xffff;
			break;
		}
	}
	return nr_ivec;
}

struct irq_handler_data {
	union {
		struct {
			unsigned int dev_handle;
			unsigned int dev_ino;
		};
		unsigned long sysino;
	};
	struct ino_bucket bucket;
	unsigned long	iclr;
	unsigned long	imap;
};

static inline unsigned int irq_data_to_handle(struct irq_data *data)
{
	struct irq_handler_data *ihd = irq_data_get_irq_handler_data(data);

	return ihd->dev_handle;
}

static inline unsigned int irq_data_to_ino(struct irq_data *data)
{
	struct irq_handler_data *ihd = irq_data_get_irq_handler_data(data);

	return ihd->dev_ino;
}

static inline unsigned long irq_data_to_sysino(struct irq_data *data)
{
	struct irq_handler_data *ihd = irq_data_get_irq_handler_data(data);

	return ihd->sysino;
}

void irq_free(unsigned int irq)
{
	void *data = irq_get_handler_data(irq);

	kfree(data);
	irq_set_handler_data(irq, NULL);
	irq_free_descs(irq, 1);
}

unsigned int irq_alloc(unsigned int dev_handle, unsigned int dev_ino)
{
	int irq;

	irq = __irq_alloc_descs(-1, 1, 1, numa_node_id(), NULL, NULL);
	if (irq <= 0)
		goto out;

	return irq;
out:
	return 0;
}

static unsigned int cookie_exists(u32 devhandle, unsigned int devino)
{
	unsigned long hv_err, cookie;
	struct ino_bucket *bucket;
	unsigned int irq = 0U;

	hv_err = sun4v_vintr_get_cookie(devhandle, devino, &cookie);
	if (hv_err) {
		pr_err("HV get cookie failed hv_err = %ld\n", hv_err);
		goto out;
	}

	if (cookie & ((1UL << 63UL))) {
		cookie = ~cookie;
		bucket = (struct ino_bucket *) __va(cookie);
		irq = bucket->__irq;
	}
out:
	return irq;
}

static unsigned int sysino_exists(u32 devhandle, unsigned int devino)
{
	unsigned long sysino = sun4v_devino_to_sysino(devhandle, devino);
	struct ino_bucket *bucket;
	unsigned int irq;

	bucket = &ivector_table[sysino];
	irq = bucket_get_irq(__pa(bucket));

	return irq;
}

void ack_bad_irq(unsigned int irq)
{
	pr_crit("BAD IRQ ack %d\n", irq);
}

void irq_install_pre_handler(int irq,
			     void (*func)(unsigned int, void *, void *),
			     void *arg1, void *arg2)
{
	pr_warn("IRQ pre handler NOT supported.\n");
}

/*
 * /proc/interrupts printing:
 */
int arch_show_interrupts(struct seq_file *p, int prec)
{
	int j;

	seq_printf(p, "NMI: ");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", cpu_data(j).__nmi_count);
	seq_printf(p, "     Non-maskable interrupts\n");
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
					IMAP_NID_SAFARI);
			}
		} else {
			tid = cpuid << IMAP_TID_SHIFT;
			tid &= IMAP_TID_UPA;
		}
	}

	return tid;
}

#ifdef CONFIG_SMP
static int irq_choose_cpu(unsigned int irq, const struct cpumask *affinity)
{
	cpumask_t mask;
	int cpuid;

	cpumask_copy(&mask, affinity);
	if (cpumask_equal(&mask, cpu_online_mask)) {
		cpuid = map_to_cpu(irq);
	} else {
		cpumask_t tmp;

		cpumask_and(&tmp, cpu_online_mask, &mask);
		cpuid = cpumask_empty(&tmp) ? map_to_cpu(irq) : cpumask_first(&tmp);
	}

	return cpuid;
}
#else
#define irq_choose_cpu(irq, affinity)	\
	real_hard_smp_processor_id()
#endif

static void sun4u_irq_enable(struct irq_data *data)
{
	struct irq_handler_data *handler_data;

	handler_data = irq_data_get_irq_handler_data(data);
	if (likely(handler_data)) {
		unsigned long cpuid, imap, val;
		unsigned int tid;

		cpuid = irq_choose_cpu(data->irq,
				       irq_data_get_affinity_mask(data));
		imap = handler_data->imap;

		tid = sun4u_compute_tid(imap, cpuid);

		val = upa_readq(imap);
		val &= ~(IMAP_TID_UPA | IMAP_TID_JBUS |
			 IMAP_AID_SAFARI | IMAP_NID_SAFARI);
		val |= tid | IMAP_VALID;
		upa_writeq(val, imap);
		upa_writeq(ICLR_IDLE, handler_data->iclr);
	}
}

static int sun4u_set_affinity(struct irq_data *data,
			       const struct cpumask *mask, bool force)
{
	struct irq_handler_data *handler_data;

	handler_data = irq_data_get_irq_handler_data(data);
	if (likely(handler_data)) {
		unsigned long cpuid, imap, val;
		unsigned int tid;

		cpuid = irq_choose_cpu(data->irq, mask);
		imap = handler_data->imap;

		tid = sun4u_compute_tid(imap, cpuid);

		val = upa_readq(imap);
		val &= ~(IMAP_TID_UPA | IMAP_TID_JBUS |
			 IMAP_AID_SAFARI | IMAP_NID_SAFARI);
		val |= tid | IMAP_VALID;
		upa_writeq(val, imap);
		upa_writeq(ICLR_IDLE, handler_data->iclr);
	}

	return 0;
}

/* Don't do anything.  The desc->status check for IRQ_DISABLED in
 * handler_irq() will skip the handler call and that will leave the
 * interrupt in the sent state.  The next ->enable() call will hit the
 * ICLR register to reset the state machine.
 *
 * This scheme is necessary, instead of clearing the Valid bit in the
 * IMAP register, to handle the case of IMAP registers being shared by
 * multiple INOs (and thus ICLR registers).  Since we use a different
 * virtual IRQ for each shared IMAP instance, the generic code thinks
 * there is only one user so it prematurely calls ->disable() on
 * free_irq().
 *
 * We have to provide an explicit ->disable() method instead of using
 * NULL to get the default.  The reason is that if the generic code
 * sees that, it also hooks up a default ->shutdown method which
 * invokes ->mask() which we do not want.  See irq_chip_set_defaults().
 */
static void sun4u_irq_disable(struct irq_data *data)
{
}

static void sun4u_irq_eoi(struct irq_data *data)
{
	struct irq_handler_data *handler_data;

	handler_data = irq_data_get_irq_handler_data(data);
	if (likely(handler_data))
		upa_writeq(ICLR_IDLE, handler_data->iclr);
}

static void sun4v_irq_enable(struct irq_data *data)
{
	unsigned long cpuid = irq_choose_cpu(data->irq,
					     irq_data_get_affinity_mask(data));
	unsigned int ino = irq_data_to_sysino(data);
	int err;

	err = sun4v_intr_settarget(ino, cpuid);
	if (err != HV_EOK)
		printk(KERN_ERR "sun4v_intr_settarget(%x,%lu): "
		       "err(%d)\n", ino, cpuid, err);
	err = sun4v_intr_setstate(ino, HV_INTR_STATE_IDLE);
	if (err != HV_EOK)
		printk(KERN_ERR "sun4v_intr_setstate(%x): "
		       "err(%d)\n", ino, err);
	err = sun4v_intr_setenabled(ino, HV_INTR_ENABLED);
	if (err != HV_EOK)
		printk(KERN_ERR "sun4v_intr_setenabled(%x): err(%d)\n",
		       ino, err);
}

static int sun4v_set_affinity(struct irq_data *data,
			       const struct cpumask *mask, bool force)
{
	unsigned long cpuid = irq_choose_cpu(data->irq, mask);
	unsigned int ino = irq_data_to_sysino(data);
	int err;

	err = sun4v_intr_settarget(ino, cpuid);
	if (err != HV_EOK)
		printk(KERN_ERR "sun4v_intr_settarget(%x,%lu): "
		       "err(%d)\n", ino, cpuid, err);

	return 0;
}

static void sun4v_irq_disable(struct irq_data *data)
{
	unsigned int ino = irq_data_to_sysino(data);
	int err;

	err = sun4v_intr_setenabled(ino, HV_INTR_DISABLED);
	if (err != HV_EOK)
		printk(KERN_ERR "sun4v_intr_setenabled(%x): "
		       "err(%d)\n", ino, err);
}

static void sun4v_irq_eoi(struct irq_data *data)
{
	unsigned int ino = irq_data_to_sysino(data);
	int err;

	err = sun4v_intr_setstate(ino, HV_INTR_STATE_IDLE);
	if (err != HV_EOK)
		printk(KERN_ERR "sun4v_intr_setstate(%x): "
		       "err(%d)\n", ino, err);
}

static void sun4v_virq_enable(struct irq_data *data)
{
	unsigned long dev_handle = irq_data_to_handle(data);
	unsigned long dev_ino = irq_data_to_ino(data);
	unsigned long cpuid;
	int err;

	cpuid = irq_choose_cpu(data->irq, irq_data_get_affinity_mask(data));

	err = sun4v_vintr_set_target(dev_handle, dev_ino, cpuid);
	if (err != HV_EOK)
		printk(KERN_ERR "sun4v_vintr_set_target(%lx,%lx,%lu): "
		       "err(%d)\n",
		       dev_handle, dev_ino, cpuid, err);
	err = sun4v_vintr_set_state(dev_handle, dev_ino,
				    HV_INTR_STATE_IDLE);
	if (err != HV_EOK)
		printk(KERN_ERR "sun4v_vintr_set_state(%lx,%lx,"
		       "HV_INTR_STATE_IDLE): err(%d)\n",
		       dev_handle, dev_ino, err);
	err = sun4v_vintr_set_valid(dev_handle, dev_ino,
				    HV_INTR_ENABLED);
	if (err != HV_EOK)
		printk(KERN_ERR "sun4v_vintr_set_state(%lx,%lx,"
		       "HV_INTR_ENABLED): err(%d)\n",
		       dev_handle, dev_ino, err);
}

static int sun4v_virt_set_affinity(struct irq_data *data,
				    const struct cpumask *mask, bool force)
{
	unsigned long dev_handle = irq_data_to_handle(data);
	unsigned long dev_ino = irq_data_to_ino(data);
	unsigned long cpuid;
	int err;

	cpuid = irq_choose_cpu(data->irq, mask);

	err = sun4v_vintr_set_target(dev_handle, dev_ino, cpuid);
	if (err != HV_EOK)
		printk(KERN_ERR "sun4v_vintr_set_target(%lx,%lx,%lu): "
		       "err(%d)\n",
		       dev_handle, dev_ino, cpuid, err);

	return 0;
}

static void sun4v_virq_disable(struct irq_data *data)
{
	unsigned long dev_handle = irq_data_to_handle(data);
	unsigned long dev_ino = irq_data_to_ino(data);
	int err;


	err = sun4v_vintr_set_valid(dev_handle, dev_ino,
				    HV_INTR_DISABLED);
	if (err != HV_EOK)
		printk(KERN_ERR "sun4v_vintr_set_state(%lx,%lx,"
		       "HV_INTR_DISABLED): err(%d)\n",
		       dev_handle, dev_ino, err);
}

static void sun4v_virq_eoi(struct irq_data *data)
{
	unsigned long dev_handle = irq_data_to_handle(data);
	unsigned long dev_ino = irq_data_to_ino(data);
	int err;

	err = sun4v_vintr_set_state(dev_handle, dev_ino,
				    HV_INTR_STATE_IDLE);
	if (err != HV_EOK)
		printk(KERN_ERR "sun4v_vintr_set_state(%lx,%lx,"
		       "HV_INTR_STATE_IDLE): err(%d)\n",
		       dev_handle, dev_ino, err);
}

static struct irq_chip sun4u_irq = {
	.name			= "sun4u",
	.irq_enable		= sun4u_irq_enable,
	.irq_disable		= sun4u_irq_disable,
	.irq_eoi		= sun4u_irq_eoi,
	.irq_set_affinity	= sun4u_set_affinity,
	.flags			= IRQCHIP_EOI_IF_HANDLED,
};

static struct irq_chip sun4v_irq = {
	.name			= "sun4v",
	.irq_enable		= sun4v_irq_enable,
	.irq_disable		= sun4v_irq_disable,
	.irq_eoi		= sun4v_irq_eoi,
	.irq_set_affinity	= sun4v_set_affinity,
	.flags			= IRQCHIP_EOI_IF_HANDLED,
};

static struct irq_chip sun4v_virq = {
	.name			= "vsun4v",
	.irq_enable		= sun4v_virq_enable,
	.irq_disable		= sun4v_virq_disable,
	.irq_eoi		= sun4v_virq_eoi,
	.irq_set_affinity	= sun4v_virt_set_affinity,
	.flags			= IRQCHIP_EOI_IF_HANDLED,
};

unsigned int build_irq(int inofixup, unsigned long iclr, unsigned long imap)
{
	struct irq_handler_data *handler_data;
	struct ino_bucket *bucket;
	unsigned int irq;
	int ino;

	BUG_ON(tlb_type == hypervisor);

	ino = (upa_readq(imap) & (IMAP_IGN | IMAP_INO)) + inofixup;
	bucket = &ivector_table[ino];
	irq = bucket_get_irq(__pa(bucket));
	if (!irq) {
		irq = irq_alloc(0, ino);
		bucket_set_irq(__pa(bucket), irq);
		irq_set_chip_and_handler_name(irq, &sun4u_irq,
					      handle_fasteoi_irq, "IVEC");
	}

	handler_data = irq_get_handler_data(irq);
	if (unlikely(handler_data))
		goto out;

	handler_data = kzalloc(sizeof(struct irq_handler_data), GFP_ATOMIC);
	if (unlikely(!handler_data)) {
		prom_printf("IRQ: kzalloc(irq_handler_data) failed.\n");
		prom_halt();
	}
	irq_set_handler_data(irq, handler_data);

	handler_data->imap  = imap;
	handler_data->iclr  = iclr;

out:
	return irq;
}

static unsigned int sun4v_build_common(u32 devhandle, unsigned int devino,
		void (*handler_data_init)(struct irq_handler_data *data,
		u32 devhandle, unsigned int devino),
		struct irq_chip *chip)
{
	struct irq_handler_data *data;
	unsigned int irq;

	irq = irq_alloc(devhandle, devino);
	if (!irq)
		goto out;

	data = kzalloc(sizeof(struct irq_handler_data), GFP_ATOMIC);
	if (unlikely(!data)) {
		pr_err("IRQ handler data allocation failed.\n");
		irq_free(irq);
		irq = 0;
		goto out;
	}

	irq_set_handler_data(irq, data);
	handler_data_init(data, devhandle, devino);
	irq_set_chip_and_handler_name(irq, chip, handle_fasteoi_irq, "IVEC");
	data->imap = ~0UL;
	data->iclr = ~0UL;
out:
	return irq;
}

static unsigned long cookie_assign(unsigned int irq, u32 devhandle,
		unsigned int devino)
{
	struct irq_handler_data *ihd = irq_get_handler_data(irq);
	unsigned long hv_error, cookie;

	/* handler_irq needs to find the irq. cookie is seen signed in
	 * sun4v_dev_mondo and treated as a non ivector_table delivery.
	 */
	ihd->bucket.__irq = irq;
	cookie = ~__pa(&ihd->bucket);

	hv_error = sun4v_vintr_set_cookie(devhandle, devino, cookie);
	if (hv_error)
		pr_err("HV vintr set cookie failed = %ld\n", hv_error);

	return hv_error;
}

static void cookie_handler_data(struct irq_handler_data *data,
				u32 devhandle, unsigned int devino)
{
	data->dev_handle = devhandle;
	data->dev_ino = devino;
}

static unsigned int cookie_build_irq(u32 devhandle, unsigned int devino,
				     struct irq_chip *chip)
{
	unsigned long hv_error;
	unsigned int irq;

	irq = sun4v_build_common(devhandle, devino, cookie_handler_data, chip);

	hv_error = cookie_assign(irq, devhandle, devino);
	if (hv_error) {
		irq_free(irq);
		irq = 0;
	}

	return irq;
}

static unsigned int sun4v_build_cookie(u32 devhandle, unsigned int devino)
{
	unsigned int irq;

	irq = cookie_exists(devhandle, devino);
	if (irq)
		goto out;

	irq = cookie_build_irq(devhandle, devino, &sun4v_virq);

out:
	return irq;
}

static void sysino_set_bucket(unsigned int irq)
{
	struct irq_handler_data *ihd = irq_get_handler_data(irq);
	struct ino_bucket *bucket;
	unsigned long sysino;

	sysino = sun4v_devino_to_sysino(ihd->dev_handle, ihd->dev_ino);
	BUG_ON(sysino >= nr_ivec);
	bucket = &ivector_table[sysino];
	bucket_set_irq(__pa(bucket), irq);
}

static void sysino_handler_data(struct irq_handler_data *data,
				u32 devhandle, unsigned int devino)
{
	unsigned long sysino;

	sysino = sun4v_devino_to_sysino(devhandle, devino);
	data->sysino = sysino;
}

static unsigned int sysino_build_irq(u32 devhandle, unsigned int devino,
				     struct irq_chip *chip)
{
	unsigned int irq;

	irq = sun4v_build_common(devhandle, devino, sysino_handler_data, chip);
	if (!irq)
		goto out;

	sysino_set_bucket(irq);
out:
	return irq;
}

static int sun4v_build_sysino(u32 devhandle, unsigned int devino)
{
	int irq;

	irq = sysino_exists(devhandle, devino);
	if (irq)
		goto out;

	irq = sysino_build_irq(devhandle, devino, &sun4v_irq);
out:
	return irq;
}

unsigned int sun4v_build_irq(u32 devhandle, unsigned int devino)
{
	unsigned int irq;

	if (sun4v_cookie_only_virqs())
		irq = sun4v_build_cookie(devhandle, devino);
	else
		irq = sun4v_build_sysino(devhandle, devino);

	return irq;
}

unsigned int sun4v_build_virq(u32 devhandle, unsigned int devino)
{
	int irq;

	irq = cookie_build_irq(devhandle, devino, &sun4v_virq);
	if (!irq)
		goto out;

	/* This is borrowed from the original function.
	 */
	irq_set_status_flags(irq, IRQ_NOAUTOEN);

out:
	return irq;
}

void *hardirq_stack[NR_CPUS];
void *softirq_stack[NR_CPUS];

void __irq_entry handler_irq(int pil, struct pt_regs *regs)
{
	unsigned long pstate, bucket_pa;
	struct pt_regs *old_regs;
	void *orig_sp;

	clear_softint(1 << pil);

	old_regs = set_irq_regs(regs);
	irq_enter();

	/* Grab an atomic snapshot of the pending IVECs.  */
	__asm__ __volatile__("rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %3, %%pstate\n\t"
			     "ldx	[%2], %1\n\t"
			     "stx	%%g0, [%2]\n\t"
			     "wrpr	%0, 0x0, %%pstate\n\t"
			     : "=&r" (pstate), "=&r" (bucket_pa)
			     : "r" (irq_work_pa(smp_processor_id())),
			       "i" (PSTATE_IE)
			     : "memory");

	orig_sp = set_hardirq_stack();

	while (bucket_pa) {
		unsigned long next_pa;
		unsigned int irq;

		next_pa = bucket_get_chain_pa(bucket_pa);
		irq = bucket_get_irq(bucket_pa);
		bucket_clear_chain_pa(bucket_pa);

		generic_handle_irq(irq);

		bucket_pa = next_pa;
	}

	restore_hardirq_stack(orig_sp);

	irq_exit();
	set_irq_regs(old_regs);
}

void do_softirq_own_stack(void)
{
	void *orig_sp, *sp = softirq_stack[smp_processor_id()];

	sp += THREAD_SIZE - 192 - STACK_BIAS;

	__asm__ __volatile__("mov %%sp, %0\n\t"
			     "mov %1, %%sp"
			     : "=&r" (orig_sp)
			     : "r" (sp));
	__do_softirq();
	__asm__ __volatile__("mov %0, %%sp"
			     : : "r" (orig_sp));
}

#ifdef CONFIG_HOTPLUG_CPU
void fixup_irqs(void)
{
	unsigned int irq;

	for (irq = 0; irq < NR_IRQS; irq++) {
		struct irq_desc *desc = irq_to_desc(irq);
		struct irq_data *data;
		unsigned long flags;

		if (!desc)
			continue;
		data = irq_desc_get_irq_data(desc);
		raw_spin_lock_irqsave(&desc->lock, flags);
		if (desc->action && !irqd_is_per_cpu(data)) {
			if (data->chip->irq_set_affinity)
				data->chip->irq_set_affinity(data,
					irq_data_get_affinity_mask(data),
					false);
		}
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}

	tick_ops->disable_irq();
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
	struct device_node *dp;
	const unsigned int *addr;

	/* PROM timer node hangs out in the top level of device siblings... */
	dp = of_find_node_by_path("/");
	dp = dp->child;
	while (dp) {
		if (of_node_name_eq(dp, "counter-timer"))
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

	/* Just as in sun4c PROM uses timer which ticks at IRQ 14.
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

void notrace init_irqwork_curcpu(void)
{
	int cpu = hard_smp_processor_id();

	trap_block[cpu].irq_worklist_pa = 0UL;
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
static void notrace register_one_mondo(unsigned long paddr, unsigned long type,
				       unsigned long qmask)
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

void notrace sun4v_register_mondo_queues(int this_cpu)
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

/* Each queue region must be a power of 2 multiple of 64 bytes in
 * size.  The base real address must be aligned to the size of the
 * region.  Thus, an 8KB queue must be 8KB aligned, for example.
 */
static void __init alloc_one_queue(unsigned long *pa_ptr, unsigned long qmask)
{
	unsigned long size = PAGE_ALIGN(qmask + 1);
	unsigned long order = get_order(size);
	unsigned long p;

	p = __get_free_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!p) {
		prom_printf("SUN4V: Error, cannot allocate queue.\n");
		prom_halt();
	}

	*pa_ptr = __pa(p);
}

static void __init init_cpu_send_mondo_info(struct trap_per_cpu *tb)
{
#ifdef CONFIG_SMP
	unsigned long page;
	void *mondo, *p;

	BUILD_BUG_ON((NR_CPUS * sizeof(u16)) > PAGE_SIZE);

	/* Make sure mondo block is 64byte aligned */
	p = kzalloc(127, GFP_KERNEL);
	if (!p) {
		prom_printf("SUN4V: Error, cannot allocate mondo block.\n");
		prom_halt();
	}
	mondo = (void *)(((unsigned long)p + 63) & ~0x3f);
	tb->cpu_mondo_block_pa = __pa(mondo);

	page = get_zeroed_page(GFP_KERNEL);
	if (!page) {
		prom_printf("SUN4V: Error, cannot allocate cpu list page.\n");
		prom_halt();
	}

	tb->cpu_list_pa = __pa(page);
#endif
}

/* Allocate mondo and error queues for all possible cpus.  */
static void __init sun4v_init_mondo_queues(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct trap_per_cpu *tb = &trap_block[cpu];

		alloc_one_queue(&tb->cpu_mondo_pa, tb->cpu_mondo_qmask);
		alloc_one_queue(&tb->dev_mondo_pa, tb->dev_mondo_qmask);
		alloc_one_queue(&tb->resum_mondo_pa, tb->resum_qmask);
		alloc_one_queue(&tb->resum_kernel_buf_pa, tb->resum_qmask);
		alloc_one_queue(&tb->nonresum_mondo_pa, tb->nonresum_qmask);
		alloc_one_queue(&tb->nonresum_kernel_buf_pa,
				tb->nonresum_qmask);
	}
}

static void __init init_send_mondo_info(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct trap_per_cpu *tb = &trap_block[cpu];

		init_cpu_send_mondo_info(tb);
	}
}

static struct irqaction timer_irq_action = {
	.name = "timer",
};

static void __init irq_ivector_init(void)
{
	unsigned long size, order;
	unsigned int ivecs;

	/* If we are doing cookie only VIRQs then we do not need the ivector
	 * table to process interrupts.
	 */
	if (sun4v_cookie_only_virqs())
		return;

	ivecs = size_nr_ivec();
	size = sizeof(struct ino_bucket) * ivecs;
	order = get_order(size);
	ivector_table = (struct ino_bucket *)
		__get_free_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!ivector_table) {
		prom_printf("Fatal error, cannot allocate ivector_table\n");
		prom_halt();
	}
	__flush_dcache_range((unsigned long) ivector_table,
			     ((unsigned long) ivector_table) + size);

	ivector_table_pa = __pa(ivector_table);
}

/* Only invoked on boot processor.*/
void __init init_IRQ(void)
{
	irq_init_hv();
	irq_ivector_init();
	map_prom_timers();
	kill_prom_timer();

	if (tlb_type == hypervisor)
		sun4v_init_mondo_queues();

	init_send_mondo_info();

	if (tlb_type == hypervisor) {
		/* Load up the boot cpu's entries.  */
		sun4v_register_mondo_queues(hard_smp_processor_id());
	}

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

	irq_to_desc(0)->action = &timer_irq_action;
}
