/*
 * Platform dependent support for SGI SN
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2004 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/irq.h>
#include <asm/sn/intr.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include "xtalk/xwidgetdev.h"
#include "pci/pcibus_provider_defs.h"
#include "pci/pcidev.h"
#include "pci/pcibr_provider.h"
#include <asm/sn/shub_mmr.h>
#include <asm/sn/sn_sal.h>

static void force_interrupt(int irq);
static void register_intr_pda(struct sn_irq_info *sn_irq_info);
static void unregister_intr_pda(struct sn_irq_info *sn_irq_info);

extern int sn_force_interrupt_flag;
extern int sn_ioif_inited;
struct sn_irq_info **sn_irq;

static inline uint64_t sn_intr_alloc(nasid_t local_nasid, int local_widget,
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
	uint64_t event_occurred, mask = 0;
	int nasid;

	irq = irq & 0xff;
	nasid = get_nasid();
	event_occurred =
	    HUB_L((uint64_t *) GLOBAL_MMR_ADDR(nasid, SH_EVENT_OCCURRED));
	if (event_occurred & SH_EVENT_OCCURRED_UART_INT_MASK) {
		mask |= (1 << SH_EVENT_OCCURRED_UART_INT_SHFT);
	}
	if (event_occurred & SH_EVENT_OCCURRED_IPI_INT_MASK) {
		mask |= (1 << SH_EVENT_OCCURRED_IPI_INT_SHFT);
	}
	if (event_occurred & SH_EVENT_OCCURRED_II_INT0_MASK) {
		mask |= (1 << SH_EVENT_OCCURRED_II_INT0_SHFT);
	}
	if (event_occurred & SH_EVENT_OCCURRED_II_INT1_MASK) {
		mask |= (1 << SH_EVENT_OCCURRED_II_INT1_SHFT);
	}
	HUB_S((uint64_t *) GLOBAL_MMR_ADDR(nasid, SH_EVENT_OCCURRED_ALIAS),
	      mask);
	__set_bit(irq, (volatile void *)pda->sn_in_service_ivecs);

	move_irq(irq);
}

static void sn_end_irq(unsigned int irq)
{
	int nasid;
	int ivec;
	uint64_t event_occurred;

	ivec = irq & 0xff;
	if (ivec == SGI_UART_VECTOR) {
		nasid = get_nasid();
		event_occurred = HUB_L((uint64_t *) GLOBAL_MMR_ADDR
				       (nasid, SH_EVENT_OCCURRED));
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

static void sn_set_affinity_irq(unsigned int irq, cpumask_t mask)
{
	struct sn_irq_info *sn_irq_info = sn_irq[irq];
	struct sn_irq_info *tmp_sn_irq_info;
	int cpuid, cpuphys;
	nasid_t t_nasid;	/* nasid to target */
	int t_slice;		/* slice to target */

	/* allocate a temp sn_irq_info struct to get new target info */
	tmp_sn_irq_info = kmalloc(sizeof(*tmp_sn_irq_info), GFP_KERNEL);
	if (!tmp_sn_irq_info)
		return;

	cpuid = first_cpu(mask);
	cpuphys = cpu_physical_id(cpuid);
	t_nasid = cpuid_to_nasid(cpuid);
	t_slice = cpuid_to_slice(cpuid);

	while (sn_irq_info) {
		int status;
		int local_widget;
		uint64_t bridge = (uint64_t) sn_irq_info->irq_bridge;
		nasid_t local_nasid = NASID_GET(bridge);

		if (!bridge)
			break;	/* irq is not a device interrupt */

		if (local_nasid & 1)
			local_widget = TIO_SWIN_WIDGETNUM(bridge);
		else
			local_widget = SWIN_WIDGETNUM(bridge);

		/* Free the old PROM sn_irq_info structure */
		sn_intr_free(local_nasid, local_widget, sn_irq_info);

		/* allocate a new PROM sn_irq_info struct */
		status = sn_intr_alloc(local_nasid, local_widget,
				       __pa(tmp_sn_irq_info), irq, t_nasid,
				       t_slice);

		if (status == 0) {
			/* Update kernels sn_irq_info with new target info */
			unregister_intr_pda(sn_irq_info);
			sn_irq_info->irq_cpuid = cpuid;
			sn_irq_info->irq_nasid = t_nasid;
			sn_irq_info->irq_slice = t_slice;
			sn_irq_info->irq_xtalkaddr =
			    tmp_sn_irq_info->irq_xtalkaddr;
			sn_irq_info->irq_cookie = tmp_sn_irq_info->irq_cookie;
			register_intr_pda(sn_irq_info);

			if (IS_PCI_BRIDGE_ASIC(sn_irq_info->irq_bridge_type)) {
				pcibr_change_devices_irq(sn_irq_info);
			}

			sn_irq_info = sn_irq_info->irq_next;

#ifdef CONFIG_SMP
			set_irq_affinity_info((irq & 0xff), cpuphys, 0);
#endif
		} else {
			break;	/* snp_affinity failed the intr_alloc */
		}
	}
	kfree(tmp_sn_irq_info);
}

struct hw_interrupt_type irq_type_sn = {
	"SN hub",
	sn_startup_irq,
	sn_shutdown_irq,
	sn_enable_irq,
	sn_disable_irq,
	sn_ack_irq,
	sn_end_irq,
	sn_set_affinity_irq
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

	if (pdacpu(cpu)->sn_last_irq == irq) {
		foundmatch = 0;
		for (i = pdacpu(cpu)->sn_last_irq - 1; i; i--) {
			tmp_irq_info = sn_irq[i];
			while (tmp_irq_info) {
				if (tmp_irq_info->irq_cpuid == cpu) {
					foundmatch++;
					break;
				}
				tmp_irq_info = tmp_irq_info->irq_next;
			}
			if (foundmatch) {
				break;
			}
		}
		pdacpu(cpu)->sn_last_irq = i;
	}

	if (pdacpu(cpu)->sn_first_irq == irq) {
		foundmatch = 0;
		for (i = pdacpu(cpu)->sn_first_irq + 1; i < NR_IRQS; i++) {
			tmp_irq_info = sn_irq[i];
			while (tmp_irq_info) {
				if (tmp_irq_info->irq_cpuid == cpu) {
					foundmatch++;
					break;
				}
				tmp_irq_info = tmp_irq_info->irq_next;
			}
			if (foundmatch) {
				break;
			}
		}
		pdacpu(cpu)->sn_first_irq = ((i == NR_IRQS) ? 0 : i);
	}
}

struct sn_irq_info *sn_irq_alloc(nasid_t local_nasid, int local_widget, int irq,
				 nasid_t nasid, int slice)
{
	struct sn_irq_info *sn_irq_info;
	int status;

	sn_irq_info = kmalloc(sizeof(*sn_irq_info), GFP_KERNEL);
	if (sn_irq_info == NULL)
		return NULL;

	memset(sn_irq_info, 0x0, sizeof(*sn_irq_info));

	status =
	    sn_intr_alloc(local_nasid, local_widget, __pa(sn_irq_info), irq,
			  nasid, slice);

	if (status) {
		kfree(sn_irq_info);
		return NULL;
	} else {
		return sn_irq_info;
	}
}

void sn_irq_free(struct sn_irq_info *sn_irq_info)
{
	uint64_t bridge = (uint64_t) sn_irq_info->irq_bridge;
	nasid_t local_nasid = NASID_GET(bridge);
	int local_widget;

	if (local_nasid & 1)	/* tio check */
		local_widget = TIO_SWIN_WIDGETNUM(bridge);
	else
		local_widget = SWIN_WIDGETNUM(bridge);

	sn_intr_free(local_nasid, local_widget, sn_irq_info);

	kfree(sn_irq_info);
}

void sn_irq_fixup(struct pci_dev *pci_dev, struct sn_irq_info *sn_irq_info)
{
	nasid_t nasid = sn_irq_info->irq_nasid;
	int slice = sn_irq_info->irq_slice;
	int cpu = nasid_slice_to_cpuid(nasid, slice);

	sn_irq_info->irq_cpuid = cpu;
	sn_irq_info->irq_pciioinfo = SN_PCIDEV_INFO(pci_dev);

	/* link it into the sn_irq[irq] list */
	sn_irq_info->irq_next = sn_irq[sn_irq_info->irq_irq];
	sn_irq[sn_irq_info->irq_irq] = sn_irq_info;

	(void)register_intr_pda(sn_irq_info);
}

static void force_interrupt(int irq)
{
	struct sn_irq_info *sn_irq_info;

	if (!sn_ioif_inited)
		return;
	sn_irq_info = sn_irq[irq];
	while (sn_irq_info) {
		if (IS_PCI_BRIDGE_ASIC(sn_irq_info->irq_bridge_type) &&
		    (sn_irq_info->irq_bridge != NULL)) {
			pcibr_force_interrupt(sn_irq_info);
		}
		sn_irq_info = sn_irq_info->irq_next;
	}
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
	uint64_t regval;
	int irr_reg_num;
	int irr_bit;
	uint64_t irr_reg;
	struct pcidev_info *pcidev_info;
	struct pcibus_info *pcibus_info;

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
		if (!test_bit(irq, pda->sn_soft_irr)) {
			if (!test_bit(irq, pda->sn_in_service_ivecs)) {
				regval &= 0xff;
				if (sn_irq_info->irq_int_bit & regval &
				    sn_irq_info->irq_last_intr) {
					regval &=
					    ~(sn_irq_info->
					      irq_int_bit & regval);
					pcibr_force_interrupt(sn_irq_info);
				}
			}
		}
	}
	sn_irq_info->irq_last_intr = regval;
}

void sn_lb_int_war_check(void)
{
	int i;

	if (!sn_ioif_inited || pda->sn_first_irq == 0)
		return;
	for (i = pda->sn_first_irq; i <= pda->sn_last_irq; i++) {
		struct sn_irq_info *sn_irq_info = sn_irq[i];
		while (sn_irq_info) {
			/* Only call for PCI bridges that are fully initialized. */
			if (IS_PCI_BRIDGE_ASIC(sn_irq_info->irq_bridge_type) &&
			    (sn_irq_info->irq_bridge != NULL)) {
				sn_check_intr(i, sn_irq_info);
			}
			sn_irq_info = sn_irq_info->irq_next;
		}
	}
}
