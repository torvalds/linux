/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2007 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: John Rigby, <jrigby@freescale.com>, Friday Apr 13 2007
 *
 * Description:
 * MPC5xxx Prototypes and definitions
 */

#ifndef __ASM_POWERPC_MPC5xxx_H__
#define __ASM_POWERPC_MPC5xxx_H__

#include <linux/property.h>

unsigned long mpc5xxx_fwnode_get_bus_frequency(struct fwnode_handle *fwnode);

static inline unsigned long mpc5xxx_get_bus_frequency(struct device *dev)
{
	return mpc5xxx_fwnode_get_bus_frequency(dev_fwnode(dev));
}

#endif /* __ASM_POWERPC_MPC5xxx_H__ */

