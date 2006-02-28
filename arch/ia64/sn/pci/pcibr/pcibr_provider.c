/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2004 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <asm/sn/addrs.h>
#include <asm/sn/geo.h>
#include <asm/sn/pcibr_provider.h>
#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/pcidev.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/sn2/sn_hwperf.h>
#include "xtalk/xwidgetdev.h"
#include "xtalk/hubdev.h"

int
sal_pcibr_slot_enable(struct pcibus_info *soft, int device, void *resp)
{
	struct ia64_sal_retval ret_stuff;
	u64 busnum;
	u64 segment;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	segment = soft->pbi_buscommon.bs_persist_segment;
	busnum = soft->pbi_buscommon.bs_persist_busnum;
	SAL_CALL_NOLOCK(ret_stuff, (u64) SN_SAL_IOIF_SLOT_ENABLE, segment,
			busnum, (u64) device, (u64) resp, 0, 0, 0);

	return (int)ret_stuff.v0;
}

int
sal_pcibr_slot_disable(struct pcibus_info *soft, int device, int action,
		       void *resp)
{
	struct ia64_sal_retval ret_stuff;
	u64 busnum;
	u64 segment;

	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	segment = soft->pbi_buscommon.bs_persist_segment;
	busnum = soft->pbi_buscommon.bs_persist_busnum;
	SAL_CALL_NOLOCK(ret_stuff, (u64) SN_SAL_IOIF_SLOT_DISABLE,
			segment, busnum, (u64) device, (u64) action,
			(u64) resp, 0, 0);

	return (int)ret_stuff.v0;
}

static int sal_pcibr_error_interrupt(struct pcibus_info *soft)
{
	struct ia64_sal_retval ret_stuff;
	u64 busnum;
	int segment;
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	segment = soft->pbi_buscommon.bs_persist_segment;
	busnum = soft->pbi_buscommon.bs_persist_busnum;
	SAL_CALL_NOLOCK(ret_stuff,
			(u64) SN_SAL_IOIF_ERROR_INTERRUPT,
			(u64) segment, (u64) busnum, 0, 0, 0, 0, 0);

	return (int)ret_stuff.v0;
}

/* 
 * PCI Bridge Error interrupt handler.  Gets invoked whenever a PCI 
 * bridge sends an error interrupt.
 */
static irqreturn_t
pcibr_error_intr_handler(int irq, void *arg, struct pt_regs *regs)
{
	struct pcibus_info *soft = (struct pcibus_info *)arg;

	if (sal_pcibr_error_interrupt(soft) < 0) {
		panic("pcibr_error_intr_handler(): Fatal Bridge Error");
	}
	return IRQ_HANDLED;
}

void *
pcibr_bus_fixup(struct pcibus_bussoft *prom_bussoft, struct pci_controller *controller)
{
	int nasid, cnode, j;
	cnodeid_t near_cnode;
	struct hubdev_info *hubdev_info;
	struct pcibus_info *soft;
	struct sn_flush_device_kernel *sn_flush_device_kernel;
	struct sn_flush_device_common *common;

	if (! IS_PCI_BRIDGE_ASIC(prom_bussoft->bs_asic_type)) {
		return NULL;
	}

	/*
	 * Allocate kernel bus soft and copy from prom.
	 */

	soft = kmalloc(sizeof(struct pcibus_info), GFP_KERNEL);
	if (!soft) {
		return NULL;
	}

	memcpy(soft, prom_bussoft, sizeof(struct pcibus_info));
	soft->pbi_buscommon.bs_base =
	    (((u64) soft->pbi_buscommon.
	      bs_base << 4) >> 4) | __IA64_UNCACHED_OFFSET;

	spin_lock_init(&soft->pbi_lock);

	/*
	 * register the bridge's error interrupt handler
	 */
	if (request_irq(SGI_PCIASIC_ERROR, (void *)pcibr_error_intr_handler,
			SA_SHIRQ, "PCIBR error", (void *)(soft))) {
		printk(KERN_WARNING
		       "pcibr cannot allocate interrupt for error handler\n");
	}

	/* 
	 * Update the Bridge with the "kernel" pagesize 
	 */
	if (PAGE_SIZE < 16384) {
		pcireg_control_bit_clr(soft, PCIBR_CTRL_PAGE_SIZE);
	} else {
		pcireg_control_bit_set(soft, PCIBR_CTRL_PAGE_SIZE);
	}

	nasid = NASID_GET(soft->pbi_buscommon.bs_base);
	cnode = nasid_to_cnodeid(nasid);
	hubdev_info = (struct hubdev_info *)(NODEPDA(cnode)->pdinfo);

	if (hubdev_info->hdi_flush_nasid_list.widget_p) {
		sn_flush_device_kernel = hubdev_info->hdi_flush_nasid_list.
		    widget_p[(int)soft->pbi_buscommon.bs_xid];
		if (sn_flush_device_kernel) {
			for (j = 0; j < DEV_PER_WIDGET;
			     j++, sn_flush_device_kernel++) {
				common = sn_flush_device_kernel->common;
				if (common->sfdl_slot == -1)
					continue;
				if ((common->sfdl_persistent_segment ==
				     soft->pbi_buscommon.bs_persist_segment) &&
				     (common->sfdl_persistent_busnum ==
				     soft->pbi_buscommon.bs_persist_busnum))
					common->sfdl_pcibus_info =
					    soft;
			}
		}
	}

	/* Setup the PMU ATE map */
	soft->pbi_int_ate_resource.lowest_free_index = 0;
	soft->pbi_int_ate_resource.ate =
	    kzalloc(soft->pbi_int_ate_size * sizeof(u64), GFP_KERNEL);

	if (!soft->pbi_int_ate_resource.ate) {
		kfree(soft);
		return NULL;
	}

	if (prom_bussoft->bs_asic_type == PCIIO_ASIC_TYPE_TIOCP) {
		/* TIO PCI Bridge: find nearest node with CPUs */
		int e = sn_hwperf_get_nearest_node(cnode, NULL, &near_cnode);

		if (e < 0) {
			near_cnode = (cnodeid_t)-1; /* use any node */
			printk(KERN_WARNING "pcibr_bus_fixup: failed to find "
				"near node with CPUs to TIO node %d, err=%d\n",
				cnode, e);
		}
		controller->node = near_cnode;
	}
	else
		controller->node = cnode;
	return soft;
}

void pcibr_force_interrupt(struct sn_irq_info *sn_irq_info)
{
	struct pcidev_info *pcidev_info;
	struct pcibus_info *pcibus_info;
	int bit = sn_irq_info->irq_int_bit;

	if (! sn_irq_info->irq_bridge)
		return;

	pcidev_info = (struct pcidev_info *)sn_irq_info->irq_pciioinfo;
	if (pcidev_info) {
		pcibus_info =
		    (struct pcibus_info *)pcidev_info->pdi_host_pcidev_info->
		    pdi_pcibus_info;
		pcireg_force_intr_set(pcibus_info, bit);
	}
}

void pcibr_target_interrupt(struct sn_irq_info *sn_irq_info)
{
	struct pcidev_info *pcidev_info;
	struct pcibus_info *pcibus_info;
	int bit = sn_irq_info->irq_int_bit;
	u64 xtalk_addr = sn_irq_info->irq_xtalkaddr;

	pcidev_info = (struct pcidev_info *)sn_irq_info->irq_pciioinfo;
	if (pcidev_info) {
		pcibus_info =
		    (struct pcibus_info *)pcidev_info->pdi_host_pcidev_info->
		    pdi_pcibus_info;

		/* Disable the device's IRQ   */
		pcireg_intr_enable_bit_clr(pcibus_info, (1 << bit));

		/* Change the device's IRQ    */
		pcireg_intr_addr_addr_set(pcibus_info, bit, xtalk_addr);

		/* Re-enable the device's IRQ */
		pcireg_intr_enable_bit_set(pcibus_info, (1 << bit));

		pcibr_force_interrupt(sn_irq_info);
	}
}

/*
 * Provider entries for PIC/CP
 */

struct sn_pcibus_provider pcibr_provider = {
	.dma_map = pcibr_dma_map,
	.dma_map_consistent = pcibr_dma_map_consistent,
	.dma_unmap = pcibr_dma_unmap,
	.bus_fixup = pcibr_bus_fixup,
	.force_interrupt = pcibr_force_interrupt,
	.target_interrupt = pcibr_target_interrupt
};

int
pcibr_init_provider(void)
{
	sn_pci_provider[PCIIO_ASIC_TYPE_PIC] = &pcibr_provider;
	sn_pci_provider[PCIIO_ASIC_TYPE_TIOCP] = &pcibr_provider;

	return 0;
}

EXPORT_SYMBOL_GPL(sal_pcibr_slot_enable);
EXPORT_SYMBOL_GPL(sal_pcibr_slot_disable);
