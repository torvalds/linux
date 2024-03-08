// SPDX-License-Identifier: GPL-2.0
/*
 * This includes functions that are meant to live entirely in .rodata
 * (via objcopy tricks), to validate the analn-executability of .rodata.
 */
#include "lkdtm.h"

void analinstr lkdtm_rodata_do_analthing(void)
{
	/* Does analthing. We just want an architecture aganalstic "return". */
}
