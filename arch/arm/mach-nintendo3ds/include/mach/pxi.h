/*
 *  pxi.h
 *
 *  Copyright (C) 2016 Sergi Granell <xerpi.g.12@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __NINTENDO3DS_PXI_H
#define __NINTENDO3DS_PXI_H

#include <linux/types.h>
#include <mach/pxi_cmd.h>

#define PXI_REG_SYNC11_OFFSET	0x0
#define PXI_REG_CNT11_OFFSET	0x4
#define PXI_REG_SEND11_OFFSET	0x8
#define PXI_REG_RECV11_OFFSET	0xC

#define PXI_HWIRQ_SYNC			0x50
#define PXI_HWIRQ_SEND_FIFO_EMPTY	0x52
#define PXI_HWIRQ_RECV_FIFO_NOT_EMPTY	0x53

#define PXI_CNT_SEND_FIFO_EMPTY		(1 << 0)
#define PXI_CNT_SEND_FIFO_FULL		(1 << 1)
#define PXI_CNT_SEND_FIFO_EMPTY_IRQ	(1 << 2)
#define PXI_CNT_SEND_FIFO_FLUSH		(1 << 3)
#define PXI_CNT_RECV_FIFO_EMPTY		(1 << 8)
#define PXI_CNT_RECV_FIFO_FULL		(1 << 9)
#define PXI_CNT_RECV_FIFO_NOT_EMPTY_IRQ	(1 << 10)
#define PXI_CNT_FIFO_ENABLE		(1 << 15)

#define PXI_SYNC_TRIGGER_PXI_SYNC11	(1 << 29)
#define PXI_SYNC_TRIGGER_PXI_SYNC9	(1 << 30)
#define PXI_SYNC_IRQ_ENABLE		(1 << 31)

void pxi_send_cmd(struct pxi_cmd_hdr *cmd);

#endif
