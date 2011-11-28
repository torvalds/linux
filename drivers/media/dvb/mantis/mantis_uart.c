/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <asm/io.h>

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"

#include "mantis_common.h"
#include "mantis_reg.h"
#include "mantis_uart.h"

struct mantis_uart_params {
	enum mantis_baud	baud_rate;
	enum mantis_parity	parity;
};

static struct {
	char string[7];
} rates[5] = {
	{ "9600" },
	{ "19200" },
	{ "38400" },
	{ "57600" },
	{ "115200" }
};

static struct {
	char string[5];
} parity[3] = {
	{ "NONE" },
	{ "ODD" },
	{ "EVEN" }
};

#define UART_MAX_BUF			16

int mantis_uart_read(struct mantis_pci *mantis, u8 *data)
{
	struct mantis_hwconfig *config = mantis->hwconfig;
	u32 stat = 0, i;

	/* get data */
	for (i = 0; i < (config->bytes + 1); i++) {

		stat = mmread(MANTIS_UART_STAT);

		if (stat & MANTIS_UART_RXFIFO_FULL) {
			dprintk(MANTIS_ERROR, 1, "RX Fifo FULL");
		}
		data[i] = mmread(MANTIS_UART_RXD) & 0x3f;

		dprintk(MANTIS_DEBUG, 1, "Reading ... <%02x>", data[i] & 0x3f);

		if (data[i] & (1 << 7)) {
			dprintk(MANTIS_ERROR, 1, "UART framing error");
			return -EINVAL;
		}
		if (data[i] & (1 << 6)) {
			dprintk(MANTIS_ERROR, 1, "UART parity error");
			return -EINVAL;
		}
	}

	return 0;
}

static void mantis_uart_work(struct work_struct *work)
{
	struct mantis_pci *mantis = container_of(work, struct mantis_pci, uart_work);
	struct mantis_hwconfig *config = mantis->hwconfig;
	u8 buf[16];
	int i;

	mantis_uart_read(mantis, buf);

	for (i = 0; i < (config->bytes + 1); i++)
		dprintk(MANTIS_INFO, 1, "UART BUF:%d <%02x> ", i, buf[i]);

	dprintk(MANTIS_DEBUG, 0, "\n");
}

static int mantis_uart_setup(struct mantis_pci *mantis,
			     struct mantis_uart_params *params)
{
	u32 reg;

	mmwrite((mmread(MANTIS_UART_CTL) | (params->parity & 0x3)), MANTIS_UART_CTL);

	reg = mmread(MANTIS_UART_BAUD);

	switch (params->baud_rate) {
	case MANTIS_BAUD_9600:
		reg |= 0xd8;
		break;
	case MANTIS_BAUD_19200:
		reg |= 0x6c;
		break;
	case MANTIS_BAUD_38400:
		reg |= 0x36;
		break;
	case MANTIS_BAUD_57600:
		reg |= 0x23;
		break;
	case MANTIS_BAUD_115200:
		reg |= 0x11;
		break;
	default:
		return -EINVAL;
	}

	mmwrite(reg, MANTIS_UART_BAUD);

	return 0;
}

int mantis_uart_init(struct mantis_pci *mantis)
{
	struct mantis_hwconfig *config = mantis->hwconfig;
	struct mantis_uart_params params;

	/* default parity: */
	params.baud_rate = config->baud_rate;
	params.parity = config->parity;
	dprintk(MANTIS_INFO, 1, "Initializing UART @ %sbps parity:%s",
		rates[params.baud_rate].string,
		parity[params.parity].string);

	init_waitqueue_head(&mantis->uart_wq);
	spin_lock_init(&mantis->uart_lock);

	INIT_WORK(&mantis->uart_work, mantis_uart_work);

	/* disable interrupt */
	mmwrite(mmread(MANTIS_UART_CTL) & 0xffef, MANTIS_UART_CTL);

	mantis_uart_setup(mantis, &params);

	/* default 1 byte */
	mmwrite((mmread(MANTIS_UART_BAUD) | (config->bytes << 8)), MANTIS_UART_BAUD);

	/* flush buffer */
	mmwrite((mmread(MANTIS_UART_CTL) | MANTIS_UART_RXFLUSH), MANTIS_UART_CTL);

	/* enable interrupt */
	mmwrite(mmread(MANTIS_INT_MASK) | 0x800, MANTIS_INT_MASK);
	mmwrite(mmread(MANTIS_UART_CTL) | MANTIS_UART_RXINT, MANTIS_UART_CTL);

	schedule_work(&mantis->uart_work);
	dprintk(MANTIS_DEBUG, 1, "UART successfully initialized");

	return 0;
}
EXPORT_SYMBOL_GPL(mantis_uart_init);

void mantis_uart_exit(struct mantis_pci *mantis)
{
	/* disable interrupt */
	mmwrite(mmread(MANTIS_UART_CTL) & 0xffef, MANTIS_UART_CTL);
	flush_work_sync(&mantis->uart_work);
}
EXPORT_SYMBOL_GPL(mantis_uart_exit);
