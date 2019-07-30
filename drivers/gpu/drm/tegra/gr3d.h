/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 NVIDIA Corporation
 */

#ifndef TEGRA_GR3D_H
#define TEGRA_GR3D_H

#define GR3D_IDX_ATTRIBUTE(x)		(0x100 + (x) * 2)
#define GR3D_IDX_INDEX_BASE		0x121
#define GR3D_QR_ZTAG_ADDR		0x415
#define GR3D_QR_CTAG_ADDR		0x417
#define GR3D_QR_CZ_ADDR			0x419
#define GR3D_TEX_TEX_ADDR(x)		(0x710 + (x))
#define GR3D_DW_MEMORY_OUTPUT_ADDRESS	0x904
#define GR3D_GLOBAL_SURFADDR(x)		(0xe00 + (x))
#define GR3D_GLOBAL_SPILLSURFADDR	0xe2a
#define GR3D_GLOBAL_SURFOVERADDR(x)	(0xe30 + (x))
#define GR3D_GLOBAL_SAMP01SURFADDR(x)	(0xe50 + (x))
#define GR3D_GLOBAL_SAMP23SURFADDR(x)	(0xe60 + (x))

#define GR3D_NUM_REGS			0xe88

#endif
