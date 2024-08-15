// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Motload compatibility for the Emerson/Artesyn MVME7100
 *
 * Copyright 2016 Elettra-Sincrotrone Trieste S.C.p.A.
 *
 * Author: Alessio Igor Bogani <alessio.bogani@elettra.eu>
 */

#include "ops.h"
#include "stdio.h"
#include "cuboot.h"

#define TARGET_86xx
#define TARGET_HAS_ETH1
#define TARGET_HAS_ETH2
#define TARGET_HAS_ETH3
#include "ppcboot.h"

static bd_t bd;

BSS_STACK(16384);

static void mvme7100_fixups(void)
{
	void *devp;
	unsigned long busfreq = bd.bi_busfreq * 1000000;

	dt_fixup_cpu_clocks(bd.bi_intfreq * 1000000, busfreq / 4, busfreq);

	devp = finddevice("/soc@f1000000");
	if (devp)
		setprop(devp, "bus-frequency", &busfreq, sizeof(busfreq));

	devp = finddevice("/soc/serial@4500");
	if (devp)
		setprop(devp, "clock-frequency", &busfreq, sizeof(busfreq));

	dt_fixup_memory(bd.bi_memstart, bd.bi_memsize);

	dt_fixup_mac_address_by_alias("ethernet0", bd.bi_enetaddr);
	dt_fixup_mac_address_by_alias("ethernet1", bd.bi_enet1addr);
	dt_fixup_mac_address_by_alias("ethernet2", bd.bi_enet2addr);
	dt_fixup_mac_address_by_alias("ethernet3", bd.bi_enet3addr);
}

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		   unsigned long r6, unsigned long r7)
{
	CUBOOT_INIT();
	fdt_init(_dtb_start);
	serial_console_init();
	platform_ops.fixups = mvme7100_fixups;
}
