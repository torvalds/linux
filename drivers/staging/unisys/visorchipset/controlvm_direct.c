/* controlvm_direct.c
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
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

/* This is a controlvm-related code that is dependent upon firmware running
 * on a virtual partition.
 */

#include "globals.h"
#include "uisutils.h"
#include "controlvm.h"
#define CURRENT_FILE_PC VISOR_CHIPSET_PC_controlvm_direct_c


/* We can fill in this code when we learn how to make vmcalls... */



int controlvm_init(void)
{
	return 0;
}



void controlvm_deinit(void)
{
}



HOSTADDRESS controlvm_get_channel_address(void)
{
	static BOOL warned = FALSE;
	U64 addr = 0;

	U32 size = 0;

	if (!VMCALL_SUCCESSFUL(Issue_VMCALL_IO_CONTROLVM_ADDR(&addr, &size))) {
		if (!warned) {
			ERRDRV("%s - vmcall to determine controlvm channel addr failed",
			       __func__);
			warned = TRUE;
		}
		return 0;
	}
	INFODRV("controlvm addr=%Lx", addr);
	return addr;
}
