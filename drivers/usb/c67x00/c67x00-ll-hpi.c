/*
 * c67x00-ll-hpi.c: Cypress C67X00 USB Low level interface using HPI
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

#include <asm/byteorder.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/usb/c67x00.h>
#include "c67x00.h"

#define COMM_REGS 14

struct c67x00_lcp_int_data {
	u16 regs[COMM_REGS];
};

/* -------------------------------------------------------------------------- */
/* Interface definitions */

#define COMM_ACK			0x0FED
#define COMM_NAK			0xDEAD

#define COMM_RESET			0xFA50
#define COMM_EXEC_INT			0xCE01
#define COMM_INT_NUM			0x01C2

/* Registers 0 to COMM_REGS-1 */
#define COMM_R(x)			(0x01C4 + 2 * (x))

#define HUSB_SIE_pCurrentTDPtr(x)	((x) ? 0x01B2 : 0x01B0)
#define HUSB_SIE_pTDListDone_Sem(x)	((x) ? 0x01B8 : 0x01B6)
#define HUSB_pEOT			0x01B4

/* Software interrupts */
/* 114, 115: */
#define HUSB_SIE_INIT_INT(x)		((x) ? 0x0073 : 0x0072)
#define HUSB_RESET_INT			0x0074

#define SUSB_INIT_INT			0x0071
#define SUSB_INIT_INT_LOC		(SUSB_INIT_INT * 2)

/* -----------------------------------------------------------------------
 * HPI implementation
 *
 * The c67x00 chip also support control via SPI or HSS serial
 * interfaces.  However, this driver assumes that register access can
 * be performed from IRQ context.  While this is a safe assuption with
 * the HPI interface, it is not true for the serial interfaces.
 */

/* HPI registers */
#define HPI_DATA	0
#define HPI_MAILBOX	1
#define HPI_ADDR	2
#define HPI_STATUS	3

static inline u16 hpi_read_reg(struct c67x00_device *dev, int reg)
{
	return __raw_readw(dev->hpi.base + reg * dev->hpi.regstep);
}

static inline void hpi_write_reg(struct c67x00_device *dev, int reg, u16 value)
{
	__raw_writew(value, dev->hpi.base + reg * dev->hpi.regstep);
}

static inline u16 hpi_read_word_nolock(struct c67x00_device *dev, u16 reg)
{
	hpi_write_reg(dev, HPI_ADDR, reg);
	return hpi_read_reg(dev, HPI_DATA);
}

static u16 hpi_read_word(struct c67x00_device *dev, u16 reg)
{
	u16 value;
	unsigned long flags;

	spin_lock_irqsave(&dev->hpi.lock, flags);
	value = hpi_read_word_nolock(dev, reg);
	spin_unlock_irqrestore(&dev->hpi.lock, flags);

	return value;
}

static void hpi_write_word_nolock(struct c67x00_device *dev, u16 reg, u16 value)
{
	hpi_write_reg(dev, HPI_ADDR, reg);
	hpi_write_reg(dev, HPI_DATA, value);
}

static void hpi_write_word(struct c67x00_device *dev, u16 reg, u16 value)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->hpi.lock, flags);
	hpi_write_word_nolock(dev, reg, value);
	spin_unlock_irqrestore(&dev->hpi.lock, flags);
}

/*
 * Only data is little endian, addr has cpu endianess
 */
static void hpi_write_words_le16(struct c67x00_device *dev, u16 addr,
				 __le16 *data, u16 count)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&dev->hpi.lock, flags);

	hpi_write_reg(dev, HPI_ADDR, addr);
	for (i = 0; i < count; i++)
		hpi_write_reg(dev, HPI_DATA, le16_to_cpu(*data++));

	spin_unlock_irqrestore(&dev->hpi.lock, flags);
}

/*
 * Only data is little endian, addr has cpu endianess
 */
static void hpi_read_words_le16(struct c67x00_device *dev, u16 addr,
				__le16 *data, u16 count)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&dev->hpi.lock, flags);
	hpi_write_reg(dev, HPI_ADDR, addr);
	for (i = 0; i < count; i++)
		*data++ = cpu_to_le16(hpi_read_reg(dev, HPI_DATA));

	spin_unlock_irqrestore(&dev->hpi.lock, flags);
}

static void hpi_set_bits(struct c67x00_device *dev, u16 reg, u16 mask)
{
	u16 value;
	unsigned long flags;

	spin_lock_irqsave(&dev->hpi.lock, flags);
	value = hpi_read_word_nolock(dev, reg);
	hpi_write_word_nolock(dev, reg, value | mask);
	spin_unlock_irqrestore(&dev->hpi.lock, flags);
}

static void hpi_clear_bits(struct c67x00_device *dev, u16 reg, u16 mask)
{
	u16 value;
	unsigned long flags;

	spin_lock_irqsave(&dev->hpi.lock, flags);
	value = hpi_read_word_nolock(dev, reg);
	hpi_write_word_nolock(dev, reg, value & ~mask);
	spin_unlock_irqrestore(&dev->hpi.lock, flags);
}

static u16 hpi_recv_mbox(struct c67x00_device *dev)
{
	u16 value;
	unsigned long flags;

	spin_lock_irqsave(&dev->hpi.lock, flags);
	value = hpi_read_reg(dev, HPI_MAILBOX);
	spin_unlock_irqrestore(&dev->hpi.lock, flags);

	return value;
}

static u16 hpi_send_mbox(struct c67x00_device *dev, u16 value)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->hpi.lock, flags);
	hpi_write_reg(dev, HPI_MAILBOX, value);
	spin_unlock_irqrestore(&dev->hpi.lock, flags);

	return value;
}

u16 c67x00_ll_hpi_status(struct c67x00_device *dev)
{
	u16 value;
	unsigned long flags;

	spin_lock_irqsave(&dev->hpi.lock, flags);
	value = hpi_read_reg(dev, HPI_STATUS);
	spin_unlock_irqrestore(&dev->hpi.lock, flags);

	return value;
}

void c67x00_ll_hpi_reg_init(struct c67x00_device *dev)
{
	int i;

	hpi_recv_mbox(dev);
	c67x00_ll_hpi_status(dev);
	hpi_write_word(dev, HPI_IRQ_ROUTING_REG, 0);

	for (i = 0; i < C67X00_SIES; i++) {
		hpi_write_word(dev, SIEMSG_REG(i), 0);
		hpi_read_word(dev, SIEMSG_REG(i));
	}
}

void c67x00_ll_hpi_enable_sofeop(struct c67x00_sie *sie)
{
	hpi_set_bits(sie->dev, HPI_IRQ_ROUTING_REG,
		     SOFEOP_TO_HPI_EN(sie->sie_num));
}

void c67x00_ll_hpi_disable_sofeop(struct c67x00_sie *sie)
{
	hpi_clear_bits(sie->dev, HPI_IRQ_ROUTING_REG,
		       SOFEOP_TO_HPI_EN(sie->sie_num));
}

/* -------------------------------------------------------------------------- */
/* Transactions */

static inline int ll_recv_msg(struct c67x00_device *dev)
{
	u16 res;

	res = wait_for_completion_timeout(&dev->hpi.lcp.msg_received, 5 * HZ);
	WARN_ON(!res);

	return (res == 0) ? -EIO : 0;
}

/* -------------------------------------------------------------------------- */
/* General functions */

u16 c67x00_ll_fetch_siemsg(struct c67x00_device *dev, int sie_num)
{
	u16 val;

	val = hpi_read_word(dev, SIEMSG_REG(sie_num));
	/* clear register to allow next message */
	hpi_write_word(dev, SIEMSG_REG(sie_num), 0);

	return val;
}

u16 c67x00_ll_get_usb_ctl(struct c67x00_sie *sie)
{
	return hpi_read_word(sie->dev, USB_CTL_REG(sie->sie_num));
}

/**
 * c67x00_ll_usb_clear_status - clear the USB status bits
 */
void c67x00_ll_usb_clear_status(struct c67x00_sie *sie, u16 bits)
{
	hpi_write_word(sie->dev, USB_STAT_REG(sie->sie_num), bits);
}

u16 c67x00_ll_usb_get_status(struct c67x00_sie *sie)
{
	return hpi_read_word(sie->dev, USB_STAT_REG(sie->sie_num));
}

/* -------------------------------------------------------------------------- */

static int c67x00_comm_exec_int(struct c67x00_device *dev, u16 nr,
				struct c67x00_lcp_int_data *data)
{
	int i, rc;

	mutex_lock(&dev->hpi.lcp.mutex);
	hpi_write_word(dev, COMM_INT_NUM, nr);
	for (i = 0; i < COMM_REGS; i++)
		hpi_write_word(dev, COMM_R(i), data->regs[i]);
	hpi_send_mbox(dev, COMM_EXEC_INT);
	rc = ll_recv_msg(dev);
	mutex_unlock(&dev->hpi.lcp.mutex);

	return rc;
}

/* -------------------------------------------------------------------------- */
/* Host specific functions */

void c67x00_ll_set_husb_eot(struct c67x00_device *dev, u16 value)
{
	mutex_lock(&dev->hpi.lcp.mutex);
	hpi_write_word(dev, HUSB_pEOT, value);
	mutex_unlock(&dev->hpi.lcp.mutex);
}

static inline void c67x00_ll_husb_sie_init(struct c67x00_sie *sie)
{
	struct c67x00_device *dev = sie->dev;
	struct c67x00_lcp_int_data data;
	int rc;

	rc = c67x00_comm_exec_int(dev, HUSB_SIE_INIT_INT(sie->sie_num), &data);
	BUG_ON(rc); /* No return path for error code; crash spectacularly */
}

void c67x00_ll_husb_reset(struct c67x00_sie *sie, int port)
{
	struct c67x00_device *dev = sie->dev;
	struct c67x00_lcp_int_data data;
	int rc;

	data.regs[0] = 50;	/* Reset USB port for 50ms */
	data.regs[1] = port | (sie->sie_num << 1);
	rc = c67x00_comm_exec_int(dev, HUSB_RESET_INT, &data);
	BUG_ON(rc); /* No return path for error code; crash spectacularly */
}

void c67x00_ll_husb_set_current_td(struct c67x00_sie *sie, u16 addr)
{
	hpi_write_word(sie->dev, HUSB_SIE_pCurrentTDPtr(sie->sie_num), addr);
}

u16 c67x00_ll_husb_get_current_td(struct c67x00_sie *sie)
{
	return hpi_read_word(sie->dev, HUSB_SIE_pCurrentTDPtr(sie->sie_num));
}

u16 c67x00_ll_husb_get_frame(struct c67x00_sie *sie)
{
	return hpi_read_word(sie->dev, HOST_FRAME_REG(sie->sie_num));
}

void c67x00_ll_husb_init_host_port(struct c67x00_sie *sie)
{
	/* Set port into host mode */
	hpi_set_bits(sie->dev, USB_CTL_REG(sie->sie_num), HOST_MODE);
	c67x00_ll_husb_sie_init(sie);
	/* Clear interrupts */
	c67x00_ll_usb_clear_status(sie, HOST_STAT_MASK);
	/* Check */
	if (!(hpi_read_word(sie->dev, USB_CTL_REG(sie->sie_num)) & HOST_MODE))
		dev_warn(sie_dev(sie),
			 "SIE %d not set to host mode\n", sie->sie_num);
}

void c67x00_ll_husb_reset_port(struct c67x00_sie *sie, int port)
{
	/* Clear connect change */
	c67x00_ll_usb_clear_status(sie, PORT_CONNECT_CHANGE(port));

	/* Enable interrupts */
	hpi_set_bits(sie->dev, HPI_IRQ_ROUTING_REG,
		     SOFEOP_TO_CPU_EN(sie->sie_num));
	hpi_set_bits(sie->dev, HOST_IRQ_EN_REG(sie->sie_num),
		     SOF_EOP_IRQ_EN | DONE_IRQ_EN);

	/* Enable pull down transistors */
	hpi_set_bits(sie->dev, USB_CTL_REG(sie->sie_num), PORT_RES_EN(port));
}

/* -------------------------------------------------------------------------- */

void c67x00_ll_irq(struct c67x00_device *dev, u16 int_status)
{
	if ((int_status & MBX_OUT_FLG) == 0)
		return;

	dev->hpi.lcp.last_msg = hpi_recv_mbox(dev);
	complete(&dev->hpi.lcp.msg_received);
}

/* -------------------------------------------------------------------------- */

int c67x00_ll_reset(struct c67x00_device *dev)
{
	int rc;

	mutex_lock(&dev->hpi.lcp.mutex);
	hpi_send_mbox(dev, COMM_RESET);
	rc = ll_recv_msg(dev);
	mutex_unlock(&dev->hpi.lcp.mutex);

	return rc;
}

/* -------------------------------------------------------------------------- */

/**
 * c67x00_ll_write_mem_le16 - write into c67x00 memory
 * Only data is little endian, addr has cpu endianess.
 */
void c67x00_ll_write_mem_le16(struct c67x00_device *dev, u16 addr,
			      void *data, int len)
{
	u8 *buf = data;

	/* Sanity check */
	if (addr + len > 0xffff) {
		dev_err(&dev->pdev->dev,
			"Trying to write beyond writable region!\n");
		return;
	}

	if (addr & 0x01) {
		/* unaligned access */
		u16 tmp;
		tmp = hpi_read_word(dev, addr - 1);
		tmp = (tmp & 0x00ff) | (*buf++ << 8);
		hpi_write_word(dev, addr - 1, tmp);
		addr++;
		len--;
	}

	hpi_write_words_le16(dev, addr, (__le16 *)buf, len / 2);
	buf += len & ~0x01;
	addr += len & ~0x01;
	len &= 0x01;

	if (len) {
		u16 tmp;
		tmp = hpi_read_word(dev, addr);
		tmp = (tmp & 0xff00) | *buf;
		hpi_write_word(dev, addr, tmp);
	}
}

/**
 * c67x00_ll_read_mem_le16 - read from c67x00 memory
 * Only data is little endian, addr has cpu endianess.
 */
void c67x00_ll_read_mem_le16(struct c67x00_device *dev, u16 addr,
			     void *data, int len)
{
	u8 *buf = data;

	if (addr & 0x01) {
		/* unaligned access */
		u16 tmp;
		tmp = hpi_read_word(dev, addr - 1);
		*buf++ = (tmp >> 8) & 0x00ff;
		addr++;
		len--;
	}

	hpi_read_words_le16(dev, addr, (__le16 *)buf, len / 2);
	buf += len & ~0x01;
	addr += len & ~0x01;
	len &= 0x01;

	if (len) {
		u16 tmp;
		tmp = hpi_read_word(dev, addr);
		*buf = tmp & 0x00ff;
	}
}

/* -------------------------------------------------------------------------- */

void c67x00_ll_init(struct c67x00_device *dev)
{
	mutex_init(&dev->hpi.lcp.mutex);
	init_completion(&dev->hpi.lcp.msg_received);
}

void c67x00_ll_release(struct c67x00_device *dev)
{
}
