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

#define DOORBELL_ROWAR_EN	0x80000000
#define DOORBELL_ROWAR_TFLOWLV	0x08000000 /* highest priority level */
#define DOORBELL_ROWAR_PCI	0x02000000 /* PCI window */
#define DOORBELL_ROWAR_NREAD	0x00040000 /* NREAD */
#define DOORBELL_ROWAR_MAINTRD	0x00070000  /* maintenance read */
#define DOORBELL_ROWAR_RES	0x00002000 /* wrtpy: reserverd */
#define DOORBELL_ROWAR_MAINTWD	0x00007000
#define DOORBELL_ROWAR_SIZE	0x0000000b /* window size is 4k */

#define RIO_ATMU_REGS_PORT1_OFFSET	0x10c00
#define RIO_ATMU_REGS_PORT2_OFFSET	0x10e00
#define RIO_S_DBELL_REGS_OFFSET	0x13400
#define RIO_S_PW_REGS_OFFSET	0x134e0
#define RIO_ATMU_REGS_DBELL_OFFSET	0x10C40
#define RIO_INB_ATMU_REGS_PORT1_OFFSET 0x10d60
#define RIO_INB_ATMU_REGS_PORT2_OFFSET 0x10f60

#define MAX_MSG_UNIT_NUM	2
#define MAX_PORT_NUM		4
#define RIO_INB_ATMU_COUNT	4

struct rio_atmu_regs {
	 u32 rowtar;
	 u32 rowtear;
	 u32 rowbar;
	 u32 pad1;
	 u32 rowar;
	 u32 pad2[3];
};

struct rio_inb_atmu_regs {
	u32 riwtar;
	u32 pad1;
	u32 riwbar;
	u32 pad2;
	u32 riwar;
	u32 pad3[3];
};

struct rio_dbell_ring {
	void *virt;
	dma_addr_t phys;
};

struct rio_port_write_msg {
	 void *virt;
	 dma_addr_t phys;
	 u32 msg_count;
	 u32 err_count;
	 u32 discard_count;
};

struct fsl_rio_dbell {
	struct rio_mport *mport[MAX_PORT_NUM];
	struct device *dev;
	struct rio_dbell_regs __iomem *dbell_regs;
	struct rio_dbell_ring dbell_ring;
	int bellirq;
};

struct fsl_rio_pw {
	struct device *dev;
	struct rio_pw_regs __iomem *pw_regs;
	struct rio_port_write_msg port_write_msg;
	int pwirq;
	struct work_struct pw_work;
	struct kfifo pw_fifo;
	spinlock_t pw_fifo_lock;
};

struct rio_priv {
	struct device *dev;
	void __iomem *regs_win;
	struct rio_atmu_regs __iomem *atmu_regs;
	struct rio_atmu_regs __iomem *maint_atmu_regs;
	struct rio_inb_atmu_regs __iomem *inb_atmu_regs;
	void __iomem *maint_win;
	void *rmm_handle; /* RapidIO message manager(unit) Handle */
};

extern void __iomem *rio_regs_win;
extern void __iomem *rmu_regs_win;

extern resource_size_t rio_law_start;

extern struct fsl_rio_dbell *dbell;
extern struct fsl_rio_pw *pw;

extern int fsl_rio_setup_rmu(struct rio_mport *mport,
	struct device_node *node);
extern int fsl_rio_port_write_init(struct fsl_rio_pw *pw);
extern int fsl_rio_pw_enable(struct rio_mport *mport, int enable);
extern void fsl_rio_port_error_handler(int offset);
extern int fsl_rio_doorbell_init(struct fsl_rio_dbell *dbell);

extern int fsl_rio_doorbell_send(struct rio_mport *mport,
				int index, u16 destid, u16 data);
extern int fsl_add_outb_message(struct rio_mport *mport,
	struct rio_dev *rdev,
	int mbox, void *buffer, size_t len);
extern int fsl_open_outb_mbox(struct rio_mport *mport,
	void *dev_id, int mbox, int entries);
extern void fsl_close_outb_mbox(struct rio_mport *mport, int mbox);
extern int fsl_open_inb_mbox(struct rio_mport *mport,
	void *dev_id, int mbox, int entries);
extern void fsl_close_inb_mbox(struct rio_mport *mport, int mbox);
extern int fsl_add_inb_buffer(struct rio_mport *mport, int mbox, void *buf);
extern void *fsl_get_inb_message(struct rio_mport *mport, int mbox);

#endif
