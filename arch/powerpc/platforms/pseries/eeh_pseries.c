/*
 * The file intends to implement the platform dependent EEH operations on pseries.
 * Actually, the pseries platform is built based on RTAS heavily. That means the
 * pseries platform dependent EEH operations will be built on RTAS calls. The functions
 * are devired from arch/powerpc/platforms/pseries/eeh.c and necessary cleanup has
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
static int ibm_configure_bridge;
static int ibm_configure_pe;

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
	ibm_configure_bridge		= rtas_token("ibm,configure-bridge");

	/* necessary sanity check */
	if (ibm_set_eeh_option == RTAS_UNKNOWN_SERVICE) {
		pr_warning("%s: RTAS service <ibm,set-eeh-option> invalid\n",
			__func__);
		return -EINVAL;
	} else if (ibm_set_slot_reset == RTAS_UNKNOWN_SERVICE) {
		pr_warning("%s: RTAS service <ibm,set-slot-reset> invalid\n",
			__func__);
		return -EINVAL;
	} else if (ibm_read_slot_reset_state2 == RTAS_UNKNOWN_SERVICE &&
		   ibm_read_slot_reset_state == RTAS_UNKNOWN_SERVICE) {
		pr_warning("%s: RTAS service <ibm,read-slot-reset-state2> and "
			"<ibm,read-slot-reset-state> invalid\n",
			__func__);
		return -EINVAL;
	} else if (ibm_slot_error_detail == RTAS_UNKNOWN_SERVICE) {
		pr_warning("%s: RTAS service <ibm,slot-error-detail> invalid\n",
			__func__);
		return -EINVAL;
	} else if (ibm_get_config_addr_info2 == RTAS_UNKNOWN_SERVICE &&
		   ibm_get_config_addr_info == RTAS_UNKNOWN_SERVICE) {
		pr_warning("%s: RTAS service <ibm,get-config-addr-info2> and "
			"<ibm,get-config-addr-info> invalid\n",
			__func__);
		return -EINVAL;
	} else if (ibm_configure_pe == RTAS_UNKNOWN_SERVICE &&
		   ibm_configure_bridge == RTAS_UNKNOWN_SERVICE) {
		pr_warning("%s: RTAS service <ibm,configure-pe> and "
			"<ibm,configure-bridge> invalid\n",
			__func__);
		return -EINVAL;
	}

	/* Initialize error log lock and size */
	spin_lock_init(&slot_errbuf_lock);
	eeh_error_buf_size = rtas_token("rtas-error-log-max");
	if (eeh_error_buf_size == RTAS_UNKNOWN_SERVICE) {
		pr_warning("%s: unknown EEH error log size\n",
			__func__);
		eeh_error_buf_size = 1024;
	} else if (eeh_error_buf_size > RTAS_ERROR_LOG_MAX) {
		pr_warning("%s: EEH error log size %d exceeds the maximal %d\n",
			__func__, eeh_error_buf_size, RTAS_ERROR_LOG_MAX);
		eeh_error_buf_size = RTAS_ERROR_LOG_MAX;
	}

	/* Set EEH probe mode */
	eeh_probe_mode_set(EEH_PROBE_MODE_DEVTREE);

	return 0;
}

/**
 * pseries_eeh_of_probe - EEH probe on the given device
 * @dn: OF node
 * @flag: Unused
 *
 * When EEH module is installed during system boot, all PCI devices
 * are checked one by one to see if it supports EEH. The function
 * is introduced for the purpose.
 */
static void *pseries_eeh_of_probe(struct device_node *dn, void *flag)
{
	struct eeh_dev *edev;
	struct eeh_pe pe;
	const u32 *class_code, *vendor_id, *device_id;
	const u32 *regs;
	int enable = 0;
	int ret;

	/* Retrieve OF node and eeh device */
	edev = of_node_to_eeh_dev(dn);
	if (!of_device_is_available(dn))
		return NULL;

	/* Retrieve class/vendor/device IDs */
	class_code = of_get_property(dn, "class-code", NULL);
	vendor_id  = of_get_property(dn, "vendor-id", NULL);
	device_id  = of_get_property(dn, "device-id", NULL);

	/* Skip for bad OF node or PCI-ISA bridge */
	if (!class_code || !vendor_id || !device_id)
		return NULL;
	if (dn->type && !strcmp(dn->type, "isa"))
		return NULL;

	/* Update class code and mode of eeh device */
	edev->class_code = *class_code;
	edev->mode = 0;

	/* Retrieve the device address */
	regs = of_get_property(dn, "reg", NULL);
	if (!regs) {
		pr_warning("%s: OF node property %s::reg not found\n",
			__func__, dn->full_name);
		return NULL;
	}

	/* Initialize the fake PE */
	memset(&pe, 0, sizeof(struct eeh_pe));
	pe.phb = edev->phb;
	pe.config_addr = regs[0];

	/* Enable EEH on the device */
	ret = eeh_ops->set_option(&pe, EEH_OPT_ENABLE);
	if (!ret) {
		edev->config_addr = regs[0];
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
			eeh_subsystem_enabled = 1;
			eeh_add_to_parent_pe(edev);

			pr_debug("%s: EEH enabled on %s PHB#%d-PE#%x, config addr#%x\n",
				__func__, dn->full_name, pe.phb->global_number,
				pe.addr, pe.config_addr);
		} else if (dn->parent && of_node_to_eeh_dev(dn->parent) &&
			   (of_node_to_eeh_dev(dn->parent))->pe) {
			/* This device doesn't support EEH, but it may have an
			 * EEH parent, in which case we mark it as supported.
			 */
			edev->config_addr = of_node_to_eeh_dev(dn->parent)->config_addr;
			edev->pe_config_addr = of_node_to_eeh_dev(dn->parent)->pe_config_addr;
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
			pr_warning("%s: Failed to get address for PHB#%d-PE#%x\n",
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
			pr_warning("%s: Failed to get address for PHB#%d-PE#%x\n",
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
 * @state: return value
 *
 * Retrieve the state of the specified PE. On RTAS compliant
 * pseries platform, there already has one dedicated RTAS function
 * for the purpose. It's notable that the associated PE config address
 * might be ready when calling the function. Therefore, endeavour to
 * use the PE config address if possible. Further more, there're 2
 * RTAS calls for the purpose, we need to try the new one and back
 * to the old one if the new one couldn't work properly.
 */
static int pseries_eeh_get_state(struct eeh_pe *pe, int *state)
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
	result = 0;
	if (rets[1]) {
		switch(rets[0]) {
		case 0:
			result &= ~EEH_STATE_RESET_ACTIVE;
			result |= EEH_STATE_MMIO_ACTIVE;
			result |= EEH_STATE_DMA_ACTIVE;
			break;
		case 1:
			result |= EEH_STATE_RESET_ACTIVE;
			result |= EEH_STATE_MMIO_ACTIVE;
			result |= EEH_STATE_DMA_ACTIVE;
			break;
		case 2:
			result &= ~EEH_STATE_RESET_ACTIVE;
			result &= ~EEH_STATE_MMIO_ACTIVE;
			result &= ~EEH_STATE_DMA_ACTIVE;
			break;
		case 4:
			result &= ~EEH_STATE_RESET_ACTIVE;
			result &= ~EEH_STATE_MMIO_ACTIVE;
			result &= ~EEH_STATE_DMA_ACTIVE;
			result |= EEH_STATE_MMIO_ENABLED;
			break;
		case 5:
			if (rets[2]) {
				if (state) *state = rets[2];
				result = EEH_STATE_UNAVAILABLE;
			} else {
				result = EEH_STATE_NOT_SUPPORT;
			}
		default:
			result = EEH_STATE_NOT_SUPPORT;
		}
	} else {
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
		ret = rtas_call(ibm_set_slot_reset, 4, 1, NULL,
				config_addr, BUID_HI(pe->phb->buid),
				BUID_LO(pe->phb->buid), EEH_RESET_HOT);
	}

	return ret;
}

/**
 * pseries_eeh_wait_state - Wait for PE state
 * @pe: EEH PE
 * @max_wait: maximal period in microsecond
 *
 * Wait for the state of associated PE. It might take some time
 * to retrieve the PE's state.
 */
static int pseries_eeh_wait_state(struct eeh_pe *pe, int max_wait)
{
	int ret;
	int mwait;

	/*
	 * According to PAPR, the state of PE might be temporarily
	 * unavailable. Under the circumstance, we have to wait
	 * for indicated time determined by firmware. The maximal
	 * wait time is 5 minutes, which is acquired from the original
	 * EEH implementation. Also, the original implementation
	 * also defined the minimal wait time as 1 second.
	 */
#define EEH_STATE_MIN_WAIT_TIME	(1000)
#define EEH_STATE_MAX_WAIT_TIME	(300 * 1000)

	while (1) {
		ret = pseries_eeh_get_state(pe, &mwait);

		/*
		 * If the PE's state is temporarily unavailable,
		 * we have to wait for the specified time. Otherwise,
		 * the PE's state will be returned immediately.
		 */
		if (ret != EEH_STATE_UNAVAILABLE)
			return ret;

		if (max_wait <= 0) {
			pr_warning("%s: Timeout when getting PE's state (%d)\n",
				__func__, max_wait);
			return EEH_STATE_NOT_SUPPORT;
		}

		if (mwait <= 0) {
			pr_warning("%s: Firmware returned bad wait value %d\n",
				__func__, mwait);
			mwait = EEH_STATE_MIN_WAIT_TIME;
		} else if (mwait > EEH_STATE_MAX_WAIT_TIME) {
			pr_warning("%s: Firmware returned too long wait value %d\n",
				__func__, mwait);
			mwait = EEH_STATE_MAX_WAIT_TIME;
		}

		max_wait -= mwait;
		msleep(mwait);
	}

	return EEH_STATE_NOT_SUPPORT;
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

	/* Figure out the PE address */
	config_addr = pe->config_addr;
	if (pe->addr)
		config_addr = pe->addr;

	/* Use new configure-pe function, if supported */
	if (ibm_configure_pe != RTAS_UNKNOWN_SERVICE) {
		ret = rtas_call(ibm_configure_pe, 3, 1, NULL,
				config_addr, BUID_HI(pe->phb->buid),
				BUID_LO(pe->phb->buid));
	} else if (ibm_configure_bridge != RTAS_UNKNOWN_SERVICE) {
		ret = rtas_call(ibm_configure_bridge, 3, 1, NULL,
				config_addr, BUID_HI(pe->phb->buid),
				BUID_LO(pe->phb->buid));
	} else {
		return -EFAULT;
	}

	if (ret)
		pr_warning("%s: Unable to configure bridge PHB#%d-PE#%x (%d)\n",
			__func__, pe->phb->global_number, pe->addr, ret);

	return ret;
}

/**
 * pseries_eeh_read_config - Read PCI config space
 * @dn: device node
 * @where: PCI address
 * @size: size to read
 * @val: return value
 *
 * Read config space from the speicifed device
 */
static int pseries_eeh_read_config(struct device_node *dn, int where, int size, u32 *val)
{
	struct pci_dn *pdn;

	pdn = PCI_DN(dn);

	return rtas_read_config(pdn, where, size, val);
}

/**
 * pseries_eeh_write_config - Write PCI config space
 * @dn: device node
 * @where: PCI address
 * @size: size to write
 * @val: value to be written
 *
 * Write config space to the specified device
 */
static int pseries_eeh_write_config(struct device_node *dn, int where, int size, u32 val)
{
	struct pci_dn *pdn;

	pdn = PCI_DN(dn);

	return rtas_write_config(pdn, where, size, val);
}

static struct eeh_ops pseries_eeh_ops = {
	.name			= "pseries",
	.init			= pseries_eeh_init,
	.of_probe		= pseries_eeh_of_probe,
	.dev_probe		= NULL,
	.set_option		= pseries_eeh_set_option,
	.get_pe_addr		= pseries_eeh_get_pe_addr,
	.get_state		= pseries_eeh_get_state,
	.reset			= pseries_eeh_reset,
	.wait_state		= pseries_eeh_wait_state,
	.get_log		= pseries_eeh_get_log,
	.configure_bridge       = pseries_eeh_configure_bridge,
	.read_config		= pseries_eeh_read_config,
	.write_config		= pseries_eeh_write_config
};

/**
 * eeh_pseries_init - Register platform dependent EEH operations
 *
 * EEH initialization on pseries platform. This function should be
 * called before any EEH related functions.
 */
static int __init eeh_pseries_init(void)
{
	int ret = -EINVAL;

	if (!machine_is(pseries))
		return ret;

	ret = eeh_ops_register(&pseries_eeh_ops);
	if (!ret)
		pr_info("EEH: pSeries platform initialized\n");
	else
		pr_info("EEH: pSeries platform initialization failure (%d)\n",
			ret);

	return ret;
}

early_initcall(eeh_pseries_init);
