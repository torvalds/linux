/*
 * Earthsoft PT3 driver
 *
 * Copyright (C) 2014 Akihiro Tsukada <tskd08@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/pci.h>

#include "pt3.h"

#define PT3_I2C_BASE  2048
#define PT3_CMD_ADDR_NORMAL 0
#define PT3_CMD_ADDR_INIT_DEMOD  4096
#define PT3_CMD_ADDR_INIT_TUNER  (4096 + 2042)

/* masks for I2C status register */
#define STAT_SEQ_RUNNING 0x1
#define STAT_SEQ_ERROR   0x6
#define STAT_NO_SEQ      0x8

#define PT3_I2C_RUN   (1 << 16)
#define PT3_I2C_RESET (1 << 17)

enum ctl_cmd {
	I_END,
	I_ADDRESS,
	I_CLOCK_L,
	I_CLOCK_H,
	I_DATA_L,
	I_DATA_H,
	I_RESET,
	I_SLEEP,
	I_DATA_L_NOP  = 0x08,
	I_DATA_H_NOP  = 0x0c,
	I_DATA_H_READ = 0x0d,
	I_DATA_H_ACK0 = 0x0e,
	I_DATA_H_ACK1 = 0x0f,
};


static void cmdbuf_add(struct pt3_i2cbuf *cbuf, enum ctl_cmd cmd)
{
	int buf_idx;

	if ((cbuf->num_cmds % 2) == 0)
		cbuf->tmp = cmd;
	else {
		cbuf->tmp |= cmd << 4;
		buf_idx = cbuf->num_cmds / 2;
		if (buf_idx < ARRAY_SIZE(cbuf->data))
			cbuf->data[buf_idx] = cbuf->tmp;
	}
	cbuf->num_cmds++;
}

static void put_end(struct pt3_i2cbuf *cbuf)
{
	cmdbuf_add(cbuf, I_END);
	if (cbuf->num_cmds % 2)
		cmdbuf_add(cbuf, I_END);
}

static void put_start(struct pt3_i2cbuf *cbuf)
{
	cmdbuf_add(cbuf, I_DATA_H);
	cmdbuf_add(cbuf, I_CLOCK_H);
	cmdbuf_add(cbuf, I_DATA_L);
	cmdbuf_add(cbuf, I_CLOCK_L);
}

static void put_byte_write(struct pt3_i2cbuf *cbuf, u8 val)
{
	u8 mask;

	mask = 0x80;
	for (mask = 0x80; mask > 0; mask >>= 1)
		cmdbuf_add(cbuf, (val & mask) ? I_DATA_H_NOP : I_DATA_L_NOP);
	cmdbuf_add(cbuf, I_DATA_H_ACK0);
}

static void put_byte_read(struct pt3_i2cbuf *cbuf, u32 size)
{
	int i, j;

	for (i = 0; i < size; i++) {
		for (j = 0; j < 8; j++)
			cmdbuf_add(cbuf, I_DATA_H_READ);
		cmdbuf_add(cbuf, (i == size - 1) ? I_DATA_H_NOP : I_DATA_L_NOP);
	}
}

static void put_stop(struct pt3_i2cbuf *cbuf)
{
	cmdbuf_add(cbuf, I_DATA_L);
	cmdbuf_add(cbuf, I_CLOCK_H);
	cmdbuf_add(cbuf, I_DATA_H);
}


/* translates msgs to internal commands for bit-banging */
static void translate(struct pt3_i2cbuf *cbuf, struct i2c_msg *msgs, int num)
{
	int i, j;
	bool rd;

	cbuf->num_cmds = 0;
	for (i = 0; i < num; i++) {
		rd = !!(msgs[i].flags & I2C_M_RD);
		put_start(cbuf);
		put_byte_write(cbuf, msgs[i].addr << 1 | rd);
		if (rd)
			put_byte_read(cbuf, msgs[i].len);
		else
			for (j = 0; j < msgs[i].len; j++)
				put_byte_write(cbuf, msgs[i].buf[j]);
	}
	if (num > 0) {
		put_stop(cbuf);
		put_end(cbuf);
	}
}

static int wait_i2c_result(struct pt3_board *pt3, u32 *result, int max_wait)
{
	int i;
	u32 v;

	for (i = 0; i < max_wait; i++) {
		v = ioread32(pt3->regs[0] + REG_I2C_R);
		if (!(v & STAT_SEQ_RUNNING))
			break;
		usleep_range(500, 750);
	}
	if (i >= max_wait)
		return -EIO;
	if (result)
		*result = v;
	return 0;
}

/* send [pre-]translated i2c msgs stored at addr */
static int send_i2c_cmd(struct pt3_board *pt3, u32 addr)
{
	u32 ret;

	/* make sure that previous transactions had finished */
	if (wait_i2c_result(pt3, NULL, 50)) {
		dev_warn(&pt3->pdev->dev, "(%s) prev. transaction stalled\n",
				__func__);
		return -EIO;
	}

	iowrite32(PT3_I2C_RUN | addr, pt3->regs[0] + REG_I2C_W);
	usleep_range(200, 300);
	/* wait for the current transaction to finish */
	if (wait_i2c_result(pt3, &ret, 500) || (ret & STAT_SEQ_ERROR)) {
		dev_warn(&pt3->pdev->dev, "(%s) failed.\n", __func__);
		return -EIO;
	}
	return 0;
}


/* init commands for each demod are combined into one transaction
 *  and hidden in ROM with the address PT3_CMD_ADDR_INIT_DEMOD.
 */
int  pt3_init_all_demods(struct pt3_board *pt3)
{
	ioread32(pt3->regs[0] + REG_I2C_R);
	return send_i2c_cmd(pt3, PT3_CMD_ADDR_INIT_DEMOD);
}

/* init commands for two ISDB-T tuners are hidden in ROM. */
int  pt3_init_all_mxl301rf(struct pt3_board *pt3)
{
	usleep_range(1000, 2000);
	return send_i2c_cmd(pt3, PT3_CMD_ADDR_INIT_TUNER);
}

void pt3_i2c_reset(struct pt3_board *pt3)
{
	iowrite32(PT3_I2C_RESET, pt3->regs[0] + REG_I2C_W);
}

/*
 * I2C algorithm
 */
int
pt3_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct pt3_board *pt3;
	struct pt3_i2cbuf *cbuf;
	int i;
	void __iomem *p;

	pt3 = i2c_get_adapdata(adap);
	cbuf = pt3->i2c_buf;

	for (i = 0; i < num; i++)
		if (msgs[i].flags & I2C_M_RECV_LEN) {
			dev_warn(&pt3->pdev->dev,
				"(%s) I2C_M_RECV_LEN not supported.\n",
				__func__);
			return -EINVAL;
		}

	translate(cbuf, msgs, num);
	memcpy_toio(pt3->regs[1] + PT3_I2C_BASE + PT3_CMD_ADDR_NORMAL / 2,
			cbuf->data, cbuf->num_cmds);

	if (send_i2c_cmd(pt3, PT3_CMD_ADDR_NORMAL) < 0)
		return -EIO;

	p = pt3->regs[1] + PT3_I2C_BASE;
	for (i = 0; i < num; i++)
		if ((msgs[i].flags & I2C_M_RD) && msgs[i].len > 0) {
			memcpy_fromio(msgs[i].buf, p, msgs[i].len);
			p += msgs[i].len;
		}

	return num;
}

u32 pt3_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C;
}
