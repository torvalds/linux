/*
 * The file intends to implement the platform dependent EEH operations on
 * powernv platform. Actually, the powernv was created in order to fully
 * hypervisor support.
 *
 * Copyright Benjamin Herrenschmidt & Gavin Shan, IBM Corporation 2013.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <asm/eeh.h>
#include <asm/eeh_event.h>
#include <asm/firmware.h>
#include <asm/io.h>
#include <asm/iommu.h>
#include <asm/machdep.h>
#include <asm/msi_bitmap.h>
#include <asm/opal.h>
#include <asm/ppc-pci.h>

#include "powernv.h"
#include "pci.h"

static bool pnv_eeh_nb_init = false;
static int eeh_event_irq = -EINVAL;

static int pnv_eeh_init(void)
{
	struct pci_controller *hose;
	struct pnv_phb *phb;

	if (!firmware_has_feature(FW_FEATURE_OPAL)) {
		pr_warn("%s: OPAL is required !\n",
			__func__);
		return -EINVAL;
	}

	/* Set probe mode */
	eeh_add_flag(EEH_PROBE_MODE_DEV);

	/*
	 * P7IOC blocks PCI config access to frozen PE, but PHB3
	 * doesn't do that. So we have to selectively enable I/O
	 * prior to collecting error log.
	 */
	list_for_each_entry(hose, &hose_list, list_node) {
		phb = hose->private_data;

		if (phb->model == PNV_PHB_MODEL_P7IOC)
			eeh_add_flag(EEH_ENABLE_IO_FOR_LOG);

		/*
		 * PE#0 should be regarded as valid by EEH core
		 * if it's not the reserved one. Currently, we
		 * have the reserved PE#255 and PE#127 for PHB3
		 * and P7IOC separately. So we should regard
		 * PE#0 as valid for PHB3 and P7IOC.
		 */
		if (phb->ioda.reserved_pe != 0)
			eeh_add_flag(EEH_VALID_PE_ZERO);

		break;
	}

	return 0;
}

static irqreturn_t pnv_eeh_event(int irq, void *data)
{
	/*
	 * We simply send a special EEH event if EEH has been
	 * enabled. We don't care about EEH events until we've
	 * finished processing the outstanding ones. Event processing
	 * gets unmasked in next_error() if EEH is enabled.
	 */
	disable_irq_nosync(irq);

	if (eeh_enabled())
		eeh_send_failure_event(NULL);

	return IRQ_HANDLED;
}

#ifdef CONFIG_DEBUG_FS
static ssize_t pnv_eeh_ei_write(struct file *filp,
				const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct pci_controller *hose = filp->private_data;
	struct eeh_dev *edev;
	struct eeh_pe *pe;
	int pe_no, type, func;
	unsigned long addr, mask;
	char buf[50];
	int ret;

	if (!eeh_ops || !eeh_ops->err_inject)
		return -ENXIO;

	/* Copy over argument buffer */
	ret = simple_write_to_buffer(buf, sizeof(buf), ppos, user_buf, count);
	if (!ret)
		return -EFAULT;

	/* Retrieve parameters */
	ret = sscanf(buf, "%x:%x:%x:%lx:%lx",
		     &pe_no, &type, &func, &addr, &mask);
	if (ret != 5)
		return -EINVAL;

	/* Retrieve PE */
	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return -ENOMEM;
	edev->phb = hose;
	edev->pe_config_addr = pe_no;
	pe = eeh_pe_get(edev);
	kfree(edev);
	if (!pe)
		return -ENODEV;

	/* Do error injection */
	ret = eeh_ops->err_inject(pe, type, func, addr, mask);
	return ret < 0 ? ret : count;
}

static const struct file_operations pnv_eeh_ei_fops = {
	.open	= simple_open,
	.llseek	= no_llseek,
	.write	= pnv_eeh_ei_write,
};

static int pnv_eeh_dbgfs_set(void *data, int offset, u64 val)
{
	struct pci_controller *hose = data;
	struct pnv_phb *phb = hose->private_data;

	out_be64(phb->regs + offset, val);
	return 0;
}

static int pnv_eeh_dbgfs_get(void *data, int offset, u64 *val)
{
	struct pci_controller *hose = data;
	struct pnv_phb *phb = hose->private_data;

	*val = in_be64(phb->regs + offset);
	return 0;
}

static int pnv_eeh_outb_dbgfs_set(void *data, u64 val)
{
	return pnv_eeh_dbgfs_set(data, 0xD10, val);
}

static int pnv_eeh_outb_dbgfs_get(void *data, u64 *val)
{
	return pnv_eeh_dbgfs_get(data, 0xD10, val);
}

static int pnv_eeh_inbA_dbgfs_set(void *data, u64 val)
{
	return pnv_eeh_dbgfs_set(data, 0xD90, val);
}

static int pnv_eeh_inbA_dbgfs_get(void *data, u64 *val)
{
	return pnv_eeh_dbgfs_get(data, 0xD90, val);
}

static int pnv_eeh_inbB_dbgfs_set(void *data, u64 val)
{
	return pnv_eeh_dbgfs_set(data, 0xE10, val);
}

static int pnv_eeh_inbB_dbgfs_get(void *data, u64 *val)
{
	return pnv_eeh_dbgfs_get(data, 0xE10, val);
}

DEFINE_SIMPLE_ATTRIBUTE(pnv_eeh_outb_dbgfs_ops, pnv_eeh_outb_dbgfs_get,
			pnv_eeh_outb_dbgfs_set, "0x%llx\n");
DEFINE_SIMPLE_ATTRIBUTE(pnv_eeh_inbA_dbgfs_ops, pnv_eeh_inbA_dbgfs_get,
			pnv_eeh_inbA_dbgfs_set, "0x%llx\n");
DEFINE_SIMPLE_ATTRIBUTE(pnv_eeh_inbB_dbgfs_ops, pnv_eeh_inbB_dbgfs_get,
			pnv_eeh_inbB_dbgfs_set, "0x%llx\n");
#endif /* CONFIG_DEBUG_FS */

/**
 * pnv_eeh_post_init - EEH platform dependent post initialization
 *
 * EEH platform dependent post initialization on powernv. When
 * the function is called, the EEH PEs and devices should have
 * been built. If the I/O cache staff has been built, EEH is
 * ready to supply service.
 */
static int pnv_eeh_post_init(void)
{
	struct pci_controller *hose;
	struct pnv_phb *phb;
	int ret = 0;

	/* Register OPAL event notifier */
	if (!pnv_eeh_nb_init) {
		eeh_event_irq = opal_event_request(ilog2(OPAL_EVENT_PCI_ERROR));
		if (eeh_event_irq < 0) {
			pr_err("%s: Can't register OPAL event interrupt (%d)\n",
			       __func__, eeh_event_irq);
			return eeh_event_irq;
		}

		ret = request_irq(eeh_event_irq, pnv_eeh_event,
				IRQ_TYPE_LEVEL_HIGH, "opal-eeh", NULL);
		if (ret < 0) {
			irq_dispose_mapping(eeh_event_irq);
			pr_err("%s: Can't request OPAL event interrupt (%d)\n",
			       __func__, eeh_event_irq);
			return ret;
		}

		pnv_eeh_nb_init = true;
	}

	if (!eeh_enabled())
		disable_irq(eeh_event_irq);

	list_for_each_entry(hose, &hose_list, list_node) {
		phb = hose->private_data;

		/*
		 * If EEH is enabled, we're going to rely on that.
		 * Otherwise, we restore to conventional mechanism
		 * to clear frozen PE during PCI config access.
		 */
		if (eeh_enabled())
			phb->flags |= PNV_PHB_FLAG_EEH;
		else
			phb->flags &= ~PNV_PHB_FLAG_EEH;

		/* Create debugfs entries */
#ifdef CONFIG_DEBUG_FS
		if (phb->has_dbgfs || !phb->dbgfs)
			continue;

		phb->has_dbgfs = 1;
		debugfs_create_file("err_injct", 0200,
				    phb->dbgfs, hose,
				    &pnv_eeh_ei_fops);

		debugfs_create_file("err_injct_outbound", 0600,
				    phb->dbgfs, hose,
				    &pnv_eeh_outb_dbgfs_ops);
		debugfs_create_file("err_injct_inboundA", 0600,
				    phb->dbgfs, hose,
				    &pnv_eeh_inbA_dbgfs_ops);
		debugfs_create_file("err_injct_inboundB", 0600,
				    phb->dbgfs, hose,
				    &pnv_eeh_inbB_dbgfs_ops);
#endif /* CONFIG_DEBUG_FS */
	}

	return ret;
}

static int pnv_eeh_find_cap(struct pci_dn *pdn, int cap)
{
	int pos = PCI_CAPABILITY_LIST;
	int cnt = 48;   /* Maximal number of capabilities */
	u32 status, id;

	if (!pdn)
		return 0;

	/* Check if the device supports capabilities */
	pnv_pci_cfg_read(pdn, PCI_STATUS, 2, &status);
	if (!(status & PCI_STATUS_CAP_LIST))
		return 0;

	while (cnt--) {
		pnv_pci_cfg_read(pdn, pos, 1, &pos);
		if (pos < 0x40)
			break;

		pos &= ~3;
		pnv_pci_cfg_read(pdn, pos + PCI_CAP_LIST_ID, 1, &id);
		if (id == 0xff)
			break;

		/* Found */
		if (id == cap)
			return pos;

		/* Next one */
		pos += PCI_CAP_LIST_NEXT;
	}

	return 0;
}

static int pnv_eeh_find_ecap(struct pci_dn *pdn, int cap)
{
	struct eeh_dev *edev = pdn_to_eeh_dev(pdn);
	u32 header;
	int pos = 256, ttl = (4096 - 256) / 8;

	if (!edev || !edev->pcie_cap)
		return 0;
	if (pnv_pci_cfg_read(pdn, pos, 4, &header) != PCIBIOS_SUCCESSFUL)
		return 0;
	else if (!header)
		return 0;

	while (ttl-- > 0) {
		if (PCI_EXT_CAP_ID(header) == cap && pos)
			return pos;

		pos = PCI_EXT_CAP_NEXT(header);
		if (pos < 256)
			break;

		if (pnv_pci_cfg_read(pdn, pos, 4, &header) != PCIBIOS_SUCCESSFUL)
			break;
	}

	return 0;
}

/**
 * pnv_eeh_probe - Do probe on PCI device
 * @pdn: PCI device node
 * @data: unused
 *
 * When EEH module is installed during system boot, all PCI devices
 * are checked one by one to see if it supports EEH. The function
 * is introduced for the purpose. By default, EEH has been enabled
 * on all PCI devices. That's to say, we only need do necessary
 * initialization on the corresponding eeh device and create PE
 * accordingly.
 *
 * It's notable that's unsafe to retrieve the EEH device through
 * the corresponding PCI device. During the PCI device hotplug, which
 * was possiblly triggered by EEH core, the binding between EEH device
 * and the PCI device isn't built yet.
 */
static void *pnv_eeh_probe(struct pci_dn *pdn, void *data)
{
	struct pci_controller *hose = pdn->phb;
	struct pnv_phb *phb = hose->private_data;
	struct eeh_dev *edev = pdn_to_eeh_dev(pdn);
	uint32_t pcie_flags;
	int ret;

	/*
	 * When probing the root bridge, which doesn't have any
	 * subordinate PCI devices. We don't have OF node for
	 * the root bridge. So it's not reasonable to continue
	 * the probing.
	 */
	if (!edev || edev->pe)
		return NULL;

	/* Skip for PCI-ISA bridge */
	if ((pdn->class_code >> 8) == PCI_CLASS_BRIDGE_ISA)
		return NULL;

	/* Initialize eeh device */
	edev->class_code = pdn->class_code;
	edev->mode	&= 0xFFFFFF00;
	edev->pcix_cap = pnv_eeh_find_cap(pdn, PCI_CAP_ID_PCIX);
	edev->pcie_cap = pnv_eeh_find_cap(pdn, PCI_CAP_ID_EXP);
	edev->aer_cap  = pnv_eeh_find_ecap(pdn, PCI_EXT_CAP_ID_ERR);
	if ((edev->class_code >> 8) == PCI_CLASS_BRIDGE_PCI) {
		edev->mode |= EEH_DEV_BRIDGE;
		if (edev->pcie_cap) {
			pnv_pci_cfg_read(pdn, edev->pcie_cap + PCI_EXP_FLAGS,
					 2, &pcie_flags);
			pcie_flags = (pcie_flags & PCI_EXP_FLAGS_TYPE) >> 4;
			if (pcie_flags == PCI_EXP_TYPE_ROOT_PORT)
				edev->mode |= EEH_DEV_ROOT_PORT;
			else if (pcie_flags == PCI_EXP_TYPE_DOWNSTREAM)
				edev->mode |= EEH_DEV_DS_PORT;
		}
	}

	edev->config_addr    = (pdn->busno << 8) | (pdn->devfn);
	edev->pe_config_addr = phb->ioda.pe_rmap[edev->config_addr];

	/* Create PE */
	ret = eeh_add_to_parent_pe(edev);
	if (ret) {
		pr_warn("%s: Can't add PCI dev %04x:%02x:%02x.%01x to parent PE (%d)\n",
			__func__, hose->global_number, pdn->busno,
			PCI_SLOT(pdn->devfn), PCI_FUNC(pdn->devfn), ret);
		return NULL;
	}

	/*
	 * If the PE contains any one of following adapters, the
	 * PCI config space can't be accessed when dumping EEH log.
	 * Otherwise, we will run into fenced PHB caused by shortage
	 * of outbound credits in the adapter. The PCI config access
	 * should be blocked until PE reset. MMIO access is dropped
	 * by hardware certainly. In order to drop PCI config requests,
	 * one more flag (EEH_PE_CFG_RESTRICTED) is introduced, which
	 * will be checked in the backend for PE state retrival. If
	 * the PE becomes frozen for the first time and the flag has
	 * been set for the PE, we will set EEH_PE_CFG_BLOCKED for
	 * that PE to block its config space.
	 *
	 * Broadcom Austin 4-ports NICs (14e4:1657)
	 * Broadcom Shiner 4-ports 1G NICs (14e4:168a)
	 * Broadcom Shiner 2-ports 10G NICs (14e4:168e)
	 */
	if ((pdn->vendor_id == PCI_VENDOR_ID_BROADCOM &&
	     pdn->device_id == 0x1657) ||
	    (pdn->vendor_id == PCI_VENDOR_ID_BROADCOM &&
	     pdn->device_id == 0x168a) ||
	    (pdn->vendor_id == PCI_VENDOR_ID_BROADCOM &&
	     pdn->device_id == 0x168e))
		edev->pe->state |= EEH_PE_CFG_RESTRICTED;

	/*
	 * Cache the PE primary bus, which can't be fetched when
	 * full hotplug is in progress. In that case, all child
	 * PCI devices of the PE are expected to be removed prior
	 * to PE reset.
	 */
	if (!(edev->pe->state & EEH_PE_PRI_BUS)) {
		edev->pe->bus = pci_find_bus(hose->global_number,
					     pdn->busno);
		if (edev->pe->bus)
			edev->pe->state |= EEH_PE_PRI_BUS;
	}

	/*
	 * Enable EEH explicitly so that we will do EEH check
	 * while accessing I/O stuff
	 */
	eeh_add_flag(EEH_ENABLED);

	/* Save memory bars */
	eeh_save_bars(edev);

	return NULL;
}

/**
 * pnv_eeh_set_option - Initialize EEH or MMIO/DMA reenable
 * @pe: EEH PE
 * @option: operation to be issued
 *
 * The function is used to control the EEH functionality globally.
 * Currently, following options are support according to PAPR:
 * Enable EEH, Disable EEH, Enable MMIO and Enable DMA
 */
static int pnv_eeh_set_option(struct eeh_pe *pe, int option)
{
	struct pci_controller *hose = pe->phb;
	struct pnv_phb *phb = hose->private_data;
	bool freeze_pe = false;
	int opt;
	s64 rc;

	switch (option) {
	case EEH_OPT_DISABLE:
		return -EPERM;
	case EEH_OPT_ENABLE:
		return 0;
	case EEH_OPT_THAW_MMIO:
		opt = OPAL_EEH_ACTION_CLEAR_FREEZE_MMIO;
		break;
	case EEH_OPT_THAW_DMA:
		opt = OPAL_EEH_ACTION_CLEAR_FREEZE_DMA;
		break;
	case EEH_OPT_FREEZE_PE:
		freeze_pe = true;
		opt = OPAL_EEH_ACTION_SET_FREEZE_ALL;
		break;
	default:
		pr_warn("%s: Invalid option %d\n", __func__, option);
		return -EINVAL;
	}

	/* Freeze master and slave PEs if PHB supports compound PEs */
	if (freeze_pe) {
		if (phb->freeze_pe) {
			phb->freeze_pe(phb, pe->addr);
			return 0;
		}

		rc = opal_pci_eeh_freeze_set(phb->opal_id, pe->addr, opt);
		if (rc != OPAL_SUCCESS) {
			pr_warn("%s: Failure %lld freezing PHB#%x-PE#%x\n",
				__func__, rc, phb->hose->global_number,
				pe->addr);
			return -EIO;
		}

		return 0;
	}

	/* Unfreeze master and slave PEs if PHB supports */
	if (phb->unfreeze_pe)
		return phb->unfreeze_pe(phb, pe->addr, opt);

	rc = opal_pci_eeh_freeze_clear(phb->opal_id, pe->addr, opt);
	if (rc != OPAL_SUCCESS) {
		pr_warn("%s: Failure %lld enable %d for PHB#%x-PE#%x\n",
			__func__, rc, option, phb->hose->global_number,
			pe->addr);
		return -EIO;
	}

	return 0;
}

/**
 * pnv_eeh_get_pe_addr - Retrieve PE address
 * @pe: EEH PE
 *
 * Retrieve the PE address according to the given tranditional
 * PCI BDF (Bus/Device/Function) address.
 */
static int pnv_eeh_get_pe_addr(struct eeh_pe *pe)
{
	return pe->addr;
}

static void pnv_eeh_get_phb_diag(struct eeh_pe *pe)
{
	struct pnv_phb *phb = pe->phb->private_data;
	s64 rc;

	rc = opal_pci_get_phb_diag_data2(phb->opal_id, pe->data,
					 PNV_PCI_DIAG_BUF_SIZE);
	if (rc != OPAL_SUCCESS)
		pr_warn("%s: Failure %lld getting PHB#%x diag-data\n",
			__func__, rc, pe->phb->global_number);
}

static int pnv_eeh_get_phb_state(struct eeh_pe *pe)
{
	struct pnv_phb *phb = pe->phb->private_data;
	u8 fstate;
	__be16 pcierr;
	s64 rc;
	int result = 0;

	rc = opal_pci_eeh_freeze_status(phb->opal_id,
					pe->addr,
					&fstate,
					&pcierr,
					NULL);
	if (rc != OPAL_SUCCESS) {
		pr_warn("%s: Failure %lld getting PHB#%x state\n",
			__func__, rc, phb->hose->global_number);
		return EEH_STATE_NOT_SUPPORT;
	}

	/*
	 * Check PHB state. If the PHB is frozen for the
	 * first time, to dump the PHB diag-data.
	 */
	if (be16_to_cpu(pcierr) != OPAL_EEH_PHB_ERROR) {
		result = (EEH_STATE_MMIO_ACTIVE  |
			  EEH_STATE_DMA_ACTIVE   |
			  EEH_STATE_MMIO_ENABLED |
			  EEH_STATE_DMA_ENABLED);
	} else if (!(pe->state & EEH_PE_ISOLATED)) {
		eeh_pe_state_mark(pe, EEH_PE_ISOLATED);
		pnv_eeh_get_phb_diag(pe);

		if (eeh_has_flag(EEH_EARLY_DUMP_LOG))
			pnv_pci_dump_phb_diag_data(pe->phb, pe->data);
	}

	return result;
}

static int pnv_eeh_get_pe_state(struct eeh_pe *pe)
{
	struct pnv_phb *phb = pe->phb->private_data;
	u8 fstate;
	__be16 pcierr;
	s64 rc;
	int result;

	/*
	 * We don't clobber hardware frozen state until PE
	 * reset is completed. In order to keep EEH core
	 * moving forward, we have to return operational
	 * state during PE reset.
	 */
	if (pe->state & EEH_PE_RESET) {
		result = (EEH_STATE_MMIO_ACTIVE  |
			  EEH_STATE_DMA_ACTIVE   |
			  EEH_STATE_MMIO_ENABLED |
			  EEH_STATE_DMA_ENABLED);
		return result;
	}

	/*
	 * Fetch PE state from hardware. If the PHB
	 * supports compound PE, let it handle that.
	 */
	if (phb->get_pe_state) {
		fstate = phb->get_pe_state(phb, pe->addr);
	} else {
		rc = opal_pci_eeh_freeze_status(phb->opal_id,
						pe->addr,
						&fstate,
						&pcierr,
						NULL);
		if (rc != OPAL_SUCCESS) {
			pr_warn("%s: Failure %lld getting PHB#%x-PE%x state\n",
				__func__, rc, phb->hose->global_number,
				pe->addr);
			return EEH_STATE_NOT_SUPPORT;
		}
	}

	/* Figure out state */
	switch (fstate) {
	case OPAL_EEH_STOPPED_NOT_FROZEN:
		result = (EEH_STATE_MMIO_ACTIVE  |
			  EEH_STATE_DMA_ACTIVE   |
			  EEH_STATE_MMIO_ENABLED |
			  EEH_STATE_DMA_ENABLED);
		break;
	case OPAL_EEH_STOPPED_MMIO_FREEZE:
		result = (EEH_STATE_DMA_ACTIVE |
			  EEH_STATE_DMA_ENABLED);
		break;
	case OPAL_EEH_STOPPED_DMA_FREEZE:
		result = (EEH_STATE_MMIO_ACTIVE |
			  EEH_STATE_MMIO_ENABLED);
		break;
	case OPAL_EEH_STOPPED_MMIO_DMA_FREEZE:
		result = 0;
		break;
	case OPAL_EEH_STOPPED_RESET:
		result = EEH_STATE_RESET_ACTIVE;
		break;
	case OPAL_EEH_STOPPED_TEMP_UNAVAIL:
		result = EEH_STATE_UNAVAILABLE;
		break;
	case OPAL_EEH_STOPPED_PERM_UNAVAIL:
		result = EEH_STATE_NOT_SUPPORT;
		break;
	default:
		result = EEH_STATE_NOT_SUPPORT;
		pr_warn("%s: Invalid PHB#%x-PE#%x state %x\n",
			__func__, phb->hose->global_number,
			pe->addr, fstate);
	}

	/*
	 * If PHB supports compound PE, to freeze all
	 * slave PEs for consistency.
	 *
	 * If the PE is switching to frozen state for the
	 * first time, to dump the PHB diag-data.
	 */
	if (!(result & EEH_STATE_NOT_SUPPORT) &&
	    !(result & EEH_STATE_UNAVAILABLE) &&
	    !(result & EEH_STATE_MMIO_ACTIVE) &&
	    !(result & EEH_STATE_DMA_ACTIVE)  &&
	    !(pe->state & EEH_PE_ISOLATED)) {
		if (phb->freeze_pe)
			phb->freeze_pe(phb, pe->addr);

		eeh_pe_state_mark(pe, EEH_PE_ISOLATED);
		pnv_eeh_get_phb_diag(pe);

		if (eeh_has_flag(EEH_EARLY_DUMP_LOG))
			pnv_pci_dump_phb_diag_data(pe->phb, pe->data);
	}

	return result;
}

/**
 * pnv_eeh_get_state - Retrieve PE state
 * @pe: EEH PE
 * @delay: delay while PE state is temporarily unavailable
 *
 * Retrieve the state of the specified PE. For IODA-compitable
 * platform, it should be retrieved from IODA table. Therefore,
 * we prefer passing down to hardware implementation to handle
 * it.
 */
static int pnv_eeh_get_state(struct eeh_pe *pe, int *delay)
{
	int ret;

	if (pe->type & EEH_PE_PHB)
		ret = pnv_eeh_get_phb_state(pe);
	else
		ret = pnv_eeh_get_pe_state(pe);

	if (!delay)
		return ret;

	/*
	 * If the PE state is temporarily unavailable,
	 * to inform the EEH core delay for default
	 * period (1 second)
	 */
	*delay = 0;
	if (ret & EEH_STATE_UNAVAILABLE)
		*delay = 1000;

	return ret;
}

static s64 pnv_eeh_phb_poll(struct pnv_phb *phb)
{
	s64 rc = OPAL_HARDWARE;

	while (1) {
		rc = opal_pci_poll(phb->opal_id);
		if (rc <= 0)
			break;

		if (system_state < SYSTEM_RUNNING)
			udelay(1000 * rc);
		else
			msleep(rc);
	}

	return rc;
}

int pnv_eeh_phb_reset(struct pci_controller *hose, int option)
{
	struct pnv_phb *phb = hose->private_data;
	s64 rc = OPAL_HARDWARE;

	pr_debug("%s: Reset PHB#%x, option=%d\n",
		 __func__, hose->global_number, option);

	/* Issue PHB complete reset request */
	if (option == EEH_RESET_FUNDAMENTAL ||
	    option == EEH_RESET_HOT)
		rc = opal_pci_reset(phb->opal_id,
				    OPAL_RESET_PHB_COMPLETE,
				    OPAL_ASSERT_RESET);
	else if (option == EEH_RESET_DEACTIVATE)
		rc = opal_pci_reset(phb->opal_id,
				    OPAL_RESET_PHB_COMPLETE,
				    OPAL_DEASSERT_RESET);
	if (rc < 0)
		goto out;

	/*
	 * Poll state of the PHB until the request is done
	 * successfully. The PHB reset is usually PHB complete
	 * reset followed by hot reset on root bus. So we also
	 * need the PCI bus settlement delay.
	 */
	rc = pnv_eeh_phb_poll(phb);
	if (option == EEH_RESET_DEACTIVATE) {
		if (system_state < SYSTEM_RUNNING)
			udelay(1000 * EEH_PE_RST_SETTLE_TIME);
		else
			msleep(EEH_PE_RST_SETTLE_TIME);
	}
out:
	if (rc != OPAL_SUCCESS)
		return -EIO;

	return 0;
}

static int pnv_eeh_root_reset(struct pci_controller *hose, int option)
{
	struct pnv_phb *phb = hose->private_data;
	s64 rc = OPAL_HARDWARE;

	pr_debug("%s: Reset PHB#%x, option=%d\n",
		 __func__, hose->global_number, option);

	/*
	 * During the reset deassert time, we needn't care
	 * the reset scope because the firmware does nothing
	 * for fundamental or hot reset during deassert phase.
	 */
	if (option == EEH_RESET_FUNDAMENTAL)
		rc = opal_pci_reset(phb->opal_id,
				    OPAL_RESET_PCI_FUNDAMENTAL,
				    OPAL_ASSERT_RESET);
	else if (option == EEH_RESET_HOT)
		rc = opal_pci_reset(phb->opal_id,
				    OPAL_RESET_PCI_HOT,
				    OPAL_ASSERT_RESET);
	else if (option == EEH_RESET_DEACTIVATE)
		rc = opal_pci_reset(phb->opal_id,
				    OPAL_RESET_PCI_HOT,
				    OPAL_DEASSERT_RESET);
	if (rc < 0)
		goto out;

	/* Poll state of the PHB until the request is done */
	rc = pnv_eeh_phb_poll(phb);
	if (option == EEH_RESET_DEACTIVATE)
		msleep(EEH_PE_RST_SETTLE_TIME);
out:
	if (rc != OPAL_SUCCESS)
		return -EIO;

	return 0;
}

static int pnv_eeh_bridge_reset(struct pci_dev *dev, int option)
{
	struct pci_dn *pdn = pci_get_pdn_by_devfn(dev->bus, dev->devfn);
	struct eeh_dev *edev = pdn_to_eeh_dev(pdn);
	int aer = edev ? edev->aer_cap : 0;
	u32 ctrl;

	pr_debug("%s: Reset PCI bus %04x:%02x with option %d\n",
		 __func__, pci_domain_nr(dev->bus),
		 dev->bus->number, option);

	switch (option) {
	case EEH_RESET_FUNDAMENTAL:
	case EEH_RESET_HOT:
		/* Don't report linkDown event */
		if (aer) {
			eeh_ops->read_config(pdn, aer + PCI_ERR_UNCOR_MASK,
					     4, &ctrl);
			ctrl |= PCI_ERR_UNC_SURPDN;
			eeh_ops->write_config(pdn, aer + PCI_ERR_UNCOR_MASK,
					      4, ctrl);
		}

		eeh_ops->read_config(pdn, PCI_BRIDGE_CONTROL, 2, &ctrl);
		ctrl |= PCI_BRIDGE_CTL_BUS_RESET;
		eeh_ops->write_config(pdn, PCI_BRIDGE_CONTROL, 2, ctrl);

		msleep(EEH_PE_RST_HOLD_TIME);
		break;
	case EEH_RESET_DEACTIVATE:
		eeh_ops->read_config(pdn, PCI_BRIDGE_CONTROL, 2, &ctrl);
		ctrl &= ~PCI_BRIDGE_CTL_BUS_RESET;
		eeh_ops->write_config(pdn, PCI_BRIDGE_CONTROL, 2, ctrl);

		msleep(EEH_PE_RST_SETTLE_TIME);

		/* Continue reporting linkDown event */
		if (aer) {
			eeh_ops->read_config(pdn, aer + PCI_ERR_UNCOR_MASK,
					     4, &ctrl);
			ctrl &= ~PCI_ERR_UNC_SURPDN;
			eeh_ops->write_config(pdn, aer + PCI_ERR_UNCOR_MASK,
					      4, ctrl);
		}

		break;
	}

	return 0;
}

void pnv_pci_reset_secondary_bus(struct pci_dev *dev)
{
	struct pci_controller *hose;

	if (pci_is_root_bus(dev->bus)) {
		hose = pci_bus_to_host(dev->bus);
		pnv_eeh_root_reset(hose, EEH_RESET_HOT);
		pnv_eeh_root_reset(hose, EEH_RESET_DEACTIVATE);
	} else {
		pnv_eeh_bridge_reset(dev, EEH_RESET_HOT);
		pnv_eeh_bridge_reset(dev, EEH_RESET_DEACTIVATE);
	}
}

/**
 * pnv_eeh_reset - Reset the specified PE
 * @pe: EEH PE
 * @option: reset option
 *
 * Do reset on the indicated PE. For PCI bus sensitive PE,
 * we need to reset the parent p2p bridge. The PHB has to
 * be reinitialized if the p2p bridge is root bridge. For
 * PCI device sensitive PE, we will try to reset the device
 * through FLR. For now, we don't have OPAL APIs to do HARD
 * reset yet, so all reset would be SOFT (HOT) reset.
 */
static int pnv_eeh_reset(struct eeh_pe *pe, int option)
{
	struct pci_controller *hose = pe->phb;
	struct pci_bus *bus;
	int ret;

	/*
	 * For PHB reset, we always have complete reset. For those PEs whose
	 * primary bus derived from root complex (root bus) or root port
	 * (usually bus#1), we apply hot or fundamental reset on the root port.
	 * For other PEs, we always have hot reset on the PE primary bus.
	 *
	 * Here, we have different design to pHyp, which always clear the
	 * frozen state during PE reset. However, the good idea here from
	 * benh is to keep frozen state before we get PE reset done completely
	 * (until BAR restore). With the frozen state, HW drops illegal IO
	 * or MMIO access, which can incur recrusive frozen PE during PE
	 * reset. The side effect is that EEH core has to clear the frozen
	 * state explicitly after BAR restore.
	 */
	if (pe->type & EEH_PE_PHB) {
		ret = pnv_eeh_phb_reset(hose, option);
	} else {
		struct pnv_phb *phb;
		s64 rc;

		/*
		 * The frozen PE might be caused by PAPR error injection
		 * registers, which are expected to be cleared after hitting
		 * frozen PE as stated in the hardware spec. Unfortunately,
		 * that's not true on P7IOC. So we have to clear it manually
		 * to avoid recursive EEH errors during recovery.
		 */
		phb = hose->private_data;
		if (phb->model == PNV_PHB_MODEL_P7IOC &&
		    (option == EEH_RESET_HOT ||
		    option == EEH_RESET_FUNDAMENTAL)) {
			rc = opal_pci_reset(phb->opal_id,
					    OPAL_RESET_PHB_ERROR,
					    OPAL_ASSERT_RESET);
			if (rc != OPAL_SUCCESS) {
				pr_warn("%s: Failure %lld clearing "
					"error injection registers\n",
					__func__, rc);
				return -EIO;
			}
		}

		bus = eeh_pe_bus_get(pe);
		if (!bus) {
			pr_err("%s: Cannot find PCI bus for PHB#%d-PE#%x\n",
			       __func__, pe->phb->global_number, pe->addr);
			return -EIO;
		}
		if (pci_is_root_bus(bus) ||
			pci_is_root_bus(bus->parent))
			ret = pnv_eeh_root_reset(hose, option);
		else
			ret = pnv_eeh_bridge_reset(bus->self, option);
	}

	return ret;
}

/**
 * pnv_eeh_wait_state - Wait for PE state
 * @pe: EEH PE
 * @max_wait: maximal period in millisecond
 *
 * Wait for the state of associated PE. It might take some time
 * to retrieve the PE's state.
 */
static int pnv_eeh_wait_state(struct eeh_pe *pe, int max_wait)
{
	int ret;
	int mwait;

	while (1) {
		ret = pnv_eeh_get_state(pe, &mwait);

		/*
		 * If the PE's state is temporarily unavailable,
		 * we have to wait for the specified time. Otherwise,
		 * the PE's state will be returned immediately.
		 */
		if (ret != EEH_STATE_UNAVAILABLE)
			return ret;

		if (max_wait <= 0) {
			pr_warn("%s: Timeout getting PE#%x's state (%d)\n",
				__func__, pe->addr, max_wait);
			return EEH_STATE_NOT_SUPPORT;
		}

		max_wait -= mwait;
		msleep(mwait);
	}

	return EEH_STATE_NOT_SUPPORT;
}

/**
 * pnv_eeh_get_log - Retrieve error log
 * @pe: EEH PE
 * @severity: temporary or permanent error log
 * @drv_log: driver log to be combined with retrieved error log
 * @len: length of driver log
 *
 * Retrieve the temporary or permanent error from the PE.
 */
static int pnv_eeh_get_log(struct eeh_pe *pe, int severity,
			   char *drv_log, unsigned long len)
{
	if (!eeh_has_flag(EEH_EARLY_DUMP_LOG))
		pnv_pci_dump_phb_diag_data(pe->phb, pe->data);

	return 0;
}

/**
 * pnv_eeh_configure_bridge - Configure PCI bridges in the indicated PE
 * @pe: EEH PE
 *
 * The function will be called to reconfigure the bridges included
 * in the specified PE so that the mulfunctional PE would be recovered
 * again.
 */
static int pnv_eeh_configure_bridge(struct eeh_pe *pe)
{
	return 0;
}

/**
 * pnv_pe_err_inject - Inject specified error to the indicated PE
 * @pe: the indicated PE
 * @type: error type
 * @func: specific error type
 * @addr: address
 * @mask: address mask
 *
 * The routine is called to inject specified error, which is
 * determined by @type and @func, to the indicated PE for
 * testing purpose.
 */
static int pnv_eeh_err_inject(struct eeh_pe *pe, int type, int func,
			      unsigned long addr, unsigned long mask)
{
	struct pci_controller *hose = pe->phb;
	struct pnv_phb *phb = hose->private_data;
	s64 rc;

	if (type != OPAL_ERR_INJECT_TYPE_IOA_BUS_ERR &&
	    type != OPAL_ERR_INJECT_TYPE_IOA_BUS_ERR64) {
		pr_warn("%s: Invalid error type %d\n",
			__func__, type);
		return -ERANGE;
	}

	if (func < OPAL_ERR_INJECT_FUNC_IOA_LD_MEM_ADDR ||
	    func > OPAL_ERR_INJECT_FUNC_IOA_DMA_WR_TARGET) {
		pr_warn("%s: Invalid error function %d\n",
			__func__, func);
		return -ERANGE;
	}

	/* Firmware supports error injection ? */
	if (!opal_check_token(OPAL_PCI_ERR_INJECT)) {
		pr_warn("%s: Firmware doesn't support error injection\n",
			__func__);
		return -ENXIO;
	}

	/* Do error injection */
	rc = opal_pci_err_inject(phb->opal_id, pe->addr,
				 type, func, addr, mask);
	if (rc != OPAL_SUCCESS) {
		pr_warn("%s: Failure %lld injecting error "
			"%d-%d to PHB#%x-PE#%x\n",
			__func__, rc, type, func,
			hose->global_number, pe->addr);
		return -EIO;
	}

	return 0;
}

static inline bool pnv_eeh_cfg_blocked(struct pci_dn *pdn)
{
	struct eeh_dev *edev = pdn_to_eeh_dev(pdn);

	if (!edev || !edev->pe)
		return false;

	if (edev->pe->state & EEH_PE_CFG_BLOCKED)
		return true;

	return false;
}

static int pnv_eeh_read_config(struct pci_dn *pdn,
			       int where, int size, u32 *val)
{
	if (!pdn)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (pnv_eeh_cfg_blocked(pdn)) {
		*val = 0xFFFFFFFF;
		return PCIBIOS_SET_FAILED;
	}

	return pnv_pci_cfg_read(pdn, where, size, val);
}

static int pnv_eeh_write_config(struct pci_dn *pdn,
				int where, int size, u32 val)
{
	if (!pdn)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (pnv_eeh_cfg_blocked(pdn))
		return PCIBIOS_SET_FAILED;

	return pnv_pci_cfg_write(pdn, where, size, val);
}

static void pnv_eeh_dump_hub_diag_common(struct OpalIoP7IOCErrorData *data)
{
	/* GEM */
	if (data->gemXfir || data->gemRfir ||
	    data->gemRirqfir || data->gemMask || data->gemRwof)
		pr_info("  GEM: %016llx %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->gemXfir),
			be64_to_cpu(data->gemRfir),
			be64_to_cpu(data->gemRirqfir),
			be64_to_cpu(data->gemMask),
			be64_to_cpu(data->gemRwof));

	/* LEM */
	if (data->lemFir || data->lemErrMask ||
	    data->lemAction0 || data->lemAction1 || data->lemWof)
		pr_info("  LEM: %016llx %016llx %016llx %016llx %016llx\n",
			be64_to_cpu(data->lemFir),
			be64_to_cpu(data->lemErrMask),
			be64_to_cpu(data->lemAction0),
			be64_to_cpu(data->lemAction1),
			be64_to_cpu(data->lemWof));
}

static void pnv_eeh_get_and_dump_hub_diag(struct pci_controller *hose)
{
	struct pnv_phb *phb = hose->private_data;
	struct OpalIoP7IOCErrorData *data = &phb->diag.hub_diag;
	long rc;

	rc = opal_pci_get_hub_diag_data(phb->hub_id, data, sizeof(*data));
	if (rc != OPAL_SUCCESS) {
		pr_warn("%s: Failed to get HUB#%llx diag-data (%ld)\n",
			__func__, phb->hub_id, rc);
		return;
	}

	switch (be16_to_cpu(data->type)) {
	case OPAL_P7IOC_DIAG_TYPE_RGC:
		pr_info("P7IOC diag-data for RGC\n\n");
		pnv_eeh_dump_hub_diag_common(data);
		if (data->rgc.rgcStatus || data->rgc.rgcLdcp)
			pr_info("  RGC: %016llx %016llx\n",
				be64_to_cpu(data->rgc.rgcStatus),
				be64_to_cpu(data->rgc.rgcLdcp));
		break;
	case OPAL_P7IOC_DIAG_TYPE_BI:
		pr_info("P7IOC diag-data for BI %s\n\n",
			data->bi.biDownbound ? "Downbound" : "Upbound");
		pnv_eeh_dump_hub_diag_common(data);
		if (data->bi.biLdcp0 || data->bi.biLdcp1 ||
		    data->bi.biLdcp2 || data->bi.biFenceStatus)
			pr_info("  BI:  %016llx %016llx %016llx %016llx\n",
				be64_to_cpu(data->bi.biLdcp0),
				be64_to_cpu(data->bi.biLdcp1),
				be64_to_cpu(data->bi.biLdcp2),
				be64_to_cpu(data->bi.biFenceStatus));
		break;
	case OPAL_P7IOC_DIAG_TYPE_CI:
		pr_info("P7IOC diag-data for CI Port %d\n\n",
			data->ci.ciPort);
		pnv_eeh_dump_hub_diag_common(data);
		if (data->ci.ciPortStatus || data->ci.ciPortLdcp)
			pr_info("  CI:  %016llx %016llx\n",
				be64_to_cpu(data->ci.ciPortStatus),
				be64_to_cpu(data->ci.ciPortLdcp));
		break;
	case OPAL_P7IOC_DIAG_TYPE_MISC:
		pr_info("P7IOC diag-data for MISC\n\n");
		pnv_eeh_dump_hub_diag_common(data);
		break;
	case OPAL_P7IOC_DIAG_TYPE_I2C:
		pr_info("P7IOC diag-data for I2C\n\n");
		pnv_eeh_dump_hub_diag_common(data);
		break;
	default:
		pr_warn("%s: Invalid type of HUB#%llx diag-data (%d)\n",
			__func__, phb->hub_id, data->type);
	}
}

static int pnv_eeh_get_pe(struct pci_controller *hose,
			  u16 pe_no, struct eeh_pe **pe)
{
	struct pnv_phb *phb = hose->private_data;
	struct pnv_ioda_pe *pnv_pe;
	struct eeh_pe *dev_pe;
	struct eeh_dev edev;

	/*
	 * If PHB supports compound PE, to fetch
	 * the master PE because slave PE is invisible
	 * to EEH core.
	 */
	pnv_pe = &phb->ioda.pe_array[pe_no];
	if (pnv_pe->flags & PNV_IODA_PE_SLAVE) {
		pnv_pe = pnv_pe->master;
		WARN_ON(!pnv_pe ||
			!(pnv_pe->flags & PNV_IODA_PE_MASTER));
		pe_no = pnv_pe->pe_number;
	}

	/* Find the PE according to PE# */
	memset(&edev, 0, sizeof(struct eeh_dev));
	edev.phb = hose;
	edev.pe_config_addr = pe_no;
	dev_pe = eeh_pe_get(&edev);
	if (!dev_pe)
		return -EEXIST;

	/* Freeze the (compound) PE */
	*pe = dev_pe;
	if (!(dev_pe->state & EEH_PE_ISOLATED))
		phb->freeze_pe(phb, pe_no);

	/*
	 * At this point, we're sure the (compound) PE should
	 * have been frozen. However, we still need poke until
	 * hitting the frozen PE on top level.
	 */
	dev_pe = dev_pe->parent;
	while (dev_pe && !(dev_pe->type & EEH_PE_PHB)) {
		int ret;
		int active_flags = (EEH_STATE_MMIO_ACTIVE |
				    EEH_STATE_DMA_ACTIVE);

		ret = eeh_ops->get_state(dev_pe, NULL);
		if (ret <= 0 || (ret & active_flags) == active_flags) {
			dev_pe = dev_pe->parent;
			continue;
		}

		/* Frozen parent PE */
		*pe = dev_pe;
		if (!(dev_pe->state & EEH_PE_ISOLATED))
			phb->freeze_pe(phb, dev_pe->addr);

		/* Next one */
		dev_pe = dev_pe->parent;
	}

	return 0;
}

/**
 * pnv_eeh_next_error - Retrieve next EEH error to handle
 * @pe: Affected PE
 *
 * The function is expected to be called by EEH core while it gets
 * special EEH event (without binding PE). The function calls to
 * OPAL APIs for next error to handle. The informational error is
 * handled internally by platform. However, the dead IOC, dead PHB,
 * fenced PHB and frozen PE should be handled by EEH core eventually.
 */
static int pnv_eeh_next_error(struct eeh_pe **pe)
{
	struct pci_controller *hose;
	struct pnv_phb *phb;
	struct eeh_pe *phb_pe, *parent_pe;
	__be64 frozen_pe_no;
	__be16 err_type, severity;
	int active_flags = (EEH_STATE_MMIO_ACTIVE | EEH_STATE_DMA_ACTIVE);
	long rc;
	int state, ret = EEH_NEXT_ERR_NONE;

	/*
	 * While running here, it's safe to purge the event queue. The
	 * event should still be masked.
	 */
	eeh_remove_event(NULL, false);

	list_for_each_entry(hose, &hose_list, list_node) {
		/*
		 * If the subordinate PCI buses of the PHB has been
		 * removed or is exactly under error recovery, we
		 * needn't take care of it any more.
		 */
		phb = hose->private_data;
		phb_pe = eeh_phb_pe_get(hose);
		if (!phb_pe || (phb_pe->state & EEH_PE_ISOLATED))
			continue;

		rc = opal_pci_next_error(phb->opal_id,
					 &frozen_pe_no, &err_type, &severity);
		if (rc != OPAL_SUCCESS) {
			pr_devel("%s: Invalid return value on "
				 "PHB#%x (0x%lx) from opal_pci_next_error",
				 __func__, hose->global_number, rc);
			continue;
		}

		/* If the PHB doesn't have error, stop processing */
		if (be16_to_cpu(err_type) == OPAL_EEH_NO_ERROR ||
		    be16_to_cpu(severity) == OPAL_EEH_SEV_NO_ERROR) {
			pr_devel("%s: No error found on PHB#%x\n",
				 __func__, hose->global_number);
			continue;
		}

		/*
		 * Processing the error. We're expecting the error with
		 * highest priority reported upon multiple errors on the
		 * specific PHB.
		 */
		pr_devel("%s: Error (%d, %d, %llu) on PHB#%x\n",
			__func__, be16_to_cpu(err_type),
			be16_to_cpu(severity), be64_to_cpu(frozen_pe_no),
			hose->global_number);
		switch (be16_to_cpu(err_type)) {
		case OPAL_EEH_IOC_ERROR:
			if (be16_to_cpu(severity) == OPAL_EEH_SEV_IOC_DEAD) {
				pr_err("EEH: dead IOC detected\n");
				ret = EEH_NEXT_ERR_DEAD_IOC;
			} else if (be16_to_cpu(severity) == OPAL_EEH_SEV_INF) {
				pr_info("EEH: IOC informative error "
					"detected\n");
				pnv_eeh_get_and_dump_hub_diag(hose);
				ret = EEH_NEXT_ERR_NONE;
			}

			break;
		case OPAL_EEH_PHB_ERROR:
			if (be16_to_cpu(severity) == OPAL_EEH_SEV_PHB_DEAD) {
				*pe = phb_pe;
				pr_err("EEH: dead PHB#%x detected, "
				       "location: %s\n",
					hose->global_number,
					eeh_pe_loc_get(phb_pe));
				ret = EEH_NEXT_ERR_DEAD_PHB;
			} else if (be16_to_cpu(severity) ==
				   OPAL_EEH_SEV_PHB_FENCED) {
				*pe = phb_pe;
				pr_err("EEH: Fenced PHB#%x detected, "
				       "location: %s\n",
					hose->global_number,
					eeh_pe_loc_get(phb_pe));
				ret = EEH_NEXT_ERR_FENCED_PHB;
			} else if (be16_to_cpu(severity) == OPAL_EEH_SEV_INF) {
				pr_info("EEH: PHB#%x informative error "
					"detected, location: %s\n",
					hose->global_number,
					eeh_pe_loc_get(phb_pe));
				pnv_eeh_get_phb_diag(phb_pe);
				pnv_pci_dump_phb_diag_data(hose, phb_pe->data);
				ret = EEH_NEXT_ERR_NONE;
			}

			break;
		case OPAL_EEH_PE_ERROR:
			/*
			 * If we can't find the corresponding PE, we
			 * just try to unfreeze.
			 */
			if (pnv_eeh_get_pe(hose,
				be64_to_cpu(frozen_pe_no), pe)) {
				pr_info("EEH: Clear non-existing PHB#%x-PE#%llx\n",
					hose->global_number, be64_to_cpu(frozen_pe_no));
				pr_info("EEH: PHB location: %s\n",
					eeh_pe_loc_get(phb_pe));

				/* Dump PHB diag-data */
				rc = opal_pci_get_phb_diag_data2(phb->opal_id,
					phb->diag.blob, PNV_PCI_DIAG_BUF_SIZE);
				if (rc == OPAL_SUCCESS)
					pnv_pci_dump_phb_diag_data(hose,
							phb->diag.blob);

				/* Try best to clear it */
				opal_pci_eeh_freeze_clear(phb->opal_id,
					be64_to_cpu(frozen_pe_no),
					OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);
				ret = EEH_NEXT_ERR_NONE;
			} else if ((*pe)->state & EEH_PE_ISOLATED ||
				   eeh_pe_passed(*pe)) {
				ret = EEH_NEXT_ERR_NONE;
			} else {
				pr_err("EEH: Frozen PE#%x "
				       "on PHB#%x detected\n",
				       (*pe)->addr,
					(*pe)->phb->global_number);
				pr_err("EEH: PE location: %s, "
				       "PHB location: %s\n",
				       eeh_pe_loc_get(*pe),
				       eeh_pe_loc_get(phb_pe));
				ret = EEH_NEXT_ERR_FROZEN_PE;
			}

			break;
		default:
			pr_warn("%s: Unexpected error type %d\n",
				__func__, be16_to_cpu(err_type));
		}

		/*
		 * EEH core will try recover from fenced PHB or
		 * frozen PE. In the time for frozen PE, EEH core
		 * enable IO path for that before collecting logs,
		 * but it ruins the site. So we have to dump the
		 * log in advance here.
		 */
		if ((ret == EEH_NEXT_ERR_FROZEN_PE  ||
		    ret == EEH_NEXT_ERR_FENCED_PHB) &&
		    !((*pe)->state & EEH_PE_ISOLATED)) {
			eeh_pe_state_mark(*pe, EEH_PE_ISOLATED);
			pnv_eeh_get_phb_diag(*pe);

			if (eeh_has_flag(EEH_EARLY_DUMP_LOG))
				pnv_pci_dump_phb_diag_data((*pe)->phb,
							   (*pe)->data);
		}

		/*
		 * We probably have the frozen parent PE out there and
		 * we need have to handle frozen parent PE firstly.
		 */
		if (ret == EEH_NEXT_ERR_FROZEN_PE) {
			parent_pe = (*pe)->parent;
			while (parent_pe) {
				/* Hit the ceiling ? */
				if (parent_pe->type & EEH_PE_PHB)
					break;

				/* Frozen parent PE ? */
				state = eeh_ops->get_state(parent_pe, NULL);
				if (state > 0 &&
				    (state & active_flags) != active_flags)
					*pe = parent_pe;

				/* Next parent level */
				parent_pe = parent_pe->parent;
			}

			/* We possibly migrate to another PE */
			eeh_pe_state_mark(*pe, EEH_PE_ISOLATED);
		}

		/*
		 * If we have no errors on the specific PHB or only
		 * informative error there, we continue poking it.
		 * Otherwise, we need actions to be taken by upper
		 * layer.
		 */
		if (ret > EEH_NEXT_ERR_INF)
			break;
	}

	/* Unmask the event */
	if (ret == EEH_NEXT_ERR_NONE && eeh_enabled())
		enable_irq(eeh_event_irq);

	return ret;
}

static int pnv_eeh_restore_config(struct pci_dn *pdn)
{
	struct eeh_dev *edev = pdn_to_eeh_dev(pdn);
	struct pnv_phb *phb;
	s64 ret;

	if (!edev)
		return -EEXIST;

	phb = edev->phb->private_data;
	ret = opal_pci_reinit(phb->opal_id,
			      OPAL_REINIT_PCI_DEV, edev->config_addr);
	if (ret) {
		pr_warn("%s: Can't reinit PCI dev 0x%x (%lld)\n",
			__func__, edev->config_addr, ret);
		return -EIO;
	}

	return 0;
}

static struct eeh_ops pnv_eeh_ops = {
	.name                   = "powernv",
	.init                   = pnv_eeh_init,
	.post_init              = pnv_eeh_post_init,
	.probe			= pnv_eeh_probe,
	.set_option             = pnv_eeh_set_option,
	.get_pe_addr            = pnv_eeh_get_pe_addr,
	.get_state              = pnv_eeh_get_state,
	.reset                  = pnv_eeh_reset,
	.wait_state             = pnv_eeh_wait_state,
	.get_log                = pnv_eeh_get_log,
	.configure_bridge       = pnv_eeh_configure_bridge,
	.err_inject		= pnv_eeh_err_inject,
	.read_config            = pnv_eeh_read_config,
	.write_config           = pnv_eeh_write_config,
	.next_error		= pnv_eeh_next_error,
	.restore_config		= pnv_eeh_restore_config
};

/**
 * eeh_powernv_init - Register platform dependent EEH operations
 *
 * EEH initialization on powernv platform. This function should be
 * called before any EEH related functions.
 */
static int __init eeh_powernv_init(void)
{
	int ret = -EINVAL;

	eeh_set_pe_aux_size(PNV_PCI_DIAG_BUF_SIZE);
	ret = eeh_ops_register(&pnv_eeh_ops);
	if (!ret)
		pr_info("EEH: PowerNV platform initialized\n");
	else
		pr_info("EEH: Failed to initialize PowerNV platform (%d)\n", ret);

	return ret;
}
machine_early_initcall(powernv, eeh_powernv_init);
