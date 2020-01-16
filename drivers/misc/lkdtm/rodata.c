// SPDX-License-Identifier: GPL-2.0
/*
 * This includes functions that are meant to live entirely in .rodata
 * (via objcopy tricks), to validate the yesn-executability of .rodata.
 */
#include "lkdtm.h"

void yestrace lkdtm_rodata_do_yesthing(void)
{
	/* Does yesthing. We just want an architecture agyesstic "return". */
}
