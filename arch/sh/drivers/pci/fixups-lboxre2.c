/*
 * linux/arch/sh/drivers/pci/ops-lboxre2.c
 *
 * Copyright (C) 2007 Nobuhiro Iwamatsu
 *
 * PCI initialization for the NTT COMWARE L-BOX RE2
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <mach/lboxre2.h>
#include "pci-sh4.h"

static char lboxre2_irq_tab[] __initdata = {
	IRQ_ETH0, IRQ_ETH1, IRQ_INTA, IRQ_INTD,
};

int __init pcibios_map_platform_irq(struct pci_dev *pdev, u8 slot, u8 pin)
{
	return lboxre2_irq_tab[slot];
}
