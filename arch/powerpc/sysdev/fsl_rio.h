/*
 * Freescale MPC85xx/MPC86xx RapidIO support
 *
 * Copyright 2009 Sysgo AG
 * Thomas Moll <thomas.moll@sysgo.com>
 * - fixed maintenance access routines, check for aligned access
 *
 * Copyright 2009 Integrated Device Technology, Inc.
 * Alex Bounine <alexandre.bounine@idt.com>
 * - Added Port-Write message handling
 * - Added Machine Check exception handling
 *
 * Copyright (C) 2007, 2008, 2010, 2011 Freescale Semiconductor, Inc.
 * Zhang Wei <wei.zhang@freescale.com>
 * Lian Minghuan-B31939 <Minghuan.Lian@freescale.com>
 * Liu Gang <Gang.Liu@freescale.com>
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __FSL_RIO_H
#define __FSL_RIO_H

#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/kfifo.h>

#define RIO_REGS_WIN(mport)	(((struct rio_priv *)(mport->priv))->regs_win)

#define RIO_MAINT_WIN_SIZE	0x400000
#define RIO_LTLEDCSR		0x0608

struct rio_atmu_regs {
	 u32 rowtar;
	 u32 rowtear;
	 u32 rowbar;
	 u32 pad2;
	 u32 rowar;
	 u32 pad3[3];
};

struct rio_port_write_msg {
	 void *virt;
	 dma_addr_t phys;
	 u32 msg_count;
	 u32 err_count;
	 u32 discard_count;
};

struct rio_priv {
	struct device *dev;
	void __iomem *regs_win;
	struct rio_atmu_regs __iomem *atmu_regs;
	struct rio_atmu_regs __iomem *maint_atmu_regs;
	void __iomem *maint_win;
	struct rio_port_write_msg port_write_msg;
	int pwirq;
	struct work_struct pw_work;
	struct kfifo pw_fifo;
	spinlock_t pw_fifo_lock;
	void *rmm_handle; /* RapidIO message manager(unit) Handle */
};

extern void __iomem *rio_regs_win;

extern int fsl_rio_setup_rmu(struct rio_mport *mport,
	struct device_node *node);
extern int fsl_rio_port_write_init(struct rio_mport *mport);
extern int fsl_rio_pw_enable(struct rio_mport *mport, int enable);
extern void fsl_rio_port_error_handler(struct rio_mport *port, int offset);

#endif
