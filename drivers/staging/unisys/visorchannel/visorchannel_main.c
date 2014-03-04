/* visorchannel_main.c
 *
 * Copyright © 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/*
 *  This is a module "wrapper" around visorchannel_funcs.
 */

#include "globals.h"
#include "channel.h"
#include "visorchannel.h"
#include "guidutils.h"

#define MYDRVNAME "visorchannel"

static int __init
visorchannel_init(void)
{
	INFODRV("driver version %s loaded", VERSION);
	return 0;
}

static void
visorchannel_exit(void)
{
	INFODRV("driver unloaded");
}

module_init(visorchannel_init);
module_exit(visorchannel_exit);

MODULE_AUTHOR("Unisys");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Supervisor channel driver for service partition: ver "
		   VERSION);
MODULE_VERSION(VERSION);
