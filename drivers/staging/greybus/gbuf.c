/*
 * Greybus gbuf handling
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/device.h>

#include "greybus.h"


struct gbuf *greybus_alloc_gbuf(struct greybus_device *gdev,
				struct gdev_cport *cport,
				gfp_t mem_flags)
{
	return NULL;
}

void greybus_free_gbuf(struct gbuf *gbuf)
{
}

int greybus_submit_gbuf(struct gbuf *gbuf, gfp_t mem_flags)
{
	return -ENOMEM;
}

int greybus_kill_gbuf(struct gbuf *gbuf)
{
	return -ENOMEM;
}



