/*
 * Test "sink" Greybus driver.
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "greybus.h"

struct test_device {
	struct greybus_module *gmod;
};

int gb_register_cport_complete(struct greybus_module *gmod,
			       gbuf_complete_t handler, int cport,
			       void *context);
void gb_deregister_cport_complete(int cport);



static int test_init(void)
{
	return 0;
}

static void test_exit(void)
{
}

module_init(test_init);
module_exit(test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
