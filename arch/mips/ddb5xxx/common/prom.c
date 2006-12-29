/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/ddb5xxx/ddb5xxx.h>
#include <asm/debug.h>

const char *get_system_type(void)
{
	switch (mips_machtype) {
	case MACH_NEC_DDB5477:		return "NEC DDB Vrc-5477";
	case MACH_NEC_ROCKHOPPER:	return "NEC Rockhopper";
	case MACH_NEC_ROCKHOPPERII:     return "NEC RockhopperII";
	default:			return "Unknown NEC board";
	}
}

#if defined(CONFIG_DDB5477)
void ddb5477_runtime_detection(void);
#endif

/* [jsun@junsun.net] PMON passes arguments in C main() style */
void __init prom_init(void)
{
	int argc = fw_arg0;
	char **arg = (char**) fw_arg1;
	int i;

	/* if user passes kernel args, ignore the default one */
	if (argc > 1)
		arcs_cmdline[0] = '\0';

	/* arg[0] is "g", the rest is boot parameters */
	for (i = 1; i < argc; i++) {
		if (strlen(arcs_cmdline) + strlen(arg[i] + 1)
		    >= sizeof(arcs_cmdline))
			break;
		strcat(arcs_cmdline, arg[i]);
		strcat(arcs_cmdline, " ");
	}

	mips_machgroup = MACH_GROUP_NEC_DDB;

#if defined(CONFIG_DDB5477)
	ddb5477_runtime_detection();
	add_memory_region(0, board_ram_size, BOOT_MEM_RAM);
#endif
}

void __init prom_free_prom_memory(void)
{
}

#if defined(CONFIG_DDB5477)

#define DEFAULT_LCS1_BASE 0x19000000
#define TESTVAL1 'K'
#define TESTVAL2 'S'

int board_ram_size;
void ddb5477_runtime_detection(void)
{
	volatile char *test_offset;
	char saved_test_byte;

        /* Determine if this is a DDB5477 board, or a BSB-VR0300
           base board.  We can tell by checking for the location of
           the NVRAM.  It lives at the beginning of LCS1 on the DDB5477,
           and the beginning of LCS1 on the BSB-VR0300 is flash memory.
           The first 2K of the NVRAM are reserved, so don't we'll poke
           around just after that.
         */

	/* We can only use the PCI bus to distinquish between
	   the Rockhopper and RockhopperII backplanes and this must
	   wait until ddb5477_board_init() in setup.c after the 5477
	   is initialized.  So, until then handle
	   both Rockhopper and RockhopperII backplanes as Rockhopper 1
	 */

        test_offset = (char *)KSEG1ADDR(DEFAULT_LCS1_BASE + 0x800);
        saved_test_byte = *test_offset;

        *test_offset = TESTVAL1;
        if (*test_offset != TESTVAL1) {
                /* We couldn't set our test value, so it must not be NVRAM,
                   so it's a BSB_VR0300 */
		mips_machtype = MACH_NEC_ROCKHOPPER;
        } else {
                /* We may have gotten lucky, and the TESTVAL1 was already
                   stored at the test location, so we must check a second
                   test value */
                *test_offset = TESTVAL2;
                if (*test_offset != TESTVAL2) {
                        /* OK, we couldn't set this value either, so it must
                           definately be a BSB_VR0300 */
			mips_machtype = MACH_NEC_ROCKHOPPER;
                } else {
                        /* We could change the value twice, so it must be
                        NVRAM, so it's a DDB_VRC5477 */
			mips_machtype = MACH_NEC_DDB5477;
                }
        }
        /* Restore the original byte */
        *test_offset = saved_test_byte;

	/* before we know a better way, we will trust PMON for getting
	 * RAM size
	 */
	board_ram_size = 1 << (36 - (ddb_in32(DDB_SDRAM0) & 0xf));

	db_run(printk("DDB run-time detection : %s, %d MB RAM\n",
				mips_machtype == MACH_NEC_DDB5477 ?
				"DDB5477" : "Rockhopper",
				board_ram_size >> 20));

	/* we can't handle ram size > 128 MB */
	db_assert(board_ram_size <= (128 << 20));
}
#endif
