/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#ifndef __EFC_COMMON_H__
#define __EFC_COMMON_H__

#include <linux/pci.h>

struct efc_dma {
	void		*virt;
	void            *alloc;
	dma_addr_t	phys;

	size_t		size;
	size_t          len;
	struct pci_dev	*pdev;
};

#define efc_log_crit(efc, fmt, args...) \
		dev_crit(&((efc)->pci)->dev, fmt, ##args)

#define efc_log_err(efc, fmt, args...) \
		dev_err(&((efc)->pci)->dev, fmt, ##args)

#define efc_log_warn(efc, fmt, args...) \
		dev_warn(&((efc)->pci)->dev, fmt, ##args)

#define efc_log_info(efc, fmt, args...) \
		dev_info(&((efc)->pci)->dev, fmt, ##args)

#define efc_log_debug(efc, fmt, args...) \
		dev_dbg(&((efc)->pci)->dev, fmt, ##args)

#endif /* __EFC_COMMON_H__ */
