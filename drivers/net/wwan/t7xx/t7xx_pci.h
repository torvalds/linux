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

#include <linux/irqreturn.h>
#include <linux/pci.h>
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
};

#endif /* __T7XX_PCI_H__ */
