/*
 * (C) 2001-2005 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <linux/module.h>

#include <asm/hw_irq.h>
#include <asm/iseries/hv_call_sc.h>

EXPORT_SYMBOL(HvCall0);
EXPORT_SYMBOL(HvCall1);
EXPORT_SYMBOL(HvCall2);
EXPORT_SYMBOL(HvCall3);
EXPORT_SYMBOL(HvCall4);
EXPORT_SYMBOL(HvCall5);
EXPORT_SYMBOL(HvCall6);
EXPORT_SYMBOL(HvCall7);

#ifdef CONFIG_SMP
EXPORT_SYMBOL(local_get_flags);
EXPORT_SYMBOL(local_irq_disable);
EXPORT_SYMBOL(local_irq_restore);
#endif
