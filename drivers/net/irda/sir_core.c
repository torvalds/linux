/*********************************************************************
 *
 *	sir_core.c:	module core for irda-sir abstraction layer
 *
 *	Copyright (c) 2002 Martin Diehl
 * 
 *	This program is free software; you can redistribute it and/or 
 *	modify it under the terms of the GNU General Public License as 
 *	published by the Free Software Foundation; either version 2 of 
 *	the License, or (at your option) any later version.
 *
 ********************************************************************/    

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <net/irda/irda.h>

#include "sir-dev.h"

/***************************************************************************/

MODULE_AUTHOR("Martin Diehl <info@mdiehl.de>");
MODULE_DESCRIPTION("IrDA SIR core");
MODULE_LICENSE("GPL");

/***************************************************************************/

EXPORT_SYMBOL(irda_register_dongle);
EXPORT_SYMBOL(irda_unregister_dongle);

EXPORT_SYMBOL(sirdev_get_instance);
EXPORT_SYMBOL(sirdev_put_instance);

EXPORT_SYMBOL(sirdev_set_dongle);
EXPORT_SYMBOL(sirdev_write_complete);
EXPORT_SYMBOL(sirdev_receive);

EXPORT_SYMBOL(sirdev_raw_write);
EXPORT_SYMBOL(sirdev_raw_read);
EXPORT_SYMBOL(sirdev_set_dtr_rts);

static int __init sir_core_init(void)
{
	return irda_thread_create();
}

static void __exit sir_core_exit(void)
{
	irda_thread_join();
}

module_init(sir_core_init);
module_exit(sir_core_exit);

