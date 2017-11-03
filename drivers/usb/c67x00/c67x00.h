// SPDX-License-Identifier: GPL-2.0+
/*
 * c67x00.h: Cypress C67X00 USB register and field definitions
 *
 * Copyright (C) 2006-2008 Barco N.V.
 *    Derived from the Cypress cy7c67200/300 ezusb linux driver and
 *    based on multiple host controller drivers inside the linux kernel.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301  USA.
 */

#ifndef _USB_C67X00_H
#define _USB_C67X00_H

#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/completion.h>
#include <linux/mutex.h>

/* ---------------------------------------------------------------------
 * Cypress C67x00 register definitions
 */

/* Hardware Revision Register */
#define HW_REV_REG		0xC004

/* General USB registers */
/* ===================== */

/* USB Control Register */
#define USB_CTL_REG(x)		((x) ? 0xC0AA : 0xC08A)

#define LOW_SPEED_PORT(x)	((x) ? 0x0800 : 0x0400)
#define HOST_MODE		0x0200
#define PORT_RES_EN(x)		((x) ? 0x0100 : 0x0080)
#define SOF_EOP_EN(x)		((x) ? 0x0002 : 0x0001)

/* USB status register - Notice it has different content in hcd/udc mode */
#define USB_STAT_REG(x)		((x) ? 0xC0B0 : 0xC090)

#define EP0_IRQ_FLG		0x0001
#define EP1_IRQ_FLG		0x0002
#define EP2_IRQ_FLG		0x0004
#define EP3_IRQ_FLG		0x0008
#define EP4_IRQ_FLG		0x0010
#define EP5_IRQ_FLG		0x0020
#define EP6_IRQ_FLG		0x0040
#define EP7_IRQ_FLG		0x0080
#define RESET_IRQ_FLG		0x0100
#define SOF_EOP_IRQ_FLG		0x0200
#define ID_IRQ_FLG		0x4000
#define VBUS_IRQ_FLG		0x8000

/* USB Host only registers */
/* ======================= */

/* Host n Control Register */
#define HOST_CTL_REG(x)		((x) ? 0xC0A0 : 0xC080)

#define PREAMBLE_EN		0x0080	/* Preamble enable */
#define SEQ_SEL			0x0040	/* Data Toggle Sequence Bit Select */
#define ISO_EN			0x0010	/* Isochronous enable  */
#define ARM_EN			0x0001	/* Arm operation */

/* Host n Interrupt Enable Register */
#define HOST_IRQ_EN_REG(x)	((x) ? 0xC0AC : 0xC08C)

#define SOF_EOP_IRQ_EN		0x0200	/* SOF/EOP Interrupt Enable  */
#define SOF_EOP_TMOUT_IRQ_EN	0x0800	/* SOF/EOP Timeout Interrupt Enable  */
#define ID_IRQ_EN		0x4000	/* ID interrupt enable */
#define VBUS_IRQ_EN		0x8000	/* VBUS interrupt enable */
#define DONE_IRQ_EN		0x0001	/* Done Interrupt Enable  */

/* USB status register */
#define HOST_STAT_MASK		0x02FD
#define PORT_CONNECT_CHANGE(x)	((x) ? 0x0020 : 0x0010)
#define PORT_SE0_STATUS(x)	((x) ? 0x0008 : 0x0004)

/* Host Frame Register */
#define HOST_FRAME_REG(x)	((x) ? 0xC0B6 : 0xC096)

#define HOST_FRAME_MASK		0x07FF

/* USB Peripheral only registers */
/* ============================= */

/* Device n Port Sel reg */
#define DEVICE_N_PORT_SEL(x)	((x) ? 0xC0A4 : 0xC084)

/* Device n Interrupt Enable Register */
#define DEVICE_N_IRQ_EN_REG(x)	((x) ? 0xC0AC : 0xC08C)

#define DEVICE_N_ENDPOINT_N_CTL_REG(dev, ep)	((dev)  		\
						 ? (0x0280 + (ep << 4)) \
						 : (0x0200 + (ep << 4)))
#define DEVICE_N_ENDPOINT_N_STAT_REG(dev, ep)	((dev)			\
						 ? (0x0286 + (ep << 4)) \
						 : (0x0206 + (ep << 4)))

#define DEVICE_N_ADDRESS(dev)	((dev) ? (0xC0AE) : (0xC08E))

/* HPI registers */
/* ============= */

/* HPI Status register */
#define SOFEOP_FLG(x)		(1 << ((x) ? 12 : 10))
#define SIEMSG_FLG(x)		(1 << (4 + (x)))
#define RESET_FLG(x)		((x) ? 0x0200 : 0x0002)
#define DONE_FLG(x)		(1 << (2 + (x)))
#define RESUME_FLG(x)		(1 << (6 + (x)))
#define MBX_OUT_FLG		0x0001	/* Message out available */
#define MBX_IN_FLG		0x0100
#define ID_FLG			0x4000
#define VBUS_FLG		0x8000

/* Interrupt routing register */
#define HPI_IRQ_ROUTING_REG	0x0142

#define HPI_SWAP_ENABLE(x)	((x) ? 0x0100 : 0x0001)
#define RESET_TO_HPI_ENABLE(x)	((x) ? 0x0200 : 0x0002)
#define DONE_TO_HPI_ENABLE(x)	((x) ? 0x0008 : 0x0004)
#define RESUME_TO_HPI_ENABLE(x)	((x) ? 0x0080 : 0x0040)
#define SOFEOP_TO_HPI_EN(x)	((x) ? 0x2000 : 0x0800)
#define SOFEOP_TO_CPU_EN(x)	((x) ? 0x1000 : 0x0400)
#define ID_TO_HPI_ENABLE	0x4000
#define VBUS_TO_HPI_ENABLE	0x8000

/* SIE msg registers */
#define SIEMSG_REG(x)		((x) ? 0x0148 : 0x0144)

#define HUSB_TDListDone		0x1000

#define SUSB_EP0_MSG		0x0001
#define SUSB_EP1_MSG		0x0002
#define SUSB_EP2_MSG		0x0004
#define SUSB_EP3_MSG		0x0008
#define SUSB_EP4_MSG		0x0010
#define SUSB_EP5_MSG		0x0020
#define SUSB_EP6_MSG		0x0040
#define SUSB_EP7_MSG		0x0080
#define SUSB_RST_MSG		0x0100
#define SUSB_SOF_MSG		0x0200
#define SUSB_CFG_MSG		0x0400
#define SUSB_SUS_MSG		0x0800
#define SUSB_ID_MSG	       	0x4000
#define SUSB_VBUS_MSG		0x8000

/* BIOS interrupt routines */

#define SUSBx_RECEIVE_INT(x)	((x) ? 97 : 81)
#define SUSBx_SEND_INT(x)	((x) ? 96 : 80)

#define SUSBx_DEV_DESC_VEC(x)	((x) ? 0x00D4 : 0x00B4)
#define SUSBx_CONF_DESC_VEC(x)	((x) ? 0x00D6 : 0x00B6)
#define SUSBx_STRING_DESC_VEC(x) ((x) ? 0x00D8 : 0x00B8)

#define CY_HCD_BUF_ADDR		0x500	/* Base address for host */
#define SIE_TD_SIZE		0x200	/* size of the td list */
#define SIE_TD_BUF_SIZE		0x400	/* size of the data buffer */

#define SIE_TD_OFFSET(host)	((host) ? (SIE_TD_SIZE+SIE_TD_BUF_SIZE) : 0)
#define SIE_BUF_OFFSET(host)	(SIE_TD_OFFSET(host) + SIE_TD_SIZE)

/* Base address of HCD + 2 x TD_SIZE + 2 x TD_BUF_SIZE */
#define CY_UDC_REQ_HEADER_BASE	0x1100
/* 8- byte request headers for IN/OUT transfers */
#define CY_UDC_REQ_HEADER_SIZE	8

#define CY_UDC_REQ_HEADER_ADDR(ep_num)	(CY_UDC_REQ_HEADER_BASE + \
					 ((ep_num) * CY_UDC_REQ_HEADER_SIZE))
#define CY_UDC_DESC_BASE_ADDRESS	(CY_UDC_REQ_HEADER_ADDR(8))

#define CY_UDC_BIOS_REPLACE_BASE	0x1800
#define CY_UDC_REQ_BUFFER_BASE		0x2000
#define CY_UDC_REQ_BUFFER_SIZE		0x0400
#define CY_UDC_REQ_BUFFER_ADDR(ep_num)	(CY_UDC_REQ_BUFFER_BASE + \
					 ((ep_num) * CY_UDC_REQ_BUFFER_SIZE))

/* ---------------------------------------------------------------------
 * Driver data structures
 */

struct c67x00_device;

/**
 * struct c67x00_sie - Common data associated with a SIE
 * @lock: lock to protect this struct and the associated chip registers
 * @private_data: subdriver dependent data
 * @irq: subdriver dependent irq handler, set NULL when not used
 * @dev: link to common driver structure
 * @sie_num: SIE number on chip, starting from 0
 * @mode: SIE mode (host/peripheral/otg/not used)
 */
struct c67x00_sie {
	/* Entries to be used by the subdrivers */
	spinlock_t lock;	/* protect this structure */
	void *private_data;
	void (*irq) (struct c67x00_sie *sie, u16 int_status, u16 msg);

	/* Read only: */
	struct c67x00_device *dev;
	int sie_num;
	int mode;
};

#define sie_dev(s)	(&(s)->dev->pdev->dev)

/**
 * struct c67x00_lcp
 */
struct c67x00_lcp {
	/* Internal use only */
	struct mutex mutex;
	struct completion msg_received;
	u16 last_msg;
};

/*
 * struct c67x00_hpi
 */
struct c67x00_hpi {
	void __iomem *base;
	int regstep;
	spinlock_t lock;
	struct c67x00_lcp lcp;
};

#define C67X00_SIES	2
#define C67X00_PORTS	2

/**
 * struct c67x00_device - Common data associated with a c67x00 instance
 * @hpi: hpi addresses
 * @sie: array of sie's on this chip
 * @pdev: platform device of instance
 * @pdata: configuration provided by the platform
 */
struct c67x00_device {
	struct c67x00_hpi hpi;
	struct c67x00_sie sie[C67X00_SIES];
	struct platform_device *pdev;
	struct c67x00_platform_data *pdata;
};

/* ---------------------------------------------------------------------
 * Low level interface functions
 */

/* Host Port Interface (HPI) functions */
u16 c67x00_ll_hpi_status(struct c67x00_device *dev);
void c67x00_ll_hpi_reg_init(struct c67x00_device *dev);
void c67x00_ll_hpi_enable_sofeop(struct c67x00_sie *sie);
void c67x00_ll_hpi_disable_sofeop(struct c67x00_sie *sie);

/* General functions */
u16 c67x00_ll_fetch_siemsg(struct c67x00_device *dev, int sie_num);
u16 c67x00_ll_get_usb_ctl(struct c67x00_sie *sie);
void c67x00_ll_usb_clear_status(struct c67x00_sie *sie, u16 bits);
u16 c67x00_ll_usb_get_status(struct c67x00_sie *sie);
void c67x00_ll_write_mem_le16(struct c67x00_device *dev, u16 addr,
			      void *data, int len);
void c67x00_ll_read_mem_le16(struct c67x00_device *dev, u16 addr,
			     void *data, int len);

/* Host specific functions */
void c67x00_ll_set_husb_eot(struct c67x00_device *dev, u16 value);
void c67x00_ll_husb_reset(struct c67x00_sie *sie, int port);
void c67x00_ll_husb_set_current_td(struct c67x00_sie *sie, u16 addr);
u16 c67x00_ll_husb_get_current_td(struct c67x00_sie *sie);
u16 c67x00_ll_husb_get_frame(struct c67x00_sie *sie);
void c67x00_ll_husb_init_host_port(struct c67x00_sie *sie);
void c67x00_ll_husb_reset_port(struct c67x00_sie *sie, int port);

/* Called by c67x00_irq to handle lcp interrupts */
void c67x00_ll_irq(struct c67x00_device *dev, u16 int_status);

/* Setup and teardown */
void c67x00_ll_init(struct c67x00_device *dev);
void c67x00_ll_release(struct c67x00_device *dev);
int c67x00_ll_reset(struct c67x00_device *dev);

#endif				/* _USB_C67X00_H */
