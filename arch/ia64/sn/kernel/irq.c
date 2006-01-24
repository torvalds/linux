/*
 * Platform dependent support for SGI SN
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/irq.h>
#include <linux/spinlock.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/intr.h>
#include <asm/sn/pcibr_provider.h>
#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/pcidev.h>
#include <asm/sn/shub_mmr.h>
#include <asm/sn/sn_sal.h>

static void force_interrupt(int irq);
static void register_intr_pda(struct sn_irq_info *sn_irq_info);
static void unregister_intr_pda(struct sn_irq_info *sn_irq_info);

int sn_force_interrupt_flag = 1;
extern int sn_ioif_inited;
static struct list_head **sn_irq_lh;
static spinlock_t sn_irq_info_lock = SPIN_LOCK_UNLOCKED; /* non-IRQ lock */

static inline u64 sn_intr_alloc(nasid_t local_nasid, int local_widget,
				     u64 sn_irq_info,
				     int req_irq, nasid_t req_nasid,
				     int req_slice)
{
	struct ia64_sal_retval ret_stuff;
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	SAL_CALL_NOLOCK(ret_stuff, (u64) SN_SAL_IOIF_INTERRUPT,
			(u64) SAL_INTR_ALLOC, (u64) local_nasid,
			(u64) local_widget, (u64) sn_irq_info, (u64) req_irq,
			(u64) req_nasid, (u64) req_slice);
	return ret_stuff.status;
}

static inline void sn_intr_free(nasid_t local_nasid, int local_widget,
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

static unsigned int sn_startup_irq(unsigned int irq)
{
	return 0;
}

static void sn_shutdown_irq(unsigned int irq)
{
}

static void sn_disable_irq(unsigned int irq)
{
}

static void sn_enable_irq(unsigned int irq)
{
}

static void sn_ack_irq(unsigned int irq)
{
	u64 event_occurred, mask = 0;

	irq = irq & 0xff;
	event_occurred =
	    HUB_L((u64*)LOCAL_MMR_ADDR(SH_EVENT_OCCURRED));
	mask = event_occurred & SH_ALL_INT_MASK;
	HUB_S((u64*)LOCAL_MMR_ADDR(SH_EVENT_OCCURRED_ALIAS),
	      mask);
	__set_bit(irq, (volatile void *)pda->sn_in_service_ivecs);

	move_irq(irq);
}

static void sn_end_irq(unsigned int irq)
{
	int ivec;
	u64 event_occurred;

	ivec = irq & 0xff;
	if (ivec == SGI_UART_VECTOR) {
		event_occurred = HUB_L((u64*)LOCAL_MMR_ADDR (SH_EVENT_OCCURRED));
		/* If the UART bit is set here, we may have received an
		 * interrupt from the UART that the driver missed.  To
		 * make sure, we IPI ourselves to force us to look again.
		 */
		if (event_occurred & SH_EVENT_OCCURRED_UART_INT_MASK) {
			platform_send_ipi(smp_processor_id(), SGI_UART_VECTOR,
					  IA64_IPI_DM_INT, 0);
		}
	}
	__clear_bit(ivec, (volatile void *)pda->sn_in_service_ivecs);
	if (sn_force_interrupt_flag)
		force_interrupt(irq);
}

static void sn_irq_info_free(struct rcu_head *head);

static void sn_set_affinity_irq(unsigned int irq, cpumask_t mask)
{
	struct sn_irq_info *sn_irq_info, *sn_irq_info_safe;
	int cpuid, cpuphys;

	cpuid = first_cpu(mask);
	cpuphys = cpu_physical_id(cpuid);

	list_for_each_entry_safe(sn_irq_info, sn_irq_info_safe,
				 sn_irq_lh[irq], list) {
		u64 bridge;
		int local_widget, status;
		nasid_t local_nasid;
		struct sn_irq_info *new_irq_info;
		struct sn_pcibus_provider *pci_provider;

		new_irq_info = kmalloc(sizeof(struct sn_irq_info), GFP_ATOMIC);
		if (new_irq_info == NULL)
			break;
		memcpy(new_irq_info, sn_irq_info, sizeof(struct sn_irq_info));

		bridge = (u64) new_irq_info->irq_bridge;
		if (!bridge) {
			kfree(new_irq_info);
			break; /* irq is not a device interrupt */
		}

		local_nasid = NASID_GET(bridge);

		if (local_nasid & 1)
			local_widget = TIO_SWIN_WIDGETNUM(bridge);
		else
			local_widget = SWIN_WIDGETNUM(bridge);

		/* Free the old PROM new_irq_info structure */
		sn_intr_free(local_nasid, local_widget, new_irq_info);
		/* Update kernels new_irq_info with new target info */
		unregister_intr_pda(new_irq_info);

		/* allocate a new PROM new_irq_info struct */
		status = sn_intr_alloc(local_nasid, local_widget,
				       __pa(new_irq_info), irq,
				       cpuid_to_nasid(cpuid),
				       cpuid_to_slice(cpuid));

		/* SAL call failed */
		if (status) {
			kfree(new_irq_info);
			break;
		}

		new_irq_info->irq_cpuid = cpuid;
		register_intr_pda(new_irq_info);

		pci_provider = sn_pci_provider[new_irq_info->irq_bridge_type];
		if (pci_provider && pci_provider->target_interrupt)
			(pci_provider->target_interrupt)(new_irq_info);

		spin_lock(&sn_irq_info_lock);
		list_replace_rcu(&sn_irq_info->list, &new_irq_info->list);
		spin_unlock(&sn_irq_info_lock);
		call_rcu(&sn_irq_info->rcu, sn_irq_info_free);

#ifdef CONFIG_SMP
		set_irq_affinity_info((irq & 0xff), cpuphys, 0);
#endif
	}
}

struct hw_interrupt_type irq_type_sn = {
	.typename	= "SN hub",
	.startup	= sn_startup_irq,
	.shutdown	= sn_shutdown_irq,
	.enable		= sn_enable_irq,
	.disable	= sn_disable_irq,
	.ack		= sn_ack_irq,
	.end		= sn_end_irq,
	.set_affinity	= sn_set_affinity_irq
};

unsigned int sn_local_vector_to_irq(u8 vector)
{
	return (CPU_VECTOR_TO_IRQ(smp_processor_id(), vector));
}

void sn_irq_init(void)
{
	int i;
	irq_desc_t *base_desc = irq_desc;

	for (i = 0; i < NR_IRQS; i++) {
		if (base_desc[i].handler == &no_irq_type) {
			base_desc[i].handler = &irq_type_sn;
		}
	}
}

static void register_intr_pda(struct sn_irq_info *sn_irq_info)
{
	int irq = sn_irq_info->irq_irq;
	int cpu = sn_irq_info->irq_cpuid;

	if (pdacpu(cpu)->sn_last_irq < irq) {
		pdacpu(cpu)->sn_last_irq = irq;
	}

	if (pdacpu(cpu)->sn_first_irq == 0 || pdacpu(cpu)->sn_first_irq > irq) {
		pdacpu(cpu)->sn_first_irq = irq;
	}
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

static void sn_irq_info_free(struct rcu_head *head)
{
	struct sn_irq_info *sn_irq_info;

	sn_irq_info = container_of(head, struct sn_irq_info, rcu);
	kfree(sn_irq_info);
}

void sn_irq_fixup(struct pci_dev *pci_dev, struct sn_irq_info *sn_irq_info)
{
	nasid_t nasid = sn_irq_info->irq_nasid;
	int slice = sn_irq_info->irq_slice;
	int cpu = nasid_slice_to_cpuid(nasid, slice);

	pci_dev_get(pci_dev);
	sn_irq_info->irq_cpuid = cpu;
	sn_irq_info->irq_pciioinfo = SN_PCIDEV_INFO(pci_dev);

	/* link it into the sn_irq[irq] list */
	spin_lock(&sn_irq_info_lock);
	list_add_rcu(&sn_irq_info->list, sn_irq_lh[sn_irq_info->irq_irq]);
	spin_unlock(&sn_irq_info_lock);

	(void)register_intr_pda(sn_irq_info);
}

void sn_irq_unfixup(struct pci_dev *pci_dev)
{
	struct sn_irq_info *sn_irq_info;

	/* Only cleanup IRQ stuff if this device has a host bus context */
	if (!SN_PCIDEV_BUSSOFT(pci_dev))
		return;

	sn_irq_info = SN_PCIDEV_INFO(pci_dev)->pdi_sn_irq_info;
	if (!sn_irq_info || !sn_irq_info->irq_irq) {
		kfree(sn_irq_info);
		return;
	}

	unregister_intr_pda(sn_irq_info);
	spin_lock(&sn_irq_info_lock);
	list_del_rcu(&sn_irq_info->list);
	spin_unlock(&sn_irq_info_lock);
	call_rcu(&sn_irq_info->rcu, sn_irq_info_free);
	pci_dev_put(pci_dev);
}

static inline void
sn_call_force_intr_provider(struct sn_irq_info *sn_irq_info)
{
	struct sn_pcibus_provider *pci_provider;

	pci_provider = sn_pci_provider[sn_irq_info->irq_bridge_type];
	if (pci_provider && pci_provider->force_interrupt)
		(*pci_provider->force_interrupt)(sn_irq_info);
}

static void force_interrupt(int irq)
{
	struct sn_irq_info *sn_irq_info;

	if (!sn_ioif_inited)
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(sn_irq_info, sn_irq_lh[irq], list)
		sn_call_force_intr_provider(sn_irq_info);

	rcu_read_unlock();
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
	int irr_reg_num;
	int irr_bit;
	u64 irr_reg;
	struct pcidev_info *pcidev_info;
	struct pcibus_info *pcibus_info;

	/*
	 * Bridge types attached to TIO (anything but PIC) do not need this WAR
	 * since they do not target Shub II interrupt registers.  If that
	 * ever changes, this check needs to accomodate.
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

	irr_reg_num = irq_to_vector(irq) / 64;
	irr_bit = irq_to_vector(irq) % 64;
	switch (irr_reg_num) {
	case 0:
		irr_reg = ia64_getreg(_IA64_REG_CR_IRR0);
		break;
	case 1:
		irr_reg = ia64_getreg(_IA64_REG_CR_IRR1);
		break;
	case 2:
		irr_reg = ia64_getreg(_IA64_REG_CR_IRR2);
		break;
	case 3:
		irr_reg = ia64_getreg(_IA64_REG_CR_IRR3);
		break;
	}
	if (!test_bit(irr_bit, &irr_reg)) {
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

void sn_irq_lh_init(void)
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
