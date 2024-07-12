/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_H_
#define _FBNIC_H_

#include "fbnic_csr.h"

struct fbnic_dev {
	struct device *dev;

	u32 __iomem *uc_addr0;
	u32 __iomem *uc_addr4;
	unsigned short num_irqs;

	u64 dsn;
};

/* Reserve entry 0 in the MSI-X "others" array until we have filled all
 * 32 of the possible interrupt slots. By doing this we can avoid any
 * potential conflicts should we need to enable one of the debug interrupt
 * causes later.
 */
enum {
	FBNIC_NON_NAPI_VECTORS
};

extern char fbnic_driver_name[];

void fbnic_devlink_free(struct fbnic_dev *fbd);
struct fbnic_dev *fbnic_devlink_alloc(struct pci_dev *pdev);
void fbnic_devlink_register(struct fbnic_dev *fbd);
void fbnic_devlink_unregister(struct fbnic_dev *fbd);

void fbnic_free_irqs(struct fbnic_dev *fbd);
int fbnic_alloc_irqs(struct fbnic_dev *fbd);

enum fbnic_boards {
	fbnic_board_asic
};

struct fbnic_info {
	unsigned int bar_mask;
};

#endif /* _FBNIC_H_ */
