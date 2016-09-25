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

#define PXI_CMD_NONE			0
#define PXI_CMD_PING			1
#define PXI_CMD_SDMMC_READ_SECTOR	2

struct pxi_cmd_hdr {
	struct {
		u16 cmd;
		u16 len;
	};
	u32 data[0];
} __attribute__((packed));

struct pxi_cmd_sdmmc_read_sector {
	struct pxi_cmd_hdr header;
	u32 sector;
	u32 paddr;
} __attribute__((packed));

#endif
