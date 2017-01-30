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

static ssize_t notrace
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

#define DUMMY_STM_MAX 32

static struct stm_data dummy_stm[DUMMY_STM_MAX];

static int nr_dummies = 4;

module_param(nr_dummies, int, 0400);

static unsigned int fail_mode;

module_param(fail_mode, int, 0600);

static int dummy_stm_link(struct stm_data *data, unsigned int master,
			  unsigned int channel)
{
	if (fail_mode && (channel & fail_mode))
		return -EINVAL;

	return 0;
}

static int dummy_stm_init(void)
{
	int i, ret = -ENOMEM;

	if (nr_dummies < 0 || nr_dummies > DUMMY_STM_MAX)
		return -EINVAL;

	for (i = 0; i < nr_dummies; i++) {
		dummy_stm[i].name = kasprintf(GFP_KERNEL, "dummy_stm.%d", i);
		if (!dummy_stm[i].name)
			goto fail_unregister;

		dummy_stm[i].sw_start		= 0x0000;
		dummy_stm[i].sw_end		= 0xffff;
		dummy_stm[i].sw_nchannels	= 0xffff;
		dummy_stm[i].packet		= dummy_stm_packet;
		dummy_stm[i].link		= dummy_stm_link;

		ret = stm_register_device(NULL, &dummy_stm[i], THIS_MODULE);
		if (ret)
			goto fail_free;
	}

	return 0;

fail_unregister:
	for (i--; i >= 0; i--) {
		stm_unregister_device(&dummy_stm[i]);
fail_free:
		kfree(dummy_stm[i].name);
	}

	return ret;

}

static void dummy_stm_exit(void)
{
	int i;

	for (i = 0; i < nr_dummies; i++) {
		stm_unregister_device(&dummy_stm[i]);
		kfree(dummy_stm[i].name);
	}
}

module_init(dummy_stm_init);
module_exit(dummy_stm_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("dummy_stm device");
MODULE_AUTHOR("Alexander Shishkin <alexander.shishkin@linux.intel.com>");
