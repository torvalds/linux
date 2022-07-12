/*
 * Linux DHD Bus Module for PCIE
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhd_pcie_linux.c 701741 2017-05-26 08:18:08Z $
 */

/* include files */
#include <typedefs.h>
#include <bcmutils.h>
#include <bcmdevs.h>
#include <siutils.h>
#include <hndsoc.h>
#include <hndpmu.h>
#include <sbchipc.h>
#if defined(DHD_DEBUG)
#include <hnd_armtrap.h>
#include <hnd_cons.h>
#endif /* defined(DHD_DEBUG) */
#include <dngl_stats.h>
#include <pcie_core.h>
#include <dhd.h>
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <dhdioctl.h>
#include <bcmmsgbuf.h>
#include <pcicfg.h>
#include <dhd_pcie.h>
#include <dhd_linux.h>
#ifdef OEM_ANDROID
#ifdef CONFIG_ARCH_MSM
#if defined(CONFIG_PCI_MSM) || defined(CONFIG_ARCH_MSM8996)
#include <linux/msm_pcie.h>
#else
#include <mach/msm_pcie.h>
#endif /* CONFIG_PCI_MSM */
#endif /* CONFIG_ARCH_MSM */
#endif /* OEM_ANDROID */

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
#include <linux/pm_runtime.h>
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
#ifndef AUTO_SUSPEND_TIMEOUT
#define AUTO_SUSPEND_TIMEOUT 1000
#endif /* AUTO_SUSPEND_TIMEOUT */
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

#include <linux/irq.h>
#ifdef USE_SMMU_ARCH_MSM
#include <asm/dma-iommu.h>
#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#endif /* USE_SMMU_ARCH_MSM */

#define PCI_CFG_RETRY 		10
#define OS_HANDLE_MAGIC		0x1234abcd	/* Magic # to recognize osh */
#define BCM_MEM_FILENAME_LEN 	24		/* Mem. filename length */

#ifdef FORCE_TPOWERON
extern uint32 tpoweron_scale;
#endif /* FORCE_TPOWERON */
/* user defined data structures  */

typedef bool (*dhdpcie_cb_fn_t)(void *);

typedef struct dhdpcie_info
{
	dhd_bus_t	*bus;
	osl_t		*osh;
	struct pci_dev  *dev;		/* pci device handle */
	volatile char	*regs;		/* pci device memory va */
	volatile char	*tcm;		/* pci device memory va */
	uint32		bar1_size;	/* pci device memory size */
	uint32		curr_bar1_win;	/* current PCIEBar1Window setting */
	struct pcos_info *pcos_info;
	uint16		last_intrstatus;	/* to cache intrstatus */
	int	irq;
	char pciname[32];
	struct pci_saved_state* default_state;
	struct pci_saved_state* state;
#ifdef BCMPCIE_OOB_HOST_WAKE
	void *os_cxt;			/* Pointer to per-OS private data */
#endif /* BCMPCIE_OOB_HOST_WAKE */
#ifdef DHD_WAKE_STATUS
	spinlock_t	pcie_lock;
	unsigned int	total_wake_count;
	int		pkt_wake;
	int		wake_irq;
#endif /* DHD_WAKE_STATUS */
#ifdef USE_SMMU_ARCH_MSM
	void *smmu_cxt;
#endif /* USE_SMMU_ARCH_MSM */
} dhdpcie_info_t;

struct pcos_info {
	dhdpcie_info_t *pc;
	spinlock_t lock;
	wait_queue_head_t intr_wait_queue;
	struct timer_list tuning_timer;
	int tuning_timer_exp;
	atomic_t timer_enab;
	struct tasklet_struct tuning_tasklet;
};

#ifdef BCMPCIE_OOB_HOST_WAKE
typedef struct dhdpcie_os_info {
	int			oob_irq_num;	/* valid when hardware or software oob in use */
	unsigned long		oob_irq_flags;	/* valid when hardware or software oob in use */
	bool			oob_irq_registered;
	bool			oob_irq_enabled;
	bool			oob_irq_wake_enabled;
	spinlock_t		oob_irq_spinlock;
	void			*dev;		/* handle to the underlying device */
} dhdpcie_os_info_t;
static irqreturn_t wlan_oob_irq(int irq, void *data);
#ifdef CUSTOMER_HW2
extern struct brcm_pcie_wake brcm_pcie_wake;
#endif /* CUSTOMER_HW2 */
#endif /* BCMPCIE_OOB_HOST_WAKE */

#ifdef USE_SMMU_ARCH_MSM
typedef struct dhdpcie_smmu_info {
	struct dma_iommu_mapping *smmu_mapping;
	dma_addr_t smmu_iova_start;
	size_t smmu_iova_len;
} dhdpcie_smmu_info_t;
#endif /* USE_SMMU_ARCH_MSM */

/* function declarations */
static int __devinit
dhdpcie_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void __devexit
dhdpcie_pci_remove(struct pci_dev *pdev);
static int dhdpcie_init(struct pci_dev *pdev);
static irqreturn_t dhdpcie_isr(int irq, void *arg);
/* OS Routine functions for PCI suspend/resume */

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
static int dhdpcie_set_suspend_resume(struct pci_dev *dev, bool state, bool byint);
#else
static int dhdpcie_set_suspend_resume(dhd_bus_t *bus, bool state);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
static int dhdpcie_resume_host_dev(dhd_bus_t *bus);
static int dhdpcie_suspend_host_dev(dhd_bus_t *bus);
static int dhdpcie_resume_dev(struct pci_dev *dev);
static int dhdpcie_suspend_dev(struct pci_dev *dev);
#ifdef DHD_PCIE_RUNTIMEPM
static int dhdpcie_pm_suspend(struct device *dev);
static int dhdpcie_pm_prepare(struct device *dev);
static int dhdpcie_pm_resume(struct device *dev);
static void dhdpcie_pm_complete(struct device *dev);
#else
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
static int dhdpcie_pm_system_suspend_noirq(struct device * dev);
static int dhdpcie_pm_system_resume_noirq(struct device * dev);
#else
static int dhdpcie_pci_suspend(struct pci_dev *dev, pm_message_t state);
static int dhdpcie_pci_resume(struct pci_dev *dev);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
#endif /* DHD_PCIE_RUNTIMEPM */

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
static int dhdpcie_pm_runtime_suspend(struct device * dev);
static int dhdpcie_pm_runtime_resume(struct device * dev);
static int dhdpcie_pm_system_suspend_noirq(struct device * dev);
static int dhdpcie_pm_system_resume_noirq(struct device * dev);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

static void dhdpcie_config_save_restore_coherent(dhd_bus_t *bus, bool state);

uint32
dhdpcie_access_cap(struct pci_dev *pdev, int cap, uint offset, bool is_ext, bool is_write,
	uint32 writeval);

static struct pci_device_id dhdpcie_pci_devid[] __devinitdata = {
	{
	vendor: VENDOR_CYPRESS,
	device: PCI_ANY_ID,
	subvendor: PCI_ANY_ID,
	subdevice: PCI_ANY_ID,
	class: PCI_CLASS_NETWORK_OTHER << 8,
	class_mask: 0xffff00,
	driver_data: 0,
	},
	{
	vendor: VENDOR_BROADCOM,
	device: PCI_ANY_ID,
	subvendor: PCI_ANY_ID,
	subdevice: PCI_ANY_ID,
	class: PCI_CLASS_NETWORK_OTHER << 8,
	class_mask: 0xffff00,
	driver_data: 0,
	},
	{ 0, 0, 0, 0, 0, 0, 0}
};
MODULE_DEVICE_TABLE(pci, dhdpcie_pci_devid);

/* Power Management Hooks */
#ifdef DHD_PCIE_RUNTIMEPM
static const struct dev_pm_ops dhd_pcie_pm_ops = {
	.prepare = dhdpcie_pm_prepare,
	.suspend = dhdpcie_pm_suspend,
	.resume = dhdpcie_pm_resume,
	.complete = dhdpcie_pm_complete,
};
#endif /* DHD_PCIE_RUNTIMEPM */
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
static const struct dev_pm_ops dhdpcie_pm_ops = {
	SET_RUNTIME_PM_OPS(dhdpcie_pm_runtime_suspend, dhdpcie_pm_runtime_resume, NULL)
	.suspend_noirq = dhdpcie_pm_system_suspend_noirq,
	.resume_noirq = dhdpcie_pm_system_resume_noirq
};
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

static struct pci_driver dhdpcie_driver = {
	node:		{&dhdpcie_driver.node, &dhdpcie_driver.node},
#ifndef BCMDHDX
	name:		"pcieh",
#else
	name:		"pciehx",
#endif /* BCMDHDX */
	id_table:	dhdpcie_pci_devid,
	probe:		dhdpcie_pci_probe,
	remove:		dhdpcie_pci_remove,
#if defined(DHD_PCIE_RUNTIMEPM) || defined(DHD_PCIE_NATIVE_RUNTIMEPM)
	.driver.pm = &dhd_pcie_pm_ops,
#else
	suspend:	dhdpcie_pci_suspend,
	resume:		dhdpcie_pci_resume,
#endif /* DHD_PCIE_RUNTIMEPM || DHD_PCIE_NATIVE_RUNTIMEPM */
};

int dhdpcie_init_succeeded = FALSE;

#ifdef USE_SMMU_ARCH_MSM
static int dhdpcie_smmu_init(struct pci_dev *pdev, void *smmu_cxt)
{
	struct dma_iommu_mapping *mapping;
	struct device_node *root_node = NULL;
	dhdpcie_smmu_info_t *smmu_info = (dhdpcie_smmu_info_t *)smmu_cxt;
	int smmu_iova_address[2];
	char *wlan_node = "android,bcmdhd_wlan";
	char *wlan_smmu_node = "wlan-smmu-iova-address";
	int atomic_ctx = 1;
	int s1_bypass = 1;
	int ret = 0;

	DHD_ERROR(("%s: SMMU initialize\n", __FUNCTION__));

	root_node = of_find_compatible_node(NULL, NULL, wlan_node);
	if (!root_node) {
		WARN(1, "failed to get device node of BRCM WLAN\n");
		return -ENODEV;
	}

	if (of_property_read_u32_array(root_node, wlan_smmu_node,
		smmu_iova_address, 2) == 0) {
		DHD_ERROR(("%s : get SMMU start address 0x%x, size 0x%x\n",
			__FUNCTION__, smmu_iova_address[0], smmu_iova_address[1]));
		smmu_info->smmu_iova_start = smmu_iova_address[0];
		smmu_info->smmu_iova_len = smmu_iova_address[1];
	} else {
		printf("%s : can't get smmu iova address property\n",
			__FUNCTION__);
		return -ENODEV;
	}

	if (smmu_info->smmu_iova_len <= 0) {
		DHD_ERROR(("%s: Invalid smmu iova len %d\n",
			__FUNCTION__, (int)smmu_info->smmu_iova_len));
		return -EINVAL;
	}

	DHD_ERROR(("%s : SMMU init start\n", __FUNCTION__));

	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(64)) ||
		pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64))) {
		DHD_ERROR(("%s: DMA set 64bit mask failed.\n", __FUNCTION__));
		return -EINVAL;
	}

	mapping = arm_iommu_create_mapping(&platform_bus_type,
		smmu_info->smmu_iova_start, smmu_info->smmu_iova_len);
	if (IS_ERR(mapping)) {
		DHD_ERROR(("%s: create mapping failed, err = %d\n",
			__FUNCTION__, ret));
		ret = PTR_ERR(mapping);
		goto map_fail;
	}

	ret = iommu_domain_set_attr(mapping->domain,
		DOMAIN_ATTR_ATOMIC, &atomic_ctx);
	if (ret) {
		DHD_ERROR(("%s: set atomic_ctx attribute failed, err = %d\n",
			__FUNCTION__, ret));
		goto set_attr_fail;
	}

	ret = iommu_domain_set_attr(mapping->domain,
		DOMAIN_ATTR_S1_BYPASS, &s1_bypass);
	if (ret < 0) {
		DHD_ERROR(("%s: set s1_bypass attribute failed, err = %d\n",
			__FUNCTION__, ret));
		goto set_attr_fail;
	}

	ret = arm_iommu_attach_device(&pdev->dev, mapping);
	if (ret) {
		DHD_ERROR(("%s: attach device failed, err = %d\n",
			__FUNCTION__, ret));
		goto attach_fail;
	}

	smmu_info->smmu_mapping = mapping;

	return ret;

attach_fail:
set_attr_fail:
	arm_iommu_release_mapping(mapping);
map_fail:
	return ret;
}

static void dhdpcie_smmu_remove(struct pci_dev *pdev, void *smmu_cxt)
{
	dhdpcie_smmu_info_t *smmu_info;

	if (!smmu_cxt) {
		return;
	}

	smmu_info = (dhdpcie_smmu_info_t *)smmu_cxt;
	if (smmu_info->smmu_mapping) {
		arm_iommu_detach_device(&pdev->dev);
		arm_iommu_release_mapping(smmu_info->smmu_mapping);
		smmu_info->smmu_mapping = NULL;
	}
}
#endif /* USE_SMMU_ARCH_MSM */

#ifdef FORCE_TPOWERON
static void
dhd_bus_get_tpoweron(dhd_bus_t *bus)
{

	uint32 tpoweron_rc;
	uint32 tpoweron_ep;

	tpoweron_rc = dhdpcie_rc_access_cap(bus, PCIE_EXTCAP_ID_L1SS,
		PCIE_EXTCAP_L1SS_CONTROL2_OFFSET, TRUE, FALSE, 0);
	tpoweron_ep = dhdpcie_ep_access_cap(bus, PCIE_EXTCAP_ID_L1SS,
		PCIE_EXTCAP_L1SS_CONTROL2_OFFSET, TRUE, FALSE, 0);
	DHD_ERROR(("%s: tpoweron_rc:0x%x tpoweron_ep:0x%x\n",
		__FUNCTION__, tpoweron_rc, tpoweron_ep));
}

static void
dhd_bus_set_tpoweron(dhd_bus_t *bus, uint16 tpoweron)
{

	dhd_bus_get_tpoweron(bus);
	/* Set the tpoweron */
	DHD_ERROR(("%s tpoweron: 0x%x\n", __FUNCTION__, tpoweron));
	dhdpcie_rc_access_cap(bus, PCIE_EXTCAP_ID_L1SS,
		PCIE_EXTCAP_L1SS_CONTROL2_OFFSET, TRUE, TRUE, tpoweron);
	dhdpcie_ep_access_cap(bus, PCIE_EXTCAP_ID_L1SS,
		PCIE_EXTCAP_L1SS_CONTROL2_OFFSET, TRUE, TRUE, tpoweron);

	dhd_bus_get_tpoweron(bus);

}

static bool
dhdpcie_chip_req_forced_tpoweron(dhd_bus_t *bus)
{
	/*
	 * On Fire's reference platform, coming out of L1.2,
	 * there is a constant delay of 45us between CLKREQ# and stable REFCLK
	 * Due to this delay, with tPowerOn < 50
	 * there is a chance of the refclk sense to trigger on noise.
	 *
	 * Which ever chip needs forced tPowerOn of 50us should be listed below.
	 */
	if (si_chipid(bus->sih) == BCM4377_CHIP_ID) {
		return TRUE;
	}
	return FALSE;
}
#endif /* FORCE_TPOWERON */

static bool
dhd_bus_aspm_enable_dev(dhd_bus_t *bus, struct pci_dev *dev, bool enable)
{
	uint32 linkctrl_before;
	uint32 linkctrl_after = 0;
	uint8 linkctrl_asm;
	char *device;

	device = (dev == bus->dev) ? "EP" : "RC";

	linkctrl_before = dhdpcie_access_cap(dev, PCIE_CAP_ID_EXP, PCIE_CAP_LINKCTRL_OFFSET,
		FALSE, FALSE, 0);
	linkctrl_asm = (linkctrl_before & PCIE_ASPM_CTRL_MASK);

	if (enable) {
		if (linkctrl_asm == PCIE_ASPM_L1_ENAB) {
			DHD_ERROR(("%s: %s already enabled  linkctrl: 0x%x\n",
				__FUNCTION__, device, linkctrl_before));
			return FALSE;
		}
		/* Enable only L1 ASPM (bit 1) */
		dhdpcie_access_cap(dev, PCIE_CAP_ID_EXP, PCIE_CAP_LINKCTRL_OFFSET, FALSE,
			TRUE, (linkctrl_before | PCIE_ASPM_L1_ENAB));
	} else {
		if (linkctrl_asm == 0) {
			DHD_ERROR(("%s: %s already disabled linkctrl: 0x%x\n",
				__FUNCTION__, device, linkctrl_before));
			return FALSE;
		}
		/* Disable complete ASPM (bit 1 and bit 0) */
		dhdpcie_access_cap(dev, PCIE_CAP_ID_EXP, PCIE_CAP_LINKCTRL_OFFSET, FALSE,
			TRUE, (linkctrl_before & (~PCIE_ASPM_ENAB)));
	}

	linkctrl_after = dhdpcie_access_cap(dev, PCIE_CAP_ID_EXP, PCIE_CAP_LINKCTRL_OFFSET,
		FALSE, FALSE, 0);
	DHD_ERROR(("%s: %s %s, linkctrl_before: 0x%x linkctrl_after: 0x%x\n",
		__FUNCTION__, device, (enable ? "ENABLE " : "DISABLE"),
		linkctrl_before, linkctrl_after));

	return TRUE;
}

static bool
dhd_bus_is_rc_ep_aspm_capable(dhd_bus_t *bus)
{
	uint32 rc_aspm_cap;
	uint32 ep_aspm_cap;

	/* RC ASPM capability */
	rc_aspm_cap = dhdpcie_access_cap(bus->rc_dev, PCIE_CAP_ID_EXP, PCIE_CAP_LINKCTRL_OFFSET,
		FALSE, FALSE, 0);
	if (rc_aspm_cap == BCME_ERROR) {
		DHD_ERROR(("%s RC is not ASPM capable\n", __FUNCTION__));
		return FALSE;
	}

	/* EP ASPM capability */
	ep_aspm_cap = dhdpcie_access_cap(bus->dev, PCIE_CAP_ID_EXP, PCIE_CAP_LINKCTRL_OFFSET,
		FALSE, FALSE, 0);
	if (ep_aspm_cap == BCME_ERROR) {
		DHD_ERROR(("%s EP is not ASPM capable\n", __FUNCTION__));
		return FALSE;
	}

	return TRUE;
}

bool
dhd_bus_aspm_enable_rc_ep(dhd_bus_t *bus, bool enable)
{
	bool ret;

	if (!bus->rc_ep_aspm_cap) {
		DHD_ERROR(("%s: NOT ASPM  CAPABLE rc_ep_aspm_cap: %d\n",
			__FUNCTION__, bus->rc_ep_aspm_cap));
		return FALSE;
	}

	if (enable) {
		/* Enable only L1 ASPM first RC then EP */
		ret = dhd_bus_aspm_enable_dev(bus, bus->rc_dev, enable);
		ret = dhd_bus_aspm_enable_dev(bus, bus->dev, enable);
	} else {
		/* Disable complete ASPM first EP then RC */
		ret = dhd_bus_aspm_enable_dev(bus, bus->dev, enable);
		ret = dhd_bus_aspm_enable_dev(bus, bus->rc_dev, enable);
	}

	return ret;
}

static void
dhd_bus_l1ss_enable_dev(dhd_bus_t *bus, struct pci_dev *dev, bool enable)
{
	uint32 l1ssctrl_before;
	uint32 l1ssctrl_after = 0;
	uint8 l1ss_ep;
	char *device;

	device = (dev == bus->dev) ? "EP" : "RC";

	/* Extendend Capacility Reg */
	l1ssctrl_before = dhdpcie_access_cap(dev, PCIE_EXTCAP_ID_L1SS,
		PCIE_EXTCAP_L1SS_CONTROL_OFFSET, TRUE, FALSE, 0);
	l1ss_ep = (l1ssctrl_before & PCIE_EXT_L1SS_MASK);

	if (enable) {
		if (l1ss_ep == PCIE_EXT_L1SS_ENAB) {
			DHD_ERROR(("%s: %s already enabled,  l1ssctrl: 0x%x\n",
				__FUNCTION__, device, l1ssctrl_before));
			return;
		}
		dhdpcie_access_cap(dev, PCIE_EXTCAP_ID_L1SS, PCIE_EXTCAP_L1SS_CONTROL_OFFSET,
			TRUE, TRUE, (l1ssctrl_before | PCIE_EXT_L1SS_ENAB));
	} else {
		if (l1ss_ep == 0) {
			DHD_ERROR(("%s: %s already disabled, l1ssctrl: 0x%x\n",
				__FUNCTION__, device, l1ssctrl_before));
			return;
		}
		dhdpcie_access_cap(dev, PCIE_EXTCAP_ID_L1SS, PCIE_EXTCAP_L1SS_CONTROL_OFFSET,
			TRUE, TRUE, (l1ssctrl_before & (~PCIE_EXT_L1SS_ENAB)));
	}
	l1ssctrl_after = dhdpcie_access_cap(dev, PCIE_EXTCAP_ID_L1SS,
		PCIE_EXTCAP_L1SS_CONTROL_OFFSET, TRUE, FALSE, 0);
	DHD_ERROR(("%s: %s %s, l1ssctrl_before: 0x%x l1ssctrl_after: 0x%x\n",
		__FUNCTION__, device, (enable ? "ENABLE " : "DISABLE"),
		l1ssctrl_before, l1ssctrl_after));

}

static bool
dhd_bus_is_rc_ep_l1ss_capable(dhd_bus_t *bus)
{
	uint32 rc_l1ss_cap;
	uint32 ep_l1ss_cap;

	/* RC Extendend Capacility */
	rc_l1ss_cap = dhdpcie_access_cap(bus->rc_dev, PCIE_EXTCAP_ID_L1SS,
		PCIE_EXTCAP_L1SS_CONTROL_OFFSET, TRUE, FALSE, 0);
	if (rc_l1ss_cap == BCME_ERROR) {
		DHD_ERROR(("%s RC is not l1ss capable\n", __FUNCTION__));
		return FALSE;
	}

	/* EP Extendend Capacility */
	ep_l1ss_cap = dhdpcie_access_cap(bus->dev, PCIE_EXTCAP_ID_L1SS,
		PCIE_EXTCAP_L1SS_CONTROL_OFFSET, TRUE, FALSE, 0);
	if (ep_l1ss_cap == BCME_ERROR) {
		DHD_ERROR(("%s EP is not l1ss capable\n", __FUNCTION__));
		return FALSE;
	}

	return TRUE;
}

void
dhd_bus_l1ss_enable_rc_ep(dhd_bus_t *bus, bool enable)
{
	bool ret;

	if ((!bus->rc_ep_aspm_cap) || (!bus->rc_ep_l1ss_cap)) {
		DHD_ERROR(("%s: NOT L1SS CAPABLE rc_ep_aspm_cap: %d rc_ep_l1ss_cap: %d\n",
			__FUNCTION__, bus->rc_ep_aspm_cap, bus->rc_ep_l1ss_cap));
		return;
	}

	/* Disable ASPM of RC and EP */
	ret = dhd_bus_aspm_enable_rc_ep(bus, FALSE);

	if (enable) {
		/* Enable RC then EP */
		dhd_bus_l1ss_enable_dev(bus, bus->rc_dev, enable);
		dhd_bus_l1ss_enable_dev(bus, bus->dev, enable);
	} else {
		/* Disable EP then RC */
		dhd_bus_l1ss_enable_dev(bus, bus->dev, enable);
		dhd_bus_l1ss_enable_dev(bus, bus->rc_dev, enable);
	}

	/* Enable ASPM of RC and EP only if this API disabled */
	if (ret == TRUE) {
		dhd_bus_aspm_enable_rc_ep(bus, TRUE);
	}
}

void
dhd_bus_aer_config(dhd_bus_t *bus)
{
	uint32 val;

	DHD_ERROR(("%s: Configure AER registers for EP\n", __FUNCTION__));
	val = dhdpcie_ep_access_cap(bus, PCIE_ADVERRREP_CAPID,
		PCIE_ADV_CORR_ERR_MASK_OFFSET, TRUE, FALSE, 0);
	if (val != (uint32)-1) {
		val &= ~CORR_ERR_AE;
		dhdpcie_ep_access_cap(bus, PCIE_ADVERRREP_CAPID,
			PCIE_ADV_CORR_ERR_MASK_OFFSET, TRUE, TRUE, val);
	} else {
		DHD_ERROR(("%s: Invalid EP's PCIE_ADV_CORR_ERR_MASK: 0x%x\n",
			__FUNCTION__, val));
	}

	DHD_ERROR(("%s: Configure AER registers for RC\n", __FUNCTION__));
	val = dhdpcie_rc_access_cap(bus, PCIE_ADVERRREP_CAPID,
		PCIE_ADV_CORR_ERR_MASK_OFFSET, TRUE, FALSE, 0);
	if (val != (uint32)-1) {
		val &= ~CORR_ERR_AE;
		dhdpcie_rc_access_cap(bus, PCIE_ADVERRREP_CAPID,
			PCIE_ADV_CORR_ERR_MASK_OFFSET, TRUE, TRUE, val);
	} else {
		DHD_ERROR(("%s: Invalid RC's PCIE_ADV_CORR_ERR_MASK: 0x%x\n",
			__FUNCTION__, val));
	}
}

#ifdef DHD_PCIE_RUNTIMEPM
static int dhdpcie_pm_suspend(struct device *dev)
{
	int ret = 0;
	struct pci_dev *pdev = to_pci_dev(dev);
	dhdpcie_info_t *pch = pci_get_drvdata(pdev);
	dhd_bus_t *bus = NULL;
	unsigned long flags;

	if (pch) {
		bus = pch->bus;
	}
	if (!bus) {
		return ret;
	}

	DHD_GENERAL_LOCK(bus->dhd, flags);
	if (!DHD_BUS_BUSY_CHECK_IDLE(bus->dhd)) {
		DHD_ERROR(("%s: Bus not IDLE!! dhd_bus_busy_state = 0x%x\n",
			__FUNCTION__, bus->dhd->dhd_bus_busy_state));
		DHD_GENERAL_UNLOCK(bus->dhd, flags);
		return -EBUSY;
	}
	DHD_BUS_BUSY_SET_SUSPEND_IN_PROGRESS(bus->dhd);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);

	if (!bus->dhd->dongle_reset)
		ret = dhdpcie_set_suspend_resume(bus, TRUE);

	DHD_GENERAL_LOCK(bus->dhd, flags);
	DHD_BUS_BUSY_CLEAR_SUSPEND_IN_PROGRESS(bus->dhd);
	dhd_os_busbusy_wake(bus->dhd);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);

	return ret;

}

static int dhdpcie_pm_prepare(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	dhdpcie_info_t *pch = pci_get_drvdata(pdev);
	dhd_bus_t *bus = NULL;

	if (!pch || !pch->bus) {
		return 0;
	}

	bus = pch->bus;
	DHD_DISABLE_RUNTIME_PM(bus->dhd);
	bus->chk_pm = TRUE;

	return 0;
}

static int dhdpcie_pm_resume(struct device *dev)
{
	int ret = 0;
	struct pci_dev *pdev = to_pci_dev(dev);
	dhdpcie_info_t *pch = pci_get_drvdata(pdev);
	dhd_bus_t *bus = NULL;
	unsigned long flags;

	if (pch) {
		bus = pch->bus;
	}
	if (!bus) {
		return ret;
	}

	DHD_GENERAL_LOCK(bus->dhd, flags);
	DHD_BUS_BUSY_SET_RESUME_IN_PROGRESS(bus->dhd);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);

	if (!bus->dhd->dongle_reset)
		ret = dhdpcie_set_suspend_resume(bus, FALSE);

	DHD_GENERAL_LOCK(bus->dhd, flags);
	DHD_BUS_BUSY_CLEAR_RESUME_IN_PROGRESS(bus->dhd);
	dhd_os_busbusy_wake(bus->dhd);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);

	return ret;
}

static void dhdpcie_pm_complete(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	dhdpcie_info_t *pch = pci_get_drvdata(pdev);
	dhd_bus_t *bus = NULL;

	if (!pch || !pch->bus) {
		return;
	}

	bus = pch->bus;
	DHD_ENABLE_RUNTIME_PM(bus->dhd);
	bus->chk_pm = FALSE;

	return;
}
#else
static int dhdpcie_pci_suspend(struct pci_dev * pdev, pm_message_t state)
{
	int ret = 0;
	dhdpcie_info_t *pch = pci_get_drvdata(pdev);
	dhd_bus_t *bus = NULL;
	unsigned long flags;

	if (pch) {
		bus = pch->bus;
	}
	if (!bus) {
		return ret;
	}

	BCM_REFERENCE(state);

	DHD_GENERAL_LOCK(bus->dhd, flags);
	if (!DHD_BUS_BUSY_CHECK_IDLE(bus->dhd)) {
		DHD_ERROR(("%s: Bus not IDLE!! dhd_bus_busy_state = 0x%x\n",
			__FUNCTION__, bus->dhd->dhd_bus_busy_state));
		DHD_GENERAL_UNLOCK(bus->dhd, flags);
		return -EBUSY;
	}
	DHD_BUS_BUSY_SET_SUSPEND_IN_PROGRESS(bus->dhd);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);

	if (!bus->dhd->dongle_reset)
		ret = dhdpcie_set_suspend_resume(bus, TRUE);

	DHD_GENERAL_LOCK(bus->dhd, flags);
	DHD_BUS_BUSY_CLEAR_SUSPEND_IN_PROGRESS(bus->dhd);
	dhd_os_busbusy_wake(bus->dhd);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);

	return ret;
}

static int dhdpcie_pci_resume(struct pci_dev *pdev)
{
	int ret = 0;
	dhdpcie_info_t *pch = pci_get_drvdata(pdev);
	dhd_bus_t *bus = NULL;
	unsigned long flags;

	if (pch) {
		bus = pch->bus;
	}
	if (!bus) {
		return ret;
	}

	DHD_GENERAL_LOCK(bus->dhd, flags);
	DHD_BUS_BUSY_SET_RESUME_IN_PROGRESS(bus->dhd);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);

	if (!bus->dhd->dongle_reset)
		ret = dhdpcie_set_suspend_resume(bus, FALSE);

	DHD_GENERAL_LOCK(bus->dhd, flags);
	DHD_BUS_BUSY_CLEAR_RESUME_IN_PROGRESS(bus->dhd);
	dhd_os_busbusy_wake(bus->dhd);
	DHD_GENERAL_UNLOCK(bus->dhd, flags);

	return ret;
}

#endif /* DHD_PCIE_RUNTIMEPM */
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
static int dhdpcie_set_suspend_resume(dhd_bus_t *bus, bool state, bool byint)
#else
static int dhdpcie_set_suspend_resume(dhd_bus_t *bus, bool state)
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
{
	int ret = 0;

	ASSERT(bus && !bus->dhd->dongle_reset);

#ifdef DHD_PCIE_RUNTIMEPM
		/* if wakelock is held during suspend, return failed */
		if (state == TRUE && dhd_os_check_wakelock_all(bus->dhd)) {
			return -EBUSY;
		}
		mutex_lock(&bus->pm_lock);
#endif /* DHD_PCIE_RUNTIMEPM */

	/* When firmware is not loaded do the PCI bus */
	/* suspend/resume only */
	if (bus->dhd->busstate == DHD_BUS_DOWN) {
		ret = dhdpcie_pci_suspend_resume(bus, state);
#ifdef DHD_PCIE_RUNTIMEPM
		mutex_unlock(&bus->pm_lock);
#endif /* DHD_PCIE_RUNTIMEPM */
		return ret;
	}
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
		ret = dhdpcie_bus_suspend(bus, state, byint);
#else
		ret = dhdpcie_bus_suspend(bus, state);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

#ifdef DHD_PCIE_RUNTIMEPM
		mutex_unlock(&bus->pm_lock);
#endif /* DHD_PCIE_RUNTIMEPM */

	return ret;
}

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
static int dhdpcie_pm_runtime_suspend(struct device * dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	dhdpcie_info_t *pch = pci_get_drvdata(pdev);
	dhd_bus_t *bus = NULL;
	int ret = 0;

	if (!pch)
		return -EBUSY;

	bus = pch->bus;

	DHD_RPM(("%s Enter\n", __FUNCTION__));

	if (atomic_read(&bus->dhd->block_bus))
		return -EHOSTDOWN;

	dhd_netif_stop_queue(bus);
	atomic_set(&bus->dhd->block_bus, TRUE);

	if (dhdpcie_set_suspend_resume(pdev, TRUE, TRUE)) {
		pm_runtime_mark_last_busy(dev);
		ret = -EAGAIN;
	}

	atomic_set(&bus->dhd->block_bus, FALSE);
	dhd_bus_start_queue(bus);

	return ret;
}

static int dhdpcie_pm_runtime_resume(struct device * dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	dhdpcie_info_t *pch = pci_get_drvdata(pdev);
	dhd_bus_t *bus = pch->bus;

	DHD_RPM(("%s Enter\n", __FUNCTION__));

	if (atomic_read(&bus->dhd->block_bus))
		return -EHOSTDOWN;

	if (dhdpcie_set_suspend_resume(pdev, FALSE, TRUE))
		return -EAGAIN;

	return 0;
}

static int dhdpcie_pm_system_suspend_noirq(struct device * dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	dhdpcie_info_t *pch = pci_get_drvdata(pdev);
	dhd_bus_t *bus = NULL;
	int ret;

	DHD_RPM(("%s Enter\n", __FUNCTION__));

	if (!pch)
		return -EBUSY;

	bus = pch->bus;

	if (atomic_read(&bus->dhd->block_bus))
		return -EHOSTDOWN;

	dhd_netif_stop_queue(bus);
	atomic_set(&bus->dhd->block_bus, TRUE);

	ret = dhdpcie_set_suspend_resume(pdev, TRUE, FALSE);

	if (ret) {
		dhd_bus_start_queue(bus);
		atomic_set(&bus->dhd->block_bus, FALSE);
	}

	return ret;
}

static int dhdpcie_pm_system_resume_noirq(struct device * dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	dhdpcie_info_t *pch = pci_get_drvdata(pdev);
	dhd_bus_t *bus = NULL;
	int ret;

	if (!pch)
		return -EBUSY;

	bus = pch->bus;

	DHD_RPM(("%s Enter\n", __FUNCTION__));

	ret = dhdpcie_set_suspend_resume(pdev, FALSE, FALSE);

	atomic_set(&bus->dhd->block_bus, FALSE);
	dhd_bus_start_queue(bus);
	pm_runtime_mark_last_busy(dhd_bus_to_dev(bus));

	return ret;
}
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

#if defined(OEM_ANDROID) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
extern void dhd_dpc_tasklet_kill(dhd_pub_t *dhdp);
#endif /* OEM_ANDROID && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

static void
dhdpcie_suspend_dump_cfgregs(struct dhd_bus *bus, char *suspend_state)
{
	DHD_ERROR(("%s: BaseAddress0(0x%x)=0x%x, "
		"BaseAddress1(0x%x)=0x%x PCIE_CFG_PMCSR(0x%x)=0x%x\n",
		suspend_state,
		PCIECFGREG_BASEADDR0,
		dhd_pcie_config_read(bus->osh,
			PCIECFGREG_BASEADDR0, sizeof(uint32)),
		PCIECFGREG_BASEADDR1,
		dhd_pcie_config_read(bus->osh,
			PCIECFGREG_BASEADDR1, sizeof(uint32)),
		PCIE_CFG_PMCSR,
		dhd_pcie_config_read(bus->osh,
			PCIE_CFG_PMCSR, sizeof(uint32))));
}

static int dhdpcie_suspend_dev(struct pci_dev *dev)
{
	int ret;
	dhdpcie_info_t *pch = pci_get_drvdata(dev);
	dhd_bus_t *bus = pch->bus;

#if defined(OEM_ANDROID) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	if (bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link is down\n", __FUNCTION__));
		return BCME_ERROR;
	}
#endif /* OEM_ANDROID && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
	DHD_ERROR(("%s: Enter\n", __FUNCTION__));
	dhdpcie_suspend_dump_cfgregs(bus, "BEFORE_EP_SUSPEND");
#if defined(OEM_ANDROID) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	dhd_dpc_tasklet_kill(bus->dhd);
#endif /* OEM_ANDROID && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
	pci_save_state(dev);
#if defined(OEM_ANDROID) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	pch->state = pci_store_saved_state(dev);
#endif /* OEM_ANDROID && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
	pci_enable_wake(dev, PCI_D0, TRUE);
	if (pci_is_enabled(dev))
		pci_disable_device(dev);

	ret = pci_set_power_state(dev, PCI_D3hot);
	if (ret) {
		DHD_ERROR(("%s: pci_set_power_state error %d\n",
			__FUNCTION__, ret));
	}
#ifdef OEM_ANDROID
	dev->state_saved = FALSE;
#endif /* OEM_ANDROID */
	dhdpcie_suspend_dump_cfgregs(bus, "AFTER_EP_SUSPEND");
	return ret;
}

#ifdef DHD_WAKE_STATUS
int bcmpcie_get_total_wake(struct dhd_bus *bus)
{
	dhdpcie_info_t *pch = pci_get_drvdata(bus->dev);

	return pch->total_wake_count;
}

int bcmpcie_set_get_wake(struct dhd_bus *bus, int flag)
{
	dhdpcie_info_t *pch = pci_get_drvdata(bus->dev);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pch->pcie_lock, flags);

	ret = pch->pkt_wake;
	pch->total_wake_count += flag;
	pch->pkt_wake = flag;

	spin_unlock_irqrestore(&pch->pcie_lock, flags);
	return ret;
}
#endif /* DHD_WAKE_STATUS */

static int dhdpcie_resume_dev(struct pci_dev *dev)
{
	int err = 0;
	dhdpcie_info_t *pch = pci_get_drvdata(dev);
#if defined(OEM_ANDROID) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	pci_load_and_free_saved_state(dev, &pch->state);
#endif /* OEM_ANDROID && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
	DHD_ERROR(("%s: Enter\n", __FUNCTION__));
#ifdef OEM_ANDROID
	dev->state_saved = TRUE;
#endif /* OEM_ANDROID */
	pci_restore_state(dev);
#ifdef FORCE_TPOWERON
	if (dhdpcie_chip_req_forced_tpoweron(pch->bus)) {
		dhd_bus_set_tpoweron(pch->bus, tpoweron_scale);
	}
#endif /* FORCE_TPOWERON */
	err = pci_enable_device(dev);
	if (err) {
		printf("%s:pci_enable_device error %d \n", __FUNCTION__, err);
		goto out;
	}
	pci_set_master(dev);
	err = pci_set_power_state(dev, PCI_D0);
	if (err) {
		printf("%s:pci_set_power_state error %d \n", __FUNCTION__, err);
		goto out;
	}
	BCM_REFERENCE(pch);
	dhdpcie_suspend_dump_cfgregs(pch->bus, "AFTER_EP_RESUME");
out:
	return err;
}

static int dhdpcie_resume_host_dev(dhd_bus_t *bus)
{
	int bcmerror = 0;
#ifdef USE_EXYNOS_PCIE_RC_PMPATCH
	bcmerror = exynos_pcie_pm_resume(SAMSUNG_PCIE_CH_NUM);
#endif /* USE_EXYNOS_PCIE_RC_PMPATCH */
#ifdef CONFIG_ARCH_MSM
	bcmerror = dhdpcie_start_host_pcieclock(bus);
#endif /* CONFIG_ARCH_MSM */
#ifdef CONFIG_ARCH_TEGRA
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0))
	bcmerror = tegra_pcie_pm_resume();
#endif // endif
#endif /* CONFIG_ARCH_TEGRA */
	if (bcmerror < 0) {
		DHD_ERROR(("%s: PCIe RC resume failed!!! (%d)\n",
			__FUNCTION__, bcmerror));
		bus->is_linkdown = 1;
#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
		bus->no_cfg_restore = 1;
#endif /* CONFIG_ARCH_MSM */
#endif /* SUPPORT_LINKDOWN_RECOVERY */
	}

	return bcmerror;
}

static int dhdpcie_suspend_host_dev(dhd_bus_t *bus)
{
	int bcmerror = 0;
#ifdef USE_EXYNOS_PCIE_RC_PMPATCH
	if (bus->rc_dev) {
		pci_save_state(bus->rc_dev);
	} else {
		DHD_ERROR(("%s: RC %x:%x handle is NULL\n",
			__FUNCTION__, PCIE_RC_VENDOR_ID, PCIE_RC_DEVICE_ID));
	}
	exynos_pcie_pm_suspend(SAMSUNG_PCIE_CH_NUM);
#endif	/* USE_EXYNOS_PCIE_RC_PMPATCH */
#ifdef CONFIG_ARCH_MSM
	bcmerror = dhdpcie_stop_host_pcieclock(bus);
#endif	/* CONFIG_ARCH_MSM */
#ifdef CONFIG_ARCH_TEGRA
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0))
	bcmerror = tegra_pcie_pm_suspend();
#endif // endif
#endif /* CONFIG_ARCH_TEGRA */
	return bcmerror;
}

/**
 * dhdpcie_os_setbar1win
 *
 * Interface function for setting bar1 window in order to allow
 * os layer to be aware of current window positon.
 *
 * @bus: dhd bus context
 * @addr: new backplane windows address for BAR1
 */
void
dhdpcie_os_setbar1win(dhd_bus_t *bus, uint32 addr)
{
	dhdpcie_info_t *pch = pci_get_drvdata(bus->dev);

	osl_pci_write_config(bus->osh, PCI_BAR1_WIN, 4, addr);
	pch->curr_bar1_win = addr;
}

/**
 * dhdpcie_os_chkbpoffset
 *
 * Check the provided address is within the current BAR1 window,
 * if not, shift the window
 *
 * @bus: dhd bus context
 * @offset: back plane address that the caller wants to access
 *
 * Return: new offset for access
 */
static ulong
dhdpcie_os_chkbpoffset(dhdpcie_info_t *pch, ulong offset)
{
	/* Determine BAR1 backplane window using window size
	 * Window address mask should be ~(size - 1)
	 */
	uint32 bpwin = (uint32)(offset & ~(pch->bar1_size - 1));

	if (bpwin != pch->curr_bar1_win) {
		/* Move BAR1 window */
		dhdpcie_os_setbar1win(pch->bus, bpwin);
	}

	return offset - bpwin;
}

/**
 * dhdpcie os layer tcm read/write interface
 */
void
dhdpcie_os_wtcm8(dhd_bus_t *bus, ulong offset, uint8 data)
{
	dhdpcie_info_t *pch = pci_get_drvdata(bus->dev);

	offset = dhdpcie_os_chkbpoffset(pch, offset);
	W_REG(bus->dhd->osh, (volatile uint8 *)(bus->tcm + offset), data);
}

uint8
dhdpcie_os_rtcm8(dhd_bus_t *bus, ulong offset)
{
	volatile uint8 data;
	dhdpcie_info_t *pch = pci_get_drvdata(bus->dev);

	offset = dhdpcie_os_chkbpoffset(pch, offset);
	data = R_REG(bus->dhd->osh, (volatile uint8 *)(bus->tcm + offset));
	return data;
}

void
dhdpcie_os_wtcm16(dhd_bus_t *bus, ulong offset, uint16 data)
{
	dhdpcie_info_t *pch = pci_get_drvdata(bus->dev);

	offset = dhdpcie_os_chkbpoffset(pch, offset);
	W_REG(bus->dhd->osh, (volatile uint16 *)(bus->tcm + offset), data);
}

uint16
dhdpcie_os_rtcm16(dhd_bus_t *bus, ulong offset)
{
	volatile uint16 data;
	dhdpcie_info_t *pch = pci_get_drvdata(bus->dev);

	offset = dhdpcie_os_chkbpoffset(pch, offset);
	data = R_REG(bus->dhd->osh, (volatile uint16 *)(bus->tcm + offset));
	return data;
}

void
dhdpcie_os_wtcm32(dhd_bus_t *bus, ulong offset, uint32 data)
{
	dhdpcie_info_t *pch = pci_get_drvdata(bus->dev);

	offset = dhdpcie_os_chkbpoffset(pch, offset);
	W_REG(bus->dhd->osh, (volatile uint32 *)(bus->tcm + offset), data);
}

uint32
dhdpcie_os_rtcm32(dhd_bus_t *bus, ulong offset)
{
	volatile uint32 data;
	dhdpcie_info_t *pch = pci_get_drvdata(bus->dev);

	offset = dhdpcie_os_chkbpoffset(pch, offset);
	data = R_REG(bus->dhd->osh, (volatile uint32 *)(bus->tcm + offset));
	return data;
}

#ifdef DHD_SUPPORT_64BIT
void
dhdpcie_os_wtcm64(dhd_bus_t *bus, ulong offset, uint64 data)
{
	dhdpcie_info_t *pch = pci_get_drvdata(bus->dev);

	offset = dhdpcie_os_chkbpoffset(pch, offset);
	W_REG(bus->dhd->osh, (volatile uint64 *)(bus->tcm + offset), data);
}

uint64
dhdpcie_os_rtcm64(dhd_bus_t *bus, ulong offset)
{
	volatile uint64 data;
	dhdpcie_info_t *pch = pci_get_drvdata(bus->dev);

	offset = dhdpcie_os_chkbpoffset(pch, offset);
	data = R_REG(bus->dhd->osh, (volatile uint64 *)(bus->tcm + offset));
	return data;
}
#endif /* DHD_SUPPORT_64BIT */

uint32
dhdpcie_rc_config_read(dhd_bus_t *bus, uint offset)
{
	uint val = -1; /* Initialise to 0xfffffff */
	if (bus->rc_dev) {
		pci_read_config_dword(bus->rc_dev, offset, &val);
		OSL_DELAY(100);
	} else {
		DHD_ERROR(("%s: RC %x:%x handle is NULL\n",
			__FUNCTION__, PCIE_RC_VENDOR_ID, PCIE_RC_DEVICE_ID));
	}
	DHD_ERROR(("%s: RC %x:%x offset 0x%x val 0x%x\n",
		__FUNCTION__, PCIE_RC_VENDOR_ID, PCIE_RC_DEVICE_ID, offset, val));
	return (val);
}

/*
 * Reads/ Writes the value of capability register
 * from the given CAP_ID section of PCI Root Port
 *
 * Arguements
 * @bus current dhd_bus_t pointer
 * @cap Capability or Extended Capability ID to get
 * @offset offset of Register to Read
 * @is_ext TRUE if @cap is given for Extended Capability
 * @is_write is set to TRUE to indicate write
 * @val value to write
 *
 * Return Value
 * Returns 0xffffffff on error
 * on write success returns BCME_OK (0)
 * on Read Success returns the value of register requested
 * Note: caller shoud ensure valid capability ID and Ext. Capability ID.
 */

uint32
dhdpcie_access_cap(struct pci_dev *pdev, int cap, uint offset, bool is_ext, bool is_write,
	uint32 writeval)
{
	int cap_ptr = 0;
	uint32 ret = -1;
	uint32 readval;

	if (!(pdev)) {
		DHD_ERROR(("%s: pdev is NULL\n", __FUNCTION__));
		return ret;
	}

	/* Find Capability offset */
	if (is_ext) {
		/* removing max EXT_CAP_ID check as
		 * linux kernel definition's max value is not upadted yet as per spec
		 */
		cap_ptr = pci_find_ext_capability(pdev, cap);

	} else {
		/* removing max PCI_CAP_ID_MAX check as
		 * pervious kernel versions dont have this definition
		 */
		cap_ptr = pci_find_capability(pdev, cap);
	}

	/* Return if capability with given ID not found */
	if (cap_ptr == 0) {
		DHD_ERROR(("%s: PCI Cap(0x%02x) not supported.\n",
			__FUNCTION__, cap));
		return BCME_ERROR;
	}

	if (is_write) {
		pci_write_config_dword(pdev, (cap_ptr + offset), writeval);
		ret = BCME_OK;

	} else {

		pci_read_config_dword(pdev, (cap_ptr + offset), &readval);
		ret = readval;
	}

	return ret;
}

uint32
dhdpcie_rc_access_cap(dhd_bus_t *bus, int cap, uint offset, bool is_ext, bool is_write,
	uint32 writeval)
{
	if (!(bus->rc_dev)) {
		DHD_ERROR(("%s: RC %x:%x handle is NULL\n",
			__FUNCTION__, PCIE_RC_VENDOR_ID, PCIE_RC_DEVICE_ID));
		return BCME_ERROR;
	}

	return dhdpcie_access_cap(bus->rc_dev, cap, offset, is_ext, is_write, writeval);
}

uint32
dhdpcie_ep_access_cap(dhd_bus_t *bus, int cap, uint offset, bool is_ext, bool is_write,
	uint32 writeval)
{
	if (!(bus->dev)) {
		DHD_ERROR(("%s: EP handle is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	return dhdpcie_access_cap(bus->dev, cap, offset, is_ext, is_write, writeval);
}

/* API wrapper to read Root Port link capability
 * Returns 2 = GEN2 1 = GEN1 BCME_ERR on linkcap not found
 */

uint32 dhd_debug_get_rc_linkcap(dhd_bus_t *bus)
{
	uint32 linkcap = -1;
	linkcap = dhdpcie_rc_access_cap(bus, PCIE_CAP_ID_EXP,
			PCIE_CAP_LINKCAP_OFFSET, FALSE, FALSE, 0);
	linkcap &= PCIE_CAP_LINKCAP_LNKSPEED_MASK;
	return linkcap;
}

static void dhdpcie_config_save_restore_coherent(dhd_bus_t *bus, bool state)
{
	if (bus->coreid == ARMCA7_CORE_ID) {
		if (state) {
			/* Sleep */
			bus->coherent_state = dhdpcie_bus_cfg_read_dword(bus,
				PCIE_CFG_SUBSYSTEM_CONTROL, 4) & PCIE_BARCOHERENTACCEN_MASK;
		} else {
			uint32 val = (dhdpcie_bus_cfg_read_dword(bus, PCIE_CFG_SUBSYSTEM_CONTROL,
				4) & ~PCIE_BARCOHERENTACCEN_MASK) | bus->coherent_state;
			dhdpcie_bus_cfg_write_dword(bus, PCIE_CFG_SUBSYSTEM_CONTROL, 4, val);
		}
	}
}

int dhdpcie_pci_suspend_resume(dhd_bus_t *bus, bool state)
{
	int rc;

	struct pci_dev *dev = bus->dev;

	if (state) {
		dhdpcie_config_save_restore_coherent(bus, state);
#if !defined(BCMPCIE_OOB_HOST_WAKE)
		dhdpcie_pme_active(bus->osh, state);
#endif // endif
		rc = dhdpcie_suspend_dev(dev);
		if (!rc) {
			dhdpcie_suspend_host_dev(bus);
		}
	} else {
		rc = dhdpcie_resume_host_dev(bus);
		if (!rc) {
			rc = dhdpcie_resume_dev(dev);
			if (PCIECTO_ENAB(bus)) {
				/* reinit CTO configuration
				 * because cfg space got reset at D3 (PERST)
				 */
				dhdpcie_cto_cfg_init(bus, TRUE);
			}
			if (PCIE_ENUM_RESET_WAR_ENAB(bus->sih->buscorerev)) {
				dhdpcie_ssreset_dis_enum_rst(bus);
			}
#if !defined(BCMPCIE_OOB_HOST_WAKE)
			dhdpcie_pme_active(bus->osh, state);
#endif // endif
		}
		dhdpcie_config_save_restore_coherent(bus, state);
#if defined(OEM_ANDROID)
#if defined(DHD_HANG_SEND_UP_TEST)
		if (bus->is_linkdown ||
			bus->dhd->req_hang_type == HANG_REASON_PCIE_RC_LINK_UP_FAIL) {
#else /* DHD_HANG_SEND_UP_TEST */
		if (bus->is_linkdown) {
#endif /* DHD_HANG_SEND_UP_TEST */
			bus->dhd->hang_reason = HANG_REASON_PCIE_RC_LINK_UP_FAIL;
			dhd_os_send_hang_message(bus->dhd);
		}
#endif /* OEM_ANDROID */
	}
	return rc;
}

static int dhdpcie_device_scan(struct device *dev, void *data)
{
	struct pci_dev *pcidev;
	int *cnt = data;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	pcidev = container_of(dev, struct pci_dev, dev);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif
	if (pcidev->vendor != VENDOR_BROADCOM && pcidev->vendor != VENDOR_CYPRESS)
		return 0;

	if (pcidev->vendor == VENDOR_CYPRESS) {
		DHD_INFO(("Found Cypress PCI device 0x%04x\n", pcidev->device));
	} else {
		DHD_INFO(("Found Broadcom PCI device 0x%04x\n", pcidev->device));
	}
	*cnt += 1;
	if (pcidev->driver && strcmp(pcidev->driver->name, dhdpcie_driver.name))
		DHD_ERROR(("Broadcom PCI Device 0x%04x has allocated with driver %s\n",
			pcidev->device, pcidev->driver->name));

	return 0;
}

int
dhdpcie_bus_register(void)
{
	int error = 0;

	if (!(error = pci_register_driver(&dhdpcie_driver))) {
		bus_for_each_dev(dhdpcie_driver.driver.bus, NULL, &error, dhdpcie_device_scan);
		if (!error) {
			DHD_ERROR(("No Broadcom PCI device enumerated!\n"));
		} else if (!dhdpcie_init_succeeded) {
			DHD_ERROR(("%s: dhdpcie initialize failed.\n", __FUNCTION__));
		} else {
			return 0;
		}

		pci_unregister_driver(&dhdpcie_driver);
		error = BCME_ERROR;
	}

	return error;
}

void
dhdpcie_bus_unregister(void)
{
	pci_unregister_driver(&dhdpcie_driver);
}

int __devinit
dhdpcie_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
#ifndef MULTI_CHIP_SUPPORT
	/* Don't enumerate more than one device */
	if (dhdpcie_init_succeeded) {
		DHD_TRACE(("%s: PCIe Enumeration is already done.\n",
		__func__));
		return -ENODEV;
	}
#endif /* MULTI_CHIP_SUPPORT */

	if (dhdpcie_chipmatch (pdev->vendor, pdev->device)) {
		DHD_ERROR(("%s: chipmatch failed!!\n", __FUNCTION__));
			return -ENODEV;
	}

	printf("PCI_PROBE:  bus %X, slot %X,vendor %X, device %X"
		"(good PCI location)\n", pdev->bus->number,
		PCI_SLOT(pdev->devfn), pdev->vendor, pdev->device);

	if (dhdpcie_init_succeeded == TRUE) {
		DHD_ERROR(("%s(): === Driver Already attached to a BRCM device === \r\n",
			__FUNCTION__));
		return -ENODEV;
	}

	if (dhdpcie_init (pdev)) {
		DHD_ERROR(("%s: PCIe Enumeration failed\n", __FUNCTION__));
		return -ENODEV;
	}

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	/*
	Since MSM PCIe RC dev usage conunt already incremented +2 even
	before dhdpcie_pci_probe() called, then we inevitably to call
	pm_runtime_put_noidle() two times to make the count start with zero.
	*/

	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

#ifdef BCMPCIE_DISABLE_ASYNC_SUSPEND
	/* disable async suspend */
	device_disable_async_suspend(&pdev->dev);
#endif /* BCMPCIE_DISABLE_ASYNC_SUSPEND */

	DHD_TRACE(("%s: PCIe Enumeration done!!\n", __FUNCTION__));
	return 0;
}

int
dhdpcie_detach(dhdpcie_info_t *pch)
{
	if (pch) {
#if defined(OEM_ANDROID) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
		if (pch->default_state) {
			pci_load_and_free_saved_state(pch->dev, &pch->default_state);
		}
#endif /* OEM_ANDROID && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
		MFREE(pch->osh, pch, sizeof(dhdpcie_info_t));
	}
	return 0;
}

void __devexit
dhdpcie_pci_remove(struct pci_dev *pdev)
{
	osl_t *osh = NULL;
	dhdpcie_info_t *pch = NULL;
	dhd_bus_t *bus = NULL;

	DHD_TRACE(("%s Enter\n", __FUNCTION__));
	pch = pci_get_drvdata(pdev);
	bus = pch->bus;
	osh = pch->osh;

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

	if (bus) {
#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
		msm_pcie_deregister_event(&bus->pcie_event);
#endif /* CONFIG_ARCH_MSM */
#ifdef EXYNOS_PCIE_LINKDOWN_RECOVERY
#if defined(CONFIG_SOC_EXYNOS8890) || defined(CONFIG_SOC_EXYNOS8895) || \
	defined(CONFIG_SOC_EXYNOS9810) || defined(CONFIG_SOC_EXYNOS9820)
		exynos_pcie_deregister_event(&bus->pcie_event);
#endif /* CONFIG_SOC_EXYNOS8890 || CONFIG_SOC_EXYNOS8895 ||
	* CONFIG_SOC_EXYNOS9810 || CONFIG_SOC_EXYNOS9820
	*/
#endif /* EXYNOS_PCIE_LINKDOWN_RECOVERY */
#endif /* SUPPORT_LINKDOWN_RECOVERY */

		bus->rc_dev = NULL;

		dhdpcie_bus_release(bus);
	}

	if (pci_is_enabled(pdev))
		pci_disable_device(pdev);
#if defined(CONFIG_ARCH_MSM)
	msm_pcie_pm_control(MSM_PCIE_SUSPEND, pdev->bus->number, pdev, NULL, 0);
#endif // endif
#ifdef BCMPCIE_OOB_HOST_WAKE
	/* pcie os info detach */
	MFREE(osh, pch->os_cxt, sizeof(dhdpcie_os_info_t));
#endif /* BCMPCIE_OOB_HOST_WAKE */
#ifdef USE_SMMU_ARCH_MSM
	/* smmu info detach */
	dhdpcie_smmu_remove(pdev, pch->smmu_cxt);
	MFREE(osh, pch->smmu_cxt, sizeof(dhdpcie_smmu_info_t));
#endif /* USE_SMMU_ARCH_MSM */
	/* pcie info detach */
	dhdpcie_detach(pch);
	/* osl detach */
	osl_detach(osh);

#if defined(BCMPCIE_OOB_HOST_WAKE) && defined(CUSTOMER_HW2) && \
	defined(CONFIG_ARCH_APQ8084)
	brcm_pcie_wake.wake_irq = NULL;
	brcm_pcie_wake.data = NULL;
#endif /* BCMPCIE_OOB_HOST_WAKE && CUSTOMR_HW2 && CONFIG_ARCH_APQ8084 */

	dhdpcie_init_succeeded = FALSE;

	DHD_TRACE(("%s Exit\n", __FUNCTION__));

	return;
}

/* Enable Linux Msi */
int
dhdpcie_enable_msi(struct pci_dev *pdev, unsigned int min_vecs, unsigned int max_vecs)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	return pci_alloc_irq_vectors(pdev, min_vecs, max_vecs, PCI_IRQ_MSI);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
	return pci_enable_msi_range(pdev, min_vecs, max_vecs);
#else
	return pci_enable_msi_block(pdev, max_vecs);
#endif // endif
}

/* Disable Linux Msi */
void
dhdpcie_disable_msi(struct pci_dev *pdev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	pci_free_irq_vectors(pdev);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
	pci_disable_msi(pdev);
#else
	pci_disable_msi(pdev);
#endif // endif
	return;
}

/* Request Linux irq */
int
dhdpcie_request_irq(dhdpcie_info_t *dhdpcie_info)
{
	dhd_bus_t *bus = dhdpcie_info->bus;
	struct pci_dev *pdev = dhdpcie_info->bus->dev;
	int host_irq_disabled;

	if (!bus->irq_registered) {
		snprintf(dhdpcie_info->pciname, sizeof(dhdpcie_info->pciname),
			"dhdpcie:%s", pci_name(pdev));

		if (bus->d2h_intr_method == PCIE_MSI) {
			if (dhdpcie_enable_msi(pdev, 1, 1) < 0) {
				DHD_ERROR(("%s: dhdpcie_enable_msi() failed\n", __FUNCTION__));
				dhdpcie_disable_msi(pdev);
				bus->d2h_intr_method = PCIE_INTX;
			}
		}

		if (request_irq(pdev->irq, dhdpcie_isr, IRQF_SHARED,
			dhdpcie_info->pciname, bus) < 0) {
			DHD_ERROR(("%s: request_irq() failed\n", __FUNCTION__));
			if (bus->d2h_intr_method == PCIE_MSI) {
				dhdpcie_disable_msi(pdev);
			}
			return -1;
		}
		else {
			bus->irq_registered = TRUE;
		}
	} else {
		DHD_ERROR(("%s: PCI IRQ is already registered\n", __FUNCTION__));
	}

	host_irq_disabled = dhdpcie_irq_disabled(bus);
	if (host_irq_disabled) {
		DHD_ERROR(("%s: PCIe IRQ was disabled(%d), so, enabled it again\n",
			__FUNCTION__, host_irq_disabled));
		dhdpcie_enable_irq(bus);
	}

	DHD_TRACE(("%s %s\n", __FUNCTION__, dhdpcie_info->pciname));

	return 0; /* SUCCESS */
}

/**
 *	dhdpcie_get_pcieirq - return pcie irq number to linux-dhd
 */
int
dhdpcie_get_pcieirq(struct dhd_bus *bus, unsigned int *irq)
{
	struct pci_dev *pdev = bus->dev;

	if (!pdev) {
		DHD_ERROR(("%s : bus->dev is NULL\n", __FUNCTION__));
		return -ENODEV;
	}

	*irq  = pdev->irq;

	return 0; /* SUCCESS */
}

#ifdef CONFIG_PHYS_ADDR_T_64BIT
#define PRINTF_RESOURCE	"0x%016llx"
#else
#define PRINTF_RESOURCE	"0x%08x"
#endif // endif

#ifdef EXYNOS_PCIE_MODULE_PATCH
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
extern struct pci_saved_state *bcm_pcie_default_state;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
#endif /* EXYNOS_MODULE_PATCH */

/*

Name:  osl_pci_get_resource

Parametrs:

1: struct pci_dev *pdev   -- pci device structure
2: pci_res                       -- structure containing pci configuration space values

Return value:

int   - Status (TRUE or FALSE)

Description:
Access PCI configuration space, retrieve  PCI allocated resources , updates in resource structure.

 */
int dhdpcie_get_resource(dhdpcie_info_t *dhdpcie_info)
{
	phys_addr_t  bar0_addr, bar1_addr;
	ulong bar1_size;
	struct pci_dev *pdev = dhdpcie_info->dev;
#if defined(CONFIG_ARCH_MSM) && !defined(ENABLE_INSMOD_NO_FW_LOAD)
	int ret;
	/* enable PCIe link */
	ret = msm_pcie_pm_control(MSM_PCIE_RESUME, pdev->bus->number,
		pdev, NULL, MSM_PCIE_CONFIG_NO_CFG_RESTORE);
	if (ret) {
		DHD_ERROR(("%s: MSM_PCIE_RESUME failed : %d\n", __FUNCTION__, ret));
		goto err;
	}
	DHD_ERROR(("PCIe:%s:enabled link\n", __FUNCTION__));
	/* recover the config space of both RC and Endpoint */
	msm_pcie_recover_config(pdev);
#endif /* CONFIG_ARCH_MSM && !ENABLE_INSMOD_NO_FW_LOAD */
#ifdef EXYNOS_PCIE_MODULE_PATCH
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	if (bcm_pcie_default_state) {
		pci_load_saved_state(pdev, bcm_pcie_default_state);
		pci_restore_state(pdev);
	}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
#endif /* EXYNOS_MODULE_PATCH */
	do {
		if (pci_enable_device(pdev)) {
			printf("%s: Cannot enable PCI device\n", __FUNCTION__);
			break;
		}
		pci_set_master(pdev);
		bar0_addr = pci_resource_start(pdev, 0);	/* Bar-0 mapped address */
		bar1_addr = pci_resource_start(pdev, 2);	/* Bar-1 mapped address */

		/* read Bar-1 mapped memory range */
		bar1_size = pci_resource_len(pdev, 2);

		if ((bar1_size == 0) || (bar1_addr == 0)) {
			printf("%s: BAR1 Not enabled for this device  size(%ld),"
				" addr(0x"PRINTF_RESOURCE")\n",
				__FUNCTION__, bar1_size, bar1_addr);
			goto err;
		}

		dhdpcie_info->regs = (volatile char *) REG_MAP(bar0_addr, DONGLE_REG_MAP_SIZE);
		dhdpcie_info->bar1_size =
			(bar1_size > DONGLE_TCM_MAP_SIZE) ? bar1_size : DONGLE_TCM_MAP_SIZE;
		dhdpcie_info->tcm = (volatile char *) REG_MAP(bar1_addr, dhdpcie_info->bar1_size);

		if (!dhdpcie_info->regs || !dhdpcie_info->tcm) {
			DHD_ERROR(("%s:ioremap() failed\n", __FUNCTION__));
			break;
		}
#ifdef EXYNOS_PCIE_MODULE_PATCH
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
		if (bcm_pcie_default_state == NULL) {
			pci_save_state(pdev);
			bcm_pcie_default_state = pci_store_saved_state(pdev);
		}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
#endif /* EXYNOS_MODULE_PATCH */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	/* Backup PCIe configuration so as to use Wi-Fi on/off process
	 * in case of built in driver
	 */
	pci_save_state(pdev);
	dhdpcie_info->default_state = pci_store_saved_state(pdev);

	if (dhdpcie_info->default_state == NULL) {
		DHD_ERROR(("%s pci_store_saved_state returns NULL\n",
			__FUNCTION__));
		REG_UNMAP(dhdpcie_info->regs);
		REG_UNMAP(dhdpcie_info->tcm);
		pci_disable_device(pdev);
		break;
	}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

		DHD_TRACE(("%s:Phys addr : reg space = %p base addr 0x"PRINTF_RESOURCE" \n",
			__FUNCTION__, dhdpcie_info->regs, bar0_addr));
		DHD_TRACE(("%s:Phys addr : tcm_space = %p base addr 0x"PRINTF_RESOURCE" \n",
			__FUNCTION__, dhdpcie_info->tcm, bar1_addr));

		return 0; /* SUCCESS  */
	} while (0);
err:
	return -1;  /* FAILURE */
}

int dhdpcie_scan_resource(dhdpcie_info_t *dhdpcie_info)
{

	DHD_TRACE(("%s: ENTER\n", __FUNCTION__));

	do {
		/* define it here only!! */
		if (dhdpcie_get_resource (dhdpcie_info)) {
			DHD_ERROR(("%s: Failed to get PCI resources\n", __FUNCTION__));
			break;
		}
		DHD_TRACE(("%s:Exit - SUCCESS \n",
			__FUNCTION__));

		return 0; /* SUCCESS */

	} while (0);

	DHD_TRACE(("%s:Exit - FAILURE \n", __FUNCTION__));

	return -1; /* FAILURE */

}

void dhdpcie_dump_resource(dhd_bus_t *bus)
{
	dhdpcie_info_t *pch;

	if (bus == NULL) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return;
	}

	if (bus->dev == NULL) {
		DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
		return;
	}

	pch = pci_get_drvdata(bus->dev);
	if (pch == NULL) {
		DHD_ERROR(("%s: pch is NULL\n", __FUNCTION__));
		return;
	}

	/* BAR0 */
	DHD_ERROR(("%s: BAR0(VA): 0x%pK, BAR0(PA): "PRINTF_RESOURCE", SIZE: %d\n",
		__FUNCTION__, pch->regs, pci_resource_start(bus->dev, 0),
		DONGLE_REG_MAP_SIZE));

	/* BAR1 */
	DHD_ERROR(("%s: BAR1(VA): 0x%pK, BAR1(PA): "PRINTF_RESOURCE", SIZE: %d\n",
		__FUNCTION__, pch->tcm, pci_resource_start(bus->dev, 2),
		pch->bar1_size));
}

#ifdef SUPPORT_LINKDOWN_RECOVERY
#if defined(CONFIG_ARCH_MSM) || (defined(EXYNOS_PCIE_LINKDOWN_RECOVERY) && \
	(defined(CONFIG_SOC_EXYNOS8890) || defined(CONFIG_SOC_EXYNOS8895) || \
	defined(CONFIG_SOC_EXYNOS9810) || defined(CONFIG_SOC_EXYNOS9820)))
void dhdpcie_linkdown_cb(struct_pcie_notify *noti)
{
	struct pci_dev *pdev = (struct pci_dev *)noti->user;
	dhdpcie_info_t *pch = NULL;

	if (pdev) {
		pch = pci_get_drvdata(pdev);
		if (pch) {
			dhd_bus_t *bus = pch->bus;
			if (bus) {
				dhd_pub_t *dhd = bus->dhd;
				if (dhd) {
					DHD_ERROR(("%s: Event HANG send up "
						"due to PCIe linkdown\n",
						__FUNCTION__));
#ifdef CONFIG_ARCH_MSM
					bus->no_cfg_restore = 1;
#endif /* CONFIG_ARCH_MSM */
					bus->is_linkdown = 1;
					DHD_OS_WAKE_LOCK(dhd);
					dhd->hang_reason = HANG_REASON_PCIE_LINK_DOWN_RC_DETECT;
					dhd_os_send_hang_message(dhd);
				}
			}
		}
	}

}
#endif /* CONFIG_ARCH_MSM || (EXYNOS_PCIE_LINKDOWN_RECOVERY &&
	* (CONFIG_SOC_EXYNOS8890 || CONFIG_SOC_EXYNOS8895 || \
	* CONFIG_SOC_EXYNOS9810 || CONFIG_SOC_EXYNOS9820))
	*/
#endif /* SUPPORT_LINKDOWN_RECOVERY */

int dhdpcie_init(struct pci_dev *pdev)
{

	osl_t 				*osh = NULL;
	dhd_bus_t 			*bus = NULL;
	dhdpcie_info_t		*dhdpcie_info =  NULL;
	wifi_adapter_info_t	*adapter = NULL;
#ifdef BCMPCIE_OOB_HOST_WAKE
	dhdpcie_os_info_t	*dhdpcie_osinfo = NULL;
#endif /* BCMPCIE_OOB_HOST_WAKE */
#ifdef USE_SMMU_ARCH_MSM
	dhdpcie_smmu_info_t	*dhdpcie_smmu_info = NULL;
#endif /* USE_SMMU_ARCH_MSM */
	int ret = 0;

	do {
		/* osl attach */
		if (!(osh = osl_attach(pdev, PCI_BUS, FALSE))) {
			DHD_ERROR(("%s: osl_attach failed\n", __FUNCTION__));
			break;
		}

		/* initialize static buffer */
		adapter = dhd_wifi_platform_get_adapter(PCI_BUS, pdev->bus->number,
			PCI_SLOT(pdev->devfn));
		if (adapter != NULL)
			DHD_ERROR(("%s: found adapter info '%s'\n", __FUNCTION__, adapter->name));
		else
			DHD_ERROR(("%s: can't find adapter info for this chip\n", __FUNCTION__));
		osl_static_mem_init(osh, adapter);

		/* Set ACP coherence flag */
		if (OSL_ACP_WAR_ENAB() || OSL_ARCH_IS_COHERENT())
			osl_flag_set(osh, OSL_ACP_COHERENCE);

		/*  allocate linux spcific pcie structure here */
		if (!(dhdpcie_info = MALLOC(osh, sizeof(dhdpcie_info_t)))) {
			DHD_ERROR(("%s: MALLOC of dhd_bus_t failed\n", __FUNCTION__));
			break;
		}
		bzero(dhdpcie_info, sizeof(dhdpcie_info_t));
		dhdpcie_info->osh = osh;
		dhdpcie_info->dev = pdev;

#ifdef BCMPCIE_OOB_HOST_WAKE
		/* allocate OS speicific structure */
		dhdpcie_osinfo = MALLOC(osh, sizeof(dhdpcie_os_info_t));
		if (dhdpcie_osinfo == NULL) {
			DHD_ERROR(("%s: MALLOC of dhdpcie_os_info_t failed\n",
				__FUNCTION__));
			break;
		}
		bzero(dhdpcie_osinfo, sizeof(dhdpcie_os_info_t));
		dhdpcie_info->os_cxt = (void *)dhdpcie_osinfo;

		/* Initialize host wake IRQ */
		spin_lock_init(&dhdpcie_osinfo->oob_irq_spinlock);
		/* Get customer specific host wake IRQ parametres: IRQ number as IRQ type */
		dhdpcie_osinfo->oob_irq_num = wifi_platform_get_irq_number(adapter,
			&dhdpcie_osinfo->oob_irq_flags);
		if (dhdpcie_osinfo->oob_irq_num < 0) {
			DHD_ERROR(("%s: Host OOB irq is not defined\n", __FUNCTION__));
		}
#endif /* BCMPCIE_OOB_HOST_WAKE */

#ifdef USE_SMMU_ARCH_MSM
		/* allocate private structure for using SMMU */
		dhdpcie_smmu_info = MALLOC(osh, sizeof(dhdpcie_smmu_info_t));
		if (dhdpcie_smmu_info == NULL) {
			DHD_ERROR(("%s: MALLOC of dhdpcie_smmu_info_t failed\n",
				__FUNCTION__));
			break;
		}
		bzero(dhdpcie_smmu_info, sizeof(dhdpcie_smmu_info_t));
		dhdpcie_info->smmu_cxt = (void *)dhdpcie_smmu_info;

		/* Initialize smmu structure */
		if (dhdpcie_smmu_init(pdev, dhdpcie_info->smmu_cxt) < 0) {
			DHD_ERROR(("%s: Failed to initialize SMMU\n",
				__FUNCTION__));
			break;
		}
#endif /* USE_SMMU_ARCH_MSM */

#ifdef DHD_WAKE_STATUS
		/* Initialize pcie_lock */
		spin_lock_init(&dhdpcie_info->pcie_lock);
#endif /* DHD_WAKE_STATUS */

		/* Find the PCI resources, verify the  */
		/* vendor and device ID, map BAR regions and irq,  update in structures */
		if (dhdpcie_scan_resource(dhdpcie_info)) {
			DHD_ERROR(("%s: dhd_Scan_PCI_Res failed\n", __FUNCTION__));

			break;
		}

		/* Bus initialization */
		ret = dhdpcie_bus_attach(osh, &bus, dhdpcie_info->regs, dhdpcie_info->tcm, pdev);
		if (ret != BCME_OK) {
			DHD_ERROR(("%s:dhdpcie_bus_attach() failed\n", __FUNCTION__));
			break;
		}

		dhdpcie_info->bus = bus;
		bus->is_linkdown = 0;
		bus->no_bus_init = FALSE;
		bus->cto_triggered = 0;

		bus->rc_dev = NULL;

		/* Get RC Device Handle */
		if (bus->dev->bus) {
			/* self member of structure pci_bus is bridge device as seen by parent */
			bus->rc_dev = bus->dev->bus->self;
			DHD_ERROR(("%s: rc_dev from dev->bus->self (%x:%x) is %pK\n", __FUNCTION__,
				bus->rc_dev->vendor, bus->rc_dev->device, bus->rc_dev));
		} else {
			DHD_ERROR(("%s: unable to get rc_dev as dev->bus is NULL\n", __FUNCTION__));
		}

		/* if rc_dev is still NULL, try to get from vendor/device IDs */
		if (bus->rc_dev == NULL) {
			bus->rc_dev = pci_get_device(PCIE_RC_VENDOR_ID, PCIE_RC_DEVICE_ID, NULL);
			DHD_ERROR(("%s: rc_dev from pci_get_device (%x:%x) is %p\n", __FUNCTION__,
				PCIE_RC_VENDOR_ID, PCIE_RC_DEVICE_ID, bus->rc_dev));
		}

		bus->rc_ep_aspm_cap = dhd_bus_is_rc_ep_aspm_capable(bus);
		bus->rc_ep_l1ss_cap = dhd_bus_is_rc_ep_l1ss_capable(bus);
		DHD_ERROR(("%s: rc_ep_aspm_cap: %d rc_ep_l1ss_cap: %d\n",
			__FUNCTION__, bus->rc_ep_aspm_cap, bus->rc_ep_l1ss_cap));
#ifdef DHD_DISABLE_ASPM
	dhd_bus_aspm_enable_rc_ep(bus, FALSE);
#endif /* DHD_DISABLE_ASPM */

#ifdef FORCE_TPOWERON
		if (dhdpcie_chip_req_forced_tpoweron(bus)) {
			dhd_bus_set_tpoweron(bus, tpoweron_scale);
		}
#endif /* FORCE_TPOWERON */

#if defined(BCMPCIE_OOB_HOST_WAKE) && defined(CUSTOMER_HW2) && \
	defined(CONFIG_ARCH_APQ8084)
		brcm_pcie_wake.wake_irq = wlan_oob_irq;
		brcm_pcie_wake.data = bus;
#endif /* BCMPCIE_OOB_HOST_WAKE && CUSTOMR_HW2 && CONFIG_ARCH_APQ8084 */

#ifdef DONGLE_ENABLE_ISOLATION
		bus->dhd->dongle_isolation = TRUE;
#endif /* DONGLE_ENABLE_ISOLATION */
#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
		bus->pcie_event.events = MSM_PCIE_EVENT_LINKDOWN;
		bus->pcie_event.user = pdev;
		bus->pcie_event.mode = MSM_PCIE_TRIGGER_CALLBACK;
		bus->pcie_event.callback = dhdpcie_linkdown_cb;
		bus->pcie_event.options = MSM_PCIE_CONFIG_NO_RECOVERY;
		msm_pcie_register_event(&bus->pcie_event);
		bus->no_cfg_restore = FALSE;
#endif /* CONFIG_ARCH_MSM */
#ifdef EXYNOS_PCIE_LINKDOWN_RECOVERY
#if defined(CONFIG_SOC_EXYNOS8890) || defined(CONFIG_SOC_EXYNOS8895) || \
	defined(CONFIG_SOC_EXYNOS9810) || defined(CONFIG_SOC_EXYNOS9820)
		bus->pcie_event.events = EXYNOS_PCIE_EVENT_LINKDOWN;
		bus->pcie_event.user = pdev;
		bus->pcie_event.mode = EXYNOS_PCIE_TRIGGER_CALLBACK;
		bus->pcie_event.callback = dhdpcie_linkdown_cb;
		exynos_pcie_register_event(&bus->pcie_event);
#endif /* CONFIG_SOC_EXYNOS8890 || CONFIG_SOC_EXYNOS8895 ||
	* CONFIG_SOC_EXYNOS9810 || CONFIG_SOC_EXYNOS9820
	*/
#endif /* EXYNOS_PCIE_LINKDOWN_RECOVERY */
		bus->read_shm_fail = FALSE;
#endif /* SUPPORT_LINKDOWN_RECOVERY */

		if (bus->intr) {
			/* Register interrupt callback, but mask it (not operational yet). */
			DHD_INTR(("%s: Registering and masking interrupts\n", __FUNCTION__));
			dhdpcie_bus_intr_disable(bus);

			if (dhdpcie_request_irq(dhdpcie_info)) {
				DHD_ERROR(("%s: request_irq() failed\n", __FUNCTION__));
				break;
			}
		} else {
			bus->pollrate = 1;
			DHD_INFO(("%s: PCIe interrupt function is NOT registered "
				"due to polling mode\n", __FUNCTION__));
		}

#if defined(BCM_REQUEST_FW)
		if (dhd_bus_download_firmware(bus, osh, NULL, NULL) < 0) {
		DHD_ERROR(("%s: failed to download firmware\n", __FUNCTION__));
		}
		bus->nv_path = NULL;
		bus->fw_path = NULL;
#endif /* BCM_REQUEST_FW */

		/* set private data for pci_dev */
		pci_set_drvdata(pdev, dhdpcie_info);

		if (dhd_download_fw_on_driverload) {
			if (dhd_bus_start(bus->dhd)) {
				DHD_ERROR(("%s: dhd_bud_start() failed\n", __FUNCTION__));
				if (!allow_delay_fwdl)
					break;
			}
		} else {
			/* Set ramdom MAC address during boot time */
			get_random_bytes(&bus->dhd->mac.octet[3], 3);
			/* Adding BRCM OUI */
			bus->dhd->mac.octet[0] = 0;
			bus->dhd->mac.octet[1] = 0x90;
			bus->dhd->mac.octet[2] = 0x4C;
		}

		/* Attach to the OS network interface */
		DHD_TRACE(("%s(): Calling dhd_register_if() \n", __FUNCTION__));
		if (dhd_attach_net(bus->dhd, TRUE)) {
			DHD_ERROR(("%s(): ERROR.. dhd_register_if() failed\n", __FUNCTION__));
			break;
		}
#ifdef WL_VIF_SUPPORT
		/* Attach to the virtual interface */
		DHD_TRACE(("%s(): Calling dhd_register_vif() \n", __FUNCTION__));
		if (dhd_register_vif(bus->dhd) != 0) {
			DHD_ERROR(("%s(): ERROR.. dhd_register_vif() failed\n", __FUNCTION__));
		}
#endif // endif

		dhdpcie_init_succeeded = TRUE;

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
		pm_runtime_set_autosuspend_delay(&pdev->dev, AUTO_SUSPEND_TIMEOUT);
		pm_runtime_use_autosuspend(&pdev->dev);
		atomic_set(&bus->dhd->block_bus, FALSE);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

		DHD_TRACE(("%s:Exit - SUCCESS \n", __FUNCTION__));
		return 0;  /* return  SUCCESS  */

	} while (0);
	/* reverse the initialization in order in case of error */

	if (bus)
		dhdpcie_bus_release(bus);

#ifdef BCMPCIE_OOB_HOST_WAKE
	if (dhdpcie_osinfo) {
		MFREE(osh, dhdpcie_osinfo, sizeof(dhdpcie_os_info_t));
	}
#endif /* BCMPCIE_OOB_HOST_WAKE */

#ifdef USE_SMMU_ARCH_MSM
	if (dhdpcie_smmu_info) {
		MFREE(osh, dhdpcie_smmu_info, sizeof(dhdpcie_smmu_info_t));
		dhdpcie_info->smmu_cxt = NULL;
	}
#endif /* USE_SMMU_ARCH_MSM */

	if (dhdpcie_info)
		dhdpcie_detach(dhdpcie_info);
	pci_disable_device(pdev);
	if (osh)
		osl_detach(osh);

	dhdpcie_init_succeeded = FALSE;

	DHD_TRACE(("%s:Exit - FAILURE \n", __FUNCTION__));

	return -1; /* return FAILURE  */
}

/* Free Linux irq */
void
dhdpcie_free_irq(dhd_bus_t *bus)
{
	struct pci_dev *pdev = NULL;

	DHD_TRACE(("%s: freeing up the IRQ\n", __FUNCTION__));
	if (bus) {
		pdev = bus->dev;
		if (bus->irq_registered) {
			free_irq(pdev->irq, bus);
			bus->irq_registered = FALSE;
			if (bus->d2h_intr_method == PCIE_MSI) {
				dhdpcie_disable_msi(pdev);
			}
		} else {
			DHD_ERROR(("%s: PCIe IRQ is not registered\n", __FUNCTION__));
		}
	}
	DHD_TRACE(("%s: Exit\n", __FUNCTION__));
	return;
}

/*

Name:  dhdpcie_isr

Parametrs:

1: IN int irq   -- interrupt vector
2: IN void *arg      -- handle to private data structure

Return value:

Status (TRUE or FALSE)

Description:
Interrupt Service routine checks for the status register,
disable interrupt and queue DPC if mail box interrupts are raised.
*/

irqreturn_t
dhdpcie_isr(int irq, void *arg)
{
	dhd_bus_t *bus = (dhd_bus_t*)arg;
	bus->isr_entry_time = OSL_LOCALTIME_NS();
	if (!dhdpcie_bus_isr(bus)) {
		DHD_LOG_MEM(("%s: dhdpcie_bus_isr returns with FALSE\n", __FUNCTION__));
	}
	bus->isr_exit_time = OSL_LOCALTIME_NS();
	return IRQ_HANDLED;
}

int
dhdpcie_disable_irq_nosync(dhd_bus_t *bus)
{
	struct pci_dev *dev;
	if ((bus == NULL) || (bus->dev == NULL)) {
		DHD_ERROR(("%s: bus or bus->dev is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	dev = bus->dev;
	disable_irq_nosync(dev->irq);
	return BCME_OK;
}

int
dhdpcie_disable_irq(dhd_bus_t *bus)
{
	struct pci_dev *dev;
	if ((bus == NULL) || (bus->dev == NULL)) {
		DHD_ERROR(("%s: bus or bus->dev is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	dev = bus->dev;
	disable_irq(dev->irq);
	return BCME_OK;
}

int
dhdpcie_enable_irq(dhd_bus_t *bus)
{
	struct pci_dev *dev;
	if ((bus == NULL) || (bus->dev == NULL)) {
		DHD_ERROR(("%s: bus or bus->dev is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	dev = bus->dev;
	enable_irq(dev->irq);
	return BCME_OK;
}

int
dhdpcie_irq_disabled(dhd_bus_t *bus)
{
	struct irq_desc *desc = irq_to_desc(bus->dev->irq);
	/* depth will be zero, if enabled */
	return desc->depth;
}

int
dhdpcie_start_host_pcieclock(dhd_bus_t *bus)
{
	int ret = 0;
#ifdef CONFIG_ARCH_MSM
#ifdef SUPPORT_LINKDOWN_RECOVERY
	int options = 0;
#endif /* SUPPORT_LINKDOWN_RECOVERY */
#endif /* CONFIG_ARCH_MSM */
	DHD_TRACE(("%s Enter:\n", __FUNCTION__));

	if (bus == NULL) {
		return BCME_ERROR;
	}

	if (bus->dev == NULL) {
		return BCME_ERROR;
	}

#ifdef CONFIG_ARCH_MSM
#ifdef SUPPORT_LINKDOWN_RECOVERY
	if (bus->no_cfg_restore) {
		options = MSM_PCIE_CONFIG_NO_CFG_RESTORE;
	}
	ret = msm_pcie_pm_control(MSM_PCIE_RESUME, bus->dev->bus->number,
		bus->dev, NULL, options);
	if (bus->no_cfg_restore && !ret) {
		msm_pcie_recover_config(bus->dev);
		bus->no_cfg_restore = 0;
	}
#else
	ret = msm_pcie_pm_control(MSM_PCIE_RESUME, bus->dev->bus->number,
		bus->dev, NULL, 0);
#endif /* SUPPORT_LINKDOWN_RECOVERY */
	if (ret) {
		DHD_ERROR(("%s Failed to bring up PCIe link\n", __FUNCTION__));
		goto done;
	}

done:
#endif /* CONFIG_ARCH_MSM */
	DHD_TRACE(("%s Exit:\n", __FUNCTION__));
	return ret;
}

int
dhdpcie_stop_host_pcieclock(dhd_bus_t *bus)
{
	int ret = 0;
#ifdef CONFIG_ARCH_MSM
#ifdef SUPPORT_LINKDOWN_RECOVERY
	int options = 0;
#endif /* SUPPORT_LINKDOWN_RECOVERY */
#endif /* CONFIG_ARCH_MSM */

	DHD_TRACE(("%s Enter:\n", __FUNCTION__));

	if (bus == NULL) {
		return BCME_ERROR;
	}

	if (bus->dev == NULL) {
		return BCME_ERROR;
	}

#ifdef CONFIG_ARCH_MSM
#ifdef SUPPORT_LINKDOWN_RECOVERY
	if (bus->no_cfg_restore) {
		options = MSM_PCIE_CONFIG_NO_CFG_RESTORE | MSM_PCIE_CONFIG_LINKDOWN;
	}

	ret = msm_pcie_pm_control(MSM_PCIE_SUSPEND, bus->dev->bus->number,
		bus->dev, NULL, options);
#else
	ret = msm_pcie_pm_control(MSM_PCIE_SUSPEND, bus->dev->bus->number,
		bus->dev, NULL, 0);
#endif /* SUPPORT_LINKDOWN_RECOVERY */
	if (ret) {
		DHD_ERROR(("Failed to stop PCIe link\n"));
		goto done;
	}
done:
#endif /* CONFIG_ARCH_MSM */
	DHD_TRACE(("%s Exit:\n", __FUNCTION__));
	return ret;
}

int
dhdpcie_disable_device(dhd_bus_t *bus)
{
	DHD_TRACE(("%s Enter:\n", __FUNCTION__));

	if (bus == NULL) {
		return BCME_ERROR;
	}

	if (bus->dev == NULL) {
		return BCME_ERROR;
	}

	if (pci_is_enabled(bus->dev))
		pci_disable_device(bus->dev);

	return 0;
}

int
dhdpcie_enable_device(dhd_bus_t *bus)
{
	int ret = BCME_ERROR;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	dhdpcie_info_t *pch;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

	DHD_TRACE(("%s Enter:\n", __FUNCTION__));

	if (bus == NULL) {
		return BCME_ERROR;
	}

	if (bus->dev == NULL) {
		return BCME_ERROR;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	pch = pci_get_drvdata(bus->dev);
	if (pch == NULL) {
		return BCME_ERROR;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)) && (LINUX_VERSION_CODE < \
	KERNEL_VERSION(3, 19, 0)) && !defined(CONFIG_SOC_EXYNOS8890)
	/* Updated with pci_load_and_free_saved_state to compatible
	 * with Kernel version 3.14.0 to 3.18.41.
	 */
	pci_load_and_free_saved_state(bus->dev, &pch->default_state);
	pch->default_state = pci_store_saved_state(bus->dev);
#else
	pci_load_saved_state(bus->dev, pch->default_state);
#endif /* LINUX_VERSION >= 3.14.0 && LINUX_VERSION < 3.19.0 && !CONFIG_SOC_EXYNOS8890 */

	/* Check if Device ID is valid */
	if (bus->dev->state_saved) {
		uint32 vid, saved_vid;
		pci_read_config_dword(bus->dev, PCI_CFG_VID, &vid);
		saved_vid = bus->dev->saved_config_space[PCI_CFG_VID];
		if (vid != saved_vid) {
			DHD_ERROR(("%s: VID(0x%x) is different from saved VID(0x%x) "
				"Skip the bus init\n", __FUNCTION__, vid, saved_vid));
			bus->no_bus_init = TRUE;
			/* Check if the PCIe link is down */
			if (vid == (uint32)-1) {
				bus->is_linkdown = 1;
#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
				bus->no_cfg_restore = TRUE;
#endif /* CONFIG_ARCH_MSM */
#endif /* SUPPORT_LINKDOWN_RECOVERY */
			}
			return BCME_ERROR;
		}
	}

	pci_restore_state(bus->dev);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) */

	ret = pci_enable_device(bus->dev);
	if (ret) {
		pci_disable_device(bus->dev);
	} else {
		pci_set_master(bus->dev);
	}

	return ret;
}

int
dhdpcie_alloc_resource(dhd_bus_t *bus)
{
	dhdpcie_info_t *dhdpcie_info;
	phys_addr_t bar0_addr, bar1_addr;
	ulong bar1_size;

	do {
		if (bus == NULL) {
			DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
			break;
		}

		if (bus->dev == NULL) {
			DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
			break;
		}

		dhdpcie_info = pci_get_drvdata(bus->dev);
		if (dhdpcie_info == NULL) {
			DHD_ERROR(("%s: dhdpcie_info is NULL\n", __FUNCTION__));
			break;
		}

		bar0_addr = pci_resource_start(bus->dev, 0);	/* Bar-0 mapped address */
		bar1_addr = pci_resource_start(bus->dev, 2);	/* Bar-1 mapped address */

		/* read Bar-1 mapped memory range */
		bar1_size = pci_resource_len(bus->dev, 2);

		if ((bar1_size == 0) || (bar1_addr == 0)) {
			printf("%s: BAR1 Not enabled for this device size(%ld),"
				" addr(0x"PRINTF_RESOURCE")\n",
				__FUNCTION__, bar1_size, bar1_addr);
			break;
		}

		dhdpcie_info->regs = (volatile char *) REG_MAP(bar0_addr, DONGLE_REG_MAP_SIZE);
		if (!dhdpcie_info->regs) {
			DHD_ERROR(("%s: ioremap() for regs is failed\n", __FUNCTION__));
			break;
		}

		bus->regs = dhdpcie_info->regs;
		dhdpcie_info->bar1_size =
			(bar1_size > DONGLE_TCM_MAP_SIZE) ? bar1_size : DONGLE_TCM_MAP_SIZE;
		dhdpcie_info->tcm = (volatile char *) REG_MAP(bar1_addr, dhdpcie_info->bar1_size);
		if (!dhdpcie_info->tcm) {
			DHD_ERROR(("%s: ioremap() for regs is failed\n", __FUNCTION__));
			REG_UNMAP(dhdpcie_info->regs);
			bus->regs = NULL;
			break;
		}

		bus->tcm = dhdpcie_info->tcm;

		DHD_TRACE(("%s:Phys addr : reg space = %p base addr 0x"PRINTF_RESOURCE" \n",
			__FUNCTION__, dhdpcie_info->regs, bar0_addr));
		DHD_TRACE(("%s:Phys addr : tcm_space = %p base addr 0x"PRINTF_RESOURCE" \n",
			__FUNCTION__, dhdpcie_info->tcm, bar1_addr));

		return 0;
	} while (0);

	return BCME_ERROR;
}

void
dhdpcie_free_resource(dhd_bus_t *bus)
{
	dhdpcie_info_t *dhdpcie_info;

	if (bus == NULL) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return;
	}

	if (bus->dev == NULL) {
		DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
		return;
	}

	dhdpcie_info = pci_get_drvdata(bus->dev);
	if (dhdpcie_info == NULL) {
		DHD_ERROR(("%s: dhdpcie_info is NULL\n", __FUNCTION__));
		return;
	}

	if (bus->regs) {
		REG_UNMAP(dhdpcie_info->regs);
		bus->regs = NULL;
	}

	if (bus->tcm) {
		REG_UNMAP(dhdpcie_info->tcm);
		bus->tcm = NULL;
	}
}

int
dhdpcie_bus_request_irq(struct dhd_bus *bus)
{
	dhdpcie_info_t *dhdpcie_info;
	int ret = 0;

	if (bus == NULL) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (bus->dev == NULL) {
		DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	dhdpcie_info = pci_get_drvdata(bus->dev);
	if (dhdpcie_info == NULL) {
		DHD_ERROR(("%s: dhdpcie_info is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (bus->intr) {
		/* Register interrupt callback, but mask it (not operational yet). */
		DHD_INTR(("%s: Registering and masking interrupts\n", __FUNCTION__));
		dhdpcie_bus_intr_disable(bus);
		ret = dhdpcie_request_irq(dhdpcie_info);
		if (ret) {
			DHD_ERROR(("%s: request_irq() failed, ret=%d\n",
				__FUNCTION__, ret));
			return ret;
		}
	}

	return ret;
}

#ifdef BCMPCIE_OOB_HOST_WAKE
#ifdef CONFIG_BCMDHD_GET_OOB_STATE
extern int dhd_get_wlan_oob_gpio(void);
#endif /* CONFIG_BCMDHD_GET_OOB_STATE */

int dhdpcie_get_oob_irq_level(void)
{
	int gpio_level;

#ifdef CONFIG_BCMDHD_GET_OOB_STATE
	gpio_level = dhd_get_wlan_oob_gpio();
#else
	gpio_level = BCME_UNSUPPORTED;
#endif /* CONFIG_BCMDHD_GET_OOB_STATE */
	return gpio_level;
}

int dhdpcie_get_oob_irq_status(struct dhd_bus *bus)
{
	dhdpcie_info_t *pch;
	dhdpcie_os_info_t *dhdpcie_osinfo;

	if (bus == NULL) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return 0;
	}

	if (bus->dev == NULL) {
		DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
		return 0;
	}

	pch = pci_get_drvdata(bus->dev);
	if (pch == NULL) {
		DHD_ERROR(("%s: pch is NULL\n", __FUNCTION__));
		return 0;
	}

	dhdpcie_osinfo = (dhdpcie_os_info_t *)pch->os_cxt;

	return dhdpcie_osinfo ? dhdpcie_osinfo->oob_irq_enabled : 0;
}

int dhdpcie_get_oob_irq_num(struct dhd_bus *bus)
{
	dhdpcie_info_t *pch;
	dhdpcie_os_info_t *dhdpcie_osinfo;

	if (bus == NULL) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return 0;
	}

	if (bus->dev == NULL) {
		DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
		return 0;
	}

	pch = pci_get_drvdata(bus->dev);
	if (pch == NULL) {
		DHD_ERROR(("%s: pch is NULL\n", __FUNCTION__));
		return 0;
	}

	dhdpcie_osinfo = (dhdpcie_os_info_t *)pch->os_cxt;

	return dhdpcie_osinfo ? dhdpcie_osinfo->oob_irq_num : 0;
}

void dhdpcie_oob_intr_set(dhd_bus_t *bus, bool enable)
{
	unsigned long flags;
	dhdpcie_info_t *pch;
	dhdpcie_os_info_t *dhdpcie_osinfo;

	if (bus == NULL) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return;
	}

	if (bus->dev == NULL) {
		DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
		return;
	}

	pch = pci_get_drvdata(bus->dev);
	if (pch == NULL) {
		DHD_ERROR(("%s: pch is NULL\n", __FUNCTION__));
		return;
	}

	dhdpcie_osinfo = (dhdpcie_os_info_t *)pch->os_cxt;
	spin_lock_irqsave(&dhdpcie_osinfo->oob_irq_spinlock, flags);
	if ((dhdpcie_osinfo->oob_irq_enabled != enable) &&
		(dhdpcie_osinfo->oob_irq_num > 0)) {
		if (enable) {
			enable_irq(dhdpcie_osinfo->oob_irq_num);
			bus->oob_intr_enable_count++;
			bus->last_oob_irq_enable_time = OSL_LOCALTIME_NS();
		} else {
			disable_irq_nosync(dhdpcie_osinfo->oob_irq_num);
			bus->oob_intr_disable_count++;
			bus->last_oob_irq_disable_time = OSL_LOCALTIME_NS();
		}
		dhdpcie_osinfo->oob_irq_enabled = enable;
	}
	spin_unlock_irqrestore(&dhdpcie_osinfo->oob_irq_spinlock, flags);
}

static irqreturn_t wlan_oob_irq(int irq, void *data)
{
	dhd_bus_t *bus;
	unsigned long flags_bus;
	DHD_TRACE(("%s: IRQ Triggered\n", __FUNCTION__));
	bus = (dhd_bus_t *)data;
	dhdpcie_oob_intr_set(bus, FALSE);
	bus->last_oob_irq_time = OSL_LOCALTIME_NS();
	bus->oob_intr_count++;
#ifdef DHD_WAKE_STATUS
#ifdef DHD_PCIE_RUNTIMEPM
	/* This condition is for avoiding counting of wake up from Runtime PM */
	if (bus->chk_pm)
#endif /* DHD_PCIE_RUNTIMPM */
	{
		bcmpcie_set_get_wake(bus, 1);
	}
#endif /* DHD_WAKE_STATUS */
#ifdef DHD_PCIE_RUNTIMEPM
	dhdpcie_runtime_bus_wake(bus->dhd, FALSE, wlan_oob_irq);
#endif /* DHD_PCIE_RUNTIMPM */
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
	dhd_bus_wakeup_work(bus->dhd);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
	DHD_BUS_LOCK(bus->bus_lock, flags_bus);
	/* Hold wakelock if bus_low_power_state is
	 * DHD_BUS_D3_INFORM_SENT OR DHD_BUS_D3_ACK_RECIEVED
	 */
	if (bus->dhd->up && bus->bus_low_power_state != DHD_BUS_NO_LOW_POWER_STATE) {
		DHD_OS_OOB_IRQ_WAKE_LOCK_TIMEOUT(bus->dhd, OOB_WAKE_LOCK_TIMEOUT);
	}
	DHD_BUS_UNLOCK(bus->bus_lock, flags_bus);
	return IRQ_HANDLED;
}

int dhdpcie_oob_intr_register(dhd_bus_t *bus)
{
	int err = 0;
	dhdpcie_info_t *pch;
	dhdpcie_os_info_t *dhdpcie_osinfo;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	if (bus == NULL) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return -EINVAL;
	}

	if (bus->dev == NULL) {
		DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
		return -EINVAL;
	}

	pch = pci_get_drvdata(bus->dev);
	if (pch == NULL) {
		DHD_ERROR(("%s: pch is NULL\n", __FUNCTION__));
		return -EINVAL;
	}

	dhdpcie_osinfo = (dhdpcie_os_info_t *)pch->os_cxt;
	if (dhdpcie_osinfo->oob_irq_registered) {
		DHD_ERROR(("%s: irq is already registered\n", __FUNCTION__));
		return -EBUSY;
	}

	if (dhdpcie_osinfo->oob_irq_num > 0) {
		DHD_INFO_HW4(("%s OOB irq=%d flags=%X \n", __FUNCTION__,
			(int)dhdpcie_osinfo->oob_irq_num,
			(int)dhdpcie_osinfo->oob_irq_flags));
		err = request_irq(dhdpcie_osinfo->oob_irq_num, wlan_oob_irq,
			dhdpcie_osinfo->oob_irq_flags, "dhdpcie_host_wake",
			bus);
		if (err) {
			DHD_ERROR(("%s: request_irq failed with %d\n",
				__FUNCTION__, err));
			return err;
		}
		err = enable_irq_wake(dhdpcie_osinfo->oob_irq_num);
		if (!err) {
			dhdpcie_osinfo->oob_irq_wake_enabled = TRUE;
		} else {
		/* On Hikey platform enable_irq_wake() is failing with error
		 * ENXIO (No such device or address). This is because the callback function
		 * irq_set_wake() is not registered in kernel, hence returning BCME_OK.
		 */
#ifdef BOARD_HIKEY
		DHD_ERROR(("%s: continue eventhough enable_irq_wake failed: %d\n",
				__FUNCTION__, err));
		err = BCME_OK;
#endif /* BOARD_HIKEY */
		}
		dhdpcie_osinfo->oob_irq_enabled = TRUE;
	}

	dhdpcie_osinfo->oob_irq_registered = TRUE;

	return err;
}

void dhdpcie_oob_intr_unregister(dhd_bus_t *bus)
{
	int err = 0;
	dhdpcie_info_t *pch;
	dhdpcie_os_info_t *dhdpcie_osinfo;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	if (bus == NULL) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return;
	}

	if (bus->dev == NULL) {
		DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
		return;
	}

	pch = pci_get_drvdata(bus->dev);
	if (pch == NULL) {
		DHD_ERROR(("%s: pch is NULL\n", __FUNCTION__));
		return;
	}

	dhdpcie_osinfo = (dhdpcie_os_info_t *)pch->os_cxt;
	if (!dhdpcie_osinfo->oob_irq_registered) {
		DHD_ERROR(("%s: irq is not registered\n", __FUNCTION__));
		return;
	}
	if (dhdpcie_osinfo->oob_irq_num > 0) {
		if (dhdpcie_osinfo->oob_irq_wake_enabled) {
			err = disable_irq_wake(dhdpcie_osinfo->oob_irq_num);
			if (!err) {
				dhdpcie_osinfo->oob_irq_wake_enabled = FALSE;
			}
		}
		if (dhdpcie_osinfo->oob_irq_enabled) {
			disable_irq(dhdpcie_osinfo->oob_irq_num);
			dhdpcie_osinfo->oob_irq_enabled = FALSE;
		}
		free_irq(dhdpcie_osinfo->oob_irq_num, bus);
	}
	dhdpcie_osinfo->oob_irq_registered = FALSE;
}
#endif /* BCMPCIE_OOB_HOST_WAKE */

#ifdef DHD_PCIE_RUNTIMEPM
bool dhd_runtimepm_state(dhd_pub_t *dhd)
{
	dhd_bus_t *bus;
	unsigned long flags;
	bus = dhd->bus;

	DHD_GENERAL_LOCK(dhd, flags);
	bus->idlecount++;

	DHD_TRACE(("%s : Enter \n", __FUNCTION__));
	if ((bus->idletime > 0) && (bus->idlecount >= bus->idletime)) {
		bus->idlecount = 0;
		if (DHD_BUS_BUSY_CHECK_IDLE(dhd) && !DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(dhd)) {
			bus->bus_wake = 0;
			DHD_BUS_BUSY_SET_RPM_SUSPEND_IN_PROGRESS(dhd);
			bus->runtime_resume_done = FALSE;
			/* stop all interface network queue. */
			dhd_bus_stop_queue(bus);
			DHD_GENERAL_UNLOCK(dhd, flags);
			DHD_ERROR(("%s: DHD Idle state!! -  idletime :%d, wdtick :%d \n",
					__FUNCTION__, bus->idletime, dhd_runtimepm_ms));
			/* RPM suspend is failed, return FALSE then re-trying */
			if (dhdpcie_set_suspend_resume(bus, TRUE)) {
				DHD_ERROR(("%s: exit with wakelock \n", __FUNCTION__));
				DHD_GENERAL_LOCK(dhd, flags);
				DHD_BUS_BUSY_CLEAR_RPM_SUSPEND_IN_PROGRESS(dhd);
				dhd_os_busbusy_wake(bus->dhd);
				bus->runtime_resume_done = TRUE;
				/* It can make stuck NET TX Queue without below */
				dhd_bus_start_queue(bus);
				DHD_GENERAL_UNLOCK(dhd, flags);
				smp_wmb();
				wake_up_interruptible(&bus->rpm_queue);
				return FALSE;
			}

			DHD_GENERAL_LOCK(dhd, flags);
			DHD_BUS_BUSY_CLEAR_RPM_SUSPEND_IN_PROGRESS(dhd);
			DHD_BUS_BUSY_SET_RPM_SUSPEND_DONE(dhd);
			/* For making sure NET TX Queue active  */
			dhd_bus_start_queue(bus);
			DHD_GENERAL_UNLOCK(dhd, flags);

			wait_event_interruptible(bus->rpm_queue, bus->bus_wake);

			DHD_GENERAL_LOCK(dhd, flags);
			DHD_BUS_BUSY_CLEAR_RPM_SUSPEND_DONE(dhd);
			DHD_BUS_BUSY_SET_RPM_RESUME_IN_PROGRESS(dhd);
			DHD_GENERAL_UNLOCK(dhd, flags);

			dhdpcie_set_suspend_resume(bus, FALSE);

			DHD_GENERAL_LOCK(dhd, flags);
			DHD_BUS_BUSY_CLEAR_RPM_RESUME_IN_PROGRESS(dhd);
			dhd_os_busbusy_wake(bus->dhd);
			/* Inform the wake up context that Resume is over */
			bus->runtime_resume_done = TRUE;
			/* For making sure NET TX Queue active  */
			dhd_bus_start_queue(bus);
			DHD_GENERAL_UNLOCK(dhd, flags);

			smp_wmb();
			wake_up_interruptible(&bus->rpm_queue);
			DHD_ERROR(("%s : runtime resume ended \n", __FUNCTION__));
			return TRUE;
		} else {
			DHD_GENERAL_UNLOCK(dhd, flags);
			/* Since one of the contexts are busy (TX, IOVAR or RX)
			 * we should not suspend
			 */
			DHD_ERROR(("%s : bus is active with dhd_bus_busy_state = 0x%x\n",
				__FUNCTION__, dhd->dhd_bus_busy_state));
			return FALSE;
		}
	}

	DHD_GENERAL_UNLOCK(dhd, flags);
	return FALSE;
} /* dhd_runtimepm_state */

/*
 * dhd_runtime_bus_wake
 *  TRUE - related with runtime pm context
 *  FALSE - It isn't invloved in runtime pm context
 */
bool dhd_runtime_bus_wake(dhd_bus_t *bus, bool wait, void *func_addr)
{
	unsigned long flags;
	bus->idlecount = 0;
	DHD_TRACE(("%s : enter\n", __FUNCTION__));
	if (bus->dhd->up == FALSE) {
		DHD_INFO(("%s : dhd is not up\n", __FUNCTION__));
		return FALSE;
	}

	DHD_GENERAL_LOCK(bus->dhd, flags);
	if (DHD_BUS_BUSY_CHECK_RPM_ALL(bus->dhd)) {
		/* Wake up RPM state thread if it is suspend in progress or suspended */
		if (DHD_BUS_BUSY_CHECK_RPM_SUSPEND_IN_PROGRESS(bus->dhd) ||
				DHD_BUS_BUSY_CHECK_RPM_SUSPEND_DONE(bus->dhd)) {
			bus->bus_wake = 1;

			DHD_GENERAL_UNLOCK(bus->dhd, flags);

			DHD_ERROR(("Runtime Resume is called in %pf\n", func_addr));
			smp_wmb();
			wake_up_interruptible(&bus->rpm_queue);
		/* No need to wake up the RPM state thread */
		} else if (DHD_BUS_BUSY_CHECK_RPM_RESUME_IN_PROGRESS(bus->dhd)) {
			DHD_GENERAL_UNLOCK(bus->dhd, flags);
		}

		/* If wait is TRUE, function with wait = TRUE will be wait in here  */
		if (wait) {
			wait_event_interruptible(bus->rpm_queue, bus->runtime_resume_done);
		} else {
			DHD_INFO(("%s: bus wakeup but no wait until resume done\n", __FUNCTION__));
		}
		/* If it is called from RPM context, it returns TRUE */
		return TRUE;
	}

	DHD_GENERAL_UNLOCK(bus->dhd, flags);

	return FALSE;
}

bool dhdpcie_runtime_bus_wake(dhd_pub_t *dhdp, bool wait, void* func_addr)
{
	dhd_bus_t *bus = dhdp->bus;
	return dhd_runtime_bus_wake(bus, wait, func_addr);
}

void dhdpcie_block_runtime_pm(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = dhdp->bus;
	bus->idletime = 0;
}

bool dhdpcie_is_resume_done(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = dhdp->bus;
	return bus->runtime_resume_done;
}
#endif /* DHD_PCIE_RUNTIMEPM */

struct device * dhd_bus_to_dev(dhd_bus_t *bus)
{
	struct pci_dev *pdev;
	pdev = bus->dev;

	if (pdev)
		return &pdev->dev;
	else
		return NULL;
}

#define KIRQ_PRINT_BUF_LEN 256

void
dhd_print_kirqstats(dhd_pub_t *dhd, unsigned int irq_num)
{
	unsigned long flags = 0;
	struct irq_desc *desc;
	int i;          /* cpu iterator */
	struct bcmstrbuf strbuf;
	char tmp_buf[KIRQ_PRINT_BUF_LEN];

	desc = irq_to_desc(irq_num);
	if (!desc) {
		DHD_ERROR(("%s : irqdesc is not found \n", __FUNCTION__));
		return;
	}
	bcm_binit(&strbuf, tmp_buf, KIRQ_PRINT_BUF_LEN);
	raw_spin_lock_irqsave(&desc->lock, flags);
	bcm_bprintf(&strbuf, "dhd irq %u:", irq_num);
	for_each_online_cpu(i)
		bcm_bprintf(&strbuf, "%10u ",
			desc->kstat_irqs ? *per_cpu_ptr(desc->kstat_irqs, i) : 0);
	if (desc->irq_data.chip) {
		if (desc->irq_data.chip->name)
			bcm_bprintf(&strbuf, " %8s", desc->irq_data.chip->name);
		else
			bcm_bprintf(&strbuf, " %8s", "-");
	} else {
		bcm_bprintf(&strbuf, " %8s", "None");
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
	if (desc->irq_data.domain)
		bcm_bprintf(&strbuf, " %d", (int)desc->irq_data.hwirq);
#ifdef CONFIG_GENERIC_IRQ_SHOW_LEVEL
	bcm_bprintf(&strbuf, " %-8s", irqd_is_level_type(&desc->irq_data) ? "Level" : "Edge");
#endif // endif
#endif /* LINUX VERSION > 3.1.0 */

	if (desc->name)
		bcm_bprintf(&strbuf, "-%-8s", desc->name);

	DHD_ERROR(("%s\n", strbuf.origbuf));
	raw_spin_unlock_irqrestore(&desc->lock, flags);
}

void
dhd_show_kirqstats(dhd_pub_t *dhd)
{
	unsigned int irq = -1;
#ifdef BCMPCIE
	dhdpcie_get_pcieirq(dhd->bus, &irq);
#endif /* BCMPCIE */
#ifdef BCMSDIO
	irq = ((wifi_adapter_info_t *)dhd->info->adapter)->irq_num;
#endif /* BCMSDIO */
	if (irq != -1) {
#ifdef BCMPCIE
		DHD_ERROR(("DUMP data kernel irq stats : \n"));
#endif /* BCMPCIE */
#ifdef BCMSDIO
		DHD_ERROR(("DUMP data/host wakeup kernel irq stats : \n"));
#endif /* BCMSDIO */
		dhd_print_kirqstats(dhd, irq);
	}
#ifdef BCMPCIE_OOB_HOST_WAKE
	irq = dhdpcie_get_oob_irq_num(dhd->bus);
	if (irq) {
		DHD_ERROR(("DUMP PCIE host wakeup kernel irq stats : \n"));
		dhd_print_kirqstats(dhd, irq);
	}
#endif /* BCMPCIE_OOB_HOST_WAKE */
}

#ifdef DHD_FW_COREDUMP
#ifdef BCMDHDX
int
dhdx_dongle_mem_dump()
{
	if (!g_dhd_bus) {
		DHD_ERROR(("%s: Bus is NULL\n", __FUNCTION__));
		return -ENODEV;
	}

	dhd_bus_dump_console_buffer(g_dhd_bus);
	dhd_prot_debug_info_print(g_dhd_bus->dhd);

	g_dhd_bus->dhd->memdump_enabled = DUMP_MEMFILE_BUGON;
	g_dhd_bus->dhd->memdump_type = DUMP_TYPE_AP_ABNORMAL_ACCESS;

#ifdef DHD_PCIE_RUNTIMEPM
	dhdpcie_runtime_bus_wake(g_dhd_bus->dhd, TRUE, __builtin_return_address(0));
#endif /* DHD_PCIE_RUNTIMEPM */

	dhd_bus_mem_dump(g_dhd_bus->dhd);
	return 0;
}
EXPORT_SYMBOL(dhdx_dongle_mem_dump);
#else
int
dhd_dongle_mem_dump(void)
{
	if (!g_dhd_bus) {
		DHD_ERROR(("%s: Bus is NULL\n", __FUNCTION__));
		return -ENODEV;
	}

	dhd_bus_dump_console_buffer(g_dhd_bus);
	dhd_prot_debug_info_print(g_dhd_bus->dhd);

	g_dhd_bus->dhd->memdump_enabled = DUMP_MEMFILE_BUGON;
	g_dhd_bus->dhd->memdump_type = DUMP_TYPE_AP_ABNORMAL_ACCESS;

#ifdef DHD_PCIE_RUNTIMEPM
	dhdpcie_runtime_bus_wake(g_dhd_bus->dhd, TRUE, __builtin_return_address(0));
#endif /* DHD_PCIE_RUNTIMEPM */

	dhd_bus_mem_dump(g_dhd_bus->dhd);
	return 0;
}
EXPORT_SYMBOL(dhd_dongle_mem_dump);
#endif /* BCMDHDX */
#endif /* DHD_FW_COREDUMP */

#ifdef BCMDHDX
bool
dhdx_bus_check_driver_up(void)
{
	dhd_bus_t *bus;
	dhd_pub_t *dhdp;
	bool isup = FALSE;

	bus = (dhd_bus_t *)g_dhd_bus;
	if (!bus) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return isup;
	}

	dhdp = bus->dhd;
	if (dhdp) {
		isup = dhdp->up;
	}

	return isup;
}
EXPORT_SYMBOL(dhdx_bus_check_driver_up);
#else
bool
dhd_bus_check_driver_up(void)
{
	dhd_bus_t *bus;
	dhd_pub_t *dhdp;
	bool isup = FALSE;

	bus = (dhd_bus_t *)g_dhd_bus;
	if (!bus) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return isup;
	}

	dhdp = bus->dhd;
	if (dhdp) {
		isup = dhdp->up;
	}

	return isup;
}
EXPORT_SYMBOL(dhd_bus_check_driver_up);
#endif /* BCMDHDX */
