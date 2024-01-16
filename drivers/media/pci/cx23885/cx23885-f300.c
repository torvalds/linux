// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Silicon Labs C8051F300 microcontroller.
 *
 * It is used for LNB power control in TeVii S470,
 * TBS 6920 PCIe DVB-S2 cards.
 *
 * Microcontroller connected to cx23885 GPIO pins:
 * GPIO0 - data		- P0.3 F300
 * GPIO1 - reset	- P0.2 F300
 * GPIO2 - clk		- P0.1 F300
 * GPIO3 - busy		- P0.0 F300
 *
 * Copyright (C) 2009 Igor M. Liplianin <liplianin@me.by>
 */

#include "cx23885.h"
#include "cx23885-f300.h"

#define F300_DATA	GPIO_0
#define F300_RESET	GPIO_1
#define F300_CLK	GPIO_2
#define F300_BUSY	GPIO_3

static void f300_set_line(struct cx23885_dev *dev, u32 line, u8 lvl)
{
	cx23885_gpio_enable(dev, line, 1);
	if (lvl == 1)
		cx23885_gpio_set(dev, line);
	else
		cx23885_gpio_clear(dev, line);
}

static u8 f300_get_line(struct cx23885_dev *dev, u32 line)
{
	cx23885_gpio_enable(dev, line, 0);

	return cx23885_gpio_get(dev, line);
}

static void f300_send_byte(struct cx23885_dev *dev, u8 dta)
{
	u8 i;

	for (i = 0; i < 8; i++) {
		f300_set_line(dev, F300_CLK, 0);
		udelay(30);
		f300_set_line(dev, F300_DATA, (dta & 0x80) >> 7);/* msb first */
		udelay(30);
		dta <<= 1;
		f300_set_line(dev, F300_CLK, 1);
		udelay(30);
	}
}

static u8 f300_get_byte(struct cx23885_dev *dev)
{
	u8 i, dta = 0;

	for (i = 0; i < 8; i++) {
		f300_set_line(dev, F300_CLK, 0);
		udelay(30);
		dta <<= 1;
		f300_set_line(dev, F300_CLK, 1);
		udelay(30);
		dta |= f300_get_line(dev, F300_DATA);/* msb first */

	}

	return dta;
}

static u8 f300_xfer(struct dvb_frontend *fe, u8 *buf)
{
	struct cx23885_tsport *port = fe->dvb->priv;
	struct cx23885_dev *dev = port->dev;
	u8 i, temp, ret = 0;

	temp = buf[0];
	for (i = 0; i < buf[0]; i++)
		temp += buf[i + 1];
	temp = (~temp + 1);/* get check sum */
	buf[1 + buf[0]] = temp;

	f300_set_line(dev, F300_RESET, 1);
	f300_set_line(dev, F300_CLK, 1);
	udelay(30);
	f300_set_line(dev, F300_DATA, 1);
	msleep(1);

	/* question: */
	f300_set_line(dev, F300_RESET, 0);/* begin to send data */
	msleep(1);

	f300_send_byte(dev, 0xe0);/* the slave address is 0xe0, write */
	msleep(1);

	temp = buf[0];
	temp += 2;
	for (i = 0; i < temp; i++)
		f300_send_byte(dev, buf[i]);

	f300_set_line(dev, F300_RESET, 1);/* sent data over */
	f300_set_line(dev, F300_DATA, 1);

	/* answer: */
	temp = 0;
	for (i = 0; ((i < 8) & (temp == 0)); i++) {
		msleep(1);
		if (f300_get_line(dev, F300_BUSY) == 0)
			temp = 1;
	}

	if (i > 7) {
		pr_err("%s: timeout, the slave no response\n",
								__func__);
		ret = 1; /* timeout, the slave no response */
	} else { /* the slave not busy, prepare for getting data */
		f300_set_line(dev, F300_RESET, 0);/*ready...*/
		msleep(1);
		f300_send_byte(dev, 0xe1);/* 0xe1 is Read */
		msleep(1);
		temp = f300_get_byte(dev);/*get the data length */
		if (temp > 14)
			temp = 14;

		for (i = 0; i < (temp + 1); i++)
			f300_get_byte(dev);/* get data to empty buffer */

		f300_set_line(dev, F300_RESET, 1);/* received data over */
		f300_set_line(dev, F300_DATA, 1);
	}

	return ret;
}

int f300_set_voltage(struct dvb_frontend *fe, enum fe_sec_voltage voltage)
{
	u8 buf[16];

	buf[0] = 0x05;
	buf[1] = 0x38;/* write port */
	buf[2] = 0x01;/* A port, lnb power */

	switch (voltage) {
	case SEC_VOLTAGE_13:
		buf[3] = 0x01;/* power on */
		buf[4] = 0x02;/* B port, H/V */
		buf[5] = 0x00;/*13V v*/
		break;
	case SEC_VOLTAGE_18:
		buf[3] = 0x01;
		buf[4] = 0x02;
		buf[5] = 0x01;/* 18V h*/
		break;
	case SEC_VOLTAGE_OFF:
		buf[3] = 0x00;/* power off */
		buf[4] = 0x00;
		buf[5] = 0x00;
		break;
	}

	return f300_xfer(fe, buf);
}
