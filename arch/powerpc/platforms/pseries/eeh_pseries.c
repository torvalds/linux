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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
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

#ifdef CONFIG_PCI_IOV
void pseries_pcibios_bus_add_device(struct pci_dev *pdev)
{
	struct pci_dn *pdn = pci_get_pdn(pdev);
	struct pci_dn *physfn_pdn;
	struct eeh_dev *edev;

	if (!pdev->is_virtfn)
		return;

	pdn->device_id  =  pdev->device;
	pdn->vendor_id  =  pdev->vendor;
	pdn->class_code =  pdev->class;
	/*
	 * Last allow unfreeze return code used for retrieval
	 * by user space in eeh-sysfs to show the last command
	 * completion from platform.
	 */
	pdn->last_allow_rc =  0;
	physfn_pdn      =  pci_get_pdn(pdev->physfn);
	pdn->pe_number  =  physfn_pdn->pe_num_map[pdn->vf_index];
	edev = pdn_to_eeh_dev(pdn);

	/*
	 * The following operations will fail if VF's sysfs files
	 * aren't created or its resources aren't finalized.
	 */
	eeh_add_device_early(pdn);
	eeh_add_device_late(pdev);
	edev->pe_config_addr =  (pdn->busno << 16) | (pdn->devfn << 8);
	eeh_rmv_from_parent_pe(edev); /* Remove as it is adding to bus pe */
	eeh_add_to_parent_pe(edev);   /* Add as VF PE type */
	eeh_sysfs_add_device(pdev);

}
#endif

/*
 * Buffer for reporting slot-error-detail rtas calls. Its here
 * in BSS, and not dynamically alloced, so that it ends up in
 * RMO where RTAS can access it.
 */
static unsigned char slot_errbuf[RTAS_ERROR_LOG_MAX];
static DEFINE_SPINLOCK(slot_errbuf_lock);
static int eeh_error_buf_size;

/**
 * pseries_eeh_init - EEH platform dependent initialization
 *
 * EEH platform dependent initialization on pseries.
 */
static int pseries_eeh_init(void)
{
	/* figure out EEH RTAS function call tokens */
	ibm_set_eeh_option		= rtas_token("ibm,set-eeh-option");
	ibm_set_slot_reset		= rtas_token("ibm,set-slot-reset");
	ibm_read_slot_reset_state2	= rtas_token("ibm,read-slot-reset-state2");
	ibm_read_slot_reset_state	= rtas_token("ibm,read-slot-reset-state");
	ibm_slot_error_detail		= rtas_token("ibm,slot-error-detail");
	ibm_get_config_addr_info2	= rtas_token("ibm,get-config-addr-info2");
	ibm_get_config_addr_info	= rtas_token("ibm,get-config-addr-info");
	ibm_configure_pe		= rtas_token("ibm,configure-pe");

	/*
	 * ibm,configure-pe and ibm,configure-bridge have the same semantics,
	 * however ibm,configure-pe can be faster.  If we can't find
	 * ibm,configure-pe then fall back to using ibm,configure-bridge.
	 */
	if (ibm_configure_pe == RTAS_UNKNOWN_SERVICE)
		ibm_configure_pe 	= rtas_token("ibm,configure-bridge");

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

	/* Initialize error log lock and size */
	spin_lock_init(&slot_errbuf_lock);
	eeh_error_buf_size = rtas_token("rtas-error-log-max");
	if (eeh_error_buf_size == RTAS_UNKNOWN_SERVICE) {
		pr_info("%s: unknown EEH error log size\n",
			__func__);
		eeh_error_buf_size = 1024;
	} else if (eeh_error_buf_size > RTAS_ERROR_LOG_MAX) {
		pr_info("%s: EEH error log size %d exceeds the maximal %d\n",
			__func__, eeh_error_buf_size, RTAS_ERROR_LOG_MAX);
		eeh_error_buf_size = RTAS_ERROR_LOG_MAX;
	}

	/* Set EEH probe mode */
	eeh_add_flag(EEH_PROBE_MODE_DEVTREE | EEH_ENABLE_IO_FOR_LOG);

#ifdef CONFIG_PCI_IOV
	/* Set EEH machine dependent code */
	ppc_md.pcibios_bus_add_device = pseries_pcibios_bus_add_device;
#endif

	return 0;
}

static int pseries_eeh_cap_start(struct pci_dn *pdn)
{
	u32 status;

	if (!pdn)
		return 0;

	rtas_read_config(pdn, PCI_STATUS, 2, &status);
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
		rtas_read_config(pdn, pos, 1, &pos);
		if (pos < 0x40)
			break;
		pos &= ~3;
		rtas_read_config(pdn, pos + PCI_CAP_LIST_ID, 1, &id);
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
	if (rtas_read_config(pdn, pos, 4, &header) != PCIBIOS_SUCCESSFUL)
		return 0;
	else if (!header)
		return 0;

	while (ttl-- > 0) {
		if (PCI_EXT_CAP_ID(header) == cap && pos)
			return pos;

		pos = PCI_EXT_CAP_NEXT(header);
		if (pos < 256)
			break;

		if (rtas_read_config(pdn, pos, 4, &header) != PCIBIOS_SUCCESSFUL)
			break;
	}

	return 0;
}

/**
 * pseries_eeh_probe - EEH probe on the given device
 * @pdn: PCI device node
 * @data: Unused
 *
 * When EEH module is installed during system boot, all PCI devices
 * are checked one by one to see if it supports EEH. The function
 * is introduced for the purpose.
 */
static void *pseries_eeh_probe(struct pci_dn *pdn, void *data)
{
	struct eeh_dev *edev;
	struct eeh_pe pe;
	u32 pcie_flags;
	int enable = 0;
	int ret;

	/* Retrieve OF node and eeh device */
	edev = pdn_to_eeh_dev(pdn);
	if (!edev || edev->pe)
		return NULL;

	/* Check class/vendor/device IDs */
	if (!pdn->vendor_id || !pdn->device_id || !pdn->class_code)
		return NULL;

	/* Skip for PCI-ISA bridge */
        if ((pdn->class_code >> 8) == PCI_CLASS_BRIDGE_ISA)
		return NULL;

	/*
	 * Update class code and mode of eeh device. We need
	 * correctly reflects that current device is root port
	 * or PCIe switch downstream port.
	 */
	edev->class_code = pdn->class_code;
	edev->pcix_cap = pseries_eeh_find_cap(pdn, PCI_CAP_ID_PCIX);
	edev->pcie_cap = pseries_eeh_find_cap(pdn, PCI_CAP_ID_EXP);
	edev->aer_cap = pseries_eeh_find_ecap(pdn, PCI_EXT_CAP_ID_ERR);
	edev->mode &= 0xFFFFFF00;
	if ((edev->class_code >> 8) == PCI_CLASS_BRIDGE_PCI) {
		edev->mode |= EEH_DEV_BRIDGE;
		if (edev->pcie_cap) {
			rtas_read_config(pdn, edev->pcie_cap + PCI_EXP_FLAGS,
					 2, &pcie_flags);
			pcie_flags = (pcie_flags & PCI_EXP_FLAGS_TYPE) >> 4;
			if (pcie_flags == PCI_EXP_TYPE_ROOT_PORT)
				edev->mode |= EEH_DEV_ROOT_PORT;
			else if (pcie_flags == PCI_EXP_TYPE_DOWNSTREAM)
				edev->mode |= EEH_DEV_DS_PORT;
		}
	}

	/* Initialize the fake PE */
	memset(&pe, 0, sizeof(struct eeh_pe));
	pe.phb = pdn->phb;
	pe.config_addr = (pdn->busno << 16) | (pdn->devfn << 8);

	/* Enable EEH on the device */
	ret = eeh_ops->set_option(&pe, EEH_OPT_ENABLE);
	if (!ret) {
		/* Retrieve PE address */
		edev->pe_config_addr = eeh_ops->get_pe_addr(&pe);
		pe.addr = edev->pe_config_addr;

		/* Some older systems (Power4) allow the ibm,set-eeh-option
		 * call to succeed even on nodes where EEH is not supported.
		 * Verify support explicitly.
		 */
		ret = eeh_ops->get_state(&pe, NULL);
		if (ret > 0 && ret != EEH_STATE_NOT_SUPPORT)
			enable = 1;

		if (enable) {
			eeh_add_flag(EEH_ENABLED);
			eeh_add_to_parent_pe(edev);

			pr_debug("%s: EEH enabled on %02x:%02x.%01x PHB#%x-PE#%x\n",
				__func__, pdn->busno, PCI_SLOT(pdn->devfn),
				PCI_FUNC(pdn->devfn), pe.phb->global_number,
				pe.addr);
		} else if (pdn->parent && pdn_to_eeh_dev(pdn->parent) &&
			   (pdn_to_eeh_dev(pdn->parent))->pe) {
			/* This device doesn't support EEH, but it may have an
			 * EEH parent, in which case we mark it as supported.
			 */
			edev->pe_config_addr = pdn_to_eeh_dev(pdn->parent)->pe_config_addr;
			eeh_add_to_parent_pe(edev);
		}
	}

	/* Save memory bars */
	eeh_save_bars(edev);

	return NULL;
}

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
	int config_addr;

	/*
	 * When we're enabling or disabling EEH functioality on
	 * the particular PE, the PE config address is possibly
	 * unavailable. Therefore, we have to figure it out from
	 * the FDT node.
	 */
	switch (option) {
	case EEH_OPT_DISABLE:
	case EEH_OPT_ENABLE:
	case EEH_OPT_THAW_MMIO:
	case EEH_OPT_THAW_DMA:
		config_addr = pe->config_addr;
		if (pe->addr)
			config_addr = pe->addr;
		break;
	case EEH_OPT_FREEZE_PE:
		/* Not support */
		return 0;
	default:
		pr_err("%s: Invalid option %d\n",
			__func__, option);
		return -EINVAL;
	}

	ret = rtas_call(ibm_set_eeh_option, 4, 1, NULL,
			config_addr, BUID_HI(pe->phb->buid),
			BUID_LO(pe->phb->buid), option);

	return ret;
}

/**
 * pseries_eeh_get_pe_addr - Retrieve PE address
 * @pe: EEH PE
 *
 * Retrieve the assocated PE address. Actually, there're 2 RTAS
 * function calls dedicated for the purpose. We need implement
 * it through the new function and then the old one. Besides,
 * you should make sure the config address is figured out from
 * FDT node before calling the function.
 *
 * It's notable that zero'ed return value means invalid PE config
 * address.
 */
static int pseries_eeh_get_pe_addr(struct eeh_pe *pe)
{
	int ret = 0;
	int rets[3];

	if (ibm_get_config_addr_info2 != RTAS_UNKNOWN_SERVICE) {
		/*
		 * First of all, we need to make sure there has one PE
		 * associated with the device. Otherwise, PE address is
		 * meaningless.
		 */
		ret = rtas_call(ibm_get_config_addr_info2, 4, 2, rets,
				pe->config_addr, BUID_HI(pe->phb->buid),
				BUID_LO(pe->phb->buid), 1);
		if (ret || (rets[0] == 0))
			return 0;

		/* Retrieve the associated PE config address */
		ret = rtas_call(ibm_get_config_addr_info2, 4, 2, rets,
				pe->config_addr, BUID_HI(pe->phb->buid),
				BUID_LO(pe->phb->buid), 0);
		if (ret) {
			pr_warn("%s: Failed to get address for PHB#%x-PE#%x\n",
				__func__, pe->phb->global_number, pe->config_addr);
			return 0;
		}

		return rets[0];
	}

	if (ibm_get_config_addr_info != RTAS_UNKNOWN_SERVICE) {
		ret = rtas_call(ibm_get_config_addr_info, 4, 2, rets,
				pe->config_addr, BUID_HI(pe->phb->buid),
				BUID_LO(pe->phb->buid), 0);
		if (ret) {
			pr_warn("%s: Failed to get address for PHB#%x-PE#%x\n",
				__func__, pe->phb->global_number, pe->config_addr);
			return 0;
		}

		return rets[0];
	}

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
	int config_addr;
	int ret;
	int rets[4];
	int result;

	/* Figure out PE config address if possible */
	config_addr = pe->config_addr;
	if (pe->addr)
		config_addr = pe->addr;

	if (ibm_read_slot_reset_state2 != RTAS_UNKNOWN_SERVICE) {
		ret = rtas_call(ibm_read_slot_reset_state2, 3, 4, rets,
				config_addr, BUID_HI(pe->phb->buid),
				BUID_LO(pe->phb->buid));
	} else if (ibm_read_slot_reset_state != RTAS_UNKNOWN_SERVICE) {
		/* Fake PE unavailable info */
		rets[2] = 0;
		ret = rtas_call(ibm_read_slot_reset_state, 3, 3, rets,
				config_addr, BUID_HI(pe->phb->buid),
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
	int config_addr;
	int ret;

	/* Figure out PE address */
	config_addr = pe->config_addr;
	if (pe->addr)
		config_addr = pe->addr;

	/* Reset PE through RTAS call */
	ret = rtas_call(ibm_set_slot_reset, 4, 1, NULL,
			config_addr, BUID_HI(pe->phb->buid),
			BUID_LO(pe->phb->buid), option);

	/* If fundamental-reset not supported, try hot-reset */
	if (option == EEH_RESET_FUNDAMENTAL &&
	    ret == -8) {
		option = EEH_RESET_HOT;
		ret = rtas_call(ibm_set_slot_reset, 4, 1, NULL,
				config_addr, BUID_HI(pe->phb->buid),
				BUID_LO(pe->phb->buid), option);
	}

	/* We need reset hold or settlement delay */
	if (option == EEH_RESET_FUNDAMENTAL ||
	    option == EEH_RESET_HOT)
		msleep(EEH_PE_RST_HOLD_TIME);
	else
		msleep(EEH_PE_RST_SETTLE_TIME);

	return ret;
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
	int config_addr;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&slot_errbuf_lock, flags);
	memset(slot_errbuf, 0, eeh_error_buf_size);

	/* Figure out the PE address */
	config_addr = pe->config_addr;
	if (pe->addr)
		config_addr = pe->addr;

	ret = rtas_call(ibm_slot_error_detail, 8, 1, NULL, config_addr,
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
 * The function will be called to reconfigure the bridges included
 * in the specified PE so that the mulfunctional PE would be recovered
 * again.
 */
static int pseries_eeh_configure_bridge(struct eeh_pe *pe)
{
	int config_addr;
	int ret;
	/* Waiting 0.2s maximum before skipping configuration */
	int max_wait = 200;

	/* Figure out the PE address */
	config_addr = pe->config_addr;
	if (pe->addr)
		config_addr = pe->addr;

	while (max_wait > 0) {
		ret = rtas_call(ibm_configure_pe, 3, 1, NULL,
				config_addr, BUID_HI(pe->phb->buid),
				BUID_LO(pe->phb->buid));

		if (!ret)
			return ret;

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
		__func__, pe->phb->global_number, pe->addr, ret);
	return ret;
}

/**
 * pseries_eeh_read_config - Read PCI config space
 * @pdn: PCI device node
 * @where: PCI address
 * @size: size to read
 * @val: return value
 *
 * Read config space from the speicifed device
 */
static int pseries_eeh_read_config(struct pci_dn *pdn, int where, int size, u32 *val)
{
	return rtas_read_config(pdn, where, size, val);
}

/**
 * pseries_eeh_write_config - Write PCI config space
 * @pdn: PCI device node
 * @where: PCI address
 * @size: size to write
 * @val: value to be written
 *
 * Write config space to the specified device
 */
static int pseries_eeh_write_config(struct pci_dn *pdn, int where, int size, u32 val)
{
	return rtas_write_config(pdn, where, size, val);
}

static int pseries_eeh_restore_config(struct pci_dn *pdn)
{
	struct eeh_dev *edev = pdn_to_eeh_dev(pdn);
	s64 ret = 0;

	if (!edev)
		return -EEXIST;

	/*
	 * FIXME: The MPS, error routing rules, timeout setting are worthy
	 * to be exported by firmware in extendible way.
	 */
	if (edev->physfn)
		ret = eeh_restore_vf_config(pdn);

	if (ret) {
		pr_warn("%s: Can't reinit PCI dev 0x%x (%lld)\n",
			__func__, edev->pe_config_addr, ret);
		return -EIO;
	}

	return ret;
}

#ifdef CONFIG_PCI_IOV
int pseries_send_allow_unfreeze(struct pci_dn *pdn,
				u16 *vf_pe_array, int cur_vfs)
{
	int rc;
	int ibm_allow_unfreeze = rtas_token("ibm,open-sriov-allow-unfreeze");
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
	struct pci_dn *pdn, *tmp, *parent, *physfn_pdn;
	int cur_vfs = 0, rc = 0, vf_index, bus, devfn;
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
			vf_pe_array[0] = cpu_to_be16(pdn->pe_number);
			physfn_pdn = pci_get_pdn(edev->physfn);
			rc = pseries_send_allow_unfreeze(physfn_pdn,
							 vf_pe_array, 1);
			pdn->last_allow_rc = rc;
		}
	}

	kfree(vf_pe_array);
	return rc;
}

static int pseries_notify_resume(struct pci_dn *pdn)
{
	struct eeh_dev *edev = pdn_to_eeh_dev(pdn);

	if (!edev)
		return -EEXIST;

	if (rtas_token("ibm,open-sriov-allow-unfreeze")
	    == RTAS_UNKNOWN_SERVICE)
		return -EINVAL;

	if (edev->pdev->is_physfn || edev->pdev->is_virtfn)
		return pseries_call_allow_unfreeze(edev);

	return 0;
}
#endif

static struct eeh_ops pseries_eeh_ops = {
	.name			= "pseries",
	.init			= pseries_eeh_init,
	.probe			= pseries_eeh_probe,
	.set_option		= pseries_eeh_set_option,
	.get_pe_addr		= pseries_eeh_get_pe_addr,
	.get_state		= pseries_eeh_get_state,
	.reset			= pseries_eeh_reset,
	.get_log		= pseries_eeh_get_log,
	.configure_bridge       = pseries_eeh_configure_bridge,
	.err_inject		= NULL,
	.read_config		= pseries_eeh_read_config,
	.write_config		= pseries_eeh_write_config,
	.next_error		= NULL,
	.restore_config		= pseries_eeh_restore_config,
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
	int ret;

	ret = eeh_ops_register(&pseries_eeh_ops);
	if (!ret)
		pr_info("EEH: pSeries platform initialized\n");
	else
		pr_info("EEH: pSeries platform initialization failure (%d)\n",
			ret);

	return ret;
}
machine_early_initcall(pseries, eeh_pseries_init);
