/*
 * A dummy STM device for stm/stm_source class testing.
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * STM class implements generic infrastructure for  System Trace Module devices
 * as defined in MIPI STPv2 specification.
 */

#undef DEBUG
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/stm.h>

static ssize_t
dummy_stm_packet(struct stm_data *stm_data, unsigned int master,
		 unsigned int channel, unsigned int packet, unsigned int flags,
		 unsigned int size, const unsigned char *payload)
{
#ifdef DEBUG
	u64 pl = 0;

	if (payload)
		pl = *(u64 *)payload;

	if (size < 8)
		pl &= (1ull << (size * 8)) - 1;
	trace_printk("[%u:%u] [pkt: %x/%x] (%llx)\n", master, channel,
		     packet, size, pl);
#endif
	return size;
}

static struct stm_data dummy_stm = {
	.name		= "dummy_stm",
	.sw_start	= 0x0000,
	.sw_end		= 0xffff,
	.sw_nchannels	= 0xffff,
	.packet		= dummy_stm_packet,
};

static int dummy_stm_init(void)
{
	return stm_register_device(NULL, &dummy_stm, THIS_MODULE);
}

static void dummy_stm_exit(void)
{
	stm_unregister_device(&dummy_stm);
}

module_init(dummy_stm_init);
module_exit(dummy_stm_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("dummy_stm device");
MODULE_AUTHOR("Alexander Shishkin <alexander.shishkin@linux.intel.com>");
