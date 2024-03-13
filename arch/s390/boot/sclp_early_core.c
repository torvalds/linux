// SPDX-License-Identifier: GPL-2.0
#include "boot.h"
#include "../../../drivers/s390/char/sclp_early_core.c"

/* SCLP early buffer must stay page-aligned and below 2GB */
static char __sclp_early_sccb[EXT_SCCB_READ_SCP] __aligned(PAGE_SIZE);

void sclp_early_setup_buffer(void)
{
	sclp_early_set_buffer(&__sclp_early_sccb);
}
