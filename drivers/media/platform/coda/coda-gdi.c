/*
 * Coda multi-standard codec IP
 *
 * Copyright (C) 2014 Philipp Zabel, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/bitops.h>
#include "coda.h"

#define XY2_INVERT	BIT(7)
#define XY2_ZERO	BIT(6)
#define XY2_TB_XOR	BIT(5)
#define XY2_XYSEL	BIT(4)
#define XY2_Y		(1 << 4)
#define XY2_X		(0 << 4)

#define XY2(luma_sel, luma_bit, chroma_sel, chroma_bit) \
	(((XY2_##luma_sel) | (luma_bit)) << 8 | \
	 (XY2_##chroma_sel) | (chroma_bit))

static const u16 xy2ca_zero_map[16] = {
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
};

static const u16 xy2ca_tiled_map[16] = {
	XY2(Y,    0, Y,    0),
	XY2(Y,    1, Y,    1),
	XY2(Y,    2, Y,    2),
	XY2(Y,    3, X,    3),
	XY2(X,    3, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
	XY2(ZERO, 0, ZERO, 0),
};

/*
 * RA[15:0], CA[15:8] are hardwired to contain the 24-bit macroblock
 * start offset (macroblock size is 16x16 for luma, 16x8 for chroma).
 * Bits CA[4:0] are set using XY2CA above. BA[3:0] seems to be unused.
 */

#define RBC_CA		(0 << 4)
#define RBC_BA		(1 << 4)
#define RBC_RA		(2 << 4)
#define RBC_ZERO	(3 << 4)

#define RBC(luma_sel, luma_bit, chroma_sel, chroma_bit) \
	(((RBC_##luma_sel) | (luma_bit)) << 6 | \
	 (RBC_##chroma_sel) | (chroma_bit))

static const u16 rbc2axi_tiled_map[32] = {
	RBC(ZERO, 0, ZERO, 0),
	RBC(ZERO, 0, ZERO, 0),
	RBC(ZERO, 0, ZERO, 0),
	RBC(CA,   0, CA,   0),
	RBC(CA,   1, CA,   1),
	RBC(CA,   2, CA,   2),
	RBC(CA,   3, CA,   3),
	RBC(CA,   4, CA,   8),
	RBC(CA,   8, CA,   9),
	RBC(CA,   9, CA,  10),
	RBC(CA,  10, CA,  11),
	RBC(CA,  11, CA,  12),
	RBC(CA,  12, CA,  13),
	RBC(CA,  13, CA,  14),
	RBC(CA,  14, CA,  15),
	RBC(CA,  15, RA,   0),
	RBC(RA,   0, RA,   1),
	RBC(RA,   1, RA,   2),
	RBC(RA,   2, RA,   3),
	RBC(RA,   3, RA,   4),
	RBC(RA,   4, RA,   5),
	RBC(RA,   5, RA,   6),
	RBC(RA,   6, RA,   7),
	RBC(RA,   7, RA,   8),
	RBC(RA,   8, RA,   9),
	RBC(RA,   9, RA,  10),
	RBC(RA,  10, RA,  11),
	RBC(RA,  11, RA,  12),
	RBC(RA,  12, RA,  13),
	RBC(RA,  13, RA,  14),
	RBC(RA,  14, RA,  15),
	RBC(RA,  15, ZERO, 0),
};

void coda_set_gdi_regs(struct coda_ctx *ctx)
{
	struct coda_dev *dev = ctx->dev;
	const u16 *xy2ca_map;
	u32 xy2rbc_config;
	int i;

	switch (ctx->tiled_map_type) {
	case GDI_LINEAR_FRAME_MAP:
	default:
		xy2ca_map = xy2ca_zero_map;
		xy2rbc_config = 0;
		break;
	case GDI_TILED_FRAME_MB_RASTER_MAP:
		xy2ca_map = xy2ca_tiled_map;
		xy2rbc_config = CODA9_XY2RBC_TILED_MAP |
				CODA9_XY2RBC_CA_INC_HOR |
				(16 - 1) << 12 | (8 - 1) << 4;
		break;
	}

	for (i = 0; i < 16; i++)
		coda_write(dev, xy2ca_map[i],
				CODA9_GDI_XY2_CAS_0 + 4 * i);
	for (i = 0; i < 4; i++)
		coda_write(dev, XY2(ZERO, 0, ZERO, 0),
				CODA9_GDI_XY2_BA_0 + 4 * i);
	for (i = 0; i < 16; i++)
		coda_write(dev, XY2(ZERO, 0, ZERO, 0),
				CODA9_GDI_XY2_RAS_0 + 4 * i);
	coda_write(dev, xy2rbc_config, CODA9_GDI_XY2_RBC_CONFIG);
	if (xy2rbc_config) {
		for (i = 0; i < 32; i++)
			coda_write(dev, rbc2axi_tiled_map[i],
					CODA9_GDI_RBC2_AXI_0 + 4 * i);
	}
}
