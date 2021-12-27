// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2021 Marvell International Ltd. All rights reserved */

#include <linux/kernel.h>
#include <linux/types.h>

#include "prestera.h"

int prestera_router_init(struct prestera_switch *sw)
{
	struct prestera_router *router;

	router = kzalloc(sizeof(*sw->router), GFP_KERNEL);
	if (!router)
		return -ENOMEM;

	sw->router = router;
	router->sw = sw;

	return 0;
}

void prestera_router_fini(struct prestera_switch *sw)
{
	kfree(sw->router);
	sw->router = NULL;
}
