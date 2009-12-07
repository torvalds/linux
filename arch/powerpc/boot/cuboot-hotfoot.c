/*
 * Old U-boot compatibility for Esteem 195E Hotfoot CPU Board
 *
 * Author: Solomon Peachy <solomon@linux-wlan.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "ops.h"
#include "stdio.h"
#include "reg.h"
#include "dcr.h"
#include "4xx.h"
#include "cuboot.h"

#define TARGET_4xx
#define TARGET_HOTFOOT

#include "ppcboot-hotfoot.h"

static bd_t bd;

#define NUM_REGS 3

static void hotfoot_fixups(void)
{
	u32 uart = mfdcr(DCRN_CPC0_UCR) & 0x7f;

	dt_fixup_memory(bd.bi_memstart, bd.bi_memsize); 

	dt_fixup_cpu_clocks(bd.bi_procfreq, bd.bi_procfreq, 0);
	dt_fixup_clock("/plb", bd.bi_plb_busfreq);
	dt_fixup_clock("/plb/opb", bd.bi_opbfreq);
	dt_fixup_clock("/plb/ebc", bd.bi_pci_busfreq);
	dt_fixup_clock("/plb/opb/serial@ef600300", bd.bi_procfreq / uart); 
	dt_fixup_clock("/plb/opb/serial@ef600400", bd.bi_procfreq / uart); 
	
	dt_fixup_mac_address_by_alias("ethernet0", bd.bi_enetaddr);
	dt_fixup_mac_address_by_alias("ethernet1", bd.bi_enet1addr);

	/* Is this a single eth/serial board? */
	if ((bd.bi_enet1addr[0] == 0) && 
	    (bd.bi_enet1addr[1] == 0) &&
	    (bd.bi_enet1addr[2] == 0) &&
	    (bd.bi_enet1addr[3] == 0) &&
	    (bd.bi_enet1addr[4] == 0) &&
	    (bd.bi_enet1addr[5] == 0)) {
		void *devp;

		printf("Trimming devtree for single serial/eth board\n");

		devp = finddevice("/plb/opb/serial@ef600300");
		if (!devp)
			fatal("Can't find node for /plb/opb/serial@ef600300");
		del_node(devp);

		devp = finddevice("/plb/opb/ethernet@ef600900");
		if (!devp)
			fatal("Can't find node for /plb/opb/ethernet@ef600900");
		del_node(devp);
	}

	ibm4xx_quiesce_eth((u32 *)0xef600800, (u32 *)0xef600900);

	/* Fix up flash size in fdt for 4M boards. */
	if (bd.bi_flashsize < 0x800000) {
		u32 regs[NUM_REGS];
		void *devp = finddevice("/plb/ebc/nor_flash@0");
		if (!devp)
			fatal("Can't find FDT node for nor_flash!??");

		printf("Fixing devtree for 4M Flash\n");
		
		/* First fix up the base addresse */
		getprop(devp, "reg", regs, sizeof(regs));
		regs[0] = 0;
		regs[1] = 0xffc00000;
		regs[2] = 0x00400000;
		setprop(devp, "reg", regs, sizeof(regs));
		
		/* Then the offsets */
		devp = finddevice("/plb/ebc/nor_flash@0/partition@0");
		if (!devp)
			fatal("Can't find FDT node for partition@0");
		getprop(devp, "reg", regs, 2*sizeof(u32));
		regs[0] -= 0x400000;
		setprop(devp, "reg", regs,  2*sizeof(u32));

		devp = finddevice("/plb/ebc/nor_flash@0/partition@1");
		if (!devp)
			fatal("Can't find FDT node for partition@1");
		getprop(devp, "reg", regs, 2*sizeof(u32));
		regs[0] -= 0x400000;
		setprop(devp, "reg", regs,  2*sizeof(u32));

		devp = finddevice("/plb/ebc/nor_flash@0/partition@2");
		if (!devp)
			fatal("Can't find FDT node for partition@2");
		getprop(devp, "reg", regs, 2*sizeof(u32));
		regs[0] -= 0x400000;
		setprop(devp, "reg", regs,  2*sizeof(u32));

		devp = finddevice("/plb/ebc/nor_flash@0/partition@3");
		if (!devp)
			fatal("Can't find FDT node for partition@3");
		getprop(devp, "reg", regs, 2*sizeof(u32));
		regs[0] -= 0x400000;
		setprop(devp, "reg", regs,  2*sizeof(u32));

		devp = finddevice("/plb/ebc/nor_flash@0/partition@4");
		if (!devp)
			fatal("Can't find FDT node for partition@4");
		getprop(devp, "reg", regs, 2*sizeof(u32));
		regs[0] -= 0x400000;
		setprop(devp, "reg", regs,  2*sizeof(u32));

		devp = finddevice("/plb/ebc/nor_flash@0/partition@6");
		if (!devp)
			fatal("Can't find FDT node for partition@6");
		getprop(devp, "reg", regs, 2*sizeof(u32));
		regs[0] -= 0x400000;
		setprop(devp, "reg", regs,  2*sizeof(u32));

		/* Delete the FeatFS node */
		devp = finddevice("/plb/ebc/nor_flash@0/partition@5");
		if (!devp)
			fatal("Can't find FDT node for partition@5");
		del_node(devp);
	}
}

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		   unsigned long r6, unsigned long r7)
{
	CUBOOT_INIT();
	platform_ops.fixups = hotfoot_fixups;
        platform_ops.exit = ibm40x_dbcr_reset;
	fdt_init(_dtb_start);
	serial_console_init();
}
