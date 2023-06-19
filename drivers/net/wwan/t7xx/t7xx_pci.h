/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Moises Veleta <moises.veleta@intel.com>
 */

#ifndef __T7XX_PCI_H__
#define __T7XX_PCI_H__

#include <linux/completion.h>
#include <linux/irqreturn.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "t7xx_reg.h"

/* struct t7xx_addr_base - holds base addresses
 * @pcie_mac_ireg_base: PCIe MAC register base
 * @pcie_ext_reg_base: used to calculate base addresses for CLDMA, DPMA and MHCCIF registers
 * @pcie_dev_reg_trsl_addr: used to calculate the register base address
 * @infracfg_ao_base: base address used in CLDMA reset operations
 * @mhccif_rc_base: host view of MHCCIF rc base addr
 */
struct t7xx_addr_base {
	void __iomem		*pcie_mac_ireg_base;
	void __iomem		*pcie_ext_reg_base;
	u32			pcie_dev_reg_trsl_addr;
	void __iomem		*infracfg_ao_base;
	void __iomem		*mhccif_rc_base;
};

typedef irqreturn_t (*t7xx_intr_callback)(int irq, void *param);

/* struct t7xx_pci_dev - MTK device context structure
 * @intr_handler: array of handler function for request_threaded_irq
 * @intr_thread: array of thread_fn for request_threaded_irq
 * @callback_param: array of cookie passed back to interrupt functions
 * @pdev: PCI device
 * @base_addr: memory base addresses of HW components
 * @md: modem interface
 * @ccmni_ctlb: context structure used to control the network data path
 * @rgu_pci_irq_en: RGU callback ISR registered and active
 * @md_pm_entities: list of pm entities
 * @md_pm_entity_mtx: protects md_pm_entities list
 * @pm_sr_ack: ack from the device when went to sleep or woke up
 * @md_pm_state: state for resume/suspend
 * @md_pm_lock: protects PCIe sleep lock
 * @sleep_disable_count: PCIe L1.2 lock counter
 * @sleep_lock_acquire: indicates that sleep has been disabled
 */
struct t7xx_pci_dev {
	t7xx_intr_callback	intr_handler[EXT_INT_NUM];
	t7xx_intr_callback	intr_thread[EXT_INT_NUM];
	void			*callback_param[EXT_INT_NUM];
	struct pci_dev		*pdev;
	struct t7xx_addr_base	base_addr;
	struct t7xx_modem	*md;
	struct t7xx_ccmni_ctrl	*ccmni_ctlb;
	bool			rgu_pci_irq_en;
	struct completion	init_done;

	/* Low Power Items */
	struct list_head	md_pm_entities;
	struct mutex		md_pm_entity_mtx;	/* Protects MD PM entities list */
	struct completion	pm_sr_ack;
	atomic_t		md_pm_state;
	spinlock_t		md_pm_lock;		/* Protects PCI resource lock */
	unsigned int		sleep_disable_count;
	struct completion	sleep_lock_acquire;
#ifdef CONFIG_WWAN_DEBUGFS
	struct dentry		*debugfs_dir;
#endif
};

enum t7xx_pm_id {
	PM_ENTITY_ID_CTRL1,
	PM_ENTITY_ID_CTRL2,
	PM_ENTITY_ID_DATA,
	PM_ENTITY_ID_INVALID
};

/* struct md_pm_entity - device power management entity
 * @entity: list of PM Entities
 * @suspend: callback invoked before sending D3 request to device
 * @suspend_late: callback invoked after getting D3 ACK from device
 * @resume_early: callback invoked before sending the resume request to device
 * @resume: callback invoked after getting resume ACK from device
 * @id: unique PM entity identifier
 * @entity_param: parameter passed to the registered callbacks
 *
 *  This structure is used to indicate PM operations required by internal
 *  HW modules such as CLDMA and DPMA.
 */
struct md_pm_entity {
	struct list_head	entity;
	int (*suspend)(struct t7xx_pci_dev *t7xx_dev, void *entity_param);
	void (*suspend_late)(struct t7xx_pci_dev *t7xx_dev, void *entity_param);
	void (*resume_early)(struct t7xx_pci_dev *t7xx_dev, void *entity_param);
	int (*resume)(struct t7xx_pci_dev *t7xx_dev, void *entity_param);
	enum t7xx_pm_id		id;
	void			*entity_param;
};

void t7xx_pci_disable_sleep(struct t7xx_pci_dev *t7xx_dev);
void t7xx_pci_enable_sleep(struct t7xx_pci_dev *t7xx_dev);
int t7xx_pci_sleep_disable_complete(struct t7xx_pci_dev *t7xx_dev);
int t7xx_pci_pm_entity_register(struct t7xx_pci_dev *t7xx_dev, struct md_pm_entity *pm_entity);
int t7xx_pci_pm_entity_unregister(struct t7xx_pci_dev *t7xx_dev, struct md_pm_entity *pm_entity);
void t7xx_pci_pm_init_late(struct t7xx_pci_dev *t7xx_dev);
void t7xx_pci_pm_exp_detected(struct t7xx_pci_dev *t7xx_dev);

#endif /* __T7XX_PCI_H__ */
