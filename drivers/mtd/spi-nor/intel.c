// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info intel_parts[] = {
	/* Intel/Numonyx -- xxxs33b */
	{ "160s33b",  INFO(0x898911, 0, 64 * 1024,  32, 0) },
	{ "320s33b",  INFO(0x898912, 0, 64 * 1024,  64, 0) },
	{ "640s33b",  INFO(0x898913, 0, 64 * 1024, 128, 0) },
};

static void intel_default_init(struct spi_nor *nor)
{
	nor->flags |= SNOR_F_HAS_LOCK;
}

static const struct spi_nor_fixups intel_fixups = {
	.default_init = intel_default_init,
};

const struct spi_nor_manufacturer spi_nor_intel = {
	.name = "intel",
	.parts = intel_parts,
	.nparts = ARRAY_SIZE(intel_parts),
	.fixups = &intel_fixups,
};
