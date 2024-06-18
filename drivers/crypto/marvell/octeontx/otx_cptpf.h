/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTX CPT driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __OTX_CPTPF_H
#define __OTX_CPTPF_H

#include <linux/types.h>
#include <linux/device.h>
#include "otx_cptpf_ucode.h"

/*
 * OcteonTX CPT device structure
 */
struct otx_cpt_device {
	void __iomem *reg_base; /* Register start address */
	struct pci_dev *pdev;	/* Pci device handle */
	struct otx_cpt_eng_grps eng_grps;/* Engine groups information */
	struct list_head list;
	u8 pf_type;	/* PF type SE or AE */
	u8 max_vfs;	/* Maximum number of VFs supported by the CPT */
	u8 vfs_enabled;	/* Number of enabled VFs */
};

void otx_cpt_mbox_intr_handler(struct otx_cpt_device *cpt, int mbx);
void otx_cpt_disable_all_cores(struct otx_cpt_device *cpt);

#endif /* __OTX_CPTPF_H */
