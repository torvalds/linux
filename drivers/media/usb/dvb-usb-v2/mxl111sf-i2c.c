/*
 *  mxl111sf-i2c.c - driver for the MaxLinear MXL111SF
 *
 *  Copyright (C) 2010-2014 Michael Krufky <mkrufky@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "mxl111sf-i2c.h"
#include "mxl111sf.h"

/* SW-I2C ----------------------------------------------------------------- */

#define SW_I2C_ADDR		0x1a
#define SW_I2C_EN		0x02
#define SW_SCL_OUT		0x04
#define SW_SDA_OUT		0x08
#define SW_SDA_IN		0x04

#define SW_I2C_BUSY_ADDR	0x2f
#define SW_I2C_BUSY		0x02

static int mxl111sf_i2c_bitbang_sendbyte(struct mxl111sf_state *state,
					 u8 byte)
{
	int i, ret;
	u8 data = 0;

	mxl_i2c("(0x%02x)", byte);

	ret = mxl111sf_read_reg(state, SW_I2C_BUSY_ADDR, &data);
	if (mxl_fail(ret))
		goto fail;

	for (i = 0; i < 8; i++) {

		data = (byte & (0x80 >> i)) ? SW_SDA_OUT : 0;

		ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
					 0x10 | SW_I2C_EN | data);
		if (mxl_fail(ret))
			goto fail;

		ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
					 0x10 | SW_I2C_EN | data | SW_SCL_OUT);
		if (mxl_fail(ret))
			goto fail;

		ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
					 0x10 | SW_I2C_EN | data);
		if (mxl_fail(ret))
			goto fail;
	}

	/* last bit was 0 so we need to release SDA */
	if (!(byte & 1)) {
		ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
					 0x10 | SW_I2C_EN | SW_SDA_OUT);
		if (mxl_fail(ret))
			goto fail;
	}

	/* CLK high for ACK readback */
	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SCL_OUT | SW_SDA_OUT);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_read_reg(state, SW_I2C_BUSY_ADDR, &data);
	if (mxl_fail(ret))
		goto fail;

	/* drop the CLK after getting ACK, SDA will go high right away */
	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SDA_OUT);
	if (mxl_fail(ret))
		goto fail;

	if (data & SW_SDA_IN)
		ret = -EIO;
fail:
	return ret;
}

static int mxl111sf_i2c_bitbang_recvbyte(struct mxl111sf_state *state,
					 u8 *pbyte)
{
	int i, ret;
	u8 byte = 0;
	u8 data = 0;

	mxl_i2c("()");

	*pbyte = 0;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SDA_OUT);
	if (mxl_fail(ret))
		goto fail;

	for (i = 0; i < 8; i++) {
		ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
					 0x10 | SW_I2C_EN |
					 SW_SCL_OUT | SW_SDA_OUT);
		if (mxl_fail(ret))
			goto fail;

		ret = mxl111sf_read_reg(state, SW_I2C_BUSY_ADDR, &data);
		if (mxl_fail(ret))
			goto fail;

		if (data & SW_SDA_IN)
			byte |= (0x80 >> i);

		ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
					 0x10 | SW_I2C_EN | SW_SDA_OUT);
		if (mxl_fail(ret))
			goto fail;
	}
	*pbyte = byte;
fail:
	return ret;
}

static int mxl111sf_i2c_start(struct mxl111sf_state *state)
{
	int ret;

	mxl_i2c("()");

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SCL_OUT | SW_SDA_OUT);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SCL_OUT);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN); /* start */
	mxl_fail(ret);
fail:
	return ret;
}

static int mxl111sf_i2c_stop(struct mxl111sf_state *state)
{
	int ret;

	mxl_i2c("()");

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN); /* stop */
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SCL_OUT);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SCL_OUT | SW_SDA_OUT);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_SCL_OUT | SW_SDA_OUT);
	mxl_fail(ret);
fail:
	return ret;
}

static int mxl111sf_i2c_ack(struct mxl111sf_state *state)
{
	int ret;
	u8 b = 0;

	mxl_i2c("()");

	ret = mxl111sf_read_reg(state, SW_I2C_BUSY_ADDR, &b);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN);
	if (mxl_fail(ret))
		goto fail;

	/* pull SDA low */
	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SCL_OUT);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SDA_OUT);
	mxl_fail(ret);
fail:
	return ret;
}

static int mxl111sf_i2c_nack(struct mxl111sf_state *state)
{
	int ret;

	mxl_i2c("()");

	/* SDA high to signal last byte read from slave */
	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SCL_OUT | SW_SDA_OUT);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl111sf_write_reg(state, SW_I2C_ADDR,
				 0x10 | SW_I2C_EN | SW_SDA_OUT);
	mxl_fail(ret);
fail:
	return ret;
}

/* ------------------------------------------------------------------------ */

static int mxl111sf_i2c_sw_xfer_msg(struct mxl111sf_state *state,
				    struct i2c_msg *msg)
{
	int i, ret;

	mxl_i2c("()");

	if (msg->flags & I2C_M_RD) {

		ret = mxl111sf_i2c_start(state);
		if (mxl_fail(ret))
			goto fail;

		ret = mxl111sf_i2c_bitbang_sendbyte(state,
						    (msg->addr << 1) | 0x01);
		if (mxl_fail(ret)) {
			mxl111sf_i2c_stop(state);
			goto fail;
		}

		for (i = 0; i < msg->len; i++) {
			ret = mxl111sf_i2c_bitbang_recvbyte(state,
							    &msg->buf[i]);
			if (mxl_fail(ret)) {
				mxl111sf_i2c_stop(state);
				goto fail;
			}

			if (i < msg->len - 1)
				mxl111sf_i2c_ack(state);
		}

		mxl111sf_i2c_nack(state);

		ret = mxl111sf_i2c_stop(state);
		if (mxl_fail(ret))
			goto fail;

	} else {

		ret = mxl111sf_i2c_start(state);
		if (mxl_fail(ret))
			goto fail;

		ret = mxl111sf_i2c_bitbang_sendbyte(state,
						    (msg->addr << 1) & 0xfe);
		if (mxl_fail(ret)) {
			mxl111sf_i2c_stop(state);
			goto fail;
		}

		for (i = 0; i < msg->len; i++) {
			ret = mxl111sf_i2c_bitbang_sendbyte(state,
							    msg->buf[i]);
			if (mxl_fail(ret)) {
				mxl111sf_i2c_stop(state);
				goto fail;
			}
		}

		/* FIXME: we only want to do this on the last transaction */
		mxl111sf_i2c_stop(state);
	}
fail:
	return ret;
}

/* HW-I2C ----------------------------------------------------------------- */

#define USB_WRITE_I2C_CMD     0x99
#define USB_READ_I2C_CMD      0xdd
#define USB_END_I2C_CMD       0xfe

#define USB_WRITE_I2C_CMD_LEN   26
#define USB_READ_I2C_CMD_LEN    24

#define I2C_MUX_REG           0x30
#define I2C_CONTROL_REG       0x00
#define I2C_SLAVE_ADDR_REG    0x08
#define I2C_DATA_REG          0x0c
#define I2C_INT_STATUS_REG    0x10

static int mxl111sf_i2c_send_data(struct mxl111sf_state *state,
				  u8 index, u8 *wdata)
{
	int ret = mxl111sf_ctrl_msg(state->d, wdata[0],
				    &wdata[1], 25, NULL, 0);
	mxl_fail(ret);

	return ret;
}

static int mxl111sf_i2c_get_data(struct mxl111sf_state *state,
				 u8 index, u8 *wdata, u8 *rdata)
{
	int ret = mxl111sf_ctrl_msg(state->d, wdata[0],
				    &wdata[1], 25, rdata, 24);
	mxl_fail(ret);

	return ret;
}

static u8 mxl111sf_i2c_check_status(struct mxl111sf_state *state)
{
	u8 status = 0;
	u8 buf[26];

	mxl_i2c_adv("()");

	buf[0] = USB_READ_I2C_CMD;
	buf[1] = 0x00;

	buf[2] = I2C_INT_STATUS_REG;
	buf[3] = 0x00;
	buf[4] = 0x00;

	buf[5] = USB_END_I2C_CMD;

	mxl111sf_i2c_get_data(state, 0, buf, buf);

	if (buf[1] & 0x04)
		status = 1;

	return status;
}

static u8 mxl111sf_i2c_check_fifo(struct mxl111sf_state *state)
{
	u8 status = 0;
	u8 buf[26];

	mxl_i2c("()");

	buf[0] = USB_READ_I2C_CMD;
	buf[1] = 0x00;

	buf[2] = I2C_MUX_REG;
	buf[3] = 0x00;
	buf[4] = 0x00;

	buf[5] = I2C_INT_STATUS_REG;
	buf[6] = 0x00;
	buf[7] = 0x00;
	buf[8] = USB_END_I2C_CMD;

	mxl111sf_i2c_get_data(state, 0, buf, buf);

	if (0x08 == (buf[1] & 0x08))
		status = 1;

	if ((buf[5] & 0x02) == 0x02)
		mxl_i2c("(buf[5] & 0x02) == 0x02"); /* FIXME */

	return status;
}

static int mxl111sf_i2c_readagain(struct mxl111sf_state *state,
				  u8 count, u8 *rbuf)
{
	u8 i2c_w_data[26];
	u8 i2c_r_data[24];
	u8 i = 0;
	u8 fifo_status = 0;
	int status = 0;

	mxl_i2c("read %d bytes", count);

	while ((fifo_status == 0) && (i++ < 5))
		fifo_status = mxl111sf_i2c_check_fifo(state);

	i2c_w_data[0] = 0xDD;
	i2c_w_data[1] = 0x00;

	for (i = 2; i < 26; i++)
		i2c_w_data[i] = 0xFE;

	for (i = 0; i < count; i++) {
		i2c_w_data[2+(i*3)] = 0x0C;
		i2c_w_data[3+(i*3)] = 0x00;
		i2c_w_data[4+(i*3)] = 0x00;
	}

	mxl111sf_i2c_get_data(state, 0, i2c_w_data, i2c_r_data);

	/* Check for I2C NACK status */
	if (mxl111sf_i2c_check_status(state) == 1) {
		mxl_i2c("error!");
	} else {
		for (i = 0; i < count; i++) {
			rbuf[i] = i2c_r_data[(i*3)+1];
			mxl_i2c("%02x\t %02x",
				i2c_r_data[(i*3)+1],
				i2c_r_data[(i*3)+2]);
		}

		status = 1;
	}

	return status;
}

#define HWI2C400 1
static int mxl111sf_i2c_hw_xfer_msg(struct mxl111sf_state *state,
				    struct i2c_msg *msg)
{
	int i, k, ret = 0;
	u16 index = 0;
	u8 buf[26];
	u8 i2c_r_data[24];
	u16 block_len;
	u16 left_over_len;
	u8 rd_status[8];
	u8 ret_status;
	u8 readbuff[26];

	mxl_i2c("addr: 0x%02x, read buff len: %d, write buff len: %d",
		msg->addr, (msg->flags & I2C_M_RD) ? msg->len : 0,
		(!(msg->flags & I2C_M_RD)) ? msg->len : 0);

	for (index = 0; index < 26; index++)
		buf[index] = USB_END_I2C_CMD;

	/* command to indicate data payload is destined for I2C interface */
	buf[0] = USB_WRITE_I2C_CMD;
	buf[1] = 0x00;

	/* enable I2C interface */
	buf[2] = I2C_MUX_REG;
	buf[3] = 0x80;
	buf[4] = 0x00;

	/* enable I2C interface */
	buf[5] = I2C_MUX_REG;
	buf[6] = 0x81;
	buf[7] = 0x00;

	/* set Timeout register on I2C interface */
	buf[8] = 0x14;
	buf[9] = 0xff;
	buf[10] = 0x00;
#if 0
	/* enable Interrupts on I2C interface */
	buf[8] = 0x24;
	buf[9] = 0xF7;
	buf[10] = 0x00;
#endif
	buf[11] = 0x24;
	buf[12] = 0xF7;
	buf[13] = 0x00;

	ret = mxl111sf_i2c_send_data(state, 0, buf);

	/* write data on I2C bus */
	if (!(msg->flags & I2C_M_RD) && (msg->len > 0)) {
		mxl_i2c("%d\t%02x", msg->len, msg->buf[0]);

		/* control register on I2C interface to initialize I2C bus */
		buf[2] = I2C_CONTROL_REG;
		buf[3] = 0x5E;
		buf[4] = (HWI2C400) ? 0x03 : 0x0D;

		/* I2C Slave device Address */
		buf[5] = I2C_SLAVE_ADDR_REG;
		buf[6] = (msg->addr);
		buf[7] = 0x00;
		buf[8] = USB_END_I2C_CMD;
		ret = mxl111sf_i2c_send_data(state, 0, buf);

		/* check for slave device status */
		if (mxl111sf_i2c_check_status(state) == 1) {
			mxl_i2c("NACK writing slave address %02x",
				msg->addr);
			/* if NACK, stop I2C bus and exit */
			buf[2] = I2C_CONTROL_REG;
			buf[3] = 0x4E;
			buf[4] = (HWI2C400) ? 0x03 : 0x0D;
			ret = -EIO;
			goto exit;
		}

		/* I2C interface can do I2C operations in block of 8 bytes of
		   I2C data. calculation to figure out number of blocks of i2c
		   data required to program */
		block_len = (msg->len / 8);
		left_over_len = (msg->len % 8);
		index = 0;

		mxl_i2c("block_len %d, left_over_len %d",
			block_len, left_over_len);

		for (index = 0; index < block_len; index++) {
			for (i = 0; i < 8; i++) {
				/* write data on I2C interface */
				buf[2+(i*3)] = I2C_DATA_REG;
				buf[3+(i*3)] = msg->buf[(index*8)+i];
				buf[4+(i*3)] = 0x00;
			}

			ret = mxl111sf_i2c_send_data(state, 0, buf);

			/* check for I2C NACK status */
			if (mxl111sf_i2c_check_status(state) == 1) {
				mxl_i2c("NACK writing slave address %02x",
					msg->addr);

				/* if NACK, stop I2C bus and exit */
				buf[2] = I2C_CONTROL_REG;
				buf[3] = 0x4E;
				buf[4] = (HWI2C400) ? 0x03 : 0x0D;
				ret = -EIO;
				goto exit;
			}

		}

		if (left_over_len) {
			for (k = 0; k < 26; k++)
				buf[k] = USB_END_I2C_CMD;

			buf[0] = 0x99;
			buf[1] = 0x00;

			for (i = 0; i < left_over_len; i++) {
				buf[2+(i*3)] = I2C_DATA_REG;
				buf[3+(i*3)] = msg->buf[(index*8)+i];
				mxl_i2c("index = %d %d data %d",
					index, i, msg->buf[(index*8)+i]);
				buf[4+(i*3)] = 0x00;
			}
			ret = mxl111sf_i2c_send_data(state, 0, buf);

			/* check for I2C NACK status */
			if (mxl111sf_i2c_check_status(state) == 1) {
				mxl_i2c("NACK writing slave address %02x",
					msg->addr);

				/* if NACK, stop I2C bus and exit */
				buf[2] = I2C_CONTROL_REG;
				buf[3] = 0x4E;
				buf[4] = (HWI2C400) ? 0x03 : 0x0D;
				ret = -EIO;
				goto exit;
			}

		}

		/* issue I2C STOP after write */
		buf[2] = I2C_CONTROL_REG;
		buf[3] = 0x4E;
		buf[4] = (HWI2C400) ? 0x03 : 0x0D;

	}

	/* read data from I2C bus */
	if ((msg->flags & I2C_M_RD) && (msg->len > 0)) {
		mxl_i2c("read buf len %d", msg->len);

		/* command to indicate data payload is
		   destined for I2C interface */
		buf[2] = I2C_CONTROL_REG;
		buf[3] = 0xDF;
		buf[4] = (HWI2C400) ? 0x03 : 0x0D;

		/* I2C xfer length */
		buf[5] = 0x14;
		buf[6] = (msg->len & 0xFF);
		buf[7] = 0;

		/* I2C slave device Address */
		buf[8] = I2C_SLAVE_ADDR_REG;
		buf[9] = msg->addr;
		buf[10] = 0x00;
		buf[11] = USB_END_I2C_CMD;
		ret = mxl111sf_i2c_send_data(state, 0, buf);

		/* check for I2C NACK status */
		if (mxl111sf_i2c_check_status(state) == 1) {
			mxl_i2c("NACK reading slave address %02x",
				msg->addr);

			/* if NACK, stop I2C bus and exit */
			buf[2] = I2C_CONTROL_REG;
			buf[3] = 0xC7;
			buf[4] = (HWI2C400) ? 0x03 : 0x0D;
			ret = -EIO;
			goto exit;
		}

		/* I2C interface can do I2C operations in block of 8 bytes of
		   I2C data. calculation to figure out number of blocks of
		   i2c data required to program */
		block_len = ((msg->len) / 8);
		left_over_len = ((msg->len) % 8);
		index = 0;

		mxl_i2c("block_len %d, left_over_len %d",
			block_len, left_over_len);

		/* command to read data from I2C interface */
		buf[0] = USB_READ_I2C_CMD;
		buf[1] = 0x00;

		for (index = 0; index < block_len; index++) {
			/* setup I2C read request packet on I2C interface */
			for (i = 0; i < 8; i++) {
				buf[2+(i*3)] = I2C_DATA_REG;
				buf[3+(i*3)] = 0x00;
				buf[4+(i*3)] = 0x00;
			}

			ret = mxl111sf_i2c_get_data(state, 0, buf, i2c_r_data);

			/* check for I2C NACK status */
			if (mxl111sf_i2c_check_status(state) == 1) {
				mxl_i2c("NACK reading slave address %02x",
					msg->addr);

				/* if NACK, stop I2C bus and exit */
				buf[2] = I2C_CONTROL_REG;
				buf[3] = 0xC7;
				buf[4] = (HWI2C400) ? 0x03 : 0x0D;
				ret = -EIO;
				goto exit;
			}

			/* copy data from i2c data payload to read buffer */
			for (i = 0; i < 8; i++) {
				rd_status[i] = i2c_r_data[(i*3)+2];

				if (rd_status[i] == 0x04) {
					if (i < 7) {
						mxl_i2c("i2c fifo empty! @ %d",
							i);
						msg->buf[(index*8)+i] =
							i2c_r_data[(i*3)+1];
						/* read again */
						ret_status =
							mxl111sf_i2c_readagain(
								state, 8-(i+1),
								readbuff);
						if (ret_status == 1) {
							for (k = 0;
							     k < 8-(i+1);
							     k++) {

					msg->buf[(index*8)+(k+i+1)] =
						readbuff[k];
					mxl_i2c("read data: %02x\t %02x",
						msg->buf[(index*8)+(k+i)],
						(index*8)+(k+i));
					mxl_i2c("read data: %02x\t %02x",
						msg->buf[(index*8)+(k+i+1)],
						readbuff[k]);

							}
							goto stop_copy;
						} else {
							mxl_i2c("readagain ERROR!");
						}
					} else {
						msg->buf[(index*8)+i] =
							i2c_r_data[(i*3)+1];
					}
				} else {
					msg->buf[(index*8)+i] =
						i2c_r_data[(i*3)+1];
				}
			}
stop_copy:
			;

		}

		if (left_over_len) {
			for (k = 0; k < 26; k++)
				buf[k] = USB_END_I2C_CMD;

			buf[0] = 0xDD;
			buf[1] = 0x00;

			for (i = 0; i < left_over_len; i++) {
				buf[2+(i*3)] = I2C_DATA_REG;
				buf[3+(i*3)] = 0x00;
				buf[4+(i*3)] = 0x00;
			}
			ret = mxl111sf_i2c_get_data(state, 0, buf,
						    i2c_r_data);

			/* check for I2C NACK status */
			if (mxl111sf_i2c_check_status(state) == 1) {
				mxl_i2c("NACK reading slave address %02x",
					msg->addr);

				/* if NACK, stop I2C bus and exit */
				buf[2] = I2C_CONTROL_REG;
				buf[3] = 0xC7;
				buf[4] = (HWI2C400) ? 0x03 : 0x0D;
				ret = -EIO;
				goto exit;
			}

			for (i = 0; i < left_over_len; i++) {
				msg->buf[(block_len*8)+i] =
					i2c_r_data[(i*3)+1];
				mxl_i2c("read data: %02x\t %02x",
					i2c_r_data[(i*3)+1],
					i2c_r_data[(i*3)+2]);
			}
		}

		/* indicate I2C interface to issue NACK
		   after next I2C read op */
		buf[0] = USB_WRITE_I2C_CMD;
		buf[1] = 0x00;

		/* control register */
		buf[2] = I2C_CONTROL_REG;
		buf[3] = 0x17;
		buf[4] = (HWI2C400) ? 0x03 : 0x0D;

		buf[5] = USB_END_I2C_CMD;
		ret = mxl111sf_i2c_send_data(state, 0, buf);

		/* control register */
		buf[2] = I2C_CONTROL_REG;
		buf[3] = 0xC7;
		buf[4] = (HWI2C400) ? 0x03 : 0x0D;

	}
exit:
	/* STOP and disable I2C MUX */
	buf[0] = USB_WRITE_I2C_CMD;
	buf[1] = 0x00;

	/* de-initilize I2C BUS */
	buf[5] = USB_END_I2C_CMD;
	mxl111sf_i2c_send_data(state, 0, buf);

	/* Control Register */
	buf[2] = I2C_CONTROL_REG;
	buf[3] = 0xDF;
	buf[4] = 0x03;

	/* disable I2C interface */
	buf[5] = I2C_MUX_REG;
	buf[6] = 0x00;
	buf[7] = 0x00;

	/* de-initilize I2C BUS */
	buf[8] = USB_END_I2C_CMD;
	mxl111sf_i2c_send_data(state, 0, buf);

	/* disable I2C interface */
	buf[2] = I2C_MUX_REG;
	buf[3] = 0x81;
	buf[4] = 0x00;

	/* disable I2C interface */
	buf[5] = I2C_MUX_REG;
	buf[6] = 0x00;
	buf[7] = 0x00;

	/* disable I2C interface */
	buf[8] = I2C_MUX_REG;
	buf[9] = 0x00;
	buf[10] = 0x00;

	buf[11] = USB_END_I2C_CMD;
	mxl111sf_i2c_send_data(state, 0, buf);

	return ret;
}

/* ------------------------------------------------------------------------ */

int mxl111sf_i2c_xfer(struct i2c_adapter *adap,
		      struct i2c_msg msg[], int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	struct mxl111sf_state *state = d->priv;
	int hwi2c = (state->chip_rev > MXL111SF_V6);
	int i, ret;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	for (i = 0; i < num; i++) {
		ret = (hwi2c) ?
			mxl111sf_i2c_hw_xfer_msg(state, &msg[i]) :
			mxl111sf_i2c_sw_xfer_msg(state, &msg[i]);
		if (mxl_fail(ret)) {
			mxl_debug_adv("failed with error %d on i2c transaction %d of %d, %sing %d bytes to/from 0x%02x",
				      ret, i+1, num,
				      (msg[i].flags & I2C_M_RD) ?
				      "read" : "writ",
				      msg[i].len, msg[i].addr);

			break;
		}
	}

	mutex_unlock(&d->i2c_mutex);

	return i == num ? num : -EREMOTEIO;
}
