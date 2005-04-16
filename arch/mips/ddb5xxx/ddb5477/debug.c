/***********************************************************************
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * arch/mips/ddb5xxx/ddb5477/debug.c
 *     vrc5477 specific debug routines.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 ***********************************************************************
 */

#include <linux/kernel.h>

#include <asm/mipsregs.h>
#include <asm/ddb5xxx/ddb5xxx.h>

typedef struct {
       const char *regname;
       unsigned regaddr;
} Register;

void jsun_show_regs(char *name, Register *regs)
{
	int i;

	printk("\nshow regs: %s\n", name);
	for(i=0;regs[i].regname!= NULL; i++) {
		printk("%-16s= %08x\t\t(@%08x)\n",
		       regs[i].regname,
		       *(unsigned *)(regs[i].regaddr),
		       regs[i].regaddr);
	}
}

static Register int_regs[] = {
	{"DDB_INTCTRL0", DDB_BASE + DDB_INTCTRL0},
	{"DDB_INTCTRL1", DDB_BASE + DDB_INTCTRL1},
	{"DDB_INTCTRL2", DDB_BASE + DDB_INTCTRL2},
	{"DDB_INTCTRL3", DDB_BASE + DDB_INTCTRL3},
	{"DDB_INT0STAT", DDB_BASE + DDB_INT0STAT},
	{"DDB_INT1STAT", DDB_BASE + DDB_INT1STAT},
	{"DDB_INT2STAT", DDB_BASE + DDB_INT2STAT},
	{"DDB_INT3STAT", DDB_BASE + DDB_INT3STAT},
	{"DDB_INT4STAT", DDB_BASE + DDB_INT4STAT},
	{"DDB_NMISTAT", DDB_BASE + DDB_NMISTAT},
	{"DDB_INTPPES0", DDB_BASE + DDB_INTPPES0},
	{"DDB_INTPPES1", DDB_BASE + DDB_INTPPES1},
	{NULL, 0x0}
};

void vrc5477_show_int_regs()
{
	jsun_show_regs("interrupt registers", int_regs);
	printk("CPU CAUSE = %08x\n", read_c0_cause());
	printk("CPU STATUS = %08x\n", read_c0_status());
}
static Register pdar_regs[] = {
        {"DDB_SDRAM0", DDB_BASE + DDB_SDRAM0},
        {"DDB_SDRAM1", DDB_BASE + DDB_SDRAM1},
        {"DDB_LCS0", DDB_BASE + DDB_LCS0},
        {"DDB_LCS1", DDB_BASE + DDB_LCS1},
        {"DDB_LCS2", DDB_BASE + DDB_LCS2},
        {"DDB_INTCS", DDB_BASE + DDB_INTCS},
        {"DDB_BOOTCS", DDB_BASE + DDB_BOOTCS},
        {"DDB_PCIW0", DDB_BASE + DDB_PCIW0},
        {"DDB_PCIW1", DDB_BASE + DDB_PCIW1},
        {"DDB_IOPCIW0", DDB_BASE + DDB_IOPCIW0},
        {"DDB_IOPCIW1", DDB_BASE + DDB_IOPCIW1},
        {NULL, 0x0}
};
void vrc5477_show_pdar_regs(void)
{
        jsun_show_regs("PDAR regs", pdar_regs);
}

static Register bar_regs[] = {
        {"DDB_BARC0", DDB_BASE + DDB_BARC0},
        {"DDB_BARM010", DDB_BASE + DDB_BARM010},
        {"DDB_BARM230", DDB_BASE + DDB_BARM230},
        {"DDB_BAR00", DDB_BASE + DDB_BAR00},
        {"DDB_BAR10", DDB_BASE + DDB_BAR10},
        {"DDB_BAR20", DDB_BASE + DDB_BAR20},
        {"DDB_BAR30", DDB_BASE + DDB_BAR30},
        {"DDB_BAR40", DDB_BASE + DDB_BAR40},
        {"DDB_BAR50", DDB_BASE + DDB_BAR50},
        {"DDB_BARB0", DDB_BASE + DDB_BARB0},
        {"DDB_BARC1", DDB_BASE + DDB_BARC1},
        {"DDB_BARM011", DDB_BASE + DDB_BARM011},
        {"DDB_BARM231", DDB_BASE + DDB_BARM231},
        {"DDB_BAR01", DDB_BASE + DDB_BAR01},
        {"DDB_BAR11", DDB_BASE + DDB_BAR11},
        {"DDB_BAR21", DDB_BASE + DDB_BAR21},
        {"DDB_BAR31", DDB_BASE + DDB_BAR31},
        {"DDB_BAR41", DDB_BASE + DDB_BAR41},
        {"DDB_BAR51", DDB_BASE + DDB_BAR51},
        {"DDB_BARB1", DDB_BASE + DDB_BARB1},
        {NULL, 0x0}
};
void vrc5477_show_bar_regs(void)
{
        jsun_show_regs("BAR regs", bar_regs);
}

static Register pci_regs[] = {
        {"DDB_PCIW0", DDB_BASE + DDB_PCIW0},
        {"DDB_PCIW1", DDB_BASE + DDB_PCIW1},
        {"DDB_PCIINIT00", DDB_BASE + DDB_PCIINIT00},
        {"DDB_PCIINIT10", DDB_BASE + DDB_PCIINIT10},
        {"DDB_PCICTL0_L", DDB_BASE + DDB_PCICTL0_L},
        {"DDB_PCICTL0_H", DDB_BASE + DDB_PCICTL0_H},
        {"DDB_PCIARB0_L", DDB_BASE + DDB_PCIARB0_L},
        {"DDB_PCIARB0_H", DDB_BASE + DDB_PCIARB0_H},
        {"DDB_PCISWP0", DDB_BASE + DDB_PCISWP0},
        {"DDB_PCIERR0", DDB_BASE + DDB_PCIERR0},
        {"DDB_IOPCIW0", DDB_BASE + DDB_IOPCIW0},
        {"DDB_IOPCIW1", DDB_BASE + DDB_IOPCIW1},
        {"DDB_PCIINIT01", DDB_BASE + DDB_PCIINIT01},
        {"DDB_PCIINIT11", DDB_BASE + DDB_PCIINIT11},
        {"DDB_PCICTL1_L", DDB_BASE + DDB_PCICTL1_L},
        {"DDB_PCICTL1_H", DDB_BASE + DDB_PCICTL1_H},
        {"DDB_PCIARB1_L", DDB_BASE + DDB_PCIARB1_L},
        {"DDB_PCIARB1_H", DDB_BASE + DDB_PCIARB1_H},
        {"DDB_PCISWP1", DDB_BASE + DDB_PCISWP1},
        {"DDB_PCIERR1", DDB_BASE + DDB_PCIERR1},
        {NULL, 0x0}
};
void vrc5477_show_pci_regs(void)
{
        jsun_show_regs("PCI regs", pci_regs);
}

static Register lb_regs[] = {
        {"DDB_LCNFG", DDB_BASE + DDB_LCNFG},
        {"DDB_LCST0", DDB_BASE + DDB_LCST0},
        {"DDB_LCST1", DDB_BASE + DDB_LCST1},
        {"DDB_LCST2", DDB_BASE + DDB_LCST2},
        {"DDB_ERRADR", DDB_BASE + DDB_ERRADR},
        {"DDB_ERRCS", DDB_BASE + DDB_ERRCS},
        {"DDB_BTM", DDB_BASE + DDB_BTM},
        {"DDB_BCST", DDB_BASE + DDB_BCST},
        {NULL, 0x0}
};
void vrc5477_show_lb_regs(void)
{
        jsun_show_regs("Local Bus regs", lb_regs);
}

void vrc5477_show_all_regs(void)
{
	vrc5477_show_pdar_regs();
	vrc5477_show_pci_regs();
	vrc5477_show_bar_regs();
	vrc5477_show_int_regs();
	vrc5477_show_lb_regs();
}
