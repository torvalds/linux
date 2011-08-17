/*
 * Platform dependent support for SGI SN
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/rculist.h>
#include <linux/slab.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/intr.h>
#include <asm/sn/pcibr_provider.h>
#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/pcidev.h>
#include <asm/sn/shub_mmr.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/sn_feature_sets.h>

static void register_intr_pda(struct sn_irq_info *sn_irq_info);
static void unregister_intr_pda(struct sn_irq_info *sn_irq_info);

extern int sn_ioif_inited;
struct list_head **sn_irq_lh;
static DEFINE_SPINLOCK(sn_irq_info_lock); /* non-IRQ lock */

u64 sn_intr_alloc(nasid_t local_nasid, int local_widget,
				     struct sn_irq_info *sn_irq_info,
				     int req_irq, nasid_t req_nasid,
				     int req_slice)
{
	struct ia64_sal_retval ret_stuff;
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	SAL_CALL_NOLOCK(ret_stuff, (u64) SN_SAL_IOIF_INTERRUPT,
			(u64) SAL_INTR_ALLOC, (u64) local_nasid,
			(u64) local_widget, __pa(sn_irq_info), (u64) req_irq,
			(u64) req_nasid, (u64) req_slice);

	return ret_stuff.status;
}

void sn_intr_free(nasid_t local_nasid, int local_widget,
				struct sn_irq_info *sn_irq_info)
{
	struct ia64_sal_retval ret_stuff;
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	SAL_CALL_NOLOCK(ret_stuff, (u64) SN_SAL_IOIF_INTERRUPT,
			(u64) SAL_INTR_FREE, (u64) local_nasid,
			(u64) local_widget, (u64) sn_irq_info->irq_irq,
			(u64) sn_irq_info->irq_cookie, 0, 0);
}

u64 sn_intr_redirect(nasid_t local_nasid, int local_widget,
		      struct sn_irq_info *sn_irq_info,
		      nasid_t req_nasid, int req_slice)
{
	struct ia64_sal_retval ret_stuff;
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	SAL_CALL_NOLOCK(ret_stuff, (u64) SN_SAL_IOIF_INTERRUPT,
			(u64) SAL_INTR_REDIRECT, (u64) local_nasid,
			(u64) local_widget, __pa(sn_irq_info),
			(u64) req_nasid, (u64) req_slice, 0);

	return ret_stuff.status;
}

static unsigned int sn_startup_irq(struct irq_data *data)
{
	return 0;
}

static void sn_shutdown_irq(struct irq_data *data)
{
}

extern void ia64_mca_register_cpev(int);

static void sn_disable_irq(struct irq_data *data)
{
	if (data->irq == local_vector_to_irq(IA64_CPE_VECTOR))
		ia64_mca_register_cpev(0);
}

static void sn_enable_irq(struct irq_data *data)
{
	if (data->irq == local_vector_to_irq(IA64_CPE_VECTOR))
		ia64_mca_register_cpev(data->irq);
}

static void sn_ack_irq(struct irq_data *data)
{
	u64 event_occurred, mask;
	unsigned int irq = data->irq & 0xff;

	event_occurred = HUB_L((u64*)LOCAL_MMR_ADDR(SH_EVENT_OCCURRED));
	mask = event_occurred & SH_ALL_INT_MASK;
	HUB_S((u64*)LOCAL_MMR_ADDR(SH_EVENT_OCCURRED_ALIAS), mask);
	__set_bit(irq, (volatile void *)pda->sn_in_service_ivecs);

	irq_move_irq(data);
}

struct sn_irq_info *sn_retarget_vector(struct sn_irq_info *sn_irq_info,
				       nasid_t nasid, int slice)
{
	int vector;
	int cpuid;
#ifdef CONFIG_SMP
	int cpuphys;
#endif
	int64_t bridge;
	int local_widget, status;
	nasid_t local_nasid;
	struct sn_irq_info *new_irq_info;
	struct sn_pcibus_provider *pci_provider;

	bridge = (u64) sn_irq_info->irq_bridge;
	if (!bridge) {
		return NULL; /* irq is not a device interrupt */
	}

	local_nasid = NASID_GET(bridge);

	if (local_nasid & 1)
		local_widget = TIO_SWIN_WIDGETNUM(bridge);
	else
		local_widget = SWIN_WIDGETNUM(bridge);
	vector = sn_irq_info->irq_irq;

	/* Make use of SAL_INTR_REDIRECT if PROM supports it */
	status = sn_intr_redirect(local_nasid, local_widget, sn_irq_info, nasid, slice);
	if (!status) {
		new_irq_info = sn_irq_info;
		goto finish_up;
	}

	/*
	 * PROM does not support SAL_INTR_REDIRECT, or it failed.
	 * Revert to old method.
	 */
	new_irq_info = kmalloc(sizeof(struct sn_irq_info), GFP_ATOMIC);
	if (new_irq_info == NULL)
		return NULL;

	memcpy(new_irq_info, sn_irq_info, sizeof(struct sn_irq_info));

	/* Free the old PROM new_irq_info structure */
	sn_intr_free(local_nasid, local_widget, new_irq_info);
	unregister_intr_pda(new_irq_info);

	/* allocate a new PROM new_irq_info struct */
	status = sn_intr_alloc(local_nasid, local_widget,
			       new_irq_info, vector,
			       nasid, slice);

	/* SAL call failed */
	if (status) {
		kfree(new_irq_info);
		return NULL;
	}

	register_intr_pda(new_irq_info);
	spin_lock(&sn_irq_info_lock);
	list_replace_rcu(&sn_irq_info->list, &new_irq_info->list);
	spin_unlock(&sn_irq_info_lock);
	kfree_rcu(sn_irq_info, rcu);


finish_up:
	/* Update kernels new_irq_info with new target info */
	cpuid = nasid_slice_to_cpuid(new_irq_info->irq_nasid,
				     new_irq_info->irq_slice);
	new_irq_info->irq_cpuid = cpuid;

	pci_provider = sn_pci_provider[new_irq_info->irq_bridge_type];

	/*
	 * If this represents a line interrupt, target it.  If it's
	 * an msi (irq_int_bit < 0), it's already targeted.
	 */
	if (new_irq_info->irq_int_bit >= 0 &&
	    pci_provider && pci_provider->target_interrupt)
		(pci_provider->target_interrupt)(new_irq_info);

#ifdef CONFIG_SMP
	cpuphys = cpu_physical_id(cpuid);
	set_irq_affinity_info((vector & 0xff), cpuphys, 0);
#endif

	return new_irq_info;
}

static int sn_set_affinity_irq(struct irq_data *data,
			       const struct cpumask *mask, bool force)
{
	struct sn_irq_info *sn_irq_info, *sn_irq_info_safe;
	unsigned int irq = data->irq;
	nasid_t nasid;
	int slice;

	nasid = cpuid_to_nasid(cpumask_first(mask));
	slice = cpuid_to_slice(cpumask_first(mask));

	list_for_each_entry_safe(sn_irq_info, sn_irq_info_safe,
				 sn_irq_lh[irq], list)
		(void)sn_retarget_vector(sn_irq_info, nasid, slice);

	return 0;
}

#ifdef CONFIG_SMP
void sn_set_err_irq_affinity(unsigned int irq)
{
        /*
         * On systems which support CPU disabling (SHub2), all error interrupts
         * are targeted at the boot CPU.
         */
        if (is_shub2() && sn_prom_feature_available(PRF_CPU_DISABLE_SUPPORT))
                set_irq_affinity_info(irq, cpu_physical_id(0), 0);
}
#else
void sn_set_err_irq_affinity(unsigned int irq) { }
#endif

static void
sn_mask_irq(struct irq_data *data)
{
}

static void
sn_unmask_irq(struct irq_data *data)
{
}

struct irq_chip irq_type_sn = {
	.name			= "SN hub",
	.irq_startup		= sn_startup_irq,
	.irq_shutdown		= sn_shutdown_irq,
	.irq_enable		= sn_enable_irq,
	.irq_disable		= sn_disable_irq,
	.irq_ack		= sn_ack_irq,
	.irq_mask		= sn_mask_irq,
	.irq_unmask		= sn_unmask_irq,
	.irq_set_affinity	= sn_set_affinity_irq
};

ia64_vector sn_irq_to_vector(int irq)
{
	if (irq >= IA64_NUM_VECTORS)
		return 0;
	return (ia64_vector)irq;
}

unsigned int sn_local_vector_to_irq(u8 vector)
{
	return (CPU_VECTOR_TO_IRQ(smp_processor_id(), vector));
}

void sn_irq_init(void)
{
	int i;

	ia64_first_device_vector = IA64_SN2_FIRST_DEVICE_VECTOR;
	ia64_last_device_vector = IA64_SN2_LAST_DEVICE_VECTOR;

	for (i = 0; i < NR_IRQS; i++) {
		if (irq_get_chip(i) == &no_irq_chip)
			irq_set_chip(i, &irq_type_sn);
	}
}

static void register_intr_pda(struct sn_irq_info *sn_irq_info)
{
	int irq = sn_irq_info->irq_irq;
	int cpu = sn_irq_info->irq_cpuid;

	if (pdacpu(cpu)->sn_last_irq < irq) {
		pdacpu(cpu)->sn_last_irq = irq;
	}

	if (pdacpu(cpu)->sn_first_irq == 0 || pdacpu(cpu)->sn_first_irq > irq)
		pdacpu(cpu)->sn_first_irq = irq;
}

static void unregister_intr_pda(struct sn_irq_info *sn_irq_info)
{
	int irq = sn_irq_info->irq_irq;
	int cpu = sn_irq_info->irq_cpuid;
	struct sn_irq_info *tmp_irq_info;
	int i, foundmatch;

	rcu_read_lock();
	if (pdacpu(cpu)->sn_last_irq == irq) {
		foundmatch = 0;
		for (i = pdacpu(cpu)->sn_last_irq - 1;
		     i && !foundmatch; i--) {
			list_for_each_entry_rcu(tmp_irq_info,
						sn_irq_lh[i],
						list) {
				if (tmp_irq_info->irq_cpuid == cpu) {
					foundmatch = 1;
					break;
				}
			}
		}
		pdacpu(cpu)->sn_last_irq = i;
	}

	if (pdacpu(cpu)->sn_first_irq == irq) {
		foundmatch = 0;
		for (i = pdacpu(cpu)->sn_first_irq + 1;
		     i < NR_IRQS && !foundmatch; i++) {
			list_for_each_entry_rcu(tmp_irq_info,
						sn_irq_lh[i],
						list) {
				if (tmp_irq_info->irq_cpuid == cpu) {
					foundmatch = 1;
					break;
				}
			}
		}
		pdacpu(cpu)->sn_first_irq = ((i == NR_IRQS) ? 0 : i);
	}
	rcu_read_unlock();
}

void sn_irq_fixup(struct pci_dev *pci_dev, struct sn_irq_info *sn_irq_info)
{
	nasid_t nasid = sn_irq_info->irq_nasid;
	int slice = sn_irq_info->irq_slice;
	int cpu = nasid_slice_to_cpuid(nasid, slice);
#ifdef CONFIG_SMP
	int cpuphys;
#endif

	pci_dev_get(pci_dev);
	sn_irq_info->irq_cpuid = cpu;
	sn_irq_info->irq_pciioinfo = SN_PCIDEV_INFO(pci_dev);

	/* link it into the sn_irq[irq] list */
	spin_lock(&sn_irq_info_lock);
	list_add_rcu(&sn_irq_info->list, sn_irq_lh[sn_irq_info->irq_irq]);
	reserve_irq_vector(sn_irq_info->irq_irq);
	spin_unlock(&sn_irq_info_lock);

	register_intr_pda(sn_irq_info);
#ifdef CONFIG_SMP
	cpuphys = cpu_physical_id(cpu);
	set_irq_affinity_info(sn_irq_info->irq_irq, cpuphys, 0);
	/*
	 * Affinity was set by the PROM, prevent it from
	 * being reset by the request_irq() path.
	 */
	irqd_mark_affinity_was_set(irq_get_irq_data(sn_irq_info->irq_irq));
#endif
}

void sn_irq_unfixup(struct pci_dev *pci_dev)
{
	struct sn_irq_info *sn_irq_info;

	/* Only cleanup IRQ stuff if this device has a host bus context */
	if (!SN_PCIDEV_BUSSOFT(pci_dev))
		return;

	sn_irq_info = SN_PCIDEV_INFO(pci_dev)->pdi_sn_irq_info;
	if (!sn_irq_info)
		return;
	if (!sn_irq_info->irq_irq) {
		kfree(sn_irq_info);
		return;
	}

	unregister_intr_pda(sn_irq_info);
	spin_lock(&sn_irq_info_lock);
	list_del_rcu(&sn_irq_info->list);
	spin_unlock(&sn_irq_info_lock);
	if (list_empty(sn_irq_lh[sn_irq_info->irq_irq]))
		free_irq_vector(sn_irq_info->irq_irq);
	kfree_rcu(sn_irq_info, rcu);
	pci_dev_put(pci_dev);

}

static inline void
sn_call_force_intr_provider(struct sn_irq_info *sn_irq_info)
{
	struct sn_pcibus_provider *pci_provider;

	pci_provider = sn_pci_provider[sn_irq_info->irq_bridge_type];

	/* Don't force an interrupt if the irq has been disabled */
	if (!irqd_irq_disabled(irq_get_irq_data(sn_irq_info->irq_irq)) &&
	    pci_provider && pci_provider->force_interrupt)
		(*pci_provider->force_interrupt)(sn_irq_info);
}

/*
 * Check for lost interrupts.  If the PIC int_status reg. says that
 * an interrupt has been sent, but not handled, and the interrupt
 * is not pending in either the cpu irr regs or in the soft irr regs,
 * and the interrupt is not in service, then the interrupt may have
 * been lost.  Force an interrupt on that pin.  It is possible that
 * the interrupt is in flight, so we may generate a spurious interrupt,
 * but we should never miss a real lost interrupt.
 */
static void sn_check_intr(int irq, struct sn_irq_info *sn_irq_info)
{
	u64 regval;
	struct pcidev_info *pcidev_info;
	struct pcibus_info *pcibus_info;

	/*
	 * Bridge types attached to TIO (anything but PIC) do not need this WAR
	 * since they do not target Shub II interrupt registers.  If that
	 * ever changes, this check needs to accommodate.
	 */
	if (sn_irq_info->irq_bridge_type != PCIIO_ASIC_TYPE_PIC)
		return;

	pcidev_info = (struct pcidev_info *)sn_irq_info->irq_pciioinfo;
	if (!pcidev_info)
		return;

	pcibus_info =
	    (struct pcibus_info *)pcidev_info->pdi_host_pcidev_info->
	    pdi_pcibus_info;
	regval = pcireg_intr_status_get(pcibus_info);

	if (!ia64_get_irr(irq_to_vector(irq))) {
		if (!test_bit(irq, pda->sn_in_service_ivecs)) {
			regval &= 0xff;
			if (sn_irq_info->irq_int_bit & regval &
			    sn_irq_info->irq_last_intr) {
				regval &= ~(sn_irq_info->irq_int_bit & regval);
				sn_call_force_intr_provider(sn_irq_info);
			}
		}
	}
	sn_irq_info->irq_last_intr = regval;
}

void sn_lb_int_war_check(void)
{
	struct sn_irq_info *sn_irq_info;
	int i;

	if (!sn_ioif_inited || pda->sn_first_irq == 0)
		return;

	rcu_read_lock();
	for (i = pda->sn_first_irq; i <= pda->sn_last_irq; i++) {
		list_for_each_entry_rcu(sn_irq_info, sn_irq_lh[i], list) {
			sn_check_intr(i, sn_irq_info);
		}
	}
	rcu_read_unlock();
}

void __init sn_irq_lh_init(void)
{
	int i;

	sn_irq_lh = kmalloc(sizeof(struct list_head *) * NR_IRQS, GFP_KERNEL);
	if (!sn_irq_lh)
		panic("SN PCI INIT: Failed to allocate memory for PCI init\n");

	for (i = 0; i < NR_IRQS; i++) {
		sn_irq_lh[i] = kmalloc(sizeof(struct list_head), GFP_KERNEL);
		if (!sn_irq_lh[i])
			panic("SN PCI INIT: Failed IRQ memory allocation\n");

		INIT_LIST_HEAD(sn_irq_lh[i]);
	}
}
