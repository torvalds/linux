// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi MIPS SoC support
 *
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <asm/machine.h>
#include <asm/prom.h>

#define DEVCPU_GCB_CHIP_REGS_CHIP_ID	0x71070000
#define CHIP_ID_PART_ID			GENMASK(27, 12)

#define OCELOT_PART_ID			(0x7514 << 12)

#define UART_UART			0x70100000

static __init bool ocelot_detect(void)
{
	u32 rev;
	int idx;

	/* Look for the TLB entry set up by redboot before trying to use it */
	write_c0_entryhi(DEVCPU_GCB_CHIP_REGS_CHIP_ID);
	mtc0_tlbw_hazard();
	tlb_probe();
	tlb_probe_hazard();
	idx = read_c0_index();
	if (idx < 0)
		return 0;

	/* A TLB entry exists, lets assume its usable and check the CHIP ID */
	rev = __raw_readl((void __iomem *)DEVCPU_GCB_CHIP_REGS_CHIP_ID);

	if ((rev & CHIP_ID_PART_ID) != OCELOT_PART_ID)
		return 0;

	/* Copy command line from bootloader early for Initrd detection */
	if (fw_arg0 < 10 && (fw_arg1 & 0xFFF00000) == 0x80000000) {
		unsigned int prom_argc = fw_arg0;
		const char **prom_argv = (const char **)fw_arg1;

		if (prom_argc > 1 && strlen(prom_argv[1]) > 0)
			/* ignore all built-in args if any f/w args given */
			strcpy(arcs_cmdline, prom_argv[1]);
	}

	return 1;
}

static void __init ocelot_earlyprintk_init(void)
{
	void __iomem *uart_base;

	uart_base = ioremap_nocache(UART_UART, 0x20);
	setup_8250_early_printk_port((unsigned long)uart_base, 2, 50000);
}

static void __init ocelot_late_init(void)
{
	ocelot_earlyprintk_init();
}

static __init const void *ocelot_fixup_fdt(const void *fdt,
					   const void *match_data)
{
	/* This has to be done so late because ioremap needs to work */
	late_time_init = ocelot_late_init;

	return fdt;
}

extern char __dtb_ocelot_pcb123_begin[];

MIPS_MACHINE(ocelot) = {
	.fdt = __dtb_ocelot_pcb123_begin,
	.fixup_fdt = ocelot_fixup_fdt,
	.detect = ocelot_detect,
};
