// SPDX-License-Identifier: GPL-2.0
/*
 * Basic framing protocol for STM devices.
 * Copyright (c) 2018, Intel Corporation.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/stm.h>
#include "stm.h"

static ssize_t basic_write(struct stm_data *data, struct stm_output *output,
			   unsigned int chan, const char *buf, size_t count,
			   struct stm_source_data *source)
{
	unsigned int c = output->channel + chan;
	unsigned int m = output->master;
	const unsigned char nil = 0;
	ssize_t sz;

	sz = stm_data_write(data, m, c, true, buf, count);
	if (sz > 0)
		data->packet(data, m, c, STP_PACKET_FLAG, 0, 0, &nil);

	return sz;
}

static const struct stm_protocol_driver basic_pdrv = {
	.owner	= THIS_MODULE,
	.name	= "p_basic",
	.write	= basic_write,
};

static int basic_stm_init(void)
{
	return stm_register_protocol(&basic_pdrv);
}

static void basic_stm_exit(void)
{
	stm_unregister_protocol(&basic_pdrv);
}

module_init(basic_stm_init);
module_exit(basic_stm_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Basic STM framing protocol driver");
MODULE_AUTHOR("Alexander Shishkin <alexander.shishkin@linux.intel.com>");
