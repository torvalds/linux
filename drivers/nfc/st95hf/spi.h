/*
 * ---------------------------------------------------------------------------
 * drivers/nfc/st95hf/spi.h functions declarations for SPI communication
 * ---------------------------------------------------------------------------
 * Copyright (C) 2015 STMicroelectronics – All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __LINUX_ST95HF_SPI_H
#define __LINUX_ST95HF_SPI_H

#include <linux/spi/spi.h>

/* Basic ST95HF SPI CMDs */
#define ST95HF_COMMAND_SEND	0x0
#define ST95HF_COMMAND_RESET	0x1
#define ST95HF_COMMAND_RECEIVE	0x2

#define ST95HF_RESET_CMD_LEN	0x1

/*
 * structure to contain st95hf spi communication specific information.
 * @req_issync: true for synchronous calls.
 * @spidev: st95hf spi device object.
 * @done: completion structure to wait for st95hf response
 *	for synchronous calls.
 * @spi_lock: mutex to allow only one spi transfer at a time.
 */
struct st95hf_spi_context {
	bool req_issync;
	struct spi_device *spidev;
	struct completion done;
	struct mutex spi_lock;
};

/* flag to differentiate synchronous & asynchronous spi request */
enum req_type {
	SYNC,
	ASYNC,
};

int st95hf_spi_send(struct st95hf_spi_context *spicontext,
		    unsigned char *buffertx,
		    int datalen,
		    enum req_type reqtype);

int st95hf_spi_recv_response(struct st95hf_spi_context *spicontext,
			     unsigned char *receivebuff);

int st95hf_spi_recv_echo_res(struct st95hf_spi_context *spicontext,
			     unsigned char *receivebuff);

#endif
