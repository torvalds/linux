/*
 * MPC8641 HPCN board definitions
 *
 * Copyright 2006 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Author: Xianghua Xiao <x.xiao@freescale.com>
 */

#ifndef __MPC8641_HPCN_H__
#define __MPC8641_HPCN_H__

#include <linux/config.h>
#include <linux/init.h>

/* PCI interrupt controller */
#define PIRQA		3
#define PIRQB		4
#define PIRQC		5
#define PIRQD		6
#define PIRQ7		7
#define PIRQE		9
#define PIRQF		10
#define PIRQG		11
#define PIRQH		12

/* PCI-Express memory map */
#define MPC86XX_PCIE_LOWER_IO        0x00000000
#define MPC86XX_PCIE_UPPER_IO        0x00ffffff

#define MPC86XX_PCIE_LOWER_MEM       0x80000000
#define MPC86XX_PCIE_UPPER_MEM       0x9fffffff

#define MPC86XX_PCIE_IO_BASE         0xe2000000
#define MPC86XX_PCIE_MEM_OFFSET      0x00000000

#define MPC86XX_PCIE_IO_SIZE         0x01000000

#define PCIE1_CFG_ADDR_OFFSET    (0x8000)
#define PCIE1_CFG_DATA_OFFSET    (0x8004)

#define PCIE2_CFG_ADDR_OFFSET    (0x9000)
#define PCIE2_CFG_DATA_OFFSET    (0x9004)

#define MPC86xx_PCIE_OFFSET PCIE1_CFG_ADDR_OFFSET
#define MPC86xx_PCIE_SIZE	(0x1000)

#define MPC86XX_RSTCR_OFFSET	(0xe00b0)	/* Reset Control Register */

#endif	/* __MPC8641_HPCN_H__ */
