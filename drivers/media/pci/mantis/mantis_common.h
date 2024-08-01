/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

*/

#ifndef __MANTIS_COMMON_H
#define __MANTIS_COMMON_H

#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "mantis_reg.h"
#include "mantis_uart.h"

#include "mantis_link.h"

#define MANTIS_ERROR		0
#define MANTIS_NOTICE		1
#define MANTIS_INFO		2
#define MANTIS_DEBUG		3
#define MANTIS_TMG		9

#define dprintk(y, z, format, arg...) do {								\
	if (z) {											\
		if	((mantis->verbose > MANTIS_ERROR) && (mantis->verbose > y))			\
			printk(KERN_ERR "%s (%d): " format "\n" , __func__ , mantis->num , ##arg);	\
		else if	((mantis->verbose > MANTIS_NOTICE) && (mantis->verbose > y))			\
			printk(KERN_NOTICE "%s (%d): " format "\n" , __func__ , mantis->num , ##arg);	\
		else if ((mantis->verbose > MANTIS_INFO) && (mantis->verbose > y))			\
			printk(KERN_INFO "%s (%d): " format "\n" , __func__ , mantis->num , ##arg);	\
		else if ((mantis->verbose > MANTIS_DEBUG) && (mantis->verbose > y))			\
			printk(KERN_DEBUG "%s (%d): " format "\n" , __func__ , mantis->num , ##arg);	\
		else if ((mantis->verbose > MANTIS_TMG) && (mantis->verbose > y))			\
			printk(KERN_DEBUG "%s (%d): " format "\n" , __func__ , mantis->num , ##arg);	\
	} else {											\
		if (mantis->verbose > y)								\
			printk(format , ##arg);								\
	}												\
} while(0)

#define mwrite(dat, addr)	writel((dat), addr)
#define mread(addr)		readl(addr)

#define mmwrite(dat, addr)	mwrite((dat), (mantis->mmio + (addr)))
#define mmread(addr)		mread(mantis->mmio + (addr))

#define MANTIS_TS_188		0
#define MANTIS_TS_204		1

#define TWINHAN_TECHNOLOGIES	0x1822
#define MANTIS			0x4e35

#define TECHNISAT		0x1ae4
#define TERRATEC		0x153b

#define MAKE_ENTRY(__subven, __subdev, __configptr, __rc) {		\
		.vendor		= TWINHAN_TECHNOLOGIES,			\
		.device		= MANTIS,				\
		.subvendor	= (__subven),				\
		.subdevice	= (__subdev),				\
		.driver_data	= (unsigned long)			\
			&(struct mantis_pci_drvdata){__configptr, __rc}	\
}

enum mantis_i2c_mode {
	MANTIS_PAGE_MODE = 0,
	MANTIS_BYTE_MODE,
};

struct mantis_pci;

struct mantis_hwconfig {
	char			*model_name;
	char			*dev_type;
	u32			ts_size;

	enum mantis_baud	baud_rate;
	enum mantis_parity	parity;
	u32			bytes;

	irqreturn_t (*irq_handler)(int irq, void *dev_id);
	int (*frontend_init)(struct mantis_pci *mantis, struct dvb_frontend *fe);

	u8			power;
	u8			reset;

	enum mantis_i2c_mode	i2c_mode;
};

struct mantis_pci_drvdata {
	struct mantis_hwconfig *hwconfig;
	char *rc_map_name;
};

struct mantis_pci {
	unsigned int		verbose;

	/*	PCI stuff		*/
	u16			vendor_id;
	u16			device_id;
	u16			subsystem_vendor;
	u16			subsystem_device;

	u8			latency;

	struct pci_dev		*pdev;

	unsigned long		mantis_addr;
	void __iomem		*mmio;

	u8			irq;
	u8			revision;

	unsigned int		num;

	/*	RISC Core		*/
	u32			busy_block;
	u32			last_block;
	u8			*buf_cpu;
	dma_addr_t		buf_dma;
	__le32			*risc_cpu;
	dma_addr_t		risc_dma;

	struct work_struct	bh_work;
	spinlock_t		intmask_lock;

	struct i2c_adapter	adapter;
	int			i2c_rc;
	wait_queue_head_t	i2c_wq;
	struct mutex		i2c_lock;

	/*	DVB stuff		*/
	struct dvb_adapter	dvb_adapter;
	struct dvb_frontend	*fe;
	struct dvb_demux	demux;
	struct dmxdev		dmxdev;
	struct dmx_frontend	fe_hw;
	struct dmx_frontend	fe_mem;
	struct dvb_net		dvbnet;

	u8			feeds;

	struct mantis_hwconfig	*hwconfig;

	u32			mantis_int_stat;
	u32			mantis_int_mask;

	/*	board specific		*/
	u8			mac_address[8];
	u32			sub_vendor_id;
	u32			sub_device_id;

	 /*	A12 A13 A14		*/
	u32			gpio_status;

	u32			gpif_status;

	struct mantis_ca	*mantis_ca;

	struct work_struct	uart_work;

	struct rc_dev		*rc;
	char			device_name[80];
	char			input_phys[80];
	char			*rc_map_name;
};

#define MANTIS_HIF_STATUS	(mantis->gpio_status)

static inline void mantis_mask_ints(struct mantis_pci *mantis, u32 mask)
{
	unsigned long flags;

	spin_lock_irqsave(&mantis->intmask_lock, flags);
	mmwrite(mmread(MANTIS_INT_MASK) & ~mask, MANTIS_INT_MASK);
	spin_unlock_irqrestore(&mantis->intmask_lock, flags);
}

static inline void mantis_unmask_ints(struct mantis_pci *mantis, u32 mask)
{
	unsigned long flags;

	spin_lock_irqsave(&mantis->intmask_lock, flags);
	mmwrite(mmread(MANTIS_INT_MASK) | mask, MANTIS_INT_MASK);
	spin_unlock_irqrestore(&mantis->intmask_lock, flags);
}

#endif /* __MANTIS_COMMON_H */
