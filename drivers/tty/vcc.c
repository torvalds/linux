/* vcc.c: sun4v virtual channel concentrator
 *
 * Copyright (C) 2017 Oracle. All rights reserved.
 */

#include <linux/module.h>

static int __init vcc_init(void)
{
	return 0;
}

static void __exit vcc_exit(void)
{
}

module_init(vcc_init);
module_exit(vcc_exit);
