/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */



#ifndef _MEI_INTERFACE_H_
#define _MEI_INTERFACE_H_

#include <linux/irqreturn.h>
#include <linux/pci.h>
#include <linux/mei.h>

#include "mei_dev.h"
#include "client.h"

/*
 * mei_cfg - mei device configuration
 *
 * @fw_status: FW status
 * @quirk_probe: device exclusion quirk
 */
struct mei_cfg {
	const struct mei_fw_status fw_status;
	bool (*quirk_probe)(struct pci_dev *pdev);
};


#define MEI_PCI_DEVICE(dev, cfg) \
	.vendor = PCI_VENDOR_ID_INTEL, .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID, \
	.driver_data = (kernel_ulong_t)&(cfg)


#define MEI_ME_RPM_TIMEOUT    500 /* ms */

/**
 * struct mei_me_hw - me hw specific data
 *
 * @cfg: per device generation config and ops
 * @mem_addr:  io memory address
 * @pg_state:      power gating state
 */
struct mei_me_hw {
	const struct mei_cfg *cfg;
	void __iomem *mem_addr;
	enum mei_pg_state pg_state;
};

#define to_me_hw(dev) (struct mei_me_hw *)((dev)->hw)

extern const struct mei_cfg mei_me_legacy_cfg;
extern const struct mei_cfg mei_me_ich_cfg;
extern const struct mei_cfg mei_me_pch_cfg;
extern const struct mei_cfg mei_me_pch_cpt_pbg_cfg;
extern const struct mei_cfg mei_me_pch8_cfg;
extern const struct mei_cfg mei_me_pch8_sps_cfg;

struct mei_device *mei_me_dev_init(struct pci_dev *pdev,
				   const struct mei_cfg *cfg);

int mei_me_pg_set_sync(struct mei_device *dev);
int mei_me_pg_unset_sync(struct mei_device *dev);

irqreturn_t mei_me_irq_quick_handler(int irq, void *dev_id);
irqreturn_t mei_me_irq_thread_handler(int irq, void *dev_id);

#endif /* _MEI_INTERFACE_H_ */
