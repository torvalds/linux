/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MacBook (Pro) SPI keyboard and touchpad driver
 *
 * Copyright (c) 2015-2019 Federico Lorenzi
 * Copyright (c) 2017-2019 Ronald Tschalär
 */

#ifndef _APPLESPI_H_
#define _APPLESPI_H_

enum applespi_evt_type {
	ET_CMD_TP_INI = BIT(0),
	ET_CMD_BL = BIT(1),
	ET_CMD_CL = BIT(2),
	ET_RD_KEYB = BIT(8),
	ET_RD_TPAD = BIT(9),
	ET_RD_UNKN = BIT(10),
	ET_RD_IRQ = BIT(11),
	ET_RD_CRC = BIT(12),
};

enum applespi_pkt_type {
	PT_READ,
	PT_WRITE,
	PT_STATUS,
};

#endif /* _APPLESPI_H_ */
