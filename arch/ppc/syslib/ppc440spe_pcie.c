/*
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Roland Dreier <rolandd@cisco.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/reg.h>
#include <asm/io.h>
#include <asm/ibm44x.h>

#include "ppc440spe_pcie.h"

static int
pcie_read_config(struct pci_bus *bus, unsigned int devfn, int offset,
		     int len, u32 *val)
{
	struct pci_controller *hose = bus->sysdata;

	if (PCI_SLOT(devfn) != 1)
		return PCIBIOS_DEVICE_NOT_FOUND;

	offset += devfn << 12;

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		*val = in_8(hose->cfg_data + offset);
		break;
	case 2:
		*val = in_le16(hose->cfg_data + offset);
		break;
	default:
		*val = in_le32(hose->cfg_data + offset);
		break;
	}

	if (0) printk("%s: read %x(%d) @ %x\n", __func__, *val, len, offset);

	return PCIBIOS_SUCCESSFUL;
}

static int
pcie_write_config(struct pci_bus *bus, unsigned int devfn, int offset,
		      int len, u32 val)
{
	struct pci_controller *hose = bus->sysdata;

	if (PCI_SLOT(devfn) != 1)
		return PCIBIOS_DEVICE_NOT_FOUND;

	offset += devfn << 12;

	switch (len) {
	case 1:
		out_8(hose->cfg_data + offset, val);
		break;
	case 2:
		out_le16(hose->cfg_data + offset, val);
		break;
	default:
		out_le32(hose->cfg_data + offset, val);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pcie_pci_ops =
{
	.read  = pcie_read_config,
	.write = pcie_write_config
};

enum {
	PTYPE_ENDPOINT		= 0x0,
	PTYPE_LEGACY_ENDPOINT	= 0x1,
	PTYPE_ROOT_PORT		= 0x4,

	LNKW_X1			= 0x1,
	LNKW_X4			= 0x4,
	LNKW_X8			= 0x8
};

static void check_error(void)
{
	u32 valPE0, valPE1, valPE2;

	/* SDR0_PEGPLLLCT1 reset */
	if (!(valPE0 = SDR_READ(PESDR0_PLLLCT1) & 0x01000000)) {
		printk(KERN_INFO "PCIE: SDR0_PEGPLLLCT1 reset error 0x%8x\n", valPE0);
	}

	valPE0 = SDR_READ(PESDR0_RCSSET);
	valPE1 = SDR_READ(PESDR1_RCSSET);
	valPE2 = SDR_READ(PESDR2_RCSSET);

	/* SDR0_PExRCSSET rstgu */
	if ( !(valPE0 & 0x01000000) ||
	     !(valPE1 & 0x01000000) ||
	     !(valPE2 & 0x01000000)) {
		printk(KERN_INFO "PCIE:  SDR0_PExRCSSET rstgu error\n");
	}

	/* SDR0_PExRCSSET rstdl */
	if ( !(valPE0 & 0x00010000) ||
	     !(valPE1 & 0x00010000) ||
	     !(valPE2 & 0x00010000)) {
		printk(KERN_INFO "PCIE:  SDR0_PExRCSSET rstdl error\n");
	}

	/* SDR0_PExRCSSET rstpyn */
	if ( (valPE0 & 0x00001000) ||
	     (valPE1 & 0x00001000) ||
	     (valPE2 & 0x00001000)) {
		printk(KERN_INFO "PCIE:  SDR0_PExRCSSET rstpyn error\n");
	}

	/* SDR0_PExRCSSET hldplb */
	if ( (valPE0 & 0x10000000) ||
	     (valPE1 & 0x10000000) ||
	     (valPE2 & 0x10000000)) {
		printk(KERN_INFO "PCIE:  SDR0_PExRCSSET hldplb error\n");
	}

	/* SDR0_PExRCSSET rdy */
	if ( (valPE0 & 0x00100000) ||
	     (valPE1 & 0x00100000) ||
	     (valPE2 & 0x00100000)) {
		printk(KERN_INFO "PCIE:  SDR0_PExRCSSET rdy error\n");
	}

	/* SDR0_PExRCSSET shutdown */
	if ( (valPE0 & 0x00000100) ||
	     (valPE1 & 0x00000100) ||
	     (valPE2 & 0x00000100)) {
		printk(KERN_INFO "PCIE:  SDR0_PExRCSSET shutdown error\n");
	}
}

/*
 * Initialize PCI Express core as described in User Manual section 27.12.1
 */
int ppc440spe_init_pcie(void)
{
	/* Set PLL clock receiver to LVPECL */
	SDR_WRITE(PESDR0_PLLLCT1, SDR_READ(PESDR0_PLLLCT1) | 1 << 28);

	check_error();

	printk(KERN_INFO "PCIE initialization OK\n");

	if (!(SDR_READ(PESDR0_PLLLCT2) & 0x10000))
		printk(KERN_INFO "PESDR_PLLCT2 resistance calibration failed (0x%08x)\n",
		       SDR_READ(PESDR0_PLLLCT2));

	/* De-assert reset of PCIe PLL, wait for lock */
	SDR_WRITE(PESDR0_PLLLCT1, SDR_READ(PESDR0_PLLLCT1) & ~(1 << 24));
	udelay(3);

	return 0;
}

int ppc440spe_init_pcie_rootport(int port)
{
	static int core_init;
	void __iomem *utl_base;
	u32 val = 0;
	int i;

	if (!core_init) {
		++core_init;
		i = ppc440spe_init_pcie();
		if (i)
			return i;
	}

	/*
	 * Initialize various parts of the PCI Express core for our port:
	 *
	 * - Set as a root port and enable max width
	 *   (PXIE0 -> X8, PCIE1 and PCIE2 -> X4).
	 * - Set up UTL configuration.
	 * - Increase SERDES drive strength to levels suggested by AMCC.
	 * - De-assert RSTPYN, RSTDL and RSTGU.
	 */
	switch (port) {
	case 0:
		SDR_WRITE(PESDR0_DLPSET, PTYPE_ROOT_PORT << 20 | LNKW_X8 << 12);

		SDR_WRITE(PESDR0_UTLSET1, 0x21222222);
		SDR_WRITE(PESDR0_UTLSET2, 0x11000000);

		SDR_WRITE(PESDR0_HSSL0SET1, 0x35000000);
		SDR_WRITE(PESDR0_HSSL1SET1, 0x35000000);
		SDR_WRITE(PESDR0_HSSL2SET1, 0x35000000);
		SDR_WRITE(PESDR0_HSSL3SET1, 0x35000000);
		SDR_WRITE(PESDR0_HSSL4SET1, 0x35000000);
		SDR_WRITE(PESDR0_HSSL5SET1, 0x35000000);
		SDR_WRITE(PESDR0_HSSL6SET1, 0x35000000);
		SDR_WRITE(PESDR0_HSSL7SET1, 0x35000000);

		SDR_WRITE(PESDR0_RCSSET,
			  (SDR_READ(PESDR0_RCSSET) & ~(1 << 24 | 1 << 16)) | 1 << 12);
		break;

	case 1:
		SDR_WRITE(PESDR1_DLPSET, PTYPE_ROOT_PORT << 20 | LNKW_X4 << 12);

		SDR_WRITE(PESDR1_UTLSET1, 0x21222222);
		SDR_WRITE(PESDR1_UTLSET2, 0x11000000);

		SDR_WRITE(PESDR1_HSSL0SET1, 0x35000000);
		SDR_WRITE(PESDR1_HSSL1SET1, 0x35000000);
		SDR_WRITE(PESDR1_HSSL2SET1, 0x35000000);
		SDR_WRITE(PESDR1_HSSL3SET1, 0x35000000);

		SDR_WRITE(PESDR1_RCSSET,
			  (SDR_READ(PESDR1_RCSSET) & ~(1 << 24 | 1 << 16)) | 1 << 12);
		break;

	case 2:
		SDR_WRITE(PESDR2_DLPSET, PTYPE_ROOT_PORT << 20 | LNKW_X4 << 12);

		SDR_WRITE(PESDR2_UTLSET1, 0x21222222);
		SDR_WRITE(PESDR2_UTLSET2, 0x11000000);

		SDR_WRITE(PESDR2_HSSL0SET1, 0x35000000);
		SDR_WRITE(PESDR2_HSSL1SET1, 0x35000000);
		SDR_WRITE(PESDR2_HSSL2SET1, 0x35000000);
		SDR_WRITE(PESDR2_HSSL3SET1, 0x35000000);

		SDR_WRITE(PESDR2_RCSSET,
			  (SDR_READ(PESDR2_RCSSET) & ~(1 << 24 | 1 << 16)) | 1 << 12);
		break;
	}

	mdelay(1000);

	switch (port) {
	case 0: val = SDR_READ(PESDR0_RCSSTS); break;
	case 1: val = SDR_READ(PESDR1_RCSSTS); break;
	case 2: val = SDR_READ(PESDR2_RCSSTS); break;
	}

	if (!(val & (1 << 20)))
		printk(KERN_INFO "PCIE%d: PGRST inactive\n", port);
	else
		printk(KERN_WARNING "PGRST for PCIE%d failed %08x\n", port, val);

	switch (port) {
	case 0: printk(KERN_INFO "PCIE0: LOOP %08x\n", SDR_READ(PESDR0_LOOP)); break;
	case 1: printk(KERN_INFO "PCIE1: LOOP %08x\n", SDR_READ(PESDR1_LOOP)); break;
	case 2: printk(KERN_INFO "PCIE2: LOOP %08x\n", SDR_READ(PESDR2_LOOP)); break;
	}

	/*
	 * Map UTL registers at 0xc_1000_0n00
	 */
	switch (port) {
	case 0:
		mtdcr(DCRN_PEGPL_REGBAH(PCIE0), 0x0000000c);
		mtdcr(DCRN_PEGPL_REGBAL(PCIE0), 0x10000000);
		mtdcr(DCRN_PEGPL_REGMSK(PCIE0), 0x00007001);
		mtdcr(DCRN_PEGPL_SPECIAL(PCIE0), 0x68782800);
		break;

	case 1:
		mtdcr(DCRN_PEGPL_REGBAH(PCIE1), 0x0000000c);
		mtdcr(DCRN_PEGPL_REGBAL(PCIE1), 0x10001000);
		mtdcr(DCRN_PEGPL_REGMSK(PCIE1), 0x00007001);
		mtdcr(DCRN_PEGPL_SPECIAL(PCIE1), 0x68782800);
		break;

	case 2:
		mtdcr(DCRN_PEGPL_REGBAH(PCIE2), 0x0000000c);
		mtdcr(DCRN_PEGPL_REGBAL(PCIE2), 0x10002000);
		mtdcr(DCRN_PEGPL_REGMSK(PCIE2), 0x00007001);
		mtdcr(DCRN_PEGPL_SPECIAL(PCIE2), 0x68782800);
	}

	utl_base = ioremap64(0xc10000000ull + 0x1000 * port, 0x100);

	/*
	 * Set buffer allocations and then assert VRB and TXE.
	 */
	out_be32(utl_base + PEUTL_OUTTR,   0x08000000);
	out_be32(utl_base + PEUTL_INTR,    0x02000000);
	out_be32(utl_base + PEUTL_OPDBSZ,  0x10000000);
	out_be32(utl_base + PEUTL_PBBSZ,   0x53000000);
	out_be32(utl_base + PEUTL_IPHBSZ,  0x08000000);
	out_be32(utl_base + PEUTL_IPDBSZ,  0x10000000);
	out_be32(utl_base + PEUTL_RCIRQEN, 0x00f00000);
	out_be32(utl_base + PEUTL_PCTL,    0x80800066);

	iounmap(utl_base);

	/*
	 * We map PCI Express configuration access into the 512MB regions
	 *     PCIE0: 0xc_4000_0000
	 *     PCIE1: 0xc_8000_0000
	 *     PCIE2: 0xc_c000_0000
	 */
	switch (port) {
	case 0:
		mtdcr(DCRN_PEGPL_CFGBAH(PCIE0), 0x0000000c);
		mtdcr(DCRN_PEGPL_CFGBAL(PCIE0), 0x40000000);
		mtdcr(DCRN_PEGPL_CFGMSK(PCIE0), 0xe0000001); /* 512MB region, valid */
		break;

	case 1:
		mtdcr(DCRN_PEGPL_CFGBAH(PCIE1), 0x0000000c);
		mtdcr(DCRN_PEGPL_CFGBAL(PCIE1), 0x80000000);
		mtdcr(DCRN_PEGPL_CFGMSK(PCIE1), 0xe0000001); /* 512MB region, valid */
		break;

	case 2:
		mtdcr(DCRN_PEGPL_CFGBAH(PCIE2), 0x0000000c);
		mtdcr(DCRN_PEGPL_CFGBAL(PCIE2), 0xc0000000);
		mtdcr(DCRN_PEGPL_CFGMSK(PCIE2), 0xe0000001); /* 512MB region, valid */
		break;
	}

	/*
	 * Check for VC0 active and assert RDY.
	 */
	switch (port) {
	case 0:
		if (!(SDR_READ(PESDR0_RCSSTS) & (1 << 16)))
			printk(KERN_WARNING "PCIE0: VC0 not active\n");
		SDR_WRITE(PESDR0_RCSSET, SDR_READ(PESDR0_RCSSET) | 1 << 20);
		break;
	case 1:
		if (!(SDR_READ(PESDR1_RCSSTS) & (1 << 16)))
			printk(KERN_WARNING "PCIE0: VC0 not active\n");
		SDR_WRITE(PESDR1_RCSSET, SDR_READ(PESDR1_RCSSET) | 1 << 20);
		break;
	case 2:
		if (!(SDR_READ(PESDR2_RCSSTS) & (1 << 16)))
			printk(KERN_WARNING "PCIE0: VC0 not active\n");
		SDR_WRITE(PESDR2_RCSSET, SDR_READ(PESDR2_RCSSET) | 1 << 20);
		break;
	}

#if 0
	/* Dump all config regs */
	for (i = 0x300; i <= 0x320; ++i)
		printk("[%04x] 0x%08x\n", i, SDR_READ(i));
	for (i = 0x340; i <= 0x353; ++i)
		printk("[%04x] 0x%08x\n", i, SDR_READ(i));
	for (i = 0x370; i <= 0x383; ++i)
		printk("[%04x] 0x%08x\n", i, SDR_READ(i));
	for (i = 0x3a0; i <= 0x3a2; ++i)
		printk("[%04x] 0x%08x\n", i, SDR_READ(i));
	for (i = 0x3c0; i <= 0x3c3; ++i)
		printk("[%04x] 0x%08x\n", i, SDR_READ(i));
#endif

	mdelay(100);

	return 0;
}

void ppc440spe_setup_pcie(struct pci_controller *hose, int port)
{
	void __iomem *mbase;

	/*
	 * Map 16MB, which is enough for 4 bits of bus #
	 */
	hose->cfg_data = ioremap64(0xc40000000ull + port * 0x40000000,
				   1 << 24);
	hose->ops = &pcie_pci_ops;

	/*
	 * Set bus numbers on our root port
	 */
	mbase = ioremap64(0xc50000000ull + port * 0x40000000, 4096);
	out_8(mbase + PCI_PRIMARY_BUS, 0);
	out_8(mbase + PCI_SECONDARY_BUS, 0);

	/*
	 * Set up outbound translation to hose->mem_space from PLB
	 * addresses at an offset of 0xd_0000_0000.  We set the low
	 * bits of the mask to 11 to turn off splitting into 8
	 * subregions and to enable the outbound translation.
	 */
	out_le32(mbase + PECFG_POM0LAH, 0);
	out_le32(mbase + PECFG_POM0LAL, hose->mem_space.start);

	switch (port) {
	case 0:
		mtdcr(DCRN_PEGPL_OMR1BAH(PCIE0),  0x0000000d);
		mtdcr(DCRN_PEGPL_OMR1BAL(PCIE0),  hose->mem_space.start);
		mtdcr(DCRN_PEGPL_OMR1MSKH(PCIE0), 0x7fffffff);
		mtdcr(DCRN_PEGPL_OMR1MSKL(PCIE0),
		      ~(hose->mem_space.end - hose->mem_space.start) | 3);
		break;
	case 1:
		mtdcr(DCRN_PEGPL_OMR1BAH(PCIE1),  0x0000000d);
		mtdcr(DCRN_PEGPL_OMR1BAL(PCIE1),  hose->mem_space.start);
		mtdcr(DCRN_PEGPL_OMR1MSKH(PCIE1), 0x7fffffff);
		mtdcr(DCRN_PEGPL_OMR1MSKL(PCIE1),
		      ~(hose->mem_space.end - hose->mem_space.start) | 3);

		break;
	case 2:
		mtdcr(DCRN_PEGPL_OMR1BAH(PCIE2),  0x0000000d);
		mtdcr(DCRN_PEGPL_OMR1BAL(PCIE2),  hose->mem_space.start);
		mtdcr(DCRN_PEGPL_OMR1MSKH(PCIE2), 0x7fffffff);
		mtdcr(DCRN_PEGPL_OMR1MSKL(PCIE2),
		      ~(hose->mem_space.end - hose->mem_space.start) | 3);
		break;
	}

	/* Set up 16GB inbound memory window at 0 */
	out_le32(mbase + PCI_BASE_ADDRESS_0, 0);
	out_le32(mbase + PCI_BASE_ADDRESS_1, 0);
	out_le32(mbase + PECFG_BAR0HMPA, 0x7fffffc);
	out_le32(mbase + PECFG_BAR0LMPA, 0);
	out_le32(mbase + PECFG_PIM0LAL, 0);
	out_le32(mbase + PECFG_PIM0LAH, 0);
	out_le32(mbase + PECFG_PIMEN, 0x1);

	/* Enable I/O, Mem, and Busmaster cycles */
	out_le16(mbase + PCI_COMMAND,
		 in_le16(mbase + PCI_COMMAND) |
		 PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

	iounmap(mbase);
}
