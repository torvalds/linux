/*
 * Basic KB3310B Embedded Controller support for the YeeLoong 2F netbook
 *
 *  Copyright (C) 2008 Lemote Inc.
 *  Author: liujl <liujl@lemote.com>, 2008-04-20
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/delay.h>

#include "ec_kb3310b.h"

static DEFINE_SPINLOCK(index_access_lock);
static DEFINE_SPINLOCK(port_access_lock);

unsigned char ec_read(unsigned short addr)
{
	unsigned char value;
	unsigned long flags;

	spin_lock_irqsave(&index_access_lock, flags);
	outb((addr & 0xff00) >> 8, EC_IO_PORT_HIGH);
	outb((addr & 0x00ff), EC_IO_PORT_LOW);
	value = inb(EC_IO_PORT_DATA);
	spin_unlock_irqrestore(&index_access_lock, flags);

	return value;
}
EXPORT_SYMBOL_GPL(ec_read);

void ec_write(unsigned short addr, unsigned char val)
{
	unsigned long flags;

	spin_lock_irqsave(&index_access_lock, flags);
	outb((addr & 0xff00) >> 8, EC_IO_PORT_HIGH);
	outb((addr & 0x00ff), EC_IO_PORT_LOW);
	outb(val, EC_IO_PORT_DATA);
	/*  flush the write action */
	inb(EC_IO_PORT_DATA);
	spin_unlock_irqrestore(&index_access_lock, flags);
}
EXPORT_SYMBOL_GPL(ec_write);

/*
 * This function is used for EC command writes and corresponding status queries.
 */
int ec_query_seq(unsigned char cmd)
{
	int timeout;
	unsigned char status;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&port_access_lock, flags);

	/* make chip goto reset mode */
	udelay(EC_REG_DELAY);
	outb(cmd, EC_CMD_PORT);
	udelay(EC_REG_DELAY);

	/* check if the command is received by ec */
	timeout = EC_CMD_TIMEOUT;
	status = inb(EC_STS_PORT);
	while (timeout-- && (status & (1 << 1))) {
		status = inb(EC_STS_PORT);
		udelay(EC_REG_DELAY);
	}

	spin_unlock_irqrestore(&port_access_lock, flags);

	if (timeout <= 0) {
		printk(KERN_ERR "%s: deadable error : timeout...\n", __func__);
		ret = -EINVAL;
	} else
		printk(KERN_INFO
			   "(%x/%d)ec issued command %d status : 0x%x\n",
			   timeout, EC_CMD_TIMEOUT - timeout, cmd, status);

	return ret;
}
EXPORT_SYMBOL_GPL(ec_query_seq);

/*
 * Send query command to EC to get the proper event number
 */
int ec_query_event_num(void)
{
	return ec_query_seq(CMD_GET_EVENT_NUM);
}
EXPORT_SYMBOL(ec_query_event_num);

/*
 * Get event number from EC
 *
 * NOTE: This routine must follow the query_event_num function in the
 * interrupt.
 */
int ec_get_event_num(void)
{
	int timeout = 100;
	unsigned char value;
	unsigned char status;

	udelay(EC_REG_DELAY);
	status = inb(EC_STS_PORT);
	udelay(EC_REG_DELAY);
	while (timeout-- && !(status & (1 << 0))) {
		status = inb(EC_STS_PORT);
		udelay(EC_REG_DELAY);
	}
	if (timeout <= 0) {
		pr_info("%s: get event number timeout.\n", __func__);

		return -EINVAL;
	}
	value = inb(EC_DAT_PORT);
	udelay(EC_REG_DELAY);

	return value;
}
EXPORT_SYMBOL(ec_get_event_num);
