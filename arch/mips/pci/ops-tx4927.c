/*
 * Define the pci_ops for the PCIC on Toshiba TX4927, TX4938, etc.
 *
 * Based on linux/arch/mips/pci/ops-tx4938.c,
 *	    linux/arch/mips/pci/fixup-rbtx4938.c,
 *	    linux/arch/mips/txx9/rbtx4938/setup.c,
 *	    and RBTX49xx patch from CELF patch archive.
 *
 * 2003-2005 (c) MontaVista Software, Inc.
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 * (C) Copyright TOSHIBA CORPORATION 2000-2001, 2004-2007
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/txx9/pci.h>
#include <asm/txx9/tx4927pcic.h>

static struct {
	struct pci_controller *channel;
	struct tx4927_pcic_reg __iomem *pcicptr;
} pcicptrs[2];	/* TX4938 has 2 pcic */

static void __init set_tx4927_pcicptr(struct pci_controller *channel,
				      struct tx4927_pcic_reg __iomem *pcicptr)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pcicptrs); i++) {
		if (pcicptrs[i].channel == channel) {
			pcicptrs[i].pcicptr = pcicptr;
			return;
		}
	}
	for (i = 0; i < ARRAY_SIZE(pcicptrs); i++) {
		if (!pcicptrs[i].channel) {
			pcicptrs[i].channel = channel;
			pcicptrs[i].pcicptr = pcicptr;
			return;
		}
	}
	BUG();
}

struct tx4927_pcic_reg __iomem *get_tx4927_pcicptr(
	struct pci_controller *channel)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pcicptrs); i++) {
		if (pcicptrs[i].channel == channel)
			return pcicptrs[i].pcicptr;
	}
	return NULL;
}

static int mkaddr(struct pci_bus *bus, unsigned int devfn, int where,
		  struct tx4927_pcic_reg __iomem *pcicptr)
{
	if (bus->parent == NULL &&
	    devfn >= PCI_DEVFN(TX4927_PCIC_MAX_DEVNU, 0))
		return -1;
	__raw_writel(((bus->number & 0xff) << 0x10)
		     | ((devfn & 0xff) << 0x08) | (where & 0xfc)
		     | (bus->parent ? 1 : 0),
		     &pcicptr->g2pcfgadrs);
	/* clear M_ABORT and Disable M_ABORT Int. */
	__raw_writel((__raw_readl(&pcicptr->pcistatus) & 0x0000ffff)
		     | (PCI_STATUS_REC_MASTER_ABORT << 16),
		     &pcicptr->pcistatus);
	return 0;
}

static int check_abort(struct tx4927_pcic_reg __iomem *pcicptr)
{
	int code = PCIBIOS_SUCCESSFUL;

	/* wait write cycle completion before checking error status */
	while (__raw_readl(&pcicptr->pcicstatus) & TX4927_PCIC_PCICSTATUS_IWB)
		;
	if (__raw_readl(&pcicptr->pcistatus)
	    & (PCI_STATUS_REC_MASTER_ABORT << 16)) {
		__raw_writel((__raw_readl(&pcicptr->pcistatus) & 0x0000ffff)
			     | (PCI_STATUS_REC_MASTER_ABORT << 16),
			     &pcicptr->pcistatus);
		/* flush write buffer */
		iob();
		code = PCIBIOS_DEVICE_NOT_FOUND;
	}
	return code;
}

static u8 icd_readb(int offset, struct tx4927_pcic_reg __iomem *pcicptr)
{
#ifdef __BIG_ENDIAN
	offset ^= 3;
#endif
	return __raw_readb((void __iomem *)&pcicptr->g2pcfgdata + offset);
}
static u16 icd_readw(int offset, struct tx4927_pcic_reg __iomem *pcicptr)
{
#ifdef __BIG_ENDIAN
	offset ^= 2;
#endif
	return __raw_readw((void __iomem *)&pcicptr->g2pcfgdata + offset);
}
static u32 icd_readl(struct tx4927_pcic_reg __iomem *pcicptr)
{
	return __raw_readl(&pcicptr->g2pcfgdata);
}
static void icd_writeb(u8 val, int offset,
		       struct tx4927_pcic_reg __iomem *pcicptr)
{
#ifdef __BIG_ENDIAN
	offset ^= 3;
#endif
	__raw_writeb(val, (void __iomem *)&pcicptr->g2pcfgdata + offset);
}
static void icd_writew(u16 val, int offset,
		       struct tx4927_pcic_reg __iomem *pcicptr)
{
#ifdef __BIG_ENDIAN
	offset ^= 2;
#endif
	__raw_writew(val, (void __iomem *)&pcicptr->g2pcfgdata + offset);
}
static void icd_writel(u32 val, struct tx4927_pcic_reg __iomem *pcicptr)
{
	__raw_writel(val, &pcicptr->g2pcfgdata);
}

static struct tx4927_pcic_reg __iomem *pci_bus_to_pcicptr(struct pci_bus *bus)
{
	struct pci_controller *channel = bus->sysdata;
	return get_tx4927_pcicptr(channel);
}

static int tx4927_pci_config_read(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 *val)
{
	struct tx4927_pcic_reg __iomem *pcicptr = pci_bus_to_pcicptr(bus);

	if (mkaddr(bus, devfn, where, pcicptr)) {
		*val = 0xffffffff;
		return -1;
	}
	switch (size) {
	case 1:
		*val = icd_readb(where & 3, pcicptr);
		break;
	case 2:
		*val = icd_readw(where & 3, pcicptr);
		break;
	default:
		*val = icd_readl(pcicptr);
	}
	return check_abort(pcicptr);
}

static int tx4927_pci_config_write(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 val)
{
	struct tx4927_pcic_reg __iomem *pcicptr = pci_bus_to_pcicptr(bus);

	if (mkaddr(bus, devfn, where, pcicptr))
		return -1;
	switch (size) {
	case 1:
		icd_writeb(val, where & 3, pcicptr);
		break;
	case 2:
		icd_writew(val, where & 3, pcicptr);
		break;
	default:
		icd_writel(val, pcicptr);
	}
	return check_abort(pcicptr);
}

static struct pci_ops tx4927_pci_ops = {
	.read = tx4927_pci_config_read,
	.write = tx4927_pci_config_write,
};

static struct {
	u8 trdyto;
	u8 retryto;
	u16 gbwc;
} tx4927_pci_opts = {
	.trdyto = 0,
	.retryto = 0,
	.gbwc = 0xfe0,	/* 4064 GBUSCLK for CCFG.GTOT=0b11 */
};

char *tx4927_pcibios_setup(char *str)
{
	if (!strncmp(str, "trdyto=", 7)) {
		u8 val = 0;
		if (kstrtou8(str + 7, 0, &val) == 0)
			tx4927_pci_opts.trdyto = val;
		return NULL;
	}
	if (!strncmp(str, "retryto=", 8)) {
		u8 val = 0;
		if (kstrtou8(str + 8, 0, &val) == 0)
			tx4927_pci_opts.retryto = val;
		return NULL;
	}
	if (!strncmp(str, "gbwc=", 5)) {
		u16 val;
		if (kstrtou16(str + 5, 0, &val) == 0)
			tx4927_pci_opts.gbwc = val;
		return NULL;
	}
	return str;
}

void __init tx4927_pcic_setup(struct tx4927_pcic_reg __iomem *pcicptr,
			      struct pci_controller *channel, int extarb)
{
	int i;
	unsigned long flags;

	set_tx4927_pcicptr(channel, pcicptr);

	if (!channel->pci_ops)
		printk(KERN_INFO
		       "PCIC -- DID:%04x VID:%04x RID:%02x Arbiter:%s\n",
		       __raw_readl(&pcicptr->pciid) >> 16,
		       __raw_readl(&pcicptr->pciid) & 0xffff,
		       __raw_readl(&pcicptr->pciccrev) & 0xff,
			extarb ? "External" : "Internal");
	channel->pci_ops = &tx4927_pci_ops;

	local_irq_save(flags);

	/* Disable All Initiator Space */
	__raw_writel(__raw_readl(&pcicptr->pciccfg)
		     & ~(TX4927_PCIC_PCICCFG_G2PMEN(0)
			 | TX4927_PCIC_PCICCFG_G2PMEN(1)
			 | TX4927_PCIC_PCICCFG_G2PMEN(2)
			 | TX4927_PCIC_PCICCFG_G2PIOEN),
		     &pcicptr->pciccfg);

	/* GB->PCI mappings */
	__raw_writel((channel->io_resource->end - channel->io_resource->start)
		     >> 4,
		     &pcicptr->g2piomask);
	____raw_writeq((channel->io_resource->start +
			channel->io_map_base - IO_BASE) |
#ifdef __BIG_ENDIAN
		       TX4927_PCIC_G2PIOGBASE_ECHG
#else
		       TX4927_PCIC_G2PIOGBASE_BSDIS
#endif
		       , &pcicptr->g2piogbase);
	____raw_writeq(channel->io_resource->start - channel->io_offset,
		       &pcicptr->g2piopbase);
	for (i = 0; i < 3; i++) {
		__raw_writel(0, &pcicptr->g2pmmask[i]);
		____raw_writeq(0, &pcicptr->g2pmgbase[i]);
		____raw_writeq(0, &pcicptr->g2pmpbase[i]);
	}
	if (channel->mem_resource->end) {
		__raw_writel((channel->mem_resource->end
			      - channel->mem_resource->start) >> 4,
			     &pcicptr->g2pmmask[0]);
		____raw_writeq(channel->mem_resource->start |
#ifdef __BIG_ENDIAN
			       TX4927_PCIC_G2PMnGBASE_ECHG
#else
			       TX4927_PCIC_G2PMnGBASE_BSDIS
#endif
			       , &pcicptr->g2pmgbase[0]);
		____raw_writeq(channel->mem_resource->start -
			       channel->mem_offset,
			       &pcicptr->g2pmpbase[0]);
	}
	/* PCI->GB mappings (I/O 256B) */
	__raw_writel(0, &pcicptr->p2giopbase); /* 256B */
	____raw_writeq(0, &pcicptr->p2giogbase);
	/* PCI->GB mappings (MEM 512MB (64MB on R1.x)) */
	__raw_writel(0, &pcicptr->p2gm0plbase);
	__raw_writel(0, &pcicptr->p2gm0pubase);
	____raw_writeq(TX4927_PCIC_P2GMnGBASE_TMEMEN |
#ifdef __BIG_ENDIAN
		       TX4927_PCIC_P2GMnGBASE_TECHG
#else
		       TX4927_PCIC_P2GMnGBASE_TBSDIS
#endif
		       , &pcicptr->p2gmgbase[0]);
	/* PCI->GB mappings (MEM 16MB) */
	__raw_writel(0xffffffff, &pcicptr->p2gm1plbase);
	__raw_writel(0xffffffff, &pcicptr->p2gm1pubase);
	____raw_writeq(0, &pcicptr->p2gmgbase[1]);
	/* PCI->GB mappings (MEM 1MB) */
	__raw_writel(0xffffffff, &pcicptr->p2gm2pbase); /* 1MB */
	____raw_writeq(0, &pcicptr->p2gmgbase[2]);

	/* Clear all (including IRBER) except for GBWC */
	__raw_writel((tx4927_pci_opts.gbwc << 16)
		     & TX4927_PCIC_PCICCFG_GBWC_MASK,
		     &pcicptr->pciccfg);
	/* Enable Initiator Memory Space */
	if (channel->mem_resource->end)
		__raw_writel(__raw_readl(&pcicptr->pciccfg)
			     | TX4927_PCIC_PCICCFG_G2PMEN(0),
			     &pcicptr->pciccfg);
	/* Enable Initiator I/O Space */
	if (channel->io_resource->end)
		__raw_writel(__raw_readl(&pcicptr->pciccfg)
			     | TX4927_PCIC_PCICCFG_G2PIOEN,
			     &pcicptr->pciccfg);
	/* Enable Initiator Config */
	__raw_writel(__raw_readl(&pcicptr->pciccfg)
		     | TX4927_PCIC_PCICCFG_ICAEN | TX4927_PCIC_PCICCFG_TCAR,
		     &pcicptr->pciccfg);

	/* Do not use MEMMUL, MEMINF: YMFPCI card causes M_ABORT. */
	__raw_writel(0, &pcicptr->pcicfg1);

	__raw_writel((__raw_readl(&pcicptr->g2ptocnt) & ~0xffff)
		     | (tx4927_pci_opts.trdyto & 0xff)
		     | ((tx4927_pci_opts.retryto & 0xff) << 8),
		     &pcicptr->g2ptocnt);

	/* Clear All Local Bus Status */
	__raw_writel(TX4927_PCIC_PCICSTATUS_ALL, &pcicptr->pcicstatus);
	/* Enable All Local Bus Interrupts */
	__raw_writel(TX4927_PCIC_PCICSTATUS_ALL, &pcicptr->pcicmask);
	/* Clear All Initiator Status */
	__raw_writel(TX4927_PCIC_G2PSTATUS_ALL, &pcicptr->g2pstatus);
	/* Enable All Initiator Interrupts */
	__raw_writel(TX4927_PCIC_G2PSTATUS_ALL, &pcicptr->g2pmask);
	/* Clear All PCI Status Error */
	__raw_writel((__raw_readl(&pcicptr->pcistatus) & 0x0000ffff)
		     | (TX4927_PCIC_PCISTATUS_ALL << 16),
		     &pcicptr->pcistatus);
	/* Enable All PCI Status Error Interrupts */
	__raw_writel(TX4927_PCIC_PCISTATUS_ALL, &pcicptr->pcimask);

	if (!extarb) {
		/* Reset Bus Arbiter */
		__raw_writel(TX4927_PCIC_PBACFG_RPBA, &pcicptr->pbacfg);
		__raw_writel(0, &pcicptr->pbabm);
		/* Enable Bus Arbiter */
		__raw_writel(TX4927_PCIC_PBACFG_PBAEN, &pcicptr->pbacfg);
	}

	__raw_writel(PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY
		     | PCI_COMMAND_PARITY | PCI_COMMAND_SERR,
		     &pcicptr->pcistatus);
	local_irq_restore(flags);

	printk(KERN_DEBUG
	       "PCI: COMMAND=%04x,PCIMASK=%04x,"
	       "TRDYTO=%02x,RETRYTO=%02x,GBWC=%03x\n",
	       __raw_readl(&pcicptr->pcistatus) & 0xffff,
	       __raw_readl(&pcicptr->pcimask) & 0xffff,
	       __raw_readl(&pcicptr->g2ptocnt) & 0xff,
	       (__raw_readl(&pcicptr->g2ptocnt) & 0xff00) >> 8,
	       (__raw_readl(&pcicptr->pciccfg) >> 16) & 0xfff);
}

static void tx4927_report_pcic_status1(struct tx4927_pcic_reg __iomem *pcicptr)
{
	__u16 pcistatus = (__u16)(__raw_readl(&pcicptr->pcistatus) >> 16);
	__u32 g2pstatus = __raw_readl(&pcicptr->g2pstatus);
	__u32 pcicstatus = __raw_readl(&pcicptr->pcicstatus);
	static struct {
		__u32 flag;
		const char *str;
	} pcistat_tbl[] = {
		{ PCI_STATUS_DETECTED_PARITY,	"DetectedParityError" },
		{ PCI_STATUS_SIG_SYSTEM_ERROR,	"SignaledSystemError" },
		{ PCI_STATUS_REC_MASTER_ABORT,	"ReceivedMasterAbort" },
		{ PCI_STATUS_REC_TARGET_ABORT,	"ReceivedTargetAbort" },
		{ PCI_STATUS_SIG_TARGET_ABORT,	"SignaledTargetAbort" },
		{ PCI_STATUS_PARITY,	"MasterParityError" },
	}, g2pstat_tbl[] = {
		{ TX4927_PCIC_G2PSTATUS_TTOE,	"TIOE" },
		{ TX4927_PCIC_G2PSTATUS_RTOE,	"RTOE" },
	}, pcicstat_tbl[] = {
		{ TX4927_PCIC_PCICSTATUS_PME,	"PME" },
		{ TX4927_PCIC_PCICSTATUS_TLB,	"TLB" },
		{ TX4927_PCIC_PCICSTATUS_NIB,	"NIB" },
		{ TX4927_PCIC_PCICSTATUS_ZIB,	"ZIB" },
		{ TX4927_PCIC_PCICSTATUS_PERR,	"PERR" },
		{ TX4927_PCIC_PCICSTATUS_SERR,	"SERR" },
		{ TX4927_PCIC_PCICSTATUS_GBE,	"GBE" },
		{ TX4927_PCIC_PCICSTATUS_IWB,	"IWB" },
	};
	int i, cont;

	printk(KERN_ERR "");
	if (pcistatus & TX4927_PCIC_PCISTATUS_ALL) {
		printk(KERN_CONT "pcistat:%04x(", pcistatus);
		for (i = 0, cont = 0; i < ARRAY_SIZE(pcistat_tbl); i++)
			if (pcistatus & pcistat_tbl[i].flag)
				printk(KERN_CONT "%s%s",
				       cont++ ? " " : "", pcistat_tbl[i].str);
		printk(KERN_CONT ") ");
	}
	if (g2pstatus & TX4927_PCIC_G2PSTATUS_ALL) {
		printk(KERN_CONT "g2pstatus:%08x(", g2pstatus);
		for (i = 0, cont = 0; i < ARRAY_SIZE(g2pstat_tbl); i++)
			if (g2pstatus & g2pstat_tbl[i].flag)
				printk(KERN_CONT "%s%s",
				       cont++ ? " " : "", g2pstat_tbl[i].str);
		printk(KERN_CONT ") ");
	}
	if (pcicstatus & TX4927_PCIC_PCICSTATUS_ALL) {
		printk(KERN_CONT "pcicstatus:%08x(", pcicstatus);
		for (i = 0, cont = 0; i < ARRAY_SIZE(pcicstat_tbl); i++)
			if (pcicstatus & pcicstat_tbl[i].flag)
				printk(KERN_CONT "%s%s",
				       cont++ ? " " : "", pcicstat_tbl[i].str);
		printk(KERN_CONT ")");
	}
	printk(KERN_CONT "\n");
}

void tx4927_report_pcic_status(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pcicptrs); i++) {
		if (pcicptrs[i].pcicptr)
			tx4927_report_pcic_status1(pcicptrs[i].pcicptr);
	}
}

static void tx4927_dump_pcic_settings1(struct tx4927_pcic_reg __iomem *pcicptr)
{
	int i;
	__u32 __iomem *preg = (__u32 __iomem *)pcicptr;

	printk(KERN_INFO "tx4927 pcic (0x%p) settings:", pcicptr);
	for (i = 0; i < sizeof(struct tx4927_pcic_reg); i += 4, preg++) {
		if (i % 32 == 0) {
			printk(KERN_CONT "\n");
			printk(KERN_INFO "%04x:", i);
		}
		/* skip registers with side-effects */
		if (i == offsetof(struct tx4927_pcic_reg, g2pintack)
		    || i == offsetof(struct tx4927_pcic_reg, g2pspc)
		    || i == offsetof(struct tx4927_pcic_reg, g2pcfgadrs)
		    || i == offsetof(struct tx4927_pcic_reg, g2pcfgdata)) {
			printk(KERN_CONT " XXXXXXXX");
			continue;
		}
		printk(KERN_CONT " %08x", __raw_readl(preg));
	}
	printk(KERN_CONT "\n");
}

void tx4927_dump_pcic_settings(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pcicptrs); i++) {
		if (pcicptrs[i].pcicptr)
			tx4927_dump_pcic_settings1(pcicptrs[i].pcicptr);
	}
}

irqreturn_t tx4927_pcierr_interrupt(int irq, void *dev_id)
{
	struct pt_regs *regs = get_irq_regs();
	struct tx4927_pcic_reg __iomem *pcicptr =
		(struct tx4927_pcic_reg __iomem *)(unsigned long)dev_id;

	if (txx9_pci_err_action != TXX9_PCI_ERR_IGNORE) {
		printk(KERN_WARNING "PCIERR interrupt at 0x%0*lx\n",
		       (int)(2 * sizeof(unsigned long)), regs->cp0_epc);
		tx4927_report_pcic_status1(pcicptr);
	}
	if (txx9_pci_err_action != TXX9_PCI_ERR_PANIC) {
		/* clear all pci errors */
		__raw_writel((__raw_readl(&pcicptr->pcistatus) & 0x0000ffff)
			     | (TX4927_PCIC_PCISTATUS_ALL << 16),
			     &pcicptr->pcistatus);
		__raw_writel(TX4927_PCIC_G2PSTATUS_ALL, &pcicptr->g2pstatus);
		__raw_writel(TX4927_PCIC_PBASTATUS_ALL, &pcicptr->pbastatus);
		__raw_writel(TX4927_PCIC_PCICSTATUS_ALL, &pcicptr->pcicstatus);
		return IRQ_HANDLED;
	}
	console_verbose();
	tx4927_dump_pcic_settings1(pcicptr);
	panic("PCI error.");
}

#ifdef CONFIG_TOSHIBA_FPCIB0
static void tx4927_quirk_slc90e66_bridge(struct pci_dev *dev)
{
	struct tx4927_pcic_reg __iomem *pcicptr = pci_bus_to_pcicptr(dev->bus);

	if (!pcicptr)
		return;
	if (__raw_readl(&pcicptr->pbacfg) & TX4927_PCIC_PBACFG_PBAEN) {
		/* Reset Bus Arbiter */
		__raw_writel(TX4927_PCIC_PBACFG_RPBA, &pcicptr->pbacfg);
		/*
		 * swap reqBP and reqXP (raise priority of SLC90E66).
		 * SLC90E66(PCI-ISA bridge) is connected to REQ2 on
		 * PCI Backplane board.
		 */
		__raw_writel(0x72543610, &pcicptr->pbareqport);
		__raw_writel(0, &pcicptr->pbabm);
		/* Use Fixed ParkMaster (required by SLC90E66) */
		__raw_writel(TX4927_PCIC_PBACFG_FIXPA, &pcicptr->pbacfg);
		/* Enable Bus Arbiter */
		__raw_writel(TX4927_PCIC_PBACFG_FIXPA |
			     TX4927_PCIC_PBACFG_PBAEN,
			     &pcicptr->pbacfg);
		printk(KERN_INFO "PCI: Use Fixed Park Master (REQPORT %08x)\n",
		       __raw_readl(&pcicptr->pbareqport));
	}
}
#define PCI_DEVICE_ID_EFAR_SLC90E66_0 0x9460
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_EFAR, PCI_DEVICE_ID_EFAR_SLC90E66_0,
	tx4927_quirk_slc90e66_bridge);
#endif
