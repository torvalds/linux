/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux DHD Bus Module for PCIE
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
 * $Id: dhd_pcie_linux.c 707536 2017-06-28 04:23:48Z $
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
#ifdef CONFIG_ARCH_MSM
#if defined(CONFIG_PCI_MSM) || defined(CONFIG_ARCH_MSM8996)
#include <linux/msm_pcie.h>
#else
#include <mach/msm_pcie.h>
#endif /* CONFIG_PCI_MSM */
#endif /* CONFIG_ARCH_MSM */
#ifdef PCIE_OOB
#include "ftdi_sio_external.h"
#endif /* PCIE_OOB */
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

#define OSL_PKTTAG_CLEAR(p) \
do { \
	struct sk_buff *s = (struct sk_buff *)(p); \
	ASSERT(OSL_PKTTAG_SZ == 32); \
	*(uint32 *)(&s->cb[0]) = 0; *(uint32 *)(&s->cb[4]) = 0; \
	*(uint32 *)(&s->cb[8]) = 0; *(uint32 *)(&s->cb[12]) = 0; \
	*(uint32 *)(&s->cb[16]) = 0; *(uint32 *)(&s->cb[20]) = 0; \
	*(uint32 *)(&s->cb[24]) = 0; *(uint32 *)(&s->cb[28]) = 0; \
} while (0)

#ifdef PCIE_OOB
#define HOST_WAKE 4   /* GPIO_0 (HOST_WAKE) - Output from WLAN */
#define DEVICE_WAKE 5  /* GPIO_1 (DEVICE_WAKE) - Input to WLAN */
#define BIT_WL_REG_ON 6
#define BIT_BT_REG_ON 7

int gpio_handle_val = 0;
unsigned char gpio_port = 0;
unsigned char gpio_direction = 0;
#define OOB_PORT "ttyUSB0"
#endif /* PCIE_OOB */

/* user defined data structures  */

typedef struct dhd_pc_res {
	uint32 bar0_size;
	void* bar0_addr;
	uint32 bar1_size;
	void* bar1_addr;
} pci_config_res, *pPci_config_res;

typedef bool (*dhdpcie_cb_fn_t)(void *);

typedef struct dhdpcie_info
{
	dhd_bus_t	*bus;
	osl_t 			*osh;
	struct pci_dev  *dev;		/* pci device handle */
	volatile char 	*regs;		/* pci device memory va */
	volatile char 	*tcm;		/* pci device memory va */
	uint32			tcm_size;	/* pci device memory size */
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
	timer_list_compat_t tuning_timer;
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
#if defined(CUSTOMER_HW2) && defined(CONFIG_ARCH_APQ8084)
extern struct brcm_pcie_wake brcm_pcie_wake;
#endif /* CUSTOMER_HW2 && CONFIG_ARCH_APQ8084 */
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

static int dhdpcie_set_suspend_resume(dhd_bus_t *bus, bool state);
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
static int dhdpcie_pci_suspend(struct pci_dev *dev, pm_message_t state);
static int dhdpcie_pci_resume(struct pci_dev *dev);
#endif /* DHD_PCIE_RUNTIMEPM */

static struct pci_device_id dhdpcie_pci_devid[] __devinitdata = {
	{ vendor: 0x14e4,
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

static struct pci_driver dhdpcie_driver = {
	node:		{&dhdpcie_driver.node, &dhdpcie_driver.node},
	name:		"pcieh",
	id_table:	dhdpcie_pci_devid,
	probe:		dhdpcie_pci_probe,
	remove:		dhdpcie_pci_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0))
	save_state:	NULL,
#endif
#ifdef DHD_PCIE_RUNTIMEPM
	.driver.pm = &dhd_pcie_pm_ops,
#else
	suspend:	dhdpcie_pci_suspend,
	resume:		dhdpcie_pci_resume,
#endif /* DHD_PCIE_RUNTIMEPM */
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

	if (pch) {
		bus = pch->bus;
		DHD_DISABLE_RUNTIME_PM(bus->dhd);
	}

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

	if (!bus->dhd->dongle_reset) {
		ret = dhdpcie_set_suspend_resume(bus, FALSE);
		bus->chk_pm = FALSE;
	}

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

	if (pch) {
		bus = pch->bus;
		DHD_ENABLE_RUNTIME_PM(bus->dhd);
	}

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

static int dhdpcie_set_suspend_resume(dhd_bus_t *bus, bool state)
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

		ret = dhdpcie_bus_suspend(bus, state);

#ifdef DHD_PCIE_RUNTIMEPM
		mutex_unlock(&bus->pm_lock);
#endif /* DHD_PCIE_RUNTIMEPM */

	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
extern void dhd_dpc_tasklet_kill(dhd_pub_t *dhdp);
#endif /* OEM_ANDROID && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

static int dhdpcie_suspend_dev(struct pci_dev *dev)
{
	int ret;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	dhdpcie_info_t *pch = pci_get_drvdata(dev);
	dhd_bus_t *bus = pch->bus;

	if (bus->is_linkdown) {
		DHD_ERROR(("%s: PCIe link is down\n", __FUNCTION__));
		return BCME_ERROR;
	}
#endif /* OEM_ANDROID && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
	DHD_TRACE_HW4(("%s: Enter\n", __FUNCTION__));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	dhd_dpc_tasklet_kill(bus->dhd);
#endif /* OEM_ANDROID && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
	pci_save_state(dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	pch->state = pci_store_saved_state(dev);
#endif /* OEM_ANDROID && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
	pci_enable_wake(dev, PCI_D0, TRUE);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
	if (pci_is_enabled(dev))
#endif
		pci_disable_device(dev);

	ret = pci_set_power_state(dev, PCI_D3hot);
	if (ret) {
		DHD_ERROR(("%s: pci_set_power_state error %d\n",
			__FUNCTION__, ret));
	}
//	dev->state_saved = FALSE;
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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	dhdpcie_info_t *pch = pci_get_drvdata(dev);
	pci_load_and_free_saved_state(dev, &pch->state);
#endif /* OEM_ANDROID && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
	DHD_TRACE_HW4(("%s: Enter\n", __FUNCTION__));
//	dev->state_saved = TRUE;
	pci_restore_state(dev);
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
	bcmerror = tegra_pcie_pm_resume();
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
	bcmerror = tegra_pcie_pm_suspend();
#endif /* CONFIG_ARCH_TEGRA */
	return bcmerror;
}

#if defined(PCIE_RC_VENDOR_ID) && defined(PCIE_RC_DEVICE_ID)
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
dhdpcie_rc_access_cap(dhd_bus_t *bus, int cap, uint offset, bool is_ext, bool is_write,
	uint32 writeval)
{
	int cap_ptr = 0;
	uint32 ret = -1;
	uint32 readval;

	if (!(bus->rc_dev)) {
		DHD_ERROR(("%s: RC %x:%x handle is NULL\n",
			__FUNCTION__, PCIE_RC_VENDOR_ID, PCIE_RC_DEVICE_ID));
		return ret;
	}

	/* Find Capability offset */
	if (is_ext) {
		/* removing max EXT_CAP_ID check as
		 * linux kernel definition's max value is not upadted yet as per spec
		 */
		cap_ptr = pci_find_ext_capability(bus->rc_dev, cap);

	} else {
		/* removing max PCI_CAP_ID_MAX check as
		 * pervious kernel versions dont have this definition
		 */
		cap_ptr = pci_find_capability(bus->rc_dev, cap);
	}

	/* Return if capability with given ID not found */
	if (cap_ptr == 0) {
		DHD_ERROR(("%s: RC %x:%x PCI Cap(0x%02x) not supported.\n",
			__FUNCTION__, PCIE_RC_VENDOR_ID, PCIE_RC_DEVICE_ID, cap));
		return BCME_ERROR;
	}

	if (is_write) {
		ret = pci_write_config_dword(bus->rc_dev, (cap_ptr + offset), writeval);
		if (ret) {
			DHD_ERROR(("%s: pci_write_config_dword failed. cap=%d offset=%d\n",
				__FUNCTION__, cap, offset));
			return BCME_ERROR;
		}
		ret = BCME_OK;

	} else {

		ret = pci_read_config_dword(bus->rc_dev, (cap_ptr + offset), &readval);

		if (ret) {
			DHD_ERROR(("%s: pci_read_config_dword failed. cap=%d offset=%d\n",
				__FUNCTION__, cap, offset));
			return BCME_ERROR;
		}
		ret = readval;
	}

	return ret;
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
#endif

int dhdpcie_pci_suspend_resume(dhd_bus_t *bus, bool state)
{
	int rc;

	struct pci_dev *dev = bus->dev;

	if (state) {
#ifndef BCMPCIE_OOB_HOST_WAKE
		dhdpcie_pme_active(bus->osh, state);
#endif /* !BCMPCIE_OOB_HOST_WAKE */
		rc = dhdpcie_suspend_dev(dev);
		if (!rc) {
			dhdpcie_suspend_host_dev(bus);
		}
	} else {
		dhdpcie_resume_host_dev(bus);
		rc = dhdpcie_resume_dev(dev);
#ifndef	BCMPCIE_OOB_HOST_WAKE
		dhdpcie_pme_active(bus->osh, state);
#endif /* !BCMPCIE_OOB_HOST_WAKE */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
#if defined(DHD_HANG_SEND_UP_TEST)
		if (bus->is_linkdown ||
			bus->dhd->req_hang_type == HANG_REASON_PCIE_RC_LINK_UP_FAIL)
#else /* DHD_HANG_SEND_UP_TEST */
		if (bus->is_linkdown)
#endif /* DHD_HANG_SEND_UP_TEST */
		{
			bus->dhd->hang_reason = HANG_REASON_PCIE_RC_LINK_UP_FAIL;
			dhd_os_send_hang_message(bus->dhd);
		}
#endif 
	}
	return rc;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
static int dhdpcie_device_scan(struct device *dev, void *data)
{
	struct pci_dev *pcidev;
	int *cnt = data;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	pcidev = container_of(dev, struct pci_dev, dev);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	if (pcidev->vendor != 0x14e4)
		return 0;

	DHD_INFO(("Found Broadcom PCI device 0x%04x\n", pcidev->device));
	*cnt += 1;
	if (pcidev->driver && strcmp(pcidev->driver->name, dhdpcie_driver.name))
		DHD_ERROR(("Broadcom PCI Device 0x%04x has allocated with driver %s\n",
			pcidev->device, pcidev->driver->name));

	return 0;
}
#endif /* LINUX_VERSION >= 2.6.0 */

int
dhdpcie_bus_register(void)
{
	int error = 0;


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0))
	if (!(error = pci_module_init(&dhdpcie_driver)))
		return 0;

	DHD_ERROR(("%s: pci_module_init failed 0x%x\n", __FUNCTION__, error));
#else
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
#endif /* LINUX_VERSION < 2.6.0 */

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
	int err = 0;
	DHD_MUTEX_LOCK();

	if (dhdpcie_chipmatch (pdev->vendor, pdev->device)) {
		DHD_ERROR(("%s: chipmatch failed!!\n", __FUNCTION__));
		err = -ENODEV;
		goto exit;
	}
	printf("PCI_PROBE:  bus %X, slot %X,vendor %X, device %X"
		"(good PCI location)\n", pdev->bus->number,
		PCI_SLOT(pdev->devfn), pdev->vendor, pdev->device);

	if (dhdpcie_init (pdev)) {
		DHD_ERROR(("%s: PCIe Enumeration failed\n", __FUNCTION__));
		err = -ENODEV;
		goto exit;
	}

#ifdef BCMPCIE_DISABLE_ASYNC_SUSPEND
	/* disable async suspend */
	device_disable_async_suspend(&pdev->dev);
#endif /* BCMPCIE_DISABLE_ASYNC_SUSPEND */

	DHD_TRACE(("%s: PCIe Enumeration done!!\n", __FUNCTION__));

exit:
	DHD_MUTEX_UNLOCK();
	return err;
}

int
dhdpcie_detach(dhdpcie_info_t *pch)
{
	if (pch) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
		if (!dhd_download_fw_on_driverload) {
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

	DHD_MUTEX_LOCK();

	pch = pci_get_drvdata(pdev);
	bus = pch->bus;
	osh = pch->osh;

#ifdef SUPPORT_LINKDOWN_RECOVERY
	if (bus) {
#ifdef CONFIG_ARCH_MSM
		msm_pcie_deregister_event(&bus->pcie_event);
#endif /* CONFIG_ARCH_MSM */
#ifdef EXYNOS_PCIE_LINKDOWN_RECOVERY
#ifdef CONFIG_SOC_EXYNOS8890
		exynos_pcie_deregister_event(&bus->pcie_event);
#endif /* CONFIG_SOC_EXYNOS8890 */
#endif /* EXYNOS_PCIE_LINKDOWN_RECOVERY */
	}
#endif /* SUPPORT_LINKDOWN_RECOVERY */

	bus->rc_dev = NULL;

	dhdpcie_bus_release(bus);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
	if (pci_is_enabled(pdev))
#endif
		pci_disable_device(pdev);
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

	DHD_MUTEX_UNLOCK();

	DHD_TRACE(("%s Exit\n", __FUNCTION__));

	return;
}

/* Free Linux irq */
int
dhdpcie_request_irq(dhdpcie_info_t *dhdpcie_info)
{
	dhd_bus_t *bus = dhdpcie_info->bus;
	struct pci_dev *pdev = dhdpcie_info->bus->dev;
	int err = 0;

	if (!bus->irq_registered) {
		snprintf(dhdpcie_info->pciname, sizeof(dhdpcie_info->pciname),
		    "dhdpcie:%s", pci_name(pdev));
#ifdef DHD_USE_MSI
		printf("%s: MSI enabled\n", __FUNCTION__);
		err = pci_enable_msi(pdev);
		if (err < 0) {
			DHD_ERROR(("%s: pci_enable_msi() failed, %d, fall back to INTx\n", __FUNCTION__, err));
		}
#else
		printf("%s: MSI not enabled\n", __FUNCTION__);
#endif /* DHD_USE_MSI */
		err = request_irq(pdev->irq, dhdpcie_isr, IRQF_SHARED,
			dhdpcie_info->pciname, bus);
		if (err) {
			DHD_ERROR(("%s: request_irq() failed\n", __FUNCTION__));
#ifdef DHD_USE_MSI
			pci_disable_msi(pdev);
#endif /* DHD_USE_MSI */
			return -1;
		} else {
			bus->irq_registered = TRUE;
		}
	} else {
		DHD_ERROR(("%s: PCI IRQ is already registered\n", __FUNCTION__));
	}

	if (!dhdpcie_irq_enabled(bus)) {
		DHD_ERROR(("%s: PCIe IRQ was disabled, so, enabled it again\n", __FUNCTION__));
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
#endif

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
	struct pci_dev *pdev = NULL;
	pdev = dhdpcie_info->dev;
#ifdef EXYNOS_PCIE_MODULE_PATCH
	pci_restore_state(pdev);
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
		dhdpcie_info->tcm_size =
			(bar1_size > DONGLE_TCM_MAP_SIZE) ? bar1_size : DONGLE_TCM_MAP_SIZE;
		dhdpcie_info->tcm = (volatile char *) REG_MAP(bar1_addr, dhdpcie_info->tcm_size);

		if (!dhdpcie_info->regs || !dhdpcie_info->tcm) {
			DHD_ERROR(("%s:ioremap() failed\n", __FUNCTION__));
			break;
		}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
		if (!dhd_download_fw_on_driverload) {
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
		}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

#ifdef EXYNOS_PCIE_MODULE_PATCH
		pci_save_state(pdev);
#endif /* EXYNOS_MODULE_PATCH */

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

#ifdef SUPPORT_LINKDOWN_RECOVERY
#if defined(CONFIG_ARCH_MSM) || (defined(EXYNOS_PCIE_LINKDOWN_RECOVERY) && \
	(defined(CONFIG_SOC_EXYNOS8890) || defined(CONFIG_SOC_EXYNOS8895)))
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
					dhd->hang_reason = HANG_REASON_PCIE_LINK_DOWN;
					dhd_os_send_hang_message(dhd);
				}
			}
		}
	}

}
#endif
/* CONFIG_ARCH_MSM || (EXYNOS_PCIE_LINKDOWN_RECOVERY &&
	* (CONFIG_SOC_EXYNOS8890 || CONFIG_SOC_EXYNOS8895))
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

	do {
		/* osl attach */
		if (!(osh = osl_attach(pdev, PCI_BUS, FALSE))) {
			DHD_ERROR(("%s: osl_attach failed\n", __FUNCTION__));
			break;
		}

		/* initialize static buffer */
		adapter = dhd_wifi_platform_get_adapter(PCI_BUS, pdev->bus->number,
			PCI_SLOT(pdev->devfn));
		if (adapter != NULL) {
			DHD_ERROR(("%s: found adapter info '%s'\n", __FUNCTION__, adapter->name));
#ifdef BUS_POWER_RESTORE
			adapter->pci_dev = pdev;
#endif
		} else
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
		bus = dhdpcie_bus_attach(osh, dhdpcie_info->regs, dhdpcie_info->tcm, pdev);
		if (!bus) {
			DHD_ERROR(("%s:dhdpcie_bus_attach() failed\n", __FUNCTION__));
			break;
		}

		dhdpcie_info->bus = bus;
		bus->is_linkdown = 0;

		/* Get RC Device Handle */
#if defined(PCIE_RC_VENDOR_ID) && defined(PCIE_RC_DEVICE_ID)
		bus->rc_dev = pci_get_device(PCIE_RC_VENDOR_ID, PCIE_RC_DEVICE_ID, NULL);
#else
		bus->rc_dev = NULL;
#endif

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
		bus->no_cfg_restore = 0;
#endif /* CONFIG_ARCH_MSM */
#ifdef EXYNOS_PCIE_LINKDOWN_RECOVERY
#if defined(CONFIG_SOC_EXYNOS8890) || defined(CONFIG_SOC_EXYNOS8895)
		bus->pcie_event.events = EXYNOS_PCIE_EVENT_LINKDOWN;
		bus->pcie_event.user = pdev;
		bus->pcie_event.mode = EXYNOS_PCIE_TRIGGER_CALLBACK;
		bus->pcie_event.callback = dhdpcie_linkdown_cb;
		exynos_pcie_register_event(&bus->pcie_event);
#endif /* CONFIG_SOC_EXYNOS8890 || CONFIG_SOC_EXYNOS8895 */
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
		if (dhd_register_if(bus->dhd, 0, TRUE)) {
			DHD_ERROR(("%s(): ERROR.. dhd_register_if() failed\n", __FUNCTION__));
			break;
		}

		dhdpcie_init_succeeded = TRUE;

#if defined(MULTIPLE_SUPPLICANT)
		wl_android_post_init(); // terence 20120530: fix critical section in dhd_open and dhdsdio_probe
#endif /* MULTIPLE_SUPPLICANT */

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
#ifdef DHD_USE_MSI
			pci_disable_msi(pdev);
#endif /* DHD_USE_MSI */
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
	if (dhdpcie_bus_isr(bus))
		return TRUE;
	else
		return FALSE;
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

bool
dhdpcie_irq_enabled(dhd_bus_t *bus)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
	struct irq_desc *desc = irq_to_desc(bus->dev->irq);
	/* depth will be zero, if enabled */
	if (!desc->depth) {
		DHD_ERROR(("%s: depth:%d\n", __FUNCTION__, desc->depth));
	}
	return desc->depth ? FALSE : TRUE;
#else
	/* return TRUE by default as there is no support for lower versions */
	return TRUE;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
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
	/* Always reset the PCIe host when wifi off */
	bus->no_cfg_restore = 1;

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
		dhdpcie_info->tcm_size =
			(bar1_size > DONGLE_TCM_MAP_SIZE) ? bar1_size : DONGLE_TCM_MAP_SIZE;
		dhdpcie_info->tcm = (volatile char *) REG_MAP(bar1_addr, dhdpcie_info->tcm_size);
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
		} else {
			disable_irq_nosync(dhdpcie_osinfo->oob_irq_num);
		}
		dhdpcie_osinfo->oob_irq_enabled = enable;
	}
	spin_unlock_irqrestore(&dhdpcie_osinfo->oob_irq_spinlock, flags);
}

static irqreturn_t wlan_oob_irq(int irq, void *data)
{
	dhd_bus_t *bus;
	DHD_TRACE(("%s: IRQ Triggered\n", __FUNCTION__));
	bus = (dhd_bus_t *)data;
	dhdpcie_oob_intr_set(bus, FALSE);
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
	if (bus->dhd->up && bus->oob_presuspend) {
		DHD_OS_OOB_IRQ_WAKE_LOCK_TIMEOUT(bus->dhd, OOB_WAKE_LOCK_TIMEOUT);
	}
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
		printf("%s OOB irq=%d flags=0x%X\n", __FUNCTION__,
			(int)dhdpcie_osinfo->oob_irq_num,
			(int)dhdpcie_osinfo->oob_irq_flags);
		err = request_irq(dhdpcie_osinfo->oob_irq_num, wlan_oob_irq,
			dhdpcie_osinfo->oob_irq_flags, "dhdpcie_host_wake",
			bus);
		if (err) {
			DHD_ERROR(("%s: request_irq failed with %d\n",
				__FUNCTION__, err));
			return err;
		}
#if defined(DISABLE_WOWLAN)
		printf("%s: disable_irq_wake\n", __FUNCTION__);
		dhdpcie_osinfo->oob_irq_wake_enabled = FALSE;
#else
		printf("%s: enable_irq_wake\n", __FUNCTION__);
		err = enable_irq_wake(dhdpcie_osinfo->oob_irq_num);
		if (!err) {
			dhdpcie_osinfo->oob_irq_wake_enabled = TRUE;
		} else
			printf("%s: enable_irq_wake failed with %d\n", __FUNCTION__, err);
#endif
		dhdpcie_osinfo->oob_irq_enabled = TRUE;
	}

	dhdpcie_osinfo->oob_irq_registered = TRUE;

	return 0;
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

#ifdef PCIE_OOB
void dhdpcie_oob_init(dhd_bus_t *bus)
{
	gpio_handle_val = get_handle(OOB_PORT);
	if (gpio_handle_val < 0)
	{
		DHD_ERROR(("%s: Could not get GPIO handle.\n", __FUNCTION__));
		ASSERT(FALSE);
	}

	gpio_direction = 0;
	ftdi_set_bitmode(gpio_handle_val, 0, BITMODE_BITBANG);

	/* Note BT core is also enabled here */
	gpio_port = 1 << BIT_WL_REG_ON | 1 << BIT_BT_REG_ON | 1 << DEVICE_WAKE;
	gpio_write_port(gpio_handle_val, gpio_port);

	gpio_direction = 1 << BIT_WL_REG_ON | 1 << BIT_BT_REG_ON | 1 << DEVICE_WAKE;
	ftdi_set_bitmode(gpio_handle_val, gpio_direction, BITMODE_BITBANG);

	bus->oob_enabled = TRUE;
	bus->oob_presuspend = FALSE;

	/* drive the Device_Wake GPIO low on startup */
	bus->device_wake_state = TRUE;
	dhd_bus_set_device_wake(bus, FALSE);
	dhd_bus_doorbell_timeout_reset(bus);

}

void
dhd_oob_set_bt_reg_on(struct dhd_bus *bus, bool val)
{
	DHD_INFO(("Set Device_Wake to %d\n", val));
	if (val)
	{
		gpio_port = gpio_port | (1 << BIT_BT_REG_ON);
		gpio_write_port(gpio_handle_val, gpio_port);
	} else {
		gpio_port = gpio_port & (0xff ^ (1 << BIT_BT_REG_ON));
		gpio_write_port(gpio_handle_val, gpio_port);
	}
}

int
dhd_oob_get_bt_reg_on(struct dhd_bus *bus)
{
	int ret;
	uint8 val;
	ret = gpio_read_port(gpio_handle_val, &val);

	if (ret < 0) {
		DHD_ERROR(("gpio_read_port returns %d\n", ret));
		return ret;
	}

	if (val & (1 << BIT_BT_REG_ON))
	{
		ret = 1;
	} else {
		ret = 0;
	}

	return ret;
}

int
dhd_os_oob_set_device_wake(struct dhd_bus *bus, bool val)
{
	if (bus->device_wake_state != val)
	{
		DHD_INFO(("Set Device_Wake to %d\n", val));

		if (bus->oob_enabled && !bus->oob_presuspend)
		{
			if (val)
			{
				gpio_port = gpio_port | (1 << DEVICE_WAKE);
				gpio_write_port_non_block(gpio_handle_val, gpio_port);
			} else {
				gpio_port = gpio_port & (0xff ^ (1 << DEVICE_WAKE));
				gpio_write_port_non_block(gpio_handle_val, gpio_port);
			}
		}

		bus->device_wake_state = val;
	}
	return BCME_OK;
}

INLINE void
dhd_os_ib_set_device_wake(struct dhd_bus *bus, bool val)
{
	/* TODO: Currently Inband implementation of Device_Wake is not supported,
	 * so this function is left empty later this can be used to support the same.
	 */
}
#endif /* PCIE_OOB */

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

#ifdef HOFFLOAD_MODULES
void
dhd_free_module_memory(struct dhd_bus *bus, struct module_metadata *hmem)
{
	struct device *dev = &bus->dev->dev;
	if (hmem) {
		dma_unmap_single(dev, (dma_addr_t) hmem->data_addr, hmem->size, DMA_TO_DEVICE);
		kfree(hmem->data);
		hmem->data = NULL;
		hmem->size = 0;
	} else {
		DHD_ERROR(("dev:%p pci unmapping error\n", dev));
	}
}

void *
dhd_alloc_module_memory(struct dhd_bus *bus, uint32_t size, struct module_metadata *hmem)
{
	struct device *dev = &bus->dev->dev;
	if (!hmem->data) {
		hmem->data = kzalloc(size, GFP_KERNEL);
		if (!hmem->data) {
			DHD_ERROR(("dev:%p mem alloc failure\n", dev));
			return NULL;
		}
	}
	hmem->size = size;
	DHD_INFO(("module size: 0x%x \n", hmem->size));
	hmem->data_addr = (u64) dma_map_single(dev, hmem->data, hmem->size, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, hmem->data_addr)) {
		DHD_ERROR(("dev:%p dma mapping error\n", dev));
		kfree(hmem->data);
		hmem->data = NULL;
		return hmem->data;
	}
	return hmem->data;
}
#endif /* HOFFLOAD_MODULES */
