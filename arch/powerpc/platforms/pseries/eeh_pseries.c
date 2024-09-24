// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The file intends to implement the platform dependent EEH operations on pseries.
 * Actually, the pseries platform is built based on RTAS heavily. That means the
 * pseries platform dependent EEH operations will be built on RTAS calls. The functions
 * are derived from arch/powerpc/platforms/pseries/eeh.c and necessary cleanup has
 * been done.
 *
 * Copyright Benjamin Herrenschmidt & Gavin Shan, IBM Corporation 2011.
 * Copyright IBM Corporation 2001, 2005, 2006
 * Copyright Dave Engebretsen & Todd Inglett 2001
 * Copyright Linas Vepstas 2005, 2006
 */

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/crash_dump.h>

#include <asm/eeh.h>
#include <asm/eeh_event.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ppc-pci.h>
#include <asm/rtas.h>

/* RTAS tokens */
static int ibm_set_eeh_option;
static int ibm_set_slot_reset;
static int ibm_read_slot_reset_state;
static int ibm_read_slot_reset_state2;
static int ibm_slot_error_detail;
static int ibm_get_config_addr_info;
static int ibm_get_config_addr_info2;
static int ibm_configure_pe;

static void pseries_eeh_init_edev(struct pci_dn *pdn);

static void pseries_pcibios_bus_add_device(struct pci_dev *pdev)
{
	struct pci_dn *pdn = pci_get_pdn(pdev);

	if (eeh_has_flag(EEH_FORCE_DISABLED))
		return;

	dev_dbg(&pdev->dev, "EEH: Setting up device\n");
#ifdef CONFIG_PCI_IOV
	if (pdev->is_virtfn) {
		pdn->device_id  =  pdev->device;
		pdn->vendor_id  =  pdev->vendor;
		pdn->class_code =  pdev->class;
		/*
		 * Last allow unfreeze return code used for retrieval
		 * by user space in eeh-sysfs to show the last command
		 * completion from platform.
		 */
		pdn->last_allow_rc =  0;
	}
#endif
	pseries_eeh_init_edev(pdn);
#ifdef CONFIG_PCI_IOV
	if (pdev->is_virtfn) {
		/*
		 * FIXME: This really should be handled by choosing the right
		 *        parent PE in pseries_eeh_init_edev().
		 */
		struct eeh_pe *physfn_pe = pci_dev_to_eeh_dev(pdev->physfn)->pe;
		struct eeh_dev *edev = pdn_to_eeh_dev(pdn);

		edev->pe_config_addr =  (pdn->busno << 16) | (pdn->devfn << 8);
		eeh_pe_tree_remove(edev); /* Remove as it is adding to bus pe */
		eeh_pe_tree_insert(edev, physfn_pe);   /* Add as VF PE type */
	}
#endif
	eeh_probe_device(pdev);
}


/**
 * pseries_eeh_get_pe_config_addr - Find the pe_config_addr for a device
 * @pdn: pci_dn of the input device
 *
 * The EEH RTAS calls use a tuple consisting of: (buid_hi, buid_lo,
 * pe_config_addr) as a handle to a given PE. This function finds the
 * pe_config_addr based on the device's config addr.
 *
 * Keep in mind that the pe_config_addr *might* be numerically identical to the
 * device's config addr, but the two are conceptually distinct.
 *
 * Returns the pe_config_addr, or a negative error code.
 */
static int pseries_eeh_get_pe_config_addr(struct pci_dn *pdn)
{
	int config_addr = rtas_config_addr(pdn->busno, pdn->devfn, 0);
	struct pci_controller *phb = pdn->phb;
	int ret, rets[3];

	if (ibm_get_config_addr_info2 != RTAS_UNKNOWN_SERVICE) {
		/*
		 * First of all, use function 1 to determine if this device is
		 * part of a PE or not. ret[0] being zero indicates it's not.
		 */
		ret = rtas_call(ibm_get_config_addr_info2, 4, 2, rets,
				config_addr, BUID_HI(phb->buid),
				BUID_LO(phb->buid), 1);
		if (ret || (rets[0] == 0))
			return -ENOENT;

		/* Retrieve the associated PE config address with function 0 */
		ret = rtas_call(ibm_get_config_addr_info2, 4, 2, rets,
				config_addr, BUID_HI(phb->buid),
				BUID_LO(phb->buid), 0);
		if (ret) {
			pr_warn("%s: Failed to get address for PHB#%x-PE#%x\n",
				__func__, phb->global_number, config_addr);
			return -ENXIO;
		}

		return rets[0];
	}

	if (ibm_get_config_addr_info != RTAS_UNKNOWN_SERVICE) {
		ret = rtas_call(ibm_get_config_addr_info, 4, 2, rets,
				config_addr, BUID_HI(phb->buid),
				BUID_LO(phb->buid), 0);
		if (ret) {
			pr_warn("%s: Failed to get address for PHB#%x-PE#%x\n",
				__func__, phb->global_number, config_addr);
			return -ENXIO;
		}

		return rets[0];
	}

	/*
	 * PAPR does describe a process for finding the pe_config_addr that was
	 * used before the ibm,get-config-addr-info calls were added. However,
	 * I haven't found *any* systems that don't have that RTAS call
	 * implemented. If you happen to find one that needs the old DT based
	 * process, patches are welcome!
	 */
	return -ENOENT;
}

/**
 * pseries_eeh_phb_reset - Reset the specified PHB
 * @phb: PCI controller
 * @config_addr: the associated config address
 * @option: reset option
 *
 * Reset the specified PHB/PE
 */
static int pseries_eeh_phb_reset(struct pci_controller *phb, int config_addr, int option)
{
	int ret;

	/* Reset PE through RTAS call */
	ret = rtas_call(ibm_set_slot_reset, 4, 1, NULL,
			config_addr, BUID_HI(phb->buid),
			BUID_LO(phb->buid), option);

	/* If fundamental-reset not supported, try hot-reset */
	if (option == EEH_RESET_FUNDAMENTAL && ret == -8) {
		option = EEH_RESET_HOT;
		ret = rtas_call(ibm_set_slot_reset, 4, 1, NULL,
				config_addr, BUID_HI(phb->buid),
				BUID_LO(phb->buid), option);
	}

	/* We need reset hold or settlement delay */
	if (option == EEH_RESET_FUNDAMENTAL || option == EEH_RESET_HOT)
		msleep(EEH_PE_RST_HOLD_TIME);
	else
		msleep(EEH_PE_RST_SETTLE_TIME);

	return ret;
}

/**
 * pseries_eeh_phb_configure_bridge - Configure PCI bridges in the indicated PE
 * @phb: PCI controller
 * @config_addr: the associated config address
 *
 * The function will be called to reconfigure the bridges included
 * in the specified PE so that the mulfunctional PE would be recovered
 * again.
 */
static int pseries_eeh_phb_configure_bridge(struct pci_controller *phb, int config_addr)
{
	int ret;
	/* Waiting 0.2s maximum before skipping configuration */
	int max_wait = 200;

	while (max_wait > 0) {
		ret = rtas_call(ibm_configure_pe, 3, 1, NULL,
				config_addr, BUID_HI(phb->buid),
				BUID_LO(phb->buid));

		if (!ret)
			return ret;
		if (ret < 0)
			break;

		/*
		 * If RTAS returns a delay value that's above 100ms, cut it
		 * down to 100ms in case firmware made a mistake.  For more
		 * on how these delay values work see rtas_busy_delay_time
		 */
		if (ret > RTAS_EXTENDED_DELAY_MIN+2 &&
		    ret <= RTAS_EXTENDED_DELAY_MAX)
			ret = RTAS_EXTENDED_DELAY_MIN+2;

		max_wait -= rtas_busy_delay_time(ret);

		if (max_wait < 0)
			break;

		rtas_busy_delay(ret);
	}

	pr_warn("%s: Unable to configure bridge PHB#%x-PE#%x (%d)\n",
		__func__, phb->global_number, config_addr, ret);
	/* PAPR defines -3 as "Parameter Error" for this function: */
	if (ret == -3)
		return -EINVAL;
	else
		return -EIO;
}

/*
 * Buffer for reporting slot-error-detail rtas calls. Its here
 * in BSS, and not dynamically alloced, so that it ends up in
 * RMO where RTAS can access it.
 */
static unsigned char slot_errbuf[RTAS_ERROR_LOG_MAX];
static DEFINE_SPINLOCK(slot_errbuf_lock);
static int eeh_error_buf_size;

static int pseries_eeh_cap_start(struct pci_dn *pdn)
{
	u32 status;

	if (!pdn)
		return 0;

	rtas_pci_dn_read_config(pdn, PCI_STATUS, 2, &status);
	if (!(status & PCI_STATUS_CAP_LIST))
		return 0;

	return PCI_CAPABILITY_LIST;
}


static int pseries_eeh_find_cap(struct pci_dn *pdn, int cap)
{
	int pos = pseries_eeh_cap_start(pdn);
	int cnt = 48;	/* Maximal number of capabilities */
	u32 id;

	if (!pos)
		return 0;

        while (cnt--) {
		rtas_pci_dn_read_config(pdn, pos, 1, &pos);
		if (pos < 0x40)
			break;
		pos &= ~3;
		rtas_pci_dn_read_config(pdn, pos + PCI_CAP_LIST_ID, 1, &id);
		if (id == 0xff)
			break;
		if (id == cap)
			return pos;
		pos += PCI_CAP_LIST_NEXT;
	}

	return 0;
}

static int pseries_eeh_find_ecap(struct pci_dn *pdn, int cap)
{
	struct eeh_dev *edev = pdn_to_eeh_dev(pdn);
	u32 header;
	int pos = 256;
	int ttl = (4096 - 256) / 8;

	if (!edev || !edev->pcie_cap)
		return 0;
	if (rtas_pci_dn_read_config(pdn, pos, 4, &header) != PCIBIOS_SUCCESSFUL)
		return 0;
	else if (!header)
		return 0;

	while (ttl-- > 0) {
		if (PCI_EXT_CAP_ID(header) == cap && pos)
			return pos;

		pos = PCI_EXT_CAP_NEXT(header);
		if (pos < 256)
			break;

		if (rtas_pci_dn_read_config(pdn, pos, 4, &header) != PCIBIOS_SUCCESSFUL)
			break;
	}

	return 0;
}

/**
 * pseries_eeh_pe_get_parent - Retrieve the parent PE
 * @edev: EEH device
 *
 * The whole PEs existing in the system are organized as hierarchy
 * tree. The function is used to retrieve the parent PE according
 * to the parent EEH device.
 */
static struct eeh_pe *pseries_eeh_pe_get_parent(struct eeh_dev *edev)
{
	struct eeh_dev *parent;
	struct pci_dn *pdn = eeh_dev_to_pdn(edev);

	/*
	 * It might have the case for the indirect parent
	 * EEH device already having associated PE, but
	 * the direct parent EEH device doesn't have yet.
	 */
	if (edev->physfn)
		pdn = pci_get_pdn(edev->physfn);
	else
		pdn = pdn ? pdn->parent : NULL;
	while (pdn) {
		/* We're poking out of PCI territory */
		parent = pdn_to_eeh_dev(pdn);
		if (!parent)
			return NULL;

		if (parent->pe)
			return parent->pe;

		pdn = pdn->parent;
	}

	return NULL;
}

/**
 * pseries_eeh_init_edev - initialise the eeh_dev and eeh_pe for a pci_dn
 *
 * @pdn: PCI device node
 *
 * When we discover a new PCI device via the device-tree we create a
 * corresponding pci_dn and we allocate, but don't initialise, an eeh_dev.
 * This function takes care of the initialisation and inserts the eeh_dev
 * into the correct eeh_pe. If no eeh_pe exists we'll allocate one.
 */
static void pseries_eeh_init_edev(struct pci_dn *pdn)
{
	struct eeh_pe pe, *parent;
	struct eeh_dev *edev;
	u32 pcie_flags;
	int ret;

	if (WARN_ON_ONCE(!eeh_has_flag(EEH_PROBE_MODE_DEVTREE)))
		return;

	/*
	 * Find the eeh_dev for this pdn. The storage for the eeh_dev was
	 * allocated at the same time as the pci_dn.
	 *
	 * XXX: We should probably re-visit that.
	 */
	edev = pdn_to_eeh_dev(pdn);
	if (!edev)
		return;

	/*
	 * If ->pe is set then we've already probed this device. We hit
	 * this path when a pci_dev is removed and rescanned while recovering
	 * a PE (i.e. for devices where the driver doesn't support error
	 * recovery).
	 */
	if (edev->pe)
		return;

	/* Check class/vendor/device IDs */
	if (!pdn->vendor_id || !pdn->device_id || !pdn->class_code)
		return;

	/* Skip for PCI-ISA bridge */
        if ((pdn->class_code >> 8) == PCI_CLASS_BRIDGE_ISA)
		return;

	eeh_edev_dbg(edev, "Probing device\n");

	/*
	 * Update class code and mode of eeh device. We need
	 * correctly reflects that current device is root port
	 * or PCIe switch downstream port.
	 */
	edev->pcix_cap = pseries_eeh_find_cap(pdn, PCI_CAP_ID_PCIX);
	edev->pcie_cap = pseries_eeh_find_cap(pdn, PCI_CAP_ID_EXP);
	edev->aer_cap = pseries_eeh_find_ecap(pdn, PCI_EXT_CAP_ID_ERR);
	edev->mode &= 0xFFFFFF00;
	if ((pdn->class_code >> 8) == PCI_CLASS_BRIDGE_PCI) {
		edev->mode |= EEH_DEV_BRIDGE;
		if (edev->pcie_cap) {
			rtas_pci_dn_read_config(pdn, edev->pcie_cap + PCI_EXP_FLAGS,
						2, &pcie_flags);
			pcie_flags = (pcie_flags & PCI_EXP_FLAGS_TYPE) >> 4;
			if (pcie_flags == PCI_EXP_TYPE_ROOT_PORT)
				edev->mode |= EEH_DEV_ROOT_PORT;
			else if (pcie_flags == PCI_EXP_TYPE_DOWNSTREAM)
				edev->mode |= EEH_DEV_DS_PORT;
		}
	}

	/* first up, find the pe_config_addr for the PE containing the device */
	ret = pseries_eeh_get_pe_config_addr(pdn);
	if (ret < 0) {
		eeh_edev_dbg(edev, "Unable to find pe_config_addr\n");
		goto err;
	}

	/* Try enable EEH on the fake PE */
	memset(&pe, 0, sizeof(struct eeh_pe));
	pe.phb = pdn->phb;
	pe.addr = ret;

	eeh_edev_dbg(edev, "Enabling EEH on device\n");
	ret = eeh_ops->set_option(&pe, EEH_OPT_ENABLE);
	if (ret) {
		eeh_edev_dbg(edev, "EEH failed to enable on device (code %d)\n", ret);
		goto err;
	}

	edev->pe_config_addr = pe.addr;

	eeh_add_flag(EEH_ENABLED);

	parent = pseries_eeh_pe_get_parent(edev);
	eeh_pe_tree_insert(edev, parent);
	eeh_save_bars(edev);
	eeh_edev_dbg(edev, "EEH enabled for device");

	return;

err:
	eeh_edev_dbg(edev, "EEH is unsupported on device (code = %d)\n", ret);
}

static struct eeh_dev *pseries_eeh_probe(struct pci_dev *pdev)
{
	struct eeh_dev *edev;
	struct pci_dn *pdn;

	pdn = pci_get_pdn_by_devfn(pdev->bus, pdev->devfn);
	if (!pdn)
		return NULL;

	/*
	 * If the system supports EEH on this device then the eeh_dev was
	 * configured and inserted into a PE in pseries_eeh_init_edev()
	 */
	edev = pdn_to_eeh_dev(pdn);
	if (!edev || !edev->pe)
		return NULL;

	return edev;
}

/**
 * pseries_eeh_init_edev_recursive - Enable EEH for the indicated device
 * @pdn: PCI device node
 *
 * This routine must be used to perform EEH initialization for the
 * indicated PCI device that was added after system boot (e.g.
 * hotplug, dlpar).
 */
void pseries_eeh_init_edev_recursive(struct pci_dn *pdn)
{
	struct pci_dn *n;

	if (!pdn)
		return;

	list_for_each_entry(n, &pdn->child_list, list)
		pseries_eeh_init_edev_recursive(n);

	pseries_eeh_init_edev(pdn);
}
EXPORT_SYMBOL_GPL(pseries_eeh_init_edev_recursive);

/**
 * pseries_eeh_set_option - Initialize EEH or MMIO/DMA reenable
 * @pe: EEH PE
 * @option: operation to be issued
 *
 * The function is used to control the EEH functionality globally.
 * Currently, following options are support according to PAPR:
 * Enable EEH, Disable EEH, Enable MMIO and Enable DMA
 */
static int pseries_eeh_set_option(struct eeh_pe *pe, int option)
{
	int ret = 0;

	/*
	 * When we're enabling or disabling EEH functionality on
	 * the particular PE, the PE config address is possibly
	 * unavailable. Therefore, we have to figure it out from
	 * the FDT node.
	 */
	switch (option) {
	case EEH_OPT_DISABLE:
	case EEH_OPT_ENABLE:
	case EEH_OPT_THAW_MMIO:
	case EEH_OPT_THAW_DMA:
		break;
	case EEH_OPT_FREEZE_PE:
		/* Not support */
		return 0;
	default:
		pr_err("%s: Invalid option %d\n", __func__, option);
		return -EINVAL;
	}

	ret = rtas_call(ibm_set_eeh_option, 4, 1, NULL,
			pe->addr, BUID_HI(pe->phb->buid),
			BUID_LO(pe->phb->buid), option);

	return ret;
}

/**
 * pseries_eeh_get_state - Retrieve PE state
 * @pe: EEH PE
 * @delay: suggested time to wait if state is unavailable
 *
 * Retrieve the state of the specified PE. On RTAS compliant
 * pseries platform, there already has one dedicated RTAS function
 * for the purpose. It's notable that the associated PE config address
 * might be ready when calling the function. Therefore, endeavour to
 * use the PE config address if possible. Further more, there're 2
 * RTAS calls for the purpose, we need to try the new one and back
 * to the old one if the new one couldn't work properly.
 */
static int pseries_eeh_get_state(struct eeh_pe *pe, int *delay)
{
	int ret;
	int rets[4];
	int result;

	if (ibm_read_slot_reset_state2 != RTAS_UNKNOWN_SERVICE) {
		ret = rtas_call(ibm_read_slot_reset_state2, 3, 4, rets,
				pe->addr, BUID_HI(pe->phb->buid),
				BUID_LO(pe->phb->buid));
	} else if (ibm_read_slot_reset_state != RTAS_UNKNOWN_SERVICE) {
		/* Fake PE unavailable info */
		rets[2] = 0;
		ret = rtas_call(ibm_read_slot_reset_state, 3, 3, rets,
				pe->addr, BUID_HI(pe->phb->buid),
				BUID_LO(pe->phb->buid));
	} else {
		return EEH_STATE_NOT_SUPPORT;
	}

	if (ret)
		return ret;

	/* Parse the result out */
	if (!rets[1])
		return EEH_STATE_NOT_SUPPORT;

	switch(rets[0]) {
	case 0:
		result = EEH_STATE_MMIO_ACTIVE |
			 EEH_STATE_DMA_ACTIVE;
		break;
	case 1:
		result = EEH_STATE_RESET_ACTIVE |
			 EEH_STATE_MMIO_ACTIVE  |
			 EEH_STATE_DMA_ACTIVE;
		break;
	case 2:
		result = 0;
		break;
	case 4:
		result = EEH_STATE_MMIO_ENABLED;
		break;
	case 5:
		if (rets[2]) {
			if (delay)
				*delay = rets[2];
			result = EEH_STATE_UNAVAILABLE;
		} else {
			result = EEH_STATE_NOT_SUPPORT;
		}
		break;
	default:
		result = EEH_STATE_NOT_SUPPORT;
	}

	return result;
}

/**
 * pseries_eeh_reset - Reset the specified PE
 * @pe: EEH PE
 * @option: reset option
 *
 * Reset the specified PE
 */
static int pseries_eeh_reset(struct eeh_pe *pe, int option)
{
	return pseries_eeh_phb_reset(pe->phb, pe->addr, option);
}

/**
 * pseries_eeh_get_log - Retrieve error log
 * @pe: EEH PE
 * @severity: temporary or permanent error log
 * @drv_log: driver log to be combined with retrieved error log
 * @len: length of driver log
 *
 * Retrieve the temporary or permanent error from the PE.
 * Actually, the error will be retrieved through the dedicated
 * RTAS call.
 */
static int pseries_eeh_get_log(struct eeh_pe *pe, int severity, char *drv_log, unsigned long len)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&slot_errbuf_lock, flags);
	memset(slot_errbuf, 0, eeh_error_buf_size);

	ret = rtas_call(ibm_slot_error_detail, 8, 1, NULL, pe->addr,
			BUID_HI(pe->phb->buid), BUID_LO(pe->phb->buid),
			virt_to_phys(drv_log), len,
			virt_to_phys(slot_errbuf), eeh_error_buf_size,
			severity);
	if (!ret)
		log_error(slot_errbuf, ERR_TYPE_RTAS_LOG, 0);
	spin_unlock_irqrestore(&slot_errbuf_lock, flags);

	return ret;
}

/**
 * pseries_eeh_configure_bridge - Configure PCI bridges in the indicated PE
 * @pe: EEH PE
 *
 */
static int pseries_eeh_configure_bridge(struct eeh_pe *pe)
{
	return pseries_eeh_phb_configure_bridge(pe->phb, pe->addr);
}

/**
 * pseries_eeh_read_config - Read PCI config space
 * @edev: EEH device handle
 * @where: PCI config space offset
 * @size: size to read
 * @val: return value
 *
 * Read config space from the speicifed device
 */
static int pseries_eeh_read_config(struct eeh_dev *edev, int where, int size, u32 *val)
{
	struct pci_dn *pdn = eeh_dev_to_pdn(edev);

	return rtas_pci_dn_read_config(pdn, where, size, val);
}

/**
 * pseries_eeh_write_config - Write PCI config space
 * @edev: EEH device handle
 * @where: PCI config space offset
 * @size: size to write
 * @val: value to be written
 *
 * Write config space to the specified device
 */
static int pseries_eeh_write_config(struct eeh_dev *edev, int where, int size, u32 val)
{
	struct pci_dn *pdn = eeh_dev_to_pdn(edev);

	return rtas_pci_dn_write_config(pdn, where, size, val);
}

#ifdef CONFIG_PCI_IOV
static int pseries_send_allow_unfreeze(struct pci_dn *pdn, u16 *vf_pe_array, int cur_vfs)
{
	int rc;
	int ibm_allow_unfreeze = rtas_function_token(RTAS_FN_IBM_OPEN_SRIOV_ALLOW_UNFREEZE);
	unsigned long buid, addr;

	addr = rtas_config_addr(pdn->busno, pdn->devfn, 0);
	buid = pdn->phb->buid;
	spin_lock(&rtas_data_buf_lock);
	memcpy(rtas_data_buf, vf_pe_array, RTAS_DATA_BUF_SIZE);
	rc = rtas_call(ibm_allow_unfreeze, 5, 1, NULL,
		       addr,
		       BUID_HI(buid),
		       BUID_LO(buid),
		       rtas_data_buf, cur_vfs * sizeof(u16));
	spin_unlock(&rtas_data_buf_lock);
	if (rc)
		pr_warn("%s: Failed to allow unfreeze for PHB#%x-PE#%lx, rc=%x\n",
			__func__,
			pdn->phb->global_number, addr, rc);
	return rc;
}

static int pseries_call_allow_unfreeze(struct eeh_dev *edev)
{
	int cur_vfs = 0, rc = 0, vf_index, bus, devfn, vf_pe_num;
	struct pci_dn *pdn, *tmp, *parent, *physfn_pdn;
	u16 *vf_pe_array;

	vf_pe_array = kzalloc(RTAS_DATA_BUF_SIZE, GFP_KERNEL);
	if (!vf_pe_array)
		return -ENOMEM;
	if (pci_num_vf(edev->physfn ? edev->physfn : edev->pdev)) {
		if (edev->pdev->is_physfn) {
			cur_vfs = pci_num_vf(edev->pdev);
			pdn = eeh_dev_to_pdn(edev);
			parent = pdn->parent;
			for (vf_index = 0; vf_index < cur_vfs; vf_index++)
				vf_pe_array[vf_index] =
					cpu_to_be16(pdn->pe_num_map[vf_index]);
			rc = pseries_send_allow_unfreeze(pdn, vf_pe_array,
							 cur_vfs);
			pdn->last_allow_rc = rc;
			for (vf_index = 0; vf_index < cur_vfs; vf_index++) {
				list_for_each_entry_safe(pdn, tmp,
							 &parent->child_list,
							 list) {
					bus = pci_iov_virtfn_bus(edev->pdev,
								 vf_index);
					devfn = pci_iov_virtfn_devfn(edev->pdev,
								     vf_index);
					if (pdn->busno != bus ||
					    pdn->devfn != devfn)
						continue;
					pdn->last_allow_rc = rc;
				}
			}
		} else {
			pdn = pci_get_pdn(edev->pdev);
			physfn_pdn = pci_get_pdn(edev->physfn);

			vf_pe_num = physfn_pdn->pe_num_map[edev->vf_index];
			vf_pe_array[0] = cpu_to_be16(vf_pe_num);
			rc = pseries_send_allow_unfreeze(physfn_pdn,
							 vf_pe_array, 1);
			pdn->last_allow_rc = rc;
		}
	}

	kfree(vf_pe_array);
	return rc;
}

static int pseries_notify_resume(struct eeh_dev *edev)
{
	if (!edev)
		return -EEXIST;

	if (rtas_function_token(RTAS_FN_IBM_OPEN_SRIOV_ALLOW_UNFREEZE) == RTAS_UNKNOWN_SERVICE)
		return -EINVAL;

	if (edev->pdev->is_physfn || edev->pdev->is_virtfn)
		return pseries_call_allow_unfreeze(edev);

	return 0;
}
#endif

/**
 * pseries_eeh_err_inject - Inject specified error to the indicated PE
 * @pe: the indicated PE
 * @type: error type
 * @func: specific error type
 * @addr: address
 * @mask: address mask
 * The routine is called to inject specified error, which is
 * determined by @type and @func, to the indicated PE
 */
static int pseries_eeh_err_inject(struct eeh_pe *pe, int type, int func,
				  unsigned long addr, unsigned long mask)
{
	struct	eeh_dev	*pdev;

	/* Check on PCI error type */
	if (type != EEH_ERR_TYPE_32 && type != EEH_ERR_TYPE_64)
		return -EINVAL;

	switch (func) {
	case EEH_ERR_FUNC_LD_MEM_ADDR:
	case EEH_ERR_FUNC_LD_MEM_DATA:
	case EEH_ERR_FUNC_ST_MEM_ADDR:
	case EEH_ERR_FUNC_ST_MEM_DATA:
		/* injects a MMIO error for all pdev's belonging to PE */
		pci_lock_rescan_remove();
		list_for_each_entry(pdev, &pe->edevs, entry)
			eeh_pe_inject_mmio_error(pdev->pdev);
		pci_unlock_rescan_remove();
		break;
	default:
		return -ERANGE;
	}

	return 0;
}

static struct eeh_ops pseries_eeh_ops = {
	.name			= "pseries",
	.probe			= pseries_eeh_probe,
	.set_option		= pseries_eeh_set_option,
	.get_state		= pseries_eeh_get_state,
	.reset			= pseries_eeh_reset,
	.get_log		= pseries_eeh_get_log,
	.configure_bridge       = pseries_eeh_configure_bridge,
	.err_inject		= pseries_eeh_err_inject,
	.read_config		= pseries_eeh_read_config,
	.write_config		= pseries_eeh_write_config,
	.next_error		= NULL,
	.restore_config		= NULL, /* NB: configure_bridge() does this */
#ifdef CONFIG_PCI_IOV
	.notify_resume		= pseries_notify_resume
#endif
};

/**
 * eeh_pseries_init - Register platform dependent EEH operations
 *
 * EEH initialization on pseries platform. This function should be
 * called before any EEH related functions.
 */
static int __init eeh_pseries_init(void)
{
	struct pci_controller *phb;
	struct pci_dn *pdn;
	int ret, config_addr;

	/* figure out EEH RTAS function call tokens */
	ibm_set_eeh_option		= rtas_function_token(RTAS_FN_IBM_SET_EEH_OPTION);
	ibm_set_slot_reset		= rtas_function_token(RTAS_FN_IBM_SET_SLOT_RESET);
	ibm_read_slot_reset_state2	= rtas_function_token(RTAS_FN_IBM_READ_SLOT_RESET_STATE2);
	ibm_read_slot_reset_state	= rtas_function_token(RTAS_FN_IBM_READ_SLOT_RESET_STATE);
	ibm_slot_error_detail		= rtas_function_token(RTAS_FN_IBM_SLOT_ERROR_DETAIL);
	ibm_get_config_addr_info2	= rtas_function_token(RTAS_FN_IBM_GET_CONFIG_ADDR_INFO2);
	ibm_get_config_addr_info	= rtas_function_token(RTAS_FN_IBM_GET_CONFIG_ADDR_INFO);
	ibm_configure_pe		= rtas_function_token(RTAS_FN_IBM_CONFIGURE_PE);

	/*
	 * ibm,configure-pe and ibm,configure-bridge have the same semantics,
	 * however ibm,configure-pe can be faster.  If we can't find
	 * ibm,configure-pe then fall back to using ibm,configure-bridge.
	 */
	if (ibm_configure_pe == RTAS_UNKNOWN_SERVICE)
		ibm_configure_pe	= rtas_function_token(RTAS_FN_IBM_CONFIGURE_BRIDGE);

	/*
	 * Necessary sanity check. We needn't check "get-config-addr-info"
	 * and its variant since the old firmware probably support address
	 * of domain/bus/slot/function for EEH RTAS operations.
	 */
	if (ibm_set_eeh_option == RTAS_UNKNOWN_SERVICE		||
	    ibm_set_slot_reset == RTAS_UNKNOWN_SERVICE		||
	    (ibm_read_slot_reset_state2 == RTAS_UNKNOWN_SERVICE &&
	     ibm_read_slot_reset_state == RTAS_UNKNOWN_SERVICE)	||
	    ibm_slot_error_detail == RTAS_UNKNOWN_SERVICE	||
	    ibm_configure_pe == RTAS_UNKNOWN_SERVICE) {
		pr_info("EEH functionality not supported\n");
		return -EINVAL;
	}

	/* Initialize error log size */
	eeh_error_buf_size = rtas_get_error_log_max();

	/* Set EEH probe mode */
	eeh_add_flag(EEH_PROBE_MODE_DEVTREE | EEH_ENABLE_IO_FOR_LOG);

	/* Set EEH machine dependent code */
	ppc_md.pcibios_bus_add_device = pseries_pcibios_bus_add_device;

	if (is_kdump_kernel() || reset_devices) {
		pr_info("Issue PHB reset ...\n");
		list_for_each_entry(phb, &hose_list, list_node) {
			// Skip if the slot is empty
			if (list_empty(&PCI_DN(phb->dn)->child_list))
				continue;

			pdn = list_first_entry(&PCI_DN(phb->dn)->child_list, struct pci_dn, list);
			config_addr = pseries_eeh_get_pe_config_addr(pdn);

			/* invalid PE config addr */
			if (config_addr < 0)
				continue;

			pseries_eeh_phb_reset(phb, config_addr, EEH_RESET_FUNDAMENTAL);
			pseries_eeh_phb_reset(phb, config_addr, EEH_RESET_DEACTIVATE);
			pseries_eeh_phb_configure_bridge(phb, config_addr);
		}
	}

	ret = eeh_init(&pseries_eeh_ops);
	if (!ret)
		pr_info("EEH: pSeries platform initialized\n");
	else
		pr_info("EEH: pSeries platform initialization failure (%d)\n",
			ret);
	return ret;
}
machine_arch_initcall(pseries, eeh_pseries_init);
