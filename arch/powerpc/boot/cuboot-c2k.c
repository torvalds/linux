/*
 * GEFanuc C2K platform code.
 *
 * Author: Remi Machet <rmachet@slac.stanford.edu>
 *
 * Originated from prpmc2800.c
 *
 * 2008 (c) Stanford University
 * 2007 (c) MontaVista, Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "types.h"
#include "stdio.h"
#include "io.h"
#include "ops.h"
#include "elf.h"
#include "gunzip_util.h"
#include "mv64x60.h"
#include "cuboot.h"
#include "ppcboot.h"

static u8 *bridge_base;

static void c2k_bridge_setup(u32 mem_size)
{
	u32 i, v[30], enables, acc_bits;
	u32 pci_base_hi, pci_base_lo, size, buf[2];
	unsigned long cpu_base;
	int rc;
	void *devp, *mv64x60_devp;
	u8 *bridge_pbase, is_coherent;
	struct mv64x60_cpu2pci_win *tbl;
	int bus;

	bridge_pbase = mv64x60_get_bridge_pbase();
	is_coherent = mv64x60_is_coherent();

	if (is_coherent)
		acc_bits = MV64x60_PCI_ACC_CNTL_SNOOP_WB
			| MV64x60_PCI_ACC_CNTL_SWAP_NONE
			| MV64x60_PCI_ACC_CNTL_MBURST_32_BYTES
			| MV64x60_PCI_ACC_CNTL_RDSIZE_32_BYTES;
	else
		acc_bits = MV64x60_PCI_ACC_CNTL_SNOOP_NONE
			| MV64x60_PCI_ACC_CNTL_SWAP_NONE
			| MV64x60_PCI_ACC_CNTL_MBURST_128_BYTES
			| MV64x60_PCI_ACC_CNTL_RDSIZE_256_BYTES;

	mv64x60_config_ctlr_windows(bridge_base, bridge_pbase, is_coherent);
	mv64x60_devp = find_node_by_compatible(NULL, "marvell,mv64360");
	if (mv64x60_devp == NULL)
		fatal("Error: Missing marvell,mv64360 device tree node\n\r");

	enables = in_le32((u32 *)(bridge_base + MV64x60_CPU_BAR_ENABLE));
	enables |= 0x007ffe00; /* Disable all cpu->pci windows */
	out_le32((u32 *)(bridge_base + MV64x60_CPU_BAR_ENABLE), enables);

	/* Get the cpu -> pci i/o & mem mappings from the device tree */
	devp = NULL;
	for (bus = 0; ; bus++) {
		char name[] = "pci ";

		name[strlen(name)-1] = bus+'0';

		devp = find_node_by_alias(name);
		if (devp == NULL)
			break;

		if (bus >= 2)
			fatal("Error: Only 2 PCI controllers are supported at" \
				" this time.\n");

		mv64x60_config_pci_windows(bridge_base, bridge_pbase, bus, 0,
				mem_size, acc_bits);

		rc = getprop(devp, "ranges", v, sizeof(v));
		if (rc == 0)
			fatal("Error: Can't find marvell,mv64360-pci ranges"
				" property\n\r");

		/* Get the cpu -> pci i/o & mem mappings from the device tree */

		for (i = 0; i < rc; i += 6) {
			switch (v[i] & 0xff000000) {
			case 0x01000000: /* PCI I/O Space */
				tbl = mv64x60_cpu2pci_io;
				break;
			case 0x02000000: /* PCI MEM Space */
				tbl = mv64x60_cpu2pci_mem;
				break;
			default:
				continue;
			}

			pci_base_hi = v[i+1];
			pci_base_lo = v[i+2];
			cpu_base = v[i+3];
			size = v[i+5];

			buf[0] = cpu_base;
			buf[1] = size;

			if (!dt_xlate_addr(devp, buf, sizeof(buf), &cpu_base))
				fatal("Error: Can't translate PCI address " \
						"0x%x\n\r", (u32)cpu_base);

			mv64x60_config_cpu2pci_window(bridge_base, bus,
				pci_base_hi, pci_base_lo, cpu_base, size, tbl);
		}

		enables &= ~(3<<(9+bus*5)); /* Enable cpu->pci<bus> i/o,
						cpu->pci<bus> mem0 */
		out_le32((u32 *)(bridge_base + MV64x60_CPU_BAR_ENABLE),
			enables);
	};
}

static void c2k_fixups(void)
{
	u32 mem_size;

	mem_size = mv64x60_get_mem_size(bridge_base);
	c2k_bridge_setup(mem_size); /* Do necessary bridge setup */
}

#define MV64x60_MPP_CNTL_0	0xf000
#define MV64x60_MPP_CNTL_2	0xf008
#define MV64x60_GPP_IO_CNTL	0xf100
#define MV64x60_GPP_LEVEL_CNTL	0xf110
#define MV64x60_GPP_VALUE_SET	0xf118

static void c2k_reset(void)
{
	u32 temp;

	udelay(5000000);

	if (bridge_base != 0) {
		temp = in_le32((u32 *)(bridge_base + MV64x60_MPP_CNTL_0));
		temp &= 0xFFFF0FFF;
		out_le32((u32 *)(bridge_base + MV64x60_MPP_CNTL_0), temp);

		temp = in_le32((u32 *)(bridge_base + MV64x60_GPP_LEVEL_CNTL));
		temp |= 0x00000004;
		out_le32((u32 *)(bridge_base + MV64x60_GPP_LEVEL_CNTL), temp);

		temp = in_le32((u32 *)(bridge_base + MV64x60_GPP_IO_CNTL));
		temp |= 0x00000004;
		out_le32((u32 *)(bridge_base + MV64x60_GPP_IO_CNTL), temp);

		temp = in_le32((u32 *)(bridge_base + MV64x60_MPP_CNTL_2));
		temp &= 0xFFFF0FFF;
		out_le32((u32 *)(bridge_base + MV64x60_MPP_CNTL_2), temp);

		temp = in_le32((u32 *)(bridge_base + MV64x60_GPP_LEVEL_CNTL));
		temp |= 0x00080000;
		out_le32((u32 *)(bridge_base + MV64x60_GPP_LEVEL_CNTL), temp);

		temp = in_le32((u32 *)(bridge_base + MV64x60_GPP_IO_CNTL));
		temp |= 0x00080000;
		out_le32((u32 *)(bridge_base + MV64x60_GPP_IO_CNTL), temp);

		out_le32((u32 *)(bridge_base + MV64x60_GPP_VALUE_SET),
				0x00080004);
	}

	for (;;);
}

static bd_t bd;

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
			unsigned long r6, unsigned long r7)
{
	CUBOOT_INIT();

	fdt_init(_dtb_start);

	bridge_base = mv64x60_get_bridge_base();

	platform_ops.fixups = c2k_fixups;
	platform_ops.exit = c2k_reset;

	if (serial_console_init() < 0)
		exit();
}
