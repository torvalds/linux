// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RDMA Network Block Driver
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */
#include "rnbd-proto.h"

const char *rnbd_access_mode_str(enum rnbd_access_mode mode)
{
	switch (mode) {
	case RNBD_ACCESS_RO:
		return "ro";
	case RNBD_ACCESS_RW:
		return "rw";
	case RNBD_ACCESS_MIGRATION:
		return "migration";
	default:
		return "unknown";
	}
}
