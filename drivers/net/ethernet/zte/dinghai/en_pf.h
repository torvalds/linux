/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ZTE DingHai Ethernet driver - PF header
 * Copyright (c) 2022-2026, ZTE Corporation.
 */

#ifndef __ZXDH_EN_PF_H__
#define __ZXDH_EN_PF_H__

#define ZXDH_PF_VENDOR_ID	0x1cf2
#define ZXDH_PF_DEVICE_ID	0x8040
#define ZXDH_VF_DEVICE_ID	0x8041

struct zxdh_core_dev {
	struct device *device;
	struct pci_dev *pdev;
	struct devlink *devlink;
	void *priv;
};

struct zxdh_pf_dev {
	void __iomem *pci_ioremap_addr[6];
};

void *zxdh_core_alloc_priv(struct zxdh_core_dev *zxdh_dev, size_t size);
void zxdh_core_free_priv(struct zxdh_core_dev *zxdh_dev);
void zxdh_pf_pci_close(struct zxdh_core_dev *zxdh_dev);

#endif /* __ZXDH_EN_PF_H__ */
