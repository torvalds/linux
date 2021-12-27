// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2021 Marvell International Ltd. All rights reserved */

#include <linux/kernel.h>
#include <linux/types.h>

#include "prestera.h"
#include "prestera_router_hw.h"

int prestera_router_init(struct prestera_switch *sw)
{
	struct prestera_router *router;
	int err;

	router = kzalloc(sizeof(*sw->router), GFP_KERNEL);
	if (!router)
		return -ENOMEM;

	sw->router = router;
	router->sw = sw;

	err = prestera_router_hw_init(sw);
	if (err)
		goto err_router_lib_init;

	return 0;

err_router_lib_init:
	kfree(sw->router);
	return err;
}

void prestera_router_fini(struct prestera_switch *sw)
{
	kfree(sw->router);
	sw->router = NULL;
}
