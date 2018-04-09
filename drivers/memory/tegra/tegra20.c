/*
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "mc.h"

static const struct tegra_mc_client tegra20_mc_clients[] = {
	{
		.id = 0x00,
		.name = "display0a",
	}, {
		.id = 0x01,
		.name = "display0ab",
	}, {
		.id = 0x02,
		.name = "display0b",
	}, {
		.id = 0x03,
		.name = "display0bb",
	}, {
		.id = 0x04,
		.name = "display0c",
	}, {
		.id = 0x05,
		.name = "display0cb",
	}, {
		.id = 0x06,
		.name = "display1b",
	}, {
		.id = 0x07,
		.name = "display1bb",
	}, {
		.id = 0x08,
		.name = "eppup",
	}, {
		.id = 0x09,
		.name = "g2pr",
	}, {
		.id = 0x0a,
		.name = "g2sr",
	}, {
		.id = 0x0b,
		.name = "mpeunifbr",
	}, {
		.id = 0x0c,
		.name = "viruv",
	}, {
		.id = 0x0d,
		.name = "avpcarm7r",
	}, {
		.id = 0x0e,
		.name = "displayhc",
	}, {
		.id = 0x0f,
		.name = "displayhcb",
	}, {
		.id = 0x10,
		.name = "fdcdrd",
	}, {
		.id = 0x11,
		.name = "g2dr",
	}, {
		.id = 0x12,
		.name = "host1xdmar",
	}, {
		.id = 0x13,
		.name = "host1xr",
	}, {
		.id = 0x14,
		.name = "idxsrd",
	}, {
		.id = 0x15,
		.name = "mpcorer",
	}, {
		.id = 0x16,
		.name = "mpe_ipred",
	}, {
		.id = 0x17,
		.name = "mpeamemrd",
	}, {
		.id = 0x18,
		.name = "mpecsrd",
	}, {
		.id = 0x19,
		.name = "ppcsahbdmar",
	}, {
		.id = 0x1a,
		.name = "ppcsahbslvr",
	}, {
		.id = 0x1b,
		.name = "texsrd",
	}, {
		.id = 0x1c,
		.name = "vdebsevr",
	}, {
		.id = 0x1d,
		.name = "vdember",
	}, {
		.id = 0x1e,
		.name = "vdemcer",
	}, {
		.id = 0x1f,
		.name = "vdetper",
	}, {
		.id = 0x20,
		.name = "eppu",
	}, {
		.id = 0x21,
		.name = "eppv",
	}, {
		.id = 0x22,
		.name = "eppy",
	}, {
		.id = 0x23,
		.name = "mpeunifbw",
	}, {
		.id = 0x24,
		.name = "viwsb",
	}, {
		.id = 0x25,
		.name = "viwu",
	}, {
		.id = 0x26,
		.name = "viwv",
	}, {
		.id = 0x27,
		.name = "viwy",
	}, {
		.id = 0x28,
		.name = "g2dw",
	}, {
		.id = 0x29,
		.name = "avpcarm7w",
	}, {
		.id = 0x2a,
		.name = "fdcdwr",
	}, {
		.id = 0x2b,
		.name = "host1xw",
	}, {
		.id = 0x2c,
		.name = "ispw",
	}, {
		.id = 0x2d,
		.name = "mpcorew",
	}, {
		.id = 0x2e,
		.name = "mpecswr",
	}, {
		.id = 0x2f,
		.name = "ppcsahbdmaw",
	}, {
		.id = 0x30,
		.name = "ppcsahbslvw",
	}, {
		.id = 0x31,
		.name = "vdebsevw",
	}, {
		.id = 0x32,
		.name = "vdembew",
	}, {
		.id = 0x33,
		.name = "vdetpmw",
	},
};

const struct tegra_mc_soc tegra20_mc_soc = {
	.clients = tegra20_mc_clients,
	.num_clients = ARRAY_SIZE(tegra20_mc_clients),
	.num_address_bits = 32,
	.client_id_mask = 0x3f,
	.intmask = MC_INT_SECURITY_VIOLATION | MC_INT_INVALID_GART_PAGE |
		   MC_INT_DECERR_EMEM,
};
