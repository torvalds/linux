#ifndef P54USB_H
#define P54USB_H

/*
 * Defines for USB based mac80211 Prism54 driver
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 *
 * Based on the islsm (softmac prism54) driver, which is:
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* for isl3886 register definitions used on ver 1 devices */
#include "p54pci.h"
#include "net2280.h"

/* pci */
#define NET2280_BASE		0x10000000
#define NET2280_BASE2		0x20000000

/* gpio */
#define P54U_BRG_POWER_UP	(1 << GPIO0_DATA)
#define P54U_BRG_POWER_DOWN	(1 << GPIO1_DATA)

/* devinit */
#define NET2280_CLK_4Mhz	(15 << LOCAL_CLOCK_FREQUENCY)
#define NET2280_CLK_30Mhz	(2 << LOCAL_CLOCK_FREQUENCY)
#define NET2280_CLK_60Mhz	(1 << LOCAL_CLOCK_FREQUENCY)
#define NET2280_CLK_STOP	(0 << LOCAL_CLOCK_FREQUENCY)
#define NET2280_PCI_ENABLE	(1 << PCI_ENABLE)
#define NET2280_PCI_SOFT_RESET	(1 << PCI_SOFT_RESET)

/* endpoints */
#define NET2280_CLEAR_NAK_OUT_PACKETS_MODE	(1 << CLEAR_NAK_OUT_PACKETS_MODE)
#define NET2280_FIFO_FLUSH			(1 << FIFO_FLUSH)

/* irq */
#define NET2280_USB_INTERRUPT_ENABLE		(1 << USB_INTERRUPT_ENABLE)
#define NET2280_PCI_INTA_INTERRUPT		(1 << PCI_INTA_INTERRUPT)
#define NET2280_PCI_INTA_INTERRUPT_ENABLE	(1 << PCI_INTA_INTERRUPT_ENABLE)

/* registers */
#define NET2280_DEVINIT		0x00
#define NET2280_USBIRQENB1	0x24
#define NET2280_IRQSTAT1	0x2c
#define NET2280_FIFOCTL         0x38
#define NET2280_GPIOCTL		0x50
#define NET2280_RELNUM		0x88
#define NET2280_EPA_RSP		0x324
#define NET2280_EPA_STAT	0x32c
#define NET2280_EPB_STAT	0x34c
#define NET2280_EPC_RSP		0x364
#define NET2280_EPC_STAT	0x36c
#define NET2280_EPD_STAT	0x38c

#define NET2280_EPA_CFG     0x320
#define NET2280_EPB_CFG     0x340
#define NET2280_EPC_CFG     0x360
#define NET2280_EPD_CFG     0x380
#define NET2280_EPE_CFG     0x3A0
#define NET2280_EPF_CFG     0x3C0
#define P54U_DEV_BASE 0x40000000

struct net2280_tx_hdr {
	__le32 device_addr;
	__le16 len;
	__le16 follower;	/* ? */
	u8 padding[8];
} __attribute__((packed));

struct lm87_tx_hdr {
	__le32 device_addr;
	__le32 chksum;
} __attribute__((packed));

/* Some flags for the isl hardware registers controlling DMA inside the
 * chip */
#define ISL38XX_DMA_STATUS_DONE			0x00000001
#define ISL38XX_DMA_STATUS_READY		0x00000002
#define NET2280_EPA_FIFO_PCI_ADDR		0x20000000
#define ISL38XX_DMA_MASTER_CONTROL_TRIGGER	0x00000004

enum net2280_op_type {
	NET2280_BRG_U32		= 0x001F,
	NET2280_BRG_CFG_U32	= 0x000F,
	NET2280_BRG_CFG_U16	= 0x0003,
	NET2280_DEV_U32		= 0x080F,
	NET2280_DEV_CFG_U32	= 0x088F,
	NET2280_DEV_CFG_U16	= 0x0883
};

#define P54U_FW_BLOCK 2048

#define X2_SIGNATURE "x2  "
#define X2_SIGNATURE_SIZE 4

struct x2_header {
	u8 signature[X2_SIGNATURE_SIZE];
	__le32 fw_load_addr;
	__le32 fw_length;
	__le32 crc;
} __attribute__((packed));

/* pipes 3 and 4 are not used by the driver */
#define P54U_PIPE_NUMBER 9

enum p54u_pipe_addr {
        P54U_PIPE_DATA = 0x01,
        P54U_PIPE_MGMT = 0x02,
        P54U_PIPE_3 = 0x03,
        P54U_PIPE_4 = 0x04,
        P54U_PIPE_BRG = 0x0d,
        P54U_PIPE_DEV = 0x0e,
        P54U_PIPE_INT = 0x0f
};

struct p54u_rx_info {
	struct urb *urb;
	struct ieee80211_hw *dev;
};

struct p54u_priv {
	struct p54_common common;
	struct usb_device *udev;
	enum {
		P54U_NET2280 = 0,
		P54U_3887
	} hw_type;

	spinlock_t lock;
	struct sk_buff_head rx_queue;
};

#endif /* P54USB_H */
