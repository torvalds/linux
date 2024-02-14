// SPDX-License-Identifier: GPL-2.0
/*
 * bootstr.c:  Boot string/argument acquisition from the PROM.
 *
 * Copyright(C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright(C) 1996,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/string.h>
#include <linux/init.h>
#include <asm/oplib.h>

/* WARNING: The boot loader knows that these next three variables come one right
 *          after another in the .data section.  Do not move this stuff into
 *          the .bss section or it will break things.
 */

/* We limit BARG_LEN to 1024 because this is the size of the
 * 'barg_out' command line buffer in the SILO bootloader.
 */
#define BARG_LEN 1024
struct {
	int bootstr_len;
	int bootstr_valid;
	char bootstr_buf[BARG_LEN];
} bootstr_info = {
	.bootstr_len = BARG_LEN,
#ifdef CONFIG_CMDLINE
	.bootstr_valid = 1,
	.bootstr_buf = CONFIG_CMDLINE,
#endif
};

char * __init
prom_getbootargs(void)
{
	/* This check saves us from a panic when bootfd patches args. */
	if (bootstr_info.bootstr_valid)
		return bootstr_info.bootstr_buf;
	prom_getstring(prom_chosen_node, "bootargs",
		       bootstr_info.bootstr_buf, BARG_LEN);
	bootstr_info.bootstr_valid = 1;
	return bootstr_info.bootstr_buf;
}
