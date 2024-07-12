/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_H_
#define _FBNIC_H_

#include <linux/io.h>

#include "fbnic_csr.h"
#include "fbnic_mac.h"

struct fbnic_dev {
	struct device *dev;

	u32 __iomem *uc_addr0;
	u32 __iomem *uc_addr4;
	const struct fbnic_mac *mac;
	unsigned short num_irqs;

	u64 dsn;
	u32 mps;
	u32 readrq;
};

/* Reserve entry 0 in the MSI-X "others" array until we have filled all
 * 32 of the possible interrupt slots. By doing this we can avoid any
 * potential conflicts should we need to enable one of the debug interrupt
 * causes later.
 */
enum {
	FBNIC_NON_NAPI_VECTORS
};

static inline bool fbnic_present(struct fbnic_dev *fbd)
{
	return !!READ_ONCE(fbd->uc_addr0);
}

static inline void fbnic_wr32(struct fbnic_dev *fbd, u32 reg, u32 val)
{
	u32 __iomem *csr = READ_ONCE(fbd->uc_addr0);

	if (csr)
		writel(val, csr + reg);
}

u32 fbnic_rd32(struct fbnic_dev *fbd, u32 reg);

static inline void fbnic_wrfl(struct fbnic_dev *fbd)
{
	fbnic_rd32(fbd, FBNIC_MASTER_SPARE_0);
}

static inline void
fbnic_rmw32(struct fbnic_dev *fbd, u32 reg, u32 mask, u32 val)
{
	u32 v;

	v = fbnic_rd32(fbd, reg);
	v &= ~mask;
	v |= val;
	fbnic_wr32(fbd, reg, v);
}

#define wr32(_f, _r, _v)	fbnic_wr32(_f, _r, _v)
#define rd32(_f, _r)		fbnic_rd32(_f, _r)
#define wrfl(_f)		fbnic_wrfl(_f)

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
