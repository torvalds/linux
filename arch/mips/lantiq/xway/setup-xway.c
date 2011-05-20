/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2011 John Crispin <blogic@openwrt.org>
 */

#include <lantiq_soc.h>

#include "../prom.h"
#include "devices.h"

void __init ltq_soc_setup(void)
{
	ltq_register_asc(0);
	ltq_register_asc(1);
	ltq_register_gpio();
	ltq_register_wdt();
}
