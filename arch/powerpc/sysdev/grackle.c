// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Functions for setting up and using a MPC106 northbridge
 * Extracted from arch/powerpc/platforms/powermac/pci.c.
 *
 * Copyright (C) 2003 Benjamin Herrenschmuidt (benh@kernel.crashing.org)
 * Copyright (C) 1997 Paul Mackerras (paulus@samba.org)
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/of.h>

#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm/grackle.h>

#define GRACKLE_CFA(b, d, o)	(0x80 | ((b) << 8) | ((d) << 16) \
				 | (((o) & ~3) << 24))

#define GRACKLE_PICR1_LOOPSNOOP		0x00000010

static inline void grackle_set_loop_snoop(struct pci_controller *bp, int enable)
{
	unsigned int val;

	out_be32(bp->cfg_addr, GRACKLE_CFA(0, 0, 0xa8));
	val = in_le32(bp->cfg_data);
	val = enable? (val | GRACKLE_PICR1_LOOPSNOOP) :
		(val & ~GRACKLE_PICR1_LOOPSNOOP);
	out_be32(bp->cfg_addr, GRACKLE_CFA(0, 0, 0xa8));
	out_le32(bp->cfg_data, val);
	(void)in_le32(bp->cfg_data);
}

void __init setup_grackle(struct pci_controller *hose)
{
	setup_indirect_pci(hose, 0xfec00000, 0xfee00000, 0);
	if (of_machine_is_compatible("PowerMac1,1"))
		pci_add_flags(PCI_REASSIGN_ALL_BUS);
	if (of_machine_is_compatible("AAPL,PowerBook1998"))
		grackle_set_loop_snoop(hose, 1);
}
