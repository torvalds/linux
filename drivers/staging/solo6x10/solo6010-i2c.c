/*
 * Copyright (C) 2010 Bluecherry, LLC www.bluecherrydvr.com
 * Copyright (C) 2010 Ben Collins <bcollins@bluecherry.net>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* XXX: The SOLO6010 i2c does not have separate interrupts for each i2c
 * channel. The bus can only handle one i2c event at a time. The below handles
 * this all wrong. We should be using the status registers to see if the bus
 * is in use, and have a global lock to check the status register. Also,
 * the bulk of the work should be handled out-of-interrupt. The ugly loops
 * that occur during interrupt scare me. The ISR should merely signal
 * thread context, ACK the interrupt, and move on. -- BenC */

#include <linux/kernel.h>

#include "solo6010.h"

u8 solo_i2c_readbyte(struct solo6010_dev *solo_dev, int id, u8 addr, u8 off)
{
	struct i2c_msg msgs[2];
	u8 data;

	msgs[0].flags = 0;
	msgs[0].addr = addr;
	msgs[0].len = 1;
	msgs[0].buf = &off;

	msgs[1].flags = I2C_M_RD;
	msgs[1].addr = addr;
	msgs[1].len = 1;
	msgs[1].buf = &data;

	i2c_transfer(&solo_dev->i2c_adap[id], msgs, 2);

	return data;
}

void solo_i2c_writebyte(struct solo6010_dev *solo_dev, int id, u8 addr,
			u8 off, u8 data)
{
	struct i2c_msg msgs;
	u8 buf[2];

	buf[0] = off;
	buf[1] = data;
	msgs.flags = 0;
	msgs.addr = addr;
	msgs.len = 2;
	msgs.buf = buf;

	i2c_transfer(&solo_dev->i2c_adap[id], &msgs, 1);
}

static void solo_i2c_flush(struct solo6010_dev *solo_dev, int wr)
{
	u32 ctrl;

	ctrl = SOLO_IIC_CH_SET(solo_dev->i2c_id);

	if (solo_dev->i2c_state == IIC_STATE_START)
		ctrl |= SOLO_IIC_START;

	if (wr) {
		ctrl |= SOLO_IIC_WRITE;
	} else {
		ctrl |= SOLO_IIC_READ;
		if (!(solo_dev->i2c_msg->flags & I2C_M_NO_RD_ACK))
			ctrl |= SOLO_IIC_ACK_EN;
	}

	if (solo_dev->i2c_msg_ptr == solo_dev->i2c_msg->len)
		ctrl |= SOLO_IIC_STOP;

	solo_reg_write(solo_dev, SOLO_IIC_CTRL, ctrl);
}

static void solo_i2c_start(struct solo6010_dev *solo_dev)
{
	u32 addr = solo_dev->i2c_msg->addr << 1;

	if (solo_dev->i2c_msg->flags & I2C_M_RD)
		addr |= 1;

	solo_dev->i2c_state = IIC_STATE_START;
	solo_reg_write(solo_dev, SOLO_IIC_TXD, addr);
	solo_i2c_flush(solo_dev, 1);
}

static void solo_i2c_stop(struct solo6010_dev *solo_dev)
{
	solo6010_irq_off(solo_dev, SOLO_IRQ_IIC);
	solo_reg_write(solo_dev, SOLO_IIC_CTRL, 0);
	solo_dev->i2c_state = IIC_STATE_STOP;
	wake_up(&solo_dev->i2c_wait);
}

static int solo_i2c_handle_read(struct solo6010_dev *solo_dev)
{
prepare_read:
	if (solo_dev->i2c_msg_ptr != solo_dev->i2c_msg->len) {
		solo_i2c_flush(solo_dev, 0);
		return 0;
	}

	solo_dev->i2c_msg_ptr = 0;
	solo_dev->i2c_msg++;
	solo_dev->i2c_msg_num--;

	if (solo_dev->i2c_msg_num == 0) {
		solo_i2c_stop(solo_dev);
		return 0;
	}

	if (!(solo_dev->i2c_msg->flags & I2C_M_NOSTART)) {
		solo_i2c_start(solo_dev);
	} else {
		if (solo_dev->i2c_msg->flags & I2C_M_RD)
			goto prepare_read;
		else
			solo_i2c_stop(solo_dev);
	}

	return 0;
}

static int solo_i2c_handle_write(struct solo6010_dev *solo_dev)
{
retry_write:
	if (solo_dev->i2c_msg_ptr != solo_dev->i2c_msg->len) {
		solo_reg_write(solo_dev, SOLO_IIC_TXD,
			       solo_dev->i2c_msg->buf[solo_dev->i2c_msg_ptr]);
		solo_dev->i2c_msg_ptr++;
		solo_i2c_flush(solo_dev, 1);
		return 0;
	}

	solo_dev->i2c_msg_ptr = 0;
	solo_dev->i2c_msg++;
	solo_dev->i2c_msg_num--;

	if (solo_dev->i2c_msg_num == 0) {
		solo_i2c_stop(solo_dev);
		return 0;
	}

	if (!(solo_dev->i2c_msg->flags & I2C_M_NOSTART)) {
		solo_i2c_start(solo_dev);
	} else {
		if (solo_dev->i2c_msg->flags & I2C_M_RD)
			solo_i2c_stop(solo_dev);
		else
			goto retry_write;
	}

	return 0;
}

int solo_i2c_isr(struct solo6010_dev *solo_dev)
{
	u32 status = solo_reg_read(solo_dev, SOLO_IIC_CTRL);
	int ret = -EINVAL;

	solo_reg_write(solo_dev, SOLO_IRQ_STAT, SOLO_IRQ_IIC);

	if (status & (SOLO_IIC_STATE_TRNS & SOLO_IIC_STATE_SIG_ERR) ||
	    solo_dev->i2c_id < 0) {
		solo_i2c_stop(solo_dev);
		return -ENXIO;
	}

	switch (solo_dev->i2c_state) {
	case IIC_STATE_START:
		if (solo_dev->i2c_msg->flags & I2C_M_RD) {
			solo_dev->i2c_state = IIC_STATE_READ;
			ret = solo_i2c_handle_read(solo_dev);
			break;
		}

		solo_dev->i2c_state = IIC_STATE_WRITE;
	case IIC_STATE_WRITE:
		ret = solo_i2c_handle_write(solo_dev);
		break;

	case IIC_STATE_READ:
		solo_dev->i2c_msg->buf[solo_dev->i2c_msg_ptr] =
			solo_reg_read(solo_dev, SOLO_IIC_RXD);
		solo_dev->i2c_msg_ptr++;

		ret = solo_i2c_handle_read(solo_dev);
		break;

	default:
		solo_i2c_stop(solo_dev);
	}

	return ret;
}

static int solo_i2c_master_xfer(struct i2c_adapter *adap,
				struct i2c_msg msgs[], int num)
{
	struct solo6010_dev *solo_dev = adap->algo_data;
	unsigned long timeout;
	int ret;
	int i;
	DEFINE_WAIT(wait);

	for (i = 0; i < SOLO_I2C_ADAPTERS; i++) {
		if (&solo_dev->i2c_adap[i] == adap)
			break;
	}

	if (i == SOLO_I2C_ADAPTERS)
		return num; /* XXX Right return value for failure? */

	mutex_lock(&solo_dev->i2c_mutex);
	solo_dev->i2c_id = i;
	solo_dev->i2c_msg = msgs;
	solo_dev->i2c_msg_num = num;
	solo_dev->i2c_msg_ptr = 0;

	solo_reg_write(solo_dev, SOLO_IIC_CTRL, 0);
	solo6010_irq_on(solo_dev, SOLO_IRQ_IIC);
	solo_i2c_start(solo_dev);

	timeout = HZ / 2;

	for (;;) {
		prepare_to_wait(&solo_dev->i2c_wait, &wait, TASK_INTERRUPTIBLE);

		if (solo_dev->i2c_state == IIC_STATE_STOP)
			break;

		timeout = schedule_timeout(timeout);
		if (!timeout)
			break;

		if (signal_pending(current))
			break;
	}

	finish_wait(&solo_dev->i2c_wait, &wait);
	ret = num - solo_dev->i2c_msg_num;
	solo_dev->i2c_state = IIC_STATE_IDLE;
	solo_dev->i2c_id = -1;

	mutex_unlock(&solo_dev->i2c_mutex);

	return ret;
}

static u32 solo_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm solo_i2c_algo = {
	.master_xfer	= solo_i2c_master_xfer,
	.functionality	= solo_i2c_functionality,
};

int solo_i2c_init(struct solo6010_dev *solo_dev)
{
	int i;
	int ret;

	solo_reg_write(solo_dev, SOLO_IIC_CFG,
		       SOLO_IIC_PRESCALE(8) | SOLO_IIC_ENABLE);

	solo_dev->i2c_id = -1;
	solo_dev->i2c_state = IIC_STATE_IDLE;
	init_waitqueue_head(&solo_dev->i2c_wait);
	mutex_init(&solo_dev->i2c_mutex);

	for (i = 0; i < SOLO_I2C_ADAPTERS; i++) {
		struct i2c_adapter *adap = &solo_dev->i2c_adap[i];

		snprintf(adap->name, I2C_NAME_SIZE, "%s I2C %d",
			 SOLO6010_NAME, i);
		adap->algo = &solo_i2c_algo;
		adap->algo_data = solo_dev;
		adap->retries = 1;
		adap->dev.parent = &solo_dev->pdev->dev;

		ret = i2c_add_adapter(adap);
		if (ret) {
			adap->algo_data = NULL;
			break;
		}
	}

	if (ret) {
		for (i = 0; i < SOLO_I2C_ADAPTERS; i++) {
			if (!solo_dev->i2c_adap[i].algo_data)
				break;
			i2c_del_adapter(&solo_dev->i2c_adap[i]);
			solo_dev->i2c_adap[i].algo_data = NULL;
		}
		return ret;
	}

	dev_info(&solo_dev->pdev->dev, "Enabled %d i2c adapters\n",
		 SOLO_I2C_ADAPTERS);

	return 0;
}

void solo_i2c_exit(struct solo6010_dev *solo_dev)
{
	int i;

	for (i = 0; i < SOLO_I2C_ADAPTERS; i++) {
		if (!solo_dev->i2c_adap[i].algo_data)
			continue;
		i2c_del_adapter(&solo_dev->i2c_adap[i]);
		solo_dev->i2c_adap[i].algo_data = NULL;
	}
}
