/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#ifndef _PDSC_H_
#define _PDSC_H_

#include <linux/debugfs.h>
#include <net/devlink.h>

#include <linux/pds/pds_common.h>
#include <linux/pds/pds_core_if.h>

#define PDSC_DRV_DESCRIPTION	"AMD/Pensando Core Driver"

struct pdsc_dev_bar {
	void __iomem *vaddr;
	phys_addr_t bus_addr;
	unsigned long len;
	int res_index;
};

/* No state flags set means we are in a steady running state */
enum pdsc_state_flags {
	PDSC_S_FW_DEAD,		    /* stopped, wait on startup or recovery */
	PDSC_S_INITING_DRIVER,	    /* initial startup from probe */
	PDSC_S_STOPPING_DRIVER,	    /* driver remove */

	/* leave this as last */
	PDSC_S_STATE_SIZE
};

struct pdsc {
	struct pci_dev *pdev;
	struct dentry *dentry;
	struct device *dev;
	struct pdsc_dev_bar bars[PDS_CORE_BARS_MAX];
	int hw_index;
	int uid;

	unsigned long state;

	struct pds_core_dev_info_regs __iomem *info_regs;
	struct pds_core_dev_cmd_regs __iomem *cmd_regs;
	struct pds_core_intr __iomem *intr_ctrl;
	u64 __iomem *intr_status;
	u64 __iomem *db_pages;
	dma_addr_t phy_db_pages;
	u64 __iomem *kern_dbpage;
};

void pdsc_debugfs_create(void);
void pdsc_debugfs_destroy(void);
void pdsc_debugfs_add_dev(struct pdsc *pdsc);
void pdsc_debugfs_del_dev(struct pdsc *pdsc);

#endif /* _PDSC_H_ */
