/*
 * Defines for the IBM TAH
 *
 * Copyright 2004 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _IBM_EMAC_TAH_H
#define _IBM_EMAC_TAH_H

/* TAH */
typedef struct tah_regs {
	u32 tah_revid;
	u32 pad[3];
	u32 tah_mr;
	u32 tah_ssr0;
	u32 tah_ssr1;
	u32 tah_ssr2;
	u32 tah_ssr3;
	u32 tah_ssr4;
	u32 tah_ssr5;
	u32 tah_tsr;
} tah_t;

/* TAH engine */
#define TAH_MR_CVR			0x80000000
#define TAH_MR_SR			0x40000000
#define TAH_MR_ST_256			0x01000000
#define TAH_MR_ST_512			0x02000000
#define TAH_MR_ST_768			0x03000000
#define TAH_MR_ST_1024			0x04000000
#define TAH_MR_ST_1280			0x05000000
#define TAH_MR_ST_1536			0x06000000
#define TAH_MR_TFS_16KB			0x00000000
#define TAH_MR_TFS_2KB			0x00200000
#define TAH_MR_TFS_4KB			0x00400000
#define TAH_MR_TFS_6KB			0x00600000
#define TAH_MR_TFS_8KB			0x00800000
#define TAH_MR_TFS_10KB			0x00a00000
#define TAH_MR_DTFP			0x00100000
#define TAH_MR_DIG			0x00080000

#endif				/* _IBM_EMAC_TAH_H */
