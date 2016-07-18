/*
 *  pxi_cmd.h
 *
 *  Copyright (C) 2016 Sergi Granell <xerpi.g.12@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __NINTENDO3DS_PXI_CMD_H
#define __NINTENDO3DS_PXI_CMD_H

#define PXI_CMD_PING 1
#define PXI_CMD_PONG 2

#define PXI_CMD_SDMMC_READ_SECTOR 3

struct pxi_cmd_hdr {
	u32 cmd;
	u32 len;
	u8 data[0];
} __attribute__((packed));

#endif
