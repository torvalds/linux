/*
 *
 * Copyright (C) 2014 Altera Corporation
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
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ALTERA_EDAC_H
#define ALTERA_EDAC_H

#include <linux/edac.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include "edac_core.h"

struct ecc_mgr_prv_data {
	int (*setup)(struct platform_device *pdev, void __iomem *base);
	int ce_clear_mask;
	int ue_clear_mask;
#ifdef CONFIG_EDAC_DEBUG
	struct edac_dev_sysfs_attribute *eccmgr_sysfs_attr;
	void * (*init_mem)(size_t size, void **other);
	void (*free_mem)(void *p, void *other);
	int ecc_enable_mask;
	int ce_set_mask;
	int ue_set_mask;
	int trig_alloc_sz;
#endif
};

struct altr_ecc_mgr_dev {
	void __iomem *base;
	int sb_irq;
	int db_irq;
	const struct ecc_mgr_prv_data *data;
	char *edac_dev_name;
};

extern const struct ecc_mgr_prv_data l2ecc_data;
extern const struct ecc_mgr_prv_data ocramecc_data;

ssize_t altr_ecc_mgr_trig(struct edac_device_ctl_info *edac_dci,
			  const char *buffer, size_t count);

#endif	/* #ifndef ALTERA_EDAC_H */
