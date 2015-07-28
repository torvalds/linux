/*
 * netup_unidvb.h
 *
 * Data type definitions for NetUP Universal Dual DVB-CI
 *
 * Copyright (C) 2014 NetUP Inc.
 * Copyright (C) 2014 Sergey Kozlov <serjk@netup.ru>
 * Copyright (C) 2014 Abylay Ospan <aospan@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-dvb.h>
#include <dvb_ca_en50221.h>

#define NETUP_UNIDVB_NAME	"netup_unidvb"
#define NETUP_UNIDVB_VERSION	"0.0.1"
#define NETUP_VENDOR_ID		0x1b55
#define NETUP_PCI_DEV_REVISION  0x2

/* IRQ-related regisers */
#define REG_ISR			0x4890
#define REG_ISR_MASKED		0x4892
#define REG_IMASK_SET		0x4894
#define REG_IMASK_CLEAR		0x4896
/* REG_ISR register bits */
#define NETUP_UNIDVB_IRQ_SPI	(1 << 0)
#define NETUP_UNIDVB_IRQ_I2C0	(1 << 1)
#define NETUP_UNIDVB_IRQ_I2C1	(1 << 2)
#define NETUP_UNIDVB_IRQ_FRA0	(1 << 4)
#define NETUP_UNIDVB_IRQ_FRA1	(1 << 5)
#define NETUP_UNIDVB_IRQ_FRB0	(1 << 6)
#define NETUP_UNIDVB_IRQ_FRB1	(1 << 7)
#define NETUP_UNIDVB_IRQ_DMA1	(1 << 8)
#define NETUP_UNIDVB_IRQ_DMA2	(1 << 9)
#define NETUP_UNIDVB_IRQ_CI	(1 << 10)
#define NETUP_UNIDVB_IRQ_CAM0	(1 << 11)
#define NETUP_UNIDVB_IRQ_CAM1	(1 << 12)

struct netup_dma {
	u8			num;
	spinlock_t		lock;
	struct netup_unidvb_dev	*ndev;
	struct netup_dma_regs	*regs;
	u32			ring_buffer_size;
	u8			*addr_virt;
	dma_addr_t		addr_phys;
	u64			addr_last;
	u32			high_addr;
	u32			data_offset;
	u32			data_size;
	struct list_head	free_buffers;
	struct work_struct	work;
	struct timer_list	timeout;
};

enum netup_i2c_state {
	STATE_DONE,
	STATE_WAIT,
	STATE_WANT_READ,
	STATE_WANT_WRITE,
	STATE_ERROR
};

struct netup_i2c_regs;

struct netup_i2c {
	spinlock_t			lock;
	wait_queue_head_t		wq;
	struct i2c_adapter		adap;
	struct netup_unidvb_dev		*dev;
	struct netup_i2c_regs		*regs;
	struct i2c_msg			*msg;
	enum netup_i2c_state		state;
	u32				xmit_size;
};

struct netup_ci_state {
	struct dvb_ca_en50221		ca;
	u8 __iomem			*membase8_config;
	u8 __iomem			*membase8_io;
	struct netup_unidvb_dev		*dev;
	int status;
	int nr;
};

struct netup_spi;

struct netup_unidvb_dev {
	struct pci_dev			*pci_dev;
	int				pci_bus;
	int				pci_slot;
	int				pci_func;
	int				board_num;
	int				old_fw;
	u32 __iomem			*lmmio0;
	u8 __iomem			*bmmio0;
	u32 __iomem			*lmmio1;
	u8 __iomem			*bmmio1;
	u8				*dma_virt;
	dma_addr_t			dma_phys;
	u32				dma_size;
	struct vb2_dvb_frontends	frontends[2];
	struct netup_i2c		i2c[2];
	struct workqueue_struct		*wq;
	struct netup_dma		dma[2];
	struct netup_ci_state		ci[2];
	struct netup_spi		*spi;
};

int netup_i2c_register(struct netup_unidvb_dev *ndev);
void netup_i2c_unregister(struct netup_unidvb_dev *ndev);
irqreturn_t netup_ci_interrupt(struct netup_unidvb_dev *ndev);
irqreturn_t netup_i2c_interrupt(struct netup_i2c *i2c);
irqreturn_t netup_spi_interrupt(struct netup_spi *spi);
int netup_unidvb_ci_register(struct netup_unidvb_dev *dev,
			     int num, struct pci_dev *pci_dev);
void netup_unidvb_ci_unregister(struct netup_unidvb_dev *dev, int num);
int netup_spi_init(struct netup_unidvb_dev *ndev);
void netup_spi_release(struct netup_unidvb_dev *ndev);
