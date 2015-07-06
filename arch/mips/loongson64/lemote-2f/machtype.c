/*
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <asm/bootinfo.h>

#include <loongson.h>

void __init mach_prom_init_machtype(void)
{
	/* We share the same kernel image file among Lemote 2F family
	 * of machines, and provide the machtype= kernel command line
	 * to users to indicate their machine, this command line will
	 * be passed by the latest PMON automatically. and fortunately,
	 * up to now, we can get the machine type from the PMON_VER=
	 * commandline directly except the NAS machine, In the old
	 * machines, this will help the users a lot.
	 *
	 * If no "machtype=" passed, get machine type from "PMON_VER=".
	 *	PMON_VER=LM8089		Lemote 8.9'' netbook
	 *		 LM8101		Lemote 10.1'' netbook
	 *	(The above two netbooks have the same kernel support)
	 *		 LM6XXX		Lemote FuLoong(2F) box series
	 *		 LM9XXX		Lemote LynLoong PC series
	 */
	if (strstr(arcs_cmdline, "PMON_VER=LM")) {
		if (strstr(arcs_cmdline, "PMON_VER=LM8"))
			mips_machtype = MACH_LEMOTE_YL2F89;
		else if (strstr(arcs_cmdline, "PMON_VER=LM6"))
			mips_machtype = MACH_LEMOTE_FL2F;
		else if (strstr(arcs_cmdline, "PMON_VER=LM9"))
			mips_machtype = MACH_LEMOTE_LL2F;
		else
			mips_machtype = MACH_LEMOTE_NAS;

		strcat(arcs_cmdline, " machtype=");
		strcat(arcs_cmdline, get_system_type());
		strcat(arcs_cmdline, " ");
	}
}
